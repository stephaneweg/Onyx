#!/bin/sh
# run_sound_resample_test.sh -- the sound output's rate converter (kernel/sys/sound_resample.h, kapi v84:
# the 48 kHz outputs, USB and HDMI) on the PC. MIT licence (Onyx).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
T=${TMPDIR:-/tmp}/onyx_resample_test; mkdir -p "$T"
g++ -std=gnu++17 -O1 -g -fsanitize=address,undefined -Wall -Wextra "$HERE/sound/resample_test.cpp" -o "$T/resample_test" -lm
"$T/resample_test"
