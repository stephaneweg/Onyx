//
// Apps/mail/contacts.h -- Mail's contacts: a Cardfile form, SD:/Documents/Contacts.card (made at the first use: name,
// e-mail, other e-mail, phone, mobile, company, address, birthday, notes), read and written with Cardfile's own model
// (Apps/cardfile/model.h) -- Cardfile opens the same file. Also the addresses written to (SD:/mail/recipients.tsv:
// an address, its name, how often, when last), so that To: completes from both. Cardfile's files are Latin-1;
// Mail's text is UTF-8: converted both ways (what Latin-1 lacks becomes '?').
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _mail_contacts_h
#define _mail_contacts_h

#include "Apps/mail/accounts.h"
#include "Apps/cardfile/model.h"

namespace mailapp {

static const char *const CONTACTS_FILE = "SD:/Documents/Contacts.card";
static const char *const RECIPIENTS_FILE = "SD:/mail/recipients.tsv";

static void latin1_to_utf8 (const char *s, char *out, int cap)
{
	int k = 0;
	for (; *s && k < cap - 3; s++)
	{
		unsigned char c = (unsigned char) *s;
		if (c < 0x80) out[k++] = (char) c; else { out[k++] = (char) (0xC0 | c >> 6); out[k++] = (char) (0x80 | (c & 63)); }
	}
	out[k] = 0;
}
static void utf8_to_latin1 (const char *s, char *out, int cap)
{
	int k = 0;
	while (*s && k < cap - 1) { unsigned c = u8get (s); out[k++] = c < 256 ? (char) c : c == 0x20AC ? (char) 0x80 : '?'; }
	out[k] = 0;
}

struct Contact { char name[120], email[160], email2[160], phone[60], mobile[60], company[120], address[300], birthday[16], notes[600]; };

struct Contacts
{
	cf::Doc doc; bool ok;
	int col[9];				// the fields' indexes (-1: the form has none)
	struct Recent { char email[160], name[120]; int count; long long last; };
	Recent *rec; int nrec;

	Contacts () : ok (false), rec (0), nrec (0) { cf::doc_init (doc); for (int i = 0; i < 9; i++) col[i] = -1; }
	~Contacts () { cf::doc_clear (doc); free (rec); }

	static const char *const *COLS () { static const char *const C[9] = { "name", "email", "email2", "phone", "mobile", "company", "address", "birthday", "notes" }; return C; }
	void map_fields ()
	{
		for (int i = 0; i < 9; i++) { col[i] = -1; for (int k = 0; k < doc.nf; k++) if (!strcmp (doc.f[k].column, COLS ()[i])) col[i] = k; }
		// a form of the user's own: its first text field is the name, the one named like "mail" the address
		if (col[0] < 0 && doc.nf) col[0] = 0;
		if (col[1] < 0) for (int k = 0; k < doc.nf; k++) if (cf::ci_has (doc.f[k].column, "mail", 4) || cf::ci_has (doc.f[k].label, "mail", 4)) { col[1] = k; break; }
	}
	void make_form ()
	{
		cf::doc_clear (doc);
		cf::scpy (doc.title, "Contacts", sizeof doc.title);
		cf::scpy (doc.info, "The people Mail writes to (Onyx Mail and Cardfile share this file)", sizeof doc.info);
		static const struct { const char *label, *col; int type; } F[9] = {
			{ "Name", "name", cf::FT_TEXT }, { "E-mail", "email", cf::FT_TEXT }, { "Other e-mail", "email2", cf::FT_TEXT },
			{ "Phone", "phone", cf::FT_TEXT }, { "Mobile", "mobile", cf::FT_TEXT }, { "Company", "company", cf::FT_TEXT },
			{ "Address", "address", cf::FT_MEMO }, { "Birthday", "birthday", cf::FT_DATE }, { "Notes", "notes", cf::FT_MEMO } };
		for (int i = 0; i < 9; i++) cf::field_init (doc.f[i], F[i].label, F[i].col, F[i].type);
		doc.nf = 9; doc.sort = 0;
		map_fields ();
	}
	void load ()
	{
		int len; char *b = file_read (CONTACTS_FILE, &len);
		if (b) { ok = cf::doc_read (doc, b, len, 0); free (b); }
		if (!ok) { make_form (); ok = true; }
		map_fields ();
		load_recent ();
	}
	bool save ()
	{
		cf::Out o; cf::doc_write (doc, o);
		kapi_mkdir ("SD:/Documents");
		return kapi_save_file (CONTACTS_FILE, o.b ? o.b : "", (unsigned) o.n) >= 0;
	}
	void get (int i, Contact &c) const
	{
		memset (&c, 0, sizeof c);
		char *dst[9] = { c.name, c.email, c.email2, c.phone, c.mobile, c.company, c.address, c.birthday, c.notes };
		int cap[9] = { sizeof c.name, sizeof c.email, sizeof c.email2, sizeof c.phone, sizeof c.mobile, sizeof c.company, sizeof c.address, sizeof c.birthday, sizeof c.notes };
		for (int k = 0; k < 9; k++) if (col[k] >= 0 && i >= 0 && i < doc.nr) latin1_to_utf8 (doc.r[i][col[k]], dst[k], cap[k]);
	}
	void set (int i, const Contact &c)
	{
		const char *src[9] = { c.name, c.email, c.email2, c.phone, c.mobile, c.company, c.address, c.birthday, c.notes };
		for (int k = 0; k < 9; k++)
			if (col[k] >= 0)
			{
				char l[1024]; utf8_to_latin1 (src[k], l, sizeof l);
				char *v = cf::value_new (doc.f[col[k]], l, 0, true);
				if (v) cf::value_set (doc.r[i][col[k]], v);
			}
	}
	int add (const Contact &c) { int i = cf::doc_add_record (doc, -1); set (i, c); return i; }
	void remove (int i) { cf::doc_del_record (doc, i); }
	int count () const { return doc.nr; }
	// the contact of an address (-1: none)
	int find (const char *email) const
	{
		char l[200]; utf8_to_latin1 (email, l, sizeof l);
		for (int i = 0; i < doc.nr; i++)
			for (int k = 1; k <= 2; k++) if (col[k] >= 0 && cf::ci_eq (doc.r[i][col[k]], l)) return i;
		return -1;
	}
	// the order shown: by name (accents and case ignored)
	void sorted (int *out) const
	{
		for (int i = 0; i < doc.nr; i++) out[i] = i;
		int c = col[0];
		if (c < 0) return;
		for (int i = 1; i < doc.nr; i++) { int v = out[i], j = i - 1; while (j >= 0 && cf::ci_cmp (doc.r[out[j]][c], doc.r[v][c]) > 0) { out[j + 1] = out[j]; j--; } out[j + 1] = v; }
	}

