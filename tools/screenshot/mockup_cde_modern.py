#!/usr/bin/env python3
"""mockup_cde_modern.py -- the CDE skin of mockup_retro.py, modernised: the same desktop (the menu
bar, the fractal, the terminal in front, the calculator) and CDE's structure -- the Front Panel
at the bottom with its switcher (the Shelf's tabs, each its own colour) and its subpanels, the
window menu / minimise / maximise buttons -- drawn the way Onyx's elegant.h can (on the branch
archive/elegant-ui-2026-09-28): anti-aliased text (DejaVu Sans, the .aaf fonts' face), rounded corners, light gradients, the icons scaled
smoothly. No drop shadows (the user's remark: the compositor would blend their band again at
every change under it, and at every move): a crisp outline instead, as CDE had. The Front Panel's launchers are the apps'
categories (app.txt, as the elegant home screen's tabs), their drawers' tabs inside the dock at
its top edge (the user's choice); one subpanel is open (Games). No column of running apps at the
right any more (the user's remark: the dock has them): a dot under a launcher says one of its
apps runs. The agenda widget stays on the desktop (the next appointments).

    python3 tools/screenshot/mockup_cde_modern.py  -> docs/gui-redesign/mockups/cde-modern*.png

Two palettes: CDE's (Default.dp, softened) and one with a Windows touch (grey faces, navy-to-blue
title bars, a teal desktop), since the user liked the Windows 3.1 mock-up too. The calculator's
keys: the user's framed button (a raised frame, a sunken well, the button set in it), rounded.
"""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mockup_retro as MR				# TERM, KEYS, MANDEL, the palettes' sources

ROOT = MR.ROOT
OUT = MR.OUT
W, H = 1024, 768
DJ = "/usr/share/fonts/truetype/dejavu/"
F = {n: ImageFont.truetype (DJ + f, s) for n, (f, s) in {
	"ui": ("DejaVuSans.ttf", 13), "uib": ("DejaVuSans-Bold.ttf", 13), "title": ("DejaVuSans-Bold.ttf", 13),
	"small": ("DejaVuSans.ttf", 11), "mono": ("DejaVuSansMono.ttf", 13), "big": ("DejaVuSans-Bold.ttf", 17)}.items ()}

PAL_CDE = dict (
	name = "cde-modern", back = (146, 160, 178), back2 = (96, 110, 130),
	active = (240, 176, 122), inactive = (172, 172, 176), title = None,
	active_fg = (48, 30, 16), inactive_fg = (44, 44, 48),
	app = (208, 194, 186), app_fg = (32, 28, 26),
	field = (92, 100, 120), field_fg = (240, 244, 248),
	menu = (247, 243, 239), menu_fg = (24, 24, 24), hi = (73, 146, 167), hi_fg = (255, 255, 255),
	term = (26, 58, 70), term_fg = (212, 234, 240), cursor = (120, 214, 236),
	dock = (164, 186, 206), dock_fg = (24, 32, 42),
	switch = [(123, 140, 162), (147, 171, 191), (73, 146, 167), (183, 135, 141)])
PAL_WIN = dict (
	name = "cde-modern-win", back = (0, 128, 128), back2 = (0, 86, 98),
	active = (214, 210, 202), inactive = (214, 210, 202),
	title = ((0, 0, 128), (22, 136, 210)), title_in = ((120, 120, 124), (190, 190, 194)),
	active_fg = (255, 255, 255), inactive_fg = (236, 236, 236),
	app = (214, 210, 202), app_fg = (0, 0, 0),
	field = (255, 255, 255), field_fg = (0, 0, 0),
	menu = (255, 255, 255), menu_fg = (0, 0, 0), hi = (10, 36, 106), hi_fg = (255, 255, 255),
	term = (14, 14, 16), term_fg = (206, 206, 206), cursor = (206, 206, 206),
	dock = (226, 223, 216), dock_fg = (0, 0, 0),
	switch = [(0, 128, 128), (128, 128, 0), (128, 0, 128), (92, 92, 96)])

# ---- anti-aliased drawing ------------------------------------------------------------------------------
S = 4							# supersampling
def lighten (c, k): return tuple (int (v + (255 - v) * k) for v in c)
def shade (c, k): return tuple (int (v * k) for v in c)
def mix (a, b, t): return tuple (int (a[i] + (b[i] - a[i]) * t) for i in range (3))
def fg_on (c): return (0, 0, 0) if (0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]) > 140 else (255, 255, 255)

