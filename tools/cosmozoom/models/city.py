#!/usr/bin/env python3
#
# city.py -- Cosmo Zoom!'s city props (the user's art direction, 2026-10-15: a futuristic city raced by day, at
# dusk and at night): skyscrapers -- walls in paint1 (the game recolours them per circuit: cream by day, dark blue
# by night), trims and terraces in paint2, windows in *_glow strips --, neon billboards (a glowing panel with UVs
# 0..1 for the game's texture), antenna masts, elevated highway pieces, a flying traffic ship, street lamps, neon
# arches, holo-rings and the start gate.
#
# Props stand on y = 0, their origin at the base centre; sizes in metres. Glow strips stand a few centimetres
# proud of the walls (they are unlit and drawn added, without outline).
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
#
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from czmodel import PI, Model, Part, loft, revolve, tube  # noqa: E402
from ships import box, fan, oct_ring, blade  # noqa: E402

SCRIPT = "city.py"


# ---- building blocks ---------------------------------------------------------------------------------------------
def chamfer_sq(w, d, ch):
    """A rectangle (half sizes w x d) with chamfered corners, counter-clockwise seen from above (x, z)."""
    return [(w, -d + ch), (w, d - ch), (w - ch, d), (-w + ch, d), (-w, d - ch), (-w, -d + ch), (-w + ch, -d), (w - ch, -d)]


def ngon(r, n, a0=0.0):
    return [(r * math.cos(a0 + 2 * PI * k / n), r * math.sin(a0 + 2 * PI * k / n)) for k in range(n)]


def prism(ring, y0, y1, mat="paint1", top="paint2", bottom=False, scale_top=1.0):
    """A vertical prism through a ring (x, z) from y0 to y1 (its top scaled by scale_top), the top capped."""
    r0 = [(x, y0, z) for x, z in ring]
    r1 = [(x * scale_top, y1, z * scale_top) for x, z in ring]
    p = loft([r0, r1], mat, cap0=bottom, cap1=False, orient=False)
    if top:
        fan(p, r1, top)
    return p.orient()


def slab(ring, y, h, grow, mat="paint2"):
    """A terrace / ledge: the ring grown by `grow`, h thick, its top at y."""
    rr = [(x * grow, z * grow) for x, z in ring]
    return prism(rr, y - h, y, mat, mat, bottom=True)


def strips_vertical(ring, faces, y0, y1, n, frac=0.6, mat="yellow_glow", out=0.06):
    """Vertical window strips on some faces of a prism (faces: edge indices of the ring), n per face."""
    p = Part()
    m = len(ring)
    for e in faces:
        a, b = np.array(ring[e % m], float), np.array(ring[(e + 1) % m], float)
        d = b - a
        L = np.linalg.norm(d)
        nrm = np.array([d[1], -d[0]]) / L          # outward for a counter-clockwise ring (seen from +y)
        # (x, z) ring seen from above with +z down the page: check the side with the centroid
        cen = np.mean(np.array(ring, float), axis=0)
        if np.dot(nrm, (a + b) / 2 - cen) < 0:
            nrm = -nrm
        w = L / n * frac
        for k in range(n):
            c = a + d * (k + 0.5) / n
            p0 = c - d / L * w / 2 + nrm * out
            p1 = c + d / L * w / 2 + nrm * out
            q = Part()
            q.v = np.array([(p0[0], y0, p0[1]), (p1[0], y0, p1[1]), (p1[0], y1, p1[1]), (p0[0], y1, p0[1])])
            q.f = [((0, 1, 2, 3), mat)]
            face_out(q, (nrm[0], 0, nrm[1]))
            p.add(q)
    return p


