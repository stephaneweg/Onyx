#!/bin/sh
# run_notes_sim_test.sh -- the scripted checks of Notes (user/Apps/notes, AutoDev round 1) in the desktop simulator:
# the app built for the PC against the stand-in kernel (tools/tests/desktop_sim/fakekapi.cpp, UIKit, FreeType, as
# shots.sh builds it), each acceptance criterion a run through a script of events with a fresh SIM_WRITES of its own
# (seeded with the sample notes of tools/tests/desktop_sim/sd/Notes when the case wants them), then asserted on what
# the run wrote (the notes, notes.ini, config.ini, the Trash, the clipboard) and on its log (the notifications sent,
# the programs started). The clock is the simulator's (Monday 2026-09-28 12:34:00): a note made by a run is
# note-20260928-123400.txt. One line a case: "ok AC4 ...", "FAIL AC4 ..."; the dumps of a few cases are made PNGs
# in $OUT (looked at by hand: the row titles of AC 5, the box of G5, the red status of G4).
#
#   sh tools/tests/run_notes_sim_test.sh          -> "notes-sim: all N checks passed" (exit 0)
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
#
set -e
cd "$(dirname "$0")/../.."
D=tools/tests/desktop_sim
OUT=${TMPDIR:-/tmp}/onyx_notes_sim
CXX="g++ -std=gnu++17 -O1 -w -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
FT=third_party/freetype-2.14.3
rm -rf "$OUT/w" "$OUT/log"; mkdir -p "$OUT/obj" "$OUT/ft" "$OUT/w" "$OUT/log"

# ---- the building (UIKit and FreeType kept between runs: rebuilt when a source is newer) --------------------
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
# (the app's own sources without -w: no warning allowed)
g++ -std=gnu++17 -O1 -Wall -Wextra -Wno-format-truncation -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I kernel/include \
	-fno-exceptions -fno-rtti -Iuser/Kits/fontkit -I$FT/include -c user/Apps/notes/main.cpp -o "$OUT/notes_main.o" 2> "$OUT/notes.warn" \
	|| { cat "$OUT/notes.warn"; echo "notes-sim: FAIL the build"; exit 1; }
if grep -q "Apps/notes/.*warning" "$OUT/notes.warn"; then cat "$OUT/notes.warn"; echo "notes-sim: FAIL warnings in Notes"; exit 1; fi
$CXX -c user/Apps/notes/notesmodel.cpp -o "$OUT/notesmodel.o"
$CXX -o "$OUT/notes" "$OUT/fakekapi.o" "$OUT/notes_main.o" "$OUT/notesmodel.o" "$OUT/libuikit.a" "$OUT/libft.a"

