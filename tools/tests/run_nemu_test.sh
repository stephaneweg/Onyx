#!/bin/sh
# run_nemu_test.sh -- NintendoEMU's native core (pc/NintendoEMU/core/nemucore.cpp) on Linux: its
# C API as the Windows front end uses it, over every ROM of $NEMU_ROMS (not kept in the repo):
# each run 120 frames (a picture must come out; the speed and the sound are shown), then the
# library's picture of it. The Windows build itself: sh pc/build.sh (Wine can run pc/dist).
#   NEMU_ROMS=/path/to/roms tools/tests/run_nemu_test.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
T=${TMPDIR:-/tmp}
R=${NEMU_ROMS:?set NEMU_ROMS to a folder of ROMs}
g++ -std=c++17 -O2 -Wall -Wextra -I"$ROOT/user" -I"$ROOT/user/Kits" "$HERE/nemu/nemutest.cpp" "$ROOT/pc/NintendoEMU/core/nemucore.cpp" "$ROOT/pc/NintendoEMU/core/gxgl.cpp" \
	"$ROOT/user/gb/gb.cpp" "$ROOT"/user/gba/*.cpp "$ROOT"/user/nes/*.cpp "$ROOT"/user/snes/*.cpp \
	"$ROOT"/user/n64/*.cpp "$ROOT"/user/gc/*.cpp -o "$T/onyx_nemutest"
fail=0
find "$R" -type f \( -iname '*.gb' -o -iname '*.gbc' -o -iname '*.gba' -o -iname '*.nes' -o -iname '*.sfc' -o -iname '*.smc' \
	-o -iname '*.z64' -o -iname '*.n64' -o -iname '*.v64' -o -iname '*.dol' \) | sort > "$T/onyx_nemu.list"
while read -r rom; do
	if "$T/onyx_nemutest" "$rom" 120 >"$T/onyx_nemu.log" && "$T/onyx_nemutest" thumb "$rom" >>"$T/onyx_nemu.log"; then
		echo "ok   $(basename "$rom"): $(sed -n 2p "$T/onyx_nemu.log")"
	else echo "FAIL $(basename "$rom"): $(tail -1 "$T/onyx_nemu.log")"; fail=1; fi
done < "$T/onyx_nemu.list"
exit $fail
