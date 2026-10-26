#!/bin/sh
# tools/tests/server_sim/pocketterm.sh -- PocketUI on the Waveshare PocketTerm35 (640 x 480, a D-pad and buttons, no
# pointer): the flat and dark look "Onyx Noir", the app's menus as ONE Menu entry opened by the keys, the toolbars that
# adapt, the pocket shell and its buttons (docs/COMPACT-SHELL-STUDY.md section 20; the mock-ups:
# docs/compact-shell/mockups/pocketterm/). Real apps built against the pocket UIKit's wire port, run under PocketUI,
# their checks counted, their screens dumped to PNG pictures.
#
# Usage: sh tools/tests/server_sim/pocketterm.sh [out dir] [only: theme menu toolbar input shell]
# SHOTS_PNG=<folder>: the pictures copied there too (docs/compact-shell/real/pocketterm/).
set -e
cd "$(dirname "$0")/../../.."
WANT=" $2 "
. tools/tests/server_sim/common.sh
want () { [ "$WANT" = "  " ] || case "$WANT" in *" $1 "*) return 0 ;; *) return 1 ;; esac; }
out () { png "$1"; if [ -n "$SHOTS_PNG" ]; then mkdir -p "$SHOTS_PNG"; cp "$OUT/$1.png" "$SHOTS_PNG/"; fi; }
# langdir <lang> [theme]: a writable overlay with that language (and SD:/etc/pocketui.ini's "[look] theme =")
langdir () {
	d="$OUT/writes-$1-$2"; rm -rf "$d"; mkdir -p "$d/etc"
	if [ -n "$1" ]; then { grep -v '^language' sdcard/etc/system.ini; echo "language=$1"; } > "$d/etc/system.ini"; fi
	if [ -n "$2" ]; then { cat sdcard/etc/pocketui.ini; printf '[look]\ntheme = %s\n' "$2"; } > "$d/etc/pocketui.ini"; fi
	echo "$d"
}
# pixel <picture> <x> <y> <rrggbb> <name>: one pixel of a PNG picture checked
pixel () {
	got=$(python3 -c "from PIL import Image; im = Image.open ('$OUT/$1.png').convert ('RGB'); print ('%02x%02x%02x' % im.getpixel (($2, $3)))")
	if [ "$got" = "$4" ]; then echo "  $5: ok"; else echo "  $5: FAILED (pixel $2,$3 of $1 is $got, not $4)"; FAIL=1; fi
}
# tone <picture> <x> <y> dark|light <name>: one pixel's brightness (under / over 50 %)
tone () {
	got=$(python3 -c "from PIL import Image; im = Image.open ('$OUT/$1.png').convert ('L'); print ('dark' if im.getpixel (($2, $3)) < 128 else 'light')")
	if [ "$got" = "$4" ]; then echo "  $5: ok"; else echo "  $5: FAILED (pixel $2,$3 of $1 is $got)"; FAIL=1; fi
}
# said <text> <name>: a line of the log
said () { if grep -q "$1" "$OUT/log.txt"; then echo "  $2: ok"; else echo "  $2: FAILED (no \"$1\" in $OUT/log.txt)"; FAIL=1; fi; }
WWW="$WW;$WW;$WW"
F10=0x119; UP=0x100; DOWN=0x101; LEFT=0x102; RIGHT=0x103
MENUS='MFile|I1~New~^N|I2~Open...~^O|I3~Save~^S|-|I4~Print...~^P|MEdit|I5~Undo~^Z|I6~Redo~^Y|-|I7~Cut~^X|I8~Copy~^C|I9~Paste~^V|MView|I10~Zoom In~|I11~Zoom Out~|MInsert|I12~Picture...~|I13~Table...~|MFormat|I14~Font...~|I15~Paragraph...~|-|I16~Bold~^B|I17~Italic~^I|I18~Underline~^U|-|I19~Page Setup...~|MTable|I20~Insert Row~|MTools|I21~Word Count~|MHelp|I22~About Letters~'

