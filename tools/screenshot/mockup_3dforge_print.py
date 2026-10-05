#!/usr/bin/env python3
"""mockup_3dforge_print.py -- mock-ups of 3DForge's Manufacture for a resin printer (an Anycubic Photon Mono 2): the
setup on the plate, the resin's values, the supports, the layers looked at one by one and the file written. The
machine is chosen in the setup: a router (the G-code of mockup_3dforge_cam.py) or a printer -- each a "generator"
with its own tools. See docs/3dforge/README.md, "Print".

    python tools/screenshot/mockup_3dforge_print.py  -> docs/3dforge/mockups/3dforge-print-*.png

The body shown is the sample bracket, tilted and lifted; its supports are real ones made here the simple way the app
would (a pillar under each low point of a grid, a thin tip, a raft); the layer shown is a real section (Manifold).
"""
import os, sys, math
import numpy as np
import manifold3d as mf

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, HERE)
import mockup_3dforge as M
import mockup_3dforge_cam as C
from mockup_3dforge import (Canvas, A, mix, shade, button, field, checkbox, segmented, group, listbox, icon, tag, prow,
			    W, H, K, FACE, PANEL, FIELD, LINE, LINE2, TEXT, DIM, FAINT, ACC, SEL_SOFT, RED, AMBER, GREEN, WHITE, BODY, INK)
from mockup_3dforge_cam import cam_icon, head, sect, drop, row

AZ, EL = -58, 26
PX, PY, PZ = 143.36, 89.1, 165.0			# the plate, the height
LIFT, TILT = 5.0, 24.0
SUP = (176, 186, 204)

# ---- the body on the plate, its supports -------------------------------------------------------------------------------
def placed ():
	p = M.stage (9).rotate ([TILT, 0, 0]); lo, hi = p.bounding_box ()[:3], p.bounding_box ()[3:]
	return p.translate ([PX / 2 - (lo[0] + hi[0]) / 2, PY / 2 - (lo[1] + hi[1]) / 2, LIFT - lo[2]])
def lowest (part, pts):
	"""under each (x, y): the lowest height of the body there, and how flat it faces down (None: nothing above)"""
	m = part.to_mesh (); V = np.asarray (m.vert_properties)[:, :3]; T = np.asarray (m.tri_verts)
	a, b, c = V[T[:, 0]], V[T[:, 1]], V[T[:, 2]]; n = np.cross (b - a, c - a); out = []
	for (x, y) in pts:
		d = (b[:, 1] - c[:, 1]) * (a[:, 0] - c[:, 0]) + (c[:, 0] - b[:, 0]) * (a[:, 1] - c[:, 1])
		with np.errstate (divide = "ignore", invalid = "ignore"):
			u = ((b[:, 1] - c[:, 1]) * (x - c[:, 0]) + (c[:, 0] - b[:, 0]) * (y - c[:, 1])) / d
			v = ((c[:, 1] - a[:, 1]) * (x - c[:, 0]) + (a[:, 0] - c[:, 0]) * (y - c[:, 1])) / d
		ok = (np.abs (d) > 1e-9) & (u >= 0) & (v >= 0) & (u + v <= 1) & (n[:, 2] < 0)
		if not ok.any (): out.append (None); continue
		z = u * a[:, 2] + v * b[:, 2] + (1 - u - v) * c[:, 2]; z[~ok] = 1e9; k = int (np.argmin (z)); out.append (float (z[k]))
	return out
