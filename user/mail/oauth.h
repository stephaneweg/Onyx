//
// mail/oauth.h -- OAuth 2 for the mail servers that refuse passwords (Outlook.com / Hotmail / Microsoft 365): the
// device code flow (RFC 8628) -- Mail shows a code, the user signs in on a phone or a PC at microsoft.com/link, Mail
// polls the token endpoint until it is allowed --, then the access token renewed with the refresh token. The
// requests are HTTPS POSTs made here over mail/conn.h (the certificate checked against SD:/res/ca-bundle); the
// answers' few JSON fields read by json_str / json_num. The application's id ("Onyx Mail", registered by the user at
// Microsoft: docs/mail/README.md) comes from SD:/etc/mail/oauth.ini. Blocking: Mail's worker thread.
// Part of Onyx's mail (docs/mail/README.md).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See mail/util.h for the full notice.
//
#ifndef ONYX_MAIL_OAUTH_H
#define ONYX_MAIL_OAUTH_H

#include "mail/conn.h"

namespace mail {

// ---- a JSON answer's top-level fields (enough for the token endpoint's answers) --------------------------------------------
static const char *json_find (const char *j, const char *key)
{
	int kn = (int) strlen (key);
	for (const char *p = j; (p = strchr (p, '"')); )
	{
		const char *s = ++p; while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; }
		if (!*p) return 0;
		bool match = p - s == kn && !memcmp (s, key, kn);
		p++;
		const char *q = p; while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
		if (*q != ':') continue;					// (a value, not a key)
		q++; while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
		if (match) return q;
		p = q;
		if (*p == '"') continue;					// (the value's string: skipped as the loop's next string)
	}
	return 0;
}
static bool json_str (const char *j, const char *key, Buf &out)
{
	out.clear ();
	const char *v = json_find (j, key);
	if (!v || *v != '"') return false;
	for (v++; *v && *v != '"'; v++)
	{
		if (*v != '\\') { out.addc (*v); continue; }
		v++;
		switch (*v)
		{
		case 'n': out.addc ('\n'); break; case 't': out.addc ('\t'); break; case 'r': break;
		case 'b': case 'f': break;
		case 'u':
		{
			unsigned u = 0; for (int i = 1; i <= 4 && v[i]; i++) u = u * 16 + (unsigned) (hexv (v[i]) < 0 ? 0 : hexv (v[i]));
			v += 4;
			if (u >= 0xD800 && u < 0xDC00 && v[1] == '\\' && v[2] == 'u')
			{
				unsigned lo = 0; for (int i = 3; i <= 6 && v[i]; i++) lo = lo * 16 + (unsigned) (hexv (v[i]) < 0 ? 0 : hexv (v[i]));
				if (lo >= 0xDC00 && lo < 0xE000) { u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00); v += 6; }
			}
			char t[4]; out.add (t, u8put (t, u)); break;
		}
		case 0: return true;
		default: out.addc (*v);
		}
	}
	return true;
}
static long json_num (const char *j, const char *key, long def = 0)
{
	const char *v = json_find (j, key);
	if (!v) return def;
	if (*v == '"') v++;
	return (*v >= '0' && *v <= '9') || *v == '-' ? atol (v) : def;
}
// a form's value, percent-encoded (application/x-www-form-urlencoded)
static void form_add (Buf &o, const char *k, const char *v)
{
	if (o.n) o.addc ('&');
	o.add (k); o.addc ('=');
	for (const unsigned char *p = (const unsigned char *) v; *p; p++)
	{
		if (isalnum (*p) || strchr ("-._~", *p)) o.addc ((char) *p);
		else if (*p == ' ') o.addc ('+');
		else o.addf ("%%%02X", *p);
	}
}

