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
g++ -std=c++17 -O1 -g -Wall -Wno-format-truncation -fsanitize=address,undefined -I"$ROOT/user" \
    "$ROOT"/user/basic/bascomp.cpp "$ROOT"/user/basic/basvm.cpp "$ROOT"/user/basic/basnum.cpp "$ROOT"/user/basic/basbax.cpp \
    "$HERE/qbstudio/qbstudio_test.cpp" -o "$BIN"
cd "$ROOT"
"$BIN" sdcard/projects/converter
