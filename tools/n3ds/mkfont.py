#!/usr/bin/env python3
# MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors
"""mkfont.py -- makes the 3DS emulator's own "shared system font": a BCFNT (the console's font format) drawn from a
free font, DejaVu Sans (third_party/dejavu-fonts-ttf-2.37). The console's font is Nintendo's and is not used; the
programs that draw text with the system font (most homebrew, many games) read this one instead, through
APT's GetSharedFont. Same metrics as the console's: cells of 24 x 30 pixels, sheets of 128 x 32 (5 glyphs each)
in the GPU's A4 format (4 bits of alpha a pixel, 8 x 8 tiles in Z order).

    python3 tools/n3ds/mkfont.py [out.bcfnt]		(default: user/Emulators/n3ds/data/sysfont.bcfnt)

Needs Pillow. The file holds offsets from its start (the plain BCFNT form); the core turns them into addresses
when it puts the font in the machine's memory (n3ds_apt.cpp)."""
import os, struct, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname (os.path.dirname (os.path.dirname (os.path.abspath (__file__))))
TTF = os.path.join (ROOT, "third_party", "dejavu-fonts-ttf-2.37", "ttf", "DejaVuSans.ttf")
TTF_BOLD = os.path.join (ROOT, "third_party", "dejavu-fonts-ttf-2.37", "ttf", "DejaVuSans-Bold.ttf")
CELL_W, CELL_H, SHEET_W, SHEET_H, PER_SHEET = 24, 30, 128, 32, 5
PIXELS = 23					# the font's size: its line fits the cell
BASELINE = 24					# pixels from the cell's top to the baseline

# what is drawn: ASCII, Latin-1, Latin Extended-A, common punctuation and symbols, arrows
CODES = list (range (0x20, 0x7F)) + list (range (0xA0, 0x180)) + [
	0x2013, 0x2014, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D, 0x201E, 0x2020, 0x2021, 0x2022, 0x2026, 0x2030, 0x2039, 0x203A,
	0x20AC, 0x2122, 0x2190, 0x2191, 0x2192, 0x2193, 0x2194, 0x21B5, 0x2212, 0x221E, 0x2248, 0x2260, 0x2264, 0x2265,
	0x25A0, 0x25A1, 0x25B2, 0x25B6, 0x25BC, 0x25C0, 0x25CB, 0x25CF, 0x2605, 0x2606, 0x2665, 0x266A, 0x2713, 0x2717 ]
# the console's own symbols (private use): the buttons, as a letter in a disc
BUTTONS = { 0xE000: "A", 0xE001: "B", 0xE002: "X", 0xE003: "Y", 0xE004: "L", 0xE005: "R", 0xE006: "+" }

XLUT = [0, 1, 4, 5, 16, 17, 20, 21]
YLUT = [0, 2, 8, 10, 32, 34, 40, 42]

