#!/usr/bin/env python3
# Cosmo Zoom! -- the circuits' source: each circuit described as a drive (straights and arcs, the heights and the
# widths at chosen places, the features and the props at named marks), made into its track file
# sdcard/apps/cosmozoom.app/tracks/<id>.trk (the control points sampled along the drive, the features' t computed).
# The layout is closed exactly: the lengths of the straights marked `free` are solved so the drive ends where it
# began (the turns must add up to a whole turn -- or none, for a figure of eight).
#   python3 tools/cosmozoom/trackgen.py [id ...]        (no id: all of them)
# Check a circuit: tools/tests/cosmozoom/trackmap.cpp (the map, the crossings), racesim.cpp, racepic.cpp.
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
import math, os, sys

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "sdcard", "apps", "cosmozoom.app", "tracks")


class Course:
    """A drive: S (straight), R / L (an arc to the right / left: radius, degrees). Heading: yaw in degrees,
    the direction (sin, cos) in (x, z) -- the engine's frame, where turning right lowers the yaw."""

    def __init__(self, cid, x=0.0, z=0.0, heading=0.0, y=0.0, step=55.0, scale=1.0):
        """scale: the whole layout made bigger or smaller (the straights, the radii, the places of things along
        and across the road; not the heights)."""
        self.id, self.x0, self.z0, self.h0, self.y0, self.step, self.k = cid, x, z, heading, y, step, scale
        self.ops = []
        self.head = []
        self.feats = []
        self.lines = []
        self.decors = []

    # ---- the drive
    def S(self, length=100.0, free=False, fix=False, m=None, **k):
        """A straight: its length is a wish -- the straights are stretched or shortened to close the lap, the free ones
        most, the fixed ones never."""
        if m:
            self.mark(m)
        self.ops.append(dict(kind="S", L=length * self.k, free=free, fix=fix, **k)); return self

    def R(self, r, deg, m=None, **k):
        if m:
            self.mark(m)
        self.ops.append(dict(kind="A", r=r * self.k, a=-deg, **k)); return self

    def L(self, r, deg, m=None, **k):
        if m:
            self.mark(m)
        self.ops.append(dict(kind="A", r=r * self.k, a=deg, **k)); return self

    def mark(self, name):
        self.ops.append(dict(kind="M", name=name)); return self

    # ---- what is on it
    def feat(self, kind, at, d=0.0, lane=None):
        self.feats.append((kind, at, d * self.k, None, 0.0, lane)); return self

    def span(self, kind, a, b, da=0.0, db=0.0, extra=None):
        self.feats.append((kind, a, da * self.k, b, db * self.k, extra)); return self

    def line(self, text):
        self.lines.append(text); return self

    def decor(self, model, at, d=0.0, lat=0.0, dy=0.0, yaw=0.0, scale=1.0, ground=False):
        """A prop placed from the road: at a mark + d metres, lat metres to the right, dy above the road (or on the
        ground: no y), yaw added to the road's heading."""
        self.decors.append((model, at, d * self.k, lat * self.k, dy, yaw, scale, ground)); return self

    def decorAbs(self, model, x, z, y=None, yaw=0.0, scale=1.0):
        """A prop at a place (y None: on the ground, its own size and heading)."""
        self.decors.append((model, None, x, z, y, yaw, scale, None)); return self

    # ---- made
    def _walk(self, lengths):
        """Poses along the drive: a list of (s, x, z, heading) at each op's start, and the end. lengths: the
        straights' lengths by op index (else their wish)."""
        x, z, h, s = self.x0, self.z0, self.h0, 0.0
        poses = []
        for i, op in enumerate(self.ops):
            poses.append((s, x, z, h))
            if op["kind"] == "S":
                L = lengths.get(i, op["L"])
                r = math.radians(h)
                x += math.sin(r) * L; z += math.cos(r) * L; s += L
                op["_L"] = L
            elif op["kind"] == "A":
                rr, a = op["r"], op["a"]
                h1 = h + a
                # the centre: to the left (a > 0) or the right of the heading
                r0 = math.radians(h)
                lx, lz = math.cos(r0), -math.sin(r0)      # the left side: (cos, -sin)
                sg = 1 if a > 0 else -1
                cx, cz = x + lx * rr * sg, z + lz * rr * sg
                r1 = math.radians(h1)
                l1x, l1z = math.cos(r1), -math.sin(r1)
                x, z = cx - l1x * rr * sg, cz - l1z * rr * sg
                h = h1
                L = abs(math.radians(a)) * rr
                s += L
                op["_L"] = L
        poses.append((s, x, z, h))
        return poses

    def build(self):
        nfree = sum(1 for o in self.ops if o["kind"] == "S" and o["free"])
        turn = sum(o["a"] for o in self.ops if o["kind"] == "A")
        if abs(turn % 360) > 0.01 and abs(turn % 360 - 360) > 0.01:
            print("  %s: the turns add up to %.1f degrees (not a whole turn)" % (self.id, turn))
        # the straights stretched to close the lap: the least change (weighted) that brings the end onto the start
        adj = [i for i, o in enumerate(self.ops) if o["kind"] == "S" and not o["fix"]]
        base = self._walk({})[-1]
        rx, rz = base[1] - self.x0, base[2] - self.z0
        self.residual = (rx, rz)
        poses0 = self._walk({})
        dirs, inv = [], []
        for i in adj:
            r = math.radians(poses0[i][3])
            dirs.append((math.sin(r), math.cos(r)))
            inv.append(1.0 if self.ops[i]["free"] else 0.08)
        m00 = sum(w * d[0] * d[0] for w, d in zip(inv, dirs)); m01 = sum(w * d[0] * d[1] for w, d in zip(inv, dirs))
        m11 = sum(w * d[1] * d[1] for w, d in zip(inv, dirs))
        det = m00 * m11 - m01 * m01
        l0 = (-rx * m11 + rz * m01) / det
        l1 = (-rz * m00 + rx * m01) / det
        sol = {}
        for i, w, d in zip(adj, inv, dirs):
            sol[i] = self.ops[i]["L"] + w * (d[0] * l0 + d[1] * l1)
            if sol[i] < 15:
                print("  %s: straight #%d came out %.1f m long (wished %.0f)" % (self.id, i, sol[i], self.ops[i]["L"]))
        poses = self._walk(sol)
        end = poses[-1]
        if abs(end[1] - self.x0) > 0.01 or abs(end[2] - self.z0) > 0.01:
            print("  %s: not closed (%.2f, %.2f)" % (self.id, end[1] - self.x0, end[2] - self.z0))
        self.total = end[0]
        self.free = sol
        self.stretch = [(i, self.ops[i]["L"], sol[i]) for i in adj]
        # the marks
        self.marks = {}
        for op, p in zip(self.ops, poses):
            if op["kind"] == "M":
                self.marks[op["name"]] = p[0]
        self.marks["start"] = 0.0
        # the keyframes: height, width, bank at the ops' ends
        def keys(name, default):
            v0 = self.y0 if name == "y" else default
            ks, v = [(0.0, v0)], v0
            for op, p0, p1 in zip(self.ops, poses, poses[1:]):
                if op["kind"] == "M" or p1[0] <= p0[0]:
                    continue
                v = op.get(name, v)
                ks.append((p1[0], v))
            ks[-1] = (self.total, v0)
            return ks
        self.ky, self.kw, self.kb = keys("y", 0.0), keys("w", 0.0), keys("bank", 0.0)
        # the samples: the control points
        pts = []
        for op, p0, p1 in zip(self.ops, poses, poses[1:]):
            L = op.get("_L", 0.0)
            if op["kind"] == "M" or L <= 0:
                continue
            if op["kind"] == "S":
                n = max(1, math.ceil(L / self.step - 0.15))
                for i in range(n):
                    f = i / n
                    pts.append((p0[0] + L * f, p0[1] + (p1[1] - p0[1]) * f, p0[2] + (p1[2] - p0[2]) * f))
            else:
                rr = op["r"]
                st = min(self.step * 0.8, max(18.0, rr * 0.62))
                n = max(1, math.ceil(L / st))
                for i in range(n):
                    f = i / n
                    pts.append((p0[0] + L * f,) + self._arcPoint(p0, op, f))
        self.pts = pts
        return self

    def _arcPoint(self, p0, op, f):
        rr, a = op["r"], op["a"]
        h, x, z = p0[3], p0[1], p0[2]
        r0 = math.radians(h)
        lx, lz = math.cos(r0), -math.sin(r0)
        sg = 1 if a > 0 else -1
        cx, cz = x + lx * rr * sg, z + lz * rr * sg
        r1 = math.radians(h + a * f)
        l1x, l1z = math.cos(r1), -math.sin(r1)
        return (cx - l1x * rr * sg, cz - l1z * rr * sg)

    @staticmethod
    def _interp(ks, s, smooth=True):
        """The value at s through the keyframes: a monotone cubic (no overshoot; level at each peak and trough)."""
        n = len(ks)
        for i in range(n - 1):
            (s0, v0), (s1, v1) = ks[i], ks[i + 1]
            if s0 <= s <= s1:
                if s1 <= s0:
                    return v1
                h = s1 - s0
                def slope(j):
                    # the tangent at keyframe j (closed: the ends wrap)
                    a = ks[j - 1] if j > 0 else (ks[-2][0] - ks[-1][0], ks[-2][1])
                    b = ks[j + 1] if j < n - 1 else (ks[1][0] + ks[-1][0], ks[1][1])
                    sj, vj = ks[j]
                    d0 = (vj - a[1]) / max(1e-6, sj - a[0])
                    d1 = (b[1] - vj) / max(1e-6, b[0] - sj)
                    if d0 * d1 <= 0:
                        return 0.0
                    return 2.0 / (1.0 / d0 + 1.0 / d1)      # (the harmonic mean: Fritsch-Carlson)
                m0, m1 = slope(i), slope(i + 1)
                f = (s - s0) / h
                h00, h10, h01, h11 = 2 * f ** 3 - 3 * f ** 2 + 1, f ** 3 - 2 * f ** 2 + f, -2 * f ** 3 + 3 * f ** 2, f ** 3 - f ** 2
                return h00 * v0 + h10 * h * m0 + h01 * v1 + h11 * h * m1
        return ks[-1][1]

    def at(self, name, d=0.0):
        """s, x, z, y, heading at a mark + d metres."""
        s = (self.marks[name] + d) % self.total
        poses = self._walk(self.free)
        for op, p0, p1 in zip(self.ops, poses, poses[1:]):
            if p0[0] <= s <= p1[0] and op["kind"] != "M":
                L = p1[0] - p0[0]
                f = (s - p0[0]) / L if L > 0 else 0
                if op["kind"] == "S":
                    x, z, h = p0[1] + (p1[1] - p0[1]) * f, p0[2] + (p1[2] - p0[2]) * f, p0[3]
                else:
                    x, z = self._arcPoint(p0, op, f)
                    h = p0[3] + op["a"] * f
                return s, x, z, self._interp(self.ky, s), h
        return s, self.x0, self.z0, self.y0, self.h0

    def t(self, name, d=0.0):
        return ((self.marks[name] + d) % self.total) / self.total

    def write(self):
        o = ["# Cosmo Zoom! -- %s: made by tools/cosmozoom/trackgen.py (edit the circuit there, then run it)." % self.head[0][1],
             "# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).", ""]
        for k, v in self.head:
            o.append("%-10s = %s" % (k, v))
        self.groundY = min(self._interp(self.ky, q[0]) for q in self.pts) - 3.0
        o.append("")
        o.append("# the drive: %.0f m, %d control points (x z height [width [bank]])" % (self.total, len(self.pts)))
        for s, x, z in self.pts:
            y = self._interp(self.ky, s)
            w = self._interp(self.kw, s)
            b = self._interp(self.kb, s)
            line = "p = %.1f %.1f %.1f" % (x, z, y)
            if abs(w) > 0.05 or abs(b) > 0.05:
                line += " %.1f" % w if abs(w) > 0.05 else " 0"
            if abs(b) > 0.05:
                line += " %.1f" % b
            o.append(line)
        o.append("")
        o.append("# the features (t: 0..1 of the lap)")
        for kind, a, da, b, db, extra in self.feats:
            if b is None:
                if kind == "boost":
                    o.append("boost = %.3f %.2f" % (self.t(a, da), extra or 0))
                else:
                    o.append("%s = %.3f" % (kind, self.t(a, da)))
            else:
                t0, t1 = self.t(a, da), self.t(b, db)
                if kind == "twist":
                    o.append("twist = %.3f %.3f %s" % (t0, t1, extra or 1))
                else:
                    o.append("%s = %.3f %.3f" % (kind, t0, t1))
        if self.lines:
            o.append("")
            o.append("# the props")
            for l in self.lines:
                if callable(l):
                    l = l(self)
                o.append(l)
        if self.decors:
            for model, at, d, lat, dy, yaw, scale, ground in self.decors:
                if at is None:
                    # (x, z given: y on the ground when none)
                    x, z, y = d, lat, dy
                    o.append("decor = %s %.1f %.1f" % (model, x, z) if y is None else "decor = %s %.1f %.1f %.1f %.0f %.2f" % (model, x, z, y, yaw, scale))
                    continue
                s, x, z, y, h = self.at(at, d)
                r = math.radians(h)
                rx, rz = -math.cos(r), math.sin(r)      # the right side
                x += rx * lat; z += rz * lat
                if ground:
                    if yaw == 0 and scale == 1:
                        o.append("decor = %s %.1f %.1f" % (model, x, z))
                    else:
                        o.append("decor = %s %.1f %.1f %.1f %.0f %.2f" % (model, x, z, self.groundY, h + yaw, scale))
                else:
                    o.append("decor = %s %.1f %.1f %.1f %.0f %.2f" % (model, x, z, y + dy, h + yaw, scale))
        o.append("")
        path = os.path.join(OUT, self.id + ".trk")
        with open(path, "w") as f:
            f.write("\n".join(o))
        print("%-10s %6.0f m  %3d points  gap as wished %+.0f %+.0f  stretched %s" % (self.id, self.total, len(self.pts), self.residual[0], self.residual[1], " ".join("%.0f>%.0f" % (a, b) for i, a, b in self.stretch if abs(a - b) > 5)))


