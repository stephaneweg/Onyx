#!/usr/bin/env python3
#
# preview.py -- the modeller's previews: a model (or a whole ship assembled from its parts at the hull's sockets,
# the wing mirrored) rendered by the game's own renderer (tools/tests/cosmozoom/objview.cpp: the toon light, the
# glass, the glow), and the contact sheets of docs/cosmozoom/models/.
#   python3 tools/cosmozoom/models/preview.py <model> [<model>...]        -> <scratch>/<model>.png
#   python3 tools/cosmozoom/models/preview.py --ship hull wing engine pilot
#   python3 tools/cosmozoom/models/preview.py --sheets                     -> docs/cosmozoom/models/*.png
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
#
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import czmodel as cz  # noqa: E402

OBJVIEW = os.environ.get("OBJVIEW", "/tmp/objview")
SRC = os.path.join(cz.ROOT, "tools", "tests", "cosmozoom", "objview.cpp")
WORK = os.environ.get("CZ_PREVIEW", os.path.join(tempfile.gettempdir(), "czpreview"))
SHEETS = os.path.join(cz.ROOT, "docs", "cosmozoom", "models")


def ensure_objview():
    if not os.path.exists(OBJVIEW) or os.path.getmtime(OBJVIEW) < max(
            os.path.getmtime(SRC), max(os.path.getmtime(os.path.join(cz.ROOT, "user", "Apps", "cosmozoom", f))
                                       for f in os.listdir(os.path.join(cz.ROOT, "user", "Apps", "cosmozoom")) if f.endswith(".h"))):
        subprocess.check_call(["g++", "-std=gnu++17", "-O2", "-w", "-I" + os.path.join(cz.ROOT, "user"),
                               "-I" + os.path.join(cz.ROOT, "user", "Libs"), "-o", OBJVIEW, SRC])


PAINT = ("#E63ACB", "#FF8A1F")     # the preview's player colours (paint1, paint2)


def preview_mtl(folder, paint=None):
    """cosmo.mtl into folder, paint1 / paint2 in the given colours."""
    paint = paint or PAINT
    src = open(os.path.join(cz.OUT_DIR, "cosmo.mtl")).read().split("\n")
    out, cur = [], None
    for line in src:
        if line.startswith("newmtl "):
            cur = line[7:].strip()
        if line.startswith("Kd ") and cur in ("paint1", "paint2"):
            r, g, b = cz.hex_kd(paint[0 if cur == "paint1" else 1])
            line = "Kd %.3f %.3f %.3f" % (r, g, b)
        out.append(line)
    open(os.path.join(folder, "cosmo.mtl"), "w").write("\n".join(out))


