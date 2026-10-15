#!/usr/bin/env python3
#
# ships.py -- Cosmo Zoom!'s ship parts, after the user's approved concept sheet (2026-10-15): six low-poly faceted
# racers -- crisp flat panels, chunky wedges, vivid flat colours, white and black racing stripes, dark-grey
# mechanics (nozzles, grilles, hover pads), dark tinted canopies, glowing nozzle cores:
#   hull_needle  "Nova Streak"   a long wedge, a flat nose with an intake mouth, racing stripes, a swept tail fin
#   hull_comet   "Comet Chaser"  a chunkier wider wedge, white stripes across the nose, twin tail fins
#   hull_whale   "Star Drifter"  a two-tone wedge (paint2 nose, paint1 back), a tail fin
#   hull_pod     "Plasma Runner" a small streamlined cockpit pod
#   hull_cube    "Ringmaster"    a chunky hull, a paint2 nose and stripes
#   hull_manta   "Manta Ray"     a wide flat delta with two engine pods built in at its wing ends
# The engines are the main variety: engine_ion (one big central jet), engine_pods (two boxy pods on short pylons at
# mid-length), engine_twin (two nacelles on lattice truss arms), engine_star (podracer nacelles out ahead on cables;
# the game draws the arc between #@ arc1 and #@ arc2), engine_ring (a big faceted ring standing over the tail).
# The wings: four small angular fins (stubby, ray, solar = "Delta", swept).
#
# Conventions (the game's): metres, +Y up, +Z forward, the ship's left is +X. A hull's origin is the ship's centre,
# y = 0 under its hover pads; sockets: wing (the +X wing's root; the game mirrors it), engine (the back's centre),
# pilot (the neck), pad1..pad4. All the hulls put the engine socket at y 0.42 and the wings high on the shoulders
# (y >= 0.6), so that the side engines (low) pass under the wings. A wing is made for the +X side, its root at the
# origin. An engine's origin is its front centre (at the hull's engine socket); flame1.. at its nozzles' exits.
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
#
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from czmodel import PI, Model, Part, loft, revolve, disc, tube  # noqa: E402

SCRIPT = "ships.py"
ENGINE_Y = 0.42

# ---- the faceted body: a loft of 16-point sections, straight between the stations ----------------------------------
# A section from the top centre round the +X side to the bottom centre and back: the flat deck in four strips (a
# centre stripe, a strip, a side stripe, the outer strip), the shoulder (xs, ys), the chine -- the widest -- (xc, yc),
# the bottom edge (xb, yb).
F1, F2, F3 = 0.14, 0.40, 0.60
RIGHT = ["dc", "d1", "ds", "d2", "upper", "side", "lower", "bottom"]
PANELS = RIGHT + RIGHT[::-1]


def section(row):
    z, yt, xd, xs, ys, xc, yc, xb, yb = row
    right = [(F1 * xd, yt), (F2 * xd, yt), (F3 * xd, yt), (xd, yt), (xs, ys), (xc, yc), (xb, yb)]
    ring = [(0.0, yt)] + right + [(0.0, yb)] + [(-x, y) for x, y in reversed(right)]
    return [(x, y, z) for x, y in ring]


def lerp_row(a, b, z):
    t = (z - a[0]) / (b[0] - a[0])
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(len(a)))


def stations(rows, extra=()):
    """The table's rows plus rows interpolated at the z in extra (stripe edges)."""
    rows = sorted(rows)
    out = list(rows)
    for z in extra:
        for a, b in zip(rows, rows[1:]):
            if a[0] < z < b[0]:
                out.append(lerp_row(a, b, z))
    return sorted(out)


def body(rows, matf, nose=None, mouth=None, back="darkgrey", extra=()):
    """The hull's body through the rows (z, yt, xd, xs, ys, xc, yc, xb, yb), back to front. nose: (z, y) an apex;
    else a flat front, with mouth = (scale, depth, inner material) an open intake mouth in it."""
    rows = stations(rows, extra)
    rings = [section(r) for r in rows]
    front_ring = rings[-1]
    if nose:
        rings.append([(0.0, nose[1], nose[0])])

    def mat(i, j, c):
        if i >= len(rows) - 1 and nose:
            return matf(PANELS[j % 16], c)
        return matf(PANELS[j % 16], c)
    p = loft(rings, mat, cap0=False, cap1=False, orient=False)
    fan(p, rings[0], back)
    if not nose:
        if mouth:
            sc, depth, inner = mouth
            fr = np.array(front_ring)
            c = fr.mean(axis=0)
            lip = [tuple(c + (q - c) * np.array([sc, sc, 1.0]) + np.array([0, 0, 0.0])) for q in fr]
            deep = [tuple(c + (q - c) * np.array([sc * 0.92, sc * 0.92, 1.0]) - np.array([0, 0, depth])) for q in fr]
            lipmat = matf("lip", c)
            q = loft([front_ring, lip], lipmat, cap0=False, cap1=False, orient=False)
            r = loft([lip, deep], inner, cap0=False, cap1=False, orient=False)
            p.add(q, r)
            fan(p, deep, inner)
        else:
            fan(p, front_ring, matf("lip", np.array(front_ring).mean(axis=0)))
    return p.orient()


