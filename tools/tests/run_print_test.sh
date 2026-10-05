#!/bin/sh
# run_print_test.sh -- the print system on the PC: a job recorded, replayed as a PDF and as PWG Raster pages
# (user/printerkit/: job.h, pdfsink.h, raster.h), the raster stream read back; the IPP messages (printerkit/ipp.h)
# encoded and decoded, and -- IPP_PRINTER=<address> -- a real printer asked what it can do and whether it
# would take a job (Validate-Job: nothing is printed). The pictures and the PDFs stay in the output folder.
#   sh tools/tests/run_print_test.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${PRINT_TEST_TMP:-${TMPDIR:-/tmp}/onyx_print_test}
FT=$ROOT/third_party/freetype-2.14.3
mkdir -p "$OUT/ft"
if [ ! -f "$OUT/libft.a" ]; then
	for f in base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c base/ftsynth.c autofit/autofit.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c; do
		gcc -O2 -w -c -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' \
			-I"$ROOT/user/ft" -I"$FT/include" "$FT/src/$f" -o "$OUT/ft/$(basename $f .c).o" &
	done; wait
	ar rcs "$OUT/libft.a" "$OUT"/ft/*.o
fi
CXX="g++ -std=gnu++17 -O2 -g -Wall -I$ROOT/user -I$ROOT/kernel/include -I$ROOT/user/ft -I$FT/include -fno-exceptions -fno-rtti -DPRINT_HOST -DIMG_HOST_TEST"
$CXX -Wno-unused-function -Wno-sign-compare -Wno-unused-parameter "$HERE/print/print_test.cpp" "$OUT/libft.a" -o "$OUT/print_test"
"$OUT/print_test" "$ROOT/sdcard/res/fonts/DejaVuSans.ttf" "$OUT"
if [ -f "$HERE/print/ipp_test.cpp" ]; then
	$CXX -Wno-unused-function "$HERE/print/ipp_test.cpp" -o "$OUT/ipp_test"
	"$OUT/ipp_test" $IPP_PRINTER
fi
