#!/usr/bin/env python3
"""mockup_paint.py -- the mock-ups of Paint made "pro": the ribbon in FreeType's text with the new tools (free-form
selection, magic wand, text, gradient, brushes), a bar of the tool's options under it, a dark neutral desk around the
picture, the layers' panel with each layer's blend mode (normal, multiply, screen, add, subtract, lighten, mask, cut
out) and opacity, the status bar with its zoom slider. See docs/paint/README.md.

    python3 tools/screenshot/mockup_paint.py  -> docs/paint/mockups/paint-*.png

On the real desktop (screenshots/desktop.png); the drawing helpers are mockup_archiver.py's. MIT licence (Onyx).
"""
import os, sys, math, random
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, HERE)
import mockup_archiver as M

M.W, M.H = 1280, 800
M.OUT = os.path.join (M.ROOT, "docs", "paint", "mockups")
K = M.K
DESK = Image.open (os.path.join (M.ROOT, "screenshots", "desktop.png")).convert ("RGB")
TEXT, DIM, FACE, SEL, LINE, LINE2, FAINT, WHITE = M.TEXT, M.DIM, M.FACE, M.SEL, M.LINE, M.LINE2, M.FAINT, (255, 255, 255)
SOFT = M.SEL_SOFT
RIB = (244, 239, 235)			# the ribbon: a light tone of the face
OPT = (232, 224, 218)			# the options' bar
PANEL = (240, 234, 229)			# the layers' panel
DESKC = (72, 74, 80)			# the desk around the picture: dark, neutral (the colours read true)
INK = (52, 48, 46)
M.F["h2"] = M._f ("DejaVuSans-Bold.ttf", 15)
M.F["lab"] = M._f ("DejaVuSans.ttf", 11)
M.F["tiny2"] = M._f ("DejaVuSans.ttf", 10)
M.F["big2"] = M._f ("DejaVuSans-Bold.ttf", 34)
M.F["serif"] = M._f ("DejaVuSerif-Bold.ttf", 30)

def screen (menus = ("File", "Edit", "Image", "Layers", "Adjust", "View", "Help")):
	c = M.Canvas ()
	c.img.paste (DESK.resize ((M.W * K, M.H * K), Image.LANCZOS), (0, 0)); c.d = ImageDraw.Draw (c.img, "RGBA")
	c.rect (0, 0, M.W, 27, (230, 222, 217)); c.hline (0, M.W, 27, LINE)
	x = 18; c.text_l (x, 0, 27, "Onyx", "menu"); x += c.tw ("Onyx", "menu") + 18
	c.text_l (x, 0, 27, "Paint", "menub"); x += c.tw ("Paint", "menub") + 20
	pos = {}
	for m in menus: pos[m] = x; c.text_l (x, 0, 27, m, "menu"); x += c.tw (m, "menu") + 18
	c.text_r (M.W - 14, 0, 27, "14:32", "menu")
	return c, pos

# ---- the picture (its layers, drawn at K x the size) --------------------------------------------------------------------
PW, PH = 640, 420
def _layer (): return Image.new ("RGBA", (PW * K, PH * K), (0, 0, 0, 0))
def art_background ():
	im = Image.new ("RGBA", (PW * K, PH * K)); d = ImageDraw.Draw (im)
	for y in range (PH * K):					# the sky at sunset
		t = y / (PH * K * 0.62)
		top, mid, low = (52, 60, 120), (220, 110, 120), (252, 196, 120)
		col = M.mix (top, mid, min (1, t * 1.5)) if t < 0.66 else M.mix (mid, low, min (1, (t - 0.66) * 3))
		d.line ([0, y, PW * K, y], fill = col + (255,))
	return im
def art_drawing ():
	im = _layer (); d = ImageDraw.Draw (im)
	S = lambda pts: [(x * K, y * K) for x, y in pts]
	d.ellipse ([(420 - 46) * K, (232 - 46) * K, (420 + 46) * K, (232 + 46) * K], fill = (255, 226, 150, 255))	# the sun
	d.polygon (S ([(0, 300), (90, 190), (160, 250), (250, 150), (350, 260), (440, 200), (540, 270), (640, 210), (640, 420), (0, 420)]), fill = (86, 70, 118, 255))
	d.polygon (S ([(250, 150), (282, 182), (262, 178), (244, 192), (228, 176)]), fill = (236, 226, 240, 255))	# snow
	d.polygon (S ([(0, 330), (120, 280), (230, 320), (360, 290), (500, 330), (640, 300), (640, 420), (0, 420)]), fill = (58, 48, 86, 255))
	d.rectangle ([0, 352 * K, PW * K, PH * K], fill = (40, 52, 90, 255))			# the lake
	for i in range (9):										# the sun's path on it
		w = 60 - i * 5; y = 358 + i * 7
		d.rectangle ([(420 - w) * K, y * K, (420 + w) * K, (y + 2) * K], fill = (255, 214, 150, 170 - i * 14))
	for x0, h in ((52, 88), (86, 120), (120, 70)):								# the firs
		d.polygon (S ([(x0, 352 - h), (x0 + 22, 352), (x0 - 22, 352)]), fill = (24, 30, 44, 255))
		d.rectangle ([(x0 - 3) * K, 352 * K, (x0 + 3) * K, 360 * K], fill = (24, 30, 44, 255))
	return im
def art_glow ():									# "Light" (Add): the sun's halo
	im = _layer (); d = ImageDraw.Draw (im)
	for r in range (150, 40, -6):
		a = int (60 * (1 - (r - 40) / 110) ** 2)
		d.ellipse ([(420 - r) * K, (232 - r) * K, (420 + r) * K, (232 + r) * K], fill = (255, 150, 60, a))
	return im
def art_tint ():									# "Warm tint" (Multiply 60 %)
	im = _layer (); d = ImageDraw.Draw (im)
	for y in range (PH * K): d.line ([0, y, PW * K, y], fill = M.mix ((255, 236, 210), (255, 200, 170), y / (PH * K)) + (255,))
	return im
def art_mask ():									# "Vignette" (Mask): a white rounded shape
	im = _layer (); d = ImageDraw.Draw (im)
	d.rounded_rectangle ([24 * K, 22 * K, (PW - 24) * K, (PH - 22) * K], 70 * K, fill = (255, 255, 255, 255))
	return im.filter (ImageFilter.GaussianBlur (9 * K))
