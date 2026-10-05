#!/bin/sh
# check_pic.sh -- is this an Onyx shared library the kernel can load? (docs/SHARED-LIBS-PLAN.md
# section 2.) Checks: an ET_DYN, two LOAD segments + PT_DYNAMIC, no text relocations, ONLY
# R_AARCH64_RELATIVE relocations and all of them in the RW segment, the entry = the export table,
# one exported symbol.
#   sh check_pic.sh                 builds the test library (user/demo) and checks it
#   sh check_pic.sh <file.so>...    checks these (user/lib/uikit.so ...)
# Needs aarch64-none-elf-* on the PATH (PREFIX=... for another toolchain).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
U=$HERE/../../../user
P=${PREFIX:-aarch64-none-elf-}
R=${P}readelf
fail () { echo "FAIL: $*"; exit 1; }
check () {
	S=$1
	$R -h "$S" | grep -q 'DYN (Shared object file)' || fail "$S: not ET_DYN"
	[ "$($R -lW "$S" | grep -c '^  LOAD')" = 2 ] || fail "$S: not two LOAD segments"
	$R -lW "$S" | grep -q '^  DYNAMIC' || fail "$S: no PT_DYNAMIC"
	if $R -dW "$S" | grep -q 'TEXTREL'; then fail "$S: text relocations"; fi
	OTHER=$($R -rW "$S" | awk '/^[0-9a-f]+ +[0-9a-f]+ R_AARCH64/ && $3 != "R_AARCH64_RELATIVE"')
	[ -z "$OTHER" ] || fail "$S: relocations other than RELATIVE: $OTHER"
	RW=$($R -lW "$S" | awk '/^  LOAD/ && $7 ~ /RW/ { print $3, $5 }')
	set -- $RW; LO=$(printf '%d' "$1"); HI=$(( LO + $(printf '%d' "$2") ))
	BAD=$($R -rW "$S" | awk -v lo=$LO -v hi=$HI '/R_AARCH64_RELATIVE/ { o = strtonum ("0x" $1); if (o < lo || o + 8 > hi) print $1 }' | head -3)
	[ -z "$BAD" ] || fail "$S: relocation outside the RW segment's file bytes: $BAD"
	ENTRY=$($R -hW "$S" | awk '/Entry point/ { print $4 }')
	TAB=$($R -sW "$S" | awk '$8 == "onyx_lib_table" { print "0x" $2; exit }')
	[ "$(printf '%d' "$ENTRY")" = "$(printf '%d' "$TAB")" ] || fail "$S: entry $ENTRY != onyx_lib_table $TAB"
	[ "$($R --dyn-syms -W "$S" | awk '$5 == "GLOBAL"' | wc -l)" = 1 ] || fail "$S: more than one exported symbol"
	echo "OK: $S -- $($R -rW "$S" | grep -c R_AARCH64_RELATIVE) RELATIVE relocations, table at $ENTRY"
	$R -lW "$S" | grep -E '^  (LOAD|DYNAMIC)'
}
if [ $# -gt 0 ]; then
	for f in "$@"; do check "$f"; done
	exit 0
fi
O=${TMPDIR:-/tmp}/onyx-shlib-check; mkdir -p "$O"
CF="-O2 -fPIC -fvisibility=hidden -ffreestanding -nostdlib -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -mgeneral-regs-only -I$U -I$U/Kits -I$U/../kernel/include"
${P}g++ $CF -c "$U/demo/demolib.cpp" -o "$O/demolib.o"
${P}g++ $CF -c "$U/librt.cpp" -o "$O/librt.o"
${P}ld -shared -Bsymbolic -z text -z max-page-size=0x10000 --no-undefined --hash-style=sysv --build-id=none \
	-T "$U/lib.ld" --version-script "$U/lib.vers" -e onyx_lib_table -o "$O/demo.so" "$O/demolib.o" "$O/librt.o"
check "$O/demo.so"
