#!/bin/sh
# run_basic_test.sh -- Onyx BASIC on the PC: builds the core (user/Libs/basic) with a console
# host (basic/host_main.cpp) under AddressSanitizer + UBSan, runs every basic/progs/*.bas
# (stdin from <name>.in when present; the GUI's events from <name>.events) and compares the output with <name>.out.
#   sh tools/tests/run_basic_test.sh [--update]      (--update rewrites the .out files)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BIN=${TMPDIR:-/tmp}/onyx_basic_host
g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I"$ROOT/user" -I"$ROOT/user/Kits" -I"$ROOT/user/Runtime" -I"$ROOT/user/Include" -I"$ROOT/user/Libs" -I"$ROOT/user/Emulators" -I"$ROOT/user/Ports" \
    "$ROOT/user/Libs/basic/basnum.cpp" "$ROOT/user/Libs/basic/bascomp.cpp" "$ROOT/user/Libs/basic/basvm.cpp" "$ROOT/user/Libs/basic/basbax.cpp" \
    -DGK_STANDALONE "$ROOT/user/Kits/gpiokit/gkcore.cpp" "$HERE/basic/host_main.cpp" -o "$BIN"
cd "$HERE/basic/progs"
fail=0
for bas in *.bas; do
	name=${bas%.bas}
	if [ -f "$name.events" ]; then export EVENTS="$(cat "$name.events")"; else unset EVENTS; fi
	if [ -f "$name.in" ]; then out=$("$BIN" "$bas" cmdarg < "$name.in" 2>&1 || true)
	else out=$("$BIN" "$bas" cmdarg < /dev/null 2>&1 || true); fi
	if [ "$1" = "--update" ]; then printf '%s\n' "$out" > "$name.out"; echo "updated $name"; continue; fi
	if [ "$(printf '%s\n' "$out")" = "$(cat "$name.out")" ]; then echo "ok   $name"
	else echo "FAIL $name"; printf '%s\n' "$out" | diff "$name.out" - | head -20; fail=1; fi
done
# the same programs through a .bax (compiled, saved, loaded back): the same output
if [ "$1" != "--update" ]; then
	for bas in *.bas; do
		name=${bas%.bas}
		if [ -f "$name.events" ]; then export EVENTS="$(cat "$name.events")"; else unset EVENTS; fi
		if [ -f "$name.in" ]; then out=$(BAX=1 "$BIN" "$bas" cmdarg < "$name.in" 2>&1 || true)
		else out=$(BAX=1 "$BIN" "$bas" cmdarg < /dev/null 2>&1 || true); fi
		if [ "$(printf '%s\n' "$out")" = "$(cat "$name.out")" ]; then echo "ok   $name (.bax)"
		else echo "FAIL $name (.bax)"; printf '%s\n' "$out" | diff "$name.out" - | head -20; fail=1; fi
	done
fi
exit $fail
