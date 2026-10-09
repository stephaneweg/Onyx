#!/bin/sh
# build.sh -- the core's test programs (arm-none-eabi-gcc): tools/tests/nds/roms/<test>.nds, and the PC's
# reference results of the C check (roms/ccheck_ref.txt).
set -e
S=$(cd "$(dirname "$0")" && pwd)
O=$S/../roms
mkdir -p "$O" "$O/obj"
CC=arm-none-eabi-gcc
C9="-mcpu=arm946e-s -O2 -ffreestanding -fno-strict-aliasing -nostdlib -fno-builtin -I$S"
C7="-mcpu=arm7tdmi -O2 -ffreestanding -fno-strict-aliasing -nostdlib -fno-builtin -I$S"
LIBGCC9=$($CC -mcpu=arm946e-s -print-libgcc-file-name)
LIBGCC7=$($CC -mcpu=arm7tdmi -mthumb -print-libgcc-file-name)
# arm9 <name> <files...> / arm7 <name> <files...>
link9 () { n=$1; shift; $CC $C9 -mthumb-interwork -T "$S/link9.ld" "$S/crt9.S" "$@" "$LIBGCC9" -o "$O/obj/$n.9.elf"; arm-none-eabi-objcopy -O binary "$O/obj/$n.9.elf" "$O/obj/$n.9.bin"; }
link7 () { n=$1; shift; $CC $C7 -mthumb-interwork -T "$S/link7.ld" "$S/crt7.S" "$@" "$LIBGCC7" -o "$O/obj/$n.7.elf"; arm-none-eabi-objcopy -O binary "$O/obj/$n.7.elf" "$O/obj/$n.7.bin"; }
pack () { python3 "$S/mknds.py" "$O/$1.nds" "$2" "$3" "$O/obj/$1.9.bin" "$O/obj/$1.7.bin"; echo "  $O/$1.nds"; }

# cputest
$CC $C9 -marm -mthumb-interwork -DCCHECK=ccheck_a9 -c "$S/ccheck.c" -o "$O/obj/cc_a9.o"
$CC $C9 -mthumb -mthumb-interwork -DCCHECK=ccheck_t9 -c "$S/ccheck.c" -o "$O/obj/cc_t9.o"
$CC $C7 -marm -mthumb-interwork -DCCHECK=ccheck_a7 -c "$S/ccheck.c" -o "$O/obj/cc_a7.o"
$CC $C7 -mthumb -mthumb-interwork -DCCHECK=ccheck_t7 -c "$S/ccheck.c" -o "$O/obj/cc_t7.o"
link9 cputest "$S/cpu9.c" "$S/asm9.S" "$O/obj/cc_a9.o" "$O/obj/cc_t9.o"
link7 cputest "$S/cpu7.c" "$O/obj/cc_a7.o" "$O/obj/cc_t7.o"
pack cputest CPUTEST CPUT
link9 bench "$S/bench.9.c" "$O/obj/cc_a9.o" "$O/obj/cc_t9.o"
link7 bench "$S/bench.7.c" "$O/obj/cc_t7.o"
pack bench BENCH BNCH
for t in systest gfx2d gfx3d sound; do
	if [ -f "$S/$t.9.c" ]; then
		link9 $t "$S/$t.9.c"
		link7 $t "$S/$t.7.c"
		pack $t $(echo $t | tr a-z A-Z) $(echo $t | cut -c1-4 | tr a-z A-Z)
	fi
done
# the reference
cc -O2 -fno-strict-aliasing -DCCHECK=ccheck -o "$O/obj/ccheck_ref" "$S/ccheck.c" "$S/ccheck_main.c"
"$O/obj/ccheck_ref" > "$O/ccheck_ref.txt"
