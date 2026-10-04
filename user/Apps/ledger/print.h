//
// print.h -- a document printed from a template: a quote, an order, a delivery note, a purchase order, an
// invoice, a credit note. Its template is a Letters document (.docx, .odt or .rtf -- the first found) in
// SD:/apps/ledger.app/templates/, named after its kind (quote, order, delivery, porder, invoice,
// creditnote): the French ones there, the Dutch and English ones in its nl/ and en/ folders -- the
// party's language chosen (its card's), else the company's (its chart's); a language without its own
// template takes the French one. Letters fills its merge fields (Letters' merge.h) with the document's (a
// Cardfile form of one record: merge.card):
//   Kind Number Date Until DueDate UntilLine Reference ReferenceLine Text Communication Terms TermsText
//   FromDocument FromLine FileName
//   CompanyName CompanyLegal CompanyStreet CompanyZip CompanyCity CompanyCountry CompanyAddress CompanyVAT
//   CompanyVATLine CompanyEmail CompanyPhone CompanyWeb CompanyContact CompanyIBAN CompanyBIC CompanyBankLine
//   CompanyRegister CompanyLegalLine
//   PartyName PartyStreet PartyZip PartyCity PartyCountry PartyAddress PartyVAT PartyVATLine PartyEmail
//   PartyPhone PartyCode
//   TotalNet TotalVAT Total VATDetail
// (the ...Line, ...Address and ...Contact ones made to be printed as they are: a label and its value,
// an address's lines -- empty when there is no value), and with its lines' (merge-lines.card, a record a
// line: the table row holding them repeated) --
//   LineNo LineText LineQty LinePrice LineVAT LineTotal LineTax LineGross
// The document made is written in SD:/docs/<its kind>/ (named after its number and its party) and shown
// in Letters, which can save it again in any of its formats.
//
#ifndef _ledger_print_h
#define _ledger_print_h

#include "commerce.h"
#include "ui.h"

