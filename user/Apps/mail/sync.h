//
// Apps/mail/sync.h -- Mail's worker thread: everything that talks to a server, one job after another, while the
// window stays live. A job is queued by the window (sync an account, fetch a message, send, change flags, move,
// delete, save a draft, check a new account's settings, Microsoft's sign-in); its result is posted back
// (kapi_post) and applied by the window's thread (main.cpp: on_result). The connections stay open between jobs
// (an account's IMAP session, its folder selected); new mail is looked for every few minutes (each account's
// check_minutes). OAuth's access tokens are renewed here before they expire.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _mail_sync_h
#define _mail_sync_h

#include "Apps/mail/store.h"
#include "mail/pop3.h"
#include "mail/smtp.h"
#include "mail/oauth.h"

namespace mailapp {

// ---- the time: UTC, from the local clock and SD:/etc/system.ini's timezone ------------------------------------------------
static int g_tzMin = -100000;
static int tz_minutes ()
{
	if (g_tzMin != -100000) return g_tzMin;
	g_tzMin = 0;
	int len; char *b = file_read ("SD:/etc/system.ini", &len);
	if (b) { const char *p = strstr (b, "\ntimezone"); if (!p && !strncmp (b, "timezone", 8)) p = b - 1; if (p) { p = strchr (p, '='); if (p) g_tzMin = atoi (p + 1); } free (b); }
	return g_tzMin;
}
static long long now_utc ()
{
	int y, mo, d, h, mi, s;
	if (!kapi_get_datetime (&y, &mo, &d, &h, &mi, &s)) return 0;
	return days_civil (y, mo, d) * 86400LL + h * 3600 + mi * 60 + s - tz_minutes () * 60LL;
}

// ---- jobs and results -----------------------------------------------------------------------------------------------------
enum { J_SYNC, J_BODY, J_FLAGS, J_MOVE, J_DELETE, J_SEND, J_DRAFT, J_CHECK, J_OAUTH_START, J_OAUTH_POLL, J_OLDER, J_PICTURE };
struct Snap { char name[200]; long uidvalidity, maxUid, minUid; long *uids; int nuids; bool sync; };
struct Job
{
	int kind; Account acct;			// (a copy: the window may change its own meanwhile)
	char root[64];				// the account's store (SD:/mail/<id>)
	Snap *snaps; int nsnaps;		// J_SYNC: the folders and what is known of them
	bool listFolders;
	char folder[200], dest[200], dir[16], part[24];
	long uid, deleteUid;			// (J_DRAFT: the draft it replaces)
	char uids[2048], change[80];
	char *raw; int rawLen;			// J_SEND / J_DRAFT: the message
	char rcpt[64][160]; int nrcpt;
	bool appendSent;
	// POP3: the UIDLs known, when first seen; the inbox's next uid
	char *popKnown; long popNext;
	DeviceCode dc;				// J_OAUTH_POLL
	Job *next;
};
struct FolderRes
{
	char name[200]; SelectInfo si; bool reset, ok;
	Vec<Msg> added;
	Vec<long> fuid; Vec<unsigned> fflags;	// flags now
	Vec<long> gone;
};
struct Result
{
	int kind; char acctId[24]; bool ok; char err[300];
	bool folderList; ImapFolder *folders; int nfolders;
	FolderRes *fr; int nfr;
	char folder[200]; long uid;
	char *popKnown;				// POP3: the UIDL list now
	bool tokens; char access[4096], refresh[4096]; long long expires;
	DeviceCode dc; int oauthState;		// (J_OAUTH_POLL: 1 signed in, 0 waiting, -1 failed)
	char *data; int dataLen;		// J_PICTURE: the picture's bytes (its url: Job::uids)
	Job *job;				// (given back: freed by the window)
};
static void result_free (Result *r)
{
	if (!r) return;
	free (r->folders);
	for (int i = 0; i < r->nfr; i++) { FolderRes &f = r->fr[i]; for (int k = 0; k < f.added.n; k++) msg_free (f.added[k]); f.added.~Vec (); f.fuid.~Vec (); f.fflags.~Vec (); f.gone.~Vec (); }
	free (r->fr); free (r->popKnown); free (r->data);
	delete r;
}
static void job_free (Job *j)
{
	if (!j) return;
	for (int i = 0; i < j->nsnaps; i++) free (j->snaps[i].uids);
	free (j->snaps); free (j->raw); free (j->popKnown);
	delete j;
}

// ---- the worker ------------------------------------------------------------------------------------------------------------
typedef void (*OnResult) (void *ctx, long value);
struct Worker
{
	volatile int lock; Job *head, *tail; int ev; int tid;
	volatile int cancel; volatile int quit;
	OnResult onResult;
	struct Session { char id[24]; Imap *imap; char selected[200]; char access[4096]; char refresh[4096]; long long expires; };
	Session ses[12]; int nses;
	volatile int busy;			// (a job running: the window's spinner)
	char current[120];			// what it does ("Checking Gmail...")

