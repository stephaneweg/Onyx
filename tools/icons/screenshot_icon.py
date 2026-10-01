#!/usr/bin/env python3
"""tools/icons/screenshot_icon.py -- Screenshot's dock / launcher icon (sdcard/apps/screenshot.app/icon.bmp):
a blue tile, a picture on it framed by a dashed selection with its corners, the pen's red stroke across --
drawn at 4 times the size (anti-aliased), brought down to 40 x 40; the pixels more than half covered kept,
the others magenta (the icons' see-through key).

    python3 tools/icons/screenshot_icon.py
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
	# the tile: a vertical gradient, rounded, a darker rim
	tile = Image.new ("RGBA", (S, S), (0, 0, 0, 0))
	g = ImageDraw.Draw (tile)
	for y in range (S):
		t = y / (S - 1)
		c = tuple (int (a + (b - a) * t) for a, b in zip ((92, 168, 220), (40, 98, 170)))
		g.line ([(0, y), (S, y)], fill = c + (255,))
	mask = Image.new ("L", (S, S), 0)
	ImageDraw.Draw (mask).rounded_rectangle ([6, 6, S - 7, S - 7], 30, fill = 255)
	img.paste (tile, (0, 0), mask)
	d.rounded_rectangle ([6, 6, S - 7, S - 7], 30, outline = (24, 64, 120, 255), width = 4)
	# the picture inside: a sky and a hill (what was captured)
	px0, py0, px1, py1 = 36, 40, 124, 112
	d.rectangle ([px0, py0, px1, py1], fill = (236, 244, 250, 255))
	d.polygon ([(px0, py1), (px0, 92), (66, 74), (92, 96), (108, 84), (px1, 98), (px1, py1)], fill = (82, 168, 98, 255))
	d.ellipse ([98, 50, 114, 66], fill = (250, 200, 60, 255))
	# the selection: dashed white, its corners solid
	for x in range (px0 - 10, px1 + 10, 14):
		d.rectangle ([x, py0 - 12, min (x + 7, px1 + 10), py0 - 8], fill = (255, 255, 255, 255))
		d.rectangle ([x, py1 + 8, min (x + 7, px1 + 10), py1 + 12], fill = (255, 255, 255, 255))
	for y in range (py0 - 10, py1 + 10, 14):
		d.rectangle ([px0 - 12, y, px0 - 8, min (y + 7, py1 + 10)], fill = (255, 255, 255, 255))
		d.rectangle ([px1 + 8, y, px1 + 12, min (y + 7, py1 + 10)], fill = (255, 255, 255, 255))
	for cx, cy, sx, sy in ((px0 - 12, py0 - 12, 1, 1), (px1 + 12, py0 - 12, -1, 1), (px0 - 12, py1 + 12, 1, -1), (px1 + 12, py1 + 12, -1, -1)):
		d.rectangle ([min (cx, cx + 20 * sx), min (cy, cy + 6 * sy), max (cx, cx + 20 * sx), max (cy, cy + 6 * sy)], fill = (255, 255, 255, 255))
		d.rectangle ([min (cx, cx + 6 * sx), min (cy, cy + 20 * sy), max (cx, cx + 6 * sx), max (cy, cy + 20 * sy)], fill = (255, 255, 255, 255))
	# the pen's stroke
	d.line ([(46, 126), (70, 118), (96, 124), (122, 112)], fill = (226, 58, 48, 255), width = 9, joint = "curve")
	for x, y in ((46, 126), (122, 112)): d.ellipse ([x - 4, y - 4, x + 4, y + 4], fill = (226, 58, 48, 255))
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
	out = os.path.join (gen_assets.APPS, "screenshot.app", "icon.bmp")
	os.makedirs (os.path.dirname (out), exist_ok = True)
	gen_assets.write_bmp (out, N, N, px)
	print ("wrote", out)

if __name__ == "__main__":
	main ()
