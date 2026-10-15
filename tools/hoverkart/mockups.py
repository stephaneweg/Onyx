#!/usr/bin/env python3
# mockups.py -- the design mock-ups of Cosmo Zoom, the anti-gravity racer of Onyx (docs/hoverkart/DESIGN.md).
#
#   python3 tools/hoverkart/mockups.py            -> docs/hoverkart/mockups/*.png
#   python3 tools/hoverkart/mockups.py title      -> only the pictures whose name holds "title"
#
# Everything is drawn by this script with Pillow (no picture is read): the title screen, the cup selection, the
# hangar, a race and a tube section with their HUD, the split screen, the sheets of the hulls, the parts, the
# pilots, the gadgets and the circuits, the palette and the art directions. The circuits' control points below
# are the layouts DESIGN.md section 7 describes (a closed Catmull-Rom spline, x / z in -1..1, scaled to the
# circuit's length); keep CIRCUITS and CUPS the same as DESIGN.md.
#
# The style (final, 2026-10-15): a neon-night city racer with a cel-shaded comic look -- dark-blue towers with lit
# windows and neon billboards, a dark road streaked with light, magenta and cyan neon rails, angular ships with long
# glowing trails, dark outlines. No faces, no mascots.
#
# MIT licence -- the Onyx authors, 2026.

import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "docs", "hoverkart", "mockups")
FONT_DIRS = [os.path.join(ROOT, "sdcard", "res", "fonts"), "/usr/share/fonts/truetype/dejavu"]
S = 2  # supersampling: drawn twice as big, then reduced (smooth edges)

# ---- the palette (DESIGN.md section 3.2) ----------------------------------------------------------------
NAVY = "#1B2340"
INK = NAVY
WHITE = "#FFFFFF"
SKYTOP = "#1F6FEB"
SKYLOW = "#8FD3FF"
CLOUD = "#FFFFFF"
CLOUDSH = "#CFE3FF"
ARCH = "#F2F6FF"
LGREY = "#C9D3E6"
STEEL = "#5B6E9A"
MINT = "#5FE3C0"
MINTL = "#8AF0D4"
TEAL = "#1FB5A8"
LIME = "#7CF06A"
MAGENTA = "#FF2E9A"
PINK = "#FF8AD0"
CYAN = "#3DE0FF"
YELLOW = "#FFD21F"
ORANGE = "#FF8A1F"
RED = "#F2384B"
VIOLET = "#7A5CFF"
CANOPY = "#24345E"
BLUE = "#2F80FF"
GOLD = "#FFC83D"
SILVER = "#C9D3E6"
BRONZE = "#E08A4A"
RAINBOW = [RED, ORANGE, YELLOW, LIME, CYAN, VIOLET]
# the neon night (DESIGN.md 3.2)
NSKY1 = "#0B1A4A"
NSKY2 = "#2A3FA0"
NTOWER = "#16245E"
NTOWER2 = "#1E3480"
ROAD = "#1A2A6C"
ROAD2 = "#22378A"
ROADU = "#101B4A"
NMAG = "#FF2BD6"
NCYAN = "#22E6FF"
NLIME = "#B8FF2E"
NYEL = "#FFE14D"
HOTPINK = "#FF4FA0"
OUTL = "#070B24"


def rgb(c, a=255):
    if isinstance(c, tuple):
        return c if len(c) == 4 else (c[0], c[1], c[2], a)
    c = c.lstrip("#")
    return (int(c[0:2], 16), int(c[2:4], 16), int(c[4:6], 16), a)


def mix(c1, c2, t):
    a, b = rgb(c1), rgb(c2)
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(4))


def light(c, t=0.35):
    return mix(c, WHITE, t)


def dark(c, t=0.3):
    return mix(c, INK, t)


_fonts = {}


def font(size, bold=True, italic=False):
    name = ("DejaVuSans-BoldOblique.ttf" if italic else "DejaVuSans-Bold.ttf") if bold else ("DejaVuSans-Oblique.ttf" if italic else "DejaVuSans.ttf")
    key = (name, int(size * S))
    if key not in _fonts:
        for d in FONT_DIRS:
            p = os.path.join(d, name)
            if os.path.exists(p):
                _fonts[key] = ImageFont.truetype(p, int(size * S))
                break
        else:
            _fonts[key] = ImageFont.load_default()
    return _fonts[key]


def chaikin(pts, n=2):
    for _ in range(n):
        out = []
        for i in range(len(pts)):
            p, q = pts[i], pts[(i + 1) % len(pts)]
            out.append((0.75 * p[0] + 0.25 * q[0], 0.75 * p[1] + 0.25 * q[1]))
            out.append((0.25 * p[0] + 0.75 * q[0], 0.25 * p[1] + 0.75 * q[1]))
        pts = out
    return pts


def ellipse_pts(cx, cy, rx, ry, rot=0.0, n=40, a0=0.0, a1=2 * math.pi):
    pts = []
    cr, sr = math.cos(rot), math.sin(rot)
    for i in range(n):
        a = a0 + (a1 - a0) * i / (n if a1 - a0 >= 2 * math.pi - 1e-6 else n - 1)
        x, y = rx * math.cos(a), ry * math.sin(a)
        pts.append((cx + x * cr - y * sr, cy + x * sr + y * cr))
    return pts


def star_pts(cx, cy, r1, r2, n=5, rot=-math.pi / 2):
    pts = []
    for i in range(2 * n):
        r = r1 if i % 2 == 0 else r2
        a = rot + math.pi * i / n
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def catmull(ctrl, steps=24):
    n = len(ctrl)
    out = []
    for i in range(n):
        p0, p1, p2, p3 = ctrl[(i - 1) % n], ctrl[i], ctrl[(i + 1) % n], ctrl[(i + 2) % n]
        for k in range(steps):
            t = k / steps
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * ((2 * p1[j]) + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t2
                                    + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t3) for j in range(2)))
    return out


