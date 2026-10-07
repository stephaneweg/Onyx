#!/bin/sh
# run_stickies_sim_test.sh -- the scripted checks of Stickies (user/Apps/stickies, AutoDev round 1) in the desktop
# simulator: the widget built for the PC against the stand-in kernel (tools/tests/desktop_sim/fakekapi.cpp, UIKit,
# FreeType, as shots.sh builds it), each acceptance criterion a run through a script of events with a fresh
# SIM_WRITES of its own (seeded with the sample notes of tools/tests/desktop_sim/sd/Notes, or with notes made here),
# then asserted on its dump -- the cards read back from the pixels by tools/tests/notes/cards.py: how many, their
# colours, where the window is -- on what it wrote (its config.ini) and on its log (the window's flags, the
# programs started, the messages sent, its exit). One line a case: "ok AC19 ...", "FAIL AC19 ...". The dumps are
# made PNGs in $OUT (looked at by hand).
#
#   sh tools/tests/run_stickies_sim_test.sh          -> "stickies-sim: all N checks passed" (exit 0)
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
#
set -e
cd "$(dirname "$0")/../.."
D=tools/tests/desktop_sim
OUT=${TMPDIR:-/tmp}/onyx_notes_sim
CXX="g++ -std=gnu++17 -O1 -w -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
FT=third_party/freetype-2.14.3
rm -rf "$OUT/sw" "$OUT/slog"; mkdir -p "$OUT/obj" "$OUT/ft" "$OUT/sw" "$OUT/slog"