def bands(ring, ys, h, mat="cyan_glow", out=0.06, faces=None):
    """Horizontal window bands (h high) around a prism at the heights ys (on the given faces, or all)."""
    p = Part()
    m = len(ring)
    cen = np.mean(np.array(ring, float), axis=0)
    for e in (faces if faces is not None else range(m)):
        a, b = np.array(ring[e % m], float), np.array(ring[(e + 1) % m], float)
        d = b - a
        nrm = np.array([d[1], -d[0]]) / np.linalg.norm(d)
        if np.dot(nrm, (a + b) / 2 - cen) < 0:
            nrm = -nrm
        a2, b2 = a + nrm * out, b + nrm * out
        for y in ys:
            q = Part()
            q.v = np.array([(a2[0], y, a2[1]), (b2[0], y, b2[1]), (b2[0], y + h, b2[1]), (a2[0], y + h, a2[1])])
            q.f = [((0, 1, 2, 3), mat)]
            face_out(q, (nrm[0], 0, nrm[1]))
            p.add(q)
    return p


def face_out(q, n):
    P = q.v
    idx = q.f[0][0]
    fn = np.cross(P[idx[1]] - P[idx[0]], P[idx[2]] - P[idx[0]])
    if np.dot(fn, n) < 0:
        q.flip()
    return q


def mast(x, y, z, h, r=0.25, light="red_glow", mat="grey"):
    """A thin antenna mast with a glowing light on its top."""
    p = tube([(x, y, z), (x, y + h, z)], r, 6, mat, cap0=False)
    p.add(revolve([(0.0, -r * 2.2), (r * 2.2, 0.0), (0.0, r * 2.2)], 6, light).tr(x, y + h + r * 1.5, z))
    return p


def panel(w, h, mat="magenta_glow", two_sided=True, z=0.0):
    """A billboard's picture: a quad in the XY plane (w x h, its centre at the origin) facing +Z, UVs 0..1
    (and a mirrored one facing -Z)."""
    p = Part()
    p.v = np.array([(-w / 2, -h / 2, z), (w / 2, -h / 2, z), (w / 2, h / 2, z), (-w / 2, h / 2, z)])
    p.uv = np.array([(0, 0), (1, 0), (1, 1), (0, 1)], float)
    p.f = [((0, 1, 2, 3), mat)]
    face_out(p, (0, 0, 1))
    if two_sided:
        q = Part()
        q.v = np.array([(w / 2, -h / 2, -z), (-w / 2, -h / 2, -z), (-w / 2, h / 2, -z), (w / 2, h / 2, -z)])
        q.uv = np.array([(0, 0), (1, 0), (1, 1), (0, 1)], float)
        q.f = [((0, 1, 2, 3), mat)]
        face_out(q, (0, 0, -1))
        p.add(q)
    return p


def frame(w, h, t, d, mat="darkgrey"):
    """A rectangular frame (w x h outside, bars t wide, d deep) in the XY plane, centred at the origin."""
    p = Part()
    p.add(box(-w / 2, w / 2, h / 2 - t, h / 2, -d / 2, d / 2, mat))
    p.add(box(-w / 2, w / 2, -h / 2, -h / 2 + t, -d / 2, d / 2, mat))
    p.add(box(-w / 2, -w / 2 + t, -h / 2 + t, h / 2 - t, -d / 2, d / 2, mat))
    p.add(box(w / 2 - t, w / 2, -h / 2 + t, h / 2 - t, -d / 2, d / 2, mat))
    return p


