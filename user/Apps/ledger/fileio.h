//
// fileio.h -- a company's books in a file (.ledger): text in Latin-1 (as Onyx writes), easy to read and to
// mend by hand, written whole at each change (a copy of the one before kept as .bak):
//
//   # Onyx Ledger -- a company's books (Belgian PCMN); open it with Ledger
//   [company]                             key = value: name, legal, street, zip, city, country, vat,
//   version = 1                           email, phone, iban, bic, register, web, vatregime (normal / franchise /
//   name = Atelier Lumen SRL              none), vatperiod (quarter / month), chart (fr / nl), and the
//   ...                                   accounts by role: customers, suppliers, vatdue, vatdeduct,
//                                         profit, loss, suspense
//   [years]      start  end  open|closed          (a line a fiscal year)
//   [journals]   code  type  name  account  iban  hidden        (type: sales, purchases, bank, cash, misc)
//   [accounts]   code  name  nature  hidden                     (nature: G, S, I or empty)
//   [parties]    id  kind  code  name  street  zip  city  country  vat  regime  email  phone  iban  bic
//                terms  account  defaccount  defvat  hidden  notes  lang   (kind: C / S; regime: be,
//                private, eu, world, cocontractor; lang: its documents' -- fr, nl, en)
//   [entries]    E  id  journal  no  date  due  party  flags  ref  comm  old  new  text   (flags: C a credit
//                note, O an opening entry, S a VAT return's settlement)
//                L  account  amount  party  vat  role  aux  due  match  text               (its lines after
//                it; role: B base, T tax, D due, N not deductible)
//   [returns]    year  period  M|Q  filed  grid=amount ...                                  (the VAT returns filed)
//   [documents]  D  id  kind  no  date  until  party  state  invoice  from  ref  text     (kind: quote, order,
//                I  text  qty  price  vat  account                                        delivery, porder;
//                                                                                          its lines after it)
//
// A tab between the cells; in a cell \t, \n, \\ for a tab, a line break, a backslash. Dates ISO
// (2026-09-29), amounts "1234.56". Read leniently: a missing cell is empty, unknown keys and sections
// are skipped, the lines' order within a section kept.
//
#ifndef _ledger_fileio_h
#define _ledger_fileio_h

#include "commerce.h"

