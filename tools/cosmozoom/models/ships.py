#!/usr/bin/env python3
#
# ships.py -- Cosmo Zoom!'s ship parts after the user's reference sheet (2026-10-15, DESIGN.md section 4):
# chamfered low-poly shapes (crisp 45-degree bevels, flat faces), soft light, no outline, solid vivid colours,
# slate-navy canopies and intakes, glowing cores in the nozzles only. The six ships of the sheet:
#   hull_needle  "Nova Streak"   a faceted arrowhead, a paint2 nose tip and side panels, white pinstripes
#   hull_comet   "Comet Chaser"  a narrow fuselage, a stepped cabin, its two pods on struts (measured on its GLB)
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


def finish(m, wing, pilot, pad_pts, z_back, pad_r=0.15, engine_y=ENGINE_Y):
    for k, (x, z) in enumerate(pad_pts):
        # (the pads are not modelled any more: the game draws their glow at these points, under the hull)
        m.socket("pad%d" % (k + 1), x, 0.0, z)
    m.socket("wing", *wing)
    m.socket("engine", 0.0, engine_y, z_back)
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


def _lerp_tab(tab, z):
    """Linear interpolation in a table of rows (z, values...) sorted by decreasing z."""
    for r0, r1 in zip(tab, tab[1:]):
        if r1[0] <= z <= r0[0]:
            t = (z - r0[0]) / (r1[0] - r0[0]) if r1[0] != r0[0] else 0.0
            return [u + (v - u) * t for u, v in zip(r0[1:], r1[1:])]
    return list((tab[0] if z > tab[0][0] else tab[-1])[1:])


def _shell(rings, mat, cap1=True):
    """A loft whose two end caps both face outward (loft's own first cap faces in); cap1 False: the back left open."""
    p = loft(rings, mat, cap0=False, cap1=False, orient=False)
    mf = mat if callable(mat) else (lambda i, j, c: mat)
    r0, r1 = np.array(rings[0], float), np.array(rings[-1], float)
    fan(p, r0[::-1], mf(0, 0, r0.mean(axis=0)))
    n = len(p.f)
    fan(p, r1, mf(len(rings) - 1, 0, r1.mean(axis=0)))
    p.orient()
    if not cap1:
        p.f = p.f[:n]
    return p


# (the planforms measured on the user's top view of the six ships, the heights on their side view, 2026-10-15)
# Nova Streak, measured on the user's generated model (sections every 0.1 m): a flat arrowhead with a knife-edge rim,
# a raised rear block across its whole width (the cowl in its middle), two exhaust pipes; the engine is the tail
_NV_FRONT = [  # (z, deck centre y, deck flat half, rim top y, bottom centre y, bottom flat half, its edge y)
    (2.24, 0.635, 0.13, 0.62, 0.47, 0.12, 0.475), (2.05, 0.665, 0.15, 0.63, 0.445, 0.17, 0.46),
    (1.55, 0.745, 0.16, 0.65, 0.395, 0.21, 0.41), (1.20, 0.79, 0.19, 0.65, 0.375, 0.25, 0.39),
    (0.44, 0.83, 0.22, 0.66, 0.33, 0.29, 0.35), (0.15, 0.85, 0.23, 0.655, 0.31, 0.31, 0.335),
    (-0.65, 0.87, 0.26, 0.65, 0.255, 0.37, 0.31)]
_NV_LABS = ["mid", "deck", "slope", "stripe", "slope", "stripe", "slope", "bevel", "rim", "bevel", "under", "bottom"]


def _nv_w(z):
    """The planform's half width: a taper from the nose, a little faster aft of z 1.0, then 1.17 from z -0.78."""
    return _lerp_tab([(2.24, 0.215), (1.00, 0.595), (-0.78, 1.17)], z)[0]


def _nv_mirror(right):
    """A closed ring from its right half (centre top first, centre bottom last)."""
    return right + [(-x, y) for x, y in right[-2:0:-1]]


