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
import os, struct, math

ROOT    = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APPS    = os.path.join(ROOT, "sdcard", "apps")
MAGENTA = (255, 0, 255)          # GIMAGE_TRANSPARENT key (R,G,B)
SZ      = 40                     # icon size


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


# ---- small drawing primitives on a 40x40 (magenta-transparent) canvas ----------

def blank():
    return [MAGENTA] * (SZ * SZ)

def pset(px, x, y, c):
    if 0 <= x < SZ and 0 <= y < SZ:
        px[y * SZ + x] = c

def prect(px, x0, y0, x1, y1, c):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            pset(px, x, y, c)

def pframe(px, x0, y0, x1, y1, c):
    for x in range(x0, x1 + 1):
        pset(px, x, y0, c); pset(px, x, y1, c)
    for y in range(y0, y1 + 1):
        pset(px, x0, y, c); pset(px, x1, y, c)

def pdisc(px, cx, cy, r, c):
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                pset(px, x, y, c)

# ---- evocative per-app icons ----------------------------------------------------

def icon_tinypad():		# a text document with lines
    px = blank()
    white, ink, bord = (245, 245, 245), (70, 100, 150), (90, 90, 110)
    prect(px, 8, 4, 31, 35, white)
    pframe(px, 8, 4, 31, 35, bord)
    for i, yy in enumerate(range(10, 33, 5)):
        prect(px, 12, yy, 12 + (19 if i % 2 == 0 else 12), yy + 1, ink)
    return px

def icon_tinycalc():		# a calculator: screen + button grid
    px = blank()
    body, scr, btn = (70, 80, 95), (150, 230, 170), (205, 210, 220)
    prect(px, 6, 4, 33, 35, body)
    pframe(px, 6, 4, 33, 35, (40, 46, 56))
    prect(px, 9, 7, 30, 13, scr)
    for r in range(3):
        for c in range(3):
            x, y = 9 + c * 8, 17 + r * 6
            prect(px, x, y, x + 5, y + 3, btn)
    return px

def icon_inidemo():		# a gear (settings/config)
    px = blank()
    steel, hole = (195, 165, 85), (60, 55, 40)
    cx, cy = 20, 20
    for k in range(8):
        a = k * math.pi / 4
        tx, ty = int(cx + 13 * math.cos(a)), int(cy + 13 * math.sin(a))
        prect(px, tx - 3, ty - 3, tx + 3, ty + 3, steel)
    pdisc(px, cx, cy, 11, steel)
    pdisc(px, cx, cy, 4, hole)
    return px

def icon_tetris():		# a purple T-tetromino
    px = blank()
    fill, edge = (180, 75, 210), (115, 40, 150)
    b = 10
    for (gx, gy) in [(0, 0), (1, 0), (2, 0), (1, 1)]:
        x, y = 5 + gx * b, 9 + gy * b
        prect(px, x, y, x + b - 2, y + b - 2, fill)
        pframe(px, x, y, x + b - 2, y + b - 2, edge)
    return px

def icon_snake():		# a green snake + red food
    px = blank()
    body, edge, red, eye = (50, 195, 80), (20, 120, 45), (235, 70, 70), (10, 10, 10)
    b = 8
    for (gx, gy) in [(0, 2), (1, 2), (2, 2), (2, 1), (2, 0), (3, 0)]:
        x, y = 4 + gx * b, 8 + gy * b
        prect(px, x, y, x + b - 1, y + b - 1, body)
        pframe(px, x, y, x + b - 1, y + b - 1, edge)
    hx, hy = 4 + 3 * b, 8                       # head: add an eye
    pset(px, hx + 5, hy + 2, eye); pset(px, hx + 5, hy + 3, eye)
    pdisc(px, 33, 31, 3, red)                   # food
    return px

def icon_same():		# a grid of 4 coloured blocks (SameGame)
    px = blank()
    cols = [(224, 80, 80), (80, 176, 96), (64, 128, 224), (224, 192, 64)]
    pat = [[0, 1, 2, 3], [1, 1, 0, 2], [2, 0, 3, 3], [3, 2, 1, 0]]
    b = 9
    for r in range(4):
        for c in range(4):
            x, y = 4 + c * b, 4 + r * b
            prect(px, x, y, x + b - 2, y + b - 2, cols[pat[r][c]])
    return px

def icon_filer():		# a manila folder
    px = blank()
    body, tab, edge = (225, 190, 90), (236, 208, 120), (150, 120, 40)
    prect(px, 7, 9, 18, 14, tab)
    prect(px, 7, 13, 33, 31, body)
    pframe(px, 7, 13, 33, 31, edge)
    return px

def icon_terminal():		# a black console with a green prompt
    px = blank()
    body, scr, prompt = (40, 44, 52), (16, 22, 18), (80, 230, 120)
    prect(px, 5, 6, 34, 33, body)
    pframe(px, 5, 6, 34, 33, (20, 24, 30))
    prect(px, 8, 9, 31, 30, scr)
    prect(px, 11, 13, 15, 14, prompt)	# ">"
    prect(px, 16, 13, 17, 14, prompt)
    prect(px, 19, 13, 26, 14, (90, 150, 110))	# cursor line
    return px

