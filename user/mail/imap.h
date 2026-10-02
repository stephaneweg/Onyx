//
// mail/imap.h -- an IMAP4rev1 client (RFC 3501; IMAP4rev2's habits, RFC 9051): the login (LOGIN, AUTHENTICATE PLAIN,
// AUTHENTICATE XOAUTH2 -- Gmail, Outlook), the folders (LIST, their special use -- RFC 6154 --, modified UTF-7 names),
// SELECT, the messages' envelopes, flags, sizes, structure (UID FETCH), a part's first bytes (the previews), a whole
// message, the flags changed (UID STORE), moved (UID MOVE, else COPY + \Deleted + EXPUNGE), a message added (APPEND:
// Sent, Drafts), IDLE (new mail at once). The responses are read whole (their literals in place) and parsed into a
// small tree (Val). Blocking: Mail's worker thread. Part of Onyx's mail (docs/mail/README.md).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See mail/util.h for the full notice.
//
#ifndef ONYX_MAIL_IMAP_H
#define ONYX_MAIL_IMAP_H

#include "mail/conn.h"

namespace mail {

// ---- a response's values ----------------------------------------------------------------------------------------------
enum { V_ATOM, V_STR, V_NIL, V_LIST };
struct Val { int type; const char *s; int n; int kid, nkid; };	// (a list: its kids are vals[kid .. kid + nkid - 1])
struct Resp
{
	Buf raw;				// the response as it came (its literals in place)
	Val *v; int nv, cap;			// the values (v[0]: the top list)
	Resp () : v (0), nv (0), cap (0) {}
	~Resp () { free (v); }
	int add (int type, const char *s, int n) { if (nv == cap) { cap = cap ? cap * 2 : 64; v = (Val *) realloc (v, sizeof (Val) * cap); } v[nv] = Val { type, s, n, 0, 0 }; return nv++; }
	// the tree: each list's kids re-laid contiguous at the end of v once parsed
	void parse_kids (const char *&p, const char *end, char close, int *first, int *cnt)
	{
		// the items parsed at this level, their ids collected, then re-laid contiguous at the end of v
		int ids[512]; int k = 0;
		while (p < end)
		{
			while (p < end && *p == ' ') p++;
			if (p >= end) break;
			if (*p == close) { p++; break; }
			int before = nv;
			parse_one (p, end, close);
			if (nv > before && k < 512) ids[k++] = before;
		}
		*first = nv; *cnt = k;
		for (int i = 0; i < k; i++) { add (0, 0, 0); v[*first + i] = v[ids[i]]; }
	}
	void parse_one (const char *&p, const char *end, char close)
	{
		if (*p == '(' || *p == '[')
		{
			char cl = *p == '(' ? ')' : ']'; p++;
			int id = add (V_LIST, 0, 0); int first, cnt; parse_kids (p, end, cl, &first, &cnt); v[id].kid = first; v[id].nkid = cnt;
		}
		else if (*p == '"')
		{
			p++; char *w = (char *) p; const char *s = p;
			while (p < end && *p != '"') { if (*p == '\\' && p + 1 < end) p++; *w++ = *p++; }
			add (V_STR, s, (int) (w - s)); if (p < end) p++;
		}
		else if (*p == '{')
		{
			long n = atol (p + 1); while (p < end && *p != '\n') p++; if (p < end) p++;
			if (p + n > end) n = end - p;
			add (V_STR, p, (int) n); p += n;
		}
		else
		{
			const char *s = p; int br = 0;
			while (p < end && (br || (*p != ' ' && *p != ')' && *p != close))) { if (*p == '[') br++; else if (*p == ']' && br) br--; p++; }
			if (p == s) { p++; return; }
			if (p - s == 3 && ieqn (s, "NIL", 3)) add (V_NIL, s, 3); else add (V_ATOM, s, (int) (p - s));
		}
	}
	// the whole response parsed: v[top] a list of its items (after "* ")
	int top;
	void parse (int from)
	{
		nv = 0;
		const char *p = raw.p + from, *end = raw.p + raw.n;
		top = add (V_LIST, 0, 0); int first, cnt; parse_kids (p, end, 0, &first, &cnt); v[top].kid = first; v[top].nkid = cnt;
	}
	const Val &kid (const Val &l, int i) const { static Val nil = { V_NIL, "", 0, 0, 0 }; return l.type == V_LIST && i < l.nkid ? v[l.kid + i] : nil; }
	const Val &item (int i) const { return kid (v[top], i); }
	int items () const { return v[top].nkid; }
	static bool is (const Val &a, const char *w) { int n = (int) strlen (w); return (a.type == V_ATOM || a.type == V_STR) && a.n == n && ieqn (a.s, w, n); }
	static long num (const Val &a) { return a.type == V_ATOM || a.type == V_STR ? atol (a.s) : 0; }
	static void str (const Val &a, char *out, int cap) { if (a.type == V_NIL || a.type == V_LIST) { if (cap) out[0] = 0; return; } int n = a.n < cap - 1 ? a.n : cap - 1; memcpy (out, a.s, n); out[n] = 0; }
	// a list's value after a key (FETCH's "UID 12 FLAGS (...)" pairs)
	const Val *after (const Val &l, const char *key) const
	{
		for (int i = 0; i + 1 < l.nkid; i++) if (is (v[l.kid + i], key)) return &v[l.kid + i + 1];
		return 0;
	}
};

// ---- modified UTF-7 (RFC 3501 §5.1.3) <-> UTF-8 -------------------------------------------------------------------------
static void mutf7_decode (Buf &o, const char *s)
{
	while (*s)
	{
		if (*s != '&') { o.addc (*s++); continue; }
		s++;
		if (*s == '-') { o.addc ('&'); s++; continue; }
		unsigned acc = 0; int bits = 0; unsigned hi = 0;
		while (*s && *s != '-')
		{
			int c = *s++, v = c == ',' ? 63 : b64v (c);
			if (v < 0) continue;
			acc = acc << 6 | (unsigned) v; bits += 6;
			if (bits >= 16)
			{
				bits -= 16; unsigned u = acc >> bits & 0xFFFF;
				if (u >= 0xD800 && u < 0xDC00) hi = u;
				else { if (hi && u >= 0xDC00 && u < 0xE000) { u = 0x10000 + ((hi - 0xD800) << 10) + (u - 0xDC00); hi = 0; } char t[4]; o.add (t, u8put (t, u)); }
			}
		}
		if (*s == '-') s++;
	}
}
static void mutf7_encode (Buf &o, const char *s)
{
	static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";
	while (*s)
	{
		unsigned char c = (unsigned char) *s;
		if (c >= 0x20 && c < 0x7F) { if (c == '&') o.add ("&-"); else o.addc ((char) c); s++; continue; }
		o.addc ('&');
		unsigned acc = 0; int bits = 0;
		while (*s && ((unsigned char) *s < 0x20 || (unsigned char) *s >= 0x7F))
		{
			unsigned u = u8get (s);
			unsigned us[2]; int k = 0;
			if (u >= 0x10000) { u -= 0x10000; us[k++] = 0xD800 + (u >> 10); us[k++] = 0xDC00 + (u & 0x3FF); } else us[k++] = u;
			for (int j = 0; j < k; j++) { acc = acc << 16 | us[j]; bits += 16; while (bits >= 6) { bits -= 6; o.addc (T[acc >> bits & 63]); } }
		}
		if (bits) o.addc (T[acc << (6 - bits) & 63]);
		o.addc ('-');
	}
}

// ---- what IMAP tells of a folder, a message ---------------------------------------------------------------------------------
enum { SP_NONE, SP_INBOX, SP_SENT, SP_DRAFTS, SP_TRASH, SP_JUNK, SP_ARCHIVE, SP_ALL, SP_FLAGGED };
struct ImapFolder { char name[200]; char show[200]; char delim; int special; bool noselect; };
struct SelectInfo { long exists, uidvalidity, uidnext; };
struct Envelope
{
	long uid; unsigned flags;		// F_* below
	long long date; long size;
	char subject[300], from[300], to[600], cc[600], replyTo[300], msgid[200], inReplyTo[200], refs[1000];
	unsigned long long gmThread;		// Gmail's X-GM-THRID (0: none)
	bool attach;
	char textPart[24]; char textEnc[24]; char textCs[24]; bool textHtml;	// the part that gives the preview
};
enum { F_SEEN = 1, F_ANSWERED = 2, F_FLAGGED = 4, F_DELETED = 8, F_DRAFT = 16, F_FORWARDED = 32 };
static unsigned flags_of (const Resp &r, const Val &l)
{
	unsigned f = 0;
	for (int i = 0; i < l.nkid; i++)
	{
		const Val &a = r.kid (l, i);
		if (Resp::is (a, "\\Seen")) f |= F_SEEN; else if (Resp::is (a, "\\Answered")) f |= F_ANSWERED; else if (Resp::is (a, "\\Flagged")) f |= F_FLAGGED;
		else if (Resp::is (a, "\\Deleted")) f |= F_DELETED; else if (Resp::is (a, "\\Draft")) f |= F_DRAFT; else if (Resp::is (a, "$Forwarded")) f |= F_FORWARDED;
	}
	return f;
}
// an ENVELOPE's address list -> "Name <a@b>, ..."
static void env_addrs (const Resp &r, const Val &l, char *out, int cap)
{
	Buf o;
	for (int i = 0; l.type == V_LIST && i < l.nkid; i++)
	{
		const Val &a = r.kid (l, i);
		char nm[200], mb[120], hs[120]; Resp::str (r.kid (a, 0), nm, sizeof nm); Resp::str (r.kid (a, 2), mb, sizeof mb); Resp::str (r.kid (a, 3), hs, sizeof hs);
		if (!hs[0]) continue;					// (a group's start / end)
		if (o.n) o.add (", ");
		if (nm[0]) { Buf d; decode_header (d, nm); o.addf ("\"%s\" <%s@%s>", d.c (), mb, hs); } else o.addf ("%s@%s", mb, hs);
	}
	scpy (out, o.c (), cap);
}
// BODYSTRUCTURE: an attachment somewhere? the part to preview (text/plain first, else text/html)
static void body_scan (const Resp &r, const Val &b, const char *path, Envelope &e, int depth)
{
	if (b.type != V_LIST || depth > 8) return;
	if (b.nkid && r.kid (b, 0).type == V_LIST)
	{	// multipart: its parts, then the subtype
		int part = 1;
		for (int i = 0; i < b.nkid && r.kid (b, i).type == V_LIST; i++, part++)
		{
			char p[24]; snprintf (p, sizeof p, path[0] ? "%s.%d" : "%s%d", path, part);
			body_scan (r, r.kid (b, i), p, e, depth + 1);
		}
		return;
	}
	char type[32], sub[32]; Resp::str (r.kid (b, 0), type, sizeof type); Resp::str (r.kid (b, 1), sub, sizeof sub);
	// the extension data's disposition: ("attachment" (...)) after md5 (text: after the lines; message/rfc822: after
	// its envelope, body and lines) -- any list among the extension data whose first word is "attachment" / "inline"
	bool att = false, inl = false;
	for (int i = 7; i < b.nkid; i++)
	{
		const Val &x = r.kid (b, i);
		if (x.type == V_LIST && x.nkid >= 2 && r.kid (x, 1).type != V_ATOM) { if (Resp::is (r.kid (x, 0), "attachment")) att = true; else if (Resp::is (r.kid (x, 0), "inline")) inl = true; }
	}
	bool text = ieq (type, "text") && (ieq (sub, "plain") || ieq (sub, "html"));
	bool named = false;
	const Val &prm = r.kid (b, 2);
	for (int i = 0; i + 1 < prm.nkid; i += 2) if (Resp::is (r.kid (prm, i), "name")) named = true;
	bool cid = r.kid (b, 3).type == V_STR;					// (a picture the HTML shows: cid:)
	// an attachment: said so; or not a text the message shows -- except the HTML's own pictures (a Content-ID, inline)
	if (!att && !text)
	{
		if (ieq (type, "image") && cid && !named) att = false;
		else if (ieq (type, "image") && cid && inl) att = false;
		else att = true;
	}
	if (text && named && !inl) att = true;					// (a .txt / .html attached)
	if (att) e.attach = true;
	if (text && !att && (!e.textPart[0] || (e.textHtml && ieq (sub, "plain"))))
	{
		scpy (e.textPart, path[0] ? path : "1", sizeof e.textPart);
		char enc[24]; Resp::str (r.kid (b, 5), enc, sizeof enc); scpy (e.textEnc, enc, sizeof e.textEnc);
		e.textCs[0] = 0;
		for (int i = 0; i + 1 < prm.nkid; i += 2) if (Resp::is (r.kid (prm, i), "charset")) Resp::str (r.kid (prm, i + 1), e.textCs, sizeof e.textCs);
		e.textHtml = ieq (sub, "html");
	}
}

// ---- the client ------------------------------------------------------------------------------------------------------------------
struct Imap
{
	Conn c; int tagn; bool loggedIn;
	char caps[1024];			// CAPABILITY's atoms, space-separated, upper case
	char err[300];
	// what came untagged while a command ran: called with each response (r.items (): its words)
	typedef void (*Untagged) (void *ctx, Resp &r);

