#!/bin/sh
# tools/tests/server_sim/run.sh -- Onyx's graphics servers on the PC, a real app as their client (server_sim.cpp):
#
#   1. PocketUI (user/Servers/common/ + user/Servers/pocketui/wm.cpp, band.cpp) with apps built against the pocket
#      UIKit's wire port (UK_PORT_POCKET + UK_PORT_WIRE: the port of SD:/lib/pocket/uikit.so): the Terminal (resizable:
#      filled, frameless, under the status band) and the Calculator (fixed: a card) at 800 x 480, the console mode,
#      the policy's checks (expect ...: the kinds, the frames, the work area, one app in front, Alt+Tab, a program
#      started or raised comes in front, cards do not move, the global menu bar takes the top band and shows the front
#      app's menus, the dock and the desktop's backmost windows refused) -- and the screenshots, into the output folder;
#   2. Elegant after the extraction of user/Servers/common/ against Elegant BEFORE it (the sources of git's HEAD~,
#      or of the revision in ELEGANT_BEFORE): the same app, the same script, the composed screens compared pixel
#      for pixel (its behaviour unchanged) -- skipped when that revision has no user/Servers/elegant/wm.
#
# Needs g++, python3 with Pillow + numpy. Usage: sh tools/tests/server_sim/run.sh [out dir]
# Exit status: 0 when every check passed and Elegant's pictures are identical.
set -e
cd "$(dirname "$0")/../../.."
. tools/tests/server_sim/common.sh
ftapp pocket pocketshell uikit_pocket
ftapp pocket menubar uikit_pocket user/Apps/clock/alarms.cpp user/Apps/clock/clocktime.cpp
app pocket terminal uikit_pocket
app pocket tinycalc uikit_pocket
app pocket tinypad uikit_pocket
app elegant tinycalc uikit_wire
app elegant terminal uikit_wire

echo "server_sim: PocketUI"
# the Terminal: resizable -> filled under the band, no frame; Alt+Tab with another program (a card) -- one in front
run pocket_terminal pocket-terminal "$WW;expect kind fill;expect frame 0;expect area 0,24,800,456;expect client 800,456;expect pos 0,24;expect front app;expect hidden 0;dump $OUT/pocket-terminal.elsm;other 300 200 Notes;$W;expect front other;expect aside 1;expect hidden 1;dump $OUT/pocket-card-over.elsm;mods 4;key 0x09;mods 0;$W;expect front app;expect aside 0;expect hidden 0;$W" \
	SIM_SCREEN=800x480 SIM_APPNAME=terminal SIM_PIPE="$PIPE"
# the Calculator: small, not resizable -> a card, framed, centred
run pocket_tinycalc pocket-calculator "$W;expect kind card;expect frame 1;expect front app;key 1;key 2;key *;key 3;key .;key 5;key =;$W;dump $OUT/pocket-calculator.elsm" \
	SIM_SCREEN=800x480 SIM_APPNAME=tinycalc
# a window bigger than the screen (the Calculator on a 320 x 240 screen): made all the same -- filled, frameless, cut
run pocket_tinycalc pocket-too-big "$W;expect kind fill;expect frame 0;expect pos 0,24;expect front app" SIM_SCREEN=320x240 SIM_APPNAME=tinycalc
# console: the same policy, no band
run pocket_terminal console-terminal "$WW;expect area 0,0,800,480;expect client 800,480;expect frame 0;dump $OUT/console-terminal.elsm" \
	SIM_SCREEN=800x480 SIM_MODE=console SIM_APPNAME=terminal SIM_PIPE="$PIPE"
# 640 x 480: the Terminal filled again
run pocket_terminal pocket-terminal-640 "$WW;expect client 640,456;dump $OUT/pocket-terminal-640.elsm" SIM_SCREEN=640x480 SIM_APPNAME=terminal SIM_PIPE="$PIPE"
# a program started over another one comes in front (tinypad filled, then a card): the first set aside -- also once the
# filled one was told its size again; raised by its own request (EL_OP_WIN_RAISE), the first comes back
SC="$WW;expect kind fill;expect front app;other 280 296 Calc;$W;expect front other;expect aside 1;$WW;$WW;expect front other;expect aside 1"
SC="$SC;screen $OUT/pocket-front.elsm;mods 4;key 0x09;mods 0;$W;expect front app;expect aside 0;raise;$W;expect front other;expect aside 1"
run pocket_tinypad pocket-front "$SC" SIM_SCREEN=800x480 SIM_APPNAME=tinypad
run pocket_tinypad pocket-front-1080 "$WW;other 280 296 Calc;$WW;expect front other;expect aside 1" SIM_SCREEN=1920x1080 SIM_APPNAME=tinypad
# a card does not move: its title bar dragged, a move asked by its program -- it stays centred (the Calculator: a card,
# SD:/etc/pocketui.ini's [cards]); another program's fixed main window is centred, frameless (PK_KIND_CENTRE), and stays
run pocket_tinycalc pocket-card-fixed "$W;expect pos 256,88;down 400 96;move 450 150;move 500 200;up 500 200;$W;expect pos 256,88;other 300 200 Calc;$W;expect okind centre;expect opos 250,152;place 10 300;$W;expect opos 250,152" \
	SIM_SCREEN=800x480 SIM_APPNAME=tinycalc
