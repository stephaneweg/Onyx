#!/usr/bin/env python3
"""mockup_3dforge_cam.py -- mock-ups of 3DForge's second part, Manufacture: G-code (GRBL) for a small CNC router
from a body -- the setup (the stock, the origin, the axes), the tool and the machine, the two operations (a
clearing in levels, a 2D contour), the G-code written. See docs/3dforge/README.md, "Manufacture".

    python tools/screenshot/mockup_3dforge_cam.py  -> docs/3dforge/mockups/3dforge-cam-*.png

The window is the app's as built (the timeline under the view, the selection at the right), in the Milk theme; the
drawing helpers are mockup_3dforge.py's. The tool paths shown are REAL ones for the sample bracket, computed here
with Manifold (the part sliced, its shadow at each level) and its Clipper2 (the offsets by the tool's radius): what
the app would compute the same way.
"""
import os, sys, math
import numpy as np
import manifold3d as mf

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, HERE)
import mockup_3dforge as M
from mockup_3dforge import (Canvas, A, mix, shade, lighten, button, field, checkbox, segmented, group, listbox, icon, cursor, tag, arrow, prow,
			    W, H, K, FACE, PANEL, FIELD, LINE, LINE2, TEXT, DIM, FAINT, ACC, SEL_SOFT, RED, AMBER, GREEN, WHITE, BODY, INK)

AZ, EL = -58, 28
PART = M.stage (9)
SX0, SY0, SX1, SY1, SZ1 = -5.0, -5.0, 95.0, 65.0, 47.0		# the stock: the body's box, 5 mm more at the sides, 2 on top
TOOL_R = 3.0
CUT = (30, 110, 220); RAPID = (226, 150, 44)

# ---- the icons of Manufacture ----------------------------------------------------------------------------------
def cam_icon (c, kind, x, y, s = 26, col = INK, acc = ACC, bg = FACE):
	u = s / 24.0; w = max (1.25, 1.7 * u); soft = mix (acc, WHITE, 0.62)
	P = lambda pts: [(x + a * u, y + b * u) for a, b in pts]
	if kind == "setup":			# the stock, its origin
		c.poly (P ([(12, 4), (20, 8), (12, 12), (4, 8)]), soft); c.poly (P ([(12, 4), (20, 8), (20, 16), (12, 20), (4, 16), (4, 8)]), None, col, w)
		c.line (P ([(4, 8), (12, 12), (20, 8)]), col, w); c.line (P ([(12, 12), (12, 20)]), col, w)
		c.line (P ([(4, 16), (4, 22)]), (58, 122, 214), w * 1.2); c.line (P ([(4, 16), (-1, 19)]), (214, 72, 62), w * 1.2); c.line (P ([(4, 16), (9, 19.5)]), (64, 160, 76), w * 1.2)
	elif kind == "tool":			# a flat end mill
		c.rect (x + 9 * u, y + 2 * u, 6 * u, 7 * u, mix (col, WHITE, 0.5), outline = col); c.rect (x + 9.5 * u, y + 9 * u, 5 * u, 12 * u, soft, outline = col)
		for i in range (3): c.line (P ([(9.5, 12 + i * 3.4), (14.5, 10 + i * 3.4)]), col, w * 0.8)
	elif kind == "clear":			# the levels cleared: rings going in
		for i, r in enumerate ((9, 6, 3)): c.rect (x + (12 - r) * u, y + (12 - r * 0.72) * u, 2 * r * u, 2 * r * 0.72 * u, None, r = 3 * u, outline = acc if i else col, width = w / K * 1.6)
		c.ellipse (x + 12 * u, y + 12 * u, 1.6 * u, col)
	elif kind == "contour":			# a shape, the path around it
		c.rect (x + 6 * u, y + 7 * u, 12 * u, 10 * u, soft, r = 2 * u, outline = col)
		c.rect (x + 2.5 * u, y + 3.5 * u, 19 * u, 17 * u, None, r = 5 * u, outline = acc, width = w / K * 1.8)
	elif kind == "post":			# a page of code
		c.poly (P ([(5, 2), (14, 2), (19, 7), (19, 22), (5, 22)]), WHITE, col, w); c.line (P ([(14, 2), (14, 7), (19, 7)]), col, w)
		for i in range (3): c.line (P ([(8, 11 + i * 3.4), (16 - i * 2, 11 + i * 3.4)]), acc, w)
	elif kind == "play": c.poly (P ([(7, 4), (20, 12), (7, 20)]), soft, col, w)
	elif kind == "design": icon (c, "box", x, y, s, col, acc, bg)

