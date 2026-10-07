#!/bin/sh
# autodev/rounds/05-critters/mockups/mockups.sh -- the UX Designer's mock-ups of Critters (AutoDev round 5), rendered by
# UIKit itself in the desktop simulator (host g++, fakekapi.cpp, FreeType's DejaVu Sans, as shots.sh does): the
# throwaway program crmock.cpp beside this, the draft levels of mklevels.py (read with FileKit's fk_kv), the mock
# French catalogue and the player's levels in sd/ (SIM_OVERLAY). Not the app, not a documentation screenshot.
#
#   sh autodev/rounds/05-critters/mockups/mockups.sh     (from anywhere; needs g++, python3 + Pillow, numpy)
#     -> autodev/rounds/05-critters/mockups/*.png ; the work in $MOCK_TMP (default /tmp/onyx_critters_mock)
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
set -e
cd "$(dirname "$0")/../../../.."
D=tools/tests/desktop_sim
M=autodev/rounds/05-critters/mockups
OUT=${MOCK_TMP:-/tmp/onyx_critters_mock}
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
python3 $M/mklevels.py $M/sd
# (named "critters": the simulator's app folder SD:/apps/critters.app -- its lang/fr.txt and levels from the overlay)
$CXX -Iuser/Kits/fontkit -I$FT/include -o "$OUT/critters" "$OUT/fakekapi.o" $M/crmock.cpp "$OUT/libuikit.a" "$OUT/libft.a" -lm

export SIM_WRITES="$OUT/writes"
: > "$OUT/log.txt"
lang () { rm -rf "$SIM_WRITES"; mkdir -p "$SIM_WRITES/etc"; if [ "$1" = fr ]; then { grep -v '^language' sdcard/etc/system.ini; echo "language=fr"; } > "$SIM_WRITES/etc/system.ini"; fi; }
sim () {	# sim DUMP [VAR=value ...]
	dump=$1; shift
	env SIM_OVERLAY=$M/sd:$D/sd SIM_POS=40,40 "$@" SIM="wait;wait;wait;dump $OUT/$dump.elsm;exit" "$OUT/critters" >>"$OUT/log.txt" 2>&1 || { echo "mockups: $dump failed ($OUT/log.txt)"; exit 1; }
	python3 $D/shot.py "$OUT/$dump.elsm" "$M/$dump.png" >/dev/null && echo "  $M/$dump.png"
}
lang en
sim cr-picker MOCK_SCENE=picker MOCK_SEL=3
sim cr-picker-locked MOCK_SCENE=picker MOCK_SEL=7
sim cr-picker-broken MOCK_SCENE=picker MOCK_SEL=13
sim cr-card MOCK_SCENE=card
sim cr-play MOCK_SCENE=play
sim cr-refuse MOCK_SCENE=refuse
sim cr-build MOCK_SCENE=build
sim cr-pause MOCK_SCENE=pause
sim cr-fast MOCK_SCENE=fast MOCK_LEVEL=7
sim cr-menu MOCK_SCENE=menu
sim cr-nuke MOCK_SCENE=nuke
sim cr-blockers MOCK_SCENE=blockers
sim cr-won MOCK_SCENE=won
sim cr-lost MOCK_SCENE=lost
sim cr-help MOCK_SCENE=help
sim cr-sheet MOCK_SCENE=sheet
lang fr
sim cr-picker-fr MOCK_SCENE=picker MOCK_SEL=2
sim cr-picker-broken-fr MOCK_SCENE=picker MOCK_SEL=13
sim cr-card-fr MOCK_SCENE=card
sim cr-play-fr MOCK_SCENE=play
sim cr-build-fr MOCK_SCENE=build
sim cr-nuke-fr MOCK_SCENE=nuke
sim cr-blockers-fr MOCK_SCENE=blockers
sim cr-won-fr MOCK_SCENE=won
sim cr-help-fr MOCK_SCENE=help
# the creatures at work, x2 (from the play screens)
python3 -c "import sys; from PIL import Image; im = Image.open(sys.argv[1]); im = im.crop((150, 110, 490, 300)); im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(sys.argv[2])" "$M/cr-play.png" "$M/cr-zoom.png" && echo "  $M/cr-zoom.png"
echo "mockups: done"
