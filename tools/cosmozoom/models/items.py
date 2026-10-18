#!/usr/bin/env python3
#
# items.py -- Cosmo Zoom!'s race objects (DESIGN.md 6, 11.2), in the clean sci-fi style the user asked for (no
# faces): the item capsule (a holographic crystal cube with a "?" inside), the plasma bubble, the ion goo puddle,
# the gravity well (file `spring`), the star bit, the storm cloud (`nebula`), the warp star, the four cups'
# trophies and the podium.
#
# Origins: the item capsule, the bubble, the star bit, the nebula and the warp star at their centre; the goo, the
# gravity well, the trophies and the podium on y = 0 at their base centre.
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
#
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from czmodel import PI, Model, Part, loft, revolve, rounded_box, sphere, tube, torus, chaikin, slab, disc, star2  # noqa: E402
from ships import box, fan  # noqa: E402
from city import face_out  # noqa: E402

SCRIPT = "items.py"


def star_gem(R, r, depth, n=5, mat="gold", a0=PI / 2, rim=None):
    """A faceted star: the star outline in the XY plane, a point in front and one behind (depth each); with rim
    (a thickness), straight sides between two copies of the outline."""
    out = [(R if i % 2 == 0 else r, a0 + PI * i / n) for i in range(2 * n)]
    ring = [(rr * math.cos(a), rr * math.sin(a)) for rr, a in out]
    if rim:
        r0 = [(x, y, -rim / 2) for x, y in ring]
        r1 = [(x, y, rim / 2) for x, y in ring]
        rings = [[(0, 0, -rim / 2 - depth)], r0, r1, [(0, 0, rim / 2 + depth)]]
    else:
        rings = [[(0, 0, -depth)], [(x, y, 0.0) for x, y in ring], [(0, 0, depth)]]
    return loft(rings, mat)


def question_mark(h=0.62, r=0.07, mat="white"):
    """A '?' made of a bent tube and a dot, standing in the XY plane, centred."""
    pts = []
    cy = h * 0.22
    R = h * 0.22
    for k in range(9):
        a = PI * 1.05 - PI * 1.35 * k / 8          # the hook, from the left round the top to the right and down
        pts.append((R * math.cos(a), cy + R * math.sin(a), 0.0))
    pts.append((0.02, cy - R * 0.95, 0.0))
    pts.append((0.0, cy - R * 1.6, 0.0))
    t = tube(pts, r, 6, mat, round0=True, round1=False, rsteps=2)
    dot = sphere(r * 1.25, 8, 5, mat).tr(0, cy - R * 2.6, 0)
    return t.add(dot)


def qmark_face(size=0.62, t=0.12, depth=0.05, mat="white"):
    """A bold '?' of five small bars in the XY plane, facing +Z, its back at z = 0, centred."""
    h, w = size, size * 0.62
    bars = [(-w / 2, w / 2, h / 2 - t, h / 2),            # the top
            (w / 2 - t, w / 2, h * 0.08, h / 2),          # the right side going down
            (-t / 2, w / 2, h * 0.08 - t, h * 0.08),      # back to the middle
            (-t / 2, t / 2, -h * 0.18, h * 0.08 - t),     # the stem
            (-t / 2, t / 2, -h / 2, -h / 2 + t)]          # the dot
    p = Part()
    for x0, x1, y0, y1 in bars:
        p.add(box(x0, x1, y0, y1, 0.0, depth, mat))
    return p


def itembox():
    m = Model("itembox", "the item box: a chrome rounded cube (1.2 m), a magenta '?' on each side and on top")
    s = 1.2
    from soft import soft_box
    m.add(soft_box(-s / 2, s / 2, -s / 2, s / 2, -s / 2, s / 2, 0.24, "chrome", k=3), "shell")
    q = qmark_face(0.66, 0.13, 0.05, "magenta")
    for ang in (0, 90, 180, 270):
        m.add(q.copy().tr(0, 0, s / 2 - 0.01).rot("y", ang), "shell")
    m.add(q.copy().tr(0, 0, s / 2 - 0.01).rot("x", -90), "shell")
    return m


