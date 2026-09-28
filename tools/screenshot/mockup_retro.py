#!/usr/bin/env python3
"""mockup_retro.py -- Onyx's classic desktop in three retro skins, to compare with the one it has
(screenshots/desktop.png): Amiga Workbench 2.x / 3.x, CDE (Motif), Windows 3.1 -- and a board of
their push buttons, their own and the user's framed one (drawn by the user: the raised button set
in a sunken 3D well; framed () draws it by code, the same pixels, in any size and colour).

    python3 tools/screenshot/mockup_retro.py      -> docs/gui-redesign/mockups/retro-*.png

The desktop of render.py's render_desktop (): the menu bar with a menu open, the fractal, the
terminal in front, the calculator, the Shelf, the panel. Only the skin changes -- the frames and
their gadgets, the menus, the buttons, the fields, the fonts, the colours:

  * Workbench: Workbench 2.0's palette (grey #AAA, black, white, blue #68B); the text in
    circle's 8 x 8 font (IBM-like, its stems already 2 px: close to the Amiga's Topaz) with each
    row doubled and the horizontal lines 2 px thick -- the Amiga's hires pixels were twice as
    tall as wide; the close gadget on the left, zoom and depth on the right.
  * CDE: the eight colours of CDE's Default.dp palette and Motif's 2-px bevels (the shades
    computed from each colour); the window menu, minimise and maximise buttons; the Front Panel,
    its switcher holding the Shelf's tabs.
  * Windows 3.1: the "Windows Default" colours (grey #C0C0C0, navy titles); the black-outlined
    push buttons with 2-px bevels; the control-menu box, the minimise / maximise arrows.

CDE's and Windows' text: Liberation Sans drawn without anti-aliasing (bitmap-like). The
calculators' keys wear the framed button in all three.
"""
import os, re, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render as R				# GLYPH (the 8 x 16 font), SD

ROOT = R.ROOT
OUT = os.path.join(ROOT, "docs", "gui-redesign", "mockups")
W, H = 1024, 768
MAGENTA = (255, 0, 255)
LIB = "/usr/share/fonts/truetype/liberation/"

