#!/bin/sh
# tools/tests/av/bench.sh -- the decoders' speed (avbench.c) on clips, for the PC and for
# AArch64 (the Pi's code paths: libvpx's NEON, dav1d's assembly) under qemu-aarch64 -cpu
# cortex-a72. qemu's time is not the Pi's: docs/06 §44 has the measures and how the Pi's
# figures are estimated from them. Clips: any WebM / MP4 (VP9, AV1, Opus); without arguments,
# the test clips (tools/tests/av/clips: tiny, 96 x 64).
#
#   sh tools/tests/av/bench.sh [clip...]          (OUT, default /tmp/avbench)
cd "$(dirname "$0")/../../.."
OUT=${OUT:-/tmp/avbench}
mkdir -p "$OUT"
[ $# -gt 0 ] || set -- tools/tests/av/clips/vp9.webm tools/tests/av/clips/av1.mp4
make -f tools/tests/av/bench.mk OUT="$OUT/pc" -j"$(nproc)" >"$OUT.pc.log" 2>&1 || { echo "build failed: $OUT.pc.log"; exit 1; }
echo "== PC (C code)"
for f; do "$OUT/pc/avbench" "$f" 3; done
if command -v aarch64-linux-gnu-gcc >/dev/null && command -v qemu-aarch64 >/dev/null; then
	make -f tools/tests/av/bench.mk OUT="$OUT/a64" AV_ARCH=aarch64 CC=aarch64-linux-gnu-gcc -j"$(nproc)" \
		>"$OUT.a64.log" 2>&1 || { echo "build failed: $OUT.a64.log"; exit 1; }
	echo "== AArch64 (NEON / assembly) under qemu"
	for f; do qemu-aarch64 -cpu cortex-a72 "$OUT/a64/avbench" "$f"; done
fi
