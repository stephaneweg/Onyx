#!/bin/sh
# run_fs_test.sh -- the FatFs of the Circle fork with Onyx's sector cache (addon/fatfs/diskio.cpp),
# on the PC: a RAM disk, random file operations checked against a model, with the cache on
# and off; both must pass and leave the same disk image, byte for byte.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
T=${TMPDIR:-/tmp}/onyx_fstest
F=$ROOT/circle/addon/fatfs
rm -rf "$T" && mkdir -p "$T"
cp "$F/ff.c" "$F/ff.h" "$F/ffunicode.c" "$F/diskio.h" "$F/diskio.cpp" "$T/"
# the fork's configuration, with f_mkfs (to format the RAM disk) and without the OS locks
sed -e 's/^#define FF_USE_MKFS[[:space:]]*0/#define FF_USE_MKFS 1/' \
    -e 's/^#define FF_FS_REENTRANT[[:space:]]*1/#define FF_FS_REENTRANT 0/' "$F/ffconf.h" > "$T/ffconf.h"
gcc -O2 -c -I"$T" "$T/ff.c" -o "$T/ff.o"
gcc -O2 -c -I"$T" "$T/ffunicode.c" -o "$T/ffunicode.o"
g++ -O2 -std=c++17 -c -I"$T" -I"$HERE/fs/stub" "$T/diskio.cpp" -o "$T/diskio.o"
g++ -O2 -std=c++17 -I"$T" -I"$HERE/fs/stub" "$HERE/fs/fstest.cpp" "$T/ff.o" "$T/ffunicode.o" "$T/diskio.o" -o "$T/fstest"
fail=0
for seed in 1 2 3; do
	"$T/fstest" $seed 4000 1 "$T/on.img" || fail=1
	"$T/fstest" $seed 4000 0 "$T/off.img" || fail=1
	cmp -s "$T/on.img" "$T/off.img" && echo "ok   seed $seed: same image with and without the cache" || { echo "FAIL seed $seed: the images differ"; fail=1; }
done
exit $fail
