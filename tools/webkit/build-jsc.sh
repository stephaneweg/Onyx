#!/bin/sh
# build-jsc.sh -- build WebKit's WTF + JavaScriptCore + the jsc shell for Onyx (PORT=Onyx; step 1
# of the WebKit port, docs/08-WEBKIT-PORT.md) with the aarch64-onyx-elf toolchain and
# tools/onyx-toolchain.cmake, against the POSIX sysroot (ICU from tools/ports).
#
#   sh tools/webkit/fetch.sh                      # the checkout (once)
#   sh tools/webkit/build-jsc.sh                  # C_LOOP interpreter -> $BUILD/bin/jsc
#   INTERP=llint sh tools/webkit/build-jsc.sh     # the LLInt (offlineasm's ARM64 back end), no JIT
#   sh tools/webkit/build-jsc.sh install          # + strip it into user/bin/jsc.elf (make stage
#                                                 #   copies it to the card as /bin/jsc)
#
# Variables: WEBKIT_DIR (the checkout, as fetch.sh), BUILD (default <WEBKIT_DIR>-build/jsc-<interp>),
# ONYX_SYSROOT (default <onyx>/out/sysroot-onyx), JOBS (default nproc), CMAKE_EXTRA (more -D...).
# Host tools: cmake >= 3.20, ninja, perl, python3, ruby (offlineasm), gperf (apt install gperf, or
# build it from source: a host tool), unifdef (USE_SYSTEM_UNIFDEF). About 70 minutes on 4 cores
# for a first build.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
# hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
# do so, subject to the following conditions: The above copyright notice and this permission
# notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
# IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ONYX=$(cd "$HERE/../.." && pwd)
if [ -z "${WEBKIT_DIR:-}" ]; then
	if [ -d /home/user ]; then WEBKIT_DIR=/home/user/webkit; else WEBKIT_DIR=$HOME/webkit; fi
fi
: "${INTERP:=cloop}"
: "${BUILD:=$WEBKIT_DIR-build/jsc-$INTERP}"
: "${ONYX_SYSROOT:=$ONYX/out/sysroot-onyx}"
: "${JOBS:=$(nproc)}"
: "${CMAKE_EXTRA:=}"
PATH=/opt/toolchains/aarch64-onyx-elf-14.2/bin:$PATH
export PATH

[ -f "$WEBKIT_DIR/Source/cmake/OptionsOnyx.cmake" ] || { echo "build-jsc.sh: no Onyx WebKit checkout at $WEBKIT_DIR: sh tools/webkit/fetch.sh" >&2; exit 1; }
[ -f "$ONYX_SYSROOT/lib/libicuuc.a" ] || { echo "build-jsc.sh: no ICU in $ONYX_SYSROOT: make -C user/libc/posix install PREFIX=aarch64-onyx-elf- && sh tools/ports/build-all.sh webkit" >&2; exit 1; }
for t in cmake ninja perl python3 ruby gperf; do
	command -v $t >/dev/null 2>&1 || { echo "build-jsc.sh: the host tool $t is missing" >&2; exit 1; }
done

case $INTERP in
cloop) INTERP_FLAGS="-DENABLE_C_LOOP=ON";;
llint) INTERP_FLAGS="-DENABLE_C_LOOP=OFF";;
*) echo "build-jsc.sh: INTERP is cloop or llint" >&2; exit 1;;
esac

if [ ! -f "$BUILD/build.ninja" ]; then
	mkdir -p "$BUILD"
	# -O2, no debug information (the size of a static binary; the build tree too)
	cmake -S "$WEBKIT_DIR" -B "$BUILD" -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE="$ONYX/tools/onyx-toolchain.cmake" -DONYX_SYSROOT="$ONYX_SYSROOT" \
		-DPORT=Onyx -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG -g0" -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG -g0" \
		-DENABLE_JIT=OFF -DENABLE_WEBASSEMBLY=OFF -DENABLE_SAMPLING_PROFILER=OFF \
		-DUSE_SYSTEM_MALLOC=ON -DENABLE_REMOTE_INSPECTOR=OFF -DDEVELOPER_MODE=OFF \
		$INTERP_FLAGS $CMAKE_EXTRA
fi
start=$(date +%s)
cmake --build "$BUILD" --target jsc -- -j"$JOBS"
echo "build-jsc.sh: jsc built in $(( $(date +%s) - start )) s: $BUILD/bin/jsc"

if [ "${1:-}" = install ]; then
	mkdir -p "$ONYX/user/bin"
	aarch64-onyx-elf-strip -o "$ONYX/user/bin/jsc.elf" "$BUILD/bin/jsc"
	ls -l "$ONYX/user/bin/jsc.elf"
fi
