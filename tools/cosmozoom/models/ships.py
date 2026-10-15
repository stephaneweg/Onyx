#!/usr/bin/env python3
#
# ships.py -- Cosmo Zoom!'s ship parts after the user's reference sheet (2026-10-15, DESIGN.md section 4):
# chamfered low-poly shapes (crisp 45-degree bevels, flat faces), soft light, no outline, solid vivid colours,
# slate-navy canopies and intakes, glowing cores in the nozzles only. The six ships of the sheet:
#   hull_needle  "Nova Streak"   a faceted arrowhead, a paint2 nose tip and side panels, white pinstripes
#   hull_comet   "Comet Chaser"  a narrow fuselage, a stepped cabin (+ engine_pods: two long boxes at its sides)
#   hull_whale   "Star Drifter"  a long fuselage, a paint2 stripe (+ engine_twin: big nacelles, lattice booms ahead)
#   hull_pod     "Plasma Runner" a small cockpit block (+ engine_star: two chunky octagonal pods ahead, on cables)
#   hull_cube    "Ringmaster"    an octagonal torpedo, a paint2 nose cone (+ engine_ring: a big ring round its back)
#   hull_manta   "Manta Ray"     a wide flat delta, a slate intake in front, boxy pods at its tips
# The wings: four small chamfered fins. The engines: slate nozzles with a soft glowing core.
#
# Conventions (the game's): metres, +Y up, +Z forward, the ship's left is +X. A hull's origin is the ship's centre,
# y = 0 under its hover pads; sockets: wing (the +X wing's root; the game mirrors it), engine (the back's centre,
# always at y 0.42), pilot (the neck), pad1..pad4. A wing is made for the +X side, its root at the origin. An
# engine's origin is its front centre (at the hull's engine socket); flame1.. at its nozzles' exits.
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
#
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from czmodel import PI, Model, Part, loft, revolve, disc, tube, torus, round_corners, sphere  # noqa: E402
from soft import Station as S, soft_loft, soft_box, soft_cyl, nozzle, soft_fin, revolve_z  # noqa: E402

SCRIPT = "ships.py"
ENGINE_Y = 0.42


# ---- shared helpers (also used by city.py, items.py, pilots.py) -------------------------------------------
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




def pad(x, z, r=0.15, h=0.16):
    """A hover pad: a small rounded dark puck under the hull (its glow is drawn by the game)."""
    prof = [(0.0, 0.02), (r - 0.04, 0.02), (r, 0.06), (r, h), (0.0, h)]
    return revolve(prof, 6, "darkgrey", a0=PI / 6).tr(x, 0, z)


def finish(m, wing, pilot, pad_pts, z_back, pad_r=0.15):
    for k, (x, z) in enumerate(pad_pts):
        # (the pads are not modelled any more: the game draws their glow at these points, under the hull)
        m.socket("pad%d" % (k + 1), x, 0.0, z)
    m.socket("wing", *wing)
    m.socket("engine", 0.0, ENGINE_Y, z_back)
    m.socket("pilot", *pilot)
    return m


# ---- the hulls (the user's reference sheet, 2026-10-15: chamfered low-poly, slate-navy canopies and intakes) -------
SL = "slate"      # the canopies' and the intakes' slate navy


def ch_box(x0, x1, y0, y1, z0, z1, c, mat="paint1", top=(), side=()):
    """A box with every edge chamfered by c (45-degree flats: crisp low-poly bevels)."""
    return soft_box(x0, x1, y0, y1, z0, z1, c, mat, k=1, top=top, side=side)


def ch_loft(sts, mat="paint1", top=(), side=(), c0=0.06, c1=0.06):
    """A loft through chamfered sections (k = 1), its two ends chamfered by c0 / c1."""
    return soft_loft(sts, mat, 1, top=top, side=side, round0=c0, round1=c1, steps=1)


