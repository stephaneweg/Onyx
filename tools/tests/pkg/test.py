#!/usr/bin/env python3
"""tools/tests/pkg/test.py -- `pkg` (user/BinUtils/pkg.cpp, user/Libs/pkg/pkglib.h) on the PC, over the desktop
simulator's kapi: a small card made into a repository by tools/pkg/mkrepo.py (a key made for the
test), then installs, updates, removals, the settings kept, the needs, the system staged and committed,
a bad signature and a bad archive refused.

    python3 tools/tests/pkg/test.py <pkg binary> <work dir>     (tools/tests/run_pkg_test.sh)
"""
import os, shutil, subprocess, sys

PKG, OUT = os.path.abspath (sys.argv[1]), os.path.abspath (sys.argv[2])
ROOT = os.path.dirname (os.path.dirname (os.path.dirname (os.path.dirname (os.path.abspath (__file__)))))
SRC, CARD, W = OUT + "/src", OUT + "/card", OUT + "/w"
fails = 0

def check (cond, what):
	global fails
	print ("  %s %s" % ("ok  " if cond else "FAIL", what))
	if not cond: fails += 1

def put (path, data):
	os.makedirs (os.path.dirname (path), exist_ok = True)
	open (path, "wb").write (data if isinstance (data, bytes) else data.encode ())

def pkg (*args, board = "pi4"):
	env = dict (os.environ, SIM_SD = CARD, SIM_WRITES = W, SIM_RAM = OUT + "/ram", SIM_ARGS = " ".join (args), SIM_BOARD = board)
	r = subprocess.run ([PKG], env = env, capture_output = True, text = True, timeout = 60)
	return r.returncode, r.stdout + r.stderr

def mkrepo (*extra):
	r = subprocess.run ([sys.executable, ROOT + "/tools/pkg/mkrepo.py", "--sd", SRC, "--ini", OUT + "/packages.ini",
			     "--versions", OUT + "/versions.ini", "--out", CARD + "/repo", "--key", OUT + "/key.pem"] + list (extra),
			    capture_output = True, text = True)
	if r.returncode: print (r.stdout + r.stderr)
	return r.returncode

def card (rel): return os.path.join (W, rel)

shutil.rmtree (OUT, ignore_errors = True)
# ---- the source card and its packages ----
put (OUT + "/packages.ini", """
[onyx]
title = Onyx system
category = System
required = 1
restart = 1
files = kernel8-rpi4.img bin/ etc/
apps = Shell
config = etc/*
[*apps]
[app.beta]
needs = alpha
""")
put (SRC + "/kernel8-rpi4.img", b"KERNEL-1" * 100)
put (SRC + "/bin/hello", b"hello 1")
put (SRC + "/etc/autostart", "pkg commit\nrun menubar\n")
put (SRC + "/apps/menubar.app/main", b"menubar 1"); put (SRC + "/apps/menubar.app/app.txt", "name = Menu Bar\ncategory = Shell\n")
put (SRC + "/apps/alpha.app/main", b"alpha 1" * 1000); put (SRC + "/apps/alpha.app/app.txt", "name = Alpha\ncategory = Games\n")
put (SRC + "/apps/alpha.app/old.txt", "gone in 1.0.1")
put (SRC + "/apps/beta.app/main", b"beta 1"); put (SRC + "/apps/beta.app/app.txt", "name = Beta\ncategory = Games\n")
put (SRC + "/apps/terminal.app/main", b"terminal 1"); put (SRC + "/apps/terminal.app/app.txt", "name = Terminal\ncategory = Productivity\n")
# alpha's settings: the user's (config)
with open (OUT + "/packages.ini", "a") as f: f.write ("[app.alpha]\nconfig = apps/alpha.app/config.ini\n")
put (SRC + "/apps/alpha.app/config.ini", "speed = 1\n")
# the key, the card's settings
r = subprocess.run ([sys.executable, ROOT + "/tools/pkg/keygen.py", OUT + "/key.pem", CARD + "/etc/pkg/onyx.pub"], capture_output = True, text = True)
check (r.returncode == 0 and os.path.exists (CARD + "/etc/pkg/onyx.pub"), "keygen: the key pair")
put (CARD + "/etc/pkg/pkg.ini", "repo = SD:/repo\nkey = SD:/etc/pkg/onyx.pub\n")
check (mkrepo () == 0 and os.path.exists (CARD + "/repo/index.sig"), "mkrepo: the repository, signed")
check (os.path.exists (CARD + "/repo/pkgs/alpha-1.0.0.opk") and os.path.exists (CARD + "/repo/pkgs/onyx-2026.10.0.opk"), "mkrepo: a package each")
idx = open (CARD + "/repo/index.txt").read ()
check ("[beta]" in idx and "needs = alpha, kapi >=" in idx, "the index: beta needs alpha")
check ("[menubar]" not in idx, "a Shell app is in the system's package")

