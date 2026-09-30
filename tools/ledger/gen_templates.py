#!/usr/bin/env python3
# gen_templates.py -- Ledger's document templates (user/Apps/ledger/print.h), written as Rich Text Format
# by hand: a quote, an order, a delivery note, a purchase order, an invoice, a credit note, in French
# (sdcard/apps/ledger.app/templates/), Dutch (templates/nl/) and English (templates/en/). Each one is
# an A4 page: the company's letterhead and the document's title, number and dates; the party's address
# (at the right, for a window envelope); the lines' table -- its heading row, then ONE row of Line...
# merge fields that Writer repeats for each line --; the totals; the VAT's detail (the legal mentions);
# the kind's own words (a quote's validity and its "for agreement", an invoice's payment); the company's
# legal line and bank at the foot. Their fields are Ledger's (print.h: «CompanyName», «PartyAddress»,
# «LineText», «Total»...); templates/fields.card holds them all with a sample's values (a template's
# merge source: Writer shows them). Open one in Writer to change it: its look, its words, a logo.
#
#   python3 tools/ledger/gen_templates.py
import os

ROOT = os.path.join (os.path.dirname (os.path.abspath (__file__)), "..", "..")
OUT = os.path.join (ROOT, "sdcard", "apps", "ledger.app", "templates")

def esc (s):
	o = []
	for ch in s:
		c = ord (ch)
		if ch in "\\{}": o.append ("\\" + ch)
		elif ch == "\u20ac": o.append ("\\'80")
		elif c < 0x80: o.append (ch)
		elif c < 0x100: o.append ("\\'%02x" % c)
		else: o.append ("\\u%d?" % (c if c < 0x8000 else c - 65536))
	return "".join (o)

def field (inst, result, fmt):
	return "{%s {\\field{\\*\\fldinst {%s }}{\\fldrslt {%s %s}}}}" % (fmt, inst, fmt, result)

def M (name, fmt):
	"""A merge field: «Name» in the format fmt."""
	return field ("MERGEFIELD " + name, "\\'ab" + name + "\\'bb", fmt)

def text (s, fmt):
	"""A text with merge fields written «Name» in it."""
	out = []; i = 0
	while i < len (s):
		a = s.find ("\u00ab", i)
		if a < 0: out.append ("{%s %s}" % (fmt, esc (s[i:]))); break
		if a > i: out.append ("{%s %s}" % (fmt, esc (s[i:a])))
		b = s.find ("\u00bb", a)
		out.append (M (s[a + 1:b], fmt)); i = b + 1
	return "".join (out)

