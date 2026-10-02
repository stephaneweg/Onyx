#!/usr/bin/env python3
"""make_library.py OUT_DIR -- a sample music library for Media Player on the PC (the simulator, the screenshots):
OUT_DIR/Music/<artist>/<album>/<nn> - <title>.<ext>, the made-up albums of the mock-ups (tools/screenshot/
mockup_music.py: their covers too), in every format the app reads -- MP3 (ID3v2 with its cover), FLAC (Vorbis
comments, a PICTURE), Ogg Vorbis (comments; the cover as the folder's cover.jpg), WAV (RIFF INFO) -- and two
albums of MIDI files (a Bach-like aria and an 8-bit game tune: several channels, General MIDI instruments).
The songs last minutes but are tiny: a quiet tone, 8 kHz mono, a low bit rate. A playlist in Music/Playlists.
And OUT_DIR/Videos: films, clips and a series' episodes (the made-up frames of the mock-ups, panned) -- VP9 + Opus
in WebM, AV1 in MP4, and one H.264 MP4 (a codec Onyx does not decode: its badge); a film left half way (videos.tsv).

Needs ffmpeg, Pillow and mutagen (pip install mutagen)."""
import os, sys, struct, subprocess, random, io

HERE = os.path.dirname (os.path.abspath (__file__))
ROOT = os.path.dirname (os.path.dirname (os.path.dirname (HERE)))
sys.path.insert (0, os.path.join (ROOT, "tools", "screenshot"))
import mockup_music as MM
from PIL import Image
from mutagen.id3 import ID3, TIT2, TPE1, TPE2, TALB, TRCK, TDRC, TCON, APIC
from mutagen.flac import FLAC, Picture
from mutagen.oggvorbis import OggVorbis

# (title, artist, year, genre, cover index, format, tracks)
ALBUMS = [
	("Northern Lights", "Lumen Drift", 2024, "Electronic", 0, "flac", ["Aurora", "Polar Wind", "Northern Lights", "Driftwood", "Snow Static", "Magnetic North"]),
	("Paper Boats", "The Paper Boats", 2021, "Indie", 1, "mp3", ["Paper Boats", "Harbour Song", "Fold Me Again", "Low Tide"]),
	("Saltwater", "Saltwater Choir", 2019, "Folk", 2, "ogg", ["Saltwater", "The Lighthouse", "Oars", "Evening Bells"]),
	("Owls at Noon", "Mira & the Owls", 2023, "Pop", 3, "mp3", ["Owls at Noon", "Sunday Dress", "Feathers", "Paper Moon", "Hush"]),
	("Grey Atlas", "Atlas Grey", 2018, "Rock", 4, "mp3", ["Concrete", "Grey Atlas", "Engines", "Static Sky"]),
	("Blue Hour Sessions", "Kōji Arai Trio", 2022, "Jazz", 5, "flac", ["Blue Hour", "Kissa", "Rain on Shinjuku", "Night Bus"]),
	("Velvet Static", "Nora Valez", 2025, "Soul", 7, "mp3", ["Velvet Static", "Slow Burn", "Gold", "Call Me Later"]),
	("Long Way Home", "The Paper Boats", 2017, "Indie", 8, "ogg", ["Long Way Home", "Ferry", "Postcards"]),
	("Glass Gardens", "Lumen Drift", 2020, "Electronic", 9, "wav", ["Glass Gardens", "Greenhouse"]),
	("Harbour Lights", "Saltwater Choir", 2016, "Folk", 10, "mp3", ["Harbour Lights", "Nets", "The Keeper"]),
]
MIDI_ALBUMS = [("Goldberg Variations", "J. S. Bach", 1741, 6, ["Aria", "Variatio 1", "Variatio 2"]),
	       ("Midnight Arcade", "8-Bit Parade", 1993, 11, ["Title Screen", "Level 1", "Boss Fight"])]

