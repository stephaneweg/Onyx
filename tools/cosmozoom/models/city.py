#!/usr/bin/env python3
#
# city.py -- Cosmo Zoom!'s city props. The final look (the user's approved image, 2026-10-15): rounded capsule
# towers -- walls in paint1 (the game recolours them: coral, lemon, mint, lavender, white, sky), bands and rounded
# caps in paint2, plain navy / white window slots --, rounded signs on poles (the panel with UVs 0..1 for the game's
# picture), a rounded street lamp, the rounded start gate, a small flying car. (The neon pieces of the night look
# before it -- antenna_mast, neon_arch, holo_ring, the highways -- are kept: they can serve.)
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
from czmodel import frame_along  # noqa: E402
from soft import Station, soft_loft, soft_box, soft_cyl, rrect  # noqa: E402

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


# ---- the towers (the final look: rounded capsule towers, walls paint1, bands and caps paint2) -----------------------
def soft_tower(w, d, r, h, cap=0.0, x=0.0, z=0.0, y0=0.0, bands_at=(), band_h=1.2, mat_band="paint2", k=3,
               cap_mat="paint2", cap_from=None):
    """A rounded tower: a rounded-rectangle section (half sizes w x d, corners r) from y0 to y0 + h, its top
    rounded over `cap`; the bands (paint2) at the heights in bands_at; the rounded top from cap_from up in cap_mat."""
    edges = sorted(set([y0] + [b for y in bands_at for b in (y, y + band_h)] + [y0 + h]))
    sts = [Station(yy, w, d, r, 0.0, 0.0) for yy in edges]

    def mat(lab, i, c):
        y = c[2]
        if cap_from is not None and y > cap_from:
            return cap_mat
        for b in bands_at:
            if b < y < b + band_h:
                return mat_band
        return "paint1"
    p = soft_loft(sts, mat, k, round0=0.0, round1=cap, steps=3 if cap > 2.0 else 2, plane="xz")
    return p.tr(x, 0, z)


def slots(x0, x1, ys, z, h=0.5, mat="navy", depth=0.25):
    """Dark window slots: thin rounded bars on a front face (z = its front) at the heights ys."""
    p = Part()
    for y in ys:
        p.add(box(x0, x1, y, y + h, z - depth, z + 0.08, mat))
    return p


def tower_chamfer():
    m = Model("tower_chamfer", "a tall pill tower: a rounded slab, a paint2 band, a rounded paint2 top, slots")
    H = 52.0
    m.add(soft_tower(7.0, 4.6, 4.5, H, cap=4.5, bands_at=(H * 0.42,), band_h=1.6, cap_from=H - 2.0))
    m.add(slots(-3.4, 3.4, (H - 9.0, H - 7.4, H - 5.8), 4.6, 0.6, "white", 0.2))
    m.add(slots(-1.6, 1.6, (9.0, 15.0, 30.0, 36.0), 4.6, 1.8, "navy", 0.2))
    return m


def tower_setback():
    m = Model("tower_setback", "a rounded box tower, a paint2 band, a rounded paint2 cap block, dark window slots")
    H = 40.0
    m.add(soft_tower(7.5, 7.5, 2.0, H, cap=1.4, bands_at=(H - 8.0,), band_h=1.6))
    m.add(soft_tower(6.4, 6.4, 1.8, 3.4, cap=1.2, y0=H - 0.2, cap_from=H - 1.0))
    m.add(slots(-4.8, 4.8, (7.0, 14.0, 21.0), 7.5, 1.0, "navy", 0.2))
    return m


def tower_stepped():
    m = Model("tower_stepped", "a stepped rounded tower: three blocks, paint2 ledges between them")
    blocks = [(9.0, 8.0, 2.4, 0.0, 18.0), (7.0, 6.2, 2.0, 18.0, 15.0), (5.0, 4.6, 1.8, 33.0, 12.0)]
    for i, (w, d, r, y0, h) in enumerate(blocks):
        top = i == len(blocks) - 1
        m.add(soft_tower(w, d, r, h, cap=1.8 if top else 0.0, y0=y0, cap_from=(y0 + h - 1.6) if top else None))
        if not top:
            m.add(soft_tower(w + 0.5, d + 0.5, r + 0.3, 1.2, cap=0.0, y0=y0 + h - 0.3, cap_from=y0 - 1, k=2))
    m.add(slots(-5.0, 5.0, (5.0, 11.0), 8.0, 1.2, "navy", 0.2))
    m.add(slots(-3.6, 3.6, (23.0, 28.0), 6.2, 1.0, "navy", 0.2))
    return m


