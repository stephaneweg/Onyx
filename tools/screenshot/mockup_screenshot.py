#!/usr/bin/env python3
"""mockup_screenshot.py -- the mock-ups of Onyx's screen capture tool, Screenshot, laid out as Windows'
Snipping Tool (the user's choice, 2026-10-01): one toolbar -- New, the mode (a rectangle by default, a
window, the full screen) and the delay as two drop-down buttons, then Copy and Save As once a capture
is made --, the capture shown below. See docs/screenshot/README.md.

    python3 tools/screenshot/mockup_screenshot.py  -> docs/screenshot/mockups/screenshot-*.png

The screen is the Pi's default 1024 x 768: the real desktop (screenshots/desktop.png) is what gets
captured. The drawing helpers and the look are mockup_archiver.py's (the Peach frame, the beige faces).
"""
import os, sys
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, HERE)
import mockup_archiver as M

M.W, M.H = 1024, 768
M.OUT = os.path.join (M.ROOT, "docs", "screenshot", "mockups")
K = M.K
DESK = Image.open (os.path.join (M.ROOT, "screenshots", "desktop.png")).convert ("RGB")
TEXT, DIM, FACE, SEL, MENU = M.TEXT, M.DIM, M.FACE, M.SEL, M.MENU
WHITE = (255, 255, 255)
PEN_COLOURS = [(20, 20, 20), (255, 255, 255), (220, 60, 50), (240, 150, 40), (250, 220, 40), (70, 170, 90), (60, 130, 220), (150, 90, 200)]

def screen ():
	c = M.Canvas ()
	c.img.paste (DESK.resize ((M.W * K, M.H * K), Image.LANCZOS), (0, 0))
	return c

def dim (c, keep = None, alpha = 120):
	"""The screen darkened, but the rectangle `keep` (x, y, w, h)."""
	ov = Image.new ("RGBA", c.img.size, (0, 0, 0, alpha))
	if keep:
		x, y, w, h = keep
		ImageDraw.Draw (ov).rectangle ([x * K, y * K, (x + w) * K - 1, (y + h) * K - 1], fill = (0, 0, 0, 0))
	base = c.img.convert ("RGBA"); base.alpha_composite (ov); c.img = base.convert ("RGB"); c.d = ImageDraw.Draw (c.img, "RGBA")

# ---- the icons ---------------------------------------------------------------------------------------
def ic_region (c, x, y, s, col):
	for i in range (0, s, 4):
		c.rect (x + i, y, 2, 2, col); c.rect (x + i, y + s - 2, 2, 2, col); c.rect (x, y + i, 2, 2, col); c.rect (x + s - 2, y + i, 2, 2, col)
	c.rect (x + s - 7, y + s - 7, 7, 7, col)
def ic_window (c, x, y, s, col):
	c.rect (x, y + 2, s, s - 4, None, r = 2, outline = col, width = 2); c.rect (x, y + 2, s, 5, col, r = 2)
def ic_screen (c, x, y, s, col):
	c.rect (x, y + 1, s, s - 7, None, r = 2, outline = col, width = 2); c.rect (x + s / 2 - 1, y + s - 6, 2, 3, col); c.rect (x + s / 2 - 5, y + s - 3, 10, 2, col)
def ic_clock (c, x, y, s, col, off = False):
	c.ellipse (x + s / 2, y + s / 2, s / 2 - 1, None, col, 2); c.line ([(x + s / 2, y + 4), (x + s / 2, y + s / 2), (x + s / 2 + 4, y + s / 2 + 2)], col, 2)
	if off: c.line ([(x - 1, y + 1), (x + s + 1, y + s - 1)], FACE, 4); c.line ([(x, y + 1), (x + s, y + s - 1)], col, 2)
def ic_pen (c, x, y, s, col, tip = None):
	c.poly ([(x + 3, y + s - 3), (x + s - 7, y + 3), (x + s - 3, y + 7), (x + 7, y + s - 3)], (90, 90, 96))
	c.poly ([(x + 2, y + s - 2), (x + 3, y + s - 7), (x + 7, y + s - 3)], tip or col)
