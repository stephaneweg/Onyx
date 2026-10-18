#!/bin/sh
# tools/icons/cosmozoom_icon.sh -- sdcard/apps/cosmozoom.app/icon.bmp from cosmozoom_icon.cpp (40 x 40, magenta see-through).
set -e
cd "$(dirname "$0")/../.."
g++ -std=gnu++17 -O2 -w -Iuser -Iuser/Libs -o /tmp/cz_icon tools/icons/cosmozoom_icon.cpp
/tmp/cz_icon /tmp/cz_icon.ppm
python3 - <<'PY'
from PIL import Image
im = Image.open ('/tmp/cz_icon.ppm').convert ('RGB')
W = 40; k = im.size[0] // W
out = Image.new ('RGB', (W, W))
for y in range (W):
	for x in range (W):
		cs = [im.getpixel ((x * k + i, y * k + j)) for j in range (k) for i in range (k)]
		keep = [c for c in cs if c != (255, 0, 255)]
		if len (keep) * 2 < len (cs): out.putpixel ((x, y), (255, 0, 255)); continue
		out.putpixel ((x, y), tuple (sum (c[n] for c in keep) // len (keep) for n in range (3)))
out.save ('sdcard/apps/cosmozoom.app/icon.bmp')
out.resize ((160, 160), Image.NEAREST).save ('/tmp/cz_icon_big.png')
PY
