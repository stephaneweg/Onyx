#!/usr/bin/env python3
#
# nature.py -- Cosmo Zoom!'s floating islands and the green things on them, in the final rounded low-poly look
# (the user's approved image, 2026-10-15): floating islands (a flat grass top with a rounded edge over an inverted
# cone of low-poly rock, boulders on top), round trees, bushes, rocks, puffy clouds, a small arched bridge.
#
# Origins: an island at the centre of its grass top (y = 0 at the grass); a cloud at its centre; the rest on y = 0
# at the centre of its base.
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
#
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from czmodel import PI, Model, Part, loft, revolve, slab, sphere, tube, chaikin  # noqa: E402
from soft import blob, rrect  # noqa: E402
from city import vertical_cyl, soft_path  # noqa: E402

SCRIPT = "nature.py"


def outline(rx, rz, n, seed, wob=0.10):
    """A rounded irregular outline (x, z), counter-clockwise."""
    rng = np.random.RandomState(seed)
    pts = []
    for k in range(n):
        a = 2 * PI * k / n
        f = 1.0 + rng.uniform(-wob, wob)
        pts.append((rx * f * math.cos(a), rz * f * math.sin(a)))
    return chaikin(pts, 1)


def grass_top(pts, thick=0.9):
    """The grass: a slab through the outline, its edge rounded, its top at y = 0."""
    g = slab(pts, thick, bevel=thick * 0.45, steps=2, mat="grass")
    g.v = np.stack([g.v[:, 0], g.v[:, 2] - thick / 2, g.v[:, 1]], axis=1)
    return g.orient()


def rock_cone(pts, depth, seed, rings=4):
    """The rocky underside: rings shrinking down to a point, jittered (low-poly), under the grass."""
    rng = np.random.RandomState(seed + 7)
    n = len(pts)
    P = np.array(pts, float)
    rs = []
    for i in range(rings):
        t = i / rings
        sc = 0.97 * (1 - t) ** 0.85
        y = -0.55 - depth * (t ** 1.1)
        ring = []
        for k in range(0, n, 2):
            j = 1.0 + rng.uniform(-0.12, 0.12) * (1 if i else 0.3)
            ring.append((P[k, 0] * sc * j, y + rng.uniform(-0.25, 0.25) * (1 if i else 0), P[k, 1] * sc * j))
        rs.append(ring)
    cx = rng.uniform(-0.1, 0.1) * depth
    rs.append([(cx, -0.55 - depth * 1.12, cx * 0.5)])
    return loft(rs, "rock", cap0=True, cap1=False)


def island(name, rx, rz, depth, seed, boulders=(), trees=(), title=""):
    m = Model(name, title)
    pts = outline(rx, rz, 12, seed)
    m.add(grass_top(pts, min(1.0, 0.12 * max(rx, rz) + 0.3)))
    m.add(rock_cone(pts, depth, seed))
    for (x, z, r, s) in boulders:
        m.add(blob(r, "pebble", 8, 5, seed=s, jitter=0.16, sy=0.75).tr(x, r * 0.45, z))
    for (x, z, h, s) in trees:
        m.add(tree_parts(h, s).tr(x, 0, z))
    return m


def tree_parts(h, seed, two=False):
    p = Part()
    p.add(vertical_cyl(h * 0.075, -0.2, h * 0.45, 0.0, 0.0, "wood", 8))
    p.add(blob(h * 0.34, "leaf", 9, 6, seed=seed, jitter=0.14, sy=0.92).tr(0, h * 0.66, 0))
    if two:
        p.add(blob(h * 0.21, "leaf", 8, 5, seed=seed + 3, jitter=0.14).tr(h * 0.20, h * 0.92, h * 0.05))
        p.add(blob(h * 0.18, "darkgreen", 8, 5, seed=seed + 5, jitter=0.14).tr(-h * 0.18, h * 0.55, -h * 0.08))
    return p


def tree_round():
    m = Model("tree_round", "a round tree: a low-poly rounded canopy on a trunk (6 m)")
    m.add(tree_parts(6.0, 11))
    return m


def tree_round2():
    m = Model("tree_round2", "a round tree with three lumps of leaves (7 m)")
    m.add(tree_parts(7.0, 21, two=True))
    return m


