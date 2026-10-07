#!/bin/sh
# run_clock_sim_test.sh -- the scripted checks of the Clock and clockd (user/Apps/clock, user/Apps/clockd -- AutoDev
# round 6; the cases of autodev/rounds/06-clock/03-technical-analysis.md §8.4 and §10.3) in the desktop simulator: each
# program built for the PC against the stand-in kernel (tools/tests/desktop_sim/fakekapi.cpp; the Clock with UIKit,
# FreeType and AudioKit, as shots.sh builds it; their own sources with -Wall -Wextra, no warning allowed), each case a
# run through a script of events with a fresh SIM_WRITES of its own, then asserted on what the program printed (SIM_LOG:
# "clockd: ring 1 12:35", "clock: tab alarms", "clock: next ..."), on what the simulator logged ("sim: exec ...",
# "sim: send clockd type 2 ..."), on the files written (alarms.txt, config.ini) and on the window's dumps (made PNGs in
# $OUT, looked at by hand).
#
# The simulator's clock: SIM_CLOCK=YYYYMMDDHHMMSS advances with the ticks (a clockd step, msleep (500), is 0.51 s; a
# Clock step, msleep (16), 20 ms); unset, the date is frozen at Monday 2026-09-28 12:34:00 (the card's system.ini:
# timezone=120, no zone= -- Brussels guessed). Services: SIM_SERVICES lists the ones that run (clock, clockd,
# notify...): a message to one is logged. A program started with lx_launch needs its SD:apps/<name>.app/main: the
# cases put a placeholder in their writes (the card has none until `make stage`).
#
#   sh tools/tests/run_clock_sim_test.sh          -> "clock-sim: all N checks passed" (exit 0)
#   sh tools/tests/run_clock_sim_test.sh build    -> the programs built only ($OUT/clockd, $OUT/clock)
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
#
set -e
cd "$(dirname "$0")/../.."
D=tools/tests/desktop_sim
OUT=${TMPDIR:-/tmp}/onyx_clock_sim
CXX="g++ -std=gnu++17 -O1 -w -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
FT=third_party/freetype-2.14.3
rm -rf "$OUT/w" "$OUT/log"; mkdir -p "$OUT/obj" "$OUT/ft" "$OUT/ak" "$OUT/w" "$OUT/log" "$OUT/fix"

