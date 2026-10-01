#!/bin/sh
# tools/tests/netsurf/mediatest.sh -- Jet Browser's <video>, <audio> and Media Source Extensions
# on the PC bench (docs/06 §44), with the clips tools/tests/av/mkmedia.py makes (uncompressed
# I420 video, PCM / FLAC audio: the codecs this build has) served by mediasrv.py (Range
# requests), the bench's stand-in sound output on (SIM_SOUND=1, what it plays written to a file):
#   - media-video.html: <video src> (WebM, MP4, MP4 with its moov at the end, fragmented MP4):
#     the events and their order, every frame presented in order (requestVideoFrameCallback),
#     the last frame's pixels = the decoded reference (RGB sums), a seek and its frame, the
#     window's picture (the frame and the native controls painted);
#   - media-mse.html: MediaSource + SourceBuffer (WebM and fragmented MP4): appends out of order,
#     appendBuffer while updating, abort, remove, buffered, endOfStream, playback to the end, the
#     A/V sync (the frames shown against the audio heard);
#   - media-audio.html: <audio src> FLAC then new Audio() WAV: events, the sound heard
#     bit-exact against the FLAC's reference PCM;
#   - media-seek.html: a 10 s file loaded by 64 KB ranges, a seek to 8 s before those bytes came;
#   - media-types.html: canPlayType, isTypeSupported, mediaCapabilities.
# Builds as jstest.sh (OUT, default /tmp/nsbench). PORT (default 8417). Exit 0: all passed.
#
#   sh tools/tests/netsurf/mediatest.sh
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
PORT=${PORT:-8417}
mkdir -p "$OUT"
make -f $T/host.mk OUT="$OUT/build" -j"$(nproc)" >"$OUT/build.log" 2>&1 ||
	{ echo "build failed: $OUT/build.log"; exit 1; }
W="$OUT/media-www"
rm -rf "$W"
mkdir -p "$W"
python3 tools/tests/av/mkmedia.py "$W" || { echo "mkmedia failed"; exit 1; }
cp $T/pages/media-*.html "$W/"
python3 $T/mediasrv.py "$W" "$PORT" >"$OUT/mediasrv.log" 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null' EXIT
sleep 1
fail=0
waits() { i=0; while [ "$i" -lt "$1" ]; do printf 'wait;'; i=$((i + 1)); done; }
run() {	# run <page?query> <log> <waits>
	rm -f "$OUT/sound.raw"
	SIM_SCREEN=800x600 SIM_SLEEP=1 SIM_POS=0,0 NS_JSDEBUG=1 SIM_REALNET=1 SIM_SOUND=1 \
	SIM_SOUNDOUT="$OUT/sound.raw" SIM_ARGS="http://127.0.0.1:$PORT/$1" \
	SIM="$(waits "$3")dump $2.elsm;exit" timeout 300 "$OUT/build/netsurf" >"$2" 2>&1
}
expect() {	# expect <log> <text>
	if grep -q -F -- "console: $2" "$1"; then echo "  ok    $2"; else echo "  FAIL  $2"; fail=1; fi
}
expect_re() {	# expect_re <log> <regex> <what>
	if grep -q -E -- "console: $2" "$1"; then echo "  ok    $3"; else echo "  FAIL  $3 ($2)"; fail=1; fi
}
refsum() { python3 $T/mediacheck.py sum "$W/frames.txt" "$1"; }

for f in clip.webm clip.mp4 clip-moovend.mp4 frag.mp4; do
	echo "media-video.html: <video src=$f>"
	L="$OUT/media-video-$f.log"
	run "media-video.html?src=$f" "$L" 300
	expect "$L" "order loadstart,durationchange,resize,loadedmetadata,loadeddata,canplay,canplaythrough,play,playing,pause,ended"
	expect_re "$L" "summary frames=(49|50) mono=true timeupdates=[0-9]+ duration=2.00 size=96x64 paused=true decoded=50" \
		"50 frames decoded, all presented in order, duration 2.00, 96x64"
	expect "$L" "frame pts=1.960 sum=$(refsum 49) 96x64"
	expect "$L" "seekframe t=1.000 pts=1.000 sum=$(refsum 25) paused=true"
	expect "$L" "buffered 1 0.00-2.00"
	expect_re "$L" "stats sound=true sync_ms=" "the sound heard"
	grep "console: stats" "$L" | sed 's/^/        /'
	grep -q "^JS " "$L" && { echo "  FAIL  script errors:"; grep -A2 "^JS " "$L" | head -6; fail=1; }