def fan(p, ring, mat):
    r = np.array(ring, float)
    if len(r) < 3:
        return
    start = len(p.v)
    p.v = np.vstack([p.v, r, r.mean(axis=0)])
    n = len(r)
    for j in range(n):
        p.f.append(((start + j, start + (j + 1) % n, start + n), mat))


# ---- blades: fins and wings (hexagonal sections: crisp leading and trailing edges) ----------------------------------
def blade(sts, mat="paint2", root_cap=True):
    """Along +X: stations [(x, z_leading, chord, thick, y)] from the root; the tip a flat cut."""
    rings = []
    for (x, zl, c, t, y) in sts:
        h = t / 2
        rings.append([(x, y, zl), (x, y + h, zl - 0.25 * c), (x, y + h * 0.8, zl - 0.7 * c), (x, y, zl - c),
                      (x, y - h * 0.8, zl - 0.7 * c), (x, y - h, zl - 0.25 * c)])
    return loft(rings, mat, cap0=root_cap, cap1=True)


def fin(sts, mat="paint2", x=0.0, cant=0.0):
    """A vertical fin: stations [(height y, z_leading, chord, thick)] from its root up; cant in degrees about Z
    (positive leans it toward -X)."""
    b = blade([(y, zl, c, t, 0.0) for (y, zl, c, t) in sts], mat)
    b.rot("z", 90 + cant)
    return b.tr(x, 0, 0)


def canopy(z0, z1, zpk, ybase, top, hw, nz=5, mat="tint", sink=0.14):
    """A faceted tinted canopy on y = ybase: from z0 (back) to z1 (front), highest at zpk."""
    rings = [[(0.0, ybase + 0.02, z0)]]
    for k in range(1, nz):
        s = k / nz
        z = z0 + (z1 - z0) * s
        if z < zpk:
            f = math.sqrt(max(0.0, 1 - ((zpk - z) / (zpk - z0)) ** 2))
        else:
            f = max(0.0, 1 - ((z - zpk) / (z1 - zpk)) ** 1.4) ** 0.7
        w, h = hw * f, (top - ybase) * f
        rings.append([(w * 0.55, ybase + h, z), (w, ybase + h * 0.5, z), (w * 0.95, ybase - sink, z),
                      (-w * 0.95, ybase - sink, z), (-w, ybase + h * 0.5, z), (-w * 0.55, ybase + h, z)])
    rings.append([(0.0, ybase + 0.02, z1)])
    return loft(rings, mat, cap0=False, cap1=False)


def hover_pad(x, z, y_top=0.24, r=0.15):
    """A pad: a short grey cylinder under the hull + its glow disc, its bottom at y = 0."""
    h = revolve([(0.0, 0.03), (r * 0.85, 0.03), (r, 0.07), (r, y_top), (0.0, y_top)], 8, "grey", a0=PI / 8)
    g = disc(r * 0.62, 8, "plasma_glow", y=0.02, down=True)
    return h.tr(x, 0, z), g.tr(x, 0, z)


def pads(m, pts, y_top=0.24, r=0.15):
    for k, (x, z) in enumerate(pts):
        h, g = hover_pad(x, z, y_top, r)
        m.add(h)
        m.add(g)
        m.socket("pad%d" % (k + 1), x, 0.0, z)


def box(x0, x1, y0, y1, z0, z1, mat, chamfer=0.0):
    """An axis-aligned box, its top edges chamfered by `chamfer`."""
    c = chamfer
    bot = [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)]
    if c > 0:
        rings = [bot, [(x0, y1 - c, z0), (x1, y1 - c, z0), (x1, y1 - c, z1), (x0, y1 - c, z1)],
                 [(x0 + c, y1, z0 + c), (x1 - c, y1, z0 + c), (x1 - c, y1, z1 - c), (x0 + c, y1, z1 - c)]]
    else:
        rings = [bot, [(p[0], y1, p[2]) for p in bot]]
    return loft(rings, mat)


