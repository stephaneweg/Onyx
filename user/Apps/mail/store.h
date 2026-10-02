//
// Apps/mail/store.h -- what Mail keeps of each account: its folders (their names, what they are for, IMAP's
// UIDVALIDITY / UIDNEXT), each folder's messages (who, to whom, when, the subject, the flags, the conversation's
// keys, the first words of the text), the messages opened (whole, as .eml) -- on the card, in SD:/mail/<account>/:
//
//   folders.tsv                  a folder a line: name, shown name, special use, delimiter, no-select, uidvalidity, uidnext, dir
//   <dir>/index.tsv              a message a line (its fields tab separated, \t \n \\ escaped)
//   <dir>/<uid>.eml              a message fetched whole
//   pop.tsv                      (POP3) the UIDLs taken: uidl, when first seen
//
// The model lives on the window's thread; the sync worker (sync.h) hands it what changed (Mail's thread applies it).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _mail_store_h
#define _mail_store_h

#include "Apps/mail/accounts.h"
#include "mail/imap.h"
#include "mail/mime.h"
#include "mail/html_dom.h"

namespace mailapp {

using html::Vec;

struct Msg
{
	long uid; unsigned flags; long long date; long size; unsigned long long gmThread;
	bool attach, textHtml;
	char *from, *to, *cc, *replyTo, *subject, *msgid, *irt, *refs, *preview;
	char textPart[24], textEnc[24], textCs[24];
};
static void msg_free (Msg &m)
{
	free (m.from); free (m.to); free (m.cc); free (m.replyTo); free (m.subject); free (m.msgid); free (m.irt); free (m.refs); free (m.preview);
	memset (&m, 0, sizeof m);
}
static void msg_from_env (Msg &m, const Envelope &e)
{
	memset (&m, 0, sizeof m);
	m.uid = e.uid; m.flags = e.flags; m.date = e.date; m.size = e.size; m.gmThread = e.gmThread; m.attach = e.attach; m.textHtml = e.textHtml;
	m.from = sdup (e.from); m.to = sdup (e.to); m.cc = sdup (e.cc); m.replyTo = sdup (e.replyTo); m.subject = sdup (e.subject);
	m.msgid = sdup (e.msgid); m.irt = sdup (e.inReplyTo); m.refs = sdup (e.refs); m.preview = sdup ("");
	scpy (m.textPart, e.textPart, sizeof m.textPart); scpy (m.textEnc, e.textEnc, sizeof m.textEnc); scpy (m.textCs, e.textCs, sizeof m.textCs);
}

struct Folder
{
	char name[200], show[120]; int special; char delim; bool noselect;
	long uidvalidity, uidnext;
	char dir[16];
	Vec<Msg> msgs;
	int unread;
	bool loaded, opened, dirty;		// (index read; looked at this session: synced with the Inbox; to be written)
	long exists;
};

// the text's first words (a part's bytes as they came: its encoding, its charset, HTML's tags off)
static void preview_of (const char *data, int n, const char *enc, const char *cs, bool html, char *out, int cap)
{
	Buf raw;
	if (ieq (enc, "base64")) b64_decode (raw, data, n);
	else if (ieq (enc, "quoted-printable")) qp_decode (raw, data, n);
	else raw.add (data, n);
	Buf u; to_utf8 (u, raw.c (), raw.n, cs && *cs ? cs : "us-ascii");
	Buf t;
	if (html)
	{	// the tags off, the head / style / script skipped, the references decoded
		const char *p = u.c (), *e = p + u.n;
		Buf plain;
		while (p < e)
		{
			if (*p == '<')
			{
				const char *q = p + 1; bool skip = false;
				if (e - q >= 5 && (ieqn (q, "style", 5) || ieqn (q, "title", 5))) skip = true;
				if (e - q >= 6 && ieqn (q, "script", 6)) skip = true;
				if (e - q >= 4 && ieqn (q, "head", 4)) skip = true;
				if (skip)
				{
					char nm[8]; int k = 0; while (q < e && k < 7 && ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z'))) nm[k++] = *q++; nm[k] = 0;
					char close[12]; snprintf (close, sizeof close, "</%s", nm);
					const char *c = q; while (c < e && !(c[0] == '<' && c[1] == '/' && ieqn (c + 2, nm, k))) c++;
					p = c; while (p < e && *p != '>') p++; if (p < e) p++;
					continue;
				}
				if (e - q >= 3 && !memcmp (q, "!--", 3)) { const char *c = q; while (c + 2 < e && !(c[0] == '-' && c[1] == '-' && c[2] == '>')) c++; p = c + 3; continue; }
				while (p < e && *p != '>') p++; if (p < e) p++;
				plain.addc (' ');
				continue;
			}
			const char *a = p; while (p < e && *p != '<') p++;
			plain.add (a, (int) (p - a));
		}
		html::decode_text (t, plain.c (), plain.n);
	}
	else t.add (u.c (), u.n);
	// the quoted lines and the white space out
	int k = 0; bool sp = true; bool bol = true;
	for (const char *p = t.c (); *p && k < cap - 4; p++)
	{
		if (bol && *p == '>') { while (*p && *p != '\n') p++; if (!*p) break; continue; }
		bol = *p == '\n';
		unsigned char c = (unsigned char) *p;
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || (c == 0xC2 && (unsigned char) p[1] == 0xA0)) { if (c == 0xC2) p++; if (!sp) out[k++] = ' '; sp = true; continue; }
		out[k++] = *p; sp = false;
	}
	while (k > 0 && out[k - 1] == ' ') k--;
	// (a UTF-8 sequence cut at the end: dropped)
	int z = k; while (z > 0 && ((unsigned char) out[z - 1] & 0xC0) == 0x80) z--;
	if (z > 0 && (unsigned char) out[z - 1] >= 0xC0) { int need = (unsigned char) out[z - 1] >= 0xF0 ? 4 : (unsigned char) out[z - 1] >= 0xE0 ? 3 : 2; if (k - (z - 1) < need) k = z - 1; }
	out[k] = 0;
}

static unsigned fnv (const char *s) { unsigned h = 2166136261u; for (; *s; s++) h = (h ^ (unsigned char) *s) * 16777619u; return h; }

struct Store
{
	Account *acct;
	Vec<Folder> folders;
	char root[64];					// SD:/mail/<id>
	Store () : acct (0) { root[0] = 0; }
	~Store () { for (int i = 0; i < folders.n; i++) clear_folder (folders[i]); }
	Store (const Store &) = delete;
	Store &operator= (const Store &) = delete;

