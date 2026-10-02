#!/usr/bin/env python3
"""gen_paint_samples.py -- two pictures to try Paint with (its layers, Open as Layer, a fade from one to the
other with a Mask "on the layer below only"): a sunset over the sea, sunny mountains. Made here (noise,
gradients, fractal ridges), so they are ours: MIT licence (Onyx).

    python3 tools/gen_paint_samples.py   -> sdcard/docs/pictures/sunset-sea.jpg, sunny-mountains.jpg (1280 x 800)
"""
import os
import numpy as np
from PIL import Image, ImageFilter

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
OUT = os.path.join (ROOT, "sdcard", "docs", "pictures")
W, H = 1280, 800
rng = np.random.default_rng (2026)

def lerp (a, b, t): return a + (b - a) * t
def smooth (t): return t * t * (3 - 2 * t)
def grad (stops, t):
	"""colour stops [(pos, (r, g, b))] at t (an array 0..1) -> H x W x 3"""
	t = np.clip (t, 0, 1); out = np.zeros (t.shape + (3,))
	for (p0, c0), (p1, c1) in zip (stops, stops[1:]):
		m = (t >= p0) & (t <= p1)
		u = ((t - p0) / max (1e-9, p1 - p0))[..., None]
		out = np.where (m[..., None], np.array (c0) + (np.array (c1) - np.array (c0)) * u, out)
	return out
def value_noise (h, w, cell, seed):
	"""smooth noise: a random grid of cell px, bicubic-ish (smoothstep) between"""
	r = np.random.default_rng (seed)
	gh, gw = h // cell + 3, w // cell + 3
	g = r.random ((gh, gw))
	y = np.arange (h) / cell; x = np.arange (w) / cell
	y0 = y.astype (int); x0 = x.astype (int); fy = smooth (y - y0)[:, None]; fx = smooth (x - x0)[None, :]
	a = g[y0][:, x0]; b = g[y0][:, x0 + 1]; c = g[y0 + 1][:, x0]; d = g[y0 + 1][:, x0 + 1]
	return lerp (lerp (a, b, fx), lerp (c, d, fx), fy)
def fbm (h, w, cell, octaves, seed, gain = 0.5):
	n = np.zeros ((h, w)); amp = 1; tot = 0
	for o in range (octaves):
		n += amp * value_noise (h, w, max (2, int (cell / 2 ** o)), seed + o); tot += amp; amp *= gain
	return n / tot
def ridge (w, base, amp, rough, seed):
	"""a mountain range's top line (midpoint displacement) -> y per x"""
	r = np.random.default_rng (seed)
	n = 1
	while n < w: n *= 2
	pts = np.zeros (n + 1); pts[0] = r.uniform (-1, 1); pts[-1] = r.uniform (-1, 1)
	step = n; scale = 1.0
	while step > 1:
		half = step // 2
		for i in range (half, n, step): pts[i] = (pts[i - half] + pts[i + half]) / 2 + r.uniform (-1, 1) * scale
		step = half; scale *= rough
	return base - pts[:w] / np.abs (pts).max () * amp
def put (img, mask, col, alpha = 1.0):
	m = np.clip (mask, 0, 1)[..., None] * alpha
	return img * (1 - m) + np.array (col, dtype = float) * m if np.ndim (col) == 1 else img * (1 - m) + col * m
def glow (cx, cy, r, power = 2.0):
	y, x = np.mgrid[0:H, 0:W]
	d = np.sqrt ((x - cx) ** 2 + (y - cy) ** 2) / r
	return np.clip (1 - d, 0, 1) ** power
def save (img, name):
	os.makedirs (OUT, exist_ok = True)
	im = Image.fromarray (np.clip (img, 0, 255).astype ("uint8"), "RGB")
	p = os.path.join (OUT, name); im.save (p, quality = 90, optimize = True); print ("wrote", os.path.relpath (p, ROOT))

