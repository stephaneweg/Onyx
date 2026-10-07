#!/usr/bin/env python3
# tools/critters/mklevels.py -- Critters' twelve shipped levels (sdcard/apps/critters.app/levels), written in the
# .level format (02-product-analysis §7.1, docs/04 "Critters"): the Training pack (one role taught a level) and the
# Expedition pack (the roles mixed). Started from the UX Designer's drafts (AutoDev round 5,
# autodev/rounds/05-critters/mockups/mklevels.py: the names, the palettes, the ideas), then DRAWN AGAIN and TUNED with
# the headless runner (tools/critters/crsim.cpp): each level is lost when nothing is done, each Training level is lost
# without its role, and each is won by its recorded solution (tools/tests/critters/solutions/<level>.sol) -- the host
# test checks all three (sh tools/tests/run_critters_test.sh, AC-30, AC-31). Edit a level here, run this script, then
# the host test; a geometry change may move a solution's steps (crsim LEVEL SOL --search "builder 0" A B finds them).
#
#   python3 tools/critters/mklevels.py [folder]      -> <folder>/<pack>-<nn>-<name>.level (12 files)
#                                                      (default: sdcard/apps/critters.app/levels)
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
import math, os, sys

# ---- the palettes (04 §5.3): earth, its speckle, the grass / moss cap, the sky -------------------------------------
PAL = {
	"soil":  dict(earth="#8A5A34", e2="#6E4428", cap="#6E8A3C", sky="#101830"),
	"sand":  dict(earth="#C89A5C", e2="#A87C44", cap="#E0BC78", sky="#2A1A30"),
	"moss":  dict(earth="#5E6E3A", e2="#4A5A2C", cap="#86B04A", sky="#0E1C24"),
	"ice":   dict(earth="#7FA2C2", e2="#5F82A2", cap="#D8ECF8", sky="#0C1A30"),
	"cave":  dict(earth="#7A5E4A", e2="#5E4636", cap="#9A7A5E", sky="#141012"),
}
STEEL = dict(material="steel", colour="#8890A0", texture="bricks", colour2="#6A7282")
WATER = dict(material="water", colour="#3070D0", texture="stripes", colour2="#4A8AE8")
LAVA  = dict(material="lava",  colour="#E05020", texture="stripes", colour2="#FF8A30")
ERASE = dict(material="erase")

def pts(p):
	return ", ".join("%d %d" % (round(x), round(y)) for x, y in p)

def nbsp(v):		# French: a no-break space before : ? ! ; (the window never wraps a line before them)
	for c in ":?!;": v = v.replace(" " + c, "\u00a0" + c)
	return v

def block(_blk, **kv):
	s = "[%s]\n" % _blk
	for k, v in kv.items():
		s += "%-10s = %s\n" % (k.replace("_fr", ".fr"), nbsp(v) if k.endswith("_fr") else v)
	return s + "\n"

def rect(x, y, w, h):	return "%d %d %d %d" % (x, y, w, h)
def earth(p, **kv):	return block("shape", **kv, colour=p["earth"], texture="speckle", colour2=p["e2"])
def box(p, x, y, w, h, cap=True):
	"""an earth rectangle, with a 3-px grass / moss cap on its top"""
	s = earth(p, rect=rect(x, y, w, h))
	if cap: s += block("shape", rect=rect(x, y, w, 3), colour=p["cap"])
	return s
def steel(x, y, w, h):	return block("shape", rect=rect(x, y, w, h), **STEEL)
def water(x, y, w, h):	return block("shape", rect=rect(x, y, w, h), **WATER)
def lava(x, y, w, h):	return block("shape", rect=rect(x, y, w, h), **LAVA)
def hole(x, y, w, h):	return block("shape", rect=rect(x, y, w, h), **ERASE)
def poly(p, points, cap=False, **kv):
	s = earth(p, points=pts(points), **kv)
	return s