# ---- the building (UIKit and FreeType shared with run_notes_sim_test.sh: rebuilt when a source is newer) ----
if [ ! -f "$OUT/libuikit.a" ] || [ -n "$(find user/Kits/uikit -newer "$OUT/libuikit.a" -name '*.[ch]*' | head -1)" ]; then
	for f in user/Kits/uikit/*.cpp; do $CXX -c "$f" -o "$OUT/obj/$(basename "$f" .cpp).o" & done; wait
	rm -f "$OUT/libuikit.a"; ar rcs "$OUT/libuikit.a" "$OUT"/obj/*.o
fi
if [ ! -f "$OUT/libft.a" ]; then
	FT_SRC="base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c base/ftsynth.c autofit/autofit.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c"
	for f in $FT_SRC; do gcc -O2 -w -c -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' \
		-Iuser/Kits/fontkit -I$FT/include $FT/src/$f -o "$OUT/ft/$(basename $f .c).o" & done; wait
	ar rcs "$OUT/libft.a" "$OUT"/ft/*.o
fi
$CXX -c $D/fakekapi.cpp -o "$OUT/fakekapi.o"
# (the widget's own source without -w: no warning allowed)
g++ -std=gnu++17 -O1 -Wall -Wextra -Wno-format-truncation -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I kernel/include \
	-fno-exceptions -fno-rtti -Iuser/Kits/fontkit -I$FT/include -c user/Apps/stickies/main.cpp -o "$OUT/stickies_main.o" 2> "$OUT/stickies.warn" \
	|| { cat "$OUT/stickies.warn"; echo "stickies-sim: FAIL the build"; exit 1; }
if grep -q "Apps/stickies/.*warning" "$OUT/stickies.warn"; then cat "$OUT/stickies.warn"; echo "stickies-sim: FAIL warnings in Stickies"; exit 1; fi
$CXX -c user/Apps/notes/notesmodel.cpp -o "$OUT/notesmodel.o"
$CXX -o "$OUT/stickies" "$OUT/fakekapi.o" "$OUT/stickies_main.o" "$OUT/notesmodel.o" "$OUT/libuikit.a" "$OUT/libft.a"

# ---- the running -------------------------------------------------------------------------------------------
PASS=0; FAILS=0
ok ()   { PASS=$((PASS + 1)); echo "ok   $*"; }
bad ()  { FAILS=$((FAILS + 1)); echo "FAIL $*"; }
check () { name=$1; shift; if "$@"; then ok "$name"; else bad "$name"; fi; }
# seed CASE [notes]: a fresh writes folder (with the sample notes), Notes' stand-in main there (lx_launch)
seed () {
	W="$OUT/sw/$1"; rm -rf "$W"; mkdir -p "$W/apps/notes.app"; : > "$W/apps/notes.app/main"
	[ "$2" = notes ] && cp -r $D/sd/Notes "$W/Notes"
	return 0
}
# note CASE FILE COLOUR PINNED MODIFIED "TEXT": a note of its own (its notes.ini section appended)
note () {
	mkdir -p "$OUT/sw/$1/Notes"; printf "$6" > "$OUT/sw/$1/Notes/$2"
	printf '[%s]\ncolour = %s\npinned = %s\nmodified = %s\n\n' "$2" "$3" "$4" "$5" >> "$OUT/sw/$1/Notes/notes.ini"
}
# run CASE "SCRIPT" [VAR=value ...]: Stickies through the script, its log in $OUT/slog/CASE.log
run () {
	c=$1; s=$2; shift 2
	env SIM_WRITES="$OUT/sw/$c" SIM_OVERLAY=$D/sd SIM_SERVICES=- "$@" SIM="$s" "$OUT/stickies" > "$OUT/slog/$c.log" 2>&1 \
		|| bad "$c: Stickies ended with an error ($OUT/slog/$c.log)"
	return 0
}
png () { [ -f "$OUT/$1.elsm" ] && python3 $D/compose.py "$OUT/$1.png" "$OUT/$1.elsm" --crop=704,0,1024,660 > /dev/null || true; }
cards () { python3 tools/tests/notes/cards.py "$OUT/$1.elsm" "$2" "$3" "$4"; }
waits () { i=0; o=""; while [ $i -lt $1 ]; do o="$o;wait"; i=$((i + 1)); done; printf '%s' "${o#;}"; }
W3="wait;wait;wait"
SHOP=note-20260928-091500.txt; ONYX=note-20260921-090000.txt; WIFI=note-20260927-183012.txt
# (a card's place: the first at y 44, client coordinates; the window 240 wide at x 776, y 40 by default)

# AC 19: two pinned notes (yellow, green) and one not: two cards, in their colours; the window's kind (AC 27)
seed ac19
note ac19 a.txt yellow 1 20260928100000 'Shopping\nmilk, eggs, butter and a very long line that the card wraps over two lines at least\nbread\ncoffee\ntomatoes\nbasil\nbatteries\nlast line cut'
note ac19 b.txt green 1 20260927100000 'Wi-Fi\nClubHouse-5G'
note ac19 c.txt pink 0 20260928110000 'Not pinned\nnot shown'
run ac19 "$W3;dump $OUT/ac19.elsm;exit" && png ac19
check "AC19 two pinned notes: two cards, yellow then green (newest first)" test "$(cards ac19)" = "2 yellow green"
check "AC19 ... at the top right of the screen (x 776, y 40)" test "$(cards ac19 --at)" = "776 40"
check "AC27 the window: borderless, back-most, system, see-through (flags 0x33)" grep -q 'sim: window stickies flags 0x33' "$OUT/slog/ac19.log"
check "AC27 ... below the cards: wholly see-through (the clicks reach the desktop)" test "$(cards ac19 --px 120 600)" = 0xFF000000
check "AC27 ... between the header's line and a card: catches the clicks" test "$(cards ac19 --px 120 38)" != 0xFF000000
# the sample notes (three pinned of six)
seed sample notes; run sample "$W3;dump $OUT/sample.elsm;exit" && png sample
check "AC19 the sample notes: three cards, blue yellow green" test "$(cards sample)" = "3 blue yellow green"
# AC 20: eight pinned notes (short ones: they all fit): the six most recently changed
seed ac20
note ac20 o1.txt yellow 1 20260901100000 'Old one\nx'
note ac20 o2.txt yellow 1 20260902100000 'Old two\nx'
note ac20 n1.txt grey 1 20260920100000 'Six\nx'
note ac20 n2.txt purple 1 20260921100000 'Five\nx'
note ac20 n3.txt pink 1 20260922100000 'Four\nx'
note ac20 n4.txt blue 1 20260923100000 'Three\nx'
note ac20 n5.txt green 1 20260924100000 'Two\nx'
note ac20 n6.txt yellow 1 20260925100000 'One\nx'
run ac20 "$W3;dump $OUT/ac20.elsm;exit" && png ac20
check "AC20 eight pinned: the six most recent, newest first" test "$(cards ac20)" = "6 yellow green blue pink purple grey"
# ... long ones: the cards that fit, then "+N more in Notes" (looked at: ac20b.png); a click on it opens Notes
seed ac20b
for k in 1 2 3 4 5 6 7 8; do note ac20b n$k.txt blue 1 2026092${k}100000 "Note $k\none\ntwo\nthree\nfour\nfive\nsix\nseven"; done
run ac20b "$W3;dump $OUT/ac20b.elsm;down 120 522;up 120 522;$W3;exit" && png ac20b
check "AC20 eight long ones: fewer cards, the room they take (the window's height)" sh -c "n=\$(python3 tools/tests/notes/cards.py '$OUT/ac20b.elsm' | cut -d' ' -f1); test \$n -ge 3 -a \$n -lt 6"
# AC 21: nothing pinned: the hint; a click on it starts Notes
seed ac21 notes; sed -i 's/^pinned = 1/pinned = 0/' "$OUT/sw/ac21/Notes/notes.ini"
run ac21 "$W3;dump $OUT/ac21.elsm;down 120 70;up 120 70;$W3;exit" && png ac21
check "AC21 nothing pinned: no card" test "$(cards ac21)" = 0
check "AC21 ... the hint's place catches the clicks" test "$(cards ac21 --px 120 70)" != 0xFF000000
check "AC21 ... a click on it starts Notes" grep -q 'sim: launch notes' "$OUT/slog/ac21.log"
check "AC21 ... the \"+N more\" line a click opens Notes too" grep -q 'sim: launch notes' "$OUT/slog/ac20b.log"
# AC 22: stickies = 0 in Notes' config.ini: no window, exit at once
seed ac22 notes; printf 'stickies = 0\n' > "$OUT/sw/ac22/apps/notes.app/config.ini"
run ac22 "$W3;dump $OUT/ac22.elsm;exit"
check "AC22 stickies = 0: no window" sh -c "! grep -q 'sim: window' '$OUT/slog/ac22.log'"
check "AC22 ... nothing dumped (it ended before the script)" test ! -e "$OUT/ac22.elsm"
# AC 23: a click on a card -- no Notes running: Notes started with the note's path; Notes running: told and raised
seed ac23 notes; run ac23 "$W3;down 120 230;up 120 230;$W3;exit"
check "AC23 a card clicked (Shopping), no Notes: notes started on its path" grep -q "sim: exec SD:apps/notes.app/main SD:/Notes/$SHOP" "$OUT/slog/ac23.log"
seed ac23b notes; run ac23b "$W3;down 120 60;up 120 60;$W3;exit" SIM_SERVICES=notes
check "AC23 a card clicked (Onyx to-do), Notes running: NOTES_MSG_OPEN with its path" grep -q "sim: send notes type 1 \"SD:/Notes/$ONYX\\\\0\"" "$OUT/slog/ac23b.log"
check "AC23 ... Notes raised" grep -q 'sim: raise_app notes' "$OUT/slog/ac23b.log"
check "AC23 ... not started again" sh -c "! grep -q 'sim: exec\|sim: launch' '$OUT/slog/ac23b.log'"
# AC 24: the header dragged: the widget moves, its place written; the next start opens there
seed ac24 notes; run ac24 "$W3;down 120 15;move 90 20;move 60 25;up 60 25;$W3;dump $OUT/ac24.elsm;exit" SIM_CURSOR=follow
check "AC24 the header dragged by (-60, +10): config.ini x = 716, y = 50" sh -c "grep -qx 'x = 716' '$OUT/sw/ac24/apps/stickies.app/config.ini' && grep -qx 'y = 50' '$OUT/sw/ac24/apps/stickies.app/config.ini'"
check "AC24 ... no note opened by the drag" sh -c "! grep -q 'sim: exec\|sim: launch' '$OUT/slog/ac24.log'"
run ac24 "$W3;dump $OUT/ac24b.elsm;exit"
check "AC24 ... the next start opens there" test "$(cards ac24b --at)" = "716 50"
# AC 25: the "stickies" service; the quit message ends it; the reload message re-reads at once
seed ac25 notes; run ac25 "$(waits 30);dump $OUT/ac25.elsm;exit" SIM_MBOX="@20:3:9:"
check "AC25 the quit message (STK_MSG_QUIT): Stickies ends (exit 0) before the script" sh -c "grep -q 'sim: exit 0' '$OUT/slog/ac25.log' && test ! -e '$OUT/ac25.elsm'"
check "AC25 ... after its window was made (registered and running)" grep -q 'sim: window stickies' "$OUT/slog/ac25.log"
seed ac25r notes; run ac25r "$W3;copy $D/sd/notes2.ini SD:/Notes/notes.ini;$(waits 10);dump $OUT/ac25r.elsm;exit" SIM_MBOX="@16:2:9:"
check "AC25 the reload message (STK_MSG_RELOAD): the change shown at once, before the poll" test "$(cards ac25r)" = "3 green blue blue"
seed ac25n notes; run ac25n "$W3;copy $D/sd/notes2.ini SD:/Notes/notes.ini;$(waits 10);dump $OUT/ac25n.elsm;exit"
check "AC25 ... (without it, not yet: the poll is every 3 s)" test "$(cards ac25n)" = "3 blue yellow green"
# AC 26: notes.ini changed on the card (Shopping unpinned, Books pinned, Wi-Fi changed): the next poll shows it
seed ac26 notes
run ac26 "$W3;dump $OUT/ac26a.elsm;copy $D/sd/notes2.ini SD:/Notes/notes.ini;$(waits 200);dump $OUT/ac26b.elsm;exit" && png ac26a && png ac26b
check "AC26 before: blue yellow green" test "$(cards ac26a)" = "3 blue yellow green"
check "AC26 after the next poll, not restarted: green blue blue" test "$(cards ac26b)" = "3 green blue blue"
# ... a note's text changed in place (the same size): picked up as well
seed ac26t notes; sed 's/milk/MILK/' $D/sd/Notes/$SHOP > "$OUT/sw/ac26t.txt"
run ac26t "$W3;dump $OUT/ac26ta.elsm;copy $OUT/sw/ac26t.txt SD:/Notes/$SHOP;$(waits 200);dump $OUT/ac26tb.elsm;exit"
check "AC26 a note's text changed in place: the card redrawn" sh -c "! cmp -s '$OUT/ac26ta.elsm' '$OUT/ac26tb.elsm'"
# Notes' View > Hide while Stickies runs without the message (stickies = 0 written): it ends at its next poll
seed hide notes
printf 'stickies = 0\n' > "$OUT/sw/hide0.ini"
run hide "$W3;copy $OUT/sw/hide0.ini SD:/apps/notes.app/config.ini;$(waits 200);dump $OUT/hide.elsm;exit"
check "stickies = 0 written while it runs: it ends at its next poll" sh -c "grep -q 'sim: exit 0' '$OUT/slog/hide.log' && test ! -e '$OUT/hide.elsm'"
# the header's ink: engraved dark ink on a light wallpaper (looked at: light.png)
seed light notes; run light "$W3;dump $OUT/light.elsm;exit" SIM_WALL=E8E4DC && python3 $D/compose.py "$OUT/light.png" "$OUT/light.elsm" --flat=E8E4DC --crop=704,0,1024,660 > /dev/null

echo
if [ $FAILS -ne 0 ]; then echo "stickies-sim: $FAILS of $((PASS + FAILS)) checks FAILED"; exit 1; fi
echo "stickies-sim: all $PASS checks passed"
