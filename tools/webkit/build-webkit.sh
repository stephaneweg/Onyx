#!/bin/sh
# build-webkit.sh -- build WebKit's WebKit2 layer (Source/WebKit) for Onyx as a static library
# (PORT=Onyx with ONYX_WEBCORE and ONYX_WEBKIT; roadmap step 1 of docs/08-WEBKIT-PORT.md): the UI,
# web and network processes' code in one libWebKit.a -- the browser is ONE program whose role is
# chosen at launch (UIProcess/Launcher/onyx/ProcessLauncherOnyx.cpp).
#
#   sh tools/webkit/build-webcore.sh               # first (the same build tree)
#   sh tools/webkit/build-webkit.sh                # -> $BUILD/lib/libWebKit.a
#   KEEP_GOING=1 sh tools/webkit/build-webkit.sh   # ninja -k 0: every error, not the first
#
# The tree is build-webcore.sh's, configured again with ONYX_WEBKIT=ON: that adds a flag to
# cmakeconfig.h (USE_NON_COMPOSITED_DRAWING_AREA), so WTF, JavaScriptCore and WebCore are compiled
# again once. Variables: as build-webcore.sh (WEBKIT_DIR, BUILD, ONYX_SYSROOT, JOBS, CMAKE_EXTRA,
# TARGET -- default WebKit). The checkout must have Source/WebKit:
#   SPARSE_EXTRA="Source/WebCore Source/WebKit" sh tools/webkit/fetch.sh
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see fetch.sh).
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
if [ -z "${WEBKIT_DIR:-}" ]; then
	if [ -d /home/user ]; then WEBKIT_DIR=/home/user/webkit; else WEBKIT_DIR=$HOME/webkit; fi
fi
: "${BUILD:=$WEBKIT_DIR-build/webcore}"
: "${TARGET:=WebKit}"
export WEBKIT_DIR BUILD TARGET

[ -f "$WEBKIT_DIR/Source/WebKit/PlatformOnyx.cmake" ] || { echo "build-webkit.sh: no Onyx WebKit in $WEBKIT_DIR: SPARSE_EXTRA=\"Source/WebCore Source/WebKit\" sh tools/webkit/fetch.sh" >&2; exit 1; }
[ -f "$BUILD/build.ninja" ] || { echo "build-webkit.sh: no build tree in $BUILD: sh tools/webkit/build-webcore.sh first" >&2; exit 1; }

if ! grep -q '^ONYX_WEBKIT:BOOL=ON' "$BUILD/CMakeCache.txt"; then
	PATH=/opt/toolchains/aarch64-onyx-elf-14.2/bin:$PATH cmake -S "$WEBKIT_DIR" -B "$BUILD" -DONYX_WEBKIT=ON ${CMAKE_EXTRA:-}
fi
exec sh "$HERE/build-webcore.sh"