def header(c, **kv):
    order = ["name", "name_fr", "time", "sky", "ground", "groundkind", "road", "rails", "walls", "neon", "city", "width", "laps", "edge"]
    c.head = [(k, kv[k]) for k in order if k in kv]


CIRCUITS = {}


def circuit(f):
    CIRCUITS[f.__name__] = f
    return f


# ======================================================================================================================
# the circuits (DESIGN.md 7.3 / 7.4)
# ======================================================================================================================

# (A mark given to an op, m="name", is where that op begins; c.mark("name") alone: the place reached.)

def ringOverTube(c, at, d, sc=2.1):
    """A glowing ring round a tube (its axis 7 m under the road's crown)."""
    c.decor("holo_ring", at, d, 0, -7 - 8 * sc, 0, sc)


def gate(c, at, d, sc=1.25):
    """A glowing ring standing over the road."""
    c.decor("holo_ring", at, d, 0, -0.6, 0, sc)


@circuit
def harbour():
    # 1. Sky Harbour: the league's home, the tutorial -- wide and gentle, the harbour bridge, the jump between two
    # piers, two speed pads; the docked ships, the towers, the "VELOCITY GRID" signs.
    c = Course("harbour")
    header(c, name="Sky Harbour", name_fr="Port du Ciel", time="day", sky="4FA8F0 D4ECFF", ground="6ED6D2 78DCD8", groundkind="sea", road="7DB8EC 86C0F0", rails="FF9E9E FFFFFF", walls="FFFFFF FF9E9E", neon="FF9E9E 8FD8FF FFE08A", city="1", width="18", laps="3", edge="FFFFFF")
    c.S(170, m="s1")
    c.R(110, 95)                                # the first long sweeper
    c.S(50, y=5, m="bridge")                    # the harbour bridge
    c.L(130, 35, y=11)
    c.R(130, 35, m="crest")
    c.S(60, free=True, y=6)
    c.R(55, 85, y=1)                            # down to the piers
    c.S(120, m="pier")                          # the two piers: the jump between them
    c.L(150, 20)
    c.R(150, 20)
    c.S(80, m="pier2")
    c.R(60, 75, y=0)
    c.L(80, 55, m="quay")                       # the quay's S-bend
    c.R(80, 70)
    c.S(150, free=True, m="back")
    c.R(70, 50)
    c.S(40)
    c.R(70, 40)
    c.S(70, fix=True)
    c.feat("boost", "s1", 100, 0)
    c.feat("items", "bridge", 20)
    c.feat("jump", "pier", 75)
    c.feat("items", "quay", 15)
    c.feat("boost", "back", 60, -0.4)
    c.feat("boost", "back", 60, 0.4)
    c.line(lambda c: "prop = kk_cargo_stack left 16 1.6 %.3f %.3f" % (c.t("pier", 10), c.t("pier2", 70)))
    c.line("scatter = kk_landingpad 7 1.2")
    c.line("scatter = traffic_ship 6 7")
    c.decor("traffic_ship", "pier", 40, -45, 0, 10, 9, ground=True)
    c.decor("traffic_ship", "pier2", 20, -50, 0, -5, 11, ground=True)
    c.decor("traffic_ship", "pier", 100, 42, 0, 180, 8, ground=True)
    gate(c, "crest", 0, 1.4)
    return c