def ground(p, x0, x1, y, h=160, wave=0, period=80, seed=0, flat=()):
	"""earth from x0 to x1, its top at y (a gentle wave of `wave` px, none over the `flat` spans), down to h; a 3-px cap"""
	step = max(8, -(-(x1 - x0) // 29))			# (<= 31 points a side: the cap's polygon holds 2 x 31 <= 64, 02 §7.1.2)
	def top(x):
		if wave == 0 or any(a <= x <= b for a, b in flat): return y
		return y + round(wave * math.sin((x + seed) * 2 * math.pi / period))
	xs = list(range(x0, x1 + 1, step))
	if xs[-1] != x1: xs.append(x1)
	t = [(x, top(x)) for x in xs]
	s = earth(p, points=pts(t + [(x1, h), (x0, h)]))
	s += block("shape", points=pts(t + [(x, yy + 3) for x, yy in reversed(t)]), colour=p["cap"])
	return s

def head(name, name_fr, hint, hint_fr, w, count, save, time, rate=50, sky="#101830", **roles):
	return block("level", format=1, name=name, name_fr=name_fr, hint=hint, hint_fr=hint_fr, size="%d 160" % w,
		     count=count, save=save, time=time, rate=rate, **roles, background=sky)

def hatch(x, y, d="right"): return block("hatch", at="%d %d" % (x, y), dir=d)
def exit_(x, y): return block("exit", at="%d %d" % (x, y))
def label(x, y, en, fr, col="#FFFFFF"): return block("label", at="%d %d" % (x, y), text=en, text_fr=fr, colour=col)

# ---- the levels ------------------------------------------------------------------------------------------------------
def gen():
	L = {}

	# T1 -- the creatures pace on a floor closed by two steel walls; a digger opens the way to the cave and its exit
	p = PAL["soil"]
	L["training-01-straight-down"] = head("Straight Down", "Tout droit vers le bas",
		"Click Digger, then click a critter standing on the floor: it digs down to the cave and the others follow.",
		"Cliquez sur Creuseur, puis sur une bestiole posée sur le sol : elle creuse jusqu'à la grotte et les autres suivent.",
		400, 10, 8, 180, sky=p["sky"], digger=3) + \
		box(p, 10, 80, 380, 20) + steel(0, 36, 10, 64) + steel(390, 36, 10, 64) + \
		box(p, 0, 138, 400, 22) + \
		hatch(80, 50) + exit_(300, 137) + label(200, 92, "DIG", "CREUSEZ")

	# T2 -- a ravine with lava at its bottom; a builder's stair carries the stream over it
	L["training-02-mind-the-gap"] = head("Mind the Gap", "Attention à la marche",
		"Click Builder, then a critter walking near the edge: its stair carries the others across the gap.",
		"Cliquez sur Bâtisseur, puis sur une bestiole qui arrive au bord : son escalier fait passer les autres.",
		480, 10, 8, 180, rate=30, sky=p["sky"], builder=4) + \
		ground(p, 0, 200, 100, wave=2, flat=((150, 200),)) + ground(p, 216, 480, 100, wave=2, seed=40, flat=((216, 260), (400, 440))) + \
		lava(196, 140, 24, 20) + \
		hatch(50, 74) + exit_(420, 99) + label(100, 125, "BUILD", "CONSTRUISEZ")

	# T3 -- the hatch faces a lake; a blocker turns the stream back over a thin crust; a digger opens the cave with the exit
	L["training-03-hold-the-line"] = head("Hold the Line", "Tenir la ligne",
		"A Blocker stands still and turns the others back before the water. Then dig down to the cave with the exit.",
		"Un Bloqueur reste sur place et fait faire demi-tour aux autres avant l'eau. Creusez ensuite jusqu'à la grotte de la sortie.",
		480, 10, 6, 180, sky=p["sky"], blocker=2, digger=1) + \
		box(p, 0, 110, 400, 50) + hole(16, 120, 268, 30) + box(p, 16, 150, 268, 10, cap=False) + \
		water(400, 116, 80, 44) + \
		hatch(330, 84) + exit_(60, 149) + label(150, 135, "EXIT BELOW", "SORTIE EN BAS", "#FFE8C0")

	# T4 -- a steel wall to climb, a platform and a drop behind it
	p = PAL["moss"]
	L["training-04-up-the-wall"] = head("Up the Wall", "Grimper au mur",
		"A Climber goes up walls instead of turning back. Every critter needs the role: give it as they come out.",
		"Un Grimpeur monte aux murs au lieu de faire demi-tour. Chaque bestiole a besoin de ce rôle : donnez-le à la sortie.",
		480, 10, 10, 180, sky=p["sky"], climber=10) + \
		ground(p, 0, 480, 120, wave=2, flat=((200, 340),)) + steel(240, 40, 24, 80) + box(p, 264, 82, 70, 38) + \
		hatch(60, 92) + exit_(420, 119)

	# T5 -- a high ledge over a deep valley
	L["training-05-soft-landing"] = head("Soft Landing", "Atterrissage en douceur",
		"A Floater opens a leaf and survives any fall. Give it to every critter before it jumps.",
		"Un Planeur ouvre une feuille et survit à toutes les chutes. Donnez-le à chaque bestiole avant qu'elle saute.",
		400, 10, 10, 180, sky=p["sky"], floater=10) + \
		box(p, 0, 40, 160, 20) + ground(p, 0, 400, 140, wave=2) + \
		hatch(40, 18) + exit_(330, 139) + label(80, 52, "JUMP", "SAUTEZ")

	# T6 -- a pen with a thin floor; one burst opens it to the cave, which leads under the wall to the exit
	L["training-06-blast-through"] = head("Blast Through", "Ouvrir la voie",
		"An Exploder bursts after five seconds and takes the earth around it. Blast a hole in the thin floor.",
		"Un Artificier éclate au bout de cinq secondes et emporte la terre autour de lui. Percez un trou dans le sol mince.",
		480, 10, 9, 180, sky=p["sky"], exploder=2) + \
		box(p, 0, 140, 480, 20) + box(p, 20, 100, 290, 6) + steel(10, 40, 10, 66) + earth(p, rect=rect(300, 30, 24, 76)) + \
		poly(p, [(330, 141), (400, 100), (480, 100), (480, 141)]) + block("shape", rect=rect(400, 100, 80, 3), colour=p["cap"]) + \
		hatch(150, 74) + exit_(450, 99) + label(160, 125, "BLAST", "FAITES SAUTER", "#FFE8C0")

	# E1 -- two hatches: on the left a gap to bridge, on the right a shelf to dig through; the exit in the middle
	p = PAL["sand"]
	L["expedition-01-two-ways"] = head("Two Ways", "Deux chemins",
		"Two hatches: turn the left stream back before the lava, bridge its gap, and dig through the shelf on the right.",
		"Deux trappes : faites faire demi-tour au flot de gauche avant la lave, franchissez son trou et creusez l'étagère à droite.",
		800, 20, 16, 240, rate=40, sky=p["sky"], builder=4, digger=2, blocker=2) + \
		ground(p, 30, 300, 80, wave=3, period=120, flat=((30, 100), (250, 300))) + ground(p, 316, 800, 80, wave=3, period=120, seed=20, flat=((316, 440),)) + \
		lava(0, 140, 30, 20) + lava(296, 140, 24, 20) + box(p, 490, 40, 310, 20) + steel(480, 0, 10, 60) + \
		hatch(80, 50, "left") + hatch(740, 14, "left") + exit_(400, 79) + label(640, 50, "DIG", "CREUSEZ", "#FFE8C0")

	# E2 -- a floor over steel with one gap in the steel; below, a trench of lava before the exit
	L["expedition-02-steel-floor"] = head("Steel Floor", "Plancher d'acier",
		"Dig only where there is no steel underneath, then bridge the trench before the exit.",
		"Ne creusez que là où il n'y a pas d'acier dessous, puis franchissez la tranchée avant la sortie.",
		800, 20, 15, 240, sky=p["sky"], digger=3, builder=3, exploder=2) + \
		box(p, 0, 80, 790, 20) + steel(0, 100, 420, 6) + steel(470, 100, 320, 6) + steel(790, 36, 10, 70) + \
		box(p, 0, 138, 600, 22) + box(p, 612, 138, 188, 22) + lava(600, 146, 12, 14) + \
		hatch(120, 54) + exit_(740, 137) + label(445, 92, "HERE", "ICI", "#FFE8C0")

	# E3 -- a ridge with a steel face to climb and a cliff behind it to float down; a pond to bridge before the exit
	p = PAL["ice"]
	L["expedition-03-the-climb"] = head("The Climb", "L'ascension",
		"Climb the ridge, float down the far side, then bridge the pond.",
		"Escaladez la crête, descendez en planant de l'autre côté, puis franchissez l'étang.",
		960, 16, 12, 300, sky=p["sky"], climber=14, floater=14, builder=2) + \
		ground(p, 0, 960, 130, wave=3, flat=((360, 600), (700, 760))) + \
		earth(p, points=pts([(440, 130), (440, 34), (470, 28), (540, 26), (580, 30), (580, 130)])) + \
		block("shape", points=pts([(440, 34), (470, 28), (540, 26), (580, 30), (580, 33), (540, 29), (470, 31), (440, 37)]), colour=p["cap"]) + \
		steel(430, 36, 10, 94) + water(716, 130, 12, 30) + \
		hatch(100, 100) + exit_(880, 129)

	# E4 -- a lava lake with three islands; a pool beyond the exit
	p = PAL["cave"]
	L["expedition-04-lava-lake"] = head("Lava Lake", "Lac de lave",
		"Hop from island to island over the lava, then stop the stragglers before the far pool.",
		"Sautez d'île en île au-dessus de la lave, puis arrêtez les traînards avant le bassin du fond.",
		1120, 30, 24, 300, rate=1, sky=p["sky"], builder=8, blocker=2, digger=1) + \
		earth(p, rect=rect(0, 0, 1120, 14)) + lava(0, 146, 1120, 14) + \
		ground(p, 0, 420, 110, 146, wave=3, flat=((380, 420),)) + \
		box(p, 432, 110, 60, 36) + box(p, 504, 110, 60, 36) + box(p, 576, 110, 60, 36) + \
		ground(p, 648, 960, 110, 146, wave=2, flat=((648, 700),)) + ground(p, 980, 1120, 110, 146) + \
		hatch(80, 84) + exit_(656, 109)

	# E5 -- caves and tunnels: dig into the tunnel, bridge its pool, blast the plug, build up to the exit's ledge
	L["expedition-05-the-maze"] = head("The Maze", "Le labyrinthe",
		"Caves and tunnels: dig, bridge, blast and build your way to the exit.",
		"Grottes et tunnels : creusez, construisez, faites sauter et bâtissez jusqu'à la sortie.",
		1280, 40, 30, 360, sky=p["sky"], climber=2, floater=2, blocker=2, builder=2, digger=2, exploder=2) + \
		earth(p, rect=rect(0, 14, 1280, 146)) + \
		hole(10, 20, 300, 50) + hole(10, 86, 700, 34) + water(400, 120, 12, 40) + \
		hole(430, 120, 280, 10) + hole(700, 50, 560, 90) + earth(p, rect=rect(600, 86, 6, 44)) + \
		box(p, 1180, 122, 100, 18) + hatch(60, 44) + exit_(1230, 121)

	# E6 -- the whole valley: a pool, a ridge, a high shelf; two exits; a fast release
	p = PAL["moss"]
	L["expedition-06-grand-tour"] = head("Grand Tour", "Le grand voyage",
		"A fast release: bridge the pool, then dig down to the cave and its exit. Climbers that can float reach the east exit.",
		"Des sorties rapides : franchissez le bassin, puis creusez jusqu'à la grotte et sa sortie. Les grimpeurs qui planent atteignent la sortie est.",
		1600, 60, 50, 420, rate=70, sky=p["sky"], climber=4, floater=4, blocker=2, builder=10, digger=4, exploder=3) + \
		ground(p, 0, 300, 118, wave=6, period=200, flat=((40, 140), (250, 300))) + water(300, 118, 14, 42) + \
		box(p, 314, 118, 6, 42) + box(p, 320, 128, 1280, 32) + hole(560, 136, 540, 14) + water(560, 150, 16, 10) + \
		steel(760, 60, 10, 76) + earth(p, rect=rect(1100, 136, 20, 14)) + \
		hatch(90, 90) + exit_(1080, 149) + exit_(1500, 127) + label(660, 132, "DIG", "CREUSEZ")
	return L

def main():
	out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../sdcard/apps/critters.app/levels")
	os.makedirs(out, exist_ok=True)
	for k, v in gen().items():
		with open(os.path.join(out, k + ".level"), "w", encoding="utf-8") as f:
			f.write("# %s -- generated by tools/critters/mklevels.py: edit the script, not the file\n" % k + v)

main()
