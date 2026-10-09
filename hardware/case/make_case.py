#!/usr/bin/env python3
# Onyx -- a two-part case for the Raspberry Pi 4 (Model B), with the Onyx logo.
#
# Copyright (c) 2026 the Onyx authors. MIT licence (see docs/LICENSING.md).
#
# A parametric generator: python3 hardware/case/make_case.py
#   -> onyx_case_base.stl, onyx_case_cover.stl, onyx_case_logo_inlay.stl,
#      onyx_case_logo_accent.stl (print orientation)
#      onyx_case_assembled.stl (the two parts in place, to look at), preview.png
# Needs: pip install manifold3d trimesh numpy (and pillow for render_preview.py)
#
# The look: a Mac mini's soft rounded slab, a GameCube's disc on the top (a circular
# groove, the Onyx gem engraved inside, a ring of vents around it), a
# shadow line where the two halves meet, a vented bottom.
#
# Coordinates: the board's frame -- x along the 85 mm side (x=0 at the micro-SD edge),
# y along the 56 mm side (y=0 at the USB-C / micro-HDMI / audio edge), z up, the case's
# bottom at z=0.  Mechanical data: the Raspberry Pi 4 Model B drawing.

import math, os, sys
import numpy as np
import manifold3d as m3d
from manifold3d import Manifold, CrossSection, FillRule

HERE = os.path.dirname(os.path.abspath(__file__))
m3d.set_circular_segments(64)

# ---------------------------------------------------------------- parameters (mm)
WALL      = 2.0          # side walls
FLOOR     = 2.0          # base floor
TOP       = 2.2          # cover top
STANDOFF  = 5.0          # floor -> PCB bottom (room for the micro-SD card)
PCB_T     = 1.4
TALLEST   = 16.0         # the USB stack above the PCB
HEADROOM  = 1.6          # above it
R_CORNER  = 6.0          # the plan's corner radius
R_EDGE    = 4.0          # the top and bottom edges' rounding (ends in a 45-degree chamfer)
REVEAL    = 0.6          # the shadow line's chamfer on each half
GPIO_SLOT = False        # True: a slot in the top over the GPIO header (for a ribbon)

Z_PCB_BOT = FLOOR + STANDOFF            # 7.0
Z_PCB_TOP = Z_PCB_BOT + PCB_T           # 8.4
Z_SPLIT   = Z_PCB_TOP                   # the halves meet at the PCB's top
Z_CEIL    = Z_PCB_TOP + TALLEST + HEADROOM   # 26.0 inner top
Z_TOP     = Z_CEIL + TOP                # 28.2

# the cavity (inner faces); the ports' side walls hug the board, and the case is square:
# the room left behind the board (the front, y-max) takes the cover's own screws
IN_X0, IN_X1 = -2.0, 86.0
IN_Y0 = -0.8
X0, X1 = IN_X0 - WALL, IN_X1 + WALL
Y0 = IN_Y0 - WALL
Y1 = Y0 + (X1 - X0)                     # square
IN_Y1 = Y1 - WALL
CX, CY = (X0 + X1) / 2, (Y0 + Y1) / 2

HOLES = [(3.5, 3.5), (61.5, 3.5), (3.5, 52.5), (61.5, 52.5)]   # M2.5 mounting holes
# the cover's screws (M3, from below), in the two corners away from the board
BOSS_D = 8.0
BOSSES = [(IN_X0 + BOSS_D / 2 + 0.3, IN_Y1 - BOSS_D / 2 - 0.3),
          (IN_X1 - BOSS_D / 2 - 0.3, IN_Y1 - BOSS_D / 2 - 0.3)]

# ---------------------------------------------------------------- helpers
def rrect(x0, y0, x1, y1, r):
    """A rounded rectangle (2-D)."""
    r = max(0.01, min(r, (x1 - x0) / 2 - 0.01, (y1 - y0) / 2 - 0.01))
    sq = CrossSection.square((x1 - x0 - 2 * r, y1 - y0 - 2 * r)).translate((x0 + r, y0 + r))
    return sq.offset(r, m3d.JoinType.Round)

