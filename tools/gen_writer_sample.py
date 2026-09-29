#!/usr/bin/env python3
# gen_writer_sample.py -- Writer's sample document, sdcard/docs/writer-tour.rtf: a page of what
# Writer does (styles, fonts, colours, highlights, lists, super/subscript, a quotation, an image),
# written as Rich Text Format by hand (the picture a PNG drawn here with Pillow + numpy, in
# \pict\pngblip). Writer opens it; the documentation's screenshot shows it
# (tools/tests/desktop_sim/shots.sh writer).
#
#   python3 tools/gen_writer_sample.py
import io, os
import numpy as np
from PIL import Image

OUT = os.path.join (os.path.dirname (os.path.abspath (__file__)), "..", "sdcard", "docs", "writer-tour.rtf")

def banner (w = 600, h = 150):
	"""An abstract banner: soft waves in teal and peach over a light sky."""
	y, x = np.mgrid[0:h, 0:w].astype (np.float32)
	t = x / w
	sky = np.stack ([248 - 20 * t, 244 - 6 * t, 238 + 10 * t], axis = -1)
	img = sky.copy ()
	for k, (amp, freq, phase, base, col) in enumerate ([
			(14, 2.1, 0.3, 0.55, (240, 176, 122)), (18, 1.6, 1.7, 0.66, (73, 146, 167)),
			(12, 2.7, 2.9, 0.78, (44, 94, 120))]):
		edge = h * base + amp * np.sin (2 * np.pi * (freq * t + phase))
		m = np.clip ((y - edge) / 2.0, 0, 1)[..., None]
		img = img * (1 - m) + np.array (col, np.float32) * m
	# a few bubbles
	for cx, cy, r, a in [(90, 40, 22, 0.35), (150, 60, 12, 0.3), (470, 34, 18, 0.3), (520, 58, 9, 0.35)]:
		d = np.sqrt ((x - cx) ** 2 + (y - cy) ** 2)
		m = (np.clip (r - d, 0, 1) * a)[..., None]
		img = img * (1 - m) + 255 * m
	im = Image.fromarray (np.clip (img, 0, 255).astype (np.uint8), "RGB")
	b = io.BytesIO (); im.save (b, "PNG", optimize = True)
	return b.getvalue (), w, h

def esc (s):
	o = []
	for ch in s:
		c = ord (ch)
		if ch in "\\{}": o.append ("\\" + ch)
		elif c < 0x80: o.append (ch)
		elif c < 0x100: o.append ("\\'%02x" % c)
		else: o.append ("\\u%d?" % (c if c < 0x8000 else c - 65536))
	return "".join (o)