# A theme gives one colour per kind of frame (active, inactive); every shade of the frame comes
# from it through a grey profile -- level 128 = the colour itself, above it toward white, below
# toward black (made once per colour: a table of one colour per row, then only straight lines).
def tone (c, level): return lighten (c, (level - 128) / 127) if level >= 128 else shade (c, level / 128)
FRAME = dict (top = 164, bottom = 115, edge = 70, btop = 172, bbottom = 118, bedge = 64)
# The colour themes the user kept (the active frame's colour; the inactive frames: Grey).
THEMES = [("Peach", (240, 176, 122)), ("Steel", (122, 152, 192)), ("Sage", (128, 170, 118)),
	  ("Brick", (196, 84, 80)), ("Slate", (58, 68, 88)), ("Grey", (172, 172, 176))]

def mask (w, h, fn):
	"""An anti-aliased coverage mask w x h: fn (draw, s) draws in 255 at s times the size."""
	m = Image.new ("L", (w * S, h * S), 0); fn (ImageDraw.Draw (m), S)
	return m.resize ((w, h), Image.BOX)
def grad (w, h, top, bottom):
	t = np.linspace (0, 1, h)[:, None, None]
	a = np.array (top, float)[None, None, :] * (1 - t) + np.array (bottom, float)[None, None, :] * t
	return Image.fromarray (np.repeat (a, w, 1).astype ("uint8"), "RGB")
