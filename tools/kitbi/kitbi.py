#!/usr/bin/env python3
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
#
# kitbi.py -- a kit's description for Onyx BASIC (SD:/lib/<kit>.bi), made from its export table and
# its headers.
#
# A BASIC program says `#import filekit` and calls the kit's functions by their name
# (`FileKit.copy (a$, b$, 0, 0)`). A shared library only has places in a table (<kit>.abi: place and
# symbol); what BASIC needs besides is each function's name and types. This tool reads them from the
# kit's headers -- the C prototypes -- and writes one line per function BASIC can call:
#
#     <name> <place> <result> <arguments or -> <C name>
#
# The types are one letter each (user/Libs/basic/basint.h): the result v i u l b c h w f d s, an
# argument i p c s f d I L F D. <name> is the C name less the kit's prefix (fk_copy -> copy) when
# nearly all its functions share one; BASIC takes both. Left out: what BASIC cannot call -- C++
# classes and overloads, a struct passed or returned by value, a reference, a variable number of
# arguments, more than 8 whole-number or 8 floating-point arguments.
#
#     python3 tools/kitbi/kitbi.py --out-dir user/lib            every kit of user/Kits
#     python3 tools/kitbi/kitbi.py --out-dir user/lib filekit    these kits
#     ... -v                                                      says what was left out, and why
#
# A new kit needs nothing here: its folder user/Kits/<kit>/ with <kit>.abi and its headers is enough.
import argparse, glob, os, re, sys

ROOT = os.path.normpath (os.path.join (os.path.dirname (os.path.abspath (__file__)), "..", ".."))
KITS = os.path.join (ROOT, "user", "Kits")

INT32 = { "int": "i", "signed": "i", "signed int": "i", "unsigned": "u", "unsigned int": "u", "long int": "l",
	  "short": "h", "short int": "h", "unsigned short": "w", "unsigned short int": "w",
	  "char": "b", "unsigned char": "b", "signed char": "c", "bool": "b", "_Bool": "b",
	  "long": "l", "unsigned long": "l", "long long": "l", "unsigned long long": "l", "long long int": "l",
	  "unsigned long long int": "l", "unsigned long int": "l",
	  "size_t": "l", "ssize_t": "l", "uintptr_t": "l", "intptr_t": "l", "ptrdiff_t": "l", "lib_size_t": "l",
	  "u8": "b", "u16": "w", "u32": "u", "u64": "l", "s8": "c", "s16": "h", "s32": "i", "s64": "l",
	  "uint8_t": "b", "uint16_t": "w", "uint32_t": "u", "uint64_t": "l", "int8_t": "c", "int16_t": "h",
	  "int32_t": "i", "int64_t": "l", "boolean": "i", "float": "f", "double": "d", "void": "v",
	  "FT_Error": "i", "FT_Int": "i", "FT_UInt": "u", "FT_Long": "l", "FT_ULong": "l", "FT_Int32": "i",
	  "FT_UInt32": "u", "FT_Short": "h", "FT_UShort": "w", "FT_Pos": "l", "FT_Fixed": "l", "FT_F26Dot6": "l",
	  "FT_Bool": "b", "FT_Byte": "b", "FT_Char": "c" }
QUALS = re.compile (r"\b(const|volatile|restrict|__restrict|extern|static|inline|struct|enum|union|noexcept|constexpr)\b")

def clean (text):
	"""a header without its comments, its strings' contents and its preprocessor lines"""
	text = re.sub (r"/\*.*?\*/", " ", text, flags = re.S)
	out = []
	cont = False
	for l in text.split ("\n"):
		l = re.sub (r'"(\\.|[^"\\])*"', '""', l)
		l = re.sub (r"//.*$", "", l)
		pp = cont or l.lstrip ().startswith ("#")
		cont = pp and l.rstrip ().endswith ("\\")
		out.append ("" if pp else l)
	return "\n".join (out)