	static void clear_folder (Folder &f) { for (int i = 0; i < f.msgs.n; i++) msg_free (f.msgs[i]); free (f.msgs.p); f.msgs.p = 0; f.msgs.n = f.msgs.cap = 0; }
	void open (Account *a)
	{
		acct = a;
		snprintf (root, sizeof root, "SD:/mail/%s", a->id);
		load_folders ();
		if (a->kind == K_POP3 && !folders.n)
		{	// the card's own folders
			static const struct { const char *n, *s; int sp; } P[] = { { "INBOX", "Inbox", SP_INBOX }, { "Sent", "Sent", SP_SENT }, { "Drafts", "Drafts", SP_DRAFTS }, { "Archive", "Archive", SP_ARCHIVE }, { "Trash", "Trash", SP_TRASH } };
			for (int i = 0; i < 5; i++) { Folder &f = add_folder (P[i].n); scpy (f.show, P[i].s, sizeof f.show); f.special = P[i].sp; f.uidnext = 1; f.uidvalidity = 1; }
			save_folders ();
		}
	}
	Folder &add_folder (const char *name)
	{
		Folder &f = folders.push ();			// (zeroed: its message list empty)
		scpy (f.name, name, sizeof f.name); scpy (f.show, name, sizeof f.show); f.delim = '/';
		snprintf (f.dir, sizeof f.dir, "f%08x", fnv (name));
		return f;
	}
	Folder *find (const char *name) { for (int i = 0; i < folders.n; i++) if (!strcmp (folders[i].name, name)) return &folders[i]; return 0; }
	Folder *special (int sp) { for (int i = 0; i < folders.n; i++) if (folders[i].special == sp) return &folders[i]; return 0; }
	int index_of (const Folder *f) const { return (int) (f - folders.p); }

