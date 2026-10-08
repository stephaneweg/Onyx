#!/bin/sh
# tools/tests/session/run.sh -- the sessions on the PC (docs/POCKETUI-TECH-STUDY.md P4):
#   1. session_test.cpp: SystemKit's session.h (the autostart of a card from before the sessions split, the session
#      files' programs, the mode in system.ini), autostart_has / autostart_ensure across the session files, Setup's
#      end with the session files -- on the card's files as shipped and the old autostart (autostart.before: the card's
#      of 2026-10-08, before the sessions);
#   2. session_tool_test.cpp: /bin/session itself (its commands, its parsing, the switch against stand-in processes,
#      windows and graphics server), each case a run of its own;
#   3. pkg commit's split: the C compile of session.h (/bin/session includes it as C on the PC).
#
#   sh tools/tests/session/run.sh          -> "session: all checks passed"
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
set -e
cd "$(dirname "$0")/../../.."
T=tools/tests/session
D=tools/tests/desktop_sim
OUT=${TMPDIR:-/tmp}/onyx_session_test
rm -rf "$OUT"; mkdir -p "$OUT/nocard"
INC="-I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include"
CXX="g++ -std=gnu++17 -O1 -g -w $INC -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
SAN="-fsanitize=undefined -fno-sanitize-recover=undefined"
fail () { echo "session: FAIL $*"; exit 1; }
N=0; ok () { N=$((N + 1)); }

$CXX -c $D/fakekapi.cpp -o "$OUT/fakekapi.o"

# ---- 1. SystemKit's sessions --------------------------------------------------------------------------------------
OLD="$OUT/autostart.old"
cp $T/autostart.before "$OLD"		# (the card's autostart of 2026-10-08, before the sessions)
$CXX $SAN -o "$OUT/session_test" $T/session_test.cpp "$OUT/fakekapi.o" -lpthread
env SIM_WRITES="$OUT/w" SIM_SD="$OUT/nocard" OLD_AUTOSTART="$OLD" SIM=exit "$OUT/session_test" || fail "SystemKit's sessions"

# ---- 3. session.h in C (warnings of its own: none) -----------------------------------------------------------------
printf '#include "systemkit/systemkit.h"\nint main (void) { char b[64]; return session_mode () + session_programs (0, b, 64) + session_migrate (); }\n' > "$OUT/s_c.c"
gcc -std=c99 -Wall -Wextra -c $INC "$OUT/s_c.c" -o "$OUT/s_c.o" 2> "$OUT/s_c.warn" || { cat "$OUT/s_c.warn"; fail "session.h in C"; }
grep -q "session\.\|autostart\." "$OUT/s_c.warn" && { cat "$OUT/s_c.warn"; fail "session.inc / autostart.inc warn in C"; }

