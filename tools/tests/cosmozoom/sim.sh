#!/bin/sh
# tools/tests/cosmozoom/sim.sh -- Cosmo Zoom! run on the PC (the stand-in kernel of the desktop simulator, UIKit,
# FreeType, AudioKit; no GPU: the software renderer), driven by a script of events, its picture dumped as a PNG.
#   sh tools/tests/cosmozoom/sim.sh OUT.png "SCRIPT" [ARGS]     (SCRIPT: desktop_sim's: wait; key K; hold K; release K...)
# Needs g++, python3 with Pillow + numpy.
set -e
cd "$(dirname "$0")/../../.."
D=tools/tests/desktop_sim
OUT=${SIM_TMP:-/tmp/onyx_cz}
mkdir -p "$OUT/obj" "$OUT/ft" "$OUT/ak" "$OUT/writes"
CXX="g++ -std=gnu++17 -O2 -w -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
if [ ! -f "$OUT/libuikit.a" ]; then
	for f in user/Kits/uikit/*.cpp; do $CXX -c "$f" -o "$OUT/obj/$(basename "$f" .cpp).o" & done; wait
	ar rcs "$OUT/libuikit.a" "$OUT"/obj/*.o
	$CXX -c $D/fakekapi.cpp -o "$OUT/fakekapi.o"
	FT=third_party/freetype-2.14.3
	for f in base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c base/ftsynth.c autofit/autofit.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c; do
		gcc -O2 -w -c -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' -Iuser/Kits/fontkit -I$FT/include $FT/src/$f -o "$OUT/ft/$(basename $f .c).o" & done; wait
	ar rcs "$OUT/libft.a" "$OUT"/ft/*.o
	I="-Iuser -Iuser/Kits -Iuser/Runtime -Iuser/Include -Iuser/Libs -Ithird_party"
	gcc -O2 -w $I -c user/Apps/media/codecs.c -o "$OUT/ak/codecs.o"; gcc -O2 -w $I -c user/Apps/media/vorbis.c -o "$OUT/ak/vorbis.o"
	for f in user/Apps/koton/synth/*.cpp user/Kits/audiokit/*.cpp; do $CXX -Iuser/Apps/koton -Iuser/Apps/media -Ithird_party -c "$f" -o "$OUT/ak/$(basename "$f" .cpp).o"; done
	ar rcs "$OUT/libaudiokit.a" "$OUT"/ak/*.o
	mkdir -p "$OUT/z"
	for f in adler32 crc32 deflate inflate inffast inftrees trees zutil; do gcc -O2 -w -c third_party/zlib-1.3.1/$f.c -o "$OUT/z/$f.o"; done
	ar rcs "$OUT/libz.a" "$OUT"/z/*.o
fi
$CXX -Iuser/Kits/fontkit -Ithird_party/freetype-2.14.3/include -o "$OUT/cosmozoom" "$OUT/fakekapi.o" user/Apps/cosmozoom/main.cpp user/Kits/filekit/fkcore.cpp user/Kits/filekit/kvtext.cpp -Ithird_party/zlib-1.3.1 \
	"$OUT/libuikit.a" "$OUT/libft.a" "$OUT/libaudiokit.a" "$OUT/libz.a" -lpthread -lm 2>&1 | grep -v "^$" || true
png=$1; script=$2; shift 2
rm -f "$OUT/dump.elsm"
env SIM_OVERLAY=$D/sd SIM_WRITES="$OUT/writes" SIM_ARGS="$*" SIM="$script;dump $OUT/dump.elsm;exit" "$OUT/cosmozoom" > "$OUT/log.txt" 2>&1 || { echo "sim: failed"; tail -5 "$OUT/log.txt"; exit 1; }
python3 $D/shot.py "$OUT/dump.elsm" "$png" > /dev/null && echo "$png"
