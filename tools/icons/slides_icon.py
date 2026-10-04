#!/usr/bin/env python3
"""tools/icons/slides_icon.py -- Slides' dock / launcher icon (sdcard/apps/slides.app/icon.bmp): a screen on its
stand, the slide on it (a teal title, three bars, a peach disc) -- drawn at 4 times the size, brought down to
40 x 40; the pixels more than half covered kept, the others magenta (the icons' see-through key).

    python3 tools/icons/slides_icon.py
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
	# the stand
	d.line ([(80, 112), (80, 140)], fill = (96, 88, 84, 255), width = 10)
	d.line ([(80, 132), (52, 154)], fill = (96, 88, 84, 255), width = 9)
	d.line ([(80, 132), (108, 154)], fill = (96, 88, 84, 255), width = 9)
	# the screen: its frame, the slide
	d.rounded_rectangle ([8, 14, 152, 116], 8, fill = (70, 64, 62, 255))
	d.rectangle ([16, 22, 144, 108], fill = (252, 250, 247, 255))
	d.rectangle ([16, 22, 22, 108], fill = (46, 110, 128, 255))
	d.rounded_rectangle ([32, 32, 104, 44], 4, fill = (24, 64, 78, 255))
	d.rounded_rectangle ([32, 50, 70, 56], 3, fill = (240, 168, 110, 255))
	for k, h in enumerate ((22, 34, 28)):
		x = 34 + k * 20
		d.rectangle ([x, 100 - h, x + 13, 100], fill = (46, 110, 128, 255) if k != 1 else (240, 168, 110, 255))
	d.ellipse ([104, 64, 136, 96], fill = (240, 168, 110, 255))
	d.pieslice ([104, 64, 136, 96], -90, 30, fill = (46, 110, 128, 255))
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
	out = os.path.join (gen_assets.APPS, "slides.app", "icon.bmp")
	os.makedirs (os.path.dirname (out), exist_ok = True)
	gen_assets.write_bmp (out, N, N, px)
	print ("wrote", out)

if __name__ == "__main__":
	main ()
