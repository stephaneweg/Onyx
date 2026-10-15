#!/usr/bin/env python3
#
# ships.py -- Cosmo Zoom!'s ship parts in the final look (the user's approved image, 2026-10-15): rounded, bevelled
# low-poly shapes (asset-pack style), soft light, no outline, solid vivid-pastel colours, glowing cores in the
# nozzles only. The six ships of the image:
#   hull_needle  "Nova Streak"   a long paint1 wedge, three white stripes on its top, a paint2 nose block, a canopy
#   hull_comet   "Comet Chaser"  a chunky rounded body and cabin, a dark intake (+ engine_pods: two boxy side pods)
#   hull_whale   "Star Drifter"  a long body, a paint2 front with a grille (+ engine_twin: nacelles on trusses)
#   hull_pod     "Plasma Runner" a small capsule cockpit (+ engine_star: two long pods out ahead, on cables)
#   hull_cube    "Ringmaster"    a rounded torpedo, a paint2 nose (+ engine_ring: a big ring over the tail)
#   hull_manta   "Manta Ray"     a wide flat rounded delta, a paint2 spine, two pods built in at its tips
# The wings: four small rounded fins. The engines: dark rounded nozzles with a soft glowing core.
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


def canopy(z0, z1, ybot, top, w, peak=0.35, mat="tint", sts=4):
    """A rounded canopy: a soft loft from z0 to z1 whose top rises to `top` at the fraction `peak` of its
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
        st.append(S(z, w * (0.75 + 0.25 * k), h, min(w, h) * 0.95, 0.0, ybot + h))
    return soft_loft(st, mat, 2, round0=0.06, round1=0.06, steps=2)


# ---- the hulls ---------------------------------------------------------------------------------------------------
def hull_needle():
    m = Model("hull_needle", "Nova Streak: a long rounded wedge, three white stripes on top, a paint2 nose block")
    cxs = 0.48
    xs = [0.34, 0.26, 0.04, -0.04, -0.26, -0.34]
    top = [(cxs - x) / (2 * cxs) for x in xs]
    sts = [S(-1.62, 0.62, 0.35, 0.14, 0, 0.53), S(-0.55, 0.64, 0.33, 0.14, 0, 0.51), S(0.30, 0.63, 0.27, 0.14, 0, 0.45),
           S(0.90, 0.61, 0.22, 0.13, 0, 0.40), S(1.36, 0.59, 0.19, 0.12, 0, 0.37)]

    def mat(lab, i, c):
        if lab in ("top1", "top3", "top5") and c[2] > -0.55:
            return "white"
        if lab == "bottom" or (lab.startswith("corner_b")):
            return "darkgrey" if lab == "bottom" else "paint1"
        return "paint1"
    m.add(soft_loft(sts, mat, 3, top=top, round0=0.10, round1=0.02, steps=3))
    # the nose block
    m.add(soft_box(-0.67, 0.67, 0.15, 0.60, 1.26, 2.06, 0.13, "paint2"))
    # the canopy at the back, a small spoiler behind it
    m.add(canopy(-1.55, -0.05, 0.74, 1.20, 0.33, 0.45))
    m.add(soft_box(-0.50, 0.50, 0.82, 0.92, -1.80, -1.55, 0.04, "paint2", k=1))
    return finish(m, (0.55, 0.52, -1.00), (0.0, 0.74, -0.95),
                  [(0.36, 1.25), (-0.36, 1.25), (0.40, -1.25), (-0.40, -1.25)], -1.72)


def hull_comet():
    m = Model("hull_comet", "Comet Chaser: a chunky rounded body and cabin, a dark front intake")
    sts = [S(-1.30, 0.78, 0.30, 0.20, 0, 0.48), S(-0.40, 0.80, 0.31, 0.20, 0, 0.49), S(0.70, 0.79, 0.29, 0.20, 0, 0.47),
           S(1.28, 0.72, 0.25, 0.18, 0, 0.44)]

    def mat(lab, i, c):
        if lab == "bottom":
            return "darkgrey"
        if lab.startswith("side") and lab.endswith("1"):
            return "paint2"
        return "paint1"
    m.add(soft_loft(sts, mat, 3, side=(0.42, 0.62), round0=0.16, round1=0.20, steps=3))
    cab = [S(-1.05, 0.56, 0.28, 0.17, 0, 0.98), S(-0.05, 0.57, 0.28, 0.17, 0, 0.98), S(0.42, 0.52, 0.18, 0.14, 0, 0.88)]

    def cmat(lab, i, c):
        if c[2] > 0.30 and (lab.startswith("top") or lab == "corner_tr" or lab == "corner_tl"):
            return "tint"                               # the windscreen
        if c[2] > 0.48:
            return "tint"
        if lab.startswith("side") and -0.85 < c[2] < 0.25 and c[1] > 0.95:
            return "tint"                               # the side windows
        if lab.startswith("top"):
            return "paint2"
        return "paint1"
    m.add(soft_loft(cab, cmat, 3, side=(0.55,), round0=0.12, round1=0.14, steps=3))
    # the dark intake in the nose, a light bar over it
    m.add(soft_box(-0.46, 0.46, 0.28, 0.52, 1.36, 1.56, 0.07, "darkgrey", k=1))
    m.add(soft_box(-0.30, 0.30, 0.60, 0.66, 1.30, 1.44, 0.03, "white", k=1))
    return finish(m, (0.70, 0.74, -0.75), (0.0, 0.86, -0.45),
                  [(0.46, 1.00), (-0.46, 1.00), (0.50, -1.05), (-0.50, -1.05)], -1.46)


def hull_whale():
    m = Model("hull_whale", "Star Drifter: a long rounded body, a paint2 front with a dark grille, a raised back")
    sts = [S(-1.55, 0.66, 0.31, 0.18, 0, 0.50), S(-0.60, 0.70, 0.29, 0.18, 0, 0.47), S(0.60, 0.68, 0.27, 0.18, 0, 0.45),
           S(1.30, 0.64, 0.24, 0.17, 0, 0.42)]

    def mat(lab, i, c):
        if lab == "bottom":
            return "darkgrey"
        if lab.startswith("top") and c[2] < -0.4 and lab in ("top1",):
            return "paint2"
        return "paint1"
    m.add(soft_loft(sts, mat, 3, top=(0.42, 0.58), round0=0.12, round1=0.05, steps=3))
    # the front block and its grille
    m.add(soft_box(-0.70, 0.70, 0.15, 0.66, 1.25, 1.95, 0.14, "paint2"))
    for k in range(3):
        z = 1.45 + 0.16 * k
        m.add(box(-0.45, 0.45, 0.60, 0.69, z - 0.04, z + 0.04, "darkgrey", 0.02))
    m.add(box(-0.50, 0.50, 0.26, 0.46, 1.90, 1.99, "darkgrey", 0.03))
    # the raised back deck with the canopy, a fin
    m.add(canopy(-1.30, 0.20, 0.80, 1.22, 0.34, 0.40))
    fin_out = round_corners([(0.0, -1.62), (0.0, -0.95), (0.50, -1.35), (0.52, -1.62)], 1, 0.25, 2)
    f = soft_fin(fin_out, 0.10, "paint2", 1)
    # slab is in (u, v, w) = (x, y, z): the outline (y, z) -> stand it up: u = height, v = z
    f.v = np.stack([f.v[:, 2], f.v[:, 0] + 0.80, f.v[:, 1]], axis=1)
    m.add(f.orient())
    return finish(m, (0.62, 0.62, -0.70), (0.0, 0.80, -0.75),
                  [(0.44, 1.00), (-0.44, 1.00), (0.46, -1.15), (-0.46, -1.15)], -1.67)


def hull_pod():
    m = Model("hull_pod", "Plasma Runner: a small rounded capsule cockpit, a paint2 nose")
    sts = [S(-0.95, 0.40, 0.33, 0.28, 0, 0.55), S(-0.20, 0.48, 0.38, 0.33, 0, 0.58), S(0.45, 0.46, 0.35, 0.31, 0, 0.55),
           S(0.95, 0.34, 0.27, 0.24, 0, 0.50)]

    def mat(lab, i, c):
        if lab == "bottom":
            return "darkgrey"
        if c[2] > 0.80:
            return "paint2"
        if lab.startswith("side") and c[2] < -0.5:
            return "paint2"
        return "paint1"
    m.add(soft_loft(sts, mat, 3, side=(0.5,), round0=0.26, round1=0.26, steps=3))
    m.add(canopy(-0.75, 0.55, 0.78, 1.26, 0.32, 0.40))
    m.add(soft_box(-0.36, 0.36, 0.30, 0.42, -1.30, -1.10, 0.05, "paint2", k=1))
    return finish(m, (0.42, 0.62, -0.45), (0.0, 0.84, -0.20),
                  [(0.28, 0.70), (-0.28, 0.70), (0.30, -0.80), (-0.30, -0.80)], -1.21, 0.13)


def hull_cube():
    m = Model("hull_cube", "Ringmaster: a rounded torpedo body, a paint2 nose, a canopy")
    sts = [S(-1.40, 0.50, 0.42, 0.40, 0, 0.56), S(-0.40, 0.56, 0.46, 0.44, 0, 0.60), S(0.60, 0.55, 0.44, 0.43, 0, 0.58),
           S(1.15, 0.48, 0.38, 0.37, 0, 0.54), S(1.55, 0.34, 0.28, 0.27, 0, 0.50)]

    def mat(lab, i, c):
        if lab == "bottom":
            return "darkgrey"
        if c[2] > 1.05:
            return "paint2"
        if lab.startswith("side") and lab.endswith("1") and c[2] < 0.9:
            return "paint2"
        return "paint1"
    m.add(soft_loft(sts, mat, 3, side=(0.45, 0.60), round0=0.12, round1=0.30, steps=3))
    m.add(canopy(-0.80, 0.55, 0.92, 1.30, 0.33, 0.40))
    return finish(m, (0.52, 0.70, -0.60), (0.0, 0.92, -0.32),
                  [(0.36, 0.95), (-0.36, 0.95), (0.38, -1.00), (-0.38, -1.00)], -1.52)


def tip_pod(x, y, z0, z1, r, mat="paint2", glow="cyan_glow"):
    p = soft_cyl(r, z0 + 0.12, z1, 0.10, 9, mat, x, y, bevel0=0.04, bevel1=r * 0.7, steps=2)
    p.add(nozzle(r * 0.86, z0 - 0.06, z0 + 0.16, glow, x, y, 9))
    return p


def hull_manta():
    m = Model("hull_manta", "Manta Ray: a wide flat rounded delta, a paint2 spine and nose, two pods at its tips")
    sts = [S(-1.30, 1.72, 0.17, 0.15, 0, 0.36), S(-0.60, 1.40, 0.18, 0.15, 0, 0.37), S(0.30, 0.86, 0.17, 0.15, 0, 0.36),
           S(1.10, 0.36, 0.14, 0.12, 0, 0.34)]

    def mat(lab, i, c):
        if lab == "bottom":
            return "darkgrey"
        return "paint1"
    m.add(soft_loft(sts, mat, 3, round0=0.10, round1=0.22, steps=3))
    spine = [S(-1.25, 0.42, 0.18, 0.15, 0, 0.52), S(0.20, 0.40, 0.18, 0.15, 0, 0.52), S(1.30, 0.20, 0.10, 0.09, 0, 0.42)]
    m.add(soft_loft(spine, lambda lab, i, c: "darkgrey" if lab == "bottom" else "paint2", 2, round0=0.10, round1=0.12,
                    steps=2))
    m.add(canopy(-1.00, 0.45, 0.62, 1.06, 0.30, 0.42))
    for sx in (1, -1):
        m.add(tip_pod(sx * 1.70, 0.38, -1.55, 0.30, 0.22))
        m.socket("flame%d" % (3 if sx > 0 else 4), sx * 1.70, 0.38, -1.63)
        # little teal tip lights on the wing's front edge
    return finish(m, (0.80, 0.52, -0.60), (0.0, 0.62, -0.45),
                  [(0.40, 0.70), (-0.40, 0.70), (0.80, -0.90), (-0.80, -0.90)], -1.40)


# ---- the wings: small rounded fins --------------------------------------------------------------------------------
def flat_fin(outline_xz, thick, mat="paint2", y=0.0, steps=2):
    """A fin lying flat: an outline (x, z) extruded in Y, its rim rounded."""
    f = soft_fin(round_corners(outline_xz, 1, 0.22, steps), thick, mat, steps)
    f.v = np.stack([f.v[:, 0], f.v[:, 2] + y, f.v[:, 1]], axis=1)
    return f.orient()


def wing_swept():
    m = Model("wing_swept", "Swept Fins: small rounded swept-back fins")
    m.add(flat_fin([(-0.12, 0.30), (-0.12, -0.45), (0.70, -0.80), (0.82, -0.70), (0.45, -0.05)], 0.11))
    return m


def wing_stubby():
    m = Model("wing_stubby", "Stubby Fins: short thick rounded trapezoids")
    m.add(flat_fin([(-0.12, 0.35), (-0.12, -0.45), (0.45, -0.40), (0.45, 0.12)], 0.13))
    return m


def wing_ray():
    m = Model("wing_ray", "Ray Wings: long straight rounded blades")
    m.add(flat_fin([(-0.12, 0.26), (-0.12, -0.28), (1.10, -0.24), (1.22, -0.12), (1.18, 0.06)], 0.10))
    return m


def wing_solar():
    m = Model("wing_solar", "Delta Fins: a small rounded delta")
    m.add(flat_fin([(-0.12, 0.55), (-0.12, -0.50), (0.72, -0.55), (0.80, -0.42)], 0.11))
    return m


# ---- the engines ---------------------------------------------------------------------------------------------------
def engine_ion():
    m = Model("engine_ion", "Ion Jet: one big dark rounded nozzle with a cyan core, a paint2 collar")
    m.add(soft_cyl(0.40, -0.22, 0.10, 0.07, 12, "paint2"))
    m.add(nozzle(0.36, -0.62, -0.16, "cyan_glow", 0, 0, 12))
    m.socket("flame1", 0.0, 0.0, -0.64)
    return m


def engine_pods():
    m = Model("engine_pods", "Side Pods: two boxy rounded pods on the hull's sides, dark intakes, cyan cores")
    X, Y, Z0, Z1 = 1.18, -0.02, 2.30, 0.35
    for sx in (1, -1):
        x0, x1 = sx * X - 0.32, sx * X + 0.32
        m.add(soft_box(x0, x1, Y - 0.27, Y + 0.27, Z1, Z0, 0.15, "paint1", top=(0.5,)))
        m.add(soft_box(sx * X - 0.24, sx * X + 0.24, Y - 0.17, Y + 0.13, Z0 - 0.08, Z0 + 0.05, 0.07, "darkgrey", k=1))
        m.add(box(x0 - 0.01, x1 + 0.01, Y + 0.05, Y + 0.12, Z1 + 0.45, Z0 - 0.45, "paint2"))
        m.add(nozzle(0.20, Z1 - 0.22, Z1 + 0.05, "cyan_glow", sx * X, Y, 10))
        # the pylon into the hull
        m.add(soft_box(min(sx * 0.45, sx * (X - 0.28)), max(sx * 0.45, sx * (X - 0.28)), Y - 0.10, Y + 0.08,
                       1.00, 1.55, 0.05, "darkgrey", k=1))
        m.socket("flame%d" % (1 if sx > 0 else 2), sx * X, Y, Z1 - 0.24)
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


def engine_twin():
    m = Model("engine_twin", "Twin Thrusters: two rounded nacelles held out on grey lattice trusses")
    X, Y, Z0, Z1, R = 1.42, 0.02, 0.85, -1.30, 0.30
    m.add(soft_box(-0.36, 0.36, -0.20, 0.20, -0.95, 0.30, 0.10, "darkgrey", k=1))
    for sx in (1, -1):
        def nmat(i, j, c):
            return "paint2" if c[2] > Z0 - 0.35 else "paint1"
        m.add(soft_cyl(R, Z1 + 0.12, Z0, 0.10, 10, nmat, sx * X, Y, bevel1=0.16, steps=2))
        m.add(disc(R * 0.62, 10, "darkgrey", y=0.0, down=True).rot("x", -90).tr(sx * X, Y, Z0 + 0.005))
        m.add(nozzle(R * 0.86, Z1 - 0.12, Z1 + 0.16, "purple_glow", sx * X, Y, 10))
        a, b = (0.30, X - R * 0.85) if sx > 0 else (-(X - R * 0.85), -0.30)
        m.add(lattice(a, b, Y, -0.25, 0.30, 5))
        m.add(tube([(sx * 0.30, Y, 0.35), (sx * (X - R * 0.85), Y, 0.35)], 0.05, 6, "grey"))
        m.socket("flame%d" % (1 if sx > 0 else 2), sx * X, Y, Z1 - 0.14)
    return m


def engine_star():
    m = Model("engine_star", "Star Drive: two long rounded pods out ahead, pulling the hull on cables")
    X, Y, Z0, Z1, R = 1.30, 0.02, 4.70, 2.30, 0.27
    for sx in (1, -1):
        def nmat(i, j, c):
            if c[2] > Z0 - 0.30 or c[2] < Z1 + 0.45:
                return "paint2"
            return "paint1"
        m.add(soft_cyl(R, Z1 + 0.10, Z0, 0.08, 10, nmat, sx * X, Y, bevel1=0.20, steps=2))
        m.add(disc(R * 0.55, 10, "purple_glow", y=0.0, down=True).rot("x", -90).tr(sx * X, Y, Z0 + 0.005))
        m.add(nozzle(R * 0.84, Z1 - 0.12, Z1 + 0.14, "magenta_glow", sx * X, Y, 10))
        # two small fins on top and below
        for sy in (1,):
            f = flat_fin([(-0.05, 0.25), (-0.05, -0.30), (0.22, -0.32), (0.22, -0.05)], 0.06, "paint2", steps=1)
            f.rot("z", 90 * sy).tr(sx * X, Y + sy * (R - 0.04), Z1 + 0.65)
            m.add(f)
        a = np.array([sx * (X - R * 0.8), Y - 0.04, Z1 + 0.30])
        b = np.array([sx * 0.22, Y - 0.04, 2.05])
        mid = (a + b) / 2 + np.array([0, -0.10, 0])
        m.add(tube([a, (a + mid) / 2 + np.array([0, -0.035, 0]), mid, (mid + b) / 2 + np.array([0, -0.035, 0]), b],
                   0.035, 6, "darkgrey"))
        m.socket("flame%d" % (1 if sx > 0 else 2), sx * X, Y, Z1 - 0.14)
        m.socket("arc%d" % (1 if sx > 0 else 2), sx * (X - R), Y, Z0 - 0.9)
    m.add(soft_cyl(0.24, -0.16, 0.08, 0.06, 10, "darkgrey", steps=1))
    return m


def engine_ring():
    m = Model("engine_ring", "Ring Drive: a big rounded ring standing over the tail, a paint2 hub and spokes")
    cy, cz, R, r = 0.10, -0.55, 0.82, 0.12
    m.add(soft_cyl(0.26, -0.85, 0.10, 0.08, 12, "paint2", 0.0, 0.0, bevel0=0.12))
    m.add(nozzle(0.20, -1.02, -0.80, "magenta_glow", 0, 0, 14))
    ring = torus(R, r, 20, 6, "paint1").rot("x", 90).tr(0, cy, cz)
    hub = soft_cyl(0.30, cz - 0.12, cz + 0.12, 0.08, 12, "paint2", 0.0, cy)
    ring.add(hub)
    for k in range(4):
        a = PI / 4 + PI / 2 * k
        ring.add(tube([(0.25 * math.cos(a), cy + 0.25 * math.sin(a), cz), ((R - 0.05) * math.cos(a), cy + (R - 0.05) * math.sin(a), cz)],
                      0.055, 6, "paint2"))
    m.add(ring, "ring")
    m.socket("ring", 0.0, cy, cz)
    m.socket("flame1", 0.0, 0.0, -1.04)
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
