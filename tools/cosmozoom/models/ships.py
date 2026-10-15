#!/usr/bin/env python3
#
# ships.py -- Cosmo Zoom!'s ship parts after the user's reference sheet (2026-10-15, DESIGN.md section 4):
# chamfered low-poly shapes (crisp 45-degree bevels, flat faces), soft light, no outline, solid vivid colours,
# slate-navy canopies and intakes, glowing cores in the nozzles only. The six ships of the sheet:
#   hull_needle  "Nova Streak"   a faceted arrowhead, a paint2 nose tip and side panels, white pinstripes
#   hull_comet   "Comet Chaser"  a narrow fuselage, a stepped cabin, its two pods on struts (measured on its GLB)
#   hull_whale   "Star Drifter"  a long fuselage, a paint2 stripe, banded nacelles, lattice booms (measured on its GLB)
#   hull_pod     "Plasma Runner" two chunky pods ahead, a plasma arc, cables back to a small cockpit (measured on its GLB)
#   hull_cube    "Ringmaster"    a torpedo, paint2 nose and tail cones, a big spinning ring on struts (measured on its GLB)
#   hull_manta   "Manta Ray"     a flat slab, a faceted centre, a slate slot and canopy, boxy pods (measured on its GLB)
#   hull_sprinter "Nebula Sprinter" a delta, green stripes, blue piping, a cockpit block, side pods, three engines (its GLB)
#   hull_wave    "Sonic Wave"    an arrow, paint2 stripes, a canopy ramp and hump, two wings a side, two nozzles (its GLB)
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
    # its engines are built in: the engine part is not drawn (its stats count), the flames leave its own exhausts;
    # the canopy is opaque, no pilot drawn
    m.socket("noengine", 0.0, 0.0, 0.0)
    m.socket("nopilot", 0.0, 0.0, 0.0)
    m.socket("flame1", 1.240, 0.730, -1.600)
    m.socket("flame2", -1.240, 0.730, -1.600)
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
    zs = np.linspace(-0.04, 1.44, 6)
    for a, b in ((0, 1), (3, 2), (0, 3), (1, 2)):  # top and bottom in step, the two sides in step
        for i in range(5):
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
    prof = [(0.0, -0.19), (0.20, -0.19), (0.20, -0.10), (0.225, -0.075), (0.26, -0.075), (0.275, -0.09),
            (0.275, -0.245), (0.30, -0.26), (0.30, -0.49), (0.315, -0.505), (0.315, -1.195), (0.30, -1.21),
            (0.30, -1.34), (0.26, -1.35), (0.197, -1.80), (0.185, -1.85), (0.165, -1.865), (0.13, -1.865),
            (0.125, -1.60), (0.0, -1.60)]

    def mat(i, j, c):
        if i == 0 or i >= 12:
            return SL
        return "paint2" if i in (6, 7, 11) else "paint1"
    prof = [(r * 1.05, z) for r, z in prof]   # (radii across the flats: the ten corners on the sides)
    return revolve_z(prof, 10, mat).tr(sx * _SD_NX, _SD_NY, 0)


def hull_whale():
    m = Model("hull_whale", "Star Drifter: a long narrow fuselage, a paint2 stripe down its deck, a slate canopy, a "
              "cabin hump, swept struts to two big banded nacelles with slate cones, lattice booms with paint2 tips")

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
    hr = [hump(z, tr, tb) for z, tr, tb in ((-0.43, 1.17, 1.17), (-0.55, 1.18, 1.17), (-0.68, 1.23, 1.18),
                                            (-0.95, 1.23, 1.16), (-1.10, 1.20, 1.12), (-1.25, 1.14, 1.08),
                                            (-1.38, 1.10, 1.04))]
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
    # its engines are built in: the engine part is not drawn (its stats count), the flames leave its own exhausts;
    # the canopy is opaque, no pilot drawn
    m.socket("noengine", 0.0, 0.0, 0.0)
    m.socket("nopilot", 0.0, 0.0, 0.0)
    m.socket("flame1", 1.255, 0.720, -1.900)
    m.socket("flame2", -1.255, 0.720, -1.900)
    return finish(m, (1.57, 0.72, -0.85), (0.0, 0.76, -0.48),
                  [(0.30, 1.60), (-0.30, 1.60), (1.25, -0.90), (-1.25, -0.90)], -1.76, engine_y=0.78)


# Plasma Runner, measured on the user's generated model (sections every 0.04 m): two chunky octagonal pods in front
# (a slate intake recessed in a frame, a slate groove, a collar, a slate exhaust cone), a small cockpit block far
# behind (side ears, a slate canopy, a raised rear block), two glowing cables back to it, a plasma arc between the pods
_PR_PX, _PR_PY = 0.98, 0.535          # the +X pod's axis
_PR_CK = [(-0.72, 0.175), (-1.10, 0.272), (-1.30, 0.286), (-1.46, 0.284), (-1.62, 0.266), (-1.83, 0.222)]  # (z, half w)
_PR_ARC_X = (-0.64, -0.48, -0.32, -0.16, 0.0, 0.16, 0.32, 0.48, 0.64)
_PR_ARC = [  # the plasma arc's four strands: (their y, their z) at _PR_ARC_X
    ((0.56, 0.62, 0.68, 0.72, 0.70, 0.725, 0.72, 0.64, 0.56), (1.33, 1.36, 1.41, 1.43, 1.40, 1.42, 1.38, 1.36, 1.33)),
    ((0.54, 0.60, 0.65, 0.62, 0.65, 0.62, 0.64, 0.66, 0.54), (1.33, 1.40, 1.38, 1.42, 1.39, 1.43, 1.37, 1.39, 1.33)),
    ((0.53, 0.56, 0.52, 0.55, 0.53, 0.55, 0.47, 0.53, 0.53), (1.30, 1.27, 1.31, 1.28, 1.32, 1.28, 1.31, 1.27, 1.30)),
    ((0.51, 0.47, 0.42, 0.40, 0.45, 0.41, 0.40, 0.47, 0.51), (1.28, 1.30, 1.26, 1.24, 1.29, 1.25, 1.28, 1.25, 1.28))]


