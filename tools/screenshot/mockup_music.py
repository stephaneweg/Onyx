#!/usr/bin/env python3
"""mockup_music.py -- the first mock-ups of Onyx's media player, a library app in the way of Windows Media
Player / iTunes / Rhythmbox: the music (MP3, OGG, FLAC, WAV and MIDI) by artists, albums, songs, genres,
folders and playlists, a now-playing view, a mini player; the videos later in the same app. See
docs/media/README.md.

    python3 tools/screenshot/mockup_music.py  -> docs/media/mockups/media-*.png

On the real desktop (screenshots/desktop.png, 1024 x 768); the drawing helpers are mockup_archiver.py's.
The covers are drawn here (made-up albums of made-up artists).
"""
import os, sys, math, random
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, HERE)
import mockup_archiver as M

M.W, M.H = 1024, 768
M.OUT = os.path.join (M.ROOT, "docs", "media", "mockups")
K = M.K
DESK = Image.open (os.path.join (M.ROOT, "screenshots", "desktop.png")).convert ("RGB")
TEXT, DIM, FACE, SEL, MENU, WHITE, LIST, LINE2, FAINT = M.TEXT, M.DIM, M.FACE, M.SEL, M.MENU, (255, 255, 255), M.LIST, M.LINE2, M.FAINT
SIDE = (226, 216, 209)
M.F["huge"] = M._f ("DejaVuSans-Bold.ttf", 26)
M.F["h2"] = M._f ("DejaVuSans-Bold.ttf", 20)
M.F["mid"] = M._f ("DejaVuSans.ttf", 15)

# ---- the library (made up) --------------------------------------------------------------------------
ALBUMS = [
	("Northern Lights", "Lumen Drift", 2024, "Electronic", 0),
	("Paper Boats", "The Paper Boats", 2021, "Indie", 1),
	("Saltwater", "Saltwater Choir", 2019, "Folk", 2),
	("Owls at Noon", "Mira & the Owls", 2023, "Pop", 3),
	("Grey Atlas", "Atlas Grey", 2018, "Rock", 4),
	("Blue Hour Sessions", "Kōji Arai Trio", 2022, "Jazz", 5),
	("Goldberg Variations", "J. S. Bach (MIDI)", 1741, "Classical", 6),
	("Velvet Static", "Nora Valez", 2025, "Soul", 7),
	("Long Way Home", "The Paper Boats", 2017, "Indie", 8),
	("Glass Gardens", "Lumen Drift", 2020, "Electronic", 9),
	("Harbour Lights", "Saltwater Choir", 2016, "Folk", 10),
	("Midnight Arcade", "8-Bit Parade (MIDI)", 1993, "Game", 11),
]
TRACKS = [("Aurora", "3:42"), ("Polar Wind", "4:05"), ("Northern Lights", "5:18"), ("Driftwood", "3:27"),
	  ("Snow Static", "4:51"), ("Magnetic North", "3:59"), ("Low Sun", "4:12"), ("Ice Fields", "6:03"),
	  ("Midnight Sun", "3:36"), ("Lumen", "4:44"), ("Home, Slowly", "5:02")]
PALETTES = [((22, 40, 80), (40, 170, 160), (180, 240, 200)), ((240, 228, 205), (40, 70, 110), (220, 90, 70)),
	    ((30, 60, 70), (90, 150, 160), (230, 230, 220)), ((250, 200, 90), (200, 80, 60), (60, 40, 50)),
	    ((60, 60, 66), (150, 150, 150), (230, 120, 40)), ((20, 30, 60), (60, 90, 170), (240, 200, 120)),
	    ((245, 240, 225), (120, 90, 50), (40, 30, 20)), ((90, 30, 70), (220, 90, 140), (250, 210, 170)),
	    ((200, 220, 230), (90, 120, 140), (230, 140, 90)), ((30, 20, 50), (120, 80, 200), (120, 230, 220)),
	    ((15, 45, 65), (240, 180, 80), (250, 240, 200)), ((20, 20, 30), (240, 60, 120), (80, 220, 250))]

def cover_img (i, s):
	"""Album i's cover, s px (drawn at 4 x, brought down)."""
	S = s * 4; a, b, c = PALETTES[i % len (PALETTES)]
	im = Image.new ("RGB", (S, S), a); d = ImageDraw.Draw (im, "RGBA")
	r = random.Random (i * 7 + 3)
	style = i % 6
	if style == 0:				# aurora bands
		for k in range (6):
			y0 = S * (0.25 + k * 0.08)
			pts = [(x, y0 + math.sin (x / S * 6 + k) * S * 0.06) for x in range (0, S + 8, 8)]
			d.line (pts, fill = b + (120 - k * 15,), width = int (S * 0.05))
		for _ in range (40): x, y = r.random () * S, r.random () * S * 0.5; d.ellipse ([x, y, x + 3, y + 3], fill = c + (200,))
	elif style == 1:			# boats: a horizon, folded paper
		d.rectangle ([0, S * 0.62, S, S], fill = b)
		for k in range (3):
			x = S * (0.2 + k * 0.27); y = S * (0.58 + (k % 2) * 0.06); w = S * 0.18
			d.polygon ([(x, y), (x + w, y), (x + w * 0.8, y + w * 0.3), (x + w * 0.2, y + w * 0.3)], fill = c)
			d.polygon ([(x + w * 0.5, y - w * 0.45), (x + w * 0.5, y), (x + w * 0.85, y)], fill = (250, 250, 245))
	elif style == 2:			# waves
		for k in range (9):
			y0 = S * (0.1 + k * 0.1)
			d.line ([(x, y0 + math.sin (x / S * 12 + k * 0.8) * S * 0.025) for x in range (0, S + 8, 8)], fill = (b if k % 2 else c) + (230,), width = int (S * 0.03))
	elif style == 3:			# a sun, circles
		d.ellipse ([S * 0.2, S * 0.18, S * 0.8, S * 0.78], fill = b)
		d.ellipse ([S * 0.36, S * 0.34, S * 0.64, S * 0.62], fill = c)
		d.rectangle ([0, S * 0.78, S, S], fill = c)
	elif style == 4:			# stripes, diagonal
		for k in range (-6, 12):
			x = k * S * 0.12
			d.polygon ([(x, 0), (x + S * 0.06, 0), (x + S * 0.06 + S, S), (x + S, S)], fill = (b if k % 3 else c) + (200,))
	else:					# a grid of dots / squares
		n = 6
		for yy in range (n):
			for xx in range (n):
				rr = S / n * (0.18 + 0.25 * r.random ())
				cx, cy = (xx + 0.5) * S / n, (yy + 0.5) * S / n
				if (xx + yy + i) % 3: d.ellipse ([cx - rr, cy - rr, cx + rr, cy + rr], fill = b)
				else: d.rectangle ([cx - rr, cy - rr, cx + rr, cy + rr], fill = c)
	# a small title band
	d.rectangle ([0, S * 0.86, S, S], fill = (0, 0, 0, 70))
	return im.resize ((s * K, s * K), Image.LANCZOS)

