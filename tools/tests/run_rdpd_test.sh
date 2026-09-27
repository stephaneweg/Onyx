#!/bin/sh
# Host test of user/bin/rdpd.c (the window-level remote desktop server): built against a mock
# kapi (real sockets, two made-up windows: rdpd/mock_rdpd.h) and driven by rdpd/rdpd_test.py
# (the protocol, LZ4 decoded, the pixels, the changed tile only, the pointer put back).
set -e
here=$(cd "$(dirname "$0")" && pwd)
b=$(mktemp -d)
cp "$here/mock_kapi.h" "$here/mock_net_kapi.h" "$here/rdpd/mock_rdpd.h" "$here/../../user/bin/remotekeys.h" "$b/"
{ cat "$here/mock_net_kapi.h"; echo '#include "mock_rdpd.h"'; } > "$b/kapi.h"
sed -e 's|^int main (void)|static int rdpd_main (void)|' "$here/../../user/bin/rdpd.c" > "$b/rdpd.c"
cat > "$b/main.c" <<'X'
#include "rdpd.c"
int main (int argc, char **argv) { snprintf (mock_args, sizeof mock_args, "%s", argc > 1 ? argv[1] : ""); return rdpd_main (); }
X
g++ -w -I"$b" -x c++ "$b/main.c" -o "$b/rdpd"
"$b/rdpd" 3391 > "$b/log" & pid=$!
trap 'kill $pid 2>/dev/null; rm -rf "$b"' EXIT
sleep 0.5
python3 "$here/rdpd/rdpd_test.py" 3391 "$b/log"
