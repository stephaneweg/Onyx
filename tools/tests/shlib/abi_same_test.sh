#!/bin/sh
# abi_same_test.sh -- the two UIKits are one interface (docs/POCKETUI-TECH-STUDY.md section 5.4): the desktop's
# lib/uikit.so and the pocket one lib/pocket/uikit.so (PocketUI's port), built by user/Makefile from the same
# objects but their port, must have the same export table slot by slot -- tools/libgen/abi_same.py, which
# `make libs` runs too. Then the checker is checked: two libraries that differ (UIKit against SystemKit, a
# generated table with two lines swapped) must be told apart.
#
#   sh tools/tests/shlib/abi_same_test.sh          (after `make` in kernel/ or `make libs` in user/)
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
L=$ROOT/user/lib
A=$ROOT/tools/libgen/abi_same.py
T=${TMPDIR:-/tmp}/abi_same_test.$$
mkdir -p "$T"
fail=0
[ -f "$L/uikit.so" ] && [ -f "$L/pocket/uikit.so" ] || { echo "abi_same_test: build the libraries first (user/: make libs)"; exit 1; }

if python3 "$A" "$L/uikit.so" "$L/pocket/uikit.so" --tables "$L/uikit_table.S" "$L/pocket/uikit_table.S"; then echo "PASS the two UIKits: the same table"
else echo "FAIL the two UIKits differ"; fail=1; fi

if python3 "$A" "$L/uikit.so" "$L/systemkit.so" >"$T/out" 2>&1; then echo "FAIL UIKit and SystemKit taken for one interface"; fail=1
else echo "PASS UIKit and SystemKit told apart ($(tail -n 1 "$T/out" | cut -c1-60)...)"; fi

# a table with two entries swapped (what a function added to one library only, or a reordered .abi, would give)
awk '/\.quad/ { n++; if (n == 3) { held = $0; next } if (n == 4) { print; print held; next } } { print }' "$L/pocket/uikit_table.S" > "$T/swapped.S"
if python3 "$A" "$L/uikit.so" "$L/pocket/uikit.so" --tables "$L/uikit_table.S" "$T/swapped.S" >"$T/out" 2>&1; then echo "FAIL a swapped table taken for the same"; fail=1
else echo "PASS a swapped table told apart"; fi

rm -rf "$T"
[ $fail = 0 ] && echo "abi_same_test: all passed" || echo "abi_same_test: FAILED"
exit $fail
