#!/bin/sh
# autodev/rounds/03-turtle-missions/mockups/mockups.sh -- the UX Designer's mock-ups of Turtle Quest's
# "Gems, portals and fractals" (AutoDev round 3), rendered by UIKit itself in the desktop simulator: the REAL
# app (user/Apps/turtle/main.cpp + world.h) copied and patched by turtle_mock.patch beside this (a sketch of the
# GUI plan: the gems, the pads, the jump, the tinted target, the HUD, the six cards, the 12 tools, the editor's
# panel; a rough engine for gems / portals / colours so the scenes really run), with mock packs 4 and 5
# (mkpacks.py). A throwaway: not the app, not the Developer's code, not a documentation screenshot.
#
#   sh autodev/rounds/03-turtle-missions/mockups/mockups.sh     (from anywhere; needs g++, patch, python3 + Pillow, numpy)
#     -> autodev/rounds/03-turtle-missions/mockups/*.png ; the work in $MOCK_TMP; MOCK lines (fits, widths) in $MOCK_TMP/log.txt
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
set -e
cd "$(dirname "$0")/../../../.."
D=tools/tests/desktop_sim
M=autodev/rounds/03-turtle-missions/mockups
OUT=${MOCK_TMP:-/tmp/onyx_turtle_mock}
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
# the app, patched
rm -rf "$OUT/src"; mkdir -p "$OUT/src/user/Apps/turtle"
cp user/Apps/turtle/main.cpp user/Apps/turtle/world.h "$OUT/src/user/Apps/turtle/"
patch -s -p1 -d "$OUT/src" < $M/turtle_mock.patch
$CXX -Iuser/Kits/fontkit -I$FT/include -o "$OUT/turtle" "$OUT/fakekapi.o" "$OUT/src/user/Apps/turtle/main.cpp" user/Libs/basic/bascomp.cpp \
	user/Libs/basic/basvm.cpp user/Libs/basic/basnum.cpp user/Libs/basic/basbax.cpp "$OUT/libuikit.a" "$OUT/libft.a"

export SIM_WRITES="$OUT/writes"
TQ="$SIM_WRITES/apps/turtle.app"
: > "$OUT/log.txt"
W="wait;wait;wait"
W9="$W;$W;$W;$W;$W;$W;$W;$W;$W"
# prog PACK LEVEL LANG SPEED [ini lines...]: a player's progress (all the cards seen unless the lines say)
prog () {
	pk=$1; lv=$2; lg=$3; sp=$4; shift 4
	rm -rf "$SIM_WRITES"; mkdir -p "$TQ/levels" "$SIM_WRITES/etc"
	python3 $M/mkpacks.py "$TQ/levels"
	if [ "$lg" = fr ]; then { grep -v '^language' sdcard/etc/system.ini; echo "language=fr"; } > "$SIM_WRITES/etc/system.ini"; fi
	{ echo "players = Sam"; echo "player = Sam"; echo "pack = SD:/apps/turtle.app/levels/$pk"; echo "level = $lv"; echo "speed = $sp"; echo
	  echo "[Sam]"; for k in gems-line gems-back gems-corners gems-count gem-door portal portal-chain portal-shortcut polygon-sub polygon-row hex-spiral halves; do echo "$k = 3"; done
	  echo "gems-corners = 2"; echo "portal-chain = 2"; echo "halves = 2"
	  for x in "$@"; do printf "%s\n" "$x"; done; } > "$TQ/progress.ini"
}
SEEN="seen.gems = 1|seen.teleport = 1|seen.color = 1|seen.params = 1|seen.function = 1|seen.recursion = 1|seen.variable = 1"
seen () { echo "$SEEN" | tr '|' '\n' | grep -v "seen.$1 " || true; }
sim () {	# sim DUMP "SCRIPT" [VAR=value ...]
	dump=$1; script=$2; shift 2
	env SIM_OVERLAY=$D/sd SIM_POS=8,34 "$@" SIM="$script;dump $OUT/$dump.elsm;exit" "$OUT/turtle" >>"$OUT/log.txt" 2>&1 || { echo "mockups: $dump failed ($OUT/log.txt)"; exit 1; }
}
png () { python3 $D/shot.py "$OUT/$1.elsm" "$M/$1.png" >/dev/null && echo "  $M/$1.png"; }
crop () { python3 -c "import sys; from PIL import Image; im = Image.open(sys.argv[1]); im.crop(tuple(int(v) for v in sys.argv[3].split(','))).save(sys.argv[2])" "$M/$1.png" "$M/$2.png" "$3" && rm -f "$M/$1.png" && echo "  $M/$2.png"; }
P4=4-gems-and-portals.turtle; P5=5-spirals-and-fractals.turtle
OLDIFS=$IFS
code () { printf '%s' "$1" | python3 -c "import sys; print(sys.argv[1] + '.code = ' + sys.stdin.read().replace(chr(10), '\\\\n') + '\\\\n')" "$2"; }

