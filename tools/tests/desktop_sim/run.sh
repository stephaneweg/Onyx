#!/bin/sh
# tools/tests/desktop_sim/run.sh -- the modernised CDE desktop on the PC, without a Pi:
#   * the kernel's window manager and compositor (kernel/gui/window.cpp) built for the host with
#     stand-ins for Circle (kstub/): its checks (wmtest.cpp) and a picture of what it draws;
#   * wtk apps built for the host against a stand-in kernel (fakekapi.cpp), run through a script
#     of events, their windows (frame + client) dumped, then laid over the wallpaper (compose.py).
# Needs g++, python3 with Pillow + numpy. Usage: sh tools/tests/desktop_sim/run.sh [out dir]
set -e
cd "$(dirname "$0")/../../.."
OUT=${1:-/tmp/desktop_sim}
mkdir -p "$OUT"
D=tools/tests/desktop_sim

# the kernel: its checks
g++ -std=gnu++17 -O1 -g -w -I $D/kstub -I kernel/include -o "$OUT/wmtest" $D/wmtest.cpp kernel/gui/window.cpp kernel/gui/gimage.cpp
"$OUT/wmtest" "$OUT"

# the apps (wtk) on the PC
CXX="g++ -std=gnu++17 -O1 -w -I user -I kernel/include -fno-exceptions -fno-rtti"
WTK="$D/imgstub.cpp $(ls user/wtk/*.cpp | grep -v imgload)"	# (stb's allocator would shadow the host's)
$CXX -o "$OUT/gallery" $D/fakekapi.cpp $D/gallery/main.cpp $WTK
SIM_POS=100,100 SIM="wait;wait;dump $OUT/gallery.elsm;exit" "$OUT/gallery"
SIM_POS=100,100 SIM="wait;wait;winctl 0;move 40 40;wait;dump $OUT/gallery-menu.elsm;exit" "$OUT/gallery"
python3 $D/compose.py "$OUT/gallery.png" "$OUT/gallery.elsm"
python3 $D/compose.py "$OUT/gallery-menu.png" "$OUT/gallery-menu.elsm"
echo "desktop_sim: done ($OUT)"
