#!/bin/sh
# run_pinball_test.sh -- Pinball's core on the PC (tools/tests/pinball/pinballtest.cpp; user/Apps/pinball/table.cpp,
# physics.cpp, rules.cpp, scores.cpp): the table reader and its errors, the physics (determinism, containment, no dead
# spot, no tunnelling through walls or flippers, the plunger), every toy, the rules, the high scores -- 02 §12 AC 1-27.
# Built twice: ASan / UBSan at -O1 running every case with reduced counts (--quick), then -O2 running the full counts.
# No warning allowed in the app's sources; no fused multiply-add (-ffp-contract=off: the same bits as on the Pi). No
# kapi: the core does no I/O (FileKit's fk_kv_* inline; the test reads the tables itself).
#
#   sh tools/tests/run_pinball_test.sh [file.table ...]     -> "ok   pinball (N checks ...)" (exit 0)
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
#
set -e
cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}
SRC="user/Apps/pinball/table.cpp user/Apps/pinball/physics.cpp user/Apps/pinball/rules.cpp user/Apps/pinball/scores.cpp"
INC="-I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Apps -I kernel/include"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    $INC tools/tests/pinball/pinballtest.cpp $SRC -o "$OUT/onyx_pinball_asan"
g++ -std=c++17 -O2 -Wall -Wextra -Werror -ffp-contract=off $INC tools/tests/pinball/pinballtest.cpp $SRC -o "$OUT/onyx_pinball"
if [ $# -gt 0 ]; then T="$*"; else T=$(ls sdcard/apps/pinball.app/tables/*.table); fi
"$OUT/onyx_pinball_asan" --quick $T
"$OUT/onyx_pinball" $T
