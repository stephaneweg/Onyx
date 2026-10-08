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
OUT=${1:-/tmp/server_sim}
mkdir -p "$OUT/obj"
D=tools/tests/desktop_sim
S=tools/tests/server_sim
INC="-I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include"
CXX="g++ -std=gnu++17 -O1 -w -fno-exceptions -fno-rtti"
SRV="-DWIN_PIXELS_HOOK -I $D/kstub"
FAIL=0
: > "$OUT/log.txt"

# ---- UIKit (two wire ports), the stand-in kernel, the apps' objects -------------------------------------------
build_uikit () {	# build_uikit <name> <flags>: UIKit's objects with that port -> $OUT/lib<name>.a
	mkdir -p "$OUT/obj/$1"
	for f in user/Kits/uikit/*.cpp; do $CXX $INC -DIMG_HOST_TEST $2 -c "$f" -o "$OUT/obj/$1/$(basename "$f" .cpp).o" & done; wait
	rm -f "$OUT/lib$1.a"; ar rcs "$OUT/lib$1.a" "$OUT/obj/$1"/*.o
}
build_uikit uikit_pocket "-DUK_PORT_WIRE -DUK_PORT_POCKET"
build_uikit uikit_wire "-DUK_PORT_WIRE"
$CXX $INC -c $D/fakekapi.cpp -o "$OUT/fakekapi.o"

# ---- the servers' objects -------------------------------------------------------------------------------------
# server <name> <sources> <flags>: the server's code for the host -> $OUT/obj/<name>/*.o
server () {
	mkdir -p "$OUT/obj/$1"; rm -f "$OUT/obj/$1"/*.o
	for f in $2; do $CXX $SRV $3 $INC -c "$f" -o "$OUT/obj/$1/$(basename "$f" .cpp).o" || return 1; done
	$CXX $SRV $3 $INC -DSIM_$(echo $1 | tr a-z A-Z) -c $S/server_sim.cpp -o "$OUT/obj/$1/server_sim.o"
	$CXX -c $S/sim_mem.cpp -o "$OUT/obj/$1/sim_mem.o"
}
C=user/Servers/common
server pocket "$C/core.cpp $C/ops.cpp $C/route.cpp $C/wm/window.cpp kernel/gui/gimage.cpp user/Servers/pocketui/wm.cpp user/Servers/pocketui/band.cpp" \
	"-I $C/wm -I $C -I user/Servers/pocketui"
server elegant "$C/core.cpp $C/ops.cpp $C/route.cpp $C/wm/window.cpp kernel/gui/gimage.cpp" "-I $C/wm -I $C"

# app <server> <app> <uikit>: the app linked with that server and UIKit -> $OUT/<server>_<app>
app () {
	$CXX $INC -o "$OUT/$1_$2" "$OUT/obj/$1"/*.o "$OUT/fakekapi.o" user/Apps/$2/main.cpp "$OUT/lib$3.a" -lpthread
}
app pocket terminal uikit_pocket
app pocket tinycalc uikit_pocket
app pocket tinypad uikit_pocket
app elegant tinycalc uikit_wire
app elegant terminal uikit_wire

# run <binary> <name> "<script>" [VAR=value ...]: the app under its server through the script (its checks counted)
run () {
	b=$1; n=$2; sc=$3; shift 3
	if env SIM_OVERLAY=$D/sd "$@" SIM="$sc;exit" "$OUT/$b" >>"$OUT/log.txt" 2>&1; then echo "  $n: ok"
	else echo "  $n: FAILED (see $OUT/log.txt)"; FAIL=1; fi
}
png () { python3 $D/shot.py "$OUT/$1.elsm" "$OUT/$1.png" >/dev/null && echo "  $OUT/$1.png"; }
W="wait;wait;wait"
WW="$W;$W;$W;$W;$W;$W"			# (the Terminal: its WINRESIZE ignored, then asked to maximise -- wm.cpp FILL_WAIT)
PIPE='SD:/ $ ls /bin | grep e\necho\nsleep\nyes\nSD:/ $ ps\n  1 k R  idle\n  2 k S  usb\n 14 a R  pocketui\n 21 a R  terminal\n 22 a S  cmd\n 25 a R  ps\nSD:/ $ echo onyx | wc -c\n5\nSD:/ $ '

echo "server_sim: PocketUI"
# the Terminal: resizable -> filled under the band, no frame; Alt+Tab with another program (a card) -- one in front
run pocket_terminal pocket-terminal "$WW;expect kind fill;expect frame 0;expect area 0,24,800,456;expect client 800,456;expect pos 0,24;expect front app;dump $OUT/pocket-terminal.elsm;other 300 200 Notes;$W;expect front other;expect aside 1;dump $OUT/pocket-card-over.elsm;mods 4;key 0x09;mods 0;$W;expect front app;expect aside 0;$W" \
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
# a card does not move: its title bar dragged, a move asked by its program -- it stays centred
run pocket_tinycalc pocket-card-fixed "$W;expect pos 256,88;down 400 96;move 450 150;move 500 200;up 500 200;$W;expect pos 256,88;other 300 200 Calc;$W;expect opos 246,136;place 10 300;$W;expect opos 246,136" \
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
for p in pocket-terminal pocket-card-over pocket-calculator console-terminal pocket-terminal-640 pocket-front pocket-menubar; do png $p; done
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
[ $FAIL = 0 ] && echo "server_sim: all passed ($OUT)" || echo "server_sim: FAILED ($OUT)"
exit $FAIL
