#!/bin/sh
# build-webcore.sh -- build WebKit's PAL + WebCore for Onyx (PORT=Onyx with ONYX_WEBCORE; step 2 of
# the WebKit port, docs/08-WEBKIT-PORT.md): Skia on the CPU (no GL, no compositor), curl with
# mbedTLS, against the POSIX sysroot and every port of tools/ports.
#
#   sh tools/webkit/fetch.sh                       # the checkout (SPARSE_EXTRA: WebCore, below)
#   sh tools/ports/build-all.sh                    # the libraries, into the sysroot
#   sh tools/webkit/build-webcore.sh               # -> $BUILD/lib/libWebCore.a
#   KEEP_GOING=1 sh tools/webkit/build-webcore.sh  # ninja -k 0: every error, not the first
#   TARGET=PAL sh tools/webkit/build-webcore.sh    # another target
#
# Variables: WEBKIT_DIR (the checkout, as fetch.sh), BUILD (default <WEBKIT_DIR>-build/webcore),
# ONYX_SYSROOT (default <onyx>/out/sysroot-onyx), JOBS (default nproc; WebCore's unified sources
# take about 1.5 GB a job), CMAKE_EXTRA (more -D...). The checkout must have Source/WebCore:
#   SPARSE_EXTRA="Source/WebCore" sh tools/webkit/fetch.sh
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
: "${CMAKE_EXTRA:=}"
: "${TARGET:=WebCore}"
PATH=/opt/toolchains/aarch64-onyx-elf-14.2/bin:$PATH
export PATH

[ -f "$WEBKIT_DIR/Source/WebCore/PlatformOnyx.cmake" ] || { echo "build-webcore.sh: no Onyx WebCore in $WEBKIT_DIR: SPARSE_EXTRA=\"Source/WebCore\" sh tools/webkit/fetch.sh" >&2; exit 1; }
for l in icuuc skia harfbuzz curl xml2 sqlite3 mbedtls; do
	[ -f "$ONYX_SYSROOT/lib/lib$l.a" ] || { echo "build-webcore.sh: no lib$l.a in $ONYX_SYSROOT: sh tools/ports/build-all.sh" >&2; exit 1; }
done
for t in cmake ninja perl python3 ruby gperf pkg-config; do
	command -v $t >/dev/null 2>&1 || { echo "build-webcore.sh: the host tool $t is missing" >&2; exit 1; }
done

if [ ! -f "$BUILD/build.ninja" ]; then
	mkdir -p "$BUILD"
	cmake -S "$WEBKIT_DIR" -B "$BUILD" -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE="$ONYX/tools/onyx-toolchain.cmake" -DONYX_SYSROOT="$ONYX_SYSROOT" \
		-DPORT=Onyx -DONYX_WEBCORE=ON -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG -g0" -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG -g0" \
		-DDEVELOPER_MODE=OFF $CMAKE_EXTRA
fi
[ "${CONFIGURE_ONLY:-0}" = 1 ] && exit 0
start=$(date +%s)
if [ "${KEEP_GOING:-0}" = 1 ]; then
	ninja -C "$BUILD" -j"$JOBS" -k 0 "$TARGET"
else
	cmake --build "$BUILD" --target "$TARGET" -- -j"$JOBS"
fi
echo "build-webcore.sh: $TARGET built in $(( $(date +%s) - start )) s in $BUILD"
