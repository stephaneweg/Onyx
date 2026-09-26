#!/bin/sh
# run_gba_test.sh -- the Game Boy Advance core (user/gba) on the PC, against jsmolka's
# gba-tests (github.com/jsmolka/gba-tests, cloned or unzipped; not kept in the repo):
#   GBA_TEST_ROMS=/path/to/gba-tests tools/tests/run_gba_test.sh
# arm, thumb, memory, bios, nes (the prefetch pipeline), unsafe and the saves (none, SRAM,
# Flash 64 / 128 KB): each test leaves its result in r12 (0 = all passed).
# With GBA_GAME=<rom.gba> it also runs that game 20 s and prints its speed.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
T=${TMPDIR:-/tmp}
R=${GBA_TEST_ROMS:?set GBA_TEST_ROMS to the gba-tests folder}
g++ -std=c++17 -O2 -Wall -Wextra -I"$ROOT/user" "$HERE/gba/gbatest.cpp" "$ROOT"/user/gba/*.cpp -o "$T/onyx_gbatest"
fail=0
for t in arm/arm thumb/thumb memory/memory bios/bios nes/nes unsafe/unsafe save/none save/sram save/flash64 save/flash128; do
	out=$(GBA_REGS=1 "$T/onyx_gbatest" "$R/$t.gba" 3 "$T/onyx_gba.ppm")
	if echo "$out" | grep -q "r12=00000000"; then echo "ok   $t"
	else echo "FAIL $t: test $(echo "$out" | grep -o 'r12=[0-9a-f]*')"; fail=1; fi
done
if [ -n "$GBA_GAME" ]; then "$T/onyx_gbatest" "$GBA_GAME" 20 "$T/onyx_game.ppm"; fi
exit $fail
