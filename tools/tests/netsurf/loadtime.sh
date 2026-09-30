#!/bin/sh
# tools/tests/netsurf/loadtime.sh -- page load times on live sites in the PC NetSurf: each URL
# loaded RUNS times (a fresh process each: no cache), its "page:load" (NS_PERF: the throbber's
# start to its stop -- the page, what it fetched, its scripts) and the fetcher's connections
# ("net:" lines: HTTP/1.1 or HTTP/2, the connect + TLS time) summed up. NS_H2=0 turns HTTP/2
# off (the fetcher offers only http/1.1): the before / after of docs/06 section 22.
#
#   sh tools/tests/netsurf/loadtime.sh [url ...]
#   NS_H2=0 RUNS=3 sh tools/tests/netsurf/loadtime.sh https://www.bbc.com/
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
RUNS=${RUNS:-3}
mkdir -p "$OUT/load"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 || { echo "build failed: $OUT/build.log"; exit 1; }
[ $# -gt 0 ] || set -- https://www.bbc.com/ https://github.com/ https://en.wikipedia.org/wiki/Raspberry_Pi
W=$(i=0; while [ $i -lt "${WAITS:-1500}" ]; do printf 'wait;'; i=$((i + 1)); done)
for u in "$@"; do
	n=$(echo "$u" | sed 's#https\?://##; s#[^a-zA-Z0-9]#_#g' | cut -c1-40)
	r=1
	while [ $r -le "$RUNS" ]; do
		L="$OUT/load/$n-h2${NS_H2:-1}-$r.log"
		SIM_REALNET=1 NS_PERF=1 SIM_SCREEN=800x1000 SIM_SLEEP=1 SIM_POS=0,0 SIM_ARGS="$u" \
			SIM="${W}exit" timeout 300 "$OUT/build/netsurf" >"$L" 2>&1
		load=$(grep -a "ONYX-PERF page:load" "$L" | head -1 | awk '{print int($3 / 1000)}')
		h1=$(grep -a -c "ONYX-PERF net:connect .* http/1.1" "$L")
		h2=$(grep -a -c "ONYX-PERF net:connect .* h2" "$L")
		reqs=$(grep -a -c "ONYX-PERF net:done" "$L")
		echo "$u run $r: load ${load:-?} ms, $reqs responses, connections: $h1 HTTP/1.1, $h2 HTTP/2"
		r=$((r + 1))
	done
done