# ---- the skyscrapers -----------------------------------------------------------------------------------------------
def tower_setback():
    m = Model("tower_setback", "a skyscraper with two setbacks, terraces, vertical window strips, a mast")
    r1, r2, r3 = chamfer_sq(9, 9, 2.0), chamfer_sq(7, 7, 1.6), chamfer_sq(4.8, 4.8, 1.2)
    m.add(prism(r1, 0, 40))
    m.add(slab(r1, 40.6, 0.8, 1.06))
    m.add(prism(r2, 40.6, 62))
    m.add(slab(r2, 62.6, 0.8, 1.08))
    m.add(prism(r3, 62.6, 74, top="darkgrey"))
    m.add(box(-2.5, 2.5, 74, 76.5, -2.5, 2.5, "paint2", 0.4))
    m.add(strips_vertical(r1, (0, 2, 4, 6), 3, 38, 3, 0.5, "yellow_glow"))
    m.add(strips_vertical(r2, (0, 2, 4, 6), 42, 60, 2, 0.5, "yellow_glow"))
    m.add(bands(r3, (65, 69), 1.4, "cyan_glow", faces=(0, 2, 4, 6)))
    m.add(mast(1.0, 76.5, 1.0, 12, 0.3))
    return m


def tower_chamfer():
    m = Model("tower_chamfer", "a tall chamfered tower, window bands, a crown and a spire")
    r = chamfer_sq(7.5, 7.5, 2.6)
    m.add(prism(r, 0, 84))
    m.add(bands(r, [6 + 7 * k for k in range(11)], 2.2, "cyan_glow", faces=(0, 2, 4, 6)))
    m.add(bands(r, [6 + 7 * k for k in range(11)], 2.2, "cyan_glow", faces=(1, 3, 5, 7)))
    # the crown: a band of paint2 leaning in, a spire
    rc = chamfer_sq(7.9, 7.9, 2.8)
    m.add(prism(rc, 84, 88, "paint2", "darkgrey", scale_top=0.7))
    m.add(revolve([(0.0, 88), (1.4, 88), (0.0, 106)], 6, "silver"))
    m.add(revolve([(0.0, -0.6), (0.6, 0.0), (0.0, 0.6)], 6, "red_glow").tr(0, 106.5, 0))
    return m


def tower_round():
    m = Model("tower_round", "a cylindrical tower, ring ledges, glowing bands, a rooftop disc")
    r = ngon(7.0, 12, PI / 12)
    m.add(prism(r, 0, 58))
    for y in (20, 40):
        m.add(slab(r, y + 0.5, 1.0, 1.07))
    m.add(bands(r, [4 + 4 * k for k in range(4)] + [24 + 4 * k for k in range(4)] + [44 + 4 * k for k in range(3)], 1.6,
                "magenta_glow", faces=range(0, 12, 2)))
    m.add(prism(ngon(8.4, 12, PI / 12), 58, 59.2, "paint2", "darkgrey", bottom=True))
    m.add(revolve([(0.0, 59.2), (3.0, 59.2), (3.0, 61.5), (0.0, 61.5)], 8, "paint2"))
    m.add(mast(0.0, 61.5, 0.0, 9, 0.28))
    return m


def tower_stepped():
    m = Model("tower_stepped", "a stepped tower: four blocks with terraces and lime strips")
    sizes = [(11, 0, 22), (9, 22, 40), (7, 40, 54), (5, 54, 64)]
    for k, (w, y0, y1) in enumerate(sizes):
        r = chamfer_sq(w, w * 0.85, 1.4)
        m.add(prism(r, y0, y1, top="paint2"))
        m.add(slab(r, y1 + 0.4, 0.8, 1.05))
        m.add(strips_vertical(r, (0, 4), y0 + 2.5, y1 - 2.5, 2, 0.45, "lime_glow"))
        m.add(bands(r, [y0 + (y1 - y0) * 0.5], 1.2, "yellow_glow", faces=(2, 6)))
    m.add(mast(-2.0, 64.4, -1.5, 8, 0.25))
    return m


