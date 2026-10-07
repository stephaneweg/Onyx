#!/usr/bin/env python3
"""cards.py -- what a dump of Stickies (user/Apps/stickies, AutoDev round 1) shows: its cards, read back from the
pixels (run_stickies_sim_test.sh). A card is a run of opaque pixels (the paper: transparency byte 0) down the
column x = 224 (inside a card, right of its text); its colour is the paper nearest to the run's middle pixel.

    python3 tools/tests/notes/cards.py DUMP.elsm      -> "2 yellow green" (the count, then each card's colour)
    python3 tools/tests/notes/cards.py DUMP.elsm --at -> "776 40" (where the window is)
    python3 tools/tests/notes/cards.py DUMP.elsm --px X Y -> the pixel 0xTTRRGGBB

MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors."""
import struct, sys

PAPER = {"yellow": 0xFCE9A6, "green": 0xD3EBC6, "blue": 0xCFE0F3, "pink": 0xF8D3D8, "purple": 0xE2D6F0, "grey": 0xE4E2DE}

def load(path):
	d = open(path, "rb").read()
	magic, w, h, x, y = struct.unpack("<5i", d[:20])
	px = struct.unpack("<%dI" % (w * h), d[20:20 + w * h * 4])
	return w, h, x, y, px

def nearest(c):
	r, g, b = (c >> 16) & 255, (c >> 8) & 255, c & 255
	def dist(p):
		return (r - (p >> 16 & 255)) ** 2 + (g - (p >> 8 & 255)) ** 2 + (b - (p & 255)) ** 2
	return min(PAPER, key=lambda k: dist(PAPER[k]))

def main():
	w, h, x, y, px = load(sys.argv[1])
	if "--at" in sys.argv:
		print(x, y); return
	if "--px" in sys.argv:
		i = sys.argv.index("--px"); X, Y = int(sys.argv[i + 1]), int(sys.argv[i + 2])
		print("0x%08X" % px[Y * w + X]); return
	col, runs, start = 224, [], None
	for yy in range(h + 1):
		opaque = yy < h and (px[yy * w + col] >> 24) == 0
		if opaque and start is None: start = yy
		if not opaque and start is not None:
			if yy - start >= 40: runs.append((start, yy))
			start = None
	names = [nearest(px[((a + b) // 2) * w + col] & 0xFFFFFF) for a, b in runs]
	print(len(runs), *names)

main()
