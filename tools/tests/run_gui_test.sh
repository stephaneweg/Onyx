#!/bin/sh
# run_gui_test.sh -- the compositor's clipped drawing on the PC (kernel/gui/gimage.cpp with
# Circle's font): a scene redrawn inside random clip rectangles must equal the full redraw
# there and leave every pixel outside untouched (the dirty-rectangle refresh relies on it).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
T=${TMPDIR:-/tmp}
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -DAARCH=64 -DRASPPI=4 -I"$ROOT/kernel/include" -I"$ROOT/circle/include" \
	"$HERE/gui/clip_test.cpp" "$ROOT/kernel/gui/gimage.cpp" "$ROOT/circle/lib/chargenerator.cpp" "$ROOT"/circle/lib/font*.cpp -o "$T/onyx_clip_test"
"$T/onyx_clip_test"
