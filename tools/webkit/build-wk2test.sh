#!/bin/sh
# build-wk2test.sh -- build wk2test (tools/webkit/wk2test.cpp: WebKit2's three processes in one
# program, a page loaded through the C API and painted into a PNG) against the build of
# build-webkit.sh, for Onyx and, with BENCH=1, relinked for the posixsim bench.
#
#   sh tools/webkit/build-webkit.sh               # libWebKit.a first
#   sh tools/webkit/build-wk2test.sh              # -> $BUILD/bin/wk2test (an Onyx program)
#   BENCH=1 sh tools/webkit/build-wk2test.sh      # + $POSIXSIM_ROOT/SD/bin/wk2test
#   BENCH=1 UMM_NEW=1 sh tools/webkit/build-wk2test.sh   # the bench's program with user/onyxpp.hpp's
#                                                 # operator new (umm.h over kapi_sbrk), as Web 1.0.4's
#                                                 # link had it: the app cores' raster must FAIL on the
#                                                 # bench ("made a kernel call: kapi slot 77")
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
# The compositor's two C files (as build-web.sh: WebKit's USE(GRAPHICS_LAYER_ONYX) calls them).
# ... and onyxcores.c (the tiles rasterised on app cores) with its four wrapped lock entries.
WRAPS="-Wl,--wrap=__retarget_lock_acquire -Wl,--wrap=__retarget_lock_acquire_recursive -Wl,--wrap=sem_wait -Wl,--wrap=pthread_mutex_lock -Wl,--wrap=pread -Wl,--wrap=pwrite"
GPC="$BUILD/wk2test-gpucomp.o $BUILD/wk2test-onyxsurface.o $BUILD/wk2test-onyxcores.o $WRAPS"
aarch64-onyx-elf-gcc -specs=$S/lib/onyx.specs -O2 -mcpu=cortex-a72 \
	-I"$ONYX/user" -I"$ONYX/user/Kits" -I"$ONYX/user/libc/posix" -I"$ONYX/kernel/include" -c "$HERE/onyxcores.c" -o "$BUILD/wk2test-onyxcores.o"
aarch64-onyx-elf-gcc -specs=$S/lib/onyx.specs -O3 -mcpu=cortex-a72 -ffp-contract=off -fno-math-errno \
	-I"$ONYX/user" -I"$ONYX/user/Kits" -I"$ONYX/kernel/include" -c "$ONYX/user/gpucomp/gpucomp.c" -o "$BUILD/wk2test-gpucomp.o"
aarch64-onyx-elf-gcc -specs=$S/lib/onyx.specs -O2 -mcpu=cortex-a72 \
	-I"$ONYX/user" -I"$ONYX/user/Kits" -I"$ONYX/kernel/include" -c "$HERE/onyxsurface.c" -o "$BUILD/wk2test-onyxsurface.o"
echo "wk2test: link"
aarch64-onyx-elf-g++ -mcpu=cortex-a72 -specs="$S/lib/onyx.specs" -L"$S/lib" -Wl,--gc-sections \
	"$BUILD/wk2test.o" $GPC $WK $LIBS -o "$BUILD/bin/wk2test"
aarch64-onyx-elf-size "$BUILD/bin/wk2test"

if [ "${BENCH:-0}" = 1 ]; then
	echo "wk2test: relink for the bench"
	# (gpucomp without its clock: qemu-user gives a program no physical counter)
	aarch64-onyx-elf-gcc -specs=$S/lib/onyx.specs -O3 -mcpu=cortex-a72 -ffp-contract=off -fno-math-errno -DGPC_NO_CLOCK \
		-I"$ONYX/user" -I"$ONYX/user/Kits" -I"$ONYX/kernel/include" -c "$ONYX/user/gpucomp/gpucomp.c" -o "$BUILD/wk2test-gpucomp-bench.o"
	GPC="$BUILD/wk2test-gpucomp-bench.o $BUILD/wk2test-onyxsurface.o $BUILD/wk2test-onyxcores.o"
	if [ "${UMM_NEW:-0}" = 1 ]; then
		printf '#include "appkit/appkit.h"\n#include "onyxpp.hpp"\n' > "$BUILD/wk2test-ummnew.cpp"
		aarch64-onyx-elf-g++ -specs=$S/lib/onyx.specs -std=gnu++17 -O2 -mcpu=cortex-a72 -fno-exceptions -fno-rtti -w \
			-I"$ONYX/user" -I"$ONYX/user/Kits" -I"$ONYX/kernel/include" -c "$BUILD/wk2test-ummnew.cpp" -o "$BUILD/wk2test-ummnew.o"
		GPC="$BUILD/wk2test-ummnew.o $GPC"
		echo "wk2test: the bench's program with onyxpp.hpp's operator new (UMM_NEW=1: a build that must fail)"
	fi
	BUILD_ONLY=1 PROG=none NAME=wk2test SIM_CXX=1 PREFIX=aarch64-onyx-elf- CFLAGS_EXTRA="-Wl,--gc-sections $WRAPS" \
		OBJS="$BUILD/wk2test.o $GPC $WK $LIBS" sh "$ONYX/tools/tests/posixsim/run.sh"
	ls -l "$POSIXSIM_ROOT/SD/bin/wk2test"
fi
