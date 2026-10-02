#!/bin/sh
# build.sh -- libjpeg-turbo 3.1.4 for the POSIX ports (third_party/libjpeg-turbo-3.1.4; the IJG licence +
# the Modified BSD licence + zlib's, LICENSE.md): libjpeg.a (the libjpeg API, version 6b ABI, with
# libjpeg-turbo's extensions -- jpeg_skip_scanlines, jpeg_crop_scanline, JCS_EXT_*: what Skia's JPEG codec
# and WebKit's JPEG decoder use; the in-tree jpeg-9f has none of them) + its headers + libjpeg.pc into the
# sysroot. libjpeg-turbo's CMake, with the NEON SIMD (simd/arm, intrinsics: GCC 14 builds them); no
# TurboJPEG API, no tools, no tests, no shared library.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (this script).
. "$(dirname "$0")/../common.sh"

SRC=$TP/libjpeg-turbo-3.1.4
B=$PORTS_OUT/build/libjpeg-turbo
mkdir -p "$B"

echo "libjpeg-turbo: configure (CMake, the Onyx toolchain file)"
onyx_cmake -S "$SRC" -B "$B" -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF \
	-DWITH_TESTS=OFF -DWITH_JAVA=OFF -DWITH_FUZZ=OFF -DWITH_SIMD=ON -DREQUIRE_SIMD=ON -DWITH_ARITH_DEC=ON \
	-DWITH_ARITH_ENC=ON -DCMAKE_INSTALL_PREFIX="$ONYX_SYSROOT" -DCMAKE_INSTALL_LIBDIR=lib \
	-DCMAKE_INSTALL_INCLUDEDIR=include \
	>"$B/configure.log" || { tail -30 "$B/configure.log"; exit 1; }
echo "libjpeg-turbo: build"
cmake --build "$B" -j "$JOBS" >"$B/build.log" || { grep -B2 -A8 "error" "$B/build.log" | head -60; exit 1; }
# the library and its headers (the "doc" component wants doc/, not vendored)
for c in lib include; do
	cmake --install "$B" --component $c >>"$B/install.log" 2>&1 || { tail -20 "$B/install.log"; exit 1; }
done
ls -l "$ONYX_SYSROOT/lib/libjpeg.a" | awk '{print "libjpeg-turbo: " $5 "  " $NF}'