	Imap () : tagn (0), loggedIn (false) { caps[0] = err[0] = 0; }
	bool fail (const char *m) { scpy (err, m, sizeof err); return false; }
	bool has (const char *cap) const
	{
		int n = (int) strlen (cap);
		for (const char *p = caps; *p; ) { while (*p == ' ') p++; const char *s = p; while (*p && *p != ' ') p++; if (p - s == n && ieqn (s, cap, n)) return true; }
		return false;
	}
	void caps_from (const char *s)
	{	// "* CAPABILITY IMAP4rev1 ..." or "[CAPABILITY ...]"
		const char *p = ifind (s, "CAPABILITY"); if (!p) return;
		p += 10; int k = 0;
		while (*p && *p != ']' && k < (int) sizeof caps - 2) { caps[k++] = (char) (*p >= 'a' && *p <= 'z' ? *p - 32 : *p); p++; }
		caps[k] = 0;
	}

	// one response: a line and its literals -> r.raw; false when the connection broke
	bool read_resp (Resp &r)
	{
		r.raw.clear (); Buf ln;
		for (;;)
		{
			if (!c.line (ln)) return fail (c.err);
			r.raw.add (ln.c (), ln.n);
			// a literal at the end: {n} or {n+}
			if (ln.n >= 3 && ln.p[ln.n - 1] == '}')
			{
				int b = ln.n - 2; while (b > 0 && ((ln.p[b] >= '0' && ln.p[b] <= '9') || ln.p[b] == '+')) b--;
				if (ln.p[b] == '{')
				{
					long n = atol (ln.p + b + 1);
					r.raw.add ("\r\n");
					if (!c.bytes (r.raw, n)) return fail (c.err);
					continue;
				}
			}
			return true;
		}
	}
	// a command: tagged, its untagged responses to cb, its end -> true when OK (err: the server's words otherwise)
	bool cmd (Untagged cb, void *ctx, const char *fmt, ...)
	{
		char tag[16]; snprintf (tag, sizeof tag, "A%03d", ++tagn);
		char line[4096]; va_list a; va_start (a, fmt); int k = vsnprintf (line, sizeof line, fmt, a); va_end (a);
		if (k >= (int) sizeof line) return fail ("A command too long.");
		Buf out; out.addf ("%s %s\r\n", tag, line);
		if (!c.send (out.c (), out.n)) return fail (c.err);
		return wait (tag, cb, ctx);
	}
	bool wait (const char *tag, Untagged cb, void *ctx)
	{
		int tl = (int) strlen (tag);
		for (;;)
		{
			Resp r;
			if (!read_resp (r)) return false;
			if (r.raw.n >= tl + 1 && !memcmp (r.raw.p, tag, tl) && r.raw.p[tl] == ' ')
			{
				const char *st = r.raw.p + tl + 1;
				if (istarts (st, "OK")) { if (ifind (st, "[CAPABILITY")) caps_from (st); return true; }
				const char *m = st; while (*m && *m != ' ') m++; while (*m == ' ') m++;
				if (*m == '[') { while (*m && *m != ']') m++; if (*m) m++; while (*m == ' ') m++; }
				return fail (*m ? m : "The server refused.");
			}
			if (r.raw.n >= 2 && r.raw.p[0] == '*')
			{
				if (istarts (r.raw.p + 2, "CAPABILITY")) caps_from (r.raw.p);
				if (cb) { r.parse (2); cb (ctx, r); }
			}
			// ("+ " continuation: not expected here)
		}
	}

