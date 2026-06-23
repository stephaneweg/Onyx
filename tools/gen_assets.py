#!/usr/bin/env python3
#
# gen_assets.py -- generate the desktop assets (the "apps" button glyph and per-app
# icon tiles) as 24-bpp uncompressed BMPs that the kernel's GImage::LoadBMP can
# decode (bottom-up, BGR, 4-byte row padding). Magenta (0xFF00FF) is the transparency
# key, so icon corners/background use it for a rounded look.
# (The wallpaper is generated at runtime by the kernel, not shipped as a file.)
#
# Run from the repo root:  python3 tools/gen_assets.py
#
import os, struct

ROOT    = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APPS    = os.path.join(ROOT, "sdcard", "apps")
MAGENTA = (255, 0, 255)          # GIMAGE_TRANSPARENT key (R,G,B)


def write_bmp(path, w, h, pixels):
    """pixels: list of (r,g,b) row-major, top-to-bottom, length w*h."""
    row_bytes = w * 3
    pad = (4 - row_bytes % 4) % 4
    img = bytearray()
    for y in range(h - 1, -1, -1):           # BMP rows are bottom-up
        for x in range(w):
            r, g, b = pixels[y * w + x]
            img += bytes((b, g, r))          # BGR
        img += b"\x00" * pad
    size = 54 + len(img)
    hdr = struct.pack("<2sIHHI", b"BM", size, 0, 0, 54)
    dib = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(img), 2835, 2835, 0, 0)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(hdr + dib + img)
    print("  %-48s %dx%d" % (os.path.relpath(path, ROOT), w, h))


def rounded_tile(w, h, fill, border=(0, 0, 0), radius=6):
    px = []
    for y in range(h):
        for x in range(w):
            # Round the corners by keying them out to magenta.
            cx = x if x >= radius else radius - x
            cy = y if y >= radius else radius - y
            cx = (w - 1 - x) if x > w - 1 - radius else cx
            cy = (h - 1 - y) if y > h - 1 - radius else cy
            corner_x = min(x, w - 1 - x)
            corner_y = min(y, h - 1 - y)
            if corner_x < radius and corner_y < radius and \
               (radius - corner_x) ** 2 + (radius - corner_y) ** 2 > radius * radius:
                px.append(MAGENTA)
            elif corner_x < 2 or corner_y < 2:
                px.append(border)
            else:
                px.append(fill)
    return px


def gen_icon(path, fill):
    write_bmp(path, 40, 40, rounded_tile(40, 40, fill))


def gen_apps_glyph(path, w=40, h=40):
    # Nine white squares (3x3) on a magenta (transparent) field.
    px = [MAGENTA] * (w * h)
    sq, gap, off = 8, 4, 6
    for r in range(3):
        for c in range(3):
            x0 = off + c * (sq + gap)
            y0 = off + r * (sq + gap)
            for y in range(y0, y0 + sq):
                for x in range(x0, x0 + sq):
                    px[y * w + x] = (235, 235, 245)
    write_bmp(path, w, h, px)


def main():
    print("Generating desktop assets under sdcard/apps/ ...")
    # (The wallpaper is now generated at runtime by the kernel -- see
    #  CWindowManager::GenerateWallpaper / kapi_wallpaper_generate.)
    gen_apps_glyph(os.path.join(APPS, "panel.app", "apps.bmp"))

    colors = {
        "demoA": (200, 60, 60),   "demoB": (60, 170, 80),
        "demoC": (220, 130, 40),  "demoD": (60, 110, 210),
        "demoE": (150, 80, 200),  "demoF": (40, 170, 175),
        "tinypad": (230, 220, 210), "tinycalc": (90, 150, 120),
        "inidemo": (170, 130, 60),
    }
    for name, col in colors.items():
        gen_icon(os.path.join(APPS, name + ".app", "icon.bmp"), col)
    print("done.")


if __name__ == "__main__":
    main()