@circuit
def domebay():
    # 2. Dome Bay: an ocean city of glass domes by day -- the cliff road, a tube flown above the sea on a long curve,
    # the rolling waves (a jump off a crest), the sandbar's S-bend, the long sea front.
    c = Course("domebay")
    header(c, name="Dome Bay", name_fr="Baie des Dômes", time="day", sky="5AB4F5 DDF2FF", ground="7FE0DC 8AE6E0", groundkind="sea", road="3FAFA8 48B8B0", rails="FFB3C7 BDF2D5", walls="F2E8D8 FFB3C7", neon="FFB3C7 7FE0DC FFE08A", city="0.6", width="16", laps="3", edge="FFFFFF")
    c.S(140, m="s1")
    c.R(75, 70)
    c.S(60, y=6, m="cliff")
    c.L(95, 60, y=8)
    c.R(65, 100, y=12)
    c.S(150, y=14, m="tubeA")                   # the tube above the sea
    c.L(230, 28, y=12)
    c.S(40)
    c.mark("tubeB")
    c.R(80, 95, y=3)
    c.S(40, fix=True, y=1, m="waves")           # the waves
    c.S(50, y=5); c.mark("crest1")
    c.S(50, y=1)
    c.S(50, y=5); c.mark("crest2")
    c.S(40, y=1)
    c.R(60, 80, y=0)
    c.L(70, 70, m="sandbar")                    # the sandbar's S-bend
    c.R(70, 83)
    c.S(150, free=True, m="front")              # the sea front
    c.L(300, 16)
    c.R(300, 16)
    c.S(60, free=True)
    c.R(65, 90)
    c.S(80, fix=True)
    c.feat("items", "s1", 90)
    c.span("tube", "tubeA", "tubeB", 0, 0)
    c.feat("boost", "crest1", -25, -0.3)
    c.feat("jump", "crest2", -10)
    c.feat("boost", "front", 40, 0.3)
    c.feat("items", "front", 120)
    c.feat("boost", "front", 250, -0.3)
    c.line("scatter = kk_dome 16 2.2")
    c.line("scatter = kk_water_storage 6 1.4")
    c.line("scatter = kk_landingpad 6 1.4")
    c.decor("kk_dome", "tubeA", 100, -60, 0, 0, 3.5, ground=True)
    c.decor("kk_dome", "tubeA", 200, 70, 0, 0, 4.0, ground=True)
    ringOverTube(c, "tubeA", 70)
    ringOverTube(c, "tubeA", 160)
    return c


