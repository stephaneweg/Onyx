//
// Apps/mail/accounts.h -- Mail's accounts: what each one is (its address, its name, IMAP or POP3, its servers, how it
// signs in), kept in SD:/etc/mail/accounts.ini; the providers known by their domain (Gmail, Outlook.com, iCloud,
// Yahoo, GMX, Proximus, Telenet, Orange, Free...: their servers filled in); the secrets -- the passwords, Microsoft's
// tokens -- kept apart in SD:/etc/mail/secrets, encrypted (AES-256-GCM, mbedTLS) with a key of this card
// (SD:/etc/mail/key, made once from the Pi's hardware random numbers). The key lives on the same card: the secrets
// are not readable as text, but whoever has the card has them; Onyx's key vault (docs/HANDOFF.md) will hold them.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _mail_accounts_h
#define _mail_accounts_h

#include "appkit/appkit.h"
#include "mail/util.h"
#include "mail/conn.h"
#include <mbedtls/gcm.h>

namespace mailapp {

using namespace mail;

static const char *const ACCOUNTS_INI = "SD:/etc/mail/accounts.ini";
static const char *const SECRETS_FILE = "SD:/etc/mail/secrets";
static const char *const KEY_FILE = "SD:/etc/mail/key";

enum { K_IMAP = 0, K_POP3 = 1 };
enum { PV_OTHER = 0, PV_GMAIL, PV_OUTLOOK, PV_ICLOUD, PV_YAHOO };
enum { AU_PASSWORD = 0, AU_OAUTH = 1 };

struct Account
{
	char id[24];			// its folder in SD:/mail/ ("a1", "a2"...)
	char email[160], name[120];	// the address, the name shown to whom it writes
	char label[60];			// what the folder list says ("Gmail", "Work")
	int kind, provider, auth;
	char inHost[120]; int inPort, inSec; char inUser[160];
	char outHost[120]; int outPort, outSec; char outUser[160];	// (outUser empty: inUser)
	bool verify;			// the servers' certificates checked
	bool popKeep; int popDays;	// POP3: leave on the server, deleted there after n days (0: never)
	unsigned colour;		// its stripe in the lists
	char signature[600];
	int checkMinutes;		// new mail looked for every n minutes (0: only by hand)
	// the secrets (from SECRETS_FILE)
	char inSecret[200], outSecret[200];
	char refresh[4096], access[4096]; long long expires;
};

static const unsigned ACCOUNT_COLOURS[8] = { 0xD93025, 0x1A73E8, 0x188038, 0xE37400, 0x9334E6, 0x007B83, 0xC5221F, 0x5F6368 };

// ---- the providers -----------------------------------------------------------------------------------------------------
struct Provider
{
	const char *domains;		// space separated
	const char *label; int provider, auth;
	const char *in; int inPort, inSec;
	const char *out; int outPort, outSec;
	const char *note;		// what the wizard tells
};
static const Provider PROVIDERS[] = {
	{ "gmail.com googlemail.com", "Gmail", PV_GMAIL, AU_PASSWORD, "imap.gmail.com", 993, SEC_TLS, "smtp.gmail.com", 465, SEC_TLS,
	  "Google asks for an app password: turn on 2-Step Verification, then make one at myaccount.google.com/apppasswords." },
	{ "outlook.com hotmail.com hotmail.fr hotmail.be hotmail.co.uk live.com live.fr live.be msn.com outlook.fr outlook.be", "Outlook", PV_OUTLOOK, AU_OAUTH,
	  "outlook.office365.com", 993, SEC_TLS, "smtp-mail.outlook.com", 587, SEC_STARTTLS, "Microsoft signs you in with a code, on a phone or a PC." },
	{ "icloud.com me.com mac.com", "iCloud", PV_ICLOUD, AU_PASSWORD, "imap.mail.me.com", 993, SEC_TLS, "smtp.mail.me.com", 587, SEC_STARTTLS,
	  "Apple asks for an app-specific password: make one at account.apple.com (Sign-In and Security)." },
	{ "yahoo.com yahoo.fr yahoo.be yahoo.co.uk ymail.com", "Yahoo", PV_YAHOO, AU_PASSWORD, "imap.mail.yahoo.com", 993, SEC_TLS, "smtp.mail.yahoo.com", 465, SEC_TLS,
	  "Yahoo asks for an app password: Account security > Generate app password." },
	{ "gmx.com gmx.net gmx.de gmx.fr", "GMX", PV_OTHER, AU_PASSWORD, "imap.gmx.net", 993, SEC_TLS, "mail.gmx.net", 587, SEC_STARTTLS,
	  "Turn on IMAP access in GMX's settings (POP3 & IMAP) first." },
	{ "proximus.be skynet.be", "Proximus", PV_OTHER, AU_PASSWORD, "imap.proximus.be", 993, SEC_TLS, "relay.proximus.be", 587, SEC_STARTTLS, "" },
	{ "telenet.be", "Telenet", PV_OTHER, AU_PASSWORD, "imap.telenet.be", 993, SEC_TLS, "smtp.telenet.be", 587, SEC_STARTTLS, "" },
	{ "orange.fr wanadoo.fr", "Orange", PV_OTHER, AU_PASSWORD, "imap.orange.fr", 993, SEC_TLS, "smtp.orange.fr", 465, SEC_TLS, "" },
	{ "free.fr", "Free", PV_OTHER, AU_PASSWORD, "imap.free.fr", 993, SEC_TLS, "smtp.free.fr", 465, SEC_TLS, "" },
	{ "laposte.net", "La Poste", PV_OTHER, AU_PASSWORD, "imap.laposte.net", 993, SEC_TLS, "smtp.laposte.net", 465, SEC_TLS, "" },
	{ "fastmail.com fastmail.fm", "Fastmail", PV_OTHER, AU_PASSWORD, "imap.fastmail.com", 993, SEC_TLS, "smtp.fastmail.com", 465, SEC_TLS,
	  "Fastmail asks for an app password (Settings > Privacy & Security)." },
	{ "proton.me protonmail.com", "Proton", PV_OTHER, AU_PASSWORD, "127.0.0.1", 1143, SEC_STARTTLS, "127.0.0.1", 1025, SEC_STARTTLS,
	  "Proton Mail needs its Bridge, which does not run on Onyx." },
};
static const int NPROVIDERS = (int) (sizeof PROVIDERS / sizeof PROVIDERS[0]);
// the provider of an address (-1: unknown)
static int provider_of (const char *email)
{
	const char *at = strrchr (email, '@'); if (!at) return -1;
	const char *dom = at + 1; int dl = (int) strlen (dom);
	if (!dl) return -1;
	for (int i = 0; i < NPROVIDERS; i++)
		for (const char *p = PROVIDERS[i].domains; *p; )
		{
			while (*p == ' ') p++;
			const char *s = p; while (*p && *p != ' ') p++;
			if (p - s == dl && ieqn (s, dom, dl)) return i;
		}
	return -1;
}
// an account filled from what is known of its address
static void account_guess (Account &a, const char *email)
{
	scpy (a.email, email, sizeof a.email);
	scpy (a.inUser, email, sizeof a.inUser); a.outUser[0] = 0;
	int p = provider_of (email);
	if (p >= 0)
	{
		const Provider &v = PROVIDERS[p];
		scpy (a.label, v.label, sizeof a.label); a.provider = v.provider; a.auth = v.auth; a.kind = K_IMAP;
		scpy (a.inHost, v.in, sizeof a.inHost); a.inPort = v.inPort; a.inSec = v.inSec;
		scpy (a.outHost, v.out, sizeof a.outHost); a.outPort = v.outPort; a.outSec = v.outSec;
		return;
	}
	// unknown: imap.<domain> / smtp.<domain>, the usual ports
	const char *at = strrchr (email, '@'); const char *dom = at ? at + 1 : "";
	a.provider = PV_OTHER; a.auth = AU_PASSWORD;
	char lbl[60]; scpy (lbl, dom, sizeof lbl); char *dot = strchr (lbl, '.'); if (dot) *dot = 0;
	if (lbl[0] >= 'a' && lbl[0] <= 'z') lbl[0] -= 32;
	scpy (a.label, lbl[0] ? lbl : "Mail", sizeof a.label);
	snprintf (a.inHost, sizeof a.inHost, a.kind == K_POP3 ? "pop.%s" : "imap.%s", dom);
	a.inPort = a.kind == K_POP3 ? 995 : 993; a.inSec = SEC_TLS;
	snprintf (a.outHost, sizeof a.outHost, "smtp.%s", dom); a.outPort = 587; a.outSec = SEC_STARTTLS;
}
static void account_defaults (Account &a)
{
	memset (&a, 0, sizeof a);
	a.kind = K_IMAP; a.inPort = 993; a.inSec = SEC_TLS; a.outPort = 587; a.outSec = SEC_STARTTLS;
	a.verify = true; a.popKeep = true; a.popDays = 0; a.checkMinutes = 5; a.colour = ACCOUNT_COLOURS[0];
}
static const char *sec_name (int s) { return s == SEC_TLS ? "tls" : s == SEC_STARTTLS ? "starttls" : "none"; }
static int sec_of (const char *s) { return ieq (s, "starttls") ? SEC_STARTTLS : ieq (s, "none") ? SEC_NONE : SEC_TLS; }

// ---- files: read whole, written whole ------------------------------------------------------------------------------------
static char *file_read (const char *path, int *len)
{
	*len = 0;
	void *f = kapi_open (path); if (!f) return 0;
	unsigned n = kapi_fsize (f);
	char *b = (char *) malloc (n + 1);
	int r = b ? kapi_read (f, b, n) : -1;
	kapi_close (f);
	if (r < 0) { free (b); return 0; }
	b[r] = 0; *len = r;
	return b;
}
static void mkdirs (const char *path)
{
	// each level of "SD:/a/b/c" made (the existing ones refused quietly)
	char p[300]; scpy (p, path, sizeof p);
	for (char *s = strchr (p, '/'); s; s = strchr (s + 1, '/'))
	{
		if (s == p || s[-1] == ':') continue;
		*s = 0; kapi_mkdir (p); *s = '/';
	}
	kapi_mkdir (p);
}

// "\n" "\t" "\\" in a value of one line
static void esc (Buf &o, const char *s) { for (; *s; s++) { if (*s == '\n') o.add ("\\n"); else if (*s == '\t') o.add ("\\t"); else if (*s == '\\') o.add ("\\\\"); else if (*s != '\r') o.addc (*s); } }
static void unesc (char *d, const char *s, int cap)
{
	int k = 0;
	for (; *s && k < cap - 1; s++)
	{
		if (*s == '\\' && s[1]) { s++; d[k++] = *s == 'n' ? '\n' : *s == 't' ? '\t' : *s; }
		else d[k++] = *s;
	}
	d[k] = 0;
}

// ---- accounts.ini -------------------------------------------------------------------------------------------------------------
struct Accounts
{
	Account a[12]; int n;
	Accounts () : n (0) {}
	Account *by_id (const char *id) { for (int i = 0; i < n; i++) if (!strcmp (a[i].id, id)) return &a[i]; return 0; }
	int index_of (const Account *x) const { return (int) (x - a); }

