#!/usr/bin/env python3
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
"""onyxremote_icon.py -- the icon of Onyx Remote (pc/OnyxRemote/onyxremote.ico): a screen showing the Onyx
desktop (its blue, a window, the dock) and the link's waves. Drawn large, reduced to each size.

    python tools/icons/onyxremote_icon.py        (needs Pillow)"""
import os
from PIL import Image, ImageDraw

S = 1024
def u (v): return int (round (v * S / 256.0))

def draw ():
	im = Image.new ("RGBA", (S, S), (0, 0, 0, 0))
	d = ImageDraw.Draw (im)
	# the screen: a dark frame, the desktop's blue (lighter at the top)
	d.rounded_rectangle ([u (14), u (34), u (242), u (190)], radius = u (16), fill = (24, 28, 36, 255))
	x0, y0, x1, y1 = u (26), u (46), u (230), u (178)
	for y in range (y0, y1):
		t = (y - y0) / float (y1 - y0)
		d.line ([x0, y, x1, y], fill = (int (58 - 26 * t), int (120 - 46 * t), int (196 - 52 * t), 255))
	# a window: its title bar, its buttons, two lines of text
	d.rounded_rectangle ([u (44), u (62), u (150), u (140)], radius = u (6), fill = (240, 242, 245, 255))
	d.rounded_rectangle ([u (44), u (62), u (150), u (80)], radius = u (6), fill = (206, 211, 219, 255))
	d.rectangle ([u (44), u (74), u (150), u (80)], fill = (206, 211, 219, 255))
	for i, c in enumerate (((222, 164, 60), (92, 176, 84), (214, 72, 66))):
		cx = u (118 + 11 * i); d.ellipse ([cx - u (4), u (71) - u (4), cx + u (4), u (71) + u (4)], fill = c + (255,))
	d.rectangle ([u (54), u (92), u (130), u (99)], fill = (120, 132, 150, 255))
	d.rectangle ([u (54), u (108), u (112), u (115)], fill = (160, 170, 186, 255))
	# the dock
	d.rounded_rectangle ([u (92), u (156), u (164), u (172)], radius = u (5), fill = (226, 230, 236, 255))
	# the stand
	d.rectangle ([u (112), u (190), u (144), u (212)], fill = (24, 28, 36, 255))
	d.rounded_rectangle ([u (76), u (210), u (180), u (226)], radius = u (8), fill = (24, 28, 36, 255))
	# the link: a dot and two waves, top right, on a disc
	d.ellipse ([u (158), u (8), u (252), u (102)], fill = (255, 255, 255, 255))
	d.ellipse ([u (164), u (14), u (246), u (96)], fill = (236, 132, 36, 255))
	cx, cy = u (186), u (76)
	for r in (u (44), u (26)):
		d.arc ([cx - r, cy - r, cx + r, cy + r], start = 270, end = 360, fill = (255, 255, 255, 255), width = u (10))
	d.ellipse ([cx - u (8), cy - u (8), cx + u (8), cy + u (8)], fill = (255, 255, 255, 255))
	return im

if __name__ == "__main__":
	out = os.path.join (os.path.dirname (os.path.abspath (__file__)), "..", "..", "pc", "OnyxRemote", "onyxremote.ico")
	big = draw ()
	big.resize ((256, 256), Image.LANCZOS).save (out, sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
	print ("written:", os.path.normpath (out))
