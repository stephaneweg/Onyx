#!/usr/bin/env python3
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
"""compat_pi.py -- runs the shared libraries' compatibility test on the Pi (the files built by
tools/tests/shlib/compat.sh): the program built against uikit N, NOT rebuilt, on the library N+1.

    python tools/tests/shlib/compat_pi.py <pi-ip> <out-dir> [ftp-user ftp-password]

Needs telnetd on the Pi; starts `run SD:/bin/ftpd SD:/` there if it does not run (its default account: onyx /
onyx). Uses SD:/lib/uikitc.so (the test's own name: the system's uikit.so is not touched) and
SD:/bin/compat-n, compat-n1 (cmd runs the programs of /bin with its output); removes them at the end. Exit status 0 only if every check passed."""
import ftplib, os, re, socket, sys, time

if len (sys.argv) < 3: sys.exit (__doc__)
HOST, OUT = sys.argv[1], sys.argv[2]
USER, PW = (sys.argv[3], sys.argv[4]) if len (sys.argv) > 4 else ("onyx", "onyx")

class Telnet:
	def __init__ (self):
		self.s = socket.create_connection ((HOST, 23), timeout = 10); self.st = 0
		self.read (1.0)
	def read (self, idle, mx = 30.0):
		t0 = last = time.time (); out = bytearray ()
		self.s.settimeout (0.2)
		while time.time () - last < idle and time.time () - t0 < mx:
			try: d = self.s.recv (65536)
			except socket.timeout: continue
			if not d: break
			last = time.time ()
			for b in d:				# (telnet commands dropped)
				if self.st == 0:
					if b == 255: self.st = 1
					else: out.append (b)
				elif self.st == 1: self.st = 2 if 251 <= b <= 254 else 0
				else: self.st = 0
		return out.decode ("utf-8", "replace").replace ("\r", "")
	def cmd (self, c, idle = 1.5):
		self.s.sendall (c.encode () + b"\r\n")
		return self.read (idle)

def ftp ():
	try:
		f = ftplib.FTP (); f.connect (HOST, 21, timeout = 5)
	except Exception:
		t = Telnet (); t.s.sendall (b"run SD:/bin/ftpd SD:/\r\n"); time.sleep (2.0); t.s.close ()
		f = ftplib.FTP (); f.connect (HOST, 21, timeout = 10)
	f.login (USER, PW)
	return f

def put (f, local, remote):
	with open (os.path.join (OUT, local), "rb") as fp:
		f.storbinary ("STOR " + remote, fp)

f = ftp ()
for d in ("lib",):
	try: f.mkd (d)
	except Exception: pass
put (f, "app-n", "bin/compat-n"); put (f, "app-n1", "bin/compat-n1")
pi = Telnet ()
failed = 0
def check (name, ok, detail = ""):
	global failed
	if not ok: failed += 1
	print ("%s %s%s" % ("PASS" if ok else "FAIL", name, "" if ok else ": " + detail))
def run (app):
	out = pi.cmd (app, 2.5)
	return dict (re.findall (r"^(\w+)=(-?\d+)$", out, re.M)), out

put (f, "uikitc-n.so", "lib/uikitc.so")
v, raw = run ("compat-n")
base = v
check ("the program built against N runs on the library N",
       v.get ("done") == "1" and v.get ("clicked") == "1" and int (v.get ("drawn", 0)) >= 1 and v.get ("reserve") == "0"
       and v.get ("bfw") != "1234", raw[-300:])

put (f, "uikitc-n1.so", "lib/uikitc.so")			# the library replaced: N+1
v, raw = run ("compat-n")				# the SAME program file, not rebuilt
check ("(a) a fix in the library reaches the program built against N", v.get ("bfw") == "1234", raw[-300:])
check ("(b) (c) it still runs: its widgets, its own class, its callback, the same layout",
       v.get ("done") == "1" and v.get ("clicked") == "1" and v.get ("drawn") == base.get ("drawn")
       and v.get ("width") == base.get ("width"), raw[-300:])
check ("(c) the library N+1 uses a reserved field of the program's widgets", v.get ("reserve") == "49374", raw[-300:])
v, raw = run ("compat-n1")
check ("a program built against N+1 uses the appended function and the reserved virtual",
       v.get ("done") == "1" and v.get ("added") == "77" and v.get ("compat") == "1" and v.get ("reserve") == "49374", raw[-300:])

put (f, "uikitc-n.so", "lib/uikitc.so")			# back to N
v, raw = run ("compat-n1")
check ("(d) a program built against N+1 is refused by the library N, with a message",
       "done" not in v and "needs the shared library" in raw and "older" in raw, raw[-300:])
v, raw = run ("compat-n")
check ("the program built against N runs again on N", v.get ("done") == "1" and v.get ("bfw") == base.get ("bfw"), raw[-300:])

for r in ("lib/uikitc.so", "bin/compat-n", "bin/compat-n1"):
	try: f.delete (r)
	except Exception: pass
try: f.quit ()
except Exception: pass
pi.cmd ("exit", 0.3)
print ("compat: all passed" if failed == 0 else "compat: %d FAILED" % failed)
sys.exit (1 if failed else 0)
