#!/usr/bin/env python3
"""tools/icons/telegram_icon.py -- the icon of Telegram for Onyx: sdcard/apps/telegram.app/icon.bmp, a sky-blue
speech bubble and, in front of it, two buddies (a blue one behind, a green one in front: the old messenger's
contact figures, drawn here). Drawn at 4 times the size, brought down to 40 x 40; the pixels more than half
covered kept, the others magenta (the icons' see-through key) -- as notes_icon.py.

    python3 tools/icons/telegram_icon.py

MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
"""
import os, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, os.path.dirname (HERE))
import gen_assets

S, N = 160, 40

def buddy (d, x, y, s, body, dark, light):
	# a head and a body, outlined, a gloss on the head
	d.ellipse ([x + s * 0.27, y, x + s * 0.73, y + s * 0.46], fill = dark)
	d.ellipse ([x, y + s * 0.42, x + s, y + s * 1.05], fill = dark)
	g = s * 0.05
	d.ellipse ([x + s * 0.27 + g, y + g, x + s * 0.73 - g, y + s * 0.46 - g], fill = body)
	d.ellipse ([x + g, y + s * 0.42 + g, x + s - g, y + s * 1.05 - g], fill = body)
	d.ellipse ([x + s * 0.34, y + s * 0.06, x + s * 0.52, y + s * 0.18], fill = light)

def telegram ():
	img = Image.new ("RGBA", (S, S), (0, 0, 0, 0))
	d = ImageDraw.Draw (img)
	# the speech bubble, top right
	d.rounded_rectangle ([46, 8, 154, 84], 22, fill = (30, 96, 170, 255))
	d.rounded_rectangle ([50, 12, 150, 80], 19, fill = (88, 160, 230, 255))
	d.rounded_rectangle ([54, 14, 146, 44], 15, fill = (150, 200, 245, 255))
	d.polygon ([(118, 78), (140, 78), (142, 100)], fill = (30, 96, 170, 255))
	d.polygon ([(122, 76), (136, 76), (138, 94)], fill = (88, 160, 230, 255))
	for k in range (3):
		x = 76 + k * 22
		d.ellipse ([x, 40, x + 12, 52], fill = (255, 255, 255, 255))
	# the buddies
	buddy (d, 44, 62, 70, (58, 142, 230, 255), (22, 80, 150, 255), (190, 225, 255, 255))
	buddy (d, 6, 70, 84, (88, 184, 48, 255), (40, 110, 20, 255), (210, 245, 180, 255))
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
	save (telegram (), "telegram")