def oct_ring(cx, cy, z, w, h, ch):
    """A chamfered rectangle (8 points) in the XY plane at z: half width w, half height h, chamfer ch."""
    return [(cx + w, cy - h + ch, z), (cx + w, cy + h - ch, z), (cx + w - ch, cy + h, z), (cx - w + ch, cy + h, z),
            (cx - w, cy + h - ch, z), (cx - w, cy - h + ch, z), (cx - w + ch, cy - h, z), (cx + w - ch, cy - h, z)]


def finish(m, wing, pilot, pad_pts, pad_r=0.15, z_back=None):
    pads(m, pad_pts, r=pad_r)
    m.socket("wing", *wing)
    m.socket("engine", 0.0, ENGINE_Y, z_back)
    m.socket("pilot", *pilot)
    return m


# ---- the hulls ---------------------------------------------------------------------------------------------------
def hull_needle():
    m = Model("hull_needle", "Nova Streak: a long wedge, a flat paint2 nose with an intake mouth, racing stripes, a swept fin")
    rows = [(-1.70, 0.70, 0.30, 0.50, 0.60, 0.56, 0.34, 0.46, 0.16),
            (-1.20, 0.75, 0.32, 0.54, 0.64, 0.60, 0.34, 0.50, 0.13),
            (-0.30, 0.71, 0.30, 0.52, 0.60, 0.58, 0.32, 0.48, 0.13),
            (0.60, 0.57, 0.26, 0.44, 0.48, 0.50, 0.28, 0.42, 0.14),
            (1.50, 0.43, 0.22, 0.34, 0.37, 0.40, 0.24, 0.34, 0.16),
            (2.10, 0.37, 0.20, 0.30, 0.32, 0.35, 0.22, 0.30, 0.18)]

    def mat(panel, c):
        x, y, z = c
        if panel == "bottom":
            return "darkgrey"
        if panel == "lip":
            return "paint2"
        if panel in ("dc",) and z > -0.3:
            return "ink"
        if panel == "ds" and z > -0.3:
            return "white"
        if z > 1.05:
            return "paint2"
        if panel == "side" and -1.15 < z < -0.45:
            return "darkgrey"                   # the side grille
        return "paint1"
    m.add(body(rows, mat, mouth=(0.70, 0.22, "darkgrey"), extra=(1.05,)))
    # grille slats over the side vents
    for sx in (1, -1):
        for k in range(3):
            z = -1.05 + 0.18 * k
            b = box(0.0, 0.04, 0.30, 0.52, z - 0.03, z + 0.03, "grey")
            m.add(b.tr(0.58, 0, 0) if sx > 0 else b.mirror_x().tr(-0.58, 0, 0))
    m.add(fin([(0.66, -0.95, 0.80, 0.09), (1.10, -1.40, 0.56, 0.07), (1.42, -1.78, 0.36, 0.05)], "paint2"))
    m.add(canopy(-0.95, 0.45, -0.40, 0.70, 1.10, 0.32))
    return finish(m, (0.50, 0.62, -0.75), (0.0, 0.70, -0.45),
                  [(0.30, 1.10), (-0.30, 1.10), (0.36, -1.15), (-0.36, -1.15)], 0.14, -1.68)


def hull_comet():
    m = Model("hull_comet", "Comet Chaser: a chunky wide wedge, an intake mouth, white stripes across the nose, twin fins")
    rows = [(-1.50, 0.78, 0.36, 0.62, 0.68, 0.70, 0.38, 0.58, 0.16),
            (-0.80, 0.82, 0.40, 0.70, 0.71, 0.80, 0.38, 0.66, 0.13),
            (0.20, 0.74, 0.38, 0.66, 0.62, 0.76, 0.34, 0.62, 0.13),
            (1.20, 0.54, 0.32, 0.52, 0.46, 0.60, 0.28, 0.50, 0.15),
            (1.90, 0.42, 0.28, 0.44, 0.38, 0.50, 0.25, 0.42, 0.18)]
    bands = [(1.12, 1.30), (1.44, 1.62)]

    def mat(panel, c):
        x, y, z = c
        if panel == "bottom":
            return "darkgrey"
        if panel == "lip":
            return "white"
        if panel in ("dc", "d1", "ds", "d2", "upper") and any(a < z < b for a, b in bands):
            return "white"
        if panel == "lower":
            return "paint2"
        if panel == "side" and -1.05 < z < -0.35:
            return "darkgrey"
        return "paint1"
    m.add(body(rows, mat, mouth=(0.66, 0.20, "darkgrey"), extra=(1.12, 1.30, 1.44, 1.62)))
    for sx in (1, -1):
        m.add(fin([(0.74, -0.85, 0.62, 0.08), (1.02, -1.15, 0.44, 0.07), (1.20, -1.38, 0.30, 0.05)], "paint2",
                  x=0.26 * sx, cant=-14 * sx))
    m.add(canopy(-0.80, 0.70, -0.25, 0.80, 1.20, 0.34))
    return finish(m, (0.62, 0.66, -0.70), (0.0, 0.78, -0.30),
                  [(0.42, 1.05), (-0.42, 1.05), (0.46, -1.05), (-0.46, -1.05)], 0.15, -1.48)