namespace lg {

static const char *TEMPLATES = "SD:/apps/ledger.app/templates";
static const char *MERGE_HEAD = "SD:/apps/ledger.app/merge.card", *MERGE_LINES = "SD:/apps/ledger.app/merge-lines.card",
		  *MERGE_JOB = "SD:/apps/ledger.app/merge.job";
enum { PK_QUOTE, PK_ORDER, PK_DELIVERY, PK_PORDER, PK_INVOICE, PK_CREDIT, PK_KINDS };
static const char *const PK_FILE[PK_KINDS] = { "quote", "order", "delivery", "porder", "invoice", "creditnote" };
static const char *const PK_FOLDER[PK_KINDS] = { "Quotes", "Orders", "Delivery notes", "Purchase orders", "Invoices", "Credit notes" };
static const char *const PK_TITLE[PK_KINDS] = { "Quote", "Order", "Delivery note", "Purchase order", "Invoice", "Credit note" };
static int pk_of (int cdKind) { return cdKind == CD_QUOTE ? PK_QUOTE : cdKind == CD_ORDER ? PK_ORDER : cdKind == CD_DELIVERY ? PK_DELIVERY : PK_PORDER; }

// ---- the documents' languages -------------------------------------------------------------------------------------------------
enum { DL_FR, DL_NL, DL_EN };
// A party's documents' language: its card's, else the company's (its chart's).
static int doc_lang (int party)
{
	const Party *p = party_of (g_b, party);
	return lang_index (p && p->lang[0] ? p->lang : g_b.chart);
}
static const char *const DOC_KIND[3][PK_KINDS] = {
	{ "Devis", "Bon de commande", "Bon de livraison", "Bon de commande", "Facture", "Note de cr\xE9" "dit" },
	{ "Offerte", "Bestelbon", "Leveringsbon", "Bestelbon", "Factuur", "Creditnota" },
	{ "Quote", "Order", "Delivery note", "Purchase order", "Invoice", "Credit note" } };
// Words said in the documents' languages: [fr, nl, en].
struct Tr { const char *w[3]; };
static const Tr T_VAT = { { "TVA", "BTW", "VAT" } }, T_TEL = { { "T\xE9l. ", "Tel. ", "Phone " } },
		T_VALID = { { "Valable jusqu'au ", "Geldig tot ", "Valid until " } },
		T_PLANNED = { { "Livraison pr\xE9vue le ", "Voorziene levering op ", "Delivery planned on " } },
		T_WANTED = { { "Livraison souhait\xE9" "e le ", "Gewenste levering op ", "Delivery wanted on " } },
		T_DUE = { { "\xC9" "ch\xE9" "ance : ", "Vervaldag: ", "Due date: " } },
		T_YOURREF = { { "Votre r\xE9" "f\xE9" "rence : ", "Uw referentie: ", "Your reference: " } },
		T_REF = { { "R\xE9" "f\xE9" "rence : ", "Referentie: ", "Reference: " } },
		T_DAYS = { { " jours", " dagen", " days" } }, T_CASH = { { "comptant", "contant", "cash" } },
		T_ON = { { " sur ", " op ", " on " } }, T_COLON = { { " : ", ": ", ": " } }, T_FROM = { { "R\xE9" "f. : ", "Ref.: ", "Ref.: " } };
static const char *tr (const Tr &t, int lang) { return t.w[lang >= 0 && lang < 3 ? lang : 0]; }
// A country's name (BE, FR... -> "Belgique", "Frankrijk"...), else its code.
static const char *country_name (const char *code, int lang)
{
	static const char *const C[][4] = {
		{ "BE", "Belgique", "Belgi\xEB", "Belgium" }, { "NL", "Pays-Bas", "Nederland", "Netherlands" }, { "FR", "France", "Frankrijk", "France" },
		{ "DE", "Allemagne", "Duitsland", "Germany" }, { "LU", "Luxembourg", "Luxemburg", "Luxembourg" }, { "GB", "Royaume-Uni", "Verenigd Koninkrijk", "United Kingdom" },
		{ "IT", "Italie", "Itali\xEB", "Italy" }, { "ES", "Espagne", "Spanje", "Spain" }, { "PT", "Portugal", "Portugal", "Portugal" },
		{ "AT", "Autriche", "Oostenrijk", "Austria" }, { "IE", "Irlande", "Ierland", "Ireland" }, { "DK", "Danemark", "Denemarken", "Denmark" },
		{ "SE", "Su\xE8" "de", "Zweden", "Sweden" }, { "FI", "Finlande", "Finland", "Finland" }, { "PL", "Pologne", "Polen", "Poland" },
		{ "CZ", "Tch\xE9quie", "Tsjechi\xEB", "Czechia" }, { "SK", "Slovaquie", "Slowakije", "Slovakia" }, { "HU", "Hongrie", "Hongarije", "Hungary" },
		{ "RO", "Roumanie", "Roemeni\xEB", "Romania" }, { "BG", "Bulgarie", "Bulgarije", "Bulgaria" }, { "GR", "Gr\xE8" "ce", "Griekenland", "Greece" },
		{ "HR", "Croatie", "Kroati\xEB", "Croatia" }, { "SI", "Slov\xE9nie", "Sloveni\xEB", "Slovenia" }, { "EE", "Estonie", "Estland", "Estonia" },
		{ "LV", "Lettonie", "Letland", "Latvia" }, { "LT", "Lituanie", "Litouwen", "Lithuania" }, { "CY", "Chypre", "Cyprus", "Cyprus" },
		{ "MT", "Malte", "Malta", "Malta" }, { "CH", "Suisse", "Zwitserland", "Switzerland" }, { "NO", "Norv\xE8ge", "Noorwegen", "Norway" },
		{ "US", "\xC9tats-Unis", "Verenigde Staten", "United States" }, { "CA", "Canada", "Canada", "Canada" }, { "JP", "Japon", "Japan", "Japan" },
		{ "CN", "Chine", "China", "China" } };
	for (unsigned i = 0; i < sizeof C / sizeof C[0]; i++) if (ci_eq (code, C[i][0])) return C[i][1 + (lang >= 0 && lang < 3 ? lang : 0)];
	return code;
}

// ---- the merge's data ---------------------------------------------------------------------------------------------------------
// The document's record (its fields added as they are set), its lines' records.
struct MergeSet { cf::Doc h, l; int hr; };
static void ms_init (MergeSet &m) { cf::doc_init (m.h); cf::doc_init (m.l); m.hr = -1; }
static void ms_free (MergeSet &m) { cf::doc_clear (m.h); cf::doc_clear (m.l); }
static void ms_head (MergeSet &m, const char *name, const char *value)
{
	if (m.hr < 0) m.hr = cf::doc_add_record (m.h, -1);
	bool multi = false; for (const char *q = value; q && *q; q++) if (*q == '\n') multi = true;
	cf::Field f; cf::field_init (f, name, name, multi ? FT_MEMO : FT_TEXT);		// (a memo's lines: Letters' line breaks)
	int k = cf::doc_insert_field (m.h, m.h.nf, f);
	if (k >= 0) sset (m.h.r[m.hr][k], value ? value : "");
}
static void ms_head_money (MergeSet &m, const char *name, money v) { char t[32]; fmt_money (v, t); ms_head (m, name, t); }
static void ms_head_date (MergeSet &m, const char *name, int d) { char t[16]; t[0] = '\0'; if (d) date_show (d, t); ms_head (m, name, t); }
// "label value" (empty without a value).
static void ms_head_line (MergeSet &m, const char *name, const char *label, const char *value)
{
	char t[200] = ""; if (value && value[0]) { scpy (t, label, sizeof t); scat (t, value, sizeof t); }
	ms_head (m, name, t);
}
static void ms_lines_fields (MergeSet &m)
{
	static const char *const F[8] = { "LineNo", "LineText", "LineQty", "LinePrice", "LineVAT", "LineTotal", "LineTax", "LineGross" };
	for (int i = 0; i < 8; i++) { cf::Field f; cf::field_init (f, F[i], F[i], FT_TEXT); cf::doc_insert_field (m.l, m.l.nf, f); }
}
static void ms_line (MergeSet &m, const char *text, long long qty, money price, int vat, money total)
{
	int r = cf::doc_add_record (m.l, -1);
	char t[48];
	itoa10 (m.l.nr, t); sset (m.l.r[r][0], t);
	sset (m.l.r[r][1], text);
	qty_show (qty, t); sset (m.l.r[r][2], t);
	fmt_money (price, t); sset (m.l.r[r][3], t);
	if (vat >= 0 && vat < NVAT) fmt_rate (VAT_DEFS[vat].rate, t); else t[0] = '\0';
	sset (m.l.r[r][4], t);
	fmt_money (total, t); sset (m.l.r[r][5], t);
	money tax = vat >= 0 && vat < NVAT && !vat_reverse (vat) ? tax_of (total, VAT_DEFS[vat].rate) : 0;
	fmt_money (tax, t); sset (m.l.r[r][6], t);
	fmt_money (total + tax, t); sset (m.l.r[r][7], t);
}
// An address's lines: the street, the postcode and city, the country when it is not `home`.
static void address_lines (const char *street, const char *zip, const char *city, const char *country, const char *home, int lang, char *out, int cap)
{
	out[0] = '\0';
	if (street[0]) scpy (out, street, cap);
	if (zip[0] || city[0])
	{
		if (out[0]) scat (out, "\n", cap);
		scat (out, zip, cap); if (zip[0] && city[0]) scat (out, " ", cap); scat (out, city, cap);
	}
	if (country[0] && !ci_eq (country, home)) { if (out[0]) scat (out, "\n", cap); scat (out, country_name (country, lang), cap); }
}
// The company's and the party's fields.
static void ms_parties (MergeSet &m, int party, int lang)
{
	char t[64], a[300];
	const Party *p = party_of (g_b, party);
	Party none; party_init (none);
	if (!p) p = &none;
	const char *home = g_b.country[0] ? g_b.country : "BE", *there = p->country[0] ? p->country : "BE";
	// the company (its country said to a party abroad)
	ms_head (m, "CompanyName", g_b.name); ms_head (m, "CompanyLegal", g_b.legal);
	ms_head (m, "CompanyStreet", g_b.street); ms_head (m, "CompanyZip", g_b.zip); ms_head (m, "CompanyCity", g_b.city);
	ms_head (m, "CompanyCountry", country_name (home, lang));
	address_lines (g_b.street, g_b.zip, g_b.city, home, there, lang, a, sizeof a); ms_head (m, "CompanyAddress", a);
	vat_show (g_b.vat, t, sizeof t); ms_head (m, "CompanyVAT", t);
	{ char l[8]; scpy (l, tr (T_VAT, lang), sizeof l); scat (l, " ", sizeof l); ms_head_line (m, "CompanyVATLine", l, t); }
	ms_head (m, "CompanyEmail", g_b.email); ms_head (m, "CompanyPhone", g_b.phone); ms_head (m, "CompanyWeb", g_b.web);
	a[0] = '\0';
	if (g_b.phone[0]) { scpy (a, tr (T_TEL, lang), sizeof a); scat (a, g_b.phone, sizeof a); }
	if (g_b.email[0]) { if (a[0]) scat (a, "\n", sizeof a); scat (a, g_b.email, sizeof a); }
	if (g_b.web[0]) { if (a[0]) scat (a, "\n", sizeof a); scat (a, g_b.web, sizeof a); }
	ms_head (m, "CompanyContact", a);
	iban_show (g_b.iban, t, sizeof t); ms_head (m, "CompanyIBAN", t); ms_head (m, "CompanyBIC", g_b.bic);
	a[0] = '\0';
	if (t[0]) { scpy (a, "IBAN ", sizeof a); scat (a, t, sizeof a); if (g_b.bic[0]) { scat (a, "  \xB7  BIC ", sizeof a); scat (a, g_b.bic, sizeof a); } }
	ms_head (m, "CompanyBankLine", a);
	ms_head (m, "CompanyRegister", g_b.reg);
	// "Atelier Lumen SRL  ·  Rue ... 12, 1000 Bruxelles  ·  TVA BE 0721.583.097  ·  RPM Bruxelles"
	scpy (a, g_b.name, sizeof a);
	if (g_b.legal[0])						// (its legal form, unless its name says it)
	{
		int n = slen (g_b.name), k = slen (g_b.legal);
		if (n < k || !ci_eq (g_b.name + n - k, g_b.legal)) { scat (a, " ", sizeof a); scat (a, g_b.legal, sizeof a); }
	}
	if (g_b.street[0] || g_b.city[0])
	{
		scat (a, "  \xB7  ", sizeof a); scat (a, g_b.street, sizeof a);
		if (g_b.street[0] && (g_b.zip[0] || g_b.city[0])) scat (a, ", ", sizeof a);
		scat (a, g_b.zip, sizeof a); if (g_b.zip[0]) scat (a, " ", sizeof a); scat (a, g_b.city, sizeof a);
	}
	if (g_b.vat[0]) { vat_show (g_b.vat, t, sizeof t); scat (a, "  \xB7  ", sizeof a); scat (a, tr (T_VAT, lang), sizeof a); scat (a, " ", sizeof a); scat (a, t, sizeof a); }
	if (g_b.reg[0]) { scat (a, "  \xB7  ", sizeof a); scat (a, g_b.reg, sizeof a); }
	ms_head (m, "CompanyLegalLine", a);
	// the party
	ms_head (m, "PartyName", p->name); ms_head (m, "PartyStreet", p->street); ms_head (m, "PartyZip", p->zip); ms_head (m, "PartyCity", p->city);
	ms_head (m, "PartyCountry", ci_eq (there, home) ? "" : country_name (there, lang));
	address_lines (p->street, p->zip, p->city, there, home, lang, a, sizeof a); ms_head (m, "PartyAddress", a);
	vat_show (p->vat, t, sizeof t); ms_head (m, "PartyVAT", t);
	{ char l[8]; scpy (l, tr (T_VAT, lang), sizeof l); scat (l, " ", sizeof l); ms_head_line (m, "PartyVATLine", l, t); }
	ms_head (m, "PartyEmail", p->email); ms_head (m, "PartyPhone", p->phone); ms_head (m, "PartyCode", p->code);
	char d[24]; itoa10 (p->terms, d); ms_head (m, "Terms", d);
	if (p->terms > 0) { scat (d, tr (T_DAYS, lang), sizeof d); ms_head (m, "TermsText", d); } else ms_head (m, "TermsText", tr (T_CASH, lang));
}
// The VAT by rate, a line each, in the document's language (the legal mentions of the reverse charges
// and exemptions included): "21 % : 262,50 sur 1.250,00".
static void vat_detail (const int *vats, const money *bases, int n, int lang, char *out, int cap)
{
	static const char *const M[5][3] = {
		{ "Livraison / service intracommunautaire, autoliquidation (art. 39bis / 21 \xA7 2 CTVA ; art. 196 Directive 2006/112/CE) : ",
		  "Intracommunautaire levering / dienst, btw verlegd (art. 39bis / 21 \xA7 2 WBTW; art. 196 Richtlijn 2006/112/EG): ",
		  "Intra-Community supply / service, reverse charge (art. 39bis / 21 \xA7 2 Belgian VAT Code; art. 196 Directive 2006/112/EC): " },
		{ "Autoliquidation, TVA due par le cocontractant (art. 20 AR n\xB0 1) : ",
		  "Verlegging van heffing, btw te voldoen door de medecontractant (art. 20 KB nr. 1): ",
		  "Reverse charge, VAT due by the co-contractor (art. 20 Royal Decree no. 1): " },
		{ "Exportation, exon\xE9r\xE9" "e (art. 39 CTVA) : ", "Uitvoer, vrijgesteld (art. 39 WBTW): ", "Export, exempt (art. 39 Belgian VAT Code): " },
		{ "Exon\xE9r\xE9" " de TVA (art. 44 CTVA) : ", "Vrijgesteld van btw (art. 44 WBTW): ", "Exempt from VAT (art. 44 Belgian VAT Code): " },
		{ "Sans TVA : ", "Zonder btw: ", "Without VAT: " } };
	int L = lang >= 0 && lang < 3 ? lang : 0;
	out[0] = '\0';
	for (int c = 0; c < NVAT; c++)
	{
		money base = 0; bool any = false;
		for (int i = 0; i < n; i++) if (vats[i] == c) { base += bases[i]; any = true; }
		if (!any) continue;
		char t[200], a[32], r[16];
		fmt_money (base, a);
		const char *code = VAT_DEFS[c].code;
		int k = seq (code, "VEUS") || seq (code, "VEUG") || seq (code, "VEUT") ? 0 : seq (code, "VCC") ? 1 : seq (code, "VEX") ? 2 : seq (code, "VX") ? 3
		      : !VAT_DEFS[c].rate || vat_reverse (c) ? 4 : -1;
		if (k >= 0) scpy (t, M[k][L], sizeof t);
		else
		{
			fmt_rate (VAT_DEFS[c].rate, r); scpy (t, r, sizeof t); scat (t, tr (T_COLON, L), sizeof t);
			char x[32]; fmt_money (tax_of (base, VAT_DEFS[c].rate), x); scat (t, x, sizeof t); scat (t, tr (T_ON, L), sizeof t);
		}
		scat (t, a, sizeof t);
		if (out[0]) scat (out, "\n", cap);
		scat (out, t, cap);
	}
	if (g_b.vatRegime == VR_FRANCHISE)
	{
		static const char *const F[3] = { "R\xE9gime particulier de franchise des petites entreprises.", "Bijzondere vrijstellingsregeling kleine ondernemingen.",
						  "Special exemption scheme for small businesses." };
		scpy (out, F[L], cap);
	}
}
// A file's name from a text (letters, digits, spaces, - kept).
static void file_safe (const char *s, char *out, int cap)
{
	int n = 0;
	for (const char *q = s; *q && n < cap - 1; q++)
	{
		unsigned char c = (unsigned char) *q;
		if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0xC0 || c == ' ' || c == '-') out[n++] = (char) c;
		else if (c == '/' || c == '.') out[n++] = '-';
	}
	out[n] = '\0';
}
static bool file_exists (const char *path) { void *f = kapi_open (path); if (f) { kapi_close (f); return true; } return false; }
// A kind's template in a language -> its path; own: only the language's own one (not the French
// default's). False: none.
static bool template_find (int kind, int lang, char *path, int cap, bool own = false)
{
	static const char *const EXT[3] = { ".docx", ".odt", ".rtf" };
	for (int pass = lang > 0 ? 0 : 1; pass < (own && lang > 0 ? 1 : 2); pass++)
		for (int i = 0; i < 3; i++)
		{
			scpy (path, TEMPLATES, cap); scat (path, "/", cap);
			if (pass == 0) { scat (path, LANG_KEY[lang], cap); scat (path, "/", cap); }
			scat (path, PK_FILE[kind], cap); scat (path, EXT[i], cap);
			if (file_exists (path)) return true;
		}
	return false;
}
// The template to print with: the language's, the default one, else one the user chooses.
static bool template_of (int kind, int lang, char *path, int cap)
{
	if (template_find (kind, lang, path, cap)) return true;
	char m[200]; scpy (m, TR ("No template for this kind of document ("), sizeof m); scat (m, TEMPLATES, sizeof m); scat (m, "/", sizeof m); scat (m, PK_FILE[kind], sizeof m);
	scat (m, TR (".rtf). Choose a Letters document to use?"), sizeof m);
	if (ask (TR ("Print"), m, MB_YESNO, 1) != 1) return false;
	return wk_file_open (path, (unsigned) cap, TEMPLATES);
}
// The data written, Letters asked to make the document.
static void ms_print (MergeSet &m, int kind, int lang, const char *fileName)
{
	char tpl[200];
	if (!template_of (kind, lang, tpl, sizeof tpl)) return;
	ms_head (m, "FileName", fileName);
	kapi_mkdir ("SD:/apps/ledger.app");
	Out h, l; cf::doc_write (m.h, h); cf::doc_write (m.l, l);
	bool ok = kapi_save_file (MERGE_HEAD, h.b ? h.b : "", (unsigned) h.n) >= 0 && kapi_save_file (MERGE_LINES, l.b ? l.b : "", (unsigned) l.n) >= 0;
	char folder[120] = "SD:/docs/"; scat (folder, PK_FOLDER[kind], sizeof folder);
	kapi_mkdir ("SD:/docs"); kapi_mkdir (folder);
	Out j;
	j.puts ("template = "); j.puts (tpl); j.puts ("\ndata = "); j.puts (MERGE_HEAD); j.puts ("\nlines = "); j.puts (MERGE_LINES);
	j.puts ("\nrecords = 1\noutput = files\nfolder = "); j.puts (folder); j.puts ("\nname = FileName\n");
	if (ok) ok = kapi_save_file (MERGE_JOB, j.b, (unsigned) j.n) >= 0;
	char args[240] = "--merge "; scat (args, MERGE_JOB, sizeof args);
	if (!ok || !kapi_exec ("SD:/apps/writer.app/main", args)) { warn (TR ("Print"), TR ("Letters could not be started.")); return; }
	char s[200]; scpy (s, TR ("Letters makes the document in "), sizeof s); scat (s, folder, sizeof s); status (s);
}
// A commercial document's title in a language: "Devis 2026/0003".
static void cdoc_title (const CDoc &d, int lang, char *out, int cap)
{
	char n[32]; cdoc_number (d, n, sizeof n);
	scpy (out, DOC_KIND[lang >= 0 && lang < 3 ? lang : 0][pk_of (d.kind)], cap); scat (out, " ", cap); scat (out, n, cap);
}

