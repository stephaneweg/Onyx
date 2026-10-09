#!/usr/bin/env python3
# Copyright (c) 2026 the Onyx authors -- MIT licence (see docs/LICENSING.md).
"""mockup_console_ps4.py -- console mode v2: the console shell (user/Apps/consolehome) redrawn in the manner of
a modern TV console home (a function row at the top, one big horizontal row of tiles, the focused item's
picture as a soft full-screen backdrop, an info area under the row) combined with Lakka's idea of classifying
the games by console. A design study only (docs/COMPACT-SHELL-STUDY.md, "Console mode v2"): nothing here is
built.

    python3 tools/screenshot/mockup_console_ps4.py     -> docs/compact-shell/mockups/console-v2-*.png

Self-contained (PIL + numpy): the drawing helpers are mockup_compact.py's, copied. The content is the card's:
the apps' names, categories and icons (sdcard/apps/<app>.app/app.txt, icon.bmp, magenta = see-through), the
consoles from the emulators' `games =` and `order =` lines, the Control Panel's applets (their .lnk help
lines), the Onyx games' real screenshots (screenshots/<game>.png) as their pictures. The ROMs are made up:
invented names, title screens drawn here (160 x 144, the Game Library's thumbnail size) as
tools/tests/desktop_sim/gamelib_samples.py does -- an N64 game's picture is a cartridge label with its name,
a GameCube one its banner. No real game, logo or console maker's mark is drawn; the pad's buttons are named
A / B / X / Y / L1 / R1 / Home as the current shell does.

Resolution independent: every size is in logical units (lp) times a scale k; the layout reads the logical
size (W / k, H / k) and has a compact variant below 560 logical lines (a 640 x 480 handheld).
"""
import math, os, random
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname (os.path.abspath (__file__))
ROOT = os.path.abspath (os.path.join (HERE, "..", ".."))
SD = os.path.join (ROOT, "sdcard")
SHOTS = os.path.join (ROOT, "screenshots")
OUT = os.path.join (ROOT, "docs", "compact-shell", "mockups")

# ---- fonts: the card's DejaVu ------------------------------------------------------------------------------
_FD = [os.path.join (SD, "res", "fonts"), "/usr/share/fonts/truetype/dejavu"]
_fc = {}
def font (size, bold = False, oblique = False):
	size = max (6, int (round (size))); k = (size, bold, oblique)
	if k not in _fc:
		name = "DejaVuSans" + ("-Bold" if bold else "") + ("Oblique" if oblique and bold else "-Oblique" if oblique else "") + ".ttf"
		for d in _FD:
			p = os.path.join (d, name)
			if os.path.exists (p): _fc[k] = ImageFont.truetype (p, size); break
		else: _fc[k] = ImageFont.load_default ()
	return _fc[k]

# ---- colour and drawing helpers (mockup_compact.py's) --------------------------------------------------------
S = 4
def lighten (c, k): return tuple (int (v + (255 - v) * k) for v in c[:3])
def shade (c, k): return tuple (int (v * k) for v in c[:3])
def mix (a, b, t): return tuple (int (a[i] + (b[i] - a[i]) * t) for i in range (3))
def mask (w, h, fn):
	w, h = int (round (w)), int (round (h))
	m = Image.new ("L", (max (1, w * S), max (1, h * S)), 0); fn (ImageDraw.Draw (m), S)
	return m.resize ((max (1, w), max (1, h)), Image.BOX)
def grad (w, h, top, bottom, horizontal = False):
	w, h = max (1, int (w)), max (1, int (h))
	n = w if horizontal else h
	t = np.linspace (0, 1, max (n, 1))
	a = np.array (top, float)[None, :] * (1 - t[:, None]) + np.array (bottom, float)[None, :] * t[:, None]
	a = a[None, :, :].repeat (h, 0) if horizontal else a[:, None, :].repeat (w, 1)
	return Image.fromarray (a.astype ("uint8"), "RGB")
