#!/bin/sh
# run_3dforge_test.sh -- 3DForge's document (user/Apps/3dforge/fdoc.h: the history of steps replayed by Manifold,
# the sketch, the fillets and chamfers, the file, the STL / OBJ exports) on the PC: 3dforge/doctest.cpp makes the
# bracket of the mock-ups step by step and checks each volume. Manifold and Clipper2 are compiled for the PC
# (kept in $HOME/.cache/onyx_3dforge_host).
#   sh tools/tests/run_3dforge_test.sh
# FORGE_SAMPLE=path: the bracket's file is written there too (sdcard/docs/3d/bracket.3df was made so).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${FORGE_HOST_OUT:-$HOME/.cache/onyx_3dforge_host}
MF="$ROOT/third_party/manifold-3.5.4"; CL="$ROOT/third_party/clipper2-46f6391/CPP/Clipper2Lib"
DEFS="-DMANIFOLD_PAR=-1 -DMANIFOLD_CROSS_SECTION -DMANIFOLD_NO_IOSTREAM -DMANIFOLD_NO_FILESYSTEM -DCLIPPER2_NO_IOSTREAM -I$MF/include -I$CL/include"
mkdir -p "$OUT/mf"
if [ ! -f "$OUT/libmanifold.a" ]; then
	for f in "$MF"/src/*.cpp "$MF"/src/cross_section/*.cpp "$CL"/src/*.cpp; do
		g++ -std=c++17 -O2 -w $DEFS -c "$f" -o "$OUT/mf/$(basename "$f" .cpp).o" &
	done; wait
	ar rcs "$OUT/libmanifold.a" "$OUT"/mf/*.o
fi
g++ -std=c++17 -O1 -g -Wall -Wno-misleading-indentation $DEFS -I"$ROOT/user" "$HERE/3dforge/doctest.cpp" "$OUT/libmanifold.a" -o "$OUT/doctest"
"$OUT/doctest" $FORGE_SAMPLE
# Manufacture: the tool paths and the G-code for the sample part
g++ -std=c++17 -O1 -g -Wall -Wno-misleading-indentation $DEFS -I"$ROOT/user" "$HERE/3dforge/camtest.cpp" "$OUT/libmanifold.a" -o "$OUT/camtest"
"$OUT/camtest" "$ROOT/sdcard/docs/3d/bracket.3df"
# Print: the sample part cut into layers for a resin printer; FORGE_PM_REF=path: a file of the printer's own slicer,
# which must be written back the same
g++ -std=c++17 -O2 -g -Wall -Wno-misleading-indentation $DEFS -I"$ROOT/user" "$HERE/3dforge/printtest.cpp" "$OUT/libmanifold.a" -o "$OUT/printtest"
"$OUT/printtest" "$ROOT/sdcard/docs/3d/bracket.3df" $FORGE_PM_REF
