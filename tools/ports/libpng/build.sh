#!/bin/sh
# build.sh -- libpng 1.6.44 for the POSIX ports (third_party/libpng-1.6.44, the apps' copy; the libpng
# licence): libpng16.a + include/libpng16 + libpng16.pc into the sysroot, for FreeType (colour bitmap
# glyphs), Skia's PNG codec and encoder, and later WebKit's PNG decoder. libpng's CMake, with the NEON
# filters (arm/), no tools, no tests, no shared library.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (this script).
. "$(dirname "$0")/../common.sh"

SRC=$TP/libpng-1.6.44
B=$PORTS_OUT/build/libpng
mkdir -p "$B"
onyx_install_deps

echo "libpng: configure (CMake, the Onyx toolchain file)"
onyx_cmake -S "$SRC" -B "$B" -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF \
	-DPNG_FRAMEWORK=OFF -DPNG_HARDWARE_OPTIMIZATIONS=ON -DPNG_ARM_NEON=on -DSKIP_INSTALL_EXECUTABLES=ON \
	-DSKIP_INSTALL_PROGRAMS=ON -DSKIP_INSTALL_EXPORT=ON -DSKIP_INSTALL_FILES=OFF \
	-DZLIB_INCLUDE_DIR="$ONYX_SYSROOT/include" -DZLIB_LIBRARY="$ONYX_SYSROOT/lib/libz.a" \
	-DCMAKE_INSTALL_PREFIX="$ONYX_SYSROOT" >"$B/configure.log" || { tail -30 "$B/configure.log"; exit 1; }
echo "libpng: build"
cmake --build "$B" -j "$JOBS" >"$B/build.log" || { grep -B2 -A8 "error" "$B/build.log" | head -60; exit 1; }
cmake --install "$B" >"$B/install.log" 2>&1 || { tail -20 "$B/install.log"; exit 1; }
ls -l "$ONYX_SYSROOT/lib/libpng16.a" | awk '{print "libpng: " $5 "  " $NF}'