def checker (w, h, s = 8):
	im = Image.new ("RGB", (w, h), (255, 255, 255)); d = ImageDraw.Draw (im)
	for y in range (0, h, s):
		for x in range ((y // s) % 2 * s, w, 2 * s): d.rectangle ([x, y, x + s - 1, y + s - 1], fill = (214, 214, 214))
	return im
def blend (base, layer, mode, op = 1.0):
	"""base RGBA, layer RGBA (straight), premultiplied maths as gpucomp's"""
	import numpy as np
	b = np.asarray (base, dtype = np.float64) / 255; s = np.asarray (layer, dtype = np.float64) / 255
	sa = s[..., 3:4] * op; S = s[..., :3] * sa; da = b[..., 3:4]; D = b[..., :3] * da
	if mode == "mask": C = D * s[..., 3:4]; A = da * s[..., 3:4]
	else:
		if mode == "multiply": C = S * D + S * (1 - da) + D * (1 - sa)
		elif mode == "screen": C = S + D - S * D
		elif mode == "add": C = np.minimum (1, S + D)
		else: C = S + D * (1 - sa)
		A = sa + da * (1 - sa)
	out = np.concatenate ([np.where (A > 0, C / np.maximum (A, 1e-9), 0), A], axis = -1)
	return Image.fromarray ((np.clip (out, 0, 1) * 255 + 0.5).astype ("uint8"), "RGBA")
LAYERS = None
def picture (with_mask = True, with_text = False, extra = None):
	global LAYERS
	if LAYERS is None: LAYERS = dict (bg = art_background (), draw = art_drawing (), glow = art_glow (), tint = art_tint (), mask = art_mask ())
	im = blend (LAYERS["bg"], LAYERS["draw"], "normal")
	if extra is not None: im = blend (im, extra, "normal")
	im = blend (im, LAYERS["tint"], "multiply", 0.6)
	im = blend (im, LAYERS["glow"], "add")
	if with_text:
		t = _layer (); d = ImageDraw.Draw (t)
		d.text ((PW / 2 * K, 92 * K), "Lac des Cimes", font = M.F["serif"], fill = (255, 246, 232, 255), anchor = "mm")
		im = blend (im, t, "normal")
	if with_mask: im = blend (im, LAYERS["mask"], "mask")
	return im
def put_picture (c, x, y, im, scale = 1.0):
	w, h = int (PW * scale * K), int (PH * scale * K)
	ck = checker (w, h, 8 * K)
	sh = Image.new ("RGBA", (w + 40 * K, h + 40 * K), (0, 0, 0, 0)); ImageDraw.Draw (sh).rectangle ([20 * K, 24 * K, w + 20 * K, h + 24 * K], fill = (0, 0, 0, 120))
	sh = sh.filter (ImageFilter.GaussianBlur (8 * K)); c.img.paste (sh, (int ((x - 20) * K), int ((y - 20) * K)), sh)
	pic = im.resize ((w, h), Image.LANCZOS)
	ck.paste (pic, (0, 0), pic); c.img.paste (ck, (int (x * K), int (y * K)))
	c.d = ImageDraw.Draw (c.img, "RGBA")
def thumb (c, x, y, w, h, im):
	t = checker (w * K, h * K, 4 * K); p = im.resize ((w * K, h * K), Image.LANCZOS); t.paste (p, (0, 0), p)
	c.img.paste (t, (int (x * K), int (y * K))); c.d = ImageDraw.Draw (c.img, "RGBA")
	c.rect (x - 1, y - 1, w + 2, h + 2, outline = (150, 140, 134))

# ---- the icons: 24 px, an outline in ink, a touch of colour ---------------------------------------------------------------
def icon (c, k, x, y, s = 24, ink = INK, dis = False):
	if dis: ink = FAINT
	L = lambda pts, w = 1.6, col = None: c.line ([(x + px * s / 24, y + py * s / 24) for px, py in pts], col or ink, w)
	P = lambda pts, col: c.poly ([(x + px * s / 24, y + py * s / 24) for px, py in pts], col)
	R = lambda px, py, w, h, col = None, r = 0, out = None, wd = 1.4: c.rect (x + px * s / 24, y + py * s / 24, w * s / 24, h * s / 24, col, r = r, outline = out, width = wd)
	E = lambda cx, cy, r, col = None, out = None, wd = 1.4: c.ellipse (x + cx * s / 24, y + cy * s / 24, r * s / 24, col, out, wd)
	if k == "paste":
		R (4, 4, 16, 18, (196, 150, 98), r = 2, out = ink); R (7, 8, 10, 12, WHITE, out = ink, wd = 1); R (8, 2, 8, 4, (120, 116, 112), r = 1.5)
		for i in range (3): R (9, 11 + i * 3, 6, 1, ink)
	elif k == "cut":
		E (7, 18, 3, None, ink, 1.6); E (17, 18, 3, None, ink, 1.6); L ([(9, 16), (16, 3)]); L ([(15, 16), (8, 3)])
	elif k == "copy":
		R (3, 3, 12, 14, WHITE, r = 1.5, out = ink); R (9, 8, 12, 14, WHITE, r = 1.5, out = ink)
	elif k == "undo":
		c.d.arc ([(x + 5 * s / 24) * K, (y + 7 * s / 24) * K, (x + 20 * s / 24) * K, (y + 20 * s / 24) * K], 180, 90, fill = ink, width = int (1.8 * K)); P ([(2, 13), (8, 8), (9, 15)], ink)
	elif k == "redo":
		c.d.arc ([(x + 4 * s / 24) * K, (y + 7 * s / 24) * K, (x + 19 * s / 24) * K, (y + 20 * s / 24) * K], 90, 0, fill = ink, width = int (1.8 * K)); P ([(22, 13), (16, 8), (15, 15)], ink)
	elif k == "select":
		for i in range (0, 18, 4): L ([(3 + i, 4), (min (21, 5 + i), 4)]); L ([(3 + i, 20), (min (21, 5 + i), 20)])
		for i in range (0, 16, 4): L ([(3, 4 + i), (3, min (20, 6 + i))]); L ([(21, 4 + i), (21, min (20, 6 + i))])
	elif k == "lasso":
		pts = [(12 + 9 * math.cos (a / 10), 9 + 6 * math.sin (a / 10)) for a in range (0, 63)]
		for i in range (0, len (pts) - 1, 2): L ([pts[i], pts[i + 1]])
		L ([(7, 14), (6, 19), (9, 22)])
	elif k == "wand":
		L ([(4, 21), (15, 10)], 2.4); P ([(16, 2), (17.5, 6), (22, 7), (18.5, 9.5), (19.5, 14), (16, 11.5), (12, 14), (13.5, 9.5), (10, 7), (14.5, 6)], (236, 180, 40))
	elif k == "crop":
		L ([(6, 2), (6, 18), (22, 18)], 2); L ([(2, 6), (18, 6), (18, 22)], 2)
	elif k == "resize":
		R (3, 9, 12, 12, None, out = ink); L ([(11, 13), (20, 4)]); L ([(14, 4), (20, 4), (20, 10)])
	elif k == "rotate":
		c.d.arc ([(x + 4 * s / 24) * K, (y + 4 * s / 24) * K, (x + 20 * s / 24) * K, (y + 20 * s / 24) * K], 200, 120, fill = ink, width = int (1.8 * K)); P ([(14, 22), (9, 18), (15, 15)], ink)
	elif k == "pencil":
		P ([(4, 20), (5, 15), (17, 3), (21, 7), (9, 19)], (246, 196, 70)); L ([(4, 20), (5, 15), (17, 3), (21, 7), (9, 19), (4, 20)], 1.3)
		P ([(4, 20), (5, 16.5), (7.5, 19)], ink); L ([(15, 5), (19, 9)], 1.3)
	elif k == "fill":
		P ([(4, 11), (11, 4), (19, 12), (12, 19)], (98, 150, 214)); L ([(4, 11), (11, 4), (19, 12), (12, 19), (4, 11)], 1.4)
		L ([(11, 4), (8, 1)]); P ([(20, 14), (22, 18), (20, 20), (18, 18)], (98, 150, 214))
	elif k == "text":
		c.text_c (x, y, s, s, "A", "h2", ink); L ([(5, 21), (19, 21)], 1.6, (73, 146, 167))
	elif k == "eraser":
		P ([(3, 15), (12, 6), (20, 14), (11, 23)], (238, 140, 160)); P ([(3, 15), (7, 11), (15, 19), (11, 23)], (250, 236, 240))
		L ([(3, 15), (12, 6), (20, 14), (11, 23), (3, 15)], 1.3); L ([(11, 23), (21, 23)], 1.3)
	elif k == "picker":
		L ([(4, 20), (14, 10)], 2.2); P ([(13, 7), (17, 3), (21, 7), (17, 11)], ink); E (6, 18, 2.2, (73, 146, 167))
	elif k == "zoom":
		E (10, 10, 6.5, (226, 238, 244), ink, 1.8); L ([(15, 15), (21, 21)], 2.6)
	elif k == "gradient":
		for i in range (16): R (4 + i, 5, 1.05, 14, M.mix ((60, 110, 200), (250, 200, 120), i / 15))
		R (4, 5, 16, 14, None, r = 1, out = ink)
	elif k == "brush":
		P ([(10, 13), (19, 3), (21, 5), (12, 15)], (150, 104, 70)); L ([(10, 13), (19, 3), (21, 5), (12, 15)], 1.2)
		P ([(10, 13), (12, 15), (9, 21), (3, 22), (5, 17)], (73, 146, 167))
	elif k == "outline": R (4, 6, 16, 12, None, r = 2, out = ink, wd = 2)
	elif k == "shapefill": R (4, 6, 16, 12, (73, 146, 167), r = 2, out = ink, wd = 1.2)
	elif k == "eye":
		c.d.ellipse ([(x + 3 * s / 24) * K, (y + 7 * s / 24) * K, (x + 21 * s / 24) * K, (y + 17 * s / 24) * K], outline = ink, width = int (1.5 * K)); E (12, 12, 3, ink)
	elif k == "eyeoff":
		c.d.ellipse ([(x + 3 * s / 24) * K, (y + 7 * s / 24) * K, (x + 21 * s / 24) * K, (y + 17 * s / 24) * K], outline = FAINT, width = int (1.5 * K)); L ([(4, 20), (20, 4)], 1.5, FAINT)
	elif k == "lock":
		R (6, 11, 12, 10, None, r = 2, out = ink); c.d.arc ([(x + 8 * s / 24) * K, (y + 4 * s / 24) * K, (x + 16 * s / 24) * K, (y + 15 * s / 24) * K], 180, 0, fill = ink, width = int (1.5 * K))
	elif k == "add": L ([(12, 5), (12, 19)], 2); L ([(5, 12), (19, 12)], 2)
	elif k == "dup": R (4, 4, 11, 11, WHITE, r = 1.5, out = ink); R (9, 9, 11, 11, WHITE, r = 1.5, out = ink)
	elif k == "trash":
		R (6, 8, 12, 13, None, r = 2, out = ink); L ([(4, 6), (20, 6)]); R (9, 3, 6, 3, None, out = ink, wd = 1.2); L ([(10, 11), (10, 18)], 1.2); L ([(14, 11), (14, 18)], 1.2)
	elif k == "up": L ([(12, 19), (12, 5)], 1.8); L ([(6, 11), (12, 5), (18, 11)], 1.8)
	elif k == "down": L ([(12, 5), (12, 19)], 1.8); L ([(6, 13), (12, 19), (18, 13)], 1.8)
	elif k == "merge": R (4, 16, 16, 5, ink, r = 1); L ([(12, 3), (12, 12)], 1.8, (73, 146, 167)); L ([(8, 9), (12, 13), (16, 9)], 1.8, (73, 146, 167))
	elif k == "fx": c.text_c (x, y, s, s, "fx", "smallb", ink)
	elif k == "grid":
		for i in range (4): L ([(3 + i * 6, 3), (3 + i * 6, 21)], 1.1); L ([(3, 3 + i * 6), (21, 3 + i * 6)], 1.1)
	elif k == "fit":
		R (6, 6, 12, 12, WHITE, out = ink, wd = 1.2)
		for (a, b, cc, d) in ((2, 6, 2, 2), (2, 2, 6, 2), (22, 6, 22, 2), (22, 2, 18, 2), (2, 18, 2, 22), (2, 22, 6, 22), (22, 18, 22, 22), (22, 22, 18, 22)): L ([(a, b), (cc, d)], 1.6)
	elif k == "pointer": P ([(6, 3), (6, 19), (10, 15), (13, 21), (15, 20), (12, 14), (18, 14)], WHITE); L ([(6, 3), (6, 19), (10, 15), (13, 21), (15, 20), (12, 14), (18, 14), (6, 3)], 1.2)
	elif k == "selsize":
		for i in range (0, 16, 4): L ([(3 + i, 5), (5 + i, 5)], 1.2); L ([(3 + i, 19), (5 + i, 19)], 1.2)
		for i in range (0, 12, 4): L ([(3, 5 + i), (3, 7 + i)], 1.2); L ([(19, 5 + i), (19, 7 + i)], 1.2)
	elif k == "imgsize": R (3, 5, 18, 14, WHITE, out = ink, wd = 1.2); P ([(5, 17), (10, 10), (14, 15), (16, 13), (19, 17)], (90, 150, 100))
	elif k == "editcol":
		for i, col in enumerate (((231, 76, 60), (243, 156, 18), (241, 196, 15), (46, 204, 113), (52, 152, 219), (155, 89, 182))):
			a0 = math.radians (90 + i * 60); a1 = math.radians (150 + i * 60)
			P ([(12, 12), (12 + 9 * math.cos (a0), 12 - 9 * math.sin (a0)), (12 + 9 * math.cos ((a0 + a1) / 2), 12 - 9 * math.sin ((a0 + a1) / 2)), (12 + 9 * math.cos (a1), 12 - 9 * math.sin (a1))], col)
		E (12, 12, 3.4, WHITE)

def chev (c, x, y, col = INK): c.poly ([(x - 3.5, y - 1.5), (x + 3.5, y - 1.5), (x, y + 2.5)], col)
def hot_box (c, x, y, w, h, on = False, hot = False):
	if on: c.rect (x, y, w, h, M.mix (RIB, SEL, 0.22), r = 5, outline = M.mix (RIB, SEL, 0.65))
	elif hot: c.rect (x, y, w, h, M.A (WHITE, 200), r = 5, outline = M.mix (RIB, LINE, 0.7))

# ---- the ribbon -------------------------------------------------------------------------------------------------------------
SHAPES = 15
def shape_glyph (c, k, x, y, w, h, col = INK):
	cx, cy, rx, ry = x + w / 2, y + h / 2, w / 2 - 2, h / 2 - 2
	def reg (n, start, inner = 0):
		pts = []
		for i in range (n * (2 if inner else 1)):
			a = math.radians (start + 360 * i / (n * (2 if inner else 1))); r = inner if inner and i % 2 else 1
			pts.append ((cx + rx * r * math.cos (a), cy - ry * r * math.sin (a)))
		return pts
	if k == 0: c.line ([(x + 2, y + h - 2), (x + w - 2, y + 2)], col, 1.4); return
	if k == 3: c.d.ellipse ([(cx - rx) * K, (cy - ry) * K, (cx + rx) * K, (cy + ry) * K], outline = col, width = int (1.3 * K)); return
	if k == 1: c.rect (x + 2, y + 3, w - 4, h - 6, outline = col, width = 1.3); return
	if k == 2: c.rect (x + 2, y + 3, w - 4, h - 6, r = 4, outline = col, width = 1.3); return
	pts = { 4: [(cx, y + 2), (x + w - 2, y + h - 2), (x + 2, y + h - 2)], 5: [(x + 2, y + 2), (x + w - 2, y + h - 2), (x + 2, y + h - 2)],
		6: [(cx, y + 2), (x + w - 2, cy), (cx, y + h - 2), (x + 2, cy)], 7: reg (5, 90), 8: reg (6, 0), 9: reg (8, 22.5),
		10: reg (4, 90, 0.4), 11: reg (5, 90, 0.42), 12: reg (6, 90, 0.55),
		13: [(x + 2, cy - 3), (x + w - 9, cy - 3), (x + w - 9, y + 2), (x + w - 2, cy), (x + w - 9, y + h - 2), (x + w - 9, cy + 3), (x + 2, cy + 3)] }.get (k)
	if k == 14:
		pts = []
		for i in range (48):
			t = 2 * math.pi * i / 48; hx = 16 * math.sin (t) ** 3; hy = 13 * math.cos (t) - 5 * math.cos (2 * t) - 2 * math.cos (3 * t) - math.cos (4 * t)
			pts.append ((cx + hx * rx / 16, y + 2 + (12 - hy) * (h - 4) / 29))
	c.line (pts + [pts[0]], col, 1.3)

PALETTE = [(0, 0, 0), (127, 127, 127), (136, 0, 21), (237, 28, 36), (255, 127, 39), (255, 242, 0), (34, 177, 76), (0, 162, 232), (63, 72, 204), (163, 73, 164),
	   (255, 255, 255), (195, 195, 195), (185, 122, 87), (255, 174, 201), (255, 201, 14), (239, 228, 176), (181, 230, 29), (153, 217, 234), (112, 146, 190), (200, 191, 231)]
CUSTOM = [(255, 226, 150), (86, 70, 118), (40, 52, 90), (220, 110, 120), None, None, None, None, None, None]

def ribbon (c, x, y, w, tool = "brush", sel_kind = "rect", shape = None, col1 = (86, 70, 118), col2 = (255, 226, 150), hot = None):
	H = 96
	c.rect (x, y, w, H, RIB); c.hline (x, x + w, y + H - 1, M.shade (RIB, 0.86))
	groups = []
	def big (bx, by, k, label, on = False, arrow = False, bw = 48):
		hot_box (c, bx, by, bw, 64, on, hot == label)
		icon (c, k, bx + (bw - 24) / 2, by + 8)
		c.text_c (bx, by + 38, bw, 16, label, "lab", TEXT)
		if arrow: chev (c, bx + bw / 2, by + 57)
	def small (bx, by, k, label = None, on = False, bw = 28, bh = 26):
		hot_box (c, bx, by, bw, bh, on, hot == (label or k))
		icon (c, k, bx + 4 if label else bx + (bw - 20) / 2, by + (bh - 20) / 2, 20)
		if label: c.text_l (bx + 28, by, bh, label, "lab", TEXT)
	def sep (sx): c.vline (sx, y + 10, y + H - 12, M.shade (RIB, 0.84))
	def glabel (gx, gw, s): c.text_c (gx, y + H - 22, gw, 16, s, "tiny2", DIM)
	gx = x + 10
	# Clipboard
	big (gx, y + 6, "paste", "Paste", bw = 46)
	small (gx + 50, y + 6, "cut"); small (gx + 50, y + 36, "copy")
	small (gx + 80, y + 6, "undo"); small (gx + 80, y + 36, "redo")
	glabel (gx, 108, "Clipboard"); gx += 116; sep (gx - 4)
	# Selection
	big (gx, y + 6, {"rect": "select", "free": "lasso", "wand": "wand"}[sel_kind], "Select", on = tool == "select", arrow = True)
	small (gx + 52, y + 4, "crop", "Crop", bw = 76, bh = 22); small (gx + 52, y + 27, "resize", "Resize", bw = 76, bh = 22); small (gx + 52, y + 50, "rotate", "Rotate", bw = 76, bh = 22)
	glabel (gx, 128, "Image"); gx += 138; sep (gx - 4)
	# Tools: 2 rows of 4
	tools = [("pencil", "pencil"), ("fill", "fill"), ("text", "text"), ("eraser", "eraser"), ("picker", "picker"), ("zoom", "zoom"), ("gradient", "gradient"), ("fx", "fx")]
	for i, (k, t) in enumerate (tools):
		small (gx + (i % 4) * 31, y + 8 + (i // 4) * 32, k, on = tool == t, bw = 28)
	glabel (gx, 122, "Tools"); gx += 130; sep (gx - 4)
	# Brushes: the brush's stroke + its name
	hot_box (c, gx, y + 6, 60, 64, tool == "brush", hot == "Brushes")
	stroke_sample (c, gx + 8, y + 12, 44, 26, "soft", (73, 146, 167))
	c.text_c (gx, y + 44, 60, 16, "Brushes", "lab", TEXT); chev (c, gx + 30, y + 63)
	glabel (gx, 60, "Brushes"); gx += 70; sep (gx - 4)
	# Shapes: the gallery 5 x 3
	c.rect (gx, y + 6, 5 * 24 + 4, 3 * 22 + 2, WHITE, r = 4, outline = M.mix (RIB, LINE, 0.8))
	for k in range (SHAPES):
		sx, sy = gx + 2 + (k % 5) * 24, y + 7 + (k // 5) * 22
		if k == shape: c.rect (sx, sy, 24, 22, SOFT, r = 3)
		shape_glyph (c, k, sx + 4, sy + 3, 16, 16)
	glabel (gx, 124, "Shapes"); gx += 134; sep (gx - 4)
	# Colours
	for i, (lab, col) in enumerate ((("1", col1), ("2", col2))):
		bx = gx + i * 42
		hot_box (c, bx, y + 6, 40, 64, i == 0)
		c.rect (bx + 7, y + 12, 26, 26, col, r = 13, outline = M.shade (col, 0.6))
		c.rect (bx + 5, y + 10, 30, 30, r = 15, outline = M.mix (RIB, INK, 0.25))
		c.text_c (bx, y + 44, 40, 16, "Colour " + lab if False else lab, "lab", TEXT)
	px0 = gx + 90
	for i, col in enumerate (PALETTE):
		c.rect (px0 + (i % 10) * 21, y + 7 + (i // 10) * 21, 18, 18, col, r = 9, outline = M.shade (col, 0.75))
	for i, col in enumerate (CUSTOM):
		if col: c.rect (px0 + i * 21, y + 7 + 2 * 21, 18, 18, col, r = 9, outline = M.shade (col, 0.75))
		else: c.rect (px0 + i * 21, y + 7 + 2 * 21, 18, 18, M.A (WHITE, 120), r = 9, outline = M.mix (RIB, LINE, 0.6))
	big (px0 + 214, y + 6, "editcol", "Edit", bw = 44)
	glabel (gx, 90 + 210 + 50, "Colours"); gx += 90 + 210 + 56
	return y + H

def stroke_sample (c, x, y, w, h, kind, col):
	"""a short S-shaped stroke of a brush"""
	pts = [(x + w * t, y + h / 2 + math.sin (t * math.pi * 2) * h * 0.3) for t in [i / 40 for i in range (41)]]
	rnd = random.Random (7)
	if kind == "pencil": c.line (pts, col, 1.2)
	elif kind == "brush": c.line (pts, col, 5)
	elif kind == "soft":
		for wd, a in ((11, 40), (8, 70), (5, 140), (3, 255)): c.line (pts, M.A (col, a), wd)
	elif kind == "callig":
		for i in range (len (pts) - 1):
			(ax, ay), (bx, by) = pts[i], pts[i + 1]
			c.poly ([(ax - 3, ay + 3), (ax + 3, ay - 3), (bx + 3, by - 3), (bx - 3, by + 3)], col)
	elif kind == "air":
		for px, py in pts[::2]:
			for _ in range (14):
				a = rnd.random () * 6.283; r = abs (rnd.gauss (0, 3.2))
				c.rect (px + math.cos (a) * r, py + math.sin (a) * r, 1, 1, M.A (col, 200))
	elif kind == "marker": c.line (pts, M.A (col, 120), 8)
	elif kind == "crayon":
		for px, py in pts:
			for _ in range (16):
				ox, oy = rnd.uniform (-3, 3), rnd.uniform (-3, 3)
				if rnd.random () < 0.6: c.rect (px + ox, py + oy, 1, 1, M.A (col, 220))
	elif kind.startswith ("pat_"):
		pat = kind[4:]
		for px, py in pts:
			for oy in range (-4, 5):
				for ox in range (-1, 2):
					X, Y = int (px + ox), int (py + oy)
					on = { "dots": X % 4 == 0 and Y % 4 == 0, "lines": (X + Y) % 4 == 0, "checks": (X // 3 + Y // 3) % 2 == 0,
					       "bricks": Y % 4 == 0 or (X + (4 if (Y // 4) % 2 else 0)) % 8 == 0, "hatch": (X + Y) % 5 == 0 or (X - Y) % 5 == 0,
					       "grid": X % 4 == 0 or Y % 4 == 0 }[pat]
					if on: c.rect (X, Y, 1, 1, col)

# ---- the options' bar ----------------------------------------------------------------------------------------------------
def slider (c, x, y, w, t, label = None, value = None):
	if label: c.text_l (x, y, 24, label, "lab", DIM); x += c.tw (label, "lab") + 8
	c.rect (x, y + 10, w, 4, M.mix (OPT, LINE, 0.9), r = 2); c.rect (x, y + 10, w * t, 4, SEL, r = 2)
	c.ellipse (x + w * t, y + 12, 7, WHITE, M.shade (OPT, 0.6), 1.2)
	if value: c.text_l (x + w + 10, y, 24, value, "lab", TEXT); return x + w + 10 + c.tw (value, "lab") + 18
	return x + w + 18
def small_drop (c, x, y, w, s, label = None):
	if label: c.text_l (x, y, 24, label, "lab", DIM); x += c.tw (label, "lab") + 8
	c.rect (x, y, w, 24, WHITE, r = 4, outline = M.mix (OPT, LINE, 0.9)); c.text_l (x + 8, y, 24, s, "lab", TEXT); chev (c, x + w - 11, y + 12)
	return x + w + 16
def toggle (c, x, y, s, on):
	w = c.tw (s, "lab") + 18
	c.rect (x, y, w, 24, M.mix (OPT, SEL, 0.25) if on else WHITE, r = 4, outline = M.mix (OPT, SEL, 0.7) if on else M.mix (OPT, LINE, 0.9))
	c.text_c (x, y, w, 24, s, "lab", TEXT); return x + w + 4
def osep (c, x, y): c.vline (x - 8, y + 4, y + 20, M.shade (OPT, 0.82))
def options_bar (c, x, y, w, kind = "brush"):
	H = 36
	c.rect (x, y, w, H, OPT); c.hline (x, x + w, y + H - 1, M.shade (OPT, 0.84))
	X = x + 12; Y = y + 6
	if kind == "brush":
		icon (c, "brush", X, Y + 1, 22); X += 30
		X = small_drop (c, X, Y, 150, "Soft round")
		osep (c, X, Y); X = slider (c, X, Y, 120, 0.42, "Size", "24 px")
		osep (c, X, Y); X = slider (c, X, Y, 100, 0.8, "Opacity", "80 %")
		osep (c, X, Y); X = slider (c, X, Y, 90, 0.35, "Hardness", "35 %")
		osep (c, X, Y); X = toggle (c, X, Y, "Eraser", False); X = toggle (c, X, Y, "Inside the selection", True)
	elif kind == "wand":
		icon (c, "wand", X, Y + 1, 22); X += 30
		c.text_l (X, Y, 24, "Selection", "lab", DIM); X += 62
		X = toggle (c, X, Y, "New", False); X = toggle (c, X, Y, "Add (Shift)", True); X = toggle (c, X, Y, "Subtract (Alt)", False); X += 12
		osep (c, X, Y); X = slider (c, X, Y, 120, 0.12, "Tolerance", "32")
		osep (c, X, Y); X = toggle (c, X, Y, "Contiguous", True); X = toggle (c, X, Y, "All layers", False); X += 12
		osep (c, X, Y); X = toggle (c, X, Y, "Select All", False); X = toggle (c, X, Y, "Invert", False); X = toggle (c, X, Y, "Deselect", False)
	elif kind == "text":
		icon (c, "text", X, Y + 1, 22); X += 30
		X = small_drop (c, X, Y, 170, "DejaVu Serif")
		X = small_drop (c, X, Y, 64, "30 px")
		for s, on in (("B", True), ("I", False), ("U", False)):
			c.rect (X, Y, 26, 24, M.mix (OPT, SEL, 0.25) if on else WHITE, r = 4, outline = M.mix (OPT, SEL, 0.7) if on else M.mix (OPT, LINE, 0.9))
			c.text_c (X, Y, 26, 24, s, "uib" if s == "B" else "ui", TEXT); X += 30
		X += 8; osep (c, X, Y)
		for i in range (3):
			c.rect (X, Y, 26, 24, M.mix (OPT, SEL, 0.25) if i == 1 else WHITE, r = 4, outline = M.mix (OPT, LINE, 0.9))
			for j, ww in enumerate ((12, 8, 12)):
				lx = X + 7 if i == 0 else X + 13 - ww / 2 if i == 1 else X + 19 - ww
				c.rect (lx, Y + 7 + j * 4, ww, 1.4, TEXT)
			X += 30
		X += 8; osep (c, X, Y); X = toggle (c, X, Y, "Smooth edges", True); X = toggle (c, X, Y, "Background (colour 2)", False)
		X += 8; osep (c, X, Y); c.text_l (X, Y, 24, "Enter: a new line.  Click outside: put it down.", "lab", DIM)
	elif kind == "fillgrad":
		icon (c, "fill", X, Y + 1, 22); X += 30
		c.text_l (X, Y, 24, "Fill with", "lab", DIM); X += 54
		X = toggle (c, X, Y, "Colour", False); X = toggle (c, X, Y, "Gradient", True); X = toggle (c, X, Y, "Pattern", False); X += 12
		osep (c, X, Y)
		c.rect (X, Y, 120, 24, WHITE, r = 4, outline = M.mix (OPT, LINE, 0.9)); grad_bar (c, X + 4, Y + 4, 96, 16, DUSK); chev (c, X + 110, Y + 12); X += 132
		for i, sh in enumerate (("lin", "bil", "rad", "sq", "con")):
			c.rect (X, Y, 26, 24, M.mix (OPT, SEL, 0.25) if i == 0 else WHITE, r = 4, outline = M.mix (OPT, SEL, 0.7) if i == 0 else M.mix (OPT, LINE, 0.9))
			grad_shape_icon (c, X + 4, Y + 3, 18, sh); X += 29
		X += 10; osep (c, X, Y); X = small_drop (c, X, Y, 92, "No repeat")
		X = toggle (c, X, Y, "Reverse", False)
		osep (c, X, Y); X = slider (c, X, Y, 70, 0.12, "Tolerance", "32")
		X = toggle (c, X, Y, "Contiguous", True)
	elif kind == "gradient":
		icon (c, "gradient", X, Y + 1, 22); X += 30
		X = toggle (c, X, Y, "Linear", True); X = toggle (c, X, Y, "Radial", False); X = toggle (c, X, Y, "Reflected", False); X += 12
		osep (c, X, Y); X = slider (c, X, Y, 100, 1.0, "Opacity", "100 %")
		osep (c, X, Y); X = toggle (c, X, Y, "Colour 1 to transparent", False)
	return y + H

# ---- gradients ----------------------------------------------------------------------------------------------------------
DUSK = [(0.0, (36, 70, 96), 255), (0.55, (128, 78, 150), 255), (1.0, (246, 166, 112), 255)]
PRESETS = [("Colour 1 to 2", [(0, (86, 70, 118), 255), (1, (255, 226, 150), 255)]), ("Colour 1 to transparent", [(0, (86, 70, 118), 255), (1, (86, 70, 118), 0)]),
	   ("Dusk hills", DUSK), ("Sunset", [(0, (52, 60, 120), 255), (0.45, (220, 110, 120), 255), (1, (252, 196, 120), 255)]),
	   ("Ocean", [(0, (8, 40, 80), 255), (0.6, (20, 130, 170), 255), (1, (190, 236, 240), 255)]),
	   ("Rainbow", [(0, (230, 40, 40), 255), (0.2, (250, 160, 30), 255), (0.4, (240, 230, 40), 255), (0.6, (50, 180, 80), 255), (0.8, (40, 110, 220), 255), (1, (140, 60, 200), 255)]),
	   ("Metal", [(0, (90, 94, 100), 255), (0.35, (225, 228, 232), 255), (0.5, (150, 154, 160), 255), (0.8, (240, 242, 244), 255), (1, (110, 114, 120), 255)]),
	   ("Fire", [(0, (40, 0, 0), 255), (0.4, (200, 30, 10), 255), (0.75, (250, 160, 20), 255), (1, (255, 250, 200), 255)])]
def grad_at (stops, t):
	t = min (1, max (0, t))
	for i in range (len (stops) - 1):
		a, b = stops[i], stops[i + 1]
		if t <= b[0]:
			u = 0 if b[0] == a[0] else (t - a[0]) / (b[0] - a[0])
			return M.mix (a[1], b[1], u), int (a[2] + (b[2] - a[2]) * u)
	return stops[-1][1], stops[-1][2]
def grad_bar (c, x, y, w, h, stops):
	n = max (2, int (w * K))
	img = checker (n, int (h * K), 4 * K).convert ("RGBA")
	g = Image.new ("RGBA", (n, int (h * K)))
	gd = ImageDraw.Draw (g)
	for i in range (n):
		col, a = grad_at (stops, i / (n - 1)); gd.line ([i, 0, i, h * K], fill = col + (a,))
	img = Image.alpha_composite (img, g)
	c.img.paste (img.convert ("RGB"), (int (x * K), int (y * K))); c.d = ImageDraw.Draw (c.img, "RGBA")
	c.rect (x, y, w, h, outline = M.mix (OPT, LINE, 0.6))
def grad_shape_icon (c, x, y, s, kind):
	n = int (s * K); im = Image.new ("RGB", (n, n)); px = im.load ()
	for j in range (n):
		for i in range (n):
			u, v = i / (n - 1), j / (n - 1)
			t = { "lin": u, "bil": abs (u - 0.5) * 2, "rad": min (1, math.hypot (u - 0.5, v - 0.5) * 2), "sq": max (abs (u - 0.5), abs (v - 0.5)) * 2,
			      "con": (math.atan2 (v - 0.5, u - 0.5) / (2 * math.pi)) % 1 }[kind]
			px[i, j] = M.mix ((60, 56, 96), (250, 250, 250), t)
	c.img.paste (im, (int (x * K), int (y * K))); c.d = ImageDraw.Draw (c.img, "RGBA")
	c.rect (x, y, s, s, outline = M.mix (OPT, LINE, 0.6))
HILLS = [(0, 330), (120, 280), (230, 320), (360, 290), (500, 330), (640, 300), (640, 352), (0, 352)]
G0, G1 = (70, 300), (590, 352)
def grad_fill_layer (stops, p0, p1):
	import numpy as np
	m = Image.new ("L", (PW * K, PH * K), 0); ImageDraw.Draw (m).polygon ([(x * K, y * K) for x, y in HILLS], fill = 255)
	yy, xx = np.mgrid[0:PH * K, 0:PW * K] / K
	dx, dy = p1[0] - p0[0], p1[1] - p0[1]
	t = np.clip (((xx - p0[0]) * dx + (yy - p0[1]) * dy) / (dx * dx + dy * dy), 0, 1)
	lut = np.array ([list (grad_at (stops, i / 255)[0]) + [grad_at (stops, i / 255)[1]] for i in range (256)], dtype = np.uint8)
	out = lut[(t * 255).astype (int)]
	out[..., 3] = (out[..., 3].astype (np.uint16) * np.asarray (m) // 255).astype (np.uint8)
	return Image.fromarray (out, "RGBA")

# ---- the layers' panel ----------------------------------------------------------------------------------------------------
LAYER_ROWS = [("Vignette", "mask", "Mask", 100, True), ("Title", "text", "Normal", 100, True), ("Light", "glow", "Add", 100, True),
	      ("Warm tint", "tint", "Multiply", 60, True), ("Landscape", "draw", "Normal", 100, True), ("Sky", "bg", "Normal", 100, True)]
def layers_panel (c, x, y, w, h, cur = 3, rows = None, open_blend = False):
	rows = rows or LAYER_ROWS
	c.rect (x, y, w, h, PANEL); c.vline (x, y, y + h, M.shade (PANEL, 0.84))
	c.text_l (x + 14, y + 6, 26, "Layers", "h2", TEXT)
	icon (c, "lock", x + w - 34, y + 9, 18, DIM)
	# the current layer's blend mode and opacity
	name, src, mode, op, vis = rows[cur]
	c.text_l (x + 14, y + 38, 24, "Blend", "lab", DIM)
	c.rect (x + 70, y + 38, w - 84, 24, WHITE, r = 4, outline = M.mix (PANEL, LINE, 0.9)); c.text_l (x + 78, y + 38, 24, mode, "lab", TEXT); chev (c, x + w - 25, y + 50)
	c.text_l (x + 14, y + 68, 24, "Opacity", "lab", DIM)
	sw = w - 84 - 46
	c.rect (x + 70, y + 78, sw, 4, M.mix (PANEL, LINE, 0.9), r = 2); c.rect (x + 70, y + 78, sw * op / 100, 4, SEL, r = 2)
	c.ellipse (x + 70 + sw * op / 100, y + 80, 7, WHITE, M.shade (PANEL, 0.6), 1.2)
	c.rect (x + w - 54, y + 68, 40, 24, WHITE, r = 4, outline = M.mix (PANEL, LINE, 0.9)); c.text_c (x + w - 54, y + 68, 40, 24, "%d %%" % op, "lab", TEXT)
	# the list
	ly = y + 102; lh = h - 102 - 44
	c.rect (x + 8, ly, w - 16, lh, WHITE, r = 6, outline = M.mix (PANEL, LINE, 0.85))
	ry = ly + 4
	for i, (nm, src, md, o, v) in enumerate (rows):
		RH = 54
		if i == cur: c.rect (x + 11, ry, w - 22, RH - 2, SEL, r = 5)
		elif i % 2: c.rect (x + 11, ry, w - 22, RH - 2, M.A ((0, 0, 0), 6), r = 5)
		fg = WHITE if i == cur else TEXT; dm = (220, 236, 242) if i == cur else DIM
		icon (c, "eye" if v else "eyeoff", x + 16, ry + 15, 20, WHITE if i == cur else INK)
		if src == "text":
			t = Image.new ("RGBA", (PW * K, PH * K), (0, 0, 0, 0)); ImageDraw.Draw (t).text ((PW / 2 * K, 92 * K), "Lac des Cimes", font = M.F["serif"], fill = (90, 70, 60, 255), anchor = "mm")
			thumb (c, x + 44, ry + 8, 54, 36, t)
		else: thumb (c, x + 44, ry + 8, 54, 36, LAYERS[src])
		c.text (x + 108, ry + 10, nm, "uib" if i == cur else "ui", fg)
		c.text (x + 108, ry + 29, md + ("" if o == 100 else " · %d %%" % o), "lab", dm)
		if md == "Mask": c.rect (x + w - 40, ry + 18, 18, 18, r = 3, outline = dm, width = 1.2); c.ellipse (x + w - 31, ry + 27, 5, dm)
		ry += RH
	# the buttons
	by = y + h - 38
	for i, k in enumerate (("add", "dup", "trash", "up", "down", "merge", "fx")):
		bx = x + 10 + i * ((w - 20) / 7)
		icon (c, k, bx + 6, by + 6, 20, INK if not (k == "up" and cur == 0) else FAINT)
	if open_blend:
		ox, oy, ow = x + 70, y + 64, w - 84
		modes = ["Normal", "Multiply", "Screen", "Add", "Subtract", "Lighten", None, "Mask", "Cut out"]
		hh = sum (8 if m is None else 26 for m in modes) + 8
		c.rect (ox + 2, oy + 4, ow, hh, M.A ((0, 0, 0), 40), r = 7)
		c.rect (ox, oy, ow, hh, WHITE, r = 7, outline = M.mix (PANEL, LINE, 0.9))
		yy = oy + 4
		hints = { "Multiply": "darkens, tints", "Screen": "lightens", "Add": "a glow", "Subtract": "", "Lighten": "", "Mask": "keeps what is under it", "Cut out": "removes it" }
		for m in modes:
			if m is None: c.hline (ox + 8, ox + ow - 8, yy + 4, LINE2); yy += 8; continue
			if m == mode: c.rect (ox + 4, yy, ow - 8, 24, SEL, r = 4)
			c.text_l (ox + 12, yy, 24, m, "lab", WHITE if m == mode else TEXT)
			hn = hints.get (m, "")
			if hn: c.text_r (ox + ow - 10, yy, 24, hn, "tiny2", (220, 236, 242) if m == mode else FAINT)
			yy += 26

# ---- the status bar --------------------------------------------------------------------------------------------------------
def status (c, x, y, w, ptr = "412, 238 px", sel = "", zoom = 100, layer = "Warm tint (Multiply)"):
	H = 28
	c.rect (x, y, w, H, OPT); c.hline (x, x + w, y, M.shade (OPT, 0.84))
	X = x + 10
	icon (c, "pointer", X, y + 5, 18); c.text_l (X + 24, y, H, ptr, "lab", TEXT); X += 130
	icon (c, "selsize", X, y + 5, 18); c.text_l (X + 24, y, H, sel, "lab", TEXT); X += 130
	icon (c, "imgsize", X, y + 5, 18); c.text_l (X + 24, y, H, "%d x %d px" % (PW, PH), "lab", TEXT); X += 140
	c.text_l (X, y, H, "Layer: " + layer, "lab", DIM)
	# the right: grid, fit, the zoom slider
	zx = x + w - 330
	icon (c, "grid", zx, y + 5, 18); icon (c, "fit", zx + 28, y + 5, 18)
	zx += 64
	c.text_c (zx, y, 16, H, "–", "ui", TEXT)
	sw = 150; t = math.log (zoom / 12.0) / math.log (3200 / 12.0)
	c.rect (zx + 20, y + 12, sw, 4, M.mix (OPT, LINE, 0.9), r = 2); c.vline (zx + 20 + sw * math.log (100 / 12) / math.log (3200 / 12), y + 8, y + 20, M.mix (OPT, LINE, 0.9))
	c.ellipse (zx + 20 + sw * t, y + 14, 6, WHITE, M.shade (OPT, 0.6), 1.2)
	c.text_c (zx + sw + 22, y, 16, H, "+", "ui", TEXT)
	c.text_l (zx + sw + 44, y, H, "%d %%" % zoom, "labb" if "labb" in M.F else "lab", TEXT)

# ---- a whole window ---------------------------------------------------------------------------------------------------------
WX, WY, WW, WH = 14, 36, 1252, 752
def paint_window (c, tool = "brush", opts = "brush", sel_kind = "rect", cur = 3, with_text = True, zoom_pic = 1.0, open_blend = False, ptr = "412, 238 px", sel = "", extra = None):
	x, y, w, h = M.window (c, WX, WY, WW, WH, "Lac des Cimes.ora - Paint")
	yy = ribbon (c, x, y, w, tool = tool, sel_kind = sel_kind)
	yy = options_bar (c, x, yy, w, opts)
	PANW = 264; SB = 12
	body_h = y + h - 28 - yy
	cw = w - PANW
	c.rect (x, yy, cw, body_h, DESKC)
	# the scroll bars (thin, on the desk)
	c.rect (x + cw - SB, yy + 4, 8, body_h - 24, M.A (WHITE, 40), r = 4); c.rect (x + cw - SB + 1, yy + 40, 6, body_h - 120, M.A (WHITE, 90), r = 3)
	c.rect (x + 4, yy + body_h - SB, cw - 24, 8, M.A (WHITE, 40), r = 4); c.rect (x + 60, yy + body_h - SB + 1, cw - 160, 6, M.A (WHITE, 90), r = 3)
	pw, ph = PW * zoom_pic, PH * zoom_pic
	px, py = x + (cw - pw) / 2, yy + (body_h - ph) / 2 - 4
	put_picture (c, px, py, picture (with_mask = True, with_text = with_text, extra = extra), zoom_pic)
	layers_panel (c, x + cw, yy, PANW, body_h, cur = cur, open_blend = open_blend)
	nm, _, md, _, _ = LAYER_ROWS[cur]
	status (c, x, y + h - 28, w, ptr, sel, layer = "%s (%s)" % (nm, md))
	return (px, py, zoom_pic), (x, yy, cw, body_h)

def ants (c, pts, closed = True):
	"""marching ants along a polyline"""
	seq = pts + ([pts[0]] if closed else [])
	acc = 0
	for i in range (len (seq) - 1):
		(ax, ay), (bx, by) = seq[i], seq[i + 1]
		L = math.hypot (bx - ax, by - ay); n = max (1, int (L / 2))
		for j in range (n):
			t0, t1 = j / n, (j + 1) / n
			col = (0, 0, 0) if int ((acc + L * t0) / 5) % 2 == 0 else WHITE
			c.line ([(ax + (bx - ax) * t0, ay + (by - ay) * t0), (ax + (bx - ax) * t1, ay + (by - ay) * t1)], col, 1.2)
		acc += L

def menu_popup (c, x, y, w, items, hot = None):
	hh = sum (8 if it is None else 26 for it in items) + 8
	c.rect (x + 2, y + 4, w, hh, M.A ((0, 0, 0), 40), r = 7)
	c.rect (x, y, w, hh, WHITE, r = 7, outline = M.mix (RIB, LINE, 0.9))
	yy = y + 4
	for it in items:
		if it is None: c.hline (x + 8, x + w - 8, yy + 4, LINE2); yy += 8; continue
		label, key, ic = it
		if label == hot: c.rect (x + 4, yy, w - 8, 24, SEL, r = 4)
		fg = WHITE if label == hot else TEXT
		if ic: icon (c, ic, x + 10, yy + 3, 18, WHITE if label == hot else INK)
		c.text_l (x + 36, yy, 24, label, "lab", fg)
		if key: c.text_r (x + w - 12, yy, 24, key, "tiny2", (220, 236, 242) if label == hot else FAINT)
		yy += 26
	return hh

# ==== the mock-ups =================================================================================================================
def m_main ():
	c, _ = screen ()
	(px, py, z), body = paint_window (c)
	# a soft stroke being drawn on "Warm tint"?  no: the brush's ring under the pointer
	cx, cy = px + 412, py + 238
	c.ellipse (cx, cy, 12, None, (0, 0, 0), 1); c.ellipse (cx, cy, 13, None, WHITE, 1)
	c.save ("paint-main.png")

def m_brushes ():
	c, _ = screen ()
	(px, py, z), body = paint_window (c)
	# the Brushes gallery, open under its button
	bx, by = WX + 4 + 10 + 116 + 138 + 130, WY + 28 + 76
	gw, gh = 420, 382
	c.rect (bx + 2, by + 4, gw, gh, M.A ((0, 0, 0), 50), r = 8)
	c.rect (bx, by, gw, gh, WHITE, r = 8, outline = M.mix (RIB, LINE, 0.9))
	def cell (i, y0, k, name):
		cx, cy = bx + 10 + (i % 3) * 134, y0 + (i // 3) * 62
		if k == "soft": c.rect (cx, cy, 128, 56, SOFT, r = 5, outline = M.mix (WHITE, SEL, 0.5))
		stroke_sample (c, cx + 10, cy + 6, 108, 28, k, (60, 56, 96))
		c.text_c (cx, cy + 36, 128, 18, name, "lab", TEXT)
	c.text (bx + 14, by + 10, "Brushes", "smallb", DIM)
	for i, (k, n) in enumerate ([("pencil", "Pencil"), ("brush", "Brush"), ("soft", "Soft round"), ("callig", "Calligraphy"), ("air", "Airbrush"), ("marker", "Marker"), ("crayon", "Crayon")]):
		cell (i, by + 30, k, n)
	py2 = by + 30 + 3 * 62 + 4
	c.hline (bx + 12, bx + gw - 12, py2, LINE2)
	c.text (bx + 14, py2 + 8, "Patterns (colour 1, on the picture's grid)", "smallb", DIM)
	for i, (k, n) in enumerate ([("pat_dots", "Dots"), ("pat_lines", "Lines"), ("pat_checks", "Checks"), ("pat_bricks", "Bricks"), ("pat_hatch", "Hatching"), ("pat_grid", "Grid")]):
		cell (i, py2 + 30, k, n)
	c.save ("paint-brushes.png")

def m_select ():
	c, pos = screen ()
	(px, py, z), body = paint_window (c, tool = "select", opts = "wand", sel_kind = "wand", cur = 4, ptr = "253, 162 px", sel = "248 x 132 px")
	# the magic wand's selection: the mountains' outline (contiguous), the ants
	pts = [(0, 300), (90, 190), (160, 250), (250, 150), (350, 260), (440, 200), (540, 270), (640, 210), (640, 332), (500, 330), (360, 290), (230, 320), (120, 280), (0, 330)]
	ants (c, [(px + a, py + b) for a, b in pts])
	c.rect (px + 252, py + 160, 1, 1)
	icon (c, "wand", px + 250, py + 158, 22, WHITE)
	# the Select menu open under its button
	sx, sy = WX + 4 + 10 + 116, WY + 28 + 74
	menu_popup (c, sx, sy, 250, [("Rectangle", "S", "select"), ("Free-form (lasso)", "L", "lasso"), ("Magic wand", "W", "wand"), None,
				      ("Select All", "Ctrl+A", None), ("Invert Selection", "Ctrl+I", None), ("Deselect", "Esc", None), None,
				      ("Delete", "Del", None), ("Crop to Selection", "", "crop")], hot = "Magic wand")
	c.save ("paint-select.png")

def m_layers ():
	c, _ = screen ()
	(px, py, z), body = paint_window (c, cur = 0, open_blend = True, opts = "gradient", tool = "gradient")
	c.save ("paint-layers.png")

def m_text ():
	c, _ = screen ()
	(px, py, z), body = paint_window (c, tool = "text", opts = "text", cur = 1, with_text = True)
	tw = M.F["serif"].getlength ("Lac des Cimes") / K
	c.dashed (px + PW / 2 - tw / 2 - 10, py + 92 - 26, tw + 20, 52, WHITE, dash = 5, r = 0)
	for hx, hy in ((0, 0), (1, 0), (0, 1), (1, 1)):
		c.rect (px + PW / 2 - tw / 2 - 10 + hx * (tw + 20) - 4, py + 66 + hy * 52 - 4, 8, 8, WHITE, outline = INK)
	c.vline (px + PW / 2 + tw / 2 + 2, py + 76, py + 108, WHITE, 2)
	c.save ("paint-text.png")

def m_resize ():
	"""the Resize dialog: Tab goes from one field to the next (uikit's fix)"""
	c, _ = screen ()
	(px, py, z), body = paint_window (c)
	c.rect (0, 0, M.W, M.H, M.A ((0, 0, 0), 50))
	dw, dh = 420, 356; dx, dy = (M.W - dw) / 2, (M.H - dh) / 2
	c.rect (dx + 3, dy + 6, dw, dh, M.A ((0, 0, 0), 60), r = 9)
	c.rect (dx, dy, dw, dh, FACE, r = 8, outline = M.shade (FACE, 0.6))
	c.grad (dx, dy, dw, 30, M.lighten (M.PEACH[0], 0.2), M.PEACH[1], r = 8, corners = (True, True, False, False))
	c.text_c (dx, dy, dw, 30, "Resize", "title")
	X, Y = dx + 22, dy + 46
	tot = M.segmented (c, X, Y, 28, ["Resize the picture", "Canvas size"], 0)
	Y += 44
	M.radio (c, X, Y, "By percentage", False); M.radio (c, X + 170, Y, "By pixels", True); Y += 36
	c.text_l (X, Y, 28, "Width", "ui", TEXT); M.field (c, X + 90, Y, 100, 28, "1280"); c.text_l (X + 200, Y, 28, "px", "ui", DIM)
	c.line ([(X + 230, Y + 14), (X + 240, Y + 14), (X + 240, Y + 50), (X + 230, Y + 50)], DIM, 1.2)
	icon (c, "lock", X + 248, Y + 22, 20, SEL)
	Y += 36
	c.text_l (X, Y, 28, "Height", "ui", TEXT)
	c.rect (X + 88, Y - 2, 104, 32, M.A (SEL, 70), r = 6)
	M.field (c, X + 90, Y, 100, 28, "840", caret = True); c.text_l (X + 200, Y, 28, "px", "ui", DIM)
	Y += 44
	M.checkbox (c, X, Y, "Keep the proportions", True); Y += 28
	c.text_l (X, Y, 16, "Resampling", "ui", TEXT); M.dropdown (c, X + 110, Y - 6, 220, 28, "Smooth (bilinear)"); Y += 40
	c.text_l (X, Y, 16, "Tab / Shift+Tab: the next / previous field.", "small", DIM)
	M.button (c, dx + dw - 196, dy + dh - 46, 86, 30, "OK", default = True); M.button (c, dx + dw - 102, dy + dh - 46, 86, 30, "Cancel")
	c.save ("paint-resize.png")

def m_gradfill ():
	"""the paint bucket in gradient mode: the zone of the press (its colour, the tolerance), the line drawn = the direction"""
	c, _ = screen ()
	(px, py, z), body = paint_window (c, tool = "fill", opts = "fillgrad", cur = 4, extra = grad_fill_layer (DUSK, G0, G1), ptr = "590, 352 px")
	ants (c, [(px + a, py + b) for a, b in HILLS])
	(ax, ay), (bx, by) = (px + G0[0], py + G0[1]), (px + G1[0], py + G1[1])
	c.line ([(ax, ay), (bx, by)], (0, 0, 0), 3); c.line ([(ax, ay), (bx, by)], WHITE, 1.4)
	c.ellipse (ax, ay, 6, WHITE, (0, 0, 0), 1.4); c.ellipse (bx, by, 6, WHITE, (0, 0, 0), 1.4)
	# the stops along the line: draggable there too
	for t, col, a in DUSK[1:-1]:
		sx, sy = ax + (bx - ax) * t, ay + (by - ay) * t
		c.poly ([(sx, sy - 7), (sx + 6, sy), (sx, sy + 7), (sx - 6, sy)], col); c.line ([(sx, sy - 7), (sx + 6, sy), (sx, sy + 7), (sx - 6, sy), (sx, sy - 7)], WHITE, 1.2)
	icon (c, "fill", bx + 8, by - 26, 22, WHITE)
	tip = "The zone clicked, filled along the line   ·   Shift: 15° steps   ·   Enter: apply   ·   Esc: cancel"
	tw = c.tw (tip, "lab") + 20
	c.rect (px + (PW - tw) / 2, py + PH + 14, tw, 24, M.A ((20, 20, 24), 210), r = 12); c.text_c (px + (PW - tw) / 2, py + PH + 14, tw, 24, tip, "lab", WHITE)
	# the gradients' list open from the options bar
	ox, oy = WX + 4 + 12 + 30 + 54 + 3 * 0, WY + 28 + 96 + 32
	ox = 300; ow = 300
	hh = len (PRESETS) * 30 + 44
	c.rect (ox + 2, oy + 4, ow, hh, M.A ((0, 0, 0), 50), r = 8); c.rect (ox, oy, ow, hh, WHITE, r = 8, outline = M.mix (RIB, LINE, 0.9))
	yy = oy + 6
	for name, st in PRESETS:
		if name == "Dusk hills": c.rect (ox + 4, yy, ow - 8, 28, SOFT, r = 4)
		grad_bar (c, ox + 10, yy + 6, 96, 16, st); c.text_l (ox + 116, yy, 28, name, "lab", TEXT); yy += 30
	c.hline (ox + 8, ox + ow - 8, yy + 3, LINE2)
	c.text_l (ox + 14, yy + 6, 28, "Edit gradients...", "lab", TEXT); c.text_r (ox + ow - 12, yy + 6, 28, "GIMP .ggr", "tiny2", FAINT)
	c.save ("paint-gradient-fill.png")

def m_gradedit ():
	"""the gradient editor, in the way of GIMP's: stops (colour, opacity), midpoints, the presets"""
	c, _ = screen ()
	paint_window (c, tool = "fill", opts = "fillgrad", cur = 4, extra = grad_fill_layer (DUSK, G0, G1))
	c.rect (0, 0, M.W, M.H, M.A ((0, 0, 0), 50))
	dw, dh = 660, 430; dx, dy = (M.W - dw) / 2, (M.H - dh) / 2
	c.rect (dx + 3, dy + 6, dw, dh, M.A ((0, 0, 0), 60), r = 9)
	c.rect (dx, dy, dw, dh, FACE, r = 8, outline = M.shade (FACE, 0.6))
	c.grad (dx, dy, dw, 30, M.lighten (M.PEACH[0], 0.2), M.PEACH[1], r = 8, corners = (True, True, False, False))
	c.text_c (dx, dy, dw, 30, "Gradient Editor", "title")
	# the presets
	lx, ly, lw, lh = dx + 14, dy + 44, 206, dh - 100
	c.rect (lx, ly, lw, lh, WHITE, r = 5, outline = LINE)
	yy = ly + 4
	for name, st in PRESETS:
		if name == "Dusk hills": c.rect (lx + 3, yy, lw - 6, 34, SEL, r = 4)
		grad_bar (c, lx + 8, yy + 4, 56, 26, st); c.text_l (lx + 72, yy, 34, name, "lab", WHITE if name == "Dusk hills" else TEXT); yy += 36
	M.button (c, lx, ly + lh + 8, 62, 28, "New"); M.button (c, lx + 67, ly + lh + 8, 62, 28, "Copy"); M.button (c, lx + 134, ly + lh + 8, 62, 28, "Delete")
	# the bar, its stops below, the midpoints above
	X, Y, BW = dx + 238, dy + 48, dw - 238 - 18
	c.text_l (X, Y, 24, "Name", "ui", TEXT); M.field (c, X + 56, Y, BW - 56, 26, "Dusk hills"); Y += 40
	grad_bar (c, X, Y + 14, BW, 56, DUSK)
	for t in (0.275, 0.775):								# the midpoints (between two stops)
		mx = X + BW * t; c.poly ([(mx, Y + 2), (mx + 5, Y + 8), (mx, Y + 13), (mx - 5, Y + 8)], M.A (WHITE, 220)); c.line ([(mx, Y + 2), (mx + 5, Y + 8), (mx, Y + 13), (mx - 5, Y + 8), (mx, Y + 2)], INK, 1)
	for i, (t, col, a) in enumerate (DUSK):						# the stops
		sx = X + BW * t; sy = Y + 72
		selc = i == 1
		c.poly ([(sx, sy), (sx + 8, sy + 12), (sx + 8, sy + 26), (sx - 8, sy + 26), (sx - 8, sy + 12)], WHITE if not selc else SEL)
		c.rect (sx - 5, sy + 13, 10, 10, col, outline = INK, width = 1)
		c.line ([(sx, sy), (sx + 8, sy + 12), (sx + 8, sy + 26), (sx - 8, sy + 26), (sx - 8, sy + 12), (sx, sy)], INK, 1)
	Y += 112
	c.text (X, Y, "Click under the bar: a new stop  ·  drag: move it (off the bar: remove it)", "tiny2", DIM)
	Y += 26
	M.group (c, X, Y, BW, 118, "The stop")
	c.text_l (X + 14, Y + 30, 26, "Colour", "ui", TEXT); c.rect (X + 84, Y + 30, 40, 26, DUSK[1][1], r = 4, outline = INK); M.field (c, X + 132, Y + 30, 90, 26, "#804E96")
	c.text_l (X + 240, Y + 30, 26, "Position", "ui", TEXT); M.field (c, X + 310, Y + 30, 64, 26, "55 %")
	c.text_l (X + 14, Y + 70, 26, "Opacity", "ui", TEXT)
	sw = 200; c.rect (X + 84, Y + 81, sw, 4, M.mix (FACE, LINE, 0.9), r = 2); c.rect (X + 84, Y + 81, sw, 4, SEL, r = 2); c.ellipse (X + 84 + sw, Y + 83, 7, WHITE, M.shade (FACE, 0.6), 1.2)
	c.text_l (X + 300, Y + 70, 26, "100 %", "ui", TEXT)
	Y += 132
	for i, b in enumerate (("Reverse", "Space evenly", "Colour 1 / 2")):
		M.button (c, X + i * 128, Y, 120, 28, b)
	M.button (c, dx + dw - 196, dy + dh - 44, 86, 30, "OK", default = True); M.button (c, dx + dw - 102, dy + dh - 44, 86, 30, "Cancel")
	c.save ("paint-gradient-editor.png")

if __name__ == "__main__":
	which = sys.argv[1:]
	for name, f in (("main", m_main), ("brushes", m_brushes), ("select", m_select), ("layers", m_layers), ("text", m_text), ("resize", m_resize),
			("gradient-fill", m_gradfill), ("gradient-editor", m_gradedit)):
		if not which or name in which: f ()