def tower_twin():
    m = Model("tower_twin", "two slim towers joined by a sky bridge")
    for sx in (1, -1):
        r = [(x + 7 * sx, z) for x, z in chamfer_sq(4.5, 4.5, 1.2)]
        h = 70 if sx > 0 else 60
        m.add(prism(r, 0, h))
        m.add(bands(r, [5 + 6 * k for k in range(int((h - 8) / 6))], 1.8, "cyan_glow" if sx > 0 else "yellow_glow",
                    faces=(0, 2, 4, 6)))
        m.add(prism([(x * 0.9 + 7 * sx * 0.1, z * 0.9) for x, z in r], h, h + 3, "paint2", "darkgrey"))
    m.add(box(-3.0, 3.0, 38, 42, -2.5, 2.5, "paint2", 0.5))
    m.add(bands([(-3, -2.5), (3, -2.5), (3, 2.5), (-3, 2.5)], (39.2,), 1.4, "magenta_glow", faces=(0, 2)))
    m.add(mast(7.0, 73, 0.0, 10, 0.25))
    return m


def tower_slim():
    m = Model("tower_slim", "a slender hexagonal needle tower, a long spire")
    r = ngon(4.5, 6, PI / 6)
    m.add(prism(r, 0, 70, scale_top=0.75))
    m.add(bands(r, [5 + 8 * k for k in range(8)], 1.5, "cyan_glow"))
    m.add(slab([(x * 0.75, z * 0.75) for x, z in r], 71, 1.0, 1.25))
    m.add(revolve([(0.0, 71), (1.8, 71), (0.0, 96)], 6, "silver"))
    m.add(revolve([(0.0, -0.6), (0.6, 0.0), (0.0, 0.6)], 6, "red_glow").tr(0, 96.5, 0))
    return m


# ---- billboards, masts, lamps ----------------------------------------------------------------------------------------
def billboard(name, glow, w=12.0, h=6.0, pole=14.0, title=""):
    m = Model(name, title or "a neon billboard on a pole (its panel: UVs 0..1)")
    y = pole + h / 2
    pic = panel(w - 0.8, h - 0.8, glow).tr(0, y, 0)
    m.add(pic, "panel")
    fr = frame(w, h, 0.45, 0.8, "darkgrey").tr(0, y, 0)
    m.add(fr)
    m.add(box(-w / 2 - 0.1, w / 2 + 0.1, y + h / 2, y + h / 2 + 0.3, -0.5, 0.5, "paint2"))    # a coloured top lip
    m.add(box(-0.5, 0.5, 0, pole + 0.4, -0.5, 0.5, "grey", 0.15))
    m.add(box(-1.4, 1.4, 0, 0.6, -1.0, 1.0, "darkgrey", 0.2))
    return m


def billboard_wall():
    m = Model("billboard_wall", "a neon billboard for a wall (its back at z = 0, facing +Z; panel UVs 0..1)")
    w, h = 10.0, 5.0
    pic = panel(w - 0.7, h - 0.7, "cyan_glow", two_sided=False).tr(0, h / 2, 0.45)
    m.add(pic, "panel")
    m.add(frame(w, h, 0.4, 0.8, "darkgrey").tr(0, h / 2, 0.4))
    m.add(box(-w / 2, w / 2, -0.2, 0.0, 0.0, 0.5, "paint2"))
    return m


def billboard_tall():
    m = Model("billboard_tall", "a tall vertical neon sign on a frame (its panel: UVs 0..1)")
    w, h = 4.0, 12.0
    y = 4.0 + h / 2
    m.add(panel(w - 0.6, h - 0.6, "lime_glow").tr(0, y, 0), "panel")
    m.add(frame(w, h, 0.35, 0.7, "darkgrey").tr(0, y, 0))
    for sx in (1, -1):
        m.add(box(sx * 1.2 - 0.25, sx * 1.2 + 0.25, 0, 4.2, -0.25, 0.25, "grey"))
    m.add(box(-w / 2 - 0.1, w / 2 + 0.1, y + h / 2, y + h / 2 + 0.3, -0.45, 0.45, "paint2"))
    return m


