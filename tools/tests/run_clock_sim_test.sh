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

# ==== an alarm ringing (step 8) ===================================================================================
# clock-ring (AC-24, 25): clock --ring 1 -> the notification first, the ring card, raised; Esc = Stop
seed ring1; fixture ring1; run clock ring1 "wait;wait;dump $OUT/ring.elsm;key 27;wait;wait;exit" SIM_SERVICES=notify,clockd "SIM_ARGS=--ring 1"; png ring
check "clock-ring: the notification Clock -- 07:00 School, its click: clock alarms" logs ring1 'sim: send notify type 1 "Clock\007:00 School\0clock alarms\0"'
check "clock-ring: rung, the window raised, on the Alarms tab" sh -c "grep -q 'clock: ring 1 07:00 School' '$OUT/log/ring1.log' && grep -q 'sim: raise_app clock' '$OUT/log/ring1.log' && grep -q 'clock: tab alarms' '$OUT/log/ring1.log'"
check "clock-ring: Esc = Stop" logs ring1 "clock: stop 1"
check "clock-ring: ... a weekly alarm stays on, clockd told" sh -c "[ '$(kv ring1 $AL 1 on)' = 1 ] && grep -q 'sim: send clockd type 2' '$OUT/log/ring1.log'"
check "clock-ring-only: started only to ring, it closes after the answer" logs ring1 "clock: ring-only, closing"
check "clock-ring-only: ... before the script's end" nolog ring1 "sim: end of the script"
seed ring2; fixture ring2; run clock ring2 "wait;wait;key 27;wait;wait;exit" SIM_SERVICES=notify,clockd "SIM_ARGS=--ring 2"
check "clock-ring: Stop on a once alarm -> written on = 0 (Medicine)" test "$(kv ring2 $AL 2 on)" = 0
check "clock-ring: ... and shown off" logs ring2 "clock: alarm 2 14:30 Medicine [Once] off"
seed ringfr fr; fixture ringfr; run clock ringfr "wait;wait;dump $OUT/ring-fr.elsm;key 27;wait;exit" SIM_SERVICES=notify,clockd "SIM_ARGS=--ring 1"; png ring-fr
check "clock-ring (fr): the notification in French (Horloge)" logs ringfr 'sim: send notify type 1 "Horloge\007:00 School\0clock alarms\0"'
# clock-snooze (AC-18, 21, 25): Enter = Snooze -> snooze = 12:34 + 10 min written; the next-alarm line says so
seed snooze; fixture snooze; run clock snooze "wait;wait;key 13;wait;wait;exit" SIM_SERVICES=notify,clockd "SIM_ARGS=--ring 1"
check "clock-snooze: Enter = Snooze, snooze = 202609281244" sh -c "grep -q 'clock: snooze 1 until 12:44' '$OUT/log/snooze.log' && [ '$(kv snooze $AL 1 snooze)' = 202609281244 ]"
check "clock-snooze: Snoozed until 12:44 (the bar, the row)" sh -c "grep -q 'clock: next snoozed 12:44 (School)' '$OUT/log/snooze.log' && grep -q 'School \[Weekdays\] on snoozed 12:44' '$OUT/log/snooze.log'"
# the snooze rung: cleared
seed snoozed; fixture snoozed; sed -i '0,/^snooze =$/s//snooze = 202609281234/' "$OUT/w/snoozed/$AL"
run clock snoozed "wait;wait;key 27;wait;exit" SIM_SERVICES=notify,clockd "SIM_ARGS=--ring 1"
check "clock-snooze: the snooze's ring answered -> snooze = cleared" test -z "$(kv snoozed $AL 1 snooze)"
# clock-nosound (AC-25, 30, R-4): no sound output -> the card's line, logged; with one: acquired, the chimes looped
seed nosound; fixture nosound; run clock nosound "wait;wait;key 27;wait;exit" SIM_SERVICES=notify,clockd "SIM_ARGS=--ring 1"
check "clock-nosound: no output -> Sound unavailable (none)" logs nosound "clock: sound unavailable none"
check "clock-nosound: ... the ring still comes (the notification, the card)" logs nosound 'sim: send notify type 1 "Clock\007:00 School'
seed sound; fixture sound; run clock sound "wait;wait;wait;wait;key 27;wait;exit" SIM_SERVICES=notify,clockd "SIM_ARGS=--ring 1" SIM_SOUND=1
check "clock-sound: with an output -> acquired, the chimes looped" sh -c "grep -q 'sim: sound acquired' '$OUT/log/sound.log' && grep -q 'clock: sound chimes (looped)' '$OUT/log/sound.log'"
check "clock-sound: ... no unavailable line" nolog sound "sound unavailable"
# the editor's Test: one round, or the amber line when nothing can be heard
TEST="443 297"
seed test; fixture test; run clock test "wait;key 13;wait;down $TEST;up $TEST;wait;dump $OUT/edit-nosound.elsm;key 27;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=alarms; png edit-nosound
check "clock-test: the editor's Test without an output -> unavailable" logs test "clock: sound unavailable none"
seed test2; fixture test2; run clock test2 "wait;key 13;wait;down $TEST;up $TEST;wait;key 27;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=alarms SIM_SOUND=1
check "clock-test: with an output -> one round of the chimes" logs test2 "clock: sound chimes"
check "clock-test: ... not looped" nolog test2 "(looped)"
# clock-missed (AC-20, 29 PC part, R-2): unanswered 2 minutes (6000 steps of 20 ms) -> Missed alarm notified, missed =
seed missed; fixture missed; run clock missed "wait;$(waits 6100)exit" SIM_SERVICES=notify,clockd "SIM_ARGS=--ring 1"
check "clock-missed: Missed alarm: 07:00 School notified" logs missed 'sim: send notify type 1 "Clock\0Missed alarm: 07:00 School\0clock alarms\0"'
check "clock-missed: missed = 202609281234 written, the row says so" sh -c "[ '$(kv missed $AL 1 missed)' = 202609281234 ] && grep -q 'missed 12:34' '$OUT/log/missed.log'"
check "clock-missed: not before 2 minutes (one notification of the ring, one of the miss)" test "$(count missed 'sim: send notify')" = 2
# a ring delivered to the running Clock (CLOCK_MSG_OPEN "--ring 1"): after Stop it stays
seed msg; fixture msg; run clock msg "wait;waitlog 100 clock: ring 1;wait;key 27;wait;$(waits 10)" SIM_SERVICES=notify,clockd SIM_ARGS=world "SIM_MBOX=@40:1:9:--ring 1"
check "clock-ring (message): rung by the running Clock" logs msg "clock: ring 1 07:00 School"
check "clock-ring (message): ... it stays after Stop" sh -c "grep -q 'clock: stop 1' '$OUT/log/msg.log' && ! grep -q 'ring-only' '$OUT/log/msg.log' && grep -q 'sim: end of the script' '$OUT/log/msg.log'"
# R-8: a ring over the open editor -> both cards; Esc stops the ring, the editor is still there (Esc: cancelled)
seed stack; fixture stack; run clock stack "wait;key 13;wait;waitlog 100 clock: ring 1;wait;dump $OUT/stack.elsm;key 27;wait;key 27;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=alarms "SIM_MBOX=@40:1:9:--ring 1"; png stack
check "clock-veil-stack: a ring over the editor, Esc stops it" sh -c "grep -q 'clock: editor edit 1' '$OUT/log/stack.log' && grep -q 'clock: stop 1' '$OUT/log/stack.log'"
check "clock-veil-stack: ... then the editor's Esc cancels it" logs stack "clock: editor cancelled"
# a ring of an alarm deleted meanwhile: nothing (started only for it: closes)
seed gone; fixture gone; run clock gone "wait;wait;exit" SIM_SERVICES=notify,clockd "SIM_ARGS=--ring 7"
check "clock-ring: an unknown alarm -> nothing rung, closed" sh -c "grep -q 'clock: ring 7: no such alarm' '$OUT/log/gone.log' && grep -q 'ring-only, closing' '$OUT/log/gone.log' && ! grep -q 'sim: send notify' '$OUT/log/gone.log'"

