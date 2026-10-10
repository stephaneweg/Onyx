#!/usr/bin/env python3
# Copyright (c) 2026 the Onyx authors -- MIT licence (see docs/LICENSING.md).
"""run_test.py -- PocketTerm Simulator against fake_rdpd.py (no Pi): the program's own --selftest presses
every control of the face, the chords, the Fn layer, the sticky modifiers, the held keys and the PC's keys,
and this script checks what the server received for each against what the default mapping says -- written
here independently of the program's tables. Also: the picture received is the one sent, pixel for pixel;
a screen that is not 640 x 480 is received at its size; the mapping file shipped is the default.

    python pc/PocketTermSim/test/run_test.py [path of PocketTermSim.exe]     (Windows; needs Pillow)
"""
import os, socket, subprocess, sys
from PIL import Image, ImageChops

HERE = os.path.dirname (os.path.abspath (__file__))
ROOT = os.path.abspath (os.path.join (HERE, "..", "..", ".."))
EXE = sys.argv[1] if len (sys.argv) > 1 else os.path.join (ROOT, "pc", "dist", "PocketTermSim.exe")
PIC = os.path.join (ROOT, "docs", "compact-shell", "mockups", "pocketterm", "noir-home.png")
OUT = os.path.join (HERE, "out"); os.makedirs (OUT, exist_ok = True)
MAPPING = os.path.join (OUT, "test.buttons.ini")
fails = 0
def check (ok, what, detail = ""):
	global fails
	if not ok: fails += 1
	if not ok or os.environ.get ("VERBOSE"): print (("ok   " if ok else "FAIL ") + what + (("   " + detail) if detail and not ok else ""))

def free_port ():
	s = socket.socket (); s.bind (("127.0.0.1", 0)); p = s.getsockname ()[1]; s.close (); return p

def session (size = ""):
	"""The self-test against a fake rdpd -> its exit code, the server's log."""
	port = free_port (); log = os.path.join (OUT, "server.log")
	srv = subprocess.Popen ([sys.executable, os.path.join (HERE, "fake_rdpd.py"), "--port", str (port), "--log", log, "--once"] + (["--size", size] if size else []), stderr = subprocess.PIPE)
	srv.stderr.readline ()						# (listening)
	if os.path.exists (MAPPING): os.remove (MAPPING)		# a fresh default mapping
	rc = subprocess.call ([EXE, "--mapping", MAPPING, "--selftest", "127.0.0.1:%d" % port, "--out", OUT], timeout = 120)
	try: srv.wait (10)
	except subprocess.TimeoutExpired: srv.kill ()
	return rc, open (log).read ().splitlines ()

# ---- what the default mapping says ---------------------------------------------------------------------------
UP, DOWN, LEFT, RIGHT, ENTER, ESC, TAB, BKSP, DEL = 0xFF52, 0xFF54, 0xFF51, 0xFF53, 0xFF0D, 0xFF1B, 0xFF09, 0xFF08, 0xFFFF
HOME, END, PGUP, PGDN = 0xFF50, 0xFF57, 0xFF55, 0xFF56
SHL, SHR, CTRL, ALTL, ALTR, SUPER = 0xFFE1, 0xFFE2, 0xFFE3, 0xFFE9, 0xFFEA, 0xFFEB
def F (n): return 0xFFBE + n - 1
def tap (sym, *mods): return [(1, m) for m in mods] + [(1, sym), (0, sym)] + [(0, m) for m in reversed (mods)]

PAD = [("Up", tap (UP)), ("Down", tap (DOWN)), ("Left", tap (LEFT)), ("Right", tap (RIGHT)), ("A", tap (ENTER)), ("B", tap (ESC)), ("X", []),
       ("Y", tap (F (10), SHL)), ("L", tap (PGUP, CTRL)), ("R", tap (PGDN, CTRL)), ("Select", tap (F (10))), ("Start", tap (SUPER))]
ROWS = ["` 1 2 3 4 5 6 7 8 9 0 - = Backspace", "Tab q w e r t y u i o p [ ] \\", "Caps a s d f g h j k l ; ' Enter",
	"ShiftL z x c v b n m , . / Up ShiftR", "FnL Ctrl AltL Space AltR CMD FnR Left Down Right"]
