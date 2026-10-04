#!/bin/sh
# check_pic.sh -- does the toolchain make an Onyx shared library the kernel can load?
# (docs/SHARED-LIBS-PLAN.md, step 0.) Builds demolib.cpp with lib.ld and checks: an ET_DYN, two
# LOAD segments + PT_DYNAMIC, no text relocations, ONLY R_AARCH64_RELATIVE relocations and all of
# them in the RW segment, the entry = the export table, one exported symbol. Needs aarch64-none-elf-*.
set -e
cd "$(dirname "$0")"
P=${PREFIX:-aarch64-none-elf-}
O=${TMPDIR:-/tmp}/onyx-shlib-check; mkdir -p "$O"
${P}g++ -O2 -fPIC -fvisibility=hidden -ffreestanding -nostdlib -fno-exceptions -fno-rtti \
	-fno-threadsafe-statics -fno-use-cxa-atexit -mgeneral-regs-only -c demolib.cpp -o "$O/demolib.o"
${P}ld -shared -Bsymbolic -z text -z max-page-size=0x10000 --no-undefined --hash-style=sysv \
	-T lib.ld --version-script lib.map -e onyx_lib_table -o "$O/demo.so" "$O/demolib.o"
R=${P}readelf
fail () { echo "FAIL: $*"; exit 1; }
$R -h "$O/demo.so" | grep -q 'DYN (Shared object file)' || fail "not ET_DYN"
[ "$($R -lW "$O/demo.so" | grep -c '^  LOAD')" = 2 ] || fail "not two LOAD segments"
$R -lW "$O/demo.so" | grep -q '^  DYNAMIC' || fail "no PT_DYNAMIC"
$R -dW "$O/demo.so" | grep -q 'TEXTREL' && fail "text relocations"
OTHER=$($R -rW "$O/demo.so" | awk '/^[0-9a-f]+ +[0-9a-f]+ R_AARCH64/ && $3 != "R_AARCH64_RELATIVE"')
[ -z "$OTHER" ] || fail "relocations other than RELATIVE: $OTHER"
RW=$($R -lW "$O/demo.so" | awk '/^  LOAD/ && $7 ~ /RW/ { print $3, $6 }')
set -- $RW; LO=$(printf '%d' "$1"); HI=$(( LO + $(printf '%d' "$2") ))
for off in $($R -rW "$O/demo.so" | awk '/R_AARCH64_RELATIVE/ { print $1 }'); do
	o=$(printf '%d' "0x$off"); [ "$o" -ge "$LO" ] && [ "$o" -lt "$HI" ] || fail "relocation at 0x$off outside the RW segment"
done
ENTRY=$($R -hW "$O/demo.so" | awk '/Entry point/ { print $4 }')
TAB=$($R -sW "$O/demo.so" | awk '$8 == "onyx_lib_table" { print "0x" $2; exit }')
[ "$(printf '%d' "$ENTRY")" = "$(printf '%d' "$TAB")" ] || fail "entry $ENTRY != onyx_lib_table $TAB"
[ "$($R --dyn-syms -W "$O/demo.so" | awk '$5 == "GLOBAL"' | wc -l)" = 1 ] || fail "more than one exported symbol"
echo "OK: $O/demo.so -- $($R -rW "$O/demo.so" | grep -c R_AARCH64_RELATIVE) RELATIVE relocations, table at $ENTRY"
$R -lW "$O/demo.so" | grep -E '^  (LOAD|DYNAMIC)'
