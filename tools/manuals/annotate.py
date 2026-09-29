#!/usr/bin/env python3
# tools/manuals/annotate.py -- a screenshot marked with numbered callouts for a manual: margins added around
# it (see-through), a numbered disc in a margin for each point, a line from the disc to the point, a dot on
# it -- the picture itself never covered. The manual's text then lists what each number is.
#
#   annotate.py IN.png OUT.png [--crop x0,y0,x1,y1] N:x,y[:l|r|t|b] ...
#
# N the number shown, x,y the point in IN's pixels (before the crop), l / r / t / b the margin its disc
# goes in -- left, right, top, bottom (by default the nearer of left and right). Discs in the same margin
# closer than a disc's size are moved along it. --crop keeps a part of IN only (its window's page, say).
import sys
from PIL import Image, ImageDraw, ImageFont

INK = (200, 72, 24, 255)		# the callouts' colour (a deep orange: seen on the Onyx themes' greys and teals)
WHITE = (255, 255, 255, 255)
R = 13					# a disc's radius
M = 46					# a margin's width

def font (size):
	for f in ("DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
		  "sdcard/fonts/LiberationSans-Bold.ttf", "LiberationSans-Bold.ttf"):
		try: return ImageFont.truetype (f, size)
		except OSError: pass
	return ImageFont.load_default ()

def main (argv):
	args = argv[1:]
	crop = None
	if "--crop" in args:
		i = args.index ("--crop"); crop = tuple (int (v) for v in args[i + 1].split (",")); del args[i:i + 2]
	if len (args) < 3:
		sys.stderr.write ("annotate.py IN.png OUT.png [--crop x0,y0,x1,y1] N:x,y[:l|r|t|b] ...\n"); return 2
	src = Image.open (args[0]).convert ("RGBA")
	ox = oy = 0
	if crop: src = src.crop (crop); ox, oy = crop[0], crop[1]
	W, H = src.size
	pts = []
	for a in args[2:]:
		p = a.split (":")
		n = p[0]; x, y = (int (v) for v in p[1].split (","))
		x -= ox; y -= oy
		side = p[2] if len (p) > 2 else ("l" if x < W / 2 else "r")
		pts.append ({"n": n, "x": x, "y": y, "side": side})
	sides = set (p["side"] for p in pts)
	ml = M if "l" in sides else 0; mr = M if "r" in sides else 0
	mt = M if "t" in sides else 0; mb = M if "b" in sides else 0
	out = Image.new ("RGBA", (W + ml + mr, H + mt + mb), (0, 0, 0, 0))
	out.paste (src, (ml, mt))
	# the discs along each margin: in order, apart
	for side in ("l", "r", "t", "b"):
		vert = side in ("l", "r")
		col = sorted ([p for p in pts if p["side"] == side], key = lambda p: p["y"] if vert else p["x"])
		last = -1000
		for p in col:
			v = max (p["y"] + mt if vert else p["x"] + ml, last + 2 * R + 6, R + 2)
			p["d"] = v; last = v
	d = ImageDraw.Draw (out)
	f = font (15)
	for p in pts:
		px, py = p["x"] + ml, p["y"] + mt
		s = p["side"]
		if s == "l": cx, cy = ml // 2, p["d"]; path = [(cx, cy), (ml, cy), (px, py)]
		elif s == "r": cx, cy = ml + W + mr // 2, p["d"]; path = [(cx, cy), (ml + W, cy), (px, py)]
		elif s == "t": cx, cy = p["d"], mt // 2; path = [(cx, cy), (cx, mt), (px, py)]
		else: cx, cy = p["d"], mt + H + mb // 2; path = [(cx, cy), (cx, mt + H), (px, py)]
		d.line (path, fill = WHITE, width = 5)
		d.line (path, fill = INK, width = 2)
		d.ellipse ([px - 5, py - 5, px + 5, py + 5], fill = WHITE)
		d.ellipse ([px - 3, py - 3, px + 3, py + 3], fill = INK)
		d.ellipse ([cx - R - 2, cy - R - 2, cx + R + 2, cy + R + 2], fill = WHITE)
		d.ellipse ([cx - R, cy - R, cx + R, cy + R], fill = INK)
		bb = d.textbbox ((0, 0), p["n"], font = f)
		d.text ((cx - (bb[2] - bb[0]) / 2 - bb[0], cy - (bb[3] - bb[1]) / 2 - bb[1]), p["n"], font = f, fill = WHITE)
	out.save (args[1])
	return 0

if __name__ == "__main__":
	sys.exit (main (sys.argv))
