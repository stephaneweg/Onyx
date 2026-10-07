#!/usr/bin/env python3
# autodev/rounds/05-critters/mockups/mklevels.py -- the UX Designer's DRAFT levels for Critters (AutoDev round 5), in
# the .level format of 02-product-analysis.md §7.1: the twelve levels' names (EN + FR), hints, sizes, counts and a
# rough geometry, in the palettes of 04-ux-design.md §5.3 -- what the mock-ups draw. A starting point for the
# Developer's tools/critters/mklevels.py (03 §4.1, step 9): the geometry is NOT checked by the host test or crsim --
# the solutions decide (AC-30, AC-31). Also writes the player's two fixture levels (02 §7.1.4's example, a broken one).
#
#   python3 mklevels.py <sd folder>   -> <sd>/apps/critters.app/levels/*.level, <sd>/docs/critters/*.level
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

def pts(p):
	return ", ".join("%d %d" % (round(x), round(y)) for x, y in p)

def block(_blk, **kv):
	s = "[%s]\n" % _blk
	for k, v in kv.items():
		s += "%-10s = %s\n" % (k.replace("_fr", ".fr"), v)
	return s + "\n"

def earth(p, **kv):	return block("shape", **kv, colour=p["earth"], texture="speckle", colour2=p["e2"])
def rect(x, y, w, h):	return "%d %d %d %d" % (x, y, w, h)

