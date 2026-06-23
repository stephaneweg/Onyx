#!/usr/bin/env python3
# Throwaway: montage the app icons into a PNG so they can be eyeballed.
import zlib, struct, os
import gen_assets as G

CELL = 52
names = list(G.ICONS.keys())
W = len(names) * CELL
H = CELL
BG = (44, 48, 58)
img = [BG] * (W * H)

for i, name in enumerate(names):
    px = G.ICONS[name]()
    ox = i * CELL + (CELL - G.SZ) // 2
    oy = (CELL - G.SZ) // 2
    for y in range(G.SZ):
        for x in range(G.SZ):
            c = px[y * G.SZ + x]
            if c != G.MAGENTA:
                img[(oy + y) * W + (ox + x)] = c

def png(path, w, h, rgb):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            r, g, b = rgb[y * w + x]
            raw += bytes((r, g, b))
    comp = zlib.compress(bytes(raw), 9)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", comp))
        f.write(chunk(b"IEND", b""))

png(os.path.join(os.path.dirname(__file__), "icons_preview.png"), W, H, img)
print("wrote tools/icons_preview.png", W, "x", H, "  order:", names)