# The words, by language.
W = {
"fr": dict (
	title = dict (quote = "DEVIS", order = "BON DE COMMANDE", delivery = "BON DE LIVRAISON", porder = "BON DE COMMANDE",
		      invoice = "FACTURE", creditnote = "NOTE DE CR\u00c9DIT"),
	no = "N\u00b0 ", date = "Date : ", customer = "CLIENT", supplier = "FOURNISSEUR",
	cols = ("Description", "Qt\u00e9", "Prix unitaire", "TVA", "Total HTVA"), dcols = ("Description", "Quantit\u00e9"),
	net = "Total HTVA", vat = "TVA", total = "Total TVAC", refund = "Total TVAC \u00e0 d\u00e9duire",
	quote = ["Ce devis est valable jusqu'au \u00abUntil\u00bb. Nos prix s'entendent hors TVA ; conditions de paiement : \u00abTermsText\u00bb.",
		 "Pour accord, merci de nous retourner ce devis dat\u00e9 et sign\u00e9."],
	quoteSign = "Pour accord \u2014 date et signature du client :",
	order = ["Nous vous remercions de votre commande, que nous confirmons aux conditions ci-dessus. Conditions de paiement : \u00abTermsText\u00bb."],
	delivery = ["Merci de v\u00e9rifier les marchandises \u00e0 leur r\u00e9ception."],
	deliverySign = "Marchandises re\u00e7ues en bon \u00e9tat \u2014 date, nom et signature :",
	porder = ["Merci de nous livrer les articles ci-dessus aux conditions convenues et de rappeler notre num\u00e9ro de commande \u00abNumber\u00bb sur votre bon de livraison et votre facture."],
	invoice = ["Merci de verser le montant de \u00abTotal\u00bb \u20ac au plus tard le \u00abDueDate\u00bb sur notre compte \u00abCompanyIBAN\u00bb (BIC \u00abCompanyBIC\u00bb) en mentionnant la communication structur\u00e9e \u00abCommunication\u00bb."],
	creditnote = ["Le montant de cette note de cr\u00e9dit sera d\u00e9duit de votre prochain paiement ou rembours\u00e9 sur votre compte.",
		      "TVA \u00e0 reverser \u00e0 l'\u00c9tat dans la mesure o\u00f9 elle a \u00e9t\u00e9 initialement d\u00e9duite."],
	page = "Page ", of = " / "),
"nl": dict (
	title = dict (quote = "OFFERTE", order = "BESTELBON", delivery = "LEVERINGSBON", porder = "BESTELBON", invoice = "FACTUUR", creditnote = "CREDITNOTA"),
	no = "Nr. ", date = "Datum: ", customer = "KLANT", supplier = "LEVERANCIER",
	cols = ("Omschrijving", "Aantal", "Eenheidsprijs", "Btw", "Totaal excl. btw"), dcols = ("Omschrijving", "Aantal"),
	net = "Totaal excl. btw", vat = "Btw", total = "Totaal incl. btw", refund = "Totaal incl. btw in mindering",
	quote = ["Deze offerte is geldig tot \u00abUntil\u00bb. Onze prijzen zijn exclusief btw; betalingsvoorwaarden: \u00abTermsText\u00bb.",
		 "Voor akkoord: gelieve deze offerte gedateerd en ondertekend terug te sturen."],
	quoteSign = "Voor akkoord \u2014 datum en handtekening van de klant:",
	order = ["Wij danken u voor uw bestelling, die wij bevestigen tegen de bovenstaande voorwaarden. Betalingsvoorwaarden: \u00abTermsText\u00bb."],
	delivery = ["Gelieve de goederen bij ontvangst te controleren."],
	deliverySign = "Goederen in goede staat ontvangen \u2014 datum, naam en handtekening:",
	porder = ["Gelieve de bovenstaande artikelen te leveren tegen de afgesproken voorwaarden en ons bestelnummer \u00abNumber\u00bb te vermelden op uw leveringsbon en uw factuur."],
	invoice = ["Gelieve het bedrag van \u00abTotal\u00bb \u20ac uiterlijk op \u00abDueDate\u00bb over te schrijven op onze rekening \u00abCompanyIBAN\u00bb (BIC \u00abCompanyBIC\u00bb) met de gestructureerde mededeling \u00abCommunication\u00bb."],
	creditnote = ["Het bedrag van deze creditnota wordt in mindering gebracht van uw volgende betaling of op uw rekening teruggestort.",
		      "Btw terug te storten aan de Staat in de mate dat ze oorspronkelijk in aftrek werd gebracht."],
	page = "Pagina ", of = " / "),
"en": dict (
	title = dict (quote = "QUOTE", order = "ORDER", delivery = "DELIVERY NOTE", porder = "PURCHASE ORDER", invoice = "INVOICE", creditnote = "CREDIT NOTE"),
	no = "No. ", date = "Date: ", customer = "CUSTOMER", supplier = "SUPPLIER",
	cols = ("Description", "Qty", "Unit price", "VAT", "Total excl. VAT"), dcols = ("Description", "Quantity"),
	net = "Total excl. VAT", vat = "VAT", total = "Total incl. VAT", refund = "Total incl. VAT credited",
	quote = ["This quote is valid until \u00abUntil\u00bb. Our prices exclude VAT; payment terms: \u00abTermsText\u00bb.",
		 "To accept it, please return this quote dated and signed."],
	quoteSign = "Accepted \u2014 date and signature:",
	order = ["Thank you for your order, which we confirm on the terms above. Payment terms: \u00abTermsText\u00bb."],
	delivery = ["Please check the goods on receipt."],
	deliverySign = "Goods received in good condition \u2014 date, name and signature:",
	porder = ["Please deliver the items above on the agreed terms and quote our order number \u00abNumber\u00bb on your delivery note and invoice."],
	invoice = ["Please pay \u00abTotal\u00bb EUR by \u00abDueDate\u00bb to our account \u00abCompanyIBAN\u00bb (BIC \u00abCompanyBIC\u00bb), quoting the structured communication \u00abCommunication\u00bb."],
	creditnote = ["The amount of this credit note will be deducted from your next payment or refunded to your account.",
		      "VAT to be repaid to the State insofar as it was initially deducted."],
	page = "Page ", of = " / "),
}

