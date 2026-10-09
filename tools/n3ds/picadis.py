#!/usr/bin/env python3
# MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors
"""picadis.py -- lists a PICA200 vertex shader (the 3DS GPU's machine words) as text, from a trace of the 3DS core:
`N3DS_GPUTRACE=<frame> n3dstest ...` prints, for the first draws of that frame, "shader entry ...", "code ...",
"desc ..." (the operand descriptors) and "uniforms ...".

    python3 tools/n3ds/picadis.py <trace.log> [n]	(the n-th shader of the log, 0 by default; all its code)

To study why a game's shader gives what it gives (n3ds_pica.cpp's interpreter)."""
import sys

NAMES = {0x00: "add", 0x01: "dp3", 0x02: "dp4", 0x03: "dph", 0x04: "dst", 0x05: "ex2", 0x06: "lg2", 0x07: "litp", 0x08: "mul",
	 0x09: "sge", 0x0A: "slt", 0x0B: "flr", 0x0C: "max", 0x0D: "min", 0x0E: "rcp", 0x0F: "rsq", 0x12: "mova", 0x13: "mov",
	 0x18: "dphi", 0x19: "dsti", 0x1A: "sgei", 0x1B: "slti", 0x20: "break", 0x21: "nop", 0x22: "end", 0x23: "breakc",
	 0x24: "call", 0x25: "callc", 0x26: "callu", 0x27: "ifu", 0x28: "ifc", 0x29: "loop", 0x2A: "emit", 0x2B: "setemit",
	 0x2C: "jmpc", 0x2D: "jmpu"}
UNARY = {0x05, 0x06, 0x07, 0x0B, 0x0E, 0x0F, 0x12, 0x13}
CMPS = ["eq", "ne", "lt", "le", "gt", "ge", "?6", "?7"]

def reg (r):
	return "v%d" % r if r < 0x10 else "r%d" % (r - 0x10) if r < 0x20 else "c%d" % (r - 0x20)
def dst (r):
	return "o%d" % r if r < 0x10 else "r%d" % (r - 0x10)
def swz (v):
	return "".join ("xyzw"[v >> (6 - 2 * i) & 3] for i in range (4))
def mask (d):
	return "".join (c for i, c in enumerate ("xyzw") if d & (8 >> i))
def idx (i):
	return ["", "[a0.x]", "[a0.y]", "[aL]"][i]

def src (r, d, shift, index = ""):
	return "%s%s%s.%s" % ("-" if d >> (shift - 1) & 1 else "", reg (r), index, swz (d >> shift & 0xFF))

def line (pc, ins, descs):
	op = ins >> 26
	if op >= 0x30:
		d = descs[ins & 0x1F]
		i = ins >> 22 & 3
		if op >= 0x38: a, b, c = src (ins >> 17 & 0x1F, d, 5), src (ins >> 10 & 0x7F, d, 14, idx (i)), src (ins >> 5 & 0x1F, d, 23)
		else: a, b, c = src (ins >> 17 & 0x1F, d, 5), src (ins >> 12 & 0x1F, d, 14), src (ins >> 5 & 0x7F, d, 23, idx (i))
		return "%-6s %s.%s, %s, %s, %s" % ("mad" if op >= 0x38 else "madi", dst (ins >> 24 & 0x1F), mask (d), a, b, c)
	if op in (0x2E, 0x2F):
		d = descs[ins & 0x7F]
		return "cmp    %s %s/%s %s" % (src (ins >> 12 & 0x7F, d, 5, idx (ins >> 19 & 3)), CMPS[ins >> 24 & 7], CMPS[ins >> 21 & 7], src (ins >> 7 & 0x1F, d, 14))
	name = NAMES.get (op, "?%02x" % op)
	if op in (0x20, 0x21, 0x22, 0x2A): return name
	if 0x23 <= op <= 0x2D and op != 0x2B:
		d, n = ins >> 10 & 0xFFF, ins & 0xFF
		if op in (0x25, 0x28, 0x2C, 0x23):
			cond = ["x==%d || y==%d", "x==%d && y==%d", "x==%d", "y==%d"][ins >> 22 & 3]
			rx, ry = ins >> 25 & 1, ins >> 24 & 1
			cond = cond % ((rx, ry) if cond.count ("%") == 2 else (rx,) if "x" in cond else (ry,))
		elif op in (0x26, 0x27, 0x2D): cond = "b%d" % (ins >> 22 & 15)
		elif op == 0x29: cond = "i%d" % (ins >> 22 & 3)
		else: cond = ""
		if op in (0x27, 0x28): return "%-6s %s -> else at %03x, %d instructions (ends at %03x)" % (name, cond, d, n, d + n)
		if op == 0x29: return "loop   %s to %03x included" % (cond, d)
		if op in (0x24, 0x25, 0x26): return "%-6s %s %03x, %d instructions" % (name, cond, d, n)
		return "%-6s %s %03x%s" % (name, cond, d, " (when false)" if op == 0x2D and n & 1 else "")
	d = descs[ins & 0x7F]
	i = ins >> 19 & 3
	if op in (0x18, 0x19, 0x1A, 0x1B): a, b = src (ins >> 14 & 0x1F, d, 5), src (ins >> 7 & 0x7F, d, 14, idx (i))
	else: a, b = src (ins >> 12 & 0x7F, d, 5, idx (i)), src (ins >> 7 & 0x1F, d, 14)
	if op == 0x12: return "mova   a0.%s, %s" % (mask (d)[:2], a)
	if op in UNARY: return "%-6s %s.%s, %s" % (name, dst (ins >> 21 & 0x1F), mask (d), a)
	return "%-6s %s.%s, %s, %s" % (name, dst (ins >> 21 & 0x1F), mask (d), a, b)

def main ():
	if len (sys.argv) < 2: sys.exit (__doc__)
	want = int (sys.argv[2]) if len (sys.argv) > 2 else 0
	shaders, cur = [], None
	for l in open (sys.argv[1], errors = "replace"):
		if l.startswith ("shader entry"): cur = {"head": l.strip ()}; shaders.append (cur)
		elif cur is not None and l.startswith ("code "): cur["code"] = [int (x, 16) for x in l.split ()[1:]]
		elif cur is not None and l.startswith ("desc "): cur["desc"] = [int (x, 16) for x in l.split ()[1:]]
		elif cur is not None and l.startswith ("uniforms "): cur["uniforms"] = l.strip ()
	if want >= len (shaders): sys.exit ("%d shaders in this log" % len (shaders))
	s = shaders[want]
	print (s["head"])
	descs = s["desc"] + [0] * 128
	for pc, ins in enumerate (s["code"]): print ("%03x: %08x  %s" % (pc, ins, line (pc, ins, descs)))
	print (s.get ("uniforms", ""))

if __name__ == "__main__":
	main ()