def hull_whale():
    m = Model("hull_whale", "Star Drifter: a two-tone wedge (a paint2 nose, a paint1 back), a tail fin")
    rows = [(-1.60, 0.80, 0.34, 0.60, 0.70, 0.68, 0.38, 0.56, 0.16),
            (-0.90, 0.86, 0.40, 0.70, 0.74, 0.80, 0.38, 0.66, 0.13),
            (0.10, 0.78, 0.38, 0.68, 0.66, 0.78, 0.34, 0.64, 0.13),
            (1.10, 0.58, 0.32, 0.56, 0.50, 0.64, 0.28, 0.54, 0.15),
            (1.80, 0.44, 0.28, 0.46, 0.40, 0.54, 0.25, 0.46, 0.18)]

    def mat(panel, c):
        x, y, z = c
        if panel == "bottom":
            return "darkgrey"
        if panel == "lip":
            return "darkgrey"
        if panel == "dc" and z > 0.35:
            return "ink"
        if panel == "ds" and z < 0.35:
            return "white"
        if z > 0.35:
            return "paint2"
        if panel == "side" and -1.2 < z < -0.5:
            return "darkgrey"
        return "paint1"
    m.add(body(rows, mat, mouth=(0.62, 0.18, "darkgrey"), extra=(0.35,)))
    m.add(fin([(0.80, -0.98, 0.80, 0.10), (1.20, -1.38, 0.56, 0.08), (1.48, -1.70, 0.36, 0.06)], "paint2"))
    m.add(canopy(-0.75, 0.75, -0.15, 0.82, 1.24, 0.35))
    return finish(m, (0.62, 0.70, -0.85), (0.0, 0.80, -0.22),
                  [(0.44, 1.05), (-0.44, 1.05), (0.48, -1.15), (-0.48, -1.15)], 0.16, -1.58)


def hull_pod():
    m = Model("hull_pod", "Plasma Runner: a small streamlined cockpit pod, a pointed paint2 nose")
    rows = [(-1.20, 0.66, 0.20, 0.42, 0.58, 0.50, 0.34, 0.38, 0.18),
            (-0.70, 0.80, 0.24, 0.50, 0.68, 0.56, 0.34, 0.44, 0.14),
            (0.10, 0.78, 0.24, 0.50, 0.64, 0.56, 0.32, 0.44, 0.14),
            (0.80, 0.58, 0.18, 0.40, 0.48, 0.46, 0.28, 0.36, 0.16),
            (1.40, 0.38, 0.08, 0.18, 0.32, 0.22, 0.26, 0.14, 0.22)]

    def mat(panel, c):
        x, y, z = c
        if panel == "bottom":
            return "darkgrey"
        if z > 0.9:
            return "paint2"
        if panel in ("dc",):
            return "paint2"
        if panel == "ds":
            return "white"
        if panel == "lower":
            return "paint2"
        return "paint1"
    m.add(body(rows, mat, nose=(1.68, 0.28), extra=(0.9,)))
    m.add(fin([(0.62, -0.72, 0.56, 0.08), (0.95, -1.00, 0.40, 0.07), (1.15, -1.20, 0.26, 0.05)], "paint2"))
    m.add(canopy(-0.55, 0.65, 0.0, 0.76, 1.18, 0.33))
    return finish(m, (0.44, 0.60, -0.55), (0.0, 0.74, -0.02),
                  [(0.30, 0.75), (-0.30, 0.75), (0.34, -0.80), (-0.34, -0.80)], 0.13, -1.18)