def cover (c, x, y, s, i, r = 6, shadow = True):
	if shadow: c.rect (x + 2, y + 3, s, s, M.A ((0, 0, 0), 45), r = r)
	im = cover_img (i, s)
	m = Image.new ("L", im.size, 0); ImageDraw.Draw (m).rounded_rectangle ([0, 0, im.size[0] - 1, im.size[1] - 1], r * K, fill = 255)
	c.img.paste (im, (int (x * K), int (y * K)), m); c.d = ImageDraw.Draw (c.img, "RGBA")

def screen ():
	c = M.Canvas ()
	c.img.paste (DESK.resize ((M.W * K, M.H * K), Image.LANCZOS), (0, 0)); c.d = ImageDraw.Draw (c.img, "RGBA")
	return c

# ---- the icons --------------------------------------------------------------------------------------------
def ic_note (c, x, y, s, col):
	c.ellipse (x + s * 0.3, y + s * 0.78, s * 0.2, col); c.rect (x + s * 0.44, y + s * 0.12, s * 0.1, s * 0.66, col)
	c.poly ([(x + s * 0.44, y + s * 0.1), (x + s * 0.9, y + s * 0.22), (x + s * 0.9, y + s * 0.38), (x + s * 0.54, y + s * 0.28)], col)
def ic_person (c, x, y, s, col):
	c.ellipse (x + s / 2, y + s * 0.32, s * 0.2, col); c.d.pieslice ([(x + s * 0.12) * K, (y + s * 0.58) * K, (x + s * 0.88) * K, (y + s * 1.3) * K], 180, 360, fill = col)
def ic_disc (c, x, y, s, col):
	c.ellipse (x + s / 2, y + s / 2, s * 0.45, None, col, 2); c.ellipse (x + s / 2, y + s / 2, s * 0.12, col)
def ic_list (c, x, y, s, col):
	for k in range (3): c.rect (x + s * 0.3, y + s * (0.2 + k * 0.28), s * 0.6, 2, col); c.ellipse (x + s * 0.14, y + s * (0.2 + k * 0.28) + 1, 1.8, col)
def ic_tag (c, x, y, s, col):
	c.poly ([(x + s * 0.1, y + s * 0.1), (x + s * 0.55, y + s * 0.1), (x + s * 0.92, y + s * 0.5), (x + s * 0.5, y + s * 0.92), (x + s * 0.1, y + s * 0.55)], col)
	c.ellipse (x + s * 0.3, y + s * 0.3, s * 0.08, SIDE)
def ic_folder (c, x, y, s, col): M.ic_folder (c, x, y + 1, s, (214, 170, 90))
def ic_heart (c, x, y, s, col):
	c.ellipse (x + s * 0.32, y + s * 0.36, s * 0.2, col); c.ellipse (x + s * 0.68, y + s * 0.36, s * 0.2, col)
	c.poly ([(x + s * 0.13, y + s * 0.44), (x + s * 0.87, y + s * 0.44), (x + s * 0.5, y + s * 0.86)], col)
def ic_clock (c, x, y, s, col):
	c.ellipse (x + s / 2, y + s / 2, s * 0.42, None, col, 1.8); c.line ([(x + s / 2, y + s * 0.25), (x + s / 2, y + s / 2), (x + s * 0.7, y + s * 0.6)], col, 1.8)
def ic_star (c, x, y, s, col):
	pts = []
	for k in range (10):
		a = -math.pi / 2 + k * math.pi / 5; rr = s * (0.48 if k % 2 == 0 else 0.2)
		pts.append ((x + s / 2 + math.cos (a) * rr, y + s / 2 + math.sin (a) * rr))
	c.poly (pts, col)
def ic_film (c, x, y, s, col):
	c.rect (x + 1, y + s * 0.18, s - 2, s * 0.64, None, r = 2, outline = col, width = 1.6)
	for k in range (4): c.rect (x + 3 + k * s * 0.24, y + s * 0.24, 2, 2, col); c.rect (x + 3 + k * s * 0.24, y + s * 0.7, 2, 2, col)
def ic_home (c, x, y, s, col):
	c.poly ([(x + s * 0.5, y + s * 0.08), (x + s * 0.95, y + s * 0.48), (x + s * 0.05, y + s * 0.48)], col)
	c.rect (x + s * 0.18, y + s * 0.46, s * 0.64, s * 0.46, col); c.rect (x + s * 0.42, y + s * 0.62, s * 0.16, s * 0.3, SIDE)
def ic_plus (c, x, y, s, col): c.rect (x + s / 2 - 1, y + 2, 2, s - 4, col); c.rect (x + 2, y + s / 2 - 1, s - 4, 2, col)
def ic_search (c, x, y, s, col): c.ellipse (x + s * 0.42, y + s * 0.42, s * 0.3, None, col, 1.8); c.line ([(x + s * 0.64, y + s * 0.64), (x + s * 0.92, y + s * 0.92)], col, 2)
def ic_play (c, x, y, s, col): c.poly ([(x + s * 0.28, y + s * 0.18), (x + s * 0.28, y + s * 0.82), (x + s * 0.84, y + s * 0.5)], col)
def ic_pause (c, x, y, s, col): c.rect (x + s * 0.26, y + s * 0.2, s * 0.16, s * 0.6, col, r = 1); c.rect (x + s * 0.58, y + s * 0.2, s * 0.16, s * 0.6, col, r = 1)
def ic_prev (c, x, y, s, col): c.rect (x + s * 0.18, y + s * 0.22, s * 0.1, s * 0.56, col); c.poly ([(x + s * 0.82, y + s * 0.22), (x + s * 0.82, y + s * 0.78), (x + s * 0.3, y + s * 0.5)], col)
def ic_next (c, x, y, s, col): c.rect (x + s * 0.72, y + s * 0.22, s * 0.1, s * 0.56, col); c.poly ([(x + s * 0.18, y + s * 0.22), (x + s * 0.18, y + s * 0.78), (x + s * 0.7, y + s * 0.5)], col)
def ic_shuffle (c, x, y, s, col):
	c.line ([(x + 2, y + s * 0.3), (x + s * 0.35, y + s * 0.3), (x + s * 0.65, y + s * 0.7), (x + s - 4, y + s * 0.7)], col, 1.8)
	c.line ([(x + 2, y + s * 0.7), (x + s * 0.35, y + s * 0.7), (x + s * 0.65, y + s * 0.3), (x + s - 4, y + s * 0.3)], col, 1.8)
	for yy in (0.3, 0.7): c.poly ([(x + s - 6, y + s * yy - 4), (x + s, y + s * yy), (x + s - 6, y + s * yy + 4)], col)
