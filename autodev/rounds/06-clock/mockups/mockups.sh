#!/bin/sh
# autodev/rounds/06-clock/mockups/mockups.sh -- the UX Designer's mock-ups of the Clock (AutoDev round 6), rendered by
# UIKit itself in the desktop simulator (host g++, fakekapi.cpp, FreeType's DejaVu Sans, as shots.sh does): the
# throwaway program clkmock.cpp beside this, the mock French catalogue in sd/ (SIM_OVERLAY). Not the app, not a
# documentation screenshot. (The shape of round 5's mockups/mockups.sh.)
#
#   sh autodev/rounds/06-clock/mockups/mockups.sh     (from anywhere; needs g++, python3 + Pillow)
#     -> autodev/rounds/06-clock/mockups/*.png ; the work in $MOCK_TMP (default /tmp/onyx_clock_mock)
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
set -e
cd "$(dirname "$0")/../../../.."
D=tools/tests/desktop_sim
M=autodev/rounds/06-clock/mockups
OUT=${MOCK_TMP:-/tmp/onyx_clock_mock}
mkdir -p "$OUT/obj" "$OUT/ft"
CXX="g++ -std=gnu++17 -O1 -w -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
FT=third_party/freetype-2.14.3
if [ ! -f "$OUT/libuikit.a" ]; then
	for f in user/Kits/uikit/*.cpp; do $CXX -c "$f" -o "$OUT/obj/$(basename "$f" .cpp).o" & done; wait
	ar rcs "$OUT/libuikit.a" "$OUT"/obj/*.o
	$CXX -c $D/fakekapi.cpp -o "$OUT/fakekapi.o"
	FT_SRC="base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c base/ftsynth.c autofit/autofit.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c"
	for f in $FT_SRC; do gcc -O2 -w -c -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' \
		-Iuser/Kits/fontkit -I$FT/include $FT/src/$f -o "$OUT/ft/$(basename $f .c).o" & done; wait
	ar rcs "$OUT/libft.a" "$OUT"/ft/*.o
fi
# (named "clock": the simulator's app folder SD:/apps/clock.app -- its lang/fr.txt from the overlay)
$CXX -Iuser/Kits/fontkit -I$FT/include -o "$OUT/clock" "$OUT/fakekapi.o" $M/clkmock.cpp "$OUT/libuikit.a" "$OUT/libft.a" -lm

export SIM_WRITES="$OUT/writes"
: > "$OUT/log.txt"
lang () { rm -rf "$SIM_WRITES"; mkdir -p "$SIM_WRITES/etc"; if [ "$1" = fr ]; then { grep -v '^language' sdcard/etc/system.ini; echo "language=fr"; } > "$SIM_WRITES/etc/system.ini"; fi; }
sim () {	# sim PICTURE SCENE
	env SIM_OVERLAY=$M/sd:$D/sd SIM_POS=40,40 MOCK_SCENE=$2 SIM="wait;wait;wait;dump $OUT/$1.elsm;exit" "$OUT/clock" >>"$OUT/log.txt" 2>&1 || { echo "mockups: $1 failed ($OUT/log.txt)"; exit 1; }
	python3 $D/shot.py "$OUT/$1.elsm" "$M/$1.png" >/dev/null && echo "  $M/$1.png"
}
S="world world-late world-empty world-nozone cities alarms alarms-empty alarms-states edit ring ring-nosound timer timer-paused timesup stopwatch stopwatch-zero stopwatch-copied"
lang en
for s in $S; do sim clk-$s $s; done
lang fr
for s in world world-nozone cities alarms alarms-empty edit ring ring-nosound timer timesup stopwatch; do sim clk-$s-fr $s; done
echo "mockups: done"
