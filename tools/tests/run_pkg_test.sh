#!/bin/sh
# tools/tests/run_pkg_test.sh -- the package manager's `pkg` (user/BinUtils/pkg.cpp, user/Libs/pkg/pkglib.h) on the
# PC: built over the desktop simulator's kapi, zlib and mbedTLS (compiled for the PC here), driven by
# tools/tests/pkg/test.py against a repository made by tools/pkg/mkrepo.py. Needs g++, gcc, python3
# with `cryptography`.
set -e
cd "$(dirname "$0")/../.."
OUT=${OUT:-/tmp/onyx_pkg_test}
mkdir -p "$OUT/z" "$OUT/mb"
Z=third_party/zlib-1.3.1; M=third_party/mbedtls-3.6.3
for f in adler32 crc32 deflate inflate inffast inftrees trees zutil; do [ -f "$OUT/z/$f.o" ] || gcc -O2 -w -c $Z/$f.c -o "$OUT/z/$f.o"; done
if [ ! -f "$OUT/libmb.a" ]; then
	for f in $M/library/*.c; do echo "$f"; done | xargs -P 8 -I{} sh -c 'gcc -O1 -w -I'$M'/include -I'$M'/library -c {} -o '"$OUT"'/mb/$(basename {} .c).o'
	ar rcs "$OUT/libmb.a" "$OUT"/mb/*.o
fi
g++ -std=gnu++17 -O1 -g -w -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include -I $Z -I $M/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST \
    -o "$OUT/pkg" user/BinUtils/pkg.cpp tools/tests/desktop_sim/fakekapi.cpp "$OUT"/z/*.o "$OUT/libmb.a" -lpthread
python3 tools/tests/pkg/test.py "$OUT/pkg" "$OUT/run"
