#!/usr/bin/env python3
# Copyright (c) 2026 the Onyx authors -- MIT licence (see docs/LICENSING.md).
"""mockup_compact.py -- the mock-ups of the compact shell study (docs/COMPACT-SHELL-STUDY.md): an Onyx
desktop for small screens -- netbooks, the Zaurus-like clamshells and slates, the Pi handhelds -- in
place of today's windowed one (Elegant's policy + the menu bar + the dock). A design analysis only:
nothing here is built.

    python3 tools/screenshot/mockup_compact.py      -> docs/compact-shell/mockups/*.png

Self-contained (PIL + numpy): the drawing helpers are those of mockup_cde_modern.py (anti-aliased masks,
the grey profile `tone`, rounded boxes, gradients), copied so that the script does not need the Circle
sources render.py reads. It uses the card's real fonts (sdcard/res/fonts: DejaVu, the UI's face), the
apps' real icons (sdcard/apps/<app>.app/icon.bmp) and their real categories (app.txt), the Milk theme's
colours (user/Kits/uikit/theme.cpp: light greys, Aqua's blue, a silver dock, OS X's beads -- the user's
choice for this study), and a few real screenshots (screenshots/*.png, recoloured to Milk by `milkify`)
for the task switcher's thumbnails, Ledger's rail and the fixed-size window.

The modes and the concepts (the study's sections 6-8):
  Pocket   -- the compact mode: A's launcher + today's global menu bar + B's split view; ONE adaptive
              layout in logical units x a scale, reflowing by the logical size and aspect
              (800 x 480, 640 x 480, 480 x 800 at 1.5x, 240 x 320 at 2x)
  Console  -- the gamepad mode, the PlayStation 2 system browser's mood (640 x 480)
  A "Tabs"    -- Qtopia / Zaurus: tabbed launcher, a taskbar (640 x 480 clamshell) -- studied
  B "Netbook" -- Ubuntu Netbook Remix: sidebar launcher, windows as top tabs (1280 x 720) -- later
"""
import math, os, random
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname (os.path.abspath (__file__))
ROOT = os.path.abspath (os.path.join (HERE, "..", ".."))
SD = os.path.join (ROOT, "sdcard")
SHOTS = os.path.join (ROOT, "screenshots")
OUT = os.path.join (ROOT, "docs", "compact-shell", "mockups")

# ---- fonts: the card's DejaVu (the UI's face), else the system's -----------------------------------------
_FD = [os.path.join (SD, "res", "fonts"), "/usr/share/fonts/truetype/dejavu"]
_fc = {}
def font (size, bold = False, mono = False):
	size = int (round (size)); k = (size, bold, mono)
	if k not in _fc:
		name = ("DejaVuSansMono" if mono else "DejaVuSans") + ("-Bold" if bold else "") + ".ttf"
		for d in _FD:
			p = os.path.join (d, name)
			if os.path.exists (p): _fc[k] = ImageFont.truetype (p, size); break
		else: _fc[k] = ImageFont.load_default ()
	return _fc[k]

# ---- the palette: the Milk theme ------------------------------------------------------------------------
def hexc (v): return ((v >> 16) & 255, (v >> 8) & 255, v & 255)
# The Milk theme (the user's choice for the compact shell): user/Kits/uikit/theme.cpp's Milk palette --
# light greys, Aqua's blue, the frames 0xE2E2E4, a silver dock; the title bars a light gradient ending on
# the window's colour, OS X's beads. (The other themes of theme.txt would apply the same way.)
P = dict (
	face = hexc (0xE4E4E4), accent = hexc (0x3D86DA), dock = hexc (0xD9DDE3),
	inactive = hexc (0xE2E2E4), frame = hexc (0xE2E2E4),
	bar = (248, 248, 248), bar2 = (226, 226, 228), ink = (24, 24, 26), ink2 = (102, 102, 108),
	field = (255, 255, 255), paper = (252, 252, 252),
	desk = (34, 58, 85), desk2 = (22, 38, 60),				# the wallpaper (screenshots/milk.png)
	term = (26, 58, 70), term_fg = (212, 234, 240), cursor = (120, 214, 236),	# (the Terminal's own: TERM_BG)
	red = (232, 86, 78), amber = (240, 180, 58), green = (76, 182, 83),
	silver = ((250, 250, 251), (226, 228, 232)))
# One colour per category: a dot on its tab (Milk keeps the tabs silver; the colour says the category).
CATS = [("Recent", (140, 146, 158)), ("Productivity", (240, 160, 60)), ("Internet", (61, 134, 218)),
	("Graphics", (76, 182, 83)), ("Multimedia", (170, 100, 200)), ("Games", (232, 86, 78)),
	("Programming", (40, 170, 170)), ("System", (110, 110, 120)), ("Settings", (150, 130, 100))]
CATC = dict (CATS)

# ---- anti-aliased drawing (mockup_cde_modern.py's) --------------------------------------------------------
S = 4
def lighten (c, k): return tuple (int (v + (255 - v) * k) for v in c[:3])
def shade (c, k): return tuple (int (v * k) for v in c[:3])
def mix (a, b, t): return tuple (int (a[i] + (b[i] - a[i]) * t) for i in range (3))
def lum (c): return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]
def fg_on (c): return (20, 20, 22) if lum (c) > 150 else (255, 255, 255)
def tone (c, level): return lighten (c, (level - 128) / 127) if level >= 128 else shade (c, level / 128)

def mask (w, h, fn):
	w, h = int (round (w)), int (round (h))
	m = Image.new ("L", (max (1, w * S), max (1, h * S)), 0); fn (ImageDraw.Draw (m), S)
	return m.resize ((max (1, w), max (1, h)), Image.BOX)
def grad (w, h, top, bottom, horizontal = False):
	w, h = int (w), int (h)
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
def ring (img, x, y, w, h, r, colour, t = 1, alpha = 255, corners = None):
	x, y, w, h = int (x), int (y), int (w), int (h)
	m = mask (w, h, lambda d, s: d.rounded_rectangle ([0, 0, w * s - 1, h * s - 1], r * s, outline = 255, width = int (t * s), corners = corners))
	put (img, x, y, m, colour, alpha)
def box (img, x, y, w, h, colour, alpha = 255):
	x, y = int (x), int (y)
	put (img, x, y, Image.new ("L", (max (1, int (w)), max (1, int (h))), 255), colour, alpha)
def hline (img, x, y, w, colour, alpha = 255): box (img, x, y, w, 1, colour, alpha)
def vline (img, x, y, h, colour, alpha = 255): box (img, x, y, 1, h, colour, alpha)
def glyph (img, x, y, w, h, fn, colour, alpha = 255): put (img, x, y, mask (int (w), int (h), fn), colour, alpha)
def dim (img, alpha = 110, colour = (10, 16, 26)):
	ov = Image.new ("RGB", img.size, colour); return Image.blend (img, ov, alpha / 255)

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

# ---- the apps: names, categories, icons ------------------------------------------------------------------
def app_meta ():
	apps = {}
	for d in sorted (os.listdir (os.path.join (SD, "apps"))):
		if not d.endswith (".app"): continue
		m = {}
		try:
			for line in open (os.path.join (SD, "apps", d, "app.txt")):
				if "=" in line and not line.lstrip ().startswith ("#"):
					k, v = [s.strip () for s in line.split ("=", 1)]; m[k] = v
		except OSError: continue
		apps[d[:-4]] = (m.get ("name", d[:-4]), m.get ("category", ""))
	return apps
APPS = app_meta ()
def apps_in (cat): return sorted ([a for a, (n, c) in APPS.items () if c == cat], key = lambda a: APPS[a][0].lower ())
def app_name (a): return APPS.get (a, (a, ""))[0]
SETTINGS = ["theme", "displayconf", "dockconf", "soundconf", "preloadconf", "keyconf", "langconf", "printconf",
	    "padconf", "wpaconf", "pkgman", "config"]
NAMES = {"displayconf": "Display", "dockconf": "Panel", "soundconf": "Sound", "preloadconf": "Preload",
	 "keyconf": "Keyboard", "langconf": "Language", "printconf": "Printers", "padconf": "Gamepad",
	 "wpaconf": "Wi-Fi", "pkgman": "Packages", "config": "App Settings", "theme": "Theme"}

_icons = {}
def app_icon (name, size):
	"""apps/<name>.app/icon.bmp (40 x 40, magenta = see-through) scaled smoothly, premultiplied."""
	k = (name, size)
	if k not in _icons:
		p = os.path.join (SD, "apps", name + ".app", "icon.bmp")
		if not os.path.exists (p): p = os.path.join (SD, "apps", "terminal.app", "icon.bmp")
		a = np.array (Image.open (p).convert ("RGB"))
		alpha = np.where ((a[..., 0] == 255) & (a[..., 1] == 0) & (a[..., 2] == 255), 0, 255).astype ("uint8")
		im = Image.fromarray (np.dstack ([a, alpha]), "RGBA")
		_icons[k] = im.convert ("RGBa").resize ((size, size), Image.LANCZOS).convert ("RGBA") if size != 40 else im
	return _icons[k]
def draw_icon (img, name, x, y, size): size = int (size); ic = app_icon (name, size); img.paste (ic, (int (x), int (y)), ic)

def milkify (im):
	"""A CDE-themed screenshot in Milk's colours: the warm greys (the beige face) made neutral and lighter,
	the teal accent made Aqua's blue -- so that the real apps' pictures match the mock-ups."""
	a = np.asarray (im.convert ("RGB")).astype (float)
	r, g, b = a[..., 0], a[..., 1], a[..., 2]
	mx = a.max (-1); mn = a.min (-1); sat = (mx - mn) / np.maximum (mx, 1)
	l = 0.3 * r + 0.59 * g + 0.11 * b
	warm = (sat < 0.22) & (r >= b)
	grey = np.clip (l * 1.16, 0, 255)
	out = a.copy ()
	for i in range (3): out[..., i] = np.where (warm, grey, out[..., i])
	teal = (sat > 0.3) & (g > r) & (b > r) & (np.abs (g - b) < 50)
	k = l / 120.0
	for i, c in enumerate ((61, 134, 218)): out[..., i] = np.where (teal, np.clip (c * k, 0, 255), out[..., i])
	return Image.fromarray (out.astype ("uint8"), "RGB")

def shot (name, crop_frame = True):
	"""A real screenshot (screenshots/<name>.png), its window frame cut off (title 28, border 4)."""
	im = Image.open (os.path.join (SHOTS, name + ".png")).convert ("RGBA")
	bg = Image.new ("RGBA", im.size, (208, 194, 186, 255)); bg.alpha_composite (im); im = bg.convert ("RGB")
	if crop_frame: im = im.crop ((4, 28, im.width - 4, im.height - 4))
	return milkify (im)

# ---- small glyphs (coverage masks from geometry, as uikit's) ---------------------------------------------
def g_wifi (d, s, w = 16, h = 14):
	cx, cy = w * s / 2, h * s - 2 * s
	for r in (12, 8, 4):
		d.arc ([cx - r * s, cy - r * s, cx + r * s, cy + r * s], 225, 315, fill = 255, width = int (1.8 * s))
	d.ellipse ([cx - 1.6 * s, cy - 1.6 * s, cx + 1.6 * s, cy + 1.6 * s], fill = 255)
def g_speaker (d, s):
	d.polygon ([(1 * s, 5 * s), (4 * s, 5 * s), (8 * s, 1.5 * s), (8 * s, 12.5 * s), (4 * s, 9 * s), (1 * s, 9 * s)], fill = 255)
	d.arc ([5 * s, 3 * s, 13 * s, 11 * s], 300, 60, fill = 255, width = int (1.5 * s))
	d.arc ([5 * s, 0 * s, 16 * s, 14 * s], 300, 60, fill = 255, width = int (1.5 * s))
def g_bell (d, s):
	d.pieslice ([2 * s, 1 * s, 12 * s, 12 * s], 180, 360, fill = 255)
	d.rectangle ([2 * s, 6.5 * s, 12 * s, 10 * s], fill = 255)
	d.polygon ([(0.5 * s, 11 * s), (13.5 * s, 11 * s), (12 * s, 9.5 * s), (2 * s, 9.5 * s)], fill = 255)
	d.ellipse ([5.5 * s, 11 * s, 8.5 * s, 14 * s], fill = 255)
def g_gem (d, s, k = 1):				# Onyx: a cut stone
	u = s * k
	d.polygon ([(4 * u, 1 * u), (12 * u, 1 * u), (16 * u, 5.5 * u), (8 * u, 15 * u), (0 * u, 5.5 * u)], fill = 255)
def g_gem_facets (d, s, k = 1):
	u = s * k
	d.line ([(0, 5.5 * u), (16 * u, 5.5 * u)], fill = 0, width = int (1 * u))
	d.line ([(4 * u, 1 * u), (6 * u, 5.5 * u), (8 * u, 15 * u), (10 * u, 5.5 * u), (12 * u, 1 * u)], fill = 0, width = int (1 * u))
def g_search (d, s, k = 1):
	u = s * k
	d.ellipse ([1 * u, 1 * u, 10 * u, 10 * u], outline = 255, width = int (2 * u))
	d.line ([(8.5 * u, 8.5 * u), (13 * u, 13 * u)], fill = 255, width = int (2.4 * u))
def g_gear (d, s, k = 1):
	u = s * k; c = 8 * u
	for i in range (8):
		a = i * math.pi / 4
		d.line ([(c, c), (c + 7.5 * u * math.cos (a), c + 7.5 * u * math.sin (a))], fill = 255, width = int (3 * u))
	d.ellipse ([c - 5.5 * u, c - 5.5 * u, c + 5.5 * u, c + 5.5 * u], fill = 255)
	d.ellipse ([c - 2.3 * u, c - 2.3 * u, c + 2.3 * u, c + 2.3 * u], fill = 0)
def g_power (d, s, k = 1):
	u = s * k
	d.arc ([2 * u, 3 * u, 14 * u, 15 * u], 300, 240, fill = 255, width = int (2 * u))
	d.line ([8 * u, 1 * u, 8 * u, 8 * u], fill = 255, width = int (2 * u))
def g_lock (d, s, k = 1):
	u = s * k
	d.arc ([4 * u, 1 * u, 12 * u, 11 * u], 180, 360, fill = 255, width = int (2 * u))
	d.line ([4 * u + u, 6 * u, 4 * u + u, 8 * u], fill = 255, width = int (2 * u)); d.line ([11 * u, 6 * u, 11 * u, 8 * u], fill = 255, width = int (2 * u))
	d.rounded_rectangle ([2 * u, 7 * u, 14 * u, 16 * u], 2 * u, fill = 255)
def g_grid (d, s, k = 1):				# home / all apps
	u = s * k
	for i in range (3):
		for j in range (3): d.rounded_rectangle ([(1 + i * 5) * u, (1 + j * 5) * u, (4 + i * 5) * u, (4 + j * 5) * u], u, fill = 255)
def g_tasks (d, s, k = 1):				# the switcher: two cards
	u = s * k
	d.rounded_rectangle ([1 * u, 4 * u, 11 * u, 15 * u], 2 * u, outline = 255, width = int (1.8 * u))
	d.rounded_rectangle ([5 * u, 1 * u, 15 * u, 11 * u], 2 * u, fill = 255)
def g_menu (d, s, k = 1):				# the hamburger
	u = s * k
	for i in range (3): d.rounded_rectangle ([1 * u, (2 + i * 5) * u, 15 * u, (4 + i * 5) * u], u, fill = 255)
def g_kbd (d, s, k = 1):
	u = s * k
	d.rounded_rectangle ([0.5 * u, 3 * u, 15.5 * u, 13 * u], 2 * u, outline = 255, width = int (1.6 * u))
	for i in range (4): d.rectangle ([(3 + i * 3) * u, 5.5 * u, (4.4 + i * 3) * u, 6.9 * u], fill = 255)
	for i in range (4): d.rectangle ([(3 + i * 3) * u, 8 * u, (4.4 + i * 3) * u, 9.4 * u], fill = 255)
	d.rectangle ([4.5 * u, 10.6 * u, 11.5 * u, 11.6 * u], fill = 255)
def g_rotate (d, s, k = 1):
	u = s * k
	d.arc ([2 * u, 2 * u, 14 * u, 14 * u], 40, 320, fill = 255, width = int (2 * u))
	d.polygon ([(14.5 * u, 1.5 * u), (14.5 * u, 7 * u), (9 * u, 7 * u)], fill = 255)
def g_moon (d, s, k = 1):
	u = s * k
	d.ellipse ([1 * u, 1 * u, 15 * u, 15 * u], fill = 255); d.ellipse ([5 * u, -1 * u, 18 * u, 11 * u], fill = 0)
def g_sun (d, s, k = 1):
	u = s * k; c = 8 * u
	for i in range (8):
		a = i * math.pi / 4
		d.line ([(c + 5 * u * math.cos (a), c + 5 * u * math.sin (a)), (c + 7.5 * u * math.cos (a), c + 7.5 * u * math.sin (a))], fill = 255, width = int (1.6 * u))
	d.ellipse ([c - 3.5 * u, c - 3.5 * u, c + 3.5 * u, c + 3.5 * u], fill = 255)
def g_close (d, s, k = 1):
	u = s * k
	d.line ([2 * u, 2 * u, 8 * u, 8 * u], fill = 255, width = int (1.6 * u)); d.line ([8 * u, 2 * u, 2 * u, 8 * u], fill = 255, width = int (1.6 * u))
def g_chev (d, s, k = 1, dirn = "down"):
	u = s * k
	pts = {"down": [(1, 3), (5, 7), (9, 3)], "right": [(3, 1), (7, 5), (3, 9)], "left": [(7, 1), (3, 5), (7, 9)], "up": [(1, 7), (5, 3), (9, 7)]}[dirn]
	d.line ([(a * u, b * u) for a, b in pts], fill = 255, width = int (1.8 * u), joint = "curve")
def g_split (d, s, k = 1):
	u = s * k
	d.rounded_rectangle ([1 * u, 2 * u, 15 * u, 14 * u], 2 * u, outline = 255, width = int (1.6 * u)); d.line ([8 * u, 2 * u, 8 * u, 14 * u], fill = 255, width = int (1.6 * u))
def g_dnd (d, s, k = 1):			# do not disturb: a bell struck through
	g_bell (d, s * k); d.line ([0, 0, 14 * s * k, 14 * s * k], fill = 0, width = int (3 * s * k)); d.line ([0, 0, 14 * s * k, 14 * s * k], fill = 255, width = int (1.4 * s * k))

def bead (img, x, y, d, c):
	"""Milk's title button: OS X's glossy bead (uikit's uk_bead)."""
	rrect (img, x, y, d, d, d / 2, (lighten (c, 0.25), shade (c, 0.85))); ring (img, x, y, d, d, d / 2, shade (c, 0.55), alpha = 200)
	rrect (img, x + d * 0.22, y + 1, d * 0.56, d * 0.38, d * 0.19, (255, 255, 255), alpha = 120)

def battery (img, x, y, pct, colour, k = 1, charging = False):
	ring (img, x, y + 1 * k, 20 * k, 11 * k, 2 * k, colour, t = 1.3 * k)
	box (img, x + 20 * k, y + 4 * k, 2 * k, 5 * k, colour)
	fw = int (16 * k * pct / 100)
	rrect (img, x + 2 * k, y + 3 * k, max (1, fw), 7 * k, 1 * k, colour if pct > 20 else P["red"])