SHIFTED = dict (zip ("`1234567890-=[]\\;',./", "~!@#$%^&*()_+{}|:\"<>?"))
SPECIAL = {"Backspace": BKSP, "Tab": TAB, "Enter": ENTER, "Up": UP, "Left": LEFT, "Down": DOWN, "Right": RIGHT, "Space": 0x20}
MODS = {"ShiftL": SHL, "ShiftR": SHR, "Ctrl": CTRL, "AltL": ALTL, "AltR": ALTR, "CMD": SUPER, "Caps": 0, "FnL": 0, "FnR": 0}
FN = {"`": ESC, "-": F (11), "=": F (12), "Backspace": DEL, "Up": PGUP, "Down": PGDN, "Left": HOME, "Right": END}
FN.update ({str (n): F (n) for n in range (1, 10)}); FN["0"] = F (10)
KEYS = [k for row in ROWS for k in row.split (" ")]
assert len (KEYS) == 64

def base (k): return SPECIAL.get (k, ord (k[0]))
def shifted (k): return SPECIAL[k] if k in SPECIAL else ord (SHIFTED[k]) if k in SHIFTED else ord (k.upper ())

expect = {}
n = 100
for name, ev in PAD: expect[n] = (name, ev); n += 1
for k in KEYS:
	if k in MODS: expect[n] = ("key " + k, [(1, MODS[k]), (0, MODS[k])] if MODS[k] else [])	# armed, locked, off: down then up
	else: expect[n] = ("key " + k, tap (base (k)))
	n += 1
n = 300
for k in KEYS:
	if k not in MODS: expect[n] = ("Fn + " + k, tap (FN.get (k, base (k)))); n += 1
expect[500] = ("Shift locked", [(1, SHL)]); n = 501
for k in KEYS:
	if k not in MODS: expect[n] = ("Shift + " + k, tap (shifted (k))); n += 1
expect[599] = ("Shift unlocked", [(0, SHL)])
A, C, V, X1 = ord ("a"), ord ("c"), ord ("v"), ord ("x")
expect.update ({
	1000: ("Ctrl then Right", tap (RIGHT, CTRL)),
	1001: ("Alt then Tab", tap (TAB, ALTL)),
	1002: ("Fn then 1, then 1", tap (F (1)) + tap (ord ("1"))),
	1003: ("right Shift then a, then a", tap (ord ("A"), SHR) + tap (A)),
	1004: ("Caps: a, 1, Shift + a", tap (ord ("A")) + tap (ord ("1")) + tap (A, SHL)),
	1005: ("Ctrl locked: c, v", [(1, CTRL)] + tap (C) + tap (V) + [(0, CTRL)]),
	1006: ("CMD tapped alone", tap (SUPER)),
	1007: ("Ctrl then the D-pad's Up", tap (UP, CTRL)),
	1008: ("Ctrl, Alt, Fn, Backspace = Ctrl+Alt+Del", [(1, CTRL), (1, ALTL), (1, DEL), (0, DEL), (0, CTRL), (0, ALTL)]),
	1009: ("CMD then Left", tap (LEFT, SUPER)),
	1200: ("PC F7 held = L", [(1, CTRL), (1, PGUP), (1, PGUP), (0, PGUP), (0, CTRL)]),
	1201: ("PC F12 = Start", tap (SUPER)),
	1202: ("PC Left", tap (LEFT)),
	1203: ("PC a", [(3, A), ("CHAR", A), (2, A)]),
	1204: ("PC e acute", [("CHAR", 0xE9)]),
	1205: ("PC Ctrl+C", [(1, CTRL), (3, C), ("CHAR", 3), (2, C), (0, CTRL)]),
	1206: ("PC F10", tap (F (10))),
	1207: ("PC Enter", tap (ENTER)),
	1300: ("the mouse on the screen at 100, 50", [("PTR", 0xFFFFFFFF, 100, 50, 0, 0)]),
})
REPEATS = {1100: ("x held", [], X1), 1101: ("the D-pad's Down held", [], DOWN), 1102: ("L held", [CTRL], PGUP)}

