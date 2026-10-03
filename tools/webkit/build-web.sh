#!/bin/sh
# build-web.sh -- build Web, the WebKit browser (user/Apps/web: its window on wtk, engine_webkit.cpp on
# WebKit2's C API), against the build of build-webkit.sh, with the POSIX toolchain (aarch64-onyx-elf):
# ONE program that is the window and, started again by WebKit, its web and network processes.
#
#   sh tools/webkit/build-webkit.sh             # libWebKit.a first
#   sh tools/webkit/build-web.sh                # -> $BUILD/bin/web, stripped into sdcard/apps/web.app/main
#
# wtk is compiled again for this toolchain (a hosted C++ program here: libstdc++'s operator new, not
# onyxpp.hpp's). engine_webkit.cpp is compiled with the command one of WebKit's own sources gets (its
# forwarding headers, its config.h). Variables: as build-wctest.sh; STAGE=0 leaves sdcard/ alone.
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
: "${JOBS:=$(nproc)}"
PATH=/opt/toolchains/aarch64-onyx-elf-14.2/bin:$PATH
export PATH
S=$ONYX_SYSROOT
APP=$ONYX/user/Apps/web
O=$BUILD/web
mkdir -p "$O/wtk" "$BUILD/bin"

[ -f "$BUILD/lib/libWebKit.a" ] || { echo "build-web.sh: no $BUILD/lib/libWebKit.a: sh tools/webkit/build-webkit.sh" >&2; exit 1; }

CXX="aarch64-onyx-elf-g++ -specs=$S/lib/onyx.specs -std=gnu++17 -O2 -mcpu=cortex-a72 -fno-exceptions -fno-rtti -w \
	-ffunction-sections -fdata-sections -I$ONYX/user -I$ONYX/kernel/include"

