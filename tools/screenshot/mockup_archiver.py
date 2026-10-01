#!/usr/bin/env python3
"""mockup_archiver.py -- the first mock-ups of Onyx's archive manager (zip, 7-zip; rar read-only).
See docs/archiver/README.md.

    python3 tools/screenshot/mockup_archiver.py  -> docs/archiver/mockups/archiver-*.png

The look is the one of today's apps (screenshots/irc.png, fileviewer.png): the Peach frame, the
beige faces, the white lists, the teal selection; the global menu bar at the top of the screen.
Everything is drawn at K times the size, then scaled down (anti-aliasing).
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname (os.path.abspath (__file__))
ROOT = os.path.dirname (os.path.dirname (HERE))
OUT = os.path.join (ROOT, "docs", "archiver", "mockups")
W, H, K = 1280, 800, 2
DJ = "/usr/share/fonts/truetype/dejavu/"
def _f (name, size): return ImageFont.truetype (DJ + name, size * K)
F = dict (ui = _f ("DejaVuSans.ttf", 13), uib = _f ("DejaVuSans-Bold.ttf", 13), small = _f ("DejaVuSans.ttf", 11),
	  smallb = _f ("DejaVuSans-Bold.ttf", 11), tiny = _f ("DejaVuSans-Bold.ttf", 7), title = _f ("DejaVuSans-Bold.ttf", 13),
	  big = _f ("DejaVuSans-Bold.ttf", 16), mono = _f ("DejaVuSansMono.ttf", 12), menu = _f ("DejaVuSans.ttf", 13),
	  menub = _f ("DejaVuSans-Bold.ttf", 13))

# ---- the palette (sampled from screenshots/irc.png) ------------------------------------------------
BACK, BACK2 = (146, 160, 178), (110, 124, 144)
FACE = (208, 194, 186); FACE2 = (220, 209, 202); LIST = (247, 245, 244); HEAD = (232, 224, 218)
LINE = (178, 164, 156); LINE2 = (226, 218, 212); TEXT = (32, 28, 26); DIM = (120, 108, 100); FAINT = (168, 156, 148)
PEACH = ((245, 200, 160), (238, 174, 122)); PEACH_EDGE = (166, 126, 92)
GREY = ((206, 206, 210), (170, 170, 174)); GREY_EDGE = (124, 124, 128)
SEL = (73, 146, 167); SEL_SOFT = (189, 211, 218); LINK = (40, 104, 128)
GREEN = (78, 160, 92); RED = (200, 74, 64); AMBER = (226, 160, 58); BLUE = (74, 128, 200)
MENU = (247, 243, 239)

def lighten (c, k): return tuple (int (v + (255 - v) * k) for v in c[:3])
def shade (c, k): return tuple (int (v * k) for v in c[:3])
def mix (a, b, t): return tuple (int (a[i] + (b[i] - a[i]) * t) for i in range (3))
def A (c, a): return tuple (c[:3]) + (a,)

class Canvas:
	def __init__ (self):
		self.img = Image.new ("RGB", (W * K, H * K), BACK); self.d = ImageDraw.Draw (self.img, "RGBA")
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
		self.d.line ([(px * K, py * K) for px, py in pts], fill = fill, width = max (1, int (width * K)), joint = "curve")
	def hline (self, x0, x1, y, fill, width = 1): self.rect (x0, y, x1 - x0, width, fill)
	def vline (self, x, y0, y1, fill, width = 1): self.rect (x, y0, width, y1 - y0, fill)
	def dashed (self, x, y, w, h, fill, dash = 6, r = 6):
		self.rect (x, y, w, h, A (fill, 30), r = r)
		for (x0, y0, x1, y1) in [(x, y, x + w, y), (x, y + h, x + w, y + h), (x, y, x, y + h), (x + w, y, x + w, y + h)]:
			n = int (max (abs (x1 - x0), abs (y1 - y0)) / dash)
			for i in range (0, n, 2):
				t0, t1 = i / n, min (1, (i + 1) / n)
				self.line ([(x0 + (x1 - x0) * t0, y0 + (y1 - y0) * t0), (x0 + (x1 - x0) * t1, y0 + (y1 - y0) * t1)], fill, 2)
	def ellipse (self, cx, cy, r, fill = None, outline = None, width = 1):
		self.d.ellipse ([(cx - r) * K, (cy - r) * K, (cx + r) * K, (cy + r) * K], fill = fill, outline = outline, width = int (width * K))
	def poly (self, pts, fill): self.d.polygon ([(px * K, py * K) for px, py in pts], fill = fill)
	def tw (self, s, font = "ui"): return F[font].getlength (s) / K
	def text (self, x, y, s, font = "ui", fill = TEXT, anchor = "la"):
		self.d.text ((x * K, y * K), s, font = F[font], fill = fill, anchor = anchor)
	def text_l (self, x, y, h, s, font = "ui", fill = TEXT): self.text (x, y + h / 2, s, font, fill, "lm")
	def text_c (self, x, y, w, h, s, font = "ui", fill = TEXT): self.text (x + w / 2, y + h / 2, s, font, fill, "mm")
	def text_r (self, x, y, h, s, font = "ui", fill = TEXT): self.text (x, y + h / 2, s, font, fill, "rm")
	def shadow (self, x, y, w, h): pass			# no drop shadows in Onyx (a crisp outline instead)
	def save (self, name):
		os.makedirs (OUT, exist_ok = True); p = os.path.join (OUT, name)
		self.img.resize ((W, H), Image.LANCZOS).save (p); print ("wrote", os.path.relpath (p, ROOT))

# ---- the desktop ---------------------------------------------------------------------------------------
def desktop (c, app, menus, open_menu = None):
	c.grad (0, 0, W, H, BACK, BACK2)
	c.rect (0, 0, W, 26, MENU); c.hline (0, W, 26, LINE)
	x = 14
	c.text_l (x, 0, 26, app, "menub"); x += c.tw (app, "menub") + 26
	pos = {}
	for m in menus:
		if m == open_menu: c.rect (x - 8, 2, c.tw (m, "menu") + 16, 22, SEL, r = 4)
		c.text_l (x, 0, 26, m, "menu", (255, 255, 255) if m == open_menu else TEXT); pos[m] = x - 8; x += c.tw (m, "menu") + 24
	c.text_r (W - 14, 0, 26, "12:34", "menu")
	# the volume and the Wi-Fi glyphs
	c.poly ([(W - 104, 10), (W - 100, 10), (W - 95, 6), (W - 95, 20), (W - 100, 16), (W - 104, 16)], TEXT)
	for r in (4, 7): c.d.arc ([(W - 94 - r) * K, (13 - r) * K, (W - 94 + r) * K, (13 + r) * K], -45, 45, fill = TEXT, width = 2 * K)
	for i, r in enumerate ((3, 7, 11)): c.d.arc ([(W - 70 - r) * K, (19 - r) * K, (W - 70 + r) * K, (19 + r) * K], 225, 315, fill = TEXT, width = 2 * K)
	return pos

def window (c, x, y, w, h, title, active = True):
	"""An Onyx frame (Peach when active): returns the client area."""
	top, bot = PEACH if active else GREY
	edge = PEACH_EDGE if active else GREY_EDGE
	c.rect (x, y, w, h, top, r = 6)
	c.grad (x, y, w, 28, lighten (top, 0.2), bot, r = 6, corners = (True, True, False, False))
	c.rect (x + 4, y + 28, w - 8, h - 32, FACE)
	c.rect (x, y, w, h, r = 6, outline = edge)
	c.text_c (x, y, w, 28, title, "title", TEXT)
	def tbtn (bx, glyph):
		c.grad (bx, y + 5, 20, 18, lighten (top, 0.5), shade (top, 0.95), r = 4); c.rect (bx, y + 5, 20, 18, r = 4, outline = edge)
		glyph (bx, y + 5)
	tbtn (x + 6, lambda bx, by: c.rect (bx + 5, by + 8, 10, 3, TEXT, r = 1))
	tbtn (x + w - 74, lambda bx, by: c.rect (bx + 6, by + 11, 8, 3, TEXT))
	tbtn (x + w - 51, lambda bx, by: c.rect (bx + 5, by + 4, 10, 10, outline = TEXT, width = 2))
	tbtn (x + w - 28, lambda bx, by: (c.line ([(bx + 6, by + 5), (bx + 14, by + 13)], TEXT, 2), c.line ([(bx + 14, by + 5), (bx + 6, by + 13)], TEXT, 2)))
	return x + 4, y + 28, w - 8, h - 32

# ---- the widgets ---------------------------------------------------------------------------------------
def button (c, x, y, w, h, label, default = False, disabled = False, accent = False):
	face = SEL if accent else FACE
	c.grad (x, y, w, h, lighten (face, 0.5 if not accent else 0.15), shade (face, 0.96), r = 5)
	c.rect (x, y, w, h, r = 5, outline = shade (face, 0.62), width = 2 if default else 1)
	fg = (255, 255, 255) if accent else (FAINT if disabled else TEXT)
	c.text_c (x, y, w, h, label, "uib" if default or accent else "ui", fg)

def field (c, x, y, w, h, s = "", placeholder = "", font = "ui", caret = False, disabled = False):
	c.rect (x, y, w, h, FACE2 if disabled else (255, 255, 255), r = 4, outline = LINE)
	c.rect (x + 1, y + 1, w - 2, 3, A ((0, 0, 0), 14), r = 2)
	if s: c.text_l (x + 8, y, h, s, font, FAINT if disabled else TEXT)
	elif placeholder: c.text_l (x + 8, y, h, placeholder, font, FAINT)
	if caret: c.vline (x + 9 + c.tw (s, font), y + 5, y + h - 5, TEXT)

def dropdown (c, x, y, w, h, s, disabled = False):
	button (c, x, y, w, h, "", disabled = disabled)
	c.text_l (x + 10, y, h, s, "ui", FAINT if disabled else TEXT)
	ax, ay = x + w - 15, y + h / 2
	c.poly ([(ax - 5, ay - 2), (ax + 5, ay - 2), (ax, ay + 4)], FAINT if disabled else TEXT)

def checkbox (c, x, y, label, on = True, disabled = False):
	c.rect (x, y, 16, 16, (255, 255, 255), r = 3, outline = shade (FACE, 0.6))
	if on:
		c.rect (x, y, 16, 16, SEL if not disabled else FAINT, r = 3)
		c.line ([(x + 4, y + 8), (x + 7, y + 11), (x + 12, y + 5)], (255, 255, 255), 2)
	c.text_l (x + 24, y, 16, label, "ui", FAINT if disabled else TEXT)

def radio (c, x, y, label, on = False, hint = None):
	c.ellipse (x + 8, y + 8, 8, (255, 255, 255), shade (FACE, 0.6))
	if on: c.ellipse (x + 8, y + 8, 8, SEL); c.ellipse (x + 8, y + 8, 3, (255, 255, 255))
	c.text_l (x + 24, y, 16, label)
	if hint: c.text_l (x + 30 + c.tw (label), y, 16, hint, "small", DIM)

def segmented (c, x, y, h, items, sel):
	ws = [c.tw (s) + 28 for s in items]; tot = sum (ws)
	c.rect (x, y, tot, h, (255, 255, 255), r = 5, outline = shade (FACE, 0.62))
	cx = x
	for i, (s, w) in enumerate (zip (items, ws)):
		if i == sel: c.rect (cx + 2, y + 2, w - 4, h - 4, SEL, r = 4)
		elif i: c.vline (cx, y + 5, y + h - 5, LINE2)
		c.text_c (cx, y, w, h, s, "uib" if i == sel else "ui", (255, 255, 255) if i == sel else TEXT); cx += w
	return tot

def scrollbar (c, x, y, h, t0, t1):
	c.rect (x, y, 10, h, A (FACE, 120), r = 5)
	c.rect (x + 1, y + h * t0, 8, h * (t1 - t0), shade (FACE, 0.8), r = 4)

def progress (c, x, y, w, h, t, colour = SEL):
	c.rect (x, y, w, h, (255, 255, 255), r = h / 2, outline = LINE)
	c.grad (x + 1, y + 1, max (h, (w - 2) * t), h - 2, lighten (colour, 0.3), colour, r = (h - 2) / 2)

def group (c, x, y, w, h, title):
	c.rect (x, y, w, h, A ((255, 255, 255), 70), r = 6, outline = A (shade (FACE, 0.7), 255))
	c.text (x + 12, y + 9, title, "smallb", DIM)

# ---- the icons (vector, drawn at any size) ---------------------------------------------------------------
def ic_folder (c, x, y, s = 18, colour = (226, 180, 92)):
	c.rect (x, y + s * 0.12, s * 0.45, s * 0.25, shade (colour, 0.85), r = 2)
	c.rect (x, y + s * 0.22, s, s * 0.68, colour, r = 2)
	c.rect (x, y + s * 0.32, s, s * 0.58, lighten (colour, 0.22), r = 2)

KINDS = dict (cpp = (74, 128, 200), h = (120, 96, 196), png = (78, 160, 92), md = (96, 104, 116), mk = (190, 120, 60),
	      txt = (140, 140, 140), img = (200, 74, 64), ini = (150, 120, 90), pdf = (200, 60, 60), elf = (60, 60, 60))
def ic_file (c, x, y, s = 18, kind = "txt"):
	col = KINDS.get (kind, (140, 140, 140)); w = s * 0.78; fx = x + (s - w) / 2; f = s * 0.28
	c.poly ([(fx, y), (fx + w - f, y), (fx + w, y + f), (fx + w, y + s), (fx, y + s)], (255, 255, 255))
	c.line ([(fx, y), (fx + w - f, y), (fx + w, y + f), (fx + w, y + s), (fx, y + s), (fx, y)], shade (FACE, 0.55), 1)
	c.poly ([(fx + w - f, y), (fx + w - f, y + f), (fx + w, y + f)], LINE2)
	c.rect (fx - 1, y + s * 0.55, w * 0.9, s * 0.32, col, r = 1)

def ic_archive (c, x, y, s = 18, colour = (186, 136, 86)):
	"""A crate with a zipper: the archive."""
	c.rect (x, y + s * 0.1, s, s * 0.85, colour, r = s * 0.12)
	c.rect (x, y + s * 0.1, s, s * 0.25, lighten (colour, 0.25), r = s * 0.12, corners = (True, True, False, False))
	c.rect (x, y + s * 0.1, s, s * 0.85, r = s * 0.12, outline = shade (colour, 0.6))
	zx = x + s / 2
	for i in range (4): c.rect (zx - s * 0.08, y + s * (0.18 + i * 0.17), s * 0.16, s * 0.08, (245, 236, 210))
	c.rect (zx - s * 0.12, y + s * 0.75, s * 0.24, s * 0.13, (245, 236, 210), r = 1)

def tb_icon (c, kind, x, y, s, disabled = False):
	"""The toolbar's glyphs, 32 px."""
	g = lambda col: FAINT if disabled else col
	if kind == "open":
		ic_folder (c, x + 2, y + 2, s - 4, g ((226, 180, 92)))
	elif kind == "new":
		ic_archive (c, x + 3, y + 3, s - 8, g ((186, 136, 86)))
		c.ellipse (x + s - 8, y + s - 8, 7, g (GREEN)); c.rect (x + s - 12, y + s - 9, 8, 2, (255, 255, 255)); c.rect (x + s - 9, y + s - 12, 2, 8, (255, 255, 255))
	elif kind == "add":
		ic_file (c, x + 2, y + 2, s - 6, "txt")
		c.ellipse (x + s - 9, y + s - 9, 8, g (GREEN)); c.rect (x + s - 14, y + s - 10, 10, 3, (255, 255, 255)); c.rect (x + s - 10, y + s - 14, 3, 10, (255, 255, 255))
	elif kind in ("extract", "extractall"):
		ic_archive (c, x + 2, y + 9, s - 12, g ((186, 136, 86)))
		col = g (BLUE); ax = x + s - 8
		c.rect (ax - 2, y + 4, 5, 15, col); c.poly ([(ax - 7, y + 17), (ax + 8, y + 17), (ax + 0.5, y + 26)], col)
		if kind == "extractall":
			c.rect (ax - 10, y + 2, 4, 11, col); c.poly ([(ax - 13, y + 12), (ax - 3, y + 12), (ax - 8, y + 18)], col)
	elif kind == "delete":
		col = g (RED); c.rect (x + 7, y + 9, s - 14, s - 11, col, r = 3); c.rect (x + 4, y + 5, s - 8, 3, col, r = 1); c.rect (x + s / 2 - 4, y + 2, 8, 3, col, r = 1)
		for i in range (3): c.rect (x + 11 + i * 4, y + 13, 2, s - 19, (255, 255, 255))
	elif kind == "test":
		col = g (GREEN); c.poly ([(x + s / 2, y + 2), (x + s - 4, y + 7), (x + s - 6, y + s - 9), (x + s / 2, y + s - 2), (x + 6, y + s - 9), (x + 4, y + 7)], col)
		c.line ([(x + 10, y + 16), (x + 14, y + 20), (x + 22, y + 11)], (255, 255, 255), 3)
	elif kind == "info":
		col = g (BLUE); c.ellipse (x + s / 2, y + s / 2, s / 2 - 3, col)
		c.rect (x + s / 2 - 2, y + 13, 4, 11, (255, 255, 255)); c.rect (x + s / 2 - 2, y + 7, 4, 4, (255, 255, 255), r = 2)