	// ---- the folders' list ----
	void load_folders ()
	{
		char p[96]; snprintf (p, sizeof p, "%s/folders.tsv", root);
		int len; char *b = file_read (p, &len); if (!b) return;
		for (char *l = b; l && *l; )
		{
			char *e = strchr (l, '\n'); if (e) *e = 0;
			char *f[8]; int k = 0; f[k++] = l;
			for (char *q = l; *q && k < 8; q++) if (*q == '\t') { *q = 0; f[k++] = q + 1; }
			if (k >= 8 && *l != '#')
			{
				char name[200]; unesc (name, f[0], sizeof name);
				Folder &F = add_folder (name);
				unesc (F.show, f[1], sizeof F.show); F.special = atoi (f[2]); F.delim = f[3][0]; F.noselect = atoi (f[4]) != 0;
				F.uidvalidity = atol (f[5]); F.uidnext = atol (f[6]); scpy (F.dir, f[7], sizeof F.dir);
			}
			l = e ? e + 1 : 0;
		}
		free (b);
	}
	void save_folders ()
	{
		mkdirs (root);
		Buf o; o.add ("# Onyx Mail: folders\n");
		for (int i = 0; i < folders.n; i++)
		{
			const Folder &f = folders[i];
			esc (o, f.name); o.addc ('\t'); esc (o, f.show);
			o.addf ("\t%d\t%c\t%d\t%ld\t%ld\t%s\n", f.special, f.delim ? f.delim : '/', f.noselect ? 1 : 0, f.uidvalidity, f.uidnext, f.dir);
		}
		char p[96]; snprintf (p, sizeof p, "%s/folders.tsv", root);
		kapi_save_file (p, o.c (), (unsigned) o.n);
	}
	// the list from the server: kept folders keep their messages, gone ones go (their files left: cleaned later)
	void set_folders (const ImapFolder *in, int n)
	{
		Vec<Folder> keep;
		for (int i = 0; i < n; i++)
		{
			Folder *old = find (in[i].name);
			Folder &f = keep.push ();
			if (old) { memcpy (&f, old, sizeof f); old->msgs.p = 0; old->msgs.n = old->msgs.cap = 0; old->name[0] = 1; }	// (moved over)
			else { scpy (f.name, in[i].name, sizeof f.name); snprintf (f.dir, sizeof f.dir, "f%08x", fnv (in[i].name)); }
			scpy (f.show, in[i].show, sizeof f.show); f.special = in[i].special; f.delim = in[i].delim; f.noselect = in[i].noselect;
		}
		for (int i = 0; i < folders.n; i++) if (folders[i].name[0] != 1) clear_folder (folders[i]);
		folders.take (keep);
		// the order: Inbox, the special ones, then the others by name
		for (int i = 1; i < folders.n; i++)
		{
			Folder t; memcpy (&t, &folders[i], sizeof t); int j = i - 1;
			while (j >= 0 && folder_cmp (t, folders[j]) < 0) { memcpy (&folders[j + 1], &folders[j], sizeof t); j--; }
			memcpy (&folders[j + 1], &t, sizeof t);
			t.msgs.p = 0;
		}
		save_folders ();
	}
	static int rank (const Folder &f)
	{
		static const int R[] = { 50, 0, 2, 1, 5, 4, 3, 6, 7 };	// none, inbox, sent, drafts, trash, junk, archive, all, flagged
		return f.special >= 0 && f.special <= 8 ? R[f.special] : 50;
	}
	static int folder_cmp (const Folder &a, const Folder &b)
	{
		int ra = rank (a), rb = rank (b);
		if (ra != rb) return ra - rb;
		Buf x, y; mutf7_decode (x, a.name); mutf7_decode (y, b.name);
		for (const char *p = x.c (), *q = y.c (); ; p++, q++) { int c = lc (*p) - lc (*q); if (c || !*p) return c; }
	}

