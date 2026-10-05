#!/bin/sh
# tools/icons/archiver_icon.sh -- sdcard/apps/archiver.app/icon.bmp from the app's own vector crate
# (archiver_icon.cpp: uikit's anti-aliased paths, on the PC). Needs g++ and python3.
set -e
cd "$(dirname "$0")/../.."
OUT=${OUT:-/tmp/onyx_icon}; mkdir -p "$OUT"
g++ -std=gnu++17 -O1 -w -I user -I user/Kits -I kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST -o "$OUT/archiver_icon" \
    tools/icons/archiver_icon.cpp user/Kits/uikit/*.cpp \
    tools/tests/desktop_sim/fakekapi.cpp -lpthread
"$OUT/archiver_icon" "$OUT/icon.ppm"
python3 - "$OUT/icon.ppm" <<'PY'
import sys, os
sys.path.insert (0, "tools")
import gen_assets
d = open (sys.argv[1], "rb").read ()
w, h = 40, 40
raw = d[d.index (b"255\n") + 4:]
px = [tuple (raw[i * 3:i * 3 + 3]) for i in range (w * h)]
gen_assets.write_bmp (os.path.join (gen_assets.APPS, "archiver.app", "icon.bmp"), w, h, px)
PY
