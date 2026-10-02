#!/usr/bin/env python3
"""mockup_photos.py -- the first mock-ups of Onyx's Photos app: the library (every photo by day, the favourites,
the albums, the folders), a photo shown with its film strip and its details (EXIF), a photo edited (crop, light,
colour, filters), the albums and what a photo can be sent to. See docs/photos/README.md.

    python3 tools/screenshot/mockup_photos.py  -> docs/photos/mockups/photos-*.png

On the real desktop (screenshots/desktop.png, 1024 x 768); the drawing helpers are mockup_archiver.py's. The photos
are drawn here (made-up landscapes, no real picture).
"""
import os, sys, math, random
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, HERE)
import mockup_archiver as M

M.W, M.H = 1024, 768
M.OUT = os.path.join (M.ROOT, "docs", "photos", "mockups")
K = M.K
DESK = Image.open (os.path.join (M.ROOT, "screenshots", "desktop.png")).convert ("RGB")
TEXT, DIM, FACE, SEL, WHITE, LIST, FAINT = M.TEXT, M.DIM, M.FACE, M.SEL, (255, 255, 255), M.LIST, M.FAINT
SIDE = (226, 216, 209)
DARK, DARK2 = (28, 30, 34), (44, 47, 53)
RED, AMBER = M.RED, M.AMBER
M.F["huge"] = M._f ("DejaVuSans-Bold.ttf", 22)
M.F["h2"] = M._f ("DejaVuSans-Bold.ttf", 16)
M.F["mid"] = M._f ("DejaVuSans.ttf", 14)

def screen (app = "Photos", menus = ("File", "Edit", "View", "Photo", "Help")):
	c = M.Canvas ()
	c.img.paste (DESK.resize ((M.W * K, M.H * K), Image.LANCZOS), (0, 0)); c.d = ImageDraw.Draw (c.img, "RGBA")
	c.rect (0, 0, 520, 27, (230, 222, 217))
	x = 18; c.text_l (x, 0, 27, "Onyx", "menu"); x += c.tw ("Onyx", "menu") + 18
	c.text_l (x, 0, 27, app, "menub"); x += c.tw (app, "menub") + 20
	for m in menus: c.text_l (x, 0, 27, m, "menu"); x += c.tw (m, "menu") + 18
	return c

