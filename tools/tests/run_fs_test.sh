#!/bin/sh
# run_fs_test.sh -- the Circle fork's FatFs with Onyx's changes (addon/fatfs: the sector
# cache in diskio.cpp, the multi-cluster reads / writes in ff.c), on the PC: a RAM disk,
# random file operations checked against a model. For each seed and cluster size (512 B,
# 1 KB): with the cache on and off, and with upstream's ff.c:
# all pass and leave the SAME disk image, byte for byte.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
T=${TMPDIR:-/tmp}/onyx_fstest
F=$ROOT/circle/addon/fatfs
rm -rf "$T" && mkdir -p "$T/up"
cp "$F/ff.c" "$F/ff.h" "$F/ffunicode.c" "$F/diskio.h" "$F/diskio.cpp" "$T/"
# upstream's ff.c: the last version not committed by the fork's owner (the Onyx changes)
UP=$(cd "$ROOT/circle" && git log --format='%H %an' -- addon/fatfs/ff.c | grep -v ' stephaneweg$' | head -1 | cut -d' ' -f1)
( cd "$ROOT/circle" && git show "$UP:addon/fatfs/ff.c" ) > "$T/up/ff.c"
# the fork's configuration, with f_mkfs (to format the RAM disk) and without the OS locks
sed -e 's/^#define FF_USE_MKFS[[:space:]]*0/#define FF_USE_MKFS 1/' \
    -e 's/^#define FF_FS_REENTRANT[[:space:]]*1/#define FF_FS_REENTRANT 0/' "$F/ffconf.h" > "$T/ffconf.h"
gcc -O2 -c -I"$T" "$T/ff.c" -o "$T/ff.o"
gcc -O2 -c -I"$T" "$T/up/ff.c" -o "$T/ff_up.o"
gcc -O2 -c -I"$T" "$T/ffunicode.c" -o "$T/ffunicode.o"
g++ -O2 -std=c++17 -c -I"$T" -I"$HERE/fs/stub" "$T/diskio.cpp" -o "$T/diskio.o"
g++ -O2 -std=c++17 -I"$T" -I"$HERE/fs/stub" "$HERE/fs/fstest.cpp" "$T/ff.o" "$T/ffunicode.o" "$T/diskio.o" -o "$T/fstest"
g++ -O2 -std=c++17 -I"$T" -I"$HERE/fs/stub" "$HERE/fs/fstest.cpp" "$T/ff_up.o" "$T/ffunicode.o" "$T/diskio.o" -o "$T/fstest_up"
fail=0
for cl in 512 1024; do
	for seed in 1 2 3; do
		"$T/fstest" $seed 4000 1 "$T/on.img" $cl || fail=1
		"$T/fstest" $seed 4000 0 "$T/off.img" $cl || fail=1
		"$T/fstest_up" $seed 4000 0 "$T/up.img" $cl || fail=1
		if cmp -s "$T/on.img" "$T/off.img" && cmp -s "$T/on.img" "$T/up.img"; then
			echo "ok   clusters $cl, seed $seed: same image (cache on / off, upstream ff.c)"
		else echo "FAIL clusters $cl, seed $seed: the images differ"; fail=1; fi
	done
done
exit $fail
