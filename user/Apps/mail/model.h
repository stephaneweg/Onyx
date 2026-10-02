//
// Apps/mail/model.h -- Mail's state, apart from its window: the accounts and their stores, the contacts, the worker;
// the jobs made from what the user does (sync, open, flag, move, delete, send), the results applied (the new
// messages, the flags, the deletions, the tokens); the lists the middle column shows -- a folder, the unified inbox
// (every account's Inbox), the starred messages, a search -- grouped in conversations (Gmail's thread id, else the
// References / In-Reply-To chain) or one by one.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _mail_model_h
#define _mail_model_h

#include "Apps/mail/sync.h"
#include "Apps/mail/contacts.h"

namespace mailapp {

enum { SEL_UNIFIED, SEL_STARRED, SEL_FOLDER, SEL_SEARCH };
struct Selection { int kind; int acct; int folder; };

struct Ref { short acct, folder; int msg; };		// a message: Model::stores[acct].folders[folder].msgs[msg]
struct Conv { int first, n; long long date; bool unread, flagged, attach; int acctMask; };
struct View
{
	Vec<Ref> refs;					// the messages, conversation after conversation (newest first inside? oldest first)
	Vec<Conv> convs;				// newest first
	void clear () { refs.clear (); convs.clear (); }
};

// a conversation's key: Gmail's thread, else the first of the References, else In-Reply-To, else its own id
static unsigned long long conv_key (const Msg &m)
{
	if (m.gmThread) return m.gmThread;
	const char *k = 0; int n = 0;
	if (m.refs && *m.refs) { const char *a = strchr (m.refs, '<'), *b = a ? strchr (a, '>') : 0; if (a && b) { k = a; n = (int) (b - a + 1); } }
	if (!k && m.irt && *m.irt) { k = m.irt; n = (int) strlen (k); }
	if (!k && m.msgid && *m.msgid) { k = m.msgid; n = (int) strlen (k); }
	if (!k) return 0x8000000000000000ULL | (unsigned long long) m.uid;
	unsigned long long h = 1469598103934665603ULL;
	for (int i = 0; i < n; i++) h = (h ^ (unsigned char) k[i]) * 1099511628211ULL;
	return h | 0x4000000000000000ULL;
}

struct Model
{
	Accounts accts;
	Store *stores[12];
	Contacts contacts;
	Worker worker;
	Selection sel;
	bool grouped;
	bool unreadOnly;
	char search[200];
	View view;
	int newMail;					// new messages in the inboxes since the last look (the notification)
	char lastError[300];
	long long lastCheck[12];

	Model () : grouped (true), unreadOnly (false), newMail (0) { for (int i = 0; i < 12; i++) { stores[i] = 0; lastCheck[i] = 0; } sel.kind = SEL_UNIFIED; sel.acct = sel.folder = 0; search[0] = lastError[0] = 0; }
	~Model () { for (int i = 0; i < 12; i++) delete stores[i]; }

	void open ()
	{
		accts.load ();
		for (int i = 0; i < accts.n; i++) open_store (i);
		contacts.load ();
	}
	void open_store (int i)
	{
		delete stores[i];
		stores[i] = new Store;
		stores[i]->open (&accts.a[i]);
		Folder *in = stores[i]->special (SP_INBOX);
		if (in) { stores[i]->load_index (*in); in->opened = true; }
		for (int k = 0; k < stores[i]->folders.n; k++) if (stores[i]->folders[k].special == SP_DRAFTS || stores[i]->folders[k].special == SP_SENT) stores[i]->load_index (stores[i]->folders[k]);
	}
	int acct_index (const char *id) const { for (int i = 0; i < accts.n; i++) if (!strcmp (accts.a[i].id, id)) return i; return -1; }

