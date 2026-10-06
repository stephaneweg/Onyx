#!/usr/bin/env python3
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
"""flicker_bench.py -- does the remote desktop show windows half painted? A client of rdpd (as Onyx Remote: the
windows' contents, no frames) that moves the pointer to and fro over one window, keeps that window's picture
as rdpd sends it, and after each round counts the pixels that are the window's background: a round where much
more of the window is background than at rest is a picture read while its program was repainting it (the
flicker seen in Onyx Remote when the pointer moves, 2026-10-06).

    python tools/tests/rdpd/flicker_bench.py <pi-ip> [--title TEXT] [--secs 15] [--port 3390]

--title: the window whose title holds TEXT (default: the largest one). rdpd has one client: Onyx Remote is
disconnected while this runs. Needs numpy."""
import socket, struct, sys, time
import numpy as np

def lz4 (src, n):
	out = bytearray (); i = 0
	while i < len (src):
		t = src[i]; i += 1; l = t >> 4
		if l == 15:
			while True:
				b = src[i]; i += 1; l += b
				if b != 255: break
		out += src[i:i + l]; i += l
		if i >= len (src): break
		off = src[i] | src[i + 1] << 8; i += 2; m = t & 15
		if m == 15:
			while True:
				b = src[i]; i += 1; m += b
				if b != 255: break
		m += 4; r = len (out) - off
		if off >= m: out += out[r:r + m]
		else:
			for k in range (m): out.append (out[r + k])
	assert len (out) == n, (len (out), n)
	return bytes (out)

class Client:
	def __init__ (self, host, port):
		self.s = socket.create_connection ((host, port), timeout = 10)
		h = self.recv (14)
		assert h[:8] == b"ONYXRDP1", h
		self.s.sendall (b"ONYXRDP1\x02" + b"\x01")		# 32-bit pixels, no frames; READY
		self.wins, self.img = {}, {}
	def recv (self, n):
		b = b""
		while len (b) < n:
			c = self.s.recv (n - len (b))
			if not c: raise EOFError
			b += c
		return b
	def round (self, timeout):
		"""One round applied -> the ids whose content changed; None: nothing came in `timeout`."""
		touched = set ()
		self.s.settimeout (timeout)
		try: head = self.recv (5)
		except socket.timeout: return None
		self.s.settimeout (10)
		while True:
			t, n = struct.unpack ("<BI", head); p = self.recv (n) if n else b""
			if t == 1:
				id, x, y, w, h, ow, oh, il, it, fl, al, st, tn = struct.unpack ("<IhhHHHHHHIBBB", p[:27])
				self.wins[id] = dict (w = w, h = h, title = p[27:27 + tn].decode ("utf-8", "replace"))
				if id not in self.img or self.img[id].shape != (h, w): self.img[id] = np.zeros ((h, w), np.uint32)
			elif t == 2:
				id = struct.unpack ("<I", p[:4])[0]; self.wins.pop (id, None); self.img.pop (id, None)
			elif t == 4:
				id, part, bpp, lz, x, y, w, h = struct.unpack ("<IBBBHHHH", p[:15])
				if part == 0 and bpp == 32 and id in self.img:
					d = lz4 (p[15:], w * h * 4) if lz else p[15:]
					a = np.frombuffer (d, np.uint32).reshape (h, w) & 0xFFFFFF
					im = self.img[id]
					hh, ww = min (h, im.shape[0] - y), min (w, im.shape[1] - x)
					if hh > 0 and ww > 0: im[y:y + hh, x:x + ww] = a[:hh, :ww]; touched.add (id)
			elif t == 5:
				self.s.sendall (b"\x01")
				return touched
			head = self.recv (5)
	def ptr (self, id, x, y):
		self.s.sendall (struct.pack ("<BIhhBb", 2, id, x, y, 0, 0))

def main ():
	a = sys.argv[1:]
	if not a: sys.exit (__doc__)
	host, title, secs, port = a.pop (0), None, 15.0, 3390
	while a:
		o = a.pop (0)
		if o == "--title": title = a.pop (0)
		elif o == "--secs": secs = float (a.pop (0))
		elif o == "--port": port = int (a.pop (0))
	c = Client (host, port)
	t0 = time.time ()
	while time.time () - t0 < 4: c.round (0.5)			# the windows, at rest
	cand = [(w["w"] * w["h"], id) for id, w in c.wins.items () if (title is None or title.lower () in w["title"].lower ()) and w["w"] >= 200 and w["h"] >= 150]
	if not cand: sys.exit ("no such window: " + ", ".join (repr (w["title"]) for w in c.wins.values ()))
	id = max (cand)[1]; W, H = c.wins[id]["w"], c.wins[id]["h"]
	vals, counts = np.unique (c.img[id], return_counts = True)
	bg = vals[counts.argmax ()]; rest = counts.max () / float (W * H)
	print ("window %r %dx%d, background %06X: %.1f%% of it at rest" % (c.wins[id]["title"], W, H, bg, 100 * rest))
	rounds = torn = 0; worst = 0.0; k = 0; t0 = last = time.time ()
	while time.time () - t0 < secs:
		now = time.time ()
		if now - last >= 0.016:					# the pointer: a zigzag over the window
			last = now; k += 1
			c.ptr (id, 20 + (k * 37) % max (1, W - 40), 20 + (k * 23) % max (1, H - 40))
		touched = c.round (0.005)
		if touched is None or id not in touched: continue
		rounds += 1
		f = float ((c.img[id] == bg).sum ()) / (W * H)
		worst = max (worst, f - rest)
		if f - rest > 0.03: torn += 1
	print ("%d rounds changed the window in %.0f s; %d of them half painted (over 3%% more background; the worst: +%.1f%%)" % (rounds, secs, torn, 100 * worst))

if __name__ == "__main__": main ()
