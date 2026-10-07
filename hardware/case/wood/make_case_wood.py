#!/usr/bin/env python3
# Onyx -- the Raspberry Pi 4 case in solid maple, for a 3-axis CNC router.
#
# Copyright (c) 2026 the Onyx authors. MIT licence (see docs/LICENSING.md).
#
# python3 hardware/case/wood/make_case_wood.py
#   -> wood_cover.stl, wood_tray.stl, wood_disc.stl (each in its machining position),
#      wood_assembled.stl (to look at), preview.png
# Needs: pip install manifold3d trimesh numpy pillow (it borrows the 3D-printed case's helpers).
#
# The same case as ../make_case.py, redrawn for wood:
# - every part is machined from ONE face, with a 1/8" (3.175 mm) flat end mill whose cutting
#   length is 20 mm: no inside corner under 2 mm of radius, every pocket reachable by the
#   bit, nothing undercut, no part taller than 20 mm (the cover 19.5, the tray 14);
# - the COVER is a 20 mm board of maple hollowed from its open side (its top face lies on
#   the spoilboard and is left as it is): the ports are notches open at the rim, the vents
#   through-cuts, the corners keep the screws' pilot holes;
# - the BASE is a TRAY machined from its open side: the floor carries the board, its rim
#   meets the cover's 14 mm up; the screws go through it from below (no counterbore can be
#   cut from that side: their heads hide behind 4 mm rubber feet);
# - the LOGO is a separate DISC, machined from its visible face (the gem's facets pocketed at
#   four depths, the peach facet for a coloured epoxy), glued from inside into the top,
#   held by its shoulder -- a GameCube's lid;
# - maple splits along its grain where a piece is thin: no ribs between ports (one bay per
#   side, under a long-grain lintel), walls 7 mm, the top 4 mm. Take the grain along x
#   (the side with USB-C and the HDMI ports).

import math, os, sys
import numpy as np
import manifold3d as m3d
from manifold3d import Manifold, CrossSection, FillRule

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from make_case import (rrect, box, cyl, to_trimesh, HOLES, IN_X0, IN_X1, IN_Y0,
                       GEM_HEX, GEM_R)
m3d.set_circular_segments(96)

# ---------------------------------------------------------------- parameters (mm)
BIT_R     = 3.175 / 2
R_IN      = 2.0           # inside corners (> the bit's radius)
WALL      = 7.0
TOP       = 4.0
PLATE     = 6.0           # the tray's floor
Z_SPLIT   = 14.0          # the tray's rim, where the cover sits (cover: 33.5 - 14 = 19.5 mm)
STANDOFF  = 5.0           # M2.5 brass standoffs, female-female, 5 mm
TALLEST   = 16.0          # the USB stack above the PCB
HEADROOM  = 1.1
R_CORNER  = 5.0           # the plan's outer corners (the corner screws need the wood)

Z_PCB_BOT = PLATE + STANDOFF            # 11.0
Z_PCB_TOP = Z_PCB_BOT + 1.4             # 12.4
Z_CEIL    = Z_PCB_TOP + TALLEST + HEADROOM   # 29.5
Z_TOP     = Z_CEIL + TOP                # 33.5

X0, X1 = IN_X0 - WALL, IN_X1 + WALL
Y0 = IN_Y0 - WALL
Y1 = Y0 + (X1 - X0)                     # square
IN_Y1 = Y1 - WALL
CX, CY = (X0 + X1) / 2, (Y0 + Y1) / 2

# the cover's screws: one in each corner of the walls, from below through the tray
SCREWS = [(IN_X0 - WALL / 2, IN_Y0 - WALL / 2), (IN_X1 + WALL / 2, IN_Y0 - WALL / 2),
          (IN_X0 - WALL / 2, IN_Y1 + WALL / 2), (IN_X1 + WALL / 2, IN_Y1 + WALL / 2)]
PILOT     = 2.2           # for 3.0 mm wood screws (Spax-like) in maple; drill it
SCREW_CLR = 3.4           # 3.0 x 25 wood screws: 14 mm of tray, 11 into the cover

# the disc
DISC_R, FLANGE_R, FLANGE_T = 25.0, 28.0, 2.0
FIT       = 0.15          # the hole's clearance around the disc
GEM_SIZE  = 20.0          # the gem's radius on the disc
RIDGE     = 1.6           # the wood left between two facets
# the facets (the site's SVG, its 32x32 box, y down) and their depths: the darker, the deeper
FACETS = [([(16, 1.5), (28.5, 9), (16, 13)], 0.6),                       # #6f87a8
          ([(16, 1.5), (3.5, 9), (16, 13)], 1.2),                        # #4a6184
          ([(16, 19), (28.5, 14.5), (28.5, 23), (16, 30.5)], 1.8),       # #324460
          ([(3.5, 9), (16, 13), (16, 30.5), (3.5, 23)], 2.4)]            # #1a2536
ACCENT = ([(16, 13), (28.5, 9), (28.5, 14.5), (16, 19)], 1.2)            # #eea06a: the epoxy