	// ---- jobs ----
	Job *job (int kind, int a)
	{
		Job *j = new Job; memset (j, 0, sizeof *j);
		j->kind = kind; j->acct = accts.a[a]; scpy (j->root, stores[a]->root, sizeof j->root);
		return j;
	}
	// new mail looked for: the Inbox and the folders looked at (all = the folder list too)
	void sync (int a, bool listFolders = true, int onlyFolder = -1)
	{
		if (a < 0 || a >= accts.n) return;
		if (onlyFolder < 0 && worker.pending_sync (accts.a[a].id)) return;
		Store &s = *stores[a];
		Job *j = job (J_SYNC, a);
		j->listFolders = listFolders && accts.a[a].kind == K_IMAP;
		if (accts.a[a].kind == K_POP3)
		{
			Folder *in = s.special (SP_INBOX);
			if (in) { scpy (j->dir, in->dir, sizeof j->dir); j->popNext = in->uidnext > 0 ? in->uidnext : s.max_uid (*in) + 1; }
			int len; char p[96]; snprintf (p, sizeof p, "%s/pop.tsv", s.root);
			j->popKnown = file_read (p, &len);
			if (!j->popKnown) j->popKnown = sdup ("\n");
		}
		else
		{
			j->snaps = (Snap *) calloc (s.folders.n ? s.folders.n : 1, sizeof (Snap)); j->nsnaps = s.folders.n;
			for (int i = 0; i < s.folders.n; i++)
			{
				Folder &f = s.folders[i]; Snap &sn = j->snaps[i];
				scpy (sn.name, f.name, sizeof sn.name);
				sn.sync = !f.noselect && (onlyFolder >= 0 ? i == onlyFolder : (f.special == SP_INBOX || f.special == SP_SENT || f.opened));	// (Sent: the conversations show the replies)
				if (!sn.sync) continue;
				s.load_index (f);
				sn.uidvalidity = f.uidvalidity; sn.maxUid = s.max_uid (f); sn.minUid = s.min_uid (f);
				sn.uids = (long *) malloc (sizeof (long) * (f.msgs.n ? f.msgs.n : 1)); sn.nuids = f.msgs.n;
				for (int k = 0; k < f.msgs.n; k++) sn.uids[k] = f.msgs[k].uid;
			}
		}
		lastCheck[a] = now_utc ();
		worker.push (j);
	}
	void sync_all () { for (int a = 0; a < accts.n; a++) sync (a); }
	// the periodic look (each account's check_minutes)
	void tick ()
	{
		long long now = now_utc ();
		for (int a = 0; a < accts.n; a++)
		{
			int m = accts.a[a].checkMinutes;
			if (m > 0 && now - lastCheck[a] >= m * 60LL) sync (a, false);
		}
	}
	void fetch_body (int a, int f, long uid)
	{
		Job *j = job (J_BODY, a); Folder &F = stores[a]->folders[f];
		scpy (j->folder, F.name, sizeof j->folder); scpy (j->dir, F.dir, sizeof j->dir); j->uid = uid;
		worker.push (j);
	}

	// ---- results ----
	// what changed for the window (bits): 1 the lists, 2 the folders, 4 a body came, 8 an error, 16 an account's tokens
	int apply (Result *r)
	{
		int a = acct_index (r->acctId);
		int changed = 0;
		if (r->err[0]) { snprintf (lastError, sizeof lastError, "%s: %s", a >= 0 ? accts.a[a].label : "Mail", r->err); changed |= 8; }
		if (a < 0) return changed;
		Store &s = *stores[a];
		if (r->tokens) { Account &x = accts.a[a]; scpy (x.access, r->access, sizeof x.access); scpy (x.refresh, r->refresh, sizeof x.refresh); x.expires = r->expires; accts.save_secrets (); changed |= 16; }
		if (r->folderList && r->nfolders) { s.set_folders (r->folders, r->nfolders); for (int i = 0; i < s.folders.n; i++) if (s.folders[i].special == SP_INBOX) s.load_index (s.folders[i]); changed |= 2; }
		for (int i = 0; i < r->nfr; i++)
		{
			FolderRes &fr = r->fr[i];
			if (!fr.ok) continue;
			Folder *f = s.find (fr.name); if (!f) continue;
			s.load_index (*f);
			if (fr.reset) s.drop_all (*f);
			f->uidvalidity = fr.si.uidvalidity; if (fr.si.uidnext) f->uidnext = fr.si.uidnext; f->exists = fr.si.exists;
			for (int k = 0; k < fr.gone.n; k++) s.remove (*f, fr.gone[k]);
			for (int k = 0; k < fr.fuid.n; k++) { Msg *m = s.msg (*f, fr.fuid[k]); if (m && m->flags != fr.fflags[k]) { m->flags = fr.fflags[k]; f->dirty = true; } }
			int fresh = 0, before = f->msgs.n;
			for (int k = 0; k < fr.added.n; k++)
			{
				if (s.msg (*f, fr.added[k].uid)) { msg_free (fr.added[k]); continue; }
				if (!(fr.added[k].flags & F_SEEN)) fresh++;
				s.insert (*f, fr.added[k]);
				memset (&fr.added[k], 0, sizeof (Msg));		// (moved into the store)
			}
			fr.added.n = 0;
			if (f->special == SP_INBOX && fresh && before > 0) newMail += fresh;		// (not the first look)
			Store::count (*f);
			if (f->dirty) s.save_index (*f);
			changed |= 1;
		}
		if (r->popKnown) { char p[96]; snprintf (p, sizeof p, "%s/pop.tsv", s.root); mkdirs (s.root); kapi_save_file (p, r->popKnown, (unsigned) strlen (r->popKnown)); }
		if (r->kind == J_SYNC) s.save_folders ();
		if (r->kind == J_BODY && r->ok) changed |= 4;
		return changed;
	}

