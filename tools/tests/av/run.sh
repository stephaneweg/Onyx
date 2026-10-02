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
# With FFmpeg (AV_WITH_FFMPEG: the Media Player's build): its libraries for the PC (third_party/ffmpeg-7.1.2/onyx/build.sh
# host, a few minutes the first time), clips in the containers and codecs it adds, made by the ffmpeg command (skipped
# without it): read, decoded, played in the file mode, a seek.
if command -v ffmpeg >/dev/null && [ "${AV_NO_FFMPEG:-}" = "" ]; then
	FFH=${FFMPEG_HOST:-/tmp/onyx_ffmpeg_host}
	sh third_party/ffmpeg-7.1.2/onyx/build.sh host "$FFH" || exit 1
	C="$OUT/ffclips"; mkdir -p "$C"
	mk () { [ -f "$C/$1" ] || ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc2=size=320x240:rate=24:duration=4 \
		-f lavfi -i sine=frequency=440:sample_rate=44100:duration=4 -shortest "$@" "$C/$1"; }
	mk h264-aac.mp4 -c:v libx264 -preset ultrafast -pix_fmt yuv420p -c:a aac
	mk h264-aac.ts -c:v libx264 -preset ultrafast -pix_fmt yuv420p -c:a aac
	mk hevc-aac.mkv -c:v libx265 -preset ultrafast -x265-params log-level=none -c:a aac
	mk xvid-mp3.avi -c:v mpeg4 -vtag XVID -c:a libmp3lame
	mk mpeg2-mp2.mpg -c:v mpeg2video -c:a mp2
	mk wmv2-wma.wmv -c:v wmv2 -c:a wmav2
	mk flv1-mp3.flv -c:v flv -c:a libmp3lame
	mk theora-vorbis.ogv -c:v libtheora -c:a libvorbis
	mk h264-ac3.mkv -c:v libx264 -preset ultrafast -pix_fmt yuv420p -c:a ac3
	mk h264-10bit-422.mp4 -c:v libx264 -preset ultrafast -pix_fmt yuv422p10le -c:a aac
	VPX=third_party/libvpx-1.15.2; DAV=third_party/dav1d-1.5.1; OPU=third_party/opus-1.5.2
	gcc -std=gnu11 -O2 -w -DAV_POSIX -DAV_WITH_FFMPEG -Iuser/av -Ithird_party/ffmpeg-7.1.2/onyx/include \
		-o "$OUT/fftest" user/av/*.c tools/tests/av/fftest.c -L"$FFH" -lavformat -lavcodec -lswscale -lswresample -lavutil -lpthread -lm || exit 1
	echo "== FFmpeg (PC)"
	"$OUT/fftest" "$C" h264-aac.mp4:96:4 h264-aac.ts:96:4 hevc-aac.mkv:96:4 xvid-mp3.avi:96:4 mpeg2-mp2.mpg:96:4 \
		wmv2-wma.wmv:96:4 flv1-mp3.flv:96:4 theora-vorbis.ogv:96:4 h264-ac3.mkv:96:4 h264-10bit-422.mp4:96:4 || fail=1
fi
exit $fail
