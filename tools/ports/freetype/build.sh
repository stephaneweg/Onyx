#!/bin/sh
# build.sh -- FreeType 2.14.3 for the POSIX ports (third_party/freetype-2.14.3, the apps' copy, FTL):
# libfreetype.a + include/freetype2 + freetype2.pc into the sysroot -- the FreeType HarfBuzz, Skia and
# (later) WebKit link. Fuller than the apps' lean build (user/ft: TrueType only, no hinting
# interpreter): what web fonts need --
#   modules (onyx_ftmodule.h): TrueType, CFF / CFF2 (OpenType PostScript outlines), sfnt, the
#     auto-hinter and the PostScript hinter, the smooth (anti-aliased) and mono rasterizers, OT-SVG;
#   options (ftoption.h's defaults, plus): the system zlib (WOFF 1), brotli (WOFF 2), libpng (colour
#     bitmap glyphs: CBDT / sbix emoji); the bytecode interpreter, variable fonts (GX / OpenType Font
#     Variations), COLR v0 / v1 colour layers -- ftoption.h's defaults; not bzip2, not HarfBuzz (the
#     auto-hinter's script coverage through HarfBuzz would make the two libraries depend on each other).
# The sources are compiled as FreeType's own "unix-free" build lists them (one file per module).
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (this script).
. "$(dirname "$0")/../common.sh"

SRC=$TP/freetype-2.14.3
B=$PORTS_OUT/build/freetype
rm -rf "$B"
mkdir -p "$B/obj"
onyx_install_deps
[ -f "$ONYX_SYSROOT/lib/libpng16.a" ] || sh "$ONYX/tools/ports/libpng/build.sh"

FT_DEFS="-DFT2_BUILD_LIBRARY -DFT_CONFIG_MODULES_H=<onyx_ftmodule.h> -DFT_CONFIG_OPTION_SYSTEM_ZLIB \
 -DFT_CONFIG_OPTION_USE_PNG -DFT_CONFIG_OPTION_USE_BROTLI -DHAVE_UNISTD_H -DHAVE_FCNTL_H"
SOURCES="base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbbox.c base/ftbitmap.c
 base/ftfstype.c base/ftgasp.c base/ftglyph.c base/ftmm.c base/ftstroke.c base/ftsynth.c base/ftotval.c
 base/ftpatent.c base/fttype1.c base/ftcid.c base/ftfntfmt.c base/ftlcdfil.c
 autofit/autofit.c truetype/truetype.c cff/cff.c sfnt/sfnt.c psaux/psaux.c psnames/psnames.c
 pshinter/pshinter.c smooth/smooth.c raster/raster.c svg/svg.c gzip/ftgzip.c"
echo "freetype: build (onyx_ftmodule.h; zlib, brotli, libpng)"
n=0
for s in $SOURCES; do
	o=$B/obj/$(basename "$s" .c).o
	"${ONYX_TOOLCHAIN_PREFIX}gcc" $CFLAGS -O2 -w $FT_DEFS -I"$ONYX/tools/ports/freetype" -I"$SRC/include" \
		-I"$ONYX_SYSROOT/include/libpng16" -c "$SRC/src/$s" -o "$o" &
	n=$((n + 1))
	if [ $n -ge "$JOBS" ]; then wait; n=0; fi
done
wait
for s in $SOURCES; do
	[ -f "$B/obj/$(basename "$s" .c).o" ] || { echo "freetype: $s did not compile" >&2; exit 1; }
done
"${ONYX_TOOLCHAIN_PREFIX}ar" rcs "$B/libfreetype.a" "$B"/obj/*.o
cp "$B/libfreetype.a" "$ONYX_SYSROOT/lib/"
rm -rf "$ONYX_SYSROOT/include/freetype2"
mkdir -p "$ONYX_SYSROOT/include/freetype2"
cp -r "$SRC/include/ft2build.h" "$SRC/include/freetype" "$ONYX_SYSROOT/include/freetype2/"
# the configuration a client sees: the same modules (FT_CONFIG_MODULES_H is only read when building)
# and the options this build has (ftoption.h with the three switched on)
sed -e 's|^/\* #define FT_CONFIG_OPTION_SYSTEM_ZLIB \*/|#define FT_CONFIG_OPTION_SYSTEM_ZLIB|' \
    -e 's|^/\* #define FT_CONFIG_OPTION_USE_PNG \*/|#define FT_CONFIG_OPTION_USE_PNG|' \
    -e 's|^/\* #define FT_CONFIG_OPTION_USE_BROTLI \*/|#define FT_CONFIG_OPTION_USE_BROTLI|' \
    "$SRC/include/freetype/config/ftoption.h" > "$ONYX_SYSROOT/include/freetype2/freetype/config/ftoption.h"
# FreeType's pkg-config version is its libtool version (2.14.x: 26.x), not the release number
printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\n\nName: FreeType 2\nURL: https://freetype.org\nDescription: A free, high-quality, and portable font engine (Onyx: 2.14.3).\nVersion: 26.4.20\nRequires.private: zlib, libpng16, libbrotlidec\nLibs: -L${libdir} -lfreetype\nLibs.private: -lpng16 -lz -lbrotlidec\nCflags: -I${includedir}/freetype2\n' \
	"$ONYX_SYSROOT" > "$ONYX_SYSROOT/lib/pkgconfig/freetype2.pc"
ls -l "$ONYX_SYSROOT/lib/libfreetype.a" | awk '{print "freetype: " $5 "  " $NF}'