def bubble():
    m = Model("bubble", "the plasma bubble (1.4 m): a see-through pink sphere crossed by two white energy rings")
    m.add(sphere(0.70, 12, 7, "bubble"))
    m.add(torus(0.66, 0.03, 16, 3, "white").rot("x", 90).rot("y", 35).rot("z", 25))
    m.add(torus(0.66, 0.03, 16, 3, "white").rot("x", 90).rot("y", -40).rot("z", -30))
    m.add(sphere(0.16, 6, 4, "magenta"))
    return m


def goo():
    m = Model("goo", "the ion goo: a fizzy mint puddle (3 m, 0.25 m high), bubbles and droplets")
    rng = np.random.RandomState(7)
    n = 14
    pts = []
    for k in range(n):
        a = 2 * PI * k / n
        r = 1.5 * (0.80 + 0.12 * math.sin(3 * a + 0.4) + 0.08 * math.cos(5 * a))
        if k % 2 == 1:
            r *= 0.86
        pts.append((r * math.cos(a), r * math.sin(a)))
    out = chaikin(pts, 1)
    p = slab(out, 0.12, bevel=0.06, steps=1, mat="mint")
    p.v = np.stack([p.v[:, 0], p.v[:, 2] + 0.06, p.v[:, 1]], axis=1)
    p.orient()
    m.add(p)
    inner = chaikin([(x * 0.55, y * 0.55) for x, y in pts], 1)
    q = slab(inner, 0.08, bevel=0.04, steps=1, mat="green")
    q.v = np.stack([q.v[:, 0], q.v[:, 2] + 0.13, q.v[:, 1]], axis=1)
    m.add(q.orient())
    for (x, z, r) in ((0.4, 0.3, 0.16), (-0.5, 0.1, 0.12), (0.1, -0.6, 0.10), (-0.2, 0.7, 0.08), (0.8, -0.3, 0.09)):
        m.add(sphere(r, 6, 4, "mint").sc(1, 0.8, 1).tr(x, 0.17, z))
    for (x, z) in ((1.75, 0.6), (-1.6, -0.7), (0.3, 1.75)):
        m.add(sphere(0.09, 6, 3, "mint").sc(1, 0.5, 1).tr(x, 0.04, z))
    return m


def spring():
    m = Model("spring", "the gravity well: a hexagonal pad, a spinning grape ring, a glowing core (1.6 m)")
    base = revolve([(0.0, 0.0), (0.80, 0.0), (0.80, 0.10), (0.66, 0.20), (0.0, 0.20)], 6, "darkgrey", a0=PI / 6)
    m.add(base)
    m.add(revolve([(0.0, 0.20), (0.50, 0.20), (0.44, 0.27), (0.0, 0.27)], 12, "plasma_glow"))
    # the pylon: three blades round a core, the orb
    for k in range(3):
        a = 2 * PI * k / 3
        b = Part()
        from ships import fin
        f = fin([(0.20, 0.10, 0.26, 0.06), (0.75, 0.04, 0.16, 0.05)], "grape")
        f.rot("y", math.degrees(a)).tr(0.30 * math.sin(a), 0, 0.30 * math.cos(a))
        m.add(f)
    m.add(revolve([(0.0, 0.2), (0.12, 0.2), (0.10, 0.80), (0.0, 0.80)], 8, "silver"))
    m.add(sphere(0.20, 10, 6, "magenta_glow").tr(0, 0.98, 0))
    ring = torus(0.52, 0.06, 16, 4, "grape").rot("x", 20).tr(0, 0.98, 0)
    m.add(ring, "ring")
    m.socket("ring", 0, 0.98, 0)
    return m


def starbit():
    m = Model("starbit", "a star bit: a faceted gold star (0.5 m)")
    m.add(star_gem(0.25, 0.11, 0.08, 5, "gold"))
    return m


