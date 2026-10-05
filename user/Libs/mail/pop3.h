//
// mail/pop3.h -- a POP3 client (RFC 1939, RFC 2449's CAPA, RFC 2595's STLS, RFC 5034's AUTH -- PLAIN, XOAUTH2): the
// login, STAT, UIDL (each message's lasting id: what was already taken), TOP (the headers alone), RETR (the whole
// message, its dot-stuffing undone), DELE, QUIT. Blocking: Mail's worker thread. Part of Onyx's mail
// (docs/mail/README.md).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See mail/util.h for the full notice.
//
#ifndef ONYX_MAIL_POP3_H
#define ONYX_MAIL_POP3_H

#include "mail/conn.h"

namespace mail {

struct PopMsg { int num; long size; char uidl[72]; };

struct Pop3
{
	Conn c; char err[300]; char caps[512];
	Pop3 () { err[0] = caps[0] = 0; }
	bool fail (const char *m) { scpy (err, m, sizeof err); return false; }
	bool has (const char *cap) const
	{
		int n = (int) strlen (cap);
		for (const char *p = caps; *p; ) { while (*p == '\n') p++; const char *s = p; while (*p && *p != '\n') p++; if (p - s >= n && ieqn (s, cap, n) && (p - s == n || s[n] == ' ')) return true; }
		return false;
	}
	// a reply's first line: +OK -> true; -ERR -> false (err: the server's words)
	bool status (Buf &ln)
	{
		if (!c.line (ln)) return fail (c.err);
		if (istarts (ln.c (), "+OK")) return true;
		const char *m = ln.c (); if (istarts (m, "-ERR")) m += 4; while (*m == ' ') m++;
		if (*m == '[') { while (*m && *m != ']') m++; if (*m) m++; while (*m == ' ') m++; }		// ([AUTH], [IN-USE]...)
		return fail (*m ? m : "The server refused.");
	}
	bool cmd (Buf &ln, const char *fmt, ...)
	{
		char t[1024]; va_list a; va_start (a, fmt); int k = vsnprintf (t, sizeof t, fmt, a); va_end (a);
		if (k > (int) sizeof t - 3) return fail ("A command too long.");
		t[k++] = '\r'; t[k++] = '\n';
		if (!c.send (t, k)) return fail (c.err);
		return status (ln);
	}
	// a multi-line reply's lines (after its +OK) up to the lone ".", the dot-stuffing undone: CRLF-ended in out
	bool lines (Buf &out)
	{
		Buf ln;
		for (;;)
		{
			if (!c.line (ln)) return fail (c.err);
			if (ln.n == 1 && ln.p[0] == '.') return true;
			const char *s = ln.c (); int n = ln.n;
			if (n && s[0] == '.') { s++; n--; }
			out.add (s, n); out.add ("\r\n");
		}
	}

	bool connect (const char *host, int port, int sec, const char *user, const char *secret, bool oauth)
	{
		Buf ln;
		if (!c.open (host, port, sec)) return fail (c.err);
		if (!status (ln)) return fail ("This is not a POP3 server.");
		capa ();
		if (sec == SEC_STARTTLS)
		{
			if (caps[0] && !has ("STLS")) return fail ("The server offers no STARTTLS (choose SSL/TLS).");
			if (!cmd (ln, "STLS")) return false;
			if (!c.starttls ()) return fail (c.err);
			capa ();
		}
		bool ok;
		if (oauth)
		{
			Buf s; s.addf ("user=%s\001auth=Bearer %s\001\001", user, secret);
			Buf b; b64_encode (b, s.c (), s.n, 0);
			ok = cmd (ln, "AUTH XOAUTH2 %s", b.c ());
		}
		else if (has ("SASL") && ifind (caps, "PLAIN"))
		{
			Buf s; s.addc (0); s.add (user); s.addc (0); s.add (secret);
			Buf b; b64_encode (b, s.p, s.n, 0);
			ok = cmd (ln, "AUTH PLAIN %s", b.c ());
			if (!ok && c.open_) ok = cmd (ln, "USER %s", user) && cmd (ln, "PASS %s", secret);
		}
		else ok = cmd (ln, "USER %s", user) && cmd (ln, "PASS %s", secret);
		if (!ok) { char m[300]; snprintf (m, sizeof m, "The sign-in was refused: %s", err); scpy (err, m, sizeof err); return false; }
		return true;
	}
	void capa ()
	{
		Buf ln, out; caps[0] = 0;
		if (!cmd (ln, "CAPA")) return;			// (an old server: no CAPA -- USER / PASS then)
		if (!lines (out)) return;
		Buf o; for (const char *p = out.c (); *p; p++) if (*p != '\r') o.addc (*p);
		scpy (caps, o.c (), sizeof caps);
	}
	// how many messages, their size in all
	bool stat (int *count, long *size)
	{
		Buf ln; if (!cmd (ln, "STAT")) return false;
		*count = 0; *size = 0; sscanf (ln.c () + 3, "%d %ld", count, size);
		return true;
	}
	// the messages: number, size, UIDL (malloc'd: free ())
	bool list (PopMsg **out, int *n)
	{
		*out = 0; *n = 0;
		Buf ln, body;
		if (!cmd (ln, "LIST") || !lines (body)) return false;
		int cap = 0; PopMsg *m = 0; int k = 0;
		for (const char *p = body.c (); *p; )
		{
			int num; long sz;
			if (sscanf (p, "%d %ld", &num, &sz) == 2)
			{
				if (k == cap) { cap = cap ? cap * 2 : 64; m = (PopMsg *) realloc (m, sizeof (PopMsg) * cap); }
				m[k].num = num; m[k].size = sz; m[k].uidl[0] = 0; k++;
			}
			while (*p && *p != '\n') p++; if (*p) p++;
		}
		// UIDL (optional in RFC 1939, everywhere in fact): without it the message's own Message-ID stands in later
		body.clear ();
		if (cmd (ln, "UIDL") && lines (body))
			for (const char *p = body.c (); *p; )
			{
				int num; char u[72];
				if (sscanf (p, "%d %71s", &num, u) == 2) for (int i = 0; i < k; i++) if (m[i].num == num) { scpy (m[i].uidl, u, sizeof m[i].uidl); break; }
				while (*p && *p != '\n') p++; if (*p) p++;
			}
		*out = m; *n = k;
		return c.open_;
	}
	// the headers and the body's first lines (TOP n k); the whole message (RETR n)
	bool top (int num, int bodyLines, Buf &out) { Buf ln; return cmd (ln, "TOP %d %d", num, bodyLines) && lines (out); }
	bool retr (int num, Buf &out) { Buf ln; return cmd (ln, "RETR %d", num) && lines (out); }
	bool dele (int num) { Buf ln; return cmd (ln, "DELE %d", num); }
	// QUIT: the deletions done (POP3 applies them only now)
	bool quit () { Buf ln; bool ok = c.open_ && cmd (ln, "QUIT"); c.close (); return ok; }
};

} // namespace mail

#endif
