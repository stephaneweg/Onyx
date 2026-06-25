#!/usr/bin/env python3
# gen-icon.py -- the httpc launcher icon for Onyx (40x40 24bpp BMP, magenta = transparent).
# Draws a little browser window: a blue address bar with a white field, then content text
# lines on white, one of them a blue hyperlink.  python3 gen-icon.py [out.bmp]
import struct, os, sys

W = H = 40
KEY = (255, 0, 255)
BORDER = (40, 54, 86)
BAR    = (42, 111, 176)
FIELD  = (236, 242, 250)
PAGE   = (255, 255, 255)
TEXT   = (150, 158, 170)
LINK   = (26, 79, 208)

px = [[KEY for _ in range(W)] for _ in range(H)]

def rect(x0, y0, x1, y1, c):
    for y in range(max(0, y0), min(H, y1)):
        for x in range(max(0, x0), min(W, x1)):
            px[y][x] = c

# window box 3..37 x 4..36
rect(3, 4, 37, 36, BORDER)            # frame
rect(4, 5, 36, 35, PAGE)              # page (interior)
rect(4, 5, 36, 14, BAR)               # title / address bar
rect(7, 7, 33, 12, FIELD)            # address field
# content text lines (left-indented), widths vary like real text
for (yy, x1, col) in [(18, 32, TEXT), (22, 28, TEXT), (26, 30, LINK), (30, 24, TEXT)]:
    rect(7, yy, x1, yy + 2, col)

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
    "httpc.app", "icon.bmp")
os.makedirs(os.path.dirname(out), exist_ok=True)
open(out, "wb").write(hdr + info + data)
print("wrote %s: %dx%d 24bpp, %d bytes" % (out, W, H, 54 + len(data)))
