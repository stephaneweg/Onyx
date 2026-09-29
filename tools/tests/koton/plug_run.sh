#!/bin/sh
# tools/tests/koton/plug_run.sh -- Koton's plugins on the PC under ASan / UBSan / LSan (plug_test.cpp:
# the rings, the engine rendering ahead and its delay compensation, a real-time run with plugin
# threads, every plugin's DSP -> /tmp/koton_plug_*.wav, the generators, the states, the manifests),
# then the host's sources compiled for the Pi (the plugins themselves: cd user && make plugins).
#   sh tools/tests/koton/plug_run.sh [--manifest <plugin>]   (--manifest: print its parameter list)
set -e
cd "$(dirname "$0")/../../.."
K=user/Apps/koton
OUT=${TMPDIR:-/tmp}/onyx_koton_plug
mkdir -p "$OUT.o"
SAN="-O1 -g -Wall -Wextra -Wno-maybe-uninitialized -fno-exceptions -fno-rtti -fsanitize=address,undefined -I $K -I user"
# the plugins, each on its own with its test symbol; the engine, the synthesiser, the host's generator
# helpers and the test
OBJS=""
for d in user/Apps/kp_*/; do
	n=$(basename "$d")
	g++ -std=gnu++17 $SAN -DKPLUG_DSP_ONLY -DKPLUG_TEST_SYM=${n}_api -c "$d/main.cpp" -o "$OUT.o/$n.o" &
	OBJS="$OBJS $OUT.o/$n.o"
done
for f in $K/engine/*.cpp $K/synth/*.cpp $K/plug/plugctx.cpp tools/tests/koton/plug_test.cpp; do
	n=$(basename "$f" .cpp)
	g++ -std=gnu++17 $SAN -pthread -c "$f" -o "$OUT.o/x_$n.o" &
	OBJS="$OBJS $OUT.o/x_$n.o"
done
wait
g++ -fsanitize=address,undefined -pthread -o "$OUT" $OBJS
"$OUT" "$@"
[ "$1" = "--manifest" ] && exit 0
A=${ARMGCC:-/opt/arm/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf/bin}/aarch64-none-elf-g++
if [ -x "$A" ]; then
	for f in $K/plug/*.cpp; do
		$A -mcpu=cortex-a72 -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -ffunction-sections \
			-fdata-sections -Wall -Wextra -Wno-maybe-uninitialized -I $K -I user -I kernel/include -c "$f" -o /dev/null
	done
	echo "aarch64 compile check (plug/*.cpp): OK"
fi