FIN_EN=$(code "' Gems, then the portals
REPEAT 2
  FORWARD 2
  PICK
END REPEAT
FORWARD 2
FORWARD 3
PICK
FORWARD 3
FORWARD 2
PICK
FORWARD 2
PICK
FORWARD 3
PICK
FORWARD 2" grand-finale)
FIN_FR=$(code "' Les gemmes, puis les portails
REPETER 2
  AVANCER 2
  RAMASSER
FIN REPETER
AVANCER 2
AVANCER 3
RAMASSER
AVANCER 3
AVANCER 2
RAMASSER
AVANCER 2
RAMASSER
AVANCER 3
RAMASSER
AVANCER 2" grand-finale)
ST=""; for i in 1 2 3 4 5 6; do ST="$ST;key 0x117;$W9"; done

# 1. the grand finale, stepped (F8 x 6) into the first jump: frozen two thirds through it
IFS='
'; prog $P4 9 en 6 $(seen x) "$FIN_EN"; IFS=$OLDIFS
sim tq-portals "wait;wait$ST" MOCK_FREEZE=0.78; png tq-portals
python3 -c "import sys; from PIL import Image; im = Image.open(sys.argv[1]).crop((600, 236, 992, 476)); im.resize((im.width * 2, im.height * 2), Image.LANCZOS).save(sys.argv[2])" "$M/tq-portals.png" "$M/tq-board-zoom.png" && echo "  $M/tq-board-zoom.png"
IFS='
'; prog $P4 9 en 6 $(seen x) "$FIN_EN"; IFS=$OLDIFS
sim tq-portals-after "wait;wait$ST;key 0x117;$W9;$W9"; png tq-portals-after
IFS='
'; prog $P4 9 fr 6 $(seen x) "$FIN_FR"; IFS=$OLDIFS
sim tq-portals-fr "wait;wait$ST" MOCK_FREEZE=0.78; png tq-portals-fr
# 2. a gem out of order: gem 3 picked before gem 2
IFS='
'; prog $P4 0 en 10 $(seen x) "$(code "FORWARD 2
PICK
FORWARD 4
PICK
FORWARD" gems-line)"; IFS=$OLDIFS
sim tq-gem-order "wait;wait;key 0x114;$W9;$W9"; png tq-gem-order
IFS='
'; prog $P4 0 fr 10 $(seen x) "$(code "AVANCER 2
RAMASSER
AVANCER 4
RAMASSER
AVANCER" gems-line)"; IFS=$OLDIFS
sim tq-gem-order-fr "wait;wait;key 0x114;$W9;$W9"; png tq-gem-order-fr
# 3. the six lesson cards, the first time (EN, FR), and the whole window for gems in French
for spec in gems:$P4:0 teleport:$P4:5 color:$P5:2 params:$P5:0 function:$P5:5 recursion:$P5:6; do
	c=${spec%%:*}; r=${spec#*:}; pk=${r%%:*}; lv=${r#*:}
	for lg in en fr; do
		IFS='
'; prog $pk $lv $lg 6 $(seen $c); IFS=$OLDIFS
		sim l_$c$lg "wait;wait;$W" MOCK_MEASURE=1
		png l_$c$lg; crop l_$c$lg lesson-$c-$lg 588,26,1008,654
	done
done
IFS='
'; prog $P4 0 fr 6 $(seen gems); IFS=$OLDIFS
sim tq-fr-gems "wait;wait;$W"; png tq-fr-gems
# 4. the snowflake run and won; the rainbow's colours wrong; a recursion without a stop
KOCH="SUB Koch (size, depth)
  IF depth = 0 THEN
    FORWARD size
    EXIT SUB
  END IF
  Koch size / 3, depth - 1
  LEFT 60
  Koch size / 3, depth - 1
  RIGHT 120
  Koch size / 3, depth - 1
  LEFT 60
  Koch size / 3, depth - 1
END SUB"
IFS='
'; prog $P5 9 en 10 $(seen x) "$(code "$KOCH
REPEAT 3
  Koch 12, 2
  RIGHT 120
END REPEAT" snowflake)"; IFS=$OLDIFS
sim tq-fractal "wait;wait;key 0x114;$W9;$W9;$W9"; png tq-fractal
IFS='
'; prog $P5 2 en 10 $(seen x); IFS=$OLDIFS
sim tq-rainbow-target "wait;wait;$W"; png tq-rainbow-target
IFS='
'; prog $P5 2 en 10 $(seen x) "$(code "FOR i = 1 TO 16
  COLOR i MOD 5 + 1
  FORWARD i
  RIGHT
NEXT" rainbow-spiral)"; IFS=$OLDIFS
sim tq-rainbow "wait;wait;key 0x114;$W9;$W9"; png tq-rainbow
IFS='
'; prog $P5 2 fr 10 $(seen x) "$(code "POUR i = 1 JUSQUE 16
  COULEUR i MOD 5 + 1
  AVANCER i
  DROITE
SUITE" rainbow-spiral)"; IFS=$OLDIFS
sim tq-rainbow-fr "wait;wait;key 0x114;$W9;$W9"; png tq-rainbow-fr
IFS='
'; prog $P5 7 en 10 $(seen x) "$(code "SUB Tree (size, depth)
  FORWARD size
  LEFT 30
  Tree size * 0.6, depth - 1
  RIGHT 60
  Tree size * 0.6, depth - 1
  LEFT 30
  BACK size
END SUB
Tree 6, 4" tree)"; IFS=$OLDIFS
sim tq-recursion "wait;wait;key 0x114;$W9;$W9"; png tq-recursion
# 5. the level picker: the packs' list open (pack 5 shown under it); in French
IFS='
'; prog $P5 3 en 6 $(seen x); IFS=$OLDIFS
sim tq-picker "wait;wait;$W;down 120 24;up 120 24;$W" MOCK_MEASURE=1; png tq-picker
IFS='
'; prog $P5 3 fr 6 $(seen x); IFS=$OLDIFS
sim tq-picker-fr "wait;wait;$W;down 120 24;up 120 24;$W" MOCK_MEASURE=1; png tq-picker-fr
IFS='
'; prog $P4 9 fr 6 $(seen x); IFS=$OLDIFS
sim tq-list4-fr "wait;wait;$W" MOCK_MEASURE=1; png tq-list4-fr; crop tq-list4-fr list4-fr 0,26,246,654
# 6. the editor: the grand finale (Ctrl+E), the Gem tool; the Idea list open (default and minimum size); the
#    drawing's choice open; Save refused (a lone portal, a gap in the gems)
IFS='
'; prog $P4 9 en 6 $(seen x); IFS=$OLDIFS
sim tq-editor "wait;wait;key 0x05;$W;$W" MOCK_EDIT="9:3,1" MOCK_MEASURE=1; png tq-editor
IFS='
'; prog $P4 9 fr 6 $(seen x); IFS=$OLDIFS
sim tq-editor-fr "wait;wait;key 0x05;$W;$W" MOCK_EDIT="10:7,1" MOCK_MEASURE=1; png tq-editor-fr
IFS='
'; prog $P4 9 en 6 $(seen x); IFS=$OLDIFS
sim tq-editor-idea "wait;wait;key 0x05;$W;down 120 41;up 120 41;$W"; png tq-editor-idea
IFS='
'; prog $P4 9 en 6 $(seen x); IFS=$OLDIFS
sim tq-editor-idea-min "wait;wait;key 0x05;$W;down 120 41;up 120 41;$W" MOCK_W=920 MOCK_H=600; png tq-editor-idea-min
IFS='
'; prog $P4 9 fr 6 $(seen x); IFS=$OLDIFS
sim tq-editor-idea-min-fr "wait;wait;key 0x05;$W;down 120 41;up 120 41;$W" MOCK_W=920 MOCK_H=600; png tq-editor-idea-min-fr
IFS='
'; prog $P5 2 en 6 $(seen x); IFS=$OLDIFS
sim tq-editor-draw "wait;wait;key 0x05;$W;down 175 291;up 175 291;$W" MOCK_EDIT="D2"; png tq-editor-draw
IFS='
'; prog $P4 5 en 6 $(seen x); IFS=$OLDIFS
sim tq-editor-refused "wait;wait;key 0x05;$W;$W" MOCK_EDIT="1:7,1;10:7,1;1:7,1;S"; png tq-editor-refused
IFS='
'; prog $P4 0 fr 6 $(seen x); IFS=$OLDIFS
sim tq-editor-gemgap-fr "wait;wait;key 0x05;$W;$W" MOCK_EDIT="1:5,1;S"; png tq-editor-gemgap-fr
# 7. the minimum window, 920 x 600, on the grand finale stepped (EN, FR)
IFS='
'; prog $P4 9 fr 6 $(seen x) "$FIN_FR"; IFS=$OLDIFS
sim tq-min-fr "wait;wait$ST;key 0x117;$W9;$W9" MOCK_W=920 MOCK_H=600; png tq-min-fr
grep "^MOCK" "$OUT/log.txt" | sort -u > "$OUT/measures.txt" || true
echo "mockups: done (the measures: $OUT/measures.txt)"
