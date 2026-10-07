#!/bin/sh
# tools/tests/run_archiver_test.sh -- the Archiver's engine (user/Apps/archiver) on the PC: arctool
# (tools/tests/archiver/arctool.cpp) over the desktop simulator's kapi and zlib, driven by test.py.
# Needs g++, gcc, python3, zip and unzip.
set -e
cd "$(dirname "$0")/../.."
OUT=${OUT:-/tmp/onyx_archiver_test}
mkdir -p "$OUT/z"
Z=third_party/zlib-1.3.1
for f in adler32 crc32 deflate inflate inffast inftrees trees zutil; do gcc -O2 -w -c $Z/$f.c -o "$OUT/z/$f.o"; done
g++ -std=gnu++17 -O1 -g -w -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include -I $Z -fno-exceptions -fno-rtti -DIMG_HOST_TEST \
    -o "$OUT/arctool" tools/tests/archiver/arctool.cpp tools/tests/desktop_sim/fakekapi.cpp "$OUT"/z/*.o -lpthread
python3 tools/tests/archiver/test.py "$OUT/arctool" "$OUT/ram" || exit 1
# /bin/zip and /bin/unzip (user/BinUtils/zip.cpp, unzip.cpp) on the same engine
for t in zip unzip; do
	g++ -std=gnu++17 -O1 -g -w -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include -I $Z -fno-exceptions -fno-rtti -DIMG_HOST_TEST \
	    -o "$OUT/$t" user/BinUtils/$t.cpp tools/tests/desktop_sim/fakekapi.cpp "$OUT"/z/*.o -lpthread
done
python3 tools/tests/archiver/cli_test.py "$OUT/zip" "$OUT/unzip" "$OUT/cliram"
