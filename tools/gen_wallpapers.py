#!/usr/bin/env python3
# gen_wallpapers.py -- the desktop's abstract wallpaper PATTERNS (sdcard/wallpapers/*.png): made by
# code, in grey (1024 x 768, the screen's default size), light for the most part. The Control
# Panel's Theme applet colours one (the wallpaper's "pattern" mode, user/Include/wallpaper.h): each pixel's
# grey MULTIPLIES the wallpaper's colours (a gradient from colour 1 to colour 2) -- white is the
# colour itself, the darker greys its shades.
#
#   python3 tools/gen_wallpapers.py [name ...]      (default: all; needs numpy + Pillow)
#   python3 tools/gen_wallpapers.py --preview OUT.png   (the patterns as the Theme applet colours
#                                                        them: screenshots/wallpapers.png)
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

W, H = 1024, 768
OUT = os.path.join (os.path.dirname (os.path.abspath (__file__)), "..", "sdcard", "wallpapers")
rng = np.random.default_rng (20260929)

def grid ():
	y, x = np.mgrid[0:H, 0:W].astype (np.float32)
	return x, y

def to_img (a, lo = 96, hi = 255):
	"""a in 0..1 -> grey lo..hi"""
	a = np.clip (a, 0.0, 1.0)
	return Image.fromarray ((lo + (hi - lo) * a).astype (np.uint8), "L")

def smooth_noise (scale, octaves = 4, seed = 0):
	"""value noise, 0..1, smooth (bicubic-upscaled random grids)"""
	r = np.random.default_rng (seed)
	acc = np.zeros ((H, W), np.float32); amp = 1.0; tot = 0.0
	for o in range (octaves):
		gw, gh = max (2, int (W / scale) + 2), max (2, int (H / scale) + 2)
		small = Image.fromarray ((r.random ((gh, gw)) * 255).astype (np.uint8), "L")
		big = np.asarray (small.resize ((W + int (scale), H + int (scale)), Image.BICUBIC), np.float32)[:H, :W] / 255.0
		acc += amp * big; tot += amp
		amp *= 0.5; scale /= 2
	return acc / tot

# ---- the patterns ----------------------------------------------------------------------------------
def waves ():
	"""flowing ribbons: layered sine bands, each shaded across its width"""
	x, y = grid ()
	a = np.full ((H, W), 0.92, np.float32)
	for i in range (7):
		ph = i * 0.9; amp = 60 + 25 * i; base = 120 + i * 85
		c = base + amp * np.sin (x / (260 + 40 * i) + ph) + 30 * np.sin (x / 97 + ph * 2)
		d = (y - c) / (70 + 10 * i)			# across the band: -1 .. 1 inside
		band = np.clip (1 - d * d, 0, 1)
		shade = 0.55 + 0.45 * np.clip (d * 0.5 + 0.5, 0, 1)
		a = a * (1 - 0.35 * band) + 0.35 * band * shade
	a += 0.08 * (1 - y / H)
	return to_img ((a - a.min ()) / (a.max () - a.min ()), 90, 255)

def lowpoly ():
	"""a low-poly facet field: a jittered grid cut into triangles, lit from the top left"""
	im = Image.new ("L", (W, H), 255); d = ImageDraw.Draw (im)
	cs = 96; nx, ny = W // cs + 3, H // cs + 3
	pts = np.zeros ((ny, nx, 2), np.float32)
	for j in range (ny):
		for i in range (nx):
			pts[j, i] = ((i - 1) * cs + rng.uniform (-0.38, 0.38) * cs, (j - 1) * cs + rng.uniform (-0.38, 0.38) * cs)
	light = np.array ([-0.5, -0.7, 0.9]); light /= np.linalg.norm (light)
	hgt = smooth_noise (300, 3, 7)
	def z (px, py): return 420 * hgt[int (min (max (py, 0), H - 1)), int (min (max (px, 0), W - 1))]
	for j in range (ny - 1):
		for i in range (nx - 1):
			p = [pts[j, i], pts[j, i + 1], pts[j + 1, i + 1], pts[j + 1, i]]
			for tri in ((p[0], p[1], p[2]), (p[0], p[2], p[3])) if (i + j) % 2 else ((p[0], p[1], p[3]), (p[1], p[2], p[3])):
				a3 = [np.array ([q[0], q[1], z (q[0], q[1])]) for q in tri]
				n = np.cross (a3[1] - a3[0], a3[2] - a3[0]); n /= np.linalg.norm (n) + 1e-6
				if n[2] < 0: n = -n
				v = 0.35 + 0.65 * max (0.0, float (np.dot (n, light)))
				cy = sum (q[1] for q in tri) / 3
				v = v * (0.85 + 0.15 * (1 - cy / H))
				d.polygon ([tuple (q) for q in tri], fill = int (95 + 160 * min (v, 1.0)))
	return im