def hull_cube():
    m = Model("hull_cube", "Ringmaster: a chunky hull, a paint2 nose and stripes")
    rows = [(-1.50, 0.86, 0.44, 0.70, 0.76, 0.76, 0.40, 0.64, 0.16),
            (-0.70, 0.90, 0.48, 0.76, 0.78, 0.84, 0.40, 0.70, 0.13),
            (0.40, 0.84, 0.46, 0.74, 0.72, 0.82, 0.36, 0.68, 0.13),
            (1.20, 0.60, 0.36, 0.58, 0.52, 0.66, 0.30, 0.56, 0.15),
            (1.75, 0.46, 0.30, 0.48, 0.40, 0.56, 0.26, 0.48, 0.18)]

    def mat(panel, c):
        x, y, z = c
        if panel == "bottom":
            return "darkgrey"
        if panel == "lip":
            return "paint2"
        if z > 0.95:
            return "paint2"
        if panel in ("d1", "d2") and z < 0.95:
            return "paint1"
        if panel in ("dc", "ds"):
            return "paint2"
        if panel == "lower":
            return "ink"
        return "paint1"
    m.add(body(rows, mat, mouth=(0.64, 0.18, "darkgrey"), extra=(0.95,)))
    m.add(canopy(-0.70, 0.75, -0.05, 0.88, 1.30, 0.36))
    m.add(fin([(0.86, -0.95, 0.56, 0.09), (1.18, -1.20, 0.40, 0.07), (1.36, -1.40, 0.26, 0.05)], "paint2"))
    return finish(m, (0.66, 0.74, -0.80), (0.0, 0.86, -0.10),
                  [(0.48, 0.95), (-0.48, 0.95), (0.52, -1.05), (-0.52, -1.05)], 0.16, -1.48)


def side_pod(x, y, z0, z1, w, h, body_mat="paint2", glow="cyan_glow"):
    """A boxy engine pod along -Z from z0 (front) to z1 (back): a chamfered box, a glowing intake in front, a
    dark nozzle and its glowing core behind."""
    ch = min(w, h) * 0.35
    rings = [oct_ring(x, y, z0, w * 0.9, h * 0.9, ch * 0.9), oct_ring(x, y, z0 - 0.06, w, h, ch),
             oct_ring(x, y, z0 - (z0 - z1) * 0.45, w, h, ch), oct_ring(x, y, z0 - (z0 - z1) * 0.55, w, h, ch),
             oct_ring(x, y, z1 + 0.10, w, h, ch), oct_ring(x, y, z1, w * 0.8, h * 0.8, ch * 0.8)]

    def mat(i, j, c):
        if i == 0:
            return "grey"
        if i == 2:
            return "white"
        if i == 4:
            return "darkgrey"
        return body_mat
    p = loft(rings, mat, cap0=False, cap1=False, orient=False)
    fan(p, rings[0], glow)
    fan(p, rings[-1], "darkgrey")
    p.orient()
    # the nozzle: a short dark ring, its glowing core
    noz = revolve([(0.0, 0.0), (min(w, h) * 0.75, 0.0), (min(w, h) * 0.7, 0.16), (0.0, 0.16)], 8, "darkgrey", a0=PI / 8)
    noz.rot("x", -90).tr(x, y, z1)
    core = disc(min(w, h) * 0.5, 8, glow, y=0.0, down=True).rot("x", -90).tr(x, y, z1 - 0.165)
    return p.add(noz).add(core)


def hull_manta():
    m = Model("hull_manta", "Manta Ray: a wide flat delta, paint2 trims and nose, two engine pods at its wing ends")
    rows = [(-1.40, 0.50, 0.30, 1.05, 0.40, 1.20, 0.28, 0.80, 0.18),
            (-0.60, 0.54, 0.30, 0.90, 0.42, 1.02, 0.28, 0.70, 0.14),
            (0.40, 0.48, 0.26, 0.55, 0.40, 0.62, 0.26, 0.45, 0.14),
            (1.30, 0.36, 0.14, 0.24, 0.32, 0.28, 0.24, 0.20, 0.18)]

    def mat(panel, c):
        x, y, z = c
        if panel == "bottom":
            return "darkgrey"
        if z > 0.85:
            return "paint2"
        if panel in ("side", "lower"):
            return "paint2"
        if panel == "dc":
            return "paint2"
        if panel == "ds":
            return "white"
        return "paint1"
    m.add(body(rows, mat, nose=(1.72, 0.26), extra=(0.85,)))
    for sx in (1, -1):
        m.add(side_pod(1.22 * sx, 0.30, -0.20, -1.60, 0.20, 0.17, "paint2", "cyan_glow"))
        m.socket("flame%d" % (3 if sx > 0 else 4), 1.22 * sx, 0.30, -1.78)
    m.add(fin([(0.50, -0.80, 0.70, 0.08), (0.88, -1.12, 0.48, 0.07), (1.10, -1.36, 0.30, 0.05)], "paint1"))
    m.add(canopy(-0.70, 0.55, -0.25, 0.52, 0.94, 0.30, mat="tint"))
    return finish(m, (0.95, 0.48, -0.35), (0.0, 0.52, -0.30),
                  [(0.40, 0.75), (-0.40, 0.75), (0.62, -0.95), (-0.62, -0.95)], 0.15, -1.38)