# ---- the menu: ONE entry in the band, opened by F10 / Alt alone, moved by the keys --------------------------------------
if want menu; then
	echo "pocketterm: the Menu entry (the menu bar under PocketUI)"
	ftapp pocket menubar uikit_pocket user/Apps/clock/alarms.cpp user/Apps/clock/clocktime.cpp
	bar () {	# bar <screen> <tag> <lang> <theme>
		sz=$1; t=$2; lg=$3; th=$4; w=${sz%x*}; h=${sz#*x}; WR=$(langdir "$lg" "$th")
		E="SIM_SCREEN=$sz SIM_APPNAME=menubar SIM_APP=menubar SIM_WRITES=$WR SIM_SERVICES=shell"
		OTH="other $w $((h - 30)) Letters;othermenu $MENUS;$W;$W;$W"
		# on the home: the menu key opens the system's rows (Open apps, Quick settings, Shut Down...)
		run pocket_menubar pt-band-home-$t "$W;$W;expect bar 1;dump $OUT/pt-band-home-$t.elsm;mods 8;mods 0;$W;expect bargrab 1;dump $OUT/pt-menu-home-$t.elsm;key 0x1b;$W;expect bargrab 0" $E
		# an app in front: the band shows its name and the Menu entry; F10 opens the column (the keys grabbed), Down x4 and
		# Right the Format menu's items, Down the second one; Esc goes back to the column, Esc again closes it
		SC="$OTH;expect bargrab 0;dump $OUT/pt-band-app-$t.elsm;key $F10;$W;expect bargrab 1;dump $OUT/pt-menu-focus-$t.elsm"
		SC="$SC;key $DOWN;key $DOWN;key $DOWN;key $DOWN;key $RIGHT;key $DOWN;key $DOWN;key $DOWN;$W;expect bargrab 1;dump $OUT/pt-menu-open-$t.elsm"
		SC="$SC;key 0x1b;$W;expect bargrab 1;key 0x1b;$W;expect bargrab 0"
		# THE MENU KEY: Super (CMD) pressed and released alone opens it; again closes it; F10 then F10 the same; Alt alone
		# is not the menu key (it was), nor Super + a key
		SC="$SC;mods 8;mods 0;$W;expect bargrab 1;dump $OUT/pt-menu-cmd-$t.elsm;mods 8;mods 0;$W;expect bargrab 0;key $F10;$W;expect bargrab 1;key $F10;$W;expect bargrab 0"
		SC="$SC;mods 4;mods 0;$W;expect bargrab 0;mods 8;key x;mods 0;$W;expect bargrab 0"
		# an item chosen: its command sent to the app (Edit > Copy: id 8), the menu closed
		SC="$SC;key $F10;key $DOWN;key $RIGHT;key $DOWN;key $DOWN;key $DOWN;key 13;$W;expect bargrab 0;expect menucmd 8"
		run pocket_menubar pt-menu-$t "$SC" $E
		for p in pt-band-home-$t pt-band-app-$t pt-menu-focus-$t pt-menu-open-$t pt-menu-home-$t; do out $p; done
	}
	bar 640x480 640 "" ""
	bar 800x480 800 "" ""
	bar 640x480 640-fr fr ""
	bar 640x480 640-milk "" system
	# the flat look: the band continues the glass (bg0), the open column is a raised surface (bg2), the chosen item the accent
	pixel pt-band-app-640 320 15 0b0c0e "the band is flat and dark"
	pixel pt-menu-open-640 320 470 0b0c0e "the hint line's strip"
	pixel pt-menu-open-640 420 278 3d86da "the chosen item is the accent"
	tone pt-band-app-640-milk 320 15 light "theme = system: the light band kept"
fi

# ---- the theme: Onyx Noir is pocket's default; the desktop, the console and "theme = system" keep theirs -----------------
if want theme; then
	echo "pocketterm: the theme (the widgets' gallery, real apps)"
	$CXX $INC -Iuser/Kits/fontkit -I$FT/include -o "$OUT/pocket_gallery" "$OUT/obj/pocket"/*.o "$OUT/fakekapi.o" $S/adaptive.cpp "$OUT/libuikit_pocket.a" "$OUT/libft.a" -lpthread
	gal () {	# gal <screen> <tag> <mode> <theme> [arg]
		sz=$1; tg=$2; md=$3; th=$4; ar=$5
		run pocket_gallery pt-gallery-$tg "$WWW;expect kind fill;$WW;$WW;dump $OUT/pt-gallery-$tg.elsm" SIM_SCREEN=$sz SIM_MODE=$md SIM_APPNAME=gallery SIM_APP=gallery SIM_ARGS=$ar SIM_WRITES=$(langdir "" "$th")
		out pt-gallery-$tg
	}
	gal 640x480 640 pocket ""
	gal 800x480 800 pocket ""
	gal 640x480 640-form pocket "" form
	gal 640x480 640-menu pocket "" menu
	gal 640x480 640-overflow pocket "" overflow
	gal 640x480 640-system pocket system
	gal 640x480 640-console console ""
	ftapp pocket control uikit_pocket
	run pocket_control pt-control-640 "$WWW;expect kind fill;$WW;dump $OUT/pt-control-640.elsm" SIM_SCREEN=640x480 SIM_APPNAME=control SIM_APP=control
	out pt-control-640
	app pocket tinypad uikit_pocket
	run pocket_tinypad pt-tinypad-640 "$WW;key H;key i;$W;dump $OUT/pt-tinypad-640.elsm" SIM_SCREEN=640x480 SIM_APPNAME=tinypad SIM_APP=tinypad
	out pt-tinypad-640
	app elegant tinypad uikit_wire
	run elegant_tinypad pt-desktop-tinypad "$WW;dump $OUT/pt-desktop-tinypad.elsm" SIM_SCREEN=800x600 SIM_APPNAME=tinypad SIM_APP=tinypad
	out pt-desktop-tinypad
	tone pt-gallery-640 630 80 dark "pocket: the widgets are on Onyx Noir"
	pixel pt-tinypad-640 600 400 15171b "pocket: a text field is a dark surface"
	tone pt-gallery-640-system 630 80 light "theme = system: theme.txt's look"
	tone pt-desktop-tinypad 400 300 light "the desktop: theme.txt's look"
fi

# ---- the toolbars adapt: UIKit's ToolBar (the host tests), Letters' two bars in one row at 640 x 480 ---------------------
if want toolbar; then
	echo "pocketterm: the toolbars (UIKit's host tests, Letters)"
	$CXX $INC -Iuser/Kits/fontkit -I$FT/include -o "$OUT/pocket_gallery" "$OUT/obj/pocket"/*.o "$OUT/fakekapi.o" $S/adaptive.cpp "$OUT/libuikit_pocket.a" "$OUT/libft.a" -lpthread
	if env ADAPT_TEST=1 SIM_OVERLAY=$D/sd SIM="exit" "$OUT/pocket_gallery" >>"$OUT/log.txt" 2>&1; then echo "  the widgets' host tests: ok"
	else echo "  the widgets' host tests: FAILED (see $OUT/log.txt)"; FAIL=1; fi
	ftapp pocket letters uikit_pocket -Wl,--unresolved-symbols=ignore-all
	ftapp elegant letters uikit_wire -Wl,--unresolved-symbols=ignore-all
	DOC=; for f in letters-tour.rtf new-year-letter.rtf; do if [ -f "$D/sd/docs/$f" ] || [ -f "sdcard/docs/$f" ]; then DOC="SD:/docs/$f"; break; fi; done
	for sz in 640x480 800x480 480x800 1280x720; do
		w=${sz%x*}
		# the window filled, one row of tools; a click on "»": its panel; Right, Right move the ring; Esc closes it
		run pocket_letters pt-letters-$sz "$WW;$WW;$W;expect kind fill;move 320 300;$W;$W;dump $OUT/pt-letters-$sz.elsm;down $((w - 20)) 47;up $((w - 20)) 47;$W;key $RIGHT;key $RIGHT;$W;dump $OUT/pt-letters-over-$sz.elsm;key 0x1b;$W;move 320 300;$W;$W;dump $OUT/pt-letters-back-$sz.elsm" \
			SIM_SCREEN=$sz SIM_APPNAME=letters SIM_APP=letters ${DOC:+SIM_ARGS=$DOC}
		out pt-letters-$sz; out pt-letters-over-$sz
		png pt-letters-back-$sz >/dev/null
		# (the band and the toolbar: the page under them has a caret that blinks)
		if python3 -c "import sys; from PIL import Image, ImageChops; a = Image.open ('$OUT/pt-letters-$sz.png').convert ('RGB').crop ((0, 0, $w, 64)); b = Image.open ('$OUT/pt-letters-back-$sz.png').convert ('RGB').crop ((0, 0, $w, 64)); sys.exit (1 if ImageChops.difference (a, b).getbbox () else 0)"; then echo "  letters $sz: the row as before once the panel is closed: ok"
		else echo "  letters $sz: FAILED (the row changed after the panel: pt-letters-back-$sz.elsm)"; FAIL=1; fi
	done
	run elegant_letters pt-letters-desktop "$WW;$WW;dump $OUT/pt-letters-desktop.elsm" SIM_SCREEN=1024x768 SIM_APPNAME=letters SIM_APP=letters ${DOC:+SIM_ARGS=$DOC}
	out pt-letters-desktop
	pixel pt-letters-640x480 40 300 ffffff "letters: the page stays white in the dark shell"
fi

# ---- InputKit: the keys and a pad as the same actions; L / R the tabs of any window ------------------------------------
# same <picture A> <picture B> <x0> <y0> <x1> <y1> yes|no <name>: that part of two pictures is the same (or is not)
same () {
	got=$(python3 -c "from PIL import Image, ImageChops; a = Image.open ('$OUT/$1.png').convert ('RGB').crop (($3, $4, $5, $6)); b = Image.open ('$OUT/$2.png').convert ('RGB').crop (($3, $4, $5, $6)); print ('no' if ImageChops.difference (a, b).getbbox () else 'yes')")
	if [ "$got" = "$7" ]; then echo "  $8: ok"; else echo "  $8: FAILED ($1 / $2: the same? $got)"; FAIL=1; fi
}
if want input; then
	echo "pocketterm: InputKit (the host test; the Terminal's and the gallery's tabs, the home, by keys and by a simulated pad)"
	$CXX $INC -o "$OUT/inputkit_test" $S/inputkit_test.cpp "$OUT/fakekapi.o" user/Kits/inputkit/inputkit.cpp -lpthread
	if env SIM_PAD=1 SIM_OVERLAY=$D/sd "$OUT/inputkit_test" >>"$OUT/log.txt" 2>&1; then echo "  inputkit's host test: ok"
	else echo "  inputkit's host test: FAILED (see $OUT/log.txt)"; FAIL=1; fi
	# the simulated pad's layout (server_sim.cpp SimPad): an unknown model, SD:/etc/gamepad.ini's [default]
	WR=$(langdir "" ""); printf '[default]\na = 1\nb = 2\nx = 3\ny = 4\nl = 5\nr = 6\nselect = 7\nstart = 8\nhome = 9\ndpad = hat\n' > "$WR/etc/gamepad.ini"
	PL=16; PR=32; PRIGHT=0x2000; PA=1
	ftapp pocket terminal uikit_pocket
	# THE TERMINAL: three tabs (F12 twice), the third shown. Ctrl+Page Up: the second; Ctrl+Page Down: the third again;
	# the pad's L: the second; R: the third; Ctrl+Tab from the third: the first
	T="$WW;key 0x11b;$W;key 0x11b;$W;$W;dump $OUT/in-term-3.elsm;mods 1;key 0x106;mods 0;$W;$W;dump $OUT/in-term-pgup.elsm;mods 1;key 0x107;mods 0;$W;$W;dump $OUT/in-term-pgdn.elsm"
	T="$T;pad $PL;$W;$W;pad 0;$W;$W;dump $OUT/in-term-l.elsm;pad $PR;$W;$W;pad 0;$W;$W;dump $OUT/in-term-r.elsm;mods 1;key 0x09;mods 0;$W;$W;dump $OUT/in-term-ctab.elsm"
	run pocket_terminal in-terminal "$T" SIM_SCREEN=640x480 SIM_APPNAME=terminal SIM_APP=terminal SIM_PIPE="$PIPE" SIM_WRITES=$WR
	for p in 3 pgup pgdn l r ctab; do png in-term-$p >/dev/null; done
	same in-term-3 in-term-pgup 0 30 640 60 no "terminal: Ctrl+Page Up shows another tab"
	same in-term-3 in-term-pgdn 0 30 640 60 yes "terminal: Ctrl+Page Down comes back"
	same in-term-pgup in-term-l 0 30 640 60 yes "terminal: the pad's L is Ctrl+Page Up"
	same in-term-3 in-term-r 0 30 640 60 yes "terminal: the pad's R is Ctrl+Page Down"
	same in-term-3 in-term-ctab 0 30 640 60 no "terminal: Ctrl+Tab goes on (to the first tab)"
	same in-term-pgup in-term-ctab 0 30 640 60 no "terminal: ... which is not the second"
	# ANOTHER TABBED WINDOW, with no code of its own for it: the gallery (a TabStrip): R, Ctrl+Page Down, then L
	$CXX $INC -Iuser/Kits/fontkit -I$FT/include -o "$OUT/pocket_gallery" "$OUT/obj/pocket"/*.o "$OUT/fakekapi.o" $S/adaptive.cpp "$OUT/libuikit_pocket.a" "$OUT/libft.a" -lpthread
	G="$WWW;$WW;move 320 300;$W;dump $OUT/in-gal-0.elsm;pad $PR;$W;$W;pad 0;$W;$W;dump $OUT/in-gal-r.elsm;pad $PL;$W;$W;pad 0;$W;$W;dump $OUT/in-gal-l.elsm;mods 1;key 0x107;mods 0;$W;$W;dump $OUT/in-gal-pgdn.elsm"
	# (2026-10-26) Tab that no control takes steps the tabs (the PocketTerm35's L and R type letters): Tab, then Shift+Tab
	G="$G;key 0x09;$W;$W;dump $OUT/in-gal-tab.elsm;mods 2;key 0x09;mods 0;$W;$W;dump $OUT/in-gal-stab.elsm"
	run pocket_gallery in-gallery "$G" SIM_SCREEN=640x480 SIM_APPNAME=gallery SIM_APP=gallery SIM_WRITES=$WR
	for p in 0 r l pgdn tab stab; do png in-gal-$p >/dev/null; done
	same in-gal-0 in-gal-r 0 60 640 100 no "gallery: the pad's R shows the next tab"
	same in-gal-0 in-gal-l 0 60 640 100 yes "gallery: the pad's L comes back"
	same in-gal-r in-gal-pgdn 0 60 640 100 yes "gallery: Ctrl+Page Down is the pad's R"
	same in-gal-pgdn in-gal-tab 0 60 640 100 no "gallery: Tab shows the next tab"
	same in-gal-pgdn in-gal-stab 0 60 640 100 yes "gallery: Shift+Tab comes back"
	# THE SHELL'S HOME: the pad's Right moves the focus as the Right key does; R changes the category as Ctrl+Page Down
	ftapp pocket pocketshell uikit_pocket
	mkdir -p "$WR/etc/pocket"; printf 'terminal\ntinypad\nletters\nfileviewer\ntinycalc\ncalendar\n' > "$WR/etc/pocket/recent"
	H="$W;$W;expect home 1;dump $OUT/in-home-0.elsm;key $RIGHT;$W;$W;dump $OUT/in-home-key.elsm;key $LEFT;$W;$W;pad $PRIGHT;wait;pad 0;$W;$W;dump $OUT/in-home-pad.elsm"
	H="$H;pad $PR;$W;$W;pad 0;$W;$W;dump $OUT/in-home-r.elsm;pad $PL;$W;$W;pad 0;$W;mods 1;key 0x107;mods 0;$W;$W;dump $OUT/in-home-pgdn.elsm"
	H="$H;key 0x09;$W;$W;dump $OUT/in-home-tab.elsm"
	run pocket_pocketshell in-home "$H" SIM_SCREEN=640x480 SIM_APPNAME=pocketshell SIM_APP=pocketshell SIM_WRITES=$WR
	for p in 0 key pad r pgdn tab; do png in-home-$p >/dev/null; done
	same in-home-0 in-home-key 0 0 640 480 no "home: the Right key moves the focus"
	same in-home-key in-home-pad 0 0 640 480 yes "home: the pad's Right does the same"
	same in-home-pad in-home-r 0 0 640 480 no "home: the pad's R changes the category"
	same in-home-r in-home-pgdn 0 0 640 480 yes "home: Ctrl+Page Down is the pad's R"
	same in-home-pgdn in-home-tab 0 0 640 480 no "home: Tab changes the category"
	if grep -q "a pad's buttons reach the shell (InputKit)" "$OUT/log.txt"; then echo "  home: the pad read through InputKit: ok"; else echo "  home: the pad through InputKit: FAILED (no line in the log)"; FAIL=1; fi
	cp "$OUT/in-term-pgup.png" "$OUT/pt-terminal-tabs-640.png"; if [ -n "$SHOTS_PNG" ]; then mkdir -p "$SHOTS_PNG"; cp "$OUT/pt-terminal-tabs-640.png" "$SHOTS_PNG/"; fi
fi

# ---- the pocket shell: the home, the switcher, quick settings -- the menu bar's picture above them ------------------------
if want shell; then
	echo "pocketterm: the pocket shell"
	ftapp pocket pocketshell uikit_pocket
	ftapp pocket menubar uikit_pocket user/Apps/clock/alarms.cpp user/Apps/clock/clocktime.cpp
	ftapp pocket terminal uikit_pocket
	app pocket tinypad uikit_pocket
	shell () {	# shell <screen> <tag> [lang]
		sz=$1; t=$2; lg=$3; w=${sz%x*}; h=${sz#*x}; sfx=$t${lg:+-$lg}; WR=$(langdir "$lg" "")
		mkdir -p "$WR/etc/pocket"; printf 'terminal\ntinypad\nletters\nfileviewer\ntinycalc\ncalendar\njet\nmail\ngamelib\nmedia\n' > "$WR/etc/pocket/recent"
		run pocket_terminal pt-app-term-$sfx "$WW;dump $OUT/pt-app-term-$sfx.elsm" SIM_SCREEN=$sz SIM_APPNAME=terminal SIM_APP=terminal SIM_PIPE="$PIPE" SIM_WRITES=$WR
		run pocket_tinypad pt-app-pad-$sfx "$WW;key N;key o;key t;key e;key s;$W;dump $OUT/pt-app-pad-$sfx.elsm" SIM_SCREEN=$sz SIM_APPNAME=tinypad SIM_APP=tinypad SIM_WRITES=$WR
		run pocket_menubar pt-bar-home-$sfx "$W;$W;dump $OUT/pt-bar-home-$sfx.elsm" SIM_SCREEN=$sz SIM_APPNAME=menubar SIM_APP=menubar SIM_WRITES=$WR
		run pocket_menubar pt-bar-pad-$sfx "other $w $((h - 30)) Notes;othermenu MFile|I1~New~^N|MEdit|I2~Copy~^C;$W;$W;$W;dump $OUT/pt-bar-pad-$sfx.elsm" \
			SIM_SCREEN=$sz SIM_APPNAME=menubar SIM_APP=menubar SIM_WRITES=$WR
		APPS="otherpic $w $((h - 30)) letters $OUT/pt-app-pad-$sfx.elsm 0 30;$W;otherpic $w $((h - 30)) terminal $OUT/pt-app-term-$sfx.elsm 0 30;$W;otherpic $w $((h - 30)) tinypad $OUT/pt-app-pad-$sfx.elsm 0 30;$W"
		E="SIM_SCREEN=$sz SIM_APPNAME=pocketshell SIM_APP=pocketshell SIM_WRITES=$WR"
		# the home: Super shows it (the keys the shell's); Right moves the focus; Tab the next category; Down to the Running pills
		SC="otherpic $w 30 menubar $OUT/pt-bar-home-$sfx.elsm 0 0 0x35 0 0;$W;$W;expect shell app;expect kind home;$APPS;expect front other;expect tasks 3"
		SC="$SC;mods 8;mods 0;$W;$W;expect home 1;expect focus app;key $RIGHT;$W;$W;dump $OUT/pt-home-$sfx.elsm"
		# typing searches; Esc clears; Ctrl+Page Down (R) the next category; Super (Start) on the home: the open apps, Esc stays;
		# Esc on the home: back to the app
		SC="$SC;key t;key e;$W;$W;dump $OUT/pt-search-$sfx.elsm;key 0x1b;$W;mods 1;key 0x107;mods 0;$W;$W;dump $OUT/pt-home-tab-$sfx.elsm"
		SC="$SC;mods 8;mods 0;$W;$W;expect shown1 1;expect home 1;key 0x1b;$W;expect shown1 0;expect home 1;key 0x1b;$W;$W;expect home 0;expect front other"
		# Super HELD half a second: Home, and nothing more when it is released
		SC="$SC;mods 8;$WW;$WW;$WW;$WW;expect home 1;mods 0;$W;expect home 1;expect shown1 0"
		run pocket_pocketshell pt-shell-home-$sfx "$SC" $E
		SC="otherpic $w 30 menubar $OUT/pt-bar-pad-$sfx.elsm 0 0 0x35 0 0;$W;$W;$APPS;expect front other;expect shown1 0"
		SW="$SC;mods 4;key 0x09;$W;$W;expect shown1 1;dump $OUT/pt-switcher-$sfx.elsm;key 0x09;mods 0;$W;$W;expect shown1 0;expect front other"
		run pocket_pocketshell pt-shell-switcher-$sfx "$SW" $E
		QS="$SC;$WW;$WW;$WW;mods 8;key n;mods 0;$W;$W;expect shown1 1;key $DOWN;key $RIGHT;$W;dump $OUT/pt-quick-$sfx.elsm;key 0x1b;$W;expect shown1 0"
		run pocket_pocketshell pt-shell-quick-$sfx "$QS" $E SIM_MBOX='1:3:Telegram\0Alice: are you on the train?\0telegram\n1:3:Packages\02 updates are ready\0control pkgman\n'
		for p in pt-home pt-search pt-home-tab pt-switcher pt-quick; do out $p-$sfx; done
	}
	shell 640x480 640
	shell 800x480 800
	shell 640x480 640 fr
	# (the pad and the keys' bindings: InputKit's, the section "input" above -- SD:/etc/pocketui.ini has no [buttons] any more)
	pixel pt-home-640 2 240 0b0c0e "the home's background is the glass' black"
	pixel pt-home-640 20 200 15171b "the card is a flat surface"
fi

[ $FAIL = 0 ] && echo "pocketterm: all passed ($OUT)" || echo "pocketterm: FAILED ($OUT)"
exit $FAIL
