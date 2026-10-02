#!/bin/sh
# build.sh -- the windows' title font (gen_title_font.cpp): the apps' FreeType built for the PC, the
# stand-in kernel (the card's fonts read from sdcard/), then sdcard/res/fonts/title.aaf written.
# MIT licence (Onyx).
set -e
cd "$(dirname "$0")/../.."
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
FT=third_party/freetype-2.14.3
for f in base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c base/ftsynth.c autofit/autofit.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c; do
	gcc -O2 -w -c -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' \
		-Iuser/ft -I$FT/include $FT/src/$f -o "$T/$(basename $f .c).o"
done
CXX="g++ -std=gnu++17 -O1 -w -I user -I kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
for f in user/wtk/*.cpp; do $CXX -c "$f" -o "$T/w_$(basename "$f" .cpp).o"; done
$CXX -c tools/tests/desktop_sim/fakekapi.cpp -o "$T/fakekapi.o"
$CXX -Iuser/ft -I$FT/include tools/title_font/gen_title_font.cpp "$T/fakekapi.o" "$T"/w_*.o "$T"/ft*.o "$T"/autofit.o "$T"/truetype.o "$T"/sfnt.o "$T"/smooth.o -lpthread -o "$T/gen"
SIM="exit" "$T/gen" "${1:-sdcard/res/fonts/title.aaf}"
