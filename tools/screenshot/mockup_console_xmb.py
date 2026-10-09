#!/usr/bin/env python3
# Copyright (c) 2026 the Onyx authors -- MIT licence (see docs/LICENSING.md).
"""mockup_console_xmb.py -- console mode v3: the console shell (user/Apps/consolehome) in the manner of Lakka's
menu (RetroArch's XMB): a horizontal row of white category icons (the Main menu, Settings, History, Favourites,
ONE PER CONSOLE that has games, Onyx games, Apps), the focused category's items as a vertical list under it,
the focused item at a fixed place, the ROM's title screen large at the right, a deeper level for a game's
actions, the quick menu over a running game -- on a calm blue gradient with a soft white ribbon. A design study
only (docs/COMPACT-SHELL-STUDY.md section 17): nothing here is built.

    python3 tools/screenshot/mockup_console_xmb.py      -> docs/compact-shell/mockups/console-v3-*.png

It reuses the drawing helpers and the content of mockup_console_ps4.py (v2: the card's apps, icons and categories,
the consoles from the emulators' `games =`, the made-up ROMs and their 160 x 144 pictures). No console maker's
logo, game or button symbol is drawn: the consoles' icons are generic shapes with their short names; the pad's
buttons are A / B / X / Y / L1 / R1 / Home. Resolution independent: logical units x a scale, a compact variant
below 560 logical lines.
"""
import math, os, random, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
import mockup_console_ps4 as v2
from mockup_console_ps4 import (font, mask, grad, put, rrect, ring, box, hline, glyph, tw, text, text_l, text_c, text_r,
				ellipsize, wrap, dim, add_glow, mix, lighten, shade, GLYPH, battery, app_icon, draw_icon, app_name,
				apps_in, ROMS, SYSNAME, SYSEXT, SYSEMU, SYSTEMS, SHORT, rom_picture, rom_file, game_picture, gameplay,
				slot_picture, HELP, OUT, ROOT, g_chev)

WHITE = (255, 255, 255)
THEMES = {"blue": ((10, 26, 74), (26, 86, 168), (60, 140, 214)),		# Lakka's default mood: a calm blue
	  "violet": ((26, 14, 60), (82, 48, 140), (150, 96, 200))}		# a theme variant (Settings' picture)

# ---- the words: English, French ---------------------------------------------------------------------------------
FR = {"Main Menu": "Menu principal", "Settings": "Réglages", "History": "Historique", "Favourites": "Favoris",
      "Onyx games": "Jeux Onyx", "Apps": "Apps", "%d games": "%d jeux", "%d items": "%d éléments", "OK": "OK", "Back": "Retour",
      "Search": "Rechercher", "Quick play": "Jouer direct", "Menu": "Menu", "Run": "Lancer", "Add to Favourites": "Ajouter aux favoris",
      "Save states": "Sauvegardes", "Information": "Informations", "Delete": "Supprimer", "Last played %s": "Dernière partie : %s",
      "Monday 21:04": "lundi 21:04", "Category": "Catégorie", "Item": "Élément", "2 of 3 slots": "2 emplacements sur 3",
      "Starts the game with the %s emulator": "Lance le jeu avec l'émulateur %s",
      "Title screen": "Écran titre", "Thu 9 Oct": "jeu. 9 oct."}
class Lang:
	def __init__ (s, code = "en"): s.code = code
	def __call__ (s, w): return FR.get (w, w) if s.code == "fr" else w

# ---- the screen and its metrics ---------------------------------------------------------------------------------
class Scr:
	def __init__ (s, W, H, k, lang = "en", theme = "blue"):
		s.W, s.H, s.k = W, H, k; s.LH = H / k; s.c = c = s.LH < 560; s.tr = Lang (lang); s.theme = theme
		s.m = dict (
			tx = 16 if c else 40, ty = 10 if c else 20, title = 16 if c else 24, count = 11 if c else 15,
			CY = 92 if c else 176,				# the category row's centre
			FX = 104 if c else 250,				# the focused category's (and the list's icons') x
			big = 50 if c else 88, small = 30 if c else 52, spr = 78 if c else 146, spl = 72 if c else 136,
			catlab = 11 if c else 15,
			FY = 184 if c else 322,				# the focused item's centre
			fi = 34 if c else 56, oi = 24 if c else 40,	# item icons: focused, others
			flab = 16 if c else 25, olab = 13 if c else 19, sub = 10 if c else 15,
			gap_below = 50 if c else 80, dy = 36 if c else 58, above = 60 if c else 100,
			lx = 30 if c else 50,				# the label's start from the icon's centre
			thx = 404 if c else 792, thy = 136 if c else 248, thw = 220 if c else 430,	# the right thumbnail
			bar = 28 if c else 38, hint = 10 if c else 14, valx = 388 if c else 760)
	def L (s, v): return int (round (v * s.k))
	def M (s, key): return s.L (s.m[key])