# ---- the wings ---------------------------------------------------------------------------------------------------
def tip_light(x, y, z, mat="yellow_glow", s=0.055):
    """A small lozenge light at a wing's tip (pointing along Z)."""
    return revolve([(0.0, -s * 1.6), (s, 0.0), (0.0, s * 1.6)], 6, mat).rot("x", 90).tr(x, y, z)


def wing_swept():
    m = Model("wing_swept", "Swept Fins: swept-back angular blades, a white stripe, a lemon tip light")
    st = [(-0.12, 0.40, 0.95, 0.10, 0.00), (0.45, -0.02, 0.70, 0.08, 0.02), (0.62, -0.15, 0.60, 0.075, 0.03),
          (0.95, -0.42, 0.36, 0.06, 0.05)]

    def mat(i, j, c):
        return "white" if i == 1 else "paint2"
    m.add(blade(st, mat))
    m.add(tip_light(0.97, 0.05, -0.42 - 0.18))
    return m


def wing_stubby():
    m = Model("wing_stubby", "Stubby Fins: short thick trapezoids with a little end plate")
    st = [(-0.12, 0.34, 0.95, 0.13, 0.0), (0.22, 0.22, 0.80, 0.11, 0.0), (0.46, 0.08, 0.60, 0.09, 0.0)]
    m.add(blade(st, lambda i, j, c: "ink" if i == 1 and j in (1, 2) else "paint2"))
    f = fin([(-0.12, 0.14, 0.64, 0.06), (0.0, 0.10, 0.60, 0.07), (0.18, 0.0, 0.42, 0.05)], "paint1")
    m.add(f.tr(0.49, 0, 0))
    return m


def wing_ray():
    m = Model("wing_ray", "Ray Wings: long straight blades, a white band, lemon tip lights")
    st = [(-0.12, 0.26, 0.66, 0.10, 0.0), (0.55, 0.16, 0.52, 0.075, 0.0), (0.75, 0.13, 0.48, 0.07, 0.0),
          (1.30, 0.04, 0.34, 0.05, 0.0)]
    m.add(blade(st, lambda i, j, c: "white" if i == 1 else "paint2"))
    m.add(tip_light(1.32, 0.0, 0.04 - 0.17))
    return m


def wing_solar():
    m = Model("wing_solar", "Delta Fins: a flat delta with a white panel and a black stripe")
    st = [(-0.12, 0.55, 1.25, 0.10, 0.0), (0.35, 0.15, 0.85, 0.08, 0.0), (0.52, 0.01, 0.70, 0.07, 0.0),
          (0.85, -0.30, 0.38, 0.05, 0.0)]

    def mat(i, j, c):
        if i == 1:
            return "ink"
        return "paint2"
    m.add(blade(st, mat))
    m.add(tip_light(0.87, 0.0, -0.30 - 0.20, "cyan_glow"))
    return m


# ---- the engines ---------------------------------------------------------------------------------------------------
def along_minus_z(p):
    """A part revolved around +Y turned to lie along -Z (+Y -> -Z)."""
    return p.rot("x", -90)


def by_ring(mats):
    def mat(i, j, c):
        for upto, mt in mats:
            if i < upto:
                return mt
        return mats[-1][1]
    return mat


def nacelle(L, R, segs=8, main="paint1", band="white", stripe="paint2", front_glow="purple_glow", back_glow="purple_glow",
            fins=True):
    """A faceted engine nacelle along -Z from its front (z = 0) to its back (z = -L): a glowing intake behind a
    dark lip, banded panels, small fins, the rear nozzle and its glowing core."""
    prof = [(0.0, 0.18 * R), (0.70 * R, 0.12 * R), (0.84 * R, -0.02 * R), (R, 0.10 * R),     # intake, lip
            (R, 0.22 * L), (R, 0.30 * L), (R, 0.58 * L), (R, 0.66 * L),                       # bands
            (R, L - 0.40 * R), (0.86 * R, L - 0.22 * R), (0.80 * R, L), (0.0, L - 0.05 * R)]
    mats = [front_glow, "darkgrey", "darkgrey", main, band, main, stripe, main, "darkgrey", "darkgrey", back_glow]

    def mat(i, j, c):
        return mats[min(i, len(mats) - 1)]
    p = along_minus_z(revolve(prof, segs, mat, a0=PI / segs, cap0=False, cap1=False))
    if fins:
        for sy in (1, -1):
            f = fin([(R * 0.85, -L * 0.62, L * 0.30, 0.06), (R * 1.55, -L * 0.74, L * 0.20, 0.05)], stripe)
            if sy < 0:
                f.sc(1, -1, 1)
            p.add(f)
    return p


