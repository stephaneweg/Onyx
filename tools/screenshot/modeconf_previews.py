#!/usr/bin/env python3
#
# modeconf_previews.py -- the Mode applet's pictures (sdcard/apps/modeconf.app): the three modes' previews, 160 x 100,
# made small from the design study's mock-ups (docs/compact-shell/mockups: the desktop, pocket's home, console's
# home; tools/screenshot/mockup_compact.py draws them), and the applet's icon (40 x 40, the magenta key) -- a
# monitor and a handheld. 24-bit BMPs, as the card's icons.
#
#   python3 tools/screenshot/modeconf_previews.py
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
#
import os
from PIL import Image, ImageDraw

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
MOCK = os.path.join(ROOT, "docs", "compact-shell", "mockups")
OUT = os.path.join(ROOT, "sdcard", "apps", "modeconf.app")
W, H = 160, 100


def preview(src, dst):
    im = Image.open(os.path.join(MOCK, src)).convert("RGB")
    s = min(W / im.width, H / im.height)
    w, h = round(im.width * s), round(im.height * s)
    small = im.resize((w, h), Image.LANCZOS)
    edge = small.getpixel((0, h // 2))            # (the letterbox: the picture's own edge colour)
    out = Image.new("RGB", (W, H), edge)
    out.paste(small, ((W - w) // 2, (H - h) // 2))
    out.save(os.path.join(OUT, "res", dst))


def icon(dst):
    im = Image.new("RGB", (40, 40), (255, 0, 255))
    d = ImageDraw.Draw(im)
    # the monitor (the desktop), back left
    d.rectangle([2, 4, 29, 23], fill=(52, 58, 66))
    d.rectangle([4, 6, 27, 21], fill=(52, 120, 196))
    d.polygon([(4, 21), (27, 6), (27, 21)], fill=(40, 100, 170))
    d.rectangle([13, 24, 18, 28], fill=(52, 58, 66))
    d.rectangle([8, 28, 23, 30], fill=(52, 58, 66))
    # the handheld (pocket, console), front right
    d.rounded_rectangle([14, 19, 38, 36], radius=5, fill=(236, 238, 242), outline=(52, 58, 66))
    d.rectangle([19, 22, 33, 32], fill=(30, 40, 70))
    d.rectangle([20, 23, 32, 25], fill=(90, 160, 230))
    d.point([(16, 27), (16, 28), (15, 28), (17, 28)], fill=(52, 58, 66))   # (a pad's cross)
    d.ellipse([35, 26, 37, 28], fill=(220, 70, 70))
    im.save(os.path.join(OUT, dst))


if __name__ == "__main__":
    os.makedirs(os.path.join(OUT, "res"), exist_ok=True)
    preview("app-media-desktop.png", "desktop.bmp")
    preview("pocket-home.png", "pocket.bmp")
    preview("console-home.png", "console.bmp")
    icon("icon.bmp")
    print("modeconf: res/desktop.bmp, res/pocket.bmp, res/console.bmp, icon.bmp in", OUT)
