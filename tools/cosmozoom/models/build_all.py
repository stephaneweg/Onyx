#!/usr/bin/env python3
#
# build_all.py -- regenerates every Cosmo Zoom! model: the shared material file and every .obj of
# sdcard/apps/cosmozoom.app/models/ (ships.py, pilots.py, items.py, props.py; import_cc0.py for the CC0 props
# when its source packs are present), and with --sheets the contact sheets of docs/cosmozoom/models/ (rendered by
# the game's own renderer, tools/tests/cosmozoom/objview.cpp) and the triangle table of docs/cosmozoom/MODELS.md.
#   python3 tools/cosmozoom/models/build_all.py [--sheets]
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
#
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import czmodel as cz  # noqa: E402
import ships  # noqa: E402
import pilots  # noqa: E402

MODULES = [ships, pilots]
for opt in ("items", "city", "nature", "props"):
    try:
        MODULES.append(__import__(opt))
    except ImportError:
        pass

# The pilots' own ships (DESIGN.md 5) and colours, for the sheets.
CREW = [   # (pilot, hull, wing, engine, (paint1, paint2)): the concept sheet's six ships, then two more
    ("pilot_bolt", "hull_needle", "wing_solar", "engine_ion", ("#E63ACB", "#FF8A1F")),
    ("pilot_bloop", "hull_comet", "wing_stubby", "engine_pods", ("#3FD7EE", "#9DEFFA")),
    ("pilot_bruno", "hull_whale", "wing_swept", "engine_twin", ("#FF8A1F", "#FFD23F")),
    ("pilot_zib", "hull_pod", "wing_stubby", "engine_star", ("#9BE03A", "#22C3B5")),
    ("pilot_kiki", "hull_cube", "wing_stubby", "engine_ring", ("#FFD23F", "#E63ACB")),
    ("pilot_pepper", "hull_manta", "wing_solar", "engine_ion", ("#FF8A1F", "#22C3B5")),
    ("pilot_beep", "hull_cube", "wing_ray", "engine_pods", ("#22C3B5", "#FFF6E0")),
    ("pilot_squiggle", "hull_needle", "wing_ray", "engine_star", ("#8C52FF", "#FF4F9A")),
]


def build():
    cz.write_mtl()
    built = {}
    for mod in MODULES:
        for name, (ntri, mdl) in mod.build().items():
            built[name] = (ntri, mdl, mod.SCRIPT)
    try:
        import import_cc0
        for name, info in import_cc0.build().items():
            built[name] = info
    except Exception as e:  # the packs are not in the repository: skip quietly
        print("(CC0 imports skipped: %s)" % e)
    return built


def table(built):
    rows = ["| Model | Triangles | Size x × y × z (m) | Sockets | Groups | Script |", "|---|---|---|---|---|---|"]
    for name in sorted(built):
        ntri, mdl, script = built[name]
        if mdl is None:       # an imported file: read it back
            path = os.path.join(cz.OUT_DIR, name + ".obj")
            V, F, S = cz.read_obj(path)
            lo, hi = V.min(axis=0), V.max(axis=0)
            socks = ", ".join(n for n, _ in S) or "—"
            groups = ", ".join(sorted({g for _, _, g in F if g})) or "—"
        else:
            p = mdl.all()
            lo, hi = p.bbox()
            socks = ", ".join(n for n, _ in mdl.sockets) or "—"
            groups = ", ".join(g for g, _ in mdl.groups if g) or "—"
        s = hi - lo
        rows.append("| `%s` | %d | %.2f × %.2f × %.2f | %s | %s | %s |" % (name, ntri, s[0], s[1], s[2], socks, groups, script))
    return "\n".join(rows)


def update_doc(built):
    path = os.path.join(cz.ROOT, "docs", "cosmozoom", "MODELS.md")
    if not os.path.exists(path):
        return
    txt = open(path).read()
    a, b = "<!-- table -->", "<!-- /table -->"
    if a in txt and b in txt:
        i, j = txt.index(a) + len(a), txt.index(b)
        txt = txt[:i] + "\n" + table(built) + "\n" + txt[j:]
        open(path, "w").write(txt)