def _pr_pod(sx):
    """A pod: chamfered octagonal sections, the slate intake recessed in its front frame, a slate groove before the
    collar, the round slate exhaust cone (8 sides) with a lip round its hollow back."""
    r = lambda z, w, h, ch: [(sx * x, y, z) for x, y, _ in oct_ring(_PR_PX, _PR_PY, z, w, h, ch)]
    body = (0.315, 0.33, 0.095)
    rings = [r(2.37, 0.215, 0.235, 0.06), r(2.44, 0.215, 0.235, 0.06), r(2.44, 0.263, 0.278, 0.07), r(2.31, *body),
             r(1.09, *body), r(1.075, 0.262, 0.285, 0.07), r(1.00, 0.262, 0.285, 0.07), r(0.995, 0.303, 0.323, 0.09),
             r(0.79, 0.303, 0.323, 0.09), r(0.778, 0.272, 0.287, 0.08)]
    p = _shell(rings, lambda i, j, c: SL if i in (0, 5) else "paint1")
    prof = [(0.0, 0.80), (0.29, 0.80), (0.29, 0.77), (0.215, 0.29), (0.19, 0.22), (0.13, 0.22), (0.12, 0.28),
            (0.0, 0.28)]
    p.add(revolve_z([(q * 1.04, z) for q, z in prof], 8, SL, a0=PI / 8).tr(sx * 0.972, 0.52, 0))
    # the arc's emitter: a glowing plate on the inner face
    e = lambda x: [(sx * x, y, z) for z, y, _ in oct_ring(1.305, 0.535, 0.0, 0.075, 0.085, 0.025)]
    p.add(_shell([e(0.64), e(0.68)], "pink_glow"))
    return p


def _pr_ck(z, top=0.79, bot=0.555):
    w = _lerp_tab(_PR_CK, z)[0]
    right = [(0.0, top), (w - 0.02, top), (w, top - 0.02), (w, bot + 0.02), (w - 0.02, bot), (0.0, bot)]
    return [(x, y, z) for x, y in _nv_mirror(right)]


def _pr_ear(sx):
    """A side ear on the cockpit: a chamfered block, its back cut in, a slate socket for the cable in its front."""
    def r(z, xo, x0=0.25, y0=0.567, y1=0.734, c=0.02):
        pts = [(x0, y1), (xo - c, y1), (xo, y1 - c), (xo, y0 + c), (xo - c, y0), (x0, y0)]
        return [(sx * x, y, z) for x, y in pts]
    hole = lambda z: r(z, 0.36, 0.30, 0.615, 0.69, 0.015)
    rings = [hole(-1.115), hole(-1.085), r(-1.085, 0.35), r(-1.115, 0.378), r(-1.46, 0.378), r(-1.62, 0.27)]
    return _shell(rings, lambda i, j, c: SL if i == 0 else "paint1")


def _pr_strand(pts, r=0.016):
    return tube([(x, y, z) for x, y, z in pts], r, 4, "pink_glow")


def hull_pod():
    m = Model("hull_pod", "Plasma Runner: two chunky octagonal pods with slate intakes, grooves and exhaust cones, a "
              "plasma arc between them, glowing cables back to a small cockpit with side ears and a slate canopy")
    for sx in (1, -1):
        m.add(_pr_pod(sx))
        m.add(_pr_ear(sx))
        # the cable: from the collar's back, along the cone's top, down to the ear's socket
        m.add(tube([(sx * x, y, z) for x, y, z in ((0.845, 0.765, 0.80), (0.80, 0.735, 0.50), (0.70, 0.712, 0.10),
                                                   (0.585, 0.692, -0.20), (0.475, 0.672, -0.50),
                                                   (0.39, 0.662, -0.75), (0.345, 0.655, -1.00),
                                                   (0.33, 0.652, -1.12))], 0.028, 5, "pink_glow"))
    # the cockpit: the slab, the canopy, the raised rear block
    sl = [_pr_ck(z) for z in (-0.72, -1.10, -1.30, -1.46, -1.62, -1.83)]
    front = [(x * 0.9, 0.567 + (y - 0.555) * 0.9, -0.70) for x, y, _ in sl[0]]
    back = [(x * 0.85, 0.567 + (y - 0.555) * 0.9, -1.86) for x, y, _ in _pr_ck(-1.83)]
    m.add(_shell([front] + sl + [back], "paint1"))
    cr = []
    for z, top, sill in ((-0.965, 0.80, 0.80), (-1.03, 0.848, 0.845), (-1.10, 0.90, 0.855), (-1.40, 0.907, 0.855)):
        right = [(0.0, top), (0.141, top), (0.152, sill), (0.162, 0.80), (0.162, 0.77), (0.0, 0.77)]
        cr.append([(x, y, z) for x, y in _nv_mirror(right)])
    m.add(_shell(cr, lambda i, j, c: "paint1" if j in (2, 7) else "tint"))   # (its lime sills)
    rb = []
    for z, top, tw, bw in ((-1.385, 0.942, 0.164, 0.196), (-1.52, 0.942, 0.160, 0.182), (-1.62, 0.89, 0.142, 0.172),
                           (-1.70, 0.84, 0.125, 0.162), (-1.78, 0.796, 0.105, 0.15)):
        right = [(0.0, top), (tw, top), (bw, 0.80), (bw, 0.77), (0.0, 0.77)]
        rb.append([(x, y, z) for x, y in _nv_mirror(right)])
    m.add(_shell(rb, "paint1"))
    # the plasma arc: four zigzag strands between the emitters
    for ys, zs in _PR_ARC:
        m.add(_pr_strand(zip(_PR_ARC_X, ys, zs)))
    # its engines are built in: the engine part is not drawn (its stats count), the flames leave its own exhausts;
    # the canopy is opaque, no pilot drawn
    m.socket("noengine", 0.0, 0.0, 0.0)
    m.socket("nopilot", 0.0, 0.0, 0.0)
    m.socket("flame1", 0.980, 0.710, 0.120)
    m.socket("flame2", -0.980, 0.710, 0.120)
    return finish(m, (0.378, 0.65, -1.30), (0.0, 0.50, -1.22),
                  [(0.98, 1.80), (-0.98, 1.80), (0.22, -1.40), (-0.22, -1.40)], -1.80, engine_y=0.67)


