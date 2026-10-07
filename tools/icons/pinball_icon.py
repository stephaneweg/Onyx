#!/usr/bin/env python3
"""tools/icons/pinball_icon.py -- the icon of Pinball (AutoDev round 4, 03-technical-analysis.md step 13):
sdcard/apps/pinball.app/icon.bmp, a playfield seen from above in Space Station's colours -- the navy table in its
steel rails, two cyan pop bumpers, a magenta-rimmed flipper pair, the steel ball. Drawn at 4 times the size, brought
down to 40 x 40; the pixels more than half covered kept, the others magenta (the icons' see-through key) -- as
circuits_icon.py.

    python3 tools/icons/pinball_icon.py

MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
"""
import os, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, HERE)
import circuits_icon

S = 160
NAVY, RAIL = (14, 26, 58, 255), (159, 179, 217, 255)
CYAN, CYAND = (34, 195, 230, 255), (16, 110, 140, 255)
MAG, IVORY = (224, 85, 154, 255), (242, 244, 248, 255)

def pinball ():
	img = Image.new ("RGBA", (S, S), (0, 0, 0, 0))
	d = ImageDraw.Draw (img)
	d.rounded_rectangle ([28, 4, 132, 156], 40, fill = RAIL)			# the cabinet's rails
	d.rounded_rectangle ([36, 12, 124, 148], 34, fill = NAVY)			# the playfield
	for x, y in ((64, 50), (98, 50), (81, 76)):					# the pop bumpers
		d.ellipse ([x - 15, y - 15, x + 15, y + 15], fill = CYAND)
		d.ellipse ([x - 12, y - 12, x + 12, y + 12], fill = CYAN)
		d.ellipse ([x - 6, y - 7, x + 4, y + 3], fill = (210, 245, 255, 255))
	for pts in (((46, 112), (74, 132)), ((114, 112), (86, 132))):		# the flippers: rubber, then the body
		d.line (pts, fill = MAG, width = 13); d.line (pts, fill = IVORY, width = 7)
		for p in pts: d.ellipse ([p[0] - 6, p[1] - 6, p[0] + 6, p[1] + 6], fill = MAG)
	d.ellipse ([70, 92, 92, 114], fill = (124, 131, 142, 255))			# the ball, steel
	d.ellipse ([72, 94, 88, 110], fill = (196, 202, 210, 255))
	d.ellipse ([74, 96, 81, 103], fill = (255, 255, 255, 255))
	return img

if __name__ == "__main__":
	circuits_icon.save (pinball (), "pinball")
