#!/bin/sh
# build.sh -- libwebp 1.4.0 for the POSIX ports (third_party/libwebp-1.4.0, the apps' copy; BSD-3 + the
# patent grant, COPYING / PATENTS): libwebp.a, libwebpdemux.a, libwebpmux.a, libsharpyuv.a + include/webp
# + their .pc files into the sysroot, for Skia's WebP codec / encoder and later WebKit's WebP decoder
# (WebKit wants WebP with demux). libwebp's CMake with the NEON code (WEBP_ENABLE_SIMD), threads; no
# tools, no examples, no extras, no shared library. Its CMake adds -pthread, which aarch64-onyx-elf's GCC
# does not take: tools/ports/drop-pthread-flag.sh as the compiler launcher.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (this script).
. "$(dirname "$0")/../common.sh"

SRC=$TP/libwebp-1.4.0
B=$PORTS_OUT/build/libwebp
mkdir -p "$B"

echo "libwebp: configure (CMake, the Onyx toolchain file)"
onyx_cmake -S "$SRC" -B "$B" -DCMAKE_C_COMPILER_LAUNCHER="sh;$ONYX/tools/ports/drop-pthread-flag.sh" -DBUILD_SHARED_LIBS=OFF -DWEBP_ENABLE_SIMD=ON -DWEBP_USE_THREAD=ON \
	-DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF \
	-DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF \
	-DWEBP_BUILD_EXTRAS=OFF -DWEBP_BUILD_WEBP_JS=OFF -DWEBP_BUILD_LIBWEBPMUX=ON \
	-DWEBP_NEAR_LOSSLESS=ON -DCMAKE_INSTALL_PREFIX="$ONYX_SYSROOT" -DCMAKE_INSTALL_LIBDIR=lib \
	-DCMAKE_INSTALL_INCLUDEDIR=include >"$B/configure.log" || { tail -30 "$B/configure.log"; exit 1; }
echo "libwebp: build"
cmake --build "$B" -j "$JOBS" >"$B/build.log" || { grep -B2 -A8 "error" "$B/build.log" | head -60; exit 1; }
cmake --install "$B" >"$B/install.log" 2>&1 || { tail -20 "$B/install.log"; exit 1; }
ls -l "$ONYX_SYSROOT/lib/libwebp.a" "$ONYX_SYSROOT/lib/libwebpdemux.a" "$ONYX_SYSROOT/lib/libwebpmux.a" \
	"$ONYX_SYSROOT/lib/libsharpyuv.a" | awk '{print "libwebp: " $5 "  " $NF}'
