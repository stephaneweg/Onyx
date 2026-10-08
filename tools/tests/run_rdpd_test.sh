#!/bin/sh
# Host test of user/BinUtils/rdpd.c (the window-level remote desktop server): built against a mock
# kapi (real sockets, two made-up windows: rdpd/mock_rdpd.h) and driven by rdpd/rdpd_test.py
# (the protocol, LZ4 decoded, the pixels, the changed tile only, the pointer put back); then rdpd/rdpd_pocket_test.py
# under a mock PocketUI (the flags told to Onyx Remote: the windows that take the PC's keys).
set -e
here=$(cd "$(dirname "$0")" && pwd)
b=$(mktemp -d)
cp "$here/mock_kapi.h" "$here/mock_net_kapi.h" "$here/rdpd/mock_rdpd.h" "$here/../../user/BinUtils/remotekeys.h" "$b/"
{ cat "$here/mock_net_kapi.h"; echo '#include "mock_rdpd.h"'; } > "$b/kapi.h" && mkdir -p "$b/appkit" && echo '#include "../kapi.h"' > "$b/appkit/appkit.h"
mkdir -p "$b/uikit" && echo '/* (the window API, uk_win_*: mock_rdpd.h) */' > "$b/uikit/win.h"
sed -e 's|^int main (void)|static int rdpd_main (void)|' "$here/../../user/BinUtils/rdpd.c" > "$b/rdpd.c"
cat > "$b/main.c" <<'X'
#include "rdpd.c"
int main (int argc, char **argv) { snprintf (mock_args, sizeof mock_args, "%s", argc > 1 ? argv[1] : ""); return rdpd_main (); }
X
g++ -w -I"$b" -x c++ "$b/main.c" -o "$b/rdpd"
"$b/rdpd" 3391 > "$b/log" & pid=$!
MOCK_POCKET=1 "$b/rdpd" 3392 > "$b/log1" & pid1=$!
MOCK_POCKET=2 "$b/rdpd" 3393 > "$b/log2" & pid2=$!
MOCK_POCKET=3 "$b/rdpd" 3394 > "$b/log3" & pid3=$!
MOCK_POCKET=4 "$b/rdpd" 3395 > "$b/log4" & pid4=$!
trap 'kill $pid $pid1 $pid2 $pid3 $pid4 2>/dev/null; rm -rf "$b"' EXIT
sleep 0.5
python3 "$here/rdpd/rdpd_test.py" 3391 "$b/log"
# under PocketUI (the mock's MOCK_POCKET): the frameless main windows and the home told as plain ones (the keys)
python3 "$here/rdpd/rdpd_pocket_test.py" 3392 "$b/log1" 1
python3 "$here/rdpd/rdpd_pocket_test.py" 3393 "$b/log2" 2
# a program with the full screen (PocketUI: a BASIC game; the desktop: an emulator): told alone, a plain window
python3 "$here/rdpd/rdpd_pocket_test.py" 3394 "$b/log3" 3
python3 "$here/rdpd/rdpd_pocket_test.py" 3395 "$b/log4" 4