def main ():
	png, pw, ph = banner ()
	hexs = png.hex ()
	pict = "{\\pict\\pngblip\\picw%d\\pich%d\\picwgoal%d\\pichgoal%d\n" % (pw, ph, pw * 15, ph * 15) \
	     + "\n".join (hexs[i:i + 128] for i in range (0, len (hexs), 128)) + "}"
	R = []
	R.append ("{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n"
		  "{\\fonttbl{\\f0\\froman\\fcharset0 Liberation Serif;}{\\f1\\fswiss\\fcharset0 Liberation Sans;}"
		  "{\\f2\\fmodern\\fcharset0 DejaVu Sans Mono;}{\\f3\\froman\\fcharset0 Gelasio;}{\\f4\\fswiss\\fcharset0 Selawik;}}\n"
		  "{\\colortbl;\\red73\\green146\\blue167;\\red192\\green57\\blue43;\\red255\\green242\\blue0;\\red89\\green89\\blue89;"
		  "\\red64\\green64\\blue64;\\red146\\green208\\blue80;\\red44\\green94\\blue120;}\n"
		  "{\\stylesheet{\\s0 Normal;}{\\s1 heading 1;}{\\s2 heading 2;}{\\s3 heading 3;}{\\s4 Title;}{\\s5 Subtitle;}{\\s6 Quote;}{\\s7 Plain Text;}}\n"
		  "\\paperw11906\\paperh16838\\margl1134\\margr1134\\margt1134\\margb1134\\viewkind1\n"
		  "{\\footer\\pard\\qc{\\field{\\*\\fldinst PAGE}{\\fldrslt 1}}\\par}\n")
	def para (style, text, extra = ""):
		defaults = { 0: "\\ql\\sa120\\sl276\\slmult1", 1: "\\ql\\sb360\\sa120\\keepn", 4: "\\qc\\sb240\\sa240\\keepn", 5: "\\qc\\sa240\\keepn",
			     6: "\\ql\\li720\\ri720\\sb120\\sa240\\sl276\\slmult1" }
		R.append ("\\pard\\plain\\s%d%s%s %s\\par\n" % (style, defaults.get (style, ""), extra, text))
	N = "{\\f0\\fs24 %s}"
	para (4, "{\\f1\\fs56\\b %s}" % esc ("Onyx Writer"))
	para (5, "{\\f1\\fs32\\cf4 %s}" % esc ("A word processor for Onyx, in the way of AbiWord"))
	para (0, "{\\f0\\fs24 %s}" % pict, "\\qc")
	para (1, "{\\f1\\fs36\\b %s}" % esc ("What Writer does"))
	para (0, (N % esc ("Writer lays its pages out itself and draws every letter with ")) + "{\\f0\\fs24\\b FreeType}" +
	      (N % esc (", from the TrueType fonts of the card, at any zoom: ")) + "{\\f1\\fs24 Liberation Sans}" + (N % ", ") +
	      "{\\f3\\fs24 Gelasio}" + (N % ", ") + "{\\f4\\fs24 Selawik}" + (N % esc (" or ")) + "{\\f2\\fs22 DejaVu Sans Mono}" +
	      (N % esc (", in every size. The paragraphs are justified, centred or aligned; the page is A4 with its margins, numbered at its foot.")), "\\qj")
	items = [
		(N % esc ("Characters: ")) + "{\\f0\\fs24\\b bold}" + (N % ", ") + "{\\f0\\fs24\\i italic}" + (N % ", ") + "{\\f0\\fs24\\ul underlined}" +
		(N % ", ") + "{\\f0\\fs24\\strike struck through}" + (N % esc (", superscript (E = mc")) + "{\\f0\\fs24\\super 2}" +
		(N % esc (") and subscript (H")) + "{\\f0\\fs24\\sub 2}" + (N % "O)."),
		(N % esc ("Colours: ")) + "{\\f0\\fs24\\cf2 red text}" + (N % ", ") + "{\\f0\\fs24\\cf1 teal text}" + (N % ", and a ") +
		"{\\f0\\fs24\\highlight3 highlighter}" + (N % "."),
		N % esc ("Paragraphs: styles, indents dragged on the ruler, spacing, bullets and numbers."),
		N % esc ("Images (PNG, JPEG, BMP, GIF, WebP), resized with the mouse."),
		N % esc ("Files: Rich Text Format with all of it, plain text, an export as HTML."),
	]
	for it in items:
		R.append ("\\pard\\plain\\s0\\ql\\li720\\fi-360\\sa120\\sl276\\slmult1{\\listtext\\f0\\fs24 \\'b7\\tab}{\\*\\pn\\pnlvlblt\\pnf0{\\pntxtb\\'b7}}%s\\par\n" % it)
	para (6, "{\\f0\\fs24\\i\\cf5 %s}" % esc ("“The spirit of KolibriOS: a lean system that does what it is asked, quickly.”"))
	para (1, "{\\f1\\fs36\\b %s}" % esc ("A few keys"))
	keys = [ "Ctrl+B, Ctrl+I, Ctrl+U: bold, italic, underline.", "Ctrl+L, Ctrl+E, Ctrl+R, Ctrl+J: the alignments.",
		 "Ctrl+Z, Ctrl+Y: undo, redo. Ctrl+F: find and replace.", "Shift+Enter: a new line in the paragraph; Tab in a list: a level down." ]
	for i, k in enumerate (keys):
		R.append ("\\pard\\plain\\s0\\ql\\li720\\fi-360\\sa120\\sl276\\slmult1{\\listtext\\f0\\fs24 %d.\\tab}{\\*\\pn\\pnlvlbody\\pndec{\\pntxta .}}%s\\par\n" % (i + 1, N % esc (k)))
	R.append ("}\n")
	os.makedirs (os.path.dirname (OUT), exist_ok = True)
	with open (OUT, "w", encoding = "ascii") as f: f.write ("".join (R))
	print (OUT, os.path.getsize (OUT), "bytes")

main ()
