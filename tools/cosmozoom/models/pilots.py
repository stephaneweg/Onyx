#!/usr/bin/env python3
#
# pilots.py -- Cosmo Zoom!'s eight pilots (the user, 2026-10-15: human racing pilots, no mascots, no faces): a
# helmeted head and shoulders in the cockpit -- a racing helmet with a tinted visor, each in its own colours and
# with its own design (stripes, side fins, a crest, antennae, a mohawk ridge, ear cups, rear blades...).
# The file names are the first cast's (pilot_pepper ... pilot_kiki): the programmer maps them to the new names.
#
# Conventions: the origin is the neck (the hull's `pilot` socket), the pilot faces +Z; `o head` (the helmet and
# what is on it: it turns), `o body` (the shoulders, the neck). Height about 0.40 m above the neck.
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
#
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from czmodel import PI, Model, Part, loft, revolve, surface_shell, tube  # noqa: E402

SCRIPT = "pilots.py"

HC = 0.20            # the helmet's centre above the neck
HR = (0.165, 0.175, 0.185)   # its radii (x, y, z)


def ell(u, v, grow=0.0, cy=HC):
    """A point on the helmet's ellipsoid: u around Y from the front (+Z), v the latitude."""
    rx, ry, rz = HR[0] + grow, HR[1] + grow, HR[2] + grow
    return (rx * math.cos(v) * math.sin(u), cy + ry * math.sin(v), rz * math.cos(v) * math.cos(u))


def torso(suit, trim):
    """The shoulders: an angular bust (octagonal sections) and the collar."""
    def ring(y, hx, hz, cz=0.0):
        pts = []
        for k in range(8):
            a = 2 * PI * (k + 0.5) / 8
            pts.append((hx * math.cos(a) * 1.08, y, cz + hz * math.sin(a)))
        return pts
    rings = [ring(-0.30, 0.25, 0.13, -0.02), ring(-0.10, 0.27, 0.14, -0.02), ring(-0.02, 0.22, 0.12, -0.02),
             ring(0.035, 0.10, 0.08, -0.01)]

    def mat(i, j, c):
        if i == 2:
            return trim
        return suit
    t = loft(rings, mat)
    neck = revolve([(0.0, 0.0), (0.075, 0.0), (0.07, 0.09), (0.0, 0.09)], 8, "darkgrey")
    return t, neck


def helmet(main, stripe=None, stripe_w=1, chin=None):
    """The helmet shell: 12 meridians (a stripe over the top: the faces either side of the x = 0 plane)."""
    segs, rings_n = 12, 6
    prof = []
    for i in range(rings_n + 1):
        v = -PI / 2 + PI * i / rings_n
        prof.append((math.cos(v) if 0 < i < rings_n else 0.0, math.sin(v)))

    def mat(i, j, c):
        if stripe:
            # the faces whose middle meridian is close to the x = 0 plane, on the upper half
            ang = (2 * PI * (j + 0.5) / segs + PI / 12) % (2 * PI)
            if abs(math.cos(ang)) < (0.30 if stripe_w == 1 else 0.6) and c[1] > HC - 0.02:
                return stripe
        if chin and c[1] < HC - 0.09 and c[2] > 0.0:
            return chin
        return main
    h = revolve(prof, segs, mat, a0=PI / 12)
    h.sc(HR[0], HR[1], HR[2]).tr(0, HC, 0)
    return h


def visor(mat="navy", top=0.38, bottom=-0.42, half=1.05, shape=None):
    """The tinted visor: a band of shell on the helmet's front."""
    def fn(u, v):
        vv = v
        if shape == "narrow":
            vv = v * (1 - 0.25 * (u / half) ** 2)
        return ell(u, vv, 0.008)
    vis = surface_shell(fn, -half, half, bottom, top, 6, 2, 0.02, mat)
    # a white highlight streak near its top left (+X)
    strip = Part()
    us = np.linspace(0.45, 0.85, 4)
    for k, u in enumerate(us):
        vv = top * 0.75 - (u - 0.45) * 0.55
        a = ell(u, vv, 0.013)
        b = ell(u, vv - 0.06, 0.013)
        strip.v = np.vstack([strip.v, [a, b]]) if len(strip.v) else np.array([a, b])
    for k in range(len(us) - 1):
        i = 2 * k
        strip.f.append(((i, i + 1, i + 3, i + 2), "white_flat"))
    # face it outward (toward +Z / +X)
    n = np.cross(strip.v[1] - strip.v[0], strip.v[2] - strip.v[0])
    if np.dot(n, strip.v[0] - np.array([0, HC, 0])) < 0:
        strip.flip()
    return vis, strip


