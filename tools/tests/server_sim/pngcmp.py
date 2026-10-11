# tools/tests/server_sim/pngcmp.py -- two pictures compared pixel for pixel: exit 0 when they are the same, 1 with the
# count of the pixels that differ and their bounding box (pocket640.sh's "same" / "differ"; the desktop's before / after).
#   python3 pngcmp.py a.png b.png [diff.png]
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
import sys
from PIL import Image, ImageChops

a = Image.open(sys.argv[1]).convert("RGBA")
b = Image.open(sys.argv[2]).convert("RGBA")
if a.tobytes() == b.tobytes():
    print("same (%dx%d)" % a.size)
    sys.exit(0)
if a.size != b.size:
    print("sizes differ: %dx%d and %dx%d" % (a.size + b.size))
    sys.exit(1)
d = ImageChops.difference(a, b)
m = d.convert("RGB").point(lambda v: 255 if v else 0).convert("L")		# (a pixel whose colour differs)
m = ImageChops.lighter(m, d.getchannel("A").point(lambda v: 255 if v else 0))	# (... or whose alpha does)
box = m.getbbox()
n = sum(m.point(lambda v: 1 if v else 0).histogram()[1:2])
print("differ: %d pixels, box %s" % (n, box))
if len(sys.argv) > 3:
    d.convert("RGB").point(lambda v: 255 if v else 0).save(sys.argv[3])
sys.exit(1)