def antenna_mast():
    m = Model("antenna_mast", "a lattice antenna mast, a red light on top")
    H = 30.0
    legs = [(1.6 * math.cos(a), 1.6 * math.sin(a)) for a in (PI / 2, PI / 2 + 2 * PI / 3, PI / 2 + 4 * PI / 3)]
    for (x, z) in legs:
        m.add(tube([(x, 0, z), (x * 0.25, H, z * 0.25)], 0.14, 4, "grey", cap0=False))
    for k in range(5):
        y0, y1 = H * k / 5, H * (k + 1) / 5
        s0, s1 = 1 - 0.75 * k / 5, 1 - 0.75 * (k + 1) / 5
        for i in range(3):
            a, b = legs[i], legs[(i + 1) % 3]
            m.add(tube([(a[0] * s0, y0, a[1] * s0), (b[0] * s1, y1, b[1] * s1)], 0.07, 4, "grey", cap0=False))
    m.add(box(-1.2, 1.2, 0, 0.5, -1.2, 1.2, "darkgrey"))
    m.add(tube([(0, H - 0.5, 0), (0, H + 5, 0)], 0.12, 6, "silver", cap0=False))
    m.add(revolve([(0.0, -0.5), (0.5, 0.0), (0.0, 0.5)], 6, "red_glow").tr(0, H + 5.5, 0))
    for y in (H * 0.4, H * 0.7):
        m.add(revolve([(0.0, -0.35), (0.35, 0.0), (0.0, 0.35)], 6, "red_glow").tr(0, y, 0))
    return m


def street_lamp():
    m = Model("street_lamp", "a street lamp: a pole, an arm, a neon light bar")
    m.add(box(-0.6, 0.6, 0, 0.4, -0.6, 0.6, "darkgrey", 0.1))
    m.add(tube([(0, 0.3, 0), (0, 7.0, 0), (0.0, 7.6, 0.4), (0, 7.8, 2.2)], 0.14, 6, "grey", cap0=False))
    head = box(-0.35, 0.35, 7.45, 7.95, 1.2, 3.6, "paint2", 0.08)
    m.add(head)
    m.add(box(-0.22, 0.22, 7.38, 7.46, 1.35, 3.45, "cyan_glow"))
    return m


# ---- the track's furniture -------------------------------------------------------------------------------------------
def arch_beams(width, height, depth, thick, mat="paint1"):
    """An angular arch (a half octagon) across the track: two legs, two slanted shoulders, the top beam."""
    w = width / 2
    pts = [(-w, 0), (-w, height * 0.72), (-w * 0.62, height), (w * 0.62, height), (w, height * 0.72), (w, 0)]
    p = Part()
    for a, b in zip(pts, pts[1:]):
        a, b = np.array(a, float), np.array(b, float)
        d = b - a
        n = np.array([-d[1], d[0]]) / np.linalg.norm(d)
        # a beam: the quad's 4 corners in xy, extruded in z
        q = [a - n * thick / 2, b - n * thick / 2, b + n * thick / 2, a + n * thick / 2]
        ext = d / np.linalg.norm(d) * thick * 0.42
        q = [q[0] - ext, q[1] + ext, q[2] + ext, q[3] - ext]
        r0 = [(x, y, -depth / 2) for x, y in q]
        r1 = [(x, y, depth / 2) for x, y in q]
        p.add(loft([r0, r1], mat))
    return p, pts


