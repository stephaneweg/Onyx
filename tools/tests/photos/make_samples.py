#!/usr/bin/env python3
#
# tools/tests/photos/make_samples.py <out> -- a made-up photo library for Photos on the PC (shots.sh photos): drawn
# photos (the mock-ups' landscapes, a town, flowers...) written as JPEGs with an EXIF -- when taken, the camera, the
# exposure, an orientation (some taken standing: stored lying, orientation 6) --, a screenshot dated by its name, in
# <out>/Pictures/<year>/<event>/ and <out>/Pictures/Camera; Photos' own files in <out>/etc/photos: a library.db (some
# favourites, a description) and albums.
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
#
import os, sys, random, datetime
from PIL import Image

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, os.path.join (HERE, "..", "..", "screenshot"))
import mockup_photos as MP

OUT = sys.argv[1] if len (sys.argv) > 1 else "/tmp/photos_samples"

CAMERAS = [("Google", "Pixel 8", (17, 10), 69), ("Apple", "iPhone 13", (16, 10), 51), ("Canon", "Canon EOS 80D", (56, 10), 350), ("SONY", "ILCE-6400", (40, 10), 250)]

# (folder, the day, how many, kinds, camera)
EVENTS = [
	("2026/Ghent", (2026, 9, 27, 16, 2), 14, ["town", "town", "sea", "field", "mountain", "lake", "sunset", "night"], 0),
	("2026/La Roche-en-Ardenne", (2026, 9, 21, 10, 15), 9, ["forest", "mountain", "lake", "field", "town"], 2),
	("2026/Brussels", (2026, 9, 15, 13, 40), 5, ["town", "night", "flowers"], 1),
	("2026/Summer at the sea", (2026, 8, 3, 9, 30), 10, ["sea", "sea", "sunset", "field"], 0),
	("2026/Garden", (2026, 6, 14, 18, 5), 6, ["flowers", "flowers", "field"], 1),
	("2025/Snow", (2025, 1, 18, 11, 0), 7, ["snow", "snow", "mountain", "forest"], 3),
	("2024/Holidays", (2024, 7, 22, 15, 20), 8, ["sea", "mountain", "lake", "sunset"], 2),
	("2023/Walks", (2023, 5, 6, 10, 45), 5, ["forest", "flowers", "field"], 3),
	("2021/Old", (2021, 10, 2, 14, 10), 4, ["lake", "forest", "town"], 3),
]

def exif_bytes (when, cam, orient, w, h, r):
	ex = Image.Exif ()
	make, model, (fa, fb), focal = cam
	ex[0x010F] = make; ex[0x0110] = model; ex[0x0112] = orient
	ex[0x0132] = when.strftime ("%Y:%m:%d %H:%M:%S")
	sub = ex.get_ifd (0x8769)
	sub[0x9003] = when.strftime ("%Y:%m:%d %H:%M:%S")
	from PIL.TiffImagePlugin import IFDRational
	sub[0x829D] = IFDRational (fa, fb)
	sub[0x829A] = IFDRational (1, r.choice ([60, 125, 250, 640, 1000]))
	sub[0x8827] = r.choice ([50, 100, 200, 400])
	sub[0x920A] = IFDRational (focal, 10)
	sub[0xA002] = w; sub[0xA003] = h
	return ex.tobytes ()

def days (y, m, d, H, M): return (datetime.date (y, m, d) - datetime.date (1970, 1, 1)).days * 86400 + H * 3600 + M * 60

