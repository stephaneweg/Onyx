#!/usr/bin/env python3
"""tools/icons/critters_icon.py -- the icon of Critters (AutoDev round 5, 03-technical-analysis.md step 13):
sdcard/apps/critters.app/icon.bmp, one of the game's creatures -- the round, pebble-shaped body in warm yellow with its
dark-brown outline and pale belly, two big eyes looking right, tiny dark feet and the green sprout on its head (04 D19:
our own drawing) -- standing on a strip of grass-topped earth. Drawn at 4 times the size, brought down to 40 x 40; the
pixels more than half covered kept, the others magenta (the icons' see-through key) -- as circuits_icon.py.

    python3 tools/icons/critters_icon.py

MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
"""
import os, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, HERE)
import circuits_icon

S = 160
LINE, BODY, BODYDK, BELLY = (74, 42, 16, 255), (255, 194, 74, 255), (229, 142, 34, 255), (255, 231, 166, 255)
LEAF, LEAFDK, FEET = (126, 217, 87, 255), (62, 138, 46, 255), (58, 36, 18, 255)
EARTH, GRASS = (138, 90, 52, 255), (110, 138, 60, 255)

def ell (d, cx, cy, rx, ry, c): d.ellipse ([cx - rx, cy - ry, cx + rx, cy + ry], fill = c)

def critter ():
	img = Image.new ("RGBA", (S, S), (0, 0, 0, 0))
	d = ImageDraw.Draw (img)
	d.rounded_rectangle ([6, 128, 154, 156], 10, fill = EARTH)			# the ground
	d.rounded_rectangle ([6, 124, 154, 136], 6, fill = GRASS)
	ell (d, 62, 122, 14, 8, FEET); ell (d, 98, 122, 14, 8, FEET)			# the feet
	cx, cy = 80, 74
	ell (d, cx, cy, 46, 50, LINE)							# the body: outline, face, shade, belly
	ell (d, cx, cy, 41, 45, BODY)
	ell (d, cx, cy + 22, 32, 20, BODYDK)
	ell (d, cx + 6, cy + 14, 24, 22, BELLY)
	ell (d, cx - 17, cy - 26, 11, 7, (255, 246, 220, 255))			# a highlight
	d.line ([(cx - 3, cy - 48), (cx + 1, cy - 70)], fill = LEAFDK, width = 6)	# the sprout
	ell (d, cx + 17, cy - 70, 17, 8, LEAFDK); ell (d, cx + 17, cy - 71, 13, 5, LEAF)
	for ex in (cx - 2, cx + 30):							# the eyes, looking right
		ell (d, ex, cy - 10, 11, 13, (255, 255, 255, 255))
		ell (d, ex + 4, cy - 9, 6, 8, (26, 14, 6, 255))
	return img

if __name__ == "__main__":
	circuits_icon.save (critter (), "critters")
