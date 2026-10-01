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

# ---- the widget: bottom right, above the dock's top (never under it, whatever its width) --------------------------------
DOCK_TOP = 676						# the dock's top edge in desktop.png
def widget (c, rows, cursor, hot = -1, fresh = -1, title = "Clipboard"):
	ww, rh = 300, 30
	wh = 40 + len (rows) * rh + 10
	wx, wy = M.W - ww - 12, DOCK_TOP - 10 - wh
	c.rect (wx, wy, ww, wh, M.A (MENU, 245), r = 12, outline = M.shade (FACE, 0.62))
	ic_clip (c, wx + 14, wy + 11, 18, TEXT)
	c.text_l (wx + 40, wy + 6, 28, title, "uib")
	c.text_l (wx + 40 + c.tw (title, "uib") + 8, wy + 6, 28, "%d / 10" % len (rows), "small", DIM)
	# Clear All: a small bin at the top right
	bx = wx + ww - 34
	c.rect (bx, wy + 8, 24, 24, M.A ((0, 0, 0), 14), r = 6)
	c.rect (bx + 7, wy + 14, 10, 13, None, r = 2, outline = DIM, width = 1.6); c.rect (bx + 5, wy + 12, 14, 2, DIM)
	c.hline (wx + 10, wx + ww - 10, wy + 38, (226, 218, 212))
	y = wy + 42
	for i, (kind, text, src, when) in enumerate (rows):
		cur = i == cursor
		if cur: c.rect (wx + 6, y, ww - 12, rh - 2, M.mix (MENU, SEL, 0.22), r = 6, outline = SEL, width = 1.5)
		elif i == hot: c.rect (wx + 6, y, ww - 12, rh - 2, M.mix (MENU, SEL, 0.08), r = 6)
		if i == fresh: c.rect (wx + 6, y, 4, rh - 2, (78, 160, 92), r = 2)
		ic_type (c, kind, wx + 14, y + 5, 18)
		right = when if i != hot else ""
		mono = src in ("Tinypad", "Terminal")
		f = "mono" if mono else "ui"
		limit = ww - 44 - 14 - (c.tw (right, "small") + 10 if right else 30)
		t = text
		while c.tw (t, f) > limit and len (t) > 4: t = t[:-2]
		if t != text: t = t[:-1] + "..."
		c.text_l (wx + 40, y, rh - 2, t, f, M.LINK if kind == "url" else TEXT)
		if i == hot:
			c.line ([(wx + ww - 26, y + 9), (wx + ww - 17, y + 18)], DIM, 1.8); c.line ([(wx + ww - 17, y + 9), (wx + ww - 26, y + 18)], DIM, 1.8)
		elif right: c.text_r (wx + ww - 14, y, rh - 2, right, "small", DIM)
		y += rh
	return wx, wy, ww, wh

ROWS = [("text", "Dentist at 17:30 -- call them before", "Calendar", "12:34"),
	("image", "Image  468 x 300", "Screenshot", "12:31"),
	("files", "kapi.cpp, kapi_abi.h  (cut)", "File Viewer", "12:28"),
	("rich", "Onyx -- a homemade OS for the Pi 4", "Writer", "12:20"),
	("text", "static int g_folder;", "Tinypad", "12:12"),
	("url", "https://github.com/stephaneweg/onyx", "Jet Browser", "11:58"),
	("text", "ls /bin | grep e", "Terminal", "11:40")]

def toast_tag (c, wx, wy, s):
	w = c.tw (s, "smallb") + 16
	c.rect (wx + 300 - w - 44, wy + 12, w, 18, (78, 160, 92), r = 9); c.text_c (wx + 300 - w - 44, wy + 12, w, 18, s, "smallb", WHITE)

# 1. Ctrl+C in the calendar: the widget comes to the front, the new item on top, the cursor on it
def shot_widget ():
	c = screen ()
	wx, wy, ww, wh = widget (c, ROWS, cursor = 0, fresh = 0)
	toast_tag (c, wx, wy, "Copied")
	c.save ("clipboard-widget.png")

# 2. a dock as wide as the screen: the widget above it; a click put the cursor on the image, the
#    pointer over a row shows its x (delete)
def shot_widget_dock ():
	c = screen ()
	# the dock stretched to the whole width (its face extended, its icons kept in the middle)
	band = DESK.crop ((183, 676, 841, 756))
	face = DESK.getpixel ((200, 740))
	c.rect (6, DOCK_TOP, M.W - 12, 80, face, r = 10, outline = M.shade (face, 0.7))
	c.img.paste (band.resize ((int (658 * K), int (80 * K)), Image.LANCZOS), (int ((M.W - 658) / 2 * K), int (DOCK_TOP * K))); c.d = ImageDraw.Draw (c.img, "RGBA")
	wx, wy, ww, wh = widget (c, ROWS, cursor = 1, hot = 4)
	M.cursor (c, wx + ww - 22, wy + 42 + 4 * 30 + 16)
	c.save ("clipboard-widget-dock.png")

if __name__ == "__main__":
	shot_widget (); shot_widget_dock ()
