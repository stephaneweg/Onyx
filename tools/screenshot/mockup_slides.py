#!/usr/bin/env python3
"""mockup_slides.py -- the first mock-ups of Onyx's presentation program (in the way of PowerPoint /
LibreOffice Impress), the third of the office suite after Letters and Sheet. See docs/slides/README.md.

    python3 tools/screenshot/mockup_slides.py  -> docs/slides/mockups/slides-*.png

On the real desktop (screenshots/desktop.png, 1024 x 768); the drawing helpers are mockup_archiver.py's.
The toolbar icons that Letters and Sheet already have are taken from their real screenshots
(screenshots/letters.png, sheet.png), so the three apps look like one suite; the new ones (a slide, a
layout, a text box, the shapes, the show...) are drawn here in the same style. The deck shown is
"Onyx Cafe -- 2026, the year in review", made from Sheet's sample (sdcard/docs/cafe-2026.xlsx) and a
picture of sdcard/docs/pictures.
"""
import os, sys, math, functools
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, HERE)
import mockup_archiver as M

M.W, M.H = 1024, 768
M.OUT = os.path.join (M.ROOT, "docs", "slides", "mockups")
K = M.K
DESK = Image.open (os.path.join (M.ROOT, "screenshots", "desktop.png")).convert ("RGB")
WRITER = Image.open (os.path.join (M.ROOT, "screenshots", "letters.png")).convert ("RGB")
SHEET = Image.open (os.path.join (M.ROOT, "screenshots", "sheet.png")).convert ("RGB")
TEXT, DIM, FACE, SEL, WHITE, LIST, LINE, LINE2, FAINT = M.TEXT, M.DIM, M.FACE, M.SEL, (255, 255, 255), M.LIST, M.LINE, M.LINE2, M.FAINT
DESKGREY = (138, 134, 132)		# behind the slide: Letters' grey round its page
PANEL = (226, 216, 209)
GUIDE = (226, 60, 140)			# the smart guides
M.F["mid"] = M._f ("DejaVuSans.ttf", 15)
M.F["midb"] = M._f ("DejaVuSans-Bold.ttf", 15)
M.F["h2"] = M._f ("DejaVuSans-Bold.ttf", 20)
M.F["huge"] = M._f ("DejaVuSans-Bold.ttf", 30)
M.F["clock"] = M._f ("DejaVuSansMono-Bold.ttf", 30)
M.F["combo"] = M._f ("DejaVuSansMono-Bold.ttf", 13)
M.F["notes"] = M._f ("DejaVuSans.ttf", 12)
M.F["pnotes"] = M._f ("DejaVuSans.ttf", 17)

# ---- the deck: Onyx Cafe, 2026 the year in review (drawn at 1920 x 1080, then scaled) ------------------------
SW, SH = 1920, 1080
LIB = "/usr/share/fonts/truetype/liberation/"
@functools.lru_cache (None)
def sf (px, bold = False, italic = False, serif = False):
	n = ("LiberationSerif" if serif else "LiberationSans") + ("-BoldItalic" if bold and italic else "-Bold" if bold else "-Italic" if italic else "-Regular")
	return ImageFont.truetype (LIB + n + ".ttf", px)

TEAL, TEAL_D, PEACH, INK, MUTED, PALE = (46, 110, 128), (24, 64, 78), (240, 168, 110), (31, 42, 48), (112, 124, 132), (246, 242, 238)
GREYBLUE = (150, 170, 186)
COFFEE = [3120.5, 2980.25, 3410, 3562.75, 3890.4, 4210, 4480.6, 3950.2, 3720, 3641.5, 3380.9, 4120.3]
TEA = [1240.2, 1310, 1150.5, 980.75, 870, 760.3, 690.1, 720, 910.45, 1120, 1290.8, 1420.6]
PASTRY = [2050, 1920.4, 2180.7, 2240, 2360.2, 2410.9, 2520.5, 2130, 2290.6, 2310, 2240.35, 2870.25]
MONTHS = "Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec".split ()

def _grad (w, h, a, b, horizontal = False):
	g = Image.new ("RGB", (w, h)); gd = ImageDraw.Draw (g)
	n = w if horizontal else h
	for i in range (n):
		col = M.mix (a, b, i / max (1, n - 1))
		if horizontal: gd.line ([i, 0, i, h], fill = col)
		else: gd.line ([0, i, w, i], fill = col)
	return g

def _waves (im, y0, cols, amp = 46):
	d = ImageDraw.Draw (im, "RGBA")
	for k, col in enumerate (cols):
		pts = [(x, y0 + k * 70 + amp * math.sin (x / 260 + k * 1.7) + 24 * math.sin (x / 97 + k)) for x in range (0, SW + 20, 20)]
		d.polygon (pts + [(SW, SH), (0, SH)], fill = col)

def _frame (title, n, dark = False):
	"""A content slide of the theme: white, the title, a peach bar under it, the footer."""
	im = Image.new ("RGB", (SW, SH), WHITE); d = ImageDraw.Draw (im, "RGBA")
	d.rectangle ([0, 0, 22, SH], fill = TEAL)
	d.text ((110, 74), title, font = sf (70, True), fill = TEAL_D)
	d.rounded_rectangle ([112, 178, 252, 188], 5, fill = PEACH)
	d.line ([110, 1010, SW - 110, 1010], fill = (226, 222, 218), width = 2)
	d.text ((110, 1028), "Onyx Café  ·  2026, the year in review", font = sf (26), fill = MUTED)
	d.text ((SW - 110, 1028), str (n), font = sf (26, True), fill = TEAL, anchor = "ra")
	return im, d

def slide_title ():
	im = _grad (SW, SH, (20, 58, 72), (52, 124, 142))
	_waves (im, 790, [(240, 168, 110, 235), (90, 160, 176, 200), (16, 48, 60, 230)])
	d = ImageDraw.Draw (im, "RGBA")
	for (x, y, r) in ((1500, 220, 120), (1700, 380, 60), (1360, 420, 34)): d.ellipse ([x - r, y - r, x + r, y + r], fill = (255, 255, 255, 26))
	d.text ((150, 300), "Onyx Café", font = sf (170, True), fill = WHITE)
	d.text ((156, 500), "2026, the year in review", font = sf (66), fill = (255, 222, 192))
	d.rounded_rectangle ([156, 612, 300, 622], 5, fill = PEACH)
	d.text ((156, 650), "Board meeting  ·  14 January 2027", font = sf (36), fill = (220, 236, 240))
	return im

def slide_agenda ():
	im, d = _frame ("Agenda", 2)
	items = [("Takings in 2026", "84,455 € — the best year so far"), ("Best sellers", "what sold, what did not"),
		 ("The new terrace", "opened in June"), ("Plans for 2027", "four steps, one per quarter")]
	for k, (a, b) in enumerate (items):
		y = 280 + k * 170
		d.ellipse ([120, y, 220, y + 100], fill = TEAL if k != 2 else PEACH)
		d.text ((170, y + 50), str (k + 1), font = sf (54, True), fill = WHITE, anchor = "mm")
		d.text ((260, y + 4), a, font = sf (54, True), fill = INK)
		d.text ((262, y + 66), b, font = sf (34), fill = MUTED)
	# a decoration at the right
	d.rounded_rectangle ([1300, 260, 1800, 920], 40, fill = PALE)
	for k in range (5):
		d.rounded_rectangle ([1370, 330 + k * 112, 1730 - k * 50, 380 + k * 112], 25, fill = (TEAL if k % 2 == 0 else PEACH) + (200 - k * 25,))
	return im

def chart_area (d, x0, y0, x1, y1, scale = 1.0):
	vmax = 5000
	d.text ((x0, y0 - 10), "Takings by month (€)", font = sf (int (34 * scale), True), fill = INK)
	gy0, gy1 = y0 + 70 * scale, y1 - 90 * scale
	for v in range (0, vmax + 1, 1000):
		y = gy1 - (gy1 - gy0) * v / vmax
		d.line ([x0 + 90 * scale, y, x1, y], fill = (230, 228, 226), width = 2)
		d.text ((x0 + 76 * scale, y), "{:,}".format (v), font = sf (int (24 * scale)), fill = MUTED, anchor = "rm")
	n = 12; slot = (x1 - x0 - 110 * scale) / n
	for i in range (n):
		bx = x0 + 104 * scale + i * slot
		for k, (vals, col) in enumerate (((COFFEE, TEAL), (TEA, PEACH), (PASTRY, GREYBLUE))):
			bw = slot * 0.24
			h = (gy1 - gy0) * vals[i] / vmax
			d.rectangle ([bx + k * bw, gy1 - h, bx + (k + 1) * bw - 3, gy1], fill = col)
		d.text ((bx + slot * 0.36, gy1 + 12 * scale), MONTHS[i], font = sf (int (24 * scale)), fill = MUTED, anchor = "ma")
	ly = y1 - 6 * scale; lx = x0 + 110 * scale
	for name, col in (("Coffee", TEAL), ("Tea", PEACH), ("Pastries", GREYBLUE)):
		d.rectangle ([lx, ly - 22 * scale, lx + 22 * scale, ly], fill = col)
		d.text ((lx + 32 * scale, ly - 11 * scale), name, font = sf (int (26 * scale)), fill = INK, anchor = "lm")
		lx += (60 + len (name) * 15) * scale