def toolbar (c, x, y, w, read_only = False, hot = None):
	items = [("open", "Open"), ("new", "New"), None, ("add", "Add"), ("extract", "Extract"), ("extractall", "Extract All"),
		 ("delete", "Delete"), None, ("test", "Test"), ("info", "Properties")]
	c.rect (x, y, w, 62, FACE)
	bx = x + 8
	for it in items:
		if it is None: c.vline (bx + 3, y + 10, y + 52, shade (FACE, 0.82)); bx += 10; continue
		k, label = it
		bw = max (58, c.tw (label, "small") + 18)
		dis = read_only and k in ("add", "delete")
		if k == hot: c.rect (bx, y + 4, bw, 54, A ((255, 255, 255), 110), r = 6, outline = A (shade (FACE, 0.7), 255))
		tb_icon (c, k, bx + (bw - 32) / 2, y + 7, 32, dis)
		c.text_c (bx, y + 41, bw, 14, label, "small", FAINT if dis else TEXT)
		bx += bw + 2
	c.hline (x, x + w, y + 62, shade (FACE, 0.85))
	return bx

def badge (c, x, y, s, bg, fg = (255, 255, 255), font = "smallb"):
	w = c.tw (s, font) + 14; c.rect (x, y, w, 18, bg, r = 9); c.text_c (x, y, w, 18, s, font, fg); return w

