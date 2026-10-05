#!/bin/sh
# pc/Koton/build.sh -- Koton for Windows, built on Linux with MinGW-w64 from the Onyx sources, unchanged:
# user/Apps/koton (the studio), user/Kits/uikit, FreeType, MeltySynth, the plugins (user/Apps/kp_*) and the AI
# helper (user/BinUtils/llm.cpp + mbedTLS), over pc/Koton/winkapi.cpp (the Onyx kernel's table on Win32).
# Result: pc/dist/Koton/ -- Koton.exe, its plugins, bin/llm.exe and the card's files it reads (the fonts,
# the theme, the drum catalogue, koton/: songs, SoundFont, plugins). Copy the folder to the PC.
#   sh pc/Koton/build.sh
# Needs x86_64-w64-mingw32-g++ (the posix threads variant is used when there), python3 + Pillow (the icon).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${KOTON_WIN_TMP:-/tmp/onyx_koton_win}
DIST="$ROOT/pc/dist/Koton"
CXX=$(command -v x86_64-w64-mingw32-g++-posix || command -v x86_64-w64-mingw32-g++)
CC=$(command -v x86_64-w64-mingw32-gcc-posix || command -v x86_64-w64-mingw32-gcc)
AR=x86_64-w64-mingw32-ar
WINDRES=x86_64-w64-mingw32-windres
STRIP=x86_64-w64-mingw32-strip
U="$ROOT/user"
K="$U/Apps/koton"
FT="$ROOT/third_party/freetype-2.14.3"
TLS="$ROOT/third_party/mbedtls-3.6.3"
FLAGS="-O2 -w -I$U -I$U/Kits -I$ROOT/kernel/include -D_WIN32_WINNT=0x0A00"
CXXF="-std=gnu++17 $FLAGS -fno-exceptions -fno-rtti -include $HERE/onyxwin.h -DIMG_HOST_TEST"
LIBS="-static -lgdi32 -luser32 -lshell32 -lole32 -lwinmm -lbcrypt -lws2_32 -lsynchronization"
mkdir -p "$OUT/uikit" "$OUT/ft" "$OUT/k" "$OUT/tls" "$OUT/kapi"
throttle () { while [ "$(jobs -p | wc -l)" -ge 8 ]; do sleep 0.2; done; }
bg () { throttle; ( "$@" || touch "$OUT/FAILED" ) & }		# a compile in the background (8 at once)
done_bg () { wait; if [ -e "$OUT/FAILED" ]; then echo "pc/Koton/build.sh: a compile failed (above)"; exit 1; fi; }
rm -f "$OUT/FAILED"