def supports (part, step = 7.0):
	lo, hi = part.bounding_box ()[:3], part.bounding_box ()[3:]
	pts = [(x, y) for x in np.arange (lo[0] + 2.5, hi[0] - 1, step) for y in np.arange (lo[1] + 2.5, hi[1] - 1, step)]
	zs = lowest (part, pts); parts = []; n = 0
	for (x, y), z in zip (pts, zs):
		if z is None or z > 26: continue
		n += 1; top = z - 1.6
		parts.append (mf.Manifold.cylinder (max (top - 0.8, 0.2), 0.7, 0.7, 12).translate ([x, y, 0.8]))
		parts.append (mf.Manifold.cylinder (1.9, 0.7, 0.22, 12).translate ([x, y, top]))
	raft = mf.CrossSection.square ([hi[0] - lo[0] + 6, hi[1] - lo[1] + 6]).translate ([lo[0] - 3, lo[1] - 3]).offset (-2, mf.JoinType.Round).offset (2, mf.JoinType.Round)
	parts.append (mf.Manifold.extrude (raft, 0.8))
	return mf.Manifold.batch_boolean (parts, mf.OpType.Add).as_original (), n

PART = placed ()
SUPS, NSUP = supports (PART)

# ---- the icons ---------------------------------------------------------------------------------------------------------
def p_icon (c, kind, x, y, s = 26, col = INK, acc = ACC, bg = FACE):
	u = s / 24.0; w = max (1.25, 1.7 * u); soft = mix (acc, WHITE, 0.62)
	P = lambda pts: [(x + a * u, y + b * u) for a, b in pts]
	if kind == "plate":			# the plate, a body on it
		c.poly (P ([(12, 13), (22, 17), (12, 21), (2, 17)]), mix (col, WHITE, 0.72), col, w)
		c.poly (P ([(12, 3), (17, 5.5), (17, 12), (12, 14.5), (7, 12), (7, 5.5)]), soft, col, w); c.line (P ([(7, 5.5), (12, 8), (17, 5.5)]), col, w); c.line (P ([(12, 8), (12, 14.5)]), col, w)
	elif kind == "resin":			# a drop
		c.poly (P ([(12, 3), (17.5, 12), (18, 15), (16.5, 19), (12, 21), (7.5, 19), (6, 15), (6.5, 12)]), soft, col, w)
		c.line (P ([(9.5, 14.5), (10.2, 17), (12, 18.2)]), acc, w)
	elif kind == "supports":		# pillars under a shape
		c.poly (P ([(3, 9), (21, 4), (21, 8), (3, 13)]), soft, col, w)
		for px_, top in ((6, 12.4), (12, 10.8), (18, 9.2)): c.line (P ([(px_, top + 1.2), (px_, 20)]), acc, w * 1.15)
		c.line (P ([(3, 20.5), (21, 20.5)]), col, w * 1.3)
	elif kind == "layers":			# sheets, one lit
		for i, yy in enumerate ((6, 11, 16)):
			c.poly (P ([(12, yy - 3), (21, yy), (12, yy + 3), (3, yy)]), acc if i == 1 else mix (col, WHITE, 0.78), col, w)
	elif kind == "file":
		c.poly (P ([(5, 2), (14, 2), (19, 7), (19, 22), (5, 22)]), WHITE, col, w); c.line (P ([(14, 2), (14, 7), (19, 7)]), col, w)
		for i in range (3): c.rect (x + 8 * u, y + (11 + i * 3.4) * u, (8 - i * 2) * u, 1.6 * u, acc)
	else: cam_icon (c, kind, x, y, s, col, acc, bg)

# ---- the window ----------------------------------------------------------------------------------------------------------
WX, WY, WW, WH = 8, 32, 1008, 730
TILES = (("plate", "Setup"), ("resin", "Resin"), None, ("supports", "Supports"), None, ("layers", "Layers"))
OPS = [("plate", "Setup 1", "Bracket · Photon Mono 2"), ("supports", "Supports", "%d pillars · a raft" % NSUP)]