def hexagons ():
	"""a honeycomb: bevelled cells, their light varying slowly"""
	x, y = grid ()
	s = 46.0
	qx = x / (s * math.sqrt (3)); qy = y / (s * 1.5)
	# nearest hexagon centre (pointy top), by rounding in the two lattices
	r1 = np.stack ([np.round (qx), np.round (qy)])
	cy1 = r1[1] * 1.5 * s; cx1 = (r1[0] + (r1[1] % 2) * 0.5) * s * math.sqrt (3)
	d1 = (x - cx1) ** 2 + (y - cy1) ** 2
	best_cx, best_cy, best_d = cx1, cy1, d1
	for ox, oy in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, -1), (-1, 1), (1, 1)):
		ry = np.round (qy) + oy; rx = np.round (qx) + ox
		cyy = ry * 1.5 * s; cxx = (rx + (ry % 2) * 0.5) * s * math.sqrt (3)
		dd = (x - cxx) ** 2 + (y - cyy) ** 2
		m = dd < best_d
		best_cx = np.where (m, cxx, best_cx); best_cy = np.where (m, cyy, best_cy); best_d = np.where (m, dd, best_d)
	dx, dy = x - best_cx, y - best_cy
	# the distance to the cell's edge (a pointy-top hexagon: its sides' normals at 0 and +-60 degrees)
	ax, ay = np.abs (dx), np.abs (dy)
	hexd = np.maximum (ax, ax * 0.5 + ay * math.sqrt (3) / 2)
	edge = s * math.sqrt (3) / 2 - hexd
	cellv = smooth_noise (220, 2, 3)[np.clip (best_cy.astype (int), 0, H - 1), np.clip (best_cx.astype (int), 0, W - 1)]
	face = 0.72 + 0.28 * cellv
	bevel = np.clip (edge / 7.0, 0, 1)
	lightside = np.clip (0.5 - (dx + dy) / (2 * s), 0, 1)
	a = face * (0.55 + 0.45 * bevel) + (1 - bevel) * 0.25 * lightside
	gap = np.clip ((edge + 1.5) / 2.0, 0, 1)
	a = a * (0.35 + 0.65 * gap)
	return to_img (a / a.max (), 80, 255)

def contours ():
	"""topographic lines over a light relief"""
	f = smooth_noise (420, 4, 11)
	f = (f - f.min ()) / (f.max () - f.min ())
	lv = f * 22
	frac = np.abs (lv - np.round (lv))			# 0 on a line
	line = np.clip (1 - frac / 0.07, 0, 1)
	major = (np.round (lv) % 5 == 0)
	a = 0.86 + 0.14 * f - line * np.where (major, 0.42, 0.24)
	return to_img (a, 70, 255)

def bokeh ():
	"""soft discs of light, of every size, over a gentle gradient"""
	x, y = grid ()
	a = 0.62 + 0.18 * (1 - y / H) + 0.1 * smooth_noise (500, 2, 5)
	img = np.array (a, np.float32)
	for i in range (70):
		cx, cy = rng.uniform (0, W), rng.uniform (0, H)
		r = rng.choice ([rng.uniform (14, 40), rng.uniform (40, 110)], p = [0.6, 0.4])
		op = rng.uniform (0.12, 0.38)
		d = np.sqrt ((x - cx) ** 2 + (y - cy) ** 2) / r
		disc = np.clip ((1 - d) * 6, 0, 1)
		rim = np.clip (1 - np.abs (d - 0.93) / 0.07, 0, 1) * 0.6
		img = img + op * (disc * 0.8 + rim) * (1 - img)
	return to_img (img, 70, 255)

