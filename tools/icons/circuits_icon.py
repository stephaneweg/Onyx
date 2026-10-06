#!/usr/bin/env python3
"""tools/icons/circuits_icon.py -- the icon of Circuits (AutoDev round 2, 03-technical-analysis.md step 8):
sdcard/apps/circuits.app/icon.bmp, an AND gate on the board's pale green paper, its two inputs fed by a lit
(green) wire and a dark one, its output a lit wire into a yellow lamp -- the board's colours (gates.h). Drawn at
4 times the size, brought down to 40 x 40; the pixels more than half covered kept, the others magenta (the
icons' see-through key) -- as notes_icon.py.

    python3 tools/icons/circuits_icon.py

MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
"""
import os, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, os.path.dirname (HERE))
import gen_assets

S, N = 160, 40
BOARD, EDGE = (238, 243, 239, 255), (120, 140, 126, 255)
INK, FACE = (43, 52, 64, 255), (227, 245, 231, 255)
WIRE1, WIRE0 = (33, 163, 70, 255), (51, 74, 94, 255)
LAMP, LAMPRIM = (255, 210, 63, 255), (150, 110, 20, 255)

def circuits ():
	img = Image.new ("RGBA", (S, S), (0, 0, 0, 0))
	d = ImageDraw.Draw (img)
	d.rounded_rectangle ([6, 14, 154, 146], 16, fill = EDGE)				# the board
	d.rounded_rectangle ([10, 18, 150, 142], 13, fill = BOARD)
	for x in range (26, 146, 20):								# its grid's dots
		for y in range (30, 140, 20):
			d.ellipse ([x - 2, y - 2, x + 2, y + 2], fill = (201, 212, 204, 255))
	# the wires: A lit, B dark, the output lit
	W = 9
	d.rectangle ([10, 56 - W // 2, 52, 56 + W // 2], fill = WIRE1)
	d.rectangle ([10, 104 - W // 2, 52, 104 + W // 2], fill = WIRE0)
	d.rectangle ([108, 80 - W // 2, 128, 80 + W // 2], fill = WIRE1)
	# the AND gate: a flat back, a half disc in front (outline then face)
	L, T, B = 48, 40, 120
	r = (B - T) // 2
	cx = 108 - r
	d.rectangle ([L, T, cx, B], fill = INK)
	d.pieslice ([cx - r, T, cx + r, B], -90, 90, fill = INK)
	k = 6
	d.rectangle ([L + k, T + k, cx, B - k], fill = FACE)
	d.pieslice ([cx - r + k, T + k, cx + r - k, B - k], -90, 90, fill = FACE)
	# the lamp, lit, with its glow
	d.ellipse ([116, 58, 160, 102], fill = (255, 236, 160, 255))
	d.ellipse ([122, 64, 154, 96], fill = LAMPRIM)
	d.ellipse ([126, 68, 150, 92], fill = LAMP)
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
	save (circuits (), "circuits")

if __name__ == "__main__":
	main ()
