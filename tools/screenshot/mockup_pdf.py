#!/usr/bin/env python3
"""mockup_pdf.py -- the first mock-ups of Onyx's PDF viewer (a polished reader, in the way of Acrobat Reader /
Edge / Evince / Preview) and of the PDF export in Writer. See docs/pdf/README.md.

    python3 tools/screenshot/mockup_pdf.py  -> docs/pdf/mockups/pdf-*.png

On the real desktop (screenshots/desktop.png, 1024 x 768); the drawing helpers are mockup_archiver.py's. The
pages shown are real: Onyx's own manuals (sdcard/manuals/*/*.pdf), rendered by pdftoppm, and the search hits
placed with pdftotext -bbox-layout (poppler-utils).
"""
import os, sys, re, subprocess, functools, html
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, HERE)
import mockup_archiver as M

M.W, M.H = 1024, 768
M.OUT = os.path.join (M.ROOT, "docs", "pdf", "mockups")
K = M.K
DESK = Image.open (os.path.join (M.ROOT, "screenshots", "desktop.png")).convert ("RGB")
TEXT, DIM, FACE, SEL, MENU, WHITE, LIST, LINE2, FAINT = M.TEXT, M.DIM, M.FACE, M.SEL, M.MENU, (255, 255, 255), M.LIST, M.LINE2, M.FAINT
SIDE = (226, 216, 209)
CANVAS = (132, 126, 122)			# behind the pages: a warm grey, the pages stand out
HIT, HIT_CUR = (255, 214, 64, 120), (255, 140, 30, 150)
M.F["huge"] = M._f ("DejaVuSans-Bold.ttf", 26)
M.F["h2"] = M._f ("DejaVuSans-Bold.ttf", 18)
M.F["mid"] = M._f ("DejaVuSans.ttf", 15)

LEDGER = os.path.join (M.ROOT, "sdcard", "manuals", "ledger", "Ledger.pdf")
KOTON = os.path.join (M.ROOT, "sdcard", "manuals", "koton", "Koton.pdf")
PT_W, PT_H = 594.96, 841.92			# A4, in points

@functools.lru_cache (None)
def page_img (pdf, n, w):
	"""Page n of pdf, w px wide (at K x)."""
	out = subprocess.run (["pdftoppm", "-f", str (n), "-l", str (n), "-scale-to-x", str (int (w * K)), "-scale-to-y", "-1", "-png", pdf], capture_output = True, check = True).stdout
	import io
	return Image.open (io.BytesIO (out)).convert ("RGB")

@functools.lru_cache (None)
def page_words (pdf, n):
	"""[(line_index, x0, y0, x1, y1, word)] in points."""
	x = subprocess.run (["pdftotext", "-bbox-layout", "-f", str (n), "-l", str (n), pdf, "-"], capture_output = True, text = True, check = True).stdout
	out = []; li = -1
	for m in re.finditer (r'<line |<word xMin="([\d.]+)" yMin="([\d.]+)" xMax="([\d.]+)" yMax="([\d.]+)">([^<]*)</word>', x):
		if m.group (0).startswith ("<line"): li += 1; continue
		out.append ((li, float (m.group (1)), float (m.group (2)), float (m.group (3)), float (m.group (4)), html.unescape (m.group (5))))
	return out

def page_text (pdf, n):
	return subprocess.run (["pdftotext", "-f", str (n), "-l", str (n), pdf, "-"], capture_output = True, text = True, check = True).stdout

def screen (app = "PDF Viewer", menus = ("File", "Edit", "View", "Go", "Help")):
	c = M.Canvas ()
	c.img.paste (DESK.resize ((M.W * K, M.H * K), Image.LANCZOS), (0, 0)); c.d = ImageDraw.Draw (c.img, "RGBA")
	# the global menu bar: this app's menus
	c.rect (0, 0, 520, 27, (230, 222, 217))
	x = 18; c.text_l (x, 0, 27, "Onyx", "menu"); x += c.tw ("Onyx", "menu") + 18
	c.text_l (x, 0, 27, app, "menub"); x += c.tw (app, "menub") + 20
	for m in menus: c.text_l (x, 0, 27, m, "menu"); x += c.tw (m, "menu") + 18
	return c

# ---- the icons ----------------------------------------------------------------------------------------------
def ic_sidebar (c, x, y, s, col):
	c.rect (x + 1, y + 3, s - 2, s - 6, None, r = 2, outline = col, width = 1.6); c.rect (x + 1, y + 3, s * 0.36, s - 6, col, r = 2)
