#!/bin/sh
# tools/tests/run_pdf_test.sh -- the PDF Viewer's MuPDF (user/Apps/pdf/mupdf.mk) built for the PC and checked on Onyx's
# own manuals: opened, a page rendered (not blank), its text, a word found, the outline (tools/tests/pdf/mutest.c).
set -e
cd "$(dirname "$0")/../.."
OUT=${PDF_TEST_TMP:-/tmp/onyx_pdf_test}
mkdir -p "$OUT"
make -s -j8 -f user/Apps/pdf/mupdf.mk MU_ROOT=. MU_CC=gcc MU_AR=ar MU_OUT="$OUT/mupdf" MU_CFLAGS=-O2 >/dev/null
gcc -O2 -Ithird_party/mupdf-1.28.5/include tools/tests/pdf/mutest.c "$OUT/mupdf/libmupdf.a" -lm -o "$OUT/mutest"
"$OUT/mutest" sdcard/manuals/ledger/Ledger.pdf 22 "$OUT/ledger22.ppm" invoice
"$OUT/mutest" sdcard/manuals/koton/Koton.pdf 4 "$OUT/koton4.ppm" chord
echo "pdf: all good"