def blade_v(points_zy, thick, x, mat, cant=0.0):
    """A thin vertical blade (a crest, a fin) from a (z, y) outline, centred on x, leaned by cant degrees."""
    from ships import fin  # noqa
    pts = points_zy
    n = len(pts)
    left = [(x - thick / 2, y, z) for z, y in pts]
    right = [(x + thick / 2, y, z) for z, y in pts]
    p = Part()
    p.v = np.array(left + right, float)
    p.f.append((tuple(range(n)), mat))
    p.f.append((tuple(range(2 * n - 1, n - 1, -1)), mat))
    for k in range(n):
        k1 = (k + 1) % n
        p.f.append(((k, n + k, n + k1, k1), mat))
    p.orient()
    if cant:
        cy = sum(y for _, y in pts) / n
        p.tr(-x, -cy, 0).rot("z", cant).tr(x, cy, 0)
    return p


def side_fins(mat, y=HC + 0.02):
    """Two small swept fins on the helmet's sides."""
    out = Part()
    for sx in (1, -1):
        pts = [(0.02, y - 0.02), (-0.06, y + 0.05), (-0.24, y + 0.09), (-0.12, y - 0.05)]
        b = blade_v(pts, 0.03, sx * (HR[0] - 0.03), mat, cant=-sx * 12)
        out.add(b)
    return out


def ear_cups(mat, cap_mat):
    out = Part()
    for sx in (1, -1):
        c = revolve([(0.0, 0.0), (0.075, 0.0), (0.075, 0.035), (0.05, 0.05), (0.0, 0.05)], 6, mat).rot("z", -90 * sx)
        c.tr(sx * (HR[0] - 0.02), HC - 0.01, -0.01)
        out.add(c)
    return out


def crest(mat, h=0.07, z0=0.10, z1=-0.22):
    """A mohawk ridge from the forehead over the top to the back."""
    pts = []
    for k in range(7):
        a = PI / 2 * 0.25 + (PI * 0.95 - PI / 2 * 0.25) * k / 6       # over the top
        r = 1.0
        y = HC + HR[1] * math.sin(a) * 0.98
        z = HR[2] * math.cos(a) * 0.98
        pts.append((z, y))
    top = []
    for k, (z, y) in enumerate(pts):
        a = PI / 2 * 0.25 + (PI * 0.95 - PI / 2 * 0.25) * k / 6
        bump = h * math.sin(PI * k / 6) ** 0.7 + 0.01
        top.append((z + math.cos(a) * bump, y + math.sin(a) * bump))
    outline = pts + top[::-1]
    return blade_v(outline, 0.035, 0.0, mat)


def spikes_ridge(mat, n=4, h=0.07):
    """A row of small swept blades over the top (a punk ridge)."""
    out = Part()
    for k in range(n):
        a = PI * (0.32 + 0.16 * k)
        y = HC + HR[1] * math.sin(a) * 0.95
        z = HR[2] * math.cos(a) * 0.95
        nx, ny = math.cos(a), math.sin(a)
        tx, ty = -ny, nx  # tangent (toward the back)
        L = h * (1.0 - 0.12 * k)
        pts = [(z + tx * 0.035, y - ty * 0.0), (z + nx * L - tx * 0.05, y + ny * L + ty * 0.0), (z - tx * 0.04, y)]
        pts = [(z - 0.035 * ny * -1, y - 0.035 * nx), (z + L * nx - 0.06 * ny, y + L * ny + 0.06 * nx * 0),
               (z + 0.035 * ny, y + 0.035 * nx * 0)]
        # simple: a triangle standing on the helmet, leaning back
        base_f = (z + 0.04 * math.sin(a), y - 0.04 * math.cos(a) * -1)
        p0 = (z + 0.04 * ny, y - 0.04 * nx)
        p1 = (z - 0.05 * ny + L * nx * 0.6 - 0.05, y + L * ny)
        p2 = (z - 0.05 * ny, y + 0.05 * nx)
        out.add(blade_v([p0, p1, p2], 0.03, 0.0, mat))
    return out


