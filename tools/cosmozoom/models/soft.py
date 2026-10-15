#!/usr/bin/env python3
#
# soft.py -- the rounded low-poly toolkit of Cosmo Zoom!'s final look (the user's approved image, 2026-10-15:
# chunky soft shapes with bevelled edges, asset-pack style, no outline): lofts through rounded-rectangle sections
# with rounded ends, rounded boxes, bevelled cylinders, rounded slabs (fins), lumpy low-poly blobs.
# The game lights the models smoothly and keeps the edges sharper than ~40 degrees hard: three segments a corner
# (30 degrees each) read as a soft rounded edge.
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
#
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from czmodel import PI, Part, loft, revolve, sphere, slab  # noqa: E402

K = 3   # segments a rounded corner


def rrect(w, h, r, k=K, top=(), side=()):
    """A rounded rectangle (half sizes w, h; corner radius r), counter-clockwise seen from +Z (x right, y up).
    top: extra points on the top edge (fractions 0..1 from the right corner to the left one); side: extra points
    on both sides (fractions 0..1 from the bottom corner up). Returns (points, labels): the label of the segment
    that starts at each point (corner_tr, top0.., corner_tl, side_l.., corner_bl, bottom, corner_br, side_r..)."""
    r = max(1e-4, min(r, w * 0.999, h * 0.999))
    cx, cy = w - r, h - r
    pts, lab = [], []
    ns = len(side)

    def arc(x0, y0, a0, name, end_label):
        for i in range(k + 1):
            a = a0 + PI / 2 * i / k
            pts.append((x0 + r * math.cos(a), y0 + r * math.sin(a)))
            lab.append(name if i < k else end_label)
    arc(cx, cy, 0.0, "corner_tr", "top0")
    for i, f in enumerate(top):
        pts.append((cx - 2 * cx * f, h)); lab.append("top%d" % (i + 1))
    arc(-cx, cy, PI / 2, "corner_tl", "side_l%d" % ns)
    for i, f in enumerate(sorted(side, reverse=True)):
        pts.append((-w, -cy + 2 * cy * f)); lab.append("side_l%d" % (ns - 1 - i))
    arc(-cx, -cy, PI, "corner_bl", "bottom")
    arc(cx, -cy, 1.5 * PI, "corner_br", "side_r0")
    for i, f in enumerate(sorted(side)):
        pts.append((w, -cy + 2 * cy * f)); lab.append("side_r%d" % (i + 1))
    out, ol = [], []
    for p, l in zip(pts, lab):
        if out and abs(out[-1][0] - p[0]) < 1e-7 and abs(out[-1][1] - p[1]) < 1e-7:
            ol[-1] = l
            continue
        out.append(p); ol.append(l)
    return out, ol


class Station:
    """A section of a soft loft: at z, centred on (cx, cy), half sizes w x h, corner radius r."""

    def __init__(self, z, w, h, r, cx=0.0, cy=0.0):
        self.z, self.w, self.h, self.r, self.cx, self.cy = z, w, h, r, cx, cy


def soft_loft(sts, mat="paint1", k=K, top=(), side=(), round0=0.0, round1=0.0, steps=3, plane="xy"):
    """A loft through rounded-rectangle sections along +Z (sts back to front); its ends rounded over round0 /
    round1 (a bevel radius: 0 = a flat cap) in `steps` rings. mat: a name or f(label, ring index, centroid).
    plane "xz": the loft goes up +Y instead (the stations' z is then the height, their cy is -z)."""
    labels = []

    def ring(st, inset=0.0, dz=0.0):
        w, h = max(st.w - inset, 0.015), max(st.h - inset, 0.015)
        rr = max(min(st.r - inset, w * 0.999, h * 0.999), 0.008)
        pts, lab = rrect(w, h, rr, k, top, side)
        labels[:] = lab
        return [(st.cx + x, st.cy + y, st.z + dz) for x, y in pts]
    rings = []
    if round0 > 0:
        for s in range(steps, 0, -1):
            a = PI / 2 * s / steps
            rings.append(ring(sts[0], round0 * (1 - math.cos(a)), -round0 * math.sin(a)))
    for st in sts:
        rings.append(ring(st))
    if round1 > 0:
        for s in range(1, steps + 1):
            a = PI / 2 * s / steps
            rings.append(ring(sts[-1], round1 * (1 - math.cos(a)), round1 * math.sin(a)))
    mf = mat if callable(mat) else (lambda lab, i, c: mat)
    labs = list(labels)

    def m2(i, j, c):
        return mf(labs[j % len(labs)], i, c)
    p = loft(rings, m2, cap0=True, cap1=True)
    if plane == "xz":
        p.v = np.stack([p.v[:, 0], p.v[:, 2], -p.v[:, 1]], axis=1)
        p.orient()
    return p