# ==== the Timer, Time's up, the timer handed to clockd (step 9) =====================================================
# the Timer's controls (the block centred in 560 x 440; client coordinates): the 5 min and 3 min presets, the minutes
# spin box (its field; its up arrow)
P5="421 193"; P3="370 193"; MINF="402 105"; MINUP="449 97"
# clock-timer (AC-32): the 5 min preset sets 0 / 5 / 0; Start, about a second -> 04:59; the duration locked while it runs
# (the minutes' up arrow does nothing); R refused while it runs; Pause holds; Reset back to 05:00; timer = 300 kept
seed timer; run clock timer "wait;down $P5;up $P5;wait;key 32;$(waits 50)down $MINUP;up $MINUP;key r;$(waits 10)key 32;wait;dump $OUT/timer-paused.elsm;$(waits 30)key r;wait;quit;wait" \
	SIM_SERVICES=notify,clockd SIM_ARGS=timer; png timer-paused
check "clock-timer: the 5 min preset -> 05:00" logs timer "clock: timer set 05:00"
check "clock-timer: Start -> 05:00, the duration locked" logs timer "clock: timer started 05:00 (the duration locked)"
check "clock-timer: R refused while it runs" logs timer "clock: timer reset refused (running)"
check "clock-timer: about a second later, Pause holds 04:59 (the up arrow changed nothing)" logs timer "clock: timer paused 04:59"
check "clock-timer: Reset -> 05:00" logs timer "clock: timer reset 05:00"
check "clock-timer: timer = 300 in config.ini" test "$(kv timer $CF 1 timer)" = 300
# the preset's double click (two clicks within 0.4 s): set and start; a preset refused while it runs
seed tdouble; run clock tdouble "wait;down $P3;up $P3;down $P3;up $P3;wait;down $P5;up $P5;wait;quit;wait" SIM_SERVICES=notify,clockd SIM_ARGS=timer
check "clock-timer: a double click on 3 min sets and starts" sh -c "grep -q 'clock: timer set 03:00' '$OUT/log/tdouble.log' && grep -q 'clock: timer started 03:00' '$OUT/log/tdouble.log'"
check "clock-timer: ... a preset refused while it runs" logs tdouble "clock: preset refused (the timer runs)"
check "clock-timer: ... timer = 180 kept" test "$(kv tdouble $CF 1 timer)" = 180
# the duration typed in the spin boxes: minutes 2, Space (the typed digits taken) -> 02:00
seed ttyped; run clock ttyped "wait;down $MINF;up $MINF;key 2;key 32;wait;quit;wait" SIM_SERVICES=notify,clockd SIM_ARGS=timer
check "clock-timer: 2 typed in the minutes, Space -> started 02:00" logs ttyped "clock: timer started 02:00"
check "clock-timer: ... timer = 120 kept" test "$(kv ttyped $CF 1 timer)" = 120
# clock-timesup (AC-33): a 3-s timer -> Time's up, notified (its click: clock timer), raised; + -> one more minute (01:00)
seed tup; config tup '[clock]\ntimer = 3\n'
run clock tup "wait;key 32;waitlog 300 clock: time's up;wait;dump $OUT/timesup.elsm;key 43;wait;$(waits 5)key 32;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=timer; png timesup
check "clock-timesup: the notification Timer -- 00:03 done (clock timer)" logs tup 'sim: send notify type 1 "Clock\0Timer — 00:03 done\0clock timer\0"'
check "clock-timesup: Time's up, the window raised" sh -c "grep -q \"clock: time's up (00:03\" '$OUT/log/tup.log' && grep -q 'sim: raise_app clock' '$OUT/log/tup.log'"
check "clock-timesup: after it started" test "$(lineof tup 'timer started 00:03')" -lt "$(lineof tup "time's up (")"
check "clock-timesup: + -> one more minute, 01:00" logs tup "clock: timer started 01:00 (one more minute)"
check "clock-timesup: ... it runs (Space pauses it at 01:00)" logs tup "clock: timer paused 01:00"
# Stop (Esc): back to the time set (Space starts 00:03 again); French: Minuteur -- 00:03 terminé
seed tstop fr; config tstop '[clock]\ntimer = 3\n'
run clock tstop "wait;key 32;waitlog 300 clock: time's up;wait;dump $OUT/timesup-fr.elsm;key 27;wait;key 32;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=timer; png timesup-fr
check "clock-timesup: Esc = Stop" logs tstop "clock: time's up stopped"
check "clock-timesup: ... back to the time set (Space: 00:03 again)" test "$(count tstop 'clock: timer started 00:03')" = 2
check "clock-timesup (fr): Minuteur -- 00:03 terminé" logs tstop 'sim: send notify type 1 "Horloge\0Minuteur — 00:03 terminé\0clock timer\0"'
# unanswered two minutes: the card goes by itself
seed tlong; config tlong '[clock]\ntimer = 3\n'
run clock tlong "wait;key 32;waitlog 300 clock: time's up;$(waits 6100)exit" SIM_SERVICES=notify,clockd SIM_ARGS=timer
check "clock-timesup: unanswered 2 minutes -> it stops by itself" logs tlong "clock: time's up unanswered"
# clock-timer-close (AC-35, R-1): the timer running, the window closed (quit; the close box) -> [timer] in alarms.txt
# (its end in ticks, its time set, its end in UTC with a real clock), clockd told; timer = 300 kept
seed tclose; fixture tclose
run clock tclose "wait;key 32;$(waits 20)quit;wait" SIM_SERVICES=notify,clockd SIM_ARGS=timer SIM_CLOCK=20260928123400
check "clock-timer-close: the timer handed to clockd" logs tclose "clock: timer handed to clockd (05:00 left)"
check "clock-timer-close: [timer] end = (ticks) and set = 300 written after the three alarms" sh -c "[ \$(grep -c '^\[alarm\]' '$OUT/w/tclose/$AL') = 3 ] && [ '$(kv tclose $AL 4 set)' = 300 ] && [ -n '$(kv tclose $AL 4 end)' ] && [ '$(kv tclose $AL 4 end)' -gt 30000 ]"
check "clock-timer-close: its end in UTC too (a real clock)" test -n "$(kv tclose $AL 4 utc)"
check "clock-timer-close: clockd told (RELOAD)" logs tclose "sim: send clockd type 2"
check "clock-timer-close: timer = 300 in config.ini" test "$(kv tclose $CF 1 timer)" = 300
# (the close box of the main window is the simulator's "quit": kapi_should_exit, as the cases above)
# a paused timer is kept (config.ini) and comes back paused
seed tpause; run clock tpause "wait;key 32;$(waits 60)key 32;wait;quit;wait" SIM_SERVICES=notify,clockd SIM_ARGS=timer
run clock tpause "wait;wait;key 32;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=timer
check "clock-timer: a paused timer kept at the close, back paused (04:59)" sh -c "grep -q 'clock: timer paused 04:59 (kept)' '$OUT/log/tpause.log' && grep -q 'clock: timer resumed 04:59' '$OUT/log/tpause.log'"
check "clock-timer: ... nothing handed to clockd" test ! -e "$OUT/w/tpause/$AL"
# clock-timer-resume (validation 1 gap 3, AC-40): a [timer] still to come (end = 13000: 120 s ahead) -> taken back,
# running 02:00, the block removed, clockd told, the Timer tab shown (no argument)
seed tres; alarms tres '[timer]\nend = 13000\nset = 300\nlabel =\n'
run clock tres "wait;wait;dump $OUT/timer-resume.elsm;wait;exit" SIM_SERVICES=notify,clockd; png timer-resume
check "clock-timer-resume: taken back, running 02:00" logs tres "clock: timer running 02:00"
check "clock-timer-resume: the [timer] block gone from alarms.txt" sh -c "! grep -q '^\[timer\]' '$OUT/w/tres/$AL'"
check "clock-timer-resume: clockd told (RELOAD)" logs tres "sim: send clockd type 2"
check "clock-timer-resume: the Timer tab shown" logs tres "clock: tab timer"
# ... already past (end = 500): left to clockd -- the file as it was, nothing sent
seed tpast; alarms tpast '[timer]\nend = 500\nset = 300\nlabel =\n'
run clock tpast "wait;wait;exit" SIM_SERVICES=notify,clockd
check "clock-timer-resume: a past [timer] left to clockd (the file unchanged)" cmp -s "$OUT/w/tpast/$AL" "$OUT/fix/tpast.alarms"
check "clock-timer-resume: ... nothing sent" nolog tpast "sim: send clockd"
# a stale [timer] (an earlier boot: end 990 s ahead for a 5-s timer) is dropped at the next save
seed tstale; fixture tstale; printf '\n[timer]\nend = 100000\nset = 5\nlabel =\n' >> "$OUT/w/tstale/$AL"
run clock tstale "wait;key 32;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=alarms
check "clock-timer: a stale [timer] dropped at the next save" sh -c "grep -q 'clock: alarm 1 turned off' '$OUT/log/tstale.log' && ! grep -q '^\[timer\]' '$OUT/w/tstale/$AL'"
# clock-timesup-hand (validation 2 gap 1): clock --ring timer, the [timer] past -> Time's up with its time set, the block
# removed (the three alarms kept), clockd told; started only for it: closes after Stop
seed thand; fixture thand; printf '\n[timer]\nend = 500\nset = 300\nlabel =\n' >> "$OUT/w/thand/$AL"
run clock thand "wait;wait;key 27;wait;wait;exit" SIM_SERVICES=notify,clockd "SIM_ARGS=--ring timer"
check "clock-timesup-hand: Time's up, Timer -- 05:00 done sent" sh -c "grep -q \"clock: time's up (05:00\" '$OUT/log/thand.log' && grep -q 'sim: send notify type 1 \"Clock.0Timer — 05:00 done.0clock timer' '$OUT/log/thand.log'"
check "clock-timesup-hand: no [timer] in alarms.txt, the three alarms kept" sh -c "! grep -q '^\[timer\]' '$OUT/w/thand/$AL' && [ \$(grep -c '^\[alarm\]' '$OUT/w/thand/$AL') = 3 ] && [ '$(kv thand $AL 1 label)' = School ]"
check "clock-timesup-hand: clockd told (RELOAD)" logs thand "sim: send clockd type 2"
check "clock-timesup-hand: started only for it, closed after Stop" sh -c "grep -q \"clock: time's up stopped\" '$OUT/log/thand.log' && grep -q 'ring-only, closing' '$OUT/log/thand.log'"
# ... +1 min instead: it stays (a timer runs: g_ringOnly cleared -- validation 3 note 4), and hands it over at the close
seed thand2; fixture thand2; printf '\n[timer]\nend = 500\nset = 300\nlabel =\n' >> "$OUT/w/thand2/$AL"
run clock thand2 "wait;wait;key 43;$(waits 20)quit;wait" SIM_SERVICES=notify,clockd "SIM_ARGS=--ring timer"
check "clock-timesup-hand: +1 min -> the Clock stays" sh -c "grep -q 'one more minute' '$OUT/log/thand2.log' && ! grep -q 'ring-only' '$OUT/log/thand2.log'"
check "clock-timesup-hand: ... the new minute handed over at the close (set = 60)" sh -c "grep -q 'timer handed to clockd (01:00 left)' '$OUT/log/thand2.log' && [ '$(kv thand2 $AL 4 set)' = 60 ]"
# by message to the running Clock (CLOCK_MSG_OPEN "--ring timer"): the same, and it stays
seed tmsg; fixture tmsg; printf '\n[timer]\nend = 500\nset = 300\nlabel =\n' >> "$OUT/w/tmsg/$AL"
run clock tmsg "wait;waitlog 100 clock: time's up;wait;key 27;$(waits 10)" SIM_SERVICES=notify,clockd SIM_ARGS=world "SIM_MBOX=@40:1:13:--ring timer"
check "clock-timesup-hand (message): rung by the running Clock, it stays after Stop" sh -c "grep -q \"clock: time's up stopped\" '$OUT/log/tmsg.log' && ! grep -q 'ring-only' '$OUT/log/tmsg.log' && grep -q 'sim: end of the script' '$OUT/log/tmsg.log'"
# clock-veil-stack (R-8): Time's up over the open alarm editor; Esc stops it, then Esc cancels the editor (still there)
seed tstack; fixture tstack; config tstack '[clock]\ntimer = 3\n'
run clock tstack "wait;key 32;mods 1;key 50;mods 0;wait;key 13;wait;waitlog 300 clock: time's up;wait;dump $OUT/timer-stack.elsm;key 27;wait;key 27;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=timer; png timer-stack
check "clock-veil-stack: Time's up over the editor" test "$(lineof tstack 'clock: editor edit 1')" -lt "$(lineof tstack "clock: time's up (")"
check "clock-veil-stack: ... the tab left as it was (the editor's)" test "$(count tstack 'clock: tab timer')" = 1
check "clock-veil-stack: Esc stops it, then Esc cancels the editor" test "$(lineof tstack "time's up stopped")" -lt "$(lineof tstack 'clock: editor cancelled')"
# clock-edit-keys (c) (G9): a Timer spin box focused, Ctrl+2 -> the Alarms tab, no 2 typed in the duration
seed tkeys; run clock tkeys "wait;down $MINF;up $MINF;mods 1;key 50;mods 0;wait;mods 1;key 51;mods 0;wait;key 32;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=timer
check "clock-edit-keys (c): Ctrl+2 over a focused spin box -> the Alarms tab" logs tkeys "clock: tab alarms"
check "clock-edit-keys (c): ... the duration unchanged (05:00)" logs tkeys "clock: timer started 05:00"

