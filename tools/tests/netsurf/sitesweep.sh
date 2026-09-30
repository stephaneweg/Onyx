#!/bin/sh
# tools/tests/netsurf/sitesweep.sh -- live sites in the PC NetSurf (https through the PC's
# network): for each, the exit status (139: a crash) and its scripts' errors, counted. Run it
# after any change to the core, the fetcher, the parser or the scripts' glue.
#
#   sh tools/tests/netsurf/sitesweep.sh [url ...]      (default: a list of big sites)
#
# WAITS (default 400) ~20 ms turns per site, then SCROLLS (default 12) wheel notches down and 4 up
# (the composited band, docs/06 §22); logs in $OUT/sweep/<site>.log.
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
mkdir -p "$OUT/sweep"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 || { echo "build failed: $OUT/build.log"; exit 1; }
[ $# -gt 0 ] || set -- https://www.google.com/ https://m.facebook.com/ https://www.bbc.com/ \
	https://www.bbc.co.uk/ https://en.wikipedia.org/wiki/Raspberry_Pi https://www.youtube.com/ \
	https://www.amazon.fr/ https://www.reddit.com/ https://www.lemonde.fr/ https://github.com/ \
	https://www.leboncoin.fr/ https://www.yahoo.com/ https://duckduckgo.com/ \
	https://developer.mozilla.org/fr/ https://translate.google.com/
W=$(i=0; while [ $i -lt "${WAITS:-400}" ]; do printf 'wait;'; i=$((i + 1)); done)
W="$W$(i=0; while [ $i -lt "${SCROLLS:-12}" ]; do printf 'wheel 400 500 -1;wait;wait;wait;'; i=$((i + 1)); done)"
W="$W$(i=0; while [ $i -lt 4 ] && [ "${SCROLLS:-12}" -gt 0 ]; do printf 'wheel 400 500 1;wait;wait;wait;'; i=$((i + 1)); done)wait;wait;"
crashed=0
for u in "$@"; do
	n=$(echo "$u" | sed 's#https\?://##; s#[^a-zA-Z0-9]#_#g' | cut -c1-40)
	SIM_REALNET=1 NS_JSDEBUG=1 NS_PERF=1 SIM_SCREEN=800x1000 SIM_SLEEP=1 SIM_POS=0,0 SIM_ARGS="$u" \
		SIM="${W}exit" timeout 200 "$OUT/build/netsurf" >"$OUT/sweep/$n.log" 2>&1
	st=$?
	echo "== $u (exit $st)"
	[ $st -ge 128 ] && [ $st -ne 143 ] && crashed=1
	grep -E "^JS |Uncaught|console: error" "$OUT/sweep/$n.log" |
		grep -o "[A-Za-z]*Error: [^(]*\|Uncaught [^(]*\|interrupted" | cut -c1-120 |
		sort | uniq -c | sort -rn | head -8
done
[ $crashed = 0 ] && echo "no crash" || { echo "CRASHED (see the logs)"; exit 1; }
