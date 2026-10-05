#!/usr/bin/env python3
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
#
# libgen.py -- the two halves of an Onyx shared library's interface (docs/SHARED-LIBS-PLAN.md, docs/03
# "Shared libraries"), from the library's objects and its append-only list of entries:
#
#   <lib>.abi      (kept in git) one entry per line, "<slot> <symbol>": the functions the library
#                  exports, in the order of its export table. APPEND-ONLY: this tool only ever adds
#                  lines at the end (the new global functions of the objects); a line is never moved
#                  or removed, so a program built against an older table keeps working. The table's
#                  VERSION is its number of entries.
#   --table        the library side: an assembly file defining `onyx_lib_table` -- version, size,
#                  init, then one pointer per entry (the kernel relocates them when it loads the
#                  library).
#   --stubs        the program side: one import stub per entry, under the entry's own name -- it
#                  loads the entry's pointer from the table and jumps there. The program's code calls
#                  `FT_Load_Glyph` or `uikit::Widget::invalidate` as before; the linker binds the call
#                  to the stub. Stubs are weak: a program's own definition of a name wins, as it won
#                  over a static library's member.
#   --bind         the program side: a constructor (priority 101: before the program's own) that
#                  opens the library and sets the pointer the stubs go through (user/Runtime/lib.h lib_bind).
#
# What a library exports: its objects' global functions (nm `T`) matching --export (a regular
# expression; default: all of them), less --exclude. Inline functions (weak) are not exported: each
# side compiles its own. Global DATA cannot be imported by a program (it is linked at a fixed address,
# the library is not): a global variable among the objects is an error, unless --allow-data matches
# it (the library then reaches it another way: uikit's globals are the program's, handed to the library
# with --data).
#
# C++ (--vtables): a class whose vtable the compiler emits only with its key function (the library's
# object) is referenced by the programs that construct or derive it. The program side gets a COPY of
# each such vtable (weak), its slots naming the same functions -- which the stubs resolve; the
# virtual functions it names that are inline in the library (weak) become entries too. No library
# address is ever in a program.
#
#   --data SYM: the bind constructor hands the library `SYM` (a `void *const SYM[]` of the program,
#   `SYM_count` entries: the addresses of the variables the library shares with it) in TLibImports.
#
#   python3 tools/libgen/libgen.py --name ft --abi user/ft/ft.abi --init ft_lib_init \
#       --export '^FT_' --table lib/ft_table.S --stubs lib/ft_stubs.S --bind lib/ft_bind.cpp objs...
#   --frozen: new functions are an error instead of being appended (a release build).
import argparse, os, re, struct, subprocess, sys

def die (msg):
	sys.exit ("libgen: " + msg)

ap = argparse.ArgumentParser ()
ap.add_argument ("--name", required = True)
ap.add_argument ("--abi", required = True)
ap.add_argument ("--init", required = True, help = "the library's init function (the table's third field)")
ap.add_argument ("--nm", default = "aarch64-none-elf-nm")
ap.add_argument ("--export", default = ".")
ap.add_argument ("--exclude", default = None)
ap.add_argument ("--export-file", default = None, help = "only the names listed in this file (one a line)")
ap.add_argument ("--allow-data", default = None)
ap.add_argument ("--frozen", action = "store_true")
ap.add_argument ("--vtables", action = "store_true", help = "C++: copies of the objects' vtables for the programs")
ap.add_argument ("--data", default = None, help = "the program's table of shared variables' addresses (bind)")
ap.add_argument ("--fixed-table", default = None, help = "the stubs read their entry at this fixed address + 8 * slot (AppKit: the kernel copies the table there for every program; no bind)")
ap.add_argument ("--table")
ap.add_argument ("--stubs")
ap.add_argument ("--bind")
ap.add_argument ("objs", nargs = "+")
a = ap.parse_args ()

# ---- the objects' symbols ------------------------------------------------------------------------------
out = subprocess.run ([a.nm, "--defined-only", "-g"] + a.objs, capture_output = True, text = True)
if out.returncode != 0:
	die ("nm failed: " + out.stderr.strip ())
funcs, weakfuncs, data = set (), set (), set ()
for line in out.stdout.splitlines ():
	f = line.split ()
	if len (f) != 3:
		continue
	kind, sym = f[1], f[2]
	if kind == "T":
		funcs.add (sym)
	elif kind == "W":
		weakfuncs.add (sym)
	elif kind in "BDRGS":
		data.add (sym)