# the vents: a ring of arcs around the disc
VENT_R0, VENT_R1, VENT_N, VENT_GAP = 31.5, 36.5, 8, 15.0

def reachable(cs, r=BIT_R + 0.1):
    """What a bit of radius r can cut out of `cs` (its morphological opening)."""
    return cs.offset(-r, m3d.JoinType.Round).offset(r, m3d.JoinType.Round)

def ring_arcs(cx, cy, r0, r1, n, gap_deg):
    out = []
    for k in range(n):
        a0 = math.radians(k * 360 / n + gap_deg / 2)
        a1 = math.radians((k + 1) * 360 / n - gap_deg / 2)
        pts = [(cx + r1 * math.cos(a0 + (a1 - a0) * i / 24), cy + r1 * math.sin(a0 + (a1 - a0) * i / 24)) for i in range(25)]
        pts += [(cx + r0 * math.cos(a1 - (a1 - a0) * i / 24), cy + r0 * math.sin(a1 - (a1 - a0) * i / 24)) for i in range(25)]
        out.append(reachable(CrossSection([pts])))
    return CrossSection.batch_boolean(out, m3d.OpType.Add)

# ---------------------------------------------------------------- the parts
def cover():
    body = rrect(X0, Y0, X1, Y1, R_CORNER).extrude(Z_TOP - Z_SPLIT).translate((0, 0, Z_SPLIT))
    cavity = rrect(IN_X0, IN_Y0, IN_X1, IN_Y1, R_IN).extrude(Z_CEIL - Z_SPLIT + 1).translate((0, 0, Z_SPLIT - 1))
    c = body - cavity
    t, lo = Z_PCB_TOP, Z_SPLIT - 1
    cuts = [
        # the y-min side: USB-C, the two micro-HDMI, audio -- one bay (no ribs to split)
        box(3.5 + 7.7 - 6.25, Y0 - 1, lo, 53.5 + 4.2, IN_Y0 + 1, t + 7.5),
        # the x-max side: the two USB stacks and Ethernet -- one bay
        box(IN_X1 - 1, 9.0 - 7.1, lo, X1 + 1, 45.75 + 8.3, t + 16.4),
    ]
    # the top: the disc's hole and, inside, its shoulder; the vents
    cuts.append(cyl(CX, CY, Z_CEIL - 1, Z_TOP + 1, 2 * (DISC_R + FIT), 192))
    cuts.append(cyl(CX, CY, Z_CEIL - 1, Z_CEIL + FLANGE_T + FIT, 2 * (FLANGE_R + FIT), 192))
    cuts.append(ring_arcs(CX, CY, VENT_R0, VENT_R1, VENT_N, VENT_GAP).extrude(TOP + 2).translate((0, 0, Z_CEIL - 1)))
    # the screws' pilot holes, from the rim
    for (sx, sy) in SCREWS:
        cuts.append(cyl(sx, sy, lo, Z_SPLIT + 12, PILOT, 24))
    return c - Manifold.batch_boolean(cuts, m3d.OpType.Add)

def tray():
    p = rrect(X0, Y0, X1, Y1, R_CORNER).extrude(Z_SPLIT)
    p = p - rrect(IN_X0, IN_Y0, IN_X1, IN_Y1, R_IN).extrude(Z_SPLIT).translate((0, 0, PLATE))
    t, hi = Z_PCB_TOP, Z_SPLIT + 1
    cuts = [
        # the ports' bays continue down into the tray's rim, as far as the plugs need
        box(3.5 + 7.7 - 6.25, Y0 - 1, t - 2.4, 53.5 + 4.2, IN_Y0 + 1, hi),
        box(IN_X1 - 1, 9.0 - 7.1, t - 0.8, X1 + 1, 45.75 + 8.3, hi),
        # the x-min side: the micro-SD card under the board, the PWR/ACT LEDs above it
        box(X0 - 1, 6.0, PLATE + 1.0, IN_X0 + 1, 36.0, hi),
    ]
    for (hx, hy) in HOLES:                       # the board: M2.5 x 10 from below into the standoffs
        cuts.append(cyl(hx, hy, -1, PLATE + 1, 2.9, 24))
    for (sx, sy) in SCREWS:                      # the cover: 3.0 x 25 wood screws, up the corners
        cuts.append(cyl(sx, sy, -1, Z_SPLIT + 1, SCREW_CLR, 24))
    gcx, gcy = 32.0, 30.0                        # a grille under the SoC: 4 mm holes
    for i in range(-5, 6):
        for j in range(-5, 6):
            px, py = gcx + i * 6.0 + (3.0 if j % 2 else 0), gcy + j * 5.2
            if math.hypot(px - gcx, py - gcy) <= 16.0:
                cuts.append(cyl(px, py, -1, PLATE + 1, 4.0, 32))
    return p - Manifold.batch_boolean(cuts, m3d.OpType.Add)