def tiled_a4 (img):
	"""A sheet (mode L, 128 x 32, the top row first) -> the GPU's A4 bytes."""
	w, h = img.size
	px = img.load ()
	out = bytearray (w * h // 2)
	for y in range (h):
		for x in range (w):
			n = XLUT[x & 7] + YLUT[y & 7] + (x & ~7) * 8 + (y & ~7) * w
			v = px[x, y] >> 4
			if n & 1: out[n >> 1] |= v << 4
			else: out[n >> 1] |= v
	return bytes (out)

def glyph (font, bold, code):
	"""-> (a CELL_W x CELL_H image, left, glyph width, advance)"""
	cell = Image.new ("L", (CELL_W, CELL_H), 0)
	d = ImageDraw.Draw (cell)
	if code in BUTTONS:
		d.ellipse ((1, BASELINE - 19, 21, BASELINE + 1), fill = 255)
		t = BUTTONS[code]
		box = bold.getbbox (t)
		d.text ((11 - (box[0] + box[2]) / 2, BASELINE - 9 - (box[1] + box[3]) / 2), t, font = bold, fill = 0)
		return cell, 0, 22, 23
	ch = chr (code)
	adv = int (round (font.getlength (ch)))
	box = font.getbbox (ch)				# (left, top, right, bottom) from the origin at the ascender
	if not box or box[2] <= box[0]: return cell, 0, 0, min (adv, CELL_W)
	left = box[0]
	width = min (box[2] - box[0], CELL_W)
	ascent = font.getmetrics ()[0]
	d.text ((-left, BASELINE - ascent), ch, font = font, fill = 255)
	return cell, max (-128, min (127, left)), width, min (adv, 255)

def main ():
	out = sys.argv[1] if len (sys.argv) > 1 else os.path.join (ROOT, "user", "Emulators", "n3ds", "data", "sysfont.bcfnt")
	font = ImageFont.truetype (TTF, PIXELS)
	bold = ImageFont.truetype (TTF_BOLD, 15)
	codes = [c for c in CODES if c == 0x20 or c == 0xA0 or font.getmask (chr (c)).getbbox ()] + sorted (BUTTONS)
	glyphs = [glyph (font, bold, c) for c in codes]
	n = len (glyphs)
	sheets = []
	for s in range ((n + PER_SHEET - 1) // PER_SHEET):
		img = Image.new ("L", (SHEET_W, SHEET_H), 0)
		for k in range (PER_SHEET):
			i = s * PER_SHEET + k
			if i < n: img.paste (glyphs[i][0], (k * (CELL_W + 1) + 1, 1))
		sheets.append (tiled_a4 (img))
	sheet_size = SHEET_W * SHEET_H // 2
	alter = codes.index (0x3F)				# '?': what an unknown character shows

	# the blocks, each: its 4 letters, its size, its body. Offsets (to a body) are from the file's start.
	HEADER, FINF = 0x14, 0x20
	tglp_at = HEADER + FINF
	tglp_body = 0x20
	sheets_at = (tglp_at + 8 + tglp_body + 0x7F) & ~0x7F
	tglp_size = sheets_at - tglp_at + len (sheets) * sheet_size
	cwdh_at = tglp_at + tglp_size
	cwdh_size = (8 + 8 + 3 * n + 3) & ~3
	cmap_at = cwdh_at + cwdh_size
	ascii_n = 0x7F - 0x20
	rest = [(c, i) for i, c in enumerate (codes) if i >= ascii_n]
	cmap1_size = 8 + 12 + 4
	cmap2_size = (8 + 12 + 2 + 4 * len (rest) + 3) & ~3
	total = cmap_at + cmap1_size + cmap2_size

	b = bytearray ()
	b += struct.pack ("<4sHHIII", b"CFNT", 0xFEFF, HEADER, 0x03000000, total, 5)
	b += struct.pack ("<4sIBBHbBBBIIIBBBB", b"FINF", FINF, 1, CELL_H, alter, 0, CELL_W, CELL_W, 1,
			  tglp_at + 8, cwdh_at + 8, cmap_at + 8, CELL_H, CELL_W, BASELINE, 0)
	b += struct.pack ("<4sIBBBBIHHHHHHI", b"TGLP", tglp_size, CELL_W, CELL_H, BASELINE, CELL_W, sheet_size, len (sheets), 0xB,
			  PER_SHEET, 1, SHEET_W, SHEET_H, sheets_at)
	b += bytes (sheets_at - len (b))
	for s in sheets: b += s
	assert len (b) == cwdh_at
	b += struct.pack ("<4sIHHI", b"CWDH", cwdh_size, 0, n - 1, 0)
	for (_, left, width, adv) in glyphs: b += struct.pack ("<bBB", left, width, adv)
	b += bytes (cmap_at - len (b))
	b += struct.pack ("<4sIHHHHIHH", b"CMAP", cmap1_size, 0x20, 0x7E, 0, 0, cmap_at + cmap1_size + 8, 0, 0)	# direct: glyph = code - 0x20
	b += struct.pack ("<4sIHHHHIH", b"CMAP", cmap2_size, 0x0000, 0xFFFF, 2, 0, 0, len (rest))			# scan: (code, glyph) pairs, sorted
	for c, i in sorted (rest): b += struct.pack ("<HH", c, i)
	b += bytes (total - len (b))
	os.makedirs (os.path.dirname (out), exist_ok = True)
	open (out, "wb").write (b)
	print ("%s: %d glyphs, %d sheets, %d bytes" % (out, n, len (sheets), len (b)))

if __name__ == "__main__":
	main ()
