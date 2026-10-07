#!/usr/bin/env python3
# tools/pinball/mktables.py -- Pinball's three shipped tables (sdcard/apps/pinball.app/tables), written in the .table
# format (docs/04 "Pinball"): Space Station, Haunted Manor, Volcano. Started from the UX Designer's drafts (AutoDev
# round 4, autodev/rounds/04-pinball/mockups/mktables.py: the layouts, colours, artwork and words), then TUNED with the
# host test (sh tools/tests/run_pinball_test.sh: the balls stay inside, no dead spot, a flipper shot reaches the top):
# the inlanes feed the flippers (the outlanes' dividers go on as the inlanes' guides), the slingshots stand clear of
# the guides, the shooter gate slants so that a ball resting on it rolls onto the playfield. Edit the tables here,
# run it, then the host test.
#
#   python3 tools/pinball/mktables.py [folder]      -> <folder>/1-space-station.table, 2-haunted-manor.table, 3-volcano.table
#                                                     (default: sdcard/apps/pinball.app/tables)
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
import math, os, sys

def pts(p):
	return ", ".join("%g %g" % (round(x, 1), round(y, 1)) for x, y in p)

def arcpts(cx, cy, r, a0, a1, n):	# table degrees (0 = +x, 90 = +y down), from a0 to a1
	return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]

def block(_blk, **kv):
	s = "[%s]\n" % _blk
	for k, v in kv.items():
		s += "%-10s = %s\n" % (k.replace("_fr", ".fr"), v)
	return s + "\n"

def circle_poly(cx, cy, r, n=20):
	return arcpts(cx, cy, r, 0, 360 - 360 / n, n - 1)

def star(x, y, r):
	return [(x, y - r), (x + r * 0.35, y), (x, y + r), (x - r * 0.35, y)]

# ---- the skeleton (03 §6.1) ------------------------------------------------------------------------------------
def skeleton(c):
	s = ""
	# the cabinet's corners outside the round top, the shooter lane's floor, the apron (artwork, drawn first)
	s += block("shape", points=pts([(0, 0), (0, 262)] + arcpts(260, 260, 258, 180, 270, 12) + [(260, 0)]), colour=c["side"])
	s += block("shape", points=pts([(260, 0)] + arcpts(260, 260, 258, 270, 360, 12) + [(520, 262), (520, 0)]), colour=c["side"])
	s += block("shape", points=pts([(478, 300), (520, 300), (520, 1040), (478, 1040)]), colour=c["lane"])
	s += block("shape", points=pts([(0, 1000), (476, 1000), (476, 1040), (0, 1040)]), colour=c["side"])
	s += block("shape", points=pts([(0, 880), (38, 880), (38, 1000), (0, 1000)]), colour=c["lane"])
	s += block("shape", points=pts([(438, 880), (476, 880), (476, 1000), (438, 1000)]), colour=c["lane"])
	return s