def shell (c, sel, hint, tool):
	M.desktop (c, "3DForge", ["File", "Edit", "View", "Create", "Modify", "Manufacture", "Help"])
	cx, cy, cw, ch = M.window (c, WX, WY, WW, WH, "3DForge — bracket.3df")
	x = cx + 8; y = cy + 4
	for k in ("new", "open", "save", None, "undo", "redo"):
		if k is None: c.vline (x + 3, y + 12, y + 44, LINE2); x += 9; continue
		icon (c, k, x + 4, y + 18, 20, INK if k != "redo" else FAINT); x += 28
	c.vline (x + 3, y + 6, y + 50, LINE2); x += 12
	# Design over Manufacture, as built
	sw = c.tw ("Manufacture", "uib") + 18; c.rect (x, y + 5, sw, 46, FIELD, r = 5, outline = LINE)
	c.text_c (x, y + 6, sw, 22, "Design", "ui", TEXT); c.rect (x + 1, y + 28, sw - 2, 22, ACC, r = 4); c.text_c (x, y + 28, sw, 22, "Manufacture", "uib", WHITE)
	x += sw + 8; c.vline (x, y + 6, y + 50, LINE2); x += 9
	for t in TILES:
		if t is None: c.vline (x + 4, y + 6, y + 50, LINE2); x += 10; continue
		k, name = t; bw = max (54, c.tw (name, "small") + 14)
		if k == tool: c.rect (x, y + 1, bw, 54, SEL_SOFT, r = 6, outline = ACC)
		p_icon (c, k, x + bw / 2 - 13, y + 5, 26, bg = SEL_SOFT if k == tool else FACE)
		c.text_c (x, y + 34, bw, 16, name, "small", TEXT); x += bw + 3
	bx = cx + cw - 132; button (c, bx, y + 14, 122, 28, "", accent = True)
	p_icon (c, "file", bx + 10, y + 19, 18, WHITE, WHITE); c.text_l (bx + 36, y + 14, 28, "Print file", "uib", WHITE)
	c.hline (cx, cx + cw, cy + 62, LINE2)
	sy = cy + ch - 27; c.hline (cx, cx + cw, sy, LINE2)
	icon (c, "info", cx + 10, sy + 5, 17, ACC); c.text_l (cx + 33, sy, 27, hint, "ui", (56, 58, 66))
	rx = cx + cw - 12
	for s in ("mm", "GPU", ".pm3n", "Photon Mono 2"):
		c.text_r (rx, sy, 27, s, "small", DIM); rx -= c.tw (s, "small") + 12; c.vline (rx + 5, sy + 7, sy + 20, LINE2); rx -= 7
	top = cy + 70; bh = ch - 70 - 35
	view = (cx + 8, top, cw - 8 - 232, bh - 52); time = (cx + 8, top + bh - 46, cw - 8 - 232, 46); right = (cx + cw - 224, top, 216, bh)
	tx, ty, tw_, th = time; listbox (c, tx, ty, tw_, th)
	for i, (k, name, det) in enumerate (OPS):
		x0 = tx + 8 + i * 38
		if i == sel: c.rect (x0 + 1, ty + 5, 34, th - 10, SEL_SOFT, r = 6, outline = ACC)
		p_icon (c, k, x0 + 6, ty + 11, 24, bg = SEL_SOFT if i == sel else FIELD)
	lx = tx + tw_ - 258; c.vline (lx - 6, ty + 8, ty + th - 8, LINE2)
	c.text (lx, ty + 7, OPS[sel][1], "smallb", TEXT); c.text (lx, ty + 23, OPS[sel][2], "small", DIM)
	return view, right

