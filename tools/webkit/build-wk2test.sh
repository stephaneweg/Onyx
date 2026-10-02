#!/bin/sh
# build-wk2test.sh -- build wk2test (tools/webkit/wk2test.cpp: WebKit2's three processes in one
# program, a page loaded through the C API and painted into a PNG) against the build of
# build-webkit.sh, for Onyx and, with BENCH=1, relinked for the posixsim bench.
#
#   sh tools/webkit/build-webkit.sh               # libWebKit.a first
#   sh tools/webkit/build-wk2test.sh              # -> $BUILD/bin/wk2test (an Onyx program)
#   BENCH=1 sh tools/webkit/build-wk2test.sh      # + $POSIXSIM_ROOT/SD/bin/wk2test
#
# It is compiled with the command one of WebKit's own sources gets (taken from ninja): the
# forwarding headers (<WebKit/...>) and Skia's are on its include path. Variables: as
# build-wctest.sh.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see fetch.sh).
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ONYX=$(cd "$HERE/../.." && pwd)
if [ -z "${WEBKIT_DIR:-}" ]; then
	if [ -d /home/user ]; then WEBKIT_DIR=/home/user/webkit; else WEBKIT_DIR=$HOME/webkit; fi
fi
: "${BUILD:=$WEBKIT_DIR-build/webcore}"
: "${ONYX_SYSROOT:=$ONYX/out/sysroot-onyx}"
: "${POSIXSIM_ROOT:=$HOME/posixsim}"
PATH=/opt/toolchains/aarch64-onyx-elf-14.2/bin:$PATH
export PATH POSIXSIM_ROOT
S=$ONYX_SYSROOT

[ -f "$BUILD/lib/libWebKit.a" ] || { echo "build-wk2test.sh: no $BUILD/lib/libWebKit.a: sh tools/webkit/build-webkit.sh" >&2; exit 1; }

# ---- compile: the command of one of WebKit's own (not unified) sources, the file swapped ----
ref=UIProcess/API/C/onyx/WKRunLoop.cpp
cmd=$(cd "$BUILD" && ninja -t commands lib/libWebKit.a | grep -F "$ref" | grep -F -- " -c " | head -n 1)
[ -n "$cmd" ] || { echo "build-wk2test.sh: no compile command for $ref in $BUILD" >&2; exit 1; }
cmd=$(printf '%s' "$cmd" | sed -E 's/ -MD -MT [^ ]+ -MF [^ ]+//; s/ -o [^ ]+/ -o wk2test.o/; s| -c [^ ]+| -c '"$HERE"'/wk2test.cpp|')
echo "wk2test: compile"
( cd "$BUILD" && sh -c "$cmd" )

# ---- link: WebKit's static libraries, then the sysroot's in dependency order ----
WK="-Wl,--start-group $BUILD/lib/libWebKit.a $BUILD/lib/libWebCore.a $BUILD/lib/libPAL.a $BUILD/lib/libJavaScriptCore.a $BUILD/lib/libWTF.a $BUILD/lib/libbmalloc.a -Wl,--end-group"
LIBS=""
for l in skia harfbuzz-icu harfbuzz freetype png16 jpeg webpmux webpdemux webp sharpyuv curl mbedtls mbedx509 mbedcrypto \
	nghttp2 brotlidec xml2 sqlite3 z icui18n icuuc icudata; do
	LIBS="$LIBS $S/lib/lib$l.a"
done
mkdir -p "$BUILD/bin"
echo "wk2test: link"
aarch64-onyx-elf-g++ -mcpu=cortex-a72 -specs="$S/lib/onyx.specs" -L"$S/lib" -Wl,--gc-sections \
	"$BUILD/wk2test.o" $WK $LIBS -o "$BUILD/bin/wk2test"
aarch64-onyx-elf-size "$BUILD/bin/wk2test"

if [ "${BENCH:-0}" = 1 ]; then
	echo "wk2test: relink for the bench"
	BUILD_ONLY=1 PROG=none NAME=wk2test SIM_CXX=1 PREFIX=aarch64-onyx-elf- CFLAGS_EXTRA="-Wl,--gc-sections" \
		OBJS="$BUILD/wk2test.o $WK $LIBS" sh "$ONYX/tools/tests/posixsim/run.sh"
	ls -l "$POSIXSIM_ROOT/SD/bin/wk2test"
fi