	// ---- connecting -------------------------------------------------------------------------------------------------------
	// user + password, or user + an OAuth access token (XOAUTH2)
	bool connect (const char *host, int port, int sec, const char *user, const char *secret, bool oauth)
	{
		loggedIn = false; caps[0] = 0;
		if (!c.open (host, port, sec)) return fail (c.err);
		Resp g; if (!read_resp (g)) return false;
		if (!istarts (g.raw.p, "* OK") && !istarts (g.raw.p, "* PREAUTH")) return fail ("This is not an IMAP server.");
		caps_from (g.raw.p);
		if (!caps[0] && !cmd (0, 0, "CAPABILITY")) return false;
		if (sec == SEC_STARTTLS)
		{
			if (!has ("STARTTLS")) return fail ("The server offers no STARTTLS (choose SSL/TLS).");
			if (!cmd (0, 0, "STARTTLS")) return false;
			if (!c.starttls ()) return fail (c.err);
			caps[0] = 0; if (!cmd (0, 0, "CAPABILITY")) return false;
		}
		bool ok;
		if (oauth)
		{
			Buf s; s.addf ("user=%s\001auth=Bearer %s\001\001", user, secret);
			Buf b; b64_encode (b, s.c (), s.n, 0);
			ok = cmd_sasl ("AUTHENTICATE XOAUTH2", b.c ());
		}
		else if (has ("AUTH=PLAIN") && !has ("LOGINDISABLED"))
		{
			Buf s; s.addc (0); s.add (user); s.addc (0); s.add (secret);
			Buf b; b64_encode (b, s.p, s.n, 0);
			ok = cmd_sasl ("AUTHENTICATE PLAIN", b.c ());
		}
		else
		{
			Buf q1, q2; quote (q1, user); quote (q2, secret);
			ok = cmd (0, 0, "LOGIN %s %s", q1.c (), q2.c ());
		}
		if (!ok) { char m[300]; snprintf (m, sizeof m, "The sign-in was refused: %s", err); scpy (err, m, sizeof err); return false; }
		loggedIn = true;
		if (!has ("IMAP4REV1") && !has ("IMAP4REV2")) { caps[0] = 0; cmd (0, 0, "CAPABILITY"); }
		return true;
	}
	// AUTHENTICATE with its initial response (SASL-IR) or after the "+" (and an empty reply to an error's "+")
	bool cmd_sasl (const char *mech, const char *b64)
	{
		char tag[16]; snprintf (tag, sizeof tag, "A%03d", ++tagn);
		Buf out;
		if (has ("SASL-IR")) out.addf ("%s %s %s\r\n", tag, mech, b64); else out.addf ("%s %s\r\n", tag, mech);
		if (!c.send (out.c (), out.n)) return fail (c.err);
		bool sentData = has ("SASL-IR");
		for (;;)
		{
			Resp r; if (!read_resp (r)) return false;
			if (r.raw.n && r.raw.p[0] == '+')
			{
				if (!sentData) { Buf d; d.addf ("%s\r\n", b64); if (!c.send (d.c (), d.n)) return fail (c.err); sentData = true; }
				else if (!c.send ("\r\n")) return fail (c.err);		// (XOAUTH2's error: answered empty)
				continue;
			}
			int tl = (int) strlen (tag);
			if (r.raw.n > tl && !memcmp (r.raw.p, tag, tl))
			{
				const char *st = r.raw.p + tl + 1;
				if (istarts (st, "OK")) { if (ifind (st, "[CAPABILITY")) caps_from (st); return true; }
				const char *m = st; while (*m && *m != ' ') m++; while (*m == ' ') m++;
				if (*m == '[') { while (*m && *m != ']') m++; if (*m) m++; while (*m == ' ') m++; }
				return fail (*m ? m : "refused");
			}
			if (r.raw.n >= 2 && r.raw.p[0] == '*' && istarts (r.raw.p + 2, "CAPABILITY")) caps_from (r.raw.p);
		}
	}
	static void quote (Buf &o, const char *s)
	{
		o.addc ('"');
		for (; *s; s++) { if (*s == '"' || *s == '\\') o.addc ('\\'); o.addc (*s); }
		o.addc ('"');
	}
	void logout () { if (c.open_) { cmd (0, 0, "LOGOUT"); c.close (); } loggedIn = false; }