def ic_open (c, x, y, s, col): M.ic_folder (c, x, y + 1, s, (214, 170, 90))
def ic_print (c, x, y, s, col):
	c.rect (x + s * 0.25, y + 2, s * 0.5, s * 0.3, None, outline = col, width = 1.5)
	c.rect (x + 1, y + s * 0.32, s - 2, s * 0.4, col, r = 2)
	c.rect (x + s * 0.25, y + s * 0.6, s * 0.5, s * 0.34, WHITE, outline = col, width = 1.5)
def ic_up (c, x, y, s, col): c.line ([(x + s * 0.2, y + s * 0.65), (x + s / 2, y + s * 0.35), (x + s * 0.8, y + s * 0.65)], col, 2.2)
def ic_down (c, x, y, s, col): c.line ([(x + s * 0.2, y + s * 0.35), (x + s / 2, y + s * 0.65), (x + s * 0.8, y + s * 0.35)], col, 2.2)
def ic_minus (c, x, y, s, col): c.rect (x + 3, y + s / 2 - 1, s - 6, 2.2, col)
def ic_plus (c, x, y, s, col): c.rect (x + 3, y + s / 2 - 1, s - 6, 2.2, col); c.rect (x + s / 2 - 1, y + 3, 2.2, s - 6, col)
def ic_rotate (c, x, y, s, col):
	c.d.arc ([(x + 2) * K, (y + 3) * K, (x + s - 2) * K, (y + s - 1) * K], 200, 520 - 40, fill = col, width = int (1.8 * K))
	c.poly ([(x + 1, y + s * 0.3), (x + s * 0.36, y + s * 0.3), (x + s * 0.12, y + s * 0.62)], col)
def ic_search (c, x, y, s, col): c.ellipse (x + s * 0.42, y + s * 0.42, s * 0.3, None, col, 1.8); c.line ([(x + s * 0.64, y + s * 0.64), (x + s * 0.92, y + s * 0.92)], col, 2)
def ic_more (c, x, y, s, col):
	for k in range (3): c.ellipse (x + s / 2, y + s * (0.2 + k * 0.3), 1.8, col)
def ic_single (c, x, y, s, col): c.rect (x + s * 0.24, y + 1, s * 0.52, s - 2, None, r = 1, outline = col, width = 1.6)
def ic_scroll (c, x, y, s, col):
	c.rect (x + s * 0.24, y - 2, s * 0.52, s * 0.5, None, r = 1, outline = col, width = 1.6); c.rect (x + s * 0.24, y + s * 0.56, s * 0.52, s * 0.5, None, r = 1, outline = col, width = 1.6)
def ic_two (c, x, y, s, col):
	c.rect (x, y + 2, s * 0.46, s - 4, None, r = 1, outline = col, width = 1.6); c.rect (x + s * 0.54, y + 2, s * 0.46, s - 4, None, r = 1, outline = col, width = 1.6)
def ic_full (c, x, y, s, col):
	for (ax, ay, dx, dy) in ((x + 2, y + 2, 1, 1), (x + s - 2, y + 2, -1, 1), (x + 2, y + s - 2, 1, -1), (x + s - 2, y + s - 2, -1, -1)):
		c.line ([(ax, ay + dy * s * 0.3), (ax, ay), (ax + dx * s * 0.3, ay)], col, 1.8)
def ic_pdf (c, x, y, s, col = (200, 60, 50)):
	"""A document with its red PDF band."""
	c.poly ([(x + s * 0.18, y), (x + s * 0.64, y), (x + s * 0.84, y + s * 0.2), (x + s * 0.84, y + s), (x + s * 0.18, y + s)], WHITE)
	c.line ([(x + s * 0.18, y), (x + s * 0.64, y), (x + s * 0.84, y + s * 0.2), (x + s * 0.84, y + s), (x + s * 0.18, y + s), (x + s * 0.18, y)], (150, 140, 134), 1)
	c.rect (x + s * 0.06, y + s * 0.5, s * 0.66, s * 0.3, col, r = 2)
	if s >= 28: c.text_c (x + s * 0.06, y + s * 0.5, s * 0.66, s * 0.3, "PDF", "tiny", WHITE)
def ic_close (c, x, y, s, col): c.line ([(x + 2, y + 2), (x + s - 2, y + s - 2)], col, 1.6); c.line ([(x + s - 2, y + 2), (x + 2, y + s - 2)], col, 1.6)

# ---- the window's parts -------------------------------------------------------------------------------------
WX, WY, WW, WH = 14, 32, 996, 640
TABS_H, TB_H, SIDE_W = 34, 46, 224

