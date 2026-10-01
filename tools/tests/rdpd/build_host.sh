#!/bin/sh
# tools/tests/rdpd/build_host.sh [OUT [RDPD_SOURCE]] -- rdpd built for the PC (rdpdhost.c: real
# sockets, fake windows) -> OUT/rdpd_host (default /tmp/rdpd_host). RDPD_SOURCE: another rdpd.c
# (e.g. `git show <old>:user/bin/rdpd.c > old.c`) to test an older server against a client.
# Then: OUT/rdpd_host [port] (RDPD_ANIM=1: a window redrawn every 40 ms), and
#   python3 tools/tests/rdpd/pipeline_test.py   (the protocol checks: lock-step, pipelined, PING)
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
OUT=${1:-/tmp/rdpd_host}
SRC=${2:-$ROOT/user/bin/rdpd.c}
mkdir -p "$OUT"
gcc -std=gnu11 -O1 -g -Wall -Wno-unused-function -Wno-format-truncation -I "$ROOT/user" -I "$ROOT/user/bin" \
	-I "$ROOT/kernel/include" -DRDPD_SRC="\"$SRC\"" -o "$OUT/rdpd_host" "$ROOT/tools/tests/rdpd/rdpdhost.c" -lpthread
echo "built: $OUT/rdpd_host"