def _nv_front_ring(z):
    yt, D, yr, yb, B, ybe = _lerp_tab(_NV_FRONT, z)
    W, c = _nv_w(z), 0.03
    # the two pinstripes (inner 0.045 wide, outer 0.035, 0.03 apart) diverge from the nose block to the step;
    # ahead of it they keep their place on the slope, as a fraction of it
    def xs(zz):
        x1 = 0.31 + 0.19 * (1.52 - zz) / 2.17
        return [x1 - 0.0225, x1 + 0.0225, x1 + 0.0525, x1 + 0.0875]
    if z > 1.52:
        D1, W1 = _lerp_tab(_NV_FRONT, 1.52)[1], _nv_w(1.52) - c
        s = [D + (W - c - D) * (x - D1) / (W1 - D1) for x in xs(1.52)]
    else:
        s = xs(z)
    sl = lambda x: yt + (yr - yt) * (x - D) / (W - c - D)
    right = [(0.0, yt), (min(0.155, D - 0.03), yt), (D, yt)] + [(x, sl(x)) for x in s] + \
            [(W - c, yr), (W, yr - c), (W, 0.53), (W - c, 0.50), (B, ybe), (0.0, yb)]
    return [(x, y, z) for x, y in _nv_mirror(right)]


def _nv_rear_ring(z, W, top, rim=None):
    """The rear block: the cowl's flat top and 45-degree sides, the raised deck to the rim, a shallow keel; rim: its
    four rim points (at the back, the rim swells round the pipes)."""
    rim = rim or [(W - 0.05, 0.90 - 0.30 * (W - 0.48)), (W, 0.63), (W, 0.53), (W - 0.03, 0.495)]
    right = [(0.0, top), (0.21, top), (0.43, 0.90)] + rim + [(0.21, 0.23), (0.18, 0.20), (0.0, 0.20)]
    return [(x, y, z) for x, y in _nv_mirror(right)]


def _nv_step(ring):
    """A ring on the step across the deck: at z -0.66 in the middle, swept back to -0.77 at the rim."""
    pts = []
    for k in range(len(ring(-0.66))):
        z = -0.66
        for _ in range(3):
            z = -0.66 - 0.11 * max(0.0, abs(ring(z)[k][0]) - 0.5) / 0.67
        pts.append(ring(z)[k])
    return pts


def hull_needle():
    m = Model("hull_needle", "Nova Streak: a flat arrowhead with a knife-edge rim, a paint2 nose block and deck panel, "
              "white pinstripes, a slate canopy under a raised rear block, two exhaust pipes")

    def mat(i, j, c):
        lab = (_NV_LABS + _NV_LABS[::-1])[j]
        z = c[2]
        if z > 1.55:
            return "paint2"                         # the nose block
        if lab == "stripe":
            return "white"
        if lab in ("mid", "bottom") and 0.44 < z < 1.46:
            return "paint2"                         # the deck panel (and its twin underneath)
        if lab == "bottom" and z < 0.15:
            return SL                               # the slate plate under the cockpit
        return "paint1"
    zs = (2.24, 2.05, 1.55, 1.46, 1.00, 0.44, 0.15)
    m.add(_shell([_nv_front_ring(z) for z in zs] + [_nv_step(_nv_front_ring)], mat, cap1=False))
    # the rear block: its front cap the step across the deck, its back corners cut in to the pipes
    m.add(_shell([_nv_step(lambda z: _nv_rear_ring(z, _nv_w(z), 1.16)), _nv_rear_ring(-0.85, 1.17, 1.16),
                  _nv_rear_ring(-1.20, 1.17, 1.12),
                  _nv_rear_ring(-1.32, 0, 1.115, [(0.95, 0.75), (1.02, 0.65), (1.02, 0.52), (0.97, 0.44)]),
                  _nv_rear_ring(-1.43, 0, 1.11, [(0.70, 0.83), (0.82, 0.69), (0.85, 0.54), (0.74, 0.38)])], "paint1"))
    # the canopy: a slate wedge rising from the deck back under the cowl's front edge
    cr = []
    for z, top, wt, wb in ((0.20, 0.87, 0.13, 0.18), (0.00, 0.95, 0.15, 0.19), (-0.20, 1.045, 0.16, 0.20),
                           (-0.40, 1.09, 0.16, 0.23), (-0.70, 1.10, 0.17, 0.26)):
        yd = _lerp_tab(_NV_FRONT, z)[0]
        right = [(0.0, top), (wt, top), (wb, yd + 0.02), (wb, yd - 0.06), (0.0, yd - 0.06)]
        cr.append([(x, y, z) for x, y in _nv_mirror(right)])
    m.add(_shell(cr, "tint"))
    # the cowl's back: a chamfered block between the pipes (the engine, its collar sunk in it, plays the tail block)
    blk = lambda z, y0: [(0.39, y0 + 0.04, z), (0.39, 0.98, z), (0.24, 1.10, z), (-0.24, 1.10, z), (-0.39, 0.98, z),
                         (-0.39, y0 + 0.04, z), (-0.35, y0, z), (0.35, y0, z)]
    m.add(_shell([blk(-1.40, 0.21), blk(-1.54, 0.25), blk(-1.58, 0.30)], "paint1"))
    for sx in (1, -1):                              # the two exhaust pipes
        m.add(nozzle(0.21, -1.60, -1.36, SL, sx * 0.63, 0.575, 8, 0.08, "paint1"))
    return finish(m, (1.12, 0.58, -1.05), (0.0, 0.58, -0.40),
                  [(0.30, 1.30), (-0.30, 1.30), (0.585, -1.30), (-0.585, -1.30)], -1.36, engine_y=0.74)