@circuit
def foundry():
    # 3. Mech Foundry: a factory district at dusk -- right-angle corners like a robot's steps, the climb to the upper
    # deck, the dive into the assembly hall's tunnel, the crate jump.
    c = Course("foundry")
    header(c, name="Mech Foundry", name_fr="Fonderie Méca", time="dusk", sky="7A8CE0 FFC59A", ground="F0D8C8 E8D0C0", groundkind="clouds", road="9A9AD8 A2A2DE", rails="FFD27A FFFFFF", walls="FFC8A0 FFD27A", neon="FFD27A FF9E7A 8FD8FF", city="1", width="15", laps="3", edge="FFFFFF")
    c.S(140, m="s1")
    c.R(28, 90)                                 # the steps: right angles
    c.S(90, m="step1")
    c.L(28, 90)
    c.S(60)
    c.R(28, 90)
    c.S(200, free=True, m="conveyor")
    c.R(70, 90, y=6)                            # up to the upper deck
    c.S(230, free=True, y=14, m="deck")
    c.R(30, 90)
    c.S(60, y=8, m="dive")
    c.S(130, y=0, m="hall")                     # the assembly hall
    c.L(45, 60)
    c.R(45, 60)
    c.S(110, m="crates")
    c.R(55, 90)
    c.S(70, fix=True)
    c.feat("boost", "s1", 60, 0)
    c.feat("items", "step1", 40)
    c.feat("boost", "conveyor", 50, 0.5)
    c.feat("boost", "conveyor", 50, -0.5)
    c.feat("items", "deck", 40)
    c.span("tunnel", "hall", "hall", -10, 180)
    c.feat("jump", "crates", 45)
    c.line(lambda c: "prop = kk_cargo_stack both 14 1.5 %.3f %.3f" % (c.t("crates"), c.t("crates", 110)))
    c.line(lambda c: "prop = kk_water_storage left 45 1.3 %.3f %.3f" % (c.t("conveyor"), c.t("conveyor", 200)))
    c.line("scatter = kk_structure_tall 14 1.6")
    c.line("scatter = kk_cargo_stack 30 2")
    return c


