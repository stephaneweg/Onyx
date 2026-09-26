#!/bin/sh
# run_gb_test.sh -- the Game Boy core (user/gb) on the PC, against the public test ROMs
# (github.com/c-sp/game-boy-test-roms, a release unzipped; not kept in the repo):
#   GB_TEST_ROMS=/path/to/game-boy-test-roms tools/tests/run_gb_test.sh
#   * Blargg cpu_instrs / instr_timing: "Passed" on the serial port;
#   * halt_bug, dmg-acid2 (grey shades), cgb-acid2: the screen equal to the reference picture (needs python3-PIL).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
T=${TMPDIR:-/tmp}
R=${GB_TEST_ROMS:?set GB_TEST_ROMS to the unzipped game-boy-test-roms folder}
g++ -std=c++17 -O2 -Wall -I"$ROOT/user" "$HERE/gb/gbtest.cpp" "$ROOT/user/gb/gb.cpp" -o "$T/onyx_gbtest"
fail=0
for t in cpu_instrs/cpu_instrs.gb:60 instr_timing/instr_timing.gb:5; do
	rom=${t%%:*}; sec=${t##*:}
	out=$("$T/onyx_gbtest" "$R/blargg/$rom" "$sec" | tr -d '\r')
	echo "$out" | grep -q "Passed" && echo "ok   $rom" || { echo "FAIL $rom"; echo "$out" | tail -5; fail=1; }
done
cmp_png () {		# rom seconds reference.png
	"$T/onyx_gbtest" "$1" "$2" "$T/onyx_gb.ppm" >/dev/null
	python3 - "$T/onyx_gb.ppm" "$3" <<'PY' && echo "ok   $(basename "$1")" || { echo "FAIL $(basename "$1")"; fail=1; }
import sys
from PIL import Image, ImageChops
a = Image.open(sys.argv[1]).convert("RGB"); b = Image.open(sys.argv[2]).convert("RGB")
sys.exit(0 if ImageChops.difference(a, b).getbbox() is None else 1)
PY
}
if python3 -c "import PIL" 2>/dev/null; then
	cmp_png "$R/blargg/halt_bug.gb" 5 "$R/blargg/halt_bug-dmg-cgb.png"	# (a screen test)
	GB_GREY=1 cmp_png "$R/dmg-acid2/dmg-acid2.gb" 3 "$R/dmg-acid2/dmg-acid2-dmg.png"
	cmp_png "$R/cgb-acid2/cgb-acid2.gbc" 3 "$R/cgb-acid2/cgb-acid2.png"
else echo "(no python3-PIL: acid2 skipped)"; fi
exit $fail