# Comet Chaser, measured on the user's generated model (sections every 0.1 m): a long narrow nose widening into a rear
# body, a raised spine with a step down to the shoulders, a stepped cabin and a roof block, two boxy pods on struts
_CM_BODY = [  # (z, spine top y, spine half, its step's foot y, shoulder x, its y, half width, side top y,
    #            side bottom y, bottom chamfer x, its y, bottom centre y)
    (2.24, 0.945, 0.24, 0.945, 0.24, 0.945, 0.33, 0.86, 0.63, 0.28, 0.565, 0.555),
    (2.10, 0.97, 0.26, 0.97, 0.26, 0.97, 0.35, 0.88, 0.63, 0.27, 0.55, 0.53),
    (1.40, 1.00, 0.25, 1.00, 0.34, 0.98, 0.41, 0.88, 0.64, 0.31, 0.52, 0.50),
    (1.00, 1.03, 0.36, 0.97, 0.38, 0.955, 0.44, 0.89, 0.64, 0.35, 0.515, 0.475),
    (0.40, 1.07, 0.39, 0.99, 0.47, 0.94, 0.50, 0.90, 0.61, 0.42, 0.49, 0.46),
    (0.20, 1.075, 0.40, 1.00, 0.46, 0.98, 0.52, 0.93, 0.60, 0.44, 0.48, 0.46),
    (-1.16, 1.09, 0.41, 0.99, 0.46, 0.97, 0.52, 0.92, 0.60, 0.44, 0.48, 0.46),
    (-1.40, 1.10, 0.39, 0.98, 0.39, 0.98, 0.49, 0.89, 0.64, 0.42, 0.55, 0.53),
    (-1.85, 1.07, 0.33, 0.95, 0.33, 0.95, 0.41, 0.91, 0.69, 0.40, 0.69, 0.69)]
_CM_CAB = [  # (z, cabin top y, its flat half, its flank's foot y on the spine edge)
    (-0.45, 1.36, 0.23, 1.12), (-1.16, 1.36, 0.24, 1.13), (-1.40, 1.29, 0.26, 1.11), (-1.85, 1.16, 0.27, 1.08)]


def _cm_ring(z, cab=True, groove=0.0):
    """groove: the flanks pulled in (the panel lines round the rear body's front and back)."""
    T, S, Yd, Xd, Yd2, W, Ys, Yb, Wb, Bb, B = _lerp_tab(_CM_BODY, z)
    Tc, Rc, Tcb = _lerp_tab(_CM_CAB, z) if cab and z <= -0.45 else (T, S - 0.04, T)
    right = [(0.0, Tc), (Rc, Tc), (S, Tcb), (S, Yd), (Xd, Yd2), (W, Ys), (W, Yb), (Wb, Bb), (0.0, B)]
    return [(x - groove if x > W - 0.1 else x, y, z) for x, y in _nv_mirror(right)]


