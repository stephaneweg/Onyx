#!/usr/bin/env python3
"""mockup_studio.py -- the first mock-ups of Studio, Onyx's IDE for desktop apps in BASIC (in the way of Visual
Studio's WPF designer and of Visual Basic): a form designer whose layout is a light text format (.form: a control
a line, its parent given by the indentation), the BASIC code of the events beside it, the code of the window
generated. See docs/studio/README.md.

    python3 tools/screenshot/mockup_studio.py  -> docs/studio/mockups/studio-*.png

On the real desktop (screenshots/desktop.png, 1024 x 768); the drawing helpers are mockup_archiver.py's, the
toolbar icons Letters' (as Slides' mock-ups did). The project shown: "Converter", a temperature converter -- the
example SD:/basic/examples/gui.bas grown into a real app.

MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
"""
import os, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, HERE)
import mockup_slides as S
M = S.M
M.OUT = os.path.join (M.ROOT, "docs", "studio", "mockups")
K = M.K
TEXT, DIM, FACE, SEL, WHITE, LINE, LINE2, FAINT = M.TEXT, M.DIM, M.FACE, M.SEL, (255, 255, 255), M.LINE, M.LINE2, M.FAINT
PANEL = S.PANEL
CANVAS = (196, 190, 186)		# the design surface (behind the form)
DOT = (178, 172, 168)
CODEBG = (252, 251, 250)
GUTTER = (238, 233, 229)
KW = (36, 96, 122); STR = (176, 88, 36); COM = (118, 138, 104); NUM = (142, 72, 156); TYPE = (150, 92, 40)
GUIDE_C = (226, 60, 140)
BLUEPRINT = (70, 140, 210)
M.F["code"] = M._f ("DejaVuSansMono.ttf", 12)
M.F["codeb"] = M._f ("DejaVuSansMono-Bold.ttf", 12)
M.F["codei"] = M._f ("DejaVuSansMono-Oblique.ttf", 12)
M.F["tag"] = M._f ("DejaVuSans-Bold.ttf", 9)
M.F["h2"] = M._f ("DejaVuSans-Bold.ttf", 17)

WX, WY, WW, WH = 4, 30, 1016, 734

def screen (menus = ("File", "Edit", "View", "Project", "Run", "Debug", "Help")):
	"""The desktop, Studio's menus in the top bar."""
	c = M.Canvas ()
	c.img.paste (S.DESK.resize ((M.W * K, M.H * K), Image.LANCZOS), (0, 0)); c.d = ImageDraw.Draw (c.img, "RGBA")
	c.rect (0, 0, 640, 27, (230, 222, 217))
	x = 18; c.text_l (x, 0, 27, "Onyx", "menu"); x += c.tw ("Onyx", "menu") + 18
	c.text_l (x, 0, 27, "Studio", "menub"); x += c.tw ("Studio", "menub") + 20
	for m in menus: c.text_l (x, 0, 27, m, "menu", TEXT); x += c.tw (m, "menu") + 18
	return c, {}

# ---- the toolbar ----------------------------------------------------------------------------------------------
def ic_run (c, x, y, s = 20, col = (60, 150, 80)):
	c.poly ([(x + 5, y + 3), (x + 5, y + s - 3), (x + s - 3, y + s / 2)], col)
def ic_debug (c, x, y, s = 20):
	c.ellipse (x + 10, y + 11, 6, (200, 74, 64)); c.ellipse (x + 10, y + 5, 3.5, (200, 74, 64))
	for dy in (8, 12, 15): c.line ([(x + 2, y + dy), (x + 18, y + dy)], (200, 74, 64), 1.3)
def ic_stop (c, x, y, s = 20, col = (190, 70, 60)): c.rect (x + 5, y + 5, s - 10, s - 10, col, r = 2)
def ic_build (c, x, y, s = 20):
	c.rect (x + 3, y + 11, 14, 6, (160, 120, 80), r = 1); c.rect (x + 6, y + 5, 8, 6, (190, 150, 100), r = 1); c.rect (x + 8, y + 2, 4, 3, (210, 175, 125))
def ic_step_over (c, x, y, s = 20):
	c.d.arc ([(x + 3) * K, (y + 3) * K, (x + 17) * K, (y + 15) * K], 180, 360, fill = SEL, width = int (2 * K))
	c.poly ([(x + 14, y + 7), (x + 19, y + 9), (x + 15, y + 12)], SEL); c.ellipse (x + 10, y + 16, 2.2, TEXT)
def ic_step_into (c, x, y, s = 20):
	c.vline (x + 10, y + 2, y + 12, SEL, 2); c.poly ([(x + 6, y + 10), (x + 14, y + 10), (x + 10, y + 15)], SEL); c.ellipse (x + 10, y + 17, 2.2, TEXT)
def ic_step_out (c, x, y, s = 20):
	c.vline (x + 10, y + 6, y + 15, SEL, 2); c.poly ([(x + 6, y + 8), (x + 14, y + 8), (x + 10, y + 3)], SEL); c.ellipse (x + 10, y + 17, 2.2, TEXT)
def ic_pause (c, x, y, s = 20): c.rect (x + 5, y + 4, 4, 12, TEXT); c.rect (x + 11, y + 4, 4, 12, TEXT)

def toolbar (c, x, y, w, debug = False):
	c.rect (x, y, w, 34, FACE)
	r1 = y + 2
	bx = S.crop (c, "file", x + 2, r1)
	bx = S.tb_sep (c, bx, r1 + 1)
	# run, debug, stop, build
	c.grad (bx + 2, r1 + 2, 78, 25, (120, 190, 120), (70, 150, 80), r = 5); c.rect (bx + 2, r1 + 2, 78, 25, r = 5, outline = (50, 110, 60))
	ic_run (c, bx + 6, r1 + 5, 19, WHITE); c.text_l (bx + 28, r1 + 2, 25, "Run" if not debug else "Continue", "smallb", WHITE); bx += 84
	bx = S.tb_btn (c, bx, r1, ic_debug, arrow = True)
	if debug:
		bx = S.tb_btn (c, bx, r1, ic_pause); bx = S.tb_btn (c, bx, r1, ic_stop)
		bx = S.tb_sep (c, bx, r1 + 1)
		bx = S.tb_btn (c, bx, r1, ic_step_over); bx = S.tb_btn (c, bx, r1, ic_step_into); bx = S.tb_btn (c, bx, r1, ic_step_out)
	else:
		bx = S.tb_btn (c, bx, r1, lambda c, x, y: ic_stop (c, x, y, col = FAINT))
		bx = S.tb_btn (c, bx, r1, ic_build)
	bx = S.tb_sep (c, bx, r1 + 1)
	S.combo (c, bx + 4, r1 + 2, 96, "Debug"); bx += 108
	# the views at the right
	seg = M.segmented
	tot = 0
	return y + 35

def views (c, x, y, sel):
	items = ["Design", "Split", "Code"]
	ws = [c.tw (s, "small") + 22 for s in items]; tot = sum (ws)
	x -= tot
	c.rect (x, y, tot, 22, WHITE, r = 5, outline = M.shade (FACE, 0.62))
	cx = x
	for i, (s, w) in enumerate (zip (items, ws)):
		if s == sel: c.rect (cx + 2, y + 2, w - 4, 18, SEL, r = 4)
		elif i: c.vline (cx, y + 4, y + 18, LINE2)
		c.text_c (cx, y, w, 22, s, "smallb" if s == sel else "small", WHITE if s == sel else TEXT); cx += w