	// ---- folders ------------------------------------------------------------------------------------------------------------
	struct ListCtx { ImapFolder *f; int n, cap; };
	static void on_list (void *ctx, Resp &r)
	{
		ListCtx &L = *(ListCtx *) ctx;
		if (!Resp::is (r.item (0), "LIST") || r.items () < 4) return;
		if (L.n == L.cap) { L.cap = L.cap ? L.cap * 2 : 32; L.f = (ImapFolder *) realloc (L.f, sizeof (ImapFolder) * L.cap); }
		ImapFolder &f = L.f[L.n]; memset (&f, 0, sizeof f);
		const Val &fl = r.item (1);
		for (int i = 0; i < fl.nkid; i++)
		{
			const Val &a = r.kid (fl, i);
			if (Resp::is (a, "\\Noselect") || Resp::is (a, "\\NonExistent")) f.noselect = true;
			else if (Resp::is (a, "\\Sent")) f.special = SP_SENT; else if (Resp::is (a, "\\Drafts")) f.special = SP_DRAFTS;
			else if (Resp::is (a, "\\Trash")) f.special = SP_TRASH; else if (Resp::is (a, "\\Junk")) f.special = SP_JUNK;
			else if (Resp::is (a, "\\Archive")) f.special = SP_ARCHIVE; else if (Resp::is (a, "\\All")) f.special = SP_ALL;
			else if (Resp::is (a, "\\Flagged")) f.special = SP_FLAGGED;
		}
		char d[8]; Resp::str (r.item (2), d, sizeof d); f.delim = d[0];
		Resp::str (r.item (3), f.name, sizeof f.name);
		if (ieq (f.name, "INBOX")) { f.special = SP_INBOX; scpy (f.name, "INBOX", sizeof f.name); }
		Buf s; mutf7_decode (s, f.name);
		// what to show: the last part of the path ("[Gmail]/Sent Mail" -> "Sent Mail")
		const char *show = s.c ();
		if (f.delim) { const char *q = strrchr (show, f.delim); if (q && q[1]) show = q + 1; }
		scpy (f.show, f.special == SP_INBOX ? "Inbox" : show, sizeof f.show);
		// no special-use flag: the usual names
		if (!f.special)
		{
			static const struct { const char *n; int sp; } N[] = { { "Sent", SP_SENT }, { "Sent Items", SP_SENT }, { "Sent Messages", SP_SENT }, { "Sent Mail", SP_SENT }, { "Envoyés", SP_SENT },
				{ "Drafts", SP_DRAFTS }, { "Brouillons", SP_DRAFTS }, { "Trash", SP_TRASH }, { "Deleted Items", SP_TRASH }, { "Deleted Messages", SP_TRASH }, { "Corbeille", SP_TRASH },
				{ "Junk", SP_JUNK }, { "Spam", SP_JUNK }, { "Junk E-mail", SP_JUNK }, { "Archive", SP_ARCHIVE }, { "Archives", SP_ARCHIVE } };
			for (unsigned k = 0; k < sizeof N / sizeof N[0]; k++) if (ieq (f.show, N[k].n)) f.special = N[k].sp;
		}
		L.n++;
	}
	// the folders (malloc'd: free ())
	bool list (ImapFolder **out, int *n)
	{
		ListCtx L = { 0, 0, 0 };
		bool ok = has ("SPECIAL-USE") ? cmd (on_list, &L, "LIST \"\" \"*\" RETURN (SPECIAL-USE)") : cmd (on_list, &L, "LIST \"\" \"*\"");
		if (!ok && L.n == 0) ok = cmd (on_list, &L, "LIST \"\" \"*\"");
		*out = L.f; *n = L.n;
		return ok;
	}

