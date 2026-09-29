#!/usr/bin/env python3
"""mockup_daw.py -- the first mock-ups of Onyx's DAW (a port of Koton Studio's philosophy: one thinks
in harmony -- a chord track pinned at the bottom, every part generated from it: chord articulations,
melodic lines, arpeggiators, drum patterns, euclidean polyrhythms). See docs/daw/README.md.

    python3 tools/screenshot/mockup_daw.py  -> docs/daw/mockups/daw-*.png

The screen is the Pi's 1920 x 1080 (cmdline.txt), the global menu bar at the top, the app maximised
in an Onyx frame (the Slate theme: a dark frame suits a dark studio). What would be wtk widgets
(buttons, dropdowns, fields, lists, tabs, sliders) is drawn as wtk draws them, darkened; what would be
a hand-drawn canvas the size of the view (the ruler, the lanes, the grids, the piano roll, the rings)
is drawn flat. Knobs and VU meters would be new wtk widgets (vpaint.h's arcs).
Everything is drawn at K times the size, then scaled down (anti-aliasing).
"""
import math, os, random
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname (os.path.abspath (__file__))
ROOT = os.path.dirname (os.path.dirname (HERE))
OUT = os.path.join (ROOT, "docs", "daw", "mockups")
W, H, K = 1920, 1080, 2
DJ = "/usr/share/fonts/truetype/dejavu/"
def _f (name, size): return ImageFont.truetype (DJ + name, size * K)
F = dict (ui = _f ("DejaVuSans.ttf", 12), uib = _f ("DejaVuSans-Bold.ttf", 12), small = _f ("DejaVuSans.ttf", 10),
	  smallb = _f ("DejaVuSans-Bold.ttf", 10), tiny = _f ("DejaVuSans.ttf", 9), title = _f ("DejaVuSans-Bold.ttf", 13),
	  big = _f ("DejaVuSans-Bold.ttf", 16), huge = _f ("DejaVuSans-Bold.ttf", 22), lcd = _f ("DejaVuSansMono-Bold.ttf", 20),
	  mono = _f ("DejaVuSansMono.ttf", 11), roman = _f ("DejaVuSerif-Bold.ttf", 20), menu = _f ("DejaVuSans.ttf", 13),
	  menub = _f ("DejaVuSans-Bold.ttf", 13))

# ---- the palette: a dark studio inside Onyx's Slate frame ----------------------------------------------
BG = (24, 27, 33); PANEL = (33, 37, 45); PANEL2 = (42, 47, 57); FACE = (54, 60, 72); LINE = (58, 64, 76)
LANE = (28, 31, 38); LANE2 = (31, 35, 42); TEXT = (218, 223, 231); DIM = (140, 150, 165); FAINT = (92, 100, 114)
ACC = (73, 176, 196); ACC2 = (52, 128, 146); PLAY = (255, 198, 70); REC = (226, 80, 76)
FIELD = (20, 23, 28)
SLATE = (58, 68, 88)
TRK = dict (lead = (226, 164, 84), counter = (206, 98, 124), bass = (118, 186, 112), drums = (222, 112, 82),
	    perc = (168, 132, 222), chords = (84, 144, 232))
FUNC = dict (T = (72, 136, 226), S = (70, 170, 132), D = (226, 136, 68))		# tonic, subdominant, dominant

def lighten (c, k): return tuple (int (v + (255 - v) * k) for v in c)
def shade (c, k): return tuple (int (v * k) for v in c)
def mix (a, b, t): return tuple (int (a[i] + (b[i] - a[i]) * t) for i in range (3))
def A (c, a): return tuple (c[:3]) + (a,)

class Canvas:
	"""The screen at K times its size: every coordinate is in screen pixels."""
	def __init__ (self, bg = BG):
		self.img = Image.new ("RGB", (W * K, H * K), bg); self.d = ImageDraw.Draw (self.img, "RGBA")
	def rect (self, x, y, w, h, fill = None, r = 0, outline = None, width = 1, corners = None):
		if w <= 0 or h <= 0: return
		b = [x * K, y * K, (x + w) * K - 1, (y + h) * K - 1]
		if r: self.d.rounded_rectangle (b, r * K, fill = fill, outline = outline, width = int (width * K), corners = corners)
		else: self.d.rectangle (b, fill = fill, outline = outline, width = int (width * K))
	def grad (self, x, y, w, h, top, bottom, r = 0, corners = None):
		g = Image.new ("RGB", (w * K, h * K))
		gd = ImageDraw.Draw (g)
		for i in range (h * K): gd.line ([0, i, w * K, i], fill = mix (top, bottom, i / max (1, h * K - 1)))
		m = Image.new ("L", (w * K, h * K), 0)
		ImageDraw.Draw (m).rounded_rectangle ([0, 0, w * K - 1, h * K - 1], r * K, fill = 255, corners = corners)
		self.img.paste (g, (int (x * K), int (y * K)), m)
	def line (self, pts, fill, width = 1):
		self.d.line ([(px * K, py * K) for px, py in pts], fill = fill, width = max (1, int (width * K)), joint = "curve")
	def hline (self, x0, x1, y, fill, width = 1): self.rect (x0, y, x1 - x0, width, fill)
	def vline (self, x, y0, y1, fill, width = 1): self.rect (x, y0, width, y1 - y0, fill)
	def ellipse (self, cx, cy, r, fill = None, outline = None, width = 1):
		self.d.ellipse ([(cx - r) * K, (cy - r) * K, (cx + r) * K, (cy + r) * K], fill = fill, outline = outline, width = int (width * K))
	def arc (self, cx, cy, r, a0, a1, fill, width = 2):
		self.d.arc ([(cx - r) * K, (cy - r) * K, (cx + r) * K, (cy + r) * K], a0, a1, fill = fill, width = int (width * K))
	def poly (self, pts, fill): self.d.polygon ([(px * K, py * K) for px, py in pts], fill = fill)
	def tw (self, s, font = "ui"): return F[font].getlength (s) / K
	def text (self, x, y, s, font = "ui", fill = TEXT, anchor = "la"):
		self.d.text ((x * K, y * K), s, font = F[font], fill = fill, anchor = anchor)
	def text_l (self, x, y, h, s, font = "ui", fill = TEXT): self.text (x, y + h / 2, s, font, fill, "lm")
	def text_c (self, x, y, w, h, s, font = "ui", fill = TEXT): self.text (x + w / 2, y + h / 2, s, font, fill, "mm")
	def text_r (self, x, y, h, s, font = "ui", fill = TEXT): self.text (x, y + h / 2, s, font, fill, "rm")
	def clip (self, x, y, w, h):
		"""A sub-canvas: drawing outside (x, y, w, h) is lost (the lanes' blocks run off the view)."""
		return Clip (self, x, y, w, h)
	def save (self, name):
		os.makedirs (OUT, exist_ok = True)
		p = os.path.join (OUT, name)
		self.img.resize ((W, H), Image.LANCZOS).save (p); print ("wrote", os.path.relpath (p, ROOT))

class Clip (Canvas):
	def __init__ (self, parent, x, y, w, h):
		self.parent, self.x, self.y, self.w, self.h = parent, x, y, w, h
		self.img = Image.new ("RGBA", (w * K, h * K), (0, 0, 0, 0)); self.d = ImageDraw.Draw (self.img, "RGBA")
		self.ox, self.oy = x, y
	# the sub-canvas has its own origin: shift every call
	def rect (self, x, y, *a, **k): Canvas.rect (self, x - self.ox, y - self.oy, *a, **k)
	def grad (self, x, y, *a, **k): Canvas.grad (self, x - self.ox, y - self.oy, *a, **k)
	def line (self, pts, *a, **k): Canvas.line (self, [(px - self.ox, py - self.oy) for px, py in pts], *a, **k)
	def ellipse (self, cx, cy, *a, **k): Canvas.ellipse (self, cx - self.ox, cy - self.oy, *a, **k)
	def arc (self, cx, cy, *a, **k): Canvas.arc (self, cx - self.ox, cy - self.oy, *a, **k)
	def poly (self, pts, fill): Canvas.poly (self, [(px - self.ox, py - self.oy) for px, py in pts], fill)
	def text (self, x, y, *a, **k): Canvas.text (self, x - self.ox, y - self.oy, *a, **k)
	def done (self): self.parent.img.paste (self.img, (int (self.x * K), int (self.y * K)), self.img)