def skeleton_walls(c):
	w = c["wall"]
	s = ""
	s += block("wall", id="outline", closed=1, points=pts([(0, 0), (520, 0), (520, 1040), (0, 1040)]), colour=w)
	s += block("arc", centre="260 260", radius=258, **{"from": 180}, to=360, colour=w, width=5)
	s += "# the shooter lane: its inner wall, its one-way gate at the top (slanted: a ball resting on it rolls off)\n"
	s += block("wall", points=pts([(476, 1040), (476, 300)]), colour=w)
	s += block("gate", a="476 300", b="518 284", pass_="up".replace("_", "")).replace("pass_", "pass")
	s += "# the outlanes' dividers, a rubber post on each; they go on as the inlanes' guides down to the flippers\n"
	s += block("wall", points=pts([(38, 700), (38, 850), (144, 894)]), colour=w)
	s += block("wall", points=pts([(438, 700), (438, 850), (332, 894)]), colour=w)
	s += block("post", at="38 700", radius=6, colour=c["post"])
	s += block("post", at="438 700", radius=6, colour=c["post"])
	s += "# the slingshots: a closed body, its face the hypotenuse\n"
	s += block("wall", closed=1, points=pts([(96, 740), (96, 820), (132, 830)]), colour=c["sling"], bounce=0.3)
	s += block("wall", closed=1, points=pts([(380, 740), (380, 820), (344, 830)]), colour=c["sling"], bounce=0.3)
	s += block("sling", id="sl", a="96 740", b="132 830", colour=c["sling"])
	s += block("sling", id="sr", a="380 740", b="344 830", colour=c["sling"])
	s += block("flipper", side="left", pivot="150 905", length=70, colour=c["flip"])
	s += block("flipper", side="right", pivot="326 905", length=70, colour=c["flip"])
	s += block("plunger", at="498 980")
	s += "# inlanes and outlanes\n"
	s += block("lane", id="out-l", group="out", rect="7 770 24 40", colour=c["lamp2"])
	s += block("lane", id="in-l", group="in", rect="55 770 24 40", colour=c["lamp2"])
	s += block("lane", id="in-r", group="in", rect="397 770 24 40", colour=c["lamp2"])
	s += block("lane", id="out-r", group="out", rect="445 770 24 40", colour=c["lamp2"])
	return s

