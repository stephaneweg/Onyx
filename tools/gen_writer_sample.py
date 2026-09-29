#!/usr/bin/env python3
# gen_writer_sample.py -- Writer's sample documents, written as Rich Text Format by hand:
#   sdcard/docs/writer-tour.rtf: two pages of what Writer does (styles, fonts, colours, highlights,
#     lists, super/subscript, a quotation, an image -- a PNG drawn here with Pillow + numpy, in
#     \pict\pngblip --, a table of contents, a header and a footer with the page's number -- the
#     first page's own --, a table with a heading row and shading). The documentation's screenshot
#     shows it (tools/tests/desktop_sim/shots.sh writer);
#   sdcard/docs/new-year-letter.rtf: a mail merge's letter for the Contacts form (SD:/docs/
#     contacts.card, whose "merge" key names it): the fields name, address, group, birthday.
#
#   python3 tools/gen_writer_sample.py
import io, os
import numpy as np
from PIL import Image

DOCS = os.path.join (os.path.dirname (os.path.abspath (__file__)), "..", "sdcard", "docs")

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

def field (inst, result, fmt):
	"""A field (PAGE, NUMPAGES, DATE, MERGEFIELD) as Writer writes it: its result in the same format."""
	return "{%s {\\field{\\*\\fldinst {%s }}{\\fldrslt {%s %s}}}}" % (fmt, inst, fmt, result)

def save (name, R):
	out = os.path.join (DOCS, name)
	os.makedirs (DOCS, exist_ok = True)
	with open (out, "w", encoding = "ascii") as f: f.write ("".join (R))
	print (out, os.path.getsize (out), "bytes")

