#!/usr/bin/env python3
# gamelib_samples.py -- sample games for the Game Library's screenshot (shots.sh), in the desktop
# simulator's writes folder (never on the card): made-up games -- empty ROM files of each system in
# SD:/simroms, the Game Library's config.ini watching that folder, and a picture for each
# (thumbs/<file>.thm: 160 x 144 RGB, as the Game Library caches them) drawn here as a title screen,
# so no emulator runs. No real game, no ROM.
#
#   python3 gamelib_samples.py <writes dir>
import os, sys
from PIL import Image, ImageDraw, ImageFont

W, H = 160, 144
GAMES = [	# file, title, top colour, bottom colour, ink
	("Sky Fortress.z64", "SKY FORTRESS", (40, 60, 140), (150, 190, 240), (255, 255, 255)),
	("Star Runner.sfc", "STAR RUNNER", (10, 10, 40), (60, 20, 90), (255, 220, 90)),
	("Castle Keeper.sfc", "CASTLE KEEPER", (30, 70, 40), (120, 170, 90), (250, 245, 220)),
	("Ocean Deep.sfc", "OCEAN DEEP", (0, 40, 80), (0, 120, 160), (200, 250, 255)),
	("Tiny Racer.gba", "TINY RACER", (200, 60, 40), (250, 190, 60), (255, 255, 255)),
	("Moon Garden.gba", "MOON GARDEN", (30, 20, 60), (90, 70, 150), (240, 230, 255)),
	("Robot Party.gbc", "ROBOT PARTY", (250, 200, 80), (240, 120, 60), (40, 30, 30)),
	("Block Drop.gb", "BLOCK DROP", (155, 188, 15), (139, 172, 15), (15, 56, 15)),
	("Maze Mouse.gb", "MAZE MOUSE", (155, 188, 15), (139, 172, 15), (15, 56, 15)),
	("Pixel Quest.nes", "PIXEL QUEST", (0, 0, 0), (20, 20, 60), (250, 160, 60)),
	("Jungle Jump.nes", "JUNGLE JUMP", (20, 90, 30), (90, 160, 60), (255, 250, 200)),
]

def picture (title, top, bottom, ink):
	im = Image.new ("RGB", (W, H))
	d = ImageDraw.Draw (im)
	for y in range (H):				# a gradient, a ground, some stars / blocks
		t = y / (H - 1)
		d.line ([(0, y), (W, y)], fill = tuple (int (top[k] + (bottom[k] - top[k]) * t) for k in range (3)))
	for i in range (18):
		x, y = (i * 53) % W, (i * 29) % 70
		d.rectangle ([x, y, x + 1, y + 1], fill = ink)
	d.rectangle ([0, 112, W, H], fill = tuple (max (0, c - 40) for c in bottom))
	for x in range (0, W, 16):
		d.rectangle ([x, 112, x + 14, 118], fill = tuple (min (255, c + 30) for c in bottom))
	font = ImageFont.load_default ()
	big = Image.new ("RGBA", (W, 20), (0, 0, 0, 0))	# the title, twice as big
	ImageDraw.Draw (big).text ((0, 2), title, font = font, fill = ink + (255,))
	box = big.getbbox () or (0, 0, 1, 1)
	big = big.crop (box)
	big = big.resize ((big.width * 2, big.height * 2), Image.NEAREST)
	if big.width > W - 8:
		big = big.resize ((W - 8, big.height), Image.NEAREST)
	im.paste (big, ((W - big.width) // 2, 40), big)
	d.text (((W - 60) // 2, 90), "PRESS START", font = font, fill = ink)
	return im

def main ():
	out = sys.argv[1]
	roms = os.path.join (out, "simroms")
	thumbs = os.path.join (out, "apps", "gamelib.app", "thumbs")
	os.makedirs (roms, exist_ok = True)
	os.makedirs (thumbs, exist_ok = True)
	for f, title, top, bottom, ink in GAMES:
		open (os.path.join (roms, f), "wb").close ()
		with open (os.path.join (thumbs, f + ".thm"), "wb") as t:
			t.write (picture (title, top, bottom, ink).tobytes ())
	with open (os.path.join (out, "apps", "gamelib.app", "config.ini"), "w") as c:
		c.write ("; the screenshot's sample games\nfolder = SD:/simroms\n")

main ()