def breadcrumb (c, x, y, w, parts, archive, fmt):
	c.rect (x, y, w, 30, (255, 255, 255), r = 6, outline = LINE)
	# back / up
	for i, g in enumerate (("<", "^")):
		bx = x + 4 + i * 28; c.rect (bx, y + 4, 24, 22, A (FACE, 90), r = 4)
		if g == "<": c.line ([(bx + 14, y + 9), (bx + 9, y + 15), (bx + 14, y + 21)], TEXT, 2)
		else: c.line ([(bx + 7, y + 18), (bx + 12, y + 11), (bx + 17, y + 18)], TEXT, 2)
	cx = x + 66
	ic_archive (c, cx, y + 6, 18); cx += 24
	c.text_l (cx, y, 30, archive, "uib", LINK); cx += c.tw (archive, "uib") + 8
	for i, p in enumerate (parts):
		c.line ([(cx + 2, y + 10), (cx + 7, y + 15), (cx + 2, y + 20)], DIM, 1.5); cx += 16
		last = i == len (parts) - 1
		c.text_l (cx, y, 30, p, "uib" if last else "ui", TEXT if last else LINK)
		if last: c.hline (cx, cx + c.tw (p, "uib"), y + 23, SEL, 2)
		cx += c.tw (p, "uib" if last else "ui") + 8
	# the search field
	field (c, x + w - 214, y + 4, 208, 22, "", "Search in the archive", "small")
	c.ellipse (x + w - 24, y + 14, 5, None, DIM, 1.5); c.line ([(x + w - 20, y + 18), (x + w - 16, y + 22)], DIM, 2)
	c.rect (x + w - 214 - c.tw (fmt, "smallb") - 24, y + 6, 1, 1)
	badge (c, x + w - 224 - c.tw (fmt, "smallb") - 14, y + 6, fmt, shade (SEL, 0.9))