# ---- the building ------------------------------------------------------------------------------------------------
$CXX -c $D/fakekapi.cpp -o "$OUT/fakekapi.o"
# (the programs' own sources without -w: no warning allowed)
WFLAGS="-std=gnu++17 -O1 -Wall -Wextra -Wno-format-truncation -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I kernel/include -fno-exceptions -fno-rtti -Iuser/Kits/fontkit -I$FT/include"
: > "$OUT/clock.warn"
wbuild () {	# wbuild SRC OBJ
	g++ $WFLAGS -c "$1" -o "$2" 2>> "$OUT/clock.warn" || { cat "$OUT/clock.warn"; echo "clock-sim: FAIL the build of $1"; exit 1; }
}
wbuild user/Apps/clock/alarms.cpp "$OUT/alarms.o"
wbuild user/Apps/clock/clocktime.cpp "$OUT/clocktime.o"
wbuild user/Apps/clockd/main.cpp "$OUT/clockd_main.o"
g++ -o "$OUT/clockd" "$OUT/fakekapi.o" "$OUT/clockd_main.o" "$OUT/alarms.o" "$OUT/clocktime.o" -lpthread
if [ -f user/Apps/clock/main.cpp ]; then
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
	if [ ! -f "$OUT/libaudiokit.a" ]; then			# (the alarms' sounds: AudioKit's FM voices, as shots.sh)
		I="-Iuser -Iuser/Kits -Iuser/Runtime -Iuser/Include -Iuser/Libs -Iuser/Emulators -Iuser/Ports -Ithird_party"
		gcc -O2 -w $I -c user/Apps/media/codecs.c -o "$OUT/ak/codecs.o"
		gcc -O2 -w $I -c user/Apps/media/vorbis.c -o "$OUT/ak/vorbis.o"
		for f in user/Apps/koton/synth/*.cpp user/Kits/audiokit/*.cpp; do
			$CXX -Iuser/Apps/koton -Iuser/Apps/media -Ithird_party -c "$f" -o "$OUT/ak/$(basename "$f" .cpp).o" &
		done; wait
		ar rcs "$OUT/libaudiokit.a" "$OUT"/ak/*.o
	fi
	wbuild user/Apps/clock/main.cpp "$OUT/clock_main.o"
	g++ -o "$OUT/clock" "$OUT/fakekapi.o" "$OUT/clock_main.o" "$OUT/alarms.o" "$OUT/clocktime.o" "$OUT/libuikit.a" "$OUT/libft.a" "$OUT/libaudiokit.a" -lpthread -lm
fi
if grep -q "Apps/clock.*warning" "$OUT/clock.warn"; then cat "$OUT/clock.warn"; echo "clock-sim: FAIL warnings in the Clock"; exit 1; fi
[ "$1" = build ] && { echo "clock-sim: built $OUT/clockd $OUT/clock"; exit 0; }

# ---- the running -------------------------------------------------------------------------------------------------
PASS=0; FAILS=0
ok ()   { PASS=$((PASS + 1)); echo "ok   $*"; }
bad ()  { FAILS=$((FAILS + 1)); echo "FAIL $*"; }
check () { name=$1; shift; if "$@"; then ok "$name"; else bad "$name"; fi; }
AL=apps/clock.app/alarms.txt
CF=apps/clock.app/config.ini
# seed CASE [fr]: a fresh writes folder (French: the system's language), the placeholders of the programs started
seed () {
	rm -rf "$OUT/w/$1"; mkdir -p "$OUT/w/$1/apps/clock.app" "$OUT/w/$1/apps/clockd.app" "$OUT/w/$1/apps/control.app"
	echo placeholder > "$OUT/w/$1/apps/clock.app/main"; echo placeholder > "$OUT/w/$1/apps/clockd.app/main"
	echo placeholder > "$OUT/w/$1/apps/control.app/main"
	if [ "$2" = fr ]; then mkdir -p "$OUT/w/$1/etc"; { grep -v '^language' sdcard/etc/system.ini; echo "language=fr"; } > "$OUT/w/$1/etc/system.ini"; fi
	return 0
}
# alarms CASE TEXT: the case's alarms.txt (printf's escapes); config CASE TEXT: its config.ini
alarms () { printf "$2" > "$OUT/w/$1/$AL"; cp "$OUT/w/$1/$AL" "$OUT/fix/$1.alarms"; }
config () { printf "$2" > "$OUT/w/$1/$CF"; }
# run PROGRAM CASE "SCRIPT" [VAR=value ...]: the program through the script (in the case's writes), its log in $OUT/log/CASE.log
run () {
	p=$1; c=$2; s=$3; shift 3
	: > "$OUT/log/$c.log"
	env -u SIM_CLOCK -u SIM_TZ -u SIM_STAT -u SIM_SERVICES -u SIM_MBOX -u SIM_ARGS -u SIM_SOUND \
		SIM_WRITES="$OUT/w/$c" SIM_POS=60,30 SIM_LOG="$OUT/log/$c.log" "$@" SIM="$s" "$OUT/$p" >> "$OUT/log/$c.log" 2>&1 \
		|| bad "$c: $p ended with an error ($OUT/log/$c.log)"
	return 0
}
png () { [ -f "$OUT/$1.elsm" ] && python3 $D/shot.py "$OUT/$1.elsm" "$OUT/$1.png" > /dev/null || true; }
logs () { grep -qF -- "$2" "$OUT/log/$1.log" || { echo "  (no '$2' in $OUT/log/$1.log)"; return 1; }; }
nolog () { ! grep -qF -- "$2" "$OUT/log/$1.log" || { echo "  ('$2' in $OUT/log/$1.log)"; return 1; }; }
count () { grep -cF -- "$2" "$OUT/log/$1.log" || true; }
counts () { n=$(count "$1" "$2"); [ "$n" = "$3" ] || { echo "  ($n x '$2' in $OUT/log/$1.log, not $3)"; return 1; }; }
waits () { printf 'wait;%.0s' $(seq 1 "$1"); }
# the line of the log that holds TEXT: its number (the order of two lines)
lineof () { grep -nF -- "$2" "$OUT/log/$1.log" | head -1 | cut -d: -f1; }
# kv CASE FILE BLOCK# KEY: a key of the n-th [block] of a written file
kv () { awk -v n="$3" -v k="$4" '/^\[/ { b++ } b == n && $1 == k { sub (/^[^=]*= ?/, ""); print; exit }' "$OUT/w/$1/$2" 2>/dev/null; }
ONE='[alarm]\nid = 1\ntime = 12:35\nlabel = Tea\non = 1\ndays =\ndate = 20260928\nsound = chimes\nsnooze =\n'

# ==== clockd (step 4) ==============================================================================================
# clockd-ring (AC-24): an alarm one minute ahead, no Clock running -> "clock --ring 1" started once, on its minute
seed ring; alarms ring "$ONE"
run clockd ring "waitlog 400 clockd: ring;$(waits 200)exit" SIM_CLOCK=20260928123400 SIM_SERVICES=notify SIM_ARGS="--grace 0"
check "clockd-ring: clock --ring 1 started once" counts ring "sim: exec SD:apps/clock.app/main --ring 1" 1
check "clockd-ring: logged with its minute" logs ring "clockd: ring 1 12:35"
check "clockd-ring: no notification of its own (the Clock rings)" nolog ring "sim: send notify"
# the moment it rang: 12:35:00 .. 12:35:02 (02 #16: within 2 s of its minute)
check "clockd-ring: rung within 2 s of 12:35:00" grep -qE "clockd: ring 1 12:35 at 12:35:0[0-2]$" "$OUT/log/ring.log"
check "clockd-ring: started after the reading at start (the order of the log)" test "$(lineof ring 'clockd: reload (start)')" -lt "$(lineof ring 'sim: exec SD:apps/clock.app/main --ring 1')"
# clockd-running (AC-24): the Clock runs -> told by CLOCK_MSG_OPEN, nothing started
seed running; alarms running "$ONE"
run clockd running "waitlog 400 clockd: ring;$(waits 20)exit" SIM_CLOCK=20260928123400 SIM_SERVICES=notify,clock SIM_ARGS="--grace 0"
check "clockd-running: the running Clock told (--ring 1)" counts running 'sim: send clock type 1 "--ring 1\0"' 1
check "clockd-running: nothing started" nolog running "sim: exec"
# clockd-reload (AC-15, 17): the file changed mid-run (12:36 added) and CLOCKD_MSG_RELOAD -> 12:35 once, 12:36 once
seed reload; alarms reload "$ONE"
printf "$ONE"'\n[alarm]\nid = 2\ntime = 12:36\nlabel = Oven\non = 1\ndays =\ndate = 20260928\nsound = beeps\nsnooze =\n' > "$OUT/fix/reload2.txt"
run clockd reload "wait;copy $OUT/fix/reload2.txt SD:/apps/clock.app/alarms.txt;waitlog 400 clockd: ring 2;$(waits 200)exit" \
	SIM_CLOCK=20260928123400 SIM_SERVICES=notify "SIM_MBOX=@2000:2:9:" SIM_ARGS="--grace 0"
check "clockd-reload: the message read (RELOAD)" logs reload "clockd: reload (message): 2 alarms"
check "clockd-reload: 12:35 rung once" counts reload "sim: exec SD:apps/clock.app/main --ring 1" 1
check "clockd-reload: 12:36 (added) rung once" counts reload "sim: exec SD:apps/clock.app/main --ring 2" 1
# the same without the message: the file looked at every 30 s
seed changed; alarms changed "$ONE"
run clockd changed "wait;copy $OUT/fix/reload2.txt SD:/apps/clock.app/alarms.txt;waitlog 400 clockd: ring 2;$(waits 20)exit" \
	SIM_CLOCK=20260928123400 SIM_SERVICES=notify SIM_ARGS="--grace 0"
check "clockd-changed: a hand-edited file seen within 30 s (no message)" logs changed "clockd: reload (changed): 2 alarms"
check "clockd-changed: 12:36 rung once" counts changed "sim: exec SD:apps/clock.app/main --ring 2" 1
# clockd-disabled (AC-19, 22): on = 0 -> nothing over 6000 steps (51 minutes)
seed disabled; alarms disabled "$(echo "$ONE" | sed 's/on = 1/on = 0/')"
run clockd disabled "$(waits 6000)exit" SIM_CLOCK=20260928123400 SIM_SERVICES=notify SIM_ARGS="--grace 0"
check "clockd-disabled: an alarm off never rings" nolog disabled "clockd: ring"
check "clockd-disabled: ... nothing started" nolog disabled "sim: exec"
# clockd-late (AC-22, 31): started at 09:00 with a 07:00 alarm of that day -> nothing
seed late; alarms late "$(echo "$ONE" | sed 's/12:35/07:00/')"
run clockd late "$(waits 300)exit" SIM_CLOCK=20260928090000 SIM_SERVICES=notify SIM_ARGS="--grace 0"
check "clockd-late: a minute past at the start is not rung late" nolog late "clockd: ring"
# the boot guard: without --grace 0 the first 90 s ring nothing (the ticks start at 1000: 8000 more, ~157 steps)
seed guard; alarms guard "$ONE"
run clockd guard "$(waits 200)exit" SIM_CLOCK=20260928123450 SIM_SERVICES=notify
check "clockd-guard: an alarm within the 90 s after the boot is not rung" nolog guard "clockd: ring"
# clockd-sync (R-6): zone=Brussels, 2026-10-25 02:59:30 wall (UTC+2) -> one minute later the clock set to UTC+1
seed sync; mkdir -p "$OUT/w/sync/etc"; printf 'timezone=120\nzone=Brussels\n' > "$OUT/w/sync/etc/system.ini"
run clockd sync "$(waits 300)exit" SIM_CLOCK=20261025025930 SIM_TZ=120 SIM_SERVICES=notify SIM_ARGS="--grace 0"
check "clockd-sync: the summer time ends: set_timezone 60, once" counts sync "sim: set_timezone 60" 1
check "clockd-sync: ... and only that" counts sync "sim: set_timezone" 1
check "clockd-sync: timezone=60 written" grep -q '^timezone=60$' "$OUT/w/sync/etc/system.ini"
# clockd-sync-nozone (gap 1): the card's own system.ini (timezone=120, no zone=) -> nothing set, nothing written
seed nozone
run clockd nozone "$(waits 200)exit" SIM_CLOCK=20261025023000 SIM_TZ=120 SIM_SERVICES=notify SIM_ARGS="--grace 0"
check "clockd-sync-nozone: no zone= -- never guessed: no set_timezone" nolog nozone "sim: set_timezone"
check "clockd-sync-nozone: system.ini not written" test ! -e "$OUT/w/nozone/etc/system.ini"
# clockd-timer (AC-33, 35; validation 2 gap 1): a [timer] 300 ticks ahead -> "--ring timer" exactly once; a RELOAD
# after the ring (the file unchanged) rings it not again
TMR='[timer]\nend = 1300\nset = 5\nlabel =\n'
seed timer; alarms timer "$TMR"
run clockd timer "$(waits 200)exit" SIM_SERVICES=notify SIM_ARGS="--grace 0"
check "clockd-timer: --ring timer started once (not every half second)" counts timer "sim: exec SD:apps/clock.app/main --ring timer" 1
seed timer2; alarms timer2 "$TMR"
run clockd timer2 "$(waits 200)exit" SIM_SERVICES=notify SIM_ARGS="--grace 0" "SIM_MBOX=@3000:2:0:"
check "clockd-timer: reloaded after the ring" logs timer2 "clockd: reload (message)"
check "clockd-timer: ... still rung once" counts timer2 "sim: exec SD:apps/clock.app/main --ring timer" 1
# clockd-fallback: the Clock can be neither told nor started (no clock.app/main) -> clockd's own word-free bubble
seed fallback; rm "$OUT/w/fallback/apps/clock.app/main"; alarms fallback "$ONE"
run clockd fallback "waitlog 400 clockd: ring;$(waits 5)exit" SIM_CLOCK=20260928123400 SIM_SERVICES=notify SIM_ARGS="--grace 0"
check "clockd-fallback: its own notification, the time and the label" logs fallback 'sim: send notify type 1 "12:35\0Tea\0clock alarms\0"'
# clockd-one: a second clockd (the service taken) quits at once -- the simulator registers any name: checked on the Pi
# clockd-quit: CLOCKD_MSG_QUIT ends it
seed quit; alarms quit "$ONE"
run clockd quit "$(waits 50)exit" SIM_SERVICES=notify "SIM_MBOX=@1200:3:9:" SIM_ARGS="--grace 0"
check "clockd-quit: CLOCKD_MSG_QUIT ends the service" logs quit "clockd: quit"

# ==== the Clock's skeleton (step 5) =================================================================================
# clock-args (AC-41): "clock stopwatch" opens on the Stopwatch tab; the tab kept in config.ini at the end
seed args; run clock args "wait;wait;dump $OUT/args.elsm;quit;wait" SIM_SERVICES=notify,clockd SIM_ARGS=stopwatch; png args
check "clock-args: clock stopwatch -> the Stopwatch tab" logs args "clock: tab stopwatch"
check "clock-args: the window titled Clock" logs args "sim: window Clock"
check "clock-args: the tab kept (config.ini tab = stopwatch)" test "$(kv args $CF 1 tab)" = stopwatch
seed lasttab; config lasttab '[clock]\ntab = alarms\n'
run clock lasttab "wait;wait;exit" SIM_SERVICES=notify,clockd
check "clock-args: clock alone -> the tab of last time" logs lasttab "clock: tab alarms"
# clock-one (AC-26): a Clock runs -> its arguments sent to it, it is raised, no window
seed one; run clock one "wait;wait;exit" SIM_SERVICES=clock SIM_ARGS=alarms
check "clock-one: the arguments sent to the running Clock" logs one 'sim: send clock type 1 "alarms\0"'
check "clock-one: the running Clock raised" logs one "sim: raise_app clock"
check "clock-one: no window of its own" nolog one "sim: window"
# clock-clockd (AC-27): no clockd service -> clockd started (lx_launch, no arguments: kapi_launch)
seed clockd; run clock clockd "wait;wait;exit" SIM_SERVICES=notify
check "clock-clockd: clockd started" logs clockd "sim: launch clockd"
seed clockdon; run clock clockdon "wait;wait;exit" SIM_SERVICES=notify,clockd
check "clock-clockd: clockd running -> not started again" nolog clockdon "sim: launch clockd"
# clock-keys (AC-41, the tabs): Ctrl+1..4, Ctrl+Tab, a second Clock's message (CLOCK_MSG_OPEN "timer")
seed keys; run clock keys "wait;mods 1;key 50;mods 0;wait;mods 1;key 51;mods 0;wait;mods 1;key 52;mods 0;wait;mods 1;key 49;mods 0;wait;mods 1;key 0x09;mods 0;wait;waitlog 50 clock: tab timer;wait;exit" \
	SIM_SERVICES=notify,clockd SIM_ARGS=world "SIM_MBOX=@40:1:9:timer"
check "clock-keys: Ctrl+2 -> Alarms" logs keys "clock: tab alarms"
check "clock-keys: Ctrl+3 -> Timer, Ctrl+4 -> Stopwatch" sh -c "[ \$(grep -c 'clock: tab timer' '$OUT/log/keys.log') -ge 1 ] && grep -q 'clock: tab stopwatch' '$OUT/log/keys.log'"
check "clock-keys: Ctrl+1 -> World, Ctrl+Tab -> the next (Alarms)" test "$(grep 'clock: tab' "$OUT/log/keys.log" | sed -n 5,6p | tr '\n' ' ')" = "clock: tab world clock: tab alarms "
check "clock-keys: CLOCK_MSG_OPEN timer -> Timer, raised" sh -c "[ \$(grep -c 'clock: tab timer' '$OUT/log/keys.log') = 2 ] && grep -q 'sim: raise_app clock' '$OUT/log/keys.log'"
# the words: every one in French (tools/lang/check.py clock: 0 missing)
check "clock-lang: check.py clock -> 0 missing" sh -c "python3 tools/lang/check.py clock | tail -1 | grep -q ' 0 missing'"

# ==== the World tab (step 6) ======================================================================================
CITIES='[clock]\ncities = Tokyo,New York,London\n'
# clock-world (AC-6, 7): Brussels (the card's timezone=120), the simulator's Monday 12:34:00 -> the rows
seed world; config world "$CITIES"
run clock world "wait;wait;dump $OUT/world.elsm;exit" SIM_SERVICES=notify,clockd SIM_ARGS=world; png world
check "clock-world: here 12:34:00 UTC+2, summer time, Brussels" logs world "clock: here 12:34:00 UTC+2 summer (Brussels)"
check "clock-world: Tokyo 19:34 +7 h" logs world "clock: city Tokyo 19:34 +7 h"
check "clock-world: New York 06:34 -6 h" logs world "clock: city New York 06:34 -6 h"
check "clock-world: London 11:34 -1 h" logs world "clock: city London 11:34 -1 h"
# AC-8 on the window (unit-tested in clocktime_test): at 23:30 Brussels Tokyo is tomorrow; at 01:00 Los Angeles yesterday
seed late; config late '[clock]\ncities = Tokyo,Los Angeles\n'
run clock late "wait;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=world SIM_CLOCK=20260928233000 SIM_TZ=120
check "clock-world: 23:30 -> Tokyo 06:30 tomorrow" logs late "clock: city Tokyo 06:30 +7 h tomorrow"
seed early; config early '[clock]\ncities = Tokyo,Los Angeles\n'
run clock early "wait;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=world SIM_CLOCK=20260929010000 SIM_TZ=120
check "clock-world: 01:00 -> Los Angeles 16:00 yesterday" logs early "clock: city Los Angeles 16:00 -9 h yesterday"
# the seconds tick, the minute's rows printed again (SIM_CLOCK advances: 3000 steps = 60 s)
seed tick; config tick "$CITIES"
run clock tick "wait;$(waits 3010)exit" SIM_SERVICES=notify,clockd SIM_ARGS=world SIM_CLOCK=20260928123400 SIM_TZ=120
check "clock-world: a minute later, the rows again (Tokyo 19:35)" logs tick "clock: city Tokyo 19:35 +7 h"
# clock-cities (AC-9): Ctrl+N + Enter adds the first of the picker (Amsterdam, the names sorted) at the end; Ctrl+Up
# moves it up; Delete removes the selection; restarted, the same list; the picker never lists a city chosen
seed cities; config cities "$CITIES"
run clock cities "wait;mods 1;key 14;mods 0;wait;dump $OUT/cities.elsm;key 13;wait;mods 1;key 0x100;mods 0;wait;key 0x108;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=world; png cities
check "clock-cities: the picker lists the 20 zones not chosen" logs cities "clock: add a city (20 zones)"
check "clock-cities: Enter adds Amsterdam at the end" logs cities "clock: cities Tokyo,New York,London,Amsterdam"
check "clock-cities: Ctrl+Up moves it up" logs cities "clock: cities Tokyo,New York,Amsterdam,London"
check "clock-cities: Delete removes it" sh -c "[ \$(grep -c 'clock: cities Tokyo,New York,London\$' '$OUT/log/cities.log') = 1 ]"
check "clock-cities: config.ini cities = in the order shown" test "$(kv cities $CF 1 cities)" = "Tokyo,New York,London"
run clock cities "wait;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=world
check "clock-cities: restarted, the same list" sh -c "grep -q 'clock: city Tokyo' '$OUT/log/cities.log' && grep -q 'clock: city London' '$OUT/log/cities.log' && ! grep -q 'Amsterdam' '$OUT/log/cities.log'"
# the filter: "lon" keeps London's neighbours only
seed filter; config filter '[clock]\ncities = Tokyo\n'
run clock filter "wait;mods 1;key 14;mods 0;key d;key u;key b;wait;key 13;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=world
check "clock-cities: typing filters (dub + Enter -> Dublin)" logs filter "clock: cities Tokyo,Dublin"
# the 13th refused
seed full; config full '[clock]\ncities = Brussels,Paris,Amsterdam,Luxembourg,Berlin,Zurich,Vienna,Rome,Madrid,Stockholm,Warsaw,London\n'
run clock full "wait;mods 1;key 14;mods 0;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=world
check "clock-cities: a 13th city refused" logs full "clock: city refused (12 already)"
check "clock-cities: ... no picker" nolog full "clock: add a city"
# clock-nozone (AC-10): no zone= and no timezone= -> Time zone not set; its button runs control langconf
seed nozone; config nozone "$CITIES"; mkdir -p "$OUT/w/nozone/etc"; printf 'language=en\n' > "$OUT/w/nozone/etc/system.ini"
run clock nozone "wait;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=world
xy=$(sed -n 's/.*Language & Region at \([0-9]*\),\([0-9]*\).*/\1 \2/p' "$OUT/log/nozone.log" | head -1)
check "clock-nozone: Time zone not set" logs nozone "time zone not set"
run clock nozone "wait;wait;down $xy;up $xy;wait;dump $OUT/nozone.elsm;exit" SIM_SERVICES=notify,clockd SIM_ARGS=world; png nozone
check "clock-nozone: its button runs control langconf" logs nozone "sim: exec SD:apps/control.app/main langconf"
check "clock-nozone: the cities with their UTC offset only" logs nozone "clock: city Tokyo 21:34 "

