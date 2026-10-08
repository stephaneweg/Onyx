#!/usr/bin/env python3
# MIT License -- Copyright (c) 2026 the Onyx authors.
# tools/libgen/check_stubs.py -- every program's AppKit import stubs against appkit.abi.
#
# A program reaches AppKit through stubs (user/lib/appkit_stubs.o, made by libgen from appkit.abi): each one
# loads its function from AppKit's table at the slot appkit.abi gives the name. A program linked with stubs
# older than appkit.abi calls the wrong functions -- on 2026-10-08 every program of a build did (an
# order-only prerequisite in user/Makefile: they were not linked again), and none could start on the Pi.
#
#   python3 tools/libgen/check_stubs.py user/Kits/appkit/appkit.abi sdcard     (tools/pkg/publish.sh runs it)
#
# Exit 1 when a stub's slot differs from appkit.abi's; 0 otherwise (also when the toolchain's nm / objdump
# cannot be found: said, nothing checked).
import os, re, shutil, subprocess, sys

NM, OBJDUMP = 'aarch64-none-elf-nm', 'aarch64-none-elf-objdump'

def main ():
	if len (sys.argv) < 3:
		print ("usage: check_stubs.py appkit.abi <dir>..."); return 2
	if not shutil.which (NM) or not shutil.which (OBJDUMP):
		print ("check_stubs: no %s on the PATH: the stubs are not checked" % NM); return 0
	abi = {}
	for l in open (sys.argv[1]):
		p = l.split ()
		if len (p) == 2 and p[0].isdigit (): abi[p[1]] = int (p[0])
	bad = n = nst = 0
	for root in sys.argv[2:]:
		for dp, dn, fn in os.walk (root):
			for f in fn:
				path = os.path.join (dp, f)
				try:
					with open (path, 'rb') as fh:
						if fh.read (4) != b'\x7fELF': continue
				except OSError: continue
				syms = subprocess.run ([NM, path], capture_output=True, text=True).stdout
				stubs = {int (m.group (1), 16): m.group (2) for m in re.finditer (r'^([0-9a-f]+) [WT] (\w+)$', syms, re.M)
					 if m.group (2) in abi}
				if not stubs: continue
				dis = subprocess.run ([OBJDUMP, '-d', '--no-show-raw-insn', path], capture_output=True, text=True).stdout.splitlines ()
				where = {}
				for i, l in enumerate (dis):
					m = re.match (r'\s*([0-9a-f]+):', l)
					if m: where[int (m.group (1), 16)] = i
				n += 1
				for addr, name in stubs.items ():
					i = where.get (addr)
					if i is None: continue
					block = "\n".join (dis[i:i + 5])
					if 'x16, #0x8000' not in block: continue	# (not a stub: AppKit's own function)
					m = re.search (r'ldr\s+x16, \[x16, #(\d+)\]', block)
					if not m: continue
					nst += 1
					if int (m.group (1)) // 8 != abi[name]:
						bad += 1
						if bad <= 10: print ("check_stubs: %s: %s at slot %d, appkit.abi says %d" % (path, name, int (m.group (1)) // 8, abi[name]))
	print ("check_stubs: %d programs, %d stubs, %d wrong" % (n, nst, bad))
	return 1 if bad else 0

sys.exit (main ())