def plate3d (c, view, sups = True, scale = 4.0):
	"""the plate, the room above it, the body and what holds it"""
	cam = M.Cam (view, AZ, EL, scale, (PX / 2, PY / 2, 22)); x, y, w, h = view
	c.grad (x, y, w, h, (247, 248, 250), (226, 230, 237), r = 5)
	M.draw_body (c, cam, M.box (0, 0, -1.2, PX, PY, 1.2), base = (198, 202, 210), shadow = False, edge = (110, 116, 128))
	for gx in np.arange (10, PX, 10): c.line ([cam.xy ((gx, 0, 0)), cam.xy ((gx, PY, 0))], A ((150, 156, 168), 110), 0.8)
	for gy in np.arange (10, PY, 10): c.line ([cam.xy ((0, gy, 0)), cam.xy ((PX, gy, 0))], A ((150, 156, 168), 110), 0.8)
	top = 62.0						# (the room goes on up to 165: its start is enough)
	for (a, b) in (((0, 0), (0, 0)), ((PX, 0), (PX, 0)), ((PX, PY), (PX, PY)), ((0, PY), (0, PY))): c.dash (cam.xy ((a[0], a[1], 0)), cam.xy ((a[0], a[1], top)), (150, 156, 168), 1.0)
	if sups: M.draw_body (c, cam, SUPS, base = SUP, shadow = False, edge = (96, 106, 126))
	M.draw_body (c, cam, PART, shadow = False)
	return cam
def chrome (c, view, caption):
	x, y, w, h = view
	M.view_cube (c, x + w - 56, y + 58, AZ, EL)
	bx, by = x + w - 40, y + 124
	c.rect (bx, by, 30, 88, A (WHITE, 215), r = 6, outline = (176, 180, 190))
	for i, k in enumerate (("home", "fit", "shaded")): icon (c, k, bx + 6, by + 6 + i * 28, 18, (70, 76, 90))
	M.triad (c, x + 34, y + h - 34, AZ, EL)
	cw = c.tw (caption, "small") + 22; c.rect (x + 10, y + 10, cw, 24, A (WHITE, 225), r = 12, outline = (176, 180, 190)); c.text_c (x + 10, y + 10, cw, 24, caption, "small", (60, 64, 76))
	c.rect (x, y, w, h, r = 5, outline = (150, 152, 160))
def phead (c, rect, kind, name, sub):
	x, y, w, h = rect; group (c, x, y, w, h)
	p_icon (c, kind, x + 12, y + 12, 26, bg = PANEL); c.text (x + 48, y + 11, name, "big"); c.text (x + 48, y + 31, sub, "small", DIM)
	c.hline (x + 10, x + w - 10, y + 52, (196, 196, 200))
	return x + 12, y + 62, w - 24

def shot_setup ():
	c = Canvas (); view, right = shell (c, 0, "The machine decides what Manufacture makes: G-code for a router, or the layers of a resin printer.", "plate")
	cam = plate3d (c, view, sups = False)
	a, b = cam.xy ((0, 0, 0)), cam.xy ((PX, 0, 0)); M.dim (c, a, b, (-10, 24), "143.4")
	a, b = cam.xy ((PX, 0, 0)), cam.xy ((PX, PY, 0)); M.dim (c, a, b, (30, 10), "89.1")
	chrome (c, view, "Setup 1 · Bracket on the plate")
	x, y, w = phead (c, right, "plate", "Setup 1", "what is made, on what")
	y = sect (c, x, y, "Machine"); drop (c, x, y, w, "Photon Mono 2 · resin"); y += 30
	for k, v in (("Screen", "4096 × 2560 · 35 µm"), ("Room", "143 × 89 × 165 mm"), ("File", ".pm3n")): c.text_l (x, y, 19, k, "small", DIM); c.text_r (x + w, y, 19, v, "small"); y += 18
	y += 8; y = sect (c, x, y, "Body"); drop (c, x, y, w, "Bracket"); y += 36
	y = sect (c, x, y, "On the plate")
	y = row (c, x, y, w, "X", "0", "mm"); y = row (c, x, y, w, "Y", "0", "mm"); y = row (c, x, y, w, "Lifted", "5", "mm", focus = True)
	y = row (c, x, y, w, "Tilted, X", "24", "°"); y = row (c, x, y, w, "Tilted, Y", "0", "°"); y = row (c, x, y, w, "Turned", "0", "°"); y += 4
	button (c, x, y, w, 26, "Lay a face on the plate…"); y += 34
	checkbox (c, x, y, "Mirror the picture", False); y += 26
	c.rect (x, y + 2, w, 40, mix (PANEL, WHITE, 0.45), r = 6, outline = (196, 196, 200))
	icon (c, "check", x + 8, y + 12, 17, GREEN); c.text (x + 32, y + 7, "90 × 71 × 51 mm: it fits", "small", (40, 110, 56)); c.text (x + 32, y + 22, "1 024 layers of 0.05 mm", "small", DIM)
	c.save ("3dforge-print-setup.png")