def facets ():
	"""crystal facets: Voronoi cells, each a plane lit from the top left, thin light edges"""
	x, y = grid ()
	n = 90
	px, py = rng.uniform (0, W, n), rng.uniform (0, H, n)
	d1 = np.full ((H, W), 1e9, np.float32); d2 = np.full ((H, W), 1e9, np.float32); idx = np.zeros ((H, W), np.int32)
	for i in range (n):
		dd = (x - px[i]) ** 2 + (y - py[i]) ** 2
		m = dd < d1
		d2 = np.where (m, d1, np.minimum (d2, dd)); idx = np.where (m, i, idx); d1 = np.where (m, dd, d1)
	gx, gy = rng.uniform (-1, 1, n), rng.uniform (-1, 1, n)
	lvl = rng.uniform (0.78, 1.0, n)
	a = lvl[idx] + 0.06 * (gx[idx] * (x - px[idx]) + gy[idx] * (y - py[idx])) / 120.0
	edge = np.sqrt (d2) - np.sqrt (d1)
	a = np.where (edge < 2.2, 1.0, a) - np.where ((edge >= 2.2) & (edge < 3.4), 0.12, 0)
	return to_img ((a - a.min ()) / (a.max () - a.min ()), 90, 255)

def silk ():
	"""silky folds: a smooth field folded by itself (domain warping), shaded"""
	x, y = grid ()
	u, v = x / W * 6.0, y / H * 4.5
	w1 = np.sin (u * 1.3 + np.sin (v * 1.7) * 1.2) + np.cos (v * 1.1 - u * 0.4)
	w2 = np.sin (v * 1.9 + w1 * 1.4) + 0.6 * np.sin (u * 0.8 - w1)
	f = np.sin (u * 0.9 + w2 * 1.6 + v * 0.3)
	a = 0.5 + 0.5 * f
	a = a ** 1.4
	a = 0.55 + 0.45 * a + 0.08 * (1 - y / H)
	return to_img ((a - a.min ()) / (a.max () - a.min ()), 85, 255)

def dunes ():
	"""layered hills (dunes, mountains far away): lighter the farther, each shaded along its crest"""
	x, y = grid ()
	a = 0.97 - 0.12 * (y / H)
	for i in range (6):
		base = 250 + i * 95
		amp = 55 + i * 14
		crest = base + amp * np.sin (x / (230 - i * 18) + i * 1.7) + 0.5 * amp * np.sin (x / (97 + i * 11) + i)
		inside = y > crest
		depth = np.clip ((y - crest) / 260.0, 0, 1)
		slope = np.gradient (crest, axis = 1)
		shade = np.clip (0.78 - 0.22 * i / 5 - 0.35 * depth + 0.9 * slope * 0.02, 0.35, 1)
		a = np.where (inside, shade, a)
	return to_img (a, 70, 255)

PATTERNS = { "waves": waves, "low-poly": lowpoly, "hexagons": hexagons, "contours": contours,
	     "bokeh": bokeh, "facets": facets, "silk": silk, "dunes": dunes }

# A gallery: each pattern coloured as wallpaper.h does it -- its grey times a gradient from colour
# 1 (top) to colour 2 (bottom) -- in a few of the Theme applet's colours.
TINTS = [((0x48, 0x78, 0xB0), (0x1C, 0x2C, 0x48)), ((0xF0, 0xB0, 0x7A), (0x8A, 0x3A, 0x5A)),
	 ((0x6A, 0xB0, 0x8A), (0x2A, 0x4A, 0x6A)), ((0xA8, 0x88, 0xD0), (0x30, 0x28, 0x58))]
def preview (out):
	tw, th, pad = 256, 192, 8
	names = sorted (PATTERNS)
	cols = 4; rows = (len (names) + cols - 1) // cols
	mont = Image.new ("RGB", (cols * (tw + pad) + pad, rows * (th + pad) + pad), (230, 226, 222))
	for i, n in enumerate (names):
		g = np.asarray (Image.open (os.path.join (OUT, n + ".png")).convert ("L").resize ((tw, th), Image.BOX), np.float32) / 255.0
		c1, c2 = TINTS[i % len (TINTS)]
		t = np.linspace (0, 1, th)[:, None, None]
		tint = np.array (c1, np.float32)[None, None, :] * (1 - t) + np.array (c2, np.float32)[None, None, :] * t
		im = Image.fromarray ((g[:, :, None] * tint).clip (0, 255).astype (np.uint8))
		mont.paste (im, (pad + (i % cols) * (tw + pad), pad + (i // cols) * (th + pad)))
	mont.save (out, optimize = True)
	print ("wrote", out)

def main ():
	if len (sys.argv) == 3 and sys.argv[1] == "--preview":
		preview (sys.argv[2]); return
	os.makedirs (OUT, exist_ok = True)
	names = sys.argv[1:] or list (PATTERNS)
	for n in names:
		im = PATTERNS[n] ()
		p = os.path.join (OUT, n + ".png")
		im.save (p, optimize = True)
		print ("%-10s %6d KB  %s" % (n, os.path.getsize (p) // 1024, os.path.relpath (p)))

main ()