def tower_twin():
    m = Model("tower_twin", "twin pill towers of two heights, joined by a rounded bridge")
    for sx, H in ((1, 46.0), (-1, 36.0)):
        m.add(soft_tower(4.4, 4.4, 4.35, H, cap=4.35, x=5.2 * sx, bands_at=(H * 0.5,), band_h=1.4, cap_from=H - 2.6))
    m.add(soft_box(-2.0, 2.0, 22.0, 25.5, -2.4, 2.4, 1.0, "paint2"))
    return m


def tower_slim():
    m = Model("tower_slim", "a slim capsule tower: a round shaft, a paint2 band, a round top")
    H = 56.0
    m.add(soft_tower(4.0, 4.0, 3.95, H, cap=3.95, bands_at=(H - 12.0,), band_h=1.4, cap_from=H - 2.6))
    return m


def tower_round():
    m = Model("tower_round", "a dome-topped round tower: a white band, a paint2 dome, slots")
    H = 30.0
    m.add(soft_tower(7.0, 7.0, 6.95, H, cap=6.95, bands_at=(H - 3.0,), band_h=1.2, mat_band="white", cap_from=H - 0.4))
    m.add(slots(-3.4, 3.4, (7.0, 13.0, 19.0), 6.8, 1.2, "navy", 0.4))
    return m


# ---- signs, lamps ------------------------------------------------------------------------------------------------
def sign(name, w, h, pole, legs=1, title="", wall=False):
    m = Model(name, title)
    y = (0.0 if wall else pole) + h / 2
    zc = 0.35 if wall else 0.0
    m.add(soft_box(-w / 2, w / 2, y - h / 2, y + h / 2, zc - 0.35, zc + 0.35, 0.30, "paint2"))
    pic = panel(w - 0.9, h - 0.9, "white", two_sided=not wall, z=0.0).tr(0, y, 0)
    if wall:
        pic.tr(0, 0, zc + 0.36)
    else:
        pic.v[:, 2] = np.sign(pic.v[:, 2] if False else 1) * pic.v[:, 2]
        # the front face at +0.36, the back face at -0.36
        front = panel(w - 0.9, h - 0.9, "white", two_sided=False).tr(0, y, 0.36)
        back = panel(w - 0.9, h - 0.9, "white", two_sided=False)
        back.v[:, 0] *= -1
        back.v[:, 2] = -0.36
        back.flip()
        back.tr(0, y, 0)
        pic = front.add(back)
    m.add(pic, "panel")
    if not wall:
        xs = [0.0] if legs == 1 else [-w * 0.3, w * 0.3]
        for x in xs:
            m.add(soft_cyl(0.30, 0.0, y - h / 2 + 0.2, 0.08, 10, "grey", x, 0.0, steps=1).rot("x", -90)
                  .tr(0, 0, 0) if False else vertical_cyl(0.30, 0.0, y - h / 2 + 0.2, x, 0.0, "grey"))
            m.add(soft_box(x - 0.9, x + 0.9, 0.0, 0.4, -0.9, 0.9, 0.15, "paint1", k=1))
    return m


def vertical_cyl(r, y0, y1, x=0.0, z=0.0, mat="grey", segs=10):
    b = min(0.08, r * 0.3)
    return revolve([(0.0, y0), (r, y0), (r, y1 - b), (r - b, y1), (0.0, y1)], segs, mat).tr(x, 0, z)


def billboard_pole():
    return sign("billboard_pole", 10.0, 5.0, 8.0, 1, "a rounded sign on a pole (its panel: UVs 0..1, o panel)")


def billboard_wall():
    return sign("billboard_wall", 8.0, 4.0, 0.0, 0, "a rounded sign for a wall: its back at z = 0, facing +Z (o panel: UVs 0..1)", wall=True)