# the desktop's global menu bar (a topmost window across the top edge: flags 0x35) is the top band: PocketUI's band
# away, the work area under it, its menus the front app's (a filled window's too); the dock (on the bottom edge,
# narrower than half the screen at 1920 x 1080) and a backmost window (the agenda, the stickies: 0x33) refused
SC="$WW;other 800 30 menubar 0x35 0 0;expect made 1;expect bar 1;$W;expect band 0;expect area 0,30,800,450;expect menu tinypad;$WW"
SC="$SC;expect client 800,450;expect pos 0,30;other 784 89 dock 0x35 8 391;expect made 0;other 300 300 agenda 0x33 10 10;expect made 0"
SC="$SC;other 280 296 Calc;$W;expect menu Calc;expect front other;screen $OUT/pocket-menubar.elsm"
run pocket_tinypad pocket-menubar "$SC" SIM_SCREEN=800x480 SIM_APPNAME=tinypad
run pocket_tinypad pocket-dock-1080 "$WW;other 839 92 dock 0x35 540 988;expect made 0;expect band 1" SIM_SCREEN=1920x1080 SIM_APPNAME=tinypad
# console: no menu bar
run pocket_tinypad console-menubar "$WW;other 800 30 menubar 0x35 0 0;expect made 0;expect bar 0;expect area 0,0,800,480" \
	SIM_SCREEN=800x480 SIM_MODE=console SIM_APPNAME=tinypad
