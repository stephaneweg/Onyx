#!/usr/bin/env python3
# Copyright (c) 2026 the Onyx authors -- MIT licence (see docs/LICENSING.md).
"""mockup_pocketterm_apps.py -- the apps of Onyx in pocket mode on the PocketTerm35 (640 x 480, a D-pad, no
pointer): the designer's answers to the nine questions of docs/compact-shell/real/pocketterm/AUDIT-640.md,
as mock-ups in the flat dark "Onyx Noir" style. Mock-ups only: nothing here is built.

    python tools/screenshot/mockup_pocketterm_apps.py   -> docs/compact-shell/mockups/pocketterm/apps/*.png

Each picture is the whole screen, 640 x 480: the shell's band (30 px) and the app's window (640 x 450). The
tokens, the fonts and the helpers are mockup_pocketterm.py's. The words beside the pictures: apps/ANSWERS.md.
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
import mockup_compact as mc
import mockup_pocketterm as pt
from mockup_compact import rrect, ring, box, hline, vline, glyph, text, text_l, text_c, text_r, tw, font, cfont, mask
from mockup_pocketterm import T, Ctx, W, H, gl, hints, draw_glyph, glyph_w, b_pill

OUT = os.path.join (pt.OUT, "apps")
REAL = os.path.join (mc.ROOT, "docs", "compact-shell", "real", "pocketterm")
BAND, TOP = 30, 70			# the band's height; where the app's content starts under its own bar
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)

# ---- the shared pieces ------------------------------------------------------------------------------------
def band (c, app, left = None, right = None, lit = None, clock = "12:34"):
	"""The shell's band as built, plus the answer to Q3: a closed drawer has no tab on the content -- the band
	names it with its button (L at the left, R at the right); the chip is lit while the drawer is open."""
	img = c.img; box (img, 0, 0, W, BAND, T["bg0"])
	gl (img, 10, 7, mc.g_gem, 15, T["text1"]); fb = c.f (14, "bold"); f = c.f (14)
	text_l (img, 32, 4, 22, app, fb, T["text1"]); x = 32 + tw (app, fb) + 14
	gl (img, x, 10, mc.g_menu, 12, T["text2"]); text_l (img, x + 19, 4, 22, "Menu", f, T["text2"]); x += 19 + tw ("Menu", f) + 18
	def chip (x, key, name, on, right_side):
		f2 = c.f (13, "bold" if on else "regular"); kw = glyph_w (key, 18); w = kw + 6 + tw (name, f2)
		if right_side: x -= w
		kx = x + w - kw if right_side else x; tx = x if right_side else x + kw + 6
		rrect (img, kx, 6, kw, 18, 5, T["accent"] if on else T["bg3"]); text_c (img, kx, 5, kw, 18, key, cfont (11, "bold"), WHITE if on else T["text1"])
		text_l (img, tx, 4, 22, name, f2, T["accent_hi"] if on else T["text2"]); return w
	if left: chip (x, "L", left, lit == "L", False)
	xr = W - 12; text_r (img, xr, 4, 22, clock, fb, T["text1"]); xr -= tw (clock, fb) + 12
	gl (img, xr - 16, 8, mc.g_wifi, 15, T["text2"]); xr -= 16 + 14
	if right: chip (xr, "R", right, lit == "R", True)

def topbar (c, title, back = None, sub = None):
	"""The app's own bar under the band: a "<" back with where it goes, the title, what is after it."""
	img = c.img; box (img, 0, BAND, W, TOP - BAND, T["bg0"]); hline (img, 0, TOP - 1, W, T["line"]); x = 14
	if back:
		gl (img, x, BAND + 14, mc.g_chev, 12, T["accent_hi"], dirn = "left"); text_l (img, x + 16, BAND, 40, back, c.f (14), T["accent_hi"]); x += 16 + tw (back, c.f (14)) + 16
	text_l (img, x, BAND, 40, title, c.f (16, "bold"), T["text1"]); x += tw (title, c.f (16, "bold")) + 10
	if sub: text_l (img, x, BAND + 1, 40, sub, c.f (13), T["text2"]); x += tw (sub, c.f (13)) + 10
	return x

def tbtn (c, xr, label, primary = False, key = None, focus = False, y = None, h = 28):
	"""A button, right-aligned at xr. Returns the x at its left."""
	img = c.img; f = c.f (14, "bold" if primary else "regular"); kf = c.f (12)
	w = tw (label, f) + 24 + ((tw (key, kf) + 8) if key else 0); x = xr - w; y = (BAND + 6) if y is None else y
	rrect (img, x, y, w, h, 8, T["accent"] if primary else T["bg2"])
	text_l (img, x + 12, y, h, label, f, WHITE if primary else T["text1"])
	if key: text_r (img, x + w - 12, y, h, key, kf, (226, 236, 250) if primary else T["text3"])
	if focus: ring (img, x - 3, y - 3, w + 6, h + 6, 10, T["accent"], t = 2)
	return x - 8

def strip (c, items, label = None):
	"""The hint line at the bottom (the Menu's, as built): shown while a view or a mode has keys of its own."""
	img = c.img; box (img, 0, H - 30, W, 30, BLACK); hline (img, 0, H - 30, W, T["line"]); x = 14
	if label:
		f = c.f (12, "bold"); w = tw (label, f) + 16; rrect (img, x, H - 24, w, 18, 5, T["accent"]); text_c (img, x, H - 25, w, 18, label, f, WHITE); x += w + 14
	hints (c, items, H - 24, align = "left", x = x, size = 12)

def dim (c, y0 = BAND, k = 0.55):
	a = c.img.crop ((0, y0, W, H)); c.img.paste (Image.blend (a, Image.new ("RGB", a.size, T["bg0"]), k), (0, y0))

def drawer (c, side, w, title, count = None):
	"""A drawer over the content (L the left one, R the right one), as built. Returns its x."""
	dim (c); img = c.img; x = 0 if side == "L" else W - w
	box (img, x, BAND, w, H - BAND, T["bg1"]); vline (img, x + w - 1 if side == "L" else x, BAND, H - BAND, T["line"])
	text_l (img, x + 16, BAND + 6, 30, title.upper (), c.f (12, "bold"), T["text2"])
	if count: text_r (img, x + w - 16, BAND + 6, 30, count, c.f (12), T["text3"])
	return x

def seg (c, x, y, items, sel, h = 30, w = None, focus = False):
	img = c.img; f = c.f (14); ws = [w or tw (s, f) + 28 for s in items]; tot = sum (ws)
	rrect (img, x, y, tot, h, 8, T["bg2"]); xx = x
	for i, s in enumerate (items):
		if i == sel: rrect (img, xx + 2, y + 2, ws[i] - 4, h - 4, 6, T["accent"])
		text_c (img, xx, y - 1, ws[i], h, s, c.f (14, "bold") if i == sel else f, WHITE if i == sel else T["text2"]); xx += ws[i]
	if focus: ring (img, x - 3, y - 3, tot + 6, h + 6, 10, T["accent"], t = 2)
	return tot

