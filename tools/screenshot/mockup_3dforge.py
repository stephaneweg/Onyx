#!/usr/bin/env python3
"""mockup_3dforge.py -- the first mock-ups of 3DForge, Onyx's small parametric CAD (sketch, extrude,
union / subtract / intersect, chamfers and fillets, STL / OBJ). See docs/3dforge/README.md.

    python tools/screenshot/mockup_3dforge.py  -> docs/3dforge/mockups/3dforge-*.png

1024 x 768, the Milk theme (screenshots/milk.png: the frame melting into the window, the beads, Aqua's
blue). The part shown is REAL: built by Manifold (`pip install manifold3d`), the kernel the app will use,
with the very operations of its history (boxes, cylinders, an extruded sketch, fillets made of a prism
less a cylinder), then drawn by a small z-buffer here (flat shading, the faces' edges, a contact shadow)
-- what the GPU will draw on the Pi. Everything is drawn at K times the size, then scaled down.
"""
import os, math
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import manifold3d as mf

HERE = os.path.dirname (os.path.abspath (__file__))
ROOT = os.path.dirname (os.path.dirname (HERE))
OUT = os.path.join (ROOT, "docs", "3dforge", "mockups")
W, H, K = 1024, 768, 2
FD = os.path.join (ROOT, "sdcard", "res", "fonts")
def _f (name, size): return ImageFont.truetype (os.path.join (FD, name), int (size * K))
F = dict (ui = _f ("DejaVuSans.ttf", 13), uib = _f ("DejaVuSans-Bold.ttf", 13), small = _f ("DejaVuSans.ttf", 11),
	  smallb = _f ("DejaVuSans-Bold.ttf", 11), tiny = _f ("DejaVuSans.ttf", 10), title = _f ("DejaVuSans-Bold.ttf", 13),
	  big = _f ("DejaVuSans-Bold.ttf", 15), cube = _f ("DejaVuSans-Bold.ttf", 9))

# ---- Milk's palette (uikit/theme.cpp: the face E4E4E4, the accent 3D86DA; skin.cpp: the beads) -----------
DESK, DESK2 = (36, 58, 90), (22, 38, 62)
FACE = (228, 228, 228); PANEL = (219, 219, 221); FIELD = (252, 252, 252); MENU = (236, 236, 238)
LINE = (164, 164, 168); LINE2 = (204, 204, 207); TEXT = (26, 26, 30); DIM = (104, 104, 112); FAINT = (158, 158, 166)
ACC = (61, 134, 218); SEL_SOFT = (190, 211, 240); EDGE = (72, 74, 82)
RED = (224, 82, 70); AMBER = (232, 150, 40); GREEN = (70, 168, 84); WHITE = (255, 255, 255)
BODY = (146, 170, 204); INK = (58, 62, 72)

def lighten (c, k): return tuple (int (v + (255 - v) * k) for v in c[:3])
def shade (c, k): return tuple (int (v * k) for v in c[:3])
def mix (a, b, t): return tuple (int (a[i] + (b[i] - a[i]) * t) for i in range (3))
def A (c, a): return tuple (c[:3]) + (a,)

class Canvas:
	def __init__ (self):
		self.img = Image.new ("RGB", (W * K, H * K), DESK); self.d = ImageDraw.Draw (self.img, "RGBA")
	def rect (self, x, y, w, h, fill = None, r = 0, outline = None, width = 1, corners = None):
		if w <= 0 or h <= 0: return
		b = [x * K, y * K, (x + w) * K - 1, (y + h) * K - 1]
		if r: self.d.rounded_rectangle (b, r * K, fill = fill, outline = outline, width = int (width * K), corners = corners)
		else: self.d.rectangle (b, fill = fill, outline = outline, width = int (width * K))
	def grad (self, x, y, w, h, top, bottom, r = 0, corners = None):
		g = Image.new ("RGB", (int (w * K), int (h * K))); gd = ImageDraw.Draw (g)
		for i in range (int (h * K)): gd.line ([0, i, w * K, i], fill = mix (top, bottom, i / max (1, h * K - 1)))
		m = Image.new ("L", g.size, 0)
		ImageDraw.Draw (m).rounded_rectangle ([0, 0, g.size[0] - 1, g.size[1] - 1], r * K, fill = 255, corners = corners)
		self.img.paste (g, (int (x * K), int (y * K)), m)
	def line (self, pts, fill, width = 1):
		self.d.line ([(px * K, py * K) for px, py in pts], fill = fill, width = max (1, int (round (width * K))), joint = "curve")
	def dash (self, p0, p1, fill, width = 1, on = 5, off = 4):
		L = math.hypot (p1[0] - p0[0], p1[1] - p0[1]); t = 0
		while t < L:
			a, b = t / L, min (1, (t + on) / L)
			self.line ([(p0[0] + (p1[0] - p0[0]) * a, p0[1] + (p1[1] - p0[1]) * a), (p0[0] + (p1[0] - p0[0]) * b, p0[1] + (p1[1] - p0[1]) * b)], fill, width)
			t += on + off
	def hline (self, x0, x1, y, fill, width = 1): self.rect (x0, y, x1 - x0, width, fill)
	def vline (self, x, y0, y1, fill, width = 1): self.rect (x, y0, width, y1 - y0, fill)
	def ellipse (self, cx, cy, r, fill = None, outline = None, width = 1, ry = None):
		ry = r if ry is None else ry
		self.d.ellipse ([(cx - r) * K, (cy - ry) * K, (cx + r) * K, (cy + ry) * K], fill = fill, outline = outline, width = max (1, int (round (width * K))))
	def arc (self, cx, cy, r, a0, a1, fill, width = 1, ry = None):
		ry = r if ry is None else ry
		self.d.arc ([(cx - r) * K, (cy - ry) * K, (cx + r) * K, (cy + ry) * K], a0, a1, fill = fill, width = max (1, int (round (width * K))))
	def poly (self, pts, fill = None, outline = None, width = 1):
		q = [(px * K, py * K) for px, py in pts]
		if fill: self.d.polygon (q, fill = fill)
		if outline: self.d.line (q + [q[0]], fill = outline, width = max (1, int (round (width * K))), joint = "curve")
	def tw (self, s, font = "ui"): return F[font].getlength (s) / K
	def text (self, x, y, s, font = "ui", fill = TEXT, anchor = "la"):
		self.d.text ((x * K, y * K), s, font = F[font], fill = fill, anchor = anchor)
	def text_l (self, x, y, h, s, font = "ui", fill = TEXT): self.text (x, y + h / 2, s, font, fill, "lm")
	def text_c (self, x, y, w, h, s, font = "ui", fill = TEXT): self.text (x + w / 2, y + h / 2, s, font, fill, "mm")
	def text_r (self, x, y, h, s, font = "ui", fill = TEXT): self.text (x, y + h / 2, s, font, fill, "rm")
	def save (self, name):
		os.makedirs (OUT, exist_ok = True); p = os.path.join (OUT, name)
		self.img.resize ((W, H), Image.LANCZOS).save (p); print ("wrote", os.path.relpath (p, ROOT))

# ---- the desktop, Milk's frame, the widgets -----------------------------------------------------------------
def desktop (c, app, menus):
	c.grad (0, 0, W, H, DESK, DESK2)
	c.rect (0, 0, W, 26, MENU); c.hline (0, W, 26, (150, 150, 156))
	x = 14
	c.text_l (x, 0, 26, "Onyx", "menu" if "menu" in F else "ui"); x += c.tw ("Onyx") + 22
	c.text_l (x, 0, 26, app, "uib"); x += c.tw (app, "uib") + 24
	for m in menus: c.text_l (x, 0, 26, m); x += c.tw (m) + 22
	c.text_r (W - 14, 0, 26, "12:34", "uib")
	c.poly ([(W - 104, 10), (W - 100, 10), (W - 95, 6), (W - 95, 20), (W - 100, 16), (W - 104, 16)], TEXT)
	for r in (4, 7): c.arc (W - 94, 13, r, -45, 45, TEXT, 1.6)
	for r in (3, 7, 11): c.arc (W - 70, 19, r, 225, 315, TEXT, 1.6)

