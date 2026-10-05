#!/bin/sh
# run_games_test.sh -- build every uikit game on the PC (uikithost/host_kapi.h: a fake kapi
# table), play a short scripted scenario and save screenshots (PNG) into $OUT
# (default /tmp/onyx_games). Checks that the code runs clean under UBSan (ASan's shadow covers the table VA).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-${TMPDIR:-/tmp}/onyx_games}; export OUT
mkdir -p "$OUT"
UIKIT=$(ls "$ROOT"/user/uikit/*.cpp | grep -v imgload)
for g in ${@:-INVADERS PIPES SOLITAIRE FREECELL GRAPHCALC ICONEDIT RTF BASICRT MENUBAR}; do
	BIN=$OUT/game_$g
	g++ -std=c++17 -O1 -g -w -fsanitize=undefined -fno-sanitize=alignment -DGAME_$g \
	    "$ROOT/user/basic/basnum.cpp" "$ROOT/user/basic/bascomp.cpp" "$ROOT/user/basic/basvm.cpp" "$ROOT/user/basic/basbax.cpp" \
	    -I"$HERE/uikithost/inc" -I"$ROOT/user" -I"$ROOT/kernel/include" \
	    "$HERE/uikithost/game_host.cpp" $UIKIT -o "$BIN"
	(cd "$OUT" && "$BIN" "$ROOT/sdcard")
done
for f in "$OUT"/*.ppm; do python3 -c "from PIL import Image; import sys; Image.open(sys.argv[1]).save(sys.argv[1][:-4]+'.png')" "$f"; rm -f "$f"; done
echo "screenshots in $OUT"
