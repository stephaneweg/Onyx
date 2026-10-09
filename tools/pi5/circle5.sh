#!/bin/sh
# tools/pi5/circle5.sh -- Circle built for the Raspberry Pi 5 (docs/PI5-PORT.md §4.4).
#
# Circle's libraries are per board (./configure -r 4 | 5), built in its own tree: the Pi 5's is
# circle5/, a copy of circle/'s sources (the fork, the submodule -- the one place a Circle patch is
# made) beside it, not in git. Run again after a change in circle/: the copy is refreshed (the
# times kept, so make rebuilds what changed) and the libraries rebuilt.
#
#   sh tools/pi5/circle5.sh [--clean]          then: make -C kernel BOARD=pi5
#
# MIT licence (docs/LICENSING.md).

set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
[ -e circle/Rules.mk ] && [ -e circle/addon/wlan/hostap/wpa_supplicant/Makefile.circle ] || git submodule update --init --recursive
[ "$1" = "--clean" ] && rm -rf circle5
mkdir -p circle5
(cd circle && tar cf - --exclude=.git --exclude='*.o' --exclude='*.a' --exclude='*.d' --exclude='*.img' \
	--exclude='*.elf' --exclude='*.map' --exclude='*.lst' --exclude=Config.mk .) | (cd circle5 && tar xf -)
cd circle5
# DEPTH=32: Onyx draws 32-bit pixels (Circle's default is 16)
[ -e Config.mk ] || ./configure -r 5 -p aarch64-none-elf- -d DEPTH=32 -f
JOBS=$(nproc 2>/dev/null || echo 4)
for d in lib lib/sched lib/fs lib/fs/fat lib/usb lib/input lib/net lib/sound addon/SDCard addon/fatfs addon/wlan; do
	make -C $d -j"$JOBS" >/dev/null || { echo "circle5: $d failed"; exit 1; }
done
make -C addon/wlan/hostap/wpa_supplicant -f Makefile.circle -j"$JOBS" >/dev/null || { echo "circle5: wpa_supplicant failed"; exit 1; }
echo "circle5: Circle built for the Pi 5 (RASPPI=5) in circle5/"
