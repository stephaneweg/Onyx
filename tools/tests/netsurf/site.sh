#!/bin/sh
# tools/tests/netsurf/site.sh -- a live site (http or https) in the PC NetSurf, and the same
# page in Chromium with NetSurf's User-Agent, to compare:
#
#   sh tools/tests/netsurf/site.sh <url> <name> [waits] [WxH screen]
#
# Writes $OUT/<name>.png (NetSurf), $OUT/<name>-chrome.png (Chromium, the same page size) and
# $OUT/<name>.log (NS_JSDEBUG + NS_PERF: the scripts' errors, console.log, timings). The PC's
# real sockets (SIM_REALNET=1) and OpenSSL (host_stubs.c) give NetSurf https. EXTRA=<sim
# commands> run after the first dump (then dumped again as <name>-2.png); NOCHROME=1 skips
# Chromium. Chromium needs the machine's CA in its NSS store (~/.pki/nssdb) behind a TLS proxy.
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
url=$1; n=$2; waits=${3:-300}; scr=${4:-800x1000}
[ -n "$url" ] && [ -n "$n" ] || { echo "usage: site.sh <url> <name> [waits] [WxH]"; exit 1; }
mkdir -p "$OUT"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 || { echo "build failed: $OUT/build.log"; exit 1; }
W=$(i=0; while [ $i -lt "$waits" ]; do printf 'wait;'; i=$((i + 1)); done)
S="${W}dump $OUT/$n.elsm;"
[ -n "$EXTRA" ] && S="$S$EXTRA;dump $OUT/$n-2.elsm;"
SIM_REALNET=1 NS_JSDEBUG=1 NS_PERF=1 SIM_SCREEN=$scr SIM_SLEEP=1 SIM_POS=0,0 SIM_ARGS="$url" \
	SIM="${S}exit" timeout "${TMO:-300}" "$OUT/build/netsurf" >"$OUT/$n.log" 2>&1
echo "netsurf: exit $? ($OUT/$n.log)"
python3 tools/tests/desktop_sim/shot.py "$OUT/$n.elsm" "$OUT/$n.png" >/dev/null && echo "$OUT/$n.png"
[ -n "$EXTRA" ] && python3 tools/tests/desktop_sim/shot.py "$OUT/$n-2.elsm" "$OUT/$n-2.png" >/dev/null && echo "$OUT/$n-2.png"
if [ -z "$NOCHROME" ]; then
	# the page area of the NetSurf window: the screen less the frame, toolbar and scroll bars
	w=${scr%x*}; h=${scr#*x}
	UA=$(sed -n '/return "Mozilla/,/;/p' third_party/netsurf/utils/useragent.c | grep -o '"[^"]*"' | tr -d '"' | tr -d '\n')
	CHROME=${CHROME:-$(ls /opt/pw-browsers/chromium_headless_shell-*/chrome-linux/headless_shell 2>/dev/null | head -1)}
	[ -x "$CHROME" ] && timeout 90 "$CHROME" --headless --no-sandbox --hide-scrollbars --user-agent="$UA" \
		--window-size="$((w - 80)),$((h - 150))" --screenshot="$(realpath -m "$OUT/$n-chrome.png")" "$url" >/dev/null 2>&1 &&
		echo "$OUT/$n-chrome.png"
fi
