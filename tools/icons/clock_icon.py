#!/usr/bin/env python3
"""tools/icons/clock_icon.py -- the icons of the Clock and clockd (AutoDev round 6, 04-ux-design.md section 1.1):
sdcard/apps/clock.app/icon.bmp, a round clock face -- a light dial, an accent rim, twelve ticks, the hands at
10:10 -- with a small bell at its top right; sdcard/apps/clockd.app/icon.bmp, the Clock's service, the same face
in greys with the bell (it is not in the dock's drawers: the Task Manager shows it). Drawn at 4 times the size,
brought down to 40 x 40; the pixels more than half covered kept, the others magenta (the icons' see-through key)
-- as notes_icon.py.

    python3 tools/icons/clock_icon.py

MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
"""
import math, os, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, os.path.dirname (HERE))
import gen_assets

S, N = 160, 40

def face (rim, dial, ink, hand2, bell):
	img = Image.new ("RGBA", (S, S), (0, 0, 0, 0))
	d = ImageDraw.Draw (img)
	cx, cy, r = 76, 84, 70
	d.ellipse ([cx - r, cy - r, cx + r, cy + r], fill = rim)				# the rim
	d.ellipse ([cx - r + 12, cy - r + 12, cx + r - 12, cy + r - 12], fill = dial)	# the dial
	for k in range (12):									# the ticks
		a = math.pi * 2 * k / 12
		r0, r1, w = (r - 30, r - 18, 7) if k % 3 == 0 else (r - 26, r - 18, 4)
		x0, y0 = cx + math.sin (a) * r0, cy - math.cos (a) * r0
		x1, y1 = cx + math.sin (a) * r1, cy - math.cos (a) * r1
		d.line ([(x0, y0), (x1, y1)], fill = ink, width = w)
	def hand (deg, length, width, colour):
		a = math.radians (deg)
		d.line ([(cx, cy), (cx + math.sin (a) * length, cy - math.cos (a) * length)], fill = colour, width = width)
	hand (305, 34, 10, ink)									# the hour hand: 10:10
	hand (60, 48, 7, ink)									# the minute hand
	hand (180, 14, 7, ink)
	hand (60, 50, 3, hand2)									# the second hand
	d.ellipse ([cx - 8, cy - 8, cx + 8, cy + 8], fill = hand2)
	# the bell, top right
	bx, by = 128, 30
	d.ellipse ([bx - 24, by - 24, bx + 24, by + 24], fill = (255, 255, 255, 255))
	d.pieslice ([bx - 16, by - 18, bx + 16, by + 14], 180, 360, fill = bell)
	d.rectangle ([bx - 16, by - 2, bx + 16, by + 8], fill = bell)
	d.rounded_rectangle ([bx - 20, by + 6, bx + 20, by + 12], 3, fill = bell)
	d.ellipse ([bx - 5, by + 10, bx + 5, by + 19], fill = bell)
	d.ellipse ([bx - 4, by - 24, bx + 4, by - 16], fill = bell)
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
	ACCENT = (46, 110, 190, 255)
	save (face (ACCENT, (246, 248, 252, 255), (40, 44, 52, 255), (217, 72, 60, 255), (232, 168, 30, 255)), "clock")
	save (face ((120, 126, 136, 255), (232, 234, 238, 255), (60, 64, 72, 255), (120, 126, 136, 255), (150, 154, 162, 255)), "clockd")

if __name__ == "__main__":
	main ()