def ic_marker (c, x, y, s, col):
	c.poly ([(x + 5, y + s - 8), (x + s - 9, y + 2), (x + s - 2, y + 9), (x + 12, y + s - 1)], (90, 90, 96))
	c.poly ([(x + 2, y + s - 2), (x + 5, y + s - 9), (x + 11, y + s - 3)], col)
	c.rect (x + 2, y + s - 2, s - 6, 3, col)
def ic_eraser (c, x, y, s, col):
	c.poly ([(x + 2, y + s - 8), (x + s - 10, y + 2), (x + s - 2, y + 10), (x + 10, y + s - 2)], (235, 130, 150))
	c.poly ([(x + 2, y + s - 8), (x + 7, y + s - 13), (x + 15, y + s - 5), (x + 10, y + s - 2)], (250, 250, 250))
	c.rect (x + 8, y + s - 1, s - 8, 2, col)
def ic_crop (c, x, y, s, col):
	c.rect (x + 5, y, 3, s - 5, col); c.rect (x + 5, y + s - 8, s - 5, 3, col)
	c.rect (x, y + 5, s - 5, 3, col); c.rect (x + s - 8, y + 5, 3, s - 5, col)
def ic_undo (c, x, y, s, col, redo = False):
	cx = x + s / 2
	if redo: c.d.arc ([(x + 3) * K, (y + 4) * K, (x + s - 3) * K, (y + s) * K], 200, 360, fill = col, width = 2 * K)
	else: c.d.arc ([(x + 3) * K, (y + 4) * K, (x + s - 3) * K, (y + s) * K], 180, 340, fill = col, width = 2 * K)
	if redo: c.poly ([(x + s - 2, y + s / 2 + 3), (x + s - 9, y + s / 2 + 1), (x + s - 3, y + s / 2 - 5)], col)
	else: c.poly ([(x + 2, y + s / 2 + 3), (x + 9, y + s / 2 + 1), (x + 3, y + s / 2 - 5)], col)
def ic_copy (c, x, y, s, col):
	c.rect (x + 6, y, s - 6, s - 6, None, r = 2, outline = col, width = 2)
	c.rect (x, y + 6, s - 6, s - 6, (250, 248, 246), r = 2, outline = col, width = 2)
def ic_save (c, x, y, s, col):
	c.rect (x, y, s, s, None, r = 3, outline = col, width = 2); c.rect (x + 5, y + 2, s - 10, 6, col); c.rect (x + 5, y + s - 9, s - 10, 7, None, outline = col, width = 2)