# ---- fonts ---------------------------------------------------------------------------------------------
def load_font8x8 ():
	txt = open (os.path.join (ROOT, "circle", "lib", "font8x8.cpp")).read ()
	body = re.search (r"font_data\[\]\s*=\s*\{(.*?)\};", txt, re.S).group (1)
	body = re.sub (r"//[^\n]*", "", body)
	vals = [int (b, 16) for b in re.findall (r"0x[0-9A-Fa-f]{2}", body)]
	return {0x21 + i: vals[i * 8:i * 8 + 8] for i in range (len (vals) // 8)}

class BFont:
	"""A bitmap font: rows of 8 bits (MSB left), each row drawn ys times."""
	def __init__ (self, glyphs, rows, ys = 1, adv = 8):
		self.g, self.rows, self.ys, self.adv = glyphs, rows, ys, adv
		self.h = rows * ys
	def width (self, s): return len (s) * self.adv
	def draw (self, img, x, y, s, c):
		px = img.load (); iw, ih = img.size
		for i, ch in enumerate (s):
			g = self.g.get (ord (ch))
			if not g: continue
			for r in range (self.rows):
				bits = g[r]
				if not bits: continue
				for k in range (self.ys):
					yy = y + r * self.ys + k
					if not 0 <= yy < ih: continue
					for col in range (8):
						if bits & (0x80 >> col):
							xx = x + i * self.adv + col
							if 0 <= xx < iw: px[xx, yy] = c
	def draw_c (self, img, x, y, w, h, s, c):	# centred in the box
		self.draw (img, x + (w - self.width (s)) // 2, y + (h - self.h) // 2, s, c)

class TFont:
	"""A TrueType face drawn without anti-aliasing: a bitmap font's look."""
	def __init__ (self, name, size):
		self.f = ImageFont.truetype (LIB + name, size)
		b = self.f.getbbox ("Hg")
		self.top, self.h = b[1], b[3] - b[1]		# the ink box of a line (cap top .. descender)
	def width (self, s): return int (round (self.f.getlength (s)))
	def draw (self, img, x, y, s, c):		# y: the ink's top
		d = ImageDraw.Draw (img); d.fontmode = "1"
		d.text ((x, y - self.top), s, font = self.f, fill = c)
	def draw_c (self, img, x, y, w, h, s, c):
		self.draw (img, x + (w - self.width (s)) // 2, y + (h - self.h) // 2, s, c)

FONT8 = load_font8x8 ()
VGA = BFont (R.GLYPH, 16)				# Onyx's 8 x 16 (the terminals)
TOPAZ = BFont (FONT8, 8, ys = 2)			# the Amiga's look: 8 x 8, rows doubled

# ---- drawing -------------------------------------------------------------------------------------------
def fill (d, x, y, w, h, c):
	if w > 0 and h > 0: d.rectangle ([x, y, x + w - 1, y + h - 1], fill = c)
def frame (d, x, y, w, h, c): d.rectangle ([x, y, x + w - 1, y + h - 1], outline = c)
def bevel (d, x, y, w, h, tl, br, tx = 1, ty = 1):
	"""A 3D edge: the top and left in tl, the bottom and right in br (tx px wide, ty px tall)."""
	fill (d, x, y, w, ty, tl); fill (d, x, y, tx, h, tl)
	fill (d, x, y + h - ty, w, ty, br); fill (d, x + w - tx, y, tx, h, br)
def mbevel (d, x, y, w, h, light, dark, t, raised = True):
	"""A 3D edge t px thick, mitred: raised = light on the top and left, dark on the bottom and
	right (sunken: the other way round); where the two meet (top right, bottom left) the light
	one wins -- as in the user's drawing."""
	for i in range (t):
		if raised:
			fill (d, x, y + i, w - i, 1, light); fill (d, x + i, y, 1, h - i, light)
			fill (d, x + w - 1 - i, y + i + 1, 1, h - i - 1, dark); fill (d, x + i + 1, y + h - 1 - i, w - i - 1, 1, dark)
		else:
			fill (d, x + w - 1 - i, y + i, 1, h - i, light); fill (d, x + i, y + h - 1 - i, w - i, 1, light)
			fill (d, x, y + i, w - i - 1, 1, dark); fill (d, x + i, y, 1, h - i - 1, dark)

def framed (d, x, y, w, h, face, light, dark, pressed = False, bt = 2):
	"""The user's button, drawn without a skin: a raised 2-px frame, 2 px of face, a sunken 2-px
	well, and in it the button -- raised by bt px (1: the first drawing, 2: the "more marked"
	one), or flush with the well when pressed. Identical to the user's drawings (the 2-px one:
	but 4 pixels of a corner's diagonal, where the first ones let the light win). Returns the
	button's box (the label's)."""
	fill (d, x, y, w, h, face)
	mbevel (d, x, y, w, h, light, dark, 2, True)
	mbevel (d, x + 4, y + 4, w - 8, h - 8, light, dark, 2, False)
	if not pressed: mbevel (d, x + 6, y + 6, w - 12, h - 12, light, dark, bt, True)
	return x + 6, y + 6, w - 12, h - 12

def framed_label (img, d, font, box, label, fg, state, light, dark):
	"""The label in the button's box: 1 px lower right when pressed; disabled, embossed (the
	dark ink over a light copy); the default button, a dotted rectangle round it."""
	x, y, w, h = box
	if state == "pressed": x += 1; y += 1
	if state == "disabled":
		font.draw_c (img, x + 1, y + 1, w, h, label, light); font.draw_c (img, x, y, w, h, label, dark)
		return
	font.draw_c (img, x, y, w, h, label, fg)
	if state == "default":
		lw, lh = font.width (label) + 10, font.h + 8; lx, ly = x + (w - lw) // 2, y + (h - lh) // 2
		for i in range (0, lw, 2): fill (d, lx + i, ly, 1, 1, fg); fill (d, lx + i, ly + lh - 1, 1, 1, fg)
		for j in range (0, lh, 2): fill (d, lx, ly + j, 1, 1, fg); fill (d, lx + lw - 1, ly + j, 1, 1, fg)
def framed_colours (face): return lighten (face, 40 / 70), face, shade (face, 127 / 185)	# (225 185 127)

def tri (d, cx, cy, s, up, c):				# a filled triangle, pointing up or down
	if up: d.polygon ([(cx - s, cy + s // 2), (cx + s, cy + s // 2), (cx, cy - s // 2 - 1)], fill = c)
	else: d.polygon ([(cx - s, cy - s // 2), (cx + s, cy - s // 2), (cx, cy + s // 2 + 1)], fill = c)
def shade (c, k): return tuple (max (0, min (255, int (v * k))) for v in c)
def lighten (c, k): return tuple (max (0, min (255, int (v + (255 - v) * k))) for v in c)
def bright (c): return (0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]) / 255

def icon (img, name, x, y, bg = None):
	"""An app's icon (apps/<name>.app/icon.bmp, magenta = see-through), or None."""
	p = os.path.join (R.SD, "apps", name + ".app", "icon.bmp")
	if not os.path.exists (p): return None
	im = Image.open (p).convert ("RGB"); iw, ih = im.size; s = im.load (); px = img.load ()
	for j in range (ih):
		for i in range (iw):
			c = s[i, j]
			if c != MAGENTA and 0 <= x + i < W and 0 <= y + j < H: px[x + i, y + j] = c
	return iw

def speaker (d, x, y, c):
	fill (d, x, y + 4, 3, 4, c)
	for k in range (4): fill (d, x + 3 + k, y + 3 - k, 1, 6 + 2 * k, c)
	for w in range (3):
		r = 3 + 3 * w
		for dy in range (-r, r + 1):
			for dx in range (1, r + 1):
				d2 = dx * dx + dy * dy
				if r * r - r <= d2 <= r * r + r and dx * 3 >= abs (dy) * 2: fill (d, x + 7 + dx, y + 6 + dy, 1, 1, c)
def wifi (d, x, y, c):
	cx, by = x + 8, y + 11
	for dy in range (-11, 1):
		for dx in range (-8, 9):
			d2 = dx * dx + dy * dy
			if abs (dx) > -dy + 1: continue
			if d2 <= 2 or 12 <= d2 <= 24 or 42 <= d2 <= 62 or 90 <= d2 <= 120: fill (d, cx + dx, by + dy, 1, 1, c)

def mandel (w, h):
	cxr, cyr, span = -0.5, 0.0, 3.0
	spanY = span * h / w; x0 = cxr - span / 2; y0 = cyr - spanY / 2
	cr = (x0 + np.arange (w) * span / w)[None, :].repeat (h, 0)
	ci = (y0 + np.arange (h) * spanY / h)[:, None].repeat (w, 1)
	zr = np.zeros_like (cr); zi = np.zeros_like (ci); it = np.zeros ((h, w), int); alive = np.ones ((h, w), bool)
	for _ in range (96):
		zr2 = zr * zr; zi2 = zi * zi; alive &= ~((zr2 + zi2) > 4)
		nzi = 2 * zr * zi + ci; nzr = zr2 - zi2 + cr
		zr = np.where (alive, nzr, zr); zi = np.where (alive, nzi, zi); it += alive
	r = np.where (it >= 96, 0, (it * 8) & 255); g = np.where (it >= 96, 0, (it * 5 + 40) & 255)
	b = np.where (it >= 96, 0, (it * 11 + 80) & 255)
	return Image.fromarray (np.dstack ([r, g, b]).astype ("uint8"), "RGB")
MANDEL = mandel (340, 240)

TERM = ["Onyx terminal -- try: ls /bin | grep e", "$ ls /bin | grep e", "echo", "sleep", "yes",
	"$ ps", "  1 k R  idle", "  2 k R  gui", " 14 a R  panel", " 17 a R  voronoy", " 21 a R  terminal",
	" 25 a R  fractal", "$ echo onyx | wc -c", "5"]
KEYS = ["sin", "cos", "tan", "ln", "log", "sqrt", "x^2", "x^y", "1/x", "e^x", "7", "8", "9", "/", "C",
	"4", "5", "6", "*", "+/-", "1", "2", "3", "-", "%", "0", ".", "pi", "=", "+"]
MENU = [("New Window", "N"), ("Open Script...", "O"), None, ("Close Window", "W"), ("Quit", "Q")]
SHELF_ITEMS = [("Projects", "folder"), ("notes.txt", "text"), ("autostart", "text"), ("sunset.bmp", "image"),
	("Text Editor", "tinypad"), ("File Viewer", "fileviewer"), ("Paint", "paint")]
PANEL_APPS = [("terminal", True), ("fileviewer", False), ("tinypad", False), ("tinycalc", True), ("mandelbrot", True)]

# ======================================================================================================
class Theme:
	"""What the three skins share: the desktop's layout and the apps' content, drawn with the
	skin's own frame, buttons, fields, fonts and colours."""
	SHELF_H = 100
	def desktop (self):
		img = Image.new ("RGB", (W, H)); d = ImageDraw.Draw (img)
		self.backdrop (img, d)
		self.shelf (img, d)
		fx, fy = self.window (img, d, 63, self.MB + 8, 340, 256, "fractal", False)
		self.fractal (img, d, fx, fy)
		cx, cy = self.window (img, d, 63, 328, 280, 296, "tinycalc", False)
		self.calc (img, d, cx, cy)
		tx, ty = self.window (img, d, 293, 108, 620, 400, "terminal", True, scroll = True)
		self.terminal (img, d, tx, ty, 620, 400)
		self.panel (img, d)
		px, py = self.menubar (img, d, "Terminal", ["File", "Edit", "View"], 1, MENU, 1)
		self.cursor (img, d, px, py)
		return img

	def fractal (self, img, d, x, y):
		img.paste (MANDEL, (x, y))
		fill (d, x, y + 240, 340, 16, self.STATUS_BG)
		self.ui.draw_c (img, x + 4, y + 240, 0, 16, "", 0)
		self.small.draw (img, x + 6, y + 240 + (16 - self.small.h) // 2, "click:in  o:out  r:reset", self.STATUS_FG)
		self.combo (img, d, x + 4, y + 4, 140, self.COMBO_H, "Mandelbrot")

	def calc (self, img, d, x, y):
		fill (d, x, y, 280, 296, self.APP_BG)
		self.field (img, d, x + 8, y + 8, 280 - 72, 36, "3.14159265")
		self.button (img, d, x + 280 - 58, y + 8, 50, 36, "RAD", embedded = True)
		for r in range (6):
			for c in range (5):
				i = r * 5 + c
				self.button (img, d, x + 8 + c * 54, y + 52 + r * 40, 50, 38, KEYS[i],
					     "pressed" if KEYS[i] == "=" else "normal", embedded = True)

# ======================================================================================================
class Workbench (Theme):
	name = "workbench"
	G, K, WH, B = (170, 170, 170), (0, 0, 0), (255, 255, 255), (102, 136, 187)	# Workbench 2.0's pens 0..3
	MB = 22
	COMBO_H = 24
	def __init__ (self):
		self.ui = self.small = self.term = TOPAZ
		self.APP_BG = self.STATUS_BG = self.G; self.STATUS_FG = self.K
	def bev (self, d, x, y, w, h, up = True):		# 1-px sides, 2-px top and bottom (hires)
		bevel (d, x, y, w, h, self.WH if up else self.K, self.K if up else self.WH, 1, 2)

	def backdrop (self, img, d): fill (d, 0, 0, W, H, self.G)

	def gadget (self, d, x, y, w, h, col, kind):
		fill (d, x, y, w, h, col); self.bev (d, x, y, w, h)
		cx, cy = x + w // 2, y + h // 2
		if kind == "close":
			fill (d, cx - 4, cy - 4, 8, 8, self.K); fill (d, cx - 3, cy - 2, 6, 4, self.WH)
		elif kind == "depth":
			frame (d, cx - 7, cy - 7, 10, 10, self.K); frame (d, cx - 7, cy - 6, 10, 8, self.K)
			fill (d, cx - 2, cy - 2, 10, 10, self.WH); frame (d, cx - 2, cy - 2, 10, 10, self.K); frame (d, cx - 2, cy - 1, 10, 8, self.K)
		elif kind == "zoom":
			frame (d, cx - 7, cy - 6, 14, 12, self.K); fill (d, cx - 6, cy - 5, 6, 5, self.WH); frame (d, cx - 7, cy - 6, 8, 7, self.K)
		elif kind == "size":
			frame (d, cx - 6, cy - 6, 12, 12, self.K); fill (d, cx - 1, cy - 1, 6, 6, self.K)
		elif kind in ("up", "down"):
			tri (d, cx, cy, 5, kind == "up", self.K)

	def window (self, img, d, x, y, cw, ch, title, active, scroll = False):
		L, T, R_, B_ = 4, 22, (18 if scroll else 4), (20 if scroll else 4)
		w, h = cw + L + R_, ch + T + B_
		col = self.B if active else self.G
		fill (d, x, y, w, h, col); self.bev (d, x, y, w, h)
		self.bev (d, x + L - 1, y + T - 2, cw + 2, ch + 4, False)		# the content: recessed
		self.gadget (d, x, y, 20, T, col, "close")
		self.gadget (d, x + w - 24, y, 24, T, col, "depth")
		self.gadget (d, x + w - 48, y, 24, T, col, "zoom")
		self.ui.draw (img, x + 26, y + (T - 16) // 2 + 1, title, self.K)
		if scroll:							# the right border's scroller
			sx = x + w - R_ + 1
			self.gadget (d, sx - 1, y + h - B_ - 36, R_, 18, col, "up")
			self.gadget (d, sx - 1, y + h - B_ - 18, R_, 18, col, "down")
			self.gadget (d, sx - 1, y + h - B_, R_, B_, col, "size")
			ty0, ty1 = y + T + 2, y + h - B_ - 38
			self.bev (d, sx + 2, ty0, R_ - 6, ty1 - ty0, False)
			fill (d, sx + 4, ty0 + 3 + (ty1 - ty0) // 3, R_ - 10, (ty1 - ty0) // 2, col)
			self.bev (d, sx + 4, ty0 + 3 + (ty1 - ty0) // 3, R_ - 10, (ty1 - ty0) // 2)
		return x + L, y + T

	def button (self, img, d, x, y, w, h, label, state = "normal", embedded = False):
		if embedded:							# the user's framed button, in the 4 pens
			box = framed (d, x, y, w, h, self.G, self.WH, self.K, state == "pressed")
			if state == "disabled": self.ui.draw_c (img, *box, label, self.K); self.ghost (d, *box)
			else: framed_label (img, d, self.ui, box, label, self.K, state, self.WH, self.K)
			return
		pressed = state == "pressed"
		fill (d, x, y, w, h, self.B if pressed else self.G); self.bev (d, x, y, w, h, not pressed)
		self.ui.draw_c (img, x, y, w, h, label, self.K)
		if state == "default": frame (d, x - 1, y - 1, w + 2, h + 2, self.K)
		if state == "disabled": self.ghost (d, x, y, w, h)
	def ghost (self, d, x, y, w, h):				# disabled: a sparse grid of black dots
		for j in range (y + 2, y + h - 2, 2):
			for i in range (x + 1 + (j // 2) % 2 * 2, x + w - 1, 4): fill (d, i, j, 1, 1, self.K)

	def field (self, img, d, x, y, w, h, text):			# the "ridge" of a string gadget
		fill (d, x, y, w, h, self.G); self.bev (d, x, y, w, h); self.bev (d, x + 1, y + 2, w - 2, h - 4, False)
		self.ui.draw (img, x + w - 8 - self.ui.width (text), y + (h - 16) // 2, text, self.K)

	def combo (self, img, d, x, y, w, h, label):			# a cycle gadget
		fill (d, x, y, w, h, self.G); self.bev (d, x, y, w, h)
		cx, cy = x + 11, y + h // 2					# the cycle mark: a loop and its arrow head
		d.arc ([cx - 6, cy - 7, cx + 6, cy + 7], 60, 330, fill = self.K, width = 2)
		d.polygon ([(cx + 2, cy - 9), (cx + 8, cy - 5), (cx + 2, cy - 2)], fill = self.K)
		fill (d, x + 22, y + 3, 1, h - 6, self.K); fill (d, x + 23, y + 3, 1, h - 6, self.WH)
		self.ui.draw (img, x + 30, y + (h - 16) // 2, label, self.K)

	def terminal (self, img, d, x, y, w, h):			# the AmigaShell's look: black on grey
		fill (d, x, y, w, h, self.G)
		for i, s in enumerate (TERM): self.term.draw (img, x + 4, y + 4 + i * 18, s, self.K)
		iy = y + 4 + len (TERM) * 18
		s = "$ cat readme.txt | grep -i pi"; self.term.draw (img, x + 4, iy, s, self.K)
		fill (d, x + 4 + self.term.width (s), iy, 8, 16, self.B)

	def menubar (self, img, d, app, menus, open_idx, items, hover):
		fill (d, 0, 0, W, self.MB, self.WH); fill (d, 0, self.MB - 2, W, 2, self.K)
		x = 6; xs = []
		for i, t in enumerate ([app] + menus):
			tw = self.ui.width (t) + 16; xs.append (x)
			if i == open_idx: fill (d, x, 0, tw, self.MB - 2, self.K)
			self.ui.draw (img, x + 8, 2, t, self.WH if i == open_idx else self.K)
			x += tw
		self.gadget (d, W - 24, 0, 24, self.MB - 2, self.WH, "depth")	# the screen's depth gadget
		self.ui.draw (img, W - 24 - 8 - 40, 2, "12:34", self.K)
		wifi (d, W - 24 - 8 - 40 - 28, 4, self.K); speaker (d, W - 24 - 8 - 40 - 54, 4, self.K)
		mx, my = xs[open_idx], self.MB
		mw = max (self.ui.width (l) for l, k in [i for i in items if i]) + 16 + 40
		mh = 4 + sum (8 if i is None else 20 for i in items)
		fill (d, mx, my, mw, mh, self.WH); self.bev (d, mx, my, mw, mh, False)
		frame (d, mx, my, mw, mh, self.K); fill (d, mx, my + mh - 2, mw, 2, self.K); fill (d, mx, my, mw, 2, self.K)
		yy = my + 2; hp = (mx + 40, my + 12)
		for n, it in enumerate (items):
			if it is None:
				for i in range (mx + 4, mx + mw - 4, 2): fill (d, i, yy + 3, 1, 2, self.K)
				yy += 8; continue
			fg = self.K
			if n == hover: fill (d, mx + 2, yy, mw - 4, 20, self.K); fg = self.WH; hp = (mx + 60, yy + 10)
			self.ui.draw (img, mx + 8, yy + 2, it[0], fg)
			kx = mx + mw - 30						# the right Amiga key + the letter
			frame (d, kx, yy + 2, 11, 16, fg); fill (d, kx, yy + 2, 11, 2, fg); fill (d, kx, yy + 16, 11, 2, fg)
			d.line ([kx + 2, yy + 15, kx + 5, yy + 5], fill = fg); d.line ([kx + 5, yy + 5, kx + 8, yy + 15], fill = fg)
			fill (d, kx + 3, yy + 11, 5, 1, fg)
			self.ui.draw (img, kx + 13, yy + 2, it[1], fg)
			yy += 20
		return hp

	def wb_icon (self, img, d, kind, x, y):			# 4-colour Workbench icons
		if kind == "folder":						# a drawer: its front, its handle
			fill (d, x + 2, y + 6, 36, 30, self.K); fill (d, x + 3, y + 8, 34, 26, self.B)
			fill (d, x + 6, y + 12, 28, 18, self.G); self.bev (d, x + 6, y + 12, 28, 18)
			fill (d, x + 15, y + 18, 10, 4, self.K); fill (d, x + 16, y + 18, 8, 2, self.WH)
		elif kind == "text":						# a page, its corner turned
			d.polygon ([(x + 8, y + 2), (x + 26, y + 2), (x + 33, y + 9), (x + 33, y + 37), (x + 8, y + 37)], fill = self.WH, outline = self.K)
			d.polygon ([(x + 26, y + 2), (x + 26, y + 9), (x + 33, y + 9)], fill = self.G, outline = self.K)
			for l in range (5): fill (d, x + 12, y + 13 + l * 5, 17 if l % 2 == 0 else 12, 2, self.B)
		elif kind == "image":
			fill (d, x + 3, y + 5, 34, 30, self.K); fill (d, x + 5, y + 7, 30, 26, self.WH)
			fill (d, x + 7, y + 9, 26, 22, self.B); d.polygon ([(x + 7, y + 30), (x + 17, y + 18), (x + 25, y + 27), (x + 33, y + 22), (x + 33, y + 30)], fill = self.G)
			fill (d, x + 25, y + 12, 5, 4, self.WH)
		else: icon (img, kind, x, y)

	def shelf (self, img, d):
		y0 = H - self.SHELF_H
		fill (d, 0, y0, W, self.SHELF_H, self.G); self.bev (d, 0, y0, W, self.SHELF_H)
		x = 6
		for i, t in enumerate (["Shelf", "Documents", "Apps", "+"]):
			tw = self.ui.width (t) + 16
			self.button (img, d, x, y0 + 4, tw, 20, t, "pressed" if i == 0 else "normal"); x += tw + 2
		for i, (lab, kind) in enumerate (SHELF_ITEMS):
			cx = 8 + i * 100
			self.wb_icon (img, d, kind, cx + 30, y0 + 28)
			self.ui.draw (img, cx + (100 - self.ui.width (lab)) // 2, y0 + 72, lab, self.K)
		tx = W - 90
		fill (d, tx, y0 + 28, 1, 60, self.K); fill (d, tx + 1, y0 + 28, 1, 60, self.WH)
		gx, gy = tx + 25, y0 + 28						# the Trash can
		fill (d, gx + 8, gy + 4, 24, 4, self.K); fill (d, gx + 15, gy + 1, 10, 3, self.K)
		d.polygon ([(gx + 10, gy + 10), (gx + 30, gy + 10), (gx + 28, gy + 38), (gx + 12, gy + 38)], fill = self.WH, outline = self.K)
		for l in range (3): fill (d, gx + 15 + l * 5, gy + 14, 1, 20, self.K)
		self.ui.draw (img, tx + (90 - 40) // 2, y0 + 72, "Trash", self.K)

	def panel (self, img, d):					# a dock window: a drag bar, the icons' buttons
		bw, bh = 60, 330; bx, by = W - bw - 2, (H - bh) // 2
		fill (d, bx, by, bw, bh, self.G); self.bev (d, bx, by, bw, bh)
		fill (d, bx + 3, by + 4, bw - 6, 10, self.B); self.bev (d, bx + 3, by + 4, bw - 6, 10, False)
		yy = by + 18
		for i, (name, running) in enumerate ([("panel", False)] + PANEL_APPS):
			self.button (img, d, bx + 4, yy, bw - 8, 44, "")
			if name == "panel":
				for r in range (3):
					for c in range (3): fill (d, bx + 17 + c * 10, yy + 9 + r * 10, 6, 6, self.K)
			else: icon (img, name, bx + 10, yy + 2)
			if running: d.polygon ([(bx + 7, yy + 34), (bx + 7, yy + 40), (bx + 13, yy + 40)], fill = self.K)
			yy += 46
		self.ui.draw (img, bx + (bw - 40) // 2, by + bh - 22, "13:37", self.K)

	def cursor (self, img, d, x, y):				# the Amiga's red pointer
		pts = [(0, 0), (0, 14), (4, 11), (7, 17), (10, 16), (7, 10), (12, 10)]
		d.polygon ([(x + a, y + b) for a, b in pts], fill = (220, 40, 30), outline = self.K)
		d.line ([x + 1, y + 2, x + 1, y + 11], fill = (255, 200, 160))

# ======================================================================================================
class CDE (Theme):
	name = "cde"
	P = [(237, 168, 112), (153, 153, 153), (137, 152, 170), (104, 111, 130),	# CDE's Default.dp: active frame,
	     (198, 178, 168), (73, 146, 167), (183, 135, 141), (147, 171, 191)]	# inactive, backdrop / One, text,
	MB = 28									# apps, menus / Three, Four, panel / Two
	COMBO_H = 26
	SHELF_H = 90
	def __init__ (self):
		self.ui = TFont ("LiberationSans-Regular.ttf", 13); self.bold = TFont ("LiberationSans-Bold.ttf", 13)
		self.small = TFont ("LiberationSans-Regular.ttf", 12); self.term = TFont ("LiberationMono-Regular.ttf", 14)
		self.APP_BG = self.P[4]; self.STATUS_BG = self.P[4]; self.STATUS_FG = self.fg (self.P[4])
	@staticmethod
	def sh (c): return lighten (c, 0.5), shade (c, 0.5), shade (c, 0.82)	# Motif: top, bottom shadow, select
	@staticmethod
	def fg (c): return (0, 0, 0) if bright (c) > 0.55 else (255, 255, 255)
	def bev (self, d, x, y, w, h, c, up = True, t = 2):
		top, bot, _ = self.sh (c); bevel (d, x, y, w, h, top if up else bot, bot if up else top, t, t)

	def backdrop (self, img, d):					# a pebbled backdrop in the palette's colour
		rng = np.random.default_rng (7)
		n = rng.random ((H // 2 + 8, W // 2 + 8))
		k = np.ones ((5, 5)) / 25
		from numpy.lib.stride_tricks import sliding_window_view as sw
		m = (sw (n, (5, 5)) * k).sum (axis = (2, 3))[:H // 2, :W // 2]
		c = np.array (self.P[2], float); t, b, _ = self.sh (self.P[2])
		out = np.where ((m > 0.56)[..., None], np.array (lighten (self.P[2], 0.18), float),
				np.where ((m < 0.44)[..., None], np.array (shade (self.P[2], 0.9), float), c))
		img.paste (Image.fromarray (np.kron (out, np.ones ((2, 2, 1))).astype ("uint8"), "RGB"), (0, 0))

	def window (self, img, d, x, y, cw, ch, title, active, scroll = False):
		BW, TH = 5, 22
		w, h = cw + 2 * BW, ch + 2 * BW + TH
		c = self.P[0] if active else self.P[1]; top, bot, _ = self.sh (c)
		fill (d, x, y, w, h, c); self.bev (d, x, y, w, h, c, True, 1); self.bev (d, x + BW - 1, y + BW - 1, w - 2 * BW + 2, h - 2 * BW + 2, c, False, 1)
		for a in (BW + TH, ):							# the corners' grooves
			for gx in (x + a, x + w - a - 2):
				fill (d, gx, y, 1, BW, bot); fill (d, gx + 1, y, 1, BW, top); fill (d, gx, y + h - BW, 1, BW, bot); fill (d, gx + 1, y + h - BW, 1, BW, top)
			for gy in (y + a, y + h - a - 2):
				fill (d, x, gy, BW, 1, bot); fill (d, x, gy + 1, BW, 1, top); fill (d, x + w - BW, gy, BW, 1, bot); fill (d, x + w - BW, gy + 1, BW, 1, top)
		ty = y + BW
		parts = [(x + BW, TH), (x + BW + TH, w - 2 * BW - 3 * TH), (x + w - BW - 2 * TH, TH), (x + w - BW - TH, TH)]
		for px, pw in parts: fill (d, px, ty, pw, TH, c); self.bev (d, px, ty, pw, TH, c, True, 1)
		mx = parts[0][0]; fill (d, mx + 5, ty + 9, 12, 4, c); self.bev (d, mx + 5, ty + 9, 12, 4, c, True, 1)	# window menu
		nx = parts[2][0]; fill (d, nx + 9, ty + 9, 4, 4, c); self.bev (d, nx + 9, ty + 9, 4, 4, c, True, 1)	# minimise
		xx = parts[3][0]; fill (d, xx + 5, ty + 5, 12, 12, c); self.bev (d, xx + 5, ty + 5, 12, 12, c, True, 1)	# maximise
		self.ui.draw_c (img, parts[1][0], ty, parts[1][1], TH, title, self.fg (c))
		return x + BW, y + BW + TH

	def button (self, img, d, x, y, w, h, label, state = "normal", embedded = False, bg = None):
		bg = bg or self.P[4]; top, bot, sel = self.sh (bg)
		if embedded:							# the user's framed button, in Motif's shades
			box = framed (d, x, y, w, h, bg, top, bot, state == "pressed")
			framed_label (img, d, self.ui, box, label, self.fg (bg), state, top, bot)
			return
		if state == "default":						# Motif's default-button frame: sunken, a gap
			self.bev (d, x, y, w, h, bg, False, 2); x, y, w, h = x + 4, y + 4, w - 8, h - 8
		pressed = state == "pressed"
		fill (d, x, y, w, h, sel if pressed else bg); self.bev (d, x, y, w, h, bg, not pressed, 2)
		f = self.ui
		f.draw_c (img, x + (1 if pressed else 0), y + (1 if pressed else 0), w, h, label, self.fg (bg))
		if state == "disabled":						# stippled: every other pixel the face again
			px = img.load ()
			for j in range (y + 2, y + h - 2):
				for i in range (x + 2 + j % 2, x + w - 2, 2): px[i, j] = bg

	def field (self, img, d, x, y, w, h, text):
		c = self.P[3]; fill (d, x, y, w, h, c); self.bev (d, x, y, w, h, self.P[4], False, 2)
		self.term.draw (img, x + w - 8 - self.term.width (text), y + (h - self.term.h) // 2, text, self.fg (c))

	def combo (self, img, d, x, y, w, h, label):			# an option menu
		c = self.P[4]; fill (d, x, y, w, h, c); self.bev (d, x, y, w, h, c, True, 2)
		self.ui.draw (img, x + 8, y + (h - self.ui.h) // 2, label, self.fg (c))
		fill (d, x + w - 20, y + h // 2 - 3, 12, 6, c); self.bev (d, x + w - 20, y + h // 2 - 3, 12, 6, c, True, 1)

	def terminal (self, img, d, x, y, w, h):			# dtterm: the palette's pane colour, a scroll bar
		c = self.P[5]; fill (d, x, y, w, h, c); fg = self.fg (c)
		lh = self.term.h + 7
		for i, s in enumerate (TERM): self.term.draw (img, x + 6, y + 6 + i * lh, s, fg)
		iy = y + 6 + len (TERM) * lh; s = "$ cat readme.txt | grep -i pi"; self.term.draw (img, x + 6, iy, s, fg)
		fill (d, x + 6 + self.term.width (s) + 1, iy - 2, 8, self.term.h + 4, fg)
		sx = x + w - 18; top, bot, sel = self.sh (c)
		fill (d, sx, y, 18, h, sel); self.bev (d, sx, y, 18, h, c, False, 2)
		for up, ay in ((True, y + 2), (False, y + h - 18)):
			d.polygon ([(sx + 3, ay + 13), (sx + 14, ay + 13), (sx + 8, ay + 3)] if up else [(sx + 3, ay + 3), (sx + 14, ay + 3), (sx + 8, ay + 13)], fill = c, outline = bot)
			d.line ([sx + 3, ay + 13, sx + 8, ay + 3] if up else [sx + 3, ay + 3, sx + 14, ay + 3], fill = top)
		fill (d, sx + 2, y + h // 2, 14, 120, c); self.bev (d, sx + 2, y + h // 2, 14, 120, c, True, 2)

	def menubar (self, img, d, app, menus, open_idx, items, hover):
		c = self.P[4]; fill (d, 0, 0, W, self.MB, c); self.bev (d, 0, 0, W, self.MB, c, True, 2); fg = self.fg (c)
		x = 6; xs = []
		for i, t in enumerate ([app] + menus):
			f = self.bold if i == 0 else self.ui; tw = f.width (t) + 18; xs.append (x)
			if i == open_idx: self.bev (d, x, 3, tw, self.MB - 6, c, True, 2)
			ty = (self.MB - f.h) // 2; f.draw (img, x + 9, ty, t, fg)
			fill (d, x + 9, ty + f.h - 1, f.width (t[0]), 1, fg)				# the mnemonic
			x += tw
		self.ui.draw (img, W - 52, (self.MB - self.ui.h) // 2, "12:34", fg)
		wifi (d, W - 82, 8, fg); speaker (d, W - 108, 8, fg)
		m = self.P[5]; mfg = self.fg (m); mtop, mbot, _ = self.sh (m)
		mx, my = xs[open_idx], self.MB - 2
		mw = max (self.ui.width (l) + self.ui.width ("Ctrl+" + k) for l, k in [i for i in items if i]) + 56
		mh = 6 + sum (8 if i is None else 24 for i in items)
		fill (d, mx, my, mw, mh, m); self.bev (d, mx, my, mw, mh, m, True, 2)
		yy = my + 3; hp = (mx + 40, my + 12)
		for n, it in enumerate (items):
			if it is None: fill (d, mx + 4, yy + 3, mw - 8, 1, mbot); fill (d, mx + 4, yy + 4, mw - 8, 1, mtop); yy += 8; continue
			if n == hover: self.bev (d, mx + 3, yy, mw - 6, 24, m, True, 2); hp = (mx + 70, yy + 12)
			ty = yy + (24 - self.ui.h) // 2
			self.ui.draw (img, mx + 12, ty, it[0], mfg); fill (d, mx + 12, ty + self.ui.h - 1, self.ui.width (it[0][0]), 1, mfg)
			k = "Ctrl+" + it[1]; self.ui.draw (img, mx + mw - 12 - self.ui.width (k), ty, k, mfg)
			yy += 24
		return hp

	def fp_control (self, img, d, x, y, w, h, kind, c):		# a Front Panel control
		if kind == "clock":
			cx, cy = x + w // 2, y + h // 2; d.ellipse ([cx - 18, cy - 18, cx + 18, cy + 18], fill = lighten (c, 0.6), outline = shade (c, 0.4))
			for a in range (12):
				import math
				t = a * math.pi / 6; d.point ((cx + 15 * math.sin (t), cy - 15 * math.cos (t)), fill = (0, 0, 0))
			d.line ([cx, cy, cx + 8, cy - 6], fill = (0, 0, 0), width = 2); d.line ([cx, cy, cx - 2, cy - 14], fill = (0, 0, 0), width = 1)
		elif kind == "calendar":
			fill (d, x + 14, y + 10, 36, 40, (255, 255, 255)); frame (d, x + 14, y + 10, 36, 40, (0, 0, 0))
			fill (d, x + 15, y + 11, 34, 11, (190, 40, 40)); self.small.draw_c (img, x + 14, y + 11, 36, 11, "SEP", (255, 255, 255))
			self.bold.draw_c (img, x + 14, y + 24, 36, 24, "28", (0, 0, 0))
		elif kind == "folder":
			fill (d, x + 13, y + 14, 16, 6, (200, 154, 72)); fill (d, x + 13, y + 18, 38, 26, (224, 180, 92)); frame (d, x + 13, y + 18, 38, 26, (144, 106, 40))
		elif kind == "text":
			fill (d, x + 20, y + 10, 26, 36, (240, 240, 240)); frame (d, x + 20, y + 10, 26, 36, (80, 80, 90))
			for l in range (5): fill (d, x + 24, y + 16 + l * 6, 18, 2, (112, 120, 128))
		elif kind == "image":
			fill (d, x + 14, y + 12, 36, 30, (112, 184, 240)); fill (d, x + 14, y + 32, 36, 10, (80, 160, 80))
			fill (d, x + 38, y + 16, 6, 6, (255, 224, 112)); frame (d, x + 14, y + 12, 36, 30, (40, 40, 48))
		elif kind == "trash":
			fill (d, x + 19, y + 12, 26, 4, shade (c, 0.4)); fill (d, x + 27, y + 9, 10, 3, shade (c, 0.4))
			fill (d, x + 21, y + 17, 22, 28, lighten (c, 0.3)); frame (d, x + 21, y + 17, 22, 28, shade (c, 0.4))
			for l in range (3): fill (d, x + 26 + l * 6, y + 21, 2, 20, shade (c, 0.6))
		else: icon (img, kind, x + (w - 40) // 2, y + (h - 40) // 2)

	def shelf (self, img, d):					# the Front Panel (the Shelf's place)
		c = self.P[7]; fg = self.fg (c); top, bot, sel = self.sh (c)
		pw, ph = 876, 76; x0, y0 = (W - pw) // 2, H - ph - 4
		fill (d, x0, y0, pw, ph, c); self.bev (d, x0, y0, pw, ph, c, True, 2)
		for hx in (x0 + 3, x0 + pw - 17):						# the move handles, the panel's buttons
			for j in range (y0 + 24, y0 + ph - 6, 4):
				fill (d, hx + 3, j, 8, 1, bot); fill (d, hx + 3, j + 1, 8, 1, top)
			fill (d, hx + 1, y0 + 4, 12, 12, c); self.bev (d, hx + 1, y0 + 4, 12, 12, c, True, 1)
		left = ["clock", "calendar", "folder", "text", "tinypad"]; right = ["fileviewer", "paint", "terminal", "trash"]
		x = x0 + 18; cw = 64
		def cell (kind, arrow):
			nonlocal x
			fill (d, x + cw - 1, y0 + 6, 1, ph - 12, bot); fill (d, x + cw, y0 + 6, 1, ph - 12, top)
			self.fp_control (img, d, x, y0 + 8, cw, ph - 16, kind, c)
			if arrow:									# the subpanel's tab
				ax = x + cw // 2 - 14; fill (d, ax, y0 - 11, 28, 11, c); self.bev (d, ax, y0 - 11, 28, 11, c, True, 1); tri (d, ax + 14, y0 - 6, 4, True, fg)
			x += cw
		for i, k in enumerate (left): cell (k, i in (2, 3, 4))
		sw_ = 228; sx = x + 4; fill (d, sx, y0 + 4, sw_, ph - 8, c); self.bev (d, sx, y0 + 4, sw_, ph - 8, c, False, 1)
		for i, (lab, col) in enumerate ([("Shelf", self.P[2]), ("Documents", self.P[7]), ("Apps", self.P[5]), ("+", self.P[6])]):
			bx, by = sx + 34 + (i % 2) * 82, y0 + 9 + (i // 2) * 30
			self.button (img, d, bx, by, 80, 28, lab, "pressed" if i == 0 else "normal", bg = col)
		fill (d, sx + 8, y0 + 12, 18, 22, c); self.bev (d, sx + 8, y0 + 12, 18, 22, c, True, 1)	# lock
		d.arc ([sx + 12, y0 + 14, sx + 21, y0 + 24], 180, 360, fill = fg, width = 2); fill (d, sx + 11, y0 + 20, 12, 10, fg)
		fill (d, sx + sw_ - 38, y0 + 40, 34, 22, c); self.bev (d, sx + sw_ - 38, y0 + 40, 34, 22, c, True, 1)
		self.small.draw_c (img, sx + sw_ - 38, y0 + 40, 34, 22, "EXIT", fg)
		fill (d, sx + sw_ - 22, y0 + 14, 10, 10, (90, 200, 90)); frame (d, sx + sw_ - 22, y0 + 14, 10, 10, bot)	# the busy light
		x = sx + sw_ + 8
		for i, k in enumerate (right): cell (k, i in (0, 2))

	def panel (self, img, d):
		c = self.P[7]; top, bot, _ = self.sh (c)
		bw, bh = 62, 346; bx, by = W - bw - 2, (H - bh) // 2 - 30
		fill (d, bx, by, bw, bh, c); self.bev (d, bx, by, bw, bh, c, True, 2)
		fill (d, bx + 4, by + 4, bw - 8, 14, c); self.bev (d, bx + 4, by + 4, bw - 8, 14, c, True, 1)
		yy = by + 22
		for name, running in [("panel", False)] + PANEL_APPS:
			if name == "panel":
				for r in range (3):
					for cc in range (3): fill (d, bx + 17 + cc * 10, yy + 9 + r * 10, 6, 6, (0, 0, 0))
			else: icon (img, name, bx + 11, yy + 2)
			if running: fill (d, bx + 6, yy + 18, 3, 10, (0, 0, 0))
			fill (d, bx + 6, yy + 45, bw - 12, 1, bot); fill (d, bx + 6, yy + 46, bw - 12, 1, top)
			yy += 48
		self.ui.draw_c (img, bx, by + bh - 24, bw, 18, "13:37", self.fg (c))

	def cursor (self, img, d, x, y):				# X11's left_ptr
		pts = [(0, 0), (0, 16), (4, 12), (7, 18), (9, 17), (6, 11), (11, 11)]
		d.polygon ([(x + a, y + b) for a, b in pts], fill = (0, 0, 0), outline = (255, 255, 255))

# ======================================================================================================
class Win31 (Theme):
	name = "win31"
	FACE, SHADOW, HI, K, NAVY, WIN = (192, 192, 192), (128, 128, 128), (255, 255, 255), (0, 0, 0), (0, 0, 128), (255, 255, 255)
	MB = 21
	COMBO_H = 22
	def __init__ (self):
		self.ui = TFont ("LiberationSans-Bold.ttf", 13); self.small = TFont ("LiberationSans-Regular.ttf", 12)
		self.term = VGA
		self.APP_BG = self.FACE; self.STATUS_BG = self.FACE; self.STATUS_FG = self.K

	def backdrop (self, img, d): fill (d, 0, 0, W, H, self.FACE)

	def box (self, d, x, y, w, h, pressed = False):		# the 3D of a button, inside its outline
		if pressed:
			fill (d, x, y, w, h, self.FACE); fill (d, x, y, w, 1, self.SHADOW); fill (d, x, y, 1, h, self.SHADOW)
		else:
			fill (d, x, y, w, h, self.FACE)
			fill (d, x, y, w - 1, 2, self.HI); fill (d, x, y, 2, h - 1, self.HI)
			fill (d, x + 1, y + h - 2, w - 1, 2, self.SHADOW); fill (d, x + w - 2, y + 1, 2, h - 1, self.SHADOW)
			fill (d, x + w - 1, y, 1, 1, self.SHADOW); fill (d, x, y + h - 1, 1, 1, self.SHADOW)
	def outline (self, d, x, y, w, h, t = 1):			# black, the corners left out (rounded)
		for k in range (t):
			fill (d, x + 1 + k, y + k, w - 2 - 2 * k, 1, self.K); fill (d, x + 1 + k, y + h - 1 - k, w - 2 - 2 * k, 1, self.K)
			fill (d, x + k, y + 1 + k, 1, h - 2 - 2 * k, self.K); fill (d, x + w - 1 - k, y + 1 + k, 1, h - 2 - 2 * k, self.K)

	def button (self, img, d, x, y, w, h, label, state = "normal", embedded = False):
		if embedded:							# the user's framed button, in Windows' greys
			box = framed (d, x, y, w, h, self.FACE, self.HI, self.SHADOW, state == "pressed")
			framed_label (img, d, self.ui, box, label, self.K, state, self.HI, self.SHADOW)
			return
		t = 2 if state == "default" else 1
		self.outline (d, x, y, w, h, t)
		pressed = state == "pressed"
		self.box (d, x + t, y + t, w - 2 * t, h - 2 * t, pressed)
		o = 1 if pressed else 0
		self.ui.draw_c (img, x + o, y + o, w, h, label, self.SHADOW if state == "disabled" else self.K)
		if state == "default":						# the focus: a dotted rectangle round the label
			lw = self.ui.width (label) + 8; lx, ly = x + (w - lw) // 2, y + (h - self.ui.h) // 2 - 3
			for i in range (0, lw, 2): fill (d, lx + i, ly, 1, 1, self.K); fill (d, lx + i, ly + self.ui.h + 5, 1, 1, self.K)
			for j in range (0, self.ui.h + 6, 2): fill (d, lx, ly + j, 1, 1, self.K); fill (d, lx + lw - 1, ly + j, 1, 1, self.K)

	def field (self, img, d, x, y, w, h, text):
		fill (d, x, y, w, h, self.WIN); frame (d, x, y, w, h, self.K)
		self.term.draw (img, x + w - 8 - self.term.width (text), y + (h - 16) // 2, text, self.K)

	def combo (self, img, d, x, y, w, h, label):
		fill (d, x, y, w, h, self.WIN); frame (d, x, y, w, h, self.K)
		fill (d, x + 2, y + 2, w - 22, h - 4, self.NAVY); self.small.draw (img, x + 5, y + (h - self.small.h) // 2, label, self.HI)
		bx = x + w - 18; self.outline (d, bx, y, 18, h); self.box (d, bx + 1, y + 1, 16, h - 2)
		tri (d, bx + 9, y + h // 2 - 2, 4, False, self.K); fill (d, bx + 5, y + h // 2 + 3, 9, 2, self.K)

	def window (self, img, d, x, y, cw, ch, title, active, scroll = False):
		BW, TH = 5, 20
		w, h = cw + 2 * BW, ch + 2 * BW + TH + 1
		fill (d, x, y, w, h, self.FACE); frame (d, x, y, w, h, self.K); frame (d, x + BW - 1, y + BW - 1, w - 2 * BW + 2, h - 2 * BW + 2, self.K)
		for gx in (x + BW + TH + 1, x + w - BW - TH - 2):				# the sizing border's corner marks
			fill (d, gx, y, 1, BW, self.K); fill (d, gx, y + h - BW, 1, BW, self.K)
		for gy in (y + BW + TH + 1, y + h - BW - TH - 2):
			fill (d, x, gy, BW, 1, self.K); fill (d, x + w - BW, gy, BW, 1, self.K)
		tx, ty, tw = x + BW, y + BW, w - 2 * BW
		fill (d, tx, ty, tw, TH, self.NAVY if active else self.WIN); fill (d, tx, ty + TH, tw, 1, self.K)
		fill (d, tx, ty, TH, TH, self.FACE); fill (d, tx + TH, ty, 1, TH, self.K)			# the control-menu box
		fill (d, tx + 5, ty + 9, 11, 3, self.HI); frame (d, tx + 4, ty + 8, 13, 5, self.K); fill (d, tx + 6, ty + 13, 12, 1, self.SHADOW); fill (d, tx + 17, ty + 9, 1, 5, self.SHADOW)
		for i, up in ((0, True), (1, False)):						# maximise, minimise
			bx = tx + tw - TH * (i + 1) - i
			fill (d, bx - 1, ty, 1, TH, self.K); self.box (d, bx, ty, TH, TH)
			tri (d, bx + TH // 2 - 1, ty + TH // 2 - 1, 4, up, self.K)
		self.ui.draw_c (img, tx + TH, ty, tw - 3 * TH - 2, TH, title, self.HI if active else self.K)
		return x + BW, y + BW + TH + 1

	def terminal (self, img, d, x, y, w, h):			# an MS-DOS Prompt's look, a scroll bar
		fill (d, x, y, w, h, self.K)
		for i, s in enumerate (TERM): self.term.draw (img, x + 4, y + 4 + i * 19, s, self.FACE)
		iy = y + 4 + len (TERM) * 19; s = "$ cat readme.txt | grep -i pi"; self.term.draw (img, x + 4, iy, s, self.FACE)
		fill (d, x + 4 + self.term.width (s), iy + 13, 8, 2, self.FACE)
		sx = x + w - 17; fill (d, sx, y, 17, h, self.FACE); fill (d, sx, y, 1, h, self.K)
		for ay, up in ((y, True), (y + h - 17, False)):
			fill (d, sx, ay + (16 if up else 0), 17, 1, self.K); self.box (d, sx + 1, ay + (0 if up else 1), 16, 16)
			tri (d, sx + 9, ay + 8, 4, up, self.K)
		ty = y + 200; fill (d, sx, ty - 1, 17, 1, self.K); fill (d, sx, ty + 17, 17, 1, self.K); self.box (d, sx + 1, ty, 16, 17)

	def menubar (self, img, d, app, menus, open_idx, items, hover):
		fill (d, 0, 0, W, self.MB, self.WIN); fill (d, 0, self.MB - 1, W, 1, self.K)
		x = 4; xs = []
		for i, t in enumerate ([app] + menus):
			tw = self.ui.width (t) + 16; xs.append (x)
			if i == open_idx: fill (d, x, 1, tw, self.MB - 3, self.NAVY)
			ty = (self.MB - 1 - self.ui.h) // 2
			self.ui.draw (img, x + 8, ty, t, self.HI if i == open_idx else self.K)
			fill (d, x + 8, ty + self.ui.h, self.ui.width (t[0]), 1, self.HI if i == open_idx else self.K)
			x += tw
		self.ui.draw (img, W - 50, (self.MB - 1 - self.ui.h) // 2, "12:34", self.K)
		wifi (d, W - 80, 4, self.K); speaker (d, W - 106, 4, self.K)
		mx, my = xs[open_idx], self.MB - 1
		mw = max (self.ui.width (l) + self.ui.width ("Ctrl+" + k) for l, k in [i for i in items if i]) + 50
		mh = 4 + sum (9 if i is None else 20 for i in items)
		fill (d, mx, my, mw, mh, self.WIN); frame (d, mx, my, mw, mh, self.K)
		fill (d, mx + mw, my + 2, 2, mh, self.K); fill (d, mx + 2, my + mh, mw, 2, self.K)			# the drop shadow
		yy = my + 2; hp = (mx + 40, my + 12)
		for n, it in enumerate (items):
			if it is None: fill (d, mx + 1, yy + 4, mw - 2, 1, self.K); yy += 9; continue
			fg = self.K
			if n == hover: fill (d, mx + 2, yy, mw - 4, 20, self.NAVY); fg = self.HI; hp = (mx + 70, yy + 10)
			ty = yy + (20 - self.ui.h) // 2
			self.ui.draw (img, mx + 14, ty, it[0], fg); fill (d, mx + 14, ty + self.ui.h, self.ui.width (it[0][0]), 1, fg)
			k = "Ctrl+" + it[1]; self.ui.draw (img, mx + mw - 12 - self.ui.width (k), ty, k, fg)
			yy += 20
		return hp

	def shelf (self, img, d):					# a toolbar: the tabs as buttons, the items
		y0 = H - self.SHELF_H
		fill (d, 0, y0, W, self.SHELF_H, self.FACE); fill (d, 0, y0, W, 1, self.K); fill (d, 0, y0 + 1, W, 2, self.HI)
		x = 6
		for i, t in enumerate (["Shelf", "Documents", "Apps", "+"]):
			tw = self.ui.width (t) + 20
			self.button (img, d, x, y0 + 6, tw, 24, t, "pressed" if i == 0 else "normal"); x += tw + 4
		for i, (lab, kind) in enumerate (SHELF_ITEMS):
			cx = 8 + i * 84
			if kind in ("folder", "text", "image"): CDE.fp_control (CDE_T, img, d, cx + 10, y0 + 30, 64, 40, kind, self.FACE)
			else: icon (img, kind, cx + 22, y0 + 34)
			l = lab if self.small.width (lab) < 80 else lab[:9] + ".."
			self.small.draw (img, cx + (84 - self.small.width (l)) // 2, y0 + 80, l, self.K)
		tx = W - 90; fill (d, tx, y0 + 34, 1, 56, self.SHADOW); fill (d, tx + 1, y0 + 34, 1, 56, self.HI)
		CDE.fp_control (CDE_T, img, d, tx + 13, y0 + 30, 64, 40, "trash", self.FACE)
		self.small.draw (img, tx + (90 - self.small.width ("Trash")) // 2, y0 + 80, "Trash", self.K)

	def panel (self, img, d):
		bw, bh = 62, 330; bx, by = W - bw - 2, (H - bh) // 2 - 20
		fill (d, bx, by, bw, bh, self.FACE); frame (d, bx, by, bw, bh, self.K); fill (d, bx + 1, by + 1, bw - 2, 1, self.HI); fill (d, bx + 1, by + 1, 1, bh - 2, self.HI)
		yy = by + 6
		for name, running in [("panel", False)] + PANEL_APPS:
			self.button (img, d, bx + 5, yy, bw - 10, 46, "", "pressed" if running else "normal")
			if name == "panel":
				for r in range (3):
					for c in range (3): fill (d, bx + 18 + c * 10, yy + 10 + r * 10, 6, 6, self.K)
			else: icon (img, name, bx + 11 + (1 if running else 0), yy + 3 + (1 if running else 0))
			yy += 48
		fill (d, bx + 6, by + bh - 28, bw - 12, 22, self.WIN); frame (d, bx + 6, by + bh - 28, bw - 12, 22, self.K)
		self.small.draw_c (img, bx + 6, by + bh - 28, bw - 12, 22, "13:37", self.K)

	def cursor (self, img, d, x, y):
		pts = [(0, 0), (0, 16), (4, 12), (7, 18), (9, 17), (6, 11), (11, 11)]
		d.polygon ([(x + a, y + b) for a, b in pts], fill = (255, 255, 255), outline = (0, 0, 0))

CDE_T = CDE ()

# ======================================================================================================
def board ():
	"""The push buttons. First the user's framed button in the user's greys: as drawn (normal,
	"more marked", pressed) and at a dialog's size (default, normal, disabled); then each skin's
	own button, and the framed one in its colours -- normal, pressed, default, disabled."""
	lab = TFont ("LiberationSans-Bold.ttf", 14); sm = TFont ("LiberationSans-Regular.ttf", 12)
	F, L_, D_ = (185, 185, 185), (225, 225, 225), (127, 127, 127)
	BW, CW, X0 = 1110, 118, 150
	img = Image.new ("RGB", (BW, 520), (236, 236, 236)); d = ImageDraw.Draw (img)
	lab.draw (img, 14, 12, "Your framed button, drawn by code (no skin): the same pixels as your drawings", (0, 0, 0))
	y = 36; fill (d, 0, y, BW, 146, F)
	for i, (cap, st, bt) in enumerate ([("normal", "normal", 1), ("normal, more marked", "normal", 2), ("pressed", "pressed", 2)]):
		x = 14 + i * 236
		sm.draw (img, x, y + 8, cap, (0, 0, 0)); framed (d, x, y + 30, 216, 92, F, L_, D_, st == "pressed", bt)
	sm.draw (img, 730, y + 8, "at a dialog's size: default, normal, disabled", (0, 0, 0))
	for i, (l, st) in enumerate ([("OK", "default"), ("Cancel", "normal"), ("Help", "disabled")]):
		bx, by = 730 + i * 122, y + 30
		box = framed (d, bx, by, 112, 40, F, L_, D_, False)
		framed_label (img, d, lab, box, l, (0, 0, 0), st, L_, D_)
		box = framed (d, bx, by + 52, 112, 40, F, L_, D_, True)
		framed_label (img, d, lab, box, l, (0, 0, 0), "pressed", L_, D_)
	sm.draw (img, 730, y + 126, "(below: the same, pressed)", (60, 60, 60))
	y = 196
	lab.draw (img, 14, y, "Each skin: its own push button, then your framed button in its colours", (0, 0, 0))
	states = ["normal", "pressed", "default", "disabled"]
	sm.draw (img, X0, y + 26, "its own", (0, 0, 0)); sm.draw (img, X0 + 4 * CW, y + 26, "framed", (0, 0, 0))
	for i, s in enumerate (states * 2): sm.draw_c (img, X0 + i * CW, y + 44, CW - 14, 16, s, (60, 60, 60))
	themes = [(Win31 (), "Windows 3.1"), (Workbench (), "Workbench"), (CDE_T, "CDE (Motif)")]
	for r, (t, name) in enumerate (themes):
		ry = y + 66 + r * 84; bg = t.APP_BG
		fill (d, 0, ry, BW, 76, bg)
		lab.draw (img, 14, ry + (76 - lab.h) // 2, name, CDE.fg (bg))
		for e in (False, True):
			for i, s in enumerate (states):
				x = X0 + (4 if e else 0) * CW + i * CW
				w_, h_ = (104, 48) if e else (96, 40)
				t.button (img, d, x, ry + (76 - h_) // 2, w_, h_, "Cancel" if i == 3 else "OK", s, embedded = e)
	return img

def main ():
	os.makedirs (OUT, exist_ok = True)
	for T in (Workbench (), CDE_T, Win31 ()):
		p = os.path.join (OUT, "retro-%s.png" % T.name); T.desktop ().save (p, optimize = True); print ("wrote", p)
	p = os.path.join (OUT, "retro-buttons.png"); board ().save (p, optimize = True); print ("wrote", p)

if __name__ == "__main__":
	main ()