def bead (c, cx, cy, col, glyph = False):
	c.ellipse (cx, cy, 7, col, shade (col, 0.72))
	c.arc (cx, cy, 5, 200, 340, A (WHITE, 120), 1.2)
	if glyph: c.rect (cx - 4, cy - 1, 8, 2, shade (col, 0.45))

def window (c, x, y, w, h, title, resizable = True, menu = True):
	"""Milk's frame: the title bar from a light tone down to the window's own colour. Returns the client."""
	c.rect (x, y, w, h, FACE, r = 8)
	c.grad (x, y, w, 27, lighten (FACE, 0.75), FACE, r = 8, corners = (True, True, False, False))
	c.rect (x, y, w, h, r = 8, outline = EDGE)
	c.text_c (x, y, w, 27, title, "title", (48, 48, 54))
	bead (c, x + 17, y + 14, (154, 168, 186) if menu else (194, 195, 200), True)
	bead (c, x + w - 17, y + 14, (232, 86, 78))
	bead (c, x + w - 39, y + 14, (76, 182, 83) if resizable else (194, 195, 200))
	bead (c, x + w - 61, y + 14, (240, 180, 58))
	return x + 1, y + 27, w - 2, h - 28

def button (c, x, y, w, h, label, accent = False, default = False, disabled = False):
	if accent: c.grad (x, y, w, h, lighten (ACC, 0.14), shade (ACC, 0.93), r = 5); c.rect (x, y, w, h, r = 5, outline = shade (ACC, 0.68))
	else:
		c.grad (x, y, w, h, (253, 253, 253), (232, 232, 234), r = 5); c.rect (x, y, w, h, r = 5, outline = (146, 146, 152))
		if default: c.rect (x - 2, y - 2, w + 4, h + 4, r = 7, outline = (146, 146, 152))
	c.text_c (x, y, w, h, label, "uib" if accent else "ui", WHITE if accent else (FAINT if disabled else TEXT))

def field (c, x, y, w, h, s = "", unit = None, focus = False, font = "ui", align = "l", link = False):
	c.rect (x, y, w, h, FIELD, r = 4, outline = LINE)
	if focus: c.rect (x - 1, y - 1, w + 2, h + 2, r = 5, outline = ACC, width = 2)
	if unit: c.text_r (x + w - 7, y, h, unit, "small", FAINT)
	col = (40, 98, 176) if link else TEXT
	if align == "r":
		rx = x + w - (7 + (c.tw (unit, "small") + 5 if unit else 0)); c.text_r (rx, y, h, s, font, col)
		if focus: c.vline (rx + 1, y + 5, y + h - 5, TEXT)
	else:
		c.text_l (x + 7, y, h, s, font, col)
		if focus: c.vline (x + 8 + c.tw (s, font), y + 5, y + h - 5, TEXT)

def checkbox (c, x, y, label, on = True):
	c.rect (x, y, 16, 16, FIELD, r = 3, outline = LINE)
	if on:
		c.rect (x, y, 16, 16, ACC, r = 3); c.line ([(x + 4, y + 8), (x + 7, y + 11), (x + 12, y + 5)], WHITE, 2)
	c.text_l (x + 23, y, 16, label)

def segmented (c, x, y, w, h, items, sel):
	c.rect (x, y, w, h, FIELD, r = 5, outline = (146, 146, 152))
	sw = w / len (items)
	for i, s in enumerate (items):
		if i == sel:
			cr = (i == 0, i == len (items) - 1, i == len (items) - 1, i == 0)
			c.rect (x + i * sw, y, sw, h, ACC, r = 5, corners = cr)
		elif i and i - 1 != sel: c.vline (x + i * sw, y + 1, y + h - 1, LINE2)
		c.text_c (x + i * sw, y, sw, h, s, "uib" if i == sel else "ui", WHITE if i == sel else TEXT)

def group (c, x, y, w, h, title = None):
	c.rect (x, y, w, h, PANEL, r = 6, outline = (190, 190, 194))
	if title: c.text (x + 11, y + 9, title, "uib")

def listbox (c, x, y, w, h): c.rect (x, y, w, h, FIELD, r = 4, outline = LINE)

def cursor (c, x, y):
	p = [(0, 0), (0, 16), (4, 12.5), (7, 19), (9.5, 18), (6.5, 11.5), (11.5, 11.5)]
	c.poly ([(x + a, y + b) for a, b in p], (20, 20, 22), WHITE, 1)

