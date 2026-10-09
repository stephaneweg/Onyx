#!/usr/bin/env python3
# Copyright (c) 2026 the Onyx authors -- MIT licence (see docs/LICENSING.md).
"""mockup_console_settings.py -- console mode: the settings applets in the XMB (Lakka) style, for a pad alone, and
the virtual keyboard where text must be typed. A design study only (docs/COMPACT-SHELL-STUDY.md section 18):
nothing here is built.

    python3 tools/screenshot/mockup_console_settings.py   -> docs/compact-shell/mockups/console-set-*.png

The home is consolehome's XMB as built (user/Apps/consolehome/xmb.h: a column per console that has ROMs, Onyx,
Apps, Settings); the Settings column holds the eight console applets. A enters one -- the XMB's deeper level: the
column row slid to the left, the applets faded as the parent column, the applet's settings as rows (an icon, a
label, the value right-aligned; Left / Right change it in place, A goes deeper or acts, B comes back).
What each row sets is what the desktop applet sets (user/Apps/{soundconf,padconf,keyconf,langconf,wpaconf,pkgman,
modeconf,displayconf}: the same files, values and ranges), plus the console's own SD:/etc/console.ini [screen].

It reuses mockup_console_xmb.py (v3) and mockup_console_ps4.py (v2): the background and its ribbon, the white
icons, the hints, the card's apps and icons. No trademark is drawn; the pad's buttons are A / B / X / Y / L1 / R1 /
Start / Home; generic gamepad drawings only.
"""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
import mockup_console_ps4 as v2
import mockup_console_xmb as v3
from mockup_console_ps4 import (font, mask, grad, put, rrect, ring, box, hline, glyph, tw, text, text_l, text_c, text_r,
				ellipsize, wrap, dim, add_glow, mix, lighten, shade, battery, app_icon, OUT, ROOT, SD, g_chev)
from mockup_console_xmb import background, white_icon, GL, hints, PADC

WHITE = (255, 255, 255)
SUBC = (210, 224, 248)
ACC = (96, 170, 255)
GREEN = (120, 220, 140); AMBER = (250, 200, 80); RED = (255, 120, 110)

# ---- more white glyphs (16 units) -------------------------------------------------------------------------------
def g_lock (d, s, k = 1):
	u = s * k
	d.arc ([4 * u, 1 * u, 12 * u, 11 * u], 180, 360, fill = 255, width = int (2 * u))
	d.line ([5 * u, 6 * u, 5 * u, 8 * u], fill = 255, width = int (2 * u)); d.line ([11 * u, 6 * u, 11 * u, 8 * u], fill = 255, width = int (2 * u))
	d.rounded_rectangle ([2.5 * u, 7 * u, 13.5 * u, 15.5 * u], 2 * u, fill = 255)
def g_trash (d, s, k = 1):
	u = s * k
	d.rectangle ([2 * u, 3 * u, 14 * u, 4.6 * u], fill = 255); d.rectangle ([6 * u, 1 * u, 10 * u, 3 * u], fill = 255)
	d.polygon ([(3.5 * u, 5.5 * u), (12.5 * u, 5.5 * u), (11.5 * u, 15 * u), (4.5 * u, 15 * u)], fill = 255)
	for x in (6.2, 8, 9.8): d.line ([x * u, 7.5 * u, x * u, 13 * u], fill = 0, width = max (1, int (0.9 * u)))
def g_refresh (d, s, k = 1):
	u = s * k
	d.arc ([2 * u, 2 * u, 14 * u, 14 * u], 30, 320, fill = 255, width = int (2 * u)); d.polygon ([(15 * u, 1.5 * u), (15 * u, 7.5 * u), (9.5 * u, 6 * u)], fill = 255)
def g_mouse (d, s, k = 1):
	u = s * k
	d.rounded_rectangle ([3.5 * u, 1 * u, 12.5 * u, 15 * u], 4.5 * u, fill = 255); d.line ([8 * u, 1.5 * u, 8 * u, 6.5 * u], fill = 0, width = max (1, int (1.2 * u)))
	d.line ([3.5 * u, 6.5 * u, 12.5 * u, 6.5 * u], fill = 0, width = max (1, int (1.2 * u)))
def g_search (d, s, k = 1):
	u = s * k
	d.ellipse ([1 * u, 1 * u, 11 * u, 11 * u], outline = 255, width = int (2.2 * u)); d.line ([9 * u, 9 * u, 14.5 * u, 14.5 * u], fill = 255, width = int (2.6 * u))
def g_mute (d, s, k = 1):
	u = s * k
	d.polygon ([(1 * u, 6 * u), (4 * u, 6 * u), (8 * u, 2.5 * u), (8 * u, 13.5 * u), (4 * u, 10 * u), (1 * u, 10 * u)], fill = 255)
	d.line ([10 * u, 5.5 * u, 15 * u, 10.5 * u], fill = 255, width = int (1.8 * u)); d.line ([15 * u, 5.5 * u, 10 * u, 10.5 * u], fill = 255, width = int (1.8 * u))
def g_bars (d, s, k = 1):			# levels (the mixer)
	u = s * k
	for i, h in enumerate ((6, 11, 8, 14)): d.rounded_rectangle ([(1 + i * 3.8) * u, (15 - h) * u, (3.6 + i * 3.8) * u, 15 * u], 0.8 * u, fill = 255)
def g_clock2 (d, s, k = 1): GL["clock"] (d, s, k)
def g_pin (d, s, k = 1):			# a map pin (the time zone)
	u = s * k
	d.ellipse ([3 * u, 1 * u, 13 * u, 11 * u], fill = 255); d.polygon ([(4 * u, 8 * u), (12 * u, 8 * u), (8 * u, 15.5 * u)], fill = 255)
	d.ellipse ([6 * u, 4 * u, 10 * u, 8 * u], fill = 0)
def g_check (d, s, k = 1):
	u = s * k; d.line ([(2 * u, 8.5 * u), (6.5 * u, 13 * u), (14 * u, 3.5 * u)], fill = 255, width = int (2.4 * u), joint = "curve")
def g_flag (d, s, k = 1):
	u = s * k; d.rectangle ([2 * u, 1 * u, 3.6 * u, 15.5 * u], fill = 255); d.polygon ([(3.6 * u, 2 * u), (14 * u, 4.5 * u), (3.6 * u, 8.5 * u)], fill = 255)
def g_eye (d, s, k = 1):
	u = s * k
	d.ellipse ([0.5 * u, 3.5 * u, 15.5 * u, 12.5 * u], fill = 255); d.ellipse ([5 * u, 5 * u, 11 * u, 11 * u], fill = 0); d.ellipse ([6.7 * u, 6.7 * u, 9.3 * u, 9.3 * u], fill = 255)
def g_box (d, s, k = 1):			# a package
	u = s * k
	d.polygon ([(8 * u, 1 * u), (15 * u, 4.5 * u), (8 * u, 8 * u), (1 * u, 4.5 * u)], fill = 255)
	d.polygon ([(1 * u, 5.5 * u), (7.4 * u, 8.8 * u), (7.4 * u, 15.5 * u), (1 * u, 12 * u)], fill = 255)
	d.polygon ([(15 * u, 5.5 * u), (8.6 * u, 8.8 * u), (8.6 * u, 15.5 * u), (15 * u, 12 * u)], fill = 255)
def g_hidden (d, s, k = 1):			# a network typed by name
	u = s * k; GL["wifi"] (d, s, k); d.rectangle ([10 * u, 9 * u, 16 * u, 16 * u], fill = 0); d.text ((10.5 * u, 7.5 * u), "?", font = font (8 * u, True), fill = 255)