# ==== the Alarms tab and the editor (step 7) =======================================================================
FX=$D/clock/alarms.txt					# (07:00 School weekdays on, 14:30 Medicine once today on, 09:00 Gym weekends off)
fixture () { cp $FX "$OUT/w/$1/$AL"; cp $FX "$OUT/fix/$1.alarms"; }
# the editor's controls (the card centred in 560 x 440; client coordinates): Weekdays, OK
WEEKDAYS="293 253"
# clock-alarm-new (AC-11, 15): Ctrl+N, Tab Tab (the label), School, Weekdays, Enter -> one [alarm] block, clockd told
seed new; run clock new "wait;mods 1;key 14;mods 0;wait;key 0x09;key 0x09;key S;key c;key h;key o;key o;key l;down $WEEKDAYS;up $WEEKDAYS;wait;dump $OUT/new.elsm;key 13;wait;wait;exit" \
	SIM_SERVICES=notify,clockd SIM_ARGS=alarms; png new
check "clock-alarm-new: the editor (New Alarm)" logs new "clock: editor new"
check "clock-alarm-new: one [alarm] block" test "$(grep -c '^\[alarm\]' "$OUT/w/new/$AL")" = 1
check "clock-alarm-new: time = 07:00, label = School, on = 1" sh -c "[ '$(kv new $AL 1 time)' = 07:00 ] && [ '$(kv new $AL 1 label)' = School ] && [ '$(kv new $AL 1 on)' = 1 ]"
check "clock-alarm-new: days = mon tue wed thu fri, sound = chimes" sh -c "[ '$(kv new $AL 1 days)' = 'mon tue wed thu fri' ] && [ '$(kv new $AL 1 sound)' = chimes ]"
check "clock-alarm-new: clockd told (RELOAD)" logs new "sim: send clockd type 2"
check "clock-alarm-new: shown: 07:00 School, Weekdays" logs new "clock: alarm 1 07:00 School [Weekdays] on"
seed newfr fr; run clock newfr "wait;mods 1;key 14;mods 0;wait;key 0x09;key 0x09;key S;key c;key h;key o;key o;key l;down $WEEKDAYS;up $WEEKDAYS;wait;key 13;wait;wait;exit" \
	SIM_SERVICES=notify,clockd SIM_ARGS=alarms
