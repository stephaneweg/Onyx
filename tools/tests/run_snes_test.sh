#!/bin/sh
# run_snes_test.sh -- the Super Nintendo core (user/snes) on the PC, against PeterLemon's test
# ROMs (github.com/PeterLemon/SNES, cloned; not kept in the repo), each compared with the
# reference picture beside it (python3 + Pillow + numpy):
#   SNES_TEST_ROMS=/path/to/PeterLemon/SNES tools/tests/run_snes_test.sh
# the 65C816 (CPUTest/CPU: 23 ROMs), the SPC700 (CPUTest/SPC700: 7), still PPU pictures (BG
# maps, 8 bpp, HDMA colour / window / Mode 7 perspective...). The animated demos, the Super FX
# / MSU-1 ones and CPUMSC (it asks for a reset) are left out.
# gilyon's cputest (github.com/gilyon/snes-tests, built with cc65) runs with
#   SNES_CPUTEST=cputest-full.sfc -> its screen (Success / Failed + the test number) in
#   $TMPDIR/onyx_cputest.ppm.  With SNES_GAME=<rom.sfc> it also runs that game 20 s (its speed).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
T=${TMPDIR:-/tmp}
R=${SNES_TEST_ROMS:?set SNES_TEST_ROMS to the PeterLemon SNES folder}
g++ -std=c++17 -O2 -Wall -Wextra -I"$ROOT/user" "$HERE/snes/snestest.cpp" "$ROOT"/user/snes/*.cpp -o "$T/onyx_snestest"
fail=0
for rom in $(cd "$R" && ls CPUTest/CPU/*/*.sfc CPUTest/SPC700/*/*.sfc PPU/BGMAP/*/*/*/*.sfc PPU/BGMAP/*/*/*/*/*.sfc \
	PPU/HDMA/RedSpace*/*.sfc PPU/HDMA/HiColor64PerTileRow/*.sfc PPU/Window/WindowHDMA/*.sfc PPU/Mode7/Perspective/*.sfc \
	PPU/Mode7/HDMA/*.sfc PPU/Rings/*.sfc 2>/dev/null | grep -v CPUMSC); do
	ref="$R/${rom%.sfc}.png"
	[ -f "$ref" ] || continue
	"$T/onyx_snestest" "$R/$rom" 40 "$T/onyx_snes.ppm" >/dev/null
	d=$(python3 -c "
import numpy as np
from PIL import Image
o=np.asarray(Image.open('$T/onyx_snes.ppm').convert('RGB')).astype(int)[:224]
r=np.asarray(Image.open('$ref').convert('RGB')).astype(int)[:224]
print('%.2f'%((np.abs(r-o).max(axis=2)>24).mean()*100))")
	if [ "$d" = "0.00" ]; then echo "ok   $rom"; else echo "FAIL $rom: $d% of the pixels differ"; fail=1; fi
done
if [ -n "$SNES_CPUTEST" ]; then "$T/onyx_snestest" "$SNES_CPUTEST" 40 "$T/onyx_cputest.ppm"; echo "(its screen: $T/onyx_cputest.ppm)"; fi
if [ -n "$SNES_GAME" ]; then "$T/onyx_snestest" "$SNES_GAME" 20 "$T/onyx_game.ppm"; fi
exit $fail
