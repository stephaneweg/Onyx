#!/bin/sh
# tools/tests/run_pdf_test.sh -- the PDF Viewer's MuPDF (user/Apps/pdf/mupdf.mk) built for the PC and checked on Onyx's
# own manuals: opened, a page rendered (not blank), its text, a word found, the outline (tools/tests/pdf/mutest.c);
# and a PDF written by user/pdf/pdfwrite.h read back (tools/tests/pdf/writetest.cpp).
set -e
cd "$(dirname "$0")/../.."
OUT=${PDF_TEST_TMP:-/tmp/onyx_pdf_test}
mkdir -p "$OUT"
make -s -j8 -f user/Apps/pdf/mupdf.mk MU_ROOT=. MU_CC=gcc MU_AR=ar MU_OUT="$OUT/mupdf" MU_CFLAGS=-O2 >/dev/null
gcc -O2 -Ithird_party/mupdf-1.28.5/include tools/tests/pdf/mutest.c "$OUT/mupdf/libmupdf.a" -lm -o "$OUT/mutest"
"$OUT/mutest" sdcard/manuals/ledger/Ledger.pdf 22 "$OUT/ledger22.ppm" invoice
"$OUT/mutest" sdcard/manuals/koton/Koton.pdf 4 "$OUT/koton4.ppm" chord
# the PDF writer (user/pdf/pdfwrite.h, Letters' and the Spreadsheet's export): a document written, then read back
g++ -std=gnu++17 -O1 -Iuser -Ithird_party/freetype-2.14.3/include tools/tests/pdf/writetest.cpp "$OUT/mupdf/libmupdf.a" -o "$OUT/writetest"
"$OUT/writetest" "$OUT/written.pdf"
"$OUT/mutest" "$OUT/written.pdf" 1 "$OUT/written1.ppm" invoice
echo "pdf: all good"