def mount_block(z1=-1.0, w=0.32, h=0.26):
    """A central block on the hull's back for the arms (its front sunk into the hull), a nozzle stub behind."""
    def r(z, k):
        return oct_ring(0.0, 0.0, z, w * k, h * k, 0.08 * k)
    p = loft([r(0.40, 1.0), r(z1 + 0.15, 1.0), r(z1, 0.75)], by_ring([(1, "darkgrey"), (99, "darkgrey")]))
    return p


def engine_ion():
    m = Model("engine_ion", "Ion Jet: one big central jet, a grey nozzle with a purple core")
    prof = [(0.0, -0.20), (0.40, -0.20), (0.46, 0.0), (0.46, 0.22), (0.50, 0.30), (0.50, 0.42), (0.46, 0.48),
            (0.44, 0.78), (0.48, 0.86), (0.40, 0.88), (0.0, 0.80)]

    def mat(i, j, c):
        return ["grey", "grey", "darkgrey", "paint2", "paint2", "darkgrey", "grey", "grey", "darkgrey", "purple_glow"][min(i, 9)]
    m.add(along_minus_z(revolve(prof, 12, mat, a0=PI / 12, cap0=False, cap1=False)))
    m.socket("flame1", 0.0, 0.0, -0.90)
    return m


def engine_pods():
    m = Model("engine_pods", "Side Pods: two boxy engine pods on short pylons along the hull's sides")
    X, Y, Z0, Z1 = 1.10, -0.14, 2.10, 0.45
    for sx in (1, -1):
        m.add(side_pod(X * sx, Y, Z0, Z1, 0.26, 0.24, "paint1", "cyan_glow"))
        # the pylons: two short blades into the hull
        for zc in (1.55, 0.95):
            b = blade([(0.25, zc + 0.16, 0.40, 0.08, Y), (X - 0.2, zc + 0.14, 0.36, 0.07, Y)], "darkgrey")
            m.add(b if sx > 0 else b.mirror_x())
        m.socket("flame%d" % (1 if sx > 0 else 2), X * sx, Y, Z1 - 0.18)
    m.add(along_minus_z(revolve([(0.0, -0.10), (0.30, -0.10), (0.34, 0.06), (0.28, 0.16), (0.0, 0.14)], 8,
                                by_ring([(3, "darkgrey"), (99, "cyan_glow")]), a0=PI / 8)))
    return m


def engine_twin():
    m = Model("engine_twin", "Twin Thrusters: two nacelles held out on lattice truss arms")
    X, Y, Z0, L, R = 1.42, -0.12, 0.70, 2.0, 0.32
    m.add(mount_block(-1.10))
    for sx in (1, -1):
        part = Part()
        part.add(nacelle(L, R, 8, main="paint1", band="white", stripe="paint2", front_glow="cyan_glow",
                         back_glow="purple_glow").tr(X, Y, Z0))
        # the truss: two arms, each two bars and a zigzag of struts between them
        for zc in (0.15, -0.75):
            for dy in (0.11, -0.11):
                part.add(tube([(0.20, Y + dy, zc), (X - R * 0.9, Y + dy, zc)], 0.035, 4, "grey", cap0=False))
            xs = np.linspace(0.25, X - R * 0.95, 5)
            for k in range(4):
                a = (xs[k], Y + (0.11 if k % 2 == 0 else -0.11), zc)
                b = (xs[k + 1], Y + (-0.11 if k % 2 == 0 else 0.11), zc)
                part.add(tube([a, b], 0.022, 4, "grey", cap0=False))
        if sx < 0:
            part.mirror_x()
        m.add(part)
        m.socket("flame%d" % (1 if sx > 0 else 2), sx * X, Y, Z0 - L - 0.02)
    return m


def engine_star():
    m = Model("engine_star", "Star Drive: two long podracer nacelles out ahead, pulling the hull on cables")
    X, Y, Z0, L, R = 1.62, 0.0, 5.00, 2.60, 0.34
    for sx in (1, -1):
        part = Part()
        part.add(nacelle(L, R, 8, main="paint1", band="paint2", stripe="white", front_glow="purple_glow",
                         back_glow="cyan_glow").tr(X, Y, Z0))
        a = np.array([X - R * 0.75, Y, Z0 - L + 0.30])
        b = np.array([0.24, Y, 2.30])
        mid = (a + b) / 2 + np.array([0, -0.12, 0])
        part.add(tube([a, (a + mid) / 2 + np.array([0, -0.04, 0]), mid, (mid + b) / 2 + np.array([0, -0.04, 0]), b],
                      0.03, 4, "darkgrey"))
        part.add(box(X - R * 1.02 - 0.05, X - R * 0.7, Y - 0.10, Y + 0.10, a[2] - 0.12, a[2] + 0.12, "grey", 0.02))
        if sx < 0:
            part.mirror_x()
        m.add(part)
        m.socket("flame%d" % (1 if sx > 0 else 2), sx * X, Y, Z0 - L - 0.02)
        m.socket("arc%d" % (1 if sx > 0 else 2), sx * (X - R), Y, Z0 - L * 0.32)
    m.add(along_minus_z(revolve([(0.0, -0.10), (0.30, -0.10), (0.34, 0.06), (0.28, 0.16), (0.0, 0.14)], 8,
                                by_ring([(3, "darkgrey"), (99, "cyan_glow")]), a0=PI / 8)))
    return m