def put (img, x, y, m, colour, alpha = 255):
	alpha = int (alpha); colour = colour if isinstance (colour, Image.Image) else tuple (int (c) for c in colour[:3])
	if alpha < 255: m = m.point (lambda v: v * alpha // 255)
	src = colour if isinstance (colour, Image.Image) else Image.new ("RGB", m.size, colour[:3])
	img.paste (src, (int (x), int (y)), m)
def rrect (img, x, y, w, h, r, colour, alpha = 255, corners = None):
	x, y, w, h = int (x), int (y), int (w), int (h)
	if w <= 0 or h <= 0: return
	m = mask (w, h, lambda d, s: d.rounded_rectangle ([0, 0, w * s - 1, h * s - 1], r * s, fill = 255, corners = corners))
	put (img, x, y, m, grad (w, h, *colour) if isinstance (colour[0], tuple) else colour, alpha)
def ring (img, x, y, w, h, r, colour, t = 1, alpha = 255):
	x, y, w, h = int (x), int (y), int (w), int (h)
	m = mask (w, h, lambda d, s: d.rounded_rectangle ([0, 0, w * s - 1, h * s - 1], r * s, outline = 255, width = max (1, int (t * s))))
	put (img, x, y, m, colour, alpha)
def box (img, x, y, w, h, colour, alpha = 255):
	put (img, int (x), int (y), Image.new ("L", (max (1, int (w)), max (1, int (h))), 255), colour, alpha)
def hline (img, x, y, w, colour, alpha = 255): box (img, x, y, w, max (1, 1), colour, alpha)
def glyph (img, x, y, w, h, fn, colour, alpha = 255): put (img, x, y, mask (int (w), int (h), fn), colour, alpha)
def tw (s, f): return int (round (f.getlength (s)))
def text (img, x, y, s, f, colour): ImageDraw.Draw (img).text ((int (x), int (y)), s, font = f, fill = colour[:3])
def _vc (f, h):
	b = f.getbbox ("Hg"); return (h - (b[3] - b[1])) // 2 - b[1]
def text_l (img, x, y, h, s, f, colour): text (img, x, y + _vc (f, h), s, f, colour)
def text_c (img, x, y, w, h, s, f, colour): text (img, x + (w - tw (s, f)) // 2, y + _vc (f, h), s, f, colour)
def text_r (img, x, y, h, s, f, colour): text (img, x - tw (s, f), y + _vc (f, h), s, f, colour)
def ellipsize (s, f, w):
	if tw (s, f) <= w: return s
	while s and tw (s + "...", f) > w: s = s[:-1]
	return s + "..."
def wrap (s, f, w):
	out, cur = [], ""
	for word in s.split ():
		t = (cur + " " + word).strip ()
		if tw (t, f) <= w: cur = t
		else: out.append (cur); cur = word
	if cur: out.append (cur)
	return out
def add_glow (img, colour, mask_l, radius, strength = 1.0):
	m = mask_l.filter (ImageFilter.GaussianBlur (radius))
	a = np.asarray (img).astype (float); mm = np.asarray (m).astype (float)[..., None] / 255 * strength
	return Image.fromarray (np.clip (a + np.array (colour, float)[None, None, :] * mm, 0, 255).astype ("uint8"), "RGB")
def dim (img, alpha, colour = (6, 10, 24)): return Image.blend (img, Image.new ("RGB", img.size, colour), alpha / 255)

# ---- small glyphs ----------------------------------------------------------------------------------------------
def g_gem (d, s, k = 1):
	u = s * k; d.polygon ([(4 * u, 1 * u), (12 * u, 1 * u), (16 * u, 5.5 * u), (8 * u, 15 * u), (0 * u, 5.5 * u)], fill = 255)
def g_gem_facets (d, s, k = 1):
	u = s * k
	d.line ([(0, 5.5 * u), (16 * u, 5.5 * u)], fill = 0, width = max (1, int (1 * u)))
	d.line ([(4 * u, 1 * u), (6 * u, 5.5 * u), (8 * u, 15 * u), (10 * u, 5.5 * u), (12 * u, 1 * u)], fill = 0, width = max (1, int (1 * u)))
def g_bell (d, s, k = 1):
	u = s * k
	d.pieslice ([3 * u, 1.5 * u, 13 * u, 12 * u], 180, 360, fill = 255); d.rectangle ([3 * u, 6.5 * u, 13 * u, 11 * u], fill = 255)
	d.polygon ([(1 * u, 12.5 * u), (15 * u, 12.5 * u), (13 * u, 10.5 * u), (3 * u, 10.5 * u)], fill = 255)
	d.ellipse ([6.3 * u, 12.5 * u, 9.7 * u, 15.5 * u], fill = 255)
def g_gear (d, s, k = 1):
	u = s * k; c = 8 * u
	for i in range (8):
		a = i * math.pi / 4; d.line ([(c, c), (c + 7.5 * u * math.cos (a), c + 7.5 * u * math.sin (a))], fill = 255, width = int (3 * u))
	d.ellipse ([c - 5.5 * u, c - 5.5 * u, c + 5.5 * u, c + 5.5 * u], fill = 255); d.ellipse ([c - 2.3 * u, c - 2.3 * u, c + 2.3 * u, c + 2.3 * u], fill = 0)
def g_power (d, s, k = 1):
	u = s * k
	d.arc ([2 * u, 3 * u, 14 * u, 15 * u], 300, 240, fill = 255, width = int (2 * u)); d.line ([8 * u, 1 * u, 8 * u, 8 * u], fill = 255, width = int (2 * u))
def g_grid (d, s, k = 1):
	u = s * k
	for i in range (3):
		for j in range (3): d.rounded_rectangle ([(1 + i * 5) * u, (1 + j * 5) * u, (4 + i * 5) * u, (4 + j * 5) * u], u, fill = 255)
def g_clock (d, s, k = 1):
	u = s * k
	d.ellipse ([1 * u, 1 * u, 15 * u, 15 * u], outline = 255, width = int (1.8 * u))
	d.line ([(8 * u, 4 * u), (8 * u, 8 * u), (11 * u, 10 * u)], fill = 255, width = int (1.8 * u))
def g_folder (d, s, k = 1):
	u = s * k
	d.rounded_rectangle ([1 * u, 3 * u, 7 * u, 6 * u], u, fill = 255); d.rounded_rectangle ([1 * u, 5 * u, 15 * u, 14 * u], 1.5 * u, fill = 255)
def g_download (d, s, k = 1):
	u = s * k
	d.line ([8 * u, 1 * u, 8 * u, 10 * u], fill = 255, width = int (2 * u))
	d.polygon ([(3.5 * u, 6.5 * u), (12.5 * u, 6.5 * u), (8 * u, 11.5 * u)], fill = 255)
	d.line ([2 * u, 14 * u, 14 * u, 14 * u], fill = 255, width = int (2 * u))
def g_play (d, s, k = 1):
	u = s * k; d.polygon ([(3 * u, 1.5 * u), (14 * u, 8 * u), (3 * u, 14.5 * u)], fill = 255)
def g_wifi (d, s, k = 1):
	u = s * k; cx, cy = 8 * u, 13 * u
	for r in (12, 8, 4): d.arc ([cx - r * u, cy - r * u, cx + r * u, cy + r * u], 225, 315, fill = 255, width = int (1.8 * u))
	d.ellipse ([cx - 1.6 * u, cy - 1.6 * u, cx + 1.6 * u, cy + 1.6 * u], fill = 255)
def g_chev (d, s, k = 1, dirn = "right"):
	u = s * k
	pts = {"right": [(5, 2), (11, 8), (5, 14)], "left": [(11, 2), (5, 8), (11, 14)], "down": [(2, 5), (8, 11), (14, 5)], "up": [(2, 11), (8, 5), (14, 11)]}[dirn]
	d.line ([(a * u, b * u) for a, b in pts], fill = 255, width = int (2.2 * u), joint = "curve")
def g_camera (d, s, k = 1):
	u = s * k
	d.rounded_rectangle ([1 * u, 4 * u, 15 * u, 14 * u], 2 * u, fill = 255); d.rectangle ([5 * u, 2 * u, 10 * u, 4 * u], fill = 255)
	d.ellipse ([5 * u, 6 * u, 11 * u, 12 * u], fill = 0); d.ellipse ([6.5 * u, 7.5 * u, 9.5 * u, 10.5 * u], fill = 255)
def g_save (d, s, k = 1):
	u = s * k
	d.rounded_rectangle ([1 * u, 1 * u, 15 * u, 15 * u], 2 * u, fill = 255); d.rectangle ([4 * u, 1 * u, 11 * u, 6 * u], fill = 0)
	d.rectangle ([4 * u, 9 * u, 12 * u, 15 * u], fill = 0)
def g_load (d, s, k = 1):
	u = s * k
	d.arc ([2 * u, 2 * u, 14 * u, 14 * u], 60, 330, fill = 255, width = int (2 * u)); d.polygon ([(14.5 * u, 1 * u), (14.5 * u, 7 * u), (9 * u, 5 * u)], fill = 255)
def g_home (d, s, k = 1):
	u = s * k
	d.polygon ([(8 * u, 1 * u), (15 * u, 7.5 * u), (13 * u, 7.5 * u), (13 * u, 15 * u), (3 * u, 15 * u), (3 * u, 7.5 * u), (1 * u, 7.5 * u)], fill = 255)
	d.rectangle ([6.5 * u, 10 * u, 9.5 * u, 15 * u], fill = 0)
def g_close (d, s, k = 1):
	u = s * k
	d.line ([3 * u, 3 * u, 13 * u, 13 * u], fill = 255, width = int (2.2 * u)); d.line ([13 * u, 3 * u, 3 * u, 13 * u], fill = 255, width = int (2.2 * u))
def g_menu (d, s, k = 1):
	u = s * k
	for i in range (3): d.rounded_rectangle ([1 * u, (2 + i * 5) * u, 15 * u, (4 + i * 5) * u], u, fill = 255)
def g_dots (d, s, k = 1):
	u = s * k
	for i in range (3): d.ellipse ([(1.5 + i * 5) * u, 6.5 * u, (4.5 + i * 5) * u, 9.5 * u], fill = 255)
GLYPH = dict (gem = g_gem, bell = g_bell, gear = g_gear, power = g_power, grid = g_grid, clock = g_clock, folder = g_folder,
	      download = g_download, play = g_play, wifi = g_wifi, camera = g_camera, save = g_save, load = g_load, home = g_home,
	      close = g_close, menu = g_menu, dots = g_dots)
def icon_g (img, name, x, y, size, colour, alpha = 255):
	"""A 16-unit glyph drawn at size x size pixels."""
	k = size / 16; glyph (img, x, y, size, size, lambda d, s: GLYPH[name] (d, s, k), colour, alpha)

def battery (img, x, y, k, colour):
	ring (img, x, y + 1 * k, 22 * k, 12 * k, 2.5 * k, colour, t = 1.4 * k)
	box (img, x + 22 * k, y + 4.5 * k, 2 * k, 5 * k, colour)
	rrect (img, x + 2.5 * k, y + 3.5 * k, int (17 * k * 0.78), 7 * k, 1 * k, colour)

# ---- the apps: names, categories, icons (the card's app.txt) ------------------------------------------------
def read_kv (path):
	m = {}
	try:
		for line in open (path, encoding = "utf-8"):
			if "=" in line and not line.lstrip ().startswith (("#", ";")):
				k, v = [s.strip () for s in line.split ("=", 1)]; m[k] = v
	except OSError: pass
	return m
APPS = {}
for d in sorted (os.listdir (os.path.join (SD, "apps"))):
	if d.endswith (".app"):
		m = read_kv (os.path.join (SD, "apps", d, "app.txt"))
		if m: APPS[d[:-4]] = m
def app_name (a): return APPS.get (a, {}).get ("name", a)
def apps_in (cat): return sorted ([a for a, m in APPS.items () if m.get ("category") == cat], key = lambda a: app_name (a).lower ())

_icons = {}
def app_icon (name, size):
	k = (name, size)
	if k not in _icons:
		p = os.path.join (SD, "apps", name + ".app", "icon.bmp")
		if not os.path.exists (p): p = os.path.join (SD, "apps", "terminal.app", "icon.bmp")
		a = np.array (Image.open (p).convert ("RGB"))
		alpha = np.where ((a[..., 0] == 255) & (a[..., 1] == 0) & (a[..., 2] == 255), 0, 255).astype ("uint8")
		im = Image.fromarray (np.dstack ([a, alpha]), "RGBA")
		_icons[k] = im.convert ("RGBa").resize ((size, size), Image.LANCZOS).convert ("RGBA")
	return _icons[k]
def draw_icon (img, name, x, y, size):
	ic = app_icon (name, int (size)); img.paste (ic, (int (x), int (y)), ic)
def icon_colour (name):
	"""The icon's mean colour (its see-through pixels left out): the tile's plate."""
	a = np.array (Image.open (os.path.join (SD, "apps", name + ".app", "icon.bmp")).convert ("RGB")).reshape (-1, 3).astype (float)
	keep = ~((a[:, 0] == 255) & (a[:, 1] == 0) & (a[:, 2] == 255))
	c = a[keep].mean (0) if keep.any () else np.array ([80, 100, 140])
	return tuple (int (v) for v in c)

# ---- the consoles: the emulators' `games =` and `order =` -----------------------------------------------------
SHORT = {"GameCube": "GC", "Nintendo 64": "N64", "Super Nintendo": "SNES", "Game Boy Advance": "GBA",
	 "Game Boy Color": "GBC", "Game Boy": "GB", "NES": "NES"}
SYSCOL = {"GC": (112, 98, 214), "N64": (46, 150, 104), "SNES": (150, 132, 196), "GBA": (66, 112, 222),
	  "GBC": (204, 84, 150), "GB": (122, 150, 44), "NES": (206, 72, 62)}
def consoles ():
	out = []
	for a, m in APPS.items ():
		if "games" not in m: continue
		for i, part in enumerate (m["games"].split (";")):
			if ":" not in part: continue
			name, exts = [s.strip () for s in part.split (":", 1)]
			out.append ((int (m.get ("order", "99")), i, name, exts.split (), a))
	return [(name, exts, emu) for _, _, name, exts, emu in sorted (out)]
SYSTEMS = consoles ()				# [(name, [ext], emulator app)] in the Game Library's order
SYSNAME = {SHORT.get (n, n): n for n, _, _ in SYSTEMS}
SYSEMU = {SHORT.get (n, n): e for n, _, e in SYSTEMS}
SYSEXT = {SHORT.get (n, n): x[0] for n, x, _ in SYSTEMS}

# ---- the made-up ROMs: (title, scene, last played, time played, save states) ------------------------------------
ROMS = {
	"GC":   [("Harbor Lights", "sea", "28 Sep", "3 h 05 min", 1), ("Ember Knights", "night", "12 Sep", "11 h 20 min", 2)],
	"N64":  [("Sky Fortress", "hills", "yesterday", "9 h 12 min", 3), ("Kite Rally", "track", "3 Oct", "2 h 48 min", 1),
		 ("Polar Post", "snow", "21 Sep", "4 h 30 min", 0), ("Hover Derby", "city", "2 Sep", "55 min", 0)],
	"SNES": [("Moon Garden", "night", "Tuesday", "14 h 02 min", 2), ("Star Runner", "space", "4 Oct", "3 h 10 min", 1),
		 ("Castle Keeper", "forest", "29 Sep", "7 h 45 min", 3), ("Ocean Deep", "sea", "14 Sep", "1 h 20 min", 0),
		 ("Thunder Lane", "track", "1 Sep", "40 min", 0)],
	"GBA":  [("Star Courier", "hills", "Monday 21:04", "6 h 41 min", 2), ("Tiny Racer", "track", "5 Oct", "2 h 15 min", 1),
		 ("Lantern Fox", "forest", "2 Oct", "5 h 30 min", 1), ("Rune Tactics", "desert", "27 Sep", "12 h 08 min", 3),
		 ("Pocket Derby", "city", "20 Sep", "1 h 02 min", 0), ("Cloud Mail", "sky", "18 Sep", "3 h 33 min", 1),
		 ("Desert Bloom", "desert", "9 Sep", "48 min", 0), ("Gear Golem", "space", "1 Sep", "2 h 10 min", 0),
		 ("Paper Ninja", "night", "30 Aug", "4 h 51 min", 2)],
	"GBC":  [("Robot Party", "city", "1 Oct", "1 h 30 min", 0), ("Bug Catcher", "forest", "22 Sep", "6 h 15 min", 1),
		 ("Tobi's Island", "sea", "11 Sep", "2 h 05 min", 0)],
	"GB":   [("Block Drop", "blocks", "Sunday", "8 h 40 min", 1), ("Maze Mouse", "forest", "19 Sep", "1 h 12 min", 0),
		 ("Frog Hop", "hills", "8 Sep", "35 min", 0), ("Pixel Knight", "night", "20 Aug", "5 h 00 min", 2)],
	"NES":  [("Pixel Quest", "hills", "6 Oct", "4 h 20 min", 1), ("Jungle Jump", "forest", "25 Sep", "2 h 55 min", 0),
		 ("Rocket Rescue", "space", "15 Sep", "1 h 05 min", 0)],
}
def rom_file (sysc, title):
	return "SD:/roms/%s/%s.%s" % (sysc.lower (), title.lower ().replace ("'", "").replace (" ", "-"), SYSEXT.get (sysc, "rom"))

# ---- the title screens (160 x 144, the Game Library's thumbnails) -------------------------------------------
PW, PH = 160, 144
def scene (style, seed, w = PW, h = PH):
	rnd = random.Random (seed)
	im = Image.new ("RGB", (w, h)); d = ImageDraw.Draw (im)
	def sky (top, bottom): im.paste (grad (w, h, top, bottom), (0, 0))
	if style in ("hills", "sky", "track"):
		sky ((70, 140, 236), (176, 216, 250) if style != "track" else (250, 196, 120))
		d.ellipse ([118, 14, 140, 36], fill = (255, 236, 140))
		for cx, cy in [(30, 26), (86, 18)]:
			for dx, r in [(-8, 6), (0, 8), (8, 6)]: d.ellipse ([cx + dx - r, cy - r * 0.6, cx + dx + r, cy + r * 0.6], fill = (250, 250, 255))
		if style == "sky":
			for i in range (5): x = rnd.randint (0, w); y = rnd.randint (60, 110); d.ellipse ([x - 14, y - 5, x + 14, y + 5], fill = (240, 246, 255))
			d.polygon ([(70, 104), (96, 98), (90, 108)], fill = (230, 80, 70)); d.line ([(96, 98), (112, 92)], fill = (255, 255, 255))
		elif style == "track":
			d.polygon ([(0, 96), (w, 96), (w, h), (0, h)], fill = (70, 150, 80))
			d.polygon ([(70, 96), (90, 96), (150, h), (10, h)], fill = (70, 70, 80))
			for i in range (5): y = 100 + i * 9; d.rectangle ([79, y, 81, y + 4], fill = (250, 250, 250))
			d.rectangle ([66, 118, 94, 132], fill = (220, 50, 50)); d.rectangle ([70, 112, 90, 120], fill = (240, 240, 255))
		else:
			d.polygon ([(0, 100), (30, 70), (60, 96), (96, 60), (130, 92), (160, 72), (160, 144), (0, 144)], fill = (70, 160, 100))
			d.polygon ([(0, 112), (50, 92), (110, 112), (160, 98), (160, 144), (0, 144)], fill = (46, 128, 76))
			d.rectangle ([0, 118, w, h], fill = (150, 96, 50))
			for x in range (0, w, 8): d.rectangle ([x, 118, x + 7, 121], fill = (90, 190, 70))
			d.rectangle ([26, 106, 33, 117], fill = (220, 50, 50)); d.rectangle ([27, 102, 32, 107], fill = (250, 210, 170))
	elif style in ("space",):
		sky ((4, 6, 24), (40, 20, 80))
		for _ in range (60): x, y = rnd.randint (0, w), rnd.randint (0, h); d.point ((x, y), fill = (255, 255, rnd.randint (160, 255)))
		d.ellipse ([96, 74, 150, 128], fill = (200, 110, 70)); d.ellipse ([104, 84, 128, 98], fill = (220, 140, 90))
		d.arc ([84, 92, 162, 112], 160, 380, fill = (240, 210, 160), width = 2)
		d.polygon ([(22, 112), (44, 104), (22, 96)], fill = (220, 230, 255)); d.polygon ([(14, 104), (22, 100), (22, 108)], fill = (255, 150, 60))
	elif style in ("night",):
		sky ((10, 14, 50), (60, 50, 120))
		for _ in range (40): x, y = rnd.randint (0, w), rnd.randint (0, 70); d.point ((x, y), fill = (250, 250, 220))
		d.ellipse ([110, 12, 136, 38], fill = (250, 246, 210)); d.ellipse ([118, 10, 142, 34], fill = (16, 20, 60))
		for x0, hh in [(10, 46), (40, 64), (70, 40)]:
			d.rectangle ([x0, 144 - hh - 20, x0 + 24, 124], fill = (24, 20, 46))
			for cx in range (x0, x0 + 24, 6): d.rectangle ([cx, 144 - hh - 25, cx + 3, 144 - hh - 20], fill = (24, 20, 46))
			d.rectangle ([x0 + 10, 144 - hh - 8, x0 + 13, 144 - hh - 3], fill = (255, 210, 100))
		d.rectangle ([0, 124, w, h], fill = (30, 40, 60))
	elif style in ("sea",):
		sky ((240, 150, 90), (250, 210, 140))
		d.ellipse ([60, 52, 100, 92], fill = (255, 236, 160))
		d.rectangle ([0, 80, w, h], fill = (30, 90, 150))
		for i in range (8):
			y = 86 + i * 7
			for x in range ((i * 13) % 20, w, 22): d.line ([(x, y), (x + 8, y)], fill = (120, 180, 230))
		d.polygon ([(110, 82), (126, 66), (146, 82)], fill = (40, 70, 50)); d.line ([(126, 66), (126, 50)], fill = (90, 60, 30), width = 2)
	elif style in ("forest",):
		sky ((120, 190, 140), (220, 240, 190))
		for i in range (14):
			x = (i * 37 + 11) % 170 - 5; hh = 40 + (i * 17) % 34; c = (30 + i * 3 % 30, 100 + (i * 23) % 60, 60)
			d.polygon ([(x, 120 - hh), (x - 14, 120), (x + 14, 120)], fill = c)
		d.rectangle ([0, 118, w, h], fill = (90, 70, 40))
	elif style in ("desert",):
		sky ((240, 170, 80), (250, 226, 160))
		d.ellipse ([20, 20, 46, 46], fill = (255, 250, 210))
		d.polygon ([(70, 104), (100, 60), (130, 104)], fill = (200, 140, 70)); d.polygon ([(100, 60), (130, 104), (112, 104)], fill = (160, 100, 50))
		d.polygon ([(0, 110), (60, 96), (120, 112), (160, 100), (160, 144), (0, 144)], fill = (230, 180, 100))
	elif style in ("city",):
		sky ((60, 20, 90), (240, 110, 120))
		for i in range (11):
			x = i * 15; hh = 30 + (i * 29) % 60
			d.rectangle ([x, 124 - hh, x + 13, 124], fill = (30, 20, 50))
			for wy in range (124 - hh + 4, 120, 7):
				for wx in (x + 3, x + 8):
					if (wx + wy + i) % 3: d.rectangle ([wx, wy, wx + 2, wy + 2], fill = (255, 220, 120))
		d.rectangle ([0, 124, w, h], fill = (40, 40, 60))
	elif style in ("snow",):
		sky ((150, 190, 240), (230, 240, 255))
		d.polygon ([(0, 100), (40, 50), (80, 100)], fill = (240, 246, 255)); d.polygon ([(60, 100), (110, 40), (160, 100)], fill = (226, 236, 250))
		d.rectangle ([0, 100, w, h], fill = (250, 252, 255))
		for _ in range (40): x, y = rnd.randint (0, w), rnd.randint (0, h); d.point ((x, y), fill = (255, 255, 255))
		d.rectangle ([64, 108, 84, 122], fill = (220, 60, 60)); d.rectangle ([68, 102, 80, 108], fill = (250, 220, 190))
	elif style == "blocks":
		sky ((180, 180, 180), (120, 120, 120))
		cols = [(40, 40, 40), (90, 90, 90), (200, 200, 200)]
		for r in range (6):
			for c in range (10):
				if (r * 7 + c * 3) % 5 < 3: d.rectangle ([c * 16, 144 - (r + 1) * 12, c * 16 + 15, 144 - r * 12 - 1], fill = cols[(r + c) % 3], outline = (20, 20, 20))
	return im

def title_text (im, title, y, ink, outline, maxw = 150, size = 20, oblique = False):
	d = ImageDraw.Draw (im)
	while size > 9 and tw (title.upper (), font (size, True, oblique)) > maxw: size -= 1
	f = font (size, True, oblique); s = title.upper ()
	d.text (((im.width - tw (s, f)) // 2, y), s, font = f, fill = ink, stroke_width = 2, stroke_fill = outline)

GB_PAL = [(15, 56, 15), (48, 98, 48), (139, 172, 15), (155, 188, 15)]
def to_gb (im):
	l = np.asarray (im.convert ("L")).astype (int); q = np.clip (l * 4 // 256, 0, 3)
	return Image.fromarray (np.array (GB_PAL, "uint8")[q], "RGB")

_pics = {}
def rom_picture (sysc, title, style, seed):
	"""A ROM's 160 x 144 picture: a title screen, an N64 label, a GameCube banner."""
	k = (sysc, title)
	if k in _pics: return _pics[k]
	if sysc == "N64":					# the cartridge's label, with the name
		im = Image.new ("RGB", (PW, PH), (70, 72, 80)); d = ImageDraw.Draw (im)
		d.rounded_rectangle ([0, 0, PW - 1, PH - 1], 10, fill = (86, 88, 96))
		art = scene (style, seed, 140, 124)
		lab = Image.new ("RGB", (140, 124)); lab.paste (art, (0, 0))
		band = grad (140, 38, SYSCOL["N64"], shade (SYSCOL["N64"], 0.5)); lab.paste (band, (0, 0))
		title_text (lab, title, 9, (255, 255, 255), (20, 30, 20), maxw = 130, size = 18)
		ImageDraw.Draw (lab).rectangle ([0, 112, 140, 124], fill = (20, 20, 24))
		for i in range (6): ImageDraw.Draw (lab).rectangle ([6 + i * 22, 116, 18 + i * 22, 119], fill = SYSCOL["N64"] if i % 2 else (220, 200, 60))
		m = mask (140, 124, lambda dd, s: dd.rounded_rectangle ([0, 0, 140 * s - 1, 124 * s - 1], 6 * s, fill = 255))
		im.paste (lab, (10, 10), m)
	elif sysc == "GC":					# the disc's banner (96 x 32), shown on its own blur
		b = scene (style, seed, 288, 96).filter (ImageFilter.GaussianBlur (1))
		b = Image.blend (b, grad (288, 96, (30, 20, 60), (10, 10, 30), horizontal = True), 0.45)
		title_text (b, title, 30, (255, 240, 210), (40, 20, 10), maxw = 260, size = 34, oblique = True)
		bg = b.resize ((PW * 2, PH)).filter (ImageFilter.GaussianBlur (10)); bg = dim (bg, 120)
		im = bg.crop ((PW // 2, 0, PW // 2 + PW, PH))
		small = b.resize ((PW, 53), Image.LANCZOS); im.paste (small, (0, 45))
		ImageDraw.Draw (im).rectangle ([0, 44, PW - 1, 98], outline = (230, 220, 255))
	else:
		im = scene (style, seed)
		ink = {"GBA": (255, 236, 120), "SNES": (255, 255, 255), "NES": (255, 200, 80), "GBC": (255, 255, 255), "GB": (255, 255, 255)}.get (sysc, (255, 255, 255))
		title_text (im, title, 30, ink, (20, 20, 50))
		d = ImageDraw.Draw (im); f = font (9, True)
		if seed % 2 == 0: d.text (((PW - tw ("PRESS START", f)) // 2, 74), "PRESS START", font = f, fill = (255, 255, 255), stroke_width = 1, stroke_fill = (20, 20, 40))
		if sysc == "GB": im = to_gb (im)
		if sysc == "NES": im = im.quantize (16).convert ("RGB")
	_pics[k] = im
	return im

def game_picture (app):
	"""An Onyx game's picture: its real screenshot (screenshots/<app>.png), the frame cut off, 10:9."""
	p = os.path.join (SHOTS, app + ".png")
	if not os.path.exists (p): return None
	im = Image.open (p).convert ("RGBA")
	bg = Image.new ("RGBA", im.size, (20, 20, 30, 255)); bg.alpha_composite (im); im = bg.convert ("RGB")
	im = im.crop ((4, 28, im.width - 4, im.height - 4))
	w, h = im.size; t = PW / PH
	if w / h > t: nw = int (h * t); im = im.crop (((w - nw) // 2, 0, (w - nw) // 2 + nw, h))
	else: nh = int (w / t); im = im.crop ((0, (h - nh) // 3, w, (h - nh) // 3 + nh))
	return im.resize ((PW * 2, PH * 2), Image.LANCZOS)

# ---- the words (the shell is translated: lang/fr.txt; the existing fr.txt's words where they exist) -------------
FR = {
	"Recent": "Récents", "Onyx games": "Jeux Onyx", "Apps": "Apps", "Settings": "Réglages", "Play": "Jouer",
	"Resume": "Reprendre", "Back": "Retour", "Options": "Options", "Shelf": "Étagère", "Menu": "Menu", "Enter": "Entrer",
	"Save states": "Sauvegardes", "Slot %d": "Emplacement %d", "Empty": "Vide", "Load": "Charger", "Delete": "Supprimer",
	"Last played %s": "Dernière partie : %s", "Played %s": "Temps de jeu : %s", "%d games": "%d jeux", "Open": "Ouvrir",
	"Monday 21:04": "lundi 21:04", "6 h 41 min": "6 h 41 min", "paused": "en pause", "Choose": "Choisir", "Move": "Aller",
	"Thu 9 Oct": "jeu. 9 oct.", "running": "en cours", "Notifications": "Notifications", "Updates": "Mises à jour", "Files": "Fichiers",
	"Power": "Éteindre", "%d of %d": "%d sur %d", "Game Boy Advance": "Game Boy Advance",
	"Save here": "Sauvegarder ici", "%d save states": "%d sauvegardes", "1 save state": "1 sauvegarde",
	"no save state": "aucune sauvegarde", "Close app": "Fermer l'app", "Pin to Recent": "Épingler aux récents",
	"Mon 21:04": "lun. 21:04", "Sun 10:12": "dim. 10:12", "2 Oct 18:30": "2 oct. 18:30", "Save": "Sauvegarder", "Slot": "Emplacement", "yesterday": "hier", "Sunday": "dimanche", "Clear all": "Tout effacer",
}
class Lang:
	def __init__ (s, code = "en"): s.code = code
	def __call__ (s, w): return FR.get (w, w) if s.code == "fr" else w

# ---- the screen: a scale, logical sizes, two layouts ---------------------------------------------------------
NAVY = (8, 14, 34)
ACCENT = (61, 134, 218)				# Milk's Aqua blue: the focus, the Play button
GLOWC = (96, 170, 255)
WORD = (236, 242, 250); DIMW = (150, 168, 196); FAINT = (110, 126, 156)

class Scr:
	def __init__ (s, W, H, k, lang = "en"):
		s.W, s.H, s.k = W, H, k; s.LW, s.LH = W / k, H / k; s.compact = s.LH < 560; s.tr = Lang (lang)
		c = s.compact
		s.m = dict (
			M = 24 if c else 64,			# the side margin
			top_y = 10 if c else 22, top_h = 28 if c else 36,
			strip_y = 46 if c else 78, chip = 30 if c else 44, chip_gap = 6 if c else 10,
			row_y = 92 if c else 150,
			fw = 150 if c else 240, fh = 135 if c else 216,	# the focused tile (10:9, the pictures' shape)
			sw = 100 if c else 160, sh = 90 if c else 144,	# the others
			gap = 10 if c else 16,
			info_gap = 18 if c else 30,
			bar_h = 40 if c else 56,
			r = 8 if c else 12)
		s.fs = dict (top = 13 if c else 18, chip = 13 if c else 17, title = 17 if c else 26, sub = 12 if c else 15,
			     info = 12 if c else 16, small = 11 if c else 14, btn = 14 if c else 19, hint = 12 if c else 15)
	def L (s, v): return int (round (v * s.k))
	def f (s, key, bold = False): return font (s.L (s.fs[key] if isinstance (key, str) else key), bold)

# ---- the backdrop: the focused item's picture, blurred, darkened, full screen ---------------------------------
def backdrop (scr, pic = None, tint = None):
	W, H = scr.W, scr.H
	if pic is None:
		img = grad (W, H, (14, 24, 52), (4, 8, 20))
	else:
		small = pic.resize ((48, 27) if pic.width >= pic.height else (48, 43), Image.BILINEAR)
		small = small.crop ((0, 0, 48, 27)) if small.height > 27 else small
		small = small.filter (ImageFilter.GaussianBlur (2.2))
		img = small.resize ((W, H), Image.BICUBIC)
		img = Image.blend (img, Image.new ("RGB", (W, H), NAVY), 0.58)
	if tint: img = Image.blend (img, Image.new ("RGB", (W, H), tint), 0.15)
	# legibility: darker at the top (the function row), much darker at the bottom (the info area and hints)
	a = np.asarray (img).astype (float)
	yy = np.linspace (0, 1, H)[:, None]
	f = 1 - 0.25 * np.clip (1 - yy / 0.12, 0, 1) - 0.55 * np.clip ((yy - 0.45) / 0.55, 0, 1) ** 1.2
	xx = np.linspace (0, 1, W)[None, :]
	f = f * (1 - 0.18 * np.clip (1 - xx / 0.25, 0, 1))
	img = Image.fromarray (np.clip (a * f[..., None], 0, 255).astype ("uint8"), "RGB")
	# a faint grain of light (the current shell's motes, calmer)
	rnd = random.Random (3); ml = Image.new ("L", (W, H), 0); d = ImageDraw.Draw (ml)
	for _ in range (int (W * H / 9000)):
		x, y = rnd.uniform (0, W), rnd.uniform (0, H * 0.7); r = rnd.choice ([0.6, 0.8, 1.0]) * scr.k
		d.ellipse ([x - r, y - r, x + r, y + r], fill = rnd.randint (30, 110))
	img.paste ((200, 222, 255), (0, 0), ml)
	return img

# ---- the function row (the top) ------------------------------------------------------------------------------
FUNCS = [("bell", "Notifications"), ("download", "Updates"), ("folder", "Files"), ("gear", "Settings"), ("power", "Power")]
def top_row (img, scr, focus = None, clock = "21:07"):
	L, m = scr.L, scr.m; M = L (m["M"]); y = L (m["top_y"]); h = L (m["top_h"])
	# the gem, the home's mark
	gs = L (18 if not scr.compact else 14)
	put (img, M, y + (h - gs) // 2, mask (gs, gs, lambda d, s: g_gem (d, s, gs / 16)), (150, 200, 255))
	put (img, M, y + (h - gs) // 2, mask (gs, gs, lambda d, s: g_gem_facets (d, s, gs / 16)), (60, 120, 210), alpha = 160)
	x = M + gs + L (18)
	for i, (g, label) in enumerate (FUNCS):
		foc = focus == i
		if foc:
			m_ = Image.new ("L", img.size, 0); ImageDraw.Draw (m_).ellipse ([x - L (3), y - L (3), x + h + L (3), y + h + L (3)], fill = 200)
			img2 = add_glow (img, GLOWC, m_, L (8), 0.8); img.paste (img2)
			rrect (img, x, y, h, h, h / 2, (lighten (ACCENT, 0.15), shade (ACCENT, 0.8)))
			ring (img, x, y, h, h, h / 2, (255, 255, 255), t = L (2))
		else:
			rrect (img, x, y, h, h, h / 2, (255, 255, 255), alpha = 22)
		isz = int (h * 0.5); icon_g (img, g, x + (h - isz) // 2, y + (h - isz) // 2, isz, WORD if foc else (200, 214, 236))
		if g == "bell":				# two unread
			bd = L (16 if not scr.compact else 13); bx = x + h - bd + L (4); by = y - L (3)
			rrect (img, bx, by, bd, bd, bd / 2, (232, 86, 78)); text_c (img, bx, by, bd, bd, "2", font (L (scr.fs["small"] - 3), True), (255, 255, 255))
		if g == "download":
			bd = L (8); rrect (img, x + h - bd, y, bd, bd, bd / 2, (120, 220, 140))
		x += h + L (12 if not scr.compact else 8)
	if focus is not None:			# the focused one's name, after the row
		text_l (img, x + L (6), y, h, scr.tr (FUNCS[focus][1]), scr.f ("info", True), WORD)
	# the right: Wi-Fi, the battery, the date and the time
	f = scr.f ("top"); xr = scr.W - M
	text_r (img, xr, y, h, clock, f, WORD); xr -= tw (clock, f) + L (14)
	if not scr.compact:
		fd = scr.f ("small"); dt = scr.tr ("Thu 9 Oct"); text_r (img, xr, y, h, dt, fd, DIMW); xr -= tw (dt, fd) + L (16)
	battery (img, xr - L (24), y + (h - L (13)) // 2, scr.k, (200, 214, 236)); xr -= L (24) + L (12)
	ws = L (16); put (img, xr - ws, y + (h - ws) // 2, mask (ws, ws, lambda d, s: g_wifi (d, s, ws / 16)), (200, 214, 236))

# ---- the shelves: Recent, Onyx games, the consoles, Apps, Settings --------------------------------------------
def shelves (with_games = True):
	sh = [("recent", "Recent", "clock", None), ("onyx", "Onyx games", "gem", None)]
	for name, _, _ in SYSTEMS:
		c = SHORT.get (name, name)
		if ROMS.get (c): sh.append ((c, name, None, SYSCOL.get (c)))	# a console with no game is not shown
	sh += [("apps", "Apps", "grid", None), ("settings", "Settings", "gear", None)]
	return sh

def shelf_strip (img, scr, sel, focused, count = None):
	"""The shelves as small chips; the chosen one opens into a pill with its name (and, focused, a ring)."""
	L, m = scr.L, scr.m; M = L (m["M"]); y = L (m["strip_y"]); c = L (m["chip"]); gap = L (m["chip_gap"])
	x = M
	f = scr.f ("chip", True); fb = font (L (11 if not scr.compact else 9), True)
	for key, name, g, col in shelves ():
		chosen = key == sel
		label = scr.tr (name)
		w = c + (tw (label, f) + L (16) if chosen else 0)
		if chosen and focused:
			m_ = Image.new ("L", img.size, 0); ImageDraw.Draw (m_).rounded_rectangle ([x - L (3), y - L (3), x + w + L (3), y + c + L (3)], c / 2, fill = 220)
			img.paste (add_glow (img, GLOWC, m_, L (9), 0.9))
			rrect (img, x, y, w, c, c / 2, (lighten (ACCENT, 0.12), shade (ACCENT, 0.78)))
			ring (img, x, y, w, c, c / 2, (255, 255, 255), t = L (2))
		elif chosen:
			rrect (img, x, y, w, c, c / 2, (255, 255, 255), alpha = 46); ring (img, x, y, w, c, c / 2, (255, 255, 255), alpha = 120, t = L (1.2))
		else:
			rrect (img, x, y, w, c, c / 2, (255, 255, 255), alpha = 18)
		# the chip's face: a glyph, or the console's short name on its colour
		if g:
			isz = int (c * 0.46); icon_g (img, g, x + (c - isz) // 2, y + (c - isz) // 2, isz, WORD if chosen else (184, 200, 226))
		else:
			code = key; d = int (c * 0.74); bx = x + (c - d) // 2; by = y + (c - d) // 2
			rrect (img, bx, by, d, d, d / 2, (lighten (col, 0.1), shade (col, 0.72)), alpha = 255 if chosen else 200)
			ff = fb
			while tw (code, ff) > d - L (4) and ff.size > 6: ff = font (ff.size - 1, True)
			text_c (img, bx, by, d, d, code, ff, (255, 255, 255))
		if chosen: text_l (img, x + c + L (2), y, c, label, f, (255, 255, 255))
		x += w + gap
	if count:					# the shelf's count, at the right of the strip
		fc = scr.f ("small"); text_r (img, scr.W - M, y, c, count, fc, DIMW)

# ---- the content row -----------------------------------------------------------------------------------------
def plate (item, w, h):
	"""An app's or an applet's tile: its icon on a plate of its own colour."""
	col = icon_colour (item["icon"])
	top = mix (col, (40, 60, 110), 0.35); bot = mix (col, (6, 10, 26), 0.75)
	im = grad (w, h, top, bot)
	isz = int (min (w, h) * 0.52); ic = app_icon (item["icon"], isz)
	im.paste (ic, ((w - isz) // 2, (h - isz) // 2 - int (h * 0.02)), ic)
	return im

def tile_image (item, w, h):
	if item.get ("pic") is not None:
		return item["pic"].resize ((w, h), Image.LANCZOS)
	return plate (item, w, h)

def draw_tile (img, scr, item, x, y, w, h, focus = 0):
	"""focus: 0 none, 1 the row's chosen tile while the focus is below it, 2 focused (ring + glow)."""
	L = scr.L; r = L (scr.m["r"])
	if focus == 2:
		m_ = Image.new ("L", img.size, 0); ImageDraw.Draw (m_).rounded_rectangle ([x - L (4), y - L (4), x + w + L (4), y + h + L (4)], r + L (4), fill = 255)
		img.paste (add_glow (img, GLOWC, m_, L (14), 0.75))
	else:					# a soft shadow
		m_ = Image.new ("L", img.size, 0); ImageDraw.Draw (m_).rounded_rectangle ([x, y + L (6), x + w, y + h + L (6)], r, fill = 170)
		sh = m_.filter (ImageFilter.GaussianBlur (L (8))); img.paste ((0, 0, 0), (0, 0), sh)
	pic = tile_image (item, w, h)
	mm = mask (w, h, lambda d, s: d.rounded_rectangle ([0, 0, w * s - 1, h * s - 1], r * s, fill = 255))
	img.paste (pic, (int (x), int (y)), mm)
	if focus == 2: ring (img, x - L (3), y - L (3), w + L (6), h + L (6), r + L (3), (255, 255, 255), t = L (3))
	elif focus == 1: ring (img, x, y, w, h, r, (255, 255, 255), alpha = 150, t = L (1.5))
	else: ring (img, x, y, w, h, r, (255, 255, 255), alpha = 40, t = L (1))
	# the corner marks: the console's badge (on a mixed shelf), running / paused
	fb = font (L (10 if not scr.compact else 8), True)
	if item.get ("badge"):
		b = item["badge"]; bw = tw (b, fb) + L (10); bh = L (17 if not scr.compact else 14)
		rrect (img, x + L (6), y + h - bh - L (6), bw, bh, L (4), SYSCOL.get (b, (60, 60, 70)), alpha = 235)
		text_c (img, x + L (6), y + h - bh - L (6) - L (1), bw, bh, b, fb, (255, 255, 255))
	if item.get ("state"):
		st = scr.tr (item["state"]); bw = tw (st, fb) + L (20); bh = L (17 if not scr.compact else 14)
		bx = x + w - bw - L (6); by = y + L (6)
		rrect (img, bx, by, bw, bh, bh / 2, (8, 14, 30), alpha = 220)
		dd = L (6); rrect (img, bx + L (6), by + (bh - dd) // 2, dd, dd, dd / 2, (120, 220, 140) if item["state"] == "running" else (250, 200, 80))
		text_l (img, bx + L (15), by - L (1), bh, st, fb, WORD)

def draw_divider (img, scr, label, colour, x, y, w, h):
	"""A category's mark in the Apps shelf's row: a slim upright card with the name turned."""
	L = scr.L
	rrect (img, x, y, w, h, L (8), (255, 255, 255), alpha = 14); ring (img, x, y, w, h, L (8), (255, 255, 255), alpha = 40)
	dd = L (8); rrect (img, x + (w - dd) // 2, y + L (10), dd, dd, dd / 2, colour)
	f = scr.f ("small", True); tl = Image.new ("L", (h, w), 0)
	lab = ellipsize (label, f, h - L (34))
	ImageDraw.Draw (tl).text ((h - L (26) - tw (lab, f), (w - f.size) // 2 - L (1)), lab, font = f, fill = 255)
	put (img, x, y, tl.rotate (90, expand = True), (200, 214, 236))

def content_row (img, scr, items, fi, row_focused, title = None, sub = None, counter = None):
	"""The big row: the focused tile at the left, larger; its title beside it, over the smaller ones."""
	L, m = scr.L, scr.m; M = L (m["M"]); y = L (m["row_y"])
	fw, fh, sw, sh, gap = L (m["fw"]), L (m["fh"]), L (m["sw"]), L (m["sh"]), L (m["gap"])
	x = M
	if fi > 0:						# more on the left
		cs = L (16); put (img, M // 2 - cs // 2, y + fh // 2 - cs // 2, mask (cs, cs, lambda d, s: g_chev (d, s, cs / 16, "left")), WORD, alpha = 170)
	order = items[fi:]
	for j, it in enumerate (order):
		if it.get ("div"):
			dw = L (40 if not scr.compact else 28)
			draw_divider (img, scr, it["div"], it["colour"], x, y + fh - sh, dw, sh); x += dw + gap; continue
		if j == 0:
			draw_tile (img, scr, it, x, y, fw, fh, 2 if row_focused else 1); x += fw + gap
			tx = x + L (4)
		else:
			if x > scr.W: break
			draw_tile (img, scr, it, x, y + fh - sh, sw, sh); x += sw + gap
	# the focused one's name and line, beside it
	it = items[fi]
	ft = scr.f ("title", True); fs = scr.f ("sub")
	room = fh - sh
	ty = y + (room - (L (scr.fs["title"]) + L (6) + L (scr.fs["sub"]) + L (4))) // 2 - L (4)
	tmax = scr.W - tx - M - (L (90) if counter else 0)
	text (img, tx, ty, ellipsize (title or it["title"], ft, tmax), ft, (255, 255, 255))
	if sub or it.get ("sub"): text (img, tx + L (1), ty + L (scr.fs["title"]) + L (8), ellipsize (sub or it.get ("sub"), fs, scr.W - tx - M), fs, DIMW)
	if counter: text_r (img, scr.W - M, ty + L (4), L (scr.fs["title"]) - L (4), counter, scr.f ("small"), DIMW)
	return y + fh

# ---- buttons and hints ---------------------------------------------------------------------------------------
def button (img, scr, x, y, label, g = None, focus = False, primary = False, h = None, w = None):
	L = scr.L; h = h or L (52 if not scr.compact else 34); f = scr.f ("btn", True)
	gs = int (h * 0.36)
	w = w or (tw (label, f) + (gs + L (12) if g else 0) + L (44 if not scr.compact else 28))
	if focus:
		m_ = Image.new ("L", img.size, 0); ImageDraw.Draw (m_).rounded_rectangle ([x - L (3), y - L (3), x + w + L (3), y + h + L (3)], h / 2, fill = 230)
		img.paste (add_glow (img, GLOWC, m_, L (12), 0.9))
		rrect (img, x, y, w, h, h / 2, (lighten (ACCENT, 0.18), shade (ACCENT, 0.8)))
		ring (img, x, y, w, h, h / 2, (255, 255, 255), t = L (2.5))
	elif primary:
		rrect (img, x, y, w, h, h / 2, (255, 255, 255), alpha = 52); ring (img, x, y, w, h, h / 2, (255, 255, 255), alpha = 110, t = L (1.2))
	else:
		rrect (img, x, y, w, h, h / 2, (255, 255, 255), alpha = 20); ring (img, x, y, w, h, h / 2, (255, 255, 255), alpha = 60, t = L (1))
	cx = x + (w - (tw (label, f) + (gs + L (12) if g else 0))) // 2
	if g: icon_g (img, g, cx, y + (h - gs) // 2, gs, (255, 255, 255)); cx += gs + L (12)
	text_l (img, cx, y - L (1), h, label, f, (255, 255, 255) if (focus or primary) else (214, 224, 240))
	return w

PADC = {"A": (94, 156, 255), "B": (255, 106, 106), "X": (180, 140, 240), "Y": (240, 190, 80)}
def hints (img, scr, items):
	"""The bottom line: a dark band, the buttons that work and what they do (the current shell's pills)."""
	L = scr.L; bh = L (scr.m["bar_h"]); y0 = scr.H - bh
	band = Image.new ("RGB", (scr.W, bh), (4, 8, 20)); img.paste (Image.blend (img.crop ((0, y0, scr.W, scr.H)), band, 0.72), (0, y0))
	hline (img, 0, y0, scr.W, (140, 170, 230), alpha = 50)
	f = scr.f ("hint"); fb = font (L (scr.fs["hint"] - 2), True); d = L (24 if not scr.compact else 19)
	x = L (scr.m["M"]); y = y0 + (bh - d) // 2
	for key, word in items:
		if key in PADC:
			rrect (img, x, y, d, d, d / 2, (14, 20, 36)); ring (img, x, y, d, d, d / 2, PADC[key], t = L (1.6))
			text_c (img, x, y - L (1), d, d, key, fb, PADC[key]); x += d + L (8)
		elif key in ("lr", "ud", "dpad"):		# the d-pad: a small cross, the used arms lit
			u = d / 16
			def fn (dd, s, key = key):
				for (ax, ay, aw, ah), arm in [((5.5, 0.5, 5, 6), "u"), ((5.5, 9.5, 5, 6), "d"), ((0.5, 5.5, 6, 5), "l"), ((9.5, 5.5, 6, 5), "r")]:
					lit = (arm in "lr" and key in ("lr", "dpad")) or (arm in "ud" and key in ("ud", "dpad"))
					dd.rounded_rectangle ([ax * u * s, ay * u * s, (ax + aw) * u * s, (ay + ah) * u * s], u * s, fill = 255 if lit else 90)
				dd.rectangle ([5.5 * u * s, 5.5 * u * s, 10.5 * u * s, 10.5 * u * s], fill = 160)
			glyph (img, x, y, d, d, fn, (220, 230, 246)); x += d + L (8)
		else:
			for part in key.split ("/"):
				pw = tw (part, fb) + L (14)
				rrect (img, x, y + L (1), pw, d - L (2), L (6), (14, 20, 36)); ring (img, x, y + L (1), pw, d - L (2), L (6), (170, 184, 210), t = L (1.3))
				text_c (img, x, y, pw, d, part, fb, (220, 230, 246)); x += pw + L (4)
			x += L (4)
		text_l (img, x, y, d, scr.tr (word), f, (214, 224, 240)); x += tw (scr.tr (word), f) + L (26 if not scr.compact else 16)

# ---- the shelves' items -------------------------------------------------------------------------------------------
def rom_items (sysc, badge = False):
	out = []
	for i, (t, style, last, played, slots) in enumerate (ROMS[sysc]):
		out.append (dict (kind = "rom", sys = sysc, title = t, pic = rom_picture (sysc, t, style, i * 7 + len (t)),
				  last = last, played = played, slots = slots, style = style, seed = i * 7 + len (t),
				  badge = sysc if badge else None))
	return out

ONYX_LINE = {"doom": "the id Software classic, ported", "tetris": "falling blocks", "pinball": "a table with multiball",
	     "critters": "lead them home", "invaders": "the arcade classic", "solitaire": "Klondike", "arkanoid": "bricks and a paddle",
	     "2048": "slide and merge", "freecell": "the card game", "minesweeper": "the grid of mines", "pipes": "connect the pipes",
	     "pong": "two paddles", "same": "clear the colours", "snake": "grow, don't bite", "sokoban": "push the crates",
	     "life": "Conway's cells"}
def onyx_items ():
	first = ["tetris", "pinball", "critters", "invaders", "solitaire", "doom", "arkanoid"]
	rest = [a for a in apps_in ("Games") if a not in first and a != "gamelib"]
	out = []
	for a in first + rest:
		if a not in APPS: continue
		out.append (dict (kind = "app", app = a, icon = a, title = app_name (a), pic = game_picture (a), sub = "Onyx  -  " + ONYX_LINE.get (a, "")))
	return out

CATCOL = {"Productivity": (240, 160, 60), "Internet": (61, 134, 218), "Graphics": (76, 182, 83), "Multimedia": (170, 100, 200),
	  "Programming": (40, 170, 170), "System": (130, 140, 160)}
KIND = {"archiver": "archives", "tinycalc": "sums", "calendar": "planner", "cardfile": "card database", "clock": "alarms, timer",
	"graphcalc": "plots", "ledger": "accounts", "letters": "word processor", "notes": "quick notes", "pdf": "PDF reader",
	"rtfview": "RTF reader", "slides": "presentations", "sheet": "spreadsheet", "tinypad": "plain text",
	"courier": "HTTP client", "irc": "chat", "jet": "web browser", "lisa": "AI assistant", "mail": "mail client", "telegram": "messenger",
	"media": "music and films", "koton": "music studio", "fmtracker": "tracker", "paint": "painting", "photos": "photo library",
	"terminal": "the shell", "fileviewer": "files", "disks": "volumes", "taskman": "processes"}
def app_items ():
	out = []
	for cat in ["Productivity", "Internet", "Graphics", "Multimedia", "Programming", "System"]:
		out.append (dict (div = cat, colour = CATCOL[cat]))
		for a in apps_in (cat):
			out.append (dict (kind = "app", app = a, icon = a, title = app_name (a), cat = cat,
					  sub = cat + ("  -  " + KIND[a] if a in KIND else ""), state = "running" if a in ("letters", "media", "telegram") else None))
	return out

APPLETS = [("modeconf", "Mode"), ("displayconf", "Display"), ("soundconf", "Sound"), ("keyconf", "Keyboard & Mouse"),
	   ("langconf", "Language & Region"), ("padconf", "Gamepad"), ("wpaconf", "Wi-Fi"), ("pkgman", "Packages"), ("control", "About")]
def applet_help ():
	h = {}
	d = os.path.join (SD, "apps", "control.app", "applets")
	for f in os.listdir (d):
		m = read_kv (os.path.join (d, f))
		if "target" in m: h[m["target"]] = m.get ("text", "")
	h["control"] = "Onyx's version, the kernel, the card, the memory"
	return h
HELP = applet_help ()
def applet_items ():
	return [dict (kind = "applet", app = a, icon = a, title = n, sub = HELP.get (a, "")) for a, n in APPLETS]

def recent_items ():
	r = []
	def rom (sysc, t, state = None):
		it = [x for x in rom_items (sysc, badge = True) if x["title"] == t][0]; it["state"] = state; return it
	def app (a, state = None, sub = ""):
		return dict (kind = "app", app = a, icon = a, title = app_name (a), pic = None, state = state, sub = sub)
	r = [rom ("GBA", "Star Courier", "paused"), app ("letters", "running", "Productivity  -  word processor"),
	     rom ("SNES", "Moon Garden"), app ("media", "running", "Multimedia  -  music and films"), rom ("N64", "Sky Fortress"),
	     dict (kind = "app", app = "tetris", icon = "tetris", title = "Tetris", pic = game_picture ("tetris"), sub = "Onyx"),
	     app ("jet"), rom ("GB", "Block Drop"), app ("terminal")]
	return r

# ---- the info area -------------------------------------------------------------------------------------------------
def slot_picture (it, i, w, h):
	"""A save state's picture: the game as it was -- its world (the title screen's scene, no title), a
	different place in it each time."""
	sc = scene (it.get ("style", "hills"), it.get ("seed", 1) + 5 * i)
	box_ = [(0, 14, 120, 122), (40, 36, 160, 144), (16, 0, 136, 108)][i % 3]
	pic = sc.crop (box_)
	d = ImageDraw.Draw (pic)					# the hero and a few coins: a game in progress
	hx = [30, 70, 52][i % 3]; hy = pic.height - 28
	if it.get ("style") != "hills": d.rectangle ([hx, hy, hx + 6, hy + 10], fill = (220, 50, 50)); d.rectangle ([hx + 1, hy - 5, hx + 5, hy], fill = (250, 210, 170))
	for j in range (3 + i): d.ellipse ([4 + j * 8, 4, 9 + j * 8, 9], fill = (255, 220, 60))
	if it.get ("sys") == "GB": pic = to_gb (pic)
	return pic.resize ((w, h), Image.NEAREST if w >= 2 * pic.width else Image.LANCZOS)

def slot_cards (img, scr, x, y, it, focus_slot = None, n = 3, label = True, new_slot = False):
	"""The save states: up to n cards, the picture at the time, its slot and date; the rest empty."""
	L = scr.L; tr = scr.tr
	tw_ = L (136 if not scr.compact else 84); th = int (tw_ * 0.9); gap = L (16 if not scr.compact else 10)
	dates = ["Mon 21:04", "Sun 10:12", "2 Oct 18:30"]
	for i in range (n):
		sx = x + i * (tw_ + gap); used = i < it["slots"]
		foc = focus_slot == i
		if foc:
			m_ = Image.new ("L", img.size, 0); ImageDraw.Draw (m_).rounded_rectangle ([sx - L (4), y - L (4), sx + tw_ + L (4), y + th + L (4)], L (12), fill = 255)
			img.paste (add_glow (img, GLOWC, m_, L (12), 0.8))
		if used:
			pic = slot_picture (it, i, tw_, th)
			mm = mask (tw_, th, lambda d, s: d.rounded_rectangle ([0, 0, tw_ * s - 1, th * s - 1], L (8) * s, fill = 255))
			img.paste (pic, (sx, y), mm)
		else:
			rrect (img, sx, y, tw_, th, L (8), (255, 255, 255), alpha = 12)
			fe = scr.f ("small", foc); text_c (img, sx, y, tw_, th, tr ("Save here") if foc else tr ("Empty"), fe, (255, 255, 255) if foc else FAINT)
		ring (img, sx - (L (3) if foc else 0), y - (L (3) if foc else 0), tw_ + (L (6) if foc else 0), th + (L (6) if foc else 0), L (8) + (L (3) if foc else 0),
		      (255, 255, 255), alpha = 255 if foc else 70, t = L (3) if foc else L (1))
		if label:
			fl = scr.f ("small", True); fd = font (L (scr.fs["small"] - 1))
			text_c (img, sx, y + th + L (6), tw_, L (18), tr ("Slot %d") % (i + 1), fl, WORD if foc else (200, 214, 236))
			if used: text_c (img, sx, y + th + L (6) + L (scr.fs["small"] + 4), tw_, L (16), tr (dates[i]), fd, DIMW)
	return tw_, th

def info_rom (img, scr, it, y0, level = "row", focus = 0):
	"""A ROM: its console, its file, when last played; Play; its save states. focus (level info): 0 Play, 1.. a slot."""
	L, m, tr = scr.L, scr.m, scr.tr; M = L (m["M"])
	fi, fs = scr.f ("info"), scr.f ("small")
	c = it["sys"]; fb = font (L (11 if not scr.compact else 9), True)
	bw = tw (c, fb) + L (12); bh = L (20 if not scr.compact else 16)
	rrect (img, M, y0 + L (1), bw, bh, L (5), SYSCOL[c]); text_c (img, M, y0, bw, bh, c, fb, (255, 255, 255))
	text_l (img, M + bw + L (10), y0, bh, SYSNAME[c], scr.f ("info", True), WORD)
	lh = L (scr.fs["info"] + 10)
	colw = L (440 if not scr.compact else 270)
	text (img, M, y0 + bh + L (8), ellipsize (rom_file (c, it["title"]), fs, colw), fs, DIMW)
	ns = it["slots"]; st = (tr ("%d save states") % ns) if ns != 1 else tr ("1 save state")
	text (img, M, y0 + bh + L (8) + L (scr.fs["small"] + 8), ellipsize (tr ("Played %s") % it["played"] + "   -   " + (st if ns else tr ("no save state")), fs, colw), fs, DIMW)
	by = y0 + bh + L (8) + 2 * L (scr.fs["small"] + 8) + L (14 if not scr.compact else 6)
	w = button (img, scr, M, by, tr ("Play"), "play", focus = level == "info" and focus == 0, primary = True)
	button (img, scr, M + w + L (14), by, "", "dots", focus = level == "info" and focus == -1, w = L (52 if not scr.compact else 34))
	# the save states, the right column
	sx = M + colw + L (20 if not scr.compact else 4)
	ft = scr.f ("info", True); text (img, sx, y0, tr ("Save states"), ft, WORD)
	n = 3 if not scr.compact else 3
	slot_cards (img, scr, sx, y0 + L (scr.fs["info"] + 14), it, focus_slot = (focus - 1) if level == "info" and focus > 0 else None, n = n)

def info_onyx_game (img, scr, it, y0, level = "row", focus = 0):
	"""One of Onyx's own games: what it is, where it lives, Play."""
	L, tr = scr.L, scr.tr; M = L (scr.m["M"])
	put (img, M, y0 + L (2), mask (L (16), L (16), lambda d, s: g_gem (d, s, L (16) / 16)), (150, 200, 255))
	text (img, M + L (24), y0, "Onyx game  -  " + ONYX_LINE.get (it["app"], ""), scr.f ("info", True), WORD)
	fs = scr.f ("small")
	text (img, M, y0 + L (32), "SD:/apps/%s.app" % it["app"], fs, DIMW)
	text (img, M, y0 + L (32) + L (scr.fs["small"] + 8), "Last played 30 Sep   -   best score 12 840", fs, DIMW)
	by = y0 + L (32) + 2 * L (scr.fs["small"] + 8) + L (14)
	w = button (img, scr, M, by, tr ("Play"), "play", focus = level == "info" and focus == 0, primary = True)
	button (img, scr, M + w + L (14), by, "", "dots", w = L (52))

def info_app (img, scr, it, y0, level = "row", focus = 0):
	L, m, tr = scr.L, scr.m, scr.tr; M = L (m["M"])
	cat = it.get ("cat", "Games"); col = CATCOL.get (cat, (232, 86, 78))
	dd = L (10); rrect (img, M, y0 + L (6), dd, dd, dd / 2, col)
	text (img, M + L (18), y0, tr (cat) + ("  -  " + KIND[it["app"]] if it["app"] in KIND else ""), scr.f ("info", True), WORD)
	desc = {"letters": "The word processor: styled RTF documents, tables of contents, mail merge, printing.",
		"media": "Music, films and radio: the library of SD:/music and SD:/videos."}.get (it["app"], "")
	fs = scr.f ("small"); yy = y0 + L (scr.fs["info"] + 12)
	for line in wrap (desc, fs, L (520))[:2]: text (img, M, yy, line, fs, DIMW); yy += L (scr.fs["small"] + 6)
	if it["app"] in ("letters",):
		ka = font (L (scr.fs["small"]), True); text (img, M, yy + L (4), "Keyboard recommended", ka, (240, 190, 80)); yy += L (scr.fs["small"] + 8)
	by = yy + L (14)
	x = M
	labels = [("Resume" if it.get ("state") == "running" else "Open", "play", True), ("Close app", "close", False), ("Pin to Recent", None, False)]
	for i, (lab, g, prim) in enumerate (labels):
		x += button (img, scr, x, by, tr (lab), g, focus = level == "info" and focus == i, primary = prim) + L (14)
	if it.get ("state") == "running":		# its live picture (EL_OP_SHOT), the right column
		p = os.path.join (SHOTS, it["app"] + ".png")
		if os.path.exists (p):
			im = Image.open (p).convert ("RGB"); im = im.crop ((4, 28, im.width - 4, im.height - 4))
			sx = M + L (640); tw_ = scr.W - M - sx; th = int (tw_ * im.height / im.width); th = min (th, scr.H - L (scr.m["bar_h"]) - L (20) - (y0 + L (26)))
			im = im.resize ((tw_, int (tw_ * im.height / im.width)), Image.LANCZOS).crop ((0, 0, tw_, th))
			text (img, sx, y0, "Running", scr.f ("info", True), WORD)
			mm = mask (tw_, th, lambda d, s: d.rounded_rectangle ([0, 0, tw_ * s - 1, th * s - 1], L (8) * s, fill = 255))
			img.paste (im, (sx, y0 + L (26)), mm); ring (img, sx, y0 + L (26), tw_, th, L (8), (255, 255, 255), alpha = 70)

def info_applet (img, scr, it, y0):
	"""An applet: its help line and its page's first rows, as the console Control Panel shows them (section 7.6)."""
	L, tr = scr.L, scr.tr; M = L (scr.m["M"])
	fs = scr.f ("small")
	text (img, M, y0, "Now  -  its page in the console Control Panel", scr.f ("info", True), WORD)
	rows = {"soundconf": [("Play on", "HDMI (the TV)"), ("Volume", "bar7"), ("Mute", "Off"), ("Test sound", "Play")]}.get (it["app"], [])
	y = y0 + L (36); rw = L (560); rh = L (40)
	for i, (k, v) in enumerate (rows):
		rrect (img, M, y, rw, rh - L (6), L (8), (255, 255, 255), alpha = 16)
		text_l (img, M + L (16), y, rh - L (6), k, scr.f ("info"), (214, 224, 240))
		if v.startswith ("bar"):
			n = int (v[3:]); bx = M + rw - L (16) - 10 * L (16)
			for j in range (10): rrect (img, bx + j * L (16), y + (rh - L (6)) // 2 - L (5), L (12), L (10), L (3), ACCENT if j < n else (70, 84, 110))
		else: text_r (img, M + rw - L (16), y, rh - L (6), v, scr.f ("info", True), WORD)
		y += rh
	button (img, scr, M + rw + L (40), y0 + L (36), tr ("Open"), "gear", primary = True)
	text (img, M + rw + L (40), y0 + L (36) + L (66), "SD:/etc/sound.ini", fs, FAINT)

# ---- the screens -----------------------------------------------------------------------------------------------------
def home (W, H, k, shelf = "GBA", fi = 0, level = "row", focus = 0, lang = "en", func_focus = None):
	scr = Scr (W, H, k, lang); tr = scr.tr
	if shelf in ROMS: items = rom_items (shelf)
	elif shelf == "recent": items = recent_items ()
	elif shelf == "onyx": items = onyx_items ()
	elif shelf == "apps": items = app_items ()
	else: items = applet_items ()
	it = items[fi]
	pic = it.get ("pic")
	img = backdrop (scr, pic if pic is not None else (plate (it, 160, 90) if shelf != "settings" else None), tint = SYSCOL.get (shelf))
	top_row (img, scr, focus = func_focus)
	real = [x for x in items if not x.get ("div")]
	idx = real.index (it) + 1
	count = (tr ("%d games") % len (real)) if shelf in ROMS else None
	shelf_strip (img, scr, shelf, level == "strip", count = None)
	sub = it.get ("sub")
	if it.get ("kind") == "rom": sub = (SYSNAME[it["sys"]] + "   " + tr ("Last played %s") % tr (it["last"])) if shelf == "recent" else (tr ("Last played %s") % tr (it["last"]))
	rb = content_row (img, scr, items, fi, level == "row", sub = sub, counter = tr ("%d of %d") % (idx, len (real)))
	y0 = rb + L_ (scr, scr.m["info_gap"])
	if it.get ("kind") == "rom": info_rom (img, scr, it, y0, level, focus)
	elif it.get ("kind") == "applet": info_applet (img, scr, it, y0)
	elif APPS.get (it.get ("app"), {}).get ("category") == "Games": info_onyx_game (img, scr, it, y0, level, focus)
	else: info_app (img, scr, it, y0, level, focus)
	# the hints
	if level == "func": return img			# (home_function draws its own)
	if level == "row":
		hs = [("A", "Play" if it.get ("kind") == "rom" or APPS.get (it.get ("app"), {}).get ("category") == "Games" else "Resume" if it.get ("state") else "Open"), ("B", "Back"), ("Y", "Options"), ("L1/R1", "Shelf"), ("Home", "Menu")]
	elif level == "info":
		hs = [("A", "Play" if focus == 0 else "Load"), ("B", "Back")] + ([("Y", "Delete")] if focus > 0 else [("Y", "Options")]) + [("lr", "Move"), ("Home", "Menu")]
	elif level == "strip":
		hs = [("A", "Enter"), ("lr", "Shelf"), ("Home", "Menu")]
	else:
		hs = [("A", "Open"), ("B", "Back"), ("lr", "Move")]
	if scr.compact: hs = [h for h in hs if h[0] not in ("Y",)]
	hints (img, scr, hs)
	return img
def L_ (scr, v): return scr.L (v)

def notif_panel (img, scr):
	"""Up from the shelves, then A on the bell: the notifications drop under the function row."""
	L = scr.L; M = L (scr.m["M"]); x = M + L (36); y = L (scr.m["top_y"] + scr.m["top_h"] + 40); w = L (460)
	notes = [("pkgman", "Updates", "3 packages: snesemu 1.4, media 2.2, kernel 0.97", "10 min"),
		 ("telegram", "Telegram", "Anna: are we still on for the game night?", "1 h")]
	h = L (20) + len (notes) * L (76) - L (4)
	m_ = Image.new ("L", img.size, 0); ImageDraw.Draw (m_).rounded_rectangle ([x, y + L (8), x + w, y + h + L (8)], L (14), fill = 200)
	img.paste ((0, 0, 0), (0, 0), m_.filter (ImageFilter.GaussianBlur (L (14))))
	rrect (img, x, y, w, h, L (14), ((26, 40, 74), (14, 22, 46))); ring (img, x, y, w, h, L (14), (255, 255, 255), alpha = 60)
	yy = y + L (16)
	for i, (a, t, body, when) in enumerate (notes):
		if i == 0:
			rrect (img, x + L (8), yy - L (4), w - L (16), L (70), L (10), (lighten (ACCENT, 0.1), shade (ACCENT, 0.75)))
			ring (img, x + L (8), yy - L (4), w - L (16), L (70), L (10), (255, 255, 255), t = L (2))
		draw_icon (img, a, x + L (20), yy + L (6), L (40))
		text (img, x + L (74), yy + L (4), t, scr.f ("info", True), (255, 255, 255))
		text_r (img, x + w - L (22), yy + L (2), L (20), when, scr.f ("small"), DIMW if i else WORD)
		text (img, x + L (74), yy + L (30), ellipsize (body, scr.f ("small"), w - L (96)), scr.f ("small"), WORD if i == 0 else DIMW)
		yy += L (76)

def home_function (W = 1280, H = 720, k = 1):
	scr = Scr (W, H, k)
	img = home (W, H, k, "GBA", 0, level = "func", func_focus = 0)
	img = dim (img, 60)
	top_row (img, scr, focus = 0)
	notif_panel (img, scr)
	hints (img, scr, [("A", "Open"), ("B", "Back"), ("X", "Clear all"), ("ud", "Choose")])
	return img

# ---- the menu over a running game ----------------------------------------------------------------------------
def gameplay (w, h, world = False):
	"""A made-up side-scroller (the emulator's picture), pixel art."""
	g = Image.new ("RGB", (240, 160), (96, 168, 248)); d = ImageDraw.Draw (g)
	for cx, cy, r in [(40, 30, 10), (54, 27, 12), (70, 31, 9), (170, 20, 9), (184, 18, 12), (198, 22, 8)]: d.ellipse ([cx - r, cy - r // 2, cx + r, cy + r // 2], fill = (248, 248, 255))
	d.polygon ([(0, 120), (40, 80), (75, 112), (120, 66), (170, 116), (240, 88), (240, 160), (0, 160)], fill = (70, 150, 110))
	d.rectangle ([0, 128, 240, 160], fill = (150, 96, 50)); [d.rectangle ([x, 128, x + 7, 131], fill = (90, 190, 70)) for x in range (0, 240, 8)]
	for x in (90, 98, 106): d.rectangle ([x, 86, x + 7, 93], fill = (230, 160, 40)); d.rectangle ([x + 1, 87, x + 6, 92], outline = (130, 70, 10))
	d.rectangle ([44, 112, 51, 127], fill = (220, 50, 50)); d.rectangle ([45, 107, 50, 113], fill = (250, 210, 170))
	d.rectangle ([150, 118, 161, 127], fill = (120, 60, 160)); d.point ([(153, 121), (158, 121)], fill = (255, 255, 255))
	for i in range (5): d.rectangle ([6 + i * 8, 6, 11 + i * 8, 11], fill = (255, 230, 60))
	d.text ((180, 4), "x 12", font = font (9, True), fill = (255, 255, 255))
	if world:					# a wider stretch of the level: the frame and its mirror
		wd = Image.new ("RGB", (480, 160)); wd.paste (g, (0, 0)); wd.paste (g.transpose (Image.FLIP_LEFT_RIGHT), (240, 0)); return wd
	gh = h; gw = int (gh * 1.5)
	out = Image.new ("RGB", (w, h), (0, 0, 0)); out.paste (g.resize ((gw, gh), Image.NEAREST), ((w - gw) // 2, 0))
	return out

def quick_menu (W = 1280, H = 720, k = 1, lang = "en"):
	scr = Scr (W, H, k, lang); L, tr = scr.L, scr.tr; M = L (scr.m["M"])
	img = gameplay (W, H)
	img = dim (img.filter (ImageFilter.GaussianBlur (L (3))), 165)
	# the panel at the left: the game's own section, then the system's
	px, py, pw = L (40), L (40), L (400); ph = H - py - L (scr.m["bar_h"]) - L (24)
	m_ = Image.new ("L", img.size, 0); ImageDraw.Draw (m_).rounded_rectangle ([px, py + L (10), px + pw, py + ph + L (10)], L (18), fill = 200)
	img.paste ((0, 0, 0), (0, 0), m_.filter (ImageFilter.GaussianBlur (L (16))))
	rrect (img, px, py, pw, ph, L (18), ((24, 38, 72), (10, 16, 36))); ring (img, px, py, pw, ph, L (18), (255, 255, 255), alpha = 60)
	pic = rom_picture ("GBA", "Star Courier", "hills", 12)
	tw_ = L (64); th = int (tw_ * 0.9)
	mm = mask (tw_, th, lambda d, s: d.rounded_rectangle ([0, 0, tw_ * s - 1, th * s - 1], L (6) * s, fill = 255)); img.paste (pic.resize ((tw_, th), Image.LANCZOS), (px + L (20), py + L (20)), mm)
	text (img, px + L (98), py + L (22), "Star Courier", scr.f ("info", True), (255, 255, 255))
	fb = font (L (10), True); bw = tw ("GBA", fb) + L (10)
	rrect (img, px + L (98), py + L (50), bw, L (17), L (4), SYSCOL["GBA"]); text_c (img, px + L (98), py + L (49), bw, L (17), "GBA", fb, (255, 255, 255))
	text (img, px + L (98) + bw + L (8), py + L (50), tr ("paused") + "  -  6 h 41 min", scr.f ("small"), DIMW)
	items = [("play", "Resume", None), ("save", "Save state", ">"), ("load", "Load state", ">"), ("camera", "Screenshot", None),
		 ("menu", "Emulator menus", ">"), None, ("home", "Home", None), ("app:letters", "Letters", None), ("app:media", "Media Player", None),
		 ("close", "Close game", None), ("gear", "Settings", None), ("power", "Shut Down...", None)]
	FRW = {"Save state": "Sauvegarder l'état", "Load state": "Charger un état", "Screenshot": "Capture d'écran", "Emulator menus": "Menus de l'émulateur",
	       "Home": "Accueil", "Close game": "Fermer le jeu", "Settings": "Réglages", "Shut Down...": "Éteindre...", "Resume": "Reprendre", "Media Player": "Lecteur multimédia"}
	y = py + L (104); rh = L (40); sel = 1
	for i, itm in enumerate (items):
		if itm is None:
			hline (img, px + L (20), y + L (8), pw - L (40), (255, 255, 255), alpha = 40)
			text (img, px + L (22), y + L (14), "System" if lang == "en" else "Système", scr.f ("small", True), FAINT); y += L (38); continue
		g, lab, more = itm
		lab = FRW.get (lab, lab) if lang == "fr" else lab
		foc = i == sel
		if foc:
			m2 = Image.new ("L", img.size, 0); ImageDraw.Draw (m2).rounded_rectangle ([px + L (10), y, px + pw - L (10), y + rh - L (4)], L (10), fill = 230)
			img.paste (add_glow (img, GLOWC, m2, L (10), 0.7))
			rrect (img, px + L (12), y, pw - L (24), rh - L (4), L (10), (lighten (ACCENT, 0.15), shade (ACCENT, 0.78)))
			ring (img, px + L (12), y, pw - L (24), rh - L (4), L (10), (255, 255, 255), t = L (2))
		isz = L (18)
		if g.startswith ("app:"): draw_icon (img, g[4:], px + L (26), y + (rh - L (4) - L (22)) // 2, L (22))
		else: icon_g (img, g, px + L (28), y + (rh - L (4) - isz) // 2, isz, (255, 255, 255) if foc else (190, 206, 232))
		text_l (img, px + L (62), y - L (2), rh, lab, scr.f ("info", foc), (255, 255, 255) if foc else (214, 224, 240))
		if g.startswith ("app:"): text_r (img, px + pw - L (28), y - L (2), rh, tr ("running"), scr.f ("small"), FAINT)
		if more: cs = L (14); put (img, px + pw - L (24) - cs, y + (rh - L (4) - cs) // 2, mask (cs, cs, lambda d, s: g_chev (d, s, cs / 16, "right")), WORD if foc else FAINT)
		y += rh
	# the slots, beside: Save state chosen
	sx = px + pw + L (28); sy = py + L (104) + rh - L (10); sw_ = W - sx - L (40); sh_ = L (268)
	rrect (img, sx, sy, sw_, sh_, L (18), ((24, 38, 72), (10, 16, 36)), alpha = 235); ring (img, sx, sy, sw_, sh_, L (18), (255, 255, 255), alpha = 60)
	text (img, sx + L (24), sy + L (20), "Save to a slot" if lang == "en" else "Sauvegarder dans un emplacement", scr.f ("info", True), WORD)
	text (img, sx + L (24), sy + L (46), "Saving over a slot asks first; the picture is the game's at that moment." if lang == "en" else
	      "Écraser un emplacement demande confirmation ; l'image est celle du jeu à cet instant.", scr.f ("small"), DIMW)
	it = dict (kind = "rom", sys = "GBA", style = "hills", seed = 12, slots = 2)
	slot_cards (img, scr, sx + L (24), sy + L (82), it, focus_slot = 2, n = 4)
	text_r (img, W - L (40), py + L (14), L (24), "21:07", scr.f ("top"), WORD)
	hints (img, scr, [("A", "Save" if lang == "en" else "Sauvegarder"), ("B", "Resume"), ("lr", "Slot" if lang == "en" else "Emplacement"), ("ud", "Choose")])
	return img

# ---- the overview sheet -------------------------------------------------------------------------------------------------
def overview (shots):
	"""Six screens with captions, and the navigation model drawn as arrows between them."""
	tw_, th = 560, 315; gx, gy = 70, 92
	W = 3 * tw_ + 2 * gx + 2 * 60; H = 130 + 2 * (th + 150) + 10 + 40 + 4 * 46 + 40
	img = grad (W, H, (16, 24, 44), (8, 12, 24))
	put (img, 60, 38, mask (26, 26, lambda d, s: g_gem (d, s, 26 / 16)), (150, 200, 255))
	text (img, 100, 34, "Onyx console mode v2 -- the shelves (consoles, Onyx games, apps, settings) and one big row", font (26, True), WORD)
	text (img, 100, 72, "Up / Down move between the four levels; Left / Right along a row; L1 / R1 change the shelf from anywhere; Home opens the menu.", font (17), DIMW)
	pos = []
	for i, (im, cap, sub) in enumerate (shots):
		r, c = divmod (i, 3)
		x = 60 + c * (tw_ + gx); y = 130 + r * (th + 150)
		t = im.resize ((tw_, th), Image.LANCZOS)
		img.paste (t, (x, y)); ring (img, x - 1, y - 1, tw_ + 2, th + 2, 4, (140, 170, 230), alpha = 120)
		text (img, x, y + th + 12, cap, font (18, True), WORD)
		for j, line in enumerate (wrap (sub, font (15), tw_)[:3]): text (img, x, y + th + 40 + j * 21, line, font (15), DIMW)
		pos.append ((x, y))
	def arrow (x0, y0, x1, y1, label, col = (120, 190, 255)):
		d = ImageDraw.Draw (img); d.line ([x0, y0, x1, y1], fill = col, width = 4)
		a = math.atan2 (y1 - y0, x1 - x0); s = 14
		d.polygon ([(x1, y1), (x1 - s * math.cos (a - 0.45), y1 - s * math.sin (a - 0.45)), (x1 - s * math.cos (a + 0.45), y1 - s * math.sin (a + 0.45))], fill = col)
		f = font (15, True); lw = tw (label, f) + 14; mx, my = (x0 + x1) / 2, (y0 + y1) / 2
		rrect (img, mx - lw / 2, my - 13, lw, 26, 8, (10, 16, 34)); ring (img, mx - lw / 2, my - 13, lw, 26, 8, col)
		text_c (img, mx - lw / 2, my - 13, lw, 26, label, f, WORD)
	# 0 function  1 home  2 details / 3 apps 4 settings 5 menu
	(x0, y0), (x1, y1), (x2, y2), (x3, y3), (x4, y4), (x5, y5) = pos
	arrow (x1 - 6, y1 + th * 0.35, x0 + tw_ + 6, y0 + th * 0.35, "Up")
	arrow (x0 + tw_ + 6, y0 + th * 0.65, x1 - 6, y1 + th * 0.65, "B / Down")
	arrow (x1 + tw_ + 6, y1 + th * 0.35, x2 - 6, y2 + th * 0.35, "Down")
	arrow (x2 - 6, y2 + th * 0.65, x1 + tw_ + 6, y1 + th * 0.65, "Up / B")
	for (x, y), lab in [((x3, y3), "from the GBA shelf: R1 x 4 (or Right along the shelves)"), ((x4, y4), "from the GBA shelf: R1 x 5"),
			    ((x5, y5), "from any game or app: Home")]:
		f = font (15, True); lw = tw (lab, f) + 40
		rrect (img, x, y - 38, lw, 28, 9, (10, 16, 34)); ring (img, x, y - 38, lw, 28, 9, (120, 190, 255))
		put (img, x + 12, y - 31, mask (14, 14, lambda d, s_: g_chev (d, s_, 14 / 16, "down")), (120, 190, 255))
		text_l (img, x + 32, y - 38, 28, lab, f, WORD)
	# the levels, schematic, at the bottom
	by = 130 + 2 * (th + 150) + 10
	text (img, 60, by, "The four levels of the home (top to bottom) -- the focus is always on exactly one of them:", font (18, True), WORD)
	levels = [("Function row", "Notifications, Updates, Files, Settings, Power  -  Left / Right"),
		  ("Shelves", "Recent, Onyx games, the consoles that have games, Apps, Settings  -  Left / Right, or L1 / R1 from anywhere"),
		  ("Content row", "the shelf's games or apps, the focused one big at the left  -  Left / Right; A plays / opens"),
		  ("Details", "Play, Options, the save-state slots (or Open, Close app...)  -  Left / Right; A acts")]
	for i, (n, d_) in enumerate (levels):
		y = by + 40 + i * 46; w = 1700 - i * 0
		rrect (img, 60, y, 230, 36, 10, (lighten (ACCENT, 0.1), shade (ACCENT, 0.75)) if i == 2 else (40, 56, 92))
		text_c (img, 60, y, 230, 36, n, font (16, True), (255, 255, 255))
		text_l (img, 310, y, 36, d_, font (16), (210, 222, 242))
		if i < 3:
			put (img, 168, y + 36, mask (14, 10, lambda d, s: d.polygon ([(0, 0), (14 * s, 0), (7 * s, 10 * s)], fill = 255)), (120, 190, 255))
	return img

# ---- main -------------------------------------------------------------------------------------------------------------
def save (img, name):
	p = os.path.join (OUT, name); img.save (p); print ("wrote", os.path.relpath (p, ROOT), img.size)

def main ():
	os.makedirs (OUT, exist_ok = True)
	a = home (1280, 720, 1, "GBA", 0, "row");			save (a, "console-v2-home.png")
	b = home (1920, 1080, 1.5, "N64", 0, "row");		save (b, "console-v2-home-1080.png")
	b2 = home (640, 480, 1, "GB", 0, "row");			save (b2, "console-v2-home-640.png")
	c = home (1280, 720, 1, "GBA", 0, "info", focus = 0);	save (c, "console-v2-game.png")
	c2 = home (1280, 720, 1, "GBA", 0, "info", focus = 2);	save (c2, "console-v2-game-slot.png")
	r = home (1280, 720, 1, "recent", 0, "row");		save (r, "console-v2-recent.png")
	o = home (1280, 720, 1, "onyx", 0, "row");			save (o, "console-v2-onyx.png")
	ap = home (1280, 720, 1, "apps", [i for i, x in enumerate (app_items ()) if x.get ("app") == "letters"][0], "row");		save (ap, "console-v2-apps.png")
	st = home (1280, 720, 1, "settings", 2, "row");		save (st, "console-v2-settings.png")
	sp = home (1280, 720, 1, "SNES", 0, "strip");		save (sp, "console-v2-shelves.png")
	fn = home_function ();					save (fn, "console-v2-function.png")
	q = quick_menu ();						save (q, "console-v2-quickmenu.png")
	fr = home (1280, 720, 1, "GBA", 0, "info", focus = 0, lang = "fr"); save (fr, "console-v2-game-fr.png")
	qf = quick_menu (lang = "fr");				save (qf, "console-v2-quickmenu-fr.png")
	ov = overview ([(fn, "Up: the function row", "Notifications (two unread), Updates, Files, Settings, Power; the time, Wi-Fi and the battery at the right."),
			(a, "Home: a console's shelf", "Game Boy Advance chosen among the shelves; its games in one row, Star Courier focused and big, its picture blurred behind."),
			(c, "Down: the game's details", "Play focused; its console, file, time played; its save states as slots (Right to reach them, A loads one)."),
			(ap, "R1 x 4: the Apps shelf", "Every app, sorted by category (slim dividers); Letters running: Resume, Close app, Pin to Recent; its live picture."),
			(st, "R1 x 5: the Settings shelf", "The console's applets; Sound focused, its page's first rows shown; A opens the console Control Panel."),
			(q, "Home in a game: the menu", "Resume, Save state (the slots beside), Load state, Screenshot, the emulator's menus; then Home, the running apps, Close, Settings, Shut Down.")])
	save (ov, "console-v2-overview.png")

if __name__ == "__main__":
	main ()
