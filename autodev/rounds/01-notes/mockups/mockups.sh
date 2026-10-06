#!/bin/sh
# autodev/rounds/01-notes/mockups/mockups.sh -- the UX Designer's mock-ups of Notes and Stickies (AutoDev round 1),
# rendered by UIKit itself in the desktop simulator: notes_mock.cpp (beside this) for the window and the widget,
# the real menu bar, agenda and dock for the desktop around them; laid over the wallpaper by compose.py.
#
#   sh autodev/rounds/01-notes/mockups/mockups.sh        (from the repository's root; needs g++, python3 + Pillow, numpy)
#     -> autodev/rounds/01-notes/mockups/*.png (the pictures of 04-ux-design.md); the work in /tmp/onyx_notes_mock
#
# A throwaway: not the app, not a documentation screenshot (screenshots/ is shots.sh's).
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
set -e
cd "$(dirname "$0")/../../../.."
D=tools/tests/desktop_sim
M=autodev/rounds/01-notes/mockups
OUT=${MOCK_TMP:-/tmp/onyx_notes_mock}
mkdir -p "$OUT/obj" "$OUT/ft" "$OUT/writes"
export SIM_WRITES="$OUT/writes"
CXX="g++ -std=gnu++17 -O1 -w -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
if [ ! -f "$OUT/libuikit.a" ]; then
	for f in user/Kits/uikit/*.cpp; do $CXX -c "$f" -o "$OUT/obj/$(basename "$f" .cpp).o" & done; wait
	ar rcs "$OUT/libuikit.a" "$OUT"/obj/*.o
	$CXX -c $D/fakekapi.cpp -o "$OUT/fakekapi.o"
	FT=third_party/freetype-2.14.3
	FT_SRC="base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c base/ftsynth.c autofit/autofit.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c"
	for f in $FT_SRC; do gcc -O2 -w -c -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' \
		-Iuser/Kits/fontkit -I$FT/include $FT/src/$f -o "$OUT/ft/$(basename $f .c).o" & done; wait
	ar rcs "$OUT/libft.a" "$OUT"/ft/*.o
fi
FT=third_party/freetype-2.14.3
FTI="-Iuser/Kits/fontkit -I$FT/include"
$CXX $FTI -o "$OUT/notes_mock" "$OUT/fakekapi.o" $M/notes_mock.cpp "$OUT/libuikit.a" "$OUT/libft.a" &
[ -f "$OUT/menubar" ] || $CXX $FTI -o "$OUT/menubar" "$OUT/fakekapi.o" user/Apps/menubar/main.cpp "$OUT/libuikit.a" "$OUT/libft.a" &
[ -f "$OUT/agenda" ] || $CXX -o "$OUT/agenda" "$OUT/fakekapi.o" user/Apps/agenda/main.cpp "$OUT/libuikit.a" &
[ -f "$OUT/dock" ] || $CXX -o "$OUT/dock" "$OUT/fakekapi.o" user/Apps/dock/main.cpp "$OUT/libuikit.a" &
wait

: > "$OUT/log.txt"
W="wait;wait;wait"
sim () {	# sim APP DUMP "SCRIPT" [VAR=value ...]
	app=$1; dump=$2; script=$3; shift 3
	env SIM_OVERLAY=$D/sd "$@" SIM="$script;dump $OUT/$dump.elsm;exit" "$OUT/$app" >>"$OUT/log.txt" 2>&1 || { echo "mockups: $dump failed ($OUT/log.txt)"; exit 1; }
}
scene () { out=$1; shift; python3 $D/compose.py "$M/$out.png" "$@" >/dev/null && echo "  $M/$out.png"; }

# Notes' menus as the menu bar shows them (uikit::Menu's spec: M a menu, I<id>~label~shortcut, - a line)
MENU='Notes|MFile/I0~New Note~^N/-/I1~Open in Text Editor~^E/-/I3~Delete Note~^D/MEdit/I4~Cut~^X/I5~Copy~^C/I6~Paste~^V/-/I7~Select All~^A/I8~Copy Note~/MNote/I9~Unpin from Desktop~^P/-/I10~Yellow~/I11~Green~/I12~Blue~/I13~Pink~/I14~Purple~/I15~Grey~/MView/I17~Hide Stickies from the Desktop~'

# the window (over a flat grey, cropped to it with its frame) -- the scenes of notes_mock.cpp
for s in window empty search error dialog; do
	sim notes_mock "n_$s" "$W" MOCK=$s SIM_POS=20,44
done
sim menubar bar "$W" SIM_MENU="$MENU"
scene notes-window   "$OUT/n_window.elsm" "$OUT/bar.elsm" --crop=0,0,800,580
scene notes-empty    "$OUT/n_empty.elsm"  "$OUT/bar.elsm" --crop=0,0,800,580
scene notes-search   "$OUT/n_search.elsm" "$OUT/bar.elsm" --crop=0,0,800,580
scene notes-error    "$OUT/n_error.elsm"  "$OUT/bar.elsm" --crop=0,0,800,580
scene notes-dialog   "$OUT/n_dialog.elsm" "$OUT/bar.elsm" --crop=0,0,800,580
# the menus open (a click on each title of the bar)
i=0
for x in ${MENU_X:-137 177 221 268}; do
	i=$((i + 1))
	sim menubar "menu$i" "wait;wait;down $x 15;up $x 15;wait;wait;$W" SIM_MENU="$MENU"
done
scene notes-menu-file "$OUT/n_window.elsm" "$OUT/menu1.elsm" --crop=0,0,600,260
scene notes-menu-edit "$OUT/n_window.elsm" "$OUT/menu2.elsm" --crop=0,0,600,260
scene notes-menu-note "$OUT/n_window.elsm" "$OUT/menu3.elsm" --crop=0,0,600,340
scene notes-menu-view "$OUT/n_window.elsm" "$OUT/menu4.elsm" --crop=0,0,600,200

# Stickies on the wallpaper, top right; the desktop around it (the agenda top left, the dock, the bar)
sim notes_mock s_cards "$W" MOCK=stickies
sim notes_mock s_hover "$W" MOCK=stickies-hover
sim notes_mock s_none  "$W" MOCK=stickies-empty
sim agenda d_agenda "$W"
sim notes_mock d_notes "$W" MOCK=window SIM_POS=60,166
sim dock d_dock "wait;wait;$W" SIM_RUNNING=notes SIM_WINS="60,166,760,508,0,1"
sim menubar d_bar "$W" SIM_MENU='Terminal|'
scene stickies         "$OUT/s_cards.elsm" "$OUT/bar.elsm" --crop=704,0,1024,620
scene stickies-hover   "$OUT/s_hover.elsm" "$OUT/bar.elsm" --crop=704,0,1024,620
scene stickies-empty   "$OUT/s_none.elsm"  "$OUT/bar.elsm" --crop=704,0,1024,200
scene desktop-stickies "$OUT/d_agenda.elsm" "$OUT/s_cards.elsm" "$OUT/d_notes.elsm" "$OUT/d_dock.elsm" "$OUT/bar.elsm"
scene desktop-clean    "$OUT/d_agenda.elsm" "$OUT/s_cards.elsm" "$OUT/d_dock.elsm" "$OUT/d_bar.elsm"
echo "mockups: done"