# ---- the panes ------------------------------------------------------------------------------------------------
def pane_head (c, x, y, w, title, extra = None):
	c.rect (x, y, w, 22, M.lighten (FACE, 0.3)); c.hline (x, x + w, y + 22, M.shade (FACE, 0.82))
	c.text_l (x + 8, y, 22, title, "smallb", TEXT)
	if extra: c.text_r (x + w - 8, y, 22, extra, "small", DIM)
	return y + 23

def tree_row (c, x, y, w, depth, label, kind, open_ = None, sel = False, dirty = False, note = None):
	if sel: c.rect (x + 2, y, w - 4, 20, S.M.SEL_SOFT, r = 3)
	ix = x + 8 + depth * 14
	if open_ is not None:
		if open_: c.line ([(ix, y + 8), (ix + 4, y + 12), (ix + 8, y + 8)], DIM, 1.3)
		else: c.line ([(ix + 2, y + 6), (ix + 6, y + 10), (ix + 2, y + 14)], DIM, 1.3)
	ix += 12
	if kind == "proj": c.rect (ix, y + 4, 13, 12, (120, 96, 196), r = 2); c.text_c (ix, y + 4, 13, 12, "B", "tag", WHITE)
	elif kind == "dir": M.ic_folder (c, ix - 1, y + 2, 15)
	elif kind == "form": c.rect (ix, y + 3, 13, 14, WHITE, r = 2, outline = SEL); c.rect (ix, y + 3, 13, 4, SEL, r = 1); c.rect (ix + 2, y + 9, 5, 2, SEL); c.rect (ix + 2, y + 13, 9, 2, DIM)
	elif kind == "bas": c.rect (ix, y + 3, 13, 14, WHITE, r = 2, outline = (74, 128, 200)); c.text_c (ix, y + 3, 13, 14, "b", "tag", (74, 128, 200))
	elif kind == "gen": c.rect (ix, y + 3, 13, 14, (240, 236, 232), r = 2, outline = FAINT); c.text_c (ix, y + 3, 13, 14, "g", "tag", FAINT)
	elif kind == "img": c.rect (ix, y + 3, 14, 14, (78, 160, 92), r = 2); c.poly ([(ix + 2, y + 15), (ix + 7, y + 8), (ix + 12, y + 15)], WHITE)
	elif kind == "ini": c.rect (ix, y + 3, 13, 14, WHITE, r = 2, outline = (190, 120, 60)); c.hline (ix + 3, ix + 10, y + 8, (190, 120, 60)); c.hline (ix + 3, ix + 10, y + 12, (190, 120, 60))
	c.text_l (ix + 20, y, 20, label + (" ●" if dirty else ""), "small", FAINT if kind == "gen" else TEXT)
	if note: c.text_r (x + w - 8, y, 20, note, "small", FAINT)
	return y + 20

def project_pane (c, x, y, w, h, sel = "Main.form"):
	c.rect (x, y, w, h, PANEL)
	y = pane_head (c, x, y, w, "Project")
	rows = [(0, "Converter", "proj", True), (1, "Forms", "dir", True), (2, "Main.form", "form", None), (2, "About.form", "form", None),
		(1, "Code", "dir", True), (2, "Main.bas", "bas", None), (2, "Units.bas", "bas", None), (1, "Generated", "dir", False),
		(1, "Resources", "dir", True), (2, "icon.bmp", "img", None), (2, "app.txt", "ini", None)]
	for d, s, k, o in rows:
		y = tree_row (c, x, y + 1, w, d, s, k, o, sel = s == sel, dirty = s == "Main.form")
	return y

TOOLS = [("Layout", ["Column", "Row", "Grid", "Group", "Tabs", "Scroll", "Spacer"]),
	 ("Controls", ["Label", "Button", "TextBox", "CheckBox", "Radio", "ListBox", "DropDown", "Slider", "Progress", "Picture", "Canvas"]),
	 ("Window", ["Menu", "ToolBar", "StatusBar", "Timer"])]

def tool_glyph (c, name, x, y):
	"""A 16 x 16 picture of each element of the toolbox."""
	g = SEL; d = DIM
	if name == "Column":
		for k in range (3): c.rect (x + 2, y + 1 + k * 5, 12, 4, M.lighten (g, 0.35), r = 1, outline = g)
	elif name == "Row":
		for k in range (3): c.rect (x + 1 + k * 5, y + 2, 4, 12, M.lighten (g, 0.35), r = 1, outline = g)
	elif name == "Grid":
		c.rect (x + 1, y + 1, 14, 14, WHITE, outline = g)
		c.vline (x + 6, y + 1, y + 15, g); c.vline (x + 10, y + 1, y + 15, g); c.hline (x + 1, x + 15, y + 6, g); c.hline (x + 1, x + 15, y + 10, g)
	elif name == "Group": c.rect (x + 1, y + 3, 14, 12, outline = d, r = 2); c.rect (x + 3, y + 1, 7, 4, PANEL); c.hline (x + 3, x + 9, y + 3, TEXT, 2)
	elif name == "Tabs": c.rect (x + 1, y + 5, 14, 10, WHITE, outline = d); c.rect (x + 1, y + 1, 6, 5, SEL); c.rect (x + 8, y + 2, 5, 4, outline = d)
	elif name == "Scroll": c.rect (x + 1, y + 1, 14, 14, WHITE, outline = d); c.rect (x + 11, y + 3, 3, 6, d, r = 1)
	elif name == "Spacer": c.line ([(x + 2, y + 8), (x + 14, y + 8)], d, 1.4); c.line ([(x + 2, y + 5), (x + 2, y + 11)], d, 1.4); c.line ([(x + 14, y + 5), (x + 14, y + 11)], d, 1.4)
	elif name == "Label": c.text_c (x, y, 16, 16, "A", "uib", TEXT)
	elif name == "Button": c.grad (x, y + 3, 16, 10, (250, 248, 246), (210, 204, 200), r = 3); c.rect (x, y + 3, 16, 10, r = 3, outline = d)
	elif name == "TextBox": c.rect (x, y + 3, 16, 10, WHITE, r = 2, outline = d); c.vline (x + 4, y + 5, y + 11, TEXT)
	elif name == "CheckBox": c.rect (x + 2, y + 2, 12, 12, SEL, r = 2); c.line ([(x + 5, y + 8), (x + 7, y + 10), (x + 11, y + 5)], WHITE, 1.6)
	elif name == "Radio": c.ellipse (x + 8, y + 8, 6, WHITE, d); c.ellipse (x + 8, y + 8, 3, SEL)
	elif name == "ListBox":
		c.rect (x + 1, y + 1, 14, 14, WHITE, outline = d); c.rect (x + 2, y + 5, 12, 3, M.SEL_SOFT)
		for k in range (3): c.hline (x + 3, x + 12, y + 3 + k * 4, d)
	elif name == "DropDown": c.rect (x, y + 3, 16, 10, WHITE, r = 2, outline = d); c.poly ([(x + 10, y + 7), (x + 14, y + 7), (x + 12, y + 10)], TEXT)
	elif name == "Slider": c.hline (x + 1, x + 15, y + 8, d, 2); c.rect (x + 6, y + 3, 4, 10, SEL, r = 1)
	elif name == "Progress": c.rect (x, y + 5, 16, 6, WHITE, r = 3, outline = d); c.rect (x + 1, y + 6, 9, 4, (78, 160, 92), r = 2)
	elif name == "Picture": c.rect (x + 1, y + 2, 14, 12, (120, 170, 210)); c.poly ([(x + 2, y + 13), (x + 7, y + 7), (x + 12, y + 13)], (78, 140, 80)); c.ellipse (x + 11, y + 5, 2, (250, 220, 110))
	elif name == "Canvas": c.rect (x + 1, y + 1, 14, 14, WHITE, outline = d); c.line ([(x + 3, y + 12), (x + 7, y + 6), (x + 10, y + 9), (x + 13, y + 3)], (200, 74, 64), 1.4)
	elif name == "Menu": c.rect (x, y + 1, 16, 4, d); c.rect (x + 3, y + 5, 10, 10, WHITE, outline = d); c.hline (x + 5, x + 11, y + 8, d); c.hline (x + 5, x + 11, y + 11, d)
	elif name == "ToolBar":
		c.rect (x, y + 4, 16, 8, M.lighten (FACE, 0.3), outline = d)
		for k in range (3): c.rect (x + 2 + k * 5, y + 6, 3, 4, SEL)
	elif name == "StatusBar": c.rect (x, y + 9, 16, 5, M.lighten (FACE, 0.3), outline = d); c.hline (x + 2, x + 9, y + 11, d)
	elif name == "Timer": c.ellipse (x + 8, y + 8, 7, WHITE, d); c.line ([(x + 8, y + 8), (x + 8, y + 3)], TEXT, 1.4); c.line ([(x + 8, y + 8), (x + 11, y + 10)], TEXT, 1.4)

