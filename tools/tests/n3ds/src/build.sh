#!/bin/sh
# build.sh -- the 3DS core's test programs (arm-none-eabi-gcc: ARMv6K, VFP, as a 3DS program is built):
# tools/tests/n3ds/progs/<test>.elf. Our own code only (sys.S: the start-up and the system calls).
set -e
S=$(cd "$(dirname "$0")" && pwd)
O=$S/../progs
mkdir -p "$O"
CC=arm-none-eabi-gcc
CF="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mfpu=vfp -marm -O2 -Wall -Wextra -ffreestanding -fno-builtin -fno-strict-aliasing -nostdlib -I$S"
LIBGCC=$($CC -march=armv6k -mfloat-abi=hard -mfpu=vfp -marm -print-libgcc-file-name)
for t in kernel gfx gpu; do
	$CC $CF -T "$S/link.ld" -Wl,--build-id=none -Wl,-z,max-page-size=0x1000 "$S/sys.S" "$S/util.c" "$S/$t.c" "$LIBGCC" -o "$O/$t.elf"
	echo "  $O/$t.elf"
done