	// ---- what the user does to messages ----
	// the uids of a list of refs in one folder, "3,5,9"
	static void uid_list (const Vec<Ref> &refs, int from, int n, Store **st, Buf &out)
	{
		for (int i = from; i < from + n; i++) { const Ref &r = refs[i]; if (out.n) out.addc (','); out.addf ("%ld", st[r.acct]->folders[r.folder].msgs[r.msg].uid); }
	}
	// flags on some messages (+ / -): done here at once, then on the server
	void set_flag (const Ref *refs, int n, unsigned flag, bool on)
	{
		for (int i = 0; i < n; i++)
		{
			const Ref &r = refs[i]; Store &s = *stores[r.acct]; Folder &f = s.folders[r.folder]; Msg &m = f.msgs[r.msg];
			bool has = (m.flags & flag) != 0; if (has == on) continue;
			if (on) m.flags |= flag; else m.flags &= ~flag;
			f.dirty = true;
			if (accts.a[r.acct].kind == K_IMAP)
			{
				Job *j = job (J_FLAGS, r.acct); scpy (j->folder, f.name, sizeof j->folder);
				snprintf (j->uids, sizeof j->uids, "%ld", m.uid);
				snprintf (j->change, sizeof j->change, "%cFLAGS.SILENT (%s)", on ? '+' : '-', flag == F_SEEN ? "\\Seen" : flag == F_FLAGGED ? "\\Flagged" : flag == F_ANSWERED ? "\\Answered" : "$Forwarded");
				worker.push (j);
			}
		}
		for (int a = 0; a < accts.n; a++) for (int k = 0; k < stores[a]->folders.n; k++) { Folder &f = stores[a]->folders[k]; if (f.dirty) { Store::count (f); stores[a]->save_index (f); } }
	}
	// moved to another folder of the same account (special: SP_ARCHIVE, SP_TRASH, SP_JUNK; or a folder's index)
	bool move (const Ref *refs, int n, int special, int destFolder = -1)
	{
		bool any = false;
		for (int i = n - 1; i >= 0; i--)
		{
			const Ref &r = refs[i]; Store &s = *stores[r.acct]; Folder &f = s.folders[r.folder];
			Folder *d = destFolder >= 0 ? &s.folders[destFolder] : s.special (special);
			if (!d && special == SP_ARCHIVE && accts.a[r.acct].provider == PV_GMAIL) d = s.special (SP_ALL);
			if (d == &f) continue;
			Msg &m = f.msgs[r.msg]; long uid = m.uid;
			if (accts.a[r.acct].kind == K_IMAP)
			{
				Job *j = job (special == SP_TRASH && !d ? J_DELETE : J_MOVE, r.acct);
				scpy (j->folder, f.name, sizeof j->folder); snprintf (j->uids, sizeof j->uids, "%ld", uid);
				if (d) scpy (j->dest, d->name, sizeof j->dest);
				worker.push (j);
			}
			else if (d)
			{	// POP3: the card's folders -- the message's file moved over
				int len; char *raw = s.body (f, uid, &len);
				s.load_index (*d);
				Msg copy = m; copy.uid = d->uidnext > 0 ? d->uidnext : s.max_uid (*d) + 1; d->uidnext = copy.uid + 1;
				copy.from = sdup (m.from); copy.to = sdup (m.to); copy.cc = sdup (m.cc); copy.replyTo = sdup (m.replyTo); copy.subject = sdup (m.subject);
				copy.msgid = sdup (m.msgid); copy.irt = sdup (m.irt); copy.refs = sdup (m.refs); copy.preview = sdup (m.preview);
				s.insert (*d, copy);
				if (raw) { s.keep_body (*d, copy.uid, raw, len); free (raw); }
				Store::count (*d); s.save_index (*d);
			}
			s.remove (f, uid);
			s.save_index (f);
			any = true;
		}
		for (int a = 0; a < accts.n; a++) stores[a]->save_folders ();
		return any;
	}
	// deleted: to the Trash, or for good when already there
	void remove (const Ref *refs, int n)
	{
		for (int i = n - 1; i >= 0; i--)
		{
			const Ref &r = refs[i]; Store &s = *stores[r.acct]; Folder &f = s.folders[r.folder];
			if (f.special == SP_TRASH || !s.special (SP_TRASH))
			{
				long uid = f.msgs[r.msg].uid;
				if (accts.a[r.acct].kind == K_IMAP) { Job *j = job (J_DELETE, r.acct); scpy (j->folder, f.name, sizeof j->folder); snprintf (j->uids, sizeof j->uids, "%ld", uid); worker.push (j); }
				s.remove (f, uid); s.save_index (f);
			}
			else move (&r, 1, SP_TRASH);
		}
	}

