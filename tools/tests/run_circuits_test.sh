#!/bin/sh
# run_circuits_test.sh -- Circuits' engine on the PC (tools/tests/circuits/circuitstest.cpp, user/Apps/circuits/
# circuit.cpp): the board, the evaluation, the Check, the circuit text, undo, the routes and hit tests; then every
# level of the card's packs (or of the packs given) read, its table, its reference solution won with three stars.
# ASan / UBSan; no warning allowed in the app's sources. No kapi: the engine does no I/O (FileKit's fk_kv_* inline).
#
#   sh tools/tests/run_circuits_test.sh [pack.circuits ...]     -> "ok   circuits (N checks ...)" (exit 0)
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
#
set -e
cd "$(dirname "$0")/../.."
BIN=${TMPDIR:-/tmp}/onyx_circuits_test
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Apps -I kernel/include \
    tools/tests/circuits/circuitstest.cpp user/Apps/circuits/circuit.cpp -o "$BIN"
if [ $# -gt 0 ]; then "$BIN" "$@"; else "$BIN" sdcard/apps/circuits.app/levels/*.circuits; fi
