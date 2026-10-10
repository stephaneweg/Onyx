#!/usr/bin/env python3
# Copyright (c) 2026 the Onyx authors -- MIT licence (see docs/LICENSING.md).
"""fake_rdpd.py -- a stand-in for Onyx's rdpd (user/BinUtils/rdpd.c), for PocketTerm Simulator's tests and
for a look at the simulator with no Pi: it speaks rdpd's hello and frame format, shows one picture as the
Pi's screen (the desktop window, uncompressed 32-bit PIXELS in strips) and writes every input message it
receives, one a line.

    python fake_rdpd.py [--port 3390] [--picture FILE.png] [--size 640x480] [--log FILE] [--once]

  --size   another screen size than the picture's (the picture is resized): the "not 640 x 480" case
  --once   leave when the first client leaves (the tests)
The log: READY, PTR id x y buttons wheel, KEY flags 0xSYM, CHAR 0xNN, RAISE id, CLOSE id, DESKTOP n,
MOVE id x y, PONG -- as the protocol at the top of rdpd.c names them. Needs Pillow.
"""
import argparse, os, socket, struct, sys, time
from PIL import Image

ap = argparse.ArgumentParser ()
ap.add_argument ("--port", type = int, default = 3390)
ap.add_argument ("--picture", default = os.path.join (os.path.dirname (os.path.abspath (__file__)), "..", "..", "..", "docs", "compact-shell", "mockups", "pocketterm", "noir-home.png"))
ap.add_argument ("--size", default = "")
ap.add_argument ("--log", default = "")
ap.add_argument ("--once", action = "store_true")
a = ap.parse_args ()

img = Image.open (a.picture).convert ("RGB")
if a.size:
	w, h = (int (v) for v in a.size.lower ().split ("x"))
	img = img.resize ((w, h), Image.LANCZOS)
W, H = img.size
r, g, b = img.split ()
bgrx = Image.merge ("RGBX", (b, g, r, Image.new ("L", (W, H), 0))).tobytes ()	# B G R T (T 0: opaque)
log = open (a.log, "w", buffering = 1) if a.log else sys.stdout
DESKTOP = 0xFFFFFFFF

def msg (t, payload = b""): return struct.pack ("<BI", t, len (payload)) + payload

def first_round ():
	out = msg (1, struct.pack ("<IhhHHHHHHIBBB", DESKTOP, 0, 0, W, H, 0, 0, 0, 0, 2, 255, 0, 0))	# WIN: the desktop, backmost
	out += msg (3, struct.pack ("<HI", 1, DESKTOP))								# ZORDER
	for y in range (0, H, 64):										# PIXELS, strips of 64 lines
		n = min (64, H - y)
		out += msg (4, struct.pack ("<IBBBHHHH", DESKTOP, 0, 32, 0, 0, y, W, n) + bgrx[y * W * 4:(y + n) * W * 4])
	return out + msg (5)											# END

SIZES = {1: 0, 2: 10, 3: 5, 4: 4, 5: 4, 6: 4, 7: 1, 8: 4, 9: 8}		# client messages: payload bytes

def serve (c):
	c.settimeout (2.0)
	c.sendall (b"ONYXRDP1" + struct.pack ("<HHH", W, H, 96))
	buf = b""
	last = time.time ()
	def need (n):
		nonlocal buf, last
		while len (buf) < n:
			if time.time () - last >= 2.0:		# PING every 2 s, as rdpd while no round goes: the client
				last = time.time ()		# takes 12 s of silence for a dead link
				c.sendall (msg (10, struct.pack ("<I", int (last * 1000) & 0xFFFFFFFF)))
			try: d = c.recv (65536)
			except socket.timeout: continue
			if not d: raise EOFError
			buf += d
		v, buf = buf[:n], buf[n:]
		return v
	hello = need (9)
	if hello[:8] != b"ONYXRDP1": return
	log.write ("HELLO options %d\n" % hello[8])
	if hello[8] & 4: c.sendall (msg (9, bytes ([2, 3])))		# CAPS: protocol 2, 3 rounds in flight
	sent = False
	while True:
		t = need (1)[0]
		if t not in SIZES: log.write ("UNKNOWN %d\n" % t); return
		p = need (SIZES[t])
		if t == 1:
			log.write ("READY\n")
			if not sent: c.sendall (first_round ()); sent = True
		elif t == 2: log.write ("PTR 0x%X %d %d %d %d\n" % struct.unpack ("<IhhBb", p))
		elif t == 3: log.write ("KEY %d 0x%04X\n" % struct.unpack ("<BI", p))
		elif t == 6: log.write ("CHAR 0x%02X\n" % struct.unpack ("<I", p))
		elif t == 4: log.write ("RAISE %d\n" % struct.unpack ("<I", p))
		elif t == 5: log.write ("CLOSE %d\n" % struct.unpack ("<I", p))
		elif t == 7: log.write ("DESKTOP %d\n" % p[0])
		elif t == 8: log.write ("PONG\n")
		elif t == 9: log.write ("MOVE 0x%X %d %d\n" % struct.unpack ("<Ihh", p))

s = socket.socket (); s.setsockopt (socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind (("127.0.0.1", a.port)); s.listen (1)
print ("fake rdpd: %d x %d on 127.0.0.1:%d" % (W, H, a.port), file = sys.stderr, flush = True)
while True:
	c, _ = s.accept ()
	try: serve (c)
	except (EOFError, ConnectionError, OSError): pass
	c.close ()
	log.write ("BYE\n")
	if a.once: break