# ---- the archive's contents ----------------------------------------------------------------------------
TREE = [(0, "Projet-Onyx.zip", "arch", True), (1, "docs", "dir", True), (2, "images", "dir", False), (1, "kernel", "dir", True),
	(2, "include", "dir", False), (2, "sys", "dir", "sel"), (3, "drivers", "dir", False), (1, "user", "dir", True),
	(2, "Apps", "dir", False), (2, "libc", "dir", False), (1, "tools", "dir", False)]
FILES = [("..", "up", "", "", None, "", ""),
	 ("drivers", "dir", "4 items", "", None, "28/09/2026 14:02", ""),
	 ("kapi.cpp", "cpp", "182.4 KB", "41.0 KB", 0.78, "28/09/2026 14:12", "Deflate"),
	 ("kapi_abi.h", "h", "31.7 KB", "7.9 KB", 0.75, "28/09/2026 14:12", "Deflate"),
	 ("kapitable.cpp", "cpp", "24.1 KB", "5.2 KB", 0.78, "27/09/2026 22:40", "Deflate"),
	 ("scheduler.cpp", "cpp", "61.7 KB", "14.9 KB", 0.76, "25/09/2026 09:15", "Deflate"),
	 ("memory.cpp", "cpp", "48.3 KB", "11.6 KB", 0.76, "25/09/2026 09:15", "Deflate"),
	 ("vfs.cpp", "cpp", "73.0 KB", "16.2 KB", 0.78, "21/09/2026 18:47", "Deflate"),
	 ("ramvol.cpp", "cpp", "12.8 KB", "3.4 KB", 0.73, "30/09/2026 11:03", "Deflate"),
	 ("boot_logo.png", "png", "48.2 KB", "48.2 KB", 0.0, "02/09/2026 16:30", "Store"),
	 ("Makefile", "mk", "6.4 KB", "1.9 KB", 0.70, "28/09/2026 14:12", "Deflate"),
	 ("README.md", "md", "9.1 KB", "3.8 KB", 0.58, "14/09/2026 10:21", "Deflate"),
	 ("kernel8-rpi4.img", "img", "2.1 MB", "1.0 MB", 0.52, "30/09/2026 23:58", "Deflate")]
COLS = [("Name", 236), ("Size", 84), ("Packed", 84), ("Ratio", 108), ("Modified", 140), ("Method", 70)]

def tree (c, x, y, w, h, rows = None, sel_dir = "sys", drop = None, root = "Projet-Onyx.zip"):
	rows = rows or [(l, root if k == "arch" else n, k, st) for l, n, k, st in TREE]
	c.rect (x, y, w, h, LIST, r = 4, outline = LINE)
	ry = y + 8
	for lvl, name, kind, st in rows:
		rx = x + 10 + lvl * 16
		if st == "sel" or name == sel_dir and st == "sel": c.rect (x + 4, ry - 1, w - 8, 24, SEL_SOFT, r = 4)
		if name == drop: c.dashed (x + 4, ry - 1, w - 8, 24, SEL)
		if kind != "file" and st is not False and st != "sel": c.line ([(rx - 4, ry + 8), (rx + 0, ry + 12), (rx + 4, ry + 8)], DIM, 1.5)
		elif kind == "dir" and st is False: c.line ([(rx - 2, ry + 6), (rx + 2, ry + 10), (rx - 2, ry + 14)], DIM, 1.5)
		elif st == "sel": c.line ([(rx - 4, ry + 8), (rx + 0, ry + 12), (rx + 4, ry + 8)], DIM, 1.5)
		if kind == "arch": ic_archive (c, rx + 10, ry + 2, 17)
		else: ic_folder (c, rx + 10, ry + 2, 17)
		c.text_l (rx + 34, ry, 22, name, "uib" if kind == "arch" or st == "sel" else "ui")
		ry += 26
	return ry

