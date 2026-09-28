#!/usr/bin/env python3
"""shot.py -- a window dumped by an Onyx app run on the PC (fakekapi.cpp, the "dump" step) as a
PNG of its own, for the documentation: its frame and client area, the see-through pixels (the
frame's rounded corners, a WIN_FLAG_ALPHA window's clear parts) transparent in the PNG.

    python3 tools/tests/desktop_sim/shot.py window.elsm out.png [--flat=RRGGBB]

--flat: lay it over a flat colour instead (an opaque PNG)."""
import struct, sys
import numpy as np
from PIL import Image

def main():
	flat = [a[7:] for a in sys.argv[1:] if a.startswith("--flat=")]
	args = [a for a in sys.argv[1:] if not a.startswith("--")]
	d = open(args[0], "rb").read()
	magic, w, h, x, y = struct.unpack("<5i", d[:20])
	px = np.frombuffer(d[20:20 + w * h * 4], dtype="<u4").reshape(h, w)
	rgb = np.stack([(px >> 16) & 255, (px >> 8) & 255, px & 255], axis=-1).astype(np.uint8)
	alpha = (255 - (px >> 24)).astype(np.uint8)
	if flat:
		c = int(flat[0], 16)
		back = np.array([(c >> 16) & 255, (c >> 8) & 255, c & 255], np.float32)
		a = alpha.astype(np.float32)[..., None] / 255.0
		img = Image.fromarray((rgb * a + back * (1 - a)).astype(np.uint8), "RGB")
	else:
		img = Image.fromarray(np.dstack([rgb, alpha]), "RGBA")
	img.save(args[1], optimize=True)
	print("wrote", args[1], img.size)

if __name__ == "__main__":
	main()