def ic_repeat (c, x, y, s, col, one = False):
	c.rect (x + 2, y + s * 0.25, s - 4, s * 0.5, None, r = 4, outline = col, width = 1.8)
	c.poly ([(x + s * 0.55, y + s * 0.25 - 4), (x + s * 0.7, y + s * 0.25), (x + s * 0.55, y + s * 0.25 + 4)], col)
	if one: c.text (x + s / 2, y + s / 2 + 1, "1", "tiny", col, "mm")
def ic_volume (c, x, y, s, col):
	c.poly ([(x + 1, y + s * 0.36), (x + s * 0.28, y + s * 0.36), (x + s * 0.55, y + s * 0.12), (x + s * 0.55, y + s * 0.88), (x + s * 0.28, y + s * 0.64), (x + 1, y + s * 0.64)], col)
	for rr in (0.22, 0.38): c.d.arc ([(x + s * 0.55 - s * rr) * K, (y + s / 2 - s * rr) * K, (x + s * 0.55 + s * rr) * K, (y + s / 2 + s * rr) * K], -50, 50, fill = col, width = int (1.6 * K))
def ic_queue (c, x, y, s, col):
	for k in range (3): c.rect (x + 2, y + s * (0.2 + k * 0.22), s * 0.6, 2, col)
	ic_play (c, x + s * 0.45, y + s * 0.45, s * 0.55, col)
def ic_mini (c, x, y, s, col):
	c.rect (x + 1, y + 2, s - 2, s - 4, None, r = 2, outline = col, width = 1.6); c.rect (x + s * 0.45, y + s * 0.5, s * 0.42, s * 0.3, col, r = 1)
def ic_eq (c, x, y, s, col, phase = 0):
	for k, h in enumerate ((0.55, 0.9, 0.4, 0.75)):
		hh = s * (0.3 + 0.7 * abs (math.sin (h * 7 + phase + k)))
		c.rect (x + k * s * 0.26, y + s - hh, s * 0.18, hh, col, r = 1)
def ic_more (c, x, y, s, col):
	for k in range (3): c.ellipse (x + s * (0.2 + k * 0.3), y + s / 2, 1.8, col)

# ---- the window's parts ------------------------------------------------------------------------------------
WX, WY, WW, WH = 14, 32, 996, 640
SIDE_W, TOP_H, NOW_H = 200, 50, 78

def round_btn (c, x, y, d, icon, col = TEXT, fill = None, outline = True):
	if fill: c.ellipse (x + d / 2, y + d / 2, d / 2, fill)
	elif outline: c.ellipse (x + d / 2, y + d / 2, d / 2, M.A (WHITE, 120), M.shade (FACE, 0.75), 1)
	icon (c, x + d * 0.22, y + d * 0.22, d * 0.56, col)