	// ---- the addresses written to ----
	void load_recent ()
	{
		free (rec); rec = 0; nrec = 0;
		int len; char *b = file_read (RECIPIENTS_FILE, &len); if (!b) return;
		int cap = 0;
		for (char *l = b; l && *l; )
		{
			char *e = strchr (l, '\n'); if (e) *e = 0;
			char *t1 = strchr (l, '\t'), *t2 = t1 ? strchr (t1 + 1, '\t') : 0, *t3 = t2 ? strchr (t2 + 1, '\t') : 0;
			if (t1 && t2 && t3)
			{
				*t1 = *t2 = *t3 = 0;
				if (nrec == cap) { cap = cap ? cap * 2 : 64; rec = (Recent *) realloc (rec, sizeof (Recent) * cap); }
				Recent &r = rec[nrec++]; scpy (r.email, l, sizeof r.email); scpy (r.name, t1 + 1, sizeof r.name); r.count = atoi (t2 + 1); r.last = atoll (t3 + 1);
			}
			l = e ? e + 1 : 0;
		}
		free (b);
	}
	void wrote_to (const char *name, const char *email, long long when)
	{
		for (int i = 0; i < nrec; i++) if (ieq (rec[i].email, email)) { rec[i].count++; rec[i].last = when; if (name && *name) scpy (rec[i].name, name, sizeof rec[i].name); save_recent (); return; }
		rec = (Recent *) realloc (rec, sizeof (Recent) * (nrec + 1));
		Recent &r = rec[nrec++]; scpy (r.email, email, sizeof r.email); scpy (r.name, name ? name : "", sizeof r.name); r.count = 1; r.last = when;
		save_recent ();
	}
	void save_recent ()
	{
		Buf o; for (int i = 0; i < nrec; i++) o.addf ("%s\t%s\t%d\t%lld\n", rec[i].email, rec[i].name, rec[i].count, rec[i].last);
		kapi_mkdir ("SD:/mail");
		kapi_save_file (RECIPIENTS_FILE, o.c (), (unsigned) o.n);
	}
	// what "pre" completes to: the contacts' and the recent addresses whose name or address starts a word with it
	struct Match { char name[120], email[160]; int score; };
	static bool word_starts (const char *s, const char *pre)
	{
		int n = (int) strlen (pre); if (!n) return false;
		for (const char *p = s; *p; p++) if ((p == s || *(p - 1) == ' ' || *(p - 1) == '.' || *(p - 1) == '@' || *(p - 1) == '-' || *(p - 1) == '_') && ieqn (p, pre, n)) return true;
		return false;
	}
	int complete (const char *pre, Match *out, int max) const
	{
		int k = 0;
		for (int i = 0; i < doc.nr && k < max; i++)
		{
			Contact c; get (i, c);
			const char *mails[2] = { c.email, c.email2 };
			for (int m = 0; m < 2 && k < max; m++)
				if (mails[m][0] && (word_starts (c.name, pre) || word_starts (mails[m], pre)))
				{ scpy (out[k].name, c.name, sizeof out[k].name); scpy (out[k].email, mails[m], sizeof out[k].email); out[k].score = 1000; k++; }
		}
		for (int i = 0; i < nrec && k < max; i++)
		{
			bool dup = false; for (int q = 0; q < k; q++) if (ieq (out[q].email, rec[i].email)) dup = true;
			if (dup) continue;
			if (word_starts (rec[i].name, pre) || word_starts (rec[i].email, pre)) { scpy (out[k].name, rec[i].name, sizeof out[k].name); scpy (out[k].email, rec[i].email, sizeof out[k].email); out[k].score = rec[i].count; k++; }
		}
		// the contacts first, then the most written to
		for (int i = 1; i < k; i++) { Match t = out[i]; int j = i - 1; while (j >= 0 && out[j].score < t.score) { out[j + 1] = out[j]; j--; } out[j + 1] = t; }
		return k;
	}
};

} // namespace mailapp

#endif
