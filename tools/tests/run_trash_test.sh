#!/bin/sh
# Host test of user/Kits/systemkit/trash.h + user/Kits/filekit/fsutil.h: they are copied next to a mock "kapi.h" (and "appkit/appkit.h", its relay)
# (mock_kapi.h -- the kernel's return conventions) and built with the host compiler.
set -e
here=$(cd "$(dirname "$0")" && pwd)
b=$(mktemp -d)
cp "$here/trash_test.cpp" "$b/"
mkdir -p "$b/systemkit" && cp "$here/../../user/Kits/systemkit/trash.h" "$here/../../user/Kits/systemkit/trash.inc" "$here/../../user/Kits/systemkit/sk_api.h" "$b/systemkit/"
mkdir -p "$b/filekit" && cp "$here/../../user/Kits/filekit/fsutil.h" "$here/../../user/Kits/filekit/fsutil.inc" "$b/filekit/"
cp "$here/mock_kapi.h" "$b/kapi.h" && mkdir -p "$b/appkit" && echo '#include "../kapi.h"' > "$b/appkit/appkit.h"
g++ -w -I"$b" "$b/trash_test.cpp" -o "$b/t"
"$b/t"
rm -rf "$b"