# ---- the icons (vector, on a 24-unit grid) --------------------------------------------------------------------
def icon (c, kind, x, y, s = 24, col = INK, acc = ACC, bg = FACE):
	u = s / 24.0; w = max (1.25, 1.7 * u)
	def P (pts): return [(x + a * u, y + b * u) for a, b in pts]
	def L (pts, colr = None, ww = None): c.line (P (pts), colr or col, ww or w)
	soft = mix (acc, WHITE, 0.62)
	if kind == "box":
		c.poly (P ([(12, 3), (20, 7.5), (12, 12), (4, 7.5)]), soft)
		c.poly (P ([(12, 3), (20, 7.5), (20, 16.5), (12, 21), (4, 16.5), (4, 7.5)]), None, col, w)
		L ([(4, 7.5), (12, 12), (20, 7.5)]); L ([(12, 12), (12, 21)])
	elif kind == "cyl":
		c.ellipse (x + 12 * u, y + 6.5 * u, 7 * u, soft, col, w, ry = 3.3 * u)
		L ([(5, 6.5), (5, 17.5)]); L ([(19, 6.5), (19, 17.5)]); c.arc (x + 12 * u, y + 17.5 * u, 7 * u, 0, 180, col, w, ry = 3.3 * u)
	elif kind == "sketch":
		c.poly (P ([(3, 9), (15, 9), (15, 21), (3, 21)]), soft, col, w)
		c.poly (P ([(11, 15), (12.2, 11), (19.5, 3.5), (22, 6), (14.8, 13.6)]), acc)
		c.poly (P ([(11, 15), (12.2, 11), (14.8, 13.6)]), col)
	elif kind == "extrude":
		c.poly (P ([(3, 17.5), (12, 21.5), (21, 17.5), (12, 13.5)]), soft, col, w)
		L ([(12, 17), (12, 5)], acc, w * 1.25); c.poly (P ([(12, 1.5), (16.2, 7), (7.8, 7)]), acc)
	elif kind in ("chamfer", "fillet"):
		if kind == "chamfer": top = [(4, 11), (11, 4)]
		else: top = [(12 - 8 * math.cos (t * math.pi / 16), 12 - 8 * math.sin (t * math.pi / 16)) for t in range (9)]
		c.poly (P ([(4, 21)] + top + [(21, 4), (21, 21)]), soft, col, w)
		L (top, acc, w * 1.5)
	elif kind == "move":
		L ([(12, 3), (12, 21)]); L ([(3, 12), (21, 12)])
		for a, b, d in (((12, 2), (9, 6), (15, 6)), ((12, 22), (9, 18), (15, 18)), ((2, 12), (6, 9), (6, 15)), ((22, 12), (18, 9), (18, 15))): c.poly (P ([a, b, d]), col)
	elif kind in ("union", "subtract", "intersect", "newbody"):
		a, b = (3, 3, 12, 12), (9, 9, 12, 12)
		def sq (r, fill, out, dashed = False):
			pts = P ([(r[0], r[1]), (r[0] + r[2], r[1]), (r[0] + r[2], r[1] + r[3]), (r[0], r[1] + r[3])])
			if fill: c.poly (pts, fill)
			if out:
				if dashed:
					for i in range (4): c.dash (pts[i], pts[(i + 1) % 4], out, w * 0.8, 2.5 * u, 2 * u)
				else: c.poly (pts, None, out, w)
		if kind == "union": sq (a, soft, None); sq (b, soft, None); c.poly (P ([(3, 3), (15, 3), (15, 9), (21, 9), (21, 21), (9, 21), (9, 15), (3, 15)]), None, col, w)
		elif kind == "subtract": sq (a, soft, col); sq (b, bg, None); sq (b, None, mix (col, bg, 0.35), True); L ([(9, 15), (9, 9), (15, 9)])
		elif kind == "intersect": sq (a, None, mix (col, bg, 0.35), True); sq (b, None, mix (col, bg, 0.35), True); sq ((9, 9, 6, 6), soft, col)
		else: sq ((5, 5, 14, 14), soft, col); L ([(12, 8.5), (12, 15.5)], acc, w * 1.2); L ([(8.5, 12), (15.5, 12)], acc, w * 1.2)
	elif kind == "measure":
		c.poly (P ([(2, 15), (15, 2), (22, 9), (9, 22)]), soft, col, w)
		for i in range (4): L ([(6 + i * 3.2, 11.2 + (-i) * 3.2 + 0), (8.2 + i * 3.2, 13.4 - i * 3.2)], col, w * 0.8)
	elif kind == "export":
		L ([(4, 14), (4, 20), (20, 20), (20, 14)]); L ([(12, 15), (12, 5)], acc, w * 1.25); c.poly (P ([(12, 1.5), (16.2, 7), (7.8, 7)]), acc)
	elif kind == "new":
		c.poly (P ([(6, 3), (14, 3), (19, 8), (19, 21), (6, 21)]), WHITE, col, w); L ([(14, 3), (14, 8), (19, 8)])
	elif kind == "open":
		c.poly (P ([(3, 6), (9, 6), (11, 8.5), (20, 8.5), (20, 19), (3, 19)]), (236, 196, 110), col, w); L ([(3, 11.5), (20, 11.5)], col, w * 0.7)
	elif kind == "save":
		c.poly (P ([(4, 4), (17, 4), (20, 7), (20, 20), (4, 20)]), soft, col, w); c.rect (x + 8 * u, y + 4 * u, 7 * u, 5 * u, col); c.rect (x + 7 * u, y + 13 * u, 10 * u, 7 * u, WHITE, outline = col)
	elif kind in ("undo", "redo"):
		f = 1 if kind == "undo" else -1; mx = x + 12 * u
		c.arc (mx + f * 1 * u, y + 13 * u, 6.5 * u, 180 if f > 0 else 90, 90 if f > 0 else 360, col, w * 1.15)
		c.poly ([(mx - f * 9.5 * u, y + 13 * u), (mx - f * 1.5 * u, y + 13 * u), (mx - f * 5.5 * u, y + 7.5 * u)][::1], col)
	elif kind == "eye":
		c.ellipse (x + 12 * u, y + 12 * u, 8.5 * u, None, col, w, ry = 5 * u); c.ellipse (x + 12 * u, y + 12 * u, 2.6 * u, col)
	elif kind == "line": L ([(4, 20), (20, 4)]); c.ellipse (x + 4 * u, y + 20 * u, 2.4 * u, acc); c.ellipse (x + 20 * u, y + 4 * u, 2.4 * u, acc)
	elif kind == "rect": c.poly (P ([(4, 6), (20, 6), (20, 18), (4, 18)]), soft, col, w); c.ellipse (x + 4 * u, y + 6 * u, 2.4 * u, acc); c.ellipse (x + 20 * u, y + 18 * u, 2.4 * u, acc)
	elif kind == "circle": c.ellipse (x + 12 * u, y + 12 * u, 8.5 * u, soft, col, w); c.ellipse (x + 12 * u, y + 12 * u, 2.2 * u, acc)
	elif kind == "arc":
		c.arc (x + 6 * u, y + 19 * u, 14 * u, 270, 360, col, w); L ([(6, 19), (6, 5)], mix (col, bg, 0.5), w * 0.7); L ([(6, 19), (20, 19)], mix (col, bg, 0.5), w * 0.7)
		for p in ((6, 19), (6, 5), (20, 19)): c.ellipse (x + p[0] * u, y + p[1] * u, 2.2 * u, acc)
	elif kind == "close":
		L ([(5, 19), (5, 6), (19, 6), (19, 19)]); c.dash (P ([(19, 19)])[0], P ([(5, 19)])[0], acc, w * 1.2, 3 * u, 2.2 * u)
		for p in ((5, 19), (19, 19)): c.ellipse (x + p[0] * u, y + p[1] * u, 2.4 * u, acc)
	elif kind == "home": c.poly (P ([(4, 12), (12, 4.5), (20, 12), (17.5, 12), (17.5, 19.5), (6.5, 19.5), (6.5, 12)]), None, col, w)
	elif kind == "fit":
		for (a, b, d) in (((4, 9), (4, 4), (9, 4)), ((15, 4), (20, 4), (20, 9)), ((20, 15), (20, 20), (15, 20)), ((9, 20), (4, 20), (4, 15))): L ([a, b, d])
		c.rect (x + 9 * u, y + 9 * u, 6 * u, 6 * u, col)
	elif kind == "shaded":
		c.poly (P ([(12, 3), (20, 7.5), (12, 12), (4, 7.5)]), lighten (col, 0.75)); c.poly (P ([(4, 7.5), (12, 12), (12, 21), (4, 16.5)]), lighten (col, 0.35)); c.poly (P ([(20, 7.5), (12, 12), (12, 21), (20, 16.5)]), lighten (col, 0.55))
		c.poly (P ([(12, 3), (20, 7.5), (20, 16.5), (12, 21), (4, 16.5), (4, 7.5)]), None, col, w * 0.8)
	elif kind == "check": L ([(5, 12.5), (10, 17.5), (19, 6.5)], col, w * 1.5)
	elif kind == "info":
		c.ellipse (x + 12 * u, y + 12 * u, 8.5 * u, None, col, w); c.rect (x + 11 * u, y + 10.5 * u, 2 * u, 6.5 * u, col); c.rect (x + 11 * u, y + 6.5 * u, 2 * u, 2.2 * u, col)

# ---- the 3D: the camera (orthographic), the z-buffer, the faces' edges ----------------------------------------
class Cam:
	def __init__ (self, rect, az, el, scale, target):
		self.rect = rect; a, e = math.radians (az), math.radians (el)
		self.d = np.array ([math.cos (e) * math.cos (a), math.cos (e) * math.sin (a), math.sin (e)])
		self.r = np.array ([-math.sin (a), math.cos (a), 0.0])
		self.u = np.array ([-math.sin (e) * math.cos (a), -math.sin (e) * math.sin (a), math.cos (e)])
		self.s = scale; self.t = np.array (target, float)
	def xy (self, p):
		"""A point of the model -> the screen (logical pixels)."""
		q = np.asarray (p, float) - self.t; x, y, w, h = self.rect
		return (x + w / 2 + float (q @ self.r) * self.s, y + h / 2 - float (q @ self.u) * self.s)
	def proj (self, V):
		"""The vertices -> the viewport's pixels (K times), the depth (larger: nearer)."""
		q = V - self.t; _, _, w, h = self.rect
		return np.stack ([(w / 2 + (q @ self.r) * self.s) * K, (h / 2 - (q @ self.u) * self.s) * K, V @ self.d], 1)

def mesh_of (man):
	m = man.to_mesh (); V = np.asarray (m.vert_properties)[:, :3].astype (float); T = np.asarray (m.tri_verts).astype (int)
	ri = np.asarray (m.run_index); ro = np.asarray (m.run_original_id)
	orig = ro[np.clip (np.searchsorted (ri, 3 * np.arange (len (T)), side = "right") - 1, 0, len (ro) - 1)]
	N = np.cross (V[T[:, 1]] - V[T[:, 0]], V[T[:, 2]] - V[T[:, 0]]); N /= np.maximum (np.linalg.norm (N, axis = 1), 1e-12)[:, None]
	# the faces: triangles joined across an edge when nearly coplanar (a tangent fillet from another primitive
	# is a face of its own: its first facet leaves the plane by more than half a degree)
	par = list (range (len (T)))
	def find (i):
		while par[i] != i: par[i] = par[par[i]]; i = par[i]
		return i
	edges = {}
	for t, tri in enumerate (T):
		for k in range (3):
			e = (min (tri[k], tri[(k + 1) % 3]), max (tri[k], tri[(k + 1) % 3])); edges.setdefault (e, []).append (t)
	c25, c05 = math.cos (math.radians (25)), math.cos (math.radians (0.5))
	for ts in edges.values ():
		if len (ts) != 2: continue
		a, b = ts; dot = float (N[a] @ N[b])
		if dot > c25 and (orig[a] == orig[b] or dot > c05): par[find (a)] = find (b)
	grp = np.array ([find (i) for i in range (len (T))])
	return V, T, N, grp

