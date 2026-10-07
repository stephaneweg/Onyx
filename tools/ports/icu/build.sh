#!/bin/sh
# build.sh -- ICU 78.3 for Onyx (third_party/icu-78.3, Unicode-3.0): libicuuc.a, libicui18n.a,
# libicudata.a (the filtered data, 15 MB, linked in) + include/unicode + icu-uc.pc / icu-i18n.pc into the
# POSIX sysroot, and the smoke program icutest (out/ports-onyx/bin/icutest.elf). CMake with
# tools/onyx-toolchain.cmake (tools/ports/icu/CMakeLists.txt: ICU's autoconf has no Onyx host).
#
# The data file (third_party/icu-78.3/source/data/in/icudt78l.dat) is made by gen-data.sh from ICU's
# data sources with the filter tools/ports/icu/data-filter.json (a host build of ICU's tools); this
# script only links it in.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (this script).
. "$(dirname "$0")/../common.sh"

SRC=$TP/icu-78.3/source
B=$PORTS_OUT/build/icu
mkdir -p "$B"

echo "icu: configure (CMake, the Onyx toolchain file)"
onyx_cmake -S "$ONYX/tools/ports/icu" -B "$B" -DICU_SRC="$SRC" -DCMAKE_INSTALL_PREFIX="$ONYX_SYSROOT" \
	>"$B/configure.log" || { tail -30 "$B/configure.log"; exit 1; }
echo "icu: build (about 460 files)"
cmake --build "$B" -j "$JOBS" >"$B/build.log" || { grep -B2 -A8 "error" "$B/build.log" | head -60; exit 1; }
cmake --install "$B" >"$B/install.log" 2>&1 || { tail -20 "$B/install.log"; exit 1; }
ls -l "$ONYX_SYSROOT/lib/libicuuc.a" "$ONYX_SYSROOT/lib/libicui18n.a" "$ONYX_SYSROOT/lib/libicudata.a" | awk '{print "icu: " $5 "  " $NF}'

# the smoke program (tools/ports/icu/icutest.cpp)
echo "icu: icutest"
"${ONYX_TOOLCHAIN_PREFIX}g++" $CFLAGS -O2 -std=gnu++17 -o "$B/icutest" "$ONYX/tools/ports/icu/icutest.cpp" \
	$LDFLAGS -licui18n -licuuc -licudata -lm
onyx_tool_done "$B/icutest" icutest
