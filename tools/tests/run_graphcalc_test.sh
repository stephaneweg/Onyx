#!/bin/sh
# run_graphcalc_test.sh -- the graphing calculator's expression parser on the PC.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BIN=${TMPDIR:-/tmp}/onyx_graphcalc_test
g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I"$ROOT/user" \
    "$HERE/graphcalc/expr_test.cpp" "$ROOT/user/basic/basnum.cpp" -o "$BIN"
"$BIN"
