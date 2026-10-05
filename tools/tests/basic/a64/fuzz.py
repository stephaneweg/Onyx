#!/usr/bin/env python3
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
"""fuzz.py -- Onyx BASIC's machine code against its VM: random programs (numbers of every type, arrays
with indices that sometimes fall outside, loops, IF, SELECT, GOSUB, SUBs and FUNCTIONs with arguments
by reference, divisions by zero and overflows under ON ERROR ... RESUME NEXT) run twice by the AArch64
host under qemu-aarch64 -- in machine code, then with MANAGED=1 -- must print the same thing.

    python3 tools/tests/basic/a64/fuzz.py <host binary> [programs] [first seed]

A program that differs is left in the current directory as fuzz_<seed>.bas."""
import os, random, subprocess, sys, tempfile

SCALARS = ["a", "b", "c", "i%", "j%", "k%", "l&", "m&", "d#", "e#"]

class Gen:
	def __init__ (self, seed):
		self.r = random.Random (seed)
		self.lines = []
		self.ind = 1
		self.label = 0
		self.loopv = 0
		self.infunc = False
	def emit (self, s): self.lines.append ("  " * self.ind + s)
	def const (self):
		r = self.r
		return r.choice ([str (r.randint (-9, 9)), str (r.randint (-300, 300)), "%.2f" % r.uniform (-50, 50), "0", "1", "2",
				  str (r.randint (-40000, 40000)), "0.5", "1E+10", "3.7#"])
	def var (self):
		r = self.r
		k = r.random ()
		if k < 0.6: return r.choice (SCALARS)
		if k < 0.8: return "x(%s)" % self.index (11)
		if k < 0.9: return "g%%(%s, %s)" % (self.index (5), self.index (4))
		return "w#(%s)" % self.index (7)
	def index (self, n):
		r = self.r
		k = r.random ()
		if k < 0.5: return str (r.randint (0, n - 1))
		if k < 0.9: return "ABS(%s) MOD %d" % (r.choice (["i%", "j%", "k%"]), n if r.random () < 0.9 else n + 2)
		return r.choice (["i%", "k% - 1", "l&"])
	def expr (self, depth = 0):
		r = self.r
		if depth > 3 or r.random () < 0.3:
			return self.const () if r.random () < 0.4 else self.var ()
		k = r.random ()
		a, b = self.expr (depth + 1), self.expr (depth + 1)
		if k < 0.45: return "(%s %s %s)" % (a, r.choice (["+", "-", "*", "+", "-", "*", "/"]), b)
		if k < 0.55: return "(%s %s %s)" % (a, r.choice (["\\", "MOD"]), b)
		if k < 0.70: return "(%s %s %s)" % (a, r.choice (["=", "<>", "<", ">", "<=", ">="]), b)
		if k < 0.78: return "(%s %s %s)" % (a, r.choice (["AND", "OR", "XOR", "EQV", "IMP"]), b)
		if k < 0.82: return "(NOT %s)" % a
		if k < 0.86: return "(-%s)" % a
		if k < 0.94: return self.call1 (a)
		if not self.infunc and r.random () < 0.6: return "F1(%s, %s)" % (a, b)
		return "F2%%(%s)" % a
	def call1 (self, a):
		f = self.r.choice (["ABS", "INT", "SGN", "FIX", "SQR", "LEN", "MIN", "MAX"])
		if f == "LEN": return "LEN(STR$(%s))" % a
		if f in ("MIN", "MAX"): return "%s(%s, %s)" % (f, a, self.const ())
		return "%s(%s)" % (f, a)
	def cond (self):
		return "%s %s %s" % (self.expr (2), self.r.choice (["=", "<>", "<", ">", "<=", ">="]), self.expr (2))
	def stmt (self, depth):
		r = self.r
		k = r.random ()
		if k < 0.34: self.emit ("%s = %s" % (self.var (), self.expr ()))
		elif k < 0.46: self.emit ("PRINT %s" % "; ".join (self.expr (1) for _ in range (r.randint (1, 3))))
		elif k < 0.56 and depth < 3:
			self.emit ("IF %s THEN" % self.cond ()); self.block (depth + 1)
			if r.random () < 0.5: self.emit ("ELSE"); self.block (depth + 1)
			self.emit ("END IF")
		elif k < 0.66 and depth < 3:
			self.loopv += 1; v = "q%d%%" % self.loopv
			self.emit ("FOR %s = %d TO %d%s" % (v, r.randint (-2, 3), r.randint (0, 7), r.choice (["", "", " STEP 2", " STEP -1"])))
			self.block (depth + 1); self.emit ("NEXT")
		elif k < 0.72 and depth < 3:
			self.loopv += 1; v = "q%d%%" % self.loopv
			self.emit ("%s = 0" % v)
			self.emit (r.choice (["DO WHILE %s < %d", "WHILE %s < %d"]) % (v, r.randint (1, 6)))
			wh = self.lines[-1].strip ().startswith ("WHILE")
			self.ind += 1; self.emit ("%s = %s + 1" % (v, v)); self.ind -= 1
			self.block (depth + 1); self.emit ("WEND" if wh else "LOOP")
		elif k < 0.78 and depth < 3:
			self.emit ("SELECT CASE %s" % self.expr (2))
			for _ in range (r.randint (1, 3)):
				self.emit (r.choice (["CASE %s" % self.const (), "CASE %d TO %d" % (r.randint (-5, 0), r.randint (1, 9)), "CASE IS > %s" % self.const ()]))
				self.block (depth + 1)
			self.emit ("CASE ELSE"); self.block (depth + 1)
			self.emit ("END SELECT")
		elif k < 0.84 and not self.infunc: self.emit ("GOSUB sub%d" % r.randint (1, 2))
		elif k < 0.90 and not self.infunc: self.emit ("P1 %s, %s, %s" % (r.choice (SCALARS[:3]), r.choice (["i%", "j%"]), self.expr (1)))
		elif k < 0.94: self.emit ("SWAP %s, %s" % (r.choice (["a", "b", "c"]), r.choice (["a", "b", "c"])))
		elif k < 0.97: self.emit ("s$ = s$ + STR$(%s): IF LEN(s$) > 40 THEN s$ = MID$(s$, 20)" % self.expr (2))
		else: self.emit ("%s = %s ^ %s" % (r.choice (["a", "d#"]), self.expr (2), r.choice (["2", "0.5", "3", "-1"])))
	def block (self, depth):
		self.ind += 1
		for _ in range (self.r.randint (1, 4)): self.stmt (depth)
		self.ind -= 1
	def program (self):
		L = self.lines
		L += ["DIM x(10), g%(4, 3), w#(6)", "ON ERROR GOTO oops", "a = 1.5: b = -2: c = 100: i% = 3: j% = -7: k% = 2: l& = 70000: m& = -5: d# = 3.14159265358979: e# = 1E+20",
		      "FOR t% = 0 TO 10: x(t%) = t% * 1.5 - 3: NEXT", "FOR t% = 0 TO 4: FOR u% = 0 TO 3: g%(t%, u%) = t% * 10 + u%: NEXT: NEXT",
		      "FOR t% = 0 TO 6: w#(t%) = 1 / (t% + 1): NEXT"]
		self.ind = 0
		for _ in range (self.r.randint (6, 14)): self.stmt (0)
		L += ["PRINT a; b; c; i%; j%; k%; l&; m&; d#; e#; s$", "FOR t% = 0 TO 10: PRINT x(t%);: NEXT: PRINT", "PRINT g%(1, 1); g%(4, 3); w#(2); errs%", "END"]
		for n in (1, 2):
			L.append ("sub%d:" % n); self.ind = 1
			for _ in range (self.r.randint (1, 4)): self.stmt (2)
			L.append ("RETURN")
		L += ["oops:", "errs% = errs% + 1", "IF errs% < 40 THEN PRINT \"E\"; ERR;", "IF errs% > 400 THEN PRINT \"too many errors\": END", "RESUME NEXT"]
		self.infunc = True
		L.append ("SUB P1 (p, q%, v)"); self.ind = 1
		self.emit ("p = p + v: q% = q% + 1")
		for _ in range (self.r.randint (0, 3)): self.stmt (2)
		L.append ("END SUB")
		L.append ("FUNCTION F1 (p, q)"); self.ind = 1
		self.emit ("IF p > q THEN F1 = p - q ELSE F1 = %s" % self.expr (2))
		L.append ("END FUNCTION")
		L.append ("FUNCTION F2% (p)"); self.ind = 1
		self.emit ("IF ABS(p) < 1000 THEN F2% = p * 2 ELSE F2% = 7")
		for _ in range (self.r.randint (0, 2)): self.stmt (2)
		L.append ("END FUNCTION")
		return "\n".join (L) + "\n"

