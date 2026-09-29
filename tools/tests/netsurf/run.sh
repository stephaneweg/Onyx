#!/bin/sh
# tools/tests/netsurf/run.sh -- NetSurf for Onyx on the PC: build it for the host (host.mk: the
# same sources as the Pi build, on the desktop simulator's stand-in kernel), open each test page
# of pages/ in it, play the page's events (pages/<name>.sim, optional: the desktop simulator's
# steps, e.g. "down 120 200;up 120 200;key a"), then dump the window (its frame and client area)
# as a PNG: $OUT/<name>.png. Needs gcc/g++, perl, libpng + zlib (dev), python3 with Pillow +
# numpy.
#
#   sh tools/tests/netsurf/run.sh [page ...]     (names without .html; default: all the pages)
#
# OUT (default /tmp/nsbench) holds the build and the pictures; WAITS (default 60) the loop turns
# (~20 ms each, real time: SIM_SLEEP) before the events and the dump.
set -e
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
WAITS=${WAITS:-60}
mkdir -p "$OUT"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 || { echo "build failed: $OUT/build.log"; exit 1; }

pages=$*
[ -n "$pages" ] || pages=$(ls $T/pages/*.html | sed 's#.*/##; s#\.html$##')
W=$(i=0; while [ $i -lt "$WAITS" ]; do printf 'wait;'; i=$((i + 1)); done)
for p in $pages; do
	extra=""
	[ -f "$T/pages/$p.sim" ] && extra="$(grep -v '^#' "$T/pages/$p.sim" | tr '\n' ';' | sed 's/;;*/;/g');$W"
	if ! SIM_SLEEP=1 SIM_POS=0,0 SIM_ARGS="file://$PWD/$T/pages/$p.html" \
	     SIM="${W}${extra}dump $OUT/$p.elsm;exit" timeout 120 "$OUT/build/netsurf" >"$OUT/$p.log" 2>&1
	then echo "  $p: failed (see $OUT/$p.log)"; continue; fi
	python3 tools/tests/desktop_sim/shot.py "$OUT/$p.elsm" "$OUT/$p.png" >/dev/null && echo "  $OUT/$p.png"
done
