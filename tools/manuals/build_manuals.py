#!/usr/bin/env python3
# tools/manuals/build_manuals.py -- the Onyx manuals' PDFs: each sdcard/manuals/<app>/<Name>.md made a PDF next
# to it (<Name>.pdf), its pictures (images/*.png) in it. The Markdown is turned into HTML here -- the
# subset the manuals are written in, the one Onyx's manual reader shows too (below) -- laid out for A4 by a
# print style sheet in the Onyx documentation's colours (docs/assets/make_reference.py) and fonts (Selawik,
# the open Segoe UI, from sdcard/res/fonts), then printed to PDF by a headless Chrome, Chromium or Edge
# (the PDF's bookmarks made from the headings).
#
#   python tools/manuals/build_manuals.py [sdcard/manuals/ledger/Ledger.md ...]	(default: all of them)
#   python tools/manuals/build_manuals.py --html ...				(the HTML only, to look at it)
#
# The browser: $CHROME if set, else the first found of Chrome, Chromium, Edge (Windows, macOS, Linux; on
# Linux also Playwright's /opt/pw-browsers/chromium). Needs Python 3 only.
#
# The Markdown subset (CommonMark's and GitHub's usual forms):
#   # .. #### headings (their anchors as GitHub makes them: "## 6. Sales" -> #6-sales)
#   paragraphs; **bold**, *italic*, `code` (a key: `Ctrl+N`), [a link](#anchor or url)
#   - / * bulleted lists, 1. numbered lists, nested by indenting (a continuation indented too)
#   > quotes (a manual's notes and tips: "> **Tip.** ...")
#   | tables | with | a header |, its |---| line (:---: centred, ---: right)
#   ![a picture](images/x.png) alone in its paragraph: a figure; an *italic line* just after: its caption
#   ``` code blocks ```, --- a rule
import os, re, sys, html, shutil, subprocess, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.dirname (os.path.abspath (__file__))))
FONTS = os.path.join (ROOT, "sdcard", "res", "fonts")

# ---- Markdown -> HTML ---------------------------------------------------------------------------------------
def slug (text):
	"""A heading's anchor, as GitHub makes it: lower case, the punctuation out, a space a hyphen."""
	t = re.sub (r"<[^>]+>", "", text).strip ().lower ()
	t = re.sub (r"[^\w\- ]", "", t)
	return t.replace (" ", "-")

def inline (s):
	"""A line's inline marks -> HTML (the text escaped)."""
	out = []; i = 0
	codes = []
	# code spans first (their text as it is)
	def keep_code (m):
		codes.append (m.group (1)); return "\x00%d\x00" % (len (codes) - 1)
	s = re.sub (r"`([^`]+)`", keep_code, s)
	s = html.escape (s, quote = False)
	# pictures, links
	s = re.sub (r"!\[([^\]]*)\]\(([^)\s]+)(?:\s+&quot;([^&]*)&quot;)?\)", lambda m: '<img src="%s" alt="%s">' % (m.group (2), m.group (1)), s)
	s = re.sub (r"\[([^\]]+)\]\(([^)\s]+)\)", lambda m: '<a href="%s">%s</a>' % (m.group (2), m.group (1)), s)
	# bold, italic
	s = re.sub (r"\*\*(.+?)\*\*", r"<strong>\1</strong>", s)
	s = re.sub (r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"<em>\1</em>", s)
	def put_code (m):
		c = html.escape (codes[int (m.group (1))], quote = False)
		k = ' class="key"' if re.fullmatch (r"(Ctrl|Alt|Shift|Esc|Enter|Tab|Space|Delete|F\d+|[←→↑↓]|Ctrl\+\S+|Alt\+\S+|Shift\+\S+)([+, ].*)?", codes[int (m.group (1))]) else ""
		return "<code%s>%s</code>" % (k, c)
	s = re.sub (r"\x00(\d+)\x00", put_code, s)
	return s

LIST_RE = re.compile (r"^(\s*)([-*]|\d+\.)\s+(.*)$")

