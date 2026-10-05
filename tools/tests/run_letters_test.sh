#!/bin/sh
# run_letters_test.sh -- Letters' files (user/Apps/letters/) on the PC: a document with all Letters knows
# (styles, lists, a table with merged cells, fields, headers and footers, an image, the mail merge's data)
# through RTF, .docx and .odt, each read back the same (with LibreOffice installed: its conversions of
# ours too). UBSan (not ASan: the simulator's kapi table sits at a fixed address, in ASan's shadow gap);
# VG=1 runs it through valgrind. Linked with the desktop simulator's uikit (the image codecs) and kapi.
#
#   sh tools/tests/run_letters_test.sh
#
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${TMPDIR:-/tmp}/onyx_writer_test
mkdir -p "$OUT/obj"
CXX="g++ -std=gnu++17 -O1 -g -I$ROOT/user -I$ROOT/user/Kits -I$ROOT/kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST -fsanitize=undefined"
for f in "$ROOT"/user/Kits/uikit/*.cpp; do $CXX -w -c "$f" -o "$OUT/obj/$(basename "$f" .cpp).o" & done; wait
rm -f "$OUT/libuikit.a"; ar rcs "$OUT/libuikit.a" "$OUT"/obj/*.o
$CXX -w -c "$HERE/desktop_sim/fakekapi.cpp" -o "$OUT/fakekapi.o"
$CXX -Wall -Wno-unused-function -Wno-format-truncation "$HERE/letters/files_test.cpp" "$OUT/fakekapi.o" "$OUT/libuikit.a" -o "$OUT/files_test"
D=$OUT/files; rm -rf "$D"; mkdir -p "$D"
if [ -n "$VG" ]; then valgrind -q --error-exitcode=3 "$OUT/files_test" "$D"; else "$OUT/files_test" "$D"; fi