def tabs (c, x, y, w, items, sel):
	c.rect (x, y, w, TABS_H, (198, 184, 176))
	tx = x + 8
	for k, name in enumerate (items):
		tw = min (210, c.tw (name, "uib") + 64)
		on = k == sel
		c.rect (tx, y + 5, tw, TABS_H - 5, FACE if on else (212, 200, 193), r = 7, corners = (True, True, False, False))
		ic_pdf (c, tx + 10, y + 12, 15)
		c.text_l (tx + 32, y + 5, TABS_H - 5, name, "uib" if on else "ui", TEXT if on else DIM)
		ic_close (c, tx + tw - 20, y + 15, 10, DIM)
		tx += tw + 4
	c.ellipse (tx + 14, y + 19, 11, None); ic_plus (c, tx + 6, y + 11, 16, DIM)

def tbtn (c, x, y, icon, col = TEXT, on = False, w = 32):
	if on: c.rect (x, y, w, 32, M.A (SEL, 50), r = 6, outline = M.A (SEL, 140))
	icon (c, x + (w - 18) / 2, y + 7, 18, col)
	return x + w + 2

def sep (c, x, y): c.vline (x + 4, y + 8, y + 34, M.shade (FACE, 0.82)); return x + 10

def toolbar (c, x, y, w, page = 9, pages = 55, zoom = "Fit width", view = "scroll", side = True, search = "", hot = None):
	c.rect (x, y, w, TB_H, FACE)
	by = y + 7
	bx = x + 8
	bx = tbtn (c, bx, by, ic_sidebar, on = side)
	bx = tbtn (c, bx, by, ic_open)
	bx = sep (c, bx, y)
	bx = tbtn (c, bx, by, ic_up); bx = tbtn (c, bx, by, ic_down)
	M.field (c, bx + 4, by + 2, 46, 28, str (page)); c.text_l (bx + 58, by + 2, 28, "/ %d" % pages, "ui", DIM); bx += 100
	bx = sep (c, bx, y)
	bx = tbtn (c, bx, by, ic_minus)
	M.dropdown (c, bx + 2, by + 1, 116, 30, zoom); bx += 122
	bx = tbtn (c, bx, by, ic_plus)
	bx = sep (c, bx, y)
	for icon, k in ((ic_single, "single"), (ic_scroll, "scroll"), (ic_two, "two")): bx = tbtn (c, bx, by, icon, on = view == k)
	bx = tbtn (c, bx, by, ic_rotate)
	bx = tbtn (c, bx, by, ic_full)
	# the right: the search, the menu
	sx = x + w - 8 - 32 - 6 - 230
	c.rect (sx, by + 2, 230, 28, WHITE, r = 14, outline = M.LINE)
	ic_search (c, sx + 10, by + 8, 16, DIM)
	c.text_l (sx + 34, by + 2, 28, search or "Find in the document", "ui", TEXT if search else FAINT)
	if search:
		before = sum (len (find_hits (LEDGER, n, search)) for n in range (1, page))
		c.text_r (sx + 222, by + 2, 28, "%d / 106" % (before + 2), "small", DIM)
	tbtn (c, x + w - 40, by, ic_more)
	c.hline (x, x + w, y + TB_H, M.shade (FACE, 0.84))

def app_window (c, title = "PDF Viewer"):
	cx, cy, cw, ch = M.window (c, WX, WY, WW, WH, title)
	return cx, cy, cw, ch

def side_tabs (c, x, y, sel):
	c.rect (x, y, SIDE_W, 44, SIDE)
	items = ["Pages", "Contents", "Find"]
	tw = (SIDE_W - 20) / 3
	c.rect (x + 10, y + 8, SIDE_W - 20, 28, WHITE, r = 6, outline = M.shade (FACE, 0.7))
	for k, s in enumerate (items):
		if k == sel: c.rect (x + 12 + k * tw, y + 10, tw - 4, 24, SEL, r = 5)
		c.text_c (x + 10 + k * tw, y + 8, tw, 28, s, "ui", WHITE if k == sel else TEXT)
	return y + 44

