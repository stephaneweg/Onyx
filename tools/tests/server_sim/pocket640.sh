#!/bin/sh
# tools/tests/server_sim/pocket640.sh -- the apps in POCKET mode on a 640 x 480 screen without a mouse (the Waveshare
# PocketTerm35: a D-pad that sends the arrows, A = Enter, B = Esc, L / R = Ctrl + Page Up / Down, a thumb keyboard).
# Real apps built against the pocket UIKit's wire port, run under PocketUI at 640 x 480, driven BY KEYS ONLY, their
# screens dumped to PNG pictures and compared:
#   - mail: three screens -- the accounts and their folders, the folder's messages, the message -- each with its "<"
#     bar; Enter goes in, Esc comes back one screen; writing a message;
#   - circuits, gpiolab, turtle: the side panels one at a time, nothing cut, every panel reached by the keys;
#   - the hint line (UIKit's uk_hint_line): a 30 px line at the window's foot that says the view's own keys, in Mail,
#     Circuits, Turtle Quest, GPIO Lab and Letters -- it changes with what the view is doing (a mode's name first);
#   - drawers: UIKit's rule -- a side panel without places opens and closes by L / R (the Calendar, the Icon Editor);
#   - audit: every other app with a window, captured (docs/compact-shell/real/pocketterm/AUDIT-640.md).
# Usage: sh tools/tests/server_sim/pocket640.sh [out dir] [only: mail circuits gpiolab turtle setup letters slides qbstudio paint applets pkgman 3dforge lr home stickies clipboard koton keys drawers audit]  (AUDIT_APPS="a b": those apps)
# SHOTS_PNG=<folder>: the pictures copied there too. MEDIA_SHOTS=<shots.sh's folder>: the libraries some apps need for
# the PC (mbedTLS, AudioKit, MuPDF: `sh tools/tests/desktop_sim/shots.sh mail media pdf` makes them); skipped without.
set -e
cd "$(dirname "$0")/../../.."
WANT=" $2 "
. tools/tests/server_sim/common.sh
want () { [ "$WANT" = "  " ] || case "$WANT" in *" $1 "*) return 0 ;; *) return 1 ;; esac; }
out () { png "$1" >/dev/null; if [ -n "$SHOTS_PNG" ]; then mkdir -p "$SHOTS_PNG"; cp "$OUT/$1.png" "$SHOTS_PNG/"; fi; }
langdir () { d="$OUT/writes-$1"; rm -rf "$d"; mkdir -p "$d/etc"; if [ -n "$1" ]; then { grep -v '^language' sdcard/etc/system.ini; echo "language=$1"; } > "$d/etc/system.ini"; fi; echo "$d"; }
WWW="$WW;$WW;$WW"
UP=0x100; DOWN=0x101; LEFT=0x102; RIGHT=0x103; PGUP=0x106; PGDN=0x107; F6=0x115; F10=0x119
P640="SIM_SCREEN=640x480 SIM_MODE=pocket"
# same <a> <b> <name>: two pictures are the same, pixel for pixel; differ: they are not
same () { if python3 $S/pngcmp.py "$OUT/$1.png" "$OUT/$2.png" >/dev/null; then echo "  $3: ok"; else echo "  $3: FAILED ($1.png and $2.png differ)"; FAIL=1; fi; }
differ () { if python3 $S/pngcmp.py "$OUT/$1.png" "$OUT/$2.png" >/dev/null; then echo "  $3: FAILED ($1.png and $2.png are the same)"; FAIL=1; else echo "  $3: ok"; fi; }
# barsame / bardiffer <a> <b> <name>: the top 80 px (PocketUI's band and the app's bar) of two pictures compared
bar80 () { python3 -c "from PIL import Image; import sys; a = Image.open ('$OUT/$1.png').convert ('RGB').crop ((0, 30, 640, 80)); b = Image.open ('$OUT/$2.png').convert ('RGB').crop ((0, 30, 640, 80)); sys.exit (0 if a.tobytes () == b.tobytes () else 1)"; }
barsame () { if bar80 $1 $2; then echo "  $3: ok"; else echo "  $3: FAILED (the bars of $1.png and $2.png differ)"; FAIL=1; fi; }
bardiffer () { if bar80 $1 $2; then echo "  $3: FAILED (the bars of $1.png and $2.png are the same)"; FAIL=1; else echo "  $3: ok"; fi; }
# pixel <picture> <x> <y> <rrggbb> <name>: one pixel of a picture
pixel () {
	got=$(python3 -c "from PIL import Image; im = Image.open ('$OUT/$1.png').convert ('RGB'); print ('%02x%02x%02x' % im.getpixel (($2, $3)))")
	if [ "$got" = "$4" ]; then echo "  $5: ok"; else echo "  $5: FAILED (pixel $2,$3 of $1 is $got, not $4)"; FAIL=1; fi
}
# flatcol <picture> <x> <y0> <y1> <name>: a column of pixels is of ONE colour (nothing drawn across it: nothing cut there)
# busy <picture> <x0> <y0> <x1> <y1> <name>: a rectangle is not of one colour (something is drawn there)
busy () {
	n=$(python3 -c "from PIL import Image; im = Image.open ('$OUT/$1.png').convert ('RGB').crop (($2, $3, $4, $5)); print (len (im.getcolors (100000) or [0] * 99))")
	if [ "$n" -gt 1 ]; then echo "  $6: ok"; else echo "  $6: FAILED (nothing drawn in $2,$3-$4,$5 of $1)"; FAIL=1; fi
}
# foot30: the hint line -- the screen's last 30 px; footsame / footdiffer <a> <b> <name>; hintline <picture> <name>: the
# line is there (its black ground at the right end, something written at its left)
foot30 () { python3 -c "from PIL import Image; import sys; a = Image.open ('$OUT/$1.png').convert ('RGB').crop ((0, 451, 640, 480)); b = Image.open ('$OUT/$2.png').convert ('RGB').crop ((0, 451, 640, 480)); sys.exit (0 if a.tobytes () == b.tobytes () else 1)"; }
footsame () { if foot30 $1 $2; then echo "  $3: ok"; else echo "  $3: FAILED (the hint lines of $1.png and $2.png differ)"; FAIL=1; fi; }
footdiffer () { if foot30 $1 $2; then echo "  $3: FAILED (the hint lines of $1.png and $2.png are the same)"; FAIL=1; else echo "  $3: ok"; fi; }
hintline () { pixel $1 634 466 000000 "$2: the hint line's ground"; busy $1 10 454 200 478 "$2: the hint line says keys"; }
size () {
	got=$(python3 -c "from PIL import Image; im = Image.open ('$OUT/$1.png'); print ('%dx%d' % im.size)")
	if [ "$got" = "$2" ]; then echo "  $3: ok"; else echo "  $3: FAILED ($1 is $got, not $2)"; FAIL=1; fi
}

# ---- Mail: three screens, by the keys ----------------------------------------------------------------------------------
# Needs what shots.sh builds for it (mbedTLS for the PC, mkaccounts): MEDIA_SHOTS = its folder; skipped when they are not there.
if want mail; then
	MS=${MEDIA_SHOTS:-/tmp/onyx_shots}; M=third_party/mbedtls-3.6.3
	if [ -f "$MS/libmb.a" ] && [ -x "$MS/mkaccounts" ]; then
		echo "pocket640: Mail"
		ftapp pocket mail uikit_pocket -I$M/include "$MS/libmb.a"
		MB=$(( 36000 + $$ % 500 * 4 ))
		python3 tools/tests/mail/fakemail.py --imap $MB --pop $((MB+1)) --smtp $((MB+2)) --http $((MB+3)) --demo personal --user me@example.com >"$OUT/mail-srv1.log" 2>&1 &
		MS1=$!
		python3 tools/tests/mail/fakemail.py --imap $((MB+100)) --pop $((MB+101)) --smtp $((MB+102)) --http $((MB+103)) --demo work --user steph@atelier-lumen.example >"$OUT/mail-srv2.log" 2>&1 &
		MS2=$!
		sleep 2
		ML="$OUT/mailw0"; rm -rf "$ML"; mkdir -p "$ML"
		SIM_WRITES="$ML" SIM=exit "$MS/mkaccounts" "Personal|me@example.com|Stéphane|imap|127.0.0.1|$MB|$((MB+2))|secret|#D93025|gmail" \
			"Atelier|steph@atelier-lumen.example|Stéphane|pop3|127.0.0.1|$((MB+101))|$((MB+102))|secret|#1A73E8" >>"$OUT/log.txt" 2>&1
		W40="$(printf 'wait;%.0s' $(seq 40))"
		# mrun <name> <lang> <script>: Mail at 640 x 480 pocket over a fresh copy of the accounts
		mrun () { mn=$1; mlg=$2; msc=$3
			rm -rf "$OUT/mailw"; cp -r "$ML" "$OUT/mailw"
			if [ -n "$mlg" ]; then { grep -v '^language' sdcard/etc/system.ini; echo "language=$mlg"; } > "$OUT/mailw/etc/system.ini"; fi
			run pocket_mail $mn "$W40;$W40;$W40;$msc" $P640 SIM_APPNAME=mail SIM_APP=mail SIM_WRITES="$OUT/mailw" SIM_SLEEP=1 SIM_REALNET=1 SIM_REALCLOCK=1
		}
		K () { echo "key $1;$W;"; }
		CN="mods 1;$W;key 0x107;$W;mods 0;$W"		# Ctrl + Page Down: R
		# screen 1, the accounts and their folders; Enter (A) on "All inboxes": screen 2, its messages; Down and Enter: screen
		# 3, the message; Esc (B): the list again, Esc: the folders again
		S1="expect kind fill;expect frame 0;dump $OUT/pt-mail-640-1-folders.elsm"
		S2="$S1;$(K 13)$W;dump $OUT/pt-mail-640-2-list.elsm"
		S3="$S2;$(K $DOWN)$(K 13)$W40;dump $OUT/pt-mail-640-3-message.elsm"
		S4="$S3;$(K 27)$W;dump $OUT/pt-mail-640-4-back-list.elsm"
		S5="$S4;$(K 27)$W;dump $OUT/pt-mail-640-5-back-folders.elsm"
		mrun pt-mail-640-screens "" "$S5"
		for f in 1-folders 2-list 3-message 4-back-list 5-back-folders; do out pt-mail-640-$f; size pt-mail-640-$f 640x480 "$f is the whole 640 x 480 screen"; done
		differ pt-mail-640-1-folders pt-mail-640-2-list "Enter on a folder: another screen"
		differ pt-mail-640-2-list pt-mail-640-3-message "Enter on a message: another screen"
		barsame pt-mail-640-2-list pt-mail-640-4-back-list "Esc on the message: the list's bar again"
		barsame pt-mail-640-1-folders pt-mail-640-5-back-folders "Esc on the list: the folders' bar again"
		bardiffer pt-mail-640-2-list pt-mail-640-3-message "the message's bar is not the list's"
		for f in 1-folders 2-list 3-message; do hintline pt-mail-640-$f "Mail, $f"; done
		footdiffer pt-mail-640-1-folders pt-mail-640-2-list "the list's keys are not the folders'"
		footdiffer pt-mail-640-2-list pt-mail-640-3-message "the message's keys are not the list's"
		footsame pt-mail-640-2-list pt-mail-640-4-back-list "back on the list: its keys again"
		# the folders by the keys: Down x4 (Starred, the account, its Inbox, the next folder) and Enter -- that folder's list;
		# R (Ctrl + Page Down) from the list: the next folder's list, the same screen
		F1="$(K $DOWN)$(K $DOWN)$(K $DOWN)$(K 13)$W40;dump $OUT/pt-mail-640-folder-list.elsm;$CN;$W40;dump $OUT/pt-mail-640-folder-next.elsm"
		mrun pt-mail-640-folder "" "$F1"
		out pt-mail-640-folder-list; out pt-mail-640-folder-next
		bardiffer pt-mail-640-2-list pt-mail-640-folder-list "Down, Enter: another folder's list"
		bardiffer pt-mail-640-folder-list pt-mail-640-folder-next "R from the list: the next folder's list"
		# the message: Right puts the ring on its first button, Down scrolls; writing: Ctrl+N from the folders, Esc back
		mrun pt-mail-640-ring "" "$(K 13)$(K 13)$W40;$(K $RIGHT)$(K $RIGHT)dump $OUT/pt-mail-640-message-ring.elsm;$(K $DOWN)$(K $DOWN)$(K $DOWN)dump $OUT/pt-mail-640-message-down.elsm"
		out pt-mail-640-message-ring; out pt-mail-640-message-down
		differ pt-mail-640-message-ring pt-mail-640-message-down "Down scrolls the message"
		# the reply's field: Down to the message's end, then into the field -- its keys say A sends; a word typed, A: sent (the
		# writing screen is not opened: the message's screen again, the field empty)
		D12="$(K $DOWN)$(K $DOWN)$(K $DOWN)$(K $DOWN)$(K $DOWN)$(K $DOWN)$(K $DOWN)$(K $DOWN)$(K $DOWN)$(K $DOWN)$(K $DOWN)$(K $DOWN)"
		mrun pt-mail-640-quick "" "$(K 13)$(K 13)$W40;dump $OUT/pt-mail-640-quick-0.elsm;$D12$D12$D12;dump $OUT/pt-mail-640-quick-1.elsm;key 0x4f;$W;key 0x6b;$W;dump $OUT/pt-mail-640-quick-2.elsm"
		for i in 0 1 2; do out pt-mail-640-quick-$i; done
		footdiffer pt-mail-640-quick-0 pt-mail-640-quick-1 "Down past the message's end: the reply's field, its keys"
		differ pt-mail-640-quick-1 pt-mail-640-quick-2 "typing goes into the reply's field"
		mrun pt-mail-640-write "" "key 14;$W;$W;dump $OUT/pt-mail-640-compose.elsm;key 27;$W;$W;dump $OUT/pt-mail-640-compose-back.elsm"
		out pt-mail-640-compose; out pt-mail-640-compose-back
		differ pt-mail-640-1-folders pt-mail-640-compose "Ctrl+N: the writing screen"
		barsame pt-mail-640-1-folders pt-mail-640-compose-back "Esc on an empty message: the folders again"
		# a reply from the message (Ctrl+R): the writing screen over it, Esc back to the message
		mrun pt-mail-640-reply "" "$(K 13)$(K 13)$W40;key 18;$W;$W;dump $OUT/pt-mail-640-reply.elsm"
		out pt-mail-640-reply
		# the two account dialogs, whole inside the window: "Accounts and settings" (the folders' last row: Down to it, Enter),
		# Esc closes; a Mail without account: Enter on its welcome opens the assistant
		D30="$(printf "key $DOWN;wait;%.0s" $(seq 1 30))"
		mrun pt-mail-640-settings "" "${D30}$W;$(K 13)$W;$W;dump $OUT/pt-mail-640-settings.elsm;$(K 27)$W;dump $OUT/pt-mail-640-settings-closed.elsm"
		out pt-mail-640-settings; out pt-mail-640-settings-closed
		differ pt-mail-640-1-folders pt-mail-640-settings "Down to the last row, Enter: Accounts and settings"
		footdiffer pt-mail-640-1-folders pt-mail-640-settings "the settings' box is over the window (whole: 624 x 440 in 640 x 450)"
		differ pt-mail-640-settings pt-mail-640-settings-closed "Esc (B) closes the box"
		footsame pt-mail-640-1-folders pt-mail-640-settings-closed "... the folders' keys again"
		rm -rf "$OUT/mailw-none"; mkdir -p "$OUT/mailw-none/etc"
		run pocket_mail pt-mail-640-wizard "$W40;$W40;$(K 13)$W;$W;dump $OUT/pt-mail-640-wizard.elsm;$(K 27)$W;dump $OUT/pt-mail-640-wizard-closed.elsm" $P640 SIM_APPNAME=mail SIM_APP=mail SIM_WRITES="$OUT/mailw-none" SIM_SLEEP=1 SIM_REALCLOCK=1
		out pt-mail-640-wizard; out pt-mail-640-wizard-closed
		differ pt-mail-640-wizard pt-mail-640-wizard-closed "Esc (B) cancels the assistant"
		busy pt-mail-640-wizard 30 420 620 470 "the assistant's buttons are in the window (600 x 440 in 640 x 450)"
		# L / R BY BOTH PATHS (the keys: Ctrl + Page Up / Down; a pad's buttons): from a folder's list, R then L -- the same screens
		CP="mods 1;$W;key 0x106;$W;mods 0;$W"
		mrun pt-mail-640-lr-key "" "$(K 13)$W;$CN;$W40;dump $OUT/pt-mail-640-lr-key-r.elsm;$CP;$W40;dump $OUT/pt-mail-640-lr-key-l.elsm"
		mrun pt-mail-640-lr-pad "" "$(K 13)$W;pad 32;$W;pad 0;$W40;dump $OUT/pt-mail-640-lr-pad-r.elsm;pad 16;$W;pad 0;$W40;dump $OUT/pt-mail-640-lr-pad-l.elsm"
		for f in key-r key-l pad-r pad-l; do out pt-mail-640-lr-$f; done
		barsame pt-mail-640-lr-key-r pt-mail-640-lr-pad-r "Mail: R by the keys and by the pad -- the same folder"
		barsame pt-mail-640-lr-key-l pt-mail-640-lr-pad-l "Mail: L by the keys and by the pad -- the same folder"
		bardiffer pt-mail-640-lr-key-r pt-mail-640-lr-key-l "Mail: L is not R"
		# in French
		mrun pt-mail-640-fr fr "dump $OUT/pt-mail-640-1-folders-fr.elsm;$(K 13)$W;dump $OUT/pt-mail-640-2-list-fr.elsm;$(K 13)$W40;dump $OUT/pt-mail-640-3-message-fr.elsm"
		out pt-mail-640-1-folders-fr; out pt-mail-640-2-list-fr; out pt-mail-640-3-message-fr
		kill $MS1 $MS2 2>/dev/null
	else echo "pocket640: Mail skipped (no $MS/libmb.a, $MS/mkaccounts: sh tools/tests/desktop_sim/shots.sh mail)"; fi