def main ():
	r = random.Random (7)
	db = []
	seed = 100
	for folder, (y, mo, d, H, M), n, kinds, ci in EVENTS:
		dirp = os.path.join (OUT, "Pictures", folder); os.makedirs (dirp, exist_ok = True)
		t = datetime.datetime (y, mo, d, H, M, 0)
		for k in range (n):
			t += datetime.timedelta (minutes = r.randint (3, 40), seconds = r.randint (0, 59))
			kind = kinds[k % len (kinds)]
			standing = (k % 5 == 3)
			w, h = (1200, 900)
			img = MP.photo (seed, w, h, kind); seed += 7
			orient = 1
			if standing:			# taken standing: the camera stores it lying (turned) and says 6
				img = MP.photo (seed, 900, 1200, kind).rotate (90, expand = True); seed += 7; orient = 6; w, h = 1200, 900
			name = "IMG_%s.jpg" % t.strftime ("%Y%m%d_%H%M%S")
			p = os.path.join (dirp, name)
			img.save (p, "JPEG", quality = 86, exif = exif_bytes (t, CAMERAS[ci], orient, w, h, r))
			db.append (("SD:/Pictures/%s/%s" % (folder, name), os.path.getsize (p), days (t.year, t.month, t.day, t.hour, t.minute) + t.second,
				    (h, w) if orient == 6 else (w, h), orient, CAMERAS[ci], kind))
	# the camera's own folder: the newest (yesterday and today), a screenshot dated by its name
	cam = os.path.join (OUT, "Pictures", "Camera"); os.makedirs (cam, exist_ok = True)
	t = datetime.datetime (2026, 9, 28, 9, 12, 0)
	for k in range (4):
		t += datetime.timedelta (minutes = 7 + k)
		img = MP.photo (900 + k * 11, 1200, 900, ["flowers", "field", "town", "sea"][k])
		name = "PXL_%s.jpg" % t.strftime ("%Y%m%d_%H%M%S")
		p = os.path.join (cam, name)
		img.save (p, "JPEG", quality = 86, exif = exif_bytes (t, CAMERAS[0], 1, 1200, 900, r))
		db.append (("SD:/Pictures/Camera/" + name, os.path.getsize (p), days (2026, 9, 28, t.hour, t.minute), (1200, 900), 1, CAMERAS[0], "x"))
	shot = os.path.join (OUT, "Pictures", "Screenshot 2026-09-26 at 20.41.10.png")
	MP.photo (77, 1024, 640, "night").save (shot, "PNG")
	db.append (("SD:/Pictures/Screenshot 2026-09-26 at 20.41.10.png", os.path.getsize (shot), days (2026, 9, 26, 20, 41) + 10, (1024, 640), 1, None, "x"))

	# Photos' files: the library (favourites, a description), the albums
	pd = os.path.join (OUT, "etc", "photos"); os.makedirs (os.path.join (pd, "albums"), exist_ok = True)
	favs = { 1, 4, 9, 15, 22, 30, 41, 50 }
	lines = ["# Photos' library (Onyx): path size taken added w h orient fav camera exposure description"]
	for i, (path, size, taken, (w, h), orient, cam, kind) in enumerate (db):
		camera = ""; expo = ""
		if cam:
			make, model, (fa, fb), focal = cam
			camera = model if make.lower () in model.lower () else ("Sony " + model if make == "SONY" else make + " " + model)
			expo = "f/%g \xb7 1/250 s \xb7 ISO 100 \xb7 %g mm" % (fa / fb, focal / 10)
		desc = "The Graslei from the Korenlei bridge, late afternoon." if i == 1 else ""
		added = taken if i < len (db) - 6 else days (2026, 9, 28, 10, 0)
		lines.append ("P\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%s\t%s" % (path, size, taken, added, w, h, orient, 1 if i in favs else 0, camera, expo, desc))
	open (os.path.join (pd, "library.db"), "w", encoding = "utf-8").write ("\n".join (lines) + "\n")
	albums = { "Ghent, September": "2026/Ghent", "The Ardennes": "2026/La Roche-en-Ardenne", "Summer at the sea": "2026/Summer at the sea",
		   "Snow, January": "2025/Snow", "Garden": "2026/Garden" }
	for name, folder in albums.items ():
		ps = [p for (p, *_rest) in db if ("/Pictures/%s/" % folder) in p]
		open (os.path.join (pd, "albums", name + ".txt"), "w", encoding = "utf-8").write ("\n".join (ps) + "\n")
	print ("%d photos in %s" % (len (db), OUT))

main ()