def blocks (lines):
	"""Lines -> HTML, block by block (lists and quotes parsed again inside)."""
	out = []; i = 0; n = len (lines)
	while i < n:
		line = lines[i]
		if not line.strip (): i += 1; continue
		# a code block
		if line.strip ().startswith ("```"):
			j = i + 1; code = []
			while j < n and not lines[j].strip ().startswith ("```"): code.append (lines[j]); j += 1
			out.append ("<pre><code>%s</code></pre>" % html.escape ("\n".join (code)))
			i = j + 1; continue
		# a heading
		m = re.match (r"^(#{1,6})\s+(.*?)\s*#*\s*$", line)
		if m:
			lv = len (m.group (1)); body = inline (m.group (2))
			out.append ('<h%d id="%s">%s</h%d>' % (lv, slug (m.group (2)), body, lv))
			i += 1; continue
		# a rule
		if re.match (r"^\s*(-{3,}|\*{3,})\s*$", line): out.append ("<hr>"); i += 1; continue
		# a quote
		if line.lstrip ().startswith (">"):
			q = []
			while i < n and lines[i].lstrip ().startswith (">"):
				q.append (re.sub (r"^\s*>\s?", "", lines[i])); i += 1
			out.append ("<blockquote>%s</blockquote>" % blocks (q)); continue
		# a table
		if line.lstrip ().startswith ("|") and i + 1 < n and re.match (r"^\s*\|?\s*:?-{2,}", lines[i + 1]):
			rows = []
			while i < n and lines[i].lstrip ().startswith ("|"): rows.append (lines[i].strip ()); i += 1
			out.append (table (rows)); continue
		# a list
		m = LIST_RE.match (line)
		if m:
			html_, i = list_block (lines, i); out.append (html_); continue
		# a paragraph (to the next blank line or block)
		p = [line.strip ()]; i += 1
		while i < n and lines[i].strip () and not LIST_RE.match (lines[i]) and not lines[i].lstrip ().startswith ((">", "|", "#", "```")):
			p.append (lines[i].strip ()); i += 1
		text = " ".join (p)
		fm = re.fullmatch (r"!\[([^\]]*)\]\(([^)\s]+)\)(?:\s+\*(.+)\*)?", text)
		if fm:								# a figure (its caption: an italic line after it)
			cap = fm.group (3)
			if not cap and i < n and re.fullmatch (r"\*[^*].*\*", lines[i].strip ()): cap = lines[i].strip ()[1:-1]; i += 1
			out.append ('<figure><img src="%s" alt="%s">%s</figure>' % (fm.group (2), html.escape (fm.group (1)),
				"<figcaption>%s</figcaption>" % inline (cap) if cap else ""))
			continue
		out.append ("<p>%s</p>" % inline (text))
	return "\n".join (out)

def list_block (lines, i):
	"""A list from lines[i] (its items, what is indented under each parsed again) -> (HTML, the next line)."""
	m = LIST_RE.match (lines[i]); base = len (m.group (1)); ordered = m.group (2)[0].isdigit ()
	start = int (m.group (2)[:-1]) if ordered else 1
	items = []; n = len (lines)
	while i < n:
		m = LIST_RE.match (lines[i])
		if not m or len (m.group (1)) != base or m.group (2)[0].isdigit () != ordered: break
		ind = len (lines[i]) - len (lines[i].lstrip ()) + len (m.group (2)) + 1
		body = [m.group (3)]; i += 1
		while i < n:
			l = lines[i]
			if not l.strip ():					# a blank line: the item goes on if what follows is indented
				if i + 1 < n and lines[i + 1].strip () and len (lines[i + 1]) - len (lines[i + 1].lstrip ()) > base: body.append (""); i += 1; continue
				break
			lead = len (l) - len (l.lstrip ())
			if lead <= base and (LIST_RE.match (l) or not body): break
			if lead <= base and not LIST_RE.match (l): body.append (l.strip ()); i += 1; continue	# (a lazy continuation)
			body.append (l[min (lead, ind):] if lead >= ind else l.strip ()); i += 1
		items.append (body)
		if i < n and not lines[i].strip (): break
	parts = []
	for b in items:
		if len (b) > 1 and any (LIST_RE.match (x) or not x for x in b[1:]):
			# the first line's paragraph, then the rest as blocks
			first = []; k = 0
			while k < len (b) and b[k] and not (k and LIST_RE.match (b[k])): first.append (b[k].strip ()); k += 1
			parts.append ("<li>%s%s</li>" % (inline (" ".join (first)), blocks (b[k:])))
		else:
			parts.append ("<li>%s</li>" % inline (" ".join (x.strip () for x in b)))
	tag = "ol" if ordered else "ul"
	st = ' start="%d"' % start if ordered and start != 1 else ""
	return "<%s%s>%s</%s>" % (tag, st, "".join (parts), tag), i

def table (rows):
	def cells (r):
		r = r.strip ()
		if r.startswith ("|"): r = r[1:]
		if r.endswith ("|"): r = r[:-1]
		return [c.strip () for c in r.split ("|")]
	head = cells (rows[0]); aligns = []
	for c in cells (rows[1]):
		aligns.append ("center" if c.startswith (":") and c.endswith (":") else "right" if c.endswith (":") else "")
	def row (cs, tag):
		# (a short cell -- "21 %", "100 %" -- kept on one line)
		return "<tr>%s</tr>" % "".join ('<%s%s%s>%s</%s>' % (tag, ' class="nw"' if tag == "td" and len (c) <= 6 else "",
			' style="text-align:%s"' % aligns[k] if k < len (aligns) and aligns[k] else "", inline (c), tag) for k, c in enumerate (cs))
	body = "".join (row (cells (r), "td") for r in rows[2:])
	return "<table><thead>%s</thead><tbody>%s</tbody></table>" % (row (head, "th"), body)

