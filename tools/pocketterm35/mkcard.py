#!/usr/bin/env python3
"""tools/pocketterm35/mkcard.py -- the Waveshare PocketTerm35's ready card, SD_PocketTerm35/, made from sdcard/.

    python3 tools/pocketterm35/mkcard.py [--out <dir>]      (after `make -C kernel stage`: the Pi 4's card staged)

The PocketTerm35 is a handheld around a Raspberry Pi 4: a 3.5-inch HDMI screen with a GT911 touch panel, a keyboard
(an RP2040 on the Pi's USB-C socket), a battery. Its card is Onyx's usual Pi 4 card (sdcard/: every app, offline),
set for the device:

  * cmdline.txt: 800 x 600, usbc=1 (the keyboard), touch=1 (the touch screen as the pointer), keyboard=US (the
    keyboard's layout: Setup proposes it and keeps it whatever the country);
  * config.txt: Waveshare's lines (Linux's overlays: Onyx does not need them), overlays/ with their files;
  * the pocket mode (SD:/etc/system.ini shell = pocket), Setup at the first boot in pocket's look (the pocket
    session's file holds the shell back meanwhile);
  * the autostart's keyboard US.

Everything else is sdcard/'s, minus the files of a used card (the Wi-Fi network, the mail accounts, the clock...:
.gitignore's private files) and Jet Browser's program (a package). The output is rebuilt from nothing each run.

MIT licence (docs/LICENSING.md).
"""
import argparse, fnmatch, os, shutil, sys

HERE = os.path.dirname (os.path.abspath (__file__))
ROOT = os.path.dirname (os.path.dirname (HERE))
SRC = os.path.join (ROOT, "sdcard")
OVL = os.path.join (HERE, "overlay")

# not on the card: a used card's own state and private files (as tools/pi5/mkcard5.py's), old kernels, Jet's program
SKIP = ["etc/wpa_supplicant.conf", "etc/clock", "etc/ftpfs.ini", "etc/mail/*", "mail/*", "apps/lisa.app/config.ini",
	"apps/gamelib.app/config.ini", "apps/gamelib.app/thumbs/*", "res/wallpaper.*", "doom/savegame/*", "doom/*.cfg",
	"apps/antigrav.app/times.txt", "*.old", "*_avant_*", "apps/jet.app/main", "README.md"]

def match (rel, pats): return any (fnmatch.fnmatchcase (rel, p) for p in pats)

def walk (top):
	out = {}
	for dp, dns, fns in os.walk (top):
		dns.sort ()
		for f in sorted (fns):
			p = os.path.join (dp, f)
			out[os.path.relpath (p, top).replace (os.sep, "/")] = p
	return out

def edit (path, fn):
	with open (path, encoding = "utf-8", newline = "") as f: s = f.read ()
	s2 = fn (s)
	if s2 == s: sys.exit ("mkcard: %s: nothing changed (its text is not the expected one any more)" % path)
	with open (path, "w", encoding = "utf-8", newline = "") as f: f.write (s2)

def system_ini (s):
	nl = "\r\n" if "\r\n" in s else "\n"
	return s.rstrip ("\r\n") + nl + "# the PocketTerm35's card: the pocket mode" + nl + "shell=pocket" + nl

def autostart (s):
	lines = s.split ("\n")
	n = 0
	for i, l in enumerate (lines):
		if l.rstrip ("\r").startswith ("keyb ") and not l.startswith ("#"):
			lines[i] = "keyb US" + ("\r" if l.endswith ("\r") else ""); n += 1
	if n != 1: sys.exit ("mkcard: SD:/etc/autostart: %d keyb lines (1 expected)" % n)
	return "\n".join (lines)

def main ():
	ap = argparse.ArgumentParser ()
	ap.add_argument ("--out", default = os.path.join (ROOT, "SD_PocketTerm35"))
	a = ap.parse_args ()
	if not os.path.isfile (os.path.join (SRC, "kernel8-rpi4.img")): sys.exit ("mkcard: no sdcard/kernel8-rpi4.img (make -C kernel stage)")
	shutil.rmtree (a.out, ignore_errors = True)
	n = 0
	for r, p in walk (SRC).items ():
		if match (r, SKIP): continue
		d = os.path.join (a.out, r); os.makedirs (os.path.dirname (d), exist_ok = True)
		shutil.copy2 (p, d); n += 1
	for r, p in walk (OVL).items ():				# (the device's own files win)
		d = os.path.join (a.out, r); os.makedirs (os.path.dirname (d), exist_ok = True)
		shutil.copy2 (p, d)
	edit (os.path.join (a.out, "etc", "system.ini"), system_ini)
	edit (os.path.join (a.out, "etc", "autostart"), autostart)
	size = sum (os.path.getsize (p) for p in walk (a.out).values ())
	print ("mkcard: %s: %d files of sdcard/ + %d of tools/pocketterm35/overlay/ (%.0f MB)" % (a.out, n, len (walk (OVL)), size / 1e6))

if __name__ == "__main__":
	main ()