def tour ():
	png, pw, ph = banner ()
	hexs = png.hex ()
	pict = "{\\pict\\pngblip\\picw%d\\pich%d\\picwgoal%d\\pichgoal%d\n" % (pw, ph, pw * 15, ph * 15) \
	     + "\n".join (hexs[i:i + 128] for i in range (0, len (hexs), 128)) + "}"
	R = []
	# colours: 1 teal, 2 red, 3 yellow, 4 grey, 5 dark grey, 6 green, 7 deep blue, 8 white, 9 a light teal
	R.append ("{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n"
		  "{\\fonttbl{\\f0\\froman\\fcharset0 Liberation Serif;}{\\f1\\fswiss\\fcharset0 Liberation Sans;}"
		  "{\\f2\\fmodern\\fcharset0 DejaVu Sans Mono;}{\\f3\\froman\\fcharset0 Gelasio;}{\\f4\\fswiss\\fcharset0 Selawik;}}\n"
		  "{\\colortbl;\\red73\\green146\\blue167;\\red192\\green57\\blue43;\\red255\\green242\\blue0;\\red89\\green89\\blue89;"
		  "\\red64\\green64\\blue64;\\red146\\green208\\blue80;\\red44\\green94\\blue120;\\red255\\green255\\blue255;\\red232\\green241\\blue243;}\n"
		  "{\\stylesheet{\\s0 Normal;}{\\s1 heading 1;}{\\s2 heading 2;}{\\s3 heading 3;}{\\s4 Title;}{\\s5 Subtitle;}{\\s6 Quote;}{\\s7 Plain Text;}"
		  "{\\s8 toc 1;}{\\s9 toc 2;}{\\s11 TOC Heading;}{\\s12 header;}{\\s13 footer;}}\n"
		  "\\paperw11906\\paperh16838\\margl1134\\margr1134\\margt1134\\margb1134\\viewkind1\n"
		  "\\sectd\\headery567\\footery567\\titlepg\n")
	# the header (not on the title page), the footer (on every page): "Page 1 of 2"
	HF = "\\f1\\fs18\\cf4"
	R.append ("{\\header\\pard\\plain\\s12\\qr{\\f1\\fs18\\b\\cf1 Onyx Writer}{%s  \\u8212? a tour}\\par}\n" % HF)
	foot = "\\pard\\plain\\s13\\qc{%s Page }%s{%s  of }%s\\par" % (HF, field ("PAGE", "1", HF), HF, field ("NUMPAGES", "2", HF))
	R.append ("{\\footer%s}\n{\\footerf%s}\n" % (foot, foot))
	def para (style, text, extra = ""):
		defaults = { 0: "\\ql\\sa120\\sl276\\slmult1", 1: "\\ql\\sb360\\sa120\\keepn", 2: "\\ql\\sb240\\sa120\\keepn", 4: "\\qc\\sb240\\sa240\\keepn",
			     5: "\\qc\\sa240\\keepn", 6: "\\ql\\li720\\ri720\\sb120\\sa240\\sl276\\slmult1", 11: "\\ql\\sb240\\sa240\\keepn" }
		R.append ("\\pard\\plain\\s%d%s%s %s\\par\n" % (style, defaults.get (style, ""), extra, text))
	N = "{\\f0\\fs24 %s}"
	MONO = "{\\f2\\fs20 %s}"
	para (4, "{\\f1\\fs56\\b %s}" % esc ("Onyx Writer"))
	para (5, "{\\f1\\fs32\\cf4 %s}" % esc ("A word processor for Onyx, in the way of AbiWord"))
	para (0, "{\\f0\\fs24 %s}" % pict, "\\qc")
	# the table of contents (Insert > Table of Contents makes it; Tools > Update Table of Contents, its pages)
	para (11, "{\\f1\\fs32\\b Contents}")
	R.append ("{\\field{\\*\\fldinst {TOC \\\\o \"1-3\" }}{\\fldrslt\n")
	for style, title, page in [ (8, "What Writer does", 1), (8, "A few keys", 1), (8, "Tables, pages and fields", 2), (9, "A mail merge", 2) ]:
		R.append ("\\pard\\plain\\s%d\\ql%s\\sa60\\tqr\\tldot\\tx9638{\\f0\\fs24 %s\\tab %d}\\par\n" % (style, "\\sb120" if style == 8 else "\\li240", esc (title), page))
	R.append ("}}\n")
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
		N % esc ("Paragraphs: styles, indents dragged on the ruler, tab stops, spacing, bullets and numbers."),
		N % esc ("Images (PNG, JPEG, BMP, GIF, WebP), resized with the mouse."),
		N % esc ("Files: Rich Text Format, Word (.docx), OpenDocument (.odt), plain text; an export as HTML."),
	]
	for it in items:
		R.append ("\\pard\\plain\\s0\\ql\\li720\\fi-360\\sa120\\sl276\\slmult1{\\listtext\\f0\\fs24 \\'b7\\tab}{\\*\\pn\\pnlvlblt\\pnf0{\\pntxtb\\'b7}}%s\\par\n" % it)
	para (6, "{\\f0\\fs24\\i\\cf5 %s}" % esc ("“The spirit of KolibriOS: a lean system that does what it is asked, quickly.”"))
	para (1, "{\\f1\\fs36\\b %s}" % esc ("A few keys"))
	keys = [ "Ctrl+B, Ctrl+I, Ctrl+U: bold, italic, underline.", "Ctrl+L, Ctrl+E, Ctrl+R, Ctrl+J: the alignments.",
		 "Ctrl+Z, Ctrl+Y: undo, redo. Ctrl+F: find and replace.", "Shift+Enter: a new line in the paragraph; Tab in a list: a level down.",
		 "Tab in a table: the next cell (at the end, a new row); a double click on a header or a footer: its text edited (Esc: back to the page)." ]
	for i, k in enumerate (keys):
		R.append ("\\pard\\plain\\s0\\ql\\li720\\fi-360\\sa120\\sl276\\slmult1{\\listtext\\f0\\fs24 %d.\\tab}{\\*\\pn\\pnlvlbody\\pndec{\\pntxta .}}%s\\par\n" % (i + 1, N % esc (k)))
	# tables, pages and fields: a table of the files (a heading row, every other row shaded)
	para (1, "{\\f1\\fs36\\b %s}" % esc ("Tables, pages and fields"))
	para (0, (N % esc ("A document is laid out in pages: a header and a footer on each (the first page may have its own), the page's number and "
			   "their count, the date and the time as ")) + "{\\f0\\fs24\\b fields}" +
	      (N % esc (", page breaks, and a table of contents made from the headings (Insert > Table of Contents; Tools > Update Table of "
			"Contents). Tables have a heading row repeated on each page, merged cells, shading and borders; their columns are dragged "
			"on the ruler. The files Writer reads and writes:")), "\\qj")
	rows = [ ("File", "Extension", "Read", "Written"), ("Rich Text Format", ".rtf", "yes", "yes"), ("Microsoft Word", ".docx", "yes", "yes"),
		 ("OpenDocument Text", ".odt", "yes", "yes"), ("Plain text", ".txt", "yes", "yes"), ("Web page", ".html", "—", "exported") ]
	widths = [ 3200, 1700, 1500, 1500 ]
	edge = "".join ("\\clbrdr%s\\brdrs\\brdrw15\\brdrcf1" % e for e in "tlbr")
	for r, cells in enumerate (rows):
		fill = "\\clcbpat1" if r == 0 else "\\clcbpat9" if r % 2 == 0 else ""
		row = "\\trowd\\trgaph108\\trleft0\\trqc%s" % ("\\trhdr" if r == 0 else "")
		x = 0
		for w in widths: x += w; row += "%s%s\\cellx%d" % (edge, fill, x)
		R.append (row + "\n")
		for c, t in enumerate (cells):
			f = "\\f1\\fs22\\b\\cf8" if r == 0 else "\\f2\\fs20" if c == 1 else "\\f0\\fs22\\b" if c == 0 else "\\f0\\fs22"
			R.append ("\\pard\\plain\\intbl\\s0%s{%s %s}\\cell\n" % ("\\qc" if c >= 2 else "\\ql", f, esc (t)))
		R.append ("\\row\n")
	R.append ("\\pard\\plain\\s0\\qj\\sl276\\slmult1{\\f0\\fs24 }\\par\n")
	para (2, "{\\f1\\fs28\\b %s}" % esc ("A mail merge"))
	para (0, (N % esc ("Tools > Mail Merge fills a letter's fields with the records of a Cardfile form: a letter a record, all in one "
			   "document or each in a file of its own (RTF, .docx or .odt). From Cardfile: Record > Mail Merge. Try it with ")) +
	      (MONO % "SD:/docs/new-year-letter.rtf") + (N % esc (" and the Contacts form.")), "\\qj")
	R.append ("}\n")
	save ("writer-tour.rtf", R)