def row (c, x, y, w, h, label, value = None, sel = False, focus = False, chev = False, sub = None, colour = None):
	img = c.img
	if sel: rrect (img, x, y, w, h, 8, T["accent"])
	elif focus: rrect (img, x, y, w, h, 8, T["bg3"])
	tx = x + 14
	if colour: rrect (img, x + 12, y + h // 2 - 5, 10, 10, 5, colour); tx += 18
	if sub:
		text (img, tx, y + 6, label, c.f (15, "bold" if sel or focus else "regular"), WHITE if sel else T["text1"]); text (img, tx, y + 27, sub, c.f (12), (226, 236, 250) if sel else T["text2"])
	else: text_l (img, tx, y, h, label, c.f (15, "bold" if sel else "regular"), WHITE if sel else T["text1"])
	xr = x + w - 14
	if chev: gl (img, xr - 10, y + h // 2 - 5, mc.g_chev, 10, WHITE if sel else T["text3"], dirn = "right"); xr -= 18
	if value: text_r (img, xr, y, h, value, c.f (14), (226, 236, 250) if sel else T["text2"])

def field (c, x, y, w, h, value, focus = False, caret = False, placeholder = None):
	img = c.img; rrect (img, x, y, w, h, 8, T["bg2"])
	if focus: ring (img, x - 2, y - 2, w + 4, h + 4, 10, T["accent"], t = 2)
	f = c.f (15); text_l (img, x + 12, y, h, value or placeholder or "", f, T["text1"] if value else T["text3"])
	if caret: box (img, x + 13 + tw (value, f), y + 7, 2, h - 14, T["accent_hi"])

def notice (c, y, title, lines, buttons, foot = None):
	"""What is not offered in pocket, said calmly: why, what can be done here, where the rest is."""
	img = c.img; x, w = 70, W - 140; hh = 76 + 22 * len (lines) + 58 + (30 if foot else 0)
	rrect (img, x, y, w, hh, 16, T["bg1"]); ring (img, x, y, w, hh, 16, T["line"])
	rrect (img, x + 24, y + 24, 36, 36, 18, T["bg3"]); text_c (img, x + 24, y + 23, 36, 36, "i", c.f (20, "bold"), T["accent_hi"])
	text (img, x + 74, y + 28, title, c.f (18, "bold"), T["text1"])
	for i, s in enumerate (lines): text (img, x + 24, y + 76 + i * 22, s, c.f (14), T["text2"])
	by = y + 76 + 22 * len (lines) + 12; xr = x + w - 24
	for i, (lab, key, prim) in enumerate (buttons): xr = tbtn (c, xr, lab, prim, key, focus = prim, y = by, h = 34)
	if foot: text (img, x + 24, by + 46, foot, c.f (12), T["text3"])

def lsans (size, bold = False):
	p = os.path.join (mc.SD, "res", "fonts", "LiberationSans-Bold.ttf" if bold else "LiberationSans-Regular.ttf")
	return ImageFont.truetype (p, max (6, int (round (size))))

def waves (w, h):
	pic = Image.new ("RGB", (w, h), (232, 236, 242)); d = ImageDraw.Draw (pic)
	for k, (col, amp, base) in enumerate ((((240, 176, 116), 0.20, 0.50), ((74, 150, 172), 0.16, 0.68), ((44, 96, 128), 0.12, 0.86))):
		pts = [(i, int ((base + amp * math.sin (i / w * 6.3 * (1.2 + k * 0.3) + k * 2)) * h)) for i in range (0, w + 1, 4)]
		d.polygon (pts + [(w, h), (0, h)], fill = col)
	return pic

def photo (w, h):
	"""A picture to work on: the card's sample if it is there, else a drawn landscape."""
	for p in (os.path.join (mc.SD, "docs", "pictures", "sunset-sea.jpg"), os.path.join (mc.SD, "docs", "pictures", "mountains.jpg")):
		if os.path.exists (p):
			im = Image.open (p).convert ("RGB"); r = max (w / im.width, h / im.height)
			im = im.resize ((int (im.width * r) + 1, int (im.height * r) + 1), Image.LANCZOS); return im.crop ((0, 0, w, h))
	pic = mc.grad (w, h, (60, 110, 190), (236, 190, 150)); d = ImageDraw.Draw (pic)
	d.ellipse ([w * 0.68, h * 0.18, w * 0.68 + h * 0.2, h * 0.38], fill = (255, 240, 200))
	for k, (col, base) in enumerate ((((96, 110, 160), 0.62), ((60, 84, 120), 0.74), ((34, 60, 70), 0.88))):
		pts = [(i, int ((base + 0.10 * math.sin (i / w * 9 * (1 + k * 0.4) + k * 1.7) * (1 - k * 0.2)) * h)) for i in range (0, w + 1, 4)]
		d.polygon (pts + [(w, h), (0, h)], fill = col)
	return pic

# ============================================== Q1 Slides ===================================================
OBJ = {"title": (64, 56, 560, 70), "sub": (64, 132, 420, 40), "text": (64, 214, 400, 190), "pic": (524, 196, 372, 230)}
def slide (img, x, y, w, n = 3, sel = None, off = (0, 0), ghost = False):
	"""A 16:9 slide (960 x 540 units) at the width w. The slide keeps ITS colours: it is a document."""
	s = w / 960.0; h = int (540 * s)
	box (img, x, y, w, h, (250, 248, 244)); box (img, x, y, int (14 * s), h, (60, 44, 36))
	def R (k):
		ox, oy = off if k == sel else (0, 0); a = OBJ[k]
		return (x + int ((a[0] + ox) * s), y + int ((a[1] + oy) * s), int (a[2] * s), int (a[3] * s))
	if ghost and sel:
		a = OBJ[sel]; ring (img, x + int (a[0] * s), y + int (a[1] * s), int (a[2] * s), int (a[3] * s), 0, (170, 172, 180))
	r = R ("title"); text (img, r[0], r[1] + int (6 * s), "Onyx in your pocket", lsans (50 * s, True), (36, 30, 28))
	r = R ("sub"); text (img, r[0], r[1] + int (4 * s), "PocketTerm35: what changes in the apps", lsans (25 * s), (120, 96, 84))
	r = R ("text")
	for i, t in enumerate (("One screen at a time", "Drawers under L and R", "Every command in the Menu", "No pointer needed")):
		rrect (img, r[0] + 2, r[1] + int ((14 + i * 44) * s), max (4, int (9 * s)), max (4, int (9 * s)), 3, (196, 110, 60))
		text (img, r[0] + int (26 * s), r[1] + int ((2 + i * 44) * s), t, lsans (27 * s), (48, 44, 42))
	r = R ("pic"); img.paste (waves (r[2], r[3]), (r[0], r[1]))
	text_r (img, x + w - int (24 * s), y + h - int (40 * s), int (30 * s), str (n), lsans (18 * s), (150, 140, 134))
	if sel:
		r = R (sel); ring (img, r[0] - 3, r[1] - 3, r[2] + 6, r[3] + 6, 2, T["accent"], t = 2)
		for hx in (r[0] - 3, r[0] + r[2] // 2, r[0] + r[2] + 3):
			for hy in (r[1] - 3, r[1] + r[3] // 2, r[1] + r[3] + 3):
				if (hx, hy) != (r[0] + r[2] // 2, r[1] + r[3] // 2): box (img, hx - 4, hy - 4, 8, 8, WHITE); ring (img, hx - 4, hy - 4, 8, 8, 0, T["accent"])
	return h

def slides_base (sel = "title", strip_items = None, label = None, off = (0, 0), ghost = False, lit = None):
	c = Ctx (); img = c.img
	band (c, "Slides", "Slides", "Inspector", lit)
	topbar (c, "pocketterm.odp", sub = "3 / 12")
	xr = tbtn (c, W - 14, "Present", True, "F5"); xr = tbtn (c, xr, "Insert"); xr = tbtn (c, xr, "»")
	box (img, 0, TOP, W, H - TOP, T["bg0"])
	sy = TOP + (H - 30 - TOP - 338) // 2
	slide (img, 20, sy, 600, sel = sel, off = off, ghost = ghost)
	strip (c, strip_items or [(["dpad"], "next object"), (["A"], "edit the text"), (["M"], "move"), (["S"], "size"), (["F6"], "tools")], label)
	return c, sy

def q1_edit (): return slides_base ()[0].img
def q1_thumbs ():
	c, _ = slides_base (lit = "L"); img = c.img
	x = drawer (c, "L", 214, "Slides", "12")
	for i in range (4):
		y = BAND + 40 + i * 98; n = i + 2
		if n == 3: rrect (img, x + 8, y - 6, 198, 96, 10, T["bg3"])
		text_r (img, x + 34, y, 84, str (n), c.f (13, "bold" if n == 3 else "regular"), T["text1"] if n == 3 else T["text3"])
		slide (img, x + 44, y, 150, n = n)
		if n == 3: ring (img, x + 41, y - 3, 156, 90, 4, T["accent"], t = 2)
	box (img, x, H - 30, 213, 30, T["bg1"])
	strip (c, [(["dpad-v"], "slide"), (["A"], "open"), (["Ctrl", "M"], "new"), (["L"], "close")])
	return img
def q1_inspector ():
	c, _ = slides_base (sel = "text", lit = "R"); img = c.img
	w = 264; x = drawer (c, "R", w, "Inspector")
	seg (c, x + 14, BAND + 40, ["Slide", "Shape", "Text"], 1, w = (w - 28) // 3)
	y = BAND + 84
	text (img, x + 16, y, "Text box", c.f (15, "bold"), T["text1"]); y += 30
	for i, (lab, val) in enumerate ((("X", "64"), ("Y", "214"), ("Width", "400"), ("Height", "190"))):
		text_l (img, x + 16, y, 32, lab, c.f (14), T["text2"]); field (c, x + 110, y, w - 126, 32, val, focus = i == 0, caret = i == 0); y += 38
	hline (img, x + 16, y + 4, w - 32, T["line"]); y += 14
	row (c, x + 8, y, w - 16, 34, "Keep the proportions", "on"); y += 34
	row (c, x + 8, y, w - 16, 34, "Order", "in front", chev = True); y += 34
	row (c, x + 8, y, w - 16, 34, "Align on the slide", chev = True)
	strip (c, [(["dpad-v"], "field"), (["0-9"], "type a value"), (["dpad-h"], "tab"), (["R"], "close")])
	return img
def q1_move ():
	c, sy = slides_base (sel = "pic", off = (-76, -22), ghost = True, label = "MOVE",
			    strip_items = [(["dpad"], "8 px"), (["Shift"], "+ 1 px"), (["A"], "done"), (["B"], "cancel"), (["S"], "size")])
	img = c.img; s = 600 / 960.0; a = OBJ["pic"]
	cx = 20 + int ((a[0] - 76 + a[2] / 2) * s); oy = sy + int ((a[1] - 22) * s)
	for yy in range (sy, sy + 338, 8): box (img, 20 + 300, yy, 1, 4, T["accent"])	# the slide's centre line: a guide
	lab = "X 448   Y 174"; f = c.f (12, "bold"); wl = tw (lab, f) + 16
	rrect (img, cx - wl // 2, oy - 30, wl, 20, 6, T["accent"]); text_c (img, cx - wl // 2, oy - 31, wl, 20, lab, f, WHITE)
	return img
def q1_present ():
	c = Ctx (bg = BLACK); img = c.img
	slide (img, 0, 60, 640)
	f = c.f (12); rrect (img, W - 76, H - 46, 62, 22, 8, (30, 32, 38)); text_c (img, W - 76, H - 47, 62, 22, "3 / 12", f, T["text2"])
	hints (c, [(["dpad-h"], "slide"), (["B"], "end")], H - 44, align = "left", x = 16, size = 12)
	return img

# ============================================== Q2 Paint ====================================================
def paint_base (title = "sunset-sea.jpg", sub = "1280 x 800", back = None):
	c = Ctx (); img = c.img
	band (c, "Paint"); topbar (c, title, back = back, sub = sub)
	box (img, 0, TOP, W, H - TOP, T["bg0"]); return c
def taskrow (c, items, sel, y, focus = True):
	img = c.img; w = (W - 28 - 8 * (len (items) - 1)) // len (items)
	for i, s in enumerate (items):
		x = 14 + i * (w + 8); rrect (img, x, y, w, 40, 10, T["accent"] if i == sel else T["bg2"])
		text_c (img, x, y - 1, w, 40, s, c.f (15, "bold" if i == sel else "regular"), WHITE if i == sel else T["text1"])
	if focus and sel is not None: x = 14 + sel * (w + 8); ring (img, x - 3, y - 3, w + 6, 46, 12, T["accent_hi"], t = 2)
def q2_view ():
	c = paint_base (); img = c.img
	xr = tbtn (c, W - 14, "Save", True, "Ctrl+S"); tbtn (c, xr, "Undo", key = "Ctrl+Z")
	ph = 300; pw = 480; img.paste (photo (pw, ph), ((W - pw) // 2, TOP + 12))
	taskrow (c, ["Crop", "Resize", "Rotate", "Adjust", "Text"], 0, TOP + 12 + ph + 12)
	strip (c, [(["dpad-h"], "task"), (["A"], "start"), (["+", "-"], "zoom"), (["Alt", "dpad"], "scroll")])
	return img
def q2_crop ():
	c = paint_base (title = "Crop", sub = "sunset-sea.jpg", back = "Paint"); img = c.img
	tbtn (c, W - 14, "Apply", True, "Enter")
	ph = 300; pw = 480; px, py = (W - pw) // 2, TOP + 12
	pic = photo (pw, ph); img.paste (Image.blend (pic, Image.new ("RGB", pic.size, T["bg0"]), 0.62), (px, py))
	fx, fy, fw, fh = 70, 36, 360, 225; img.paste (pic.crop ((fx, fy, fx + fw, fy + fh)), (px + fx, py + fy))
	ring (img, px + fx - 2, py + fy - 2, fw + 4, fh + 4, 0, T["accent"], t = 2)
	for i in (1, 2): box (img, px + fx + fw * i // 3, py + fy, 1, fh, WHITE, 90); box (img, px + fx, py + fy + fh * i // 3, fw, 1, WHITE, 90)
	y = TOP + 12 + ph + 12; x = 14
	x += b_pill (img, x, y + 9, 22, "L") + 8
	x += seg (c, x, y + 5, ["Free", "1 : 1", "4 : 3", "16 : 10", "16 : 9"], 3) + 8
	b_pill (img, x, y + 9, 22, "R")
	text_r (img, W - 14, y, 40, "960 x 600 px", c.f (14), T["text2"])
	strip (c, [(["dpad"], "move the frame"), (["Shift", "dpad"], "its size"), (["A"], "apply"), (["B"], "cancel")], "CROP")
	return img
def q2_brush ():
	"""The alternative, not recommended: the ribbon as groups behind tabs, a pointer moved by the D-pad."""
	c = paint_base (); img = c.img
	y = TOP + 6; x = 14
	x += b_pill (img, x, y + 5, 22, "L") + 8
	x += seg (c, x, y, ["Image", "Tools", "Brushes", "Shapes", "Colours", "Text"], 2) + 8
	b_pill (img, x, y + 5, 22, "R")
	y += 38
	for i, col in enumerate (((236, 96, 60), WHITE)):		# the two colours: always the row's first thing
		rrect (img, 14 + i * 14, y + 2 + i * 8, 26, 26, 6, col); ring (img, 14 + i * 14, y + 2 + i * 8, 26, 26, 6, T["bg0"], t = 2)
	x = 66
	for i, s in enumerate (("Pencil", "Brush", "Marker", "Spray", "Eraser")):
		f = c.f (14); w = tw (s, f) + 22; rrect (img, x, y + 4, w, 30, 8, T["bg3"] if i == 1 else T["bg2"]); text_c (img, x, y + 3, w, 30, s, f, T["text1"]); x += w + 6
	text_r (img, W - 14, y + 4, 30, "12 px", c.f (14), T["text2"])
	ph = 268; pw = 430; px, py = (W - pw) // 2, y + 44
	pic = photo (pw, ph); d = ImageDraw.Draw (pic)
	pts = [(120 + i * 4, 150 + int (18 * math.sin (i / 5.0)) - i // 3) for i in range (40)]
	d.line (pts, fill = (236, 96, 60), width = 7, joint = "curve"); img.paste (pic, (px, py))
	cx, cy = px + pts[-1][0], py + pts[-1][1]
	for dx, dy, ww, hh in ((-11, -1, 8, 3), (4, -1, 8, 3), (-1, -11, 3, 8), (-1, 4, 3, 8)): box (img, cx + dx - 1, cy + dy - 1, ww + 2, hh + 2, BLACK); box (img, cx + dx, cy + dy, ww, hh, WHITE)
	strip (c, [(["dpad"], "the pointer"), (["A"], "held: paint"), (["L", "R"], "group"), (["F6"], "the row")], "POINTER")
	return img

# ============================================== Q3 the drawer's tab ==========================================
def week (c, y0):
	img = c.img; days = ["MON 28", "TUE 29", "WED 30", "THU 1", "FRI 2", "SAT 3", "SUN 4"]; hx = 52; dw = (W - hx - 6) // 7
	for i, d in enumerate (days):
		x = hx + i * dw; text_c (img, x, y0, dw, 28, d, c.f (12, "bold"), T["accent_hi"] if i == 0 else T["text2"]); vline (img, x, y0 + 28, H - y0 - 28, T["line"])
	for r in range (8):
		y = y0 + 34 + r * 46; hline (img, hx, y, W - hx, T["line"]); text_r (img, hx - 8, y - 9, 18, "%d:00" % (8 + r), c.f (12), T["text2"])
	for col, r0, r1, name, colour in ((0, 1.0, 2.0, "Team stand-up", (61, 134, 218)), (1, 2.5, 4.0, "Dentist", (232, 86, 78)), (3, 0.5, 1.5, "Pay the rent", (70, 167, 88)),
					   (5, 2.0, 4.5, "Farmers market", (240, 160, 60)), (6, 1.5, 3.5, "Long run", (140, 110, 220)), (2, 4.5, 5.5, "Call Jonas", (61, 134, 218))):
		x = hx + col * dw + 3; y = y0 + 34 + int (r0 * 46) + 2; hh = int ((r1 - r0) * 46) - 4
		rrect (img, x, y, dw - 6, hh, 6, tuple (int (v * 0.38 + T["bg1"][j] * 0.62) for j, v in enumerate (colour))); box (img, x, y + 4, 3, hh - 8, colour)
		text (img, x + 9, y + 5, mc.ellipsize (name, c.f (12, "bold"), dw - 18), c.f (12, "bold"), T["text1"])
def q3_closed (lit = None):
	c = Ctx (); img = c.img
	band (c, "Calendar", "Calendars", None, lit); x = topbar (c, "September 2026", sub = "week 40")
	seg (c, W - 14 - 204, BAND + 5, ["Day", "Week", "Month"], 1, w = 68)
	box (img, 0, TOP, W, H - TOP, T["bg0"]); week (c, TOP + 2)
	return c
def q3_open ():
	c = q3_closed ("L"); img = c.img; w = 250; x = drawer (c, "L", w, "Calendars")
	rrect (img, 12, BAND + 40, w - 24, 150, 10, T["bg2"]); text_c (img, 12, BAND + 46, w - 24, 24, "September 2026", c.f (13, "bold"), T["text1"])
	for r in range (5):
		for d in range (7):
			n = r * 7 + d;
			if 1 <= n <= 30:
				xx = 20 + d * 31; yy = BAND + 74 + r * 22
				if n == 28: rrect (img, xx + 3, yy, 24, 20, 6, T["accent"])
				text_c (img, xx, yy - 1, 30, 20, str (n), c.f (12, "bold" if n == 28 else "regular"), WHITE if n == 28 else (T["text2"] if d < 5 else T["text3"]))
	y = BAND + 204
	for i, (name, col) in enumerate ((("Work", (61, 134, 218)), ("Personal", (70, 167, 88)), ("Family", (240, 160, 60)), ("Sport", (140, 110, 220)), ("Birthdays", (232, 86, 130)))):
		row (c, 8, y, w - 16, 34, name, "shown" if i != 3 else "hidden", focus = i == 0, colour = col); y += 36
	strip (c, [(["dpad-v"], "calendar"), (["A"], "show / hide"), (["L"], "close")])
	return img

# ============================================== Q4 Koton ====================================================
TRACKS = [("Drums", (232, 86, 78)), ("Bass", (240, 160, 60)), ("Chords", (61, 134, 218)), ("Melody", (70, 167, 88)), ("Pad", (140, 110, 220)), ("FX", (40, 170, 170))]
def koton_base (tab):
	c = Ctx (); img = c.img
	band (c, "Koton"); topbar (c, "Night drive", sub = "120 bpm   4 / 4")
	xr = W - 14; text_r (img, xr, BAND, 40, "5 . 1 . 00", font (15, True, True), T["text1"]); xr -= 110
	for k, on in (("stop", False), ("play", True), ("start", False)):
		rrect (img, xr - 34, BAND + 5, 34, 30, 8, T["accent"] if on else T["bg2"]); cx, cy = xr - 17, BAND + 20
		d = ImageDraw.Draw (img)
		if k == "play": d.polygon ([(cx - 5, cy - 7), (cx + 7, cy), (cx - 5, cy + 7)], fill = WHITE)
		if k == "stop": d.rectangle ([cx - 6, cy - 6, cx + 6, cy + 6], fill = T["text1"])
		if k == "start": d.polygon ([(cx + 6, cy - 7), (cx - 4, cy), (cx + 6, cy + 7)], fill = T["text1"]); d.rectangle ([cx - 7, cy - 7, cx - 5, cy + 7], fill = T["text1"])
		xr -= 40
	box (img, 0, TOP, W, H - TOP, T["bg0"])
	x = 14; x += b_pill (img, x, TOP + 13, 22, "L") + 8; x += seg (c, x, TOP + 8, ["Song", "Mixer"], tab, w = 96) + 8; b_pill (img, x, TOP + 13, 22, "R")
	text_r (img, W - 14, TOP + 8, 30, "6 tracks   2 : 08", c.f (13), T["text2"])
	return c
def q4_song ():
	c = koton_base (0); img = c.img; y0 = TOP + 50; lx = 150
	for b in range (9):
		x = lx + b * 53; text (img, x + 3, y0, str (b * 4 + 1), c.f (11), T["text3"]); vline (img, x, y0 + 16, 6 * 48, T["line"])
	pat = ["11111111", "01111111", "00111111", "00011011", "00110011", "00000110"]
	for i, (name, col) in enumerate (TRACKS):
		y = y0 + 18 + i * 48; foc = i == 3
		if foc: rrect (img, 8, y, W - 16, 44, 8, T["bg2"]); ring (img, 8, y, W - 16, 44, 8, T["accent"], t = 2)
		box (img, 16, y + 8, 4, 28, col); text_l (img, 28, y, 44, name, c.f (15, "bold" if foc else "regular"), T["text1"])
		for j, (lab, on) in enumerate ((("M", i == 4), ("S", False))):
			rrect (img, 96 + j * 24, y + 11, 20, 22, 5, T["warning"] if on else T["bg3"]); text_c (img, 96 + j * 24, y + 10, 20, 22, lab, c.f (11, "bold"), (20, 20, 22) if on else T["text2"])
		for b, ch in enumerate (pat[i]):
			if ch == "1": rrect (img, lx + b * 53 + 2, y + 8, 49, 28, 5, tuple (int (v * (0.30 if i == 4 else 0.62)) for v in col))
	px = lx + 53 + 26; box (img, px, y0 + 12, 2, 6 * 48 + 8, T["text1"])
	strip (c, [(["dpad-v"], "track"), (["dpad-h"], "the bar"), (["Space"], "play"), (["A"], "mute"), (["S"], "solo")])
	return img
def q4_mixer ():
	c = koton_base (1); img = c.img; y0 = TOP + 52; sw = 80
	lev = [0.78, 0.70, 0.55, 0.66, 0.40, 0.30, 0.74]
	for i, (name, col) in enumerate (TRACKS + [("Master", T["text1"])]):
		x = 14 + i * (sw + 8); foc = i == 3
		rrect (img, x, y0, sw, 318, 10, T["bg2"] if foc else T["bg1"])
		if foc: ring (img, x, y0, sw, 318, 10, T["accent"], t = 2)
		box (img, x + 10, y0 + 10, sw - 20, 3, col); text_c (img, x, y0 + 18, sw, 22, name, c.f (13, "bold" if foc else "regular"), T["text1"])
		fx = x + 24; fy = y0 + 52; fh = 196
		rrect (img, fx, fy, 8, fh, 4, T["bg3"]); lv = int (fh * lev[i]); rrect (img, fx, fy + fh - lv, 8, lv, 4, T["accent"] if foc else T["text3"])
		rrect (img, fx - 8, fy + fh - lv - 6, 24, 12, 4, T["text1"])
		for m in range (2):
			mh = int (fh * lev[i] * (0.9 - m * 0.12)); box (img, x + 50 + m * 7, fy, 5, fh, T["bg3"]); box (img, x + 50 + m * 7, fy + fh - mh, 5, mh, T["success"])
		text_c (img, x, fy + fh + 6, sw, 18, "%+.1f dB" % (lev[i] * 24 - 18), c.f (11), T["text2"])
		if i < 6:
			for j, (lab, on) in enumerate ((("M", i == 4), ("S", False))):
				rrect (img, x + 16 + j * 26, y0 + 284, 22, 22, 5, T["warning"] if on else T["bg3"]); text_c (img, x + 16 + j * 26, y0 + 283, 22, 22, lab, c.f (11, "bold"), (20, 20, 22) if on else T["text2"])
	strip (c, [(["dpad-h"], "track"), (["dpad-v"], "level"), (["A"], "mute"), (["S"], "solo"), (["Space"], "play")])
	return img
def q4_notice ():
	c = koton_base (0); img = c.img; dim (c, TOP, 0.35)
	notice (c, TOP + 66, "Writing notes needs a pointer",
		["In pocket, Koton plays a project and mixes it: levels, mute, solo,", "the tempo, the export to WAV. The piano roll, the sound chains and",
		 "the generators are drawn and dragged with a mouse."],
		[("Back to the song", "B", True), ("Open FM Tracker", None, False)],
		"All of Koton: desktop mode, with a mouse (Control Panel > Mode).")
	return img

# ============================================== Q5 Setup ====================================================
def setup_base (n, title):
	c = Ctx (); img = c.img
	band (c, "Onyx Setup"); box (img, 0, BAND, W, 40, T["bg0"])
	text_l (img, 16, BAND, 40, title, c.f (16, "bold"), T["text1"])
	text_r (img, W - 16, BAND, 40, "%d of 7" % n, c.f (13), T["text2"])
	sw = 30; x0 = W - 16 - tw ("%d of 7" % n, c.f (13)) - 14 - 7 * (sw + 4)
	for i in range (7): rrect (img, x0 + i * (sw + 4), BAND + 18, sw, 4, 2, T["accent"] if i < n else T["bg3"])
	hline (img, 0, TOP - 1, W, T["line"]); box (img, 0, TOP, W, H - TOP, T["bg0"]); return c
def setup_foot (c, back = True, nxt = "Next", extra = None):
	img = c.img; y = H - 48; hline (img, 0, y - 8, W, T["line"])
	if back: pt.dialog_btn (c, 16, y, 130, 36, "B", "Back")
	pt.dialog_btn (c, W - 16 - 170, y, 170, 36, "A", nxt, True)
	if extra: text_c (img, 150, y, W - 16 - 170 - 150, 36, extra, c.f (13), T["text3"])
def q5_welcome ():
	c = setup_base (1, "Welcome"); img = c.img
	gl (img, 26, TOP + 22, mc.g_gem, 44, T["accent"]); text (img, 86, TOP + 18, "Welcome to Onyx", c.f (28, "bold"), T["text1"])
	text (img, 86, TOP + 56, "Seven short steps, about two minutes. Everything can be", c.f (15), T["text2"])
	text (img, 86, TOP + 78, "changed later in the Control Panel.", c.f (15), T["text2"])
	text_l (img, 28, TOP + 122, 40, "Language", c.f (15), T["text2"])
	rrect (img, 130, TOP + 122, 300, 40, 10, T["bg2"]); ring (img, 127, TOP + 119, 306, 46, 12, T["accent"], t = 2)
	gl (img, 142, TOP + 137, mc.g_chev, 11, T["text2"], dirn = "left"); text_c (img, 130, TOP + 121, 300, 40, "English", c.f (16, "bold"), T["text1"]); gl (img, 408, TOP + 137, mc.g_chev, 11, T["text2"], dirn = "right")
	rrect (img, 16, TOP + 184, W - 32, 124, 14, T["bg1"])
	text (img, 32, TOP + 196, "This device has no mouse: its buttons do everything", c.f (14, "bold"), T["text1"])
	items = [("dpad", "move from one choice to the next"), ("A", "choose, go to the next step"), ("B", "go back one step"), ("CMD", "the Menu, in every app")]
	for i, (g, s) in enumerate (items):
		x = 32 + (i % 2) * 296; y = TOP + 228 + (i // 2) * 38
		draw_glyph (img, x, y, 26, g); text_l (img, x + 44 if g != "CMD" else x + 56, y, 26, s, c.f (14), T["text2"])
	setup_foot (c, back = False, extra = "Keyboard: Enter and Esc do the same")
	return img
NETS = [("Maison", 4, True, "saved"), ("Voisin-5G", 3, True, ""), ("Livebox-1280", 3, True, ""), ("FreeWifi", 2, False, "open"), ("Atelier Lumen", 1, True, "")]
def bars (img, x, y, n):
	for i in range (4): box (img, x + i * 6, y + 12 - i * 4, 4, 4 + i * 4, T["text1"] if i < n else T["bg3"])
def q5_wifi ():
	c = setup_base (3, "Wi-Fi"); img = c.img
	text (img, 16, TOP + 10, "Choose your network. The country is Belgium (step 2).", c.f (14), T["text2"])
	y = TOP + 40
	for i, (name, n, lockd, note) in enumerate (NETS):
		foc = i == 1
		rrect (img, 16, y, W - 32, 46, 10, T["bg2"] if foc else T["bg1"])
		if foc: ring (img, 14, y - 2, W - 28, 50, 12, T["accent"], t = 2)
		bars (img, 32, y + 15, n); text_l (img, 70, y, 46, name, c.f (16, "bold" if foc else "regular"), T["text1"])
		if lockd: gl (img, W - 56, y + 15, mc.g_lock, 14, T["text2"])
		if note: text_r (img, W - 70, y, 46, note, c.f (13), T["success"] if note == "saved" else T["text2"])
		y += 52
	text_l (img, 30, y + 2, 30, "Not now: skip this step", c.f (14), T["accent_hi"])
	setup_foot (c, nxt = "Connect")
	return img
def q5_password ():
	c = setup_base (3, "Wi-Fi"); img = c.img
	gl (img, 16, TOP + 20, mc.g_chev, 12, T["accent_hi"], dirn = "left"); text_l (img, 32, TOP + 8, 36, "Networks", c.f (14), T["accent_hi"])
	bars (img, 30, TOP + 66, 3); text (img, 66, TOP + 56, "Voisin-5G", c.f (24, "bold"), T["text1"])
	text (img, 30, TOP + 104, "Password", c.f (14), T["text2"])
	field (c, 30, TOP + 128, W - 60, 46, "tarte-tatin-2026", focus = True, caret = True)
	rrect (img, 30, TOP + 190, 42, 24, 12, T["accent"]); rrect (img, 50, TOP + 192, 20, 20, 10, WHITE); text_l (img, 84, TOP + 188, 28, "Show the password", c.f (15), T["text1"])
	text_r (img, W - 30, TOP + 188, 28, "16 characters", c.f (13), T["text3"])
	rrect (img, 30, TOP + 236, W - 60, 56, 12, T["bg1"])
	text (img, 46, TOP + 246, "Type it with the keyboard. Digits: the top row; capitals: Shift;", c.f (13), T["text2"])
	text (img, 46, TOP + 266, "symbols are on the Fn layer of this keyboard.", c.f (13), T["text2"])
	setup_foot (c, nxt = "Connect", extra = "Enter connects too")
	return img

# ============================================== Q6 QBStudio =================================================
CODE = ["' Main.form -- the window's events", "SUB Go_Click ()", "  IF Name.Text = \"\" THEN", "    Status.Text = \"Type a name first\"", "    EXIT SUB", "  END IF",
	"  Page.Show \"Home\", Name.Text", "  Status.Text = \"Hello, \" + Name.Text", "END SUB", "", "SUB Main_Load ()", "  Name.Focus", "END SUB"]
KW = ("SUB", "IF", "THEN", "EXIT", "END", "ELSE")
def qb_base (view, lit = None):
	c = Ctx (); img = c.img
	band (c, "QBStudio", "Project", "Properties", lit); topbar (c, "Main.form", sub = "Pages")
	xr = tbtn (c, W - 14, "Run", True, "F5"); xr = tbtn (c, xr, "Check")
	seg (c, xr - 150, BAND + 5, ["Form", "Code"], view, w = 72)
	box (img, 0, TOP, W, H - TOP, T["bg0"]); return c
def q6_code ():
	c = qb_base (1); img = c.img; f = font (15, False, True); fb = font (15, True, True); lh = 22; cw = int (f.getlength ("M"))
	box (img, 0, TOP, 44, H - TOP - 30, T["bg1"])
	for i, s in enumerate (CODE):
		y = TOP + 8 + i * lh; text_r (img, 36, y - 2, lh, str (i + 1), font (13, False, True), T["text3"]); x = 54
		if i == 7: box (img, 44, y - 2, W - 44, lh, T["bg1"])
		if s.startswith ("'"): text (img, x, y, s, f, T["text3"]); continue
		for wd in s.split (" "):
			if wd in KW: text (img, x, y, wd, fb, T["accent_hi"])
			elif wd.startswith ("\"") or wd.endswith ("\""): text (img, x, y, wd, f, T["success"])
			else: text (img, x, y, wd, f, T["text1"])
			x += cw * (len (wd) + 1)
	box (img, 54 + cw * 38, TOP + 8 + 7 * lh, 2, 18, T["accent_hi"])
	strip (c, [(["F7"], "form / code"), (["F5"], "run"), (["L"], "project"), (["R"], "properties"), (["CMD"], "menu")])
	return img
def form (img, x, y, sel = "go", off = (0, 0)):
	"""The form being designed, at its real size (540 x 300)."""
	c = Ctx (); c.img = img; w, h = 540, 300
	rrect (img, x, y, w, h, 8, T["bg1"]); ring (img, x, y, w, h, 8, T["line"]); rrect (img, x, y, w, 26, 8, T["bg2"], corners = (True, True, False, False)); text_l (img, x + 12, y, 26, "Pages", c.f (13, "bold"), T["text1"])
	box (img, x + 1, y + 26, 130, h - 27, T["bg2"])
	for i, s in enumerate (("Home", "Sidebar", "Settings")): text_l (img, x + 14, y + 38 + i * 28, 28, s, c.f (14, "bold" if i == 0 else "regular"), T["text1"] if i == 0 else T["text2"])
	text (img, x + 150, y + 46, "Your name", c.f (14), T["text2"]); field (c, x + 150, y + 70, 240, 32, "Stéphane")
	ox, oy = off; gx, gy = x + 402 + ox, y + 70 + oy
	rrect (img, gx, gy, 110, 32, 8, T["accent"]); text_c (img, gx, gy - 1, 110, 32, "Go", c.f (14, "bold"), WHITE)
	rrect (img, x + 150, y + 122, 362, 130, 8, T["bg0"]); text (img, x + 162, y + 132, "Page: Host", c.f (12), T["text3"])
	box (img, x + 131, y + h - 26, w - 132, 1, T["line"]); text_l (img, x + 150, y + h - 26, 26, "Status", c.f (12), T["text3"])
	if sel == "go":
		ring (img, gx - 3, gy - 3, 116, 38, 2, T["accent_hi"], t = 2)
		for hx in (gx - 3, gx + 113):
			for hy in (gy - 3, gy + 35): box (img, hx - 4, hy - 4, 8, 8, WHITE)
	return gx, gy
def q6_form ():
	c = qb_base (0); img = c.img; fx, fy = 50, TOP + 32
	for gx in range (fx - 40, fx + 590, 20):
		for gy in range (TOP + 12, H - 40, 20): box (img, gx, gy, 1, 1, T["line"])
	gx, gy = form (img, fx, fy)
	lab = "Go : Button   402, 70   110 x 32"; f = c.f (12, "bold"); wl = tw (lab, f) + 16
	rrect (img, gx + 116 - wl, gy - 30, wl, 20, 6, T["accent"]); text_c (img, gx + 116 - wl, gy - 31, wl, 20, lab, f, WHITE)
	strip (c, [(["dpad"], "next control"), (["A"], "its code"), (["M"], "move"), (["S"], "size"), (["L"], "add"), (["R"], "properties")])
	return img
def q6_toolbox ():
	c = qb_base (0, "L"); img = c.img; form (img, 50, TOP + 32, sel = None)
	w = 236; x = drawer (c, "L", w, "Project")
	y = BAND + 38
	for lab, ind, sel in (("Main.form", 0, True), ("Sidebar.form", 0, False), ("Home.form", 0, False), ("Pages.bas", 0, False)):
		row (c, x + 8, y, w - 16, 30, lab, focus = sel); y += 30
	hline (img, x + 16, y + 8, w - 32, T["line"]); y += 16
	text_l (img, x + 16, y, 26, "ADD TO THE FORM", c.f (12, "bold"), T["text2"]); y += 28
	for i, lab in enumerate (("Label", "Button", "Textbox", "Checkbox", "List", "Picture", "Row")):
		row (c, x + 8, y, w - 16, 30, lab, "after Go" if i == 1 else None, sel = i == 1); y += 30
	strip (c, [(["dpad-v"], "choose"), (["A"], "add it, then move it"), (["L"], "close")])
	return img

# ============================================== Q7 3DForge ==================================================
def iso (x, y, z, ox, oy, s):
	return (ox + (x - y) * 0.866 * s, oy + (x + y) * 0.5 * s - z * s)
def part (img, ox, oy, s, height = 12):
	d = ImageDraw.Draw (img); P = lambda x, y, z: iso (x, y, z, ox, oy, s)
	top, left, right = (118, 150, 190), (72, 96, 130), (52, 72, 100)
	def boxp (x0, y0, z0, x1, y1, z1):
		d.polygon ([P (x0, y1, z0), P (x1, y1, z0), P (x1, y1, z1), P (x0, y1, z1)], fill = left)
		d.polygon ([P (x1, y0, z0), P (x1, y1, z0), P (x1, y1, z1), P (x1, y0, z1)], fill = right)
		d.polygon ([P (x0, y0, z1), P (x1, y0, z1), P (x1, y1, z1), P (x0, y1, z1)], fill = top)
	boxp (0, 0, 0, 90, 60, height)
	boxp (0, 0, height, 14, 60, 62)
	for cx, cy in ((40, 30), (68, 30)):
		c0 = P (cx, cy, height); rx, ry = 8 * 0.866 * s, 8 * 0.5 * s
		d.ellipse ([c0[0] - rx, c0[1] - ry, c0[0] + rx, c0[1] + ry], fill = (30, 40, 56))
	c0 = P (14, 30, 40); d.ellipse ([c0[0] - 2, c0[1] - 9 * s * 0.6, c0[0] + 7 * s * 0.5, c0[1] + 9 * s * 0.6], fill = (30, 40, 56))
def forge_base (lit = None, height = 12):
	c = Ctx (); img = c.img
	box (img, 0, TOP, W, H - TOP, T["bg1"])
	d = ImageDraw.Draw (img)
	for i in range (-6, 12):
		d.line ([iso (i * 20, -120, 0, 300, 250, 2.3), iso (i * 20, 220, 0, 300, 250, 2.3)], fill = T["bg2"]); d.line ([iso (-120, i * 20, 0, 300, 250, 2.3), iso (220, i * 20, 0, 300, 250, 2.3)], fill = T["bg2"])
	part (img, 300, 230, 2.3, height)
	band (c, "3DForge", "Steps", "Display", lit); topbar (c, "bracket.forge", sub = "90 x 60 x 62 mm")
	xr = tbtn (c, W - 14, "Export STL", True); f = c.f (12, "bold"); wl = tw ("VIEW AND VALUES", f) + 16
	rrect (img, xr - wl, BAND + 10, wl, 20, 6, T["bg3"]); text_c (img, xr - wl, BAND + 9, wl, 20, "VIEW AND VALUES", f, T["text2"])
	return c
def q7_view ():
	c = forge_base (); img = c.img
	for i, (k, s) in enumerate ((("1", "Front"), ("2", "Top"), ("3", "Right"), ("0", "Home"))):
		x = W - 86; y = TOP + 12 + i * 30; rrect (img, x, y, 72, 24, 7, T["bg2"]); draw_glyph (img, x + 4, y + 3, 18, k); text_l (img, x + 30, y, 24, s, c.f (12), T["text1"] if i != 3 else T["accent_hi"])
	text (img, 16, H - 58, "4 steps   1 body   1 248 triangles", c.f (12), T["text3"])
	strip (c, [(["dpad"], "turn"), (["+", "-"], "zoom"), (["Alt", "dpad"], "slide"), (["L"], "steps"), (["R"], "display")])
	return img
def q7_steps ():
	c = forge_base ("L", height = 15); img = c.img; w = 262; x = drawer (c, "L", w, "Steps", "4")
	y = BAND + 40
	for i, (lab, sub) in enumerate ((("1  Sketch", "on the XY plane: 90 x 60"), ("2  Extrude", ""), ("3  Holes", "2 x Ø 16, through"), ("4  Wall", "sketch on a face, extrude 50"))):
		if i == 1:
			rrect (img, x + 8, y, w - 16, 118, 10, T["bg2"]); text (img, x + 22, y + 8, lab, c.f (15, "bold"), T["text1"])
			text_l (img, x + 22, y + 38, 32, "Height", c.f (14), T["text2"]); field (c, x + 100, y + 38, 96, 32, "15", focus = True, caret = True); text_l (img, x + 204, y + 38, 32, "mm", c.f (14), T["text2"])
			text (img, x + 22, y + 82, "was 12 mm   Enter: rebuild", c.f (12), T["accent_hi"]); y += 124
		else: row (c, x + 8, y, w - 16, 50, lab, sub = sub); y += 54
	hline (img, x + 16, y + 6, w - 32, T["line"]); text (img, x + 16, y + 16, "New steps (a sketch, a fillet) need a", c.f (12), T["text3"]); text (img, x + 16, y + 33, "pointer: they are in desktop mode.", c.f (12), T["text3"])
	strip (c, [(["dpad-v"], "step"), (["0-9"], "a value"), (["A"], "rebuild"), (["B"], "undo"), (["L"], "close")])
	return img

# ============================================== Q8 Turtle Quest's level editor ==============================
def q8_texts ():
	c = Ctx (); img = c.img
	band (c, "Turtle Quest"); topbar (c, "New level", back = "Levels", sub = "1 of 2: the texts")
	tbtn (c, W - 14, "The map", True, "F2")
	box (img, 0, TOP, W, H - TOP, T["bg0"]); y = TOP + 12
	for i, (lab, val, hh) in enumerate ((("Title", "The maze", 34), ("Goal", "Find the way out. Keep your right hand on the wall!", 34), ("Hint", "WALLRIGHT () says whether a wall is at your right.", 34),
					    ("Lesson", "WHILE repeats as long as its test is true...", 58))):
		text_l (img, 16, y, 34, lab, c.f (14), T["text2"]); field (c, 110, y, W - 126, hh, mc.ellipsize (val, c.f (15), W - 160), focus = i == 1, caret = i == 1); y += hh + 8
	hline (img, 16, y + 2, W - 32, T["line"]); y += 12
	for i, (lab, val) in enumerate ((("3 stars up to", "7"), ("2 stars up to", "10"), ("Width", "11"), ("Height", "9"))):
		x = 16 + (i % 2) * 312; yy = y + (i // 2) * 42
		text_l (img, x, yy, 34, lab, c.f (14), T["text2"]); field (c, x + 130, yy, 70, 34, val); text_l (img, x + 210, yy, 34, "instructions" if i < 2 else "cells", c.f (13), T["text3"])
	y += 90; text_l (img, 16, y, 30, "Start program", c.f (14), T["text2"]); text_l (img, 146, y, 30, "empty", c.f (14), T["text1"]); text_r (img, W - 16, y, 30, "Words allowed: all", c.f (14), T["text2"])
	strip (c, [(["dpad-v"], "field"), (["A"], "edit"), (["F2"], "the map"), (["B"], "back to the levels")])
	return img
MAZE = ["###########", "#T..#.....#", "###.#.###.#", "#...#...#.#", "#.#####.#.#", "#.#...#.#.#", "#.#.#.#...#", "#...#...#F#", "###########"]
def q8_map ():
	c = Ctx (); img = c.img
	band (c, "Turtle Quest"); topbar (c, "The maze", back = "The texts", sub = "2 of 2: the map")
	tbtn (c, W - 14, "Try it", True, "F5")
	box (img, 0, TOP, W, H - TOP, T["bg0"]); cs = 34; gx = (W - 11 * cs) // 2; gy = TOP + 10
	rrect (img, gx - 6, gy - 6, 11 * cs + 12, 9 * cs + 12, 10, (96, 150, 88))
	for r, line in enumerate (MAZE):
		for q, ch in enumerate (line):
			x, y = gx + q * cs, gy + r * cs
			rrect (img, x + 1, y + 1, cs - 2, cs - 2, 4, (104, 116, 140) if ch == "#" else (236, 224, 196))
			if ch == "T": rrect (img, x + 8, y + 8, cs - 16, cs - 16, 9, (52, 140, 70))
			if ch == "F": box (img, x + 14, y + 7, 2, 20, (60, 40, 30)); ImageDraw.Draw (img).polygon ([(x + 16, y + 7), (x + 27, y + 12), (x + 16, y + 17)], fill = (226, 60, 50))
	cx, cy = gx + 7 * cs, gy + 5 * cs; ring (img, cx - 2, cy - 2, cs + 4, cs + 4, 6, T["accent"], t = 3); ring (img, cx - 4, cy - 4, cs + 8, cs + 8, 8, WHITE, t = 1)
	y = gy + 9 * cs + 14; x = 14; x += b_pill (img, x, y + 6, 22, "L") + 8
	for i, (lab, col) in enumerate ((("Wall", (104, 116, 140)), ("Floor", (236, 224, 196)), ("Turtle", (52, 140, 70)), ("Flag", (226, 60, 50)), ("Coin", (240, 190, 60)), ("Door", (150, 110, 80)), ("Erase", T["bg3"]))):
		f = c.f (14, "bold" if i == 0 else "regular"); w = tw (lab, f) + 40
		rrect (img, x, y, w, 34, 9, T["bg3"] if i == 0 else T["bg1"]); rrect (img, x + 10, y + 9, 16, 16, 4, col); text_l (img, x + 32, y, 34, lab, f, T["text1"] if i == 0 else T["text2"])
		if i == 0: ring (img, x, y, w, 34, 9, T["accent"], t = 2)
		x += w + 5
	b_pill (img, x + 3, y + 6, 22, "R")
	strip (c, [(["dpad"], "the cell"), (["A"], "put; held: a run"), (["L", "R"], "tool"), (["F5"], "try"), (["B"], "the texts")])
	return img

# ============================================== Q9 the desktop's small windows ==============================
def q9_clipboard ():
	c = Ctx (); img = c.img
	band (c, "Text Editor"); topbar (c, "pocketterm.txt", sub = "SD:/docs")
	pt.editor_body (c, TOP, px = 15)
	dim (c, BAND, 0.6)
	x, y, w, h = 96, 44, W - 192, 388
	rrect (img, x, y, w, h, 16, T["bg1"]); ring (img, x, y, w, h, 16, T["line"])
	text_l (img, x + 20, y + 10, 30, "Clipboard", c.f (17, "bold"), T["text1"]); text_r (img, x + w - 20, y + 10, 30, "the last 10", c.f (13), T["text2"])
	clips = [("- D-pad and A / B do what the arrows, Enter...", "Text Editor", "now"), ("SD:/docs/pictures/sunset-sea.jpg", "File Viewer", "2 min"),
		 ("Amount: 2.528,90 € (VAT included), within 30...", "Mail", "14 min"), ("WHILE NOT ONGOAL ()", "Turtle Quest", "1 h"), ("picture   960 x 600", "Paint", "1 h"), ("192.168.0.10", "Terminal", "3 h")]
	yy = y + 48
	for i, (s, app, when) in enumerate (clips):
		row (c, x + 10, yy, w - 20, 50, mc.ellipsize (s, c.f (15), w - 110), when, sel = i == 1, sub = "from " + app); yy += 54
	box (img, 0, H - 30, W, 30, BLACK)
	strip (c, [(["dpad-v"], "choose"), (["A"], "paste here"), (["Bksp"], "forget it"), (["B"], "close")])
	return img
def q9_stickies ():
	c = Ctx (); img = c.img
	band (c, "Stickies"); topbar (c, "Stickies", sub = "4 notes")
	tbtn (c, W - 14, "New", True, "Ctrl+N")
	box (img, 0, TOP, W, H - TOP, T["bg0"])
	notes = [((246, 214, 110), "Shopping", ["butter, apples (8)", "flour", "vanilla"]), ((158, 214, 150), "PocketTerm", ["charger: USB-C 5 V 3 A", "the Fn layer: Q = Esc"]),
		 ((240, 168, 176), "Call back", ["Jonas, before Friday", "the quote for the labels"]), ((150, 196, 240), "Ideas", ["a level with two flags", "Koton: export to MP3"])]
	cw, chh = (W - 14 * 3) // 2, 160
	for i, (col, title, lines) in enumerate (notes):
		x = 14 + (i % 2) * (cw + 14); y = TOP + 12 + (i // 2) * (chh + 12)
		rrect (img, x, y, cw, chh, 10, col); text (img, x + 16, y + 12, title, c.f (16, "bold"), (30, 30, 32))
		for j, s in enumerate (lines): text (img, x + 16, y + 44 + j * 24, s, c.f (15), (48, 48, 52))
		if i == 1: ring (img, x - 4, y - 4, cw + 8, chh + 8, 13, T["accent"], t = 3)
	strip (c, [(["dpad"], "note"), (["A"], "write in it"), (["Bksp"], "throw away"), (["Ctrl", "N"], "new")])
	return img

# ============================================== the sheets ==================================================
INK, INK2 = (28, 30, 34), (96, 100, 108)
def sheet (name, title, sub, items, cols = 3):
	gap, top, cap = 30, 112, 66; cw, chh = W, H; rows = (len (items) + cols - 1) // cols
	sw = max (60 + cols * cw + (cols - 1) * gap + 60, 1460); sh = top + rows * (chh + cap) + 20
	img = Image.new ("RGB", (sw, sh), (244, 245, 247))
	text (img, 60, 26, title, cfont (28, "bold"), INK)
	for j, s in enumerate (sub.split ("\n")): text (img, 60, 66 + j * 20, s, cfont (15), INK2)
	for i, (im, caption, note) in enumerate (items):
		x = 60 + (i % cols) * (cw + gap); y = top + (i // cols) * (chh + cap)
		img.paste (im.convert ("RGB"), (x, y)); ring (img, x - 1, y - 1, cw + 2, chh + 2, 0, (200, 202, 208))
		text (img, x, y + chh + 8, caption, cfont (16, "bold"), INK); text (img, x, y + chh + 32, note, cfont (13), INK2)
	img.save (os.path.join (OUT, name))

def main ():
	os.makedirs (OUT, exist_ok = True)
	def S (im, n): im.save (os.path.join (OUT, n)); return im
	a = [S (q1_edit (), "slides-edit.png"), S (q1_thumbs (), "slides-thumbnails.png"), S (q1_inspector (), "slides-inspector.png"), S (q1_move (), "slides-move.png"), S (q1_present (), "slides-present.png")]
	sheet ("q1-slides.png", "Q1 Slides: the slide alone, the objects stepped by the D-pad, moved in a mode",
	       "L = the slides, R = the inspector (drawers, named in the band). No pointer: the D-pad goes from object to object; M and S start the move and size modes.",
	       [(a[0], "Editing", "an object is chosen: the hint line says what the keys do"), (a[1], "L: the slides", "a list of thumbnails over the slide"), (a[2], "R: the inspector", "exact values typed; the same numbers the move mode changes"),
		(a[3], "M: the move mode", "8 px steps, Shift 1 px; the centre guide lights up; A keeps, B cancels"), (a[4], "F5: presenting", "the whole 640 x 480, no band; the hint fades after 3 s")])
	b = [S (q2_view (), "paint-view.png"), S (q2_crop (), "paint-crop.png"), S (q2_brush (), "paint-brush-alternative.png")]
	sheet ("q2-paint.png", "Q2 Paint: in pocket it adjusts a picture; it does not paint",
	       "Recommended: the picture and ONE row of tasks (Crop, Resize, Rotate, Adjust, Text). Brushes, shapes and layers stay in desktop mode.\nThe third picture is the alternative that was weighed and is NOT recommended.",
	       [(b[0], "The picture and its tasks (recommended)", "the row is UIKit's toolbar: Left / Right, A starts a task"), (b[1], "Crop", "the frame moved and sized by the D-pad; L / R the ratio"),
		(b[2], "NOT recommended: painting with the D-pad", "the ribbon's groups behind tabs, the two colours first in the row, a key pointer")])
	cal = Image.open (os.path.join (REAL, "audit-calendar-640.png")).convert ("RGB")
	d = [S (q3_closed ().img, "drawer-closed.png"), S (q3_open (), "drawer-open.png")]
	sheet ("q3-drawer-tab.png", "Q3 A closed drawer: no tab, no gutter, no handle -- the band names it",
	       "On a device without a pointer a tab is a target nobody can press: it only hides content. The band shows the drawer's name beside its button (L at the left,\nR at the right, as in Q1 and Q6); the chip is lit while the drawer is open. With a mouse plugged in, the tab comes back.",
	       [(cal, "Today (the capture)", "the tab covers the hours' first digit"), (d[0], "Closed: the content has the whole width", "\"L Calendars\" in the band"), (d[1], "Open", "the chip is lit; L or B closes")])
	e = [S (q4_song (), "koton-song.png"), S (q4_mixer (), "koton-mixer.png"), S (q4_notice (), "koton-not-in-pocket.png")]
	sheet ("q4-koton.png", "Q4 Koton: it plays and mixes a project; composing is not in pocket",
	       "Two views behind L / R: the Song (the tracks, their blocks, the playhead) and the Mixer. The piano roll, the sound chains and the generators are not offered.",
	       [(e[0], "Song", "rows of 48 px; mute and solo by A and S"), (e[1], "Mixer", "one strip per track; Up / Down the level"), (e[2], "What it says for the rest", "the same card serves Paint's brushes and 3DForge's sketches")])
	g = [S (q5_welcome (), "setup-welcome.png"), S (q5_wifi (), "setup-wifi.png"), S (q5_password (), "setup-wifi-password.png")]
	sheet ("q5-setup.png", "Q5 Setup: one page a step, \"3 of 7\" in the bar, Back and Next on B and A",
	       "The steps' column is gone; the welcome page teaches the four controls, once. Lists take the width; a password has its own page.",
	       [(g[0], "1 Welcome", "the device's buttons drawn: there is no mouse to discover them"), (g[1], "3 Wi-Fi", "rows of 46 px; skipping is a row, not a hidden button"), (g[2], "3 Wi-Fi: the password", "one field, shown by default while typed on a thumb keyboard")])
	k = [S (q6_code (), "qbstudio-code.png"), S (q6_form (), "qbstudio-form.png"), S (q6_toolbox (), "qbstudio-toolbox.png")]
	sheet ("q6-qbstudio.png", "Q6 QBStudio: the form OR the code (F7), the project and toolbox under L, the properties under R",
	       "No split view. On the form the D-pad goes from control to control, as on a slide (Q1): M moves, S sizes, on the form's 8 px grid.",
	       [(k[0], "Code", "the whole width: 15 px monospace, 68 columns"), (k[1], "Form", "a 540 x 300 form fits at its real size"), (k[2], "L: the project and the toolbox", "A adds the control after the chosen one, already in move mode")])
	m = [S (q7_view (), "3dforge-view.png"), S (q7_steps (), "3dforge-steps.png")]
	sheet ("q7-3dforge.png", "Q7 3DForge: look at a part and change its values; no sketching in pocket",
	       "The D-pad always turns the view: there is no second mode to show. The steps' numbers can be retyped (a height, a diameter): the part is rebuilt.",
	       [(m[0], "The view", "D-pad turns, + / - zoom, 1 2 3 the standard views"), (m[1], "L: the steps", "a step's value edited by typing; new steps are not offered")], cols = 2)
	n = [S (q8_texts (), "turtle-editor-texts.png"), S (q8_map (), "turtle-editor-map.png")]
	sheet ("q8-turtle-editor.png", "Q8 Turtle Quest's level editor: yes, in two steps",
	       "A grid is a D-pad's natural ground: a cell cursor, not a pointer. Step 1 the texts (a form), step 2 the map with one row of tools under L / R.",
	       [(n[0], "1 of 2: the texts", "rows of a form; F2 goes to the map"), (n[1], "2 of 2: the map", "A puts the tool's cell; A held and the D-pad paints a run")], cols = 2)
	o = [S (q9_clipboard (), "clipboard-sheet.png"), S (q9_stickies (), "stickies.png")]
	sheet ("q9-small-windows.png", "Q9 The desktop's small windows: a sheet, a full-screen app, and nothing",
	       "The clipboard's history is a sheet over the app in front (Ctrl + Shift + V, or the Menu). Stickies is an ordinary full-screen app. The Agenda has no window: Home's Today line and the Calendar.",
	       [(o[0], "The clipboard's history: a sheet", "A pastes into the app under it"), (o[1], "Stickies: a full-screen app", "the notes keep their colours: they are the content")], cols = 2)
	print ("wrote", OUT)

if __name__ == "__main__": main ()
