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
[ "$1" = build ] && { echo "clock-sim: built $OUT/clockd${CLOCK_BUILT:+, $OUT/clock}"; exit 0; }

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

echo
if [ $FAILS -ne 0 ]; then echo "clock-sim: $FAILS of $((PASS + FAILS)) checks FAILED"; exit 1; fi
echo "clock-sim: all $PASS checks passed"
