#!/usr/bin/env python3
"""forge_scene.py -- the events of 3DForge's screenshots (shots.sh): each scene a script of clicks and moves for the
desktop simulator, on the sample part SD:/docs/3d/bracket.3df. A point of the model is turned into the pixel the
app shows it at -- its home view of the bracket (the camera of user/Apps/3dforge/frender.h), or the view from above
a sketch on the plate's top face is drawn in.

    python3 tools/tests/desktop_sim/forge_scene.py main | shapes | box | cut | sketch | fillet | export
                                                   | cam | cam-ops | cam-sim | cam-gcode        (Manufacture)
                                                   | print | print-supports | print-layers   (... for a resin printer)
                                                   | fdm | fdm-layers                         (... for a filament printer)
"""
import math, sys

W, H = 1006, 621				# the client area on the simulator's screen
VX, VY, VW, VH = 8, 70, W - 8 - 232, H - 70 - 35 - 52	# the view
S = min (VW, VH) * 0.82 / math.sqrt (90 * 90 + 60 * 60 + 45 * 45)	# fit (): pixels a millimetre

def P (x, y, z):
	"""the bracket's home view"""
	az, el = math.radians (-58), math.radians (28)
	r = (-math.sin (az), math.cos (az), 0); u = (-math.sin (el) * math.cos (az), -math.sin (el) * math.sin (az), math.cos (el))
	q = (x - 45, y - 30, z - 22.5)
	return "%d %d" % (VX + VW / 2 + sum (a * b for a, b in zip (q, r)) * S, VY + VH / 2 - sum (a * b for a, b in zip (q, u)) * S)
def T (x, y):
	"""from above, the view's centre on (45, 30)"""
	return "%d %d" % (VX + VW / 2 + (x - 45) * S, VY + VH / 2 - (y - 30) * S)
def ck (p): return "down %s;up %s;wait" % (p, p)
def mv (p): return "move %s" % p
def typed (s): return ";".join ("key 0x08" if c == "<" else "key 0x0D" if c == "!" else "key %s" % c for c in s)

SHAPES = ck ("310 30")			# the Shapes button; its fold-out's cells:
CELL = dict (box = "321 97", cyl = "393 97", sphere = "464 97", torus = "536 97", pyramid = "321 159", prism = "393 159", taper = "464 159")
TOOL = dict (sketch = "366 30", extrude = "419 30", fillet = "482 30", chamfer = "536 30", move = "590 30", union = "650 30", subtract = "705 30")
SK = dict (line = "194 30", rect = "253 30", circle = "311 30", arc = "362 30", arc3 = "414 30", spline = "466 30", point = "516 30", close = "567 30", finish = "930 28")
# Manufacture: the switch, its tools
CAM = dict (on = "220 42", setup = "302 31", tool = "352 31", clearing = "416 31", contour = "472 31", simulate = "540 31", gcode = "944 30")

def scene (name):
	w = "wait;wait"
	if name == "main": return w + ";wait"
	if name == "shapes": return ";".join ([w, "down 310 30;up 310 30;wait", mv (CELL["torus"]), "wait"])
	if name == "box":			# a box on the wall's top... the plate's top face: base, then the height pulled
		return ";".join ([w, SHAPES, ck (CELL["box"]), mv (P (70, 30, 8)), ck (P (70, 30, 8)), mv (P (80, 38, 8)), ck (P (80, 38, 8)), mv (P (80, 38, 26)), "wait;wait"])
	if name == "cut":			# a cylinder pushed into the plate: Subtract chosen
		return ";".join ([w, SHAPES, ck (CELL["cyl"]), mv (P (25, 42, 8)), ck (P (25, 42, 8)), mv (P (31, 42, 8)), ck (P (31, 42, 8)), mv (P (31, 42, -2)), "wait;wait"])
	if name == "sketch":			# on the plate's top face: a rectangle, a circle, then a run of lines being closed
		return ";".join ([w, ck (TOOL["sketch"]), mv (P (25, 42, 8)), ck (P (25, 42, 8)), "wait",
				  ck (SK["rect"]), mv (T (6, 34)), ck (T (6, 34)), mv (T (24, 46)), ck (T (24, 46)),
				  ck (SK["circle"]), mv (T (74, 40)), ck (T (74, 40)), mv (T (79, 40)), ck (T (79, 40)),
				  ck (SK["point"]), mv (T (45, 20)), ck (T (45, 20)),
				  ck (SK["spline"]), mv (T (8, 12)), ck (T (8, 12)), mv (T (22, 20)), ck (T (22, 20)), mv (T (34, 10)), ck (T (34, 10)), mv (T (45, 20)), ck (T (45, 20)), ck (T (45, 20)), "key 0x1B",
				  ck (SK["line"]), mv (T (36, 34)), ck (T (36, 34)), mv (T (58, 34)), ck (T (58, 34)), mv (T (58, 46)), ck (T (58, 46)), mv (T (36, 34)), "wait;wait"])
	if name == "fillet":			# the wall's top front edge chosen, its radius typed
		e = P (45, 52, 45)
		return ";".join ([w, ck (TOOL["fillet"]), mv (e), ck (e), typed ("<4"), "wait;wait"])
	if name == "export": return ";".join ([w, ck ("950 28"), "wait;wait"])
	# Manufacture: the setup; a clearing then a contour; what is left of the stock; the G-code
	cam = ";".join ([w, ck (CAM["on"]), "wait;wait"])
	ops = ";".join ([cam, ck (CAM["clearing"]), "wait;wait", ck (CAM["contour"]), "wait;wait"])
	if name == "cam": return cam
	if name == "cam-ops": return ops
	if name == "cam-sim": return ";".join ([ops, ck (CAM["simulate"]), "wait;wait"])
	if name == "cam-gcode": return ";".join ([ops, ck (CAM["gcode"]), "wait;wait"])
	# ... for a resin printer (the menu's "Process: Resin Printing", "Supports", "Generate Supports", "Layers": its
	# items 44 to 47): the body on the plate; lifted 5 mm and tilted 24 degrees, its supports; its layers
	pr = ";".join ([cam, "menu 44", w])
	tilt = ";".join ([ck ("926 353"), typed ("<5!"), "wait", ck ("926 380"), typed ("<24!")] + ["wait"] * 8)
	sup = ";".join ([pr, tilt, "menu 45", "wait", "menu 46", w])
	if name == "print": return pr
	if name == "print-supports": return sup
	if name == "print-layers": return ";".join ([sup, "menu 47"] + ["wait"] * 140 + ["move 753 380;down 753 380;move 753 372;wait;up 753 372", w])
	# ... for a filament printer ("Process: Filament Printing", "Filament": the menu's items 48, 49): its values; a
	# layer being played (the bar dragged, Play)
	fd = ";".join ([cam, "menu 48", w])
	if name == "fdm": return ";".join ([fd, "menu 49", w])
	if name == "fdm-layers": return ";".join ([fd, "menu 47", w, "move 748 425;down 748 425;move 748 420;wait;up 748 420;wait", ck ("748 461")] + ["wait"] * 28)
	raise SystemExit ("no such scene: " + name)

if __name__ == "__main__":
	print (scene (sys.argv[1] if len (sys.argv) > 1 else "main"), end = "")
