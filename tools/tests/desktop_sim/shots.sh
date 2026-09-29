#!/bin/sh
# tools/tests/desktop_sim/shots.sh -- the documentation's screenshots (screenshots/*.png), taken
# from the REAL apps run on the PC: each app built for the host against the stand-in kernel
# (fakekapi.cpp; wtk with its real image codecs), driven by a script of events, its window dumped
# (the frame wtk drew + the client area), then made a PNG of its own (shot.py: the rounded corners
# see-through) or laid over the wallpaper with others (compose.py: the desktop, the menu bar, the
# dock...). Sample files (a note, appointments) come from sd/ (SIM_OVERLAY), not from the card.
# A Control Panel applet is run as one (SIM_APPLET: its surface dumped), then shown in the Control
# Panel's window (SIM_MAIL: its hello, SIM_SURFACE: those pixels).
#
#   sh tools/tests/desktop_sim/shots.sh [name ...]	(default: all of them)
#
# Needs g++, python3 with Pillow + numpy. Not made here: nintendoemu.png (an emulator's) and
# arkanoid.png (a BASIC program's): tools/screenshot/render.py's.
set -e
cd "$(dirname "$0")/../../.."
D=tools/tests/desktop_sim
OUT=${SHOTS_TMP:-/tmp/onyx_shots}
PNG=screenshots
WANT=" $* "
rm -rf "$OUT/writes"; mkdir -p "$OUT/obj" "$OUT/writes"
: > "$OUT/log.txt"
export SIM_WRITES="$OUT/writes"			# (what the apps save: there, never on the card)
CXX="g++ -std=gnu++17 -O1 -w -I user -I kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST"

want () { [ "$WANT" = "  " ] || case "$WANT" in *" $1 "*) return 0 ;; *) return 1 ;; esac; }

