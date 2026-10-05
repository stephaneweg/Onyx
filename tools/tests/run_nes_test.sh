#!/bin/sh
# run_nes_test.sh -- the NES core (user/nes) on the PC, against the classic test ROMs
# (github.com/christopherpow/nes-test-roms, cloned; not kept in the repo):
#   NES_TEST_ROMS=/path/to/nes-test-roms tools/tests/run_nes_test.sh
# nestest (the CPU against its reference trace), then blargg's ROMs, which report through
# $6000 / $6004: the instructions and their timing, the APU (length counters, frame IRQ,
# DMC) and the MMC3 IRQ. With NES_GAME=<rom.nes> it also runs that game 20 s (its speed).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
T=${TMPDIR:-/tmp}
R=${NES_TEST_ROMS:?set NES_TEST_ROMS to the nes-test-roms folder}
g++ -std=c++17 -O2 -Wall -Wextra -I"$ROOT/user" -I"$ROOT/user/Kits" "$HERE/nes/nestest.cpp" "$ROOT"/user/nes/*.cpp -o "$T/onyx_nestest"
fail=0
if "$T/onyx_nestest" cpu "$R/other/nestest.nes" "$R/other/nestest.log" | grep -q "all match"; then echo "ok   nestest"
else echo "FAIL nestest"; fail=1; fi
for t in instr_test-v5/all_instrs instr_timing/instr_timing \
	apu_test/rom_singles/1-len_ctr apu_test/rom_singles/2-len_table apu_test/rom_singles/3-irq_flag \
	apu_test/rom_singles/4-jitter apu_test/rom_singles/5-len_timing apu_test/rom_singles/6-irq_flag_timing \
	apu_test/rom_singles/7-dmc_basics apu_test/rom_singles/8-dmc_rates \
	mmc3_test/1-clocking mmc3_test/2-details mmc3_test/3-A12_clocking mmc3_test/5-MMC3; do
	out=$("$T/onyx_nestest" run "$R/$t.nes" 40 2>&1)
	if echo "$out" | grep -q -E "Passed|All [0-9]+ tests passed"; then echo "ok   $t"
	else echo "FAIL $t: $(echo "$out" | grep -E 'Failed|status' | tail -1)"; fail=1; fi
done
if [ -n "$NES_GAME" ]; then "$T/onyx_nestest" run "$NES_GAME" 20 "$T/onyx_nes.ppm"; fi
exit $fail