@circuit
def jungle():
    # 4. Jungle Isles: a figure of eight in a glowing jungle at night -- the high bridge over the low road, the vine
    # tunnel, the waterfall jump down into the lower lobe.
    c = Course("jungle", heading=45)
    header(c, name="Jungle Isles", name_fr="Îles de la Jungle", time="day", sky="4FB0E8 D8F4EC", ground="FFFFFF F4FAFF", groundkind="islands", road="6CCB8E 75D296", rails="FFF2A0 FFFFFF", walls="BDF2D5 FFF2A0", neon="FFF2A0 6CCB8E FFB3C7", city="0.4", width="15", laps="3", edge="FFFFFF")
    c.S(260, free=True, m="under")              # the low road, under the bridge
    c.R(100, 90)
    c.S(86, m="lower")
    c.R(80, 90)
    c.S(90)
    c.R(100, 90, y=12)
    c.S(290, free=True, y=14, m="bridge")       # up over the low road
    c.L(100, 90, y=10)
    c.S(90, m="vines")                          # the vine tunnel
    c.L(80, 90, y=8)
    c.S(70, m="falls")
    c.S(60, y=1)                                # the waterfall
    c.L(100, 90, y=0)
    c.S(90)
    c.feat("items", "under", 60)
    c.feat("boost", "lower", 30, 0)
    c.feat("boost", "bridge", 40, 0)
    c.span("tunnel", "vines", "vines", -10, 100)
    c.feat("items", "vines", 120)
    c.feat("jump", "falls", 62)
    c.line("scatter = kn_tree 60 2.2")
    c.line("scatter = kn_tree_pine 40 2.8")
    c.line("prop = kn_tree both 16 1.6")
    c.line("scatter = kk_structure_tall 5 1.4")
    return c


@circuit
def crystal():
    # 5. Crystal Moon: ice under the aurora -- the high ridge, the ski jump off the cliff, the valley's hairpins (the
    # ice cave through the middle peak), the long climb back.
    c = Course("crystal", heading=-90, y=18)
    header(c, name="Crystal Moon", name_fr="Lune de Cristal", time="night", sky="232A72 6A70C8", ground="E8F0FF DCE6FA", groundkind="snow", road="B8A8F0 C0B2F4", rails="FFFFFF A8E0FF", walls="E8F0FF B8A8F0", neon="B8A8F0 A8E0FF FFFFFF", city="0.3", width="15", laps="3", edge="FFFFFF")
    c.S(260, free=True, m="ridge")
    c.L(55, 90, y=17)
    c.S(70, y=15, m="cliff")                    # the ski jump
    c.S(110, y=5)
    c.S(100, free=True, y=0)
    c.L(60, 150)
    c.S(200, y=6, m="up1")
    c.R(22, 120, y=8, m="cave")                 # the first hairpin, in the ice cave
    c.S(200, y=0, m="down")
    c.L(22, 120, m="pin2")
    c.S(200, y=9, m="climb")
    c.S(200, y=18)
    c.L(60, 120)
    c.S(60, fix=True)
    c.feat("items", "ridge", 120)
    c.feat("jump", "cliff", 30)
    c.feat("boost", "up1", 40, 0)
    c.span("tunnel", "cave", "down", -30, 40)
    c.feat("items", "down", 120)
    c.feat("boost", "climb", 60, -0.3)
    c.feat("boost", "climb", 60, 0.3)
    c.line("scatter = kn_tree_pine 70 2.4")
    c.line("scatter = kk_rocks_a 24 2.5")
    c.line("scatter = kk_dome 5 2.0")
    c.decor("kk_dome", "ridge", 120, 45, 0, 0, 2.6, ground=True)
    return c


@circuit
def asteroids():
    # 6. Asteroid Rings: a mining colony in a planet's rings, in deep space -- the hollow asteroid, the low-gravity
    # jumps down a long dive, the twisting ribbon, the tube between two rocks; climbs and dives in the void.
    c = Course("asteroids", scale=0.9)
    header(c, name="Asteroid Rings", name_fr="Anneaux d'Astéroïdes", time="space", sky="16164E 4A3A9A", ground="5A4A6A 4F4060", groundkind="none", road="A8A0F0 B0A8F4", rails="FFB3C7 FFE8A0", walls="C8B8FF FFE8A0", neon="FFB3C7 FFE8A0 A8E0FF", city="0", width="16", laps="3", edge="FFFFFF")
    c.S(200, free=True, m="s1")
    c.R(90, 70, y=10)
    c.S(80, y=22)
    c.L(70, 50, y=28, m="rock")                 # into the hollow asteroid
    c.S(130)
    c.R(80, 110, y=18)
    c.S(220, fix=True, y=-8, m="jumps")        # the dive: low-gravity jumps
    c.R(100, 60, y=-14)
    c.L(80, 40, y=-16)
    c.S(200, fix=True, m="twist")              # the twisting ribbon
    c.R(90, 100, y=-6)
    c.S(330, free=True, y=8, m="tubeA")         # the tube between two rocks
    c.L(250, 20, y=10)
    c.R(70, 130, y=2, m="tubeB")
    c.S(90, fix=True, y=0)
    c.feat("boost", "s1", 70, 0)
    c.span("tunnel", "rock", "rock", 30, 170)
    c.feat("items", "rock", 200)
    c.feat("jump", "jumps", 40)
    c.feat("jump", "jumps", 140)
    c.span("twist", "twist", "twist", 20, 190, 1)
    c.span("tube", "tubeA", "tubeB", 0, 10)
    c.feat("items", "tubeB", 90)
    c.feat("boost", "jumps", 100, -0.4)
    import random
    rnd = random.Random(6)
    c.decor("kk_rock_a", "rock", 80, 0, -14, 0, 9)          # the hollow asteroid round the tunnel
    c.decor("kk_rocks_a", "rock", 120, 0, -10, 90, 7)
    for k in range(20):
        m = rnd.choice(["kk_rock_a", "kk_rocks_a"])
        c.decor(m, rnd.choice(["s1", "jumps", "twist", "tubeA", "rock"]), rnd.uniform(0, 140), rnd.choice([-1, 1]) * rnd.uniform(35, 130),
                rnd.uniform(-60, 40), rnd.uniform(0, 360), rnd.uniform(3, 9))
    c.decor("kk_basemodule_a", "jumps", 60, -70, -30, 0, 2.5)
    c.decor("kk_structure_tall", "jumps", 60, -70, -12, 0, 1.5)
    c.decor("kk_solarpanel", "jumps", 60, -95, -28, 90, 4)
    c.decor("kk_basemodule_c", "tubeA", 60, 70, -35, 30, 2.5)
    c.decor("kk_landingpad", "tubeA", 60, 70, -38, 30, 2.5)
    c.decor("kk_dome", "twist", 80, -60, -40, 0, 3)
    ringOverTube(c, "tubeA", 50)
    ringOverTube(c, "tubeA", 140)
    gate(c, "s1", 30)
    c.decor("traffic_ship", "s1", 80, -50, 15, 90, 5)
    c.decor("traffic_ship", "twist", 40, 40, 20, 270, 5)
    return c