# ---- the window as built, in Manufacture ---------------------------------------------------------------------------
WX, WY, WW, WH = 8, 32, 1008, 730
OPS = [("setup", "Setup 1", "Bracket · stock 100 × 70 × 47"), ("clear", "Clearing 1", "Ø 6 flat · 7 levels"), ("contour", "Contour 1", "Ø 6 flat · outside · 5 passes")]

def shell (c, sel, hint, tool = None):
	M.desktop (c, "3DForge", ["File", "Edit", "View", "Create", "Modify", "Manufacture", "Help"])
	cx, cy, cw, ch = M.window (c, WX, WY, WW, WH, "3DForge — bracket.3df")
	x = cx + 8; y = cy + 4
	for k in ("new", "open", "save", None, "undo", "redo"):
		if k is None: c.vline (x + 3, y + 12, y + 44, LINE2); x += 9; continue
		icon (c, k, x + 4, y + 18, 20, INK if k != "redo" else FAINT); x += 28
	c.vline (x + 3, y + 6, y + 50, LINE2); x += 12
	# Design | Manufacture: the two halves of the app
	segmented (c, x, y + 14, 196, 28, ["Design", "Manufacture"], 1); x += 208
	c.vline (x, y + 6, y + 50, LINE2); x += 9
	for t in (("setup", "Setup"), ("tool", "Tool"), None, ("clear", "Clearing"), ("contour", "Contour"), None, ("play", "Simulate")):
		if t is None: c.vline (x + 4, y + 6, y + 50, LINE2); x += 10; continue
		k, name = t; bw = max (54, c.tw (name, "small") + 14)
		if k == tool: c.rect (x, y + 1, bw, 54, SEL_SOFT, r = 6, outline = ACC)
		cam_icon (c, k, x + bw / 2 - 13, y + 5, 26, bg = SEL_SOFT if k == tool else FACE)
		c.text_c (x, y + 34, bw, 16, name, "small", TEXT); x += bw + 3
	bx = cx + cw - 122; button (c, bx, y + 14, 112, 28, "", accent = True)
	cam_icon (c, "post", bx + 10, y + 19, 18, WHITE, WHITE); c.text_l (bx + 36, y + 14, 28, "G-code", "uib", WHITE)
	c.hline (cx, cx + cw, cy + 62, LINE2)
	sy = cy + ch - 27; c.hline (cx, cx + cw, sy, LINE2)
	icon (c, "info", cx + 10, sy + 5, 17, ACC); c.text_l (cx + 33, sy, 27, hint, "ui", (56, 58, 66))
	rx = cx + cw - 12
	for s in ("mm", "GPU", "GRBL", "Two Trees TTC 450"):
		c.text_r (rx, sy, 27, s, "small", DIM); rx -= c.tw (s, "small") + 12; c.vline (rx + 5, sy + 7, sy + 20, LINE2); rx -= 7
	top = cy + 70; bh = ch - 70 - 35
	view = (cx + 8, top, cw - 8 - 232, bh - 52); time = (cx + 8, top + bh - 46, cw - 8 - 232, 46); right = (cx + cw - 224, top, 216, bh)
	# the timeline: the setup and its operations
	tx, ty, tw_, th = time; listbox (c, tx, ty, tw_, th)
	for i, (k, name, det) in enumerate (OPS):
		x0 = tx + 8 + i * 38
		if i == sel: c.rect (x0 + 1, ty + 5, 34, th - 10, SEL_SOFT, r = 6, outline = ACC)
		cam_icon (c, k, x0 + 6, ty + 11, 24, bg = SEL_SOFT if i == sel else FIELD)
	lx = tx + tw_ - 258; c.vline (lx - 6, ty + 8, ty + th - 8, LINE2)
	c.text (lx, ty + 7, OPS[sel][1], "smallb", TEXT); c.text (lx, ty + 23, OPS[sel][2], "small", DIM)
	return view, right

