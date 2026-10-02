#!/usr/bin/env python3
"""tools/icons/photos_icon.py -- Photos' dock / launcher icon (sdcard/apps/photos.app/icon.bmp): two prints fanned out,
the front one a landscape (a sky, a sun, hills) in a white border -- drawn at 4 times the size, brought down to 40 x 40;
the pixels more than half covered kept, the others magenta (the icons' see-through key).

    python3 tools/icons/photos_icon.py
"""
import os, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, os.path.dirname (HERE))
import gen_assets

S, N = 160, 40

def print_ (w, h, angle, landscape):
	p = Image.new ("RGBA", (w, h), (0, 0, 0, 0)); d = ImageDraw.Draw (p)
	d.rounded_rectangle ([0, 0, w - 1, h - 1], 8, fill = (252, 251, 248, 255), outline = (120, 112, 106, 255), width = 4)
	x0, y0, x1, y1 = 10, 10, w - 11, h - 26
	if landscape:
		for y in range (y0, y1):
			t = (y - y0) / (y1 - y0); d.line ([(x0, y), (x1, y)], (int (92 + 100 * t), int (152 + 70 * t), int (220 + 25 * t), 255))
		d.ellipse ([x1 - 40, y0 + 10, x1 - 14, y0 + 36], fill = (250, 196, 70, 255))
		d.polygon ([(x0, y1), (x0, y1 - 26), (x0 + 30, y1 - 52), (x0 + 62, y1 - 22), (x0 + 84, y1 - 40), (x1, y1 - 14), (x1, y1)], fill = (66, 140, 84, 255))
		d.polygon ([(x0, y1), (x0, y1 - 12), (x0 + 50, y1 - 26), (x1, y1 - 6), (x1, y1)], fill = (46, 112, 64, 255))
	else:
		d.rectangle ([x0, y0, x1, y1], fill = (224, 150, 120, 255))
	return p.rotate (angle, resample = Image.BICUBIC, expand = True)

def draw ():
	img = Image.new ("RGBA", (S, S), (0, 0, 0, 0))
	back = print_ (112, 124, 14, False); img.alpha_composite (back, (4, 6))
	front = print_ (120, 132, -8, True); img.alpha_composite (front, (S - front.width - 2, S - front.height - 2))
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
	out = os.path.join (gen_assets.APPS, "photos.app", "icon.bmp")
	os.makedirs (os.path.dirname (out), exist_ok = True)
	gen_assets.write_bmp (out, N, N, px)
	print ("wrote", out)

if __name__ == "__main__":
	main ()
