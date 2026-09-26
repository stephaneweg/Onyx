#!/bin/sh
# run_fms_test.sh -- fmtracker's file formats and the kernel FM synthesizer on the PC:
#   * every SD:/music/fms/*.FMS is parsed and written back byte for byte, every
#     apps/fmtracker.app/ins/*.FMI round-trips (user/Apps/fmtracker/fms.h);
#   * kernel/sys/sound.cpp renders a few instruments (tools/tests/sound/synth_test.cpp):
#     each must make sound while held, and the sine must have the right pitch.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
T=${TMPDIR:-/tmp}
g++ -std=c++17 -O1 -g -Wall -fsanitize=address,undefined -I"$ROOT/kernel/include" "$HERE/fms/fms_test.cpp" -o "$T/onyx_fms_test"
"$T/onyx_fms_test" "$ROOT"/sdcard/music/fms/*.FMS "$ROOT"/sdcard/apps/fmtracker.app/ins/*.FMI
g++ -std=c++17 -O1 -g -Wall -fsanitize=address,undefined -I"$ROOT/kernel/include" "$HERE/sound/synth_test.cpp" -o "$T/onyx_synth_test"
fail=0
out=$("$T/onyx_synth_test" "$T/sine.wav")
echo "$out"
echo "$out" | grep -q "crossings/s 52[0-4]" || { echo "FAIL: sine pitch"; fail=1; }
for i in PIANO ORGAN FLUTE BDRUM1 SNARE1 VIOLIN; do
	out=$("$T/onyx_synth_test" "$T/$i.wav" "$ROOT/sdcard/apps/fmtracker.app/ins/$i.FMI")
	echo "$out"
	echo "$out" | grep -q ": peak 0 rms" && { echo "FAIL: $i is silent"; fail=1; }
done
exit $fail
