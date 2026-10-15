#!/usr/bin/env python3
#
# import_cc0.py -- a few public-domain (CC0) models converted to Cosmo Zoom!'s format: the palette texture's colour
# baked into per-vertex colours ("v x y z r g b": the game's loader reads them), the model rescaled to metres and
# its origin moved to the centre of its base (y = 0 at its lowest point).
#   KayKit Space Base Bits by Kay Lousberg (CC0) -- www.kaylousberg.com
#   Toy Car Kit by Kenney (CC0) -- www.kenney.nl            (no cars: we race hover ships)
# The packs are not in the repository: set CZ_KAYKIT / CZ_KENNEY to their folders (defaults: /tmp/claude-0/refs/...);
# without them nothing is done and the files already made stay.
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). (This script;
# the models it makes stay CC0, credited in their header and in docs/cosmozoom/MODELS.md.)
#
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import czmodel as cz  # noqa: E402

SCRIPT = "import_cc0.py"
KAYKIT = os.environ.get("CZ_KAYKIT", "/tmp/claude-0/refs/kaykit/KayKit_Space_Base_Bits_1.0_SOURCE/Assets/obj")
KENNEY = os.environ.get("CZ_KENNEY", "/tmp/claude-0/refs/kenney/Models/OBJ format")
CREDIT = {"kaykit": "KayKit Space Base Bits by Kay Lousberg (CC0) -- www.kaylousberg.com",
          "kenney": "Toy Car Kit by Kenney (CC0) -- www.kenney.nl"}

# our name: (pack, source file, target height in metres, title)
IMPORTS = {
    "kk_basemodule_a": ("kaykit", "basemodule_A", 7.0, "a white base module (a round station block)"),
    "kk_basemodule_c": ("kaykit", "basemodule_C", 7.0, "a base module with an orange band"),
    "kk_dome": ("kaykit", "dome", 8.0, "a glass dome on a white base"),
    "kk_landingpad": ("kaykit", "landingpad_large", 3.0, "a large octagonal landing pad"),
    "kk_solarpanel": ("kaykit", "solarpanel", 2.5, "a solar panel"),
    "kk_structure_tall": ("kaykit", "structure_tall", 16.0, "a tall lattice structure"),
    "kk_water_storage": ("kaykit", "water_storage", 8.0, "storage tanks"),
    "kk_windturbine": ("kaykit", ["windturbine_tall", "windturbine_tall_fan:fan"], 16.0,
                       "a wind turbine (o fan: its blades, they spin about Z at the socket fan)"),
    "kk_cargo_stack": ("kaykit", "cargo_A_stacked", 3.0, "stacked cargo crates"),
    "kk_rock_a": ("kaykit", "rock_A", 3.0, "a rock"),
    "kk_rocks_a": ("kaykit", "rocks_A", 2.0, "a few rocks"),
    "kn_tree": ("kenney", "tree", 7.0, "a round tree"),
    "kn_tree_pine": ("kenney", "tree-pine", 9.0, "a pine tree"),
    "kn_cone": ("kenney", "item-cone", 0.8, "a traffic cone"),
}


def read_mtl(path):
    mats, cur = {}, None
    if not os.path.exists(path):
        return mats
    for line in open(path):
        t = line.split()
        if not t:
            continue
        if t[0] == "newmtl":
            cur = t[1]
            mats[cur] = {"kd": (1, 1, 1), "d": 1.0, "map": None}
        elif cur and t[0] == "Kd":
            mats[cur]["kd"] = tuple(float(x) for x in t[1:4])
        elif cur and t[0] == "d":
            mats[cur]["d"] = float(t[1])
        elif cur and t[0] == "map_Kd":
            mats[cur]["map"] = os.path.join(os.path.dirname(path), " ".join(t[1:]))
    return mats


def convert(name, pack, src, height, title):
    folder = KAYKIT if pack == "kaykit" else KENNEY
    srcs = src if isinstance(src, list) else [src]
    V, VC, VT, faces = [], [], [], []
    mats = {}
    for one in srcs:
        fname, _, grp = one.partition(":")
        path = os.path.join(folder, fname + ".obj")
        if not os.path.exists(path):
            return None
        read_one(path, folder, V, VC, VT, faces, mats, grp or None)
    return write_one(name, pack, srcs, height, title, V, VC, VT, faces, mats)


def read_one(path, folder, V, VC, VT, faces, mats, grp):
    cur = None
    v0, t0 = len(V), len(VT)
    for line in open(path):
        t = line.split()
        if not t:
            continue
        if t[0] == "mtllib":
            mats.update(read_mtl(os.path.join(folder, " ".join(t[1:]))))
        elif t[0] == "v":
            V.append([float(x) for x in t[1:4]])
            VC.append([float(x) for x in t[4:7]] if len(t) >= 7 else [1.0, 1.0, 1.0])
        elif t[0] == "vt":
            VT.append([float(x) for x in t[1:3]])
        elif t[0] == "usemtl":
            cur = t[1]
        elif t[0] == "f":
            vs, ts = [], []
            for w in t[1:]:
                a = w.split("/")
                vi = int(a[0])
                vs.append(v0 + vi - 1 if vi > 0 else len(V) + vi)
                if len(a) > 1 and a[1]:
                    ti = int(a[1])
                    ts.append(t0 + ti - 1 if ti > 0 else len(VT) + ti)
            faces.append((vs, ts, cur, grp))


def write_one(name, pack, srcs, height, title, V, VC, VT, faces, mats):
    V = np.array(V)
    images = {}

    def colour(ts, mname, vs):
        m = mats.get(mname, {"kd": (1, 1, 1), "d": 1.0, "map": None})
        kd = np.array(m["kd"])
        vc = np.mean([VC[i] for i in vs], axis=0)
        if m["map"] and ts:
            if m["map"] not in images:
                images[m["map"]] = Image.open(m["map"]).convert("RGB")
            im = images[m["map"]]
            uv = np.mean([VT[i] for i in ts], axis=0)
            x = int((uv[0] % 1.0) * (im.size[0] - 1) + 0.5)
            y = int((1.0 - uv[1] % 1.0) * (im.size[1] - 1) + 0.5)
            c = np.array(im.getpixel((x, y))) / 255.0
            return c * kd * vc, m["d"]
        return kd * vc, m["d"]

    lo, hi = V.min(axis=0), V.max(axis=0)
    s = height / max(hi[1] - lo[1], 1e-6)
    centre = np.array([(lo[0] + hi[0]) / 2, lo[1], (lo[2] + hi[2]) / 2])
    P = (V - centre) * s
    out = ["# %s.obj -- Cosmo Zoom!: %s (imported)" % (name, title),
           "# From: %s, %s -- public domain (CC0), with thanks." % (CREDIT[pack], ", ".join(x.split(":")[0] + ".obj" for x in srcs)),
           "# Converted by tools/cosmozoom/models/import_cc0.py: the texture's colours baked into the vertices, scaled",
           "# to metres (x %.3f), the origin at the base's centre." % s,
           "mtllib cosmo.mtl"]
    groups = {}
    for vs, ts, mname, grp in faces:
        if grp:
            pts = P[vs]
            groups.setdefault(grp, []).append(pts)
    for grp, lst in groups.items():
        allp = np.vstack(lst)
        c = (allp.min(axis=0) + allp.max(axis=0)) / 2
        out.append("#@ %s %.3f %.3f %.3f" % (grp, c[0], c[1], c[2]))
    body = {}
    verts = []
    ntri = 0
    for vs, ts, mname, grp in faces:
        c, d = colour(ts, mname, vs)
        k = len(verts)
        for i in vs:
            verts.append("v %.4f %.4f %.4f %.3f %.3f %.3f" % (P[i][0], P[i][1], P[i][2], c[0], c[1], c[2]))
        key = (grp or "", "glass" if d < 0.5 else "white")
        body.setdefault(key, []).append("f " + " ".join(str(k + 1 + j) for j in range(len(vs))))
        ntri += len(vs) - 2
    out += verts
    for (g, mt) in sorted(body):
        if g:
            out.append("o " + g)
        out.append("usemtl " + mt)
        out += body[(g, mt)]
    os.makedirs(cz.OUT_DIR, exist_ok=True)
    open(os.path.join(cz.OUT_DIR, name + ".obj"), "w").write("\n".join(out) + "\n")
    size = (hi - lo) * s
    return ntri, size


def build():
    """{name: (triangles, None, script)} for build_all (its table lists them apart)."""
    done = {}
    for name, (pack, src, h, title) in IMPORTS.items():
        r = convert(name, pack, src, h, title)
        if r:
            done[name] = (r[0], None, SCRIPT)
            print("%-20s %5d tris  %5.1f x %5.1f x %5.1f m  <- %s %s" % (name, r[0], r[1][0], r[1][1], r[1][2], pack, src))
    return done


if __name__ == "__main__":
    build()