def ground(p, x0, x1, y, h=160, wave=0, period=80, seed=0):
	"""earth from x0 to x1, its top at y (a gentle wave of `wave` px), down to h; a grass/moss cap 3 px on top"""
	step = max(8, -(-(x1 - x0) // 29))			# (<= 31 points a side: the cap's polygon holds 2 x 31 <= 64, 02 §7.1.2)
	top = [(x, y + wave * math.sin((x + seed) * 2 * math.pi / period)) for x in range(x0, x1 + 1, step)]
	if top[-1][0] != x1: top.append((x1, y + wave * math.sin((x1 + seed) * 2 * math.pi / period)))
	s = earth(p, points=pts(top + [(x1, h), (x0, h)]))
	s += block("shape", points=pts(top + [(x, yy + 3) for x, yy in reversed(top)]), colour=p["cap"])
	return s

def head(name, name_fr, hint, hint_fr, w, count, save, time, rate=50, sky="#101830", **roles):
	return block("level", format=1, name=name, name_fr=name_fr, hint=hint, hint_fr=hint_fr, size="%d 160" % w,
		     count=count, save=save, time=time, rate=rate, **roles, background=sky)

def hatch(x, y, d="right"): return block("hatch", at="%d %d" % (x, y), dir=d)
def exit_(x, y): return block("exit", at="%d %d" % (x, y))
def label(x, y, en, fr, col="#FFFFFF"): return block("label", at="%d %d" % (x, y), text=en, text_fr=fr, colour=col)

def gen():
	L = {}
	p = PAL["soil"]
	L["training-01-straight-down"] = head("Straight Down", "Tout droit vers le bas",
		"Click Digger, then click a critter standing on the floor.", "Cliquez sur Creuseur, puis sur une bestiole posée sur le sol.",
		400, 10, 8, 180, sky=p["sky"], digger=3) + \
		ground(p, 0, 400, 70, 100) + earth(p, rect=rect(0, 138, 400, 22)) + \
		block("shape", rect=rect(0, 30, 10, 40), **STEEL) + block("shape", rect=rect(390, 30, 10, 40), **STEEL) + \
		hatch(80, 44) + exit_(300, 137) + label(200, 86, "DIG", "CREUSEZ")
	L["training-02-mind-the-gap"] = head("Mind the Gap", "Attention à la marche",
		"Click Builder, then a critter walking toward the gap: it lays a stair.", "Cliquez sur Bâtisseur, puis sur une bestiole qui marche vers le trou : elle pose un escalier.",
		480, 10, 8, 180, sky=p["sky"], builder=4) + \
		ground(p, 0, 200, 100, wave=3) + ground(p, 232, 480, 100, wave=3, seed=40) + \
		block("shape", points=pts([(200, 160), (200, 140), (216, 150), (232, 140), (232, 160)]), **LAVA) + \
		hatch(50, 70) + exit_(420, 99)
	L["training-03-hold-the-line"] = head("Hold the Line", "Tenir la ligne",
		"A Blocker stands still and turns the others back. Keep them out of the water.", "Un Bloqueur reste sur place et fait demi-tour aux autres. Gardez-les hors de l'eau.",
		480, 10, 6, 180, sky=p["sky"], blocker=2, digger=1) + \
		ground(p, 0, 380, 110, wave=2) + earth(p, rect=rect(380, 130, 100, 30)) + block("shape", rect=rect(380, 118, 100, 12), **WATER) + \
		hatch(300, 80) + exit_(70, 109)
	p = PAL["moss"]
	L["training-04-up-the-wall"] = head("Up the Wall", "Grimper au mur",
		"A Climber goes up walls instead of turning. Give every critter the role.", "Un Grimpeur monte aux murs au lieu de faire demi-tour. Donnez ce rôle à chaque bestiole.",
		480, 10, 10, 180, sky=p["sky"], climber=10) + \
		ground(p, 0, 480, 120) + block("shape", rect=rect(240, 40, 24, 80), **STEEL) + earth(p, rect=rect(264, 82, 70, 40)) + \
		hatch(60, 90) + exit_(420, 119)
	L["training-05-soft-landing"] = head("Soft Landing", "Atterrissage en douceur",
		"A Floater opens a leaf and survives any fall.", "Un Planeur ouvre une feuille et survit à toutes les chutes.",
		400, 10, 10, 180, sky=p["sky"], floater=10) + \
		ground(p, 0, 160, 40, 60) + ground(p, 0, 400, 140, wave=2) + hatch(40, 18) + exit_(330, 139)
	L["training-06-blast-through"] = head("Blast Through", "Ouvrir la voie",
		"An Exploder bursts after five seconds and takes the earth around it.", "Un Artificier éclate au bout de cinq secondes et emporte la terre autour de lui.",
		480, 10, 9, 180, sky=p["sky"], exploder=2) + \
		ground(p, 0, 480, 120) + earth(p, rect=rect(220, 40, 20, 80)) + hatch(60, 90) + exit_(400, 119)
	p = PAL["sand"]
	L["expedition-01-two-ways"] = head("Two Ways", "Deux chemins",
		"Two hatches: route both streams to the same exit.", "Deux trappes : menez les deux colonnes à la même sortie.",
		800, 20, 16, 240, sky=p["sky"], builder=4, digger=2, blocker=2) + \
		ground(p, 0, 800, 128, wave=4, period=120) + earth(p, rect=rect(0, 74, 290, 12)) + earth(p, rect=rect(520, 74, 280, 12)) + \
		earth(p, points=pts([(330, 130), (370, 96), (430, 96), (470, 130)])) + \
		block("shape", points=pts([(560, 160), (560, 132), (600, 126), (640, 132), (640, 160)]), **WATER) + \
		block("shape", rect=rect(380, 96, 40, 8), **STEEL) + \
		hatch(70, 48) + hatch(740, 48, "left") + exit_(250, 125) + label(400, 115, "EAST", "EST", "#FFE8C0")
	L["expedition-02-steel-floor"] = head("Steel Floor", "Plancher d'acier",
		"Dig only where there is no steel underneath.", "Ne creusez que là où il n'y a pas d'acier dessous.",
		800, 20, 15, 240, sky=p["sky"], digger=3, builder=3, exploder=2) + \
		ground(p, 0, 800, 80, 112, wave=2) + block("shape", rect=rect(0, 112, 330, 8), **STEEL) + block("shape", rect=rect(420, 112, 380, 8), **STEEL) + \
		ground(p, 0, 800, 140, wave=3, period=160) + \
		block("shape", points=pts([(600, 160), (600, 142), (640, 136), (700, 142), (700, 160)]), **LAVA) + \
		hatch(120, 54) + exit_(740, 140)
	p = PAL["ice"]
	L["expedition-03-the-climb"] = head("The Climb", "L'ascension",
		"Climb the ridge, then float down the far side.", "Escaladez la crête, puis descendez en planant de l'autre côté.",
		960, 20, 14, 300, sky=p["sky"], climber=6, floater=6, builder=2) + \
		ground(p, 0, 960, 130, wave=3) + earth(p, points=pts([(380, 130), (440, 40), (520, 30), (580, 130)])) + \
		block("shape", rect=rect(436, 40, 10, 90), **STEEL) + hatch(100, 100) + exit_(880, 128)
	p = PAL["cave"]
	L["expedition-04-lava-lake"] = head("Lava Lake", "Lac de lave",
		"Bridge the lake, and block the stragglers.", "Franchissez le lac et bloquez les retardataires.",
		1120, 30, 24, 300, sky=p["sky"], builder=8, blocker=2, digger=1) + \
		ground(p, 0, 420, 110, wave=3) + ground(p, 700, 1120, 110, wave=3) + earth(p, rect=rect(0, 0, 1120, 14)) + \
		block("shape", rect=rect(420, 140, 280, 20), **LAVA) + hatch(80, 80) + exit_(1040, 109)
	L["expedition-05-the-maze"] = head("The Maze", "Le labyrinthe",
		"Caves and tunnels: every role once.", "Grottes et tunnels : chaque rôle une fois.",
		1280, 40, 30, 360, sky=p["sky"], climber=2, floater=2, blocker=2, builder=2, digger=2, exploder=2) + \
		earth(p, rect=rect(0, 20, 1280, 140)) + \
		"".join(block("shape", circle="%d %d %d" % (90 + i * 150, 70 + (i % 3) * 25, 34 + (i % 2) * 8), material="erase") for i in range(8)) + \
		block("shape", rect=rect(0, 60, 1280, 20), material="erase") + block("shape", rect=rect(600, 100, 200, 10), **STEEL) + \
		hatch(60, 64) + exit_(1220, 79)
	p = PAL["moss"]
	L["expedition-06-grand-tour"] = head("Grand Tour", "Le grand voyage",
		"The whole valley, two exits, a fast release.", "Toute la vallée, deux sorties, des sorties rapides.",
		1600, 60, 50, 420, rate=70, sky=p["sky"], climber=4, floater=4, blocker=2, builder=10, digger=4, exploder=3) + \
		ground(p, 0, 1600, 118, wave=8, period=200) + earth(p, points=pts([(500, 120), (560, 50), (640, 46), (700, 120)])) + \
		block("shape", rect=rect(900, 132, 140, 28), **WATER) + earth(p, rect=rect(1180, 60, 200, 14)) + \
		block("shape", rect=rect(1300, 20, 16, 40), **STEEL) + hatch(90, 80) + exit_(800, 114) + exit_(1500, 112)
	return L

def user_levels():
	example = """# My first level -- dig through the floor, bridge the gap.
[level]
format     = 1
name       = My First Level
name.fr    = Mon premier niveau
hint       = Dig down through the floor, then build across the gap.
hint.fr    = Creusez le sol, puis construisez un pont au-dessus du trou.
size       = 480 160
count      = 10
save       = 7
time       = 180
rate       = 50
digger     = 2
builder    = 3
background = #142040

# the ground, the upper floor, a ravine
[shape]
rect    = 0 120 200 40
texture = speckle
[shape]
rect    = 240 120 240 40
texture = speckle
[shape]
rect    = 0 70 200 12
colour  = #6E8A3C
[shape]
points  = 200 160, 200 120, 210 140, 230 140, 240 120, 240 160
material = lava

# a steel post the diggers must avoid, and a cave under it
[shape]
rect     = 120 70 16 12
material = steel
[shape]
circle   = 60 140 10
material = erase

[hatch]
at  = 40 60
dir = right

[exit]
at  = 440 119

[label]
at      = 100 150
text    = DIG
text.fr = CREUSEZ
"""
	broken = example.replace("[shape]\nrect    = 0 70 200 12", "[shap]\nrect    = 0 70 200 12", 1).replace("My first level", "A level with a typo", 1)
	return {"my-first-level": example, "cliffs": broken}

def main():
	sd = sys.argv[1]
	lv = os.path.join(sd, "apps/critters.app/levels"); os.makedirs(lv, exist_ok=True)
	for k, v in gen().items():
		open(os.path.join(lv, k + ".level"), "w").write("# generated by mklevels.py (DRAFT, the UX's mock-ups) -- edit the script, not the file\n" + v)
	dc = os.path.join(sd, "docs/critters"); os.makedirs(dc, exist_ok=True)
	for k, v in user_levels().items():
		open(os.path.join(dc, k + ".level"), "w").write(v)

main()
