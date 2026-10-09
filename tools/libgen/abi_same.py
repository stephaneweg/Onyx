#!/usr/bin/env python3
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
#
# abi_same.py -- two builds of one Onyx shared library must have the same export table, slot by slot: the
# desktop's UIKit (lib/uikit.so, Elegant's port) and the pocket one (lib/pocket/uikit.so, PocketUI's port) are
# built from the same sources and objects but their port, and PocketUI loads the pocket one under the name
# SD:/lib/uikit.so (kapi_lib_open_as) -- every program, built against the desktop's import library (its stubs
# call the entries BY SLOT), must find the same function at every slot (docs/POCKETUI-TECH-STUDY.md section 5.4,
# docs/03 "One UIKit per server").
#
#   python3 tools/libgen/abi_same.py lib/uikit.so lib/pocket/uikit.so [--tables lib/uikit_table.S lib/pocket/uikit_table.S]
#
# The checks (each failure printed; exit 1 if any):
#   1. each library's export table (the symbol onyx_lib_table, read from the ELF file: its version -- the number of
#      entries --, its size, then one pointer per entry, given by the dynamic relocations) has the same version and
#      size in both;
#   2. at every slot (and the init's), the function the pointer names (by the library's symbol table) is the same
#      symbol in both;
#   3. with --tables: libgen's two generated tables (*_table.S) list the same entries in the same order (their
#      .word / .quad lines, comments stripped).
# Run by `make libs` (user/Makefile: lib/pocket/abi_same.stamp) after both libraries are linked, and by
# tools/tests/shlib/abi_same_test.sh.
import argparse, struct, sys

def die (msg):
	sys.exit ("abi_same: " + msg)