def billboard_tall():
    return sign("billboard_tall", 3.6, 9.0, 3.0, 2, "a tall rounded sign on two legs (o panel: UVs 0..1)")


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
    m = Model("street_lamp", "a rounded street lamp: a grey pole bent over, a paint2 head, a white light")
    m.add(soft_box(-0.5, 0.5, 0.0, 0.35, -0.5, 0.5, 0.15, "paint1", k=1))
    m.add(tube([(0, 0.3, 0), (0, 6.6, 0), (0.0, 7.3, 0.35), (0, 7.5, 1.4)], 0.13, 8, "grey", cap0=False))
    m.add(soft_box(-0.40, 0.40, 7.25, 7.80, 0.9, 2.6, 0.22, "paint2"))
    m.add(soft_box(-0.26, 0.26, 7.18, 7.26, 1.05, 2.45, 0.03, "white_flat", k=1))
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


def soft_path(path, w, h, r, mat="paint1", k=3):
    """A rounded-rectangle section (half sizes w across, h along the path's normal plane) swept along a path."""
    P, T, N, B = frame_along(path)
    pts, lab = rrect(w, h, r, k)
    rings = []
    for i in range(len(P)):
        rings.append([tuple(P[i] + N[i] * u + B[i] * v) for u, v in pts])
    return loft(rings, mat)


def start_gate():
    m = Model("start_gate", "the start gate: a rounded pastel arch over the track, a checker banner, three lights")
    W, H, R = 9.6, 9.0, 2.6
    path = [(-W, 0.0, 0.0), (-W, H - R, 0.0)]
    for i in range(1, 5):
        a = PI - PI / 2 * i / 4
        path.append((-W + R + R * math.cos(a), H - R + R * math.sin(a), 0.0))
    for i in range(0, 5):
        a = PI / 2 - PI / 2 * i / 4
        path.append((W - R + R * math.cos(a), H - R + R * math.sin(a), 0.0))
    path.append((W, 0.0, 0.0))
    m.add(soft_path(path, 0.75, 1.0, 0.45, "paint1", k=2))
    for sx in (1, -1):
        m.add(soft_box(sx * W - 1.3, sx * W + 1.3, 0.0, 1.2, -1.4, 1.4, 0.35, "paint2", k=1))
        m.add(soft_box(sx * W - 0.95, sx * W + 0.95, 5.6, 6.2, -1.15, 1.15, 0.25, "paint2", k=1))
    m.add(checker_banner(11.0, 1.6, 12, 2, 6.2, 0.0, 0.2))
    m.add(soft_box(-3.6, 3.6, H - 0.6, H + 1.6, -0.75, 0.75, 0.5, "paint2"))
    for k, x in enumerate((-2.2, 0.0, 2.2)):
        mat = "red" if k < 2 else "mint"
        light = Part()
        for zf, sgn in ((0.75, 1), (-0.75, -1)):
            l = revolve([(0.0, 0.0), (0.62, 0.0), (0.56, 0.16), (0.0, 0.22)], 10, mat).rot("x", 90 * sgn)
            light.add(l.tr(x, H + 0.5, zf))
        m.add(light, "light%d" % (k + 1))
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
    m = Model("traffic_ship", "a small rounded flying car (3.6 m): paint1 body, a tinted cabin, white lights")
    sts = [Station(-1.6, 0.85, 0.28, 0.24, 0, 0.45), Station(0.9, 0.85, 0.26, 0.22, 0, 0.43), Station(1.5, 0.75, 0.22, 0.2, 0, 0.40)]
    m.add(soft_loft(sts, lambda lab, i, c: "darkgrey" if lab == "bottom" else "paint1", 2, round0=0.2, round1=0.2, steps=2))
    cab = [Station(-1.0, 0.6, 0.25, 0.2, 0, 0.88), Station(0.3, 0.6, 0.22, 0.2, 0, 0.86)]
    m.add(soft_loft(cab, lambda lab, i, c: "tint" if (c[2] > 0.2 or lab.startswith("side")) else "paint2", 2, round0=0.15, round1=0.25, steps=2))
    for sx in (1, -1):
        m.add(soft_box(sx * 0.55 - 0.18, sx * 0.55 + 0.18, 0.40, 0.55, 1.62, 1.72, 0.04, "white_flat", k=1))
        m.add(soft_box(sx * 0.55 - 0.18, sx * 0.55 + 0.18, 0.40, 0.55, -1.86, -1.76, 0.04, "red", k=1))
    return m


ALL = {
    "tower_setback": tower_setback,
    "tower_chamfer": tower_chamfer,
    "tower_round": tower_round,
    "tower_stepped": tower_stepped,
    "tower_twin": tower_twin,
    "tower_slim": tower_slim,
    "billboard_pole": billboard_pole,
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
