#!/bin/sh
# elfrun.sh APP [STACK_BYTES] -- an app's own Pi binary (user/APP.elf, else sdcard/apps/APP.app/main: newlib
# and all, as the Pi runs it) on the PC, under qemu-aarch64, its kapi table the desktop simulator's. The
# script and the other variables as for shots.sh: SIM="wait;...;dump x.elsm;exit", SIM_ARGS, SIM_WRITES...
# The stack: 256 KB, or STACK_BYTES (an app.txt's "stack", e.g. 4194304 for the Spreadsheet), a guard
# page below it (an overflow faults). What the simulator cannot show -- the C library, the code the Pi's
# compiler made -- shows here. Needs aarch64-linux-gnu-g++ and qemu-aarch64:
#   apt install g++-aarch64-linux-gnu qemu-user
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT"
OUT=${TMPDIR:-/tmp}/onyx_elfrun
mkdir -p "$OUT"
CXX="aarch64-linux-gnu-g++ -std=gnu++17 -O1 -g -w -I user -I user/Kits -I kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
$CXX -c tools/tests/desktop_sim/fakekapi.cpp -o "$OUT/fakekapi.o"
$CXX -static -o "$OUT/elfrun" tools/tests/desktop_sim/elfrun.cpp "$OUT/fakekapi.o"
elf=user/$1.elf
[ -f "$elf" ] || elf=sdcard/apps/$1.app/main
export SIM_OVERLAY=${SIM_OVERLAY:-tools/tests/desktop_sim/sd} SIM_WRITES=${SIM_WRITES:-$OUT/writes}
exec qemu-aarch64 "$OUT/elfrun" "$elf" ${2:-262144}