def file_list (c, x, y, w, h, files = None, sel = (), drop_row = None, focus = True, method = None):
	files = files or FILES
	c.rect (x, y, w, h, LIST, r = 4, outline = LINE)
	c.grad (x + 1, y + 1, w - 2, 26, lighten (HEAD, 0.4), HEAD, r = 4, corners = (True, True, False, False))
	c.hline (x + 1, x + w - 1, y + 27, LINE)
	cx = x + 10
	for i, (name, cw) in enumerate (COLS):
		right = name in ("Size", "Packed")
		if right: c.text_r (cx + cw - 14, y, 28, name, "smallb", DIM)
		else: c.text_l (cx, y, 28, name, "smallb", DIM)
		if name == "Name": c.poly ([(cx + 44, y + 11), (cx + 52, y + 11), (cx + 48, y + 17)], DIM)
		if i: c.vline (cx - 8, y + 6, y + 22, LINE2)
		cx += cw
	ry = y + 30; rh = 26
	for i, (name, kind, size, packed, ratio, mod, meth) in enumerate (files):
		if ry + rh > y + h - 4: break
		s = name in sel
		if s: c.rect (x + 3, ry, w - 18, rh - 2, SEL if focus else SEL_SOFT, r = 4)
		elif i % 2 == 0: c.rect (x + 3, ry, w - 18, rh - 2, A ((0, 0, 0), 7), r = 4)
		if name == drop_row: c.dashed (x + 3, ry, w - 18, rh - 2, SEL)
		fg = (255, 255, 255) if s and focus else TEXT
		dm = (226, 240, 244) if s and focus else DIM
		cx = x + 10
		if kind == "up":
			c.line ([(cx + 3, ry + 13), (cx + 9, ry + 7), (cx + 15, ry + 13)], DIM, 2); c.vline (cx + 8, ry + 8, ry + 19, DIM, 2)
			c.text_l (cx + 26, ry, rh - 2, "..  (parent folder)", "ui", DIM)
		else:
			if kind == "dir": ic_folder (c, cx, ry + 3, 18)
			else: ic_file (c, cx, ry + 3, 18, kind)
			c.text_l (cx + 26, ry, rh - 2, name + ("/" if kind == "dir" else ""), "uib" if kind == "dir" else "ui", fg)
			cx += COLS[0][1]
			c.text_r (cx + COLS[1][1] - 14, ry, rh - 2, size, "ui", fg if kind != "dir" else dm); cx += COLS[1][1]
			c.text_r (cx + COLS[2][1] - 14, ry, rh - 2, packed, "ui", dm); cx += COLS[2][1]
			if ratio is not None:
				c.rect (cx, ry + 9, 54, 7, A ((0, 0, 0), 22), r = 3)
				if ratio > 0: c.rect (cx, ry + 9, 54 * ratio, 7, (255, 255, 255) if s and focus else mix (GREEN, SEL, 0.4), r = 3)
				c.text_l (cx + 60, ry, rh - 2, "%d %%" % round (ratio * 100), "small", dm)
			cx += COLS[3][1]
			c.text_l (cx, ry, rh - 2, mod, "small", dm); cx += COLS[4][1]
			c.text_l (cx, ry, rh - 2, method if method and meth else meth, "small", dm)
		ry += rh
	scrollbar (c, x + w - 13, y + 31, h - 36, 0.0, 0.82)

def statusbar (c, x, y, w, left, right):
	c.rect (x, y, w, 24, FACE); c.hline (x, x + w, y, shade (FACE, 0.85))
	c.ellipse (x + 12, y + 12, 4, GREEN)
	c.text_l (x + 22, y, 24, left, "ui")
	c.text_r (x + w - 10, y, 24, right, "ui", DIM)

def info_card (c, x, y, w, rows, title = "Archive"):
	h = 34 + len (rows) * 20
	c.rect (x, y, w, h, A ((255, 255, 255), 80), r = 6, outline = A (shade (FACE, 0.75), 255))
	c.text (x + 12, y + 10, title, "smallb", DIM)
	ry = y + 30
	for k, v in rows:
		c.text_l (x + 12, ry, 18, k, "small", DIM); c.text_r (x + w - 12, ry, 18, v, "smallb", TEXT); ry += 20
	return h

def menu_popup (c, x, y, w, items, hot = None, title = None):
	h = 10 + sum (9 if it is None else 26 for it in items) + (26 if title else 0)
	c.rect (x, y, w, h, MENU, r = 8, outline = shade (FACE, 0.62))
	ry = y + 5
	if title: c.text_l (x + 14, ry, 24, title, "uib"); ry += 26; c.hline (x + 10, x + w - 10, ry - 3, LINE2)
	for it in items:
		if it is None: c.hline (x + 10, x + w - 10, ry + 4, LINE2); ry += 9; continue
		label, key, dis = (it + (None, False))[:3] if len (it) < 3 else it
		if label == hot: c.rect (x + 5, ry, w - 10, 26, SEL, r = 5)
		fg = (255, 255, 255) if label == hot else (FAINT if dis else TEXT)
		c.text_l (x + 16, ry, 26, label, "ui", fg)
		if key: c.text_r (x + w - 14, ry, 26, key, "small", (226, 240, 244) if label == hot else DIM)
		ry += 26
	return h

def cursor (c, x, y):
	pts = [(x, y), (x, y + 18), (x + 4.5, y + 14), (x + 8, y + 21), (x + 11, y + 20), (x + 7.5, y + 13), (x + 13, y + 13)]
	c.poly (pts, (0, 0, 0)); c.line (pts + [pts[0]], (255, 255, 255), 1.2)

# ---- the main window ------------------------------------------------------------------------------------
def archiver_window (c, x, y, w, h, title = "Archiver - Projet-Onyx.zip", active = True, sel = (), fmt = "ZIP",
		     read_only = False, drop_row = None, drop_tree = None, focus = True, hot = None):
	cx, cy, cw, ch = window (c, x, y, w, h, title, active)
	toolbar (c, cx, cy, cw, read_only, hot)
	breadcrumb (c, cx + 8, cy + 70, cw - 16, ["kernel", "sys"], title.split (" - ")[-1], fmt)
	ly = cy + 108; lh = ch - 108 - 30; tw_ = 236
	tree (c, cx + 8, ly, tw_, lh, drop = drop_tree, root = title.split (" - ")[-1])
	file_list (c, cx + 8 + tw_ + 8, ly, cw - tw_ - 24, lh, sel = sel, drop_row = drop_row, focus = focus,
		   method = "RAR 5" if "RAR" in fmt else None)
	return cx, cy, cw, ch, ly, lh, tw_

