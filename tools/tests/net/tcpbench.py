#!/usr/bin/env python3
"""
tools/tests/net/tcpbench.py -- the PC's side of /bin/tcpbench (user/bin/tcpbench.c): the Pi's TCP speed
without a disk and without the internet, each way, and the latency of a round trip.

  python3 tools/tests/net/tcpbench.py HOST [MEGABYTES] [PORT]      (default 8 MB, port 5001)

Prints, for the Pi sending and for the Pi receiving: the rate, and how regular it was (the bytes of each
half second: a stall shows as zeros). Then 200 echo round trips of 100 bytes: their median and worst.

Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence.
"""
import socket, struct, sys, time

def connect(host, port):
	s = socket.create_connection((host, port), timeout=30)
	s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
	return s

def pi_sends(host, port, mb):
	s = connect(host, port)
	s.sendall(b'S %d\n' % mb)
	total, t0, last, slices, cur = 0, time.time(), time.time(), [], 0
	while True:
		d = s.recv(1 << 16)
		if not d: break
		total += len(d); cur += len(d)
		now = time.time()
		if now - last >= 0.5:
			slices.append(cur); cur = 0; last = now
	dt = time.time() - t0
	slices.append(cur)
	print('the Pi sends:    %d bytes in %.2f s: %.0f KB/s; by half second (KB): %s' % (total, dt, total / 1024 / dt, ' '.join(str(x // 1024) for x in slices[:40])))
	s.close()

def tcp_info(s):
	"""This side's view of the connection (Linux): the congestion window (segments), the smoothed round trip
	(ms), the segments sent again so far, the slow start threshold -- None where TCP_INFO is not there."""
	try:
		raw = s.getsockopt(socket.IPPROTO_TCP, socket.TCP_INFO, 104)
		v = struct.unpack('8B24I', raw[:104])[8:]
		return {'cwnd': v[18], 'rtt': v[15] / 1000.0, 'retrans': v[23], 'ssthresh': v[17], 'unacked': v[4], 'mss': v[2]}
	except Exception:
		return None

def pi_receives(host, port, mb):
	s = connect(host, port)
	s.sendall(b'R\n')
	block = b'y' * 65536
	total, t0, last, slices, cur, infos = 0, time.time(), time.time(), [], 0, []
	while total < mb * 1024 * 1024:
		s.sendall(block)
		total += len(block); cur += len(block)
		now = time.time()
		if now - last >= 0.5:
			slices.append(cur); cur = 0; last = now
			i = tcp_info(s)
			if i: infos.append(i)
	if infos:
		print('  the sender (this PC), each half second: cwnd (segments) / round trip (ms) / segments sent again so far:')
		print('  ' + ' '.join('%d/%.1f/%d' % (i['cwnd'], i['rtt'], i['retrans']) for i in infos[:24]) + ' (mss %d)' % infos[0]['mss'])
	s.shutdown(socket.SHUT_WR)
	ans = b''
	while not ans.endswith(b'\n'):
		d = s.recv(64)
		if not d: break
		ans += d
	dt = time.time() - t0
	slices.append(cur)
	print('the Pi receives: %d bytes in %.2f s: %.0f KB/s (the Pi says: %s); by half second (KB): %s' % (total, dt, total / 1024 / dt, ans.decode().strip(), ' '.join(str(x // 1024) for x in slices[:40])))
	s.close()

def echo(host, port, n=200):
	s = connect(host, port)
	s.sendall(b'E\n')
	times = []
	for i in range(n):
		t0 = time.time()
		s.sendall(b'z' * 100)
		got = 0
		while got < 100:
			d = s.recv(100 - got)
			if not d: break
			got += len(d)
		times.append((time.time() - t0) * 1000)
	s.close()
	times.sort()
	print('echo of 100 bytes, %d round trips: median %.1f ms, 90%% %.1f ms, worst %.1f ms' % (n, times[n // 2], times[n * 9 // 10], times[-1]))

if __name__ == '__main__':
	host = sys.argv[1]
	mb = int(sys.argv[2]) if len(sys.argv) > 2 else 8
	port = int(sys.argv[3]) if len(sys.argv) > 3 else 5001
	echo(host, port)
	pi_sends(host, port, mb)
	pi_receives(host, port, mb)
