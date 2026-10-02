#!/usr/bin/env python3
"""tools/icons/mail_icon.py -- Mail's dock / launcher icon (sdcard/apps/mail.app/icon.bmp): an envelope (its flap, its
folds), a teal paper plane leaving it -- drawn at 4 times the size, brought down to 40 x 40; the pixels more than half
covered kept, the others magenta (the icons' see-through key).

    python3 tools/icons/mail_icon.py
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
	# the envelope
	x0, y0, x1, y1 = 10, 52, 136, 142
	d.rounded_rectangle ([x0, y0, x1, y1], 12, fill = (252, 250, 247, 255), outline = (112, 104, 98, 255), width = 5)
	cx = (x0 + x1) // 2
	d.line ([(x0 + 4, y1 - 4), (cx, 104), (x1 - 4, y1 - 4)], fill = (190, 182, 176, 255), width = 5, joint = "curve")
	d.polygon ([(x0 + 3, y0 + 6), (cx, 108), (x1 - 3, y0 + 6)], fill = (226, 220, 214, 255))
	d.line ([(x0 + 4, y0 + 6), (cx, 108), (x1 - 4, y0 + 6)], fill = (112, 104, 98, 255), width = 5, joint = "curve")
	# the paper plane
	plane = [(84, 40), (154, 8), (126, 76), (108, 58)]
	d.polygon (plane, fill = (73, 146, 167, 255))
	d.polygon ([(108, 58), (154, 8), (112, 74)], fill = (46, 108, 128, 255))
	d.line (plane + [plane[0]], fill = (40, 90, 110, 255), width = 4, joint = "curve")
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
	out = os.path.join (gen_assets.APPS, "mail.app", "icon.bmp")
	os.makedirs (os.path.dirname (out), exist_ok = True)
	gen_assets.write_bmp (out, N, N, px)
	print ("wrote", out)

if __name__ == "__main__":
	main ()
