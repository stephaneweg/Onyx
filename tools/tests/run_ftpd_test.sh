#!/bin/sh
# Host test of user/bin/ftpd.c: built against a mock kapi (real sockets + a temp dir as
# the SD card), started on port 21 (root) and driven by Python's ftplib (ftpd_test.py).
set -e
here=$(cd "$(dirname "$0")" && pwd)
b=$(mktemp -d)
sed -e 's|#include "applib.h"|#include "mini_applib.h"|' -e 's|#include "umm.h"|#include "mini_umm.h"|' \
    -e 's|^int main (void)|static int ftpd_main (void)|' -e 's|^static int session (char \*a)|static int mock_session (char *a)|' \
    -e 's|return session (a);|return mock_session (a);|' "$here/../../user/bin/ftpd.c" > "$b/ftpd.c"
cp "$here/mock_kapi.h" "$here/mock_net_kapi.h" "$here/mini_applib.h" "$here/mini_umm.h" "$b/"
cp "$here/mock_net_kapi.h" "$b/kapi.h"
cat > "$b/main.c" <<'X'
#include "ftpd.c"
int main (int argc, char **argv) { snprintf (mock_args, sizeof mock_args, "%s", argc > 1 ? argv[1] : ""); return ftpd_main (); }
X
g++ -w -I"$b" -x c++ "$b/main.c" -o "$b/ftpd"
rm -rf /tmp/onyx_mock && mkdir -p /tmp/onyx_mock/pub && echo "hello onyx" > /tmp/onyx_mock/pub/readme.txt
"$b/ftpd" "SD:/pub tester secret" & pid=$!
trap 'kill $pid 2>/dev/null; pkill -P $pid 2>/dev/null; rm -rf "$b"' EXIT
sleep 0.5
set +e
timeout 60 python3 "$here/ftpd_test.py"; rc=$?
[ $rc -eq 124 ] && echo "FAIL: timed out (a lost read? see mock_net_kapi.h)"
exit $rc