def put (img, x, y, m, colour, alpha = 255):
	"""colour (an RGB triple or an image of m's size) through the mask m at (x, y)."""
	if alpha < 255: m = m.point (lambda v: v * alpha // 255)
	src = colour if isinstance (colour, Image.Image) else Image.new ("RGB", m.size, colour)
	img.paste (src, (x, y), m)
def rrect (img, x, y, w, h, r, colour, alpha = 255, corners = None):
	m = mask (w, h, lambda d, s: d.rounded_rectangle ([0, 0, w * s - 1, h * s - 1], r * s, fill = 255, corners = corners))
	put (img, x, y, m, grad (w, h, *colour) if isinstance (colour[0], tuple) else colour, alpha)
def ring (img, x, y, w, h, r, colour, t = 1, alpha = 255, corners = None):
	m = mask (w, h, lambda d, s: d.rounded_rectangle ([0, 0, w * s - 1, h * s - 1], r * s, outline = 255, width = t * s, corners = corners))
	put (img, x, y, m, colour, alpha)
def text (img, x, y, s, font, colour):			# y: the line's top (the font's ascent box)
	ImageDraw.Draw (img).text ((x, y), s, font = F[font], fill = colour)
def tw (s, font): return int (round (F[font].getlength (s)))
def text_c (img, x, y, w, h, s, font, colour):
	b = F[font].getbbox ("Hg"); th = b[3] - b[1]
	text (img, x + (w - tw (s, font)) // 2, y + (h - th) // 2 - b[1], s, font, colour)
def text_l (img, x, y, h, s, font, colour):
	b = F[font].getbbox ("Hg"); th = b[3] - b[1]
	text (img, x, y + (h - th) // 2 - b[1], s, font, colour)
def glyph (img, x, y, w, h, fn, colour, alpha = 255): put (img, x, y, mask (w, h, fn), colour, alpha)

_icons = {}
def app_icon (name, size):
	"""apps/<name>.app/icon.bmp (40 x 40, magenta = see-through) scaled smoothly, premultiplied."""
	k = (name, size)
	if k not in _icons:
		p = os.path.join (MR.R.SD, "apps", name + ".app", "icon.bmp")
		im = Image.open (p).convert ("RGB"); a = np.array (im)
		alpha = np.where ((a[..., 0] == 255) & (a[..., 1] == 0) & (a[..., 2] == 255), 0, 255).astype ("uint8")
		rgba = Image.fromarray (np.dstack ([a, alpha]), "RGBA").convert ("RGBa").resize ((size, size), Image.LANCZOS).convert ("RGBA")
		_icons[k] = rgba
	return _icons[k]
def draw_icon (img, name, x, y, size): ic = app_icon (name, size); img.paste (ic, (x, y), ic)

# ---- the widgets ----------------------------------------------------------------------------------------
def framed (img, x, y, w, h, face, pressed = False, label = "", font = "ui", fg = (0, 0, 0)):
	"""The user's framed button, rounded: a raised frame, a sunken well, the button set in it
	(raised; pressed: flush, a shade darker)."""
	rrect (img, x, y, w, h, 7, (lighten (face, 0.45), shade (face, 0.86)))
	ring (img, x, y, w, h, 7, shade (face, 0.62), alpha = 200)
	rrect (img, x + 3, y + 3, w - 6, h - 6, 5, (shade (face, 0.70), lighten (face, 0.35)))	# the well: dark at the top
	bx, by, bw, bh = x + 5, y + 5, w - 10, h - 10
	if pressed: rrect (img, bx, by, bw, bh, 4, (shade (face, 0.84), shade (face, 0.92)))
	else:
		rrect (img, bx, by, bw, bh, 4, (lighten (face, 0.55), shade (face, 0.95)))
		ring (img, bx, by, bw, bh, 4, (255, 255, 255), alpha = 90, corners = None)
	if label: text_c (img, bx + (1 if pressed else 0), by + (1 if pressed else 0), bw, bh, label, font, fg)

def field (img, x, y, w, h, s, bg, fg):
	rrect (img, x, y, w, h, 5, bg); ring (img, x, y, w, h, 5, (0, 0, 0), alpha = 70)
	rrect (img, x + 1, y + 1, w - 2, 6, 4, (0, 0, 0), alpha = 28, corners = (True, True, False, False))	# the inner shadow
	text_l (img, x + w - 10 - tw (s, "mono"), y, h, s, "mono", fg)

# ---- the desktop ----------------------------------------------------------------------------------------
class Modern:
	TH, BW = 28, 4
	def __init__ (self, pal, outline = None): self.p = pal; self.outline = outline	# None, "dark", "black"

	def backdrop (self, img):
		p = self.p; img.paste (grad (W, H, lighten (p["back"], 0.12), p["back2"]), (0, 0))
		rng = np.random.default_rng (7)							# CDE's pebbles, faint
		n = rng.random ((H // 3 + 8, W // 3 + 8))
		from numpy.lib.stride_tricks import sliding_window_view as sw
		m = (sw (n, (5, 5))).mean (axis = (2, 3))[:H // 3, :W // 3]
		v = np.clip ((m - 0.5) * 900 + 128, 0, 255).astype ("uint8")
		tex = Image.fromarray (np.kron (v, np.ones ((3, 3), "uint8")), "L").resize ((W, H))
		light = Image.new ("RGB", (W, H), lighten (p["back"], 0.3)); dark = Image.new ("RGB", (W, H), shade (p["back2"], 0.8))
		img.paste (light, (0, 0), tex.point (lambda q: max (0, q - 128) // 7))
		img.paste (dark, (0, 0), tex.point (lambda q: max (0, 128 - q) // 7))

	def menubar (self, img):
		p = self.p; bar = p["app"]
		img.paste (grad (W, 30, lighten (bar, 0.45), lighten (bar, 0.1)), (0, 0))
		put (img, 0, 29, Image.new ("L", (W, 1), 255), shade (bar, 0.6))
		x = 14
		for i, t in enumerate (["Terminal", "File", "Edit", "View"]):
			f = "uib" if i == 0 else "ui"; text_l (img, x, 0, 30, t, f, p["app_fg"]); x += tw (t, f) + 22
		text_l (img, W - 54, 0, 30, "12:34", "uib", p["app_fg"])
		c = p["app_fg"]
		glyph (img, W - 84, 8, 18, 14, lambda d, s: [d.arc ([(9 - r) * s, (13 - r) * s, (9 + r) * s, (13 + r) * s], 225, 315, fill = 255, width = int (1.6 * s)) for r in (4, 8, 12)] + [d.ellipse ([7.6 * s, 11.6 * s, 10.4 * s, 14 * s], fill = 255)], c)
		glyph (img, W - 112, 8, 20, 14, lambda d, s: [d.polygon ([(1 * s, 5 * s), (4 * s, 5 * s), (9 * s, 1 * s), (9 * s, 13 * s), (4 * s, 9 * s), (1 * s, 9 * s)], fill = 255)] + [d.arc ([(12 - r) * s, (7 - r) * s, (12 + r) * s, (7 + r) * s], 300, 60, fill = 255, width = int (1.5 * s)) for r in (3, 6)], c)

	def window (self, img, x, y, cw, ch, title, active):
		p = self.p; TH, BW = self.TH, self.BW; w, h = cw + 2 * BW, ch + TH + BW
		fc = p["active"] if active else p["inactive"]
		rrect (img, x, y, w, h, 8, (tone (fc, FRAME["top"]), tone (fc, FRAME["bottom"])))
		tcol = p["title"] if active else p.get ("title_in")
		if tcol: rrect (img, x + 1, y + 1, w - 2, TH - 1, 7, (tcol[0], tcol[1]), corners = (True, True, False, False))
		ring (img, x, y, w, h, 8, tone (fc, FRAME["edge"]), alpha = 170)
		if self.outline: ring (img, x, y, w, h, 8, (0, 0, 0) if self.outline == "black" else tone (fc, 28))	# a 1-px outline
		put (img, x + 8, y + 1, Image.new ("L", (w - 16, 1), 255), (255, 255, 255), 110)			# the top light
		tbg = mix (tcol[0], tcol[1], 0.5) if tcol else fc
		fg = p["active_fg"] if active else p["inactive_fg"]
		def tbutton (bx, kind):
			rrect (img, bx, y + 5, 22, 19, 5, (tone (tbg, FRAME["btop"]), tone (tbg, FRAME["bbottom"])), alpha = 235)
			ring (img, bx, y + 5, 22, 19, 5, tone (tbg, FRAME["bedge"]), alpha = 150)
			gc = fg
			if kind == "menu": glyph (img, bx + 5, y + 12, 12, 5, lambda d, s: d.rounded_rectangle ([0, 0, 12 * s - 1, 5 * s - 1], 2 * s, fill = 255), gc)
			elif kind == "min": glyph (img, bx + 8, y + 15, 6, 4, lambda d, s: d.rounded_rectangle ([0, 0, 6 * s - 1, 4 * s - 1], 1 * s, fill = 255), gc)
			elif kind == "max": glyph (img, bx + 6, y + 9, 10, 11, lambda d, s: d.rounded_rectangle ([0, 0, 10 * s - 1, 11 * s - 1], 2 * s, outline = 255, width = int (1.7 * s)), gc)
			elif kind == "close": glyph (img, bx + 6, y + 9, 10, 11, lambda d, s: (d.line ([1 * s, 1.5 * s, 9 * s, 9.5 * s], fill = 255, width = 2 * s), d.line ([9 * s, 1.5 * s, 1 * s, 9.5 * s], fill = 255, width = 2 * s)), gc)
		tbutton (x + 6, "menu")
		for i, k in enumerate (["close", "max", "min"]): tbutton (x + w - 28 - i * 25, k)
		text_c (img, x + 32, y, w - 64 - 3 * 25, TH, title, "title", fg)
		cx, cy = x + BW, y + TH
		return cx, cy

	def fractal (self, img, cx, cy):
		p = self.p; img.paste (MR.MANDEL, (cx, cy))
		img.paste (grad (340, 16, lighten (p["app"], 0.2), p["app"]), (cx, cy + 240))
		text_l (img, cx + 6, cy + 240, 16, "click: in    o: out    r: reset", "small", p["app_fg"])
		rrect (img, cx + 6, cy + 6, 132, 24, 6, (lighten (p["app"], 0.5), p["app"]), alpha = 240)
		ring (img, cx + 6, cy + 6, 132, 24, 6, shade (p["app"], 0.5), alpha = 160)
		text_l (img, cx + 14, cy + 6, 24, "Mandelbrot", "ui", p["app_fg"])
		glyph (img, cx + 118, cy + 15, 10, 6, lambda d, s: d.polygon ([(0, 0), (10 * s, 0), (5 * s, 6 * s)], fill = 255), p["app_fg"])

	def calc (self, img, cx, cy):
		p = self.p; img.paste (grad (280, 296, lighten (p["app"], 0.15), shade (p["app"], 0.96)), (cx, cy))
		field (img, cx + 8, cy + 8, 208, 36, "3.14159265", p["field"], p["field_fg"])
		framed (img, cx + 222, cy + 8, 52, 36, p["app"], False, "RAD", "small", p["app_fg"])
		for r in range (6):
			for c in range (5):
				k = MR.KEYS[r * 5 + c]
				framed (img, cx + 8 + c * 54, cy + 52 + r * 40, 51, 38, p["app"], k == "=", k, "ui", p["app_fg"])

	def terminal (self, img, cx, cy, w, h):
		p = self.p; img.paste (p["term"], (cx, cy, cx + w, cy + h))
		for i, s in enumerate (MR.TERM): text (img, cx + 10, cy + 8 + i * 20, s, "mono", p["term_fg"])
		iy = cy + 8 + len (MR.TERM) * 20; s = "$ cat readme.txt | grep -i pi"; text (img, cx + 10, iy, s, "mono", p["term_fg"])
		rrect (img, cx + 11 + tw (s, "mono"), iy + 1, 8, 16, 2, p["cursor"])
		rrect (img, cx + w - 10, cy + 6, 5, h - 12, 3, p["term_fg"], alpha = 30)			# a slim scroll bar
		rrect (img, cx + w - 10, cy + 150, 5, 150, 3, p["term_fg"], alpha = 120)

	def dock (self, img, open_cat = "Games", hover = 4):
		"""The Front Panel, modernised: the categories' launchers (a subpanel each), the switcher
		(the Shelf's tabs), the system, the terminal, the files, the Trash. No clock nor date: the
		menu bar has the time, a click on it shows a calendar (or starts the Calendar app)."""
		p = self.p; dw, dh = 754, 80; x0, y0 = (W - dw) // 2, H - dh - 12
		rrect (img, x0, y0, dw, dh, 14, (lighten (p["dock"], 0.35), shade (p["dock"], 0.94)))
		ring (img, x0, y0, dw, dh, 14, shade (p["dock"], 0.55), alpha = 170)
		put (img, x0 + 14, y0 + 1, Image.new ("L", (dw - 28, 1), 255), (255, 255, 255), 140)
		fgc = p["dock_fg"]; CW = 60; x = x0 + 12; opened = None
		def sep (sx): put (img, sx, y0 + 12, Image.new ("L", (1, dh - 24), 255), shade (p["dock"], 0.6), 150); put (img, sx + 1, y0 + 12, Image.new ("L", (1, dh - 24), 255), (255, 255, 255), 110)
		def launcher (kind, cat = None, last = False, running = False):
			nonlocal x, opened
			cx = x + CW // 2
			if kind == "clock":
				glyph (img, cx - 22, y0 + 22, 44, 44, lambda d, s: d.ellipse ([0, 0, 44 * s - 1, 44 * s - 1], fill = 255), (250, 250, 250))
				glyph (img, cx - 22, y0 + 22, 44, 44, lambda d, s: d.ellipse ([0, 0, 44 * s - 1, 44 * s - 1], outline = 255, width = 2 * s), shade (p["dock"], 0.45))
				def hands (d, s):
					for a in range (12):
						t = a * math.pi / 6; d.ellipse ([(22 + 17 * math.sin (t)) * s - s, (22 - 17 * math.cos (t)) * s - s, (22 + 17 * math.sin (t)) * s + s, (22 - 17 * math.cos (t)) * s + s], fill = 255)
					d.line ([22 * s, 22 * s, (22 + 10 * math.sin (1.1)) * s, (22 - 10 * math.cos (1.1)) * s], fill = 255, width = 3 * s)
					d.line ([22 * s, 22 * s, (22 + 15 * math.sin (-0.2)) * s, (22 - 15 * math.cos (-0.2)) * s], fill = 255, width = 2 * s)
				glyph (img, cx - 22, y0 + 22, 44, 44, hands, (30, 30, 34))
			elif kind == "date":
				rrect (img, cx - 20, y0 + 21, 40, 46, 6, (255, 255, 255)); ring (img, cx - 20, y0 + 21, 40, 46, 6, (0, 0, 0), alpha = 90)
				rrect (img, cx - 20, y0 + 21, 40, 14, 6, (210, 50, 44), corners = (True, True, False, False))
				text_c (img, cx - 20, y0 + 21, 40, 14, "SEP", "small", (255, 255, 255)); text_c (img, cx - 20, y0 + 36, 40, 30, "28", "big", (30, 30, 30))
			elif kind == "trash":
				def can (d, s):
					d.rounded_rectangle ([6 * s, 8 * s, 34 * s, 12 * s], 1 * s, fill = 255); d.rounded_rectangle ([15 * s, 4 * s, 25 * s, 8 * s], 1 * s, fill = 255)
					d.polygon ([(9 * s, 14 * s), (31 * s, 14 * s), (29 * s, 42 * s), (11 * s, 42 * s)], fill = 255)
				glyph (img, cx - 20, y0 + 22, 40, 44, can, shade (p["dock"], 0.5))
				def ribs (d, s):
					for k in range (3): d.line ([(15 + k * 5) * s, 18 * s, (15 + k * 5) * s, 38 * s], fill = 255, width = 2 * s)
				glyph (img, cx - 20, y0 + 22, 40, 44, ribs, lighten (p["dock"], 0.5))
			else: draw_icon (img, kind, cx - 22, y0 + 22, 44)
			if cat:								# the subpanel's tab
				o = cat == open_cat
				bottom = (False, False, True, True)					# (inside the dock, at its edge)
				rrect (img, cx - 16, y0 + 1, 32, 13, 6, (shade (p["dock"], 0.93), shade (p["dock"], 0.84)) if not o else (lighten (p["hi"], 0.2), p["hi"]), corners = bottom)
				ring (img, cx - 16, y0 + 1, 32, 13, 6, shade (p["dock"], 0.55), alpha = 140, corners = bottom)
				c = p["hi_fg"] if o else fgc
				glyph (img, cx - 5, y0 + 5, 10, 6, (lambda d, s: d.polygon ([(0, 0), (10 * s, 0), (5 * s, 6 * s)], fill = 255)) if o else (lambda d, s: d.polygon ([(0, 6 * s), (10 * s, 6 * s), (5 * s, 0)], fill = 255)), c)
				if o: opened = (cx, cat)
			if running: rrect (img, cx - 3, y0 + dh - 9, 6, 5, 2, p["hi"])		# an app of it is running
			x += CW
			if not last: sep (x - 1)
		for k, c, r in (("tinypad", "Productivity", True), ("netsurf", "Internet", False), ("paint", "Graphics", True), ("tetris", "Games", False)):
			launcher (k, c, running = r)						# (the calculator, the fractal)
		sx, sw_ = x + 6, 236; x = sx + sw_ + 6						# the switcher: the Shelf's tabs
		rrect (img, sx, y0 + 8, sw_, dh - 16, 10, shade (p["dock"], 0.9), alpha = 200); ring (img, sx, y0 + 8, sw_, dh - 16, 10, shade (p["dock"], 0.6), alpha = 120)
		for i, (lab, col) in enumerate (zip (["Shelf", "Documents", "Apps", "+"], p["switch"])):
			bx, by = sx + 30 + (i % 2) * 90, y0 + 16 + (i // 2) * 26
			cur = i == 0
			rrect (img, bx, by, 86, 23, 7, (shade (col, 0.8), col) if cur else (lighten (col, 0.3), shade (col, 0.9)))
			ring (img, bx, by, 86, 23, 7, shade (col, 0.5), alpha = 200)
			if cur: rrect (img, bx + 1, by + 1, 84, 5, 6, (0, 0, 0), alpha = 50, corners = (True, True, False, False))
			text_c (img, bx, by + (1 if cur else 0), 86, 23, lab, "uib" if cur else "ui", fg_on (col))
		def lock (d, s): d.arc ([4 * s, 1 * s, 12 * s, 11 * s], 180, 360, fill = 255, width = 2 * s); d.rounded_rectangle ([2 * s, 7 * s, 14 * s, 17 * s], 2 * s, fill = 255)
		glyph (img, sx + 8, y0 + 20, 16, 18, lock, fgc)
		def gear (d, s):							# under the lock: the settings (the config app)
			cx, cy, pts = 9 * s, 9 * s, []
			for k in range (16):						# 8 teeth: outer / inner radius in turn
				a = (k * math.pi / 8) + math.pi / 16; r = (8.6 if k % 2 == 0 else 6.2) * s
				for da in (-0.16, 0.16): pts.append ((cx + r * math.cos (a + da), cy + r * math.sin (a + da)))
			d.polygon (pts, fill = 255); d.ellipse ([cx - 2.8 * s, cy - 2.8 * s, cx + 2.8 * s, cy + 2.8 * s], fill = 0)
		glyph (img, sx + 7, y0 + 45, 18, 18, gear, fgc)
		def power (d, s): d.arc ([2 * s, 3 * s, 16 * s, 17 * s], 300, 240, fill = 255, width = 2 * s); d.line ([9 * s, 1 * s, 9 * s, 9 * s], fill = 255, width = 2 * s)
		glyph (img, sx + sw_ - 26, y0 + 44, 18, 18, power, (190, 50, 44))
		sep (x - 7)
		for k, c, r in (("config", "System", False), ("terminal", None, True), ("fileviewer", None, False)): launcher (k, c, running = r)
		launcher ("trash", last = True)
		if opened: self.subpanel (img, opened[0], y0, opened[1], hover)

	def subpanel (self, img, cx, dock_y, cat, hover, running = ()):
		p = self.p; items = [("tetris", "Tetris"), ("snake", "Snake"), ("minesweeper", "Minesweeper"), ("2048", "2048"), ("doom", "Doom"), ("gamelib", "Game Library")]
		pw, rh = 214, 38; ph = 40 + len (items) * rh + 10; px, py = cx - pw // 2, dock_y - 12 - ph
		rrect (img, px, py, pw, ph, 12, p["menu"]); ring (img, px, py, pw, ph, 12, shade (p["dock"], 0.5), alpha = 150)
		glyph (img, cx - 8, py + ph - 1, 16, 8, lambda d, s: d.polygon ([(0, 0), (16 * s, 0), (8 * s, 8 * s)], fill = 255), p["menu"])
		text_l (img, px + 14, py + 6, 26, cat, "uib", p["menu_fg"])
		put (img, px + 10, py + 36, Image.new ("L", (pw - 20, 1), 255), shade (p["menu"], 0.75))
		for i, (n, l) in enumerate (items):
			iy = py + 42 + i * rh; fc = p["menu_fg"]
			if i == hover: rrect (img, px + 6, iy, pw - 12, rh - 2, 7, (lighten (p["hi"], 0.15), p["hi"])); fc = p["hi_fg"]; self.pointer = (px + 120, iy + 20)
			draw_icon (img, n, px + 14, iy + 3, 30); text_l (img, px + 54, iy, rh - 2, l, "ui", fc)
			if n in running: rrect (img, px + pw - 22, iy + rh // 2 - 4, 6, 6, 3, fc if i == hover else p["hi"])	# it runs

	def agenda (self, img, x = 12, y = 42):
		"""The agenda widget on the desktop (Apps/agenda: the calendar's next appointments, today
		first; under every window), part of the wallpaper: no card, no shadow -- its text and an
		etched line straight on the desktop (a see-through window, WIN_FLAG_ALPHA), the ink chosen
		from the wallpaper's brightness under it (wallpaper_buffer): engraved on a light one,
		white with a soft shadow on a dark one."""
		p = self.p; w = 344
		rows = [("Today", "Dentist at 17:30"), ("Tue 29 Sep", "Onyx: test the dock on the Pi"),
			("Thu 1 Oct", "Pay the rent"), ("Sat 10 Oct", "Birthday party")]
		h = 34 + len (rows) * 21 + 8
		back = tuple (int (v) for v in np.asarray (img.crop ((x, y, x + w, y + h)), float).reshape (-1, 3).mean (0))
		light = (0.3 * back[0] + 0.59 * back[1] + 0.11 * back[2]) > 128
		ink = (24, 34, 50) if light else (250, 252, 255)
		dim = mix (ink, back, 0.38)
		def ftext (tx, ty, th, s_, font, colour):
			b = F[font].getbbox ("Hg"); oy = ty + (th - (b[3] - b[1])) // 2 - b[1]
			if light: text (img, tx, oy + 1, s_, font, lighten (back, 0.6))			# engraved: light below
			else:
				m = Image.new ("L", (tw (s_, font) + 12, th + 12), 0)
				ImageDraw.Draw (m).text ((6, oy - ty + 6), s_, font = F[font], fill = 255)
				put (img, tx - 6, ty - 5, m.filter (ImageFilter.GaussianBlur (1.8)), (0, 0, 0), 190)
			text (img, tx, oy, s_, font, colour)
		def etched (lx, ly, lw):
			put (img, lx, ly, Image.new ("L", (lw, 1), 255), shade (back, 0.7) if light else (0, 0, 0), 200 if light else 110)
			put (img, lx, ly + 1, Image.new ("L", (lw, 1), 255), lighten (back, 0.5) if light else (255, 255, 255), 200 if light else 70)
		rrect (img, x + 12, y + 9, 16, 17, 3, (255, 255, 255)); ring (img, x + 12, y + 9, 16, 17, 3, (0, 0, 0), alpha = 90)
		rrect (img, x + 12, y + 9, 16, 6, 3, (210, 50, 44), corners = (True, True, False, False))
		ftext (x + 36, y + 5, 26, "Next appointments", "uib", ink)
		ftext (x + 36 + tw ("Next appointments", "uib") + 6, y + 5, 26, "(4)", "ui", dim)
		etched (x + 10, y + 33, w - 20)
		for i, (dt, note) in enumerate (rows):
			ry = y + 38 + i * 21
			if dt == "Today":
				rrect (img, x + 12, ry + 1, 50, 18, 6, (lighten (p["hi"], 0.15), p["hi"])); text_c (img, x + 12, ry + 1, 50, 18, dt, "small", p["hi_fg"])
				ftext (x + 100, ry, 20, note, "uib", ink)
			else:
				ftext (x + 14, ry, 20, dt, "small", dim)
				ftext (x + 100, ry, 20, note, "ui", ink)

	def cursor (self, img, x, y):
		pts = [(0, 0), (0, 17), (4.5, 13), (7.5, 19.5), (10, 18.5), (7, 12), (12.5, 12)]
		glyph (img, x - 1, y - 1, 16, 23, lambda d, s: d.polygon ([((a + 1) * s, (b + 1) * s) for a, b in pts], fill = 255), (0, 0, 0))
		glyph (img, x - 1, y - 1, 16, 23, lambda d, s: d.polygon ([((a + 1) * s + 1.3 * s * (0.6 if a else 1), (b + 1) * s + 1.6 * s * (1 if b < 16 else -0.6)) for a, b in [(0, 0), (0, 14.2), (4.3, 10.6), (7.3, 16.8), (8.4, 16.3), (5.4, 10), (9.6, 10)]], fill = 255), (255, 255, 255))

	def desktop (self):
		img = Image.new ("RGB", (W, H)); self.pointer = (470, 360)
		self.backdrop (img)
		self.agenda (img, 12, 42)						# (its place: top left, as today)
		fx, fy = self.window (img, 600, 40, 340, 256, "fractal", False); self.fractal (img, fx, fy)
		cx, cy = self.window (img, 24, 196, 280, 296, "tinycalc", False); self.calc (img, cx, cy)
		tx, ty = self.window (img, 360, 214, 560, 360, "terminal", True); self.terminal (img, tx, ty, 560, 360)
		self.menubar (img)
		self.dock (img)
		self.cursor (img, *self.pointer)
		return img

def colours ():
	"""One colour per frame, the rest computed: the same window from the six colour themes, each
	frame's shades from its one colour through the grey profile -- without an outline, with a
	dark 1-px one (the frame's colour, very dark), with a black one."""
	bases = [(c, "%s  0x%02X%02X%02X" % ((n,) + c)) for n, c in THEMES]
	rows = [(None, "no outline"), ("dark", "1-px dark outline"), ("black", "1-px black outline")]
	img = Image.new ("RGB", (1284, 70 + 150 * len (rows)))
	img.paste (grad (1284, img.size[1], lighten (PAL_CDE["back"], 0.12), PAL_CDE["back2"]), (0, 0))
	for i, (c, name) in enumerate (bases):
		text_c (img, 12 + i * 212, 8, 200, 20, name.split ()[0], "uib", (24, 34, 50))
		text_c (img, 12 + i * 212, 28, 200, 18, name.split ()[1] + (" (inactive)" if name.startswith ("Grey") else ""), "small", (40, 50, 66))
	for r, (ol, label) in enumerate (rows):
		y = 56 + r * 150
		text_l (img, 14, y, 18, label, "uib", (24, 34, 50))
		for i, (c, name) in enumerate (bases):
			pal = dict (PAL_CDE, active = c, active_fg = fg_on (c))
			m = Modern (pal, ol); x = 12 + i * 212
			cx, cy = m.window (img, x, y + 22, 192, 88, "window", True)
			img.paste (grad (192, 88, lighten (pal["app"], 0.15), shade (pal["app"], 0.96)), (cx, cy))
			framed (img, cx + 46, cy + 22, 100, 40, pal["app"], False, "OK", "uib", pal["app_fg"])
	return img

def stretched ():
	"""The buttons are drawn at their size: their gradients computed at each redraw from their
	height (the grey profile spread over its rows), the edges and the corners' radius fixed --
	stretched at will. The same framed button at five sizes, one pressed."""
	img = Image.new ("RGB", (1024, 200)); img.paste (grad (1024, 200, lighten (PAL_CDE["back"], 0.12), PAL_CDE["back2"]), (0, 0))
	face, fg = PAL_CDE["app"], PAL_CDE["app_fg"]
	rrect (img, 10, 10, 1004, 180, 12, (lighten (face, 0.2), shade (face, 0.96))); ring (img, 10, 10, 1004, 180, 12, tone (face, 70), alpha = 170)
	x = 30
	for w, h, label, pressed in ((56, 24, "OK", False), (96, 34, "Cancel", False), (140, 48, "Stretched", False),
				     (200, 72, "Taller", False), (140, 48, "Pressed", True), (230, 36, "A wide one", False)):
		y = 100 - h // 2
		framed (img, x, y, w, h, face, pressed, label, "uib" if h >= 40 else "ui", fg)
		text_c (img, x, 150, w, 18, "%d x %d" % (w, h), "small", mix (fg, face, 0.4))
		x += w + 22
	return img

def main ():
	for pal in (PAL_CDE, PAL_WIN):
		p = os.path.join (OUT, pal["name"] + ".png"); Modern (pal).desktop ().save (p, optimize = True); print ("wrote", p)
	p = os.path.join (OUT, "cde-modern-colours.png"); colours ().save (p, optimize = True); print ("wrote", p)
	p = os.path.join (OUT, "cde-modern-outline.png"); Modern (PAL_CDE, "dark").desktop ().save (p, optimize = True); print ("wrote", p)
	p = os.path.join (OUT, "cde-modern-buttons.png"); stretched ().save (p, optimize = True); print ("wrote", p)

if __name__ == "__main__":
	main ()