def gem_pockets():
    """The gem's facets as pockets: [(CrossSection, depth)], centred on (0, 0), y up."""
    k = GEM_SIZE / GEM_R
    out = []
    for pts, depth in FACETS + [ACCENT]:
        cs = CrossSection([[((x - 16) * k, (16 - y) * k) for x, y in pts]], FillRule.EvenOdd)
        out.append((reachable(cs.offset(-RIDGE / 2, m3d.JoinType.Miter)), depth))
    return out

def disc():
    """The logo's disc, its visible face up (z = FLANGE_T + TOP_VISIBLE)."""
    boss_h = TOP - FLANGE_T
    d = (Manifold.cylinder(FLANGE_T, FLANGE_R, FLANGE_R, 192) +
         Manifold.cylinder(boss_h, DISC_R, DISC_R, 192).translate((0, 0, FLANGE_T)))
    face = FLANGE_T + boss_h
    cuts = [cs.extrude(depth + 1).translate((0, 0, face - depth)) for cs, depth in gem_pockets()]
    return d - Manifold.batch_boolean(cuts, m3d.OpType.Add)

def accent_fill():
    """The peach facet's pocket alone (what the epoxy fills), on the disc as it lies."""
    cs, depth = gem_pockets()[-1]
    return cs.extrude(depth).translate((0, 0, TOP - depth))

def main():
    c, p, d = cover(), tray(), disc()
    for name, m in (("cover", c), ("tray", p), ("disc", d)):
        if m.status() != m3d.Error.NoError or m.is_empty():
            sys.exit(f"{name}: bad solid ({m.status()})")
    # in place: the disc in the top, its gem reading from the front (y-max, the plain face)
    place = lambda m: m.rotate((0, 0, 180)).translate((CX, CY, Z_CEIL))
    d_in, a_in = place(d), place(accent_fill())
    j = lambda n: os.path.join(HERE, n)
    to_trimesh(c.rotate((180, 0, 0)).translate((0, 0, Z_TOP))).export(j("wood_cover.stl"))
    to_trimesh(p).export(j("wood_tray.stl"))
    to_trimesh(d).export(j("wood_disc.stl"))
    to_trimesh(c + p + d_in).export(j("wood_assembled.stl"))
    to_trimesh(c).export(j("_cover_in_place.stl")); to_trimesh(p).export(j("_tray_in_place.stl"))
    to_trimesh(d_in).export(j("_disc_in_place.stl")); to_trimesh(a_in).export(j("_accent_in_place.stl"))
    for name, m in (("cover", c), ("tray", p), ("disc", d)):
        bb = m.bounding_box()
        print(f"{name:6s} {m.num_tri():7d} tris  {bb[3]-bb[0]:.1f} x {bb[4]-bb[1]:.1f} x {bb[5]-bb[2]:.1f} mm  genus {m.genus()}")
    # checks: the disc fits its hole; nothing meets the board
    print("disc vs cover:", round((c ^ d_in).volume(), 3), "mm3")
    t = Z_PCB_TOP
    pcb = rrect(0, 0, 85, 56, 3).extrude(1.4).translate((0, 0, Z_PCB_BOT))
    parts = {'pcb': pcb, 'usbc': box(6.7, -1.3, t, 15.7, 6, t + 3.2), 'hdmi': box(22.25, -1, t, 43.25, 6.5, t + 3),
             'audio': box(50, 2, t, 57, 12, t + 6), 'usb': box(70, 2.4, t, 87.1, 33.6, t + 16),
             'eth': box(66, 37.75, t, 87.1, 53.75, t + 13.5), 'gpio': box(7.1, 50, t, 57.9, 55, t + 8.6),
             'sd': box(-2.5, 22, Z_PCB_BOT - 1.4, 15, 34, Z_PCB_BOT), 'heatsink': box(22, 24, t, 37, 39, t + 11)}
    for n, part in parts.items():
        v = (part ^ (c + p + d_in)).volume()
        if v > 0.01: print("COLLISION", n, round(v, 2))
    plugs = {'usbc': box(7.0, -20, t + 0.1, 15.4, -1.3, t + 3.1), 'hdmi0': box(22.5, -20, t + 0.1, 29.5, -1, t + 2.9),
             'hdmi1': box(36, -20, t + 0.1, 43, -1, t + 2.9), 'audio': box(50.5, -20, t, 56.5, -2, t + 6),
             'usb_a': box(87.1, 2.6, t + 0.2, 110, 15.4, t + 15.8), 'usb_b': box(87.1, 20.6, t + 0.2, 110, 33.4, t + 15.8),
             'eth': box(87.1, 37.95, t + 0.2, 110, 53.55, t + 13.3), 'sd': box(-20, 22.5, Z_PCB_BOT - 1.3, -2.5, 33.5, Z_PCB_BOT - 0.4)}
    blocked = [n for n, b in plugs.items() if ((c + p) ^ b).volume() > 0.01]
    print("plugs blocked:", blocked or "none")

if __name__ == "__main__":
    main()