	void load ()
	{
		n = 0;
		int len; char *b = file_read (ACCOUNTS_INI, &len); if (!b) return;
		Account *cur = 0;
		for (char *l = b; l && *l; )
		{
			char *e = strchr (l, '\n'); if (e) *e = 0;
			char *r = e ? e - 1 : l + strlen (l) - 1; while (r >= l && (*r == '\r' || *r == ' ' || *r == '\t')) *r-- = 0;
			while (*l == ' ' || *l == '\t') l++;
			if (!strcmp (l, "[account]")) { if (n < 12) { cur = &a[n++]; account_defaults (*cur); } else cur = 0; }
			else if (cur && *l && *l != '#' && *l != ';')
			{
				char *eq = strchr (l, '=');
				if (eq)
				{
					char *k = l, *ke = eq; while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t')) ke--; *ke = 0;
					char *v = eq + 1; while (*v == ' ' || *v == '\t') v++;
					set (*cur, k, v);
				}
			}
			l = e ? e + 1 : 0;
		}
		free (b);
		load_secrets ();
	}
	static void set (Account &x, const char *k, const char *v)
	{
		if (!strcmp (k, "id")) scpy (x.id, v, sizeof x.id);
		else if (!strcmp (k, "email")) scpy (x.email, v, sizeof x.email);
		else if (!strcmp (k, "name")) scpy (x.name, v, sizeof x.name);
		else if (!strcmp (k, "label")) scpy (x.label, v, sizeof x.label);
		else if (!strcmp (k, "kind")) x.kind = ieq (v, "pop3") ? K_POP3 : K_IMAP;
		else if (!strcmp (k, "provider")) x.provider = ieq (v, "gmail") ? PV_GMAIL : ieq (v, "outlook") ? PV_OUTLOOK : ieq (v, "icloud") ? PV_ICLOUD : ieq (v, "yahoo") ? PV_YAHOO : PV_OTHER;
		else if (!strcmp (k, "auth")) x.auth = ieq (v, "oauth") ? AU_OAUTH : AU_PASSWORD;
		else if (!strcmp (k, "in_host")) scpy (x.inHost, v, sizeof x.inHost);
		else if (!strcmp (k, "in_port")) x.inPort = atoi (v);
		else if (!strcmp (k, "in_security")) x.inSec = sec_of (v);
		else if (!strcmp (k, "in_user")) scpy (x.inUser, v, sizeof x.inUser);
		else if (!strcmp (k, "out_host")) scpy (x.outHost, v, sizeof x.outHost);
		else if (!strcmp (k, "out_port")) x.outPort = atoi (v);
		else if (!strcmp (k, "out_security")) x.outSec = sec_of (v);
		else if (!strcmp (k, "out_user")) scpy (x.outUser, v, sizeof x.outUser);
		else if (!strcmp (k, "verify")) x.verify = atoi (v) != 0;
		else if (!strcmp (k, "pop_keep")) x.popKeep = atoi (v) != 0;
		else if (!strcmp (k, "pop_days")) x.popDays = atoi (v);
		else if (!strcmp (k, "colour")) x.colour = (unsigned) strtoul (v + (*v == '#'), 0, 16);
		else if (!strcmp (k, "signature")) unesc (x.signature, v, sizeof x.signature);
		else if (!strcmp (k, "check_minutes")) x.checkMinutes = atoi (v);
	}
	bool save () const
	{
		Buf o;
		o.add ("# Onyx Mail's accounts (Mail writes this file; the passwords are not here: SD:/etc/mail/secrets, encrypted)\n");
		static const char *const PV[] = { "other", "gmail", "outlook", "icloud", "yahoo" };
		for (int i = 0; i < n; i++)
		{
			const Account &x = a[i];
			o.addf ("\n[account]\nid = %s\nemail = %s\nname = %s\nlabel = %s\nkind = %s\nprovider = %s\nauth = %s\n", x.id, x.email, x.name, x.label,
				x.kind == K_POP3 ? "pop3" : "imap", PV[x.provider], x.auth == AU_OAUTH ? "oauth" : "password");
			o.addf ("in_host = %s\nin_port = %d\nin_security = %s\nin_user = %s\n", x.inHost, x.inPort, sec_name (x.inSec), x.inUser);
			o.addf ("out_host = %s\nout_port = %d\nout_security = %s\nout_user = %s\n", x.outHost, x.outPort, sec_name (x.outSec), x.outUser);
			o.addf ("verify = %d\npop_keep = %d\npop_days = %d\ncolour = #%06X\ncheck_minutes = %d\nsignature = ", x.verify ? 1 : 0, x.popKeep ? 1 : 0, x.popDays, x.colour & 0xFFFFFF, x.checkMinutes);
			esc (o, x.signature); o.addc ('\n');
		}
		mkdirs ("SD:/etc/mail");
		return kapi_save_file (ACCOUNTS_INI, o.c (), (unsigned) o.n) >= 0;
	}
	// a new account's id: the first free "aN"
	void new_id (char *out, int cap) const
	{
		for (int k = 1; k < 100; k++) { snprintf (out, cap, "a%d", k); bool used = false; for (int i = 0; i < n; i++) if (!strcmp (a[i].id, out)) used = true; if (!used) return; }
	}

