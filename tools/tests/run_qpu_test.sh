#!/bin/sh
# run_qpu_test.sh -- the QPU tools on the PC: user/v3d/qpu.h (the run-time builder) against
# tools/qpu's assembler and its instruction restrictions (qpubuild_test.cpp).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
Q="$root/tools/qpu"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
for f in qpulib ralloc_stub mesa/broadcom/qpu/qpu_instr mesa/broadcom/qpu/qpu_pack mesa/broadcom/qpu/qpu_disasm; do
	gcc -std=gnu11 -O1 -w -I"$Q/mesa" -I"$Q" -c "$Q/$f.c" -o "$T/$(basename $f).o"
done
g++ -std=c++17 -O1 -w -I"$root/user" -I"$Q" -I"$Q/mesa" "$here/v3d/qpubuild_test.cpp" "$root/user/v3d/qpu.cpp" "$T"/*.o -o "$T/qpubuild"
"$T/qpubuild"
