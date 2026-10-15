#!/usr/bin/env python3
#
# import_glb.py -- Cosmo Zoom!'s ships made by a generative 3D tool (the user's GLB files, 2026-10-15: high-poly,
# textured, lighting baked into the texture) turned into the game's low-poly OBJ: the colours of the texture sorted
# into the game's materials (paint1 / paint2 the player's colours, slate, white), the mesh welded along its UV seams
# and reduced to ~3000 triangles, its faces turned outward (the generators count on double-sided drawing; the game
# draws one side), scaled and turned to the game's axes, its sockets found on its shape. A GLB already reduced
# (Blender's Decimate, ...) is kept as it is. A GLB without a texture is not handled yet.
#   python3 tools/cosmozoom/models/import_glb.py      (needs: pip install trimesh fast-simplification scipy pillow rtree)
# The GLB sources are in tools/cosmozoom/models/glb/ (not in git: 15-25 MB each); the OBJ they give are committed,
# so build_all.py leaves these hulls alone (IMPORTS).
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
#
import colorsys
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import czmodel as cz  # noqa: E402

GLB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "glb")

# name: (source, title, length (m), triangles, colour rules, default material)
# a rule: [material, hue min, hue max, saturation min, value min, saturation max] -- the first that fits
IMPORTS = {}           # (none now: the Nova and the Comet were remodelled in ships.py from their GLB)


def classify(c, rules, default):
    h, sat, val = colorsys.rgb_to_hsv(*c)
    hd = h * 360
    for name, h0, h1, s0, v0, s1 in rules:
        if h0 <= hd < h1 and s0 <= sat <= s1 and val >= v0:
            return name
    return default


def convert(src, length, target, rules, default):
    import trimesh
    import fast_simplification
    from scipy.spatial import cKDTree
    m = trimesh.load(src, force="mesh", process=False)
    V, F = np.asarray(m.vertices, float), np.asarray(m.faces)
    # each face's colour: the texture at its corners and centre (the lighting baked in: a pale pink is a lit magenta)
    img = np.asarray(m.visual.material.baseColorTexture.convert("RGB")).astype(float) / 255
    H, W = img.shape[:2]
    fuv = np.asarray(m.visual.uv)[F]
    pts = np.concatenate([fuv, fuv.mean(axis=1, keepdims=True)], axis=1)
    u = np.clip((pts[..., 0] % 1) * (W - 1), 0, W - 1).astype(int)
    v = np.clip((1 - pts[..., 1] % 1) * (H - 1), 0, H - 1).astype(int)
    fcls = np.array([classify(c, rules, default) for c in img[v, u].mean(axis=1)])
    cent = V[F].mean(axis=1)
    # the axes: the longest horizontal one becomes Z (glTF is Y-up), the nose (the orange end, else the narrower
    # end) +Z
    ext = V.max(0) - V.min(0)
    ax = 0 if ext[0] >= ext[2] else 2
    mid = (V.max(0) + V.min(0)) / 2
    org = cent[fcls == "paint2"]
    if len(org) > 50:
        sgn = 1 if np.median(org[:, ax]) > mid[ax] else -1
    else:
        lo, hi = V[:, ax].min(), V[:, ax].max()
        t = (hi - lo) * 0.12
        o = 2 - ax
        sgn = 1 if np.ptp(V[V[:, ax] > hi - t][:, o]) < np.ptp(V[V[:, ax] < lo + t][:, o]) else -1
    R = np.array([[0, 0, -sgn], [0, 1, 0], [sgn, 0, 0]], float) if ax == 0 else np.diag([sgn, 1, sgn]).astype(float)
    # weld along the UV seams (else the reduction opens holes between the texture's islands), reduce
    w = trimesh.Trimesh(V, F, process=False)
    w.merge_vertices(merge_tex=True, merge_norm=True)
    if len(w.faces) > target * 1.05:
        Vd, Fd = fast_simplification.simplify(np.asarray(w.vertices, np.float32), np.asarray(w.faces, np.int32),
                                              1 - target / len(w.faces))
    else:                                    # (a model already reduced, e.g. in Blender: kept as it is)
        Vd, Fd = w.vertices, w.faces
    Vd, Fd = np.asarray(Vd, float), np.array(Fd)
    _, idx = cKDTree(cent).query(Vd[Fd].mean(axis=1), k=15)
    # a new face's material: the most of its 15 nearest original faces'
    dcls = []
    for row in idx:
        ks, c = np.unique(fcls[row], return_counts=True)
        dcls.append(ks[np.argmax(c)])
    dcls = np.array(dcls)
    # outward: as the nearest original face (the file's normals); where walls lie close together (hollow intakes)
    # the nearest disagree: both sides
    fn0 = np.asarray(m.vertex_normals)[F].sum(axis=1)
    fn0 /= np.linalg.norm(fn0, axis=1, keepdims=True) + 1e-12
    P0 = Vd[Fd]
    fnd = np.cross(P0[:, 1] - P0[:, 0], P0[:, 2] - P0[:, 0])
    fnd /= np.linalg.norm(fnd, axis=1, keepdims=True) + 1e-12
    dots = (fn0[idx] * fnd[:, None, :]).sum(axis=2)
    flip = dots[:, 0] < 0
    # the 15 nearest disagree, or the nearest is nearly side-on: both sides (the cost: a few hundred triangles)
    both = (np.sign(dots) != np.sign(dots[:, :1])).any(axis=1) | (np.abs(dots[:, 0]) < 0.5)
    Fd[flip] = Fd[flip][:, ::-1]
    # and every face whose back can be seen from outside: rays from its centre, backward (a little spread), that
    # leave the hull without hitting it
    tm = trimesh.Trimesh(Vd, Fd, process=False)
    C = Vd[Fd].mean(axis=1)
    Nf = np.cross(Vd[Fd[:, 1]] - Vd[Fd[:, 0]], Vd[Fd[:, 2]] - Vd[Fd[:, 0]])
    Nf /= np.linalg.norm(Nf, axis=1, keepdims=True) + 1e-12
    rng = np.random.RandomState(1)
    seen = np.zeros(len(Fd), bool)
    for _ in range(5):
        d = -Nf + rng.normal(0, 0.35, Nf.shape)
        d /= np.linalg.norm(d, axis=1, keepdims=True)
        hit = tm.ray.intersects_any(C + d * 1e-3, d)
        seen |= ~hit
    both |= seen
    Fd = np.concatenate([Fd, Fd[both][:, ::-1]])
    dcls = np.concatenate([dcls, dcls[both]])
    # the game's axes and size: `length` long, the bottom at y 0.2, the back at z -1.86
    P = Vd @ R.T
    P *= length / np.ptp(P[:, 2])
    P[:, 0] -= (P[:, 0].max() + P[:, 0].min()) / 2
    P[:, 1] += 0.2 - P[:, 1].min()
    P[:, 2] += -1.86 - P[:, 2].min()
    return P, Fd, dcls