def run (binary, path, managed):
	env = dict (os.environ)
	if managed: env["MANAGED"] = "1"
	else: env.pop ("MANAGED", None)
	try:
		r = subprocess.run (["qemu-aarch64", binary, path], stdin = subprocess.DEVNULL, capture_output = True, timeout = 60, env = env)
		return r.stdout + r.stderr
	except subprocess.TimeoutExpired:
		return b"TIMEOUT"

def main ():
	if len (sys.argv) < 2: raise SystemExit (__doc__)
	binary = sys.argv[1]; count = int (sys.argv[2]) if len (sys.argv) > 2 else 300; seed0 = int (sys.argv[3]) if len (sys.argv) > 3 else 1
	bad = 0; errors = 0
	with tempfile.TemporaryDirectory () as tmp:
		for seed in range (seed0, seed0 + count):
			src = Gen (seed).program ()
			path = os.path.join (tmp, "f.bas")
			open (path, "w").write (src)
			n = run (binary, path, False); m = run (binary, path, True)
			if b"COMPILE ERROR" in m: errors += 1
			if n != m:
				bad += 1
				open ("fuzz_%d.bas" % seed, "w").write (src)
				print ("DIFF seed %d (fuzz_%d.bas)" % (seed, seed))
				nl, ml = n.decode ("latin-1").splitlines (), m.decode ("latin-1").splitlines ()
				for i in range (max (len (nl), len (ml))):
					x = nl[i] if i < len (nl) else "<end>"; y = ml[i] if i < len (ml) else "<end>"
					if x != y: print ("  line %d\n    native : %s\n    managed: %s" % (i + 1, x[:160], y[:160])); break
				if bad >= 5: break
	print ("fuzz: %d programs, %d differ, %d did not compile" % (count, bad, errors))
	return 1 if bad or errors > count // 2 else 0

sys.exit (main ())