def raster (cam, V, T, N):
	_, _, w, h = cam.rect; Wp, Hp = int (w * K), int (h * K)
	P = cam.proj (V); zb = np.full ((Hp, Wp), -1e18); ib = np.full ((Hp, Wp), -1, np.int32)
	for t in np.nonzero (N @ cam.d > 1e-6)[0]:
		(x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = P[T[t, 0]], P[T[t, 1]], P[T[t, 2]]
		ax, bx = max (int (math.floor (min (x0, x1, x2))), 0), min (int (math.ceil (max (x0, x1, x2))), Wp - 1)
		ay, by = max (int (math.floor (min (y0, y1, y2))), 0), min (int (math.ceil (max (y0, y1, y2))), Hp - 1)
		if ax > bx or ay > by: continue
		area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
		if abs (area) < 1e-9: continue
		X, Y = np.meshgrid (np.arange (ax, bx + 1) + 0.5, np.arange (ay, by + 1) + 0.5)
		w0 = ((x1 - X) * (y2 - Y) - (x2 - X) * (y1 - Y)) / area; w1 = ((x2 - X) * (y0 - Y) - (x0 - X) * (y2 - Y)) / area; w2 = 1 - w0 - w1
		z = w0 * z0 + w1 * z1 + w2 * z2; sub = zb[ay:by + 1, ax:bx + 1]
		m = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4) & (z > sub)
		sub[m] = z[m]; ib[ay:by + 1, ax:bx + 1][m] = t
	return ib

def draw_body (c, cam, man, base = BODY, alpha = 255, pick = (), tint = ACC, shadow = True, edge = (34, 46, 66), fade = 0.0, bg = (240, 242, 246)):
	"""One body in the viewport: flat shading, its faces' edges, a soft shadow on the ground. pick: points of the
	model whose faces are shown selected."""
	V, T, N, grp = mesh_of (man); x, y, w, h = cam.rect
	if shadow:
		V0 = V.copy (); V0[:, 2] = 0; N0 = np.tile (np.array ([0, 0, 1.0]), (len (T), 1))
		m = (raster (cam, V0, T[:, ::-1], N0) >= 0) | (raster (cam, V0, T, N0) >= 0)
		sh = Image.fromarray ((m * 255).astype (np.uint8)).filter (ImageFilter.GaussianBlur (7 * K))
		c.img.paste (Image.new ("RGB", sh.size, (60, 70, 90)), (int (x * K), int (y * K)), sh.point (lambda v: int (v * 0.30)))
	ib = raster (cam, V, T, N); hit = ib >= 0
	lit = cam.d * 0.74 + cam.u * 0.52 - cam.r * 0.34; lit /= np.linalg.norm (lit)
	k = 0.54 + 0.54 * np.clip (N @ lit, 0, 1) + 0.10 * np.clip (N @ (cam.r * 0.8 + cam.d * 0.6), 0, 1)
	col = np.clip (np.array (base, float)[None, :] * k[:, None] * 1.02, 0, 255)
	sel = set ()
	for p in pick:
		px, py, _ = cam.proj (np.array ([p], float))[0]
		if 0 <= int (py) < ib.shape[0] and 0 <= int (px) < ib.shape[1] and ib[int (py), int (px)] >= 0: sel.add (grp[ib[int (py), int (px)]])
	if sel:
		ms = np.isin (grp, list (sel)); col[ms] = col[ms] * 0.30 + np.array (tint, float) * 0.70 * (0.72 + 0.34 * k[ms, None])
		col = np.clip (col, 0, 255)
	g = np.where (hit, grp[np.maximum (ib, 0)], -1)
	e = np.zeros (g.shape, bool); dh = g[:, 1:] != g[:, :-1]; dv = g[1:] != g[:-1]
	e[:, 1:] |= dh; e[:, :-1] |= dh; e[1:] |= dv; e[:-1] |= dv
	rgb = col[np.maximum (ib, 0)]
	if fade: rgb = rgb * (1 - fade) + np.array (bg, float) * fade; edge = mix (edge, bg, fade * 0.8)
	out = np.zeros (g.shape + (4,), np.uint8); out[..., :3] = rgb.astype (np.uint8); out[..., 3] = np.where (hit, alpha, 0)
	out[e, :3] = edge; out[e, 3] = 255 if alpha == 255 else min (255, alpha + 110)
	im = Image.fromarray (out, "RGBA"); c.img.paste (im, (int (x * K), int (y * K)), im)

def skew_text (c, O, U, Vd, s, fill = DIM):
	"""A word lying on a face: the square O, O+U, O+U+Vd, O+Vd (logical pixels)."""
	S = 96 * K; src = Image.new ("RGBA", (S, S), (0, 0, 0, 0)); ImageDraw.Draw (src).text ((S / 2, S / 2), s, font = _f ("DejaVuSans-Bold.ttf", 23), fill = fill + (255,), anchor = "mm")
	O, U, Vd = np.array (O) * K, np.array (U) * K, np.array (Vd) * K
	pts = np.array ([O, O + U, O + U + Vd, O + Vd]); mn = np.floor (pts.min (0)); mx = np.ceil (pts.max (0))
	Mi = np.linalg.inv (np.array ([[U[0], Vd[0]], [U[1], Vd[1]]])) * S; off = Mi @ (mn - O)
	im = src.transform ((int (mx[0] - mn[0]), int (mx[1] - mn[1])), Image.AFFINE, (Mi[0, 0], Mi[0, 1], off[0], Mi[1, 0], Mi[1, 1], off[1]), Image.BICUBIC)
	c.img.paste (im, (int (mn[0]), int (mn[1])), im)

def view_cube (c, cx, cy, az, el, size = 40):
	cam = Cam ((cx - 50, cy - 50, 100, 100), az, el, size, (0.5, 0.5, 0.5))
	f = lambda *p: cam.xy (p)
	c.ellipse (cx, cy + 35, 42, A ((120, 130, 150), 40), ry = 12)
	for pts, col in (([f (0, 0, 1), f (1, 0, 1), f (1, 1, 1), f (0, 1, 1)], (252, 252, 253)), ([f (0, 0, 0), f (1, 0, 0), f (1, 0, 1), f (0, 0, 1)], (236, 238, 242)),
			 ([f (1, 0, 0), f (1, 1, 0), f (1, 1, 1), f (1, 0, 1)], (220, 224, 231))):
		c.poly (pts, col, (140, 146, 158), 1)
	def lab (o, a, b, s):
		o, a, b = np.array (f (*o)), np.array (f (*a)), np.array (f (*b)); skew_text (c, o, a - o, b - o, s)
	lab ((0, 1, 1), (1, 1, 1), (0, 0, 1), "TOP"); lab ((0, 0, 1), (1, 0, 1), (0, 0, 0), "FRONT"); lab ((1, 0, 1), (1, 1, 1), (1, 0, 0), "RIGHT")

def triad (c, x, y, az, el):
	cam = Cam ((x - 20, y - 20, 40, 40), az, el, 24, (0, 0, 0))
	for v, col, n in (((1, 0, 0), (214, 72, 62), "X"), ((0, 1, 0), (64, 160, 76), "Y"), ((0, 0, 1), (58, 122, 214), "Z")):
		p = cam.xy (v); c.line ([(x, y), p], col, 2); q = cam.xy (tuple (1.35 * a for a in v)); c.text (q[0], q[1], n, "smallb", col, "mm")
	c.ellipse (x, y, 2.5, (90, 94, 104))

def ground (c, cam, x0 = -40, x1 = 130, y0 = -40, y1 = 100, step = 10):
	for i in range (x0, x1 + 1, step):
		c.line ([cam.xy ((i, y0, 0)), cam.xy ((i, y1, 0))], (203, 208, 217) if i % 50 else (176, 183, 196), 0.6)
	for j in range (y0, y1 + 1, step):
		c.line ([cam.xy ((x0, j, 0)), cam.xy ((x1, j, 0))], (203, 208, 217) if j % 50 else (176, 183, 196), 0.6)
	c.line ([cam.xy ((0, 0, 0)), cam.xy ((x1, 0, 0))], (214, 92, 82), 1.2); c.line ([cam.xy ((0, 0, 0)), cam.xy ((0, y1, 0))], (80, 168, 90), 1.2)

