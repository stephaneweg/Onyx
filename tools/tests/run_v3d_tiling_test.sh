#!/bin/sh
# The V3D texture layouts (kern/v3d_tiling.h) against Mesa's reference functions, on the PC.
set -e
cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}/onyx_v3d_tiling_test
g++ -O2 -w -Ikernel/include -o "$OUT" tools/tests/v3d/tiling_test.cpp
"$OUT"