def sheets(built=None):
    import preview as pv
    W = pv.WORK
    os.makedirs(W, exist_ok=True)
    out = pv.SHEETS
    # the six hulls as whole ships, with their pilots
    cells = []
    seen = set()
    for pil, h, w, e, paint in CREW:
        if h in seen:
            continue
        seen.add(h)
        png = os.path.join(W, "sheet_%s.png" % h)
        info = pv.tri_info(pv.ship_png(h, w, e, pil, png, 400, paint))
        cells.append((png, "%s + %s + %s" % (h[5:], w[5:], e[7:]), info))
    pv.sheet(cells, os.path.join(out, "ships.png"), 3, 0, 400, "Cosmo Zoom! -- the six hulls, assembled (front 3/4)")
    pv.sheet(cells, os.path.join(out, "ships_back.png"), 3, 3, 400, "Cosmo Zoom! -- the six hulls (the chase camera's side)")
    # the engines on one hull, the wings on one hull
    cells = []
    for e in ("engine_ion", "engine_pods", "engine_twin", "engine_star", "engine_ring"):
        png = os.path.join(W, "sheet_%s.png" % e)
        info = pv.tri_info(pv.model_png(e, png, 400))
        cells.append((png, e, info))
    for wg in ("wing_stubby", "wing_ray", "wing_solar", "wing_swept"):
        png = os.path.join(W, "sheet_%s.png" % wg)
        info = pv.tri_info(pv.model_png(wg, png, 400))
        cells.append((png, wg, info))
    pv.sheet(cells, os.path.join(out, "parts.png"), 5, 0, 300, "Cosmo Zoom! -- the engines and the wings")
    cells = []
    for e in ("engine_ion", "engine_pods", "engine_twin", "engine_star", "engine_ring"):
        for h in ("hull_needle", "hull_manta"):
            png = os.path.join(W, "sheet_ship_%s_%s.png" % (h, e))
            info = pv.tri_info(pv.ship_png(h, "wing_swept", e, "pilot_pepper", png, 400, ("#FF8A1F", "#3A86FF")))
            cells.append((png, "%s + swept + %s" % (h[5:], e[7:]), info))
    pv.sheet(cells, os.path.join(out, "engines.png"), 4, 0, 320, "Cosmo Zoom! -- every engine on a narrow and a wide hull")
    # the pilots
    cells = []
    for pil, *_ in CREW:
        png = os.path.join(W, "sheet_%s.png" % pil)
        info = pv.tri_info(pv.model_png(pil, png, 300))
        cells.append((png, pil, info))
    pv.sheet(cells, os.path.join(out, "pilots.png"), 4, 0, 260, "Cosmo Zoom! -- the pilots")
    # the race objects, the props
    for mod, sheet_name, title in (("items", "items.png", "Cosmo Zoom! -- the race objects"),
                                   ("city", "city.png", "Cosmo Zoom! -- the city (walls paint1, bands and caps paint2: sample pastels)"),
                                   ("nature", "nature.png", "Cosmo Zoom! -- the floating islands, trees, rocks, clouds"),
                                   ("props", "props.png", "Cosmo Zoom! -- the other props")):
        m = sys.modules.get(mod)
        if not m:
            continue
        cells = []
        pastels = [("#FF8C7A", "#FFF6E0"), ("#FFE066", "#FFFFFF"), ("#9BE3B5", "#FFF6E0"), ("#C9B6FF", "#FFFFFF"),
                   ("#FFFFFF", "#8FD3FF"), ("#8FD3FF", "#FFFFFF")]
        for k_, name in enumerate(m.ALL):
            paint = pastels[k_ % 6] if mod == "city" else ("#3A86FF", "#FFE135")
            png = os.path.join(W, "sheet_%s.png" % name)
            info = pv.tri_info(pv.render(os.path.join(cz.OUT_DIR, name + ".obj"), png, 300, paint))
            cells.append((png, name, info))
        cols = 6 if len(cells) > 12 else 4
        pv.sheet(cells, os.path.join(out, sheet_name), cols, 0, 240, title)

    try:
        import import_cc0
        cells = []
        for name in import_cc0.IMPORTS:
            if os.path.exists(os.path.join(cz.OUT_DIR, name + ".obj")):
                png = os.path.join(W, "sheet_%s.png" % name)
                cells.append((png, name, pv.tri_info(pv.model_png(name, png, 300))))
        if cells:
            pv.sheet(cells, os.path.join(out, "cc0.png"), 5, 0, 220, "Imported CC0 props (KayKit Space Base Bits, Kenney Toy Car Kit)")
    except ImportError:
        pass
    print("sheets in", out)


if __name__ == "__main__":
    b = build()
    total = 0
    for name in sorted(b):
        print("%-22s %5d" % (name, b[name][0]))
    update_doc(b)
    if "--sheets" in sys.argv:
        sheets(b)
