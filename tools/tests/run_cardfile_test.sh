#!/bin/sh
# run_cardfile_test.sh -- Cardfile's document (user/Apps/cardfile/model.h) on the PC: values, files, CSV, order.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BIN=${TMPDIR:-/tmp}/onyx_cardfile_test
g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-function -fsanitize=address,undefined -I"$ROOT/user" -I"$ROOT/user/Kits" \
    "$HERE/cardfile/model_test.cpp" -o "$BIN"
"$BIN"
