#!/usr/bin/env python3
"""tools/icons/media_icon.py -- Media Player's dock / launcher icon (sdcard/apps/media.app/icon.bmp): an orange-pink
tile, a white disc with its label in the accent, a note over it -- drawn at 4 times the size, brought down to
40 x 40; the pixels more than half covered kept, the others magenta (the icons' see-through key).

    python3 tools/icons/media_icon.py
"""
import os, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, os.path.dirname (HERE))
import gen_assets

S, N = 160, 40

def draw ():
	img = Image.new ("RGBA", (S, S), (0, 0, 0, 0))
	tile = Image.new ("RGBA", (S, S))
	g = ImageDraw.Draw (tile)
	for y in range (S):
		t = y / (S - 1)
		g.line ([(0, y), (S, y)], fill = tuple (int (a + (b - a) * t) for a, b in zip ((250, 120, 90), (210, 60, 130))) + (255,))
	m = Image.new ("L", (S, S), 0); ImageDraw.Draw (m).rounded_rectangle ([6, 6, S - 7, S - 7], 30, fill = 255)
	img.paste (tile, (0, 0), m)
	d = ImageDraw.Draw (img)
	d.rounded_rectangle ([6, 6, S - 7, S - 7], 30, outline = (140, 40, 80, 255), width = 4)
	# the disc
	cx, cy, r = 70, 88, 50
	d.ellipse ([cx - r, cy - r, cx + r, cy + r], fill = (250, 246, 240, 255))
	for rr in (42, 34): d.ellipse ([cx - rr, cy - rr, cx + rr, cy + rr], outline = (225, 215, 205, 255), width = 2)
	d.ellipse ([cx - 18, cy - 18, cx + 18, cy + 18], fill = (73, 146, 167, 255))
	d.ellipse ([cx - 5, cy - 5, cx + 5, cy + 5], fill = (250, 246, 240, 255))
	# the note
	d.ellipse ([96, 100, 128, 124], fill = (255, 255, 255, 255))
	d.rectangle ([120, 30, 128, 112], fill = (255, 255, 255, 255))
	d.polygon ([(120, 30), (146, 40), (146, 58), (128, 50)], fill = (255, 255, 255, 255))
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
	out = os.path.join (gen_assets.APPS, "media.app", "icon.bmp")
	os.makedirs (os.path.dirname (out), exist_ok = True)
	gen_assets.write_bmp (out, N, N, px)
	print ("wrote", out)

if __name__ == "__main__":
	main ()