def top_lanes(xs, c, y=108):
	s = "# the top lanes (rotate group), their separators\n"
	for i, x in enumerate(xs):
		s += block("lane", id="l%d" % (i + 1), group="top", rect="%d %d 30 50" % (x - 15, y), colour=c["lamp"])
	seps = [xs[0] - 35] + [(a + b) // 2 for a, b in zip(xs, xs[1:])] + [xs[-1] + 35]
	for x in seps:
		s += block("wall", points=pts([(x, y - 16), (x, y + 52)]), colour=c["wall"], width=6)
	return s

def header(name, name_fr, goal, goal_fr, gravity, ballsave, bg):
	return ("# %s -- a Pinball table (SD:/apps/pinball.app/tables), the format in docs/04 \"Pinball\";\n"
		"# made by tools/pinball/mktables.py -- edit it there, then run sh tools/tests/run_pinball_test.sh.\n"
		"# MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.\n\n" % name) + \
		block("table", format=1, name=name, name_fr=name_fr, goal=goal, goal_fr=goal_fr, size="520 1040",
		      gravity=gravity, ball=13, balls=3, ballsave=ballsave, drain=1010, rotate="top", background=bg)

# ---- 1. Space Station ---------------------------------------------------------------------------------------------
def space_station():
	c = dict(side="#1B2440", lane="#0A1228", wall="#9FB3D9", post="#E8EEF8", sling="#E0559A", flip="#F2F4F8",
		 lamp="#FFD34D", lamp2="#7FD8FF")
	s = header("Space Station", "Station spatiale", "Dock the shuttle twice for multiball.",
		   "Amarrez la navette deux fois pour le multiball.", 1200, 10, "#0E1A3A")
	s += "# ---- the artwork: nebulae, a planet and its ring, stars -------------------------------------------------\n"
	s += skeleton(c)
	s += block("shape", points=pts([(40, 380), (140, 330), (210, 420), (180, 520), (90, 560), (44, 500)]), colour="#132452")
	s += block("shape", points=pts([(300, 560), (420, 520), (460, 640), (380, 700), (300, 660)]), colour="#14214A")
	s += block("shape", points=pts(circle_poly(250, 470, 54, 28)), colour="#1C3470")
	s += block("shape", points=pts(circle_poly(250, 470, 46, 28)), colour="#22408A")
	s += block("shape", points=pts([(180, 478), (320, 456), (322, 466), (182, 488)]), colour="#5B78C0")
	for (x, y, r) in [(120, 250, 6), (390, 200, 5), (80, 640, 4), (430, 560, 6), (200, 700, 4), (330, 400, 4), (150, 600, 5), (60, 300, 4)]:
		s += block("shape", points=pts(star(x, y, r)), colour="#AFC4F0")
	s += block("label", at="250 655", text="STATION", text_fr="STATION", size=3, colour="#2A4486")
	s += block("label", at="238 1020", text="SPACE STATION", text_fr="STATION SPATIALE", size=2, colour="#7F93C9")
	s += "# ---- the walls ------------------------------------------------------------------------------------------\n"
	s += skeleton_walls(c)
	s += "# the left orbit's channel (its inner wall), the orbit itself: a path over the table back to the right inlane\n"
	s += block("wall", points=pts([(44, 560), (44, 380)]), colour=c["wall"])
	path = [(23, 520), (23, 330)] + arcpts(260, 260, 236, 180, 340, 14) + [(458, 360), (458, 690), (421, 745)]
	s += block("ramp", id="orbit", a="4 540", b="42 540", pass_="up", path=pts(path), time=1.2, out="0 320",
		   score=2000, colour="#4FA3FF", width=24).replace("pass_", "pass")
	s += top_lanes([180, 250, 320], c)
	s += "# ---- the toys -------------------------------------------------------------------------------------------\n"
	for i, (x, y) in enumerate([(185, 245), (315, 245), (250, 322)]):
		s += block("bumper", id="b%d" % (i + 1), at="%d %d" % (x, y), radius=28, kick=900, score=100, colour="#22C3E6")
	s += block("saucer", id="dock", at="405 430", radius=18, hold=1.5, out="-350 450", score=750, colour="#FF8A3D")
	s += block("label", at="405 468", text="DOCK", text_fr="QUAI", size=1, colour="#FF8A3D")
	for i, y in enumerate([470, 512, 554]):
		s += block("target", id="t%d" % (i + 1), kind="drop", bank="solar", a="66 %d" % y, b="66 %d" % (y + 36), colour="#F2B33D")
	s += block("label", at="96 530", text="SOLAR", text_fr="SOLAIRE", size=1, colour="#F2B33D", angle=90)
	s += block("target", id="st1", kind="standup", a="196 562", b="226 552", colour="#8CE05A")
	s += block("target", id="st2", kind="standup", a="274 552", b="304 562", colour="#8CE05A")
	s += block("label", at="23 470", text="ORBIT", text_fr="ORBITE", size=1, colour="#4FA3FF", angle=90)
	s += "# ---- the rules -------------------------------------------------------------------------------------------\n"
	s += block("rule", when="lanes top", do="multiplier; score 1000", message="Bonus x up!", message_fr="Bonus x augmenté !")
	s += block("rule", when="bank solar", do="bonus 5000", message="Solar array: 5,000 bonus", message_fr="Panneaux : 5 000 de bonus")
	s += block("rule", when="ramp orbit", count=3, once=1, do="extraball", message="Extra ball!", message_fr="Bille supplémentaire !")
	s += block("rule", when="saucer dock", count=2, do="multiball 2", message="MULTIBALL!", message_fr="MULTIBALL !")
	return s

# ---- 2. Haunted Manor ---------------------------------------------------------------------------------------------
def haunted_manor():
	c = dict(side="#2A1A30", lane="#140A1C", wall="#C0A070", post="#F0E6D0", sling="#9C5BD0", flip="#F4F0E6",
		 lamp="#F0C040", lamp2="#B0E0A0")
	s = header("Haunted Manor", "Manoir hanté", "Clear the ghosts and the bats, then lock 2 balls in the crypt: 3-ball multiball.",
		   "Abattez les fantômes et les chauves-souris, puis bloquez 2 billes dans la crypte : multiball à 3 billes.", 1400, 8, "#1C1028")
	s += skeleton(c)
	s += block("shape", points=pts(circle_poly(330, 170, 40, 24)), colour="#E8E0B8")	# the moon
	s += block("shape", points=pts(circle_poly(346, 160, 34, 24)), colour="#1C1028")
	s += block("shape", points=pts([(120, 700), (120, 560), (170, 500), (220, 560), (220, 520), (250, 470), (280, 520), (280, 560), (330, 500), (380, 560), (380, 700)]), colour="#2A1838")
	for x in (150, 250, 350):
		s += block("shape", points=pts([(x - 10, 600), (x + 10, 600), (x + 10, 630), (x - 10, 630)]), colour="#F0C040")
	s += block("label", at="250 680", text="MANOR", text_fr="MANOIR", size=3, colour="#3C2850")
	s += block("label", at="238 1020", text="HAUNTED MANOR", text_fr="MANOIR HANTÉ", size=2, colour="#B090C0")
	s += skeleton_walls(c)
	s += "# the right orbit's channel\n"
	s += block("wall", points=pts([(440, 560), (440, 380)]), colour=c["wall"])
	path = [(458, 520), (458, 330)] + arcpts(260, 260, 236, 0, -170, 14) + [(30, 340), (30, 690), (55, 745)]
	s += block("ramp", id="orbit", a="442 540", b="474 540", pass_="up", path=pts(path), time=1.2, out="0 320",
		   score=2000, colour="#70C0A0", width=24).replace("pass_", "pass")
	s += top_lanes([180, 250, 320], c)
	for i, (x, y) in enumerate([(190, 250), (310, 250), (250, 330)]):
		s += block("bumper", id="b%d" % (i + 1), at="%d %d" % (x, y), radius=28, kick=950, score=100, colour="#6EE07A")
	s += block("saucer", id="crypt", at="110 300", radius=18, hold=1.5, out="350 420", score=750, colour="#A050D0")
	s += block("label", at="110 338", text="CRYPT", text_fr="CRYPTE", size=1, colour="#C080F0")
	for i, y in enumerate([430, 470, 510, 550]):
		s += block("target", id="g%d" % (i + 1), kind="drop", bank="ghosts", a="66 %d" % y, b="66 %d" % (y + 34), colour="#E8E8F0")
	for i, y in enumerate([430, 470, 510, 550]):
		s += block("target", id="v%d" % (i + 1), kind="drop", bank="bats", a="410 %d" % y, b="410 %d" % (y + 34), colour="#D04848")
	s += block("label", at="92 500", text="GHOSTS", text_fr="FANTÔMES", size=1, colour="#E8E8F0", angle=90)
	s += block("label", at="384 500", text="BATS", text_fr="CHAUVES-SOURIS", size=1, colour="#E07070", angle=90)
	for i, x in enumerate([200, 250, 300]):
		s += block("target", id="s%d" % (i + 1), kind="standup", a="%d 420" % (x - 14), b="%d 420" % (x + 14), colour="#F0C040")
	s += block("rule", when="lanes top", do="multiplier", message="Bonus x up!", message_fr="Bonus x augmenté !")
	s += block("rule", when="bank ghosts", do="score 10000", message="Ghosts banished!", message_fr="Fantômes chassés !")
	s += block("rule", when="bank bats", do="score 10000", message="Bats scattered!", message_fr="Chauves-souris dispersées !")
	s += block("rule", when="saucer crypt", count=2, do="multiball 3", message="MULTIBALL!", message_fr="MULTIBALL !")
	s += "# (02 asks: both banks twice -> an extra ball; one event per rule: the bats twice, once a game)\n"
	s += block("rule", when="bank bats", count=2, once=1, do="extraball", message="Extra ball!", message_fr="Bille supplémentaire !")
	return s

# ---- 3. Volcano -----------------------------------------------------------------------------------------------------
def volcano():
	c = dict(side="#2A1A14", lane="#140A06", wall="#B09A88", post="#F4E8DC", sling="#FF7A2A", flip="#F6F0EA",
		 lamp="#FFD040", lamp2="#FFB070")
	s = header("Volcano", "Volcan", "Hit the lava ramp 5 times: eruption multiball!",
		   "Touchez 5 fois la rampe de lave : multiball éruption !", 1700, 6, "#1F120E")
	s += skeleton(c)
	s += block("shape", points=pts([(60, 720), (190, 430), (230, 400), (270, 400), (310, 430), (440, 720)]), colour="#3A1C12")
	s += block("shape", points=pts([(200, 440), (230, 405), (270, 405), (300, 440), (270, 470), (250, 450), (230, 475)]), colour="#C83A10")
	s += block("shape", points=pts([(120, 640), (180, 600), (200, 620), (150, 660)]), colour="#5A2410")
	s += block("shape", points=pts([(330, 600), (390, 640), (370, 660), (320, 620)]), colour="#5A2410")
	s += block("label", at="250 690", text="VOLCANO", text_fr="VOLCAN", size=3, colour="#4A2414")
	s += block("label", at="238 1020", text="VOLCANO", text_fr="VOLCAN", size=2, colour="#D09070")
	s += skeleton_walls(c)
	s += block("flipper", side="right", pivot="432 470", length=55, rest=20, up=-30, colour=c["flip"])
	s += block("wall", points=pts([(476, 392), (452, 412), (452, 480), (440, 492)]), colour=c["wall"])	# (its top slants: no ball rests between it and the shooter lane)
	s += block("wall", points=pts([(200, 560), (200, 520)]), colour=c["wall"])
	s += block("wall", points=pts([(300, 560), (300, 520)]), colour=c["wall"])
	path = [(250, 520), (250, 420), (250, 200), (200, 120), (120, 150), (70, 300), (50, 690), (55, 745)]
	s += block("ramp", id="lava", a="204 540", b="296 540", pass_="up", path=pts(path), time=1.0, out="0 340",
		   score=1500, colour="#FF4020", width=26).replace("pass_", "pass")
	s += block("label", at="250 590", text="LAVA", text_fr="LAVE", size=2, colour="#FF6A1A")
	s += top_lanes([160, 220, 280, 340], c)
	for i, (x, y) in enumerate([(160, 250), (250, 225), (340, 250), (250, 315)]):
		s += block("bumper", id="b%d" % (i + 1), at="%d %d" % (x, y), radius=26, kick=1000, score=100, colour="#FF6A1A")
	s += block("saucer", id="crater", at="400 330", radius=18, hold=1.2, out="-380 420", score=750, colour="#FFD040")
	s += block("label", at="400 366", text="CRATER", text_fr="CRATÈRE", size=1, colour="#FFD040")
	for i, y in enumerate([480, 520, 560]):
		s += block("target", id="t%d" % (i + 1), kind="drop", bank="magma", a="66 %d" % y, b="66 %d" % (y + 34), colour="#FFB070")
	s += block("rule", when="ramp lava", count=5, do="multiball 2", message="ERUPTION! MULTIBALL!", message_fr="ÉRUPTION ! MULTIBALL !")
	s += block("rule", when="lanes top", do="multiplier", message="Bonus x up!", message_fr="Bonus x augmenté !")
	s += block("rule", when="ramp lava", count=10, once=1, do="extraball", message="Extra ball!", message_fr="Bille supplémentaire !")
	s += block("rule", when="bank magma", do="ballsave 10", message="Ball save 10 s", message_fr="Bille sauvée 10 s")
	return s

if __name__ == "__main__":
	d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "sdcard", "apps", "pinball.app", "tables")
	os.makedirs(d, exist_ok=True)
	for f, t in (("1-space-station.table", space_station()), ("2-haunted-manor.table", haunted_manor()), ("3-volcano.table", volcano())):
		open(os.path.join(d, f), "w", encoding="utf-8").write(t)
		print("  " + os.path.normpath(os.path.join(d, f)))