def view_begin (c, view, scale = 4.6, target = (45, 30, 16), stock = True, fade = 0.0):
	cam = M.Cam (view, AZ, EL, scale, target); x, y, w, h = view
	c.grad (x, y, w, h, (247, 248, 250), (226, 230, 237), r = 5)
	M.ground (c, cam, -20, 120, -30, 90)
	M.draw_body (c, cam, PART, shadow = False, fade = fade, bg = (236, 239, 244))
	if stock:				# the stock: a see-through block around the body
		blk = M.box (SX0, SY0, 0, SX1 - SX0, SY1 - SY0, SZ1)
		M.draw_body (c, cam, blk, base = (236, 200, 130), alpha = 46, shadow = False, edge = (150, 110, 40))
	return cam
def view_end (c, view, caption):
	x, y, w, h = view
	M.view_cube (c, x + w - 56, y + 58, AZ, EL)
	bx, by = x + w - 40, y + 124
	c.rect (bx, by, 30, 88, A (WHITE, 215), r = 6, outline = (176, 180, 190))
	for i, k in enumerate (("home", "fit", "shaded")): icon (c, k, bx + 6, by + 6 + i * 28, 18, (70, 76, 90))
	M.triad (c, x + 34, y + h - 34, AZ, EL)
	cw = c.tw (caption, "small") + 22; c.rect (x + 10, y + 10, cw, 24, A (WHITE, 225), r = 12, outline = (176, 180, 190)); c.text_c (x + 10, y + 10, cw, 24, caption, "small", (60, 64, 76))
	c.rect (x, y, w, h, r = 5, outline = (150, 152, 160))

def origin_axes (c, cam, p, flipx = False):
	"""the work origin: X red, Y green, Z blue"""
	o = cam.xy (p)
	for v, col, n in (((1, 0, 0), (214, 72, 62), "X"), ((0, 1, 0), (64, 160, 76), "Y"), ((0, 0, 1), (58, 122, 214), "Z")):
		q = cam.xy ((p[0] + v[0] * 22, p[1] + v[1] * 22, p[2] + v[2] * 22)); arrow (c, o, q, col, 2.6, False, 10)
		t = cam.xy ((p[0] + v[0] * 27, p[1] + v[1] * 27, p[2] + v[2] * 27)); c.text (t[0], t[1], n, "uib", col, "mm")
	c.ellipse (o[0], o[1], 5, WHITE, (40, 44, 54), 2)

def path3 (c, cam, pts, col, width = 1.0, alpha = 255, closed = True):
	q = [cam.xy (p) for p in pts]
	if closed: q.append (q[0])
	c.line (q, A (col, alpha), width)
def loops (cs, z):
	return [[(float (p[0]), float (p[1]), z) for p in poly] for poly in cs.to_polygons ()]

# ---- the tool paths of the sample: the clearing's levels, the contour's passes --------------------------------------
def shadow_above (z):
	"""what the tool must stay out of at height z: the part's shadow from there up"""
	top = PART.trim_by_plane ([0, 0, 1], z + 1e-3)
	return top.project () if not top.is_empty () else mf.CrossSection ()
def clearing_paths (stepover = 2.4, levels = (41, 35, 29, 23, 17, 11, 8.2)):
	stock = mf.CrossSection.square ([SX1 - SX0 + 2 * TOOL_R, SY1 - SY0 + 2 * TOOL_R]).translate ([SX0 - TOOL_R, SY0 - TOOL_R])
	out = []
	for z in levels:
		keep = shadow_above (z); lv = []
		if keep.is_empty ():
			ring = stock.offset (-TOOL_R * 1.2, mf.JoinType.Round)
			while not ring.is_empty (): lv += loops (ring, z); ring = ring.offset (-stepover, mf.JoinType.Round)
		else:
			d = TOOL_R + 0.3
			while True:
				ring = keep.offset (d, mf.JoinType.Round, 2.0, 48) ^ stock
				band = stock - keep.offset (d - stepover * 0.5, mf.JoinType.Round, 2.0, 48)
				if band.is_empty (): break
				lv += loops (keep.offset (d, mf.JoinType.Round, 2.0, 48) ^ stock, z); d += stepover
				if d > 120: break
		out.append ((z, lv))
	return out