class Canvas:
    def __init__(self, w, h, bg=WHITE):
        self.w, self.h = w, h
        if bg is None:  # a transparent sprite
            self.img = Image.new("RGBA", (w * S, h * S), (0, 0, 0, 0))
        else:
            self.img = Image.new("RGB", (w * S, h * S), rgb(bg)[:3])
        self.d = ImageDraw.Draw(self.img, "RGBA")

    # -- primitives (logical coordinates) --
    def P(self, pts):
        return [(x * S, y * S) for x, y in pts]

    def B(self, box):
        return [box[0] * S, box[1] * S, box[2] * S, box[3] * S]

    def poly(self, pts, fill=None, ol=INK, w=3.0):
        if fill is not None:
            self.d.polygon(self.P(pts), fill=rgb(fill))
        if ol is not None and w > 0:
            p = self.P(pts)
            self.d.line(p + [p[0], p[1]], fill=rgb(ol), width=max(1, int(w * S)), joint="curve")

    def line(self, pts, col=INK, w=3.0, closed=False):
        p = self.P(pts)
        if closed:
            p = p + [p[0], p[1]]
        self.d.line(p, fill=rgb(col), width=max(1, int(w * S)), joint="curve")
        if not closed and w >= 3:
            for q in (p[0], p[-1]):
                r = w * S / 2
                self.d.ellipse([q[0] - r, q[1] - r, q[0] + r, q[1] + r], fill=rgb(col))

    def circle(self, cx, cy, r, fill=None, ol=INK, w=3.0):
        self.ellipse(cx, cy, r, r, fill, ol, w)

    def ellipse(self, cx, cy, rx, ry, fill=None, ol=INK, w=3.0):
        self.d.ellipse(self.B((cx - rx, cy - ry, cx + rx, cy + ry)), fill=rgb(fill) if fill else None,
                       outline=rgb(ol) if (ol and w > 0) else None, width=max(1, int(w * S)) if ol else 0)

    def rrect(self, box, r, fill=None, ol=INK, w=3.0):
        self.d.rounded_rectangle(self.B(box), radius=r * S, fill=rgb(fill) if fill else None,
                                 outline=rgb(ol) if (ol and w > 0) else None, width=max(1, int(w * S)) if ol else 0)

    def text(self, x, y, s, size, fill=WHITE, stroke=INK, sw=None, anchor="mm", bold=True, italic=False):
        f = font(size, bold, italic)
        if sw is None:
            sw = max(1, size // 7) if stroke else 0
        self.d.text((x * S, y * S), s, font=f, fill=rgb(fill), anchor=anchor,
                    stroke_width=int(sw * S) if stroke else 0, stroke_fill=rgb(stroke) if stroke else None)

    def text_w(self, s, size, bold=True, italic=False):
        b = font(size, bold, italic).getbbox(s)
        return (b[2] - b[0]) / S

    def grad(self, box, c1, c2, r=0, horizontal=False, ol=None, w=3.0, stops=None):
        x0, y0, x1, y1 = [int(v * S) for v in box]
        gw, gh = max(1, x1 - x0), max(1, y1 - y0)
        g = Image.new("RGB", (gw, gh))
        gd = ImageDraw.Draw(g)
        n = gw if horizontal else gh
        cols = stops or [c1, c2]
        for i in range(n):
            t = i / max(1, n - 1) * (len(cols) - 1)
            k = min(int(t), len(cols) - 2)
            c = mix(cols[k], cols[k + 1], t - k)
            if horizontal:
                gd.line([(i, 0), (i, gh)], fill=c[:3])
            else:
                gd.line([(0, i), (gw, i)], fill=c[:3])
        m = Image.new("L", (gw, gh), 0)
        ImageDraw.Draw(m).rounded_rectangle([0, 0, gw - 1, gh - 1], radius=int(r * S), fill=255)
        self.img.paste(g, (x0, y0), m)
        if ol:
            self.rrect(box, r, None, ol, w)

    def shadow(self, draw_fn, off=(5, 7), blur=7, alpha=70):
        layer = Image.new("RGBA", self.img.size, (0, 0, 0, 0))
        ld = ImageDraw.Draw(layer, "RGBA")
        save = self.d
        self.d = ld
        draw_fn(self, off, rgb(INK, alpha))
        self.d = save
        layer = layer.filter(ImageFilter.GaussianBlur(blur * S))
        self.img.paste(layer, (0, 0), layer)

    def rr_shadow(self, box, r, off=(5, 7), blur=7, alpha=70):
        def f(c, o, col):
            c.d.rounded_rectangle(c.B((box[0] + o[0], box[1] + o[1], box[2] + o[0], box[3] + o[1])), radius=r * S, fill=col)
        self.shadow(f, off, blur, alpha)

    def poly_fill_pattern(self, pts, c1, c2, pattern, scale=1.0):
        """Fills a polygon with a paint pattern (DESIGN.md 4.4), clipped to its shape."""
        P = self.P(pts)
        xs, ys = [p[0] for p in P], [p[1] for p in P]
        x0, y0, x1, y1 = int(min(xs)) - 2, int(min(ys)) - 2, int(max(xs)) + 3, int(max(ys)) + 3
        w, h = x1 - x0, y1 - y0
        lay = Image.new("RGB", (w, h), rgb(c1)[:3])
        ld = ImageDraw.Draw(lay, "RGBA")
        u = max(6, int(14 * S * scale))
        if pattern == "stripes":
            ld.rectangle([0, int(h * 0.55), w, int(h * 0.75)], fill=rgb(c2))
            ld.rectangle([0, int(h * 0.80), w, int(h * 0.86)], fill=rgb(c2))
        elif pattern == "dots":
            for yy in range(0, h + u, u * 2):
                for xx in range((yy // (u * 2)) % 2 * u, w + u, u * 2):
                    ld.ellipse([xx - u * 0.45, yy - u * 0.45, xx + u * 0.45, yy + u * 0.45], fill=rgb(c2))
        elif pattern == "stars":
            for yy in range(u, h + u, int(u * 2.4)):
                for xx in range(((yy // u) % 2) * u + u // 2, w + u, int(u * 2.4)):
                    ld.polygon(star_pts(xx, yy, u * 0.7, u * 0.3), fill=rgb(c2))
        elif pattern == "zigzag":
            yy = int(h * 0.55)
            zz = []
            for i, xx in enumerate(range(-u, w + u * 2, u)):
                zz.append((xx, yy + (-u * 0.6 if i % 2 else u * 0.6)))
            ld.polygon(zz + [(w + u * 2, h), (-u, h)], fill=rgb(c2))
        elif pattern == "checker":
            for yy in range(0, h, u):
                for xx in range(0, w, u):
                    if ((xx // u) + (yy // u)) % 2:
                        ld.rectangle([xx, yy, xx + u - 1, yy + u - 1], fill=rgb(c2))
        elif pattern == "rainbow":
            bh = max(2, int(h * 0.08))
            for i, c in enumerate(RAINBOW):
                ld.rectangle([0, int(h * 0.45) + i * bh, w, int(h * 0.45) + (i + 1) * bh], fill=rgb(c))
        # a soft top light (the toon ramp's light band) and a darker belly
        ld.rectangle([0, 0, w, int(h * 0.22)], fill=(255, 255, 255, 60))
        ld.rectangle([0, int(h * 0.88), w, h], fill=rgb(INK, 40))
        m = Image.new("L", (w, h), 0)
        ImageDraw.Draw(m).polygon([(p[0] - x0, p[1] - y0) for p in P], fill=255)
        self.img.paste(lay, (x0, y0), m)

    def save(self, name):
        os.makedirs(OUT, exist_ok=True)
        im = self.img.resize((self.w, self.h), Image.LANCZOS).convert("RGB")
        p = os.path.join(OUT, name)
        im.save(p, optimize=True)
        print("wrote", os.path.relpath(p, ROOT))



# ---- the teams and pilots (DESIGN.md 5) ----------------------------------------------------------------
TEAMS = [
    # key, pilot, team EN, team FR, main, trim, accent, number, default (hull, wing, engine)
    ("ember", "Kai Moreno", "Ember", "Braise", RED, WHITE, YELLOW, 7, ("comet", "swept", "tether")),
    ("zenith", "Lumi Sato", "Zenith", "Zénith", ARCH, CYAN, MAGENTA, 1, ("needle", "ray", "central")),
    ("volt", "Aya Okafor", "Volt", "Volt", YELLOW, NAVY, CYAN, 22, ("pulse", "stub", "pods")),
    ("glacier", "Nils Berg", "Glacier", "Glacier", CYAN, WHITE, BLUE, 14, ("manta", "twin", "ring")),
    ("orchid", "Zara Haddad", "Orchid", "Orchidée", MAGENTA, WHITE, YELLOW, 9, ("needle", "swept", "tether")),
    ("verde", "Teo Rossi", "Verde", "Verde", LIME, NAVY, WHITE, 33, ("titan", "stub", "pods")),
    ("nebula", "Mika Lind", "Nebula", "Nébuleuse", VIOLET, PINK, WHITE, 18, ("manta", "swept", "central")),
    ("sunburst", "Ravi Kapoor", "Sunburst", "Soleil", ORANGE, YELLOW, NAVY, 42, ("bastion", "stub", "ring")),
]
TEAM = {t[0]: t for t in TEAMS}


def team_logo(c, key, x, y, r, col=WHITE):
    w = max(1.5, r * 0.12)
    if key == "ember":
        c.poly([(x, y - r), (x + 0.6 * r, y + 0.7 * r), (x, y + 0.3 * r), (x - 0.6 * r, y + 0.7 * r)], col, None, 0)
    elif key == "zenith":
        c.circle(x, y, 0.75 * r, None, col, w * 1.4)
        c.line([(x - r, y), (x + r, y)], col, w * 1.4)
    elif key == "volt":
        c.poly([(x + 0.15 * r, y - r), (x - 0.5 * r, y + 0.15 * r), (x - 0.02 * r, y + 0.15 * r), (x - 0.2 * r, y + r), (x + 0.5 * r, y - 0.2 * r), (x + 0.02 * r, y - 0.2 * r)], col, None, 0)
    elif key == "glacier":
        c.poly([(x + r * math.cos(a), y + r * math.sin(a)) for a in [i * math.pi / 3 + math.pi / 6 for i in range(6)]], None, col, w * 1.4)
        c.poly([(x + 0.4 * r * math.cos(a), y + 0.4 * r * math.sin(a)) for a in [i * math.pi / 3 + math.pi / 6 for i in range(6)]], col, None, 0)
    elif key == "orchid":
        for a in (0, math.pi / 2):
            c.poly([(x + r * math.cos(a), y + r * math.sin(a)), (x + 0.3 * r * math.cos(a + 1.57), y + 0.3 * r * math.sin(a + 1.57)),
                    (x - r * math.cos(a), y - r * math.sin(a)), (x - 0.3 * r * math.cos(a + 1.57), y - 0.3 * r * math.sin(a + 1.57))], col, None, 0)
    elif key == "verde":
        for k in range(2):
            c.line([(x - 0.8 * r, y - 0.5 * r + k * 0.6 * r), (x, y + 0.1 * r + k * 0.6 * r), (x + 0.8 * r, y - 0.5 * r + k * 0.6 * r)], col, w * 1.6)
    elif key == "nebula":
        for k in range(3):
            c.circle(x + (k - 1) * 0.15 * r, y, (1 - k * 0.3) * r, None, col, w)
    elif key == "sunburst":
        c.poly(star_pts(x, y, r, 0.45 * r, 8), col, None, 0)


def helmet(c, cx, cy, r, key, shoulders=True):
    """A pilot seen from the front: the helmet with a closed visor, the shoulders in the team's suit. No face."""
    t = TEAM[key]
    main, trim, acc = t[4], t[5], t[6]
    ow = max(1.5, r * 0.05)
    if shoulders:
        sh = [(cx - 1.55 * r, cy + 1.9 * r), (cx - 1.35 * r, cy + 1.05 * r), (cx - 0.5 * r, cy + 0.82 * r), (cx + 0.5 * r, cy + 0.82 * r),
              (cx + 1.35 * r, cy + 1.05 * r), (cx + 1.55 * r, cy + 1.9 * r)]
        c.poly(sh, main, NAVY, ow)
        c.poly([(cx - 0.35 * r, cy + 0.85 * r), (cx + 0.35 * r, cy + 0.85 * r), (cx + 0.2 * r, cy + 1.9 * r), (cx - 0.2 * r, cy + 1.9 * r)], trim, None, 0)
        c.poly([(cx - 1.4 * r, cy + 1.25 * r), (cx - 0.9 * r, cy + 1.05 * r), (cx - 0.85 * r, cy + 1.3 * r), (cx - 1.45 * r, cy + 1.5 * r)], acc, None, 0)
        team_logo(c, key, cx + 0.9 * r, cy + 1.45 * r, 0.22 * r, trim if trim != main else NAVY)
        c.rrect((cx - 0.45 * r, cy + 0.72 * r, cx + 0.45 * r, cy + 0.98 * r), 0.08 * r, STEEL, NAVY, ow)
    # the helmet: a slightly angular shell
    shell = chaikin([(cx - 0.95 * r, cy + 0.65 * r), (cx - 1.0 * r, cy - 0.2 * r), (cx - 0.75 * r, cy - 0.85 * r), (cx, cy - 1.08 * r),
                     (cx + 0.75 * r, cy - 0.85 * r), (cx + 1.0 * r, cy - 0.2 * r), (cx + 0.95 * r, cy + 0.65 * r), (cx, cy + 0.85 * r)], 1)
    c.poly(shell, main, NAVY, ow * 1.2)
    c.poly([(cx - 0.12 * r, cy - 1.06 * r), (cx + 0.12 * r, cy - 1.06 * r), (cx + 0.1 * r, cy - 0.45 * r), (cx - 0.1 * r, cy - 0.45 * r)], trim, None, 0)
    c.poly([(cx - 0.98 * r, cy - 0.1 * r), (cx - 0.55 * r, cy - 0.35 * r), (cx - 0.6 * r, cy + 0.5 * r), (cx - 0.92 * r, cy + 0.55 * r)], acc, None, 0)
    # the visor: dark, a wide angular band with a sky reflection
    visor = [(cx - 0.8 * r, cy - 0.32 * r), (cx + 0.8 * r, cy - 0.32 * r), (cx + 0.7 * r, cy + 0.3 * r), (cx + 0.25 * r, cy + 0.42 * r),
             (cx - 0.25 * r, cy + 0.42 * r), (cx - 0.7 * r, cy + 0.3 * r)]
    c.poly(visor, CANOPY, NAVY, ow)
    c.poly([(cx - 0.62 * r, cy - 0.24 * r), (cx + 0.1 * r, cy - 0.24 * r), (cx - 0.05 * r, cy - 0.12 * r), (cx - 0.66 * r, cy - 0.1 * r)], rgb(SKYLOW, 200), None, 0)
    c.poly([(cx + 0.25 * r, cy - 0.24 * r), (cx + 0.5 * r, cy - 0.24 * r), (cx + 0.4 * r, cy - 0.12 * r), (cx + 0.18 * r, cy - 0.12 * r)], rgb(WHITE, 160), None, 0)
    c.text(cx + 0.72 * r, cy - 0.55 * r, str(t[7]), max(7, int(r * 0.32)), trim if trim != main else NAVY, None, 0, italic=True)


# ---- the ships (DESIGN.md 4) ---------------------------------------------------------------------------
# metres: x forward (screen right in the side views), y up; z to the right in the top views.
HULLS = {
    "comet": dict(name="Nova Streak", fr="Nova Streak", file="hull_comet", stats=(3, 3, 3, 3), hw=0.8,
                  side=[(-1.7, 0.25), (-1.75, 0.7), (-0.9, 0.82), (-0.2, 0.82), (0.5, 0.62), (1.75, 0.32), (1.6, 0.22), (-0.2, 0.18)],
                  canopy=[(-0.75, 0.8), (-0.2, 1.0), (0.45, 0.66), (-0.1, 0.7)],
                  top=[(-1.7, -0.55), (-0.6, -0.8), (0.6, -0.55), (1.8, -0.06), (1.8, 0.06), (0.6, 0.55), (-0.6, 0.8), (-1.7, 0.55)]),
    "needle": dict(name="Comet Chaser", fr="Chasseur de Comètes", file="hull_needle", stats=(5, 2, 3, 2), hw=0.65,
                   side=[(-2.0, 0.25), (-2.0, 0.62), (-1.2, 0.72), (-0.3, 0.62), (2.15, 0.32), (2.05, 0.24), (-0.5, 0.18)],
                   canopy=[(-1.45, 0.7), (-0.95, 0.9), (-0.35, 0.62), (-0.9, 0.64)],
                   top=[(-2.0, -0.45), (-1.0, -0.65), (0.2, -0.35), (2.2, -0.04), (2.2, 0.04), (0.2, 0.35), (-1.0, 0.65), (-2.0, 0.45)]),
    "titan": dict(name="Star Drifter", fr="Dériveur Stellaire", file="hull_whale", stats=(4, 2, 2, 5), hw=1.05,
                  side=[(-1.8, 0.22), (-1.85, 0.95), (-0.4, 1.02), (0.6, 0.92), (1.85, 0.55), (1.85, 0.22)],
                  canopy=[(-0.5, 1.0), (0.0, 1.2), (0.6, 0.92), (0.05, 0.95)],
                  top=[(-1.8, -0.95), (1.2, -1.05), (1.9, -0.7), (1.9, 0.7), (1.2, 1.05), (-1.8, 0.95)]),
    "manta": dict(name="Manta Ray", fr="Raie Manta", file="hull_manta", stats=(2, 3, 5, 1), hw=1.2,
                  side=[(-1.7, 0.28), (-1.7, 0.56), (-0.4, 0.62), (1.75, 0.36), (1.65, 0.26), (0.0, 0.22)],
                  canopy=[(-0.65, 0.6), (-0.25, 0.78), (0.35, 0.5), (-0.2, 0.55)],
                  top=[(-1.6, -0.5), (-0.6, -1.2), (0.6, -0.9), (1.8, 0.0), (0.6, 0.9), (-0.6, 1.2), (-1.6, 0.5)]),
    "pulse": dict(name="Plasma Runner", fr="Coureur Plasma", file="hull_pod", stats=(3, 5, 2, 2), hw=0.85,
                  side=[(-1.5, 0.22), (-1.6, 0.95), (-0.6, 0.95), (0.2, 0.75), (1.55, 0.4), (1.5, 0.22)],
                  canopy=[(-0.25, 0.86), (0.2, 1.02), (0.72, 0.62), (0.2, 0.7)],
                  top=[(-1.5, -0.85), (-0.5, -0.85), (0.6, -0.5), (1.6, -0.1), (1.6, 0.1), (0.6, 0.5), (-0.5, 0.85), (-1.5, 0.85)]),
    "bastion": dict(name="Ringmaster", fr="Maître de l'Anneau", file="hull_cube", stats=(4, 3, 1, 5), hw=0.95,
                    side=[(-1.8, 0.22), (-1.8, 0.88), (-0.9, 0.98), (0.7, 0.95), (1.85, 0.48), (1.85, 0.22)],
                    canopy=[(-0.35, 0.96), (0.1, 1.14), (0.7, 0.94), (0.1, 0.96)],
                    top=[(-1.8, -0.9), (-0.9, -0.98), (1.0, -0.85), (1.9, -0.35), (1.9, 0.35), (1.0, 0.85), (-0.9, 0.98), (-1.8, 0.9)]),
}
HULL_ORDER = ["comet", "needle", "titan", "manta", "pulse", "bastion"]
WINGS = {
    "stub": dict(name="Stub Fins", fr="Ailerons courts", mod="+1 Accel   -1 Handling"),
    "ray": dict(name="Ray Wings", fr="Ailes Rayon", mod="+1 Speed   -1 Accel"),
    "twin": dict(name="Twin Blades", fr="Doubles lames", mod="+1 Handling   -1 Speed"),
    "swept": dict(name="Swept Fins", fr="Ailes en flèche", mod="Faster drift charge"),
}
WING_ORDER = ["stub", "ray", "twin", "swept"]
ENGINES = {
    "central": dict(name="Central Jet", fr="Réacteur central", file="engine_ion", mod="Balanced"),
    "pods": dict(name="Side Pods", fr="Nacelles latérales", file="engine_twin", mod="+1 Accel   -1 Speed"),
    "ring": dict(name="Ring Drive", fr="Anneau de poussée", file="engine_ring", mod="+1 Handling   -1 Weight"),
    "tether": dict(name="Tether Pods", fr="Nacelles captives", file="engine_star", mod="+1 Speed   -1 Handling"),
}
ENGINE_ORDER = ["central", "pods", "ring", "tether"]


def jet(c, x, y, sc, length=1.2, dirx=-1, cols=(CYAN, "#B9F6FF", WHITE), h=0.22):
    """An angular exhaust flame, pointing along dirx."""
    for k, col in enumerate(cols):
        L = length * (1 - k * 0.3)
        hh = h * (1 - k * 0.3)
        c.poly([(x, y - hh * sc), (x + dirx * L * sc, y), (x, y + hh * sc)], rgb(col, 230), None, 0)


def fill_livery(c, pts, col1, col2, acc, pattern="stripe", scale=1.0):
    """The flat colour blocks of a livery, clipped to a polygon (screen points)."""
    P = c.P(pts)
    xs, ys = [p[0] for p in P], [p[1] for p in P]
    x0, y0, x1, y1 = int(min(xs)) - 2, int(min(ys)) - 2, int(max(xs)) + 3, int(max(ys)) + 3
    w, h = x1 - x0, y1 - y0
    lay = Image.new("RGB", (w, h), rgb(col1)[:3])
    ld = ImageDraw.Draw(lay, "RGBA")
    # the white belly block (always: the colour-block look)
    ld.polygon([(0, h * 0.68), (w, h * 0.58), (w, h), (0, h)], fill=rgb(ARCH if col1 != ARCH else LGREY))
    if pattern == "stripe":
        ld.polygon([(w * 0.25, 0), (w * 0.42, 0), (w * 0.3, h), (w * 0.13, h)], fill=rgb(col2))
        ld.polygon([(w * 0.45, 0), (w * 0.5, 0), (w * 0.38, h), (w * 0.33, h)], fill=rgb(acc))
    elif pattern == "split":
        ld.polygon([(w * 0.55, 0), (w, 0), (w, h), (w * 0.45, h)], fill=rgb(col2))
    elif pattern == "chevrons":
        for k in range(3):
            x = w * (0.5 + k * 0.12)
            ld.polygon([(x, 0), (x + w * 0.06, 0), (x + w * 0.12, h * 0.5), (x + w * 0.06, h), (x, h), (x + w * 0.06, h * 0.5)], fill=rgb(col2 if k != 1 else acc))
    elif pattern == "checker":
        u = max(4, int(h * 0.18))
        for yy in range(0, h, u):
            for xx in range(int(w * 0.3), int(w * 0.5), u):
                if ((xx // u) + (yy // u)) % 2:
                    ld.rectangle([xx, yy, xx + u - 1, yy + u - 1], fill=rgb(col2))
    elif pattern == "fade":
        for i in range(w):
            t = i / w
            if t > 0.4:
                ld.line([(i, 0), (i, h * 0.62)], fill=mix(col1, col2, min(1, (t - 0.4) * 2)))
    ld.polygon([(0, h * 0.62), (w, h * 0.52), (w, h * 0.56), (0, h * 0.66)], fill=rgb(acc))  # the accent line
    ld.rectangle([0, 0, w, int(h * 0.12)], fill=(255, 255, 255, 50))  # the cel light band
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).polygon([(p[0] - x0, p[1] - y0) for p in P], fill=255)
    c.img.paste(lay, (x0, y0), m)


def ship_side(c, cx, cy, sc, hull, wing, eng, col1, col2, acc, num=7, pattern="stripe", fl=True, hover=0.35, glow=True):
    """A ship from its left side, the nose to the right; (cx, cy) = the ground under its centre."""
    H = HULLS[hull]
    X = lambda x: cx + x * sc
    Y = lambda y: cy - (y + hover) * sc
    ow = max(1.2, sc * 0.022)
    side = H["side"]
    xmin = min(p[0] for p in side)
    xmax = max(p[0] for p in side)
    if glow:
        c.ellipse(cx, cy + 0.06 * sc, 1.7 * sc, 0.12 * sc, rgb(NAVY, 40), None, 0)
        for k in range(3):
            c.ellipse(cx, Y(0.1) + k * 0.04 * sc, (1.4 - k * 0.2) * sc, (0.11 - k * 0.025) * sc, rgb(CYAN, 60 + k * 50), None, 0)
    # the far tether pod and its tethers (behind the hull)
    if eng == "tether":
        pod(c, X, Y, sc, 1.25, 0.62, col1, acc, ow, fl, far=True)
        tether(c, [(X(0.6), Y(0.55)), (X(1.0), Y(0.62))], sc)
    # the far wing
    wing_side(c, X, Y, sc, wing, -0.45, 0.5 + 0.08, dark(col2, 0.15), ow)
    # the engine behind the hull
    if eng == "central":
        if fl:
            jet(c, X(xmin - 0.55), Y(0.52), sc, 1.4)
        c.poly([(X(xmin + 0.1), Y(0.32)), (X(xmin - 0.55), Y(0.38)), (X(xmin - 0.55), Y(0.68)), (X(xmin + 0.1), Y(0.76))], STEEL, NAVY, ow)
        c.poly([(X(xmin - 0.5), Y(0.4)), (X(xmin - 0.6), Y(0.4)), (X(xmin - 0.6), Y(0.64)), (X(xmin - 0.5), Y(0.64))], CYAN, None, 0)
    elif eng == "ring":
        if fl:
            jet(c, X(xmin - 0.5), Y(0.52), sc, 1.0, h=0.3)
        c.poly([(X(xmin + 0.1), Y(0.42)), (X(xmin - 0.35), Y(0.45)), (X(xmin - 0.35), Y(0.6)), (X(xmin + 0.1), Y(0.62))], STEEL, NAVY, ow)
        c.ellipse(X(xmin - 0.38), Y(0.52), 0.16 * sc, 0.55 * sc, col2, NAVY, ow)
        c.ellipse(X(xmin - 0.38), Y(0.52), 0.07 * sc, 0.4 * sc, CYAN, None, 0)
    # the tail fin
    yb = max(p[1] for p in side if p[0] < xmin + 0.7)
    fin = [(X(xmin + 0.95), Y(yb - 0.02)), (X(xmin + 0.15), Y(yb + 0.55)), (X(xmin - 0.12), Y(yb + 0.55)), (X(xmin + 0.1), Y(yb - 0.05))]
    c.poly(fin, col2, NAVY, ow)
    c.poly([(X(xmin + 0.3), Y(yb + 0.35)), (X(xmin + 0.12), Y(yb + 0.5)), (X(xmin - 0.05), Y(yb + 0.5)), (X(xmin + 0.1), Y(yb + 0.35))], acc, None, 0)
    # the hull with its livery
    pts = [(X(x), Y(y)) for x, y in side]
    fill_livery(c, pts, col1, col2, acc, pattern)
    c.poly(pts, None, NAVY, ow * 1.2)
    # facet lines (crisp panels)
    c.line([(X(xmin + 0.2), Y(0.45)), (X(xmax - 0.6), Y(0.4))], rgb(NAVY, 90), ow * 0.8)
    c.line([(X(0.2), Y(0.25)), (X(0.0), Y(0.55))], rgb(NAVY, 70), ow * 0.8)
    # the canopy
    cp = [(X(x), Y(y)) for x, y in H["canopy"]]
    c.poly(cp, CANOPY, NAVY, ow)
    c.line([cp[0], (cp[0][0] + (cp[1][0] - cp[0][0]) * 0.9, cp[0][1] + (cp[1][1] - cp[0][1]) * 0.9)], rgb(SKYLOW, 220), max(1, ow * 1.3))
    # the number
    nx = xmin + 0.55
    c.poly([(X(nx - 0.3), Y(0.62)), (X(nx + 0.32), Y(0.62)), (X(nx + 0.26), Y(0.32)), (X(nx - 0.36), Y(0.32))], WHITE, NAVY, ow * 0.8)
    c.text(X(nx - 0.02), Y(0.47), str(num), max(6, int(sc * 0.24)), NAVY, None, 0, italic=True)
    # the hover pads
    for px in (-0.9, 0.7):
        c.poly([(X(px - 0.35), Y(0.24)), (X(px + 0.35), Y(0.24)), (X(px + 0.28), Y(0.12)), (X(px - 0.28), Y(0.12))], STEEL, NAVY, ow * 0.8)
        c.poly([(X(px - 0.25), Y(0.13)), (X(px + 0.25), Y(0.13)), (X(px + 0.22), Y(0.09)), (X(px - 0.22), Y(0.09))], CYAN, None, 0)
    # the near wing, the side pods, the near tether pod
    wing_side(c, X, Y, sc, wing, -0.45, 0.45, col2, ow)
    if eng == "pods":
        pp = [(X(xmin - 0.25), Y(0.2)), (X(0.9), Y(0.22)), (X(1.25), Y(0.36)), (X(0.9), Y(0.5)), (X(xmin - 0.25), Y(0.52))]
        if fl:
            jet(c, X(xmin - 0.3), Y(0.36), sc, 1.1, h=0.16)
        c.poly(pp, col2, NAVY, ow)
        c.poly([(X(0.9), Y(0.24)), (X(1.2), Y(0.36)), (X(0.9), Y(0.48))], NAVY, None, 0)
        c.poly([(X(xmin - 0.25), Y(0.24)), (X(xmin - 0.32), Y(0.27)), (X(xmin - 0.32), Y(0.45)), (X(xmin - 0.25), Y(0.48))], CYAN, None, 0)
        c.line([(X(xmin), Y(0.44)), (X(0.8), Y(0.44))], acc, max(1.5, sc * 0.05))
    if eng == "tether":
        tether(c, [(X(0.5), Y(0.35)), (X(0.95), Y(0.2))], sc)
        pod(c, X, Y, sc, 1.3, 0.12, col1, acc, ow, fl)


def pod(c, X, Y, sc, px, py, col, acc, ow, fl=True, far=False):
    """A detached engine pod (Tether Pods): 2.2 m long, centred at (px, py)."""
    k = 0.85 if far else 1.0
    pts = [(X(px - 1.0 * k), Y(py - 0.12)), (X(px + 0.5), Y(py - 0.14)), (X(px + 0.95), Y(py)), (X(px + 0.5), Y(py + 0.15)), (X(px - 1.0 * k), Y(py + 0.15))]
    if fl:
        jet(c, X(px - 1.12 * k), Y(py), sc, 1.0 * k, h=0.15)
    c.poly(pts, dark(col, 0.15) if far else col, NAVY, ow)
    c.poly([(X(px + 0.6), Y(py - 0.12)), (X(px + 0.95), Y(py)), (X(px + 0.6), Y(py + 0.12))], NAVY, None, 0)
    c.line([(X(px - 0.9), Y(py + 0.08)), (X(px + 0.4), Y(py + 0.08))], acc, max(1.2, sc * 0.04))
    c.poly([(X(px - 1.1 * k), Y(py - 0.14)), (X(px - 1.18 * k), Y(py - 0.1)), (X(px - 1.18 * k), Y(py + 0.12)), (X(px - 1.1 * k), Y(py + 0.17))], CYAN, None, 0)


def tether(c, pts, sc):
    # drawn as a pink lightning arc (the Plasma Runner's look)
    (ax, ay), (bx, by) = pts[0], pts[-1]
    zz = [(ax + (bx - ax) * t + (5 if i % 2 else -5) * (0 < t < 1), ay + (by - ay) * t + (4 if i % 2 else -4) * (0 < t < 1)) for i, t in enumerate([0, 0.25, 0.5, 0.75, 1])]
    c.line(zz, rgb(HOTPINK, 150), max(3, sc * 0.1))
    c.line(zz, rgb(WHITE, 230), max(1, sc * 0.02))
    return
    c.line(pts, rgb(CYAN, 120), max(3, sc * 0.12))
    c.line(pts, rgb(MAGENTA, 255), max(1.5, sc * 0.04))
    c.line(pts, rgb(WHITE, 220), max(1, sc * 0.015))


def wing_side(c, X, Y, sc, wing, wx, wy, col, ow):
    if wing == "stub":
        pts = [(wx + 0.35, wy), (wx - 0.45, wy + 0.03), (wx - 0.75, wy - 0.16), (wx - 0.1, wy - 0.14)]
    elif wing == "ray":
        pts = [(wx + 0.45, wy + 0.02), (wx - 1.3, wy + 0.06), (wx - 1.45, wy - 0.04), (wx + 0.35, wy - 0.08)]
    elif wing == "twin":
        for dy in (0.12, -0.1):
            p = [(wx + 0.4, wy + dy), (wx - 0.75, wy + dy + 0.08), (wx - 0.95, wy + dy - 0.02), (wx + 0.25, wy + dy - 0.06)]
            c.poly([(X(x), Y(y)) for x, y in p], col, NAVY, ow)
        return
    else:
        pts = [(wx + 0.4, wy), (wx - 0.4, wy + 0.06), (wx - 1.05, wy + 0.5), (wx - 1.25, wy + 0.46), (wx - 0.85, wy - 0.08)]
    c.poly([(X(x), Y(y)) for x, y in pts], col, NAVY, ow)
    if wing == "ray":
        c.circle(X(wx - 1.38), Y(wy), 0.06 * sc, CYAN, None, 0)


def ship_top(c, cx, cy, sc, hull, wing, eng, col1, col2, acc, num=7, pattern="stripe", fl=True):
    """A ship from above, the nose to the right."""
    H = HULLS[hull]
    X = lambda x: cx + x * sc
    Z = lambda z: cy + z * sc
    ow = max(1.2, sc * 0.022)
    top = H["top"]
    xmin = min(p[0] for p in top)
    hw = H["hw"]
    for s in (-1, 1):
        if wing == "stub":
            w = [(0.0, hw * 0.8), (-0.4, hw + 0.6), (-0.85, hw + 0.6), (-0.8, hw * 0.8)]
        elif wing == "ray":
            w = [(0.1, hw * 0.8), (-0.15, hw + 1.4), (-0.55, hw + 1.4), (-0.6, hw * 0.8)]
        elif wing == "twin":
            for off in (0.15, -0.65):
                w = [(off + 0.2, hw * 0.8), (off - 0.25, hw + 1.0), (off - 0.5, hw + 1.0), (off - 0.3, hw * 0.8)]
                c.poly([(X(x), Z(s * z)) for x, z in w], col2, NAVY, ow)
            continue
        else:
            w = [(0.2, hw * 0.8), (-0.8, hw + 1.05), (-1.15, hw + 1.05), (-0.85, hw * 0.8)]
        c.poly([(X(x), Z(s * z)) for x, z in w], col2, NAVY, ow)
        if wing == "ray":
            c.circle(X(-0.35), Z(s * (hw + 1.35)), 0.07 * sc, CYAN, None, 0)
    if eng == "central":
        if fl:
            jet(c, X(xmin - 0.6), cy, sc, 1.3, h=0.25)
        c.poly([(X(xmin + 0.1), Z(-0.32)), (X(xmin - 0.6), Z(-0.25)), (X(xmin - 0.6), Z(0.25)), (X(xmin + 0.1), Z(0.32))], STEEL, NAVY, ow)
    elif eng == "ring":
        if fl:
            jet(c, X(xmin - 0.5), cy, sc, 0.9, h=0.3)
        c.poly([(X(xmin - 0.25), Z(-0.6)), (X(xmin - 0.5), Z(-0.6)), (X(xmin - 0.5), Z(0.6)), (X(xmin - 0.25), Z(0.6))], col2, NAVY, ow)
        c.poly([(X(xmin - 0.33), Z(-0.45)), (X(xmin - 0.42), Z(-0.45)), (X(xmin - 0.42), Z(0.45)), (X(xmin - 0.33), Z(0.45))], CYAN, None, 0)
    elif eng == "pods":
        for s in (-1, 1):
            z0 = s * (hw + 0.05)
            if fl:
                jet(c, X(xmin - 0.3), Z(z0), sc, 1.0, h=0.15)
            c.poly([(X(xmin - 0.25), Z(z0 - 0.22)), (X(0.9), Z(z0 - 0.2)), (X(1.25), Z(z0)), (X(0.9), Z(z0 + 0.2)), (X(xmin - 0.25), Z(z0 + 0.22))], col2, NAVY, ow)
    elif eng == "tether":
        for s in (-1, 1):
            pz = s * 1.6
            tether(c, [(X(0.6), Z(s * hw * 0.6)), (X(0.9), Z(pz))], sc)
            tether(c, [(X(-0.4), Z(s * hw * 0.7)), (X(0.4), Z(pz))], sc)
            if fl:
                jet(c, X(0.1), Z(pz), sc, 1.0, h=0.15)
            c.poly([(X(0.1), Z(pz - 0.16)), (X(1.9), Z(pz - 0.18)), (X(2.35), Z(pz)), (X(1.9), Z(pz + 0.18)), (X(0.1), Z(pz + 0.16))], col1, NAVY, ow)
            c.line([(X(0.3), Z(pz)), (X(1.8), Z(pz))], acc, max(1.2, sc * 0.05))
    pts = [(X(x), Z(z)) for x, z in top]
    fill_livery(c, pts, col1, col2, acc, pattern)
    c.poly(pts, None, NAVY, ow * 1.2)
    kx = H["canopy"][1][0]
    c.poly([(X(kx - 0.55), Z(-0.22)), (X(kx + 0.5), Z(-0.12)), (X(kx + 0.5), Z(0.12)), (X(kx - 0.55), Z(0.22))], CANOPY, NAVY, ow)
    c.line([(X(kx - 0.4), Z(-0.12)), (X(kx + 0.35), Z(-0.06))], rgb(SKYLOW, 200), max(1, ow))


def ship_back(c, cx, cy, sc, hull, wing, eng, col1, col2, acc, boost=0.5):
    """A ship from behind (the race view); (cx, cy) = the ground under it."""
    hw = HULLS[hull]["hw"] * 1.05
    ow = max(1.2, sc * 0.03)
    y0 = cy - 0.3 * sc
    c.ellipse(cx, cy, 1.7 * sc, 0.2 * sc, rgb(NAVY, 50), None, 0)
    c.ellipse(cx, y0 + 0.05 * sc, 1.3 * sc, 0.14 * sc, rgb(CYAN, 150), None, 0)
    if eng == "tether":
        for s in (-1, 1):
            px = cx + s * 1.6 * sc * 0.85
            py = y0 - 0.55 * sc
            tether(c, [(cx + s * hw * 0.6 * sc, y0 - 0.45 * sc), (px, py)], sc)
            c.poly([(px - 0.24 * sc, py - 0.22 * sc), (px + 0.24 * sc, py - 0.22 * sc), (px + 0.3 * sc, py + 0.05 * sc), (px, py + 0.2 * sc), (px - 0.3 * sc, py + 0.05 * sc)], col1, NAVY, ow)
            c.circle(px, py - 0.02 * sc, 0.15 * sc, CYAN, NAVY, ow * 0.7)
            c.circle(px, py - 0.02 * sc, 0.07 * sc, WHITE, None, 0)
    # the wings
    span = {"stub": 0.6, "ray": 1.45, "twin": 1.0, "swept": 1.1}[wing]
    for s in (-1, 1):
        lift = 0.25 if wing == "swept" else 0.0
        c.poly([(cx + s * (hw - 0.2) * sc, y0 - 0.32 * sc), (cx + s * (hw + span) * sc, y0 - (0.42 + lift) * sc),
                (cx + s * (hw + span) * sc, y0 - (0.5 + lift) * sc), (cx + s * (hw - 0.2) * sc, y0 - 0.5 * sc)], col2, NAVY, ow)
        if wing == "twin":
            c.poly([(cx + s * (hw - 0.2) * sc, y0 - 0.6 * sc), (cx + s * (hw + span * 0.8) * sc, y0 - 0.75 * sc),
                    (cx + s * (hw + span * 0.8) * sc, y0 - 0.82 * sc), (cx + s * (hw - 0.2) * sc, y0 - 0.72 * sc)], col2, NAVY, ow)
    # the hull's back: an angular wedge
    pts = [(cx - hw * sc, y0), (cx - (hw + 0.08) * sc, y0 - 0.38 * sc), (cx - hw * 0.55 * sc, y0 - 0.7 * sc),
           (cx + hw * 0.55 * sc, y0 - 0.7 * sc), (cx + (hw + 0.08) * sc, y0 - 0.38 * sc), (cx + hw * sc, y0)]
    fill_livery(c, pts, col1, col2, acc, "plain")
    c.poly(pts, None, NAVY, ow)
    c.poly([(cx - 0.35 * sc, y0 - 0.7 * sc), (cx + 0.35 * sc, y0 - 0.7 * sc), (cx + 0.22 * sc, y0 - 0.9 * sc), (cx - 0.22 * sc, y0 - 0.9 * sc)], CANOPY, NAVY, ow)
    c.poly([(cx - 0.05 * sc, y0 - 0.7 * sc), (cx + 0.05 * sc, y0 - 0.7 * sc), (cx + 0.03 * sc, y0 - 1.25 * sc), (cx - 0.03 * sc, y0 - 1.25 * sc)], col2, NAVY, ow * 0.8)
    g = 0.22 + 0.12 * boost
    if eng in ("central", "tether"):
        c.circle(cx, y0 - 0.35 * sc, g * 1.6 * sc, rgb(CYAN, 70), None, 0)
        c.poly([(cx - g * sc, y0 - 0.2 * sc), (cx + g * sc, y0 - 0.2 * sc), (cx + g * 0.8 * sc, y0 - 0.52 * sc), (cx - g * 0.8 * sc, y0 - 0.52 * sc)], CYAN, NAVY, ow)
        c.poly([(cx - g * 0.5 * sc, y0 - 0.27 * sc), (cx + g * 0.5 * sc, y0 - 0.27 * sc), (cx + g * 0.4 * sc, y0 - 0.45 * sc), (cx - g * 0.4 * sc, y0 - 0.45 * sc)], WHITE, None, 0)
    elif eng == "ring":
        c.circle(cx, y0 - 0.38 * sc, 0.55 * sc, None, NAVY, 0.18 * sc)
        c.circle(cx, y0 - 0.38 * sc, 0.55 * sc, None, col2, 0.12 * sc)
        c.circle(cx, y0 - 0.38 * sc, 0.42 * sc, rgb(CYAN, 160), None, 0)
        c.circle(cx, y0 - 0.38 * sc, 0.15 * sc, WHITE, None, 0)
    elif eng == "pods":
        for s in (-1, 1):
            px = cx + s * (hw + 0.1) * sc
            c.poly([(px - 0.25 * sc, y0 - 0.05 * sc), (px + 0.25 * sc, y0 - 0.05 * sc), (px + 0.25 * sc, y0 - 0.45 * sc), (px - 0.25 * sc, y0 - 0.45 * sc)], col2, NAVY, ow)
            c.circle(px, y0 - 0.25 * sc, 0.15 * sc, CYAN, NAVY, ow * 0.7)
            c.circle(px, y0 - 0.25 * sc, 0.07 * sc, WHITE, None, 0)


def ship_for(key):
    t = TEAM[key]
    h, w, e = t[8]
    return dict(hull=h, wing=w, eng=e, col1=t[4], col2=t[5], acc=t[6], num=t[7])


# ---- the gadgets (DESIGN.md 6) ---------------------------------------------------------------------------
GADGETS = [
    ("boost", "Hyper Boost", "Hyper-Boost", "helps", "+50% speed, 1.5 s"),
    ("triple", "Triple Hyper", "Triple Boost", "helps", "three boosts"),
    ("shield", "Shield Dome", "Bouclier", "helps", "blocks one hit, 8 s"),
    ("bubble", "Plasma Bubble", "Bulle Plasma", "hinders", "bubbles a ship 1.2 s"),
    ("goo", "Ion Goo", "Gelée ionique", "hinders", "a slippery gel behind"),
    ("well", "Gravity Well", "Puits de gravité", "hinders", "pulls and slows, 6 s"),
    ("tractor", "Tractor Beam", "Rayon tracteur", "catch-up", "pulls you to the one ahead"),
    ("emp", "EMP Burst", "Impulsion IEM", "hinders", "jams those behind"),
    ("storm", "Ion Storm", "Orage ionique", "hits leader", "a storm over the leader"),
    ("warp", "Warp Star", "Étoile Warp", "catch-up", "autopilot, 5 s"),
]


def chevron(c, x, y, s, col, w=None):
    c.poly([(x - 0.5 * s, y - 0.6 * s), (x, y - 0.6 * s), (x + 0.55 * s, y), (x, y + 0.6 * s), (x - 0.5 * s, y + 0.6 * s), (x + 0.05 * s, y)], col, NAVY, w or max(1.5, s * 0.08))


def gadget_icon(c, kind, x, y, r):
    ow = max(1.5, r * 0.05)
    if kind == "boost":
        jet(c, x - 0.3 * r, y, r, 0.7, h=0.3)
        chevron(c, x + 0.05 * r, y, 0.55 * r, YELLOW)
        chevron(c, x + 0.5 * r, y, 0.55 * r, ORANGE)
    elif kind == "triple":
        for k in range(3):
            chevron(c, x - 0.55 * r + k * 0.5 * r, y, 0.6 * r, (YELLOW, ORANGE, MAGENTA)[k])
    elif kind == "shield":
        hexa = [(x + 0.85 * r * math.cos(a), y + 0.85 * r * math.sin(a)) for a in [i * math.pi / 3 for i in range(6)]]
        c.poly(hexa, rgb(CYAN, 110), NAVY, ow * 1.5)
        for a in [i * math.pi / 3 for i in range(6)]:
            c.line([(x, y), (x + 0.85 * r * math.cos(a), y + 0.85 * r * math.sin(a))], rgb(WHITE, 160), ow)
        c.poly([(x + 0.4 * r * math.cos(a), y + 0.4 * r * math.sin(a)) for a in [i * math.pi / 3 for i in range(6)]], rgb(WHITE, 120), CYAN, ow)
    elif kind == "bubble":
        for k in range(3):
            c.line([(x - 1.0 * r, y + (k - 1) * 0.25 * r), (x - 0.65 * r, y + (k - 1) * 0.25 * r)], MAGENTA, ow * 1.5)
        c.circle(x + 0.1 * r, y, 0.6 * r, rgb(MAGENTA, 150), NAVY, ow * 1.3)
        c.poly([(x - 0.25 * r, y - 0.3 * r), (x + 0.0 * r, y - 0.42 * r), (x + 0.05 * r, y - 0.32 * r), (x - 0.2 * r, y - 0.2 * r)], WHITE, None, 0)
    elif kind == "goo":
        blob = [(x - 0.85 * r, y + 0.4 * r), (x - 0.6 * r, y), (x - 0.3 * r, y + 0.1 * r), (x - 0.1 * r, y - 0.45 * r), (x + 0.2 * r, y - 0.05 * r),
                (x + 0.5 * r, y - 0.25 * r), (x + 0.85 * r, y + 0.4 * r)]
        c.poly(chaikin(blob, 2), LIME, NAVY, ow * 1.3)
        for (bx, by, br) in ((-0.35, 0.22, 0.1), (0.3, 0.2, 0.08), (0.0, -0.05, 0.07)):
            c.circle(x + bx * r, y + by * r, br * r, rgb(WHITE, 200), None, 0)
    elif kind == "well":
        c.circle(x, y, 0.85 * r, VIOLET, NAVY, ow * 1.3)
        for k in range(3):
            pts = [(x + r * 0.8 * (1 - i / 40) * math.cos(i * 0.3 + k * 2.09), y + r * 0.8 * (1 - i / 40) * math.sin(i * 0.3 + k * 2.09)) for i in range(40)]
            c.line(pts, (CYAN, MAGENTA, WHITE)[k], ow * 1.5)
    elif kind == "tractor":
        c.poly([(x - 0.75 * r, y + 0.5 * r), (x + 0.85 * r, y - 0.35 * r), (x + 0.85 * r, y + 0.55 * r)], rgb(CYAN, 110), None, 0)
        for k in range(3):
            c.line([(x - 0.4 * r + k * 0.4 * r, y + (0.32 - k * 0.22) * r), (x - 0.4 * r + k * 0.4 * r, y + (0.55 - k * 0.0) * r)], WHITE, ow)
        c.poly([(x - 0.95 * r, y + 0.25 * r), (x - 0.55 * r, y + 0.25 * r), (x - 0.5 * r, y + 0.75 * r), (x - 1.0 * r, y + 0.75 * r)], STEEL, NAVY, ow)
        c.circle(x - 0.75 * r, y + 0.5 * r, 0.14 * r, CYAN, None, 0)
        c.poly([(x + 0.55 * r, y - 0.5 * r), (x + 1.0 * r, y - 0.3 * r), (x + 0.55 * r, y - 0.1 * r)], MAGENTA, NAVY, ow)
    elif kind == "emp":
        c.circle(x, y, 0.85 * r, None, CYAN, ow * 2)
        c.circle(x, y, 0.55 * r, None, rgb(CYAN, 150), ow * 1.2)
        for k in range(6):
            a = k * math.pi / 3 + 0.3
            p = [(x + 0.3 * r * math.cos(a), y + 0.3 * r * math.sin(a)), (x + 0.6 * r * math.cos(a + 0.2), y + 0.6 * r * math.sin(a + 0.2)),
                 (x + 0.55 * r * math.cos(a - 0.1), y + 0.55 * r * math.sin(a - 0.1)), (x + 0.95 * r * math.cos(a), y + 0.95 * r * math.sin(a))]
            c.line(p, YELLOW, ow * 1.3)
        c.circle(x, y, 0.2 * r, WHITE, NAVY, ow)
    elif kind == "storm":
        for k in range(4):
            c.line([(x + (-0.45 + k * 0.3) * r, y + 0.35 * r), (x + (-0.55 + k * 0.3) * r, y + 0.65 * r)], CYAN, ow)
        blobs = [(-0.5, 0.1, 0.35), (0.5, 0.1, 0.35), (-0.15, -0.2, 0.45), (0.25, -0.25, 0.4), (0, 0.12, 0.4)]
        for bx, by, br in blobs:
            c.circle(x + bx * r, y + by * r, br * r + ow * 1.2, NAVY, None, 0)
        for bx, by, br in blobs:
            c.circle(x + bx * r, y + by * r, br * r, "#3A4F8F", None, 0)
        c.poly([(x + 0.05 * r, y - 0.1 * r), (x - 0.2 * r, y + 0.35 * r), (x + 0.0 * r, y + 0.35 * r), (x - 0.15 * r, y + 0.85 * r), (x + 0.3 * r, y + 0.2 * r), (x + 0.1 * r, y + 0.2 * r), (x + 0.25 * r, y - 0.1 * r)], YELLOW, NAVY, ow)
    elif kind == "warp":
        for k, col in enumerate((MAGENTA, CYAN, YELLOW)):
            c.line([(x - 1.0 * r, y + (k - 1) * 0.2 * r + 0.3 * r), (x - 0.2 * r, y + (k - 1) * 0.12 * r)], col, ow * 2)
        c.poly(star_pts(x + 0.25 * r, y, 0.75 * r, 0.33 * r), None, YELLOW, ow * 3)
        c.poly(star_pts(x + 0.25 * r, y, 0.75 * r, 0.33 * r), None, NAVY, ow * 0.8)
        c.poly(star_pts(x + 0.25 * r, y, 0.35 * r, 0.16 * r), WHITE, None, 0)


def capsule(c, x, y, r, t=0.0):
    """The item capsule: a faceted crystal (an octahedron) with rainbow facets and a white core."""
    c.circle(x, y, 1.3 * r, rgb(WHITE, 50), None, 0)
    ca = math.cos(t)
    top, bot = (x, y - 1.15 * r), (x, y + 1.15 * r)
    mids = [(x + r * 0.85 * math.cos(t + k * math.pi / 2), y + 0.22 * r * math.sin(t + k * math.pi / 2)) for k in range(4)]
    faces = []
    for k in range(4):
        a, b = mids[k], mids[(k + 1) % 4]
        depth = (a[1] + b[1]) / 2
        faces.append((depth, [top, a, b], k, 0))
        faces.append((depth, [bot, a, b], k, 1))
    faces.sort()
    for d, f, k, lo in faces:
        col = RAINBOW[(k * 2 + lo) % 6]
        c.poly(f, rgb(light(col, 0.15 if lo else 0.35), 220), NAVY, max(1.2, r * 0.06))
    c.poly([(x, y - 0.45 * r), (x + 0.3 * r, y), (x, y + 0.45 * r), (x - 0.3 * r, y)], WHITE, None, 0)


# ---- the circuits (DESIGN.md 7) --------------------------------------------------------------------------
# key, EN, FR, cup, (sky, ground haze, track, accent), length (m), control points (x, z in -1..1), features:
# (t, kind) with kind in start, boost, items, jump, tunnel, loop, hazard, bank; ("tube", t0, t1), ("short", t0, t1).
def lemniscate(n=10, ax=0.9, az=0.6):
    return [(ax * math.sin(2 * math.pi * i / n), az * math.sin(4 * math.pi * i / n)) for i in range(n)]


CIRCUITS = [
    ("harbour", "Neon Harbour", "Port Néon", 0, (NSKY1, NSKY2, ROAD, NMAG), 1500,
     [(-0.8, 0.2), (-0.65, -0.6), (0.2, -0.75), (0.85, -0.35), (0.8, 0.4), (0.15, 0.5), (-0.3, 0.78)],
     [(0.0, "start"), (0.12, "boost"), (0.27, "items"), (0.45, "jump"), (0.62, "items"), (0.8, "boost"), (0.9, "bank")]),
    ("domebay", "Dome Bay", "Baie des Dômes", 0, (NSKY1, "#1F5FA0", ROAD, NCYAN), 1650,
     [(-0.9, 0.0), (-0.55, -0.72), (0.25, -0.5), (0.9, -0.8), (0.88, 0.3), (0.25, 0.15), (-0.15, 0.78), (-0.72, 0.62)],
     [(0.0, "start"), (0.15, "items"), ("tube", 0.26, 0.36), (0.45, "jump"), ("short", 0.6, 0.72), (0.66, "boost"), (0.82, "items"), (0.92, "hazard")]),
    ("foundry", "Mech Foundry", "Fonderie Méca", 0, (NSKY1, "#4A2A6A", ROAD, ORANGE), 1700,
     [(-0.85, -0.75), (0.0, -0.8), (0.85, -0.72), (0.82, -0.1), (0.25, -0.05), (0.2, 0.72), (-0.4, 0.8), (-0.85, 0.5)],
     [(0.0, "start"), (0.14, "boost"), (0.25, "items"), (0.4, "hazard"), (0.55, "tunnel"), (0.7, "jump"), (0.82, "items"), (0.93, "boost")]),
    ("jungle", "Glow Jungle", "Jungle Lumineuse", 0, (NSKY1, "#145A5A", ROAD, NLIME), 1900,
     lemniscate(10, 0.9, 0.62),
     [(0.0, "start"), (0.1, "items"), (0.22, "jump"), (0.35, "tunnel"), (0.5, "boost"), (0.6, "items"), ("short", 0.7, 0.8), (0.88, "hazard")]),
    ("crystal", "Crystal Moon", "Lune de Cristal", 1, ("#1A2F6A", "#3FD9A0", "#2A4A9A", VIOLET), 2000,
     [(-0.85, 0.75), (-0.85, -0.2), (-0.4, -0.75), (-0.2, 0.2), (0.15, -0.75), (0.55, -0.2), (0.85, -0.75), (0.85, 0.75)],
     [(0.0, "start"), (0.12, "items"), (0.3, "hazard"), (0.42, "boost"), (0.55, "tunnel"), (0.7, "jump"), (0.82, "items"), (0.93, "bank")]),
    ("asteroid", "Asteroid Rings", "Anneaux d'Astéroïdes", 1, ("#070F33", "#3A2A6A", ROAD, NMAG), 2100,
     [(-0.85, -0.3), (-0.5, -0.8), (0.0, -0.4), (0.5, -0.85), (0.88, -0.2), (0.4, 0.2), (0.75, 0.75), (-0.1, 0.65), (-0.45, 0.2), (-0.85, 0.6)],
     [(0.0, "start"), (0.1, "boost"), (0.2, "tunnel"), (0.38, "items"), (0.48, "jump"), ("tube", 0.55, 0.68), (0.75, "items"), ("short", 0.84, 0.94)]),
    ("cloudcity", "Skyline City", "Cité Skyline", 1, (NSKY1, NSKY2, ROAD, NCYAN), 2200,
     [(-0.9, 0.1), (-0.5, -0.75), (0.3, -0.85), (0.85, -0.3), (0.6, 0.15), (0.2, -0.15), (0.0, 0.35), (0.6, 0.8), (-0.4, 0.8)],
     [(0.0, "start"), (0.1, "items"), (0.25, "loop"), (0.4, "boost"), ("tube", 0.5, 0.62), (0.68, "items"), (0.78, "hazard"), (0.88, "boost")]),
    ("dunes", "Twin Sun Dunes", "Dunes des Deux Soleils", 1, ("#3A3A9A", "#FF9A5A", "#5A3A7A", ORANGE), 2150,
     [(-0.9, -0.6), (-0.3, -0.85), (0.3, -0.45), (0.9, -0.75), (0.8, 0.0), (0.25, 0.1), (0.75, 0.75), (-0.3, 0.6), (-0.85, 0.2)],
     [(0.0, "start"), (0.12, "jump"), (0.25, "items"), (0.38, "hazard"), (0.52, "boost"), (0.63, "tunnel"), (0.78, "items"), ("short", 0.86, 0.96)]),
    ("lava", "Lava Moon", "Lune de Lave", 2, ("#1A1440", "#A0402A", ROAD, ORANGE), 2400,
     [(-0.9, 0.0), (-0.6, -0.75), (0.2, -0.85), (0.85, -0.4), (0.75, 0.45), (0.0, 0.5), (-0.25, 0.0), (0.2, -0.3), (0.3, 0.2), (-0.3, 0.8), (-0.75, 0.6)],
     [(0.0, "start"), (0.1, "items"), (0.22, "hazard"), (0.32, "jump"), ("tube", 0.42, 0.6), (0.7, "tunnel"), (0.8, "items"), (0.9, "jump")]),
    ("cylinder", "Hyper Cylinder", "Hyper Cylindre", 2, (NSKY1, NSKY2, ROAD2, NCYAN), 2300,
     [(-0.85, -0.8), (-0.2, -0.8), (-0.2, -0.3), (0.3, -0.3), (0.3, -0.8), (0.85, -0.8), (0.85, 0.8), (0.2, 0.8), (0.2, 0.3), (-0.4, 0.3), (-0.4, 0.8), (-0.85, 0.8)],
     [(0.0, "start"), ("tube", 0.04, 0.3), (0.34, "jump"), ("tube", 0.38, 0.64), (0.67, "items"), ("tube", 0.7, 0.96), (0.98, "boost")]),
    ("carnival", "Star Carnival", "Fête Spatiale", 2, ("#140A3A", "#5A2A8A", ROAD, NYEL), 2600,
     [(0.0, -0.9), (0.3, -0.35), (0.9, -0.3), (0.45, 0.12), (0.62, 0.85), (0.0, 0.42), (-0.62, 0.85), (-0.45, 0.12), (-0.9, -0.3), (-0.3, -0.35)],
     [(0.0, "start"), (0.1, "items"), (0.2, "jump"), (0.32, "loop"), (0.45, "boost"), ("tube", 0.55, 0.68), (0.74, "items"), (0.82, "hazard"), (0.92, "boost")]),
    ("aurora", "Aurora Forest", "Forêt Aurore", 2, ("#1A2F6A", "#B85CFF", "#2A4A9A", NLIME), 2500,
     [(-0.85, 0.7), (-0.9, -0.1), (-0.5, -0.8), (-0.05, -0.2), (0.25, -0.85), (0.85, -0.6), (0.6, 0.0), (0.88, 0.6), (0.3, 0.85), (0.0, 0.3), (-0.35, 0.85)],
     [(0.0, "start"), (0.1, "items"), (0.22, "tunnel"), (0.3, "jump"), ("tube", 0.38, 0.5), ("short", 0.55, 0.64), (0.72, "boost"), (0.85, "hazard")]),
    ("ribbon", "Starlight Ribbon", "Ruban d'Étoiles", 3, ("#05092A", NSKY2, ROAD2, NMAG), 3200,
     [(-0.9, 0.0), (-0.7, -0.8), (-0.1, -0.5), (0.2, -0.9), (0.9, -0.6), (0.55, -0.05), (0.9, 0.5), (0.4, 0.85), (0.1, 0.3), (-0.3, 0.85), (-0.55, 0.3)],
     [(0.0, "start"), (0.08, "boost"), (0.18, "loop"), (0.3, "items"), (0.4, "jump"), (0.5, "boost"), ("tube", 0.6, 0.8), (0.84, "loop"), (0.9, "items")]),
]

CUPS = [
    ("moon", "Moonbeam Cup", "Coupe Rayon de Lune", CYAN, ["harbour", "domebay", "foundry", "jungle"]),
    ("nebula", "Nebula Cup", "Coupe Nébuleuse", VIOLET, ["crystal", "asteroid", "cloudcity", "dunes"]),
    ("nova", "Supernova Cup", "Coupe Supernova", ORANGE, ["lava", "cylinder", "carnival", "aurora"]),
    ("galaxy", "Galaxy Cup", "Coupe Galaxie", MAGENTA, ["jungle*", "cloudcity*", "carnival*", "ribbon"]),
]
CIRC = {k[0]: k for k in CIRCUITS}


def circuit_name(key):
    k = CIRC[key.rstrip("*")]
    return k[1] + (" Remix" if key.endswith("*") else "")


def feature_icon(c, kind, x, y, r):
    def disc(col):
        c.poly([(x - r, y - r * 0.8), (x + r * 1.1, y - r * 0.8), (x + r, y + r * 0.8), (x - r * 1.1, y + r * 0.8)], col, NAVY, 1.5)
    if kind == "boost":
        disc(YELLOW)
        chevron(c, x, y, r * 0.9, ORANGE, 1)
    elif kind == "jump":
        disc(MINT)
        c.poly([(x - 0.6 * r, y + 0.45 * r), (x + 0.6 * r, y + 0.45 * r), (x + 0.6 * r, y - 0.4 * r)], WHITE, None, 0)
    elif kind == "tunnel":
        disc(LGREY)
        c.d.arc(c.B((x - 0.55 * r, y - 0.5 * r, x + 0.55 * r, y + 0.6 * r)), 180, 360, fill=rgb(NAVY), width=int(2.5 * S))
    elif kind == "items":
        disc(WHITE)
        capsule(c, x, y, r * 0.5, 0.5)
    elif kind == "loop":
        disc(MAGENTA)
        c.d.arc(c.B((x - 0.45 * r, y - 0.45 * r, x + 0.45 * r, y + 0.45 * r)), 200, 520, fill=rgb(WHITE), width=int(2.5 * S))
    elif kind == "hazard":
        disc(ORANGE)
        c.text(x, y + 1, "!", int(r * 1.3), WHITE, None, 0)
    elif kind == "bank":
        disc(CYAN)
        c.line([(x - 0.5 * r, y + 0.3 * r), (x + 0.5 * r, y - 0.3 * r)], WHITE, 3)
    elif kind == "tube":
        disc(TEAL)
        c.ellipse(x, y, 0.55 * r, 0.55 * r, None, WHITE, 2.5)
        c.ellipse(x, y, 0.2 * r, 0.2 * r, None, MAGENTA, 2)
    elif kind == "start":
        for i in range(4):
            for j in range(2):
                c.d.rectangle(c.B((x - r + i * r / 2, y - r / 2 + j * r / 2, x - r + (i + 1) * r / 2, y + j * r / 2)), fill=rgb(NAVY if (i + j) % 2 else WHITE))
        c.d.rectangle(c.B((x - r, y - r / 2, x + r, y + r / 2)), outline=rgb(NAVY), width=int(1.5 * S))


def draw_circuit(c, key, box, track_w=14, icons=True, dots=None, reverse=False):
    k = CIRC[key]
    x0, y0, x1, y1 = box
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    sx, sy = (x1 - x0) / 2 * 0.86, (y1 - y0) / 2 * 0.86
    pts = [(cx + px * sx, cy + pz * sy) for px, pz in catmull(k[6], 20)]
    n = len(pts)
    c.line(pts, NAVY, track_w + 5, closed=True)
    c.line(pts, k[4][3], track_w + 2, closed=True)
    c.line(pts, k[4][2], track_w - 2, closed=True)
    for f in k[7]:
        if f[0] == "tube":
            seg = pts[int(f[1] * n):int(f[2] * n) + 1]
            c.line(seg, NAVY, track_w + 9)
            c.line(seg, TEAL, track_w + 5)
            for i in range(0, len(seg) - 1, 3):
                c.line([seg[i], seg[i + 1]], MAGENTA, max(2, track_w * 0.35))
    c.line(pts, rgb(WHITE, 150), max(1.2, track_w * 0.12), closed=True)
    if icons:
        for f in k[7]:
            if f[0] == "short":
                a, b = pts[int(f[1] * n) % n], pts[int(f[2] * n) % n]
                for i in range(0, 8, 2):
                    c.line([(a[0] + (b[0] - a[0]) * i / 8, a[1] + (b[1] - a[1]) * i / 8), (a[0] + (b[0] - a[0]) * (i + 1) / 8, a[1] + (b[1] - a[1]) * (i + 1) / 8)], YELLOW, 4)
            elif f[0] == "tube":
                p = pts[int((f[1] + f[2]) / 2 * n) % n]
                feature_icon(c, "tube", p[0], p[1], track_w * 0.85)
            else:
                p = pts[int(f[0] * n) % n]
                feature_icon(c, f[1], p[0], p[1], track_w * 0.85)
    if dots:
        for t, col in dots:
            p = pts[int(t * n) % n]
            c.circle(p[0], p[1], track_w * 0.6, col, NAVY, 2)
    return pts


# ---- the world: sky, clouds, architecture ------------------------------------------------------------------
def sky(c, box, top=NSKY1, low=NSKY2, seed=0, clouds=True):
    """The night sky: a deep blue gradient, stars, a few flying ships' lights."""
    c.grad(box, top, low)
    rnd = random.Random(seed)
    x0, y0, x1, y1 = box
    for k in range(int((x1 - x0) * (y1 - y0) / 6000)):
        c.circle(x0 + rnd.random() * (x1 - x0), y0 + rnd.random() * (y1 - y0) * 0.7, 0.8 + rnd.random() * 1.4, rgb(WHITE, 120 + int(rnd.random() * 120)), None, 0)
    if clouds:
        for k in range(3):
            x, y = x0 + rnd.random() * (x1 - x0), y0 + (y1 - y0) * (0.1 + rnd.random() * 0.3)
            c.line([(x - 60, y + 6), (x, y)], rgb(NCYAN if k % 2 else NMAG, 150), 2)
            c.poly([(x, y - 3), (x + 18, y), (x, y + 3)], NTOWER2, OUTL, 1)


def cloud_card(c, x, y, w, rnd=None):
    """A stylised flat cloud: white puffs with one pale-blue shade band at the bottom, a flat base."""
    rnd = rnd or random.Random(int(x + y))
    n = 5
    puffs = []
    for i in range(n):
        px = x - w / 2 + w * (i + 0.5) / n
        pr = w * (0.13 + 0.12 * math.sin(math.pi * (i + 0.5) / n)) * (0.85 + rnd.random() * 0.3)
        puffs.append((px, y - pr * 0.4, pr))
    for px, py, pr in puffs:
        c.ellipse(px, py + pr * 0.18, pr * 1.05, pr * 0.82, CLOUDSH, None, 0)
    for px, py, pr in puffs:
        c.ellipse(px, py, pr, pr * 0.78, CLOUD, None, 0)


def cloud_sea(c, y, x0=0, x1=None, seed=3):
    """The city at the horizon: far towers with lit windows, billboards, a stacked highway with light trails."""
    x1 = x1 or c.w
    rnd = random.Random(seed)
    x = x0 - 20
    while x < x1:
        w = 30 + rnd.random() * 50
        h = 60 + rnd.random() * 170
        c.d.rectangle(c.B((x, y - h, x + w, y + 40)), fill=rgb(NTOWER))
        for wy in range(int(y - h + 8), int(y + 30), 9):
            for wx in range(int(x + 5), int(x + w - 4), 8):
                if rnd.random() < 0.35:
                    c.d.rectangle(c.B((wx, wy, wx + 3, wy + 4)), fill=rgb(NYEL if rnd.random() < 0.6 else NCYAN, 200))
        if rnd.random() < 0.3:
            col = (NMAG, NCYAN, NLIME)[int(rnd.random() * 3)]
            by = y - h * (0.4 + rnd.random() * 0.4)
            c.d.rectangle(c.B((x + 4, by, x + w - 4, by + 16)), fill=rgb(col, 230))
        x += w + 4 + rnd.random() * 20
    c.d.rectangle(c.B((x0, y + 20, x1, c.h)), fill=rgb(ROADU))
    for k in range(2):
        yy = y + 8 + k * 16
        c.d.rectangle(c.B((x0, yy, x1, yy + 6)), fill=rgb(NTOWER2))
        for j in range(0, int(x1 - x0), 23):
            c.line([(x0 + j + k * 9, yy + 3), (x0 + j + 10 + k * 9, yy + 3)], (NYEL, "#FF5A5A")[k], 2)


def tower(c, x, y, w, h, strip=NMAG):
    """A dark-blue neon skyscraper: lit windows, a neon edge strip, a billboard, an outline."""
    rnd = random.Random(int(x * 7 + h))
    c.poly([(x - w / 2, y), (x - w / 2, y - h), (x + w / 2, y - h), (x + w / 2, y)], NTOWER, OUTL, 2)
    c.poly([(x + w * 0.2, y), (x + w * 0.2, y - h), (x + w / 2, y - h), (x + w / 2, y)], NTOWER2, None, 0)
    for wy in range(int(y - h + 10), int(y - 6), 12):
        for wx in range(int(x - w / 2 + 6), int(x + w / 2 - 6), 10):
            if rnd.random() < 0.45:
                c.d.rectangle(c.B((wx, wy, wx + 5, wy + 6)), fill=rgb(NYEL if rnd.random() < 0.7 else NCYAN, 210))
    c.line([(x - w / 2, y), (x - w / 2, y - h)], strip, max(2, w * 0.05))
    by = y - h * 0.62
    words = ["VELOCITY GRID", "HYPER", "COSMO ZOOM", "NOVA", "GRID 9"]
    col = (NMAG, NCYAN, NLIME)[int(rnd.random() * 3)]
    c.rrect((x - w * 0.62, by - w * 0.22, x + w * 0.62, by + w * 0.22), 3, rgb(OUTL, 230), col, 3)
    word = words[int(rnd.random() * len(words))]
    size = max(7, int(min(w * 0.3, (w * 1.15) / max(1, len(word)) * 1.6)))
    c.text(x, by, word, size, col, None, 0, italic=True)


def terminal(c, x, y, w, h):
    """An elevated highway stack curving away, with streams of light trails."""
    for k in range(3):
        yy = y - h * (0.3 + k * 0.35)
        pts = [(x - w / 2 + k * 20, yy + 12), (x, yy), (x + w / 2, yy + 18 - k * 4)]
        sm = catmull(pts + [(x + w, yy + 60), (x - w, yy + 60)], 10)[:21]
        c.line(sm, OUTL, 12)
        c.line(sm, NTOWER2, 8)
        c.line(sm, rgb((NYEL, "#FF5A5A", NCYAN)[k], 230), 2.5)
    for k in range(4):
        px = x - w / 2 + k * w / 3
        c.d.rectangle(c.B((px - 4, y - h * 0.3, px + 4, y)), fill=rgb(NTOWER))


def ring_gate(c, cx, cy, r, w, col=NTOWER2, strip=NCYAN):
    c.circle(cx, cy, r, None, NAVY, w + 3)
    c.circle(cx, cy, r, None, col, w)
    c.circle(cx, cy, r, None, strip, max(1.5, w * 0.25))


def slant(c, x, y, w, h, fill, edge=None, k=0.21, ew=2):
    """A slanted panel (a parallelogram leaning right)."""
    d = h * k
    pts = [(x + d, y), (x + w + d, y), (x + w - d, y + h), (x - d, y + h)]
    c.poly(pts, fill, edge, ew if edge else 0)
    return pts


def itext(c, x, y, s, size, fill=WHITE, stroke=NAVY, sw=None, anchor="mm"):
    c.text(x, y, s, size, fill, stroke, sw if sw is not None else max(1, size // 12), anchor, True, True)


# ================================ the pictures ================================
def speed_lines(c, n=14, seed=1, box=None, col=(255, 255, 255, 150)):
    rnd = random.Random(seed)
    x0, y0, x1, y1 = box or (0, 0, c.w, c.h)
    for k in range(n):
        y = y0 + rnd.random() * (y1 - y0)
        x = x0 + rnd.random() * (x1 - x0)
        L = 60 + rnd.random() * 140
        c.line([(x, y), (x + L, y)], col, 2 + rnd.random() * 2)


def logo(c, cx, cy, size):
    for dx, dy, col in ((10, 8, MAGENTA), (5, 4, CYAN)):
        itext(c, cx + dx, cy + dy, "COSMO ZOOM", size, col, col, max(2, size // 14))
    itext(c, cx, cy, "COSMO ZOOM", size, WHITE, NAVY, max(2, size // 14))
    w = c.text_w("COSMO ZOOM", size, True, True)
    slant(c, cx - w / 2 + 20, cy + size * 0.52, w * 0.62, 12, MAGENTA, NAVY, 0.9)
    slant(c, cx - w / 2 + 20 + w * 0.64, cy + size * 0.52, w * 0.3, 12, CYAN, NAVY, 0.9)


def track_band(c, left, right, surf=ROAD, rail=NMAG):
    """A track ribbon seen in perspective: two edge polylines (near first)."""
    c.poly(left + right[::-1], surf, None, 0)
    n = len(left)
    for i in range(n - 1):
        if i % 2 == 0:
            q = [left[i], left[i + 1], right[i + 1], right[i]]
            c.poly(q, ROAD2, None, 0)
    under = [(x, y + 26 * (1 - i / n)) for i, (x, y) in enumerate(right)]
    c.poly(right + under[::-1], ROADU, None, 0)
    mid = [((l[0] + r[0]) / 2, (l[1] + r[1]) / 2) for l, r in zip(left, right)]
    for i in range(0, n - 1, 2):
        c.line([mid[i], mid[i + 1]], rgb(WHITE, 220), max(1.5, 7 * (1 - i / n)))
    for edge, rail in ((left, NMAG), (right, NCYAN)):
        for i in range(n - 1):
            th = 12 * (1 - i / n) + 2
            p, q = edge[i], edge[i + 1]
            c.poly([p, q, (q[0], q[1] - th), (p[0], p[1] - th)], rail, None, 0)
            c.line([(p[0], p[1] - th), (q[0], q[1] - th)], light(rail, 0.5), max(1, th * 0.3))
        c.line(edge, NAVY, 1.5)


def pic_title():
    c = Canvas(1280, 720)
    sky(c, (0, 0, 1280, 720), seed=4)
    cloud_sea(c, 470)
    c.circle(1130, 120, 46, rgb("#E9F3FF"), None, 0)
    c.circle(1130, 120, 46, None, rgb(WHITE, 200), 3)
    for (x, w, h) in ((880, 60, 230), (960, 44, 180), (1040, 70, 290), (1150, 50, 200), (1220, 40, 150)):
        tower(c, x, 470, w, h, MAGENTA if x % 2 else CYAN)
    terminal(c, 300, 470, 380, 90)
    ring_gate(c, 760, 360, 95, 14)
    # the track, sweeping from the bottom-left to the horizon
    ctrlL = [(-80, 780), (300, 650), (640, 560), (900, 500), (1300, 470)]
    ctrlR = [(80, 900), (500, 760), (800, 640), (1050, 560), (1300, 520)]
    L = catmull(ctrlL + [(1500, 600), (-200, 1000)], 10)[:40]
    R = catmull(ctrlR + [(1500, 700), (-200, 1100)], 10)[:40]
    track_band(c, L, R)
    speed_lines(c, 18, 2, (0, 380, 1280, 720))
    e = ship_for("ember")
    ship_side(c, 470, 690, 78, **e)
    o = ship_for("orchid")
    ship_side(c, 900, 560, 46, **o)
    v = ship_for("volt")
    ship_side(c, 700, 610, 36, **v)
    logo(c, 640, 150, 118)
    slant(c, 420, 240, 440, 44, rgb(NAVY, 220), MAGENTA, 0.3)
    itext(c, 640, 262, "ANTI-GRAVITY GRAND PRIX", 24, WHITE, None, 0)
    slant(c, 530, 330, 220, 40, MAGENTA, NAVY, 0.3)
    itext(c, 640, 350, "PRESS START", 22, WHITE, NAVY, 2)
    itext(c, 1255, 705, "1-4 players  -  Onyx", 15, WHITE, NAVY, 2, anchor="rm")
    c.save("title.png")


# ---- the race view and the HUD ----
def race_view(player="ember", seed=0, curve=0.012, hud=True, pos=2, item="shield", opps=None, compact=False):
    c = Canvas(1280, 720)
    H0, F, Hc = 300, 900, 2.3
    sky(c, (0, 0, 1280, H0 + 2), seed=seed + 1)
    cloud_sea(c, H0 - 4, seed=seed)
    for (x, w, h) in ((90, 40, 150), (150, 30, 110), (1120, 46, 170), (1190, 34, 120)):
        tower(c, x + seed * 40, H0 + 4, w, h, MAGENTA)
    terminal(c, 300 + seed * 60, H0 + 4, 200, 40)

    def X(x, z):
        return 640 + (x + curve * z * z - 0.4) * F / z

    def Y(y, z):
        return H0 + (Hc - y) * F / z
    zs = []
    z = 4.2
    while z < 180:
        zs.append(z)
        z *= 1.035
    for z1, z0 in list(zip(zs[1:], zs[:-1]))[::-1]:
        band = int(z0 / 4) % 2
        q = lambda a, b, ya=0, yb=0: [(X(a, z1), Y(ya, z1)), (X(b, z1), Y(yb, z1)), (X(b, z0), Y(yb, z0)), (X(a, z0), Y(ya, z0))]
        c.d.polygon(c.P(q(-7, 7)), fill=rgb(ROAD2 if band else ROAD))
        c.d.polygon(c.P([(X(-7, z1), Y(0, z1)), (X(-7, z0), Y(0, z0)), (X(-7.2, z0), Y(-0.8, z0)), (X(-7.2, z1), Y(-0.8, z1))]), fill=rgb(ROADU))
        for lx in (-6.3, -3.5, 2.6, 6.1):
            if (int(z0 * 0.7) + int(lx)) % 3:
                c.d.polygon(c.P(q(lx, lx + 0.12)), fill=rgb(NCYAN if lx in (-3.5, 2.6) else WHITE, 230))
        if int(z0 / 3) % 3 == 0:
            c.d.polygon(c.P(q(-0.15, 0.15)), fill=rgb(WHITE, 220))
        for sx in (-1, 1):
            rc = NMAG if sx < 0 else NCYAN
            c.d.polygon(c.P([(X(sx * 7.0, z1), Y(0, z1)), (X(sx * 7.0, z1), Y(0.9, z1)), (X(sx * 7.0, z0), Y(0.9, z0)), (X(sx * 7.0, z0), Y(0, z0))]), fill=rgb(rc))
            c.d.polygon(c.P([(X(sx * 7.0, z1), Y(0.9, z1)), (X(sx * 7.4, z1), Y(0.9, z1)), (X(sx * 7.4, z0), Y(0.9, z0)), (X(sx * 7.0, z0), Y(0.9, z0))]), fill=rgb(light(rc, 0.5)))
            if int(z0) % 8 == 0:
                c.d.polygon(c.P([(X(sx * 7.0, z1), Y(0, z1)), (X(sx * 7.0, z1), Y(0.95, z1)), (X(sx * 7.0, z0), Y(0.95, z0)), (X(sx * 7.0, z0), Y(0, z0))]), fill=rgb(LGREY))
        if 8 < z0 < 13.5:
            c.d.polygon(c.P(q(1.4, 4.6)), fill=rgb(NCYAN, 200))
            if int(z0 * 2) % 2 == 0:
                zc = z0
                pts = [(X(1.6, zc), Y(0, zc)), (X(3.0, zc - 1.0), Y(0, zc - 1.0)), (X(4.4, zc), Y(0, zc)), (X(4.4, zc + 0.5), Y(0, zc + 0.5)), (X(3.0, zc - 0.5), Y(0, zc - 0.5)), (X(1.6, zc + 0.5), Y(0, zc + 0.5))]
                c.d.polygon(c.P(pts), fill=rgb(WHITE))
    for sx in (-1, 1):
        c.line([(X(sx * 7.4, z), Y(0.9, z)) for z in zs if z > 4.3], NAVY, 1.5)
    # the ring gate
    za = 58
    gx, gy = X(0, za), Y(4.5, za)
    ring_gate(c, gx, gy, 9.5 * F / za, max(4, 0.9 * F / za))
    for x in (-4.5, -1.5, 1.5, 4.5):
        capsule(c, X(x, 21), Y(1.3, 21), 0.7 * F / 21, x * 0.4)
    for k in range(5):
        zz = 13 + k * 2
        x, y, r = X(-3.2 + 0.2 * k, zz), Y(0.9, zz), 0.3 * F / zz
        c.poly([(x + r * math.cos(a), y + r * math.sin(a)) for a in [i * math.pi / 3 for i in range(6)]], YELLOW, NAVY, 1.5)
    opps = opps if opps is not None else [("orchid", 2.8, 15), ("volt", -2.5, 28), ("zenith", 1.0, 48)]
    for key, ox, oz in sorted(opps, key=lambda o: -o[2]):
        s = ship_for(key)
        ship_back(c, X(ox, oz), Y(0, oz), F / oz, s["hull"], s["wing"], s["eng"], s["col1"], s["col2"], s["acc"])
    s = ship_for(player)
    ship_back(c, 640, 700, 70, s["hull"], s["wing"], s["eng"], s["col1"], s["col2"], s["acc"], 1.0)
    rnd = random.Random(seed + 4)
    for k in range(8):
        sx = 640 + (-1 if k % 2 else 1) * (70 + rnd.random() * 60)
        sy = 690 - rnd.random() * 30
        c.poly(star_pts(sx, sy, 8, 2.5, 4, rnd.random()), YELLOW if k % 3 else WHITE, None, 0)
    speed_lines(c, 10, seed, (0, 330, 1280, 720))
    if hud:
        draw_hud(c, pos, item, compact, player)
    return c


def draw_hud(c, pos, item, compact, player, mapkey="harbour"):
    navy = rgb(NAVY, 190)
    if not compact:
        order = ["orchid", "ember", "volt", "zenith", "sunburst", "glacier", "nebula", "verde"]
        for k, key in enumerate(order):
            y = 124 + k * 40
            me = key == player
            slant(c, 18, y, 140 if me else 128, 34, rgb(MAGENTA, 230) if me else navy, WHITE if me else None, 0.25)
            itext(c, 38, y + 18, str(k + 1), 18, WHITE, None, 0)
            c.d.polygon(c.P([(58, y + 4), (64, y + 4), (60, y + 30), (54, y + 30)]), fill=rgb(TEAM[key][4]))
            helmet(c, 84, y + 18, 11, key, shoulders=False)
            itext(c, 102, y + 18, TEAM[key][1].split()[0][:3].upper(), 11, WHITE, None, 0, anchor="lm")
    # the gadget slot
    gs = 1.25 if compact else 1.0
    slant(c, 18, 16, 128 * gs, 92 * gs, navy, WHITE, 0.2)
    slant(c, 26 * gs, 22 * gs, 112 * gs, 80 * gs, None, MAGENTA, 0.2, 1.5)
    gadget_icon(c, item, 18 + 64 * gs, 16 + 46 * gs, 34 * gs)
    # the position, the energy bar, the drift bar
    px = 520 if not compact else 470
    itext(c, px, 56, str(pos), 76 if not compact else 90, WHITE, NAVY, 4, anchor="rm")
    itext(c, px + 4, 70, "/8", 30 if not compact else 36, WHITE, NAVY, 3, anchor="lm")
    if not compact:
        slant(c, 600, 30, 330, 20, navy, WHITE, 0.6, 2)
        slant(c, 604, 33, 230, 14, MAGENTA, None, 0.6)
        itext(c, 600, 18, "ENERGY", 12, WHITE, None, 0, anchor="lm")
        c.poly([(960 + 9 * math.cos(a), 40 + 9 * math.sin(a)) for a in [i * math.pi / 3 for i in range(6)]], YELLOW, NAVY, 1.5)
        itext(c, 976, 40, "7", 20, WHITE, NAVY, 2, anchor="lm")
        slant(c, 610, 60, 250, 10, navy, None, 0.6)
        slant(c, 612, 61, 120, 8, CYAN, None, 0.6)
        slant(c, 700, 61, 60, 8, YELLOW, None, 0.6)
        itext(c, 866, 65, "DRIFT", 11, WHITE, None, 0, anchor="lm")
    # the laps, the times
    itext(c, 1255, 44, "LAP 2/3", 40 if not compact else 48, WHITE, NAVY, 3, anchor="rm")
    if not compact:
        itext(c, 1255, 84, "1:02.45", 24, YELLOW, NAVY, 2, anchor="rm")
        itext(c, 1255, 110, "BEST 0:31.20", 15, WHITE, NAVY, 2, anchor="rm")
        # the mini-map
        slant(c, 24, 520, 220, 180, navy, WHITE, 0.15)
        draw_circuit(c, mapkey, (40, 530, 236, 690), 8, icons=False, dots=[(0.31, MAGENTA), (0.18, YELLOW), (0.26, WHITE), (0.12, ORANGE), (0.05, CYAN)])
        pts = catmull(CIRC[mapkey][6], 20)
        p = pts[int(0.29 * len(pts))]
        c.circle(138 + p[0] * 98 * 0.86, 610 + p[1] * 80 * 0.86, 8, RED, WHITE, 2.5)
    # the speed
    itext(c, 1180, 650, "142", 80 if not compact else 96, WHITE, NAVY, 4, anchor="rm")
    itext(c, 1188, 668, "KM/H", 22 if not compact else 26, WHITE, NAVY, 2, anchor="lm")
    for k in range(12):
        col = CYAN if k < 6 else (YELLOW if k < 9 else MAGENTA)
        slant(c, 980 + k * 22, 695, 17, 10, col if k < 9 else rgb(WHITE, 90), None, 0.6)


def pic_race():
    race_view().save("race_hud.png")


def sprite_ship(key, angle, scale_px):
    """A ship from behind as a rotated sprite (for the tube)."""
    sp = Canvas(400, 260, None)
    s = ship_for(key)
    ship_back(sp, 200, 200, 70, s["hull"], s["wing"], s["eng"], s["col1"], s["col2"], s["acc"])
    im = sp.img.rotate(angle, resample=Image.BICUBIC, expand=True)
    k = scale_px / 70.0
    return im.resize((max(1, int(im.width * k)), max(1, int(im.height * k))), Image.LANCZOS)


def pic_tube():
    """Riding the outside of a tube: the camera rolls with the ship, so the world (sky, towers) is tilted."""
    c = Canvas(1280, 720)
    big = Canvas(1800, 1300)
    sky(big, (0, 0, 1800, 760), seed=7)
    cloud_sea(big, 740, seed=5)
    for (x, w, h) in ((300, 70, 300), (420, 50, 220), (1350, 80, 340), (1480, 50, 240), (1580, 60, 280)):
        tower(big, x, 750, w, h, MAGENTA)
    ring_gate(big, 900, 560, 150, 16)
    rot = big.img.rotate(-24, resample=Image.BICUBIC)
    W, H = 1280 * S, 720 * S
    c.img.paste(rot.crop(((rot.width - W) // 2, (rot.height - H) // 2 - 40 * S, (rot.width + W) // 2, (rot.height + H) // 2 - 40 * S)), (0, 0))
    R, d, F, VY = 7.0, 9.3, 800, 250
    lim = math.acos(R / d)

    def P(phi, z, h=0.0):
        x = (R + h) * math.sin(phi)
        y = (R + h) * math.cos(phi) - d
        return (640 + x * F / z, VY - y * F / z)
    zs = []
    z = 3.4
    while z < 220:
        zs.append(z)
        z *= 1.04
    nphi = 16
    phis = [-lim + 2 * lim * i / nphi for i in range(nphi + 1)]
    for z1, z0 in list(zip(zs[1:], zs[:-1]))[::-1]:
        band = int(z0 / 6) % 2
        for i in range(nphi):
            a, b = phis[i], phis[i + 1]
            m = (a + b) / 2
            base = ROAD if band else ROAD2
            shade = 0.0 if m < 0.15 else 0.18
            col = mix(base, ROADU, shade + abs(m) / lim * 0.4)
            c.d.polygon(c.P([P(a, z1), P(b, z1), P(b, z0), P(a, z0)]), fill=col)
        for sp in (-math.radians(22.5), 0.0, math.radians(22.5)):
            w = math.radians(1.2)
            c.d.polygon(c.P([P(sp - w, z1), P(sp + w, z1), P(sp + w, z0), P(sp - w, z0)]), fill=rgb(NMAG if sp else NCYAN))
        if int(z0 / 20) != int(z1 / 20):
            c.line([P(p, z0) for p in phis], NCYAN, max(1.5, 30 / z0))
    # the silhouette edges
    for side in (-1, 1):
        c.line([P(side * lim, z) for z in zs], NAVY, 2)
    # a spiral of boost pads, capsules, energy cells
    for k, (zz, ph) in enumerate(((10, -18), (13, -9), (16, 0), (19, 9), (22, 18))):
        p, q = P(math.radians(ph), zz), P(math.radians(ph), zz + 1.6)
        r = 1.2 * F / zz
        c.poly([(p[0] - r, p[1]), (q[0], q[1]), (p[0] + r, p[1])], ORANGE, NAVY, 1)
        c.poly([(p[0] - r * 0.6, p[1] - 1), (q[0], q[1] + r * 0.15), (p[0] + r * 0.6, p[1] - 1)], YELLOW, None, 0)
    for ph in (-26, -9, 9, 26):
        x, y = P(math.radians(ph), 30, 1.3)
        capsule(c, x, y, 0.7 * F / 30, ph * 0.05)
    for key, ph, zz in (("glacier", 24, 15), ("orchid", -14, 34)):
        x, y = P(math.radians(ph), zz)
        im = sprite_ship(key, -ph, F / zz)
        c.img.paste(im, (int(x * S - im.width / 2), int(y * S - im.height * 0.8)), im)
    s = ship_for("ember")
    ship_back(c, 640, 700, 70, s["hull"], s["wing"], s["eng"], s["col1"], s["col2"], s["acc"], 1.0)
    speed_lines(c, 12, 9, (0, 300, 1280, 720))
    draw_hud(c, 3, "boost", False, "ember", mapkey="cylinder")
    slant(c, 520, 120, 240, 40, rgb(NAVY, 190), CYAN, 0.3)
    itext(c, 640, 141, "TUBE SECTION", 20, CYAN, None, 0)
    c.save("race_tube.png")


def pic_split():
    out = Canvas(1280, 720, NAVY)
    views = [dict(player="ember", seed=0, pos=2, item="shield"),
             dict(player="volt", seed=1, curve=-0.008, pos=4, item="goo", opps=[("ember", -1.5, 18), ("orchid", 2.5, 36)]),
             dict(player="glacier", seed=2, curve=0.004, pos=1, item="boost", opps=[("sunburst", 1.5, 60)]),
             dict(player="nebula", seed=3, curve=-0.014, pos=7, item="warp", opps=[("verde", 2.0, 16), ("zenith", -2.0, 28), ("volt", 0.5, 55)])]
    for i, v in enumerate(views):
        cv = race_view(compact=True, **v)
        im = cv.img.resize((636 * S, 356 * S), Image.LANCZOS)
        out.img.paste(im, ((i % 2) * 644 * S, (i // 2) * 364 * S))
    for i, key in enumerate(("ember", "volt", "glacier", "nebula")):
        x, y = (i % 2) * 644 + 250, (i // 2) * 364 + 12
        slant(out, x, y, 60, 26, TEAM[key][4], WHITE, 0.3)
        itext(out, x + 30, y + 13, "P%d" % (i + 1), 15, WHITE, NAVY, 2)
    out.save("race_split4.png")


# ---- the menus ----
def stat_bar(c, x, y, label, val, delta=0, col=CYAN):
    itext(c, x, y, label, 16, WHITE, None, 0, anchor="lm")
    px = x + 140
    for k in range(5):
        filled = k < val
        gained = delta > 0 and val <= k < val + delta
        lost = delta < 0 and val + delta <= k < val
        fill = col if filled else (MINT if gained else rgb(WHITE, 40))
        if lost:
            fill = MAGENTA
        slant(c, px + k * 34, y - 8, 28, 16, fill, None, 0.5)
    if delta:
        itext(c, px + 180, y, ("+%d" % delta) if delta > 0 else str(delta), 17, MINT if delta > 0 else MAGENTA, NAVY, 1, anchor="lm")


def pic_garage():
    c = Canvas(1280, 720)
    c.grad((0, 0, 1280, 720), ARCH, LGREY)
    sky(c, (0, 60, 1280, 330), seed=11)
    c.d.rectangle(c.B((0, 60, 1280, 330)), outline=rgb(NAVY), width=int(2 * S))
    for x in range(0, 1281, 160):
        c.d.rectangle(c.B((x - 6, 60, x + 6, 330)), fill=rgb(ARCH))
        c.line([(x - 6, 60), (x - 6, 330)], rgb(NAVY, 120), 1)
    c.d.rectangle(c.B((0, 0, 1280, 60)), fill=rgb(ARCH))
    c.line([(0, 52), (1280, 52)], MAGENTA, 3)
    c.d.rectangle(c.B((0, 330, 1280, 720)), fill=rgb("#E3E9F4"))
    for k in range(-6, 14):
        c.line([(640 + k * 120, 720), (640 + k * 30, 330)], rgb(STEEL, 50), 1.5)
    # the platform
    c.ellipse(360, 560, 300, 64, STEEL, NAVY, 2)
    c.ellipse(360, 550, 300, 64, WHITE, NAVY, 2)
    c.ellipse(360, 550, 250, 50, None, CYAN, 4)
    c.ellipse(360, 550, 200, 40, rgb(CYAN, 60), None, 0)
    e = ship_for("ember")
    ship_side(c, 380, 520, 92, **e)
    itext(c, 140, 650, "rotate: right stick / Q E", 13, STEEL, None, 0, anchor="lm")
    # the name plate
    slant(c, 30, 76, 440, 74, rgb(NAVY, 220), MAGENTA, 0.2)
    c.d.polygon(c.P([(52, 82), (110, 82), (100, 144), (42, 144)]), fill=rgb(RED))
    helmet(c, 76, 106, 17, "ember")
    itext(c, 126, 100, "KAI MORENO", 26, WHITE, None, 0, anchor="lm")
    itext(c, 126, 128, "TEAM EMBER  -  #07  -  Comet", 15, PINK, None, 0, anchor="lm")
    slant(c, 760, 8, 300, 40, MAGENTA, NAVY, 0.3)
    itext(c, 910, 28, "HANGAR", 24, WHITE, NAVY, 2)
    # the panel
    px0, py0, px1, py1 = 650, 74, 1260, 660
    c.poly([(px0 + 20, py0), (px1, py0), (px1 - 20, py1), (px0, py1)], rgb(NAVY, 225), WHITE, 2)
    tabs = ["Pilot", "Hull", "Wings", "Engine", "Livery"]
    tx = px0 + 30
    for t in tabs:
        sel = t == "Engine"
        slant(c, tx, py0 + 14, 104, 34, MAGENTA if sel else rgb(WHITE, 30), WHITE if sel else None, 0.3)
        itext(c, tx + 52, py0 + 31, t, 17, WHITE, None, 0)
        tx += 112
    for i, ek in enumerate(ENGINE_ORDER):
        col, row = i % 2, i // 2
        bx, by = px0 + 30 + col * 285, py0 + 66 + row * 122
        sel = ek == "tether"
        hov = ek == "ring"
        slant(c, bx, by, 268, 110, rgb(WHITE, 30), YELLOW if sel else (CYAN if hov else rgb(WHITE, 90)), 0.12, 3 if (sel or hov) else 1.5)
        ship_top(c, bx + 80, by + 56, 20, "comet", "swept", ek, RED, WHITE, YELLOW, 7)
        itext(c, bx + 150, by + 32, ENGINES[ek]["name"], 16, WHITE, None, 0, anchor="lm")
        mods = ENGINES[ek]["mod"].split("   ")
        for k, m in enumerate(mods):
            itext(c, bx + 150, by + 58 + k * 20, m, 13, MINT if m.startswith("+") else (MAGENTA if m.startswith("-") else LGREY), None, 0, anchor="lm")
        if sel:
            itext(c, bx + 236, by + 92, "ON", 14, YELLOW, None, 0)
    sy = py0 + 330
    itext(c, px0 + 36, sy - 12, "STATS  (preview: Ring Drive)", 15, CYAN, None, 0, anchor="lm")
    stat_bar(c, px0 + 40, sy + 20, "Speed", 4, -1)
    stat_bar(c, px0 + 40, sy + 48, "Acceleration", 3, 0)
    stat_bar(c, px0 + 40, sy + 76, "Handling", 2, 2)
    stat_bar(c, px0 + 40, sy + 104, "Weight", 3, -1)
    qy = sy + 150
    itext(c, px0 + 36, qy, "LIVERY", 15, CYAN, None, 0, anchor="lm")
    for k, col in enumerate([RED, ORANGE, YELLOW, LIME, MINT, CYAN, BLUE, VIOLET, MAGENTA, WHITE]):
        x = px0 + 130 + k * 42
        if k == 0:
            c.poly([(x, qy - 18), (x + 18, qy), (x, qy + 18), (x - 18, qy)], YELLOW, None, 0)
        c.poly([(x, qy - 13), (x + 13, qy), (x, qy + 13), (x - 13, qy)], col, NAVY, 1.5)
    for k, ptn in enumerate(["plain", "stripe", "split", "chevrons", "checker", "fade"]):
        x = px0 + 40 + k * 92
        box = [(x + 6, qy + 30), (x + 82, qy + 30), (x + 76, qy + 70), (x, qy + 70)]
        fill_livery(c, box, RED, WHITE, YELLOW, ptn)
        c.poly(box, None, YELLOW if k == 1 else WHITE, 3 if k == 1 else 1.5)
    slant(c, 20, 676, 1240, 34, rgb(NAVY, 200), None, 0.3)
    x = 50
    for lab, col, txt in (("A", LIME, "Pick"), ("B", RED, "Back"), ("Y", YELLOW, "Random"), ("X", CYAN, "Test drive"), ("+", MAGENTA, "Ready")):
        c.poly([(x - 10, 682), (x + 14, 682), (x + 10, 704), (x - 14, 704)], col, WHITE, 1.5)
        itext(c, x, 693, lab, 14, NAVY, None, 0)
        itext(c, x + 22, 693, txt, 15, WHITE, None, 0, anchor="lm")
        x += 60 + c.text_w(txt, 15, True, True)
    c.save("garage.png")


def trophy(c, x, y, s, col, emblem=None):
    ow = max(1.5, s * 0.03)
    bowl = [(x - 0.62 * s, y - 1.0 * s), (x + 0.62 * s, y - 1.0 * s), (x + 0.4 * s, y - 0.35 * s), (x + 0.12 * s, y - 0.2 * s), (x - 0.12 * s, y - 0.2 * s), (x - 0.4 * s, y - 0.35 * s)]
    for sx in (-1, 1):
        c.poly([(x + sx * 0.6 * s, y - 0.95 * s), (x + sx * 0.85 * s, y - 0.9 * s), (x + sx * 0.6 * s, y - 0.45 * s), (x + sx * 0.5 * s, y - 0.55 * s), (x + sx * 0.68 * s, y - 0.85 * s)], col, NAVY, ow)
    c.poly(bowl, col, NAVY, ow)
    c.poly([(x - 0.5 * s, y - 0.95 * s), (x - 0.35 * s, y - 0.95 * s), (x - 0.25 * s, y - 0.4 * s), (x - 0.32 * s, y - 0.42 * s)], rgb(WHITE, 140), None, 0)
    c.poly([(x - 0.08 * s, y - 0.2 * s), (x + 0.08 * s, y - 0.2 * s), (x + 0.1 * s, y + 0.15 * s), (x - 0.1 * s, y + 0.15 * s)], col, NAVY, ow)
    c.poly([(x - 0.4 * s, y + 0.15 * s), (x + 0.4 * s, y + 0.15 * s), (x + 0.45 * s, y + 0.35 * s), (x - 0.45 * s, y + 0.35 * s)], ARCH, NAVY, ow)
    c.line([(x - 0.4 * s, y + 0.25 * s), (x + 0.4 * s, y + 0.25 * s)], MAGENTA, max(1.5, s * 0.04))
    if emblem:
        ex, ey, r = x, y - 0.68 * s, 0.2 * s
        if emblem == "moon":
            c.circle(ex, ey, r, WHITE, None, 0)
            c.circle(ex + 0.4 * r, ey - 0.25 * r, 0.8 * r, col, None, 0)
        elif emblem == "nebula":
            pts = [(ex + r * (i / 30) * math.cos(i * 0.4), ey + r * (i / 30) * math.sin(i * 0.4)) for i in range(31)]
            c.line(pts, WHITE, max(1.5, r * 0.18))
        elif emblem == "nova":
            c.poly(star_pts(ex, ey, r * 1.1, r * 0.4, 8), WHITE, None, 0)
        elif emblem == "galaxy":
            for k in range(2):
                pts = [(ex + r * (i / 30) * math.cos(i * 0.3 + k * math.pi), ey + 0.6 * r * (i / 30) * math.sin(i * 0.3 + k * math.pi)) for i in range(31)]
                c.line(pts, WHITE, max(1.5, r * 0.14))


def pic_cups():
    c = Canvas(1280, 720)
    sky(c, (0, 0, 1280, 720), seed=21)
    cloud_sea(c, 600, seed=2)
    slant(c, 40, 30, 420, 56, rgb(NAVY, 220), MAGENTA, 0.3)
    itext(c, 250, 58, "SELECT A CUP", 32, WHITE, None, 0)
    x = 690
    for i, (n, sub) in enumerate((("Orbit", "108 km/h"), ("Hyper", "144 km/h"), ("Warp", "180 km/h"), ("Mirror", "locked"))):
        sel = i == 1
        slant(c, x, 34, 126, 50, MAGENTA if sel else rgb(NAVY, 190 if i < 3 else 120), WHITE if sel else None, 0.3)
        itext(c, x + 63, 52, n, 20, WHITE, None, 0)
        itext(c, x + 63, 72, sub, 12, PINK if sel else LGREY, None, 0)
        x += 138
    medals = [[GOLD, SILVER, None], [BRONZE, None, None], [None, None, None], [None, None, None]]
    for i, cup in enumerate(CUPS):
        x0 = 60 + i * 296
        sel = i == 1
        locked = i == 3
        top, bot, w = (118, 600, 270) if not sel else (104, 610, 270)
        c.shadow(lambda cc, o, col: cc.d.polygon(cc.P([(x0 + 30 + o[0], top + o[1]), (x0 + w + 30 + o[0], top + o[1]), (x0 + w + o[0], bot + o[1]), (x0 + o[0], bot + o[1])]), fill=col), (6, 10), 8, 70)
        card = [(x0 + 30, top), (x0 + w + 30, top), (x0 + w, bot), (x0, bot)]
        c.poly(card, rgb(NAVY, 230), MAGENTA if sel else WHITE, 4 if sel else 2)
        band = [(x0 + 30, top), (x0 + w + 30, top), (x0 + w + 21, top + 190), (x0 + 9, top + 190)]
        c.poly(band, cup[3], None, 0)
        c.poly([(x0 + 60, top), (x0 + 140, top), (x0 + 100, top + 190), (x0 + 20, top + 190)], rgb(WHITE, 40), None, 0)
        trophy(c, x0 + 15 + w / 2, top + 150, 110, GOLD if not locked else LGREY, cup[0])
        itext(c, x0 + 12 + w / 2, top + 222, cup[1], 23, WHITE, None, 0)
        for k, tk in enumerate(cup[4]):
            y = top + 262 + k * 40
            nm = circuit_name(tk) if (not locked or k == 3) else "? ? ?"
            slant(c, x0 + 18 - k * 2.6, y, w - 20, 32, rgb(WHITE, 30), None, 0.3)
            c.d.polygon(c.P([(x0 + 24 - k * 2.6, y + 4), (x0 + 30 - k * 2.6, y + 4), (x0 + 26 - k * 2.6, y + 28), (x0 + 20 - k * 2.6, y + 28)]), fill=rgb(CIRC[tk.rstrip("*")][4][2]))
            itext(c, x0 + 42 - k * 2.6, y + 16, nm, 15 if c.text_w(nm, 15, True, True) < w - 50 else 12, WHITE, None, 0, anchor="lm")
        for m in range(3):
            mx, my = x0 + w / 2 + (m - 1) * 50, bot - 36
            col = medals[i][m]
            c.poly([(mx, my - 14), (mx + 14, my), (mx, my + 14), (mx - 14, my)], col or rgb(WHITE, 40), NAVY if col else None, 1.5)
        if locked:
            c.poly(card, rgb(NAVY, 150), None, 0)
            slant(c, x0 + 30, top + 130, w - 30, 64, rgb(NAVY, 240), YELLOW, 0.2)
            itext(c, x0 + 15 + w / 2, top + 152, "LOCKED", 20, YELLOW, None, 0)
            itext(c, x0 + 15 + w / 2, top + 178, "a trophy in every other cup", 12, WHITE, None, 0)
    slant(c, 20, 670, 1240, 36, rgb(NAVY, 200), None, 0.3)
    x = 50
    for lab, col, txt in (("A", LIME, "Choose"), ("B", RED, "Back"), ("L", CYAN, "Class"), ("R", CYAN, "Class")):
        c.poly([(x - 10, 677), (x + 14, 677), (x + 10, 699), (x - 14, 699)], col, WHITE, 1.5)
        itext(c, x, 688, lab, 14, NAVY, None, 0)
        itext(c, x + 22, 688, txt, 15, WHITE, None, 0, anchor="lm")
        x += 60 + c.text_w(txt, 15, True, True)
    c.save("cup_select.png")


# ---- the sheets ----
def sheet_bg(c, title, sub):
    c.grad((0, 0, c.w, c.h), "#EAF3FF", "#D6E6FA")
    slant(c, c.w / 2 - 300, 20, 600, 52, NAVY, MAGENTA, 0.3)
    itext(c, c.w / 2, 46, title, 30, WHITE, None, 0)
    c.text(c.w / 2, 98, sub, 16, NAVY, None, 0, bold=False)


def card(c, box, fill=WHITE, edge=NAVY):
    x0, y0, x1, y1 = box
    pts = [(x0 + 14, y0), (x1, y0), (x1 - 14, y1), (x0, y1)]
    c.shadow(lambda cc, o, col: cc.d.polygon(cc.P([(p[0] + o[0], p[1] + o[1]) for p in pts]), fill=col), (4, 7), 6, 50)
    c.poly(pts, fill, edge, 2)


def pips(c, x, y, label, v, col):
    c.text(x, y, label, 15, NAVY, None, 0, anchor="lm")
    for k in range(5):
        slant(c, x + 95 + k * 22, y - 8, 17, 16, col if k < v else rgb(NAVY, 30), NAVY if k < v else None, 0.4, 1)


DEMO = {"comet": "ember", "needle": "orchid", "titan": "verde", "manta": "glacier", "pulse": "volt", "bastion": "sunburst"}
# the six ships of the user's concept sheet: default parts and colours (main, trim, accent)
ROSTER = {"comet": ("swept", "central", HOTPINK, ORANGE, NYEL), "needle": ("ray", "pods", NCYAN, WHITE, NMAG),
          "titan": ("stub", "pods", ORANGE, NYEL, NTOWER2), "manta": ("stub", "pods", ORANGE, "#1FD1C1", WHITE),
          "pulse": ("stub", "tether", LIME, "#1FD1C1", HOTPINK), "bastion": ("twin", "ring", NYEL, NMAG, WHITE)}


def pic_hulls():
    c = Canvas(1600, 1000)
    sheet_bg(c, "THE SIX SHIPS", "The user's concept sheet: each hull with its default parts and colours (side and top, nose to the right).")
    for i, hk in enumerate(HULL_ORDER):
        H = HULLS[hk]
        wing, eng, m, t2, a = ROSTER[hk]
        x0, y0 = 40 + (i % 3) * 515, 135 + (i // 3) * 430
        card(c, (x0, y0, x0 + 495, y0 + 410), NTOWER, NCYAN)
        itext(c, x0 + 30, y0 + 30, "%d  %s" % (i + 1, H["name"]), 25, m, OUTL, 2, anchor="lm")
        c.text(x0 + 470, y0 + 22, H["fr"], 14, WHITE, None, 0, anchor="rm", bold=False)
        c.text(x0 + 470, y0 + 42, H["file"] + ".obj", 13, NCYAN, None, 0, anchor="rm", bold=False)
        ship_side(c, x0 + 250, y0 + 190, 56, hk, wing, eng, m, t2, a, i + 1)
        ship_top(c, x0 + 150, y0 + 315, 36, hk, wing, eng, m, t2, a, i + 1)
        for k, (lab, v) in enumerate(zip(("Speed", "Accel", "Handling", "Weight"), H["stats"])):
            c.text(x0 + 290, y0 + 262 + k * 30, lab, 15, WHITE, None, 0, anchor="lm")
            for j in range(5):
                slant(c, x0 + 385 + j * 20, y0 + 254 + k * 30, 15, 16, m if j < v else rgb(WHITE, 40), None, 0.4)
    c.save("hulls.png")


def pic_parts():
    c = Canvas(1600, 1120)
    sheet_bg(c, "WINGS, ENGINES, LIVERY", "Each part changes a stat by one pip at most; the final stats are clamped to 1..5.")
    itext(c, 40, 140, "WINGS", 24, MAGENTA, NAVY, 1, anchor="lm")
    for i, wk in enumerate(WING_ORDER):
        x0, y0 = 40 + i * 385, 160
        card(c, (x0, y0, x0 + 365, y0 + 330))
        c.text(x0 + 182, y0 + 28, WINGS[wk]["name"], 21, NAVY, None, 0)
        c.text(x0 + 182, y0 + 52, WINGS[wk]["fr"], 14, NAVY, None, 0, bold=False)
        ship_top(c, x0 + 182, y0 + 150, 42, "comet", wk, "central", ARCH, CYAN, MAGENTA, 1, fl=False)
        ship_side(c, x0 + 195, y0 + 278, 30, "comet", wk, "central", ARCH, CYAN, MAGENTA, 1, fl=False)
        slant(c, x0 + 30, y0 + 294, 300, 26, rgb(YELLOW, 120), None, 0.3)
        c.text(x0 + 182, y0 + 307, WINGS[wk]["mod"], 14, NAVY, None, 0)
    itext(c, 40, 530, "ENGINES", 24, CYAN, NAVY, 1, anchor="lm")
    for i, ek in enumerate(ENGINE_ORDER):
        x0, y0 = 40 + i * 385, 550
        card(c, (x0, y0, x0 + 365, y0 + 380))
        c.text(x0 + 182, y0 + 28, ENGINES[ek]["name"], 21, NAVY, None, 0)
        c.text(x0 + 182, y0 + 52, "%s  -  %s.obj" % (ENGINES[ek]["fr"], ENGINES[ek]["file"]), 13, NAVY, None, 0, bold=False)
        ship_side(c, x0 + 200, y0 + 180, 50, "comet", "stub", ek, RED, WHITE, YELLOW, 7)
        ship_top(c, x0 + 170, y0 + 290, 30, "comet", "stub", ek, RED, WHITE, YELLOW, 7)
        slant(c, x0 + 30, y0 + 342, 300, 26, rgb(YELLOW, 120), None, 0.3)
        c.text(x0 + 182, y0 + 355, ENGINES[ek]["mod"], 14, NAVY, None, 0)
    itext(c, 40, 965, "LIVERY PATTERNS", 24, LIME, NAVY, 1, anchor="lm")
    for k, ptn in enumerate(["plain", "stripe", "split", "chevrons", "checker", "fade"]):
        x = 40 + k * 258
        box = [(x + 14, 990), (x + 240, 990), (x + 226, 1080), (x, 1080)]
        cols = [(RED, WHITE, YELLOW), (MAGENTA, WHITE, CYAN), (CYAN, NAVY, YELLOW), (YELLOW, NAVY, MAGENTA), (LIME, NAVY, WHITE), (VIOLET, PINK, WHITE)][k]
        fill_livery(c, box, *cols, ptn)
        c.poly(box, None, NAVY, 2)
        slant(c, x + 70, 1068, 100, 26, WHITE, NAVY, 0.3, 1.5)
        c.text(x + 120, 1081, ptn, 14, NAVY, None, 0)
    c.save("parts.png")


def pic_pilots():
    c = Canvas(1600, 900)
    sheet_bg(c, "PILOTS AND TEAMS", "Human racers, always in helmets with closed visors; each team has its colours, number and logo.")
    for i, t in enumerate(TEAMS):
        key, name, ten, tfr, main, trim, acc, num, dflt = t
        x0, y0 = 40 + (i % 4) * 385, 140 + (i // 4) * 375
        card(c, (x0, y0, x0 + 365, y0 + 355), WHITE)
        c.poly([(x0 + 14, y0), (x0 + 365, y0), (x0 + 330, y0 + 225), (x0 - 20 + 14 * 0.36, y0 + 225)][:3] + [(x0 + 330, y0 + 225), (x0 + 4, y0 + 225)], main, None, 0)
        c.poly([(x0 + 230, y0), (x0 + 300, y0), (x0 + 265, y0 + 225), (x0 + 195, y0 + 225)], rgb(WHITE, 60), None, 0)
        team_logo(c, key, x0 + 320, y0 + 36, 20, trim if trim != main else NAVY)
        itext(c, x0 + 50, y0 + 40, "%02d" % num, 30, trim if trim not in (main,) else NAVY, NAVY, 2)
        helmet(c, x0 + 182, y0 + 115, 58, key)
        c.d.rectangle(c.B((x0 + 4, y0 + 225, x0 + 336, y0 + 228)), fill=rgb(NAVY))
        itext(c, x0 + 182, y0 + 258, name.upper(), 23, NAVY, None, 0)
        c.text(x0 + 182, y0 + 288, "Team %s  /  Équipe %s" % (ten, tfr), 15, NAVY, None, 0, bold=False)
        for k, col in enumerate((main, trim, acc)):
            c.poly([(x0 + 130 + k * 36, y0 + 312), (x0 + 146 + k * 36, y0 + 328), (x0 + 130 + k * 36, y0 + 344), (x0 + 114 + k * 36, y0 + 328)], col, NAVY, 1.5)
        c.text(x0 + 240, y0 + 328, main, 12, NAVY, None, 0, anchor="lm", bold=False)
    c.save("pilots.png")


def pic_items():
    c = Canvas(1600, 860)
    sheet_bg(c, "GADGETS", "From the item capsules (top right). Nothing violent: a hit ship spins in an energy bubble, then carries on.")
    tagcol = {"helps": LIME, "hinders": ORANGE, "catch-up": CYAN, "hits leader": VIOLET}
    for i, (k, en, fr, tag, what) in enumerate(GADGETS):
        x0, y0 = 30 + (i % 5) * 310, 140 + (i // 5) * 345
        card(c, (x0, y0, x0 + 290, y0 + 325))
        c.poly([(x0 + 70, y0 + 40), (x0 + 240, y0 + 40), (x0 + 220, y0 + 180), (x0 + 50, y0 + 180)], rgb(tagcol[tag], 70), None, 0)
        gadget_icon(c, k, x0 + 145, y0 + 110, 58)
        c.text(x0 + 145, y0 + 212, en, 21, NAVY, None, 0)
        c.text(x0 + 145, y0 + 238, fr, 14, NAVY, None, 0, bold=False)
        slant(c, x0 + 85, y0 + 254, 120, 26, tagcol[tag], NAVY, 0.3, 1.5)
        itext(c, x0 + 145, y0 + 267, tag.upper(), 12, NAVY, None, 0)
        c.text(x0 + 145, y0 + 302, what, 14, NAVY, None, 0, bold=False)
    capsule(c, 1530, 60, 30, 0.5)
    c.save("items.png")


def pic_circuits():
    c = Canvas(1600, 1560)
    sheet_bg(c, "CIRCUITS AND CUPS", "Top views of the 13 layouts (CIRCUITS in mockups.py); teal with magenta stripes = a tube section.")
    y = 125
    for ci, cup in enumerate(CUPS):
        slant(c, 30, y, 1530, 44, cup[3], NAVY, 0.3)
        trophy(c, 70, y + 38, 30, GOLD, cup[0])
        itext(c, 110, y + 22, "%s  /  %s" % (cup[1], cup[2]), 21, WHITE, NAVY, 2, anchor="lm")
        y += 56
        for k, tk in enumerate(cup[4]):
            key = tk.rstrip("*")
            K = CIRC[key]
            skyc, haze, track, acc = K[4]
            x0 = 30 + k * 388
            c.grad((x0, y, x0 + 372, y + 270), skyc, haze, 6)
            c.rrect((x0, y, x0 + 372, y + 270), 6, None, NAVY, 2)
            draw_circuit(c, key, (x0 + 30, y + 50, x0 + 342, y + 240), 12)
            itext(c, x0 + 16, y + 22, circuit_name(tk), 19, WHITE, NAVY, 2, anchor="lm")
            c.text(x0 + 356, y + 22, "%d m" % K[5], 14, WHITE, NAVY, 2, anchor="rm")
            c.text(x0 + 16, y + 255, K[2] + (" (inverse)" if tk.endswith("*") else ""), 13, NAVY, None, 0, anchor="lm", bold=False)
            if tk.endswith("*"):
                slant(c, x0 + 250, y + 236, 110, 24, MAGENTA, NAVY, 0.3, 1.5)
                itext(c, x0 + 305, y + 248, "REVERSED", 12, WHITE, None, 0)
        y += 290
    lx = 40
    for kind, lab in (("start", "start"), ("boost", "boost"), ("items", "capsules"), ("jump", "jump"), ("tunnel", "tunnel"), ("loop", "loop"),
                      ("tube", "tube"), ("hazard", "hazard"), ("bank", "banked")):
        feature_icon(c, kind, lx + 14, y + 6, 12)
        c.text(lx + 34, y + 6, lab, 15, NAVY, None, 0, anchor="lm")
        lx += 140
    c.line([(lx, y + 6), (lx + 30, y + 6)], YELLOW, 4)
    c.text(lx + 40, y + 6, "shortcut", 15, NAVY, None, 0, anchor="lm")
    c.save("circuits.png")


def pic_palette():
    c = Canvas(1600, 900)
    sheet_bg(c, "PALETTE", "Flat cel-shaded colours: blue sky, white architecture, mint/teal tracks, magenta rails, vivid team colours.")
    master = [("navy", NAVY), ("sky top", SKYTOP), ("sky low", SKYLOW), ("cloud", CLOUD), ("cloud shade", CLOUDSH), ("arch white", ARCH),
              ("light grey", LGREY), ("steel blue", STEEL), ("track mint", MINT), ("track light", MINTL), ("teal", TEAL), ("lime", LIME),
              ("magenta", MAGENTA), ("pink", PINK), ("cyan", CYAN), ("yellow", YELLOW), ("orange", ORANGE), ("red", RED), ("violet", VIOLET), ("canopy", CANOPY)]
    for i, (n, h) in enumerate(master):
        x, y = 50 + (i % 10) * 152, 150 + (i // 10) * 170
        c.poly([(x + 20, y), (x + 130, y), (x + 110, y + 90), (x, y + 90)], h, NAVY, 2)
        c.text(x + 65, y + 110, n, 15, NAVY, None, 0)
        c.text(x + 65, y + 130, h.upper(), 13, NAVY, None, 0, bold=False)
    y = 520
    c.text(50, y, "Circuits: sky / ground haze / track / accent", 18, NAVY, None, 0, anchor="lm")
    for i, K in enumerate(CIRCUITS):
        x = 50 + (i % 7) * 218
        yy = y + 40 + (i // 7) * 90
        for k, h in enumerate(K[4]):
            c.poly([(x + k * 46 + 8, yy), (x + k * 46 + 44, yy), (x + k * 46 + 36, yy + 36), (x + k * 46, yy + 36)], h, NAVY, 1.5)
        c.text(x, yy + 54, K[1], 13, NAVY, None, 0, anchor="lm")
    y = 760
    c.text(50, y, "Teams", 18, NAVY, None, 0, anchor="lm")
    for i, t in enumerate(TEAMS):
        x = 50 + i * 190
        for k, h in enumerate(t[4:7]):
            c.poly([(x + k * 40 + 8, y + 24), (x + k * 40 + 38, y + 24), (x + k * 40 + 30, y + 54), (x + k * 40, y + 54)], h, NAVY, 1.5)
        c.text(x, y + 72, t[2], 13, NAVY, None, 0, anchor="lm")
    c.save("palette.png")


def pic_proposals():
    c = Canvas(1600, 640)
    sheet_bg(c, "ART DIRECTION", "Chosen: Sky Circuit. The earlier proposals of the same day were too cute, too dark or too retro.")
    boxes = [(30, 130, 530, 620), (550, 130, 1050, 620), (1070, 130, 1570, 620)]
    x0, y0, x1, y1 = boxes[0]
    sky(c, boxes[0], seed=31)
    cloud_sea(c, y0 + 300, x0, x1)
    tower(c, x0 + 400, y0 + 300, 50, 180)
    tower(c, x0 + 460, y0 + 300, 34, 120, CYAN)
    track_band(c, [(x0, y0 + 470), (x0 + 250, y0 + 400), (x1, y0 + 340)], [(x0, y0 + 540), (x0 + 250, y0 + 460), (x1, y0 + 380)])
    e = ship_for("ember")
    ship_side(c, x0 + 230, y0 + 400, 46, **e)
    labels = [("A  Sky Circuit  -  chosen", MAGENTA), ("B  Candy Cosmos  -  too cute", PINK), ("C  Neon Nebula  -  too dark", VIOLET)]
    # B: the candy pastel draft
    x0, y0, x1, y1 = boxes[1]
    c.grad(boxes[1], "#9FD9FF", "#FFD1EC")
    for k in range(3):
        c.circle(x0 + 120 + k * 140, y0 + 330, 60 + k * 10, ["#FFB0D6", "#B5F2D0", "#FFE135"][k], NAVY, 3)
    c.circle(x0 + 380, y0 + 110, 50, "#FF9FCF", NAVY, 3)
    c.ellipse(x0 + 250, y0 + 250, 120, 45, "#FF4F9A", NAVY, 4)
    c.circle(x0 + 260, y0 + 215, 32, rgb("#BDF3FF", 200), NAVY, 3)
    # C: the neon night
    x0, y0, x1, y1 = boxes[2]
    c.grad(boxes[2], "#140F35", "#3A1E5C")
    for k in range(40):
        rnd = random.Random(k)
        c.circle(x0 + 20 + rnd.random() * 460, y0 + 20 + rnd.random() * 250, 1.5 + rnd.random() * 2, WHITE, None, 0)
    for k in range(7):
        c.line([(x0 + 10, y0 + 300 + k * 24), (x1 - 10, y0 + 300 + k * 24)], rgb("#FF3EA5", 150), 2)
    for k in range(9):
        c.line([((x0 + x1) / 2, y0 + 300), (x0 + k * 62, y1)], rgb("#2DE2E6", 150), 2)
    n = ship_for("zenith")
    ship_side(c, (x0 + x1) / 2, y0 + 290, 46, n["hull"], n["wing"], n["eng"], "#2A1E5C", "#2DE2E6", "#FF3EA5", 1)
    for b, (lab, col) in zip(boxes, labels):
        c.rrect(b, 6, None, NAVY, 3)
        slant(c, b[0] + 20, b[3] - 60, b[2] - b[0] - 40, 40, rgb(NAVY, 230), col, 0.3)
        itext(c, (b[0] + b[2]) / 2, b[3] - 40, lab, 18, WHITE, None, 0)
    c.save("proposals.png")


PICS = dict(title=pic_title, cup_select=pic_cups, garage=pic_garage, race_hud=pic_race, race_tube=pic_tube, race_split4=pic_split,
            hulls=pic_hulls, parts=pic_parts, pilots=pic_pilots, items=pic_items, circuits=pic_circuits, palette=pic_palette,
            proposals=pic_proposals)


def main():
    want = sys.argv[1:]
    for name, fn in PICS.items():
        if not want or any(w in name for w in want):
            fn()


if __name__ == "__main__":
    main()
