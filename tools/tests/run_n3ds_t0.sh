#!/bin/sh
# run_n3ds_t0.sh -- the 3DS emulator's phase T0 (docs/3DS-EMULATOR-STUDY.md): Dynarmic (the ARM11 JIT; its sources
# in third_party/, built by user/Libs/dynarmic/Makefile with Onyx's bare-metal toolchain) and n3ds/dynarmic_t0.cpp
# run through its AArch64 back end under qemu-aarch64 (newlib's system calls done by Linux ones:
# basic/a64/linux_shim.c; the code memory: n3ds/t0_shim.c). ARM, Thumb and VFP programs, with the memory
# callbacks and with a page table, and a code invalidation: "all passed".
#   sh tools/tests/run_n3ds_t0.sh
# Needs aarch64-none-elf-g++ (the kernel's toolchain) and qemu-aarch64 (qemu-user). The first run builds
# Dynarmic (a few minutes) into N3DS_BUILD (default ~/.cache/onyx_n3ds).
# With the apps' libraries built (make -C user), it also links the same test as an Onyx program,
# $N3DS_BUILD/n3dst0.elf: put it (stripped) in SD:/bin of a Pi and run "n3dst0" there -- the same lines.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
B=${N3DS_BUILD:-$HOME/.cache/onyx_n3ds}
command -v aarch64-none-elf-g++ > /dev/null || { echo "skipped: no aarch64-none-elf-g++"; exit 0; }
command -v qemu-aarch64 > /dev/null || { echo "skipped: no qemu-aarch64"; exit 0; }
mkdir -p "$B"
D="$ROOT/user/Libs/dynarmic"
make -C "$D" -j"$(nproc)" OUT="$B/dynarmic" > "$B/build.log" 2>&1 || { grep -E "error|Error" "$B/build.log" | head -20; exit 1; }
DYN="$ROOT/third_party/dynarmic-a466015"
INC="-I$DYN/src -I$DYN/externals/mcl/include -I$DYN/externals/fmt/include -isystem $ROOT/third_party/boost-1.86.0 -I$D/onyx -include $D/onyx/onyx_std_mutex.h"
ARCH="-mcpu=cortex-a72 -mno-outline-atomics -ffunction-sections -fdata-sections"
aarch64-none-elf-gcc -O2 -c "$HERE/basic/a64/linux_shim.c" -o "$B/linux_shim.o"
aarch64-none-elf-gcc -O2 -I"$D/onyx" -c "$HERE/n3ds/t0_shim.c" -o "$B/t0_shim.o"
aarch64-none-elf-g++ -std=c++20 -O2 -Wall $ARCH $INC -static -nostartfiles --specs=nosys.specs -Wl,--gc-sections \
	"$HERE/n3ds/dynarmic_t0.cpp" "$B/dynarmic/libdynarmic.a" "$B/linux_shim.o" "$B/t0_shim.o" -o "$B/dynarmic_t0" 2>&1 \
	| grep -v "is not implemented and will always fail" | grep -v "in function" || true
[ -x "$B/dynarmic_t0" ] || { echo "the test did not build"; exit 1; }
qemu-aarch64 "$B/dynarmic_t0"
# the same test as an Onyx program (linked as the apps are: user/Makefile's NL_CXXFLAGS / NL_LDFLAGS)
U="$ROOT/user"
if [ -f "$U/lib/appkit_stubs.o" ] && [ -f "$U/Runtime/libc/crt0libc.o" ] && [ -f "$U/Runtime/libc/onyx_syscalls.o" ]; then
	aarch64-none-elf-g++ -std=c++20 -O2 $ARCH -fno-stack-protector -fno-pic -fno-pie -nostartfiles -fno-exceptions -fno-rtti \
		-fno-threadsafe-statics -fno-use-cxa-atexit $INC -Wl,-T,"$U/Runtime/user.ld" -Wl,-z,max-page-size=0x10000 \
		-Wl,--build-id=none -Wl,--gc-sections "$U/lib/appkit_stubs.o" "$U/Runtime/libc/crt0libc.o" "$U/Runtime/libc/onyx_syscalls.o" \
		"$HERE/n3ds/dynarmic_t0.cpp" "$B/dynarmic/codemem.o" "$B/dynarmic/libdynarmic.a" -lm -o "$B/n3dst0.elf" \
		&& echo "the Onyx program: $B/n3dst0.elf ($(aarch64-none-elf-size "$B/n3dst0.elf" | tail -1 | cut -f1) bytes of code)"
fi