fi

# ---- Circuits, GPIO Lab, Turtle Quest -----------------------------------------------------------------------------------
BAS="user/Libs/basic/bascomp.cpp user/Libs/basic/basvm.cpp user/Libs/basic/basnum.cpp user/Libs/basic/basbax.cpp"
if want circuits; then
	echo "pocket640: Circuits"
	ftapp pocket circuits uikit_pocket user/Apps/circuits/circuit.cpp
	CQ () { d=$(langdir "$1"); mkdir -p "$d/apps/circuits.app"; cp $D/circuits/solving.ini "$d/apps/circuits.app/progress.ini"; echo "$d"; }
	K () { echo "key $1;$W;"; }
	LL="mods 1;$W;key $PGUP;$W;mods 0;$W;"		# Ctrl + Page Up: L
	C="$P640 SIM_APPNAME=circuits SIM_APP=circuits"
	# the board has the levels' room; L opens their drawer, Up the level before, Enter closes it
	WR=$(CQ "")
	run pocket_circuits pt-circuits-640 "$WWW;expect kind fill;expect frame 0;$W;dump $OUT/pt-circuits-640.elsm;${LL}dump $OUT/pt-circuits-640-levels.elsm;$(K $UP)$(K 13)dump $OUT/pt-circuits-640-level.elsm" $C SIM_WRITES=$WR
	for f in pt-circuits-640 pt-circuits-640-levels pt-circuits-640-level; do out $f; size $f 640x480 "$f is the whole 640 x 480 screen"; done
	differ pt-circuits-640 pt-circuits-640-levels "L opens the levels' drawer"
	differ pt-circuits-640 pt-circuits-640-level "Up in the drawer: the level before"
	differ pt-circuits-640-levels pt-circuits-640-level "Enter closes the drawer"
	hintline pt-circuits-640 "Circuits"; hintline pt-circuits-640-levels "Circuits, the drawer open"
	footdiffer pt-circuits-640 pt-circuits-640-levels "the drawer's keys are not the board's"
	footsame pt-circuits-640 pt-circuits-640-level "the drawer closed: the board's keys again"
	pixel pt-circuits-640 40 50 15171b "the level's card is a dark surface"
	# the board by the keys: the arrows bring its pointer, 1 arms the first gate, Enter puts it; Enter on it takes it, the
	# arrows move it, Enter puts it down
	WR=$(CQ "")
	run pocket_circuits pt-circuits-640-keys "$WWW;$(K $RIGHT)$(K $UP)$(K $UP)$(K $UP)$(K $UP)$(K $UP)dump $OUT/pt-circuits-640-pointer.elsm;key 0x31;$W;dump $OUT/pt-circuits-640-armed.elsm;$(K 13)dump $OUT/pt-circuits-640-put.elsm" $C SIM_WRITES=$WR
	for f in pt-circuits-640-pointer pt-circuits-640-armed pt-circuits-640-put; do out $f; done
	differ pt-circuits-640 pt-circuits-640-pointer "the arrows bring the board's pointer"
	differ pt-circuits-640-pointer pt-circuits-640-armed "1 arms a gate at the pointer"
	differ pt-circuits-640-armed pt-circuits-640-put "Enter puts the gate"
	footdiffer pt-circuits-640-pointer pt-circuits-640-armed "a gate armed: the hint line says PUT and its keys"
	footsame pt-circuits-640-armed pt-circuits-640-put "the gate put: still armed (another can be put), the same keys"
	# F8: the step mode (the line says STEP), F7 back to live; F5 checks: the result's card, its keys
	WR=$(CQ "")
	run pocket_circuits pt-circuits-640-step "$WWW;key 0x117;$W;dump $OUT/pt-circuits-640-step.elsm;key 0x116;$W;dump $OUT/pt-circuits-640-live.elsm;key 0x114;$W;dump $OUT/pt-circuits-640-check.elsm;key 0x110;$W;dump $OUT/pt-circuits-640-lesson.elsm" $C SIM_WRITES=$WR
	for f in pt-circuits-640-step pt-circuits-640-live pt-circuits-640-check pt-circuits-640-lesson; do out $f; done
	pixel pt-circuits-640-check 300 300 15171b "the result's card is a dark surface (it was white)"
	pixel pt-circuits-640-lesson 400 330 15171b "F1: the lesson's card is a dark surface (it was white)"
	footdiffer pt-circuits-640 pt-circuits-640-step "F8: the step mode's keys"
	footsame pt-circuits-640 pt-circuits-640-live "F7: live again"
	differ pt-circuits-640-live pt-circuits-640-check "F5 checks the board"
	WR=$(CQ fr)
	run pocket_circuits pt-circuits-640-fr "$WWW;dump $OUT/pt-circuits-640-fr.elsm" $C SIM_WRITES=$WR
	out pt-circuits-640-fr
