#!/usr/bin/env python3
"""mockup_clipboard.py -- the first mock-ups of Onyx's shared clipboard (a service every app reaches by IPC:
a ring of 10 items of any type -- text, image, paths, rich text --, a cursor on the one Ctrl+V pastes)
and of its front end: a panel from the menu bar, and the same as a window. See docs/clipboard/README.md.

    python3 tools/screenshot/mockup_clipboard.py  -> docs/clipboard/mockups/clipboard-*.png

On the real desktop (screenshots/desktop.png, 1024 x 768); the helpers are mockup_archiver.py's.
"""
import os, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, HERE)
import mockup_archiver as M

M.W, M.H = 1024, 768
M.OUT = os.path.join (M.ROOT, "docs", "clipboard", "mockups")
K = M.K
DESK = Image.open (os.path.join (M.ROOT, "screenshots", "desktop.png")).convert ("RGB")
TEXT, DIM, FACE, SEL, MENU, WHITE = M.TEXT, M.DIM, M.FACE, M.SEL, M.MENU, (255, 255, 255)
SHOT = DESK.crop ((392, 128, 860, 428))

# the items, newest first: (type, preview lines, source, when, size, flags)
ITEMS = [
	("text", ["Dentist at 17:30 -- call them before"], "Calendar", "12:34", "36 chars", ""),
	("image", ["Screenshot 468 x 300"], "Screenshot", "12:31", "549 KB", ""),
	("files", ["kapi.cpp", "kapi_abi.h"], "File Viewer", "12:28", "2 files", "cut"),
	("rich", ["Onyx -- a homemade OS for the Pi 4"], "Writer", "12:20", "rich text", ""),
	("text", ["static int g_folder;", "static int g_hist[64];"], "Tinypad", "12:12", "2 lines", ""),
	("url", ["https://github.com/stephaneweg/onyx"], "Jet Browser", "11:58", "link", ""),
	("text", ["ls /bin | grep e"], "Terminal", "11:40", "16 chars", ""),
]
CURSOR = 1						# the item Ctrl+V pastes (here: the screenshot)

def screen ():
	c = M.Canvas ()
	c.img.paste (DESK.resize ((M.W * K, M.H * K), Image.LANCZOS), (0, 0))
	return c

def ic_clip (c, x, y, s, col):
	c.rect (x + 2, y + 3, s - 4, s - 3, None, r = 2, outline = col, width = 2)
	c.rect (x + s / 2 - 4, y, 8, 5, col, r = 1.5)
def ic_type (c, kind, x, y, s = 22):
	col = { "text": (96, 104, 116), "image": (78, 160, 92), "files": (226, 180, 92), "rich": (60, 96, 176), "url": (40, 120, 180) }[kind]
	c.rect (x, y, s, s, col, r = 5)
	if kind == "text":
		for i in range (3): c.rect (x + 5, y + 6 + i * 4, s - 10 - (i == 2) * 5, 2, WHITE)
	elif kind == "image":
		c.poly ([(x + 4, y + s - 5), (x + 10, y + 9), (x + 15, y + s - 8), (x + 18, y + s - 11), (x + s - 3, y + s - 5)], WHITE); c.ellipse (x + s - 7, y + 7, 2.5, WHITE)
	elif kind == "files":
		c.rect (x + 4, y + 7, s - 8, s - 11, WHITE, r = 2); c.rect (x + 4, y + 5, 7, 3, WHITE, r = 1)
	elif kind == "rich":
		c.text_c (x, y, s, s, "A", "uib", WHITE); c.rect (x + 5, y + s - 5, s - 10, 2, WHITE)
	elif kind == "url":
		c.ellipse (x + s / 2, y + s / 2, 7, None, WHITE, 2); c.line ([(x + 4, y + s / 2), (x + s - 4, y + s / 2)], WHITE, 1.5)

def item_row (c, x, y, w, it, i, hot = False, wide = False):
	kind, lines, src, when, size, flags = it
	h = 64 if kind == "image" else 52
	cur = i == CURSOR
	if cur: c.rect (x, y, w, h, M.mix (MENU, SEL, 0.18), r = 8, outline = SEL, width = 2)
	elif hot: c.rect (x, y, w, h, M.mix (MENU, SEL, 0.08), r = 8)
	ic_type (c, kind, x + 12, y + 10)
	tx = x + 46
	if kind == "image":
		c.img.paste (SHOT.resize ((int (84 * K), int (54 * K)), Image.LANCZOS), (int (tx * K), int ((y + 5) * K))); c.d = ImageDraw.Draw (c.img, "RGBA")
		c.rect (tx, y + 5, 84, 54, None, outline = (170, 160, 150))
		c.text (tx + 94, y + 10, lines[0], "uib")
		c.text (tx + 94, y + 32, src + "  -  " + when, "small", DIM)
	else:
		first = lines[0] + ("  (+%d)" % (len (lines) - 1) if len (lines) > 1 and kind != "files" else "")
		if kind == "files": first = ", ".join (lines)
		mono = kind == "text" and src in ("Tinypad", "Terminal")
		t = first
		while c.tw (t, "mono" if mono else "ui") > w - 120 and len (t) > 4: t = t[:-2]
		if t != first: t = t[:-1] + "..."
		c.text (tx, y + 8, t, "mono" if mono else "ui", M.LINK if kind == "url" else TEXT)
		meta = src + "  -  " + when + "  -  " + size
		c.text (tx, y + 29, meta, "small", DIM)
		if flags == "cut":
			bx = tx + c.tw (meta, "small") + 10
			c.rect (bx, y + 28, c.tw ("cut", "smallb") + 12, 16, (226, 160, 58), r = 8); c.text_c (bx, y + 28, c.tw ("cut", "smallb") + 12, 16, "cut", "smallb", WHITE)
	# the right side: the cursor's mark, or the hovered row's actions
	if cur:
		lab = "Ctrl+V pastes this"
		lw = c.tw (lab, "smallb") + 16
		c.rect (x + w - lw - 10, y + h - 28, lw, 20, SEL, r = 10); c.text_c (x + w - lw - 10, y + h - 28, lw, 20, lab, "smallb", WHITE)
	elif hot:
		c.rect (x + w - 34, y + 12, 24, 24, M.A ((0, 0, 0), 18), r = 6)
		c.line ([(x + w - 27, y + 19), (x + w - 17, y + 29)], DIM, 2); c.line ([(x + w - 17, y + 19), (x + w - 27, y + 29)], DIM, 2)
	return h