def neon_arch():
    m = Model("neon_arch", "an angular neon arch across the track (18 m wide, 10 m high)")
    p, pts = arch_beams(18.0, 10.0, 1.6, 1.2, "paint1")
    m.add(p)
    # neon strips along the inner edge, both faces
    for a, b in zip(pts, pts[1:]):
        for zf, mat in ((0.84, "magenta_glow"), (-0.84, "cyan_glow")):
            a2, b2 = np.array(a, float), np.array(b, float)
            d = b2 - a2
            n = np.array([-d[1], d[0]]) / np.linalg.norm(d)
            inner = -n if np.dot(n, -(a2 + b2) / 2 + np.array([0, 5])) < 0 else n
            s0, s1 = a2 + inner * 0.25, b2 + inner * 0.25
            t0, t1 = a2 + inner * 0.55, b2 + inner * 0.55
            q = Part()
            q.v = np.array([(s0[0], s0[1], zf), (s1[0], s1[1], zf), (t1[0], t1[1], zf), (t0[0], t0[1], zf)])
            q.f = [((0, 1, 2, 3), mat)]
            face_out(q, (0, 0, np.sign(zf)))
            m.add(q)
    for sx in (1, -1):
        m.add(box(sx * 9 - 1.2, sx * 9 + 1.2, 0, 0.8, -1.3, 1.3, "darkgrey", 0.2))
    return m


def holo_ring():
    m = Model("holo_ring", "a holo-ring floating over the track (16 m across): a glowing band in a thin frame")
    R, segs = 8.0, 24
    glow = Part()
    rim = Part()
    for k in range(segs):
        a0, a1 = 2 * PI * k / segs, 2 * PI * (k + 1) / segs
        for zf in (0.25, -0.25):
            q = Part()
            pts = [(R * math.cos(a0), R + R * math.sin(a0)), (R * math.cos(a1), R + R * math.sin(a1)),
                   ((R - 0.9) * math.cos(a1), R + (R - 0.9) * math.sin(a1)), ((R - 0.9) * math.cos(a0), R + (R - 0.9) * math.sin(a0))]
            q.v = np.array([(x, y, zf) for x, y in pts])
            q.f = [((0, 1, 2, 3), "cyan_glow")]
            face_out(q, (0, 0, np.sign(zf)))
            glow.add(q)
    # the rim: a thin square-section ring outside the glow
    for k in range(segs):
        a0, a1 = 2 * PI * k / segs, 2 * PI * (k + 1) / segs

        def c(a, r, z):
            return (r * math.cos(a), R + r * math.sin(a), z)
        r0 = [c(a0, R, -0.3), c(a0, R + 0.4, -0.3), c(a0, R + 0.4, 0.3), c(a0, R, 0.3)]
        r1 = [c(a1, R, -0.3), c(a1, R + 0.4, -0.3), c(a1, R + 0.4, 0.3), c(a1, R, 0.3)]
        seg = loft([r0, r1], "paint2" if k % 6 == 0 else "paint1", cap0=False, cap1=False, orient=False)
        rim.add(seg)
    from ships import fix_out_torus
    rim = fix_out_torus(rim, (0, R, 0), R + 0.2)
    m.add(glow, "glow")
    m.add(rim)
    m.notes.append("Its centre is 8 m above the origin: place the origin on the track.")
    return m


def checker_banner(w, h, cols, rows, y0, zf, depth=0.25):
    """A thin banner of black and white squares, both faces."""
    p = Part()
    cw, rh = w / cols, h / rows
    for i in range(cols):
        for j in range(rows):
            mt = "ink" if (i + j) % 2 == 0 else "white"
            x0, x1 = -w / 2 + i * cw, -w / 2 + (i + 1) * cw
            y_0, y_1 = y0 + j * rh, y0 + (j + 1) * rh
            for zz, n in ((zf + depth / 2, 1), (zf - depth / 2, -1)):
                q = Part()
                q.v = np.array([(x0, y_0, zz), (x1, y_0, zz), (x1, y_1, zz), (x0, y_1, zz)])
                q.f = [((0, 1, 2, 3), mt if n > 0 else ("white" if mt == "ink" else "ink"))]
                face_out(q, (0, 0, n))
                p.add(q)
    p.add(box(-w / 2, w / 2, y0 - 0.15, y0, zf - depth / 2 - 0.02, zf + depth / 2 + 0.02, "darkgrey"))
    return p