@circuit
def skyline():
    # 7. Skyline City: the tallest neon city by day -- the loop, the spiral round a tower (flown over its own way in),
    # the tube across the central gap, the twist on the long north avenue.
    c = Course("skyline")
    header(c, name="Skyline City", name_fr="Cité Skyline", time="day", sky="4FA8F0 D4ECFF", ground="FFFFFF F4FAFF", groundkind="clouds", road="2FA59A 38AFA4", rails="FFB3C7 BDF2D5", walls="0 FFFFFF", neon="FFB3C7 8FE8FF FFE08A", city="1", width="15", laps="3", edge="FFFFFF")
    c.S(150, m="s1")
    c.R(80, 90)
    c.S(178, m="loopRoad")                      # the loop
    c.L(70, 40)
    c.R(70, 40)
    c.S(80, fix=True, m="spiral")
    c.R(48, 270, y=16, bank=6, m="spiralR")     # the spiral round the tower, over its own way in
    c.S(70, fix=True, bank=0, m="spiralEnd")
    c.L(70, 90, y=14)
    c.S(300, free=True, m="tubeA")              # the tube across the central gap
    c.L(70, 90, y=4)
    c.S(400, free=True, y=0, m="avenue")        # the north avenue
    c.L(70, 90)
    c.S(120, m="top")
    c.L(70, 90)
    c.S(70, fix=True)
    c.feat("items", "s1", 90)
    c.feat("loop", "loopRoad", 50)
    c.feat("boost", "spiral", 30, 0)
    c.span("tube", "tubeA", "tubeA", 20, 250)
    c.feat("items", "avenue", 40)
    c.span("twist", "avenue", "avenue", 130, 330, 1)
    c.feat("boost", "top", 50, 0.4)
    gate(c, "loopRoad", 30)
    c.decor("tower_round", "spiralR", 0, 48, 0, 0, 1.6, ground=True)
    ringOverTube(c, "tubeA", 120, 2.4)
    return c


@circuit
def dunes():
    # 8. Twin Sun Dunes: a desert planet at sunset -- big dune crests and their jumps, long sweeping curves, the
    # canyon tunnel, the outpost's wind turbines.
    c = Course("dunes", scale=0.92)
    header(c, name="Twin Sun Dunes", name_fr="Dunes des Deux Soleils", time="dusk", sky="6A7AD8 FFC48A", ground="F2D2A0 EBC896", groundkind="sand", road="F0A890 F4B098", rails="FFFFFF FFD27A", walls="FFD8B0 FFFFFF", neon="FFD27A FF9E7A FFFFFF", city="0.35", width="16", laps="3", edge="FFFFFF")
    c.S(110, m="s1")
    c.S(80, y=9); c.mark("dune1")
    c.R(110, 60, y=4)
    c.S(80, y=2)
    c.L(90, 70, y=6)
    c.S(100, y=12); c.mark("dune2")
    c.R(90, 120, y=4)
    c.S(100, y=0, m="flat")
    c.R(140, 50, y=8)
    c.S(120, y=14); c.mark("dune3")
    c.L(70, 80, y=3, m="canyon")                # the canyon
    c.R(70, 100, y=0)
    c.S(200, free=True, y=6, m="north")
    c.R(80, 90, y=10)
    c.S(120, y=4, m="top")
    c.L(250, 22, y=10)
    c.R(250, 22, y=4)
    c.S(150, free=True, y=0)
    c.R(90, 90)
    c.S(80, fix=True)
    c.feat("jump", "dune1", -10)
    c.feat("boost", "dune2", -60, -0.4)
    c.feat("items", "flat", 40)
    c.feat("jump", "dune3", -10)
    c.span("tunnel", "canyon", "canyon", 0, 150)
    c.feat("boost", "north", 30, 0)
    c.feat("items", "top", 60)
    c.feat("boost", "top", 250, 0.4)
    c.line("scatter = kk_windturbine 14 1.6")
    c.line("scatter = kk_solarpanel 18 2")
    c.line("scatter = kk_rocks_a 30 3")
    c.line("scatter = kk_rock_a 16 5")
    c.line("prop = kk_rock_a both 30 3")
    c.decor("kk_rocks_a", "canyon", 40, 25, 0, 0, 7, ground=True)
    c.decor("kk_rocks_a", "canyon", 90, -25, 0, 90, 7, ground=True)
    c.decor("kk_basemodule_a", "flat", 60, 50, 0, 0, 1.8, ground=True)
    c.decor("kk_dome", "flat", 100, 55, 0, 0, 1.8, ground=True)
    return c


