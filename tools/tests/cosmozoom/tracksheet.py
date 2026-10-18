#!/usr/bin/env python3
# Cosmo Zoom! -- the contact sheet of the circuits seen from above (docs/cosmozoom/tracks.png): each circuit's map
# (trackmap.cpp: the road coloured by its height, the features marked) with its name, its length and the winner's
# best lap of an 8-AI race at the Hyper class (racesim.cpp).
#   g++ -std=gnu++17 -O2 -w -Iuser -Iuser/Libs -o /tmp/trackmap tools/tests/cosmozoom/trackmap.cpp
#   g++ -std=gnu++17 -O2 -w -Iuser -Iuser/Libs -o /tmp/racesim tools/tests/cosmozoom/racesim.cpp
#   python3 tools/tests/cosmozoom/tracksheet.py [out.png]
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
import os, re, subprocess, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")
TRACKS = os.path.join(ROOT, "sdcard", "apps", "cosmozoom.app", "tracks")
CUPS = os.path.join(ROOT, "sdcard", "apps", "cosmozoom.app", "cups.ini")
TRACKMAP = os.environ.get("TRACKMAP", "/tmp/trackmap")
RACESIM = os.environ.get("RACESIM", "/tmp/racesim")
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "docs", "cosmozoom", "tracks.png")


def font(size, bold=False):
    for f in ("/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf" % ("-Bold" if bold else ""),):
        if os.path.exists(f):
            return ImageFont.truetype(f, size)
    return ImageFont.load_default()


# the circuits in the cups' order
ids, names = [], {}
for line in open(CUPS, encoding="utf-8"):
    m = re.match(r"\s*(\w+)\s*=\s*([^|]+)\|", line)
    if m and m.group(1) not in ("name", "name_fr", "color", "unlock", "tracks"):
        ids.append(m.group(1)); names[m.group(1)] = m.group(2).strip()

TILE, LAB = 400, 64
cols = 4
rows = (len(ids) + cols - 1) // cols
sheet = Image.new("RGB", (cols * TILE, 70 + rows * (TILE + LAB)), (8, 10, 32))
d = ImageDraw.Draw(sheet)
d.text((16, 14), "COSMO ZOOM!  THE 13 CIRCUITS", font=font(26, True), fill=(255, 255, 255))
d.text((16, 46), "Seen from above: the road coloured by its height (blue low, cyan, green, yellow, magenta high); green bar: the start; "
       "cyan: speed pads; yellow: capsules; red: jumps; white: loops; teal/magenta: tubes; orange edges: twists; grey: tunnels; "
       "squares: props. Bar: 100 m.", font=font(11), fill=(170, 180, 230))
for i, cid in enumerate(ids):
    trk = os.path.join(TRACKS, cid + ".trk")
    ppm = "/tmp/tracksheet_%s.ppm" % cid
    info = subprocess.run([TRACKMAP, trk, ppm], capture_output=True, text=True).stdout
    race = subprocess.run([RACESIM, trk], capture_output=True, text=True).stdout
    length = re.search(r": (\d+) m,", info).group(1)
    lo, hi = re.search(r"height (\S+) \.\. (\S+) m", info).groups()
    laps = [float(x) for x in re.findall(r"best lap (\S+)", race)]
    x, y = (i % cols) * TILE, 70 + (i // cols) * (TILE + LAB)
    sheet.paste(Image.open(ppm).resize((TILE, TILE), Image.LANCZOS), (x, y))
    d.text((x + 10, y + TILE + 6), "%d. %s" % (i + 1, names[cid]), font=font(17, True), fill=(255, 255, 255))
    d.text((x + 10, y + TILE + 32), "%s m   heights %d..%d m   best lap %.1f s" % (length, round(float(lo)) + 0, round(float(hi)), min(laps)),
           font=font(13), fill=(150, 230, 255))
sheet.save(out)
print(out)