CALLOUT = (1330, 270, 1810, 900)		# the callout box of slide 3 (slide px): selected in the main mock-up
def slide_chart ():
	im, d = _frame ("Takings by month", 3)
	chart_area (d, 110, 290, 1250, 960)
	x0, y0, x1, y1 = CALLOUT
	d.rounded_rectangle ([x0, y0, x1, y1], 36, fill = PALE)
	d.rounded_rectangle ([x0, y0, x0 + 16, y1], 8, fill = PEACH)
	d.text ((x0 + 60, y0 + 60), "84,455 €", font = sf (96, True), fill = TEAL_D)
	d.text ((x0 + 64, y0 + 176), "takings in 2026", font = sf (38), fill = MUTED)
	d.rounded_rectangle ([x0 + 60, y0 + 270, x0 + 330, y0 + 360], 45, fill = (220, 240, 226))
	d.text ((x0 + 195, y0 + 315), "▲ +31.2 %", font = sf (44, True), fill = (40, 130, 70), anchor = "mm")
	d.text ((x0 + 64, y0 + 380), "over 2025", font = sf (34), fill = MUTED)
	d.text ((x0 + 64, y0 + 470), "Best month", font = sf (32, True), fill = INK)
	d.text ((x0 + 64, y0 + 516), "December: 8,411 €", font = sf (34), fill = INK)
	return im

def slide_table ():
	im, d = _frame ("Best sellers", 4)
	rows = [("Product", "Units", "Takings", "vs 2025"), ("Flat white", "6,412", "22,442 €", "+18 %"), ("Croissant", "5,980", "11,960 €", "+9 %"),
		("Espresso", "5,104", "12,760 €", "+4 %"), ("Chai latte", "2,233", "8,932 €", "+41 %"), ("Carrot cake", "1,876", "7,504 €", "+12 %"), ("Iced tea", "1,140", "3,420 €", "−6 %")]
	cw = [620, 300, 340, 300]; x0, y0, rh = 110, 270, 100
	for r, row in enumerate (rows):
		y = y0 + r * rh
		if r == 0: d.rounded_rectangle ([x0, y, x0 + sum (cw), y + rh], 16, fill = TEAL, corners = (True, True, False, False))
		elif r % 2 == 0: d.rectangle ([x0, y, x0 + sum (cw), y + rh], fill = PALE)
		x = x0
		for c, cell in enumerate (row):
			col = WHITE if r == 0 else ((40, 130, 70) if cell.startswith ("+") else (190, 70, 60) if cell.startswith ("−") else INK)
			f = sf (40, r == 0 or c == 0 and r == 1)
			if c == 0: d.text ((x + 36, y + rh / 2), cell, font = f, fill = col, anchor = "lm")
			else: d.text ((x + cw[c] - 36, y + rh / 2), cell, font = f, fill = col, anchor = "rm")
			x += cw[c]
	d.line ([x0, y0 + 7 * rh, x0 + sum (cw), y0 + 7 * rh], fill = TEAL, width = 4)
	d.text ((1720, 300), "★", font = ImageFont.truetype ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 90), fill = PEACH, anchor = "ma")
	return im