	// ---- a folder opened -----------------------------------------------------------------------------------------------------
	static void on_select (void *ctx, Resp &r)
	{
		SelectInfo &s = *(SelectInfo *) ctx;
		if (r.items () >= 2 && Resp::is (r.item (1), "EXISTS")) s.exists = Resp::num (r.item (0));
		const char *u = ifind (r.raw.c (), "[UIDVALIDITY "); if (u) s.uidvalidity = atol (u + 13);
		u = ifind (r.raw.c (), "[UIDNEXT "); if (u) s.uidnext = atol (u + 9);
	}
	char selected[200];
	bool select (const char *name, SelectInfo *si, bool readOnly = false)
	{
		SelectInfo s = { 0, 0, 0 };
		Buf q; Buf enc; mutf7_encode (enc, name); quote (q, enc.c ());
		if (!cmd (on_select, &s, "%s %s", readOnly ? "EXAMINE" : "SELECT", q.c ())) return false;
		scpy (selected, name, sizeof selected);
		if (si) *si = s;
		return true;
	}

	// ---- the messages' facts -------------------------------------------------------------------------------------------------
	typedef void (*OnEnvelope) (void *ctx, const Envelope &e);
	struct FetchCtx { OnEnvelope cb; void *ctx; };
	static void on_fetch_env (void *ctx, Resp &r)
	{
		FetchCtx &F = *(FetchCtx *) ctx;
		if (r.items () < 3 || !Resp::is (r.item (1), "FETCH")) return;
		const Val &l = r.item (2);
		Envelope e; memset (&e, 0, sizeof e);
		const Val *v;
		if ((v = r.after (l, "UID"))) e.uid = Resp::num (*v);
		if (!e.uid) return;
		if ((v = r.after (l, "FLAGS"))) e.flags = flags_of (r, *v);
		if ((v = r.after (l, "RFC822.SIZE"))) e.size = Resp::num (*v);
		if ((v = r.after (l, "X-GM-THRID"))) e.gmThread = strtoull (v->s, 0, 10);
		if ((v = r.after (l, "INTERNALDATE"))) { char d[64]; Resp::str (*v, d, sizeof d); e.date = parse_date (d); }
		if ((v = r.after (l, "ENVELOPE")))
		{
			char t[400]; Resp::str (r.kid (*v, 0), t, sizeof t); long long d = parse_date (t); if (d) e.date = d;
			Resp::str (r.kid (*v, 1), t, sizeof t); Buf s; decode_header (s, t); scpy (e.subject, s.c (), sizeof e.subject);
			env_addrs (r, r.kid (*v, 2), e.from, sizeof e.from);
			env_addrs (r, r.kid (*v, 4), e.replyTo, sizeof e.replyTo);
			env_addrs (r, r.kid (*v, 5), e.to, sizeof e.to);
			env_addrs (r, r.kid (*v, 6), e.cc, sizeof e.cc);
			Resp::str (r.kid (*v, 8), e.inReplyTo, sizeof e.inReplyTo);
			Resp::str (r.kid (*v, 9), e.msgid, sizeof e.msgid);
		}
		if ((v = r.after (l, "BODYSTRUCTURE"))) body_scan (r, *v, "", e, 0);
		// the References header (BODY[HEADER.FIELDS (REFERENCES)] -> its text)
		for (int i = 0; i + 1 < l.nkid; i++)
		{
			const Val &k = r.kid (l, i);
			if (k.type == V_ATOM && ieqn (k.s, "BODY[HEADER.FIELDS", 18))
			{
				char h[1200]; Resp::str (r.kid (l, i + 1), h, sizeof h);
				const char *p = ifind (h, "References:");
				if (p) { p += 11; Buf u; for (; *p; p++) if (*p != '\r' && *p != '\n') u.addc (*p == '\t' ? ' ' : *p); scpy (e.refs, u.c (), sizeof e.refs); }
			}
		}
		F.cb (F.ctx, e);
	}
	// the messages uid from..to (to 0: '*'): their envelope, flags, size, structure, References
	bool fetch_envelopes (long from, long to, OnEnvelope cb, void *ctx)
	{
		FetchCtx F = { cb, ctx };
		char range[48]; if (to > 0) snprintf (range, sizeof range, "%ld:%ld", from, to); else snprintf (range, sizeof range, "%ld:*", from);
		const char *gm = has ("X-GM-EXT-1") ? " X-GM-THRID" : "";
		return cmd (on_fetch_env, &F, "UID FETCH %s (UID FLAGS INTERNALDATE RFC822.SIZE ENVELOPE BODYSTRUCTURE BODY.PEEK[HEADER.FIELDS (REFERENCES)]%s)", range, gm);
	}
	// the flags only (a resync: what changed elsewhere)
	typedef void (*OnFlags) (void *ctx, long uid, unsigned flags);
	struct FlagsCtx { OnFlags cb; void *ctx; };
	static void on_flags (void *ctx, Resp &r)
	{
		FlagsCtx &F = *(FlagsCtx *) ctx;
		if (r.items () < 3 || !Resp::is (r.item (1), "FETCH")) return;
		const Val &l = r.item (2); const Val *u = r.after (l, "UID"), *f = r.after (l, "FLAGS");
		if (u && f) F.cb (F.ctx, Resp::num (*u), flags_of (r, *f));
	}
	bool fetch_flags (long from, OnFlags cb, void *ctx)
	{
		FlagsCtx F = { cb, ctx };
		return cmd (on_flags, &F, "UID FETCH %ld:* (UID FLAGS)", from < 1 ? 1 : from);
	}
	// a section of a message (part "1.2", "" = the whole message), the first `max` bytes (0: all) -> out
	struct BodyCtx { Buf *out; bool got; };
	static void on_body (void *ctx, Resp &r)
	{
		BodyCtx &B = *(BodyCtx *) ctx;
		if (r.items () < 3 || !Resp::is (r.item (1), "FETCH")) return;
		const Val &l = r.item (2);
		for (int i = 0; i + 1 < l.nkid; i++)
		{
			const Val &k = r.kid (l, i);
			if (k.type == V_ATOM && (ieqn (k.s, "BODY[", 5) || ieqn (k.s, "RFC822", 6)))
			{
				const Val &d = r.kid (l, i + 1);
				if (d.type == V_STR) { B.out->add (d.s, d.n); B.got = true; }
			}
		}
	}
	bool fetch_body (long uid, const char *part, long max, Buf &out)
	{
		BodyCtx B = { &out, false };
		char sec[64]; snprintf (sec, sizeof sec, "BODY.PEEK[%s]", part ? part : "");
		bool ok = max > 0 ? cmd (on_body, &B, "UID FETCH %ld (%s<0.%ld>)", uid, sec, max) : cmd (on_body, &B, "UID FETCH %ld (%s)", uid, sec);
		if (ok && !B.got) return fail ("The message is not there any more.");
		return ok;
	}