TW = 9638					# the text's width (A4, 2 cm margins), twips
ACC, GREY, FILL, WHITE, LINE, INK = 1, 2, 3, 4, 5, 6	# the colours' numbers

def cells (widths, left = 0, fill = None, rows = False):
	"""A row's cells' definitions: their right edges (absolute), a fill, lines at their tops and bottoms."""
	o = ""; x = left
	for i, w in enumerate (widths):
		x += w
		if rows: o += "\\clbrdrt\\brdrs\\brdrw10\\brdrcf%d\\clbrdrb\\brdrs\\brdrw10\\brdrcf%d" % (LINE, LINE)
		f = fill[i] if isinstance (fill, (list, tuple)) else fill
		if f: o += "\\clcbpat%d" % f
		o += "\\cellx%d" % x
	return o

def cellp (content, align = "\\ql", extra = "", last = True):
	return "\\pard\\plain\\intbl%s%s %s%s\n" % (align, extra, content, "\\cell" if last else "\\par")

def template (lang, kind):
	w = W[lang]
	R = []
	R.append ("{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n"
		  "{\\fonttbl{\\f0\\fswiss\\fcharset0 Liberation Sans;}}\n"
		  "{\\colortbl;\\red44\\green94\\blue120;\\red102\\green102\\blue102;\\red232\\green241\\blue243;\\red255\\green255\\blue255;"
		  "\\red196\\green206\\blue212;\\red38\\green38\\blue38;}\n"
		  "{\\stylesheet{\\s0 Normal;}{\\s12 header;}{\\s13 footer;}}\n"
		  "{\\*\\docvar {OnyxMergeSource}{SD:/apps/ledger.app/templates/fields.card}}\n"
		  "\\paperw11906\\paperh16838\\margl1134\\margr1134\\margt851\\margb1418\\viewkind1\n"
		  "\\sectd\\footery567\n")
	# the foot: the company's legal line, its bank, the page
	F = "\\f0\\fs15\\cf%d" % GREY
	R.append ("{\\footer\n\\pard\\plain\\s13\\qc%s\\par\n" % M ("CompanyLegalLine", F))
	R.append ("\\pard\\plain\\s13\\qc%s{%s   \\u183?   %s}%s{%s %s}%s\\par\n}\n" % (M ("CompanyBankLine", F), F, esc (w["page"]), field ("PAGE", "1", F),
											    F, esc (w["of"]), field ("NUMPAGES", "1", F)))
	B = "\\f0\\fs19\\cf%d" % INK
	S = "\\f0\\fs17\\cf%d" % GREY
	# the letterhead: the company at the left, the document at the right
	R.append ("\\trowd\\trgaph0\\trleft0%s\n" % cells ((5600, TW - 5600)))
	R.append (cellp (M ("CompanyName", "\\f0\\fs32\\b\\cf%d" % ACC), extra = "\\sa60", last = False))
	R.append (cellp (M ("CompanyAddress", S), last = False))
	R.append (cellp (M ("CompanyVATLine", S), last = False))
	R.append (cellp (M ("CompanyContact", S)))
	R.append (cellp ("{\\f0\\fs40\\b\\cf%d %s}" % (ACC, esc (w["title"][kind])), "\\qr", "\\sa60", last = False))
	R.append (cellp ("{\\f0\\fs20\\b\\cf%d %s}%s" % (INK, esc (w["no"]), M ("Number", "\\f0\\fs20\\b\\cf%d" % INK)), "\\qr", last = False))
	R.append (cellp (text (w["date"] + "\u00abDate\u00bb", B), "\\qr", last = False))
	R.append (cellp (M ("UntilLine", B), "\\qr"))
	R.append ("\\row\n")
	R.append ("\\pard\\plain\\sb0\\sa0{\\f0\\fs12 }\\par\n")
	# the party (at the right: a window envelope's), the references at the left
	R.append ("\\trowd\\trgaph0\\trleft0%s\n" % cells ((5600, TW - 5600)))
	R.append (cellp (M ("ReferenceLine", B), extra = "\\sb480", last = False))
	R.append (cellp (M ("FromLine", B)))
	R.append (cellp ("{\\f0\\fs15\\b\\cf%d %s}" % (GREY, esc (w["supplier" if kind == "porder" else "customer"])), extra = "\\sb240\\sa60", last = False))
	R.append (cellp (M ("PartyName", "\\f0\\fs23\\b\\cf%d" % INK), extra = "\\sa40", last = False))
	R.append (cellp (M ("PartyAddress", "\\f0\\fs21\\cf%d" % INK), last = False))
	R.append (cellp (M ("PartyVATLine", S), extra = "\\sb60"))
	R.append ("\\row\n")
	# what it is about
	R.append ("\\pard\\plain\\ql\\sb480\\sa160%s\\par\n" % M ("Text", "\\f0\\fs22\\b\\cf%d" % INK))
	# the lines: a heading row, the lines' row (repeated by Writer)
	H = "\\f0\\fs17\\b\\cf%d" % WHITE
	if kind == "delivery":
		widths = (TW - 1800, 1800); heads = w["dcols"]; fields = ("LineText", "LineQty")
	else:
		widths = (TW - 900 - 1500 - 800 - 1700, 900, 1500, 800, 1700); heads = w["cols"]
		fields = ("LineText", "LineQty", "LinePrice", "LineVAT", "LineTotal")
	R.append ("\\trowd\\trgaph100\\trleft0\\trhdr%s\n" % cells (widths, fill = ACC, rows = True))
	for i, h in enumerate (heads):
		R.append (cellp ("{%s %s}" % (H, esc (h)), "\\ql" if i == 0 else "\\qr", "\\sb60\\sa60"))
	R.append ("\\row\n")
	R.append ("\\trowd\\trgaph100\\trleft0%s\n" % cells (widths, rows = True))
	for i, f in enumerate (fields):
		R.append (cellp (M (f, B), "\\ql" if i == 0 else "\\qr", "\\sb60\\sa60"))
	R.append ("\\row\n")
	# the totals, at the right
	if kind != "delivery":
		R.append ("\\pard\\plain\\sb0\\sa0{\\f0\\fs8 }\\par\n")
		tw = (2900, 1700); left = TW - sum (tw)
		for label, name, strong in ((w["net"], "TotalNet", False), (w["vat"], "TotalVAT", False),
					    (w["refund"] if kind == "creditnote" else w["total"], "Total", True)):
			R.append ("\\trowd\\trgaph100\\trleft%d%s\n" % (left, cells (tw, left, fill = FILL if strong else None)))
			fmt = "\\f0\\fs%d%s\\cf%d" % (21 if strong else 19, "\\b" if strong else "", INK)
			R.append (cellp ("{%s %s}" % (fmt, esc (label)), "\\ql", "\\sb40\\sa40"))
			R.append (cellp (M (name, fmt) + "{%s  \\'80}" % fmt, "\\qr", "\\sb40\\sa40"))
			R.append ("\\row\n")
		R.append ("\\pard\\plain\\ql\\sb200\\sa120%s\\par\n" % M ("VATDetail", "\\f0\\fs16\\cf%d" % GREY))
	# the kind's own words
	for p in w[kind]:
		R.append ("\\pard\\plain\\qj\\sb120\\sa60\\sl264\\slmult1%s\\par\n" % text (p, B))
	sign = w.get (kind + "Sign")
	if sign:
		R.append ("\\pard\\plain\\ql\\sb480\\sa1200{\\f0\\fs19\\b\\cf%d %s}\\par\n" % (INK, esc (sign)))
	R.append ("}\n")
	d = OUT if lang == "fr" else os.path.join (OUT, lang)
	os.makedirs (d, exist_ok = True)
	path = os.path.join (d, kind + ".rtf")
	with open (path, "w", encoding = "ascii") as f: f.write ("".join (R))
	return path