class Elf:
	def __init__ (self, path):
		self.path = path
		d = self.d = open (path, "rb").read ()
		if d[:4] != b"\x7fELF" or d[4] != 2 or d[5] != 1:
			die ("%s: not a 64-bit little-endian ELF file" % path)
		shoff, = struct.unpack_from ("<Q", d, 0x28)
		shentsize, shnum, shstrndx = struct.unpack_from ("<HHH", d, 0x3A)
		self.secs = [struct.unpack_from ("<IIQQQQIIQQ", d, shoff + i * shentsize) for i in range (shnum)]
		shstr = self.secs[shstrndx]
		def secname (s):
			o = shstr[4] + s[0]
			return d[o : d.index (b"\0", o)].decode ()
		self.names = [secname (s) for s in self.secs]
		self.syms = []					# (name, value, size, type, shndx)
		for s in self.secs:
			if s[1] not in (2, 11):			# SHT_SYMTAB, SHT_DYNSYM
				continue
			stroff = self.secs[s[6]][4]
			for i in range (s[5] // 24):
				n, info, other, shndx, value, size = struct.unpack_from ("<IBBHQQ", d, s[4] + i * 24)
				name = d[stroff + n : d.index (b"\0", stroff + n)].decode ()
				if name:
					self.syms.append ((name, value, size, info & 15, shndx))
		self.by_addr = {}
		for name, value, size, typ, shndx in self.syms:	# functions first, then any symbol (a better name wins)
			if typ == 2 and shndx != 0:
				self.by_addr.setdefault (value, name)
		for name, value, size, typ, shndx in self.syms:
			if shndx != 0 and typ in (0, 1):
				self.by_addr.setdefault (value, name)
		self.dynsym = []
		for s in self.secs:
			if s[1] == 11:
				stroff = self.secs[s[6]][4]
				for i in range (s[5] // 24):
					n, info, other, shndx, value, size = struct.unpack_from ("<IBBHQQ", d, s[4] + i * 24)
					self.dynsym.append (d[stroff + n : d.index (b"\0", stroff + n)].decode ())
		self.relocs = {}				# address -> ("rel", addend) | ("sym", name, addend)
		for s in self.secs:
			if s[1] != 4:				# SHT_RELA
				continue
			for i in range (s[5] // 24):
				off, info, addend = struct.unpack_from ("<QQq", d, s[4] + i * 24)
				rtype, sym = info & 0xFFFFFFFF, info >> 32
				if rtype == 1027:		# R_AARCH64_RELATIVE
					self.relocs[off] = ("rel", addend)
				elif rtype in (257, 1025):	# R_AARCH64_ABS64, R_AARCH64_GLOB_DAT
					self.relocs[off] = ("sym", self.dynsym[sym] if sym < len (self.dynsym) else "?", addend)

	def symbol (self, name):
		for n, value, size, typ, shndx in self.syms:
			if n == name and shndx != 0:
				return value, size
		die ("%s: no symbol %s (not an Onyx library?)" % (self.path, name))

	def read (self, addr, n):			# the bytes at a virtual address
		for s in self.secs:
			if s[1] != 8 and s[3] <= addr < s[3] + s[5] and s[3] != 0:	# (not NOBITS; mapped)
				o = s[4] + addr - s[3]
				return self.d[o : o + n]
		die ("%s: address 0x%x is in no section" % (self.path, addr))

	def pointer (self, addr):			# what the pointer stored at addr names, once relocated
		r = self.relocs.get (addr)
		if r is None:
			v, = struct.unpack ("<Q", self.read (addr, 8))
			return self.by_addr.get (v, "0x%x" % v) if v else "0"
		if r[0] == "rel":
			return self.by_addr.get (r[1], "0x%x" % r[1])
		return r[1] if r[2] == 0 else "%s%+d" % (r[1], r[2])

	def table (self):				# (version, size, [init, entry 0, entry 1, ...])
		addr, size = self.symbol ("onyx_lib_table")
		version, tsize = struct.unpack ("<II", self.read (addr, 8))
		if tsize != 16 + 8 * version:
			die ("%s: the table's size (%d) is not 16 + 8 x its version (%d)" % (self.path, tsize, version))
		return version, tsize, [self.pointer (addr + 8 + 8 * i) for i in range (version + 1)]

def table_lines (path):
	out = []
	for line in open (path, encoding = "utf-8"):
		line = line.split ("/*")[0].strip ()
		if line.startswith (".word") or line.startswith (".quad"):
			out.append (" ".join (line.split ()))
	return out

def main ():
	ap = argparse.ArgumentParser ()
	ap.add_argument ("--nm", default = None, help = "(accepted for the Makefiles' symmetry: the ELF files are read directly)")
	ap.add_argument ("--tables", nargs = 2, default = None, metavar = ("A_TABLE_S", "B_TABLE_S"))
	ap.add_argument ("a")
	ap.add_argument ("b")
	o = ap.parse_args ()
	A, B = Elf (o.a), Elf (o.b)
	va, sa, ea = A.table ()
	vb, sb, eb = B.table ()
	bad = 0
	if va != vb or sa != sb:
		print ("abi_same: the tables differ: %s version %d (%d bytes), %s version %d (%d bytes)" % (o.a, va, sa, o.b, vb, sb))
		bad += 1
	for i in range (min (len (ea), len (eb))):
		if ea[i] != eb[i]:
			bad += 1
			if bad <= 20:
				print ("abi_same: %s: %s in %s, %s in %s" % ("the init" if i == 0 else "slot %d" % (i - 1), ea[i], o.a, eb[i], o.b))
	unnamed = [e for e in ea if e.startswith ("0x")]
	if unnamed:
		print ("abi_same: %d entries name no symbol (a stripped library?): %s" % (len (unnamed), " ".join (unnamed[:5])))
		bad += 1
	if o.tables:
		ta, tb = table_lines (o.tables[0]), table_lines (o.tables[1])
		if ta != tb:
			bad += 1
			diff = next ((i for i in range (min (len (ta), len (tb))) if ta[i] != tb[i]), min (len (ta), len (tb)))
			print ("abi_same: the generated tables differ at their line %d: %s / %s" % (diff, ta[diff] if diff < len (ta) else "(end)",
				tb[diff] if diff < len (tb) else "(end)"))
	if bad:
		print ("abi_same: FAILED -- %s and %s are not the same library's interface (%d difference%s)" % (o.a, o.b, bad, "" if bad == 1 else "s"))
		sys.exit (1)
	print ("abi_same: %s and %s: the same %d entries, slot by slot" % (o.a, o.b, va))

if __name__ == "__main__":
	main ()
