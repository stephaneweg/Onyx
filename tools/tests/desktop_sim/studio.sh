#!/bin/sh
# tools/tests/desktop_sim/studio.sh -- uikit's studio widgets (Knob, VuMeter, SegmentedControl, ToolBar
# + ToolButton, LcdDisplay) and uikit's text through a FreeType face (ft/uikitface.h), on the PC: the
# gallery gallery/studio.cpp built twice -- with the face (-DWITH_FT, DejaVu Sans 13 px: FreeType as
# user/Makefile builds it for the Pi) and with uikit's bitmap fonts --, each run in the card's theme and
# in a dark studio palette (SIM_DARK=1), its window written as a PNG:
#
#   sh tools/tests/desktop_sim/studio.sh [out dir]        (default /tmp/onyx_studio)
#     -> studio-ft.png, studio-ft-dark.png, studio-bitmap.png, studio-bitmap-dark.png,
#        studio-ft-edit.png (the text box: a click in it, é typed: the caret by measure),
#        widgets-ft.png, widgets-ft-tip.png (the Widget Showcase, unchanged, under the face --
#        gallery/ftwrap.cpp installs it before the app's main; a tooltip); and runs facetest.cpp
#        (the face's measures, the carets and clicks, UTF-8 editing: "facetest: all passed")
#
# Not a documentation screenshot (screenshots/ is shots.sh's). Needs g++, python3 with Pillow + numpy.
set -e
cd "$(dirname "$0")/../../.."
D=tools/tests/desktop_sim
OUT=${1:-/tmp/onyx_studio}
mkdir -p "$OUT/obj" "$OUT/ft" "$OUT/writes"
export SIM_WRITES="$OUT/writes"
CXX="g++ -std=gnu++17 -O1 -w -I user -I kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
for f in user/uikit/*.cpp; do $CXX -c "$f" -o "$OUT/obj/$(basename "$f" .cpp).o" & done; wait
rm -f "$OUT/libuikit.a"; ar rcs "$OUT/libuikit.a" "$OUT"/obj/*.o
$CXX -c $D/fakekapi.cpp -o "$OUT/fakekapi.o"
FT=third_party/freetype-2.14.3
FT_SRC="base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c base/ftsynth.c autofit/autofit.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c"
for f in $FT_SRC; do gcc -O2 -w -c -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>' \
	-Iuser/ft -I$FT/include $FT/src/$f -o "$OUT/ft/$(basename $f .c).o" & done; wait
rm -f "$OUT/libft.a"; ar rcs "$OUT/libft.a" "$OUT"/ft/*.o
$CXX -DWITH_FT -Iuser/ft -I$FT/include -o "$OUT/studio_ft" "$OUT/fakekapi.o" $D/gallery/studio.cpp "$OUT/libuikit.a" "$OUT/libft.a" &
$CXX -o "$OUT/studio_bitmap" "$OUT/fakekapi.o" $D/gallery/studio.cpp "$OUT/libuikit.a" &
$CXX -Dmain=app_main -c user/Apps/widgets/main.cpp -o "$OUT/widgets_app.o" &
wait
$CXX -Iuser/ft -I$FT/include -o "$OUT/widgets_ft" "$OUT/fakekapi.o" $D/gallery/ftwrap.cpp "$OUT/widgets_app.o" "$OUT/libuikit.a" "$OUT/libft.a"
$CXX -Iuser/ft -I$FT/include -o "$OUT/facetest" "$OUT/fakekapi.o" $D/facetest.cpp "$OUT/libuikit.a" "$OUT/libft.a"
"$OUT/facetest" | tail -1			# (the measures, the carets, UTF-8 editing)
run () {	# run APP NAME "SCRIPT" [VAR=value ...]
	app=$1; name=$2; script=$3; shift 3
	env SIM_POS=40,40 "$@" SIM="$script;dump $OUT/$name.elsm;exit" "$OUT/$app" >>"$OUT/log.txt" 2>&1 || { echo "studio: $name failed ($OUT/log.txt)"; exit 1; }
	python3 $D/shot.py "$OUT/$name.elsm" "$OUT/$name.png" --flat=808890 >/dev/null && echo "  $OUT/$name.png"
}
: > "$OUT/log.txt"
W="wait;wait;wait"
run studio_ft studio-ft "$W"
run studio_ft studio-ft-dark "$W" SIM_DARK=1
run studio_bitmap studio-bitmap "$W"
run studio_bitmap studio-bitmap-dark "$W" SIM_DARK=1
# the text box: a click after "Ghibli" (the caret at the nearest glyph's edge), then typed: é, a space
run studio_ft studio-ft-edit "wait;down 247 269;up 247 269;wait;key 0xE9;key 32;wait;down 30 350;move 200 372;up 200 372;$W" SIM_DARK=1
T="wait"; for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40; do T="$T;wait"; done
run widgets_ft widgets-ft "$W" SIM_APP=widgets
run widgets_ft widgets-ft-tip "wait;move 470 466;$T" SIM_APP=widgets
echo "studio: done ($OUT)"