# Ringmaster, measured on the user's generated model (sections every 0.05 m): a long torpedo of rounded-octagon
# sections, a paint2 nose cone round a recessed slate intake, a slate canopy into a dorsal block, a round paint2 tail
# cone set a little higher, a big flat ring round the rear body on two side struts (the ring spins about Z)
_RM_BODY = [  # (z, axis y, half width, half height)
    (2.24, 0.933, 0.226, 0.160), (2.19, 0.932, 0.235, 0.173), (2.00, 0.923, 0.275, 0.224),
    (1.80, 0.916, 0.307, 0.261), (1.60, 0.9125, 0.331, 0.290), (1.42, 0.910, 0.353, 0.307),
    (1.00, 0.8985, 0.368, 0.3145), (0.45, 0.8935, 0.388, 0.3175), (0.00, 0.889, 0.386, 0.325),
    (-0.85, 0.893, 0.379, 0.320)]
_RM_SEC = [(0.0, 1.0), (0.62, 1.0), (0.93, 0.78), (1.0, 0.35), (1.0, -0.19), (0.88, -0.62), (0.66, -0.885),
           (0.0, -1.0)]        # the section's right half, in (half width, half height)
_RM_TAIL = [  # (z, axis y, radius): round (12 sides), a slate exhaust recessed in its flat end
    (-0.84, 0.980, 0.352), (-0.90, 0.982, 0.352), (-1.05, 0.986, 0.338), (-1.25, 0.994, 0.302),
    (-1.50, 1.002, 0.250), (-1.70, 1.009, 0.205), (-1.80, 1.011, 0.180), (-1.845, 1.012, 0.155),
    (-1.86, 1.012, 0.130), (-1.86, 1.012, 0.090), (-1.83, 1.012, 0.090)]
_RM_RC = (0.0, 1.017, -0.6865)       # the ring's centre
_RM_RING = (0.676, 0.828, -0.855, -0.518, 0.04)   # its inner and outer radii, back and front z, the chamfer


def _rm_ring(z, cy=None, W=None, H=None):
    c, w, h = _lerp_tab(_RM_BODY, z)
    cy, W, H = cy if cy is not None else c, W or w, H or h
    return [(x * W, cy + y * H, z) for x, y in _nv_mirror(_RM_SEC)]


def _rm_deck(z, top, tw, bw, foot, base=1.17):
    """A ring of the canopy or the dorsal block: a flat top, sloped flanks down to the deck (foot), sunk to base."""
    return [(x, y, z) for x, y in _nv_mirror([(0.0, top), (tw, top), (bw, foot), (bw, base), (0.0, base)])]


def _rm_band(segs=24):
    """The ring: a flat band (a chamfered rectangle in section), its axis Z through _RM_RC."""
    R0, R1, z0, z1, c = _RM_RING
    sec = [(R1, z0 + c), (R1, z1 - c), (R1 - c, z1), (R0 + c, z1), (R0, z1 - c), (R0, z0 + c), (R0 + c, z0),
           (R1 - c, z0)]
    k = 1.0 / math.cos(PI / segs / 2)       # (the corners a little out: the flats' mean on the circle)
    rings = [[(k * r * math.cos(a), _RM_RC[1] + k * r * math.sin(a), z) for r, z in sec]
             for a in (2 * PI * (i + 0.5) / segs for i in range(segs))]
    p = loft(rings + rings[:1], "paint1", cap0=False, cap1=False, orient=False)
    n = segs * len(sec)
    p.v = p.v[:n]
    p.f = [(tuple(i % n for i in idx), mt) for idx, mt in p.f]
    return p.orient()