def start_gate():
    m = Model("start_gate", "the start gate: a neon arch over the 16 m track, a checker banner, three lights")
    p, pts = arch_beams(19.0, 9.0, 2.0, 1.4, "paint1")
    m.add(p)
    # paint2 caps where the legs meet the shoulders
    for sx in (1, -1):
        m.add(box(sx * 9.5 - 1.0, sx * 9.5 + 1.0, 6.2, 6.8, -1.1, 1.1, "paint2"))
        m.add(box(sx * 9.5 - 1.3, sx * 9.5 + 1.3, 0, 1.0, -1.4, 1.4, "darkgrey", 0.25))
        # neon strips up the legs (front faces)
        for zf in (1.01, -1.01):
            q = Part()
            x0, x1 = sx * 9.5 - 0.25, sx * 9.5 + 0.25
            q.v = np.array([(x0, 1.2, zf), (x1, 1.2, zf), (x1, 6.0, zf), (x0, 6.0, zf)])
            q.f = [((0, 1, 2, 3), "magenta_glow")]
            face_out(q, (0, 0, np.sign(zf)))
            m.add(q)
    m.add(checker_banner(11.0, 1.6, 14, 2, 6.6, 0.0))
    # the three lights in a dark housing over the banner (o light1..3: the game colours them)
    m.add(box(-3.9, 3.9, 8.2, 10.5, -0.7, 0.7, "darkgrey", 0.3))
    for k, x in enumerate((-2.5, 0.0, 2.5)):
        light = revolve([(0.0, 0.0), (0.88, 0.0), (0.88, 0.22), (0.0, 0.30)], 10, "red_glow" if k < 2 else "lime_glow")
        light.rot("x", 90)
        front = light.copy().tr(x, 9.35, 0.70)
        back = light.copy().rot("y", 180).tr(x, 9.35, -0.70)
        m.add(front.add(back), "light%d" % (k + 1))
    return m


# ---- the background: highways, traffic -------------------------------------------------------------------------------
def highway_piece(name, angle, radius, title):
    m = Model(name, title)
    W, deck, H = 12.0, 1.4, 18.0
    n = 6 if angle else 1
    L = radius * math.radians(angle) if angle else 40.0
    segs = []
    for k in range(n + 1):
        t = k / n
        if angle:
            a = math.radians(angle) * (t - 0.5)
            cx, cz = radius - radius * math.cos(a), radius * math.sin(a)
            tx, tz = math.sin(a), math.cos(a)
        else:
            cx, cz, tx, tz = 0.0, L * (t - 0.5), 0.0, 1.0
        nx, nz = tz, -tx
        segs.append((cx, cz, nx, nz))

    def ring(cx, cz, nx, nz):
        w = W / 2
        return [(cx + nx * w, H - deck, cz + nz * w), (cx + nx * (w + 0.3), H - deck * 0.4, cz + nz * (w + 0.3)),
                (cx + nx * w, H, cz + nz * w), (cx - nx * w, H, cz - nz * w),
                (cx - nx * (w + 0.3), H - deck * 0.4, cz - nz * (w + 0.3)), (cx - nx * w, H - deck, cz - nz * w)]

    def mat(i, j, c):
        if j == 2:
            return "darkgrey"           # the road
        if j in (1, 4):
            return "paint2"
        return "paint1"
    m.add(loft([ring(*s) for s in segs], mat))
    # the glowing edge strips on the road's sides, the pillars
    for side in (1, -1):
        strip = []
        for (cx, cz, nx, nz) in segs:
            w = W / 2 - 0.4
            strip.append((cx + nx * w * side, cz + nz * w * side))
        for (a, b) in zip(strip, strip[1:]):
            q = Part()
            q.v = np.array([(a[0], H + 0.05, a[1]), (b[0], H + 0.05, b[1]), (b[0] - (b[0] - a[0]) * 0 + 0.0, H + 0.05, b[1]),
                            (a[0], H + 0.05, a[1])])
            off = np.array([segs[0][2], segs[0][3]]) * 0.0
            # a flat strip 0.35 wide on the road
            pa = np.array(a)
            pb = np.array(b)
            d = pb - pa
            n2 = np.array([d[1], -d[0]]) / np.linalg.norm(d) * 0.35
            q.v = np.array([(pa[0], H + 0.05, pa[1]), (pb[0], H + 0.05, pb[1]), (pb[0] + n2[0], H + 0.05, pb[1] + n2[1]),
                            (pa[0] + n2[0], H + 0.05, pa[1] + n2[1])])
            q.f = [((0, 1, 2, 3), "cyan_glow" if side > 0 else "magenta_glow")]
            face_out(q, (0, 1, 0))
            m.add(q)
    for t in ((0.2, 0.8) if not angle else (0.25, 0.75)):
        k = int(round(t * n)) if angle else 0
        if angle:
            cx, cz = segs[k][0], segs[k][1]
        else:
            cx, cz = 0.0, L * (t - 0.5)
        m.add(prism(chamfer_sq(1.4, 1.4, 0.4), 0, H - deck, "paint1", None, scale_top=1.0).tr(cx, 0, cz))
        m.add(box(cx - 4.5, cx + 4.5, H - deck - 1.0, H - deck, cz - 1.2, cz + 1.2, "paint2", 0.0))
    return m


