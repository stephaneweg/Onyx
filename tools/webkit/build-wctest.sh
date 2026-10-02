#!/bin/sh
# build-wctest.sh -- build wctest (tools/webkit/wctest.cpp: a page loaded, laid out and painted
# into a PNG by WebCore alone) against the WebCore build of build-webcore.sh, for Onyx and,
# with BENCH=1, relinked for the posixsim bench (tools/tests/posixsim: qemu on the PC).
#
#   sh tools/webkit/build-webcore.sh              # libWebCore.a first
#   sh tools/webkit/build-wctest.sh               # -> $BUILD/bin/wctest (an Onyx program)
#   BENCH=1 sh tools/webkit/build-wctest.sh       # + $POSIXSIM_ROOT/SD/bin/wctest
#
# It is compiled with the very command WebCore's own sources get (taken from ninja), so it sees
# WebCore's private headers and configuration. Variables: WEBKIT_DIR, BUILD, ONYX_SYSROOT (as
# build-webcore.sh), POSIXSIM_ROOT (default $HOME/posixsim).
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

[ -f "$BUILD/lib/libWebCore.a" ] || { echo "build-wctest.sh: no $BUILD/lib/libWebCore.a: sh tools/webkit/build-webcore.sh" >&2; exit 1; }

# ---- compile: the command of one of WebCore's own (not unified) sources, the file swapped ----
ref=platform/onyx/PlatformScreenOnyx.cpp
cmd=$(cd "$BUILD" && ninja -t commands lib/libWebCore.a | grep -F "$ref" | grep -F -- " -c " | head -n 1)
[ -n "$cmd" ] || { echo "build-wctest.sh: no compile command for $ref in $BUILD" >&2; exit 1; }
cmd=$(printf '%s' "$cmd" | sed -E 's/ -MD -MT [^ ]+ -MF [^ ]+//; s/ -o [^ ]+/ -o wctest.o/; s| -c [^ ]+| -c '"$HERE"'/wctest.cpp|')
echo "wctest: compile"
( cd "$BUILD" && sh -c "$cmd" )

# ---- link: WebKit's static libraries, then the sysroot's in dependency order ----
WK="-Wl,--start-group $BUILD/lib/libWebCore.a $BUILD/lib/libPAL.a $BUILD/lib/libJavaScriptCore.a $BUILD/lib/libWTF.a $BUILD/lib/libbmalloc.a -Wl,--end-group"
LIBS=""
for l in skia harfbuzz-icu harfbuzz freetype png16 jpeg webpmux webpdemux webp sharpyuv curl mbedtls mbedx509 mbedcrypto \
	nghttp2 brotlidec xml2 sqlite3 z icui18n icuuc icudata; do
	LIBS="$LIBS $S/lib/lib$l.a"
done
mkdir -p "$BUILD/bin"
echo "wctest: link"
aarch64-onyx-elf-g++ -mcpu=cortex-a72 -specs="$S/lib/onyx.specs" -L"$S/lib" -Wl,--gc-sections \
	"$BUILD/wctest.o" $WK $LIBS -o "$BUILD/bin/wctest"
aarch64-onyx-elf-size "$BUILD/bin/wctest"

if [ "${BENCH:-0}" = 1 ]; then
	echo "wctest: relink for the bench"
	BUILD_ONLY=1 PROG=none NAME=wctest SIM_CXX=1 PREFIX=aarch64-onyx-elf- CFLAGS_EXTRA="-Wl,--gc-sections" \
		OBJS="$BUILD/wctest.o $WK $LIBS" sh "$ONYX/tools/tests/posixsim/run.sh"
	ls -l "$POSIXSIM_ROOT/SD/bin/wctest"
fi