namespace lg {

static const char *const REGIME_KEY[PR_COUNT] = { "be", "private", "eu", "world", "cocontractor" };
static const char ROLE_KEY[] = { 0, 'B', 'T', 'D', 'N' };

// ---- writing ---------------------------------------------------------------------------------------------------------
static void put_c (Out &o, const char *v) { o.put ('\t'); put_cell (o, v, false); }
static void put_n (Out &o, long long v) { char t[24]; itoa10 (v, t); put_c (o, t); }
static void put_m (Out &o, money v) { char t[32]; fmt_plain (v, t); put_c (o, t); }
static void put_d (Out &o, int v) { char t[16]; date_iso (v, t); put_c (o, t); }

static void book_write (const Book &b, Out &o)
{
	o.puts ("# Onyx Ledger -- a company's books (Belgian PCMN); open it with Ledger\n[company]\n");
	put_kv (o, "version", "1");
	put_kv (o, "name", b.name); put_kv (o, "legal", b.legal); put_kv (o, "street", b.street); put_kv (o, "zip", b.zip);
	put_kv (o, "city", b.city); put_kv (o, "country", b.country); put_kv (o, "vat", b.vat); put_kv (o, "email", b.email);
	put_kv (o, "phone", b.phone); put_kv (o, "iban", b.iban); put_kv (o, "bic", b.bic);
	if (b.reg[0]) put_kv (o, "register", b.reg);
	if (b.web[0]) put_kv (o, "web", b.web);
	put_kv (o, "vatregime", b.vatRegime == VR_FRANCHISE ? "franchise" : b.vatRegime == VR_NONE ? "none" : "normal");
	put_kv (o, "vatperiod", b.vatPeriod == VP_MONTH ? "month" : "quarter");
	put_kv (o, "chart", b.chart);
	put_kv (o, "customers", b.accCustomers); put_kv (o, "suppliers", b.accSuppliers); put_kv (o, "vatdue", b.accVatDue);
	put_kv (o, "vatdeduct", b.accVatDeduct); put_kv (o, "profit", b.accProfit); put_kv (o, "loss", b.accLoss);
	put_kv (o, "suspense", b.accSuspense);
	o.puts ("\n[years]\n");
	for (int i = 0; i < b.nyr; i++)
	{
		char t[16]; date_iso (b.yr[i].start, t); o.puts (t);
		put_d (o, b.yr[i].end); put_c (o, b.yr[i].closed ? "closed" : "open"); o.put ('\n');
	}
	o.puts ("\n[journals]\n");
	for (int i = 0; i < b.njr; i++)
	{
		const Journal &j = b.jr[i];
		put_cell (o, j.code, true); put_c (o, JT_KEY[j.type]); put_c (o, j.name); put_c (o, j.account); put_c (o, j.iban);
		put_c (o, j.hidden ? "hidden" : ""); o.put ('\n');
	}
	o.puts ("\n[accounts]\n");
	for (int i = 0; i < b.nacc; i++)
	{
		const Account &a = b.acc[i];
		put_cell (o, a.code, true); put_c (o, a.name);
		char n[2] = { a.nature, 0 }; put_c (o, n); put_c (o, a.hidden ? "hidden" : ""); o.put ('\n');
	}
	o.puts ("\n[parties]\n");
	for (int i = 0; i < b.npty; i++)
	{
		const Party &p = b.pty[i];
		char t[16]; itoa10 (p.id, t); o.puts (t);
		put_c (o, p.kind == PK_CUSTOMER ? "C" : "S"); put_c (o, p.code); put_c (o, p.name); put_c (o, p.street); put_c (o, p.zip);
		put_c (o, p.city); put_c (o, p.country); put_c (o, p.vat); put_c (o, REGIME_KEY[p.regime < PR_COUNT ? p.regime : 0]);
		put_c (o, p.email); put_c (o, p.phone); put_c (o, p.iban); put_c (o, p.bic); put_n (o, p.terms);
		put_c (o, p.account); put_c (o, p.defAcc); put_c (o, p.defVat); put_c (o, p.hidden ? "hidden" : ""); put_c (o, p.notes);
		put_c (o, p.lang);
		o.put ('\n');
	}
	o.puts ("\n[entries]\n");
	for (int i = 0; i < b.ne; i++)
	{
		const Entry &e = b.e[i];
		o.puts ("E"); put_n (o, e.id); put_c (o, e.journal >= 0 && e.journal < b.njr ? b.jr[e.journal].code : "?"); put_n (o, e.no);
		put_d (o, e.date); put_d (o, e.due); put_n (o, e.party);
		char f[4]; int k = 0; if (e.flags & EF_CREDIT) f[k++] = 'C'; if (e.flags & EF_OPENING) f[k++] = 'O'; if (e.flags & EF_SETTLE) f[k++] = 'S'; f[k] = '\0';
		put_c (o, f); put_c (o, e.ref); put_c (o, e.comm); put_m (o, e.stmtOld); put_m (o, e.stmtNew); put_c (o, e.text);
		o.put ('\n');
		for (int j = 0; j < e.nl; j++)
		{
			const Line &l = e.l[j];
			o.puts ("L"); put_c (o, l.account); put_m (o, l.amount); put_n (o, l.party); put_c (o, vat_code (l.vat));
			char r[2] = { ROLE_KEY[l.role < 5 ? l.role : 0], 0 }; put_c (o, r); put_m (o, l.aux);
			put_d (o, l.due); put_n (o, l.match); put_c (o, l.text);
			o.put ('\n');
		}
	}
	o.puts ("\n[returns]\n");
	for (int i = 0; i < b.nret; i++)
	{
		const VatReturn &r = b.ret[i];
		char t[16]; itoa10 (r.year, t); o.puts (t); put_n (o, r.period); put_c (o, r.monthly ? "M" : "Q"); put_d (o, r.filed);
		o.put ('\t');
		bool first = true;
		for (int g = 0; g < 100; g++) if (r.grid[g])
		{
			if (!first) o.put (' ');
			first = false;
			char gl[4]; grid_label (g, gl); o.puts (gl); o.put ('=');
			char m[32]; fmt_plain (r.grid[g], m); o.puts (m);
		}
		o.put ('\n');
	}
	o.puts ("\n[documents]\n");
	for (int i = 0; i < b.ncd; i++)
	{
		const CDoc &d = b.cd[i];
		o.puts ("D"); put_n (o, d.id); put_c (o, CD_KEY[d.kind < CD_COUNT ? d.kind : 0]); put_n (o, d.no); put_d (o, d.date); put_d (o, d.until);
		put_n (o, d.party); put_c (o, CS_KEY[d.status < CS_COUNT ? d.status : 0]); put_n (o, d.invoice); put_n (o, d.from); put_c (o, d.ref); put_c (o, d.text);
		o.put ('\n');
		for (int k = 0; k < d.nl; k++)
		{
			const CLine &l = d.l[k];
			char q[32]; fmt_num (l.qty, 3, q);
			o.puts ("I"); put_c (o, l.text); put_c (o, q); put_m (o, l.price); put_c (o, vat_code (l.vat)); put_c (o, l.account);
			o.put ('\n');
		}
	}
}

// ---- reading ----------------------------------------------------------------------------------------------------------
// A line's cells (up to cap) -> how many.
static int split_cells (const char *p, const char *end, char cells[][256], int cap)
{
	int n = 0;
	while (p && n < cap) { p = read_cell (p, end, cells[n], 256); n++; }
	for (int i = n; i < cap; i++) cells[i][0] = '\0';
	return n;
}
static money cell_money (const char *s) { money v = 0; if (s[0]) parse_num (s, 2, &v, 0); return v; }
static int cell_int (const char *s) { long long v = 0; bool neg = *s == '-'; if (neg) s++; while (digit (*s)) v = v * 10 + (*s++ - '0'); return (int) (neg ? -v : v); }
static int cell_date (const char *s) { int y, m, d; return read_iso (s, &y, &m, &d) ? ymd (y, m, d) : 0; }

// The books from a file's text; false (and why) when it is not one.
static bool book_read (Book &b, const char *text, int len, const char **why)
{
	book_clear (b);
	enum { S_NONE, S_COMPANY, S_YEARS, S_JOURNALS, S_ACCOUNTS, S_PARTIES, S_ENTRIES, S_RETURNS, S_DOCS, S_OTHER };
	int sec = S_NONE; bool company = false;
	static char c[24][256];
	Entry cur; entry_init (cur); bool inEntry = false;
	CDoc cdc; cdoc_init (cdc); bool inDoc = false;
	const char *p = text, *end = text + len;
	int maxMatch = 0;
	while (p < end)
	{
		const char *e = p; while (e < end && *e != '\n') e++;
		const char *le = e; if (le > p && le[-1] == '\r') le--;
		const char *s = p; p = e < end ? e + 1 : e;
		if (s == le) continue;
		if (*s == '[')
		{
			if (inEntry) { entry_insert (b, cur); inEntry = false; }
			if (inDoc) { cdoc_save (b, cdc); inDoc = false; }
			char name[24]; int n = 0; for (const char *q = s + 1; q < le && *q != ']' && n < 23; q++) name[n++] = *q; name[n] = '\0';
			sec = ci_eq (name, "company") ? S_COMPANY : ci_eq (name, "years") ? S_YEARS : ci_eq (name, "journals") ? S_JOURNALS
			    : ci_eq (name, "accounts") ? S_ACCOUNTS : ci_eq (name, "parties") ? S_PARTIES : ci_eq (name, "entries") ? S_ENTRIES
			    : ci_eq (name, "returns") ? S_RETURNS : ci_eq (name, "documents") ? S_DOCS : S_OTHER;
			if (sec == S_COMPANY) company = true;
			continue;
		}
		if (sec == S_NONE || sec == S_OTHER) continue;
		if ((*s == '#' || *s == ';') && sec == S_COMPANY) continue;
		if (sec == S_COMPANY)
		{
			const char *eq = s; while (eq < le && *eq != '=') eq++;
			if (eq >= le) continue;
			char k[24], v[256]; int kn = 0;
			for (const char *q = s; q < eq && kn < 23; q++) if (*q != ' ' && *q != '\t') k[kn++] = *q;
			k[kn] = '\0';
			const char *vs = eq + 1; while (vs < le && (*vs == ' ' || *vs == '\t')) vs++;
			int vn = 0; for (const char *q = vs; q < le && vn < 255; q++) v[vn++] = *q;
			while (vn > 0 && (v[vn - 1] == ' ' || v[vn - 1] == '\t')) vn--;
			v[vn] = '\0';
			struct { const char *k; char *d; int cap; } F[] = {
				{ "name", b.name, NAME_MAX }, { "legal", b.legal, 24 }, { "street", b.street, NAME_MAX }, { "zip", b.zip, 12 },
				{ "city", b.city, 48 }, { "country", b.country, 4 }, { "vat", b.vat, 20 }, { "email", b.email, 72 },
				{ "phone", b.phone, 24 }, { "iban", b.iban, 36 }, { "bic", b.bic, 12 }, { "chart", b.chart, 4 },
				{ "register", b.reg, 48 }, { "web", b.web, 64 },
				{ "customers", b.accCustomers, CODE_MAX }, { "suppliers", b.accSuppliers, CODE_MAX }, { "vatdue", b.accVatDue, CODE_MAX },
				{ "vatdeduct", b.accVatDeduct, CODE_MAX }, { "profit", b.accProfit, CODE_MAX }, { "loss", b.accLoss, CODE_MAX },
				{ "suspense", b.accSuspense, CODE_MAX } };
			bool done = false;
			for (unsigned i = 0; i < sizeof F / sizeof F[0]; i++) if (ci_eq (k, F[i].k)) { scpy (F[i].d, v, F[i].cap); done = true; }
			if (done) continue;
			if (ci_eq (k, "vatregime")) b.vatRegime = ci_eq (v, "franchise") ? VR_FRANCHISE : ci_eq (v, "none") ? VR_NONE : VR_NORMAL;
			else if (ci_eq (k, "vatperiod")) b.vatPeriod = ci_eq (v, "month") ? VP_MONTH : VP_QUARTER;
			continue;
		}
		int n = split_cells (s, le, c, 24);
		if (sec == S_YEARS)
		{
			if (b.nyr >= MAXYEARS) continue;
			Year &y = b.yr[b.nyr];
			y.start = cell_date (c[0]); y.end = cell_date (c[1]); y.closed = ci_eq (c[2], "closed");
			if (y.start && y.end && y.end >= y.start) b.nyr++;
		}
		else if (sec == S_JOURNALS)
		{
			if (b.njr >= MAXJOURNALS || !c[0][0]) continue;
			int t = JT_MISC; for (int i = 0; i < JT_COUNT; i++) if (ci_eq (c[1], JT_KEY[i])) t = i;
			Journal &j = jrn_add (b, c[0], c[2], t, c[3], c[4]);
			j.hidden = ci_eq (c[5], "hidden");
		}
		else if (sec == S_ACCOUNTS)
		{
			if (!c[0][0]) continue;
			int i = acc_put (b, c[0], c[1], c[2][0] == 'G' || c[2][0] == 'S' || c[2][0] == 'I' ? c[2][0] : (char) NAT_AUTO);
			b.acc[i].hidden = ci_eq (c[3], "hidden");
		}
		else if (sec == S_PARTIES)
		{
			int id = cell_int (c[0]);
			if (id <= 0 || party_index (b, id) >= 0) continue;
			Party &pt = party_add (b);
			pt.id = id; if (id >= b.nextParty) b.nextParty = id + 1;
			pt.kind = c[1][0] == 'S' || c[1][0] == 's' ? PK_SUPPLIER : PK_CUSTOMER;
			scpy (pt.code, c[2], PCODE_MAX); scpy (pt.name, c[3], NAME_MAX); scpy (pt.street, c[4], NAME_MAX); scpy (pt.zip, c[5], 12);
			scpy (pt.city, c[6], 48); scpy (pt.country, c[7][0] ? c[7] : "BE", 4); scpy (pt.vat, c[8], 20);
			pt.regime = (unsigned char) regime_of (pt.vat, pt.country);
			for (int i = 0; i < PR_COUNT; i++) if (ci_eq (c[9], REGIME_KEY[i])) pt.regime = (unsigned char) i;
			scpy (pt.email, c[10], 72); scpy (pt.phone, c[11], 24); scpy (pt.iban, c[12], 36); scpy (pt.bic, c[13], 12);
			pt.terms = (short) (c[14][0] ? cell_int (c[14]) : 30);
			scpy (pt.account, c[15], CODE_MAX); scpy (pt.defAcc, c[16], CODE_MAX); scpy (pt.defVat, c[17], 8);
			pt.hidden = ci_eq (c[18], "hidden"); pt.notes = sdup (c[19]);
			scpy (pt.lang, ci_eq (c[20], "fr") || ci_eq (c[20], "nl") || ci_eq (c[20], "en") ? c[20] : "", sizeof pt.lang);
			// (kept sorted by id)
			for (int k = b.npty - 1; k > 0 && b.pty[k].id < b.pty[k - 1].id; k--) { Party t = b.pty[k]; b.pty[k] = b.pty[k - 1]; b.pty[k - 1] = t; }
		}
		else if (sec == S_ENTRIES)
		{
			if (c[0][0] == 'E')
			{
				if (inEntry) entry_insert (b, cur);
				entry_init (cur); inEntry = true;
				cur.id = cell_int (c[1]);
				cur.journal = jrn_find (b, c[2]);
				if (cur.journal < 0) { jrn_add (b, c[2][0] ? c[2] : "?", c[2], JT_MISC); cur.journal = b.njr - 1; }
				cur.no = cell_int (c[3]); cur.date = cell_date (c[4]); cur.due = cell_date (c[5]); cur.party = cell_int (c[6]);
				for (const char *f = c[7]; *f; f++) { if (*f == 'C') cur.flags |= EF_CREDIT; if (*f == 'O') cur.flags |= EF_OPENING; if (*f == 'S') cur.flags |= EF_SETTLE; }
				scpy (cur.ref, c[8], sizeof cur.ref); scpy (cur.comm, c[9], sizeof cur.comm);
				cur.stmtOld = cell_money (c[10]); cur.stmtNew = cell_money (c[11]); sset (cur.text, c[12]);
				if (cur.id <= 0 || entry_index (b, cur.id) >= 0) cur.id = 0;	// (no id, or twice: a new one)
			}
			else if (c[0][0] == 'L' && inEntry)
			{
				Line &l = entry_add_line (cur);
				scpy (l.account, c[1], CODE_MAX); l.amount = cell_money (c[2]); l.party = cell_int (c[3]);
				l.vat = (signed char) (c[4][0] ? vat_find (c[4]) : -1);
				for (int r = 1; r < 5; r++) if (c[5][0] == ROLE_KEY[r]) l.role = (unsigned char) r;
				if (l.vat < 0) l.role = LR_NONE;
				l.aux = cell_money (c[6]); l.due = cell_date (c[7]); l.match = cell_int (c[8]);
				if (l.match > maxMatch) maxMatch = l.match;
				sset (l.text, c[9]);
			}
			(void) n;
		}
		else if (sec == S_DOCS)
		{
			if (c[0][0] == 'D')
			{
				if (inDoc) cdoc_save (b, cdc);
				cdoc_init (cdc); inDoc = true;
				cdc.id = cell_int (c[1]);
				for (int k = 0; k < CD_COUNT; k++) if (ci_eq (c[2], CD_KEY[k])) cdc.kind = k;
				cdc.no = cell_int (c[3]); cdc.date = cell_date (c[4]); cdc.until = cell_date (c[5]); cdc.party = cell_int (c[6]);
				for (int k = 0; k < CS_COUNT; k++) if (ci_eq (c[7], CS_KEY[k])) cdc.status = k;
				cdc.invoice = cell_int (c[8]); cdc.from = cell_int (c[9]); scpy (cdc.ref, c[10], sizeof cdc.ref); sset (cdc.text, c[11]);
				if (cdc.id <= 0 || cdoc_index (b, cdc.id) >= 0 || !cdc.date) { cdoc_free (cdc); inDoc = false; }
			}
			else if (c[0][0] == 'I' && inDoc)
			{
				CLine &l = cdoc_add_line (cdc);
				sset (l.text, c[1]);
				long long q = 0; if (c[2][0]) parse_num (c[2], 3, &q, 0); l.qty = q;
				l.price = cell_money (c[3]); l.vat = (signed char) (c[4][0] ? vat_find (c[4]) : -1); scpy (l.account, c[5], CODE_MAX);
			}
		}
		else if (sec == S_RETURNS)
		{
			VatReturn &r = return_add (b);
			r.year = cell_int (c[0]); r.period = cell_int (c[1]); r.monthly = c[2][0] == 'M'; r.filed = cell_date (c[3]);
			for (const char *q = c[4]; *q; )
			{
				while (*q == ' ') q++;
				int g = 0; while (digit (*q)) g = g * 10 + (*q++ - '0');
				if (*q != '=') break;
				q++;
				char m[32]; int k = 0; while (*q && *q != ' ' && k < 31) m[k++] = *q++;
				m[k] = '\0';
				if (g >= 0 && g < 100) r.grid[g] = cell_money (m);
			}
			if (!r.year || r.period < 1 || r.period > (r.monthly ? 12 : 4)) b.nret--;
		}
	}
	if (inEntry) entry_insert (b, cur);
	entry_free (cur);
	if (inDoc) cdoc_save (b, cdc);
	cdoc_free (cdc);
	if (!company) { book_clear (b); *why = "This file is not a Ledger company's books."; return false; }
	b.nextMatch = maxMatch + 1;
	years_sort (b);
	matches_check (b);
	b.changes++;
	return true;
}

// ---- files ------------------------------------------------------------------------------------------------------------------
// A file's bytes (a NUL after them); false when it cannot be read.
static bool file_read (const char *path, char **out, int *len)
{
	void *f = kapi_open (path);
	if (!f) return false;
	unsigned sz = kapi_fsize (f);
	if (sz > 256u << 20) sz = 256u << 20;
	char *b = new char[sz + 1];
	int n = kapi_read (f, b, sz);
	kapi_close (f);
	if (n < 0) n = 0;
	b[n] = '\0';
	*out = b; *len = n;
	return true;
}
// The books written: in path.tmp first, then the old file kept as path.bak and the new one renamed
// (a failure half-way leaves the old file or its .bak). False when nothing could be written.
static bool book_save (const Book &b, const char *path)
{
	Out o; book_write (b, o);
	char tmp[220], bak[220];
	scpy (tmp, path, sizeof tmp); scat (tmp, ".tmp", sizeof tmp);
	scpy (bak, path, sizeof bak); scat (bak, ".bak", sizeof bak);
	if (kapi_save_file (tmp, o.b, (unsigned) o.n) < 0) return kapi_save_file (path, o.b, (unsigned) o.n) >= 0;
	void *f = kapi_open (path);
	if (f)
	{
		kapi_close (f);
		kapi_remove (bak);
		if (kapi_rename (path, bak) < 0) { kapi_remove (tmp); return kapi_save_file (path, o.b, (unsigned) o.n) >= 0; }
	}
	if (kapi_rename (tmp, path) < 0) return kapi_save_file (path, o.b, (unsigned) o.n) >= 0;
	return true;
}

} // namespace lg

#endif