# ---- the background: a gradient and the soft white ribbon ------------------------------------------------------
def background (scr):
	W, H = scr.W, scr.H; a, b, c = THEMES[scr.theme]
	yy, xx = np.mgrid[0:H, 0:W].astype (float)
	t = np.clip ((xx / W) * 0.55 + (yy / H) * 0.45, 0, 1)
	col = np.where (t[..., None] < 0.6, np.array (a)[None, None] + (np.array (b) - np.array (a))[None, None] * (t[..., None] / 0.6),
			np.array (b)[None, None] + (np.array (c) - np.array (b))[None, None] * ((t[..., None] - 0.6) / 0.4))
	img = Image.fromarray (np.clip (col, 0, 255).astype ("uint8"), "RGB")
	# the ribbon: a band twisting across the screen (two curves crossing), filled faintly, its threads brighter
	band = Image.new ("L", (W, H), 0); d = ImageDraw.Draw (band)
	x = np.linspace (-20, W + 20, 260)
	def curve (ph, amp, base, fr = 1.0):
		return base * H + amp * H * np.sin (2 * math.pi * fr * x / W * 1.15 + ph)
	y1 = curve (0.4, 0.10, 0.60); y2 = curve (0.4 + 2.2, 0.08, 0.64, 1.08)
	poly = list (zip (x, y1)) + list (zip (x[::-1], y2[::-1]))
	d.polygon ([(float (p), float (q)) for p, q in poly], fill = 34)
	threads = Image.new ("L", (W, H), 0); dt = ImageDraw.Draw (threads)
	for i in range (14):
		f = i / 13; ys = y1 * (1 - f) + y2 * f + 6 * scr.k * np.sin (x / W * 9 + i)
		dt.line ([(float (p), float (q)) for p, q in zip (x, ys)], fill = 46 if i % 3 else 70, width = max (1, scr.L (1)))
	band = band.filter (ImageFilter.GaussianBlur (scr.L (6)))
	threads = threads.filter (ImageFilter.GaussianBlur (scr.L (0.8)))
	img.paste (WHITE, (0, 0), band); img.paste (WHITE, (0, 0), threads)
	# a soft light at the top left, a vignette at the bottom
	g = Image.new ("L", (W, H), 0); ImageDraw.Draw (g).ellipse ([-W * 0.3, -H * 0.6, W * 0.6, H * 0.5], fill = 26)
	img.paste (WHITE, (0, 0), g.filter (ImageFilter.GaussianBlur (H / 6)))
	return img

# ---- the white icons ---------------------------------------------------------------------------------------------
def g_star (d, s, k = 1):
	u = s * k; pts = []
	for i in range (10):
		r = 7.5 if i % 2 == 0 else 3.2; a = -math.pi / 2 + i * math.pi / 5
		pts.append ((8 * u + r * u * math.cos (a), 8.4 * u + r * u * math.sin (a)))
	d.polygon (pts, fill = 255)
def g_info (d, s, k = 1):
	u = s * k
	d.ellipse ([1 * u, 1 * u, 15 * u, 15 * u], fill = 255); d.rectangle ([7 * u, 7 * u, 9 * u, 12 * u], fill = 0); d.ellipse ([6.9 * u, 3.6 * u, 9.1 * u, 5.8 * u], fill = 0)
def g_monitor (d, s, k = 1):
	u = s * k
	d.rounded_rectangle ([1 * u, 2 * u, 15 * u, 12 * u], 1.5 * u, fill = 255); d.rectangle ([2.6 * u, 3.6 * u, 13.4 * u, 10.4 * u], fill = 0)
	d.rectangle ([7 * u, 12 * u, 9 * u, 14 * u], fill = 255); d.rectangle ([4.5 * u, 14 * u, 11.5 * u, 15 * u], fill = 255)
def g_speaker (d, s, k = 1):
	u = s * k
	d.polygon ([(1 * u, 6 * u), (4 * u, 6 * u), (8 * u, 2.5 * u), (8 * u, 13.5 * u), (4 * u, 10 * u), (1 * u, 10 * u)], fill = 255)
	d.arc ([5 * u, 4 * u, 12 * u, 12 * u], 300, 60, fill = 255, width = int (1.5 * u)); d.arc ([5 * u, 1 * u, 15.5 * u, 15 * u], 300, 60, fill = 255, width = int (1.5 * u))
def g_pad (d, s, k = 1):
	u = s * k
	d.rounded_rectangle ([0.5 * u, 4 * u, 15.5 * u, 12.5 * u], 4 * u, fill = 255)
	d.rectangle ([3 * u, 7.5 * u, 6.5 * u, 9 * u], fill = 0); d.rectangle ([4 * u, 6.5 * u, 5.5 * u, 10 * u], fill = 0)
	for cx, cy in [(11, 7), (13, 8.7), (11, 10.4), (9.2, 8.7)]: d.ellipse ([(cx - 0.8) * u, (cy - 0.8) * u, (cx + 0.8) * u, (cy + 0.8) * u], fill = 0)
def g_kbd (d, s, k = 1):
	u = s * k
	d.rounded_rectangle ([0.5 * u, 3.5 * u, 15.5 * u, 12.5 * u], 1.6 * u, fill = 255)
	for r in range (2):
		for i in range (5): d.rectangle ([(2 + i * 2.6) * u, (5.3 + r * 2.4) * u, (3.4 + i * 2.6) * u, (6.6 + r * 2.4) * u], fill = 0)
	d.rectangle ([4.5 * u, 10.2 * u, 11.5 * u, 11.2 * u], fill = 0)
