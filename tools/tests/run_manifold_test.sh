#!/bin/sh
# run_manifold_test.sh -- Manifold as Onyx links it (user/Libs/manifold/libmanifold.a: third_party/manifold-3.5.4
# + Clipper2, the bare-metal toolchain, newlib), tried in AArch64 on the PC: manifold/mftest.cpp linked with it,
# newlib's system calls done by Linux ones (basic/a64/linux_shim.c), run under qemu-aarch64. Unions, cuts,
# intersections, and the bracket of 3DForge's mock-ups step by step: closed solids of the volumes expected.
#   sh tools/tests/run_manifold_test.sh
# Needs aarch64-none-elf-g++ (the kernel's toolchain) and qemu-aarch64 (qemu-user).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
command -v aarch64-none-elf-g++ > /dev/null || { echo "skipped: no aarch64-none-elf-g++"; exit 0; }
command -v qemu-aarch64 > /dev/null || { echo "skipped: no qemu-aarch64"; exit 0; }
BIN=${MF_A64_BIN:-$HOME/.cache/onyx_mftest_a64}
mkdir -p "$(dirname "$BIN")"
make -s -C "$ROOT/user" -j"$(nproc)" Libs/manifold/libmanifold.a
MF="$ROOT/third_party/manifold-3.5.4"; CL="$ROOT/third_party/clipper2-46f6391/CPP/Clipper2Lib"
aarch64-none-elf-gcc -O2 -c "$HERE/basic/a64/linux_shim.c" -o "$BIN.shim.o"
aarch64-none-elf-g++ -std=c++17 -mcpu=cortex-a72 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -static -nostartfiles --specs=nosys.specs \
    -include "$ROOT/user/Libs/manifold/onyx_manifold.h" -DMANIFOLD_PAR=-1 -DMANIFOLD_CROSS_SECTION -DMANIFOLD_NO_IOSTREAM \
    -DMANIFOLD_NO_FILESYSTEM -DCLIPPER2_NO_IOSTREAM -I"$MF/include" -I"$CL/include" -Wl,--gc-sections \
    "$HERE/manifold/mftest.cpp" "$BIN.shim.o" "$ROOT/user/Libs/manifold/libmanifold.a" -lm -o "$BIN" 2>&1 \
    | grep -v "is not implemented and will always fail" | grep -v "in function" || true
[ -x "$BIN" ] || { echo "mftest did not build"; exit 1; }
qemu-aarch64 -cpu cortex-a72 "$BIN"