def contour_paths (zs = (6, 4, 2, 0, -0.5)):
	outline = PART.project ().offset (TOOL_R, mf.JoinType.Round, 2.0, 96)
	big = max (outline.to_polygons (), key = len)
	return [[(float (p[0]), float (p[1]), z) for p in big] for z in zs]

# ---- the panels ------------------------------------------------------------------------------------------------------
def head (c, rect, kind, name, sub):
	x, y, w, h = rect; group (c, x, y, w, h)
	cam_icon (c, kind, x + 12, y + 12, 26, bg = PANEL); c.text (x + 48, y + 11, name, "big"); c.text (x + 48, y + 31, sub, "small", DIM)
	c.hline (x + 10, x + w - 10, y + 52, (196, 196, 200))
	return x + 12, y + 62, w - 24
def sect (c, x, y, s): c.text (x, y, s, "uib"); return y + 21
def drop (c, x, y, w, s):
	c.grad (x, y, w, 26, (253, 253, 253), (232, 232, 234), r = 5); c.rect (x, y, w, 26, r = 5, outline = (146, 146, 152))
	c.text_l (x + 8, y, 26, s); c.poly ([(x + w - 17, y + 11), (x + w - 9, y + 11), (x + w - 13, y + 16)], TEXT)
def row (c, x, y, w, label, value, unit = None, fw = 86, focus = False): prow (c, x, y, w, label, value, unit, focus, fw = fw); return y + 29
def ok_cancel (c, rect):
	x, y, w, h = rect; button (c, x + 12, y + h - 40, 90, 28, "Cancel"); button (c, x + w - 102, y + h - 40, 90, 28, "OK", accent = True)

def shot_setup ():
	c = Canvas (); view, right = shell (c, 0, "Click one of the stock's 27 points to put the origin there; the arrows are the machine's X, Y and Z.", "setup")
	cam = view_begin (c, view)
	xs, ys, zs = (SX0, (SX0 + SX1) / 2, SX1), (SY0, (SY0 + SY1) / 2, SY1), (0, SZ1 / 2, SZ1)
	for z in zs:
		for yv in ys:
			for xv in xs:
				p = cam.xy ((xv, yv, z)); c.ellipse (p[0], p[1], 3.2, WHITE, (150, 110, 40), 1.4)
	o = (SX0, SY0, SZ1); origin_axes (c, cam, o)
	p = cam.xy (o); tag (c, p[0] + 96, p[1] + 40, "Origin: top, front left"); cursor (c, p[0] + 6, p[1] + 8)
	a, b = cam.xy ((SX0, SY0, 0)), cam.xy ((SX1, SY0, 0)); M.dim (c, a, b, (-10, 24), "100")
	a, b = cam.xy ((SX1, SY0, 0)), cam.xy ((SX1, SY1, 0)); M.dim (c, a, b, (30, 10), "70")
	view_end (c, view, "Setup 1 · the stock around Bracket")
	x, y, w = head (c, right, "setup", "Setup 1", "what is cut, out of what")
	y = sect (c, x, y, "Body"); drop (c, x, y, w, "Bracket"); y += 30
	c.text (x, y, "The other bodies are hidden.", "small", DIM); y += 24
	y = sect (c, x, y, "Stock"); segmented (c, x, y, w, 26, ["Around it", "Fixed size"], 0); y += 34
	y = row (c, x, y, w, "Sides, more", "5", "mm"); y = row (c, x, y, w, "Top, more", "2", "mm"); y = row (c, x, y, w, "Under, more", "0", "mm")
	c.text_l (x, y, 20, "Size", "ui", DIM); c.text_r (x + w, y, 20, "100 × 70 × 47 mm"); y += 28
	y = sect (c, x, y, "Origin")
	# the 27 points: three layers of nine
	for li, name in enumerate (("Top", "Middle", "Bottom")):
		gx = x + li * 66; c.text (gx + 27, y, name, "small", DIM, "ma")
		c.rect (gx, y + 16, 56, 44, FIELD, r = 4, outline = LINE)
		for j in range (3):
			for i in range (3):
				on = li == 0 and i == 0 and j == 2
				c.ellipse (gx + 10 + i * 18, y + 24 + j * 14, 4.2 if on else 3, ACC if on else (196, 198, 204), shade (ACC, 0.7) if on else (150, 152, 160))
	y += 70
	y = sect (c, x, y, "Axes")
	c.text_l (x, y, 26, "X goes"); drop (c, x + 62, y, 82, "right"); checkbox (c, x + 152, y + 5, "", False); y += 30
	c.text_l (x, y, 26, "Y goes"); drop (c, x + 62, y, 82, "back"); checkbox (c, x + 152, y + 5, "", False); y += 30
	c.text (x + 152, y - 76, "flip", "small", DIM); c.text_l (x, y, 20, "Z goes up, away from the stock.", "small", DIM)
	ok_cancel (c, right); c.save ("3dforge-cam-setup.png")

