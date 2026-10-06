#!/usr/bin/env python3
"""tools/icons/notes_icon.py -- the icons of Notes and Stickies (AutoDev round 1, 04-ux-design.md section 9):
sdcard/apps/notes.app/icon.bmp, a square yellow note pad (its darker band on top, as Stickies' cards), three
ruled lines, a pencil across its lower right corner; sdcard/apps/stickies.app/icon.bmp, a yellow sticky note,
its corner turned, a red push pin at its top. Drawn at 4 times the size, brought down to 40 x 40; the pixels
more than half covered kept, the others magenta (the icons' see-through key) -- as slides_icon.py.

    python3 tools/icons/notes_icon.py

MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
"""
import os, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, os.path.dirname (HERE))
import gen_assets

S, N = 160, 40
PAPER, BAND, INK, PIN = (252, 233, 166, 255), (232, 178, 31, 255), (43, 41, 37, 255), (217, 102, 122, 255)

def notes ():
	img = Image.new ("RGBA", (S, S), (0, 0, 0, 0))
	d = ImageDraw.Draw (img)
	d.rounded_rectangle ([14, 12, 146, 148], 12, fill = (150, 120, 40, 255))		# the pad's edge
	d.rounded_rectangle ([18, 16, 142, 144], 9, fill = PAPER)
	d.rounded_rectangle ([18, 16, 142, 42], 9, fill = BAND)
	d.rectangle ([18, 32, 142, 42], fill = BAND)
	for k in range (4):									# the binding's rings
		x = 36 + k * 28
		d.ellipse ([x, 8, x + 12, 22], fill = (110, 104, 96, 255))
	for y in (66, 90, 114):									# the ruled lines
		d.rectangle ([34, y, 126 if y < 114 else 84, y + 5], fill = (150, 146, 136, 255))
	# the pencil, across the lower right corner
	d.polygon ([(150, 82), (162, 94), (102, 154), (90, 142)], fill = (70, 130, 160, 255))
	d.polygon ([(150, 82), (162, 94), (156, 100), (144, 88)], fill = (217, 102, 122, 255))
	d.polygon ([(90, 142), (102, 154), (84, 160)], fill = (240, 210, 160, 255))
	d.polygon ([(88, 152), (92, 156), (84, 160)], fill = INK)
	return img

def stickies ():
	img = Image.new ("RGBA", (S, S), (0, 0, 0, 0))
	d = ImageDraw.Draw (img)
	d.polygon ([(20, 30), (140, 30), (140, 116), (110, 146), (20, 146)], fill = (160, 128, 40, 255))
	d.polygon ([(24, 34), (136, 34), (136, 112), (108, 142), (24, 142)], fill = PAPER)
	d.polygon ([(136, 112), (108, 112), (108, 142)], fill = (226, 196, 110, 255))		# the turned corner
	for y in (70, 92, 114):
		d.rectangle ([40, y, 120 if y < 114 else 92, y + 5], fill = (170, 150, 100, 255))
	d.ellipse ([62, 6, 98, 42], fill = PIN)							# the push pin
	d.ellipse ([70, 12, 84, 26], fill = (244, 176, 186, 255))
	d.rectangle ([77, 40, 83, 56], fill = (110, 104, 96, 255))
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

def main ():
	save (notes (), "notes")
	save (stickies (), "stickies")

if __name__ == "__main__":
	main ()