def canopy(z0, z1, ybot, top, w, peak=0.35, mat="tint", sts=4):
    """A faceted canopy: a chamfered loft from z0 to z1 whose top rises to `top` at the fraction `peak` of its
    length; its bottom (ybot) sunk into the hull."""
    st = []
    for i in range(sts):
        f = i / (sts - 1)
        z = z0 + (z1 - z0) * f
        if f <= peak:
            k = math.sin(PI / 2 * (0.35 + 0.65 * f / peak))
        else:
            k = math.cos(PI / 2 * (f - peak) / (1 - peak)) ** 0.8
        k = max(k, 0.25)
        h = (top - ybot) * k / 2
        st.append(S(z, w * (0.75 + 0.25 * k), h, min(w, h) * 0.7, 0.0, ybot + h))
    return ch_loft(st, mat, c0=0.05, c1=0.05)


# (the planforms measured on the user's top view of the six ships, the heights on their side view, 2026-10-15)
def hull_needle():
    m = Model("hull_needle", "Nova Streak: a faceted arrowhead widest near its back, a blunt paint2 nose, white pinstripes")
    # (z, half width, bottom, top): the top a ridge, the bottom flat
    prof = [(2.25, 0.31, 0.36, 0.64), (1.37, 0.49, 0.31, 0.76), (0.0, 0.92, 0.27, 0.90), (-1.46, 1.385, 0.17, 1.10),
            (-1.84, 1.00, 0.15, 1.08)]
    slope = [0.0, 0.40, 0.45, 0.50, 0.55, 1.0]        # from the shoulder (0) up to the ridge (1)
    rings, labs = [], []
    for z, w, yb, yt in prof[::-1]:
        h = yt - yb
        sh = (w * 0.95, yb + 0.32 * h)
        rt = (w * 0.15, yt)
        r, lab = [(w, yb), sh], ["side", "slope0"]
        for k in range(1, len(slope) - 1):
            t = slope[k]
            r.append((sh[0] + (rt[0] - sh[0]) * t, sh[1] + (rt[1] - sh[1]) * t)); lab.append("slope%d" % k)
        r.append(rt); lab.append("ridge")
        left = [(-x, y) for x, y in r[::-1]]
        llab = ["slope%d" % (len(slope) - 2 - k) for k in range(len(slope) - 1)] + ["side", "bottom"]
        ring = r + left
        labs = lab + llab
        rings.append([(x, y, z) for x, y in ring])

    def mat(i, j, c):
        lab = labs[j % len(labs)]
        if c[2] > 1.37:
            return "paint2"                         # the blunt nose
        if lab == "ridge" and 0.22 < c[2] < 1.37:
            return "paint2"                         # the panel on the ridge
        if lab in ("slope1", "slope3") and -1.0 < c[2] < 1.37:
            return "white"                          # the pinstripes
        if lab == "bottom":
            return SL
        return "paint1"
    m.add(loft(rings, mat))
    m.add(canopy(-0.95, 0.00, 0.86, 1.22, 0.30, 0.40))
    m.add(ch_box(-0.20, 0.20, 0.95, 1.16, -1.86, -0.95, 0.06, "paint1"))
    return finish(m, (1.20, 0.30, -1.30), (0.0, 0.76, -0.45),
                  [(0.30, 1.40), (-0.30, 1.40), (0.90, -1.30), (-0.90, -1.30)], -1.84)


def hull_comet():
    m = Model("hull_comet", "Comet Chaser: a narrow neck and nose ahead of a wide rear block, a slate canopy")
    neck = [S(0.25, 0.44, 0.27, 0.09, 0, 0.92), S(1.50, 0.42, 0.26, 0.09, 0, 0.88), S(2.00, 0.30, 0.18, 0.07, 0, 0.80)]
    m.add(ch_loft(neck, lambda lab, i, c: SL if lab == "bottom" else "paint1", c0=0.04, c1=0.06))
    rear = [S(-2.00, 0.86, 0.40, 0.12, 0, 0.85), S(0.35, 0.86, 0.40, 0.12, 0, 0.85)]
    m.add(ch_loft(rear, lambda lab, i, c: SL if lab == "bottom" else "paint1", c0=0.08, c1=0.16))
    m.add(canopy(-0.70, 0.28, 1.18, 1.48, 0.24, 0.30))
    m.add(ch_box(-0.33, 0.33, 1.20, 1.36, -1.58, -0.82, 0.05, "paint1"))
    return finish(m, (0.86, 0.60, -1.20), (0.0, 1.02, -0.20),
                  [(0.50, 1.20), (-0.50, 1.20), (0.60, -1.50), (-0.60, -1.50)], -2.00)


