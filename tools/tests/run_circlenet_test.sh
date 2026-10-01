#!/bin/sh
# Host test of our Circle fork's TCP: duplicate ACKs (RFC 5681), the RTO (200 ms minimum, Karn),
# CSocket::Send's count. Builds circle/lib/net's real tcpconnection.cpp, retranstimeoutcalc.cpp,
# socket.cpp (+ the buffers, queues, checksum) against stub Circle headers
# (tools/tests/circlenet/stub: a simulated clock, no tasks, no network). docs/05.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${OUT:-/tmp/circlenet}
mkdir -p "$OUT"
CXX=${CXX:-g++}
FLAGS="-std=gnu++14 -O1 -g -DAARCH=64 -DRASPPI=4 -fno-exceptions -fno-rtti -fno-builtin -w"
INC="-I $ROOT/tools/tests/circlenet/stub -I $ROOT/circle/include"
OBJS=""
for f in lib/net/tcpconnection lib/net/retranstimeoutcalc lib/net/socket lib/net/netconnection \
         lib/net/netbuffer lib/net/netbufferqueue lib/net/reassemblyqueue lib/net/checksumcalculator \
         lib/net/ipaddress lib/net/netconfig lib/ptrlist lib/string; do
	o="$OUT/$(basename $f).o"
	$CXX $FLAGS $INC -c "$ROOT/circle/$f.cpp" -o "$o"
	OBJS="$OBJS $o"
done
$CXX $FLAGS $INC -c "$ROOT/tools/tests/circlenet/tcptest.cpp" -o "$OUT/tcptest.o"
$CXX -o "$OUT/tcptest" "$OUT/tcptest.o" $OBJS
"$OUT/tcptest"
