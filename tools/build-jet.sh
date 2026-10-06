#!/bin/sh
# build-jet.sh -- build Jet Browser (WebKit) from the sources, the way its package is built, and put
# it on the card: sdcard/apps/jet.app/main.
#
#   sh tools/build-jet.sh [--no-jit] [--jobs N]
#
#   --no-jit    JavaScriptCore's interpreter only (the LLInt): a smaller, slower browser
#   --jobs N    parallel compile jobs (default: the cores, capped at one per 2.5 GB of memory:
#               WebCore's unified sources take 1.5-2.5 GB a job; a job killed = fewer jobs)
#
# Everything comes from pinned sources, patched with Onyx's changes (docs/08-WEBKIT-PORT.md):
#   - WebKit: the revision of tools/webkit/revision.sh, fetched from github.com/WebKit/WebKit (a sparse,
#     shallow checkout beside the clone, ~1 GB), then Onyx's patch series tools/webkit/patches/*.patch;
#   - its libraries, vendored in third_party/ (ICU, Skia, HarfBuzz, FreeType, libpng, libjpeg-turbo,
#     libwebp, curl, mbedTLS, SQLite, libxml2; libvpx, dav1d, Opus for the video) and built by
#     tools/ports/build-all.sh against the POSIX layer (user/Runtime/libc/posix);
#   - the toolchain aarch64-onyx-elf (GCC 14.2, POSIX threads): prebuilt, from
#     github.com/stephaneweg/onyx-toolchain (its sources are there too), by tools/toolchain/fetch.sh.
#
# In order: 1. host tools  2. Onyx itself (tools/build-sdcard.sh: the Arm toolchain, Circle, the kits
# Jet links)  3. the aarch64-onyx-elf toolchain  4. the POSIX sysroot  5. the libraries  6. WebKit's
# checkout + patches  7. WebKit (WTF, JavaScriptCore, PAL, WebCore, WebKit2)  8. Jet, staged on the card.
# Each step is skipped or resumed when already done: running it again continues where it stopped.
#
# Expect hours the first time (tried from a fresh clone on 4 cores and 15 GB of memory: 1 h 56 for
# every step, WebKit's 7394 the most; ~45 min for WebKit alone on 16 cores) and ~25 GB of disk. Runs on
# Linux x86_64 or WSL 2 (Ubuntu); under WSL keep the clone on the Linux side, and give WSL memory
# (%UserProfile%\.wslconfig: memory=, swap=). Variables: WEBKIT_DIR (the checkout, default ../webkit
# beside the clone), BUILD (the build tree, default <WEBKIT_DIR>-build/webkit-jit).
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
# granted, free of charge, to any person obtaining a copy of this software and associated documentation
# files (the "Software"), to deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
# Software, and to permit persons to whom the Software is furnished to do so, subject to the following
# conditions: The above copyright notice and this permission notice shall be included in all copies or
# substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED.
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
JIT=1
JOBS=
while [ $# -gt 0 ]; do
    case "$1" in
        --no-jit) JIT=0; shift ;;
        --jobs) JOBS=$2; shift 2 ;;
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "build-jet: unknown option $1 (--help)"; exit 2 ;;
    esac
done
if [ -z "$JOBS" ]; then
    cores=$(nproc 2>/dev/null || echo 4)
    mem_mb=$(awk '/MemTotal/ { print int($2 / 1024) }' /proc/meminfo 2>/dev/null || echo 8192)
    JOBS=$(( mem_mb / 2560 )); [ "$JOBS" -ge 1 ] || JOBS=1
    [ "$JOBS" -le "$cores" ] || JOBS=$cores
fi
: "${WEBKIT_DIR:=$(dirname "$ROOT")/webkit}"
if [ $JIT = 1 ]; then : "${BUILD:=$WEBKIT_DIR-build/webkit-jit}"; else : "${BUILD:=$WEBKIT_DIR-build/webcore}"; fi
export WEBKIT_DIR BUILD JOBS
say() { printf '\n== %s\n' "$*"; }

# 1. Host tools
say "1/8 host tools"
missing=
for t in git make g++ python3 perl ruby gperf unifdef cmake ninja curl xz bison flex pkg-config; do
    command -v $t >/dev/null 2>&1 || missing="$missing $t"
done
if [ -n "$missing" ]; then
    echo "missing:$missing"
    echo "install them (Ubuntu / Debian / WSL):"
    echo "  sudo apt update && sudo apt install -y git build-essential python3 perl ruby gperf unifdef \\"
    echo "       cmake ninja-build curl xz-utils bison flex pkg-config ccache"
    exit 1