def letter ():
	R = []
	R.append ("{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n"
		  "{\\fonttbl{\\f0\\froman\\fcharset0 Liberation Serif;}{\\f1\\fswiss\\fcharset0 Liberation Sans;}}\n"
		  "{\\colortbl;\\red89\\green89\\blue89;\\red73\\green146\\blue167;}\n"
		  "{\\stylesheet{\\s0 Normal;}{\\s12 header;}{\\s13 footer;}}\n"
		  "{\\*\\docvar {OnyxMergeSource}{SD:/docs/contacts.card}}\n"
		  "\\paperw11906\\paperh16838\\margl1418\\margr1418\\margt1701\\margb1417\\viewkind1\n"
		  "\\sectd\\headery709\\footery567\n")
	HF = "\\f1\\fs18\\cf1"
	R.append ("{\\header\n\\pard\\plain\\s12\\ql{\\f1\\fs20\\b\\cf2 The Onyx household}\\par\n"
		  "\\pard\\plain\\s12\\qr{%s Lyon, }%s\\par\n}\n" % (HF, field ("DATE \\\\@ \"d MMMM yyyy\"", "1 January 2027", HF)))
	R.append ("{\\footer\n\\pard\\plain\\s13\\qc{%s %s}\\par\n}\n" % (HF, esc ("Written with Onyx Writer — a mail merge of Cardfile's Contacts")))
	M = lambda name, fmt = "\\f0\\fs24": field ("MERGEFIELD " + name, "\\'ab" + name + "\\'bb", fmt)
	N = "{\\f0\\fs24 %s}"
	B = "\\pard\\plain\\s0\\qj\\sa120\\sl276\\slmult1"
	R.append ("\\pard\\plain\\s0\\ql\\li5103\\sb480\\sl276\\slmult1%s\\par\n" % M ("name", "\\f0\\fs24\\b"))
	R.append ("\\pard\\plain\\s0\\ql\\li5103\\sa720\\sl276\\slmult1%s\\par\n" % M ("address"))
	R.append ("\\pard\\plain\\s0\\ql\\sa240\\sl276\\slmult1%s%s%s\\par\n" % (N % "Dear ", M ("name"), N % ","))
	R.append ("%s%s%s%s\\par\n" % (B, N % "As every year, we are writing to the people of our ", M ("group", "\\f0\\fs24\\i"),
				       N % " list to wish them a happy new year: health, time for the ones you love, and a few good surprises."))
	R.append ("%s%s%s%s\\par\n" % (B, N % "We have not forgotten your birthday either (", M ("birthday"), N % "): see you then, we hope."))
	R.append ("\\pard\\plain\\s0\\qj\\sb240\\sa120\\sl276\\slmult1%s\\par\n" % (N % "Best wishes,"))
	R.append ("\\pard\\plain\\s0\\ql\\sb480\\sa120\\sl276\\slmult1{\\f0\\fs24\\i The Onyx household}\\par\n")
	R.append ("}\n")
	save ("new-year-letter.rtf", R)

tour ()
letter ()