def typedefs (text):
	"""the headers' own type names: name -> ('int', letter) | ('fn',) | ('struct',) | ('ptr',)"""
	t = {}
	for m in re.finditer (r"\btypedef\b([^;{}]*?)\(\s*\*\s*(\w+)\s*\)\s*\([^;{}]*\)\s*;", text):
		t[m.group (2)] = ("fn",)
	for m in re.finditer (r"\btypedef\s+(?:struct|union|class)\s+\w*\s*(\**)\s*(\w+)\s*;", text):
		t[m.group (2)] = ("ptr",) if m.group (1) else ("struct",)
	for m in re.finditer (r"\btypedef\s+(?:struct|union)\s*\w*\s*\{", text):	# typedef struct { ... } name;
		d = 1; i = m.end ()
		while i < len (text) and d: d += { "{": 1, "}": -1 }.get (text[i], 0); i += 1
		n = re.match (r"\s*(\**)\s*(\w+)\s*;", text[i:])
		if n: t[n.group (2)] = ("ptr",) if n.group (1) else ("struct",)
	for m in re.finditer (r"\btypedef\s+enum\b[^;{}]*(?:\{[^{}]*\})?\s*(\w+)\s*;", text):
		t[m.group (1)] = ("int", "i")
	for m in re.finditer (r"\btypedef\s+([\w\s]+?)\s*(\**)\s*\b(\w+)\s*;", text):
		base = " ".join (QUALS.sub (" ", m.group (1)).split ())
		if m.group (3) in t: continue
		if m.group (2): t[m.group (3)] = ("ptr",)
		elif base in INT32 and INT32[base] != "v": t[m.group (3)] = ("int", INT32[base])
	for m in re.finditer (r"\b(struct|union|class)\s+(\w+)\s*[{;:]", text): t.setdefault (m.group (2), ("struct",))
	for m in re.finditer (r"\benum\s+(?:class\s+)?(\w+)\s*[{;:]", text): t.setdefault (m.group (1), ("int", "i"))
	return t

def split_args (s):
	out = []; d = 0; cur = ""
	for ch in s:
		if ch in "(<[": d += 1
		elif ch in ")>]": d -= 1
		if ch == "," and d == 0: out.append (cur); cur = ""
		else: cur += ch
	if cur.strip (): out.append (cur)
	return out

class Skip (Exception): pass

def base_of (decl, tdefs, named):
	"""a declaration ("const char *name", "unsigned n = 0") -> (letter of its base type or a tag, stars, const?)"""
	decl = decl.split ("=")[0].strip ()
	if "&" in decl: raise Skip ("a reference")
	if "..." in decl: raise Skip ("a variable number of arguments")
	if re.search (r"\(\s*\*", decl): return ("fn", 0, False)
	arr = decl.count ("[")
	decl = re.sub (r"\[[^\]]*\]", "", decl)
	stars = decl.count ("*") + (1 if arr else 0)
	const = bool (re.search (r"\bconst\b", decl.split ("*")[0]))
	words = QUALS.sub (" ", decl.replace ("*", " ")).split ()
	if not words: raise Skip ("no type")
	# the parameter's own name: the last word, unless the type is all there is
	def known (ws): return " ".join (ws) in INT32 or (len (ws) == 1 and ws[0] in tdefs)
	if named and len (words) > 1 and not known (words): words = words[:-1]
	if not known (words):
		if stars: return ("x", stars, const)		# a pointer to something of the kit's: a handle
		raise Skip ("the type '%s'" % " ".join (words))
	w = " ".join (words)
	if w in INT32: return (INT32[w], stars, const)
	k = tdefs[w]
	if k[0] == "int": return (k[1], stars, const)
	if k[0] == "fn": return ("fn", stars, const)
	if k[0] == "ptr": return ("x", stars + 1, const)
	return ("struct", stars, const)

def ret_letter (decl, tdefs):
	b, stars, const = base_of (decl, tdefs, False)
	if b == "fn": raise Skip ("it returns a function")
	if stars: return "s" if (b in "bc" and stars == 1 and const) else "l"
	if b in ("struct", "x"): raise Skip ("it returns a struct")
	return b

def arg_letter (decl, tdefs):
	b, stars, const = base_of (decl, tdefs, True)
	if b == "fn": return "c"
	if not stars:
		if b in ("struct", "x"): raise Skip ("a struct by value")
		if b == "v": raise Skip ("a void argument")
		return b if b in "fd" else "i"
	if stars == 1 and b in "bc": return "s" if const else "p"
	if stars == 1 and not const:
		if b in "iu": return "I"
		if b == "l": return "L"
		if b == "f": return "F"
		if b == "d": return "D"
	if stars == 2 and not const: return "L"		# void **out, char **text: a pointer's address
	return "p"

def prototypes (text, name):
	"""every declaration of `name` in the cleaned text: (what is before the name, its arguments)"""
	found = []
	for m in re.finditer (r"(?<![\w.>:~])" + re.escape (name) + r"\s*\(", text):
		i = m.end (); d = 1
		while i < len (text) and d: d += { "(": 1, ")": -1 }.get (text[i], 0); i += 1
		if d: continue
		after = re.match (r"\s*(const\s*)?(noexcept\s*)?([;{])", text[i:])
		if not after: continue
		j = m.start ()
		while j > 0 and text[j - 1] not in ";{}": j -= 1
		before = text[j:m.start ()].strip ()
		before = re.sub (r"\b[A-Z][A-Z0-9]*_(API|FN|C|EXPORT)\b", " ", before)
		before = re.sub (r"\b(FT_EXPORT|FT_BASE)\s*\(\s*([^)]*)\)", r"\2", before)
		before = re.sub (r'extern\s*""', " ", before).strip ()
		if not before or re.search (r"[=(),.]|\b(return|else|case|typedef|new|delete|if|while)\b", before): continue
		found.append ((before, text[m.end ():i - 1].strip ()))
	return found