def render(obj, png, size=320, paint=None):
    ensure_objview()
    os.makedirs(os.path.dirname(png) or ".", exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    if os.path.dirname(os.path.abspath(obj)) != os.path.abspath(WORK):
        shutil.copy(obj, os.path.join(WORK, os.path.basename(obj)))
        obj = os.path.join(WORK, os.path.basename(obj))
    preview_mtl(WORK, paint)
    ppm = png[:-4] + ".ppm"
    out = subprocess.run([OBJVIEW, obj, ppm, str(size)], capture_output=True, text=True)
    Image.open(ppm).save(png)
    os.remove(ppm)
    return out.stdout


def assemble(hull, wing, engine, pilot, out_obj):
    """One .obj of a whole ship (the parts at the hull's sockets; the wing mirrored to -X)."""
    d = cz.OUT_DIR
    HV, HF, HS = cz.read_obj(os.path.join(d, hull + ".obj"))
    S = dict(HS)
    parts = [(HV, HF)]
    if wing:
        WV, WF, _ = cz.read_obj(os.path.join(d, wing + ".obj"))
        w = np.array(S.get("wing", (0.6, 0.4, -0.3)))
        parts.append((WV + w, WF))
        M = WV * np.array([-1, 1, 1]) + w * np.array([-1, 1, 1])
        parts.append((M, [(tuple(reversed(i)), m, g) for i, m, g in WF]))
    if engine:
        EV, EF, ES = cz.read_obj(os.path.join(d, engine + ".obj"))
        parts.append((EV + np.array(S.get("engine", (0, 0.4, -1.4))), EF))
    if pilot:
        PV, PF, _ = cz.read_obj(os.path.join(d, pilot + ".obj"))
        parts.append((PV + np.array(S.get("pilot", (0, 0.9, 0))), PF))
    os.makedirs(os.path.dirname(out_obj), exist_ok=True)
    lines = ["mtllib cosmo.mtl"]
    body = []
    base = 0
    for V, F in parts:
        lines += ["v %.4f %.4f %.4f" % tuple(p) for p in V]
        mat = None
        for idx, m, g in F:
            if m != mat:
                body.append("usemtl " + m)
                mat = m
            body.append("f " + " ".join(str(i + 1 + base) for i in idx))
        base += len(V)
    open(out_obj, "w").write("\n".join(lines + body) + "\n")
    return out_obj


def ship_png(hull, wing, engine, pilot, png, size=320, paint=None):
    obj = assemble(hull, wing, engine, pilot, os.path.join(WORK, "ship_%s.obj" % hull))
    return render(obj, png, size, paint)


def model_png(name, png=None, size=320, paint=None):
    png = png or os.path.join(WORK, name + ".png")
    obj = name if name.endswith(".obj") else os.path.join(cz.OUT_DIR, name + ".obj")
    return render(obj, png, size, paint)


def font(sz):
    for f in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", os.path.join(cz.ROOT, "sdcard", "res", "fonts", "DejaVuSans-Bold.ttf")):
        if os.path.exists(f):
            return ImageFont.truetype(f, sz)
    return ImageFont.load_default()


def sheet(cells, out, cols=3, view=0, cell=320, title=""):
    """cells: [(png of 4 views, label, info)]; view: which of the 4 views (0 3/4 front, 1 side, 2 top, 3 back),
    or -1 for the whole 2x2."""
    imgs = []
    for png, label, info in cells:
        im = Image.open(png)
        S = im.size[0] // 2
        if view >= 0:
            im = im.crop(((view % 2) * S, (view // 2) * S, (view % 2) * S + S, (view // 2) * S + S))
        im = im.resize((cell, cell), Image.LANCZOS)
        imgs.append((im, label, info))
    rows = (len(imgs) + cols - 1) // cols
    th = 44 if title else 0
    W, H = cols * cell, rows * (cell + 40) + th
    sh = Image.new("RGB", (W, H), (250, 246, 255))
    dr = ImageDraw.Draw(sh)
    if title:
        dr.text((12, 8), title, fill=(43, 33, 64), font=font(24))
    for k, (im, label, info) in enumerate(imgs):
        x, y = (k % cols) * cell, (k // cols) * (cell + 40) + th
        sh.paste(im, (x, y))
        dr.text((x + 8, y + cell + 2), label, fill=(43, 33, 64), font=font(16))
        dr.text((x + 8, y + cell + 21), info, fill=(110, 100, 140), font=font(12))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sh.save(out)
    return out


def tri_info(stdout):
    # "x.obj: 640 triangles, size 1.70 x ..."
    try:
        head = stdout.splitlines()[0]
        n = head.split(":")[1].split("triangles")[0].strip()
        size = head.split("size ")[1].split(" (")[0]
        return "%s tris, %s m" % (n, size)
    except Exception:
        return ""


if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "--ship":
        out = os.path.join(WORK, "ship_%s.png" % a[1])
        print(ship_png(a[1], a[2], a[3], a[4] if len(a) > 4 else None, out), out)
    elif a and a[0] == "--sheets":
        import build_all
        build_all.sheets()
    else:
        for n in a:
            png = os.path.join(WORK, n + ".png")
            print(model_png(n, png), png)
