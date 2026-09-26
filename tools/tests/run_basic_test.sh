#!/bin/sh
# run_basic_test.sh -- Onyx BASIC on the PC: builds the core (user/basic) with a console
# host (basic/host_main.cpp) under AddressSanitizer + UBSan, runs every basic/progs/*.bas
# (stdin from <name>.in when present) and compares the output with <name>.out.
#   sh tools/tests/run_basic_test.sh [--update]      (--update rewrites the .out files)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BIN=${TMPDIR:-/tmp}/onyx_basic_host
g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I"$ROOT/user" \
    "$ROOT/user/basic/basnum.cpp" "$ROOT/user/basic/bascomp.cpp" "$ROOT/user/basic/basvm.cpp" \
    "$HERE/basic/host_main.cpp" -o "$BIN"
cd "$HERE/basic/progs"
fail=0
for bas in *.bas; do
	name=${bas%.bas}
	if [ -f "$name.in" ]; then out=$("$BIN" "$bas" cmdarg < "$name.in" 2>&1 || true)
	else out=$("$BIN" "$bas" cmdarg < /dev/null 2>&1 || true); fi
	if [ "$1" = "--update" ]; then printf '%s\n' "$out" > "$name.out"; echo "updated $name"; continue; fi
	if [ "$(printf '%s\n' "$out")" = "$(cat "$name.out")" ]; then echo "ok   $name"
	else echo "FAIL $name"; printf '%s\n' "$out" | diff "$name.out" - | head -20; fail=1; fi
done
exit $fail
