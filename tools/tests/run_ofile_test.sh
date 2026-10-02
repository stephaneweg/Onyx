#!/bin/sh
# run_ofile_test.sh -- the kernel's open files of kapi v75 (kernel/sys/ofile.cpp: file_*, path_*,
# dir_read) on the PC: the real ofile.cpp over the Circle fork's FatFs on a RAM disk (SD: FAT32,
# SD1: exFAT) and the real RAM: (ramfs.cpp), the kernel around them stubbed (tools/tests/ofile).
# docs/02 section 8 "v75: files and processes". MIT licence (Onyx).
#   CIRCLE=<the Circle tree> (default: ../../circle from here, or /home/user/Onyx/circle)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
CIRCLE=${CIRCLE:-$ROOT/circle}
[ -f "$CIRCLE/addon/fatfs/ff.c" ] || CIRCLE=/home/user/Onyx/circle
T=${TMPDIR:-/tmp}/onyx_ofiletest
F=$CIRCLE/addon/fatfs
rm -rf "$T" && mkdir -p "$T/fatfs"
cp "$F/ff.c" "$F/ff.h" "$F/ffunicode.c" "$F/diskio.h" "$F/diskio.cpp" "$T/"
# the fork's configuration, with f_mkfs and f_fdisk, without the OS locks (one flow here), the
# free clusters always counted (no FSINFO: the end checks that every cluster came back)
sed -e 's/^#define FF_USE_MKFS[[:space:]]*0/#define FF_USE_MKFS 1/' \
    -e 's/^#define FF_FS_NOFSINFO[[:space:]]*0/#define FF_FS_NOFSINFO 3/' \
    -e 's/^#define FF_FS_REENTRANT[[:space:]]*1/#define FF_FS_REENTRANT 0/' "$F/ffconf.h" > "$T/ffconf.h"
cp "$T/ff.h" "$T/ffconf.h" "$T/fatfs/"
INC="-I$HERE/ofile/stub -I$HERE/fs/stub -I$ROOT/kernel/include -I$T"
gcc -O1 -g -c -I"$T" "$T/ff.c" -o "$T/ff.o"
gcc -O1 -g -c -I"$T" "$T/ffunicode.c" -o "$T/ffunicode.o"
g++ -O1 -g -std=c++17 -c -I"$T" -I"$HERE/fs/stub" "$T/diskio.cpp" -o "$T/diskio.o"
FLAGS="-std=gnu++17 -O1 -g -fsanitize=address,undefined -w -DRAMFS_HOST_TEST"
g++ $FLAGS $INC -c "$ROOT/kernel/sys/ofile.cpp" -o "$T/ofile.o"
g++ $FLAGS $INC -c "$ROOT/kernel/sys/ramfs.cpp" -o "$T/ramfs.o"
g++ $FLAGS $INC "$HERE/ofile/ofiletest.cpp" "$T/ofile.o" "$T/ramfs.o" "$T/ff.o" "$T/ffunicode.o" "$T/diskio.o" -o "$T/ofiletest"
fail=0
for seed in 1 2 3; do
	"$T/ofiletest" $seed 1500 || fail=1
done
[ $fail = 0 ] && echo "all passed"
exit $fail
