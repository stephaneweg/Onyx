#!/usr/bin/env python3
"""paint_scene.py -- the simulator's scripts for Paint's screenshots (shots.sh): the window at 4,30 on the
1024 x 768 screen (1008 x 704), the picture fitted.

    paint_scene.py landscape   a picture made of layers: a sky (the fill's Sunset gradient), a sun and
                               mountains (an ellipse, the lasso + fill), a warm tint (Multiply 60 %), a title
                               (the Text tool), a vignette (a white rounded rectangle as a Mask)
    paint_scene.py fade        sunset-sea.jpg, sunny-mountains.jpg opened as a layer, faded into it by a
                               white-to-transparent gradient as a Mask on the layer below only
    paint_scene.py brushes     the Brushes gallery open
MIT licence (Onyx).
"""
import sys
s = []
def c (x, y): s.append ("down %d %d;up %d %d" % (x, y, x, y))
def rc (x, y): s.append ("rdown %d %d;rup %d %d" % (x, y, x, y))
def drag (pts):
	x, y = pts[0]; s.append ("down %d %d" % (x, y))
	for (a, b), (cc, d) in zip (pts, pts[1:]):
		n = max (1, int (max (abs (cc - a), abs (d - b)) / 6))
		for i in range (1, n + 1): s.append ("move %d %d" % (a + (cc - a) * i / n, b + (d - b) * i / n))
	s.append ("up %d %d" % pts[-1])
def key (k): s.append ("key %s" % k)
def w (): s.append ("wait")
def typ (t):
	for ch in t: key (32 if ch == " " else ch)
def pal (i, row = 0): return (719 + i * 21, 16 + row * 21)		# a palette colour's place
SHAPE = lambda k: (484 + 2 + (k % 5) * 24 + 12, 8 + (k // 5) * 22 + 11)
def newlayer (): c (769, 653); w ()
def blend (row_y): c (894, 182); w (); c (894, row_y); w ()
MASK_Y, ONLYBELOW_Y, MULT_Y = 365, 422, 236

def landscape ():
	OX, OY, Z = 98, 210, 0.67
	P = lambda x, y: (int (OX + x * Z), int (OY + y * Z))
	w ()
	key ("f"); w (); c (134, 114); w (); c (300, 114); w (); c (396, 242); w ()		# the fill: Gradient, Sunset
	drag ([P (400, 5), P (400, 470)]); w (); key (13); w ()
	newlayer ()
	rc (*pal (5)); w ()								# colour 2 yellow: the sun
	c (*SHAPE (3)); w (); c (90, 114); w ()					# the ellipse; outline off -> filled
	drag ([P (470, 230), P (580, 340)]); w ()
	key ("l"); w (); c (*pal (9)); w ()						# the lasso; purple
	drag ([P (x, y) for x, y in [(0, 375), (112, 237), (200, 312), (312, 187), (437, 325), (550, 250), (675, 337), (800, 262), (800, 560), (0, 560), (0, 375)]]); w ()
	key ("f"); w (); c (68, 114); w (); c (*P (400, 480)); w (); key (27); w ()
	key ("l"); w (); c (*pal (8)); w ()
	drag ([P (x, y) for x, y in [(0, 412), (150, 350), (287, 400), (450, 362), (625, 412), (800, 375), (800, 560), (0, 560), (0, 412)]]); w ()
	key ("f"); w (); c (*P (400, 500)); w (); key (27); w ()
	newlayer ()									# a warm tint, multiplied
	c (*pal (5, 1)); w (); key ("f"); w (); c (*P (400, 300)); w ()
	blend (MULT_Y)
	drag ([(920, 212), (889, 212)]); w ()
	newlayer ()									# the title
	c (*pal (0, 1)); w ()
	key ("t"); w (); c (313, 114); w ()
	c (*P (250, 70)); w (); typ ("Lac des Cimes"); w ()
	c (*SHAPE (2)); w ()								# (the rounded rectangle: the text put down)
	newlayer ()									# the vignette: a Mask
	rc (*pal (0, 1)); w ()
	drag ([P (30, 28), P (770, 532)]); w ()
	blend (MASK_Y)
	c (894, 427); w (); c (300, 680); w ()					# (the landscape's layer chosen; the pointer away)
	w (); w ()

def fade ():
	w (); w ()
	s.append ("menu 2"); w (); typ ("docs/pictures/sunny-mountains.jpg"); key (13); w ()
	c (400, 650); w ()								# (put down)
	newlayer ()
	key ("g"); w (); c (90, 114); w (); c (90, 182); w ()				# the gradient: Colour 1 to transparent
	c (*pal (0, 1)); w ()								# white
	drag ([(250, 400), (480, 400)]); w (); key (13); w ()
	blend (MASK_Y); blend (ONLYBELOW_Y)
	w ()

def brushes ():
	w (); w (); key ("b"); w (); c (444, 60); w (); w ()

{"landscape": landscape, "fade": fade, "brushes": brushes}[sys.argv[1]] ()
print (";".join (s))