# ---- the photos (drawn: landscapes, a town, flowers...) ---------------------------------------------------------------
def lerp (a, b, t): return tuple (int (a[i] + (b[i] - a[i]) * t) for i in range (3))
def photo (seed, w, h, kind = None, adjust = None):
	"""A made-up photo, w x h px (already at K)."""
	r = random.Random (seed)
	kinds = ["sea", "mountain", "sunset", "town", "forest", "flowers", "lake", "field", "snow", "night"]
	kind = kind or kinds[seed % len (kinds)]
	img = Image.new ("RGB", (w, h)); d = ImageDraw.Draw (img)
	skies = { "sea": ((110, 170, 230), (200, 225, 245)), "mountain": ((90, 150, 220), (190, 215, 240)), "sunset": ((70, 60, 120), (250, 150, 80)),
		  "town": ((150, 190, 230), (220, 230, 240)), "forest": ((150, 190, 220), (210, 225, 230)), "flowers": ((120, 180, 235), (210, 230, 245)),
		  "lake": ((120, 165, 210), (220, 225, 235)), "field": ((100, 160, 225), (230, 230, 220)), "snow": ((150, 180, 215), (235, 240, 245)),
		  "night": ((10, 15, 40), (40, 50, 90)) }
	top, bot = skies[kind]
	hz = int (h * r.uniform (0.42, 0.6))
	for y in range (hz): d.line ([(0, y), (w, y)], lerp (top, bot, y / max (1, hz)))
	if kind == "sunset": d.ellipse ([w * 0.55, hz - h * 0.12, w * 0.55 + h * 0.18, hz + h * 0.06], (255, 205, 120))
	if kind in ("sea", "field", "flowers", "town"):
		for k in range (r.randint (2, 4)):
			cx, cy, s = r.uniform (0, w), r.uniform (0, hz * 0.6), r.uniform (h * 0.05, h * 0.1)
			for j in range (4): d.ellipse ([cx + j * s * 0.7 - s, cy - s * 0.6 + (j % 2) * s * 0.2, cx + j * s * 0.7 + s, cy + s * 0.6], (250, 250, 252))
	if kind == "night":
		for k in range (80): x, y = r.uniform (0, w), r.uniform (0, hz); d.point ((x, y), (255, 255, 230))
		d.ellipse ([w * 0.75, h * 0.1, w * 0.75 + h * 0.08, h * 0.18], (245, 240, 210))
	# the land
	if kind in ("mountain", "snow", "lake"):
		pts = [(0, hz)]
		x = 0
		while x < w:
			x += r.uniform (w * 0.08, w * 0.2); pts.append ((x, hz - r.uniform (h * 0.1, h * 0.32)))
		pts += [(w, hz), (w, h), (0, h)]
		d.polygon (pts, (110, 120, 140) if kind != "snow" else (225, 232, 240))
		for i in range (1, len (pts) - 3):
			px, py = pts[i]
			if kind != "lake": d.polygon ([(px, py), (px - w * 0.04, py + h * 0.06), (px + w * 0.04, py + h * 0.06)], (245, 247, 250))
	ground = { "sea": (40, 110, 160), "mountain": (80, 130, 70), "sunset": (40, 50, 80), "town": (150, 140, 130), "forest": (40, 90, 50),
		   "flowers": (90, 150, 60), "lake": (60, 120, 170), "field": (200, 180, 90), "snow": (240, 244, 248), "night": (20, 25, 40) }[kind]
	for y in range (hz, h): d.line ([(0, y), (w, y)], lerp (ground, tuple (int (v * 0.7) for v in ground), (y - hz) / max (1, h - hz)))
	if kind == "sea":
		d.polygon ([(0, h * 0.82), (w, h * 0.74), (w, h), (0, h)], (225, 205, 160))
		for k in range (12): y = r.uniform (hz, h * 0.75); d.line ([(r.uniform (0, w), y), (r.uniform (0, w), y)], (120, 180, 210), max (1, h // 160))
	if kind == "town":
		x = 0
		while x < w:
			bw, bh = r.uniform (w * 0.06, w * 0.12), r.uniform (h * 0.15, h * 0.35)
			col = r.choice ([(190, 120, 90), (210, 190, 150), (170, 90, 70), (230, 210, 180), (140, 110, 90)])
			d.rectangle ([x, hz - bh, x + bw, hz + h * 0.05], col)
			d.polygon ([(x, hz - bh), (x + bw / 2, hz - bh - bw * 0.6), (x + bw, hz - bh)], (110, 60, 50))
			for wy in range (int (hz - bh + bw * 0.2), int (hz), max (4, int (bh / 4))):
				d.rectangle ([x + bw * 0.25, wy, x + bw * 0.4, wy + bw * 0.15], (70, 80, 100)); d.rectangle ([x + bw * 0.6, wy, x + bw * 0.75, wy + bw * 0.15], (70, 80, 100))
			x += bw + r.uniform (0, w * 0.01)
		d.rectangle ([0, hz + h * 0.05, w, h * 0.72], (80, 120, 150))
	if kind in ("forest", "mountain", "lake"):
		for k in range (40 if kind == "forest" else 14):
			tx, ty = r.uniform (0, w), r.uniform (hz - h * 0.02, h * 0.95 if kind == "forest" else hz + h * 0.08); s = r.uniform (h * 0.06, h * 0.14) * (1.4 if kind == "forest" else 1)
			d.polygon ([(tx, ty - s * 2), (tx - s * 0.6, ty), (tx + s * 0.6, ty)], (30 + r.randint (0, 30), 80 + r.randint (0, 40), 40))
	if kind == "lake": d.rectangle ([0, hz + h * 0.06, w, h], (70, 125, 175))
	if kind == "flowers":
		for k in range (160):
			fx, fy = r.uniform (0, w), r.uniform (hz, h); s = max (2, (fy - hz) / (h - hz) * h * 0.03)
			d.ellipse ([fx - s, fy - s, fx + s, fy + s], r.choice ([(230, 60, 80), (250, 200, 60), (240, 240, 240), (200, 90, 200)]))
	if kind == "field":
		for k in range (24): y = hz + (k / 24) ** 1.6 * (h - hz); d.line ([(0, y), (w, y)], (180, 160, 70), max (1, h // 200))
	if kind == "night":
		x = 0
		while x < w:
			bw, bh = r.uniform (w * 0.05, w * 0.1), r.uniform (h * 0.1, h * 0.3)
			d.rectangle ([x, hz - bh, x + bw, h], (25, 28, 40))
			for k in range (int (bh / 8)):
				if r.random () < 0.5: wx, wy = x + r.uniform (bw * 0.1, bw * 0.8), hz - bh + r.uniform (0, bh); d.rectangle ([wx, wy, wx + bw * 0.1, wy + bw * 0.08], (250, 210, 120))
			x += bw + 2
	img = img.filter (ImageFilter.GaussianBlur (max (0.6, w / 900)))
	if adjust:
		from PIL import ImageEnhance
		b, ct, s, warm = adjust
		img = ImageEnhance.Brightness (img).enhance (b); img = ImageEnhance.Contrast (img).enhance (ct); img = ImageEnhance.Color (img).enhance (s)
		if warm: rr, gg, bb = img.split (); rr = rr.point (lambda v: min (255, v + warm)); bb = bb.point (lambda v: max (0, v - warm)); img = Image.merge ("RGB", (rr, gg, bb))
	return img

def paste_photo (c, x, y, w, h, seed, kind = None, r = 6, adjust = None, crop = None):
	pw, ph = int (w * K), int (h * K)
	im = photo (seed, pw, ph, kind, adjust)
	mask = Image.new ("L", (pw, ph), 0); ImageDraw.Draw (mask).rounded_rectangle ([0, 0, pw - 1, ph - 1], int (r * K), fill = 255)
	c.img.paste (im, (int (x * K), int (y * K)), mask)

# ---- the icons --------------------------------------------------------------------------------------------------------
def ic_heart (c, x, y, s, col, fill = True):
	pts = []
	for k in range (40):
		t = k / 40 * 2 * math.pi
		px = 16 * math.sin (t) ** 3; py = -(13 * math.cos (t) - 5 * math.cos (2 * t) - 2 * math.cos (3 * t) - math.cos (4 * t))
		pts.append ((x + s / 2 + px / 34 * s, y + s * 0.45 + py / 34 * s))
	if fill: c.poly (pts, col)
	else: c.line (pts + [pts[0]], col, 1.6)
def ic_photos (c, x, y, s, col):
	c.rect (x + 1, y + 3, s - 2, s - 6, None, r = 2, outline = col, width = 1.6)
	c.poly ([(x + 3, y + s - 5), (x + s * 0.38, y + s * 0.48), (x + s * 0.58, y + s * 0.7), (x + s * 0.72, y + s * 0.56), (x + s - 3, y + s - 5)], col)
	c.ellipse (x + s * 0.7, y + s * 0.32, s * 0.08, col)
def ic_clock (c, x, y, s, col): c.ellipse (x + s / 2, y + s / 2, s * 0.42, None, col, 1.6); c.line ([(x + s / 2, y + s * 0.25), (x + s / 2, y + s / 2), (x + s * 0.7, y + s * 0.62)], col, 1.6)
def ic_album (c, x, y, s, col):
	c.rect (x + 4, y + 1, s - 6, s - 6, None, r = 2, outline = col, width = 1.4); c.rect (x + 1, y + 4, s - 6, s - 6, M.A (col, 60), r = 2, outline = col, width = 1.4)
def ic_folder (c, x, y, s, col): c.rect (x + 1, y + s * 0.3, s - 2, s * 0.58, None, r = 2, outline = col, width = 1.5); c.rect (x + 1, y + s * 0.18, s * 0.4, s * 0.18, col, r = 1)
def ic_import (c, x, y, s, col):
	c.line ([(x + s / 2, y + 2), (x + s / 2, y + s * 0.62)], col, 2); c.poly ([(x + s * 0.28, y + s * 0.45), (x + s * 0.72, y + s * 0.45), (x + s / 2, y + s * 0.72)], col)
	c.line ([(x + 2, y + s * 0.7), (x + 2, y + s - 2), (x + s - 2, y + s - 2), (x + s - 2, y + s * 0.7)], col, 1.8)
def ic_play (c, x, y, s, col): c.poly ([(x + s * 0.25, y + s * 0.15), (x + s * 0.85, y + s * 0.5), (x + s * 0.25, y + s * 0.85)], col)
def ic_search (c, x, y, s, col): c.ellipse (x + s * 0.42, y + s * 0.42, s * 0.3, None, col, 1.8); c.line ([(x + s * 0.64, y + s * 0.64), (x + s * 0.92, y + s * 0.92)], col, 2)
def ic_info (c, x, y, s, col): c.ellipse (x + s / 2, y + s / 2, s * 0.44, None, col, 1.6); c.rect (x + s * 0.45, y + s * 0.42, s * 0.1, s * 0.34, col); c.ellipse (x + s / 2, y + s * 0.28, s * 0.06, col)
def ic_rotate (c, x, y, s, col):
	c.d.arc ([(x + 3) * K, (y + 3) * K, (x + s - 3) * K, (y + s - 3) * K], 120, 400, fill = col, width = int (1.8 * K))
	c.poly ([(x + 2, y + s * 0.3), (x + s * 0.32, y + s * 0.22), (x + s * 0.12, y + s * 0.5)], col)
def ic_edit (c, x, y, s, col):
	for k, (yy, kx) in enumerate (((0.28, 0.35), (0.5, 0.65), (0.72, 0.45))):
		c.line ([(x + 2, y + s * yy), (x + s - 2, y + s * yy)], col, 1.5); c.ellipse (x + s * kx, y + s * yy, s * 0.11, col)
def ic_share (c, x, y, s, col):
	c.line ([(x + s / 2, y + 2), (x + s / 2, y + s * 0.6)], col, 1.8); c.line ([(x + s * 0.3, y + s * 0.22), (x + s / 2, y + 2), (x + s * 0.7, y + s * 0.22)], col, 1.8)
	c.line ([(x + s * 0.25, y + s * 0.42), (x + 3, y + s * 0.42), (x + 3, y + s - 2), (x + s - 3, y + s - 2), (x + s - 3, y + s * 0.42), (x + s * 0.75, y + s * 0.42)], col, 1.6)
def ic_trash (c, x, y, s, col):
	c.rect (x + s * 0.22, y + s * 0.28, s * 0.56, s * 0.66, None, r = 2, outline = col, width = 1.5)
	c.rect (x + s * 0.12, y + s * 0.17, s * 0.76, 2, col); c.rect (x + s * 0.38, y + s * 0.06, s * 0.24, 2, col)
def ic_back (c, x, y, s, col): c.line ([(x + s * 0.62, y + s * 0.2), (x + s * 0.3, y + s * 0.5), (x + s * 0.62, y + s * 0.8)], col, 2.2)
def ic_next (c, x, y, s, col): c.line ([(x + s * 0.38, y + s * 0.2), (x + s * 0.7, y + s * 0.5), (x + s * 0.38, y + s * 0.8)], col, 2.2)
def ic_crop (c, x, y, s, col):
	c.line ([(x + s * 0.28, y + 2), (x + s * 0.28, y + s * 0.72), (x + s - 2, y + s * 0.72)], col, 1.8); c.line ([(x + 2, y + s * 0.28), (x + s * 0.72, y + s * 0.28), (x + s * 0.72, y + s - 2)], col, 1.8)
def ic_sun (c, x, y, s, col):
	c.ellipse (x + s / 2, y + s / 2, s * 0.2, col)
	for k in range (8): a = k * math.pi / 4; c.line ([(x + s / 2 + math.cos (a) * s * 0.3, y + s / 2 + math.sin (a) * s * 0.3), (x + s / 2 + math.cos (a) * s * 0.45, y + s / 2 + math.sin (a) * s * 0.45)], col, 1.5)
def ic_drop (c, x, y, s, col): c.poly ([(x + s / 2, y + 2), (x + s * 0.8, y + s * 0.55), (x + s / 2, y + s - 2), (x + s * 0.2, y + s * 0.55)], col)
def ic_magic (c, x, y, s, col):
	c.line ([(x + 3, y + s - 3), (x + s * 0.68, y + s * 0.32)], col, 2.2)
	for (a, b) in ((0.75, 0.15), (0.9, 0.4), (0.55, 0.08)): c.ellipse (x + s * a, y + s * b, s * 0.06, col)
def ic_plus (c, x, y, s, col): c.rect (x + s * 0.2, y + s * 0.45, s * 0.6, s * 0.1, col); c.rect (x + s * 0.45, y + s * 0.2, s * 0.1, s * 0.6, col)
def check_mark (c, x, y, on = True):
	if on: c.ellipse (x + 10, y + 10, 10, M.SEL); c.ellipse (x + 10, y + 10, 10, None, WHITE, 1.5); c.line ([(x + 5, y + 10), (x + 9, y + 14), (x + 15, y + 6)], WHITE, 2)
	else: c.ellipse (x + 10, y + 10, 9, None, M.DIM, 1.5)

# ---- the window's parts -----------------------------------------------------------------------------------------------
WX, WY, WW, WH = 14, 32, 996, 640
TB_H, SIDE_W = 50, 200

def tbtn (c, x, y, icon, label = None, accent = False, col = None, hot = False):
	col = col or TEXT
	w = 34 if not label else c.tw (label) + 44
	if accent: M.button (c, x, y, w, 34, "", accent = True)
	elif hot: c.rect (x, y, w, 34, M.A (WHITE, 110), r = 6)
	icon (c, x + 9, y + 8, 18, WHITE if accent else col)
	if label: c.text_l (x + 34, y, 34, label, "uib" if accent else "ui", WHITE if accent else col)
	return x + w + 4

def toolbar (c, x, y, w, sel_count = 0):
	c.rect (x, y, w, TB_H, FACE)
	bx = x + 10
	bx = tbtn (c, bx, y + 8, ic_import, "Import", accent = True) + 8
	bx = tbtn (c, bx, y + 8, ic_play, "Slideshow")
	c.vline (bx + 3, y + 12, y + 38, M.shade (FACE, 0.82)); bx += 12
	if sel_count:
		c.text_l (bx, y, TB_H, "%d selected" % sel_count, "uib", M.SEL); bx += c.tw ("%d selected" % sel_count, "uib") + 12
		for ic in (lambda c, a, b, s, k: ic_heart (c, a, b, s, k, False), ic_album, ic_share, ic_trash): bx = tbtn (c, bx, y + 8, ic)
	# the thumbnails' size, the search
	sx = x + w - 10 - 230
	c.rect (sx, y + 11, 230, 28, WHITE, r = 14, outline = M.LINE)
	ic_search (c, sx + 10, y + 17, 16, DIM); c.text_l (sx + 34, y + 11, 28, "Search: a place, a date...", "ui", FAINT)
	zx = sx - 150
	ic_photos (c, zx, y + 18, 13, DIM); c.rect (zx + 20, y + 24, 90, 3, M.LINE2, r = 1); c.rect (zx + 20, y + 24, 52, 3, M.SEL, r = 1)
	c.ellipse (zx + 72, y + 25, 7, WHITE, M.shade (FACE, 0.6), 1.2); ic_photos (c, zx + 118, y + 15, 19, DIM)
	c.hline (x, x + w, y + TB_H, M.shade (FACE, 0.84))

def sidebar (c, x, y, h, sel = "All photos"):
	c.rect (x, y, SIDE_W, h, SIDE); c.vline (x + SIDE_W, y, y + h, M.shade (FACE, 0.82))
	yy = y + 12
	def head (t):
		nonlocal yy
		c.text (x + 18, yy + 6, t, "tiny", DIM); yy += 22
	def item (label, icon, count = None, colour = TEXT):
		nonlocal yy
		on = label == sel
		if on: c.rect (x + 8, yy, SIDE_W - 16, 26, M.SEL, r = 6)
		icon (c, x + 18, yy + 5, 16, WHITE if on else colour)
		c.text_l (x + 44, yy, 26, label, "uib" if on else "ui", WHITE if on else TEXT)
		if count is not None: c.text_r (x + SIDE_W - 18, yy, 26, str (count), "small", WHITE if on else DIM)
		yy += 28
	head ("LIBRARY")
	item ("All photos", ic_photos, 2814); item ("Favourites", ic_heart, 126, RED); item ("Recently added", ic_clock, 48)
	yy += 6; head ("ALBUMS")
	for nm, n in (("Ghent, September", 64), ("The Ardennes", 38), ("The kids", 412), ("Brewery labels", 22)): item (nm, ic_album, n, DIM)
	c.text_l (x + 44, yy, 26, "New album...", "ui", M.SEL); ic_plus (c, x + 18, yy + 5, 16, M.SEL); yy += 34
	head ("FOLDERS")
	for nm, n in (("Pictures", 1960), ("Camera (import)", 812), ("SD1: Photos 2019", 42)): item (nm, ic_folder, n, (210, 165, 80))

# ---- 1. the library -------------------------------------------------------------------------------------------------------
def shot_library ():
	c = screen ()
	cx, cy, cw, ch = M.window (c, WX, WY, WW, WH, "Photos — All photos")
	toolbar (c, cx, cy, cw, sel_count = 3)
	sidebar (c, cx, cy + TB_H, ch - TB_H)
	gx, gy, gw = cx + SIDE_W + 1, cy + TB_H + 1, cw - SIDE_W - 1
	c.rect (gx, gy, gw, ch - TB_H - 1, LIST)
	y = gy + 12
	groups = [("Saturday 27 September 2026", "Ghent · 18 photos", 12, 1), ("Sunday 21 September 2026", "La Roche-en-Ardenne · 9 photos", 6, 40), ("Monday 15 September 2026", "Brussels · 4 photos", 4, 80)]
	sel = { 2, 4, 9 }; fav = { 1, 5, 41 }
	tile = 106; gap = 6
	cols = (gw - 32 - 44) // (tile + gap)
	for gi, (day, where, n, base) in enumerate (groups):
		c.text (gx + 18, y, day, "h2"); c.text (gx + 18 + c.tw (day, "h2") + 12, y + 3, where, "ui", DIM)
		check_mark (c, gx + 18 + cols * (tile + gap) - gap - 22, y + 2, False)
		y += 30
		for k in range (n):
			tx = gx + 18 + (k % cols) * (tile + gap); ty = y + (k // cols) * (tile + gap)
			if ty + tile > gy + ch - TB_H - 34: break
			idx = base + k
			paste_photo (c, tx, ty, tile, tile, idx * 7 + 3, r = 4)
			if idx in sel:
				c.rect (tx, ty, tile, tile, M.A (M.SEL, 70), r = 4); c.rect (tx, ty, tile, tile, None, r = 4, outline = M.SEL, width = 3)
				check_mark (c, tx + 6, ty + 6, True)
			if idx in fav: ic_heart (c, tx + tile - 24, ty + tile - 22, 16, WHITE)
			if idx == 5:
				c.rect (tx + tile - 44, ty + 6, 38, 18, M.A ((0, 0, 0), 150), r = 9); c.text_c (tx + tile - 44, ty + 6, 38, 18, "0:42", "smallb", WHITE)
		y += ((min (n, 99) + cols - 1) // cols) * (tile + gap) + 14
	# the year's bar at the right (the timeline)
	bx = gx + gw - 26
	c.rect (bx, gy + 10, 3, ch - TB_H - 60, M.LINE2, r = 1)
	for i, yr in enumerate (("2026", "2025", "2024", "2023", "2021", "2019")):
		c.text_r (bx - 6, gy + 10 + i * 70, 16, yr, "small", DIM if i else M.SEL)
	c.rect (bx - 2, gy + 14, 7, 30, M.SEL, r = 3)
	# the status line
	c.rect (gx, cy + ch - 26, gw, 26, M.HEAD); c.text_l (gx + 14, cy + ch - 26, 26, "2 814 photos and 37 videos · 3 selected (11,2 MB)", "small", DIM)
	c.save ("photos-library.png")

# ---- 2. a photo, its film strip, its details ------------------------------------------------------------------------------
def shot_viewer ():
	c = screen ()
	cx, cy, cw, ch = M.window (c, WX, WY, WW, WH, "Photos — IMG_2041.jpg")
	c.rect (cx, cy, cw, ch, DARK)
	# the bar: back, the name, the actions
	c.rect (cx, cy, cw, 46, DARK2)
	bx = tbtn (c, cx + 8, cy + 6, ic_back, "All photos", col = (230, 230, 232))
	c.text_c (cx, cy, cw - 300, 46, "Saturday 27 September 2026, 16:42  ·  14 of 18", "ui", (200, 202, 206))
	ax = cx + cw - 8 - 7 * 38
	for ic, on in ((lambda c, a, b, s, k: ic_heart (c, a, b, s, RED, True), False), (ic_rotate, False), (ic_edit, False), (ic_album, False), (ic_share, False), (ic_trash, False), (ic_info, True)):
		if on: c.rect (ax, cy + 6, 34, 34, M.A (WHITE, 40), r = 6)
		ic (c, ax + 8, cy + 14, 18, (230, 230, 232)); ax += 38
	# the photo
	iw = cw - 280
	pw, ph = iw - 80, ch - 46 - 110
	paste_photo (c, cx + 40, cy + 66, pw, ph, 77, "town", r = 2)
	for side, ic in ((0, ic_back), (1, ic_next)):
		x = cx + 10 if not side else cx + iw - 44
		c.ellipse (x + 17, cy + 66 + ph / 2, 17, M.A ((0, 0, 0), 120)); ic (c, x + 6, cy + 66 + ph / 2 - 11, 22, WHITE)
	# the film strip
	fy = cy + ch - 70
	for k in range (11):
		fx = cx + 30 + k * 64
		if fx + 58 > cx + iw - 20: break
		paste_photo (c, fx, fy, 58, 46, 1000 + k * 13 + (77 if k == 5 else 0), "town" if k == 5 else None, r = 3)
		if k == 5: c.rect (fx - 2, fy - 2, 62, 50, None, r = 4, outline = WHITE, width = 2)
		else: c.rect (fx, fy, 58, 46, M.A ((0, 0, 0), 70), r = 3)
	# the details
	px = cx + iw
	c.rect (px, cy + 46, cw - iw, ch - 46, DARK2)
	y = cy + 62
	c.text (px + 18, y, "Details", "h2", WHITE); y += 34
	rows = [("IMG_2041.jpg", "4032 × 3024 · 3,4 MB", ic_photos), ("Saturday 27 September 2026", "16:42", ic_clock),
		("Pixel 8", "f/1.7 · 1/640 s · ISO 50 · 6,9 mm", ic_info), ("Ghent, Belgium", "Graslei · 51.0547° N, 3.7206° E", ic_folder),
		("Pictures / 2026 / Ghent", "SD:/Pictures/2026/Ghent", ic_folder)]
	for a, b, ic in rows:
		ic (c, px + 18, y + 2, 18, (170, 174, 180))
		c.text (px + 48, y, a, "uib", (235, 235, 238)); c.text (px + 48, y + 18, b, "small", (160, 164, 170)); y += 46
	c.text (px + 18, y + 4, "ALBUMS", "tiny", (150, 154, 160)); y += 22
	for nm in ("Ghent, September", "Favourites"):
		w = c.tw (nm, "small") + 22; c.rect (px + 18, y, w, 22, (70, 74, 82), r = 11); c.text_c (px + 18, y, w, 22, nm, "small", (230, 230, 232)); y += 28
	y += 8
	c.text (px + 18, y, "DESCRIPTION", "tiny", (150, 154, 160)); y += 20
	c.rect (px + 18, y, cw - iw - 36, 60, (52, 56, 63), r = 6, outline = (80, 84, 92))
	c.text (px + 28, y + 9, "The Graslei from the Korenlei bridge,", "small", (220, 220, 224)); c.text (px + 28, y + 26, "late afternoon.", "small", (220, 220, 224))
	# the zoom
	c.rect (cx + iw / 2 - 70, fy - 40, 140, 28, M.A ((0, 0, 0), 150), r = 14)
	c.text_c (cx + iw / 2 - 70, fy - 40, 140, 28, "−    Fit    +", "uib", WHITE)
	c.save ("photos-viewer.png")

# ---- 3. editing -------------------------------------------------------------------------------------------------------------
def slider (c, x, y, w, label, val, lo = -100, hi = 100, light = True):
	fg, dim = ((235, 235, 238), (160, 164, 170))
	c.text (x, y, label, "ui", fg); c.text_r (x + w, y + 7, 0, ("%+d" % val) if lo < 0 else str (val), "small", dim)
	ty = y + 24
	c.rect (x, ty, w, 4, (80, 84, 92), r = 2)
	t = (val - lo) / (hi - lo)
	mid = x + w * ((0 - lo) / (hi - lo)) if lo < 0 else x
	c.rect (min (mid, x + w * t), ty, abs (x + w * t - mid), 4, M.SEL, r = 2)
	c.ellipse (x + w * t, ty + 2, 8, WHITE, M.SEL, 1.5)

def shot_edit ():
	c = screen ()
	cx, cy, cw, ch = M.window (c, WX, WY, WW, WH, "Photos — Editing IMG_1988.jpg")
	c.rect (cx, cy, cw, ch, DARK)
	c.rect (cx, cy, cw, 46, DARK2)
	tbtn (c, cx + 8, cy + 6, ic_back, "Cancel", col = (230, 230, 232))
	c.text_c (cx, cy, cw - 260, 46, "IMG_1988.jpg", "uib", (220, 222, 226))
	bx = cx + cw - 8 - 300
	c.rect (bx, cy + 7, 120, 32, (70, 74, 82), r = 6); c.text_c (bx, cy + 7, 120, 32, "Before / after", "ui", (230, 230, 232))
	M.button (c, bx + 128, cy + 7, 172, 32, "Save a copy  ▾", accent = True)
	# the photo, cropped, a grid of thirds
	pw, ph = cw - 330, ch - 46 - 60
	ix, iy = cx + 30, cy + 76
	paste_photo (c, ix, iy, pw, ph, 55, "lake", r = 0, adjust = (1.08, 1.12, 1.25, 8))
	kx0, ky0, kx1, ky1 = ix + pw * 0.08, iy + ph * 0.1, ix + pw * 0.94, iy + ph * 0.86
	for (a, b, w_, h_) in ((ix, iy, pw, ky0 - iy), (ix, ky1, pw, iy + ph - ky1), (ix, ky0, kx0 - ix, ky1 - ky0), (kx1, ky0, ix + pw - kx1, ky1 - ky0)):
		c.rect (a, b, w_, h_, M.A ((0, 0, 0), 140))
	c.rect (kx0, ky0, kx1 - kx0, ky1 - ky0, None, outline = WHITE, width = 2)
	for t in (1 / 3, 2 / 3):
		c.vline (kx0 + (kx1 - kx0) * t, ky0, ky1, M.A (WHITE, 120)); c.hline (kx0, kx1, ky0 + (ky1 - ky0) * t, M.A (WHITE, 120))
	for (hx, hy) in ((kx0, ky0), (kx1, ky0), (kx0, ky1), (kx1, ky1)):
		c.rect (hx - 5, hy - 5, 10, 10, WHITE, r = 2)
	# the panel
	px = cx + cw - 290
	c.rect (px, cy + 46, 290, ch - 46, DARK2)
	tabs = [("Crop", ic_crop), ("Adjust", ic_sun), ("Filters", ic_magic)]
	tx = px + 14
	for i, (t, ic) in enumerate (tabs):
		w = 84
		if i == 1: c.rect (tx, cy + 58, w, 54, (70, 74, 82), r = 8)
		ic (c, tx + w / 2 - 10, cy + 64, 20, WHITE if i == 1 else (170, 174, 180))
		c.text_c (tx, cy + 88, w, 20, t, "smallb" if i == 1 else "small", WHITE if i == 1 else (170, 174, 180)); tx += w + 6
	y = cy + 126
	c.rect (px + 14, y, 262, 32, (60, 64, 72), r = 6); ic_magic (c, px + 24, y + 7, 18, AMBER); c.text_l (px + 50, y, 32, "Enhance (automatic)", "uib", (235, 235, 238)); y += 46
	c.text (px + 16, y, "LIGHT", "tiny", (150, 154, 160)); y += 18
	for lab, v in (("Exposure", 8), ("Contrast", 12), ("Highlights", -20), ("Shadows", 25)): slider (c, px + 16, y, 256, lab, v); y += 44
	c.text (px + 16, y + 2, "COLOUR", "tiny", (150, 154, 160)); y += 20
	for lab, v in (("Saturation", 25), ("Warmth", 10)): slider (c, px + 16, y, 256, lab, v); y += 44
	slider (c, px + 16, y, 256, "Sharpness", 30, 0, 100); y += 50
	c.text (px + 16, y, "Crop: 3:2  ·  Straighten  −1,5°", "small", (190, 194, 200))
	# the crop's ratios under the photo
	rx = ix
	for i, r in enumerate (("Free", "Original", "1:1", "4:3", "3:2", "16:9")):
		w = c.tw (r, "small") + 22
		c.rect (rx, cy + ch - 44, w, 26, M.SEL if r == "3:2" else (60, 64, 72), r = 13); c.text_c (rx, cy + ch - 44, w, 26, r, "smallb" if r == "3:2" else "small", WHITE); rx += w + 6
	ic_rotate (c, rx + 14, cy + ch - 41, 20, (220, 220, 224))
	c.save ("photos-edit.png")

# ---- 4. the albums; a photo sent somewhere ------------------------------------------------------------------------------------
def shot_albums ():
	c = screen ()
	cx, cy, cw, ch = M.window (c, WX, WY, WW, WH, "Photos — Albums")
	toolbar (c, cx, cy, cw)
	sidebar (c, cx, cy + TB_H, ch - TB_H, sel = None)
	gx, gy, gw = cx + SIDE_W + 1, cy + TB_H + 1, cw - SIDE_W - 1
	c.rect (gx, gy, gw, ch - TB_H - 1, LIST)
	c.text (gx + 20, gy + 14, "Albums", "huge")
	M.button (c, gx + gw - 150, gy + 14, 130, 30, "+ New album")
	albums = [("Ghent, September", 64, 11, "town"), ("The Ardennes", 38, 22, "forest"), ("The kids", 412, 33, "field"), ("Brewery labels", 22, 44, "sunset"),
		  ("Summer at the sea", 151, 55, "sea"), ("Snow, January", 47, 66, "snow"), ("Garden", 89, 77, "flowers"), ("Night walks", 12, 88, "night")]
	tw_, th_ = 168, 128
	for i, (nm, n, seed, kind) in enumerate (albums):
		ax = gx + 20 + (i % 4) * (tw_ + 16); ay = gy + 64 + (i // 4) * (th_ + 56)
		# a stack: two cards behind
		c.rect (ax + 8, ay - 8, tw_ - 16, th_, (205, 198, 192), r = 8); c.rect (ax + 4, ay - 4, tw_ - 8, th_, (222, 216, 210), r = 8)
		paste_photo (c, ax, ay, tw_, th_, seed, kind, r = 8)
		c.text (ax + 2, ay + th_ + 8, nm, "uib"); c.text (ax + 2, ay + th_ + 25, "%d photos" % n, "small", DIM)
	# a right click on "Garden": what it can be sent to
	mx, my = gx + 20 + 2 * (tw_ + 16) + 90, gy + 64 + (th_ + 56) + 60
	M.menu_popup (c, mx, my, 230, [("Open",), ("Slideshow", "F5"), None, ("Send by Mail...",), ("Set as the wallpaper",), ("Export as a PDF...",), None, ("Rename...", "F2"), ("Delete the album...", "Del")], hot = "Send by Mail...")
	c.save ("photos-albums.png")

if __name__ == "__main__":
	shot_library (); shot_viewer (); shot_edit (); shot_albums ()
