#!/usr/bin/env python3
# gen_pcmn.py -- Ledger's default chart of accounts (the Belgian PCMN in French, the MAR in Dutch):
# tools/ledger/pcmn.tsv (code, French name, Dutch name) -> user/Apps/ledger/pcmn.h (a table in
# Latin-1, as Onyx shows text).
#
#   python3 tools/ledger/gen_pcmn.py                       (pcmn.h from pcmn.tsv)
#   python3 tools/ledger/gen_pcmn.py HEADINGS.tsv ODOO.csv... (pcmn.tsv made first: the headings --
#                                     class, two- and three-digit groups -- of a PCMN table, the working
#                                     accounts of Odoo's l10n_be chart -- account.account-be.csv and its
#                                     companies' account.account-be_comp.csv --, padded to six digits
#                                     as Odoo installs them)
#
# The account names are the PCMN's (Royal Decree of 21 October 2018, as amended), the working
# accounts' those of Odoo's Belgian localisation (LGPL-3), both official wording.
import csv, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
TSV = os.path.join(HERE, "pcmn.tsv")
OUT = os.path.join(HERE, "..", "..", "user", "Apps", "ledger", "pcmn.h")

# Accounts Odoo's chart leaves out that Ledger posts to (the result's appropriation, private sales...).
EXTRA = [
	("693000", "Bénéfice à reporter", "Over te dragen winst"),
	("793000", "Perte à reporter", "Over te dragen verlies"),
	("694000", "Rémunération du capital", "Vergoeding van het kapitaal"),
	("489000", "Autres dettes diverses", "Andere diverse schulden"),
]
DROP = { "4001" }			# (Odoo's point of sale's customers)

def make_tsv(headings, odoos):
	rows = {}
	for r in csv.reader(open(headings, encoding="utf-8"), delimiter="\t"):
		if len(r) < 3 or not r[0].isdigit() or len(r[0]) > 3: continue
		rows[r[0]] = (r[1].strip(), r[2].strip())
	for odoo in odoos:
		for r in csv.DictReader(open(odoo, encoding="utf-8")):
			c = r["code"].strip()
			if not c.isdigit() or c in DROP: continue
			code = (c + "000000")[:6]
			if code in rows: raise SystemExit("two accounts at " + code)
			rows[code] = ((r.get("name@fr") or r["name"]).strip(), (r.get("name@nl") or r["name"]).strip())
	for c, fr, nl in EXTRA: rows.setdefault(c, (fr, nl))
	with open(TSV, "w", encoding="utf-8") as f:
		f.write("# Ledger's default chart of accounts: code, French name (PCMN), Dutch name (MAR)\n")
		for code in sorted(rows): f.write("%s\t%s\t%s\n" % (code, rows[code][0], rows[code][1]))

def latin1(s):
	t = { "\u2019": "'", "\u2018": "'", "\u2013": "-", "\u2014": "-", "\u201c": '"', "\u201d": '"', "\u0153": "oe",
	      "\u0152": "OE", "\u20ac": "\x80", "\u2026": "...", "\u00a0": " " }
	s = "".join(t.get(ch, ch) for ch in s)
	out = []
	for ch in s:
		o = ord(ch)
		if o < 0x100: out.append(ch)
		else: out.append("?")
	return "".join(out)

# A name as long as Ledger keeps (127 characters: model.h ACC_NAME_MAX): the few longer ones cut at a word.
def fit(s, cap=127):
	if len(s) <= cap: return s
	cut = s[:cap - 3].rsplit(" ", 1)[0].rstrip(" ,;:-(")
	return cut + "..."

def c_str(s):
	b = fit(latin1(s))
	o = []
	for ch in b:
		n = ord(ch)
		if ch == "\\" or ch == '"': o.append("\\" + ch)
		elif n < 0x20 or n > 0x7E: o.append("\\x%02X\"\"" % n)
		else: o.append(ch)
	return '"' + "".join(o) + '"'

if len(sys.argv) >= 3: make_tsv(sys.argv[1], sys.argv[2:])
rows = []
for line in open(TSV, encoding="utf-8"):
	if line.startswith("#") or not line.strip(): continue
	code, fr, nl = line.rstrip("\n").split("\t")
	rows.append((code, fr, nl))
with open(OUT, "w", encoding="latin-1") as f:
	f.write("//\n// pcmn.h -- Ledger's default chart of accounts: the Belgian PCMN (French) / MAR (Dutch): its headings\n")
	f.write("// (classes, two- and three-digit groups) and the working accounts, six digits. Made by\n")
	f.write("// tools/ledger/gen_pcmn.py from tools/ledger/pcmn.tsv -- do not edit by hand.\n//\n")
	f.write("#ifndef _ledger_pcmn_h\n#define _ledger_pcmn_h\n\nnamespace lg {\n\n")
	f.write("struct PcmnRow { const char *code, *fr, *nl; };\nstatic const PcmnRow PCMN[] = {\n")
	for code, fr, nl in rows: f.write("\t{ \"%s\", %s, %s },\n" % (code, c_str(fr), c_str(nl)))
	f.write("};\nenum { NPCMN = sizeof PCMN / sizeof PCMN[0] };\n\n} // namespace lg\n\n#endif\n")
print(len(rows), "accounts ->", os.path.normpath(OUT))