# ---- wtk, for this toolchain ----
echo "web: wtk"
for f in "$ONYX"/user/wtk/*.cpp; do
	o="$O/wtk/$(basename "$f" .cpp).o"
	if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then echo "$CXX -c $f -o $o"; fi
done > "$O/wtk.jobs"
[ -s "$O/wtk.jobs" ] && xargs -P "$JOBS" -I{} sh -c '{}' < "$O/wtk.jobs"
rm -f "$O/libwtk.a"; aarch64-onyx-elf-ar rcs "$O/libwtk.a" "$O"/wtk/*.o

# ---- the window ----
echo "web: the window"
WIN=""
for f in "$APP"/*.cpp; do				# (all but the engines: main, downloads, ...)
	case $(basename "$f") in engine_*) continue ;; esac
	o="$O/$(basename "$f" .cpp).o"
	$CXX -c "$f" -o "$o"
	WIN="$WIN $o"
done

# ---- the engine: the command of one of WebKit's own sources, the file swapped ----
ref=UIProcess/API/C/onyx/WKRunLoop.cpp
cmd=$(cd "$BUILD" && ninja -t commands lib/libWebKit.a | grep -F "$ref" | grep -F -- " -c " | head -n 1)
[ -n "$cmd" ] || { echo "build-web.sh: no compile command for $ref in $BUILD" >&2; exit 1; }
cmd=$(printf '%s' "$cmd" | sed -E 's/ -MD -MT [^ ]+ -MF [^ ]+//; s| -o [^ ]+| -o web/engine_webkit.o|; s| -c [^ ]+| -c '"$APP"'/engine_webkit.cpp|')
echo "web: the engine"
( cd "$BUILD" && sh -c "$cmd" )

# ---- link: the window, wtk, WebKit, the sysroot's libraries ----
WK="-Wl,--start-group $BUILD/lib/libWebKit.a $BUILD/lib/libWebCore.a $BUILD/lib/libPAL.a $BUILD/lib/libJavaScriptCore.a $BUILD/lib/libWTF.a $BUILD/lib/libbmalloc.a -Wl,--end-group"
LIBS=""
for l in skia harfbuzz-icu harfbuzz freetype png16 jpeg webpmux webpdemux webp sharpyuv curl mbedtls mbedx509 mbedcrypto \
	nghttp2 brotlidec xml2 sqlite3 z icui18n icuuc icudata; do
	LIBS="$LIBS $S/lib/lib$l.a"
done
# Skia keeps to the sizes it asks for (skmallocsize.c; SKMALLOCSIZE=0: Skia's own sk_malloc_size, newlib's
# malloc_usable_size -- which /bin/malloctest shows to be right: what crashed Web on the Pi with it is
# not known yet, docs/08-WEBKIT-PORT.md "Step 3")
aarch64-onyx-elf-gcc -specs=$S/lib/onyx.specs -O2 -mcpu=cortex-a72 -c "$HERE/skmallocsize.c" -o "$O/skmallocsize.o"
SKMS="$O/skmallocsize.o -Wl,--wrap=_Z14sk_malloc_sizePvm"
[ "${SKMALLOCSIZE:-1}" = 1 ] || SKMS=""
# The compositor's two C files (WebKit's USE(GRAPHICS_LAYER_ONYX) calls them; they call the kernel through
# kapi.h): the GPU compositing service, built as user/Makefile builds it, and the kernel surfaces.
GPC="$O/gpucomp.o $O/onyxsurface.o"
aarch64-onyx-elf-gcc -specs=$S/lib/onyx.specs -O3 -mcpu=cortex-a72 -ffp-contract=off -fno-math-errno \
	-I"$ONYX/user" -I"$ONYX/kernel/include" -c "$ONYX/user/gpucomp/gpucomp.c" -o "$O/gpucomp.o"
aarch64-onyx-elf-gcc -specs=$S/lib/onyx.specs -O2 -mcpu=cortex-a72 \
	-I"$ONYX/user" -I"$ONYX/kernel/include" -c "$HERE/onyxsurface.c" -o "$O/onyxsurface.o"
# HEAPCHECK=1: the checking malloc of heapcheck.c in front of newlib's (a hunt for heap corruption);
# HEAPCHECK=2: the same with blocks as large as newlib's and a malloc_usable_size that says so, Skia
# filling them (no skmallocsize.o): what Skia does with the slack, under the canaries
HC=""
if [ "${HEAPCHECK:-0}" != 0 ]; then
	HCD=""
	if [ "$HEAPCHECK" = 2 ]; then HCD=-DHEAPCHECK_SLACK; SKMS=""; fi
	aarch64-onyx-elf-gcc -specs=$S/lib/onyx.specs -O2 -mcpu=cortex-a72 -fno-omit-frame-pointer $HCD -c "$HERE/heapcheck.c" -o "$O/heapcheck.o"
	HC="$O/heapcheck.o"
	for f in malloc free realloc calloc memalign aligned_alloc posix_memalign malloc_usable_size; do HC="$HC -Wl,--wrap=$f"; done
	echo "web: with heapcheck ($HEAPCHECK)"
fi
echo "web: link"
aarch64-onyx-elf-g++ -mcpu=cortex-a72 -specs="$S/lib/onyx.specs" -L"$S/lib" -Wl,--gc-sections \
	$WIN "$O/engine_webkit.o" "$O/libwtk.a" $SKMS $HC $GPC $WK $LIBS -o "$BUILD/bin/web"
aarch64-onyx-elf-size "$BUILD/bin/web"

if [ "${STAGE:-1}" = 1 ]; then
	mkdir -p "$ONYX/sdcard/apps/web.app"
	aarch64-onyx-elf-strip -o "$ONYX/sdcard/apps/web.app/main" "$BUILD/bin/web"
	cp "$APP/app.txt" "$APP/start.html" "$ONYX/sdcard/apps/web.app/"
	ls -l "$ONYX/sdcard/apps/web.app/main"
fi