def ic_paint (c, x, y, s, col):
	c.ellipse (x + s / 2, y + s / 2, s / 2, (238, 226, 206), (150, 120, 90), 1.5)
	for i, cc in enumerate ([(220, 60, 50), (60, 130, 220), (70, 170, 90), (250, 200, 40)]):
		c.ellipse (x + 5 + (i % 2) * 7 + (i // 2) * 3, y + 5 + (i // 2) * 7, 2.5, cc)
def ic_new (c, x, y, s, col):
	c.rect (x + s / 2 - 1.5, y + 2, 3, s - 4, col); c.rect (x + 2, y + s / 2 - 1.5, s - 4, 3, col)

# ---- the bar on the dimmed screen (a region, a window): the mode, and close ------------------------------------
MODES = [("Rectangle", ic_region), ("Window", ic_window), ("Full screen", ic_screen)]
def capture_bar (c, mode):
	bw = 8 + 3 * 44 + 10 + 40
	x = (M.W - bw) / 2; y = 12
	c.rect (x, y, bw, 48, MENU, r = 12, outline = (150, 140, 132))
	for i, (n, fn) in enumerate (MODES):
		bx = x + 8 + i * 44
		if n == mode: c.rect (bx, y + 6, 40, 36, SEL, r = 8)
		fn (c, bx + 10, y + 14, 20, WHITE if n == mode else TEXT)
	cx = x + bw - 36
	c.vline (cx - 4, y + 10, y + 38, (200, 190, 182))
	c.line ([(cx + 8, y + 18), (cx + 20, y + 30)], TEXT, 2); c.line ([(cx + 20, y + 18), (cx + 8, y + 30)], TEXT, 2)
	return x, y, bw

def hint (c, s):
	w = c.tw (s, "ui") + 40
	c.rect ((M.W - w) / 2, M.H - 118, w, 34, M.A ((20, 20, 24), 200), r = 17)
	c.text_c ((M.W - w) / 2, M.H - 118, w, 34, s, "ui", WHITE)

def crosshair (c, x, y):
	for d, l in ((1, 0), (0, 1)):
		c.line ([(x - 12 * d, y - 12 * l), (x - 3 * d, y - 3 * l)], (0, 0, 0), 3); c.line ([(x + 3 * d, y + 3 * l), (x + 12 * d, y + 12 * l)], (0, 0, 0), 3)
		c.line ([(x - 12 * d, y - 12 * l), (x - 3 * d, y - 3 * l)], WHITE, 1.2); c.line ([(x + 3 * d, y + 3 * l), (x + 12 * d, y + 12 * l)], WHITE, 1.2)

# ---- the toolbar: New | the mode v, the delay v | Copy, Save As (once captured) ----------------------------------
TB_H = 52
def drop_button (c, x, y, w, h, icon, badge = None, on = False):
	"""An icon and its arrow: a drop-down button (on: its menu open)."""
	if on: c.rect (x, y, w, h, M.A ((255, 255, 255), 170), r = 6, outline = M.shade (FACE, 0.7))
	icon (c, x + 10, y + (h - 20) / 2, 20)
	ax = x + w - 13
	if badge:
		bw = c.tw (badge, "smallb") + 10
		c.rect (x + 30, y + h / 2 - 8, bw, 16, SEL, r = 8); c.text_c (x + 30, y + h / 2 - 8, bw, 16, badge, "smallb", WHITE)
	c.poly ([(ax - 4, y + h / 2 - 2), (ax + 4, y + h / 2 - 2), (ax, y + h / 2 + 3)], DIM)

def tool_button (c, x, y, w, h, icon, label, hot = False):
	if hot: c.rect (x, y, w, h, M.A ((255, 255, 255), 150), r = 6, outline = M.shade (FACE, 0.7))
	icon (c, x + 9, y + (h - 18) / 2, 18)
	c.text_l (x + 34, y, h, label, "ui")

def snip_toolbar (c, cx, cy, cw, mode = "Rectangle", delay = None, captured = False, open_menu = None, hot = None, tool = 1):
	"""The one toolbar; returns the places of its buttons (for the menus under them)."""
	c.rect (cx, cy, cw, TB_H, FACE)
	ty = cy + 9; th = 34
	pos = {}
	# New: the main action
	M.button (c, cx + 10, ty, 92, th, "", accent = True)
	ic_new (c, cx + 22, ty + 9, 16, WHITE); c.text_l (cx + 44, ty, th, "New", "uib", WHITE)
	x = cx + 112; c.vline (x, ty + 4, ty + th - 4, M.shade (FACE, 0.8)); x += 8
	# the mode, the delay
	icon = dict (MODES)[mode]
	pos["mode"] = x; drop_button (c, x, ty, 52, th, lambda c, a, b, s: icon (c, a, b, s, TEXT), on = open_menu == "mode"); x += 56
	dw = 52 + (c.tw (delay, "smallb") + 14 if delay else 0)
	pos["delay"] = x; drop_button (c, x, ty, dw, th, lambda c, a, b, s: ic_clock (c, a, b, s, TEXT, off = not delay), badge = delay, on = open_menu == "delay"); x += dw + 4
	# once a capture is made: Copy, Save As -- at the left, after the mode and the delay
	if captured:
		c.vline (x, ty + 4, ty + th - 4, M.shade (FACE, 0.8)); x += 8
		tool_button (c, x, ty, 84, th, lambda c, a, b, s: ic_copy (c, a, b + 1, 17, TEXT), "Copy", hot == "copy"); x += 88
		tool_button (c, x, ty, 118, th, lambda c, a, b, s: ic_save (c, a, b + 1, 17, TEXT), "Save As...", hot == "save"); x += 122
		# the drawing tools, in the middle: pen, marker (their colour under them), eraser | crop | undo, redo
		mx = cx + cw - 10 - (3 * 46 + 12 + 40 + 12 + 2 * 38)
		c.vline (mx - 8, ty + 4, ty + th - 4, M.shade (FACE, 0.8))
		for i, (fn, col) in enumerate (((lambda c, a, b, s: ic_pen (c, a, b, s, TEXT, (220, 60, 50)), (220, 60, 50)),
						 (lambda c, a, b, s: ic_marker (c, a, b, s, (250, 220, 40)), (250, 220, 40)),
						 (lambda c, a, b, s: ic_eraser (c, a, b, s, TEXT), None))):
			bx = mx + i * 46; on = (i == tool)
			if on: c.rect (bx, ty, 42, th, M.A ((255, 255, 255), 170), r = 6, outline = SEL, width = 2)
			fn (c, bx + 6, ty + 5, 22)
			if col: c.rect (bx + 8, ty + th - 6, 18, 3, col, r = 1); c.poly ([(bx + 32, ty + 15), (bx + 38, ty + 15), (bx + 35, ty + 19)], DIM)
		x2 = mx + 3 * 46 + 4; c.vline (x2, ty + 4, ty + th - 4, M.shade (FACE, 0.8)); x2 += 8
		ic_crop (c, x2 + 9, ty + 7, 20, TEXT); x2 += 44
		c.vline (x2, ty + 4, ty + th - 4, M.shade (FACE, 0.8)); x2 += 8
		ic_undo (c, x2 + 6, ty + 6, 22, TEXT); ic_undo (c, x2 + 44, ty + 6, 22, M.FAINT, True)
	c.hline (cx, cx + cw, cy + TB_H, M.shade (FACE, 0.85))
	return pos

def keycap (c, x, y, s):
	w = c.tw (s, "uib") + 18
	c.rect (x, y + 2, w, 26, M.shade (FACE, 0.75), r = 5)
	c.rect (x, y, w, 25, (252, 250, 248), r = 5, outline = M.shade (FACE, 0.6))
	c.text_c (x, y, w, 25, s, "uib")
	return w

def tick (c, x, y, col = TEXT):
	c.line ([(x, y + 5), (x + 4, y + 9), (x + 11, y + 1)], col, 2)

def menu_icons (c, x, y, w, items, sel, hot):
	"""A drop-down's menu: an icon (or none), a label, a tick at the one chosen."""
	h = 10 + 30 * len (items)
	c.rect (x, y, w, h, MENU, r = 8, outline = M.shade (FACE, 0.62))
	ry = y + 5
	for label, icon, key in items:
		on = label == hot
		if on: c.rect (x + 5, ry, w - 10, 30, SEL, r = 5)
		fg = WHITE if on else TEXT
		if label == sel: tick (c, x + 14, ry + 10, fg)
		lx = x + 36
		if icon: icon (c, lx, ry + 6, 18, fg); lx += 28
		c.text_l (lx, ry, 30, label, "ui", fg)
		if key: c.text_r (x + w - 14, ry, 30, key, "small", (226, 240, 244) if on else DIM)
		ry += 30
	return h

# ---- 1. the window, nothing captured yet ----------------------------------------------------------------------
START = (300, 200, 520, 220)
def empty_window (c, open_menu = None, delay = None, mode = "Rectangle"):
	x, y, w, h = START
	cx, cy, cw, ch = M.window (c, x, y, w, h, "Screenshot")
	pos = snip_toolbar (c, cx, cy, cw, mode = mode, delay = delay, open_menu = open_menu)
	by = cy + TB_H + (ch - TB_H) / 2 - 22
	a, b = "Press", "to start a capture"
	kw = c.tw ("Print Screen", "uib") + 18
	tot = c.tw (a) + 10 + kw + 10 + c.tw (b)
	tx = cx + (cw - tot) / 2
	c.text_l (tx, by, 25, a); tx += c.tw (a) + 10
	tx += keycap (c, tx, by, "Print Screen") + 10
	c.text_l (tx, by, 25, b)
	c.text_c (cx, by + 34, cw, 18, "or click New: the capture is copied, then saved when you want", "small", DIM)
	return cx, cy, cw, ch, pos

def shot_start ():
	c = screen ()
	empty_window (c)
	M.cursor (c, 352, 262)
	c.save ("screenshot-start.png")

def shot_mode ():
	c = screen ()
	cx, cy, cw, ch, pos = empty_window (c, open_menu = "mode")
	menu_icons (c, pos["mode"], cy + TB_H - 4, 210, [("Rectangle", lambda c, a, b, s, f: ic_region (c, a, b, s, f), "Print"),
		("Window", lambda c, a, b, s, f: ic_window (c, a, b, s, f), "Alt+Print"), ("Full screen", lambda c, a, b, s, f: ic_screen (c, a, b, s, f), "")],
		"Rectangle", "Window")
	M.cursor (c, pos["mode"] + 110, cy + TB_H + 48)
	c.save ("screenshot-mode.png")

def shot_delay_menu ():
	c = screen ()
	cx, cy, cw, ch, pos = empty_window (c, open_menu = "delay")
	menu_icons (c, pos["delay"], cy + TB_H - 4, 170, [("No delay", None, ""), ("3 seconds", None, ""), ("5 seconds", None, ""), ("10 seconds", None, "")],
		"No delay", "5 seconds")
	M.cursor (c, pos["delay"] + 90, cy + TB_H + 82)
	c.save ("screenshot-delay-menu.png")

# ---- 2. a region being chosen ------------------------------------------------------------------------------
def shot_region ():
	c = screen ()
	sx, sy, sw, sh = 392, 128, 468, 300
	dim (c, (sx, sy, sw, sh))
	c.rect (sx - 1, sy - 1, sw + 2, sh + 2, None, outline = WHITE, width = 2)
	for hx in (sx, sx + sw / 2, sx + sw):
		for hy in (sy, sy + sh / 2, sy + sh):
			if hx == sx + sw / 2 and hy == sy + sh / 2: continue
			c.ellipse (hx, hy, 5, WHITE, SEL, 2)
	label = "%d x %d" % (sw, sh)
	lw = c.tw (label, "smallb") + 16
	c.rect (sx, sy + sh + 8, lw, 22, M.A ((20, 20, 24), 210), r = 6); c.text_c (sx, sy + sh + 8, lw, 22, label, "smallb", WHITE)
	# the magnifier at the pointer: the pixels under it, enlarged
	px, py = sx + sw, sy + sh
	crop = DESK.crop ((px - 10, py - 7, px + 10, py + 7)).resize ((120 * K, 84 * K), Image.NEAREST)
	mx, my = px + 24, py + 16
	c.img.paste (crop, (int (mx * K), int (my * K))); c.d = ImageDraw.Draw (c.img, "RGBA")
	c.rect (mx, my, 120, 84, None, outline = WHITE, width = 2)
	c.line ([(mx + 60, my), (mx + 60, my + 84)], M.A (SEL, 200), 1); c.line ([(mx, my + 42), (mx + 120, my + 42)], M.A (SEL, 200), 1)
	c.rect (mx, my + 84, 120, 20, M.A ((20, 20, 24), 210)); c.text_c (mx, my + 84, 120, 20, "%d, %d" % (px, py), "small", WHITE)
	capture_bar (c, "Rectangle")
	hint (c, "Drag a rectangle  -  Enter: the whole screen  -  Esc: cancel")
	crosshair (c, px, py)
	c.save ("screenshot-region.png")

# ---- 3. a window chosen ----------------------------------------------------------------------------------------
def shot_window ():
	c = screen ()
	wx, wy, ww, wh = 389, 128, 627, 430			# the terminal's frame in desktop.png
	dim (c, (wx, wy, ww, wh))
	c.rect (wx - 2, wy - 2, ww + 4, wh + 4, None, r = 8, outline = SEL, width = 4)
	label = "terminal  -  627 x 430"
	lw = c.tw (label, "uib") + 24
	c.rect (wx + (ww - lw) / 2, wy + wh / 2 - 17, lw, 34, M.A (SEL, 235), r = 17); c.text_c (wx + (ww - lw) / 2, wy + wh / 2 - 17, lw, 34, label, "uib", WHITE)
	capture_bar (c, "Window")
	hint (c, "Click a window  -  Tab: the next one  -  Esc: cancel")
	M.cursor (c, 700, 420)
	c.save ("screenshot-window.png")

# ---- 4. the delay counting down --------------------------------------------------------------------------------
def shot_delay ():
	c = screen ()
	cx, cy = M.W / 2, M.H / 2 - 30
	c.ellipse (cx, cy, 70, M.A ((20, 20, 24), 190))
	c.d.arc ([(cx - 66) * K, (cy - 66) * K, (cx + 66) * K, (cy + 66) * K], -90, 150, fill = M.A (SEL, 255), width = 6 * K)
	c.text (cx, cy + 4, "2", "huge" if "huge" in M.F else "big", WHITE, "mm")
	t = "Capturing the screen in 2 s  -  Esc: cancel"; tw = c.tw (t, "uib") + 36
	c.rect (cx - tw / 2, cy + 82, tw, 30, M.A ((20, 20, 24), 200), r = 15)
	c.text_c (cx - tw / 2, cy + 82, tw, 30, t, "uib", WHITE)
	c.save ("screenshot-delay.png")

# ---- 5. the capture made: shown, Copy and Save As at the left ----------------------------------------------------
def shot_result ():
	c = screen ()
	x, y, w, h = 30, 40, 964, 610
	cx, cy, cw, ch = M.window (c, x, y, w, h, "Screenshot")
	snip_toolbar (c, cx, cy, cw, delay = "3 s", captured = True, hot = "save")
	# the capture, fitted, on a soft backdrop
	ax, ay, aw, ah = cx, cy + TB_H + 1, cw, ch - TB_H - 1 - 24
	c.rect (ax, ay, aw, ah, (196, 186, 178))
	cap = DESK.crop ((389, 128, 389 + 627, 128 + 430))
	S = min ((aw - 48) / 627, (ah - 48) / 430, 1.0)
	iw, ih = 627 * S, 430 * S
	ix, iy = ax + (aw - iw) / 2, ay + (ah - ih) / 2
	c.rect (ix + 3, iy + 5, iw, ih, M.A ((0, 0, 0), 40))
	c.img.paste (cap.resize ((int (iw * K), int (ih * K)), Image.LANCZOS), (int (ix * K), int (iy * K))); c.d = ImageDraw.Draw (c.img, "RGBA")
	c.rect (ix, iy, iw, ih, None, outline = (150, 140, 132))
	def P (px, py): return (ix + (px - 389) * S, iy + (py - 128) * S)
	for (a, b) in (((394, 359), (566, 359)),):				# the marker over "echo onyx | wc -c"
		c.line ([P (*a), P (*b)], M.A ((250, 220, 40), 120), 13 * S)
	p0 = P (400, 375); c.d.ellipse ([(p0[0] - 12) * K, (p0[1] - 12) * K, (p0[0] + 14) * K, (p0[1] + 12) * K], outline = (220, 60, 50), width = int (3 * K))
	a0, a1 = P (520, 430), P (416, 384)
	c.line ([a0, ((a0[0] + a1[0]) / 2 + 6, (a0[1] + a1[1]) / 2 + 18), a1], (220, 60, 50), 3)
	c.poly ([a1, (a1[0] + 15, a1[1] + 1), (a1[0] + 7, a1[1] + 13)], (220, 60, 50))
	# the status bar
	c.rect (cx, cy + ch - 24, cw, 24, FACE); c.hline (cx, cx + cw, cy + ch - 24, M.shade (FACE, 0.85))
	c.ellipse (cx + 12, cy + ch - 12, 4, M.GREEN)
	c.text_l (cx + 22, cy + ch - 24, 24, "Window ‘terminal’  -  627 x 430  -  copied to the clipboard  -  not saved", "ui")
	c.text_r (cx + cw - 10, cy + ch - 24, 24, "%d %%" % round (S * 100), "ui", DIM)
	# Save As... is hovered: its tip
	M.cursor (c, cx + 410, cy + 30)
	tip = "Save As... (^S)  -  PNG, JPEG or BMP"
	tw = c.tw (tip, "small") + 16
	c.rect (cx + 410, cy + 54, tw, 22, (252, 248, 236), r = 4, outline = (150, 140, 132)); c.text_c (cx + 410, cy + 54, tw, 22, tip, "small")
	c.save ("screenshot-result.png")

if __name__ == "__main__":
	M.F["huge"] = M._f ("DejaVuSans-Bold.ttf", 54)
	for f in ("screenshot-editor.png",):
		p = os.path.join (M.OUT, f)
		if os.path.exists (p): os.remove (p)
	shot_start (); shot_mode (); shot_delay_menu (); shot_region (); shot_window (); shot_delay (); shot_result ()