def shot_main ():
	c = Canvas ()
	desktop (c, "Archiver", ["File", "Edit", "View", "Actions", "Help"])
	x, y, w, h = 70, 50, 1140, 720
	sel = ("kapi.cpp", "kapi_abi.h", "kapitable.cpp")
	cx, cy, cw, ch, ly, lh, tw_ = archiver_window (c, x, y, w, h, sel = sel)
	info_card (c, cx + 16, ly + lh - 236, tw_ - 16, [("Format", "ZIP (Deflate)"), ("Files", "248"), ("Folders", "31"),
		("Original size", "42.7 MB"), ("Packed size", "13.9 MB"), ("Saved", "67 %"), ("Encrypted", "no"), ("Comment", "-")])
	statusbar (c, cx, cy + ch - 24, cw, "3 of 13 selected  (238.2 KB)", "Projet-Onyx.zip  -  248 files  -  42.7 MB -> 13.9 MB")
	# the right-click menu on the selection
	menu_popup (c, cx + 470, ly + 132, 236, [("Open", "Enter"), ("Open With...", ""), None, ("Extract...", "Ctrl+E"),
		("Extract Here", ""), ("Extract to Desktop", ""), None, ("Rename", "F2"), ("Delete from Archive", "Del"), None,
		("Copy Path", ""), ("Properties", "Alt+Enter")], hot = "Extract...")
	cursor (c, cx + 560, ly + 213)
	c.save ("archiver-main.png")

# ---- the extract dialog -----------------------------------------------------------------------------------
def shot_extract ():
	c = Canvas ()
	desktop (c, "Archiver", ["File", "Edit", "View", "Actions", "Help"])
	sel = ("kapi.cpp", "kapi_abi.h", "kapitable.cpp")
	archiver_window (c, 70, 50, 1140, 720, "Archiver - Onyx-backup.rar", active = False, sel = sel, fmt = "RAR 5  -  read only",
			 read_only = True, focus = False)
	x, y, w, h = 330, 84, 620, 660
	cx, cy, cw, ch = window (c, x, y, w, h, "Extract")
	px = cx + 20; py = cy + 16
	ic_archive (c, px, py, 40)
	c.text (px + 54, py + 2, "Extract from Onyx-backup.rar", "big")
	c.text (px + 54, py + 24, "kernel/sys/  -  RAR 5, solid: the files before them are decoded too", "small", DIM)
	py += 60
	group (c, px, py, cw - 40, 90, "WHAT")
	radio (c, px + 16, py + 30, "The selection", True, "3 files, 238.2 KB")
	radio (c, px + 16, py + 56, "Everything", False, "248 files in 31 folders, 42.7 MB")
	py += 104
	group (c, px, py, cw - 40, 102, "WHERE")
	field (c, px + 16, py + 30, cw - 40 - 32 - 96, 28, "SD:/home/stephan/Documents")
	button (c, cx + cw - 20 - 16 - 88, py + 30, 88, 28, "Browse...")
	checkbox (c, px + 16, py + 70, "Into a new folder named", True)
	field (c, px + 216, py + 64, 200, 28, "Onyx-backup")
	py += 116
	group (c, px, py, cw - 40, 118, "FOLDERS")
	radio (c, px + 16, py + 30, "Keep the archive's folders", True, "kernel/sys/kapi.cpp")
	radio (c, px + 16, py + 56, "From the current folder down", False, "kapi.cpp")
	radio (c, px + 16, py + 82, "All in one folder (flat)", False, "kapi.cpp")
	py += 132
	group (c, px, py, cw - 40, 92, "IF A FILE EXISTS")
	c.text_l (px + 16, py + 30, 28, "Existing files:")
	dropdown (c, px + 130, py + 30, 220, 28, "Ask for each one")
	checkbox (c, px + 16, py + 66, "Open the folder when done", True)
	checkbox (c, px + 280, py + 66, "Keep the dates", True)
	py += 104
	field (c, px, py, 260, 28, "", "Password (if encrypted)", disabled = True)
	button (c, cx + cw - 20 - 110, cy + ch - 46, 110, 32, "Extract", accent = True)
	button (c, cx + cw - 20 - 110 - 12 - 96, cy + ch - 46, 96, 32, "Cancel")
	c.save ("archiver-extract.png")

# ---- drag & drop from the File Viewer ----------------------------------------------------------------------
def fileviewer (c, x, y, w, h, active):
	cx, cy, cw, ch = window (c, x, y, w, h, "File Viewer", active)
	c.rect (cx + 8, cy + 8, cw - 16, 30, (255, 255, 255), r = 6, outline = LINE)
	bx = cx + 20
	for i, p in enumerate (["SD Card", "home", "stephan", "Pictures"]):
		last = i == 3
		c.text_l (bx, cy + 8, 30, p, "uib" if last else "ui", TEXT if last else LINK); bx += c.tw (p, "uib" if last else "ui") + 8
		if not last: c.line ([(bx + 2, cy + 18), (bx + 7, cy + 23), (bx + 2, cy + 28)], DIM, 1.5); bx += 16
	# the sidebar
	sx, sy = cx + 8, cy + 46
	c.rect (sx, sy, 140, ch - 46 - 30, FACE)
	for i, (t, k) in enumerate ([("Personal", "h"), ("Trash", "i"), ("Computer", "h"), ("SD Card", "s"), ("RAM", "i"), ("Network", "h")]):
		ry = sy + 6 + i * 26
		if k == "s": c.rect (sx + 2, ry, 136, 24, SEL, r = 5)
		c.text_l (sx + (12 if k == "h" else 30), ry, 24, t, "uib" if k == "h" else "ui", (255, 255, 255) if k == "s" else (DIM if k == "h" else TEXT))
	lx, ly, lw, lh = cx + 156, cy + 46, cw - 164, ch - 46 - 30
	c.rect (lx, ly, lw, lh, LIST, r = 4, outline = LINE)
	files = [("Holidays 2026", "dir"), ("Wallpapers", "dir"), ("pi-desk.png", "png"), ("onyx-boot.png", "png"), ("schema-gpu.png", "png"),
		 ("cat.jpg", "png"), ("receipt.pdf", "pdf"), ("notes.txt", "txt"), ("screenshot-12-34.png", "png"), ("sunset.jpg", "png")]
	sel = ("pi-desk.png", "onyx-boot.png")
	for i, (n, k) in enumerate (files):
		ry = ly + 6 + i * 26
		if n in sel: c.rect (lx + 4, ry, lw - 8, 24, SEL if active else SEL_SOFT, r = 5)
		if k == "dir": ic_folder (c, lx + 12, ry + 3, 18)
		else: ic_file (c, lx + 12, ry + 3, 18, k)
		c.text_l (lx + 38, ry, 24, n, "ui", (255, 255, 255) if n in sel and active else TEXT)
	c.rect (cx, cy + ch - 24, cw, 24, FACE); c.hline (cx, cx + cw, cy + ch - 24, shade (FACE, 0.85))
	c.text_l (cx + 10, cy + ch - 24, 24, "10 items  -  2 selected (1.9 MB)")