def toolbox (c, x, y, w, h, hot = None):
	c.rect (x, y, w, h, PANEL)
	y = pane_head (c, x, y, w, "Toolbox")
	M.field (c, x + 6, y + 4, w - 12, 22, placeholder = "Search the controls", font = "small"); y += 30
	col = (w - 12) / 2
	for title, items in TOOLS:
		c.line ([(x + 8, y + 7), (x + 12, y + 11), (x + 16, y + 7)], DIM, 1.3); c.text_l (x + 20, y, 18, title, "smallb", DIM); y += 19
		for k, it in enumerate (items):
			ix = x + 6 + (k % 2) * col; iy = y + (k // 2) * 21
			if it == hot: c.rect (ix, iy, col - 2, 20, M.SEL_SOFT, r = 3, outline = SEL)
			tool_glyph (c, it, ix + 4, iy + 2)
			c.text_l (ix + 25, iy, 20, it, "small")
		y += ((len (items) + 1) // 2) * 21 + 4
	return y

# ---- the form being designed: Converter's main window -----------------------------------------------------------
FORM = """# Main.form -- the converter's window (Studio writes it; it reads as you see it)
Window Main "Temperature converter" size=380x260 min=320x220 resizable
  Menu
    "&File"
      "&Copy the result" name=mnuCopy key=Ctrl+C
      -
      "&Quit" name=mnuQuit key=Ctrl+Q
    "&Help"
      "&About Converter" name=mnuAbout
  Column padding=14 gap=10
    Row gap=8
      Label "Celsius:" width=90
      TextBox celsius "20" fill
    Row gap=8
      Label "Fahrenheit:" width=90
      TextBox fahrenheit "" fill readonly
    CheckBox live "Convert as I type" checked
    Slider scale max=200 fill
    Spacer
    Row gap=8 align=right
      Button clear "Clear" cancel
      Button convert "Convert" default
  StatusBar status "Ready"
"""

def highlight_form (line):
	"""(text, colour, font) runs of a .form line."""
	out = []
	if line.lstrip ().startswith ("#"): return [(line, COM, "codei")]
	s = line
	ind = len (s) - len (s.lstrip ()); out.append ((s[:ind], TEXT, "code")); s = s[ind:]
	parts = []
	i = 0; first = True
	while i < len (s):
		if s[i] == '"':
			j = s.index ('"', i + 1) + 1; out.append ((s[i:j], STR, "code")); i = j; continue
		if s[i] == " ": out.append ((" ", TEXT, "code")); i += 1; continue
		j = i
		while j < len (s) and s[j] != " ": j += 1
		w = s[i:j]
		if first and w[0].isupper () and w != "-": out.append ((w, KW, "codeb"))
		elif "=" in w:
			k, v = w.split ("=", 1)
			out.append ((k + "=", TYPE, "code")); out.append ((v, NUM if v[:1].isdigit () else TEXT, "code"))
		elif w == "-": out.append ((w, DIM, "code"))
		elif w in ("fill", "readonly", "checked", "resizable", "default", "cancel"): out.append ((w, TYPE, "code"))
		else: out.append ((w, TEXT, "codeb"))
		first = False; i = j
	return out

BAS_KW = set ("SUB END FUNCTION IF THEN ELSE ELSEIF DIM AS SHARED STRING INTEGER DOUBLE FOR TO NEXT DO LOOP WHILE WEND SELECT CASE RETURN CALL CONST NOT AND OR".split ())
BAS_FN = set ("VAL STR$ TIME$ FORMAT$ MSGBOX SETCLIPBOARD NOTIFY LEFT$ RIGHT$ INT".split ())
def highlight_bas (line):
	out = []
	i = 0; s = line
	while i < len (s):
		ch = s[i]
		if ch == "'": out.append ((s[i:], COM, "codei")); break
		if ch == '"':
			j = s.index ('"', i + 1) + 1; out.append ((s[i:j], STR, "code")); i = j; continue
		if ch.isdigit ():
			j = i
			while j < len (s) and (s[j].isdigit () or s[j] == "."): j += 1
			out.append ((s[i:j], NUM, "code")); i = j; continue
		if ch.isalpha () or ch == "_":
			j = i
			while j < len (s) and (s[j].isalnum () or s[j] in "_$."): j += 1
			w = s[i:j]
			if w.upper () in BAS_KW: out.append ((w, KW, "codeb"))
			elif w.upper () in BAS_FN: out.append ((w, TYPE, "code"))
			elif "." in w:
				a, b = w.split (".", 1); out.append ((a, TEXT, "code")); out.append (("." + b, (40, 120, 140), "code"))
			else: out.append ((w, TEXT, "code"))
			i = j; continue
		out.append ((ch, TEXT, "code")); i += 1
	return out

def code_view (c, x, y, w, h, lines, hl, first = 1, cur = None, bp = (), ip = None, marks = None, guides = True, scroll = (0.0, 0.5)):
	c.rect (x, y, w, h, CODEBG)
	gw = 46
	c.rect (x, y, gw, h, GUTTER); c.vline (x + gw, y, y + h, LINE2)
	lh = 17
	yy = y + 4
	for k, ln in enumerate (lines):
		n = first + k
		if yy + lh > y + h: break
		if n == ip: c.rect (x + gw + 1, yy, w - gw - 14, lh, (252, 236, 160))
		elif n == cur: c.rect (x + gw + 1, yy, w - gw - 14, lh, (234, 242, 246))
		c.text_r (x + gw - 18, yy, lh, str (n), "code", SEL if n in (cur, ip) else FAINT)
		if n in bp: c.ellipse (x + 9, yy + lh / 2, 5.5, (200, 60, 56))
		if n == ip: c.poly ([(x + 3, yy + 3), (x + 3, yy + lh - 3), (x + 12, yy + lh / 2)], (230, 170, 30))
		# the indentation guides
		ind = len (ln) - len (ln.lstrip ())
		tx = x + gw + 8
		cw = c.tw ("M", "code")
		if guides:
			for g in range (2, ind + 1, 2): c.vline (tx + (g - 2) * cw + cw / 2, yy, yy + lh, (226, 222, 218))
		for s, col, f in hl (ln):
			c.text_l (tx, yy, lh, s, f, col); tx += c.tw (s, f)
		if marks and n in marks:
			kind, msg = marks[n]
			mx = x + gw + 8 + len (ln.rstrip ()) * cw + 14
			col = (200, 60, 56) if kind == "error" else (210, 150, 30)
			c.rect (mx, yy + 2, c.tw (msg, "small") + 12, lh - 4, M.A (col, 40), r = 3); c.text_l (mx + 6, yy, lh, msg, "small", col)
		yy += lh
	M.scrollbar (c, x + w - 12, y + 4, h - 8, scroll[0], scroll[1])

def form_window (c, x, y, w, h, title, state = None):
	"""The designed window, drawn as Onyx draws it; the boxes of its layout returned (for the overlays)."""
	cx, cy, cw, ch = M.window (c, x, y, w, h, title)
	# its menu bar
	c.rect (cx, cy, cw, 22, M.lighten (FACE, 0.45)); c.hline (cx, cx + cw, cy + 22, LINE2)
	c.text_l (cx + 10, cy, 22, "File", "small"); c.text_l (cx + 48, cy, 22, "Help", "small")
	boxes = {}
	pad = 14
	x0, y0, x1 = cx + pad, cy + 22 + pad, cx + cw - pad
	boxes["column"] = (x0, y0, x1 - x0, ch - 22 - 22 - 2 * pad)
	ry = y0
	for label, name, val, ro in (("Celsius:", "celsius", state[0] if state else "20", False), ("Fahrenheit:", "fahrenheit", state[1] if state else "", True)):
		boxes["row_" + name] = (x0, ry, x1 - x0, 26)
		c.text_l (x0, ry, 26, label, "ui")
		M.field (c, x0 + 98, ry, x1 - x0 - 98, 26, val, disabled = False)
		if ro: c.rect (x0 + 99, ry + 1, x1 - x0 - 100, 24, M.A (FACE, 70), r = 4)
		boxes[name] = (x0 + 98, ry, x1 - x0 - 98, 26)
		ry += 36
	M.checkbox (c, x0, ry + 2, "Convert as I type", True); boxes["live"] = (x0, ry, 160, 20); ry += 30
	# the slider
	c.hline (x0, x1, ry + 9, M.shade (FACE, 0.7), 4); c.rect (x0, ry + 9, (x1 - x0) * 0.32, 4, SEL, r = 2)
	c.rect (x0 + (x1 - x0) * 0.32 - 6, ry + 1, 12, 20, WHITE, r = 3, outline = M.shade (FACE, 0.55)); boxes["scale"] = (x0, ry, x1 - x0, 22); ry += 30
	# the buttons, at the bottom right
	by = boxes["column"][1] + boxes["column"][3] - 30
	boxes["spacer"] = (x0, ry, x1 - x0, by - 10 - ry)
	M.button (c, x1 - 96, by, 96, 30, "Convert", default = True); boxes["convert"] = (x1 - 96, by, 96, 30)
	M.button (c, x1 - 96 - 8 - 84, by, 84, 30, "Clear"); boxes["clear"] = (x1 - 188, by, 84, 30)
	boxes["row_buttons"] = (x0, by, x1 - x0, 30)
	# the status bar
	sy = cy + ch - 22
	c.rect (cx, sy, cw, 22, M.lighten (FACE, 0.2)); c.hline (cx, cx + cw, sy, LINE2)
	c.text_l (cx + 8, sy, 22, state[2] if state else "Ready", "small", DIM)
	return boxes

def tag (c, x, y, s, col = BLUEPRINT):
	w = c.tw (s, "tag") + 8
	c.rect (x, y - 12, w, 12, col, r = 2); c.text_c (x, y - 12, w, 12, s, "tag", WHITE)

def blueprint (c, b, label, col = BLUEPRINT):
	x, y, w, h = b
	c.dashed (x - 3, y - 3, w + 6, h + 6, col, dash = 4, r = 3)
	tag (c, x - 3, y - 3, label, col)

def design_surface (c, x, y, w, h, drop = True):
	c.rect (x, y, w, h, CANVAS)
	for gy in range (int (y) + 8, int (y + h), 16):
		for gx in range (int (x) + 8, int (x + w), 16): c.rect (gx, gy, 1.2, 1.2, DOT)
	fx, fy, fw, fh = x + (w - 400) / 2, y + 26, 400, 296
	boxes = form_window (c, fx, fy, fw, fh, "Temperature converter")
	# the layout's boxes (the blueprint), the selection
	blueprint (c, boxes["column"], "Column  padding 14 · gap 10", (110, 150, 200))
	for k in ("row_celsius", "row_fahrenheit", "row_buttons"): c.dashed (*[v for v in (boxes[k][0] - 1, boxes[k][1] - 1, boxes[k][2] + 2, boxes[k][3] + 2)], (150, 180, 220), dash = 3, r = 2)
	tag (c, boxes["row_buttons"][0], boxes["row_buttons"][1] - 1, "Row  align right", (110, 150, 200))
	sx, sy, sw, sh = boxes["spacer"]
	c.line ([(sx + 30, sy + 4), (sx + 30, sy + sh - 4)], (150, 180, 220), 1); c.text_l (sx + 36, sy, sh, "Spacer: the free height", "tag", (110, 150, 200))
	S.handles (c, *boxes["convert"], rot = False)
	c.rect (boxes["convert"][0], boxes["convert"][1] + 30 + 4, 82, 13, SEL, r = 2); c.text_c (boxes["convert"][0], boxes["convert"][1] + 34, 82, 13, "Button convert", "tag", WHITE)
	# a CheckBox being dropped from the toolbox: its place in the Column, shown before the drop
	if drop:
		lx, ly = boxes["live"][0], boxes["live"][1] + 25
		c.rect (lx - 2, ly, boxes["column"][2] + 4, 3, GUIDE_C, r = 1)
		c.ellipse (lx - 2, ly + 1.5, 3.5, GUIDE_C); c.ellipse (lx + boxes["column"][2] + 2, ly + 1.5, 3.5, GUIDE_C)
		gx, gy = lx + 190, ly + 10
		c.rect (gx, gy, 118, 24, M.A (WHITE, 220), r = 4, outline = SEL)
		tool_glyph (c, "CheckBox", gx + 5, gy + 4); c.text_l (gx + 26, gy, 24, "CheckBox", "small")
		M.cursor (c, gx + 60, gy + 14)
		c.text (x + 20, y + h - 24, "Dropped here, the CheckBox becomes the Column's 5th child: no x, no y to give.", "small", (70, 64, 60))
	# the form's size, the breakpoints of a resize
	c.text (fx + fw + 10, fy + fh - 16, "380 × 260", "small", DIM)
	return boxes

def tabs (c, x, y, w, items, sel):
	c.rect (x, y, w, 26, M.shade (FACE, 0.92))
	tx = x + 4
	for s, kind in items:
		tw = c.tw (s, "smallb") + 46
		if s == sel: c.rect (tx, y + 3, tw, 23, CODEBG if kind != "form" else M.lighten (FACE, 0.4), r = 4, corners = (True, True, False, False))
		ix = tx + 8
		if kind == "form": c.rect (ix, y + 9, 11, 12, WHITE, r = 2, outline = SEL); c.rect (ix, y + 9, 11, 3, SEL)
		elif kind == "gen": c.rect (ix, y + 9, 11, 12, (240, 236, 232), r = 2, outline = FAINT)
		else: c.rect (ix, y + 9, 11, 12, WHITE, r = 2, outline = (74, 128, 200))
		c.text_l (ix + 16, y + 3, 23, s, "smallb" if s == sel else "small", FAINT if kind == "gen" else TEXT)
		c.text_l (tx + tw - 14, y + 3, 23, "×", "small", DIM)
		tx += tw + 2
	return y + 26

def prop_row (c, x, y, w, name, value, kind = "text", sel = False, link = False):
	kw = 96
	if sel: c.rect (x, y, w, 22, M.SEL_SOFT)
	c.hline (x, x + w, y + 22, LINE2); c.vline (x + kw, y, y + 22, LINE2)
	c.text_l (x + 8, y, 22, name, "small", TEXT)
	vx = x + kw + 6
	if kind == "check":
		c.rect (vx, y + 4, 14, 14, SEL if value else WHITE, r = 3, outline = M.shade (FACE, 0.6))
		if value: c.line ([(vx + 3, y + 11), (vx + 6, y + 14), (vx + 11, y + 7)], WHITE, 1.6)
	elif kind == "choice":
		c.text_l (vx, y, 22, value, "small"); c.poly ([(x + w - 16, y + 9), (x + w - 8, y + 9), (x + w - 12, y + 14)], DIM)
	elif kind == "event":
		if value: c.text_l (vx, y, 22, value, "small", M.LINK); c.hline (vx, vx + c.tw (value, "small"), y + 16, M.LINK)
		else: c.text_l (vx, y, 22, "(double-click: a new SUB)", "small", FAINT)
	elif kind == "colour":
		c.rect (vx, y + 4, 14, 14, value, r = 2, outline = LINE); c.text_l (vx + 20, y, 22, "theme's text", "small", DIM)
	else: c.text_l (vx, y, 22, value, "small", DIM if value.startswith ("(") else TEXT)
	return y + 22

def props_pane (c, x, y, w, h, obj = ("convert", "Button"), events = True):
	c.rect (x, y, w, h, PANEL)
	y = pane_head (c, x, y, w, "Properties")
	c.rect (x + 6, y + 4, w - 12, 30, WHITE, r = 4, outline = LINE)
	tool_glyph (c, obj[1], x + 12, y + 11)
	c.text_l (x + 34, y + 4, 30, obj[0], "uib"); c.text_l (x + 40 + c.tw (obj[0], "uib"), y + 4, 30, obj[1], "small", DIM)
	y += 40
	c.rect (x + 6, y, w - 12, 22, WHITE, r = 5, outline = M.shade (FACE, 0.7))
	half = (w - 16) / 2
	for k, s in enumerate (("Properties", "Events")):
		on = (k == 0)
		if on: c.rect (x + 8 + k * half, y + 2, half - 2, 18, SEL, r = 4)
		c.text_c (x + 6 + k * half, y, half, 22, s, "smallb" if on else "small", WHITE if on else TEXT)
	y += 30
	y = S.section (c, x, y, w, "Common")
	for row in (("Name", "convert"), ("Text", "Convert"), ("Default", True, "check"), ("Cancel", False, "check"), ("Enabled", True, "check"),
		    ("Visible", True, "check"), ("Tooltip", "Celsius to Fahrenheit")):
		y = prop_row (c, x, y, w, row[0], row[1], row[2] if len (row) > 2 else "text")
	y = S.section (c, x, y + 4, w, "Layout")
	for row in (("Width", "(as its text)"), ("Height", "(as its text)"), ("Fill", False, "check"), ("Margin", "0"), ("Align", "right", "choice")):
		y = prop_row (c, x, y, w, row[0], row[1], row[2] if len (row) > 2 else "text")
	if events:
		y = S.section (c, x, y + 4, w, "Events")
		y = prop_row (c, x, y, w, "Click", "convert_Click", "event", sel = True)
		y = prop_row (c, x, y, w, "Focus", "", "event")
	return y

def statusbar (c, x, y, w, left, right):
	c.rect (x, y, w, 22, FACE); c.hline (x, x + w, y, M.shade (FACE, 0.84))
	tx = x + 10
	for s in left: c.text_l (tx, y, 22, s, "combo"); tx += c.tw (s, "combo") + 24
	rx = x + w - 10
	for s in right: c.text_r (rx, y, 22, s, "combo"); rx -= c.tw (s, "combo") + 24

def ide (c, title = "Studio — Converter", debug = False):
	cx, cy, cw, ch = M.window (c, WX, WY, WW, WH, title)
	y = toolbar (c, cx, cy, cw, debug = debug)
	return cx, cy, cw, ch, y

LW, RW = 196, 236

# ---- 1. the designer ---------------------------------------------------------------------------------------------
def mock_designer ():
	c, _ = screen ()
	cx, cy, cw, ch, y = ide (c)
	views (c, cx + cw - 8, cy + 6, "Split")
	body = cy + ch - 22 - y
	py = project_pane (c, cx, y, LW, 252)
	toolbox (c, cx, y + 252, LW, body - 252, hot = "CheckBox")
	c.vline (cx + LW, y, y + body, M.shade (FACE, 0.8))
	ex, ew = cx + LW + 1, cw - LW - 1 - RW
	ty = tabs (c, ex, y, ew, [("Main.form", "form"), ("Main.bas", "bas"), ("About.form", "form")], "Main.form")
	split = 352
	design_surface (c, ex, ty, ew, split)
	# the splitter, then the form's text (kept in step with the drawing)
	c.rect (ex, ty + split, ew, 6, M.shade (FACE, 0.92)); c.rect (ex + ew / 2 - 16, ty + split + 2, 32, 2, DIM, r = 1)
	lines = FORM.rstrip ("\n").split ("\n")
	code_view (c, ex, ty + split + 6, ew, y + body - (ty + split + 6), lines[8:], highlight_form, first = 9, cur = 22, scroll = (0.3, 0.9))
	props_pane (c, cx + cw - RW, y, RW, body)
	c.vline (cx + cw - RW - 1, y, y + body, M.shade (FACE, 0.8))
	statusbar (c, cx, cy + ch - 22, cw, ["Main.form", "Button convert", "Main › Column › Row › convert"], ["Saved", "Ln 22, Col 7"])
	c.save ("studio-designer.png")

# ---- 2. the code --------------------------------------------------------------------------------------------------
MAIN_BAS = """' Main.bas -- what the converter does (the window itself: Main.form)

SUB Main_Load
  status.Text = "Type a temperature in Celsius"
  celsius.Focus
END SUB

SUB convert_Click
  DIM c AS DOUBLE, f AS DOUBLE
  c = VAL(celsius.Text)
  f = c * 9 / 5 + 32
  fahrenheit.Text = FORMAT$(f, "0.0")
  scale.Value = INT(f)
  status.Text = "Converted at " + TIME$
END SUB

SUB celsius_Change
  IF live.Checked THEN convert_Click
END SUB

SUB clear_Click
  celsius.Text = "": fahrenheit.Text = ""
  status.Text = "Ready"
END SUB

SUB mnuCopy_Click
  SETCLIPBOARD fahrenheit.
END SUB

SUB mnuQuit_Click
  Main.Close
END SUB
"""

def dropdown_pair (c, x, y, w, a, b):
	half = (w - 6) / 2
	M.dropdown (c, x, y, half, 24, a); M.dropdown (c, x + half + 6, y, half, 24, b)

def outline_pane (c, x, y, w, h, sel = "mnuCopy_Click"):
	c.rect (x, y, w, h, PANEL)
	y = pane_head (c, x, y, w, "Outline", "Main.bas")
	items = [("Main", "Window", [("Main_Load", "Load")]), ("celsius", "TextBox", [("celsius_Change", "Change")]),
		 ("convert", "Button", [("convert_Click", "Click")]), ("clear", "Button", [("clear_Click", "Click")]),
		 ("mnuCopy", "Menu", [("mnuCopy_Click", "Click")]), ("mnuQuit", "Menu", [("mnuQuit_Click", "Click")]),
		 ("mnuAbout", "Menu", [])]
	for name, kind, subs in items:
		tool_glyph (c, kind if kind not in ("Window", "Menu") else ("Menu" if kind == "Menu" else "Group"), x + 10, y + 3)
		c.text_l (x + 32, y, 22, name, "smallb"); c.text_l (x + 40 + c.tw (name, "smallb"), y, 22, kind, "small", DIM); y += 22
		for s, ev in subs:
			if s == sel: c.rect (x + 24, y, w - 30, 20, M.SEL_SOFT, r = 3)
			c.text_l (x + 36, y, 20, "SUB " + s, "small", M.LINK); y += 20
		if not subs:
			c.text_l (x + 36, y, 20, "Click: (none yet)", "small", FAINT); y += 20
	y += 6
	c.hline (x + 8, x + w - 8, y, LINE2); y += 6
	c.text (x + 10, y, "Units.bas", "smallb"); y += 20
	for s in ("FUNCTION CToF (c)", "FUNCTION FToC (f)"): c.text_l (x + 36, y, 20, s, "small", M.LINK); y += 20
	return y

def bottom_panel (c, x, y, w, h, tabs_, sel, rows):
	c.rect (x, y, w, h, WHITE); c.hline (x, x + w, y, M.shade (FACE, 0.8))
	c.rect (x, y, w, 24, M.lighten (FACE, 0.35))
	tx = x + 6
	for s in tabs_:
		tw = c.tw (s, "small") + 20
		if s == sel: c.rect (tx, y + 3, tw, 21, WHITE, r = 4, corners = (True, True, False, False))
		c.text_c (tx, y + 3, tw, 21, s, "smallb" if s == sel else "small"); tx += tw + 2
	yy = y + 28
	for icon, s, where in rows:
		col = {"ok": (78, 160, 92), "warn": (210, 150, 30), "err": (200, 60, 56), "info": SEL}[icon]
		c.ellipse (x + 14, yy + 9, 5, col)
		c.text_l (x + 26, yy, 18, s, "small"); c.text_r (x + w - 12, yy, 18, where, "small", DIM)
		yy += 20

def mock_code ():
	c, _ = screen ()
	cx, cy, cw, ch, y = ide (c)
	views (c, cx + cw - 8, cy + 6, "Code")
	body = cy + ch - 22 - y
	project_pane (c, cx, y, LW, 252, sel = "Main.bas")
	outline_pane (c, cx, y + 252, LW, body - 252)
	c.vline (cx + LW, y, y + body, M.shade (FACE, 0.8))
	ex, ew = cx + LW + 1, cw - LW - 1
	ty = tabs (c, ex, y, ew, [("Main.form", "form"), ("Main.bas", "bas"), ("Main.form.bas", "gen")], "Main.bas")
	c.rect (ex, ty, ew, 32, M.lighten (FACE, 0.35))
	dropdown_pair (c, ex + 50, ty + 4, 420, "mnuCopy", "Click")
	c.text_l (ex + 486, ty + 4, 24, "the object, its event: the SUB is written for you", "small", DIM)
	ty += 32
	panel_h = 118
	lines = MAIN_BAS.rstrip ("\n").split ("\n")
	code_view (c, ex, ty, ew, y + body - panel_h - ty, lines, highlight_bas, cur = 27, scroll = (0.2, 0.8))
	bottom_panel (c, ex, y + body - panel_h, ew, panel_h, ["Problems  1", "Output", "Find"], "Problems  1",
		      [("warn", "Main.bas 27: 'fahrenheit.' -- a property or a method is expected", "Main.bas  27:28"),
		       ("info", "Main.form: 9 controls, 6 events handled, 1 not (mnuAbout.Click)", "Main.form"),
		       ("ok", "Main.form.bas generated (the window's code: 112 lines, read-only)", "12:41:07")])
	# the completion of "fahrenheit."
	lh = 17; cw_ = c.tw ("M", "code")
	items = [("Text", "property", "STRING", True), ("ReadOnly", "property", "-1 / 0", False), ("Enabled", "property", "-1 / 0", False),
		 ("Visible", "property", "-1 / 0", False), ("Focus", "method", "", False), ("SelectAll", "method", "", False), ("Tooltip", "property", "STRING", False)]
	pw = 300
	px, py = ex + 46 + 8 + len ("  SETCLIPBOARD fahrenheit.") * cw_, ty + 4 + 26 * lh - (10 + len (items) * 22) - 2
	c.rect (px, py, pw, 10 + len (items) * 22, WHITE, r = 5, outline = M.shade (FACE, 0.6))
	for k, (n, kind, t, on) in enumerate (items):
		iy = py + 5 + k * 22
		if on: c.rect (px + 4, iy, pw - 8, 22, SEL, r = 4)
		c.rect (px + 10, iy + 5, 12, 12, (40, 120, 140) if kind == "property" else (150, 92, 40), r = 2)
		c.text_c (px + 10, iy + 5, 12, 12, "p" if kind == "property" else "m", "tag", WHITE)
		c.text_l (px + 30, iy, 22, n, "codeb" if on else "code", WHITE if on else TEXT)
		c.text_r (px + pw - 10, iy, 22, t, "small", (220, 236, 240) if on else DIM)
	# the tip beside it
	tx, tyy = px + pw + 6, py
	c.rect (tx, tyy, 236, 64, (255, 252, 236), r = 5, outline = (210, 196, 150))
	c.text (tx + 10, tyy + 8, "fahrenheit.Text  AS STRING", "codeb")
	c.text (tx + 10, tyy + 28, "The box's text (TextBox fahrenheit,", "small", DIM)
	c.text (tx + 10, tyy + 44, "Main.form line 15: readonly)", "small", DIM)
	statusbar (c, cx, cy + ch - 22, cw, ["Main.bas", "SUB mnuCopy_Click"], ["UTF-8 · BASIC", "Ln 27, Col 28"])
	c.save ("studio-code.png")

# ---- 3. running, debugging -------------------------------------------------------------------------------------------
def mock_debug ():
	c, _ = screen ()
	cx, cy, cw, ch, y = ide (c, "Studio — Converter  [paused]", debug = True)
	views (c, cx + cw - 8, cy + 6, "Code")
	body = cy + ch - 22 - y
	panel_h = 196
	ex, ew = cx, cw
	ty = tabs (c, ex, y, ew, [("Main.form", "form"), ("Main.bas", "bas")], "Main.bas")
	lines = MAIN_BAS.rstrip ("\n").split ("\n")
	code_view (c, ex, ty, ew, y + body - panel_h - ty, lines[:24], highlight_bas, bp = (11, 18), ip = 11, scroll = (0.0, 0.7))
	# the value under the pointer
	lh = 17; cw_ = c.tw ("M", "code")
	hx, hy = ex + 46 + 8 + 6 * cw_, ty + 4 + 10 * lh
	c.rect (hx - 2, hy - 1, cw_ * 1 + 4, lh + 2, M.A (SEL, 40), r = 2)
	tx = ex + 46 + 8 + len ("  f = c * 9 / 5 + 32") * cw_ + 16
	c.rect (tx, hy - 4, 128, 24, (255, 252, 236), r = 4, outline = (210, 196, 150)); c.text_l (tx + 8, hy - 4, 24, "c = 37.5", "codeb")
	c.line ([(hx + cw_ / 2, hy + lh / 2), (tx, hy + 8)], (210, 196, 150), 1)
	# the app, running beside it (its own window, the program's)
	ax, ay = cx + cw - 420, ty + 40
	form_window (c, ax, ay, 400, 296, "Temperature converter", state = ("37.5", "", "Type a temperature in Celsius"))
	M.cursor (c, ax + 330, ay + 250)
	# the panels: variables, watch, call stack
	py = y + body - panel_h
	c.rect (ex, py, ew, panel_h, WHITE); c.hline (ex, ex + ew, py, M.shade (FACE, 0.8))
	colw = [ew * 0.42, ew * 0.30, ew * 0.28]
	px = ex
	for title, rows in (("Variables  (convert_Click)", [("c", "37.5", "DOUBLE"), ("f", "0", "DOUBLE"), ("celsius.Text", "\"37.5\"", "STRING"),
							      ("live.Checked", "-1", "control"), ("scale.Value", "68", "control"), ("SHARED  units$", "\"C\"", "STRING")]),
			    ("Watch", [("f * 2", "0", ""), ("LEN(celsius.Text)", "4", ""), ("+ an expression", "", "")]),
			    ("Call stack", [("convert_Click", "Main.bas 11", ""), ("celsius_Change", "Main.bas 18", ""), ("(the events)", "Main.form.bas 87", "")])):
		w = colw.pop (0)
		c.rect (px, py, w, 24, M.lighten (FACE, 0.35)); c.text_l (px + 10, py, 24, title, "smallb")
		yy = py + 28
		for k, (a, b, t) in enumerate (rows):
			if title.startswith ("Call") and k == 0: c.poly ([(px + 8, yy + 4), (px + 8, yy + 16), (px + 16, yy + 10)], (230, 170, 30))
			c.text_l (px + 22, yy, 20, a, "code", FAINT if a.startswith ("+") or a.startswith ("(") else TEXT)
			c.text_l (px + w * 0.52, yy, 20, b, "codeb" if b else "code", NUM if b[:1].isdigit () or b[:1] == "-" else STR)
			if t: c.text_r (px + w - 10, yy, 20, t, "small", DIM)
			yy += 22
		px += w; c.vline (px, py, py + panel_h, LINE2)
	statusbar (c, cx, cy + ch - 22, cw, ["Paused at Main.bas 11 (a breakpoint)", "F5 continue · F10 over · F11 into"], ["Converter running", "Ln 11"])
	c.save ("studio-debug.png")

# ---- 4. a new project -----------------------------------------------------------------------------------------------
def template_card (c, x, y, w, h, title, desc, kind, sel = False):
	c.rect (x, y, w, h, WHITE if not sel else M.lighten (SEL, 0.82), r = 6, outline = SEL if sel else M.shade (FACE, 0.7), width = 2 if sel else 1)
	# a small picture of the window it makes
	px, py, pw, ph = x + 12, y + 10, w - 24, 70
	c.rect (px, py, pw, ph, (236, 230, 226), r = 4)
	c.rect (px, py, pw, 9, (238, 174, 122), r = 3, corners = (True, True, False, False))
	if kind == "window":
		c.rect (px + 8, py + 18, 30, 6, DIM, r = 1); c.rect (px + 44, py + 16, pw - 52, 10, WHITE, r = 2, outline = LINE)
		c.rect (px + 8, py + 34, 30, 6, DIM, r = 1); c.rect (px + 44, py + 32, pw - 52, 10, WHITE, r = 2, outline = LINE)
		c.rect (px + pw - 40, py + ph - 18, 32, 12, M.lighten (SEL, 0.4), r = 3)
	elif kind == "document":
		c.rect (px, py + 9, pw, 7, M.lighten (FACE, 0.4)); c.rect (px, py + 16, pw, 9, M.lighten (FACE, 0.2))
		for k in range (5): c.rect (px + 3 + k * 9, py + 17, 6, 6, SEL, r = 1)
		c.rect (px + 6, py + 28, pw - 12, ph - 40, WHITE)
		for k in range (3): c.rect (px + 10, py + 32 + k * 7, pw - 40 - k * 12, 3, LINE2)
		c.rect (px, py + ph - 8, pw, 8, M.lighten (FACE, 0.2))
	elif kind == "dialog":
		c.rect (px + 20, py + 18, pw - 40, 8, LINE2, r = 2); c.rect (px + 20, py + 32, pw - 60, 8, LINE2, r = 2)
		c.rect (px + pw - 80, py + ph - 18, 32, 12, WHITE, r = 3, outline = LINE); c.rect (px + pw - 44, py + ph - 18, 32, 12, M.lighten (SEL, 0.4), r = 3)
	elif kind == "list":
		c.rect (px + 6, py + 14, pw * 0.35, ph - 20, WHITE, outline = LINE)
		for k in range (4): c.rect (px + 9, py + 18 + k * 10, pw * 0.3, 6, M.SEL_SOFT if k == 1 else LINE2)
		c.rect (px + pw * 0.42, py + 14, pw * 0.55, ph - 20, WHITE, outline = LINE)
		for k in range (3): c.rect (px + pw * 0.46, py + 20 + k * 12, pw * 0.4, 6, LINE2)
	elif kind == "game":
		c.rect (px, py + 9, pw, ph - 9, (16, 18, 40))
		for k in range (6): c.rect (px + 10 + k * 16, py + 16, 13, 5, ((220, 80, 80), (230, 160, 60), (90, 180, 90), (80, 140, 220), (180, 100, 200), (230, 210, 90))[k])
		c.rect (px + pw / 2 - 12, py + ph - 8, 24, 3, WHITE); c.ellipse (px + pw / 2 + 10, py + ph - 20, 2.5, WHITE)
	elif kind == "console":
		c.rect (px, py + 9, pw, ph - 9, (30, 34, 40))
		for k, s in enumerate (("$ convert 20", "68 F", "$ _")): c.text (px + 6, py + 13 + k * 15, s, "tag", (200, 220, 200))
	c.text (x + 12, y + 88, title, "uib")
	yy = y + 108
	for s in desc: c.text (x + 12, yy, s, "small", DIM); yy += 15

def mock_new ():
	c, _ = screen ()
	cx, cy, cw, ch, y = ide (c, "Studio")
	body = cy + ch - 22 - y
	# the start page behind
	c.rect (cx, y, cw, body, M.lighten (FACE, 0.25))
	c.text (cx + 30, y + 26, "Studio", "huge", M.shade (FACE, 0.6))
	c.text (cx + 30, y + 70, "Recent projects", "smallb", DIM)
	for k, s in enumerate (("Converter  ·  SD:/projects/converter", "Notes  ·  SD:/projects/notes", "Snake  ·  SD:/projects/snake")):
		c.text (cx + 30, y + 92 + k * 20, s, "small", M.LINK)
	statusbar (c, cx, cy + ch - 22, cw, ["No project"], [""])
	# the dialog
	dw, dh = 760, 560
	dx, dy = (M.W - dw) / 2, 110
	ix, iy, iw, ih = M.window (c, dx, dy, dw, dh, "New project")
	c.text (ix + 18, iy + 14, "What will it be?", "h2")
	c.text (ix + 18, iy + 40, "Each makes a window (a .form), its code (a .bas), an icon and app.txt: it runs at once.", "small", DIM)
	cards = [("A window", ["Controls in a column:", "a converter, a form to fill"], "window"),
		 ("A document app", ["Menu, toolbar, status bar,", "New / Open / Save done"], "document"),
		 ("A dialog", ["A question and OK / Cancel,", "for a small tool"], "dialog"),
		 ("A list and its details", ["A list at the left, a record", "at the right (addresses...)"], "list"),
		 ("A game", ["SCREEN 13 full screen, the", "loop, the keys, the pads"], "game"),
		 ("A console tool", ["For the terminal: arguments,", "PRINT, pipes; no window"], "console")]
	cw2, ch2 = (iw - 36 - 2 * 14) / 3, 150
	for k, (t, d, kind) in enumerate (cards):
		template_card (c, ix + 18 + (k % 3) * (cw2 + 14), iy + 64 + (k // 3) * (ch2 + 12), cw2, ch2, t, d, kind, sel = k == 1)
	fy = iy + 64 + 2 * (ch2 + 12) + 6
	c.text_l (ix + 18, fy, 26, "Name", "ui"); M.field (c, ix + 110, fy, 260, 26, "Recipes", caret = True)
	c.text_l (ix + 390, fy, 26, "Title", "ui"); M.field (c, ix + 440, fy, iw - 458, 26, "Recipe Book")
	fy += 36
	c.text_l (ix + 18, fy, 26, "Folder", "ui"); M.field (c, ix + 110, fy, iw - 220, 26, "SD:/projects/recipes"); M.button (c, ix + iw - 100, fy, 82, 26, "Browse...")
	fy += 36
	c.text_l (ix + 18, fy, 26, "Category", "ui"); M.dropdown (c, ix + 110, fy, 200, 26, "Productivity")
	M.checkbox (c, ix + 330, fy + 5, "Compiled (main.bax)", True)
	M.button (c, ix + iw - 212, iy + ih - 44, 92, 30, "Cancel"); M.button (c, ix + iw - 110, iy + ih - 44, 92, 30, "Create", accent = True)
	c.save ("studio-new.png")

# ---- 5. the form as text, the code generated ----------------------------------------------------------------------
GEN = """' Main.form.bas -- made by Studio from Main.form: do not edit (it is made again)
DIM SHARED Main AS Window, celsius AS TextBox, fahrenheit AS TextBox
DIM SHARED live AS CheckBox, scale AS Slider, clear AS Button
DIM SHARED convert AS Button, status AS StatusBar

SUB Main_Create
  Main = NEW Window ("Temperature converter", 380, 260, 1)
  Main.MinSize 320, 220
  Main.Menu "File|&Copy the result\\tCtrl+C=mnuCopy|-|&Quit\\tCtrl+Q=mnuQuit"
  Main.Menu "Help|&About Converter=mnuAbout"
  celsius = NEW TextBox (Main, "20")
  fahrenheit = NEW TextBox (Main, ""): fahrenheit.ReadOnly = -1
  live = NEW CheckBox (Main, "Convert as I type", -1)
  scale = NEW Slider (Main, 200)
  clear = NEW Button (Main, "Clear"): convert = NEW Button (Main, "Convert")
  status = NEW StatusBar (Main, "Ready")
  Main_Layout Main.Width, Main.Height
END SUB

' the Column, its Rows: where each control goes for a size w x h
SUB Main_Layout (w, h)
  DIM x AS INTEGER, y AS INTEGER, inner AS INTEGER
  x = 14: y = 22 + 14: inner = w - 28
  celsius.Move x + 98, y, inner - 98, 26: y = y + 36
  fahrenheit.Move x + 98, y, inner - 98, 26: y = y + 36
  live.Move x, y, inner, 20: y = y + 30
  scale.Move x, y, inner, 22
  convert.Move x + inner - 96, h - 22 - 14 - 30, 96, 30
  clear.Move x + inner - 96 - 8 - 84, h - 22 - 14 - 30, 84, 30
END SUB

' the events -> your SUBs (Main.bas)
SUB Main_Run
  Main_Create: Main_Load
  DO
    SELECT CASE WAITEVENT
    CASE -1: EXIT DO
    CASE RESIZED: Main_Layout Main.Width, Main.Height
    CASE convert: convert_Click
    CASE clear: clear_Click
    CASE celsius: celsius_Change
    CASE MENUITEM ("mnuCopy"): mnuCopy_Click
    CASE MENUITEM ("mnuQuit"): mnuQuit_Click
    END SELECT
  LOOP
END SUB
"""

def mock_generated ():
	c, _ = screen ()
	cx, cy, cw, ch, y = ide (c)
	views (c, cx + cw - 8, cy + 6, "Code")
	body = cy + ch - 22 - y
	half = (cw - 1) // 2
	# left: the form, as text; right: what Studio makes of it
	ty = tabs (c, cx, y, half, [("Main.form", "form")], "Main.form")
	code_view (c, cx, ty, half, y + body - ty, FORM.rstrip ("\n").split ("\n"), highlight_form, cur = 13, scroll = (0.0, 1.0))
	c.vline (cx + half, y, y + body, M.shade (FACE, 0.75))
	tx = cx + half + 1
	ty2 = tabs (c, tx, y, cw - half - 1, [("Main.form.bas  (generated, read-only)", "gen")], "Main.form.bas  (generated, read-only)")
	code_view (c, tx, ty2, cw - half - 1, y + body - ty2, GEN.rstrip ("\n").split ("\n")[:38], highlight_bas, cur = 24, guides = False, scroll = (0.0, 0.75))
	c.rect (tx, ty2, cw - half - 1, y + body - ty2, M.A ((240, 236, 232), 70))
	# the arrows: a line of the form -> the code it becomes
	lh = 17
	ya, yb = ty + 4 + 12 * lh + lh / 2, ty2 + 4 + 23 * lh + lh / 2
	c.line ([(cx + half - 30, ya), (cx + half + 30, yb)], GUIDE_C, 2)
	c.ellipse (cx + half - 30, ya, 3.5, GUIDE_C); c.poly ([(cx + half + 30, yb), (cx + half + 21, yb - 7), (cx + half + 19, yb + 2)], GUIDE_C)
	statusbar (c, cx, cy + ch - 22, cw, ["Main.form → Main.form.bas: made again at each change of the form"], ["read-only", "Ln 24"])
	c.save ("studio-generated.png")

if __name__ == "__main__":
	which = sys.argv[1:]
	for name, f in (("designer", mock_designer), ("code", mock_code), ("debug", mock_debug), ("new", mock_new), ("generated", mock_generated)):
		if not which or name in which: f ()
