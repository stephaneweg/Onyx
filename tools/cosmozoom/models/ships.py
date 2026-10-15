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


def hull_needle():
    m = Model("hull_needle", "Nova Streak: a faceted arrowhead, a paint2 nose tip and side panels, white pinstripes")
    Y0 = 0.20
    # (z, half width, height): wide at the back, a sharp nose
    prof = [(-1.62, 1.18, 0.62), (-0.70, 1.04, 0.64), (0.40, 0.74, 0.52), (1.30, 0.44, 0.36), (2.05, 0.18, 0.20),
            (2.32, 0.06, 0.11)]
    rings = []
    for z, w, h in prof:
        rings.append([(w, Y0 + 0.04, z), (w, Y0 + 0.13 * h / 0.44, z), (w * 0.86, Y0 + h * 0.48, z), (w * 0.82, Y0 + h * 0.54, z),
                      (w * 0.36, Y0 + h, z), (-w * 0.36, Y0 + h, z), (-w * 0.82, Y0 + h * 0.54, z), (-w * 0.86, Y0 + h * 0.48, z),
                      (-w, Y0 + 0.13 * h / 0.44, z), (-w, Y0 + 0.04, z), (-w * 0.9, Y0, z), (w * 0.9, Y0, z)])

    def mat(i, j, c):
        if c[2] > 1.75:
            return "paint2"                         # the nose tip
        if j in (2, 6):
            return "white"                          # the pinstripes along the shoulders
        if j in (1, 7) and -0.9 < c[2] < 0.6:
            return "paint2"                         # the side panels
        if j in (10,):
            return SL
        return "paint1"
    m.add(loft(rings, mat))
    # the canopy, the spine behind it
    m.add(canopy(-1.25, 0.35, Y0 + 0.55, Y0 + 0.95, 0.30, 0.40))
    m.add(ch_box(-0.26, 0.26, Y0 + 0.50, Y0 + 0.74, -1.66, -1.10, 0.06, "paint1"))
    return finish(m, (1.00, 0.30, -1.20), (0.0, Y0 + 0.48, -0.55),
                  [(0.40, 1.20), (-0.40, 1.20), (0.70, -1.20), (-0.70, -1.20)], -1.62)


def hull_comet():
    m = Model("hull_comet", "Comet Chaser: a narrow chamfered fuselage, a stepped cabin, a pointed slate nose")
    sts = [S(-1.45, 0.50, 0.30, 0.12, 0, 0.50), S(0.60, 0.50, 0.30, 0.12, 0, 0.50), S(1.55, 0.36, 0.22, 0.10, 0, 0.44),
           S(2.05, 0.22, 0.13, 0.07, 0, 0.40)]

    def mat(lab, i, c):
        if lab == "bottom":
            return SL
        if c[2] > 1.95:
            return SL                                # the nose's intake
        return "paint1"
    m.add(ch_loft(sts, mat, c0=0.08, c1=0.05))
    # the stepped cabin: a block, the canopy on its front, a step behind
    cab = [S(-1.10, 0.36, 0.17, 0.08, 0, 0.92), S(0.10, 0.36, 0.17, 0.08, 0, 0.92), S(0.70, 0.28, 0.08, 0.05, 0, 0.84)]
    m.add(ch_loft(cab, "paint1", c0=0.08, c1=0.04))
    m.add(canopy(-0.35, 0.95, 0.90, 1.24, 0.27, 0.30))
    m.add(ch_box(-0.30, 0.30, 0.96, 1.16, -1.25, -0.75, 0.06, "paint2"))
    return finish(m, (0.48, 0.50, -0.80), (0.0, 0.80, 0.15),
                  [(0.36, 1.00), (-0.36, 1.00), (0.40, -1.05), (-0.40, -1.05)], -1.45)


