#!/bin/sh
# tools/tests/json/run.sh -- user/json.hpp's tests on the PC under ASan / LSan (a leak fails).
#   sh tools/tests/json/run.sh [more JSON files to round-trip]
set -e
cd "$(dirname "$0")/../../.."
OUT=${TMPDIR:-/tmp}/onyx_json_test
g++ -std=gnu++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I user -I user/Kits -o "$OUT" tools/tests/json/test_json.cpp
"$OUT" "$@"