def shot_resin ():
	c = Canvas (); view, right = shell (c, 0, "The resin's values are kept under its name: the next part starts with them.", "resin")
	plate3d (c, view); chrome (c, view, "Resin · Standard · 0.05 mm")
	x, y, w = phead (c, right, "resin", "Resin", "exposures, the lift")
	drop (c, x, y, w - 60, "Standard"); button (c, x + w - 54, y, 54, 26, "Save"); y += 34
	y = sect (c, x, y, "Layers")
	y = row (c, x, y, w, "Height", "0.05", "mm", focus = True); y = row (c, x, y, w, "Exposure", "2.5", "s"); y = row (c, x, y, w, "Light off", "2", "s"); y += 4
	y = sect (c, x, y, "First layers")
	y = row (c, x, y, w, "How many", "5"); y = row (c, x, y, w, "Exposure", "25", "s"); y = row (c, x, y, w, "Then, over", "10", "lay."); y += 4
	y = sect (c, x, y, "Lift, after each layer")
	y = row (c, x, y, w, "Slowly", "2", "mm"); y = row (c, x, y, w, "... at", "1", "mm/s"); y = row (c, x, y, w, "Then", "4", "mm"); y = row (c, x, y, w, "... at", "4", "mm/s")
	y = row (c, x, y, w, "Back down at", "3", "mm/s")
	c.text (x, y + 2, "Standard, ABS-like, Plant-based: Anycubic's", "small", DIM); c.text (x, y + 17, "values for this printer. Yours: Save.", "small", DIM)
	c.save ("3dforge-print-resin.png")

def shot_supports ():
	c = Canvas (); view, right = shell (c, 1, "Click the body's underside to add a support, a support to remove it. Generate puts them where the body hangs.", "supports")
	cam = plate3d (c, view, scale = 4.6)
	p = cam.xy ((PX / 2 - 18, PY / 2 - 20, 9.5)); c.ellipse (p[0], p[1], 6, None, AMBER, 2); M.cursor (c, p[0] + 5, p[1] + 6); tag (c, p[0] + 78, p[1] - 14, "add a support here")
	chrome (c, view, "Supports · %d pillars · a raft" % NSUP)
	x, y, w = phead (c, right, "supports", "Supports", "what holds the body")
	button (c, x, y, w, 28, "Generate", accent = True); y += 36
	y = sect (c, x, y, "Where")
	y = row (c, x, y, w, "Overhang from", "45", "°", focus = True); y = row (c, x, y, w, "A pillar every", "7", "mm"); y += 2
	checkbox (c, x, y, "Also under low points", True); y += 24
	checkbox (c, x, y, "From the body too", False); y += 30
	y = sect (c, x, y, "Pillars")
	y = row (c, x, y, w, "Diameter", "1.4", "mm"); y = row (c, x, y, w, "Tip", "0.45", "mm"); y = row (c, x, y, w, "Into the body", "0.2", "mm"); y += 4
	y = sect (c, x, y, "Raft"); checkbox (c, x, y, "A raft under it all", True); y += 26
	y = row (c, x, y, w, "Thick", "0.8", "mm"); y = row (c, x, y, w, "Wider by", "3", "mm")
	c.rect (x, y + 2, w, 40, mix (PANEL, WHITE, 0.45), r = 6, outline = (196, 196, 200))
	icon (c, "check", x + 8, y + 12, 17, GREEN); c.text (x + 32, y + 7, "%d pillars · 1.9 ml more" % NSUP, "small", (40, 110, 56)); c.text (x + 32, y + 22, "nothing starts in mid-air", "small", DIM)
	c.save ("3dforge-print-supports.png")

