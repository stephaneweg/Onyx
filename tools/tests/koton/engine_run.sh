#!/bin/sh
# tools/tests/koton/engine_run.sh -- Koton's engine (model, theory, generators, compiler, audio
# engine + MeltySynth) on the PC under ASan / UBSan / LSan, and the same sources compiled for the Pi.
#   sh tools/tests/koton/engine_run.sh [file.sf2]	(default /opt/sf2/GeneralUser-GS.sf2)
set -e
cd "$(dirname "$0")/../../.."
K=user/Apps/koton
OUT=${TMPDIR:-/tmp}/onyx_koton_engine
g++ -std=gnu++17 -O1 -g -Wall -Wextra -fno-exceptions -fno-rtti -fsanitize=address,undefined -I $K -I user \
	-o "$OUT" tools/tests/koton/engine_test.cpp $K/engine/*.cpp $K/synth/*.cpp
"$OUT" "$@"
# the Pi: every engine source compiles with the app's flags (a newlib app, FPU on)
A=${ARMGCC:-/opt/arm/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf/bin}/aarch64-none-elf-g++
if [ -x "$A" ]; then
	for f in $K/engine/*.cpp; do
		$A -mcpu=cortex-a72 -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -ffunction-sections \
			-fdata-sections -Wall -Wextra -I $K -I user -I kernel/include -c "$f" -o /dev/null
	done
	echo "aarch64 compile check: OK"
fi
