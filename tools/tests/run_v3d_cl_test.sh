#!/bin/sh
# The V3D control-list packets (kern/v3d_cl.h) against Mesa's field positions, on the PC.
set -e
cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}/onyx_v3d_cl_test
g++ -std=gnu++20 -O1 -w -include assert.h -Itools/tests/fs/stub -Ikernel/include -o "$OUT" tools/tests/v3d/cl_test.cpp
"$OUT"
# kern/v3d_pack42.h / v3d_pack71.h (tools/v3d/genpackets.py from Mesa's XML): the 4.2 output against the
# hand-written packets above, byte for byte; and the headers are the generator's current output
g++ -std=gnu++20 -O1 -w -include assert.h -Itools/tests/fs/stub -Ikernel/include -o "$OUT.pack" tools/tests/v3d/pack_test.cpp
"$OUT.pack"
for v in 42 71; do
	python3 tools/v3d/genpackets.py $v | cmp -s - kernel/include/kern/v3d_pack$v.h || { echo "FAIL: kernel/include/kern/v3d_pack$v.h is not genpackets.py's output (make -C tools/v3d)"; exit 1; }
done
echo "ok: kern/v3d_pack42.h, v3d_pack71.h are genpackets.py's output"