def tag (c, x, y, s, active = False, col = ACC):
	"""A dimension on the drawing: a value that can be clicked and typed (active: being typed)."""
	w = c.tw (s, "uib" if active else "ui") + 16; h = 22
	c.rect (x - w / 2, y - h / 2, w, h, WHITE, r = 5, outline = col if active else (150, 154, 164), width = 2 if active else 1)
	c.text_c (x - w / 2, y - h / 2, w, h, s, "uib" if active else "ui", TEXT)
	if active: c.vline (x + c.tw (s, "uib") / 2 + 2, y - 7, y + 7, TEXT)

def arrow (c, p0, p1, col, width = 2.4, knob = True, head = 11):
	dx, dy = p1[0] - p0[0], p1[1] - p0[1]; L = math.hypot (dx, dy) or 1; ux, uy = dx / L, dy / L
	c.line ([p0, (p1[0] - ux * head * 0.8, p1[1] - uy * head * 0.8)], col, width)
	c.poly ([p1, (p1[0] - ux * head - uy * head * 0.45, p1[1] - uy * head + ux * head * 0.45), (p1[0] - ux * head + uy * head * 0.45, p1[1] - uy * head - ux * head * 0.45)], col)
	if knob: c.ellipse (p0[0], p0[1], 4.5, WHITE, col, 2)

def dim (c, a, b, off, s, col = (92, 98, 112)):
	"""A dimension line between two points of the screen, moved aside by off (a vector)."""
	a2, b2 = (a[0] + off[0], a[1] + off[1]), (b[0] + off[0], b[1] + off[1])
	for p, q in ((a, a2), (b, b2)): c.line ([(p[0] + off[0] * 0.15, p[1] + off[1] * 0.15), (q[0] + off[0] * 0.12, q[1] + off[1] * 0.12)], col, 0.7)
	arrow (c, a2, b2, col, 0.9, False, 7); arrow (c, b2, a2, col, 0.9, False, 7)
	tag (c, (a2[0] + b2[0]) / 2, (a2[1] + b2[1]) / 2, s)

# ---- the part: a bracket, made as the app will make it ---------------------------------------------------------
mf.set_circular_segments (96)
def O (m): return m.as_original ()
def box (x, y, z, w, d, h): return O (mf.Manifold.cube ([w, d, h]).translate ([x, y, z]))
def cyl_z (x, y, z0, z1, r, r2 = None): return O (mf.Manifold.cylinder (z1 - z0, r, r if r2 is None else r2).translate ([x, y, z0]))
def cyl_y (x, z, y0, y1, r): return O (mf.Manifold.cylinder (y1 - y0, r, r).rotate ([-90, 0, 0]).translate ([x, y0, z]))
def cyl_x (y, z, x0, x1, r): return O (mf.Manifold.cylinder (x1 - x0, r, r).rotate ([0, 90, 0]).translate ([x0, y, z]))
def rounded (w, d, r): return mf.CrossSection.square ([w - 2 * r, d - 2 * r]).translate ([r, r]).offset (r, mf.JoinType.Round, 2.0, 96)

PW, PD, PT, WT, WH = 90, 60, 8, 8, 45		# the plate, the wall's thickness, the height
def stage (n):
	"""The part after the n first features of its history."""
	plate = box (0, 0, 0, PW, PD, PT)
	if n >= 2: plate = O (rounded (PW, PD, 8).extrude (PT))				# Fillet 1: the plate's four corners
	m = plate
	if n >= 3:
		wall = box (0, PD - WT, 0, PW, WT, WH)
		if n >= 4:										# Fillet 2: the wall's two top corners
			prof = rounded (PW, WH + 30, 12).translate ([0, -30]) ^ mf.CrossSection.square ([PW, WH])
			wall = O (prof.extrude (WT).rotate ([90, 0, 0]).translate ([0, PD, 0]))
		m = m + wall
	if n >= 5: m = m - cyl_y (45, 27, PD - WT - 1, PD + 1, 11)					# Cylinder 1: the wall's hole
	if n >= 7:										# Sketch 1 + Extrude 1: two holes and a slot
		slot = mf.CrossSection.batch_hull ([mf.CrossSection.circle (5).translate ([36, 22]), mf.CrossSection.circle (5).translate ([54, 22])])
		m = m - cyl_z (16, 22, -1, PT + 1, 4.5) - cyl_z (74, 22, -1, PT + 1, 4.5) - O (slot.extrude (PT + 2).translate ([0, 0, -1]))
	if n >= 8:										# Chamfer 1: the two holes' rims
		for hx in (16, 74): m = m - cyl_z (hx, 22, PT - 2.5, PT + 0.01, 4.5, 7.0)
	if n >= 9:										# Fillet 3: where the wall meets the plate
		y0 = PD - WT; m = m + (box (0, y0 - 6, PT, PW, 6.01, 6) - cyl_x (y0 - 6, PT + 6, -1, PW + 1, 6))
	return m

HISTORY = [("box", "Box 1", "90 × 60 × 8"), ("fillet", "Fillet 1", "R 8 · 4 edges"), ("box", "Box 2", "90 × 8 × 37"), ("fillet", "Fillet 2", "R 12 · 2 edges"),
	   ("cyl", "Cylinder 1", "Ø 22 · cut"), ("sketch", "Sketch 1", "3 outlines"), ("extrude", "Extrude 1", "through · cut"),
	   ("chamfer", "Chamfer 1", "2.5 · 2 edges"), ("fillet", "Fillet 3", "R 6 · 1 edge")]

# ---- the window: the toolbar, the panels, the status bar ------------------------------------------------------
WX, WY, WW, WH_ = 8, 32, 1008, 730
TOOLS = [("box", "Box"), ("cyl", "Cylinder"), ("sketch", "Sketch"), ("extrude", "Extrude"), None, ("fillet", "Fillet"), ("chamfer", "Chamfer"), ("move", "Move"), None,
	 ("union", "Union"), ("subtract", "Subtract"), ("intersect", "Intersect"), None, ("measure", "Measure")]
SKTOOLS = [("line", "Line"), ("rect", "Rectangle"), ("circle", "Circle"), ("arc", "Arc"), ("close", "Close"), None, ("measure", "Measure")]

def shell (c, title, tool = None, sketch = False, hint = "", tris = 0, dirty = False):
	desktop (c, "3DForge", ["File", "Edit", "View", "Create", "Modify", "Help"])
	cx, cy, cw, ch = window (c, WX, WY, WW, WH_, "3DForge — " + title)
	# the toolbar: the document, undo / redo, then the tools (a picture, its name)
	x = cx + 10; y = cy + 4
	for i, k in enumerate (("new", "open", "save", None, "undo", "redo")):
		if k is None: c.vline (x + 3, y + 12, y + 44, LINE2); x += 9; continue
		icon (c, k, x + 5, y + 18, 20, INK if k != "redo" else FAINT); x += 31
	c.vline (x + 3, y + 6, y + 50, LINE2); x += 12
	for t in (SKTOOLS if sketch else TOOLS):
		if t is None: c.vline (x + 4, y + 6, y + 50, LINE2); x += 10; continue
		k, name = t; bw = max (54, c.tw (name, "small") + 14)
		if k == tool: c.rect (x, y + 1, bw, 54, SEL_SOFT, r = 6, outline = ACC)
		icon (c, k, x + bw / 2 - 13, y + 5, 26, bg = SEL_SOFT if k == tool else FACE)
		c.text_c (x, y + 34, bw, 16, name, "small", TEXT); x += bw + 3
	if sketch:
		button (c, cx + cw - 232, y + 14, 84, 28, "Cancel"); button (c, cx + cw - 140, y + 14, 130, 28, "Finish sketch", accent = True)
	else:
		bx = cx + cw - 108; button (c, bx, y + 14, 98, 28, "", accent = True)
		icon (c, "export", bx + 10, y + 19, 18, WHITE, WHITE); c.text_l (bx + 36, y + 14, 28, "Export", "uib", WHITE)
	c.hline (cx, cx + cw, cy + 62, LINE2)
	# the status bar: what to do now, then the grid, the snapping, the unit
	sy = cy + ch - 27; c.hline (cx, cx + cw, sy, LINE2)
	icon (c, "info", cx + 10, sy + 5, 17, ACC); c.text_l (cx + 33, sy, 27, hint, "ui", (56, 58, 66))
	rx = cx + cw - 12
	for s in ("mm", "Snap 1 mm", "Grid 10 mm", "{:,} triangles".format (tris).replace (",", " ")):
		c.text_r (rx, sy, 27, s, "small", DIM); rx -= c.tw (s, "small") + 12; c.vline (rx + 5, sy + 7, sy + 20, LINE2); rx -= 7
	left = (cx + 8, cy + 70, 196, ch - 70 - 35); right = (cx + cw - 224, cy + 70, 216, ch - 70 - 35)
	view = (cx + 212, cy + 70, cw - 212 - 232, ch - 70 - 35)
	return left, view, right