def box(x0, y0, z0, x1, y1, z1):
    return Manifold.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))

def cyl(x, y, z0, z1, d, seg=0):
    return Manifold.cylinder(z1 - z0, d / 2, d / 2, seg).translate((x, y, z0))

def stroke(p, q, w):
    """A line with round caps, w wide (2-D)."""
    c = CrossSection.circle(w / 2, 24)
    return CrossSection.batch_hull([c.translate(p), c.translate(q)])

def edge_inset(t, r):
    """The inset of an edge rounded with radius r, t mm from the face: a quarter circle
    from the wall down to 45 degrees, then a 45-degree chamfer -- printable on the bed."""
    t45 = r * (1 - math.sqrt(0.5))           # where the arc reaches 45 degrees
    if t >= r: return 0.0
    if t >= t45:
        d = r - t
        return r - math.sqrt(r * r - d * d)
    return (r - r * math.sqrt(0.5)) + (t45 - t)      # chamfer part

def rounded_slab(z0, z1, round_bottom, round_top, r=R_EDGE, steps=10):
    """The outer body between z0 and z1, its bottom and/or top edge rounded."""
    zs = set([z0, z1])
    for k in range(steps + 1):
        t = r * k / steps
        if round_bottom: zs.add(z0 + t)
        if round_top:    zs.add(z1 - t)
    t45 = r * (1 - math.sqrt(0.5))
    if round_bottom: zs.add(z0 + t45)
    if round_top:    zs.add(z1 - t45)
    zs = sorted(z for z in zs if z0 <= z <= z1)
    def inset_at(z):
        i = 0.0
        if round_bottom: i = max(i, edge_inset(z - z0, r))
        if round_top:    i = max(i, edge_inset(z1 - z, r))
        return i
    slabs = []
    for z in zs:
        i = inset_at(z)
        cs = rrect(X0 + i, Y0 + i, X1 - i, Y1 - i, max(0.5, R_CORNER - i))
        slabs.append(cs.extrude(0.001).translate((0, 0, min(z, z1 - 0.001))))
    return Manifold.batch_hull(slabs)

def reveal_cut(z_at, z_far):
    """The shadow line: a 45-degree chamfer around the outer edge, REVEAL deep at z_at,
    nothing at z_far (the volume to cut away)."""
    lo, hi = sorted([z_at, z_far])
    r = rrect(X0, Y0, X1, Y1, R_CORNER).extrude(0.001)
    ri = rrect(X0 + REVEAL, Y0 + REVEAL, X1 - REVEAL, Y1 - REVEAL, R_CORNER - REVEAL).extrude(0.001)
    frustum = Manifold.batch_hull([ri.translate((0, 0, z_at - 0.0005)), r.translate((0, 0, z_far - 0.0005))])
    band = rrect(X0 - 1, Y0 - 1, X1 + 1, Y1 + 1, R_CORNER + 1).extrude(hi - lo).translate((0, 0, lo))
    return band - frustum

def slot_x(x, z, w, h, r=1.0):
    """An opening through the y-min wall (centred on x, z)."""
    cs = rrect(-w / 2, -h / 2, w / 2, h / 2, r)
    return cs.extrude(WALL + 4).rotate((90, 0, 0)).translate((x, IN_Y0 + 2, z))

def slot_y(y, z0, z1, w, r=1.0):
    """An opening through the x-max wall (centred on y, from z0 to z1)."""
    cs = rrect(-w / 2, z0, w / 2, z1, r)          # (y, z) plane
    return cs.extrude(WALL + 4).rotate((90, 0, 90)).translate((IN_X1 - 2, y, 0))