def icon_2048():		# four coloured tiles
    px = blank()
    cols = [(238, 228, 218), (242, 177, 121), (237, 204, 97), (245, 124, 95)]
    for i in range(4):
        x, y = 6 + (i % 2) * 15, 6 + (i // 2) * 15
        prect(px, x, y, x + 12, y + 12, cols[i])
    return px

def icon_life():		# a glider
    px = blank()
    g = (96, 230, 144)
    for (cx, cy) in [(1, 0), (2, 1), (0, 2), (1, 2), (2, 2)]:
        x, y = 8 + cx * 8, 8 + cy * 8
        prect(px, x, y, x + 6, y + 6, g)
    return px

def icon_pong():		# two paddles + ball
    px = blank()
    prect(px, 6, 10, 9, 28, (235, 235, 235))
    prect(px, 31, 14, 34, 32, (235, 235, 235))
    prect(px, 18, 19, 23, 24, (96, 230, 144))
    return px

def icon_sokoban():		# a box on a target
    px = blank()
    prect(px, 13, 13, 27, 27, (60, 192, 96))		# box (on target = green)
    pframe(px, 13, 13, 27, 27, (30, 110, 50))
    prect(px, 18, 18, 22, 22, (180, 90, 90))		# target dot
    return px

def icon_calendar():		# a calendar page with a red header
    px = blank()
    prect(px, 6, 7, 33, 33, (235, 235, 240))
    prect(px, 6, 7, 33, 13, (210, 70, 70))
    pframe(px, 6, 7, 33, 33, (120, 120, 130))
    for r in range(3):
        for c in range(4):
            x, y = 10 + c * 6, 18 + r * 5
            prect(px, x, y, x + 2, y + 2, (150, 160, 170))
    return px

def icon_mandel():		# coloured field with dark bulbs
    px = [(((x * 5) & 255), ((y * 4) & 255), 120) for y in range(SZ) for x in range(SZ)]
    pdisc(px, 23, 20, 9, (0, 0, 0))
    pdisc(px, 12, 20, 5, (0, 0, 0))
    return px

def icon_mines():		# a mine on grey
    px = blank()
    prect(px, 5, 5, 34, 34, (88, 100, 114))
    pframe(px, 5, 5, 34, 34, (40, 46, 54))
    prect(px, 19, 8, 21, 32, (20, 20, 20))
    prect(px, 8, 19, 32, 21, (20, 20, 20))
    pdisc(px, 20, 20, 8, (20, 20, 20))
    pdisc(px, 17, 17, 2, (220, 220, 220))
    return px

def icon_paint():		# a palette with colour dabs
    px = blank()
    pdisc(px, 20, 22, 14, (225, 210, 180))
    pdisc(px, 14, 18, 3, (224, 80, 80))
    pdisc(px, 22, 15, 3, (80, 176, 96))
    pdisc(px, 27, 21, 3, (64, 128, 224))
    pdisc(px, 18, 27, 3, (224, 192, 64))
    return px

def icon_eyes():		# two googly eyes
    px = blank()
    pdisc(px, 14, 20, 9, (255, 255, 255)); pdisc(px, 16, 22, 3, (24, 24, 32))
    pdisc(px, 28, 20, 9, (255, 255, 255)); pdisc(px, 30, 22, 3, (24, 24, 32))
    return px

def icon_taskman():		# a list of tasks (status dot + bar)
    px = blank()
    prect(px, 6, 7, 33, 33, (40, 48, 58))
    pframe(px, 6, 7, 33, 33, (90, 100, 114))
    for i in range(4):
        y = 11 + i * 6
        prect(px, 9, y, 12, y + 2, (96, 200, 120))
        prect(px, 15, y, 30, y + 2, (150, 160, 170))
    return px

def icon_sheet():		# a grid with a header row
    px = blank()
    prect(px, 6, 7, 33, 33, (236, 236, 238))
    prect(px, 6, 7, 33, 12, (80, 150, 96))
    pframe(px, 6, 7, 33, 33, (120, 120, 130))
    for i in range(1, 4): prect(px, 6 + i * 7, 12, 6 + i * 7, 33, (175, 184, 190))
    for j in range(1, 4): prect(px, 6, 12 + j * 5, 33, 12 + j * 5, (175, 184, 190))
    return px

def icon_voronoy():		# blue cellular blobs (the Voronoi wallpaper generator)
    px = blank()
    prect(px, 5, 5, 34, 34, (30, 60, 110))
    pdisc(px, 13, 14, 8, (60, 110, 170))
    pdisc(px, 28, 12, 7, (40, 90, 150))
    pdisc(px, 13, 29, 7, (84, 134, 194))
    pdisc(px, 29, 29, 8, (50, 100, 160))
    pframe(px, 5, 5, 34, 34, (20, 40, 80))
    return px

ICONS = {
    "tinypad": icon_tinypad, "tinycalc": icon_tinycalc, "inidemo": icon_inidemo,
    "tetris": icon_tetris, "snake": icon_snake, "same": icon_same,
    "filer": icon_filer, "terminal": icon_terminal,
    "2048": icon_2048, "life": icon_life, "pong": icon_pong, "sokoban": icon_sokoban,
    "calendar": icon_calendar, "mandelbrot": icon_mandel,
    "minesweeper": icon_mines, "paint": icon_paint,
    "eyes": icon_eyes, "sheet": icon_sheet, "taskman": icon_taskman,
    "voronoy": icon_voronoy,
}


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

    # Demos keep plain coloured tiles.
    demos = {
        "demoA": (200, 60, 60),  "demoB": (60, 170, 80),
        "demoC": (220, 130, 40), "demoD": (60, 110, 210),
        "demoE": (150, 80, 200), "demoF": (40, 170, 175),
    }
    for name, col in demos.items():
        gen_icon(os.path.join(APPS, name + ".app", "icon.bmp"), col)

    # Real apps get evocative icons.
    for name, fn in ICONS.items():
        write_bmp(os.path.join(APPS, name + ".app", "icon.bmp"), SZ, SZ, fn())
    print("done.")


if __name__ == "__main__":
    main()
