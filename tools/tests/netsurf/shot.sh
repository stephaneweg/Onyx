#!/bin/sh
# tools/tests/netsurf/shot.sh -- one page in the PC NetSurf (as run.sh, for any URL or file):
#
#   sh tools/tests/netsurf/shot.sh <url | file> <out.png> [WxH screen] [waits]
#
# The window takes the simulated screen's size (SIM_SCREEN, default 1600x1000: a 1280 px wide
# page); waits (default 100) are ~20 ms loop turns before the dump. Builds as run.sh does
# (OUT, default /tmp/nsbench). Compare with a real browser: chrome.sh.
set -e
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
url=$1; png=$2; screen=${3:-1600x1000}; waits=${4:-100}
[ -n "$url" ] && [ -n "$png" ] || { echo "usage: shot.sh <url|file> <out.png> [WxH] [waits]"; exit 1; }
case "$url" in *://*) ;; *) url="file://$(realpath "$url")";; esac
mkdir -p "$OUT"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 || { echo "build failed: $OUT/build.log"; exit 1; }
W=$(i=0; while [ $i -lt "$waits" ]; do printf 'wait;'; i=$((i + 1)); done)
SIM_SCREEN=$screen SIM_SLEEP=1 SIM_POS=0,0 SIM_ARGS="$url" SIM="${W}dump $OUT/shot.elsm;exit" \
	timeout 300 "$OUT/build/netsurf" >"$OUT/shot.log" 2>&1 || { echo "failed: $OUT/shot.log"; exit 1; }
python3 tools/tests/desktop_sim/shot.py "$OUT/shot.elsm" "$png" >/dev/null && echo "$png"