# ==== the Stopwatch (step 10) =====================================================================================
# clock-sw (AC-37, 38): Space, three laps (about 12.0 / 11.8 / 12.8 s), Space -> 3 rows newest first, the fastest and
# the slowest marked; R refused while it runs; Ctrl+C -> the laps on the clipboard (tab-separated, the header); R clears
seed sw; run clock sw "wait;key 32;$(waits 600)key l;key r;$(waits 590)key l;$(waits 640)key l;key 32;wait;dump $OUT/stopwatch.elsm;mods 1;key 0x03;mods 0;wait;dump $OUT/stopwatch-copied.elsm;key r;wait;dump $OUT/stopwatch-zero.elsm;exit" \
	SIM_SERVICES=notify,clockd SIM_ARGS=stopwatch SIM_CLIPFILE="$OUT/sw.clip"; png stopwatch; png stopwatch-copied; png stopwatch-zero
check "clock-sw: started, three laps" sh -c "grep -q 'clock: stopwatch started 00:00.00' '$OUT/log/sw.log' && grep -q 'clock: lap 3 ' '$OUT/log/sw.log'"
check "clock-sw: R refused while it runs" logs sw "clock: stopwatch reset refused (running)"
check "clock-sw: stopped with 3 laps" logs sw "(3 laps)"
check "clock-sw: the rows newest first (3, 2, 1)" test "$(grep -A3 'stopwatch stopped' "$OUT/log/sw.log" | sed -n 2,4p | cut -d' ' -f3 | tr '\n' ' ')" = "3 2 1 "
check "clock-sw: lap 2 (11.84) the fastest, lap 3 (12.82) the slowest" sh -c "grep -A3 'stopwatch stopped' '$OUT/log/sw.log' | grep -q 'lap 2 00:11.84 00:23.86 fastest' && grep -A3 'stopwatch stopped' '$OUT/log/sw.log' | grep -q 'lap 3 00:12.82 00:36.68 slowest'"
check "clock-sw: the lap times add up to the total" sh -c "grep -A3 'stopwatch stopped' '$OUT/log/sw.log' | grep -q 'lap 1 00:12.02 00:12.02\$'"
check "clock-sw: Ctrl+C -> laps copied" logs sw "clock: laps copied (3)"
check "clock-sw: the clipboard: the header, then a lap a line, tab-separated, oldest first" sh -c "printf 'Lap\tLap time\tTotal\n1\t00:12.02\t00:12.02\n2\t00:11.84\t00:23.86\n3\t00:12.82\t00:36.68\n' | cmp -s - '$OUT/sw.clip'"
check "clock-sw: R clears it (stopped)" logs sw "clock: stopwatch reset"
# French: the header in the system's language
seed swfr fr; run clock swfr "wait;key 32;$(waits 60)key l;$(waits 60)key l;key 32;wait;dump $OUT/stopwatch-fr.elsm;mods 1;key 0x03;mods 0;wait;exit" \
	SIM_SERVICES=notify,clockd SIM_ARGS=stopwatch SIM_CLIPFILE="$OUT/swfr.clip"; png stopwatch-fr
