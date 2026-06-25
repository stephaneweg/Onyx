#!/usr/bin/env python3
# gen-icon.py -- generate the NetSurf launcher icon for Onyx.
#
# Onyx app icons are 40x40 24bpp uncompressed BMPs; magenta (0xFF00FF) is the transparent
# key (user/bmp.hpp). This draws a "web globe": a teal->deep-blue shaded sphere with a
# graticule (meridians + parallels), a dark rim and an upper-left highlight. Run by the
# netsurf-app.mk `stage` target; output path defaults to the staged app.
#
#   python3 gen-icon.py [out.bmp]
import struct, math, os, sys

W = H = 40
KEY = (255, 0, 255)
cx = cy = 19.5
R = 18.0

def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

OCEAN_T = (90, 180, 225)
OCEAN_B = (24, 92, 150)
GRID    = (208, 236, 255)
OUTLINE = (12, 54, 96)

px = [[KEY for _ in range(W)] for _ in range(H)]
for y in range(H):
    for x in range(W):
        dx, dy = x + 0.5 - cx, y + 0.5 - cy
        r = math.hypot(dx, dy)
        if r > R:
            continue
        if r > R - 1.7:
            px[y][x] = OUTLINE
            continue
        t = max(0.0, min(1.0, (dx + dy) / (2 * R) + 0.5))
        col = lerp(OCEAN_T, OCEAN_B, t)
        hl = math.hypot(dx + 6.5, dy + 6.5)
        if hl < 6.0:
            col = lerp(col, (235, 248, 255), (6.0 - hl) / 6.0 * 0.5)
        line = abs(dx) < 0.9
        if not line:
            for a in (R, R * 0.60, R * 0.24):
                if abs((dx / a) ** 2 + (dy / R) ** 2 - 1.0) < 0.10:
                    line = True
                    break
        if not line:
            for ky in (-0.52, 0.0, 0.52):
                yy = ky * R
                halfw = math.sqrt(max(0.0, R * R - yy * yy)) * 0.98
                if abs(dy - yy) < 0.9 and abs(dx) < halfw:
                    line = True
                    break
        px[y][x] = lerp(col, GRID, 0.65) if line else col

pixels = bytearray()
for y in range(H - 1, -1, -1):
    for x in range(W):
        r, g, b = px[y][x]
        pixels += bytes((b, g, r))
data = bytes(pixels)
hdr = b'BM' + struct.pack('<IHHI', 54 + len(data), 0, 0, 54)
info = struct.pack('<IiiHHIIiiII', 40, W, H, 1, 24, 0, len(data), 2835, 2835, 0, 0)

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "sdcard", "apps",
    "netsurf.app", "icon.bmp")
os.makedirs(os.path.dirname(out), exist_ok=True)
open(out, "wb").write(hdr + info + data)
print("wrote %s: %dx%d 24bpp, %d bytes" % (out, W, H, 54 + len(data)))