print ("install")
rc, o = pkg ("list"); check (rc == 0 and "0 packages" in o, "nothing installed")
rc, o = pkg ("add", "beta"); print (o)
check (rc == 0 and "alpha 1.0.0: " in o and "beta 1.0.0: " in o, "add beta: alpha first (its need), then beta")
check ("sim: kill dock" in o and "sim: launch dock" in o, "an app installed: the dock started again")
check (open (card ("apps/alpha.app/main"), "rb").read () == b"alpha 1" * 1000, "alpha's files on the card")
check (os.path.exists (card ("var/pkg/db/alpha.ini")) and os.path.exists (card ("var/pkg/db/beta.ini")), "the database")
rc, o = pkg ("add", "beta"); check (rc == 1 and "already installed" in o and "sim: kill dock" not in o, "add again: said, nothing done (the dock left alone)")
rc, o = pkg ("add", "nothing"); check (rc == 2 and "no such package" in o, "an unknown package")
rc, o = pkg ("info", "alpha"); check (rc == 0 and "installed: 1.0.0" in o, "info")

print ("update")
put (card ("apps/alpha.app/config.ini"), "speed = 9\n")			# the user changed it
put (SRC + "/apps/alpha.app/main", b"alpha 2" * 1000); put (SRC + "/apps/alpha.app/config.ini", "speed = 2\nnew = 1\n")
os.remove (SRC + "/apps/alpha.app/old.txt")
check (mkrepo () != 0, "mkrepo: changed files without a new version refused")
check (mkrepo ("--bump") == 0 and "alpha-1.0.1.opk" in os.listdir (CARD + "/repo/pkgs"), "mkrepo --bump: alpha 1.0.1")
rc, o = pkg ("check"); check (rc == 0 and "alpha" in o and "1.0.1" in o and "beta" not in o.split ("available")[0].split ("alpha")[0], "check: alpha's update")
rc, o = pkg ("update", "-a"); print (o)
check (rc == 0 and open (card ("apps/alpha.app/main"), "rb").read () == b"alpha 2" * 1000, "update -a: alpha 1.0.1")
check (open (card ("apps/alpha.app/config.ini")).read () == "speed = 9\n", "the user's setting kept")
check (os.path.exists (card ("apps/alpha.app/config.ini.new")), "the new one beside it (.new)")
check (not os.path.exists (card ("apps/alpha.app/old.txt")), "a file the new version has not: removed")
rc, o = pkg ("update", "-a"); check (rc == 1 and "up to date" in o, "nothing more to update")

print ("modes")
put (SRC + "/apps/beta.app/main", b"beta 2"); mkrepo ("--bump")
rc, o = pkg ("upgrade"); check ("beta 1.0.0 -> 1.0.1 available (manual)" in o and open (card ("apps/beta.app/main"), "rb").read () == b"beta 1", "upgrade: a manual one only listed")
rc, o = pkg ("mode", "beta", "auto"); check (rc == 0, "mode beta auto")
rc, o = pkg ("upgrade"); check (open (card ("apps/beta.app/main"), "rb").read () == b"beta 2", "upgrade: an automatic one updated")

print ("remove")
rc, o = pkg ("delete", "alpha"); check (rc == 2 and "beta needs it" in o, "alpha: refused, beta needs it")
rc, o = pkg ("delete", "beta"); check (rc == 0 and not os.path.exists (card ("apps/beta.app/main")), "delete beta: its files")
check (not os.path.exists (card ("apps/beta.app")), "its empty folder gone")
rc, o = pkg ("delete", "beta"); check (rc == 1 and "not installed" in o, "delete again: said")
rc, o = pkg ("delete", "alpha"); check (rc == 0 and os.path.exists (card ("apps/alpha.app/config.ini")) and not os.path.exists (card ("apps/alpha.app/main")), "delete alpha: the user's changed setting kept")

print ("a running app")
rc, o = pkg ("add", "terminal"); check (rc == 0, "add terminal")
put (SRC + "/apps/terminal.app/main", b"terminal 2"); mkrepo ("--bump")
rc, o = pkg ("update", "terminal"); check (rc == 2 and "terminal is running" in o, "update while it runs: refused")