def hull_whale():
    m = Model("hull_whale", "Star Drifter: a long chamfered fuselage, a paint2 stripe down its back, a slate canopy")
    sts = [S(-1.55, 0.46, 0.27, 0.12, 0, 0.48), S(0.40, 0.48, 0.28, 0.12, 0, 0.49), S(1.40, 0.38, 0.22, 0.10, 0, 0.45),
           S(2.10, 0.20, 0.13, 0.07, 0, 0.41)]

    def mat(lab, i, c):
        if lab == "bottom":
            return SL
        if lab in ("top1",):
            return "paint2"                         # the stripe down the back
        if c[2] > 2.05:
            return "paint2"
        return "paint1"
    m.add(ch_loft(sts, mat, top=(0.30, 0.70), c0=0.08, c1=0.06))
    m.add(canopy(-0.75, 0.80, 0.70, 1.10, 0.30, 0.40))
    m.add(ch_box(-0.06, 0.06, 0.70, 0.98, -1.55, -0.95, 0.03, "paint1"))
    # a little fin under the belly
    m.add(ch_box(-0.05, 0.05, 0.02, 0.24, -0.80, -0.20, 0.03, "paint1"))
    return finish(m, (0.46, 0.45, -0.70), (0.0, 0.62, 0.05),
                  [(0.34, 1.00), (-0.34, 1.00), (0.36, -1.15), (-0.36, -1.15)], -1.55)


def hull_pod():
    m = Model("hull_pod", "Plasma Runner: a small chamfered cockpit block, a slate canopy and front")
    sts = [S(-0.80, 0.40, 0.25, 0.10, 0, 0.55), S(0.55, 0.40, 0.25, 0.10, 0, 0.55), S(0.85, 0.34, 0.20, 0.08, 0, 0.52)]

    def mat(lab, i, c):
        if lab == "bottom" or c[2] > 0.84:
            return SL
        return "paint1"
    m.add(ch_loft(sts, mat, c0=0.08, c1=0.04))
    m.add(canopy(-0.55, 0.55, 0.72, 1.06, 0.27, 0.40))
    m.add(ch_box(-0.44, 0.44, 0.38, 0.48, -0.25, 0.35, 0.03, "paint2"))   # the band round its middle
    return finish(m, (0.40, 0.50, -0.40), (0.0, 0.62, -0.05),
                  [(0.26, 0.60), (-0.26, 0.60), (0.28, -0.60), (-0.28, -0.60)], -0.88, 0.13)


def hull_cube():
    m = Model("hull_cube", "Ringmaster: an octagonal torpedo, a paint2 nose cone with a slate intake, a slate canopy")
    sts = [S(-1.50, 0.44, 0.40, 0.17, 0, 0.62), S(0.70, 0.46, 0.42, 0.18, 0, 0.62), S(1.35, 0.36, 0.33, 0.14, 0, 0.60),
           S(1.85, 0.20, 0.19, 0.08, 0, 0.58)]

    def mat(lab, i, c):
        if c[2] > 1.86:
            return SL                                # the intake in the nose
        if c[2] > 0.95:
            return "paint2"
        return "paint1"
    m.add(ch_loft(sts, mat, c0=0.10, c1=0.04))
    m.add(canopy(-0.45, 0.65, 0.90, 1.22, 0.24, 0.40))
    return finish(m, (0.42, 0.55, -0.70), (0.0, 0.78, 0.10),
                  [(0.32, 0.95), (-0.32, 0.95), (0.34, -1.00), (-0.34, -1.00)], -1.60)


def tip_pod(x, y, z0, z1, w, h, mat="paint2"):
    p = ch_box(x - w, x + w, y - h, y + h, z0, z1, 0.06, mat)
    p.add(ch_box(x - w * 0.62, x + w * 0.62, y - h * 0.55, y + h * 0.55, z0 - 0.04, z0 + 0.05, 0.03, SL))
    p.add(ch_box(x - w * 0.62, x + w * 0.62, y - h * 0.55, y + h * 0.55, z1 - 0.05, z1 + 0.04, 0.03, SL))
    return p