def shot_layers ():
	c = Canvas (); view, right = shell (c, 1, "The layers as the screen will show them: white is lit. Drag the bar at the right, or use the wheel.", "layers")
	x, y, w, h = view; c.rect (x, y, w, h, (18, 20, 26), r = 5)
	# the screen, a real section of the body and its supports
	z = 14.0; both = PART + SUPS; polys = both.slice (z).to_polygons ()
	s = min ((w - 110) / PX, (h - 70) / PY); ox = x + 28; oy = y + 44
	c.rect (ox, oy, PX * s, PY * s, (0, 0, 0), outline = (70, 76, 92))
	def area (p): return sum (p[i - 1][0] * p[i][1] - p[i][0] * p[i - 1][1] for i in range (len (p)))
	for p in sorted (polys, key = lambda q: -abs (area (q))):
		c.poly ([(ox + float (a) * s, oy + (PY - float (b)) * s) for a, b in p], WHITE if area (p) > 0 else (0, 0, 0))
	c.text (ox, oy - 22, "Layer 280 of 1 024  ·  14.0 mm  ·  2.5 s", "small", (200, 204, 214))
	c.text (ox + PX * s, oy - 22, "4096 × 2560", "small", (120, 126, 140), "ra")
	c.text (ox, oy + PY * s + 8, "lit: 5.9 cm²", "small", (150, 156, 170))
	# the layers' bar
	bx = x + w - 44; by0 = y + 44; bh_ = PY * s
	c.rect (bx + 10, by0, 6, bh_, (60, 64, 78), r = 3); ky = by0 + bh_ * (1 - 280 / 1024.0)
	c.rect (bx + 10, ky, 6, by0 + bh_ - ky, ACC, r = 3); c.rect (bx + 2, ky - 7, 22, 14, WHITE, r = 5, outline = (120, 126, 140))
	c.text (bx + 13, by0 - 18, "1024", "small", (150, 156, 170), "ma"); c.text (bx + 13, by0 + bh_ + 6, "1", "small", (150, 156, 170), "ma")
	c.rect (x, y, w, h, r = 5, outline = (150, 152, 160))
	x, y, w = phead (c, right, "layers", "Layers", "what the screen shows")
	for k, v in (("Layers", "1 024"), ("Height", "51.2 mm"), ("Resin", "67.4 ml"), ("Time", "2 h 58")): c.text_l (x, y, 22, k, "ui", DIM); c.text_r (x + w, y, 22, v); y += 23
	y += 6; y = sect (c, x, y, "Checked")
	for s_, ok in (("It fits the plate and the room", True), ("Each layer rests on the last", True), ("The first layers lie on the plate", True), ("No closed hollow keeps resin", True)):
		icon (c, "check" if ok else "warn", x, y, 16, GREEN); c.text_l (x + 24, y, 16, s_, "small", (50, 52, 60)); y += 21
	y += 8; y = sect (c, x, y, "This layer")
	for k, v in (("Number", "280"), ("At", "14.0 mm"), ("Exposure", "2.5 s"), ("Lit", "5.9 cm²")): c.text_l (x, y, 21, k, "ui", DIM); c.text_r (x + w, y, 21, v); y += 22
	y += 8; checkbox (c, x, y, "Show it in the 3D view", False)
	rx, ry, rw, rh = right; button (c, rx + 12, ry + rh - 40, rw - 24, 28, "Write the print file…", accent = True)
	c.save ("3dforge-print-layers.png")

if __name__ == "__main__":
	shot_setup (); shot_resin (); shot_supports (); shot_layers ()
