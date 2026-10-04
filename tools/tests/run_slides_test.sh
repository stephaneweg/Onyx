#!/bin/sh
# run_slides_test.sh -- Slides' files (user/Apps/slides/odp.h) on the PC: the sample deck made
# (tools/tests/slides/make_sample.cpp -> a scratch file; MAKE=1 also writes sdcard/docs/cafe-2026.odp), read and
# checked, written again and read back the same; with LibreOffice installed, its conversion of the deck read too.
# Linked with the desktop simulator's wtk (the image codecs), its kapi, FreeType (the text's layout) and gpucomp
# (the CPU's path: a chart's picture).
#
#   sh tools/tests/run_slides_test.sh
#
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${TMPDIR:-/tmp}/onyx_slides_test
FT=$ROOT/third_party/freetype-2.14.3
mkdir -p "$OUT/obj" "$OUT/ft"
CXX="g++ -std=gnu++17 -O1 -g -I$ROOT/user -I$ROOT/kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
for f in "$ROOT"/user/wtk/*.cpp; do $CXX -w -c "$f" -o "$OUT/obj/$(basename "$f" .cpp).o" & done; wait
rm -f "$OUT/libwtk.a"; ar rcs "$OUT/libwtk.a" "$OUT"/obj/*.o
$CXX -w -c "$HERE/desktop_sim/fakekapi.cpp" -o "$OUT/fakekapi.o"
for f in base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c base/ftsynth.c autofit/autofit.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c; do
	gcc -O2 -w -c -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' \
		-I"$ROOT/user/ft" -I"$FT/include" "$FT/src/$f" -o "$OUT/ft/$(basename $f .c).o" &
done; wait
rm -f "$OUT/libft.a"; ar rcs "$OUT/libft.a" "$OUT"/ft/*.o
gcc -O2 -w -I"$ROOT/user" -I"$ROOT/kernel/include" -c "$ROOT/user/gpucomp/gpucomp.c" -o "$OUT/gpucomp.o"
L="$OUT/fakekapi.o $OUT/gpucomp.o $OUT/libwtk.a $OUT/libft.a"
$CXX -w -I"$ROOT/user/ft" -I"$FT/include" "$HERE/slides/make_sample.cpp" $L -o "$OUT/make_sample"
$CXX -w -I"$ROOT/user/ft" -I"$FT/include" "$HERE/slides/slides_test.cpp" $L -o "$OUT/slides_test"
cd "$ROOT"
SAMPLE="$OUT/cafe-2026.odp"; [ -n "$MAKE" ] && SAMPLE=sdcard/docs/cafe-2026.odp
SIM_SD=sdcard "$OUT/make_sample" "$SAMPLE"
OTHER=""
if command -v soffice >/dev/null 2>&1; then
	rm -rf "$OUT/lo"; mkdir -p "$OUT/lo"
	if timeout 300 soffice --headless --convert-to odp --outdir "$OUT/lo" "$SAMPLE" >/dev/null 2>&1; then OTHER="$OUT/lo/$(basename "$SAMPLE")"; fi
fi
SIM_SD=sdcard "$OUT/slides_test" "$SAMPLE" $OTHER