def sockets(P, Fd, dcls):
    """The sockets from the shape: the engine at the middle of the back, the pilot sunk under the canopy (the slate
    on top), the wing at the widest point of the back half, the pads under the hull."""
    zb = P[:, 2].min()
    back = P[(P[:, 2] < zb + 0.25) & (np.abs(P[:, 0]) < 0.6)]
    eng = (0.0, float(np.median(back[:, 1])) if len(back) else 0.42, float(zb))
    c = P[Fd].mean(axis=1)
    can = c[(dcls == "slate") & (np.abs(c[:, 0]) < 0.5) & (c[:, 1] > np.percentile(P[:, 1], 70))]
    if len(can):
        pil = (0.0, float(can[:, 1].max()) - 0.50, float(np.median(can[:, 2])))
    else:
        pil = (0.0, float(P[:, 1].max()) - 0.55, -0.4)
    rear = P[P[:, 2] < -0.6]
    k = np.argmax(rear[:, 0])
    wing = (float(rear[k, 0]) - 0.05, float(rear[k, 1]), float(rear[k, 2]))
    hw = float(np.abs(P[:, 0]).max())
    pads = [(0.30, 1.30), (-0.30, 1.30), (min(0.9, hw * 0.5), -1.30), (-min(0.9, hw * 0.5), -1.30)]
    return eng, pil, wing, pads


def write(name, title, P, Fd, dcls):
    eng, pil, wing, pads = sockets(P, Fd, dcls)
    path = os.path.join(cz.OUT_DIR, name + ".obj")
    with open(path, "w") as f:
        f.write("# %s.obj -- Cosmo Zoom!: %s\n" % (name, title))
        f.write("# Generated by tools/cosmozoom/models/import_glb.py from glb/%s: do not edit.\n" % IMPORTS[name][0])
        f.write("# Metres, +Y up, +Z forward (the left is +X), faces counter-clockwise seen from outside.\n")
        f.write("# MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).\n")
        f.write("mtllib cosmo.mtl\n")
        for k, (x, z) in enumerate(pads):
            f.write("#@ pad%d %.3f 0.000 %.3f\n" % (k + 1, x, z))
        f.write("#@ wing %.3f %.3f %.3f\n" % wing)
        f.write("#@ engine %.3f %.3f %.3f\n" % eng)
        f.write("#@ pilot %.3f %.3f %.3f\n" % pil)
        for p in P:
            f.write("v %.4f %.4f %.4f\n" % tuple(p))
        cur = None
        for i in np.argsort(dcls, kind="stable"):
            if dcls[i] != cur:
                cur = dcls[i]
                f.write("usemtl %s\n" % cur)
            f.write("f %d %d %d\n" % tuple(Fd[i] + 1))
    return len(Fd)


def build(names=None):
    out = {}
    for name, (src, title, length, target, rules, default) in IMPORTS.items():
        if names and name not in names:
            continue
        path = os.path.join(GLB_DIR, src)
        if not os.path.exists(path):
            print("(%s: %s not here, the committed OBJ kept)" % (name, src))
            continue
        P, Fd, dcls = convert(path, length, target, rules, default)
        out[name] = write(name, title, P, Fd, dcls)
    return out


if __name__ == "__main__":
    for n, t in build(sys.argv[1:] or None).items():
        print(n, t)