# ---- 2. /bin/session ----------------------------------------------------------------------------------------------
mkdir -p "$OUT/obj"
gcc -std=gnu99 -O1 -g -w $INC -Dmain=session_main -c user/BinUtils/session.c -o "$OUT/obj/session.o"
$CXX -c user/Kits/uikit/win.cpp -o "$OUT/obj/win.o"
$CXX -c user/Kits/uikit/port.cpp -o "$OUT/obj/port.o"
$CXX -o "$OUT/session_tool" $T/session_tool_test.cpp "$OUT"/obj/*.o "$OUT/fakekapi.o" -lpthread

# card CASE: a fresh card with the shipped etc/ files (system.ini, autostart, the sessions')
card () {
	W="$OUT/t_$1"; rm -rf "$W"; mkdir -p "$W/etc/session" "$W/tmp"
	cp sdcard/etc/system.ini sdcard/etc/autostart "$W/etc/"; cp sdcard/etc/session/* "$W/etc/session/"
}
# tool CASE "ARGS" [VAR=value ...] -> its log in $OUT/CASE.log, its exit status in $OUT/CASE.rc
tool () {
	c=$1; a=$2; shift 2
	env SIM_WRITES="$OUT/t_$c" SIM_SD="$OUT/nocard" SIM_ARGS="$a" SIM=exit "$@" "$OUT/session_tool" > "$OUT/$c.log" 2>&1 || { cat "$OUT/$c.log"; fail "$c: crashed"; }
	sed -n 's/^h: exit //p' "$OUT/$c.log" > "$OUT/$c.rc"
}
rc () { [ "$(cat "$OUT/$1.rc")" = "$2" ] || { cat "$OUT/$1.log"; fail "$1: exit $(cat "$OUT/$1.rc"), not $2"; }; ok; }
has () { grep -qF -- "$2" "$OUT/$1.log" || { cat "$OUT/$1.log"; fail "$1: no '$2'"; }; ok; }
hasnt () { ! grep -qF -- "$2" "$OUT/$1.log" || { cat "$OUT/$1.log"; fail "$1: '$2' there"; }; ok; }
order () { a=$(grep -nF -- "$2" "$OUT/$1.log" | head -1 | cut -d: -f1); b=$(grep -nF -- "$3" "$OUT/$1.log" | head -1 | cut -d: -f1)
	[ -n "$a" ] && [ -n "$b" ] && [ "$a" -lt "$b" ] || { cat "$OUT/$1.log"; fail "$1: '$2' not before '$3'"; }; ok; }
shell () { sed -n 's/^ *shell *= *//p' "$OUT/t_$1/etc/system.ini"; }