def hull_whale():
    m = Model("hull_whale", "Star Drifter: a long thin pointed fuselage, a paint2 stripe down its back, yellow struts")
    sts = [S(-1.91, 0.41, 0.24, 0.10, 0, 0.55), S(1.00, 0.41, 0.24, 0.10, 0, 0.55), S(1.70, 0.25, 0.17, 0.07, 0, 0.52),
           S(2.10, 0.08, 0.08, 0.03, 0, 0.50)]

    def mat(lab, i, c):
        if lab == "bottom":
            return SL
        if lab == "top1" and -1.09 < c[2] < 1.66:
            return "paint2"                         # the stripe down the back
        return "paint1"
    m.add(ch_loft(sts, mat, top=(0.30, 0.70), c0=0.06, c1=0.02))
    m.add(canopy(-0.85, -0.10, 0.72, 1.00, 0.20, 0.40))
    m.add(ch_box(-0.05, 0.05, 0.10, 0.34, -1.40, -0.90, 0.02, "paint1"))      # a little keel below
    return finish(m, (0.41, 0.55, -1.20), (0.0, 0.54, -0.45),
                  [(0.30, 1.00), (-0.30, 1.00), (0.30, -1.30), (-0.30, -1.30)], -1.91)


def hull_pod():
    m = Model("hull_pod", "Plasma Runner: a small chamfered cockpit block, a slate canopy and front")
    sts = [S(-0.55, 0.37, 0.25, 0.10, 0, 0.55), S(0.30, 0.37, 0.25, 0.10, 0, 0.55), S(0.54, 0.30, 0.19, 0.08, 0, 0.53)]

    def mat(lab, i, c):
        if lab == "bottom" or c[2] > 0.52:
            return SL
        return "paint1"
    m.add(ch_loft(sts, mat, c0=0.08, c1=0.06))
    m.add(canopy(-0.30, 0.40, 0.72, 0.98, 0.22, 0.40))
    return finish(m, (0.37, 0.50, -0.30), (0.0, 0.54, 0.05),
                  [(0.24, 0.35), (-0.24, 0.35), (0.24, -0.35), (-0.24, -0.35)], -0.55, 0.13)


def hull_cube():
    m = Model("hull_cube", "Ringmaster: an octagonal torpedo, a paint2 nose cone, a slate canopy")
    sts = [S(-1.30, 0.40, 0.40, 0.17, 0, 0.62), S(1.20, 0.40, 0.40, 0.17, 0, 0.62), S(1.70, 0.27, 0.27, 0.11, 0, 0.62),
           S(2.00, 0.12, 0.12, 0.05, 0, 0.62)]

    def mat(lab, i, c):
        if c[2] > 1.20:
            return "paint2"
        return "paint1"
    m.add(ch_loft(sts, mat, c0=0.04, c1=0.04))
    m.add(canopy(-0.55, 0.20, 0.92, 1.20, 0.22, 0.40))
    return finish(m, (0.40, 0.55, -0.20), (0.0, 0.74, -0.15),
                  [(0.28, 1.00), (-0.28, 1.00), (0.28, -1.00), (-0.28, -1.00)], -1.30)


def tip_pod(x, y, z0, z1, w, h, mat="paint2"):
    p = ch_box(x - w, x + w, y - h, y + h, z0, z1, 0.06, mat)
    p.add(ch_box(x - w * 0.62, x + w * 0.62, y - h * 0.55, y + h * 0.55, z0 - 0.04, z0 + 0.05, 0.03, SL))
    p.add(ch_box(x - w * 0.62, x + w * 0.62, y - h * 0.55, y + h * 0.55, z1 - 0.05, z1 + 0.04, 0.03, SL))
    return p