def traffic_ship():
    m = Model("traffic_ship", "a small flying traffic ship (4 m), glowing lights: it crosses the sky")
    from ships import body
    rows = [(-1.8, 0.95, 0.40, 0.75, 0.80, 0.85, 0.50, 0.70, 0.20),
            (-0.6, 1.05, 0.45, 0.85, 0.90, 0.95, 0.50, 0.80, 0.15),
            (0.8, 0.95, 0.42, 0.80, 0.80, 0.90, 0.45, 0.75, 0.18),
            (1.9, 0.62, 0.30, 0.60, 0.55, 0.70, 0.38, 0.55, 0.26)]

    def mat(panel, c):
        if panel == "bottom":
            return "darkgrey"
        if panel == "lip":
            return "darkgrey"
        if panel == "side":
            return "paint2"
        return "paint1"
    m.add(body(rows, mat, mouth=(0.75, 0.1, "darkgrey")))
    from ships import canopy
    m.add(canopy(-0.7, 1.2, 0.2, 1.00, 1.38, 0.46))
    # lights: a cyan strip under each side, magenta tail lights, a glowing belly disc
    for sx in (1, -1):
        m.add(box(sx * 0.9 - 0.05, sx * 0.9 + 0.05, 0.38, 0.48, -1.6, 1.6, "cyan_glow"))
        m.add(box(sx * 0.55 - 0.18, sx * 0.55 + 0.18, 0.40, 0.70, -1.84, -1.80, "magenta_glow"))
    from czmodel import disc
    m.add(disc(0.55, 8, "plasma_glow", y=0.12, down=True))
    return m


ALL = {
    "tower_setback": tower_setback,
    "tower_chamfer": tower_chamfer,
    "tower_round": tower_round,
    "tower_stepped": tower_stepped,
    "tower_twin": tower_twin,
    "tower_slim": tower_slim,
    "billboard_pole": lambda: billboard("billboard_pole", "magenta_glow"),
    "billboard_wall": billboard_wall,
    "billboard_tall": billboard_tall,
    "antenna_mast": antenna_mast,
    "street_lamp": street_lamp,
    "neon_arch": neon_arch,
    "holo_ring": holo_ring,
    "start_gate": start_gate,
    "highway_straight": lambda: highway_piece("highway_straight", 0, 0, "an elevated highway piece, straight (40 m), on pillars, glowing edges"),
    "highway_curve": lambda: highway_piece("highway_curve", 30, 60.0, "an elevated highway piece, a 30-degree curve (radius 60 m), glowing edges"),
    "traffic_ship": traffic_ship,
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
