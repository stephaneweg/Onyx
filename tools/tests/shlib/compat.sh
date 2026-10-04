#!/bin/sh
# compat.sh -- the compatibility test of the shared libraries (docs/SHARED-LIBS-PLAN.md section 6:
# "the reason for the work"): a program built against version N of wtk keeps running, NOT REBUILT,
# on version N+1 of the library.
#
# Builds, from the tree as it is (N) and from a patched copy of user/wtk (N+1):
#   wtkc-N.so, wtkc-N1.so     the library, under the name "wtkc" (the system's wtk.so is not touched)
#   app-N, app-N1             tools/tests/shlib/compat/app.cpp against each
# N+1 is N with: (a) a function "fixed" (wk_bfw returns 1234), (b) a function appended
# (wk_compat_added), (c) a reserved virtual slot of Widget given a meaning (onCompat, called by
# setFocus) and a reserved field written by Widget's constructor.
#
#   sh tools/tests/shlib/compat.sh [out-dir]          build (aarch64-none-elf-* on the PATH)
#   python tools/tests/shlib/compat_pi.py <pi-ip> <out-dir>   run on the Pi, PASS / FAIL lines
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
U=$ROOT/user
OUT=${1:-$ROOT/out/shlib-compat}
P=${PREFIX:-aarch64-none-elf-}
rm -rf "$OUT" && mkdir -p "$OUT/n/obj" "$OUT/n1/obj" "$OUT/n1/src"

LIBF="-ffreestanding -nostdlib -fPIC -fvisibility=hidden -mgeneral-regs-only -O2 -w -fno-stack-protector -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -DONYX_LIB_BUILD"
APPF="-ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only -O2 -w -fno-stack-protector -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit"
LDF="-shared -Bsymbolic -z text -z max-page-size=0x10000 --no-undefined --hash-style=sysv --build-id=none -T $U/lib.ld --version-script $U/lib.vers -e onyx_lib_table"
LIBGCC=$(${P}gcc -mcpu=cortex-a72 -print-libgcc-file-name)

# N+1's sources: a copy of user/wtk, patched
cp -r "$U/wtk" "$OUT/n1/src/wtk"
rm -f "$OUT"/n1/src/wtk/*.o "$OUT"/n1/src/wtk/*.a
python3 - "$OUT/n1/src/wtk" <<'EOF'
import sys
d = sys.argv[1]
def sub (f, old, new):
	p = d + "/" + f; s = open (p, encoding = "utf-8").read ()
	assert old in s, (f, old)
	open (p, "w", encoding = "utf-8", newline = "\n").write (s.replace (old, new, 1))
# (a) a "bug fixed" in a function of the library
sub ("text.cpp", "int wk_bfw () { int f = kapi_font_width  (); return f < 1 ? 8  : f; }",
     "int wk_bfw () { return 1234; }\nint wk_compat_added () { return 77; }")	# (b) and a function appended
sub ("text.h", "int  wk_bfw ();", "int  wk_bfw ();\nint  wk_compat_added ();")
# (c) a reserved virtual slot given a meaning, a reserved field used
sub ("widget.h", "virtual void wk_reserved0 () {}", "virtual void onCompat () {}")
sub ("widget.cpp", "{ canvas.alloc (w, h); }", "{ canvas.alloc (w, h); reserved_[0] = 49374; }")
sub ("widget.cpp", "\tr->clearFocusTree ();\n\tfocusPathUp ();", "\tr->clearFocusTree ();\n\tfocusPathUp ();\n\tonCompat ();")
EOF

cp "$U/wtk/wtk.abi" "$OUT/wtkc.abi"		# the list: N's, then N+1 appends to the same file

build () {	# $1: n | n1   $2: where "wtk/..." comes from   $3: extra app flags
	D=$OUT/$1; INC="-I$2 -I$U -I$ROOT/kernel/include"
	OBJS="$D/obj/globals.o"
	for f in "$2"/wtk/*.cpp; do
		b=$(basename "$f" .cpp); F=$LIBF
		case $b in canvas|imgload) F="$(echo "$LIBF" | sed 's/-mgeneral-regs-only//') -mcpu=cortex-a72";; esac
		${P}g++ $F $INC -c "$f" -o "$D/obj/$b.o" &
		[ "$b" = globals ] || OBJS="$OBJS $D/obj/$b.o"
	done
	${P}g++ $LIBF $INC -c "$U/librt.cpp" -o "$D/obj/librt.o" &
	wait
	python3 "$ROOT/tools/libgen/libgen.py" --nm ${P}nm --name wtkc --abi "$OUT/wtkc.abi" --init wtk_lib_init --vtables \
		--data onyx_wtk_data --allow-data '^_ZN3wtk' --table "$D/table.S" --stubs "$D/stubs.S" --bind "$D/bind.cpp" $OBJS
	${P}gcc -c "$D/table.S" -o "$D/table.o"
	${P}gcc -c "$D/stubs.S" -o "$D/stubs.o"
	${P}g++ $APPF $INC -c "$D/bind.cpp" -o "$D/bind.o"
	${P}g++ $APPF $INC -c "$2/wtk/globals.cpp" -o "$D/globals_imp.o"
	${P}ld $LDF -o "$OUT/wtkc-$1.so" $OBJS "$D/table.o" "$D/obj/librt.o" "$LIBGCC"
	rm -f "$D/wtkc.imp.a"; ${P}ar rcs "$D/wtkc.imp.a" "$D/stubs.o" "$D/bind.o" "$D/globals_imp.o"
	${P}g++ $APPF $3 $INC -Wl,-T,"$U/user.ld" -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none \
		-Wl,--defsym,memset=kapi_memset -Wl,--defsym,memcpy=kapi_memcpy -Wl,--defsym,memmove=kapi_memmove \
		"$U/crt0.S" "$HERE/compat/app.cpp" "$D/wtkc.imp.a" -o "$OUT/app-$1"
}
build n "$U" ""
N=$(grep -c '^[0-9]' "$OUT/wtkc.abi")
# (c) the reserved virtual is RENAMED on its line of the list: the slot keeps its place
sed -i 's/_ZN3wtk6Widget12wk_reserved0Ev/_ZN3wtk6Widget8onCompatEv/' "$OUT/wtkc.abi"
build n1 "$OUT/n1/src" "-DCOMPAT_N1"
N1=$(grep -c '^[0-9]' "$OUT/wtkc.abi")
sh "$HERE/check_pic.sh" "$OUT/wtkc-n.so" "$OUT/wtkc-n1.so" > /dev/null
echo "built in $OUT: wtkc-n.so (version $N), wtkc-n1.so (version $N1), app-n, app-n1"
[ "$N1" -gt "$N" ] || { echo "FAIL: N+1 has no new entry"; exit 1; }
