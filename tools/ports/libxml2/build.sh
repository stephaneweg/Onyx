#!/bin/sh
# build.sh -- libxml2 for Onyx (third_party/libxml2-2.13.8, MIT): libxml2.a + its headers into the
# POSIX sysroot, and xmllint (out/ports/bin/xmllint.elf). CMake with tools/onyx-toolchain.cmake.
#
# Threads on (pthreads), zlib on (in-tree zlib), no iconv (newlib's is configured out: UTF-8,
# UTF-16, ISO-8859-x and ASCII are libxml2's own), no HTTP / FTP, no dynamic modules, no Python.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (this script).
. "$(dirname "$0")/../common.sh"

SRC=$TP/libxml2-2.13.8
B=$PORTS_OUT/build/libxml2
mkdir -p "$B"
onyx_install_deps

echo "libxml2: configure (CMake, the Onyx toolchain file)"
onyx_cmake -S "$SRC" -B "$B" -DBUILD_SHARED_LIBS=OFF \
	-DLIBXML2_WITH_ICONV=OFF -DLIBXML2_WITH_ICU=OFF -DLIBXML2_WITH_HTTP=OFF -DLIBXML2_WITH_FTP=OFF \
	-DLIBXML2_WITH_MODULES=OFF -DLIBXML2_WITH_PYTHON=OFF -DLIBXML2_WITH_LZMA=OFF \
	-DLIBXML2_WITH_ZLIB=ON -DZLIB_INCLUDE_DIR="$ONYX_SYSROOT/include" -DZLIB_LIBRARY="$ONYX_SYSROOT/lib/libz.a" \
	-DLIBXML2_WITH_THREADS=ON -DLIBXML2_WITH_TESTS=OFF -DLIBXML2_WITH_PROGRAMS=ON \
	-DLIBXML2_WITH_CATALOG=ON -DCMAKE_INSTALL_PREFIX="$ONYX_SYSROOT" >"$B/configure.log" || { tail -30 "$B/configure.log"; exit 1; }
echo "libxml2: build"
cmake --build "$B" -j "$JOBS" >"$B/build.log" || { grep -B2 -A8 "error" "$B/build.log" | head -60; exit 1; }
cmake --install "$B" >"$B/install.log" 2>&1 || { tail -20 "$B/install.log"; exit 1; }

onyx_tool_done "$B/xmllint" xmllint
