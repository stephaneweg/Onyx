#!/bin/sh
# tools/tests/koton/plug_host_run.sh -- the plugin host (plug/plughost.cpp) and the plugins' kernel side
# (kplug.h) on the PC: plug_host_test.cpp over the desktop simulator's stand-in kernel
# (tools/tests/desktop_sim/fakekapi.cpp, unchanged), kp_fm2 / kp_delay / kp_arp built as for the Pi but
# for the host, their main () renamed; under UBSan (ASan's shadow gap covers the kapi table's fixed
# address: the pure parts run under ASan in plug_run.sh).
#   sh tools/tests/koton/plug_host_run.sh      (KPLUG_SHOT=prefix: kp_fm2's and kp_arp's editors as they
#                                              drew them, prefix-fm2.ppm / prefix-arp.ppm)
set -e
cd "$(dirname "$0")/../../.."
K=user/Apps/koton
D=tools/tests/desktop_sim
OUT=${TMPDIR:-/tmp}/onyx_koton_plughost
mkdir -p "$OUT.o"
# the card the host scans: SD:/koton/plugins/<name>/{plugin.json, main}
CARD="$OUT.sd"
rm -rf "$CARD"
for n in fm2 delay arp; do
	mkdir -p "$CARD/koton/plugins/$n"
	cp user/Apps/kp_$n/plugin.json "$CARD/koton/plugins/$n/"
	echo "(the program)" > "$CARD/koton/plugins/$n/main"
done
CXX="g++ -std=gnu++17 -O1 -g -w -fno-exceptions -fno-rtti -fsanitize=undefined -pthread -I user -I $K -I kernel/include"
OBJS=""; PIDS=""
for n in fm2 delay arp; do
	$CXX -Dmain=kp_${n}_main -c user/Apps/kp_$n/main.cpp -o "$OUT.o/kp_$n.o" & PIDS="$PIDS $!"
	OBJS="$OBJS $OUT.o/kp_$n.o"
done
for f in $D/fakekapi.cpp $D/imgstub.cpp $(ls user/uikit/*.cpp | grep -v imgload) $K/plug/plughost.cpp $K/plug/plugctx.cpp \
	 $K/engine/*.cpp $K/synth/*.cpp tools/tests/koton/plug_host_test.cpp; do
	n=$(echo "$f" | tr '/' '_')
	$CXX -c "$f" -o "$OUT.o/$n.o" & PIDS="$PIDS $!"
	OBJS="$OBJS $OUT.o/$n.o"
done
for p in $PIDS; do wait $p || { echo "a compilation failed"; exit 1; }; done
$CXX -o "$OUT" $OBJS
mkdir -p "$CARD.sd"
SIM_SD="$CARD.sd" SIM_WRITES="$CARD" "$OUT"		# (an empty card: the real one carries the staged plugins)