print ("the system: staged, committed at boot")
rc, o = pkg ("add", "onyx"); print (o)
check (rc == 0 and "staged" in o and os.path.exists (card ("var/pkg/stage/onyx/kernel8-rpi4.img")), "add onyx: staged")
check (not os.path.exists (card ("kernel8-rpi4.img")), "the kernel not replaced yet")
rc, o = pkg ("list"); check ("staged: restart to finish" in o, "list: staged")
rc, o = pkg ("commit"); print (o)
check ("[sim: reboot]" in o and os.path.exists (card ("kernel8-rpi4.img")) and os.path.exists (card ("bin/hello")), "commit: moved in, then a reboot")
check (os.path.exists (card ("var/pkg/db/onyx.ini")) and not os.path.exists (card ("var/pkg/stage")), "commit: the database, the stage gone")
# the sessions (2026-10-08): the card's autostart from before them split once by the commit (SystemKit's session_migrate)
asf = open (card ("etc/autostart")).read () if os.path.exists (card ("etc/autostart")) else ""
dkf = open (card ("etc/session/desktop")).read () if os.path.exists (card ("etc/session/desktop")) else ""
check ("\nsession\n" in asf and "run menubar" not in asf and "run menubar\n" in dkf and "SD:/etc/autostart split" in o, "commit: the autostart split into the desktop's session")
rc, o = pkg ("commit"); check ("split" not in o and open (card ("etc/autostart")).read () == asf, "commit again: not split twice")
rc, o = pkg ("delete", "onyx"); check (rc == 2 and "part of the system" in o, "the system cannot be removed")

print ("a file moved into a package of its own (pkgman's own package)")
ini = open (OUT + "/packages.ini").read ().replace ("[onyx]\n", "[mover]\ntitle = Mover\ncategory = System\nrequired = 1\nfiles = bin/hello\n[onyx]\nneeds = mover\n", 1)
open (OUT + "/packages.ini", "w").write (ini)
put (SRC + "/etc/wpa_supplicant.conf", 'network={\n\tssid="home"\n\tpsk="secret"\n}\n')	# the user's: never packaged
check (mkrepo ("--bump") == 0, "mkrepo --bump: onyx without bin/hello, mover with it")
import zipfile
onyx_opk = sorted (f for f in os.listdir (CARD + "/repo/pkgs") if f.startswith ("onyx-"))[-1]
check ("etc/wpa_supplicant.conf" not in zipfile.ZipFile (CARD + "/repo/pkgs/" + onyx_opk).namelist (), "the Wi-Fi settings never packaged")
rc, o = pkg ("update", "onyx"); print (o)
check (rc == 0 and os.path.exists (card ("var/pkg/db/mover.ini")) and "staged" in o, "update onyx: mover first (its need, at once), onyx staged")
rc, o = pkg ("commit"); print (o)
check (os.path.exists (card ("bin/hello")), "commit: the file onyx dropped is kept, mover has it")
rc, o = pkg ("delete", "mover"); check (rc == 2, "mover cannot be removed")

print ("a package renamed (replaces =: writer became letters)")
put (SRC + "/apps/olda.app/main", b"olda 1"); put (SRC + "/apps/olda.app/app.txt", "name = Old\ncategory = Productivity\n")
put (SRC + "/docs/tour.txt", "a sample\n")
with open (OUT + "/packages.ini", "a") as f: f.write ("[olda-samples]\ntitle = Old samples\nfiles = docs/*.txt\nconfig = docs/*.txt\nneeds = olda\n")
check (mkrepo ("--bump") == 0, "mkrepo: olda and its samples")
rc, o = pkg ("add", "olda-samples"); check (rc == 0 and os.path.exists (card ("docs/tour.txt")), "add olda-samples (and olda)")
rc, o = pkg ("mode", "olda", "auto"); check (rc == 0, "mode olda auto")
shutil.rmtree (SRC + "/apps/olda.app")
put (SRC + "/apps/newa.app/main", b"newa 1"); put (SRC + "/apps/newa.app/app.txt", "name = New\ncategory = Productivity\n")
ini = open (OUT + "/packages.ini").read ().replace ("[olda-samples]\ntitle = Old samples", "[newa-samples]\nreplaces = olda-samples\ntitle = New samples").replace ("needs = olda\n", "needs = newa\n")
open (OUT + "/packages.ini", "w").write (ini + "[app.newa]\nreplaces = olda\n")
check (mkrepo ("--bump") == 0, "mkrepo: newa replaces olda, newa-samples olda-samples")
idx = open (CARD + "/repo/index.txt").read ()
check ("replaces = olda\n" in idx and "[olda]" not in idx, "the index: olda gone, newa replaces it")
rc, o = pkg ("check"); check (rc == 0 and "olda" in o and "newa" not in o.split ("olda")[0], "check: olda's update is newa")
rc, o = pkg ("update", "olda", "olda-samples"); print (o)
check (rc == 0 and os.path.exists (card ("apps/newa.app/main")) and not os.path.exists (card ("apps/olda.app")), "update olda: newa in, olda's files gone")
check (not os.path.exists (card ("var/pkg/db/olda.ini")) and not os.path.exists (card ("var/pkg/db/olda-samples.ini")), "olda, olda-samples: out of the database")
check (os.path.exists (card ("docs/tour.txt")) and not os.path.exists (card ("docs/tour.txt.new")), "the sample taken over: kept, no .new")
rc, o = pkg ("info", "newa"); check ("auto" in o, "newa: olda's mode (auto)")
rc, o = pkg ("check"); check ("olda" not in o and "newa" not in o, "nothing more for them")