# ---- a sunset over the sea ----------------------------------------------------------------------------------------------
def sunset ():
	y, x = np.mgrid[0:H, 0:W]
	hz = 470								# the horizon
	sky = grad ([(0, (28, 30, 82)), (0.35, (96, 52, 128)), (0.62, (222, 96, 104)), (0.85, (250, 160, 96)), (1, (255, 214, 140))], y / hz)
	img = sky.copy ()
	sx, sy = 820, 430
	img = put (img, glow (sx, sy, 520, 1.6), (255, 170, 90), 0.55)
	img = put (img, glow (sx, sy, 160, 1.4), (255, 220, 150), 0.8)
	sun = np.clip ((58 - np.sqrt ((x - sx) ** 2 + (y - sy) ** 2)) / 2, 0, 1)
	img = put (img, sun, (255, 244, 206))
	# clouds: long streaks of noise, lit from below near the sun
	n = fbm (H, W, 220, 5, 11)
	streak = np.clip ((fbm (H, W // 6, 60, 4, 12)), 0, 1)
	streak = np.array (Image.fromarray ((streak * 255).astype ("uint8")).resize ((W, H), Image.BICUBIC)) / 255.0
	band = np.exp (-((y - 300) / 120.0) ** 2) + 0.6 * np.exp (-((y - 150) / 70.0) ** 2)
	c = np.clip ((streak * 0.7 + n * 0.5 - 0.55) * 3, 0, 1) * band
	lit = grad ([(0, (120, 60, 120)), (0.5, (250, 120, 110)), (1, (255, 200, 150))], np.clip (1 - np.abs (x - sx) / 900, 0, 1) * np.clip (y / hz, 0, 1) * 1.2)
	img = img * (1 - c[..., None] * 0.85) + lit * c[..., None] * 0.85
	# an island far away
	xs = np.arange (W)
	env = np.clip (np.sin (np.clip ((xs - 70) / 520, 0, 1) * np.pi), 0, 1) ** 0.7		# (a hump: it rises from the water, sinks again)
	isl = hz - (env * (34 + 14 * np.sin (xs / 37.0) + 10 * np.sin (xs / 13.0 + 1)))
	m = np.clip ((y - isl[None, :]) * 1.5, 0, 1) * (y < hz)
	img = put (img, m, (70, 40, 86), 0.92)
	# the sea: the sky mirrored, darker, ripples, the sun's path
	sea_t = (y - hz) / (H - hz)
	refl = sky[np.clip (2 * hz - y, 0, H - 1), x]
	sea = refl * 0.55 + np.array ((20, 24, 60)) * 0.45
	sea = sea * (1 - sea_t[..., None] * 0.45)
	rip = fbm (H, W // 8, 6, 3, 21)
	rip = np.array (Image.fromarray ((rip * 255).astype ("uint8")).resize ((W, H), Image.BICUBIC)) / 255.0
	path = np.exp (-((x - sx) / (60 + 260 * sea_t)) ** 2) * np.clip ((rip - 0.45) * 4, 0, 1)
	sea = sea + path[..., None] * np.array ((255, 190, 120)) * 0.75
	sea = sea * (0.92 + 0.16 * rip[..., None])
	img = np.where ((y >= hz)[..., None], sea, img)
	img = put (img, np.clip (1 - np.abs (y - hz) / 3, 0, 1), (255, 200, 150), 0.35)	# the horizon's glow
	save (img, "sunset-sea.jpg")

# ---- sunny mountains -------------------------------------------------------------------------------------------------------
def peaks (w, base, height, n, rough, seed):
	"""a range's top line: a few peaks (their flanks) and fractal detail"""
	r = np.random.default_rng (seed)
	xs = np.arange (w, dtype = float)
	top = np.zeros (w)
	for _ in range (n):
		cx = r.uniform (-0.1, 1.1) * w; h = r.uniform (0.45, 1.0) * height; sl = r.uniform (0.35, 0.8)
		top = np.maximum (top, h - np.abs (xs - cx) * sl)
	d = ridge (w, 0, 1, rough, seed + 7)
	smooth_top = base - np.clip (top, 0, None)
	return base - np.clip (top + d * height * 0.12, 0, None), smooth_top
def box (a, k):
	return np.convolve (np.pad (a, k, mode = "edge"), np.ones (2 * k + 1) / (2 * k + 1), mode = "same")[k:-k]
def mountains ():
	y, x = np.mgrid[0:H, 0:W]
	img = grad ([(0, (46, 106, 192)), (0.55, (112, 170, 226)), (1, (206, 228, 244))], y / 560)
	sx, sy = 250, 130
	img = put (img, glow (sx, sy, 640, 2.2), (255, 248, 222), 0.55)
	img = put (img, np.clip ((38 - np.sqrt ((x - sx) ** 2 + (y - sy) ** 2)) / 2, 0, 1), (255, 252, 238))
	n = fbm (H, W, 260, 6, 31)
	cl = np.clip ((n - 0.6) * 5, 0, 1) * np.exp (-((y - 170) / 120.0) ** 2)
	shade = np.clip (fbm (H, W, 120, 3, 32) * 1.2, 0, 1)
	img = img * (1 - cl[..., None]) + (np.array ((255, 255, 255)) * (0.84 + 0.16 * shade[..., None])) * cl[..., None]
	# ranges, far to near: lighter and bluer far away (the air), snow above their snow line, the slopes
	# facing the sun (on the left) lit
	ranges = [(520, 300, 6, 0.55, 41, (138, 156, 196), 330), (580, 250, 5, 0.55, 42, (98, 116, 156), 400),
		  (640, 150, 4, 0.5, 43, (64, 94, 96), None), (690, 70, 4, 0.45, 44, (66, 108, 62), None)]
	for base, height, npk, rough, seed, col, snowline in ranges:
		top, st = peaks (W, base, height, npk, rough, seed)
		m = np.clip (y - top[None, :], 0, 1)
		slope = box (np.gradient (st), 3)[None, :]
		grain = fbm (H, W, 14, 3, seed + 200)
		lit = np.clip (0.55 + slope * 0.6 + (grain - 0.5) * 0.25, 0.2, 1.1)
		depth = np.clip ((y - top[None, :]) / 260, 0, 1)
		c = np.array (col) * (0.72 + 0.4 * lit[..., None]) * (1 - 0.18 * depth[..., None])
		if snowline:
			edge = snowline + fbm (H, W, 30, 3, seed + 100) * 70 - 35
			s = np.clip ((edge - y) / 6, 0, 1)
			snow = np.array ((246, 248, 252)) * (0.78 + 0.22 * np.clip (lit, 0, 1)[..., None])
			c = c * (1 - s[..., None]) + snow * s[..., None]
		img = img * (1 - m[..., None]) + c * m[..., None]
		# (the air between the ranges: a little haze at their feet)
		img = put (img, np.exp (-((y - base) / 50.0) ** 2) * m, (196, 214, 234), 0.18)
	mead = 712 + 10 * np.sin (np.arange (W) / 90.0)
	m = np.clip (y - mead[None, :], 0, 1)
	g = grad ([(0, (128, 176, 72)), (1, (62, 114, 48))], (y - 700) / 100) * (0.9 + 0.2 * fbm (H, W, 40, 3, 51)[..., None])
	img = img * (1 - m[..., None]) + g * m[..., None]
	lake = np.clip ((1 - ((x - 860) / 250.0) ** 2 - ((y - 748) / 22.0) ** 2) * 8, 0, 1)
	sky_r = grad ([(0, (150, 196, 236)), (1, (90, 140, 200))], (y - 726) / 44)
	img = img * (1 - lake[..., None]) + sky_r * lake[..., None]
	for (fx, fh) in ((90, 150), (150, 200), (205, 130), (1110, 180), (1170, 230), (1225, 150)):
		for k in range (5):						# (tiers of branches)
			t0 = 790 - fh + k * fh / 6; t1 = t0 + fh / 3
			tri = np.clip (np.minimum (t1 - y, (y - t0) * 0.3 - np.abs (x - fx) + 1), 0, 1) * (y > t0)
			img = put (img, tri, (26, 56, 40), 0.96)
		img = put (img, ((np.abs (x - fx) < 4) & (y > 770) & (y < 795)).astype (float), (60, 40, 30))
	save (img, "sunny-mountains.jpg")

if __name__ == "__main__":
	sunset (); mountains ()