	Worker () : lock (0), head (0), tail (0), ev (-1), tid (-1), cancel (0), quit (0), onResult (0), nses (0), busy (0) { current[0] = 0; }

	void push (Job *j)
	{
		j->next = 0;
		kapi_lock (&lock);
		if (tail) tail->next = j; else head = j;
		tail = j;
		kapi_unlock (&lock);
		if (ev > 0) kapi_event_set (ev);
	}
	Job *pop ()
	{
		kapi_lock (&lock);
		Job *j = head;
		if (j) { head = j->next; if (!head) tail = 0; }
		kapi_unlock (&lock);
		return j;
	}
	bool pending_sync (const char *id)
	{
		kapi_lock (&lock);
		bool p = false; for (Job *j = head; j; j = j->next) if (j->kind == J_SYNC && !strcmp (j->acct.id, id)) p = true;
		kapi_unlock (&lock);
		return p;
	}
	Session &session (const char *id)
	{
		for (int i = 0; i < nses; i++) if (!strcmp (ses[i].id, id)) return ses[i];
		Session &s = ses[nses < 12 ? nses++ : 11]; memset (&s, 0, sizeof s); scpy (s.id, id, sizeof s.id);
		return s;
	}
	void drop (Session &s) { if (s.imap) { s.imap->c.close (); delete s.imap; s.imap = 0; } s.selected[0] = 0; }

	void post (Result *r) { kapi_post (onResult, r, 0); }

	// ---- signing in ----
	// the secret to sign in with: the password, or a fresh access token (refreshed when near its end)
	bool secret (Job &j, Result &r, bool out, const char **sec)
	{
		Account &a = j.acct;
		if (a.auth != AU_OAUTH) { *sec = out && a.outSecret[0] ? a.outSecret : a.inSecret; return true; }
		Session &s = session (a.id);
		if (!s.refresh[0]) { scpy (s.refresh, a.refresh, sizeof s.refresh); scpy (s.access, a.access, sizeof s.access); s.expires = a.expires; }
		long long now = now_utc ();
		if (!s.access[0] || s.expires - 120 < now)
		{
			OAuth o; oauth_load (o.cfg); o.cancel = &cancel;
			Tokens t; memset (&t, 0, sizeof t); scpy (t.refresh, s.refresh, sizeof t.refresh);
			bool signedOut;
			if (!o.refresh (t, now, &signedOut)) { snprintf (r.err, sizeof r.err, "%s", signedOut ? "Microsoft asks you to sign in again (Accounts and settings)." : o.err); return false; }
			scpy (s.access, t.access, sizeof s.access); scpy (s.refresh, t.refresh, sizeof s.refresh); s.expires = t.expires;
			r.tokens = true; scpy (r.access, t.access, sizeof r.access); scpy (r.refresh, t.refresh, sizeof r.refresh); r.expires = t.expires;
		}
		*sec = s.access;
		return true;
	}
	Imap *imap (Job &j, Result &r)
	{
		Session &s = session (j.acct.id);
		if (s.imap && s.imap->c.open_) return s.imap;
		drop (s);
		const char *sec; if (!secret (j, r, false, &sec)) return 0;
		Imap *im = new Imap;
		im->c.cancel = &cancel; im->c.verify = j.acct.verify;
		snprintf (current, sizeof current, "Connecting to %s...", j.acct.label);
		if (!im->connect (j.acct.inHost, j.acct.inPort, j.acct.inSec, j.acct.inUser, sec, j.acct.auth == AU_OAUTH)) { scpy (r.err, im->err, sizeof r.err); delete im; return 0; }
		s.imap = im;
		return im;
	}
	bool select (Session &s, const char *name, SelectInfo *si, Result &r)
	{
		if (!s.imap->select (name, si)) { scpy (r.err, s.imap->err, sizeof r.err); if (!s.imap->c.open_) drop (s); s.selected[0] = 0; return false; }
		scpy (s.selected, name, sizeof s.selected);
		return true;
	}
	// a folder made current (SELECT when another one is)
	bool ensure (Session &s, const char *name, Result &r)
	{
		if (!strcmp (s.selected, name)) return true;
		SelectInfo si; return select (s, name, &si, r);
	}

