#!/usr/bin/env python3
"""tools/icons/pdf_icon.py -- the PDF Viewer's dock / launcher icon (sdcard/apps/pdf.app/icon.bmp): a white page, its
corner folded, lines of text, the red "PDF" band across it, a teal magnifier over the corner -- drawn at 4 times the size,
brought down to 40 x 40; the pixels more than half covered kept, the others magenta (the icons' see-through key).

    python3 tools/icons/pdf_icon.py
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, os.path.dirname (HERE))
import gen_assets

S, N = 160, 40

def draw ():
	img = Image.new ("RGBA", (S, S), (0, 0, 0, 0))
	d = ImageDraw.Draw (img)
	# the page, its corner folded
	x0, y0, x1, y1, f = 26, 8, 134, 152, 34
	page = [(x0, y0), (x1 - f, y0), (x1, y0 + f), (x1, y1), (x0, y1)]
	d.polygon (page, fill = (252, 250, 247, 255))
	d.line (page + [page[0]], fill = (120, 110, 104, 255), width = 5, joint = "curve")
	d.polygon ([(x1 - f, y0), (x1 - f, y0 + f), (x1, y0 + f)], fill = (214, 206, 200, 255))
	d.line ([(x1 - f, y0), (x1 - f, y0 + f), (x1, y0 + f)], fill = (120, 110, 104, 255), width = 4)
	for k in range (4):
		y = 46 + k * 14
		d.rounded_rectangle ([x0 + 16, y, x1 - (20 if k % 2 else 34), y + 6], 3, fill = (176, 168, 162, 255))
	# the red band
	d.rounded_rectangle ([12, 104, 112, 140], 7, fill = (200, 52, 44, 255))
	fnt = ImageFont.truetype ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 30)
	d.text ((62, 123), "PDF", font = fnt, fill = (255, 255, 255, 255), anchor = "mm")
	# the magnifier
	cx, cy, r = 118, 104, 22
	d.line ([(cx + 14, cy + 14), (cx + 34, cy + 34)], fill = (40, 90, 110, 255), width = 14)
	d.ellipse ([cx - r, cy - r, cx + r, cy + r], fill = (210, 236, 242, 255), outline = (73, 146, 167, 255), width = 8)
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
	out = os.path.join (gen_assets.APPS, "pdf.app", "icon.bmp")
	os.makedirs (os.path.dirname (out), exist_ok = True)
	gen_assets.write_bmp (out, N, N, px)
	print ("wrote", out)

if __name__ == "__main__":
	main ()