GL.update (lock = g_lock, trash = g_trash, refresh = g_refresh, mouse = g_mouse, search = g_search, mute = g_mute, bars = g_bars,
	   pin = g_pin, check = g_check, flag = g_flag, eye = g_eye, box = g_box, hidden = g_hidden)

# ---- the words ------------------------------------------------------------------------------------------------------
FR = {"Settings": "Réglages", "Sound": "Son", "Gamepad": "Manette", "Keyboard & Mouse": "Clavier et souris",
      "Language & Region": "Langue et région", "Wi-Fi": "Wi-Fi", "Packages": "Paquets", "Mode": "Mode", "Display": "Affichage",
      "Back": "Retour", "OK": "OK", "Change": "Changer", "Item": "Élément", "Delete": "Effacer", "Space": "Espace",
      "Shift": "Maj", "Cursor": "Curseur", "Done": "Valider", "Connect": "Se connecter", "Password": "Mot de passe",
      "Show password": "Afficher le mot de passe", "Security": "Sécurité", "Signal": "Signal", "Forget this network": "Oublier ce réseau",
      "Category": "Catégorie", "Open": "Ouvrir", "Symbols": "Symboles",
      "Move": "Aller", "Type": "Taper", "good, -58 dBm": "bon, -58 dBm", "not set": "aucun", "show": "afficher", "hide": "masquer"}
class Lang:
	def __init__ (s, code = "en"): s.code = code
	def __call__ (s, w): return FR.get (w, w) if s.code == "fr" else w

# ---- the screen ----------------------------------------------------------------------------------------------------------
class Scr (v3.Scr):
	def __init__ (s, W, H, k, lang = "en", theme = "blue"):
		v3.Scr.__init__ (s, W, H, k, "en", theme); s.tr = Lang (lang); s.lang = lang
		c = s.c
		s.m.update (
			px = 22 if c else 110,				# the parent column's icons (the column row slid there too)
			plab = 11 if c else 17, pmax = 0 if c else 232,	# its labels (none when compact)
			rx = 76 if c else 420,				# the applet's rows: the icons
			rdy = 30 if c else 54,				# a row's height (the others)
			vr = 620 if c else 1220,			# the values' right edge (no right panel)
			vr2 = 410 if c else 790,			# ... with a right panel
			panx = 424 if c else 830, panw = 200 if c else 400)

# ---- the column row of the home (as built: the consoles that have ROMs, then Onyx, Apps, Settings) ----------------------------
def columns ():
	c = []
	for name, _, _ in v2.SYSTEMS:
		code = v2.SHORT.get (name, name)
		if v2.ROMS.get (code): c.append ((code, name, ("console", code)))
	return c + [("onyx", "Onyx", "pad"), ("apps", "Apps", "grid"), ("settings", "Settings", "gear")]

APPLETS = [	# icon (glyph), name, the desktop applet (its .lnk help line), the console's own help line
	("speaker", "Sound", "soundconf", "The output, the volume, mute, a test sound, the programs playing"),
	("pad", "Gamepad", "padconf", "The pads plugged in, their buttons mapped, the keyboard as pad 1"),
	("kbd", "Keyboard & Mouse", "keyconf", "The keyboard's layout; the mouse wheel's speed"),
	("globe", "Language & Region", "langconf", "The language of the programs; the time zone"),
	("wifi", "Wi-Fi", "wpaconf", "The networks around: join one, forget one, the country"),
	("box", "Packages", "pkgman", "Updates, the apps installed, more apps to install"),
	("modes", "Mode", "modeconf", "The interface: desktop, pocket or console"),
	("monitor", "Display", "displayconf", "The screen's resolution; the games' own resolutions"),
]
FR_APPLET_HELP = {
	"Sound": "La sortie, le volume, la sourdine, un son d'essai, les programmes qui jouent",
	"Wi-Fi": "Les réseaux autour : en rejoindre un, en oublier un, le pays"}

# ---- drawing the deeper level ----------------------------------------------------------------------------------------------
def title (img, scr, t, sub = None):
	L, M = scr.L, scr.M
	ft = font (M ("title"), True); x = M ("tx"); y = M ("ty")
	text (img, x, y, t, ft, WHITE)
	if sub: text (img, x + tw (t, ft) + L (14), y + (M ("title") - M ("count")) * 0.75, sub, font (M ("count")), (200, 214, 240))
	fc = font (M ("title") * 0.85); xr = scr.W - M ("tx")
	text_r (img, xr, y, M ("title") + L (4), "21:07", fc, WHITE); xr -= tw ("21:07", fc) + L (14)
	if not scr.c: text_r (img, xr, y, M ("title") + L (4), "jeu. 9 oct." if scr.lang == "fr" else "Thu 9 Oct", font (M ("count")), (200, 214, 240))

