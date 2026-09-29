//
// sepa.h -- suppliers paid through the bank: their open invoices chosen, a SEPA credit transfer file (ISO
// 20022 pain.001.001.09, as Febelfin's guidelines have it for Belgian banks) uploaded to the bank's site.
// The company pays from an account (a bank journal's IBAN, else the company's); a transfer an invoice:
// its supplier's name and address (the hybrid form: postcode, town, country, the street in a line), IBAN
// (and BIC when known), the amount still due, the supplier's structured communication when the invoice
// has one (+++...+++: "SCOR", issued by "BBA"), else its number as free text; all on the day asked. The
// texts in the EPC's Latin set (accents taken off; what it lacks: a space). Its invoices are then flagged
// (EF_PAYING): not offered again until the bank's statement pays them.
//
#ifndef _ledger_sepa_h
#define _ledger_sepa_h

#include "post.h"
#include "reports.h"

namespace lg {

struct Pay { int entry, line; money amount; };		// a supplier's open line (its entry's index, the line's), the amount to pay

// The suppliers' lines to pay: open, credit (what is owed), on a suppliers' account -> how many, by due date.
static int sepa_open (const Book &b, Pay *p, int cap)
{
	int n = 0;
	for (int i = 0; i < b.ne && n < cap; i++)
	{
		const Entry &e = b.e[i];
		for (int k = 0; k < e.nl && n < cap; k++)
		{
			const Line &x = e.l[k];
			const Party *pt = x.party ? party_of (b, x.party) : 0;
			if (!pt || pt->kind != PK_SUPPLIER || x.match || x.amount >= 0 || !acc_party (b, x.account)) continue;
			p[n].entry = i; p[n].line = k; p[n].amount = -x.amount; n++;
		}
	}
	for (int a = 1; a < n; a++)					// (by due date, then document)
		for (int c = a; c > 0; c--)
		{
			const Line &x = b.e[p[c].entry].l[p[c].line], &y = b.e[p[c - 1].entry].l[p[c - 1].line];
			int dx = x.due ? x.due : b.e[p[c].entry].date, dy = y.due ? y.due : b.e[p[c - 1].entry].date;
			if (dx < dy || (dx == dy && p[c].entry < p[c - 1].entry)) { Pay t = p[c]; p[c] = p[c - 1]; p[c - 1] = t; } else break;
		}
	return n;
}
// Why that payment cannot be made ("": it can).
static const char *sepa_check (const Book &b, const Pay &p)
{
	const Line &x = b.e[p.entry].l[p.line];
	const Party *pt = party_of (b, x.party);
	if (!pt) return "Its supplier is unknown.";
	if (!pt->iban[0]) return "Its supplier has no IBAN (its card).";
	char ib[40]; iban_normalize (pt->iban, ib, sizeof ib);
	if (!iban_ok (ib)) return "Its supplier's IBAN is not a valid one (its card).";
	if (p.amount <= 0) return "Nothing to pay.";
	return "";
}
// A text in the EPC's Latin set (a-z A-Z 0-9 / - ? : ( ) . , ' + space), max characters.
static void sepa_text (const char *in, char *out, int max)
{
	int n = 0; bool sp = true;
	for (const unsigned char *p = (const unsigned char *) in; *p && n < max; p++)
	{
		unsigned c = *p;
		if (c >= 0xC0 && c <= 0xC5) c = 'A'; else if (c >= 0xE0 && c <= 0xE5) c = 'a';
		else if (c == 0xC7) c = 'C'; else if (c == 0xE7) c = 'c';
		else if (c >= 0xC8 && c <= 0xCB) c = 'E'; else if (c >= 0xE8 && c <= 0xEB) c = 'e';
		else if (c >= 0xCC && c <= 0xCF) c = 'I'; else if (c >= 0xEC && c <= 0xEF) c = 'i';
		else if (c >= 0xD2 && c <= 0xD6) c = 'O'; else if (c >= 0xF2 && c <= 0xF6) c = 'o';
		else if (c >= 0xD9 && c <= 0xDC) c = 'U'; else if (c >= 0xF9 && c <= 0xFC) c = 'u';
		else if (c == 0xD1) c = 'N'; else if (c == 0xF1) c = 'n'; else if (c == 0xDF) c = 's';
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '?' || c == ':' || c == '('
			  || c == ')' || c == '.' || c == ',' || c == '\'' || c == '+';
		if (!ok) { if (!sp) { out[n++] = ' '; sp = true; } continue; }
		out[n++] = (char) c; sp = false;
	}
	while (n > 0 && out[n - 1] == ' ') n--;
	out[n] = '\0';
}
static void xml_put (Out &o, const char *s)				// (after sepa_text: only the apostrophe to escape)
{
	for (const char *p = s; *p; p++) if (*p == '\'') o.puts ("&apos;"); else o.put (*p);
}
static void x_el (Out &o, const char *tag, const char *v) { o.puts ("<"); o.puts (tag); o.puts (">"); xml_put (o, v); o.puts ("</"); o.puts (tag); o.puts (">"); }
static void x_amount (money v, char *out) { long long a = v < 0 ? -v : v; char t[24]; itoa10 ((int) (a / 100), t); scpy (out, t, 32); scat (out, ".", 32); char d[3] = { (char) ('0' + a % 100 / 10), (char) ('0' + a % 10), 0 }; scat (out, d, 32); }
static void x_date (int ymd, char *out) { date_iso (ymd, out); }
// A party's (or the company's) postal address, hybrid: its postcode, town, country, its street as a line.
static void x_address (Out &o, const char *street, const char *zip, const char *city, const char *country)
{
	char t[80];
	o.puts ("<PstlAdr>");
	if (zip[0]) { sepa_text (zip, t, 16); if (t[0]) x_el (o, "PstCd", t); }
	sepa_text (city[0] ? city : "-", t, 35); x_el (o, "TwnNm", t[0] ? t : "-");
	char cc[4]; scpy (cc, country[0] ? country : "BE", sizeof cc); for (char *q = cc; *q; q++) *q = up (*q); x_el (o, "Ctry", cc);
	if (street[0]) { sepa_text (street, t, 70); if (t[0]) x_el (o, "AdrLine", t); }
	o.puts ("</PstlAdr>");
}
// An enterprise number (KBO / BCE: the VAT number's ten digits) as an organisation's identification.
static void x_orgid (Out &o, const char *vat)
{
	if (!(vat[0] == 'B' && vat[1] == 'E' && slen (vat) == 12)) return;
	o.puts ("<Id><OrgId><Othr>"); x_el (o, "Id", vat + 2); x_el (o, "Issr", "KBO-BCE"); o.puts ("</Othr></OrgId></Id>");
}
// The payments (checked: sepa_check) as pain.001.001.09 -> false: nothing to write. msgId: the file's
// own (35 characters at most); created: when (yyyymmdd, hhmmss); exec: the day the bank pays.
static bool sepa_write (const Book &b, const Pay *p, int n, int exec, int created, int hms, const char *iban, const char *bic, const char *msgId, Out &o)
{
	if (n <= 0) return false;
	money sum = 0; for (int i = 0; i < n; i++) sum += p[i].amount;
	char t[160], a[32], d[16];
	o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	o.puts ("<Document xmlns=\"urn:iso:std:iso:20022:tech:xsd:pain.001.001.09\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n<CstmrCdtTrfInitn>\n");
	// the group's header
	o.puts ("<GrpHdr>"); x_el (o, "MsgId", msgId);
	x_date (created, d); scpy (t, d, sizeof t); scat (t, "T", sizeof t);
	{ char h[12]; h[0] = (char) ('0' + hms / 100000); h[1] = (char) ('0' + hms / 10000 % 10); h[2] = ':'; h[3] = (char) ('0' + hms / 1000 % 10); h[4] = (char) ('0' + hms / 100 % 10);
	  h[5] = ':'; h[6] = (char) ('0' + hms / 10 % 10); h[7] = (char) ('0' + hms % 10); h[8] = '\0'; scat (t, h, sizeof t); }
	o.puts ("<CreDtTm>"); o.puts (t); o.puts ("</CreDtTm>");
	itoa10 (n, t); x_el (o, "NbOfTxs", t); x_amount (sum, a); x_el (o, "CtrlSum", a);
	char nm[80]; sepa_text (b.name, nm, 70);
	o.puts ("<InitgPty>"); x_el (o, "Nm", nm); x_orgid (o, b.vat); o.puts ("</InitgPty></GrpHdr>\n");
	// the payment's information: the company, its account; the transfers
	o.puts ("<PmtInf>"); scpy (t, msgId, 32); scat (t, "-1", sizeof t); x_el (o, "PmtInfId", t);
	x_el (o, "PmtMtd", "TRF"); x_el (o, "BtchBookg", "true"); itoa10 (n, t); x_el (o, "NbOfTxs", t); x_el (o, "CtrlSum", a);
	o.puts ("<PmtTpInf><SvcLvl><Cd>SEPA</Cd></SvcLvl></PmtTpInf>");
	x_date (exec, d); o.puts ("<ReqdExctnDt><Dt>"); o.puts (d); o.puts ("</Dt></ReqdExctnDt>");
	o.puts ("<Dbtr>"); x_el (o, "Nm", nm); x_address (o, b.street, b.zip, b.city, b.country); x_orgid (o, b.vat); o.puts ("</Dbtr>");
	char ib[40]; iban_normalize (iban, ib, sizeof ib);
	o.puts ("<DbtrAcct><Id>"); x_el (o, "IBAN", ib); o.puts ("</Id><Ccy>EUR</Ccy></DbtrAcct>");
	char bc[16]; iban_normalize (bic, bc, sizeof bc);
	if (bic_ok (bc)) { o.puts ("<DbtrAgt><FinInstnId>"); x_el (o, "BICFI", bc); o.puts ("</FinInstnId></DbtrAgt>"); }
	else o.puts ("<DbtrAgt><FinInstnId><Othr><Id>NOTPROVIDED</Id></Othr></FinInstnId></DbtrAgt>");
	x_el (o, "ChrgBr", "SLEV");
	o.puts ("\n");
	for (int i = 0; i < n; i++)
	{
		const Entry &e = b.e[p[i].entry]; const Line &x = e.l[p[i].line];
		const Party *pt = party_of (b, x.party);
		char ref[40], id[40];
		entry_ref (b, e, ref, sizeof ref); sepa_text (ref, id, 35);
		for (char *q = id; *q; q++) if (*q == ' ' || *q == '/') *q = '-';
		o.puts ("<CdtTrfTxInf><PmtId>"); x_el (o, "InstrId", id); x_el (o, "EndToEndId", id); o.puts ("</PmtId>");
		x_amount (p[i].amount, a); o.puts ("<Amt><InstdAmt Ccy=\"EUR\">"); o.puts (a); o.puts ("</InstdAmt></Amt>");
		char pb[16]; iban_normalize (pt->bic, pb, sizeof pb);
		if (bic_ok (pb)) { o.puts ("<CdtrAgt><FinInstnId>"); x_el (o, "BICFI", pb); o.puts ("</FinInstnId></CdtrAgt>"); }
		sepa_text (pt->name, t, 70);
		o.puts ("<Cdtr>"); x_el (o, "Nm", t[0] ? t : "-"); x_address (o, pt->street, pt->zip, pt->city, pt->country); o.puts ("</Cdtr>");
		char pi[40]; iban_normalize (pt->iban, pi, sizeof pi);
		o.puts ("<CdtrAcct><Id>"); x_el (o, "IBAN", pi); o.puts ("</Id></CdtrAcct>");
		o.puts ("<RmtInf>");
		char d12[16];
		if (e.comm[0] && ogm_parse (e.comm, d12))
		{
			o.puts ("<Strd><CdtrRefInf><Tp><CdOrPrtry><Cd>SCOR</Cd></CdOrPrtry><Issr>BBA</Issr></Tp>"); x_el (o, "Ref", d12); o.puts ("</CdtrRefInf></Strd>");
		}
		else
		{
			char u[160];
			if (e.ref[0]) { scpy (u, e.ref, sizeof u); if (e.text[0]) { scat (u, " ", sizeof u); scat (u, e.text, sizeof u); } }
			else scpy (u, e.text[0] ? e.text : ref, sizeof u);
			sepa_text (u, t, 140);
			x_el (o, "Ustrd", t[0] ? t : id);
		}
		o.puts ("</RmtInf></CdtTrfTxInf>\n");
	}
	o.puts ("</PmtInf>\n</CstmrCdtTrfInitn>\n</Document>\n");
	return true;
}

} // namespace lg

#endif