# ---- the widgets (wtk's shapes, the dark studio's colours) ----------------------------------------------
def button (c, x, y, w, h, label = "", kind = "face", font = "ui", icon = None, pressed = False):
	face = dict (face = FACE, accent = ACC2, flat = PANEL2, rec = (120, 44, 44)).get (kind, FACE)
	if pressed: face = shade (face, 0.8)
	c.grad (x, y, w, h, lighten (face, 0.12), shade (face, 0.9), r = 4)
	c.rect (x, y, w, h, r = 4, outline = A (shade (face, 0.55), 255))
	c.hline (x + 3, x + w - 3, y + 1, A ((255, 255, 255), 28))
	fg = (255, 255, 255) if kind == "accent" else TEXT
	if icon and label:
		icon (c, x + 1, y, 24, h, fg); c.text_l (x + 26, y, h, label, font, fg)
	elif icon: icon (c, x, y, w, h, fg)
	elif label: c.text_c (x, y, w, h, label, font, fg)

def dropdown (c, x, y, w, h, value, label = None, font = "ui"):
	c.rect (x, y, w, h, FIELD, r = 4, outline = LINE)
	tx = x + 8
	if label: c.text_l (tx, y, h, label, "small", DIM); tx += c.tw (label, "small") + 6
	c.text_l (tx, y, h, value, font, TEXT)
	ax = x + w - 14; ay = y + h / 2
	c.poly ([(ax - 4, ay - 2), (ax + 4, ay - 2), (ax, ay + 3)], DIM)

def field (c, x, y, w, h, value, font = "ui", align = "l", fg = TEXT):
	c.rect (x, y, w, h, FIELD, r = 4, outline = LINE)
	if align == "r": c.text_r (x + w - 7, y, h, value, font, fg)
	else: c.text_l (x + 7, y, h, value, font, fg)

def spin (c, x, y, w, h, value):
	field (c, x, y, w, h, value, align = "l")
	c.vline (x + w - 16, y + 2, y + h - 2, LINE)
	c.poly ([(x + w - 12, y + h / 2 - 2), (x + w - 4, y + h / 2 - 2), (x + w - 8, y + 4)], DIM)
	c.poly ([(x + w - 12, y + h / 2 + 2), (x + w - 4, y + h / 2 + 2), (x + w - 8, y + h - 4)], DIM)

def seg (c, x, y, h, labels, active, font = "ui", pad = 14):
	"""A segmented control (wtk: radio buttons drawn as a strip)."""
	ws = [c.tw (l, font) + 2 * pad for l in labels]; tot = sum (ws)
	c.rect (x, y, tot, h, FIELD, r = 5, outline = LINE)
	xx = x
	for i, l in enumerate (labels):
		if i == active:
			c.grad (xx + 1, y + 1, int (ws[i]) - 2, h - 2, lighten (ACC2, 0.15), ACC2, r = 4)
			c.text_c (xx, y, ws[i], h, l, font, (255, 255, 255))
		else:
			c.text_c (xx, y, ws[i], h, l, font, DIM)
			if i: c.vline (xx, y + 4, y + h - 4, LINE)
		xx += ws[i]
	return x + tot

def toggle (c, x, y, on, label = None):
	c.rect (x, y, 30, 16, ACC2 if on else FACE, r = 8)
	c.ellipse (x + (22 if on else 8), y + 8, 6, fill = (235, 240, 245))
	if label: c.text_l (x + 38, y, 16, label, "ui", TEXT)

def check (c, x, y, on, label):
	c.rect (x, y, 14, 14, ACC2 if on else FIELD, r = 3, outline = LINE)
	if on: c.line ([(x + 3, y + 7), (x + 6, y + 10), (x + 11, y + 4)], (255, 255, 255), 2)
	c.text_l (x + 20, y - 1, 16, label, "ui", TEXT)

def knob (c, cx, cy, r, v, label = None, value = None, colour = ACC):
	"""A knob (new wtk widget: vpaint.h's arc): a track 270 degrees wide, the value's arc, a pointer."""
	a0, a1 = 135, 405
	c.ellipse (cx, cy, r - 3, fill = mix (FACE, BG, 0.2), outline = shade (FACE, 0.6), width = 1)
	c.arc (cx, cy, r, a0, a1, FIELD, 3)
	c.arc (cx, cy, r, a0, a0 + (a1 - a0) * v, colour, 3)
	a = math.radians (a0 + (a1 - a0) * v)
	c.line ([(cx + math.cos (a) * (r - 11), cy + math.sin (a) * (r - 11)), (cx + math.cos (a) * (r - 5), cy + math.sin (a) * (r - 5))], TEXT, 2)
	if label: c.text (cx, cy + r + 4, label, "tiny", DIM, "ma")
	if value: c.text (cx, cy + r + 15, value, "tiny", TEXT, "ma")

def hslider (c, x, y, w, v, colour = ACC):
	c.rect (x, y + 5, w, 4, FIELD, r = 2)
	c.rect (x, y + 5, int (w * v), 4, colour, r = 2)
	kx = x + w * v
	c.rect (kx - 5, y, 10, 14, lighten (FACE, 0.3), r = 3, outline = shade (FACE, 0.5))

def vu (c, x, y, w, h, lv, rv, vertical = True):
	"""A stereo VU meter (new wtk widget): green -> amber -> red segments."""
	c.rect (x, y, w, h, FIELD, r = 2)
	for i, v in enumerate ((lv, rv)):
		if vertical:
			bw = (w - 3) / 2; bx = x + 1 + i * (bw + 1); n = int ((h - 2) / 3)
			for s in range (n):
				if s / n > v: break
				col = (84, 200, 110) if s / n < 0.7 else (236, 190, 70) if s / n < 0.88 else (226, 80, 70)
				c.rect (bx, y + h - 2 - (s + 1) * 3, bw, 2, col)
		else:
			bh = (h - 3) / 2; by = y + 1 + i * (bh + 1); n = int ((w - 2) / 3)
			for s in range (n):
				if s / n > v: break
				col = (84, 200, 110) if s / n < 0.7 else (236, 190, 70) if s / n < 0.88 else (226, 80, 70)
				c.rect (x + 1 + s * 3, by, 2, bh, col)

def section (c, x, y, w, label, colour = DIM):
	c.text (x, y, label.upper (), "smallb", colour)
	c.hline (x + c.tw (label.upper (), "smallb") + 8, x + w, y + 7, LINE)

# icons: tiny vector glyphs
def ic_play (c, x, y, w, h, fg): cx, cy = x + w / 2 + 1, y + h / 2; c.poly ([(cx - 5, cy - 7), (cx - 5, cy + 7), (cx + 7, cy)], fg)
def ic_stop (c, x, y, w, h, fg): cx, cy = x + w / 2, y + h / 2; c.rect (cx - 5, cy - 5, 11, 11, fg, r = 1)
def ic_rec (c, x, y, w, h, fg): c.ellipse (x + w / 2, y + h / 2, 6, fill = REC)
def ic_loop (c, x, y, w, h, fg):
	cx, cy = x + w / 2, y + h / 2; c.arc (cx, cy, 7, 30, 330, fg, 2); c.poly ([(cx + 5, cy - 8), (cx + 10, cy - 3), (cx + 3, cy - 2)], fg)
def ic_back (c, x, y, w, h, fg):
	cx, cy = x + w / 2, y + h / 2; c.rect (cx - 7, cy - 6, 2, 12, fg); c.poly ([(cx + 6, cy - 6), (cx + 6, cy + 6), (cx - 4, cy)], fg)
def ic_metro (c, x, y, w, h, fg):
	cx, cy = x + w / 2, y + h / 2; c.poly ([(cx - 6, cy + 7), (cx + 6, cy + 7), (cx + 2, cy - 7), (cx - 2, cy - 7)], None)
	c.line ([(cx - 6, cy + 7), (cx - 2, cy - 7), (cx + 2, cy - 7), (cx + 6, cy + 7), (cx - 6, cy + 7)], fg, 1.5); c.line ([(cx, cy + 4), (cx + 5, cy - 5)], fg, 1.5)
def ic_undo (c, x, y, w, h, fg):
	cx, cy = x + w / 2, y + h / 2; c.arc (cx + 1, cy + 2, 6, 180, 360, fg, 2); c.poly ([(cx - 9, cy + 1), (cx - 2, cy + 1), (cx - 5, cy + 6)], fg)