# THE FULL-SCREEN RULE (the user, 2026-10-08): every app's main window fills the work area (frameless), whatever its size;
# a fixed one smaller than the work area is centred over a matte of its background colour; the apps of
# SD:/etc/pocketui.ini [cards] (and app.txt "pocket = card") are cards; a program's other windows are cards. At
# 800 x 480 and 1920 x 1080: Letters, the File Viewer, the Terminal, the Text Editor filled -- and the keys typed reach
# them (also with a window of no app standing above: the keys follow the front program); the Calculator a card (listed),
# centred over its matte when the list does not name it; a program's second window (a dialog) a card.
ftapp pocket fileviewer uikit_pocket
ftapp pocket letters uikit_pocket -Wl,--unresolved-symbols=ignore-all	# (its printing: PrinterKit is not linked on the PC, never called here)
NOCARDS="$OUT/writes-nocards"; rm -rf "$NOCARDS"; mkdir -p "$NOCARDS/etc"; printf '[cards]\n# none\n' > "$NOCARDS/etc/pocketui.ini"
for sz in 800x480 1920x1080; do
	w=${sz%x*}; h=${sz#*x}; A="expect kind fill;expect frame 0;expect pos 0,24;expect client $w,$((h - 24));expect focus app"
	run pocket_letters full-letters-$sz "$WW;$WW;$A;key H;key i;$W;expect keys 2" SIM_SCREEN=$sz SIM_APPNAME=letters SIM_APP=letters
	# (the File Viewer: resizable since P6 -- filled at both sizes; the Calculator below is the centred fixed app)
	run pocket_fileviewer full-fileviewer-$sz "$WW;$WW;$A;expect matte 0" SIM_SCREEN=$sz SIM_APPNAME=fileviewer SIM_APP=fileviewer
	run pocket_terminal full-terminal-$sz "$WW;$A;key l;key s;$W;expect keys 2" SIM_SCREEN=$sz SIM_APPNAME=terminal SIM_PIPE="$PIPE"
	run pocket_tinypad full-tinypad-$sz "$WW;$A;key a;$W;expect keys 1;other 200 100 blocker 0x11 10 300;$W;key b;key c;$W;expect keys 3;expect focus app" \
		SIM_SCREEN=$sz SIM_APPNAME=tinypad
	run pocket_tinycalc full-calc-card-$sz "$W;expect kind card;expect frame 1;expect matte 0" SIM_SCREEN=$sz SIM_APPNAME=tinycalc
	run pocket_tinycalc full-calc-centre-$sz "$W;$W;expect kind centre;expect frame 0;expect matte 1;expect focus app;key 7;$W;expect keys 1;dump $OUT/pocket-centre-$sz.elsm" \
		SIM_SCREEN=$sz SIM_APPNAME=tinycalc SIM_WRITES=$NOCARDS
	run pocket_tinypad full-dialog-$sz "$WW;other 400 300 notes;$W;expect okind centre;owin 200 120 About;$W;expect okind1 card;expect made 1" SIM_SCREEN=$sz SIM_APPNAME=tinypad
done
png pocket-centre-800x480
# THE FULL SCREEN (a BASIC program's way, Arkanoid: a window, then uk_win_fullscreen_begin; tools/tests/server_sim/fsapp.cpp):
# the server told, its state in the windows' list (rdpd's, vncd's), its window at 0,0 and left there by the policy, the
# keys its own (Alt+Tab too), nothing set aside; given back: the window centred again, the keys, the front app
$CXX $INC -o "$OUT/pocket_fsapp" "$OUT/obj/pocket"/*.o "$OUT/fakekapi.o" $S/fsapp.cpp "$OUT/libuikit_pocket.a" -lpthread
for sz in 800x480 1920x1080; do
	w=${sz%x*}; h=${sz#*x}; CP="$(((w - 320) / 2)),$((24 + (h - 24 - 200) / 2))"
	SC="$W;$W;expect kind centre;expect pos $CP;expect full 0;key f;$W;$W;expect full 1;expect pos 0,0;expect focus app;expect keys 1"
	SC="$SC;mods 4;key 0x09;mods 0;key x;$W;$W;expect keys 3;expect full 1;expect pos 0,0;expect front app"
	SC="$SC;other 300 200 notes;$W;$W;expect full 1;expect pos 0,0;key g;$W;$W;expect full 0;expect pos $CP;expect focus other"
	run pocket_fsapp fullscreen-$sz "$SC" SIM_SCREEN=$sz SIM_APPNAME=fsapp
done
# ... and on the desktop (Elegant: an emulator's full screen, its framed window): the list says it, frameless, at 0, 0
# (rdpd tells it alone, a plain window: tools/tests/run_rdpd_test.sh MOCK_POCKET=3, 4)
$CXX $INC -o "$OUT/elegant_fsapp" "$OUT/obj/elegant"/*.o "$OUT/fakekapi.o" $S/fsapp.cpp "$OUT/libuikit_wire.a" -lpthread
run elegant_fsapp elegant-fullscreen "$W;$W;expect full 0;key f;$W;$W;expect full 1;expect focus app;expect keys 1;key g;$W;$W;expect full 0" SIM_SCREEN=1024x768 SIM_APPNAME=fsapp
for p in pocket-terminal pocket-card-over pocket-calculator console-terminal pocket-terminal-640 pocket-front pocket-menubar; do png $p; done
# GPIO Lab filled (the Pi's report on 2026.10.126: "a bit too wide and too tall"): its window exactly the work area, its
# layout (LabRoot::onResized) inside it -- the pictures pocket-gpiolab-<size>.png (its Code view: -code)
mkdir -p "$OUT/glz"
for f in adler32 crc32 deflate inflate inffast inftrees trees zutil; do gcc -O2 -w -c third_party/zlib-1.3.1/$f.c -o "$OUT/glz/$f.o"; done
ftapp pocket gpiolab uikit_pocket -Ithird_party/zlib-1.3.1 user/Kits/gpiokit/gkcore.cpp user/Kits/filekit/fkcore.cpp "$OUT"/glz/*.o \
	user/Libs/basic/bascomp.cpp user/Libs/basic/basvm.cpp user/Libs/basic/basnum.cpp user/Libs/basic/basbax.cpp
GW=$(printf 'wait;%.0s' $(seq 1 30))
for sz in 1920x1080 1280x720; do
	w=${sz%x*}; h=${sz#*x}
	run pocket_gpiolab gpiolab-$sz "${GW}expect kind fill;expect pos 0,24;expect client $w,$((h - 24));dump $OUT/pocket-gpiolab-$sz.elsm" SIM_SCREEN=$sz SIM_APPNAME=gpiolab "SIM_ARGS=--demo --tab chart"
	run pocket_gpiolab gpiolab-code-$sz "${GW}expect client $w,$((h - 24));dump $OUT/pocket-gpiolab-code-$sz.elsm" SIM_SCREEN=$sz SIM_APPNAME=gpiolab "SIM_ARGS=--demo --code"
	png pocket-gpiolab-$sz; png pocket-gpiolab-code-$sz
done

# ---- the pocket shell (pocketshell, phase P5): the launcher, the search, the switcher, quick settings ----------
# The real pocketshell as the client; the other programs are canvases painted from the real apps' pictures (the
# Terminal, the Text Editor, the Calculator run at that size first) and the real menu bar's top band (run at that size,
# its band dumped). The checks: the shell registered, its home backmost at the work area, home when no app is in front,
# the keys (the launcher's when home; Super, Alt+Tab, Super+N taken from the front app), the tasks, the overlays shown /
# parked, the front app after the switcher. The pictures: pocketshell-<shot>-<size>[-fr].png in the output folder
# (SHELL_PNG=<folder>: copied there too -- docs/compact-shell/real/).
echo "server_sim: the pocket shell"
NOTES='1:3:Telegram\0Marie: on se voit demain ? -- 2 unread\0telegram\n1:3:Packages\03 updates: onyx 2026.10.125, jet, ledger\0control pkgman\n1:3:Calendar\0Dentist at 17:30\0'
# langdir <code>: the writes' folder with etc/system.ini's "language=" (empty: English)
langdir () { d="$OUT/writes-$1"; rm -rf "$d"; mkdir -p "$d/etc"; if [ -n "$1" ]; then { grep -v '^language' sdcard/etc/system.ini; echo "language=$1"; } > "$d/etc/system.ini"; fi; echo "$d"; }
shellshots () {		# shellshots <W>x<H> <tag> <shots: home search switcher quick> [lang]
	sz=$1; t=$2; want=" $3 "; lg=$4; w=${sz%x*}; h=${sz#*x}; sfx=$t${lg:+-$lg}
	WR=$(langdir "$lg")
	run pocket_terminal app-term-$sfx "$WW;dump $OUT/app-term-$sfx.elsm" SIM_SCREEN=$sz SIM_APPNAME=terminal SIM_APP=terminal SIM_PIPE="$PIPE" SIM_WRITES=$WR
	run pocket_tinypad app-pad-$sfx "$WW;dump $OUT/app-pad-$sfx.elsm" SIM_SCREEN=$sz SIM_APPNAME=tinypad SIM_APP=tinypad SIM_WRITES=$WR
	run pocket_menubar bar-home-$sfx "$W;$W;dump $OUT/bar-home-$sfx.elsm" SIM_SCREEN=$sz SIM_APPNAME=menubar SIM_APP=menubar SIM_WRITES=$WR
	run pocket_menubar bar-term-$sfx "other $w $((h - 30)) Terminal;othermenu MShell|I1~New Tab~|MEdit|I2~Copy~|MView|I3~Bigger~|MTabs|I4~Next~;$W;$W;$W;dump $OUT/bar-term-$sfx.elsm" \
		SIM_SCREEN=$sz SIM_APPNAME=menubar SIM_APP=menubar SIM_WRITES=$WR
	APPS="otherpic $w $((h - 30)) tinypad $OUT/app-pad-$sfx.elsm 0 24;$W;otherpic 260 300 tinycalc $OUT/pocket-calculator.elsm 262 115 0x40;$W;otherpic $w $((h - 30)) terminal $OUT/app-term-$sfx.elsm 0 24;$W"
	E="SIM_SCREEN=$sz SIM_APPNAME=pocketshell SIM_APP=pocketshell SIM_WRITES=$WR"
	case "$want" in *" home "*|*" search "*)
		SC="otherpic $w 30 menubar $OUT/bar-home-$sfx.elsm 0 0 0x35 0 0;$W;$W;expect shell app;expect kind home;expect home 1;$APPS;expect front other;expect home 0;expect tasks 3"
		SC="$SC;mods 8;mods 0;$W;$W;expect home 1;expect focus app;expect aside 0;expect area 0,30,$w,$((h - 30));expect pos 0,30;dump $OUT/pocketshell-home-$sfx.elsm"
		SC="$SC;key t;key e;$W;$W;expect keys 2;dump $OUT/pocketshell-search-$sfx.elsm;key 0x1b;$W;mods 8;mods 0;$W;$W;expect home 0;expect front other"
		run pocket_pocketshell shell-home-$sfx "$SC" $E ;;
	esac
	case "$want" in *" switcher "*|*" quick "*)
		SC="otherpic $w 30 menubar $OUT/bar-term-$sfx.elsm 0 0 0x35 0 0;$W;$W;$APPS;expect front other;expect shown1 0"
		SW="$SC;mods 4;key 0x09;$W;$W;expect shown1 1;expect front other;dump $OUT/pocketshell-switcher-$sfx.elsm;key 0x09;mods 0;$W;$W;expect shown1 0;expect front other"
		run pocket_pocketshell shell-switcher-$sfx "$SW" $E
		QS="$SC;$WW;$WW;$WW;mods 8;key n;mods 0;$W;$W;expect shown1 1;expect keys 0;dump $OUT/pocketshell-quick-$sfx.elsm;key 0x1b;$W;expect shown1 0"
		run pocket_pocketshell shell-quick-$sfx "$QS" $E SIM_MBOX="$NOTES" ;;
	esac
	for s in home search switcher quick; do
		case "$want" in *" $s "*) png pocketshell-$s-$sfx; if [ -n "$SHELL_PNG" ]; then mkdir -p "$SHELL_PNG"; cp "$OUT/pocketshell-$s-$sfx.png" "$SHELL_PNG/"; fi ;; esac
	done
}
shellshots 800x480 800 "home search switcher quick"
shellshots 1920x1080 1080 "home search switcher quick"
shellshots 1280x720 720 "home switcher"
shellshots 640x480 640 "home switcher"
shellshots 480x800 portrait "home search switcher quick"
shellshots 800x480 800 "home search switcher quick" fr
# console: the shell's home is the whole screen (no band)
run pocket_pocketshell shell-console "$W;$W;expect shell app;expect kind home;expect area 0,0,800,480;expect pos 0,0" SIM_SCREEN=800x480 SIM_MODE=console SIM_APPNAME=pocketshell
# the launcher's keys from the start (the Pi's report on 2026.10.126: "the shell does not react to the keyboard"):
# typing goes to the search (calc), Down / Up choose, Enter opens the result (the Calculator); "qqq" (no app: its only
# result is to run it) then Esc clears the search, the arrows move in the grid (Right x3, Left: its 3rd app), Enter
# opens it (not "qqq" run)
n0=$(grep -c "^sim: launch" "$OUT/log.txt" || true)
SK="otherpic 1920 30 menubar $OUT/bar-home-1080.elsm 0 0 0x35 0 0;$W;$W;expect home 1;key c;key a;key l;key c;$W;key 0x101;key 0x100;$W;key 13;$W"
SK="$SK;key q;key q;key q;$W;key 0x1b;$W;key 0x103;key 0x103;key 0x103;key 0x102;$W;key 13;$W;$W;expect keys 16;expect home 1"
run pocket_pocketshell shell-keys-1080 "$SK" SIM_SCREEN=1920x1080 SIM_APPNAME=pocketshell SIM_APP=pocketshell SIM_WRITES=$(langdir "")
L=$(grep "^sim: launch" "$OUT/log.txt" | tail -n +$((n0 + 1)) | tr '\n' ' ')
case "$L" in "sim: launch tinycalc sim: launch terminal ") ;; "sim: launch tinycalc sim: launch "?*" ") L=ok ;; esac	# (terminal: "qqq" not cleared, run)
if [ "$L" = ok ]; then echo "  shell-keys-1080: the search's result and the grid's app opened"
else echo "  shell-keys-1080: FAILED (opened: $L)"; FAIL=1; fi
grep -h "server_sim: FAIL" "$OUT/log.txt" && FAIL=1
echo "  checks: $(grep -c 'server_sim: PASS' "$OUT/log.txt") passed, $(grep -c 'server_sim: FAIL' "$OUT/log.txt") failed"

# ---- Elegant: after the extraction, against before it ------------------------------------------------------
echo "server_sim: Elegant, before and after user/Servers/common/"
REV=${ELEGANT_BEFORE:-HEAD}
OLD="$OUT/before"
rm -rf "$OLD"; mkdir -p "$OLD/wm/kern/gui"
if git cat-file -e "$REV:user/Servers/elegant/wm/window.cpp" 2>/dev/null; then
	for f in core.cpp core.h corepriv.h ops.cpp wm/window.cpp wm/cursors.inc wm/kern/gui/window.h; do git show "$REV:user/Servers/elegant/$f" > "$OLD/$f"; done
	server elegant_old "$OLD/core.cpp $OLD/ops.cpp $OLD/wm/window.cpp kernel/gui/gimage.cpp" "-I $OLD/wm -I $OLD -DSIM_OLD"
	app elegant_old tinycalc uikit_wire
	app elegant_old terminal uikit_wire
	SC_CALC="$W;key 1;key 2;key *;key 3;key .;key 5;key =;move 300 300;$W"
	SC_TERM="$W;$W;move 200 200;down 200 200;up 200 200;$W"
	for v in elegant elegant_old; do
		run ${v}_tinycalc $v-calculator "$SC_CALC;dump $OUT/$v-calculator.elsm" SIM_APPNAME=tinycalc
		run ${v}_terminal $v-terminal "$SC_TERM;dump $OUT/$v-terminal.elsm" SIM_APPNAME=terminal SIM_PIPE="$PIPE"
	done
	for a in calculator terminal; do
		if cmp -s "$OUT/elegant-$a.elsm" "$OUT/elegant_old-$a.elsm"; then echo "  $a: the same pixels"
		else echo "  $a: DIFFERENT pixels (elegant-$a.elsm / elegant_old-$a.elsm)"; FAIL=1; fi
	done
	png elegant-calculator; png elegant-terminal
else
	echo "  (skipped: $REV has no user/Servers/elegant/wm -- set ELEGANT_BEFORE to a revision before the extraction)"
fi
# ---- Elegant: the common code now against the revision before (HEAD, or COMMON_BEFORE) ---------------------
# PocketUI's phases change user/Servers/common/ (P5: the policy's mods hook, the Super bit kept from the window
# manager, el_core_xfer, el_core_next_key, CWindow::SetNoInset -- none of which Elegant uses): Elegant built from
# both, the same apps and scripts (keys, a drag, Alt+Tab, Super held), the composed screens compared pixel for pixel.
REVC=${COMMON_BEFORE:-HEAD}
echo "server_sim: Elegant, user/Servers/common/ of $REVC against the working tree"
if git cat-file -e "$REVC:user/Servers/common/core.cpp" 2>/dev/null; then
	OC="$OUT/common-before"; rm -rf "$OC"; mkdir -p "$OC"
	git archive "$REVC" user/Servers/common | tar -x -C "$OC"
	C2=$OC/user/Servers/common
	server elegant_head "$C2/core.cpp $C2/ops.cpp $C2/route.cpp $C2/wm/window.cpp kernel/gui/gimage.cpp" "-I $C2/wm -I $C2"
	server elegant "$C/core.cpp $C/ops.cpp $C/route.cpp $C/wm/window.cpp kernel/gui/gimage.cpp" "-I $C/wm -I $C"
	for v in elegant elegant_head; do app $v tinycalc uikit_wire; app $v terminal uikit_wire; app $v tinypad uikit_wire; done
	SC_PAD="$W;key H;key i;mods 8;key x;mods 0;mods 4;key 0x09;mods 0;other 300 200 Notes;$W;mods 4;key 0x09;mods 0;key !;$W;move 300 300;down 300 300;up 300 300;$W"
	for v in elegant elegant_head; do
		run ${v}_tinycalc $v-cmp-calculator "$W;key 1;key 2;key *;key 3;key =;move 300 300;$W;dump $OUT/$v-cmp-calculator.elsm" SIM_APPNAME=tinycalc
		run ${v}_terminal $v-cmp-terminal "$W;$W;move 200 200;down 200 200;up 200 200;$W;dump $OUT/$v-cmp-terminal.elsm" SIM_APPNAME=terminal SIM_PIPE="$PIPE"
		run ${v}_tinypad $v-cmp-tinypad "$SC_PAD;dump $OUT/$v-cmp-tinypad.elsm" SIM_APPNAME=tinypad
	done
	for a in calculator terminal tinypad; do
		if cmp -s "$OUT/elegant-cmp-$a.elsm" "$OUT/elegant_head-cmp-$a.elsm"; then echo "  $a: the same pixels"
		else echo "  $a: DIFFERENT pixels (elegant-cmp-$a.elsm / elegant_head-cmp-$a.elsm)"; FAIL=1; fi
	done
else
	echo "  (skipped: $REVC has no user/Servers/common)"
fi
[ $FAIL = 0 ] && echo "server_sim: all passed ($OUT)" || echo "server_sim: FAILED ($OUT)"
exit $FAIL