def hull_manta():
    m = Model("hull_manta", "Manta Ray: a flat hexagon wider than long, a raised centre, boxy pods at its tips")
    # (the wing's tips run along the pods: a chord, not a point; the wings fall from a raised faceted centre to the
    # pods -- the reference sheet)
    yc = 0.40
    rings = []
    for z, w in ((-1.27, 0.82), (-0.80, 1.44), (0.55, 1.44), (1.40, 0.22)):
        rw = min(0.34, w * 0.75)
        yt = yc - 0.10 * min(1.0, (w - rw) / 1.0)
        rings.append([(w, yt - 0.035, z), (w, yt + 0.035, z), (rw, yc + 0.12, z), (0.0, yc + 0.15, z), (-rw, yc + 0.12, z),
                      (-w, yt + 0.035, z), (-w, yt - 0.035, z), (-rw, yc - 0.10, z), (rw, yc - 0.10, z)])
    m.add(loft(rings, lambda i, j, c: SL if j in (6, 7) else "paint1"))
    m.add(ch_box(-0.16, 0.16, 0.34, 0.46, 1.36, 1.44, 0.02, SL))      # the intake slot in the nose
    m.add(canopy(-0.80, -0.05, 0.50, 0.80, 0.24, 0.42))
    for sx in (1, -1):
        m.add(tip_pod(sx * 1.62, 0.30, -0.95, 0.70, 0.20, 0.18))
        m.socket("flame%d" % (3 if sx > 0 else 4), sx * 1.62, 0.30, -1.00)
    return finish(m, (1.00, 0.36, -0.60), (0.0, 0.40, -0.40),
                  [(0.40, 0.60), (-0.40, 0.60), (0.80, -0.70), (-0.80, -0.70)], -1.27)


# (the "No Fins" wing has no model: the game and the previews draw nothing for it -- parts.h, WINGS[4])


# ---- the wings: small chamfered fins -------------------------------------------------------------------------------
def flat_fin(outline_xz, thick, mat="paint1", y=0.0, steps=1):
    """A fin lying flat: an outline (x, z) extruded in Y, its rim chamfered."""
    f = soft_fin(round_corners(outline_xz, 1, 0.12, 1), thick, mat, steps)
    f.v = np.stack([f.v[:, 0], f.v[:, 2] + y, f.v[:, 1]], axis=1)
    return f.orient()


def wing_swept():
    m = Model("wing_swept", "Swept Fins: small chamfered swept-back fins")
    m.add(flat_fin([(-0.12, 0.30), (-0.12, -0.45), (0.70, -0.80), (0.82, -0.70), (0.45, -0.05)], 0.11))
    return m


def wing_stubby():
    m = Model("wing_stubby", "Stubby Fins: short thick chamfered trapezoids")
    m.add(flat_fin([(-0.12, 0.35), (-0.12, -0.45), (0.45, -0.40), (0.45, 0.12)], 0.13))
    return m


def wing_ray():
    m = Model("wing_ray", "Ray Wings: long straight chamfered blades")
    m.add(flat_fin([(-0.12, 0.26), (-0.12, -0.28), (1.10, -0.24), (1.22, -0.12), (1.18, 0.06)], 0.10))
    return m


def wing_solar():
    m = Model("wing_solar", "Delta Fins: a small chamfered delta")
    m.add(flat_fin([(-0.12, 0.55), (-0.12, -0.50), (0.72, -0.55), (0.80, -0.42)], 0.11))
    return m


# ---- the engines ---------------------------------------------------------------------------------------------------
def engine_ion():
    m = Model("engine_ion", "Ion Jet: one big slate nozzle with a cyan core, a paint2 collar")
    m.add(soft_cyl(0.36, -0.22, 0.10, 0.08, 8, "paint2", steps=1))
    m.add(nozzle(0.32, -0.58, -0.16, "cyan_glow", 0, 0, 8, mat="paint2"))
    m.socket("flame1", 0.0, 0.0, -0.60)
    return m