check "clock-sw (fr): the clipboard's header in French" sh -c "head -1 '$OUT/swfr.clip' | grep -q '^Tour	Temps du tour	Total\$'"
# Ctrl+C on another tab: nothing copied
seed swother; run clock swother "wait;key 32;$(waits 30)key l;mods 1;key 49;mods 0;wait;mods 1;key 0x03;mods 0;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=stopwatch
check "clock-sw: Ctrl+C off the Stopwatch tab copies nothing" nolog swother "laps copied"
# the stopwatch kept at the close (R-1): running in the same boot (its start in ticks) -> it goes on
seed swkeep; config swkeep '[clock]\nsw_run = 1\nsw_start = 500\nsw_base = 0\nsw_total = 380\nsw_tick = 880\nsw_utc = -1\nsw_laps = 200\n'
run clock swkeep "wait;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=stopwatch
check "clock-sw: the same boot -> it goes on from its start (00:05, 1 lap)" grep -qE "clock: stopwatch kept 00:05\.[0-9]{2} running \(1 laps\)" "$OUT/log/swkeep.log"
# ... another boot, the real time known both times (60 s later): it went on meanwhile
UTC=$(python3 -c "import calendar;print(calendar.timegm((2026,9,28,10,34,0)) - 60)")
seed swboot; config swboot "[clock]\nsw_run = 1\nsw_start = 50000\nsw_base = 0\nsw_total = 1000\nsw_tick = 51000\nsw_utc = $UTC\nsw_laps =\n"
run clock swboot "wait;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=stopwatch SIM_CLOCK=20260928123400
check "clock-sw: another boot, the real time known -> 00:10 + 60 s, running" grep -qE "clock: stopwatch kept 01:10\.[0-9]{2} running" "$OUT/log/swboot.log"
# ... another boot, no real time: stopped at the time it had
seed swlost; config swlost '[clock]\nsw_run = 1\nsw_start = 50000\nsw_base = 0\nsw_total = 1000\nsw_tick = 51000\nsw_utc = -1\nsw_laps =\n'
run clock swlost "wait;wait;exit" SIM_SERVICES=notify,clockd SIM_ARGS=stopwatch
check "clock-sw: another boot, no real time -> stopped at 00:10.00" logs swlost "clock: stopwatch kept 00:10.00 (0 laps)"
# the close keeps it: sw_* in config.ini (the laps' totals)
seed swclose; run clock swclose "wait;key 32;$(waits 50)key l;$(waits 20)quit;wait" SIM_SERVICES=notify,clockd SIM_ARGS=stopwatch
check "clock-sw: kept at the close, running (sw_run = 1, sw_start, sw_laps)" sh -c "[ '$(kv swclose $CF 1 sw_run)' = 1 ] && [ -n '$(kv swclose $CF 1 sw_start)' ] && [ '$(kv swclose $CF 1 sw_laps)' -gt 0 ]"

echo
if [ $FAILS -ne 0 ]; then echo "clock-sim: $FAILS of $((PASS + FAILS)) checks FAILED"; exit 1; fi
echo "clock-sim: all $PASS checks passed"
