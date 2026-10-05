#!/bin/sh
# run_qbstudio_test.sh -- QBStudio's core on the PC (tools/tests/qbstudio/qbstudio_test.cpp): the sample project
# (sdcard/projects/converter) read, written back, laid out, its code generated, compiled by Onyx BASIC and run.
#
#   sh tools/tests/run_qbstudio_test.sh       (SHOW=1: the generated code and the run's log)
#
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BIN=${TMPDIR:-/tmp}/onyx_qbstudio_test
g++ -std=c++17 -O1 -g -Wall -Wno-format-truncation -fsanitize=address,undefined -I"$ROOT/user" -I"$ROOT/user/Kits" -I"$ROOT/user/Runtime" -I"$ROOT/user/Include" -I"$ROOT/user/Libs" -I"$ROOT/user/Emulators" -I"$ROOT/user/Ports" \
    "$ROOT"/user/Libs/basic/bascomp.cpp "$ROOT"/user/Libs/basic/basvm.cpp "$ROOT"/user/Libs/basic/basnum.cpp "$ROOT"/user/Libs/basic/basbax.cpp \
    "$HERE/qbstudio/qbstudio_test.cpp" -o "$BIN"
cd "$ROOT"
"$BIN" sdcard/projects/converter
