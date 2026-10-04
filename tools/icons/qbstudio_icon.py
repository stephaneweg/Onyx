#!/usr/bin/env python3
"""tools/icons/qbstudio_icon.py -- QBStudio's dock / launcher icon (sdcard/apps/qbstudio.app/icon.bmp): a window being
designed (its title bar, two rows of controls in their blue layout boxes, a button chosen) and BASIC's badge -- drawn at
4 times the size, brought down to 40 x 40; the pixels more than half covered kept, the others magenta (the see-through key).

    python3 tools/icons/qbstudio_icon.py
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, os.path.dirname (HERE))
import gen_assets

S, N = 160, 40
BLUE = (60, 140, 216, 255)

def draw ():
	img = Image.new ("RGBA", (S, S), (0, 0, 0, 0))
	d = ImageDraw.Draw (img)
	# the window
	d.rounded_rectangle ([6, 10, 150, 136], 10, fill = (88, 80, 76, 255))
	d.rounded_rectangle ([10, 14, 146, 132], 8, fill = (238, 233, 226, 255))
	d.rounded_rectangle ([10, 14, 146, 40], 8, fill = (236, 156, 104, 255))
	d.rectangle ([10, 30, 146, 40], fill = (236, 156, 104, 255))
	# its layout: two rows (a label, a field), a button chosen
	for y in (50, 76):
		d.rectangle ([18, y, 138, y + 20], outline = BLUE, width = 3)
		d.rectangle ([24, y + 7, 48, y + 13], fill = (90, 84, 80, 255))
		d.rectangle ([56, y + 4, 132, y + 16], fill = (255, 255, 255, 255), outline = (150, 144, 140, 255), width = 2)
	d.rounded_rectangle ([84, 104, 136, 124], 5, fill = (214, 208, 200, 255), outline = (120, 114, 110, 255), width = 2)
	for x, y in ((84, 104), (136, 104), (84, 124), (136, 124)):
		d.rectangle ([x - 5, y - 5, x + 5, y + 5], fill = (255, 255, 255, 255), outline = BLUE, width = 3)
	# BASIC's badge
	d.rounded_rectangle ([96, 98, 156, 156], 12, fill = (104, 70, 190, 255))
	try: f = ImageFont.truetype (os.path.join (gen_assets.ROOT, "sdcard", "res", "fonts", "DejaVuSans-Bold.ttf"), 42)
	except Exception: f = ImageFont.load_default ()
	d.text ((126, 128), "B", font = f, fill = (255, 255, 255, 255), anchor = "mm")
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
	out = os.path.join (gen_assets.APPS, "qbstudio.app", "icon.bmp")
	os.makedirs (os.path.dirname (out), exist_ok = True)
	gen_assets.write_bmp (out, N, N, px)
	print ("wrote", out)

if __name__ == "__main__":
	main ()