def engine_pods():
    m = Model("engine_pods", "Side Pods: two tall chamfered boxes beside the hull's back, slate ends, cyan cores")
    X, Y, W, H, Z0, Z1 = 1.29, 0.08, 0.29, 0.40, 2.22, 0.0
    for sx in (1, -1):
        x = sx * X
        m.add(ch_box(x - W, x + W, Y - H, Y + H, Z1, Z0, 0.09, "paint1"))
        m.add(ch_box(x - W * 0.72, x + W * 0.72, Y - H * 0.75, Y + H * 0.75, Z0 - 0.06, Z0 + 0.05, 0.05, SL))
        m.add(ch_box(x - W * 0.72, x + W * 0.72, Y - H * 0.75, Y + H * 0.75, Z1 - 0.06, Z1 + 0.06, 0.05, SL))
        m.add(disc(0.14, 8, "cyan_glow", y=0.0, down=True).rot("x", 90).tr(x, Y, Z1 - 0.065))
        m.add(ch_box(x - W - 0.01, x + W + 0.01, Y - H * 0.35, Y - H * 0.20, Z1 + 0.30, Z0 - 0.30, 0.02, SL))
        # the strut into the hull
        a, b = sorted((sx * 0.80, sx * (X - W + 0.05)))
        m.add(ch_box(a, b, Y + 0.10, Y + 0.30, 1.30, 1.80, 0.03, "paint1"))
        m.socket("flame%d" % (1 if sx > 0 else 2), x, Y, Z1 - 0.10)
    return m


def lattice(x0, x1, y, z, h=0.24, n=4, r=0.035, mat="grey"):
    """A flat lattice arm in the XY plane (from x0 to x1, h high): two rails and a zigzag."""
    p = Part()
    for dy in (h / 2, -h / 2):
        p.add(tube([(x0, y + dy, z), (x1, y + dy, z)], r, 5, mat))
    xs = np.linspace(x0, x1, n + 1)
    for k in range(n):
        a = (xs[k], y + (h / 2 if k % 2 == 0 else -h / 2), z)
        b = (xs[k + 1], y + (-h / 2 if k % 2 == 0 else h / 2), z)
        p.add(tube([a, b], r * 0.7, 4, mat))
    return p


def boom(x, y, z0, z1, w=0.13, n=5, r=0.035, mat="paint1"):
    """A square lattice boom along +Z (from z0 to z1): four rails and zigzags on its sides."""
    p = Part()
    cs = [(x + w, y + w), (x - w, y + w), (x - w, y - w), (x + w, y - w)]
    for cx, cy in cs:
        p.add(tube([(cx, cy, z0), (cx, cy, z1)], r, 4, mat))
    zs = np.linspace(z0, z1, n + 1)
    for s in range(4):
        (ax, ay), (bx, by) = cs[s], cs[(s + 1) % 4]
        for k in range(n):
            u, v = ((ax, ay), (bx, by)) if k % 2 == 0 else ((bx, by), (ax, ay))
            p.add(tube([(u[0], u[1], zs[k]), (v[0], v[1], zs[k + 1])], r * 0.75, 4, mat))
    return p


def engine_twin():
    m = Model("engine_twin", "Twin Nacelles: two big octagonal nacelles on paint2 struts, lattice booms ahead with paint2 tips")
    X, Y, Z0, Z1, R = 1.40, 0.13, 1.65, -0.19, 0.30
    for sx in (1, -1):
        x = sx * X

        def nmat(i, j, c):
            return "paint2" if (1.40 < c[2] < 1.58 or 0.12 < c[2] < 0.32) else "paint1"
        m.add(soft_cyl(R, Z1 + 0.19, Z0, 0.08, 8, nmat, x, Y, steps=1))
        m.add(nozzle(R * 0.86, Z1, Z1 + 0.22, "orange_glow", x, Y, 8, mat=SL))
        m.add(soft_cyl(R * 0.70, Z0 - 0.04, Z0 + 0.05, 0.03, 8, SL, x, Y, steps=1))
        # the strut to the hull, the boom ahead with its little tip
        a, b = sorted((sx * 0.35, sx * (X - R * 0.8)))
        m.add(ch_box(a, b, Y - 0.05, Y + 0.05, 0.64, 1.38, 0.025, "paint2"))
        m.add(boom(x, Y, Z0 + 0.04, Z0 + 1.95, 0.10, 6, 0.03))
        m.add(ch_box(x - 0.11, x + 0.11, Y - 0.11, Y + 0.11, Z0 + 1.95, Z0 + 2.19, 0.03, "paint2"))
        m.add(ch_box(x - 0.07, x + 0.07, Y - 0.07, Y + 0.07, Z0 + 2.17, Z0 + 2.22, 0.015, SL))
        m.socket("flame%d" % (1 if sx > 0 else 2), x, Y, Z1 - 0.04)
    return m


