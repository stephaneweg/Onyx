#!/usr/bin/env python3
# Copyright (c) 2026 the Onyx authors -- MIT licence (see docs/LICENSING.md).
"""mockup_console_switch.py -- console mode v4: the console shell (user/Apps/consolehome) in the spirit of a hybrid
handheld's home menu (the Switch 1 and 2's manner: big square tiles, a thin pulsing ring, a status bar, round
buttons) with the user's layout: THE CATEGORIES AT THE BOTTOM as a row of round tiles (one per console that has
games, then Onyx's own games, Apps, Settings), THE CONTENT ABOVE as one row of big tiles that scrolls sideways
(a console's games, Onyx's games, Apps' category folders -- in a folder a Back tile first, then its apps --, the
settings pages). Up / Down moves between the two rows, Left / Right in a row, L1 / R1 change the category at once.
Three variants: V1 "light", V2 "dark", V3 "hero" (larger focused tile, the focused game's picture as a banner).
A design study only (docs/COMPACT-SHELL-STUDY.md section 19): nothing here is built.

    python3 tools/screenshot/mockup_console_switch.py   -> docs/compact-shell/mockups/console-switch-*.png

It reuses the helpers and the content of mockup_console_ps4.py (v2) and mockup_console_xmb.py (v3): the card's
apps, icons and categories, the consoles from the emulators' `games =`, the made-up ROMs and their pictures, the
white glyphs and the generic console shapes. No console maker's logo, game, button symbol or asset is drawn: the
consoles are generic shapes with their short names, the pad's buttons are A / B / X / Y / L1 / R1 / Home.
Resolution independent: logical units (lp) x a scale; a compact set below 560 logical lines.
"""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
import mockup_console_ps4 as v2
import mockup_console_xmb as v3
from mockup_console_ps4 import (font, mask, grad, put, rrect, ring, box, tw, text, text_l, text_c, text_r, ellipsize, wrap,
				dim, mix, lighten, shade, app_name, apps_in, icon_colour, rom_picture, game_picture, gameplay,
				slot_picture, scene, title_text, HELP, OUT, ROOT)
GL = v3.GL
WHITE = (255, 255, 255)

# ---- the themes ----------------------------------------------------------------------------------------------------
THEMES = {
	"light": dict (bg = ((242, 242, 244), (228, 229, 233)), fg = (44, 44, 50), sub = (112, 114, 124), faint = (168, 170, 178),
		       line = (204, 205, 212), accent = (0, 166, 214), circle = (255, 255, 255), panel = (250, 250, 252),
		       neutral = (252, 252, 253), shadow = 70, dark = False),
	"dark":  dict (bg = ((52, 52, 58), (38, 38, 43)), fg = (240, 240, 244), sub = (168, 170, 180), faint = (112, 114, 124),
		       line = (82, 83, 92), accent = (34, 204, 240), circle = (72, 72, 80), panel = (58, 58, 66),
		       neutral = (70, 70, 78), shadow = 150, dark = True),
	"hero":  dict (bg = ((12, 20, 44), (6, 10, 24)), fg = (236, 242, 250), sub = (150, 168, 196), faint = (96, 112, 142),
		       line = (44, 58, 92), accent = (96, 176, 255), circle = (26, 38, 70), panel = (20, 30, 58),
		       neutral = (30, 44, 78), shadow = 170, dark = True),
}

# ---- the words: English, French -------------------------------------------------------------------------------------
FR = {"Onyx games": "Jeux Onyx", "Apps": "Apps", "Settings": "Réglages", "%d games": "%d jeux", "%d apps": "%d apps",
      "Last played %s": "Dernière partie : %s", "%d save states": "%d sauvegardes", "1 save state": "1 sauvegarde",
      "no save state": "aucune sauvegarde", "Monday 21:04": "lundi 21:04", "Move": "Aller", "Category": "Catégorie",
      "Favourite": "Favori", "Options": "Options", "Back": "Retour", "Play": "Jouer", "Open": "Ouvrir", "Start": "Lancer",
      "All games": "Tous les jeux", "A to Z": "de A à Z", "5 Oct": "5 oct.", "2 Oct": "2 oct.", "27 Sep": "27 sept.",
      "20 Sep": "20 sept.", "18 Sep": "18 sept.", "Games": "Jeux", "Row": "Rangée", "yesterday": "hier", "Sunday": "dimanche"}
class Lang:
	def __init__ (s, code = "en"): s.code = code
	def __call__ (s, w): return FR.get (w, w) if s.code == "fr" else w

# ---- the consoles: v2's, plus the Nintendo DS (ndsemu's `games =`) ------------------------------------------------
SHORT = dict (v2.SHORT, **{"Nintendo DS": "NDS"})
SYSCOL = dict (v2.SYSCOL, NDS = (92, 110, 150))
ROMS = dict (v2.ROMS)
ROMS["NDS"] = [("Puzzle Tower", "city", "Saturday", "3 h 12 min", 1), ("Garden Pals", "forest", "30 Sep", "9 h 40 min", 2),
	       ("Sky Ink", "sky", "16 Sep", "1 h 25 min", 0)]
SYSTEMS = [(SHORT.get (n, n), n, x[0], e) for n, x, e in v2.SYSTEMS]	# (code, name, extension, emulator)
SYSNAME = {c: n for c, n, _, _ in SYSTEMS}
SYSEXT = {c: x for c, _, x, _ in SYSTEMS}
SYSEMU = {c: e for c, _, _, e in SYSTEMS}

def nds_icon (d, s, size, code):
	"""A generic two-screen clamshell with its short name."""
	u = s * size / 64; t = lambda v: v * u
	d.rounded_rectangle ([t (10), t (4), t (54), t (30)], t (5), fill = 255); d.rounded_rectangle ([t (17), t (8), t (47), t (26)], t (2), fill = 0)
	d.rounded_rectangle ([t (10), t (33), t (54), t (60)], t (5), fill = 255); d.rounded_rectangle ([t (21), t (37), t (43), t (55)], t (2), fill = 0)
	d.rectangle ([t (12), t (44), t (18), t (46)], fill = 0); d.rectangle ([t (14), t (42), t (16), t (48)], fill = 0)
	for cx, cy in [(49, 42), (49, 49)]: d.ellipse ([t (cx - 1.8), t (cy - 1.8), t (cx + 1.8), t (cy + 1.8)], fill = 0)
	fb = font (max (6, int (t (9))), True); w = fb.getlength (code); b = fb.getbbox (code)
	d.text ((t (32) - w / 2, t (17) - (b[1] + b[3]) / 2), code, font = fb, fill = 255)
def console_mask (size, code):
	size = int (size)
	return mask (size, size, lambda d, s: (nds_icon if code == "NDS" else v3.console_icon) (d, s, size, code))
def glyph_mask (size, name):
	size = int (size); return mask (size, size, lambda d, s: GL[name] (d, s, size / 16))

