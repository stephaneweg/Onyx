#!/bin/sh
# tools/tests/server_sim/adaptive.sh -- PocketUI's phase P6, the adaptive widgets (docs/POCKETUI-TECH-STUDY.md sections
# 6.3-6.14): real apps built against the pocket UIKit's wire port, run under PocketUI at 800 x 480, 1920 x 1080, in
# portrait (480 x 800) and in console (640 x 480) -- their checks counted (expect ...; the gallery's own), their
# screens dumped to PNG pictures:
#   - the Control Panel (user/Apps/control, the SidePanel pilot): its applets as links at the left in landscape, the
#     chosen one (Language & Region, run first as an applet at the pane's size) filling the rest; portrait: the list,
#     then an applet with its "<" bar; console: the SidePanel's column; in French too;
#   - the gallery (tools/tests/server_sim/adaptive.cpp): a ToolBar with priorities and its overflow, a TabStrip, a
#     navigation SidePanel, a DataGrid with roles, a FormDialog, a context menu -- each widget's presentation checked;
#   - the pilots: the Task Manager (the grid's roles: cards in portrait), the Terminal (its tabs, no source change);
#   - the viewport: Setup (800 x 600, fixed) at 800 x 480 scrolled by the wheel over its indicator;
#   - the File Viewer: resizable, its places a SidePanel in pocket (landscape, portrait's drawer) and console.
# Usage: sh tools/tests/server_sim/adaptive.sh [out dir] [only: control gallery pilots viewport fileviewer media photos gamelib mail calendar irc ledger archiver]
# SHOTS_PNG=<folder>: the pictures copied there too (docs/compact-shell/real/).
set -e
cd "$(dirname "$0")/../../.."
WANT=" $2 "
. tools/tests/server_sim/common.sh
want () { [ "$WANT" = "  " ] || case "$WANT" in *" $1 "*) return 0 ;; *) return 1 ;; esac; }
out () { png "$1"; if [ -n "$SHOTS_PNG" ]; then mkdir -p "$SHOTS_PNG"; cp "$OUT/$1.png" "$SHOTS_PNG/"; fi; }
langdir () { d="$OUT/writes-$1"; rm -rf "$d"; mkdir -p "$d/etc"; if [ -n "$1" ]; then { grep -v '^language' sdcard/etc/system.ini; echo "language=$1"; } > "$d/etc/system.ini"; fi; echo "$d"; }
WWW="$WW;$WW;$WW"

# ---- the Control Panel ----------------------------------------------------------------------------------------------
if want control; then
	echo "adaptive: the Control Panel"
	ftapp pocket control uikit_pocket
	ftapp pocket langconf uikit_pocket
	# cpanel <screen> <tag> <mode> [lang]: the Control Panel once (the size of its applets' surface: its log), Language
	# & Region as an applet at that size, then the Control Panel showing it
	cpanel () {
		sz=$1; t=$2; md=$3; lg=$4; sfx=$t${lg:+-$lg}; WR=$(langdir "$lg")
		SIM_OVERLAY=$D/sd SIM_SCREEN=$sz SIM_MODE=$md SIM_APPNAME=control SIM_APP=control SIM_ARGS=langconf SIM_WRITES=$WR \
			SIM="$WWW;exit" "$OUT/pocket_control" > "$OUT/cp-$sfx.txt" 2>&1 || true
		pane=$(sed -n 's/^sim: surface_create //p' "$OUT/cp-$sfx.txt" | tail -1)
		echo "    $sfx: the applets' surface $pane"
		run pocket_langconf langconf-ap-$sfx "$W;$W;dump $OUT/langconf-ap-$sfx.elsm" SIM_APPLET=1 SIM_SURFSIZE=$pane SIM_SCREEN=$sz SIM_MODE=$md SIM_APPNAME=langconf SIM_APP=langconf SIM_WRITES=$WR
		run pocket_control control-$sfx "$WWW;expect kind fill;expect frame 0;$WW;dump $OUT/control-$sfx.elsm" SIM_SCREEN=$sz SIM_MODE=$md SIM_APPNAME=control SIM_APP=control \
			SIM_ARGS=langconf SIM_MAIL=40:7 SIM_SURFACE="$OUT/langconf-ap-$sfx.elsm" SIM_WRITES=$WR
		out control-$sfx
	}
	cpanel 800x480 800 pocket
	cpanel 1920x1080 1080 pocket
	cpanel 1280x720 720 pocket
	cpanel 640x480 console console
	cpanel 480x800 portrait pocket
	cpanel 800x480 800 pocket fr
	cpanel 1280x720 720 pocket fr
	# portrait: the list (no applet asked), one column
	run pocket_control control-portrait-list "$WWW;expect kind fill;dump $OUT/control-portrait-list.elsm" SIM_SCREEN=480x800 SIM_APPNAME=control SIM_APP=control
	out control-portrait-list
	# the keys: Ctrl+Page Down (R1) the next applet in landscape; Esc back to the list in portrait
	run pocket_control control-keys "$WWW;mods 1;key 0x107;mods 0;$WW;dump $OUT/control-r1.elsm" SIM_SCREEN=800x480 SIM_APPNAME=control SIM_APP=control
