#!/bin/sh
# autodev/rounds/02-circuits/mockups/mockups.sh -- the UX Designer's mock-ups of Circuits (AutoDev round 2),
# rendered by UIKit itself in the desktop simulator: circuits_mock.cpp (beside this) for the window, the real
# menu bar for the menus; laid by compose.py over a flat desktop colour (MOCK_WALL="" for the Voronoi wallpaper,
# which needs the circle submodule checked out).
#
#   sh autodev/rounds/02-circuits/mockups/mockups.sh        (from the repository's root; needs g++, python3 + Pillow, numpy)
#     -> autodev/rounds/02-circuits/mockups/*.png (the pictures of 04-ux-design.md); the work in $MOCK_TMP
#
# A throwaway: not the app, not a documentation screenshot (screenshots/ is shots.sh's).
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
set -e
cd "$(dirname "$0")/../../../.."
D=tools/tests/desktop_sim
M=autodev/rounds/02-circuits/mockups
OUT=${MOCK_TMP:-/tmp/onyx_circuits_mock}
mkdir -p "$OUT/obj" "$OUT/ft" "$OUT/writes"
export SIM_WRITES="$OUT/writes"
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
FTI="-Iuser/Kits/fontkit -I$FT/include"
$CXX $FTI -o "$OUT/circuits_mock" "$OUT/fakekapi.o" $M/circuits_mock.cpp "$OUT/libuikit.a" "$OUT/libft.a" &
[ -f "$OUT/menubar" ] || $CXX $FTI -o "$OUT/menubar" "$OUT/fakekapi.o" user/Apps/menubar/main.cpp "$OUT/libuikit.a" "$OUT/libft.a" &
wait

: > "$OUT/log.txt"
W="wait;wait;wait"
sim () {	# sim APP DUMP "SCRIPT" [VAR=value ...]
	app=$1; dump=$2; script=$3; shift 3
	env SIM_OVERLAY=$D/sd "$@" SIM="$script;dump $OUT/$dump.elsm;exit" "$OUT/$app" >>"$OUT/log.txt" 2>&1 || { echo "mockups: $dump failed ($OUT/log.txt)"; exit 1; }
}
scene () { out=$1; shift; python3 $D/compose.py "$M/$out.png" "$@" ${MOCK_WALL:---flat=2F4A63} >/dev/null && echo "  $M/$out.png"; }

# Circuits' menus as the menu bar shows them (uikit::Menu's spec: M a menu, I<id>~label~shortcut, - a line)
MENU='Circuits|MGame/I0~Next Level~^N/I1~Previous Level~^P/I2~Restart Level~/-/I3~Quit~^Q/MEdit/I4~Undo~^Z/I5~Redo~^Y/-/I6~Delete~Del/I7~Clear Board~/-/I8~Copy Truth Table~/I9~Copy Circuit~/MSimulate/I10~Check~F5/-/I11~Step~F8/I12~Live~F7/I13~Reset~F9/MLevels/I14~Open Level Pack...~^O/MHelp/I15~Lesson~F1/I16~Hint~F2/-/I17~About Circuits~'
MENU_FR='Circuits|MJeu/I0~Niveau suivant~^N/I1~Niveau précédent~^P/I2~Recommencer le niveau~/-/I3~Quitter~^Q/MÉdition/I4~Annuler~^Z/I5~Rétablir~^Y/-/I6~Supprimer~Suppr/I7~Vider le plateau~/-/I8~Copier la table de vérité~/I9~Copier le circuit~/MSimulation/I10~Vérifier~F5/-/I11~Pas à pas~F8/I12~En direct~F7/I13~Au départ~F9/MNiveaux/I14~Ouvrir un recueil...~^O/MAide/I15~Leçon~F1/I16~Indice~F2/-/I17~À propos de Circuits~'

for s in main place wire step check won refused lesson empty dialog; do
	sim circuits_mock "c_$s" "$W" MOCK=$s SIM_POS=8,34
done
sim circuits_mock c_fr "$W" MOCK=main MOCK_LANG=fr SIM_POS=8,34
sim circuits_mock c_frcheck "$W" MOCK=check MOCK_LANG=fr SIM_POS=8,34
sim circuits_mock c_levels "$W" MOCK=levels SIM_POS=20,40
sim menubar bar "$W" SIM_MENU="$MENU"
sim menubar barfr "$W" SIM_MENU="$MENU_FR"
for s in main place wire step check won refused lesson empty dialog; do
	scene "circuits-$s" "$OUT/c_$s.elsm" "$OUT/bar.elsm" --crop=0,0,1024,700
done
scene circuits-fr       "$OUT/c_fr.elsm" "$OUT/barfr.elsm" --crop=0,0,1024,700
scene circuits-fr-check "$OUT/c_frcheck.elsm" "$OUT/barfr.elsm" --crop=0,0,1024,700
scene circuits-levels   "$OUT/c_levels.elsm" "$OUT/bar.elsm" --crop=0,0,300,720
# the menus open (a click on each title of the bar)
i=0
for x in ${MENU_X:-150 196 238 296 352}; do
	i=$((i + 1))
	sim menubar "menu$i" "wait;wait;down $x 15;up $x 15;wait;wait;$W" SIM_MENU="$MENU"
done
scene circuits-menu-game     "$OUT/c_main.elsm" "$OUT/menu1.elsm" --crop=0,0,560,240
scene circuits-menu-edit     "$OUT/c_main.elsm" "$OUT/menu2.elsm" --crop=0,0,560,300
scene circuits-menu-simulate "$OUT/c_main.elsm" "$OUT/menu3.elsm" --crop=0,0,560,240
scene circuits-menu-levels   "$OUT/c_main.elsm" "$OUT/menu4.elsm" --crop=0,0,560,160
scene circuits-menu-help     "$OUT/c_main.elsm" "$OUT/menu5.elsm" --crop=0,0,560,200
echo "mockups: done"