# ---- the page ----------------------------------------------------------------------------------------------------
CSS = """
@font-face { font-family: "Selawik"; src: url("FONTS/selawk.ttf"); font-weight: 400; }
@font-face { font-family: "Selawik"; src: url("FONTS/selawksl.ttf"); font-weight: 300; }
@font-face { font-family: "Selawik"; src: url("FONTS/selawksb.ttf"); font-weight: 600; }
@font-face { font-family: "Selawik"; src: url("FONTS/selawkb.ttf"); font-weight: 700; }
@font-face { font-family: "Onyx Mono"; src: url("FONTS/DejaVuSansMono.ttf"); }
@page { size: A4; margin: 20mm 17mm 20mm 17mm;
	@top-left { content: "TITLE"; font: 8.5pt "Selawik"; color: #5A6B7B; }
	@top-right { content: "Onyx"; font: 600 8.5pt "Selawik"; color: #C8962E; }
	@bottom-center { content: counter(page); font: 8.5pt "Selawik"; color: #5A6B7B; } }
@page :first { @top-left { content: none; } @top-right { content: none; } @bottom-center { content: none; } }
html { font: 10.5pt/1.45 "Selawik", "Segoe UI", sans-serif; color: #22262B; }
body { margin: 0; }
.cover { height: 247mm; display: flex; flex-direction: column; justify-content: center; border-left: 3mm solid #C8962E; padding-left: 12mm; }
.cover h1 { font: 300 34pt/1.15 "Selawik"; color: #101417; margin: 0 0 6mm; border: none; }
.cover p { font-size: 13pt; color: #5A6B7B; max-width: 140mm; }
.cover p:first-of-type { font-size: 15pt; color: #C8962E; font-style: italic; }
.cover .brand { margin-top: 30mm; font: 600 11pt "Selawik"; color: #2F5C8F; letter-spacing: 0.3em; }
h2 { font: 600 19pt/1.2 "Selawik"; color: #16324F; margin: 0 0 5mm; padding-bottom: 2mm; border-bottom: 0.6mm solid #C8962E;
	break-before: page; break-after: avoid; }
h3 { font: 600 13.5pt/1.25 "Selawik"; color: #2F5C8F; margin: 7mm 0 2.5mm; break-after: avoid; }
h4 { font: 600 11pt "Selawik"; color: #5A6B7B; margin: 5mm 0 2mm; break-after: avoid; }
p { margin: 0 0 2.6mm; orphans: 3; widows: 3; }
p:has(+ figure), p:has(+ table), p:has(+ ul), p:has(+ ol) { break-after: avoid; }
li { break-inside: avoid; }
td.nw { white-space: nowrap; }
a { color: #2F5C8F; text-decoration: none; }
strong { font-weight: 600; color: #101417; }
ul, ol { margin: 0 0 3mm; padding-left: 6mm; }
li { margin: 0 0 1.2mm; }
li > ul, li > ol { margin: 1.2mm 0 0; }
code { font: 8.8pt "Onyx Mono", monospace; color: #1B3A57; background: #F2F4F7; padding: 0.2mm 1mm; border-radius: 1mm; }
code.key { background: #FFFFFF; border: 0.25mm solid #C9D2DC; border-bottom-width: 0.6mm; padding: 0 1.2mm; }
pre { background: #F2F4F7; padding: 3mm; border-radius: 1.5mm; font-size: 8.8pt; }
pre code { background: none; padding: 0; }
blockquote { margin: 3mm 0 4mm; padding: 2.5mm 4mm; background: #FBF6EA; border-left: 1.2mm solid #C8962E; border-radius: 0 1.5mm 1.5mm 0;
	break-inside: avoid; }
blockquote p:last-child { margin-bottom: 0; }
figure { margin: 4mm 0 5mm; text-align: center; break-inside: avoid; }
figure img { max-width: 150mm; max-height: 125mm; }
figcaption { font-size: 9pt; color: #5A6B7B; font-style: italic; margin-top: 1.5mm; }
table { width: 100%; border-collapse: collapse; margin: 2mm 0 4mm; font-size: 9.2pt; break-inside: auto; }
thead { display: table-header-group; }
tr { break-inside: avoid; }
th { background: #16324F; color: #FFFFFF; font-weight: 600; text-align: left; padding: 1.6mm 2.2mm; }
td { padding: 1.4mm 2.2mm; border-bottom: 0.25mm solid #C9D2DC; vertical-align: top; }
tbody tr:nth-child(even) td { background: #F6F8FA; }
th strong { color: #FFFFFF; }
hr { border: none; border-top: 0.3mm solid #C9D2DC; margin: 5mm 0; }
#contents + ul { columns: 2; column-gap: 10mm; list-style: none; padding: 0; }
#contents + ul li { margin-bottom: 2mm; }
"""