# The fields' sample: a Cardfile form of one record (the demo company's quote), each of Ledger's fields --
# the lines' too, with the first line's values -- so that Writer's Tools > Mail Merge lists them and shows
# them in a template (its OnyxMergeSource).
SAMPLE = [
	("Kind", "Devis"), ("Number", "2026/0001"), ("Date", "28/09/2026"), ("Until", "28/10/2026"), ("DueDate", ""),
	("UntilLine", "Valable jusqu'au 28/10/2026"), ("Reference", "CMD-4411"), ("ReferenceLine", "Votre r\u00e9f\u00e9rence : CMD-4411"),
	("Text", "La nouvelle carte du restaurant"), ("Communication", ""), ("Terms", "30"), ("TermsText", "30 jours"),
	("FromDocument", ""), ("FromLine", ""), ("FileName", "Quote 2026-0001 Brasserie du Sablon SRL"),
	("CompanyName", "Atelier Lumen SRL"), ("CompanyLegal", "SRL"), ("CompanyStreet", "Rue des Tanneurs 58"), ("CompanyZip", "1000"),
	("CompanyCity", "Bruxelles"), ("CompanyCountry", "Belgique"), ("CompanyAddress", "Rue des Tanneurs 58\n1000 Bruxelles"),
	("CompanyVAT", "BE 0721.583.097"), ("CompanyVATLine", "TVA BE 0721.583.097"), ("CompanyEmail", "compta@atelier-lumen.be"),
	("CompanyPhone", "+32 2 512 44 90"), ("CompanyWeb", "www.atelier-lumen.be"),
	("CompanyContact", "T\u00e9l. +32 2 512 44 90\ncompta@atelier-lumen.be\nwww.atelier-lumen.be"),
	("CompanyIBAN", "BE41 3630 8123 4510"), ("CompanyBIC", "BBRUBEBB"), ("CompanyBankLine", "IBAN BE41 3630 8123 4510  \u00b7  BIC BBRUBEBB"),
	("CompanyRegister", "RPM Bruxelles"),
	("CompanyLegalLine", "Atelier Lumen SRL  \u00b7  Rue des Tanneurs 58, 1000 Bruxelles  \u00b7  TVA BE 0721.583.097  \u00b7  RPM Bruxelles"),
	("PartyName", "Brasserie du Sablon SRL"), ("PartyStreet", "Place du Grand Sablon 12"), ("PartyZip", "1000"), ("PartyCity", "Bruxelles"),
	("PartyCountry", ""), ("PartyAddress", "Place du Grand Sablon 12\n1000 Bruxelles"), ("PartyVAT", "BE 0745.321.175"),
	("PartyVATLine", "TVA BE 0745.321.175"), ("PartyEmail", "info@brasseriedusablon.be"), ("PartyPhone", "+32 2 511 20 20"), ("PartyCode", "BRASSERI"),
	("TotalNet", "1.495,00"), ("TotalVAT", "313,95"), ("Total", "1.808,95"), ("VATDetail", "21 % : 313,95 sur 1.495,00"),
	("LineNo", "1"), ("LineText", "Design of the new menu"), ("LineQty", "2,5"), ("LinePrice", "450,00"), ("LineVAT", "21 %"),
	("LineTotal", "1.125,00"), ("LineTax", "236,25"), ("LineGross", "1.361,25") ]

def fields_card ():
	o = ["# Onyx Cardfile -- a form and its records (open it with Cardfile)\n[form]\nversion = 1\n",
	     "title = Ledger's merge fields\n",
	     "description = The fields a document's template can hold (print.h), with a quote's values: Writer's Tools > Mail Merge lists them.\n"]
	for k, v in SAMPLE:
		o.append ("\n[field]\ncolumn = %s\nlabel = %s\ntype = %s\n" % (k, k, "multiline" if "\n" in v else "text"))
	o.append ("\n[records]\n" + "\t".join (k for k, v in SAMPLE) + "\n")
	o.append ("\t".join (v.replace ("\\", "\\\\").replace ("\n", "\\n") for k, v in SAMPLE) + "\n")
	path = os.path.join (OUT, "fields.card")
	with open (path, "w", encoding = "latin-1") as f: f.write ("".join (o))
	return path

p = fields_card ()
print (os.path.relpath (p, ROOT), os.path.getsize (p), "bytes")
for lang in ("fr", "nl", "en"):
	for kind in ("quote", "order", "delivery", "porder", "invoice", "creditnote"):
		p = template (lang, kind)
		print (os.path.relpath (p, ROOT), os.path.getsize (p), "bytes")
