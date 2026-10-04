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
# the apps' TrueType-only FreeType (user/ft/, as user/Makefile builds it for the Pi): Letters'
FT=third_party/freetype-2.14.3
FT_SRC="base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c base/ftsynth.c autofit/autofit.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c"
mkdir -p "$OUT/ft"
for f in $FT_SRC; do gcc -O2 -w -c -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' \
	-Iuser/ft -I$FT/include $FT/src/$f -o "$OUT/ft/$(basename $f .c).o" & done; wait
rm -f "$OUT/libft.a"; ar rcs "$OUT/libft.a" "$OUT"/ft/*.o
build () {
	extra=""; [ "$1" = graphcalc ] && extra=user/basic/basnum.cpp
	[ "$1" = gamelib ] && extra="user/gb/gb.cpp $(ls user/gba/*.cpp user/nes/*.cpp user/snes/*.cpp)"
	if [ "$1" = koton ]; then			# (the studio: its engine, MeltySynth, its plugin host, FreeType)
		K=user/Apps/koton; mkdir -p "$OUT/koton"
		for f in $K/engine/*.cpp $K/synth/*.cpp $K/plug/*.cpp; do $CXX -I$K -c "$f" -o "$OUT/koton/$(basename "$f" .cpp).o" || return 1; done
		$CXX -I$K -Iuser/ft -I$FT/include -o "$OUT/koton/koton" "$OUT/fakekapi.o" $K/main.cpp "$OUT"/koton/*.o "$OUT/libwtk.a" "$OUT/libft.a"
		cp "$OUT/koton/koton" "$OUT/koton.bin"; return
	fi
	if [ "$1" = archiver ]; then			# (newlib-like: FreeType, zlib)
		mkdir -p "$OUT/zlib"
		for f in adler32 crc32 deflate inflate inffast inftrees trees zutil; do gcc -O2 -w -c third_party/zlib-1.3.1/$f.c -o "$OUT/zlib/$f.o" || return 1; done
		$CXX -Iuser/ft -I$FT/include -Ithird_party/zlib-1.3.1 -Iuser/Apps/archiver -o "$OUT/archiver" "$OUT/fakekapi.o" user/Apps/archiver/main.cpp \
			"$OUT/libwtk.a" "$OUT/libft.a" "$OUT"/zlib/*.o -lpthread; return
	fi
	if [ "$1" = media ]; then			# (newlib-like: FreeType, the decoders, Koton's MeltySynth)
		mkdir -p "$OUT/media"
		gcc -O2 -w -Iuser -Ithird_party -c user/Apps/media/codecs.c -o "$OUT/media/codecs.o" || return 1
		gcc -O2 -w -Iuser -Ithird_party -c user/Apps/media/vorbis.c -o "$OUT/media/vorbis.o" || return 1
		for f in user/Apps/koton/synth/*.cpp; do $CXX -c "$f" -o "$OUT/media/$(basename "$f" .cpp).o" || return 1; done
		FFH=${FFMPEG_HOST:-/tmp/onyx_ffmpeg_host}			# (the videos: user/av, its codecs, FFmpeg for the PC)
		sh third_party/ffmpeg-7.1.2/onyx/build.sh host "$FFH" || return 1
		make -s -f $D/av_host.mk OUT="$OUT/av" -j"$(nproc)" || return 1
		$CXX -Iuser/ft -I$FT/include -Ithird_party -o "$OUT/media.bin" "$OUT/fakekapi.o" user/Apps/media/main.cpp "$OUT"/media/*.o \
			"$OUT/libwtk.a" "$OUT/libft.a" "$OUT/av/libavhost.a" -L"$FFH" -lavformat -lavcodec -lswscale -lswresample -lavutil -lpthread -lm; return
	fi
	if [ "$1" = pkgman ]; then			# (the Package Manager: pkg/pkglib.h -- zlib, mbedTLS built for the PC)
		M=third_party/mbedtls-3.6.3; mkdir -p "$OUT/mb" "$OUT/pkzlib"
		if [ ! -f "$OUT/libmb.a" ]; then
			for f in $M/library/*.c; do gcc -O1 -w -I$M/include -I$M/library -c $f -o "$OUT/mb/$(basename $f .c).o" || return 1; done
			ar rcs "$OUT/libmb.a" "$OUT"/mb/*.o
		fi
		for f in adler32 crc32 deflate inflate inffast inftrees trees zutil; do gcc -O2 -w -c third_party/zlib-1.3.1/$f.c -o "$OUT/pkzlib/$f.o" || return 1; done
		$CXX -Iuser/ft -I$FT/include -Ithird_party/zlib-1.3.1 -I$M/include -o "$OUT/pkgman" "$OUT/fakekapi.o" user/Apps/pkgman/main.cpp \
			"$OUT/libwtk.a" "$OUT/libft.a" "$OUT"/pkzlib/*.o "$OUT/libmb.a" -lpthread; return
	fi
	if [ "$1" = mail ]; then			# (Mail: mbedTLS built for the PC, as the Package Manager's; its demo accounts' maker)
		M=third_party/mbedtls-3.6.3; mkdir -p "$OUT/mb"
		if [ ! -f "$OUT/libmb.a" ]; then
			for f in $M/library/*.c; do gcc -O1 -w -I$M/include -I$M/library -c $f -o "$OUT/mb/$(basename $f .c).o" || return 1; done
			ar rcs "$OUT/libmb.a" "$OUT"/mb/*.o
		fi
		$CXX -Iuser/ft -I$FT/include -I$M/include -o "$OUT/mail" "$OUT/fakekapi.o" user/Apps/mail/main.cpp "$OUT/libwtk.a" "$OUT/libft.a" "$OUT/libmb.a" -lpthread || return 1
		$CXX -I$M/include -o "$OUT/mkaccounts" "$OUT/fakekapi.o" tools/tests/mail/mkaccounts.cpp "$OUT/libmb.a" -lpthread; return
	fi
	if [ "$1" = pdf ]; then				# (the PDF Viewer: MuPDF for the PC -- user/Apps/pdf/mupdf.mk with gcc; its FreeType)
		make -s -j8 -f user/Apps/pdf/mupdf.mk MU_ROOT=. MU_CC=gcc MU_AR=ar MU_OUT="$OUT/mupdf" MU_CFLAGS=-O2 || return 1
		$CXX -Iuser/ft -I$FT/include -Ithird_party/mupdf-1.28.5/include -o "$OUT/pdf.bin" "$OUT/fakekapi.o" user/Apps/pdf/main.cpp \
			"$OUT/libwtk.a" "$OUT/mupdf/libmupdf.a" -lpthread -lm; return
	fi
	if [ "$1" = paint ]; then			# (newlib-like: FreeType; the canvas through gpucomp -- the CPU's path here)
		gcc -O2 -w -Iuser -Ikernel/include -c user/gpucomp/gpucomp.c -o "$OUT/gpucomp.o" || return 1
		$CXX -Iuser/ft -I$FT/include -o "$OUT/paint" "$OUT/fakekapi.o" user/Apps/paint/main.cpp "$OUT/gpucomp.o" "$OUT/libwtk.a" "$OUT/libft.a"; return
	fi
	if [ "$1" = slides ]; then			# (newlib-like: FreeType; the slides' layers through gpucomp -- the CPU's path here)
		gcc -O2 -w -Iuser -Ikernel/include -c user/gpucomp/gpucomp.c -o "$OUT/gpucomp_sl.o" || return 1
		$CXX -Iuser/ft -I$FT/include -o "$OUT/slides" "$OUT/fakekapi.o" user/Apps/slides/main.cpp "$OUT/gpucomp_sl.o" "$OUT/libwtk.a" "$OUT/libft.a"; return
	fi
	if [ "$1" = clipboard ]; then			# (the widget, clipd as a thread: clipboard_demo.cpp)
		$CXX -Iuser/ft -I$FT/include -Iuser/Apps/clipd -o "$OUT/clipboard" "$OUT/fakekapi.o" $D/clipboard_demo.cpp \
			"$OUT/libwtk.a" "$OUT/libft.a" -lpthread; return
	fi
	if [ "$1" = courier ]; then			# (newlib-like: FreeType; no TLS on the PC)
		$CXX -Iuser/ft -I$FT/include -DCOURIER_NO_TLS -o "$OUT/courier" "$OUT/fakekapi.o" user/Apps/courier/main.cpp "$OUT/libwtk.a" "$OUT/libft.a" -lpthread; return
	fi
	case " letters sheet calendar control theme config wpaconf padconf dockconf soundconf displayconf keyconf preloadconf gamelib setup menubar screenshot fileviewer photos ledger fmtracker taskman " in
	*" $1 "*)				# (FreeType's text: user/Makefile's FT_APPS)
		$CXX -Iuser/ft -I$FT/include -o "$OUT/$1" "$OUT/fakekapi.o" user/Apps/$1/main.cpp $extra "$OUT/libwtk.a" "$OUT/libft.a"; return ;;
	esac
	$CXX -o "$OUT/$1" "$OUT/fakekapi.o" user/Apps/$1/main.cpp $extra "$OUT/libwtk.a"
}
APPS="2048 agenda applist calendar cardfile control dock dockconf eyes fileviewer freecell gamelib graphcalc iconedit
      fmtracker invaders irc mandelbrot menubar minesweeper paint pipes rtfview solitaire taskman terminal theme
      tinycalc tinypad widgets wifimenu letters sheet slides ledger koton courier archiver clipboard screenshot media pdf mail photos setup pkgman
      config wpaconf padconf soundconf displayconf keyconf preloadconf"
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
	a=$1; d=$2; sc=$3; shift 3
	sim $a ${d}_ap "$sc" SIM_APPLET=1 "$@"
	sim control $d "$W" $P SIM_ARGS=$a SIM_MAIL=40:7 SIM_SURFACE="$OUT/${d}_ap.elsm"
}

# ---- the apps, a window each -------------------------------------------------------------------
if want tinycalc; then sim tinycalc tinycalc "wait;$(typ '12*3.5=');$W" $P; png tinycalc; fi
if want terminal; then
	sim terminal terminal "$W" $P SIM_PIPE='/ $ ls /bin | grep e\necho\nsleep\nyes\n/ $ ps\n  1 k R  idle\n  2 k S  compositor\n 14 a R  menubar\n 15 a R  dock\n 16 a S  agenda\n 21 a R  terminal\n/ $ echo onyx | wc -c\n5\n/ $ '
	png terminal
fi
if want tinypad; then sim tinypad tinypad "$W;key 0x101;key 0x101;key 0x101;key 0x101;key 0x104;$W" $P SIM_ARGS=SD:/notes.txt; png tinypad; fi
if want paint; then			# (the Paint mock-ups made real: docs/paint; tools/tests/desktop_sim/paint_scene.py)
	PP=SIM_POS=4,30
	sim paint paint "$(python3 $D/paint_scene.py landscape);$W" $PP; png paint
	sim paint paint-fade "$(python3 $D/paint_scene.py fade);$W" $PP SIM_ARGS=SD:/docs/pictures/sunset-sea.jpg; png paint-fade
	sim paint paint-brushes "$(python3 $D/paint_scene.py brushes);$W" $PP SIM_ARGS=SD:/docs/pictures/sunny-mountains.jpg; png paint-brushes
	# the pixel grid (View > Grid: 400 %, then 600 %)
	sim paint paint-grid "wait;wait;menu 53;menu 49;$W" $PP SIM_ARGS=SD:/docs/pictures/sunset-sea.jpg; png paint-grid
fi
if want calendar; then			# (the sample calendar.ics of sd/: the week of Monday 28 September 2026, the
					#  simulator's today; the month, the day; a weekly event opened -- the wheel down to
					#  19:00, a double click on Tuesday's badminton)
	sim calendar calendar "$W" $P; png calendar
	sim calendar calendar-month "$W;down 941 23;up 941 23;$W" $P; png calendar-month
	sim calendar calendar-day "$W;down 781 23;up 781 23;$W" $P; png calendar-day
	sim calendar calendar-event "$W;wheel 600 400 -3;wait;down 471 515;up 471 515;down 471 515;up 471 515;$W;$W" $P; png calendar-event
fi
if want mandelbrot; then sim mandelbrot mandelbrot "$W" $P; png mandelbrot; fi
if want eyes; then sim eyes eyes "$W" $P SIM_CURSOR=260,-40; png eyes; fi
if want taskman; then			# (its two tabs: the processes -- a row chosen --, the memory after a few samples)
	sim taskman taskman "$W;key 0x101;key 0x101;key 0x101;$W" $P; png taskman
	sim taskman taskman-memory "$W;down 180 24;up 180 24;$W;$W;$W;$W;$W;$W;$W;$W" $P; png taskman-memory
fi
if want 2048; then
	m=""; for k in 0x102 0x100 0x103 0x101 0x102 0x100 0x102 0x100 0x103 0x100 0x102 0x100 0x103 0x101 0x102 0x100 0x102 0x100 0x103 0x100 0x102 0x100 0x102 0x100 0x103 0x100; do m="$m;key $k;wait"; done
	sim 2048 2048 "wait$m;$W" $P; png 2048
fi
if want minesweeper; then sim minesweeper minesweeper "wait;down 140 111;up 140 111;rdown 92 159;rup 92 159;$W" $P; png minesweeper; fi
if want irc; then			# (a session canned (SIM_NET): #onyx and its users, #raspberrypi's unread lines and a
					#  mention, a private message from alice, the LIST; the Rooms view (its toolbar button);
					#  alice's conversation window, its lines mailed by the main window (SIM_MBOX))
	A=$(printf '\001')
	N=":irc.libera.chat NOTICE * :*** Looking up your hostname...\r\n:irc.libera.chat 001 stephan :Welcome to the Libera.Chat Internet Relay Chat Network stephan\r\n:irc.libera.chat 375 stephan :- irc.libera.chat Message of the Day -\r\n:irc.libera.chat 372 stephan :- Welcome to Libera Chat, the IRC network for free and open-source software.\r\n:irc.libera.chat 376 stephan :End of /MOTD command.\r\n:stephan!~onyx@192.168.1.42 JOIN #raspberrypi\r\n:irc.libera.chat 332 stephan #raspberrypi :Raspberry Pi help and chat\r\n:irc.libera.chat 353 stephan = #raspberrypi :stephan @ChanServ zed yan\r\n:stephan!~onyx@192.168.1.42 JOIN #onyx\r\n:irc.libera.chat 332 stephan #onyx :Onyx -- a homemade OS for the Raspberry Pi 4 | be nice\r\n:irc.libera.chat 353 stephan = #onyx :stephan @bob alice +carol dave_ eve\r\n:irc.libera.chat 366 stephan #onyx :End of /NAMES list.\r\n:alice!~alice@host PRIVMSG #onyx :hey, is this the bare-metal Pi channel?\r\n:bob!~bob@host PRIVMSG #onyx :yep -- Onyx, a hobby OS built on Circle\r\n:bob!~bob@host PRIVMSG #onyx :it has its own window manager, a widget toolkit and a bunch of apps\r\n:carol!~c@host JOIN #onyx\r\n:carol!~c@host PRIVMSG #onyx :${A}ACTION waves${A}\r\n:dave_!~d@host PRIVMSG #onyx :stephan: does the Wi-Fi work on it?\r\n:zed!~z@h PRIVMSG #raspberrypi :anyone tried the new firmware?\r\n:yan!~y@h PRIVMSG #raspberrypi :yes, works fine here\r\n:zed!~z@h PRIVMSG #raspberrypi :stephan: what about you?\r\n:alice!~alice@host PRIVMSG stephan :hi! I saw your message in #onyx\r\n:irc.libera.chat 321 stephan Channel :Users  Name\r\n:irc.libera.chat 322 stephan #raspberrypi 1204 :[+nt] Raspberry Pi help and chat | be patient, ask your question\r\n:irc.libera.chat 322 stephan #pidev 87 :Pi firmware, kernel and bare-metal development\r\n:irc.libera.chat 322 stephan #piaware 41 :ADS-B on the Pi\r\n:irc.libera.chat 322 stephan #pihole 33 :Pi-hole network ad blocking\r\n:irc.libera.chat 322 stephan #onyx 6 :Onyx -- a homemade OS for the Raspberry Pi 4\r\n:irc.libera.chat 322 stephan #libera 2311 :Libera Chat support | https://libera.chat\r\n:irc.libera.chat 322 stephan #linux 1893 :Linux help and discussion\r\n:irc.libera.chat 322 stephan #python 1612 :Python programming | pastebin: bpa.st\r\n:irc.libera.chat 322 stephan #osdev 512 :Operating system development | wiki.osdev.org\r\n:irc.libera.chat 322 stephan #circle 12 :Circle: C++ bare metal environment for the Raspberry Pi\r\n:irc.libera.chat 322 stephan #retrocomputing 230 :Old machines, new tricks\r\n:irc.libera.chat 322 stephan #emacs 870 :The extensible editor\r\n:irc.libera.chat 322 stephan #c 640 :The C programming language\r\n:irc.libera.chat 322 stephan #rust 1120 :Rust programming\r\n:irc.libera.chat 322 stephan #tiny 2 :a tiny room\r\n:irc.libera.chat 323 stephan :End of /LIST\r\n"
	sim irc irc "$W;$W;$(typ 'yes, over Wi-Fi');$W" $P SIM_NET="$N"
	png irc
	sim irc irc-rooms "$W;$W;down 831 19;up 831 19;$W" $P SIM_NET="$N"
	png irc-rooms
	sim irc irc-pm "$W;$W" $P SIM_ARGS="--pm alice" SIM_MBOX='7204:7:1\tstephan\tirc.libera.chat\talice\n7202:7:m\t12:30\talice\thi! I saw your message in #onyx\n7202:7:m\t12:30\talice\tis Onyx open source? I would love to try it on my Pi 4\n7202:7:M\t12:31\tstephan\tyes, it is on GitHub\n7202:7:M\t12:31\tstephan\tyou just copy the files to a FAT32 SD card and boot\n7202:7:m\t12:32\talice\tnice, and does the Wi-Fi work?\n7202:7:M\t12:48\tstephan\tit does -- I am chatting with you from it right now :)\n7202:7:a\t12:49\talice\tis impressed\n7202:7:m\t12:49\talice\tthat is so cool'
	png irc-pm
fi
if want fileviewer; then sim fileviewer fileviewer "wait;down 300 205;up 300 205;wait;down 480 109;up 480 109;$W" $P; png fileviewer; fi
if want fmtracker; then			# (a song of SD:/music/fms; a block chosen; the instrument dialog: a click on a channel's name)
	sim fmtracker fmtracker "wait;wait;key 0x101;key 0x101;key 0x101;key 0x101;key 0x101;key 0x101;down 600 250;move 700 300;up 700 300;$W" $P SIM_ARGS=SD:/music/fms/AIRWOLF.FMS; png fmtracker
	sim fmtracker fmtracker-instrument "wait;wait;down 330 64;up 330 64;$W" $P SIM_ARGS=SD:/music/fms/AIRWOLF.FMS; png fmtracker-instrument
fi
if want solitaire; then sim solitaire solitaire "$W" $P; png solitaire; fi
if want freecell; then sim freecell freecell "$W" $P; png freecell; fi
if want pipes; then sim pipes pipes "$W" $P; png pipes; fi
if want invaders; then sim invaders invaders "$W;$W;$W;key 32;$W;$W;$W;$W" $P; png invaders; fi
if want graphcalc; then sim graphcalc graphcalc "$W" $P; png graphcalc; fi
if want iconedit; then sim iconedit iconedit "$W" $P SIM_ARGS=SD:/apps/invaders.app/icon.bmp; png iconedit; fi
if want rtfview; then sim rtfview rtfview "$W" $P SIM_ARGS=SD:/docs/onyx-rtf-sample.rtf; png rtfview; fi
if want letters; then			# (the sample document, a word of its contents chosen: the toolbar follows it; its second
					#  page: the header, the table, the caret in a cell; the mail merge: the letter's fields shown
					#  with a record of the Contacts)
	sim letters letters "wait;down 232 550;up 232 550;down 232 550;up 232 550;$W" $P SIM_ARGS=SD:/docs/letters-tour.rtf
	png letters
	sim letters letters-table "wait;wheel 500 400 -10;wait;wheel 500 400 -10;wait;wheel 500 400 -9;wait;down 479 481;up 479 481;$W" $P \
		SIM_ARGS=SD:/docs/letters-tour.rtf
	png letters-table
	sim letters letters-merge "wait;menu 60;wait;down 520 261;up 520 261;$W" $P SIM_ARGS=SD:/docs/new-year-letter.rtf; png letters-merge
	sim letters letters-pdf "wait;menu 6;$W" $P SIM_ARGS=SD:/docs/letters-tour.rtf; png letters-pdf	# (File > Export as PDF)
fi
if want sheet; then			# (the sample workbook: the Total column chosen -- its sum below --; a filter's drop-down; the loan's names)
	sim sheet sheet "wait;down 450 405;move 450 300;move 450 218;up 450 218;$W" $P SIM_ARGS=SD:/docs/cafe-2026.xlsx; png sheet
	sim sheet sheet-filter "wait;down 60 197;move 300 300;move 520 405;up 520 405;wait;down 372 17;up 372 17;wait;down 127 198;up 127 198;$W" $P SIM_ARGS=SD:/docs/cafe-2026.xlsx
	png sheet-filter
	sim sheet sheet-loan "wait;down 262 661;up 262 661;wait;down 200 228;up 200 228;$W" $P SIM_ARGS=SD:/docs/cafe-2026.xlsx; png sheet-loan
	sim sheet sheet-pdf "wait;menu 5;$W" $P SIM_ARGS=SD:/docs/cafe-2026.xlsx; png sheet-pdf		# (File > Export as PDF)
fi
if want courier; then			# (the demo collection and environments of sd/courier, its tabs open; a POST sent: its
					#  answer canned (SIM_NET); the tests' results; the environments)
	# (a card of links to sdcard/, with the demo as its courier/: the card's folders win over the writes')
	rm -rf "$OUT/csd"; mkdir -p "$OUT/csd"
	for f in sdcard/*; do [ "$f" = sdcard/courier ] || ln -s "$PWD/$f" "$OUT/csd/"; done
	cp -r $D/sd/courier "$OUT/csd/"; rm -rf "$OUT/writes/courier"
	CN='HTTP/1.1 201 Created\r\nContent-Type: application/json; charset=utf-8\r\nSet-Cookie: session=7f3a9c; Path=/; HttpOnly\r\nLocation: /users/1024\r\nServer: onyx-demo\r\nContent-Length: 285\r\n\r\n{"id":1024,"name":"Grace","email":"grace.hopper@example.com","role":"admin","team":"onyx","createdAt":"2026-09-30T14:03:22Z","token":"eyJhbGciOiJIUzI1NiJ9.onyx","links":{"self":"/users/1024","team":"/teams/onyx"},"tags":["pioneer","compiler"],"active":true,"score":98.5,"manager":null}'
	sim courier courier "wait;wait;mods 1;key 13;mods 0;$W;$W" $P SIM_SD="$OUT/csd" SIM_NET="$CN"; png courier
	sim courier courier-tests "wait;wait;mods 1;key 13;mods 0;$W;down 614 364;up 614 364;$W" $P SIM_SD="$OUT/csd" SIM_NET="$CN"; png courier-tests
	sim courier courier-env "wait;wait;down 40 160;up 40 160;wait;down 160 180;up 160 180;$W" $P SIM_SD="$OUT/csd"; png courier-env
	rm -rf "$OUT/writes/courier" "$OUT/csd"
fi
if want archiver; then			# (a sample archive of Onyx's sources in RAM:, arc_sample.py: opened, in kernel/sys three
					#  files selected; the Extract dialog; files dragged from the File Viewer over a folder;
					#  the welcome page with the archive among the recent ones)
	rm -rf "$OUT/arc"; mkdir -p "$OUT/arc"; rm -rf "$OUT/writes/apps/archiver.app"
	python3 $D/arc_sample.py "$OUT/arc/Projet-Onyx.zip"
	AN="down 300 175;up 300 175;down 300 175;up 300 175;wait;wait;down 330 201;up 330 201;down 330 201;up 330 201;wait;wait"
	AS="down 330 279;up 330 279;mods 1;down 330 305;up 330 305;down 330 331;up 330 331;mods 0;wait"
	sim archiver archiver "wait;wait;$AN;$AS;$W" $P SIM_RAM="$OUT/arc" SIM_ARGS=RAM:/Projet-Onyx.zip; png archiver
	sim archiver archiver-extract "wait;wait;$AN;$AS;key 0x05;$W" $P SIM_RAM="$OUT/arc" SIM_ARGS=RAM:/Projet-Onyx.zip; png archiver-extract
	sim archiver archiver-drop "wait;wait;down 300 175;up 300 175;down 300 175;up 300 175;wait;wait;dragover 330 175;$W" $P SIM_RAM="$OUT/arc" SIM_ARGS=RAM:/Projet-Onyx.zip
	png archiver-drop
	sim archiver archiver-welcome "$W" $P SIM_RAM="$OUT/arc"; png archiver-welcome
	rm -rf "$OUT/arc" "$OUT/writes/apps/archiver.app"
fi
if want slides; then			# (the sample deck: slide 3, its callout chosen; the sorter; the effects; the show, mid-transition)
	SL=SIM_ARGS=SD:/docs/cafe-2026.odp
	sim slides slides "wait;wait;key 0x101;key 0x101;wait;down 636 352;up 636 352;wait;down 853 90;up 853 90;$W" $P $SL; png slides
	sim slides slides-sorter "wait;wait;down 871 687;up 871 687;$W" $P $SL; png slides-sorter
	sim slides slides-animate "wait;wait;key 0x101;key 0x101;wait;down 636 352;up 636 352;wait;down 964 90;up 964 90;$W" $P $SL; png slides-animate
	sim slides slides-text "wait;wait;key 0x101;wait;down 300 262;up 300 262;down 300 262;up 300 262;wait;down 300 262;up 300 262;down 300 262;up 300 262;wait;down 908 90;up 908 90;$W" $P $SL; png slides-text
	sim slides slides-show "wait;wait;key 0x101;key 0x101;wait;key 0x114;wait;wait;wait;wait;key 32;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait;wait" $P $SL; png slides-show
fi
if want cardfile; then			# (the sample: a record; the list sorted by title, a row chosen; the design of the genre's choices)
	sim cardfile cardfile "$W" $P SIM_ARGS=SD:/docs/books.card; png cardfile
	sim cardfile cardfile-list "wait;key 0x115;wait;down 60 62;up 60 62;wait;down 300 161;up 300 161;$W" $P SIM_ARGS=SD:/docs/books.card
	png cardfile-list
	sim cardfile cardfile-design "wait;key 0x116;wait;down 100 157;up 100 157;$W" $P SIM_ARGS=SD:/docs/books.card; png cardfile-design
	sim cardfile cardfile-merge "wait;menu 21;$W" $P SIM_ARGS=SD:/docs/contacts.card; png cardfile-merge	# (Record > Mail Merge)
fi
if want ledger; then			# (the demo company: its overview, its sales, an invoice, the quotes and orders, a quote, the bank's
					#  CODA statement imported, the general ledger, the VAT; a quote printed: Ledger writes the merge's data
					#  and request, Letters makes the document from its template)
	L=SIM_ARGS=SD:/docs/demo-company.ledger
	sim ledger ledger "$W" $P $L; png ledger
	sim ledger ledger-sales "wait;down 60 172;up 60 172;$W" $P $L; png ledger-sales
	sim ledger ledger-invoice "wait;down 60 172;up 60 172;wait;down 400 150;up 400 150;wait;key 13;$W" $P $L; png ledger-invoice
	sim ledger ledger-quotes "wait;down 60 316;up 60 316;$W" $P $L; png ledger-quotes
	sim ledger ledger-quote "wait;down 60 316;up 60 316;wait;down 400 218;up 400 218;wait;key 13;$W" $P $L; png ledger-quote
	sim ledger ledger-coda "wait;down 60 233;up 60 233;wait;down 756 28;up 756 28;wait;down 499 441;up 499 441;wait;$(typ demo-bank-statement.cod);wait;down 540 478;up 540 478;$W" $P $L
	png ledger-coda
	sim ledger ledger-reports "wait;down 60 485;up 60 485;$W" $P $L; png ledger-reports
	sim ledger ledger-vat "wait;down 60 515;up 60 515;$W" $P $L; png ledger-vat
	sim ledger ledger-print0 "wait;down 60 316;up 60 316;wait;down 400 218;up 400 218;wait;key 13;wait;down 592 28;up 592 28;$W" $P $L
	sim letters ledger-print "wait;wait;wait;wait;winctl 2;wait;wait;wheel 500 400 -3;$W" $P SIM_ARGS="--merge SD:/apps/ledger.app/merge.job" SIM_OVERLAY="$OUT/writes"
	png ledger-print
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
# (the other Control Panel applets, each in the Control Panel's window)
for a in displayconf soundconf keyconf preloadconf wpaconf config; do
	if want $a; then applet $a $a "$W"; png $a; fi
done
if want padconf; then			# (an Xbox 360 pad plugged in, SIM_PAD: two buttons held, the stick pushed)
	sim padconf padconf_ap "$W" SIM_APPLET=1 SIM_PAD=1
	sim control padconf "$W" $P SIM_ARGS=padconf SIM_MAIL=40:7 SIM_SURFACE="$OUT/padconf_ap.elsm"; png padconf
fi

# ---- the desktop's parts, over the wallpaper ------------------------------------------------------
MENU_TINYPAD='tinypad|MFile/I0~New~^N/I1~Open...~^O/-/I2~Save~^S/I3~Save As...~/MEdit/I4~Cut~^X/I5~Copy~^C/I6~Paste~^V/-/I7~Select All~^A/I8~Copy All~'
if want menubar; then
	sim menubar menubar "wait;wait;down 150 15;up 150 15;wait;wait;move 160 70;$W" SIM_MENU="$MENU_TINYPAD"
	scene menubar "$OUT/menubar.elsm" --crop=0,0,1024,200
fi
if want koton; then			# (the studio, maximised on a 1920 x 1080 screen, its demo song: a chord's editor,
	KS="SIM_SCREEN=1920x1080 SIM_ARGS=SD:/koton/songs/demo.kson"	# the accompaniment's drawn grid, the drums, the rings, a riff)
	sim koton.bin koton "wait;wait;down 338 512;up 338 512;$W" $KS; png koton
	sim koton.bin koton-accomp "wait;wait;down 400 237;up 400 237;wait;down 750 730;up 750 730;$W" $KS; png koton-accomp
	sim koton.bin koton-drums "wait;wait;down 400 382;up 400 382;$W" $KS; png koton-drums
	sim koton.bin koton-rings "wait;wait;down 800 462;up 800 462;$W" $KS; png koton-rings
	sim koton.bin koton-riff "wait;wait;down 400 162;up 400 162;$W" $KS; png koton-riff
	sim koton.bin koton-ai "wait;wait;down 1600 30;up 1600 30;$W" $KS; png koton-ai
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
if want screenshot; then			# (Screenshot: its window; a rectangle being dragged on the frozen screen -- the
					#  screen is screenshots/desktop.png, SIM_GRAB --; a window chosen; the capture
					#  drawn on: the pen's ring, the marker, an arrow; the pen's palette)
	python3 - "$OUT/desk.elsm" <<'PY'
import struct, sys
import numpy as np
from PIL import Image
im = Image.open ("screenshots/desktop.png").convert ("RGB"); a = np.asarray (im).astype (np.uint32)
px = (a[..., 0] << 16) | (a[..., 1] << 8) | a[..., 2]
open (sys.argv[1], "wb").write (struct.pack ("<5i", 0x4D534C45, im.width, im.height, 0, 0) + px.astype ("<u4").tobytes ())
PY
	G="SIM_GRAB=$OUT/desk.elsm"; SP=SIM_POS=300,200
	ss () { rm -f "$OUT/writes/etc/screenshot.ini"; sim screenshot "$@"; }	# (each from the default settings)
	W10="$W;$W;$W;$W"
	CAP="wait;wait;down 50 25;up 50 25;$W10;$W10;move 300 200;wait;down 389 128;move 500 250;move 1016 558;wait"
	ss screenshot "$W" $SP $G; png screenshot
	ss screenshot-select "$CAP;move 566 366;wait;wait" $SP $G; png screenshot-select
	ss screenshot-window "wait;wait;down 152 25;up 152 25;move 170 94;wait;down 170 94;up 170 94;wait;down 50 25;up 50 25;$W10;$W10;move 150 400;wait;wait" \
		$SP $G SIM_WINS="389,128,627,430,0,1,terminal;53,251,286,325,0,0,tinycalc"; png screenshot-window
	E="$CAP;up 1016 558;$W10;$W10;$W10;$W10;$W10;$(ring 107 322 13);wait;down 598 25;up 598 25;wait;$(stroke 102 306 272 306);wait;down 546 25;up 546 25"
	E="$E;$(stroke 286 392 230 372 180 352 128 332);move 420 25;$W"
	ss screenshot-edit "$E" $SP $G; png screenshot-edit
	ss screenshot-pen "$E;down 573 25;up 573 25;move 600 95;wait;wait" $SP $G; png screenshot-pen
fi
if want media; then			# (Media Player over a sample library made by tools/tests/media/make_library.py -- ffmpeg,
					#  mutagen --: the home, the albums, an album playing, the songs' menu, now playing, a MIDI
					#  file's notes, the folders, the mini player; the videos, one playing, an episode's end;
					#  each run over a fresh copy of the library)
	[ -d "$OUT/mlib/Videos" ] || python3 tools/tests/media/make_library.py "$OUT/mlib" >/dev/null
	mm () { rm -rf "$OUT/writes/Music" "$OUT/writes/Videos" "$OUT/writes/etc/media"; mkdir -p "$OUT/writes/etc"; cp -r "$OUT/mlib/Music" "$OUT/mlib/Videos" "$OUT/writes/"; cp -r "$OUT/mlib/etc/media" "$OUT/writes/etc/";
		n=$1; shift; s=$1; shift
		env SIM_OVERLAY=$D/sd SIM_SLEEP=1 SIM_POS=12,30 "$@" SIM="$s;dump $OUT/$n.elsm;exit" "$OUT/media.bin" >>"$OUT/log.txt" 2>&1 || { echo "shots: media failed"; exit 1; }; }
	W10="$W;$W;$W;$W"; S0="$W10;$W10;$W10;$W10;$W10"	# (the scan, the covers)
	ALB="down 100 164;up 100 164;$W10;$W10"				# (Albums)
	PLAY="move 520 230;$W;down 470 300;up 470 300;$W10;$W10"	# (the 1st album's page; its Play)
	mm media-home "$S0;move 900 120;$W10" ; png media-home
	mm media-albums "$S0;$ALB;move 540 230;$W10;down 300 220;up 300 220;$W10;move 490 231;down 490 231;up 490 231;$W10;$ALB;move 760 420;$W10"; png media-albums
	mm media-album "$S0;$ALB;down 300 220;up 300 220;$W10;move 490 231;down 490 231;up 490 231;$W10;$W10;move 400 360;$W"; png media-album
	mm media-songs "$S0;down 100 194;up 100 194;$W10;down 300 333;up 300 333;mods 2;down 300 397;up 300 397;mods 0;$W;rdown 300 365;rup 300 365;$W;move 380 400;$W;$W"; png media-songs
	mm media-nowplaying "$S0;$ALB;down 300 220;up 300 220;$W10;move 490 231;down 490 231;up 490 231;$W10;down 40 585;up 40 585;$W10;$W10"; png media-nowplaying
	mm media-midi "$S0;down 100 194;up 100 194;$W10;move 300 397;down 300 397;up 300 397;wait;down 300 397;up 300 397;$W10;down 40 585;up 40 585;$W10;$W10;$W10;$W10;$W10;$W10;$W10;$W10;$W10;$W10"; png media-midi
	mm media-welcome "$S0;menu 1;$W10"; png media-welcome
	mm media-mini "$S0;$ALB;down 300 220;up 300 220;$W10;move 490 231;down 490 231;up 490 231;$W10;down 841 581;up 841 581;$W10;$W10"; png media-mini
	VID="down 100 342;up 100 342;$W10;$W10;$W10"				# (Clips and series: their frames decoded)
	mm media-videos "$S0;$VID;move 600 210;$W10"; png media-videos
	mm media-watch "$S0;down 414 183;up 414 183;$W10;$W10;$W10;$W10;move 500 300;$W"; png media-watch
	mm media-episode "$S0;$VID;down 366 185;up 366 185;$W10;key 9;$W10;$W10;$W10;$W10;$W10;$W10;$W10;$W10;$W10;$W10;$W10;$W10;$W10;$W10;$W10"; png media-episode
fi
if want pdf; then			# (the PDF Viewer over Onyx's own manuals, sdcard/manuals: reading, the contents, a search,
					#  two pages and the zoom's menu, the home with recent documents, the Properties)
	pv () { n=$1; shift; s=$1; shift
		env SIM_OVERLAY=$D/sd SIM_SLEEP=1 SIM_POS=12,30 "$@" SIM="$s;dump $OUT/$n.elsm;exit" "$OUT/pdf.bin" >>"$OUT/log.txt" 2>&1 || { echo "shots: pdf failed"; exit 1; }; }
	W10="$W;$W;$W;$W"; P0="$W10;$W10"; LED="SIM_ARGS=SD:/manuals/ledger/Ledger.pdf"
	rm -rf "$OUT/writes/etc/pdf"
	GO8="down 185 57;up 185 57;key 0x08;key 8;key 13;$W10;$W10"
	pv pdf "$P0;$GO8;wheel 600 300 -1;$W10;$W10;$W10" "$LED"; png pdf
	pv pdf-contents "$P0;down 112 102;up 112 102;$W;down 26 245;up 26 245;$W;down 110 297;up 110 297;$W10;$W10;$W10;down 330 330;move 600 380;move 830 420;up 830 420;$W;rdown 700 420;rup 700 420;$W" "$LED"; png pdf-contents
	pv pdf-find "$P0;down 800 57;up 800 57;wait;$(typ invoice);$W10;$W10;$W10;$W10;key 13;$W10;$W10;$W10" "$LED"; png pdf-find
	pv pdf-zoom "$P0;down 28 57;up 28 57;$W;down 545 57;up 545 57;$W10;mods 1;key 0;mods 0;$W10;down 185 57;up 185 57;key 0x08;key 4;key 13;$W10;$W10;$W10;down 350 57;up 350 57;move 370 136;$W" SIM_ARGS=SD:/manuals/koton/Koton.pdf; png pdf-zoom
	pv pdf-props "$P0;key 0x04;$W" "$LED"; png pdf-props
	mkdir -p "$OUT/writes/etc/pdf"			# (the recent documents: where they were left, when -- the sim's day is 28 Sep 2026)
	printf '%s\t%s\t%s\t%s\n' SD:/manuals/ledger/Ledger.pdf 8 55 202609281042 SD:/manuals/koton/Koton.pdf 3 33 202609280915 \
		SD:/manuals/ledger/Ledger.fr.pdf 0 59 202609271810 SD:/manuals/koton/Koton.fr.pdf 11 36 202609261420 \
		SD:/manuals/ledger/Ledger.nl.pdf 0 60 202609251130 > "$OUT/writes/etc/pdf/recent.tsv"
	pv pdf-home "$P0;$W10;$W10;$W10;$W10;move 330 250;$W10"; png pdf-home
fi
if want clipboard; then			# (the shared clipboard's widget over the desktop: clipd's ring seeded,
					#  the cursor on the image; the pointer over a row -- its x)
	[ -f "$OUT/d_term.elsm" ] || { echo "shots: clipboard needs desktop's dumps (shots.sh desktop clipboard)"; exit 1; }
	sim clipboard clipboard "wait;wait;wait;move 200 260;$W" SIM_IPC=1
	scene clipboard "$OUT/d_agenda.elsm" "$OUT/d_calc.elsm" "$OUT/d_term.elsm" "$OUT/d_dock.elsm" "$OUT/d_bar.elsm" "$OUT/clipboard.elsm"
fi
if want pkgman; then			# (the Package Manager: a card of its own -- pkg_sample.py: a repository with 4 updates,
					#  a few apps not installed; what it installs written apart, $OUT/pkgw)
	python3 $D/pkg_sample.py "$OUT/pkgsim" >/dev/null
	PK="SIM_SD=$OUT/pkgsim/card SIM_SLEEP=1"
	for t in "pkgman:" "pkgman-installed:down 165 25;up 165 25;wait;wait" "pkgman-available:down 300 25;up 300 25;wait;wait" \
		 "pkgman-restart:down 600 447;up 600 447;$(printf 'wait;%.0s' $(seq 250))"; do
		rm -rf "$OUT/pkgw"; mkdir -p "$OUT/pkgw"
		n=${t%%:*}; applet pkgman $n "wait;wait;wait;wait;wait;${t#*:};$W" $PK SIM_WRITES="$OUT/pkgw"; png $n
	done
fi
if want mail; then			# (Mail against two made-up mailboxes: tools/tests/mail/fakemail.py --demo personal (IMAP, a
					#  Gmail-like folder tree) and --demo work (POP3); the accounts and the contacts made by Mail's own
					#  code (mkaccounts); the real network of the PC, local ports only)
	MB=$(( 33000 + $$ % 500 * 4 ))
	python3 tools/tests/mail/fakemail.py --imap $MB --pop $((MB+1)) --smtp $((MB+2)) --http $((MB+3)) --demo personal --user me@example.com >"$OUT/mail-srv1.log" 2>&1 &
	MS1=$!
	python3 tools/tests/mail/fakemail.py --imap $((MB+100)) --pop $((MB+101)) --smtp $((MB+102)) --http $((MB+103)) --demo work --user steph@atelier-lumen.example >"$OUT/mail-srv2.log" 2>&1 &
	MS2=$!
	sleep 2
	ML="$OUT/mailw0"; rm -rf "$ML"; mkdir -p "$ML"
	SIM_WRITES="$ML" SIM=exit "$OUT/mkaccounts" "Personal|me@example.com|Stéphane|imap|127.0.0.1|$MB|$((MB+2))|secret|#D93025|gmail" \
		"Atelier|steph@atelier-lumen.example|Stéphane|pop3|127.0.0.1|$((MB+101))|$((MB+102))|secret|#1A73E8" >>"$OUT/log.txt" 2>&1
	printf 'client_id = test-client\nhost = 127.0.0.1\nport = %d\ntls = 0\n' $((MB+3)) > "$ML/etc/mail/oauth.ini"
	W40="$(printf 'wait;%.0s' $(seq 40))"
	ms () { n=$1; shift; rm -rf "$OUT/mailw"; cp -r "$ML" "$OUT/mailw"; [ "$1" = new ] && { rm -rf "$OUT/mailw/etc/mail/accounts.ini" "$OUT/mailw/etc/mail/secrets"; shift; }
		env SIM_OVERLAY=$D/sd SIM_SLEEP=1 SIM_REALNET=1 SIM_REALCLOCK=1 SIM_POS=10,30 SIM_WRITES="$OUT/mailw" SIM="$W40;$W40;$W40;$1;dump $OUT/$n.elsm;exit" "$OUT/mail" >>"$OUT/log.txt" 2>&1 \
			|| { echo "shots: mail failed"; kill $MS1 $MS2; exit 1; }
		png $n; }
	ms mail "down 380 240;up 380 240;$W40;$W40"
	ms mail-thread "down 380 430;up 380 430;$W40;$W40"
	ms mail-html "down 380 330;up 380 330;$W40;$W40"
	ms mail-compose "down 380 430;up 380 430;$W40;down 870 386;up 870 386;$W40;$(typ 'Sure, I will add them to the quote.');$W40"
	ms mail-contacts "down 60 537;up 60 537;$W40;down 380 390;up 380 390;$W40"
	WZ="down 606 414;up 606 414;$W40;$(typ 'Stephane');down 500 260;up 500 260"
	ms mail-wizard new "$WZ;$(typ 'steph.demo@gmail.com');$W40;down 714 514;up 714 514;$W40"
	ms mail-outlook new "$WZ;$(typ 'steph.demo@outlook.com');$W40;down 714 514;up 714 514;$W40;down 330 291;up 330 291;wait;wait;wait;wait;wait"
	kill $MS1 $MS2
fi
if want photos; then			# (Photos over a made-up library -- tools/tests/photos/make_samples.py: drawn photos as JPEGs with
					#  their EXIF, a library.db with favourites, albums --: the days, a selection, a photo and its
					#  details, editing (adjust, crop, filters), the albums; each run over a fresh copy)
	[ -d "$OUT/plib/Pictures" ] || python3 tools/tests/photos/make_samples.py "$OUT/plib" >/dev/null
	rm -rf "$OUT/pthumbs"
	pp () { n=$1; shift; s=$1; shift
		rm -rf "$OUT/writes/Pictures" "$OUT/writes/etc/photos"; mkdir -p "$OUT/writes/etc"; cp -r "$OUT/plib/Pictures" "$OUT/writes/"; cp -r "$OUT/plib/etc/photos" "$OUT/writes/etc/"
		[ -d "$OUT/pthumbs" ] && cp -r "$OUT/pthumbs" "$OUT/writes/etc/photos/thumbs"
		env SIM_OVERLAY=$D/sd SIM_SLEEP=1 SIM_POS=12,30 "$@" SIM="$s;dump $OUT/$n.elsm;exit" "$OUT/photos" >>"$OUT/log.txt" 2>&1 || { echo "shots: photos failed"; exit 1; }
		[ -d "$OUT/pthumbs" ] || cp -r "$OUT/writes/etc/photos/thumbs" "$OUT/pthumbs"
		png $n; }
	W10="$W;$W;$W;$W"; S0="$W10;$W10;$W10;$W10;$W10;$W10;$W10;$W10"	# (the scan, the thumbnails)
	OPEN="down 300 360;up 300 360;$W10;$W10"; EDIT="$OPEN;down 812 24;up 812 24;$W10"
	pp photos "$S0;move 600 470;$W10"
	pp photos-select "$S0;down 232 279;up 232 279;$W;down 397 320;up 397 320;$W;down 640 320;up 640 320;$W;move 520 330;$W10"
	pp photos-viewer "$S0;$OPEN;key 0x103;$W10;$W10;move 400 300;$W10"
	pp photos-edit "$S0;$EDIT;down 857 151;up 857 151;$W10;$W10"
	pp photos-crop "$S0;$EDIT;down 767 83;up 767 83;$W;down 749 324;up 749 324;$W10;$W10"
	pp photos-filters "$S0;$EDIT;down 950 110;up 950 110;$W10;$W10;down 800 300;up 800 300;$W10;$W10"
	pp photos-albums "$S0;down 60 196;up 60 196;$W10;$W10"
	pp photos-menu "$S0;rdown 300 360;rup 300 360;$W;$W"
fi
if want milk; then			# (the Milk scheme: the overlay milk/, its theme.txt)
	M=SIM_OVERLAY=$D/milk:$D/sd
	sim agenda m_agenda "$W" $M
	sim calendar m_cal "wait;down 196 199;up 196 199;$W" $M SIM_POS=30,236 SIM_INACTIVE=1
	sim widgets m_widgets "$W" $M SIM_POS=336,172
	sim dock m_dock "wait;wait;$W" $M SIM_RUNNING=calendar,widgets SIM_WINS="30,236,400,352,0,0;336,172,668,472,0,1"
	sim menubar m_bar "$W" $M SIM_MENU='widgets|'
	scene milk "$OUT/m_agenda.elsm" "$OUT/m_cal.elsm" "$OUT/m_widgets.elsm" "$OUT/m_dock.elsm" "$OUT/m_bar.elsm"
fi
if want setup; then			# (Setup, the first-run wizard: its pages over the wallpaper -- "--demo <page>[b|c]"
					#  opens a page in a given state, writing nothing; a Full HD monitor, SIM_NATIVE)
	for d in 0 1 2 2b 2c 3 3b 4 5 6; do
		sim setup fb_$d "$W" SIM_ARGS="--demo $d"
		scene setup-$d "$OUT/fb_$d.elsm"
	done
fi
echo "shots: done"