fi
if want gpiolab; then
	echo "pocket640: GPIO Lab"
	mkdir -p "$OUT/glz"
	for f in adler32 crc32 deflate inflate inffast inftrees trees zutil; do gcc -O2 -w -c third_party/zlib-1.3.1/$f.c -o "$OUT/glz/$f.o"; done
	ftapp pocket gpiolab uikit_pocket -Ithird_party/zlib-1.3.1 user/Kits/gpiokit/gkcore.cpp user/Kits/filekit/fkcore.cpp "$OUT"/glz/*.o $BAS
	K () { echo "key $1;$W;"; }
	LL="mods 1;$W;key $PGUP;$W;mods 0;$W;"		# Ctrl + Page Up: L
	GL=$(printf 'wait;%.0s' $(seq 1 300))
	G="$P640 SIM_APPNAME=gpiolab SIM_APP=gpiolab"
	# the pin's panel and the chart take the width; L opens the header's drawer, Down the next pin, Enter closes it; then
	# the arrows are the focus's: Right in the modes' row changes the pin's mode
	WR=$(langdir "")
	run pocket_gpiolab pt-gpiolab-640 "${GL}expect kind fill;expect frame 0;$W;dump $OUT/pt-gpiolab-640.elsm;${LL}dump $OUT/pt-gpiolab-640-header.elsm;$(K $DOWN)dump $OUT/pt-gpiolab-640-header-down.elsm;$(K 13)dump $OUT/pt-gpiolab-640-pin.elsm;$(K $RIGHT)dump $OUT/pt-gpiolab-640-mode.elsm" $G SIM_WRITES=$WR "SIM_ARGS=--demo --tab chart"
	for f in pt-gpiolab-640 pt-gpiolab-640-header pt-gpiolab-640-header-down pt-gpiolab-640-pin pt-gpiolab-640-mode; do out $f; size $f 640x480 "$f is the whole 640 x 480 screen"; done
	differ pt-gpiolab-640 pt-gpiolab-640-header "L opens the header's drawer"
	differ pt-gpiolab-640-header pt-gpiolab-640-header-down "Down in the drawer: another pin"
	differ pt-gpiolab-640-header-down pt-gpiolab-640-pin "Enter closes the drawer"
	hintline pt-gpiolab-640 "GPIO Lab"
	footdiffer pt-gpiolab-640 pt-gpiolab-640-header "the header's keys are not the pin's"
	python3 -c "from PIL import Image; import sys; im = Image.open ('$OUT/pt-gpiolab-640.png').convert ('RGB').crop ((0, 190, 6, 300)); sys.exit (0 if len (im.getcolors (1000)) <= 2 else 1)" && echo "  no pointer: no tab on the edge: ok" || { echo "  no pointer: no tab on the edge: FAILED"; FAIL=1; }
	# L is the drawer's, R steps the window's tabs, round: Pins -> Code -> Pins (uk_segmented_nav)
	RR="mods 1;$W;key $PGDN;$W;mods 0;$W;"
	WR=$(langdir "")
	run pocket_gpiolab pt-gpiolab-640-tabs "${GL}dump $OUT/pt-gpiolab-640-tab0.elsm;${RR}dump $OUT/pt-gpiolab-640-tab1.elsm;${RR}dump $OUT/pt-gpiolab-640-tab2.elsm;${LL}dump $OUT/pt-gpiolab-640-tab3.elsm" $G SIM_WRITES=$WR "SIM_ARGS=--demo --tab chart"
	for i in 0 1 2 3; do out pt-gpiolab-640-tab$i; done
	bardiffer pt-gpiolab-640-tab0 pt-gpiolab-640-tab1 "R: Pins -> Code"
	footdiffer pt-gpiolab-640-tab0 pt-gpiolab-640-tab1 "the Code view's keys are not the pins'"
	barsame pt-gpiolab-640-tab0 pt-gpiolab-640-tab2 "R again: Code -> Pins"
	footdiffer pt-gpiolab-640-tab2 pt-gpiolab-640-tab3 "L still opens the header's drawer"
	WR=$(langdir "")
	run pocket_gpiolab pt-gpiolab-640-code "${GL}dump $OUT/pt-gpiolab-640-code.elsm" $G SIM_WRITES=$WR "SIM_ARGS=--code --run"
	out pt-gpiolab-640-code
	WR=$(langdir fr)
	run pocket_gpiolab pt-gpiolab-640-fr "${GL}dump $OUT/pt-gpiolab-640-fr.elsm" $G SIM_WRITES=$WR "SIM_ARGS=--demo --tab i2c"
	out pt-gpiolab-640-fr
fi
if want turtle; then
	echo "pocket640: Turtle Quest"
	ftapp pocket turtle uikit_pocket $BAS
	TQ () { d=$(langdir "$1"); mkdir -p "$d/apps/turtle.app"; cp $D/turtle/$2.ini "$d/apps/turtle.app/progress.ini"; echo "$d"; }
	K () { echo "key $1;$W;"; }
	LL="mods 1;$W;key $PGUP;$W;mods 0;$W;"		# Ctrl + Page Up: L
	T="$P640 SIM_APPNAME=turtle SIM_APP=turtle"
	# the program and the board share the window; L opens the levels' drawer, Up the level before, Enter closes it; the
	# keys are the program's again (a letter typed), F5 runs it
	WR=$(TQ "" maze)
	run pocket_turtle pt-turtle-640 "$WWW;expect kind fill;expect frame 0;$W;dump $OUT/pt-turtle-640.elsm;${LL}dump $OUT/pt-turtle-640-levels.elsm;$(K $UP)dump $OUT/pt-turtle-640-levels-up.elsm;$(K 13)dump $OUT/pt-turtle-640-level.elsm;key 0x58;$W;dump $OUT/pt-turtle-640-typed.elsm" $T SIM_WRITES=$WR
	for f in pt-turtle-640 pt-turtle-640-levels pt-turtle-640-levels-up pt-turtle-640-level pt-turtle-640-typed; do out $f; size $f 640x480 "$f is the whole 640 x 480 screen"; done
	differ pt-turtle-640 pt-turtle-640-levels "L opens the levels' drawer"
	differ pt-turtle-640-levels pt-turtle-640-levels-up "Up in the drawer: the level before"
	differ pt-turtle-640-levels-up pt-turtle-640-level "Enter closes the drawer"
	differ pt-turtle-640-level pt-turtle-640-typed "the keys are the program's again"
	hintline pt-turtle-640 "Turtle Quest"
	footdiffer pt-turtle-640 pt-turtle-640-levels "the drawer's keys are not the program's"
	footsame pt-turtle-640 pt-turtle-640-level "the drawer closed: the program's keys again"
	pixel pt-turtle-640 612 46 15171b "the level's card is a dark surface"
	WR=$(TQ "" maze)
	R12=$(printf 'wait;%.0s' $(seq 1 120))
	run pocket_turtle pt-turtle-640-run "$WWW;key 0x114;${R12}dump $OUT/pt-turtle-640-run.elsm" $T SIM_WRITES=$WR
	out pt-turtle-640-run
	differ pt-turtle-640 pt-turtle-640-run "F5 runs the program"
	# F8: a step (the line says STEP); F2: the hint in the card
	WR=$(TQ "" maze)
	run pocket_turtle pt-turtle-640-step "$WWW;key 0x117;$W;$W;dump $OUT/pt-turtle-640-step.elsm;key 0x118;$W;key 0x111;$W;dump $OUT/pt-turtle-640-hint.elsm;key 0x110;$W;dump $OUT/pt-turtle-640-lesson.elsm" $T SIM_WRITES=$WR
	out pt-turtle-640-step; out pt-turtle-640-hint; out pt-turtle-640-lesson
	pixel pt-turtle-640-lesson 450 260 15171b "F1: the lesson's card is a dark surface (it was white)"
	footdiffer pt-turtle-640 pt-turtle-640-step "F8: the step mode's name in the hint line"
	differ pt-turtle-640 pt-turtle-640-hint "F9, F2: the hint in the card"
	# THE LEVEL EDITOR (Ctrl+E), in steps: the texts (a form; Down: the next field); F2: the map -- Right, Down move the
	# cell, R the next tool (Floor), Enter puts it, Shift + Right paints a run, L the tool before; F2: the program; Esc:
	# the texts again; F5 tries the level
	WR=$(TQ "" maze)
	CE="key 0x05;$W"; SH="mods 2;$W;"; S0="mods 0;$W;"; RR="mods 1;$W;key $PGDN;$W;mods 0;$W;"
	R12=$(printf 'wait;%.0s' $(seq 1 120))
	E1="$WWW;$CE;$W;dump $OUT/pt-turtle-640-editor-texts.elsm;$(K $DOWN)dump $OUT/pt-turtle-640-editor-field.elsm;key 0x111;$W;dump $OUT/pt-turtle-640-editor-map.elsm;$(K $RIGHT)$(K $DOWN)dump $OUT/pt-turtle-640-editor-cell.elsm;${RR}dump $OUT/pt-turtle-640-editor-tool.elsm;$(K 13)dump $OUT/pt-turtle-640-editor-put.elsm;${SH}$(K $RIGHT)$(K $RIGHT)${S0}dump $OUT/pt-turtle-640-editor-run.elsm;${LL}dump $OUT/pt-turtle-640-editor-tool-back.elsm"
	E1="$E1;key 0x111;$W;dump $OUT/pt-turtle-640-editor-program.elsm;$(K 27)dump $OUT/pt-turtle-640-editor-texts-again.elsm;key 0x114;${R12}dump $OUT/pt-turtle-640-editor-try.elsm"
	run pocket_turtle pt-turtle-640-editor "$E1" $T SIM_WRITES=$WR
	for f in texts field map cell tool put run tool-back program texts-again try; do out pt-turtle-640-editor-$f; size pt-turtle-640-editor-$f 640x480 "the editor, $f: the whole 640 x 480 screen"; done
	differ pt-turtle-640 pt-turtle-640-editor-texts "Ctrl+E: the editor, its texts"
	hintline pt-turtle-640-editor-texts "the editor"
	differ pt-turtle-640-editor-texts pt-turtle-640-editor-field "Down: the next field"
	differ pt-turtle-640-editor-field pt-turtle-640-editor-map "F2: the map"
	footdiffer pt-turtle-640-editor-field pt-turtle-640-editor-map "the map's keys are not the texts'"
	differ pt-turtle-640-editor-map pt-turtle-640-editor-cell "Right, Down: the cell's cursor"
	differ pt-turtle-640-editor-cell pt-turtle-640-editor-tool "R: the next tool"
	differ pt-turtle-640-editor-tool pt-turtle-640-editor-put "A puts the tool's cell"
	differ pt-turtle-640-editor-put pt-turtle-640-editor-run "Shift + Right, Right: a run painted"
	differ pt-turtle-640-editor-run pt-turtle-640-editor-tool-back "L: the tool before (no drawer in the editor)"
	differ pt-turtle-640-editor-tool-back pt-turtle-640-editor-program "F2: the program"
	footsame pt-turtle-640-editor-texts pt-turtle-640-editor-texts-again "Esc: the texts again"
	differ pt-turtle-640-editor-texts-again pt-turtle-640-editor-try "F5 tries the level"
	WR=$(TQ fr star-fr)
	run pocket_turtle pt-turtle-640-fr "$WWW;dump $OUT/pt-turtle-640-fr.elsm" $T SIM_WRITES=$WR
	out pt-turtle-640-fr
fi

# ---- Letters: the hint line every window of tools and a page gets (UK_HINTS_STANDARD) -------------------------------------
if want letters; then
	echo "pocket640: Letters"
	ftapp pocket letters uikit_pocket -Wl,--unresolved-symbols=ignore-all
	WR=$(langdir "")
	DOC=; for f in letters-tour.rtf new-year-letter.rtf; do if [ -f "$D/sd/docs/$f" ] || [ -f "sdcard/docs/$f" ]; then DOC="SD:/docs/$f"; break; fi; done
	# F6: the ring on the toolbar (Right, Right: two tools on); Esc: the page again
	run pocket_letters pt-letters-640 "$WWW;$WW;expect kind fill;dump $OUT/pt-letters-640.elsm;key $F6;$W;key $RIGHT;$W;key $RIGHT;$W;dump $OUT/pt-letters-640-f6.elsm" $P640 SIM_APPNAME=letters SIM_APP=letters SIM_WRITES=$WR ${DOC:+SIM_ARGS=$DOC}
	out pt-letters-640; out pt-letters-640-f6
	size pt-letters-640 640x480 "Letters: the whole 640 x 480 screen"
	hintline pt-letters-640 "Letters"
	bardiffer pt-letters-640 pt-letters-640-f6 "Letters: F6 rings the toolbar"
fi

# ---- Slides (Q1): the slide alone, L the slides, R the inspector; the objects by the keys (uikit/objnav.h) ----------------
if want slides; then
	echo "pocket640: Slides"
	GC="$OUT/gpucomp_au.o"; [ -f "$GC" ] || gcc -O2 -w -ffp-contract=off -Iuser -Iuser/Kits -Iuser/Runtime -Iuser/Include -Iuser/Libs -Ikernel/include -c user/Libs/gpucomp/gpucomp.c -o "$GC"
	AK=""; [ -f "${MEDIA_SHOTS:-/tmp/onyx_shots}/libaudiokit.a" ] && AK="${MEDIA_SHOTS:-/tmp/onyx_shots}/libaudiokit.a"
	ftapp pocket slides uikit_pocket "$GC" $AK -lm -Wl,--unresolved-symbols=ignore-all
	K () { echo "key $1;$W;"; }
	LL="mods 1;$W;key $PGUP;$W;mods 0;$W;"; RR="mods 1;$W;key $PGDN;$W;mods 0;$W;"
	SH="mods 2;$W;"; S0="mods 0;$W;"
	SL="$P640 SIM_APPNAME=slides SIM_APP=slides SIM_ARGS=SD:/docs/cafe-2026.odp"
	W8="$WWW;$WW"
	# the slide alone; L: the slides (Down, Down: the third; Enter opens it, the drawer away); R: the inspector (Right: its
	# next tab; Down, Down: a row ringed), R again closes it
	WR=$(langdir "")
	run pocket_slides pt-slides-640 "$W8;expect kind fill;expect frame 0;dump $OUT/pt-slides-640-edit.elsm;${LL}dump $OUT/pt-slides-640-thumbnails.elsm;$(K $DOWN)$(K $DOWN)$(K 13)dump $OUT/pt-slides-640-slide3.elsm;${RR}dump $OUT/pt-slides-640-inspector.elsm;$(K $RIGHT)$(K $DOWN)$(K $DOWN)dump $OUT/pt-slides-640-inspector-row.elsm;${RR}dump $OUT/pt-slides-640-inspector-closed.elsm" $SL SIM_WRITES=$WR
	for f in edit thumbnails slide3 inspector inspector-row inspector-closed; do out pt-slides-640-$f; size pt-slides-640-$f 640x480 "Slides, $f: the whole 640 x 480 screen"; done
	hintline pt-slides-640-edit "Slides"
	differ pt-slides-640-edit pt-slides-640-thumbnails "L opens the slides' drawer"
	footdiffer pt-slides-640-edit pt-slides-640-thumbnails "the drawer's keys are not the slide's"
	differ pt-slides-640-edit pt-slides-640-slide3 "Down, Down, Enter in the drawer: the third slide"
	footsame pt-slides-640-edit pt-slides-640-slide3 "the drawer closed: the slide's keys again"
	differ pt-slides-640-slide3 pt-slides-640-inspector "R opens the inspector"
	differ pt-slides-640-inspector pt-slides-640-inspector-row "the inspector by the keys: a tab, a row"
	same pt-slides-640-slide3 pt-slides-640-inspector-closed "R again closes the inspector"
	# the inspector's rows of SEVERAL buttons (bold, italic...; the alignments): Down (an object), R, Right x2 (the Text tab),
	# Down x4: the row of styles, the ring on its first button; Right: on the second -- the hint line says "its buttons";
	# A: that one's action (Italic on); the themes two by two: Down x6 from the Slide tab's top reaches the third row's first
	WR=$(langdir "")
	run pocket_slides pt-slides-640-buttons "$W8;$(K $DOWN)${RR}$(K $RIGHT)$(K $RIGHT)$(K $DOWN)$(K $DOWN)$(K $DOWN)dump $OUT/pt-slides-640-insp-size.elsm;$(K $DOWN)dump $OUT/pt-slides-640-insp-b0.elsm;$(K $RIGHT)dump $OUT/pt-slides-640-insp-b1.elsm;$(K 13)dump $OUT/pt-slides-640-insp-b2.elsm" $SL SIM_WRITES=$WR
	WR=$(langdir "")
	run pocket_slides pt-slides-640-themes "$W8;${RR}$(K $DOWN)$(K $DOWN)$(K $DOWN)$(K $DOWN)dump $OUT/pt-slides-640-insp-t0.elsm;$(K $RIGHT)dump $OUT/pt-slides-640-insp-t1.elsm;$(K $DOWN)dump $OUT/pt-slides-640-insp-t3.elsm;$(K 13)$W;dump $OUT/pt-slides-640-insp-t3-set.elsm" $SL SIM_WRITES=$WR
	for f in size b0 b1 b2 t0 t1 t3 t3-set; do out pt-slides-640-insp-$f; done
	differ pt-slides-640-insp-b0 pt-slides-640-insp-b1 "a row of buttons: Right puts the ring on the next one"
	footdiffer pt-slides-640-insp-size pt-slides-640-insp-b0 "on such a row the hint line says Left / Right are its buttons"
	differ pt-slides-640-insp-b1 pt-slides-640-insp-b2 "A: the button the ring is on (Italic), not the row's first"
	differ pt-slides-640-insp-t0 pt-slides-640-insp-t1 "the themes: Right, the one beside"
	differ pt-slides-640-insp-t1 pt-slides-640-insp-t3 "the themes: Down, the one below"
	differ pt-slides-640-insp-t3 pt-slides-640-insp-t3-set "A: that theme is the deck's (the slide drawn again)"
	# the objects: the D-pad chooses one, then the next; M: the move mode (Right x3: 24 px; Shift + Down: 1 px), Enter keeps;
	# S: the size mode, Esc cancels (the object as it was kept); Enter edits its text, Esc ends
	WR=$(langdir "")
	OB="$W8;$(K $DOWN)dump $OUT/pt-slides-640-obj1.elsm;$(K $DOWN)dump $OUT/pt-slides-640-obj2.elsm;key 0x6d;$W;dump $OUT/pt-slides-640-move.elsm;$(K $RIGHT)$(K $RIGHT)$(K $RIGHT)${SH}$(K $DOWN)${S0}dump $OUT/pt-slides-640-moved.elsm;$(K 13)dump $OUT/pt-slides-640-kept.elsm"
	OB="$OB;key 0x73;$W;$(K $RIGHT)$(K $DOWN)dump $OUT/pt-slides-640-size.elsm;$(K 27)dump $OUT/pt-slides-640-size-cancelled.elsm;$(K 13)key 0x21;$W;dump $OUT/pt-slides-640-text.elsm;$(K 27)dump $OUT/pt-slides-640-text-done.elsm"
	run pocket_slides pt-slides-640-objects "$OB" $SL SIM_WRITES=$WR
	for f in obj1 obj2 move moved kept size size-cancelled text text-done; do out pt-slides-640-$f; done
	differ pt-slides-640-edit pt-slides-640-obj1 "Down: an object chosen"
	differ pt-slides-640-obj1 pt-slides-640-obj2 "Down again: the next object"
	footdiffer pt-slides-640-obj2 pt-slides-640-move "M: the move mode, named in the hint line"
	differ pt-slides-640-move pt-slides-640-moved "the D-pad moves the object"
	footsame pt-slides-640-obj2 pt-slides-640-kept "Enter keeps: the slide's keys again"
	differ pt-slides-640-obj2 pt-slides-640-kept "the object stays where it was moved"
	footdiffer pt-slides-640-move pt-slides-640-size "S: the size mode"
	same pt-slides-640-kept pt-slides-640-size-cancelled "Esc cancels the size: the object as it was"
	footdiffer pt-slides-640-kept pt-slides-640-text "Enter: the object's text edited"
	# F5 presents: Enter (A) the next slide, Esc (B) ends
	WR=$(langdir "")
	run pocket_slides pt-slides-640-show "$W8;expect full 0;key 0x114;$W8;expect full 1;dump $OUT/pt-slides-640-present.elsm;$(K 13)$W8;expect full 1;$(K 13)$W8;$(K 27)$W8;expect full 0;dump $OUT/pt-slides-640-present-end.elsm" $SL SIM_WRITES=$WR
	for f in present present-end; do out pt-slides-640-$f; done	# (the checks: "expect full" -- F5 takes the full screen, Enter goes on, Esc gives it back)
	hintline pt-slides-640-present-end "Esc (B) ends the show: Slides again"
	WR=$(langdir fr)
	run pocket_slides pt-slides-640-fr "$W8;$(K $DOWN)key 0x6d;$W;dump $OUT/pt-slides-640-move-fr.elsm" $SL SIM_WRITES=$WR
	out pt-slides-640-move-fr
fi

# ---- QBStudio (Q6): the form OR the code (F7), L the project and the toolbox, R the properties; a control by the keys --------
if want qbstudio; then
	echo "pocket640: QBStudio"
	ftapp pocket qbstudio uikit_pocket $BAS user/Libs/basic/baskits.cpp
	K () { echo "key $1;$W;"; }
	LL="mods 1;$W;key $PGUP;$W;mods 0;$W;"; RR="mods 1;$W;key $PGDN;$W;mods 0;$W;"
	QB="$P640 SIM_APPNAME=qbstudio SIM_APP=qbstudio SIM_ARGS=SD:/projects/pages"
	W8="$WWW;$WW"
	# the form alone; the D-pad: a control, the next; M: the move mode (Right: a place later, or 8 px on a Canvas), Enter
	# keeps; S: the size mode (Right x2: 16 px wider), Esc: as it was
	WR=$(langdir "")
	F1="$W8;expect kind fill;expect frame 0;dump $OUT/pt-qbstudio-640-form.elsm;$(K $DOWN)dump $OUT/pt-qbstudio-640-ctl1.elsm;key 0x6d;$W;dump $OUT/pt-qbstudio-640-move.elsm;$(K $RIGHT)dump $OUT/pt-qbstudio-640-moved.elsm;$(K 13)dump $OUT/pt-qbstudio-640-kept.elsm"
	F1="$F1;key 0x73;$W;$(K $RIGHT)$(K $RIGHT)dump $OUT/pt-qbstudio-640-size.elsm;$(K 27)dump $OUT/pt-qbstudio-640-size-cancelled.elsm;$(K $DOWN)dump $OUT/pt-qbstudio-640-ctl2.elsm"
	run pocket_qbstudio pt-qbstudio-640 "$F1" $QB SIM_WRITES=$WR
	for f in form ctl1 ctl2 move moved kept size size-cancelled; do out pt-qbstudio-640-$f; size pt-qbstudio-640-$f 640x480 "QBStudio, $f: the whole 640 x 480 screen"; done
	hintline pt-qbstudio-640-form "QBStudio"
	differ pt-qbstudio-640-form pt-qbstudio-640-ctl1 "Down: a control chosen"
	footdiffer pt-qbstudio-640-ctl1 pt-qbstudio-640-move "M: the move mode, named in the hint line"
	differ pt-qbstudio-640-move pt-qbstudio-640-moved "the D-pad moves the control (in a Row: a place later)"
	footsame pt-qbstudio-640-ctl1 pt-qbstudio-640-kept "Enter keeps: the form's keys again"
	differ pt-qbstudio-640-ctl1 pt-qbstudio-640-kept "the control stays where it was moved"
	differ pt-qbstudio-640-kept pt-qbstudio-640-size "S, Right, Right: the control wider"
	same pt-qbstudio-640-kept pt-qbstudio-640-size-cancelled "Esc cancels the size: the control as it was"
	differ pt-qbstudio-640-size-cancelled pt-qbstudio-640-ctl2 "Down: the next control"
	# L: the project and the toolbox (the keys are the toolbox's: Down x3, Enter adds that control -- the drawer away, the
	# control in the move mode; Enter keeps it); R: the properties (Down, Down: a row; R closes)
	WR=$(langdir "")
	F2="$W8;$(K $DOWN)${LL}dump $OUT/pt-qbstudio-640-toolbox.elsm;$(K $DOWN)$(K $DOWN)$(K $DOWN)dump $OUT/pt-qbstudio-640-toolbox-row.elsm;$(K 13)dump $OUT/pt-qbstudio-640-added.elsm;$(K 13)dump $OUT/pt-qbstudio-640-added-kept.elsm"
	F2="$F2;${RR}dump $OUT/pt-qbstudio-640-props.elsm;$(K $DOWN)$(K $DOWN)dump $OUT/pt-qbstudio-640-props-row.elsm;${RR}dump $OUT/pt-qbstudio-640-props-closed.elsm"
	run pocket_qbstudio pt-qbstudio-640-drawers "$F2" $QB SIM_WRITES=$WR
	for f in toolbox toolbox-row added added-kept props props-row props-closed; do out pt-qbstudio-640-$f; done
	differ pt-qbstudio-640-ctl1 pt-qbstudio-640-toolbox "L opens the project and the toolbox"
	differ pt-qbstudio-640-toolbox pt-qbstudio-640-toolbox-row "Down in the drawer: a tool ringed"
	differ pt-qbstudio-640-ctl1 pt-qbstudio-640-added "Enter adds the control: the drawer away, the form changed"
	footdiffer pt-qbstudio-640-ctl1 pt-qbstudio-640-added "the control added is in the move mode"
	footsame pt-qbstudio-640-ctl1 pt-qbstudio-640-added-kept "Enter keeps it: the form's keys again"
	differ pt-qbstudio-640-added-kept pt-qbstudio-640-props "R opens the properties"
	differ pt-qbstudio-640-props pt-qbstudio-640-props-row "Down, Down in the properties: a row ringed"
	same pt-qbstudio-640-added-kept pt-qbstudio-640-props-closed "R again closes them"
	# F7: the code (its own bar, its keys), F7: the form again
	WR=$(langdir "")
	run pocket_qbstudio pt-qbstudio-640-f7 "$W8;key 0x116;$W8;dump $OUT/pt-qbstudio-640-code.elsm;key 0x116;$W8;dump $OUT/pt-qbstudio-640-form-again.elsm" $QB SIM_WRITES=$WR
	out pt-qbstudio-640-code; out pt-qbstudio-640-form-again
	differ pt-qbstudio-640-form pt-qbstudio-640-code "F7: the code"
	footdiffer pt-qbstudio-640-form pt-qbstudio-640-code "the code's keys are not the form's"
	same pt-qbstudio-640-form pt-qbstudio-640-form-again "F7 again: the form"
	WR=$(langdir fr)
	run pocket_qbstudio pt-qbstudio-640-fr "$W8;$(K $DOWN)dump $OUT/pt-qbstudio-640-form-fr.elsm" $QB SIM_WRITES=$WR
	out pt-qbstudio-640-form-fr
fi

# ---- Paint (Q2): in pocket it adjusts a picture -- one row of tasks (Crop, Resize, Rotate, Adjust, Text) under it -------------
if want paint; then
	echo "pocket640: Paint"
	GC="$OUT/gpucomp_au.o"; [ -f "$GC" ] || gcc -O2 -w -ffp-contract=off -Iuser -Iuser/Kits -Iuser/Runtime -Iuser/Include -Iuser/Libs -Ikernel/include -c user/Libs/gpucomp/gpucomp.c -o "$GC"
	AK=""; [ -f "${MEDIA_SHOTS:-/tmp/onyx_shots}/libaudiokit.a" ] && AK="${MEDIA_SHOTS:-/tmp/onyx_shots}/libaudiokit.a"
	ftapp pocket paint uikit_pocket "$GC" $AK -lm -Wl,--unresolved-symbols=ignore-all
	K () { echo "key $1;$W;"; }
	RR="mods 1;$W;key $PGDN;$W;mods 0;$W;"; SH="mods 2;$W;"; S0="mods 0;$W;"
	PA="$P640 SIM_APPNAME=paint SIM_APP=paint SIM_ARGS=SD:/docs/pictures/sunny-mountains.jpg"
	W8="$WWW;$WW"
	# the picture over its row of tasks; A on Crop: a frame (the D-pad moves it, Shift + D-pad sizes it, R the next ratio),
	# A applies -- the picture is the frame's; Ctrl+Z gives it back
	WR=$(langdir "")
	C1="$W8;expect kind fill;expect frame 0;dump $OUT/pt-paint-640-view.elsm;$(K 13)dump $OUT/pt-paint-640-crop.elsm;$(K $RIGHT)$(K $DOWN)dump $OUT/pt-paint-640-crop-moved.elsm;${SH}$(K $LEFT)$(K $LEFT)${S0}dump $OUT/pt-paint-640-crop-sized.elsm;${RR}dump $OUT/pt-paint-640-crop-ratio.elsm;$(K 13)$W8;dump $OUT/pt-paint-640-cropped.elsm;mods 1;$W;key 26;$W;mods 0;$W8;dump $OUT/pt-paint-640-crop-undone.elsm"
	run pocket_paint pt-paint-640 "$C1" $PA SIM_WRITES=$WR
	for f in view crop crop-moved crop-sized crop-ratio cropped crop-undone; do out pt-paint-640-$f; size pt-paint-640-$f 640x480 "Paint, $f: the whole 640 x 480 screen"; done
	hintline pt-paint-640-view "Paint"
	differ pt-paint-640-view pt-paint-640-crop "A on Crop: the frame"
	footdiffer pt-paint-640-view pt-paint-640-crop "the crop's keys, CROP in the hint line"
	differ pt-paint-640-crop pt-paint-640-crop-moved "the D-pad moves the frame"
	differ pt-paint-640-crop-moved pt-paint-640-crop-sized "Shift + D-pad sizes it"
	differ pt-paint-640-crop-sized pt-paint-640-crop-ratio "R: the next ratio"
	differ pt-paint-640-view pt-paint-640-cropped "A applies: the picture cropped"
	footsame pt-paint-640-view pt-paint-640-cropped "the tasks' keys again"
	picsame () { if python3 -c "from PIL import Image; import sys; a = Image.open ('$OUT/$1.png').convert ('RGB').crop ((0, 30, 640, 400)); b = Image.open ('$OUT/$2.png').convert ('RGB').crop ((0, 30, 640, 400)); sys.exit (0 if a.tobytes () == b.tobytes () else 1)"; then echo "  $3: ok"; else echo "  $3: FAILED (the pictures of $1.png and $2.png differ)"; FAIL=1; fi; }
	picsame pt-paint-640-view pt-paint-640-crop-undone "Ctrl+Z: the picture as it was"
	# Rotate (Right, Right on the row, A): Right turns it, Up flips it, B cancels -- as it was; Crop then B: nothing changed
	WR=$(langdir "")
	R1="$W8;$(K $RIGHT)$(K $RIGHT)dump $OUT/pt-paint-640-task-rotate.elsm;$(K 13)$(K $RIGHT)$W8;dump $OUT/pt-paint-640-rotated.elsm;$(K $UP)$W8;dump $OUT/pt-paint-640-flipped.elsm;$(K 27)$W8;dump $OUT/pt-paint-640-rotate-cancelled.elsm"
	R1="$R1;$(K 13)$(K $LEFT)$(K 13)$W8;dump $OUT/pt-paint-640-rotate-kept.elsm"
	run pocket_paint pt-paint-640-rot "$R1" $PA SIM_WRITES=$WR
	for f in task-rotate rotated flipped rotate-cancelled rotate-kept; do out pt-paint-640-$f; done
	differ pt-paint-640-view pt-paint-640-task-rotate "Right, Right: the Rotate task chosen"
	differ pt-paint-640-task-rotate pt-paint-640-rotated "A, Right: the picture turned"
	differ pt-paint-640-rotated pt-paint-640-flipped "Up: flipped"
	picsame pt-paint-640-task-rotate pt-paint-640-rotate-cancelled "B cancels: the picture as it was"
	differ pt-paint-640-task-rotate pt-paint-640-rotate-kept "A, Left, A: turned and kept"
	# Resize and Adjust are dialogs by the keys (Esc leaves each); Text: a box in the middle, typed, A puts it
	WR=$(langdir "")
	T1="$W8;$(K $RIGHT)$(K 13)$W;dump $OUT/pt-paint-640-resize.elsm;$(K 27)$(K $RIGHT)$(K $RIGHT)$(K 13)$W;dump $OUT/pt-paint-640-adjust.elsm;$(K 27)$(K $RIGHT)$(K 13)dump $OUT/pt-paint-640-text.elsm;key 0x4f;$W;key 0x6b;$W;dump $OUT/pt-paint-640-text-typed.elsm;$(K 13)$W8;dump $OUT/pt-paint-640-text-put.elsm"
	run pocket_paint pt-paint-640-dlg "$T1" $PA SIM_WRITES=$WR
	for f in resize adjust text text-typed text-put; do out pt-paint-640-$f; done
	differ pt-paint-640-view pt-paint-640-resize "Resize: its dialog"
	differ pt-paint-640-view pt-paint-640-adjust "Adjust: its dialog"
	footdiffer pt-paint-640-view pt-paint-640-text "Text: TEXT in the hint line"
	differ pt-paint-640-text pt-paint-640-text-typed "typing writes on the picture"
	differ pt-paint-640-view pt-paint-640-text-put "A puts the text: the picture changed"
	WR=$(langdir fr)
	run pocket_paint pt-paint-640-fr "$W8;dump $OUT/pt-paint-640-view-fr.elsm" $PA SIM_WRITES=$WR
	out pt-paint-640-view-fr
fi

# ---- the Control Panel's applets made for 700 x 470 (Theme, Mode, Preload, App Settings, Panel): in pocket under 800 px
# the pane is given as it is (640 px wide) and each fits its layout to it (uk_fit_pane) -- nothing out at the right, no pane
# scrolled sideways; the keys move the focus over what they show
if want applets; then
	echo "pocket640: the Control Panel's applets in a 640 px pane"
	ftapp pocket control uikit_pocket >>"$OUT/log.txt" 2>&1
	# apk <applet> <fitted: 1 a layout made for 700 x 470, 0 one that already fits> <keys...>: the applet in the pane the
	# Control Panel gives it, as it comes; then DRIVEN BY THE KEYS as on the device -- the Control Panel sends an applet its
	# keys as AP_KEY messages, and so does the simulator's "key" for a program run as an applet (fakekapi.cpp)
	apk () {
		a=$1; fitted=$2; keys=$3
		ftapp pocket $a uikit_pocket -lm -Wl,--unresolved-symbols=ignore-all >>"$OUT/log.txt" 2>&1 || { echo "  $a: NOT BUILT (see $OUT/log.txt)"; FAIL=1; return 0; }
		WR=$(langdir "")
		env SIM_OVERLAY=$D/sd $P640 SIM_APPNAME=control SIM_APP=control SIM_ARGS=$a SIM_WRITES=$WR SIM="$WWW;exit" "$OUT/pocket_control" > "$OUT/cp-$a.txt" 2>&1 || true
		pane=$(sed -n 's/^sim: surface_create //p' "$OUT/cp-$a.txt" | tail -1)
		case "$pane" in 640x*) echo "  $a: the pane is the window's width ($pane): ok" ;; *) echo "  $a: FAILED (the pane asked is '$pane', not 640 wide)"; FAIL=1 ;; esac
		env SIM_OVERLAY=$D/sd SIM_APPLET=1 SIM_SURFSIZE=$pane $P640 SIM_APPNAME=$a SIM_APP=$a SIM_WRITES=$WR SIM="$W;$W;dump $OUT/pt-applet-$a-0.elsm;$keys;$W;$W;dump $OUT/pt-applet-$a-1.elsm;exit" timeout 120 "$OUT/pocket_$a" >>"$OUT/log.txt" 2>&1 || true
		env SIM_OVERLAY=$D/sd $P640 SIM_APPNAME=control SIM_APP=control SIM_ARGS=$a SIM_MAIL=40:7 SIM_SURFACE="$OUT/pt-applet-$a-0.elsm" SIM_WRITES=$WR SIM="$WWW;$WW;dump $OUT/pt-applet-$a-640.elsm;exit" timeout 120 "$OUT/pocket_control" >>"$OUT/log.txt" 2>&1 || true
		env SIM_OVERLAY=$D/sd $P640 SIM_APPNAME=control SIM_APP=control SIM_ARGS=$a SIM_MAIL=40:7 SIM_SURFACE="$OUT/pt-applet-$a-1.elsm" SIM_WRITES=$WR SIM="$WWW;$WW;dump $OUT/pt-applet-$a-640-keys.elsm;exit" timeout 120 "$OUT/pocket_control" >>"$OUT/log.txt" 2>&1 || true
		for f in pt-applet-$a-640 pt-applet-$a-640-keys; do python3 $D/shot.py "$OUT/$f.elsm" "$OUT/$f.png" >/dev/null 2>&1 || { echo "  $a: FAILED (no picture $f)"; FAIL=1; }; done
		[ -n "$SHOTS_PNG" ] && { mkdir -p "$SHOTS_PNG"; cp "$OUT/pt-applet-$a-640.png" "$OUT/pt-applet-$a-640-keys.png" "$SHOTS_PNG/"; }
		size pt-applet-$a-640 640x480 "$a in the Control Panel: the whole 640 x 480 screen"
		[ $fitted = 1 ] && busy pt-applet-$a-640 560 80 636 470 "$a: its right part is in the pane"
		differ pt-applet-$a-640 pt-applet-$a-640-keys "$a: the keys reach it (the focus moved, a value changed)"
	}
	KD="key $DOWN;$W;key $DOWN;$W"; KR="key $RIGHT;$W"
	# what sets the device up first: Display, Sound, Wi-Fi, Language & Region, Mode, Theme -- then the other fitted ones
	apk displayconf 0 "$KD"
	apk soundconf 0 "$KD;$KR;$KR"
	apk wpaconf 0 "$KD"
	apk langconf 0 "$KD;$KR"
	apk modeconf 1 "key $DOWN;$W;$KR;$KR;key 32"	# (a card reached, the next one, Space: that mode chosen -- its frame)
	apk theme 1 "$KD;$KR"
	apk preloadconf 1 "$KD;$KR;$KD"
	apk config 1 "$KD;$KD"
	apk dockconf 1 "$KD;$KR"
fi

# ---- the Package Manager: its window is the screen's (it was a fixed 700 x 470 in a viewport), its tabs under L / R -----------
if want pkgman; then
	MS=${MEDIA_SHOTS:-/tmp/onyx_shots}; M=third_party/mbedtls-3.6.3
	if [ -f "$MS/libmb.a" ]; then
		echo "pocket640: the Package Manager"
		Z="$OUT/auz"; if [ ! -f "$Z/zutil.o" ]; then mkdir -p "$Z"; for f in adler32 crc32 deflate inflate inffast inftrees trees zutil; do gcc -O2 -w -c third_party/zlib-1.3.1/$f.c -o "$Z/$f.o"; done; fi
		ftapp pocket pkgman uikit_pocket -Ithird_party/zlib-1.3.1 -I$M/include "$Z"/*.o "$MS/libmb.a"
		RR="mods 1;$W;key $PGDN;$W;mods 0;$W;"; LL="mods 1;$W;key $PGUP;$W;mods 0;$W;"
		WR=$(langdir "")
		run pocket_pkgman pt-pkgman-640 "$WWW;$WW;expect kind fill;expect frame 0;expect client 640,450;dump $OUT/pt-pkgman-640.elsm;${RR}$WW;dump $OUT/pt-pkgman-640-installed.elsm;key $DOWN;$W;key $DOWN;$W;dump $OUT/pt-pkgman-640-row.elsm;${LL}$WW;dump $OUT/pt-pkgman-640-back.elsm" $P640 SIM_APPNAME=pkgman SIM_APP=pkgman SIM_WRITES=$WR
		for f in pt-pkgman-640 pt-pkgman-640-installed pt-pkgman-640-row pt-pkgman-640-back; do out $f; size $f 640x480 "$f is the whole 640 x 480 screen"; done
		busy pt-pkgman-640 450 440 636 476 "the buttons of its foot are in the window"
		differ pt-pkgman-640 pt-pkgman-640-installed "R: the next tab (Installed)"
		differ pt-pkgman-640-installed pt-pkgman-640-row "Down, Down: the keys in the list"
		bardiffer pt-pkgman-640-installed pt-pkgman-640-back "L: the tab before"
	else echo "pocket640: the Package Manager skipped (no $MS/libmb.a)"; fi
fi

# ---- 3DForge (Q7): view a part and change its steps' values -- the D-pad always turns the view, L the steps, R Display -----
if want 3dforge; then
	MS=${MEDIA_SHOTS:-/tmp/onyx_shots}
	if [ -f "$MS/libmanifold.a" ]; then
		echo "pocket640: 3DForge"
		MF=third_party/manifold-3.5.4; CL=third_party/clipper2-46f6391/CPP/Clipper2Lib
		ftapp pocket 3dforge uikit_pocket -DMANIFOLD_PAR=-1 -DMANIFOLD_CROSS_SECTION -DMANIFOLD_NO_IOSTREAM -DMANIFOLD_NO_FILESYSTEM -DCLIPPER2_NO_IOSTREAM -I$MF/include -I$CL/include -Iuser/Apps/3dforge "$MS/libmanifold.a"
		K () { echo "key $1;$W;"; }
		LL="mods 1;$W;key $PGUP;$W;mods 0;$W;"; RR="mods 1;$W;key $PGDN;$W;mods 0;$W;"; SH="mods 2;$W;"; S0="mods 0;$W;"
		FG="$P640 SIM_APPNAME=3dforge SIM_APP=3dforge SIM_ARGS=SD:/docs/3d/bracket.3df"
		W8="$WWW;$WW"
		# the part; Right, Right, Up: the view turned; Shift + Left: slid; +: nearer; 1: from the front; 0: home again
		WR=$(langdir "")
		V1="$W8;expect kind fill;expect frame 0;dump $OUT/pt-3dforge-640-view.elsm;$(K $RIGHT)$(K $RIGHT)$(K $UP)$W;dump $OUT/pt-3dforge-640-turned.elsm;${SH}$(K $LEFT)${S0}dump $OUT/pt-3dforge-640-slid.elsm;key 0x2b;$W;dump $OUT/pt-3dforge-640-zoomed.elsm;key 0x31;$W;dump $OUT/pt-3dforge-640-front.elsm;key 0x30;$W;dump $OUT/pt-3dforge-640-home.elsm"
		run pocket_3dforge pt-3dforge-640 "$V1" $FG SIM_WRITES=$WR
		for f in view turned slid zoomed front home; do out pt-3dforge-640-$f; size pt-3dforge-640-$f 640x480 "3DForge, $f: the whole 640 x 480 screen"; done
		hintline pt-3dforge-640-view "3DForge"
		differ pt-3dforge-640-view pt-3dforge-640-turned "the D-pad turns the view"
		differ pt-3dforge-640-turned pt-3dforge-640-slid "Shift + D-pad slides it"
		differ pt-3dforge-640-slid pt-3dforge-640-zoomed "+ zooms"
		differ pt-3dforge-640-zoomed pt-3dforge-640-front "1: from the front"
		same pt-3dforge-640-view pt-3dforge-640-home "0: home again, the picture of the start"
		# L: the steps; Down: a step (it lights up on the part), Down: the next; A: into its values; a digit typed and Enter: the
		# part rebuilt; L closes -- the part is another; R: Display, Down, Space: a switch
		WR=$(langdir "")
		S1="$W8;${LL}dump $OUT/pt-3dforge-640-steps.elsm;$(K $DOWN)dump $OUT/pt-3dforge-640-step1.elsm;$(K $DOWN)dump $OUT/pt-3dforge-640-step2.elsm;$(K $UP)$(K 13)dump $OUT/pt-3dforge-640-values.elsm;key 0x35;$W;$(K 13)$W8;dump $OUT/pt-3dforge-640-rebuilt.elsm;$(K 27)dump $OUT/pt-3dforge-640-steps-again.elsm;${LL}dump $OUT/pt-3dforge-640-closed.elsm"
		S1="$S1;${RR}dump $OUT/pt-3dforge-640-display.elsm;$(K $DOWN)$(K 32)dump $OUT/pt-3dforge-640-display-off.elsm;${RR}dump $OUT/pt-3dforge-640-display-closed.elsm"
		run pocket_3dforge pt-3dforge-640-steps-run "$S1" $FG SIM_WRITES=$WR
		for f in steps step1 step2 values rebuilt steps-again closed display display-off display-closed; do out pt-3dforge-640-$f; done
		differ pt-3dforge-640-rebuilt pt-3dforge-640-steps-again "B in the values: the steps' list again"
		differ pt-3dforge-640-view pt-3dforge-640-steps "L opens the steps"
		footdiffer pt-3dforge-640-view pt-3dforge-640-steps "the steps' keys are not the view's"
		differ pt-3dforge-640-steps pt-3dforge-640-step1 "Down: a step chosen"
		differ pt-3dforge-640-step1 pt-3dforge-640-step2 "Down: the next step"
		differ pt-3dforge-640-step1 pt-3dforge-640-values "A: into the step's values (a field has the keys)"
		differ pt-3dforge-640-values pt-3dforge-640-rebuilt "a digit typed, Enter: the value kept"
		differ pt-3dforge-640-view pt-3dforge-640-closed "L closes the steps: the part was rebuilt, another picture"
		differ pt-3dforge-640-closed pt-3dforge-640-display "R opens Display"
		differ pt-3dforge-640-display pt-3dforge-640-display-off "Down, Space: a switch changed"
		differ pt-3dforge-640-closed pt-3dforge-640-display-closed "R closes it: the view drawn with the switch changed"
		WR=$(langdir fr)
		run pocket_3dforge pt-3dforge-640-fr "$W8;${LL}dump $OUT/pt-3dforge-640-steps-fr.elsm" $FG SIM_WRITES=$WR
		out pt-3dforge-640-steps-fr
	else echo "pocket640: 3DForge skipped (no $MS/libmanifold.a: sh tools/tests/desktop_sim/shots.sh 3dforge)"; fi
fi

# ---- L / R BY BOTH PATHS in every reworked app: the keys (Ctrl + Page Up / Down) and a pad's buttons (InputKit's Prev /
# Next) must do the same thing -- UIKit's one rule. Each app twice: L, then L again and R, by the keys; the same by the
# pad; the screens compared (the binaries are the sections' above: run them first in this folder; Mail's is in its section)
if want lr; then
	echo "pocket640: L / R by the keys and by the pad"
	KL="mods 1;$W;key $PGUP;$W;mods 0;$W;$W"; KR="mods 1;$W;key $PGDN;$W;mods 0;$W;$W"
	XL="pad 16;$W;pad 0;$W;$W;$W"; XR="pad 32;$W;pad 0;$W;$W;$W"
	# lr <app> <how the two pictures are compared: all | bars> [VAR=value ...]
	lr () {
		a=$1; how=$2; shift 2
		[ -x "$OUT/pocket_$a" ] || { echo "  $a: not built in this folder (run its section first)"; return 0; }
		for p in key pad; do
			if [ $p = key ]; then L=$KL; R=$KR; else L=$XL; R=$XR; fi
			WR=$(langdir ""); [ $a = circuits ] && { mkdir -p "$WR/apps/circuits.app"; cp $D/circuits/solving.ini "$WR/apps/circuits.app/progress.ini"; }
			[ $a = turtle ] && { mkdir -p "$WR/apps/turtle.app"; cp $D/turtle/maze.ini "$WR/apps/turtle.app/progress.ini"; }
			run pocket_$a lr-$a-$p "$WWW;$WW;$WW;${L};dump $OUT/lr-$a-$p-l.elsm;${L};${R};dump $OUT/lr-$a-$p-r.elsm" $P640 SIM_APPNAME=$a SIM_APP=$a SIM_WRITES=$WR "$@" >/dev/null
			png lr-$a-$p-l >/dev/null; png lr-$a-$p-r >/dev/null
		done
		if [ $how = all ]; then same lr-$a-key-l lr-$a-pad-l "$a: L by the keys and by the pad"; same lr-$a-key-r lr-$a-pad-r "$a: R by the keys and by the pad"
		else barsame lr-$a-key-l lr-$a-pad-l "$a: L by the keys and by the pad (the bar)"; footsame lr-$a-key-l lr-$a-pad-l "$a: ... (the hint line)"; barsame lr-$a-key-r lr-$a-pad-r "$a: R by the keys and by the pad (the bar)"; footsame lr-$a-key-r lr-$a-pad-r "$a: ... (the hint line)"; fi
	}
	lr circuits all
	lr turtle all
	lr gpiolab bars "SIM_ARGS=--demo --tab chart"
	lr letters all SIM_ARGS=SD:/docs/letters-tour.rtf
	lr slides all SIM_ARGS=SD:/docs/cafe-2026.odp
	lr qbstudio all SIM_ARGS=SD:/projects/pages
	lr paint all SIM_ARGS=SD:/docs/pictures/sunny-mountains.jpg
	lr pkgman all
	lr 3dforge all SIM_ARGS=SD:/docs/3d/bracket.3df
	[ -x "$OUT/pocket_setup" ] || ftapp pocket setup uikit_pocket	# (its section comes after this one)
	lr setup all
fi

# ---- the Home (Q9, the Agenda): the Calendar's next appointment is a line on the Home; Up from the apps' first row puts
# the keys on it (a ring), Down gives them back, A opens the Calendar
if want home; then
	echo "pocket640: the Home's Today line by the keys"
	ftapp pocket pocketshell uikit_pocket
	WR=$(langdir ""); mkdir -p "$WR/apps/calendar.app" "$WR/etc/pocket"
	printf '20260928|09:30 Stand-up\n20260928|14:00 Team call\n20261001|Pay the rent\n' > "$WR/apps/calendar.app/agenda.txt"
	printf 'terminal\ntinypad\nletters\ncalendar\n' > "$WR/etc/pocket/recent"
	E="$P640 SIM_APPNAME=pocketshell SIM_APP=pocketshell SIM_WRITES=$WR SIM_DATE=20260928 SIM_TIME=0900"
	run pocket_pocketshell pt-home-640-today "$W;$W;expect shell app;expect kind home;mods 8;mods 0;$W;$W;expect home 1;dump $OUT/pt-home-640-0.elsm;key $UP;$W;$W;dump $OUT/pt-home-640-today.elsm;key $DOWN;$W;$W;dump $OUT/pt-home-640-back.elsm;key $UP;$W;key 13;$W;$W;dump $OUT/pt-home-640-opened.elsm" $E
	for f in 0 today back opened; do out pt-home-640-$f; done
	differ pt-home-640-0 pt-home-640-today "Up from the first row: the Today line has the keys (a ring)"
	same pt-home-640-0 pt-home-640-back "Down: the apps again"
	differ pt-home-640-today pt-home-640-opened "A on it: the Calendar asked for, the line let go"
fi

# ---- Stickies (Q9): in pocket an ordinary full-screen app -- the pinned notes as cards, two columns; the D-pad, A opens Notes
if want stickies; then
	echo "pocket640: Stickies, a full-screen app"
	AK=""; [ -f "${MEDIA_SHOTS:-/tmp/onyx_shots}/libaudiokit.a" ] && AK="${MEDIA_SHOTS:-/tmp/onyx_shots}/libaudiokit.a"
	ftapp pocket stickies uikit_pocket user/Apps/notes/notesmodel.cpp $AK -lm
	WR=$(langdir ""); mkdir -p "$WR/Notes"
	printf 'Shopping\n\nMilk, eggs, bread\nCoffee beans\n' > "$WR/Notes/shopping.txt"
	printf 'Ideas for the handheld\n\nA case in walnut.\nA strap.\nA second battery.\n' > "$WR/Notes/ideas.txt"
	printf 'Call the plumber\n\nBefore Friday. 02 555 01 23\n' > "$WR/Notes/plumber.txt"
	printf '[shopping.txt]\npinned = 1\ncolour = yellow\n[ideas.txt]\npinned = 1\ncolour = blue\n[plumber.txt]\npinned = 1\ncolour = green\n' > "$WR/Notes/notes.ini"
	ST="$P640 SIM_APPNAME=stickies SIM_APP=stickies SIM_WRITES=$WR"
	run pocket_stickies pt-stickies-640 "$WWW;$WW;expect kind fill;expect frame 0;dump $OUT/pt-stickies-640.elsm;key $RIGHT;$W;dump $OUT/pt-stickies-640-right.elsm;key $LEFT;$W;key $DOWN;$W;dump $OUT/pt-stickies-640-down.elsm" $ST
	for f in pt-stickies-640 pt-stickies-640-right pt-stickies-640-down; do out $f; size $f 640x480 "$f is the whole 640 x 480 screen"; done
	hintline pt-stickies-640 "Stickies"
	busy pt-stickies-640 20 50 620 300 "the pinned notes are shown"
	differ pt-stickies-640 pt-stickies-640-right "Right: the next card"
	differ pt-stickies-640 pt-stickies-640-down "Left, Down: another card"
fi

# ---- the clipboard's history as a sheet (Q9): Ctrl + Shift + V over the app in front -- the shell's key, whatever the
# app --; Up / Down choose, A pastes into the app under it (the entry made clipd's cursor, the sheet away, Ctrl + V typed
# by the shell), Bksp forgets an entry, B closes. clipd runs as a thread of the test (clipsheet_demo.cpp, SIM_IPC=1), its
# ring seeded with the mock-up's copies; the simulator's clock is 12:34.
if want clipboard; then
	echo "pocket640: the clipboard's history, a sheet over the app"
	$CXX $INC -Iuser/Kits/fontkit -I$FT/include -Dmain=shell_main -c user/Apps/pocketshell/main.cpp -o "$OUT/clipshell.o"
	$CXX $INC -Iuser/Apps/clipd -o "$OUT/pocket_clipsheet" "$OUT/obj/pocket"/*.o "$OUT/fakekapi.o" "$OUT/clipshell.o" $S/clipsheet_demo.cpp "$OUT/libuikit_pocket.a" "$OUT/libft.a" -lpthread
	ftapp pocket menubar uikit_pocket user/Apps/clock/alarms.cpp user/Apps/clock/clocktime.cpp
	app pocket tinypad uikit_pocket
	CMENUS='MFile|I1~New~^N|I2~Open...~^O|I3~Save~^S|MEdit|I7~Cut~^X|I8~Copy~^C|I9~Paste~^V'
	clip () {	# clip <tag> <lang>: the app (the Text Editor) and the band's pictures, then the shell over them
		t=$1; lg=$2; WR=$(langdir "$lg")
		printf '[default]\na = 1\nb = 2\nx = 3\ny = 4\nl = 5\nr = 6\nselect = 7\nstart = 8\nhome = 9\ndpad = hat\n' > "$WR/etc/gamepad.ini"
		run pocket_tinypad pt-clip-pad$t "$WW;key P;key o;key c;key k;key e;key t;key T;key e;key r;key m;key 13;key 13;key T;key O;key D;key O;key :;$W;dump $OUT/pt-clip-pad$t.elsm" $P640 SIM_APPNAME=tinypad SIM_APP=tinypad SIM_WRITES=$WR
		run pocket_menubar pt-clip-bar$t "other 640 450 Text_Editor;othermenu $CMENUS;$W;$W;$W;dump $OUT/pt-clip-bar$t.elsm" $P640 SIM_APPNAME=menubar SIM_APP=menubar SIM_WRITES=$WR
		CE="$P640 SIM_APPNAME=pocketshell SIM_APP=pocketshell SIM_WRITES=$WR SIM_IPC=1 SIM_SLEEP=1"
		CAPP="otherpic 640 30 menubar $OUT/pt-clip-bar$t.elsm 0 0 0x35 0 0;$W;$W;otherpic 640 450 tinypad $OUT/pt-clip-pad$t.elsm 0 30;$W;$W;expect front other;expect shown1 0"
		COPEN="mods 3;wait;key 0x16;wait;mods 0;$W;$W"
	}
	clip "" ""
	# Ctrl + Shift + V: the sheet, the newest entry chosen; Down: the second (the mock-up's picture); Bksp forgets it (5 left,
	# the next takes its place); Down, A: the sheet away, that entry clipd's cursor, Ctrl + V typed for the app
	SC="$CAPP;dump $OUT/pt-clip-640-before.elsm;$COPEN;expect shown1 1;expect typed none;dump $OUT/pt-clip-640-0.elsm;key $DOWN;$W;dump $OUT/pt-clip-640-1-chosen.elsm"
	SC="$SC;key 0x7f;$W;$W;$W;dump $OUT/pt-clip-640-2-forgotten.elsm;key $DOWN;$W;key 13;$W;$W;expect shown1 0;expect front other;expect typed 1:22;dump $OUT/pt-clip-640-3-pasted.elsm"
	# again: the entry pasted is the chosen one now (the clipboard's current entry); B closes, nothing typed more; the
	# letter with Ctrl and Shift held (a remote desktop's way) opens it too, and closes it; Ctrl + V alone is the app's
	SC="$SC;$COPEN;expect shown1 1;dump $OUT/pt-clip-640-4-again.elsm;key 0x1b;$W;expect shown1 0"
	SC="$SC;mods 3;wait;key V;wait;mods 0;$W;expect shown1 1;mods 3;wait;key v;wait;mods 0;$W;expect shown1 0;mods 1;wait;key 0x16;wait;mods 0;$W;expect shown1 0"
	# a pad: Down and A do what the keys do (the first entry below the chosen one, pasted)
	SC="$SC;$COPEN;expect shown1 1;pad 0x4000;$W;pad 0;$W;$W;dump $OUT/pt-clip-640-5-pad.elsm;pad 1;$W;pad 0;$W;$W;expect shown1 0;expect typed 1:22"
	run pocket_clipsheet pt-clip-640 "$SC" $CE
	for f in before 0 1-chosen 2-forgotten 3-pasted 4-again 5-pad; do out pt-clip-640-$f; size pt-clip-640-$f 640x480 "the sheet, $f: the whole 640 x 480 screen"; done
	differ pt-clip-640-before pt-clip-640-0 "Ctrl + Shift + V: the sheet over the app"
	pixel pt-clip-640-0 634 466 000000 "the sheet: its hint line's ground"; busy pt-clip-640-0 10 454 360 478 "the sheet: its hint line says the keys"
	pixel pt-clip-640-0 110 120 3d86da "the newest entry is the chosen one (the accent)"
	pixel pt-clip-640-1-chosen 110 174 3d86da "Down: the second entry chosen"
	differ pt-clip-640-1-chosen pt-clip-640-2-forgotten "Bksp: the entry forgotten"
	same pt-clip-640-before pt-clip-640-3-pasted "A: the sheet away, the app as it was"
	pixel pt-clip-640-4-again 110 228 3d86da "again: the pasted entry is the clipboard's current one"
	pixel pt-clip-640-5-pad 110 282 3d86da "a pad's Down: the next entry"
	if grep -q 'clipsheet: delete -- 5 entries' "$OUT/log.txt"; then echo "  Bksp: clipd forgot the entry (5 left): ok"; else echo "  Bksp: clipd forgot the entry: FAILED (no line in the log)"; FAIL=1; fi
	if grep -q 'clipsheet: cursor -- 5 entries, the cursor is "WHILE NOT ONGOAL ()"' "$OUT/log.txt"; then echo "  A: the chosen entry is what Ctrl + V pastes: ok"; else echo "  A: the chosen entry is what Ctrl + V pastes: FAILED (no line in the log)"; FAIL=1; fi
	# nothing copied yet; more entries than the sheet shows (Down x 7: it scrolls); in French
	run pocket_clipsheet pt-clip-640-empty "$CAPP;$COPEN;expect shown1 1;dump $OUT/pt-clip-640-empty.elsm;key 13;$W;expect shown1 0;expect typed none" $CE CLIPSHEET_EMPTY=1
	KD7="key $DOWN;key $DOWN;key $DOWN;key $DOWN;key $DOWN;key $DOWN;key $DOWN"
	run pocket_clipsheet pt-clip-640-many "$CAPP;$COPEN;expect shown1 1;dump $OUT/pt-clip-640-many-0.elsm;$KD7;$W;dump $OUT/pt-clip-640-many-7.elsm" $CE CLIPSHEET_MANY=1
	clip -fr fr
	run pocket_clipsheet pt-clip-640-fr "$CAPP;$COPEN;expect shown1 1;key $DOWN;$W;dump $OUT/pt-clip-640-fr.elsm" $CE
	for f in empty many-0 many-7 fr; do out pt-clip-640-$f; done
	differ pt-clip-640-empty pt-clip-640-0 "nothing copied: the sheet says so"
	differ pt-clip-640-many-0 pt-clip-640-many-7 "nine entries: Down x 7 scrolls the sheet"
	differ pt-clip-640-1-chosen pt-clip-640-fr "the sheet in French"
	# the Menu's column has "Clipboard history" before Quit: Up twice from the first row, A -- the shell is asked (106)
	WR=$(langdir "")
	run pocket_menubar pt-clip-640-menu "other 640 450 Text_Editor;othermenu $CMENUS;$W;$W;$W;key $F10;$W;expect bargrab 1;key $UP;key $UP;$W;dump $OUT/pt-clip-640-menu.elsm;key 13;$W;expect bargrab 0" $P640 SIM_APPNAME=menubar SIM_APP=menubar SIM_WRITES=$WR SIM_SERVICES=shell
	out pt-clip-640-menu
	if grep -q 'sim: send shell type 106' "$OUT/log.txt"; then echo "  the Menu's Clipboard history asks the shell for the sheet: ok"; else echo "  the Menu's Clipboard history: FAILED (the shell was not asked)"; FAIL=1; fi
fi

# ---- Koton (Q4): in pocket it plays a project and mixes it -- two views under L / R, Song (the tracks as rows, their
# blocks, the playhead) and Mixer (a strip a track); composing answers with the "not in pocket" card (a command of the
# Menu's Compose: sent as the menu bar sends it, `menucmd`). The simulator has no sound: nothing plays there (the bar
# says "No sound"), the playhead is moved by the keys.
if want koton; then
	echo "pocket640: Koton, the song and the mixer"
	KT=user/Apps/koton; mkdir -p "$OUT/koton"
	for f in $KT/engine/*.cpp $KT/synth/*.cpp $KT/plug/*.cpp; do [ -f "$OUT/koton/$(basename "$f" .cpp).o" ] || $CXX $INC -I$KT -c "$f" -o "$OUT/koton/$(basename "$f" .cpp).o"; done
	ftapp pocket koton uikit_pocket -I$KT "$OUT"/koton/*.o
	K () { echo "key $1;$W;"; }
	LL="mods 1;$W;key $PGUP;$W;mods 0;$W;"; RR="mods 1;$W;key $PGDN;$W;mods 0;$W;"; SH="mods 2;$W;"; S0="mods 0;$W;"
	KO="$P640 SIM_APPNAME=koton SIM_APP=koton SIM_ARGS=SD:/koton/songs/demo.kson"
	W8="$WWW;$WW"
	# the song; Down, Down: the third track; Right x3: the playhead three bars on; A: muted; S: soloed (the others' blocks dim);
	# S, A: as it was; Home: the playhead at the start
	WR=$(langdir "")
	S1="$W8;expect kind fill;expect frame 0;dump $OUT/pt-koton-640-song.elsm;$(K $DOWN)$(K $DOWN)dump $OUT/pt-koton-640-song-track.elsm;$(K $RIGHT)$(K $RIGHT)$(K $RIGHT)dump $OUT/pt-koton-640-song-bar.elsm"
	S1="$S1;$(K 13)dump $OUT/pt-koton-640-song-mute.elsm;$(K s)dump $OUT/pt-koton-640-song-solo.elsm;$(K s)$(K 13)dump $OUT/pt-koton-640-song-back.elsm;$(K 0x104)dump $OUT/pt-koton-640-song-home.elsm"
	# R: the mixer; Right: the second strip; Up x3: its level; Shift + Down: a fine step; A: muted; S: soloed; L: the song again
	S1="$S1;${RR}dump $OUT/pt-koton-640-mixer.elsm;$(K $RIGHT)dump $OUT/pt-koton-640-mixer-strip.elsm;$(K $UP)$(K $UP)$(K $UP)dump $OUT/pt-koton-640-mixer-level.elsm;${SH}$(K $DOWN)${S0}dump $OUT/pt-koton-640-mixer-fine.elsm"
	S1="$S1;$(K 13)$(K s)dump $OUT/pt-koton-640-mixer-ms.elsm;${LL}dump $OUT/pt-koton-640-song-again.elsm"
	run pocket_koton pt-koton-640 "$S1" $KO SIM_WRITES=$WR
	for f in song song-track song-bar song-mute song-solo song-back song-home mixer mixer-strip mixer-level mixer-fine mixer-ms song-again; do out pt-koton-640-$f; size pt-koton-640-$f 640x480 "Koton, $f: the whole 640 x 480 screen"; done
	hintline pt-koton-640-song "Koton, the song"; hintline pt-koton-640-mixer "Koton, the mixer"
	busy pt-koton-640-song 150 140 630 440 "the song: the tracks' blocks are drawn"
	flatcol () { n=$(python3 -c "from PIL import Image; im = Image.open ('$OUT/$1.png').convert ('RGB').crop (($2, $3, $2 + 1, $4)); print (len (im.getcolors (100000)))"); if [ "$n" = 1 ]; then echo "  $5: ok"; else echo "  $5: FAILED (something is drawn across x = $2 of $1)"; FAIL=1; fi; }
	flatcol pt-koton-640-song 637 70 440 "the song: nothing cut at the right edge"
	flatcol pt-koton-640-mixer 637 70 440 "the mixer: nothing cut at the right edge"
	differ pt-koton-640-song pt-koton-640-song-track "Down, Down: another track chosen"
	differ pt-koton-640-song-track pt-koton-640-song-bar "Right x 3: the playhead three bars on"
	differ pt-koton-640-song-bar pt-koton-640-song-mute "A: the track muted"
	differ pt-koton-640-song-mute pt-koton-640-song-solo "S: the track soloed"
	same pt-koton-640-song-bar pt-koton-640-song-back "S, A again: as it was"
	same pt-koton-640-song-track pt-koton-640-song-home "Home: the playhead at the start again"
	differ pt-koton-640-song-home pt-koton-640-mixer "R: the mixer"
	footdiffer pt-koton-640-song pt-koton-640-mixer "the mixer's keys are not the song's"
	differ pt-koton-640-mixer pt-koton-640-mixer-strip "Right: the next strip"
	differ pt-koton-640-mixer-strip pt-koton-640-mixer-level "Up x 3: the level raised"
	differ pt-koton-640-mixer-level pt-koton-640-mixer-fine "Shift + Down: a fine step down"
	differ pt-koton-640-mixer-fine pt-koton-640-mixer-ms "A, S: muted and soloed"
	differ pt-koton-640-mixer-ms pt-koton-640-song-again "L: the song again"
	barsame pt-koton-640-song pt-koton-640-song-again "the song's bar and tabs again"
	# the card: Compose > Edit a block... (the Menu's command 10); Left: Open FM Tracker has the ring; Right, A (or B): back
	# to the song; again, Left, A: FM Tracker asked for; the Mixer's command (4) from the Menu
	WR=$(langdir "")
	S2="$W8;dump $OUT/pt-koton-640-c0.elsm;menucmd 10;$W;dump $OUT/pt-koton-640-card.elsm;$(K $LEFT)dump $OUT/pt-koton-640-card-fm.elsm;$(K $RIGHT)$(K 13)dump $OUT/pt-koton-640-card-back.elsm"
	S2="$S2;menucmd 12;$W;$(K 27)dump $OUT/pt-koton-640-card-esc.elsm;menucmd 11;$W;$(K $LEFT)$(K 13)dump $OUT/pt-koton-640-card-open.elsm;menucmd 4;$W;dump $OUT/pt-koton-640-menu-mixer.elsm"
	run pocket_koton pt-koton-640-card-run "$S2" $KO SIM_WRITES=$WR
	for f in c0 card card-fm card-back card-esc card-open menu-mixer; do out pt-koton-640-$f; done
	differ pt-koton-640-c0 pt-koton-640-card "Compose > Edit a block: the card"
	footdiffer pt-koton-640-c0 pt-koton-640-card "the card's keys are not the song's"
	differ pt-koton-640-card pt-koton-640-card-fm "Left: Open FM Tracker has the ring"
	same pt-koton-640-c0 pt-koton-640-card-back "Right, A: back to the song"
	same pt-koton-640-c0 pt-koton-640-card-esc "the card again (Generators...), B: back to the song"
	same pt-koton-640-c0 pt-koton-640-card-open "the card again, Left, A: the card away"
	if grep -q "sim: launch fmtracker\|sim: raise_app fmtracker" "$OUT/log.txt"; then echo "  Open FM Tracker: the app asked for: ok"; else echo "  Open FM Tracker: FAILED (not asked for)"; FAIL=1; fi
	differ pt-koton-640-c0 pt-koton-640-menu-mixer "the Menu's Mixer: the mixer"
	# L / R by a pad as by the keys; in French
	WR=$(langdir ""); printf '[default]\na = 1\nb = 2\nx = 3\ny = 4\nl = 5\nr = 6\nselect = 7\nstart = 8\nhome = 9\ndpad = hat\n' > "$WR/etc/gamepad.ini"
	run pocket_koton pt-koton-640-pad "$W8;pad 32;$W;pad 0;$W;$W;$W;dump $OUT/pt-koton-640-pad-r.elsm;pad 16;$W;pad 0;$W;$W;$W;dump $OUT/pt-koton-640-pad-l.elsm;pad 0x4000;$W;pad 0;$W;$W;pad 0x4000;$W;pad 0;$W;$W;dump $OUT/pt-koton-640-pad-down.elsm" $KO SIM_WRITES=$WR
	for f in pad-r pad-l pad-down; do out pt-koton-640-$f; done
	same pt-koton-640-mixer pt-koton-640-pad-r "a pad's R: the mixer, as Ctrl + Page Down"
	same pt-koton-640-song pt-koton-640-pad-l "a pad's L: the song, as Ctrl + Page Up"
	same pt-koton-640-song-track pt-koton-640-pad-down "a pad's Down, Down: the third track, as the keys"
	WR=$(langdir fr)
	run pocket_koton pt-koton-640-fr "$W8;dump $OUT/pt-koton-640-song-fr.elsm;menucmd 10;$W;dump $OUT/pt-koton-640-card-fr.elsm" $KO SIM_WRITES=$WR
	out pt-koton-640-song-fr; out pt-koton-640-card-fr
	differ pt-koton-640-song pt-koton-640-song-fr "the song in French"
fi

# ---- Setup, the first start (Q5): one page a step, A the next, B back, no Display step -------------------------------
if want setup; then
	echo "pocket640: Setup"
	ftapp pocket setup uikit_pocket
	K () { echo "key $1;$W;"; }
	WR=$(langdir "")
	SC="$WWW;expect kind fill;dump $OUT/pt-setup-640-1-welcome.elsm;$(K 13)dump $OUT/pt-setup-640-2-region.elsm;$(K $DOWN)$(K 13)$W;dump $OUT/pt-setup-640-3-wifi.elsm"
	SC="$SC;$(K 13)dump $OUT/pt-setup-640-4-name.elsm;$(K $DOWN)$(K $DOWN)$(K 32)dump $OUT/pt-setup-640-4-name-switch.elsm;$(K 13)dump $OUT/pt-setup-640-5-ready.elsm;$(K 27)dump $OUT/pt-setup-640-back.elsm"
	run pocket_setup pt-setup-640 "$SC" $P640 SIM_APPNAME=setup SIM_APP=setup SIM_WRITES=$WR
	for f in 1-welcome 2-region 3-wifi 4-name 4-name-switch 5-ready back; do out pt-setup-640-$f; size pt-setup-640-$f 640x480 "Setup, $f: the whole 640 x 480 screen"; done
	differ pt-setup-640-1-welcome pt-setup-640-2-region "Setup: A leaves the welcome page"
	# (800 x 600, the PocketTerm35's card: pocket's look too -- the desktop's 800 x 600 window would not fit)
	run pocket_setup pt-setup-800 "$WWW;expect kind fill;dump $OUT/pt-setup-800-1-welcome.elsm;$(K 13)dump $OUT/pt-setup-800-2-region.elsm" SIM_SCREEN=800x600 SIM_MODE=pocket SIM_APPNAME=setup SIM_APP=setup SIM_WRITES=$WR
	for f in 1-welcome 2-region; do out pt-setup-800-$f; size pt-setup-800-$f 800x600 "Setup at 800 x 600, $f: the whole screen"; done
	differ pt-setup-640-2-region pt-setup-640-3-wifi "Setup: A, the Wi-Fi step"
	differ pt-setup-640-3-wifi pt-setup-640-4-name "Setup: A after Wi-Fi is the name (no Display, no Appearance step)"
	differ pt-setup-640-4-name pt-setup-640-4-name-switch "Setup: Down, Down, Space flips a service's switch"
	differ pt-setup-640-4-name-switch pt-setup-640-5-ready "Setup: A, ready"
	barsame pt-setup-640-4-name pt-setup-640-back "Setup: B goes back one step"
	# the Wi-Fi step by the keys: Down -- the next network (one with a password) under the bar; A: ITS PASSWORD'S PAGE (that network
	# alone, the field); typed (shown as typed); B: the list again; Down, A, the password, A: connecting
	WR=$(langdir "")
	WF="$WWW;$(K 13)$(K $DOWN)$(K 13)$W;$(K $DOWN)dump $OUT/pt-setup-640-wifi-row.elsm;$(K 13)dump $OUT/pt-setup-640-wifi-password.elsm;key 0x6f;$W;key 0x6e;$W;key 0x79;$W;key 0x78;$W;dump $OUT/pt-setup-640-wifi-typed.elsm;$(K 27)dump $OUT/pt-setup-640-wifi-back.elsm"
	WF="$WF;$(K $DOWN)$(K 13)key 0x6f;$W;key 0x6e;$W;key 0x79;$W;key 0x78;$W;key 0x6f;$W;key 0x6e;$W;key 0x79;$W;key 0x78;$W;$(K 13)$W;dump $OUT/pt-setup-640-wifi-joining.elsm"
	run pocket_setup pt-setup-640-wifi "$WF" $P640 SIM_APPNAME=setup SIM_APP=setup SIM_WRITES=$WR
	for f in wifi-row wifi-password wifi-typed wifi-back wifi-joining; do out pt-setup-640-$f; done
	differ pt-setup-640-3-wifi pt-setup-640-wifi-row "Setup, Wi-Fi: Down puts the bar on the next network"
	differ pt-setup-640-wifi-row pt-setup-640-wifi-password "Setup, Wi-Fi: A opens the password's page"
	differ pt-setup-640-wifi-password pt-setup-640-wifi-typed "Setup, Wi-Fi: the password typed is shown"
	differ pt-setup-640-wifi-typed pt-setup-640-wifi-back "Setup, Wi-Fi: B goes back to the list (every network again)"
	barsame pt-setup-640-wifi-row pt-setup-640-wifi-back "Setup, Wi-Fi: ... on the same step"
	differ pt-setup-640-wifi-back pt-setup-640-wifi-joining "Setup, Wi-Fi: the password typed, A: connecting"
	WR=$(langdir fr)
	run pocket_setup pt-setup-640-fr "$WWW;dump $OUT/pt-setup-640-1-welcome-fr.elsm" $P640 SIM_APPNAME=setup SIM_APP=setup SIM_WRITES=$WR
	out pt-setup-640-1-welcome-fr
fi

# ---- the drawers by the keys (UIKit): a side panel without places opens and closes by L / R ------------------------------
if want drawers; then
	echo "pocket640: the drawers by L / R (the Calendar's side, the Icon Editor's colours)"
	LL="mods 1;$W;key $PGUP;$W;mods 0;$W;"; RR="mods 1;$W;key $PGDN;$W;mods 0;$W;"
	ftapp pocket calendar uikit_pocket
	WR=$(langdir "")
	run pocket_calendar pt-drawer-calendar "$WWW;dump $OUT/pt-drawer-calendar-0.elsm;${LL}dump $OUT/pt-drawer-calendar-1.elsm;key 27;$W;dump $OUT/pt-drawer-calendar-2.elsm" $P640 SIM_APPNAME=calendar SIM_APP=calendar SIM_WRITES=$WR SIM_DATE=20260930 SIM_TIME=1042
	for i in 0 1 2; do out pt-drawer-calendar-$i; done
	differ pt-drawer-calendar-0 pt-drawer-calendar-1 "the Calendar: L opens its side"
	same pt-drawer-calendar-0 pt-drawer-calendar-2 "the Calendar: Esc closes it, the window as before"
	app pocket iconedit uikit_pocket
	WR=$(langdir "")
	run pocket_iconedit pt-drawer-iconedit "$WWW;dump $OUT/pt-drawer-iconedit-0.elsm;${RR}dump $OUT/pt-drawer-iconedit-1.elsm;${RR}dump $OUT/pt-drawer-iconedit-2.elsm" $P640 SIM_APPNAME=iconedit SIM_APP=iconedit SIM_WRITES=$WR SIM_ARGS=SD:/apps/invaders.app/icon.bmp
	for i in 0 1 2; do out pt-drawer-iconedit-$i; done
	differ pt-drawer-iconedit-0 pt-drawer-iconedit-1 "the Icon Editor: R opens its colours"
	same pt-drawer-iconedit-0 pt-drawer-iconedit-2 "the Icon Editor: R again closes them"
	# no pointer: a closed drawer shows no tab (the window's left edge is the content's); a pointer seen: the tab is back
	run pocket_calendar pt-drawer-pointer "$WWW;move 300 200;$W;move 302 202;$W;dump $OUT/pt-drawer-calendar-pointer.elsm" $P640 SIM_APPNAME=calendar SIM_APP=calendar SIM_WRITES=$WR SIM_DATE=20260930 SIM_TIME=1042
	out pt-drawer-calendar-pointer
	differ pt-drawer-calendar-0 pt-drawer-calendar-pointer "a pointer seen: the drawer's tab comes back"
	python3 -c "from PIL import Image; import sys; im = Image.open ('$OUT/pt-drawer-calendar-0.png').convert ('RGB').crop ((0, 230, 4, 330)); sys.exit (0 if len (im.getcolors (1000)) <= 3 else 1)" && echo "  no pointer: no tab on the edge: ok" || { echo "  no pointer: no tab on the edge: FAILED"; FAIL=1; }
	# the band names the drawers beside their buttons (the menu bar given the lines UIKit's SidePanel sends with the menus)
	ftapp pocket menubar uikit_pocket user/Apps/clock/alarms.cpp user/Apps/clock/clocktime.cpp
	WR=$(langdir "")
	DM='DL0~Levels|DR1~Inspector|MFile|I1~Open...~^O|MEdit|I2~Undo~^Z'
	run pocket_menubar pt-drawer-band "$W;other 640 450 Slides;othermenu $DM;$W;$W;$W;dump $OUT/pt-drawer-band.elsm" SIM_SCREEN=640x480 SIM_APPNAME=menubar SIM_APP=menubar SIM_WRITES=$WR SIM_SERVICES=shell
	out pt-drawer-band
	busy pt-drawer-band 190 4 300 26 "the band names the left drawer after Menu"
fi

# ---- the audit: every other app with a window, as it is at 640 x 480 pocket (AUDIT-640.md beside the pictures) ----------
# pb <app>: the app built for PocketUI on the PC (shots.sh's recipes) -> $OUT/pocket_<app>; fails when it cannot be
pb () {
	a=$1; MS=${MEDIA_SHOTS:-/tmp/onyx_shots}; M=third_party/mbedtls-3.6.3; FFH=${FFMPEG_HOST:-/tmp/onyx_ffmpeg_host}
	AK=""; [ -f "$MS/libaudiokit.a" ] && AK="$MS/libaudiokit.a"
	Z="$OUT/auz"; if [ ! -f "$Z/zutil.o" ]; then mkdir -p "$Z"; for f in adler32 crc32 deflate inflate inffast inftrees trees zutil; do gcc -O2 -w -c third_party/zlib-1.3.1/$f.c -o "$Z/$f.o"; done; fi
	GC="$OUT/gpucomp_au.o"; [ -f "$GC" ] || gcc -O2 -w -ffp-contract=off -Iuser -Iuser/Kits -Iuser/Runtime -Iuser/Include -Iuser/Libs -Ikernel/include -c user/Libs/gpucomp/gpucomp.c -o "$GC"
	U="-Wl,--unresolved-symbols=ignore-all"
	case $a in
	circuits) ftapp pocket $a uikit_pocket user/Apps/circuits/circuit.cpp ;;
	notes|stickies) ftapp pocket $a uikit_pocket user/Apps/notes/notesmodel.cpp $AK -lm ;;
	clock) ftapp pocket $a uikit_pocket user/Apps/clock/alarms.cpp user/Apps/clock/clocktime.cpp $AK -lm ;;
	graphcalc) ftapp pocket $a uikit_pocket user/Libs/basic/basnum.cpp $AK -lm ;;
	gamelib) ftapp pocket $a uikit_pocket user/Kits/gamekit/gamekit.cpp user/Emulators/gb/gb.cpp $(ls user/Emulators/gba/*.cpp user/Emulators/nes/*.cpp user/Emulators/snes/*.cpp) ;;
	archiver) ftapp pocket $a uikit_pocket -Ithird_party/zlib-1.3.1 -Iuser/Apps/archiver -Iuser/Kits/filekit user/Kits/filekit/fkcore.cpp "$Z"/*.o ;;
	pkgman) ftapp pocket $a uikit_pocket -Ithird_party/zlib-1.3.1 -I$M/include "$Z"/*.o "$MS/libmb.a" ;;
	mail) ftapp pocket $a uikit_pocket -I$M/include "$MS/libmb.a" ;;
	telegram) ftapp pocket $a uikit_pocket -I$M/include -Ithird_party/zlib-1.3.1 -Iuser/Kits/filekit user/Kits/filekit/fkcore.cpp "$Z"/*.o "$MS/libmbtg.a" ;;
	paint|slides|letters|photos|sheet|printconf) ftapp pocket $a uikit_pocket "$GC" $AK -lm $U ;;
	pdf) $CXX $INC -Iuser/Kits/fontkit -I$FT/include -Ithird_party/mupdf-1.28.5/include -o "$OUT/pocket_pdf" "$OUT/obj/pocket"/*.o "$OUT/fakekapi.o" user/Apps/pdf/main.cpp "$OUT/libuikit_pocket.a" "$MS/mupdf/libmupdf.a" -lpthread -lm $U ;;
	3dforge) MF=third_party/manifold-3.5.4; CL=third_party/clipper2-46f6391/CPP/Clipper2Lib
		ftapp pocket $a uikit_pocket -DMANIFOLD_PAR=-1 -DMANIFOLD_CROSS_SECTION -DMANIFOLD_NO_IOSTREAM -DMANIFOLD_NO_FILESYSTEM -DCLIPPER2_NO_IOSTREAM -I$MF/include -I$CL/include -Iuser/Apps/3dforge "$MS/libmanifold.a" ;;
	qbstudio) ftapp pocket $a uikit_pocket $BAS user/Libs/basic/baskits.cpp ;;
	turtle) ftapp pocket $a uikit_pocket $BAS ;;
	gpiolab) ftapp pocket $a uikit_pocket -Ithird_party/zlib-1.3.1 user/Kits/gpiokit/gkcore.cpp user/Kits/filekit/fkcore.cpp "$Z"/*.o $BAS ;;
	courier) ftapp pocket $a uikit_pocket -DCOURIER_NO_TLS ;;
	media) ftapp pocket $a uikit_pocket -Ithird_party $AK "$MS/av/libavhost.a" -L"$FFH/libavformat" -L"$FFH/libavcodec" -L"$FFH/libswscale" -L"$FFH/libswresample" -L"$FFH/libavutil" -L"$FFH" -lavformat -lavcodec -lswscale -lswresample -lavutil -lm ;;
	koton) K=user/Apps/koton; mkdir -p "$OUT/koton"
		for f in $K/engine/*.cpp $K/synth/*.cpp $K/plug/*.cpp; do [ -f "$OUT/koton/$(basename "$f" .cpp).o" ] || $CXX $INC -I$K -c "$f" -o "$OUT/koton/$(basename "$f" .cpp).o" || return 1; done
		ftapp pocket $a uikit_pocket -I$K "$OUT"/koton/*.o ;;
	*) ftapp pocket $a uikit_pocket $AK -lm $U ;;
	esac
}
# ---- THE PASS BY THE KEYS ALONE over the apps the audit judged from a picture (AUDIT-640.md, "the audit's missing half"):
# each app's main path driven by keys only, a picture a step (keys-<app>-<n>.png; SHOTS_PNG: pt-keys-<app>-<n>.png), the
# steps that must change the screen checked. KEYS_APPS="a b": those apps only.
if want keys; then
	echo "pocket640: the pass by the keys alone"
	K () { echo "key $1;$W;"; }
	LL="mods 1;$W;key $PGUP;$W;mods 0;$W;"; RR="mods 1;$W;key $PGDN;$W;mods 0;$W;"
	CT () { echo "mods 1;$W;key $1;$W;mods 0;$W;"; }			# Ctrl + a key (its control code, or a letter)
	TAB="key 0x09;$W;"
	# kd <app> <waits> "<script: @0 .. @9 a picture each>" [VAR=value ...]
	kd () {
		a=$1; n=$2; sc=$3; shift 3
		if ! pb $a >>"$OUT/log.txt" 2>&1 || [ ! -x "$OUT/pocket_$a" ]; then echo "  $a: NOT BUILT for the PC (see $OUT/log.txt)"; return 0; fi
		WR=$(langdir ""); ws=$(printf 'wait;%.0s' $(seq 1 $n))
		kn=${KN:-$a}; KN=
		rm -f "$OUT"/keys-$kn-?.elsm "$OUT"/keys-$kn-?.png
		sc=$(echo "$sc" | sed "s|@\([0-9]\)|dump $OUT/keys-$kn-\1.elsm|g")
		env SIM_OVERLAY=$D/sd $P640 SIM_APPNAME=$a SIM_APP=$a SIM_WRITES=$WR "$@" SIM="${ws}${sc};exit" timeout 180 "$OUT/pocket_$a" >>"$OUT/log.txt" 2>&1 || true
		for e in "$OUT"/keys-$kn-?.elsm; do
			[ -f "$e" ] || continue
			p=${e%.elsm}.png; python3 $D/shot.py "$e" "$p" >/dev/null 2>&1
			if [ -n "$SHOTS_PNG" ]; then mkdir -p "$SHOTS_PNG"; cp "$p" "$SHOTS_PNG/pt-$(basename "$p")"; fi
		done
	}
	# kdiff / ksame <app> <n> <m> <what>: two of its pictures differ / are the same
	kin () { case " $KLIST " in *" $1 "*) return 0 ;; esac; return 1; }
	kdiff () { kin $1 || return 0; if [ ! -f "$OUT/keys-$1-$2.png" ] || [ ! -f "$OUT/keys-$1-$3.png" ]; then echo "  $1: $4: FAILED (no picture)"; FAIL=1; else differ keys-$1-$2 keys-$1-$3 "$1: $4"; fi; }
	ksame () { kin $1 || return 0; if [ ! -f "$OUT/keys-$1-$2.png" ] || [ ! -f "$OUT/keys-$1-$3.png" ]; then echo "  $1: $4: FAILED (no picture)"; FAIL=1; else same keys-$1-$2 keys-$1-$3 "$1: $4"; fi; }
	PDFDOC=$(find sdcard/manuals -name "*.pdf" | sort | head -1)
	rm -rf "$OUT/arc"; mkdir -p "$OUT/arc"; python3 $D/arc_sample.py "$OUT/arc/Projet-Onyx.zip" >/dev/null 2>&1 || true
	KLIST=${KEYS_APPS:-"notes tinypad fileviewer pdf media imageview iconedit fmtracker irc telegram disks taskman calendar archiver cardfile ledger lisa clock tinycalc graphcalc dlg-open dlg-save dlg-message dlg-colour dlg-font"}
	for a in $KLIST; do
		case $a in
		notes) kd $a 80 "@0;key x;$W;$(CT 0x0e)key y;$W;@1;${TAB}@2;$(K $DOWN)@3;$(K 13)key z;$W;@4;$(K $F6)$(K $RIGHT)@5" ;;
		tinypad) kd $a 40 "@0;key a;key b;$W;@1;$(K $F6)@2;$(K $RIGHT)$(K 13)@3;$(K 27)@4;$(CT 0x06)@5" SIM_ARGS=SD:/notes.txt ;;
		fileviewer) kd $a 60 "@0;$(K $DOWN)$(K $DOWN)@1;$(K 13)@2;$(K 0x7f)@3;$(K $RIGHT)@4;$(K $F6)@5" SIM_ARGS=SD:/apps ;;
		pdf) kd $a 200 "@0;$(K $PGDN)@1;${LL}@2;$(K $DOWN)$(K $DOWN)@3;$(K $RIGHT)@4;$(K $DOWN)$(K $DOWN)@5" SIM_SLEEP=1 SIM_ARGS=SD:/${PDFDOC#sdcard/} ;;
		media) kd $a 220 "@0;$(K $DOWN)@1;$(K 13)@2;$(K 0x20)@3;$(K $RIGHT)@4;${TAB}@5" SIM_SLEEP=1
			# (2026-10-26) the content by the keys: its cursor over the pieces, A on one, Bksp back
			KN=mediacur kd $a 220 "@0;$(K $DOWN)@1;$(K $DOWN)@2;$(K $RIGHT)@3;$(K $DOWN)@4;$(K 13)@5;$(K 0x08)@6" SIM_SLEEP=1 ;;
		imageview) kd $a 80 "@0;$(CT 0x0f)$W;@1;$(K 27)@2" ;;	# (no picture: the PC's build has no ImageKit -- its file chooser, then)
		iconedit) kd $a 60 "@0;$(K $RIGHT)$(K $DOWN)@1;key 0x2e;$W;key 0x2e;$W;$(K 13)@2;mods 2;$W;$(K $RIGHT)$(K $RIGHT)mods 0;$W;@3;key l;$W;$(K $DOWN)$(K 13)$(K $RIGHT)$(K $RIGHT)$(K $DOWN)@4;$(K 13)@5" SIM_ARGS=SD:/apps/invaders.app/icon.bmp ;;
		fmtracker) kd $a 60 "@0;$(K $DOWN)@1;$(K $RIGHT)@2;key z;$W;@3;$(K 0x20)@4;${TAB}@5" SIM_ARGS=SD:/music/fms/AIRWOLF.FMS ;;
		irc) kd $a 80 "@0;$(K $DOWN)@1;${TAB}@2;key h;key i;$W;@3;$(K 13)@4;$(K $F6)@5" ;;
		telegram) kd $a 80 "@0;$(K $DOWN)@1;$(K 13)@2;key h;key i;$W;@3;$(K 13)@4;$(K 27)@5" SIM_STAT=1 SIM_ARGS=--demo SIM_SERVICES=notify ;;
		disks) kd $a 80 "@0;$(K $DOWN)$(K $DOWN)@1;$(K $DOWN)$(K $DOWN)$(K $DOWN)@2;$(K $DOWN)@3;$(K $RIGHT)@4;$(K $DOWN)@5" SIM_USB=2 SIM_VOLS=SD1 ;;
		taskman) kd $a 80 "@0;$(K $DOWN)$(K $DOWN)@1;${RR}@2;${RR}@3;${LL}@4;${LL}@5" ;;
		calendar) kd $a 60 "@0;$(CT 0x0e)@1;key L;key u;key n;key c;key h;$W;${TAB}@2;$(K 27)@3;${LL}@4;$(K $DOWN)$(K 13)@5" SIM_DATE=20260930 SIM_TIME=1042 ;;
		archiver) kd $a 60 "@0;$(K $DOWN)$(K $DOWN)@1;$(K 13)@2;$(K 0x7f)@3;$(K $F6)@4;$(K $RIGHT)$(K 13)@5" SIM_RAM=$OUT/arc SIM_ARGS=RAM:/Projet-Onyx.zip ;;
		cardfile) kd $a 60 "@0;$(K $DOWN)@1;$(K $F6)@2;$(K 0x116)@3;$(K 0x114)@4;$(CT 0x06)@5" SIM_ARGS=SD:/docs/contacts.card ;;
		ledger) kd $a 80 "@0;$(K $DOWN)@1;$(K $RIGHT)@2;$(K 13)@3;${RR}@4;$(K $DOWN)@5" SIM_ARGS=SD:/docs/demo-company.ledger ;;
		lisa) kd $a 80 "@0;key h;key e;key l;key l;key o;$W;@1;$(K 13)$WW;@2;$(K $UP)@3;${TAB}@4;$(K $F6)@5" ;;
		clock) kd $a 60 "@0;${RR}@1;${RR}@2;$(K 0x20)@3;${RR}@4;${LL}@5" SIM_DATE=20260930 SIM_TIME=1042 ;;
		tinycalc) kd $a 80 "@0;key 1;key 2;key 0x2b;key 3;$W;@1;$(K 13)@2;$(K $RIGHT)@3;$(K $DOWN)@4;$(K 13)@5" ;;
		# THE SHARED DIALOGS every app opens, at 640 x 450: the file chooser (Open, Save As), a message box, the colour
		# chooser, Letters' font box
		dlg-open) KN=$a kd tinypad 40 "$(CT 0x0f)$W;@0;$(K $DOWN)$(K $DOWN)@1;$(K 13)$W;@2;$(K $DOWN)@3;${TAB}@4;$(K 27)@5" SIM_ARGS=SD:/notes.txt ;;
		dlg-save) KN=$a kd tinypad 40 "menucmd 3;$W;$W;@0;key a;key b;$W;@1;${TAB}@2;${TAB}@3;$(K $DOWN)@4;$(K 27)@5" SIM_ARGS=SD:/notes.txt ;;
		dlg-message) KN=$a kd tinypad 40 "key a;$W;$(CT 0x0e)$W;@0;$(K $RIGHT)@1;$(K $RIGHT)@2;$(K $LEFT)@3;$(K 27)@4;$(CT 0x0e)$W;$(K $RIGHT)$(K 13)@5" SIM_ARGS=SD:/notes.txt ;;
		dlg-colour) KN=$a kd iconedit 60 "menucmd 12;$W;$W;@0;$(K $RIGHT)@1;$(K $DOWN)@2;${TAB}@3;$(K 13)@4;$(K 27)@5" SIM_ARGS=SD:/apps/invaders.app/icon.bmp ;;
		dlg-font) KN=$a kd letters 80 "$(CT 0x04)$W;$W;@0;$(K $DOWN)@1;${TAB}@2;$(K $DOWN)@3;$(K $RIGHT)@4;$(K 27)@5" SIM_ARGS=SD:/docs/letters-tour.rtf ;;
		graphcalc) kd $a 80 "@0;$(K 27)@1;$(K $RIGHT)@2;key 0x2b;$W;@3;${TAB}@4;key x;$W;@5" ;;
		esac
	done
	# what each key must have done (the pictures were looked at: AUDIT-640.md has the verdicts)
	kdiff notes 0 1 "typing, Ctrl + N: a second note"; kdiff notes 1 2 "Tab: the list has the keys"; kdiff notes 2 3 "Down: the other note"; kdiff notes 4 5 "F6, Right: the ring on the toolbar"
	kdiff tinypad 0 1 "typing"
	kdiff fileviewer 0 1 "Down x 2: another folder"; kdiff fileviewer 1 2 "Enter: into it"; kdiff fileviewer 2 3 "Bksp: out of it"
	kdiff pdf 0 1 "Page Down scrolls"; kdiff pdf 1 2 "L: the drawer"; kdiff pdf 2 3 "Down x 2 in Pages: the third page"; kdiff pdf 3 4 "Right: Contents"; kdiff pdf 4 5 "Down x 2: another entry and its page"
	kdiff media 0 1 "Down: the search field has the keys"
	if kin media; then
		differ keys-mediacur-0 keys-mediacur-1 "media: Down: the keys' cursor on a piece of the page"
		differ keys-mediacur-2 keys-mediacur-3 "media: Right: the next piece"
		differ keys-mediacur-4 keys-mediacur-5 "media: A: the piece opened"
		differ keys-mediacur-5 keys-mediacur-6 "media: Bksp: back"
	fi
	kdiff imageview 0 1 "Ctrl + O: the file chooser"; ksame imageview 0 2 "Esc: closed"
	kdiff iconedit 0 1 "the arrows: a pixel has the keys"; kdiff iconedit 1 2 ". . and A: a pixel drawn in another colour"; kdiff iconedit 2 3 "Shift + Right x 2: a run"; kdiff iconedit 3 4 "L, A, the arrows: a line stretched"; kdiff iconedit 4 5 "A: the line kept"
	kdiff fmtracker 0 1 "Down: the next row"; kdiff fmtracker 1 2 "Right: the next column"
	kdiff irc 1 2 "Tab: the server's field"; kdiff irc 2 3 "typing in it"
	kdiff telegram 2 3 "typing a message"; kdiff telegram 3 4 "Enter: sent"
	kdiff disks 0 1 "Down x 2: a volume"; kdiff disks 1 2 "Down x 3: the last volume"; kdiff disks 2 3 "Down past the list's end: the focus goes on (the list was a trap)"
	kdiff taskman 0 1 "Down x 2: a task"; kdiff taskman 1 2 "R: the next tab"; kdiff taskman 2 3 "R: the next"; kdiff taskman 3 4 "L: back"
	kdiff calendar 0 1 "Ctrl + N: the new event's box"; kdiff calendar 1 2 "a title typed, Tab"; ksame calendar 0 3 "Esc: closed, nothing kept"; kdiff calendar 3 4 "L: the drawer"
	python3 -c "from PIL import Image; import sys; im = Image.open ('$OUT/keys-calendar-1.png').convert ('RGB'); sys.exit (0 if len (im.crop ((420, 430, 610, 462)).getcolors (4096)) > 2 else 1)" 2>/dev/null && echo "  calendar: the event box's Cancel and Save are on the screen: ok" || { kin calendar && { echo "  calendar: the event box's buttons: FAILED (not on the screen)"; FAIL=1; } || true; }
	kdiff archiver 0 1 "Down x 2: an entry"; kdiff archiver 1 2 "Enter: into the folder"; kdiff archiver 2 3 "Bksp: out of it"
	kdiff cardfile 0 1 "Down: the next field"; kdiff cardfile 1 2 "F6: the list"; kdiff cardfile 2 3 "F7: the design"; kdiff cardfile 3 4 "F5: the form"; kdiff cardfile 4 5 "Ctrl + F: the search"
	kdiff ledger 0 1 "Down: the rail has the keys"; kdiff ledger 3 4 "R: the next place"; kdiff ledger 4 5 "Down: the next row"
	kdiff lisa 0 1 "typing"
	kdiff clock 0 1 "R: Alarms"; kdiff clock 1 2 "R: Timer"; kdiff clock 2 3 "Space: the timer starts"; kdiff clock 3 4 "R: Stopwatch"; kdiff clock 4 5 "L: back"
	kdiff tinycalc 0 1 "1 2 + 3 typed"; kdiff tinycalc 1 2 "Enter: 15"
	kdiff graphcalc 0 1 "Esc: the graph has the keys"; kdiff graphcalc 1 2 "Right: the graph moved"; kdiff graphcalc 2 3 "+: nearer"; kdiff graphcalc 3 4 "Tab: a field again"
	kdiff dlg-open 0 1 "the file chooser: Down x 2"; kdiff dlg-open 1 2 "Enter: into the folder"; kdiff dlg-open 3 4 "Tab: its buttons"; kdiff dlg-open 4 5 "Esc: closed"
	kdiff dlg-save 0 1 "Save As: a name typed"; kdiff dlg-save 1 2 "Tab: Save"; kdiff dlg-save 2 3 "Tab: Cancel"
	kdiff dlg-message 0 1 "a message box: Right, a button ringed"; kdiff dlg-message 1 2 "Right: the next"; kdiff dlg-message 3 4 "Esc: closed"
	kdiff dlg-colour 0 1 "the colour chooser: Right, a slider"; kdiff dlg-colour 1 2 "Down: the next slider"; kdiff dlg-colour 3 4 "Enter: closed"
	kdiff dlg-font 0 1 "Letters' font box: Down, another size"; kdiff dlg-font 1 2 "Tab"; kdiff dlg-font 2 3 "Down"
fi

if want audit; then
	echo "pocket640: the audit (every app at 640 x 480 pocket)"
	A=${AUDIT_PNG:-$OUT/audit}; mkdir -p "$A"
	# au <app> <waits> [VAR=value ...]: built, run, its screen -> $A/audit-<app>-640.png
	au () {
		a=$1; n=$2; shift 2
		if ! pb $a >>"$OUT/log.txt" 2>&1 || [ ! -x "$OUT/pocket_$a" ]; then echo "  $a: NOT BUILT for the PC (see $OUT/log.txt)"; return 0; fi
		WR=$(langdir ""); ws=$(printf 'wait;%.0s' $(seq 1 $n))
		rm -f "$OUT/audit-$a-640.elsm"
		env SIM_OVERLAY=$D/sd $P640 SIM_APPNAME=$a SIM_APP=$a SIM_WRITES=$WR "$@" SIM="${ws}dump $OUT/audit-$a-640.elsm;exit" timeout 120 "$OUT/pocket_$a" >>"$OUT/log.txt" 2>&1 || true
		if [ -f "$OUT/audit-$a-640.elsm" ] && python3 $D/shot.py "$OUT/audit-$a-640.elsm" "$A/audit-$a-640.png" >/dev/null 2>&1; then echo "  $a: $A/audit-$a-640.png"
		else echo "  $a: NO PICTURE (it ended, or drew nothing)"; fi
	}
	# ap <applet>: a Control Panel applet, run at the size of the Control Panel's pane, then shown in it
	ftapp pocket control uikit_pocket >>"$OUT/log.txt" 2>&1
	ap () {
		a=$1
		if ! pb $a >>"$OUT/log.txt" 2>&1; then echo "  $a: NOT BUILT for the PC"; return 0; fi
		WR=$(langdir "")
		env SIM_OVERLAY=$D/sd $P640 SIM_APPNAME=control SIM_APP=control SIM_ARGS=$a SIM_WRITES=$WR SIM="$WWW;exit" "$OUT/pocket_control" > "$OUT/cp-$a.txt" 2>&1 || true
		pane=$(sed -n 's/^sim: surface_create //p' "$OUT/cp-$a.txt" | tail -1)
		[ -n "$pane" ] || { echo "  $a: the Control Panel made no surface for it"; return 0; }
		env SIM_OVERLAY=$D/sd SIM_APPLET=1 SIM_SURFSIZE=$pane $P640 SIM_APPNAME=$a SIM_APP=$a SIM_WRITES=$WR SIM="$W;$W;dump $OUT/audit-$a-ap.elsm;exit" timeout 120 "$OUT/pocket_$a" >>"$OUT/log.txt" 2>&1 || true
		env SIM_OVERLAY=$D/sd $P640 SIM_APPNAME=control SIM_APP=control SIM_ARGS=$a SIM_MAIL=40:7 SIM_SURFACE="$OUT/audit-$a-ap.elsm" SIM_WRITES=$WR SIM="$WWW;$WW;dump $OUT/audit-$a-640.elsm;exit" timeout 120 "$OUT/pocket_control" >>"$OUT/log.txt" 2>&1 || true
		if python3 $D/shot.py "$OUT/audit-$a-640.elsm" "$A/audit-$a-640.png" >/dev/null 2>&1; then echo "  $a ($pane): $A/audit-$a-640.png"; else echo "  $a: NO PICTURE"; fi
	}
	LIST=${AUDIT_APPS:-"letters sheet slides paint notes stickies tinypad rtfview cardfile calendar agenda clock tinycalc graphcalc fileviewer archiver pdf photos media imageview iconedit fmtracker koton irc telegram courier ledger pkgman disks taskman terminal screenshot setup control qbstudio qbasic 3dforge gamelib widgets lisa life mandelbrot eyes lock shutdown theme modeconf displayconf dockconf soundconf preloadconf keyconf langconf printconf padconf wpaconf config"}
	rm -rf "$OUT/arc"; mkdir -p "$OUT/arc"; python3 $D/arc_sample.py "$OUT/arc/Projet-Onyx.zip" >/dev/null 2>&1 || true
	PDFDOC=$(find sdcard/manuals -name "*.pdf" | sort | head -1)
	for a in $LIST; do
		case $a in
		letters) au $a 80 SIM_ARGS=SD:/docs/letters-tour.rtf ;;
		sheet) au $a 80 SIM_ARGS=SD:/docs/cafe-2026.xlsx ;;
		paint) au $a 80 SIM_ARGS=SD:/docs/pictures/sunny-mountains.jpg ;;
		tinypad) au $a 40 SIM_ARGS=SD:/notes.txt ;;
		rtfview) au $a 60 SIM_ARGS=SD:/docs/onyx-rtf-sample.rtf ;;
		cardfile) au $a 60 SIM_ARGS=SD:/docs/contacts.card ;;
		fileviewer) au $a 60 SIM_ARGS=SD:/apps ;;
		archiver) au $a 60 SIM_RAM=$OUT/arc SIM_ARGS=RAM:/Projet-Onyx.zip ;;
		pdf) au $a 200 SIM_SLEEP=1 SIM_ARGS=SD:/${PDFDOC#sdcard/} ;;
		iconedit) au $a 60 SIM_ARGS=SD:/apps/invaders.app/icon.bmp ;;
		fmtracker) au $a 60 SIM_ARGS=SD:/music/fms/AIRWOLF.FMS ;;
		telegram) au $a 80 SIM_STAT=1 SIM_ARGS=--demo SIM_SERVICES=notify ;;
		ledger) au $a 80 SIM_ARGS=SD:/docs/demo-company.ledger ;;
		calendar|agenda|clock) au $a 60 SIM_DATE=20260930 SIM_TIME=1042 ;;
		media|photos) au $a 220 SIM_SLEEP=1 ;;
		qbstudio) au $a 80 SIM_ARGS=SD:/projects/pages ;;
		theme|modeconf|displayconf|dockconf|soundconf|preloadconf|keyconf|langconf|printconf|padconf|wpaconf|config) ap $a ;;
		*) au $a 80 ;;
		esac
	done
fi

grep -h "server_sim: FAIL" "$OUT/log.txt" && FAIL=1
echo "  checks: $(grep -c 'server_sim: PASS' "$OUT/log.txt") passed, $(grep -c 'server_sim: FAIL' "$OUT/log.txt") failed"
[ $FAIL = 0 ] && echo "pocket640: all passed ($OUT)" || echo "pocket640: FAILED ($OUT)"
exit $FAIL