# the boot: the running server's mode (the PC's: the desktop) -> its file, Setup's held-back lines not run
card boot; tool boot ""
rc boot 0; has boot "sim: exec SD:bin/run voronoy"; has boot "sim: exec SD:bin/run setup"; has boot "sim: exec SD:bin/run notifyd"
hasnt boot "run menubar"; hasnt boot "run dock"; hasnt boot "run terminal"
# a mode's file asked; the desktop's file missing: its own programs; a pocket file missing: nothing
card start; tool start "start pocket"; rc start 0; has start "sim: exec SD:bin/run menubar"; has start "sim: exec SD:bin/run terminal"; hasnt start "voronoy"
card nodesk; rm "$OUT/t_nodesk/etc/session/desktop"; tool nodesk "start"; rc nodesk 0; has nodesk "the desktop's own programs"; has nodesk "sim: exec SD:bin/run dock"
card nopocket; rm "$OUT/t_nopocket/etc/session/pocket"; tool nopocket "start pocket"; rc nopocket 5; hasnt nopocket "sim: exec"
# `sleep`, `wait`, a `session` line inside a session file (skipped), arguments
card lines; printf 'sleep 1\nwait pkg commit\nsession\n  run tinypad SD:/a.txt  \r\n#setup: run dock\n' > "$OUT/t_lines/etc/session/console"
tool lines "start console"; rc lines 0; has lines "sim: spawn SD:bin/pkg commit"; has lines "sim: exec SD:bin/run tinypad SD:/a.txt"; hasnt lines "SD:bin/session"; hasnt lines "run dock"
# list, mode, help, the parsing's errors
card list; tool list "list console"; rc list 0; has list "terminal"; has list "gamelib"
card list2; tool list2 "list"; rc list2 0; has list2 "notifyd"
card mode; printf 'shell=console\n' >> "$OUT/t_mode/etc/system.ini"; tool mode "mode"; rc mode 0; has mode "chosen: console"; has mode "running: desktop"
card warn; printf 'shell=pocket\n' >> "$OUT/t_warn/etc/system.ini"; tool warn ""; rc warn 0; has warn "asks for pocket, the server running is the desktop's"; has warn "run voronoy"
tool help "help"; rc help 0; has help "usage: session"
tool bad1 "switch netbook"; rc bad1 4; has bad1 "no mode \"netbook\""
tool bad2 "switch"; rc bad2 4
tool bad3 "switch pocket --bogus"; rc bad3 4; has bad3 "what is --bogus"
tool bad4 "switch pocket --wait x"; rc bad4 4
tool bad5 "frobnicate"; rc bad5 4
# the same mode: nothing done
card same; tool same "switch Desktop"; rc same 0; has same "already"; hasnt same "h: switch"
# a program that does not close: listed, nothing switched (shell= untouched, no session program ended)
P="7:menubar:s,8:dock:s,10:agenda:s,11:notifyd:s,21:printd,30:ledger:wx,31:tinypad:w,9:clockd"
card wait; tool wait "switch pocket --wait 1" HPROCS="$P"
rc wait 2; has wait "h: close 30"; has wait "h: close 31"; has wait "Ledger - doc is waiting for an answer"; hasnt wait "h: switch"; hasnt wait "h: kill"
hasnt wait "h: close 7"; [ "$(shell wait)" = "" ] || fail "wait: shell= written"; ok
grep -qx "ledger	Ledger - doc" "$OUT/t_wait/tmp/session.wait" || fail "wait: session.wait"; ok
# --no-ask: not asked again; --force: ended, then the switch
card force; tool force "switch pocket --no-ask --force --wait 1" HPROCS="$P"
rc force 0; hasnt force "h: close"; has force "h: kill 30 1"; has force "h: kill 7 0"; has force "h: kill 8 0"; has force "h: kill 10 0"; has force "h: kill 11 0"
has force "h: kill 21 0"; hasnt force "h: kill 9"; has force "h: switch"; [ "$(shell force)" = "pocket" ] || fail "force: shell=$(shell force)"; ok
order force "h: kill 7 0" "h: switch"; order force "h: switch" "sim: exec SD:bin/run menubar"; has force "sim: exec SD:bin/run terminal"
has force "sim: exec SD:bin/run printd"; hasnt force "run voronoy"
# every program closes at once: no wait; the console's file; the parent (the terminal that typed it) kept, ended last
card cons; tool cons "switch console" HPROCS="7:menubar:s,31:tinypad:w,12:terminal:wp"
rc cons 0; has cons "h: close 31"; hasnt cons "h: close 12"; order cons "sim: exec SD:bin/run gamelib" "h: kill 12 0"; [ "$(shell cons)" = "console" ] || fail "cons: shell"; ok
# --keep: the Control Panel (an applet's host) not asked, ended last
card keep; tool keep "switch pocket --keep 40,41" HPROCS="40:control:w,31:tinypad:w"
rc keep 0; hasnt keep "h: close 40"; has keep "h: close 31"; order keep "h: switch" "h: kill 40 0"
# its server failed: the desktop, shell=desktop, the desktop's file, a notification
card fell; tool fell "switch pocket" HPROCS="7:menubar:s" HSWITCH=1 SIM_SERVICES=notify
rc fell 1; [ "$(shell fell)" = "desktop" ] || fail "fell: shell=$(shell fell)"; ok; has fell "sim: exec SD:bin/run voronoy"; hasnt fell "run terminal"
has fell "could not start: the desktop instead"
# refused (a full-screen program): shell= back as it was, the old session started again
card busy; printf 'shell=desktop\n' >> "$OUT/t_busy/etc/system.ini"; tool busy "switch console" HPROCS="7:menubar:s,21:printd" HSWITCH=-16
rc busy 3; [ "$(shell busy)" = "desktop" ] || fail "busy: shell=$(shell busy)"; ok; has busy "sim: exec SD:bin/run voronoy"; has busy "sim: exec SD:bin/run printd"; hasnt busy "gamelib"
# a kernel before v97: shell= written, the Pi restarted
card old; tool old "--switch pocket" HSWITCH=-88; has old "restarting"; has old "[sim: reboot]"; [ "$(shell old)" = "pocket" ] || fail "old: shell"; ok
# migrate (what pkg commit does)
card mig; cp "$OLD" "$OUT/t_mig/etc/autostart"; tool mig "migrate"; rc mig 0; has mig "split"
grep -qx "session" "$OUT/t_mig/etc/autostart" && grep -qx "#setup: run dock" "$OUT/t_mig/etc/session/desktop" || fail "mig: the files"; ok
card mig2; tool mig2 "migrate"; rc mig2 0; has mig2 "nothing to split"
echo "session: /bin/session: $N checks passed"

echo "session: all checks passed"
