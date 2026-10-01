#!/bin/sh
# run_ramfs_test.sh -- the kernel's RAM file system (kernel/sys/ramfs.cpp, the RAM: volume) on the
# PC: built with RAMFS_HOST_TEST (the pages from the PC's allocator, no scheduler), random file
# operations checked against a model, then the edges (a full volume, a file removed while open, a
# dead process's handles, the names, every page given back). docs/02 *The RAM: volume*.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${OUT:-${TMPDIR:-/tmp}/onyx_ramfstest}
mkdir -p "$OUT"
FLAGS="-std=gnu++17 -O2 -g -DAARCH=64 -DRASPPI=4 -DRAMFS_HOST_TEST -fsanitize=address,undefined -w"
g++ $FLAGS -I "$ROOT/kernel/include" -I "$ROOT/circle/include" \
	"$ROOT/kernel/sys/ramfs.cpp" "$ROOT/tools/tests/ramfs/ramfstest.cpp" -o "$OUT/ramfstest"
fail=0
for seed in 1 2 3 4 5; do
	"$OUT/ramfstest" $seed 3000 || fail=1
done
[ $fail = 0 ] && echo "all passed"
exit $fail
