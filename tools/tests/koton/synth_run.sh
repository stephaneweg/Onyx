#!/bin/sh
# tools/tests/koton/synth_run.sh -- Koton's SoundFont synthesizer (user/Apps/koton/synth, the
# MeltySynth port) on the PC: the tests under ASan / UBSan / LSan (a leak fails), then the speed
# (real-time factor, 32 voices) without sanitizers, then a compile check of every .cpp for the Pi
# (aarch64-none-elf, the app flags, -Wall -Wextra) when that toolchain is installed.
#   sh tools/tests/koton/synth_run.sh [file.sf2]     (default /opt/sf2/GeneralUser-GS.sf2)
set -e
cd "$(dirname "$0")/../../.."
OUT=${TMPDIR:-/tmp}/koton_synth_test
SRC="tools/tests/koton/synth_test.cpp user/Apps/koton/synth/soundfont.cpp user/Apps/koton/synth/voice.cpp user/Apps/koton/synth/effects.cpp user/Apps/koton/synth/synthesizer.cpp"

g++ -std=gnu++17 -O2 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$OUT" $SRC -lm
"$OUT" "$@"

g++ -std=gnu++17 -O2 -Wall -Wextra -o "$OUT.bench" $SRC -lm
"$OUT.bench" --bench "$@"

A64=/opt/arm/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-g++
if [ -x "$A64" ]; then
	for f in user/Apps/koton/synth/*.cpp; do
		"$A64" -std=gnu++17 -mcpu=cortex-a72 -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics \
			-fno-use-cxa-atexit -ffunction-sections -fdata-sections -Wall -Wextra -Werror \
			-I user -I kernel/include -c "$f" -o "${TMPDIR:-/tmp}/koton_synth_a64.o"
	done
	echo "aarch64 compile check: OK (no warnings)"
fi
