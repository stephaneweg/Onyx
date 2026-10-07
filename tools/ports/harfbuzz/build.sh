#!/bin/sh
# build.sh -- HarfBuzz 14.5.1 for Onyx (third_party/harfbuzz-14.5.1, "Old MIT"): libharfbuzz.a (with the
# FreeType font functions, hb-ft) and libharfbuzz-icu.a (ICU's Unicode functions, hb-icu: what WebKit's
# find_package(HarfBuzz COMPONENTS ICU) wants) + include/harfbuzz + harfbuzz.pc / harfbuzz-icu.pc into the
# sysroot, and the smoke program hbtest (out/ports-onyx/bin/hbtest.elf). HarfBuzz's CMake with
# tools/onyx-toolchain.cmake; FreeType and ICU from the sysroot (tools/ports/freetype, tools/ports/icu).
# Not built: harfbuzz-subset, -raster, -vector, -gpu, the utilities, cairo / glib / graphite2.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (this script).
. "$(dirname "$0")/../common.sh"

SRC=$TP/harfbuzz-14.5.1
B=$PORTS_OUT/build/harfbuzz
mkdir -p "$B"
onyx_install_deps
[ -f "$ONYX_SYSROOT/lib/libfreetype.a" ] || sh "$ONYX/tools/ports/freetype/build.sh"
[ -f "$ONYX_SYSROOT/lib/libicuuc.a" ] || sh "$ONYX/tools/ports/icu/build.sh"

echo "harfbuzz: configure (CMake, the Onyx toolchain file)"
onyx_cmake -S "$SRC" -B "$B" -DBUILD_SHARED_LIBS=OFF -DHB_HAVE_FREETYPE=ON -DHB_HAVE_ICU=ON \
	-DHB_BUILD_SUBSET=OFF -DHB_BUILD_RASTER=OFF -DHB_BUILD_VECTOR=OFF -DHB_BUILD_GPU=OFF -DHB_BUILD_UTILS=OFF \
	-DHB_HAVE_GLIB=OFF -DHB_HAVE_GOBJECT=OFF -DHB_HAVE_CAIRO=OFF -DHB_HAVE_GRAPHITE2=OFF -DHB_HAVE_INTROSPECTION=OFF \
	-DFREETYPE_INCLUDE_DIRS="$ONYX_SYSROOT/include/freetype2" -DFREETYPE_LIBRARY="$ONYX_SYSROOT/lib/libfreetype.a" \
	-DICU_ROOT="$ONYX_SYSROOT" -DCMAKE_INSTALL_PREFIX="$ONYX_SYSROOT" >"$B/configure.log" || { tail -30 "$B/configure.log"; exit 1; }
echo "harfbuzz: build"
cmake --build "$B" -j "$JOBS" >"$B/build.log" || { grep -B2 -A8 "error" "$B/build.log" | head -60; exit 1; }
cmake --install "$B" >"$B/install.log" 2>&1 || { tail -20 "$B/install.log"; exit 1; }
ls -l "$ONYX_SYSROOT/lib/libharfbuzz.a" "$ONYX_SYSROOT/lib/libharfbuzz-icu.a" | awk '{print "harfbuzz: " $5 "  " $NF}'

# the smoke program (tools/ports/harfbuzz/hbtest.cpp; HarfBuzz's own Unicode functions: no ICU data in it)
echo "harfbuzz: hbtest"
"${ONYX_TOOLCHAIN_PREFIX}g++" $CFLAGS -O2 "$ONYX/tools/ports/harfbuzz/hbtest.cpp" -o "$B/hbtest" \
	-I"$ONYX_SYSROOT/include/harfbuzz" -I"$ONYX_SYSROOT/include/freetype2" $LDFLAGS \
	-lharfbuzz -lfreetype -lpng16 -lbrotlidec -lz -lm
onyx_tool_done "$B/hbtest" hbtest
