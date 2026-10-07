#!/usr/bin/env python3
"""tools/icons/gpiolab_icon.py -- GPIO Lab's dock / launcher icon (sdcard/apps/gpiolab.app/icon.bmp): a green
circuit board, the header's two rows of gold pins along it, a red LED lit by one of them through a wire -- drawn
at 4 times the size (anti-aliased), brought down to 40 x 40; the pixels more than half covered kept, the others
magenta (the icons' see-through key).

    python3 tools/icons/gpiolab_icon.py

MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
"""
import os, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, os.path.dirname (HERE))
import gen_assets

S, N = 160, 40

def draw ():
	img = Image.new ("RGBA", (S, S), (0, 0, 0, 0))
	d = ImageDraw.Draw (img)
	# the board: a green tile, a darker rim
	tile = Image.new ("RGBA", (S, S), (0, 0, 0, 0))
	g = ImageDraw.Draw (tile)
	for y in range (S):
		t = y / (S - 1)
		c = tuple (int (a + (b - a) * t) for a, b in zip ((54, 150, 96), (24, 92, 56)))
		g.line ([(0, y), (S, y)], fill = c + (255,))
	mask = Image.new ("L", (S, S), 0)
	ImageDraw.Draw (mask).rounded_rectangle ([6, 6, S - 7, S - 7], 30, fill = 255)
	img.paste (tile, (0, 0), mask)
	d.rounded_rectangle ([6, 6, S - 7, S - 7], 30, outline = (14, 58, 34, 255), width = 4)
	# the header: a black strip, two rows of gold pins
	d.rounded_rectangle ([26, 22, 70, 138], 6, fill = (24, 28, 26, 255))
	for i in range (5):
		for x in (38, 58):
			y = 36 + i * 22
			d.ellipse ([x - 7, y - 7, x + 7, y + 7], fill = (226, 196, 110, 255))
			d.ellipse ([x - 3, y - 3, x + 3, y + 3], fill = (150, 120, 50, 255))
	# a trace from a pin to the LED, and the LED lit
	d.line ([(58, 58), (96, 58), (112, 80)], fill = (220, 236, 200, 255), width = 6, joint = "curve")
	d.ellipse ([98, 74, 138, 114], fill = (255, 120, 100, 120))
	d.ellipse ([104, 80, 132, 108], fill = (236, 52, 44, 255))
	d.ellipse ([110, 84, 120, 94], fill = (255, 200, 190, 255))
	return img

def main ():
	img = draw ().resize ((N, N), Image.LANCZOS)
	px = []
	for y in range (N):
		for x in range (N):
			r, g, b, a = img.getpixel ((x, y))
			if a < 128: px.append ((255, 0, 255)); continue
			k = 255 / a
			c = (min (255, int (r * k)), min (255, int (g * k)), min (255, int (b * k)))
			px.append (c if c != (255, 0, 255) else (255, 0, 254))
	out = os.path.join (gen_assets.APPS, "gpiolab.app", "icon.bmp")
	os.makedirs (os.path.dirname (out), exist_ok = True)
	gen_assets.write_bmp (out, N, N, px)
	print ("wrote", out)

if __name__ == "__main__":
	main ()
