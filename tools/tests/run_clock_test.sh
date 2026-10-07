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
zone () {	# zone CASE [VAR=value ...] -- the writes' system.ini: $OUT/zone_CASE.ini (fix), else none (the card's); the log in $OUT/zone_CASE.log
	c=$1; shift
	W="$OUT/wz_$c"; rm -rf "$W"; mkdir -p "$W/etc"
	if [ -f "$OUT/zone_$c.ini" ]; then cp "$OUT/zone_$c.ini" "$W/etc/system.ini"; fi
	env -u SIM_CLOCK -u SIM_TZ -u SIM_STAT SIM_WRITES="$W" SIM="$(steps 120)" "$@" "$OUT/zone_test" $c > "$OUT/zone_$c.log" 2>&1 || { cat "$OUT/zone_$c.log"; fail "zone $c"; }
	grep -q "zone $c: ok" "$OUT/zone_$c.log" || { cat "$OUT/zone_$c.log"; fail "zone $c: no ok"; }
}
zone at
# (locale.inc is C as well: a C program including systemkit.h has it inline on the PC)
printf '#include "systemkit/systemkit.h"\nint main (void) { return locale_zone_offset_at (0, 0LL); }\n' > "$OUT/zone_c.c"
gcc -std=c99 -Wall -Wextra -c $INC "$OUT/zone_c.c" -o "$OUT/zone_c.o" 2> "$OUT/zone_c.warn" || { cat "$OUT/zone_c.warn"; fail "locale.h in C"; }
grep -q "locale" "$OUT/zone_c.warn" && { cat "$OUT/zone_c.warn"; fail "locale.inc warns in C"; }
echo "clock: SystemKit's locale_zone_offset_at: ok"
STZ='sim: set_timezone'
once () { [ "$(grep -c "^$STZ" "$OUT/zone_$1.log")" = 1 ] && grep -q "^$STZ $2\$" "$OUT/zone_$1.log" || { cat "$OUT/zone_$1.log"; fail "zone $1: not one '$STZ $2'"; }; }
none () { grep -q "^$STZ" "$OUT/zone_$1.log" && { cat "$OUT/zone_$1.log"; fail "zone $1: the clock was set"; }; return 0; }
same () { cmp -s "$OUT/wz_$1/etc/system.ini" "$OUT/zone_$1.ini" || fail "zone $1: system.ini written"; }
fix () { printf "$2" > "$OUT/zone_$1.ini"; }	# fix CASE TEXT: the case's system.ini, kept to compare
fix autumn 'verbose=0\ntimezone=120\nzone=Brussels\n'; zone autumn SIM_CLOCK=20261025025930 SIM_TZ=120;	once autumn 60
fix spring 'zone=Paris\ntimezone=60\n'; zone spring SIM_CLOCK=20270328015930 SIM_TZ=60;			once spring 120
fix newyork 'timezone=-240\nzone=New York\n'; zone newyork SIM_CLOCK=20261101015930 SIM_TZ=-240;		once newyork -300
fix wrong 'timezone=60\nzone=Brussels\n'; zone wrong SIM_CLOCK=20260928123400 SIM_TZ=60;			once wrong 120
grep -q '^zone=Brussels$' "$OUT/wz_wrong/etc/system.ini" && grep -q '^timezone=120$' "$OUT/wz_wrong/etc/system.ini" || fail "zone wrong: system.ini"
fix right 'timezone=120\nzone=Brussels\n'; zone right SIM_CLOCK=20260928123400 SIM_TZ=120;			none right; same right
fix nozone 'language=en\n'; zone nozone SIM_CLOCK=20261025025930 SIM_TZ=120;				none nozone; same nozone
# validation 1, gap 1: timezone=120 and no zone= on the change night -- never Helsinki guessed, nothing written
fix guess 'verbose=0\ntimezone=120\nntp=pool.ntp.org\n'; zone guess SIM_CLOCK=20261025023000 SIM_TZ=120;	none guess; same guess
zone card SIM_CLOCK=20261025023000 SIM_TZ=120;								none card
[ -e "$OUT/wz_card/etc/system.ini" ] && fail "zone card: the card's system.ini written"
fix nowhere 'zone=Nowhere\ntimezone=120\n'; zone nowhere SIM_CLOCK=20261025025930 SIM_TZ=120;		none nowhere; same nowhere
fix noclock 'zone=Brussels\ntimezone=60\n'; zone noclock;							none noclock; same noclock
echo "clock: SystemKit's locale_zone_sync: ok"

echo "clock: all checks passed"
