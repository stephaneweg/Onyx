#!/bin/sh
# run_gpucomp_test.sh -- the GPU compositing service (user/gpucomp) on the PC: its CPU path and its
# GPU path (on hostkapi.cpp's software V3D: the kernel's FS_TEX in the QPU simulator) against a
# reference, partial updates, refused textures, the GPU lost; then gpcdemo's own self-test and
# benchmark built for the PC (CPU, then the software V3D).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
Q="$root/tools/qpu"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
for f in qpulib ralloc_stub mesa/broadcom/qpu/qpu_instr mesa/broadcom/qpu/qpu_pack mesa/broadcom/qpu/qpu_disasm; do
	gcc -std=gnu11 -O1 -w -I"$Q/mesa" -I"$Q" -c "$Q/$f.c" -o "$T/$(basename $f).o"
done
g++ -std=gnu++17 -O2 -w -I"$root/kernel/include" -I"$Q" -I"$Q/mesa" -c "$here/gpucomp/hostkapi.cpp" -o "$T/hostkapi.o"
g++ -std=gnu++17 -O2 -w -I"$Q" -I"$Q/mesa" -c "$Q/qpusim.cpp" -o "$T/sim.o"
gcc -std=gnu11 -O2 -ffp-contract=off -Wall -Wextra -I"$root/user" -I"$root/kernel/include" -c "$root/user/gpucomp/gpucomp.c" -o "$T/gpucomp.o"
g++ -std=gnu++17 -O2 -w -I"$root/user" -I"$root/kernel/include" "$here/gpucomp/gpctest.cpp" "$T/gpucomp.o" "$T/hostkapi.o" "$T/sim.o" "$T"/qpu*.o "$T"/ralloc_stub.o -lpthread -o "$T/gpctest"
"$T/gpctest"
# the CPU path built for the Pi (-O3: NEON loops) under qemu-aarch64 = the PC's, bit for bit
A64=${A64_GCC:-aarch64-none-elf-gcc}
if command -v "$A64" >/dev/null 2>&1 && command -v qemu-aarch64 >/dev/null 2>&1; then
	gcc -O2 -ffp-contract=off -I"$root/user" -I"$root/kernel/include" "$here/gpucomp/neontest.c" "$root/user/gpucomp/gpucomp.c" -o "$T/neon_pc"
	"$A64" -ffreestanding -nostdlib -static -fno-pic -fno-pie -mcpu=cortex-a72 -O3 -ffp-contract=off -fno-math-errno -fno-stack-protector \
		-DGPC_NO_CLOCK -I"$root/user" -I"$root/kernel/include" "$here/gpucomp/neontest.c" "$root/user/gpucomp/gpucomp.c" -lgcc \
		-Wl,-Ttext=0x400000 -Wl,-e,_start -o "$T/neon_pi"
	"$T/neon_pc" > "$T/h_pc.txt"; qemu-aarch64 "$T/neon_pi" > "$T/h_pi.txt"
	if cmp -s "$T/h_pc.txt" "$T/h_pi.txt"; then echo "PASS  the CPU path built for the Pi (NEON, qemu-aarch64) = the PC's ($(wc -l < "$T/h_pc.txt") pictures)"
	else echo "FAIL  the CPU path built for the Pi differs from the PC's:"; paste "$T/h_pc.txt" "$T/h_pi.txt"; exit 1; fi
else
	echo "(skipped: the CPU path on qemu-aarch64 -- $A64 or qemu-aarch64 not found)"
fi
if [ -f "$root/user/bin/gpcdemo.c" ]; then
	gcc -std=gnu11 -O2 -Wall -Wextra -DGPC_HOST -I"$root/user" -I"$root/kernel/include" "$root/user/bin/gpcdemo.c" "$T/gpucomp.o" "$T/hostkapi.o" "$T/sim.o" "$T"/qpu*.o "$T"/ralloc_stub.o -lstdc++ -lm -lpthread -o "$T/gpcdemo"
	echo "---- gpcdemo test (the CPU)"; "$T/gpcdemo" test
	echo "---- gpcdemo test (the software V3D, 480 x 270)"; GPC_SOFTGPU=1 "$T/gpcdemo" test 480 270
	echo "---- gpcdemo bench 1920 1080 (the CPU; the PC)"; "$T/gpcdemo" bench 1920 1080 5
fi