fi
echo "ok ($JOBS jobs; WebKit in $WEBKIT_DIR, built in $BUILD)"

# 2. Onyx itself: the Arm toolchain, Circle, the kits (Jet links uikit's and systemkit's import side)
say "2/8 Onyx (tools/build-sdcard.sh)"
TC=arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf
for d in /opt/toolchains/$TC "$HOME/.cache/onyx/$TC"; do
    [ -x "$d/bin/aarch64-none-elf-gcc" ] && PATH="$d/bin:$PATH"
done
if [ -e "$ROOT/user/lib/uikit_stubs.S" ] && [ -e "$ROOT/user/lib/systemkit_stubs.S" ] && command -v aarch64-none-elf-gcc >/dev/null 2>&1; then
    echo "already built"
else
    sh "$ROOT/tools/build-sdcard.sh"
    for d in /opt/toolchains/$TC "$HOME/.cache/onyx/$TC"; do
        [ -x "$d/bin/aarch64-none-elf-gcc" ] && PATH="$d/bin:$PATH"
    done
fi
make -C "$ROOT/user" lib/uikit.imp.a lib/systemkit.imp.a >/dev/null

# 3. The aarch64-onyx-elf toolchain (prebuilt; /opt/toolchains when writable, else ~/.cache/onyx)
say "3/8 toolchain (aarch64-onyx-elf)"
OTC=aarch64-onyx-elf-14.2
for d in /opt/toolchains/$OTC "$HOME/.cache/onyx/$OTC"; do
    [ -x "$d/bin/aarch64-onyx-elf-gcc" ] && PATH="$d/bin:$PATH"
done
if ! command -v aarch64-onyx-elf-gcc >/dev/null 2>&1; then
    if mkdir -p /opt/toolchains 2>/dev/null && [ -w /opt/toolchains ]; then P=/opt/toolchains; else P=$HOME/.cache/onyx; fi
    PREFIX=$P sh "$ROOT/tools/toolchain/fetch.sh"
    PATH="$P/$OTC/bin:$PATH"
fi
export PATH
aarch64-onyx-elf-gcc --version | head -1

# 4. The POSIX sysroot (libonyxposix) for that toolchain: out/sysroot-onyx
say "4/8 POSIX sysroot"
make -C "$ROOT/user/Runtime/libc/posix" install PREFIX=aarch64-onyx-elf- -j"$JOBS" >/dev/null
echo "ok"

# 5. WebKit's libraries, from third_party/, into the sysroot
say "5/8 libraries (ICU, Skia, HarfBuzz, FreeType, images, curl, mbedTLS, SQLite, libxml2)"
S=$ROOT/out/sysroot-onyx/lib
done_all=1
for l in libicuuc libskia libharfbuzz libfreetype libpng16 libjpeg libwebp libcurl libmbedtls libsqlite3 libxml2; do
    [ -e "$S/$l.a" ] || done_all=0
done
if [ $done_all = 1 ]; then echo "already built"; else sh "$ROOT/tools/ports/build-all.sh"; fi

# 6. WebKit at the pinned revision, with Onyx's patches
say "6/8 WebKit's sources (the pinned revision + tools/webkit/patches)"
if [ -f "$WEBKIT_DIR/Source/WebKit/PlatformOnyx.cmake" ]; then
    echo "already fetched ($WEBKIT_DIR)"
else
    TESTS=0 SPARSE_EXTRA="Source/WebCore Source/WebKit" sh "$ROOT/tools/webkit/fetch.sh"
fi

# 7. WebKit: WTF, JavaScriptCore, PAL, WebCore and WebKit2 as static libraries
say "7/8 WebKit (the long one)"
if [ $JIT = 1 ]; then
    CMAKE_EXTRA="-DONYX_WEBKIT=ON -DENABLE_JIT=ON -DENABLE_DFG_JIT=ON -DENABLE_FTL_JIT=OFF"
else
    CMAKE_EXTRA="-DONYX_WEBKIT=ON"
fi
CMAKE_EXTRA=$CMAKE_EXTRA TARGET=WebKit sh "$ROOT/tools/webkit/build-webcore.sh"

# 8. Jet: the window, linked with WebKit, stripped into sdcard/apps/jet.app/main
say "8/8 Jet"
sh "$ROOT/tools/webkit/build-web.sh"

say "done"
echo "Jet is in $ROOT/sdcard/apps/jet.app/: copy that folder to SD:/apps/ on the card"
echo "(or the whole sdcard/ folder), with SD:/lib/uikit.so and SD:/lib/systemkit.so of the same build."
