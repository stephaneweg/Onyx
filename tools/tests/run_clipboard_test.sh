#!/bin/sh
# tools/tests/run_clipboard_test.sh -- the shared clipboard (clipd, clipboard.h) on the PC: test.cpp over the
# desktop simulator's kapi with its in-process mailboxes (SIM_IPC=1) and a RAM: of its own.
set -e
cd "$(dirname "$0")/../.."
OUT=${OUT:-/tmp/onyx_clipboard_test}
mkdir -p "$OUT"; rm -rf "$OUT/ram"; mkdir -p "$OUT/ram"
g++ -std=gnu++17 -O1 -g -w -I user -I user/Kits -I user/Apps/clipd -I kernel/include -fno-exceptions -fno-rtti -DIMG_HOST_TEST \
    -o "$OUT/cliptest" tools/tests/clipboard/test.cpp tools/tests/desktop_sim/fakekapi.cpp -lpthread
SIM_IPC=1 SIM_RAM="$OUT/ram" timeout 60 "$OUT/cliptest"