def topbar (c, cx, cy, cw, title_path = None, search = "", view = "grid"):
	c.rect (cx, cy, cw, TOP_H, FACE)
	round_btn (c, cx + 12, cy + 10, 30, lambda c, a, b, s, col: c.line ([(a + s * 0.65, b + s * 0.15), (a + s * 0.3, b + s / 2), (a + s * 0.65, b + s * 0.85)], col, 2.2))
	round_btn (c, cx + 48, cy + 10, 30, lambda c, a, b, s, col: c.line ([(a + s * 0.35, b + s * 0.15), (a + s * 0.7, b + s / 2), (a + s * 0.35, b + s * 0.85)], col, 2.2), FAINT)
	if title_path:
		x = cx + 92
		for k, p in enumerate (title_path):
			last = k == len (title_path) - 1
			c.text_l (x, cy, TOP_H, p, "uib" if last else "ui", TEXT if last else M.LINK); x += c.tw (p, "uib" if last else "ui") + 8
			if not last: c.text_l (x, cy, TOP_H, "›", "ui", DIM); x += 14
	# the search, the views
	sx = cx + cw - 12 - 280
	c.rect (sx, cy + 11, 220, 28, WHITE, r = 14, outline = M.LINE)
	ic_search (c, sx + 10, cy + 17, 16, DIM)
	c.text_l (sx + 34, cy + 11, 28, search or "Search the library", "ui", TEXT if search else FAINT)
	vx = cx + cw - 12 - 50
	c.rect (vx, cy + 11, 50, 28, WHITE, r = 6, outline = M.LINE)
	c.rect (vx + 2, cy + 13, 23, 24, SEL if view == "grid" else WHITE, r = 5)
	for k in range (4): c.rect (vx + 7 + (k % 2) * 7, cy + 18 + (k // 2) * 7, 5, 5, WHITE if view == "grid" else DIM)
	c.rect (vx + 25, cy + 13, 23, 24, SEL if view == "list" else WHITE, r = 5)
	for k in range (3): c.rect (vx + 30, cy + 18 + k * 5, 13, 2, WHITE if view == "list" else DIM)
	c.hline (cx, cx + cw, cy + TOP_H, M.shade (FACE, 0.85))

SIDE_ITEMS = [("Home", ic_home), ("LIBRARY", None), ("Artists", ic_person), ("Albums", ic_disc), ("Songs", ic_note), ("Genres", ic_tag), ("Folders", ic_folder),
	      ("VIDEOS", None), ("Films", ic_film), ("Clips and series", ic_film),
	      ("PLAYLISTS", None), ("Favourites", ic_heart), ("Recently added", ic_clock),
	      ("Sunday morning", ic_list), ("Workout", ic_list)]
def sidebar (c, x, y, h, sel):
	c.rect (x, y, SIDE_W, h, SIDE)
	c.vline (x + SIDE_W, y, y + h, M.shade (FACE, 0.82))
	yy = y + 8
	for label, icon in SIDE_ITEMS:
		if icon is None:
			yy += 4; c.text (x + 16, yy + 4, label, "smallb", DIM); yy += 20; continue
		on = label == sel
		if on: c.rect (x + 8, yy, SIDE_W - 16, 26, SEL, r = 6)
		col = WHITE if on else TEXT
		dim = False
		icon (c, x + 18, yy + 5, 16, col if not dim else FAINT)
		c.text_l (x + 44, yy, 26, label, "uib" if on else "ui", col if not dim else FAINT)
		if dim: M.badge (c, x + 150, yy + 7, "soon", (200, 190, 182), WHITE)
		yy += 28
	c.text_l (x + 18, y + h - 34, 30, "+  New playlist", "ui", M.LINK)

def nowbar (c, x, y, w, i = 0, title = "Northern Lights", artist = "Lumen Drift  ·  Northern Lights", t = 0.38, elapsed = "2:01", total = "5:18",
	    playing = True, shuffle = False, repeat = 1):
	c.rect (x, y, w, NOW_H, MENU); c.hline (x, x + w, y, M.shade (FACE, 0.8))
	cover (c, x + 12, y + 11, 56, i, r = 5, shadow = False)
	c.text (x + 80, y + 18, title, "uib"); c.text (x + 80, y + 40, artist, "small", DIM)
	ic_heart (c, x + 80 + max (c.tw (title, "uib"), 120) + 10, y + 17, 15, (220, 70, 80))
	# the transport, centred
	mx = x + w / 2
	ic_shuffle (c, mx - 118, y + 14, 18, SEL if shuffle else DIM)
	ic_prev (c, mx - 68, y + 12, 22, TEXT)
	c.ellipse (mx, y + 24, 19, SEL); (ic_pause if playing else ic_play) (c, mx - 10, y + 14, 20, WHITE)
	ic_next (c, mx + 46, y + 12, 22, TEXT)
	ic_repeat (c, mx + 100, y + 14, 18, SEL if repeat else DIM, one = repeat == 2)
	# the progress
	bx, bw = mx - 190, 380
	c.text_r (bx - 10, y + 50, 16, elapsed, "small", DIM); c.text_l (bx + bw + 10, y + 50, 16, total, "small", DIM)
	c.rect (bx, y + 56, bw, 4, (214, 204, 196), r = 2); c.rect (bx, y + 56, bw * t, 4, SEL, r = 2)
	c.ellipse (bx + bw * t, y + 58, 6, WHITE, SEL, 2)
	# the right: queue, mini, volume
	rx = x + w - 16
	c.rect (rx - 90, y + 36, 90, 4, (214, 204, 196), r = 2); c.rect (rx - 90, y + 36, 60, 4, M.shade (TEXT, 1) if False else (110, 100, 94), r = 2)
	c.ellipse (rx - 30, y + 38, 5, WHITE, (110, 100, 94), 1.5)
	ic_volume (c, rx - 118, y + 29, 18, TEXT)
	ic_mini (c, rx - 150, y + 29, 18, TEXT)
	ic_queue (c, rx - 180, y + 29, 18, TEXT)

def app_window (c, title = "Media Player"):
	cx, cy, cw, ch = M.window (c, WX, WY, WW, WH, title)
	c.rect (cx, cy, cw, ch, LIST)
	return cx, cy, cw, ch

def section_head (c, x, y, w, title, sub, right = None):
	c.text (x, y, title, "huge"); c.text (x + c.tw (title, "huge") + 14, y + 10, sub, "ui", DIM)
	if right:
		rw = c.tw (right) + 44
		M.dropdown (c, x + w - rw, y + 2, rw, 28, right)

# ---- 1. the albums ---------------------------------------------------------------------------------------
def shot_albums ():
	c = screen ()
	cx, cy, cw, ch = app_window (c)
	topbar (c, cx, cy, cw, ["Music", "Albums"])
	body_h = ch - TOP_H - NOW_H
	sidebar (c, cx, cy + TOP_H + 1, body_h - 1, "Albums")
	ax, ay, aw = cx + SIDE_W + 24, cy + TOP_H + 18, cw - SIDE_W - 48
	section_head (c, ax, ay, aw, "Albums", "12 albums  ·  128 songs  ·  9 h 41 min", "Sort: Recently added")
	S, G = 132, 22
	cols = int ((aw + G) // (S + G))
	gx = ax + (aw - (cols * S + (cols - 1) * G)) / 2
	for k, (t, a, yr, g, i) in enumerate (ALBUMS[:cols * 2]):
		x = gx + (k % cols) * (S + G); y = ay + 52 + (k // cols) * (S + 58)
		cover (c, x, y, S, i)
		if k == 0:									# the one playing
			c.rect (x + S - 34, y + S - 34, 28, 28, M.A ((0, 0, 0), 150), r = 14); ic_eq (c, x + S - 27, y + S - 27, 14, WHITE)
		if k == 5:									# under the pointer: play, more
			c.rect (x, y, S, S, M.A ((0, 0, 0), 70), r = 6)
			c.ellipse (x + 26, y + S - 26, 17, SEL); ic_play (c, x + 16, y + S - 36, 20, WHITE)
			c.ellipse (x + S - 22, y + S - 26, 14, M.A (WHITE, 220)); ic_more (c, x + S - 31, y + S - 35, 18, TEXT)
			M.cursor (c, x + 30, y + S - 22)
		c.text (x, y + S + 8, t if c.tw (t, "uib") < S else t[:16] + "…", "uib"); c.text (x, y + S + 27, a, "small", DIM)
	nowbar (c, cx, cy + ch - NOW_H, cw)
	c.save ("media-albums.png")

# ---- 2. an album ------------------------------------------------------------------------------------------
def track_rows (c, x, y, w, rows, playing = 2, sel = (), cols = None, head = True, hot = None):
	"""rows: tuples (n, title, artist, album, time, fmt); cols: which columns, with their x."""
	if head:
		c.rect (x, y, w, 28, M.HEAD if hasattr (M, "HEAD") else (232, 224, 218))
		for name, cxx, align in cols:
			(c.text_r if align == "r" else c.text_l) (x + cxx, y, 28, name, "smallb", DIM)
		y += 30
	for k, r in enumerate (rows):
		ry = y + k * 32
		on = k in sel; pl = k == playing
		if on: c.rect (x + 4, ry, w - 8, 30, M.SEL_SOFT, r = 5)
		elif k == hot: c.rect (x + 4, ry, w - 8, 30, (238, 232, 228), r = 5)
		elif k % 2: c.rect (x + 4, ry, w - 8, 30, (243, 240, 238), r = 5)
		for (name, cxx, align), v in zip (cols, r):
			col = SEL if pl and name in ("#", "Title") else (TEXT if name in ("Title",) else DIM)
			font = "uib" if pl and name == "Title" else ("ui" if name in ("Title", "Artist", "Album") else "small")
			if name == "#" and pl: ic_eq (c, x + cxx - 12, ry + 9, 12, SEL); continue
			if name == "fmt":
				M.badge (c, x + cxx, ry + 8, v, (176, 166, 158) if v != "MIDI" else (120, 96, 196), WHITE); continue
			(c.text_r if align == "r" else c.text_l) (x + cxx, ry, 30, v, font, col)
	return y + len (rows) * 32

def shot_album ():
	c = screen ()
	cx, cy, cw, ch = app_window (c)
	topbar (c, cx, cy, cw, ["Music", "Albums", "Northern Lights"])
	body_h = ch - TOP_H - NOW_H
	sidebar (c, cx, cy + TOP_H + 1, body_h - 1, "Albums")
	ax, ay, aw = cx + SIDE_W + 24, cy + TOP_H + 20, cw - SIDE_W - 48
	# the header: the cover, the album's facts, its buttons -- on a band of the cover's colour
	band = Image.new ("RGB", (int (aw * K), int (200 * K)), PALETTES[0][0])
	blur = cover_img (0, 60).resize ((int (aw * K), int (200 * K))).filter (ImageFilter.GaussianBlur (24 * K))
	band = Image.blend (band, blur, 0.6)
	m = Image.new ("L", band.size, 0); ImageDraw.Draw (m).rounded_rectangle ([0, 0, band.size[0] - 1, band.size[1] - 1], 10 * K, fill = 255)
	c.img.paste (band, (int (ax * K), int (ay * K)), m); c.d = ImageDraw.Draw (c.img, "RGBA")
	cover (c, ax + 20, ay + 20, 160, 0)
	tx = ax + 204
	c.text (tx, ay + 26, "ALBUM", "smallb", (200, 230, 230))
	c.text (tx, ay + 44, "Northern Lights", "huge", WHITE)
	c.text (tx, ay + 80, "Lumen Drift", "mid", (230, 240, 240))
	c.text (tx, ay + 104, "2024  ·  Electronic  ·  11 songs  ·  49 min  ·  FLAC 44.1 kHz", "small", (200, 220, 222))
	M.button (c, tx, ay + 140, 100, 34, "", accent = True); ic_play (c, tx + 12, ay + 148, 18, WHITE); c.text_l (tx + 36, ay + 140, 34, "Play", "uib", WHITE)
	c.rect (tx + 110, ay + 140, 112, 34, M.A (WHITE, 200), r = 5); ic_shuffle (c, tx + 120, ay + 148, 18, TEXT); c.text_l (tx + 146, ay + 140, 34, "Shuffle", "ui")
	c.rect (tx + 232, ay + 140, 34, 34, M.A (WHITE, 200), r = 17); ic_heart (c, tx + 241, ay + 149, 16, (220, 70, 80))
	c.rect (tx + 274, ay + 140, 34, 34, M.A (WHITE, 200), r = 17); ic_more (c, tx + 282, ay + 148, 18, TEXT)
	cols = [("#", 30, "r"), ("Title", 50, "l"), ("Artist", 380, "l"), ("Time", aw - 70, "r"), ("fmt", aw - 56, "l")]
	rows = [(str (k + 1), t, "Lumen Drift", d, "FLAC") for k, (t, d) in enumerate (TRACKS[:6])]
	track_rows (c, ax, ay + 216, aw, rows, playing = 2, cols = [cl for cl in cols], hot = 5)
	M.cursor (c, ax + 300, ay + 216 + 30 + 5 * 32 + 12)
	nowbar (c, cx, cy + ch - NOW_H, cw)
	c.save ("media-album.png")

# ---- 3. the songs, as a list: several selected, the context menu ------------------------------------------------
def shot_songs ():
	c = screen ()
	cx, cy, cw, ch = app_window (c)
	topbar (c, cx, cy, cw, ["Music", "Songs"], view = "list")
	body_h = ch - TOP_H - NOW_H
	sidebar (c, cx, cy + TOP_H + 1, body_h - 1, "Songs")
	ax, ay, aw = cx + SIDE_W + 24, cy + TOP_H + 18, cw - SIDE_W - 48
	section_head (c, ax, ay, aw, "Songs", "128 songs", "Sort: Artist")
	songs = []
	for k in range (12):
		t, a, yr, g, i = ALBUMS[k % len (ALBUMS)]
		tt, d = TRACKS[(k * 5) % len (TRACKS)]
		fmt = "MIDI" if "MIDI" in a else ["MP3", "FLAC", "OGG", "MP3", "WAV"][k % 5]
		songs.append ((tt, a, t, d, fmt))
	songs.sort (key = lambda s: s[1])
	cols = [("Title", 12, "l"), ("Artist", 220, "l"), ("Album", 400, "l"), ("Time", aw - 70, "r"), ("fmt", aw - 56, "l")]
	pl = [k for k, s in enumerate (songs) if s[1] == "Lumen Drift"][0]
	track_rows (c, ax, ay + 48, aw, songs, playing = pl, sel = (4, 5, 6), cols = cols)
	# the context menu of the selection, its submenu open
	mx, my = ax + 330, ay + 40
	h = M.menu_popup (c, mx, my, 220, [("Play", "Enter"), ("Play next", ""), ("Add to the queue", ""), None, ("Add to a playlist", "›"),
		("Add to Favourites", ""), None, ("Go to the album", ""), ("Go to the artist", ""), None, ("Properties...", "Alt+Enter"),
		("Show in the File Viewer", ""), ("Remove from the library", "Del")], hot = "Add to a playlist", title = "3 songs")
	M.menu_popup (c, mx + 214, my + 26 + 5 + 4 * 26 - 5, 190, [("New playlist...", ""), None, ("Sunday morning", ""), ("Workout", ""), ("Piano evenings", "")], hot = "Workout")
	M.cursor (c, mx + 300, my + 26 + 5 + 4 * 26 + 70)
	nowbar (c, cx, cy + ch - NOW_H, cw)
	c.save ("media-songs.png")

# ---- 4. now playing: the full view ------------------------------------------------------------------------------
def shot_nowplaying ():
	c = screen ()
	cx, cy, cw, ch = app_window (c)
	# the backdrop: the cover blurred over the whole client area, darkened
	bg = cover_img (0, 80).resize ((int (cw * K), int (ch * K))).filter (ImageFilter.GaussianBlur (40 * K))
	dark = Image.new ("RGB", bg.size, (10, 14, 24)); bg = Image.blend (bg, dark, 0.45)
	c.img.paste (bg, (int (cx * K), int (cy * K))); c.d = ImageDraw.Draw (c.img, "RGBA")
	round_btn (c, cx + 16, cy + 14, 32, lambda c, a, b, s, col: c.line ([(a + s * 0.15, b + s * 0.35), (a + s / 2, b + s * 0.7), (a + s * 0.85, b + s * 0.35)], col, 2.2), WHITE, fill = M.A (WHITE, 40))
	c.text_l (cx + 58, cy + 14, 32, "Now playing", "uib", WHITE)
	# the cover, the song
	S = 300; x0, y0 = cx + 60, cy + 70
	cover (c, x0, y0, S, 0, r = 10)
	c.text (x0, y0 + S + 22, "Northern Lights", "huge", WHITE)
	c.text (x0, y0 + S + 58, "Lumen Drift  ·  Northern Lights (2024)", "mid", (210, 222, 230))
	c.text (x0, y0 + S + 84, "FLAC  ·  44.1 kHz  ·  16 bit  ·  912 kbit/s", "small", (170, 186, 196))
	# the transport, large
	ty = y0 + S + 118
	c.rect (x0, ty, S, 4, M.A (WHITE, 70), r = 2); c.rect (x0, ty, S * 0.38, 4, WHITE, r = 2); c.ellipse (x0 + S * 0.38, ty + 2, 7, WHITE)
	c.text (x0, ty + 12, "2:01", "small", (200, 210, 220)); c.text_r (x0 + S, ty + 20, 0, "-3:17", "small", (200, 210, 220))
	mx = x0 + S / 2; by = ty + 58
	ic_shuffle (c, mx - 140, by - 9, 18, (200, 210, 220)); ic_prev (c, mx - 82, by - 13, 26, WHITE)
	c.ellipse (mx, by, 27, WHITE); ic_pause (c, mx - 14, by - 14, 28, (20, 30, 50))
	ic_next (c, mx + 56, by - 13, 26, WHITE); ic_repeat (c, mx + 122, by - 9, 18, WHITE)
	# up next
	qx, qy, qw = cx + cw - 360, cy + 70, 320
	c.rect (qx, qy, qw, 470, M.A ((255, 255, 255), 28), r = 12)
	c.text (qx + 18, qy + 16, "Up next", "big", WHITE); c.text_r (qx + qw - 18, qy + 24, 0, "Clear", "ui", (190, 220, 230))
	q = [("Driftwood", "Lumen Drift", "3:27", 0), ("Snow Static", "Lumen Drift", "4:51", 0), ("Magnetic North", "Lumen Drift", "3:59", 0),
	     ("Paper Boats", "The Paper Boats", "4:18", 1), ("Saltwater", "Saltwater Choir", "5:40", 2), ("Owls at Noon", "Mira & the Owls", "3:12", 3),
	     ]
	for k, (t, a, d, i) in enumerate (q):
		ry = qy + 56 + k * 56
		if k == 1: c.rect (qx + 8, ry - 4, qw - 16, 52, M.A (WHITE, 40), r = 8)
		cover (c, qx + 18, ry, 44, i, r = 4, shadow = False)
		c.text (qx + 74, ry + 4, t, "uib", WHITE); c.text (qx + 74, ry + 24, a, "small", (190, 205, 215))
		c.text_r (qx + qw - 18, ry + 22, 0, d, "small", (190, 205, 215))
	c.text (qx + 18, qy + 450 - 18, "From the album, then shuffled: 24 more", "small", (170, 190, 200))
	c.save ("media-nowplaying.png")

# ---- 5. a MIDI file playing: its notes, a colour an instrument, scrolling under the playhead --------------------
TRACK_COLOURS = [(240, 110, 90), (250, 190, 70), (120, 210, 110), (80, 190, 230), (170, 130, 250), (250, 120, 200), (90, 220, 200)]
TRACK_NAMES = ["Piano", "Strings", "Horn", "Flute", "Pizzicato", "Bass", "Harp"]
def piano_roll (c, x, y, w, h, seed = 5, playhead = 0.32, keys = True):
	c.rect (x, y, w, h, (24, 22, 34), r = 10)
	kx = x + (46 if keys else 0)
	if keys:					# the keyboard at the left
		rows = 36; kh = (h - 16) / rows
		for k in range (rows):
			black = (k % 12) in (1, 3, 6, 8, 10)
			c.rect (x + 8, y + 8 + k * kh, 30 if not black else 20, kh - 0.6, (60, 58, 70) if black else (230, 228, 236), r = 1)
	r = random.Random (seed)
	# the bars
	for b in range (9):
		bx = kx + 8 + b * (w - (kx - x) - 16) / 8
		c.vline (bx, y + 8, y + h - 8, M.A (WHITE, 26))
		c.text (bx + 4, y + 10, str (17 + b), "small", (130, 128, 150))
	rows = 36; kh = (h - 16) / rows
	for t, col in enumerate (TRACK_COLOURS):
		base = [24, 18, 14, 8, 20, 30, 4][t]
		px = kx + 10 + r.random () * 20
		while px < x + w - 20:
			ln = (8 + r.random () * 44) * (2 if t in (1, 5) else 1)
			row = base + r.randint (-3, 3)
			ln = min (ln, x + w - 10 - px)
			if ln < 6: break
			if 0 <= row < rows:
				ny = y + 8 + row * kh
				past = px + ln < x + w * playhead
				cc = col if not past else tuple (int (v * 0.55 + 24 * 0.45) for v in col)
				c.rect (px, ny + 0.6, ln - 2, kh - 1.4, cc, r = 2)
				if px < x + w * playhead < px + ln:	# sounding: lit
					c.rect (px - 1, ny - 0.6, ln, kh + 1.2, None, r = 3, outline = WHITE, width = 1.4)
			px += ln + r.random () * 26
	ph = x + w * playhead
	c.rect (ph - 1, y + 4, 2, h - 8, WHITE)
	c.poly ([(ph - 6, y + 4), (ph + 6, y + 4), (ph, y + 11)], WHITE)

def shot_midi ():
	c = screen ()
	cx, cy, cw, ch = app_window (c)
	topbar (c, cx, cy, cw, ["Music", "Albums", "Goldberg Variations"])
	body_h = ch - TOP_H - NOW_H
	sidebar (c, cx, cy + TOP_H + 1, body_h - 1, "Albums")
	ax, ay, aw = cx + SIDE_W + 24, cy + TOP_H + 18, cw - SIDE_W - 48
	cover (c, ax, ay, 104, 6)
	c.text (ax + 124, ay + 2, "MIDI  ·  NOW PLAYING", "smallb", (120, 96, 196))
	c.text (ax + 124, ay + 20, "Aria (Goldberg Variations, BWV 988)", "h2")
	c.text (ax + 124, ay + 50, "J. S. Bach  ·  arranged for strings  ·  7 instruments  ·  ♩ = 64", "small", DIM)
	c.text (ax + 124, ay + 78, "Played by", "small", DIM)
	M.dropdown (c, ax + 184, ay + 72, 250, 28, "GeneralUser GS  (30 MB)")
	piano_roll (c, ax, ay + 122, aw, 300)
	# the instruments: a dot of their colour each
	lx = ax
	for col, n in zip (TRACK_COLOURS, TRACK_NAMES):
		c.ellipse (lx + 6, ay + 440, 5, col); c.text_l (lx + 16, ay + 430, 20, n, "small", DIM); lx += 30 + c.tw (n, "small")
	nowbar (c, cx, cy + ch - NOW_H, cw, i = 6, title = "Aria", artist = "J. S. Bach (MIDI)  ·  Goldberg Variations", t = 0.31, elapsed = "1:22", total = "4:25")
	c.save ("media-midi.png")

# ---- 6. the mini player: the window reduced to a card at the bottom right of the screen -------------------------
def shot_mini ():
	c = screen ()
	w, h = 300, 92
	x, y = M.W - w - 12, 676 - h - 10
	c.rect (x + 2, y + 4, w, h, M.A ((0, 0, 0), 60), r = 12)
	bg = cover_img (3, 60).resize ((int (w * K), int (h * K))).filter (ImageFilter.GaussianBlur (26 * K))
	bg = Image.blend (bg, Image.new ("RGB", bg.size, (22, 20, 28)), 0.5)
	m = Image.new ("L", bg.size, 0); ImageDraw.Draw (m).rounded_rectangle ([0, 0, bg.size[0] - 1, bg.size[1] - 1], 12 * K, fill = 255)
	c.img.paste (bg, (int (x * K), int (y * K)), m); c.d = ImageDraw.Draw (c.img, "RGBA")
	c.rect (x, y, w, h, None, r = 12, outline = M.A (WHITE, 60))
	cover (c, x + 10, y + 10, 72, 3, r = 6, shadow = False)
	c.text (x + 94, y + 12, "Owls at Noon", "uib", WHITE)
	c.text (x + 94, y + 31, "Mira & the Owls", "small", (220, 220, 225))
	bx, bw = x + 94, w - 110
	c.rect (bx, y + 52, bw, 3, M.A (WHITE, 70), r = 2); c.rect (bx, y + 52, bw * 0.62, 3, WHITE, r = 2)
	mx = bx + 46
	ic_prev (c, mx - 46, y + 62, 18, WHITE); c.ellipse (mx, y + 71, 12, WHITE); ic_pause (c, mx - 7, y + 64, 14, (30, 30, 40)); ic_next (c, mx + 28, y + 62, 18, WHITE)
	c.text_r (x + w - 12, y + 72, 0, "2:10 / 3:31", "small", (210, 210, 215))
	# its two small buttons, top right: the window back, close
	for k, glyph in enumerate (("max", "close")):
		gx = x + w - 44 + k * 20; gy = y + 8
		c.ellipse (gx + 7, gy + 7, 8, M.A (WHITE, 50))
		if glyph == "close": c.line ([(gx + 4, gy + 4), (gx + 10, gy + 10)], WHITE, 1.4); c.line ([(gx + 10, gy + 4), (gx + 4, gy + 10)], WHITE, 1.4)
		else: c.rect (gx + 3, gy + 3, 8, 8, None, outline = WHITE, width = 1.3)
	M.cursor (c, mx + 4, y + 76)
	c.save ("media-mini.png")

# ---- 7. the first start: the library is empty ------------------------------------------------------------------
def shot_welcome ():
	c = screen ()
	cx, cy, cw, ch = app_window (c)
	topbar (c, cx, cy, cw, ["Music"])
	body_h = ch - TOP_H
	sidebar (c, cx, cy + TOP_H + 1, body_h - 1, "Albums")
	ax, aw = cx + SIDE_W, cw - SIDE_W
	mx = ax + aw / 2; y = cy + TOP_H + 90
	for k in range (3): cover (c, mx - 110 + k * 70, y + (10 if k != 1 else 0), 90, [1, 0, 3][k], r = 8)
	ic_note (c, mx - 14, y + 30, 28, WHITE)
	y += 130
	c.text (mx, y, "Your music, all in one place", "huge", TEXT, "mt")
	c.text (mx, y + 40, "Media Player finds the songs in the folders you give it, and keeps watching them.", "ui", DIM, "mt")
	c.text (mx, y + 60, "MP3, OGG, FLAC, WAV and MIDI (played through a SoundFont).", "ui", DIM, "mt")
	# the folders
	fy = y + 100; fw = 420; fx = mx - fw / 2
	c.rect (fx, fy, fw, 118, WHITE, r = 8, outline = LINE2)
	for k, (p, n) in enumerate ((("SD:/Music", "128 songs found"), ("USB:/Albums", "the USB drive: 41 songs found"), ("SD1:/Midi", "scanning...  212"))):
		ry = fy + 10 + k * 34
		ic_folder (c, fx + 14, ry + 6, 18, None)
		c.text_l (fx + 42, ry, 30, p, "uib"); c.text_r (fx + fw - 40, ry + 15, 0, n, "small", DIM)
		if k == 2: M.progress (c, fx + fw - 160, ry + 22, 120, 5, 0.6)
		c.text_l (fx + fw - 26, ry, 30, "×", "ui", FAINT)
	M.button (c, mx - 150, fy + 136, 140, 34, "Add a folder...")
	M.button (c, mx + 10, fy + 136, 140, 34, "Done", accent = True)
	M.cursor (c, mx + 70, fy + 156)
	c.save ("media-welcome.png")

# ---- 8. the home: the music and the videos, what to go on with ---------------------------------------------------
def frame_img (i, w, h):
	"""A video's frame (made up): a landscape, a city at night, the sea at sunset, a cartoon, a concert."""
	W2, H2 = w * 4, h * 4
	im = Image.new ("RGB", (W2, H2)); d = ImageDraw.Draw (im, "RGBA")
	sk = [((90, 150, 220), (210, 230, 250)), ((10, 14, 40), (60, 40, 90)), ((250, 140, 80), (120, 60, 120)), ((120, 210, 250), (200, 240, 255)),
	      ((20, 10, 30), (90, 20, 60)), ((40, 80, 60), (150, 190, 140))][i % 6]
	for yy in range (H2):
		t = yy / H2; d.line ([(0, yy), (W2, yy)], fill = tuple (int (a + (b - a) * t) for a, b in zip (*sk)))
	r = random.Random (i)
	if i % 6 == 0:
		for k, col in enumerate (((70, 100, 140), (50, 80, 110), (40, 70, 60))):
			pts = [(0, H2)] + [(x, H2 * (0.45 + 0.12 * k) + math.sin (x / W2 * 7 + k) * H2 * 0.08) for x in range (0, W2 + 10, 10)] + [(W2, H2)]
			d.polygon (pts, fill = col)
	elif i % 6 == 1:
		for k in range (24):
			bw = W2 / 24; bh = H2 * (0.25 + r.random () * 0.5)
			d.rectangle ([k * bw, H2 - bh, (k + 1) * bw - 2, H2], fill = (18, 20, 36))
			for _ in range (6): wx = k * bw + r.random () * (bw - 6); wy = H2 - bh + r.random () * bh; d.rectangle ([wx, wy, wx + 4, wy + 5], fill = (250, 210, 120))
	elif i % 6 == 2:
		d.ellipse ([W2 * 0.4, H2 * 0.35, W2 * 0.6, H2 * 0.35 + W2 * 0.2], fill = (255, 220, 140))
		d.rectangle ([0, H2 * 0.6, W2, H2], fill = (60, 40, 90))
		for k in range (8): d.line ([(W2 * 0.35 + k * 6, H2 * (0.62 + k * 0.04)), (W2 * 0.65 - k * 6, H2 * (0.62 + k * 0.04))], fill = (255, 200, 120, 160), width = 4)
	elif i % 6 == 3:
		d.rectangle ([0, H2 * 0.7, W2, H2], fill = (110, 190, 90))
		d.ellipse ([W2 * 0.35, H2 * 0.3, W2 * 0.6, H2 * 0.75], fill = (250, 170, 60))
		d.ellipse ([W2 * 0.42, H2 * 0.42, W2 * 0.47, H2 * 0.5], fill = WHITE); d.ellipse ([W2 * 0.5, H2 * 0.42, W2 * 0.55, H2 * 0.5], fill = WHITE)
		d.ellipse ([W2 * 0.435, H2 * 0.445, W2 * 0.46, H2 * 0.48], fill = (20, 20, 20)); d.ellipse ([W2 * 0.515, H2 * 0.445, W2 * 0.54, H2 * 0.48], fill = (20, 20, 20))
	elif i % 6 == 4:
		for k in range (6): d.polygon ([(W2 * (0.1 + k * 0.16), 0), (W2 * (0.05 + k * 0.16), H2 * 0.7), (W2 * (0.2 + k * 0.16), H2 * 0.7)], fill = (250, 100 + k * 25, 200, 70))
		for k in range (40): px = r.random () * W2; d.ellipse ([px, H2 * 0.8 + r.random () * H2 * 0.15, px + 14, H2 + 20], fill = (10, 6, 16))
	else:
		for k in range (5): d.ellipse ([r.random () * W2, H2 * 0.5 + r.random () * H2 * 0.3, r.random () * W2 + 60, H2], fill = (60, 110, 70))
	return im.resize ((w * K, h * K), Image.LANCZOS)

def thumb (c, x, y, w, h, i, dur, watched = 0.0):
	c.rect (x + 2, y + 3, w, h, M.A ((0, 0, 0), 45), r = 6)
	im = frame_img (i, w, h)
	m = Image.new ("L", im.size, 0); ImageDraw.Draw (m).rounded_rectangle ([0, 0, im.size[0] - 1, im.size[1] - 1], 6 * K, fill = 255)
	c.img.paste (im, (int (x * K), int (y * K)), m); c.d = ImageDraw.Draw (c.img, "RGBA")
	dw = c.tw (dur, "smallb") + 10
	c.rect (x + w - dw - 6, y + h - 22, dw, 16, M.A ((0, 0, 0), 170), r = 4); c.text_c (x + w - dw - 6, y + h - 22, dw, 16, dur, "smallb", WHITE)
	if watched:
		c.rect (x + 6, y + h - 5, w - 12, 3, M.A (WHITE, 90), r = 1); c.rect (x + 6, y + h - 5, (w - 12) * watched, 3, (230, 70, 60), r = 1)

def row_head (c, x, y, w, title, more = "See all"):
	c.text (x, y, title, "big"); c.text_r (x + w, y + 9, 0, more + "  ›", "ui", M.LINK)

def shot_home ():
	c = screen ()
	cx, cy, cw, ch = app_window (c)
	topbar (c, cx, cy, cw, ["Home"])
	body_h = ch - TOP_H - NOW_H
	sidebar (c, cx, cy + TOP_H + 1, body_h - 1, "Home")
	ax, ay, aw = cx + SIDE_W + 24, cy + TOP_H + 14, cw - SIDE_W - 48
	c.text (ax, ay, "Good evening", "huge")
	c.text (ax + c.tw ("Good evening", "huge") + 14, ay + 10, "128 songs  ·  23 videos  ·  5 playlists", "ui", DIM)
	# go on: the film left half way, the album playing
	y = ay + 46
	c.rect (ax, y, aw / 2 - 8, 76, WHITE, r = 8, outline = LINE2)
	thumb (c, ax + 8, y + 8, 107, 60, 2, "1:42:10", 0.45)
	c.text (ax + 126, y + 12, "Sunset Harbour", "uib"); c.text (ax + 126, y + 31, "Film  ·  56 min left", "small", DIM)
	M.button (c, ax + 126, y + 46, 96, 24, "", accent = True); ic_play (c, ax + 132, y + 49, 16, WHITE); c.text_l (ax + 150, y + 46, 24, "Resume", "smallb", WHITE)
	x2 = ax + aw / 2 + 8
	c.rect (x2, y, aw / 2 - 8, 76, WHITE, r = 8, outline = LINE2)
	cover (c, x2 + 8, y + 8, 60, 0, r = 5, shadow = False)
	c.text (x2 + 80, y + 12, "Northern Lights", "uib"); c.text (x2 + 80, y + 31, "Lumen Drift  ·  song 3 of 11", "small", DIM)
	ic_eq (c, x2 + 80, y + 50, 14, SEL); c.text (x2 + 100, y + 50, "Playing", "smallb", SEL)
	# the music played lately
	y += 92
	row_head (c, ax, y, aw, "Recently played")
	S, G = 100, 16
	for k, i in enumerate ((0, 3, 6, 1, 7, 5)):
		x = ax + k * (S + G)
		if x + S > ax + aw: break
		cover (c, x, y + 30, S, i)
		t, a = ALBUMS[i][0], ALBUMS[i][1]
		c.text (x, y + 30 + S + 6, t if c.tw (t, "smallb") < S else t[:12] + "…", "smallb"); c.text (x, y + 30 + S + 21, a if c.tw (a, "small") < S else a[:14] + "…", "small", DIM)
	# the videos
	y += 30 + S + 44
	row_head (c, ax, y, aw, "Videos")
	VW, VH = 165, 93
	vids = [("Sunset Harbour", "Film", "1:42:10", 2, 0.45), ("Mountain Trails", "Clip", "12:31", 0, 0), ("City Nights", "Series  ·  S1 E3", "24:05", 1, 1.0),
		("Foxy & Friends", "Cartoon", "7:48", 3, 0.2), ("Live at the Arena", "Concert", "58:12", 4, 0)]
	for k, (t, kind, dur, i, wt) in enumerate (vids):
		x = ax + k * (VW + 14)
		if x + VW > ax + aw: break
		thumb (c, x, y + 30, VW, VH, i, dur, wt)
		if k == 1:
			c.rect (x, y + 30, VW, VH, M.A ((0, 0, 0), 60), r = 6)
			c.ellipse (x + VW / 2, y + 30 + VH / 2, 20, M.A (WHITE, 230)); ic_play (c, x + VW / 2 - 9, y + 30 + VH / 2 - 11, 22, TEXT)
			M.cursor (c, x + VW / 2 + 6, y + 30 + VH / 2 + 8)
		c.text (x, y + 30 + VH + 6, t, "smallb"); c.text (x, y + 30 + VH + 21, kind, "small", DIM)
	nowbar (c, cx, cy + ch - NOW_H, cw)
	c.save ("media-home.png")

if __name__ == "__main__":
	shot_home (); shot_albums (); shot_album (); shot_songs (); shot_nowplaying (); shot_midi (); shot_mini (); shot_welcome ()
