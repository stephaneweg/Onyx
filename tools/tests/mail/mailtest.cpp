//
// mailtest.cpp -- Mail's protocol layer (user/mail/) against fakemail.py, on the PC: MIME and the text tools alone,
// then IMAP (login, folders, envelopes, structure, bodies, flags, move, append, IDLE + SMTP's delivery), POP3, SMTP,
// OAuth's device code flow and XOAUTH2. Run by tools/tests/run_mail_test.sh.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#include "mail/imap.h"
#include "mail/pop3.h"
#include "mail/smtp.h"
#include "mail/mime.h"
#include "mail/oauth.h"

using namespace mail;

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf ("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #c); printf (__VA_ARGS__); printf ("\n"); } } while (0)

static int port (const char *env, int def) { const char *e = getenv (env); return e ? atoi (e) : def; }

static void test_util ()
{
	Buf o;
	decode_header (o, "=?UTF-8?Q?Bj=C3=B6rn_M=C3=BCller?= <b@x>"); CHECK (!strcmp (o.c (), "Björn Müller <b@x>"), "%s", o.c ());
	o.clear (); decode_header (o, "=?iso-8859-1?q?caf=E9?= =?iso-8859-1?q?_cr=E8me?="); CHECK (!strcmp (o.c (), "café crème"), "%s", o.c ());
	o.clear (); decode_header (o, "=?UTF-8?B?UmVwb3J0IOKAkyBRMyByw6lzdWx0YXRz?="); CHECK (!strcmp (o.c (), "Report – Q3 résultats"), "%s", o.c ());
	o.clear (); encode_header (o, "Café ☕"); Buf d; decode_header (d, o.c ()); CHECK (!strcmp (d.c (), "Café ☕"), "%s -> %s", o.c (), d.c ());
	long long t = parse_date ("Tue, 1 Sep 2026 10:00:00 +0200"); CHECK (t == 1788249600LL, "%lld", t);
	char ds[64]; format_date (ds, sizeof ds, t, 120); CHECK (!strcmp (ds, "Tue, 01 Sep 2026 10:00:00 +0200"), "%s", ds);
	t = parse_date ("01-Sep-2026 08:00:00 +0000"); CHECK (t == 1788249600LL, "%lld", t);
	Addr a[8]; int n = parse_addrs ("\"Lefèvre, Anna\" <anna@x.org>, bob@y.com, Carl <c@z>", a, 8);
	CHECK (n == 3 && !strcmp (a[0].name, "Lefèvre, Anna") && !strcmp (a[1].email, "bob@y.com") && !strcmp (a[2].name, "Carl"), "%d %s|%s|%s", n, a[0].name, a[1].email, a[2].name);
	Buf m; mutf7_decode (m, "Projets &AOk-t&AOk-"); CHECK (!strcmp (m.c (), "Projets été"), "%s", m.c ());
	Buf e; mutf7_encode (e, "Projets été & co"); CHECK (!strcmp (e.c (), "Projets &AOk-t&AOk- &- co"), "%s", e.c ());
}

static void test_mime ()
{
	const char *raw =
		"From: \"Anna\" <anna@x.org>\r\nTo: me@onyx.test\r\nSubject: =?utf-8?q?Fa=C3=A7ade?=\r\n  and more\r\n"
		"Content-Type: multipart/mixed; boundary=\"XX\"\r\n\r\npreamble\r\n"
		"--XX\r\nContent-Type: multipart/alternative; boundary=YY\r\n\r\n"
		"--YY\r\nContent-Type: text/plain; charset=iso-8859-1\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\nCaf=E9 =\r\nsoft\r\n"
		"--YY\r\nContent-Type: text/html; charset=utf-8\r\n\r\n<p>Café</p>\r\n--YY--\r\n"
		"--XX\r\nContent-Type: application/pdf; name=\"a.pdf\"\r\nContent-Disposition: attachment;\r\n filename*0*=utf-8''r%C3%A9sum;\r\n filename*1*=%C3%A9.pdf\r\n"
		"Content-Transfer-Encoding: base64\r\n\r\nJVBERi0xLjQK\r\n--XX--\r\n";
	Mime M; M.parse (raw, (int) strlen (raw));
	Buf s; M.header ("Subject", s); CHECK (!strcmp (s.c (), "Façade and more"), "[%s]", s.c ());
	CHECK (M.np == 5, "%d parts", M.np);
	int h = M.body_part (true), p = M.body_part (false);
	CHECK (h >= 0 && ieq (M.parts[h].sub, "html"), "html %d", h);
	CHECK (p >= 0 && ieq (M.parts[p].sub, "plain"), "plain %d", p);
	Buf tx; if (p >= 0) M.text (M.parts[p], tx); CHECK (!strcmp (tx.c (), "Café soft"), "[%s]", tx.c ());
	int at[4]; int na = M.attachments (at, 4);
	CHECK (na == 1 && !strcmp (M.parts[at[0]].name, "résumé.pdf"), "%d %s", na, na ? M.parts[at[0]].name : "");
	Buf pdf; if (na) M.decoded (M.parts[at[0]], pdf); CHECK (pdf.n == 9 && !memcmp (pdf.p, "%PDF-1.4\n", 9), "%d", pdf.n);

	// a message made, then read back
	const char png[] = "\x89PNG fake";
	Attachment att[2] = { { "note é.txt", "text/plain", "hello\n", 6, 0 }, { "logo.png", "image/png", png, 9, "logo@onyx" } };
	Outgoing o; memset (&o, 0, sizeof o);
	o.from = "Me Moi <me@onyx.test>"; o.to = "\"Lefèvre, Anna\" <anna@x.org>, bob@y.com"; o.cc = "Zoë <z@q.be>";
	o.subject = "Réunion — lundi"; o.text = "Bonjour,\n.leading dot\nà lundi.\n"; o.html = "<p>Bonjour,</p><img src=\"cid:logo@onyx\"><p>à lundi.</p>";
	o.att = att; o.natt = 2; o.date = 1788249600LL; o.tzMin = 120; o.inReplyTo = "<a@b>"; o.references = "<z@y> <a@b>";
	Buf msg; build (msg, o);
	CHECK (strstr (msg.c (), "\r\nMessage-ID: <") && o.msgid[0], "msgid");
	CHECK (!strchr (msg.c (), '\n') || !strstr (msg.c (), "\n\n"), "bare LF");
	for (int i = 0; i < msg.n; i++) if ((unsigned char) msg.p[i] >= 0x80) { CHECK (false, "8-bit byte at %d", i); break; }
	Mime R; R.parse (msg.c (), msg.n);
	R.header ("Subject", s); CHECK (!strcmp (s.c (), "Réunion — lundi"), "[%s]", s.c ());
	R.header_raw ("To", s); Addr a[4]; int n = parse_addrs (s.c (), a, 4);
	CHECK (n == 2 && !strcmp (a[0].name, "Lefèvre, Anna") && !strcmp (a[1].email, "bob@y.com"), "[%s]", s.c ());
	R.header ("Cc", s); CHECK (!strcmp (s.c (), "Zoë <z@q.be>"), "[%s]", s.c ());
	p = R.body_part (false); tx.clear (); if (p >= 0) R.text (R.parts[p], tx);
	CHECK (!strcmp (tx.c (), "Bonjour,\n.leading dot\nà lundi."), "[%s]", tx.c ());
	h = R.body_part (true); tx.clear (); if (h >= 0) R.text (R.parts[h], tx); CHECK (strstr (tx.c (), "à lundi"), "[%s]", tx.c ());
	na = R.attachments (at, 4); CHECK (na == 1 && !strcmp (R.parts[at[0]].name, "note é.txt"), "%d [%s]", na, na ? R.parts[at[0]].name : "");
	int c = R.by_cid ("logo@onyx"); Buf img; if (c >= 0) R.decoded (R.parts[c], img); CHECK (c >= 0 && img.n == 9 && !memcmp (img.p, png, 9), "cid %d", c);
	char em[4][160]; CHECK (addr_emails (o.to, em, 4) == 2 && !strcmp (em[0], "anna@x.org"), "rcpt");

	// JSON
	const char *j = "{\"a\":\"x\",\"user_code\":\"AB\\u00e9\\\"\",\"interval\":5,\"n\":{\"k\":\"v\"}}";
	Buf v; CHECK (json_str (j, "user_code", v) && !strcmp (v.c (), "ABé\""), "[%s]", v.c ());
	CHECK (json_num (j, "interval") == 5, "interval");
	CHECK (!json_str (j, "x", v), "absent");
}

struct Got { int n; long uids[64]; Envelope e[64]; };
static void on_env (void *ctx, const Envelope &e) { Got &g = *(Got *) ctx; if (g.n < 64) { g.uids[g.n] = e.uid; g.e[g.n] = e; g.n++; } }

static void test_imap ()
{
	int ip = port ("IMAP_PORT", 10143);
	Imap bad; CHECK (!bad.connect ("127.0.0.1", ip, SEC_NONE, "me@onyx.test", "wrong", false), "wrong password");
	CHECK (strstr (bad.err, "Invalid credentials"), "[%s]", bad.err);

	Imap im;
	CHECK (im.connect ("127.0.0.1", ip, SEC_NONE, "me@onyx.test", "secret", false), "connect: %s", im.err);
	CHECK (im.has ("IDLE") && im.has ("MOVE"), "caps [%s]", im.caps);
	ImapFolder *f; int nf;
	CHECK (im.list (&f, &nf), "list: %s", im.err);
	int sent = -1, inbox = -1, ete = -1;
	for (int i = 0; i < nf; i++) { if (f[i].special == SP_SENT) sent = i; if (f[i].special == SP_INBOX) inbox = i; if (!strcmp (f[i].show, "Projets été")) ete = i; }
	CHECK (nf == 8 && sent >= 0 && inbox >= 0 && ete >= 0, "%d folders", nf);
	if (sent >= 0) CHECK (!strcmp (f[sent].show, "Sent Mail") && !strcmp (f[sent].name, "[Gmail]/Sent Mail"), "%s", f[sent].show);
	for (int i = 0; i < nf; i++) if (!strcmp (f[i].name, "[Gmail]")) CHECK (f[i].noselect, "noselect");
	free (f);

	SelectInfo si; CHECK (im.select ("INBOX", &si), "select: %s", im.err);
	CHECK (si.exists == 5 && si.uidnext == 6 && si.uidvalidity, "exists %ld next %ld", si.exists, si.uidnext);
	Got g; g.n = 0;
	CHECK (im.fetch_envelopes (1, 0, on_env, &g), "fetch: %s", im.err);
	CHECK (g.n == 5, "%d envelopes", g.n);
	const Envelope *shop = 0, *bill = 0, *bjorn = 0, *re = 0;
	for (int i = 0; i < g.n; i++)
	{
		if (strstr (g.e[i].subject, "shipped")) shop = &g.e[i];
		if (strstr (g.e[i].subject, "Facture")) bill = &g.e[i];
		if (strstr (g.e[i].subject, "Report")) bjorn = &g.e[i];
		if (strstr (g.e[i].subject, "Re: Weekend")) re = &g.e[i];
	}
	CHECK (shop && bill && bjorn && re, "subjects");
	if (bjorn) { CHECK (!strcmp (bjorn->subject, "Report – Q3 résultats"), "[%s]", bjorn->subject); CHECK (strstr (bjorn->from, "Björn Müller"), "[%s]", bjorn->from); CHECK (!bjorn->attach, "attach"); CHECK (!strcmp (bjorn->textPart, "1") && ieq (bjorn->textEnc, "quoted-printable"), "[%s|%s]", bjorn->textPart, bjorn->textEnc); }
	if (shop) { CHECK (!shop->attach, "the cid picture is not an attachment"); CHECK (!strcmp (shop->textPart, "1.1"), "[%s]", shop->textPart); CHECK (shop->gmThread, "thrid"); }
	if (bill) { CHECK (bill->attach, "pdf"); CHECK (!strcmp (bill->textPart, "1") && ieq (bill->textCs, "iso-8859-1"), "[%s|%s]", bill->textPart, bill->textCs); }
	if (re) { CHECK (!strcmp (re->inReplyTo, "<ardennes-1@example.org>"), "[%s]", re->inReplyTo); CHECK (strstr (re->refs, "<ardennes-1@example.org>"), "[%s]", re->refs); CHECK (!(re->flags & F_SEEN), "unseen"); }
	if (shop) CHECK (shop->date > 1700000000LL && shop->size > 500, "date %lld size %ld", shop->date, shop->size);

	// a preview, the whole message
	if (bill)
	{
		Buf b; CHECK (im.fetch_body (bill->uid, bill->textPart, 20, b), "body: %s", im.err);
		CHECK (b.n == 20 && !memcmp (b.p, "Voici la facture de ", 20), "[%s]", b.c ());
		Buf whole; CHECK (im.fetch_body (bill->uid, "", 0, whole), "whole: %s", im.err);
		Mime M; M.parse (whole.c (), whole.n); int at[4]; CHECK (M.attachments (at, 4) == 1 && !strcmp (M.parts[at[0]].name, "facture sept.pdf"), "att");
		int p = M.body_part (false); Buf t; if (p >= 0) M.text (M.parts[p], t); CHECK (strstr (t.c (), "Merci à vous"), "[%s]", t.c ());
	}
	// flags
	if (re)
	{
		char u[16]; snprintf (u, sizeof u, "%ld", re->uid);
		CHECK (im.store (u, "+FLAGS (\\Seen \\Flagged)"), "store: %s", im.err);
		struct F { long uid; unsigned fl; } fl = { 0, 0 };
		im.fetch_flags (re->uid, [] (void *c, long uid, unsigned f) { F &x = *(F *) c; if (!x.uid) { x.uid = uid; x.fl = f; } }, &fl);
		CHECK (fl.uid == re->uid && (fl.fl & F_SEEN) && (fl.fl & F_FLAGGED), "flags %u", fl.fl);
	}
	// move to Trash, append to Sent
	if (bjorn)
	{
		char u[16]; snprintf (u, sizeof u, "%ld", bjorn->uid);
		CHECK (im.move (u, "[Gmail]/Trash"), "move: %s", im.err);
		CHECK (im.select ("[Gmail]/Trash", &si) && si.exists == 1, "trash %ld", si.exists);
	}
	const char *m = "From: me@onyx.test\r\nTo: anna@x.org\r\nSubject: appended é\r\n\r\nHi\r\n";
	CHECK (im.append ("[Gmail]/Sent Mail", "(\\Seen)", m, (int) strlen (m)), "append: %s", im.err);
	CHECK (im.select ("[Gmail]/Sent Mail", &si) && si.exists == 2, "sent %ld", si.exists);
	CHECK (im.select ("Projets été", &si) && si.exists == 0, "utf-7 select: %s", im.err);

	// IDLE: nothing, then a message delivered by SMTP while idling
	CHECK (im.select ("INBOX", &si), "select");
	bool ch; unsigned t0 = kapi_clock_us ();
	CHECK (im.idle (300, &ch) && !ch, "idle quiet: %s", im.err);
	CHECK ((kapi_clock_us () - t0) / 1000 >= 250, "idle waited %u ms", (kapi_clock_us () - t0) / 1000);
	struct Th { static int run (void *) { kapi_msleep (300); Smtp s; if (s.connect ("127.0.0.1", port ("SMTP_PORT", 10587), SEC_NONE, "me@onyx.test", "secret", false)) { const char *r[1] = { "me@onyx.test" }; const char *b = "Subject: ping\r\n\r\npong\r\n"; s.send ("me@onyx.test", r, 1, b, (int) strlen (b)); s.quit (); } return 0; } };
	int tid = kapi_thread_create (Th::run, 0, 0, "smtp");
	CHECK (im.idle (5000, &ch) && ch, "idle woke: %s", im.err);
	if (tid > 0) kapi_thread_join (tid, KAPI_WAIT_FOREVER, 0);
	CHECK (im.select ("INBOX", &si) && si.exists == 5, "inbox now %ld", si.exists);		// (5 - moved + delivered)
	im.logout ();
}

static void test_pop3 ()
{
	Pop3 p;
	CHECK (p.connect ("127.0.0.1", port ("POP_PORT", 10110), SEC_NONE, "me@onyx.test", "secret", false), "pop: %s", p.err);
	int n; long sz; CHECK (p.stat (&n, &sz) && n >= 5 && sz > 1000, "stat %d %ld", n, sz);
	PopMsg *m; int k; CHECK (p.list (&m, &k) && k == n && m[0].uidl[0] == 'u', "list %d", k);
	Buf top; CHECK (p.top (1, 0, top) && strstr (top.c (), "Subject:") && !strstr (top.c (), "\r\n\r\nHi"), "top");
	Buf whole; CHECK (p.retr (k, whole) && strstr (whole.c (), "pong"), "retr [%.40s]", whole.c ());
	free (m);
	p.quit ();
	Pop3 bad; CHECK (!bad.connect ("127.0.0.1", port ("POP_PORT", 10110), SEC_NONE, "me@onyx.test", "nope", false) && strstr (bad.err, "Invalid login"), "[%s]", bad.err);
}

static void test_smtp ()
{
	Smtp s;
	CHECK (s.connect ("127.0.0.1", port ("SMTP_PORT", 10587), SEC_NONE, "me@onyx.test", "secret", false), "smtp: %s", s.err);
	CHECK (s.maxSize == 35882577, "size %ld", s.maxSize);
	const char *r[2] = { "anna@x.org", "nobody@x.org" };
	const char *b = "Subject: dots\r\n\r\n.a dot line\n..two\r\nend";
	CHECK (s.send ("me@onyx.test", r, 2, b, (int) strlen (b)), "send: %s", s.err);
	CHECK (strstr (s.err, "nobody@x.org"), "partial [%s]", s.err);
	const char *r2[1] = { "nobody@x.org" };
	CHECK (!s.send ("me@onyx.test", r2, 1, b, (int) strlen (b)), "no recipient");
	s.quit ();
	Smtp bad; CHECK (!bad.connect ("127.0.0.1", port ("SMTP_PORT", 10587), SEC_NONE, "me@onyx.test", "nope", false) && strstr (bad.err, "535"), "[%s]", bad.err);
}

static void test_oauth ()
{
	OAuth o; oauth_defaults (o.cfg);
	scpy (o.cfg.host, "127.0.0.1", sizeof o.cfg.host); o.cfg.port = port ("HTTP_PORT", 10080); o.cfg.sec = SEC_NONE;
	DeviceCode dc;
	CHECK (!strcmp (o.cfg.clientId, "85ccaf6e-81ff-4a62-9194-930fc36429ad"), "the built-in id: [%s]", o.cfg.clientId);
	o.cfg.clientId[0] = 0;
	CHECK (!o.start (dc) && strstr (o.err, "not set up"), "[%s]", o.err);
	scpy (o.cfg.clientId, "wrong", sizeof o.cfg.clientId);
	CHECK (!o.start (dc) && strstr (o.err, "Application not found") && !strstr (o.err, "Trace"), "[%s]", o.err);
	scpy (o.cfg.clientId, "test-client", sizeof o.cfg.clientId);
	CHECK (o.start (dc) && !strcmp (dc.userCode, "ONYX-7Q2K") && dc.interval == 1 && strstr (dc.verifyUri, "devicelogin"), "start: %s", o.err);
	Tokens t; memset (&t, 0, sizeof t);
	CHECK (o.poll (dc, t, 1000) == 0, "pending");
	CHECK (o.poll (dc, t, 1000) == 1 && !strcmp (t.access, "tok-1") && !strcmp (t.refresh, "ref-1") && t.expires == 4600, "token %s", o.err);
	bool out; CHECK (o.refresh (t, 2000, &out) && !strcmp (t.access, "tok-2") && !strcmp (t.refresh, "ref-2"), "refresh: %s", o.err);
	CHECK (!o.refresh (t, 2000, &out) && out, "expired refresh -> signed out");
	// XOAUTH2 on IMAP and SMTP with the token
	Imap im; CHECK (im.connect ("127.0.0.1", port ("IMAP_PORT", 10143), SEC_NONE, "me@onyx.test", "tok-2", true), "xoauth2 imap: %s", im.err); im.logout ();
	Imap bad; CHECK (!bad.connect ("127.0.0.1", port ("IMAP_PORT", 10143), SEC_NONE, "me@onyx.test", "tok-9", true), "bad token refused");
	Smtp s; CHECK (s.connect ("127.0.0.1", port ("SMTP_PORT", 10587), SEC_NONE, "me@onyx.test", "tok-1", true), "xoauth2 smtp: %s", s.err); s.quit ();
}

static void test_tls ()
{
	int tp = port ("IMAPS_PORT", 0); if (!tp) return;
	Imap im; CHECK (!im.connect ("localhost", tp, SEC_TLS, "me@onyx.test", "secret", false) && strstr (im.err, "not trusted"), "self-signed refused: [%s]", im.err);
	Imap ok; ok.c.verify = false;
	CHECK (ok.connect ("localhost", tp, SEC_TLS, "me@onyx.test", "secret", false), "tls (no check): %s", ok.err);
	SelectInfo si; CHECK (ok.select ("INBOX", &si) && si.exists > 0, "tls select");
	ok.logout ();
}

// (on a thread of its own: the stand-in kernel's main-thread sleeps play a script)
static int run (void *)
{
	test_util ();
	test_mime ();
	test_imap ();
	test_pop3 ();
	test_smtp ();
	test_oauth ();
	test_tls ();
	return 0;
}
int main ()
{
	int tid = kapi_thread_create (run, 0, 1 << 20, "test");
	if (tid > 0) kapi_thread_join (tid, KAPI_WAIT_FOREVER, 0);
	printf ("mail: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
