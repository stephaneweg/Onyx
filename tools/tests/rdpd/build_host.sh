#!/bin/sh
# tools/tests/rdpd/build_host.sh [OUT [RDPD_SOURCE]] -- rdpd built for the PC (rdpdhost.c: real
# sockets, fake windows) -> OUT/rdpd_host (default /tmp/rdpd_host). RDPD_SOURCE: another rdpd.c
# (e.g. `git show <old>:user/BinUtils/rdpd.c > old.c`) to test an older server against a client.
# Then: OUT/rdpd_host [port] (RDPD_ANIM=1: a window redrawn every 40 ms), and
#   python3 tools/tests/rdpd/pipeline_test.py   (the protocol checks: lock-step, pipelined, PING)
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
OUT=${1:-/tmp/rdpd_host}
SRC=${2:-$ROOT/user/BinUtils/rdpd.c}
mkdir -p "$OUT"
INC="-I $ROOT/user -I $ROOT/user/Kits -I $ROOT/user/Runtime -I $ROOT/user/Include -I $ROOT/user/Libs -I $ROOT/user/Emulators -I $ROOT/user/Ports -I $ROOT/user/BinUtils -I $ROOT/kernel/include"
gcc -std=gnu11 -O1 -g -Wall -Wno-unused-function -Wno-format-truncation $INC -DRDPD_SRC="\"$SRC\"" -c "$ROOT/tools/tests/rdpd/rdpdhost.c" -o "$OUT/rdpdhost.o"
# (the window API is UIKit's, uk_win_*: its entries and its port, relaying to rdpdhost.c's table on a PC)
for f in win port; do g++ -std=gnu++17 -O1 -g -w -fno-exceptions -fno-rtti $INC -c "$ROOT/user/Kits/uikit/$f.cpp" -o "$OUT/uk_$f.o"; done
g++ -o "$OUT/rdpd_host" "$OUT/rdpdhost.o" "$OUT/uk_win.o" "$OUT/uk_port.o" -lpthread
echo "built: $OUT/rdpd_host"