done
python3 tools/tests/desktop_sim/shot.py "$OUT/media-video-clip.webm.log.elsm" "$OUT/media-video.png" >/dev/null 2>&1
if python3 - "$OUT/media-video.png" <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
# the dump has the window frame: the page starts at 4, 68; the video box: 192 x 128 there; its
# controls' bar: its bottom 36 px (dark); the frame at 1.0 s is the bright "flash"
fr = im.getpixel((60, 100)); bar = im.getpixel((100, 185)); bg = im.getpixel((400, 300))
ok = sum(fr) > 500 and fr != bg and max(bar) < 70 and sum(bg) > 700
print('        frame', fr, 'controls', bar, 'page', bg)
sys.exit(0 if ok else 1)
EOF
then echo "  ok    the window: the frame and the native controls painted"; else echo "  FAIL  the window's picture"; fail=1; fi

for m in "mse.webm mse.json video/webm;codecs=%22i420,pcm%22" "frag.mp4 frag.json video/mp4;codecs=%22i420,pcm%22"; do
	set -- $m
	echo "media-mse.html: MediaSource, $1"
	L="$OUT/media-mse-$1.log"
	run "media-mse.html?file=$1&json=$2&mime=$3" "$L" 300
	expect "$L" "isTypeSupported true vp9=false"
	expect "$L" "init updating=false events=updatestart,update,updateend"
	expect "$L" "appendBuffer while updating: InvalidStateError"
	expect "$L" "buffered after 3,4 [1.00,2.00]"
	expect "$L" "buffered all [0.00,2.00] element [0.00,2.00]"
	expect "$L" "buffered after remove [0.00,0.50][1.00,2.00]"
	expect "$L" "buffered again [0.00,2.00]"
	expect "$L" "ended ended duration=2.00"
	expect "$L" "sourceended"
	expect "$L" "play resolved"
	expect_re "$L" "summary t=2.00 decoded=50 dropped=0 sound=true sync_ms=([0-9]|[1-3][0-9])\." "played to the end, A/V sync under 40 ms"
	grep "console: summary" "$L" | sed 's/^/        /'
	grep -q "^JS " "$L" && { echo "  FAIL  script errors:"; grep -A2 "^JS " "$L" | head -6; fail=1; }
done

echo "media-audio.html: <audio src=tone.flac controls>, new Audio('tone.wav')"
L="$OUT/media-audio.log"
run "media-audio.html?src=tone.flac&second=tone.wav" "$L" 450
expect "$L" "summary duration=1.500 t=1.500 sound=true rect=300x54"
expect "$L" "order volumechange,volumechange,loadstart,durationchange,loadedmetadata,loadeddata,canplay,canplaythrough,play,playing,pause,ended"
expect "$L" "second duration=1.000 paused=false"
expect "$L" "second ended t=1.000"
if python3 $T/mediacheck.py heard "$OUT/sound.raw" "$W/tone-ref.raw" | sed 's/^/        /'; then
	echo "  ok    the FLAC heard bit-exact"; else echo "  FAIL  the FLAC heard"; fail=1; fi

for f in long.webm long.mp4; do
	echo "media-seek.html: $f by 64 KB ranges, a seek to 8 s"
	L="$OUT/media-seek-$f.log"
	: >"$OUT/mediasrv.log"
	run "media-seek.html?src=$f" "$L" 200
	expect "$L" "duration 10.00"
	expect "$L" "seekframe t=8.000 pts=8.000 sum=$(refsum 0)"
	expect_re "$L" "after play t=8\.[5-9]" "played on from 8 s"
	n=$(grep -c "GET /$f" "$OUT/mediasrv.log")
	if [ "$n" -lt 40 ]; then echo "  ok    $n range requests (not the whole file)"; else echo "  FAIL  $n range requests"; fail=1; fi
done

echo "media-types.html: canPlayType, isTypeSupported, mediaCapabilities"
L="$OUT/media-types.log"
run "media-types.html" "$L" 40
expect "$L" 'type video/webm; codecs="i420, pcm" can="probably" mse=true'
expect "$L" 'type audio/webm; codecs="pcm" can="probably" mse=true'
expect "$L" 'type audio/flac can="maybe" mse=false'
expect "$L" 'type video/x-nothing can="" mse=false'
expect "$L" "api function,function,function,function,function,function,function,true,function,HAVE_ENOUGH_DATA=4,0,0,true,true"
expect "$L" "caps supported=true smooth=true powerEfficient=false"
expect "$L" "caps supported=false smooth=false powerEfficient=false"
grep "console: codecs\|console: type .*codecs=\"\(vp9\|avc1\|opus\|av01\)" "$L" | sed 's/^/        /'

[ $fail = 0 ] && echo "mediatest: all passed" || echo "mediatest: FAILED"
exit $fail