# ---- 1. the panel, from the menu bar -----------------------------------------------------------------------------
def shot_panel ():
	c = screen ()
	# the clipboard's icon among the menu bar's (left of the volume), lit
	ix = 878
	c.rect (ix - 6, 2, 30, 22, SEL, r = 5); ic_clip (c, ix, 4, 18, WHITE)
	c.ellipse (ix + 18, 6, 6, (226, 80, 76)); c.text_c (ix + 12, 0, 12, 12, "7", "tiny", WHITE)
	pw = 440; px = M.W - pw - 12; py = 30
	rows = sum (64 if it[0] == "image" else 52 for it in ITEMS) + 6 * len (ITEMS)
	ph = 56 + rows + 56
	c.rect (px, py, pw, ph, MENU, r = 12, outline = M.shade (FACE, 0.62))
	c.poly ([(ix + 3, py), (ix + 9, py - 6), (ix + 15, py)], MENU)
	ic_clip (c, px + 16, py + 16, 20, TEXT)
	c.text_l (px + 44, py + 10, 32, "Clipboard", "big")
	c.text_r (px + pw - 16, py + 10, 32, "7 of 10", "small", DIM)
	c.hline (px + 12, px + pw - 12, py + 50, (226, 218, 212))
	y = py + 58
	for i, it in enumerate (ITEMS):
		y += item_row (c, px + 10, y, pw - 20, it, i, hot = i == 4) + 6
	c.hline (px + 12, px + pw - 12, y + 2, (226, 218, 212))
	M.button (c, px + 14, y + 12, 104, 30, "Clear All")
	c.text_l (px + 132, y + 12, 30, "Click an item: Ctrl+V pastes it", "small", DIM)
	M.cursor (c, px + pw - 60, py + 58 + 52 * 2 + 64 + 6 * 4 + 52 + 20)
	c.save ("clipboard-panel.png")

# ---- 2. the same as a window: the list, the item shown whole ---------------------------------------------------------
def shot_window ():
	c = screen ()
	x, y, w, h = 120, 60, 800, 560
	cx, cy, cw, ch = M.window (c, x, y, w, h, "Clipboard")
	# the toolbar
	c.rect (cx, cy, cw, 46, FACE); c.hline (cx, cx + cw, cy + 46, M.shade (FACE, 0.85))
	M.button (c, cx + 10, cy + 8, 120, 30, "Paste Next")
	M.button (c, cx + 138, cy + 8, 90, 30, "Delete")
	M.button (c, cx + 236, cy + 8, 100, 30, "Clear All")
	M.field (c, cx + cw - 230, cy + 10, 220, 26, "", "Search in the clipboard", "small")
	# the list
	lx, ly, lw = cx + 10, cy + 56, 380
	c.rect (lx, ly, lw, ch - 56 - 34, M.LIST, r = 6, outline = M.LINE)
	yy = ly + 6
	for i, it in enumerate (ITEMS):
		yy += item_row (c, lx + 4, yy, lw - 8, it, i) + 4
	# the item at the cursor, whole
	vx, vw = lx + lw + 10, cw - lw - 30
	c.rect (vx, ly, vw, ch - 56 - 34, M.LIST, r = 6, outline = M.LINE)
	c.text (vx + 14, ly + 12, "Screenshot 468 x 300", "uib")
	c.text (vx + 14, ly + 32, "An image  -  from Screenshot at 12:31  -  549 KB", "small", DIM)
	iw = vw - 28; ih = iw * 300 / 468
	c.img.paste (SHOT.resize ((int (iw * K), int (ih * K)), Image.LANCZOS), (int ((vx + 14) * K), int ((ly + 58) * K))); c.d = ImageDraw.Draw (c.img, "RGBA")
	c.rect (vx + 14, ly + 58, iw, ih, None, outline = (170, 160, 150))
	c.text (vx + 14, ly + 70 + ih, "Pastes as an image in Paint, Writer...;", "small", DIM)
	c.text (vx + 14, ly + 88 + ih, "as a PNG file in the File Viewer.", "small", DIM)
	M.button (c, vx + 14, ly + 116 + ih, 150, 30, "Save as PNG...")
	# the status bar
	c.rect (cx, cy + ch - 24, cw, 24, FACE); c.hline (cx, cx + cw, cy + ch - 24, M.shade (FACE, 0.85))
	c.ellipse (cx + 12, cy + ch - 12, 4, M.GREEN)
	c.text_l (cx + 22, cy + ch - 24, 24, "7 of 10 items  -  1.2 MB in memory", "ui")
	c.text_r (cx + cw - 10, cy + ch - 24, 24, "Up / Down: choose  -  Enter: paste next  -  Del: delete", "ui", DIM)
	c.save ("clipboard-window.png")

if __name__ == "__main__":
	shot_panel (); shot_window ()