def _cm_pod(sx):
    """A side pod: a chamfered box, a collar and a groove behind its intake's lip, the slate intake recessed in its
    front, a tapered slate exhaust block at its back, recessed too."""
    def r(z, x0, x1, y0, y1, c):
        ring = oct_ring((x0 + x1) / 2, (y0 + y1) / 2, 0, (x1 - x0) / 2, (y1 - y0) / 2, c)
        return [(sx * x, y, z) for x, y, _ in ring]
    body = (0.92, 1.565, 0.35, 1.11, 0.085)
    rings = [r(0.25, 1.01, 1.48, 0.43, 1.03, 0.05), r(0.48, 1.01, 1.48, 0.43, 1.03, 0.05),
             r(0.48, 0.955, 1.535, 0.38, 1.08, 0.07), r(0.45, 0.93, 1.555, 0.36, 1.10, 0.08),
             r(0.34, 0.93, 1.555, 0.36, 1.10, 0.08), r(0.32, 0.96, 1.53, 0.39, 1.08, 0.07),
             r(0.23, 0.96, 1.53, 0.39, 1.08, 0.07), r(0.21, *body), r(-1.16, *body),
             r(-1.16, 1.00, 1.47, 0.41, 1.01, 0.06), r(-1.56, 1.03, 1.44, 0.47, 0.98, 0.05),
             r(-1.56, 1.07, 1.40, 0.51, 0.94, 0.04), r(-1.46, 1.07, 1.40, 0.51, 0.94, 0.04)]
    return _shell(rings, lambda i, j, c: SL if i == 0 or i >= 9 else "paint1")


def hull_comet():
    m = Model("hull_comet", "Comet Chaser: a long nose, a raised spine, a slate canopy before a stepped cabin, "
              "two boxy side pods on struts with slate intakes and exhausts, a small ventral fin")
    # the fuselage: its front a slate panel recessed in a frame, the cabin rising off the spine from z -0.45
    front = _cm_ring(2.24)

    def inset(z):
        return [(x * 0.28 / 0.33, 0.62 + (y - 0.555) * 0.22 / 0.39, z) for x, y, _ in front]
    rings = [inset(2.17), inset(2.24)] + [_cm_ring(z) for z in (2.24, 2.10, 1.40, 1.00, 0.40, 0.37)] + \
            [_cm_ring(0.355, groove=0.06), _cm_ring(0.285, groove=0.06), _cm_ring(0.27), _cm_ring(-0.45, False)] + \
            [_cm_ring(z) for z in (-0.45, -1.16)] + [_cm_ring(-1.20, groove=0.06)] + \
            [_cm_ring(z) for z in (-1.24, -1.40, -1.85)]
    m.add(_shell(rings, lambda i, j, c: SL if i == 0 else "paint1"))
    # the canopy: a slate wedge on the spine, rising back to the cabin's front
    cr = []
    for z, top, wt, wb in ((0.37, 1.09, 0.17, 0.22), (0.30, 1.115, 0.19, 0.27), (0.15, 1.185, 0.20, 0.30),
                           (0.00, 1.255, 0.20, 0.31), (-0.15, 1.295, 0.20, 0.31), (-0.47, 1.30, 0.20, 0.31)):
        yd = _lerp_tab(_CM_BODY, z)[0]
        right = [(0.0, top), (wt, top), (wb, yd), (wb, yd - 0.06), (0.0, yd - 0.06)]
        cr.append([(x, y, z) for x, y in _nv_mirror(right)])
    m.add(_shell(cr, "tint"))
    # the roof block on the cabin
    roof = lambda z, top: [(x, y, z) for x, y in _nv_mirror([(0.0, top), (0.12, top), (0.24, 1.35), (0.24, 1.30),
                                                              (0.0, 1.30)])]
    m.add(_shell([roof(-0.60, 1.48), roof(-1.16, 1.48), roof(-1.32, 1.43), roof(-1.38, 1.35)], "paint1"))
    # the pods and the struts (one bar through the fuselage), the ventral fin (its foot slate)
    m.add(_cm_pod(1))
    m.add(_cm_pod(-1))
    m.add(_shell([[(-0.93, y, -0.87), (0.93, y, -0.87), (0.93, y, -0.07), (-0.93, y, -0.07)] for y in (0.63, 0.89)],
                 "paint1"))
    fin = lambda y, z0, z1, w, c: [(w, y, z1 - c), (w, y, z0 + c), (w - c, y, z0), (c - w, y, z0), (-w, y, z0 + c),
                                   (-w, y, z1 - c), (c - w, y, z1), (w - c, y, z1)]
    m.add(_shell([fin(0.20, 0.92, 1.42, 0.11, 0.03), fin(0.50, 1.05, 1.31, 0.09, 0.03)],
                 lambda i, j, c: SL if c[1] < 0.21 else "paint1"))
    return finish(m, (1.54, 0.80, -0.80), (0.0, 0.82, -0.20),
                  [(0.22, 1.75), (-0.22, 1.75), (1.24, -0.50), (-1.24, -0.50)], -1.84, engine_y=0.93)