# ---- C++: the vtables, read from the objects (ELF64 little-endian relocatable files) --------------------
def elf_vtables (path):
	d = open (path, "rb").read ()
	if d[:4] != b"\x7fELF":
		return {}
	shoff, = struct.unpack_from ("<Q", d, 0x28)
	shentsize, shnum, shstrndx = struct.unpack_from ("<HHH", d, 0x3A)
	secs = [struct.unpack_from ("<IIQQQQIIQQ", d, shoff + i * shentsize) for i in range (shnum)]
	symtab = next ((s for s in secs if s[1] == 2), None)		# SHT_SYMTAB
	if symtab is None:
		return {}
	stroff = secs[symtab[6]][4]
	def name_at (o):
		return d[stroff + o : d.index (b"\0", stroff + o)].decode ()
	syms = []
	for i in range (symtab[5] // 24):
		n, info, other, shndx, value, size = struct.unpack_from ("<IBBHQQ", d, symtab[4] + i * 24)
		syms.append ((name_at (n), info, shndx, value, size))
	relas = {}						# section index -> { offset: (symbol index, addend) }
	for s in secs:
		if s[1] == 4:					# SHT_RELA
			m = relas.setdefault (s[7], {})
			for i in range (s[5] // 24):
				off, info, addend = struct.unpack_from ("<QQq", d, s[4] + i * 24)
				m[off] = (info >> 32, info & 0xFFFFFFFF, addend)
	out = {}
	for name, info, shndx, value, size in syms:
		if not name.startswith ("_ZTV") or (info >> 4) not in (1, 2) or shndx == 0 or shndx >= 0xFF00:
			continue				# (a global or weak vtable defined here)
		sec = secs[shndx]
		rel = relas.get (shndx, {})
		slots = []
		for o in range (value, value + size, 8):
			if o in rel:
				si, rtype, addend = rel[o]
				if rtype != 257:		# R_AARCH64_ABS64
					die ("%s: %s: relocation type %d in a vtable" % (path, name, rtype))
				if syms[si][0] == "":
					die ("%s: %s names a local function: it cannot be copied for the programs" % (path, name))
				slots.append ((syms[si][0], addend))
			else:
				slots.append ((None, struct.unpack_from ("<q", d, sec[4] + o)[0] if sec[1] != 8 else 0))
		out[name] = slots
	return out

vtables = {}
if a.vtables:
	for o in a.objs:
		for name, slots in elf_vtables (o).items ():
			vtables.setdefault (name, slots)
	data -= set (vtables)
vt_funcs = { s for slots in vtables.values () for s, _ in slots if s is not None and (s in funcs or s in weakfuncs) }
RUNTIME = { "onyx_lib_init", "onyx_lib_alloc", "onyx_lib_free", "onyx_lib_data", "onyx_lib_table", a.init }
exp = re.compile (a.export)
exc = re.compile (a.exclude) if a.exclude else None
exports = { s for s in funcs if s not in RUNTIME and exp.search (s) and not (exc and exc.search (s)) }
if a.export_file:
	exports &= set (open (a.export_file, encoding = "utf-8").read ().split ())
exports |= vt_funcs				# (what the programs' vtable copies name)
allow = re.compile (a.allow_data) if a.allow_data else None
bad = sorted (s for s in data if not (allow and allow.search (s)) and s not in RUNTIME)
if bad:
	die ("global data a program cannot import (make it static, reach it through a function, or see "
	     "--allow-data): " + " ".join (bad[:20]) + (" ..." if len (bad) > 20 else ""))

# ---- the list: append-only ---------------------------------------------------------------------------------
entries = []
if os.path.exists (a.abi):
	for n, line in enumerate (open (a.abi, encoding = "utf-8")):
		line = line.split ("#")[0].strip ()
		if not line:
			continue
		f = line.split ()
		if len (f) != 2 or not f[0].isdigit ():
			die ("%s:%d: not \"<slot> <symbol>\"" % (a.abi, n + 1))
		if int (f[0]) != len (entries):
			die ("%s:%d: slot %s where %d is expected -- the list is append-only: a line was moved "
			     "or removed" % (a.abi, n + 1, f[0], len (entries)))
		entries.append (f[1])
if len (set (entries)) != len (entries):
	die (a.abi + ": an entry is listed twice")
gone = [s for s in entries if s not in funcs and s not in weakfuncs]
if gone:
	die ("the library no longer defines these entries of %s (an entry is never removed: keep the "
	     "function, even as a stub): %s" % (a.abi, " ".join (gone[:20])))
new = sorted (exports - set (entries))
if new:
	if a.frozen:
		die ("functions not in %s (--frozen): %s" % (a.abi, " ".join (new[:20])))
	first = not entries
	with open (a.abi, "a", encoding = "utf-8", newline = "\n") as f:
		if first:
			f.write ("# %s.abi -- the export table of SD:/lib/%s.so: \"<slot> <symbol>\", in the table's order.\n"
				 "# APPEND-ONLY (tools/libgen/libgen.py adds the new functions at the end): never move or remove a\n"
				 "# line -- programs built against an older table call the entries by their slot. The one edit allowed:\n"
				 "# a function RENAMED keeps its line, with its new symbol (a reserved virtual given a meaning).\n"
				 "# The table's version is its number of entries.\n" % (a.name, a.name))
		for s in new:
			f.write ("%d %s\n" % (len (entries), s))
			entries.append (s)
	print ("libgen: %s: %d new entr%s appended to %s (version %d now)"
	       % (a.name, len (new), "y" if len (new) == 1 else "ies", a.abi, len (entries)))
version = len (entries)
HEADER = 16					# unsigned version, size; init

def write (path, text):
	if path is None:
		return
	old = open (path, encoding = "utf-8").read () if os.path.exists (path) else None
	if old != text:				# (unchanged: left alone, so that make does not rebuild)
		with open (path, "w", encoding = "utf-8", newline = "\n") as f:
			f.write (text)

GEN = "GENERATED by tools/libgen/libgen.py from %s -- do not edit" % os.path.basename (a.abi)

# ---- the library side: the table ------------------------------------------------------------------------
t = ["/* %s_table.S -- %s. The export table of %s.so (version %d). */" % (a.name, GEN, a.name, version),
     "\t.section .data.rel.ro.onyx_lib_table, \"aw\", %progbits",
     "\t.balign 8",
     "\t.globl onyx_lib_table",
     "\t.type onyx_lib_table, %object",
     "onyx_lib_table:",
     "\t.word %d\t\t/* version */" % version,
     "\t.word %d\t\t/* size */" % (HEADER + 8 * version),
     "\t.quad %s" % a.init]
t += ["\t.quad %s" % s for s in entries]
t += ["\t.size onyx_lib_table, . - onyx_lib_table", ""]
write (a.table, "\n".join (t))

# ---- the program side: the stubs ---------------------------------------------------------------------------
var = "onyx_%s_table" % a.name
s = ["/* %s_stubs.S -- %s. The import stubs of %s.so: each one jumps through its entry of the" % (a.name, GEN, a.name),
     " * library's export table (%s, set by %s_bind before the program's constructors). x16 / x17 are" % (var, a.name),
     " * the procedure-call scratch registers: a call may always change them. */"]
for i, sym in enumerate (entries):
	off = HEADER + 8 * i
	s += ["\t.section .text.%s, \"ax\", %%progbits" % sym,
	      "\t.weak %s" % sym,
	      "\t.type %s, %%function" % sym,
	      "\t.balign 4",
	      "%s:" % sym]
	if a.fixed_table is not None:			# entry i at a fixed address: no variable, no bind, valid from the first instruction
		va = int (a.fixed_table, 0)
		if 8 * i > 32760: die ("--fixed-table: too many entries for one ldr")
		s += ["\tmovz x16, #0x%x" % (va & 0xFFFF), "\tmovk x16, #0x%x, lsl #16" % ((va >> 16) & 0xFFFF),
		      "\tmovk x16, #0x%x, lsl #32" % ((va >> 32) & 0xFFFF), "\tldr x16, [x16, #%d]" % (8 * i),
		      "\tbr x16", "\t.size %s, . - %s" % (sym, sym)]
		continue
	s += ["\tadrp x16, %s" % var,
	      "\tldr x16, [x16, :lo12:%s]" % var]
	if off <= 32760:
		s += ["\tldr x16, [x16, #%d]" % off]
	else:
		s += ["\tmov x17, #%d" % off, "\tldr x16, [x16, x17]"]
	s += ["\tbr x16", "\t.size %s, . - %s" % (sym, sym)]
for name in sorted (vtables):			# the vtables' copies: the same slots, through the stubs
	s += ["\t.section .data.rel.ro.%s, \"aw\", %%progbits" % name,
	      "\t.weak %s" % name,
	      "\t.type %s, %%object" % name,
	      "\t.balign 8",
	      "%s:" % name]
	for sym, v in vtables[name]:
		s.append ("\t.quad %s" % (v if sym is None else sym if v == 0 else "%s%+d" % (sym, v)))
	s.append ("\t.size %s, . - %s" % (name, name))
s.append ("")
write (a.stubs, "\n".join (s))

# ---- the program side: the bind constructor ----------------------------------------------------------------
b = """// %(name)s_bind.cpp -- %(gen)s.
// Opens SD:/lib/%(name)s.so before the program's own constructors (priority 101) and sets the pointer
// the import stubs go through. The program was built against version %(version)d of the table: an older
// library is refused (user/Runtime/lib.h lib_bind says so and ends the program).
#include "lib.h"

extern "C" { const void *%(var)s; }
%(decl)s
__attribute__ ((constructor (101))) static void %(name)s_bind (void)
{
	if (%(var)s == 0) %(var)s = lib_bind ("%(name)s", %(version)d, %(imports)s);
}
""" % { "name": a.name, "gen": GEN, "version": version, "var": var,
	"decl": "" if a.data is None else
		"extern \"C\" { extern void *const %s[]; extern const unsigned %s_count; }\n" % (a.data, a.data),
	"imports": "lib_cxx_imports ()" if a.data is None else "lib_cxx_imports_data (%s, %s_count)" % (a.data, a.data) }
if a.bind: write (a.bind, b)