def describe (kit, verbose):
	folder = os.path.join (KITS, kit)
	abi = os.path.join (folder, kit + ".abi")
	if not os.path.isfile (abi): return None
	entries = []
	for l in open (abi, encoding = "utf-8"):
		f = l.split ()
		if len (f) == 2 and f[0].isdigit (): entries.append ((int (f[0]), f[1]))
	text = ""
	for h in sorted (glob.glob (os.path.join (folder, "*.h"))) + sorted (glob.glob (os.path.join (folder, "*.hpp"))):
		text += clean (open (h, encoding = "utf-8", errors = "replace").read ()) + "\n"
	tdefs = typedefs (text)
	# the kit's prefix: what nearly all its C names start with (fk_, ik_, kapi_)
	cnames = [s for _, s in entries if re.fullmatch (r"[A-Za-z]\w*", s)]
	count = {}
	for s in cnames:
		m = re.match (r"[A-Za-z0-9]+_", s)
		if m: count[m.group (0)] = count.get (m.group (0), 0) + 1
	prefix = ""
	if count:
		best = max (count, key = count.get)
		if count[best] * 10 >= len (cnames) * 8: prefix = best
	lines = []; left = []; taken = set ()
	for slot, sym in entries:
		name = sym
		m = re.fullmatch (r"_Z(\d+)(\w+)", sym)			# a C++ function outside any namespace
		if m and len (m.group (2)) >= int (m.group (1)): name = m.group (2)[:int (m.group (1))]
		elif sym.startswith ("_Z"): left.append ((sym, "C++ (a class or a namespace)")); continue
		if name.endswith ("_") or "__" in name: left.append ((sym, "internal")); continue
		protos = { (re.sub (r"\s+", " ", b), re.sub (r"\s+", " ", a)) for b, a in prototypes (text, name) }
		if not protos: left.append ((sym, "no prototype in the kit's headers")); continue
		if len (protos) > 1 and len ({ len (split_args (a)) for _, a in protos }) > 1: left.append ((sym, "overloaded")); continue
		before, args = sorted (protos)[0]
		try:
			ret = ret_letter (before, tdefs)
			al = ""
			if args and args != "void":
				for a in split_args (args): al += arg_letter (a, tdefs)
			if sum (1 for c in al if c not in "fd") > 8 or sum (1 for c in al if c in "fd") > 8 or len (al) > 16:
				raise Skip ("too many arguments")
		except Skip as e:
			left.append ((sym, str (e))); continue
		short = name[len (prefix):] if prefix and name.startswith (prefix) and len (name) > len (prefix) else name
		if short.lower () in taken: short = name
		taken.add (short.lower ())
		lines.append ("%s %d %s %s %s" % (short, slot, ret, al or "-", name))
	out = ["# %s.bi -- %s for Onyx BASIC (#import %s): made by tools/kitbi/kitbi.py from %s.abi and the kit's headers;" % (kit, kit, kit, kit),
	       "# not edited by hand. <name> <place> <result> <arguments or -> <C name>; the types: user/Libs/basic/basint.h.",
	       "kit %s %d" % (kit, len (entries))] + lines
	if verbose:
		for sym, why in left: print ("  %s: %s left out (%s)" % (kit, sym, why))
	return "\n".join (out) + "\n", len (lines), len (entries)

ap = argparse.ArgumentParser ()
ap.add_argument ("--out-dir", required = True)
ap.add_argument ("-v", "--verbose", action = "store_true")
ap.add_argument ("kits", nargs = "*")
a = ap.parse_args ()
kits = a.kits or sorted (d for d in os.listdir (KITS) if os.path.isfile (os.path.join (KITS, d, d + ".abi")))
os.makedirs (a.out_dir, exist_ok = True)
for kit in kits:
	r = describe (kit, a.verbose)
	if r is None: sys.exit ("kitbi: no user/Kits/%s/%s.abi" % (kit, kit))
	text, n, total = r
	path = os.path.join (a.out_dir, kit + ".bi")
	old = open (path, encoding = "utf-8", newline = "").read () if os.path.isfile (path) else None
	if old != text: open (path, "w", encoding = "utf-8", newline = "\n").write (text)
	print ("kitbi: %s.bi -- %d of the %d entries" % (kit, n, total))