# ---- the wallpaper: today's navy with Voronoi's soft cells -----------------------------------------------
def wallpaper (w, h, seed = 7):
	img = grad (w, h, P["desk"], P["desk2"])
	rnd = random.Random (seed)
	ov = Image.new ("L", (w, h), 0); d = ImageDraw.Draw (ov)
	for _ in range (int (w * h / 26000) + 6):
		r = rnd.randint (int (h * 0.12), int (h * 0.42)); cx = rnd.randint (-r // 2, w + r // 2); cy = rnd.randint (-r // 2, h + r // 2)
		d.ellipse ([cx - r, cy - r, cx + r, cy + r], fill = rnd.randint (6, 20))
	ov = ov.filter (ImageFilter.GaussianBlur (h / 14))
	img.paste (Image.new ("RGB", (w, h), (120, 150, 190)), (0, 0), ov)
	return img

# ---- the status bar (today's menu bar, the Onyx button made Home) ----------------------------------------
BAR = 26
def statusbar (img, w, title = None, menus = (), open_menu = None, home = False, k = 1, compact = False,
	       pct = 78, notif = True, tray = True, tasks = 3, clock = "12:34", x0 = 0, y0 = 0, hot = None, lite = False):
	"""home: the launcher is shown (the Onyx button lit). compact: the menus folded into one button.
	Returns the x of each menu title (for the drop-down)."""
	h = int (BAR * k)
	img.paste (grad (w, h, P["bar"], P["bar2"]), (x0, y0)); hline (img, x0, y0 + h - 1, w, shade (P["bar2"], 0.72))
	f, fb = font (12 * k), font (12 * k, True)
	x = x0 + 4 * k
	# the Onyx button: the gem + "Onyx"; lit on the launcher
	bw = (22 if compact else 64) * k
	if home or hot == "onyx": rrect (img, x, y0 + 3 * k, bw, h - 6 * k, 5 * k, (lighten (P["accent"], 0.15), shade (P["accent"], 0.9)))
	gc = (255, 255, 255) if (home or hot == "onyx") else (40, 44, 52)
	gm = mask (14 * k, 14 * k, lambda d, s: g_gem (d, s, 0.85 * k))
	put (img, x + 4 * k, y0 + 6 * k, gm, gc)
	if not compact: text_l (img, x + 22 * k, y0, h, "Onyx", fb, gc)
	x += bw + 6 * k
	pos = {}
	if title:
		text_l (img, x, y0, h, title, fb, P["ink"]); x += tw (title, fb) + 14 * k
	if compact and menus:
		pos["Menu"] = x
		lit = open_menu is not None
		if lit: rrect (img, x - 4 * k, y0 + 3 * k, 26 * k, h - 6 * k, 5 * k, P["accent"])
		glyph (img, x + 1 * k, y0 + 5 * k, 16 * k, 16 * k, lambda d, s: g_menu (d, s, k), (255, 255, 255) if lit else P["ink"])
	else:
		for m in menus:
			mw = tw (m, f)
			pos[m] = x - 8 * k
			if m == open_menu:
				rrect (img, x - 7 * k, y0 + 3 * k, mw + 14 * k, h - 6 * k, 5 * k, P["accent"]); text_l (img, x, y0, h, m, f, (255, 255, 255))
			else: text_l (img, x, y0, h, m, f, P["ink"])
			x += mw + 16 * k
	# the right side: the tray, the notifications, Wi-Fi, the volume, the battery, the time
	xr = x0 + w - 6 * k
	fcl = font (12 * k, True)
	if hot == "clock": rrect (img, xr - 166 * k, y0 + 3 * k, 170 * k, h - 6 * k, 5 * k, P["accent"])
	ic = (255, 255, 255) if hot == "clock" else (40, 44, 52)
	text_r (img, xr, y0, h, clock, fcl, ic); xr -= tw (clock, fcl) + 8 * k
	if pct is not None:
		if not lite: sp = "%d%%" % pct; text_r (img, xr, y0, h, sp, f, ic); xr -= tw (sp, f) + 3 * k
		battery (img, xr - 22 * k, y0 + 7 * k, pct, ic, k); xr -= 30 * k
	if lite: notif = tray = False
	else: glyph (img, xr - 16 * k, y0 + 6 * k, 16 * k, 14 * k, lambda d, s: g_speaker (d, s * k), ic); xr -= 24 * k
	if not (lite and w / k < 300): glyph (img, xr - 16 * k, y0 + 6 * k, 16 * k, 14 * k, lambda d, s: g_wifi (d, s * k), ic); xr -= 24 * k
	if notif:
		glyph (img, xr - 14 * k, y0 + 6 * k, 14 * k, 14 * k, lambda d, s: g_bell (d, s * k), ic)
		rrect (img, xr - 4 * k, y0 + 4 * k, 7 * k, 7 * k, 3.5 * k, (226, 80, 64)); xr -= 24 * k
	if tray:
		rrect (img, xr - 15 * k, y0 + 5 * k, 15 * k, 15 * k, 7.5 * k, (40, 150, 220))
		rrect (img, xr - 11 * k, y0 + 9 * k, 7 * k, 7 * k, 3.5 * k, (255, 255, 255)); xr -= 24 * k
	return pos

# ---- menus (today's drop-down: light, rounded, accent selection) -----------------------------------------
def dropdown (img, x, y, items, sel = None, w = None, k = 1, sub = None):
	"""items: (label, shortcut) or "-" ; sub: the index of an item with a submenu (a chevron)."""
	f = font (12 * k); rh = 26 * k
	w = w or max ([tw (it[0], f) + tw (it[1], f) for it in items if it != "-"]) + 70 * k
	h = sum (9 * k if it == "-" else rh for it in items) + 10 * k
	rrect (img, x, y, w, h, 7 * k, (249, 249, 251)); ring (img, x, y, w, h, 7 * k, (60, 60, 64), alpha = 120)
	cy = y + 5 * k
	for i, it in enumerate (items):
		if it == "-":
			hline (img, x + 10 * k, cy + 4 * k, w - 20 * k, (206, 206, 208)); cy += 9 * k; continue
		fg = P["ink"]
		if i == sel: rrect (img, x + 5 * k, cy, w - 10 * k, rh, 5 * k, P["accent"]); fg = (255, 255, 255)
		grey = it[0].startswith ("~")
		lab = it[0].lstrip ("~")
		text_l (img, x + 14 * k, cy, rh, lab, f, (150, 146, 142) if grey else fg)
		if it[1]: text_r (img, x + w - 14 * k, cy, rh, it[1], f, fg if i == sel else (120, 116, 112))
		if sub == i: glyph (img, x + w - 18 * k, cy + 8 * k, 10 * k, 10 * k, lambda d, s: g_chev (d, s, k, "right"), fg)
		cy += rh
	return w, h

# ---- launcher pieces --------------------------------------------------------------------------------------
def tabs_row (img, x, y, w, cats, active, k = 1, h = 32, font_size = 12, scroll = True, bottom_colour = None):
	"""The category tabs: silver, a dot of the category's colour; the active one white, joining the panel below."""
	f = font (font_size * k, True); h = h * k
	cx = x
	out = []
	for name, col in cats:
		tw_ = tw (name, f) + 36 * k
		if cx + tw_ > x + w - (26 * k if scroll else 0): break
		on = name == active
		top, bot = ((255, 255, 255), (250, 250, 251)) if on else ((236, 237, 240), (208, 210, 216))
		th = h if on else h - 4 * k
		rrect (img, cx, y + h - th, tw_, th + 8 * k, 8 * k, (top, bot), corners = (True, True, False, False))
		if not on: ring (img, cx, y + h - th, tw_, th + 8 * k, 8 * k, (0, 0, 0), alpha = 50, corners = (True, True, False, False))
		if on: box (img, cx, y + h - 1, tw_, 9 * k, (250, 250, 251))
		rrect (img, cx + 10 * k, y + h - th + (th - 8 * k) // 2, 8 * k, 8 * k, 4 * k, col)
		text_c (img, cx + 9 * k, y + h - th, tw_ - 9 * k, th, name, f, P["ink"] if on else (60, 60, 66))
		out.append ((name, cx, tw_))
		cx += tw_ + 3 * k
	if scroll:
		rrect (img, x + w - 22 * k, y + 6 * k, 22 * k, h - 8 * k, 6 * k, (255, 255, 255), alpha = 46)
		glyph (img, x + w - 16 * k, y + 6 * k + (h - 8 * k - 10 * k) // 2, 10 * k, 10 * k, lambda d, s: g_chev (d, s, k, "right"), (230, 236, 244))
	return out

def tile (img, x, y, w, h, app, label, focus = False, running = False, k = 1, isz = 48, fg = (20, 20, 22), small = False):
	if focus:
		rrect (img, x + 2 * k, y + 2 * k, w - 4 * k, h - 4 * k, 9 * k, (lighten (P["accent"], 0.2), shade (P["accent"], 0.95)))
		ring (img, x, y, w, h, 11 * k, (255, 255, 255), t = 2 * k, alpha = 230)
	draw_icon (img, app, x + (w - isz) // 2, y + 8 * k, isz)
	f = font ((10 if small else 11) * k, focus)
	lab = ellipsize (label, f, w - 6 * k)
	text_c (img, x, y + isz + 10 * k, w, 16 * k, lab, f, (255, 255, 255) if focus else fg)
	if running: rrect (img, x + w // 2 - 3 * k, y + h - 7 * k, 6 * k, 4 * k, 2 * k, P["accent"] if not focus else (255, 255, 255))

def keycap (img, x, y, label, k = 1, f = None, w = None, colour = (237, 237, 239), fg = (30, 30, 32)):
	f = f or font (10 * k, True); w = w or max (tw (label, f) + 10 * k, 18 * k); h = 18 * k
	rrect (img, x, y, w, h, 4 * k, (lighten (colour, 0.4), shade (colour, 0.86))); ring (img, x, y, w, h, 4 * k, (0, 0, 0), alpha = 70)
	text_c (img, x, y - 1 * k, w, h, label, f, fg)
	return w

def hint (img, x, y, keys, words, colour = (236, 240, 246), k = 1):
	"""A key hint: keycaps then a word."""
	for key in keys: x += keycap (img, x, y, key, k) + 3 * k
	f = font (11 * k); text_l (img, x + 2 * k, y, 18 * k, words, f, colour)
	return x + 2 * k + tw (words, f) + 14 * k

# ---- device frames -----------------------------------------------------------------------------------------
def frame_clamshell (screen, label = None):
	"""A Zaurus SL-C-like clamshell: the lid (the screen), the hinge, the keyboard below."""
	sw, sh = screen.size; m = 34
	W, H = sw + 2 * m, sh + 2 * m + 260
	img = Image.new ("RGB", (W, H), (255, 255, 255))
	rrect (img, 0, 0, W, sh + 2 * m, 22, ((70, 74, 82), (44, 46, 52)))
	ring (img, 0, 0, W, sh + 2 * m, 22, (20, 20, 24), alpha = 160)
	rrect (img, m - 6, m - 6, sw + 12, sh + 12, 6, (12, 12, 14))
	img.paste (screen, (m, m))
	if label: text_c (img, 0, sh + m + 6, W, m - 10, label, font (11, True), (170, 174, 182))
	# the hinge and the base
	ky = sh + 2 * m + 6
	rrect (img, 30, ky - 8, W - 60, 14, 6, ((40, 42, 48), (90, 94, 102)))
	rrect (img, 0, ky, W, 250, 22, ((206, 208, 212), (170, 172, 178)))
	ring (img, 0, ky, W, 250, 22, (90, 92, 98), alpha = 140)
	rows = ["1234567890-", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM,."]
	fk = font (12, True); kx0 = 40; kw = (W - 80 - 10 * 6) // 11
	for r, row in enumerate (rows):
		off = [0, kw // 3, kw // 2 + 6, kw][r]
		for i, ch in enumerate (row):
			x = kx0 + off + i * (kw + 6); y = ky + 22 + r * 46
			rrect (img, x, y, kw, 38, 7, ((250, 250, 252), (214, 216, 220))); ring (img, x, y, kw, 38, 7, (90, 92, 98), alpha = 120)
			text_c (img, x, y, kw, 34, ch, fk, (40, 42, 48))
	y = ky + 22 + 4 * 46
	for x, w, lab in [(40, 70, "Fn"), (116, 70, "Home"), (192, W - 192 - 192, ""), (W - 186, 70, "Menu"), (W - 110, 70, "OK")]:
		rrect (img, x, y, w, 30, 7, ((250, 250, 252), (214, 216, 220))); ring (img, x, y, w, 30, 7, (90, 92, 98), alpha = 120)
		text_c (img, x, y, w, 28, lab, font (11, True), (40, 42, 48))
	return img

def frame_slate (screen, label = None, buttons = ("Home", "Tasks", "Menu", "Back")):
	"""A Zaurus SL-5500-like slate: the screen, the d-pad and four application keys below."""
	sw, sh = screen.size; m = 30
	W, H = sw + 2 * m, sh + m + 210
	img = Image.new ("RGB", (W, H), (255, 255, 255))
	rrect (img, 0, 0, W, H, 40, ((200, 204, 212), (150, 154, 164)))
	ring (img, 0, 0, W, H, 40, (70, 72, 80), alpha = 150)
	rrect (img, m - 8, m - 8, sw + 16, sh + 16, 10, (20, 20, 24))
	img.paste (screen, (m, m))
	if label: text_c (img, 0, sh + m + 10, W, 22, label, font (14, True), (50, 54, 62))
	cy = sh + m + 120; cx = W // 2
	rrect (img, cx - 60, cy - 60, 120, 120, 60, ((120, 124, 134), (86, 90, 100)))
	for a, (dx, dy) in enumerate ([(0, -1), (1, 0), (0, 1), (-1, 0)]):
		glyph (img, cx + dx * 38 - 10, cy + dy * 38 - 10, 20, 20, lambda d, s, a = a: d.polygon (
			[[(10 * s, 3 * s), (17 * s, 14 * s), (3 * s, 14 * s)], [(17 * s, 10 * s), (6 * s, 3 * s), (6 * s, 17 * s)],
			 [(10 * s, 17 * s), (17 * s, 6 * s), (3 * s, 6 * s)], [(3 * s, 10 * s), (14 * s, 3 * s), (14 * s, 17 * s)]][a], fill = 255), (230, 232, 236))
	rrect (img, cx - 22, cy - 22, 44, 44, 22, ((150, 154, 164), (110, 114, 124))); text_c (img, cx - 22, cy - 22, 44, 44, "OK", font (12, True), (240, 240, 244))
	bx = [36, 108, W - 172, W - 100]
	for i, b in enumerate (buttons):
		x = bx[i]; y = cy - 52 + (i % 2) * 0
		rrect (img, x, cy - 20, 64, 40, 20, ((236, 238, 242), (190, 194, 202))); ring (img, x, cy - 20, 64, 40, 20, (70, 72, 80), alpha = 120)
		text_c (img, x, cy - 20, 64, 40, b, font (11, True), (50, 52, 60))
	return img

def frame_handheld (screen, label = None):
	"""A Pi handheld (a GPi / uConsole-like game shell): d-pad left, ABXY right, Select / Start."""
	sw, sh = screen.size; side = 170; m = 30
	W, H = sw + 2 * side, sh + 2 * m
	img = Image.new ("RGB", (W, H), (255, 255, 255))
	rrect (img, 0, 0, W, H, 70, ((64, 68, 78), (36, 38, 44))); ring (img, 0, 0, W, H, 70, (10, 10, 12), alpha = 160)
	rrect (img, side - 10, m - 10, sw + 20, sh + 20, 10, (8, 8, 10)); img.paste (screen, (side, m))
	cy = H // 2 - 40; cx = side // 2 + 4
	for dx, dy, ww, hh in [(-14, -46, 28, 92), (-46, -14, 92, 28)]:
		rrect (img, cx + dx, cy + dy, ww, hh, 6, ((30, 30, 34), (16, 16, 18)))
	bx = W - side // 2 - 4
	for lab, dx, dy, col in [("X", 0, -34, (90, 130, 210)), ("Y", -34, 0, (100, 180, 110)), ("A", 34, 0, (214, 88, 80)), ("B", 0, 34, (226, 190, 70))]:
		rrect (img, bx + dx - 18, cy + dy - 18, 36, 36, 18, (lighten (col, 0.2), shade (col, 0.8)))
		text_c (img, bx + dx - 18, cy + dy - 18, 36, 36, lab, font (14, True), (255, 255, 255))
	for i, lab in enumerate (("SELECT", "START")):
		x = side // 2 - 30 if i == 0 else W - side // 2 - 30
		rrect (img, x, cy + 120, 60, 16, 8, (24, 24, 28)); text_c (img, x, cy + 138, 60, 16, lab, font (9, True), (170, 174, 182))
	if label: text_c (img, side, H - m + 6, sw, m - 10, label, font (11, True), (170, 174, 182))
	return img

def bezel (screen, m = 14):
	"""A plain dark bezel round a screen (the overview)."""
	img = Image.new ("RGB", (screen.width + 2 * m, screen.height + 2 * m), (255, 255, 255))
	rrect (img, 0, 0, img.width, img.height, m, ((60, 64, 72), (34, 36, 42))); img.paste (screen, (m, m))
	return img

def caption_sheet (img, title, sub = None, pad = 18):
	"""A light card around a picture, a title above (for the overview and the frames)."""
	f, fs = font (15, True), font (12)
	th = 30 + (20 if sub else 0)
	out = Image.new ("RGB", (img.width + 2 * pad, img.height + th + 2 * pad), (255, 255, 255))
	text (out, pad, pad - 2, title, f, (24, 26, 30))
	if sub: text (out, pad, pad + 20, sub, fs, (100, 104, 110))
	out.paste (img, (pad, pad + th))
	return out

# ===========================================================================================================
# P -- "Pocket", the recommended concept (800 x 480)
# ===========================================================================================================
PW, PH = 800, 480

def pocket_home (w = PW, h = PH, cat = "Productivity", focus = 6, search = None):
	img = wallpaper (w, h)
	statusbar (img, w, home = True)
	# the search field and the hints
	y = BAR + 10
	rrect (img, 12, y, 300, 28, 14, P["field"]); ring (img, 12, y, 300, 28, 14, (0, 0, 0), alpha = 60)
	glyph (img, 22, y + 7, 14, 14, lambda d, s: g_search (d, s), (110, 110, 116))
	f = font (12)
	if search: text_l (img, 42, y, 28, search, f, P["ink"]); vline (img, 42 + tw (search, f) + 1, y + 7, 14, P["ink"])
	else: text_l (img, 42, y, 28, "Type to find an app, a file, a setting", f, (140, 138, 136))
	hx = 330
	if search:
		hx = hint (img, hx, y + 5, ["Up", "Down"], "choose"); hx = hint (img, hx, y + 5, ["Enter"], "open"); hint (img, hx, y + 5, ["Esc"], "clear")
		return search_results (img, w, h, y + 38, search)
	hx = hint (img, hx, y + 5, ["Tab"], "category")
	hx = hint (img, hx, y + 5, ["Arrows"], "choose")
	hx = hint (img, hx, y + 5, ["Enter"], "open")
	if search: return search_results (img, w, h, y + 38, search)
	# the tabs and the panel
	ty = y + 38
	tabs_row (img, 12, ty, w - 24, CATS, cat)
	py = ty + 32; ph = h - py - 78
	rrect (img, 12, py, w - 24, ph, 10, ((250, 250, 251), (228, 230, 234)), corners = (False, True, True, True))
	ring (img, 12, py, w - 24, ph, 10, (0, 0, 0), alpha = 60, corners = (False, True, True, True))
	apps = apps_in (cat) if cat not in ("Recent", "Settings") else (
		["terminal", "fileviewer", "ledger", "tinypad", "jet", "telegram", "mail", "media"] if cat == "Recent" else SETTINGS)
	tw_, th_ = 94, 84; cols = (w - 40) // tw_
	gx = 12 + (w - 24 - cols * tw_) // 2
	running = {"tinypad", "ledger", "terminal", "fileviewer", "telegram"}
	for i, a in enumerate (apps):
		r, c = divmod (i, cols)
		if 10 + (r + 1) * th_ > ph: break
		tile (img, gx + c * tw_, py + 8 + r * th_, tw_ - 4, th_ - 4, a, NAMES.get (a, app_name (a)), focus = i == focus,
		      running = a in running)
	# the running strip (Qtopia's taskbar, on the launcher only)
	sy = h - 68
	text_l (img, 16, sy, 18, "Running", font (11, True), (210, 220, 234))
	hint (img, 90, sy, ["Alt", "Tab"], "switch", k = 1)
	x = 16; cy = sy + 22
	for a in ["terminal", "ledger", "tinypad", "fileviewer", "telegram"]:
		n = app_name (a); cw = 34 + tw (n, font (12)) + 30
		rrect (img, x, cy, cw, 36, 9, ((247, 247, 249), (222, 222, 224))); ring (img, x, cy, cw, 36, 9, (0, 0, 0), alpha = 80)
		draw_icon (img, a, x + 6, cy + 6, 24); text_l (img, x + 36, cy, 36, n, font (12), P["ink"])
		glyph (img, x + cw - 20, cy + 13, 10, 10, lambda d, s: g_close (d, s), (120, 116, 112))
		x += cw + 8
	return img

def search_results (img, w, h, y, q):
	"""Typing on the launcher: the tabs give way to the results, grouped; Enter opens the first."""
	ph = h - y - 12
	rrect (img, 12, y, w - 24, ph, 10, ((247, 247, 249), (234, 234, 236))); ring (img, 12, y, w - 24, ph, 10, (0, 0, 0), alpha = 70)
	groups = [("Apps", [("ledger", "Ledger", "Productivity -- running", True)]),
		  ("Files", [("ledger", "demo-company.ledger", "SD:/docs/  --  yesterday", False), ("pdf", "ledger-manual.pdf", "SD:/manuals/  --  1.2 MB", False)]),
		  ("Settings", [("displayconf", "Display: LED backlight brightness", "Control Panel > Display", False)]),
		  ("Run", [("terminal", "led", "run it in a Terminal (a /bin command)", False)])]
	cy = y + 10; fb = font (12, True)
	first = True
	for g, rows in groups:
		text (img, 28, cy + 2, g.upper (), font (10, True), (130, 126, 122)); cy += 20
		for icon, name, sub, run in rows:
			if first: rrect (img, 20, cy, w - 40, 38, 7, (lighten (P["accent"], 0.1), shade (P["accent"], 0.95)))
			fg = (255, 255, 255) if first else P["ink"]
			draw_icon (img, icon, 30, cy + 5, 28)
			i = name.lower ().find (q)
			text (img, 68, cy + 3, name, fb, fg)
			if i >= 0: hline (img, 68 + tw (name[:i], fb), cy + 19, tw (name[i:i + len (q)], fb), fg)
			text (img, 68, cy + 20, sub, font (10), (226, 238, 244) if first else P["ink2"])
			if first: text_r (img, w - 32, cy, 38, "Enter", font (11, True), (255, 255, 255))
			first = False
			cy += 40
		cy += 4
	return img

# ---- the apps' screens (drawn the way the real ones look) -------------------------------------------------
TERM_LINES = [("SD:/ $ ", "ls /bin | grep e"), ("", "echo"), ("", "sleep"), ("", "yes"), ("SD:/ $ ", "ps"),
	("", "  1 k R  idle"), ("", "  2 a S  elegant"), ("", " 14 a S  pocket"), ("", " 15 a S  launcher"),
	("", " 16 a S  notifyd"), ("", " 21 a R  terminal"), ("", " 22 a S  ledger"), ("SD:/ $ ", "cat /etc/system.ini | grep shell"),
	("", "shell=pocket"), ("SD:/ $ ", "")]

def terminal_screen (img, x, y, w, h, k = 1, lines = TERM_LINES, tabs = ("SD:/", "docs", "ping 192.168.1.1"), fs = 13):
	box (img, x, y, w, h, P["term"])
	th = 28 * k
	box (img, x, y, w, th, shade (P["term"], 0.8))
	tx = x + 6 * k
	for i, t in enumerate (tabs):
		tw0 = min (150 * k, (w - 40 * k) / len (tabs) - 4 * k)
		if i == 0: rrect (img, tx, y + 4 * k, tw0, th - 4 * k, 6 * k, P["term"], corners = (True, True, False, False))
		if t.startswith ("ping"): rrect (img, tx + 8 * k, y + 13 * k, 6 * k, 6 * k, 3 * k, (90, 170, 240))		# (busy)
		text_l (img, tx + (18 if t.startswith ("ping") else 10) * k, y + 4 * k, th - 4 * k, ellipsize (t, font (12 * k), tw0 - (32 if (i == 0 or tw0 > 110 * k) else 22) * k), font (12 * k), P["term_fg"] if i == 0 else (150, 180, 190))
		if i == 0 or tw0 > 110 * k: glyph (img, tx + tw0 - 18 * k, y + 13 * k, 10 * k, 10 * k, lambda d, s: g_close (d, s, k), (150, 180, 190))
		tx += tw0 + 4 * k
	text_l (img, tx + 6 * k, y + 4 * k, th - 4 * k, "+", font (14 * k), (150, 180, 190))
	fm = font (fs * k, mono = True); lh = int (fs * 1.45) * k
	cy = y + th + 8 * k
	for p, s in lines:
		if cy + lh > y + h: break
		text (img, x + 10 * k, cy, p + s, fm, P["term_fg"])
		if p and s == "": rrect (img, x + 10 * k + tw (p, fm), cy + 1 * k, 8 * k, lh - 3 * k, 1, P["cursor"])
		cy += lh

def pocket_terminal ():
	img = Image.new ("RGB", (PW, PH), P["face"])
	statusbar (img, PW, title = "Terminal", menus = ("Shell",))			# (its one menu: user/Apps/terminal)
	terminal_screen (img, 0, BAR, PW, PH - BAR)
	return img

def editor_screen (img, x, y, w, h, k = 1, path = "SD:/docs/compact.txt", focus = True, lines = None, fs = 13):
	box (img, x, y, w, h, P["face"])
	f = font (12 * k)
	text_l (img, x + 8 * k, y, 24 * k, path, font (12 * k, True), P["ink"])
	rrect (img, x + 4 * k, y + 24 * k, w - 8 * k, h - 28 * k, 4 * k, P["paper"])
	ring (img, x + 4 * k, y + 24 * k, w - 8 * k, h - 28 * k, 4 * k, P["accent"] if focus else (0, 0, 0), alpha = 220 if focus else 60)
	lines = lines or ["The compact shell -- notes", "", "- one app at a time, full screen, no frame",
			  "- the menu bar stays: the app's menus at the top", "- Alt+Tab: the switcher, live thumbnails",
			  "- Super+Left / Right: two apps side by side", "- the clock opens quick settings", "",
			  "TODO: UIKit's compact metrics, the focus ring", "      on every widget (the d-pad)"]
	fm = font (fs * k, mono = True); lh = int (fs * 1.45) * k; cy = y + 30 * k
	for i, s in enumerate (lines):
		if cy + lh > y + h - 6 * k: break
		text (img, x + 12 * k, cy, s, fm, P["ink"]); cy += lh
	if focus: vline (img, x + 12 * k + tw (lines[-1], fm) + 1, cy - lh + 2 * k, lh - 4 * k, P["ink"])

def pocket_menu ():
	img = Image.new ("RGB", (PW, PH), P["face"])
	pos = statusbar (img, PW, title = "Text Editor", menus = ("File", "Edit", "Search", "View"), open_menu = "Edit")
	editor_screen (img, 0, BAR, PW, PH - BAR, focus = False)
	img = dim_below (img, BAR, 40)
	dropdown (img, pos["Edit"], BAR + 2, [("Undo", "Ctrl+Z"), ("~Redo", "Ctrl+Y"), "-", ("Cut", "Ctrl+X"), ("Copy", "Ctrl+C"),
		("Paste", "Ctrl+V"), "-", ("Select All", "Ctrl+A"), ("Insert", ""), "-", ("Preferences...", "")], sel = 4, w = 220, sub = 8)
	# the key hints, along the bottom (a toast-like strip)
	y = PH - 40
	rrect (img, 250, y, 536, 30, 15, (30, 36, 46), alpha = 230)
	x = 262
	x = hint (img, x, y + 6, ["F10"], "the menus")
	x = hint (img, x, y + 6, ["<", ">"], "menu to menu")
	x = hint (img, x, y + 6, ["Alt", "E"], "Edit at once")
	x = hint (img, x, y + 6, ["Esc"], "close")
	return img

def dim_below (img, y, alpha):
	top = img.crop ((0, 0, img.width, y)); out = dim (img, alpha); out.paste (top, (0, 0)); return out

def pocket_switcher ():
	base = pocket_terminal ()
	img = dim (base, 244)
	statusbar (img, PW, title = "Terminal", menus = ())
	cards = [("terminal", None), ("ledger", "ledger"), ("tinypad", None), ("fileviewer", "fileviewer"), ("telegram", "telegram")]
	text_c (img, 0, BAR + 26, PW, 24, "Open apps -- the most recent first", font (15, True), (236, 240, 246))
	small, big, gap = 136, 196, 10
	widths = [big if i == 1 else small for i in range (len (cards))]
	x = (PW - sum (widths) - gap * (len (cards) - 1)) // 2
	mid = 210
	for i, (a, sname) in enumerate (cards):
		cw = widths[i]; ch = int (cw * 0.62); sel = i == 1
		cy = mid - ch // 2
		if sname: th = shot (sname)
		elif a == "terminal":
			th = Image.new ("RGB", (PW, PH - BAR), P["term"]); terminal_screen (th, 0, 0, PW, PH - BAR)
		else:
			th = Image.new ("RGB", (PW, PH - BAR), P["face"]); editor_screen (th, 0, 0, PW, PH - BAR)
		th = th.resize ((cw, int (cw * th.height / th.width)), Image.LANCZOS).crop ((0, 0, cw, ch))
		if sel: rrect (img, x - 7, cy - 7, cw + 14, ch + 48, 12, (lighten (P["accent"], 0.15), shade (P["accent"], 0.92)))
		box (img, x, cy, cw, ch, P["face"]); img.paste (th, (x, cy)); ring (img, x, cy, cw, ch, 3, (0, 0, 0), alpha = 120)
		draw_icon (img, a, x, cy + ch + 8, 24)
		f = font (12, sel)
		text_l (img, x + 30, cy + ch + 8, 24, ellipsize (app_name (a), f, cw - 32), f, (255, 255, 255))
		if i == 0: text (img, x, cy - 20, "now", font (10, True), (170, 186, 206))
		if sel:
			rrect (img, x + cw - 24, cy - 16, 28, 28, 14, (60, 64, 72)); ring (img, x + cw - 24, cy - 16, 28, 28, 14, (255, 255, 255), t = 1.5)
			glyph (img, x + cw - 15, cy - 7, 10, 10, lambda d, s: g_close (d, s), (255, 255, 255))
		x += cw + gap
	y = PH - 110
	text_c (img, 0, y, PW, 20, "Thumbnails from Elegant's copies of the windows (EL_OP_SHOT): no app redraws for the switcher",
		font (11), (170, 186, 206))
	y = PH - 46; x = 106
	x = hint (img, x, y, ["Alt", "Tab"], "next")
	x = hint (img, x, y, ["Shift", "Tab"], "back")
	x = hint (img, x, y, ["Del"], "close the app")
	x = hint (img, x, y, ["S"], "split with the current")
	x = hint (img, x, y, ["Esc"], "stay")
	return img

def toggle_tile (img, x, y, w, h, gl, label, sub, on):
	bg = (lighten (P["accent"], 0.12), shade (P["accent"], 0.92)) if on else ((243, 243, 245), (222, 222, 224))
	rrect (img, x, y, w, h, 10, bg); ring (img, x, y, w, h, 10, (0, 0, 0), alpha = 50)
	fg = (255, 255, 255) if on else P["ink"]
	glyph (img, x + 10, y + (h - 16) // 2, 16, 16, gl, fg)
	text (img, x + 34, y + 7, label, font (12, True), fg)
	text (img, x + 34, y + 23, sub, font (11), (230, 240, 244) if on else P["ink2"])

def slider (img, x, y, w, v, gl, label):
	glyph (img, x, y + 1, 16, 16, gl, P["ink"])
	rrect (img, x + 26, y + 6, w - 26, 6, 3, (194, 194, 196))
	rrect (img, x + 26, y + 6, int ((w - 26) * v), 6, 3, P["accent"])
	cx = x + 26 + int ((w - 26) * v)
	rrect (img, cx - 9, y - 1, 18, 18, 9, ((255, 255, 255), (218, 218, 220))); ring (img, cx - 9, y - 1, 18, 18, 9, (0, 0, 0), alpha = 90)

def notif_card (img, x, y, w, app, title, body, when):
	rrect (img, x, y, w, 46, 9, (255, 255, 255)); ring (img, x, y, w, 46, 9, (0, 0, 0), alpha = 40)
	draw_icon (img, app, x + 9, y + 9, 28)
	text (img, x + 46, y + 6, title, font (12, True), P["ink"]); text_r (img, x + w - 10, y + 4, 18, when, font (10), P["ink2"])
	text (img, x + 46, y + 24, ellipsize (body, font (11), w - 56), font (11), P["ink2"])

def quick_panel (img, x, y, w, k = 1):
	h = 438
	rrect (img, x, y, w, h, 12, ((247, 247, 249), (233, 233, 235))); ring (img, x, y, w, h, 12, (40, 40, 44), alpha = 140)
	text (img, x + 14, y + 12, "Thursday 8 October", font (13, True), P["ink"])
	text_r (img, x + w - 14, y + 10, 20, "12:34", font (16, True), P["ink"])
	tx, ty = x + 12, y + 42; tw0 = (w - 32) // 2; th0 = 42
	toggle_tile (img, tx, ty, tw0, th0, lambda d, s: g_wifi (d, s), "Wi-Fi", "Maison-5G", True)
	toggle_tile (img, tx + tw0 + 8, ty, tw0, th0, lambda d, s: g_kbd (d, s), "Keyboard", "on screen: auto", False)
	toggle_tile (img, tx, ty + th0 + 8, tw0, th0, lambda d, s: g_rotate (d, s), "Rotate", "landscape", False)
	toggle_tile (img, tx + tw0 + 8, ty + th0 + 8, tw0, th0, lambda d, s: g_dnd (d, s), "Do not disturb", "off", False)
	sy = ty + 2 * th0 + 26
	slider (img, x + 16, sy, w - 36, 0.62, lambda d, s: g_sun (d, s), "Brightness")
	slider (img, x + 16, sy + 30, w - 36, 0.45, lambda d, s: g_speaker (d, s), "Volume")
	ny = sy + 60
	hline (img, x + 12, ny, w - 24, (200, 200, 202))
	text (img, x + 14, ny + 8, "Notifications", font (12, True), P["ink"]); text_r (img, x + w - 14, ny + 6, 18, "Clear all", font (11), P["accent"])
	notif_card (img, x + 12, ny + 30, w - 24, "telegram", "Telegram", "Marie: on se voit demain ? -- 2 unread", "2 min")
	notif_card (img, x + 12, ny + 80, w - 24, "pkgman", "Packages", "3 updates: onyx 2026.10.92, jet, ledger", "1 h")
	notif_card (img, x + 12, ny + 130, w - 24, "calendar", "Calendar", "Dentist at 17:30", "today")
	by = y + h - 40
	for i, (gl, lab) in enumerate ([(g_gear, "Control Panel"), (g_lock, "Lock"), (g_power, "Power")]):
		bw = [130, 76, 84][i]; bx = x + 12 + [0, 138, 222][i]
		rrect (img, bx, by, bw, 28, 8, ((249, 249, 251), (222, 222, 224))); ring (img, bx, by, bw, 28, 8, (0, 0, 0), alpha = 70)
		glyph (img, bx + 8, by + 6, 16, 16, lambda d, s, gl = gl: gl (d, s), P["red"] if gl is g_power else P["ink"])
		text_l (img, bx + 28, by, 28, lab, font (11, True), P["ink"])

def pocket_quick ():
	img = pocket_terminal ()
	img = dim_below (img, BAR, 120)
	statusbar (img, PW, title = "Terminal", menus = ("Shell",), hot = "clock")
	quick_panel (img, PW - 346, BAR + 4, 340)
	y = PH - 40; x = 20
	x = hint (img, x, y, ["Super", "N"], "or a click on the clock")
	return img

# ---- Ledger in compact mode: the sidebar folds into a rail ------------------------------------------------
def ledger_icon (y, src):
	return src.crop ((20, y - 11, 44, y + 13))

def pocket_ledger ():
	img = Image.new ("RGB", (PW, PH), P["face"])
	statusbar (img, PW, title = "Ledger", menus = ("File", "Edit", "Journals", "Reports", "Help"))
	src = Image.open (os.path.join (SHOTS, "ledger.png")).convert ("RGBA")
	bg = Image.new ("RGBA", src.size, (208, 194, 186, 255)); bg.alpha_composite (src); src = milkify (bg)
	# the rail: the sidebar's icons, the labels gone; the chosen one lit
	rw = 52
	box (img, 0, BAR, rw, PH - BAR, shade (P["face"], 0.93)); vline (img, rw, BAR, PH - BAR, shade (P["face"], 0.75))
	ys = [147, 201, 231, 261, 291, 345, 399, 429, 483, 513, 543]
	cy = BAR + 8
	for i, yy in enumerate (ys):
		if i == 0:
			rrect (img, 6, cy - 2, 40, 32, 7, (lighten (P["accent"], 0.1), shade (P["accent"], 0.95)))
			glyph (img, 17, cy + 5, 18, 18, lambda d, s: [d.rounded_rectangle ([(a * 10) * s, (b * 10) * s, (a * 10 + 8) * s, (b * 10 + 8) * s], 2 * s, fill = 255) for a in (0, 1) for b in (0, 1)], (255, 255, 255))
		else: img.paste (src.crop ((22, yy - 9, 42, yy + 11)), (16, cy + 4))
		if i == 1: rrect (img, 32, cy + 0, 15, 13, 6.5, (210, 60, 50)); text_c (img, 32, cy + 0, 15, 13, "1", font (9, True), (255, 255, 255))
		cy += 34
		if i in (4, 5, 7): hline (img, 12, cy - 2, 28, shade (P["face"], 0.75)); cy += 4
	hline (img, 12, PH - 38, 28, shade (P["face"], 0.75))
	img.paste (src.crop ((22, 597 - 9, 42, 597 + 11)), (16, PH - 30))
	# the content: the header, four cards, the chart, the two panels made one row
	x0 = rw + 12; cw = PW - x0 - 12
	img.paste (src.crop ((230, 40, 500, 80)), (x0, BAR + 6))
	btn = src.crop ((649, 41, 989, 72)); img.paste (btn, (PW - 12 - btn.width, BAR + 10))
	y = BAR + 50
	hline (img, x0, y, cw, shade (P["face"], 0.78))
	cards = [((228, 100, 408, 183))]
	cwid = (cw - 3 * 8) // 4
	for i in range (4):
		c = src.crop ((228 + i * 193, 100, 228 + i * 193 + 180, 184))
		c = Image.fromarray (np.hstack ([np.array (c)[:, :cwid - 14], np.array (c)[:, 180 - 14:]]))	# narrower: its middle cut
		img.paste (c, (x0 + i * (cwid + 8), y + 8))
	# the chart (by code, the screenshot's colours and values) -- shorter
	cy0 = y + 8 + 84 + 8; ch = PH - cy0 - 10
	rrect (img, x0, cy0, cw, ch, 8, P["paper"]); ring (img, x0, cy0, cw, ch, 8, (0, 0, 0), alpha = 40)
	text (img, x0 + 14, cy0 + 10, "Sales and purchases by month (excl. VAT)", font (12, True), P["ink"])
	for j, (lab, col) in enumerate ([("Sales", (61, 134, 218)), ("Purchases", (178, 178, 184))]):
		lx = x0 + cw - 170 + j * 74
		rrect (img, lx, cy0 + 14, 9, 9, 2, col); text (img, lx + 13, cy0 + 10, lab, font (11), P["ink2"])
	months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
	sales = [12.3, 8.4, 8.8, 9.9, 8.5, 12.3, 13.4, 10.3, 12.4, 0, 0, 0]; purch = [10.5, 10.0, 9.5, 7.0, 11.1, 7.9, 9.1, 10.1, 9.4, 0, 0, 0]
	px0 = x0 + 54; pw = cw - 70; py0 = cy0 + 36; pbh = ch - 70
	for t in range (5):
		yy = py0 + pbh - int (pbh * t / 4)
		hline (img, px0, yy, pw, (218, 218, 220)); text_r (img, px0 - 6, yy - 8, 16, "%d.000" % (t * 5) if t else "0", font (10), P["ink2"])
	gw = pw / 12
	for i, m in enumerate (months):
		bx = px0 + int (i * gw + gw * 0.14); bw = int (gw * 0.34)
		for j, (v, col) in enumerate ([(sales[i], (61, 134, 218)), (purch[i], (178, 178, 184))]):
			bh = int (pbh * v / 20)
			if bh: rrect (img, bx + j * (bw + 2), py0 + pbh - bh, bw, bh, 2, (lighten (col, 0.1), shade (col, 0.9)), corners = (True, True, False, False))
		text_c (img, px0 + int (i * gw), py0 + pbh + 4, int (gw), 16, m, font (10), P["ink2"])
	return img

# ---- split view: two apps side by side (B's idea, kept) ---------------------------------------------------
def fileviewer_compact (img, x, y, w, h, k = 1):
	box (img, x, y, w, h, P["face"])
	# the path bar
	rrect (img, x + 6, y + 6, w - 12, 28, 8, ((243, 243, 245), (232, 232, 234))); ring (img, x + 6, y + 6, w - 12, 28, 8, (0, 0, 0), alpha = 60)
	f = font (12)
	text_l (img, x + 16, y + 6, 28, "SD Card", f, P["ink"]); glyph (img, x + 76, y + 15, 10, 10, lambda d, s: g_chev (d, s, 1, "right"), P["ink2"])
	text_l (img, x + 94, y + 6, 28, "docs", font (12, True), P["accent"]); hline (img, x + 94, y + 27, tw ("docs", font (12, True)), P["accent"])
	# one column (the sidebar folded: a chevron on the path bar's left opens it)
	ly = y + 42
	rrect (img, x + 6, ly, w - 12, h - 42 - 30, 6, P["paper"])
	files = [("manuals", True, ""), ("samples", True, ""), ("compact.txt", False, "2.1 KB"), ("handoff.txt", False, "88 KB"),
		 ("ideas.txt", False, "14 KB"), ("letter.odt", False, "31 KB"), ("budget.ods", False, "12 KB"), ("photo.jpg", False, "1.4 MB"),
		 ("readme.txt", False, "3.0 KB")]
	for i, (n, d, sz) in enumerate (files):
		ry = ly + 4 + i * 30
		if ry + 30 > y + h - 34: break
		if n == "compact.txt": rrect (img, x + 10, ry, w - 20, 28, 6, P["accent"])
		fg = (255, 255, 255) if n == "compact.txt" else P["ink"]
		# a little icon: folder or page
		if d:
			rrect (img, x + 18, ry + 9, 18, 13, 2, (222, 180, 90)); rrect (img, x + 18, ry + 6, 8, 5, 1, (222, 180, 90))
		else:
			rrect (img, x + 20, ry + 5, 14, 18, 2, (255, 255, 255)); ring (img, x + 20, ry + 5, 14, 18, 2, (120, 120, 126))
			for t in range (3): hline (img, x + 23, ry + 10 + t * 4, 8, (150, 150, 156))
		text_l (img, x + 44, ry, 28, n, f, fg)
		if sz: text_r (img, x + w - 18, ry, 28, sz, font (11), (230, 240, 244) if n == "compact.txt" else P["ink2"])
		if d: glyph (img, x + w - 26, ry + 9, 10, 10, lambda d_, s: g_chev (d_, s, 1, "right"), P["ink2"])
	box (img, x, y + h - 26, w, 26, shade (P["face"], 0.95)); hline (img, x, y + h - 26, w, shade (P["face"], 0.8))
	text_l (img, x + 10, y + h - 26, 26, "9 items  -  compact.txt (2.1 KB)", font (11), P["ink"])

def pocket_split ():
	img = Image.new ("RGB", (PW, PH), P["face"])
	statusbar (img, PW, title = "Text Editor", menus = ("File", "Edit", "Search", "View"))
	lw = 318
	fileviewer_compact (img, 0, BAR, lw, PH - BAR)
	# the divider: a handle to drag (or Super+[ / ])
	box (img, lw, BAR, 6, PH - BAR, shade (P["face"], 0.78))
	rrect (img, lw + 1, BAR + (PH - BAR) // 2 - 20, 4, 40, 2, (255, 255, 255))
	editor_screen (img, lw + 6, BAR, PW - lw - 6, PH - BAR, fs = 12,
		lines = ["The compact shell -- notes", "", "- one app at a time, full screen", "- the menu bar stays (the app",
			 "  in front's menus)", "- Alt+Tab: the switcher", "- Super+Left / Right: two apps", "  side by side, 40/60 or 50/50",
			 "- the clock: quick settings", "", "TODO: UIKit's compact metrics"])
	# the focused side: a thin accent line under the bar
	box (img, lw + 6, BAR, PW - lw - 6, 3, P["accent"])
	y = PH - 40
	rrect (img, 330, y, 456, 30, 15, (30, 36, 46), alpha = 230)
	x = 342
	x = hint (img, x, y + 6, ["Super", "<"], "left")
	x = hint (img, x, y + 6, ["Super", "["], "narrower")
	x = hint (img, x, y + 6, ["Super", "Tab"], "other side")
	return img

# ---- a fixed-size window (Calculator): a card, centred, the rest dimmed -----------------------------------
def pocket_fixed ():
	img = pocket_home (cat = "Productivity", focus = 1)
	img = dim_below (img, BAR, 165)
	statusbar (img, PW, title = "Calculator", menus = ("Edit", "View"))
	c = shot ("tinycalc")
	x = (PW - c.width) // 2; y = BAR + (PH - BAR - c.height) // 2 + 4
	rrect (img, x - 6, y - 30, c.width + 12, 30, 10, ((250, 250, 250), P["face"]), corners = (True, True, False, False))
	box (img, x - 6, y, c.width + 12, c.height + 6, P["face"])
	ring (img, x - 6, y - 30, c.width + 12, c.height + 36, 10, (20, 20, 24), alpha = 170)
	text_c (img, x - 6, y - 30, c.width + 12, 28, "Calculator", font (12, True), P["ink"])
	bead (img, x + c.width - 18, y - 22, 14, P["red"])
	img.paste (c, (x, y))
	return img

# ===========================================================================================================
# Pocket in portrait, its details on a 240 x 320 slate (drawn at 2x: 480 x 640), Zaurus SL-5500-like
# ===========================================================================================================
Z = 2
def slate_bar (img, w, title = None, k = Z, menu = True, home = False, lit_menu = False):
	h = 24 * k
	img.paste (grad (w, h, P["bar"], P["bar2"]), (0, 0)); hline (img, 0, h - 1, w, shade (P["bar2"], 0.72))
	ic = (40, 44, 52)
	x = 4 * k
	if home: rrect (img, x, 3 * k, 22 * k, h - 6 * k, 5 * k, P["accent"])
	put (img, x + 4 * k, 5 * k, mask (14 * k, 14 * k, lambda d, s: g_gem (d, s, 0.85 * k)), (255, 255, 255) if home else ic)
	x += 28 * k
	if title: text_l (img, x, 0, h, title, font (11 * k, True), P["ink"]); x += tw (title, font (11 * k, True)) + 8 * k
	if menu:
		if lit_menu: rrect (img, x - 3 * k, 3 * k, 22 * k, h - 6 * k, 5 * k, P["accent"])
		glyph (img, x + 1 * k, 4 * k, 16 * k, 16 * k, lambda d, s: g_menu (d, s, 0.9 * k), (255, 255, 255) if lit_menu else ic)
	xr = w - 4 * k
	text_r (img, xr, 0, h, "12:34", font (11 * k, True), ic); xr -= tw ("12:34", font (11 * k, True)) + 6 * k
	battery (img, xr - 22 * k, 6 * k, 78, ic, k); xr -= 28 * k
	glyph (img, xr - 16 * k, 5 * k, 16 * k, 14 * k, lambda d, s: g_wifi (d, s * k), ic)

def slate_softkeys (img, w, h, keys, k = Z):
	sh = int (26 * k); y = h - sh
	img.paste (grad (w, sh, (232, 232, 234), (208, 208, 210)), (0, y)); hline (img, 0, y, w, shade (P["face"], 0.7))
	n = len (keys); cw = w // n
	for i, (gl, lab) in enumerate (keys):
		x = i * cw
		glyph (img, x + (cw - 13 * k) // 2, y + 2 * k, 13 * k, 13 * k, lambda d, s, gl = gl: gl (d, s, 0.78 * k), P["ink"])
		text_c (img, x, y + 16 * k, cw, 9 * k, lab, font (8 * k), P["ink2"])
		if i: vline (img, x, y + 4 * k, sh - 8 * k, shade (P["face"], 0.8))

def slate_home (cat = "Games", focus = 4):
	k = Z; w, h = 240 * k, 320 * k
	img = wallpaper (w, h, seed = 3)
	slate_bar (img, w, home = True, menu = False)
	y = 24 * k + 6 * k
	rrect (img, 6 * k, y, w - 12 * k, 22 * k, 11 * k, P["field"])
	glyph (img, 13 * k, y + 5 * k, 12 * k, 12 * k, lambda d, s: g_search (d, s, 0.8 * k), (110, 110, 116))
	text_l (img, 28 * k, y, 22 * k, "Find", font (10 * k), (140, 138, 136))
	ty = y + 28 * k
	order = [("Recent", CATC["Recent"]), ("Productivity", CATC["Productivity"]), ("Games", CATC["Games"]), ("Internet", CATC["Internet"])]
	# the tabs scroll: the chosen one in the middle, its neighbours cut
	cats = [c for c in CATS]
	f = font (10 * k, True); th = 24 * k
	i0 = [c[0] for c in cats].index (cat)
	cx = w // 2 - (tw (cat, f) + 26 * k) // 2
	xs = {}
	x = cx
	for j in range (i0, len (cats)):
		xs[j] = x; x += tw (cats[j][0], f) + 26 * k + 3 * k
	x = cx
	for j in range (i0 - 1, -1, -1):
		x -= tw (cats[j][0], f) + 26 * k + 3 * k; xs[j] = x
	for j, (n, col) in enumerate (cats):
		x = xs[j]; ww = tw (n, f) + 26 * k
		if x + ww < 0 or x > w: continue
		on = j == i0
		top, bot = ((255, 255, 255), (250, 250, 251)) if on else ((236, 237, 240), (208, 210, 216))
		hh = th if on else th - 3 * k
		rrect (img, x, ty + th - hh, ww, hh + 6 * k, 7 * k, (top, bot), corners = (True, True, False, False))
		rrect (img, x + 7 * k, ty + th - hh + (hh - 6 * k) // 2, 6 * k, 6 * k, 3 * k, col)
		text_c (img, x + 6 * k, ty + th - hh, ww - 6 * k, hh, n, f, P["ink"] if on else (60, 60, 66))
	py = ty + th; ph = h - py - 26 * k - 4 * k
	rrect (img, 4 * k, py, w - 8 * k, ph, 8 * k, ((250, 250, 251), (228, 230, 234)))
	apps = apps_in (cat)
	cols = 3; cw = (w - 16 * k) // cols; chh = 66 * k
	for i, a in enumerate (apps):
		r, c = divmod (i, cols)
		if (r + 1) * chh > ph - 6 * k: break
		tile (img, 8 * k + c * cw, py + 6 * k + r * chh, cw - 2 * k, chh - 4 * k, a, app_name (a), focus = i == focus, k = k, isz = 34 * k,
		      running = a in ("tetris",), small = True)
	slate_softkeys (img, w, h, [(g_grid, "Home"), (g_tasks, "Tasks"), (g_menu, "Menu"), (g_kbd, "Keys")])
	return img

def osk (img, x, y, w, k = Z, shift = False):
	rows = ["qwertyuiop", "asdfghjkl", "zxcvbnm"]
	kh = 24 * k; gap = 2 * k; h = 4 * kh + 5 * gap + 4 * k
	box (img, x, y, w, h, (190, 190, 192)); hline (img, x, y, w, shade (P["face"], 0.6))
	kw = (w - 11 * gap) / 10
	for r, row in enumerate (rows):
		off = [0, kw / 2, kw * 1.5][r]
		for i, ch in enumerate (row):
			kx = int (x + gap + off + i * (kw + gap)); ky = y + gap + 2 * k + r * (kh + gap)
			rrect (img, kx, ky, int (kw), kh, 4 * k, ((252, 251, 250), (228, 228, 230)))
			text_c (img, kx, ky, int (kw), kh, ch, font (11 * k), P["ink"])
	ky = y + gap + 2 * k + 2 * (kh + gap)
	for kx, kw2, lab in [(x + gap, kw * 1.4, "Shift"), (x + w - gap - kw * 1.4, kw * 1.4, "<x")]:
		rrect (img, int (kx), ky, int (kw2), kh, 4 * k, ((218, 218, 220), (196, 196, 198)))
		text_c (img, int (kx), ky, int (kw2), kh, lab, font (8 * k, True), P["ink"])
	ky += kh + gap
	for kx, kw2, lab in [(x + gap, kw * 2, "123"), (x + gap * 2 + kw * 2, kw * 6 + gap * 5, "space"), (x + w - gap - kw * 2, kw * 2, "Enter")]:
		accent = lab == "Enter"
		rrect (img, int (kx), ky, int (kw2), kh, 4 * k, (lighten (P["accent"], 0.1), shade (P["accent"], 0.92)) if accent else ((218, 218, 220), (196, 196, 198)))
		text_c (img, int (kx), ky, int (kw2), kh, lab, font (9 * k, True), (255, 255, 255) if accent else P["ink"])
	return h

def slate_app ():
	k = Z; w, h = 240 * k, 320 * k
	img = Image.new ("RGB", (w, h), P["face"])
	slate_bar (img, w, title = "Notes")
	y = 24 * k
	# the notes list as a header (one line) -- the app's own pane folded
	box (img, 0, y, w, 22 * k, shade (P["face"], 0.95))
	glyph (img, 6 * k, y + 6 * k, 10 * k, 10 * k, lambda d, s: g_chev (d, s, k, "left"), P["ink"])
	text_l (img, 20 * k, y, 22 * k, "Shopping", font (10 * k, True), P["ink"]); text_r (img, w - 8 * k, y, 22 * k, "3 of 12", font (9 * k), P["ink2"])
	y += 22 * k
	box (img, 4 * k, y + 4 * k, w - 8 * k, 120 * k, P["paper"]); ring (img, 4 * k, y + 4 * k, w - 8 * k, 120 * k, 3 * k, P["accent"], t = 1 * k)
	lines = ["Saturday market:", "- leeks, potatoes", "- Herve cheese", "- coffee (beans)", "- batteries AA x4", "- a microSD, 32 GB"]
	fm = font (10 * k); cy = y + 8 * k
	for s in lines: text (img, 10 * k, cy, s, fm, P["ink"]); cy += 16 * k
	vline (img, 10 * k + tw (lines[-1], fm) + 1, cy - 15 * k, 12 * k, P["ink"])
	osk (img, 0, h - 26 * k - 108 * k, w)
	slate_softkeys (img, w, h, [(g_grid, "Home"), (g_tasks, "Tasks"), (g_menu, "Menu"), (g_kbd, "Hide")])
	return img

def slate_menu ():
	k = Z; w, h = 240 * k, 320 * k
	img = Image.new ("RGB", (w, h), P["face"])
	slate_bar (img, w, title = "Notes", lit_menu = True)
	y = 24 * k
	box (img, 0, y, w, 22 * k, shade (P["face"], 0.95))
	text_l (img, 20 * k, y, 22 * k, "Shopping", font (10 * k, True), P["ink"])
	box (img, 4 * k, y + 26 * k, w - 8 * k, 200 * k, P["paper"])
	fm = font (10 * k); cy = y + 30 * k
	for s in ["Saturday market:", "- leeks, potatoes", "- Herve cheese", "- coffee (beans)"]: text (img, 10 * k, cy, s, fm, P["ink"]); cy += 16 * k
	img = dim_below (img, 24 * k, 120)
	# the menu as a sheet from the bottom: the top-level menus as tabs, the items below (big rows)
	sh = 214 * k; sy = h - 26 * k - sh
	rrect (img, 0, sy, w, sh + 10 * k, 12 * k, ((249, 249, 251), (236, 236, 238)), corners = (True, True, False, False))
	rrect (img, w // 2 - 16 * k, sy + 5 * k, 32 * k, 4 * k, 2 * k, (186, 186, 188))
	menus = ["File", "Edit", "Note", "View"]
	mx = 8 * k; my = sy + 14 * k
	for m in menus:
		mw = tw (m, font (10 * k, True)) + 16 * k
		if m == "Edit": rrect (img, mx, my, mw, 20 * k, 10 * k, P["accent"])
		text_c (img, mx, my, mw, 20 * k, m, font (10 * k, True), (255, 255, 255) if m == "Edit" else P["ink"])
		mx += mw + 4 * k
	items = [("Undo", "^Z"), ("Cut", "^X"), ("Copy", "^C"), ("Paste", "^V"), ("Select All", "^A"), ("Find...", "^F")]
	iy = my + 26 * k
	for i, (lab, sc) in enumerate (items):
		if i == 2: rrect (img, 6 * k, iy, w - 12 * k, 26 * k, 6 * k, (lighten (P["accent"], 0.75), lighten (P["accent"], 0.65)))
		text_l (img, 16 * k, iy, 26 * k, lab, font (11 * k), P["ink"]); text_r (img, w - 16 * k, iy, 26 * k, sc, font (9 * k), P["ink2"])
		if i < len (items) - 1 and i != 1 and i != 2: hline (img, 16 * k, iy + 26 * k, w - 32 * k, (222, 222, 224))
		iy += 28 * k
	slate_softkeys (img, w, h, [(g_grid, "Home"), (g_tasks, "Tasks"), (g_menu, "Close"), (g_kbd, "Keys")])
	return img

# ===========================================================================================================
# Pocket is ONE adaptive layout: logical units x a scale factor, rules that reflow by the logical size
# (docs/COMPACT-SHELL-STUDY.md section 6.1). The same three screens at any size and orientation.
# ===========================================================================================================
def layout_of (W, H, k):
	"""The rules: the logical size decides; landscape vs portrait by the aspect."""
	lw, lh = W / k, H / k
	L = dict (lw = lw, lh = lh, portrait = lh > lw * 1.05)
	L["inline_menus"] = lw >= 560				# the menus in the bar, else one ☰ (portrait: a sheet)
	L["lite_bar"] = lw < 420				# the bar keeps Wi-Fi, battery, time only
	L["hints"] = lw >= 700					# the key hints beside the search field
	L["running"] = not L["portrait"] and lh >= 440		# the Running strip on the launcher
	L["softkeys"] = L["portrait"]				# Home, Tasks, Menu, Keys (devices without the keys)
	L["tile"] = (94, 84, 48) if lw >= 600 else (80, 76, 40)	# tile w, h, icon
	L["tab_h"] = 32 if lh >= 420 else 28
	return L

def adaptive_home (W, H, k = 1, cat = "Productivity", focus = 6):
	L = layout_of (W, H, k); u = lambda v: int (v * k)
	img = wallpaper (W, H, seed = 7)
	statusbar (img, W, home = True, k = k, compact = not L["inline_menus"], lite = L["lite_bar"])
	bottom = u (26) if L["softkeys"] else 0
	y = u (BAR + 8)
	fw = u (300) if L["hints"] else W - u (24)
	rrect (img, u (12), y, fw, u (26), u (13), P["field"]); ring (img, u (12), y, fw, u (26), u (13), (0, 0, 0), alpha = 60)
	glyph (img, u (21), y + u (6), u (14), u (14), lambda d, s: g_search (d, s, k), (110, 110, 116))
	text_l (img, u (40), y, u (26), "Type to find" if L["lw"] < 420 else "Type to find an app, a file, a setting", font (u (12)), (140, 138, 136))
	if L["hints"]:
		hx = 330; hx = hint (img, hx, y + 4, ["Tab"], "category"); hx = hint (img, hx, y + 4, ["Arrows"], "choose"); hint (img, hx, y + 4, ["Enter"], "open")
	ty = y + u (34)
	tabs_row (img, u (8), ty, W - u (16), CATS, cat, k = k, h = L["tab_h"], font_size = 12 if L["lw"] >= 420 else 11)
	py = ty + u (L["tab_h"])
	run_h = u (68) if L["running"] else 0
	ph = H - py - run_h - bottom - u (8)
	rrect (img, u (8), py, W - u (16), ph, u (10), ((250, 250, 251), (228, 230, 234)), corners = (False, True, True, True))
	ring (img, u (8), py, W - u (16), ph, u (10), (0, 0, 0), alpha = 60, corners = (False, True, True, True))
	tw0, th0, isz = L["tile"]
	cols = max (3, int ((L["lw"] - 24) // tw0)); tile_w = (W - u (24)) // cols
	apps = apps_in (cat)
	running = {"tinypad", "ledger", "terminal", "fileviewer", "telegram"}
	rows_fit = max (1, int ((ph - u (8)) // u (th0)))
	if focus >= rows_fit * cols: focus = rows_fit * cols - 2		# (the focus on a visible tile)
	for i, a in enumerate (apps):
		r, c = divmod (i, cols)
		if u (8) + (r + 1) * u (th0) > ph: break
		tile (img, u (12) + c * tile_w, py + u (6) + r * u (th0), tile_w - u (2), u (th0) - u (4), a, app_name (a), focus = i == focus,
		      running = a in running, k = k, isz = u (isz), small = L["lw"] < 600)
	if L["running"]:
		sy = H - u (64)
		text_l (img, u (14), sy, u (18), "Running", font (u (11), True), (210, 220, 234))
		x = u (14); cy = sy + u (22)
		for a in ["terminal", "ledger", "tinypad", "fileviewer", "telegram"]:
			n = app_name (a); cw = u (34) + tw (n, font (u (12))) + u (30)
			if x + cw > W - u (10): break
			rrect (img, x, cy, cw, u (34), u (9), P["silver"]); ring (img, x, cy, cw, u (34), u (9), (0, 0, 0), alpha = 80)
			draw_icon (img, a, x + u (6), cy + u (5), u (24)); text_l (img, x + u (36), cy, u (34), n, font (u (12)), P["ink"])
			glyph (img, x + cw - u (20), cy + u (12), u (10), u (10), lambda d, s: g_close (d, s, k), (120, 116, 112))
			x += cw + u (8)
	if L["softkeys"]: slate_softkeys (img, W, H, [(g_grid, "Home"), (g_tasks, "Tasks"), (g_menu, "Menu"), (g_kbd, "Keys")], k = k)
	return img

def adaptive_app (W, H, k = 1):
	L = layout_of (W, H, k); u = lambda v: int (v * k)
	img = Image.new ("RGB", (W, H), P["face"])
	statusbar (img, W, title = "Text Editor", menus = ("File", "Edit", "Search", "View"), k = k, compact = not L["inline_menus"], lite = L["lite_bar"])
	bottom = u (26) if L["softkeys"] else 0
	lines = ["The compact shell -- notes", "", "- one app, the whole screen", "- the menu bar stays", "- Alt+Tab: the switcher",
		 "- two apps side by side", "  when the screen is wide", "- the clock: quick settings", "", "TODO: UIKit's compact metrics"]
	editor_screen (img, 0, u (BAR), W, H - u (BAR) - bottom, k = k, fs = 12 if L["lw"] >= 420 else 10, lines = lines)
	if L["softkeys"]: slate_softkeys (img, W, H, [(g_grid, "Home"), (g_tasks, "Tasks"), (g_menu, "Menu"), (g_kbd, "Keys")], k = k)
	return img

def _thumb (a, w, h):
	if a == "terminal":
		t = Image.new ("RGB", (PW, PH - BAR), P["term"]); terminal_screen (t, 0, 0, PW, PH - BAR)
	elif a == "tinypad":
		t = Image.new ("RGB", (PW, PH - BAR), P["face"]); editor_screen (t, 0, 0, PW, PH - BAR)
	else: t = shot ({"ledger": "ledger", "fileviewer": "fileviewer", "telegram": "telegram"}[a])
	t = t.resize ((w, max (h, int (w * t.height / t.width))), Image.LANCZOS)
	return t.crop ((0, 0, w, h))

def adaptive_switcher (W, H, k = 1):
	L = layout_of (W, H, k); u = lambda v: int (v * k)
	img = dim (adaptive_app (W, H, k), 252)
	statusbar (img, W, title = "Text Editor", k = k, compact = True, lite = L["lite_bar"])
	cards = ["tinypad", "ledger", "terminal", "fileviewer", "telegram"]
	if not L["portrait"]:
		# a row: the next one bigger; as many as fit
		small, big, gap = u (136) if L["lw"] >= 700 else u (112), u (196) if L["lw"] >= 700 else u (164), u (10)
		n = len (cards)
		while n > 2 and big + (n - 1) * (small + gap) > W - u (16): n -= 1
		x = (W - big - (n - 1) * (small + gap)) // 2; mid = H // 2 - u (14)
		text_c (img, 0, u (BAR + 20), W, u (24), "Open apps", font (u (15), True), (236, 240, 246))
		for i, a in enumerate (cards[:n]):
			cw = big if i == 1 else small; ch = int (cw * 0.62); cy = mid - ch // 2; sel = i == 1
			if sel: rrect (img, x - u (7), cy - u (7), cw + u (14), ch + u (46), u (12), (lighten (P["accent"], 0.15), shade (P["accent"], 0.92)))
			img.paste (_thumb (a, cw, ch), (x, cy)); ring (img, x, cy, cw, ch, 3, (0, 0, 0), alpha = 120)
			draw_icon (img, a, x, cy + ch + u (8), u (22))
			f = font (u (12), sel); text_l (img, x + u (28), cy + ch + u (8), u (22), ellipsize (app_name (a), f, cw - u (30)), f, (255, 255, 255))
			x += cw + gap
		y = H - u (44); x = u (20)
		if L["lw"] >= 600:
			x = hint (img, x, y, ["Alt", "Tab"], "next"); x = hint (img, x, y, ["Del"], "close"); hint (img, x, y, ["Esc"], "stay")
	else:
		# a column: rows with a thumbnail, the next one lit; Tasks key / arrows, OK
		text_l (img, u (12), u (BAR + 6), u (22), "Open apps", font (u (13), True), (236, 240, 246))
		y = u (BAR + 32); rh = u (64); tw0 = u (84)
		for i, a in enumerate (cards):
			if y + rh > H - u (30): break
			sel = i == 1
			if sel: rrect (img, u (6), y - u (3), W - u (12), rh, u (9), (lighten (P["accent"], 0.15), shade (P["accent"], 0.92)))
			img.paste (_thumb (a, tw0, u (52)), (u (12), y + u (3))); ring (img, u (12), y + u (3), tw0, u (52), 2, (0, 0, 0), alpha = 120)
			draw_icon (img, a, u (12) + tw0 + u (8), y + u (8), u (20))
			text (img, u (12) + tw0 + u (34), y + u (8), app_name (a), font (u (12), True), (255, 255, 255))
			text (img, u (12) + tw0 + u (8), y + u (32), ["now", "Overview", "~ (2 tabs)", "SD:/docs", "2 unread"][i], font (u (10)), (220, 232, 246) if sel else (160, 176, 196))
			y += rh + u (6)
	if L["softkeys"]: slate_softkeys (img, W, H, [(g_grid, "Home"), (g_tasks, "Tasks"), (g_menu, "Menu"), (g_kbd, "Keys")], k = k)
	return img

ADAPT_SIZES = [("800 x 480, 1x", 800, 480, 1), ("640 x 480, 1x", 640, 480, 1), ("480 x 800, 1.5x (320 x 533)", 480, 800, 1.5),
	       ("480 x 640, 2x (240 x 320)", 480, 640, 2)]

def adaptive_sheet (sizes, by_rows, title):
	"""The same three screens (Home, an app, the switcher) at several sizes."""
	screens = [("Home", adaptive_home), ("An app (Text Editor)", adaptive_app), ("The switcher", adaptive_switcher)]
	pics = [[fn (W, H, k) for _, fn in screens] for _, W, H, k in sizes]
	gap, pad, head = 22, 24, 44
	cellw = max (W for _, W, H, k in sizes); cellh = max (H for _, W, H, k in sizes)
	rowy = [pad + head]
	for _, W, H, k in sizes: rowy.append (rowy[-1] + H + gap + 22)
	if by_rows:						# a row per size, a column per screen
		Wd = pad * 2 + 3 * cellw + 2 * gap; Hd = rowy[-1]
	else:							# a column per size, a row per screen
		Wd = pad * 2 + len (sizes) * (cellw + gap) - gap; Hd = pad + head + 3 * (cellh + gap + 22)
	out = Image.new ("RGB", (int (Wd), int (Hd)), (236, 236, 238))
	text (out, pad, pad - 6, title, font (20, True), P["ink"])
	for si, (lab, W, H, k) in enumerate (sizes):
		for j, (sname, _) in enumerate (screens):
			pic = pics[si][j]
			if by_rows: x = pad + j * (cellw + gap); y = rowy[si]
			else: x = pad + si * (cellw + gap); y = pad + head + j * (cellh + gap + 22)
			out.paste (pic, (x, y + 22)); ring (out, x - 1, y + 21, pic.width + 2, pic.height + 2, 3, (60, 60, 66), alpha = 160)
			if (by_rows and si == 0) or (not by_rows and j == 0):
				pass
			text (out, x, y + 2, (sname + "  --  " + lab) if True else sname, font (12, True), P["ink2"])
	return out

# ===========================================================================================================
# Concept A -- "Tabs" (Qtopia on the Zaurus): 640 x 480 clamshell
# ===========================================================================================================
def concept_a ():
	w, h = 640, 480
	img = Image.new ("RGB", (w, h), (228, 228, 230))
	# no menu bar: the tabs at the top, the taskbar at the bottom (Qtopia's)
	tabs = [("Applications", (110, 148, 196)), ("Games", (196, 104, 100)), ("Settings", (150, 138, 104)), ("Documents", (120, 166, 112))]
	img.paste (grad (w, 38, (214, 216, 222), (196, 198, 206)), (0, 0))
	tabs_row (img, 6, 6, w - 12, tabs, "Applications", h = 32, scroll = False)
	rrect (img, 0, 38, w, h - 38 - 30, 0, ((250, 250, 251), (228, 230, 234)))
	apps = ["tinypad", "letters", "sheet", "calendar", "notes", "clock", "tinycalc", "ledger", "pdf", "archiver",
		"jet", "mail", "telegram", "terminal", "fileviewer", "photos", "media", "paint", "cardfile", "slides"]
	cols = 7; cw = (w - 16) // cols; chh = 84
	for i, a in enumerate (apps):
		r, c = divmod (i, cols)
		if 44 + (r + 1) * chh > h - 32: break
		tile (img, 8 + c * cw, 46 + r * chh, cw - 4, chh - 4, a, app_name (a), focus = i == 9)
	# the taskbar
	ty = h - 30
	img.paste (grad (w, 30, (222, 222, 224), (192, 192, 194)), (0, ty)); hline (img, 0, ty, w, (150, 146, 142))
	rrect (img, 4, ty + 3, 44, 24, 6, (lighten (P["accent"], 0.1), shade (P["accent"], 0.9)))
	put (img, 9, ty + 8, mask (14, 14, lambda d, s: g_gem (d, s, 0.85)), (255, 255, 255)); text_l (img, 25, ty + 3, 24, "Go", font (11, True), (255, 255, 255))
	x = 56
	for a in ["terminal", "ledger", "telegram"]:
		rrect (img, x, ty + 3, 30, 24, 5, (255, 255, 255), alpha = 120 if a != "ledger" else 230); draw_icon (img, a, x + 3, ty + 3, 24); x += 34
	xr = w - 6
	text_r (img, xr, ty, 30, "12:34", font (12, True), P["ink"]); xr -= 48
	battery (img, xr - 22, ty + 9, 78, P["ink"]); xr -= 30
	glyph (img, xr - 16, ty + 8, 16, 14, lambda d, s: g_wifi (d, s), P["ink"]); xr -= 26
	glyph (img, xr - 16, ty + 7, 16, 16, lambda d, s: g_kbd (d, s), P["ink"])
	return img

# ===========================================================================================================
# Concept B -- "Netbook" (Ubuntu Netbook Remix / Moblin): 1280 x 720
# ===========================================================================================================
def concept_b ():
	w, h = 1280, 720
	img = wallpaper (w, h, seed = 11)
	# the top panel: the Onyx button, the open windows as tabs (Maximus: no title bars), the status
	ph = 34
	img.paste (grad (w, ph, (52, 58, 70), (30, 34, 42)), (0, 0))
	rrect (img, 6, 4, 98, 26, 7, (lighten (P["accent"], 0.1), shade (P["accent"], 0.9)))
	put (img, 14, 10, mask (16, 16, lambda d, s: g_gem (d, s, 0.95)), (255, 255, 255)); text_l (img, 36, 4, 26, "Onyx", font (13, True), (255, 255, 255))
	x = 116
	for i, a in enumerate (["terminal", "ledger", "jet", "telegram"]):
		n = app_name (a); tw0 = 44 + tw (n, font (12))
		rrect (img, x, 5, tw0, 29, 7, ((96, 104, 120), (70, 76, 90)) if i == 1 else ((62, 68, 80), (44, 48, 58)), corners = (True, True, False, False))
		draw_icon (img, a, x + 8, 9, 20); text_l (img, x + 34, 5, 29, n, font (12, i == 1), (236, 240, 246))
		x += tw0 + 4
	xr = w - 10
	for s in ["12:34"]: text_r (img, xr, 0, ph, s, font (13, True), (236, 240, 246)); xr -= 60
	battery (img, xr - 22, 11, 78, (236, 240, 246)); xr -= 34
	glyph (img, xr - 16, 10, 16, 14, lambda d, s: g_speaker (d, s), (236, 240, 246)); xr -= 26
	glyph (img, xr - 16, 10, 16, 14, lambda d, s: g_wifi (d, s), (236, 240, 246))
	# the sidebar: the categories
	sx, sy, sw = 16, ph + 18, 220
	rrect (img, sx, sy, sw, h - sy - 16, 12, (247, 247, 249), alpha = 235)
	rrect (img, sx + 10, sy + 10, sw - 20, 28, 14, P["field"]); ring (img, sx + 10, sy + 10, sw - 20, 28, 14, (0, 0, 0), alpha = 60)
	glyph (img, sx + 20, sy + 17, 14, 14, lambda d, s: g_search (d, s), (110, 110, 116)); text_l (img, sx + 40, sy + 10, 28, "Search", font (12), (140, 138, 136))
	cy = sy + 48
	for n, col in CATS:
		if n == "Recent": n = "Favourites"
		on = n == "Favourites"
		if on: rrect (img, sx + 8, cy, sw - 16, 34, 8, P["accent"])
		rrect (img, sx + 16, cy + 10, 14, 14, 4, col)
		text_l (img, sx + 40, cy, 34, n, font (13), (255, 255, 255) if on else P["ink"])
		cy += 38
	cy += 8; hline (img, sx + 16, cy, sw - 32, (206, 206, 208)); cy += 10
	for n in ["Files", "Shut down"]:
		text_l (img, sx + 40, cy, 34, n, font (13), P["ink"]); cy += 36
	# the grid: big icons
	gx, gy = sx + sw + 20, sy
	gw = w - gx - 16 - 260
	rrect (img, gx, gy, gw, h - gy - 16, 12, (247, 247, 249), alpha = 200)
	text (img, gx + 18, gy + 14, "Favourites", font (16, True), P["ink"])
	apps = ["jet", "mail", "telegram", "letters", "sheet", "ledger", "photos", "media", "paint", "terminal", "fileviewer",
		"calendar", "notes", "pdf", "gamelib", "qbstudio"]
	cols = 6; cw = (gw - 30) // cols; chh = 128
	for i, a in enumerate (apps):
		r, c = divmod (i, cols)
		if 50 + (r + 1) * chh > h - gy - 16: break
		x0 = gx + 15 + c * cw; y0 = gy + 48 + r * chh
		if i == 5: rrect (img, x0 + 4, y0, cw - 8, chh - 8, 12, P["accent"])
		draw_icon (img, a, x0 + (cw - 64) // 2, y0 + 16, 64)
		text_c (img, x0, y0 + 90, cw, 20, app_name (a), font (13, i == 5), (255, 255, 255) if i == 5 else P["ink"])
	# the right column: running, recent documents
	rx = w - 16 - 244; ry = sy
	rrect (img, rx, ry, 244, h - ry - 16, 12, (247, 247, 249), alpha = 235)
	text (img, rx + 16, ry + 14, "Recent documents", font (13, True), P["ink"])
	for i, (n, a) in enumerate ([("budget-2026.ods", "sheet"), ("lettre-banque.odt", "letters"), ("notes.txt", "tinypad"),
				     ("q3-vat.pdf", "pdf"), ("holiday.jpg", "photos"), ("compact.txt", "tinypad")]):
		yy = ry + 44 + i * 44
		draw_icon (img, a, rx + 14, yy + 4, 28); text (img, rx + 52, yy + 4, n, font (12), P["ink"])
		text (img, rx + 52, yy + 21, ["2 h ago", "yesterday", "yesterday", "Monday", "1 Oct", "today"][i], font (10), P["ink2"])
	return img

# ===========================================================================================================
# Console mode -- the PlayStation 2's system browser as the mood: deep space, floating light, glowing
# translucent towers, big thin words, gamepad first (640 x 480: a Pi handheld, or a TV)
# ===========================================================================================================
CW, CH = 640, 480
GLOW = P["accent"]					# Milk's Aqua blue, as the glow
_cf = {}
def cfont (size, weight = "light"):
	"""Selawik (on the card, sdcard/res/fonts): light for the big words, semibold for the chosen one."""
	k = (size, weight)
	if k not in _cf:
		n = {"light": "selawkl.ttf", "semi": "selawksl.ttf", "regular": "selawk.ttf", "bold": "selawksb.ttf"}[weight]
		p = os.path.join (SD, "res", "fonts", n)
		_cf[k] = ImageFont.truetype (p, size) if os.path.exists (p) else font (size, weight == "bold")
	return _cf[k]

def glow_text (img, x, y, s, f, colour = (255, 255, 255), glow = None, radius = 6, strength = 2, anchor = "la"):
	"""Text with a soft halo (a blurred copy in the glow colour under it)."""
	glow = glow or GLOW
	lay = Image.new ("L", img.size, 0); ImageDraw.Draw (lay).text ((x, y), s, font = f, fill = 255, anchor = anchor)
	halo = lay.filter (ImageFilter.GaussianBlur (radius))
	for _ in range (strength): img.paste (glow, (0, 0), halo)
	ImageDraw.Draw (img).text ((x, y), s, font = f, fill = colour, anchor = anchor)

def add_glow (img, layer_rgb, mask_l, radius):
	"""layer (RGB) through mask, blurred, added (screen-like) onto img."""
	m = mask_l.filter (ImageFilter.GaussianBlur (radius))
	a = np.asarray (img).astype (float); l = np.asarray (layer_rgb).astype (float); mm = np.asarray (m).astype (float)[..., None] / 255
	return Image.fromarray (np.clip (a + l * mm, 0, 255).astype ("uint8"), "RGB")

def space (w, h, seed = 2, towers = True, haze = True):
	"""The PS2 browser's world: deep blue to black, a floor of haze, glowing translucent towers of cubes
	receding into the dark, soft floating motes."""
	img = grad (w, h, (2, 3, 10), (8, 18, 48))
	rnd = random.Random (seed)
	if haze:					# a horizon of light, low
		hz = Image.new ("L", (w, h), 0); d = ImageDraw.Draw (hz)
		d.ellipse ([-w * 0.3, h * 0.62, w * 1.3, h * 1.25], fill = 90)
		img = add_glow (img, Image.new ("RGB", (w, h), (30, 70, 150)), hz, h / 6)
	if towers:
		tl = Image.new ("RGB", (w, h), (0, 0, 0)); tm = Image.new ("L", (w, h), 0)
		dl = ImageDraw.Draw (tl); dm = ImageDraw.Draw (tm)
		hor = h * 0.66
		cols = sorted ([(rnd.uniform (0.12, 0.75), rnd.uniform (-0.05, 1.05)) for _ in range (20)])	# (depth, x)
		for depth, fx in cols:					# far first
			sz = 4 + 22 * depth; x = fx * w; base = hor + (h - hor) * (depth - 0.12) * 0.8
			n = rnd.randint (2, 9)
			for i in range (n):
				y = base - (i + 1) * sz * 1.08
				lum = int (40 + 150 * depth * (0.5 + 0.5 * rnd.random ()))
				c = (int (lum * 0.45), int (lum * 0.75), lum)
				dx = sz * 0.35
				dl.polygon ([(x, y), (x + sz, y), (x + sz + dx, y - dx * 0.6), (x + dx, y - dx * 0.6)], fill = lighten (c, 0.3))	# top
				dl.rectangle ([x, y, x + sz, y + sz], fill = c)
				dl.polygon ([(x + sz, y), (x + sz + dx, y - dx * 0.6), (x + sz + dx, y + sz - dx * 0.6), (x + sz, y + sz)], fill = shade (c, 0.6))
				a = int (40 + 110 * depth)
				dm.polygon ([(x, y), (x + sz, y), (x + sz + dx, y - dx * 0.6), (x + dx, y - dx * 0.6)], fill = a)
				dm.rectangle ([x, y, x + sz, y + sz], fill = a)
				dm.polygon ([(x + sz, y), (x + sz + dx, y - dx * 0.6), (x + sz + dx, y + sz - dx * 0.6), (x + sz, y + sz)], fill = a)
				dl.rectangle ([x, y, x + sz, y + sz], outline = lighten (c, 0.5))
		img = add_glow (img, tl, tm.point (lambda v: v * 0.7), 6)	# their glow
		img.paste (tl, (0, 0), tm.point (lambda v: v * 0.42))	# and themselves, translucent
	# floating motes
	ml = Image.new ("L", (w, h), 0); d = ImageDraw.Draw (ml)
	for _ in range (int (w * h / 2500)):
		x, y = rnd.uniform (0, w), rnd.uniform (0, h); r = rnd.choice ([0.6, 0.8, 1, 1.2, 1.6, 2.4])
		d.ellipse ([x - r, y - r, x + r, y + r], fill = rnd.randint (60, 255))
	img = add_glow (img, Image.new ("RGB", (w, h), (120, 180, 255)), ml, 2.5)
	img.paste ((220, 236, 255), (0, 0), ml.point (lambda v: v * 0.6))
	return img

def glass (img, x, y, w, h, r = 10, alpha = 70, edge = 150, colour = (120, 170, 255)):
	"""A translucent panel: a faint blue fill darker at the bottom, a bright thin edge, a sheen on top."""
	rrect (img, x, y, w, h, r, ((20, 40, 90), (6, 12, 34)), alpha = alpha + 60)
	rrect (img, x + 2, y + 2, w - 4, h * 0.45, r - 2, (lighten (colour, 0.5), colour), alpha = 14, corners = (True, True, False, False))
	ring (img, x, y, w, h, r, lighten (colour, 0.4), alpha = edge)

def glow_box (img, x, y, w, h, r = 10, colour = None, strength = 1.0):
	colour = colour or GLOW
	m = Image.new ("L", img.size, 0); ImageDraw.Draw (m).rounded_rectangle ([x, y, x + w, y + h], r, outline = 255, width = 3)
	out = add_glow (img, Image.new ("RGB", img.size, tuple (int (c * strength) for c in lighten (colour, 0.2))), m, 7)
	rrect (out, x, y, w, h, r, ((80, 150, 255), (30, 70, 170)), alpha = 70)
	ring (out, x, y, w, h, r, (200, 230, 255), t = 1.5, alpha = 230)
	return out

def pad_btn (img, x, y, kind, d = 20):
	"""The pad's face buttons as shapes (cross = confirm, circle = back, triangle, square), a dark disc."""
	rrect (img, x, y, d, d, d / 2, ((40, 44, 56), (16, 18, 24))); ring (img, x, y, d, d, d / 2, (150, 160, 180), alpha = 160)
	col = {"x": (120, 170, 255), "o": (255, 110, 110), "t": (110, 220, 160), "s": (240, 140, 220)}[kind]
	u = d / 20
	def fn (dd, s):
		if kind == "x": dd.line ([6 * u * s, 6 * u * s, 14 * u * s, 14 * u * s], fill = 255, width = int (2 * u * s)); dd.line ([14 * u * s, 6 * u * s, 6 * u * s, 14 * u * s], fill = 255, width = int (2 * u * s))
		if kind == "o": dd.ellipse ([5.5 * u * s, 5.5 * u * s, 14.5 * u * s, 14.5 * u * s], outline = 255, width = int (2 * u * s))
		if kind == "t": dd.polygon ([(10 * u * s, 5 * u * s), (15 * u * s, 14 * u * s), (5 * u * s, 14 * u * s)], outline = 255, width = int (2 * u * s))
		if kind == "s": dd.rectangle ([6 * u * s, 6 * u * s, 14 * u * s, 14 * u * s], outline = 255, width = int (2 * u * s))
	glyph (img, x, y, d, d, fn, col)

def shoulder_w (lab): return max (30, tw (lab, font (10, True)) + 12)
def shoulder (img, x, y, lab):
	w = shoulder_w (lab)
	rrect (img, x, y, w, 18, 5, ((60, 66, 80), (26, 28, 36))); ring (img, x, y, w, 18, 5, (150, 160, 180), alpha = 160)
	text_c (img, x, y - 1, w, 18, lab, font (10, True), (220, 228, 240))
	return w + 4

def console_hints (img, items, y = None):
	"""The bottom line: the buttons and what they do, centred."""
	y = y or CH - 34
	f = cfont (15, "regular")
	widths = []
	for kind, word in items: widths.append ((26 if kind in ("x", "o", "t", "s") else sum (shoulder_w (l) + 4 for l in kind.split ("/"))) + tw (word, f) + 22)
	x = (CW - sum (widths)) // 2
	for (kind, word), wd in zip (items, widths):
		if kind in ("x", "o", "t", "s"): pad_btn (img, x, y, kind); x2 = x + 26
		else:
			x2 = x
			for lab in kind.split ("/"): x2 += shoulder (img, x2, y + 1, lab)
		text (img, x2, y + 1, word, f, (214, 224, 240)); x = x + wd

def console_top (img, title = None, gem = True):
	"""The top line: the Onyx gem and a title at the left, the time and the battery at the right -- faint."""
	if gem: put (img, 22, 20, mask (16, 16, lambda d, s: g_gem (d, s, 1)), (170, 200, 240))
	if title: glow_text (img, 46, 15, title, cfont (20, "light"), (230, 240, 255), radius = 4, strength = 1)
	text_r (img, CW - 22, 14, 24, "12:34", cfont (18, "regular"), (210, 222, 240))
	battery (img, CW - 102, 20, 78, (180, 196, 220))

def console_home (sel = 0):
	img = space (CW, CH, seed = 4)
	console_top (img)
	# the big words, a column at the left (the PS2's "Browser / System Configuration")
	items = [("Games", "18 games  -  6 systems"), ("Media", "music, videos, photos"), ("Apps", "the desktop's apps, full screen"),
		 ("Files", "the SD card, USB, the network"), ("Settings", "screen, sound, pad, Wi-Fi")]
	y0 = 104
	for i, (word, sub) in enumerate (items):
		y = y0 + i * 64 + (8 if i > sel else 0)
		if i == sel:
			img = glow_box (img, 40, y - 6, 300, 64, 12)
			glow_text (img, 62, y - 2, word, cfont (34, "semi"), (255, 255, 255), radius = 8, strength = 2)
			text (img, 64, y + 36, sub, cfont (14, "regular"), (190, 214, 245))
		else:
			glow_text (img, 62, y + 4, word, cfont (28, "light"), (176, 196, 226), radius = 5, strength = 1, glow = (30, 60, 120))
	# at the right, what the chosen item holds: the last played, as memory-card tiles floating
	glass (img, 384, 98, 236, 268, 12, alpha = 40, edge = 90)
	glow_text (img, 400, 108, "Last played", cfont (16, "regular"), (200, 220, 245), radius = 3, strength = 1)
	for i, (a, name, sysn) in enumerate ([("doom", "Doom", "Onyx"), ("gbaemu", "Star Courier", "GBA"), ("snesemu", "Moon Garden", "SNES")]):
		y = 140 + i * 82
		tile3d (img, 404, y, 64, a, sysn, focus = False)
		text (img, 482, y + 12, name, cfont (19, "regular"), (226, 236, 252))
		text (img, 482, y + 38, ["yesterday", "Monday  -  slot 2", "1 Oct"][i], cfont (13, "regular"), (150, 176, 214))
	console_hints (img, [("x", "Enter"), ("o", "Back"), ("t", "Options"), ("L1/R1", "Section")])
	return img

def tile3d (img, x, y, s, app, badge = None, focus = False, label = None):
	"""A game's tile, like a memory card's save icon: a glossy rounded block with depth, the icon in it."""
	d = int (s * 0.14)
	rrect (img, x + d, y - d, s, s, s * 0.16, ((70, 110, 190), (20, 40, 90)), alpha = 150)	# the back face (depth)
	rrect (img, x, y, s, s, s * 0.16, ((70, 100, 160), (14, 24, 54)) if not focus else ((120, 170, 255), (30, 70, 170)))
	ring (img, x, y, s, s, s * 0.16, (200, 225, 255), alpha = 120 if not focus else 240, t = 1.2)
	rrect (img, x + 3, y + 3, s - 6, s * 0.4, s * 0.12, (255, 255, 255), alpha = 30, corners = (True, True, False, False))
	isz = int (s * 0.62); draw_icon (img, app, x + (s - isz) // 2, y + (s - isz) // 2, isz)
	if badge:
		f = font (max (8, int (s * 0.13)), True); bw = tw (badge, f) + 8
		rrect (img, x + s - bw - 3, y + s - 15, bw, 13, 4, (8, 14, 30), alpha = 220); text_c (img, x + s - bw - 3, y + s - 16, bw, 13, badge, f, (200, 220, 255))

LIB = [("doom", "Doom", "Onyx"), ("tetris", "Tetris", "Onyx"), ("pinball", "Pinball", "Onyx"), ("critters", "Critters", "Onyx"),
       ("gbemu", "Pixel Knight", "GB"), ("gbemu", "Tobi's Island", "GBC"), ("gbaemu", "Star Courier", "GBA"), ("gbaemu", "Rune Valley", "GBA"),
       ("nesemu", "Micro Quest", "NES"), ("snesemu", "Moon Garden", "SNES"), ("n64emu", "Turbo Kart 64", "N64"), ("gcemu", "Ocean Gate", "GC")]

def console_library (sel = 6):
	img = space (CW, CH, seed = 9, towers = False)
	console_top (img, "Games")
	# the sections, switched with L1 / R1
	secs = ["All", "Onyx", "Game Boy", "GBA", "NES", "SNES", "N64", "GC"]
	x = 56; y = 52
	shoulder (img, 8, y + 3, "L1")
	for i, sname in enumerate (secs):
		f = cfont (16, "semi" if i == 0 else "regular"); ww = tw (sname, f)
		if i == 0: rrect (img, x - 8, y, ww + 16, 24, 12, ((90, 150, 250), (40, 90, 200)), alpha = 200)
		text (img, x, y + 2, sname, f, (255, 255, 255) if i == 0 else (160, 182, 214)); x += ww + 22
	shoulder (img, CW - 38, y + 3, "R1")
	# the grid of tiles, the chosen one bigger, glowing
	cols = 6; s = 64; gx = 46; gy = 104; stepx = 96; stepy = 100
	for i, (a, name, sysn) in enumerate (LIB):
		r, c = divmod (i, cols); x = gx + c * stepx; y = gy + r * stepy
		if i == sel: continue
		tile3d (img, x, y, s, a, sysn)
		text_c (img, x - 14, y + s + 4, s + 28, 18, ellipsize (name, cfont (13, "regular"), s + 26), cfont (13, "regular"), (170, 192, 224))
	r, c = divmod (sel, cols); x = gx + c * stepx - 10; y = gy + r * stepy - 10
	a, name, sysn = LIB[sel]
	m = Image.new ("L", img.size, 0); ImageDraw.Draw (m).rounded_rectangle ([x - 4, y - 4, x + 88, y + 88], 16, fill = 160)
	img = add_glow (img, Image.new ("RGB", img.size, GLOW), m, 10)
	tile3d (img, x, y, 84, a, sysn, focus = True)
	# the chosen game's details, a glass panel at the bottom
	py = 316
	glass (img, 30, py, CW - 60, 104, 12)
	glow_text (img, 50, py + 10, name, cfont (26, "semi"), (255, 255, 255), radius = 5, strength = 1)
	text (img, 52, py + 46, "Game Boy Advance  -  SD:/roms/gba", cfont (14, "regular"), (176, 200, 236))
	text (img, 52, py + 68, "Played 6 h 40 min  -  last: Monday", cfont (14, "regular"), (176, 200, 236))
	for i in range (3):					# the save states, like memory-card slots
		sx = CW - 60 - 24 - (2 - i) * 62
		rrect (img, sx, py + 18, 54, 40, 6, ((60, 90, 150), (20, 34, 70)) if i < 2 else (18, 26, 48))
		ring (img, sx, py + 18, 54, 40, 6, (170, 200, 245), alpha = 140)
		text_c (img, sx, py + 60, 54, 16, ["slot 1", "slot 2", "empty"][i], cfont (12, "regular"), (160, 186, 222))
		if i < 2: draw_icon (img, "gbaemu", sx + 15, py + 26, 24)
	console_hints (img, [("x", "Play"), ("s", "Save states"), ("t", "Options"), ("o", "Back"), ("L1/R1", "System")])
	return img

def console_settings (sel = 2):
	img = space (CW, CH, seed = 12, towers = True)
	img = dim (img, 90)
	console_top (img, "System Configuration")
	rows = [("Clock", "12:34  -  Brussels (UTC+2)"), ("Screen", "640 x 480  -  scale 1x"), ("Language", "Français"),
		("Sound", "volume 70 %  -  the jack"), ("Gamepad", "USB pad 1: mapped"), ("Wi-Fi", "Maison-5G"), ("Packages", "3 updates"),
		("Mode", "Console  (desktop, pocket)"), ("About", "Onyx 2026.10.92  -  kapi v94")]
	glass (img, 60, 62, CW - 120, 350, 14)
	y = 76
	for i, (k_, v) in enumerate (rows):
		if i == sel:
			img = glow_box (img, 72, y - 3, CW - 144, 34, 8)
			text (img, 92, y + 3, k_, cfont (19, "semi"), (255, 255, 255)); f_ = cfont (17, "regular"); text (img, CW - 92 - tw (v, f_), y + 5, v, f_, (255, 255, 255))
			glyph (img, CW - 86, y + 9, 10, 10, lambda d, s: g_chev (d, s, 1, "right"), (220, 236, 255))
		else:
			text (img, 92, y + 3, k_, cfont (19, "light"), (200, 214, 236)); f_ = cfont (16, "regular"); text (img, CW - 92 - tw (v, f_), y + 6, v, f_, (140, 166, 204))
		y += 37
	console_hints (img, [("x", "Change"), ("o", "Back"), ("t", "Desktop's Control Panel")])
	return img

def game_screen (w, h):
	"""A made-up pixel-art game (a side-scroller) at a console's size: the emulator's picture."""
	g = Image.new ("RGB", (160, 120), (96, 168, 248)); d = ImageDraw.Draw (g)
	for i, (cx, cy, r) in enumerate ([(30, 24, 8), (40, 22, 9), (52, 25, 7), (110, 16, 7), (120, 14, 9), (130, 17, 6)]): d.ellipse ([cx - r, cy - r // 2, cx + r, cy + r // 2], fill = (248, 248, 255))
	d.polygon ([(0, 90), (30, 60), (55, 85), (85, 50), (120, 88), (160, 66), (160, 120), (0, 120)], fill = (70, 150, 110))
	d.rectangle ([0, 96, 160, 120], fill = (150, 96, 50)); [d.rectangle ([x, 96, x + 7, 99], fill = (90, 190, 70)) for x in range (0, 160, 8)]
	for x in (60, 68, 76): d.rectangle ([x, 64, x + 7, 71], fill = (230, 160, 40)); d.rectangle ([x + 1, 65, x + 6, 70], outline = (130, 70, 10))
	d.rectangle ([30, 84, 37, 95], fill = (220, 50, 50)); d.rectangle ([31, 80, 36, 85], fill = (250, 210, 170)); d.rectangle ([30, 79, 37, 81], fill = (220, 50, 50))
	d.rectangle ([100, 88, 109, 95], fill = (120, 60, 160)); d.point ([(102, 90), (106, 90)], fill = (255, 255, 255))
	for i in range (5): d.rectangle ([4 + i * 7, 4, 9 + i * 7, 9], fill = (255, 230, 60))
	return g.resize ((w, h), Image.NEAREST)

def console_overlay ():
	img = Image.new ("RGB", (CW, CH), (0, 0, 0))
	img.paste (game_screen (CW, CH), (0, 0))
	img = dim (img, 150, (4, 8, 24))
	# the quick menu: a glass column at the left, Home pressed in game
	glass (img, 24, 24, 250, CH - 80, 14, alpha = 110)
	draw_icon (img, "gbaemu", 40, 38, 32)
	text (img, 80, 36, "Star Courier", cfont (19, "semi"), (255, 255, 255)); text (img, 80, 58, "paused  -  6 h 41 min", cfont (13, "regular"), (160, 190, 230))
	items = ["Resume", "Save state", "Load state", "Screenshot", "Controls", "Speed: normal", "Back to Games"]
	y = 96
	for i, it in enumerate (items):
		if i == 1:
			img = glow_box (img, 34, y - 2, 230, 34, 8)
			text (img, 52, y + 3, it, cfont (19, "semi"), (255, 255, 255))
		else: text (img, 52, y + 3, it, cfont (19, "light"), (204, 218, 240))
		y += 40
	# the save slots, beside: memory-card like, the picture at the time
	glass (img, 290, 90, 326, 200, 12, alpha = 90)
	text (img, 306, 100, "Save to slot", cfont (16, "regular"), (210, 226, 250))
	for i in range (3):
		sx = 306 + i * 102; sy = 130
		th = game_screen (92, 69) if i < 2 else Image.new ("RGB", (92, 69), (14, 22, 44))
		if i == 1: th = dim (th, 60)
		img.paste (th, (sx, sy)); ring (img, sx, sy, 92, 69, 3, (255, 255, 255) if i == 2 else (150, 180, 230), t = 2 if i == 2 else 1)
		text_c (img, sx, sy + 74, 92, 18, ["slot 1", "slot 2", "empty"][i], cfont (14, "regular"), (220, 232, 250))
		text_c (img, sx, sy + 92, 92, 16, ["Mon 21:04", "Sun 10:12", "new"][i], cfont (12, "regular"), (150, 176, 214))
	if True:
		m = Image.new ("L", img.size, 0); ImageDraw.Draw (m).rectangle ([510, 128, 602, 201], outline = 255, width = 3)
		img = add_glow (img, Image.new ("RGB", img.size, GLOW), m, 6)
	console_top (img, gem = False)
	console_hints (img, [("x", "Save"), ("o", "Resume"), ("L1/R1", "Slot")], y = CH - 40)
	return img

def console_switcher ():
	img = space (CW, CH, seed = 21)
	img = dim (img, 60)
	console_top (img, "Running")
	cards = [("gbaemu", "Star Courier", "GBA  -  paused"), ("media", "Media Player", "playing: track 4"), ("telegram", "Telegram", "2 unread"),
		 ("fileviewer", "Files", "SD:/roms")]
	# a shallow arc in depth: the chosen card in front, the others smaller, further, dimmer
	geo = [(-1, 60, 150, 150, 120), (0, 196, 120, 248, 186), (1, 466, 150, 150, 120), (2, 590, 166, 120, 96)]
	order = [3, 0, 2, 1]
	thumbs = {0: game_screen (248, 186), 1: None, 2: None, 3: None}
	for idx in order:
		pos, x, y, w, h = geo[idx]
		a, name, sub = cards[idx]
		if idx == 0: th = game_screen (w, h)
		else:
			src = {"media": "media-home", "telegram": "telegram", "fileviewer": "fileviewer"}[a]
			im_ = shot (src); kk = max (w / im_.width, h / im_.height)
			th = im_.resize ((int (im_.width * kk) + 1, int (im_.height * kk) + 1), Image.LANCZOS).crop ((0, 0, w, h))
		if idx != 1: th = dim (th, 110, (4, 8, 24))
		if idx == 1:
			m = Image.new ("L", img.size, 0); ImageDraw.Draw (m).rounded_rectangle ([x - 6, y - 6, x + w + 6, y + h + 6], 10, fill = 200)
			img = add_glow (img, Image.new ("RGB", img.size, GLOW), m, 12)
		img.paste (th, (x, y)); ring (img, x, y, w, h, 4, (220, 236, 255) if idx == 1 else (110, 140, 190), t = 2 if idx == 1 else 1)
		# a reflection below, faint
		ref = th.transpose (Image.FLIP_TOP_BOTTOM).crop ((0, 0, w, h // 3))
		rm = Image.fromarray ((np.linspace (70, 0, h // 3)[:, None].repeat (w, 1)).astype ("uint8"), "L")
		img.paste (ref, (x, y + h + 4), rm)
	a, name, sub = cards[1]
	draw_icon (img, a, 196, 330, 32)
	glow_text (img, 236, 326, name, cfont (24, "semi"), (255, 255, 255), radius = 5, strength = 1)
	text (img, 238, 356, sub, cfont (14, "regular"), (170, 196, 232))
	for i in range (4): rrect (img, CW // 2 - 34 + i * 18, 392, 8, 8, 4, (200, 225, 255), alpha = 240 if i == 1 else 80)
	console_hints (img, [("x", "Switch"), ("s", "Close"), ("o", "Back"), ("L1/R1", "Choose")])
	return img

# ===========================================================================================================
# Three real apps in every mode: the Terminal, the Media Player, Letters (docs section "Three apps in every
# mode"). Their real layouts: user/Apps/terminal (a TabStrip + its own TermView, one menu "Shell"),
# user/Apps/media (its own Sidebar 208 / TopBar 52 / Content / NowBar 80, menus File Play View),
# user/Apps/letters (its own ToolBar of two rows, a Ruler, the PageView, a StatusBar; 7 menus).
# ===========================================================================================================
def ffont (name, size):
	p = os.path.join (SD, "res", "fonts", name)
	return ImageFont.truetype (p, int (round (size))) if os.path.exists (p) else font (size)

def milkify_keep_teal (im):
	"""milkify, but the terminal's own teal kept (a terminal stays dark)."""
	a = np.asarray (im.convert ("RGB")).astype (float); out = np.asarray (milkify (im)).astype (float)
	r, g, b = a[..., 0], a[..., 1], a[..., 2]
	dark_teal = (b > r + 20) & (g > r + 10) & (0.3 * r + 0.59 * g + 0.11 * b < 90)
	for i in range (3): out[..., i] = np.where (dark_teal, a[..., i], out[..., i])
	return Image.fromarray (out.astype ("uint8"), "RGB")

def real (name):
	"""A real screenshot, its CDE frame cut off, in Milk's colours."""
	im = Image.open (os.path.join (SHOTS, name + ".png")).convert ("RGBA")
	bg = Image.new ("RGBA", im.size, (208, 194, 186, 255)); bg.alpha_composite (im)
	im = bg.convert ("RGB").crop ((4, 28, im.width - 4, im.height - 4))
	return milkify_keep_teal (im) if name == "terminal" else milkify (im)

def milk_frame (img, x, y, cw, ch, title):
	"""A window in Milk (uikit/skin.cpp): the title a gradient down to the window's colour, the frame melting
	into it, OS X's beads (the window menu at the left; minimise, maximise, close at the right)."""
	w, h = cw + 8, ch + 32
	rrect (img, x, y, w, h, 8, P["face"])
	rrect (img, x, y, w, 28, 8, ((250, 250, 250), P["face"]), corners = (True, True, False, False))
	ring (img, x, y, w, h, 8, (40, 40, 46), alpha = 150)
	hline (img, x + 8, y + 1, w - 16, (255, 255, 255), alpha = 120)
	bead (img, x + 9, y + 7, 14, (154, 168, 186))
	for i, c in enumerate ((P["red"], P["green"], P["amber"])): bead (img, x + w - 23 - i * 20, y + 7, 14, c)
	text_c (img, x, y, w, 28, title, font (13, True), P["ink"])
	return x + 4, y + 28

def desktop_scene (title, menus, content, sw = 1280, sh = 800, dock_icons = ("tinypad", "jet", "paint", "media", "gamelib", "terminal", "fileviewer", "letters")):
	"""Today's desktop in Milk: the menu bar, the app's window, the silver dock over it (topmost)."""
	img = wallpaper (sw, sh, seed = 7)
	statusbar (img, sw, title = title, menus = menus)
	cw, ch = content.size
	x = (sw - cw - 8) // 2; y = BAR + max (8, (sh - BAR - 70 - ch - 32) // 2)
	cx, cy = milk_frame (img, x, y, cw, ch, title); img.paste (content, (cx, cy))
	n = len (dock_icons); dw = n * 62 + 24; dx = (sw - dw) // 2; dy = sh - 66
	rrect (img, dx, dy, dw, 60, 12, ((244, 246, 249), P["dock"]), alpha = 245); ring (img, dx, dy, dw, 60, 12, (60, 64, 72), alpha = 130)
	for i, a in enumerate (dock_icons):
		draw_icon (img, a, dx + 16 + i * 62, dy + 9, 40)
		if a in (title.lower (), "terminal"): rrect (img, dx + 33 + i * 62, dy + 52, 6, 4, 2, P["accent"])
	return img

def pad_glass_note (img, x, y, w, title, body, icon = None):
	"""Console mode's honest note: a glass panel with a line or two."""
	h = 64 if body else 40
	glass (img, x, y, w, h, 12, alpha = 120, edge = 170)
	tx = x + 16
	if icon: glyph (img, x + 16, y + 14, 34, 34, icon, (190, 214, 250)); tx = x + 62
	glow_text (img, tx, y + 9, title, cfont (19, "semi"), (255, 255, 255), radius = 4, strength = 1)
	if body: text (img, tx, y + 36, body, cfont (14, "regular"), (176, 200, 236))

# ---- the Terminal ------------------------------------------------------------------------------------------
TERM_SHORT = [("SD:/ $ ", "ls /bin | grep e"), ("", "echo"), ("", "sleep"), ("", "yes"), ("SD:/ $ ", "ps"),
	      ("", "  1 k R  idle"), ("", " 14 a S  elegant"), ("", " 21 a R  terminal"), ("", " 25 a R  ping"),
	      ("SD:/ $ ", "echo onyx | wc -c"), ("", "5"), ("SD:/ $ ", "")]

def term_keys_row (img, x, y, w, k):
	"""The input method's terminal row (the text-input hint says "terminal"): Esc, Tab, Ctrl, Alt, the arrows."""
	keys = ["Esc", "Tab", "Ctrl", "Alt", "<", "^", "v", ">", "|", "~", "/"]
	h = int (26 * k); box (img, x, y, w, h, (176, 178, 184))
	kw = (w - (len (keys) + 1) * 2 * k) / len (keys)
	for i, lab in enumerate (keys):
		kx = int (x + 2 * k + i * (kw + 2 * k))
		rrect (img, kx, y + 3 * k, int (kw), h - 6 * k, 4 * k, ((236, 236, 238), (212, 212, 216)))
		text_c (img, kx, y + 3 * k, int (kw), h - 6 * k, lab, font (9 * k, True), P["ink"])
	return h

def app_terminal_portrait (W = 480, H = 800, k = 1.5, keyboard = True):
	img = Image.new ("RGB", (W, H), P["face"])
	statusbar (img, W, title = "Terminal", menus = ("Shell",), k = k, compact = True, lite = True)
	soft = int (26 * k); bottom = soft
	if keyboard:
		oh = int ((4 * 24 + 5 * 2 + 4) * k); ky = H - soft - oh
		osk (img, 0, ky, W, k = k)
		kr = int (26 * k); term_keys_row (img, 0, ky - kr, W, k); bottom = soft + oh + kr
	terminal_screen (img, 0, int (BAR * k), W, H - int (BAR * k) - bottom, k = k, fs = 9 if W / k < 300 else 11, lines = TERM_SHORT,
			 tabs = ("SD:/", "docs", "ping"))
	slate_softkeys (img, W, H, [(g_grid, "Home"), (g_tasks, "Tasks"), (g_menu, "Menu"), (g_kbd, "Hide" if keyboard else "Keys")], k = k)
	return img

def menu_hint (img, y = 14, words = "menu"):
	"""The transient hint at an app's start in console mode (about 3 s): Home (or Alt / F10) shows the menus."""
	f = cfont (15, "regular")
	parts = [("HOME", ""), ("", "or"), ("Alt", ""), ("", words)]
	w = 0
	for key, word in parts: w += (tw (key, font (10, True)) + 14 + 6) if key else (tw (word, f) + 8)
	x = (CW - w - 28) // 2
	rrect (img, x, y, w + 28, 30, 15, (10, 16, 34), alpha = 215); ring (img, x, y, w + 28, 30, 15, (140, 180, 250), alpha = 170)
	x += 14
	for key, word in parts:
		if key:
			kw = tw (key, font (10, True)) + 14
			rrect (img, x, y + 6, kw, 18, 5, ((70, 80, 100), (34, 38, 50))); ring (img, x, y + 6, kw, 18, 5, (170, 190, 220), alpha = 160)
			text_c (img, x, y + 5, kw, 18, key, font (10, True), (230, 236, 246)); x += kw + 6
		else: text (img, x, y + 5, word, f, (214, 226, 246)); x += tw (word, f) + 8

def console_menubar (img, menus, open_menu, items, sel, system = ("Home", "Switch app", "Quit")):
	"""The app's own menus, revealed on demand in console style: big items, a glowing focus, the pad's
	directions; the system's items in a slim row below them."""
	h = 46
	img.paste (Image.blend (img.crop ((0, 0, CW, h + 30)), Image.new ("RGB", (CW, h + 30), (6, 10, 26)), 0.86), (0, 0))
	hline (img, 0, h, CW, (110, 150, 220), alpha = 160); hline (img, 0, h + 30, CW, (110, 150, 220), alpha = 90)
	f = cfont (20, "light"); fs = cfont (20, "semi")
	x = 16; pos = {}
	for m_ in menus:
		ww = tw (m_, fs) + 20; pos[m_] = x
		if m_ == open_menu:
			img2 = glow_box (img, x, 7, ww, 32, 8); img.paste (img2)
			text (img, x + 10, 10, m_, fs, (255, 255, 255))
		else: text (img, x + 10, 10, m_, f, (196, 212, 236))
		x += ww + 2
	sx = 16
	for it in system:
		text (img, sx, h + 6, it, cfont (14, "regular"), (150, 176, 214)); sx += tw (it, cfont (14, "regular")) + 26
	text_r (img, CW - 16, h + 4, 20, "the system", cfont (12, "regular"), (110, 134, 176))
	# the drop-down, in glass
	dx = pos[open_menu]; rh = 30; dh = sum (10 if it == "-" else rh for it in items) + 14; dw = 250
	glass (img, dx, h + 36, dw, dh, 10, alpha = 150, edge = 170)
	y = h + 43
	for i, it in enumerate (items):
		if it == "-": hline (img, dx + 12, y + 4, dw - 24, (110, 140, 200), alpha = 120); y += 10; continue
		if i == sel:
			img2 = glow_box (img, dx + 6, y, dw - 12, rh - 2, 7); img.paste (img2)
		text (img, dx + 18, y + 4, it[0], cfont (17, "semi" if i == sel else "light"), (255, 255, 255) if i == sel else (206, 220, 242))
		if it[1]: text_r (img, dx + dw - 16, y + 2, 24, it[1], cfont (13, "regular"), (150, 176, 214))
		y += rh
	return img

def app_terminal_console ():
	"""Console mode: the app full screen, no chrome; a keyboard expected (no keyboard on screen)."""
	img = Image.new ("RGB", (CW, CH), P["term"])
	terminal_screen (img, 0, 0, CW, CH, fs = 13, lines = TERM_SHORT)
	menu_hint (img, y = CH - 136)
	# a keyboard is needed: said once, at the start, if none is plugged in
	pad_glass_note (img, 40, CH - 92, CW - 80, "A keyboard is needed to type", "Plug in a USB or Bluetooth keyboard; the pad only scrolls and switches tabs.",
			icon = lambda d, s: g_kbd (d, s, 2.1))
	return img

# ---- the Media Player ------------------------------------------------------------------------------------
_media_src = {}
def media_src (name):
	if name not in _media_src:
		im = Image.open (os.path.join (SHOTS, name + ".png")).convert ("RGBA")
		bg = Image.new ("RGBA", im.size, (208, 194, 186, 255)); bg.alpha_composite (im); _media_src[name] = bg.convert ("RGB")
	return _media_src[name]
def cover (n, size):
	"""An album's real cover (screenshots/media-albums.png), n = 0..7."""
	src = media_src ("media-albums"); x = [290, 452, 614, 776][n % 4]; y = [156, 360][n // 4]
	return src.crop ((x, y, x + 140, y + 140)).resize ((size, size), Image.LANCZOS)
def big_cover (size):
	return media_src ("media-nowplaying").crop ((252, 158, 482, 388)).resize ((size, size), Image.LANCZOS)
MEDIA_ICONS = {"Home": 52, "Artists": 110, "Albums": 140, "Songs": 170, "Genres": 200, "Folders": 230, "Films": 288,
	       "Clips and series": 318, "Favourites": 376, "Recently added": 406, "Sunday morning": 436, "Workout": 466}
def media_icon (name, size = 20):
	"""The sidebar's real icon, its background made see-through (RGBA)."""
	y = MEDIA_ICONS[name]; im = media_src ("media-nowplaying").crop ((20, y - 10, 40, y + 10))
	a = np.asarray (im).astype (int); bgc = a[0, 0]
	alpha = np.clip ((np.abs (a - bgc).sum (-1) - 12) * 4, 0, 255).astype ("uint8")
	rgba = Image.fromarray (np.dstack ([a.astype ("uint8"), alpha]), "RGBA")
	return rgba if size == 20 else rgba.resize ((size, size), Image.LANCZOS)
SONGS = [("Title Screen", "8-Bit Parade", "Midnight Arcade", "0:41", "MIDI"), ("Level 1", "8-Bit Parade", "Midnight Arcade", "0:41", "MIDI"),
	 ("Concrete", "Atlas Grey", "Grey Atlas", "2:53", "MP3"), ("Grey Atlas", "Atlas Grey", "Grey Atlas", "4:51", "MP3"),
	 ("Engines", "Atlas Grey", "Grey Atlas", "4:18", "MP3"), ("Aria", "J. S. Bach", "Goldberg Variations", "0:42", "MIDI"),
	 ("Variatio 1", "J. S. Bach", "Goldberg Variations", "0:43", "MIDI"), ("Blue Hour", "Koji Arai Trio", "Blue Hour Sessions", "4:54", "FLAC"),
	 ("Kissa", "Koji Arai Trio", "Blue Hour Sessions", "3:01", "FLAC"), ("Glass Gardens", "Lumen Drift", "Glass Gardens", "3:26", "WAV"),
	 ("Greenhouse", "Lumen Drift", "Glass Gardens", "2:41", "WAV")]
BADGE = {"MIDI": (130, 90, 200), "MP3": (150, 150, 156), "FLAC": (150, 150, 156), "WAV": (150, 150, 156)}

def media_nowbar (img, x, y, w, h, k = 1, compact = False):
	u = lambda v: int (v * k)
	box (img, x, y, w, h, (238, 238, 240)); hline (img, x, y, w, (200, 200, 204))
	cs = h - u (16); img.paste (big_cover (cs), (x + u (8), y + u (8)))
	text (img, x + cs + u (16), y + u (10), "Glass Gardens", font (u (12), True), P["ink"])
	text (img, x + cs + u (16), y + u (28), "Lumen Drift", font (u (11)), P["ink2"])
	cx = x + w // 2 + (u (30) if compact else 0)
	if compact: cx = x + w - u (70)
	for dx, g in [(-u (44), "prev"), (0, "play"), (u (44), "next")]:
		if g == "play":
			rrect (img, cx + dx - u (16), y + h // 2 - u (16) - (0 if compact else u (6)), u (32), u (32), u (16), P["accent"])
			glyph (img, cx + dx - u (5), y + h // 2 - u (6) - (0 if compact else u (6)), u (12), u (12), lambda d, s: [d.rectangle ([1 * s * k, 0, 4 * s * k, 12 * s * k], fill = 255), d.rectangle ([7 * s * k, 0, 10 * s * k, 12 * s * k], fill = 255)], (255, 255, 255))
		else:
			glyph (img, cx + dx - u (7), y + h // 2 - u (7) - (0 if compact else u (6)), u (14), u (14), lambda d, s, g = g: d.polygon (
				[(12 * s * k, 1 * s * k), (12 * s * k, 13 * s * k), (3 * s * k, 7 * s * k)] if g == "prev" else [(2 * s * k, 1 * s * k), (2 * s * k, 13 * s * k), (11 * s * k, 7 * s * k)], fill = 255), P["ink"])
	if not compact:
		px = x + w // 2 - u (180); pw = u (360); py = y + h - u (16)
		rrect (img, px, py, pw, u (4), u (2), (206, 206, 210)); rrect (img, px, py, pw // 3, u (4), u (2), P["accent"])
		text_r (img, px - u (6), py - u (7), u (16), "1:08", font (u (10)), P["ink2"]); text (img, px + pw + u (6), py - u (6), "3:26", font (u (10)), P["ink2"])
		glyph (img, x + w - u (130), y + h // 2 - u (7), u (16), u (14), lambda d, s: g_speaker (d, s * k), P["ink"])
		rrect (img, x + w - u (104), y + h // 2 - u (2), u (88), u (4), u (2), (206, 206, 210)); rrect (img, x + w - u (104), y + h // 2 - u (2), u (60), u (4), u (2), P["accent"])

def app_media_pocket ():
	"""800 x 480: the sidebar folds into a rail; the top bar keeps back / forward, the crumbs, the search; the
	table loses nothing (740 px is enough); the now bar is 64 px."""
	img = Image.new ("RGB", (PW, PH), P["face"])
	statusbar (img, PW, title = "Media Player", menus = ("File", "Play", "View"))
	rw = 52; top = BAR; nh = 64
	box (img, 0, top, rw, PH - top - nh, (226, 226, 228)); vline (img, rw, top, PH - top - nh, (200, 200, 204))
	y = top + 8
	for n in ["Home", "Artists", "Albums", "Songs", "Genres", "Folders", "-", "Films", "Clips and series", "-", "Favourites", "Recently added"]:
		if n == "-": hline (img, 12, y + 2, 28, (200, 200, 204)); y += 8; continue
		if n == "Songs":
			rrect (img, 6, y - 3, 40, 28, 7, P["accent"])
			img.paste ((255, 255, 255), (16, y + 1), media_icon (n).getchannel ("A"))
		else: ic_ = media_icon (n); img.paste (ic_, (16, y + 1), ic_)
		y += 31
	# the top bar
	x0 = rw + 1; ty = top
	box (img, x0, ty, PW - x0, 44, (232, 232, 234)); hline (img, x0, ty + 43, PW - x0, (204, 204, 208))
	for i in range (2):
		rrect (img, x0 + 10 + i * 34, ty + 8, 28, 28, 14, ((250, 250, 250), (226, 226, 230))); ring (img, x0 + 10 + i * 34, ty + 8, 28, 28, 14, (0, 0, 0), alpha = 70)
		glyph (img, x0 + 19 + i * 34, ty + 17, 10, 10, lambda d, s, i = i: g_chev (d, s, 1, "left" if i == 0 else "right"), P["ink"] if i == 0 else (170, 170, 176))
	text_l (img, x0 + 86, ty, 44, "Music", font (12), P["ink"]); glyph (img, x0 + 130, ty + 18, 8, 8, lambda d, s: g_chev (d, s, 0.8, "right"), P["ink2"])
	text_l (img, x0 + 144, ty, 44, "Songs", font (12, True), P["ink"])
	rrect (img, PW - 210, ty + 9, 198, 26, 6, P["field"]); ring (img, PW - 210, ty + 9, 198, 26, 6, (0, 0, 0), alpha = 70)
	text_l (img, PW - 200, ty + 9, 26, "Search the library", font (12), (150, 150, 156))
	# the table
	cy = ty + 52; cx = x0 + 14; cw = PW - cx - 14
	text (img, cx, cy - 2, "Songs", font (22, True), P["ink"]); text (img, cx + 84, cy + 7, "45 songs", font (12), P["ink2"])
	cy += 34
	cols = [(cx + 10, "Title"), (cx + 230, "Artist"), (cx + 390, "Album"), (cx + 600, "Time")]
	rrect (img, cx, cy, cw, 24, 5, (226, 226, 230))
	for xx, lab in cols: text_l (img, xx, cy, 24, lab, font (11, True), P["accent"] if lab == "Artist" else P["ink2"])
	cy += 26
	for i, (t, a, al, tm, b) in enumerate (SONGS):
		if cy + 26 > PH - nh - 2: break
		if i == 4: rrect (img, cx, cy, cw, 25, 5, (lighten (P["accent"], 0.72), lighten (P["accent"], 0.66)))
		elif i % 2: rrect (img, cx, cy, cw, 25, 5, (238, 238, 240))
		text_l (img, cols[0][0], cy, 25, t, font (12), P["ink"]); text_l (img, cols[1][0], cy, 25, a, font (12), P["ink2"])
		text_l (img, cols[2][0], cy, 25, ellipsize (al, font (12), 200), font (12), P["ink2"]); text_l (img, cols[3][0], cy, 25, tm, font (11), P["ink2"])
		bw = tw (b, font (9, True)) + 12; rrect (img, cx + cw - bw - 10, cy + 5, bw, 15, 7, BADGE[b]); text_c (img, cx + cw - bw - 10, cy + 4, bw, 15, b, font (9, True), (255, 255, 255))
		cy += 26
	media_nowbar (img, 0, PH - nh, PW, nh)
	return img

def app_media_portrait (W = 480, H = 800, k = 1.5):
	"""Portrait: Now Playing takes the screen -- the cover, the title, the controls, Up next below."""
	u = lambda v: int (v * k)
	img = Image.new ("RGB", (W, H), (24, 28, 40))
	statusbar (img, W, title = "Media Player", menus = ("File", "Play", "View"), k = k, compact = True, lite = True)
	img.paste (grad (W, H - u (BAR) - u (26), (40, 46, 66), (14, 16, 26)), (0, u (BAR)))
	y = u (BAR + 8)
	rrect (img, u (10), y, u (30), u (30), u (15), (255, 255, 255), alpha = 40)
	glyph (img, u (18), y + u (8), u (14), u (14), lambda d, s: g_menu (d, s, 0.85 * k), (236, 240, 248))	# the drawer
	text_l (img, u (48), y, u (30), "Now playing", font (u (13), True), (236, 240, 248))
	cs = W - u (150); y += u (36)
	img.paste (big_cover (cs), ((W - cs) // 2, y)); y += cs + u (12)
	text (img, u (32), y, "Glass Gardens", font (u (20), True), (255, 255, 255)); y += u (28)
	text (img, u (32), y, "Lumen Drift  -  Glass Gardens", font (u (12)), (190, 200, 220)); y += u (26)
	rrect (img, u (32), y, W - u (64), u (4), u (2), (80, 88, 110)); rrect (img, u (32), y, (W - u (64)) // 3, u (4), u (2), (120, 180, 250))
	rrect (img, u (32) + (W - u (64)) // 3 - u (6), y - u (4), u (12), u (12), u (6), (255, 255, 255))
	text (img, u (32), y + u (8), "1:08", font (u (10)), (170, 180, 200)); text_r (img, W - u (32), y + u (6), u (14), "3:26", font (u (10)), (170, 180, 200))
	y += u (30); cx = W // 2
	rrect (img, cx - u (24), y, u (48), u (48), u (24), (120, 180, 250))
	glyph (img, cx - u (8), y + u (16), u (16), u (16), lambda d, s: [d.rectangle ([2 * s * k, 0, 6 * s * k, 16 * s * k], fill = 255), d.rectangle ([10 * s * k, 0, 14 * s * k, 16 * s * k], fill = 255)], (20, 24, 36))
	for dx, g in [(-u (80), "prev"), (u (80), "next")]:
		glyph (img, cx + dx - u (9), y + u (15), u (18), u (18), lambda d, s, g = g: d.polygon (
			[(16 * s * k, 1 * s * k), (16 * s * k, 17 * s * k), (3 * s * k, 9 * s * k)] if g == "prev" else [(2 * s * k, 1 * s * k), (2 * s * k, 17 * s * k), (15 * s * k, 9 * s * k)], fill = 255), (236, 240, 248))
	y += u (62)
	text (img, u (20), y, "Up next", font (u (12), True), (236, 240, 248)); y += u (22)
	for i, (t, a, n) in enumerate ([("Greenhouse", "Lumen Drift", 1), ("Concrete", "Atlas Grey", 3)]):
		if y + u (40) > H - u (30): break
		img.paste (cover (n, u (34)), (u (20), y)); text (img, u (62), y + u (2), t, font (u (12), True), (236, 240, 248))
		text (img, u (62), y + u (19), a, font (u (10)), (170, 180, 200)); y += u (42)
	slate_softkeys (img, W, H, [(g_grid, "Home"), (g_tasks, "Tasks"), (g_menu, "Menu"), (g_kbd, "Keys")], k = k)
	return img

def app_media_drawer (W = 480, H = 800, k = 1.5):
	"""Portrait: the songs as one column (title, then artist and album), the sidebar a drawer over it."""
	u = lambda v: int (v * k)
	img = Image.new ("RGB", (W, H), P["paper"])
	statusbar (img, W, title = "Media Player", menus = ("File", "Play", "View"), k = k, compact = True, lite = True)
	y = u (BAR)
	box (img, 0, y, W, u (40), (232, 232, 234)); text_l (img, u (48), y, u (40), "Songs", font (u (14), True), P["ink"])
	y += u (44)
	for i, (t, a, al, tm, b) in enumerate (SONGS):
		if y + u (40) > H - u (26) - u (56): break
		text (img, u (14), y + u (3), t, font (u (12), True), P["ink"]); text (img, u (14), y + u (20), a + "  -  " + al, font (u (10)), P["ink2"])
		text_r (img, W - u (14), y + u (4), u (16), tm, font (u (10)), P["ink2"]); hline (img, u (14), y + u (38), W - u (28), (224, 224, 228)); y += u (40)
	media_nowbar (img, 0, H - u (26) - u (56), W, u (56), k = k, compact = True)
	img = dim_below (img, u (BAR), 110)
	dw = u (220); box (img, 0, u (BAR), dw, H - u (BAR) - u (26), (240, 240, 242)); vline (img, dw, u (BAR), H - u (BAR) - u (26), (150, 150, 156))
	y = u (BAR + 10)
	for n in ["Home", "LIBRARY", "Artists", "Albums", "Songs", "Genres", "Folders", "VIDEOS", "Films", "Clips and series", "PLAYLISTS", "Favourites", "Recently added", "Sunday morning"]:
		if n.isupper(): text (img, u (16), y + u (4), n, font (u (9), True), (130, 130, 136)); y += u (22); continue
		if n == "Songs": rrect (img, u (8), y, dw - u (16), u (28), u (6), P["accent"])
		ic_ = media_icon (n, u (16))
		if n == "Songs": img.paste ((255, 255, 255), (u (18), y + u (6)), ic_.getchannel ("A"))
		else: img.paste (ic_, (u (18), y + u (6)), ic_)
		text_l (img, u (44), y, u (28), n, font (u (12)), (255, 255, 255) if n == "Songs" else P["ink"]); y += u (30)
	slate_softkeys (img, W, H, [(g_grid, "Home"), (g_tasks, "Tasks"), (g_menu, "Menu"), (g_kbd, "Keys")], k = k)
	return img

def app_media_console ():
	"""Console mode: the Media Player full screen, its own Now playing view at the console's size; Home shows
	its menus (File, Play, View) over it."""
	img = Image.new ("RGB", (CW, CH), (24, 28, 40))
	img.paste (grad (CW, CH, (40, 46, 66), (12, 14, 22)), (0, 0))
	cs = 250; x0, y0 = 36, 70
	img.paste (big_cover (cs), (x0, y0))
	tx = x0 + cs + 30
	text (img, tx, y0 + 4, "Now playing", font (13, True), (170, 186, 214))
	text (img, tx, y0 + 30, "Glass Gardens", font (26, True), (255, 255, 255))
	text (img, tx, y0 + 66, "Lumen Drift  -  Glass Gardens", font (14), (190, 200, 220))
	text (img, tx, y0 + 88, "WAV  -  8.0 kHz  -  8 bit", font (11), (140, 150, 172))
	rrect (img, tx, y0 + 124, 290, 5, 2, (80, 88, 110)); rrect (img, tx, y0 + 124, 96, 5, 2, (120, 180, 250))
	rrect (img, tx + 90, y0 + 119, 14, 14, 7, (255, 255, 255))
	text (img, tx, y0 + 136, "1:08", font (11), (170, 180, 200)); text_r (img, tx + 290, y0 + 134, 16, "3:26", font (11), (170, 180, 200))
	cx = tx + 145; cy = y0 + 180
	m = Image.new ("L", img.size, 0); ImageDraw.Draw (m).ellipse ([cx - 30, cy - 30, cx + 30, cy + 30], outline = 255, width = 3)
	img = add_glow (img, Image.new ("RGB", img.size, GLOW), m, 6)						# (the pad's focus)
	rrect (img, cx - 26, cy - 26, 52, 52, 26, (120, 180, 250))
	glyph (img, cx - 9, cy - 9, 18, 18, lambda d, s: [d.rectangle ([2 * s, 0, 7 * s, 18 * s], fill = 255), d.rectangle ([11 * s, 0, 16 * s, 18 * s], fill = 255)], (20, 24, 36))
	for dx, g in [(-86, "prev"), (86, "next")]:
		glyph (img, cx + dx - 10, cy - 10, 20, 20, lambda d, s, g = g: d.polygon (
			[(18 * s, 1 * s), (18 * s, 19 * s), (3 * s, 10 * s)] if g == "prev" else [(2 * s, 1 * s), (2 * s, 19 * s), (17 * s, 10 * s)], fill = 255), (236, 240, 248))
	y = y0 + cs + 26
	text (img, x0, y, "Up next", font (14, True), (236, 240, 248))
	for i, (t, a, n) in enumerate ([("Greenhouse", "Lumen Drift", 1), ("Concrete", "Atlas Grey", 3), ("Aria", "J. S. Bach", 2)]):
		xx = x0 + i * 190; yy = y + 26
		img.paste (cover (n, 48), (xx, yy)); text (img, xx + 58, yy + 6, t, font (13, True), (236, 240, 248)); text (img, xx + 58, yy + 26, a, font (11), (170, 180, 200))
	menu_hint (img, y = 14)
	return img

# ---- Letters -------------------------------------------------------------------------------------------------
_lsrc = None
def letters_src ():
	global _lsrc
	if _lsrc is None:
		im = Image.open (os.path.join (SHOTS, "letters.png")).convert ("RGBA")
		bg = Image.new ("RGBA", im.size, (208, 194, 186, 255)); bg.alpha_composite (im); _lsrc = milkify (bg.convert ("RGB"))
	return _lsrc
# the real toolbar's buttons (letters.png: centre x, row y)
LT1 = {"new": 25, "open": 54, "save": 83, "undo": 122, "redo": 152, "cut": 191, "copy": 220, "paste": 249, "find": 290, "marks": 319,
       "break": 359, "table": 388, "symbol": 430, "image": 459, "zoomout": 499, "zoomin": 615}
LT2 = {"bold": 418, "italic": 447, "under": 476, "strike": 505, "sup": 536, "sub": 566, "colour": 610, "high": 652,
       "left": 702, "centre": 731, "right": 760, "justify": 789, "bullets": 829, "numbers": 858, "outdent": 889, "indent": 918}
def ltool (name, size = 24):
	src = letters_src ()
	if name in LT1: cx, cy = LT1[name], 45
	else: cx, cy = LT2[name], 78
	im = src.crop ((cx - 12, cy - 12, cx + 12, cy + 12))
	return im if size == 24 else im.resize ((size, size), Image.LANCZOS)
def lcombo (which, w = None):
	src = letters_src (); x0, x1 = {"style": (11, 146), "font": (153, 328), "size": (335, 391), "zoom": (515, 598)}[which]
	y0, y1 = (66, 91) if which != "zoom" else (33, 58)
	im = src.crop ((x0, y0, x1, y1))
	if w and w < im.width: im = Image.fromarray (np.hstack ([np.asarray (im)[:, :w - 24], np.asarray (im)[:, im.width - 24:]]))
	return im

def letters_toolbar (img, x, y, w, items, k = 1, h = 34, lit = None):
	"""One row of Letters' tools: names (a button), ("combo", which, width), "|" (a separator), ">>" (the overflow)."""
	box (img, x, y, w, h, (232, 232, 234)); hline (img, x, y + h - 1, w, (204, 204, 208))
	cx = x + 6
	for it in items:
		if it == "|": vline (img, cx + 3, y + 7, h - 14, (196, 196, 200)); cx += 8; continue
		if it == ">>":
			bx = x + w - int (34 * k)
			if lit == ">>": rrect (img, bx, y + 4, int (28 * k), h - 8, 5, P["accent"])
			text_c (img, bx, y + 1, int (28 * k), h - 6, "»", font (int (16 * k), True), (255, 255, 255) if lit == ">>" else P["ink"]); continue
		if isinstance (it, tuple):
			c = lcombo (it[1], it[2]); c = c.resize ((int (c.width * k), int (c.height * k)), Image.LANCZOS) if k != 1 else c
			img.paste (c, (cx, y + (h - c.height) // 2)); cx += c.width + 6; continue
		s_ = int (24 * k); t = ltool (it, s_)
		if it == "left": rrect (img, cx - 2, y + (h - s_) // 2 - 2, s_ + 4, s_ + 4, 4, lighten (P["accent"], 0.6))
		img.paste (t, (cx, y + (h - s_) // 2)) if it != "left" else img.paste (t.convert ("RGB"), (cx, y + (h - s_) // 2), t.convert ("L").point (lambda v: 255 if v < 200 else 0))
		cx += s_ + int (5 * k)
	return cx

def letters_doc (img, x, y, w, h, scale = 1.0, page = True, caret = True):
	"""The document of screenshots/letters.png, typeset at `scale` (its banner the real picture)."""
	if page:
		box (img, x, y, w, h, (150, 150, 154))
		pw = int (min (w - 40 * scale, 794 * scale)); px = x + (w - pw) // 2; py = y + int (16 * scale)
		box (img, px, py, pw, h, (255, 255, 255)); m = int (76 * scale)
	else:
		box (img, x, y, w, h, (255, 255, 255)); px, py, pw = x, y, w; m = int (14 * scale)
	cl, cw = px + m, pw - 2 * m
	yy = py + int ((70 if page else 10) * scale)
	sansb, sans, serif, serifb = (lambda sz: ffont ("LiberationSans-Bold.ttf", sz * scale)), (lambda sz: ffont ("LiberationSans-Regular.ttf", sz * scale)), \
		(lambda sz: ffont ("LiberationSerif-Regular.ttf", sz * scale)), (lambda sz: ffont ("LiberationSans-Bold.ttf", sz * scale))
	def centre (s_, f, col):
		nonlocal yy
		lines = [s_] if tw (s_, f) <= cw else wrap (s_, f, cw)
		for l in lines: text (img, cl + (cw - tw (l, f)) // 2, yy, l, f, col); yy += int (f.size * 1.3)
	centre ("Onyx Letters", sansb (34), (0, 0, 0)); yy += int (6 * scale)
	centre ("A word processor for Onyx, in the way of AbiWord", sans (21), (80, 80, 86)); yy += int (10 * scale)
	ban = Image.open (os.path.join (SHOTS, "letters.png")).convert ("RGB").crop ((197, 319, 797, 469)); bw = min (cw, int (600 * scale)); bh = int (bw * 150 / 600)
	img.paste (ban.resize ((bw, bh), Image.LANCZOS), (cl + (cw - bw) // 2, yy)); yy += bh + int (40 * scale)
	text (img, cl, yy, "Contents", serifb (21), (0, 0, 0)); yy += int (40 * scale)
	f = serif (15.5)
	for i, (t, n) in enumerate ([("What Letters does", "1"), ("A few keys", "1"), ("Tables, pages and fields", "2"), ("   A mail merge", "2")]):
		if yy > y + h - 10: break
		text (img, cl, yy, t, f, (0, 0, 0)); nx = cl + cw - tw (n, f); text (img, nx, yy, n, f, (0, 0, 0))
		dx = cl + tw (t, f) + 4
		while dx < nx - 8: text (img, dx, yy, ".", f, (0, 0, 0)); dx += max (3, int (4 * scale))
		if i == 0 and caret:
			lx = cl + tw ("What ", f); rrect (img, lx - 1, yy, tw ("Letters", f) + 2, int (f.size * 1.2), 0, lighten (P["accent"], 0.6))
			text (img, lx, yy, "Letters", f, (0, 0, 0))
		yy += int (30 * scale)
	yy += int (14 * scale)
	if yy < y + h - 20:
		text (img, cl, yy, "What Letters does", serifb (21), (0, 0, 0)); yy += int (36 * scale)
	for l in wrap ("Letters writes letters, reports and documents with pages: styles, fonts, tables, pictures, headers and footers, fields and a table of contents. It reads and writes .rtf, .odt and .docx, and exports to PDF and HTML.", serif (15.5), cw):
		if yy > y + h - 10: break
		text (img, cl, yy, l, serif (15.5), (0, 0, 0)); yy += int (22 * scale)

def wrap (s_, f, w):
	out, cur = [], ""
	for word in s_.split ():
		t = (cur + " " + word).strip ()
		if tw (t, f) <= w: cur = t
		else: out.append (cur); cur = word
	if cur: out.append (cur)
	return out

def letters_status (img, x, y, w, h = 22):
	box (img, x, y, w, h, (228, 228, 230)); hline (img, x, y, w, (204, 204, 208))
	f = font (11); text_l (img, x + 10, y, h, "letters-tour.rtf", f, P["ink"])
	text_l (img, x + w // 2 - 70, y, h, "Page 1 of 2    Words: 393", f, P["ink"]); text_r (img, x + w - 10, y, h, "-  100%  +", f, P["ink"])

LETTERS_MENUS = ("File", "Edit", "View", "Insert", "Format", "Table", "Tools")
def app_letters_pocket (overflow = True, menu = None):
	img = Image.new ("RGB", (PW, PH), P["face"])
	pos = statusbar (img, PW, title = "Letters", menus = LETTERS_MENUS, open_menu = menu)
	ty = BAR
	letters_toolbar (img, 0, ty, PW, ["new", "open", "save", "|", "undo", "redo", "|", ("combo", "style", 118), "|", "bold", "italic", "under", "|",
					  "left", "centre", "right", "justify", "|", "bullets", "numbers", ">>"], lit = ">>" if overflow else None)
	dy = ty + 34
	letters_doc (img, 0, dy, PW, PH - dy - 22, scale = 0.86)
	letters_status (img, 0, PH - 22, PW)
	if overflow:
		# the overflow: the tools that did not fit, as a grid (the second row and the rest of the first)
		ox, oy, ow = PW - 296, dy + 2, 288
		items = ["cut", "copy", "paste", "find", "marks", "break", "table", "symbol", "image", "zoomout", "zoomin",
			 "strike", "sup", "sub", "colour", "high", "outdent", "indent"]
		oh = 40 + 2 * 36 + ((len (items) + 7) // 8) * 34 + 8
		rrect (img, ox, oy, ow, oh, 8, (252, 252, 252)); ring (img, ox, oy, ow, oh, 8, (40, 40, 46), alpha = 140)
		img.paste (lcombo ("font", 168), (ox + 10, oy + 10)); img.paste (lcombo ("size"), (ox + 186, oy + 10))
		img.paste (lcombo ("zoom"), (ox + 10, oy + 44)); text_l (img, ox + 100, oy + 44, 25, "zoom", font (11), P["ink2"])
		for i, it in enumerate (items):
			r, c = divmod (i, 8); img.paste (ltool (it), (ox + 12 + c * 34, oy + 84 + r * 34))
		text_l (img, ox + 10, oy + oh - 26, 20, "Tools that do not fit: UIKit's Toolbar overflow", font (10), P["ink2"])
	if menu:
		img = dim_below (img, BAR, 30)
		dropdown (img, pos[menu], BAR + 2, [("Font...", "Ctrl+D"), ("Paragraph...", ""), ("Tabs...", ""), "-", ("Bold", "Ctrl+B"), ("Italic", "Ctrl+I"),
			  ("Underline", "Ctrl+U"), ("Strikethrough", ""), ("Clear Formatting", ""), "-", ("Align Left", "Ctrl+L"), ("Centre", "Ctrl+E"),
			  ("Align Right", "Ctrl+R"), ("Justify", "Ctrl+J"), "-", ("Bullets", ""), ("Numbering", "")], sel = 4, w = 220)
	return img

def app_letters_portrait (W = 480, H = 800, k = 1.5, sheet = False):
	"""Portrait: one row of tools (the rest in the overflow), the text reflowed to the width (a draft view: no
	pages -- they come back in landscape), the keyboard on screen; or the menus as a bottom sheet."""
	u = lambda v: int (v * k)
	img = Image.new ("RGB", (W, H), P["face"])
	statusbar (img, W, title = "Letters", menus = LETTERS_MENUS, k = k, compact = True, lite = True, open_menu = "x" if sheet else None)
	ty = u (BAR)
	letters_toolbar (img, 0, ty, W, ["undo", "|", "bold", "italic", "under", "|", "left", "bullets", ">>"], k = k, h = u (34))
	dy = ty + u (34)
	soft = u (26); oh = 0 if sheet else int ((4 * 24 + 5 * 2 + 4) * k)
	letters_doc (img, 0, dy, W, H - dy - soft - oh, scale = 0.84 * k, page = False)
	if not sheet: osk (img, 0, H - soft - oh, W, k = k)
	if sheet:
		img = dim_below (img, u (BAR), 120)
		sh = u (300); sy = H - soft - sh
		rrect (img, 0, sy, W, sh + u (10), u (12), ((252, 252, 252), (238, 238, 240)), corners = (True, True, False, False))
		rrect (img, W // 2 - u (16), sy + u (5), u (32), u (4), u (2), (190, 190, 194))
		mx = u (8); my = sy + u (14)
		for m_ in LETTERS_MENUS:
			mw = tw (m_, font (u (10), True)) + u (14)
			if mx + mw > W: break
			if m_ == "Format": rrect (img, mx, my, mw, u (20), u (10), P["accent"])
			text_c (img, mx, my, mw, u (20), m_, font (u (10), True), (255, 255, 255) if m_ == "Format" else P["ink"]); mx += mw + u (2)
		glyph (img, W - u (16), my + u (5), u (10), u (10), lambda d, s: g_chev (d, s, k, "right"), P["ink2"])
		iy = my + u (28)
		for i, (lab, sc) in enumerate ([("Font...", "^D"), ("Paragraph...", ""), ("Bold", "^B"), ("Italic", "^I"), ("Underline", "^U"),
						("Align Left", "^L"), ("Centre", "^E"), ("Bullets", ""), ("Numbering", "")]):
			if iy + u (26) > H - soft - u (4): break
			if i == 2: rrect (img, u (6), iy, W - u (12), u (26), u (6), (lighten (P["accent"], 0.75), lighten (P["accent"], 0.65)))
			text_l (img, u (16), iy, u (26), lab, font (u (12)), P["ink"]); text_r (img, W - u (16), iy, u (26), sc, font (u (10)), P["ink2"])
			iy += u (28)
	slate_softkeys (img, W, H, [(g_grid, "Home"), (g_tasks, "Tasks"), (g_menu, "Close" if sheet else "Menu"), (g_kbd, "Keys" if sheet else "Hide")], k = k)
	return img

def app_letters_console (menu = False):
	"""Console mode: Letters full screen, no chrome (its toolbars hidden: everything is in its menus); Home,
	Alt or F10, or the pointer pushed against the top edge, reveals its menus in console style."""
	img = Image.new ("RGB", (CW, CH), (150, 150, 154))
	letters_doc (img, 0, 0, CW, CH, scale = 0.74, caret = True)
	if not menu:
		menu_hint (img, y = 14)
		rrect (img, CW - 160, CH - 34, 148, 24, 12, (10, 16, 34), alpha = 190)
		text_c (img, CW - 160, CH - 35, 148, 24, "Keyboard: connected", cfont (13, "regular"), (200, 220, 248))
		return img
	img = dim (img, 170, (4, 8, 24))
	box (img, 0, CH - 46, CW, 46, (6, 10, 26), alpha = 200)
	console_menubar (img, LETTERS_MENUS, "Format", [("Font...", "Ctrl+D"), ("Paragraph...", ""), "-", ("Bold", "Ctrl+B"), ("Italic", "Ctrl+I"),
			 ("Underline", "Ctrl+U"), "-", ("Align Left", "Ctrl+L"), ("Centre", "Ctrl+E"), ("Bullets", "")], sel = 3)
	console_hints (img, [("L1/R1", "Menus"), ("x", "Choose"), ("o", "Close"), ("HOME", "Close")])
	return img

# ---- the sheets ----------------------------------------------------------------------------------------------
def app_sheet (title, items, cols = 3, cell_w = 520):
	"""Per-app comparison: each variant scaled to one width (or its height kept for portrait), with captions."""
	pad, gap, cap = 24, 24, 44
	cells = []
	for im, lab, sub in items:
		if im.height > im.width:	pic = im.resize ((int (im.width * 470 / im.height), 470), Image.LANCZOS)	# portrait: by height
		else: pic = im.resize ((cell_w, int (im.height * cell_w / im.width)), Image.LANCZOS)
		cells.append ((pic, lab, sub))
	rows = [cells[i:i + cols] for i in range (0, len (cells), cols)]
	W = pad * 2 + cols * cell_w + (cols - 1) * gap
	H = pad + 44 + sum (max (c[0].height for c in r) + cap + gap for r in rows)
	out = Image.new ("RGB", (W, H), (236, 236, 238))
	text (out, pad, pad - 4, title, font (20, True), P["ink"])
	y = pad + 44
	for r in rows:
		x = pad
		for pic, lab, sub in r:
			text (out, x, y, lab, font (13, True), P["ink"]); text (out, x, y + 18, sub, font (11), P["ink2"])
			out.paste (pic, (x + (cell_w - pic.width) // 2, y + cap)); ring (out, x + (cell_w - pic.width) // 2 - 1, y + cap - 1, pic.width + 2, pic.height + 2, 3, (60, 60, 66), alpha = 150)
			x += cell_w + gap
		y += max (c[0].height for c in r) + cap + gap
	return out

def apps_main ():
	# the Terminal
	td = desktop_scene ("Terminal", ("Shell",), real ("terminal"), 1024, 768); save (td, "app-terminal-desktop.png")
	tp = pocket_terminal (); save (tp, "app-terminal-pocket.png")
	tpp = app_terminal_portrait (); save (tpp, "app-terminal-portrait.png")
	t24 = app_terminal_portrait (480, 640, 2, keyboard = False); save (t24, "app-terminal-240.png")
	tc = app_terminal_console (); save (tc, "app-terminal-console.png")
	save (app_sheet ("The Terminal in every mode", [(td, "Desktop (today)", "620 x 420 window, its tabs, the Shell menu"),
		(tp, "Pocket, 800 x 480", "full screen: the view reflows by itself (more columns)"), (tc, "Console, 640 x 480", "full screen, no chrome; a real keyboard expected"),
		(tpp, "Pocket, 480 x 800 portrait", "keyboard on screen + the terminal row (Esc Tab Ctrl...)"), (t24, "Pocket, 240 x 320 (2x)", "a slate with its own keys: 44 columns")]), "apps-terminal.png")
	# the Media Player
	md = desktop_scene ("Media Player", ("File", "Play", "View"), real ("media-albums")); save (md, "app-media-desktop.png")
	mp = app_media_pocket (); save (mp, "app-media-pocket.png")
	mpp = app_media_portrait (); save (mpp, "app-media-portrait.png")
	mdr = app_media_drawer (); save (mdr, "app-media-drawer.png")
	mc = app_media_console (); save (mc, "app-media-console.png")
	save (app_sheet ("The Media Player in every mode", [(md, "Desktop (today)", "sidebar 208, top bar, content, now bar 80"),
		(mp, "Pocket, 800 x 480", "the sidebar a rail of icons, the table whole, now bar 64"), (mc, "Console, 640 x 480", "full screen, its Now playing; Home shows its menus"),
		(mpp, "Pocket, 480 x 800: Now playing", "the cover, the controls, Up next"), (mdr, "Pocket, 480 x 800: the drawer", "the sidebar slides over a one-column list")]), "apps-media.png")
	# Letters
	lr = real ("letters"); lr.paste (Image.open (os.path.join (SHOTS, "letters.png")).convert ("RGB").crop ((197, 319, 797, 469)), (193, 291))	# (the document's picture: its own colours)
	ld = desktop_scene ("Letters", LETTERS_MENUS, lr); save (ld, "app-letters-desktop.png")
	lp = app_letters_pocket (); save (lp, "app-letters-pocket.png")
	lm = app_letters_pocket (overflow = False, menu = "Format"); save (lm, "app-letters-menu.png")
	lpp = app_letters_portrait (); save (lpp, "app-letters-portrait.png")
	lps = app_letters_portrait (sheet = True); save (lps, "app-letters-sheet.png")
	lc = app_letters_console (); save (lc, "app-letters-console.png")
	lcm = app_letters_console (menu = True); save (lcm, "app-letters-console-menu.png")
	save (app_sheet ("Letters in every mode", [(ld, "Desktop (today)", "two rows of tools, the ruler, the page"),
		(lp, "Pocket, 800 x 480", "one row of tools, the rest behind »; no ruler"), (lm, "Pocket, 800 x 480: the menus", "the status bar's Format menu (the app's own)"),
		(lpp, "Pocket, 480 x 800", "a draft view (no pages), keyboard on screen"), (lps, "Pocket, 480 x 800: the menus", "a bottom sheet, the menus as tabs"),
		(lc, "Console, 640 x 480", "full screen, no chrome; a keyboard to write"), (lcm, "Console: Home pressed", "its own menus revealed, console-styled")], cols = 3), "apps-letters.png")

# ===========================================================================================================
# The compact metrics: the same widgets at the three densities
# ===========================================================================================================
def metrics_sheet ():
	w, h = 1140, 540
	img = Image.new ("RGB", (w, h), (255, 255, 255))
	profs = [("Regular", "today's desktop: mouse, 1024 x 768 and up", dict (text = 13, row = 24, btn = 28, chk = 16, sb = 14, menu = 24, pad = 8)),
		 ("Compact", "small screen, keyboard + trackpad / stylus", dict (text = 12, row = 22, btn = 26, chk = 15, sb = 8, menu = 22, pad = 5)),
		 ("Touch", "finger: 7 mm targets (about 36 px at 133 dpi)", dict (text = 14, row = 36, btn = 38, chk = 22, sb = 4, menu = 36, pad = 10))]
	cw = w // 3
	for i, (name, sub, m) in enumerate (profs):
		x0 = i * cw + 20; y = 18
		if i: vline (img, i * cw, 14, h - 28, (222, 222, 224))
		text (img, x0, y, name, font (18, True), P["ink"]); text (img, x0, y + 26, sub, font (11), P["ink2"])
		y += 56
		panel_w = cw - 40
		rrect (img, x0, y, panel_w, h - y - 18, 10, P["face"])
		px, py = x0 + 14, y + 14
		f = font (m["text"])
		# buttons
		for j, lab in enumerate (["OK", "Cancel"]):
			bw = tw (lab, f) + 2 * m["pad"] + 24
			rrect (img, px + j * (bw + 8), py, bw, m["btn"], 6, ((249, 249, 251), (208, 208, 210)) if j else (lighten (P["accent"], 0.12), shade (P["accent"], 0.9)))
			ring (img, px + j * (bw + 8), py, bw, m["btn"], 6, (0, 0, 0), alpha = 80)
			text_c (img, px + j * (bw + 8), py, bw, m["btn"], lab, f, (255, 255, 255) if not j else P["ink"])
		text_r (img, x0 + panel_w - 14, py, m["btn"], "%d px" % m["btn"], font (10), P["ink2"])
		py += m["btn"] + 14
		# a check box and a field
		c = m["chk"]
		rrect (img, px, py, c, c, 3, (255, 255, 255)); ring (img, px, py, c, c, 3, (0, 0, 0), alpha = 120)
		glyph (img, px + 2, py + 2, c - 4, c - 4, lambda d, s, c = c: d.line ([(0.15 * (c - 4) * s, 0.5 * (c - 4) * s), (0.42 * (c - 4) * s, 0.78 * (c - 4) * s), (0.88 * (c - 4) * s, 0.2 * (c - 4) * s)], fill = 255, width = int (2 * s)), P["accent"])
		text_l (img, px + c + 8, py, c, "Show hidden files", f, P["ink"])
		py += c + 12
		fh = m["row"] + 4
		rrect (img, px, py, panel_w - 28, fh, 5, P["field"]); ring (img, px, py, panel_w - 28, fh, 5, P["accent"], t = 2 if i else 1)
		text_l (img, px + 8, py, fh, "letter-to-the-bank.odt", f, P["ink"])
		py += fh + 12
		# a list with its scroll bar, the focus row
		lh = m["row"]; rows = ["apps", "docs", "etc", "fonts", "music", "projects", "wallpapers"]
		lw = panel_w - 28
		listh = min (len (rows) * lh + 6, h - 18 - py - (2 * m["menu"] + 8) - 44)
		rrect (img, px, py, lw, listh, 5, P["paper"]); ring (img, px, py, lw, listh, 5, (0, 0, 0), alpha = 60)
		for j, r in enumerate (rows):
			ry = py + 3 + j * lh
			if ry + lh > py + listh - 2: break
			if j == 2:
				rrect (img, px + 3, ry, lw - 6 - m["sb"] - 2, lh, 4, P["accent"])
				if i: ring (img, px + 1, ry - 2, lw - 2 - m["sb"] - 2, lh + 4, 5, P["ink"], t = 1.5)
			text_l (img, px + 10, ry, lh, r, f, (255, 255, 255) if j == 2 else P["ink"])
		sbw = m["sb"]
		rrect (img, px + lw - sbw - 3, py + 3, sbw, listh - 6, sbw / 2, (208, 208, 210) if i == 0 else (255, 255, 255), alpha = 255 if i == 0 else 0)
		rrect (img, px + lw - sbw - 3, py + 6, sbw, listh // 3, sbw / 2, (150, 144, 140))
		py += listh + 12
		# a menu, two rows
		mh = m["menu"]
		rrect (img, px, py, lw, 2 * mh + 8, 6, (249, 249, 251)); ring (img, px, py, lw, 2 * mh + 8, 6, (0, 0, 0), alpha = 90)
		rrect (img, px + 4, py + 4, lw - 8, mh, 4, P["accent"])
		text_l (img, px + 12, py + 4, mh, "Copy", f, (255, 255, 255)); text_r (img, px + lw - 12, py + 4, mh, "Ctrl+C", f, (255, 255, 255))
		text_l (img, px + 12, py + 4 + mh, mh, "Paste", f, P["ink"]); text_r (img, px + lw - 12, py + 4 + mh, mh, "Ctrl+V", f, P["ink2"])
		sb = "overlay %d px" % m["sb"] if i else "%d px" % m["sb"]
		text (img, x0 + 14, h - 36, "text %d px  -  row %d  -  menu %d  -  scroll bar %s" % (m["text"], m["row"], m["menu"], sb), font (10), P["ink"])
	text (img, 20, h - 16, "", font (10), P["ink2"])
	return img

# ===========================================================================================================
# The navigation map: the screens and the keys between them
# ===========================================================================================================
def nav_map (thumbs):
	w, h = 1200, 640
	img = Image.new ("RGB", (w, h), (255, 255, 255))
	tw0 = 300; th0 = 180
	pos = {"home": (450, 40), "app": (450, 330), "switch": (60, 330), "quick": (840, 330), "menu": (840, 40), "split": (60, 40)}
	titles = {"home": "Launcher (Home)", "app": "An app, full screen", "switch": "Switcher", "quick": "Quick settings",
		  "menu": "The app's menus", "split": "Split view"}
	for k_, (x, y) in pos.items ():
		t = thumbs[k_].resize ((tw0, th0), Image.LANCZOS)
		rrect (img, x - 6, y - 6, tw0 + 12, th0 + 40, 10, (241, 241, 243)); ring (img, x - 6, y - 6, tw0 + 12, th0 + 40, 10, (0, 0, 0), alpha = 60)
		img.paste (t, (x, y)); text_c (img, x, y + th0 + 4, tw0, 26, titles[k_], font (13, True), P["ink"])
	d = ImageDraw.Draw (img)
	def arrow (x1, y1, x2, y2, lab, lx, ly, col = P["accent"]):
		d.line ([(x1, y1), (x2, y2)], fill = col, width = 3)
		a = math.atan2 (y2 - y1, x2 - x1)
		d.polygon ([(x2, y2), (x2 - 12 * math.cos (a - 0.4), y2 - 12 * math.sin (a - 0.4)), (x2 - 12 * math.cos (a + 0.4), y2 - 12 * math.sin (a + 0.4))], fill = col)
		f = font (11, True); ww = tw (lab, f) + 12
		rrect (img, lx - ww // 2, ly - 10, ww, 20, 10, (255, 255, 255)); ring (img, lx - ww // 2, ly - 10, ww, 20, 10, col, t = 1.5)
		text_c (img, lx - ww // 2, ly - 10, ww, 20, lab, f, col)
	arrow (585, 262, 585, 322, "Enter / tap", 585 - 70, 292)
	arrow (615, 322, 615, 262, "Super / Home key", 615 + 82, 292, (90, 96, 110))
	arrow (444, 420, 368, 420, "Alt+Tab", 406, 400)
	arrow (368, 460, 444, 460, "Enter / Esc", 406, 480, (90, 96, 110))
	arrow (756, 420, 832, 420, "Super+N / clock", 794, 400)
	arrow (832, 460, 756, 460, "Esc", 794, 480, (90, 96, 110))
	arrow (756, 340, 840, 230, "F10 / Menu key", 820, 290)
	arrow (450, 340, 366, 230, "Super+Left / Right", 380, 290)
	text (img, 60, h - 34, "Every screen is reached from the keyboard, the d-pad (Home, Tasks, Menu keys) or the touch screen: no gesture is the only way.",
	      font (12), P["ink2"])
	return img

# ===========================================================================================================
# Console mode's home as the browser (the user, 2026-10-08): the left column = the categories of the apps
# (their app.txt's `category`), the focused one glowing; the right panel = the apps of that category as
# glossy tiles. D-pad up / down: the categories; right or cross: into the right list (a tile glows, its
# description at the left); circle: back to the categories; triangle: a tile's options.
# ===========================================================================================================
CATC.setdefault ("Files", (120, 150, 190))
# The left column: "Recent" first (the home opens on what was used last: one press resumes it), then the
# categories of app.txt (Games takes the Emulators' ROMs too; Demos and Shell are not shown), then Files and
# Settings (the File Viewer's volumes, the Control Panel's applets).
HOME_CATS = ["Recent", "Games", "Productivity", "Internet", "Graphics", "Multimedia", "Programming", "System", "Files", "Settings"]
HOME_SUB = {"Recent": "what you used last", "Games": "%d games  -  ROMs of 6 consoles", "Productivity": "%d apps  -  documents, accounts",
	    "Internet": "%d apps  -  the web, mail, messages", "Graphics": "%d apps  -  painting, photos, 3D",
	    "Multimedia": "%d apps  -  music, films, a studio", "Programming": "%d apps  -  BASIC, logic, the GPIO",
	    "System": "%d apps  -  terminal, disks", "Files": "the SD card, USB, the network", "Settings": "screen, sound, pad, Wi-Fi"}
# what each app is: the line under its tile (the docs/04 catalog's words, short)
KIND = {"archiver": "archives", "tinycalc": "sums", "calendar": "planner", "cardfile": "card database", "clock": "alarms, timer",
	"graphcalc": "plots", "ledger": "accounts", "letters": "word processor", "notes": "quick notes", "pdf": "PDF reader",
	"rtfview": "RTF reader", "slides": "presentations", "sheet": "spreadsheet", "tinypad": "plain text",
	"courier": "HTTP client", "irc": "chat", "jet": "web browser", "lisa": "AI assistant", "mail": "mail client", "telegram": "messenger"}
DESC = {"letters": ("The word processor: styled RTF documents, tables of contents, printing.", True),
	"jet": ("Onyx's web browser, on WebKit: tabs, bookmarks, downloads.", True),
	"telegram": ("The messenger: your chats, one window a conversation.", True)}
RUNNING = {"letters", "media", "telegram"}
# the right panel's tiles: (icon's app, name, line, badge)
RECENT = [("letters", "Letters", "running", None), ("gbaemu", "Star Courier", "paused", "GBA"), ("media", "Media Player", "playing", None),
	  ("telegram", "Telegram", "2 unread", None), ("jet", "Jet", "today", None), ("terminal", "Terminal", "today", None),
	  ("ledger", "Ledger", "yesterday", None), ("doom", "Doom", "yesterday", None), ("fileviewer", "File Viewer", "Monday", None)]
GAMES = [("doom", "Doom", "yesterday", "Onyx"), ("gbaemu", "Star Courier", "Monday", "GBA"), ("snesemu", "Moon Garden", "1 Oct", "SNES"),
	 ("tetris", "Tetris", "30 Sep", "Onyx"), ("n64emu", "Turbo Kart 64", "28 Sep", "N64"), ("pinball", "Pinball", "27 Sep", "Onyx"),
	 ("critters", "Critters", "25 Sep", "Onyx"), ("gbemu", "Pixel Knight", "20 Sep", "GB"), ("solitaire", "Solitaire", "18 Sep", "Onyx")]
def home_tiles (cat):
	if cat == "Recent": return RECENT, len (RECENT) + 4
	if cat == "Games": return GAMES, len (apps_in ("Games")) + 8
	lst = [(a, app_name (a), KIND.get (a, ""), None) for a in apps_in (cat)]
	return lst, len (lst)
def home_count (cat):
	if cat == "Games": return len (apps_in ("Games"))
	return len (apps_in (cat))

def vignette (img, k = 0.55):
	w, h = img.size
	yy, xx = np.mgrid[0:h, 0:w]; r = np.sqrt (((xx - w / 2) / (w * 0.62)) ** 2 + ((yy - h * 0.45) / (h * 0.62)) ** 2)
	m = np.clip (1 - k * np.clip (r - 0.55, 0, 1) ** 1.5, 0, 1)
	return Image.fromarray ((np.asarray (img).astype (float) * m[..., None]).astype ("uint8"), "RGB")

def home_top (img):
	"""The gem and Onyx at the left, faint; Wi-Fi, the battery and the time at the right."""
	put (img, 22, 19, mask (16, 16, lambda d, s: g_gem (d, s, 1)), (170, 205, 245))
	text (img, 44, 15, "Onyx", cfont (17, "light"), (150, 176, 214))
	text_r (img, CW - 22, 13, 24, "12:34", cfont (19, "regular"), (220, 230, 246))
	text_r (img, CW - 82, 15, 22, "Thu 8 Oct", cfont (14, "regular"), (140, 164, 204))
	battery (img, CW - 186, 20, 78, (180, 196, 220))
	put (img, CW - 210, 17, mask (16, 14, lambda d, s: g_wifi (d, s)), (180, 196, 220))

def hint_strip (img):
	"""The bottom band under the buttons: a darker glass strip with a hairline."""
	y = CH - 46
	rrect (img, 0, y, CW, 46, 0, ((4, 8, 22), (2, 4, 12)), alpha = 150)
	hline (img, 0, y, CW, (120, 160, 230), alpha = 60)

def home_tile (img, x, y, s, app, badge = None, focus = False, running = False):
	"""tile3d with a running dot (Aqua, glowing) in its corner."""
	tile3d (img, x, y, s, app, badge, focus = focus)
	if running:
		cx, cy = x + s - 3, y + 3
		m = Image.new ("L", img.size, 0); ImageDraw.Draw (m).ellipse ([cx - 5, cy - 5, cx + 5, cy + 5], fill = 255)
		img.paste (add_glow (img, Image.new ("RGB", img.size, (90, 170, 255)), m, 4))
		rrect (img, cx - 4, cy - 4, 8, 8, 4, ((200, 235, 255), (80, 160, 255))); ring (img, cx - 4, cy - 4, 8, 8, 4, (10, 20, 50), alpha = 200)

PX, PY, PWD, PHT = 316, 62, 304, 370			# the right panel
def home_panel (img, cat, focus = None, lit = True):
	"""The right panel: the category's apps as tiles, three a row; `focus` = the index of the glowing tile."""
	glass (img, PX, PY, PWD, PHT, 14, alpha = 46 if lit else 30, edge = 110 if lit else 70)
	tiles, total = home_tiles (cat)
	glow_text (img, PX + 16, PY + 10, "Last used" if cat == "Recent" else ("Last played" if cat == "Games" else cat), cfont (17, "regular"), (206, 224, 248), radius = 3, strength = 1)
	cnt = str (total); text_r (img, PX + PWD - 16, PY + 10, 22, cnt, cfont (15, "regular"), (130, 156, 200))
	hline (img, PX + 16, PY + 38, PWD - 32, (140, 180, 240), alpha = 50)
	s = 54; fl = None
	for i, (a, name, line, badge) in enumerate (tiles[:9]):
		r, c = divmod (i, 3); cx = PX + 14 + c * 92; ty = PY + 48 + r * 100; tx = cx + (92 - s) // 2
		run = a in RUNNING and cat != "Games"
		if i == focus: fl = (tx, ty, cx, a, name, line, badge, run); continue
		home_tile (img, tx, ty, s, a, badge, running = run)
		f1 = cfont (14, "regular"); text_c (img, cx, ty + s + 5, 92, 16, ellipsize (name, f1, 88), f1, (214, 228, 248) if lit else (170, 190, 222))
		f2 = cfont (12, "regular"); text_c (img, cx, ty + s + 22, 92, 14, ellipsize (line, f2, 88), f2, (126, 152, 196))
	if total > 9:
		f = cfont (13, "regular"); more = "%d more" % (total - 9)
		w_ = tw (more, f) + 16; mx = PX + (PWD - w_) // 2
		glyph (img, mx, PY + PHT - 18, 10, 10, lambda d, s_: g_chev (d, s_, 1, "down"), (140, 170, 220))
		text (img, mx + 16, PY + PHT - 22, more, f, (140, 166, 210))
	if fl:							# the chosen tile: lifted, larger, glowing
		tx, ty, cx, a, name, line, badge, run = fl
		S2 = 64; x2 = tx - (S2 - s) // 2; y2 = ty - (S2 - s) // 2 - 2
		m = Image.new ("L", img.size, 0); ImageDraw.Draw (m).rounded_rectangle ([x2 - 6, y2 - 6, x2 + S2 + 6, y2 + S2 + 6], 16, fill = 190)
		img.paste (add_glow (img, Image.new ("RGB", img.size, GLOW), m, 11))
		home_tile (img, x2, y2, S2, a, badge, focus = True, running = run)
		f1 = cfont (14, "semi"); text_c (img, cx - 4, ty + s + 5, 100, 16, ellipsize (name, f1, 98), f1, (255, 255, 255))
		f2 = cfont (12, "regular"); text_c (img, cx, ty + s + 22, 92, 14, line, f2, (180, 206, 245))
		return (x2, y2, S2)

def home_left (img, sel):
	"""The categories, a column; the chosen one glowing with its line; the list scrolls, faded at its ends."""
	col = img.copy()
	y = 80 - max (0, sel - 1) * 40
	for i, cat in enumerate (HOME_CATS):
		cc = CATC.get (cat, (140, 150, 170))
		if i == sel:
			col = glow_box (col, 34, y - 6, 264, 64, 12)
			sub = HOME_SUB[cat]; sub = sub % home_count (cat) if "%d" in sub else sub
			rrect (col, 46, y + 12, 8, 8, 4, (lighten (cc, 0.4), cc))
			glow_text (col, 62, y - 3, cat, cfont (32, "semi"), (255, 255, 255), radius = 8, strength = 2)
			text (col, 64, y + 35, sub, cfont (14, "regular"), (190, 214, 245))
			# a thin light from the chosen word to its panel
			m = Image.new ("L", col.size, 0); ImageDraw.Draw (m).line ([298, y + 26, PX, y + 26], fill = 255, width = 2)
			col = add_glow (col, Image.new ("RGB", col.size, GLOW), m, 3); hline (col, 298, y + 25, PX - 298, (190, 220, 255), alpha = 200)
			y += 72
		else:
			rrect (col, 47, y + 13, 6, 6, 3, cc, alpha = 170)
			glow_text (col, 62, y + 2, cat, cfont (25, "light"), (172, 192, 224), radius = 5, strength = 1, glow = (30, 60, 120))
			y += 40
	fm = np.zeros ((CH, CW), float); ys = np.arange (CH)
	a = np.clip ((ys - 48) / 26, 0, 1) * np.clip ((434 - ys) / 40, 0, 1)
	fm[:, :PX - 2] = a[:, None]; fm[:, PX - 2:] = 1
	img.paste (col, (0, 0), Image.fromarray ((fm * 255).astype ("uint8"), "L"))
	return img

def home_left_compact (img, sel):
	"""In the right list: the categories small and dim, the chosen one marked (no glow: the focus is at the right)."""
	y = 64
	for i, cat in enumerate (HOME_CATS):
		cc = CATC.get (cat, (140, 150, 170))
		if i == sel:
			rrect (img, 34, y - 3, 264, 26, 8, ((50, 90, 170), (24, 46, 100)), alpha = 150); ring (img, 34, y - 3, 264, 26, 8, (150, 190, 250), alpha = 120)
			rrect (img, 46, y + 7, 7, 7, 3.5, (lighten (cc, 0.4), cc))
			text (img, 62, y - 1, cat, cfont (17, "semi"), (240, 246, 255))
			glyph (img, 282, y + 5, 10, 10, lambda d, s: g_chev (d, s, 1, "right"), (200, 224, 255))
		else:
			rrect (img, 47, y + 8, 5, 5, 2.5, cc, alpha = 120)
			text (img, 62, y, cat, cfont (16, "light"), (130, 150, 186))
		y += 25
	return img

def wrap2 (s_, f, w):
	out, cur = [], ""
	for wd in s_.split ():
		t = (cur + " " + wd).strip ()
		if tw (t, f) <= w: cur = t
		else: out.append (cur); cur = wd
	return out + [cur] if cur else out

def home_desc (img, a, name, cat):
	"""The chosen app's card, low at the left: its name, what it is, a line about it, the keyboard, running."""
	x, y, w, h = 24, 318, 280, 114
	glass (img, x, y, w, h, 12, alpha = 70, edge = 130)
	draw_icon (img, a, x + 14, y + 12, 32)
	glow_text (img, x + 56, y + 8, name, cfont (22, "semi"), (255, 255, 255), radius = 4, strength = 1)
	cc = CATC.get (cat, (140, 150, 170)); rrect (img, x + 58, y + 40, 6, 6, 3, cc)
	text (img, x + 70, y + 34, cat + "  -  " + KIND.get (a, ""), cfont (13, "regular"), (150, 178, 220))
	d, kb = DESC.get (a, ("", False))
	f = cfont (14, "regular")
	for i, ln in enumerate (wrap2 (d, f, w - 28)[:2]): text (img, x + 14, y + 54 + i * 18, ln, f, (204, 220, 244))
	bx = x + 14; by = y + h - 22
	if kb:
		put (img, bx, by + 1, mask (16, 16, lambda d_, s: g_kbd (d_, s)), (240, 200, 120))
		text (img, bx + 22, by, "keyboard recommended", cfont (13, "regular"), (240, 214, 160)); bx += 22 + tw ("keyboard recommended", cfont (13, "regular")) + 16
	if a in RUNNING:
		m = Image.new ("L", img.size, 0); ImageDraw.Draw (m).ellipse ([bx, by + 5, bx + 8, by + 13], fill = 255)
		img.paste (add_glow (img, Image.new ("RGB", img.size, (90, 170, 255)), m, 3))
		rrect (img, bx, by + 5, 8, 8, 4, ((200, 235, 255), (80, 160, 255)))
		text (img, bx + 14, by, "running", cfont (13, "regular"), (170, 210, 255))

def home_base (seed = 4):
	img = space (CW, CH, seed = seed)
	img = vignette (img)
	home_top (img); hint_strip (img)
	return img

def console_home2 (sel = 1):
	img = home_base ()
	img = home_left (img, sel)
	home_panel (img, HOME_CATS[sel])
	console_hints (img, [("x", "Enter"), ("o", "Back"), ("t", "Options"), ("L1/R1", "Page")])
	return img

def console_home_inlist (sel = 2, focus = 7, options = False):
	img = home_base ()
	img = home_left_compact (img, sel)
	cat = HOME_CATS[sel]
	geo = home_panel (img, cat, focus = focus)
	a, name, line, badge = home_tiles (cat)[0][focus]
	if not options:
		home_desc (img, a, name, cat)
		console_hints (img, [("x", "Open"), ("t", "Options"), ("o", "Categories"), ("L1/R1", "Page")])
		return img
	# triangle: a small glass menu beside the tile, the rest dimmed
	x2, y2, S2 = geo
	keep = img.copy (); km = Image.new ("L", img.size, 0)
	ImageDraw.Draw (km).rounded_rectangle ([x2 - 5, y2 - 5, x2 + S2 + 5, y2 + S2 + 5], 14, fill = 255)
	img = dim (img, 120, (2, 6, 20)); img.paste (keep, (0, 0), km.filter (ImageFilter.GaussianBlur (2)))
	items = ["Open", "Pin to Recent", "Close app", "Info"] if a in RUNNING else ["Open", "Pin to Recent", "Info"]
	mw, mh = 190, 46 + 36 * len (items); mx = x2 - mw - 16; my = min (y2 - 30, CH - 56 - mh)
	rrect (img, mx, my, mw, mh, 12, (6, 12, 32), alpha = 170)
	glass (img, mx, my, mw, mh, 12, alpha = 120, edge = 170)
	draw_icon (img, a, mx + 12, my + 10, 24)
	text (img, mx + 44, my + 11, name, cfont (17, "semi"), (236, 244, 255))
	hline (img, mx + 12, my + 42, mw - 24, (150, 190, 250), alpha = 70)
	for i, it in enumerate (items):
		iy = my + 48 + i * 36
		if i == 1:
			img = glow_box (img, mx + 8, iy, mw - 16, 32, 8)
			text (img, mx + 22, iy + 4, it, cfont (18, "semi"), (255, 255, 255))
		else:
			text (img, mx + 22, iy + 4, it, cfont (18, "light"), (255, 170, 160) if it == "Close app" else (206, 220, 244))
	# a small pointer from the menu to its tile
	glyph (img, mx + mw - 1, y2 + S2 // 2 - 6, 8, 12, lambda d, s: d.polygon ([(0, 0), (8 * s, 6 * s), (0, 12 * s)], fill = 255), (150, 190, 250), alpha = 170)
	console_hints (img, [("x", "Choose"), ("o", "Close")])
	return img

def console_home_main ():
	h1 = console_home2 (1); save (h1, "console-home-v2.png")
	h2 = console_home2 (2); save (h2, "console-home-productivity.png")
	h3 = console_home_inlist (); save (h3, "console-home-inlist.png")
	h4 = console_home_inlist (options = True); save (h4, "console-home-options.png")
	save (console_sheet ([(h1, "1. Home: the categories at the left, Games focused, its games at the right"),
			      (h2, "2. Down: Productivity focused, its apps at the right"),
			      (h3, "3. Right or X: into the list, Letters glowing, its card at the left"),
			      (h4, "4. Triangle on a tile: its options")],
			     "Console mode's home: the categories and their apps (640 x 480)"), "console-home-sheet.png")

# ===========================================================================================================
# The File Viewer in console mode (user/Apps/fileviewer): its places (Personal / Computer / Network, SD:/etc/
# places.ini) as the SidePanel's console column (a d-pad column, L1 / R1 the sections), its column browser as one
# big focusable list (right enters a folder, circle goes up), its path bar as the breadcrumb, its preview column
# as a panel, its right-click menu on triangle, its menus (File, Go, Edit) revealed by Home. A file's icon = the
# icon of the app that opens it (SD:/etc/fileassoc.ini, as the File Viewer and the dock read it).
# ===========================================================================================================
def _assoc ():
	m = {}
	try:
		for line in open (os.path.join (SD, "etc", "fileassoc.ini")):
			line = line.split ("#")[0]
			if "=" in line:
				k, v = [s.strip () for s in line.split ("=", 1)]
				if v: m[k.lower ()] = v
	except OSError: pass
	return m
FASSOC = _assoc ()
FKIND = {"card": "Cardfile cards", "odp": "presentation", "pptx": "presentation", "xlsx": "spreadsheet", "cod": "statement",
	 "ledger": "accounts", "rtf": "RTF document", "png": "PNG picture", "jpg": "JPEG picture", "txt": "text", "ini": "settings"}
def fsize (n):
	return "%d B" % n if n < 1024 else ("%.1f KB" % (n / 1024) if n < 10240 else ("%d KB" % round (n / 1024) if n < 1048576 else "%.1f MB" % (n / 1048576)))
def folder_list (rel):
	"""A real folder of the card: (name, is_dir, line, size, opener app) -- folders first, as the File Viewer."""
	p = os.path.join (SD, rel); out = []
	for n in sorted (os.listdir (p), key = lambda s: (not os.path.isdir (os.path.join (p, s)), s.lower ())):
		if n.startswith ("."): continue
		q = os.path.join (p, n)
		if os.path.isdir (q):
			k = len ([x for x in os.listdir (q) if not x.startswith (".")]); out.append ((n, True, "%d item%s" % (k, "" if k == 1 else "s"), "", None))
		else:
			ext = n.rsplit (".", 1)[-1].lower (); out.append ((n, False, FKIND.get (ext, ext.upper () + " file"), fsize (os.path.getsize (q)), FASSOC.get (ext)))
	return out

def g_folder (d, s, k = 1):
	u = s * k
	d.rounded_rectangle ([1 * u, 2 * u, 8 * u, 6 * u], 1.2 * u, fill = 160)
	d.rounded_rectangle ([1 * u, 4 * u, 19 * u, 16 * u], 2 * u, fill = 255)
def g_page (d, s, k = 1):
	u = s * k
	d.polygon ([(3 * u, 1 * u), (12 * u, 1 * u), (17 * u, 6 * u), (17 * u, 19 * u), (3 * u, 19 * u)], fill = 255)
	d.polygon ([(12 * u, 1 * u), (12 * u, 6 * u), (17 * u, 6 * u)], fill = 140)
def g_drive (d, s, k = 1):
	u = s * k
	d.rounded_rectangle ([1 * u, 5 * u, 19 * u, 15 * u], 2.5 * u, fill = 255); d.ellipse ([14 * u, 9 * u, 16 * u, 11 * u], fill = 0)
def g_usb (d, s, k = 1):
	u = s * k
	d.rounded_rectangle ([6 * u, 1 * u, 14 * u, 7 * u], 1 * u, outline = 255, width = int (1.6 * u)); d.rounded_rectangle ([4 * u, 7 * u, 16 * u, 19 * u], 2.5 * u, fill = 255)
def g_trash (d, s, k = 1):
	u = s * k
	d.rectangle ([3 * u, 4 * u, 17 * u, 5.6 * u], fill = 255); d.rectangle ([8 * u, 2 * u, 12 * u, 4 * u], fill = 255)
	d.polygon ([(4.5 * u, 7 * u), (15.5 * u, 7 * u), (14.5 * u, 19 * u), (5.5 * u, 19 * u)], fill = 255)
def g_globe (d, s, k = 1):
	u = s * k
	d.ellipse ([2 * u, 2 * u, 18 * u, 18 * u], outline = 255, width = int (1.6 * u)); d.ellipse ([6.5 * u, 2 * u, 13.5 * u, 18 * u], outline = 255, width = int (1.4 * u))
	d.line ([2 * u, 10 * u, 18 * u, 10 * u], fill = 255, width = int (1.4 * u))
def g_note (d, s, k = 1):
	u = s * k
	d.line ([8 * u, 4 * u, 8 * u, 15 * u], fill = 255, width = int (1.8 * u)); d.line ([8 * u, 4 * u, 16 * u, 2 * u, 16 * u, 13 * u], fill = 255, width = int (1.8 * u))
	d.ellipse ([3.5 * u, 12.5 * u, 8.5 * u, 17 * u], fill = 255); d.ellipse ([11.5 * u, 10.5 * u, 16.5 * u, 15 * u], fill = 255)
def g_photo (d, s, k = 1):
	u = s * k
	d.rounded_rectangle ([1 * u, 3 * u, 19 * u, 17 * u], 2 * u, outline = 255, width = int (1.6 * u))
	d.polygon ([(3 * u, 15 * u), (8 * u, 9 * u), (11 * u, 12 * u), (13 * u, 10 * u), (17 * u, 15 * u)], fill = 255); d.ellipse ([12.5 * u, 5 * u, 15.5 * u, 8 * u], fill = 255)
def g_pad (d, s, k = 1):
	u = s * k
	d.rounded_rectangle ([1 * u, 6 * u, 19 * u, 15 * u], 4.5 * u, fill = 255)
	d.rectangle ([4.5 * u, 9.8 * u, 8.5 * u, 11.2 * u], fill = 0); d.rectangle ([5.8 * u, 8.5 * u, 7.2 * u, 12.5 * u], fill = 0)
	d.ellipse ([12.5 * u, 8.3 * u, 14.5 * u, 10.3 * u], fill = 0); d.ellipse ([14.5 * u, 10.5 * u, 16.5 * u, 12.5 * u], fill = 0)

def mouse_pointer (img, x, y):
	"""The mouse's arrow (console keeps the mouse: hover = focus, right click = triangle)."""
	pts = [(0, 0), (0, 17), (4.5, 13), (7.5, 20), (10, 19), (7, 12.5), (12.5, 12.5)]
	glyph (img, x - 1, y - 1, 16, 24, lambda d, s: d.polygon ([((a + 1.5) * s, (b + 1.5) * s) for a, b in pts], fill = 255), (0, 0, 0), alpha = 230)
	glyph (img, x, y, 14, 22, lambda d, s: d.polygon ([((a + 0.5) * s, (b + 0.5) * s) for a, b in pts], fill = 255), (255, 255, 255))

# the places, as places.ini would hold them: pinned folders, the Trash; the volumes; the network
FV_PLACES = [("Personal", [("Documents", g_folder), ("Music", g_note), ("Pictures", g_photo), ("ROMs", g_pad), ("Trash", g_trash)]),
	     ("Computer", [("SD Card", g_drive), ("USB Stick", g_usb)]),
	     ("Network", [("Connect to Server...", g_globe)])]
FX, FY, FWD = 202, 60, 424				# the list
def fv_base (crumbs, count):
	img = space (CW, CH, seed = 31, towers = False); img = dim (img, 70, (2, 4, 14)); img = vignette (img, 0.45)
	draw_icon (img, "fileviewer", 16, 13, 26)
	x = 52; f = cfont (17, "regular"); fs = cfont (17, "semi")
	for i, c in enumerate (crumbs):
		last = i == len (crumbs) - 1; ww = tw (c, fs if last else f) + 22
		if last:
			rrect (img, x, 12, ww, 28, 14, ((80, 140, 240), (30, 70, 170)), alpha = 190); ring (img, x, 12, ww, 28, 14, (190, 220, 255), alpha = 200)
			text_c (img, x, 12 - 1, ww, 28, c, fs, (255, 255, 255))
		else:
			rrect (img, x, 12, ww, 28, 14, (20, 34, 70), alpha = 150); ring (img, x, 12, ww, 28, 14, (110, 140, 200), alpha = 110)
			text_c (img, x, 12 - 1, ww, 28, c, f, (190, 208, 236))
			glyph (img, x + ww + 7, 21, 10, 10, lambda d, s: g_chev (d, s, 1, "right"), (130, 160, 210))
		x += ww + (24 if not last else 0)
	text_r (img, CW - 18, 14, 24, count, cfont (14, "regular"), (140, 166, 210))
	hint_strip (img)
	return img

def fv_places (img, current, focus = False):
	glass (img, 14, FY, 176, 372, 12, alpha = 40, edge = 90)
	y = FY + 10
	for gi, (grp, rows) in enumerate (FV_PLACES):
		text (img, 28, y, grp.upper (), font (9, True), (110, 136, 180)); y += 18
		for name, gl in rows:
			if name == current:
				if focus: img = glow_box (img, 20, y - 1, 164, 30, 8)
				else: rrect (img, 20, y - 1, 164, 30, 8, ((50, 90, 170), (24, 46, 100)), alpha = 150); ring (img, 20, y - 1, 164, 30, 8, (150, 190, 250), alpha = 110)
			on = name == current
			put (img, 30, y + 6, mask (18, 18, lambda d, s, g = gl: g (d, s, 0.9)), (230, 240, 255) if on else (130, 160, 210))
			ff = cfont (16, "semi" if on else "light")
			text (img, 56, y + 3, ellipsize (name, ff, 124), ff, (255, 255, 255) if on else (186, 202, 230))
			y += 32
		y += 8
	# the L1 / R1 marks: the sections, from anywhere
	shoulder (img, 24, FY + 372 - 26, "L1"); shoulder (img, 150, FY + 372 - 26, "R1")
	text_c (img, 58, FY + 372 - 27, 88, 18, "places", cfont (12, "regular"), (110, 136, 180))
	return img

def fv_icon (img, x, y, isdir, opener, sz = 26, lit = False):
	if isdir:
		put (img, x, y + 1, mask (sz, sz, lambda d, s: g_folder (d, s, sz / 20)), ((150, 200, 255), (60, 120, 220)) and grad (sz, sz, (170, 214, 255), (60, 120, 230)))
	elif opener: draw_icon (img, opener, x, y, sz)
	else: put (img, x + 1, y, mask (sz, sz, lambda d, s: g_page (d, s, sz / 20)), (200, 212, 232))

def fv_list (img, entries, focus, x = FX, w = FWD, rows = 10, rh = 34):
	glass (img, x, FY, w, 372, 12, alpha = 30, edge = 70)
	y = FY + 8; fr = None
	for i, (n, isdir, line, size, opener) in enumerate (entries[:rows]):
		if i == focus: fr = y; img = glow_box (img, x + 6, y, w - 12, rh - 2, 8)
		else:
			if i % 2: rrect (img, x + 6, y, w - 12, rh - 2, 8, (120, 160, 230), alpha = 10)
		fv_icon (img, x + 14, y + 3, isdir, opener)
		lit = i == focus
		fn = cfont (18 if lit else 17, "semi" if lit else "light")
		right = x + w - 18
		if isdir: glyph (img, right - 8, y + 11, 10, 10, lambda d, s: g_chev (d, s, 1, "right"), (220, 236, 255) if lit else (120, 150, 200)); right -= 18
		fz = cfont (13, "regular")
		if size and w > 300: text_r (img, right, y + 4, 24, size, fz, (220, 234, 255) if lit else (130, 156, 200)); right -= 62
		if w > 300: text_r (img, right, y + 4, 24, line, fz, (200, 220, 250) if lit else (110, 136, 182)); right -= tw (line, fz) + 12
		elif isdir: text_r (img, right, y + 4, 24, line, fz, (200, 220, 250) if lit else (110, 136, 182)); right -= tw (line, fz) + 12
		text (img, x + 50, y + 5, ellipsize (n, fn, right - x - 56), fn, (255, 255, 255) if lit else (210, 222, 244))
		y += rh
	if len (entries) > rows:
		f = cfont (13, "regular"); more = "%d more" % (len (entries) - rows); w_ = tw (more, f) + 16; mx = x + (w - w_) // 2
		glyph (img, mx, FY + 372 - 18, 10, 10, lambda d, s: g_chev (d, s, 1, "down"), (140, 170, 220)); text (img, mx + 16, FY + 372 - 22, more, f, (140, 166, 210))
	return img, fr

FV_HINTS = [("x", "Open"), ("o", "Up"), ("t", "Actions"), ("L1/R1", "Places"), ("HOME", "Menu")]
def console_files (actions = False, menu = False):
	ents = folder_list ("docs")
	img = fv_base (["SD Card", "docs"], "%d items" % len (ents))
	img = fv_places (img, "Documents")
	foc = [e[0] for e in ents].index ("letters-tour.rtf")
	img, fr = fv_list (img, ents, foc)
	if not actions and not menu:
		console_hints (img, FV_HINTS); return img
	if menu:
		img = dim (img, 150, (4, 8, 24)); box (img, 0, CH - 46, CW, 46, (6, 10, 26), alpha = 200)
		console_menubar (img, ("File", "Go", "Edit"), "Go", [("SD Card", ""), ("USB Stick", ""), ("Eject USB Stick", ""), ("Disks (Format...)", ""), ("Trash", ""),
				 ("Connect to Server...", ""), "-", ("Pin This Folder...", ""), "-", ("Restore from Trash", ""), ("Empty Trash...", "")], sel = 1)
		console_hints (img, [("L1/R1", "Menus"), ("x", "Choose"), ("o", "Close"), ("HOME", "Close")])
		return img
	# triangle (or a right click: the pointer) on a file -- the File Viewer's own row menu, console-styled
	keep = img.copy (); km = Image.new ("L", img.size, 0)
	ImageDraw.Draw (km).rounded_rectangle ([FX + 4, fr - 2, FX + FWD - 4, fr + 34], 9, fill = 255)
	img = dim (img, 120, (2, 6, 20)); img.paste (keep, (0, 0), km.filter (ImageFilter.GaussianBlur (2)))
	items = [("Open", "Letters"), ("Open with...", ""), ("Copy", ""), ("Move...", ""), ("Rename...", "kbd"), ("Move to Trash", ""), ("Info", "")]
	mw, rh = 236, 34; mh = 50 + rh * len (items); mx = FX + 170; my = max (FY - 6, fr - mh - 6)
	rrect (img, mx, my, mw, mh, 12, (6, 12, 32), alpha = 180); glass (img, mx, my, mw, mh, 12, alpha = 120, edge = 170)
	draw_icon (img, "letters", mx + 12, my + 11, 24)
	text (img, mx + 44, my + 12, "letters-tour.rtf", cfont (16, "semi"), (236, 244, 255))
	hline (img, mx + 12, my + 44, mw - 24, (150, 190, 250), alpha = 70)
	for i, (it, extra) in enumerate (items):
		iy = my + 50 + i * rh
		if i == 1:
			img = glow_box (img, mx + 8, iy, mw - 16, rh - 3, 8); text (img, mx + 22, iy + 4, it, cfont (18, "semi"), (255, 255, 255))
		else: text (img, mx + 22, iy + 4, it, cfont (18, "light"), (255, 170, 160) if it == "Move to Trash" else (206, 220, 244))
		if extra == "kbd": put (img, mx + mw - 34, iy + 9, mask (16, 16, lambda d, s: g_kbd (d, s)), (240, 200, 120))
		elif extra: text_r (img, mx + mw - 16, iy + 4, 24, extra, cfont (13, "regular"), (140, 166, 210))
	mouse_pointer (img, FX + 128, fr + 18)
	console_hints (img, [("x", "Choose"), ("o", "Close")])
	return img

def fv_rail (img, current):
	"""The places folded to a rail of icons (the list and a preview need the width); L1 / R1 still step through them."""
	glass (img, 14, FY, 44, 372, 12, alpha = 40, edge = 90)
	y = FY + 12
	for grp, rows in FV_PLACES:
		for name, gl in rows:
			on = name == current
			if on: rrect (img, 18, y - 4, 36, 30, 8, ((50, 90, 170), (24, 46, 100)), alpha = 170); ring (img, 18, y - 4, 36, 30, 8, (150, 190, 250), alpha = 130)
			put (img, 27, y + 2, mask (18, 18, lambda d, s, g = gl: g (d, s, 0.9)), (235, 244, 255) if on else (120, 150, 200))
			y += 34
		hline (img, 24, y - 4, 24, (120, 150, 210), alpha = 60); y += 6
	return img

def console_files_preview (pick = "sunset-sea.jpg"):
	ents = folder_list ("docs/pictures")
	img = fv_base (["SD Card", "docs", "pictures"], "%d items" % len (ents))
	img = fv_rail (img, "Pictures")
	foc = [e[0] for e in ents].index (pick)
	img, fr = fv_list (img, ents, foc, x = 66, w = 250)
	# the preview: the File Viewer's preview column (name, type, size, the picture) as a glass panel
	px, pw = 324, 302
	glass (img, px, FY, pw, 372, 12, alpha = 46, edge = 110)
	pic = Image.open (os.path.join (SD, "docs", "pictures", pick)).convert ("RGB")
	tw_ = pw - 28; th_ = int (tw_ * pic.height / pic.width); thumb = pic.resize ((tw_, th_), Image.LANCZOS)
	m = Image.new ("L", img.size, 0); ImageDraw.Draw (m).rectangle ([px + 14, FY + 14, px + 14 + tw_, FY + 14 + th_], fill = 200)
	img = add_glow (img, Image.new ("RGB", img.size, (60, 120, 220)), m, 8)
	img.paste (thumb, (px + 14, FY + 14)); ring (img, px + 14, FY + 14, tw_, th_, 2, (220, 236, 255), alpha = 200)
	ref = thumb.transpose (Image.FLIP_TOP_BOTTOM).crop ((0, 0, tw_, th_ // 4))
	rm = Image.fromarray ((np.linspace (60, 0, th_ // 4)[:, None].repeat (tw_, 1)).astype ("uint8"), "L"); img.paste (ref, (px + 14, FY + 16 + th_), rm)
	y = FY + 24 + th_ + th_ // 4
	glow_text (img, px + 16, y, pick, cfont (21, "semi"), (255, 255, 255), radius = 4, strength = 1)
	n, isdir, kind, size, opener = ents[foc]
	f = cfont (14, "regular")
	for i, (k_, v) in enumerate ([("Type", kind), ("Size", size), ("Picture", "%d x %d" % pic.size)]):
		text (img, px + 16, y + 34 + i * 21, k_, f, (120, 146, 190)); text (img, px + 86, y + 34 + i * 21, v, f, (210, 224, 246))
	yy = y + 34 + 3 * 21 + 6
	hline (img, px + 16, yy, pw - 32, (140, 180, 240), alpha = 50)
	text (img, px + 16, yy + 8, "Opens with", f, (120, 146, 190))
	draw_icon (img, opener, px + 100, yy + 6, 22); text (img, px + 128, yy + 7, app_name (opener), cfont (15, "regular"), (226, 236, 252))
	console_hints (img, FV_HINTS)
	return img

def console_files_main ():
	f1 = console_files (); save (f1, "console-files.png")
	f2 = console_files (actions = True); save (f2, "console-files-actions.png")
	f3 = console_files (menu = True); save (f3, "console-files-menu.png")
	f4 = console_files_preview (); save (f4, "console-files-preview.png")
	save (console_sheet ([(f1, "1. SD:/docs: the places at the left, the folder as big rows"),
			      (f4, "2. A picture focused: the places fold to a rail, the preview panel"),
			      (f2, "3. Triangle or a right click on a file: its actions"),
			      (f3, "4. Home: the File Viewer's own menus, Go open")],
			     "The File Viewer in console mode (640 x 480)"), "console-files-sheet.png")

# ===========================================================================================================
def save (img, name):
	p = os.path.join (OUT, name); img.save (p, optimize = True); print ("  ", os.path.relpath (p, ROOT), img.size)

def sheet (items, cols, title, pad = 24, gap = 24):
	"""An overview: pictures (each with a caption) in a grid, scaled to one width."""
	cw = max (i[0].width for i in items)
	rows = [items[i:i + cols] for i in range (0, len (items), cols)]
	cells = []
	for r in rows:
		cr = [caption_sheet (im, t, s) for im, t, s in r]; cells.append (cr)
	W = pad * 2 + sum (c.width for c in cells[0]) + gap * (cols - 1)
	H = pad * 2 + 40 + sum (max (c.height for c in r) for r in cells) + gap * (len (cells) - 1)
	out = Image.new ("RGB", (W, H), (233, 233, 235))
	text (out, pad, pad, title, font (20, True), P["ink"])
	y = pad + 40
	for r in cells:
		x = pad
		for c in r: out.paste (c, (x, y)); x += c.width + gap
		y += max (c.height for c in r) + gap
	return out

def fit (img, w):
	return img.resize ((w, int (img.height * w / img.width)), Image.LANCZOS)

def console_sheet (pics, title = "Console mode: the PS2's mood, a gamepad first (640 x 480)"):
	"""Console mode's screens on one page, two a row, at their real size."""
	gap, pad = 20, 24; rows = (len (pics) + 1) // 2
	out = Image.new ("RGB", (pad * 2 + 2 * CW + gap, pad + 40 + rows * (CH + gap + 24)), (16, 18, 24))
	text (out, pad, pad - 4, title, font (20, True), (230, 236, 246))
	for i, (pic, lab) in enumerate (pics):
		r, c = divmod (i, 2); x = pad + c * (CW + gap); y = pad + 40 + r * (CH + gap + 24)
		text (out, x, y, lab, font (13, True), (170, 186, 210)); out.paste (pic, (x, y + 22))
	return out

def main ():
	os.makedirs (OUT, exist_ok = True)
	print ("mockup_compact.py ->", os.path.relpath (OUT, ROOT))
	for old in ("concept-c-carousel.png",):			# (the carousel became Console mode)
		if os.path.exists (os.path.join (OUT, old)): os.remove (os.path.join (OUT, old))
	# Pocket at 800 x 480: the detailed screens
	home = pocket_home (); save (home, "pocket-home.png")
	term = pocket_terminal (); save (term, "pocket-terminal.png")
	led = pocket_ledger (); save (led, "pocket-ledger.png")
	men = pocket_menu (); save (men, "pocket-menu.png")
	sw = pocket_switcher (); save (sw, "pocket-switcher.png")
	qk = pocket_quick (); save (qk, "pocket-quick.png")
	sp = pocket_split (); save (sp, "pocket-split.png")
	fx = pocket_fixed (); save (fx, "pocket-fixed.png")
	save (pocket_home (cat = "Recent", focus = 0, search = "led"), "pocket-search.png")
	# Pocket is one adaptive layout: the same screens at four sizes
	save (adaptive_sheet (ADAPT_SIZES[:2], False, "Pocket in landscape: the same screens at 800 x 480 and 640 x 480"), "pocket-adaptive-landscape.png")
	save (adaptive_sheet (ADAPT_SIZES[2:], True, "Pocket in portrait: the same screens at 480 x 800 (1.5x) and 480 x 640 (2x)"), "pocket-adaptive-portrait.png")
	cmp_ = [adaptive_home (W, H, k) for _, W, H, k in ADAPT_SIZES]
	hh = 400; parts = [fit (c, int (c.width * hh / c.height)) for c in cmp_]
	strip = Image.new ("RGB", (sum (p_.width for p_ in parts) + 22 * (len (parts) + 1), hh + 70), (236, 236, 238))
	text (strip, 22, 10, "One Pocket, four screens: the launcher reflows (shown at the same height)", font (16, True), P["ink"])
	x = 22
	for p_, (lab, W, H, k) in zip (parts, ADAPT_SIZES):
		strip.paste (p_, (x, 44)); text (strip, x, hh + 48, lab, font (11, True), P["ink2"]); x += p_.width + 22
	save (strip, "pocket-adaptive.png")
	# Pocket in portrait on a 240 x 320 slate (2x): its details (the keyboard on screen, the menus as a sheet)
	sh = slate_home (); sa = slate_app (); sm = slate_menu ()
	gap = 30
	trio = [frame_slate (sh, "Home: the tabs scroll"), frame_slate (sa, "Notes, the keyboard on screen"), frame_slate (sm, "The menus: a sheet")]
	out = Image.new ("RGB", (sum (t.width for t in trio) + gap * 4, trio[0].height + 2 * gap + 30), (255, 255, 255))
	text (out, gap, 14, "Pocket in portrait, on a 240 x 320 slate (drawn at 2x: 480 x 640)", font (16, True), P["ink"])
	x = gap
	for t in trio: out.paste (t, (x, gap + 30)); x += t.width + gap
	save (out, "pocket-portrait.png")
	one = Image.new ("RGB", (240 * 3 + 40, 320 + 60), (255, 255, 255))
	text (one, 10, 8, "The same three at their real size (1x)", font (13, True), P["ink"])
	for i, s_ in enumerate ([sh, sa, sm]): one.paste (s_.resize ((240, 320), Image.LANCZOS), (10 + i * 250, 40))
	save (one, "pocket-portrait-1x.png")
	# Console mode
	ch_ = console_home (); save (ch_, "console-home.png")
	cl = console_library (); save (cl, "console-library.png")
	cs = console_settings (); save (cs, "console-settings.png")
	co = console_overlay (); save (co, "console-overlay.png")
	cw_ = console_switcher (); save (cw_, "console-switcher.png")
	save (frame_handheld (ch_, "Console mode on a Pi handheld, 640 x 480"), "console-handheld.png")
	console_home_main ()					# the home as the browser: categories and their apps
	console_files_main ()					# the File Viewer in console mode
	save (console_sheet ([(ch_, "Home"), (cl, "Games: the library"), (cs, "Settings"), (co, "In a game: Home pressed"), (cw_, "Running: switch")]), "console-overview.png")
	# the alternatives studied
	a = concept_a (); save (frame_clamshell (a, "640 x 480 -- a Zaurus SL-C-like clamshell"), "concept-a-tabs.png")
	b = concept_b (); save (b, "concept-b-netbook.png")
	save (metrics_sheet (), "metrics.png")
	save (nav_map ({"home": home, "app": led, "switch": sw, "quick": qk, "menu": men, "split": sp}), "navigation.png")
	desk = Image.open (os.path.join (SHOTS, "milk.png")).convert ("RGB")
	port = Image.new ("RGB", (800, 480), (34, 58, 85))
	p1 = fit (cmp_[2], int (480 * 480 / 800)); p1 = p1.resize ((int (p1.width * 440 / p1.height), 440), Image.LANCZOS)
	p2 = cmp_[3].resize ((int (480 * 440 / 640), 440), Image.LANCZOS)
	port.paste (p1, (400 - p1.width - 20, 20)); port.paste (p2, (420, 20))
	ov = sheet ([(fit (bezel (desk), 560), "Desktop (today)", "the windowed desktop, Milk: 1024 x 768 and up"),
		     (fit (bezel (home), 560), "Pocket, landscape", "one app at a time, Onyx's menu bar kept (800 x 480)"),
		     (fit (bezel (port), 560), "Pocket, portrait", "the same layout, reflowed (480 x 800, 240 x 320)"),
		     (fit (bezel (ch_), 560), "Console", "the PS2's mood, a gamepad first (640 x 480)"),
		     (fit (bezel (a), 560), "Studied: A -- Tabs", "Qtopia: menus inside the apps -- folded into Pocket"),
		     (fit (bezel (b, 20), 560), "Later: B -- Netbook", "UNR: a sidebar, windows as tabs (1280 x 720)")], 2,
		    "Onyx's modes: desktop, pocket, console -- and the concepts studied")
	save (ov, "overview.png")
	apps_main ()						# three real apps in every mode

if __name__ == "__main__":
	import sys
	if sys.argv[1:] == ["console-home"]: os.makedirs (OUT, exist_ok = True); console_home_main ()	# (only the console home's)
	elif sys.argv[1:] == ["console-files"]: os.makedirs (OUT, exist_ok = True); console_files_main ()	# (only the File Viewer's)
	else: main ()