	// ---- the jobs ----
	struct EnvCollect { Vec<Msg> *out; long above; };
	static void on_env (void *ctx, const Envelope &e) { EnvCollect &c = *(EnvCollect *) ctx; if (e.uid > c.above) msg_from_env (c.out->push (), e); }
	struct FlagCollect { FolderRes *fr; };
	static void on_flags (void *ctx, long uid, unsigned f) { FolderRes &fr = *((FlagCollect *) ctx)->fr; fr.fuid.push (uid); fr.fflags.push (f); }
	struct PrevCollect { Vec<Msg> *msgs; };
	static void on_part (void *ctx, long uid, const char *d, int n)
	{
		Vec<Msg> &m = *((PrevCollect *) ctx)->msgs;
		for (int i = 0; i < m.n; i++) if (m[i].uid == uid) { char p[240]; preview_of (d, n, m[i].textEnc, m[i].textCs, m[i].textHtml, p, sizeof p); free (m[i].preview); m[i].preview = sdup (p); break; }
	}
	void previews (Imap &im, Vec<Msg> &msgs)
	{
		// one FETCH per part path ("1", "1.1"...), up to 200 messages at a time
		char *done = (char *) calloc (msgs.n + 1, 1);
		for (int i = 0; i < msgs.n; i++)
		{
			if (done[i] || !msgs[i].textPart[0]) continue;
			char part[24]; scpy (part, msgs[i].textPart, sizeof part);
			Buf uids; int k = 0;
			for (int j = i; j < msgs.n && k < 200; j++) if (!done[j] && !strcmp (msgs[j].textPart, part)) { if (uids.n) uids.addc (','); uids.addf ("%ld", msgs[j].uid); done[j] = 1; k++; }
			PrevCollect pc = { &msgs };
			if (!im.fetch_parts (uids.c (), part, 700, on_part, &pc)) break;
		}
		free (done);
	}
	void sync_imap (Job &j, Result &r)
	{
		Imap *im = imap (j, r); if (!im) return;
		Session &s = session (j.acct.id);
		if (j.listFolders)
		{
			snprintf (current, sizeof current, "Checking %s...", j.acct.label);
			if (!im->list (&r.folders, &r.nfolders)) { scpy (r.err, im->err, sizeof r.err); if (!im->c.open_) drop (s); return; }
			r.folderList = true;
			// the first look: no folder known yet -- the Inbox at least
			// (and Sent: the conversations show one's replies)
			const char *want[2] = { "INBOX", 0 };
			for (int i = 0; i < r.nfolders; i++) if (r.folders[i].special == SP_SENT && !r.folders[i].noselect) want[1] = r.folders[i].name;
			for (int w = 0; w < 2; w++)
			{
				if (!want[w]) continue;
				bool have = false; for (int i = 0; i < j.nsnaps; i++) if (j.snaps[i].sync && ieq (j.snaps[i].name, want[w])) have = true;
				if (have) continue;
				j.snaps = (Snap *) realloc (j.snaps, sizeof (Snap) * (j.nsnaps + 1));
				Snap &sn = j.snaps[j.nsnaps++]; memset (&sn, 0, sizeof sn); scpy (sn.name, want[w], sizeof sn.name); sn.sync = true;
			}
		}
		r.fr = (FolderRes *) calloc (j.nsnaps ? j.nsnaps : 1, sizeof (FolderRes)); r.nfr = 0;
		for (int i = 0; i < j.nsnaps; i++)
		{
			Snap &sn = j.snaps[i];
			if (!sn.sync) continue;
			if (cancel || quit) break;
			FolderRes &fr = r.fr[r.nfr++];
			scpy (fr.name, sn.name, sizeof fr.name);
			if (!s.imap || !select (s, sn.name, &fr.si, r)) { if (!s.imap) return; continue; }
			fr.reset = sn.uidvalidity && fr.si.uidvalidity != sn.uidvalidity;
			bool known = sn.maxUid > 0 && !fr.reset;
			EnvCollect ec = { &fr.added, known ? sn.maxUid : 0 };
			bool ok;
			if (known) ok = fr.si.uidnext <= 0 || fr.si.uidnext > sn.maxUid + 1 ? im->fetch_envelopes (sn.maxUid + 1, 0, on_env, &ec) : true;
			else ok = fr.si.exists > 0 ? im->fetch_envelopes_seq (fr.si.exists > 100 ? fr.si.exists - 99 : 1, 0, on_env, &ec) : true;
			if (!ok) { scpy (r.err, im->err, sizeof r.err); if (!im->c.open_) { drop (s); return; } continue; }
			if (known && sn.nuids)
			{	// the flags of what is known; what is not there any more
				FlagCollect fc = { &fr };
				if (im->fetch_flags (sn.minUid, on_flags, &fc))
					for (int k = 0; k < sn.nuids; k++)
					{
						bool there = false; for (int q = 0; q < fr.fuid.n; q++) if (fr.fuid[q] == sn.uids[k]) { there = true; break; }
						if (!there) fr.gone.push (sn.uids[k]);
					}
			}
			if (fr.added.n) { snprintf (current, sizeof current, "%s: %d new...", j.acct.label, fr.added.n); previews (*im, fr.added); }
			fr.ok = true;
		}
		r.ok = !r.err[0];
	}
	void sync_pop (Job &j, Result &r)
	{
		const char *sec; if (!secret (j, r, false, &sec)) return;
		Pop3 p; p.c.cancel = &cancel; p.c.verify = j.acct.verify;
		snprintf (current, sizeof current, "Checking %s...", j.acct.label);
		if (!p.connect (j.acct.inHost, j.acct.inPort, j.acct.inSec, j.acct.inUser, sec, j.acct.auth == AU_OAUTH)) { scpy (r.err, p.err, sizeof r.err); return; }
		PopMsg *m; int n;
		if (!p.list (&m, &n)) { scpy (r.err, p.err, sizeof r.err); p.quit (); return; }
		r.fr = (FolderRes *) calloc (1, sizeof (FolderRes)); r.nfr = 1;
		FolderRes &fr = r.fr[0]; scpy (fr.name, "INBOX", sizeof fr.name); fr.si.uidvalidity = 1;
		Buf known; if (j.popKnown) known.add (j.popKnown);
		long long now = now_utc ();
		long next = j.popNext > 0 ? j.popNext : 1;
		char dir[96]; snprintf (dir, sizeof dir, "%s/%s", j.root, j.dir); mkdirs (dir);
		Buf still;
		for (int i = 0; i < n && !cancel; i++)
		{
			const char *u = m[i].uidl[0] ? m[i].uidl : 0;
			char key[90]; snprintf (key, sizeof key, "\n%s\t", u ? u : "");
			const char *hit = u ? strstr (known.c (), key) : 0;
			long long seen = hit ? atoll (strchr (hit + 1, '\t') + 1) : now;
			if (!hit)
			{
				snprintf (current, sizeof current, "%s: message %d of %d...", j.acct.label, i + 1, n);
				Buf raw; if (!p.retr (m[i].num, raw)) { scpy (r.err, p.err, sizeof r.err); break; }
				Envelope e; envelope_of (raw.c (), raw.n, e); e.uid = next++;
				Msg &mm = fr.added.push (); msg_from_env (mm, e);
				char pv[240]; preview_from_raw (raw.c (), raw.n, pv, sizeof pv); free (mm.preview); mm.preview = sdup (pv);
				char path[128]; snprintf (path, sizeof path, "%s/%ld.eml", dir, e.uid); kapi_save_file (path, raw.c (), (unsigned) raw.n);
			}
			bool del = !j.acct.popKeep || (j.acct.popDays > 0 && now - seen > j.acct.popDays * 86400LL);
			if (del && u) p.dele (m[i].num);
			else if (u) still.addf ("\n%s\t%lld", u, seen);
		}
		free (m);
		p.quit ();
		still.addc ('\n');
		r.popKnown = sdup (still.c ());
		fr.si.uidnext = next;
		fr.ok = !r.err[0];
		r.ok = fr.ok;
	}
	void body (Job &j, Result &r)
	{
		scpy (r.folder, j.folder, sizeof r.folder); r.uid = j.uid;
		Imap *im = imap (j, r); if (!im) return;
		Session &s = session (j.acct.id);
		if (!ensure (s, j.folder, r)) return;
		snprintf (current, sizeof current, "Opening the message...");
		Buf b;
		if (!im->fetch_body (j.uid, "", 0, b)) { scpy (r.err, im->err, sizeof r.err); if (!im->c.open_) drop (s); return; }
		char dir[96]; snprintf (dir, sizeof dir, "%s/%s", j.root, j.dir); mkdirs (dir);
		char path[128]; snprintf (path, sizeof path, "%s/%ld.eml", dir, j.uid);
		kapi_save_file (path, b.c (), (unsigned) b.n);
		r.ok = true;
	}
	void change (Job &j, Result &r)
	{
		scpy (r.folder, j.folder, sizeof r.folder);
		if (j.acct.kind == K_POP3) { r.ok = true; return; }		// (the card's folders: done by the window)
		Imap *im = imap (j, r); if (!im) return;
		Session &s = session (j.acct.id);
		if (!ensure (s, j.folder, r)) return;
		bool ok = true;
		if (j.kind == J_FLAGS) ok = im->store (j.uids, j.change);
		else if (j.kind == J_MOVE) ok = im->move (j.uids, j.dest);
		else if (j.kind == J_DELETE) ok = j.dest[0] ? im->move (j.uids, j.dest) : im->expunge (j.uids);
		if (!ok) { scpy (r.err, im->err, sizeof r.err); if (!im->c.open_) drop (s); return; }
		r.ok = true;
	}
	void send (Job &j, Result &r)
	{
		const char *sec; if (!secret (j, r, true, &sec)) return;
		Smtp sm; sm.c.cancel = &cancel; sm.c.verify = j.acct.verify;
		snprintf (current, sizeof current, "Sending...");
		const char *user = j.acct.outUser[0] ? j.acct.outUser : j.acct.inUser;
		if (!sm.connect (j.acct.outHost, j.acct.outPort, j.acct.outSec, user, sec, j.acct.auth == AU_OAUTH)) { scpy (r.err, sm.err, sizeof r.err); return; }
		const char *rc[64]; for (int i = 0; i < j.nrcpt; i++) rc[i] = j.rcpt[i];
		bool ok = sm.send (j.acct.email, rc, j.nrcpt, j.raw, j.rawLen);
		if (!ok) { scpy (r.err, sm.err, sizeof r.err); sm.quit (); return; }
		if (sm.err[0]) scpy (r.err, sm.err, sizeof r.err);			// (sent, some refused)
		sm.quit ();
		r.ok = true;
		// a copy in Sent (Gmail and Outlook keep it themselves)
		if (j.appendSent && j.acct.kind == K_IMAP && j.dest[0])
		{
			Result r2; memset (&r2, 0, sizeof r2);
			Imap *im = imap (j, r2);
			if (im && !im->append (j.dest, "(\\Seen)", j.raw, j.rawLen)) { if (!im->c.open_) drop (session (j.acct.id)); }
		}
	}
	void draft (Job &j, Result &r)
	{
		if (j.acct.kind != K_IMAP || !j.dest[0]) { r.ok = true; return; }
		Imap *im = imap (j, r); if (!im) return;
		Session &s = session (j.acct.id);
		if (!im->append (j.dest, "(\\Seen \\Draft)", j.raw, j.rawLen)) { scpy (r.err, im->err, sizeof r.err); if (!im->c.open_) drop (s); return; }
		if (j.deleteUid > 0)
		{
			char u[24]; snprintf (u, sizeof u, "%ld", j.deleteUid);
			if (ensure (s, j.dest, r)) im->expunge (u);
		}
		r.ok = true;
	}
	// a new account's settings tried: incoming, then outgoing
	void check (Job &j, Result &r)
	{
		const char *sec; if (!secret (j, r, false, &sec)) return;
		snprintf (current, sizeof current, "Trying %s...", j.acct.inHost);
		if (j.acct.kind == K_POP3)
		{
			Pop3 p; p.c.cancel = &cancel; p.c.verify = j.acct.verify;
			if (!p.connect (j.acct.inHost, j.acct.inPort, j.acct.inSec, j.acct.inUser, sec, j.acct.auth == AU_OAUTH)) { snprintf (r.err, sizeof r.err, "Incoming (POP3): %s", p.err); return; }
			p.quit ();
		}
		else
		{
			Imap im; im.c.cancel = &cancel; im.c.verify = j.acct.verify;
			if (!im.connect (j.acct.inHost, j.acct.inPort, j.acct.inSec, j.acct.inUser, sec, j.acct.auth == AU_OAUTH)) { snprintf (r.err, sizeof r.err, "Incoming (IMAP): %s", im.err); return; }
			im.logout ();
		}
		if (!secret (j, r, true, &sec)) return;
		snprintf (current, sizeof current, "Trying %s...", j.acct.outHost);
		Smtp sm; sm.c.cancel = &cancel; sm.c.verify = j.acct.verify;
		const char *user = j.acct.outUser[0] ? j.acct.outUser : j.acct.inUser;
		if (!sm.connect (j.acct.outHost, j.acct.outPort, j.acct.outSec, user, sec, j.acct.auth == AU_OAUTH)) { snprintf (r.err, sizeof r.err, "Outgoing (SMTP): %s", sm.err); return; }
		sm.quit ();
		r.ok = true;
	}
	void oauth (Job &j, Result &r)
	{
		OAuth o; oauth_load (o.cfg); o.cancel = &cancel;
		if (j.kind == J_OAUTH_START)
		{
			snprintf (current, sizeof current, "Asking Microsoft for a code...");
			if (!o.start (r.dc)) { scpy (r.err, o.err, sizeof r.err); return; }
			r.ok = true; return;
		}
		r.dc = j.dc;
		Tokens t; memset (&t, 0, sizeof t);
		// asked every interval seconds until the user has signed in, refused, the code expired or the wizard closed
		unsigned t0 = kapi_get_ticks ();
		snprintf (current, sizeof current, "Waiting for Microsoft...");
		for (;;)
		{
			r.oauthState = o.poll (r.dc, t, now_utc ());
			if (r.oauthState != 0 || cancel || quit) break;
			if ((kapi_get_ticks () - t0) / 100 > (unsigned) r.dc.expiresIn) { r.oauthState = -1; scpy (r.err, "The code has expired: try again.", sizeof r.err); return; }
			for (int k = 0; k < r.dc.interval * 10 && !cancel && !quit; k++) kapi_msleep (100);
		}
		if (cancel || quit) { scpy (r.err, "Cancelled.", sizeof r.err); r.oauthState = -1; return; }
		if (r.oauthState < 0) { scpy (r.err, o.err, sizeof r.err); return; }
		if (r.oauthState == 1) { r.tokens = true; scpy (r.access, t.access, sizeof r.access); scpy (r.refresh, t.refresh, sizeof r.refresh); r.expires = t.expires; }
		r.ok = true;
	}
	// a remote picture (the message's sender's server: asked only once the user allowed it)
	void picture (Job &j, Result &r)
	{
		snprintf (current, sizeof current, "Fetching the pictures...");
		Buf b; int st = http_get (j.uids, b, 6 << 20, r.err, sizeof r.err, &cancel);
		if (st != 200) { if (st > 0) snprintf (r.err, sizeof r.err, "The picture's server said %d.", st); return; }
		r.data = b.take (); r.dataLen = b.n; r.ok = true;
	}
	void run (Job *j)
	{
		Result *r = new Result; memset (r, 0, sizeof *r);
		r->kind = j->kind; scpy (r->acctId, j->acct.id, sizeof r->acctId); r->job = j;
		busy = 1;
		switch (j->kind)
		{
		case J_SYNC: case J_OLDER: if (j->acct.kind == K_POP3) sync_pop (*j, *r); else sync_imap (*j, *r); break;
		case J_BODY: body (*j, *r); break;
		case J_FLAGS: case J_MOVE: case J_DELETE: change (*j, *r); break;
		case J_SEND: send (*j, *r); break;
		case J_DRAFT: draft (*j, *r); break;
		case J_CHECK: check (*j, *r); break;
		case J_OAUTH_START: case J_OAUTH_POLL: oauth (*j, *r); break;
		case J_PICTURE: picture (*j, *r); break;
		}
		busy = 0; current[0] = 0;
		post (r);
	}
	static int thread (void *arg)
	{
		Worker &w = *(Worker *) arg;
		while (!w.quit)
		{
			Job *j = w.pop ();
			if (!j) { kapi_event_wait (w.ev, 1000); continue; }
			w.cancel = 0;
			w.run (j);
		}
		for (int i = 0; i < w.nses; i++) { if (w.ses[i].imap) { w.ses[i].imap->c.timeoutMs = 2000; w.ses[i].imap->logout (); } w.drop (w.ses[i]); }
		return 0;
	}
	bool start (OnResult cb)
	{
		onResult = cb;
		ev = kapi_event_create (0, 0);
		tid = kapi_thread_create (thread, this, 512 * 1024, "mail-sync");
		return tid > 0;
	}
	void stop ()
	{
		quit = 1; cancel = 1;
		if (ev > 0) kapi_event_set (ev);
		if (tid > 0) kapi_thread_join (tid, 5000, 0);
	}
};

} // namespace mailapp

#endif
