#!/bin/sh
# Host test of user/bin/ftpfs.cpp (the FTP: file-system provider), plain FTP, against
#   1. pyftpdlib (a third-party server: MLSD, LIST formats), port 2121
#   2. our own ftpd (built like run_ftpd_test.sh), port 21 (needs root)
# TLS is stubbed out here (FTPS is exercised on hardware only).
set -e
here=$(cd "$(dirname "$0")" && pwd)
b=$(mktemp -d)
root=$(mktemp -d)
cleanup () { kill $srv 2>/dev/null || true; kill $ftpd 2>/dev/null || true; pkill -P $ftpd 2>/dev/null || true; rm -rf "$b" "$root"; }
trap cleanup EXIT
cp "$here/../../user/bin/ftpfs.cpp" "$here/ftpfs_test.cpp" "$here/mock_kapi.h" "$here/mock_net_kapi.h" "$b/"
mkdir -p "$b/tls" && cp "$here/stub_tls.hpp" "$b/tls/onyx_tls.hpp"
cat "$here/mock_net_kapi.h" > "$b/kapi.h"
cat >> "$b/kapi.h" <<'X'
struct kapi_vfs_req { unsigned id; int op; char path[300]; char path2[300]; long a0, a1, a2; unsigned in_len; };
#define VFS_OP_OPEN 1
#define VFS_OP_READ 2
#define VFS_OP_CLOSE 3
#define VFS_OP_LIST 4
#define VFS_OP_SAVE 5
#define VFS_OP_MKDIR 6
#define VFS_OP_REMOVE 7
#define VFS_OP_RENAME 8
static inline int kapi_vfs_register (const char *) { return 1; }
static inline int kapi_vfs_next (struct kapi_vfs_req *, int) { return 0; }
int kapi_vfs_req_data (unsigned, void *, unsigned, unsigned);
static inline int kapi_vfs_reply (unsigned, int, const void *, unsigned) { return 1; }
X
sed -i 's|^int main (void)|static int ftpfs_main (void)|' "$b/ftpfs.cpp"
g++ -w -I"$b" "$b/ftpfs_test.cpp" -o "$b/t"
# 1. pyftpdlib
mkdir -p "$root/sub" && printf 'hello onyx\n' > "$root/readme.txt"
python3 -c "
from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import FTPServer
import logging; logging.disable(logging.CRITICAL)
a = DummyAuthorizer(); a.add_user('tester', 'secret', '$root', perm='elradfmwMT')
h = FTPHandler; h.authorizer = a
FTPServer(('127.0.0.1', 2121), h).serve_forever()" & srv=$!
sleep 1
echo "--- against pyftpdlib"
timeout 60 "$b/t" "FTP:tester:secret@127.0.0.1:2121"
# 2. our ftpd (its mock maps SD:/ to /tmp/onyx_mock)
echo "--- against ftpd"
sed -e 's|#include "applib.h"|#include "mini_applib.h"|' -e 's|#include "umm.h"|#include "mini_umm.h"|' \
    -e 's|^int main (void)|static int ftpd_main (void)|' -e 's|^static int session (char \*a)|static int mock_session (char *a)|' \
    -e 's|return session (a);|return mock_session (a);|' "$here/../../user/bin/ftpd.c" > "$b/ftpd.c"
cp "$here/mini_applib.h" "$here/mini_umm.h" "$b/"
mkdir -p "$b/srv" && cp "$here/mock_kapi.h" "$here/mini_applib.h" "$here/mini_umm.h" "$b/srv/" && cp "$here/mock_net_kapi.h" "$b/srv/kapi.h" && cp "$here/mock_net_kapi.h" "$b/srv/"
cp "$b/ftpd.c" "$b/srv/"
printf '#include "ftpd.c"\nint main (int c, char **v) { snprintf (mock_args, sizeof mock_args, "%%s", c > 1 ? v[1] : ""); return ftpd_main (); }\n' > "$b/srv/main.c"
g++ -w -I"$b/srv" -x c++ "$b/srv/main.c" -o "$b/ftpd"
rm -rf /tmp/onyx_mock && mkdir -p /tmp/onyx_mock/pub/sub && printf 'hello onyx\n' > /tmp/onyx_mock/pub/readme.txt
"$b/ftpd" "SD:/pub tester secret" > /dev/null & ftpd=$!
sleep 0.5
timeout 60 "$b/t" "FTP:tester:secret@127.0.0.1:21"
