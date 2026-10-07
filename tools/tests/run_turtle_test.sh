#!/bin/sh
# run_turtle_test.sh -- Turtle Quest's engine on the PC (tools/tests/turtle/turtletest.cpp): every level of the card's
# packs solved by its own solution, with three stars; the player's errors made friendly.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BIN=${TMPDIR:-/tmp}/onyx_turtle_test
B=$ROOT/user/Libs/basic
g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I"$ROOT/user" -I"$ROOT/user/Apps" -I"$ROOT/user/Libs" \
    "$B/basnum.cpp" "$B/bascomp.cpp" "$B/basvm.cpp" "$B/basbax.cpp" "$HERE/turtle/turtletest.cpp" -o "$BIN"
"$BIN" "$ROOT"/sdcard/apps/turtle.app/levels/*.turtle