	// ---- the secrets ----------------------------------------------------------------------------------------------------
	static bool key (unsigned char k[32])
	{
		int len; char *b = file_read (KEY_FILE, &len);
		if (b && len == 32) { memcpy (k, b, 32); free (b); return true; }
		free (b);
		if (kapi_random (k, 32) != 32) return false;
		mkdirs ("SD:/etc/mail");
		return kapi_save_file (KEY_FILE, k, 32) == 32;
	}
	void load_secrets ()
	{
		int len; char *b = file_read (SECRETS_FILE, &len);
		if (!b) return;
		unsigned char k[32];
		Buf plain;
		if (len > 9 + 12 + 16 && !memcmp (b, "ONYXMAIL1", 9) && key (k))
		{
			const unsigned char *iv = (const unsigned char *) b + 9, *ct = iv + 12; int cn = len - 9 - 12 - 16;
			const unsigned char *tag = ct + cn;
			plain.reserve (cn + 1);
			mbedtls_gcm_context g; mbedtls_gcm_init (&g);
			if (mbedtls_gcm_setkey (&g, MBEDTLS_CIPHER_ID_AES, k, 256) == 0 &&
			    mbedtls_gcm_auth_decrypt (&g, (size_t) cn, iv, 12, 0, 0, tag, 16, ct, (unsigned char *) plain.p) == 0)
			{ plain.n = cn; plain.p[cn] = 0; }
			mbedtls_gcm_free (&g);
			memset (k, 0, sizeof k);
		}
		free (b);
		// id <TAB> what <TAB> value
		for (char *l = plain.p; l && *l; )
		{
			char *e = strchr (l, '\n'); if (e) *e = 0;
			char *t1 = strchr (l, '\t'), *t2 = t1 ? strchr (t1 + 1, '\t') : 0;
			if (t1 && t2)
			{
				*t1 = *t2 = 0;
				Account *x = by_id (l);
				const char *w = t1 + 1, *v = t2 + 1;
				if (x)
				{
					if (!strcmp (w, "in")) unesc (x->inSecret, v, sizeof x->inSecret);
					else if (!strcmp (w, "out")) unesc (x->outSecret, v, sizeof x->outSecret);
					else if (!strcmp (w, "refresh")) unesc (x->refresh, v, sizeof x->refresh);
					else if (!strcmp (w, "access")) unesc (x->access, v, sizeof x->access);
					else if (!strcmp (w, "expires")) x->expires = atoll (v);
				}
			}
			l = e ? e + 1 : 0;
		}
		if (plain.p) memset (plain.p, 0, plain.cap);
	}
	bool save_secrets () const
	{
		Buf plain;
		for (int i = 0; i < n; i++)
		{
			const Account &x = a[i];
			if (x.inSecret[0]) { plain.addf ("%s\tin\t", x.id); esc (plain, x.inSecret); plain.addc ('\n'); }
			if (x.outSecret[0]) { plain.addf ("%s\tout\t", x.id); esc (plain, x.outSecret); plain.addc ('\n'); }
			if (x.refresh[0]) { plain.addf ("%s\trefresh\t", x.id); esc (plain, x.refresh); plain.addc ('\n'); }
			if (x.access[0]) { plain.addf ("%s\taccess\t", x.id); esc (plain, x.access); plain.addf ("\n%s\texpires\t%lld\n", x.id, x.expires); }
		}
		unsigned char k[32], iv[12], tag[16];
		if (!key (k) || kapi_random (iv, 12) != 12) return false;
		Buf out; out.reserve (9 + 12 + plain.n + 16);
		out.add ("ONYXMAIL1"); out.add (iv, 12);
		unsigned char *ct = (unsigned char *) malloc (plain.n + 1);
		bool ok = false;
		mbedtls_gcm_context g; mbedtls_gcm_init (&g);
		if (ct && mbedtls_gcm_setkey (&g, MBEDTLS_CIPHER_ID_AES, k, 256) == 0 &&
		    mbedtls_gcm_crypt_and_tag (&g, MBEDTLS_GCM_ENCRYPT, (size_t) plain.n, iv, 12, 0, 0, (const unsigned char *) plain.c (), ct, 16, tag) == 0)
		{
			if (plain.n) out.add (ct, plain.n);
			out.add (tag, 16);
			mkdirs ("SD:/etc/mail");
			ok = kapi_save_file (SECRETS_FILE, out.c (), (unsigned) out.n) >= 0;
		}
		mbedtls_gcm_free (&g);
		memset (k, 0, sizeof k);
		if (plain.p) memset (plain.p, 0, plain.cap);
		free (ct);
		return ok;
	}
};

} // namespace mailapp

#endif
