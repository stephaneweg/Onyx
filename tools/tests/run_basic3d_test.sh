#!/bin/sh
# run_basic3d_test.sh -- the BASIC 3D statements on the PC: builds the BASIC core with a
# window-less ScreenHost (software renderer), runs basic3d/scene.bas, saves each RENDER3D as a
# PPM (in $OUT, default /tmp/onyx_basic3d) and checks a few pixels of the first one.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-${TMPDIR:-/tmp}/onyx_basic3d}
BIN=${TMPDIR:-/tmp}/onyx_basic3d_host
mkdir -p "$OUT"
g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I"$ROOT/user" -I"$ROOT/user/Kits" -I"$ROOT/user/Runtime" -I"$ROOT/user/Include" -I"$ROOT/user/Libs" -I"$ROOT/user/Emulators" -I"$ROOT/user/Ports" \
    "$ROOT/user/Libs/basic/basnum.cpp" "$ROOT/user/Libs/basic/bascomp.cpp" "$ROOT/user/Libs/basic/basvm.cpp" "$ROOT/user/Libs/basic/basbax.cpp" \
    "$HERE/basic3d/render_host.cpp" -o "$BIN"
"$BIN" "$HERE/basic3d/scene.bas" "$OUT"
python3 - "$OUT/out_0.ppm" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
parts = d.split(b'\n', 3); w, h = map(int, parts[1].split()); px = parts[3]
def at(x, y): i = (y * w + x) * 3; return tuple(px[i:i + 3])
checks = { 'sky (top left)': (at(5, 5), lambda c: c == (20, 24, 40)),
           'the sphere is red (centre)': (at(w // 2 + 20, h // 2 - 30), lambda c: c[0] > 120 and c[1] < 90),
           'the floor is grey (bottom)': (at(w // 2, h - 20), lambda c: abs(c[0] - c[1]) < 25 and c[0] > 40) }
bad = 0
for name, (c, ok) in checks.items():
    print(('ok   ' if ok(c) else 'FAIL ') + name, c); bad += not ok(c)
sys.exit(bad)
PY
