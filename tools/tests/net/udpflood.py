#!/usr/bin/env python3
"""
tools/tests/net/udpflood.py -- datagrams sent to the Pi's `tcpbench udp` at a chosen rate: how many frames a
second the Pi takes when nothing paces the sender (TCP does: its rate says little of the link).

  python3 tools/tests/net/udpflood.py HOST RATE [SECONDS] [SIZE] [PORT]     (RATE: datagrams a second)

Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence.
"""
import socket, struct, sys, time

host = sys.argv[1]
rate = int(sys.argv[2])
secs = float(sys.argv[3]) if len(sys.argv) > 3 else 4
size = int(sys.argv[4]) if len(sys.argv) > 4 else 1400
port = int(sys.argv[5]) if len(sys.argv) > 5 else 5002
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
pad = b'u' * (size - 4)
t0 = time.time()
n = 0
while True:
	now = time.time()
	if now - t0 >= secs: break
	due = int((now - t0) * rate)
	while n < due:
		s.sendto(struct.pack('<I', n) + pad, (host, port))
		n += 1
	time.sleep(0.0005)
print('sent %d datagrams of %d bytes in %.1f s (%d a second, %d KB/s)' % (n, size, time.time() - t0, n / (time.time() - t0), n * size / 1024 / (time.time() - t0)))
