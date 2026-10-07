#!/usr/bin/env python3
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
"""pi_apps.py -- start every app of a Pi running Onyx, one after the other, over telnetd, and check
that each one comes up on the shared libraries (docs/SHARED-LIBS-PLAN.md section 6, "Pi, step 1c"):
the process is there a few seconds after its start (or ended by itself with nothing in the kernel
log), no fault, no "needs the shared library" line. Each app is then closed (kill).

    python tools/tests/shlib/pi_apps.py <pi-ip> [--shots DIR] [--only a,b,c] [--from name] [--wait S]

--shots DIR: a screenshot of each app there (needs vncdotool: python -m pip install vncdotool).
Prints PASS / FAIL per app and a summary; exit status 0 only if none failed.
Not started: the desktop's own processes (they run already: restart the Pi to renew them) and the
apps that would act on the machine (lock, shutdown, setup)."""
import os, re, socket, subprocess, sys, time

SKIP = { "menubar", "dock", "notifyd", "clipd", "pkgd", "agenda",	# the desktop: running already
	 "lock", "shutdown", "setup",					# act on the machine
	 }				# need arguments

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

def procs (pi):
	out = {}
	for line in pi.cmd ("ps").splitlines ():
		f = line.split ()
		if len (f) >= 7 and f[0].isdigit () and f[1] == "a":
			out[int (f[0])] = f[6]
	return out

def main ():
	args = sys.argv[1:]
	if not args: sys.exit (__doc__)
	host = args.pop (0)
	shots, only, wait, first = None, None, 4.0, ""
	while args:
		o = args.pop (0)
		if o == "--shots": shots = args.pop (0)
		elif o == "--only": only = set (args.pop (0).split (","))
		elif o == "--wait": wait = float (args.pop (0))
		elif o == "--from": first = args.pop (0)
	if shots: os.makedirs (shots, exist_ok = True)
	pi = Pi (host)
	names = sorted (n[:-4] for n in re.findall (r"(\S+\.app)/?", pi.cmd ("ls SD:/apps", 1.5)) if n.endswith (".app"))
	names = [n for n in names if ((only is None and n not in SKIP) or (only is not None and n in only)) and n >= first]
	log_pi = Pi (host)						# a second session stays in kmsg: the kernel log as it comes
	log_pi.cmd ("kmsg", 2.0)					# (its backlog read and dropped)
	failed, ended = [], []
	for n in names:
		before = procs (pi)
		pi.cmd ("run " + n, 0.5)
		time.sleep (wait)
		after = procs (pi)
		# (only the app itself: never another process that came meanwhile -- a telnet session's...)
		new = [p for p in after if p not in before and (after[p] == n or after[p].endswith ("/%s.app/main" % n))]
		log = log_pi.read (1.0)
		bad = [l.strip () for l in log.splitlines ()
		       if re.search (r"killed|fault|data abort|needs the shared library|cannot load|out of memory|PANIC", l, re.I)]
		if shots and new:
			try:
				subprocess.run ([sys.executable, "-m", "vncdotool.command", "-s", host, "capture", os.path.join (shots, n + ".png")],
						capture_output = True, timeout = 25)
			except subprocess.TimeoutExpired:
				pass						# (a full-screen app: vncd shows nothing)
		for p in new:
			pi.cmd ("kill %d" % p, 0.4)
		libs = sorted (set (re.findall (r"lib: sd:/lib/(\w+)\.so", log)))
		if bad:
			failed.append (n); print ("FAIL %-14s %s" % (n, bad[0][:150]))
		elif not new:
			ended.append (n); print ("PASS %-14s (ended by itself; libraries: %s)" % (n, " ".join (libs) or "-"))
		else:
			print ("PASS %-14s (libraries: %s)" % (n, " ".join (libs) or "-"))
		time.sleep (0.5)
	print ("%d apps started, %d failed%s" % (len (names), len (failed), ": " + " ".join (failed) if failed else ""))
	if ended: print ("ended by themselves (look at them by hand): " + " ".join (ended))
	for p, n in procs (pi).items ():				# the log session's kmsg (and its shell) ended
		if n == "SD:/bin/kmsg": pi.cmd ("kill %d" % p, 0.4)
	try: pi.cmd ("exit", 0.3)
	except Exception: pass
	sys.exit (1 if failed else 0)

main ()
