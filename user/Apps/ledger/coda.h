//
// coda.h -- a bank's statements in CODA (Febelfin's "coded statement of account", version 2): lines of 128
// characters -- 0 the file's header, 1 the old balance (the account: its IBAN, or an old Belgian number),
// 21 / 22 / 23 a movement (its amount and dates, its communication -- a Belgian structured one, +++...+++,
// or free text --, the counterparty's account, BIC and name), 3x more about it, 4 free text, 8 the new
// balance, 9 the trailer (another statement may follow). Read into CodaStmt (coda_read); then made a
// statement of Ledger's (post.h) for a bank journal (coda_statement): each movement's party found by its
// structured communication (the invoice it pays, ticked: sales and purchases alike), else by the
// counterparty's account (a party's IBAN) or name, its open item ticked when one is as much (or all of
// them together are); the bank's charges on 657200; what is not found left for the user to complete.
//
#ifndef _ledger_coda_h
#define _ledger_coda_h

#include "post.h"

namespace lg {

enum { CODA_MAXST = 64 };
struct CodaMove
{
	money amount;					// in > 0, out < 0
	int date, value;				// its entry date, its value date
	char ogm[13];					// a structured communication's twelve digits ("": none)
	char comm[160];					// the free communication
	char cpIban[36], cpBic[12], cpName[36];		// the counterparty
	char code[9];					// its transaction's code: type, family (2), operation (2), category (3)
	char ref[22];					// the bank's reference
};
struct CodaStmt
{
	char iban[36], cur[4], holder[28];
	int paper, seq;					// the paper statement's number, the CODA one
	int oldDate, newDate;
	money oldBal, newBal;
	CodaMove *m; int nm, cap;
};
static void coda_init (CodaStmt &c) { c.iban[0] = c.cur[0] = c.holder[0] = '\0'; c.paper = c.seq = c.oldDate = c.newDate = 0; c.oldBal = c.newBal = 0; c.m = 0; c.nm = c.cap = 0; }
static void coda_free (CodaStmt &c) { delete [] c.m; c.m = 0; c.nm = c.cap = 0; }
static CodaMove &coda_add (CodaStmt &c)
{
	if (c.nm == c.cap) { int k = c.cap ? c.cap * 2 : 32; CodaMove *n = new CodaMove[k]; for (int i = 0; i < c.nm; i++) n[i] = c.m[i]; delete [] c.m; c.m = n; c.cap = k; }
	CodaMove &m = c.m[c.nm++];
	m.amount = 0; m.date = m.value = 0; m.ogm[0] = m.comm[0] = m.cpIban[0] = m.cpBic[0] = m.cpName[0] = m.code[0] = m.ref[0] = '\0';
	return m;
}

// ---- reading ---------------------------------------------------------------------------------------------------------
// A record's field: characters [a, b] (from 1, as Febelfin numbers them), trimmed.
static void cfield (const char *l, int a, int b, char *out, int cap)
{
	int n = 0;
	for (int i = a - 1; i < b && n < cap - 1; i++) out[n++] = l[i];
	while (n > 0 && out[n - 1] == ' ') n--;
	int s = 0; while (s < n && out[s] == ' ') s++;
	for (int i = 0; i < n - s; i++) out[i] = out[i + s];
	out[n - s] = '\0';
}
static long long cnum (const char *l, int a, int b) { long long v = 0; for (int i = a - 1; i < b; i++) if (digit (l[i])) v = v * 10 + (l[i] - '0'); return v; }
// DDMMYY -> yyyymmdd (0: none).
static int cdate (const char *l, int a)
{
	int d = (int) cnum (l, a, a + 1), m = (int) cnum (l, a + 2, a + 3), y = (int) cnum (l, a + 4, a + 5);
	if (!d || !m) return 0;
	return (2000 + y) * 10000 + m * 100 + d;
}
// An amount: its sign's character (0 credit: in; 1 debit: out), then 15 digits with 3 decimals -> cents.
static money camount (const char *l, int signAt, int from)
{
	long long v = cnum (l, from, from + 14);
	money c = (money) ((v + 5) / 10);
	return l[signAt - 1] == '1' ? -c : c;
}
// An old Belgian account number (12 digits) -> its IBAN.
static void bban_iban (const char *d12, char *out, int cap)
{
	char t[24]; scpy (t, d12, sizeof t); scat (t, "111400", sizeof t);		// (BE00 as digits)
	int r = 0; for (const char *p = t; *p; p++) r = (r * 10 + (*p - '0')) % 97;
	int chk = 98 - r;
	char o[24] = "BE"; o[2] = (char) ('0' + chk / 10); o[3] = (char) ('0' + chk % 10); o[4] = '\0'; scat (o, d12, sizeof o);
	scpy (out, o, cap);
}
// An account's number as CODA writes it (an IBAN, a Belgian number, spaces) -> an IBAN when it is one.
static void caccount (const char *raw, char *out, int cap)
{
	char t[40]; iban_normalize (raw, t, sizeof t);
	bool d = slen (t) == 12; for (int i = 0; d && t[i]; i++) if (!digit (t[i])) d = false;
	if (d) bban_iban (t, out, cap); else scpy (out, t, cap);
}
// Words' spaces made single, the ends trimmed.
static void squeeze (char *s)
{
	int n = 0; bool sp = true;
	for (const char *p = s; *p; p++)
	{
		if (*p == ' ') { if (!sp) s[n++] = ' '; sp = true; }
		else { s[n++] = *p; sp = false; }
	}
	while (n > 0 && s[n - 1] == ' ') n--;
	s[n] = '\0';
}
// A CODA file -> its statements (st: CODA_MAXST, each initialised here; coda_free them after) -> how many
// (0: none -- *why says why).
static int coda_read (const char *b, int n, CodaStmt *st, const char **why)
{
	*why = "The file holds no CODA statement (its lines: 128 characters, the first one starting with 0).";
	int ns = 0; CodaStmt *cur = 0; CodaMove *mv = 0;
	bool skip = false;					// (a globalisation's details: their total already read)
	char comm[200] = "";					// (the movement's free communication so far)
	auto endMove = [&] () { if (mv && !mv->ogm[0]) { scpy (mv->comm, comm, sizeof mv->comm); squeeze (mv->comm); } comm[0] = '\0'; };
	const char *p = b, *end = b + n;
	while (p < end)
	{
		const char *e = p; while (e < end && *e != '\n' && *e != '\r' && e - p < 128) e++;
		char l[130]; int k = 0;
		for (const char *q = p; q < e && k < 128; q++) l[k++] = *q == '\t' ? ' ' : *q;
		while (k < 128) l[k++] = ' ';
		l[128] = '\0';
		bool blank = e == p;
		p = e; while (p < end && (*p == '\n' || *p == '\r')) p++;
		if (blank) continue;
		switch (l[0])
		{
		case '0': break;
		case '1':
			endMove (); mv = 0;
			if (ns >= CODA_MAXST) { *why = "The file holds too many statements."; break; }
			cur = &st[ns++]; coda_init (*cur);
			{
				char a[40];
				if (l[1] == '0') { cfield (l, 6, 17, a, sizeof a); caccount (a, cur->iban, sizeof cur->iban); cfield (l, 19, 21, cur->cur, sizeof cur->cur); }
				else if (l[1] == '2') { cfield (l, 6, 36, a, sizeof a); caccount (a, cur->iban, sizeof cur->iban); cfield (l, 40, 42, cur->cur, sizeof cur->cur); }
				else { cfield (l, 6, 39, a, sizeof a); caccount (a, cur->iban, sizeof cur->iban); cfield (l, 40, 42, cur->cur, sizeof cur->cur); }
			}
			cur->paper = (int) cnum (l, 3, 5);
			cur->oldBal = camount (l, 43, 44); cur->oldDate = cdate (l, 59);
			cfield (l, 65, 90, cur->holder, sizeof cur->holder);
			cur->seq = (int) cnum (l, 126, 128);
			skip = false;
			break;
		case '2':
			if (!cur) break;
			if (l[1] == '1')
			{
				endMove ();
				skip = cnum (l, 7, 10) != 0;				// (a detail of a globalised amount)
				if (skip) { mv = 0; break; }
				mv = &coda_add (*cur);
				cfield (l, 11, 31, mv->ref, sizeof mv->ref);
				mv->amount = camount (l, 32, 33);
				mv->value = cdate (l, 48);
				cfield (l, 54, 61, mv->code, sizeof mv->code);
				mv->date = cdate (l, 116);
				if (l[61] == '1' && (l[62] == '1' && l[63] == '0' && (l[64] == '1' || l[64] == '2')))	// 101 / 102: +++...+++
				{
					char d[16]; cfield (l, 66, 77, d, sizeof d);
					if (!ogm_parse (d, mv->ogm)) { mv->ogm[0] = '\0'; scpy (comm, d, sizeof comm); }
				}
				else if (l[61] == '1') { char t[60]; cfield (l, 66, 115, t, sizeof t); scpy (comm, t, sizeof comm); }
				else { char t[60]; int k2 = 0; for (int i = 62; i < 115; i++) t[k2++] = l[i]; t[k2] = '\0'; scpy (comm, t, sizeof comm); }
			}
			else if (l[1] == '2' && mv && !skip)
			{
				char t[60]; int k2 = 0; for (int i = 10; i < 63; i++) t[k2++] = l[i]; t[k2] = '\0'; scat (comm, t, sizeof comm);
				cfield (l, 99, 109, mv->cpBic, sizeof mv->cpBic);
			}
			else if (l[1] == '3' && mv && !skip)
			{
				char a[40]; cfield (l, 11, 44, a, sizeof a); caccount (a, mv->cpIban, sizeof mv->cpIban);
				cfield (l, 48, 82, mv->cpName, sizeof mv->cpName);
				char t[48]; int k2 = 0; for (int i = 82; i < 125; i++) t[k2++] = l[i]; t[k2] = '\0'; scat (comm, t, sizeof comm);
			}
			break;
		case '3': case '4': break;					// (more about a movement; free text)
		case '8':
			endMove (); mv = 0;
			if (!cur) break;
			cur->newBal = camount (l, 42, 43); cur->newDate = cdate (l, 58);
			break;
		case '9': endMove (); mv = 0; cur = 0; break;
		default:
			for (int i = 0; i < ns; i++) coda_free (st[i]);
			*why = "The file is not a CODA file (a line starts with neither 0, 1, 2, 3, 4, 8 nor 9).";
			return 0;
		}
	}
	endMove ();
	for (int i = 0; i < ns; i++)
	{
		money s = st[i].oldBal; for (int k = 0; k < st[i].nm; k++) s += st[i].m[k].amount;
		if (s != st[i].newBal) { for (int j = 0; j < ns; j++) coda_free (st[j]); *why = "A statement's movements do not add up to its new balance: the file is incomplete."; return 0; }
		if (!st[i].newDate) st[i].newDate = st[i].nm ? st[i].m[st[i].nm - 1].date : st[i].oldDate;
	}
	if (ns) *why = "";
	return ns;
}

// ---- made a statement -------------------------------------------------------------------------------------------------
// Where needle is in hay (-1: nowhere).
static int sfind (const char *hay, const char *needle)
{
	int n = slen (needle);
	if (!n) return 0;
	for (int i = 0; hay[i]; i++) { int k = 0; while (k < n && hay[i + k] == needle[k]) k++; if (k == n) return i; }
	return -1;
}
// A name folded for comparing: capitals, no accents, letters and digits only.
static void fold_name (const char *s, char *out, int cap)
{
	int n = 0;
	for (const unsigned char *p = (const unsigned char *) s; *p && n < cap - 1; p++)
	{
		unsigned c = *p;
		if (c >= 0xC0 && c <= 0xC5) c = 'A'; else if (c >= 0xE0 && c <= 0xE5) c = 'A';
		else if (c == 0xC7 || c == 0xE7) c = 'C';
		else if ((c >= 0xC8 && c <= 0xCB) || (c >= 0xE8 && c <= 0xEB)) c = 'E';
		else if ((c >= 0xCC && c <= 0xCF) || (c >= 0xEC && c <= 0xEF)) c = 'I';
		else if ((c >= 0xD2 && c <= 0xD6) || (c >= 0xF2 && c <= 0xF6)) c = 'O';
		else if ((c >= 0xD9 && c <= 0xDC) || (c >= 0xF9 && c <= 0xFC)) c = 'U';
		else if (c == 0xD1 || c == 0xF1) c = 'N';
		c = (unsigned) up ((char) c);
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out[n++] = (char) c;
	}
	out[n] = '\0';
}
// A party's open lines (its customers' / suppliers' account's, not matched): their LineRefs -> how many.
static int party_open (const Book &b, int party, LineRef *r, int cap)
{
	int n = 0;
	for (int i = 0; i < b.ne && n < cap; i++)
		for (int k = 0; k < b.e[i].nl && n < cap; k++)
		{
			const Line &x = b.e[i].l[k];
			if (x.party == party && !x.match && x.amount && acc_party (b, x.account)) { r[n].e = i; r[n].l = k; n++; }
		}
	return n;
}
// The party a movement comes from: by the counterparty's account, else its name.
static int coda_party (const Book &b, const CodaMove &m)
{
	if (m.cpIban[0])
		for (int i = 0; i < b.npty; i++) { char t[40]; iban_normalize (b.pty[i].iban, t, sizeof t); if (t[0] && seq (t, m.cpIban)) return b.pty[i].id; }
	char cn[40]; fold_name (m.cpName, cn, sizeof cn);
	if (slen (cn) < 4) return 0;
	int hit = 0, hits = 0;
	for (int i = 0; i < b.npty; i++)
	{
		char pn[80]; fold_name (b.pty[i].name, pn, sizeof pn);
		if (slen (pn) < 4) continue;
		if (seq (pn, cn)) return b.pty[i].id;
		if (sfind (pn, cn) >= 0 || sfind (cn, pn) >= 0) { hit = b.pty[i].id; hits++; }
	}
	return hits == 1 ? hit : 0;
}
// A CODA statement -> journal j's statement (s: initialised here): its movements, their parties and the
// items they pay -> how many movements are left to complete (no party nor account).
static int coda_statement (const Book &b, const CodaStmt &c, int journal, Statement &s)
{
	st_free (s); st_init (s);
	s.journal = journal; s.date = c.newDate;
	char t[48] = "Statement "; scat_num (t, c.paper, sizeof t); scpy (s.text, t, sizeof s.text);
	s.old = fin_balance_before (b, journal, s.date);
	int left = 0;
	static LineRef open[400];
	for (int i = 0; i < c.nm; i++)
	{
		const CodaMove &m = c.m[i];
		StLine &l = st_add (s);
		l.amount = m.amount;
		// its description: the counterparty, the communication
		char d[200] = "";
		if (m.cpName[0]) scpy (d, m.cpName, sizeof d);
		if (m.ogm[0]) { char o[24]; ogm_show (m.ogm, o); if (d[0]) scat (d, " ", sizeof d); scat (d, o, sizeof d); }
		else if (m.comm[0]) { if (d[0]) scat (d, " ", sizeof d); scat (d, m.comm, sizeof d); }
		scpy (l.text, d, sizeof l.text);
		// the invoice its structured communication names
		if (m.ogm[0])
			for (int x = 0; x < b.ne && !l.npay; x++)
			{
				const Entry &e = b.e[x];
				if (!seq (e.comm, m.ogm)) continue;
				for (int k = 0; k < e.nl; k++)
					if (e.l[k].party && !e.l[k].match && acc_party (b, e.l[k].account))
					{ l.party = e.l[k].party; l.pay[0].entry = e.id; l.pay[0].line = k; l.npay = 1; break; }
			}
		// else its party, the items it pays: the one as much, else all together when they are
		if (!l.party) l.party = coda_party (b, m);
		if (l.party && !l.npay)
		{
			int no = party_open (b, l.party, open, 400), hit = -1, hits = 0;
			money all = 0;
			for (int k = 0; k < no; k++) { money a = b.e[open[k].e].l[open[k].l].amount; all += a; if (a == m.amount) { hit = k; hits++; } }
			if (hits == 1) { l.pay[0].entry = b.e[open[hit].e].id; l.pay[0].line = open[hit].l; l.npay = 1; }
			else if (!hits && no > 1 && no <= MAXPAY && all == m.amount)
				for (int k = 0; k < no; k++) { l.pay[l.npay].entry = b.e[open[k].e].id; l.pay[l.npay].line = open[k].l; l.npay++; }
		}
		// the bank's own charges (families 35: closing; 80: costs)
		if (!l.party && m.amount < 0 && !m.cpIban[0] && slen (m.code) >= 3 && ((m.code[1] == '3' && m.code[2] == '5') || (m.code[1] == '8' && m.code[2] == '0'))
		    && acc_postable (b, "657200"))
			scpy (l.account, "657200", CODE_MAX);
		if (!l.party && !l.account[0]) left++;
	}
	s.now = s.old + st_sum (s);
	return left;
}
// Is that statement in the books already? (the journal's statement of that day ending on that balance)
static bool coda_known (const Book &b, const CodaStmt &c, int journal)
{
	for (int i = 0; i < b.ne; i++)
	{
		const Entry &e = b.e[i];
		if (e.journal == journal && e.date == c.newDate && e.stmtNew == c.newBal && (e.stmtOld || e.stmtNew)) return true;
	}
	return false;
}
// The bank journal of an account (its IBAN) -> its index, -1: none.
static int coda_journal (const Book &b, const char *iban)
{
	for (int j = 0; j < b.njr; j++) { char t[40]; iban_normalize (b.jr[j].iban, t, sizeof t); if (b.jr[j].type == JT_BANK && t[0] && seq (t, iban)) return j; }
	return -1;
}

// ---- written (the demo company's, the tests') ------------------------------------------------------------------------------
static void cput (char *l, int a, const char *s, int w) { for (int i = 0; i < w && s[i]; i++) l[a - 1 + i] = s[i]; }
static void cputn (char *l, int a, long long v, int w) { if (v < 0) v = -v; for (int i = w - 1; i >= 0; i--) { l[a - 1 + i] = (char) ('0' + v % 10); v /= 10; } }
static void cput_date (char *l, int a, int ymd) { cputn (l, a, d_of (ymd), 2); cputn (l, a + 2, m_of (ymd), 2); cputn (l, a + 4, y_of (ymd) % 100, 2); }
static void cput_amount (char *l, int signAt, money v) { l[signAt - 1] = v < 0 ? '1' : '0'; cputn (l, signAt + 1, (long long) (v < 0 ? -v : v) * 10, 15); }
static void cline (Out &o, char *l) { l[128] = '\0'; o.puts (l); o.puts ("\r\n"); for (int i = 0; i < 128; i++) l[i] = ' '; }
// A statement as CODA (version 2; its account an IBAN): created on `created`, the bank's BIC; `last`: the
// file's last one.
static void coda_write (const CodaStmt &c, const char *bic, const char *holderId, int created, bool first, bool last, Out &o)
{
	char l[130]; for (int i = 0; i < 128; i++) l[i] = ' ';
	int n = 0; money deb = 0, cred = 0;
	if (first)
	{
		l[0] = '0'; cputn (l, 2, 0, 4); cput_date (l, 6, created); cput (l, 12, "000", 3); cput (l, 15, "05", 2);
		cput (l, 25, "LEDGER", 10); cput (l, 35, c.holder, 26); cput (l, 61, bic, 11); cput (l, 72, holderId, 11); cput (l, 84, "00000", 5); l[127] = '2';
		cline (o, l);
	}
	l[0] = '1'; l[1] = '2'; cputn (l, 3, c.paper, 3); cput (l, 6, c.iban, 31); cput (l, 40, c.cur[0] ? c.cur : "EUR", 3);
	cput_amount (l, 43, c.oldBal); cput_date (l, 59, c.oldDate); cput (l, 65, c.holder, 26); cput (l, 91, "Compte courant", 35); cputn (l, 126, c.seq, 3);
	cline (o, l); n++;
	for (int i = 0; i < c.nm; i++)
	{
		const CodaMove &m = c.m[i];
		bool more = m.cpBic[0] || m.cpIban[0] || m.cpName[0] || slen (m.comm) > 53;
		char seqs[8]; int sq = i + 1;
		l[0] = '2'; l[1] = '1'; cputn (l, 3, sq, 4); cputn (l, 7, 0, 4); cput (l, 11, m.ref, 21); cput_amount (l, 32, m.amount); cput_date (l, 48, m.value ? m.value : m.date);
		cput (l, 54, m.code[0] ? m.code : "00150000", 8);
		if (m.ogm[0]) { l[61] = '1'; cput (l, 63, "101", 3); cput (l, 66, m.ogm, 12); }
		else { l[61] = '0'; cput (l, 63, m.comm, 53); }
		cput_date (l, 116, m.date); cputn (l, 122, c.paper, 3); l[124] = '0'; l[125] = more ? '1' : '0'; l[127] = '0';
		cline (o, l); n++;
		if (more)
		{
			l[0] = '2'; l[1] = '2'; cputn (l, 3, sq, 4); cputn (l, 7, 0, 4);
			if (!m.ogm[0] && slen (m.comm) > 53) cput (l, 11, m.comm + 53, 53);
			cput (l, 99, m.cpBic, 11); l[125] = '1'; l[127] = '0';
			cline (o, l); n++;
			l[0] = '2'; l[1] = '3'; cputn (l, 3, sq, 4); cputn (l, 7, 0, 4); cput (l, 11, m.cpIban, 34); if (m.cpIban[0]) cput (l, 45, "EUR", 3);
			cput (l, 48, m.cpName, 35); l[125] = '0'; l[127] = '0';
			cline (o, l); n++;
		}
		(void) seqs;
		if (m.amount < 0) deb -= m.amount; else cred += m.amount;
	}
	l[0] = '8'; cputn (l, 2, c.paper, 3); cput (l, 5, c.iban, 31); cput (l, 39, c.cur[0] ? c.cur : "EUR", 3); cput_amount (l, 42, c.newBal); cput_date (l, 58, c.newDate); l[127] = '0';
	cline (o, l); n++;
	l[0] = '9'; cputn (l, 17, n, 6); cputn (l, 23, deb * 10, 15); cputn (l, 38, cred * 10, 15); l[127] = last ? '2' : '1';
	cline (o, l);
}

} // namespace lg

#endif