def soft_box(x0, x1, y0, y1, z0, z1, r, mat="paint1", k=2, top=(), side=()):
    """A box with every edge rounded by r."""
    w, h = (x1 - x0) / 2, (y1 - y0) / 2
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    r = min(r, w, h, (z1 - z0) / 2 - 1e-3)
    sts = [Station(z0 + r, w, h, r, cx, cy), Station(z1 - r, w, h, r, cx, cy)]
    return soft_loft(sts, mat, k, top, side, round0=r, round1=r, steps=k)


def revolve_z(profile, segs=14, mat="darkgrey", a0=0.0):
    """A surface of revolution around the Z axis: profile [(radius, z)] (radius 0: on the axis)."""
    p = revolve(profile, segs, mat, a0=a0)
    p.v = np.stack([p.v[:, 0], -p.v[:, 2], p.v[:, 1]], axis=1)
    return p.orient()


def soft_cyl(r, z0, z1, bevel=0.06, segs=14, mat="darkgrey", x=0.0, y=0.0, bevel0=None, bevel1=None, steps=2):
    """A cylinder along Z from z0 (back) to z1 (front), its two rims rounded. mat: a name or f(ring, ...)."""
    b0 = min(bevel if bevel0 is None else bevel0, r * 0.95)
    b1 = min(bevel if bevel1 is None else bevel1, r * 0.95)
    prof = [(0.0, z0)]
    for s in range(steps + 1):
        a = PI / 2 * s / steps
        prof.append((r - b0 + b0 * math.sin(a), z0 + b0 - b0 * math.cos(a)))
    for s in range(steps + 1):
        a = PI / 2 * s / steps
        prof.append((r - b1 + b1 * math.cos(a), z1 - b1 + b1 * math.sin(a)))
    prof.append((0.0, z1))
    return revolve_z(prof, segs, mat).tr(x, y, 0)


def nozzle(r, z0, z1, glow="cyan_glow", x=0.0, y=0.0, segs=14, depth=0.10, mat="darkgrey"):
    """A dark rounded nozzle from z1 (front) back to z0, its soft glowing core set into its back face."""
    b = min(0.08, r * 0.3)
    prof = [(0.0, z1), (r - b, z1), (r - b * 0.29, z1 - b * 0.29), (r, z1 - b), (r, z0 + b), (r - b * 0.29, z0 + b * 0.29),
            (r * 0.70, z0), (r * 0.62, z0 + depth), (0.0, z0 + depth)]
    n = len(prof)

    def mt(i, j, c):
        return glow if i >= n - 2 else mat
    return revolve_z(prof, segs, mt).tr(x, y, 0)


def soft_fin(outline, thick, mat="paint2", steps=2):
    """A fin: a 2D outline (u, v) extruded thick, its whole rim rounded (bevel = half the thickness)."""
    return slab(outline, thick, bevel=thick / 2, steps=steps, mat=mat)


def blob(r, mat="leaf", segs=8, rings=6, seed=1, jitter=0.12, sy=1.0):
    """A lumpy low-poly ball (a tree's canopy, a bush, a boulder, a cloud's puff)."""
    p = sphere(r, segs, rings, mat)
    rng = np.random.RandomState(seed)
    keys = {}
    for i, v in enumerate(p.v):
        k = tuple(np.round(v, 5))
        if k not in keys:
            keys[k] = 1.0 + rng.uniform(-jitter, jitter)
        p.v[i] = v * keys[k]
    p.v[:, 1] *= sy
    return p.orient()


def rotate_about(p, axis, deg, centre):
    c = np.array(centre, float)
    return p.tr(*(-c)).rot(axis, deg).tr(*c)