# ---------------------------------------------------------------- the logo
# The Onyx mark, as on the Onyx web site: the gem -- a hexagon cut in facets, one facet in peach.
GEM_HEX  = [(16, 1.5), (28.5, 9), (28.5, 23), (16, 30.5), (3.5, 23), (3.5, 9)]   # the SVG's 32x32 box, y down
GEM_EDGES = [((3.5, 9), (16, 13)), ((28.5, 9), (16, 13)), ((16, 1.5), (16, 13)), ((16, 13), (16, 30.5)),
             ((28.5, 14.5), (16, 19))]
GEM_ACCENT = [(16, 13), (28.5, 9), (28.5, 14.5), (16, 19)]
GEM_R = 14.5                                  # the hexagon's radius in the box (its centre: 16, 16)

def logo_2d(radius, stroke_w=0.9):
    """The gem, its hexagon `radius` mm, centred on (0, 0): (lines, accent) -- its outline and
    facets, and the peach facet, apart for a second colour."""
    k = radius / GEM_R
    P = lambda p: ((p[0] - 16) * k, (16 - p[1]) * k)      # SVG -> mm, y up
    hexa = [P(p) for p in GEM_HEX]
    segs = [(hexa[i], hexa[(i + 1) % 6]) for i in range(6)] + [(P(p), P(q)) for p, q in GEM_EDGES]
    lines = CrossSection.batch_boolean([stroke(p, q, stroke_w) for p, q in segs], m3d.OpType.Add)
    accent = CrossSection([[P(p) for p in GEM_ACCENT]], FillRule.EvenOdd).offset(-stroke_w / 2, m3d.JoinType.Miter)
    return lines, accent

LOGO_R    = 28.0     # the disc's groove radius
GEM_SIZE  = 20.0     # the gem's radius in it
GROOVE_W  = 1.0
ENGRAVE   = 0.6      # the logo's and groove's depth
LOGO_CX, LOGO_CY = CX, CY

def top_marks_2d():
    """The engravings on the top: (the mark's lines, its accent, the disc's groove)."""
    # the widest mark the disc holds, 2.5 mm inside the groove
    lines, accent = logo_2d(GEM_SIZE)
    # the vented side (y-max) is the front, the cables leave at the back and sides (a Mac
    # mini): the gem reads from the front, as on the site
    place = lambda cs: cs.rotate(180).translate((LOGO_CX, LOGO_CY))
    ring = CrossSection.circle(LOGO_R + GROOVE_W / 2, 128) - CrossSection.circle(LOGO_R - GROOVE_W / 2, 128)
    return place(lines), place(accent), ring.translate((LOGO_CX, LOGO_CY))

def vent_ring_2d():
    """Arc vents around the disc (the SoC lies beneath)."""
    r0, r1 = LOGO_R + 3.0, LOGO_R + 6.5
    n, gap = 12, 9.0                   # 12 arcs, 9 degrees between them
    out = []
    for k in range(n):
        a0 = math.radians(k * 360 / n + gap / 2)
        a1 = math.radians((k + 1) * 360 / n - gap / 2)
        pts = []
        for i in range(17):
            a = a0 + (a1 - a0) * i / 16
            pts.append((LOGO_CX + r1 * math.cos(a), LOGO_CY + r1 * math.sin(a)))
        for i in range(17):
            a = a1 - (a1 - a0) * i / 16
            pts.append((LOGO_CX + r0 * math.cos(a), LOGO_CY + r0 * math.sin(a)))
        out.append(CrossSection([pts]))
    return CrossSection.batch_boolean(out, m3d.OpType.Add)

