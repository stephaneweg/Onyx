#!/usr/bin/env python3
"""pkg_sample.py -- a card for the Package Manager's screenshots (shots.sh pkgman): the real card seen
through symbolic links, but its own etc/pkg (a repository of its own, a key made here) and var/pkg/db
(a few apps not installed); the repository made by tools/pkg/mkrepo.py from sdcard/ with a few newer
versions -- updates to show.

    python3 tools/tests/desktop_sim/pkg_sample.py <out dir>   -> <out>/card (SIM_SD)
"""
import os, shutil, subprocess, sys

ROOT = os.path.dirname (os.path.dirname (os.path.dirname (os.path.dirname (os.path.abspath (__file__)))))
OUT = os.path.abspath (sys.argv[1]); CARD = OUT + "/card"; SD = ROOT + "/sdcard"
NOT_INSTALLED = ["snesemu", "n64emu", "irc", "doom", "tetris", "paint", "koton-samples", "ledger-samples"]
NEWER = { "onyx": "2026.11.0", "jet": "1.1.0", "archiver": "1.0.1", "koton": "1.0.1", "paint": "1.0.1" }

shutil.rmtree (CARD, ignore_errors = True); os.makedirs (CARD)
for e in os.listdir (SD):
	if e not in ("etc", "var"): os.symlink (os.path.join (SD, e), os.path.join (CARD, e))
os.makedirs (CARD + "/etc")
for e in os.listdir (SD + "/etc"):
	if e != "pkg": os.symlink (os.path.join (SD, "etc", e), os.path.join (CARD, "etc", e))
os.makedirs (CARD + "/etc/pkg"); os.makedirs (CARD + "/var/pkg/db")
for f in os.listdir (SD + "/var/pkg/db"):
	if f[:-4] not in NOT_INSTALLED: shutil.copy (os.path.join (SD, "var/pkg/db", f), CARD + "/var/pkg/db/" + f)
open (CARD + "/etc/pkg/pkg.ini", "w").write ("repo = SD:/pkgrepo\nkey = SD:/etc/pkg/onyx.pub\n")
key = OUT + "/key.pem"
if os.path.exists (key): os.remove (key)
subprocess.check_call ([sys.executable, ROOT + "/tools/pkg/keygen.py", key, CARD + "/etc/pkg/onyx.pub"], stdout = subprocess.DEVNULL)
v = open (ROOT + "/tools/pkg/versions.ini").read ().splitlines ()
v = [("%s = %s" % (l.split ("=")[0].strip (), NEWER[l.split ("=")[0].strip ()]) if "=" in l and l.split ("=")[0].strip () in NEWER else l) for l in v]
open (OUT + "/versions.ini", "w").write ("\n".join (v) + "\n")
subprocess.check_call ([sys.executable, ROOT + "/tools/pkg/mkrepo.py", "--sd", SD, "--out", CARD + "/pkgrepo",
			"--versions", OUT + "/versions.ini", "--key", key], stdout = subprocess.DEVNULL, stderr = subprocess.DEVNULL)
print ("pkg_sample:", CARD)