# Star Drifter, measured on the user's generated model (sections every 0.1 m): a long narrow fuselage, a raised paint2
# stripe down its deck, a slate canopy before a cabin hump, swept struts to two big banded nacelles with slate cones,
# a square lattice boom ahead of each nacelle to a paint2 tip block
_SD_BODY = [  # (z, deck top y, stripe half, its step, side half width, side bottom y, bottom flat half, bottom y)
    (2.30, 0.85, 0.10, 0.0, 0.20, 0.69, 0.15, 0.66), (2.20, 0.88, 0.12, 0.0, 0.22, 0.67, 0.16, 0.65),
    (2.00, 0.88, 0.13, 0.0, 0.23, 0.67, 0.16, 0.64), (1.60, 0.90, 0.15, 0.0, 0.26, 0.66, 0.20, 0.62),
    (1.00, 0.94, 0.16, 0.03, 0.30, 0.65, 0.22, 0.59), (0.60, 0.97, 0.18, 0.05, 0.335, 0.63, 0.23, 0.58),
    (0.20, 0.98, 0.21, 0.05, 0.37, 0.61, 0.25, 0.56), (0.00, 0.975, 0.23, 0.04, 0.39, 0.60, 0.28, 0.55),
    (-0.40, 0.96, 0.25, 0.01, 0.41, 0.59, 0.28, 0.54), (-1.25, 0.96, 0.25, 0.01, 0.41, 0.60, 0.32, 0.56),
    (-1.45, 0.94, 0.22, 0.0, 0.36, 0.63, 0.29, 0.59), (-1.75, 0.91, 0.17, 0.0, 0.30, 0.67, 0.24, 0.65)]
_SD_LABS = ["deck", "step", "ledge", "slope", "bevel", "side", "chamfer", "bottom"]
_SD_NX, _SD_NY = 1.255, 0.716        # the +X nacelle's axis


def _sd_ring(z):
    T, D, s, W, Ys, Wb, B = _lerp_tab(_SD_BODY, z)
    L = min(D + 0.06, W - 0.06)
    right = [(0.0, T), (D, T), (D + 0.01, T - s), (L, T - s - 0.01), (W - 0.02, 0.86), (W, 0.81), (W, Ys), (Wb, B),
             (0.0, B)]
    return [(x, y, z) for x, y in _nv_mirror(right)]


def _sd_bar(a, b, r, mat, n=4):
    """A thin straight bar from a to b: n flat sides (the first face up), both ends closed."""
    a, b = np.array(a, float), np.array(b, float)
    d = (b - a) / np.linalg.norm(b - a)
    u = np.cross(d, (0.0, 1.0, 0.0))
    u = u / np.linalg.norm(u) if np.linalg.norm(u) > 1e-6 else np.array([1.0, 0.0, 0.0])
    v = np.cross(u, d)
    ring = [u * math.cos(PI / n + 2 * PI * k / n) + v * math.sin(PI / n + 2 * PI * k / n) for k in range(n)]
    p = Part(np.array([a + r * q for q in ring] + [b + r * q for q in ring]))
    p.f = [((k, (k + 1) % n, n + (k + 1) % n, n + k), mat) for k in range(n)]
    p.f += [((0, k + 1, k), mat) for k in range(1, n - 1)] + [((n, n + k, n + k + 1), mat) for k in range(1, n - 1)]
    return p.orient()


