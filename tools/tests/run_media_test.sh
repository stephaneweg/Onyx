#!/bin/sh
# tools/tests/run_media_test.sh -- Media Player's decoders and tags on the PC: sample files made by ffmpeg
# (MP3, Ogg Vorbis, FLAC, WAV -- 48 kHz, 22 kHz mono --) and a MIDI file, decoded by tools/tests/media/
# dectest.cpp through the simulator's kapi (SIM_OVERLAY: the samples as SD:/). Needs g++ and ffmpeg.
set -e
cd "$(dirname "$0")/../.."
OUT=${OUT:-/tmp/onyx_media_test}; rm -rf "$OUT"; mkdir -p "$OUT/sd"
S="$OUT/sd"
ffmpeg -loglevel error -f lavfi -i "sine=frequency=440:duration=5" -ar 48000 -ac 2 "$S/t48.wav"
ffmpeg -loglevel error -i "$S/t48.wav" -b:a 128k "$S/t.mp3"
ffmpeg -loglevel error -i "$S/t48.wav" -c:a libvorbis -q:a 3 "$S/t.ogg"
ffmpeg -loglevel error -i "$S/t48.wav" -ar 44100 "$S/t.flac"
ffmpeg -loglevel error -f lavfi -i "sine=frequency=330:duration=3" -ar 22050 -ac 1 "$S/m22.wav"
python3 tools/tests/media/make_midi.py "$S/t.mid"
SD="$PWD/sdcard"
K=user/Apps/koton/synth
CXX="g++ -std=gnu++17 -O2 -w -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include -I third_party -fno-exceptions -fno-rtti -DIMG_HOST_TEST"
gcc -O2 -w -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I third_party -c user/Apps/media/codecs.c -o "$OUT/codecs.o"
gcc -O2 -w -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I third_party -c user/Apps/media/vorbis.c -o "$OUT/vorbis.o"
$CXX -c tools/tests/desktop_sim/fakekapi.cpp -o "$OUT/fakekapi.o"
$CXX -o "$OUT/dectest" tools/tests/media/dectest.cpp $K/*.cpp "$OUT/codecs.o" "$OUT/vorbis.o" "$OUT/fakekapi.o" -lpthread -lm
cd "$OUT"
SIM_SD="$SD" SIM_OVERLAY="$S" SIM="exit" ./dectest SD:/t48.wav SD:/t.mp3 SD:/t.ogg SD:/t.flac SD:/m22.wav SD:/t.mid
