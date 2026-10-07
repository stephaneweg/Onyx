#!/bin/sh
# autodev/rounds/04-pinball/mockups/mockups.sh -- the UX Designer's mock-ups of Pinball (AutoDev round 4), rendered by
# UIKit itself in the desktop simulator (host g++, fakekapi.cpp, FreeType's DejaVu Sans, as shots.sh does): the
# throwaway program pinmock.cpp beside this, the draft tables of mktables.py (read with FileKit's fk_kv), the mock
# French catalogue and the player's tables in sd/ (SIM_OVERLAY). Not the app, not a documentation screenshot.
#
#   sh autodev/rounds/04-pinball/mockups/mockups.sh     (from anywhere; needs g++, python3 + Pillow, numpy)
#     -> autodev/rounds/04-pinball/mockups/*.png ; the work in $MOCK_TMP (default /tmp/onyx_pinball_mock)
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
set -e
cd "$(dirname "$0")/../../../.."
D=tools/tests/desktop_sim
M=autodev/rounds/04-pinball/mockups
OUT=${MOCK_TMP:-/tmp/onyx_pinball_mock}
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
python3 $M/mktables.py $M/sd/apps/pinball.app/tables >/dev/null
# (named "pinball": the simulator's app folder SD:/apps/pinball.app -- its lang/fr.txt and tables from the overlay)
$CXX -Iuser/Kits/fontkit -I$FT/include -o "$OUT/pinball" "$OUT/fakekapi.o" $M/pinmock.cpp "$OUT/libuikit.a" "$OUT/libft.a" -lm

export SIM_WRITES="$OUT/writes"
: > "$OUT/log.txt"
lang () { rm -rf "$SIM_WRITES"; mkdir -p "$SIM_WRITES/etc"; if [ "$1" = fr ]; then { grep -v '^language' sdcard/etc/system.ini; echo "language=fr"; } > "$SIM_WRITES/etc/system.ini"; fi; }
sim () {	# sim DUMP [VAR=value ...]
	dump=$1; shift
	env SIM_OVERLAY=$M/sd:$D/sd SIM_POS=40,40 "$@" SIM="wait;wait;wait;dump $OUT/$dump.elsm;exit" "$OUT/pinball" >>"$OUT/log.txt" 2>&1 || { echo "mockups: $dump failed ($OUT/log.txt)"; exit 1; }
	python3 $D/shot.py "$OUT/$dump.elsm" "$M/$dump.png" >/dev/null && echo "  $M/$dump.png"
}
lang en
sim pb-picker MOCK_SCENE=picker MOCK_SEL=0
sim pb-picker-broken MOCK_SCENE=picker MOCK_SEL=4
sim pb-play MOCK_SCENE=play
sim pb-launch MOCK_SCENE=launch
sim pb-multiball MOCK_SCENE=multiball
sim pb-pause MOCK_SCENE=pause
sim pb-bonus MOCK_SCENE=bonus
sim pb-tilt MOCK_SCENE=tilt MOCK_PAD=1
sim pb-name MOCK_SCENE=name
sim pb-scores MOCK_SCENE=scores
sim pb-max MOCK_SCENE=play SIM_SCREEN=1280x800 SIM_POS=0,24 MOCK_W=1272 MOCK_H=740
sim pb-tables MOCK_SCENE=trio MOCK_W=776 MOCK_H=520
lang fr
sim pb-picker-fr MOCK_SCENE=picker MOCK_SEL=1
sim pb-picker-volcano-fr MOCK_SCENE=picker MOCK_SEL=2
sim pb-picker-broken-fr MOCK_SCENE=picker MOCK_SEL=4
sim pb-play-fr MOCK_SCENE=multiball MOCK_SEL=0
sim pb-pause-fr MOCK_SCENE=pause MOCK_SEL=1
sim pb-name-fr MOCK_SCENE=name
# the play screen at 2x (the inserts, the flippers, the ball)
python3 -c "import sys; from PIL import Image; im = Image.open(sys.argv[1]); im = im.crop((10, 470, 340, 700)); im.resize((im.width * 2, im.height * 2), Image.LANCZOS).save(sys.argv[2])" "$M/pb-play.png" "$M/pb-zoom.png" && echo "  $M/pb-zoom.png"
echo "mockups: done"
