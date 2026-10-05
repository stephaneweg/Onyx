#!/usr/bin/env python3
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
"""run_pi.py -- runs fsrace (fsrace.c: the full screen taken during the compositor's display DMA)
on a Pi running Onyx, over telnetd; uploads it as SD:/bin/fsrace by ftpd and removes it at the end.

    python tools/tests/fsrace/run_pi.py <pi-ip> <fsrace binary> [rounds]

Build the binary first (the bare-metal toolchain on the PATH), from user/bin:
    aarch64-none-elf-gcc -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only -O2 \\
        -fno-stack-protector -I.. -I../../kernel/include -Wl,-T,../user.ld \\
        -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none -Wl,--defsym,memset=kapi_memset \\
        -Wl,--defsym,memcpy=kapi_memcpy -Wl,--defsym,memmove=kapi_memmove \\
        ../crt0.S ../../tools/tests/fsrace/fsrace.c -o fsrace

Prints PASS ("fsrace: ok" came back) or FAIL (the Pi stopped answering: a kernel without the fix
freezes within a few rounds, the hang watchdog restarts it, SD:/etc/lastcrash.txt tells where).
The screen flickers while it runs. Needs ftpd on the Pi (started here if it does not answer)."""
import ftplib, socket, sys, time

def strip (data, st):			# (the telnet client of tools/tests/basic/pi_prof.py)
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
	def read (self, idle, mx = 20.0, until = None):
		t0 = last = time.time (); buf = b""
		self.s.settimeout (0.2)
		while time.time () - last < idle and time.time () - t0 < mx:
			try:
				d = self.s.recv (65536)
				if not d: break
				buf += strip (d, self.st); last = time.time ()
				if until and until in buf: break
			except socket.timeout:
				pass
			except OSError:
				break
		return buf.decode ("utf-8", "replace").replace ("\r", "")
	def cmd (self, c, idle = 1.0, mx = 20.0, until = None):
		self.s.sendall (c.encode () + b"\r\n")
		return self.read (idle, mx, until)

def upload (ip, local, remote):
	for attempt in range (4):
		try:
			f = ftplib.FTP (); f.connect (ip, 21, timeout = 20); f.login ("onyx", "onyx")
			with open (local, "rb") as fh: f.storbinary ("STOR " + remote, fh)
			f.quit (); return True
		except Exception as e:
			print ("  (ftp: %s)" % e)
			if attempt == 0:
				try: Pi (ip).cmd ("run SD:/bin/ftpd SD:/", 2.0)
				except Exception: pass
			time.sleep (2)
	return False

def main ():
	if len (sys.argv) < 3: print (__doc__); return 2
	ip, binary = sys.argv[1], sys.argv[2]
	rounds = sys.argv[3] if len (sys.argv) > 3 else "300"
	if not upload (ip, binary, "bin/fsrace"): print ("FAIL: no upload"); return 2
	pi = Pi (ip)
	out = pi.cmd ("fsrace " + rounds, idle = 25.0, mx = 600.0, until = b"fsrace: ok")
	print (out)
	ok = "fsrace: ok" in out
	if ok:
		pi.read (1.0)
		print (pi.cmd ("rm SD:/bin/fsrace", 1.5))
	print ("PASS" if ok else "FAIL: the last line above is where it stopped (the Pi restarts by itself; "
				 "then: cat SD:/etc/lastcrash.txt, rm SD:/bin/fsrace)")
	return 0 if ok else 1

if __name__ == "__main__":
	sys.exit (main ())