def page (md_path, title):
	with open (md_path, encoding = "utf-8") as f: lines = f.read ().split ("\n")
	body = blocks (lines)
	# the cover: what comes before the first chapter
	k = body.find ("<h2")
	cover, rest = (body[:k], body[k:]) if k > 0 else ("", body)
	if cover: cover = '<section class="cover">%s<div class="brand">ONYX</div></section>' % cover
	fonts = "file:///" + FONTS.replace (os.sep, "/").lstrip ("/")
	base = "file:///" + os.path.dirname (os.path.abspath (md_path)).replace (os.sep, "/").lstrip ("/") + "/"
	css = CSS.replace ("FONTS", fonts).replace ("TITLE", title.replace ('"', "'"))
	return ('<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><base href="%s"><title>%s</title><style>%s</style></head>'
		'<body>%s%s</body></html>' % (base, html.escape (title), css, cover, rest))

# ---- the printing ------------------------------------------------------------------------------------------------
def browser ():
	if os.environ.get ("CHROME"): return os.environ["CHROME"]
	cands = []
	if os.name == "nt":
		for v in ("PROGRAMFILES", "PROGRAMFILES(X86)", "LOCALAPPDATA"):
			d = os.environ.get (v)
			if d: cands += [os.path.join (d, "Google", "Chrome", "Application", "chrome.exe"), os.path.join (d, "Microsoft", "Edge", "Application", "msedge.exe"),
					os.path.join (d, "Chromium", "Application", "chrome.exe")]
	elif sys.platform == "darwin":
		cands += ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "/Applications/Chromium.app/Contents/MacOS/Chromium",
			  "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge"]
	for n in ("google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "microsoft-edge", "msedge"):
		p = shutil.which (n)
		if p: cands.append (p)
	cands.append ("/opt/pw-browsers/chromium")
	for c in cands:
		if c and os.path.exists (c): return c
	return None

def to_pdf (html_path, pdf_path):
	b = browser ()
	if not b: sys.exit ("build_manuals: no Chrome, Chromium or Edge found (set CHROME to one)")
	with tempfile.TemporaryDirectory () as prof:
		args = [b, "--headless", "--disable-gpu", "--no-sandbox", "--no-pdf-header-footer", "--generate-pdf-document-outline",
			"--allow-file-access-from-files", "--user-data-dir=" + prof, "--virtual-time-budget=10000",
			"--print-to-pdf=" + os.path.abspath (pdf_path), "file:///" + os.path.abspath (html_path).replace (os.sep, "/").lstrip ("/")]
		r = subprocess.run (args, stdout = subprocess.PIPE, stderr = subprocess.PIPE)
	if not os.path.exists (pdf_path) or os.path.getmtime (pdf_path) < os.path.getmtime (html_path) - 1:
		sys.stderr.write (r.stderr.decode (errors = "replace")); sys.exit ("build_manuals: %s not printed" % pdf_path)

def main (argv):
	html_only = "--html" in argv
	mds = [a for a in argv if a.endswith (".md")]
	if not mds:
		top = os.path.join (ROOT, "sdcard", "manuals")
		for app in sorted (os.listdir (top)):
			d = os.path.join (top, app)
			if os.path.isdir (d): mds += [os.path.join (d, f) for f in sorted (os.listdir (d)) if f.endswith (".md")]
	for md in mds:
		with open (md, encoding = "utf-8") as f: first = f.readline ()
		title = re.sub (r"^#\s*", "", first).strip () or os.path.basename (md)[:-3]
		base = os.path.splitext (md)[0]
		out = base + ".html" if html_only else os.path.join (tempfile.gettempdir (), "onyx-manual-%s.html" % os.path.basename (base))
		with open (out, "w", encoding = "utf-8") as f: f.write (page (md, title))
		if html_only: print ("  " + os.path.relpath (out, ROOT)); continue
		to_pdf (out, base + ".pdf")
		os.remove (out)
		print ("  " + os.path.relpath (base + ".pdf", ROOT))
	return 0

if __name__ == "__main__":
	sys.exit (main (sys.argv[1:]))