def hull_manta():
    m = Model("hull_manta", "Manta Ray: a wide flat chamfered delta, a slate intake in its front, boxy pods at its tips")
    sts = [S(-1.20, 1.66, 0.15, 0.07, 0, 0.36), S(-0.50, 1.50, 0.17, 0.07, 0, 0.37), S(0.40, 0.92, 0.17, 0.07, 0, 0.37),
           S(1.15, 0.42, 0.14, 0.06, 0, 0.35)]

    def mat(lab, i, c):
        if lab == "bottom":
            return SL
        return "paint1"
    m.add(ch_loft(sts, mat, c0=0.07, c1=0.10))
    # the raised centre and its front intake
    mid = [S(-1.15, 0.48, 0.16, 0.08, 0, 0.52), S(0.40, 0.46, 0.16, 0.08, 0, 0.52), S(1.15, 0.30, 0.10, 0.06, 0, 0.44)]
    m.add(ch_loft(mid, lambda lab, i, c: SL if lab == "bottom" else "paint1", c0=0.07, c1=0.06))
    m.add(ch_box(-0.55, 0.55, 0.26, 0.40, 1.08, 1.30, 0.03, SL))
    m.add(canopy(-0.70, 0.55, 0.62, 0.98, 0.28, 0.42))
    for sx in (1, -1):
        m.add(tip_pod(sx * 1.66, 0.38, -1.50, 0.35, 0.18, 0.20))
        m.socket("flame%d" % (3 if sx > 0 else 4), sx * 1.66, 0.38, -1.58)
    return finish(m, (0.80, 0.45, -0.60), (0.0, 0.52, -0.10),
                  [(0.40, 0.70), (-0.40, 0.70), (0.80, -0.90), (-0.80, -0.90)], -1.27)


# ---- the wings: small chamfered fins -------------------------------------------------------------------------------
def flat_fin(outline_xz, thick, mat="paint2", y=0.0, steps=1):
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
    m.add(nozzle(0.32, -0.58, -0.16, "cyan_glow", 0, 0, 8, mat=SL))
    m.socket("flame1", 0.0, 0.0, -0.60)
    return m


def engine_pods():
    m = Model("engine_pods", "Side Pods: two long chamfered boxes beside the hull, slate intakes, cyan cores")
    X, Y, W, H, Z0, Z1 = 0.98, -0.02, 0.32, 0.32, 2.35, -0.30
    for sx in (1, -1):
        x = sx * X
        m.add(ch_box(x - W, x + W, Y - H, Y + H, Z1, Z0, 0.09, "paint1", top=(0.5,)))
        m.add(ch_box(x - W * 0.72, x + W * 0.72, Y - H * 0.70, Y + H * 0.70, Z0 - 0.06, Z0 + 0.05, 0.05, SL))
        m.add(ch_box(x - W * 0.72, x + W * 0.72, Y - H * 0.70, Y + H * 0.70, Z1 - 0.06, Z1 + 0.06, 0.05, SL))
        m.add(disc(0.13, 8, "cyan_glow", y=0.0, down=True).rot("x", 90).tr(x, Y, Z1 - 0.065))
        m.add(ch_box(x - W - 0.01, x + W + 0.01, Y + 0.10, Y + 0.17, Z0 - 0.75, Z0 - 0.55, 0.02, "paint2"))
        # the pylon into the hull
        a, b = sorted((sx * 0.30, sx * (X - W + 0.05)))
        m.add(ch_box(a, b, Y - 0.08, Y + 0.08, 0.40, 1.40, 0.03, "paint1"))
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
    m = Model("engine_twin", "Twin Nacelles: two big octagonal nacelles on flat struts, lattice booms ahead with paint2 tips")
    X, Y, Z0, Z1, R = 1.40, 0.04, 1.25, -0.25, 0.40
    for sx in (1, -1):
        x = sx * X

        def nmat(i, j, c):
            return "paint2" if 0.55 < c[2] < 0.80 else "paint1"
        m.add(soft_cyl(R, Z1 + 0.05, Z0, 0.10, 8, nmat, x, Y, steps=1))
        m.add(soft_cyl(R * 0.80, Z0 - 0.05, Z0 + 0.07, 0.04, 8, SL, x, Y, steps=1))
        m.add(nozzle(R * 0.80, Z1 - 0.12, Z1 + 0.10, "orange_glow", x, Y, 8, mat=SL))
        # the strut to the hull, the boom ahead with its little tip
        a, b = sorted((sx * 0.30, sx * (X - R * 0.8)))
        m.add(ch_box(a, b, Y - 0.06, Y + 0.06, 0.15, 0.85, 0.03, "paint1"))
        m.add(boom(x, Y, Z0 + 0.05, Z0 + 1.85, 0.12))
        m.add(ch_box(x - 0.17, x + 0.17, Y - 0.17, Y + 0.17, Z0 + 1.85, Z0 + 2.20, 0.04, "paint2"))
        m.add(ch_box(x - 0.11, x + 0.11, Y - 0.11, Y + 0.11, Z0 + 2.18, Z0 + 2.24, 0.02, SL))
        m.socket("flame%d" % (1 if sx > 0 else 2), x, Y, Z1 - 0.14)
    return m