check "clock-alarm-new (fr): the same tokens written (days = mon tue wed thu fri, sound = chimes)" sh -c "[ '$(kv newfr $AL 1 days)' = 'mon tue wed thu fri' ] && [ '$(kv newfr $AL 1 sound)' = chimes ]"
check "clock-alarm-new (fr): shown in French (En semaine)" logs newfr "clock: alarm 1 07:00 School [En semaine] on"
# AC-13: a once alarm made at 12:34 for 10:00 -> tomorrow; for 13:00 -> today (the hours typed, Enter: committed, OK)
seed once10; run clock once10 "wait;mods 1;key 14;mods 0;wait;key 1;key 0;key 13;wait;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=alarms
check "clock-once: 10:00 made at 12:34 -> date = 20260929" sh -c "[ '$(kv once10 $AL 1 time)' = 10:00 ] && [ '$(kv once10 $AL 1 date)' = 20260929 ]"
seed once13; run clock once13 "wait;mods 1;key 14;mods 0;wait;key 1;key 3;key 13;wait;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=alarms
check "clock-once: 13:00 made at 12:34 -> date = 20260928" sh -c "[ '$(kv once13 $AL 1 time)' = 13:00 ] && [ '$(kv once13 $AL 1 date)' = 20260928 ]"
# clock-next (AC-12): the fixtures -> today 14:30 in 1 h 56; School alone -> tomorrow 07:00 in 18 h 26; all off -> none
seed next3; fixture next3; run clock next3 "wait;wait;dump $OUT/alarms.elsm;exit" SIM_SERVICES=notify,clockd SIM_ARGS=alarms; png alarms
check "clock-next: Next alarm: today 14:30 -- in 1 h 56 min" logs next3 "clock: next today 14:30 in 1 h 56 min"
check "clock-next: the rows sorted by time (07:00, 09:00, 14:30)" test "$(grep 'clock: alarm [0-9]' "$OUT/log/next3.log" | head -3 | cut -d' ' -f4 | tr '\n' ' ')" = "07:00 09:00 14:30 "
check "clock-next: Gym off, Medicine once (AC-14's words)" sh -c "grep -q 'clock: alarm 3 09:00 Gym \[Weekends\] off' '$OUT/log/next3.log' && grep -q 'clock: alarm 2 14:30 Medicine \[Once\] on' '$OUT/log/next3.log'"
seed next1; alarms next1 '[alarm]\nid = 1\ntime = 07:00\nlabel = School\non = 1\ndays = mon tue wed thu fri\n'
run clock next1 "wait;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=alarms
check "clock-next: School alone -> tomorrow 07:00 in 18 h 26 min" logs next1 "clock: next tomorrow 07:00 in 18 h 26 min"
seed next0; fixture next0; run clock next0 "wait;key 32;wait;key 0x101;key 0x101;key 32;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=alarms
check "clock-next: Space turns the selection off (School, then Medicine) -> No alarm set" logs next0 "clock: next none"
check "clock-next: ... written on = 0 (block 1 and 2)" sh -c "[ '$(kv next0 $AL 1 on)' = 0 ] && [ '$(kv next0 $AL 2 on)' = 0 ]"
check "clock-next: ... clockd told each time" test "$(count next0 'sim: send clockd type 2')" = 2
# AC-14: the repeat words; a custom set (Mon, Wed, Fri), every day -- in English and French
seed rep; alarms rep '[alarm]\nid = 1\ntime = 06:00\non = 1\ndays = mon wed fri\n[alarm]\nid = 2\ntime = 06:30\non = 1\ndays = mon tue wed thu fri sat sun\n'
run clock rep "wait;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=alarms
check "clock-repeat: Mon, Wed, Fri; Every day; no label -> Alarm" sh -c "grep -q 'clock: alarm 1 06:00 Alarm \[Mon, Wed, Fri\] on' '$OUT/log/rep.log' && grep -q 'clock: alarm 2 06:30 Alarm \[Every day\] on' '$OUT/log/rep.log'"
seed repfr fr; cp "$OUT/w/rep/$AL" "$OUT/w/repfr/$AL"
run clock repfr "wait;wait;dump $OUT/alarms-fr.elsm;exit" SIM_SERVICES=notify,clockd SIM_ARGS=alarms; png alarms-fr
check "clock-repeat (fr): lun., mer., ven.; Tous les jours; Alarme" sh -c "grep -q 'clock: alarm 1 06:00 Alarme \[lun., mer., ven.\] on' '$OUT/log/repfr.log' && grep -q 'Tous les jours' '$OUT/log/repfr.log'"
# Delete: the selection deleted, written, clockd told
seed del; fixture del; run clock del "wait;key 0x108;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=alarms
check "clock-delete: Delete deletes the selected alarm (School)" sh -c "! grep -q School '$OUT/w/del/$AL' && grep -q Medicine '$OUT/w/del/$AL'"
check "clock-delete: clockd told" logs del "sim: send clockd type 2"
# clock-21 (AC-15): 20 alarms -> Ctrl+N refused
seed a21; : > "$OUT/w/a21/$AL"; for i in $(seq 1 20); do printf '[alarm]\nid = %d\ntime = 06:%02d\non = 1\ndays = sun\n' $i $i >> "$OUT/w/a21/$AL"; done
run clock a21 "wait;mods 1;key 14;mods 0;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=alarms
check "clock-21: a 21st alarm refused" logs a21 "clock: alarm refused (20 already)"
check "clock-21: ... no editor" nolog a21 "clock: editor new"
# clock-invalid (AC-16): time = 25:99 shown Invalid; an unknown key kept when the app saves (a toggle)
seed inv; alarms inv '[alarm]\nid = 1\ntime = 07:00\nlabel = School\non = 1\ndays = mon\ncolour = red\n[alarm]\nid = 9\ntime = 25:99\nlabel = Broken\non = 1\n'
run clock inv "wait;key 32;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=alarms
check "clock-invalid: 25:99 shown Invalid" logs inv "clock: alarm 9 invalid"
check "clock-invalid: the unknown key kept after a save (colour = red)" test "$(kv inv $AL 1 colour)" = red
check "clock-invalid: ... the invalid time written back as it was" test "$(kv inv $AL 2 time)" = 25:99
check "clock-invalid: ... and the toggle written (on = 0)" test "$(kv inv $AL 1 on)" = 0
# clock-edit-keys (G9, AC-41): (a) the editor on School, the focus on Hours, 8 typed, Enter -> time = 08:00 saved;
# (b) 8 typed, Esc -> nothing written
seed ekeys; fixture ekeys; run clock ekeys "wait;key 13;wait;key 56;key 13;wait;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=alarms
check "clock-edit-keys (a): Enter over Hours: the 8 committed, then OK (time = 08:00)" sh -c "[ '$(kv ekeys $AL 1 time)' = 08:00 ] && [ '$(kv ekeys $AL 1 label)' = School ]"
seed ekeys2; fixture ekeys2; run clock ekeys2 "wait;key 13;wait;key 56;key 27;wait;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=alarms
check "clock-edit-keys (b): Esc over Hours: the editor closed, alarms.txt unchanged" sh -c "grep -q 'clock: editor cancelled' '$OUT/log/ekeys2.log' && cmp -s '$OUT/w/ekeys2/$AL' '$OUT/fix/ekeys2.alarms'"
# the editor's Delete (the double click: the editor; its Delete button)
seed edel; fixture edel; run clock edel "wait;key 13;wait;down 120 364;up 120 364;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=alarms
check "clock-edit: the editor's Delete deletes School" sh -c "grep -q 'clock: alarm 1 deleted' '$OUT/log/edel.log' && ! grep -q School '$OUT/w/edel/$AL'"
# alarms.txt cannot be written: the change kept, Not saved in the footer (logged)
seed rofs; fixture rofs; run clock rofs "wait;key 32;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=alarms SIM_ROFS=SD:/apps/clock.app
check "clock-rofs: a read-only card -> not saved, said" logs rofs "clock: alarms.txt not saved"

echo
if [ $FAILS -ne 0 ]; then echo "clock-sim: $FAILS of $((PASS + FAILS)) checks FAILED"; exit 1; fi
echo "clock-sim: all $PASS checks passed"