def antennae(mat, tip):
    out = Part()
    for sx in (1, -1):
        base = np.array(ell(sx * 0.9, 0.85, -0.01))
        top = base + np.array([sx * 0.07, 0.17, -0.06])
        out.add(tube([base, top], 0.014, 5, mat, cap0=False))
        ball = revolve([(0.0, -0.035), (0.03, -0.02), (0.035, 0.0), (0.03, 0.02), (0.0, 0.035)], 6, tip)
        out.add(ball.tr(*top))
    return out


def rear_blades(mat):
    """Two swept blades at the back of the helmet (like gills / wings)."""
    out = Part()
    for sx in (1, -1):
        pts = [(-0.10, HC + 0.08), (-0.30, HC + 0.16), (-0.25, HC + 0.05), (-0.13, HC - 0.03)]
        out.add(blade_v(pts, 0.03, sx * 0.08, mat, cant=-sx * 22))
    return out


def chin_box(mat):
    return revolve([(0.0, 0.0), (0.06, 0.0), (0.06, 0.04), (0.0, 0.04)], 6, mat).rot("x", 90).tr(0, HC - 0.12, 0.15)


PILOTS = {
    # name: suit, trim, helmet, stripe, extra
    "pilot_pepper": dict(suit="red", trim="cream", main="red", stripe="white", extra="fins", extra_mat="cream"),
    "pilot_bruno": dict(suit="orange", trim="yellow", main="orange", stripe="yellow", stripe_w=2, extra="cups", extra_mat="darkgrey"),
    "pilot_bolt": dict(suit="yellow", trim="sky", main="yellow", stripe=None, extra="crest", extra_mat="sky"),
    "pilot_zib": dict(suit="mint", trim="yellow", main="mint", stripe="white", extra="antennae", extra_mat="silver", tip="yellow"),
    "pilot_beep": dict(suit="teal", trim="cream", main="white", stripe="teal", stripe_w=2, extra="cups", extra_mat="teal"),
    "pilot_bloop": dict(suit="blue", trim="white", main="blue", stripe="white", extra="fins", extra_mat="sky"),
    "pilot_squiggle": dict(suit="grape", trim="pink", main="grape", stripe=None, extra="ridge", extra_mat="pink"),
    "pilot_kiki": dict(suit="pink", trim="lavender", main="pink", stripe="lavender", extra="rear", extra_mat="lavender"),
}


def make(name):
    d = PILOTS[name]
    m = Model(name, "a pilot (%s): a racing helmet with a tinted visor, the shoulders" % d["main"])
    t, neck = torso(d["suit"], d["trim"])
    m.add(t, "body")
    m.add(neck, "body")
    m.add(helmet(d["main"], d.get("stripe"), d.get("stripe_w", 1)), "head")
    vis, hl = visor(shape="narrow" if d["extra"] in ("crest", "ridge") else None)
    m.add(vis, "head")
    m.add(hl, "head")
    e = d["extra"]
    if e == "fins":
        m.add(side_fins(d["extra_mat"]), "head")
    elif e == "cups":
        m.add(ear_cups(d["extra_mat"], d["main"]), "head")
    elif e == "crest":
        m.add(crest(d["extra_mat"]), "head")
    elif e == "antennae":
        m.add(antennae(d["extra_mat"], d.get("tip", "yellow")), "head")
    elif e == "ridge":
        m.add(spikes_ridge(d["extra_mat"]), "head")
    elif e == "rear":
        m.add(rear_blades(d["extra_mat"]), "head")
    return m


ALL = {n: (lambda n=n: make(n)) for n in PILOTS}


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