def oct_pod(x, y, z0, z1, w, h=None, mat="paint1", band="paint1"):
    """An octagonal chamfered pod from z0 (back) to z1 (front), a groove just ahead of its back."""
    h = w if h is None else h
    c = w * 0.38
    zb = z0 + 0.20
    g = 0.04
    sts = [S(z0 + c * 0.3, w, h, c, x, y), S(zb - 0.07, w, h, c, x, y), S(zb - 0.06, w - g, h - g, c - g * 0.4, x, y),
           S(zb + 0.06, w - g, h - g, c - g * 0.4, x, y), S(zb + 0.07, w, h, c, x, y), S(z1 - c * 0.5, w, h, c, x, y)]
    return ch_loft(sts, lambda lab, i, cc: band if 2 <= i <= 3 else mat, c0=c * 0.3, c1=c * 0.5)


def engine_star():
    m = Model("engine_star", "Plasma Pods: two chunky octagonal pods out ahead, pulling the cockpit on glowing cables")
    X, Y, Z0, Z1, W, H = 0.89, 0.13, 4.20, 2.60, 0.32, 0.42
    for sx in (1, -1):
        x = sx * X
        m.add(oct_pod(x, Y, Z1, Z0, W, H))
        m.add(ch_box(x - W * 0.72, x + W * 0.72, Y - H * 0.75, Y + H * 0.75, Z0 - 0.06, Z0 + 0.05, W * 0.22, SL))
        prof = [(W * 0.82, Z1 + 0.03), (W * 0.76, Z1 - 0.20), (W * 0.50, Z1 - 0.59), (0.0, Z1 - 0.59)]
        cone = revolve_z([(0.0, Z1 + 0.03)] + prof, 8, lambda i, j, c: "purple_glow" if i >= 3 else SL, a0=PI / 8)
        cone.v[:, 1] *= H / W
        m.add(cone.tr(x, Y, 0))
        a = np.array([x - sx * W * 0.3, Y + 0.15, Z1 - 0.30])
        b = np.array([sx * 0.18, Y + 0.05, 1.10])
        mid = (a + b) / 2 + np.array([0, 0.10, 0])
        m.add(tube([a, (a + mid) / 2 + np.array([0, 0.04, 0]), mid, (mid + b) / 2 + np.array([0, 0.02, 0]), b],
                   0.03, 5, "pink_glow"))
        m.socket("flame%d" % (1 if sx > 0 else 2), x, Y, Z1 - 0.62)
        m.socket("arc%d" % (1 if sx > 0 else 2), x - sx * W, Y, Z1 + 0.50)
    m.add(soft_cyl(0.18, -0.12, 0.06, 0.05, 8, SL, steps=1))
    return m


def engine_ring():
    m = Model("engine_ring", "Ring Drive: a paint2 tail cone, a big chamfered ring round the hull on four struts")
    cy, cz, R, r = 0.20, 0.50, 0.90, 0.15
    prof = [(0.0, 0.02), (0.40, 0.02), (0.40, -0.05), (0.27, -0.45), (0.13, -0.70), (0.0, -0.70)]
    m.add(revolve_z(prof, 8, lambda i, j, c: SL if i >= 4 else "paint2", a0=PI / 8).tr(0, cy, 0))
    m.add(disc(0.08, 8, "magenta_glow", y=0.0, down=True).rot("x", 90).tr(0, cy, -0.705))
    ring = torus(R, r, 24, 8, "paint1").rot("x", 90).tr(0, cy, cz)
    for k in range(4):
        a = PI / 4 + PI / 2 * k
        ring.add(tube([(0.38 * math.cos(a), cy + 0.38 * math.sin(a), cz), ((R - 0.08) * math.cos(a), cy + (R - 0.08) * math.sin(a), cz)],
                      0.06, 5, "paint1"))
    m.add(ring, "ring")
    m.socket("ring", 0.0, cy, cz)
    m.socket("flame1", 0.0, cy, -0.72)
    return m


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