def ic_redo (c, x, y, w, h, fg):
	cx, cy = x + w / 2, y + h / 2; c.arc (cx - 1, cy + 2, 6, 180, 360, fg, 2); c.poly ([(cx + 9, cy + 1), (cx + 2, cy + 1), (cx + 5, cy + 6)], fg)
def ic_save (c, x, y, w, h, fg):
	cx, cy = x + 13, y + h / 2; c.rect (cx - 6, cy - 6, 12, 12, None, r = 1, outline = fg, width = 1.5); c.rect (cx - 3, cy - 6, 6, 4, fg)
def ic_ai (c, x, y, w, h, fg):
	cx, cy = x + 13, y + h / 2
	for a in range (4):
		t = math.radians (a * 90); c.line ([(cx, cy), (cx + math.cos (t) * 7, cy + math.sin (t) * 7)], fg, 1.5)
	c.poly ([(cx, cy - 7), (cx + 2, cy - 2), (cx + 7, cy), (cx + 2, cy + 2), (cx, cy + 7), (cx - 2, cy + 2), (cx - 7, cy), (cx - 2, cy - 2)], fg)
	c.ellipse (cx + 7, cy - 6, 2, fill = fg)

# ---- the Onyx desktop around the app ---------------------------------------------------------------------
MENUS = ["File", "Edit", "Track", "Insert", "Generate", "Transport", "View", "Help"]
def menubar (c):
	c.grad (0, 0, W, 28, (238, 238, 240), (214, 214, 218)); c.hline (0, W, 27, (150, 150, 156))
	x = 14; c.text_l (x, 0, 28, "Onyx", "menub", (20, 20, 24)); x += 60
	c.text_l (x, 0, 28, "Koton Studio", "menub", (20, 20, 24)); x += c.tw ("Koton Studio", "menub") + 24
	for m in MENUS: c.text_l (x, 0, 28, m, "menu", (20, 20, 24)); x += c.tw (m, "menu") + 22
	c.text_l (W - 58, 0, 28, "21:47", "menub", (20, 20, 24))
	cx, cy = W - 88, 18
	for r in (4, 8, 12): c.arc (cx, cy, r, 225, 315, (30, 30, 34), 1.6)
	c.poly ([(W - 130, 11), (W - 126, 11), (W - 120, 6), (W - 120, 22), (W - 126, 17), (W - 130, 17)], (30, 30, 34))

def frame (c, title):
	"""The app maximised in an Onyx frame (Slate): 28-px title bar, 4-px border."""
	x, y, w, h = 0, 28, W, H - 28
	c.grad (x, y, w, h, lighten (SLATE, 0.28), shade (SLATE, 0.9), r = 8, corners = (True, True, False, False))
	c.rect (x, y, w, h, r = 8, outline = shade (SLATE, 0.55), corners = (True, True, False, False))
	def tb (bx, kind):
		c.grad (bx, y + 5, 22, 19, lighten (SLATE, 0.4), SLATE, r = 5); c.rect (bx, y + 5, 22, 19, r = 5, outline = shade (SLATE, 0.5))
		g = (235, 238, 244)
		if kind == "menu": c.rect (bx + 5, y + 12, 12, 5, g, r = 2)
		elif kind == "min": c.rect (bx + 8, y + 15, 6, 4, g, r = 1)
		elif kind == "max": c.rect (bx + 6, y + 9, 10, 11, None, r = 2, outline = g, width = 1.7)
		else: c.line ([(bx + 7, y + 10), (bx + 15, y + 18)], g, 2); c.line ([(bx + 15, y + 10), (bx + 7, y + 18)], g, 2)
	tb (6, "menu")
	for i, k in enumerate (["close", "max", "min"]): tb (W - 28 - i * 25, k)
	c.text_c (0, y, W, 28, title, "title", (240, 243, 248))
	c.rect (4, 56, W - 8, H - 60, BG)
	return 4, 56, W - 8, H - 60

# ---- the song ---------------------------------------------------------------------------------------------
# F# minor (aeolian), 16 bars, a chord every two bars: (name, roman numeral, function, pitch classes)
CHORDS = [("F#m9", "i", "T", [6, 9, 1, 4, 8]), ("Bm9", "iv", "S", [11, 2, 6, 9, 1]), ("C#7sus4", "V", "D", [1, 6, 8, 11]),
	  ("Dmaj7", "VI", "S", [2, 6, 9, 1]), ("E7", "V/III", "D", [4, 8, 11, 2]), ("Amaj7", "III", "T", [9, 1, 4, 8]),
	  ("C#7", "V", "D", [1, 5, 8, 11]), ("F#m9", "i", "T", [6, 9, 1, 4, 8])]
SCALE = {6, 8, 9, 11, 1, 2, 4}
NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
BARS = 16
TRACKS = [("Lead", "lead", "Riff", "SF2 · Kalimba"), ("Counter", "counter", "Melodic line", "SF2 · Flute"),
	  ("Bass", "bass", "Arpeggiator", "Plugin · Acid bass"), ("Drums", "drums", "Drum pattern", "SF2 · Standard kit"),
	  ("Poly perc", "perc", "Polyrhythm", "SF2 · Percussion")]
MARKERS = [(0, "Intro"), (4, "Verse"), (12, "Chorus")]

# ---- the main window's parts --------------------------------------------------------------------------------
def tabs (c, x, y, w, docs, active):
	c.rect (x, y, w, 30, PANEL)
	xx = x + 6
	for i, (name, dirty) in enumerate (docs):
		tw = c.tw (name, "ui") + (44 if i else 28)
		if i == active:
			c.rect (xx, y + 4, tw, 26, BG, r = 5, corners = (True, True, False, False)); c.hline (xx + 4, xx + tw - 4, y + 4, ACC, 2)
		c.text_l (xx + 12, y + 4, 26, name, "uib" if i == active else "ui", TEXT if i == active else DIM)
		if i:
			c.text_l (xx + tw - 22, y + 4, 26, "●" if dirty else "×", "small", ACC if dirty else FAINT)
		xx += tw + 4

def toolbar (c, x, y, w, pos = "6.3.2", playing = True):
	c.rect (x, y, w, 50, PANEL); c.hline (x, x + w, y + 49, LINE)
	yy = y + 9; h = 32; xx = x + 10
	button (c, xx, yy, 34, h, icon = ic_save); xx += 38
	button (c, xx, yy, 34, h, icon = ic_undo); xx += 38
	button (c, xx, yy, 34, h, icon = ic_redo); xx += 50
	button (c, xx, yy, 34, h, icon = ic_back); xx += 38
	button (c, xx, yy, 44, h, icon = ic_play, kind = "accent" if playing else "face"); xx += 48
	button (c, xx, yy, 34, h, icon = ic_stop); xx += 38
	button (c, xx, yy, 34, h, icon = ic_rec); xx += 38
	button (c, xx, yy, 34, h, icon = ic_loop, pressed = True); xx += 38
	button (c, xx, yy, 34, h, icon = ic_metro); xx += 48
	# the position display (an LCD, hand drawn)
	c.rect (xx, yy - 2, 190, h + 4, (12, 16, 20), r = 5, outline = LINE)
	c.text_l (xx + 12, yy - 2, h + 4, pos, "lcd", (120, 226, 240))
	c.text_l (xx + 108, yy - 2, 18, "BAR.BEAT.16", "tiny", FAINT)
	c.text_l (xx + 108, yy + 14, 18, "0:14.83", "small", DIM)
	xx += 202
	dropdown (c, xx, yy, 104, h, "108", "BPM"); xx += 112
	dropdown (c, xx, yy, 236, h, "F# minor (aeolian)", "Key"); xx += 244
	dropdown (c, xx, yy, 92, h, "4/4", "Meter"); xx += 100
	dropdown (c, xx, yy, 116, h, "54 %", "Swing"); xx += 124
	dropdown (c, xx, yy, 104, h, "1/16", "Snap"); xx += 124
	# on the right: the AI, the engine's core, the master meter
	rx = x + w - 10
	vu (c, rx - 150, yy + 4, 150, 24, 0.66, 0.61, vertical = False); rx -= 160
	c.text_r (rx, yy, 16, "MASTER", "smallb", DIM); c.text_r (rx, yy + 16, 16, "-6.2 dB", "small", TEXT); rx -= 70
	button (c, rx - 190, yy, 190, h, "Compose with AI…", kind = "accent", icon = ic_ai); rx -= 200
	c.text_r (rx, yy, 16, "CORE 2 · DSP", "smallb", DIM)
	c.rect (rx - 84, yy + 21, 84, 6, FIELD, r = 3); c.rect (rx - 84, yy + 21, 17, 6, (84, 200, 110), r = 3)
	c.text_r (rx - 90, yy + 16, 16, "21 %", "small", TEXT)