def hull_cube():
    m = Model("hull_cube", "Ringmaster: a long torpedo, a paint2 nose cone round a slate intake, a slate canopy into "
              "a dorsal block, a paint2 tail cone, a big ring round its rear body on two struts")
    # the body: the slate intake recessed in the nose's rim, the paint2 cone to z 1.42
    inset = lambda z: _rm_ring(z, 0.93, 0.160, 0.100)
    rings = [inset(2.20), inset(2.238)] + [_rm_ring(r[0]) for r in _RM_BODY]
    m.add(_shell(rings, lambda i, j, c: SL if i == 0 else "paint2" if c[2] > 1.42 else "paint1"))
    # the tail cone
    tr = [[(1.01 * r * math.cos(a), cy + 1.01 * r * math.sin(a), z) for a in (2 * PI * k / 12 for k in range(12))]
          for z, cy, r in _RM_TAIL]
    m.add(_shell(tr, lambda i, j, c: SL if i >= 9 else "paint2"))
    # the canopy (opaque slate), rising back into the dorsal block
    m.add(_shell([_rm_deck(z, t, tw, bw, 1.212) for z, t, tw, bw in
                  ((0.34, 1.218, 0.10, 0.15), (0.25, 1.26, 0.13, 0.18), (0.15, 1.31, 0.14, 0.19),
                   (0.00, 1.36, 0.14, 0.195), (-0.18, 1.39, 0.14, 0.195), (-0.42, 1.402, 0.14, 0.195))], SL))
    m.add(_shell([_rm_deck(z, t, tw, bw, 1.23) for z, t, tw, bw in
                  ((-0.40, 1.40, 0.14, 0.22), (-0.47, 1.437, 0.155, 0.25), (-0.80, 1.437, 0.155, 0.25),
                   (-0.85, 1.41, 0.12, 0.24))], "paint1"))
    # the two struts (fixed) and the ring (it spins about Z round its socket)
    for sx in (1, -1):
        st = lambda x, zf: [(sx * x, y, z) for z, y, _ in oct_ring((zf - 0.80) / 2, 0.9375, 0.0, (zf + 0.80) / 2,
                                                                   0.1075, 0.03)]
        m.add(_shell([st(0.36, -0.47), st(0.60, -0.555), st(0.72, -0.555)], "paint1"))   # (its root swept forward)
    m.add(_rm_band(), "ring")
    m.socket("ring", *_RM_RC)
    # its engine is built in: the engine part is not drawn (its stats count), the flame leaves its tail's exhaust;
    # the canopy is opaque, no pilot drawn
    m.socket("noengine", 0.0, 0.0, 0.0)
    m.socket("nopilot", 0.0, 0.0, 0.0)
    m.socket("flame1", 0.0, 1.012, -1.870)
    return finish(m, (0.385, 0.85, 0.75), (0.0, 1.21, -0.10),
                  [(0.25, 1.40), (-0.25, 1.40), (0.25, -1.20), (-0.25, -1.20)], -1.86, engine_y=1.01)


# Manta Ray, measured on the user's generated model (sections every 0.1 m): mirror-symmetric top and bottom about
# y 0.665 -- a flat slab with bevelled edges, a raised faceted centre (a slate canopy and a hump on top, their twins
# underneath), a slate slot in the nose, a wide slate exhaust in the back, two boxy paint2 pods at the tips
_MR_Y = 1.33                          # y + y' (the mirror's plane at y 0.665)
_MR_T = [(1.09, 0.79), (0.88, 0.83), (0.68, 0.86), (0.48, 0.90), (0.28, 0.92), (0.08, 0.94), (-0.22, 0.97),
         (-1.24, 0.97)]               # (z, the centre's top y)
_MR_W = [(1.00, 0.37), (0.12, 1.44), (-1.015, 1.44), (-1.27, 0.42)]   # (z, the slab's half width)
_MR_PX = 1.607                        # the +X pod's axis x (its y 0.665)


def _mr_ring(z, top, flip=False):
    """A closed ring from its right top half (centre first), the bottom its mirror in y; flip: all of it mirrored."""
    right = top + [(x, _MR_Y - y) for x, y in top[::-1]]
    return [(x, _MR_Y - y if flip else y, z) for x, y in _nv_mirror(right)]


def _mr_centre(z):
    """The centre: a flat top, a small chamfer, a 0.6 facet down into the slab, a short side (the nose's)."""
    T = _lerp_tab(_MR_T, z)[0]
    G = min(max(0.38, 0.35 + (T - 0.795) / 0.6), _lerp_tab(_MR_W, z)[0])
    return _mr_ring(z, [(0.0, T), (0.31, T), (0.35, T - 0.025), (G, min(0.77, T - 0.05))])


def _mr_slab(z, d):
    W = _lerp_tab(_MR_W, z)[0]
    return _mr_ring(z, [(0.0, 0.78), (W - d, 0.78), (W, 0.705)])


def _mr_deck(flip):
    """The slate canopy rising back to the hump (on top; flip: its twin underneath)."""
    rows = ((-0.21, 0.975, 0.10, 0.16), (-0.40, 1.08, 0.15, 0.22), (-0.68, 1.095, 0.15, 0.22),
            (-0.80, 1.13, 0.18, 0.27), (-1.00, 1.13, 0.18, 0.27), (-1.235, 1.04, 0.16, 0.25))
    rings = []
    for z, top, tw, bw in rows:
        r = [(0.0, top), (tw, top), (bw, 0.97), (bw, 0.93), (0.0, 0.93)]
        rings.append([(x, _MR_Y - y if flip else y, z) for x, y in _nv_mirror(r)])
    return _shell(rings, lambda i, j, c: SL if i < 2 else "paint1")


def _mr_pod(sx):
    """A pod: a chamfered box tapered at both ends, a slate intake and exhaust recessed in its end frames."""
    r = lambda z, w, h, ch: [(sx * x, y, z) for x, y, _ in oct_ring(_MR_PX, 0.665, z, w, h, ch)]
    inner, end, body = (0.09, 0.085, 0.03), (0.142, 0.155, 0.045), (0.207, 0.2125, 0.06)
    rings = [r(0.295, *inner), r(0.335, *inner), r(0.335, *end), r(0.05, *body), r(-0.97, *body),
             r(-1.245, *end), r(-1.245, *inner), r(-1.205, *inner)]
    return _shell(rings, lambda i, j, c: SL if i == 0 or i >= 6 else "paint2")


