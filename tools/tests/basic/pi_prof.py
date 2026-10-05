#!/usr/bin/env python3
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
"""pi_prof.py -- where a BASIC program's time goes, on a Pi running Onyx (basic -p: the VM's
instructions / the runtime's primitives / the waits). Uploads a runtime as SD:/bin/basicp (the
card's /bin/basic is left alone) by ftpd, runs it over telnetd.

    python tools/tests/basic/pi_prof.py <pi-ip> [--runtime user/bin/basic.elf] [--managed] [--key]
        calc | calls             a benchmark (tools/tests/basic/bench/<name>.bas), to its end
        <SD: path> [seconds]     a program of the card (a game: started, measured for <seconds>, then killed)

Needs ftpd on the Pi (started here, detached: "run SD:/bin/ftpd SD:/") and, for a game's key, vncdotool."""
import ftplib, os, re, socket, subprocess, sys, time

# (the telnet client of tools/tests/shlib/pi_apps.py)
def strip (data, st):
	out = bytearray ()
	for b in data:
		s = st[0]
		if s == 0:
			if b == 255: st[0] = 1
			else: out.append (b)
		elif s == 1:
			if b == 255: out.append (b); st[0] = 0
			elif 251 <= b <= 254: st[0] = 2
			elif b == 250: st[0] = 3
			else: st[0] = 0
		elif s == 2: st[0] = 0
		elif s == 3:
			if b == 255: st[0] = 4
		elif s == 4: st[0] = 0 if b == 240 else 3
	return bytes (out)

class Pi:
	def __init__ (self, host):
		self.s = socket.create_connection ((host, 23), timeout = 10)
		self.st = [0]
		self.read (1.0)
	def read (self, idle, mx = 20.0):
		t0 = last = time.time (); buf = b""
		self.s.settimeout (0.2)
		while time.time () - last < idle and time.time () - t0 < mx:
			try:
				d = self.s.recv (65536)
				if not d: break
				buf += strip (d, self.st); last = time.time ()
			except socket.timeout:
				pass
		return buf.decode ("utf-8", "replace").replace ("\r", "")
	def cmd (self, c, idle = 1.0, mx = 20.0):
		self.s.sendall (c.encode () + b"\r\n")
		return self.read (idle, mx)
	def ctrl_c (self):
		self.s.sendall (b"\x03")
		return self.read (0.8)

# Keys typed through vncd without asking for the screen (a full-screen program's capture can hang): a
# minimal RFB client -- the handshake, then KeyEvent messages.
def vnc_keys (ip, keysyms):
	import struct
	try:
		s = socket.create_connection ((ip, 5900), timeout = 8)
		s.recv (12); s.sendall (b"RFB 003.008" + bytes ([10]))
		n = s.recv (1)[0]; types = s.recv (n)
		if 1 not in types: print ("  (vncd wants a password: no key sent)"); return
		s.sendall (bytes ([1])); s.recv (4)
		s.sendall (bytes ([1])); s.recv (4096)			# ClientInit (shared), ServerInit
		for k in keysyms:
			s.sendall (struct.pack (">BBHI", 4, 1, 0, k)); time.sleep (0.15)
			s.sendall (struct.pack (">BBHI", 4, 0, 0, k)); time.sleep (1.0)
		s.close ()
	except Exception as e:
		print ("  (no key sent: %s)" % e)

ROOT = os.path.abspath (os.path.join (os.path.dirname (os.path.abspath (__file__)), "..", "..", ".."))

def upload (ip, local, remote):
	for attempt in range (4):
		try:
			f = ftplib.FTP (); f.connect (ip, 21, timeout = 20); f.login ("onyx", "onyx")
			with open (local, "rb") as fh: f.storbinary ("STOR " + remote, fh)
			f.quit (); return
		except Exception as e:
			print ("  ftp:", e, "(retry)"); time.sleep (2)
	raise SystemExit ("upload failed: " + local)

def main ():
	a = sys.argv[1:]
	if len (a) < 2: raise SystemExit (__doc__)
	ip = a.pop (0)
	runtime = os.path.join (ROOT, "user", "bin", "basic.elf"); flags = "-p"; key = False
	while a and a[0].startswith ("--"):
		o = a.pop (0)
		if o == "--runtime": runtime = a.pop (0)
		elif o == "--managed": flags = "-m -p"
		elif o == "--key": key = True
	what = a[0]; secs = int (a[1]) if len (a) > 1 else 25
	pi = Pi (ip)
	try:
		ftplib.FTP ().connect (ip, 21, timeout = 3)
	except Exception:
		pi.cmd ("run SD:/bin/ftpd SD:/", 2.0)	# (detached: a session's own programs end with it)
	upload (ip, runtime, "bin/basicp")
	if what in ("calc", "calls", "calls_t"):
		upload (ip, os.path.join (ROOT, "tools", "tests", "basic", "bench", what + ".bas"), what + ".bas")
		out = pi.cmd ("basicp %s SD:/%s.bas" % (flags, what), 6.0, 300.0)
		print (out)
		pi.cmd ("exit", 1.0)
		return
	pi.cmd ("rm SD:/basprof.txt", 1.0)
	p2 = Pi (ip)					# the program holds this shell
	p2.s.sendall (("basicp %s %s\r\n" % (flags, what)).encode ())
	time.sleep (4)
	if key: vnc_keys (ip, [0x20, 0x20])		# leave a title screen: Space, twice
	time.sleep (secs)
	print (pi.cmd ("cat SD:/basprof.txt", 2.0))
	ps = pi.cmd ("ps", 2.0)
	for line in ps.splitlines ():
		if "basicp" in line:
			m = re.search (r"\b(\d+)\b", line)
			if m: print (pi.cmd ("kill " + m.group (1), 1.5).strip ())
	time.sleep (1)
	print (pi.cmd ("cat SD:/basprof.txt", 2.0))
	pi.cmd ("exit", 1.0); p2.cmd ("exit", 1.0)

main ()