def nebula():
    m = Model("nebula", "the storm cloud: faceted grape and lavender puffs, lightning bolts under it (3 m)")
    puffs = [(0.0, 0.25, 0.0, 0.75, "lavender"), (-0.75, 0.0, 0.1, 0.58, "grape"), (0.75, 0.0, -0.05, 0.6, "grape"),
             (0.3, 0.05, 0.55, 0.5, "lavender"), (-0.3, 0.0, -0.55, 0.52, "grape")]
    for (x, y, z, r, mt) in puffs:
        m.add(sphere(r, 8, 5, mt).sc(1, 0.78, 1).tr(x, y, z))
    m.add(revolve([(0.0, -0.45), (1.0, -0.40), (1.15, -0.25), (0.0, -0.20)], 9, "grape").tr(0, 0, 0))
    for (x, z, s) in ((-0.4, 0.2, 1.0), (0.45, -0.1, 0.8)):
        bolt = [(0.10, 0.0), (-0.10, -0.28), (0.02, -0.28), (-0.14, -0.62), (0.16, -0.22), (0.04, -0.22), (0.20, 0.0)]
        b = slab(bolt, 0.07, bevel=0.0001, steps=1, mat="yellow_glow")
        b.sc(s).tr(x, -0.38, z)
        m.add(b)
    return m


def warpstar():
    m = Model("warpstar", "the warp star: a big faceted star (3 m) with a white rim, glowing tips")
    m.add(star_gem(1.5, 0.72, 0.42, 5, "yellow", rim=0.36))
    m.add(star_gem(1.62, 0.78, 0.0001, 5, "white", rim=0.18))
    for i in range(5):
        a = PI / 2 + 2 * PI * i / 5
        m.add(sphere(0.12, 6, 4, "yellow_glow").tr(1.64 * math.cos(a), 1.64 * math.sin(a), 0))
    return m


# ---- trophies and podium ---------------------------------------------------------------------------------------------
def cup(emblem):
    """A gold cup on a dark plinth; returns (part, top y)."""
    p = Part()
    p.add(revolve([(0.0, 0.0), (0.42, 0.0), (0.42, 0.18), (0.36, 0.24), (0.0, 0.24)], 6, "darkgrey", a0=PI / 6))
    p.add(revolve([(0.0, 0.24), (0.30, 0.24), (0.30, 0.30), (0.0, 0.30)], 6, "paint2" if False else "silver", a0=PI / 6))
    p.add(revolve([(0.0, 0.30), (0.20, 0.30), (0.08, 0.42), (0.08, 0.62), (0.22, 0.74), (0.36, 0.96),
                   (0.40, 1.22), (0.34, 1.24), (0.0, 1.10)], 10, "gold"))
    for sx in (1, -1):
        h = tube([(sx * 0.30, 1.10, 0), (sx * 0.52, 1.08, 0), (sx * 0.50, 0.90, 0), (sx * 0.28, 0.80, 0)], 0.04, 5, "gold")
        p.add(h)
    return p, 1.24


def trophy(name, title, emblem):
    m = Model(name, title)
    p, top = cup(emblem)
    m.add(p)
    y = top + 0.32
    if emblem == "moon":
        out = [(0.30 * math.cos(a), 0.30 * math.sin(a)) for a in np.linspace(PI * 0.35, PI * 1.65, 9)]
        inn = [(0.13 + 0.24 * math.cos(a), 0.24 * math.sin(a)) for a in np.linspace(PI * 1.55, PI * 0.45, 7)]
        e = slab(out + inn, 0.10, bevel=0.03, steps=1, mat="sky")
        m.add(e.rot("z", 15).tr(0, y, 0))
    elif emblem == "nebula":
        pts = []
        for k in range(14):
            a = 2 * PI * 1.3 * k / 13
            r = 0.05 + 0.22 * k / 13
            pts.append((r * math.cos(a), y + r * math.sin(a), 0.0))
        m.add(tube(pts[::2], lambda t: 0.045 + 0.02 * t, 6, "grape", round0=True, round1=True, rsteps=2))
    elif emblem == "supernova":
        m.add(star_gem(0.34, 0.14, 0.06, 8, "orange", rim=0.06).tr(0, y, 0))
        m.add(sphere(0.10, 8, 5, "yellow").tr(0, y, 0.06))
    elif emblem == "galaxy":
        for arm in (0, PI):
            pts = []
            for k in range(9):
                a = arm + 2.6 * k / 8
                r = 0.05 + 0.28 * k / 8
                pts.append((r * math.cos(a), y + r * math.sin(a) * 0.9, 0.0))
            m.add(tube(pts[::2], lambda t: 0.05 * (1 - 0.6 * t), 5, "magenta", round1=True, rsteps=2))
        m.add(sphere(0.09, 8, 5, "white").tr(0, y, 0))
    # the emblem's stem
    m.add(tube([(0, top - 0.02, 0), (0, y - 0.18, 0)], 0.035, 6, "gold", cap0=False))
    return m