def _sd_boom(sx):
    """The square lattice boom from inside the nacelle's intake to the tip block: four rails converging forward, a
    zigzag of struts on each face."""
    p = Part()
    c = lambda z: (sx * 1.245, 0.705 + 0.0106 * z)
    hx = lambda z: 0.116 - 0.031 * z
    hy = lambda z: 0.117 - 0.0218 * z
    corner = lambda k, z: (c(z)[0] + (1, -1, -1, 1)[k] * hx(z), c(z)[1] + (1, 1, -1, -1)[k] * hy(z), z)
    for k in range(4):
        p.add(_sd_bar(corner(k, -0.19), corner(k, 1.50), 0.035, "paint1"))
    zs = np.linspace(-0.04, 1.44, 5)
    for a, b in ((0, 1), (3, 2), (0, 3), (1, 2)):  # top and bottom in step, the two sides in step
        for i in range(4):
            u, v = (a, b) if i % 2 == 0 else (b, a)
            p.add(_sd_bar(corner(u, zs[i]), corner(v, zs[i + 1]), 0.024, "paint1"))
    # the tip block: paint2, its front a chamfered frame round a recessed slate slot
    x, y = sx * 1.244, 0.712
    r = lambda z, w, h, ch: oct_ring(x, y, z, w, h, ch)
    p.add(_shell([r(1.45, 0.118, 0.130, 0.03), r(1.55, 0.122, 0.135, 0.03), r(1.76, 0.100, 0.125, 0.03),
                  r(1.82, 0.085, 0.108, 0.025), r(1.82, 0.045, 0.080, 0.015), r(1.80, 0.045, 0.080, 0.015)],
                 lambda i, j, cc: SL if i >= 4 else "paint2"))
    return p


def _sd_nacelle(sx):
    """A nacelle (10 sides): a rounded intake lip round a recessed slate face, a paint2 band, the body, a paint2 band,
    the slate exhaust cone, hollow."""
    prof = [(0.0, -0.19), (0.20, -0.19), (0.20, -0.10), (0.225, -0.075), (0.26, -0.075), (0.275, -0.09), (0.275, -0.245),
            (0.30, -0.26), (0.30, -0.49), (0.315, -0.505), (0.315, -1.195), (0.30, -1.21), (0.30, -1.34), (0.26, -1.35),
            (0.197, -1.80), (0.185, -1.85), (0.165, -1.865), (0.13, -1.865), (0.125, -1.60), (0.0, -1.60)]

    def mat(i, j, c):
        if i == 0 or i >= 12:
            return SL
        return "paint2" if i in (6, 7, 11) else "paint1"
    prof = [(r * 1.05, z) for r, z in prof]   # (radii across the flats: the ten corners on the sides)
    return revolve_z(prof, 10, mat).tr(sx * _SD_NX, _SD_NY, 0)