def hull_manta():
    m = Model("hull_manta", "Manta Ray: a flat bevelled slab wider than long, a raised faceted centre, a slate slot in "
              "the nose, a slate canopy and hump (and their twins underneath), boxy paint2 pods at its tips")
    # the slab: its leading and trailing edges bevelled to a rim; its tips run into the pods
    m.add(_shell([_mr_slab(0.98, 0.16), _mr_slab(0.12, 0.16), _mr_slab(0.0, 0.04), _mr_slab(-0.85, 0.04),
                  _mr_slab(-1.015, 0.30), _mr_slab(-1.20, 0.30)], "paint1"))
    # the centre: the slate slot recessed in the nose's frame, the slate exhaust recessed in the back
    nose = _mr_centre(1.09)
    inset = lambda ring, z, sx, sy: [(x * sx, 0.665 + (y - 0.665) * sy, z) for x, y, _ in ring]
    rings = [inset(nose, 1.04, 0.85, 0.55), inset(nose, 1.09, 0.85, 0.55)] + \
            [_mr_centre(z) for z in (1.09, 0.88, 0.68, 0.48, 0.28, 0.08, -0.22, -1.00, -1.20, -1.24)] + \
            [_mr_ring(z, [(0.0, 0.80), (0.20, 0.80), (0.25, 0.78), (0.48, 0.675)]) for z in (-1.24, -1.215)]
    m.add(_shell(rings, lambda i, j, c: SL if i == 0 or i >= len(rings) - 2 else "paint1"))
    m.add(_mr_deck(False))
    m.add(_mr_deck(True))
    for sx in (1, -1):
        m.add(_mr_pod(sx))
        # the paint2 cheek down the nose's side, along the slab's leading edge
        ck = lambda x, z, y1: [(sx * x, 0.56, z), (sx * (x + 0.035), 0.575, z), (sx * (x + 0.035), y1 - 0.015, z),
                               (sx * x, y1, z), (sx * (x - 0.04), y1, z), (sx * (x - 0.04), 0.56, z)]
        m.add(_shell([ck(0.355, 1.075, 0.74), ck(0.375, 1.02, 0.765), ck(0.45, 0.80, 0.755)], "paint2"))
    # its engines are built in: the engine part is not drawn (its stats count), the flames leave the pods' exhausts
    # and the centre's; the canopy is opaque, no pilot drawn
    m.socket("noengine", 0.0, 0.0, 0.0)
    m.socket("nopilot", 0.0, 0.0, 0.0)
    m.socket("flame1", _MR_PX, 0.665, -1.260)
    m.socket("flame2", -_MR_PX, 0.665, -1.260)
    m.socket("flame3", 0.0, 0.665, -1.250)
    return finish(m, (1.81, 0.70, -0.45), (0.0, 0.97, -0.45),
                  [(0.30, 0.80), (-0.30, 0.80), (1.61, -0.60), (-1.61, -0.60)], -1.27, engine_y=0.665)


# Nebula Sprinter, measured on the user's generated model (sections every 0.1 m): a yellow delta, a slate nose intake,
# a raised deck between two rails, green drop faces, blue piping down the flanks and out along the wings (and under),
# a slate keel, a slate canopy into a cockpit block and its ridge, three slate pods a side, three round engines
_NS_TAB = [  # (z, deck y, deck half, rail y, rail x, shoulder x, y, crease x, y, under x, y, keel x, y, its foot x, y)
    (2.34, 0.845, 0.19, 0.846, 0.205, 0.225, 0.838, 0.26, 0.80, 0.26, 0.505, 0.22, 0.497, 0.18, 0.495),
    (2.00, 0.88, 0.23, 0.882, 0.25, 0.27, 0.87, 0.33, 0.81, 0.28, 0.485, 0.23, 0.468, 0.17, 0.465),
    (1.60, 0.97, 0.25, 0.975, 0.31, 0.335, 0.905, 0.42, 0.84, 0.32, 0.45, 0.27, 0.40, 0.21, 0.375),
    (1.42, 1.00, 0.245, 1.025, 0.34, 0.363, 0.918, 0.46, 0.855, 0.345, 0.438, 0.30, 0.33, 0.21, 0.215),
    (1.00, 1.067, 0.25, 1.10, 0.377, 0.412, 0.949, 0.55, 0.896, 0.40, 0.424, 0.34, 0.368, 0.29, 0.222),
    (0.55, 1.125, 0.27, 1.157, 0.383, 0.462, 0.966, 0.62, 0.925, 0.52, 0.455, 0.37, 0.33, 0.28, 0.257),
    (0.12, 1.15, 0.25, 1.175, 0.37, 0.42, 1.05, 0.66, 0.945, 0.63, 0.47, 0.38, 0.30, 0.29, 0.27),
    (0.02, 1.16, 0.25, 1.173, 0.36, 0.40, 1.08, 0.58, 0.985, 0.62, 0.465, 0.34, 0.29, 0.27, 0.268),
    (-0.43, 1.10, 0.25, 1.10, 0.33, 0.45, 0.99, 0.575, 0.972, 0.60, 0.453, 0.35, 0.30, 0.27, 0.285),
    (-0.80, 1.07, 0.25, 1.07, 0.33, 0.52, 0.975, 0.77, 0.916, 0.45, 0.40, 0.38, 0.31, 0.315, 0.288),
    (-1.00, 1.05, 0.25, 1.05, 0.33, 0.555, 0.968, 0.88, 0.886, 0.44, 0.39, 0.40, 0.32, 0.35, 0.29),
    (-1.22, 1.05, 0.25, 1.05, 0.33, 0.535, 0.99, 0.88, 0.897, 0.425, 0.392, 0.39, 0.323, 0.353, 0.296),
    (-1.30, 1.05, 0.25, 1.05, 0.33, 0.535, 0.99, 0.56, 0.985, 0.425, 0.392, 0.39, 0.33, 0.353, 0.30)]
_NS_RIM = [  # (z, the half width, the rim's top y, its bottom y)
    (2.34, 0.32, 0.72, 0.605), (1.60, 0.54, 0.754, 0.594), (1.00, 0.73, 0.773, 0.58), (0.55, 0.86, 0.778, 0.58),
    (-0.43, 1.53, 0.735, 0.578), (-1.22, 1.54, 0.72, 0.59), (-1.30, 0.62, 0.96, 0.44)]