def oct_pod(x, y, z0, z1, w, mat="paint1", band="paint2"):
    """An octagonal chamfered pod from z0 (back) to z1 (front), a band near its front."""
    c = w * 0.38
    p = ch_box(x - w, x + w, y - w, y + w, z0, z1, c, mat)
    zb = z1 - (z1 - z0) * 0.30
    p.add(ch_box(x - w - 0.02, x + w + 0.02, y - w - 0.02, y + w + 0.02, zb - 0.08, zb + 0.08, c, band))
    return p


def engine_star():
    m = Model("engine_star", "Plasma Pods: two chunky octagonal pods out ahead, pulling the cockpit on glowing cables")
    X, Y, Z0, Z1, W = 1.10, 0.02, 4.10, 2.50, 0.38
    for sx in (1, -1):
        x = sx * X
        m.add(oct_pod(x, Y, Z1, Z0, W))
        m.add(ch_box(x - W * 0.72, x + W * 0.72, Y - W * 0.72, Y + W * 0.72, Z0 - 0.06, Z0 + 0.06, W * 0.25, SL))
        prof = [(W * 0.70, Z1 + 0.04), (W * 0.62, Z1 - 0.10), (W * 0.42, Z1 - 0.38), (0.0, Z1 - 0.38)]
        m.add(revolve_z([(0.0, Z1 + 0.04)] + prof, 8, lambda i, j, c: "purple_glow" if i >= 3 else SL, a0=PI / 8).tr(x, Y, 0))
        a = np.array([x - sx * W * 0.4, Y + 0.10, Z1 - 0.20])
        b = np.array([sx * 0.25, Y + 0.10, 1.65])
        mid = (a + b) / 2 + np.array([0, 0.14, 0])
        m.add(tube([a, (a + mid) / 2 + np.array([0, 0.05, 0]), mid, (mid + b) / 2 + np.array([0, 0.02, 0]), b],
                   0.03, 5, "pink_glow"))
        m.socket("flame%d" % (1 if sx > 0 else 2), x, Y, Z1 - 0.42)
        m.socket("arc%d" % (1 if sx > 0 else 2), x - sx * W, Y, Z1 + 0.45)
    m.add(soft_cyl(0.22, -0.16, 0.08, 0.06, 8, SL, steps=1))
    return m


def engine_ring():
    m = Model("engine_ring", "Ring Drive: a big chamfered ring round the hull's back, on four struts")
    cy, cz, R, r = 0.18, 0.75, 1.00, 0.12
    m.add(soft_cyl(0.30, -0.25, 0.10, 0.08, 8, "paint2", steps=1))
    m.add(nozzle(0.26, -0.50, -0.20, "magenta_glow", 0, 0, 8, mat=SL))
    ring = torus(R, r, 24, 6, "paint1").rot("x", 90).tr(0, cy, cz)
    for k in range(4):
        a = PI / 4 + PI / 2 * k
        ring.add(tube([(0.38 * math.cos(a), cy + 0.38 * math.sin(a), cz), ((R - 0.06) * math.cos(a), cy + (R - 0.06) * math.sin(a), cz)],
                      0.06, 5, "paint1"))
    m.add(ring, "ring")
    m.socket("ring", 0.0, cy, cz)
    m.socket("flame1", 0.0, 0.0, -0.52)
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