def run (*a): subprocess.run (a, check = True, stdout = subprocess.DEVNULL, stderr = subprocess.DEVNULL)

def cover_jpeg (i, s = 300):
	im = MM.cover_img (i, s // MM.K).resize ((s, s), Image.LANCZOS)
	b = io.BytesIO (); im.convert ("RGB").save (b, "JPEG", quality = 88); return b.getvalue ()

def audio (path, seconds, fmt, seed):
	f = 220 + seed % 7 * 55
	src = ["-f", "lavfi", "-i", "sine=frequency=%d:duration=%d:sample_rate=8000" % (f, seconds), "-af", "volume=0.08", "-ac", "1"]
	if fmt == "mp3": run ("ffmpeg", "-y", *src, "-b:a", "8k", path)
	elif fmt == "flac": run ("ffmpeg", "-y", *src, "-sample_fmt", "s16", path)
	elif fmt == "ogg": run ("ffmpeg", "-y", *src, "-c:a", "libvorbis", "-q:a", "-1", path)
	else: run ("ffmpeg", "-y", *src, "-acodec", "pcm_u8", path)

def riff_info (path, title, artist, album, year, genre, track):
	data = open (path, "rb").read ()
	def sub (k, v):
		b = v.encode ("utf-8") + b"\0"
		if len (b) % 2: b += b"\0"
		return k + struct.pack ("<I", len (b)) + b
	info = b"INFO" + sub (b"INAM", title) + sub (b"IART", artist) + sub (b"IPRD", album) + sub (b"ICRD", str (year)) + sub (b"IGNR", genre) + sub (b"ITRK", str (track))
	lst = b"LIST" + struct.pack ("<I", len (info)) + info
	body = data[12:] + lst
	open (path, "wb").write (b"RIFF" + struct.pack ("<I", 4 + len (body)) + b"WAVE" + body)

def vlq (n):
	b = [n & 0x7F]; n >>= 7
	while n: b.append ((n & 0x7F) | 0x80); n >>= 7
	return bytes (reversed (b))

def midi (path, name, style, seed):
	"""A type-1 file: a tempo track, then a track a channel (its program, its notes)."""
	r = random.Random (seed)
	tracks = []
	tempo = vlq (0) + bytes ([0xFF, 0x03, len (name)]) + name.encode () + vlq (0) + bytes ([0xFF, 0x51, 3]) + struct.pack (">I", 650000 if style == 0 else 420000)[1:] + vlq (0) + bytes ([0xFF, 0x2F, 0])
	tracks.append (tempo)
	if style == 0:	# strings and piano: an aria over a bass
		parts = [(0, 0, 60, 79, 240), (1, 48, 55, 72, 480), (2, 60, 50, 65, 960), (3, 73, 72, 88, 120), (4, 45, 48, 60, 480), (5, 43, 36, 48, 960), (6, 46, 60, 84, 240)]
	else:		# a game tune: square leads, a bass, drums
		parts = [(0, 80, 64, 84, 120), (1, 81, 52, 72, 240), (2, 38, 36, 48, 240), (9, 0, 36, 42, 120)]
	scale = [0, 2, 4, 5, 7, 9, 11]
	for ch, prog, lo, hi, step in parts:
		ev = vlq (0) + bytes ([0xC0 | ch, prog])
		t = 0; key = (lo + hi) // 2; bars = 64 if style == 0 else 96
		total = bars * 480
		pending = 0
		while t < total:
			dur = step * r.choice ([1, 1, 2]) if ch != 9 else step
			key += r.choice ([-2, -1, 1, 2, 0, 3, -3])
			key = max (lo, min (hi, key))
			k = (key // 12) * 12 + min (scale, key=lambda s: abs (s - key % 12))
			if ch == 9: k = r.choice ([36, 38, 42, 42])
			ev += vlq (pending) + bytes ([0x90 | ch, k, 70 + r.randint (0, 40)])
			ev += vlq (dur - 10) + bytes ([0x80 | ch, k, 0])
			pending = 10 if r.random () > 0.2 else 10 + step
			t += dur + (pending - 10)
		ev += vlq (0) + bytes ([0xFF, 0x2F, 0])
		tracks.append (ev)
	d = b"MThd" + struct.pack (">IHHH", 6, 1, len (tracks), 480)
	for t in tracks: d += b"MTrk" + struct.pack (">I", len (t)) + t
	open (path, "wb").write (d)

# (path under Videos, the mock-ups' frame, seconds, codec)
VIDEOS = [("Films/Sunset Harbour.webm", 2, 40, "vp9"), ("Films/Night Train.webm", 1, 24, "vp9"), ("Clips/Mountain Trails.webm", 0, 16, "vp9"),
	  ("Series/City Nights S01E03.webm", 1, 14, "vp9"), ("Series/City Nights S01E04.webm", 4, 14, "vp9"), ("Clips/Foxy & Friends.mp4", 3, 8, "av1"),
	  ("Clips/Garden Party.mp4", 0, 6, "h264")]

def video (path, frame, seconds, codec, tmp):
	png = os.path.join (tmp, "frame%d.png" % frame)
	if not os.path.exists (png): MM.frame_img (frame, 480, 270).convert ("RGB").resize ((1280, 720), Image.LANCZOS).save (png)
	fps = 24
	pan = "zoompan=z='1.0+0.0012*on':x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)':d=%d:s=640x360:fps=%d" % (seconds * fps, fps)
	src = ["-loop", "1", "-i", png, "-f", "lavfi", "-i", "sine=frequency=330:duration=%d:sample_rate=48000" % seconds, "-filter_complex", "[0:v]" + pan + "[v];[1:a]volume=0.05[a]",
	       "-map", "[v]", "-map", "[a]", "-t", str (seconds), "-pix_fmt", "yuv420p"]
	if codec == "vp9": run ("ffmpeg", "-y", *src, "-c:v", "libvpx-vp9", "-b:v", "150k", "-deadline", "realtime", "-cpu-used", "8", "-c:a", "libopus", "-b:a", "24k", path)
	elif codec == "av1": run ("ffmpeg", "-y", *src, "-c:v", "libaom-av1", "-b:v", "150k", "-cpu-used", "8", "-row-mt", "1", "-c:a", "libopus", "-b:a", "24k", "-strict", "-2", path)
	else: run ("ffmpeg", "-y", *src, "-c:v", "libx264", "-preset", "ultrafast", "-b:v", "150k", "-c:a", "aac", "-b:a", "32k", path)

def videos (root):
	out = os.path.join (root, "Videos"); tmp = os.path.join (root, "tmp"); os.makedirs (tmp, exist_ok = True)
	for rel, frame, sec, codec in VIDEOS:
		p = os.path.join (out, rel); os.makedirs (os.path.dirname (p), exist_ok = True)
		if not os.path.exists (p): video (p, frame, sec, codec, tmp)
	# a film left half way, an episode seen (the app reads its facts again: the size 0 does not match)
	st = os.path.join (root, "etc", "media"); os.makedirs (st, exist_ok = True)
	rows = [("Films/Sunset Harbour.webm", 18000, 0, 202610011950), ("Series/City Nights S01E03.webm", 0, 1, 202609301930)]
	open (os.path.join (st, "videos.tsv"), "w").write ("#onyx-videos 1\n" + "".join ("SD:/Videos/%s\t0\t0\t0\t0\t\t\t1\t0\t0\t0\t%d\t%d\t%d\t%d\t-\t%s\n" %
		(rel, pos, w, t, t, os.path.splitext (os.path.basename (rel))[0]) for rel, pos, w, t in rows))

def main ():
	out = os.path.join (sys.argv[1], "Music")
	os.makedirs (out, exist_ok = True)
	r = random.Random (7)
	made = []
	for k, (title, artist, year, genre, ci, fmt, songs) in enumerate (ALBUMS):
		d = os.path.join (out, artist, title); os.makedirs (d, exist_ok = True)
		jpg = cover_jpeg (ci)
		if fmt in ("ogg", "wav"): open (os.path.join (d, "cover.jpg"), "wb").write (jpg)
		for n, s in enumerate (songs):
			p = os.path.join (d, "%02d - %s.%s" % (n + 1, s, fmt))
			made.append ("SD:/Music/%s/%s/%s" % (artist, title, os.path.basename (p)))
			if os.path.exists (p): continue
			audio (p, r.randint (150, 330), fmt, k * 10 + n)
			if fmt == "mp3":
				t = ID3 (); t.add (TIT2 (encoding = 3, text = s)); t.add (TPE1 (encoding = 3, text = artist)); t.add (TALB (encoding = 3, text = title))
				t.add (TRCK (encoding = 3, text = "%d/%d" % (n + 1, len (songs)))); t.add (TDRC (encoding = 3, text = str (year))); t.add (TCON (encoding = 3, text = genre))
				t.add (APIC (encoding = 0, mime = "image/jpeg", type = 3, desc = "", data = jpg)); t.save (p)
			elif fmt == "flac":
				f = FLAC (p); f["TITLE"] = s; f["ARTIST"] = artist; f["ALBUM"] = title; f["TRACKNUMBER"] = str (n + 1); f["DATE"] = str (year); f["GENRE"] = genre
				pic = Picture (); pic.type = 3; pic.mime = "image/jpeg"; pic.width = pic.height = 300; pic.depth = 24; pic.data = jpg; f.add_picture (pic); f.save ()
			elif fmt == "ogg":
				f = OggVorbis (p); f["TITLE"] = s; f["ARTIST"] = artist; f["ALBUM"] = title; f["TRACKNUMBER"] = str (n + 1); f["DATE"] = str (year); f["GENRE"] = genre; f.save ()
			else: riff_info (p, s, artist, title, year, genre, n + 1)
	for k, (title, artist, year, ci, songs) in enumerate (MIDI_ALBUMS):
		d = os.path.join (out, artist, title); os.makedirs (d, exist_ok = True)
		open (os.path.join (d, "cover.jpg"), "wb").write (cover_jpeg (ci))
		for n, s in enumerate (songs):
			p = os.path.join (d, "%02d - %s.mid" % (n + 1, s))
			midi (p, s, k, k * 10 + n)
			made.append ("SD:/Music/%s/%s/%s" % (artist, title, os.path.basename (p)))
	pl = os.path.join (out, "Playlists"); os.makedirs (pl, exist_ok = True)
	open (os.path.join (pl, "Sunday morning.m3u"), "w").write ("#EXTM3U\n" + "\n".join (made[i] for i in (6, 12, 22, 24, 30)) + "\n")
	open (os.path.join (pl, "Workout.m3u"), "w").write ("#EXTM3U\n" + "\n".join (made[i] for i in (18, 19, 27, 28)) + "\n")
	# what was played (Media Player's stats: the home's rows), a few favourites
	st = os.path.join (sys.argv[1], "etc", "media"); os.makedirs (st, exist_ok = True)
	rows = [(0, 12, 202609281930, 1), (2, 8, 202609291012, 0), (12, 5, 202609301822, 1), (18, 7, 202609302041, 0), (25, 3, 202610011105, 0),
		(31, 9, 202610011733, 1), (39, 4, 202610011750, 0)]
	open (os.path.join (st, "stats.tsv"), "w").write ("# path plays lastPlayed favourite\n" + "".join ("%s\t%d\t%d\t%d\n" % (made[i], p, t, f) for i, p, t, f in rows))
	videos (sys.argv[1])
	print ("made", len (made), "songs in", out, "and", len (VIDEOS), "videos")

if __name__ == "__main__":
	main ()
