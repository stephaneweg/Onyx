#!/bin/sh
# run_n3ds_test.sh -- the Nintendo 3DS core (user/Emulators/n3ds) on the PC: builds tools/tests/n3ds/n3dstest for
# AArch64 with Onyx's toolchain (Dynarmic: user/Libs/dynarmic; newlib's system calls done by Linux ones:
# basic/a64/linux_shim.c, n3ds/t0_shim.c) and runs the test programs of tools/tests/n3ds/progs under
# qemu-aarch64 -- the real JIT. Each program checks itself and ends with "<n> checks, 0 failed".
#   sh tools/tests/run_n3ds_test.sh [program.elf [frames]]	(a program: run it and show what it says)
# Needs aarch64-none-elf-g++, qemu-aarch64, and arm-none-eabi-gcc for the test programs (built here when it is
# on the PATH: n3ds/src/build.sh).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
B=${N3DS_BUILD:-$HOME/.cache/onyx_n3ds}
command -v aarch64-none-elf-g++ > /dev/null || { echo "skipped: no aarch64-none-elf-g++"; exit 0; }
command -v qemu-aarch64 > /dev/null || { echo "skipped: no qemu-aarch64"; exit 0; }
mkdir -p "$B/core"
D="$ROOT/user/Libs/dynarmic"
make -C "$D" -j"$(nproc)" OUT="$B/dynarmic" > "$B/build.log" 2>&1 || { grep -E "error|Error" "$B/build.log" | head -20; exit 1; }
DYN="$ROOT/third_party/dynarmic-a466015"
INC="-I$DYN/src -I$DYN/externals/mcl/include -I$DYN/externals/fmt/include -isystem $ROOT/third_party/boost-1.86.0 -I$D/onyx -include $D/onyx/onyx_std_mutex.h"
ARCH="-mcpu=cortex-a72 -mno-outline-atomics -ffunction-sections -fdata-sections"
U="-I$ROOT/user/Emulators"
OBJS=""
for src in "$ROOT"/user/Emulators/n3ds/*.cpp "$HERE/n3ds/n3dstest.cpp"; do
	o="$B/core/$(basename "$src" .cpp).o"
	if [ ! -f "$o" ] || [ "$src" -nt "$o" ] || [ "$ROOT/user/Emulators/n3ds/n3ds.h" -nt "$o" ]; then
		case "$src" in
		*n3ds_cpu.cpp) aarch64-none-elf-g++ -std=c++20 -O2 -Wall -Wextra $ARCH $INC $U -c "$src" -o "$o" ;;
		*)             aarch64-none-elf-g++ -std=c++17 -O2 -Wall -Wextra $ARCH -fno-exceptions -fno-rtti $U -c "$src" -o "$o" ;;
		esac
	fi
	OBJS="$OBJS $o"
done
aarch64-none-elf-gcc -O2 -c "$HERE/basic/a64/linux_shim.c" -o "$B/linux_shim.o"
aarch64-none-elf-gcc -O2 -I"$D/onyx" -c "$HERE/n3ds/t0_shim.c" -o "$B/t0_shim.o"
aarch64-none-elf-g++ $ARCH -static -nostartfiles --specs=nosys.specs -Wl,--gc-sections $OBJS "$B/dynarmic/libdynarmic.a" \
	"$B/linux_shim.o" "$B/t0_shim.o" -o "$B/n3dstest" 2>&1 | grep -v "is not implemented and will always fail" | grep -v "in function" || true
[ -x "$B/n3dstest" ] || { echo "n3dstest did not build"; exit 1; }
if [ -n "$1" ]; then exec qemu-aarch64 "$B/n3dstest" "$@"; fi
if command -v arm-none-eabi-gcc > /dev/null; then sh "$HERE/n3ds/src/build.sh" > /dev/null; fi
[ -d "$HERE/n3ds/progs" ] || { echo "no test program (tools/tests/n3ds/src/build.sh needs arm-none-eabi-gcc)"; exit 0; }
fail=0
for p in "$HERE"/n3ds/progs/*.elf; do
	name=$(basename "$p" .elf)
	out=$(qemu-aarch64 "$B/n3dstest" "$p" 2> "$B/$name.err") && st=0 || st=$?
	last=$(printf '%s\n' "$out" | tail -1)
	if [ $st = 0 ] && printf '%s' "$last" | grep -q "checks, 0 failed$"; then echo "ok   $name ($last)"
	else echo "FAIL $name"; printf '%s\n' "$out" | grep -E "^FAIL|^     " | head -20; echo "     $last"; cat "$B/$name.err"; fail=1; fi
done
exit $fail