def viewport (c, rect, cam, caption = None, top = False):
	x, y, w, h = rect
	c.grad (x, y, w, h, (247, 248, 250), (226, 230, 237), r = 5); c.rect (x, y, w, h, r = 5, outline = (150, 152, 160))
	return x, y, w, h

def viewport_chrome (c, rect, az, el, caption = None, top = False):
	x, y, w, h = rect
	if top:
		c.rect (x + w - 78, y + 12, 64, 64, (252, 252, 253), r = 4, outline = (140, 146, 158)); c.text_c (x + w - 78, y + 12, 64, 64, "TOP", "smallb", DIM)
	else: view_cube (c, x + w - 56, y + 58, az, el)
	bx = x + w - 40; by = y + 124
	c.rect (bx, by, 30, 88, A (WHITE, 215), r = 6, outline = (176, 180, 190))
	for i, k in enumerate (("home", "fit", "shaded")): icon (c, k, bx + 6, by + 6 + i * 28, 18, (70, 76, 90))
	triad (c, x + 34, y + h - 34, az, el) if not top else None
	if caption:
		cw = c.tw (caption, "small") + 22; c.rect (x + 10, y + 10, cw, 24, A (WHITE, 225), r = 12, outline = (176, 180, 190)); c.text_c (x + 10, y + 10, cw, 24, caption, "small", (60, 64, 76))
	c.rect (x, y, w, h, r = 5, outline = (150, 152, 160))

def left_panel (c, rect, n, sel = None, bodies = (("Bracket", BODY, True),), sketch = None):
	x, y, w, h = rect
	c.text (x + 4, y + 2, "Bodies", "uib"); listbox (c, x, y + 22, w, 26 * max (2, len (bodies)) + 6)
	for i, (name, col, on) in enumerate (bodies):
		ry = y + 25 + i * 26
		icon (c, "eye", x + 6, ry + 4, 18, INK if on else FAINT); c.rect (x + 30, ry + 6, 14, 14, col, r = 3, outline = shade (col, 0.6)); c.text_l (x + 52, ry, 26, name)
	y2 = y + 22 + 26 * max (2, len (bodies)) + 18
	c.text (x + 4, y2, "History", "uib"); c.text (x + w - 4, y2 + 2, "{} steps".format (n), "small", DIM, "ra")
	hh = h - (y2 - y) - 22; listbox (c, x, y2 + 20, w, hh)
	for i, (k, name, det) in enumerate (HISTORY):
		ry = y2 + 24 + i * 34
		if i >= n:
			if i == n: c.hline (x + 6, x + w - 6, ry - 2, ACC, 2); c.poly ([(x + 2, ry - 6), (x + 9, ry - 1), (x + 2, ry + 4)], ACC)
			continue
		if i == sel: c.rect (x + 3, ry, w - 6, 32, SEL_SOFT, r = 4)
		icon (c, k, x + 9, ry + 6, 20, bg = SEL_SOFT if i == sel else FIELD)
		c.text (x + 38, ry + 3, name, "uib" if i == sel else "ui"); c.text (x + 38, ry + 18, det, "tiny", DIM)
	if n >= len (HISTORY):
		ry = y2 + 24 + n * 34; c.hline (x + 6, x + w - 6, ry + 2, ACC, 2); c.poly ([(x + 2, ry - 2), (x + 9, ry + 3), (x + 2, ry + 8)], ACC)

def prow (c, x, y, w, label, value, unit = None, focus = False, link = False, fw = 96):
	c.text_l (x, y, 26, label, "ui", (50, 52, 60)); field (c, x + w - fw, y, fw, 26, value, unit, focus, align = "l" if link else "r", link = link)

def op_list (c, x, y, w, sel):
	"""New body / Union / Subtract / Intersect: one choice."""
	c.text (x, y, "Operation", "uib"); listbox (c, x, y + 20, w, 4 * 28 + 6)
	for i, (k, name) in enumerate ((("newbody", "New body"), ("union", "Union"), ("subtract", "Subtract"), ("intersect", "Intersect"))):
		ry = y + 23 + i * 28
		if i == sel: c.rect (x + 3, ry, w - 6, 28, ACC, r = 4)
		icon (c, k, x + 10, ry + 4, 20, WHITE if i == sel else INK, WHITE if i == sel else ACC, ACC if i == sel else FIELD)
		c.text_l (x + 40, ry, 28, name, "uib" if i == sel else "ui", WHITE if i == sel else TEXT)
	return y + 20 + 4 * 28 + 6

def tool_head (c, rect, kind, name, steps = None, at = 0):
	x, y, w, h = rect; group (c, x, y, w, h)
	icon (c, kind, x + 12, y + 12, 26, bg = PANEL); c.text (x + 48, y + 11, name, "big");
	if steps:
		sx = x + 48
		for i, s in enumerate (steps):
			col = ACC if i == at else (GREEN if i < at else FAINT)
			c.text (sx, y + 31, ("✓ " if i < at else "") + s, "small", col); sx += c.tw (("✓ " if i < at else "") + s, "small") + 10
	c.hline (x + 10, x + w - 10, y + 52, (196, 196, 200))
	return x + 12, y + 64, w - 24

def ok_cancel (c, rect, ok = "OK"):
	x, y, w, h = rect; button (c, x + 12, y + h - 40, 90, 28, "Cancel"); button (c, x + w - 102, y + h - 40, 90, 28, ok, accent = True)

AZ, EL = -58, 28
def cam3d (view, scale = 4.15, target = (45, 30, 15)): return Cam (view, AZ, EL, scale, target)

