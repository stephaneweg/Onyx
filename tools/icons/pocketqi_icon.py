#!/usr/bin/env python3
"""tools/icons/pocketqi_icon.py -- PocketQI's icon: sdcard/apps/pocketqi.app/icon.bmp, Pocket's face -- a dark rounded
screen with its two glowing blue eyes, smiling (the lower lids' arc: PocketQI's "joyeux"), and a small speech bubble
at its top right. Drawn at 4 times the size, brought down to 40 x 40; the pixels more than half covered kept, the
others magenta (the icons' see-through key) -- as clock_icon.py.

    python3 tools/icons/pocketqi_icon.py

MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
"""
import os, sys
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, os.path.dirname (HERE))
import gen_assets

S, N = 160, 40
EYE = (56, 214, 245, 255)

def face ():
	img = Image.new ("RGBA", (S, S), (0, 0, 0, 0))
	d = ImageDraw.Draw (img)
	d.rounded_rectangle ([6, 22, 150, 150], 30, fill = (18, 24, 34, 255), outline = (70, 84, 104, 255), width = 4)
	d.rounded_rectangle ([14, 30, 142, 142], 24, fill = (6, 9, 14, 255))
	# the eyes' glow, then the eyes: rounded, the lower lid an arc (a smile)
	glow = Image.new ("RGBA", (S, S), (0, 0, 0, 0))
	g = ImageDraw.Draw (glow)
	for x0 in (34, 88):
		g.rounded_rectangle ([x0 - 6, 52, x0 + 40, 118], 22, fill = (56, 214, 245, 110))
	glow = glow.filter (ImageFilter.GaussianBlur (7))
	img.alpha_composite (glow)
	d = ImageDraw.Draw (img)
	for x0 in (34, 88):
		d.rounded_rectangle ([x0, 58, x0 + 34, 112], 16, fill = EYE)
		d.ellipse ([x0 - 12, 86, x0 + 46, 150], fill = (6, 9, 14, 255))		# the lower lid: the smile
	# the speech bubble, top right
	d.ellipse ([108, 2, 158, 40], fill = (255, 255, 255, 255), outline = (46, 110, 190, 255), width = 4)
	d.polygon ([(116, 30), (110, 46), (130, 36)], fill = (255, 255, 255, 255))
	for k, x in enumerate ((122, 133, 144)):
		d.ellipse ([x - 4, 17, x + 4, 25], fill = (46, 110, 190, 255))
	return img

def save (img, app):
	img = img.resize ((N, N), Image.LANCZOS)
	px = []
	for y in range (N):
		for x in range (N):
			r, g, b, a = img.getpixel ((x, y))
			if a < 128: px.append ((255, 0, 255)); continue
			k = 255 / a
			c = (min (255, int (r * k)), min (255, int (g * k)), min (255, int (b * k)))
			px.append (c if c != (255, 0, 255) else (255, 0, 254))
	out = os.path.join (gen_assets.APPS, app + ".app", "icon.bmp")
	os.makedirs (os.path.dirname (out), exist_ok = True)
	gen_assets.write_bmp (out, N, N, px)
	print ("wrote", out)

if __name__ == "__main__":
	save (face (), "pocketqi")
