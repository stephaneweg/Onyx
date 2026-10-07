#!/usr/bin/env python3
"""arc_sample.py OUT.zip -- the Archiver's sample archive for the screenshots (shots.sh): some of Onyx's
own sources in a ZIP, their dates fixed (the pictures stay the same from one run to the next)."""
import sys, os, zipfile, random
ROOT = os.path.dirname (os.path.dirname (os.path.dirname (os.path.dirname (os.path.abspath (__file__)))))
random.seed (5)
def when (i): return (2026, 9, 14 + i % 15, 9 + i % 9, (i * 7) % 60, 0)
with zipfile.ZipFile (sys.argv[1], "w", zipfile.ZIP_DEFLATED) as z:
	i = 0
	for top, n in (("kernel/sys", 14), ("kernel/include/kern", 12), ("docs/archiver", 2), ("user/Apps/irc", 1), ("user/Runtime/libc", 4), ("tools/screenshot", 5)):
		for f in sorted (os.listdir (os.path.join (ROOT, top)))[:n]:
			p = os.path.join (ROOT, top, f)
			if not os.path.isfile (p): continue
			info = zipfile.ZipInfo (top + "/" + f, when (i)); info.compress_type = zipfile.ZIP_DEFLATED; i += 1
			z.writestr (info, open (p, "rb").read ())
	for f, name in (("screenshots/irc.png", "docs/images/irc.png"), ("screenshots/fileviewer.png", "docs/images/fileviewer.png")):
		info = zipfile.ZipInfo (name, when (i)); info.compress_type = zipfile.ZIP_STORED; i += 1
		z.writestr (info, open (os.path.join (ROOT, f), "rb").read ())
	info = zipfile.ZipInfo ("README.md", when (3)); info.compress_type = zipfile.ZIP_DEFLATED
	z.writestr (info, open (os.path.join (ROOT, "README.md"), "rb").read ())
	z.comment = b"Onyx sources, 2026-10-01"
