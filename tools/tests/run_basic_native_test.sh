#!/bin/sh
# run_basic_native_test.sh -- Onyx BASIC in machine code (user/basic/basjit.h, AArch64), on the PC: the
# host of run_basic_test.sh built for AArch64 with the bare-metal toolchain (newlib; its system calls
# done by Linux ones: basic/a64/linux_shim.c) and run under qemu-aarch64. Every basic/progs/*.bas must
# print what its .out says -- what the VM prints -- in machine code, and again with MANAGED=1 (the VM of
# the same build); then the fuzzer (basic/a64/fuzz.py): random programs, machine code against VM.
#   sh tools/tests/run_basic_native_test.sh [--nofuzz]
# Needs aarch64-none-elf-g++ (the kernel's toolchain) and qemu-aarch64 (qemu-user).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
command -v aarch64-none-elf-g++ > /dev/null || { echo "skipped: no aarch64-none-elf-g++"; exit 0; }
command -v qemu-aarch64 > /dev/null || { echo "skipped: no qemu-aarch64"; exit 0; }
BIN=${BAS_A64_BIN:-$HOME/.cache/onyx_basic_host_a64}
mkdir -p "$(dirname "$BIN")"
aarch64-none-elf-gcc -O2 -c "$HERE/basic/a64/linux_shim.c" -o "$BIN.shim.o"
aarch64-none-elf-g++ -std=c++17 -O2 -Wall -Wextra -DBAS_A64_SHIM -static -nostartfiles --specs=nosys.specs -I"$ROOT/user" -I"$ROOT/user/Kits" \
    "$ROOT/user/basic/basnum.cpp" "$ROOT/user/basic/bascomp.cpp" "$ROOT/user/basic/basvm.cpp" "$ROOT/user/basic/basbax.cpp" \
    "$HERE/basic/host_main.cpp" "$BIN.shim.o" -o "$BIN" 2>&1 | grep -v "is not implemented and will always fail" | grep -v "in function" || true
[ -x "$BIN" ] || { echo "the AArch64 host did not build"; exit 1; }
cd "$HERE/basic/progs"
fail=0
for mode in native managed; do
	if [ $mode = managed ]; then export MANAGED=1; else unset MANAGED; fi
	for bas in *.bas; do
		name=${bas%.bas}
		if [ -f "$name.events" ]; then export EVENTS="$(cat "$name.events")"; else unset EVENTS; fi
		if [ -f "$name.in" ]; then out=$(qemu-aarch64 "$BIN" "$bas" cmdarg < "$name.in" 2>&1 || true)
		else out=$(qemu-aarch64 "$BIN" "$bas" cmdarg < /dev/null 2>&1 || true); fi
		if [ "$(printf '%s\n' "$out")" = "$(cat "$name.out")" ]; then echo "ok   $name ($mode)"
		else echo "FAIL $name ($mode)"; printf '%s\n' "$out" | diff "$name.out" - | head -20; fail=1; fi
	done
done
unset MANAGED EVENTS
if [ "$1" != "--nofuzz" ] && [ -f "$HERE/basic/a64/fuzz.py" ]; then
	python3 "$HERE/basic/a64/fuzz.py" "$BIN" ${FUZZ_N:-300} || fail=1
fi
exit $fail