_pc = {}
def rom_pic (code, title, style, seed):
	"""A ROM's picture: v2's 160 x 144 title screens; a DS game: its two screens, the title above."""
	if code != "NDS": return rom_picture (code, title, style, seed)
	k = (code, title)
	if k not in _pc:
		im = Image.new ("RGB", (160, 144), (20, 20, 26))
		top = scene (style, seed, 160, 70); title_text (top, title, 22, (255, 255, 255), (20, 20, 50), size = 18)
		bot = scene (style, seed + 3, 160, 70); bot = Image.blend (bot, Image.new ("RGB", bot.size, (20, 30, 70)), 0.55)
		d = ImageDraw.Draw (bot); f = font (9, True); s = "TOUCH TO START"
		d.text (((160 - tw (s, f)) // 2, 30), s, font = f, fill = (255, 255, 255))
		im.paste (top, (0, 0)); im.paste (bot, (0, 74)); _pc[k] = im
	return _pc[k]
def rom_file (code, title):
	return "%s.%s" % (title.lower ().replace ("'", "").replace (" ", "-"), SYSEXT.get (code, "rom"))

# ---- pictures for the tiles -------------------------------------------------------------------------------------------
def crisp (im, w, h):
	"""Pixel art scaled up: an integer factor first (sharp pixels), then to the size."""
	f = max (1, math.ceil (max (w / im.width, h / im.height)))
	return im.resize ((im.width * f, im.height * f), Image.NEAREST).resize ((w, h), Image.LANCZOS)
def square (im):
	w, h = im.size; s = min (w, h); return im.crop (((w - s) // 2, (h - s) // 2, (w - s) // 2 + s, (h - s) // 2 + s))
def fit_square (im):
	"""A 10:9 title screen made square without cutting its title: its own blur above and below."""
	w, h = im.size; s = max (w, h)
	bg = Image.blend (im.resize ((s, s), Image.BILINEAR).filter (ImageFilter.GaussianBlur (6)), Image.new ("RGB", (s, s), (0, 0, 0)), 0.25)
	bg.paste (im, ((s - w) // 2, (s - h) // 2)); return bg
def g_left (d, s, k = 1):
	u = s * k; d.polygon ([(1 * u, 8 * u), (7.5 * u, 1.5 * u), (7.5 * u, 5.5 * u), (15 * u, 5.5 * u), (15 * u, 10.5 * u), (7.5 * u, 10.5 * u), (7.5 * u, 14.5 * u)], fill = 255)
GL = dict (GL, left = g_left)
_ic = {}
def icon (name, size):
	"""An app's 40 x 40 icon (magenta = see-through), scaled with sharp pixels."""
	k = (name, size)
	if k not in _ic:
		p = os.path.join (v2.SD, "apps", name + ".app", "icon.bmp")
		if not os.path.exists (p): p = os.path.join (v2.SD, "apps", "terminal.app", "icon.bmp")
		a = np.array (Image.open (p).convert ("RGB"))
		al = np.where ((a[..., 0] == 255) & (a[..., 1] == 0) & (a[..., 2] == 255), 0, 255).astype ("uint8")
		im = Image.fromarray (np.dstack ([a, al]), "RGBA")
		f = max (1, math.ceil (size / im.width))
		_ic[k] = im.resize ((im.width * f, im.height * f), Image.NEAREST).resize ((size, size), Image.LANCZOS)
	return _ic[k]

CATCOL = dict (v2.CATCOL, Demos = (222, 92, 124))
SETTINGS = [("folder", "Games", (232, 128, 48)), ("speaker", "Sound", (226, 76, 92)), ("pad", "Gamepad", (120, 96, 220)),
	    ("kbd", "Keyboard & Mouse", (90, 110, 140)), ("globe", "Language & Region", (40, 160, 150)), ("wifi", "Wi-Fi", (50, 130, 226)),
	    ("download", "Packages", (70, 170, 90)), ("modes", "Mode", (200, 100, 180)), ("monitor", "Display", (30, 150, 210))]
SETHELP = {"Games": "The folders the games are looked for in, on the card and the USB sticks",
	   "Sound": "The output, the volume, mute, a test sound", "Gamepad": "The pads, their buttons, the mapping wizard",
	   "Keyboard & Mouse": "The layout, the wheel's speed, the keyboard as pad 1", "Language & Region": "The system's language and time zone",
	   "Wi-Fi": "The networks around: join one, forget one, the country", "Packages": "Updates and new software from the repository",
	   "Mode": "Desktop, pocket or console: the shell Onyx starts in", "Display": "The resolution, the games' resolutions"}
APP_CATS = ["Productivity", "Internet", "Graphics", "Programming", "Demos", "Multimedia", "System"]

def tile_picture (scr, it, w, h):
	"""The tile's face, w x h, by its kind."""
	t = scr.t; k = it["kind"]
	if k == "rom": return crisp (fit_square (it["pic"]), w, h) if it["sys"] not in ("N64", "GC") else fit_square (it["pic"]).resize ((w, h), Image.LANCZOS)
	if k == "onyx" and it.get ("pic") is not None: return square (it["pic"]).resize ((w, h), Image.LANCZOS)
	if k in ("app", "onyx"):				# the icon on a plate of its colour, the name under it
		col = icon_colour (it["app"])
		im = grad (w, h, mix (col, WHITE, 0.72), mix (col, WHITE, 0.55)) if not t["dark"] else grad (w, h, mix (col, (40, 44, 60), 0.45), mix (col, (16, 18, 28), 0.7))
		isz = int (h * 0.44); ic = icon (it["app"], isz); im.paste (ic, ((w - isz) // 2, int (h * 0.17)), ic)
		f = font (h * 0.075, True); nm = ellipsize (it["title"], f, w - h * 0.12)
		text_c (im, 0, int (h * 0.70), w, int (h * 0.12), nm, f, (40, 40, 48) if not t["dark"] else (240, 242, 248))
		return im
	if k == "folder":					# a folder of the category's colour: its first apps on a card, its name
		col = CATCOL.get (it["cat"], (120, 130, 150))
		im = grad (w, h, lighten (col, 0.12), shade (col, 0.78)); d = ImageDraw.Draw (im)
		u = h / 100
		card = Image.new ("L", (w, h), 0); dc = ImageDraw.Draw (card)
		dc.rounded_rectangle ([8 * u, 9 * u, 40 * u, 20 * u], 3 * u, fill = 255)
		dc.rounded_rectangle ([8 * u, 15 * u, w - 8 * u, 64 * u], 5 * u, fill = 255)
		im.paste ((255, 255, 255) if not t["dark"] else (236, 238, 244), (0, 0), card.point (lambda v: v * 235 // 255))
		apps = it["apps"][:4]; isz = int (16 * u); gx = (w - 4 * isz - 3 * 5 * u) / 2
		for i, a in enumerate (apps):
			ic = icon (a, isz); im.paste (ic, (int (gx + i * (isz + 5 * u)), int (32 * u)), ic)
		f = font (9.5 * u, True); text (im, 8 * u, 70 * u, ellipsize (it["title"], f, w - 16 * u), f, WHITE)
		f2 = font (6.6 * u); text (im, 8 * u, 83 * u, it["count"], f2, (255, 255, 255))
		return im
	if k == "back":
		im = Image.new ("RGB", (w, h), t["neutral"]); u = h / 100
		put (im, (w - 30 * u) / 2, 22 * u, glyph_mask (30 * u, "left"), t["sub"])
		f = font (11 * u, True); text_c (im, 0, 60 * u, w, 14 * u, it["title"], f, t["fg"])
		f2 = font (7 * u); text_c (im, 0, 75 * u, w, 10 * u, it["sub"], f2, t["sub"])
		return im
	if k == "all":
		im = Image.new ("RGB", (w, h), t["neutral"]); u = h / 100
		put (im, (w - 28 * u) / 2, 22 * u, glyph_mask (28 * u, "grid"), t["accent"])
		f = font (10 * u, True); text_c (im, 0, 60 * u, w, 14 * u, it["title"], f, t["fg"])
		f2 = font (7 * u); text_c (im, 0, 75 * u, w, 10 * u, it["sub"], f2, t["sub"])
		return im
	if k == "setting":
		col = it["col"]; im = grad (w, h, lighten (col, 0.1), shade (col, 0.8)); u = h / 100
		put (im, (w - 36 * u) / 2, 18 * u, glyph_mask (36 * u, it["glyph"]), WHITE)
		f = font (9 * u, True); nm = it["title"]
		while tw (nm, f) > w - 12 * u and f.size > 8: f = font (f.size - 1, True)
		text_c (im, 0, 68 * u, w, 14 * u, nm, f, WHITE)
		return im
	return Image.new ("RGB", (w, h), (128, 128, 128))

# ---- the screen and its metrics -----------------------------------------------------------------------------------------
class Scr:
	def __init__ (s, W, H, k, theme = "light", lang = "en"):
		s.W, s.H, s.k = W, H, k; s.LH = H / k; s.c = c = s.LH < 560; s.t = THEMES[theme]; s.theme = theme
		s.hero = theme == "hero"; s.tr = Lang (lang); s.lang = lang
		s.m = dict (
			mx = 24 if c else 64,				# the side margin
			top_h = 42 if c else 70, av = 24 if c else 40, crumb = 14 if c else 21, clock = 15 if c else 23,
			lab_y = 48 if c else 92, lab = 15 if c else 24,	# the focused tile's name, above it
			row_y = 74 if c else 134,			# the content row's top
			T = 156 if c else 272, G = 10 if c else 16, r = 8 if c else 12,
			meta_y = 238 if c else 424, meta = 11 if c else 16,
			cat_y = 326 if c else 550,			# the category row's centre
			cd = 46 if c else 80, cs = 58 if c else 104, catlab = 12 if c else 17,
			bar_y = 432 if c else 652, hint = 12 if c else 16, hb = 18 if c else 26,
			rg = 3 if c else 5, rw = 3 if c else 4)		# the focus ring: gap, width
		if s.hero:
			s.m.update (r = 22, T = 180, Tf = 240, G = 18, row_bot = 550, cat_y = 610, chip = 54, ci = 30, bar_y = 662,
				    ht = 46, hmeta = 18, hy = 104)
	def L (s, v): return int (round (v * s.k))
	def M (s, key): return s.L (s.m[key])

def background (scr):
	t = scr.t; img = grad (scr.W, scr.H, *t["bg"])
	if scr.hero:					# a faint glow of light at the top right
		g = Image.new ("L", img.size, 0); ImageDraw.Draw (g).ellipse ([scr.W * 0.4, -scr.H * 0.7, scr.W * 1.4, scr.H * 0.5], fill = 40)
		img.paste ((90, 140, 230), (0, 0), g.filter (ImageFilter.GaussianBlur (scr.H / 5)))
	return img

def shadow (img, scr, x, y, w, h, r, strength = None, ellipse = False, dy = 6, blur = 9):
	L = scr.L; s = int (scr.t["shadow"] if strength is None else strength)
	m = Image.new ("L", img.size, 0); d = ImageDraw.Draw (m)
	box_ = [x, y + L (dy), x + w, y + h + L (dy)]
	d.ellipse (box_, fill = s) if ellipse else d.rounded_rectangle (box_, r, fill = s)
	img.paste ((0, 0, 0), (0, 0), m.filter (ImageFilter.GaussianBlur (L (blur))))

def focus_ring (img, scr, x, y, w, h, r, ellipse = False):
	"""The ring around the focus: a gap, an accent line, a soft halo (it pulses between this and a lighter tint)."""
	L, M = scr.L, scr.M; g, t = M ("rg"), M ("rw"); col = scr.t["accent"]
	x0, y0, w0, h0 = x - g - t, y - g - t, w + 2 * (g + t), h + 2 * (g + t); r0 = (h0 / 2) if ellipse else r + g + t
	halo = Image.new ("L", img.size, 0); ImageDraw.Draw (halo).rounded_rectangle ([x0, y0, x0 + w0, y0 + h0], r0, outline = 255, width = t * 3)
	img.paste (col, (0, 0), halo.filter (ImageFilter.GaussianBlur (L (6))).point (lambda v: v * 110 // 255))
	ring (img, x0, y0, w0, h0, r0 / 1 if not ellipse else h0 / 2, col, t = t)
	ring (img, x0 + t, y0 + t, w0 - 2 * t, h0 - 2 * t, max (1, r0 - t), lighten (col, 0.55), t = max (1, L (1)), alpha = 150)

def draw_tile (img, scr, it, x, y, w, h, focus = False, state = None):
	r = scr.L (scr.m["r"])
	shadow (img, scr, x, y, w, h, r)
	pic = tile_picture (scr, it, w, h)
	img.paste (pic, (int (x), int (y)), mask (w, h, lambda d, s: d.rounded_rectangle ([0, 0, w * s - 1, h * s - 1], r * s, fill = 255)))
	if not scr.t["dark"]: ring (img, x, y, w, h, r, (0, 0, 0), alpha = 22, t = 1)
	if it.get ("state"):						# running, suspended: a pill in the corner
		L = scr.L; fb = font (L (11 if not scr.c else 8), True); st = scr.tr (it["state"]); bh = L (20 if not scr.c else 14)
		bw = tw (st, fb) + L (24); bx = x + w - bw - L (8); by = y + L (8)
		rrect (img, bx, by, bw, bh, bh / 2, (16, 18, 26), alpha = 215); dd = L (7)
		rrect (img, bx + L (7), by + (bh - dd) // 2, dd, dd, dd / 2, (110, 220, 140) if it["state"] == "running" else (250, 200, 80))
		text_l (img, bx + L (17), by - L (1), bh, st, fb, (240, 242, 248))
	if focus: focus_ring (img, scr, x, y, w, h, r)

# ---- the top bar ------------------------------------------------------------------------------------------------------------
def g_signal (d, s, k = 1):
	u = s * k
	for i in range (4): d.rounded_rectangle ([(1 + i * 4) * u, (13 - i * 3.4) * u, (3.6 + i * 4) * u, 15 * u], 0.8 * u, fill = 255)
def top_bar (img, scr, crumb, clock = "21:07"):
	L, M = scr.L, scr.M; t = scr.t; h = M ("top_h"); mx = M ("mx"); av = M ("av"); cy = h // 2 + L (4)
	# the left: Onyx's round badge (the profile's place), then where we are
	rrect (img, mx, cy - av // 2, av, av, av / 2, t["accent"])
	gs = int (av * 0.56); put (img, mx + (av - gs) / 2, cy - gs / 2 + L (1), glyph_mask (gs, "gem"), WHITE)
	x = mx + av + L (14); fb = font (M ("crumb"), True); fs = font (M ("crumb"))
	for i, part in enumerate (crumb):
		f = fb if i == len (crumb) - 1 else fs; col = t["fg"] if i == len (crumb) - 1 else t["sub"]
		text_l (img, x, cy - L (14), L (28), part, f, col); x += tw (part, f)
		if i < len (crumb) - 1:
			ch = L (12 if not scr.c else 9); put (img, x + L (8), cy - ch / 2, mask (ch, ch, lambda d, s: v2.g_chev (d, s, ch / 16)), t["sub"]); x += ch + L (16)
	# the right: the time, the pad and its charge, the Wi-Fi
	fc = font (M ("clock")); xr = scr.W - mx
	text_r (img, xr, cy - L (14), L (28), clock, fc, t["fg"]); xr -= tw (clock, fc) + L (18)
	v2.battery (img, xr - L (24), cy - L (7), scr.k * (0.8 if scr.c else 1), t["fg"]); xr -= L (32)
	ps = L (22 if not scr.c else 16); put (img, xr - ps, cy - ps / 2, glyph_mask (ps, "pad"), t["fg"]); xr -= ps + L (16)
	ws = L (20 if not scr.c else 15); put (img, xr - ws, cy - ws / 2, glyph_mask (ws, "wifi"), t["fg"])

# ---- the hints bar (the bottom) -------------------------------------------------------------------------------------------
def hints (img, scr, items, left = None):
	L, M = scr.L, scr.M; t = scr.t; y0 = M ("bar_y"); mx = M ("mx"); bh = scr.H - y0; d = M ("hb")
	box (img, mx, y0, scr.W - 2 * mx, max (1, L (1)), t["line"])
	f = font (M ("hint")); fb = font (M ("hint") - L (2), True); yc = y0 + (bh - d) // 2
	if left:
		ps = L (22 if not scr.c else 16); put (img, mx + L (4), y0 + (bh - ps) / 2, glyph_mask (ps, "pad"), t["sub"])
		text_l (img, mx + ps + L (14), y0, bh, left, f, t["sub"])
	def kw (key):
		if key in ("A", "B", "X", "Y", "lr", "ud", "dpad"): return d
		return sum (tw (p, fb) + L (14) + L (4) for p in key.split ("/")) - L (4)
	x = scr.W - mx - sum (kw (k) + L (8) + tw (scr.tr (w), f) + L (26) for k, w in items) + L (26)
	for key, word in items:
		if key in ("A", "B", "X", "Y"):
			rrect (img, x, yc, d, d, d / 2, t["fg"]); text_c (img, x, yc - L (1), d, d, key, fb, t["bg"][0]); x += d
		elif key in ("lr", "ud", "dpad"):
			u = d / 16
			def fn (dd, s, key = key):
				for (ax, ay, aw, ah), arm in [((5.5, 0.5, 5, 6), "u"), ((5.5, 9.5, 5, 6), "d"), ((0.5, 5.5, 6, 5), "l"), ((9.5, 5.5, 6, 5), "r")]:
					lit = key == "dpad" or (arm in "lr") == (key == "lr")
					dd.rounded_rectangle ([ax * u * s, ay * u * s, (ax + aw) * u * s, (ay + ah) * u * s], u * s, fill = 255 if lit else 90)
				dd.rectangle ([5.5 * u * s, 5.5 * u * s, 10.5 * u * s, 10.5 * u * s], fill = 150)
			put (img, x, yc, mask (d, d, fn), t["fg"]); x += d
		else:
			for p in key.split ("/"):
				pw = tw (p, fb) + L (14)
				rrect (img, x, yc + L (1), pw, d - L (2), (d - L (2)) / 2, t["fg"]); text_c (img, x, yc, pw, d, p, fb, t["bg"][0]); x += pw + L (4)
			x -= L (4)
		x += L (8); text_l (img, x, y0, bh, scr.tr (word), f, t["fg"]); x += tw (scr.tr (word), f) + L (26)

# ---- the content ----------------------------------------------------------------------------------------------------------
def categories ():
	c = [(code, name, ("console", code), SYSCOL[code]) for code, name, _, _ in SYSTEMS if ROMS.get (code)]
	return c + [("onyx", "Onyx games", "pad", (232, 120, 44)), ("apps", "Apps", "grid", (50, 130, 226)), ("settings", "Settings", "gear", (124, 128, 142))]

def rom_items (code, all_tile = True):
	out = []
	for i, (ti, style, last, played, slots) in enumerate (ROMS[code]):		# (v2's lists: the last played first)
		out.append (dict (kind = "rom", sys = code, title = ti, pic = rom_pic (code, ti, style, i * 7 + len (ti)), last = last,
				  played = played, slots = slots, style = style, seed = i * 7 + len (ti)))
	if all_tile: out.append (dict (kind = "all", title = "All games", sub = "%d games  ·  A to Z" % len (out), n = len (out)))
	return out
def onyx_items ():
	return [dict (kind = "onyx", app = x["app"], title = x["title"], pic = x.get ("pic"), sub = x.get ("sub", "")) for x in v2.onyx_items ()]
def folder_items ():
	out = []
	for c in APP_CATS:
		apps = apps_in (c)
		if apps: out.append (dict (kind = "folder", cat = c, title = c, apps = apps, count = "%d apps" % len (apps)))
	return out
def folder_content (cat):
	out = [dict (kind = "back", title = "Back", sub = "to Apps")]
	for a in apps_in (cat):
		out.append (dict (kind = "app", app = a, title = app_name (a), sub = cat + ("  -  " + v2.KIND[a] if a in v2.KIND else ""),
				  state = "running" if a in ("letters", "media", "telegram") else None))
	return out
def setting_items ():
	return [dict (kind = "setting", glyph = g, title = n, col = c, sub = SETHELP[n]) for g, n, c in SETTINGS]

def meta_of (scr, it):
	tr = scr.tr; k = it["kind"]
	if k == "rom":
		sl = tr ("no save state") if not it["slots"] else tr ("1 save state") if it["slots"] == 1 else tr ("%d save states") % it["slots"]
		return "%s   ·   %s   ·   %s   ·   %s" % (rom_file (it["sys"], it["title"]), tr ("Last played %s") % tr (it["last"]), it["played"], sl)
	if k == "all": return "Every game of the console in a grid, A to Z; Y searches"
	if k == "folder": return "%s:  %s ..." % (tr (it["count"]) if "%" not in it["count"] else it["count"], ", ".join (app_name (a) for a in it["apps"][:5]))
	if k == "back": return "Back to the Apps' folders (B does the same)"
	if k in ("app", "onyx"): return it.get ("sub", "") + ("   ·   running: Letters-tour.rtf" if it.get ("state") == "running" else "")
	if k == "setting": return it["sub"]
	if k == "cat": return it["sub"]
	return ""

# ---- the rows -------------------------------------------------------------------------------------------------------------
def content_row (img, scr, items, sel, focused, heading = None, first = None):
	"""One row of square tiles from the left margin; the row scrolls so that the chosen tile stays in sight with
	one tile after it. Focused: the ring and the name above the tile; not focused: the category's name as the row's
	heading, the chosen tile without a ring."""
	L, M = scr.L, scr.M; t = scr.t; mx = M ("mx"); T = M ("T"); G = M ("G"); y = M ("row_y")
	vis = max (1, int ((scr.W - mx) // (T + G)))
	if first is None: first = max (0, sel - (vis - 2)) if sel >= vis - 1 else 0
	x0 = mx - first * (T + G) + (int (T * 0.42) if first > 0 else 0)
	for i, it in enumerate (items):
		x = x0 + i * (T + G)
		if x > scr.W or x + T < 0: continue
		draw_tile (img, scr, it, x, y, T, T, focus = focused and i == sel)
	sx = x0 + sel * (T + G); ly = M ("lab_y")
	if focused:
		f = font (M ("lab"), True); name = scr.tr (items[sel]["title"]) if items[sel]["kind"] in ("back", "all") else items[sel]["title"]
		lx = min (sx, scr.W - mx - tw (name, f)); lx = max (mx, lx)
		text_l (img, lx, ly, L (34 if not scr.c else 22), ellipsize (name, f, scr.W - mx - lx), f, t["accent"])
	elif heading:
		f = font (M ("lab"), True); text_l (img, mx, ly, L (34 if not scr.c else 22), heading[0], f, t["fg"])
		if heading[1]: text_l (img, mx + tw (heading[0], f) + L (14), ly + L (2), L (34 if not scr.c else 22), heading[1], font (M ("meta")), t["sub"])
	# under the row: the chosen tile's line and the place in the row
	fm = font (M ("meta")); cnt = "%d / %d" % (sel + 1, len ([x for x in items if x["kind"] not in ("all", "back")])) if items[sel]["kind"] not in ("all", "back") else ""
	if focused:
		text_r (img, scr.W - mx, M ("meta_y"), L (22), cnt, fm, t["sub"])
		text_l (img, mx, M ("meta_y"), L (22), ellipsize (meta_of (scr, items[sel]), fm, scr.W - 2 * mx - tw (cnt, fm) - L (30)), fm, t["sub"])
	# the row goes on: a soft fade at the right edge
	return sx

def cat_row (img, scr, cats, ci, focused):
	"""The categories: round tiles across the bottom, centred (scrolling when they do not fit). The shown one
	carries a dot and its name; focused, the ring, its name in the accent colour."""
	L, M = scr.L, scr.M; t = scr.t; cd = M ("cd"); cs = M ("cs"); n = len (cats); cy = M ("cat_y"); mx = M ("mx")
	total = (n - 1) * cs + cd; avail = scr.W - 2 * mx
	if total <= avail: x0 = (scr.W - total) // 2 + cd // 2
	else:
		off = min (max (0, ci * cs - (avail - cd) // 2), total - avail); x0 = mx + cd // 2 - off
	base = img.copy () if total > avail else None
	for i, (key, name, ic, col) in enumerate (cats):
		cx = x0 + i * cs
		if cx < -cd or cx > scr.W + cd: continue
		x, y = cx - cd // 2, cy - cd // 2
		shadow (img, scr, x, y, cd, cd, cd / 2, ellipse = True, dy = 4, blur = 6, strength = scr.t["shadow"] * 0.8)
		rrect (img, x, y, cd, cd, cd / 2, t["circle"])
		gc = col if not t["dark"] else lighten (col, 0.3)
		if isinstance (ic, tuple): gs = int (cd * 0.62); m = console_mask (gs, ic[1])
		else: gs = int (cd * 0.48); m = glyph_mask (gs, ic)
		put (img, cx - gs / 2, cy - gs / 2, m, gc)
		if i == ci:
			fl = font (M ("catlab"), True); nm = scr.tr (name)
			if focused: focus_ring (img, scr, x, y, cd, cd, cd / 2, ellipse = True)
			else:
				dd = L (7 if not scr.c else 5); rrect (img, cx - dd / 2, cy + cd // 2 + L (8), dd, dd, dd / 2, t["accent"])
			text_c (img, cx - L (150), cy + cd // 2 + L (16 if not scr.c else 12), L (300), L (24 if not scr.c else 16), nm, fl, t["accent"] if focused else t["fg"])
	if total > avail:					# more categories beyond the edges: cut at the margins, small chevrons
		band = (cy - cd, cy + cd)
		img.paste (base.crop ((0, band[0], mx - L (2), band[1])), (0, band[0]))
		img.paste (base.crop ((scr.W - mx + L (2), band[0], scr.W, band[1])), (scr.W - mx + L (2), band[0]))
		ch = L (14 if not scr.c else 10)
		if x0 - cd // 2 < mx: put (img, (mx - ch) // 2, cy - ch / 2, mask (ch, ch, lambda d, s: v2.g_chev (d, s, ch / 16, "left")), t["sub"])
		if x0 + (n - 1) * cs + cd // 2 > scr.W - mx: put (img, scr.W - (mx + ch) // 2, cy - ch / 2, mask (ch, ch, lambda d, s: v2.g_chev (d, s, ch / 16)), t["sub"])

# ---- V3: the hero layout ------------------------------------------------------------------------------------------------------
def hero_banner (img, scr, it):
	"""The chosen item's picture as a wide banner behind the top half, blurred and faded into the background."""
	W, H = scr.W, scr.H; bh = int (H * 0.72)
	k = it["kind"]
	if k == "rom": src = scene (it["style"], it["seed"], 320, 320)
	elif k == "onyx" and it.get ("pic") is not None: src = square (it["pic"])
	else:						# no picture: a wash of the item's colour
		col = it.get ("col") or CATCOL.get (it.get ("cat"), None) or (icon_colour (it["app"]) if it.get ("app") else (60, 90, 150))
		src = grad (320, 320, lighten (col, 0.1), shade (col, 0.35), horizontal = True)
	pic = src.resize ((W // 8, W // 8), Image.BILINEAR)
	pic = pic.crop ((0, (pic.height - bh // 8) // 2, pic.width, (pic.height - bh // 8) // 2 + bh // 8)).filter (ImageFilter.GaussianBlur (2.5))
	ban = pic.resize ((W, bh), Image.BICUBIC); ban = Image.blend (ban, Image.new ("RGB", ban.size, scr.t["bg"][0]), 0.35)
	a = np.zeros ((bh, W)); yy = np.linspace (0, 1, bh)[:, None]; xx = np.linspace (0, 1, W)[None, :]
	a = np.clip (1.15 - yy * 1.25, 0, 1) * np.clip (0.35 + xx * 1.1, 0, 1) * 0.85
	img.paste (ban, (0, 0), Image.fromarray ((a * 255).astype ("uint8"), "L"))
	# legibility: a dark veil at the left where the words are
	v = Image.fromarray ((np.clip (0.75 - xx * 1.3, 0, 1) * np.ones ((bh, 1)) * 150).astype ("uint8"), "L")
	img.paste (scr.t["bg"][1], (0, 0), v)

def pill (img, scr, x, y, label, key = None, primary = False, h = None):
	L = scr.L; t = scr.t; h = h or L (44); f = font (L (17), True); kd = int (h * 0.6)
	w = tw (scr.tr (label), f) + L (36) + (kd + L (10) if key else 0)
	if primary: rrect (img, x, y, w, h, h / 2, t["accent"])
	else: rrect (img, x, y, w, h, h / 2, WHITE, alpha = 30); ring (img, x, y, w, h, h / 2, WHITE, alpha = 90, t = L (1.2))
	xx = x + L (18)
	if key:
		rrect (img, xx, y + (h - kd) // 2, kd, kd, kd / 2, WHITE if primary else t["fg"])
		text_c (img, xx, y + (h - kd) // 2 - L (1), kd, kd, key, font (L (13), True), t["accent"] if primary else t["bg"][0]); xx += kd + L (10)
	text_l (img, xx, y, h, scr.tr (label), f, WHITE); return w

def hero_text (img, scr, it, cat_name):
	L, M = scr.L, scr.M; t = scr.t; mx = M ("mx"); y = M ("hy")
	fs = font (L (16), True); text (img, mx, y, cat_name.upper (), fs, t["accent"])
	f = font (M ("ht"), True); nm = scr.tr (it["title"]) if it["kind"] in ("back", "all") else it["title"]
	text (img, mx - L (2), y + L (26), ellipsize (nm, f, scr.W * 0.6), f, t["fg"])
	fm = font (M ("hmeta")); lines = wrap (meta_of (scr, it).replace ("   ·   ", "  ·  "), fm, scr.W * 0.55)[:2]
	for j, ln in enumerate (lines): text (img, mx, y + L (90) + j * L (26), ln, fm, t["sub"])
	by = y + L (90) + len (lines) * L (26) + L (20)
	k = it["kind"]
	acts = [("A", "Play", True), ("X", "Options", False), ("Y", "Favourite", False)] if k == "rom" else \
	       [("A", "Open", True), ("L1/R1", None, False)][:1] if k == "cat" else \
	       [("A", "Open", True)] if k in ("folder", "setting", "all") else [("A", "Start", True), ("X", "Options", False)]
	x = mx
	for key, lab, prim in acts: x += pill (img, scr, x, by, lab, key, prim) + L (14)

def hero_row (img, scr, items, sel, focused):
	"""The tiles bottom-aligned on one line, the chosen one larger."""
	L, M = scr.L, scr.M; mx = M ("mx"); T = M ("T"); Tf = M ("Tf"); G = M ("G"); yb = M ("row_bot")
	vis = max (1, int ((scr.W - mx - Tf) // (T + G)) + 1)
	first = max (0, sel - (vis - 2)) if sel >= vis - 1 else 0
	x = mx - (int (T * 0.4) if first else 0)
	for i, it in enumerate (items[first - (1 if first else 0):], start = first - (1 if first else 0)):
		s = Tf if i == sel else T
		if x > scr.W: break
		draw_tile (img, scr, it, x, yb - s, s, s, focus = focused and i == sel)
		if i == sel and not focused:
			ring (img, x - L (4), yb - s - L (4), s + L (8), s + L (8), L (26), scr.t["sub"], t = L (2), alpha = 120)
		x += s + G
	fm = font (L (15)); cnt = "%d / %d" % (sel + 1, len ([x for x in items if x["kind"] not in ("all", "back")])) if items[sel]["kind"] not in ("all", "back") else ""
	text_r (img, scr.W - mx, yb - Tf - L (30), L (22), cnt, fm, scr.t["sub"])

def chip_row (img, scr, cats, ci, focused):
	"""V3's categories: rounded chips, the shown one widened with its name."""
	L, M = scr.L, scr.M; t = scr.t; h = M ("chip"); cy = M ("cat_y"); isz = M ("ci"); g = L (12); f = font (L (17), True)
	ws = [h + (tw (scr.tr (c[1]), f) + L (20) if i == ci else 0) for i, c in enumerate (cats)]
	total = sum (ws) + g * (len (cats) - 1); x = (scr.W - total) // 2
	for i, (key, name, ic, col) in enumerate (cats):
		w = ws[i]; y = cy - h // 2
		on = i == ci
		rrect (img, x, y, w, h, h / 2, t["accent"] if (on and focused) else lighten (t["circle"], 0.12) if on else t["circle"])
		gc = WHITE if (on and focused) else lighten (col, 0.35)
		m = console_mask (int (isz * 1.25), ic[1]) if isinstance (ic, tuple) else glyph_mask (isz, ic)
		put (img, x + (h - m.width) / 2, cy - m.height / 2, m, gc)
		if on: text_l (img, x + h - L (4), y, h, scr.tr (name), f, WHITE if focused else t["fg"])
		if on and focused: focus_ring (img, scr, x, y, w, h, h / 2)
		x += w + g

# ---- the screens -------------------------------------------------------------------------------------------------------------
def content (scr, cat, folder = None):
	"""(items, crumb, heading) of a category."""
	tr = scr.tr
	if cat in ROMS:
		it = rom_items (cat); n = len (it) - 1
		return it, [SYSNAME[cat]], (SYSNAME[cat], tr ("%d games") % n)
	if cat == "onyx":
		it = onyx_items (); return it, [tr ("Onyx games")], (tr ("Onyx games"), tr ("%d games") % len (it))
	if cat == "apps":
		if folder: it = folder_content (folder); return it, [tr ("Apps"), folder], (folder, tr ("%d apps") % (len (it) - 1))
		it = folder_items (); return it, [tr ("Apps")], (tr ("Apps"), "%d folders" % len (it))
	it = setting_items (); return it, [tr ("Settings")], (tr ("Settings"), "%d pages" % len (it))

def hint_set (scr, cat, items, sel, focus):
	if focus == "cats": return [("lr", "Category"), ("ud", "Row"), ("A", "Open")] if not scr.c else [("lr", "Category"), ("A", "Open")]
	k = items[sel]["kind"]
	if scr.c: return [("dpad", "Move"), ("B", "Back"), ("A", "Play" if k == "rom" else "Open")]
	if k == "rom": return [("dpad", "Move"), ("L1/R1", "Category"), ("Y", "Favourite"), ("X", "Options"), ("B", "Back"), ("A", "Play")]
	if k in ("app", "onyx"): return [("dpad", "Move"), ("L1/R1", "Category"), ("X", "Options"), ("B", "Back"), ("A", "Start")]
	return [("dpad", "Move"), ("L1/R1", "Category"), ("B", "Back"), ("A", "Open")]

def home (W, H, k, theme = "light", cat = "GBA", sel = 0, focus = "row", folder = None, lang = "en", left = None):
	scr = Scr (W, H, k, theme, lang); img = background (scr)
	cats = categories (); ci = [c[0] for c in cats].index (cat)
	items, crumb, heading = content (scr, cat, folder)
	if scr.hero:
		hero_banner (img, scr, items[sel])
		top_bar (img, scr, crumb)
		if focus == "cats":			# the category itself in the banner while the focus is on it
			first = items[0]; n = len ([x for x in items if x["kind"] not in ("all", "back")])
			sub = (scr.tr ("%d games") % n + "  ·  " + "the last played: %s (%s)" % (first["title"], scr.tr (first["last"]))) if first["kind"] == "rom" else heading[1]
			hero_text (img, scr, dict (kind = "cat", title = crumb[-1], sub = sub), "Category")
		else: hero_text (img, scr, items[sel], crumb[-1])
		hero_row (img, scr, items, sel, focus == "row")
		chip_row (img, scr, cats, ci, focus == "cats")
	else:
		top_bar (img, scr, crumb)
		content_row (img, scr, items, sel, focus == "row", heading)
		cat_row (img, scr, cats, ci, focus == "cats")
	hints (img, scr, hint_set (scr, cat, items, sel, focus), left = left if left is not None else (None if scr.c else "Pad 1"))
	return img

# ---- the side panel: a game's options (X) and the menu over a running game (Home) ---------------------------------------
def panel_rows (img, scr, x, y, w, rows, sel, rh = 56):
	"""rows: (glyph, label, value, extra height, drawer). The focused row: the ring."""
	L = scr.L; t = scr.t; rh = L (rh); f = font (L (19)); fb = font (L (19), True); fv = font (L (16))
	for i, (g, lab, val, ex, draw) in enumerate (rows):
		h = rh + ex
		if i and sel not in (i, i - 1): box (img, x + L (16), y, w - L (32), max (1, L (1)), t["line"])
		gs = L (22); put (img, x + L (20), y + (rh - gs) / 2, glyph_mask (gs, g), t["accent"] if i == sel else t["sub"])
		text_l (img, x + L (58), y, rh, scr.tr (lab), fb if i == sel else f, t["fg"])
		if val: text_r (img, x + w - L (20), y, rh, val, fv, t["accent"] if i == sel else t["sub"])
		if draw: draw (img, x, y + rh - L (6), w)
		if i == sel: focus_ring (img, scr, x + L (6), y + L (4), w - L (12), h - L (8), L (10))
		y += h
	return y

def side_panel (img, scr, pic, title, sub, rows, sel, foot = None, wide = 520, rh = 56):
	L = scr.L; t = scr.t; w = L (wide); x = scr.W - w - L (28); y = L (28); h = scr.H - L (56) - L (74)
	shadow (img, scr, x, y, w, h, L (22), strength = 160, dy = 8, blur = 18)
	rrect (img, x, y, w, h, L (22), t["panel"])
	ps = L (96); img.paste (pic.resize ((ps, ps), Image.LANCZOS), (x + L (24), y + L (24)),
				mask (ps, ps, lambda d, s: d.rounded_rectangle ([0, 0, ps * s - 1, ps * s - 1], L (12) * s, fill = 255)))
	text (img, x + L (140), y + L (34), ellipsize (title, font (L (26), True), w - L (160)), font (L (26), True), t["fg"])
	for j, ln in enumerate (sub): text (img, x + L (140), y + L (72) + j * L (22), ln, font (L (15)), t["sub"])
	yy = panel_rows (img, scr, x + L (12), y + L (140), w - L (24), rows, sel, rh)
	if foot: foot (img, x, y + h, w)
	return x

def slot_strip (scr, it, n_used, focus = None):
	def draw (img, x, y, w):
		L = scr.L; t = scr.t; sw = (w - L (58) - L (24) - 2 * L (10)) // 3; sh = int (sw * 144 / 160); xx = x + L (58)
		for i in range (3):
			if i < n_used:
				img.paste (slot_picture (it, i, sw, sh), (xx, y), mask (sw, sh, lambda d, s: d.rounded_rectangle ([0, 0, sw * s - 1, sh * s - 1], L (8) * s, fill = 255)))
				text_c (img, xx, y + sh + L (2), sw, L (20), ["Mon 21:04", "Sun 10:12"][i], font (L (13)), t["sub"])
			else:
				rrect (img, xx, y, sw, sh, L (8), t["neutral"]); ring (img, xx, y, sw, sh, L (8), t["line"], t = L (1.5))
				text_c (img, xx, y, sw, sh, "+", font (L (28)), t["faint"])
				text_c (img, xx, y + sh + L (2), sw, L (20), "empty", font (L (13)), t["faint"])
			xx += sw + L (10)
	return draw

def options_sheet (W, H, k, theme = "light"):
	"""X on a game: its options in a panel at the right, over the home (dimmed)."""
	img = home (W, H, k, theme, "GBA", 0, "row")
	scr = Scr (W, H, k, theme); L = scr.L
	img = dim (img, 90 if theme == "light" else 120, (20, 22, 30))
	it = rom_items ("GBA")[0]
	rows = [("play", "Play", "gbaemu", 0, None), ("save", "Save states", "2 of 3", L (148), slot_strip (scr, it, 2)),
		("star", "Add to Favourites", None, 0, None), ("info", "Information", "6 h 41 min", 0, None),
		("close", "Delete", None, 0, None)]
	side_panel (img, scr, fit_square (it["pic"]), "Star Courier", ["Game Boy Advance  ·  star-courier.gba", "Last played Monday 21:04"], rows, 1)
	hints_over (img, scr, [("ud", "Move"), ("lr", "Slot"), ("Y", "Delete slot"), ("B", "Close"), ("A", "Load")])
	return img

def hints_over (img, scr, items):
	"""The hints bar redrawn over a dimmed screen (its band cleared first)."""
	y0 = scr.M ("bar_y"); band = background (scr).crop ((0, y0, scr.W, scr.H)); img.paste (band, (0, y0)); hints (img, scr, items, left = "Pad 1")

def game_menu (W, H, k, theme = "light"):
	"""Home in a game: the game paused and dimmed, the menu as a panel at the right, quick settings at its foot."""
	scr = Scr (W, H, k, theme); L = scr.L; t = scr.t
	img = gameplay (W, H).filter (ImageFilter.GaussianBlur (L (1.5)))
	img = dim (img, 120 if theme == "light" else 150, (16, 18, 26))
	it = rom_items ("GBA")[0]
	def slotval (img_, x, y, w): pass
	rows = [("play", "Resume", None, 0, None), ("save", "Save state", "<  Slot 2  >", 0, None), ("load", "Load state", "Slot 2", 0, None),
		("camera", "Screenshot", None, 0, None), ("pad", "Controls", None, 0, None), ("home", "Home", "the game waits", 0, None),
		("close", "Close game", None, 0, None)]
	def foot (img_, x, yb, w):
		yy = yb - L (82); box (img_, x + L (28), yy - L (14), w - L (56), max (1, L (1)), t["line"])
		gs = L (22); put (img_, x + L (32), yy + L (4), glyph_mask (gs, "speaker"), t["sub"])
		bx = x + L (70); bw = w - L (70) - L (120); seg = 10; sg = L (4); swd = (bw - (seg - 1) * sg) / seg
		for i in range (seg): rrect (img_, bx + i * (swd + sg), yy + L (9), swd, L (12), L (3), t["accent"] if i < 7 else t["line"])
		text_r (img_, x + w - L (28), yy, L (30), "7", font (L (17), True), t["fg"])
		put (img_, x + L (32), yy + L (40), glyph_mask (gs, "wifi"), t["sub"]); text_l (img_, x + L (70), yy + L (36), L (30), "Maison-5G", font (L (16)), t["sub"])
		text_r (img_, x + w - L (28), yy + L (36), L (30), "21:07", font (L (16), True), t["fg"])
	side_panel (img, scr, fit_square (it["pic"]), "Star Courier", ["gbaemu  ·  paused, 6 h 41 min", "Overwrites slot 2 (Sun 10:12)"], rows, 1, foot = foot, wide = 500, rh = 50)
	hints_over (img, scr, [("ud", "Move"), ("lr", "Slot"), ("B", "Resume"), ("A", "OK")])
	return img

# ---- a settings page, Switch-like -----------------------------------------------------------------------------------------------
def settings_page (W, H, k, theme = "light", page = "Sound", sel = 1):
	"""The pages' list at the left (a bar on the chosen one), its rows at the right (a line between rows, the
	focused row in the ring); xset.h's pages, rows and values as they are, only drawn in the tiles' theme."""
	scr = Scr (W, H, k, theme); L, M = scr.L, scr.M; t = scr.t; img = background (scr)
	top_bar (img, scr, [scr.tr ("Settings"), page])
	lx = M ("mx"); lw = L (330); y = L (96); rh = L (50); f = font (L (18)); fb = font (L (18), True)
	box (img, lx + lw + L (24), L (90), max (1, L (1)), M ("bar_y") - L (110), t["line"])
	for g, n, c in SETTINGS:
		on = n == page
		if on:
			rrect (img, lx, y + L (3), lw, rh - L (6), L (10), t["panel"] if not t["dark"] else lighten (t["panel"], 0.06))
			box (img, lx + L (6), y + L (13), L (4), rh - L (26), t["accent"])
		cs = L (30); rrect (img, lx + L (20), y + (rh - cs) / 2, cs, cs, L (8), c)
		gs = L (18); put (img, lx + L (20) + (cs - gs) / 2, y + (rh - gs) / 2, glyph_mask (gs, g), WHITE)
		text_l (img, lx + L (64), y, rh, n, fb if on else f, t["accent"] if on else t["fg"]); y += rh
	# the page
	px = lx + lw + L (56); pw = scr.W - px - M ("mx")
	text (img, px, L (100), page, font (L (30), True), t["fg"])
	text (img, px, L (142), SETHELP[page], font (L (16)), t["sub"])
	rows = [("Play on", "Headphone jack", None), ("Volume", None, "slider"), ("Mute", None, "toggle"), ("Play a test sound", None, "go")]
	y = L (190); rh = L (70)
	for i, (lab, val, kind) in enumerate (rows):
		box (img, px, y, pw, max (1, L (1)), t["line"])
		on = i == sel; h = rh + (L (22) if on else 0)
		text_l (img, px + L (20), y + (L (-8) if on else 0), rh, lab, font (L (21), on), t["fg"])
		if on: text (img, px + L (20), y + L (46), "The whole system's volume (kept in SD:/etc/sound.ini)", font (L (15)), t["sub"])
		xr = px + pw - L (20)
		if val: text_r (img, xr, y, rh, val, font (L (19)), t["accent"])
		if kind == "slider":
			seg = 10; sg = L (5); swd = L (22); bx = xr - L (60) - seg * swd - (seg - 1) * sg
			text_l (img, bx - L (28), y - L (8), rh, "<", font (L (20), True), t["sub"])
			for j in range (seg): rrect (img, bx + j * (swd + sg), y + rh // 2 - L (15), swd, L (14), L (4), t["accent"] if j < 7 else t["line"])
			text_r (img, xr, y - L (8), rh, "7  >", font (L (19), True), t["fg"])
		if kind == "toggle":
			tw_, th = L (52), L (28); rrect (img, xr - tw_, y + (rh - th) // 2, tw_, th, th / 2, t["line"])
			rrect (img, xr - tw_ + L (3), y + (rh - th) // 2 + L (3), th - L (6), th - L (6), (th - L (6)) / 2, WHITE)
		if kind == "go": put (img, xr - L (14), y + (rh - L (14)) / 2, mask (L (14), L (14), lambda d, s: v2.g_chev (d, s, L (14) / 16)), t["sub"])
		if on: focus_ring (img, scr, px, y + L (6), pw, h - L (10), L (12))
		y += h
	box (img, px, y, pw, max (1, L (1)), t["line"])
	hints (img, scr, [("ud", "Move"), ("lr", "Change"), ("L1/R1", "Page"), ("B", "Back"), ("A", "OK")], left = "Pad 1")
	return img

# ---- the sheets ---------------------------------------------------------------------------------------------------------------
def sheet (shots, title, sub, cols = 3, bg = ((236, 237, 240), (222, 224, 230)), dark = False):
	tw_, th = 600, 338; gx = 56; rows = (len (shots) + cols - 1) // cols
	W = cols * tw_ + (cols - 1) * gx + 120; H = 150 + rows * (th + 130) + 20
	img = grad (W, H, *bg); fg = (30, 32, 40) if not dark else (236, 240, 248); sb = (96, 100, 112) if not dark else (160, 170, 190)
	put (img, 60, 40, glyph_mask (26, "gem"), (0, 150, 200))
	text (img, 100, 34, title, font (28, True), fg); text (img, 100, 76, sub, font (17), sb)
	for i, (im, cap, line) in enumerate (shots):
		r, c = divmod (i, cols); x = 60 + c * (tw_ + gx); y = 140 + r * (th + 130)
		shadow_img = Image.new ("L", img.size, 0); ImageDraw.Draw (shadow_img).rounded_rectangle ([x, y + 6, x + tw_, y + th + 6], 10, fill = 90)
		img.paste ((0, 0, 0), (0, 0), shadow_img.filter (ImageFilter.GaussianBlur (8)))
		img.paste (im.resize ((tw_, th), Image.LANCZOS), (x, y))
		text (img, x, y + th + 14, cap, font (19, True), fg)
		for j, ln in enumerate (wrap (line, font (15), tw_)[:3]): text (img, x, y + th + 44 + j * 21, ln, font (15), sb)
	return img

def save (img, name):
	p = os.path.join (OUT, name); img.save (p); print ("wrote", os.path.relpath (p, ROOT), img.size)

def main ():
	os.makedirs (OUT, exist_ok = True)
	W, H, K = 1920, 1080, 1.5
	# V1, light: every key state
	a = home (W, H, K, "light", "GBA", 0, "row");				save (a, "console-switch-v1-home.png")
	b = home (W, H, K, "light", "SNES", 1, "cats");				save (b, "console-switch-v1-categories.png")
	c = home (W, H, K, "light", "apps", 0, "row");				save (c, "console-switch-v1-apps.png")
	d = home (W, H, K, "light", "apps", 2, "row", folder = "Productivity");	save (d, "console-switch-v1-folder.png")
	e = home (W, H, K, "light", "settings", 1, "row");			save (e, "console-switch-v1-settings.png")
	f = settings_page (W, H, K, "light");					save (f, "console-switch-v1-setpage.png")
	g = options_sheet (W, H, K, "light");					save (g, "console-switch-v1-options.png")
	h = game_menu (W, H, K, "light");					save (h, "console-switch-v1-menu.png")
	i = home (640, 480, 1, "light", "GB", 1, "row");			save (i, "console-switch-v1-home-640.png")
	j = home (W, H, K, "light", "GBA", 0, "row", lang = "fr");		save (j, "console-switch-v1-home-fr.png")
	# V2, dark
	a2 = home (W, H, K, "dark", "N64", 3, "row");				save (a2, "console-switch-v2-home.png")
	b2 = home (W, H, K, "dark", "apps", 2, "row", folder = "Internet");	save (b2, "console-switch-v2-folder.png")
	c2 = home (W, H, K, "dark", "onyx", 0, "cats");				save (c2, "console-switch-v2-onyx.png")
	d2 = game_menu (W, H, K, "dark");					save (d2, "console-switch-v2-menu.png")
	e2 = settings_page (W, H, K, "dark", "Sound", 1);			save (e2, "console-switch-v2-setpage.png")
	# V3, hero
	a3 = home (W, H, K, "hero", "GBA", 0, "row");				save (a3, "console-switch-v3-home.png")
	b3 = home (W, H, K, "hero", "N64", 0, "cats");				save (b3, "console-switch-v3-categories.png")
	c3 = home (W, H, K, "hero", "apps", 1, "row");				save (c3, "console-switch-v3-apps.png")
	d3 = home (W, H, K, "hero", "settings", 5, "row");			save (d3, "console-switch-v3-settings.png")
	# the sheets
	ov = sheet ([(a, "Home: a console's games", "Game Boy Advance shown (a dot under its round tile); Star Courier chosen: its name above, its line under the row."),
		     (b, "Down: the categories", "The ring on Super Nintendo; the row above shows its games, the chosen one without a ring; Up goes back to it."),
		     (c, "Apps: the folders", "One folder tile per category, its first four apps inside; A opens it."),
		     (d, "In a folder", "A Back tile first, then the category's apps (Archiver, Calculator, Calendar...); B goes back too."),
		     (e, "Settings: the pages as tiles", "Games, Sound, Gamepad, Keyboard & Mouse, Language & Region, Wi-Fi, Packages, Mode, Display."),
		     (f, "A settings page", "The pages at the left, the rows at the right: xset.h's rows, drawn in the theme."),
		     (g, "X on a game: its options", "Play, Save states (the slots' pictures, Left / Right a slot), Add to Favourites, Information, Delete."),
		     (h, "Home in a game: the menu", "The game paused; Resume, Save / Load state, Screenshot, Controls, Home, Close; the volume, the Wi-Fi."),
		     (i, "640 x 480 (compact)", "The same layout with 156 lp tiles; the category row scrolls, cut at the margins (chevrons).")],
		    "Onyx console mode v4 -- V1 light: the categories at the bottom, the content above",
		    "Up / Down: between the two rows; Left / Right: in a row; L1 / R1: the category at once; A opens; B back; X options; Y favourite; Home: the menu.")
	save (ov, "console-switch-overview.png")
	a2g = home (W, H, K, "dark", "GBA", 0, "row")
	vs = sheet ([(a, "V1 -- light", "A clean light-grey home; square tiles, a thin cyan ring; white round categories."),
		     (a2g, "V2 -- dark", "The same layout, dark grey; the ring brighter; the categories' circles grey."),
		     (a3, "V3 -- hero (larger tiles)", "The chosen game's picture as a banner with its name, details and Play; the chosen tile larger; the categories as chips.")],
		    "Onyx console mode v4 -- the three variants (1920 x 1080)", "The same moment in each: the Game Boy Advance's games, Star Courier chosen.",
		    bg = ((40, 42, 50), (24, 26, 32)), dark = True)
	save (vs, "console-switch-variants.png")

if __name__ == "__main__":
	main ()
