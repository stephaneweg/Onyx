#!/bin/sh
# run_clock_test.sh -- the PC unit tests of the Clock and clockd (AutoDev round 6, docs: autodev/rounds/06-clock/):
#   step 0  the stand-in kernel's SIM_CLOCK / SIM_TZ / set_timezone (clock/sim_probe.cpp)
#   step 1  SystemKit's locale_zone_offset_at and locale_zone_sync (clock/zone_test.cpp)
# Each test program is linked with the desktop simulator's fakekapi.o when it needs the kapi (the files go to a
# fresh SIM_WRITES of their own). UndefinedBehaviorSanitizer on (AddressSanitizer cannot be: its shadow memory
# covers the address where fakekapi maps the kernel's table).
#
#   sh tools/tests/run_clock_test.sh          -> "clock: all checks passed"
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
#
set -e
cd "$(dirname "$0")/../.."
T=tools/tests/clock
D=tools/tests/desktop_sim
OUT=${TMPDIR:-/tmp}/onyx_clock_test
rm -rf "$OUT"; mkdir -p "$OUT"
INC="-I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include"
CXX="g++ -std=gnu++17 -g $INC -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
SAN="-fsanitize=undefined -fno-sanitize-recover=undefined"
fail () { echo "clock: FAIL $*"; exit 1; }
steps () { printf 'wait;%.0s' $(seq 1 "$1"); echo exit; }	# a script of N waits then exit

$CXX -O1 -w -c $D/fakekapi.cpp -o "$OUT/fakekapi.o"

# ---- step 0: the simulator's clock -------------------------------------------------------------------------
$CXX -O1 -Wall -Wextra -Werror $SAN -o "$OUT/sim_probe" $T/sim_probe.cpp "$OUT/fakekapi.o" -lpthread
probe () {	# probe CASE [VAR=value ...] -- the log in $OUT/probe_CASE.log
	c=$1; shift
	W="$OUT/w_$c"; rm -rf "$W"; mkdir -p "$W"
	env -u SIM_CLOCK -u SIM_TZ -u SIM_STAT SIM_WRITES="$W" SIM="$(steps 130)" "$@" "$OUT/sim_probe" $c > "$OUT/probe_$c.log" 2>&1 || { cat "$OUT/probe_$c.log"; fail "probe $c"; }
	grep -q "probe $c: ok" "$OUT/probe_$c.log" || { cat "$OUT/probe_$c.log"; fail "probe $c: no ok"; }
}
probe clock SIM_CLOCK=20260928123400
probe tz SIM_CLOCK=20261025025930 SIM_TZ=120
grep -q '^sim: set_timezone 60$' "$OUT/probe_tz.log" || fail "set_timezone not logged"
grep -q '^sim: set_timezone 900$' "$OUT/probe_tz.log" || fail "a refused set_timezone not logged"
probe sim_tz SIM_CLOCK=20260101000000 SIM_TZ=-300
probe noclock
grep -q '^sim: set_timezone 60$' "$OUT/probe_noclock.log" || fail "set_timezone not logged (SIM_CLOCK unset)"
probe stat SIM_STAT=1
echo "clock: the simulator's clock: ok"

# ---- step 1: SystemKit's time zones at an instant -----------------------------------------------------------
$CXX -O1 -Wall -Wextra -Werror $SAN -o "$OUT/zone_test" $T/zone_test.cpp "$OUT/fakekapi.o" -lpthread
zone () {	# zone CASE SYSTEM.INI-TEXT ("-": none in the writes, the card's) [VAR=value ...] -- the log in $OUT/zone_CASE.log
	c=$1; ini=$2; shift 2
	W="$OUT/wz_$c"; rm -rf "$W"; mkdir -p "$W/etc"
	[ "$ini" = - ] || printf "$ini" > "$W/etc/system.ini"
	env -u SIM_CLOCK -u SIM_TZ -u SIM_STAT SIM_WRITES="$W" SIM="$(steps 30)" "$@" "$OUT/zone_test" $c > "$OUT/zone_$c.log" 2>&1 || { cat "$OUT/zone_$c.log"; fail "zone $c"; }
	grep -q "zone $c: ok" "$OUT/zone_$c.log" || { cat "$OUT/zone_$c.log"; fail "zone $c: no ok"; }
}
zone at -
# (locale.inc is C as well: a C program including systemkit.h has it inline on the PC)
printf '#include "systemkit/systemkit.h"\nint main (void) { return locale_zone_offset_at (0, 0LL); }\n' > "$OUT/zone_c.c"
gcc -std=c99 -Wall -Wextra -c $INC "$OUT/zone_c.c" -o "$OUT/zone_c.o" 2> "$OUT/zone_c.warn" || { cat "$OUT/zone_c.warn"; fail "locale.h in C"; }
grep -q "locale" "$OUT/zone_c.warn" && { cat "$OUT/zone_c.warn"; fail "locale.inc warns in C"; }
echo "clock: SystemKit's locale_zone_offset_at: ok"

echo "clock: all checks passed"
