#!/bin/sh
# run_n64_test.sh -- the N64 core (user/n64) on the PC, against PeterLemon's test ROMs
# (github.com/PeterLemon/N64, cloned; not kept in the repo), each compared with the reference
# picture beside it (python3 + Pillow + numpy), on the lines both have:
#   N64_TEST_ROMS=/path/to/PeterLemon/N64 tools/tests/run_n64_test.sh [filter]
# With N64_GAME=<rom.z64> it also runs that game 300 frames (its speed, its last picture in
# $TMPDIR/onyx_n64_game.ppm).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
T=${TMPDIR:-/tmp}
R=${N64_TEST_ROMS:?set N64_TEST_ROMS to the PeterLemon N64 folder}
g++ -std=c++17 -O2 -Wall -Wextra -I"$ROOT/user" -I"$ROOT/user/Kits" "$HERE/n64/n64test.cpp" "$ROOT"/user/n64/*.cpp -o "$T/onyx_n64test"
fail=0; pass=0
for rom in $(cd "$R" && find CPUTest -name "*.N64" | sort | grep "${1:-.}"); do
	ref="$R/${rom%.N64}.png"
	[ -f "$ref" ] || continue
	timeout 60 "$T/onyx_n64test" "$R/$rom" 60 "$T/onyx_n64.ppm" >/dev/null || { echo "FAIL $rom: did not finish"; fail=$((fail+1)); continue; }
	d=$(python3 -c "
import numpy as np
from PIL import Image
o=np.asarray(Image.open('$T/onyx_n64.ppm').convert('RGB')).astype(int)
r=np.asarray(Image.open('$ref').convert('RGB')).astype(int)
if r.shape[0] >= 1.8 * o.shape[0]: o = np.repeat(o, 2, axis=0)
if r.shape[1] >= 1.8 * o.shape[1]: o = np.repeat(o, 2, axis=1)
h=min(o.shape[0],r.shape[0]); w=min(o.shape[1],r.shape[1])
print('%.2f'%((np.abs(r[:h,:w]-o[:h,:w]).max(axis=2)>24).mean()*100))" 2>/dev/null || echo noref)
	if [ "$d" = "0.00" ]; then echo "ok   $rom"; pass=$((pass+1)); else echo "FAIL $rom: $d% of the pixels differ"; fail=$((fail+1)); fi
done
echo "$pass ok, $fail failed"
if [ -n "$N64_GAME" ]; then "$T/onyx_n64test" "$N64_GAME" 300 "$T/onyx_n64_game.ppm"; fi
[ $fail -eq 0 ]
