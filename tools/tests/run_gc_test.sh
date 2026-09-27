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
# the hardware: a bare-metal program (hwtest.c -> .dol): the VI's picture and display interrupts,
# the GX FIFO through the write-gather pipe (a token and "draw done" reported by the PE)
powerpc-linux-gnu-gcc $F -ffreestanding -nostdlib -Wl,-Ttext=0x80003100 -Wl,-e,_start "$here/gc/hwtest.c" -o "$T/hwtest.elf"
python3 "$root/tools/gc/elf2dol.py" "$T/hwtest.elf" "$T/hwtest.dol"
out=$("$T/gctest" dol "$T/hwtest.dol" 40 "$T/hw.ppm")
echo "$out" | grep -q "results: 0000001E 0000000F 00001234 .* 600D600D" && echo "ok  : hwtest.dol: 30 VI interrupts, PE token 0x1234 + draw done" || { echo "FAIL hwtest.dol"; echo "$out"; exit 1; }
# the GX drawing: gxtest.c's commands through the FIFO (vertex format, matrices, projection,
# viewport, TEV, an RGB565 texture) -> the GPU frame, rendered in software: the checker quad, the
# shaded triangle
powerpc-linux-gnu-gcc $F -ffreestanding -nostdlib -Wl,-Ttext=0x80003100 -Wl,-e,_start "$here/gc/gxtest.c" -o "$T/gxtest.elf"
python3 "$root/tools/gc/elf2dol.py" "$T/gxtest.elf" "$T/gxtest.dol"
GC_GX="$T/gx.ppm" "$T/gctest" dol "$T/gxtest.dol" 5 "$T/gxxfb.ppm" > /dev/null
python3 - "$T/gx.ppm" <<'X'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); p = im.load()
checks = [((108, 103), (255, 255, 255), "the checker: white"), ((118, 103), (255, 0, 0), "the checker: red"),
          ((450, 110), (255, 0, 0), "the triangle's red corner"), ((590, 375), (0, 255, 0), "its green corner"),
          ((360, 375), (0, 0, 255), "its blue corner"), ((20, 20), (0, 0, 0), "the background")]
bad = 0
for (x, y), want, what in checks:
    got = p[x, y]
    if max(abs(a - b) for a, b in zip(got, want)) > 40: print("FAIL gxtest: %s at %d,%d: %s" % (what, x, y, got)); bad += 1
print("ok  : gxtest.dol: the GX frame (textured quad, shaded triangle)" if not bad else "FAIL gxtest.dol")
sys.exit(bad)
X