PICT = os.path.join (M.ROOT, "sdcard", "docs", "pictures", "sunset-sea.jpg")
def slide_picture ():
	im = Image.new ("RGB", (SW, SH), WHITE); d = ImageDraw.Draw (im, "RGBA")
	p = Image.open (PICT).convert ("RGB")
	pw, ph = 1080, SH; r = max (pw / p.size[0], ph / p.size[1])
	p = p.resize ((int (p.size[0] * r) + 1, int (p.size[1] * r) + 1), Image.LANCZOS)
	p = p.crop (((p.size[0] - pw) // 2, (p.size[1] - ph) // 2, (p.size[0] - pw) // 2 + pw, (p.size[1] - ph) // 2 + ph))
	im.paste (p, (0, 0))
	d.rectangle ([1080, 0, 1096, SH], fill = PEACH)
	d.text ((1180, 180), "The new terrace", font = sf (68, True), fill = TEAL_D)
	d.rounded_rectangle ([1182, 284, 1322, 294], 5, fill = PEACH)
	for k, s in enumerate (["Opened on 12 June", "40 seats, facing the sea", "+22 % takings in summer", "Open until 23:00 in July and August"]):
		y = 360 + k * 110
		d.ellipse ([1186, y + 14, 1210, y + 38], fill = TEAL)
		d.text ((1240, y), s, font = sf (40), fill = INK)
	d.text ((1180, 1028), "5", font = sf (26, True), fill = TEAL)
	return im

def slide_timeline ():
	im, d = _frame ("Plans for 2027", 6)
	y = 560
	d.line ([180, y, 1740, y], fill = (210, 206, 202), width = 10)
	steps = [("Q1", "Loyalty card", "on the phone app"), ("Q2", "Brunch menu", "weekends, 9:00–14:00"), ("Q3", "A second shop", "near the station"), ("Q4", "Roast our own", "a small roaster, 5 kg")]
	for k, (q, a, b) in enumerate (steps):
		x = 290 + k * 450
		col = TEAL if k % 2 == 0 else PEACH
		d.ellipse ([x - 54, y - 54, x + 54, y + 54], fill = col)
		d.text ((x, y), q, font = sf (40, True), fill = WHITE, anchor = "mm")
		cy = 300 if k % 2 == 0 else 660
		d.rounded_rectangle ([x - 190, cy, x + 190, cy + 180], 26, fill = PALE)
		d.text ((x, cy + 52), a, font = sf (40, True), fill = INK, anchor = "mm")
		d.text ((x, cy + 116), b, font = sf (30), fill = MUTED, anchor = "mm")
	return im

def slide_quote ():
	im, d = _frame ("What our customers say", 7)
	d.text ((150, 230), "“", font = sf (400, True, serif = True), fill = PEACH + (160,))
	d.text ((300, 380), "The best flat white in town, and now", font = sf (66, False, True, True), fill = INK)
	d.text ((300, 470), "with a view of the sea.", font = sf (66, False, True, True), fill = INK)
	d.text ((300, 620), "— Marie L., a regular since 2021", font = sf (38), fill = MUTED)
	for k in range (5): d.text ((300 + k * 70, 720), "★", font = ImageFont.truetype ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 60), fill = PEACH)
	d.text ((680, 734), "4.8 / 5 on 1,204 reviews", font = sf (34), fill = MUTED)
	return im

def slide_thanks ():
	im = _grad (SW, SH, (20, 58, 72), (52, 124, 142))
	_waves (im, 830, [(240, 168, 110, 235), (16, 48, 60, 230)])
	d = ImageDraw.Draw (im, "RGBA")
	d.text ((SW / 2, 400), "Thank you", font = sf (160, True), fill = WHITE, anchor = "mm")
	d.text ((SW / 2, 560), "Questions?", font = sf (64), fill = (255, 222, 192), anchor = "mm")
	return im

DECK = [slide_title, slide_agenda, slide_chart, slide_table, slide_picture, slide_timeline, slide_quote, slide_thanks]
SECTIONS = {1: "Introduction", 3: "Results", 6: "Next year"}
TRANS = ["Fade", "Push", "Wipe", "Fade", "Cover", "Push", "Fade", "Zoom"]

@functools.lru_cache (None)
def slide (n):
	return DECK[n - 1] ()

def paste (c, im, x, y, w, h, filt = Image.LANCZOS):
	c.img.paste (im.resize ((int (w * K), int (h * K)), filt), (int (x * K), int (y * K))); c.d = ImageDraw.Draw (c.img, "RGBA")

def put_slide (c, n, x, y, w, shadow = True, im = None):
	h = w * 9 / 16
	if shadow: c.rect (x + 2, y + 3, w, h, M.A ((0, 0, 0), 60))
	paste (c, im or slide (n), x, y, w, h)
	return h

# ---- the screen, the window ---------------------------------------------------------------------------------
def screen (menus = ("File", "Edit", "View", "Insert", "Format", "Slide", "Show", "Help"), open_menu = None):
	c = M.Canvas ()
	c.img.paste (DESK.resize ((M.W * K, M.H * K), Image.LANCZOS), (0, 0)); c.d = ImageDraw.Draw (c.img, "RGBA")
	c.rect (0, 0, 640, 27, (230, 222, 217))
	x = 18; c.text_l (x, 0, 27, "Onyx", "menu"); x += c.tw ("Onyx", "menu") + 18
	c.text_l (x, 0, 27, "Slides", "menub"); x += c.tw ("Slides", "menub") + 20
	pos = {}
	for m in menus:
		if m == open_menu: c.rect (x - 7, 3, c.tw (m, "menu") + 14, 21, SEL, r = 4)
		c.text_l (x, 0, 27, m, "menu", WHITE if m == open_menu else TEXT); pos[m] = x - 7; x += c.tw (m, "menu") + 18
	return c, pos

WX, WY, WW, WH = 8, 32, 1008, 640

# icons taken from Letters and Sheet (screenshot px: x0, y0, x1, y1)
CROPS = dict (
	file = (WRITER, (8, 30, 336, 60)),		# new open save | undo redo | cut copy paste | find pilcrow
	table = (WRITER, (375, 30, 401, 60)), symbol = (WRITER, (419, 30, 440, 60)), image = (WRITER, (445, 30, 472, 60)),
	zoom = (WRITER, (485, 30, 628, 60)),
	bius = (WRITER, (404, 63, 578, 94)), colours = (WRITER, (591, 63, 681, 94)), align = (WRITER, (684, 63, 807, 94)),
	lists = (WRITER, (811, 63, 933, 94)),
	chart = (SHEET, (532, 30, 559, 60)), font = (SHEET, (9, 64, 182, 94)), valign = (SHEET, (572, 63, 663, 94)),
	borders = (SHEET, (904, 63, 952, 94)))

def crop (c, name, x, y):
	src, box = CROPS[name]
	im = src.crop (box)
	paste (c, im, x, y, im.size[0], im.size[1], Image.NEAREST)
	return x + im.size[0]

# ---- the new icons, 20 px, in the style of Letters' ---------------------------------------------------------
def ic_newslide (c, x, y, s = 20):
	c.rect (x + 1, y + 3, s - 4, s * 0.62, WHITE, r = 1, outline = (90, 90, 96))
	c.rect (x + 3, y + 5, s * 0.4, 2, TEAL); c.rect (x + 3, y + 9, s * 0.55, 1.5, FAINT); c.rect (x + 3, y + 12, s * 0.45, 1.5, FAINT)
	c.ellipse (x + s - 4, y + s - 4, 5, M.GREEN); c.rect (x + s - 7, y + s - 4.8, 6, 1.6, WHITE); c.rect (x + s - 4.8, y + s - 7, 1.6, 6, WHITE)
def ic_layout (c, x, y, s = 20):
	c.rect (x + 1, y + 3, s - 2, s - 6, WHITE, r = 1, outline = (90, 90, 96))
	c.rect (x + 3, y + 5, s - 6, 3, TEAL); c.rect (x + 3, y + 10, s / 2 - 4, s - 15, M.lighten (TEAL, 0.55)); c.rect (x + s / 2 + 1, y + 10, s / 2 - 4, s - 15, M.lighten (PEACH, 0.3))
def ic_dup (c, x, y, s = 20):
	c.rect (x + 5, y + 2, s - 7, s * 0.55, WHITE, r = 1, outline = (90, 90, 96)); c.rect (x + 1, y + 7, s - 7, s * 0.55, WHITE, r = 1, outline = (90, 90, 96))
	c.rect (x + 3, y + 9, s * 0.35, 2, TEAL)
def ic_delslide (c, x, y, s = 20):
	c.rect (x + 1, y + 3, s - 4, s * 0.62, WHITE, r = 1, outline = (90, 90, 96))
	c.ellipse (x + s - 4, y + s - 4, 5, M.RED); c.rect (x + s - 7, y + s - 4.8, 6, 1.6, WHITE)
def ic_textbox (c, x, y, s = 20):
	c.rect (x + 1, y + 2, s - 2, s - 4, WHITE, outline = (90, 90, 96))
	for (px, py) in ((x + 1, y + 2), (x + s - 1, y + 2), (x + 1, y + s - 2), (x + s - 1, y + s - 2)): c.rect (px - 1.5, py - 1.5, 3, 3, SEL)
	c.text_c (x, y + 1, s, s - 2, "A", "uib", TEXT)
def ic_shapes (c, x, y, s = 20):
	c.rect (x + 1, y + 8, 10, 10, (110, 160, 210), outline = (60, 100, 150))
	c.ellipse (x + 13, y + 8, 6, (240, 168, 110), (180, 110, 60))
	c.poly ([(x + 9, y + 1), (x + 15, y + 11), (x + 3, y + 11)], M.A ((90, 170, 110), 230))
def ic_connector (c, x, y, s = 20):
	c.line ([(x + 3, y + s - 4), (x + s - 5, y + 5)], (70, 70, 76), 1.8)
	c.poly ([(x + s - 2, y + 2), (x + s - 9, y + 4), (x + s - 4, y + 9)], (70, 70, 76))
	c.ellipse (x + 3, y + s - 4, 2.4, SEL)
def ic_media (c, x, y, s = 20):
	c.rect (x + 1, y + 3, s - 2, s - 6, (60, 64, 70), r = 2)
	c.poly ([(x + 8, y + 7), (x + 14, y + 10), (x + 8, y + 13)], WHITE)
	for k in range (4): c.rect (x + 2 + k * 5, y + 4, 2, 1.5, (180, 180, 180)); c.rect (x + 2 + k * 5, y + s - 5.5, 2, 1.5, (180, 180, 180))
def ic_play (c, x, y, s = 20, col = WHITE):
	c.poly ([(x + 4, y + 2), (x + s - 2, y + s / 2), (x + 4, y + s - 2)], col)
def ic_forward (c, x, y, s = 20):
	c.rect (x + 1, y + 1, 12, 12, M.lighten (TEAL, 0.5), outline = (90, 90, 96)); c.rect (x + 6, y + 6, 12, 12, (110, 160, 210), outline = (60, 100, 150))
def ic_backward (c, x, y, s = 20):
	c.rect (x + 6, y + 6, 12, 12, (110, 160, 210), outline = (60, 100, 150)); c.rect (x + 1, y + 1, 12, 12, M.lighten (TEAL, 0.5), outline = (90, 90, 96))
def ic_alignobj (c, x, y, s = 20):
	c.rect (x + 2, y + 1, 1.6, s - 2, (70, 70, 76)); c.rect (x + 5, y + 4, 12, 5, (110, 160, 210)); c.rect (x + 5, y + 11, 8, 5, M.lighten (PEACH, 0.1))
def ic_group (c, x, y, s = 20):
	c.dashed (x + 1, y + 1, s - 2, s - 2, (90, 90, 96), dash = 3, r = 0)
	c.rect (x + 4, y + 4, 7, 7, (110, 160, 210)); c.ellipse (x + 13, y + 13, 3.5, PEACH)
def ic_spacing (c, x, y, s = 20):
	for k in range (3): c.rect (x + 8, y + 3 + k * 6, 11, 1.8, (70, 70, 76))
	c.rect (x + 3, y + 3, 1.6, s - 6, SEL); c.poly ([(x + 0.5, y + 6), (x + 7, y + 6), (x + 3.8, y + 2)], SEL); c.poly ([(x + 0.5, y + s - 6), (x + 7, y + s - 6), (x + 3.8, y + s - 2)], SEL)
def ic_notes (c, x, y, s = 16, col = TEXT):
	c.rect (x + 1, y + 1, s - 2, s - 2, None, r = 2, outline = col, width = 1.3)
	for k in range (3): c.rect (x + 4, y + 5 + k * 3, s - 8, 1.2, col)
def ic_normal (c, x, y, s = 16, col = TEXT):
	c.rect (x, y + 2, s, s - 4, None, outline = col, width = 1.3); c.rect (x, y + 2, 4, s - 4, col)
def ic_sorter (c, x, y, s = 16, col = TEXT):
	for i in range (2):
		for j in range (2): c.rect (x + i * 9, y + 2 + j * 7, 7, 5, col)
def ic_reading (c, x, y, s = 16, col = TEXT):
	c.rect (x, y + 2, s, s - 4, None, outline = col, width = 1.3); c.poly ([(x + 6, y + 5), (x + 11, y + 8), (x + 6, y + 11)], col)

def tb_sep (c, x, y, h = 26): c.vline (x + 4, y + 4, y + h - 2, M.shade (FACE, 0.82)); return x + 10

def tb_btn (c, x, y, icon, on = False, arrow = False, w = 29):
	if on: c.rect (x, y + 1, w + (10 if arrow else 0), 26, (178, 196, 202), r = 3, outline = (140, 160, 166))
	icon (c, x + (w - 20) / 2, y + 4)
	if arrow: c.poly ([(x + w, y + 13), (x + w + 7, y + 13), (x + w + 3.5, y + 17)], TEXT); w += 10
	return x + w

def combo (c, x, y, w, s):
	"""Letters' combo: white field, the bitmap-like font, a small button at the right."""
	c.rect (x, y, w, 25, (247, 245, 244), r = 3, outline = (150, 140, 134))
	c.text_l (x + 6, y, 25, s, "combo", TEXT)
	bx = x + w - 19
	c.grad (bx, y + 3, 16, 19, (236, 232, 229), (196, 190, 186), r = 3); c.rect (bx, y + 3, 16, 19, r = 3, outline = (130, 124, 120))
	c.line ([(bx + 4, y + 11), (bx + 8, y + 15), (bx + 12, y + 11)], TEXT, 1.6)

def toolbars (c, x, y, w, hot = None, font = "Liberation Sans", size = "40"):
	"""Two rows, as Letters' and Sheet's: the file and the slide's objects; then the text."""
	c.rect (x, y, w, 70, FACE)
	r1 = y + 2
	bx = crop (c, "file", x + 2, r1)
	bx = tb_sep (c, bx, r1 + 1)
	bx = tb_btn (c, bx, r1, ic_newslide, arrow = True, on = hot == "new")
	bx = tb_btn (c, bx, r1, ic_layout, arrow = True, on = hot == "layout")
	bx = tb_btn (c, bx, r1, ic_dup); bx = tb_btn (c, bx, r1, ic_delslide)
	bx = tb_sep (c, bx, r1 + 1)
	bx = tb_btn (c, bx, r1, ic_textbox, on = hot == "text")
	bx = crop (c, "image", bx, r1); bx = crop (c, "table", bx + 2, r1); bx = crop (c, "chart", bx + 2, r1)
	bx = tb_btn (c, bx, r1, ic_shapes, arrow = True, on = hot == "shapes")
	bx = tb_btn (c, bx, r1, ic_connector); bx = tb_btn (c, bx, r1, ic_media); bx = crop (c, "symbol", bx, r1)
	bx = tb_sep (c, bx, r1 + 1)
	bx = crop (c, "zoom", bx, r1)
	# the show, at the right
	sx = x + w - 120
	c.grad (sx, r1 + 2, 112, 25, M.lighten (SEL, 0.15), M.shade (SEL, 0.9), r = 5); c.rect (sx, r1 + 2, 112, 25, r = 5, outline = M.shade (SEL, 0.7))
	ic_play (c, sx + 6, r1 + 6, 17); c.text_l (sx + 28, r1 + 2, 25, "Start show", "smallb", WHITE)
	c.vline (sx + 96, r1 + 6, r1 + 23, M.A (WHITE, 120)); c.line ([(sx + 100, r1 + 12), (sx + 104, r1 + 16), (sx + 108, r1 + 12)], WHITE, 1.5)
	# row 2: the text
	r2 = y + 35
	bx = crop (c, "font", x + 2, r2 - 1)
	combo (c, bx + 4, r2 + 1, 54, size); bx += 64
	bx = tb_sep (c, bx, r2)
	bx = crop (c, "bius", bx, r2 - 1)
	bx = tb_sep (c, bx, r2)
	bx = crop (c, "colours", bx, r2 - 1)
	bx = tb_sep (c, bx, r2)
	bx = crop (c, "align", bx, r2 - 1)
	bx = crop (c, "valign", bx + 2, r2 - 1)
	bx = tb_sep (c, bx, r2)
	bx = crop (c, "lists", bx, r2 - 1)
	bx = tb_btn (c, bx, r2 - 1, ic_spacing)
	bx = tb_sep (c, bx, r2)
	bx = tb_btn (c, bx, r2 - 1, ic_forward); bx = tb_btn (c, bx, r2 - 1, ic_backward)
	c.hline (x, x + w, y + 70, M.shade (FACE, 0.84))
	return y + 71

def status (c, x, y, w, left, view = "normal", zoom = "72%"):
	c.rect (x, y, w, 22, FACE); c.hline (x, x + w, y, M.shade (FACE, 0.84))
	tx = x + 10
	for k, s in enumerate (left):
		c.text_l (tx, y, 22, s, "combo", TEXT); tx += c.tw (s, "combo") + 26
	rx = x + w - 10
	c.text_r (rx, y, 22, "+", "uib"); rx -= 22
	c.text_r (rx, y, 22, zoom, "combo"); rx -= c.tw (zoom, "combo") + 10
	c.text_r (rx, y, 22, "−", "uib"); rx -= 24
	for k, ic in (("reading", ic_reading), ("sorter", ic_sorter), ("normal", ic_normal)):
		if view == k: c.rect (rx - 22, y + 2, 22, 18, (178, 196, 202), r = 3)
		ic (c, rx - 19, y + 3, 16); rx -= 26
	c.vline (rx - 4, y + 4, y + 18, M.shade (FACE, 0.8)); rx -= 12
	if view == "normal":
		c.rect (rx - 22, y + 2, 22, 18, (178, 196, 202), r = 3); ic_notes (c, rx - 19, y + 3, 16); c.text_r (rx - 26, y, 22, "Notes", "combo")

# ---- the panes ----------------------------------------------------------------------------------------------
def thumbs (c, x, y, w, h, cur = 3, sel = (3,), first = 1, hidden = ()):
	c.rect (x, y, w, h, PANEL); c.vline (x + w, y, y + h, M.shade (FACE, 0.8))
	tw = w - 52; th = tw * 9 / 16
	yy = y + 8; n = first
	while n <= len (DECK) and yy < y + h:
		if n in SECTIONS:
			c.line ([(x + 9, yy + 6), (x + 13, yy + 10), (x + 17, yy + 6)], DIM, 1.4)
			c.text_l (x + 22, yy, 16, SECTIONS[n], "smallb", DIM); yy += 20
		if n in sel: c.rect (x + 26, yy - 4, tw + 8, th + 8, M.A (SEL, 60) if n != cur else SEL, r = 4)
		c.text_r (x + 20, yy, 16, str (n), "smallb" if n == cur else "small", SEL if n == cur else DIM)
		vis = min (th, y + h - yy)
		if vis > 2:
			im = slide (n)
			sm = im.resize ((int (tw * K), int (th * K)), Image.LANCZOS).crop ((0, 0, int (tw * K), int (vis * K)))
			c.img.paste (sm, (int ((x + 30) * K), int (yy * K))); c.d = ImageDraw.Draw (c.img, "RGBA")
			c.rect (x + 30, yy, tw, vis, outline = M.shade (FACE, 0.6))
			if n in hidden: c.rect (x + 30, yy, tw, vis, M.A (PANEL, 150))
		yy += th + 14; n += 1
	M.scrollbar (c, x + w - 12, y + 6, h - 12, 0.0, 0.62)

def handles (c, x, y, w, h, rot = True):
	c.rect (x, y, w, h, outline = SEL, width = 1.2)
	for hx in (x, x + w / 2, x + w):
		for hy in (y, y + h / 2, y + h):
			if hx == x + w / 2 and hy == y + h / 2: continue
			c.rect (hx - 3.5, hy - 3.5, 7, 7, WHITE, outline = SEL, width = 1.2)
	if rot:
		c.vline (x + w / 2, y - 16, y, SEL); c.ellipse (x + w / 2, y - 19, 4.5, (120, 200, 120), M.shade ((120, 200, 120), 0.6))

def section (c, x, y, w, title, open_ = True):
	c.rect (x, y, w, 22, M.lighten (FACE, 0.35))
	if open_: c.line ([(x + 8, y + 9), (x + 12, y + 13), (x + 16, y + 9)], DIM, 1.5)
	else: c.line ([(x + 10, y + 7), (x + 14, y + 11), (x + 10, y + 15)], DIM, 1.5)
	c.text_l (x + 22, y, 22, title, "smallb", TEXT)
	return y + 26

def swatch (c, x, y, col, w = 34, label = None):
	c.rect (x, y, w, 22, WHITE, r = 3, outline = LINE); c.rect (x + 3, y + 3, w - 16, 16, col, r = 2)
	c.line ([(x + w - 11, y + 9), (x + w - 7, y + 13), (x + w - 3, y + 9)], TEXT, 1.4)

def spin (c, x, y, w, s):
	M.field (c, x, y, w, 22, s, font = "small")
	c.line ([(x + w - 12, y + 9), (x + w - 8, y + 5), (x + w - 4, y + 9)], DIM, 1.2); c.line ([(x + w - 12, y + 13), (x + w - 8, y + 17), (x + w - 4, y + 13)], DIM, 1.2)

def sidebar_tabs (c, x, y, w, sel):
	c.rect (x, y, w, h_ (c), PANEL)
	items = ["Slide", "Shape", "Text", "Animate"]
	tw = (w - 16) / len (items)
	c.rect (x + 8, y + 8, w - 16, 24, WHITE, r = 5, outline = M.shade (FACE, 0.7))
	for k, s in enumerate (items):
		if s == sel: c.rect (x + 10 + k * tw, y + 10, tw - 4, 20, SEL, r = 4)
		c.text_c (x + 8 + k * tw, y + 8, tw, 24, s, "small", WHITE if s == sel else TEXT)
	return y + 40
def h_ (c): return 0

def sidebar_shape (c, x, y, w, h):
	c.rect (x, y, w, h, PANEL); c.vline (x, y, y + h, M.shade (FACE, 0.8))
	yy = sidebar_tabs (c, x, y, w, "Shape")
	yy = section (c, x + 1, yy, w - 1, "Fill")
	M.dropdown (c, x + 10, yy, 110, 22, "Solid"); swatch (c, x + 128, yy, PALE, 44)
	c.text_l (x + 10, yy + 26, 20, "Transparency", "small", DIM); spin (c, x + w - 66, yy + 26, 56, "0 %"); yy += 54
	yy = section (c, x + 1, yy, w - 1, "Line")
	M.dropdown (c, x + 10, yy, 110, 22, "None"); swatch (c, x + 128, yy, (200, 200, 200), 44); yy += 30
	yy = section (c, x + 1, yy, w - 1, "Shape")
	c.text_l (x + 10, yy, 22, "Corners", "small", DIM); spin (c, x + w - 66, yy, 56, "18 px"); yy += 26
	M.checkbox (c, x + 10, yy + 3, "Shadow", False); yy += 26
	c.text_l (x + 10, yy, 22, "Accent bar", "small", DIM); swatch (c, x + w - 54, yy, PEACH, 44); yy += 30
	yy = section (c, x + 1, yy, w - 1, "Position and size")
	for k, (a, v, b, u) in enumerate ((("X", "13.3 cm", "Y", "2.7 cm"), ("W", "4.8 cm", "H", "6.3 cm"))):
		c.text_l (x + 10, yy, 22, a, "small", DIM); spin (c, x + 26, yy, 76, v)
		c.text_l (x + 110, yy, 22, b, "small", DIM); spin (c, x + 126, yy, 76, u); yy += 26
	c.text_l (x + 10, yy, 22, "Rotation", "small", DIM); spin (c, x + 76, yy, 62, "0°")
	M.checkbox (c, x + 148, yy + 3, "", True); c.text_l (x + 166, yy, 22, "Ratio", "small", DIM); yy += 30
	yy = section (c, x + 1, yy, w - 1, "Arrange")
	bx = x + 10
	for ic in (ic_forward, ic_backward, ic_alignobj, ic_group):
		c.rect (bx, yy, 30, 26, M.lighten (FACE, 0.5), r = 4, outline = LINE); ic (c, bx + 5, yy + 3); bx += 36
	yy += 34
	yy = section (c, x + 1, yy, w - 1, "Alt text", False)

def notes_pane (c, x, y, w, h, lines):
	c.rect (x, y, w, h, WHITE); c.hline (x, x + w, y, M.shade (FACE, 0.75))
	c.rect (x + w / 2 - 16, y + 2, 32, 3, M.shade (FACE, 0.8), r = 1)
	for k, s in enumerate (lines): c.text (x + 14, y + 10 + k * 17, s, "notes", TEXT if k else DIM)

def normal_view (c, cur = 3, sidebar = "shape", hot = None, sel_box = True, notes = None, extra = None):
	cx, cy, cw, ch = M.window (c, WX, WY, WW, WH, "Slides — cafe-2026.odp")
	y = toolbars (c, cx, cy, cw, hot = hot)
	TH_W, SB_W, NOTES_H = 160, 228, 64
	body_h = cy + ch - 22 - y
	thumbs (c, cx, y, TH_W, body_h, cur = cur, sel = (cur,))
	ex = cx + TH_W + 1; ew = cw - TH_W - 1 - SB_W
	eh = body_h - NOTES_H
	c.rect (ex, y, ew, eh, DESKGREY)
	sw = ew - 40; sh = sw * 9 / 16
	if sh > eh - 30: sh = eh - 30; sw = sh * 16 / 9
	sx = ex + (ew - sw) / 2; sy = y + (eh - sh) / 2
	put_slide (c, cur, sx, sy, sw)
	sc = sw / SW
	geo = (sx, sy, sc)
	if sel_box:
		x0, y0, x1, y1 = CALLOUT
		handles (c, sx + x0 * sc, sy + y0 * sc, (x1 - x0) * sc, (y1 - y0) * sc)
	if extra: extra (c, geo)
	M.scrollbar (c, ex + ew - 12, y + 4, eh - 8, 0.3, 0.55)
	notes_pane (c, ex, y + eh, ew, NOTES_H, notes or ["Notes", "Say first: the best year since we opened (2019). Thank the team.",
							       "Point to December: the Christmas menu. Then the summer: the terrace (next slide)."])
	if sidebar == "shape": sidebar_shape (c, ex + ew, y, SB_W, body_h)
	elif callable (sidebar): sidebar (c, ex + ew, y, SB_W, body_h)
	status (c, cx, cy + ch - 22, cw, ["Slide %d of %d" % (cur, len (DECK)), SECTIONS[max (k for k in SECTIONS if k <= cur)], "Theme: Café"])
	return geo, (ex, y, ew, eh)

# ---- 1. the main window -------------------------------------------------------------------------------------
def mock_main ():
	c, _ = screen ()
	normal_view (c)
	c.save ("slides-main.png")

# ---- 2. the slide sorter -------------------------------------------------------------------------------------
def mock_sorter ():
	c, _ = screen ()
	cx, cy, cw, ch = M.window (c, WX, WY, WW, WH, "Slides — cafe-2026.odp")
	y = toolbars (c, cx, cy, cw)
	h = cy + ch - 22 - y
	c.rect (cx, y, cw, h, (236, 230, 226))
	cols = 5; gap = 22; tw = (cw - 60 - (cols - 1) * gap) / cols; th = tw * 9 / 16
	yy = y + 12; col = 0; x0 = cx + 30
	for n in range (1, len (DECK) + 1):
		if n in SECTIONS:
			if col: yy += th + 46; col = 0
			c.line ([(x0 - 14, yy + 7), (x0 - 10, yy + 11), (x0 - 6, yy + 7)], DIM, 1.5)
			c.text_l (x0, yy, 18, SECTIONS[n], "uib", TEXT)
			cnt = sum (1 for k in range (n, len (DECK) + 1) if k == n or k not in SECTIONS and all (s not in SECTIONS for s in range (n + 1, k + 1)))
			c.text_l (x0 + c.tw (SECTIONS[n], "uib") + 10, yy, 18, "%d slides" % cnt, "small", DIM)
			c.hline (x0 + c.tw (SECTIONS[n], "uib") + 80, cx + cw - 30, yy + 9, LINE)
			yy += 26
		tx = x0 + col * (tw + gap)
		selected = n in (4, 5)
		if selected: c.rect (tx - 5, yy - 5, tw + 10, th + 10, SEL if n == 5 else M.A (SEL, 110), r = 5)
		put_slide (c, n, tx, yy, tw, shadow = not selected)
		# under it: the number, the transition, the timing
		c.text_l (tx, yy + th + 4, 18, str (n), "smallb", SEL if selected else TEXT)
		c.text_l (tx + 22, yy + th + 4, 18, "☆ " + TRANS[n - 1], "small", DIM)
		if n in (3, 5): c.text_r (tx + tw, yy + th + 4, 18, "●  2 effects" if n == 3 else "◷ 0:20", "small", DIM)
		if n == 7:
			c.rect (tx, yy, tw, th, M.A ((236, 230, 226), 150))
			c.rect (tx + tw - 70, yy + 6, 64, 18, M.A ((40, 40, 40), 180), r = 9); c.text_c (tx + tw - 70, yy + 6, 64, 18, "hidden", "smallb", WHITE)
		col += 1
		if col == cols: col = 0; yy += th + 32
	status (c, cx, cy + ch - 22, cw, ["2 slides selected", "8 slides, 3 sections", "Show: 6 min 40"], view = "sorter", zoom = "")
	c.save ("slides-sorter.png")

# ---- 3. the layouts and the themes --------------------------------------------------------------------------
LAYOUTS = ["Title", "Title and content", "Two contents", "Comparison", "Section header", "Title only", "Picture and text", "Blank"]
def layout_icon (c, x, y, w, h, k, accent = TEAL):
	c.rect (x, y, w, h, WHITE, outline = LINE)
	bar = M.lighten (accent, 0.2); g = (210, 206, 202)
	def lines (lx, ly, lw, n):
		for i in range (n): c.rect (lx, ly + i * 7, lw * (0.9 if i % 2 else 1), 3, g, r = 1)
	if k == 0:
		c.rect (x + w * 0.15, y + h * 0.36, w * 0.7, 7, bar, r = 2); c.rect (x + w * 0.28, y + h * 0.56, w * 0.44, 4, g, r = 1)
	elif k == 4:
		c.rect (x, y, w, h, M.lighten (accent, 0.75)); c.rect (x + w * 0.12, y + h * 0.48, w * 0.6, 7, bar, r = 2)
	elif k == 7: pass
	else:
		c.rect (x + 6, y + 6, w * 0.55, 6, bar, r = 2)
		if k == 1: lines (x + 8, y + 20, w - 16, 4)
		elif k == 2: lines (x + 8, y + 20, w / 2 - 12, 4); lines (x + w / 2 + 4, y + 20, w / 2 - 12, 4)
		elif k == 3:
			for j in (0, 1):
				lx = x + 8 + j * (w / 2 - 2); c.rect (lx, y + 19, w / 2 - 12, 4, M.lighten (PEACH, 0.2), r = 1); lines (lx, y + 27, w / 2 - 12, 3)
		elif k == 6:
			c.rect (x + 1, y + 1, w * 0.48, h - 2, (150, 186, 200)); c.poly ([(x + 1, y + h - 1), (x + w * 0.2, y + h * 0.55), (x + w * 0.48, y + h - 1)], (90, 130, 150))
			c.rect (x + w * 0.55, y + 8, w * 0.38, 5, bar, r = 2); lines (x + w * 0.55, y + 20, w * 0.38, 3)

def mock_layouts ():
	c, _ = screen ()
	geo, (ex, ey, ew, eh) = normal_view (c, cur = 6, sidebar = sidebar_slide, hot = "layout", sel_box = False,
					     notes = ["Notes", "Four steps, one per quarter. The second shop depends on the bank (decision in March)."])
	# the Layout drop-down, under its button (its position: after file, new slide)
	px, py = WX + 4 + 2 + 328 + 10 + 39 - 4, WY + 28 + 30
	cols, iw, ih = 4, 104, 59
	pw = cols * (iw + 12) + 14; ph = 2 * (ih + 34) + 76
	c.rect (px + 3, py + 4, pw, ph, M.A ((0, 0, 0), 50), r = 6)
	c.rect (px, py, pw, ph, M.MENU, r = 6, outline = M.shade (FACE, 0.62))
	c.text_l (px + 12, py + 4, 24, "Layout of slide 6", "smallb", DIM)
	for k, name in enumerate (LAYOUTS):
		ix = px + 12 + (k % cols) * (iw + 12); iy = py + 30 + (k // cols) * (ih + 34)
		if k == 1: c.rect (ix - 5, iy - 5, iw + 10, ih + 30, M.A (SEL, 60), r = 5, outline = SEL)
		layout_icon (c, ix, iy, iw, ih, k)
		c.text_c (ix - 6, iy + ih + 2, iw + 12, 18, name, "small", TEXT)
	ly = py + ph - 40
	c.hline (px + 8, px + pw - 8, ly, M.LINE2)
	c.text_l (px + 14, ly + 4, 16, "Reset the slide to its layout", "small"); c.text_l (px + 14, ly + 20, 16, "Edit the layouts in the master…", "small")
	c.save ("slides-layouts.png")

THEMES = [("Café", TEAL, PEACH), ("Peach", (176, 110, 60), (240, 176, 122)), ("Steel", (60, 84, 120), (122, 152, 192)), ("Sage", (70, 110, 64), (128, 170, 118)),
	  ("Brick", (150, 56, 52), (196, 84, 80)), ("Slate", (40, 46, 60), (120, 130, 150))]
def sidebar_slide (c, x, y, w, h):
	c.rect (x, y, w, h, PANEL); c.vline (x, y, y + h, M.shade (FACE, 0.8))
	yy = sidebar_tabs (c, x, y, w, "Slide")
	yy = section (c, x + 1, yy, w - 1, "Layout")
	M.dropdown (c, x + 10, yy, w - 20, 22, "Title and content"); yy += 30
	yy = section (c, x + 1, yy, w - 1, "Theme")
	tw = (w - 30) / 2
	for k, (name, a, b) in enumerate (THEMES):
		tx = x + 10 + (k % 2) * (tw + 10); ty = yy + (k // 2) * 62
		if k == 0: c.rect (tx - 3, ty - 3, tw + 6, 58, SEL, r = 4)
		c.rect (tx, ty, tw, tw * 9 / 16 * 0.85, WHITE, outline = LINE)
		c.rect (tx, ty, 4, tw * 9 / 16 * 0.85, a); c.rect (tx + 10, ty + 8, tw * 0.5, 5, a, r = 1); c.rect (tx + 10, ty + 16, 18, 3, b, r = 1)
		for i in range (2): c.rect (tx + 10, ty + 24 + i * 6, tw * 0.6, 2.5, (210, 206, 202))
		c.text_l (tx, ty + 39, 14, name, "small", WHITE if k == 0 else TEXT)
	yy += 190
	c.text_l (x + 10, yy, 22, "Colours", "small", DIM)
	for k, col in enumerate ((TEAL_D, TEAL, PEACH, GREYBLUE, INK, PALE)): c.rect (x + 64 + k * 22, yy + 3, 18, 16, col, r = 2, outline = LINE)
	yy += 26
	c.text_l (x + 10, yy, 22, "Fonts", "small", DIM); M.dropdown (c, x + 64, yy, w - 74, 22, "Liberation Sans"); yy += 30
	yy = section (c, x + 1, yy, w - 1, "Background")
	M.dropdown (c, x + 10, yy, 110, 22, "Theme"); swatch (c, x + 128, yy, WHITE, 44); yy += 28
	M.checkbox (c, x + 10, yy + 2, "Master's objects", True); yy += 26
	yy = section (c, x + 1, yy, w - 1, "Slide size")
	M.dropdown (c, x + 10, yy, w - 20, 22, "16:9 widescreen")

# ---- 4. the animations --------------------------------------------------------------------------------------
EFFECTS = [(1, "Chart", "Wipe", "by series", "On click", 0.8, "in"), (1, "Callout", "Zoom", "", "After previous", 0.5, "in"),
	   (2, "+31.2 %", "Pulse", "", "On click", 0.6, "em"), (3, "Callout", "Fade", "", "On click", 0.4, "out")]
def sidebar_anim (c, x, y, w, h):
	c.rect (x, y, w, h, PANEL); c.vline (x, y, y + h, M.shade (FACE, 0.8))
	yy = sidebar_tabs (c, x, y, w, "Animate")
	yy = section (c, x + 1, yy, w - 1, "Transition to this slide")
	M.dropdown (c, x + 10, yy, w - 20, 22, "Push"); yy += 26
	M.dropdown (c, x + 10, yy, 110, 22, "From right"); c.text_l (x + 128, yy, 22, "0.7 s", "small", DIM); yy += 26
	c.text_l (x + 10, yy, 22, "Next slide", "small", DIM); M.dropdown (c, x + 76, yy, w - 86, 22, "On click"); yy += 30
	yy = section (c, x + 1, yy, w - 1, "Effects on this slide")
	c.rect (x + 8, yy, w - 16, 4 * 38 + 6, WHITE, r = 4, outline = LINE)
	cols = dict (**{"in": (60, 150, 80), "em": (210, 160, 40), "out": (190, 70, 60)})
	for k, (n, obj, eff, opt, start, dur, kind) in enumerate (EFFECTS):
		ry = yy + 3 + k * 38
		if k == 1: c.rect (x + 10, ry, w - 20, 36, M.A (SEL, 70), r = 3)
		c.text_c (x + 12, ry, 16, 18, str (n) if start == "On click" or k == 0 else "", "smallb", DIM)
		c.ellipse (x + 38, ry + 10, 6, cols[kind])
		c.text_l (x + 50, ry + 1, 18, eff + "  ·  " + obj, "smallb" if k == 1 else "small", TEXT)
		c.text_l (x + 50, ry + 17, 16, start + ("  ·  " + opt if opt else ""), "small", DIM)
		# its bar on the time line
		bx0 = x + w - 44; c.rect (bx0, ry + 6, 36 * dur, 6, M.lighten (cols[kind], 0.3), r = 2)
	yy += 4 * 38 + 12
	bx = x + 10
	for s in ("Add ▾", "Remove", "▲", "▼"):
		bw = c.tw (s, "small") + 16; M.button (c, bx, yy, bw, 22, ""); c.text_c (bx, yy, bw, 22, s, "small"); bx += bw + 4
	yy += 30
	yy = section (c, x + 1, yy, w - 1, "Zoom  ·  Callout")
	for lab, val in (("Start", "After previous"), ("Delay", "0.2 s"), ("Duration", "0.5 s")):
		c.text_l (x + 10, yy, 22, lab, "small", DIM)
		if lab == "Start": M.dropdown (c, x + 76, yy, w - 86, 22, val)
		else: spin (c, x + 76, yy, 70, val)
		yy += 26
	M.button (c, x + 10, yy + 4, w - 20, 24, "▶  Play the slide's effects")

def anim_marks (c, geo):
	sx, sy, sc = geo
	def badge (px, py, s, col):
		c.rect (sx + px * sc, sy + py * sc, 16, 15, col, r = 2); c.text_c (sx + px * sc, sy + py * sc, 16, 15, s, "smallb", WHITE)
	badge (70, 290, "1", (60, 150, 80))
	badge (CALLOUT[0] - 46, CALLOUT[1], "1", (60, 150, 80)); badge (CALLOUT[0] - 46, CALLOUT[1] + 42, "3", (190, 70, 60))
	badge (CALLOUT[0] + 20, CALLOUT[1] + 270, "2", (210, 160, 40))
	x0, y0, x1, y1 = CALLOUT
	c.rect (sx + x0 * sc, sy + y0 * sc, (x1 - x0) * sc, (y1 - y0) * sc, outline = SEL, width = 1.5)

def mock_anim ():
	c, _ = screen ()
	normal_view (c, sidebar = sidebar_anim, sel_box = False, extra = anim_marks)
	c.save ("slides-animate.png")

# ---- 5. the shapes, the smart guides ------------------------------------------------------------------------
SHAPE_ROWS = [("Lines", ["line", "arrow", "darrow", "elbow", "curve", "free"]),
	      ("Basic shapes", ["rect", "round", "ellipse", "tri", "rtri", "diamond", "pent", "hex", "star", "heart"]),
	      ("Arrows", ["right", "left", "up", "down", "lr", "chev", "uturn", "circarrow"]),
	      ("Callouts", ["callout", "cloud", "thought"]),
	      ("Flowchart", ["process", "decision", "data", "terminal", "doc", "connector"])]
def shape_glyph (c, k, x, y, s = 18):
	col, edge = (110, 160, 210), (60, 100, 150)
	cx_, cy_ = x + s / 2, y + s / 2
	def reg (n, r0, r1 = None, rot = -math.pi / 2):
		pts = []
		m = n * (2 if r1 else 1)
		for i in range (m):
			r = r0 if (i % 2 == 0 or not r1) else r1
			a = rot + i * 2 * math.pi / m
			pts.append ((cx_ + r * math.cos (a), cy_ + r * math.sin (a)))
		c.poly (pts, col)
	if k == "line": c.line ([(x + 2, y + s - 2), (x + s - 2, y + 2)], edge, 1.6)
	elif k in ("arrow", "darrow"):
		c.line ([(x + 2, y + s - 2), (x + s - 4, y + 4)], edge, 1.6); c.poly ([(x + s - 1, y + 1), (x + s - 8, y + 3), (x + s - 3, y + 8)], edge)
		if k == "darrow": c.poly ([(x + 1, y + s - 1), (x + 8, y + s - 3), (x + 3, y + s - 8)], edge)
	elif k == "elbow": c.line ([(x + 2, y + 3), (x + s / 2, y + 3), (x + s / 2, y + s - 3), (x + s - 2, y + s - 3)], edge, 1.6)
	elif k == "curve": c.d.arc ([(x + 1) * K, (y + 3) * K, (x + s - 1) * K, (y + s + 12) * K], 190, 350, fill = edge, width = int (1.6 * K))
	elif k == "free": c.line ([(x + 2, y + 12), (x + 6, y + 4), (x + 9, y + 13), (x + 13, y + 5), (x + 16, y + 11)], edge, 1.6)
	elif k == "rect": c.rect (x + 2, y + 3, s - 4, s - 6, col)
	elif k == "round": c.rect (x + 2, y + 3, s - 4, s - 6, col, r = 4)
	elif k == "ellipse": c.d.ellipse ([(x + 1) * K, (y + 3) * K, (x + s - 1) * K, (y + s - 3) * K], fill = col)
	elif k == "tri": c.poly ([(cx_, y + 2), (x + s - 1, y + s - 2), (x + 1, y + s - 2)], col)
	elif k == "rtri": c.poly ([(x + 2, y + 2), (x + s - 2, y + s - 2), (x + 2, y + s - 2)], col)
	elif k == "diamond": reg (4, 8, rot = 0)
	elif k == "pent": reg (5, 8.5)
	elif k == "hex": reg (6, 8.5, rot = 0)
	elif k == "star": reg (5, 9, 4)
	elif k == "heart":
		c.ellipse (cx_ - 3.5, y + 6, 4.2, col); c.ellipse (cx_ + 3.5, y + 6, 4.2, col); c.poly ([(x + 1.3, y + 7.5), (x + s - 1.3, y + 7.5), (cx_, y + s - 1)], col)
	elif k in ("right", "left", "up", "down", "lr"):
		pts = [(0, 0.35), (0.55, 0.35), (0.55, 0.12), (1, 0.5), (0.55, 0.88), (0.55, 0.65), (0, 0.65)]
		if k == "lr": pts = [(0, 0.5), (0.3, 0.15), (0.3, 0.35), (0.7, 0.35), (0.7, 0.15), (1, 0.5), (0.7, 0.85), (0.7, 0.65), (0.3, 0.65), (0.3, 0.85)]
		tf = {"right": lambda a, b: (a, b), "left": lambda a, b: (1 - a, b), "up": lambda a, b: (b, 1 - a), "down": lambda a, b: (b, a), "lr": lambda a, b: (a, b)}[k]
		c.poly ([(x + 1 + tf (a, b)[0] * (s - 2), y + 1 + tf (a, b)[1] * (s - 2)) for a, b in pts], col)
	elif k == "chev": c.poly ([(x + 2, y + 2), (x + 10, y + 2), (x + s - 1, cy_), (x + 10, y + s - 2), (x + 2, y + s - 2), (x + 9, cy_)], col)
	elif k == "uturn":
		c.d.arc ([(x + 3) * K, (y + 2) * K, (x + s - 3) * K, (y + 14) * K], 180, 360, fill = col, width = int (3.5 * K))
		c.rect (x + 3, y + 8, 3.5, 8, col); c.poly ([(x + s - 8, y + 9), (x + s, y + 9), (x + s - 4.5, y + 15)], col)
	elif k == "circarrow":
		c.d.arc ([(x + 2) * K, (y + 2) * K, (x + s - 2) * K, (y + s - 2) * K], 30, 320, fill = col, width = int (3 * K))
		c.poly ([(x + s - 7, y + 1), (x + s, y + 6), (x + s - 9, y + 9)], col)
	elif k == "callout": c.rect (x + 1, y + 2, s - 2, s - 8, col, r = 3); c.poly ([(x + 4, y + s - 7), (x + 9, y + s - 7), (x + 3, y + s)], col)
	elif k == "cloud":
		for (dx, dy, r) in ((5, 10, 4.5), (9, 6, 5), (13, 10, 4.5), (9, 12, 4.5)): c.ellipse (x + dx, y + dy, r, col)
	elif k == "thought":
		c.d.ellipse ([(x + 3) * K, (y + 1) * K, (x + s - 1) * K, (y + 12) * K], fill = col); c.ellipse (x + 4, y + 14, 2, col); c.ellipse (x + 2, y + 17, 1.2, col)
	elif k == "process": c.rect (x + 1, y + 4, s - 2, s - 8, col)
	elif k == "decision": reg (4, 8, rot = 0)
	elif k == "data": c.poly ([(x + 5, y + 4), (x + s - 1, y + 4), (x + s - 5, y + s - 4), (x + 1, y + s - 4)], col)
	elif k == "terminal": c.rect (x + 1, y + 4, s - 2, s - 8, col, r = 5)
	elif k == "doc":
		c.rect (x + 2, y + 2, s - 4, s - 7, col); c.d.chord ([(x + 2) * K, (y + s - 9) * K, (x + s / 2) * K, (y + s - 1) * K], 0, 180, fill = col)
	elif k == "connector": c.ellipse (cx_, cy_, 7, col)

def shapes_popup (c, px, py):
	pw = 10 * 26 + 20
	rows = SHAPE_ROWS
	ph = 30 + sum (20 + 26 * math.ceil (len (r[1]) / 10) for r in rows) + 34
	c.rect (px + 3, py + 4, pw, ph, M.A ((0, 0, 0), 50), r = 6)
	c.rect (px, py, pw, ph, M.MENU, r = 6, outline = M.shade (FACE, 0.62))
	M.field (c, px + 8, py + 6, pw - 16, 20, "", "Search the shapes", "small")
	yy = py + 32
	for title, ks in rows:
		c.text_l (px + 10, yy, 18, title, "smallb", DIM); yy += 20
		for i, k in enumerate (ks):
			gx = px + 10 + (i % 10) * 26; gy = yy + (i // 10) * 26
			if k == "callout" and title == "Callouts": c.rect (gx - 2, gy - 2, 24, 24, M.A (SEL, 70), r = 3, outline = SEL)
			shape_glyph (c, k, gx + 1, gy + 1)
		yy += 26 * math.ceil (len (ks) / 10)
	c.hline (px + 8, px + pw - 8, yy + 2, M.LINE2)
	c.text_l (px + 12, yy + 6, 22, "Recently used:", "small", DIM)
	for i, k in enumerate (("round", "right", "callout", "star")): shape_glyph (c, k, px + 110 + i * 26, yy + 8)

def drag_guides (c, geo):
	sx, sy, sc = geo
	# the picture of the terrace being moved on the timeline slide: the guides snap it
	gx0, gy0, gx1, gy1 = 1340 - 190, 660, 1340 + 190, 840		# the Q3 card
	p = Image.open (PICT).convert ("RGB")
	w, h = 380 * sc, 200 * sc
	px, py = sx + (1340 - 190) * sc, sy + 840 * sc + 14 * sc
	# the ghost of the picture, aligned with the card above it
	ph = p.resize ((int (w * K), int (h * K)), Image.LANCZOS)
	m = Image.new ("L", ph.size, 190)
	c.img.paste (ph, (int (px * K), int ((py - 26) * K)), m); c.d = ImageDraw.Draw (c.img, "RGBA")
	handles (c, px, py - 26, w, h, rot = False)
	for gx in (px, px + w / 2, px + w):
		for k in range (0, int (sy + 640 * sc - (py - 26)) - 2, -1): pass
		y0, y1 = sy + 300 * sc, py - 26 + h + 8
		for t in range (int (y0), int (y1), 6): c.vline (gx, t, min (t + 3, y1), GUIDE, 1.2)
	# equal spacing marks
	c.rect (px - 92, py - 22, 84, 18, GUIDE, r = 9); c.text_c (px - 92, py - 22, 84, 18, "centred · Q3", "smallb", WHITE)
	c.rect (px + 4, py - 26 + h + 6, 112, 18, M.A ((30, 30, 30), 200), r = 4)
	c.text_c (px + 4, py - 26 + h + 6, 112, 18, "X 12.1  Y 14.5 cm", "small", WHITE)
	# the pointer
	mx, my = px + w * 0.55, py - 26 + h * 0.45
	c.poly ([(mx, my), (mx, my + 18), (mx + 4.5, my + 13.5), (mx + 8, my + 21), (mx + 11, my + 19.5), (mx + 7.5, my + 12.5), (mx + 13, my + 12.5)], WHITE)
	c.line ([(mx, my), (mx, my + 18), (mx + 4.5, my + 13.5), (mx + 8, my + 21), (mx + 11, my + 19.5), (mx + 7.5, my + 12.5), (mx + 13, my + 12.5), (mx, my)], TEXT, 1)

def mock_shapes ():
	c, _ = screen ()
	normal_view (c, cur = 6, sidebar = "shape", hot = "shapes", sel_box = False, extra = drag_guides,
		     notes = ["Notes", "Four steps, one per quarter. The second shop depends on the bank (decision in March)."])
	shapes_popup (c, WX + 4 + 2 + 328 + 10 + 39 * 2 + 58 + 10 + 29 + 27 + 28 + 29 - 140, WY + 28 + 30)
	c.save ("slides-shapes.png")

# ---- 6. the master ------------------------------------------------------------------------------------------
def master_slide (k = 1):
	im = Image.new ("RGB", (SW, SH), WHITE); d = ImageDraw.Draw (im, "RGBA")
	d.rectangle ([0, 0, 22, SH], fill = TEAL)
	def ph (b, s, f, col = MUTED):
		x0, y0, x1, y1 = b
		for (a, bb) in (((x0, y0), (x1, y0)), ((x0, y1), (x1, y1)), ((x0, y0), (x0, y1)), ((x1, y0), (x1, y1))):
			n = int (max (abs (bb[0] - a[0]), abs (bb[1] - a[1])) / 14)
			for i in range (0, n, 2):
				t0, t1 = i / n, (i + 1) / n
				d.line ([a[0] + (bb[0] - a[0]) * t0, a[1] + (bb[1] - a[1]) * t0, a[0] + (bb[0] - a[0]) * t1, a[1] + (bb[1] - a[1]) * t1], fill = (160, 160, 160), width = 3)
		return d
	ph ((96, 60, SW - 96, 170), "", None); d.text ((110, 74), "Click to edit the title style", font = sf (70, True), fill = TEAL_D)
	d.rounded_rectangle ([112, 178, 252, 188], 5, fill = PEACH)
	ph ((96, 240, SW - 96, 960), "", None)
	for lvl, s, px in ((0, "Click to edit the text styles", 48), (1, "Second level", 40), (2, "Third level", 34), (3, "Fourth level", 30)):
		y = 280 + lvl * 92
		x = 130 + lvl * 70
		d.ellipse ([x, y + px * 0.35, x + px * 0.42, y + px * 0.77], fill = TEAL if lvl % 2 == 0 else PEACH)
		d.text ((x + px * 0.9, y), s, font = sf (px), fill = INK)
	d.line ([110, 1010, SW - 110, 1010], fill = (226, 222, 218), width = 2)
	ph ((96, 1018, 900, 1066), "", None); d.text ((110, 1028), "<footer>", font = sf (26), fill = MUTED)
	ph ((SW - 300, 1018, SW - 96, 1066), "", None); d.text ((SW - 110, 1028), "<#>", font = sf (26, True), fill = TEAL, anchor = "ra")
	return im

def mock_master ():
	c, _ = screen (open_menu = None)
	cx, cy, cw, ch = M.window (c, WX, WY, WW, WH, "Slides — cafe-2026.odp  [Master]")
	y = toolbars (c, cx, cy, cw)
	# the master's bar
	c.rect (cx, y, cw, 32, (246, 226, 200)); c.hline (cx, cx + cw, y + 32, M.shade (FACE, 0.84))
	c.text_l (cx + 12, y, 32, "Master view", "uib", (120, 70, 30))
	bx = cx + 120
	for s in ("Insert layout", "Rename", "Delete", "Placeholders ▾", "Theme ▾", "Background ▾"):
		bw = c.tw (s, "small") + 18; M.button (c, bx, y + 5, bw, 22, ""); c.text_c (bx, y + 5, bw, 22, s, "small"); bx += bw + 6
	M.button (c, cx + cw - 150, y + 5, 140, 22, "Close the master", accent = True)
	y += 33
	h = cy + ch - 22 - y
	# the layouts, under the master
	TW = 196
	c.rect (cx, y, TW, h, PANEL); c.vline (cx + TW, y, y + h, M.shade (FACE, 0.8))
	tw = TW - 40; th = tw * 9 / 16
	yy = y + 10
	c.rect (cx + 14, yy - 4, tw + 18, th + 8, M.A (SEL, 60), r = 4)
	c.rect (cx + 20, yy, tw + 8, th, WHITE, outline = M.shade (FACE, 0.6)); layout_icon (c, cx + 20, yy, tw + 8, th, 1); c.text_l (cx + 24, yy + th - 18, 16, "Master", "smallb", TEAL)
	yy += th + 12
	for k, name in enumerate (LAYOUTS):
		if yy + 50 > y + h: break
		c.vline (cx + 18, yy - 8, yy + 24, LINE)
		if k == 1: c.rect (cx + 26, yy - 4, tw - 4, 49 + 4, SEL, r = 4)
		layout_icon (c, cx + 30, yy, 78, 44, k)
		c.text (cx + 114, yy + 4, name.split (" ")[0], "small", WHITE if k == 1 else TEXT)
		if len (name.split (" ")) > 1: c.text (cx + 114, yy + 18, " ".join (name.split (" ")[1:]), "small", WHITE if k == 1 else DIM)
		c.text (cx + 114, yy + 32, (lambda n: "%d slide%s" % (n, "" if n == 1 else "s")) ([1, 4, 0, 0, 0, 0, 1, 0][k]), "small", (220, 236, 240) if k == 1 else FAINT)
		yy += 54
	ex = cx + TW + 1; ew = cw - TW - 1 - 228
	c.rect (ex, y, ew, h, DESKGREY)
	sw = ew - 40; sh = sw * 9 / 16; sx = ex + 20; sy = y + (h - sh) / 2 - 10
	put_slide (c, 0, sx, sy, sw, im = master_slide ())
	sc = sw / SW
	handles (c, sx + 96 * sc, sy + 240 * sc, (SW - 192) * sc, 720 * sc, rot = False)
	c.text (sx, sy + sh + 8, "Title and content — used by 4 slides", "small", WHITE)
	# the right: the placeholder's text levels
	def side (c, x, y, w, h):
		c.rect (x, y, w, h, PANEL); c.vline (x, y, y + h, M.shade (FACE, 0.8))
		yy = sidebar_tabs (c, x, y, w, "Text")
		yy = section (c, x + 1, yy, w - 1, "Placeholder: content")
		for lvl, (sz, bul) in enumerate (((24, "●"), (20, "●"), (17, "–"), (15, "·"))):
			c.text_l (x + 10, yy, 22, "Level %d" % (lvl + 1), "small", DIM)
			spin (c, x + 70, yy, 52, "%d pt" % sz)
			M.dropdown (c, x + 128, yy, 44, 22, bul); swatch (c, x + w - 48, yy, TEAL if lvl % 2 == 0 else PEACH, 38)
			yy += 26
		yy += 4
		c.text_l (x + 10, yy, 22, "Font", "small", DIM); M.dropdown (c, x + 70, yy, w - 80, 22, "Liberation Sans"); yy += 26
		c.text_l (x + 10, yy, 22, "Spacing", "small", DIM); spin (c, x + 70, yy, 64, "1.15"); spin (c, x + 140, yy, 76, "12 pt"); yy += 26
		c.text_l (x + 10, yy, 22, "Autofit", "small", DIM); M.dropdown (c, x + 70, yy, w - 80, 22, "Shrink text"); yy += 30
		yy = section (c, x + 1, yy, w - 1, "On every slide of the layout")
		for s, on in (("Title", True), ("Footer", True), ("Slide number", True), ("Date", False), ("Logo (the theme's)", False)):
			M.checkbox (c, x + 12, yy + 2, s, on); yy += 24
	side (c, ex + ew, y, 228, h)
	status (c, cx, cy + ch - 22, cw, ["Master: Café", "Layout: Title and content"], view = "normal", zoom = "64%")
	c.save ("slides-master.png")

# ---- 7. the presenter's console -----------------------------------------------------------------------------
def mock_presenter ():
	c = M.Canvas ()
	BG, BG2, FG, FG2 = (26, 28, 32), (40, 43, 48), (236, 236, 236), (150, 154, 160)
	c.rect (0, 0, M.W, M.H, BG)
	# the top: where we are, the timer, the clock
	c.rect (0, 0, M.W, 48, BG2)
	c.text_l (20, 0, 48, "Slide 3 of 8", "h2", FG); c.text_l (160, 0, 48, "·  Results", "mid", FG2)
	c.text_c (0, 0, M.W, 48, "00:07:42", "clock", FG)
	c.rect (M.W / 2 + 84, 15, 18, 18, None, r = 9, outline = FG2, width = 1.5); c.rect (M.W / 2 + 89, 20, 3, 8, FG2); c.rect (M.W / 2 + 95, 20, 3, 8, FG2)
	c.text_l (M.W / 2 + 110, 0, 48, "↺", "h2", FG2)
	c.text_r (M.W - 20, 0, 48, "14:32", "h2", FG2)
	# the progress of the show: 3 / 8 and the planned time
	c.rect (0, 48, M.W, 4, (60, 64, 70)); c.rect (0, 48, M.W * 3 / 8, 4, PEACH)
	# the current slide, with the pen
	sx, sy, sw = 24, 72, 604
	sh = put_slide (c, 3, sx, sy, sw, shadow = False)
	c.rect (sx - 1, sy - 1, sw + 2, sh + 2, outline = PEACH, width = 2)
	sc = sw / SW
	c.d.ellipse ([(sx + 1360 * sc) * K, (sy + 310 * sc) * K, (sx + 1810 * sc) * K, (sy + 450 * sc) * K], outline = (226, 60, 60), width = int (2.6 * K))
	c.text (sx, sy + sh + 8, "Current slide", "small", FG2)
	# the next one
	nx = sx + sw + 24; nw = M.W - nx - 24
	c.text (nx, sy - 2, "Next", "smallb", FG2)
	nh = put_slide (c, 4, nx, sy + 18, nw, shadow = False)
	c.text (nx, sy + 18 + nh + 8, "4  ·  Best sellers  ·  Wipe", "small", FG2)
	# the effects still to come on this slide
	ey = sy + 18 + nh + 40
	c.text (nx, ey, "On this slide", "smallb", FG2); ey += 22
	for k, (n, obj, eff, opt, start, dur, kind) in enumerate (EFFECTS):
		done = k < 2
		c.ellipse (nx + 7, ey + 9, 5, (90, 90, 96) if done else ((60, 150, 80), (210, 160, 40), (210, 160, 40), (190, 70, 60))[k])
		c.text_l (nx + 20, ey, 18, eff + " · " + obj, "small", FG2 if done else FG)
		if k == 2: c.text_r (nx + nw, ey, 18, "next click", "smallb", PEACH)
		ey += 22
	# the notes, big
	ny = sy + sh + 32
	c.rect (sx - 4, ny, M.W - 40, M.H - ny - 64, BG2, r = 8)
	c.text (sx + 10, ny + 8, "Notes", "smallb", FG2)
	c.text_r (M.W - 60, ny + 8 + 7, 14, "A−   A+", "smallb", FG2)
	for k, s in enumerate (["Say first: the best year since we opened (2019). Thank the team.",
				"Point to December: the Christmas menu (+22 % on the month).",
				"Click: the +31.2 % pulses — compare with 2025 (64,370 €).",
				"Then the summer: the terrace — next slide."]):
		c.text (sx + 10, ny + 30 + k * 27, s, "pnotes", FG)
	# the controls
	by = M.H - 52
	bx = 24
	for s in ("◀  Previous", "Next  ▶", "Pen", "Pointer", "Zoom", "Black screen", "Slides…"):
		bw = c.tw (s, "ui") + 28
		on = s == "Pen"
		c.rect (bx, by, bw, 34, (226, 60, 60) if on else (52, 56, 62), r = 6)
		c.text_c (bx, by, bw, 34, s, "uib" if on else "ui", FG)
		bx += bw + 8
	c.rect (M.W - 24 - 120, by, 120, 34, (150, 56, 52), r = 6); c.text_c (M.W - 24 - 120, by, 120, 34, "End the show", "uib", FG)
	c.save ("slides-presenter.png")

if __name__ == "__main__":
	which = sys.argv[1:]
	for name, f in (("main", mock_main), ("sorter", mock_sorter), ("layouts", mock_layouts), ("animate", mock_anim),
			("shapes", mock_shapes), ("master", mock_master), ("presenter", mock_presenter)):
		if not which or name in which: f ()
