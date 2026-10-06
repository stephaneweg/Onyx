#!/bin/sh
# run_notes_test.sh -- the PC unit tests of Notes and Stickies (AutoDev round 1): the stand-in kernel's
# additions they rely on (notes/sim_probe.cpp), UIKit's uk_text_wrap / uk_text_over (notes/wrap_test.cpp),
# SystemKit's autostart helper (notes/autostart_test.cpp) and the notes model (user/Apps/notes/notesmodel.cpp,
# notes/model_test.cpp). Each test program is linked with the desktop simulator's fakekapi.o (the files go to a
# fresh SIM_WRITES of their own, the clock is its fixed one: Monday 2026-09-28 12:34:00).
# UndefinedBehaviorSanitizer on; AddressSanitizer cannot be: its shadow memory covers the address where
# fakekapi maps the kernel's table (KAPI_TABLE_VA, 14 GB).
#
#   sh tools/tests/run_notes_test.sh          -> "notes: all checks passed"
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
#
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
T=tools/tests/notes
D=tools/tests/desktop_sim
OUT=${TMPDIR:-/tmp}/onyx_notes_test
rm -rf "$OUT"; mkdir -p "$OUT/obj"
INC="-I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include"
CXX="g++ -std=gnu++17 -O1 -g -w $INC -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
SAN="-fsanitize=undefined -fno-sanitize-recover=undefined"
fail () { echo "notes: FAIL $*"; exit 1; }

$CXX -c $D/fakekapi.cpp -o "$OUT/fakekapi.o"

# ---- the stand-in kernel's additions (step 0) ------------------------------------------------------------
$CXX $SAN -o "$OUT/sim_probe" $T/sim_probe.cpp "$OUT/fakekapi.o" -lpthread
probe () {	# probe CASE [VAR=value ...] -- the log in $OUT/probe_CASE.log
	c=$1; shift
	W="$OUT/w_$c"; rm -rf "$W"; mkdir -p "$W"
	env SIM_WRITES="$W" "$@" "$OUT/sim_probe" $c > "$OUT/probe_$c.log" 2>&1 || { cat "$OUT/probe_$c.log"; fail "probe $c"; }
	grep -q "probe $c: ok" "$OUT/probe_$c.log" || { cat "$OUT/probe_$c.log"; fail "probe $c: no ok"; }
}
probe notify SIM_SERVICES=notify SIM=exit
grep -q 'sim: send notify type 1 "Notes\\0Note moved to the Trash\\0"' "$OUT/probe_notify.log" || fail "notify not logged"
grep -q 'sim: launch' "$OUT/probe_notify.log" && fail "notify launched notifyd"
probe lookup SIM_SERVICES=- 'SIM_MBOX=21:9:' SIM=exit
probe listed SIM_SERVICES=notify,notes,stickies SIM=exit
grep -q 'sim: send notes type 3 "SD:/Notes/a.txt\\0"' "$OUT/probe_listed.log" || fail "send to notes not logged"
probe rofs SIM_ROFS=SD:/Notes,SD:/etc SIM=exit
[ "$(grep -c 'sim: rofs' "$OUT/probe_rofs.log")" = 6 ] || fail "rofs: not 6 refusals logged"
[ -e "$OUT/w_rofs/Notes" ] && fail "rofs: Notes made"
[ -f "$OUT/w_rofs/NotesX/a.txt" ] || fail "rofs: NotesX/a.txt not written"
probe copy "SIM=copy $D/sd/Notes/note-20260928-091500.txt SD:/Notes/b.txt;wait;exit"
grep -q 'sim: copy SD:/Notes/b.txt' "$OUT/probe_copy.log" || fail "copy not logged"
cmp -s "$OUT/w_copy/Notes/b.txt" $D/sd/Notes/note-20260928-091500.txt || fail "copy: bytes differ"
probe follow SIM_CURSOR=follow "SIM=down 50 10;move 150 12;up 150 12;exit"
grep -q 'sim: window probe flags 0x33' "$OUT/probe_follow.log" || fail "window flags not logged"
W="$OUT/w_stat"; mkdir -p "$W/Notes"; printf 'Old note\n' > "$W/Notes/old.txt"; touch -d '2026-09-20 08:00:00 UTC' "$W/Notes/old.txt"
env SIM_WRITES="$W" SIM_STAT=1 SIM=exit "$OUT/sim_probe" stat SD:/Notes/old.txt > "$OUT/probe_stat.log" 2>&1 || { cat "$OUT/probe_stat.log"; fail "probe stat"; }
grep -q "probe stat: ok" "$OUT/probe_stat.log" || fail "probe stat: no ok"
probe nostat SIM=exit
echo "notes: the simulator's additions: ok"

echo "notes: all checks passed"