def shot_tool ():
	c = Canvas (); view, right = shell (c, 0, "The tool and the machine are kept as presets: the next part starts with them.", "tool")
	cam = view_begin (c, view)
	# the tool over the stock: a flat end mill
	t0 = (SX0 + 30, SY0 + 10, SZ1 + 6)
	M.draw_body (c, cam, M.cyl_z (t0[0], t0[1], t0[2], t0[2] + 22, 3), base = (190, 196, 206), shadow = False, edge = (70, 76, 90))
	M.draw_body (c, cam, M.cyl_z (t0[0], t0[1], t0[2] + 22, t0[2] + 30, 3.0), base = (120, 126, 138), shadow = False, edge = (60, 64, 76))
	a, b = cam.xy ((t0[0] - 3, t0[1], t0[2])), cam.xy ((t0[0] + 3, t0[1], t0[2])); M.dim (c, a, b, (0, 26), "Ø 6")
	a, b = cam.xy ((t0[0] + 3, t0[1], t0[2])), cam.xy ((t0[0] + 3, t0[1], t0[2] + 22)); M.dim (c, a, b, (40, 0), "22")
	origin_axes (c, cam, (SX0, SY0, SZ1))
	view_end (c, view, "Tool · 6 mm flat end mill")
	x, y, w = head (c, right, "tool", "Tool", "a flat end mill")
	y = sect (c, x, y, "Preset"); drop (c, x, y, w - 60, "6 flat · wood"); button (c, x + w - 54, y, 54, 26, "Save"); y += 36
	y = row (c, x, y, w, "Diameter", "6", "mm", focus = True); y = row (c, x, y, w, "Cutting length", "22", "mm"); y += 6
	y = sect (c, x, y, "Speeds")
	y = row (c, x, y, w, "Spindle", "12000", "rpm"); y = row (c, x, y, w, "Cutting feed", "1200", "/min"); y = row (c, x, y, w, "Plunge feed", "300", "/min")
	y = row (c, x, y, w, "Travel", "3000", "/min"); y += 6
	y = sect (c, x, y, "Machine"); drop (c, x, y, w, "Two Trees TTC 450"); y += 32
	for k, v in (("Travel", "460 × 460 × 80 mm"), ("Spindle", "0 – 24000 rpm"), ("G-code", "GRBL 1.1")): c.text_l (x, y, 20, k, "ui", DIM); c.text_r (x + w, y, 20, v); y += 21
	y += 6; checkbox (c, x, y, "Wait 3 s for the spindle", True)
	ok_cancel (c, right); c.save ("3dforge-cam-tool.png")

def draw_paths (c, cam, levels, emphasise = None):
	for i, (z, lv) in enumerate (levels):
		last = i == len (levels) - 1 if emphasise is None else i == emphasise
		for l in lv: path3 (c, cam, l, CUT, 1.3 if last else 0.8, 255 if last else 120)