@circuit
def lava():
    # 9. Lava Moon: a volcano moon at night -- the corkscrew tube up round the cone, the long dive through the black
    # rock's tunnel, the lava-rock jumps, the glowing plain.
    c = Course("lava")
    header(c, name="Lava Moon", name_fr="Lune de Lave", time="dusk", sky="5A4AA8 FF9A7A", ground="B8806E AE7866", groundkind="rock", road="9A8AD8 A292DC", rails="FFB07A FFF0C0", walls="FFB8A0 FFF0C0", neon="FFB07A FFF0C0 C8B8FF", city="0.3", width="15", laps="3", edge="FFFFFF")
    c.S(150, m="s1")
    c.R(90, 70)
    c.S(110, m="plain")
    c.L(80, 40)
    c.R(70, 100)
    c.S(90, m="rocks")
    c.R(70, 30, y=2)
    c.S(150, free=True, m="toCone")
    c.R(55, 400, y=30, m="tubeA")               # the corkscrew round the volcano
    c.mark("tubeB")
    c.S(60, fix=True, y=28)
    c.S(250, free=True, y=4, m="tunnel")        # the dive through the black rock
    c.R(160, 160, y=0)
    c.S(150, m="lavaJump")
    c.S(80, fix=True)
    c.feat("items", "s1", 90)
    c.feat("boost", "plain", 40, 0)
    c.feat("jump", "rocks", 40)
    c.span("tube", "tubeA", "tubeB", 0, 0)
    c.span("tunnel", "tunnel", "tunnel", 90, 230)
    c.feat("items", "lavaJump", 20)
    c.feat("jump", "lavaJump", 100)
    c.decor("kk_rock_a", "tubeA", 0, 55, 0, 0, 8, ground=True)     # the volcano, in the corkscrew
    c.line("scatter = kk_rocks_a 40 3")
    c.line("scatter = kk_rock_a 20 5")
    c.line("scatter = kk_structure_tall 6 1.8")
    c.line("prop = kk_rock_a both 40 2")
    return c


@circuit
def cylinder():
    # 10. Hyper Cylinder: a tube course above the clouds by day -- three long tubes on wide curves, the short flat
    # links, the jump between two tubes, the spirals of speed pads.
    c = Course("cylinder", scale=0.93)
    header(c, name="Hyper Cylinder", name_fr="Hyper Cylindre", time="day", sky="3F9CF0 CFEAFF", ground="FFFFFF F4FAFF", groundkind="clouds", road="8FD0F0 98D6F2", rails="FFFFFF FF9AC0", walls="FFFFFF FF9AC0", neon="FF9AC0 8FD0F0 FFE08A", city="1", width="16", laps="3", edge="FFFFFF")
    c.S(90, fix=True, m="s1")
    c.S(120, y=8, m="tube1a")
    c.R(220, 100, y=14)
    c.S(60)
    c.mark("tube1b")
    c.R(45, 35, y=12)
    c.S(70, fix=True, m="gap")
    c.S(140, free=True, y=10, m="tube2a")
    c.L(200, 45, y=18)
    c.R(160, 140, y=20)
    c.S(100, free=True)
    c.mark("tube2b")
    c.R(45, 45, y=18, m="corner")
    c.S(200, free=True, y=10, m="tube3a")
    c.R(200, 55, y=2)
    c.S(80, y=0)
    c.mark("tube3b")
    c.R(60, 30)
    c.S(80, fix=True)
    c.span("tube", "tube1a", "tube1b", 0, 0)
    c.span("tube", "tube2a", "tube2b", 0, 0)
    c.span("tube", "tube3a", "tube3b", 0, 0)
    c.feat("jump", "gap", 25)
    c.feat("items", "corner", 20)
    for k in range(5):
        c.feat("boost", "tube1a", 70 + 22 * k, -0.6 + 0.3 * k)
        c.feat("boost", "tube3a", 70 + 22 * k, 0.6 - 0.3 * k)
    c.feat("boost", "tube3b", 40, 0)
    c.feat("items", "tube2a", 120)
    for at, d in (("tube1a", 60), ("tube2a", 60), ("tube2a", 330), ("tube3a", 150)):
        ringOverTube(c, at, d, 2.4)
    return c