def bush_round():
    m = Model("bush_round", "a round bush: three low-poly lumps (2.4 m)")
    for (x, z, r, s, mt) in ((0.0, 0.0, 0.9, 1, "leaf"), (0.8, 0.3, 0.65, 2, "leaf"), (-0.7, -0.2, 0.7, 3, "darkgreen")):
        m.add(blob(r, mt, 8, 5, seed=s, jitter=0.14, sy=0.85).tr(x, r * 0.62, z))
    return m


def rock_round(name, r, seed, title):
    m = Model(name, title)
    m.add(blob(r, "pebble", 9, 6, seed=seed, jitter=0.2, sy=0.72).tr(0, r * 0.62, 0))
    return m


def rock_round2():
    m = Model("rock_round2", "a group of rounded rocks (3.5 m)")
    for (x, z, r, s) in ((0.0, 0.0, 1.2, 4), (1.3, 0.5, 0.75, 5), (-0.9, 0.8, 0.55, 6)):
        m.add(blob(r, "pebble", 8, 5, seed=s, jitter=0.2, sy=0.72).tr(x, r * 0.6, z))
    return m


def cloud_puff():
    m = Model("cloud_puff", "a puffy cloud: low-poly rounded white balls (about 20 m wide)")
    for (x, y, z, r, s) in ((0.0, 1.5, 0.0, 6.0, 1), (-6.0, 0.0, 0.8, 4.4, 2), (6.2, -0.2, -0.5, 4.6, 3), (2.8, 3.6, 1.8, 3.8, 4),
                            (-2.8, 3.0, -1.6, 3.6, 5), (-9.5, -1.2, 0.2, 2.6, 6)):
        b = blob(r, "white", 10, 7, seed=s, jitter=0.05)
        # a flatter bottom
        b.v[:, 1] = np.where(b.v[:, 1] < 0, b.v[:, 1] * 0.55, b.v[:, 1])
        m.add(b.tr(x, y, z))
    return m


def bridge_arch():
    m = Model("bridge_arch", "a small arched bridge (14 m long, 3 m wide) with rails")
    L, rise, half_w = 7.0, 2.4, 1.5
    path = []
    n = 10
    for i in range(n + 1):
        t = -1 + 2 * i / n
        path.append((t * L, rise * (1 - t * t) + 0.3, 0.0))
    # the deck: a rounded section swept along the arch (wide across, thin)
    m.add(soft_path(path, 0.3, half_w, 0.25, "white", k=2))
    for sz in (1, -1):
        rail = [(x, y + 1.0, sz * (half_w - 0.12)) for (x, y, _) in path]
        m.add(tube(rail, 0.09, 5, "sky", round0=True, round1=True, rsteps=1))
        for i in range(0, n + 1, 2):
            x, y, _ = path[i]
            m.add(tube([(x, y + 0.2, sz * (half_w - 0.12)), (x, y + 1.0, sz * (half_w - 0.12))], 0.07, 4, "sky", cap0=False))
    for sx in (1, -1):
        m.add(revolve([(0.0, 0.0), (0.9, 0.0), (0.8, 0.5), (0.0, 0.55)], 8, "pebble").sc(1, 1, 1.7).tr(sx * L, 0, 0))
    return m


ALL = {
    "island_small": lambda: island("island_small", 6.0, 5.0, 6.0, 3, boulders=((2.2, 1.0, 0.9, 1), (-1.0, -2.2, 0.6, 2)),
                                   title="a small floating island: a grass top over a cone of rock, two boulders"),
    "island_big": lambda: island("island_big", 12.0, 10.0, 11.0, 5, boulders=((5.0, 3.0, 1.4, 3), (-6.0, -2.0, 1.0, 4), (1.0, -6.0, 0.8, 5)),
                                 trees=((-3.0, 4.0, 6.0, 9), (6.5, -3.5, 5.0, 10)),
                                 title="a big floating island: grass, rock, three boulders, two round trees"),
    "island_long": lambda: island("island_long", 16.0, 6.0, 8.0, 8, boulders=((9.0, 1.5, 1.1, 6), (-10.0, -1.0, 0.9, 7)),
                                  trees=((3.0, 1.5, 5.5, 12),),
                                  title="a long floating island: grass, rock, two boulders, a round tree"),
    "tree_round": tree_round,
    "tree_round2": tree_round2,
    "bush_round": bush_round,
    "rock_round": lambda: rock_round("rock_round", 1.6, 9, "a rounded low-poly rock (3 m)"),
    "rock_round2": rock_round2,
    "cloud_puff": cloud_puff,
    "bridge_arch": bridge_arch,
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