// ---- an HTTPS POST (a form), its answer's status and body ------------------------------------------------------------------
static int https_post (const char *host, int port, int sec, const char *path, const char *form, Buf &body, char *err, int errCap, volatile int *cancel)
{
	Conn c; c.cancel = cancel; c.timeoutMs = 20000;
	body.clear ();
	if (!c.open (host, port, sec)) { scpy (err, c.err, errCap); return -1; }
	Buf rq;
	rq.addf ("POST %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Onyx-Mail/1.0\r\nAccept: application/json\r\n"
		"Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", path, host, (int) strlen (form));
	rq.add (form);
	if (!c.send (rq.c (), rq.n)) { scpy (err, c.err, errCap); return -1; }
	Buf ln;
	if (!c.line (ln)) { scpy (err, c.err, errCap); return -1; }
	int status = 0; const char *sp = strchr (ln.c (), ' '); if (sp) status = atoi (sp + 1);
	long len = -1; bool chunked = false;
	for (;;)
	{
		if (!c.line (ln)) { scpy (err, c.err, errCap); return -1; }
		if (!ln.n) break;
		if (istarts (ln.c (), "Content-Length:")) len = atol (ln.c () + 15);
		if (istarts (ln.c (), "Transfer-Encoding:") && ifind (ln.c (), "chunked")) chunked = true;
	}
	if (chunked)
	{
		for (;;)
		{
			if (!c.line (ln)) break;
			long n = strtol (ln.c (), 0, 16); if (n <= 0) break;
			if (!c.bytes (body, n) || !c.line (ln)) break;
		}
	}
	else if (len >= 0) { if (len > (4 << 20) || !c.bytes (body, len)) { scpy (err, c.err, errCap); return -1; } }
	else { Buf rest; while (c.line (ln)) { body.add (ln.c (), ln.n); body.addc ('\n'); } }
	return status;
}

