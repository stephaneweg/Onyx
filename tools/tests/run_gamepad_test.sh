#!/bin/sh
# run_gamepad_test.sh -- user/Include/gamepad.h on the PC: generic pads (axes / hat d-pad), a pad's own
# section and [default] of SD:/etc/gamepad.ini, a pad Circle knows, the keyboard focus.
# (fake_kapi.h stands for user/kapi.h: gamepad.h includes "kapi.h" from its own folder.)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
T=${TMPDIR:-/tmp}/onyx_gamepad
mkdir -p "$T"
cp "$ROOT/user/Include/gamepad.h" "$T/gamepad.h"
cp "$HERE/gamepad/fake_kapi.h" "$T/kapi.h" && mkdir -p "$T/appkit" && echo '#include "../kapi.h"' > "$T/appkit/appkit.h"
g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I"$T" -I"$ROOT/kernel/include" "$HERE/gamepad/gamepad_test.cpp" -o "$T/gamepad_test"
"$T/gamepad_test"