	// ---- a folder's index ----
	void path_of (const Folder &f, const char *file, char *out, int cap) const { snprintf (out, cap, "%s/%s/%s", root, f.dir, file); }
	void load_index (Folder &f)
	{
		if (f.loaded) return;
		f.loaded = true;
		char p[128]; path_of (f, "index.tsv", p, sizeof p);
		int len; char *b = file_read (p, &len); if (!b) return;
		for (char *l = b; l && *l; )
		{
			char *e = strchr (l, '\n'); if (e) *e = 0;
			if (*l != '#')
			{
				char *v[20]; int k = 0; v[k++] = l;
				for (char *q = l; *q && k < 20; q++) if (*q == '\t') { *q = 0; v[k++] = q + 1; }
				if (k >= 19)
				{
					Msg &m = f.msgs.push ();
					m.uid = atol (v[0]); m.flags = (unsigned) atol (v[1]); m.date = atoll (v[2]); m.size = atol (v[3]); m.gmThread = strtoull (v[4], 0, 10);
					m.attach = v[5][0] == '1'; scpy (m.textPart, v[6], sizeof m.textPart); scpy (m.textEnc, v[7], sizeof m.textEnc); scpy (m.textCs, v[8], sizeof m.textCs); m.textHtml = v[9][0] == '1';
					char t[2000];
					char **dst[9] = { &m.from, &m.to, &m.cc, &m.replyTo, &m.subject, &m.msgid, &m.irt, &m.refs, &m.preview };
					for (int i = 0; i < 9; i++) { unesc (t, v[10 + i], sizeof t); *dst[i] = sdup (t); }
				}
			}
			l = e ? e + 1 : 0;
		}
		free (b);
		count (f);
	}
	void save_index (Folder &f)
	{
		char d[96]; snprintf (d, sizeof d, "%s/%s", root, f.dir); mkdirs (d);
		Buf o; o.addf ("# Onyx Mail: %s\n", f.name);
		for (int i = 0; i < f.msgs.n; i++)
		{
			const Msg &m = f.msgs[i];
			o.addf ("%ld\t%u\t%lld\t%ld\t%llu\t%d\t%s\t%s\t%s\t%d", m.uid, m.flags, m.date, m.size, m.gmThread, m.attach ? 1 : 0, m.textPart, m.textEnc, m.textCs, m.textHtml ? 1 : 0);
			const char *s[9] = { m.from, m.to, m.cc, m.replyTo, m.subject, m.msgid, m.irt, m.refs, m.preview };
			for (int k = 0; k < 9; k++) { o.addc ('\t'); esc (o, s[k] ? s[k] : ""); }
			o.addc ('\n');
		}
		char p[128]; path_of (f, "index.tsv", p, sizeof p);
		kapi_save_file (p, o.c (), (unsigned) o.n);
		f.dirty = false;
	}
	static void count (Folder &f) { f.unread = 0; for (int i = 0; i < f.msgs.n; i++) if (!(f.msgs[i].flags & F_SEEN)) f.unread++; }
	Msg *msg (Folder &f, long uid) { for (int i = 0; i < f.msgs.n; i++) if (f.msgs[i].uid == uid) return &f.msgs[i]; return 0; }
	void remove (Folder &f, long uid)
	{
		for (int i = 0; i < f.msgs.n; i++)
			if (f.msgs[i].uid == uid)
			{
				msg_free (f.msgs[i]);
				for (int k = i; k < f.msgs.n - 1; k++) f.msgs[k] = f.msgs[k + 1];
				f.msgs.n--;
				char p[128], nm[32]; snprintf (nm, sizeof nm, "%ld.eml", uid); path_of (f, nm, p, sizeof p); kapi_remove (p);
				f.dirty = true;
				break;
			}
		count (f);
	}
	// a new message in its place (by uid)
	Msg &insert (Folder &f, const Msg &m)
	{
		Msg &slot = f.msgs.push ();
		int i = f.msgs.n - 1;
		while (i > 0 && f.msgs[i - 1].uid > m.uid) { f.msgs[i] = f.msgs[i - 1]; i--; }
		f.msgs[i] = m;
		(void) slot;
		f.dirty = true;
		return f.msgs[i];
	}
	void drop_all (Folder &f)
	{
		for (int i = 0; i < f.msgs.n; i++) { char p[128], nm[32]; snprintf (nm, sizeof nm, "%ld.eml", f.msgs[i].uid); path_of (f, nm, p, sizeof p); kapi_remove (p); msg_free (f.msgs[i]); }
		f.msgs.n = 0; f.dirty = true; count (f);
	}
	// the message whole, if fetched (malloc'd)
	char *body (const Folder &f, long uid, int *len) const { char p[128], nm[32]; snprintf (nm, sizeof nm, "%ld.eml", uid); path_of (f, nm, p, sizeof p); return file_read (p, len); }
	void keep_body (const Folder &f, long uid, const char *data, int n) const
	{
		char d[96]; snprintf (d, sizeof d, "%s/%s", root, f.dir); mkdirs (d);
		char p[128], nm[32]; snprintf (nm, sizeof nm, "%ld.eml", uid); path_of (f, nm, p, sizeof p);
		kapi_save_file (p, data, (unsigned) n);
	}
	long max_uid (const Folder &f) const { long m = 0; for (int i = 0; i < f.msgs.n; i++) if (f.msgs[i].uid > m) m = f.msgs[i].uid; return m; }
	long min_uid (const Folder &f) const { long m = 0; for (int i = 0; i < f.msgs.n; i++) if (!m || f.msgs[i].uid < m) m = f.msgs[i].uid; return m; }
};

// a message read from its bytes (POP3, a .eml opened): what an Envelope says
static void envelope_of (const char *raw, int n, Envelope &e)
{
	memset (&e, 0, sizeof e);
	Mime M; M.parse (raw, n);
	Buf v;
	M.header ("Subject", v); scpy (e.subject, v.c (), sizeof e.subject);
	Addr a[32]; char t[600];
	struct { const char *h; char *out; int cap; } A[] = { { "From", e.from, sizeof e.from }, { "To", e.to, sizeof e.to }, { "Cc", e.cc, sizeof e.cc }, { "Reply-To", e.replyTo, sizeof e.replyTo } };
	for (int k = 0; k < 4; k++)
	{
		M.header_raw (A[k].h, v);
		int na = parse_addrs (v.c (), a, 32);
		Buf o; for (int i = 0; i < na; i++) { if (o.n) o.add (", "); if (a[i].name[0]) o.addf ("\"%s\" <%s>", a[i].name, a[i].email); else o.add (a[i].email); }
		scpy (A[k].out, o.c (), A[k].cap);
	}
	(void) t;
	M.header_raw ("Date", v); e.date = parse_date (v.c ());
	M.header_raw ("Message-ID", v); scpy (e.msgid, v.c (), sizeof e.msgid);
	M.header_raw ("In-Reply-To", v); scpy (e.inReplyTo, v.c (), sizeof e.inReplyTo);
	M.header_raw ("References", v); scpy (e.refs, v.c (), sizeof e.refs);
	e.size = n;
	int at[4]; e.attach = M.attachments (at, 4) > 0;
	int b = M.body_part (false);
	if (b >= 0) { e.textHtml = ieq (M.parts[b].sub, "html"); }
}
// its preview from the bytes
static void preview_from_raw (const char *raw, int n, char *out, int cap)
{
	Mime M; M.parse (raw, n);
	int b = M.body_part (false);
	out[0] = 0;
	if (b < 0) return;
	const Part &p = M.parts[b];
	int len = p.bn < 4000 ? p.bn : 4000;
	preview_of (p.body, len, p.enc, p.charset, ieq (p.sub, "html"), out, cap);
}

} // namespace mailapp

#endif