@circuit
def carnival():
    # 11. Star Carnival: a neon fair at dusk -- the loop through the giant ring, the tube through the big wheel's hub,
    # the twist, the bumper zone (rows of capsules).
    c = Course("carnival")
    header(c, name="Star Carnival", name_fr="Fête Spatiale", time="dusk", sky="6A5AC8 FFB0C8", ground="FFFFFF FFF0F4", groundkind="islands", road="F0A0C8 F4A8CE", rails="FFE88A A8F0FF", walls="FFE0F0 FFE88A", neon="FFE88A F0A0C8 A8F0FF", city="0.6", width="16", laps="3", edge="FFFFFF")
    c.S(150, m="s1")
    c.R(70, 60)
    c.S(90, m="jumpRd")
    c.L(55, 110)
    c.S(60)
    c.R(60, 150)
    c.S(200, fix=True, m="loopRd")              # the loop through the giant ring
    c.R(70, 70)
    c.S(180, fix=True, m="mid")
    c.L(60, 100)
    c.R(60, 130, y=10)
    c.S(200, free=True, m="tubeA")              # the tube through the big wheel's hub
    c.R(70, 80, y=4)
    c.S(380, free=True, m="bumper")
    c.R(70, 80, y=0)
    c.S(70, fix=True)
    c.feat("items", "s1", 90)
    c.feat("jump", "jumpRd", 45)
    c.feat("boost", "loopRd", 20, 0)
    c.feat("loop", "loopRd", 70)
    c.span("twist", "mid", "mid", 25, 160, 1)
    c.span("tube", "tubeA", "tubeA", 0, 200)
    c.feat("items", "bumper", 120)
    c.feat("items", "bumper", 160)
    c.feat("items", "bumper", 200)
    c.feat("boost", "s1", 40, 0.4)
    gate(c, "loopRd", 50, 1.4)
    ringOverTube(c, "tubeA", 100, 5.0)          # the big wheel
    ringOverTube(c, "tubeA", 104, 4.6)
    c.line("scatter = holo_ring 10 1.5")
    return c


@circuit
def aurora():
    # 12. Aurora Forest: giant crystal trees under the aurora -- the root tunnel, the root jump, the tube up a giant
    # trunk and the long dive down, the walkways.
    c = Course("aurora")
    header(c, name="Aurora Forest", name_fr="Forêt Aurore", time="aurora", sky="2A3A8A 8FD8C8", ground="FFFFFF EEF4FF", groundkind="islands", road="7FD0B8 88D6BE", rails="D8B8FF FFFFFF", walls="E8F2FF D8B8FF", neon="D8B8FF 7FD0B8 FFFFFF", city="0.4", width="15", laps="3", edge="FFFFFF")
    c.S(140, m="s1")
    c.R(80, 80)
    c.S(90, m="roots")                          # under the roots
    c.L(70, 70)
    c.S(120, m="rootJump")
    c.R(60, 120)
    c.S(220, fix=True, y=22, m="tubeA")         # the tube up the giant trunk
    c.L(150, 40, y=32)
    c.mark("tubeB")
    c.S(80, y=28)
    c.R(70, 140, y=12)
    c.S(260, free=True, y=2, m="dive")          # the long dive
    c.L(60, 90, y=0)
    c.R(55, 150)
    c.S(120, free=True, m="walk")
    c.R(80, 70)
    c.S(80, fix=True)
    c.feat("items", "s1", 90)
    c.span("tunnel", "roots", "roots", -10, 100)
    c.feat("jump", "rootJump", 60)
    c.span("tube", "tubeA", "tubeB", 0, 0)
    c.feat("boost", "dive", 40, 0)
    c.feat("items", "walk", 40)
    c.feat("boost", "walk", 80, -0.4)
    c.line("scatter = kn_tree_pine 70 4")
    c.line("scatter = kn_tree 50 3")
    c.line("prop = kn_tree_pine both 22 2.5")
    return c


@circuit
def ribbon():
    # 13. Starlight Ribbon: the final -- a road of light through deep space: two loops, the long corkscrew tube, the
    # most jumps, a chain of speed pads, two twists, the ring gates.
    c = Course("ribbon")
    header(c, name="Starlight Ribbon", name_fr="Ruban d'Étoiles", time="space", sky="0E1450 3A3AA0", ground="16245E 1A2A6C", groundkind="none", road="9FE0F0 A8E6F4", rails="FFB3C7 FFFFFF", walls="C8D8FF FFB3C7", neon="FFB3C7 9FE0F0 FFE08A", city="0", width="15", laps="3", edge="FFFFFF")
    c.S(200, fix=True, m="s1")
    c.R(90, 70, y=12)
    c.S(160, m="loop1")
    c.L(80, 80, y=25)
    c.S(120, fix=True, y=30, m="jumps")
    c.R(60, 160, y=20)
    c.S(260, free=True, y=0, m="twist1")
    c.R(55, 450, y=-45, m="spiral")             # the corkscrew tube
    c.mark("spiralEnd")
    c.S(300, free=True, y=-10, m="top")
    c.R(90, 120, y=0)
    c.S(60, fix=True)
    c.feat("boost", "s1", 60, 0)
    c.feat("boost", "s1", 90, 0)
    c.feat("boost", "s1", 120, 0)
    c.feat("loop", "loop1", 60)
    c.feat("items", "jumps", 5)
    c.feat("jump", "jumps", 40)
    c.feat("jump", "jumps", 95)
    c.span("twist", "twist1", "twist1", 30, 220, 1)
    c.feat("jump", "twist1", 330)
    c.span("tube", "spiral", "spiralEnd", 0, 0)
    c.span("twist", "top", "top", 20, 180, 1)
    c.feat("loop", "top", 240)
    c.feat("items", "top", 340)
    c.feat("jump", "top", 390)
    gate(c, "s1", 20, 1.3)
    gate(c, "loop1", 40, 1.3)
    gate(c, "top", 70, 1.3)
    gate(c, "top", 200, 1.3)
    return c


if __name__ == "__main__":
    want = sys.argv[1:] or list(CIRCUITS)
    for cid in want:
        c = CIRCUITS[cid]()
        c.build()
        c.write()