# ---- the running -------------------------------------------------------------------------------------------
PASS=0; FAILS=0
ok ()   { PASS=$((PASS + 1)); echo "ok   $*"; }
bad ()  { FAILS=$((FAILS + 1)); echo "FAIL $*"; }
check () { name=$1; shift; if "$@"; then ok "$name"; else bad "$name"; fi; }
# seed CASE [notes] [last=FILE]: a fresh writes folder (with the sample notes; Notes' config.ini saying `last`)
seed () {
	W="$OUT/w/$1"; rm -rf "$W"; mkdir -p "$W"
	[ "$2" = notes ] && cp -r $D/sd/Notes "$W/Notes"
	case "$3" in last=*) mkdir -p "$W/apps/notes.app"; printf 'last = %s\n' "${3#last=}" > "$W/apps/notes.app/config.ini" ;; esac
	return 0
}
# run CASE "SCRIPT" [VAR=value ...]: Notes through the script (in the case's writes), its log in $OUT/log/CASE.log
run () {
	c=$1; s=$2; shift 2
	env SIM_WRITES="$OUT/w/$c" SIM_OVERLAY=$D/sd SIM_POS=20,44 SIM_SERVICES=notify "$@" SIM="$s" "$OUT/notes" > "$OUT/log/$c.log" 2>&1 \
		|| bad "$c: Notes ended with an error ($OUT/log/$c.log)"
	return 0
}
png () { [ -f "$OUT/$1.elsm" ] && python3 $D/shot.py "$OUT/$1.elsm" "$OUT/$1.png" > /dev/null || true; }
# typ "text": a key a step (a space is key 32)
typ () {
	s=$1; o=""
	while [ -n "$s" ]; do c=${s%"${s#?}"}; s=${s#?}; if [ "$c" = " " ]; then o="$o;key 32"; else o="$o;key $c"; fi; done
	printf '%s' "${o#;}"
}
waits () { i=0; o=""; while [ $i -lt $1 ]; do o="$o;wait"; i=$((i + 1)); done; printf '%s' "${o#;}"; }
T=3; W3="wait;wait;wait"; QUIT="quit;wait;wait"; PAUSE=$(waits 60)	# (60 turns: 1.2 s of the simulator's clock)
NEW=note-20260928-123400.txt; SHOP=note-20260928-091500.txt; WIFI=note-20260927-183012.txt; GIFT=note-20260925-200210.txt
ONYX=note-20260921-090000.txt
cfg_last () { grep -q "^last = $2\$" "$OUT/w/$1/apps/notes.app/config.ini"; }
# ini CASE FILE KEY VALUE: notes.ini's section of FILE has KEY = VALUE
ini () { awk -v s="[$2]" -v k="$3" -v v="$4" '$0 == s { in_s = 1; next } /^\[/ { in_s = 0 } in_s && $1 == k && $3 == v { f = 1 } END { exit !f }' "$OUT/w/$1/Notes/notes.ini"; }
ntxt () { ls "$OUT/w/$1/Notes/" 2> /dev/null | grep -c '\.txt$' || true; }
# (the row y of the list, client coordinates: the tool bar 44, the head 44, rows of 56 from 4 below it)
row () { echo $((44 + 44 + 4 + $1 * 56 + 26)); }

# AC 1 / AC 2 (else branch): the sample notes, newest first, the newest selected (no `last`)
seed ac1 notes; run ac1 "$W3;dump $OUT/ac1.elsm;$QUIT" && png ac1
check "AC1  list newest first, the newest (Onyx to-do) selected at start" cfg_last ac1 $ONYX
# AC 2: `last` names Shopping -> Shopping selected: Ctrl+P unpins it (it was pinned)
seed ac2 notes last=$SHOP; run ac2 "$W3;key 16;$W3;$QUIT"
check "AC2  config.ini's last selected (Shopping: Ctrl+P unpinned it)" ini ac2 $SHOP pinned 0
check "AC2  ... and still the last one" cfg_last ac2 $SHOP
# AC 3: no SD:/Notes -> an empty new note, nothing written to SD:/Notes
seed ac3; run ac3 "$W3;dump $OUT/ac3.elsm;$QUIT" && png ac3
check "AC3  no Notes folder: nothing written under SD:/Notes" test ! -e "$OUT/w/ac3/Notes"
check "AC3  ... config.ini written, no last note" cfg_last ac3 ""
# AC 4: typing then a pause -> the file, exactly, and its notes.ini section
seed ac4; run ac4 "$W3;$(typ Shopping);key 13;$(typ milk);key 13;$(typ eggs);$PAUSE;dump $OUT/ac4.elsm;exit" && png ac4
check "AC4  the note written after a pause, its exact bytes" sh -c "printf 'Shopping\nmilk\neggs' | cmp -s - '$OUT/w/ac4/Notes/$NEW'"
check "AC4  notes.ini: colour = yellow" ini ac4 $NEW colour yellow
check "AC4  notes.ini: pinned = 0" ini ac4 $NEW pinned 0
check "AC4  notes.ini: modified = 20260928123400" ini ac4 $NEW modified 20260928123400
# AC 5: the row's title follows the first line (dump looked at: "Shop" as the row's title)
seed ac5; run ac5 "$W3;$(typ Shop);wait;dump $OUT/ac5.elsm;exit" && png ac5
check "AC5  nothing written before the pause" test ! -e "$OUT/w/ac5/Notes"
# AC 6: Ctrl+N on the sample notes, then another note chosen while it is empty: no file, no row
seed ac6 notes; run ac6 "$W3;key 14;$W3;dump $OUT/ac6a.elsm;down 120 $(row 2);up 120 $(row 2);$W3;dump $OUT/ac6b.elsm;$QUIT" && png ac6a && png ac6b
check "AC6  the empty new note left: no file written" test ! -e "$OUT/w/ac6/Notes/$NEW"
check "AC6  ... the six notes only" test "$(ntxt ac6)" = 6
check "AC6  ... the note clicked selected (Shopping: row 2 once the new note is gone)" cfg_last ac6 $SHOP
# AC 7: a title changed: the file keeps its name
seed ac7 notes last=$SHOP; run ac7 "$W3;key 13;$(typ My);key 32;$PAUSE;$QUIT"
check "AC7  the title changed, the same file" sh -c "head -1 '$OUT/w/ac7/Notes/$SHOP' | grep -qx 'My Shopping'"
check "AC7  ... no other file" test "$(ntxt ac7)" = 6
# AC 8: File > Delete Note: to the Trash, the next note selected, the notification
seed ac8 notes last=$SHOP; run ac8 "$W3;menu 2;$W3;dump $OUT/ac8.elsm;$QUIT" && png ac8
check "AC8  the note's file gone from SD:/Notes" test ! -e "$OUT/w/ac8/Notes/$SHOP"
check "AC8  ... in the Trash" test -f "$OUT/w/ac8/.Trash/files/$SHOP"
check "AC8  ... its notes.ini section dropped" sh -c "! grep -q '$SHOP' '$OUT/w/ac8/Notes/notes.ini'"
check "AC8  ... the notification sent" grep -q 'sim: send notify type 1 "Notes\\0Note moved to the Trash\\0"' "$OUT/log/ac8.log"
check "AC8  ... the next note selected (Wi-Fi)" cfg_last ac8 $WIFI
# Delete (the key) in the list does the same
seed del notes last=$SHOP; run del "$W3;key 0x108;$W3;$QUIT"
check "AC8  Delete in the list: to the Trash" test -f "$OUT/w/del/.Trash/files/$SHOP"
# AC 9: Note > Green; the tool bar's blue dot
seed ac9 notes last=$SHOP; run ac9 "$W3;menu 10;$W3;dump $OUT/ac9.elsm;$QUIT" && png ac9
check "AC9  Note > Green: colour = green" ini ac9 $SHOP colour green
seed ac9b notes last=$SHOP; run ac9b "$W3;down 337 22;up 337 22;$W3;$QUIT"
check "AC9  the tool bar's blue button: colour = blue" ini ac9b $SHOP colour blue
# AC 10: Ctrl+P pins (Gift ideas, not pinned; opened by its path: AC 14 too), Ctrl+P again unpins
seed ac10 notes; run ac10 "$W3;key 16;$W3;dump $OUT/ac10.elsm;$QUIT" SIM_ARGS=SD:/Notes/$GIFT && png ac10
check "AC10 Ctrl+P: pinned = 1" ini ac10 $GIFT pinned 1
seed ac10b notes; run ac10b "$W3;key 16;$W3;key 16;$W3;$QUIT" SIM_ARGS=SD:/Notes/$GIFT
check "AC10 Ctrl+P twice: pinned = 0" ini ac10b $GIFT pinned 0
seed ac10c notes; run ac10c "$W3;down 232 22;up 232 22;$W3;$QUIT" SIM_ARGS=SD:/Notes/$GIFT
check "AC10 the tool bar's Pin: pinned = 1" ini ac10c $GIFT pinned 1
# AC 12: Edit > Copy Note puts the whole text on the clipboard; Ctrl+V in a new note pastes it
seed ac12 notes last=$SHOP; run ac12 "$W3;menu 7;wait;key 14;$W3;key 22;$PAUSE;$QUIT" SIM_CLIPFILE="$OUT/w/ac12.clip"
check "AC12 Copy Note: the note's whole text on the clipboard" cmp -s "$OUT/w/ac12.clip" $D/sd/Notes/$SHOP
check "AC12 Ctrl+V in a new note: the same text, saved" cmp -s "$OUT/w/ac12/Notes/$NEW" $D/sd/Notes/$SHOP
# AC 13: a .txt dropped on the list: a new note of its text, selected; the file unchanged
seed ac13 notes; run ac13 "$W3;drop 120 300 SD:/notes.txt;$W3;dump $OUT/ac13.elsm;$QUIT" && png ac13
check "AC13 the dropped file's text a new note" cmp -s "$OUT/w/ac13/Notes/$NEW" $D/sd/notes.txt
check "AC13 ... selected" cfg_last ac13 $NEW
check "AC13 ... the dropped file left alone" test ! -e "$OUT/w/ac13/notes.txt"
# G5: a dropped file larger than 64 KB: refused (the box), no note
seed g5 notes; head -c 70000 /dev/zero | tr '\0' 'x' > "$OUT/w/big.txt"
run g5 "$W3;copy $OUT/w/big.txt SD:/docs/server-log.txt;drop 120 300 SD:/docs/server-log.txt;$W3;dump $OUT/g5.elsm;key 13;$W3;$QUIT" && png g5
check "G5   a file over 64 KB refused: no new note" test "$(ntxt g5)" = 6
# AC 14: notes <path> opens that note
seed ac14 notes; run ac14 "$W3;$QUIT" SIM_ARGS=SD:/Notes/$GIFT
check "AC14 the argument's note selected" cfg_last ac14 $GIFT
# AC 15: the window closed right after typing: written
seed ac15; run ac15 "$W3;$(typ Hello);quit;wait"
check "AC15 closed right after typing: the note written" sh -c "printf 'Hello' | cmp -s - '$OUT/w/ac15/Notes/$NEW'"
# D3: a note emptied by the user, then left: to the Trash, silently
seed d3 notes last=$SHOP; run d3 "$W3;key 13;key 0x01;key 0x08;$W3;down 120 $(row 0);up 120 $(row 0);$W3;$QUIT"
check "D3   an emptied note left: to the Trash" test -f "$OUT/w/d3/.Trash/files/$SHOP"
check "D3   ... no notification" sh -c "! grep -q 'sim: send notify' '$OUT/log/d3.log'"
# G4 / D8: SD:/Notes read-only: the red status, nothing written; at quit the text on the clipboard + a notification
seed g4; run g4 "$W3;$(typ Lost);$PAUSE;dump $OUT/g4.elsm;$QUIT" SIM_ROFS=SD:/Notes SIM_CLIPFILE="$OUT/w/g4.clip" && png g4
check "G4   the save refused (sim: rofs)" grep -q 'sim: rofs' "$OUT/log/g4.log"
check "G4   ... nothing under SD:/Notes" test ! -e "$OUT/w/g4/Notes"
check "G4   ... at quit: the text on the clipboard" sh -c "printf 'Lost' | cmp -s - '$OUT/w/g4.clip'"
check "G4   ... and a notification" grep -q 'sim: send notify type 1 "Notes\\0Notes could not save' "$OUT/log/g4.log"
# Ctrl+E: the note saved, then opened in the Text Editor
seed ctrle notes last=$SHOP; run ctrle "$W3;key 13;$(typ X);key 0x05;$W3;$QUIT"
check "Ctrl+E the note saved first" sh -c "head -c 1 '$OUT/w/ctrle/Notes/$SHOP' | grep -q X"
check "Ctrl+E ... the Text Editor started on it" grep -q "tinypad.*SD:/Notes/$SHOP" "$OUT/log/ctrle.log"
# a note put in SD:/Notes by another program (the Text Editor, FTP) is listed at the idle rescan (~5 s); with
# SIM_STAT its date is the file's (the host's clock: the newest, row 0)
seed rescan notes; printf 'Put by hand\nfrom a PC\n' > "$OUT/w/hand.txt"
run rescan "$W3;copy $OUT/w/hand.txt SD:/Notes/hand-note.txt;$(waits 260);down 120 $(row 0);up 120 $(row 0);$W3;$QUIT" SIM_STAT=1
check "rescan: a note put by another program listed (and chosen)" cfg_last rescan hand-note.txt
# Stickies told to re-read after a save (when its service runs)
seed reload notes last=$SHOP; run reload "$W3;key 16;$W3;$QUIT" SIM_SERVICES=notify,stickies
check "the stickies service told to re-read (STK_MSG_RELOAD) after a pin" grep -q 'sim: send stickies type 2' "$OUT/log/reload.log"

# ---- step 7: Notes <-> Stickies (AC 11, G7, the single instance) -------------------------------------------
# (lx_launch starts an app whose SD:apps/<name>.app/main exists: a stand-in put in the writes)
ncfg () { mkdir -p "$W/apps/notes.app"; printf "$1" > "$W/apps/notes.app/config.ini"; }	# Notes' config.ini
mains () { for a in "$@"; do mkdir -p "$W/apps/$a.app"; : > "$W/apps/$a.app/main"; done; }
VIEW="menu 15"
AS=SD:/etc/autostart; OLDAS="$OUT/w/autostart.old"	# (the card's autostart as it was before this round)
grep -v 'run stickies\|^# Stickies: ' sdcard/etc/autostart > "$OLDAS"
asfile () { echo "$OUT/w/$1/etc/autostart"; }
# AC 11 Hide: stickies = 0 written, the quit message to the running Stickies, autostart left alone
seed ac11h notes; mains stickies; run ac11h "$W3;$VIEW;$W3;dump $OUT/ac11h.elsm;$QUIT" SIM_SERVICES=notify,stickies && png ac11h
check "AC11 Hide: stickies = 0 in config.ini" grep -q '^stickies = 0$' "$OUT/w/ac11h/apps/notes.app/config.ini"
check "AC11 ... the quit message sent to Stickies (STK_MSG_QUIT)" grep -q 'sim: send stickies type 3' "$OUT/log/ac11h.log"
check "AC11 ... autostart not written" test ! -e "$(asfile ac11h)"
# AC 11 Show (Stickies not running), on the card of before this round (no stickies line): stickies = 1,
# Stickies started, the line inserted after the agenda's (held back as it is), preload /boot still last
seed ac11s notes; mains stickies; ncfg 'stickies = 0\n'
mkdir -p "$W/etc"; cp "$OLDAS" "$W/etc/autostart"
run ac11s "$W3;$VIEW;$W3;dump $OUT/ac11s.elsm;$QUIT" SIM_SERVICES=notify && png ac11s
check "AC11 Show: stickies = 1 in config.ini" grep -q '^stickies = 1$' "$OUT/w/ac11s/apps/notes.app/config.ini"
check "AC11 ... Stickies started" grep -q 'sim: launch stickies' "$OUT/log/ac11s.log"
check "G7   Show: '#setup: run stickies' right after '#setup: run agenda'" sh -c "grep -A2 '^#setup: run agenda\$' '$(asfile ac11s)' | tail -1 | grep -qx '#setup: run stickies'"
check "G7   ... exactly one stickies line" test "$(grep -c 'run stickies' "$(asfile ac11s)")" = 1
check "G7   ... preload /boot still the last line" test "$(tail -1 "$(asfile ac11s)")" = "preload /boot"
check "G7   ... nothing else changed" sh -c "grep -v 'run stickies\|^# Stickies: ' '$(asfile ac11s)' | cmp -s - '$OLDAS'"
# Show, Hide, Show again: one line still
seed g7twice notes; mains stickies; ncfg 'stickies = 0\n'
mkdir -p "$W/etc"; cp "$OLDAS" "$W/etc/autostart"
run g7twice "$W3;$VIEW;$W3;$VIEW;$W3;$VIEW;$W3;$QUIT" SIM_SERVICES=notify
check "G7   Show / Hide / Show: exactly one stickies line" test "$(grep -c 'run stickies' "$(asfile g7twice)")" = 1
# the card as shipped now (#setup: run stickies already there), and a hand-made 'run stickies': unchanged
seed g7card notes; mains stickies; ncfg 'stickies = 0\n'
run g7card "$W3;$VIEW;$W3;dump $OUT/g7card.elsm;$QUIT" SIM_SERVICES=notify && png g7card
check "G7   the shipped card's autostart (#setup: run stickies): not written" test ! -e "$(asfile g7card)"
seed g7hand notes; mains stickies; ncfg 'stickies = 0\n'
mkdir -p "$W/etc"; printf 'run menubar\nrun stickies\npreload /boot\n' > "$W/etc/autostart"; cp "$W/etc/autostart" "$OUT/w/g7hand.as"
run g7hand "$W3;$VIEW;$W3;$QUIT" SIM_SERVICES=notify
check "G7   a 'run stickies' line there: autostart unchanged" cmp -s "$(asfile g7hand)" "$OUT/w/g7hand.as"
# Show while Stickies runs: not started a second time
seed g7run notes; mains stickies; ncfg 'stickies = 0\n'
run g7run "$W3;$VIEW;$W3;$QUIT" SIM_SERVICES=notify,stickies
check "G7   Show while Stickies runs: not started again" sh -c "! grep -q 'sim: launch stickies' '$OUT/log/g7run.log'"
# Show, the autostart read-only: Stickies shown all the same
seed g7ro notes; mains stickies; ncfg 'stickies = 0\n'
mkdir -p "$W/etc"; cp "$OLDAS" "$W/etc/autostart"
run g7ro "$W3;$VIEW;$W3;dump $OUT/g7ro.elsm;$QUIT" SIM_SERVICES=notify SIM_ROFS=SD:/etc && png g7ro
check "G7   autostart read-only: Stickies started all the same" grep -q 'sim: launch stickies' "$OUT/log/g7ro.log"
check "G7   ... the file unchanged" cmp -s "$(asfile g7ro)" "$OLDAS"
# a pin while Stickies is hidden: nothing started, autostart untouched; while shown but not running: started
seed g7pin notes last=$GIFT; mains stickies; ncfg "stickies = 0\nlast = $GIFT\n"
run g7pin "$W3;key 16;$W3;$QUIT" SIM_SERVICES=notify
check "G7   a pin, Stickies hidden: nothing started" sh -c "! grep -q 'sim: launch stickies' '$OUT/log/g7pin.log'"
check "G7   ... autostart untouched" test ! -e "$(asfile g7pin)"
seed g7pin2 notes last=$GIFT; mains stickies
run g7pin2 "$W3;key 16;$W3;$QUIT" SIM_SERVICES=notify
check "G7   a pin, Stickies shown but not running: started (no autostart)" sh -c "grep -q 'sim: launch stickies' '$OUT/log/g7pin2.log' && test ! -e '$(asfile g7pin2)'"
# one Notes at a time: a second Notes (the "notes" service found) forwards its argument, raises the first, ends
seed one notes; run one "$W3;$QUIT" SIM_SERVICES=notify,notes SIM_ARGS=SD:/Notes/$GIFT
check "AC25 a second Notes: its argument sent to the first (NOTES_MSG_OPEN)" grep -q "sim: send notes type 1 \"SD:/Notes/$GIFT\\\\0\"" "$OUT/log/one.log"
check "AC25 ... the first raised" grep -q 'sim: raise_app notes' "$OUT/log/one.log"
check "AC25 ... and no window of its own" sh -c "! grep -q 'sim: window' '$OUT/log/one.log'"
seed alone notes; run alone "$W3;$QUIT" SIM_SERVICES=-
check "AC25 no other Notes (register refused or not): Notes runs" grep -q 'sim: window Notes' "$OUT/log/alone.log"
# NOTES_MSG_OPEN received (a Stickies click, a second Notes): that note chosen
seed open notes last=$SHOP; run open "$W3;$W3;$QUIT" SIM_MBOX="1:9:SD:/Notes/$GIFT"
check "AC23 NOTES_MSG_OPEN received: that note chosen" cfg_last open $GIFT

echo
if [ $FAILS -ne 0 ]; then echo "notes-sim: $FAILS of $((PASS + FAILS)) checks FAILED"; exit 1; fi
echo "notes-sim: all $PASS checks passed"