_NS_PIPE = [(2.34, 0.29), (1.80, 0.43), (1.42, 0.54), (1.00, 0.63), (0.55, 0.73), (0.12, 0.70), (0.02, 0.72),
            (-0.43, 0.97), (-0.80, 1.25), (-1.00, 1.22), (-1.22, 1.17), (-1.30, 0.60)]     # (z, the piping's centre x)
_NS_LABS = ["deck", "railin", "rail", "drop", "slope", "slope2", "pipe", "wing", "rim", "uwing", "upipe", "uslope",
            "band", "keel", "bottom"]


def _ns_ring(z):
    Yd, Xd, Yr, Xr, Xs, Ys, Xm, Ym, Xu, Yu, Xk, Yk, Xb, Yb = _lerp_tab(_NS_TAB, z)
    W, Yt, Yw = _lerp_tab(_NS_RIM, z)
    pc = _lerp_tab(_NS_PIPE, z)[0]
    p0, p1 = min(max(pc - 0.045, Xm + 0.01), W - 0.04), min(max(pc + 0.045, Xm + 0.03), W - 0.015)
    top = lambda x: Ym + (Yt - Ym) * (x - Xm) / (W - Xm)
    bot = lambda x: Yu + (Yw - Yu) * (x - Xu) / (W - Xu)
    right = [(0.0, Yd), (Xd, Yd), (Xd + 0.015, Yr), (Xr, Yr), (Xs, Ys), (Xm, Ym), (p0, top(p0)), (p1, top(p1)),
             (W, Yt), (W, Yw), (p1, bot(p1)), (p0, bot(p0)), (Xu, Yu), (Xk, Yk), (Xb, Yb), (0.0, Yb)]
    return [(x, y, z) for x, y in _nv_mirror(right)]


def _ns_body():
    def mat(i, j, c):
        lab, z = (_NS_LABS + _NS_LABS[::-1])[j], c[2]
        if i == 0:
            return SL                                   # the intake's recess
        if i == 1:
            return "paint1"                             # its frame
        if lab == "deck" and 0.5 < z < 1.9 or lab == "drop" and z < 1.9 or lab == "rail" and z < 1.0 \
                or lab == "band" and -0.43 < z < 1.0 \
                or lab in ("wing", "uwing") and 0.12 < z < 0.55 or lab == "slope2" and -1.22 < z < -1.0:
            return "paint2"                             # the green stripes and patches
        if lab in ("pipe", "upipe") and -1.22 < z < 2.0:
            return "blue"
        if lab in ("keel", "bottom") and z < 1.6 or lab == "band" and z < -0.43:
            return SL                                   # the keel
        return "paint1"
    nose = _ns_ring(2.34)
    inset = lambda z: [(x * 0.75, 0.67 + (y - 0.67) * 0.7, z) for x, y, _ in nose]
    rings = [inset(2.27), inset(2.34)] + [_ns_ring(r[0]) for r in _NS_TAB]
    return _shell(rings, mat)


def _ns_deck(rows, sec, mat):
    """A shell through rows (z, top y, ...): sec(top, ...) gives the right half of its section."""
    return _shell([[(x, y, r[0]) for x, y in _nv_mirror(sec(*r[1:]))] for r in rows], mat)


def _ns_pod(z0, z1):
    ring = [(0.30, 1.27), (0.44, 1.315), (0.575, 1.15), (0.575, 1.09), (0.52, 0.99), (0.38, 1.05)]
    sh = lambda z, k: [(0.45 + (x - 0.45) * k, 1.15 + (y - 1.15) * k, z) for x, y in ring]
    return _shell([sh(z0, 0.82), sh(z0 - 0.025, 1.0), sh(z1 + 0.025, 1.0), sh(z1, 0.82)], SL)


def hull_sprinter():
    m = Model("hull_sprinter", "Nebula Sprinter: a yellow delta, a slate nose intake, green stripes and blue piping, "
              "a slate keel, a slate canopy into a cockpit block, three slate pods a side, three round engines")
    m.add(_ns_body())
    # the canopy (opaque slate) rising back into the cockpit block, the block, the ridge on it (its front slate)
    dk = lambda z: _lerp_tab(_NS_TAB, z)[0]
    m.add(_ns_deck([(z, t, tw, bw, dk(z)) for z, t, tw, bw in
                    ((0.80, 1.115, 0.10, 0.16), (0.65, 1.175, 0.165, 0.215), (0.45, 1.29, 0.175, 0.225),
                     (0.25, 1.37, 0.18, 0.235), (0.05, 1.405, 0.19, 0.245), (-0.05, 1.41, 0.19, 0.245))],
                   lambda t, tw, bw, yd: [(0.0, t), (tw, t), (bw, yd - 0.005), (bw, yd - 0.06), (0.0, yd - 0.06)], SL))
    m.add(_ns_deck([(0.02, 1.47), (-0.90, 1.47), (-1.22, 1.415), (-1.29, 1.40)],
                   lambda t: [(0.0, t), (0.17, t), (0.21, t - 0.02), (0.42, 1.06), (0.56, 0.975), (0.56, 0.92),
                              (0.0, 0.92)], "paint1"))
    m.add(_ns_deck([(-0.12, 1.60), (-0.16, 1.625), (-0.32, 1.625), (-0.65, 1.625), (-0.80, 1.54), (-0.87, 1.475)],
                   lambda t: [(0.0, t), (0.14, t), (0.175, min(t, 1.48)), (0.175, 1.44), (0.0, 1.44)],
                   lambda i, j, c: SL if i <= 1 else "paint1"))
    for k in range(3):                                  # the pods
        z0 = -0.335 - 0.26 * k
        for sx in (1, -1):
            m.add(_ns_pod(z0, z0 - 0.24).sc(sx, 1, 1) if sx < 0 else _ns_pod(z0, z0 - 0.24))
    # the engines (decagons, their flats' mean on the circle): the top one slate, the two lower ones out of paint1
    # collars; slate exits recessed
    top = [(0.0, -1.28), (0.25, -1.28), (0.25, -1.70), (0.23, -1.82), (0.14, -1.82), (0.12, -1.76), (0.0, -1.76)]
    k = 1.03
    m.add(revolve_z([(r * k, z) for r, z in top], 10, SL, a0=PI / 10).tr(0.0, 1.11, 0.0))
    low = [(0.0, -1.28), (0.29, -1.28), (0.29, -1.46), (0.225, -1.50), (0.225, -1.74), (0.21, -1.86), (0.13, -1.86),
           (0.11, -1.80), (0.0, -1.80)]
    for sx in (1, -1):
        m.add(revolve_z([(r * k, z) for r, z in low], 10, lambda i, j, c: "paint1" if i <= 2 else SL,
                        a0=PI / 10).tr(sx * 0.24, 0.765, 0.0))
    # its engines are built in: the engine part is not drawn (its stats count), the flames leave its three exits;
    # the canopy is opaque, no pilot drawn
    m.socket("noengine", 0.0, 0.0, 0.0)
    m.socket("nopilot", 0.0, 0.0, 0.0)
    m.socket("flame1", 0.0, 1.11, -1.83)
    m.socket("flame2", 0.24, 0.765, -1.87)
    m.socket("flame3", -0.24, 0.765, -1.87)
    return finish(m, (1.53, 0.655, -0.85), (0.0, 1.15, 0.30),
                  [(0.30, 1.30), (-0.30, 1.30), (0.90, -1.00), (-0.90, -1.00)], -1.30, engine_y=0.88)


