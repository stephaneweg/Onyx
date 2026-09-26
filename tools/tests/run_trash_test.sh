#!/bin/sh
# Host test of user/trash.h + user/fsutil.h: they are copied next to a mock "kapi.h"
# (mock_kapi.h -- the kernel's return conventions) and built with the host compiler.
set -e
here=$(cd "$(dirname "$0")" && pwd)
b=$(mktemp -d)
cp "$here/../../user/trash.h" "$here/../../user/fsutil.h" "$here/trash_test.cpp" "$b/"
cp "$here/mock_kapi.h" "$b/kapi.h"
g++ -w -I"$b" "$b/trash_test.cpp" -o "$b/t" && "$b/t"
rm -rf "$b"
