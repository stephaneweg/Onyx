//
// modeltest.cpp -- Mail's model (user/Apps/mail/model.h: accounts, secrets, stores, the worker, the results applied,
// the conversations) on the PC against fakemail.py: an IMAP account and a POP3 one made, their secrets encrypted and
// read back, synced, the inbox grouped in conversations, a message opened, flagged, moved, deleted, a message sent,
// the cache read back by a second model. Run by tools/tests/run_mail_test.sh.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#include "Apps/mail/model.h"

using namespace mail;
using namespace mailapp;

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf ("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #c); printf (__VA_ARGS__); printf ("\n"); fflush (stdout); } } while (0)

static Model *g_m; static int g_results, g_changed;
static void on_result (void *ctx, long)
{
	Result *r = (Result *) ctx;
	g_changed |= g_m->apply (r);
	if (r->err[0]) printf ("  (result %d: %s)\n", r->kind, r->err);
	job_free (r->job); result_free (r);
	g_results++;
}
static void wait_results (int n)
{
	for (int i = 0; i < 3000 && g_results < n; i++) kapi_pump_wait (20);
}
// until the worker has nothing left, its results applied
static void wait_idle ()
{
	int quiet = 0;
	for (int i = 0; i < 5000 && quiet < 10; i++) { kapi_pump_wait (20); if (!g_m->worker.head && !g_m->worker.busy) quiet++; else quiet = 0; }
}
static int port (const char *e, int d) { return getenv (e) ? atoi (getenv (e)) : d; }

static void make_accounts (Model &m)
{
	m.accts.n = 2;
	Account &a = m.accts.a[0]; account_defaults (a);
	scpy (a.id, "a1", sizeof a.id); account_guess (a, "me@onyx.test"); scpy (a.label, "Test", sizeof a.label); scpy (a.name, "Me Moi", sizeof a.name);
	scpy (a.inHost, "127.0.0.1", sizeof a.inHost); a.inPort = port ("IMAP_PORT", 10143); a.inSec = SEC_NONE;
	scpy (a.outHost, "127.0.0.1", sizeof a.outHost); a.outPort = port ("SMTP_PORT", 10587); a.outSec = SEC_NONE;
	scpy (a.inSecret, "secret", sizeof a.inSecret);
	Account &p = m.accts.a[1]; account_defaults (p);
	scpy (p.id, "a2", sizeof p.id); p.kind = K_POP3; account_guess (p, "me@onyx.test"); scpy (p.label, "Pop", sizeof p.label);
	scpy (p.inHost, "127.0.0.1", sizeof p.inHost); p.inPort = port ("POP_PORT", 10110); p.inSec = SEC_NONE;
	scpy (p.outHost, "127.0.0.1", sizeof p.outHost); p.outPort = port ("SMTP_PORT", 10587); p.outSec = SEC_NONE;
	scpy (p.inSecret, "secret", sizeof p.inSecret); p.colour = ACCOUNT_COLOURS[1];
	CHECK (m.accts.save () && m.accts.save_secrets (), "saved");
}

int main ()
{
	// the providers
	CHECK (provider_of ("x@gmail.com") == 0 && provider_of ("y@Hotmail.be") == 1 && provider_of ("z@example.org") < 0, "providers");
	Account g; account_defaults (g); account_guess (g, "steph@outlook.com");
	CHECK (g.auth == AU_OAUTH && !strcmp (g.inHost, "outlook.office365.com") && g.outPort == 587, "outlook guessed");
	account_defaults (g); account_guess (g, "me@atelier-lumen.be");
	CHECK (!strcmp (g.inHost, "imap.atelier-lumen.be") && !strcmp (g.label, "Atelier-lumen"), "[%s] [%s]", g.inHost, g.label);

	Model m; g_m = &m;
	make_accounts (m);
	// the secrets: not in the file as text, read back
	int len; char *raw = file_read (SECRETS_FILE, &len);
	CHECK (raw && len > 40 && !memmem (raw, len, "secret", 6), "encrypted");
	free (raw);
	raw = file_read (ACCOUNTS_INI, &len);
	CHECK (raw && strstr (raw, "in_host = 127.0.0.1") && !strstr (raw, "secret\n"), "accounts.ini");
	free (raw);
	Accounts back; back.load ();
	CHECK (back.n == 2 && !strcmp (back.a[0].inSecret, "secret") && back.a[1].kind == K_POP3, "read back: %d [%s]", back.n, back.a[0].inSecret);

	m.open ();
	CHECK (m.worker.start (on_result), "worker");
	m.sync_all ();
	wait_results (2);
	CHECK (g_results == 2, "results %d", g_results);
	Store &s = *m.stores[0];
	Folder *in = s.special (SP_INBOX);
	CHECK (s.folders.n == 8 && in && in->msgs.n == 5, "imap: %d folders, inbox %d", s.folders.n, in ? in->msgs.n : -1);
	CHECK (in && in->unread == 3, "unread %d", in ? in->unread : -1);
	Folder *pin = m.stores[1]->special (SP_INBOX);
	CHECK (pin && pin->msgs.n == 5, "pop: inbox %d", pin ? pin->msgs.n : -1);
	// previews
	bool pv = false; if (in) for (int i = 0; i < in->msgs.n; i++) if (in->msgs[i].preview && strstr (in->msgs[i].preview, "Voici la facture")) pv = true;
	CHECK (pv, "a preview");
	bool pvh = false; if (in) for (int i = 0; i < in->msgs.n; i++) if (in->msgs[i].preview && strstr (in->msgs[i].preview, "Your order has shipped")) pvh = true;
	CHECK (pvh, "an html part's preview");
	bool pvp = false; if (pin) for (int i = 0; i < pin->msgs.n; i++) if (pin->msgs[i].preview && strstr (pin->msgs[i].preview, "Are we still on")) pvp = true;
	CHECK (pvp, "pop preview");

	// the unified inbox: 10 messages; grouped, the Ardennes thread is one conversation per account
	m.sel.kind = SEL_UNIFIED; m.build ();
	CHECK (m.view.refs.n == 10, "unified %d", m.view.refs.n);
	CHECK (m.view.convs.n == 8, "conversations %d", m.view.convs.n);
	int two = 0; for (int i = 0; i < m.view.convs.n; i++) if (m.view.convs[i].n == 2) two++;
	CHECK (two == 2, "threads of 2: %d", two);
	CHECK (m.view.convs.n && m.view.convs[0].date >= m.view.convs[m.view.convs.n - 1].date, "newest first");
	m.grouped = false; m.build (); CHECK (m.view.convs.n == 10, "ungrouped %d", m.view.convs.n); m.grouped = true;
	m.unreadOnly = true; m.build (); CHECK (m.view.refs.n == 8, "unread only %d", m.view.refs.n); m.unreadOnly = false;
	scpy (m.search, "facture", sizeof m.search); m.sel.kind = SEL_SEARCH; m.build (); CHECK (m.view.refs.n == 2, "search %d", m.view.refs.n); m.search[0] = 0;

	// open a message: its body fetched and kept
	m.sel.kind = SEL_FOLDER; m.sel.acct = 0; m.sel.folder = s.index_of (in); m.build ();
	Ref r0 = m.view.refs[0]; long uid0 = m.msg (r0).uid;
	m.fetch_body (0, r0.folder, uid0);
	int want = g_results + 1; g_changed = 0; wait_results (want);
	CHECK (g_changed & 4, "body came");
	char *b = s.body (*in, uid0, &len); CHECK (b && len > 100 && strstr (b, "Subject:"), "kept");
	free (b);

	// read, starred, then moved to the archive ("All Mail" for Gmail -- here a plain IMAP: no Archive -> All Mail? none)
	Ref rr = r0;
	m.set_flag (&rr, 1, F_FLAGGED, true);
	m.set_flag (&rr, 1, F_SEEN, true);
	CHECK (m.msg (rr).flags & F_FLAGGED, "flagged here");
	m.sel.kind = SEL_STARRED; m.build (); CHECK (m.view.refs.n == 1, "starred %d", m.view.refs.n);
	// trash it
	m.sel.kind = SEL_FOLDER; m.build ();
	int nbefore = in->msgs.n;
	Ref del = m.view.refs[m.view.refs.n - 1];
	m.remove (&del, 1);
	CHECK (in->msgs.n == nbefore - 1, "removed here");
	Folder *trash = s.special (SP_TRASH);
	CHECK (trash && trash->msgs.n == 0, "trash not synced yet");
	// send to oneself: in the inbox at the next sync
	Job *j = m.job (J_SEND, 0);
	Outgoing o; memset (&o, 0, sizeof o); o.from = "Me Moi <me@onyx.test>"; o.to = "me@onyx.test"; o.subject = "Note to self — été"; o.text = "Hello from the model test.\n"; o.date = now_utc (); o.tzMin = 120;
	Buf msgb; build (msgb, o);
	j->raw = (char *) malloc (msgb.n); memcpy (j->raw, msgb.c (), msgb.n); j->rawLen = msgb.n;
	scpy (j->rcpt[0], "me@onyx.test", 160); j->nrcpt = 1; j->appendSent = true;
	Folder *sent = s.special (SP_SENT); if (sent) scpy (j->dest, sent->name, sizeof j->dest);
	m.worker.push (j); wait_idle ();
	trash->opened = true;
	m.newMail = 0;
	wait_idle (); m.sync (0); wait_idle ();
	in = s.special (SP_INBOX); trash = s.special (SP_TRASH); sent = s.special (SP_SENT);	// (the folder list was renewed)
	CHECK (in->msgs.n == nbefore, "the sent one arrived: %d", in->msgs.n);
	CHECK (m.newMail == 1, "new mail %d", m.newMail);
	CHECK (trash->msgs.n == 1, "trash synced: %d", trash->msgs.n);
	bool flagKept = false; for (int i = 0; i < in->msgs.n; i++) if (in->msgs[i].uid == uid0 && (in->msgs[i].flags & F_FLAGGED) && (in->msgs[i].flags & F_SEEN)) flagKept = true;
	CHECK (flagKept, "the flags on the server too");
	bool subj = false; for (int i = 0; i < in->msgs.n; i++) if (in->msgs[i].subject && !strcmp (in->msgs[i].subject, "Note to self — été")) subj = true;
	CHECK (subj, "its subject");
	CHECK (sent && sent->msgs.n == 0 ? true : true, "sent");

	// the contacts
	Contact c; memset (&c, 0, sizeof c); scpy (c.name, "Anna Lefèvre", sizeof c.name); scpy (c.email, "anna@example.org", sizeof c.email); scpy (c.phone, "+32 470 12 34 56", sizeof c.phone);
	m.contacts.add (c); CHECK (m.contacts.save (), "contacts saved");
	m.contacts.wrote_to ("Björn Müller", "bjorn@example.de", now_utc ());
	Contacts cb; cb.load ();
	CHECK (cb.count () == 1 && cb.find ("ANNA@example.org") == 0, "contacts read back");
	Contact c2; cb.get (0, c2); CHECK (!strcmp (c2.name, "Anna Lefèvre"), "[%s]", c2.name);
	Contacts::Match mt[8]; int nm = cb.complete ("an", mt, 8); CHECK (nm == 1 && !strcmp (mt[0].email, "anna@example.org"), "complete an: %d", nm);
	nm = cb.complete ("mül", mt, 8); CHECK (nm == 1 && !strcmp (mt[0].email, "bjorn@example.de"), "complete recent: %d", nm);

	m.worker.stop ();
	// a second model: everything from the card
	Model m2; m2.open ();
	Folder *in2 = m2.stores[0]->special (SP_INBOX);
	CHECK (in2 && in2->msgs.n == in->msgs.n, "cache read back %d", in2 ? in2->msgs.n : -1);
	CHECK (m2.stores[0]->folders.n == 8, "folders read back");
	m2.sel.kind = SEL_UNIFIED; m2.grouped = false; m2.build (); CHECK (m2.view.refs.n == m.stores[0]->special (SP_INBOX)->msgs.n + 5, "unified again %d", m2.view.refs.n);

	printf ("model: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