def g_globe (d, s, k = 1):
	u = s * k
	d.ellipse ([1 * u, 1 * u, 15 * u, 15 * u], fill = 255)
	d.ellipse ([5 * u, 2.2 * u, 11 * u, 13.8 * u], outline = 0, width = max (1, int (1.1 * u)))
	d.line ([1 * u, 8 * u, 15 * u, 8 * u], fill = 0, width = max (1, int (1.1 * u))); d.line ([8 * u, 1 * u, 8 * u, 15 * u], fill = 0, width = max (1, int (1.1 * u)))
def g_modes (d, s, k = 1):			# the three modes: a screen, a phone, a pad
	u = s * k
	d.rounded_rectangle ([0.5 * u, 2 * u, 10 * u, 9 * u], 1 * u, fill = 255); d.rectangle ([1.7 * u, 3.2 * u, 8.8 * u, 7.8 * u], fill = 0)
	d.rounded_rectangle ([11 * u, 4 * u, 15.5 * u, 12.5 * u], 1 * u, fill = 255)
	d.rounded_rectangle ([2 * u, 10.5 * u, 10 * u, 15 * u], 2 * u, fill = 255)
def g_back (d, s, k = 1):
	u = s * k; d.arc ([3 * u, 3 * u, 15 * u, 15 * u], 180, 450, fill = 255, width = int (2 * u)); d.polygon ([(0, 9 * u), (6 * u, 9 * u), (3 * u, 4.5 * u)], fill = 255)
def g_list (d, s, k = 1):
	u = s * k
	for i in range (4): d.rounded_rectangle ([1 * u, (1.5 + i * 3.6) * u, 3 * u, (3.5 + i * 3.6) * u], 0.6 * u, fill = 255); d.rounded_rectangle ([4.5 * u, (1.8 + i * 3.6) * u, 15 * u, (3.2 + i * 3.6) * u], 0.6 * u, fill = 255)
GL = dict (GLYPH, star = g_star, info = g_info, monitor = g_monitor, speaker = g_speaker, pad = g_pad, kbd = g_kbd, globe = g_globe,
	   modes = g_modes, back = g_back, list = g_list)

def console_icon (d, s, size, code):
	"""A console as a generic white shape (no maker's design) with its short name in it."""
	u = s * size / 64; t = lambda v: v * u
	fb = font (max (6, int (t (11 if len (code) < 4 else 9))), True)
	def label (cx, cy, colour = 0):
		w = fb.getlength (code); b = fb.getbbox (code); d.text ((cx - w / 2, cy - (b[1] + b[3]) / 2), code, font = fb, fill = colour)
	if code in ("GB", "GBC"):					# an upright handheld
		d.rounded_rectangle ([t (16), t (4), t (48), t (60)], t (6), fill = 255)
		d.rounded_rectangle ([t (20), t (9), t (44), t (31)], t (3), fill = 0); label (t (32), t (20), 255)
		d.rectangle ([t (21), t (42), t (31), t (45)], fill = 0); d.rectangle ([t (24.5), t (38.5), t (27.5), t (48.5)], fill = 0)
		for cx, cy in [(39, 45), (44, 41)]: d.ellipse ([t (cx - 2.6), t (cy - 2.6), t (cx + 2.6), t (cy + 2.6)], fill = 0)
	elif code == "GBA":						# a wide handheld
		d.rounded_rectangle ([t (2), t (16), t (62), t (48)], t (12), fill = 255)
		d.rounded_rectangle ([t (19), t (20), t (45), t (44)], t (3), fill = 0); label (t (32), t (32), 255)
		d.rectangle ([t (6), t (30.5), t (15), t (33.5)], fill = 0); d.rectangle ([t (9), t (27.5), t (12), t (36.5)], fill = 0)
		for cx, cy in [(51, 34), (56, 29)]: d.ellipse ([t (cx - 2.4), t (cy - 2.4), t (cx + 2.4), t (cy + 2.4)], fill = 0)
	elif code in ("NES", "SNES"):					# a flat controller
		r = t (14) if code == "SNES" else t (3)
		d.rounded_rectangle ([t (2), t (18), t (62), t (46)], r, fill = 255)
		d.rectangle ([t (9), t (30.5), t (21), t (33.5)], fill = 0); d.rectangle ([t (13.5), t (26), t (16.5), t (38)], fill = 0)
		if code == "SNES":
			for cx, cy in [(50, 26), (56, 32), (50, 38), (44, 32)]: d.ellipse ([t (cx - 2.6), t (cy - 2.6), t (cx + 2.6), t (cy + 2.6)], fill = 0)
		else:
			for cx in (47, 55): d.ellipse ([t (cx - 3.2), t (33 - 3.2), t (cx + 3.2), t (33 + 3.2)], fill = 0)
		label (t (32), t (40) if code == "NES" else t (40), 0)
	elif code == "N64":						# a cartridge with a label
		d.polygon ([(t (8), t (6)), (t (56), t (6)), (t (56), t (50)), (t (50), t (58)), (t (14), t (58)), (t (8), t (50))], fill = 255)
		d.rounded_rectangle ([t (14), t (12), t (50), t (40)], t (3), fill = 0); label (t (32), t (26), 255)
		for i in range (5): d.rectangle ([t (18 + i * 6), t (46), t (20 + i * 6), t (54)], fill = 0)
	elif code == "GC":						# a disc
		d.ellipse ([t (4), t (4), t (60), t (60)], fill = 255); d.ellipse ([t (26), t (26), t (38), t (38)], fill = 0)
		d.arc ([t (12), t (12), t (52), t (52)], 200, 250, fill = 0, width = max (1, int (t (2))))
		label (t (32), t (48), 0)

