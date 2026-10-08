#!/usr/bin/env python3
"""Onyx web site -- one-off asset preparation (needs Pillow; the page generator build.py does not).

  python website/_build/make_assets.py

- assets/icons/<app>.png : the apps' 40 x 40 icons (sdcard/apps/<app>.app/icon.bmp, magenta = transparent)
- assets/img/<base>-crop.webp : the captures that are shown cut (fileviewer, gamelib, koton), and their
  lines in assets/img/sizes.tsv
- assets/img/<base>.webp (+ -half) : a capture of screenshots/ brought (back) into the site: add its name to
  CAPTURES when a page starts using one that is not in assets/img
- favicon-32.png, apple-touch-icon.png : the gem, drawn from the geometry of favicon.svg
(assets/og.png, the 1200 x 630 social card, is a browser capture of the home page's hero in dark: made by hand.)
MIT licence (ours)."""
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SITE = os.path.dirname(HERE)
REPO = os.path.dirname(SITE)

ICONS = """letters sheet slides calendar ledger cardfile pdf notes tinypad tinycalc graphcalc archiver rtfview
jet mail telegram irc paint koton 3dforge screenshot iconedit fmtracker mandelbrot photos media imageview
pinball critters solitaire freecell doom arkanoid invaders pipes tetris 2048 minesweeper sokoban snake same
pong life gamelib turtle circuits qbstudio qbasic gpiolab courier fileviewer control pkgman disks taskman
terminal clock""".split()

# Captures the pages use that the first conversion did not keep (see check.py --prune).
CAPTURES = "letters-table theme clock-world clock-world-fr".split()


def icon(name):
    src = os.path.join(REPO, "sdcard", "apps", name + ".app", "icon.bmp")
    if not os.path.exists(src):
        print("no icon:", name)
        return
    im = Image.open(src).convert("RGBA")
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if r == 255 and g == 0 and b == 255:
                px[x, y] = (0, 0, 0, 0)
    if im.size != (40, 40):
        print("size", name, im.size)
    im.save(os.path.join(SITE, "assets", "icons", name + ".png"), optimize=True)


def crop(src, box, base):
    """A piece of a capture, saved lossless as <base>.webp (and -half when wider than 900 px), with its
    line in sizes.tsv. box = (left, top, right, bottom); None = the capture's own edge."""
    im = Image.open(os.path.join(REPO, "screenshots", src + ".png"))
    im = im.convert("RGBA" if im.mode in ("RGBA", "LA", "P") else "RGB")
    l, t, r, b = box
    im = im.crop((l or 0, t or 0, im.width if r is None else r, im.height if b is None else b))
    out = os.path.join(SITE, "assets", "img", base)
    im.save(out + ".webp", lossless=True, method=6)
    if im.width > 900:
        im.resize(((im.width + 1) // 2, (im.height + 1) // 2), Image.LANCZOS).save(out + "-half.webp", lossless=True, method=6)
    tsv = os.path.join(SITE, "assets", "img", "sizes.tsv")
    NL, TAB = chr(10), chr(9)
    lines = [x for x in open(tsv, encoding="utf-8").read().splitlines() if x and x.split(TAB)[0] != base]
    lines.append(TAB.join([base, str(im.width), str(im.height), im.mode]))
    open(tsv, "w", encoding="utf-8", newline=NL).write(NL.join(lines) + NL)


def capture(base):
    """screenshots/<base>.png -> assets/img/<base>.webp, lossless, and a half-size copy when wider than 900 px."""
    out = os.path.join(SITE, "assets", "img", base)
    if os.path.exists(out + ".webp"):
        return
    crop(base, (None, None, None, None), base)


# The gem (favicon.svg, logo.svg): a hexagon cut in facets, one facet filled.
GEM_HEX = [(16, 1.5), (28.5, 9), (28.5, 23), (16, 30.5), (3.5, 23), (3.5, 9)]
GEM_LINES = [[(3.5, 9), (16, 13), (28.5, 9)], [(16, 1.5), (16, 30.5)], [(28.5, 14.5), (16, 19)]]
GEM_FACET = [(16, 13), (28.5, 9), (28.5, 14.5), (16, 19)]


def gem_png(path, size, radius):
    """The favicon as a PNG: the gem on a black tile (corner radius in 32nds; 0 = a full square)."""
    k = 8                                   # drawn eight times larger, then reduced
    n = size * k
    u = n / 32.0
    im = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, n - 1, n - 1), radius=radius * u, fill=(10, 10, 10, 255))

    def pt(p):
        return ((3.2 + p[0] * .8) * u, (3.2 + p[1] * .8) * u)
    d.polygon([pt(p) for p in GEM_FACET], fill=(62, 219, 143, 255))
    w = 2.2 * .8 * u
    for line in [GEM_HEX + GEM_HEX[:1]] + GEM_LINES:
        pts = [pt(p) for p in line]
        d.line(pts, fill=(244, 244, 242, 255), width=int(round(w)))
        for x, y in pts:                    # round joins and caps
            d.ellipse((x - w / 2, y - w / 2, x + w / 2, y + w / 2), fill=(244, 244, 242, 255))
    im.resize((size, size), Image.LANCZOS).save(path, optimize=True)


if __name__ == "__main__":
    for n in ICONS:
        icon(n)
    for n in CAPTURES:
        capture(n)
    gem_png(os.path.join(SITE, "favicon-32.png"), 32, 7)
    gem_png(os.path.join(SITE, "apple-touch-icon.png"), 180, 0)
    # The crops the asset curator asked for (assets.md, "usable only if cropped").
    crop("fileviewer", (None, None, 650, None), "fileviewer-crop")   # without the preview of a system script
    crop("gamelib", (225, 315, None, None), "gamelib-crop")          # without the list of systems and their icons
    crop("koton", (None, None, None, 936), "koton-crop")             # without the status bar's "no sound" notice
