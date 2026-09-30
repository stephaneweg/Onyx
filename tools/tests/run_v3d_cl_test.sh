#!/bin/sh
# The V3D control-list packets (kern/v3d_cl.h) against Mesa's field positions, on the PC.
set -e
cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}/onyx_v3d_cl_test
g++ -std=gnu++20 -O1 -w -include assert.h -Itools/tests/fs/stub -Ikernel/include -o "$OUT" tools/tests/v3d/cl_test.cpp
"$OUT"