def col_row (img, scr, f_key, x_focus, alpha = 1.0, label = True):
	"""The home's column row, its chosen column at x_focus (the home: FX; a deeper level: slid left)."""
	L, M = scr.L, scr.M; cols = columns (); f = [c[0] for c in cols].index (f_key); CY = M ("CY")
	for i, (key, name, ic) in enumerate (cols):
		if i == f: x = x_focus
		elif i > f: x = x_focus + M ("big") // 2 + L (scr.m["spr"] - scr.m["big"] // 2) + (i - f - 1) * M ("spr")
		else: x = x_focus - (f - i) * M ("spl")
		size = M ("big") if i == f else M ("small")
		if x < -size or x > scr.W + size: continue
		white_icon (img, ic, x, CY, size, 255 * alpha if i == f else 110 * alpha)
	if label:
		fl = font (M ("catlab"), True); n = scr.tr (cols[f][1])
		text_c (img, x_focus - L (120), CY + M ("big") // 2 + L (4), L (240), M ("catlab") + L (6), n, fl, mix ((40, 80, 150), WHITE, alpha))

def icon_any (img, scr, ic, cx, cy, size, alpha):
	if isinstance (ic, str) and ic.startswith ("app:"):
		im = app_icon (ic[4:], int (size))
		if alpha < 255: im = im.copy (); im.putalpha (im.split ()[3].point (lambda v: v * alpha // 255))
		img.paste (im, (int (cx - size / 2), int (cy - size / 2)), im)
	elif ic: white_icon (img, ic, cx, cy, size, alpha)

def parent_col (img, scr, items, f, x = None, alpha = 120):
	"""The parent level, faded at the left: its icons, its labels small (the focused one a little larger)."""
	L, M = scr.L, scr.M; x = M ("px") if x is None else x; FY = M ("FY")
	for j, (ic, lab) in enumerate (items):
		if j == f: y = FY; size = M ("oi") + L (6); a = alpha + 40
		elif j > f: y = FY + M ("gap_below") - L (16) + (j - f - 1) * L (scr.m["dy"] - 18); size = M ("oi") - L (6); a = alpha - 40
		else: y = FY - M ("gap_below") + L (16) - (f - j - 1) * L (scr.m["dy"] - 18); size = M ("oi") - L (6); a = alpha - 50
		if y < M ("CY") + M ("big") // 2 + L (30) and j != f: continue
		if y > scr.H - M ("bar") - L (14): continue
		icon_any (img, scr, ic, x, y, size, a)
		if M ("pmax") > 0:
			fl = font (M ("plab") + (L (2) if j == f else 0), j == f)
			text_l (img, x + size // 2 + L (12), y - L (11), L (22), ellipsize (scr.tr (lab), fl, M ("pmax")), fl, mix ((40, 80, 150), WHITE, a / 255))

def seg_bar (img, scr, xr, cy, v, n, w_seg = None, lit = True):
	L = scr.L; w_seg = w_seg or L (14 if not scr.c else 9); g = L (4 if not scr.c else 3); h = L (12 if not scr.c else 9)
	x0 = xr - n * (w_seg + g) + g
	for i in range (n):
		rrect (img, x0 + i * (w_seg + g), cy - h // 2, w_seg, h, L (3), (WHITE if lit else SUBC) if i < v else (90, 120, 180), alpha = 255 if i < v else 150)
	return x0

def toggle (img, scr, xr, cy, on, alpha = 255):
	L = scr.L; w = L (44 if not scr.c else 32); h = L (24 if not scr.c else 18)
	x = xr - w; rrect (img, x, cy - h // 2, w, h, h / 2, (90, 200, 120) if on else (80, 100, 150), alpha = alpha)
	d = h - L (6); put (img, x + (w - d - L (3) if on else L (3)), cy - d // 2, mask (d, d, lambda dd, s: dd.ellipse ([0, 0, d * s - 1, d * s - 1], fill = 255)), WHITE, alpha)
	return x

def signal (img, scr, xr, cy, n, alpha = 255):
	L = scr.L; bw = L (5 if not scr.c else 4); g = L (3); x = xr - 4 * (bw + g) + g
	for i in range (4):
		h = L ((6 + i * 4) if not scr.c else (4 + i * 3))
		rrect (img, x + i * (bw + g), cy + L (8 if not scr.c else 6) - h, bw, h, L (1.5), WHITE if i < n else (90, 120, 180), alpha = alpha if i < n else 160)
	return x

def rows (img, scr, items, f, x_icon = None, vr = None, sub_override = None):
	"""The applet's settings. A row: dict (ic, label, sub, kind, val ...). kind: choice (val; < > when focused),
	slider (v, n, txt), toggle (on), action, menu (val, a chevron), info (val), net (bars, lock, val), radio (on)."""
	L, M, tr = scr.L, scr.M, scr.tr
	cx = M ("rx") if x_icon is None else x_icon; FY = M ("FY"); vr = vr or M ("vr")
	lx = cx + M ("lx")
	for j, r in enumerate (items):
		foc = j == f
		if foc: y = FY; size = M ("fi"); a = 255; fl = font (M ("flab"), True)
		elif j > f:
			y = FY + M ("gap_below") + (j - f - 1) * M ("rdy"); size = M ("oi"); a = 160; fl = font (M ("olab"))
			if y > scr.H - M ("bar") - L (16): continue
		else:
			y = FY - M ("gap_below") - (f - j - 1) * M ("rdy"); size = M ("oi"); a = 150 - 22 * (f - j - 1); fl = font (M ("olab"))
			if y < M ("ty") + M ("title") + L (30): continue
		if r.get ("sep"):					# a small heading between groups
			text_l (img, lx, y - L (12), L (24), tr (r["sep"]), font (M ("sub"), True), mix ((40, 80, 150), WHITE, 0.55)); continue
		icon_any (img, scr, r.get ("ic"), cx, y, size, a)
		col = mix ((40, 80, 150), WHITE, a / 255)
		k = r.get ("kind", "action"); vx = vr
		fv = font (M ("olab") * (1.0 if not foc else 1.05), foc)
		cyv = y - (L (2) if foc else 0)
		# the value, right-aligned
		if k == "choice":
			v = r["val"]
			if foc:
				ca = L (14); x2 = vr - ca
				put (img, x2, cyv - ca // 2, mask (ca, ca, lambda d, s: g_chev (d, s, ca / 16, "right")), WHITE)
				x1 = x2 - L (10) - tw (v, fv); text_l (img, x1, cyv - L (12), L (24), v, fv, WHITE)
				put (img, x1 - L (10) - ca, cyv - ca // 2, mask (ca, ca, lambda d, s: g_chev (d, s, ca / 16, "left")), WHITE)
				vx = x1 - L (10) - ca
			else: vx = vr - tw (v, fv); text_l (img, vx, cyv - L (11), L (22), v, fv, col)
		elif k == "slider":
			t = r.get ("txt", "%d" % r["v"]); ft = font (M ("olab"), foc)
			text_l (img, vr - tw (t, ft), cyv - L (11), L (22), t, ft, WHITE if foc else col)
			vx = seg_bar (img, scr, vr - tw (t, ft) - L (14), cyv, r["v"], r["n"], lit = foc)
			if foc and r.get ("arrows"):
				ca = L (14); put (img, vx - L (8) - ca, cyv - ca // 2, mask (ca, ca, lambda d, s: g_chev (d, s, ca / 16, "left")), (150, 170, 210))
				put (img, vr + L (6), cyv - ca // 2, mask (ca, ca, lambda d, s: g_chev (d, s, ca / 16, "right")), WHITE)
		elif k == "toggle":
			vx = toggle (img, scr, vr, cyv, r["on"], a)
			if r.get ("val"): t = r["val"]; vx -= L (10) + tw (t, fv); text_l (img, vx, cyv - L (11), L (22), t, fv, col)
		elif k in ("menu", "info"):
			v = r.get ("val", "")
			if k == "menu":
				ca = L (14); put (img, vr - ca, cyv - ca // 2, mask (ca, ca, lambda d, s: g_chev (d, s, ca / 16, "right")), col); vx = vr - ca - L (10)
			else: vx = vr
			if v:
				colv = (r.get ("vcol") or (col if not foc else WHITE))
				vx -= tw (v, fv); text_l (img, vx, cyv - L (11), L (22), v, fv, colv)
		elif k == "net":
			vx = signal (img, scr, vr, cyv, r["bars"], a)
			if r.get ("lock"): ls = L (14 if not scr.c else 11); vx -= ls + L (10); white_icon (img, "lock", vx + ls / 2, cyv, ls, a)
			if r.get ("val"): t = r["val"]; vx -= L (12) + tw (t, fv); text_l (img, vx, cyv - L (11), L (22), t, fv, (r.get ("vcol") or col))
		elif k == "radio":
			d_ = L (18 if not scr.c else 14); vx = vr - d_
			ring (img, vx, cyv - d_ // 2, d_, d_, d_ / 2, col, t = L (2))
			if r["on"]: dd = d_ - L (8); rrect (img, vx + L (4), cyv - dd // 2, dd, dd, dd / 2, WHITE)
			if r.get ("val"): t = r["val"]; vx -= L (12) + tw (t, fv); text_l (img, vx, cyv - L (11), L (22), t, fv, (r.get ("vcol") or col))
		elif k == "progress":
			bw = L (180 if not scr.c else 90); h = L (8); x0 = vr - bw
			rrect (img, x0, cyv - h // 2, bw, h, h / 2, (60, 90, 150)); rrect (img, x0, cyv - h // 2, int (bw * r["pct"] / 100), h, h / 2, GREEN)
			t = r.get ("val", "%d %%" % r["pct"]); vx = x0 - L (12) - tw (t, fv); text_l (img, vx, cyv - L (11), L (22), t, fv, WHITE if foc else col)
		# the label, and the focused row's help line
		maxw = vx - lx - L (20)
		lab = r["label"]
		text_l (img, lx, y - (L (14) if foc else L (11)), L (24) if foc else L (22), ellipsize (lab, fl, maxw), fl, WHITE if foc else col)
		if foc:
			s_ = sub_override if sub_override is not None else r.get ("sub")
			if s_: text (img, lx + L (1), y + L (10), ellipsize (s_, font (M ("sub")), (vr if k in ("action",) else vr) - lx), font (M ("sub")), SUBC)

def applet_page (scr, applet, theme = "blue"):
	"""The deeper level of an applet: the background, the column row slid left, the applets faded as the parent."""
	img = background (scr)
	col_row (img, scr, "settings", scr.M ("px"), alpha = 0.55, label = False)
	names = [a[1] for a in APPLETS]; f = names.index (applet)
	parent_col (img, scr, [(a[0], a[1]) for a in APPLETS], f)
	return img

def panel (img, scr, x, y, w, h, alpha = 70):
	L = scr.L
	rrect (img, x, y, w, h, L (12), (6, 14, 40), alpha = alpha + 60); ring (img, x, y, w, h, L (12), WHITE, alpha = 50)

# ---- the pad drawing (generic: a twin-grip pad, the buttons by place) --------------------------------------------------------------
def draw_pad (img, scr, cx, cy, w, lit = (), done = (), ask = None):
	"""A generic pad seen from the top: lit = buttons held now; done = learnt (filled); ask = the one asked (glowing)."""
	L = scr.L; h = int (w * 0.58); x0, y0 = cx - w // 2, cy - h // 2
	body = mask (w, h, lambda d, s: (d.rounded_rectangle ([w * 0.08 * s, 0, w * 0.92 * s, h * 0.62 * s], h * 0.25 * s, fill = 255),
					  d.ellipse ([0, h * 0.2 * s, w * 0.36 * s, h * 0.97 * s], fill = 255), d.ellipse ([w * 0.64 * s, h * 0.2 * s, w * s - 1, h * 0.97 * s], fill = 255)))
	put (img, x0, y0, body, (30, 50, 100), 200);
	from PIL import ImageChops
	edge = ImageChops.subtract (body, body.filter (ImageFilter.MinFilter (3))); put (img, x0, y0, edge, WHITE, 170)
	u = w / 100
	def btn (name, bx, by, r, shape = "o", label = None):
		X, Y = x0 + bx * u, y0 + by * u; R = r * u
		state = "ask" if name == ask else "lit" if name in lit else "done" if name in done else "off"
		if state == "ask":
			m_ = Image.new ("L", img.size, 0); ImageDraw.Draw (m_).ellipse ([X - R - L (6), Y - R - L (6), X + R + L (6), Y + R + L (6)], fill = 255)
			img.paste (add_glow (img, ACC, m_, L (10), 1.2))
		fillc = {"ask": WHITE, "lit": ACC, "done": (170, 196, 240), "off": None}[state]
		if shape == "o":
			if fillc: rrect (img, X - R, Y - R, 2 * R, 2 * R, R, fillc)
			ring (img, X - R, Y - R, 2 * R, 2 * R, R, WHITE, t = L (1.6), alpha = 220)
		else:
			if fillc: rrect (img, X - R * 1.6, Y - R * 0.6, R * 3.2, R * 1.2, R * 0.5, fillc)
			ring (img, X - R * 1.6, Y - R * 0.6, R * 3.2, R * 1.2, R * 0.5, WHITE, t = L (1.4), alpha = 220)
		if label:
			fb = font (max (7, R * 1.0), True); text_c (img, X - R, Y - R - L (1), 2 * R, 2 * R, label, fb, (20, 30, 60) if fillc in (WHITE, (170, 196, 240)) else WHITE)
	# the d-pad
	for name, bx, by in [("up", 21, 15), ("down", 21, 33), ("left", 12, 24), ("right", 30, 24)]:
		X, Y = x0 + bx * u, y0 + by * u; s_ = 4.2 * u
		st = "ask" if name == ask else "lit" if name in lit else "done" if name in done else "off"
		c_ = {"ask": WHITE, "lit": ACC, "done": (170, 196, 240), "off": None}[st]
		if c_: rrect (img, X - s_, Y - s_, 2 * s_, 2 * s_, L (3), c_)
		ring (img, X - s_, Y - s_, 2 * s_, 2 * s_, L (3), WHITE, t = L (1.4), alpha = 220)
	btn ("y", 79, 14, 4.4, label = "Y"); btn ("x", 70, 24, 4.4, label = "X"); btn ("b", 88, 24, 4.4, label = "B"); btn ("a", 79, 34, 4.4, label = "A")
	btn ("select", 42, 20, 2.6, "p"); btn ("start", 58, 20, 2.6, "p"); btn ("home", 50, 31, 3.6, label = "")
	btn ("l3", 33, 47, 6.5); btn ("r3", 67, 47, 6.5)
	# the shoulders, above
	for name, bx, lab in [("l", 20, "L1"), ("r", 80, "R1")]:
		X, Y = x0 + bx * u, y0 - 4 * u; st = "ask" if name == ask else "lit" if name in lit else "done" if name in done else "off"
		c_ = {"ask": WHITE, "lit": ACC, "done": (170, 196, 240), "off": None}[st]
		if st == "ask":
			m_ = Image.new ("L", img.size, 0); ImageDraw.Draw (m_).rounded_rectangle ([X - 13 * u, Y - 5 * u, X + 13 * u, Y + 5 * u], L (6), fill = 255)
			img.paste (add_glow (img, ACC, m_, L (10), 1.2))
		if c_: rrect (img, X - 11 * u, Y - 3 * u, 22 * u, 6 * u, L (5), c_)
		ring (img, X - 11 * u, Y - 3 * u, 22 * u, 6 * u, L (5), WHITE, t = L (1.4), alpha = 220)
		text_c (img, X - 11 * u, Y - 3 * u - L (1), 22 * u, 6 * u, lab, font (max (7, 4 * u), True), (20, 30, 60) if c_ in (WHITE, (170, 196, 240)) else WHITE)

# ---- the dialog and the virtual keyboard ------------------------------------------------------------------------------------
def dialog (img, scr, title_, lines, buttons, sel, extra = None):
	"""The XMB's message: a dark box centred, its title, its words, its choices in a row (the chosen one white)."""
	L = scr.L; w = L (560 if not scr.c else 400); fl = font (L (16 if not scr.c else 12))
	h = L (96) + len (lines) * L (24) + L (66)
	x = (scr.W - w) // 2; y = (scr.H - h) // 2 - L (10)
	img = dim (img, 130, (2, 8, 30))
	sh = Image.new ("L", img.size, 0); ImageDraw.Draw (sh).rounded_rectangle ([x, y + L (10), x + w, y + h + L (10)], L (16), fill = 200)
	img.paste ((0, 0, 0), (0, 0), sh.filter (ImageFilter.GaussianBlur (L (16))))
	rrect (img, x, y, w, h, L (16), ((26, 54, 116), (12, 26, 66))); ring (img, x, y, w, h, L (16), WHITE, alpha = 90)
	text (img, x + L (28), y + L (22), title_, font (L (22 if not scr.c else 16), True), WHITE)
	yy = y + L (62)
	for ln in lines: text (img, x + L (28), yy, ln, fl, SUBC); yy += L (24)
	if extra: extra (img, x, yy, w)
	bw = L (150); bh = L (42); gx = L (16); bx = x + w - L (28) - len (buttons) * bw - (len (buttons) - 1) * gx; by = y + h - bh - L (22)
	for i, b in enumerate (buttons):
		X = bx + i * (bw + gx)
		if i == sel:
			m_ = Image.new ("L", img.size, 0); ImageDraw.Draw (m_).rounded_rectangle ([X, by, X + bw, by + bh], bh / 2, fill = 255)
			img.paste (add_glow (img, ACC, m_, L (10), 0.8))
			rrect (img, X, by, bw, bh, bh / 2, WHITE); text_c (img, X, by, bw, bh, b, font (L (17), True), (16, 40, 96))
		else:
			rrect (img, X, by, bw, bh, bh / 2, WHITE, alpha = 30); ring (img, X, by, bw, bh, bh / 2, WHITE, alpha = 120)
			text_c (img, X, by, bw, bh, b, font (L (17)), WHITE)
	return img

QWERTY = ["1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm,._"]
AZERTY = ["1234567890", "azertyuiop", "qsdfghjklm", "wxcvbn,.-_"]
SYMS   = ["!@#$%^&*()", "~`=+[]{}\\|", ";:'\"<>/?€£", "§¨°²µ«»¿¡ "]
def vkeyboard (img, scr, field_label, value, secret = False, shown = False, layout = QWERTY, sel = (1, 4), shift = False,
	       caret = None, page = "abc"):
	"""The pad's keyboard over the dimmed applet: the field being edited at the top, a grid of keys, the special row."""
	L = scr.L; tr = scr.tr; c = scr.c
	img = dim (img, 120, (2, 8, 30))
	kw = L (56 if not c else 40); kh = L (48 if not c else 34); g = L (8 if not c else 5)
	cols = 10; gw = cols * kw + (cols - 1) * g
	pw = gw + L (48 if not c else 24); ph = L (78 if not c else 56) + 5 * kh + 4 * g + L (30 if not c else 16)
	px = (scr.W - pw) // 2; py = scr.H - scr.M ("bar") - ph - L (12 if not c else 6)
	sh = Image.new ("L", img.size, 0); ImageDraw.Draw (sh).rounded_rectangle ([px, py + L (8), px + pw, py + ph + L (8)], L (18), fill = 200)
	img.paste ((0, 0, 0), (0, 0), sh.filter (ImageFilter.GaussianBlur (L (14))))
	rrect (img, px, py, pw, ph, L (18), ((24, 50, 110), (10, 22, 58))); ring (img, px, py, pw, ph, L (18), WHITE, alpha = 80)
	# the field
	fx = px + L (24 if not c else 12); fy = py + L (14 if not c else 8); fw = pw - 2 * (fx - px); fh = L (44 if not c else 32)
	text (img, fx, fy - L (2), field_label, font (L (13 if not c else 10), True), SUBC)
	fy += L (20 if not c else 14)
	rrect (img, fx, fy, fw, fh, L (8), (4, 12, 36), alpha = 220); ring (img, fx, fy, fw, fh, L (8), ACC, t = L (2))
	ft = font (L (20 if not c else 14), False)
	shown_txt = ("•" * len (value)) if (secret and not shown) else value
	if secret and not shown and value:				# the last letter typed shown a moment
		shown_txt = "•" * (len (value) - 1) + value[-1]
	text_l (img, fx + L (14), fy, fh, shown_txt, ft, WHITE)
	cx_ = fx + L (14) + tw (shown_txt[: (caret if caret is not None else len (shown_txt))], ft) + L (2)
	box (img, cx_, fy + L (8), L (2), fh - L (16), WHITE)
	if secret:						# show / hide, at the field's right
		es = L (18 if not c else 14); ex = fx + fw - L (14) - es
		white_icon (img, "eye", ex + es / 2, fy + fh / 2, es, 255 if shown else 140)
		t = tr ("Show password") if not c else ""
		if t: text_r (img, ex - L (8), fy, fh, "L3 : " + tr ("hide" if shown else "show") if scr.lang == "fr" else "L3: " + ("hide" if shown else "show"), font (L (12)), SUBC)
	# the keys
	gx0 = px + (pw - gw) // 2; gy0 = fy + fh + L (16 if not c else 10)
	fk = font (L (20 if not c else 14))
	grid = SYMS if page == "sym" else layout
	for r, row in enumerate (grid):
		for i, ch in enumerate (row):
			x = gx0 + i * (kw + g); y = gy0 + r * (kh + g)
			label = ch.upper () if shift and ch.isalpha () else ch
			foc = (r, i) == sel
			if foc:
				m_ = Image.new ("L", img.size, 0); ImageDraw.Draw (m_).rounded_rectangle ([x, y, x + kw, y + kh], L (8), fill = 255)
				img.paste (add_glow (img, ACC, m_, L (10), 0.9))
				rrect (img, x, y, kw, kh, L (8), WHITE); text_c (img, x, y, kw, kh, label, font (L (22 if not c else 15), True), (16, 40, 96))
			else:
				rrect (img, x, y, kw, kh, L (8), WHITE, alpha = 26); ring (img, x, y, kw, kh, L (8), WHITE, alpha = 60)
				text_c (img, x, y, kw, kh, label, fk, WHITE)
	# the special row: Shift (Y) | ?123 (Y twice) | Space (X) | < > (L1 R1) | Delete (B) | Done (Start)
	y = gy0 + 4 * (kh + g)
	specials = [(2, "Shift" if page == "abc" else "abc", "Y"), (2, "?123" if page == "abc" else "abc", None), (3, "Space", "X"),
		    (1, "<", "L1"), (1, ">", "R1"), (2, "Delete", "B")]
	x = gx0; fs_ = font (L (14 if not c else 10), True); fsb = font (L (10 if not c else 8))
	units = [s_[0] for s_ in specials] + [2]; unit_w = (gw - (len (units) - 1) * g) / sum (units)
	for n, lab, btn in specials + [(2, "Done", "Start")]:
		w_ = int (round (unit_w * n + g * (n - 1))) if False else int (round (unit_w * n))
		on = (lab == "Shift" and shift)
		focus_k = sel == (4, lab)
		if focus_k:
			rrect (img, x, y, w_, kh, L (8), WHITE)
		elif lab == "Done":
			rrect (img, x, y, w_, kh, L (8), ((90, 160, 255), (40, 100, 210)))
		else:
			rrect (img, x, y, w_, kh, L (8), WHITE, alpha = 50 if on else 16); ring (img, x, y, w_, kh, L (8), WHITE, alpha = 60)
		shown_lab = tr (lab) if lab not in ("<", ">") else lab
		colt = (16, 40, 96) if focus_k else WHITE
		if btn and not c:
			text_c (img, x, y - L (7), w_, kh, shown_lab, fs_, colt); text_c (img, x, y + L (11), w_, kh, btn, fsb, SUBC if not focus_k else (60, 90, 150))
		else: text_c (img, x, y, w_, kh, shown_lab, fs_, colt)
		x += w_ + g
	return img

# ---- the screens ------------------------------------------------------------------------------------------------------------
def save (img, name):
	p = os.path.join (OUT, name); img.save (p); print ("wrote", os.path.relpath (p, ROOT), img.size)

def settings_column (W = 1280, H = 720, k = 1, f = 0, lang = "en"):
	"""The home on the Settings column: the eight console applets as items, their help lines."""
	scr = Scr (W, H, k, lang); tr = scr.tr
	img = background (scr)
	title (img, scr, tr ("Settings"), "8 éléments" if lang == "fr" else "8 items")
	col_row (img, scr, "settings", scr.M ("FX"))
	items = [dict (icon = g, label = tr (n), sub = (FR_APPLET_HELP.get (n, h) if lang == "fr" else h)) for g, n, a, h in APPLETS]
	v3.item_list (img, scr, items, f, max_label = scr.W - scr.M ("FX") - scr.M ("lx") - scr.L (60))
	hints (img, scr, [("L1/R1", "Category"), ("Home", "Menu"), ("B", "Back"), ("A", "Open")], left = "Onyx  -  " + tr ("Settings"))
	return img

def sound (W = 1280, H = 720, k = 1, f = 1, step = False):
	scr = Scr (W, H, k); img = applet_page (scr, "Sound")
	title (img, scr, "Sound", "Settings")
	R = [dict (ic = "speaker", label = "Play on", kind = "choice", val = "HDMI (the screen)", sub = "Automatic, HDMI, the headphone jack, a USB headset: the ones plugged in"),
	     dict (ic = "bars", label = "Volume", kind = "slider", v = 8 if step else 7, n = 10, txt = "8 / 10" if step else "7 / 10", arrows = step,
		   sub = "Left / Right: a step; applied at once to everything that plays" if not step else "8 / 10 -- applied at once, kept in SD:/etc/sound.ini (volume=8)"),
	     dict (ic = "mute", label = "Mute", kind = "toggle", on = False, sub = "Everything silent; the volume kept"),
	     dict (ic = "play", label = "Play a test sound", kind = "action", sub = "A short chime (when no app holds the output)"),
	     dict (sep = "Programs playing"),
	     dict (ic = "app:media", label = "Media Player", kind = "slider", v = 9, n = 10, txt = "90 %", sub = "Its own volume, 0 to 100 % by tens (SD:/etc/mixer.ini)"),
	     dict (ic = "app:gbaemu", label = "Game Boy Advance", kind = "slider", v = 6, n = 10, txt = "60 %")]
	rows (img, scr, R, f)
	if step:						# the step taken: a small level flash at the top right
		bx = scr.W - scr.L (300); by = scr.L (70)
		panel (img, scr, bx, by, scr.L (250), scr.L (54), 90)
		white_icon (img, "speaker", bx + scr.L (30), by + scr.L (27), scr.L (24))
		seg_bar (img, scr, bx + scr.L (232), by + scr.L (27), 8, 10)
	hints (img, scr, [("lr", "Change"), ("ud", "Item"), ("B", "Back"), ("A", "OK")], left = "Onyx  -  SD:/etc/sound.ini, mixer.ini")
	return img

def gamepad (W = 1280, H = 720, k = 1):
	scr = Scr (W, H, k); img = applet_page (scr, "Gamepad"); L = scr.L
	title (img, scr, "Gamepad", "Settings")
	R = [dict (ic = "pad", label = "Pad 1", kind = "menu", val = "USB pad 045e:028e", sub = "Known to Circle; its mapping: its own"),
	     dict (ic = "pad", label = "Map the buttons", kind = "action", sub = "Press each button when asked: 17 steps"),
	     dict (ic = "pad", label = "Pad 2", kind = "info", val = "nothing plugged in"),
	     dict (ic = "kbd", label = "Keyboard as pad 1", kind = "menu", val = "21 keys"),
	     dict (ic = "trash", label = "Forget this pad's mapping", kind = "action"),
	     dict (ic = "refresh", label = "Read gamepad.ini again", kind = "action")]
	rows (img, scr, R, 1, vr = scr.M ("vr2"))
	# the right panel: what the apps see, live
	x, y, w = scr.M ("panx"), L (250), scr.M ("panw")
	panel (img, scr, x, y, w, L (330))
	text (img, x + L (20), y + L (16), "What the apps see (pad 1), live", font (L (15), True), WHITE)
	draw_pad (img, scr, x + w // 2, y + L (170), L (300), lit = ("a", "right"))
	text (img, x + L (20), y + L (290), "Held now: A, Right   -   axes 2, hats 1", font (L (13)), SUBC)
	hints (img, scr, [("ud", "Item"), ("B", "Back"), ("A", "OK")], left = "Onyx  -  SD:/etc/gamepad.ini")
	return img

def gamepad_map (W = 1280, H = 720, k = 1):
	scr = Scr (W, H, k); img = applet_page (scr, "Gamepad"); L = scr.L
	img = dim (img, 150, (2, 8, 30))
	title (img, scr, "Map the buttons", "Pad 1  -  USB gamepad 045e:028e")
	cx = scr.W // 2
	text_c (img, 0, L (88), scr.W, L (30), "Step 6 of 17", font (L (16)), SUBC)
	text_c (img, 0, L (120), scr.W, L (40), "Press the RIGHT face button", font (L (30), True), WHITE)
	text_c (img, 0, L (166), scr.W, L (26), "(the one at the right of the four: it is B in the apps)", font (L (16)), SUBC)
	draw_pad (img, scr, cx, L (400), L (460), done = ("up", "down", "left", "right", "a"), ask = "b")
	# the progress: a dot per step
	n = 17; d = L (10); g = L (8); x0 = cx - (n * d + (n - 1) * g) // 2; y = L (560)
	for i in range (n):
		c_ = WHITE if i < 5 else ACC if i == 5 else (80, 100, 150)
		rrect (img, x0 + i * (d + g), y, d, d, d / 2, c_)
	text_c (img, 0, L (586), scr.W, L (24), "Its rest is read first: release every button. Nothing pressed for 8 s: skipped.", font (L (14)), SUBC)
	hints (img, scr, [("Hold Home", "Cancel"), ("Select", "Skip (it has none)")], left = "Onyx  -  the pad's buttons cannot steer while they are learnt")
	return img

def keyboard (W = 1280, H = 720, k = 1):
	scr = Scr (W, H, k); img = applet_page (scr, "Keyboard & Mouse")
	title (img, scr, "Keyboard & Mouse", "Settings")
	R = [dict (ic = "kbd", label = "Keyboard layout", kind = "choice", val = "Belgian (azerty)", sub = "Taken at once; set at every boot (SD:/etc/autostart: keyb BE)  -  8 layouts (SD:/etc/keymaps)"),
	     dict (ic = "kbd", label = "Try it", kind = "menu", val = "a physical keyboard", sub = "A field to type in with the keyboard plugged in"),
	     dict (sep = "Mouse"),
	     dict (ic = "mouse", label = "Wheel: lines a notch scrolls", kind = "slider", v = 3, n = 16, txt = "3", sub = "SD:/etc/theme.txt  wheelspeed=3")]
	rows (img, scr, R, 0)
	hints (img, scr, [("lr", "Change"), ("ud", "Item"), ("B", "Back"), ("A", "OK")], left = "Onyx  -  SD:/etc/keymaps/BE.kmap")
	return img

def language (W = 1280, H = 720, k = 1):
	scr = Scr (W, H, k); img = applet_page (scr, "Language & Region")
	title (img, scr, "Language & Region", "Settings")
	R = [dict (ic = "globe", label = "Language", kind = "choice", val = "English", sub = "English, Français  -  the programs started from now on speak it (system.ini language=)"),
	     dict (ic = "pin", label = "Time zone", kind = "choice", val = "Brussels  (UTC+2, summer time)", sub = "23 cities  -  the clock follows at once (system.ini zone=, timezone=)"),
	     dict (ic = "clock", label = "Now", kind = "info", val = "Thu 9 Oct 2026  21:07")]
	rows (img, scr, R, 1)
	hints (img, scr, [("lr", "Change"), ("ud", "Item"), ("B", "Back"), ("A", "OK")], left = "Onyx  -  SD:/etc/system.ini")
	return img

NETS = [("Maison", 4, True, "Connected"), ("Maison-5G", 3, True, None), ("Voisin", 2, True, None), ("FreeWifi", 1, False, "open"), ("Livebox-1280", 1, True, None)]
def wifi (W = 1280, H = 720, k = 1, lang = "en"):
	scr = Scr (W, H, k, lang); img = applet_page (scr, "Wi-Fi"); c = scr.c
	title (img, scr, "Wi-Fi", "Settings" if lang == "en" else "Réglages")
	R = [dict (ic = "wifi", label = n, kind = "net", bars = b, lock = lk, val = v, vcol = GREEN if v == "Connected" else None,
		   sub = ("WPA2  -  good signal (-58 dBm)  -  A: its page" if n == "Maison-5G" else "")) for n, b, lk, v in NETS]
	R[0]["ic"] = "check"
	R += [dict (ic = "refresh", label = "Scan again", kind = "info", val = "5 found, 3 s ago"),
	      dict (ic = "hidden", label = "A hidden network...", kind = "menu"),
	      dict (ic = "flag", label = "Country", kind = "choice", val = "BE")]
	rows (img, scr, R, 1)
	hints (img, scr, [("ud", "Item"), ("Y", "Scan"), ("B", "Back"), ("A", "OK")] if not c else [("ud", "Item"), ("B", "Back"), ("A", "OK")],
	       left = None if c else "Onyx  -  connected to Maison, 192.168.1.23")
	return img

def wifi_network (scr, typed = "", shown = False):
	"""A network's page: the deeper level under Wi-Fi (the networks faded at the left)."""
	tr = scr.tr
	img = background (scr)
	parent_col (img, scr, [("wifi", n) for n, _, _, _ in NETS], 1)
	title (img, scr, "Maison-5G", "Wi-Fi")
	R = [dict (ic = "check", label = tr ("Connect"), kind = "action", sub = "Joins it now; kept in SD:/etc/wpa_supplicant.conf"),
	     dict (ic = "lock", label = tr ("Password"), kind = "menu", val = ("•" * len (typed)) if typed else tr ("not set"), sub = "8 to 63 characters"),
	     dict (ic = "eye", label = tr ("Show password"), kind = "toggle", on = shown),
	     dict (ic = "info", label = tr ("Security"), kind = "info", val = "WPA2-PSK"),
	     dict (ic = "wifi", label = tr ("Signal"), kind = "info", val = tr ("good, -58 dBm")),
	     dict (ic = "trash", label = tr ("Forget this network"), kind = "action")]
	rows (img, scr, R, 1)
	return img

def wifi_password (W = 1280, H = 720, k = 1, lang = "en"):
	scr = Scr (W, H, k, lang); tr = scr.tr
	img = wifi_network (scr, "tortue-bleue")
	img = vkeyboard (img, scr, ("Password for Maison-5G" if lang == "en" else "Mot de passe de Maison-5G"), "tortue-bleu",
			 secret = True, layout = AZERTY if lang == "fr" else QWERTY, sel = (1, 2) if lang == "en" else (1, 2))
	hints (img, scr, [("dpad", "Move"), ("A", "Type"), ("B", "Delete"), ("X", "Space"), ("Y", "Shift"), ("L1/R1", "Cursor"), ("Start", "Done")])
	return img

PKGS = [("snesemu", "Super Nintendo", "1.0.40", "1.0.41", "The emulator: a faster mode 7", None),
	("media", "Media Player", "2.2.0", "2.2.1", "Subtitles in the films", 42),
	("onyx", "Onyx (the system)", "2026.10.148", "2026.10.151", "The kernel and the card: restart to finish", None)]
def packages (W = 1280, H = 720, k = 1):
	scr = Scr (W, H, k); img = applet_page (scr, "Packages")
	title (img, scr, "Packages", "Settings")
	R = [dict (ic = "download", label = "Install the 3 updates", kind = "action", sub = "One after the other; the system's waits for a restart"),
	     dict (sep = "Updates")]
	for name, t, a, b, s, pct in PKGS:
		ic = "app:" + name if os.path.exists (os.path.join (SD, "apps", name + ".app")) else "box"
		if pct is not None: R.append (dict (ic = ic, label = t, kind = "progress", pct = pct, val = "Installing 42 %", sub = s))
		else: R.append (dict (ic = ic, label = t, kind = "info", val = "%s  >  %s" % (a, b), vcol = AMBER if name == "onyx" else None, sub = s))
	R += [dict (ic = "box", label = "Installed", kind = "menu", val = "64 packages"),
	      dict (ic = "download", label = "Available", kind = "menu", val = "12 more"),
	      dict (ic = "search", label = "Search...", kind = "menu"),
	      dict (ic = "refresh", label = "Check now", kind = "info", val = "index of today 09:12, signed")]
	rows (img, scr, R, 2)
	hints (img, scr, [("ud", "Item"), ("Y", "Search"), ("X", "Update mode"), ("B", "Back"), ("A", "OK")], left = "Onyx  -  SD:/var/pkg, stephaneweg/onyx-packages")
	return img

def packages_search (W = 1280, H = 720, k = 1):
	scr = Scr (W, H, k); img = background (scr)
	parent_col (img, scr, [("download", "Install the 3 updates"), ("box", "Installed"), ("download", "Available"), ("search", "Search...")], 3)
	title (img, scr, "Search", "Packages")
	R = [dict (ic = "app:gbemu", label = "Game Boy", kind = "info", val = "installed 1.0.40", sub = "Emulators  -  Game Boy and Game Boy Color"),
	     dict (ic = "app:gbaemu", label = "Game Boy Advance", kind = "info", val = "installed 1.0.40"),
	     dict (ic = "app:gamelib", label = "Game Library", kind = "info", val = "installed 1.0.27"),
	     dict (ic = "box", label = "gamekit", kind = "info", val = "installed 1.9.0")]
	img = vkeyboard (img, scr, "Search the packages (the name, the title, the category, the summary)", "game", layout = QWERTY, sel = (2, 4))
	# the matches, live, above the keyboard (the list behind follows when the keyboard closes)
	L = scr.L; x = L (300); y = L (78); w = L (680)
	text (img, x, y, "4 packages match \"game\"", font (L (14), True), SUBC); y += L (28)
	for i, r in enumerate (R):
		icon_any (img, scr, r["ic"], x + L (16), y + L (18), L (28), 255)
		text_l (img, x + L (44), y + L (6), L (24), r["label"], font (L (17), i == 0), WHITE)
		text_r (img, x + w, y + L (6), L (24), r["val"], font (L (15)), SUBC)
		y += L (44)
	hints (img, scr, [("dpad", "Move"), ("A", "Type"), ("B", "Delete"), ("X", "Space"), ("Y", "Shift"), ("Start", "Done")])
	return img

def mode (W = 1280, H = 720, k = 1):
	scr = Scr (W, H, k); img = applet_page (scr, "Mode"); L = scr.L
	title (img, scr, "Mode", "Settings")
	R = [dict (ic = "monitor", label = "Desktop", kind = "radio", on = False, sub = "Windows, the menu bar, the dock"),
	     dict (ic = "modes", label = "Pocket", kind = "radio", on = False),
	     dict (ic = "pad", label = "Console", kind = "radio", on = True, val = "in use", vcol = GREEN)]
	rows (img, scr, R, 0, vr = scr.M ("vr2"))
	p = os.path.join (SD, "apps", "modeconf.app", "res", "desktop.bmp")
	if os.path.exists (p):
		pic = Image.open (p).convert ("RGB"); x, y, w = scr.M ("panx"), L (250), scr.M ("panw"); h = int (w * pic.height / pic.width)
		img.paste (pic.resize ((w, h), Image.LANCZOS), (x, y)); ring (img, x, y, w, h, 2, WHITE, alpha = 80)
		yy = y + h + L (12)
		for ln in wrap ("Windows side by side, the menu bar and the dock: for a monitor, a keyboard and a mouse.", font (L (15)), w):
			text (img, x, yy, ln, font (L (15)), WHITE); yy += L (21)
		yy += L (6)
		for ln in wrap ("A: switch -- the open programs are closed first (each asks about its unsaved work)", font (L (13)), w):
			text (img, x, yy, ln, font (L (13)), SUBC); yy += L (18)
	hints (img, scr, [("ud", "Item"), ("B", "Back"), ("A", "Switch")], left = "Onyx  -  SD:/etc/system.ini shell=console")
	return img

MODES = ["1024 x 768", "1280 x 720", "1280 x 800", "1280 x 1024", "1366 x 768", "1440 x 900", "1600 x 900", "1600 x 1200",
	 "1680 x 1050", "1920 x 1080", "1920 x 1200", "2560 x 1440"]
def display (W = 1280, H = 720, k = 1, show_hints = True):
	scr = Scr (W, H, k); img = applet_page (scr, "Display")
	title (img, scr, "Display", "Settings")
	R = [dict (ic = "monitor", label = "Resolution", kind = "choice", val = "1920 x 1080  (16:9, Full HD)", sub = "12 sizes  -  A tries it; kept only if you say so (SD:/cmdline.txt width=, height=)"),
	     dict (ic = "info", label = "The screen now", kind = "info", val = "1280 x 720"),
	     dict (ic = "pad", label = "The games' resolutions", kind = "menu", val = "6 consoles", sub = "")]
	rows (img, scr, R, 0)
	if show_hints: hints (img, scr, [("lr", "Change"), ("ud", "Item"), ("B", "Back"), ("A", "Try it")], left = "Onyx  -  SD:/cmdline.txt")
	return img

def display_confirm (W = 1280, H = 720, k = 1):
	scr = Scr (W, H, k); img = display (W, H, k, show_hints = False); L = scr.L
	def bar (img_, x, y, w):
		rrect (img_, x + L (28), y + L (4), w - L (56), L (8), L (4), (60, 90, 150)); rrect (img_, x + L (28), y + L (4), int ((w - L (56)) * 12 / 15), L (8), L (4), AMBER)
	img = dialog (img, scr, "Keep this resolution?", ["The screen is now 1920 x 1080.", "Back to 1280 x 720 in 12 s if nothing is pressed."],
		      ["Keep", "Go back"], 0, extra = bar)
	hints (img, scr, [("lr", "Choose"), ("B", "Go back"), ("A", "OK")], left = "Onyx  -  a black screen? wait: it comes back by itself")
	return img

def display_games (W = 1280, H = 720, k = 1):
	scr = Scr (W, H, k); img = background (scr)
	parent_col (img, scr, [("monitor", "Resolution"), ("info", "The screen now"), ("pad", "The games' resolutions")], 2)
	title (img, scr, "The games' resolutions", "Display")
	rv = []
	for name, _, emu in v2.SYSTEMS:
		code = v2.SHORT.get (name, name)
		if code == "GBC": continue			# (gbemu: one app for both)
		own = v2.read_kv (os.path.join (SD, "apps", emu + ".app", "app.txt")).get ("resolution", "")
		label = {"GB": "Game Boy, Game Boy Color"}.get (code, name)
		rv.append ((code, label, emu, own))
	R = []
	for code, label, emu, own in rv:
		if emu == "n64emu": val = "1024 x 768"
		elif emu == "gcemu": val = "System (1280 x 720)"
		else: val = "its own: " + own.replace ("x", " x ")
		R.append (dict (ic = ("console", code), label = label, kind = "choice", val = val,
				sub = "SD:/etc/console.ini [screen]  n64emu = 1024x768  (its app.txt says 640x480)" if emu == "n64emu" else ""))
	R.append (dict (ic = "refresh", label = "All back to their own", kind = "action"))
	f = [r["label"] for r in R].index ("Nintendo 64")
	rows (img, scr, R, f)
	hints (img, scr, [("lr", "Change"), ("ud", "Item"), ("Y", "Its own"), ("B", "Back")], left = "Onyx  -  while a game is in front; at home, the system's again")
	return img

# ---- the overview -----------------------------------------------------------------------------------------------------------
def overview (shots):
	tw_, th = 448, 252; gx = 40; cols = 4
	rows_ = (len (shots) + cols - 1) // cols
	W = cols * tw_ + (cols - 1) * gx + 120; H = 150 + rows_ * (th + 150)
	img = grad (W, H, (14, 26, 60), (6, 12, 28))
	put (img, 60, 38, mask (26, 26, lambda d, s: GL["gem"] (d, s, 26 / 16)), (150, 200, 255))
	text (img, 100, 32, "Console mode -- the settings applets in the XMB, for a pad alone", font (26, True), WHITE)
	text (img, 100, 70, "Settings column: Up / Down an applet, A enters it. In an applet: Up / Down a row, Left / Right change its value at once, A acts or goes deeper,", font (16), (170, 190, 220))
	text (img, 100, 92, "B goes back. Text: the pad's keyboard (d-pad, A types, B deletes, X space, Y shift, L1 / R1 the cursor, Start done).", font (16), (170, 190, 220))
	for i, (im, cap, sub, move) in enumerate (shots):
		r, c = divmod (i, cols); x = 60 + c * (tw_ + gx); y = 150 + r * (th + 150)
		img.paste (im.resize ((tw_, int (tw_ * im.height / im.width)), Image.LANCZOS), (x, y)); ring (img, x - 1, y - 1, tw_ + 2, th + 2, 4, (140, 170, 230), alpha = 120)
		if move:
			f = font (13, True); lw = tw (move, f) + 18
			rrect (img, x, y - 30, lw, 24, 8, (8, 14, 34), alpha = 230); ring (img, x, y - 30, lw, 24, 8, (120, 190, 255)); text_c (img, x, y - 30, lw, 24, move, f, WHITE)
		text (img, x, y + th + 10, cap, font (16, True), WHITE)
		for j, line in enumerate (wrap (sub, font (13), tw_)[:4]): text (img, x, y + th + 34 + j * 18, line, font (13), (170, 190, 220))
	return img

def main ():
	os.makedirs (OUT, exist_ok = True)
	P = {}
	def s (name, img): save (img, "console-set-%s.png" % name); P[name] = img
	s ("column", settings_column ())
	s ("sound", sound ())
	s ("sound-volume", sound (step = True))
	s ("gamepad", gamepad ())
	s ("gamepad-map", gamepad_map ())
	s ("keyboard", keyboard ())
	s ("language", language ())
	s ("wifi", wifi ())
	s ("wifi-password", wifi_password ())
	s ("wifi-password-fr", wifi_password (lang = "fr"))
	s ("wifi-640", wifi (640, 480, 1))
	s ("packages", packages ())
	s ("packages-search", packages_search ())
	s ("mode", mode ())
	s ("display", display ())
	s ("display-confirm", display_confirm ())
	s ("display-games", display_games ())
	s ("column-fr", settings_column (lang = "fr", f = 4))
	ov = overview ([(P["column"], "The Settings column", "The eight console applets, each with its help line.", None),
			(P["sound"], "Sound", "Play on, Volume, Mute, a test sound, the programs playing.", "A on Sound"),
			(P["sound-volume"], "A value changed in place", "Right on Volume: 8 / 10, applied at once.", "Right"),
			(P["gamepad"], "Gamepad", "Pad 1, Map the buttons, the keyboard as pad 1; what the apps see, live.", "A on Gamepad"),
			(P["gamepad-map"], "Map the buttons", "Press the button asked; hold Home to cancel.", "A on Map"),
			(P["keyboard"], "Keyboard & Mouse", "The layout, Try it, the wheel's speed.", "A on Keyboard"),
			(P["language"], "Language & Region", "The language, the time zone.", "A on Language"),
			(P["wifi"], "Wi-Fi", "The networks with their signal; scan, a hidden network, the country.", "A on Wi-Fi"),
			(P["wifi-password"], "A network: the password", "The pad's keyboard over its page; the password in dots.", "A on a network, A on Password"),
			(P["packages"], "Packages", "Install the updates, one installing inline; Installed, Available, Search.", "A on Packages"),
			(P["packages-search"], "Search", "Typing 'game': the results above the keyboard.", "A on Search..."),
			(P["mode"], "Mode", "Desktop, Pocket, Console (in use).", "A on Mode"),
			(P["display"], "Display", "The resolution tried with A.", "A on Display"),
			(P["display-confirm"], "Keep it?", "12 s left, then back by itself.", "A on Resolution"),
			(P["display-games"], "The games' resolutions", "Each emulator: its own, the system's, or a size (console.ini).", "A on Games' res."),
			(P["wifi-password-fr"], "In French (AZERTY)", "The keyboard follows the system's language.", None)])
	save (ov, "console-set-overview.png")

if __name__ == "__main__":
	main ()