def white_icon (img, kind, cx, cy, size, alpha = 255):
	"""kind: a glyph name of GL, or ("console", code). Centred at (cx, cy)."""
	size = int (size); x, y = int (cx - size / 2), int (cy - size / 2)
	if isinstance (kind, tuple):
		m = mask (size, size, lambda d, s: console_icon (d, s, size, kind[1]))
	else:
		m = mask (size, size, lambda d, s: GL[kind] (d, s, size / 16))
	# a faint shadow under the white
	sh = Image.new ("L", img.size, 0); sh.paste (m, (x, y + max (1, size // 40))); sh = sh.filter (ImageFilter.GaussianBlur (max (1, size / 24)))
	put (img, 0, 0, sh, (0, 10, 40), alpha * 0.35)
	put (img, x, y, m, WHITE, alpha)

# ---- the categories --------------------------------------------------------------------------------------------------
def categories ():
	c = [("main", "Main Menu", "gem"), ("settings", "Settings", "gear"), ("history", "History", "clock"), ("fav", "Favourites", "star")]
	for name, _, _ in SYSTEMS:
		code = SHORT.get (name, name)
		if ROMS.get (code): c.append ((code, name, ("console", code)))	# a console with no game is not shown
	c += [("onyx", "Onyx games", "pad"), ("apps", "Apps", "grid")]
	return c

# ---- the items of a category: (icon, label, sublabel, picture, value) ------------------------------------------------
def rom_list (code):
	out = []
	for i, (t, style, last, played, slots) in enumerate (ROMS[code]):
		out.append (dict (icon = ("console", code), label = t, pic = rom_picture (code, t, style, i * 7 + len (t)), sys = code,
				  last = last, played = played, slots = slots, style = style, seed = i * 7 + len (t)))
	return sorted (out, key = lambda r: r["label"].lower ())		# a playlist is alphabetical
SETTINGS = [("monitor", "Display", "displayconf"), ("speaker", "Sound", "soundconf"), ("pad", "Gamepad", "padconf"),
	    ("kbd", "Keyboard & Mouse", "keyconf"), ("wifi", "Wi-Fi", "wpaconf"), ("globe", "Language & Region", "langconf"),
	    ("modes", "Mode", "modeconf"), ("download", "Packages", "pkgman"), ("info", "About", "control")]
def settings_list ():
	return [dict (icon = g, label = n, sub = HELP.get (a, "")) for g, n, a in SETTINGS]
def app_list ():
	out = []
	for cat in ["Productivity", "Internet", "Graphics", "Multimedia", "Programming", "System"]:
		for a in apps_in (cat): out.append (dict (app = a, label = app_name (a), sub = cat + ("  -  " + v2.KIND[a] if a in v2.KIND else "")))
	return out
def onyx_list ():
	out = []
	for a in apps_in ("Games"):
		if a == "gamelib": continue
		out.append (dict (app = a, label = app_name (a), sub = "Onyx game", pic = game_picture (a)))
	return out

# ---- the parts -----------------------------------------------------------------------------------------------------------
def top (img, scr, title, count = None):
	L, M = scr.L, scr.M
	ft = font (M ("title"), True); x = M ("tx"); y = M ("ty")
	text (img, x, y, title, ft, WHITE)
	if count: text (img, x + tw (title, ft) + L (14), y + (M ("title") - M ("count")) * 0.75, count, font (M ("count")), (200, 214, 240))
	fc = font (M ("title") * 0.85); xr = scr.W - M ("tx")
	text_r (img, xr, y, M ("title") + L (4), "21:07", fc, WHITE); xr -= tw ("21:07", fc) + L (14)
	battery (img, xr - L (24), y + (M ("title") + L (4) - L (13)) // 2, scr.k, (230, 238, 255)); xr -= L (36)
	if not scr.c:
		fd = font (M ("count")); dt = scr.tr ("Thu 9 Oct"); text_r (img, xr, y, M ("title") + L (4), dt, fd, (200, 214, 240))

def cat_row (img, scr, cats, f, alpha = 1.0, shift = 0, show_label = True):
	"""The categories: the focused one at FX, large and opaque, its name under it; the others small and faded."""
	L, M = scr.L, scr.M; CY = M ("CY"); FX = M ("FX") + shift
	for i, (key, name, ic) in enumerate (cats):
		if i == f: x = FX
		elif i > f: x = FX + M ("big") // 2 + L (scr.m["spr"] - scr.m["big"] // 2) + (i - f - 1) * M ("spr")
		else: x = FX - (f - i) * M ("spl")
		size = M ("big") if i == f else M ("small")
		if x < -size or x > scr.W + size: continue
		white_icon (img, ic, x, CY, size, 255 * alpha if i == f else 110 * alpha)
	if show_label and alpha > 0.5:
		name = scr.tr (cats[f][1]); fl = font (M ("catlab"), True)
		text_c (img, FX - L (120), CY + M ("big") // 2 + L (4), L (240), M ("catlab") + L (6), name, fl, WHITE)

def item_icon (img, scr, it, cx, cy, size, alpha):
	if it.get ("app"):
		ic = app_icon (it["app"], int (size))
		if alpha < 255:
			a = ic.split ()[3].point (lambda v: v * alpha // 255); ic = ic.copy (); ic.putalpha (a)
		img.paste (ic, (int (cx - size / 2), int (cy - size / 2)), ic)
	else: white_icon (img, it["icon"], cx, cy, size, alpha)

def item_list (img, scr, items, f, x_icon = None, sub = None, value_of = None, max_label = None, deep = False):
	"""The vertical list: the focused item at FY (larger, white, its sublabel under it), the ones after it below,
	the ones before it above the category row, faded."""
	L, M = scr.L, scr.M; FY = M ("FY"); cx = x_icon if x_icon is not None else M ("FX")
	lx = cx + M ("lx"); maxw = max_label or (M ("thx") - L (24) - lx)
	for j, it in enumerate (items):
		if j == f:
			y = FY; size = M ("fi"); a = 255; fl = font (M ("flab"), True)
		elif j > f:
			y = FY + M ("gap_below") + (j - f - 1) * M ("dy"); size = M ("oi"); a = 150; fl = font (M ("olab"))
			if y > scr.H - M ("bar") - L (14): continue
		else:
			if deep:		# a deeper level has no category row: the items before sit evenly above
				y = FY - M ("gap_below") - (f - j - 1) * M ("dy"); a = 150 - 25 * (f - j - 1)
			else:
				y = M ("CY") - M ("above") - (f - j - 1) * M ("dy"); a = 90 - 30 * (f - j - 1)
			size = M ("oi"); fl = font (M ("olab"))
			if y < M ("ty") + M ("title") + L (24) or a <= 0: continue
		item_icon (img, scr, it, cx, y, size, a)
		col = mix ((40, 80, 150), WHITE, a / 255)
		lab = scr.tr (it["label"])
		vw = 0
		if value_of and value_of (it):
			v = value_of (it); fv = font (M ("olab") if j != f else M ("flab") * 0.85, j == f)
			vw = tw (v, fv) + L (24); text_r (img, M ("valx"), y - L (12) if j == f else y - L (10), L (24) if j == f else L (20), v, fv, col)
		text_l (img, lx, y - (L (14) if j == f and (sub or it.get ("sub")) else L (12) if j == f else L (11)), L (24) if j == f else L (22), ellipsize (lab, fl, maxw - vw), fl, col)
		if j == f:
			s_ = sub if sub is not None else it.get ("sub")
			if s_: text (img, lx + L (1), y + L (10), ellipsize (s_, font (M ("sub")), maxw), font (M ("sub")), (210, 224, 248))

def thumbnail (img, scr, pic, caption = None):
	L, M = scr.L, scr.M
	w = M ("thw"); h = int (w * 0.9); x = M ("thx"); y = M ("thy")
	r = pic.width / pic.height
	if abs (r - 160 / 144) > 0.05: h = int (w / r)			# another shape (an app's picture): its own ratio
	sh = Image.new ("L", img.size, 0); ImageDraw.Draw (sh).rectangle ([x, y + L (8), x + w, y + h + L (8)], fill = 150)
	img.paste ((0, 6, 30), (0, 0), sh.filter (ImageFilter.GaussianBlur (L (12))))
	img.paste (pic.resize ((w, h), Image.LANCZOS), (x, y))
	if caption: text_c (img, x, y + h + L (8), w, L (20), caption, font (M ("sub")), (210, 224, 248))

PADC = {"A": (94, 156, 255), "B": (255, 106, 106), "X": (180, 140, 240), "Y": (240, 190, 80)}
def hints (img, scr, items, left = None):
	"""A thin bar at the bottom: what the screen is (left) and the buttons (right, as Lakka's)."""
	L, M = scr.L, scr.M; bh = M ("bar"); y0 = scr.H - bh
	band = Image.new ("RGB", (scr.W, bh), (4, 10, 30)); img.paste (Image.blend (img.crop ((0, y0, scr.W, scr.H)), band, 0.55), (0, y0))
	hline (img, 0, y0, scr.W, WHITE, alpha = 40)
	f = font (M ("hint")); fb = font (M ("hint") - L (1), True); d = L (18 if not scr.c else 15)
	if left:
		gs = L (12 if not scr.c else 10); put (img, M ("tx"), y0 + (bh - gs) // 2, mask (gs, gs, lambda dd, s: GL["gem"] (dd, s, gs / 16)), (170, 205, 255))
		text_l (img, M ("tx") + gs + L (8), y0, bh, left, f, (200, 214, 240))
	# measure, then draw from the right
	def width (key, word):
		if key in PADC or key in ("lr", "ud"): kw = d
		else: kw = sum (tw (p, fb) + L (12) + L (3) for p in key.split ("/"))
		return kw + L (6) + tw (scr.tr (word), f) + L (18)
	x = scr.W - M ("tx") - sum (width (k, w) for k, w in items) + L (18); y = y0 + (bh - d) // 2
	for key, word in items:
		if key in PADC:
			rrect (img, x, y, d, d, d / 2, (10, 16, 36)); ring (img, x, y, d, d, d / 2, PADC[key], t = L (1.4))
			text_c (img, x, y - L (1), d, d, key, fb, PADC[key]); x += d
		elif key in ("lr", "ud"):
			u = d / 16
			def fn (dd, s, key = key):
				for (ax, ay, aw, ah), arm in [((5.5, 0.5, 5, 6), "u"), ((5.5, 9.5, 5, 6), "d"), ((0.5, 5.5, 6, 5), "l"), ((9.5, 5.5, 6, 5), "r")]:
					lit = (arm in "lr") == (key == "lr")
					dd.rounded_rectangle ([ax * u * s, ay * u * s, (ax + aw) * u * s, (ay + ah) * u * s], u * s, fill = 255 if lit else 80)
				dd.rectangle ([5.5 * u * s, 5.5 * u * s, 10.5 * u * s, 10.5 * u * s], fill = 140)
			glyph (img, x, y, d, d, fn, (230, 238, 252)); x += d
		else:
			for p in key.split ("/"):
				pw = tw (p, fb) + L (12)
				rrect (img, x, y + L (1), pw, d - L (2), L (5), (10, 16, 36)); ring (img, x, y + L (1), pw, d - L (2), L (5), (190, 204, 230), t = L (1.2))
				text_c (img, x, y, pw, d, p, fb, (230, 238, 252)); x += pw + L (3)
			x -= L (3)
		x += L (6); text_l (img, x, y0, bh, scr.tr (word), f, (220, 230, 248)); x += tw (scr.tr (word), f) + L (18)

# ---- the screens ---------------------------------------------------------------------------------------------------------
HOME_HINTS = [("lr", "Category"), ("ud", "Item"), ("Y", "Search"), ("X", "Quick play"), ("B", "Back"), ("A", "OK")]
def home (W, H, k, cat = "GBA", f = None, lang = "en", theme = "blue"):
	scr = Scr (W, H, k, lang, theme); tr = scr.tr
	cats = categories (); ci = [c[0] for c in cats].index (cat)
	img = background (scr)
	if cat in ROMS:
		items = rom_list (cat); f = [r["label"] for r in items].index ("Star Courier") if (f is None and cat == "GBA") else (f or 0)
		it = items[f]
		top (img, scr, SYSNAME[cat], tr ("%d games") % len (items))
		cat_row (img, scr, cats, ci)
		sub = "%s  -  %s" % (rom_file (cat, it["label"]).split ("/")[-1], tr ("Last played %s") % tr (it["last"]))
		item_list (img, scr, items, f, sub = sub)
		thumbnail (img, scr, it["pic"])
		hs = HOME_HINTS if not scr.c else [("lr", "Category"), ("ud", "Item"), ("B", "Back"), ("A", "OK")]
		hints (img, scr, hs, left = None if scr.c else "Onyx  -  %s (%s)" % (SYSNAME[cat], SYSEMU[cat]))
	elif cat == "settings":
		items = settings_list (); f = 1 if f is None else f
		top (img, scr, tr ("Settings"), tr ("%d items") % len (items))
		cat_row (img, scr, cats, ci)
		item_list (img, scr, items, f, max_label = scr.W - scr.M ("FX") - scr.M ("lx") - scr.L (80))
		hints (img, scr, [("lr", "Category"), ("ud", "Item"), ("B", "Back"), ("A", "OK")], left = "Onyx  -  SD:/etc/sound.ini")
	elif cat == "apps":
		items = app_list (); f = [x["app"] for x in items].index ("letters") if f is None else f
		top (img, scr, tr ("Apps"), tr ("%d items") % len (items))
		cat_row (img, scr, cats, ci)
		item_list (img, scr, items, f, value_of = lambda x: "running" if x["app"] in ("letters", "media", "telegram") else None,
			   )
		p = os.path.join (v2.SHOTS, items[f]["app"] + ".png")
		if os.path.exists (p):					# a running app: its live picture at the right
			im = Image.open (p).convert ("RGB"); thumbnail (img, scr, im.crop ((4, 28, im.width - 4, im.height - 4)), caption = "Running  -  Letters-tour.rtf")
		hints (img, scr, [("lr", "Category"), ("ud", "Item"), ("Y", "Search"), ("B", "Back"), ("A", "OK")], left = "Onyx  -  Productivity")
	elif cat == "onyx":
		items = onyx_list (); f = [x["app"] for x in items].index ("tetris") if f is None else f
		top (img, scr, tr ("Onyx games"), tr ("%d games") % len (items))
		cat_row (img, scr, cats, ci)
		item_list (img, scr, items, f, sub = "Onyx game  -  SD:/apps/tetris.app")
		if items[f].get ("pic") is not None: thumbnail (img, scr, items[f]["pic"])
		hints (img, scr, HOME_HINTS, left = "Onyx  -  Onyx games")
	return img

def game_sub (W, H, k, lang = "en", f = 0, slots = False):
	"""A on a game: the deeper level -- the categories gone, the game faded at the left, its actions as a list."""
	scr = Scr (W, H, k, lang); tr = scr.tr; L, M = scr.L, scr.M
	img = background (scr)
	items = rom_list ("GBA"); it = [r for r in items if r["label"] == "Star Courier"][0]
	top (img, scr, "Star Courier", SYSNAME["GBA"] if not slots else tr ("Save states"))
	# the parents: the game's icon (and, one level deeper, Save states') at the left, faded; their labels hidden
	px = M ("FX") - L (150 if not scr.c else 70)
	if slots:
		white_icon (img, ("console", "GBA"), px - L (90), M ("FY"), M ("oi"), 50)
		white_icon (img, "save", px, M ("FY"), M ("fi"), 90)
	else: white_icon (img, ("console", "GBA"), px, M ("FY"), M ("fi"), 90)
	cx = M ("FX") + L (40 if not scr.c else 20)
	if not slots:
		acts = [dict (icon = "play", label = "Run", sub = tr ("Starts the game with the %s emulator") % "gbaemu"),
			dict (icon = "star", label = "Add to Favourites"), dict (icon = "save", label = "Save states", val = tr ("2 of 3 slots")),
			dict (icon = "info", label = "Information"), dict (icon = "close", label = "Delete")]
		item_list (img, scr, acts, f, x_icon = cx, value_of = lambda x: x.get ("val"), deep = True)
		thumbnail (img, scr, it["pic"])
		hints (img, scr, [("ud", "Item"), ("B", "Back"), ("A", "OK")], left = None if scr.c else "Onyx  -  " + rom_file ("GBA", "Star Courier"))
	else:
		dates = ["Mon 21:04", "Sun 10:12", None]
		acts = [dict (icon = "save", label = "Slot %d" % (i + 1) if lang == "en" else "Emplacement %d" % (i + 1),
			      sub = "A loads it, Y deletes it" if dates[i] else "", val = dates[i] or "empty") for i in range (3)]
		item_list (img, scr, acts, f, x_icon = cx, value_of = lambda x: x["val"], deep = True)
		thumbnail (img, scr, slot_picture (it, f, 160, 144), caption = "Slot %d  -  %s" % (f + 1, dates[f]))
		hints (img, scr, [("ud", "Item"), ("Y", "Delete"), ("B", "Back"), ("A", "Load")], left = "Onyx  -  the save states of star-courier.gba")
	return img

def quick_menu (W = 1280, H = 720, k = 1, lang = "en"):
	"""Home in a game: Lakka's quick menu, in the XMB list's style, over the paused game."""
	scr = Scr (W, H, k, lang); L, M = scr.L, scr.M
	img = gameplay (W, H).filter (ImageFilter.GaussianBlur (L (2)))
	img = dim (img, 175, (6, 16, 48))		# the game, paused, dimmed behind the menu
	FRQ = {"Resume": "Reprendre", "Restart": "Recommencer", "Close game": "Fermer le jeu", "Save state": "Sauvegarder l'état",
	       "Load state": "Charger l'état", "Undo load state": "Annuler le chargement", "Screenshot": "Capture d'écran",
	       "Add to Favourites": "Ajouter aux favoris", "Controls": "Commandes", "Emulator options": "Options de l'émulateur",
	       "Switch app": "Changer d'app", "Home": "Accueil", "Quick Menu": "Menu rapide"}
	t = (lambda w: FRQ.get (w, w)) if lang == "fr" else (lambda w: w)
	top (img, scr, t ("Quick Menu"), "Star Courier  -  Game Boy Advance")
	acts = [("play", "Resume"), ("back", "Restart"), ("close", "Close game"), ("save", "Save state"), ("load", "Load state"),
		("back", "Undo load state"), ("camera", "Screenshot"), ("star", "Add to Favourites"), ("pad", "Controls"),
		("list", "Emulator options"), ("grid", "Switch app"), ("home", "Home")]
	items = [dict (icon = g, label = t (lab)) for g, lab in acts]
	items[3]["val"] = "<  Slot 2  >" if lang == "en" else "<  Empl. 2  >"; items[4]["val"] = "Slot 2" if lang == "en" else "Empl. 2"
	items[3]["sub"] = ("Overwrites slot 2 (Sun 10:12); Left / Right: another slot" if lang == "en" else
			   "Remplace l'emplacement 2 (dim. 10:12) ; gauche / droite : un autre")
	# the quick menu's list stands where the categories were: the items above sit higher
	item_list (img, scr, items, 3, value_of = lambda x: x.get ("val"), deep = True)
	it = dict (sys = "GBA", style = "hills", seed = 12)
	thumbnail (img, scr, slot_picture (it, 1, 160, 144), caption = "Slot 2  -  Sun 10:12" if lang == "en" else "Emplacement 2  -  dim. 10:12")
	hints (img, scr, [("lr", "Slot" if lang == "en" else "Emplacement"), ("ud", "Item"), ("B", "Back"), ("A", "OK"), ("Home", "Resume")],
	       left = "Onyx  -  gbaemu, paused 6 h 41 min" if lang == "en" else "Onyx  -  gbaemu, en pause, 6 h 41 min")
	return img

# ---- the sheets -------------------------------------------------------------------------------------------------------------
def overview (shots):
	tw_, th = 560, 315; gx = 70
	W = 3 * tw_ + 2 * gx + 120; H = 140 + 2 * (th + 150) + 30
	img = grad (W, H, (14, 26, 60), (6, 12, 28))
	put (img, 60, 38, mask (26, 26, lambda d, s: GL["gem"] (d, s, 26 / 16)), (150, 200, 255))
	text (img, 100, 34, "Onyx console mode v3 -- the XMB-like shell (Lakka): categories across, items down", font (26, True), WHITE)
	text (img, 100, 72, "Left / Right: the category (one per console); Up / Down: the item; A: deeper (a game's actions); B: back; Home in a game: the quick menu.", font (17), (170, 190, 220))
	for i, (im, cap, sub, move) in enumerate (shots):
		r, c = divmod (i, 3); x = 60 + c * (tw_ + gx); y = 140 + r * (th + 150)
		img.paste (im.resize ((tw_, th), Image.LANCZOS), (x, y)); ring (img, x - 1, y - 1, tw_ + 2, th + 2, 4, (140, 170, 230), alpha = 120)
		if move:
			f = font (14, True); lw = tw ("from Home: " + move if move.startswith (("Left", "Right")) else move, f) + 20
			rrect (img, x, y - 34, lw, 26, 8, (8, 14, 34), alpha = 230); ring (img, x, y - 34, lw, 26, 8, (120, 190, 255))
			text_c (img, x, y - 34, lw, 26, "from Home: " + move if move.startswith (("Left", "Right")) else move, f, WHITE)
		text (img, x, y + th + 12, cap, font (18, True), WHITE)
		for j, line in enumerate (wrap (sub, font (15), tw_)[:3]): text (img, x, y + th + 40 + j * 21, line, font (15), (170, 190, 220))
	return img

def compare (a, b):
	tw_, th = 900, 506; W = 2 * tw_ + 3 * 50; H = th + 330
	img = grad (W, H, (14, 26, 60), (6, 12, 28))
	text (img, 50, 30, "v2 or v3? The same moment: the Game Boy Advance's games, Star Courier focused (1280 x 720)", font (24, True), WHITE)
	cols = [("v2 -- the TV console home (PS4-like): one big row", a,
		 ["Shelves as chips; the games as big pictures in ONE ROW, the focused one larger", "The picture blurred full screen; details under the row (Play, save slots)",
		  "Sees ~7 games at once by their pictures; slots visible without a step", "4 levels: function row, shelves, row, details"]),
		("v3 -- the XMB (Lakka): categories across, items down", b,
		 ["White icons across: Main menu, Settings, History, Favourites, the consoles, Onyx games, Apps", "The games as a vertical LIST of names (alphabetical), the title screen at the right",
		  "Sees ~6 names at once, one picture; long lists read fast; actions one level deeper (A)", "2 axes only: Left / Right the category, Up / Down the item"])]
	for i, (cap, im, lines) in enumerate (cols):
		x = 50 + i * (tw_ + 50); y = 80
		img.paste (im.resize ((tw_, th), Image.LANCZOS), (x, y)); ring (img, x - 1, y - 1, tw_ + 2, th + 2, 4, (140, 170, 230), alpha = 140)
		text (img, x, y + th + 16, cap, font (20, True), WHITE)
		for j, l in enumerate (lines):
			put (img, x + 2, y + th + 58 + j * 28, mask (8, 8, lambda d, s: d.ellipse ([0, 0, 8 * s - 1, 8 * s - 1], fill = 255)), (120, 190, 255))
			text (img, x + 18, y + th + 50 + j * 28, l, font (16), (190, 206, 232))
	return img

def save (img, name):
	p = os.path.join (OUT, name); img.save (p); print ("wrote", os.path.relpath (p, ROOT), img.size)

def main ():
	os.makedirs (OUT, exist_ok = True)
	a = home (1280, 720, 1, "GBA");				save (a, "console-v3-home.png")
	b = home (1920, 1080, 1.5, "N64", 1);			save (b, "console-v3-home-1080.png")
	b2 = home (640, 480, 1, "GB", 1);			save (b2, "console-v3-home-640.png")
	c = home (1280, 720, 1, "settings", theme = "violet");	save (c, "console-v3-settings.png")
	ap = home (1280, 720, 1, "apps");			save (ap, "console-v3-apps.png")
	on = home (1280, 720, 1, "onyx");			save (on, "console-v3-onyx.png")
	d = game_sub (1280, 720, 1);				save (d, "console-v3-game.png")
	ds = game_sub (1280, 720, 1, f = 1, slots = True);	save (ds, "console-v3-savestates.png")
	e = quick_menu ();					save (e, "console-v3-quickmenu.png")
	fr = game_sub (1280, 720, 1, lang = "fr");		save (fr, "console-v3-game-fr.png")
	frh = home (1280, 720, 1, "GBA", lang = "fr");		save (frh, "console-v3-home-fr.png")
	ov = overview ([(a, "Home: a console's games", "Game Boy Advance among the categories; its 9 games as a list (alphabetical), Star Courier focused, its title screen at the right.", None),
			(d, "A: the game's actions", "The categories gone, the game faded at the left: Run, Add to Favourites, Save states (2 of 3), Information, Delete.", "A on a game"),
			(ds, "A on Save states: the slots", "Slot 2 focused, its picture at the right; A loads it, Y deletes it.", "A on Save states"),
			(c, "The Settings category", "Display, Sound, Gamepad, Keyboard & Mouse, Wi-Fi, Language & Region, Mode, Packages, About (a theme variant: violet).", "Left x 6"),
			(ap, "The Apps category", "Every app, its real icon; Letters focused, running.", "Right x 5"),
			(e, "Home in a game: the quick menu", "Resume, Restart, Close game, Save state (slot 2, Left / Right to change), Load state, Undo, Screenshot...", "Home in a game")])
	save (ov, "console-v3-overview.png")
	v2home = v2.home (1280, 720, 1, "GBA", 0, "row")
	save (compare (v2home, a), "console-v2-v3-compare.png")

if __name__ == "__main__":
	main ()