def sidebar_pages (c, x, y, h, pdf, first, cur):
	c.rect (x, y, SIDE_W, h, SIDE); c.vline (x + SIDE_W, y, y + h, M.shade (FACE, 0.82))
	yy = side_tabs (c, x, y, 0) + 6
	tw = 96; th = int (tw * PT_H / PT_W)
	n = first
	while yy + 40 < y + h:
		tx = x + (SIDE_W - tw) / 2
		if n == cur: c.rect (tx - 6, yy - 6, tw + 12, th + 12, M.A (SEL, 70), r = 6, outline = SEL, width = 2)
		im = page_img (pdf, n, tw)
		vis = min (th, y + h - yy)
		if vis <= 0: break
		c.rect (tx + 1, yy + 2, tw, vis, M.A ((0, 0, 0), 50))
		c.img.paste (im.crop ((0, 0, im.size[0], int (vis * K))), (int (tx * K), int (yy * K))); c.d = ImageDraw.Draw (c.img, "RGBA")
		if yy + th + 22 < y + h: c.text_c (x, yy + th + 4, SIDE_W, 16, str (n), "smallb" if n == cur else "small", SEL if n == cur else DIM)
		yy += th + 30; n += 1
	M.scrollbar (c, x + SIDE_W - 13, y + 52, h - 60, 0.12, 0.2)