# ---- the mock-ups ------------------------------------------------------------------------------------------------
def shot_main (export = False):
	c = Canvas (); m = stage (9); tris = len (m.to_mesh ().tri_verts)
	left, view, right = shell (c, "bracket.3df", None, hint = "Pick a tool, or click a body, a face or an edge. Drag turns the view, the wheel zooms.", tris = tris)
	cam = cam3d (view); viewport (c, view, cam); ground (c, cam); draw_body (c, cam, m); viewport_chrome (c, view, AZ, EL)
	left_panel (c, left, 9)
	x, y, w, h = right; group (c, x, y, w, h)
	c.rect (x + 12, y + 14, 22, 22, BODY, r = 4, outline = shade (BODY, 0.6)); c.text (x + 44, y + 11, "Bracket", "big"); c.text (x + 44, y + 31, "Body · 9 steps", "small", DIM)
	c.hline (x + 10, x + w - 10, y + 52, (196, 196, 200))
	px, py, pw = x + 12, y + 64, w - 24
	c.text_l (px, py, 26, "Name", "ui", (50, 52, 60)); field (c, px + pw - 122, py, 122, 26, "Bracket"); py += 34
	c.text_l (px, py, 26, "Colour", "ui", (50, 52, 60))
	for i, col in enumerate ((BODY, (196, 200, 208), (214, 170, 120), (150, 190, 150), (212, 132, 124))):
		c.rect (px + pw - 122 + i * 25, py + 3, 20, 20, col, r = 4, outline = shade (col, 0.6))
		if i == 0: c.rect (px + pw - 124 + i * 25, py + 1, 24, 24, r = 6, outline = ACC, width = 2)
	py += 42; c.text (px, py, "Measures", "uib"); py += 22
	vol = m.volume () / 1000.0; area = m.surface_area () / 100.0
	for k, v in (("Size", "90 × 60 × 45 mm"), ("Volume", "{:.1f} cm³".format (vol)), ("Surface", "{:.0f} cm²".format (area)), ("Triangles", "{:,}".format (tris).replace (",", " "))):
		c.text_l (px, py, 22, k, "ui", DIM); c.text_r (px + pw, py, 22, v); py += 23
	py += 14; c.text (px, py, "Display", "uib"); py += 24
	for s, on in (("Edges", True), ("Grid", True), ("Shadow", True), ("See through", False)): checkbox (c, px, py, s, on); py += 25
	py += 12; c.text (px, py, "Print check", "uib"); py += 24
	icon (c, "check", px, py - 1, 18, GREEN); c.text_l (px + 24, py, 16, "Closed solid, ready to print", "ui", (40, 110, 56))
	if not export: c.save ("3dforge-main.png"); return
	# the Export dialog
	dw, dh = 440, 416; dx, dy = (W - dw) // 2, 176
	c.rect (0, 27, W, H - 27, A ((20, 28, 44), 70))
	fx, fy, fw, fh = window (c, dx, dy, dw, dh, "Export", resizable = False, menu = False)
	x = fx + 20; y = fy + 16; w = fw - 40
	c.text_l (x, y, 28, "Format"); segmented (c, x + 110, y, 200, 28, ["STL", "OBJ"], 0); y += 40
	c.text_l (x, y, 28, "What"); segmented (c, x + 110, y, w - 110, 28, ["Whole part", "Selected body"], 0); y += 40
	c.text_l (x, y, 28, "Curves"); segmented (c, x + 110, y, w - 110, 28, ["Draft", "Fine", "Very fine"], 1); y += 34
	c.text (x + 110, y, "96 sides to a circle · at most 0.05 mm off the curve", "tiny", DIM); y += 26
	checkbox (c, x + 110, y, "Binary file (smaller)", True); y += 34
	c.text_l (x, y, 28, "Name"); field (c, x + 110, y, w - 110, 28, "bracket.stl"); y += 38
	c.text_l (x, y, 28, "Folder"); field (c, x + 110, y, w - 110 - 86, 28, "SD:/docs/3d"); button (c, x + w - 78, y, 78, 28, "Browse…"); y += 44
	c.rect (x, y, w, 34, PANEL, r = 6, outline = (190, 190, 194)); icon (c, "check", x + 10, y + 8, 18, GREEN)
	c.text_l (x + 36, y, 34, "{:,} triangles · {:.0f} KB · closed solid".format (tris, (84 + 50 * tris) / 1024).replace (",", " "), "ui", (50, 52, 60))
	button (c, fx + fw - 220, fy + fh - 44, 92, 28, "Cancel"); button (c, fx + fw - 118, fy + fh - 44, 98, 28, "Export", accent = True)
	c.save ("3dforge-export.png")

def shot_box ():
	c = Canvas (); m = stage (2); tris = len (m.to_mesh ().tri_verts)
	left, view, right = shell (c, "bracket.3df •", "box", hint = "Move up to set the height and click — or type it and press Enter.", tris = tris)
	cam = cam3d (view); viewport (c, view, cam); ground (c, cam); draw_body (c, cam, m)
	y0 = PD - WT
	draw_body (c, cam, box (0, y0, PT, PW, WT, 37), base = (96, 160, 236), alpha = 196, shadow = False, edge = (30, 84, 170))
	f = cam.xy
	c.poly ([f ((0, y0, PT)), f ((PW, y0, PT)), f ((PW, PD, PT)), f ((0, PD, PT))], None, ACC, 2)
	dim (c, f ((0, y0, PT)), f ((PW, y0, PT)), (-12, 26), "90"); dim (c, f ((PW, y0, PT)), f ((PW, PD, PT)), (34, 8), "8")
	top = f ((45, y0 + 4, WH)); tip = (top[0], top[1] - 64); arrow (c, top, tip, AMBER)
	c.dash (f ((PW, PD, PT)), f ((PW, PD, WH)), A (AMBER, 255), 1.2); tag (c, tip[0] + 52, tip[1] + 20, "37 mm", True, AMBER); cursor (c, tip[0] + 4, tip[1] + 8)
	viewport_chrome (c, view, AZ, EL, "Box 2 · on the top face of Box 1")
	left_panel (c, left, 2); x, y, w, h = left
	px, py, pw = tool_head (c, right, "box", "Box 2", ("Base", "Height"), 1)
	prow (c, px, py, pw, "Width", "90", "mm"); py += 32; prow (c, px, py, pw, "Depth", "8", "mm"); py += 32; prow (c, px, py, pw, "Height", "37", "mm", True); py += 44
	py = op_list (c, px, py, pw, 1) + 12
	c.text_l (px, py, 20, "Joined to", "ui", DIM); c.rect (px + pw - 112, py + 3, 14, 14, BODY, r = 3, outline = shade (BODY, 0.6)); c.text_l (px + pw - 92, py, 20, "Bracket"); py += 30
	checkbox (c, px, py, "Centred on the first click", False)
	ok_cancel (c, right); c.save ("3dforge-box.png")

def shot_subtract ():
	c = Canvas (); m = stage (4); tris = len (m.to_mesh ().tri_verts)
	left, view, right = shell (c, "bracket.3df •", "cyl", hint = "Pushed into the body: Subtract is chosen. Click to finish.", tris = tris)
	cam = cam3d (view); viewport (c, view, cam); ground (c, cam); draw_body (c, cam, m, pick = [(20, PD - WT, 30)], tint = (120, 168, 232))
	y0 = PD - WT; f = cam.xy
	draw_body (c, cam, cyl_y (45, 27, y0 - 10, PD + 6, 11), base = (236, 104, 88), alpha = 150, shadow = False, edge = (170, 44, 34))
	p0 = f ((45, y0 - 10, 27)); p1 = f ((45, y0 - 34, 27)); arrow (c, p1, (p0[0] - 3, p0[1] + 2), RED)
	q = f ((45, y0 - 10, 38)); tag (c, q[0] - 6, q[1] - 20, "Ø 22"); tag (c, p1[0] - 54, p1[1] + 6, "through", True, RED); cursor (c, p1[0] + 2, p1[1] + 6)
	viewport_chrome (c, view, AZ, EL, "Cylinder 1 · on the front face of Box 2")
	left_panel (c, left, 4)
	px, py, pw = tool_head (c, right, "cyl", "Cylinder 1", ("Circle", "Depth"), 1)
	prow (c, px, py, pw, "Diameter", "22", "mm"); py += 32; prow (c, px, py, pw, "Depth", "8", "mm", True); py += 34
	checkbox (c, px, py, "Through the whole body", True); py += 34
	py = op_list (c, px, py, pw, 2) + 12
	c.text_l (px, py, 20, "Cut from", "ui", DIM); c.rect (px + pw - 112, py + 3, 14, 14, BODY, r = 3, outline = shade (BODY, 0.6)); c.text_l (px + pw - 92, py, 20, "Bracket")
	ok_cancel (c, right); c.save ("3dforge-subtract.png")

