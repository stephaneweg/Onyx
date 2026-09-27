#!/bin/sh
# run_gc_test.sh -- the GameCube core (user/gc) on the PC:
#   * the Gekko's integer / floating point instructions against qemu-ppc: tools/tests/gc/cputest.c
#     compiled once (powerpc-linux-gnu-gcc), run under qemu-ppc -cpu 750 (the reference) and in
#     the interpreter (linked at 0x80003100): every result, CR, XER, FPRF must match;
#   * the paired singles / quantized loads and stores against the manual (pstest.S).
# Needs gcc-powerpc-linux-gnu, binutils-powerpc-linux-gnu and qemu-user.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
F="-mcpu=750 -O1 -fno-stack-protector -fno-pic -fno-pie"
powerpc-linux-gnu-gcc $F -c "$here/gc/cputest.c" -o "$T/cputest.o"
powerpc-linux-gnu-gcc $F -static "$here/gc/cputest_main.c" "$T/cputest.o" -o "$T/cputest_qemu"
qemu-ppc -cpu 750 "$T/cputest_qemu" > "$T/expected.bin"
powerpc-linux-gnu-ld -Ttext=0x80003100 -e run_tests -nostdlib "$T/cputest.o" -o "$T/cputest.elf"
powerpc-linux-gnu-as -m750cl "$here/gc/pstest.S" -o "$T/pstest.o"
powerpc-linux-gnu-ld -Ttext=0x80003100 -e run_ps -nostdlib "$T/pstest.o" -o "$T/pstest.elf"
g++ -std=c++17 -O2 -Wall -Wextra -I"$root/user" "$here/gc/gctest.cpp" "$root"/user/gc/*.cpp -o "$T/gctest"
"$T/gctest" cpu "$T/cputest.elf" "$T/expected.bin"
"$T/gctest" ps "$T/pstest.elf"