def track_header (c, x, y, w, h, t, i, selected = False):
	name, key, kind, sound = t; col = TRK[key]
	c.rect (x, y, w, h, PANEL2 if selected else PANEL); c.hline (x, x + w, y + h - 1, LINE)
	c.rect (x, y, 5, h - 1, col)
	c.text (x + 14, y + 7, name, "uib", TEXT)
	c.text (x + 14 + c.tw (name, "uib") + 8, y + 9, kind, "small", DIM)
	# M S R
	for j, (l, on, oc) in enumerate ((("M", False, (220, 170, 60)), ("S", i == 1 and False, (80, 180, 220)), ("R", False, REC))):
		bx = x + w - 84 + j * 26
		c.rect (bx, y + 6, 22, 18, oc if on else FACE, r = 3); c.text_c (bx, y + 6, 22, 18, l, "smallb", (20, 20, 20) if on else DIM)
	if h < 70:							# short lanes: the sound and the pan only
		dropdown (c, x + 12, y + 30, w - 72, 20, sound, font = "small")
		knob (c, x + w - 34, y + 40, 12, [0.5, 0.35, 0.5, 0.5, 0.68][i], colour = col)
		return
	dropdown (c, x + 12, y + 30, w - 72, 22, sound, font = "small")
	hslider (c, x + 12, y + 58, w - 110, [0.72, 0.6, 0.78, 0.8, 0.5][i], col)
	c.text (x + w - 92, y + 58, ["-3.1", "-5.0", "-2.2", "-1.8", "-7.4"][i], "tiny", DIM)
	knob (c, x + w - 34, y + 50, 16, [0.5, 0.35, 0.5, 0.5, 0.68][i], colour = col)
	c.text (x + w - 34, y + 69, "PAN", "tiny", FAINT, "ma")