def groups (lines):
	g, cur = {}, None
	for l in lines:
		p = l.split ()
		if p[0] == "RAISE": cur = int (p[1]); g[cur] = []
		elif cur is None or p[0] in ("READY", "PONG", "BYE"): continue
		elif p[0] == "KEY": g[cur].append ((int (p[1]), int (p[2], 16)))
		elif p[0] == "CHAR": g[cur].append (("CHAR", int (p[1], 16)))
		elif p[0] == "PTR": g[cur].append (("PTR", int (p[1], 16)) + tuple (int (v) for v in p[2:]))
		else: g[cur].append (tuple (p))
	return g
def show (ev): return " ".join ("%s:%s" % (e[0], "0x%X" % e[1] if isinstance (e[1], int) else e[1]) for e in ev) or "(nothing)"

# ---- 1: every control ------------------------------------------------------------------------------------------
rc, lines = session ()
err = os.path.join (OUT, "selftest-error.txt")
check (rc == 0, "the self-test ran (exit %d)" % rc, open (err).read () if rc and os.path.exists (err) else "")
check (bool (lines) and lines[0] == "HELLO options 12", "the hello: 32 bits, Onyx frames, pipelined, COPY", lines[0] if lines else "")
check ("DESKTOP 1" in lines[:3], "the desktop asked")
g = groups (lines)
check (9999 in g, "the log is whole")
for n in sorted (expect):
	name, ev = expect[n]
	got = g.get (n)
	check (got == ev, "%d %s -> %s" % (n, name, show (ev)), "got " + (show (got) if got is not None else "no mark"))
for n, (name, mods, sym) in REPEATS.items ():
	got = g.get (n, [])
	head, tail = [(1, m) for m in mods], [(0, sym)] + [(0, m) for m in reversed (mods)]
	mid = got[len (head):len (got) - len (tail)]
	check (got[:len (head)] == head and got[-len (tail):] == tail and len (mid) >= 3 and all (e == (1, sym) for e in mid), "%d %s: %d downs then one up" % (n, name, len (mid)), "got " + show (got))
check (len (g) == len (expect) + len (REPEATS) + 1, "no step missing or unknown (%d marks)" % len (g))
order = [l.split (" ", 1)[1] for l in open (os.path.join (OUT, "order.txt")).read ().splitlines ()]
check (order == [p[0] for p in PAD] + ["key:" + k for k in KEYS], "the face's controls: the 12 pad buttons and the 64 keys, in order")
src = Image.open (PIC).convert ("RGB")
check (ImageChops.difference (Image.open (os.path.join (OUT, "remote.png")).convert ("RGB"), src).getbbox () is None, "the screen received is the picture sent, pixel for pixel")
# the device's picture holds the middle of the screen untouched (1:1, not resampled)
dev = Image.open (os.path.join (OUT, "device-connected.png")).convert ("RGB")
want = src.crop ((220, 190, 420, 290)).tobytes ()
found = any (dev.crop ((x, y, x + 200, y + 100)).tobytes () == want for x in range (200, 500) for y in range (150, 420))
check (found, "the face shows the screen pixel for pixel at 1x")

# ---- 2: a screen of another size -------------------------------------------------------------------------------
rc, lines = session (size = "1024x768")
check (rc == 0 and Image.open (os.path.join (OUT, "remote.png")).size == (1024, 768), "a 1024 x 768 screen is received at its size")
check (groups (lines).get (1300) == [("PTR", 0xFFFFFFFF, 160, 80, 0, 0)], "its pointer is scaled: 100, 50 in the glass = 160, 80 on the Pi", show (groups (lines).get (1300, [])))

# ---- 3: the files ----------------------------------------------------------------------------------------------
shipped = os.path.join (os.path.dirname (EXE), "PocketTermSim.buttons.ini")
check (os.path.exists (shipped) and open (shipped, "rb").read () == open (MAPPING, "rb").read (), "the mapping file shipped is the default one")
subprocess.call ([EXE, "--mapping", MAPPING, "--sends", os.path.join (OUT, "sends.txt")])
check ("Ctrl+PageUp" in open (os.path.join (OUT, "sends.txt")).read (), "the table of what each control sends")

print ("%d failure%s" % (fails, "" if fails == 1 else "s") if fails else "all passed (%d steps checked)" % (len (expect) + len (REPEATS)))
sys.exit (1 if fails else 0)