# ---- the Onyx kernel's table on Windows ---------------------------------------------------------------
$CXX $CXXF -c "$HERE/winkapi.cpp" -o "$OUT/kapi/winkapi.o"
# ---- uikit ---------------------------------------------------------------------------------------------------
for f in "$U"/Kits/uikit/*.cpp; do bg $CXX $CXXF -c "$f" -o "$OUT/uikit/$(basename "$f" .cpp).o" ; done; done_bg
rm -f "$OUT/libuikit.a"; $AR rcs "$OUT/libuikit.a" "$OUT"/uikit/*.o
# ---- FreeType (Onyx's configuration) -----------------------------------------------------------------------
for f in base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c base/ftsynth.c autofit/autofit.c \
	 truetype/truetype.c sfnt/sfnt.c smooth/smooth.c; do
	bg $CC -O2 -w -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' \
		-I"$U/ft" -I"$FT/include" -c "$FT/src/$f" -o "$OUT/ft/$(basename "$f" .c).o"
done; done_bg
rm -f "$OUT/libft.a"; $AR rcs "$OUT/libft.a" "$OUT"/ft/*.o
# ---- Koton ---------------------------------------------------------------------------------------------------
for f in "$K"/engine/*.cpp "$K"/synth/*.cpp "$K"/plug/*.cpp; do bg $CXX $CXXF -fno-math-errno -I"$K" -c "$f" -o "$OUT/k/$(basename "$f" .cpp).o" ; done; done_bg
python3 - "$ROOT/sdcard/apps/koton.app/icon.bmp" "$OUT/koton.ico" <<'EOF'
import sys
from PIL import Image
im = Image.open (sys.argv[1]).convert ("RGBA")
px = im.load ()
for y in range (im.size[1]):				# (the icon's magenta: see-through)
	for x in range (im.size[0]):
		r, g, b, a = px[x, y]
		if (r, g, b) == (255, 0, 255): px[x, y] = (0, 0, 0, 0)
im.save (sys.argv[2], sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64)])
EOF
printf '1 ICON "%s"\n' "$OUT/koton.ico" > "$OUT/koton.rc"
$WINDRES "$OUT/koton.rc" -O coff -o "$OUT/koton_res.o"
mkdir -p "$DIST"
$CXX $CXXF -g -fno-math-errno -mwindows -I"$K" -I"$U/ft" -I"$FT/include" -o "$OUT/Koton.exe" "$K/main.cpp" \
	"$OUT/kapi/winkapi.o" "$OUT"/k/*.o "$OUT/koton_res.o" "$OUT/libuikit.a" "$OUT/libft.a" $LIBS
cp "$OUT/Koton.exe" "$DIST/Koton.exe"		# (the one with its symbols stays in $OUT: addr2line -e)
# ---- the plugins: koton/plugins/<name>/main.exe + plugin.json ---------------------------------------------------------
for d in "$U"/Apps/kp_*; do
	n=$(basename "$d"); n=${n#kp_}
	mkdir -p "$DIST/koton/plugins/$n"
	bg $CXX $CXXF -fno-math-errno -mwindows -o "$DIST/koton/plugins/$n/main.exe" "$d/main.cpp" "$OUT/kapi/winkapi.o" "$OUT/libuikit.a" $LIBS
	cp "$d/plugin.json" "$DIST/koton/plugins/$n/"
done; done_bg
# ---- the AI helper: bin/llm.exe (mbedTLS over Winsock, as on Onyx over its TCP) -------------------------------------------
for f in "$TLS"/library/*.c; do bg $CC -O2 -w -I"$TLS/include" -c "$f" -o "$OUT/tls/$(basename "$f" .c).o" ; done; done_bg
rm -f "$OUT/libmbed.a"; $AR rcs "$OUT/libmbed.a" "$OUT"/tls/*.o
mkdir -p "$DIST/bin"
$CXX $CXXF -I"$TLS/include" -o "$DIST/bin/llm.exe" "$U/BinUtils/llm.cpp" "$OUT/kapi/winkapi.o" "$OUT/libmbed.a" $LIBS
$STRIP "$DIST/Koton.exe" "$DIST"/koton/plugins/*/main.exe "$DIST/bin/llm.exe"
# ---- the card's files Koton reads ----------------------------------------------------------------------------------------
SD="$ROOT/sdcard"
mkdir -p "$DIST/fonts" "$DIST/res/fonts" "$DIST/etc" "$DIST/apps/koton.app" "$DIST/koton/songs" "$DIST/res/soundfonts" "$DIST/manuals/koton"
cp "$SD"/fonts/*.fnt "$DIST/fonts/"
cp "$SD"/res/fonts/* "$DIST/res/fonts/"
cp "$SD/etc/theme.txt" "$DIST/etc/"
cp "$SD/apps/koton.app/drums.json" "$DIST/apps/koton.app/"
cp "$SD"/koton/songs/* "$DIST/koton/songs/"
cp "$SD"/res/soundfonts/* "$DIST/res/soundfonts/"
cp "$SD"/manuals/koton/*.pdf "$DIST/manuals/koton/"
cp "$HERE/README.txt" "$DIST/"
echo "built: $DIST"
