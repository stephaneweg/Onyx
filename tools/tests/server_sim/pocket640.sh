#!/bin/sh
# tools/tests/server_sim/pocket640.sh -- the apps in POCKET mode on a 640 x 480 screen without a mouse (the Waveshare
# PocketTerm35: a D-pad that sends the arrows, A = Enter, B = Esc, L / R = Ctrl + Page Up / Down, a thumb keyboard).
# Real apps built against the pocket UIKit's wire port, run under PocketUI at 640 x 480, driven BY KEYS ONLY, their
# screens dumped to PNG pictures and compared:
#   - mail: three screens -- the accounts and their folders, the folder's messages, the message -- each with its "<"
#     bar; Enter goes in, Esc comes back one screen; writing a message;
#   - circuits, gpiolab, turtle: the side panels one at a time, nothing cut, every panel reached by the keys;
#   - drawers: UIKit's rule -- a side panel without places opens and closes by L / R (the Calendar, the Icon Editor);
#   - audit: every other app with a window, captured (docs/compact-shell/real/pocketterm/AUDIT-640.md).
# Usage: sh tools/tests/server_sim/pocket640.sh [out dir] [only: mail circuits gpiolab turtle drawers audit]  (AUDIT_APPS="a b": those apps)
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
		mrun pt-mail-640-write "" "key 14;$W;$W;dump $OUT/pt-mail-640-compose.elsm;key 27;$W;$W;dump $OUT/pt-mail-640-compose-back.elsm"
		out pt-mail-640-compose; out pt-mail-640-compose-back
		differ pt-mail-640-1-folders pt-mail-640-compose "Ctrl+N: the writing screen"
		barsame pt-mail-640-1-folders pt-mail-640-compose-back "Esc on an empty message: the folders again"
		# a reply from the message (Ctrl+R): the writing screen over it, Esc back to the message
		mrun pt-mail-640-reply "" "$(K 13)$(K 13)$W40;key 18;$W;$W;dump $OUT/pt-mail-640-reply.elsm"
		out pt-mail-640-reply
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
	# the board by the keys: the arrows bring its pointer, 1 arms the first gate, Enter puts it; Enter on it takes it, the
	# arrows move it, Enter puts it down
	WR=$(CQ "")
	run pocket_circuits pt-circuits-640-keys "$WWW;$(K $RIGHT)$(K $UP)$(K $UP)$(K $UP)$(K $UP)$(K $UP)dump $OUT/pt-circuits-640-pointer.elsm;key 0x31;$W;dump $OUT/pt-circuits-640-armed.elsm;$(K 13)dump $OUT/pt-circuits-640-put.elsm" $C SIM_WRITES=$WR
	for f in pt-circuits-640-pointer pt-circuits-640-armed pt-circuits-640-put; do out $f; done
	differ pt-circuits-640 pt-circuits-640-pointer "the arrows bring the board's pointer"
	differ pt-circuits-640-pointer pt-circuits-640-armed "1 arms a gate at the pointer"
	differ pt-circuits-640-armed pt-circuits-640-put "Enter puts the gate"
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
	WR=$(TQ "" maze)
	R12=$(printf 'wait;%.0s' $(seq 1 120))
	run pocket_turtle pt-turtle-640-run "$WWW;key 0x114;${R12}dump $OUT/pt-turtle-640-run.elsm" $T SIM_WRITES=$WR
	out pt-turtle-640-run
	differ pt-turtle-640 pt-turtle-640-run "F5 runs the program"
	WR=$(TQ fr star-fr)
	run pocket_turtle pt-turtle-640-fr "$WWW;dump $OUT/pt-turtle-640-fr.elsm" $T SIM_WRITES=$WR
	out pt-turtle-640-fr
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