# ---------------------------------------------------------------- the whole case
def build():
    body = rounded_slab(0.0, Z_TOP, True, True)
    cavity = rrect(IN_X0, IN_Y0, IN_X1, IN_Y1, max(1.0, R_CORNER - WALL)).extrude(Z_CEIL - FLOOR).translate((0, 0, FLOOR))
    shell = body - cavity

    # base: standoffs
    for (hx, hy) in HOLES:
        shell = shell + cyl(hx, hy, FLOOR - 0.1, Z_PCB_BOT, 6.0)
    # cover: posts that press the board down and take the screws
    for (hx, hy) in HOLES:
        shell = shell + cyl(hx, hy, Z_PCB_TOP + 0.15, Z_CEIL + 0.1, 5.4)
    # the cover's screw bosses: a column from the floor to the top, split with the rest
    for (bx, by) in BOSSES:
        shell = shell + cyl(bx, by, FLOOR - 0.1, Z_CEIL + 0.1, BOSS_D)

    cuts = []
    for (hx, hy) in HOLES:
        cuts.append(cyl(hx, hy, -1, Z_PCB_TOP + 0.2, 2.8))         # M2.5 clearance
        cuts.append(cyl(hx, hy, -1, 2.6, 5.4))                      # the screw head's counterbore
        cuts.append(cyl(hx, hy, Z_PCB_TOP + 0.1, Z_CEIL - 2.0, 2.2))  # pilot (self-tapping M2.5)
    for (bx, by) in BOSSES:
        cuts.append(cyl(bx, by, -1, Z_SPLIT + 0.2, 3.4))            # M3 clearance
        cuts.append(cyl(bx, by, -1, 3.0, 6.4))                      # the screw head's counterbore
        cuts.append(cyl(bx, by, Z_SPLIT - 0.1, Z_CEIL - 2.0, 2.6))   # pilot (self-tapping M3)

    t = Z_PCB_TOP
    # the y-min side: USB-C, micro-HDMI 0 and 1, audio
    cuts.append(slot_x(3.5 + 7.7, t + 1.6, 12.5, 7.4, 2.5))
    cuts.append(slot_x(3.5 + 7.7 + 14.8, t + 1.5, 10.6, 7.0, 2.0))
    cuts.append(slot_x(3.5 + 7.7 + 14.8 + 13.5, t + 1.5, 10.6, 7.0, 2.0))
    cuts.append(Manifold.cylinder(WALL + 4, 4.2, 4.2, 48).rotate((90, 0, 0)).translate((53.5, IN_Y0 + 2, t + 3.0)))
    # the x-max side: the two USB stacks, Ethernet
    cuts.append(slot_y(9.0, t - 0.4, t + 16.4, 14.2, 1.2))
    cuts.append(slot_y(27.0, t - 0.4, t + 16.4, 14.2, 1.2))
    cuts.append(slot_y(45.75, t - 0.4, t + 13.9, 16.6, 1.2))
    # the x-min side: the micro-SD card, under the board, with room for a finger
    cuts.append(box(X0 - 1, 28 - 8.0, FLOOR + 1.0, IN_X0 + 0.5, 28 + 8.0, Z_SPLIT + 0.01))
    # the two status LEDs (PWR, ACT) at the micro-SD edge: a little window
    cuts.append(box(X0 - 1, 6.0, t + 0.6, IN_X0 + 0.5, 12.5, t + 2.4))

    # the back (y-max): vertical vents in the cover's wall
    for k in range(15):
        x = 10.0 + k * 4.6
        cs = rrect(-0.9, Z_SPLIT + 4.0, 0.9, Z_CEIL - 3.0, 0.9)
        cuts.append(cs.extrude(WALL + 4).rotate((90, 0, 0)).translate((x, Y1 + 2, 0)))

    # the top: the disc's groove, the logo (engraved), the arc vents
    logo, accent, ring = top_marks_2d()
    cuts.append((logo + accent + ring).extrude(ENGRAVE + 0.01).translate((0, 0, Z_TOP - ENGRAVE)))
    cuts.append(vent_ring_2d().extrude(TOP + 1).translate((0, 0, Z_CEIL - 0.5)))
    if GPIO_SLOT:
        cuts.append(box(6.0, 48.5, Z_CEIL - 1, 59.5, 56.5, Z_TOP + 1))

    # the bottom: a round grille under the SoC (Mac mini) and four recesses for rubber feet
    gcx, gcy = 32.0, 30.0
    for i in range(-6, 7):
        for j in range(-6, 7):
            px, py = gcx + i * 3.4 + (1.7 if j % 2 else 0), gcy + j * 2.95
            if math.hypot(px - gcx, py - gcy) <= 15.0:
                cuts.append(cyl(px, py, -1, FLOOR + 1, 2.0, 16))
    for fx, fy in [(14.0, 6.5), (74.0, 6.5), (14.0, IN_Y1 - 8.0), (74.0, IN_Y1 - 8.0)]:
        cuts.append(cyl(fx, fy, -1, 0.8, 10.4))

    shell = shell - Manifold.batch_boolean(cuts, m3d.OpType.Add)

    # split, the shadow line, a lip on the base (the back and micro-SD sides) to register the cover
    base  = shell.trim_by_plane((0, 0, -1), -Z_SPLIT)
    cover = shell.trim_by_plane((0, 0, 1), Z_SPLIT)
    base  = base  - reveal_cut(Z_SPLIT, Z_SPLIT - REVEAL)
    cover = cover - reveal_cut(Z_SPLIT, Z_SPLIT + REVEAL)

    LIP_T, LIP_H, CLR = 1.2, 3.0, 0.25
    lip_outer = rrect(IN_X0 + CLR, IN_Y0 + CLR, IN_X1 - CLR, IN_Y1 - CLR, max(1.0, R_CORNER - WALL - CLR))
    lip_ring = lip_outer - lip_outer.offset(-LIP_T, m3d.JoinType.Round)
    lip = lip_ring.extrude(LIP_H).translate((0, 0, Z_SPLIT - 0.01))
    # keep the back side and the micro-SD side only (no room among the ports), away from the SD notch
    keep = box(X0 - 1, 55.0, 0, X1 + 1, Y1 + 1, 100) + box(X0 - 1, Y0 - 1, 0, -0.3, Y1 + 1, 100)
    lip = (lip ^ keep) - box(X0 - 1, 28 - 9.0, 0, 0, 28 + 9.0, 100) - box(X0 - 1, 5.0, 0, 0, 13.5, 100)
    for (bx, by) in BOSSES:
        lip = lip - cyl(bx, by, 0, 100, BOSS_D + 0.6)
    base = base + lip

    inlay  = logo.extrude(ENGRAVE - 0.05).translate((0, 0, Z_TOP - ENGRAVE))
    inlay2 = accent.extrude(ENGRAVE - 0.05).translate((0, 0, Z_TOP - ENGRAVE))
    return base, cover, inlay, inlay2

