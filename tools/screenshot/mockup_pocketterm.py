#!/usr/bin/env python3
# Copyright (c) 2026 the Onyx authors -- MIT licence (see docs/LICENSING.md).
"""mockup_pocketterm.py -- PocketUI on the Waveshare PocketTerm35 (a 3.5" 640 x 480 screen at about 229 ppi,
a D-pad, A / B / X / Y, L / R, Start / Select and a thumb keyboard): an audit of today's captures on that
device and two FLAT, DARK directions for a modernised PocketUI. Mock-ups only: nothing here is built.

    python tools/screenshot/mockup_pocketterm.py     -> docs/compact-shell/mockups/pocketterm/*.png

  Direction 1, "Onyx Noir"  -- today's pocket shell (status, search, chips, grid, running) restyled, 1x.
  Direction 2, "Onyx Deck"  -- one thing per screen, a carousel home for the D-pad, drawn at 1.5x.

It draws with mockup_compact.py's helpers (anti-aliased shapes, the card's fonts, the apps' real icons).
The device's face is drawn from one product photo: its proportions, the glass' corner radius, the buttons'
places and the keys' legends are assumptions (see ASSUMED below).
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
import mockup_compact as mc
from mockup_compact import (rrect, ring, box, hline, vline, glyph, text, text_l, text_c, text_r, tw, font, cfont,
			    mix, shade, lighten, mask, put)

OUT = os.path.join (mc.ROOT, "docs", "compact-shell", "mockups", "pocketterm")
REAL = os.path.join (mc.ROOT, "docs", "compact-shell", "real")
W, H = 640, 480

# ---- the device (ASSUMED from one photo, but its outer size and the screen's) --------------------------------
FACE_MM = (93.5, 168.5)
SCREEN_MM = (71.0, 53.0)
PXMM = W / SCREEN_MM[0]			# 9.01 px per mm on the device: one pixel is 0.111 mm
MM = 1.0 / PXMM
GLASS_R = 27				# the visible picture's corner radius under the glass, px (ASSUMED: 3 mm)
PPI = 25.4 * PXMM

# ---- the tokens: flat and dark (the user's decision) ---------------------------------------------------------
def hx (v): return ((v >> 16) & 255, (v >> 8) & 255, v & 255)
T = dict (
	bg0 = hx (0x0B0C0E),		# the screen's background: continues the black glass
	bg1 = hx (0x15171B),		# a surface: a card, a panel, a sheet
	bg2 = hx (0x1F2228),		# raised: a field, an icon's plate, a menu, a key
	bg3 = hx (0x2C3038),		# pressed / chosen / a chip
	line = hx (0x343842),		# a 1 px separator, a hairline round a plate
	text1 = hx (0xECEEF2),		# body text
	text2 = hx (0xA4AAB5),		# secondary text
	text3 = hx (0x717783),		# disabled, a placeholder
	accent = hx (0x3D86DA),		# Onyx's Aqua blue: the focus, a default button, a switch that is on
	accent_hi = hx (0x7DB4F2),	# the accent as text on dark
	danger = hx (0xE5484D), success = hx (0x46A758), warning = hx (0xF1B33B),
	paper = hx (0xFAFAF8), ink = hx (0x1B1C1F), term = hx (0x0E1316), term_fg = hx (0xD6E2E0))

def _lin (c): c = c / 255.0; return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
def lum (c): return 0.2126 * _lin (c[0]) + 0.7152 * _lin (c[1]) + 0.0722 * _lin (c[2])
def contrast (a, b):
	la, lb = lum (a), lum (b)
	return (max (la, lb) + 0.05) / (min (la, lb) + 0.05)

FR = {"Type to search": "Tapez pour chercher", "Recent": "Récents", "opened last": "ouverts en dernier",
      "Productivity": "Bureautique", "Graphics": "Graphisme", "Games": "Jeux", "Running": "En cours",
      "choose": "choisir", "open": "ouvrir", "category": "catégorie", "tasks": "tâches", "search": "chercher",
      "Text Editor": "Éditeur de texte", "File Viewer": "Fichiers", "Calculator": "Calculatrice",
      "Calendar": "Calendrier", "Game Library": "Ludothèque", "Apps": "Applis", "Files": "Fichiers",
      "Settings": "Réglages", "options": "options", "section": "rubrique", "Open": "Ouvert",
      "last used at": "utilisé à", "3 open": "3 ouvertes", "Media Player": "Lecteur multimédia"}
LANG = ["en"]
def tr (s): return FR.get (s, s) if LANG[0] == "fr" else s

# ---- a drawing context at a scale: sizes in logical pixels ---------------------------------------------------
class Ctx:
	def __init__ (self, k = 1.0, bg = None):
		self.k = k
		self.img = Image.new ("RGB", (W, H), bg or T["bg0"])
		self.lw, self.lh = W / k, H / k
	def u (self, v): return int (round (v * self.k))
	def f (self, size, weight = "regular"): return cfont (max (6, int (round (size * self.k))), weight)
	def mono (self, px, bold = False): return font (px, bold, True)

def gl (img, x, y, fn, size, colour, **kw):
	"""A 16-unit glyph of mockup_compact (g_wifi, g_gem...) at size px."""
	k = size / 16.0
	glyph (img, x, y, size + 2, size + 2, lambda d, s: fn (d, s, k, **kw) if fn not in (mc.g_wifi, mc.g_speaker, mc.g_bell) else fn (d, s * k), colour)

# ---- the device's own buttons, as flat glyphs ----------------------------------------------------------------
def b_face (img, x, y, d, letter, on = False):
	rrect (img, x, y, d, d, d / 2, T["accent"] if on else T["bg3"])
	text_c (img, x, y - 1, d, d, letter, cfont (int (d * 0.62), "bold"), (255, 255, 255) if on else T["text1"])
	return d
def b_pill (img, x, y, h, label):
	f = cfont (int (h * 0.62), "bold"); w = tw (label, f) + int (h * 0.7)
	rrect (img, x, y, w, h, h * 0.3, T["bg3"]); text_c (img, x, y - 1, w, h, label, f, T["text1"])
	return w
def b_key (img, x, y, h, label):
	f = cfont (int (h * 0.6), "regular"); w = max (h, tw (label, f) + int (h * 0.6))
	ring (img, x, y, w, h, h * 0.22, T["text3"], t = 1); text_c (img, x, y - 1, w, h, label, f, T["text2"])
	return w
def b_dpad (img, x, y, d, lit = "all"):
	a = d / 3.0
	def arm (dd, s, which):
		if which in ("all", "h"): dd.rounded_rectangle ([0, a * s, d * s, 2 * a * s], a * s * 0.25, fill = 255)
		if which in ("all", "v"): dd.rounded_rectangle ([a * s, 0, 2 * a * s, d * s], a * s * 0.25, fill = 255)
	glyph (img, x, y, d, d, lambda dd, s: arm (dd, s, "all"), T["bg3"])
	if lit != "none": glyph (img, x, y, d, d, lambda dd, s: arm (dd, s, lit), T["text1"] if lit != "all" else T["text2"])
	return d
def glyph_w (kind, h):
	im = Image.new ("RGB", (400, 80), (0, 0, 0)); return draw_glyph (im, 0, 0, h, kind)
def draw_glyph (img, x, y, h, kind):
	if kind in ("A", "B", "X", "Y"): return b_face (img, x, y, h, kind, kind == "A")
	if kind in ("L", "R", "Start", "Select"): return b_pill (img, x, y, h, kind)
	if kind.startswith ("dpad"): return b_dpad (img, x, y, h, {"dpad": "all", "dpad-h": "h", "dpad-v": "v"}[kind])
	return b_key (img, x, y, h, kind)
def hints (c, items, y, align = "right", x = None, size = 13, colour = None):
	"""A line of hints: [(glyphs, word), ...]; glyphs is a list of kinds. Returns its width."""
	img = c.img; h = c.u (size + 5); f = c.f (size); gap = c.u (4); sep = c.u (14)
	widths = [sum (glyph_w (g, h) + gap for g in gs) + tw (word, f) for gs, word in items]
	total = sum (widths) + sep * (len (items) - 1)
	x0 = (W - c.u (22) - total) if align == "right" else ((W - total) // 2 if align == "centre" else x)
	xx = x0
	for (gs, word), wd in zip (items, widths):
		for g in gs: xx += draw_glyph (img, xx, y, h, g) + gap
		text_l (img, xx, y - 1, h, word, f, colour or T["text2"]); xx += tw (word, f) + sep
	return total

# ---- the status row: no bar, it melts into the glass ---------------------------------------------------------
def status (c, title = None, menus = (), open_menu = None, home = False, clock = "12:34", fold = False, menu = None):
	"""menu: the single Menu entry (the default: Select or F10 puts the focus on it) -- "idle", "focus" or "open";
	menus: the titles written in the band instead (the alternative, for an app of three or four menus)."""
	img = c.img; y = c.u (7); h = c.u (22); x = c.u (22); fb = c.f (14, "bold"); fr = c.f (14)
	gl (img, x, y + c.u (3), mc.g_gem, c.u (15), T["accent"] if home else T["text1"]); x += c.u (22)
	pos = {}
	if title is None:
		text_l (img, x, y, h, "Onyx", fb, T["text1"] if home else T["text2"])
	else:
		text_l (img, x, y, h, title, fb, T["text1"]); x += tw (title, fb) + c.u (16)
		if menu:
			wm = c.u (24) + tw ("Menu", fr) + c.u (10)
			if menu == "open": rrect (img, x - c.u (4), y, wm, h, c.u (6), T["bg3"])
			if menu in ("focus", "open"): ring (img, x - c.u (7), y - c.u (3), wm + c.u (6), h + c.u (6), c.u (8), T["accent"], t = max (2, c.u (2)))
			gl (img, x + c.u (2), y + c.u (5), mc.g_menu, c.u (12), T["text1"] if menu != "idle" else T["text2"])
			text_l (img, x + c.u (22), y, h, "Menu", fr, T["text1"] if menu != "idle" else T["text2"]); pos["Menu"] = x - c.u (4); x += wm + c.u (10)
		for m in menus:
			wm = tw (m, fr)
			if m == open_menu: rrect (img, x - c.u (7), y, wm + c.u (14), h, c.u (6), T["bg3"])
			text_l (img, x, y, h, m, fr, T["text1"] if m == open_menu else T["text2"]); pos[m] = x - c.u (7); x += wm + c.u (15)
		if fold: text_l (img, x, y, h, "»", fb, T["text2"])
	xr = W - c.u (22)
	text_r (img, xr, y, h, clock, fb, T["text1"]); xr -= tw (clock, fb) + c.u (12)
	ft = c.f (12)
	text_r (img, xr, y, h, "78 %", ft, T["text2"]); xr -= tw ("78 %", ft) + c.u (5)
	mc.battery (img, xr - c.u (22), y + c.u (5), 78, T["text2"], k = c.k); xr -= c.u (22) + c.u (12)
	gl (img, xr - c.u (16), y + c.u (4), mc.g_wifi, c.u (15), T["text2"]); xr -= c.u (16) + c.u (10)
	return pos

def plate (c, x, y, s, app, isz, focus = False, running = False):
	"""An app's icon on dark: a flat plate (several icons are dark themselves), a hairline; the focus is a ring."""
	img = c.img; r = int (s * 0.24)
	rrect (img, x, y, s, s, r, T["bg2"]); ring (img, x, y, s, s, r, T["line"], t = 1)
	mc.draw_icon (img, app, x + (s - isz) // 2, y + (s - isz) // 2, isz)
	if focus:
		o = c.u (4); ring (img, x - o, y - o, s + 2 * o, s + 2 * o, r + o, T["accent"], t = max (2, c.u (2)))
	if running: rrect (img, x + s // 2 - c.u (2), y + s + c.u (5), c.u (5), c.u (5), c.u (3), T["accent"])

APPN = {"tinypad": "Text Editor", "tinycalc": "Calculator", "fileviewer": "File Viewer", "gamelib": "Game Library"}
def aname (a): return tr (APPN.get (a, mc.app_name (a)))
RECENT = ["terminal", "tinypad", "letters", "fileviewer", "tinycalc", "calendar", "jet", "mail", "gamelib", "media"]

# ============================================== Direction 1: Onyx Noir ========================================
def n1_home ():
	c = Ctx (); img = c.img
	status (c, home = True)
	rrect (img, 14, 38, W - 28, 36, 18, T["bg2"])
	gl (img, 28, 48, mc.g_search, 15, T["text3"]); text_l (img, 52, 38, 36, tr ("Type to search"), c.f (15), T["text3"])
	x = 14; fc = c.f (14)
	x += b_pill (img, x, 87, 22, "L") + 8
	for i, (name, col) in enumerate ((("Recent", (140, 146, 158)),) + tuple ((n, mc.CATC[n]) for n in ("Productivity", "Internet", "Graphics", "Games"))):
		wd = tw (tr (name), fc) + 34
		rrect (img, x, 83, wd, 30, 15, T["bg3"] if i == 0 else T["bg1"])
		rrect (img, x + 11, 94, 8, 8, 4, col); text_l (img, x + 25, 83, 30, tr (name), c.f (14, "bold") if i == 0 else fc, T["text1"] if i == 0 else T["text2"])
		x += wd + 6
	b_pill (img, W - 14 - 22, 87, 22, "R")
	rrect (img, 14, 121, W - 28, 252, 16, T["bg1"])
	text_l (img, 30, 129, 24, tr ("Recent"), c.f (15, "bold"), T["text1"])
	text_l (img, 30 + tw (tr ("Recent"), c.f (15, "bold")) + 10, 129, 24, tr ("opened last"), c.f (13), T["text2"])
	cw = (W - 28 - 16) // 5; s = 60
	for i, a in enumerate (RECENT):
		cx = 22 + (i % 5) * cw; cy = 160 + (i // 5) * 104
		plate (c, cx + (cw - s) // 2, cy, s, a, 44, focus = i == 1, running = i in (0, 1, 2))
		f = c.f (14, "bold") if i == 1 else c.f (14)
		text_c (img, cx, cy + s + 13, cw, 20, mc.ellipsize (aname (a), f, cw - 6), f, T["text1"] if i == 1 else T["text2"])
	text_l (img, 22, 381, 20, tr ("Running"), c.f (14, "bold"), T["text1"]); x = 22 + tw (tr ("Running"), c.f (14, "bold")) + 12
	for a in ("terminal", "tinypad", "letters"):
		f = c.f (13); wd = 34 + tw (aname (a), f) + 12
		rrect (img, x, 379, wd, 26, 13, T["bg2"]); mc.draw_icon (img, a, x + 7, 383, 18)
		text_l (img, x + 30, 379, 26, aname (a), f, T["text1"]); x += wd + 6
	hline (img, 22, 416, W - 44, T["line"])
	hints (c, [(["dpad"], tr ("choose")), (["A"], tr ("open")), (["L", "R"], tr ("category")), (["Start"], tr ("tasks"))], 429, align = "centre")
	return img

TERM = [("SD:/ $ ", "ls /bin | grep e"), ("", "echo"), ("", "sleep"), ("", "yes"), ("SD:/ $ ", "ps"), ("", "  1 k R  idle"),
	("", "  2 k S  usb"), ("", " 14 a R  pocketui"), ("", " 21 a R  terminal"), ("", " 22 a S  cmd"), ("", " 25 a R  ps"),
	("SD:/ $ ", "pkg check"), ("", "onyx 2026.10.161   up to date"), ("", "systemkit 1.94.0   up to date"),
	("SD:/ $ ", "echo onyx | wc -c"), ("", "5"), ("SD:/ $ ", "")]
def term_grid (img, x, y, w, h, px):
	"""A TrueType monospace grid. Returns (columns, lines, cell width, cell height)."""
	f = font (px, False, True); cwd = int (round (f.getlength ("M"))); chh = int (round (px * 1.22))
	cols, rows = w // cwd, h // chh
	for i, (p, s) in enumerate (TERM[:rows]):
		yy = y + i * chh
		text (img, x, yy, p, f, T["accent_hi"]); text (img, x + cwd * len (p), yy, s, f, T["term_fg"])
		if i == len (TERM) - 1: box (img, x + cwd * len (p), yy + 2, cwd, chh - 3, T["term_fg"])
	return cols, rows, cwd, chh

def n1_terminal ():
	c = Ctx (bg = T["term"]); img = c.img
	box (img, 0, 0, W, 62, T["bg0"])
	status (c, "Terminal", menu = "idle")
	x = 20
	for i, t in enumerate (("SD:/", "docs", "ping 192.168.0.1")):
		f = c.f (13, "bold") if i == 0 else c.f (13); wd = tw (t, f) + 26
		if i == 0: rrect (img, x, 34, wd, 28, 8, T["term"], corners = (True, True, False, False)); box (img, x + 10, 34, wd - 20, 2, T["accent"])
		text_c (img, x, 34, wd, 27, t, f, T["text1"] if i == 0 else T["text2"]); x += wd + 2
	text_c (img, x, 34, 26, 27, "+", c.f (16), T["text2"])
	hints (c, [(["L", "R"], "tab")], 39, size = 11)
	cols, rows, cw, ch = term_grid (img, 12, 70, W - 24, H - 70 - 10, 15)
	n1_terminal.grid = (cols, rows, cw, ch)
	return img

EDIT = ["PocketTerm35 -- notes", "", "- the screen: 640 x 480, 3.5 inches", "- one pixel is 0.11 mm: type must grow",
	"- D-pad and A / B do what the arrows,", "  Enter and Esc do", "- Start: this app's menus", "- Select: Home; held: the open apps", "",
	"TODO: try Ctrl + arrows as the pointer"]
def editor_body (c, y, px = 15, lines = EDIT, x = 0, w = W):
	img = c.img; box (img, x, y, w, H - y, T["bg1"]); f = font (px, False, True); lh = int (px * 1.4)
	for i, s in enumerate (lines):
		text (img, x + c.u (18), y + c.u (12) + i * lh, s, f, T["text1"])
	box (img, x + c.u (18) + int (f.getlength (lines[-1])) + 1, y + c.u (12) + (len (lines) - 1) * lh, 2, lh - 2, T["accent"])

def menu_rows (c, x, y, w, rows, sel, rh):
	img = c.img
	for i, r in enumerate (rows):
		if r is None: hline (img, x + c.u (10), y + rh // 4, w - c.u (20), T["line"]); y += rh // 2; continue
		name, key = r
		if i == sel: rrect (img, x + c.u (5), y, w - c.u (10), rh, c.u (7), T["accent"])
		text_l (img, x + c.u (16), y, rh, name, c.f (15), (255, 255, 255) if i == sel else T["text1"])
		if key: text_r (img, x + w - c.u (16), y, rh, key, c.f (13), (225, 236, 250) if i == sel else T["text3"])
		y += rh
	return y
EDIT_MENU = [("Undo", "Ctrl+Z"), ("Redo", "Ctrl+Y"), None, ("Cut", "Ctrl+X"), ("Copy", "Ctrl+C"), ("Paste", "Ctrl+V"), None,
	     ("Select All", "Ctrl+A"), ("Find...", "Ctrl+F")]

def n1_editor ():
	c = Ctx (); img = c.img
	pos = status (c, "Text Editor", ("File", "Edit", "Search", "View"), open_menu = "Edit")
	text_l (img, 22, 36, 22, "SD:/docs/pocketterm.txt", c.f (13), T["text2"])
	editor_body (c, 62)
	img2 = Image.blend (img, Image.new ("RGB", img.size, T["bg0"]), 0.45); img.paste (img2.crop ((0, 62, W, H)), (0, 62))
	x, w = pos["Edit"], 232
	rrect (img, x, 33, w, 268, 12, T["bg2"]); ring (img, x, 33, w, 268, 12, T["line"])
	menu_rows (c, x, 40, w, EDIT_MENU, 4, 32)
	box (img, 0, H - 40, W, 40, T["bg0"])
	hints (c, [(["dpad-v"], "move"), (["A"], "choose"), (["dpad-h"], "other menu"), (["B"], "close")], H - 32, align = "centre")
	return img

def page (img, x, y, w, h, zoom):
	"""Letters' document: a white page (it is what will be printed), its content at the zoom."""
	box (img, x, y, w, h, T["paper"])
	def lf (size, bold = False):
		p = os.path.join (mc.SD, "res", "fonts", "LiberationSans-Bold.ttf" if bold else "LiberationSans-Regular.ttf")
		return ImageFont.truetype (p, max (6, int (round (size * zoom))))
	cx = x + w // 2
	t = lf (37, True); text (img, cx - tw ("Onyx Letters", t) // 2, y + int (58 * zoom), "Onyx Letters", t, T["ink"])
	box (img, cx - tw ("Onyx Letters", t) // 2 - 3, y + int (58 * zoom), 2, int (44 * zoom), T["ink"])
	s = "A word processor for Onyx, in the way of AbiWord"; t2 = lf (21)
	text (img, cx - tw (s, t2) // 2, y + int (118 * zoom), s, t2, (86, 88, 94))
	px, py, pw, ph = x + int (96 * zoom), y + int (160 * zoom), w - int (192 * zoom), int (150 * zoom)
	if py < y + h:
		ph = min (ph, y + h - py)
		pic = Image.new ("RGB", (pw, max (1, ph)), (234, 236, 240)); d = ImageDraw.Draw (pic)
		for k2, (col, amp, base) in enumerate ((((240, 176, 116), 0.20, 0.55), ((74, 150, 172), 0.16, 0.72), ((44, 96, 128), 0.12, 0.88))):
			pts = [(i, int ((base + amp * math.sin (i / pw * 6.3 * (1.2 + k2 * 0.3) + k2 * 2)) * int (150 * zoom))) for i in range (0, pw + 1, 4)]
			d.polygon (pts + [(pw, ph + 400), (0, ph + 400)], fill = col)
		img.paste (pic, (px, py))
	ty = y + int (330 * zoom); tf = lf (12)
	for i in range (8):
		if ty + i * int (19 * zoom) + 12 < y + h: box (img, x + int (96 * zoom), ty + i * int (19 * zoom), int ((w - 192 * zoom) * (0.97 if i % 4 != 3 else 0.6)), max (2, int (7 * zoom)), (196, 198, 204))

def tool (c, x, y, s, label, on = False, weight = "bold", colour = None):
	if on: rrect (c.img, x, y, s, s, c.u (7), T["bg3"])
	text_c (c.img, x, y - 1, s, s, label, c.f (15, weight), colour or T["text1"]); return s
def combo (c, x, y, w, h, label):
	rrect (c.img, x, y, w, h, c.u (8), T["bg2"]); text_l (c.img, x + c.u (10), y, h, label, c.f (14), T["text1"])
	gl (c.img, x + w - c.u (20), y + h // 2 - c.u (4), mc.g_chev, c.u (11), T["text2"]); return w

# ---- the responsive toolbar: one rule for every app ----------------------------------------------------------
# A tool: (kind, label, name, shortcut, priority 1..3, width, least width). kind: b a button, c a combo, - a
# separator. Letters' two toolbars as its 640 x 480 capture shows them.
def _b (label, name, key, pr): return dict (kind = "b", label = label, name = name, key = key, pr = pr, w = 30, minw = 30)
def _c (label, name, pr, w, minw, icon): return dict (kind = "c", label = label, name = name, key = "", pr = pr, w = w, minw = minw, icon = icon)
SEP = dict (kind = "-", label = "", name = "", key = "", pr = 0, w = 9, minw = 9)
LETTERS_STD = [_b ("✚", "New", "Ctrl+N", 3), _b ("▱", "Open...", "Ctrl+O", 2), _b ("▣", "Save", "Ctrl+S", 1), SEP,
	       _b ("↶", "Undo", "Ctrl+Z", 1), _b ("↷", "Redo", "Ctrl+Y", 2), SEP,
	       _b ("✂", "Cut", "Ctrl+X", 3), _b ("❐", "Copy", "Ctrl+C", 3), _b ("▤", "Paste", "Ctrl+V", 3), SEP,
	       _b ("find", "Find...", "Ctrl+F", 2), _b ("¶", "Show marks", "", 3), SEP,
	       _b ("┄", "Page break", "Ctrl+Enter", 3), _b ("⊞", "Table", "", 3), _b ("Ω", "Symbol...", "", 3), _b ("◩", "Picture...", "", 3), SEP,
	       _c ("100 %", "Zoom", 3, 74, 60, "%")]
LETTERS_FMT = [_c ("Title", "Style", 1, 112, 84, "¶"), _c ("Liberation Sans", "Font", 2, 150, 104, "F"), _c ("28", "Size", 1, 62, 50, "28"), SEP,
	       _b ("B", "Bold", "Ctrl+B", 1), _b ("I", "Italic", "Ctrl+I", 1), _b ("U", "Underline", "Ctrl+U", 1), _b ("S", "Strike", "", 3),
	       _b ("x²", "Superscript", "", 3), _b ("x₂", "Subscript", "", 3), SEP,
	       _b ("A", "Text colour", "", 2), _b ("▨", "Highlight", "", 3), SEP,
	       _b ("≡", "Align left", "Ctrl+L", 2), _b ("≣", "Centre", "Ctrl+E", 2), _b ("≢", "Justify", "Ctrl+J", 3), _b ("•", "Bullets", "", 2), _b ("1.", "Numbers", "", 3)]
GAP, MORE = 2, 36
def tb_width (items):
	its = [t for t in items]
	return sum (t["w"] + GAP for t in its)
def tb_clean (items):
	"""No separator first, last or twice."""
	out = []
	for t in items:
		if t["kind"] == "-" and (not out or out[-1]["kind"] == "-"): continue
		out.append (t)
	while out and out[-1]["kind"] == "-": out.pop ()
	return out
def tb_resolve (items, avail):
	"""The rule. 1: the combos shrink to their least width. 2: the priority 3 tools go to the overflow, from
	the end. 3: the combos of priority 2 and 3 fold to an icon that opens them. 4: the priority 2 tools go.
	5: the priority 1 combos fold. The priority 1 tools stay. Returns (the row, the overflow)."""
	row = [dict (t) for t in items]; over = []
	fits = lambda: tb_width (tb_clean (row)) + (MORE if over else 0) <= avail
	if fits (): return tb_clean (row), over
	for t in row:
		if t["kind"] == "c": t["w"] = t["minw"]
	def drop (pr):
		for t in reversed ([t for t in row if t["pr"] == pr]):
			if fits (): return
			row.remove (t); over.insert (0, t)
	drop (3)
	def fold (prs):
		for pr in prs:
			for t in row:
				if not fits () and t["kind"] == "c" and t["pr"] == pr and "28" not in t["icon"]: t["w"] = 30; t["folded"] = True
	fold ((3, 2)); drop (2); fold ((1,))
	order = {id (t): i for i, t in enumerate (items)}
	return tb_clean (row), over
def tb_draw (c, x, y, items, over, focus_more = False, right = None):
	img = c.img; fs = font (c.u (15))
	for t in items:
		w = c.u (t["w"]); s = c.u (30)
		if t["kind"] == "-": vline (img, x + c.u (4), y + c.u (6), c.u (18), T["line"])
		elif t["kind"] == "c" and not t.get ("folded"): combo (c, x, y, w, s, mc.ellipsize (t["label"], c.f (14), w - c.u (32)))
		else:
			lab = t["icon"] if t["kind"] == "c" else t["label"]
			if t["kind"] == "c": rrect (img, x, y, s, s, c.u (8), T["bg2"])
			if t["name"] == "Bold": rrect (img, x, y, s, s, c.u (7), T["bg3"])
			f = c.f (15, "bold") if lab in ("B", "I", "U", "S", "A", "F", "1.", "%") else fs
			if lab == "find": gl (img, x + c.u (8), y + c.u (8), mc.g_search, c.u (14), T["text1"])
			else: text_c (img, x, y - 1, s, s, lab, f, T["text1"])
			if t["name"] == "Text colour": box (img, x + c.u (8), y + s - c.u (7), s - c.u (16), max (2, c.u (3)), T["danger"])
		x += w + c.u (GAP)
	if over:
		mx = (right - c.u (MORE - 2)) if right else x + c.u (4)
		rrect (img, mx, y, c.u (34), c.u (30), c.u (8), T["bg3"] if focus_more else T["bg2"]); text_c (img, mx, y - 1, c.u (34), c.u (30), "»", c.f (16, "bold"), T["text1"])
		if focus_more: ring (img, mx - 3, y - 3, c.u (34) + 6, c.u (30) + 6, c.u (10), T["accent"], t = 2)
		return mx
	return x
LETTERS_MERGED = LETTERS_FMT[:3] + [SEP] + [t for t in LETTERS_STD if t["pr"] == 1] + [SEP] + LETTERS_FMT[4:] + [SEP] + [t for t in LETTERS_STD if t["pr"] != 1]
LETTERS_MENUS = ["File", "Edit", "View", "Insert", "Format", "Table", "Tools", "Help"]
FORMAT_MENU = [("Font...", "", False), ("Paragraph...", "", False), ("Styles", ">", False), None, ("Bold", "Ctrl+B", True), ("Italic", "Ctrl+I", False),
	       ("Underline", "Ctrl+U", False), None, ("Align", ">", False), ("Bullets and Numbering...", "", False), None, ("Page Setup...", "", False)]

def n1_letters (menu = "idle", overflow = False, open_menu = False):
	c = Ctx (); img = c.img
	pos = status (c, "Letters", menu = "open" if open_menu else menu)
	y = 36
	row, over = tb_resolve (LETTERS_MERGED, W - 28)
	n1_letters.over = over; n1_letters.row = row
	mx = tb_draw (c, 14, y, row, over, focus_more = overflow, right = W - 14)
	box (img, 0, 72, W, H - 72 - 30, T["bg0"])
	zoom = (W - 28) / 794.0				# A4 at 96 dpi is 794 px wide: fit the width
	page (img, 14, 80, W - 28, H - 80 - 30, zoom)
	box (img, 0, H - 30, W, 30, T["bg0"])
	text_l (img, 22, H - 28, 22, "letters-tour.rtf", c.f (13), T["text2"])
	text_c (img, 0, H - 28, W, 22, "Page 1 of 2   ·   393 words", c.f (13), T["text2"])
	text_r (img, W - 22, H - 28, 22, "Fit width  %d %%" % round (zoom * 100), c.f (13), T["text2"])
	n1_letters.zoom = zoom
	if overflow or open_menu:
		img.paste (Image.blend (img, Image.new ("RGB", img.size, T["bg0"]), 0.45).crop ((0, 72, W, H)), (0, 72))
	if overflow:					# the overflow menu: the tools that left the row, by name
		rows = [(t["name"], t["key"] if t["kind"] == "b" else ">", t["label"] if t["kind"] == "c" else None) for t in over if t["kind"] != "-"]
		more = len (rows) - 11; rows = rows[:11] + ([("%d more" % more, "v", None)] if more > 0 else [])
		rh = 28; mw = 300; mh = 14 + rh * len (rows); x = W - 14 - mw
		rrect (img, x, 70, mw, mh, 12, T["bg2"]); ring (img, x, 70, mw, mh, 12, T["line"])
		yy = 77
		for i, (name, key, val) in enumerate (rows):
			sel = i == 1
			if sel: rrect (img, x + 5, yy, mw - 10, rh, 7, T["accent"])
			text_l (img, x + 16, yy, rh, name, c.f (14), (255, 255, 255) if sel else T["text1"])
			if val: text_l (img, x + 16 + tw (name, c.f (14)) + 10, yy, rh, val, c.f (13), T["text2"])
			if key == ">": gl (img, x + mw - 26, yy + 9, mc.g_chev, 10, T["text2"], dirn = "right")
			elif key == "v": gl (img, x + mw - 26, yy + 9, mc.g_chev, 10, T["text2"], dirn = "down")
			elif key: text_r (img, x + mw - 16, yy, rh, key, c.f (12), (226, 236, 250) if sel else T["text3"])
			yy += rh
		box (img, 0, H - 30, W, 30, T["bg0"])
		hints (c, [(["dpad-v"], "move"), (["A"], "do it"), (["B"], "close")], H - 26, align = "left", x = 22, size = 12)
	if open_menu:					# Select: the Menu entry unfolds -- the titles, then the chosen menu's items
		x = pos["Menu"]; rh = 30; w1 = 150; h1 = 14 + rh * len (LETTERS_MENUS)
		rrect (img, x, 34, w1, h1, 12, T["bg2"]); ring (img, x, 34, w1, h1, 12, T["line"])
		for i, m in enumerate (LETTERS_MENUS):
			yy = 41 + i * rh; sel = m == "Format"
			if sel: rrect (img, x + 5, yy, w1 - 10, rh, 7, T["bg3"])
			text_l (img, x + 16, yy, rh, m, c.f (15, "bold") if sel else c.f (15), T["text1"])
			gl (img, x + w1 - 24, yy + 10, mc.g_chev, 10, T["text1"] if sel else T["text3"], dirn = "right")
		x2 = x + w1 + 6; w2 = 290; h2 = 14 + sum (rh if r else rh // 2 for r in FORMAT_MENU)
		rrect (img, x2, 34 + 4 * rh - 96, w2, h2, 12, T["bg2"]); ring (img, x2, 34 + 4 * rh - 96, w2, h2, 12, T["line"])
		yy = 34 + 4 * rh - 96 + 7
		for i, r in enumerate (FORMAT_MENU):
			if r is None: hline (img, x2 + 12, yy + rh // 4, w2 - 24, T["line"]); yy += rh // 2; continue
			name, key, on = r; sel = name == "Italic"
			if sel: rrect (img, x2 + 5, yy, w2 - 10, rh, 7, T["accent"])
			if on: text_l (img, x2 + 14, yy, rh, "✓", font (14, True), T["accent_hi"])
			text_l (img, x2 + 36, yy, rh, name, c.f (15), (255, 255, 255) if sel else T["text1"])
			if key == ">": gl (img, x2 + w2 - 26, yy + 10, mc.g_chev, 10, T["text2"], dirn = "right")
			elif key: text_r (img, x2 + w2 - 16, yy, rh, key, c.f (13), (226, 236, 250) if sel else T["text3"])
			yy += rh
		box (img, 0, H - 30, W, 30, T["bg0"])
		hints (c, [(["dpad"], "move"), (["A"], "choose"), (["B"], "back"), (["Select"], "close")], H - 26, align = "centre", size = 12)
	return img

def thumb (im, w, h, r):
	t = im.resize ((w, h), Image.LANCZOS); m = mask (w, h, lambda d, s: d.rounded_rectangle ([0, 0, w * s - 1, h * s - 1], r * s, fill = 255))
	return t, m

def n1_switcher (shots):
	c = Ctx (); img = c.img
	status (c, "Text Editor", menu = "idle")
	text_c (img, 0, 44, W, 26, "Open apps", c.f (18, "bold"), T["text1"])
	text_c (img, 0, 70, W, 20, "the most recent first", c.f (13), T["text2"])
	cards = [("terminal", shots[0], 140), ("tinypad", shots[1], 300), ("letters", shots[2], 140)]
	xs = [18, 170, 482]
	for (a, im, wd), x in zip (cards, xs):
		hh = wd * 3 // 4; y = 112 + (225 - hh) // 2
		t, m = thumb (im, wd, hh, 10); img.paste (t, (x, y), m)
		if wd == 300: ring (img, x - 4, y - 4, wd + 8, hh + 8, 14, T["accent"], t = 3)
		else: ring (img, x, y, wd, hh, 10, T["line"])
		f = c.f (15, "bold") if wd == 300 else c.f (14)
		lw = 26 + tw (aname (a), f); lx = x + (wd - lw) // 2
		mc.draw_icon (img, a, lx, y + hh + 14, 20); text_l (img, lx + 26, y + hh + 12, 24, aname (a), f, T["text1"] if wd == 300 else T["text2"])
	hline (img, 22, 416, W - 44, T["line"])
	hints (c, [(["dpad-h"], "choose"), (["A"], "switch"), (["X"], "close it"), (["B"], "stay"), (["Alt", "Tab"], "next")], 429, align = "centre")
	return img

def slider (c, x, y, w, v, fn, label, value):
	img = c.img; h = c.u (40)
	gl (img, x, y + c.u (12), fn, c.u (16), T["text1"]); text_l (img, x + c.u (28), y, h, label, c.f (15), T["text1"])
	bx = x + c.u (140); bw = w - c.u (140) - c.u (52); bh = c.u (8)
	rrect (img, bx, y + h // 2 - bh // 2, bw, bh, bh // 2, T["bg3"]); rrect (img, bx, y + h // 2 - bh // 2, int (bw * v), bh, bh // 2, T["accent"])
	kd = c.u (18); rrect (img, bx + int (bw * v) - kd // 2, y + h // 2 - kd // 2, kd, kd, kd // 2, T["text1"])
	text_r (img, x + w, y, h, value, c.f (14), T["text2"])
def toggle (c, x, y, w, h, fn, label, sub, on):
	img = c.img
	rrect (img, x, y, w, h, c.u (12), T["accent"] if on else T["bg2"])
	gl (img, x + c.u (12), y + c.u (11), fn, c.u (16), (255, 255, 255) if on else T["text1"])
	text (img, x + c.u (12), y + h - c.u (40), label, c.f (14, "bold"), (255, 255, 255) if on else T["text1"])
	text (img, x + c.u (12), y + h - c.u (21), sub, c.f (12), (226, 236, 250) if on else T["text2"])

def n1_quick (under):
	c = Ctx (); img = c.img
	img.paste (Image.blend (under, Image.new ("RGB", under.size, T["bg0"]), 0.6))
	status (c, home = True)
	x, y, w, h = 110, 36, W - 124, 372
	rrect (img, x, y, w, h, 16, T["bg1"]); ring (img, x, y, w, h, 16, T["line"])
	tw3 = (w - 32 - 16) // 3
	for i, (fn, lab, sub, on) in enumerate (((mc.g_wifi, "Wi-Fi", "Maison", True), (mc.g_dnd, "Do not disturb", "off", False), (mc.g_moon, "Night light", "off", False))):
		toggle (c, x + 16 + i * (tw3 + 8), y + 16, tw3, 76, fn, lab, sub, on)
	ring (img, x + 16 - 4, y + 16 - 4, tw3 + 8, 84, 15, T["accent_hi"], t = 2)
	slider (c, x + 20, y + 106, w - 40, 0.7, mc.g_speaker, "Volume", "7 / 10")
	slider (c, x + 20, y + 150, w - 40, 0.55, mc.g_sun, "Brightness", "55 %")
	hline (img, x + 16, y + 202, w - 32, T["line"])
	mc.battery (img, x + 20, y + 219, 78, T["success"], k = 1.2)
	text_l (img, x + 56, y + 212, 28, "Battery 78 %", c.f (15, "bold"), T["text1"]); text_r (img, x + w - 20, y + 212, 28, "about 3 h 10 left", c.f (14), T["text2"])
	hline (img, x + 16, y + 250, w - 32, T["line"])
	text_l (img, x + 20, y + 258, 24, "Telegram", c.f (13, "bold"), T["text1"]); text_l (img, x + 90, y + 258, 24, "Alice: are you on the train?", c.f (13), T["text2"]); text_r (img, x + w - 20, y + 258, 24, "12:31", c.f (12), T["text3"])
	text_l (img, x + 20, y + 284, 24, "Packages", c.f (13, "bold"), T["text1"]); text_l (img, x + 90, y + 284, 24, "2 updates are ready", c.f (13), T["text2"]); text_r (img, x + w - 20, y + 284, 24, "11:58", c.f (12), T["text3"])
	bw = (w - 32 - 16) // 3
	for i, (fn, lab) in enumerate (((mc.g_gear, "Settings"), (mc.g_lock, "Lock"), (mc.g_power, "Power"))):
		bx = x + 16 + i * (bw + 8); rrect (img, bx, y + 322, bw, 36, 10, T["bg2"])
		f = c.f (14); lw = 22 + tw (lab, f); gl (img, bx + (bw - lw) // 2, y + 332, fn, 14, T["text1"]); text_l (img, bx + (bw - lw) // 2 + 22, y + 322, 36, lab, f, T["text1"])
	box (img, 0, 412, W, H - 412, T["bg0"])
	hints (c, [(["dpad"], "move"), (["A"], "switch"), (["dpad-h"], "adjust"), (["B"], "close")], 429, align = "centre")
	return img

def dialog_btn (c, x, y, w, h, g, label, default = False):
	img = c.img
	rrect (img, x, y, w, h, c.u (10), T["accent"] if default else T["bg3"])
	f = c.f (15, "bold" if default else "regular"); gh = c.u (18); lw = glyph_w (g, gh) + c.u (8) + tw (label, f); xx = x + (w - lw) // 2
	if default: rrect (img, xx, y + (h - gh) // 2, gh, gh, gh / 2, (255, 255, 255)); text_c (img, xx, y + (h - gh) // 2 - 1, gh, gh, g, cfont (int (gh * 0.62), "bold"), T["accent"])
	else: draw_glyph (img, xx, y + (h - gh) // 2, gh, g)
	text_l (img, xx + gh + c.u (8), y, h, label, f, (255, 255, 255) if default else T["text1"])

def n1_dialog (under):
	c = Ctx (); img = c.img
	img.paste (Image.blend (under, Image.new ("RGB", under.size, T["bg0"]), 0.65))
	x, y, w, h = 90, 128, 460, 204
	rrect (img, x, y, w, h, 16, T["bg1"]); ring (img, x, y, w, h, 16, T["line"])
	text (img, x + 26, y + 24, "Save the changes to pocketterm.txt?", c.f (18, "bold"), T["text1"])
	text (img, x + 26, y + 60, "It was changed 4 minutes ago. If you do not save,", c.f (15), T["text2"])
	text (img, x + 26, y + 82, "these changes are lost.", c.f (15), T["text2"])
	bw = (w - 52 - 20) // 3
	dialog_btn (c, x + 26, y + 136, bw, 42, "B", "Cancel")
	dialog_btn (c, x + 26 + bw + 10, y + 136, bw, 42, "X", "Don't save")
	dialog_btn (c, x + 26 + 2 * (bw + 10), y + 136, bw, 42, "A", "Save", True)
	return img

def lock (c, sub = "Press A or any key"):
	img = c.img
	gl (img, W // 2 - c.u (14), c.u (56), mc.g_gem, c.u (28), T["accent"])
	f = c.f (84, "light"); text_c (img, 0, c.u (108), W, c.u (96), "12:34", f, T["text1"])
	text_c (img, 0, c.u (212), W, c.u (26), "Saturday 10 October", c.f (17), T["text2"])
	f2 = c.f (13); lw = tw ("78 %", f2) + c.u (30)
	mc.battery (img, W // 2 - lw // 2, c.u (258), 78, T["text2"], k = c.k); text_l (img, W // 2 - lw // 2 + c.u (30), c.u (252), c.u (24), "78 %", f2, T["text2"])
	hints (c, [(["A"], sub)], H - c.u (52), align = "centre")
	return img

# ============================================== Direction 2: Onyx Deck ========================================
SECTIONS = ["Recent", "Apps", "Games", "Files", "Settings"]
def d2_home (k = 1.5):
	c = Ctx (k); img = c.img; u = c.u
	x0 = u (20)
	ft = c.f (26, "bold"); text (img, x0, u (10), tr ("Recent"), ft, T["text1"])
	fb = c.f (13, "bold"); text_r (img, W - u (20), u (14), u (20), "12:34", fb, T["text1"])
	mc.battery (img, W - u (20) - tw ("12:34", fb) - u (34), u (19), 78, T["text2"], k = k)
	y = u (50); x = x0; fs = c.f (12)
	x += b_pill (img, x, y, u (16), "L") + u (8)
	for i, s in enumerate (SECTIONS):
		f = c.f (12, "bold") if i == 0 else fs
		text_l (img, x, y, u (16), tr (s), f, T["accent_hi"] if i == 0 else T["text3"]); x += tw (tr (s), f) + u (12)
	b_pill (img, x, y, u (16), "R")
	s = u (112); gap = u (12); ty = u (80); isz = u (64)
	for i, a in enumerate (RECENT):
		x = x0 + i * (s + gap)
		if x > W: break
		rrect (img, x, ty, s, s, u (16), T["bg2"] if i else T["bg3"])
		ic = mc.app_icon (a, isz); img.paste (ic, (x + (s - isz) // 2, ty + (s - isz) // 2), ic)
		if i == 0: ring (img, x - u (4), ty - u (4), s + u (8), s + u (8), u (20), T["accent"], t = max (2, u (2.4)))
		if i < 3: rrect (img, x + s - u (16), ty + u (10), u (6), u (6), u (3), T["accent"])
	text (img, x0, u (204), aname (RECENT[0]), c.f (20, "bold"), T["text1"])
	text (img, x0, u (232), "%s  ·  %s 12:31" % (tr ("Open"), tr ("last used at")), c.f (12), T["text2"])
	text_r (img, W - u (20), u (210), u (20), "1 / %d" % len (RECENT), c.f (12), T["text3"])
	gl (img, x0, u (263), mc.g_search, u (12), T["text3"]); text_l (img, x0 + u (18), u (259), u (20), tr ("Type to search"), c.f (12), T["text3"])
	text_r (img, W - u (20), u (259), u (20), tr ("3 open"), c.f (12), T["text3"])
	hints (c, [(["dpad-h"], tr ("choose")), (["A"], tr ("open")), (["Y"], tr ("options")), (["Start"], tr ("tasks"))], H - u (31), align = "left", x = x0, size = 12)
	return img

def d2_terminal (k = 1.5, px = 18):
	c = Ctx (k, bg = T["term"]); img = c.img; u = c.u
	sh = u (18)
	text_l (img, u (14), u (4), sh, "SD:/", c.f (11, "bold"), T["text2"]); text_l (img, u (14) + tw ("SD:/", c.f (11, "bold")) + u (10), u (4), sh, "1 / 3", c.f (11), T["text3"])
	text_r (img, W - u (14), u (4), sh, "12:34", c.f (11), T["text3"])
	cols, rows, cw, ch = term_grid (img, u (8), u (4) + sh + u (2), W - u (16), H - u (4) - sh - u (2) - u (6), px)
	return img, (cols, rows, cw, ch)

def d2_menu (level = "items"):
	"""Select: the app's menus as ONE column that drills in place (what console mode does today, flat)."""
	c = Ctx (1.5); img = c.img; u = c.u
	text_l (img, u (14), u (4), u (20), "letters-tour.rtf", c.f (12, "bold"), T["text1"])
	zoom = (W - u (16)) / 794.0
	page (img, u (8), u (28), W - u (16), H - u (28), zoom)
	img.paste (Image.blend (img, Image.new ("RGB", img.size, T["bg0"]), 0.62))
	sw = u (236); x = 0
	box (img, x, 0, sw, H, T["bg1"]); vline (img, x + sw, 0, H, T["line"])
	rh = u (31); y = u (40)
	if level == "titles":
		text_l (img, x + u (20), u (10), u (22), "Letters", c.f (15, "bold"), T["text1"]); text_l (img, x + u (20) + tw ("Letters", c.f (15, "bold")) + u (8), u (10), u (22), "Menu", c.f (12), T["text2"])
		for i, m in enumerate (LETTERS_MENUS):
			sel = m == "Format"
			if sel: rrect (img, x + u (8), y, sw - u (16), rh, u (8), T["accent"])
			text_l (img, x + u (20), y, rh, m, c.f (15, "bold" if sel else "regular"), (255, 255, 255) if sel else T["text1"])
			gl (img, x + sw - u (26), y + rh // 2 - u (5), mc.g_chev, u (10), (255, 255, 255) if sel else T["text3"], dirn = "right"); y += rh + u (2)
		hints (c, [(["A"], "open"), (["B"], "close")], H - u (27), align = "left", x = sw + u (16), size = 12)
	else:
		gl (img, x + u (14), u (15), mc.g_chev, u (11), T["text2"], dirn = "left")
		text_l (img, x + u (30), u (10), u (22), "Format", c.f (15, "bold"), T["text1"]); text_l (img, x + u (30) + tw ("Format", c.f (15, "bold")) + u (8), u (10), u (22), "Letters", c.f (12), T["text2"])
		for r in [e for e in FORMAT_MENU if e][:8]:
			name, key, on = r; sel = name == "Italic"
			if sel: rrect (img, x + u (8), y, sw - u (16), rh, u (8), T["accent"])
			text_l (img, x + u (20), y, rh, mc.ellipsize (name, c.f (15), sw - u (96)), c.f (15, "bold" if sel else "regular"), (255, 255, 255) if sel else T["text1"])
			if key == ">": gl (img, x + sw - u (26), y + rh // 2 - u (5), mc.g_chev, u (10), T["text3"], dirn = "right")
			elif key: text_r (img, x + sw - u (18), y, rh, key, c.f (11), (226, 236, 250) if sel else T["text3"])
			if on: rrect (img, x + u (11), y + rh // 2 - u (2), u (5), u (5), u (3), T["accent_hi"])
			y += rh + u (2)
		hints (c, [(["A"], "do it"), (["B"], "back")], H - u (27), align = "left", x = sw + u (16), size = 12)
	return img

def d2_letters ():
	c = Ctx (1.5); img = c.img; u = c.u
	text_l (img, u (14), u (4), u (20), "letters-tour.rtf", c.f (12, "bold"), T["text1"])
	text_r (img, W - u (14), u (4), u (20), "1 / 2   ·   fit width", c.f (12), T["text2"])
	zoom = (W - u (16)) / 794.0
	page (img, u (8), u (28), W - u (16), H - u (28), zoom)
	bh = u (40); by = H - bh - u (10); bx = u (14); bw = W - u (28)
	rrect (img, bx, by, bw, bh, u (12), T["bg1"]); ring (img, bx, by, bw, bh, u (12), T["line"])
	x = bx + u (8); y = by + u (6); s = u (28)
	x += combo (c, x, y, u (86), s, "Title") + u (6)
	x += combo (c, x, y, u (50), s, "28") + u (8)
	for i, (lab, on) in enumerate ((("B", True), ("I", False), ("U", False))):
		if i == 0: ring (img, x - 2, y - 2, s + 4, s + 4, u (9), T["accent"], t = 2)
		x += tool (c, x, y, s, lab, on) + u (2)
	x += u (4); tool (c, x, y, s, "A"); box (img, x + u (8), y + s - u (6), s - u (16), max (2, u (2)), T["danger"]); x += s + u (4)
	rrect (img, bx + bw - u (8) - s, y, s, s, u (8), T["bg2"]); text_c (img, bx + bw - u (8) - s, y - 1, s, s, "»", c.f (15, "bold"), T["text1"])
	d2_letters.zoom = zoom
	return img

def d2_switcher (shots):
	c = Ctx (1.5); img = c.img; u = c.u
	text (img, u (20), u (10), "Open apps", c.f (22, "bold"), T["text1"]); text_r (img, W - u (20), u (16), u (20), "2 / 3", c.f (12), T["text3"])
	cw, chh = u (236), u (177); cx = (W - cw) // 2; cy = u (52)
	sw2, sh2 = u (196), u (147)
	for im, x in ((shots[0], cx - sw2 - u (14)), (shots[2], cx + cw + u (14))):
		t, m = thumb (im, sw2, sh2, u (10)); t = Image.blend (t, Image.new ("RGB", t.size, T["bg0"]), 0.55); img.paste (t, (x, cy + (chh - sh2) // 2), m)
	t, m = thumb (shots[1], cw, chh, u (12)); img.paste (t, (cx, cy), m)
	ring (img, cx - u (4), cy - u (4), cw + u (8), chh + u (8), u (16), T["accent"], t = max (2, u (2.4)))
	f = c.f (18, "bold"); lw = u (30) + tw ("Text Editor", f); lx = (W - lw) // 2
	mc.draw_icon (img, "tinypad", lx, cy + chh + u (14), u (22)); text_l (img, lx + u (30), cy + chh + u (12), u (26), "Text Editor", f, T["text1"])
	hints (c, [(["dpad-h"], "choose"), (["A"], "switch"), (["X"], "close it"), (["B"], "stay")], H - u (31), align = "centre", size = 12)
	return img

def d2_quick ():
	c = Ctx (1.5); img = c.img; u = c.u
	text (img, u (20), u (10), "Quick settings", c.f (22, "bold"), T["text1"])
	f2 = c.f (12); text_r (img, W - u (20), u (16), u (20), "78 %  ·  about 3 h 10", f2, T["text2"])
	mc.battery (img, W - u (20) - tw ("78 %  ·  about 3 h 10", f2) - u (30), u (21), 78, T["success"], k = 1.5)
	y = u (56); w = W - u (40)
	for i, (fn, lab, v, val) in enumerate (((mc.g_speaker, "Volume", 0.7, "7"), (mc.g_sun, "Brightness", 0.55, "55 %"))):
		yy = y + i * u (62); foc = i == 0
		rrect (img, u (20), yy, w, u (54), u (12), T["bg1"])
		if foc: ring (img, u (20) - 2, yy - 2, w + 4, u (54) + 4, u (13), T["accent"], t = max (2, u (2)))
		gl (img, u (32), yy + u (10), fn, u (14), T["text1"]); text_l (img, u (54), yy + u (6), u (22), lab, c.f (13, "bold"), T["text1"])
		text_r (img, u (20) + w - u (12), yy + u (6), u (22), val, c.f (13), T["text2"])
		bx, bw, bh = u (32), w - u (24), u (10)
		rrect (img, bx, yy + u (34), bw, bh, bh // 2, T["bg3"]); rrect (img, bx, yy + u (34), int (bw * v), bh, bh // 2, T["accent"])
	tw3 = (w - u (16)) // 3; ty = y + u (126)
	for i, (fn, lab, sub, on) in enumerate (((mc.g_wifi, "Wi-Fi", "Maison", True), (mc.g_dnd, "Quiet", "off", False), (mc.g_lock, "Lock", "", False))):
		x = u (20) + i * (tw3 + u (8)); rrect (img, x, ty, tw3, u (58), u (12), T["accent"] if on else T["bg2"])
		gl (img, x + u (10), ty + u (9), fn, u (13), (255, 255, 255) if on else T["text1"])
		text (img, x + u (10), ty + u (30), lab, c.f (13, "bold"), (255, 255, 255) if on else T["text1"])
		if sub: text_r (img, x + tw3 - u (10), ty + u (32), u (16), sub, c.f (11), (226, 236, 250) if on else T["text2"])
	hints (c, [(["dpad-v"], "move"), (["dpad-h"], "adjust"), (["A"], "switch"), (["B"], "close")], H - u (31), align = "centre", size = 12)
	return img

def d2_dialog (under):
	c = Ctx (1.5); img = c.img; u = c.u
	img.paste (Image.blend (under, Image.new ("RGB", under.size, T["bg0"]), 0.7))
	y = u (112)
	rrect (img, 0, y, W, H - y + 30, u (18), T["bg1"]); hline (img, u (18), y, W - u (36), T["line"])
	text (img, u (22), y + u (16), "Save the changes?", c.f (20, "bold"), T["text1"])
	text (img, u (22), y + u (46), "pocketterm.txt was changed 4 minutes ago.", c.f (12), T["text2"])
	for i, (g, lab, d) in enumerate ((("A", "Save", True), ("X", "Don't save", False), ("B", "Cancel", False))):
		dialog_btn (c, u (22), y + u (72) + i * u (43), W - u (44), u (38), g, lab, d)
	return img

# ============================================== the device's face =============================================
def mm (v): return int (round (v * PXMM))
KEYS = ["` 1 2 3 4 5 6 7 8 9 0 - = ⌫", "Tab Q W E R T Y U I O P [ ] \\", "Caps A S D F G H J K L ; ' ↵",
	"⇧ Z X C V B N M , . / ↑ ⇧", "Fn Ctrl Alt __ __ __ Alt CMD Fn ← ↓ →"]
def device (screen, highlight = ()):
	"""The PocketTerm35's face at the screen's own scale (the screen's pixels are not resampled).
	Returns the picture and the screen's place in it."""
	fw, fh = mm (FACE_MM[0]), mm (FACE_MM[1])
	img = Image.new ("RGB", (fw + 40, fh + 40), (236, 237, 240)); ox = oy = 20
	rrect (img, ox, oy, fw, fh, mm (9), (22, 23, 26)); ring (img, ox, oy, fw, fh, mm (9), (52, 54, 60), t = 2)
	for i in range (4): rrect (img, ox + fw // 2 - mm (9) + i * mm (6) - 3, oy + mm (6.5), 7, 7, 3.5, (60, 200, 110) if i == 0 else (46, 48, 54))
	gx, gy, gw, gh = ox + (fw - mm (80)) // 2, oy + mm (11), mm (80), mm (61)
	rrect (img, gx, gy, gw, gh, mm (6), (4, 4, 5)); ring (img, gx, gy, gw, gh, mm (6), (40, 42, 48))
	sx, sy = gx + (gw - W) // 2, gy + (gh - H) // 2
	m = mask (W, H, lambda d, s: d.rounded_rectangle ([0, 0, W * s - 1, H * s - 1], GLASS_R * s, fill = 255))
	img.paste (screen, (sx, sy), m)
	body2, cap, leg = (34, 36, 41), (48, 50, 57), (176, 180, 190)
	hi = T["accent"]
	def lit (name): return name in highlight
	# the D-pad
	cx, cy, a = ox + mm (19), oy + mm (88), mm (5.2)
	glyph (img, cx - int (a * 1.5), cy - int (a * 1.5), a * 3, a * 3, lambda d, s: (d.rounded_rectangle ([0, a * s, 3 * a * s, 2 * a * s], a * s * 0.25, fill = 255), d.rounded_rectangle ([a * s, 0, 2 * a * s, 3 * a * s], a * s * 0.25, fill = 255)), hi if lit ("dpad") else cap)
	# L, R
	for i, lab in enumerate (("L", "R")):
		x = ox + mm (36.5) + i * mm (11); rrect (img, x, oy + mm (84), mm (9), mm (5.5), mm (1.6), hi if lit (lab) else cap); text_c (img, x, oy + mm (84), mm (9), mm (5.5), lab, cfont (mm (3.2), "bold"), leg)
	# X (top) Y (left) A (right) B (bottom)
	fx, fy, d, r = ox + mm (75), oy + mm (88), mm (6.4), mm (7.2)
	for lab, dx, dy in (("X", 0, -1), ("Y", -1, 0), ("A", 1, 0), ("B", 0, 1)):
		x, y = fx + dx * r - d // 2, fy + dy * r - d // 2
		rrect (img, x, y, d, d, d / 2, hi if lit (lab) else cap); text_c (img, x, y - 1, d, d, lab, cfont (mm (3.4), "bold"), (255, 255, 255) if lit (lab) else leg)
	for i, lab in enumerate (("Select", "Start")):
		x = ox + mm (36) + i * mm (11.5); rrect (img, x, oy + mm (96), mm (10), mm (3.6), mm (1.8), hi if lit (lab) else cap)
		text_c (img, x, oy + mm (99.6), mm (10), mm (4), lab.upper (), cfont (mm (1.9), "bold"), (130, 134, 144))
	# the keyboard
	ky = oy + mm (110); rowh = mm (10.2); kf = font (mm (2.2))
	for r_, row in enumerate (KEYS):
		ks = row.split (" "); n = len (ks); pitch = (fw - mm (7)) / n; x0 = ox + mm (3.5)
		i = 0
		while i < n:
			span = 1
			if ks[i] == "__":
				while i + span < n and ks[i + span] == "__": span += 1
			x = int (x0 + i * pitch); wk = int (pitch * span) - mm (1.1)
			rrect (img, x, ky + r_ * rowh, wk, rowh - mm (2.4), mm (1.4), hi if lit ("key:" + ks[i]) else body2)
			if ks[i] != "__": text_c (img, x, ky + r_ * rowh - 1, wk, rowh - mm (2.4), ks[i], kf, (255, 255, 255) if lit ("key:" + ks[i]) else leg)
			i += span
	return img, (sx, sy)

def glass (screen, highlight = (), below = 34.0):
	"""The device's top: the glass and the buttons under it (the keyboard cut off)."""
	img, (sx, sy) = device (screen, highlight)
	return img.crop ((0, 0, img.width, 20 + mm (72 + below))), (sx, sy)

# ============================================== the sheets ====================================================
TAGS = {"theme": ("THEME", T["success"]), "shell": ("SHELL", T["warning"]), "app": ("APP", (236, 140, 70)),
	"scale": ("SCALE 1.5x", T["danger"]), "uikit": ("UIKIT", T["accent_hi"])}
INK, INK2 = (28, 30, 34), (96, 100, 108)
def chip (img, x, y, tag):
	lab, col = TAGS[tag]; f = cfont (12, "bold"); w = tw (lab, f) + 14
	rrect (img, x, y, w, 20, 5, col); text_c (img, x, y - 1, w, 20, lab, f, (16, 16, 18)); return w + 6
def legend (img, x, y):
	f = cfont (14)
	for tag, word in (("theme", "colours, radii, fonts: one theme change, every app inherits it (cheap)"),
			  ("shell", "the pocket shell's own layout or input (moderate)"), ("uikit", "a UIKit widget's behaviour (moderate, once for all apps)"),
			  ("app", "that app's own work"), ("scale", "needs the unbuilt 1.5x scaling (expensive)")):
		x += chip (img, x, y, tag); text_l (img, x, y, 20, word, f, INK2); x += tw (word, f) + 22

def sheet (name, title, sub, items, cols, in_device = False, zoom = 1.0, notes = ()):
	"""items: [(screen, caption, tags, extra line)]."""
	cells = []
	for im, cap, tags, extra in items:
		pic = glass (im)[0] if in_device else im
		if zoom != 1.0: pic = pic.resize ((int (pic.width * zoom), int (pic.height * zoom)), Image.LANCZOS)
		cells.append ((pic, cap, tags, extra))
	cw = max (p.width for p, _, _, _ in cells); chh = max (p.height for p, _, _, _ in cells)
	gap, top, cap_h = 36, 132, 106
	rows = (len (cells) + cols - 1) // cols
	sw = 60 + cols * cw + (cols - 1) * gap + 60; sh = top + rows * (chh + cap_h + 22) + 24 + 24 * len (notes)
	sw = max (sw, 1880)
	img = Image.new ("RGB", (sw, sh), (244, 245, 247))
	text (img, 60, 30, title, cfont (30, "bold"), INK); text (img, 60, 70, sub, cfont (16), INK2)
	legend (img, 60, 100)
	for i, (pic, cap, tags, extra) in enumerate (cells):
		x = 60 + (i % cols) * (cw + gap); y = top + (i // cols) * (chh + cap_h + 22)
		img.paste (pic, (x, y))
		if not in_device: ring (img, x - 1, y - 1, pic.width + 2, pic.height + 2, 0, (200, 202, 208))
		text (img, x, y + chh + 8, cap, cfont (17, "bold"), INK)
		xx = x
		for t in tags: xx += chip (img, xx, y + chh + 33, t)
		if extra:
			words = extra.split (" "); lines = [""]
			for wd in words:
				if tw ((lines[-1] + " " + wd).strip (), cfont (13)) > cw: lines.append (wd)
				else: lines[-1] = (lines[-1] + " " + wd).strip ()
			for j, ln in enumerate (lines[:3]): text (img, x, y + chh + 56 + j * 17, ln, cfont (13), INK2)
	for i, s in enumerate (notes): text (img, 60, sh - 24 * (len (notes) - i) - 12, s, cfont (15), INK2)
	img.save (os.path.join (OUT, name)); return img

def token_table ():
	rows = [("bg0", "the screen: continues the black glass"), ("bg1", "a surface: a card, a panel, a sheet"), ("bg2", "raised: a field, an icon's plate, a menu"),
		("bg3", "chosen / pressed / a chip, a key's face"), ("line", "a 1 px separator, a hairline"), ("text1", "body text"), ("text2", "secondary text"),
		("text3", "a placeholder, disabled"), ("accent", "Aqua: the focus ring, the default button, on"), ("accent_hi", "the accent as text on dark"),
		("danger", "destructive, an error"), ("success", "done, the battery"), ("warning", "attention")]
	w, h = 1500, 150 + len (rows) * 44 + 150
	img = Image.new ("RGB", (w, h), (244, 245, 247))
	text (img, 60, 30, "The tokens: flat and dark", cfont (30, "bold"), INK)
	text (img, 60, 70, "Both directions use them. No gradient, no bevel, no gloss; a shadow nowhere -- a floating layer gets a 1 px hairline and a dimmed backdrop.", cfont (16), INK2)
	y = 120
	for name, what in rows:
		col = T[name]
		rrect (img, 60, y, 150, 34, 8, col); ring (img, 60, y, 150, 34, 8, (200, 202, 208))
		text_l (img, 230, y, 34, name, cfont (17, "bold"), INK); text_l (img, 350, y, 34, "#%02X%02X%02X" % col, font (15, False, True), INK2)
		text_l (img, 470, y, 34, what, cfont (16), INK)
		if name.startswith ("text") or name == "accent_hi":
			s = "on bg0 %.1f : 1     on bg1 %.1f : 1     on bg2 %.1f : 1" % (contrast (col, T["bg0"]), contrast (col, T["bg1"]), contrast (col, T["bg2"]))
			text_l (img, 900, y, 34, s, font (14, False, True), INK2)
		if name == "accent":
			text_l (img, 900, y, 34, "white on it %.1f : 1 (bold, 15 px and more only)   ring on bg0 %.1f : 1" % (contrast ((255, 255, 255), col), contrast (col, T["bg0"])), font (14, False, True), INK2)
		y += 44
	y += 14
	for s in ("Contrast (WCAG): body text needs 4.5 : 1, large or bold text and the focus ring 3 : 1. text3 is for placeholders only.",
		  "Type: Selawik for the chrome (on the card: res/fonts), DejaVu Sans Mono for the Terminal and code -- anti-aliased TrueType everywhere, no bitmap face.",
		  "Direction 1 scale at 1x: 13 / 14 / 15 / 18 px.   Direction 2 scale at 1.5x: 11 / 12 / 13 / 15 / 20 / 26 lp = 17 / 18 / 20 / 23 / 30 / 39 px.",
		  "The apps' icons stay as they are, on a flat bg2 plate with a hairline (several are dark themselves: the Terminal's would vanish on bg0); the focus is a 2 px Aqua ring."):
		text (img, 60, y, s, cfont (15), INK2); y += 26
	img.save (os.path.join (OUT, "tokens.png")); return img

def toolbar_rule ():
	"""The responsive toolbar's rule, run on Letters' two toolbars at three widths."""
	w, h = 1500, 900
	img = Image.new ("RGB", (w, h), (244, 245, 247))
	text (img, 60, 30, "Toolbars adapt: one rule for every app (UIKit's ToolBar)", cfont (30, "bold"), INK)
	text (img, 60, 70, "Letters' two toolbars (%d tools) resolved by the rule below, in the flat dark style. Nothing is clipped: what leaves the row is in the overflow." % len ([t for t in LETTERS_STD + LETTERS_FMT if t["kind"] != "-"]), cfont (16), INK2)
	y = 120
	cases = [(800, 480, True, "800 x 480 (landscape, short): the two rows merge into one"), (640, 480, True, "640 x 480 (the PocketTerm35): one row"),
		 (480, 800, False, "480 x 800 (portrait, tall): two rows kept, each resolved")]
	for wd, hh, merged, cap in cases:
		text (img, 60, y, cap, cfont (17, "bold"), INK); y += 30
		rows = [LETTERS_MERGED] if merged else [LETTERS_STD, LETTERS_FMT]
		c = Ctx (); strip = Image.new ("RGB", (wd, 12 + 36 * len (rows)), T["bg0"]); c.img = strip
		notes = []
		for i, items in enumerate (rows):
			row, over = tb_resolve (items, wd - 28); tb_draw (c, 14, 9 + i * 36, row, over, right = wd - 14)
			notes.append ("%d shown, %d in the overflow%s" % (len ([t for t in row if t["kind"] != "-"]), len (over), ", %d combo folded to an icon" % len ([t for t in row if t.get ("folded")]) if any (t.get ("folded") for t in row) else ""))
		img.paste (strip, (60, y)); text (img, 60 + wd + 20, y + 10, " / ".join (notes), cfont (14), INK2); y += strip.height + 26
	y += 6
	for s2 in ("The rule, applied in this order while the row is too wide:",
		   "1.  the combo boxes (style, font, size, zoom) shrink to their least width;",
		   "2.  the tools of priority 3 move into the overflow \u00bb (from the row's end, keeping their order);",
		   "3.  the combo boxes of priority 2 and 3 fold to one icon that opens their list;",
		   "4.  the tools of priority 2 move into the overflow;",
		   "5.  last, the combo boxes of priority 1 fold to an icon (the size stays a number); the tools of priority 1 always stay.",
		   "Two toolbars merge into one row when the window is short (under 560 logical lines): priority 1 of both first, then the rest.",
		   "\u00bb is the row's last control: the arrows / D-pad reach it like any tool, A or Enter opens it; it lists the tools by NAME with their shortcut and state;",
		   "a folded or moved combo box is a sub-menu. Every tool also stays in the app's menus (Select), so nothing depends on the pointer.",
		   "The app only declares priorities and least widths (uikit/toolbar.h has priorities and an overflow in the pocket UIKit); the shrinking, the folding",
		   "and the merge are UIKit's, once for all apps. Letters must move its two hand-made rows to ToolBar: that is its own work."):
		text (img, 60, y, s2, cfont (15, "bold") if s2.endswith (":") else cfont (15), INK if s2.endswith (":") else INK2); y += 25
	img = img.crop ((0, 0, w, y + 30)); img.save (os.path.join (OUT, "toolbar-rule.png")); return img

def marker (img, x, y, n):
	rrect (img, x - 13, y - 13, 26, 26, 13, T["danger"]); ring (img, x - 13, y - 13, 26, 26, 13, (255, 255, 255), t = 2)
	text_c (img, x - 13, y - 14, 26, 26, str (n), cfont (15, "bold"), (255, 255, 255))

def audit_panel (shot, marks, notes, title, full = False, width = 1500):
	"""A real capture in the device, numbered marks on it, the notes beside."""
	pic, (sx, sy) = device (shot) if full else glass (shot)
	for n, (x, y) in enumerate (marks, 1): marker (pic, sx + x, sy + y, n)
	h = max (pic.height, 100 + 64 * len (notes)) + 70
	img = Image.new ("RGB", (width, h), (244, 245, 247))
	text (img, 40, 22, title, cfont (24, "bold"), INK)
	img.paste (pic, (30, 64))
	x = 30 + pic.width + 30; y = 84
	for n, (head, body) in enumerate (notes, 1):
		marker (img, x + 13, y + 13, n)
		text (img, x + 38, y, head, cfont (17, "bold"), INK)
		for j, line in enumerate (body.split ("\n")): text (img, x + 38, y + 25 + j * 20, line, cfont (14), INK2)
		y += 38 + 20 * (body.count ("\n") + 1) + 12
	return img

def stack (name, parts, head = None, sub = None):
	w = max (p.width for p in parts); top = 110 if head else 0
	img = Image.new ("RGB", (w, top + sum (p.height for p in parts)), (244, 245, 247))
	if head: text (img, 40, 26, head, cfont (30, "bold"), INK); text (img, 40, 68, sub, cfont (16), INK2)
	y = top
	for p in parts: img.paste (p, (0, y)); y += p.height
	img.save (os.path.join (OUT, name)); return img

def px_mm (px): return "%.1f mm" % (px * MM)

def audit ():
	real = lambda n: Image.open (os.path.join (REAL, n)).convert ("RGB")
	home = audit_panel (real ("pocketshell-home-640.png"),
		[(40, 14), (590, 14), (320, 58), (86, 208), (460, 394), (60, 440), (624, 468)],
		[("Light Milk chrome on a black body", "The silver bar, the white panel and the navy wallpaper make a bright rectangle in the black glass:\nthe screen reads as a window in the device, not as its face."),
		 ("The rounded glass clips the corners", "With a %d px (3 mm, assumed) corner the Onyx button's edge and the clock's last digit sit %s from the cut;\nnothing keeps a safe margin." % (GLASS_R, px_mm (6))),
		 ("Text is about 1 mm tall", "12 px labels have capitals of %s, the 13 px placeholder %s. A phone's body text is about 1.8 mm.\nReadable at 25 cm with good eyes, tiring beyond." % (px_mm (8.7), px_mm (9.5))),
		 ("Icons: 40 px = %s on a %s plate" % (px_mm (40), px_mm (60)), "Fine as D-pad targets, small as pictures; six columns leave the panel half empty.\nGradient plates, soft shadows and the glossy pill are the look the user no longer wants."),
		 ("The hints name keys this device hides", "Tab, the arrows and Alt+Tab: the arrows are on an Fn layer of the thumb keyboard, and the\nD-pad, A / B / X / Y, L / R, Start and Select just under the screen are never mentioned."),
		 ("Running: thumbnails %s tall" % px_mm (60), "Too small to recognise; the close crosses are %s wide and nothing can press them\n(no touch driver, no pointer)." % px_mm (16)),
		 ("Bottom corners", "The last thumbnail's label and its cross fall in the rounded corner's zone.")],
		"Pocket home (pocketshell-home-640.png) in the PocketTerm35, at the screen's own scale", full = True, width = 1900)
	term = audit_panel (real ("pocket-terminal-640.png"),
		[(60, 12), (100, 41), (120, 150), (12, 276), (620, 440)],
		[("An empty bar, a bitmap title", "The title is the kernel's 8 x 16 bitmap face, no menus are shown, the bar is beige: CDE's look, not Milk's."),
		 ("A dated tab strip", "A bevelled folder tab with a 9 px cross and a '+', %s tall, for one tab." % px_mm (30)),
		 ("8 x 16 cells = %s x %s" % (px_mm (8), px_mm (16)), "79 columns x 25 lines, capitals about 1.1 mm, thin aliased strokes: the densest and hardest text on the device."),
		 ("Square corners", "The first column starts 7 px from the edge: on the last lines its first character meets the rounded glass."),
		 ("A 4 px beige frame", "The window's face shows round the console: a light border against the black body.")],
		"Terminal (pocket-terminal-640.png)", width = 1900)
	parts = [home, term]
	cand = [os.path.join (OUT, "src", "letters-640x480.png")]
	for p in cand:
		if os.path.exists (p):
			parts.append (audit_panel (Image.open (p).convert ("RGB"),
				[(300, 12), (320, 58), (630, 74), (630, 104), (400, 290), (320, 468)],
				[("An almost empty top band", "24 lines for a gem: the app's name and its seven menus are not shown at this width."),
				 ("Chrome takes %d of 480 lines" % 150, "Two toolbar rows, the ruler and the status bar: 31 %% of the screen (%s of 53 mm) before the page." % px_mm (150)),
				 ("The second toolbar is cut", "The colour buttons run off the right edge with no overflow button: what is beyond cannot be reached."),
				 ("Bitmap text in the combo boxes", "'Title', 'Liberation Sans', '28', the ruler and the status bar use the 8 x 16 face; buttons are %s." % px_mm (24)),
				 ("The page at 100 % is wider than the screen", "The line is cut at 'AbiWo...': reading means scrolling sideways at every line."),
				 ("CDE beige, bevels, a grey desk", "None of it matches a matte black handheld.")],
				"Letters, the heavy app (the user's capture, pocket mode, 640 x 480)", width = 1900))
	stack ("audit.png", parts, "Today's PocketUI on the PocketTerm35: what does not fit",
	       "The real captures, pixel for pixel, behind the device's rounded glass. 640 px across 71 mm: %.0f ppi, one pixel is %.3f mm." % (PPI, MM))
	sw = audit_panel (real ("pocketshell-switcher-640.png"), [(320, 52), (120, 240), (280, 452)],
		[("A headline over the app's chrome", "White bold text laid on the dimmed tab strip; the menu bar stays lit above."),
		 ("Side cards %s wide" % px_mm (182), "Their content is unreadable; the centre card's teal frame and gloss are Milk's."),
		 ("Alt+Tab, Shift+Tab, Del, Esc", "Keyboard only, and Del is not a key of this keyboard's base layer.")], "Switcher (pocketshell-switcher-640.png)", width = 1900)
	ch = audit_panel (real ("consolehome-tiles-light-home-640-fr.png"), [(100, 130), (320, 326), (500, 456)],
		[("Console tiles (light): the right structure", "One row, big pictures, L / R categories and A / B hints: this already fits the D-pad. It is light, though."),
		 ("Category discs 46 px = %s" % px_mm (46), "Good targets; the labels under them are 12 px."),
		 ("A and B, but no X / Y / Start / Select", "The hints use two of the device's ten controls.")], "Console home, light tiles (consolehome-tiles-light-home-640-fr.png)", width = 1900)
	cs = audit_panel (real ("consolehome-set-sound-640-fr.png"), [(300, 118), (30, 200), (540, 466)],
		[("The settings rows work", "Value rows driven by the D-pad are what this device needs; the help line is 10 px (%s capitals)." % px_mm (7.3)),
		 ("XMB wave, gradients", "The PS2 mood is a gradient blue wallpaper and glassy rows: the opposite of flat and dark."),
		 ("A dark hint bar already", "Its shape is right; it should melt into the glass instead of being a navy band.")], "Console settings, Sound (consolehome-set-sound-640-fr.png)", width = 1900)
	stack ("audit-2.png", [sw, ch, cs], "Today's PocketUI on the PocketTerm35 (2): the switcher and the console home", "Same scale. The console home's structure is the closest to what the device asks for.")

def true_size (items):
	dpi = 96.0; rw = int (round (SCREEN_MM[0] / 25.4 * dpi)); rh = int (round (SCREEN_MM[1] / 25.4 * dpi))
	w = 60 + 640 + 60 + rw + 420; h = 150 + len (items) * (480 + 70) + 60
	img = Image.new ("RGB", (w, h), (244, 245, 247))
	text (img, 60, 28, "True size", cfont (30, "bold"), INK)
	text (img, 60, 68, "Left: the 640 x 480 picture as a monitor shows it. Right: the same at the screen's real size, %d x %d mm, when this" % SCREEN_MM, cfont (16), INK2)
	text (img, 60, 90, "picture is viewed at 100 % on a 96 dpi monitor (Windows scaling at 100 %) or printed at 96 dpi. Check with the ruler.", cfont (16), INK2)
	y = 150
	for im, cap in items:
		img.paste (im, (60, y)); ring (img, 59, y - 1, 642, 482, 0, (200, 202, 208))
		x = 60 + 640 + 60
		small = im.resize ((rw, rh), Image.LANCZOS); m = mask (rw, rh, lambda d, s: d.rounded_rectangle ([0, 0, rw * s - 1, rh * s - 1], GLASS_R * rw / 640 * s, fill = 255))
		rrect (img, x - 14, y + 120 - 14, rw + 28, rh + 28, 22, (10, 10, 12)); img.paste (small, (x, y + 120), m)
		text (img, x + rw + 34, y + 130, cap, cfont (18, "bold"), INK)
		text (img, 60, y + 488, cap + " -- 640 x 480", cfont (14), INK2)
		y += 550
	x = 60 + 640 + 60; yy = 150 + 40
	n = int (round (50 / 25.4 * dpi)); box (img, x, yy, n, 2, INK)
	for i in range (0, 51, 10): xx = x + int (round (i / 25.4 * dpi)); box (img, xx, yy - 8, 1, 10, INK); text (img, xx - 4, yy + 6, str (i), cfont (12), INK2)
	text (img, x + n + 14, yy - 10, "50 mm", cfont (14, "bold"), INK)
	img.save (os.path.join (OUT, "true-size.png"))

def button_map ():
	blank = Image.new ("RGB", (W, H), T["bg0"])
	c = Ctx (); gl (c.img, W // 2 - 20, 170, mc.g_gem, 40, T["accent"]); text_c (c.img, 0, 230, W, 40, "Onyx", cfont (30, "bold"), T["text1"])
	pic, _ = device (c.img, highlight = ("dpad", "A", "B", "X", "Y", "L", "R", "Start", "Select"))
	rows = [("", "On the home and in the shell's screens", "In an app"),
		("dpad", "move the focus", "the arrows: the focus, the caret, a list"),
		("A", "open / confirm (Enter)", "Enter: activate the focused control"),
		("B", "back / cancel / close a sheet (Esc)", "Esc: close a menu, a dialog; back"),
		("X", "second action: close the chosen app, remove", "the app's second action; the format bar (Letters)"),
		("Y", "options of the chosen item; search (home)", "the context menu (Menu key, Shift+F10)"),
		("L", "previous section / category / tab", "Page Up; the previous tab or menu"),
		("R", "next section / category / tab", "Page Down; the next tab or menu"),
		("Start", "the open apps (tasks)", "Home; held: the open apps"),
		("Select", "(no menus on the home)", "the MENU entry of the top band: focus it, unfold it")]
	menu = ["Select (or Alt tapped alone, or F10 = Fn + 0) puts the focus on the Menu entry and unfolds the app's menus as a column.",
		"Up / Down move; Right or A opens a menu or a sub-menu; A or Enter does the item; B, Esc or Left goes back one level;",
		"Select again (or B at the first level) closes it and gives the focus back to the app. The same in console mode."]
	chords = [("CMD", "Home (Super)"), ("Alt + Tab", "the next app; Alt held: the switcher"), ("CMD + Left / Right", "two apps side by side (640 px: 2 x 320)"), ("Ctrl + arrows", "move the pointer (the user's plan); Ctrl + Enter: click"),
		  ("Alt alone, F10", "the Menu entry, as Select (F10 is Fn + 0)"), ("typing on the home", "search apps, files, settings")]
	w = pic.width + 1220; h = max (pic.height + 130, 1160)
	img = Image.new ("RGB", (w, h), (244, 245, 247))
	text (img, 40, 26, "The PocketTerm35's buttons in PocketUI: a proposal", cfont (30, "bold"), INK)
	text (img, 40, 68, "Console mode's conventions (A confirms, B goes back, Y options, X second action, L / R sections), extended to pocket mode. Every action also has a key.", cfont (16), INK2)
	img.paste (pic, (20, 110))
	x = pic.width + 60; y = 130; cws = (150, 470, 500)
	for i, (g, a, b) in enumerate (rows):
		if i == 0:
			text (img, x + cws[0], y, a, cfont (17, "bold"), INK); text (img, x + cws[0] + cws[1], y, b, cfont (17, "bold"), INK); y += 40; continue
		rrect (img, x - 14, y - 8, sum (cws) + 28, 50, 10, T["bg1"])
		draw_glyph (img, x, y + 4, 26, g)
		text_l (img, x + cws[0], y, 34, a, cfont (16), T["text1"]); text_l (img, x + cws[0] + cws[1], y, 34, b, cfont (16), T["text1"])
		y += 58
	rrect (img, x - 14, y - 4, sum (cws) + 28, 92, 10, T["accent"])
	for j, line in enumerate (menu): text (img, x, y + 6 + j * 26, line, cfont (15, "bold" if j == 0 else "regular"), (255, 255, 255))
	y += 104; text (img, x, y, "Key chords (the thumb keyboard)", cfont (17, "bold"), INK); y += 36
	for k2, what in chords:
		rrect (img, x - 14, y - 6, sum (cws) + 28, 40, 10, (232, 234, 238))
		text_l (img, x, y, 28, k2, cfont (16, "bold"), INK); text_l (img, x + 250, y, 28, what, cfont (16), INK); y += 46
	text (img, x, y + 10, "Assumed: the buttons reach Onyx as a gamepad or as keys (not checked on the device); CMD is the Super key; the arrows are Fn + keys.", cfont (14), INK2)
	img.save (os.path.join (OUT, "button-map.png"))

def main ():
	os.makedirs (OUT, exist_ok = True)
	save = lambda im, n: (im.save (os.path.join (OUT, n)), im)[1]
	token_table ()
	# ---- direction 1
	h1 = save (n1_home (), "noir-home.png"); t1 = save (n1_terminal (), "noir-terminal.png"); e1 = save (n1_editor (), "noir-editor-menu.png")
	l1 = save (n1_letters (), "noir-letters.png"); lf1 = save (n1_letters (menu = "focus"), "noir-menu-focus.png")
	lm1 = save (n1_letters (open_menu = True), "noir-menu-open.png"); lo1 = save (n1_letters (overflow = True), "noir-letters-overflow.png")
	toolbar_rule ()
	c0 = Ctx (); status (c0, "Text Editor", menu = "idle"); text_l (c0.img, 22, 36, 22, "SD:/docs/pocketterm.txt", c0.f (13), T["text2"]); editor_body (c0, 62); ed_plain = c0.img
	s1 = save (n1_switcher ((t1, ed_plain, l1)), "noir-switcher.png"); q1 = save (n1_quick (h1), "noir-quick.png")
	d1 = save (n1_dialog (ed_plain), "noir-dialog.png"); k1 = save (lock (Ctx ()), "noir-lock.png")
	LANG[0] = "fr"; f1 = save (n1_home (), "noir-home-fr.png"); LANG[0] = "en"
	g = n1_terminal.grid
	n1 = [(h1, "Home", ("theme", "shell"), "the shell's layout kept: status, search, chips (L / R), the grid, running; hints for this device"),
	      (t1, "Terminal: %d x %d" % g[:2], ("theme", "app"), "DejaVu Sans Mono 15 px, cells %d x %d px (%s x %s); flat tabs; today 79 x 25 at 8 x 16" % (g[2], g[3], px_mm (g[2]), px_mm (g[3]))),
	      (l1, "Letters: the toolbars resolved", ("theme", "uikit", "app"), "%d tools in the row, %d in the overflow; fit width %d %%; ruler off; the page stays white" % (len ([t for t in n1_letters.row if t["kind"] != "-"]), len (n1_letters.over), round (n1_letters.zoom * 100))),
	      (lf1, "Select: the focus on Menu", ("theme", "shell"), "one Menu entry in the band (Alt alone or F10 on the keyboard)"),
	      (lm1, "The menus unfolded", ("theme", "uikit"), "Letters' eight menus as a column; Format open: checks, shortcuts, sub-menu arrows"),
	      (lo1, "The toolbar's overflow, open", ("theme", "uikit"), "the tools that left the row, by name; combos as sub-menus"),
	      (e1, "Alternative: the titles in the band", ("theme",), "kept for an app of three or four menus; not the default"),
	      (s1, "Task switcher", ("theme", "shell"), "the same three cards, flat; X closes (Del today)"),
	      (q1, "Quick settings", ("theme", "shell"), "brightness needs the backlight's control (not in Onyx today)"),
	      (d1, "A dialog", ("theme", "uikit"), "the device's buttons drawn on the dialog's buttons"),
	      (k1, "Lock / boot splash", ("shell",), "black to the glass' edge: the device looks off but for the gem and the time"),
	      (f1, "Home in French", ("theme", "shell"), "the words fit")]
	sheet ("noir-sheet.png", "Direction 1 -- Onyx Noir: today's pocket shell, flat and dark, at 1x", "The exact 640 x 480 pictures. Same structure as the built pocketshell; the type goes from 12-13 px to 14-15 px, the chrome melts into the glass.", n1, 4)
	sheet ("noir-device.png", "Direction 1 -- Onyx Noir, in the PocketTerm35", "Behind the rounded glass, half size. 14 to 22 px of safe margin keep everything clear of the corners.", n1[:12], 4, in_device = True, zoom = 0.5)
	# ---- direction 2
	h2 = save (d2_home (1.5), "deck-home.png"); h2x1 = save (d2_home (1.0), "deck-home-1x.png")
	t2, g2 = d2_terminal (1.5, 18); save (t2, "deck-terminal.png"); t2x1, g2x1 = d2_terminal (1.0, 13); save (t2x1, "deck-terminal-1x.png")
	m2 = save (d2_menu ("items"), "deck-menu-items.png"); m2t = save (d2_menu ("titles"), "deck-menu-titles.png"); l2 = save (d2_letters (), "deck-letters.png")
	c2 = Ctx (1.5); text_l (c2.img, c2.u (14), c2.u (4), c2.u (20), "Text Editor", c2.f (12, "bold"), T["text1"]); editor_body (c2, c2.u (28), px = 18, lines = EDIT[:9]); ed2 = c2.img
	s2 = save (d2_switcher ((t2, ed2, l2)), "deck-switcher.png"); q2 = save (d2_quick (), "deck-quick.png")
	dd2 = save (d2_dialog (ed2), "deck-dialog.png"); k2 = save (lock (Ctx (1.5)), "deck-lock.png") if False else save (deck_lock (), "deck-lock.png")
	LANG[0] = "fr"; f2 = save (d2_home (1.5), "deck-home-fr.png"); LANG[0] = "en"
	n2 = [(h2, "Home: one shelf, one row", ("shell", "scale"), "pocket and console homes merged; tiles %s; L / R change the shelf; typing searches" % px_mm (168)),
	      (t2, "Terminal: %d x %d" % g2[:2], ("app", "scale"), "no chrome but a line; 18 px, cells %d x %d px (%s x %s)" % (g2[2], g2[3], px_mm (g2[2]), px_mm (g2[3]))),
	      (m2t, "Select: the app's menus, one column", ("uikit", "scale"), "console mode's menu (built), flat: Letters' eight menus; rows %s" % px_mm (46)),
	      (m2, "A menu drilled in place", ("uikit", "scale"), "427 lp is too narrow for two columns: B goes back to the titles"),
	      (l2, "Letters: the page; X shows the format bar", ("app", "uikit", "scale"), "no toolbar until asked; fit width %d %%; the page stays white" % round (d2_letters.zoom * 100)),
	      (s2, "Task switcher: one card", ("shell", "scale"), "the chosen app at %s wide, its neighbours peeking" % px_mm (354)),
	      (q2, "Quick settings: a full screen", ("shell", "scale"), "two sliders, three switches; the battery in the title line"),
	      (dd2, "A dialog: a bottom sheet", ("uikit", "scale"), "one button per row, each with its device button"),
	      (k2, "Lock / boot splash", ("shell",), "the time at %s tall" % px_mm (90)),
	      (f2, "Home in French", ("shell", "scale"), "the words fit")]
	sheet ("deck-sheet.png", "Direction 2 -- Onyx Deck: one thing per screen, drawn at 1.5x (427 x 320 logical)", "The exact 640 x 480 pictures. A carousel home for the D-pad, apps without chrome, menus and formatting on demand.", n2, 4)
	sheet ("deck-device.png", "Direction 2 -- Onyx Deck, in the PocketTerm35", "Behind the rounded glass, half size.", n2[:9], 4, in_device = True, zoom = 0.5)
	sheet ("deck-1x-vs-1.5x.png", "Direction 2 at 1x and at 1.5x: the trade-off", "The same layout rules at two scales. 1.5x is what the eye wants at %.0f ppi; 1x is what Onyx can draw today." % PPI,
	       [(h2x1, "Home at 1x (640 x 480 lp)", ("shell",), "five tiles of %s; 12-13 px text, capitals about %s" % (px_mm (112), px_mm (9))),
		(h2, "Home at 1.5x (427 x 320 lp)", ("shell", "scale"), "three tiles of %s; 18-20 px text, capitals about %s" % (px_mm (168), px_mm (13.5))),
		(t2x1, "Terminal at 1x: %d x %d" % g2x1[:2], ("app",), "13 px, cells %d x %d px: today's density, anti-aliased" % g2x1[2:]),
		(t2, "Terminal at 1.5x: %d x %d" % g2[:2], ("app", "scale"), "18 px, cells %d x %d px: no 80 columns (a 'small' size brings %d back)" % (g2[2], g2[3], g2x1[0]))], 2,
	       notes = ("Today's Terminal: 79 x 25 (8 x 16 bitmap). Direction 1: %d x %d (15 px). Direction 2: %d x %d at 1.5x, %d x %d at 1x." % (g[0], g[1], g2[0], g2[1], g2x1[0], g2x1[1]),))
	# ---- the rest
	hero = Image.new ("RGB", (10, 10))
	a, _ = device (h1); b, _ = device (h2)
	hero = Image.new ("RGB", (a.width + b.width + 180, a.height + 150), (244, 245, 247))
	text (hero, 60, 26, "The two directions on the device", cfont (30, "bold"), INK); text (hero, 60, 68, "At the screen's own scale (not resampled).", cfont (16), INK2)
	hero.paste (a, (50, 110)); hero.paste (b, (a.width + 130, 110))
	text (hero, 70, a.height + 112, "Direction 1 -- Onyx Noir (1x)", cfont (18, "bold"), INK); text (hero, a.width + 150, a.height + 112, "Direction 2 -- Onyx Deck (1.5x)", cfont (18, "bold"), INK)
	hero.save (os.path.join (OUT, "directions-on-device.png"))
	true_size ([(Image.open (os.path.join (REAL, "pocketshell-home-640.png")).convert ("RGB"), "Today"), (h1, "Direction 1 -- Onyx Noir"), (h2, "Direction 2 -- Onyx Deck")])
	button_map ()
	audit ()
	print ("wrote", OUT)

def deck_lock ():
	c = Ctx (1.5); img = c.img; u = c.u
	gl (img, u (20), u (14), mc.g_gem, u (16), T["accent"]); text_l (img, u (42), u (12), u (20), "Onyx", c.f (13, "bold"), T["text1"])
	mc.battery (img, W - u (20) - u (22), u (17), 78, T["text2"], k = 1.5)
	text (img, u (20), u (72), "12:34", c.f (88, "light"), T["text1"])
	text (img, u (24), u (186), "Saturday 10 October", c.f (17), T["text2"])
	text (img, u (24), u (214), "Team call at 14:00", c.f (13), T["accent_hi"])
	hints (c, [(["A"], "unlock")], H - u (31), align = "left", x = u (22), size = 12)
	return img

if __name__ == "__main__": main ()