# ---- the building ------------------------------------------------------------------------------
# wtk (with the image codecs, on the host's libc) and the stand-in kernel, once; then the apps
for f in user/wtk/*.cpp; do $CXX -c "$f" -o "$OUT/obj/$(basename "$f" .cpp).o" & done; wait
rm -f "$OUT/libwtk.a"; ar rcs "$OUT/libwtk.a" "$OUT"/obj/*.o
$CXX -c $D/fakekapi.cpp -o "$OUT/fakekapi.o"
# the apps' TrueType-only FreeType (user/ft/, as user/Makefile builds it for the Pi): Writer's
FT=third_party/freetype-2.14.3
FT_SRC="base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c base/ftsynth.c autofit/autofit.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c"
mkdir -p "$OUT/ft"
for f in $FT_SRC; do gcc -O2 -w -c -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' \
	-Iuser/ft -I$FT/include $FT/src/$f -o "$OUT/ft/$(basename $f .c).o" & done; wait
rm -f "$OUT/libft.a"; ar rcs "$OUT/libft.a" "$OUT"/ft/*.o
build () {
	extra=""; [ "$1" = graphcalc ] && extra=user/basic/basnum.cpp
	[ "$1" = gamelib ] && extra="user/gb/gb.cpp $(ls user/gba/*.cpp user/nes/*.cpp user/snes/*.cpp)"
	if [ "$1" = writer ]; then
		$CXX -Iuser/ft -I$FT/include -o "$OUT/$1" "$OUT/fakekapi.o" user/Apps/$1/main.cpp "$OUT/libwtk.a" "$OUT/libft.a"; return
	fi
	$CXX -o "$OUT/$1" "$OUT/fakekapi.o" user/Apps/$1/main.cpp $extra "$OUT/libwtk.a"
}
APPS="2048 agenda applist calendar control dock dockconf eyes fileviewer freecell gamelib graphcalc iconedit
      invaders irc mandelbrot menubar minesweeper paint pipes rtfview solitaire taskman terminal theme
      tinycalc tinypad widgets wifimenu writer"
for a in $APPS; do build $a & done
# the BASIC runtime (SD:/bin/basic: a BASIC program's window)
$CXX -o "$OUT/basic" "$OUT/fakekapi.o" user/basic/runtime.cpp user/basic/bascomp.cpp user/basic/basvm.cpp user/basic/basnum.cpp user/basic/basbax.cpp "$OUT/libwtk.a" &
wait

# ---- the running -------------------------------------------------------------------------------
# sim APP DUMP "SCRIPT" [VAR=value ...]: APP run through the script, then its window -> DUMP.elsm
sim () {
	app=$1; dump=$2; script=$3; shift 3
	if ! env SIM_OVERLAY=$D/sd "$@" SIM="$script;dump $OUT/$dump.elsm;exit" "$OUT/$app" >>"$OUT/log.txt" 2>&1
	then echo "shots: $app failed (see $OUT/log.txt)"; exit 1; fi
}
png () { python3 $D/shot.py "$OUT/$1.elsm" "$PNG/$1.png" >/dev/null && echo "  $PNG/$1.png"; }
scene () { out=$1; shift; python3 $D/compose.py "$PNG/$out.png" "$@" >/dev/null && echo "  $PNG/$out.png"; }
# typ "text": typing it (a key a step; a space is key 32)
typ () {
	s=$1; o=""
	while [ -n "$s" ]; do
		c=${s%"${s#?}"}; s=${s#?}
		if [ "$c" = " " ]; then o="$o;key 32"; else o="$o;key $c"; fi
	done
	printf '%s' "${o#;}"
}
# stroke x0 y0 x1 y1 ...: a drag through the points (a step every 3 pixels); ring cx cy r: a circle
stroke () {
	awk -v p="$*" 'BEGIN { n = split (p, a, " "); printf "down %d %d", a[1], a[2]
		for (i = 3; i < n; i += 2) { x0 = a[i-2]; y0 = a[i-1]; x1 = a[i]; y1 = a[i+1]
			k = int (sqrt ((x1-x0)^2 + (y1-y0)^2) / 3) + 1
			for (j = 1; j <= k; j++) printf ";move %d %d", x0 + (x1-x0)*j/k, y0 + (y1-y0)*j/k }
		printf ";up %d %d", a[n-1], a[n] }'
}
ring () {
	awk -v cx=$1 -v cy=$2 -v r=$3 'BEGIN { n = int (6.2832 * r / 3); printf "down %d %d", cx + r, cy
		for (i = 1; i <= n; i++) printf ";move %d %d", cx + r*cos (i*6.2832/n), cy + r*sin (i*6.2832/n)
		printf ";up %d %d", cx + r, cy }'
}
W="wait;wait;wait"
P=SIM_POS=100,100
# the windows the dock's workspaces draw small (x,y,w,h,desk,keys)
WINS="100,80,500,400,0,1;560,200,400,300,0,0;150,120,600,450,1,0;50,60,300,200,2,0"
# applet APP DUMP "SCRIPT": APP as a Control Panel applet, shown in the Control Panel's window
applet () {
	sim $1 ${2}_ap "$3" SIM_APPLET=1
	sim control $2 "$W" $P SIM_ARGS=$1 SIM_MAIL=40:7 SIM_SURFACE="$OUT/${2}_ap.elsm"
}

# ---- the apps, a window each -------------------------------------------------------------------
if want tinycalc; then sim tinycalc tinycalc "wait;$(typ '12*3.5=');$W" $P; png tinycalc; fi
if want terminal; then
	sim terminal terminal "$W" $P SIM_PIPE='/ $ ls /bin | grep e\necho\nsleep\nyes\n/ $ ps\n  1 k R  idle\n  2 k S  compositor\n 14 a R  menubar\n 15 a R  dock\n 16 a S  agenda\n 21 a R  terminal\n/ $ echo onyx | wc -c\n5\n/ $ '
	png terminal
fi
if want tinypad; then sim tinypad tinypad "$W;key 0x101;key 0x101;key 0x101;key 0x101;key 0x104;$W" $P SIM_ARGS=SD:/notes.txt; png tinypad; fi
if want paint; then			# (a picture on three layers: the sky filled, hills and a sun, a house, stars, a heart selected)
	c () { printf "down %d %d;up %d %d" $1 $2 $1 $2; }
	r () { printf "rdown %d %d;rup %d %d" $1 $2 $1 $2; }
	S="wait;$(c 291 25);$(c 824 37);$(c 286 259)"
	S="$S;menu 23;$(c 422 18);$(c 498 54);$(c 804 17);$(r 804 37);$(stroke 0 475 406 715);$(stroke 286 499 764 727)"
	S="$S;$(c 764 17);$(r 784 17);$(stroke 549 175 645 271)"
	S="$S;menu 23;$(c 376 18);$(c 724 37);$(r 784 37);$(stroke 227 403 382 547);$(c 445 18);$(c 724 17);$(r 744 17);$(stroke 209 321 400 407)"
	S="$S;$(c 376 18);$(r 724 37);$(stroke 286 475 320 547)"
	S="$S;$(c 376 62);$(c 764 37);$(r 784 17);$(stroke 119 187 167 235);$(stroke 185 241 215 271);$(c 445 62);$(c 744 17);$(r 744 37);$(stroke 465 427 525 487)"
	sim paint paint "$S;$(c 142 39);$(stroke 451 415 539 499);$W" $P
	png paint
	# the pixel grid (the Grid toggle: 400 %, then 800 %), over the heart's edge
	sim paint paint-grid "$S;$(c 962 22);menu 31;menu 31;$(c 291 57);$(c 465 460);$W" $P
	png paint-grid
fi
if want calendar; then sim calendar calendar "wait;down 196 199;up 196 199;$W" $P; png calendar; fi
if want mandelbrot; then sim mandelbrot mandelbrot "$W" $P; png mandelbrot; fi
if want eyes; then sim eyes eyes "$W" $P SIM_CURSOR=260,-40; png eyes; fi
if want taskman; then sim taskman taskman "$W;key 0x101;key 0x101;key 0x101;key 0x101;key 0x101;key 0x101;key 0x101;key 0x101;key 0x101;$W" $P; png taskman; fi
if want 2048; then
	m=""; for k in 0x102 0x100 0x103 0x101 0x102 0x100 0x102 0x100 0x103 0x100 0x102 0x100 0x103 0x101 0x102 0x100 0x102 0x100 0x103 0x100 0x102 0x100 0x102 0x100 0x103 0x100; do m="$m;key $k;wait"; done
	sim 2048 2048 "wait$m;$W" $P; png 2048
fi
if want minesweeper; then sim minesweeper minesweeper "wait;down 140 111;up 140 111;rdown 92 159;rup 92 159;$W" $P; png minesweeper; fi
if want irc; then
	sim irc irc "$W;wait;$(typ 'hello from Onyx :)');key 13;wait;$(typ 'what are you all running it on?');$W" $P \
	SIM_NET=':irc.libera.chat NOTICE * :*** Looking up your hostname...\r\n:irc.libera.chat 001 onyx-user :Welcome to Libera.Chat, onyx-user\r\n:onyx-user!~onyx@192.168.1.42 JOIN #onyx\r\n:irc.libera.chat 332 onyx-user #onyx :Onyx -- a homemade OS for the Raspberry Pi 4\r\n:alice!~alice@host JOIN #onyx\r\n:alice!~alice@host PRIVMSG #onyx :hey, is this the bare-metal Pi channel?\r\n:bob!~bob@host PRIVMSG #onyx :yep -- Onyx, a hobby OS on Circle\r\n'
	png irc
fi
if want fileviewer; then sim fileviewer fileviewer "wait;down 300 181;up 300 181;wait;down 480 85;up 480 85;$W" $P; png fileviewer; fi
if want solitaire; then sim solitaire solitaire "$W" $P; png solitaire; fi
if want freecell; then sim freecell freecell "$W" $P; png freecell; fi
if want pipes; then sim pipes pipes "$W" $P; png pipes; fi
if want invaders; then sim invaders invaders "$W;$W;$W;key 32;$W;$W;$W;$W" $P; png invaders; fi
if want graphcalc; then sim graphcalc graphcalc "$W" $P; png graphcalc; fi
if want iconedit; then sim iconedit iconedit "$W" $P SIM_ARGS=SD:/apps/invaders.app/icon.bmp; png iconedit; fi
if want rtfview; then sim rtfview rtfview "$W" $P SIM_ARGS=SD:/docs/onyx-rtf-sample.rtf; png rtfview; fi
if want writer; then			# (the sample document; a double click selects a word: the toolbar follows it)
	sim writer writer "wait;down 585 572;up 585 572;down 585 572;up 585 572;$W" $P SIM_ARGS=SD:/docs/writer-tour.rtf
	png writer
fi
if want widgets; then sim widgets widgets "$W" $P; png widgets; fi
if want applist; then sim applist applist "$W" $P; png applist; fi
if want control; then sim control control "wait;move 200 130;$W" $P; png control; fi
if want basicdemo; then sim basic basicdemo "$W;$W;$W" $P SIM_APP=basic SIM_ARGS=SD:/apps/basicdemo.app/main.bas; png basicdemo; fi
if want gamelib; then
	python3 $D/gamelib_samples.py "$OUT/writes"
	sim gamelib gamelib "$W;$W" $P
	png gamelib
fi
if want theme; then			# (the wallpaper a pattern: SD:/wallpapers' hexagons, coloured)
	mkdir -p "$OUT/writes/etc"
	printf 'mode = pattern\ncolor = 0x4878B0\ncolor2 = 0x1C2C48\ndirection = vertical\npattern = SD:/wallpapers/hexagons.png\n' > "$OUT/writes/etc/wallpaper.ini"
	applet theme theme "$W"; png theme
	rm -f "$OUT/writes/etc/wallpaper.ini"
fi
if want dockconf; then applet dockconf dockconf "$W"; png dockconf; fi

# ---- the desktop's parts, over the wallpaper ------------------------------------------------------
MENU_TINYPAD='tinypad|MFile/I0~New~^N/I1~Open...~^O/-/I2~Save~^S/I3~Save As...~/MEdit/I4~Cut~^X/I5~Copy~^C/I6~Paste~^V/-/I7~Select All~^A/I8~Copy All~'
if want menubar; then
	sim menubar menubar "wait;wait;down 150 15;up 150 15;wait;wait;move 160 70;$W" SIM_MENU="$MENU_TINYPAD"
	scene menubar "$OUT/menubar.elsm" --crop=0,0,1024,200
fi
if want volume; then
	sim menubar volume "wait;wait;down 925 15;up 925 15;$W" SIM_MENU="$MENU_TINYPAD"
	scene volume "$OUT/volume.elsm" --crop=0,0,1024,150
fi
if want clock; then
	sim menubar clock "wait;wait;down 990 15;up 990 15;$W" SIM_MENU="$MENU_TINYPAD"
	scene clock "$OUT/clock.elsm" --crop=624,0,1024,330
fi
if want wifimenu; then
	sim menubar bar "$W" SIM_MENU="$MENU_TINYPAD"
	sim wifimenu wifimenu "$W"
	scene wifimenu "$OUT/bar.elsm" "$OUT/wifimenu.elsm" --crop=0,0,1024,300
fi
if want dock; then			# (the pointer on the Games launcher's strip: its name grows the dock up)
	sim dock dock "wait;wait;move 222 8;wait;down 222 38;up 222 38;$W" SIM_RUNNING=terminal,tetris,tinycalc SIM_WINS="$WINS"
	scene dock "$OUT/dock.elsm" --crop=60,176,964,768
fi
if want agenda; then
	sim agenda agenda "$W"
	scene agenda "$OUT/agenda.elsm" --crop=0,20,400,200
fi
if want desktop; then
	sim agenda d_agenda "$W"
	sim tinycalc d_calc "wait;$(typ '12*3.5=');$W" SIM_POS=52,250 SIM_INACTIVE=1
	sim terminal d_term "$W" SIM_POS=388,128 SIM_PIPE='/ $ ls /bin | grep e\necho\nsleep\nyes\n/ $ ps\n  1 k R  idle\n  2 k S  compositor\n 14 a R  menubar\n 15 a R  dock\n 16 a S  agenda\n 21 a R  terminal\n 22 a S  tinycalc\n/ $ echo onyx | wc -c\n5\n/ $ '
	sim dock d_dock "wait;wait;$W" SIM_RUNNING=terminal,tinycalc SIM_WINS="52,250,316,412,0,0;388,128,568,408,0,1;150,120,600,450,1,0"
	sim menubar d_bar "$W" SIM_MENU='terminal|'
	scene desktop "$OUT/d_agenda.elsm" "$OUT/d_calc.elsm" "$OUT/d_term.elsm" "$OUT/d_dock.elsm" "$OUT/d_bar.elsm"
fi
echo "shots: done"