# Sonic Wave, measured on the user's generated model (sections every 0.05 m): a purple arrow, a flat belly, a deck
# sloping to low sides, paint2 stripes and a deck panel, a slate canopy ramp into a cockpit hump, two swept wings a side
# (the upper with a paint2 band, the lower drooping with a paint2 patch under it), two square paint2 nozzles
_SW_TAB = [  # (z, deck y, deck half, shoulder x, y, slope foot x, y, side x, side top y, belly y)
    (1.86, 0.850, 0.11, 0.150, 0.825, 0.160, 0.80, 0.175, 0.77, 0.700),
    (1.80, 0.870, 0.13, 0.180, 0.840, 0.190, 0.80, 0.206, 0.77, 0.680),
    (1.60, 0.900, 0.15, 0.215, 0.860, 0.245, 0.81, 0.264, 0.78, 0.671),
    (1.42, 0.930, 0.16, 0.230, 0.890, 0.300, 0.82, 0.320, 0.79, 0.668),
    (1.00, 0.984, 0.18, 0.300, 0.925, 0.430, 0.83, 0.455, 0.80, 0.667),
    (0.94, 0.995, 0.18, 0.310, 0.930, 0.455, 0.83, 0.480, 0.80, 0.667),
    (0.60, 1.050, 0.18, 0.340, 0.965, 0.555, 0.83, 0.587, 0.79, 0.667),
    (0.15, 1.100, 0.15, 0.290, 1.050, 0.690, 0.83, 0.740, 0.78, 0.667),
    (0.00, 1.110, 0.15, 0.260, 1.100, 0.750, 0.82, 0.796, 0.78, 0.667),
    (-0.20, 1.130, 0.20, 0.260, 1.115, 0.765, 0.80, 0.805, 0.77, 0.667),
    (-0.50, 1.140, 0.25, 0.500, 0.985, 0.760, 0.80, 0.800, 0.77, 0.667),
    (-1.30, 1.150, 0.30, 0.500, 0.990, 0.750, 0.80, 0.790, 0.77, 0.667),
    (-1.45, 1.150, 0.30, 0.470, 1.040, 0.690, 0.85, 0.720, 0.82, 0.667),
    (-1.62, 1.150, 0.36, 0.490, 1.030, 0.570, 0.93, 0.600, 0.89, 0.665),
    (-1.70, 1.150, 0.38, 0.480, 1.050, 0.520, 0.99, 0.550, 0.95, 0.660),
    (-1.90, 1.160, 0.33, 0.450, 1.045, 0.470, 1.00, 0.480, 0.96, 0.665),
    (-2.16, 1.150, 0.30, 0.390, 1.050, 0.410, 1.01, 0.420, 0.97, 0.685)]
_SW_ST1 = [(1.45, 0.235, 0.235), (1.42, 0.235, 0.245), (1.00, 0.26, 0.31), (0.60, 0.28, 0.36), (0.15, 0.31, 0.43),
           (0.00, 0.42, 0.42)]
_SW_ST2 = [(0.96, 0.43, 0.43), (0.94, 0.43, 0.445), (0.60, 0.49, 0.535), (0.15, 0.58, 0.67), (0.00, 0.66, 0.73),
           (-0.20, 0.72, 0.75), (-0.50, 0.73, 0.755), (-0.52, 0.74, 0.74)]   # (z, the stripes' edges x)
_SW_LABS = ["panel", "deck", "shoulder", "st1", "slope", "st2", "slope", "chamfer", "side", "bevel", "belly"]


def _sw_ring(z):
    """A section: the deck, a shoulder and a slope (their crease at S) carrying the two stripes, a low side, a bevel,
    the flat belly; out of a stripe's run its edges sit on the crease."""
    T, D, Sx, Sy, Bx, By, W, Yw, Yb = _lerp_tab(_SW_TAB, z)
    on = lambda x: (x, float(np.interp(x, [D, Sx, Bx], [T, Sy, By])))
    xs, lo = [], D
    for tab in (_SW_ST1, _SW_ST2):          # the stripes' edges, kept on the slope and in order
        for x in (_lerp_tab(tab, z) if tab[-1][0] <= z <= tab[0][0] else (Sx, Sx)):
            lo = min(max(x, lo + 0.002), Bx - 0.004)
            xs.append(lo)
    right = [(0.0, T), (0.12, T), (D, T)] + [on(x) for x in xs] + \
            [(Bx, By), (W, Yw), (W, Yb + 0.035), (W - 0.05, Yb), (0.0, Yb)]
    return [(x, y, z) for x, y in _nv_mirror(right)]