def hull_whale():
    m = Model("hull_whale", "Star Drifter: a long narrow fuselage, a paint2 stripe down its deck, a slate canopy, a "
              "cabin hump, swept struts to two big banded nacelles with slate cones, lattice booms ahead with paint2 tips")

    def mat(i, j, c):
        lab = (_SD_LABS + _SD_LABS[::-1])[j]
        z = c[2]
        if lab == "deck" and -0.45 < z < 1.72:
            return "paint2"                         # the stripe down the deck
        if lab == "bottom" and -0.35 < z < 2.25:
            return "paint2"                         # its twin underneath
        if lab == "side" and (0.0 < z < 2.20 or -1.75 < z < -1.35):
            return "paint2"                         # the side panels
        return "paint1"
    zs = sorted([r[0] for r in _SD_BODY] + [1.72], reverse=True)   # (1.72: the stripe's front end)
    nose = [(x * 0.85, 0.74 + (y - 0.755) * 0.6, 2.34) for x, y, _ in _sd_ring(2.30)]
    tail = [(x * 0.8, 0.78 + (y - 0.78) * 0.75, -1.80) for x, y, _ in _sd_ring(-1.75)]
    m.add(_shell([nose] + [_sd_ring(z) for z in zs] + [tail], mat))
    # the canopy: a slate wedge on the stripe, rising back into the cabin hump
    cr = []
    for z, top, wt, wb in ((0.18, 0.985, 0.05, 0.12), (0.10, 1.00, 0.10, 0.16), (0.00, 1.04, 0.14, 0.18),
                           (-0.20, 1.10, 0.15, 0.20), (-0.40, 1.14, 0.16, 0.22), (-0.50, 1.15, 0.16, 0.23)):
        yd = _lerp_tab(_SD_BODY, z)[0]
        right = [(0.0, top), (wt, top), (wb, yd), (wb, yd - 0.06), (0.0, yd - 0.06)]
        cr.append([(x, y, z) for x, y in _nv_mirror(right)])
    m.add(_shell(cr, "tint"))
    # the cabin hump: a paint2 ridge on its roof
    hump = lambda z, tr, tb: [(x, y, z) for x, y in _nv_mirror([(0.0, tr), (0.10, tr), (0.13, tb), (0.19, tb - 0.03),
                                                                 (0.29, 0.94), (0.29, 0.90), (0.0, 0.90)])]
    hr = [hump(-0.43, 1.17, 1.17), hump(-0.55, 1.18, 1.17), hump(-0.68, 1.23, 1.18), hump(-0.95, 1.23, 1.16), hump(-1.10, 1.20, 1.12),
          hump(-1.25, 1.14, 1.08), hump(-1.38, 1.10, 1.04)]
    m.add(_shell(hr, lambda i, j, c: "paint2" if j in (0, 1, 10, 11) and 1 < i < 6 else "paint1"))
    for sx in (1, -1):
        # the strut: a chamfered plate swept back (its back paint2) and a thicker spar across it
        def plate(x, zf, zb):
            pts = [(zf, 0.75), (zf - 0.04, 0.80), (-0.90, 0.80), (zb + 0.04, 0.80), (zb, 0.75), (zb + 0.04, 0.70),
                   (-0.90, 0.70), (zf - 0.04, 0.70)]
            return [(sx * x, y, z) for z, y in pts]
        m.add(_shell([plate(0.36, -0.33, -1.26), plate(1.00, -0.54, -1.13)],
                     lambda i, j, c: "paint2" if c[2] < -0.90 else "paint1"))
        spar = lambda x: [(sx * x, y, z) for z, y, _ in oct_ring(-0.7875, 0.745, 0.0, 0.1125, 0.085, 0.025)]
        m.add(_shell([spar(0.36), spar(1.00)], "paint1"))
        m.add(_sd_nacelle(sx))
        m.add(_sd_boom(sx))
    # the ventral leg: a post on a flared foot
    leg = lambda y, w, z0, z1: [(w, y, z0), (w, y, z1), (-w, y, z1), (-w, y, z0)]
    m.add(_shell([leg(0.20, 0.08, 0.77, 1.175), leg(0.24, 0.08, 0.77, 1.175), leg(0.33, 0.05, 0.885, 1.057),
                  leg(0.60, 0.05, 0.885, 1.057)], "paint1"))
    return finish(m, (1.57, 0.72, -0.85), (0.0, 0.66, -0.30),
                  [(0.30, 1.60), (-0.30, 1.60), (1.25, -0.90), (-1.25, -0.90)], -1.76, engine_y=0.78)


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
    from import_glb import IMPORTS          # (the hulls imported from the user's generated GLB: not built here)
    out = {}
    for n, fn in ALL.items():
        if (names and n not in names) or n in IMPORTS:
            continue
        mdl = fn()
        out[n] = (mdl.write(script=SCRIPT), mdl)
    return out


if __name__ == "__main__":
    from czmodel import write_mtl
    write_mtl()
    for n, (t, _) in build(sys.argv[1:] or None).items():
        print(n, t)