fi

# ---- the gallery and the widgets' host tests -----------------------------------------------------------------------
if want gallery; then
	echo "adaptive: the widgets (host tests, then the gallery)"
	$CXX $INC -Iuser/Kits/fontkit -I$FT/include -o "$OUT/pocket_gallery" "$OUT/obj/pocket"/*.o "$OUT/fakekapi.o" $S/adaptive.cpp "$OUT/libuikit_pocket.a" "$OUT/libft.a" -lpthread
	if env ADAPT_TEST=1 SIM_OVERLAY=$D/sd SIM="exit" "$OUT/pocket_gallery" >>"$OUT/log.txt" 2>&1; then echo "  tests: ok"
	else echo "  tests: FAILED (see $OUT/log.txt)"; FAIL=1; fi
	# gal <screen> <tag> <mode> [arg]: the gallery under PocketUI (an argument: a dialog, a menu, a panel opened)
	gal () {
		sz=$1; tg=$2; md=$3; ar=$4
		run pocket_gallery gallery-$tg "$WWW;expect kind fill;$WW;$WW;dump $OUT/gallery-$tg.elsm" SIM_SCREEN=$sz SIM_MODE=$md SIM_APPNAME=gallery SIM_APP=gallery SIM_ARGS=$ar
		out gallery-$tg
	}
	gal 800x480 800 pocket
	gal 1920x1080 1080 pocket
	gal 480x800 portrait pocket
	gal 640x480 console console
	gal 800x480 800-overflow pocket overflow
	gal 800x480 800-form pocket form
	gal 800x480 800-menu pocket menu
	gal 800x480 800-inspector pocket inspector
	gal 480x800 portrait-form pocket form
	gal 480x800 portrait-menu pocket menu
	gal 480x800 portrait-tabs pocket tabs
	gal 480x800 portrait-drawer pocket drawer
	gal 480x800 portrait-inspector pocket inspector
	gal 640x480 console-form console form
	gal 640x480 console-menu console menu
	# the desktop's UIKit (Elegant): the same binary as it always looks
	$CXX $INC -Iuser/Kits/fontkit -I$FT/include -o "$OUT/elegant_gallery" "$OUT/obj/elegant"/*.o "$OUT/fakekapi.o" $S/adaptive.cpp "$OUT/libuikit_wire.a" "$OUT/libft.a" -lpthread
	run elegant_gallery gallery-desktop "$W;$W;dump $OUT/gallery-desktop.elsm" SIM_SCREEN=1280x800 SIM_APPNAME=gallery SIM_APP=gallery
	out gallery-desktop
fi

# ---- the pilots: the Task Manager (the grid's roles), the Terminal (its tabs: TabStrip, no source change) ---------
if want pilots; then
	echo "adaptive: the pilots"
	ftapp pocket taskman uikit_pocket
	app pocket terminal uikit_pocket
	for t in 800x480:800 480x800:portrait 640x480:console; do
		sz=${t%%:*}; tg=${t##*:}; md=pocket; [ $tg = console ] && md=console
		run pocket_taskman taskman-$tg "$WWW;expect kind fill;$W;dump $OUT/taskman-$tg.elsm" SIM_SCREEN=$sz SIM_MODE=$md SIM_APPNAME=taskman SIM_APP=taskman
		out taskman-$tg
	done
	# landscape: the "+" clicked three times -- four tabs; portrait: the title and its count (the Terminal's least width,
	# half its first one, is over 480: the window wider than the screen, in the viewport -- P7)
	for t in "800x480:800:down 99 39;up 99 39;$W;down 180 39;up 180 39;$W;down 262 39;up 262 39;$W" "480x800:portrait:" "640x480:console:"; do
		sz=${t%%:*}; r=${t#*:}; tg=${r%%:*}; clicks=${r#*:}; md=pocket; [ $tg = console ] && md=console
		run pocket_terminal terminal-tabs-$tg "$WW;$clicks;$WW;expect kind fill;dump $OUT/terminal-tabs-$tg.elsm" SIM_SCREEN=$sz SIM_MODE=$md SIM_APPNAME=terminal SIM_PIPE="$PIPE" SIM_PIPE2='SD:/docs $ ' SIM_PIPE3='SD:/ $ ' SIM_PIPE4='SD:/ $ '
		out terminal-tabs-$tg
	done
fi

# ---- the viewport: a window bigger than the work area that will not shrink (Setup: 800 x 600, fixed) ----------------
if want viewport; then
	echo "adaptive: the viewport"
	ftapp pocket setup uikit_pocket
	run pocket_setup viewport-setup "$WWW;expect kind fill;dump $OUT/viewport-setup.elsm;wheel 795 300 -3;$W;dump $OUT/viewport-setup-scrolled.elsm" SIM_SCREEN=800x480 SIM_APPNAME=setup SIM_APP=setup
	out viewport-setup; out viewport-setup-scrolled
fi

# ---- the File Viewer: resizable, the places a SidePanel in pocket and console ------------------------------------------
if want fileviewer; then
	echo "adaptive: the File Viewer"
	ftapp pocket fileviewer uikit_pocket
	for t in 800x480:800 1920x1080:1080 480x800:portrait 640x480:console; do
		sz=${t%%:*}; tg=${t##*:}; md=pocket; [ $tg = console ] && md=console	# (not $n: run sets it)
		run pocket_fileviewer fileviewer-$tg "$WWW;expect kind fill;expect frame 0;$W;dump $OUT/fileviewer-$tg.elsm" SIM_SCREEN=$sz SIM_MODE=$md SIM_APPNAME=fileviewer SIM_APP=fileviewer SIM_ARGS=SD:/apps
		out fileviewer-$tg
	done
	# portrait: the places' drawer opened by its tab at the left edge
	run pocket_fileviewer fileviewer-portrait-drawer "$WWW;down 6 400;up 6 400;$W;dump $OUT/fileviewer-portrait-drawer.elsm" SIM_SCREEN=480x800 SIM_APPNAME=fileviewer SIM_APP=fileviewer
	out fileviewer-portrait-drawer
fi

# ---- P7, the apps migrated: the Media Player (its sidebar a navigation SidePanel) ---------------------------------------
# Needs what shots.sh builds for it (AudioKit, user/Libs/av and FFmpeg for the PC): `sh tools/tests/desktop_sim/shots.sh media`
# first; MEDIA_SHOTS = its folder (SHOTS_TMP, /tmp/onyx_shots), FFMPEG_HOST as there. Skipped when they are not there.
if want media; then
	MS=${MEDIA_SHOTS:-/tmp/onyx_shots}; FFH=${FFMPEG_HOST:-/tmp/onyx_ffmpeg_host}
	if [ -f "$MS/libaudiokit.a" ] && [ -f "$MS/av/libavhost.a" ] && [ -f "$FFH/libavcodec/libavcodec.a" ]; then
		echo "adaptive: the Media Player"
		ftapp pocket media uikit_pocket -Ithird_party "$MS/libaudiokit.a" "$MS/av/libavhost.a" -L"$FFH/libavformat" -L"$FFH/libavcodec" -L"$FFH/libswscale" -L"$FFH/libswresample" -L"$FFH/libavutil" \
			-lavformat -lavcodec -lswscale -lswresample -lavutil -lm
		for t in 800x480:800 1280x720:720 480x800:portrait 640x480:console; do
			sz=${t%%:*}; tg=${t##*:}; md=pocket; [ $tg = console ] && md=console
			WR=$(langdir "")
			run pocket_media media-$tg "$WWW;$WWW;$WWW;$WWW;expect kind fill;expect frame 0;$W;dump $OUT/media-$tg.elsm" SIM_SCREEN=$sz SIM_MODE=$md SIM_APPNAME=media SIM_APP=media SIM_WRITES=$WR SIM_SLEEP=1
			out media-$tg
		done
		# the songs' table (the rail's fourth icon), a song started: the bar below
		WR=$(langdir "")
		run pocket_media media-songs-800 "$WWW;$WWW;$WWW;$WWW;down 24 268;up 24 268;$WWW;move 300 230;down 300 230;up 300 230;wait;down 300 230;up 300 230;$WWW;dump $OUT/media-songs-800.elsm" SIM_SCREEN=800x480 SIM_APPNAME=media SIM_APP=media SIM_WRITES=$WR SIM_SLEEP=1
		out media-songs-800
		# landscape: the rail under the pointer shows its labels; portrait: the drawer opened by its tab; French
		WR=$(langdir "")
		run pocket_media media-rail-open "$WWW;$WWW;move 24 200;$W;$W;dump $OUT/media-rail-open.elsm" SIM_SCREEN=800x480 SIM_APPNAME=media SIM_APP=media SIM_WRITES=$WR SIM_SLEEP=1
		out media-rail-open
		WR=$(langdir "")
		run pocket_media media-portrait-drawer "$WWW;$WWW;$WWW;$WWW;down 6 400;up 6 400;$W;dump $OUT/media-portrait-drawer.elsm" SIM_SCREEN=480x800 SIM_APPNAME=media SIM_APP=media SIM_WRITES=$WR SIM_SLEEP=1
		out media-portrait-drawer
		WR=$(langdir fr)
		run pocket_media media-720-fr "$WWW;$WWW;dump $OUT/media-720-fr.elsm" SIM_SCREEN=1280x720 SIM_APPNAME=media SIM_APP=media SIM_WRITES=$WR SIM_SLEEP=1
		out media-720-fr
	else echo "adaptive: the Media Player skipped (no $MS/libaudiokit.a: sh tools/tests/desktop_sim/shots.sh media)"; fi
fi

# ---- P7: Photos (its sidebar a navigation SidePanel) over the made-up library of tools/tests/photos/make_samples.py -------
if want photos; then
	echo "adaptive: Photos"
	gcc -O2 -w -ffp-contract=off -Iuser -Iuser/Kits -Iuser/Runtime -Iuser/Include -Iuser/Libs -Ikernel/include -c user/Libs/gpucomp/gpucomp.c -o "$OUT/gpucomp_photos.o"
	ftapp pocket photos uikit_pocket "$OUT/gpucomp_photos.o" -Wl,--unresolved-symbols=ignore-all
	[ -d "$OUT/plib/Pictures" ] || python3 tools/tests/photos/make_samples.py "$OUT/plib" >/dev/null
	plib () { d=$(langdir "$1"); cp -r "$OUT/plib/Pictures" "$d/"; cp -r "$OUT/plib/etc/photos" "$d/etc/"; echo "$d"; }
	PW="$WWW;$WWW;$WWW;$WWW;$WWW;$WWW"		# (the scan, the thumbnails)
	for t in 800x480:800 1280x720:720 480x800:portrait 640x480:console; do
		sz=${t%%:*}; tg=${t##*:}; md=pocket; [ $tg = console ] && md=console
		WR=$(plib "")
		run pocket_photos photos-$tg "$PW;expect kind fill;expect frame 0;$W;dump $OUT/photos-$tg.elsm" SIM_SCREEN=$sz SIM_MODE=$md SIM_APPNAME=photos SIM_APP=photos SIM_WRITES=$WR SIM_SLEEP=1
		out photos-$tg
	done
	WR=$(plib "")
	run pocket_photos photos-rail-open "$PW;move 24 200;$W;$W;dump $OUT/photos-rail-open.elsm" SIM_SCREEN=800x480 SIM_APPNAME=photos SIM_APP=photos SIM_WRITES=$WR SIM_SLEEP=1
	out photos-rail-open
	WR=$(plib "")
	run pocket_photos photos-portrait-drawer "$PW;down 6 420;up 6 420;$W;dump $OUT/photos-portrait-drawer.elsm" SIM_SCREEN=480x800 SIM_APPNAME=photos SIM_APP=photos SIM_WRITES=$WR SIM_SLEEP=1
	out photos-portrait-drawer
	WR=$(plib fr)
	run pocket_photos photos-720-fr "$PW;dump $OUT/photos-720-fr.elsm" SIM_SCREEN=1280x720 SIM_APPNAME=photos SIM_APP=photos SIM_WRITES=$WR SIM_SLEEP=1
	out photos-720-fr
fi

# ---- P7: the Game Library (its sidebar a SidePanel in pocket and console; the desktop keeps its own) ---------------------
if want gamelib; then
	echo "adaptive: the Game Library"
	ftapp pocket gamelib uikit_pocket user/Emulators/gb/gb.cpp $(ls user/Emulators/gba/*.cpp user/Emulators/nes/*.cpp user/Emulators/snes/*.cpp)
	for t in 800x480:800 1280x720:720 480x800:portrait 640x480:console; do
		sz=${t%%:*}; tg=${t##*:}; md=pocket; [ $tg = console ] && md=console
		WR=$(langdir ""); python3 $D/gamelib_samples.py "$WR"
		run pocket_gamelib gamelib-$tg "$WWW;$WWW;expect kind fill;expect frame 0;$W;dump $OUT/gamelib-$tg.elsm" SIM_SCREEN=$sz SIM_MODE=$md SIM_APPNAME=gamelib SIM_APP=gamelib SIM_WRITES=$WR
		out gamelib-$tg
	done
	WR=$(langdir ""); python3 $D/gamelib_samples.py "$WR"
	run pocket_gamelib gamelib-portrait-drawer "$WWW;$WWW;down 6 400;up 6 400;$W;dump $OUT/gamelib-portrait-drawer.elsm" SIM_SCREEN=480x800 SIM_APPNAME=gamelib SIM_APP=gamelib SIM_WRITES=$WR
	out gamelib-portrait-drawer
	WR=$(langdir fr); python3 $D/gamelib_samples.py "$WR"
	run pocket_gamelib gamelib-720-fr "$WWW;$WWW;dump $OUT/gamelib-720-fr.elsm" SIM_SCREEN=1280x720 SIM_APPNAME=gamelib SIM_APP=gamelib SIM_WRITES=$WR
	out gamelib-720-fr
fi

# ---- P7: Mail (its sidebar a SidePanel; one pane at a time in a narrow window) against shots.sh's made-up mailboxes -------
# Needs what shots.sh builds for it (mbedTLS for the PC, mkaccounts): MEDIA_SHOTS = its folder; skipped when they are not there.
if want mail; then
	MS=${MEDIA_SHOTS:-/tmp/onyx_shots}; M=third_party/mbedtls-3.6.3
	if [ -f "$MS/libmb.a" ] && [ -x "$MS/mkaccounts" ]; then
		echo "adaptive: Mail"
		ftapp pocket mail uikit_pocket -I$M/include "$MS/libmb.a"
		MB=$(( 34000 + $$ % 500 * 4 ))
		python3 tools/tests/mail/fakemail.py --imap $MB --pop $((MB+1)) --smtp $((MB+2)) --http $((MB+3)) --demo personal --user me@example.com >"$OUT/mail-srv1.log" 2>&1 &
		MS1=$!
		python3 tools/tests/mail/fakemail.py --imap $((MB+100)) --pop $((MB+101)) --smtp $((MB+102)) --http $((MB+103)) --demo work --user steph@atelier-lumen.example >"$OUT/mail-srv2.log" 2>&1 &
		MS2=$!
		sleep 2
		ML="$OUT/mailw0"; rm -rf "$ML"; mkdir -p "$ML"
		SIM_WRITES="$ML" SIM=exit "$MS/mkaccounts" "Personal|me@example.com|Stéphane|imap|127.0.0.1|$MB|$((MB+2))|secret|#D93025|gmail" \
			"Atelier|steph@atelier-lumen.example|Stéphane|pop3|127.0.0.1|$((MB+101))|$((MB+102))|secret|#1A73E8" >>"$OUT/log.txt" 2>&1
		W40="$(printf 'wait;%.0s' $(seq 40))"
		# mrun <name> <screen> <mode> <lang> <script>: Mail over a fresh copy of the accounts
		mrun () { mn=$1; msz=$2; mmd=$3; mlg=$4; msc=$5
			rm -rf "$OUT/mailw"; cp -r "$ML" "$OUT/mailw"
			if [ -n "$mlg" ]; then { grep -v '^language' sdcard/etc/system.ini; echo "language=$mlg"; } > "$OUT/mailw/etc/system.ini"; fi
			run pocket_mail $mn "$W40;$W40;$W40;$msc;dump $OUT/$mn.elsm" SIM_SCREEN=$msz SIM_MODE=$mmd SIM_APPNAME=mail SIM_APP=mail SIM_WRITES="$OUT/mailw" SIM_SLEEP=1 SIM_REALNET=1 SIM_REALCLOCK=1
			out $mn; }
		mrun mail-800 800x480 pocket "" "expect kind fill;expect frame 0;down 200 250;up 200 250;$W40"
		mrun mail-720-fr 1280x720 pocket fr "down 380 270;up 380 270;$W40"
		mrun mail-portrait 480x800 pocket "" "$W"
		mrun mail-portrait-read 480x800 pocket "" "down 240 280;up 240 280;$W40"
		mrun mail-portrait-drawer 480x800 pocket "" "down 6 420;up 6 420;$W"
		mrun mail-console 640x480 console "" "$W"
		kill $MS1 $MS2 2>/dev/null
	else echo "adaptive: Mail skipped (no $MS/libmb.a, $MS/mkaccounts: sh tools/tests/desktop_sim/shots.sh mail)"; fi
fi

# ---- P7: the Calendar (its side -- the month, the calendars, the tasks -- a SidePanel of free content: a drawer in pocket) --
if want calendar; then
	echo "adaptive: the Calendar"
	ftapp pocket calendar uikit_pocket
	for t in 800x480:800 1280x720:720 480x800:portrait 640x480:console; do
		sz=${t%%:*}; tg=${t##*:}; md=pocket; [ $tg = console ] && md=console
		WR=$(langdir "")
		run pocket_calendar calendar-$tg "$WWW;expect kind fill;expect frame 0;$W;dump $OUT/calendar-$tg.elsm" SIM_SCREEN=$sz SIM_MODE=$md SIM_APPNAME=calendar SIM_APP=calendar SIM_WRITES=$WR SIM_DATE=20260930 SIM_TIME=1042
		out calendar-$tg
	done
	WR=$(langdir "")
	run pocket_calendar calendar-800-drawer "$WWW;down 6 250;up 6 250;$W;dump $OUT/calendar-800-drawer.elsm" SIM_SCREEN=800x480 SIM_APPNAME=calendar SIM_APP=calendar SIM_WRITES=$WR SIM_DATE=20260930 SIM_TIME=1042
	out calendar-800-drawer
	WR=$(langdir fr)
	run pocket_calendar calendar-720-fr "$WWW;dump $OUT/calendar-720-fr.elsm" SIM_SCREEN=1280x720 SIM_APPNAME=calendar SIM_APP=calendar SIM_WRITES=$WR SIM_DATE=20260930 SIM_TIME=1042
	out calendar-720-fr
fi

# ---- P7: IRC (its conversations a SidePanel in pocket and console; the desktop keeps its tree and splitter) -----------------
# The canned session is shots.sh's (its A= and N= lines, taken from there: one source).
if want irc; then
	echo "adaptive: IRC"
	app pocket irc uikit_pocket
	eval "$(sed -n '/^if want irc; then/,/^fi/p' $D/shots.sh | grep -E '^[[:space:]]*(A|N)=')"
	for t in 800x480:800 1280x720:720 480x800:portrait 640x480:console; do
		sz=${t%%:*}; tg=${t##*:}; md=pocket; [ $tg = console ] && md=console
		WR=$(langdir "")
		run pocket_irc irc-$tg "$WWW;expect kind fill;expect frame 0;$W;dump $OUT/irc-$tg.elsm" SIM_SCREEN=$sz SIM_MODE=$md SIM_APPNAME=irc SIM_APP=irc SIM_WRITES=$WR SIM_NET="$N"
		out irc-$tg
	done
	WR=$(langdir "")
	run pocket_irc irc-rail-open "$WWW;move 24 150;$W;$W;dump $OUT/irc-rail-open.elsm" SIM_SCREEN=800x480 SIM_APPNAME=irc SIM_APP=irc SIM_WRITES=$WR SIM_NET="$N"
	out irc-rail-open
	WR=$(langdir fr)
	run pocket_irc irc-720-fr "$WWW;dump $OUT/irc-720-fr.elsm" SIM_SCREEN=1280x720 SIM_APPNAME=irc SIM_APP=irc SIM_WRITES=$WR SIM_NET="$N"
	out irc-720-fr
fi

# ---- P7: Ledger (its side bar a SidePanel with a header -- the company, the fiscal year -- and a footer) over the demo company
if want ledger; then
	echo "adaptive: Ledger"
	ftapp pocket ledger uikit_pocket
	L=SIM_ARGS=SD:/docs/demo-company.ledger
	for t in 800x480:800 1280x720:720 480x800:portrait 640x480:console; do
		sz=${t%%:*}; tg=${t##*:}; md=pocket; [ $tg = console ] && md=console
		WR=$(langdir "")
		run pocket_ledger ledger-$tg "$WWW;expect kind fill;expect frame 0;$W;dump $OUT/ledger-$tg.elsm" SIM_SCREEN=$sz SIM_MODE=$md SIM_APPNAME=ledger SIM_APP=ledger SIM_WRITES=$WR $L
		out ledger-$tg
	done
	WR=$(langdir "")
	run pocket_ledger ledger-rail-open "$WWW;move 24 200;$W;$W;dump $OUT/ledger-rail-open.elsm" SIM_SCREEN=800x480 SIM_APPNAME=ledger SIM_APP=ledger SIM_WRITES=$WR $L
	out ledger-rail-open
	WR=$(langdir "")
	run pocket_ledger ledger-portrait-drawer "$WWW;down 6 400;up 6 400;$W;dump $OUT/ledger-portrait-drawer.elsm" SIM_SCREEN=480x800 SIM_APPNAME=ledger SIM_APP=ledger SIM_WRITES=$WR $L
	out ledger-portrait-drawer
	WR=$(langdir fr)
	run pocket_ledger ledger-720-fr "$WWW;dump $OUT/ledger-720-fr.elsm" SIM_SCREEN=1280x720 SIM_APPNAME=ledger SIM_APP=ledger SIM_WRITES=$WR $L
	out ledger-720-fr
fi

# ---- P7: the Archiver (pocket, console: its folders and card a SidePanel's content -- a drawer on a small screen) -------
if want archiver; then
	echo "adaptive: the Archiver"
	mkdir -p "$OUT/arcz"
	for f in adler32 crc32 deflate inflate inffast inftrees trees zutil; do gcc -O2 -w -c third_party/zlib-1.3.1/$f.c -o "$OUT/arcz/$f.o"; done
	ftapp pocket archiver uikit_pocket -Ithird_party/zlib-1.3.1 -Iuser/Apps/archiver -Iuser/Kits/filekit user/Kits/filekit/fkcore.cpp "$OUT"/arcz/*.o
	rm -rf "$OUT/arc"; mkdir -p "$OUT/arc"; python3 $D/arc_sample.py "$OUT/arc/Projet-Onyx.zip"
	A="SIM_RAM=$OUT/arc SIM_ARGS=RAM:/Projet-Onyx.zip"
	for t in 800x480:800 1280x720:720 480x800:portrait 640x480:console; do
		sz=${t%%:*}; tg=${t##*:}; md=pocket; [ $tg = console ] && md=console
		WR=$(langdir "")
		run pocket_archiver archiver-$tg "$WWW;expect kind fill;expect frame 0;$W;dump $OUT/archiver-$tg.elsm" SIM_SCREEN=$sz SIM_MODE=$md SIM_APPNAME=archiver SIM_APP=archiver SIM_WRITES=$WR $A
		out archiver-$tg
	done
	WR=$(langdir "")
	run pocket_archiver archiver-800-drawer "$WWW;down 14 300;up 14 300;$W;dump $OUT/archiver-800-drawer.elsm" SIM_SCREEN=800x480 SIM_APPNAME=archiver SIM_APP=archiver SIM_WRITES=$WR $A
	out archiver-800-drawer
	WR=$(langdir fr)
	run pocket_archiver archiver-720-fr "$WWW;dump $OUT/archiver-720-fr.elsm" SIM_SCREEN=1280x720 SIM_APPNAME=archiver SIM_APP=archiver SIM_WRITES=$WR $A
	out archiver-720-fr
fi

grep -h "server_sim: FAIL" "$OUT/log.txt" && FAIL=1
echo "  checks: $(grep -c 'server_sim: PASS' "$OUT/log.txt") passed, $(grep -c 'server_sim: FAIL' "$OUT/log.txt") failed"
[ $FAIL = 0 ] && echo "adaptive: all passed ($OUT)" || echo "adaptive: FAILED ($OUT)"
exit $FAIL
