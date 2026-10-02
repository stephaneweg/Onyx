#!/bin/sh
# run_ipc_test.sh -- the kernel's IPC of kapi v76 (kernel/sys/lsock.cpp: local sockets and the
# handles they carry; kernel/sys/shm.cpp: shared memory objects; kernel/sys/handle.cpp: the tables)
# on the PC, the kernel around them stubbed (tools/tests/ipc: two handle tables standing for two
# processes). docs/02 section 8 "v76: IPC". MIT licence (Onyx).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
T=${TMPDIR:-/tmp}/onyx_ipctest
rm -rf "$T" && mkdir -p "$T"
INC="-I$HERE/ipc/stub -I$ROOT/kernel/include"
FLAGS="-std=gnu++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wno-unused-parameter"
for f in lsock shm handle; do
	g++ $FLAGS $INC -c "$ROOT/kernel/sys/$f.cpp" -o "$T/$f.o"
done
g++ $FLAGS $INC "$HERE/ipc/ipchost.cpp" "$T/lsock.o" "$T/shm.o" "$T/handle.o" -o "$T/ipchost"
"$T/ipchost"