def drag_ghost (c, x, y, names, n_badge, label):
	w = 210
	c.rect (x, y, w, 30 + 22 * len (names), A (MENU, 228), r = 8, outline = A (SEL, 255))
	for i, n in enumerate (names):
		ic_file (c, x + 10, y + 8 + i * 22, 16, "png"); c.text_l (x + 34, y + 6 + i * 22, 20, n, "small")
	c.text_l (x + 10, y + 6 + len (names) * 22, 20, label, "smallb", LINK)
	c.ellipse (x + w - 2, y + 2, 11, RED); c.text_c (x + w - 13, y - 9, 22, 22, str (n_badge), "smallb", (255, 255, 255))

def shot_dragdrop ():
	c = Canvas ()
	desktop (c, "Archiver", ["File", "Edit", "View", "Actions", "Help"])
	ax, ay, aw, ah = 440, 44, 820, 740
	# the archive is open in docs/images/
	files = [("..", "up", "", "", None, "", ""),
		 ("screens", "dir", "12 items", "", None, "29/09/2026 19:20", ""),
		 ("logo.svg", "txt", "14.2 KB", "4.1 KB", 0.71, "18/09/2026 08:44", "Deflate"),
		 ("architecture.png", "png", "212.0 KB", "209.8 KB", 0.01, "18/09/2026 08:44", "Deflate"),
		 ("dock-v2.png", "png", "88.6 KB", "88.6 KB", 0.0, "26/09/2026 21:05", "Store"),
		 ("kapi-table.pdf", "pdf", "301.5 KB", "262.0 KB", 0.13, "27/09/2026 10:30", "Deflate")]
	c_cols = COLS[:]
	COLS[4] = ("Modified", 132)
	cx, cy, cw, ch = window (c, ax, ay, aw, ah, "Archiver - Projet-Onyx.zip", True)
	toolbar (c, cx, cy, cw, hot = None)
	breadcrumb (c, cx + 8, cy + 70, cw - 16, ["docs", "images"], "Projet-Onyx.zip", "ZIP")
	ly = cy + 108; lh = ch - 108 - 30
	tree (c, cx + 8, ly, 170, lh, rows = [(0, "Projet-Onyx.zip", "arch", True), (1, "docs", "dir", True), (2, "images", "dir", "sel"),
		(3, "screens", "dir", False), (1, "kernel", "dir", False), (1, "user", "dir", False), (1, "tools", "dir", False)], drop = None)
	COLS[0] = ("Name", 170); COLS[4] = ("Modified", 128)
	fx, fw = cx + 186, cw - 194
	file_list (c, fx, ly, fw, lh, files = files)
	# the drop zone: the whole list glows, the target written at the bottom
	c.dashed (fx + 4, ly + 30, fw - 22, lh - 36, SEL, r = 8)
	by = ly + lh - 92
	c.rect (fx + 40, by, fw - 80, 66, A (MENU, 245), r = 10, outline = SEL, width = 2)
	ic_archive (c, fx + 58, by + 15, 36)
	c.text (fx + 108, by + 12, "Drop to add 2 files to", "ui", DIM)
	c.text (fx + 108, by + 32, "Projet-Onyx.zip  >  docs / images /", "uib", LINK)
	c.text_r (fx + fw - 56, by + 2, 66, "on a folder: into it", "small", DIM)
	COLS[:] = c_cols
	statusbar (c, cx, cy + ch - 24, cw, "6 items in docs/images/", "Ctrl while dropping: keep the files' folders")
	fileviewer (c, 24, 120, 520, 470, False)
	drag_ghost (c, 742, 368, ["pi-desk.png", "onyx-boot.png"], 2, "+ Add to the archive")
	cursor (c, 736, 360)
	# the Add menu (the other way in)
	c.text (24, 620, "or:  File  >  Add Files...  (Ctrl+Shift+A)  /  Add Folder...", "uib", (255, 255, 255))
	c.save ("archiver-dragdrop.png")

