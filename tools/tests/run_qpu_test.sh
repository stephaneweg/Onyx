#!/bin/sh
# run_qpu_test.sh -- the QPU tools on the PC: user/Libs/v3d/qpu.h (the run-time builder) against
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
g++ -std=c++17 -O1 -w -I"$root/user" -I"$root/user/Kits" -I"$root/user/Runtime" -I"$root/user/Include" -I"$root/user/Libs" -I"$root/user/Emulators" -I"$root/user/Ports" -I"$Q" -I"$Q/mesa" "$here/v3d/qpubuild_test.cpp" "$root/user/Libs/v3d/qpu.cpp" "$T"/*.o -o "$T/qpubuild"
"$T/qpubuild"
g++ -std=c++17 -O1 -w -I"$Q" -I"$Q/mesa" "$here/v3d/qpusim_test.cpp" "$Q/qpusim.cpp" "$T"/*.o -o "$T/qpusim"
"$T/qpusim"
g++ -std=c++17 -O1 -w -I"$root/user" -I"$root/user/Kits" -I"$root/user/Runtime" -I"$root/user/Include" -I"$root/user/Libs" -I"$root/user/Emulators" -I"$root/user/Ports" -I"$Q" -I"$Q/mesa" "$here/v3d/shaders_test.cpp" "$root/user/Libs/v3d/shaders.cpp" "$root/user/Libs/v3d/qpu.cpp" "$Q/qpusim.cpp" "$T"/*.o -o "$T/shaders"
"$T/shaders"
g++ -std=c++17 -O1 -w -I"$root/user" -I"$root/user/Kits" -I"$root/user/Runtime" -I"$root/user/Include" -I"$root/user/Libs" -I"$root/user/Emulators" -I"$root/user/Ports" -I"$Q" -I"$Q/mesa" "$here/v3d/gxtev_test.cpp" "$root/user/Libs/v3d/gxtev.cpp" "$root/user/Libs/v3d/qpu.cpp" "$Q/qpusim.cpp" "$T"/*.o -o "$T/gxtev"
"$T/gxtev" ${GXTEV_N:-2000}
# the kernel's shaders, V3D 4.2 and 7.1 (kshaders_test.cpp): the restrictions, the same outputs, the reference;
# and the committed .inc are what the .qasm assemble into
g++ -std=c++17 -O1 -w -I"$Q" -I"$Q/mesa" "$here/v3d/kshaders_test.cpp" "$Q/qpusim.cpp" "$T"/*.o -o "$T/kshaders"
"$T/kshaders" | tail -1
gcc -O1 -std=gnu11 -w -I"$Q/mesa" -o "$T/qpuasm" "$Q/qpuasm.c" "$Q/qpulib.c" "$Q/ralloc_stub.c" "$Q/mesa/broadcom/qpu/qpu_instr.c" "$Q/mesa/broadcom/qpu/qpu_pack.c" "$Q/mesa/broadcom/qpu/qpu_disasm.c"
for s in v3d_shaders v3d_shaders71; do
	(cd "$root/kernel/sys" && "$T/qpuasm" $s.qasm "$T/$s.inc" >/dev/null)
	if [ "$(tail -n +2 "$T/$s.inc")" = "$(tail -n +2 "$root/kernel/sys/$s.inc")" ]; then echo "ok  : kernel/sys/$s.inc is its .qasm's"; else echo "FAIL: kernel/sys/$s.inc is stale (cd tools/qpu && make)"; exit 1; fi
done
