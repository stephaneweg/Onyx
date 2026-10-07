#!/usr/bin/env python3
# mkpacks.py -- the mock-up's packs 4 and 5 (AutoDev round 3, the UX Designer): the titles and texts of
# 02-product-analysis.md section 6, a few real maps and solutions (the ones the pictures show), the other levels
# a plain map. NOT the packs the Developer writes (step 6 / 7): their maps and pars come from the reference
# solutions run by the test bench.   python3 mkpacks.py <out-folder>
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
import os, sys

def level(id, t, tfr, concept, words, text, tfr_text, hint, hint_fr, par, mapl, sol, draw=None):
    s = ["[level]", "id = " + id, "title = " + t, "title.fr = " + tfr, "concept = " + concept, "words = " + words]
    if draw: s.append("draw = " + draw)
    s += ["text = " + text, "text.fr = " + tfr_text, "hint = " + hint, "hint.fr = " + hint_fr, "par = " + par, "map ="]
    s += ["| " + r for r in mapl]
    s += ["solution ="] + ["| " + r for r in sol.strip("\n").split("\n")] + [""]
    return "\n".join(s)

def page(x, y, ch):
    rows = [["."] * 24 for _ in range(18)]
    rows[y][x] = ch
    return ["".join(r) for r in rows]

G = "FORWARD BACK LEFT RIGHT PICK ITEM"
CORR = ["##########", "#>.1.2.3*#", "##########"]
p4 = ["# Turtle Quest -- MOCK-UP pack 4 (autodev/rounds/03-turtle-missions/mockups)", "[pack]", "title = 4. Gems and portals", "title.fr = 4. Gemmes et portails", ""]
p4.append(level("gems-line", "Gems in a row", "Gemmes en rang", "gems", G,
    "Pick the gems in order -- 1, then 2, then 3 -- and reach the flag.", "Ramasse les gemmes dans l'ordre -- 1, puis 2, puis 3 -- et rejoins le drapeau.",
    "REPEAT 3: FORWARD 2, PICK.", "REPETER 3 : AVANCER 2, RAMASSER.", "4 7", CORR, "REPEAT 3\n  FORWARD 2\n  PICK\nEND REPEAT\nFORWARD"))
p4.append(level("gems-back", "Wrong way round", "À l'envers", "gems", G,
    "Gem 1 is at the far end. Go there first, then pick the others on the way back.", "La gemme 1 est tout au bout. Va d'abord là-bas, puis ramasse les autres en revenant.",
    "FORWARD 6, PICK, then BACK 2 and PICK, twice.", "AVANCER 6, RAMASSER, puis RECULER 2 et RAMASSER, deux fois.", "5 8",
    ["##########", "#*>.3.2.1#", "##########"], "FORWARD 6\nPICK\nREPEAT 2\n  BACK 2\n  PICK\nEND REPEAT\nBACK 3"))
for id, t, tf in [("gems-corners", "Round the square", "Le tour du carré"), ("gems-count", "Back and forth", "Aller et retour"), ("gem-door", "The locked gem", "Gemme sous clé")]:
    p4.append(level(id, t, tf, "gems", G + " GEM WALL", "Pick the gems in order.", "Ramasse les gemmes dans l'ordre.", "-", "-", "4 7", CORR, "REPEAT 3\n  FORWARD 2\n  PICK\nEND REPEAT\nFORWARD"))
p4.append(level("portal", "A first jump", "Un premier saut", "teleport", "FORWARD BACK LEFT RIGHT FRONT WALL",
    "No way between the two rooms... but a portal in each. Step on it and come out of its twin.", "Pas de passage entre les deux salles... mais un portail dans chacune. Pose-toi dessus et ressors par son jumeau.",
    "FORWARD 4: two steps to the portal, two more from its twin.", "AVANCER 4 : deux pas jusqu'au portail, deux autres depuis son jumeau.", "1 3",
    ["###########", "#>.T###T.*#", "###########"], "FORWARD 4"))
for id, t, tf in [("portal-chain", "Portal hopping", "Saut sur saut"), ("portal-shortcut", "The short way", "Le raccourci"), ("portal-gems", "Past the portal", "Après le portail")]:
    p4.append(level(id, t, tf, "teleport", "FORWARD BACK LEFT RIGHT FRONT WALL PICK ITEM GEM", "Use the portals.", "Utilise les portails.", "-", "-", "1 3",
        ["###########", "#>.T###T.*#", "###########"], "FORWARD 4"))
FINALE = ["############", "#>.1.2.T####", "############", "#T..3..U####", "############", "#U.4.k.D5.*#", "############"]
p4.append(level("grand-finale", "Grand finale", "Le grand final", "teleport", "FORWARD BACK LEFT RIGHT PICK ITEM GEM FRONT WALL ONGOAL KEYS",
    "Five gems in order, two pairs of portals, a key and its door. Then the flag!", "Cinq gemmes dans l'ordre, deux paires de portails, une clé et sa porte. Puis le drapeau !",
    "Each corridor: walk, pick, walk into the portal.", "Chaque couloir : avance, ramasse, entre dans le portail.", "12 15", FINALE,
    "REPEAT 2\n  FORWARD 2\n  PICK\nEND REPEAT\nFORWARD 2\nFORWARD 3\nPICK\nFORWARD 3\nFORWARD 2\nPICK\nFORWARD 2\nPICK\nFORWARD 3\nPICK\nFORWARD 2"))