	// ---- changes ---------------------------------------------------------------------------------------------------------------
	// "+FLAGS (\Seen)" / "-FLAGS (\Flagged)" on a set of uids ("12,15:17")
	bool store (const char *uids, const char *change) { return cmd (0, 0, "UID STORE %s %s", uids, change); }
	bool move (const char *uids, const char *dest)
	{
		Buf q, enc; mutf7_encode (enc, dest); quote (q, enc.c ());
		if (has ("MOVE")) return cmd (0, 0, "UID MOVE %s %s", uids, q.c ());
		if (!cmd (0, 0, "UID COPY %s %s", uids, q.c ())) return false;
		if (!store (uids, "+FLAGS.SILENT (\\Deleted)")) return false;
		return has ("UIDPLUS") ? cmd (0, 0, "UID EXPUNGE %s", uids) : cmd (0, 0, "EXPUNGE");
	}
	bool expunge (const char *uids)
	{
		if (!store (uids, "+FLAGS.SILENT (\\Deleted)")) return false;
		return has ("UIDPLUS") ? cmd (0, 0, "UID EXPUNGE %s", uids) : cmd (0, 0, "EXPUNGE");
	}
	// a message added to a folder (its flags: "(\Seen)", "(\Seen \Draft)")
	bool append (const char *folder, const char *flags, const char *msg, int n)
	{
		Buf q, enc; mutf7_encode (enc, folder); quote (q, enc.c ());
		char tag[16]; snprintf (tag, sizeof tag, "A%03d", ++tagn);
		bool lit = has ("LITERAL+") || has ("LITERAL-");
		Buf out; out.addf ("%s APPEND %s %s {%d%s}\r\n", tag, q.c (), flags ? flags : "()", n, lit ? "+" : "");
		if (!c.send (out.c (), out.n)) return fail (c.err);
		if (!lit)
		{
			Resp r; if (!read_resp (r)) return false;
			if (!r.raw.n || r.raw.p[0] != '+') return fail ("The server refused the message.");
		}
		if (!c.send (msg, n) || !c.send ("\r\n")) return fail (c.err);
		return wait (tag, 0, 0);
	}
	// IDLE: wait (at most ms) for the server to say something changed -> true when it did
	bool idle (unsigned ms, bool *changed)
	{
		*changed = false;
		if (!has ("IDLE")) return false;
		char tag[16]; snprintf (tag, sizeof tag, "A%03d", ++tagn);
		Buf out; out.addf ("%s IDLE\r\n", tag);
		if (!c.send (out.c (), out.n)) return fail (c.err);
		Resp r; if (!read_resp (r)) return false;
		if (!r.raw.n || r.raw.p[0] != '+') return fail ("IDLE refused.");
		unsigned keep = c.timeoutMs; c.timeoutMs = ms; c.softTimeout = true;
		Resp u;
		bool got = read_resp (u);
		c.timeoutMs = keep; c.softTimeout = false;
		if (got) *changed = true;
		else if (!c.timedOut || !c.open_) return false;		// (the connection lost; a time-out: nothing new)
		if (!c.send ("DONE\r\n")) return fail (c.err);
		return wait (tag, 0, 0);
	}
};

} // namespace mail

#endif
