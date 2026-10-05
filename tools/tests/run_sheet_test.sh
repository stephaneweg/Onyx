#!/bin/sh
# run_sheet_test.sh -- the spreadsheet's engine (user/Apps/sheet/) on the PC: formulas, functions, formats,
# typed entries, files (the sample workbook sdcard/docs/cafe-2026.xlsx too; with LibreOffice installed,
# its .ods and .xlsx). ASan + UBSan.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BIN=${TMPDIR:-/tmp}/onyx_sheet_test
g++ -std=gnu++17 -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -fsanitize=address,undefined -I"$ROOT/user" -I"$ROOT/user/Kits" \
    "$HERE/sheet/engine_test.cpp" -o "$BIN" -lm
"$BIN"
g++ -std=gnu++17 -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare -fsanitize=address,undefined -I"$ROOT/user" -I"$ROOT/user/Kits" \
    "$HERE/sheet/files_test.cpp" -o "$BIN-files" -lm
D=${TMPDIR:-/tmp}/onyx_sheet_files; rm -rf "$D"; mkdir -p "$D"
"$BIN-files" "$D" "$ROOT/sdcard/docs/cafe-2026.xlsx"