// ---- the device code flow ----------------------------------------------------------------------------------------------
struct OAuthCfg
{
	char clientId[80];		// the application's id (a GUID); empty: not set up yet
	char tenant[40];		// "common" (any Microsoft account), "consumers" (personal ones), an organisation's id
	char host[80]; int port, sec;	// login.microsoftonline.com:443 TLS (a test server otherwise)
	char scope[300];
};
static void oauth_defaults (OAuthCfg &o)
{
	memset (&o, 0, sizeof o);
	scpy (o.tenant, "common", sizeof o.tenant);
	scpy (o.host, "login.microsoftonline.com", sizeof o.host); o.port = 443; o.sec = SEC_TLS;
	scpy (o.scope, "https://outlook.office.com/IMAP.AccessAsUser.All https://outlook.office.com/POP.AccessAsUser.All "
		"https://outlook.office.com/SMTP.Send offline_access", sizeof o.scope);
}
// SD:/etc/mail/oauth.ini: "client_id = ...", "tenant = ...", (tests: "host =", "port =", "tls = 0")
static void oauth_load (OAuthCfg &o, const char *path = "SD:/etc/mail/oauth.ini")
{
	oauth_defaults (o);
	void *f = kapi_open (path); if (!f) return;
	unsigned n = kapi_fsize (f); if (n > 8192) n = 8192;
	char *b = (char *) malloc (n + 1); int r = b ? kapi_read (f, b, n) : -1; kapi_close (f);
	if (r < 0) { free (b); return; }
	b[r] = 0;
	for (char *l = b; *l; )
	{
		char *e = strchr (l, '\n'); if (e) *e = 0;
		char *eq = strchr (l, '=');
		if (eq && l[0] != '#' && l[0] != ';')
		{
			char *k = l; char *ke = eq; while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t')) ke--; *ke = 0;
			while (*k == ' ' || *k == '\t') k++;
			char *v = eq + 1; while (*v == ' ' || *v == '\t') v++;
			char *ve = v + strlen (v); while (ve > v && (ve[-1] == ' ' || ve[-1] == '\r' || ve[-1] == '\t')) *--ve = 0;
			if (ieq (k, "client_id")) scpy (o.clientId, v, sizeof o.clientId);
			else if (ieq (k, "tenant")) scpy (o.tenant, v, sizeof o.tenant);
			else if (ieq (k, "host")) scpy (o.host, v, sizeof o.host);
			else if (ieq (k, "port")) o.port = atoi (v);
			else if (ieq (k, "tls")) o.sec = atoi (v) ? SEC_TLS : SEC_NONE;
			else if (ieq (k, "scope")) scpy (o.scope, v, sizeof o.scope);
		}
		if (!e) break;
		l = e + 1;
	}
	free (b);
}
struct DeviceCode
{
	char userCode[32], verifyUri[200];	// what the user is shown: "ABCD-EFGH", "https://microsoft.com/devicelogin"
	char deviceCode[1200];
	int interval, expiresIn;		// seconds
};
struct Tokens
{
	char access[4096], refresh[4096];
	long long expires;			// (seconds since 1970)
};
struct OAuth
{
	OAuthCfg cfg; char err[300]; volatile int *cancel;
	OAuth () : cancel (0) { err[0] = 0; oauth_defaults (cfg); }
	bool fail (const char *m) { scpy (err, m, sizeof err); return false; }
	int post (const char *what, const Buf &form, Buf &body)
	{
		char path[160]; snprintf (path, sizeof path, "/%s/oauth2/v2.0/%s", cfg.tenant, what);
		return https_post (cfg.host, cfg.port, cfg.sec, path, form.c (), body, err, sizeof err, cancel);
	}
	// the server's error in words
	void explain (const char *body, const char *start)
	{
		Buf e, d; json_str (body, "error", e); json_str (body, "error_description", d);
		const char *nl = strchr (d.c (), '\r'); if (!nl) nl = strchr (d.c (), '\n');
		char m[300]; snprintf (m, sizeof m, "%s%s%s%.*s", start, e.n ? e.c () : "", e.n && d.n ? ": " : "", nl ? (int) (nl - d.c ()) : 200, d.c ());
		scpy (err, m, sizeof err);
	}
	// 1. a code to show the user
	bool start (DeviceCode &dc)
	{
		memset (&dc, 0, sizeof dc);
		if (!cfg.clientId[0]) return fail ("Outlook's sign-in is not set up yet: the application's id is missing (SD:/etc/mail/oauth.ini).");
		Buf form, body; form_add (form, "client_id", cfg.clientId); form_add (form, "scope", cfg.scope);
		int st = post ("devicecode", form, body);
		if (st < 0) return false;
		if (st != 200) { explain (body.c (), "Microsoft refused: "); return false; }
		Buf v;
		json_str (body.c (), "user_code", v); scpy (dc.userCode, v.c (), sizeof dc.userCode);
		json_str (body.c (), "device_code", v); scpy (dc.deviceCode, v.c (), sizeof dc.deviceCode);
		if (!json_str (body.c (), "verification_uri", v)) json_str (body.c (), "verification_url", v);
		scpy (dc.verifyUri, v.c (), sizeof dc.verifyUri);
		dc.interval = (int) json_num (body.c (), "interval", 5); if (dc.interval < 1) dc.interval = 5;
		dc.expiresIn = (int) json_num (body.c (), "expires_in", 900);
		if (!dc.userCode[0] || !dc.deviceCode[0]) return fail ("Microsoft's answer was not understood.");
		return true;
	}
	bool take_tokens (const char *body, Tokens &t, long long now)
	{
		Buf v;
		if (!json_str (body, "access_token", v) || !v.n) return fail ("Microsoft's answer had no token.");
		scpy (t.access, v.c (), sizeof t.access);
		if (json_str (body, "refresh_token", v) && v.n) scpy (t.refresh, v.c (), sizeof t.refresh);	// (else the old one stays)
		t.expires = now + json_num (body, "expires_in", 3600);
		return true;
	}
	// 2. asked every dc.interval seconds: 1 signed in (t filled), 0 not yet, -1 refused / expired / failed (err)
	int poll (DeviceCode &dc, Tokens &t, long long now)
	{
		Buf form, body;
		form_add (form, "grant_type", "urn:ietf:params:oauth:grant-type:device_code");
		form_add (form, "client_id", cfg.clientId); form_add (form, "device_code", dc.deviceCode);
		int st = post ("token", form, body);
		if (st < 0) return 0;				// (the network: tried again next time)
		if (st == 200) return take_tokens (body.c (), t, now) ? 1 : -1;
		Buf e; json_str (body.c (), "error", e);
		if (ieq (e.c (), "authorization_pending")) return 0;
		if (ieq (e.c (), "slow_down")) { dc.interval += 5; return 0; }
		if (ieq (e.c (), "authorization_declined")) { fail ("The sign-in was declined."); return -1; }
		if (ieq (e.c (), "expired_token")) { fail ("The code has expired: try again."); return -1; }
		explain (body.c (), "Microsoft refused: ");
		return -1;
	}
	// 3. a new access token (before t.expires): false -> signed out (the user signs in again) or no network (err)
	bool refresh (Tokens &t, long long now, bool *signedOut)
	{
		*signedOut = false;
		if (!t.refresh[0]) { *signedOut = true; return fail ("Sign in to Microsoft again."); }
		Buf form, body;
		form_add (form, "grant_type", "refresh_token"); form_add (form, "client_id", cfg.clientId);
		form_add (form, "refresh_token", t.refresh); form_add (form, "scope", cfg.scope);
		int st = post ("token", form, body);
		if (st < 0) return false;
		if (st == 200) return take_tokens (body.c (), t, now);
		Buf e; json_str (body.c (), "error", e);
		if (ieq (e.c (), "invalid_grant") || ieq (e.c (), "interaction_required")) *signedOut = true;
		explain (body.c (), "Microsoft refused: ");
		return false;
	}
};

} // namespace mail

#endif