# ---- the Add dialog and the progress --------------------------------------------------------------------------
def shot_add ():
	c = Canvas ()
	desktop (c, "Archiver", ["File", "Edit", "View", "Actions", "Help"])
	archiver_window (c, 70, 50, 1140, 720, active = False, focus = False)
	x, y, w, h = 214, 72, 660, 604
	cx, cy, cw, ch = window (c, x, y, w, h, "Add to the Archive")
	px = cx + 20; py = cy + 16
	ic_archive (c, px, py, 40)
	c.text (px + 54, py + 2, "Add to Projet-Onyx.zip", "big")
	c.text (px + 54, py + 24, "2 files and 1 folder, 4.6 MB", "small", DIM)
	py += 60
	lw = cw - 40 - 120
	c.rect (px, py, lw, 150, LIST, r = 4, outline = LINE)
	rows = [("pi-desk.png", "png", "1.2 MB", "SD:/home/stephan/Pictures"), ("onyx-boot.png", "png", "0.7 MB", "SD:/home/stephan/Pictures"),
		("Holidays 2026/", "dir", "2.7 MB", "14 files")]
	for i, (n, k, s, where) in enumerate (rows):
		ry = py + 6 + i * 28
		if i == 2: c.rect (px + 3, ry, lw - 6, 26, SEL_SOFT, r = 4)
		if k == "dir": ic_folder (c, px + 10, ry + 4, 18)
		else: ic_file (c, px + 10, ry + 4, 18, k)
		c.text_l (px + 36, ry, 26, n, "uib" if k == "dir" else "ui")
		c.text_l (px + 200, ry, 26, where, "small", DIM)
		c.text_r (px + lw - 12, ry, 26, s, "small", DIM)
	bx = px + lw + 12
	button (c, bx, py, 108, 28, "Add Files...")
	button (c, bx, py + 36, 108, 28, "Add Folder...")
	button (c, bx, py + 72, 108, 28, "Remove")
	py += 166
	group (c, px, py, cw - 40, 140, "WHERE IN THE ARCHIVE")
	c.text_l (px + 16, py + 30, 28, "Into the folder:")
	dropdown (c, px + 140, py + 30, 260, 28, "docs / images /")
	button (c, px + 410, py + 30, 110, 28, "New Folder...")
	radio (c, px + 16, py + 70, "Keep the folders I add", True, "Holidays 2026/beach/01.jpg")
	radio (c, px + 16, py + 96, "Only the files", False, "01.jpg")
	py += 154
	group (c, px, py, cw - 40, 108, "COMPRESSION")
	segmented (c, px + 16, py + 30, 28, ["Store", "Fast", "Normal", "Best"], 2)
	c.text_l (px + 330, py + 30, 28, "Deflate - already packed files (png, jpg, zip) stored", "small", DIM)
	c.text_l (px + 16, py + 70, 28, "If a file exists:")
	dropdown (c, px + 140, py + 70, 200, 28, "Ask for each one")
	checkbox (c, px + 360, py + 76, "Encrypt (AES-256)", False)
	button (c, cx + cw - 20 - 110, cy + ch - 46, 110, 32, "Add", accent = True)
	button (c, cx + cw - 20 - 110 - 12 - 96, cy + ch - 46, 96, 32, "Cancel")
	# the progress window (an earlier job: the archive is rewritten into a new copy, then swapped)
	x, y, w, h = 884, 584, 380, 176
	cx, cy, cw, ch = window (c, x, y, w, h, "Adding - Projet-Onyx.zip", active = False)
	c.text (cx + 16, cy + 14, "Compressing  screens/desk-03.png", "ui")
	progress (c, cx + 16, cy + 40, cw - 32, 14, 0.64)
	c.text (cx + 16, cy + 62, "11 of 17 files  -  2.9 of 4.6 MB  -  1.4 MB/s", "small", DIM)
	c.text (cx + 16, cy + 80, "Writing a new copy; the old archive is replaced at the end", "small", DIM)
	button (c, cx + cw - 16 - 90, cy + ch - 40, 90, 28, "Cancel")
	button (c, cx + cw - 16 - 90 - 10 - 110, cy + ch - 40, 110, 28, "Background")
	c.save ("archiver-add.png")

# ---- the welcome screen (nothing open) -------------------------------------------------------------------------
def shot_welcome ():
	c = Canvas ()
	desktop (c, "Archiver", ["File", "Edit", "View", "Actions", "Help"], open_menu = "File")
	x, y, w, h = 70, 50, 1140, 720
	cx, cy, cw, ch = window (c, x, y, w, h, "Archiver")
	toolbar (c, cx, cy, cw)
	ax, ay, aw, ah = cx + 8, cy + 70, cw - 16, ch - 70 - 30
	c.rect (ax, ay, aw, ah, LIST, r = 6, outline = LINE)
	mx = ax + aw / 2
	c.dashed (mx - 260, ay + 40, 520, 200, SEL, r = 14)
	ic_archive (c, mx - 32, ay + 64, 64)
	c.text_c (mx - 260, ay + 140, 520, 26, "Drop an archive here to open it", "big")
	c.text_c (mx - 260, ay + 168, 520, 20, "or files and folders to make a new one", "ui", DIM)
	button (c, mx - 200, ay + 196, 190, 30, "Open an Archive...", accent = True)
	button (c, mx + 10, ay + 196, 190, 30, "New Archive...")
	# the formats
	fy = ay + 270
	c.text_c (ax, fy, aw, 20, "FORMATS", "smallb", DIM)
	fmts = [("ZIP", "open, extract, add, delete", GREEN), ("7z", "open, extract, add, delete", GREEN),
		("TAR / .tar.gz / .tgz", "open, extract, add", GREEN), ("RAR (4, 5)", "open, extract  -  read only", AMBER),
		("GZ / BZ2 / XZ / ZST", "one file: extract", SEL)]
	tot = 0
	cards = []
	for n, d, col in fmts: cards.append ((n, d, col, max (c.tw (n, "uib"), c.tw (d, "small")) + 34)); tot += cards[-1][3] + 10
	fx = mx - tot / 2
	for n, d, col, cw_ in cards:
		c.rect (fx, fy + 28, cw_, 54, A ((255, 255, 255), 255), r = 8, outline = LINE2)
		c.ellipse (fx + 14, fy + 44, 4, col)
		c.text (fx + 24, fy + 36, n, "uib"); c.text (fx + 14, fy + 58, d, "small", DIM); fx += cw_ + 10
	# the recent archives
	ry = fy + 110
	c.text (ax + 160, ry, "RECENT", "smallb", DIM); ry += 22
	for n, where, size, when in [("Projet-Onyx.zip", "SD:/home/stephan/Documents", "13.9 MB", "today 12:20"),
				     ("Onyx-backup.rar", "SD:/backup", "211.4 MB", "yesterday"),
				     ("roms-snes.7z", "SD:/roms", "48.0 MB", "28/09/2026"),
				     ("netsurf-src.tar.gz", "SD:/src", "9.6 MB", "21/09/2026")]:
		c.rect (ax + 156, ry, aw - 312, 34, A ((0, 0, 0), 7), r = 6)
		ic_archive (c, ax + 168, ry + 7, 20)
		c.text_l (ax + 198, ry, 34, n, "uib"); c.text_l (ax + 380, ry, 34, where, "small", DIM)
		c.text_r (ax + aw - 300, ry, 34, size, "small", DIM); c.text_r (ax + aw - 172, ry, 34, when, "small", DIM)
		ry += 40
	statusbar (c, cx, cy + ch - 24, cw, "No archive open", "Archiver 1.0")
	menu_popup (c, 64, 26, 270, [("New Archive...", "Ctrl+N"), ("Open...", "Ctrl+O"), ("Open Recent", ">"), None,
		("Add Files...", "Ctrl+Shift+A", True), ("Add Folder...", "", True), None, ("Extract...", "Ctrl+E", True),
		("Extract All...", "Ctrl+Shift+E", True), None, ("Close", "Ctrl+W"), ("Quit", "Ctrl+Q")], hot = "Open...")
	c.save ("archiver-welcome.png")

if __name__ == "__main__":
	shot_welcome (); shot_main (); shot_extract (); shot_dragdrop (); shot_add ()
