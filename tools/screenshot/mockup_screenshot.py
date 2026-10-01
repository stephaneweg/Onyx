#!/usr/bin/env python3
"""mockup_screenshot.py -- the first mock-ups of Onyx's screen capture tool, Screenshot, (as Windows 10's Snip & Sketch):
a capture of a region, a window or the whole screen, now or after a delay; then the picture in an
editor -- a pen and a marker to draw on it, an eraser, a crop --, saved and copied to the clipboard.
See docs/screenshot/README.md.

    python3 tools/screenshot/mockup_screenshot.py  -> docs/screenshot/mockups/capture-*.png

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
def ic_clock (c, x, y, s, col):
	c.ellipse (x + s / 2, y + s / 2, s / 2 - 1, None, col, 2); c.line ([(x + s / 2, y + 4), (x + s / 2, y + s / 2), (x + s / 2 + 4, y + s / 2 + 2)], col, 2)
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

# ---- the bar on the dimmed screen --------------------------------------------------------------------------
def capture_bar (c, mode, delay = "No delay"):
	items = [("Region", ic_region), ("Window", ic_window), ("Screen", ic_screen)]
	bw = 44 * 3 + 12 + 150 + 12 + 40
	x = (M.W - bw) / 2; y = 12
	c.rect (x, y, bw, 48, MENU, r = 12, outline = (150, 140, 132))
	for i, (n, fn) in enumerate (items):
		bx = x + 8 + i * 44
		if n == mode: c.rect (bx, y + 6, 40, 36, SEL, r = 8)
		fn (c, bx + 10, y + 14, 20, WHITE if n == mode else TEXT)
	sx = x + 8 + 3 * 44 + 4
	c.vline (sx, y + 10, y + 38, (200, 190, 182))
	ic_clock (c, sx + 12, y + 14, 20, TEXT)
	c.text_l (sx + 40, y, 48, delay, "ui")
	c.poly ([(sx + 136, y + 22), (sx + 146, y + 22), (sx + 141, y + 28)], DIM)
	cx = x + bw - 32
	c.vline (cx - 6, y + 10, y + 38, (200, 190, 182))
	c.line ([(cx + 6, y + 18), (cx + 18, y + 30)], TEXT, 2); c.line ([(cx + 18, y + 18), (cx + 6, y + 30)], TEXT, 2)
	return x, y, bw

def hint (c, s):
	w = c.tw (s, "ui") + 40
	c.rect ((M.W - w) / 2, M.H - 118, w, 34, M.A ((20, 20, 24), 200), r = 17)
	c.text_c ((M.W - w) / 2, M.H - 118, w, 34, s, "ui", WHITE)

def crosshair (c, x, y):
	for d, l in ((1, 0), (0, 1)):
		c.line ([(x - 12 * d, y - 12 * l), (x - 3 * d, y - 3 * l)], (0, 0, 0), 3); c.line ([(x + 3 * d, y + 3 * l), (x + 12 * d, y + 12 * l)], (0, 0, 0), 3)
		c.line ([(x - 12 * d, y - 12 * l), (x - 3 * d, y - 3 * l)], WHITE, 1.2); c.line ([(x + 3 * d, y + 3 * l), (x + 12 * d, y + 12 * l)], WHITE, 1.2)

# ---- 1. the app's own window: what to capture, when ------------------------------------------------------------
def shot_start ():
	c = screen ()
	x, y, w, h = 430, 230, 440, 300
	cx, cy, cw, ch = M.window (c, x, y, w, h, "Screenshot")
	c.text (cx + 20, cy + 16, "Take a capture of the screen", "big")
	c.text (cx + 20, cy + 40, "draw on it, save it, copy it", "small", DIM)
	# what
	c.text (cx + 20, cy + 70, "WHAT", "smallb", DIM)
	for i, (n, fn) in enumerate ([("Region", ic_region), ("Window", ic_window), ("Screen", ic_screen)]):
		bx = cx + 20 + i * 132; on = i == 0
		c.rect (bx, cy + 88, 124, 64, SEL if on else (244, 238, 234), r = 8, outline = M.shade (FACE, 0.7) if not on else None)
		fn (c, bx + 50, cy + 96, 24, WHITE if on else TEXT)
		c.text_c (bx, cy + 126, 124, 20, n, "uib" if on else "ui", WHITE if on else TEXT)
	c.text (cx + 20, cy + 166, "DELAY", "smallb", DIM)
	M.segmented (c, cx + 20, cy + 184, 30, ["None", "3 s", "5 s", "10 s"], 0)
	M.checkbox (c, cx + 20, cy + 236, "Show the pointer", False)
	# the New button, its menu open
	bx, by = cx + cw - 20 - 170, cy + ch - 50
	M.button (c, bx, by, 136, 34, "New capture", accent = True)
	c.rect (bx + 136, by, 34, 34, M.shade (SEL, 0.86), r = 5); c.poly ([(bx + 147, by + 15), (bx + 159, by + 15), (bx + 153, by + 21)], WHITE)
	M.menu_popup (c, bx + 6, by + 38, 210, [("Capture now", "Print"), ("In 3 seconds", ""), ("In 5 seconds", ""), ("In 10 seconds", ""), None,
		("Open a picture...", "^O")], hot = "In 3 seconds")
	M.cursor (c, bx + 120, by + 88)
	c.save ("screenshot-start.png")

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
	capture_bar (c, "Region")
	hint (c, "Drag a region  -  Enter: the whole screen  -  Esc: cancel")
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
	capture_bar (c, "Window", "3 seconds")
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

# ---- 5. the editor: the capture, drawn on --------------------------------------------------------------------
def tool_button (c, x, y, w, h, icon, label = None, on = False, split = False):
	if on: c.rect (x, y, w, h, M.A ((255, 255, 255), 150), r = 6, outline = M.shade (FACE, 0.7))
	iw = 22
	icon (c, x + (w - iw - (14 if split else 0)) / 2 if not label else x + 8, y + (h - iw) / 2, iw)
	if label: c.text_l (x + 36, y, h, label, "ui")
	if split: c.poly ([(x + w - 14, y + h / 2 - 2), (x + w - 6, y + h / 2 - 2), (x + w - 10, y + h / 2 + 3)], DIM)

def shot_editor ():
	c = screen ()
	x, y, w, h = 40, 34, 944, 640
	cx, cy, cw, ch = M.window (c, x, y, w, h, "Screenshot - Screenshot 2026-10-01 12-34.png")
	# the toolbar
	ty = cy + 6
	c.rect (cx, cy, cw, 52, FACE)
	M.button (c, cx + 10, ty + 4, 92, 34, "")
	ic_new (c, cx + 20, ty + 13, 16, TEXT); c.text_l (cx + 42, ty + 4, 34, "New", "ui"); c.poly ([(cx + 84, ty + 19), (cx + 92, ty + 19), (cx + 88, ty + 24)], DIM)
	tx = cx + 128
	tools = [(lambda c, a, b, s: ic_pen (c, a, b, s, TEXT, (220, 60, 50)), False, "Pen"), (lambda c, a, b, s: ic_marker (c, a, b, s, (250, 220, 40)), True, "Marker"),
		 (lambda c, a, b, s: ic_eraser (c, a, b, s, TEXT), False, "Eraser")]
	for i, (fn, on, _) in enumerate (tools):
		tool_button (c, tx + i * 52, ty + 2, 48, 38, fn, on = on, split = i < 2)
	sx = tx + 3 * 52 + 6; c.vline (sx, ty + 6, ty + 36, M.shade (FACE, 0.8))
	tool_button (c, sx + 8, ty + 2, 40, 38, lambda c, a, b, s: ic_crop (c, a, b, s, TEXT))
	sx += 56; c.vline (sx, ty + 6, ty + 36, M.shade (FACE, 0.8))
	tool_button (c, sx + 8, ty + 2, 36, 38, lambda c, a, b, s: ic_undo (c, a, b, s, TEXT))
	tool_button (c, sx + 46, ty + 2, 36, 38, lambda c, a, b, s: ic_undo (c, a, b, s, TEXT, True))
	sx += 92; c.vline (sx, ty + 6, ty + 36, M.shade (FACE, 0.8))
	c.text_l (sx + 12, ty + 2, 38, "-", "big"); c.text_c (sx + 26, ty + 2, 50, 38, "100 %", "ui"); c.text_l (sx + 80, ty + 2, 38, "+", "big")
	# the right side: copy, save, more
	rx = cx + cw - 10
	for lab, fn, wdt in (("Save As...", None, 96), ("Save", ic_save, 84), ("Copy", ic_copy, 84)):
		rx -= wdt + 6
		if fn: tool_button (c, rx, ty + 2, wdt, 38, lambda c, a, b, s, f = fn: f (c, a + 1, b + 2, 18, TEXT), lab)
		else: M.button (c, rx, ty + 4, wdt, 34, lab)
	rx -= 46; tool_button (c, rx, ty + 2, 40, 38, lambda c, a, b, s: ic_paint (c, a, b, s, TEXT))
	c.hline (cx, cx + cw, cy + 52, M.shade (FACE, 0.85))
	# the canvas: the capture on a soft backdrop, with the drawings
	ax, ay, aw, ah = cx, cy + 53, cw, ch - 53 - 24
	c.rect (ax, ay, aw, ah, (196, 186, 178))
	cap = DESK.crop ((392, 128, 392 + 468, 128 + 300))
	iw, ih = 468 * 1.25, 300 * 1.25
	ix, iy = ax + (aw - iw) / 2, ay + (ah - ih) / 2
	c.rect (ix + 4, iy + 6, iw, ih, M.A ((0, 0, 0), 40))
	c.img.paste (cap.resize ((int (iw * K), int (ih * K)), Image.LANCZOS), (int (ix * K), int (iy * K))); c.d = ImageDraw.Draw (c.img, "RGBA")
	S = 1.25
	def P (px, py): return (ix + (px - 392) * S, iy + (py - 128) * S)
	# the marker: yellow, see-through, over two lines of ps
	for (a, b) in (((396, 279), (530, 279)), ((396, 343), (536, 343))):
		c.line ([P (*a), P (*b)], M.A ((250, 220, 40), 120), 14 * S)
	# the pen: a red ring around "echo onyx | wc -c" and an arrow to it, a word
	cxp, cyp = P (492, 361)
	c.d.ellipse ([(cxp - 118) * K, (cyp - 22) * K, (cxp + 112) * K, (cyp + 20) * K], outline = (220, 60, 50), width = int (3.5 * K))
	a0, a1 = P (700, 400), P (600, 372)
	c.line ([a0, ((a0[0] + a1[0]) / 2 + 10, (a0[1] + a1[1]) / 2 + 22), a1], (220, 60, 50), 3.5)
	c.poly ([a1, (a1[0] + 16, a1[1] + 2), (a1[0] + 8, a1[1] + 14)], (220, 60, 50))
	c.text (a0[0] - 40, a0[1] + 4, "it counts 5 bytes", "big", (250, 110, 90))
	# the marker's options, open under its button
	px0, py0 = tx + 52 - 30, ty + 46
	pw, ph = 236, 128
	c.rect (px0, py0, pw, ph, MENU, r = 10, outline = M.shade (FACE, 0.62))
	c.poly ([(px0 + 54, py0), (px0 + 62, py0 - 8), (px0 + 70, py0)], MENU)
	c.text (px0 + 14, py0 + 10, "COLOUR", "smallb", DIM)
	for i, col in enumerate (PEN_COLOURS):
		ex, ey = px0 + 26 + (i % 8) * 26, py0 + 44
		c.ellipse (ex, ey, 10, col, (170, 160, 150), 1)
		if col == (250, 220, 40): c.ellipse (ex, ey, 13, None, SEL, 2.5)
	c.text (px0 + 14, py0 + 66, "SIZE", "smallb", DIM)
	c.rect (px0 + 14, py0 + 94, pw - 70, 6, (220, 212, 206), r = 3); c.rect (px0 + 14, py0 + 94, (pw - 70) * 0.55, 6, SEL, r = 3)
	c.ellipse (px0 + 14 + (pw - 70) * 0.55, py0 + 97, 8, WHITE, SEL, 2)
	c.line ([(px0 + pw - 44, py0 + 97), (px0 + pw - 16, py0 + 97)], M.A ((250, 220, 40), 160), 12)
	# the status bar
	c.rect (cx, cy + ch - 24, cw, 24, FACE); c.hline (cx, cx + cw, cy + ch - 24, M.shade (FACE, 0.85))
	c.ellipse (cx + 12, cy + ch - 12, 4, M.GREEN)
	c.text_l (cx + 22, cy + ch - 24, 24, "468 x 300  -  copied to the clipboard  -  not saved yet", "ui")
	c.text_r (cx + cw - 10, cy + ch - 24, 24, "Marker: drag to draw, Shift: a straight line", "ui", DIM)
	# the notification bubble (notifyd's), bottom right of the screen
	nx, ny, nw, nh = M.W - 372, M.H - 214, 352, 84
	c.rect (nx, ny, nw, nh, M.A (MENU, 250), r = 10, outline = M.shade (FACE, 0.62))
	c.img.paste (cap.resize ((96 * K, 62 * K), Image.LANCZOS), (int ((nx + 12) * K), int ((ny + 11) * K))); c.d = ImageDraw.Draw (c.img, "RGBA")
	c.rect (nx + 12, ny + 11, 96, 62, None, outline = (180, 170, 160))
	c.text (nx + 120, ny + 12, "Screenshot copied to the clipboard", "uib")
	c.text (nx + 120, ny + 34, "Saved in SD:/Pictures/Screenshots", "small", DIM)
	c.text (nx + 120, ny + 52, "Click to draw on it", "small", M.LINK)
	M.cursor (c, a1[0] + 40, a1[1] - 30)
	c.save ("screenshot-editor.png")

if __name__ == "__main__":
	M.F["huge"] = M._f ("DejaVuSans-Bold.ttf", 54)
	shot_start (); shot_region (); shot_window (); shot_delay (); shot_editor ()
