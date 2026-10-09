#!/bin/sh
# run_nds_test.sh -- the Nintendo DS core (user/Emulators/nds) on the PC: builds tools/tests/nds/ndstest
# and runs the test programs of tools/tests/nds/roms (made from the sources there: tools/tests/nds/build.sh
# with arm-none-eabi-gcc), each leaving its result in its picture's checksum or its text; with
# NDS_GAME=<rom.nds> it also runs that game 600 frames and prints its speed.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
T=${TMPDIR:-/tmp}
g++ -std=c++17 -O2 -g -Wall -Wextra -I"$ROOT/user" -I"$ROOT/user/Kits" -I"$ROOT/user/Runtime" -I"$ROOT/user/Include" -I"$ROOT/user/Libs" -I"$ROOT/user/Emulators" "$HERE/nds/ndstest.cpp" "$ROOT"/user/Emulators/nds/*.cpp -o "$T/onyx_ndstest"
echo "built $T/onyx_ndstest"
if [ -d "$HERE/nds/roms" ]; then sh "$HERE/nds/check.sh" "$T/onyx_ndstest"; fi
if [ -n "$NDS_GAME" ]; then "$T/onyx_ndstest" "$NDS_GAME" 600 "$T/onyx_nds_game.ppm"; fi