D = "FORWARD BACK LEFT RIGHT PENUP PENDOWN COLOR"
KOCH = """SUB Koch (size, depth)
  IF depth = 0 THEN
    FORWARD size
    EXIT SUB
  END IF
  Koch size / 3, depth - 1
  LEFT 60
  Koch size / 3, depth - 1
  RIGHT 120
  Koch size / 3, depth - 1
  LEFT 60
  Koch size / 3, depth - 1
END SUB
"""
TREE = """SUB Tree (size, depth)
  IF depth = 0 THEN EXIT SUB
  FORWARD size
  LEFT 30
  Tree size * 0.6, depth - 1
  RIGHT 60
  Tree size * 0.6, depth - 1
  LEFT 30
  BACK size
END SUB
Tree 6, 4"""
POLY = "SUB Polygon (sides, size)\n  REPEAT sides\n    FORWARD size\n    RIGHT 360 / sides\n  END REPEAT\nEND SUB\n"
p5 = ["# Turtle Quest -- MOCK-UP pack 5 (autodev/rounds/03-turtle-missions/mockups)", "[pack]", "title = 5. Spirals and fractals", "title.fr = 5. Spirales et fractales", ""]
L5 = [("polygon-sub", "Any polygon", "Tout polygone", "params", None, (3, 8, "^"), POLY + "Polygon 5, 3\nPENUP\nRIGHT\nFORWARD 8\nLEFT\nPENDOWN\nPolygon 8, 2"),
      ("polygon-row", "Three to eight", "De trois à huit", "params", None, (8, 14, "^"), POLY + "FOR s = 3 TO 8\n  Polygon s, 2\nNEXT"),
      ("rainbow-spiral", "Rainbow snail", "Arc-en-ciel", "color", "color", (11, 9, "^"), "FOR i = 1 TO 16\n  COLOR i MOD 6 + 1\n  FORWARD i\n  RIGHT\nNEXT"),
      ("hex-spiral", "Hexagon spiral", "Spirale à six pans", "variable", None, (11, 8, "^"), "n = 0.5\nREPEAT 18\n  FORWARD n\n  RIGHT 60\n  n = n + 0.5\nEND REPEAT"),
      ("color-flower", "Two-colour flower", "Fleur bicolore", "color", "color", (11, 9, "^"), "FOR i = 1 TO 6\n  IF i MOD 2 = 0 THEN COLOR 4 ELSE COLOR 1\n  REPEAT 6\n    FORWARD 2.5\n    RIGHT 60\n  END REPEAT\n  RIGHT 60\nNEXT"),
      ("halves", "Half of half", "Moitié de moitié", "function", None, (3, 2, ">"), "FUNCTION Half (x)\n  Half = x / 2\nEND FUNCTION\nn = 8\nREPEAT 5\n  FORWARD n\n  RIGHT\n  n = Half (n)\nEND REPEAT"),
      ("snail-rec", "Snail in a snail", "Escargot gigogne", "recursion", None, (3, 15, "^"), "SUB Snail (size)\n  IF size < 1 THEN EXIT SUB\n  FORWARD size\n  RIGHT\n  Snail size - 1\nEND SUB\nSnail 13"),
      ("tree", "A tree", "Un arbre", "recursion", None, (11, 17, "^"), TREE),
      ("koch", "The Koch curve", "La courbe de Koch", "recursion", None, (2, 12, ">"), KOCH + "Koch 18, 3"),
      ("snowflake", "The snowflake", "Le flocon de neige", "recursion", None, (5, 5, ">"), KOCH + "REPEAT 3\n  Koch 12, 2\n  RIGHT 120\nEND REPEAT"),
      ("sierpinski", "Sierpinski", "Sierpinski", "recursion", "color", (3, 16, ">"), "SUB Tri (size, depth)\n  IF depth = 0 THEN\n    REPEAT 3\n      FORWARD size\n      LEFT 120\n    END REPEAT\n    EXIT SUB\n  END IF\n  COLOR depth + 1\n  Tri size / 2, depth - 1\n  PENUP\n  FORWARD size / 2\n  PENDOWN\n  Tri size / 2, depth - 1\n  PENUP\n  LEFT 60\n  FORWARD size / 2\n  RIGHT 60\n  PENDOWN\n  Tri size / 2, depth - 1\n  PENUP\n  RIGHT 120\n  FORWARD size / 2\n  LEFT 120\n  PENDOWN\nEND SUB\nTri 16, 3")]
TXT = {"snowflake": ("Three Koch curves, turned by 120 degrees, close into a snowflake.", "Trois courbes de Koch, tournées de 120 degrés, se referment en flocon."),
       "rainbow-spiral": ("A square spiral of 16 legs, each one square longer, in the rainbow's colours: the colour comes from the counter.", "Une spirale carrée de 16 côtés, chacun une case plus long, aux couleurs de l'arc-en-ciel : la couleur vient du compteur."),
       "tree": ("A trunk, then two smaller trees on it, turned by 30 degrees each way. Four levels of branches.", "Un tronc, puis deux arbres plus petits dessus, tournés de 30 degrés de chaque côté. Quatre étages de branches.")}
for id, t, tf, c, dr, (x, y, h), sol in L5:
    tx = TXT.get(id, ("Reproduce the figure.", "Reproduis la figure."))
    p5.append(level(id, t, tf, c, D, tx[0], tx[1], "-", "-", "14 18", page(x, y, h), sol, dr or "1"))
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
open(os.path.join(out, "4-gems-and-portals.turtle"), "w", encoding="utf-8").write("\n".join(p4))
open(os.path.join(out, "5-spirals-and-fractals.turtle"), "w", encoding="utf-8").write("\n".join(p5))