def _sw_body():
    labs = _SW_LABS + _SW_LABS[::-1]

    def mat(i, j, c):
        lab, z = labs[j], c[2]
        if lab == "panel" and 0.15 < z < 0.94 or lab == "st1" and 0.0 < z < 1.42 or lab == "st2" and -0.5 < z < 0.94:
            return "paint2"
        return "paint1"
    return _shell([_sw_ring(r[0]) for r in _SW_TAB], mat)


def _sw_wing(sts, band_top, mat_band):
    """A flat swept wing along +X: stations (x, z leading, z trailing, centre y, half thick, band z0, z1) -- the band
    a paint2 strip across its top (band_top) or its bottom; the tip cap paint2."""
    rings = []
    for x, zl, zt, yc, h, b0, b1 in sts:
        top = [(yc + h, zl - 0.05), (yc + h, b0 if band_top else zl - 0.06), (yc + h, b1 if band_top else zl - 0.07),
               (yc + h, zt + 0.03)]
        bot = [(yc - h, zt + 0.03), (yc - h, zt + 0.04 if band_top else b1), (yc - h, zt + 0.05 if band_top else b0),
               (yc - h, zl - 0.05)]
        rings.append([(x, yc, zl)] + [(x, y, z) for y, z in top] + [(x, yc, zt)] + [(x, y, z) for y, z in bot])
    n = len(sts)

    def mat(i, j, c):
        if i == n - 1 and j == 0:
            return "paint2"                     # the tip
        if mat_band(i) and (j == 2 if band_top else j == 7):
            return "paint2"
        return "paint1"
    return _shell(rings, mat)


def _sw_nozzle(sx):
    r = lambda z, w, ch: [(sx * x, y, z) for x, y, _ in oct_ring(0.21, 0.905, z, w, w, ch)]
    return _shell([r(-2.27, 0.075, 0.025), r(-2.34, 0.075, 0.025), r(-2.34, 0.15, 0.045), r(-2.14, 0.15, 0.045)],
                  lambda i, j, c: SL if i == 0 else "paint2")


def hull_wave():
    m = Model("hull_wave", "Sonic Wave: a purple arrow, a flat belly, paint2 stripes and deck panel, a slate canopy "
              "ramp into a cockpit hump, two swept wings a side, two square paint2 nozzles")
    m.add(_sw_body())
    # the canopy: a slate ramp along the deck, into the cockpit hump (its front slate too)
    cs = lambda t: [(0.0, t), (0.12, t), (0.16, t - 0.03), (0.19, 1.14), (0.19, 1.05), (0.0, 1.05)]
    m.add(_ns_deck([(z, t) for z, t in ((-0.03, 1.12), (-0.30, 1.222), (-0.60, 1.34), (-0.75, 1.36), (-1.02, 1.37))],
                   cs, lambda i, j, c: SL if j in (0, 1, 2, 8, 9, 10) else "paint1"))
    hs = lambda t, tw, bw: [(0.0, t), (tw, t), (bw, 1.155), (bw, 1.10), (0.0, 1.10)]
    m.add(_ns_deck([(-1.00, 1.412, 0.165, 0.29), (-1.55, 1.405, 0.165, 0.29), (-1.70, 1.37, 0.16, 0.27),
                    (-1.90, 1.33, 0.15, 0.24), (-2.16, 1.25, 0.12, 0.20)], hs,
                   lambda i, j, c: SL if i == 0 and c[2] > -1.01 else "paint1"))
    for sx in (1, -1):
        # the upper wing (a paint2 band behind its leading edge) and the lower one (drooping, a paint2 patch under it)
        up = _sw_wing([(0.35, -0.245, -1.669, 1.07, 0.048, -0.50, -0.55),
                       (0.48, -0.345, -1.669, 1.08, 0.048, -0.50, -0.64),
                       (1.40, -1.050, -1.669, 1.145, 0.046, -1.30, -1.42),
                       (1.586, -1.200, -1.669, 1.155, 0.044, -1.45, -1.45)], True, lambda i: i == 1)
        lo = _sw_wing([(0.70, 0.138, -1.38, 0.735, 0.053, -0.68, -1.15),
                       (0.83, -0.054, -1.41, 0.725, 0.052, -0.68, -1.15),
                       (1.40, -0.898, -1.554, 0.625, 0.048, -1.38, -1.38),
                       (1.58, -1.164, -1.60, 0.605, 0.045, -1.45, -1.45)], False, lambda i: i == 1)
        for p in (up, lo, _sw_nozzle(1)):
            m.add(p.sc(sx, 1, 1) if sx < 0 else p)
    # its engines are built in: the engine part is not drawn (its stats count), the flames leave the two nozzles;
    # the canopy is opaque, no pilot drawn
    m.socket("noengine", 0.0, 0.0, 0.0)
    m.socket("nopilot", 0.0, 0.0, 0.0)
    m.socket("flame1", 0.21, 0.905, -2.30)
    m.socket("flame2", -0.21, 0.905, -2.30)
    return finish(m, (1.58, 0.605, -1.40), (0.0, 1.15, -0.60),
                  [(0.30, 1.20), (-0.30, 1.20), (0.60, -1.30), (-0.60, -1.30)], -2.34, engine_y=0.905)


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
    "hull_sprinter": hull_sprinter,
    "hull_wave": hull_wave,
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
