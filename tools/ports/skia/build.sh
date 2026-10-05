#!/bin/sh
# build.sh -- Skia for Onyx (third_party/skia-m154: WebKit's copy of Skia, milestone 154, BSD-3), the CPU
# raster back end: libskia.a + include/skia + skia.pc into the POSIX sysroot (tools/ports/skia/CMakeLists.txt,
# CMake with tools/onyx-toolchain.cmake), and the smoke programs skiatest (a scene rendered to a PNG, checked)
# and skiademo (the same scene in a window: a visual check on the Pi) in out/ports-onyx/bin.
#
#   SKIA_SRC=<dir>   another Skia tree (e.g. a WebKit checkout's Source/ThirdParty/skia)
#
# Needs FreeType, libpng, libjpeg-turbo, libwebp in the sysroot (built first when missing), and for the smoke
# programs HarfBuzz and ICU (they shape their text with HarfBuzz, as WebKit does).
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (this script).
. "$(dirname "$0")/../common.sh"

: "${SKIA_SRC:=$TP/skia-m154}"
B=$PORTS_OUT/build/skia
mkdir -p "$B"
onyx_install_deps
for dep in libpng:png16 freetype:freetype libjpeg-turbo:jpeg libwebp:webp harfbuzz:harfbuzz icu:icuuc; do
	[ -f "$ONYX_SYSROOT/lib/lib${dep#*:}.a" ] || sh "$ONYX/tools/ports/${dep%%:*}/build.sh"
done

echo "skia: configure (CMake, the Onyx toolchain file; $SKIA_SRC)"
onyx_cmake -S "$ONYX/tools/ports/skia" -B "$B" -DSKIA_SRC="$SKIA_SRC" -DCMAKE_INSTALL_PREFIX="$ONYX_SYSROOT" \
	>"$B/configure.log" || { tail -30 "$B/configure.log"; exit 1; }
echo "skia: build (about 550 files: several minutes)"
cmake --build "$B" -j "$JOBS" >"$B/build.log" || { grep -B2 -A12 "error" "$B/build.log" | head -80; exit 1; }
rm -rf "$ONYX_SYSROOT/include/skia"
cmake --install "$B" >"$B/install.log" 2>&1 || { tail -20 "$B/install.log"; exit 1; }
ls -l "$ONYX_SYSROOT/lib/libskia.a" | awk '{print "skia: " $5 "  " $NF}'

# the smoke programs (tools/ports/skia/skiatest.cpp, skiademo.cpp: scene.h draws the scene; skiatest shapes
# with ICU's Unicode functions as WebKit does, skiademo with HarfBuzz's own: 16 MB of ICU data less)
SKCF="-std=gnu++20 -O2 -Wno-attributes -I$ONYX_SYSROOT/include/skia -I$ONYX_SYSROOT/include/harfbuzz $(PKG_CONFIG_LIBDIR=$ONYX_SYSROOT/lib/pkgconfig pkg-config --cflags-only-other skia)"
SKLIBS="-lskia -lharfbuzz -lfreetype -lpng16 -ljpeg -lwebpmux -lwebpdemux -lwebp -lsharpyuv -lbrotlidec -lz -lm"
ICULIBS="-lharfbuzz-icu -licui18n -licuuc -licudata"
echo "skia: skiatest, skiademo"
"${ONYX_TOOLCHAIN_PREFIX}g++" $CFLAGS $SKCF "$ONYX/tools/ports/skia/skiatest.cpp" -o "$B/skiatest" $LDFLAGS $ICULIBS $SKLIBS
"${ONYX_TOOLCHAIN_PREFIX}g++" $CFLAGS $SKCF -DSCENE_NO_ICU -I"$ONYX/user" -I"$ONYX/user/Kits" -I"$ONYX/kernel/include" "$ONYX/tools/ports/skia/skiademo.cpp" \
	-o "$B/skiademo" $LDFLAGS $SKLIBS
onyx_tool_done "$B/skiatest" skiatest
onyx_tool_done "$B/skiademo" skiademo