def engine_ring():
    m = Model("engine_ring", "Ring Drive: a big faceted ring standing over the tail, a core nozzle")
    # the core nozzle
    prof = [(0.0, -0.20), (0.32, -0.20), (0.36, 0.0), (0.36, 0.40), (0.40, 0.48), (0.30, 0.52), (0.0, 0.46)]
    m.add(along_minus_z(revolve(prof, 8, by_ring([(2, "grey"), (3, "paint2"), (4, "darkgrey"), (99, "magenta_glow")]),
                                a0=PI / 8, cap0=False, cap1=False)))
    # the ring: 12 faceted segments (a square section), paint1 / paint2 in turn, standing across the flight path
    RC, cy, cz, half_w, half_d = 1.02, 0.30, -0.45, 0.10, 0.16
    segs = 12
    ring = Part()
    for k in range(segs):
        a0, a1 = 2 * PI * k / segs, 2 * PI * (k + 1) / segs

        def corner(a, rr, dz):
            return (rr * math.cos(a), cy + rr * math.sin(a), cz + dz)
        q = [corner(a0, RC - half_w, -half_d), corner(a0, RC + half_w, -half_d), corner(a0, RC + half_w, half_d),
             corner(a0, RC - half_w, half_d)]
        r = [corner(a1, RC - half_w, -half_d), corner(a1, RC + half_w, -half_d), corner(a1, RC + half_w, half_d),
             corner(a1, RC - half_w, half_d)]
        mt = "paint2" if k % 2 == 0 else "paint1"
        seg = loft([q, r], mt, cap0=False, cap1=False, orient=False)
        ring.add(seg)
    ring = fix_out_torus(ring, (0, cy, cz), RC)
    # its lights: four small glowing studs on its front face
    for k in range(4):
        a = PI / 4 + PI / 2 * k
        ring.add(revolve([(0.0, 0.0), (0.06, 0.0), (0.0, 0.04)], 6, "magenta_glow").rot("x", 90)
                 .tr(RC * math.cos(a), cy + RC * math.sin(a), cz + half_d))
    m.add(ring, "ring")
    # the two struts from the hull's back to the ring's sides
    for sx in (1, -1):
        m.add(blade([(0.25, cz + 0.30, 0.44, 0.08, 0.0), (RC - half_w, cz + 0.14, 0.26, 0.07, cy * 0.0)], "darkgrey")
              if sx > 0 else blade([(0.25, cz + 0.30, 0.44, 0.08, 0.0), (RC - half_w, cz + 0.14, 0.26, 0.07, 0.0)],
                                   "darkgrey").mirror_x())
    m.socket("ring", 0.0, cy, cz)
    m.socket("flame1", 0.0, 0.0, -0.54)
    return m


def fix_out_torus(p, centre, R):
    """Faces of a ring turned away from its section's centre line (a circle of radius R around Z at centre)."""
    C = np.array(centre, float)
    out = []
    for idx, mt in p.f:
        P = p.v[list(idx)]
        c = P.mean(axis=0) - C
        n = np.cross(P[1] - P[0], P[2] - P[0])
        rr = math.hypot(c[0], c[1])
        on = np.array([c[0] / rr * R, c[1] / rr * R, 0.0])
        if np.dot(n, c - on) < 0:
            idx = tuple(reversed(idx))
        out.append((idx, mt))
    p.f = out
    return p


ALL = {
    "hull_needle": hull_needle,
    "hull_comet": hull_comet,
    "hull_whale": hull_whale,
    "hull_pod": hull_pod,
    "hull_cube": hull_cube,
    "hull_manta": hull_manta,
    "wing_stubby": wing_stubby,
    "wing_ray": wing_ray,
    "wing_solar": wing_solar,
    "wing_swept": wing_swept,
    "engine_ion": engine_ion,
    "engine_pods": engine_pods,
    "engine_twin": engine_twin,
    "engine_star": engine_star,
    "engine_ring": engine_ring,
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
    from czmodel import write_mtl
    write_mtl()
    for n, (t, _) in build(sys.argv[1:] or None).items():
        print(n, t)