def podium():
    m = Model("podium", "the podium: three steps (1st in the middle), the numbers as geometry, light strips")
    steps = [(0.0, 2.0, "1", "gold"), (-2.6, 1.4, "2", "silver"), (2.6, 0.9, "3", "orange")]
    for x, h, num, mt in steps:
        m.add(box(x - 1.25, x + 1.25, 0, h, -1.2, 1.2, "paint1", 0.12))
        m.add(box(x - 1.30, x + 1.30, h - 0.22, h - 0.12, -1.25, 1.25, "paint2"))
        m.add(box(x - 1.0, x + 1.0, 0.08, 0.16, 1.2, 1.24, "cyan_glow"))
        d = digit(num, 1.0 if h > 1.2 else 0.62, mt)
        m.add(d.tr(x, h * 0.5 - 0.02, 1.2))
    return m


def digit(ch, h, mat):
    """A blocky digit (1, 2, 3) as a thin extruded outline in the XY plane, its front at z = +0.08."""
    w = h * 0.55
    t = h * 0.2
    segs = {"1": [(0.5, 0, 0.5, 1)], "2": [(0, 1, 1, 1), (1, 1, 1, 0.5), (1, 0.5, 0, 0.5), (0, 0.5, 0, 0), (0, 0, 1, 0)],
            "3": [(0, 1, 1, 1), (1, 1, 1, 0), (0, 0.5, 1, 0.5), (0, 0, 1, 0)]}[ch]
    p = Part()
    for (x0, y0, x1, y1) in segs:
        ax, ay = (x0 - 0.5) * w, (y0 - 0.5) * h
        bx, by = (x1 - 0.5) * w, (y1 - 0.5) * h
        p.add(box(min(ax, bx) - t / 2, max(ax, bx) + t / 2, min(ay, by) - t / 2, max(ay, by) + t / 2, 0.0, 0.08, mat))
    if ch == "1":
        p.add(box(-t * 1.6, 0.0, h / 2 - t * 1.2, h / 2 + t / 2, 0.0, 0.08, mat))
    return p


ALL = {
    "itembox": itembox,
    "bubble": bubble,
    "goo": goo,
    "spring": spring,
    "starbit": starbit,
    "nebula": nebula,
    "warpstar": warpstar,
    "trophy_moon": lambda: trophy("trophy_moon", "the Moonbeam Cup's trophy: a gold cup, a crescent moon", "moon"),
    "trophy_nebula": lambda: trophy("trophy_nebula", "the Nebula Cup's trophy: a gold cup, a swirl", "nebula"),
    "trophy_supernova": lambda: trophy("trophy_supernova", "the Supernova Cup's trophy: a gold cup, a burst", "supernova"),
    "trophy_galaxy": lambda: trophy("trophy_galaxy", "the Galaxy Cup's trophy: a gold cup, a spiral galaxy", "galaxy"),
    "podium": podium,
}


def build(names=None):
    out = {}
    for n, fn in ALL.items():
        if names and n not in names:
            continue
        mdl = fn()
        out[n] = (mdl.write(script=SCRIPT), mdl)
    return out


if __name__ == "__main__":
    for n, (t, _) in build(sys.argv[1:] or None).items():
        print(n, t)
