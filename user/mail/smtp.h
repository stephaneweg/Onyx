//
// mail/smtp.h -- an SMTP submission client (RFC 5321, RFC 6409; STARTTLS RFC 3207; AUTH RFC 4954 -- PLAIN, LOGIN,
// XOAUTH2): EHLO, the login, MAIL FROM / RCPT TO / DATA (the dot-stuffing done), QUIT. Blocking: Mail's worker thread.
// Part of Onyx's mail (docs/mail/README.md).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See mail/util.h for the full notice.
//
#ifndef ONYX_MAIL_SMTP_H
#define ONYX_MAIL_SMTP_H

#include "mail/conn.h"

namespace mail {

struct Smtp
{
	Conn c; char err[300]; char ext[1024];		// EHLO's extensions, '\n'-separated, upper case
	long maxSize;
	Smtp () : maxSize (0) { err[0] = ext[0] = 0; }
	bool fail (const char *m) { scpy (err, m, sizeof err); return false; }
	bool has (const char *e) const
	{
		int n = (int) strlen (e);
		for (const char *p = ext; *p; ) { while (*p == '\n') p++; const char *s = p; while (*p && *p != '\n') p++; if (p - s >= n && ieqn (s, e, n) && (p - s == n || s[n] == ' ' || s[n] == '=')) return true; }
		return false;
	}
	// a reply (its lines "250-...", the last "250 ..."): its code; text: the lines' words (without the codes)
	int reply (Buf *text = 0)
	{
		Buf ln; if (text) text->clear ();
		for (;;)
		{
			if (!c.line (ln)) { fail (c.err); return -1; }
			if (ln.n < 3) continue;
			if (text) { if (text->n) text->addc ('\n'); text->add (ln.n > 4 ? ln.p + 4 : "", ln.n > 4 ? ln.n - 4 : 0); }
			if (ln.n == 3 || ln.p[3] != '-') return atoi (ln.c ());
		}
	}
	// a command and its reply: true when the code is `want`'s class (2 -> 2xx, 3 -> 3xx)
	bool cmd (int want, const char *fmt, ...)
	{
		char t[1100]; va_list a; va_start (a, fmt); int k = vsnprintf (t, sizeof t, fmt, a); va_end (a);
		if (k > (int) sizeof t - 3) return fail ("A command too long.");
		t[k++] = '\r'; t[k++] = '\n';
		if (!c.send (t, k)) return fail (c.err);
		Buf txt; int code = reply (&txt);
		if (code < 0) return false;
		if (code / 100 != want) { char m[300]; snprintf (m, sizeof m, "%d %s", code, txt.c ()); for (char *q = m; *q; q++) if (*q == '\n') *q = ' '; return fail (m); }
		return true;
	}
	bool ehlo ()
	{
		const char *me = "onyx.local";
		char t[64]; int k = snprintf (t, sizeof t, "EHLO %s\r\n", me);
		if (!c.send (t, k)) return fail (c.err);
		Buf txt; int code = reply (&txt);
		if (code != 250)
		{	// an old server: HELO
			if (code < 0) return false;
			return cmd (2, "HELO %s", me);
		}
		Buf u; for (const char *p = txt.c (); *p; p++) u.addc ((char) (*p >= 'a' && *p <= 'z' ? *p - 32 : *p));
		const char *nl = strchr (u.c (), '\n');			// (the first line: the server's name)
		scpy (ext, nl ? nl + 1 : "", sizeof ext);
		const char *sz = ifind (ext, "SIZE "); maxSize = sz ? atol (sz + 5) : 0;
		return true;
	}
	bool connect (const char *host, int port, int sec, const char *user, const char *secret, bool oauth)
	{
		if (!c.open (host, port, sec)) return fail (c.err);
		Buf txt; int code = reply (&txt);
		if (code != 220) return fail (code < 0 ? err : "This is not an SMTP server.");
		if (!ehlo ()) return false;
		if (sec == SEC_STARTTLS)
		{
			if (!has ("STARTTLS")) return fail ("The server offers no STARTTLS (choose SSL/TLS).");
			if (!cmd (2, "STARTTLS")) return false;
			if (!c.starttls ()) return fail (c.err);
			if (!ehlo ()) return false;
		}
		if (!user || !user[0]) return true;			// (a server that needs no sign-in)
		bool ok;
		if (oauth)
		{
			Buf s; s.addf ("user=%s\001auth=Bearer %s\001\001", user, secret);
			Buf b; b64_encode (b, s.c (), s.n, 0);
			ok = cmd (2, "AUTH XOAUTH2 %s", b.c ());
			if (!ok && c.open_) { cmd (2, ""); }		// (a 334 error challenge: answered empty)
		}
		else if (has ("AUTH") && !ifind (ext, "PLAIN") && ifind (ext, "LOGIN"))
		{
			Buf u, p; b64_encode (u, user, (int) strlen (user), 0); b64_encode (p, secret, (int) strlen (secret), 0);
			ok = cmd (3, "AUTH LOGIN") && cmd (3, "%s", u.c ()) && cmd (2, "%s", p.c ());
		}
		else
		{
			Buf s; s.addc (0); s.add (user); s.addc (0); s.add (secret);
			Buf b; b64_encode (b, s.p, s.n, 0);
			ok = cmd (2, "AUTH PLAIN %s", b.c ());
		}
		if (!ok) { char m[300]; snprintf (m, sizeof m, "The sign-in was refused: %s", err); scpy (err, m, sizeof err); return false; }
		return true;
	}
	// a message sent: from, its recipients (addresses alone), the message (CRLF lines, headers and body)
	bool send (const char *from, const char *const *rcpt, int nrcpt, const char *msg, int n)
	{
		if (maxSize && n > maxSize) return fail ("The message is too big for this server.");
		if (has ("SIZE") ? !cmd (2, "MAIL FROM:<%s> SIZE=%d", from, n) : !cmd (2, "MAIL FROM:<%s>", from)) return false;
		int good = 0; char bad[300]; bad[0] = 0;
		for (int i = 0; i < nrcpt; i++)
		{
			if (cmd (2, "RCPT TO:<%s>", rcpt[i])) good++;
			else if (!c.open_) return false;
			else if (!bad[0]) snprintf (bad, sizeof bad, "%s: %s", rcpt[i], err);
		}
		if (!good) { cmd (2, "RSET"); return fail (bad[0] ? bad : "No recipient."); }
		if (!cmd (3, "DATA")) return false;
		// the body, its lines' leading dots doubled, CRLF everywhere, then "."
		Buf o; o.reserve (n + n / 50 + 8);
		bool bol = true;
		for (int i = 0; i < n; i++)
		{
			char ch = msg[i];
			if (bol && ch == '.') o.addc ('.');
			if (ch == '\n' && (i == 0 || msg[i - 1] != '\r')) o.addc ('\r');
			o.addc (ch);
			bol = ch == '\n';
		}
		if (!bol) o.add ("\r\n");
		o.add (".\r\n");
		if (!c.send (o.c (), o.n)) return fail (c.err);
		Buf txt; int code = reply (&txt);
		if (code / 100 != 2) { if (code >= 0) { char m[300]; snprintf (m, sizeof m, "%d %s", code, txt.c ()); fail (m); } return false; }
		if (bad[0]) { char m[300]; snprintf (m, sizeof m, "Sent, but refused for %s", bad); scpy (err, m, sizeof err); }
		return true;
	}
	void quit () { if (c.open_) cmd (2, "QUIT"); c.close (); }
};

} // namespace mail

#endif