def doc_view (c, x, y, w, h, pdf, pages, pw, scroll, hits = None, sel_lines = None, two = False, gap = 14):
	"""Continuous pages, pw px wide, centred; scroll = px from the top of pages[0]. hits: {page: [(word index, current)]}."""
	c.rect (x, y, w, h, CANVAS)
	ph = int (pw * PT_H / PT_W)
	sc = pw / PT_W
	cols = 2 if two else 1
	totw = cols * pw + (cols - 1) * gap
	px0 = x + (w - totw) / 2
	clip = (x, y, x + w, y + h)
	for k, n in enumerate (pages):
		col, row = (k % cols, k // cols)
		px = px0 + col * (pw + gap); py = y + 16 + row * (ph + gap) - scroll
		if py > y + h or py + ph < y: continue
		im = page_img (pdf, n, pw)
		# clip to the view
		t0 = max (0, y - py); t1 = min (ph, y + h - py)
		crop = im.crop ((0, int (t0 * K), im.size[0], int (t1 * K)))
		c.rect (px + 2, py + t0 + 3, pw, t1 - t0, M.A ((0, 0, 0), 60))
		c.img.paste (crop, (int (px * K), int ((py + t0) * K))); c.d = ImageDraw.Draw (c.img, "RGBA")
		words = page_words (pdf, n)
		def inside (wy0, wy1): return py + wy0 * sc >= y and py + wy1 * sc <= y + h
		if hits and n in hits:
			for wi, cur in hits[n]:
				_, a, b, cc, d, _ = words[wi]
				if inside (b, d): c.rect (px + a * sc - 1, py + b * sc - 1, (cc - a) * sc + 2, (d - b) * sc + 2, HIT_CUR if cur else HIT, r = 2)
		if sel_lines and n in sel_lines:
			lo, hi = sel_lines[n]
			for li in range (lo, hi + 1):
				ws = [wd for wd in words if wd[0] == li]
				if not ws: continue
				a = min (wd[1] for wd in ws); b = min (wd[2] for wd in ws); cc = max (wd[3] for wd in ws); d = max (wd[4] for wd in ws)
				if inside (b, d): c.rect (px + a * sc - 1, py + b * sc - 1, (cc - a) * sc + 2, (d - b) * sc + 3, (73, 146, 167, 80))
	# the scroll bar
	M.scrollbar (c, x + w - 13, y + 4, h - 8, 0.15, 0.2)
	return px0, ph, sc

def find_hits (pdf, n, term):
	return [i for i, wd in enumerate (page_words (pdf, n)) if term in wd[5].lower ()]

def bubble (c, x, y, s):
	w = c.tw (s, "smallb") + 20
	c.rect (x - w / 2, y, w, 24, M.A ((30, 28, 26), 200), r = 12); c.text_c (x - w / 2, y, w, 24, s, "smallb", WHITE)

# ---- 1. reading --------------------------------------------------------------------------------------------
def shot_read ():
	c = screen ()
	cx, cy, cw, ch = app_window (c, "Ledger — User Manual")
	tabs (c, cx, cy, cw, ["Ledger.pdf", "Koton.pdf"], 0)
	toolbar (c, cx, cy + TABS_H, cw, page = 8)
	by = cy + TABS_H + TB_H + 1; bh = ch - TABS_H - TB_H - 1
	sidebar_pages (c, cx, by, bh, LEDGER, 7, 8)
	vx = cx + SIDE_W + 1; vw = cw - SIDE_W - 1
	pw = vw - 72
	doc_view (c, vx, by, vw, bh, LEDGER, [8, 9], pw, 40)
	bubble (c, vx + vw / 2, by + bh - 40, "Page 8 of 55")
	c.save ("pdf-read.png")

# ---- 2. the contents, a selection ---------------------------------------------------------------------------
TOC = [("1. Introduction", 3, []), ("2. Getting started", 5, []), ("3. The window", 8, ["3.1 The pages", "3.2 Lists", "3.3 Forms", "3.4 Keyboard"]),
       ("4. Setting up your books", 11, []), ("5. Customers and suppliers", 15, []), ("6. Sales", 19, []), ("7. Purchases", 24, []),
       ("8. Paying your suppliers (SEPA)", 27, []), ("9. Bank and cash", 29, []), ("10. Miscellaneous operations", 33, []),
       ("11. Quotes, orders and delivery notes", 35, []), ("12. Printing from templates", 38, []), ("13. Reports", 41, []), ("14. VAT", 44, []),
       ("15. Closing the fiscal year", 47, [])]

def sidebar_contents (c, x, y, h):
	c.rect (x, y, SIDE_W, h, SIDE); c.vline (x + SIDE_W, y, y + h, M.shade (FACE, 0.82))
	yy = side_tabs (c, x, y, 1) + 4
	for title, pg, subs in TOC:
		if yy > y + h - 26: break
		open_ = bool (subs)
		ax, ay = x + 16, yy + 13
		if open_: c.poly ([(ax - 4, ay - 2), (ax + 4, ay - 2), (ax, ay + 3)], DIM)
		else: c.poly ([(ax - 2, ay - 4), (ax + 3, ay), (ax - 2, ay + 4)], FAINT)
		t = title
		while c.tw (t) > SIDE_W - 70: t = t[:-2].rstrip () + "…"
		c.text_l (x + 26, yy, 26, t, "ui"); c.text_r (x + SIDE_W - 14, yy, 26, str (pg), "small", DIM)
		yy += 26
		for s in subs:
			on = s == "3.2 Lists"
			if on: c.rect (x + 20, yy + 1, SIDE_W - 28, 24, SEL, r = 5)
			c.text_l (x + 38, yy, 26, s, "uib" if on else "ui", WHITE if on else TEXT)
			yy += 26

def shot_contents ():
	c = screen ()
	cx, cy, cw, ch = app_window (c, "Ledger — User Manual")
	tabs (c, cx, cy, cw, ["Ledger.pdf", "Koton.pdf"], 0)
	toolbar (c, cx, cy + TABS_H, cw, page = 9, zoom = "125 %")
	by = cy + TABS_H + TB_H + 1; bh = ch - TABS_H - TB_H - 1
	sidebar_contents (c, cx, by, bh)
	vx = cx + SIDE_W + 1; vw = cw - SIDE_W - 1
	pw = vw - 72
	# the paragraph under "3.2 Lists" selected
	words = page_words (LEDGER, 9)
	lines = sorted ({wd[0] for wd in words if wd[5] in ("Filters", "Search", "Double-click")})
	px0, ph, sc = doc_view (c, vx, by, vw, bh, LEDGER, [9], pw, 30, sel_lines = {9: (lines[0], lines[-1] + 1)})
	# the context menu, at the end of the selection
	ly = [wd for wd in words if wd[0] == lines[-1] + 1][-1]
	mx, my = px0 + ly[3] * sc - 140, by + 16 - 30 + ly[4] * sc + 6
	M.menu_popup (c, mx, my, 230, [("Copy", "Ctrl+C"), ("Select All", "Ctrl+A"), None, ("Find “Search”", "Ctrl+F"), ("Look Up in Jet Browser", None)], hot = "Copy")
	M.cursor (c, mx + 60, my + 12)
	c.save ("pdf-contents.png")

# ---- 3. find ------------------------------------------------------------------------------------------------
def snippet (pdf, n, term, k):
	"""The k-th line of page n holding the term, cut around it."""
	ls = [l.strip () for l in page_text (pdf, n).splitlines () if term in l.lower ()]
	if k >= len (ls): return None
	l = ls[k]; i = l.lower ().index (term)
	a = max (0, i - 14); s = ("…" if a else "") + l[a:a + 34] + "…"
	return s

def sidebar_find (c, x, y, h, term, cur_page):
	c.rect (x, y, SIDE_W, h, SIDE); c.vline (x + SIDE_W, y, y + h, M.shade (FACE, 0.82))
	yy = side_tabs (c, x, y, 2)
	M.field (c, x + 10, yy + 2, SIDE_W - 20, 28, term, caret = True)
	yy += 36
	M.checkbox (c, x + 12, yy + 2, "Match case", on = False); yy += 22
	M.checkbox (c, x + 12, yy + 2, "Whole words", on = False); yy += 26
	c.text (x + 12, yy, "106 results on 31 pages", "smallb", DIM); yy += 22
	for n in (19, 20, 21, 22, 23, 26):
		hits = len (find_hits (LEDGER, n, term))
		if not hits: continue
		if yy > y + h - 40: break
		c.text_l (x + 12, yy, 20, "Page %d" % n, "smallb", TEXT); c.text_r (x + SIDE_W - 14, yy, 20, str (hits), "small", DIM); yy += 20
		for k in range (2 if n != cur_page else 3):
			s = snippet (LEDGER, n, term, k)
			if not s or yy > y + h - 30: break
			on = n == cur_page and k == 1
			if on: c.rect (x + 6, yy, SIDE_W - 12, 22, SEL, r = 5)
			# the term in bold
			i = s.lower ().index (term); tx = x + 14
			for part, f in ((s[:i], "small"), (s[i:i + len (term)], "smallb"), (s[i + len (term):], "small")):
				while c.tw (part, f) > x + SIDE_W - 12 - tx and part: part = part[:-1]
				c.text_l (tx, yy, 22, part, f, WHITE if on else (TEXT if f == "smallb" else DIM)); tx += c.tw (part, f)
			yy += 22
		yy += 4

def shot_find ():
	c = screen ()
	cx, cy, cw, ch = app_window (c, "Ledger — User Manual")
	tabs (c, cx, cy, cw, ["Ledger.pdf", "Koton.pdf"], 0)
	toolbar (c, cx, cy + TABS_H, cw, page = 22, search = "invoice")
	by = cy + TABS_H + TB_H + 1; bh = ch - TABS_H - TB_H - 1
	sidebar_find (c, cx, by, bh, "invoice", 22)
	vx = cx + SIDE_W + 1; vw = cw - SIDE_W - 1
	hs = find_hits (LEDGER, 22, "invoice")
	doc_view (c, vx, by, vw, bh, LEDGER, [22], vw - 72, 40, hits = {22: [(i, k == 1) for k, i in enumerate (hs)]})
	c.save ("pdf-find.png")

# ---- 4. two pages, the zoom menu ------------------------------------------------------------------------------
def shot_zoom ():
	c = screen ()
	cx, cy, cw, ch = app_window (c, "Koton — User Manual")
	tabs (c, cx, cy, cw, ["Ledger.pdf", "Koton.pdf"], 1)
	toolbar (c, cx, cy + TABS_H, cw, page = 4, pages = int (subprocess.run (["pdfinfo", KOTON], capture_output = True, text = True).stdout.split ("Pages:")[1].split ()[0]),
		 zoom = "Fit page", view = "two", side = False)
	by = cy + TABS_H + TB_H + 1; bh = ch - TABS_H - TB_H - 1
	ph = bh - 32; pw = int (ph * PT_W / PT_H)
	doc_view (c, cx, by, cw, bh, KOTON, [4, 5], pw, 0, two = True, gap = 10)
	# the zoom's menu, open under its drop-down
	zx = cx + 8 + 34 * 2 + 10 + 34 * 2 + 100 + 10 + 34 + 2
	M.menu_popup (c, zx, cy + TABS_H + 40, 180,
		[("Fit page", "Ctrl+0"), ("Fit width", "Ctrl+9"), ("Actual size", "Ctrl+1"), None,
		 ("50 %", None), ("75 %", None), ("100 %", None), ("125 %", None), ("150 %", None), ("200 %", None), ("400 %", None), None,
		 ("Zoom in", "Ctrl++"), ("Zoom out", "Ctrl+-")], hot = "Fit width")
	c.line ([(zx + 100, cy + TABS_H + 58), (zx + 104, cy + TABS_H + 62), (zx + 112, cy + TABS_H + 52)], SEL, 2)
	M.cursor (c, zx + 60, cy + TABS_H + 40 + 5 + 26 + 14)
	c.save ("pdf-zoom.png")

# ---- 5. the home: no document open ----------------------------------------------------------------------------
RECENT = [(LEDGER, "Ledger.pdf", "SD:/manuals/ledger", "Page 9 of 55", "Today, 10:42", 9),
	  (KOTON, "Koton.pdf", "SD:/manuals/koton", "Page 4 of %s", "Today, 09:15", 1),
	  (os.path.join (M.ROOT, "sdcard", "manuals", "ledger", "Ledger.fr.pdf"), "Ledger.fr.pdf", "SD:/manuals/ledger", "Page 1 of 59", "Yesterday", 1),
	  (os.path.join (M.ROOT, "sdcard", "manuals", "koton", "Koton.fr.pdf"), "Koton.fr.pdf", "SD:/manuals/koton", "Page 12 of %s", "28 Sep", 12),
	  (os.path.join (M.ROOT, "sdcard", "manuals", "ledger", "Ledger.nl.pdf"), "Ledger.nl.pdf", "SD:/manuals/ledger", "Page 1 of 60", "27 Sep", 1)]

def shot_home ():
	c = screen ()
	cx, cy, cw, ch = app_window (c, "PDF Viewer")
	tabs (c, cx, cy, cw, ["Home"], 0)
	c.rect (cx, cy + TABS_H, cw, ch - TABS_H, LIST)
	ax, ay = cx + 40, cy + TABS_H + 26
	c.text (ax, ay, "Recent documents", "huge")
	M.button (c, cx + cw - 40 - 150, ay, 150, 34, "", accent = True); ic_open (c, cx + cw - 40 - 140, ay + 8, 18, WHITE)
	c.text_l (cx + cw - 40 - 114, ay, 34, "Open a file…", "uib", WHITE)
	S, G = 148, 30; th = int (S * PT_H / PT_W)
	for k, (pdf, name, folder, pos, when, pg) in enumerate (RECENT):
		x = ax + k * (S + G); y = ay + 58
		im = page_img (pdf, 1, S)
		c.rect (x + 2, y + 3, S, th, M.A ((0, 0, 0), 50), r = 4)
		c.img.paste (im, (int (x * K), int (y * K))); c.d = ImageDraw.Draw (c.img, "RGBA")
		c.rect (x, y, S, th, None, outline = M.LINE)
		if k == 1:			# under the pointer
			c.rect (x, y, S, th, M.A (SEL, 40)); c.rect (x - 1, y - 1, S + 2, th + 2, None, outline = SEL, width = 2)
			c.ellipse (x + S - 16, y + 16, 11, M.A (WHITE, 230)); ic_more (c, x + S - 25, y + 7, 18, TEXT)
			M.cursor (c, x + S / 2, y + th / 2)
		# how far it was read
		npg = int (subprocess.run (["pdfinfo", pdf], capture_output = True, text = True).stdout.split ("Pages:")[1].split ()[0])
		c.rect (x, y + th - 4, S * pg / npg, 4, SEL)
		c.text (x, y + th + 10, name, "uib"); c.text (x, y + th + 28, (pos % npg) if "%" in pos else pos, "small", DIM)
		c.text (x, y + th + 44, when, "small", FAINT)
	# where to find documents
	fy = ay + 58 + th + 92
	c.text (ax, fy, "Folders", "h2")
	for k, (name, sub) in enumerate ((("Manuals", "SD:/manuals  ·  5 documents"), ("Documents", "SD:/Documents  ·  12 documents"), ("USB drive", "USB:/  ·  3 documents"))):
		x = ax + k * 290; y = fy + 34
		c.rect (x, y, 270, 56, WHITE, r = 8, outline = M.LINE2)
		M.ic_folder (c, x + 14, y + 14, 28, (214, 170, 90))
		c.text (x + 56, y + 11, name, "uib"); c.text (x + 56, y + 31, sub, "small", DIM)
	c.text_c (cx, cy + ch - 40, cw, 20, "Or drop a PDF file here from the File Viewer.", "ui", FAINT)
	c.save ("pdf-home.png")

# ---- 6. the document's properties ------------------------------------------------------------------------------
def dialog (c, x, y, w, h, title):
	cx, cy, cw, ch = M.window (c, x, y, w, h, title)
	return cx, cy, cw, ch

def shot_props ():
	c = screen ()
	cx, cy, cw, ch = app_window (c, "Ledger — User Manual")
	tabs (c, cx, cy, cw, ["Ledger.pdf", "Koton.pdf"], 0)
	toolbar (c, cx, cy + TABS_H, cw, page = 8)
	by = cy + TABS_H + TB_H + 1; bh = ch - TABS_H - TB_H - 1
	sidebar_pages (c, cx, by, bh, LEDGER, 7, 8)
	vx = cx + SIDE_W + 1; vw = cw - SIDE_W - 1
	doc_view (c, vx, by, vw, bh, LEDGER, [8, 9], vw - 72, 40)
	c.rect (0, 0, M.W, M.H, M.A ((0, 0, 0), 40))
	dx, dy, dw, dh = 262, 150, 500, 470
	x, y, w, h = dialog (c, dx, dy, dw, dh, "Document Properties")
	M.segmented (c, x + 16, y + 14, 28, ["Description", "Fonts", "Security"], 0)
	rows = [("File", "Ledger.pdf"), ("Location", "SD:/manuals/ledger"), ("Size", "2.3 MB (2 423 823 bytes)"), None,
		("Title", "Ledger — User Manual"), ("Author", "—"), ("Subject", "—"), ("Keywords", "—"), None,
		("Created", "29 September 2026, 22:34"), ("Modified", "29 September 2026, 22:34"), ("Application", "HeadlessChrome 141"),
		("PDF producer", "Skia/PDF m141"), None, ("PDF version", "1.4 (tagged)"), ("Pages", "55  ·  A4, 210 × 297 mm"), ("Fast web view", "No")]
	ry = y + 58
	for r in rows:
		if r is None: c.hline (x + 16, x + w - 16, ry + 4, LINE2); ry += 10; continue
		c.text_l (x + 24, ry, 22, r[0], "ui", DIM); c.text_l (x + 160, ry, 22, r[1], "uib" if r[0] == "Title" else "ui"); ry += 22
	M.button (c, x + w - 106, y + h - 46, 90, 32, "Close", default = True)
	c.save ("pdf-props.png")

# ---- 7. Writer: Export as PDF ------------------------------------------------------------------------------------
def shot_export ():
	c = screen ("Writer", ("File", "Edit", "View", "Insert", "Format", "Table", "Help"))
	wr = Image.open (os.path.join (M.ROOT, "screenshots", "letters.png")).convert ("RGB")
	c.img.paste (wr.resize ((wr.size[0] * K, wr.size[1] * K), Image.LANCZOS), (int (8 * K), int (30 * K))); c.d = ImageDraw.Draw (c.img, "RGBA")
	c.rect (0, 0, M.W, M.H, M.A ((0, 0, 0), 40))
	dx, dy, dw, dh = 242, 100, 540, 556
	x, y, w, h = dialog (c, dx, dy, dw, dh, "Export as PDF")
	yy = y + 16
	c.text_l (x + 18, yy, 28, "Save as", "ui", DIM); M.field (c, x + 110, yy, w - 128 - 90, 28, "writer-tour.pdf"); M.button (c, x + w - 104, yy, 86, 28, "Browse…"); yy += 34
	c.text_l (x + 18, yy, 28, "In", "ui", DIM); M.dropdown (c, x + 110, yy, w - 128, 28, "SD:/Documents"); yy += 44
	M.group (c, x + 16, yy, w - 32, 104, "PAGES")
	M.radio (c, x + 32, yy + 30, "All", True, "(2 pages)"); M.radio (c, x + 32, yy + 54, "Current page"); M.radio (c, x + 32, yy + 78, "Pages")
	M.field (c, x + 110, yy + 74, 140, 24, "", "e.g. 1-2, 5", font = "small")
	yy += 116
	M.group (c, x + 16, yy, w - 32, 128, "CONTENT")
	M.checkbox (c, x + 32, yy + 30, "Embed the fonts (the document looks the same everywhere)", on = True)
	M.checkbox (c, x + 32, yy + 54, "Bookmarks from the headings", on = True)
	M.checkbox (c, x + 32, yy + 78, "Links clickable", on = True)
	c.text_l (x + 32, yy + 100, 20, "Images", "ui"); M.segmented (c, x + 110, yy + 98, 24, ["Full quality", "Smaller file"], 0)
	yy += 140
	M.group (c, x + 16, yy, w - 32, 82, "DOCUMENT")
	c.text_l (x + 32, yy + 26, 24, "Title", "ui", DIM); M.field (c, x + 110, yy + 24, w - 158, 24, "A tour of Writer", font = "small")
	c.text_l (x + 32, yy + 52, 24, "Author", "ui", DIM); M.field (c, x + 110, yy + 50, w - 158, 24, "Stéphane", font = "small")
	yy += 94
	M.checkbox (c, x + 18, yy + 8, "Open the PDF when it is saved", on = True)
	M.button (c, x + w - 220, y + h - 48, 96, 32, "Cancel"); M.button (c, x + w - 114, y + h - 48, 96, 32, "Export", accent = True)
	c.save ("pdf-export.png")

if __name__ == "__main__":
	shot_read (); shot_contents (); shot_find (); shot_zoom (); shot_home (); shot_props (); shot_export ()
