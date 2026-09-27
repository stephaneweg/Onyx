#!/bin/sh
# run_v3d_clip_test.sh -- kern/v3d_clip.h on the PC (see tools/tests/v3d/cliptest.cpp).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
T=${TMPDIR:-/tmp}
g++ -std=c++17 -O2 -Wall -Wextra -fsanitize=address,undefined -I"$ROOT/kernel/include" -I"$ROOT/user" "$HERE/v3d/cliptest.cpp" -o "$T/onyx_v3d_clip"
"$T/onyx_v3d_clip"
