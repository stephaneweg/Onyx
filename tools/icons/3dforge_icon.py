#!/usr/bin/env python3
"""tools/icons/3dforge_icon.py -- 3DForge's dock / launcher icon (sdcard/apps/3dforge.app/icon.bmp): a solid seen from
its corner -- a block with a round hole through its top, a rounded edge -- on the view's grid, drawn at 4 times the
size, brought down to 40 x 40; the pixels more than half covered kept, the others magenta (the see-through key).

    python3 tools/icons/3dforge_icon.py
"""
import os, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, os.path.dirname (HERE))
import gen_assets

S, N = 160, 40
EDGE = (30, 44, 68, 255)

def draw ():
	img = Image.new ("RGBA", (S, S), (0, 0, 0, 0))
	d = ImageDraw.Draw (img)
	# the tile: the view's pale ground, its grid
	d.rounded_rectangle ([4, 4, 156, 156], 22, fill = (236, 240, 246, 255), outline = (120, 130, 150, 255), width = 4)
	for i in range (1, 6):
		d.line ([(8 + i * 24, 110 + i * 7), (8 + i * 24 + 60, 110 + i * 7 - 34)], fill = (196, 204, 216, 255), width = 2)
	# the block: its top, its left and right faces
	top = [(80, 30), (136, 56), (80, 84), (24, 56)]
	left = [(24, 56), (80, 84), (80, 134), (24, 106)]
	right = [(136, 56), (80, 84), (80, 134), (136, 106)]
	d.polygon (left, fill = (112, 142, 190, 255)); d.polygon (right, fill = (78, 104, 150, 255)); d.polygon (top, fill = (160, 188, 228, 255))
	for p in (top, left, right): d.line (p + [p[0]], fill = EDGE, width = 5, joint = "curve")
	# the hole through the top; the height's arrow
	d.ellipse ([58, 44, 102, 68], fill = (52, 72, 108, 255), outline = EDGE, width = 4)
	d.ellipse ([64, 52, 96, 68], fill = (96, 124, 170, 255))
	d.line ([(120, 40), (120, 12)], fill = (232, 150, 44, 255), width = 7)
	d.polygon ([(120, 2), (132, 20), (108, 20)], fill = (232, 150, 44, 255))
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
	out = os.path.join (gen_assets.APPS, "3dforge.app", "icon.bmp")
	os.makedirs (os.path.dirname (out), exist_ok = True)
	gen_assets.write_bmp (out, N, N, px)
	print ("wrote", out)

if __name__ == "__main__":
	main ()