	// ---- the middle column's list ----
	static bool has_ci (const char *s, const char *w) { return s && ifind (s, w) != 0; }
	bool matches (const Msg &m) const
	{
		if (unreadOnly && (m.flags & F_SEEN)) return false;
		if (!search[0]) return true;
		// every word of the search somewhere (who, to whom, the subject, the text's start)
		char w[200]; const char *p = search;
		while (*p)
		{
			while (*p == ' ') p++;
			int k = 0; while (*p && *p != ' ' && k < 199) w[k++] = *p++;
			w[k] = 0; if (!k) break;
			if (!has_ci (m.from, w) && !has_ci (m.to, w) && !has_ci (m.cc, w) && !has_ci (m.subject, w) && !has_ci (m.preview, w)) return false;
		}
		return true;
	}
	void add_folder (Vec<Ref> &out, int a, int f, bool starredOnly)
	{
		Store &s = *stores[a]; Folder &F = s.folders[f];
		s.load_index (F);
		for (int i = 0; i < F.msgs.n; i++)
		{
			const Msg &m = F.msgs[i];
			if (m.flags & F_DELETED) continue;
			if (starredOnly && !(m.flags & F_FLAGGED)) continue;
			if (!matches (m)) continue;
			Ref &r = out.push (); r.acct = (short) a; r.folder = (short) f; r.msg = i;
		}
	}
	const Msg &msg (const Ref &r) const { return stores[r.acct]->folders[r.folder].msgs[r.msg]; }
	void build ()
	{
		view.clear ();
		Vec<Ref> all;
		switch (sel.kind)
		{
		case SEL_UNIFIED: for (int a = 0; a < accts.n; a++) { Folder *in = stores[a]->special (SP_INBOX); if (in) add_folder (all, a, stores[a]->index_of (in), false); } break;
		case SEL_STARRED: for (int a = 0; a < accts.n; a++) for (int f = 0; f < stores[a]->folders.n; f++) { int sp = stores[a]->folders[f].special; if (sp == SP_INBOX || sp == SP_SENT || sp == SP_ARCHIVE || sp == SP_FLAGGED || stores[a]->folders[f].opened) { if (sp == SP_ALL || sp == SP_FLAGGED) continue; add_folder (all, a, f, true); } } break;
		case SEL_SEARCH: for (int a = 0; a < accts.n; a++) for (int f = 0; f < stores[a]->folders.n; f++) { int sp = stores[a]->folders[f].special; if (sp == SP_ALL || sp == SP_JUNK || sp == SP_TRASH) continue; if (stores[a]->folders[f].loaded) add_folder (all, a, f, false); } break;
		default: if (sel.acct >= 0 && sel.acct < accts.n && sel.folder >= 0 && sel.folder < stores[sel.acct]->folders.n) add_folder (all, sel.acct, sel.folder, false); break;
		}
		// grouped: a conversation's replies from Sent shown with it (as Gmail does) -- not in a folder's own list of Sent
		if (grouped && all.n && sel.kind != SEL_SEARCH && !(sel.kind == SEL_FOLDER && stores[sel.acct]->folders[sel.folder].special == SP_SENT))
		{
			int n0 = all.n;
			unsigned long long *have = (unsigned long long *) malloc (sizeof (unsigned long long) * n0);
			for (int i = 0; i < n0; i++) have[i] = conv_key (msg (all[i])) ^ ((unsigned long long) all[i].acct << 56);
			for (int a = 0; a < accts.n; a++)
			{
				Folder *s = stores[a]->special (SP_SENT); if (!s) continue;
				int fi = stores[a]->index_of (s);
				stores[a]->load_index (*s);
				for (int i = 0; i < s->msgs.n; i++)
				{
					unsigned long long k = conv_key (s->msgs[i]) ^ ((unsigned long long) a << 56);
					for (int q = 0; q < n0; q++) if (have[q] == k) { Ref &r = all.push (); r.acct = (short) a; r.folder = (short) fi; r.msg = i; break; }
				}
			}
			free (have);
		}
		// grouped: by (account, key); the conversations newest first, their messages oldest first
		int n = all.n;
		unsigned long long *keys = (unsigned long long *) malloc (sizeof (unsigned long long) * (n ? n : 1));
		for (int i = 0; i < n; i++) { const Msg &m = msg (all[i]); keys[i] = grouped ? conv_key (m) ^ ((unsigned long long) all[i].acct << 56) : (unsigned long long) i; }
		// the order: by date, newest first
		int *ord = (int *) malloc (sizeof (int) * (n ? n : 1));
		for (int i = 0; i < n; i++) ord[i] = i;
		sort_by_date (all, ord, n);
		char *taken = (char *) calloc (n + 1, 1);
		for (int oi = 0; oi < n; oi++)
		{
			int i = ord[oi]; if (taken[i]) continue;
			Conv &c = view.convs.push (); c.first = view.refs.n; c.n = 0; c.date = msg (all[i]).date;
			// its members, oldest first
			int start = view.refs.n;
			for (int oj = n - 1; oj >= oi; oj--)
			{
				int k = ord[oj]; if (taken[k] || keys[k] != keys[i]) continue;
				taken[k] = 1; view.refs.push (all[k]); c.n++;
				const Msg &m = msg (all[k]);
				if (!(m.flags & F_SEEN)) c.unread = true;
				if (m.flags & F_FLAGGED) c.flagged = true;
				if (m.attach) c.attach = true;
				c.acctMask |= 1 << all[k].acct;
			}
			(void) start;
		}
		free (keys); free (ord); free (taken);
	}
	void sort_by_date (const Vec<Ref> &all, int *ord, int n) const
	{
		// a merge sort (lists of thousands)
		if (n < 2) return;
		int *tmp = (int *) malloc (sizeof (int) * n);
		for (int w = 1; w < n; w *= 2)
		{
			for (int lo = 0; lo < n; lo += 2 * w)
			{
				int mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n, a = lo, b = mid, k = lo;
				while (a < mid && b < hi) { if (msg (all[ord[a]]).date >= msg (all[ord[b]]).date) tmp[k++] = ord[a++]; else tmp[k++] = ord[b++]; }
				while (a < mid) tmp[k++] = ord[a++];
				while (b < hi) tmp[k++] = ord[b++];
			}
			memcpy (ord, tmp, sizeof (int) * n);
		}
		free (tmp);
	}
	// the counts the left column shows
	int unread_inboxes () const { int k = 0; for (int a = 0; a < accts.n; a++) { Folder *in = stores[a]->special (SP_INBOX); if (in) k += in->unread; } return k; }
};

} // namespace mailapp

#endif