print ("two boards (the Pi 5's distribution: mkrepo --board pi5, the repository's pi5/ folder)")
SRC5 = OUT + "/src5"
shutil.copytree (SRC, SRC5); os.remove (SRC5 + "/kernel8-rpi4.img"); put (SRC5 + "/kernel_2712.img", b"KERNEL-5" * 100)
ini = open (OUT + "/packages.ini").read ().replace ("files = kernel8-rpi4.img bin/ etc/\n", "files = kernel8-rpi4.img bin/ etc/\nfiles.pi5 = kernel_2712.img bin/ etc/\n", 1)
open (OUT + "/packages.ini", "w").write (ini + "[pi4only]\ntitle = Pi 4 only\nboards = pi4\nfiles = docs/\n")
r = subprocess.run ([sys.executable, ROOT + "/tools/pkg/mkrepo.py", "--board", "pi5", "--sd", SRC5, "--ini", OUT + "/packages.ini",
		     "--versions", OUT + "/versions5.ini", "--out", CARD + "/repo/pi5", "--key", OUT + "/key.pem"], capture_output = True, text = True)
idx5 = open (CARD + "/repo/pi5/index.txt").read () if r.returncode == 0 else ""
check (r.returncode == 0 and "\nboard = pi5\n" in idx5, "mkrepo --board pi5: the index says board = pi5")
on5 = idx5.split ("[onyx]")[1].split ("\n[")[0] if "[onyx]" in idx5 else ""
z5 = [f for f in os.listdir (CARD + "/repo/pi5/pkgs") if f.startswith ("onyx-")] if r.returncode == 0 else []
check (z5 and "kernel_2712.img" in zipfile.ZipFile (CARD + "/repo/pi5/pkgs/" + z5[0]).namelist () and "kapi >=" not in on5, "the Pi 5's onyx: its kernel (files.pi5), no kapi need (it brings the kernel)")
check ("[pi4only]" not in idx5, "a package of boards = pi4: not in the Pi 5's")
put (CARD + "/etc/pkg/pkg.ini", "repo = SD:/repo/pi5\nkey = SD:/etc/pkg/onyx.pub\n")
rc, o = pkg ("check"); check (rc != 0 and "for the pi5, this is a pi4" in o, "a Pi 4 reading the Pi 5's repository: refused")
rc, o = pkg ("check", board = "pi5"); check (rc in (0, 1) and "refused" not in o and "signature" not in o, "a Pi 5 reading it: taken (checked: nothing to update)")
put (CARD + "/etc/pkg/pkg.ini", "repo = SD:/repo\nkey = SD:/etc/pkg/onyx.pub\n")
rc, o = pkg ("check", board = "pi5"); check (rc != 0 and "for the pi4, this is a pi5" in o, "a Pi 5 reading the Pi 4's repository: refused")
rc, o = pkg ("check"); check (rc == 0, "the Pi 4 reading its own: taken")

print ("trust")
idx = open (CARD + "/repo/index.txt").read ()
open (CARD + "/repo/index.txt", "w").write (idx.replace ("title = Alpha", "title = Evil"))
rc, o = pkg ("check"); check ("signature is wrong" in o, "a changed index: its signature refused")
open (CARD + "/repo/index.txt", "w").write (idx)
p = CARD + "/repo/pkgs/alpha-1.0.1.opk"; b = bytearray (open (p, "rb").read ()); b[-30] ^= 1; open (p, "wb").write (bytes (b))
rc, o = pkg ("add", "alpha"); check (rc == 2 and "not the one of the index" in o, "a changed archive refused")
check (not os.path.exists (card ("apps/alpha.app/main")), "... and nothing written")

print ("%d failure(s)" % fails)
sys.exit (1 if fails else 0)