# ---------------------------------------------------------------- output
def to_trimesh(man):
    import trimesh
    mesh = man.to_mesh()
    return trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3],
                           faces=np.asarray(mesh.tri_verts), process=True)

def main():
    import trimesh
    base, cover, inlay, accent = build()
    for name, man in (("base", base), ("cover", cover), ("inlay", inlay), ("accent", accent)):
        if man.status() != m3d.Error.NoError or man.is_empty():
            sys.exit(f"{name}: bad solid ({man.status()})")
    out = HERE
    # print orientation: the base as it stands; the cover upside down, its top on the bed
    flip = lambda m_: m_.rotate((180, 0, 0)).translate((0, 0, Z_TOP))
    to_trimesh(base).export(os.path.join(out, "onyx_case_base.stl"))
    to_trimesh(flip(cover)).export(os.path.join(out, "onyx_case_cover.stl"))
    to_trimesh(flip(inlay)).export(os.path.join(out, "onyx_case_logo_inlay.stl"))
    to_trimesh(flip(accent)).export(os.path.join(out, "onyx_case_logo_accent.stl"))
    to_trimesh(base + cover).export(os.path.join(out, "onyx_case_assembled.stl"))
    for name, man in (("base", base), ("cover", cover), ("inlay", inlay), ("accent", accent)):
        bb = man.bounding_box()
        print(f"{name:6s} {man.num_tri():7d} tris  {man.volume()/1000:6.1f} cm3  "
              f"{bb[3]-bb[0]:.1f} x {bb[4]-bb[1]:.1f} x {bb[5]-bb[2]:.1f} mm  genus {man.genus()}")

if __name__ == "__main__":
    main()
