#!/bin/sh
# tools/tests/netsurf/jit/build.sh -- qjsrun (QuickJS-ng alone, qjsrun.c) for the PC and for
# AArch64 Linux (static; run by qemu-aarch64 -- the Pi's CPU flags), into $OUT (default
# /tmp/qjsrun). CFLAGS adds to both; OPT replaces -O2; QJSC names another quickjs.c (a baseline).
#
#   sh tools/tests/netsurf/jit/build.sh
cd "$(dirname "$0")/../../../.."
OUT=${OUT:-/tmp/qjsrun}
Q=third_party/quickjs-ng-0.17.0
SRC="${QJSC:-$Q/quickjs.c} $Q/libregexp.c $Q/libunicode.c $Q/dtoa.c tools/tests/netsurf/jit/qjsrun.c"
mkdir -p "$OUT"
gcc ${OPT:--O2} -std=gnu11 -w -DNDEBUG $CFLAGS -I$Q -o "$OUT/qjsrun" $SRC -lm || exit 1
if command -v aarch64-linux-gnu-gcc >/dev/null; then
	aarch64-linux-gnu-gcc -mcpu=cortex-a72 ${OPT:--O2} -std=gnu11 -w -static -DNDEBUG $CFLAGS -I$Q \
		-o "$OUT/qjsrun-a64" $SRC -lm || exit 1
fi
ls -l "$OUT"/qjsrun*