def shot_fillet ():
	c = Canvas (); m = stage (9); tris = len (m.to_mesh ().tri_verts)
	left, view, right = shell (c, "bracket.3df •", "fillet", hint = "Click the edges to round, then drag the arrow or type the radius.", tris = tris)
	cam = cam3d (view); viewport (c, view, cam); ground (c, cam)
	y0 = PD - WT; s = 6 * math.sin (math.pi / 4); p = (30, y0 - 6 + s, PT + 6 - s)
	draw_body (c, cam, m, pick = [p]); f = cam.xy
	a = f ((45, y0 - 6 + s, PT + 6 - s)); b = f ((45, y0 - 6 + s - 13, PT + 6 - s + 13)); arrow (c, a, b, AMBER)
	tag (c, b[0] - 40, b[1] - 8, "R 6", True, AMBER); cursor (c, b[0] + 3, b[1] + 6)
	viewport_chrome (c, view, AZ, EL, "Fillet 3 · 1 edge")
	left_panel (c, left, 8)
	px, py, pw = tool_head (c, right, "fillet", "Fillet 3")
	segmented (c, px, py, pw, 28, ["Fillet", "Chamfer"], 0); py += 40
	prow (c, px, py, pw, "Radius", "6", "mm", True); py += 42
	c.text (px, py, "Edges", "uib"); c.text (px + pw, py + 2, "1 chosen", "small", DIM, "ra"); listbox (c, px, py + 20, pw, 90)
	c.rect (px + 3, py + 23, pw - 6, 28, SEL_SOFT, r = 4); c.line ([(px + 12, py + 44), (px + 12, py + 37), (px + 26, py + 30)], ACC, 2)
	c.text_l (px + 36, py + 23, 28, "Box 1 / Box 2"); c.text_r (px + pw - 10, py + 23, 28, "90 mm", "small", DIM)
	c.text_c (px, py + 56, pw, 50, "Click another edge to add it", "small", FAINT); py += 124
	checkbox (c, px, py, "Follow the edges that go on", True); py += 34
	c.rect (px, py, pw, 62, mix (PANEL, WHITE, 0.45), r = 6, outline = (196, 196, 200))
	c.text (px + 10, py + 8, "Straight edges and edges on a", "small", DIM); c.text (px + 10, py + 24, "circle can be rounded; the others", "small", DIM); c.text (px + 10, py + 40, "show in grey when you point them.", "small", DIM)
	ok_cancel (c, right); c.save ("3dforge-fillet.png")

def shot_sketch ():
	c = Canvas (); m = stage (5); tris = len (m.to_mesh ().tri_verts)
	left, view, right = shell (c, "bracket.3df •", "arc", True, hint = "Arc: click the centre, then where it ends. It starts at the end of Line 2.", tris = tris)
	cam = Cam (view, -90, 90, 5.3, (45, 27, PT)); viewport (c, view, cam)
	x, y, w, h = view
	for i in range (-100, 200, 5):
		gx, gy = cam.xy ((i, i, 0)); col = (212, 216, 224) if i % 10 else (190, 196, 206)
		if x + 2 < gx < x + w - 2: c.line ([(gx, y + 1), (gx, y + h - 1)], col, 0.6)
		if y + 2 < gy < y + h - 2: c.line ([(x + 1, gy), (x + w - 1, gy)], col, 0.6)
	draw_body (c, cam, m, shadow = False, fade = 0.55, bg = (236, 239, 244)); f = lambda px, py: cam.xy ((px, py, PT)); S = cam.s
	fillc = A (ACC, 62); ln = (30, 92, 186)
	for hx in (16, 74):
		p = f (hx, 22); c.ellipse (p[0], p[1], 4.5 * S, fillc, ln, 1.8); c.ellipse (p[0], p[1], 2.5, ln)
	# the slot: Line 1, Arc 1, Line 2, Arc 2 (being drawn: it comes back on Line 1's start and closes the outline)
	a, b, d, e = f (36, 17), f (54, 17), f (54, 27), f (36, 27); cl, cr = f (36, 22), f (54, 22); r = 5 * S
	c.rect (a[0], d[1], b[0] - a[0], a[1] - d[1], fillc); c.d.pieslice ([(cr[0] - r) * K, (cr[1] - r) * K, (cr[0] + r) * K, (cr[1] + r) * K], -90, 90, fill = fillc)
	c.d.pieslice ([(cl[0] - r) * K, (cl[1] - r) * K, (cl[0] + r) * K, (cl[1] + r) * K], 90, 270, fill = fillc)
	c.line ([a, b], ln, 1.8); c.arc (cr[0], cr[1], r, -90, 90, ln, 1.8); c.line ([d, e], ln, 1.8); c.arc (cl[0], cl[1], r, 90, 270, AMBER, 2.6)
	c.dash (cl, e, AMBER, 1); c.dash (cl, a, AMBER, 1)
	for p in (b, d, e, cr): c.ellipse (p[0], p[1], 3.2, WHITE, ln, 1.6)
	c.ellipse (cl[0], cl[1], 3.2, WHITE, AMBER, 1.8); c.ellipse (a[0], a[1], 6.5, None, GREEN, 2); c.ellipse (a[0], a[1], 3.2, WHITE, GREEN, 1.8)
	# the dimensions: from the plate's edges, then each element's own values
	dim (c, f (0, 22), f (16, 22), (0, 66), "16"); dim (c, f (16, 0), f (16, 22), (-110, 0), "22"); dim (c, f (16, 22), f (36, 22), (0, 66), "20")
	dim (c, a, b, (0, 40), "18"); tag (c, f (16, 22)[0], f (16, 22)[1] - 4.5 * S - 18, "Ø 9"); tag (c, f (74, 22)[0], f (74, 22)[1] - 4.5 * S - 18, "Ø 9")
	tag (c, cr[0] + r + 30, cr[1], "R 5"); tag (c, cl[0] - 4, cl[1] - r - 20, "180°", True, AMBER)
	c.text (a[0] - 12, a[1] + 14, "closes the outline", "small", (40, 120, 56), "ra"); cursor (c, a[0] + 3, a[1] + 4)
	viewport_chrome (c, view, -90, 90, "Sketch 1 · on the top face of Box 1", top = True)
	# at the left: the sketch's elements, in the order they were drawn
	x, y, w, h = left; c.text (x + 4, y + 2, "Sketch 1", "uib"); c.text (x + w - 4, y + 4, "6 elements", "small", DIM, "ra"); listbox (c, x, y + 22, w, h - 22 - 96)
	els = [("circle", "Circle 1", "Ø 9 · at 16, 22"), ("circle", "Circle 2", "Ø 9 · at 74, 22"), ("line", "Line 1", "18 · at 0°"), ("arc", "Arc 1", "R 5 · 180°"), ("line", "Line 2", "18 · at 180°"), ("arc", "Arc 2", "R 5 · 180°")]
	for i, (k, name, det) in enumerate (els):
		ry = y + 26 + i * 34
		if i == 5: c.rect (x + 3, ry, w - 6, 32, SEL_SOFT, r = 4)
		icon (c, k, x + 9, ry + 6, 20, bg = SEL_SOFT if i == 5 else FIELD); c.text (x + 38, ry + 3, name, "uib" if i == 5 else "ui"); c.text (x + 38, ry + 18, det, "tiny", DIM)
	gy = y + h - 84; group (c, x, gy, w, 84, "Outlines")
	icon (c, "check", x + 11, gy + 31, 17, GREEN); c.text_l (x + 34, gy + 30, 18, "3 closed", "ui", (40, 110, 56)); c.text_l (x + 34, gy + 54, 18, "none open", "ui", DIM)
	px, py, pw = tool_head (c, right, "arc", "Arc 2", ("Centre", "End"), 1)
	prow (c, px, py, pw, "Starts at", "Line 2 · end", link = True, fw = 112); py += 32
	prow (c, px, py, pw, "Centre", "5 at 90°", link = False, fw = 112); py += 32
	prow (c, px, py, pw, "Radius", "5", "mm", fw = 112); py += 32
	prow (c, px, py, pw, "Sweep", "180", "°", True, fw = 112); py += 40
	c.text_l (px, py, 26, "Turns"); segmented (c, px + pw - 112, py, 112, 26, ["Left", "Right"], 1); py += 40
	checkbox (c, px, py, "Snap to points and angles", True); py += 26; checkbox (c, px, py, "Construction line", False); py += 38
	c.rect (px, py, pw, 78, mix (PANEL, WHITE, 0.45), r = 6, outline = (196, 196, 200))
	for i, s in enumerate (("Each element starts from a point", "and keeps its own values. Change", "one: what was drawn after it", "follows, nothing before moves.")): c.text (px + 10, py + 8 + i * 16, s, "small", DIM)
	x, y, w, h = right; button (c, x + 12, y + h - 40, w - 24, 28, "Next element")
	c.save ("3dforge-sketch.png")

if __name__ == "__main__":
	shot_main (); shot_main (True); shot_box (); shot_subtract (); shot_sketch (); shot_fillet ()