// ---- what is printed --------------------------------------------------------------------------------------------------------
static void print_cdoc (const CDoc &d)
{
	int lang = doc_lang (d.party), pk = pk_of (d.kind);
	static MergeSet m; ms_init (m);				// (static: two Cardfile forms, 80 KB)
	char n[32], t[64], x[120];
	cdoc_number (d, n, sizeof n);
	ms_head (m, "Kind", DOC_KIND[lang][pk]); ms_head (m, "Number", n);
	ms_head_date (m, "Date", d.date); ms_head_date (m, "Until", d.until); ms_head (m, "DueDate", "");
	char u[16] = ""; if (d.until) date_show (d.until, u);
	ms_head_line (m, "UntilLine", tr (d.kind == CD_QUOTE ? T_VALID : d.kind == CD_ORDER ? T_PLANNED : T_WANTED, lang), d.kind == CD_DELIVERY ? "" : u);
	ms_head (m, "Reference", d.ref); ms_head_line (m, "ReferenceLine", tr (cd_sale (d.kind) ? T_YOURREF : T_REF, lang), d.ref);
	ms_head (m, "Text", d.text); ms_head (m, "Communication", "");
	x[0] = '\0'; { int i = d.from ? cdoc_index (g_b, d.from) : -1; if (i >= 0) cdoc_title (g_b.cd[i], lang, x, sizeof x); }
	ms_head (m, "FromDocument", x); ms_head_line (m, "FromLine", tr (T_FROM, lang), x);
	ms_parties (m, d.party, lang);
	money net, tax, tot; cdoc_totals (d, &net, &tax, &tot);
	ms_head_money (m, "TotalNet", net); ms_head_money (m, "TotalVAT", tax); ms_head_money (m, "Total", tot);
	int vats[256]; money bases[256]; int nv = 0;
	ms_lines_fields (m);
	for (int i = 0; i < d.nl; i++)
	{
		const CLine &l = d.l[i];
		if (!l.text[0] && !l.price) continue;
		money lt = cline_total (l);
		ms_line (m, l.text, l.qty, l.price, l.vat, lt);
		if (nv < 256) { vats[nv] = l.vat; bases[nv] = lt; nv++; }
	}
	char vd[800]; vat_detail (vats, bases, nv, lang, vd, sizeof vd); ms_head (m, "VATDetail", d.kind == CD_DELIVERY ? "" : vd);
	char fn[120]; scpy (fn, CD_NAME[d.kind], sizeof fn); scat (fn, " ", sizeof fn); scat (fn, n, sizeof fn);
	scat (fn, " ", sizeof fn); scat (fn, party_name (g_b, d.party), sizeof fn);
	file_safe (fn, t, sizeof t);
	ms_print (m, pk, lang, t);
	ms_free (m);
}
static void print_invoice (const Entry &e)
{
	Invoice v; inv_init (v);
	if (!inv_from_entry (g_b, e, v)) { warn (TR ("Print"), TR ("This entry is not an invoice Ledger made.")); return; }
	int lang = doc_lang (e.party);
	static MergeSet m; ms_init (m);
	char n[32], t[64];
	entry_number (g_b, e, n, sizeof n);
	bool cn = v.credit;
	ms_head (m, "Kind", DOC_KIND[lang][cn ? PK_CREDIT : PK_INVOICE]); ms_head (m, "Number", n);
	ms_head_date (m, "Date", e.date); ms_head_date (m, "Until", e.due); ms_head_date (m, "DueDate", e.due);
	char u[16] = ""; if (e.due) date_show (e.due, u);
	ms_head_line (m, "UntilLine", tr (T_DUE, lang), cn ? "" : u);
	ms_head (m, "Reference", e.ref); ms_head_line (m, "ReferenceLine", tr (T_YOURREF, lang), e.ref);
	ms_head (m, "Text", e.text);
	char c[24] = ""; if (e.comm[0]) ogm_show (e.comm, c); ms_head (m, "Communication", c);
	char x[120] = ""; for (int i = 0; i < g_b.ncd; i++) if (g_b.cd[i].invoice == e.id) { cdoc_title (g_b.cd[i], lang, x, sizeof x); break; }
	ms_head (m, "FromDocument", x); ms_head_line (m, "FromLine", tr (T_FROM, lang), x);
	ms_parties (m, e.party, lang);
	money net, tax, tot; inv_totals (v, &net, &tax, &tot);
	ms_head_money (m, "TotalNet", net); ms_head_money (m, "TotalVAT", tax); ms_head_money (m, "Total", tot);
	int vats[256]; money bases[256]; int nv = 0;
	ms_lines_fields (m);
	for (int i = 0; i < v.nl; i++)
	{
		const InvLine &l = v.l[i];
		ms_line (m, l.text[0] ? l.text : v.text, 1000, l.net, l.vat, l.net);
		if (nv < 256) { vats[nv] = l.vat; bases[nv] = l.net; nv++; }
	}
	char vd[800]; vat_detail (vats, bases, nv, lang, vd, sizeof vd); ms_head (m, "VATDetail", vd);
	char fn[120]; scpy (fn, cn ? "Credit note " : "Invoice ", sizeof fn); scat (fn, n, sizeof fn); scat (fn, " ", sizeof fn); scat (fn, party_name (g_b, e.party), sizeof fn);
	file_safe (fn, t, sizeof t);
	ms_print (m, cn ? PK_CREDIT : PK_INVOICE, lang, t);
	ms_free (m);
	inv_free (v);
}

} // namespace lg

#endif
