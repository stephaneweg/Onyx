#!/bin/sh
# tools/tests/av/run.sh -- the media library's tests (user/av): the clips made by mkmedia.py,
# avtest built for the PC and run; with qemu-aarch64 and aarch64-linux-gnu-gcc, built for
# AArch64 too (the NEON paths) and run under qemu.
#
#   sh tools/tests/av/run.sh            (OUT, default /tmp/avtest)
cd "$(dirname "$0")/../../.."
OUT=${OUT:-/tmp/avtest}
mkdir -p "$OUT"
python3 tools/tests/av/mkmedia.py "$OUT/media" || { echo "mkmedia failed"; exit 1; }
SRC="user/av/av_demux.c user/av/av_mkv.c user/av/av_mp4.c user/av/av_riff.c user/av/av_flac.c user/av/av_mp3.c user/av/av_stub.c \
     user/av/av_codec.c user/av/av_yuv.c user/av/av_resample.c user/av/av_store.c user/av/av_player.c"
CF="-std=gnu99 -O2 -g -Wall -Wno-unused-function -DAV_POSIX -Iuser/av"
fail=0
gcc $CF -o "$OUT/avtest" $SRC tools/tests/av/avtest.c -lpthread -lm || exit 1
echo "== PC"
"$OUT/avtest" "$OUT/media" || fail=1
if command -v aarch64-linux-gnu-gcc >/dev/null && command -v qemu-aarch64 >/dev/null; then
	aarch64-linux-gnu-gcc $CF -static -o "$OUT/avtest-a64" $SRC tools/tests/av/avtest.c -lpthread -lm || exit 1
	echo "== AArch64 (qemu)"
	qemu-aarch64 "$OUT/avtest-a64" "$OUT/media" || fail=1
fi
exit $fail
