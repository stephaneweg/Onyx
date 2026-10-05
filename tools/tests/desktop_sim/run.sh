#!/bin/sh
# tools/tests/desktop_sim/run.sh -- the modernised CDE desktop on the PC, without a Pi:
#   * the window manager and compositor (Elegant's: user/Servers/elegant/wm/window.cpp) built for the host with
#     stand-ins for Circle (kstub/): its checks (wmtest.cpp) and a picture of what it draws;
#   * uikit apps built for the host against a stand-in kernel (fakekapi.cpp), run through a script
#     of events, their windows (frame + client) dumped, then laid over the wallpaper (compose.py).
# Needs g++, python3 with Pillow + numpy. Usage: sh tools/tests/desktop_sim/run.sh [out dir]
set -e
cd "$(dirname "$0")/../../.."
OUT=${1:-/tmp/desktop_sim}
mkdir -p "$OUT"
D=tools/tests/desktop_sim

# the kernel: its checks
g++ -std=gnu++17 -O1 -g -w -I $D/kstub -I user/Servers/elegant/wm -I kernel/include -o "$OUT/wmtest" $D/wmtest.cpp user/Servers/elegant/wm/window.cpp kernel/gui/gimage.cpp
"$OUT/wmtest" "$OUT"

# the apps (uikit) on the PC
CXX="g++ -std=gnu++17 -O1 -w -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -I kernel/include -fno-exceptions -fno-rtti"
UIKIT="$D/imgstub.cpp $(ls user/Kits/uikit/*.cpp | grep -v imgload)"	# (stb's allocator would shadow the host's)
$CXX -o "$OUT/gallery" $D/fakekapi.cpp $D/gallery/main.cpp $UIKIT
SIM_POS=100,100 SIM="wait;wait;dump $OUT/gallery.elsm;exit" "$OUT/gallery"
SIM_POS=100,100 SIM="wait;wait;winctl 0;move 40 40;wait;dump $OUT/gallery-menu.elsm;exit" "$OUT/gallery"
python3 $D/compose.py "$OUT/gallery.png" "$OUT/gallery.elsm"
python3 $D/compose.py "$OUT/gallery-menu.png" "$OUT/gallery-menu.elsm"
echo "desktop_sim: done ($OUT)"
