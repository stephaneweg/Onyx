#!/usr/bin/env python3
"""tools/icons/ndsemu_icon.py -- the Nintendo DS emulator's icon: sdcard/apps/ndsemu.app/icon.bmp, a DS open --
its two halves one above the other, a light screen in each (the bottom one with a d-pad on its left and four
buttons on its right), the hinge between. Drawn at 4 times the size, brought down to 40 x 40; the pixels more
than half covered kept, the others magenta (the icons' see-through key) -- as clock_icon.py.

    python3 tools/icons/ndsemu_icon.py

MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
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
	body, edge, screen, frame = (196, 200, 210, 255), (120, 126, 140, 255), (126, 196, 238, 255), (44, 48, 60, 255)
	# the top half and the bottom half
	d.rounded_rectangle ([22, 4, 138, 76], 12, fill = edge)
	d.rounded_rectangle ([26, 8, 134, 72], 10, fill = body)
	d.rounded_rectangle ([6, 84, 154, 156], 14, fill = edge)
	d.rounded_rectangle ([10, 88, 150, 152], 12, fill = body)
	d.rectangle ([30, 74, 130, 86], fill = (90, 96, 110, 255))			# the hinge
	# the screens
	d.rectangle ([44, 16, 116, 64], fill = frame); d.rectangle ([48, 20, 112, 60], fill = screen)
	d.rectangle ([44, 96, 116, 144], fill = frame); d.rectangle ([48, 100, 112, 140], fill = screen)
	d.polygon ([(56, 44), (72, 30), (80, 38), (96, 26), (104, 34), (104, 56), (56, 56)], fill = (70, 150, 90, 255))	# a hill on top
	# the d-pad, the buttons
	d.rectangle ([22, 112, 32, 136], fill = frame); d.rectangle ([15, 119, 39, 129], fill = frame)
	for (x, y) in ((136, 112), (128, 124), (144, 124), (136, 136)):
		d.ellipse ([x - 5, y - 5, x + 5, y + 5], fill = frame)
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
	save (draw (), "ndsemu")
