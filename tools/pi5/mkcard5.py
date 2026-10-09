#!/usr/bin/env python3
"""tools/pi5/mkcard5.py -- the Raspberry Pi 5's card, sdcard5/, made from sdcard/ (docs/PI5-PORT.md §4.4).

    python3 tools/pi5/mkcard5.py            (run by `make -C kernel BOARD=pi5 stage`, before the Pi 5's
                                             kernel and the programs are copied in)

sdcard/ is the one source of what is not built (etc/, fonts/, res/, the apps' bundles, the samples...):
sdcard5/ gets every file of it, except the Pi 4's own (its kernel, its boot firmware, its device trees,
its Wi-Fi files, its config.txt / cmdline.txt, the database of what is installed), then the Pi 5's own
from tools/pi5/overlay/ (config.txt, cmdline.txt, the bcm2712 device trees, overlays/, the Pi 5's Wi-Fi
file names, etc/pkg/pkg.ini: the repository's pi5/ folder). A file of sdcard5/ that is in neither is
removed -- but the user's own (the Wi-Fi network, the mail accounts, the clock...: .gitignore's
private files) are left as they are. Copies only what changed (size and time), so a second run is quick.

MIT licence (docs/LICENSING.md).
"""
import fnmatch, os, shutil, sys

HERE = os.path.dirname (os.path.abspath (__file__))
ROOT = os.path.dirname (os.path.dirname (HERE))
SRC = os.path.join (ROOT, "sdcard")
OVL = os.path.join (HERE, "overlay")
DST = os.path.join (ROOT, "sdcard5")

# the Pi 4's own files: not on the Pi 5's card
PI4_ONLY = ["kernel8-rpi4.img", "kernel8-rpi4.img.old", "start4.elf", "fixup4.dat", "armstub8-rpi4.bin",
	    "bcm2711-*.dtb", "config.txt", "cmdline.txt", "firmware/brcmfmac*-sdio.bin",
	    "firmware/brcmfmac*-sdio.txt", "firmware/brcmfmac*-sdio.clm_blob", "var/pkg/db/*"]
# the card's own state and the user's private files: neither copied over nor removed (as .gitignore's)
KEEP = ["etc/wpa_supplicant.conf", "etc/clock", "etc/ftpfs.ini", "etc/mail/*", "mail/*", "var/*",
	"apps/lisa.app/config.ini", "apps/gamelib.app/config.ini", "apps/gamelib.app/thumbs/*",
	"res/wallpaper.*", "doom/savegame/*", "doom/*.cfg", "kernel_2712.img", "kernel_2712.img.old"]

def match (rel, pats): return any (fnmatch.fnmatchcase (rel, p) for p in pats)

def walk (top):
	out = {}
	for dp, dns, fns in os.walk (top):
		dns.sort ()
		for f in sorted (fns):
			p = os.path.join (dp, f)
			out[os.path.relpath (p, top).replace (os.sep, "/")] = p
	return out

def same (a, b):
	try:
		sa, sb = os.stat (a), os.stat (b)
		return sa.st_size == sb.st_size and int (sa.st_mtime) == int (sb.st_mtime)
	except OSError: return False

def main ():
	if not os.path.isdir (SRC): sys.exit ("mkcard5: no sdcard/")
	want = {r: p for r, p in walk (SRC).items () if not match (r, PI4_ONLY) and not match (r, KEEP)}
	want.update (walk (OVL))						# (the Pi 5's own files win)
	os.makedirs (DST, exist_ok = True)
	copied = removed = 0
	for r, p in want.items ():
		d = os.path.join (DST, r)
		if same (p, d): continue
		os.makedirs (os.path.dirname (d), exist_ok = True)
		shutil.copy2 (p, d); copied += 1
	for r, p in walk (DST).items ():
		if r in want or match (r, KEEP): continue
		os.remove (p); removed += 1
	for dp, dns, fns in os.walk (DST, topdown = False):		# (the folders left empty)
		if dp != DST and not os.listdir (dp): os.rmdir (dp)
	print ("mkcard5: sdcard5/ from sdcard/ + tools/pi5/overlay/: %d copied, %d removed" % (copied, removed))

if __name__ == "__main__":
	main ()