def shot_clearing ():
	c = Canvas (); view, right = shell (c, 1, "Clearing: the stock removed level by level around the body, the tool never deeper than the level's step.", "clear")
	cam = view_begin (c, view, fade = 0.15)
	lev = clearing_paths (); draw_paths (c, cam, lev)
	# the way in: a rapid above the stock, down beside it
	st = lev[0][1][0][0]; top = (st[0], st[1], SZ1 + 10); o = (SX0, SY0, SZ1 + 10)
	c.dash (cam.xy (o), cam.xy (top), RAPID, 1.6); c.dash (cam.xy (top), cam.xy (st), RAPID, 1.6)
	origin_axes (c, cam, (SX0, SY0, SZ1))
	view_end (c, view, "Clearing 1 · 7 levels · 38 min")
	x, y, w = head (c, right, "clear", "Clearing 1", "roughing, level by level")
	c.text_l (x, y, 26, "Tool"); drop (c, x + 46, y, w - 46, "Ø 6 flat · wood"); y += 34
	y = sect (c, x, y, "Passes")
	y = row (c, x, y, w, "Step over", "2.4", "mm", focus = True); c.text (x, y - 3, "40 % of the tool", "small", DIM); y += 16
	y = row (c, x, y, w, "Step down", "6", "mm"); y = row (c, x, y, w, "Left on walls", "0.3", "mm"); y = row (c, x, y, w, "Left on floors", "0.2", "mm"); y += 4
	c.text_l (x, y, 26, "Cut"); segmented (c, x + w - 150, y, 150, 26, ["Climb", "Convent."], 0); y += 34
	c.text_l (x, y, 26, "Entry"); segmented (c, x + w - 150, y, 150, 26, ["Outside", "Helix"], 0); y += 36
	y = sect (c, x, y, "Heights")
	y = row (c, x, y, w, "Safe, above", "10", "mm"); y = row (c, x, y, w, "Retract", "3", "mm")
	c.text_l (x, y, 26, "Down to"); drop (c, x + w - 124, y, 124, "top of the plate"); y += 30
	y = row (c, x, y, w, "Lowest Z", "-38.8", "mm")
	c.rect (x, y + 2, w, 40, mix (PANEL, WHITE, 0.45), r = 6, outline = (196, 196, 200))
	icon (c, "check", x + 8, y + 12, 17, GREEN); c.text (x + 32, y + 7, "1 840 mm cut · 38 min", "small", (40, 110, 56)); c.text (x + 32, y + 22, "never below Z -38.8", "small", DIM)
	ok_cancel (c, right); c.save ("3dforge-cam-clearing.png")

def shot_contour ():
	c = Canvas (); view, right = shell (c, 2, "Contour: the tool follows the body's outline, its side on it, a pass a step down. Tabs hold the part at the end.", "contour")
	cam = view_begin (c, view, stock = False)
	# what the clearing left: shown as the body alone, the stock's footprint dashed
	for a, b in (((SX0, SY0), (SX1, SY0)), ((SX1, SY0), (SX1, SY1)), ((SX1, SY1), (SX0, SY1)), ((SX0, SY1), (SX0, SY0))):
		c.dash (cam.xy ((a[0], a[1], 0)), cam.xy ((b[0], b[1], 0)), (150, 110, 40), 1.2)
	cp = contour_paths ()
	for i, l in enumerate (cp): path3 (c, cam, l, CUT, 1.6 if i == len (cp) - 1 else 1.0, 255 if i == len (cp) - 1 else 150)
	# the ramp in, the tabs
	st = cp[0][0]; top = (st[0], st[1], SZ1 + 10)
	c.dash (cam.xy ((SX0, SY0, SZ1 + 10)), cam.xy (top), RAPID, 1.6); c.dash (cam.xy (top), cam.xy ((st[0], st[1], 8)), RAPID, 1.6)
	for (tx, ty) in ((45, -3), (45, 63), (-3, 30), (93, 30)):
		p = cam.xy ((tx, ty, 0.6)); c.rect (p[0] - 7, p[1] - 4, 14, 8, AMBER, r = 2, outline = shade (AMBER, 0.7))
	p = cam.xy ((45, -3, 0.6)); tag (c, p[0], p[1] + 20, "tab")
	origin_axes (c, cam, (SX0, SY0, SZ1))
	view_end (c, view, "Contour 1 · outside · 5 passes · 6 min")
	x, y, w = head (c, right, "contour", "Contour 1", "the outline, to its depth")
	c.text_l (x, y, 26, "Tool"); drop (c, x + 46, y, w - 46, "Ø 6 flat · wood"); y += 34
	y = sect (c, x, y, "Outline")
	drop (c, x, y, w, "The body, seen from above"); y += 32
	c.text_l (x, y, 26, "Side"); segmented (c, x + w - 150, y, 150, 26, ["Outside", "Inside"], 0); y += 32
	c.text_l (x, y, 26, "Cut"); segmented (c, x + w - 150, y, 150, 26, ["Climb", "Convent."], 0); y += 36
	y = sect (c, x, y, "Passes")
	y = row (c, x, y, w, "Step down", "2", "mm", focus = True)
	c.text_l (x, y, 26, "From"); drop (c, x + w - 124, y, 124, "top of the plate"); y += 30
	c.text_l (x, y, 26, "Down to"); drop (c, x + w - 124, y, 124, "under the body"); y += 30
	y = row (c, x, y, w, "... and", "0.5", "mm"); y += 2
	checkbox (c, x, y, "Ramp into each pass", True); y += 26
	checkbox (c, x, y, "Tabs", True); c.text_r (x + w, y, 16, "4 · 6 × 1.5 mm", "small", DIM); y += 30
	y = sect (c, x, y, "Heights"); y = row (c, x, y, w, "Safe, above", "10", "mm"); y = row (c, x, y, w, "Retract", "3", "mm")
	ok_cancel (c, right); c.save ("3dforge-cam-contour.png")