def timeline (c, x, y, w, h, playhead = 5.55, selected = None):
	"""The arrangement view: one canvas the size of the view (the ruler, markers, tempo, lanes)."""
	HW = 268; TW = w - HW; bw = TW / BARS
	RH, MH, TH_ = 26, 20, 24; CH = 78
	nt = len (TRACKS); th = (h - RH - MH - TH_ - CH) // nt
	gx = x + HW
	def bx (bar): return gx + bar * bw
	# the header column's top boxes
	c.rect (x, y, HW, RH + MH + TH_, PANEL); c.hline (x, x + HW, y + RH + MH + TH_ - 1, LINE)
	c.text_l (x + 12, y, RH, "ARRANGEMENT", "smallb", DIM)
	button (c, x + HW - 96, y + 3, 42, 20, "+ Track", font = "small", kind = "flat")
	button (c, x + HW - 50, y + 3, 42, 20, "Mixer", font = "small", kind = "flat")
	c.text_l (x + 12, y + RH, MH, "Sections", "small", DIM)
	c.text_l (x + 12, y + RH + MH, TH_, "Tempo", "small", DIM); c.text_r (x + HW - 12, y + RH + MH, TH_, "108 → 112", "small", TEXT)
	cl = c.clip (gx, y, TW, h)
	# the ruler
	cl.rect (gx, y, TW, RH, PANEL2)
	for b in range (BARS + 1):
		X = bx (b)
		cl.vline (X, y + 10, y + RH, FAINT)
		if b < BARS: cl.text (X + 4, y + 3, str (b + 1), "small", TEXT)
		for q in range (1, 4): cl.vline (X + q * bw / 4, y + 19, y + RH, LINE)
	# the loop range (bars 5-12)
	cl.rect (bx (4), y, bx (12) - bx (4), 6, A (ACC, 150))
	# section markers
	cl.rect (gx, y + RH, TW, MH, LANE2)
	for b, name in MARKERS:
		X = bx (b); cl.poly ([(X, y + RH + 2), (X + 8, y + RH + 2), (X, y + RH + 12)], PLAY)
		cl.rect (X + 1, y + RH + 2, c.tw (name, "smallb") + 16, MH - 4, A (PLAY, 50), r = 3)
		cl.text (X + 10, y + RH + 4, name, "smallb", PLAY)
	# the tempo lane: a line with its break points
	ty = y + RH + MH; cl.rect (gx, ty, TW, TH_, LANE)
	pts = [(gx, ty + 16), (bx (12), ty + 16), (bx (12.8), ty + 7), (bx (16), ty + 7)]
	cl.line (pts, (236, 190, 70), 1.5)
	for p in pts[1:3]: cl.ellipse (p[0], p[1], 3, fill = (236, 190, 70))
	# the lanes
	ly = ty + TH_
	rnd = random.Random (4)
	for i, t in enumerate (TRACKS):
		yy = ly + i * th
		track_header (c, x, yy, HW, th, t, i, selected = (selected == i))
		cl.rect (gx, yy, TW, th, LANE if i % 2 == 0 else LANE2); cl.hline (gx, gx + TW, yy + th - 1, LINE)
		for b in range (BARS): cl.vline (bx (b), yy, yy + th, (40, 45, 54))
		col = TRK[t[1]]
		blocks = dict (lead = [(0, 4, "Lead intro"), (4, 8, "Lead verse A"), (12, 16, "Lead chorus")],
			       counter = [(4, 12, "Counter · wave contour")], bass = [(0, 16, "Arpeggiator · up 1/8 · root + 5th")],
			       drums = [(0, 4, "Afro 6/8 · light"), (4, 11, "Standard · normal"), (11, 12, "Fill"), (12, 16, "Standard · dense")],
			       perc = [(4, 16, "Euclid E(3,8) · E(5,12) · E(7,16)")])[t[1]]
		for (b0, b1, label) in blocks:
			X0, X1 = bx (b0) + 1, bx (b1) - 1; Y0, Y1 = yy + 4, yy + th - 5
			sel = (selected == i and b0 == 4)
			cl.rect (X0, Y0, X1 - X0, Y1 - Y0, A (shade (col, 0.55), 235), r = 4, outline = lighten (col, 0.5) if sel else shade (col, 0.8), width = 2 if sel else 1)
			cl.rect (X0, Y0, X1 - X0, 16, A (shade (col, 0.75), 255), r = 4, corners = (True, True, False, False))
			cl.text (X0 + 6, Y0 + 2, label, "smallb", (250, 250, 250))
			by0, by1 = Y0 + 20, Y1 - 4
			k = t[1]
			if k == "lead":
				p = 64
				for s in range (int ((b1 - b0) * 4)):
					if rnd.random () < 0.28: continue
					p = max (58, min (80, p + rnd.choice ([-3, -2, -1, 0, 1, 2, 3])))
					ny = by1 - (p - 56) / 26 * (by1 - by0)
					cl.rect (bx (b0 + s / 4) + 1, ny, bw / 4 * rnd.choice ([1, 1, 2]) - 2, 3, lighten (col, 0.45))
			elif k == "counter":
				pp = []
				for s in range (int ((b1 - b0) * 8) + 1):
					X = bx (b0 + s / 8); v = 0.5 + 0.35 * math.sin (s / 5.0) + 0.1 * math.sin (s / 1.7)
					pp.append ((X, by1 - v * (by1 - by0)))
				cl.line (pp, A (lighten (col, 0.4), 200), 1.5)
				for s in range (0, int ((b1 - b0) * 2)):
					cl.rect (bx (b0 + s / 2) + 1, by1 + 1, bw / 2 * 0.8, 2, lighten (col, 0.3))
			elif k == "bass":
				for s in range (int ((b1 - b0) * 8)):
					ch = CHORDS[int ((b0 + s / 8) // 2)]; pc = ch[3][0] if s % 2 == 0 else ch[3][2 if len (ch[3]) > 2 else 1]
					ny = by1 - ((pc + (0 if s % 4 < 2 else 12)) % 24) / 24 * (by1 - by0)
					cl.rect (bx (b0 + s / 8) + 1, ny, bw / 8 - 2, 3, lighten (col, 0.45))
			elif k == "drums":
				rows = [(0, [0, 8]), (1, [4, 12]), (2, list (range (0, 16, 2)) if "light" not in label else [0, 3, 6, 9, 12])]
				if "dense" in label: rows[2] = (2, list (range (16)))
				if label == "Fill": rows = [(0, [0]), (1, [8, 10, 12, 13, 14, 15]), (2, [])]
				for bb in range (b0, b1):
					for r, hits in rows:
						for hh in hits:
							cl.rect (bx (bb + hh / 16) + 1, by0 + 2 + r * 9, 3, 6, lighten (col, 0.5 - r * 0.12))
			elif k == "perc":
				for bb in range (b0, b1):
					for r, (kk, nn) in enumerate (((3, 8), (5, 12), (7, 16))):
						pat = [((j * kk) % nn) < kk for j in range (nn)]
						for j, on in enumerate (pat):
							if on: cl.ellipse (bx (bb + j / nn) + 3, by0 + 5 + r * 9, 2.4, fill = lighten (col, 0.3 + r * 0.12))
	# the chord track, pinned at the bottom
	cy = ly + nt * th
	c.rect (x, cy, HW, CH, PANEL); c.rect (x, cy, 5, CH, TRK["chords"]); c.hline (x, x + HW, cy, ACC)
	c.text (x + 14, cy + 8, "Chords", "uib"); c.text (x + 14 + c.tw ("Chords", "uib") + 8, cy + 10, "harmony · drives every track", "small", DIM)
	dropdown (c, x + 12, cy + 30, 150, 22, "SF2 · Soft piano", font = "small")
	button (c, x + 168, cy + 30, 88, 22, "Cadence…", font = "small", kind = "flat")
	c.text (x + 14, cy + 58, "voice leading: closest bass", "tiny", FAINT)
	cl.rect (gx, cy, TW, CH, (22, 25, 31)); cl.hline (gx, gx + TW, cy, ACC)
	for j, (name, roman, fn, pcs) in enumerate (CHORDS):
		X0, X1 = bx (j * 2) + 2, bx (j * 2 + 2) - 2; Y0 = cy + 6; hh = CH - 12
		fc = FUNC[fn]
		cl.grad (int (X0), int (Y0), int (X1 - X0), hh, lighten (fc, 0.12), shade (fc, 0.72), r = 5)
		if selected is None and j == 0: cl.rect (X0, Y0, X1 - X0, hh, r = 5, outline = (255, 255, 255), width = 2)
		cl.text (X0 + 8, Y0 + 5, name, "uib", (255, 255, 255))
		cl.text (X1 - 8, Y0 + 5, {"T": "tonic", "S": "subdom.", "D": "dominant"}[fn], "tiny", A ((255, 255, 255), 170), "ra")
		cl.text ((X0 + X1) / 2, Y0 + hh / 2 + 8, roman, "roman", (255, 255, 255), "mm")
		# the voicing, as dots on a 2-octave strip
		for p in pcs:
			cl.ellipse (X0 + 10 + p / 12 * 22, Y0 + hh - 9, 2.2, fill = A ((255, 255, 255), 200))
	# the playhead across everything
	X = bx (playhead)
	cl.poly ([(X - 6, y), (X + 6, y), (X, y + 9)], PLAY); cl.vline (X, y, y + h, PLAY, 1.5)
	cl.done ()
	# a vertical scrollbar at the right of the lanes
	return cy + CH

def browser (c0, x, y, w, h, tab = 0):
	c0.rect (x, y, w, h, PANEL); c0.vline (x, y, y + h, LINE)
	c = c0.clip (x + 1, y, w - 1, h)
	seg (c, x + 10, y + 8, 26, ["Generators", "Sounds", "Effects"], tab, font = "small", pad = 13)
	field (c, x + 10, y + 42, w - 20, 24, "Search…", fg = FAINT)
	groups = [("Harmony", TRK["chords"], ["Chord progression", "Cadence (30 styles)", "Suggest next chord", "Voice leading"]),
		  ("Rhythm", TRK["drums"], ["Drum pattern", "Euclidean rhythm", "Polyrhythm (rings)", "Balanced rhythm"]),
		  ("Melody", TRK["lead"], ["Melodic line", "Arpeggiator", "Bass line", "Cellular automaton", "1/f fractal"]),
		  ("AI", ACC, ["Compose a piece", "Add a track"])]
	yy = y + 78
	for g, col, items in groups:
		section (c, x + 12, yy, w - 24, g); yy += 18
		for it in items:
			c.rect (x + 12, yy + 3, 14, 14, A (col, 60), r = 3, outline = col)
			c.text_l (x + 34, yy, 20, it, "ui", TEXT)
			if "IPC" in it or it in ("Arpeggiator", "Cellular automaton", "L-system", "1/f fractal"):
				c.text_r (x + w - 12, yy, 20, "plugin", "tiny", FAINT)
			yy += 20
		yy += 4
	section (c, x + 12, yy, w - 24, "Plugins (IPC)"); yy += 18
	for it, kind in (("Acid bass", "instrument"), ("FM 2-op", "instrument"), ("Karplus-Strong", "instrument"),
			 ("Delay", "effect"), ("Plate reverb", "effect"), ("Chorus", "effect")):
		c.ellipse (x + 19, yy + 10, 4, fill = (84, 200, 110))
		c.text_l (x + 34, yy, 20, it, "ui", TEXT); c.text_r (x + w - 12, yy, 20, kind, "tiny", FAINT); yy += 21
	c.done ()
	full = yy - y + 10
	if full > h:							# the list scrolls: a thin bar at the right
		c0.rect (x + w - 6, y + 74, 4, h - 80, FIELD, r = 2)
		c0.rect (x + w - 6, y + 74, 4, (h - 80) * h / full, FAINT, r = 2)

def statusbar (c, x, y, w, extra = "Snap 1/16 · Zoom 86 px/bar"):
	c.rect (x, y, w, 24, PANEL); c.hline (x, x + w, y, LINE)
	c.ellipse (x + 14, y + 12, 4, fill = (84, 200, 110))
	parts = ["Engine on core 2 · 44.1 kHz · block 256 · latency 23 ms", "Memory 41.6 MB · song arena 3.2 MB · 0 leaks (debug)",
		 "Saved 21:44 · africa.kson", extra]
	xx = x + 26
	for p in parts: c.text_l (xx, y, 24, p, "small", DIM); xx += c.tw (p, "small") + 30; c.vline (xx - 15, y + 5, y + 19, LINE)

def editor_header (c, x, y, w, title, sub, segs = None, active = 0):
	c.rect (x, y, w, 34, PANEL2); c.hline (x, x + w, y + 33, LINE)
	c.poly ([(x + 12, y + 14), (x + 20, y + 14), (x + 16, y + 20)], DIM)
	c.text_l (x + 28, y, 34, title, "uib", TEXT); c.text_l (x + 32 + c.tw (title, "uib"), y, 34, sub, "ui", DIM)
	if segs: seg (c, x + w // 2 - 170, y + 5, 24, segs, active, font = "ui")

# ---- mock-up 1: the arrangement + the chord editor --------------------------------------------------------------
def voice_grid (c, x, y, w, h, bars = 2, spb = 4):
	"""The chord articulation grid: rows = chord voices (bass, 1, 3, 5, 7, 9, 1', 3', 5'), columns = slices."""
	rows = ["5'", "3'", "1'", "9", "7", "5", "3", "1", "Bass"]
	notes = ["C#5", "A4", "F#4", "G#4", "E4", "C#4", "A3", "F#3", "F#2"]
	LW = 74; rh = (h - 22) / len (rows); cols = bars * 4 * spb; cw = (w - LW) / cols
	c.rect (x, y, w, h, LANE)
	# beat header
	for b in range (bars * 4):
		c.text (x + LW + b * spb * cw + 4, y + 4, f"{b // 4 + 1}.{b % 4 + 1}", "tiny", DIM)
	gy = y + 22
	for r, (lab, nn) in enumerate (zip (rows, notes)):
		ry = gy + r * rh
		c.rect (x, ry, LW - 4, rh - 1, PANEL2)
		c.text_l (x + 8, ry, rh, lab, "uib" if lab in ("1", "Bass") else "ui", TEXT)
		c.text_r (x + LW - 10, ry, rh, nn, "tiny", FAINT)
		for col in range (cols):
			cx = x + LW + col * cw
			base = (38, 43, 52) if (col // spb) % 2 == 0 else (34, 38, 46)
			c.rect (cx + 1, ry + 1, cw - 2, rh - 2, base, r = 2)
	# the pattern: a bossa-like comp -- bass on 1 and 3 (dotted), the chord on the off-beats, a top voice line
	GREEN = (86, 206, 150)
	def n (row, s, l, col = GREEN):
		c.rect (x + LW + s * cw + 1, gy + row * rh + 2, l * cw - 2, rh - 4, col, r = 3)
		c.rect (x + LW + s * cw + 1, gy + row * rh + 2, 3, rh - 4, lighten (col, 0.5), r = 1)
	for bar in range (bars):
		o = bar * 16
		n (8, o + 0, 3); n (8, o + 3, 1); n (8, o + 8, 3); n (8, o + 11, 1)
		for s in (2, 6, 10, 14):
			for row in (6, 5, 4): n (row, o + s, 2)
		n (3, o + 5, 2, (120, 190, 236)); n (2, o + 13, 3, (120, 190, 236))
	ph = x + LW + 5.5 * cw
	c.vline (ph, y, y + h, PLAY, 1.5)
	c.rect (x, y, w, h, outline = LINE)

def chord_editor (c, x, y, w, h):
	editor_header (c, x, y, w, "Chord editor", "F#m9  ·  degree i  ·  bars 1–2  ·  applies to every chord with this style",
		       ["Articulation", "Melodic cell", "Voicing"], 0)
	y += 34; h -= 34
	c.rect (x, y, w, h, BG)
	# left: the chord's properties (wtk dropdowns)
	px = x + 14; pw = 300
	section (c, px, y + 10, pw, "The chord")
	rows = [("Degree", "i  (F#)"), ("Colour", "9th  (7 + 9)"), ("Suspension", "None"), ("Quality", "Auto (from the key)"),
		("Inversion", "Auto · voice leading"), ("Bass", "Root, every half bar")]
	yy = y + 30
	for lab, v in rows:
		c.text_l (px, yy, 25, lab, "ui", DIM); dropdown (c, px + 104, yy, pw - 104, 25, v); yy += 30
	toggle (c, px, yy + 4, True, "Open voicing"); toggle (c, px + 150, yy + 4, False, "Lock to key"); yy += 30
	section (c, px, yy + 4, pw, "Suggest the next chord"); yy += 24
	cw = (pw - 12) / 3
	for j, (roman, name, why, fn) in enumerate ((("iv", "Bm9", "smooth", "S"), ("VI", "Dmaj7", "deceptive", "S"),
						     ("v", "C#m7", "modal", "D"))):
		cx = px + j * (cw + 6)
		c.rect (cx, yy, cw, 44, PANEL2, r = 5, outline = LINE)
		c.rect (cx, yy, 34, 44, FUNC[fn], r = 5, corners = (True, False, False, True))
		c.text_c (cx, yy, 34, 44, roman, "uib", (255, 255, 255))
		c.text (cx + 40, yy + 7, name, "uib"); c.text (cx + 40, yy + 25, why, "small", DIM)
	# middle: the style bar + the voice grid (a hand-drawn canvas)
	mx = px + pw + 24; mw = w - (mx - x) - 330
	button (c, mx, y + 10, 84, 26, "Listen", icon = ic_play, font = "ui")
	dropdown (c, mx + 92, y + 10, 180, 26, "Bossa comp (custom)", "Style")
	dropdown (c, mx + 280, y + 10, 104, 26, "2", "Bars")
	dropdown (c, mx + 392, y + 10, 128, 26, "4", "Slices/beat")
	button (c, mx + mw - 244, y + 10, 118, 26, "Save style…"); button (c, mx + mw - 120, y + 10, 120, 26, "Generate ▸", kind = "accent")
	voice_grid (c, mx, y + 46, mw, h - 58)
	# right: the track's sound chain (the plugins, over IPC) and its meter
	rx = x + w - 314; rw = 300
	c.rect (rx - 8, y, 322, h, PANEL); c.vline (rx - 8, y, y + h, LINE)
	section (c, rx, y + 10, rw, "Chords · sound chain")
	chain = [("Instrument", "SF2 · Soft piano", "GeneralUser GS", None),
		 ("Effect", "Chorus", "plugin · pid 31", [("Rate", 0.3, "0.8 Hz"), ("Depth", 0.45, "35 %"), ("Mix", 0.3, "30 %")]),
		 ("Effect", "Plate reverb", "plugin · pid 32", [("Size", 0.7, "2.4 s"), ("Damp", 0.4, "40 %"), ("Mix", 0.25, "22 %")])]
	yy = y + 30
	for kind, name, sub, ks in chain:
		hh = 34 if not ks else 98
		c.rect (rx, yy, rw - 40, hh, PANEL2, r = 6, outline = LINE)
		c.text (rx + 10, yy + 6, kind.upper (), "tiny", FAINT); c.text (rx + 10, yy + 17, name, "uib")
		c.text_r (rx + rw - 52, yy + 6, 12, sub, "tiny", DIM)
		toggle (c, rx + rw - 80, yy + 18, True) if ks else None
		if ks:
			for j, (l, v, s) in enumerate (ks): knob (c, rx + 40 + j * 80, yy + 58, 17, v, l, s)
		yy += hh + 6
	c.rect (rx, yy, rw - 40, 26, None, r = 6, outline = LINE); c.text_c (rx, yy, rw - 40, 26, "+ Add an effect", "ui", DIM)
	vu (c, rx + rw - 30, y + 30, 22, h - 44, 0.62, 0.58)

def mockup_main ():
	c = Canvas (); menubar (c); X, Y, Wc, Hc = frame (c, "Koton Studio — africa.kson")
	tabs (c, X, Y, Wc, [("Home", False), ("africa.kson", True), ("F#m_ballad.kson", False)], 1)
	toolbar (c, X, Y + 30, Wc)
	BWD = 270; ED = 356
	top = Y + 80; bottom = Y + Hc - 24
	tw = Wc - BWD
	ty = timeline (c, X, top, tw, bottom - ED - top - 6)
	browser (c, X + tw, top, BWD, bottom - ED - top - 6)
	c.rect (X, ty, Wc, 6, PANEL2); c.rect (X + Wc // 2 - 20, ty + 2, 40, 2, FAINT)		# the splitter
	chord_editor (c, X, ty + 6, Wc, bottom - ty - 6)
	statusbar (c, X, bottom, Wc)
	c.save ("daw-main.png")

# ---- mock-up 2: the piano roll, harmony-aware ---------------------------------------------------------------
def piano_roll (c, x, y, w, h):
	editor_header (c, x, y, w, "Riff editor", "Lead verse A  ·  bars 5–12  ·  24 slices/beat",
		       ["Piano roll", "Rhythm only", "Constraints"], 0)
	y += 34; h -= 34
	c.rect (x, y, w, h, BG)
	# the tool strip
	xx = x + 12; yy = y + 8
	xx = seg (c, xx, yy, 26, ["Draw", "Select", "Erase", "Slice"], 0) + 12
	dropdown (c, xx, yy, 112, 26, "1/16", "Grid"); xx += 120
	dropdown (c, xx, yy, 250, 26, "Chord tones + scale", "Snap pitch"); xx += 258
	dropdown (c, xx, yy, 110, 26, "12 %", "Humanize"); xx += 118
	button (c, xx, yy, 84, 26, "Listen", icon = ic_play); xx += 92
	button (c, xx, yy, 96, 26, "MIDI in ●"); xx += 104
	button (c, xx, yy, 138, 26, "Variation (AI)…", kind = "flat")
	# the roll: the keyboard, the chord strip above, then the grid (one canvas the size of the view)
	rx = x + 12; ry = yy + 36; rw = w - 24 - 330; rh = h - (ry - y) - 70
	KW = 56; CS = 22
	lo, hi = 60, 84; n = hi - lo + 1; nh = (rh - CS) / n
	bars = 8; gx = rx + KW; gw = rw - KW; bw = gw / bars
	c.rect (rx, ry, rw, rh, LANE)
	# the chord strip: bars 5-12 are chords 2..5 (two bars each)
	for j in range (4):
		name, roman, fn, pcs = CHORDS[2 + j]
		c.grad (int (gx + j * 2 * bw + 1), ry + 1, int (2 * bw - 2), CS - 2, lighten (FUNC[fn], 0.1), shade (FUNC[fn], 0.7), r = 4)
		c.text_l (gx + j * 2 * bw + 8, ry + 1, CS - 2, f"{name}  ·  {roman}", "smallb", (255, 255, 255))
	gy = ry + CS
	for i in range (n):
		p = hi - i; pc = p % 12; yy2 = gy + i * nh
		black = pc in (1, 3, 6, 8, 10)
		c.rect (rx, yy2, KW, nh, (26, 28, 32) if black else (206, 210, 216)); c.hline (rx, rx + KW, yy2, (120, 124, 130))
		if pc == 0 or pc == 6: c.text_r (rx + KW - 4, yy2, nh, NAMES[pc] + str (p // 12 - 1), "tiny", (40, 40, 44) if not black else (200, 200, 205))
		# a row per key: in the scale lighter, out of it darker; under each chord its tones tinted
		for j in range (4):
			pcs = CHORDS[2 + j][3]
			base = (40, 45, 55) if pc in SCALE else (29, 32, 39)
			if pc in pcs: base = mix (base, FUNC[CHORDS[2 + j][2]], 0.22)
			c.rect (gx + j * 2 * bw, yy2, 2 * bw, nh - 1, base)
	for b in range (bars * 16 + 1):
		X = gx + b * bw / 16
		c.vline (X, gy, gy + n * nh, (70, 78, 92) if b % 16 == 0 else (50, 56, 66) if b % 4 == 0 else (38, 42, 50))
	# the notes (a melody on the chord tones, with passing notes)
	mel = [(0, 3, 73), (3, 1, 71), (4, 4, 68), (8, 2, 66), (10, 2, 68), (12, 4, 71), (16, 6, 74), (22, 2, 73), (24, 4, 69), (28, 4, 66),
	       (32, 3, 76), (35, 1, 74), (36, 4, 73), (40, 8, 69), (48, 2, 71), (50, 2, 68), (52, 4, 64), (56, 8, 68),
	       (64, 4, 72), (68, 2, 73), (70, 2, 76), (72, 8, 77), (80, 4, 73), (84, 4, 71), (88, 8, 68), (96, 3, 69), (99, 1, 71), (100, 4, 73),
	       (104, 8, 76), (112, 4, 73), (116, 12, 78)]
	sel = {6, 7}
	for i, (s, l, p) in enumerate (mel):
		if not (lo <= p <= hi): continue
		X0 = gx + s * bw / 16; Y0 = gy + (hi - p) * nh
		col = (60, 214, 226) if i not in sel else (255, 214, 110)
		c.rect (X0 + 1, Y0 + 1, l * bw / 16 - 2, nh - 2, col, r = 3)
		c.rect (X0 + 1, Y0 + 1, 3, nh - 2, lighten (col, 0.6), r = 1)
	# the velocity lane
	vy = gy + n * nh + 6; vh = 52
	c.rect (rx, vy, rw, vh, LANE); c.text (rx + 6, vy + 4, "Velocity", "tiny", DIM)
	for i, (s, l, p) in enumerate (mel):
		v = 0.55 + 0.35 * ((s % 16) == 0) + 0.1 * math.sin (i)
		X = gx + s * bw / 16 + 3
		c.vline (X, vy + vh - 4 - v * (vh - 10), vy + vh - 4, (60, 214, 226), 2); c.ellipse (X + 1, vy + vh - 4 - v * (vh - 10), 3, fill = (60, 214, 226))
	ph = gx + 0.55 * 2 * bw
	c.vline (ph, ry, vy + vh, PLAY, 1.5)
	c.rect (rx, ry, rw, vy + vh - ry, outline = LINE)
	# right: the harmony helper
	hx = x + w - 318; hw = 304
	c.rect (hx - 10, y, 328, h, PANEL); c.vline (hx - 10, y, y + h, LINE)
	section (c, hx, y + 10, hw, "Harmony at the cursor")
	c.text (hx, y + 30, "C#7sus4", "huge"); c.text (hx + 130, y + 36, "V  ·  dominant  ·  bars 5–6", "small", DIM)
	# a one-octave keyboard with the chord tones lit
	ky = y + 66; kw = hw / 7
	white = [0, 2, 4, 5, 7, 9, 11]; tones = CHORDS[2][3]
	for i, pc in enumerate (white):
		on = pc in tones
		c.rect (hx + i * kw, ky, kw - 2, 58, (120, 196, 240) if on else (214, 218, 224), r = 3)
		c.text (hx + i * kw + kw / 2 - 1, ky + 44, NAMES[pc], "tiny", (20, 30, 40), "ma")
	for i, pc in ((0, 1), (1, 3), (3, 6), (4, 8), (5, 10)):
		on = pc in tones
		c.rect (hx + (i + 1) * kw - kw * 0.3 - 1, ky, kw * 0.6, 34, (60, 150, 214) if on else (30, 32, 36), r = 2)
		c.text (hx + (i + 1) * kw - 1, ky + 22, NAMES[pc], "tiny", (230, 235, 240) if on else (150, 150, 150), "ma")
	yy = ky + 72
	for lab, v in (("Chord tones", "C#  F#  G#  B"), ("Scale", "F# aeolian: F# G# A B C# D E"), ("Tension", "resolves to VI (Dmaj7) next"),
		       ("Selection", "2 notes · E5 → F#5 · 1 beat")):
		c.text (hx, yy, lab, "small", DIM); c.text (hx + 90, yy, v, "small", TEXT); yy += 20
	section (c, hx, yy + 8, hw, "Transform the selection"); yy += 30
	for i, (l, k) in enumerate ((("Snap to chord", "face"), ("Invert", "face"), ("Retrograde", "face"), ("Transpose ±", "face"),
				     ("Harmonise 3rd", "face"), ("Double 8ve", "face"))):
		button (c, hx + (i % 2) * 152, yy + (i // 2) * 32, 146, 26, l, font = "small")
	yy += 104
	section (c, hx, yy, hw, "Constraint chain (plugins)"); yy += 20
	for l in ("Keep in range F#3–B5", "Max leap: a 6th"):
		check (c, hx, yy, True, l); yy += 22

def mockup_roll ():
	c = Canvas (); menubar (c); X, Y, Wc, Hc = frame (c, "Koton Studio — africa.kson")
	tabs (c, X, Y, Wc, [("Home", False), ("africa.kson", True), ("F#m_ballad.kson", False)], 1)
	toolbar (c, X, Y + 30, Wc, pos = "5.3.1")
	BWD = 270; ED = 470
	top = Y + 80; bottom = Y + Hc - 24
	tw = Wc - BWD
	ty = timeline (c, X, top, tw, bottom - ED - top - 6, playhead = 5.55 - 0.0, selected = 0)
	browser (c, X + tw, top, BWD, bottom - ED - top - 6)
	c.rect (X, ty, Wc, 6, PANEL2); c.rect (X + Wc // 2 - 20, ty + 2, 40, 2, FAINT)
	piano_roll (c, X, ty + 6, Wc, bottom - ty - 6)
	statusbar (c, X, bottom, Wc, "Lead · 31 notes · 2 selected")
	c.save ("daw-pianoroll.png")

# ---- mock-up 3: the polyrhythm editor, the AI dialog over it ---------------------------------------------------------
def poly_editor (c, x, y, w, h):
	editor_header (c, x, y, w, "Polyrhythm", "Poly perc  ·  bars 5–16  ·  cycle 1 bar",
		       ["Rings", "Grid", "Emergent melody"], 0)
	y += 34; h -= 34
	c.rect (x, y, w, h, BG)
	# the rings (a canvas): three euclidean layers
	cx, cy, R = x + 190, y + h / 2 + 4, min (150, h / 2 - 20)
	layers = [((3, 8, 0), (222, 112, 82), "Conga low", R), ((5, 12, 1), (236, 190, 70), "Shaker", R * 0.74), ((7, 16, 2), (168, 132, 222), "Clave", R * 0.48)]
	c.ellipse (cx, cy, R + 14, fill = (30, 34, 41))
	for (k, n, rot), col, name, r in layers:
		c.ellipse (cx, cy, r, outline = shade (col, 0.5), width = 1.5)
		pat = [(((j + rot) * k) % n) < k for j in range (n)]
		pts = []
		for j in range (n):
			a = -math.pi / 2 + 2 * math.pi * j / n
			px, py = cx + math.cos (a) * r, cy + math.sin (a) * r
			if pat[j]: pts.append ((px, py))
		if len (pts) > 2: c.poly (pts, A (col, 40)); c.line (pts + [pts[0]], A (col, 180), 1.5)
		for j in range (n):
			a = -math.pi / 2 + 2 * math.pi * j / n
			px, py = cx + math.cos (a) * r, cy + math.sin (a) * r
			if pat[j]: c.ellipse (px, py, 6, fill = col, outline = lighten (col, 0.5))
			else: c.ellipse (px, py, 3, fill = shade (col, 0.5))
	a = -math.pi / 2 + 2 * math.pi * 0.3
	c.line ([(cx, cy), (cx + math.cos (a) * (R + 12), cy + math.sin (a) * (R + 12))], PLAY, 2)
	c.ellipse (cx, cy, 4, fill = PLAY)
	# the layers' table (wtk: labels, NumericUpDown, dropdowns)
	tx = x + 400; tw_ = w - 400 - 20
	section (c, tx, y + 10, tw_, "Layers")
	heads = [("Layer", 0), ("Hits", 150), ("Steps", 230), ("Rotate", 310), ("Sound", 400), ("Velocity", 610), ("Pattern", 730)]
	for l, o in heads: c.text (tx + o, y + 30, l, "small", DIM)
	yy = y + 48
	for (k, n, rot), col, name, r in layers:
		c.rect (tx, yy, tw_, 34, PANEL2, r = 5)
		c.rect (tx, yy, 5, 34, col, r = 2)
		c.text_l (tx + 14, yy, 34, f"E({k},{n})", "uib")
		spin (c, tx + 150, yy + 5, 64, 24, str (k)); spin (c, tx + 230, yy + 5, 64, 24, str (n)); spin (c, tx + 310, yy + 5, 64, 24, str (rot))
		dropdown (c, tx + 400, yy + 5, 196, 24, name, font = "small")
		hslider (c, tx + 610, yy + 10, 100, 0.8 - 0.15 * layers.index (((k, n, rot), col, name, r)), col)
		pat = [(((j + rot) * k) % n) < k for j in range (n)]
		cw = min (18, (tw_ - 740) / 16)
		for j in range (n):
			c.rect (tx + 730 + j * cw * 16 / n, yy + 9, cw * 16 / n - 2, 16, col if pat[j] else FIELD, r = 2)
		yy += 40
	button (c, tx, yy + 2, 120, 26, "+ Add a layer", kind = "flat")
	yy += 44
	section (c, tx, yy, tw_, "Emergent melody"); yy += 22
	check (c, tx, yy, True, "Pitch the hits from the chord track (monodic pick: lowest free voice)")
	dropdown (c, tx + 520, yy - 4, 200, 24, "Chord tones", "Pitches", font = "small")
	yy += 28
	for j, (l, v, s) in enumerate ((("Swing", 0.3, "54 %"), ("Humanize", 0.2, "12 ms"), ("Accent", 0.7, "+6 dB"), ("Density", 0.5, "×1"))):
		knob (c, tx + 30 + j * 90, yy + 26, 19, v, l, s)

def ai_dialog (c):
	"""A wtk Modal: the prompt goes to the `llm` helper (HTTPS, JSON) -- the reply lands on the timeline."""
	w, h = 700, 604; x, y = (W - w) // 2, 150
	c.rect (0, 28, W, H - 28, (0, 0, 0, 120))
	# the dialog in its Onyx frame
	c.grad (x, y, w, h, lighten (SLATE, 0.3), shade (SLATE, 0.9), r = 8)
	c.rect (x, y, w, h, r = 8, outline = shade (SLATE, 0.5))
	c.text_c (x, y, w, 28, "Compose with AI", "title", (240, 243, 248))
	X, Y, Wd, Hd = x + 4, y + 28, w - 8, h - 32
	c.rect (X, Y, Wd, Hd, PANEL)
	px = X + 22; pw = Wd - 44; yy = Y + 16
	c.text (px, yy, "Describe a style and an intention: the AI writes chords, sections, articulations,", "ui", DIM); yy += 18
	c.text (px, yy, "melodic lines and drums as JSON; Koton places them on the timeline.", "ui", DIM); yy += 30
	section (c, px, yy, pw, "Model", ACC); yy += 20
	dropdown (c, px, yy, 250, 28, "Gemini · gemini-2.5-flash", font = "ui"); dropdown (c, px + 260, yy, 190, 28, "Key: studio (saved)", font = "ui")
	c.text_l (px + 466, yy, 28, "Thinking", "ui", DIM); hslider (c, px + 536, yy + 7, pw - 536, 0.3)
	yy += 44
	section (c, px, yy, pw, "The piece", ACC); yy += 22
	for lab, wdg in (("Style", lambda Y_: field (c, px + 110, Y_, pw - 110, 28, "Afro-jazz, Ghibli colours")),
			 ("Length", lambda Y_: (field (c, px + 110, Y_, 80, 28, "32"), c.text_l (px + 198, Y_, 28, "bars", "ui", DIM),
						dropdown (c, px + 260, Y_, 220, 28, "Keep F# minor, 108", "Key/BPM"))),
			 ("Melody", lambda Y_: seg (c, px + 110, Y_, 28, ["Melodic line (rhythm only)", "Full melody (notes)"], 1))):
		c.text_l (px, yy, 28, lab, "ui", TEXT); wdg (yy); yy += 36
	check (c, px + 110, yy + 4, True, "Drum track"); check (c, px + 260, yy + 4, True, "Polyrhythm layer"); check (c, px + 440, yy + 4, False, "Chords in their own voice")
	yy += 34
	c.text (px, yy, "Intention (optional)", "ui", TEXT); yy += 20
	c.rect (px, yy, pw, 74, FIELD, r = 4, outline = ACC)
	c.text (px + 8, yy + 8, "A walk at dusk: a calm verse on a kora-like riff, a chorus that opens", "ui", TEXT)
	c.text (px + 8, yy + 26, "up (IV → V/III), a polyrhythm under the last 8 bars.|", "ui", TEXT)
	yy += 88
	section (c, px, yy, pw, "Generation", ACC); yy += 22
	button (c, px, yy, 120, 30, "Generate", kind = "accent"); button (c, px + 128, yy, 130, 30, "Copy the prompt"); button (c, px + 266, yy, 140, 30, "Paste a reply")
	# the progress of the request (the helper process streams its state)
	c.rect (px + 420, yy + 11, pw - 420, 8, FIELD, r = 4); c.rect (px + 420, yy + 11, (pw - 420) * 0.62, 8, ACC, r = 4)
	c.text (px + 420, yy - 4, "receiving JSON · 18.4 KB", "tiny", DIM)
	yy += 42
	c.rect (px, yy, pw, 30, PANEL2, r = 4); c.poly ([(px + 12, yy + 12), (px + 20, yy + 12), (px + 16, yy + 18)], DIM)
	c.text_l (px + 28, yy, 30, "Advanced — the JSON (request / reply)", "ui", DIM)
	yy += 44
	button (c, X + Wd - 230, yy, 100, 30, "Cancel"); button (c, X + Wd - 122, yy, 100, 30, "Place it", kind = "accent")

def mockup_poly_ai ():
	c = Canvas (); menubar (c); X, Y, Wc, Hc = frame (c, "Koton Studio — africa.kson")
	tabs (c, X, Y, Wc, [("Home", False), ("africa.kson", True), ("F#m_ballad.kson", False)], 1)
	toolbar (c, X, Y + 30, Wc, pos = "9.1.1", playing = False)
	BWD = 270; ED = 380
	top = Y + 80; bottom = Y + Hc - 24
	tw = Wc - BWD
	ty = timeline (c, X, top, tw, bottom - ED - top - 6, playhead = 8.3, selected = 4)
	browser (c, X + tw, top, BWD, bottom - ED - top - 6)
	c.rect (X, ty, Wc, 6, PANEL2); c.rect (X + Wc // 2 - 20, ty + 2, 40, 2, FAINT)
	poly_editor (c, X, ty + 6, Wc, bottom - ty - 6)
	statusbar (c, X, bottom, Wc, "llm helper: POST generateContent …")
	ai_dialog (c)
	c.save ("daw-poly-ai.png")

if __name__ == "__main__":
	mockup_main (); mockup_roll (); mockup_poly_ai ()
