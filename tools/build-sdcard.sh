#!/bin/sh
# build-sdcard.sh -- build Onyx from a fresh clone and stage a bootable card in sdcard/.
#
#   sh tools/build-sdcard.sh [--card <dir>] [--clean]
#
#   --card <dir>   then copy the whole card to <dir>: the root of a FAT32 SD card
#                  (Linux: /media/<you>/<card>; WSL: /mnt/e after `sudo mount -t drvfs E: /mnt/e`)
#   --clean        rebuild Circle's libraries even when they are already built
#
# What it does, in order (docs/03-DEVELOPER-GUIDE.md §1-§4):
#   1. checks the host tools (git, make, g++, python3, curl, xz) and says what to install;
#   2. finds the AArch64 toolchain (Arm GNU Toolchain 14.2.rel1, aarch64-none-elf): on the PATH,
#      in /opt/toolchains, or in ~/.cache/onyx -- else downloads it there (~100 MB, no sudo);
#   3. fetches Circle (the git submodule) and builds its libraries for the Pi 4 (once);
#   4. builds the kernel, the kits, the apps and the /bin tools (make in kernel/);
#   5. stages them into sdcard/ (make stage), next to the firmware and the files already there.
#
# Runs on Linux x86_64 or WSL 2 (Ubuntu). Keep the clone on the Linux side under WSL (~/, not
# /mnt/c: ten times slower). Jet Browser (WebKit) is not built here -- its build takes hours
# (docs/08-WEBKIT-PORT.md) -- and is not on the card: install the package `jet` on the Pi, from the
# Package Manager (the Control Panel, the gear on the dock).
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
CARD=
CLEAN=0
while [ $# -gt 0 ]; do
    case "$1" in
        --card) CARD=$2; shift 2 ;;
        --clean) CLEAN=1; shift ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "build-sdcard: unknown option $1 (--help)"; exit 2 ;;
    esac
done
JOBS=$(nproc 2>/dev/null || echo 4)
say() { printf '\n== %s\n' "$*"; }

# 1. Host tools
say "1/5 host tools"
missing=
for t in git make g++ python3 curl xz; do
    command -v $t >/dev/null 2>&1 || missing="$missing $t"
done
if [ -n "$missing" ]; then
    echo "missing:$missing"
    echo "install them (Ubuntu / Debian / WSL):"
    echo "  sudo apt update && sudo apt install -y git build-essential python3 curl xz-utils"
    exit 1
fi
echo "ok"

# 2. The AArch64 bare-metal toolchain
say "2/5 toolchain (aarch64-none-elf, Arm GNU Toolchain 14.2.rel1)"
TC=arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf
if ! command -v aarch64-none-elf-gcc >/dev/null 2>&1; then
    for d in /opt/toolchains/$TC "$HOME/.cache/onyx/$TC"; do
        [ -x "$d/bin/aarch64-none-elf-gcc" ] && PATH="$d/bin:$PATH" && break
    done
fi
if ! command -v aarch64-none-elf-gcc >/dev/null 2>&1; then
    case "$(uname -m)" in x86_64) ;; *) echo "no prebuilt toolchain for $(uname -m): install $TC by hand"; exit 1 ;; esac
    mkdir -p "$HOME/.cache/onyx"
    echo "downloading $TC (~100 MB) into ~/.cache/onyx"
    curl -fL --retry 3 -o "$HOME/.cache/onyx/$TC.tar.xz" \
        "https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/$TC.tar.xz"
    tar -xJf "$HOME/.cache/onyx/$TC.tar.xz" -C "$HOME/.cache/onyx"
    rm -f "$HOME/.cache/onyx/$TC.tar.xz"
    PATH="$HOME/.cache/onyx/$TC/bin:$PATH"
fi
export PATH
aarch64-none-elf-gcc --version | head -1

# 3. Circle, the hardware layer (a git submodule: the fork stephaneweg/circle, branch onyx)
say "3/5 Circle"
cd "$ROOT"
[ -e circle/Rules.mk ] || git submodule update --init --recursive
if [ $CLEAN = 1 ] || [ ! -e circle/lib/libcircle.a ] || [ ! -e circle/addon/wlan/hostap/wpa_supplicant/libwpa_supplicant.a ]; then
    # DEPTH=32 is required: Onyx draws 32-bit pixels (Circle's default is 16)
    (cd circle && ./configure -r 4 -p aarch64-none-elf- -d DEPTH=32 -f)
    for d in lib lib/sched lib/fs lib/fs/fat lib/usb lib/input lib/net lib/sound \
             addon/SDCard addon/fatfs addon/wlan; do
        (cd "circle/$d" && make clean >/dev/null && make -j"$JOBS") || exit 1
    done
    (cd circle/addon/wlan/hostap/wpa_supplicant && make -f Makefile.circle clean >/dev/null && make -f Makefile.circle -j"$JOBS")
else
    echo "already built (--clean to rebuild)"
fi

# 4. The kernel, the kits, the apps, the /bin tools
say "4/5 kernel and programs"
make -C kernel -j"$JOBS"

# 5. The card
say "5/5 staging into sdcard/"
make -C kernel stage
if [ -n "$CARD" ]; then
    [ -d "$CARD" ] || { echo "no such directory: $CARD"; exit 1; }
    echo "copying sdcard/ to $CARD"
    cp -r "$ROOT/sdcard/." "$CARD/"
    sync
fi

say "done"
echo "The card is in $ROOT/sdcard/: copy everything in it to the root of a FAT32 SD card"
echo "(or run again with --card <dir>), put it in a Raspberry Pi 4 or a Pi 400, and power on."
echo "Jet Browser is not on the card: install it from the Package Manager (the Control Panel, the gear on the dock)."