def shot_post ():
	c = Canvas (); view, right = shell (c, 2, "G-code: the operations in their order, checked, then written for GRBL.")
	cam = view_begin (c, view, fade = 0.15)
	draw_paths (c, cam, clearing_paths ())
	for l in contour_paths (): path3 (c, cam, l, CUT, 1.0, 160)
	origin_axes (c, cam, (SX0, SY0, SZ1)); view_end (c, view, "Setup 1 · 2 operations · 44 min")
	x, y, w = head (c, right, "setup", "Setup 1", "what is cut, out of what")
	for k, v in (("Body", "Bracket"), ("Stock", "100 × 70 × 47 mm"), ("Origin", "top, front left"), ("Operations", "2"), ("Time", "44 min")): c.text_l (x, y, 22, k, "ui", DIM); c.text_r (x + w, y, 22, v); y += 23
	# the dialog
	dw, dh = 560, 452; dx, dy = (W - dw) // 2, 150
	c.rect (0, 27, W, H - 27, A ((20, 28, 44), 70))
	fx, fy, fw, fh = M.window (c, dx, dy, dw, dh, "G-code", resizable = False, menu = False)
	x = fx + 20; y = fy + 14; w = fw - 40
	c.text_l (x, y, 28, "Name"); field (c, x + 80, y, w - 80, 28, "bracket.nc"); y += 36
	c.text_l (x, y, 28, "Folder"); field (c, x + 80, y, w - 80 - 86, 28, "SD:/docs/3d"); button (c, x + w - 78, y, 78, 28, "Browse…"); y += 40
	c.text (x, y, "Checked", "uib"); y += 22
	for s in ("No fast move through the stock or the body", "Nothing below Z −47.5 (the stock's underside, 0.5 mm more)", "Inside the machine's travel: 100 × 70 of 460 × 460 mm",
		  "The tool's cutting length (22 mm) covers the deepest cut (6 mm a level)"):
		icon (c, "check", x, y, 16, GREEN); c.text_l (x + 24, y, 16, s, "ui", (50, 52, 60)); y += 21
	y += 8; c.text (x, y, "The file's start", "uib"); c.text (x + w, y + 2, "4 212 lines · 86 KB · 44 min", "small", DIM, "ra"); y += 22
	c.rect (x, y, w, 128, (250, 250, 251), r = 4, outline = LINE)
	code = ["(3DForge - bracket.3df - Setup 1)", "(T1  6 mm flat end mill)", "G21 G90 G17 G94", "G0 Z10.000", "M3 S12000", "G4 P3", "(Clearing 1)", "G0 X-8.600 Y-8.600"]
	mono = M._f ("DejaVuSansMono.ttf", 11)
	for i, s in enumerate (code): c.d.text (((x + 10) * K, (y + 8 + i * 14.5) * K), s, font = mono, fill = (70, 76, 90) if s.startswith ("(") else TEXT)
	button (c, fx + fw - 238, fy + fh - 44, 92, 28, "Cancel"); button (c, fx + fw - 136, fy + fh - 44, 116, 28, "Save G-code", accent = True)
	c.save ("3dforge-cam-gcode.png")

if __name__ == "__main__":
	shot_setup (); shot_tool (); shot_clearing (); shot_contour (); shot_post ()
