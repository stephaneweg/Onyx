#!/bin/sh
# test-webcore.sh -- WebCore on the posixsim bench (qemu on the PC): wctest (tools/webkit/wctest.cpp,
# built by BENCH=1 tools/webkit/build-wctest.sh) loads tools/webkit/tests/page1.html, lays it out,
# paints it with Skia on the CPU into a PNG; then the checks --
#   what wctest prints: the title, the body's text (fonts found, the script run by JavaScriptCore);
#   the picture's pixels (checkpng.py): CSS boxes, flexbox, grid, a gradient, an inline SVG, a
#   canvas drawn by the script, and ink where the text and the form controls are.
# The picture stays in $POSIXSIM_ROOT/RAM/page1.png (to look at). PASS / FAIL lines; the exit
# status is the number of failures. The bench is not the Pi: the Pi is the reference.
#
#   BENCH=1 sh tools/webkit/build-wctest.sh && sh tools/webkit/test-webcore.sh
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see fetch.sh).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ONYX=$(cd "$HERE/../.." && pwd)
: "${POSIXSIM_ROOT:=$HOME/posixsim}"
: "${POSIXSIM_QEMU:=$(command -v qemu-aarch64-static || command -v qemu-aarch64)}"
export POSIXSIM_ROOT POSIXSIM_QEMU
R=$POSIXSIM_ROOT
[ -x "$R/SD/bin/wctest" ] || { echo "test-webcore.sh: no $R/SD/bin/wctest: BENCH=1 sh tools/webkit/build-wctest.sh" >&2; exit 2; }

mkdir -p "$R/SD/tmp" "$R/RAM" "$R/SD/res/fonts"
cp "$HERE/tests/page1.html" "$R/SD/tmp/page1.html"
cp "$ONYX"/sdcard/res/fonts/*.ttf "$R/SD/res/fonts/"
rm -f "$R/RAM/page1.png"

fails=0
ok () { echo "PASS  $1"; }
ko () { echo "FAIL  $1 ($2)"; fails=$((fails + 1)); }

start=$(date +%s)
out=$(POSIXSIM_ARGV0="SD:/bin/wctest" timeout "${TIMEOUT:-300}" "$POSIXSIM_QEMU" "$R/SD/bin/wctest" SD:/tmp/page1.html RAM:/page1.png 800 600 2>&1)
st=$?
echo "$out" | sed 's/^/  | /'
echo "wctest: exit $st in $(( $(date +%s) - start )) s"
[ $st -eq 0 ] && ok "wctest ran" || ko "wctest ran" "exit $st"
case "$out" in *"title: Onyx WebCore test"*) ok "the title";; *) ko "the title" "not found";; esac
case "$out" in *"Hello from WebCore on Onyx"*) ok "the text (layout, innerText)";; *) ko "the text" "not found";; esac
case "$out" in *"script: ran, 6 * 7 = 42"*) ok "the script changed the DOM (JavaScriptCore in WebCore)";; *) ko "the script" "not run";; esac
case "$out" in *"été, Ελληνικά, Кириллица"*) ok "UTF-8 text";; *) ko "UTF-8 text" "not found";; esac

if [ -f "$R/RAM/page1.png" ]; then
	# (x,y = r,g,b) the red, green, blue boxes; flexbox thirds; grid 1fr 2fr; the SVG's square and
	# circle; the canvas's square and disc; the gradient's middle; ink in the heading, the
	# paragraphs and the form controls
	python3 "$HERE/tests/checkpng.py" "$R/RAM/page1.png" \
		50,50=255,0,0 150,50=0,128,0 250,50=0,0,255 \
		50,125=10,20,30 150,125=40,50,60 250,125=70,80,90 \
		50,175=200,100,0 200,175=0,100,200 \
		405,5=0,255,255 450,50=128,0,128 \
		405,115=255,128,0 450,160=0,0,0 \
		675,50=255,128,128 \
		ink=10,220,400,40 ink=10,265,500,100 ink=10,400,500,40
	fails=$((fails + $?))
else
	ko "the picture" "no $R/RAM/page1.png"
fi
echo "test-webcore.sh: $fails failed"
exit $fails
