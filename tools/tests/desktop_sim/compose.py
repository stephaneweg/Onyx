#!/usr/bin/env python3
"""compose.py -- lay the windows dumped by Onyx's apps run on the PC (fakekapi.cpp, the "dump"
step) over the wallpaper as the Onyx compositor does: each pixel 0xTTRRGGBB with TT its
transparency (0 = opaque: a frame's rounded corners, a see-through window), at the window's place.

    python3 tools/tests/desktop_sim/compose.py out.png a.elsm b.elsm ... [--flat=RRGGBB] [--crop=x0,y0,x1,y1]

The dumps are drawn in the order given (the bottom first). The wallpaper: render.py's Voronoi
(the desktop's default), or a flat colour with --flat=RRGGBB; --crop keeps a part of the screen."""
import os, struct, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "screenshot"))

def wallpaper(flat=None):
	if flat:
		c = int(flat, 16)
		return np.tile(np.array([(c >> 16) & 255, (c >> 8) & 255, c & 255], np.float32), (768, 1024, 1))
	import render as r
	return np.asarray(r.voronoi_wallpaper(1024, 768).convert("RGB"), dtype=np.float32)

def load(path):
	d = open(path, "rb").read()
	magic, w, h, x, y = struct.unpack("<5i", d[:20])
	px = np.frombuffer(d[20:20 + w * h * 4], dtype="<u4").reshape(h, w)
	return x, y, px

def main():
	flat = [a[7:] for a in sys.argv[1:] if a.startswith("--flat=")]
	crop = [tuple(int(v) for v in a[7:].split(",")) for a in sys.argv[1:] if a.startswith("--crop=")]
	args = [a for a in sys.argv[1:] if not a.startswith("--")]
	out, dumps = args[0], args[1:]
	img = wallpaper(flat[0] if flat else None)
	H, W = img.shape[:2]
	for p in dumps:
		x, y, px = load(p)
		h, w = px.shape
		x0, y0, x1, y1 = max(x, 0), max(y, 0), min(x + w, W), min(y + h, H)
		sub = px[y0 - y:y1 - y, x0 - x:x1 - x]
		a = (255 - (sub >> 24)).astype(np.float32)[..., None] / 255.0
		rgb = np.stack([(sub >> 16) & 255, (sub >> 8) & 255, sub & 255], axis=-1).astype(np.float32)
		img[y0:y1, x0:x1] = rgb * a + img[y0:y1, x0:x1] * (1 - a)
	pic = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))
	if crop:
		pic = pic.crop(crop[0])
	pic.save(out, optimize=True)
	print("wrote", out)

if __name__ == "__main__":
	main()
