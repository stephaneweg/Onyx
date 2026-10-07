//
// client.h -- the Telegram client under the window: the sign-in (the phone number, the code, the
// cloud password through SRP, a new account's name), the sessions with the data centres (the home
// one and those the profile photos are on: an exported authorization each), and the model the window
// draws -- the users, the groups and channels, the conversations and their messages, who is online,
// who is typing -- kept up to date by the updates the server pushes (and getDifference after a gap).
//
// The window reads the model and watches `rev` (bumped at every change); it acts through the calls
// below (sendCode, signIn, open, send...). Nothing here draws or blocks. Portable: the platform's
// tgplat.h (Onyx) or tools/tests/telegram/tgplat_host.h (the PC's tests).
//
// The session (the authorization keys, the home data centre, the account) is kept in <dir>/session.dat;
// the profile photos in <dir>/cache/<photo id>.jpg.
//
// MIT licence.
//
#ifndef TG_CLIENT_H
#define TG_CLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mtproto.h"

namespace tg {

// ---- the model ---------------------------------------------------------------------------------------

enum PeerType { P_USER = 0, P_CHAT = 1, P_CHANNEL = 2 };
inline long long pkey (int t, long long id) { return id * 4 + t; }
inline int ptype (long long k) { return (int) (k & 3); }
inline long long pid (long long k) { return k >> 2; }

enum Status { ST_UNKNOWN, ST_ONLINE, ST_OFFLINE, ST_RECENTLY, ST_WEEK, ST_MONTH, ST_LONGAGO };
enum Media { M_NONE, M_PHOTO, M_VIDEO, M_VOICE, M_AUDIO, M_FILE, M_STICKER, M_GIF, M_GEO, M_CONTACT, M_POLL, M_WEB, M_OTHER, M_SERVICE };
enum Service { SV_NONE, SV_CREATE, SV_TITLE, SV_PHOTO, SV_ADD, SV_LEFT, SV_REMOVE, SV_JOINED_LINK, SV_PIN, SV_CALL, SV_JOINED_TG, SV_SCREENSHOT, SV_OTHER };

inline char *sdup (const char *s, int n = -1)
{
	if (!s) s = "";
	if (n < 0) n = (int) strlen (s);
	char *p = (char *) malloc ((size_t) n + 1);
	if (p) { memcpy (p, s, (size_t) n); p[n] = 0; }
	return p;
}
inline void sset (char *&dst, const char *s) { free (dst); dst = sdup (s); }

struct User
{
	long long id, access;
	char *first, *last, *username, *phone;
	int status, wasOnline, expires;
	long long photoId;
	int photoDc;
	bool self, contact, bot, deleted;
};

struct Chat
{
	long long id, access;
	char *title;
	int kind;			// 1 a group, 2 a supergroup, 3 a channel
	long long photoId;
	int photoDc;
	int members;
	bool left;
};

struct Msg
{
	int id;
	long long from;			// the sender (a peer key), 0 none
	int date;
	bool out, edited, pending, failed;
	unsigned char media, service;
	char *text;			// the text (UTF-8), "" none
	char *extra;			// a sticker's emoji, a file's name, a service's title
	long long svcUser;		// (a service's user: who was added / removed)
	int replyTo;
	long long randomId;		// (ours, until the server gives its id)
	// a photo: the server's (its id, access hash, file reference, data centre, the size fetched: its type, w, h),
	// or ours being sent (local: its file in the cache; progress 0..100)
	long long photoId, photoAccess;
	char *photoRef; int photoRefN, photoDc;
	short pw, ph;
	char thumb[4];
	char *local;
	signed char progress;
};

struct Conv
{
	long long peer;
	Msg *m;
	int n, cap;
	bool loaded, loading, complete, pinned, inList;
	int unread, readOutMax, readInMax, topId, topDate;
	long long typingUser;
	long long typingUntil;		// (ms)
	int typingKind;			// 0 typing, 1 recording a voice message...
	unsigned rev;
};

// An open-addressing map of 64-bit keys to indexes (+1; 0 empty).
struct IdMap
{
	long long *k; int *v; int cap, n;
	IdMap () : k (0), v (0), cap (0), n (0) {}
	~IdMap () { free (k); free (v); }
	static unsigned h (long long x) { unsigned long long z = (unsigned long long) x * 0x9E3779B97F4A7C15ull; return (unsigned) (z >> 32); }
	int get (long long key) const
	{
		if (!cap) return -1;
		for (unsigned i = h (key) & (unsigned) (cap - 1); v[i]; i = (i + 1) & (unsigned) (cap - 1)) if (k[i] == key) return v[i] - 1;
		return -1;
	}
	void put (long long key, int idx)
	{
		if ((n + 1) * 2 > cap) grow ();
		unsigned i = h (key) & (unsigned) (cap - 1);
		while (v[i] && k[i] != key) i = (i + 1) & (unsigned) (cap - 1);
		if (!v[i]) n++;
		k[i] = key; v[i] = idx + 1;
	}
	void grow ()
	{
		int oc = cap; long long *ok = k; int *ov = v;
		cap = cap ? cap * 2 : 256;
		k = (long long *) calloc ((size_t) cap, sizeof *k);
		v = (int *) calloc ((size_t) cap, sizeof *v);
		n = 0;
		for (int i = 0; i < oc; i++) if (ov[i]) put (ok[i], ov[i] - 1);
		free (ok); free (ov);
	}
};

template <class T> struct List
{
	T **a; int n, cap; IdMap map;
	List () : a (0), n (0), cap (0) {}
	T *find (long long key) const { int i = map.get (key); return i < 0 ? 0 : a[i]; }
	T *add (long long key)
	{
		T *x = find (key);
		if (x) return x;
		if (n == cap) { cap = cap ? cap * 2 : 64; a = (T **) realloc (a, sizeof (T *) * (size_t) cap); }
		x = (T *) calloc (1, sizeof (T));
		a[n] = x; map.put (key, n); n++;
		return x;
	}
};

// ---- the client ----------------------------------------------------------------------------------------

enum AuthState
{
	AS_START,		// reading the session
	AS_NEED_API,		// no api_id / api_hash yet (config.ini)
	AS_PHONE,		// the phone number to give
	AS_CODE,		// the code sent: to give
	AS_PASSWORD,		// the two-step verification's password
	AS_SIGNUP,		// a new account: its name
	AS_READY		// signed in
};

enum ReqKind
{
	R_NONE, R_SENDCODE, R_SIGNIN, R_SIGNUP, R_GETPASSWORD, R_CHECKPASSWORD, R_SELF, R_STATE, R_DIALOGS, R_CONTACTS,
	R_HISTORY, R_SEND, R_READ, R_DIFF, R_STATUS, R_TYPING, R_EXPORT, R_IMPORT, R_FILE, R_CONFIG, R_LOGOUT, R_OTHER,
	R_PART, R_SENDMEDIA
};

enum { MAXDC = 6, MAXPEND = 256 };

struct DcAddr { char ip[48]; int port; };

class Client;

class DcListener : public mt::Listener
{
public:
	Client *c; int dc;
	void onResult (int req, const tl::Val &v) override;
	void onError (int req, int code, const char *m) override;
	void onUpdates (const tl::Val &v) override;
	void onKeyReady () override;
	void onNewSession () override;
};

class Client
{
public:
	// what the window reads
	AuthState state;
	bool busy;				// a sign-in step is on its way
	char error[160];			// the last error to show ("" none): the server's words (PHONE_CODE_INVALID...)
	int floodWait;				// (FLOOD_WAIT's seconds, with the error)
	char phone[32], codeType[16];		// the code went by: "app", "sms", "call"...
	int codeLength;
	char passwordHint[64];
	unsigned rev;				// bumped at every change of the model
	long long selfId;
	List<User> users;
	List<Chat> chats;
	List<Conv> convs;
	long long *order; int norder;		// the conversations, the pinned first then the latest (dialogs ())
	bool dialogsLoaded, contactsLoaded;
	bool online;				// the network: the home session works
	// a new message for the notifications (popped by the window)
	struct Incoming { long long peer; int id; };
	Incoming inbox[32]; int ninbox;

	// the account's settings
	int apiId;
	char apiHash[64];
	bool test;
	char dir[96];				// "SD:/apps/telegram.app/"
	const char *lang;
	bool demo = false;			// (--demo: a model made up, no network)

	Client () : state (AS_START), busy (false), floodWait (0), codeLength (0), rev (1), selfId (0), order (0), norder (0),
		    dialogsLoaded (false), contactsLoaded (false), online (false), ninbox (0), apiId (0), test (false), lang ("en"),
		    m_home (2), m_npend (0), m_pts (0), m_qts (0), m_date (0), m_haveState (false), m_lastStatus (0),
		    m_wantOnline (true), m_lastTyping (0), m_lastTypingPeer (0), m_photoBusy (false), m_photoQ (0), m_nphotoQ (0),
		    m_fileBuf (0), m_fileN (0), m_randomSeq (1), m_needDiff (false), m_saveDirty (false)
	{
		error[0] = phone[0] = codeType[0] = passwordHint[0] = apiHash[0] = 0;
		m_codeHash[0] = 0;
		strcpy (dir, "./");
		for (int i = 0; i < MAXDC; i++) { m_l[i].c = this; m_l[i].dc = i; m_s[i].dc = i; m_s[i].L = &m_l[i]; m_auth[i] = false; m_exporting[i] = false; m_addr[i].ip[0] = 0; m_addr[i].port = 443; }
		memset (m_pend, 0, sizeof m_pend);
		m_pwAlgoOk = false;
	}

	// ---- start, stop --------------------------------------------------------------------------

	// Reads the session; then signs in again by itself (AS_READY) or waits for the phone number.
	void begin ()
	{
		tl::load ();
		tgc::rng_init ();
		defaultAddrs ();
		loadSession ();
		if (!apiId || !apiHash[0]) { state = AS_NEED_API; rev++; return; }
		connectHome ();
		if (m_s[m_home].haveKey && selfId) { state = AS_READY; afterLogin (true); }
		else state = AS_PHONE;
		rev++;
	}
	void setApi (int id, const char *hash, bool testServers)
	{
		apiId = id; snprintf (apiHash, sizeof apiHash, "%s", hash); test = testServers;
		defaultAddrs ();
		for (int i = 0; i < MAXDC; i++) m_s[i].init.api_id = apiId;
		if (state == AS_NEED_API) { connectHome (); state = AS_PHONE; rev++; }
	}
	// When the app ends: offline, the session kept.
	void end ()
	{
		if (state == AS_READY && m_s[m_home].ready ()) setOnline (false);
		saveSession ();
	}

	// Each tick.
	void tick ()
	{
		long long t = tg_unix_ms ();
		for (int i = 1; i < MAXDC && !demo; i++)
		{
			mt::Session &s = m_s[i];
			if (s.broken () && t >= s.retryAt ()) s.start (tg_new_transport ());
			s.poll ();
		}
		bool on = demo || (m_s[m_home].ready () && !m_s[m_home].broken ());
		if (on != online)
		{
			if (on && state == AS_READY && m_haveState) m_needDiff = true;	// (back after a cut: what was missed)
			online = on; rev++;
		}
		if (state == AS_READY && on)
		{
			if (m_needDiff && m_haveState) { m_needDiff = false; getDifference (); }
			if (m_wantOnline && t - m_lastStatus > 240000) setOnline (true);
			if (!m_photoBusy && m_nphotoQ) nextPhoto ();
			if (!m_upBusy && m_nup) nextPart ();
		}
		// the typing marks run out
		for (int i = 0; i < convs.n; i++)
		{
			Conv *c = convs.a[i];
			if (c->typingUntil && t > c->typingUntil) { c->typingUntil = 0; c->typingUser = 0; c->rev++; rev++; }
		}
		if (m_saveDirty) { m_saveDirty = false; saveSession (); }
	}

	// ---- signing in -------------------------------------------------------------------------

	void sendCode (const char *number)
	{
		int o = 0;
		for (const char *p = number; *p && o < (int) sizeof phone - 1; p++) if ((*p >= '0' && *p <= '9') || (*p == '+' && o == 0)) phone[o++] = *p;
		phone[o] = 0;
		error[0] = 0;
		tl::Arena a;
		tl::Val *q = tl::make (a, "auth.sendCode");
		q->set ("phone_number", tl::S (a, phone[0] == '+' ? phone + 1 : phone));
		q->set ("api_id", tl::I (apiId));
		q->set ("api_hash", tl::S (a, apiHash));
		q->set ("settings", tl::obj (a, "codeSettings"));
		call (m_home, *q, R_SENDCODE);
		busy = true; rev++;
	}
	void signIn (const char *code)
	{
		error[0] = 0;
		tl::Arena a;
		tl::Val *q = tl::make (a, "auth.signIn");
		q->set ("phone_number", tl::S (a, phone[0] == '+' ? phone + 1 : phone));
		q->set ("phone_code_hash", tl::S (a, m_codeHash));
		q->set ("phone_code", tl::S (a, code));
		call (m_home, *q, R_SIGNIN);
		busy = true; rev++;
	}
	void signUp (const char *first, const char *last)
	{
		error[0] = 0;
		tl::Arena a;
		tl::Val *q = tl::make (a, "auth.signUp");
		q->set ("phone_number", tl::S (a, phone[0] == '+' ? phone + 1 : phone));
		q->set ("phone_code_hash", tl::S (a, m_codeHash));
		q->set ("first_name", tl::S (a, first));
		q->set ("last_name", tl::S (a, last ? last : ""));
		call (m_home, *q, R_SIGNUP);
		busy = true; rev++;
	}
	// The cloud password: account.getPassword's parameters (asked when the code said
	// SESSION_PASSWORD_NEEDED), then SRP.
	void checkPassword (const char *password)
	{
		error[0] = 0;
		if (!m_pwAlgoOk) { snprintf (error, sizeof error, "PASSWORD_PARAMS"); rev++; return; }
		unsigned char A[256], M1[32];
		if (!tgc::srp (password, m_salt1, m_salt1n, m_salt2, m_salt2n, m_pwG, m_pwP, 256, m_srpB, m_srpBn, A, M1))
		{
			snprintf (error, sizeof error, "PASSWORD_PARAMS"); rev++; return;
		}
		tl::Arena a;
		tl::Val *in = tl::make (a, "inputCheckPasswordSRP");
		in->set ("srp_id", tl::L (m_srpId));
		in->set ("A", tl::S (a, A, 256));
		in->set ("M1", tl::S (a, M1, 32));
		tl::Val *q = tl::make (a, "auth.checkPassword");
		q->set ("password", *in);
		call (m_home, *q, R_CHECKPASSWORD);
		busy = true; rev++;
	}
	void backToPhone () { state = AS_PHONE; error[0] = 0; busy = false; rev++; }
	void logOut ()
	{
		tl::Arena a;
		if (m_s[m_home].ready ()) call (m_home, *tl::make (a, "auth.logOut"), R_LOGOUT);
		forget ();
	}

	// ---- the conversations ------------------------------------------------------------------

	// The conversations in their order (pinned first, then the latest message's date).
	void dialogs ()
	{
		free (order);
		order = (long long *) malloc (sizeof (long long) * (size_t) (convs.n + 1));
		norder = 0;
		for (int i = 0; i < convs.n; i++) if (convs.a[i]->inList) order[norder++] = convs.a[i]->peer;
		// (insertion sort: a few hundred)
		for (int i = 1; i < norder; i++)
		{
			long long k = order[i]; Conv *ck = convs.find (k);
			int j = i - 1;
			while (j >= 0 && before (ck, convs.find (order[j]))) { order[j + 1] = order[j]; j--; }
			order[j + 1] = k;
		}
	}
	Conv *conv (long long peer) { return convs.find (peer); }
	User *user (long long id) { return users.find (id); }
	Chat *chat (long long id) { return chats.find (id); }
	User *self () { return users.find (selfId); }

	// A peer's name (a user's first + last name, a chat's title) into out.
	void peerName (long long peer, char *out, int cap)
	{
		out[0] = 0;
		if (ptype (peer) == P_USER)
		{
			User *u = user (pid (peer));
			if (!u) { snprintf (out, (size_t) cap, "#%lld", pid (peer)); return; }
			if (u->deleted) { snprintf (out, (size_t) cap, "Deleted Account"); return; }
			if (u->last && u->last[0]) snprintf (out, (size_t) cap, "%s %s", u->first ? u->first : "", u->last);
			else snprintf (out, (size_t) cap, "%s", u->first && u->first[0] ? u->first : u->username ? u->username : "?");
		}
		else
		{
			Chat *c = chat (pid (peer));
			snprintf (out, (size_t) cap, "%s", c && c->title ? c->title : "?");
		}
	}

	// The conversation opened: its last messages (once), read up to the newest.
	void open (long long peer)
	{
		Conv *c = convs.add (peer);
		c->peer = peer;
		if (!c->loaded && !c->loading) loadHistory (c, 0);
		markRead (c);
	}
	void loadOlder (long long peer)
	{
		Conv *c = conv (peer);
		if (!c || c->loading || c->complete || !c->n) return;
		loadHistory (c, c->m[0].id);
	}
	void markRead (Conv *c)
	{
		int top = c->n ? c->m[c->n - 1].id : c->topId;
		if (!c->unread && top <= c->readInMax) return;
		if (!m_s[m_home].ready ()) return;
		tl::Arena a;
		tl::Val *q;
		if (ptype (c->peer) == P_CHANNEL)
		{
			q = tl::make (a, "channels.readHistory");
			q->set ("channel", inputChannel (a, pid (c->peer)));
		}
		else
		{
			q = tl::make (a, "messages.readHistory");
			q->set ("peer", inputPeer (a, c->peer));
		}
		q->set ("max_id", tl::I (top));
		call (m_home, *q, R_READ);
		c->unread = 0; c->readInMax = top; c->rev++; rev++;
	}

	// A message sent (shown at once, "pending" until the server gives its id).
	void send (long long peer, const char *text)
	{
		if (!text || !text[0]) return;
		Conv *c = convs.add (peer);
		c->peer = peer; c->inList = true;
		long long rid = tgc::random64 ();
		Msg m; memset (&m, 0, sizeof m);
		m.id = 0x7fff0000 + (m_randomSeq++ & 0xffff);	// (a local id until the server's: after the others)
		m.from = pkey (P_USER, selfId); m.out = true; m.pending = true; m.randomId = rid;
		m.date = serverTime ();
		m.text = sdup (text); m.extra = sdup ("");
		insertMsg (c, m);
		c->topDate = m.date;
		tl::Arena a;
		tl::Val *q = tl::make (a, "messages.sendMessage");
		q->set ("peer", inputPeer (a, peer));
		q->set ("message", tl::S (a, text));
		q->set ("random_id", tl::L (rid));
		call (m_home, *q, R_SEND, peer, rid);
		c->rev++; rev++;
		dialogs ();
	}
	// Typing in a conversation: told to the others (at most every 5 s).
	void typing (long long peer)
	{
		long long t = tg_unix_ms ();
		if (peer == m_lastTypingPeer && t - m_lastTyping < 5000) return;
		if (!m_s[m_home].ready () || (ptype (peer) == P_CHANNEL && isBroadcast (peer))) return;
		m_lastTyping = t; m_lastTypingPeer = peer;
		tl::Arena a;
		tl::Val *q = tl::make (a, "messages.setTyping");
		q->set ("peer", inputPeer (a, peer));
		q->set ("action", tl::obj (a, "sendMessageTypingAction"));
		call (m_home, *q, R_TYPING);
	}
	void setOnline (bool on)
	{
		m_wantOnline = on;
		m_lastStatus = tg_unix_ms ();
		if (!m_s[m_home].ready () && !demo) return;
		tl::Arena a;
		tl::Val *q = tl::make (a, "account.updateStatus");
		q->set ("offline", tl::B (!on));
		call (m_home, *q, R_STATUS);
		User *u = self ();
		if (u) { u->status = on ? ST_ONLINE : ST_OFFLINE; u->wasOnline = serverTime (); rev++; }
	}
	bool wantOnline () const { return m_wantOnline; }

	// ---- profile photos -----------------------------------------------------------------------

	// The photo's file in the cache (out), true if it is there; else it is asked for (once).
	bool photo (long long peer, char *out, int cap)
	{
		long long id = 0; int pdc = 0;
		if (ptype (peer) == P_USER) { User *u = user (pid (peer)); if (u) { id = u->photoId; pdc = u->photoDc; } }
		else { Chat *c = chat (pid (peer)); if (c) { id = c->photoId; pdc = c->photoDc; } }
		if (!id) return false;
		snprintf (out, (size_t) cap, "%scache/%llx.jpg", dir, (unsigned long long) id);
		if (m_haveFile.get (id) == 1) return true;
		if (m_haveFile.get (id) == 2) return false;	// (asked, or failed)
		int n = 0;
		unsigned char *b = tg_load (out, &n);
		if (b) { free (b); m_haveFile.put (id, 1); return true; }
		m_haveFile.put (id, 2);
		PhotoReq r; memset (&r, 0, sizeof r);
		r.peer = peer; r.id = id; r.dc = pdc;
		queuePhoto (r);
		return false;
	}

	// A message's photo in the cache (out): true if it is there (ours being sent: its own file); else it is
	// asked for (once).
	bool msgPhoto (const Msg &m, char *out, int cap)
	{
		if (m.local) { snprintf (out, (size_t) cap, "%s", m.local); return true; }
		if (!m.photoId) return false;
		snprintf (out, (size_t) cap, "%scache/m%llx%s.jpg", dir, (unsigned long long) m.photoId, m.thumb);
		int st = m_haveMsgFile.get (m.photoId);
		if (st == 1) return true;
		if (st == 2) return false;
		int n = 0;
		unsigned char *b = tg_load (out, &n);
		if (b) { free (b); m_haveMsgFile.put (m.photoId, 1); return true; }
		m_haveMsgFile.put (m.photoId, 2);
		PhotoReq r; memset (&r, 0, sizeof r);
		r.kind = 1; r.id = m.photoId; r.access = m.photoAccess; r.dc = m.photoDc;
		r.ref = (unsigned char *) malloc ((size_t) (m.photoRefN ? m.photoRefN : 1));
		if (r.ref && m.photoRefN) memcpy (r.ref, m.photoRef, (size_t) m.photoRefN);
		r.refn = m.photoRefN;
		memcpy (r.thumb, m.thumb, 4);
		queuePhoto (r);
		return false;
	}

	// ---- sending a photo --------------------------------------------------------------------------

	// A JPEG (w x h) sent to peer with its caption: shown at once (its file kept in the cache), sent in
	// parts (upload.saveFilePart), then messages.sendMedia. One photo at a time, in order.
	void sendPhoto (long long peer, const unsigned char *jpg, int n, int w, int h, const char *caption)
	{
		if (!jpg || n <= 0) return;
		Conv *c = convs.add (peer);
		c->peer = peer; c->inList = true;
		long long rid = tgc::random64 ();
		char path[160];
		snprintf (path, sizeof path, "%scache/out%llx.jpg", dir, (unsigned long long) rid);
		tg_save (path, jpg, n);
		Msg m; memset (&m, 0, sizeof m);
		m.id = 0x7fff0000 + (m_randomSeq++ & 0xffff);
		m.from = pkey (P_USER, selfId); m.out = true; m.pending = true; m.randomId = rid;
		m.date = serverTime ();
		m.media = M_PHOTO;
		m.text = sdup (caption ? caption : ""); m.extra = sdup ("");
		m.local = sdup (path);
		m.pw = (short) w; m.ph = (short) h;
		insertMsg (c, m);
		c->topDate = m.date;
		Upload u; memset (&u, 0, sizeof u);
		u.peer = peer; u.rid = rid; u.fileId = tgc::random64 ();
		u.data = (unsigned char *) malloc ((size_t) n);
		if (!u.data) return;
		memcpy (u.data, jpg, (size_t) n);
		u.n = n; u.parts = (n + PART - 1) / PART;
		u.caption = sdup (caption ? caption : "");
		m_up = (Upload *) realloc (m_up, sizeof (Upload) * (size_t) (m_nup + 1));
		m_up[m_nup++] = u;
		c->rev++; rev++;
		dialogs ();
	}

	// Any request (the tests'; R_OTHER): its answer or its error to otherFn.
	int invoke (const tl::Val &q, int dc = 0) { return call (dc ? dc : m_home, q, R_OTHER); }
	void (*otherFn) (void *ctx, int req, const tl::Val &v, int code, const char *err) = 0;
	void *otherCtx = 0;

	int serverTime () const { return m_s[m_home].serverTime (); }
	int homeDc () const { return m_home; }
	bool isBroadcast (long long peer) { Chat *c = chat (pid (peer)); return c && c->kind == 3; }

	// ---- the sessions' events (DcListener) ----------------------------------------------------

	void result (int dc, int req, const tl::Val &v)
	{
		Pend *p = pend (req);
		if (!p) return;
		Pend pe = *p;
		p->req = 0; p->body = 0;
		free (pe.body); pe.body = 0;
		switch (pe.kind)
		{
		case R_SENDCODE: gotSentCode (v); break;
		case R_SIGNIN: case R_SIGNUP: case R_CHECKPASSWORD: gotAuthorization (v); break;
		case R_GETPASSWORD: gotPassword (v); break;
		case R_SELF:
			for (int i = 0; i < v.count (); i++) addUser (v[i]);
			if (v.count () && !v[0].is ("userEmpty")) { selfId = v[0]["id"].i (); m_saveDirty = true; }
			rev++;
			break;
		case R_STATE: setState (v); break;
		case R_DIALOGS: gotDialogs (v, pe); break;
		case R_CONTACTS: gotContacts (v); break;
		case R_HISTORY: gotHistory (v, pe); break;
		case R_SEND: gotSent (v, pe); break;
		case R_DIFF: gotDifference (v); break;
		case R_EXPORT: gotExport (v, pe); break;
		case R_IMPORT: m_auth[dc] = true; m_exporting[dc] = false; m_saveDirty = true; flushDc (dc); break;
		case R_FILE: gotFile (v, pe); break;
		case R_PART: m_upBusy = false; if (m_nup) { m_up[0].next++; uploadProgress (); } break;
		case R_SENDMEDIA: m_upBusy = false; popUpload (); updates (v); break;
		case R_CONFIG: gotConfig (v); break;
		case R_READ: if (v.ok () && v["pts"].ok ()) bumpPts ((int) v["pts"].i ()); break;
		case R_OTHER: if (otherFn) otherFn (otherCtx, pe.req, v, 0, 0); break;
		default: break;
		}
	}

	void failed (int dc, int req, int code, const char *msg)
	{
		Pend *p = pend (req);
		if (!p) return;
		Pend pe = *p;
		p->req = 0; p->body = 0;		// (pe owns the body now: sent again, or freed)
		tg_log ("rpc error %d %s (request kind %d, dc%d)", code, msg, pe.kind, dc);
		// a migration: the request again on the data centre named
		int n = 0;
		if (code == 303 && (sscanf (msg, "PHONE_MIGRATE_%d", &n) == 1 || sscanf (msg, "NETWORK_MIGRATE_%d", &n) == 1 || sscanf (msg, "USER_MIGRATE_%d", &n) == 1))
		{
			if (n > 0 && n < MAXDC)
			{
				m_home = n;
				m_saveDirty = true;
				connect (n);
				if (pe.body) { m_s[n].init.api_id = apiId; repost (n, pe); return; }
			}
		}
		if (code == 303 && sscanf (msg, "FILE_MIGRATE_%d", &n) == 1 && pe.kind == R_FILE && n > 0 && n < MAXDC)
		{
			pe.dc = n; repostFile (pe); return;
		}
		free (pe.body);
		if (code == 420 && sscanf (msg, "FLOOD_WAIT_%d", &n) == 1) floodWait = n;
		if (code == 401 && (!strcmp (msg, "AUTH_KEY_UNREGISTERED") || !strcmp (msg, "SESSION_REVOKED") || !strcmp (msg, "USER_DEACTIVATED")))
		{
			if (dc == m_home) { forget (); snprintf (error, sizeof error, "%s", msg); rev++; return; }
			m_auth[dc] = false;
		}
		switch (pe.kind)
		{
		case R_SENDCODE: case R_SIGNUP: case R_CHECKPASSWORD:
			busy = false; snprintf (error, sizeof error, "%s", msg); rev++;
			break;
		case R_SIGNIN:
			busy = false;
			if (!strcmp (msg, "SESSION_PASSWORD_NEEDED"))
			{
				tl::Arena a;
				call (m_home, *tl::make (a, "account.getPassword"), R_GETPASSWORD);
				busy = true;
			}
			else snprintf (error, sizeof error, "%s", msg);
			rev++;
			break;
		case R_HISTORY: { Conv *c = conv (pe.a); if (c) { c->loading = false; c->complete = true; c->rev++; } rev++; break; }
		case R_SEND:
		{
			Conv *c = conv (pe.a);
			for (int i = 0; c && i < c->n; i++) if (c->m[i].randomId == pe.b && c->m[i].pending) { c->m[i].pending = false; c->m[i].failed = true; c->rev++; }
			snprintf (error, sizeof error, "%s", msg);
			rev++;
			break;
		}
		case R_PART: case R_SENDMEDIA:
		{
			m_upBusy = false;
			if (!m_nup) break;
			if (pe.kind == R_PART && code != 400 && m_up[0].tries++ < 3) break;	// (a part again, a few times)
			Conv *c = conv (m_up[0].peer);
			for (int i = 0; c && i < c->n; i++) if (c->m[i].randomId == m_up[0].rid && c->m[i].pending) { c->m[i].pending = false; c->m[i].failed = true; c->rev++; }
			popUpload ();
			snprintf (error, sizeof error, "%s", msg);
			rev++;
			break;
		}
		case R_FILE: case R_EXPORT: case R_IMPORT:
			m_photoBusy = false;
			if (pe.kind != R_FILE) m_exporting[pe.dc] = false;
			break;
		case R_DIFF: if (!strcmp (msg, "PERSISTENT_TIMESTAMP_INVALID")) { m_haveState = false; getState (); } break;
		case R_OTHER: if (otherFn) otherFn (otherCtx, pe.req, tl::none (), code, msg); break;
		default: break;
		}
	}

	void keyReady (int dc) { (void) dc; m_saveDirty = true; }
	void newSession (int dc) { if (dc == m_home && state == AS_READY) m_needDiff = true; }

	// The pushed updates (and the Updates a request answered).
	void updates (const tl::Val &v)
	{
		if (v.is ("updateShortMessage") || v.is ("updateShortChatMessage"))
		{
			bool chatMsg = v.is ("updateShortChatMessage");
			Msg m; memset (&m, 0, sizeof m);
			m.id = (int) v["id"].i ();
			m.out = v["out"].b ();
			m.date = (int) v["date"].i ();
			m.text = sdup (v["message"].str ()); m.extra = sdup ("");
			long long peer;
			if (chatMsg) { peer = pkey (P_CHAT, v["chat_id"].i ()); m.from = pkey (P_USER, v["from_id"].i ()); }
			else { peer = pkey (P_USER, v["user_id"].i ()); m.from = m.out ? pkey (P_USER, selfId) : peer; }
			if (v["reply_to"].ok ()) m.replyTo = (int) v["reply_to"]["reply_to_msg_id"].i ();
			newMessage (peer, m, true);
			if (!users.find (pid (m.from)) || (chatMsg && !chats.find (pid (peer)))) m_needDiff = true;	// (someone unknown: the full message)
			bumpPts ((int) v["pts"].i ());
			return;
		}
		if (v.is ("updateShort")) { update (v["update"]); return; }
		if (v.is ("updates") || v.is ("updatesCombined"))
		{
			addUsers (v["users"]); addChats (v["chats"]);
			const tl::Val &u = v["updates"];
			for (int i = 0; i < u.count (); i++) update (u[i]);
			return;
		}
		if (v.is ("updatesTooLong")) { m_needDiff = true; return; }
		if (v.is ("updateShortSentMessage")) return;	// (a request's answer: gotSent)
	}

private:
	struct Pend { int req, kind, dc; long long a, b; unsigned char *body; int n; const tl::Ctor *fn; };
	struct PhotoReq { long long peer, id, access; int dc, kind; unsigned char *ref; int refn; char thumb[4]; };	// kind 0: a profile photo, 1: a message's
	struct Upload { long long peer, rid, fileId; unsigned char *data; int n, parts, next, tries; char *caption; };
	enum { PART = 131072 };
	Upload *m_up = 0; int m_nup = 0; bool m_upBusy = false;
	IdMap m_haveMsgFile;

	mt::Session m_s[MAXDC];
	DcListener m_l[MAXDC];
	bool m_auth[MAXDC], m_exporting[MAXDC];
	DcAddr m_addr[MAXDC];
	int m_home;
	Pend m_pend[MAXPEND];
	int m_npend;
	char m_codeHash[128];
	int m_pts, m_qts, m_date;
	bool m_haveState;
	long long m_lastStatus;
	bool m_wantOnline;
	long long m_lastTyping, m_lastTypingPeer;
	// the password
	bool m_pwAlgoOk;
	unsigned char m_salt1[128], m_salt2[128], m_pwP[256], m_srpB[256];
	int m_salt1n, m_salt2n, m_pwG, m_srpBn;
	long long m_srpId;
	// the photos
	IdMap m_haveFile;
	bool m_photoBusy;
	PhotoReq *m_photoQ; int m_nphotoQ;
	PhotoReq m_curPhoto = {};
	unsigned char *m_fileBuf; int m_fileN;
	unsigned m_randomSeq;
	bool m_needDiff, m_saveDirty;

	static bool before (Conv *a, Conv *b)
	{
		if (a->pinned != b->pinned) return a->pinned;
		return a->topDate > b->topDate;
	}

	// ---- the data centres -----------------------------------------------------------------

	void defaultAddrs ()
	{
		static const char *prod[] = { "", "149.154.175.53", "149.154.167.51", "149.154.175.100", "149.154.167.91", "91.108.56.130" };
		static const char *tst[] = { "", "149.154.175.10", "149.154.167.40", "149.154.175.117", "", "" };
		for (int i = 1; i < MAXDC; i++)
		{
			if (!m_addr[i].ip[0]) { snprintf (m_addr[i].ip, sizeof m_addr[i].ip, "%s", test ? tst[i] : prod[i]); m_addr[i].port = 443; }
			m_s[i].test = test;
			m_s[i].init.api_id = apiId;
			m_s[i].init.lang = lang;
		}
	}
	void connect (int dc)
	{
		if (demo) return;
		mt::Session &s = m_s[dc];
		if (s.connected () || !m_addr[dc].ip[0]) return;
		memcpy (s.host, m_addr[dc].ip, sizeof s.host < sizeof m_addr[dc].ip ? sizeof s.host : sizeof m_addr[dc].ip);
		s.host[sizeof s.host - 1] = 0;
		s.port = m_addr[dc].port;
		s.start (tg_new_transport ());
	}
	void connectHome () { connect (m_home); }

	// A request -> its pending record (kept with its body: a migration sends it again).
	int call (int dc, const tl::Val &q, int kind, long long a = 0, long long b = 0)
	{
		if (demo) return 0;
		connect (dc);
		int slot = -1;
		for (int i = 0; i < MAXPEND; i++) if (!m_pend[i].req) { slot = i; break; }
		if (slot < 0) return 0;
		int id = m_s[dc].call (q);
		if (!id) return 0;
		Pend &p = m_pend[slot];
		free (p.body);
		memset (&p, 0, sizeof p);
		p.req = id; p.kind = kind; p.dc = dc; p.a = a; p.b = b; p.fn = q.c;
		if (kind == R_SENDCODE || kind == R_SIGNIN || kind == R_FILE || kind == R_SELF)
		{
			tl::Buf body;
			tl::encode (body, q);
			p.n = body.n; p.body = body.take ();
		}
		return id;
	}
	Pend *pend (int req)
	{
		for (int i = 0; i < MAXPEND; i++) if (m_pend[i].req == req) return &m_pend[i];
		return 0;
	}
	// (a request's body sent again, elsewhere)
	void repost (int dc, Pend &pe)
	{
		unsigned char *body = pe.body;
		int n = pe.n;
		pe.body = 0;
		int slot = -1;
		for (int i = 0; i < MAXPEND; i++) if (!m_pend[i].req) { slot = i; break; }
		int id = slot < 0 ? 0 : m_s[dc].callRaw (body, n, pe.fn);
		if (!id) { free (body); return; }
		Pend &p = m_pend[slot];
		memset (&p, 0, sizeof p);
		p.req = id; p.kind = pe.kind; p.dc = dc; p.a = pe.a; p.b = pe.b; p.fn = pe.fn;
		p.body = body; p.n = n;
	}

	// ---- signing in -----------------------------------------------------------------------

	void gotSentCode (const tl::Val &v)
	{
		busy = false;
		if (v.is ("auth.sentCodeSuccess")) { gotAuthorization (v["authorization"]); return; }
		snprintf (m_codeHash, sizeof m_codeHash, "%s", v["phone_code_hash"].str ());
		tg_log ("sentCode: %s, hash %s, length %d, next %s", v["type"].name (), m_codeHash, (int) v["type"]["length"].i (), v["next_type"].name ());
		const tl::Val &t = v["type"];
		codeLength = (int) t["length"].i ();
		const char *n = t.name ();
		snprintf (codeType, sizeof codeType, "%s",
			  strstr (n, "App") ? "app" : strstr (n, "Sms") ? "sms" : strstr (n, "Call") ? "call" : strstr (n, "Email") ? "email" : "other");
		if (v.is ("auth.sentCodePaymentRequired")) { snprintf (error, sizeof error, "PAYMENT_REQUIRED"); rev++; return; }
		state = AS_CODE; rev++;
	}

	void gotPassword (const tl::Val &v)
	{
		busy = false;
		const tl::Val &algo = v["current_algo"];
		m_pwAlgoOk = false;
		if (algo.is ("passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow") && algo["p"].len () == 256
		    && algo["salt1"].len () <= 128 && algo["salt2"].len () <= 128 && v["srp_B"].len () <= 256)
		{
			memcpy (m_salt1, algo["salt1"].s, (size_t) algo["salt1"].len ()); m_salt1n = algo["salt1"].len ();
			memcpy (m_salt2, algo["salt2"].s, (size_t) algo["salt2"].len ()); m_salt2n = algo["salt2"].len ();
			memcpy (m_pwP, algo["p"].s, 256);
			m_pwG = (int) algo["g"].i ();
			memcpy (m_srpB, v["srp_B"].s, (size_t) v["srp_B"].len ()); m_srpBn = v["srp_B"].len ();
			m_srpId = v["srp_id"].i ();
			m_pwAlgoOk = true;
		}
		snprintf (passwordHint, sizeof passwordHint, "%s", v["hint"].str ());
		state = AS_PASSWORD; rev++;
	}

	void gotAuthorization (const tl::Val &v)
	{
		busy = false;
		if (v.is ("auth.authorizationSignUpRequired")) { state = AS_SIGNUP; rev++; return; }
		if (!v.is ("auth.authorization")) { snprintf (error, sizeof error, "UNEXPECTED_ANSWER"); rev++; return; }
		addUser (v["user"]);
		selfId = v["user"]["id"].i ();
		m_auth[m_home] = true;
		state = AS_READY;
		error[0] = 0;
		saveSession ();
		afterLogin (false);
		rev++;
	}

	void afterLogin (bool resumed)
	{
		tl::Arena a;
		if (resumed)				// (is the key still signed in? and who are we)
		{
			tl::Val *q = tl::make (a, "users.getUsers");
			tl::Val ids = tl::Vec (a, 1);
			ids.v[0] = tl::obj (a, "inputUserSelf");
			q->set ("id", ids);
			call (m_home, *q, R_SELF);
		}
		call (m_home, *tl::make (a, "help.getConfig"), R_CONFIG);
		getState ();
		loadDialogs ();
		tl::Val *c = tl::make (a, "contacts.getContacts");
		c->set ("hash", tl::L (0));
		call (m_home, *c, R_CONTACTS);
		setOnline (m_wantOnline);
	}

	void forget ()
	{
		for (int i = 1; i < MAXDC; i++) { m_s[i].stop (); m_s[i].haveKey = false; m_auth[i] = false; }
		selfId = 0; state = AS_PHONE; busy = false;
		dialogsLoaded = contactsLoaded = false;
		norder = 0;
		saveSession ();
		connectHome ();
		rev++;
	}

	void gotConfig (const tl::Val &v)
	{
		const tl::Val &o = v["dc_options"];
		bool got[MAXDC] = { false };
		for (int i = 0; i < o.count (); i++)
		{
			const tl::Val &d = o[i];
			int id = (int) d["id"].i ();
			if (id <= 0 || id >= MAXDC || got[id] || d["ipv6"].b () || d["media_only"].b () || d["tcpo_only"].b () || d["cdn"].b ()) continue;
			if (!d["static"].b () && d["secret"].ok ()) continue;
			snprintf (m_addr[id].ip, sizeof m_addr[id].ip, "%s", d["ip_address"].str ());
			m_addr[id].port = (int) d["port"].i ();
			got[id] = true;
		}
		m_saveDirty = true;
	}

	// ---- the updates' state ----------------------------------------------------------------

	void getState () { tl::Arena a; call (m_home, *tl::make (a, "updates.getState"), R_STATE); }
	void setState (const tl::Val &s)
	{
		m_pts = (int) s["pts"].i (); m_qts = (int) s["qts"].i (); m_date = (int) s["date"].i ();
		m_haveState = true;
	}
	void bumpPts (int pts) { if (pts > m_pts) m_pts = pts; }
	void getDifference ()
	{
		tl::Arena a;
		tl::Val *q = tl::make (a, "updates.getDifference");
		q->set ("pts", tl::I (m_pts));
		q->set ("date", tl::I (m_date));
		q->set ("qts", tl::I (m_qts));
		call (m_home, *q, R_DIFF);
	}
	void gotDifference (const tl::Val &v)
	{
		if (v.is ("updates.differenceEmpty")) { m_date = (int) v["date"].i (); return; }
		if (v.is ("updates.differenceTooLong")) { m_pts = (int) v["pts"].i (); loadDialogs (); return; }
		addUsers (v["users"]); addChats (v["chats"]);
		const tl::Val &nm = v["new_messages"];
		for (int i = 0; i < nm.count (); i++) { Msg m; long long peer; if (readMessage (nm[i], m, peer)) newMessage (peer, m, true); }
		const tl::Val &ou = v["other_updates"];
		for (int i = 0; i < ou.count (); i++) update (ou[i]);
		bool slice = v.is ("updates.differenceSlice");
		setState (slice ? v["intermediate_state"] : v["state"]);
		if (slice) getDifference ();
	}

	// One Update.
	void update (const tl::Val &u)
	{
		const char *n = u.name ();
		if (!strcmp (n, "updateNewMessage") || !strcmp (n, "updateNewChannelMessage"))
		{
			Msg m; long long peer;
			if (readMessage (u["message"], m, peer)) newMessage (peer, m, true);
			if (!strcmp (n, "updateNewMessage")) bumpPts ((int) u["pts"].i ());
		}
		else if (!strcmp (n, "updateEditMessage") || !strcmp (n, "updateEditChannelMessage"))
		{
			Msg m; long long peer;
			if (readMessage (u["message"], m, peer)) newMessage (peer, m, false);
		}
		else if (!strcmp (n, "updateMessageID"))
		{
			int id = (int) u["id"].i (); long long rid = u["random_id"].i ();
			for (int i = 0; i < convs.n; i++)
			{
				Conv *c = convs.a[i];
				for (int k = 0; k < c->n; k++)
					if (c->m[k].pending && c->m[k].randomId == rid) { c->m[k].randomId = 0; c->m[k].id = id; c->m[k].pending = false; sortMsgs (c); c->rev++; rev++; return; }
			}
		}
		else if (!strcmp (n, "updateDeleteMessages") || !strcmp (n, "updateDeleteChannelMessages"))
		{
			const tl::Val &ids = u["messages"];
			long long only = !strcmp (n, "updateDeleteChannelMessages") ? pkey (P_CHANNEL, u["channel_id"].i ()) : 0;
			for (int i = 0; i < convs.n; i++)
			{
				Conv *c = convs.a[i];
				if (only && c->peer != only) continue;
				if (!only && ptype (c->peer) == P_CHANNEL) continue;
				for (int k = 0; k < ids.count (); k++) removeMsg (c, (int) ids[k].i ());
			}
		}
		else if (!strcmp (n, "updateUserStatus"))
		{
			User *x = user (u["user_id"].i ());
			if (x) { readStatus (x, u["status"]); rev++; }
		}
		else if (!strcmp (n, "updateUserName"))
		{
			User *x = user (u["user_id"].i ());
			if (x) { sset (x->first, u["first_name"].str ()); sset (x->last, u["last_name"].str ()); rev++; }
		}
		else if (!strcmp (n, "updateUserTyping") || !strcmp (n, "updateChatUserTyping") || !strcmp (n, "updateChannelUserTyping"))
		{
			long long peer, who;
			if (!strcmp (n, "updateUserTyping")) { peer = pkey (P_USER, u["user_id"].i ()); who = peer; }
			else
			{
				peer = !strcmp (n, "updateChatUserTyping") ? pkey (P_CHAT, u["chat_id"].i ()) : pkey (P_CHANNEL, u["channel_id"].i ());
				who = peerKey (u["from_id"]);
			}
			Conv *c = convs.add (peer); c->peer = peer;
			const char *act = u["action"].name ();
			if (!strcmp (act, "sendMessageCancelAction")) { c->typingUntil = 0; c->typingUser = 0; }
			else
			{
				c->typingUser = who; c->typingUntil = tg_unix_ms () + 6000;
				c->typingKind = strstr (act, "RecordAudio") ? 1 : strstr (act, "Upload") ? 2 : 0;
			}
			c->rev++; rev++;
		}
		else if (!strcmp (n, "updateReadHistoryOutbox") || !strcmp (n, "updateReadChannelOutbox"))
		{
			long long peer = !strcmp (n, "updateReadChannelOutbox") ? pkey (P_CHANNEL, u["channel_id"].i ()) : peerKey (u["peer"]);
			Conv *c = conv (peer);
			if (c) { int m = (int) u["max_id"].i (); if (m > c->readOutMax) c->readOutMax = m; c->rev++; rev++; }
		}
		else if (!strcmp (n, "updateReadHistoryInbox") || !strcmp (n, "updateReadChannelInbox"))
		{
			long long peer = !strcmp (n, "updateReadChannelInbox") ? pkey (P_CHANNEL, u["channel_id"].i ()) : peerKey (u["peer"]);
			Conv *c = conv (peer);
			if (c) { c->readInMax = (int) u["max_id"].i (); c->unread = (int) u["still_unread_count"].i (); c->rev++; rev++; }
		}
	}

	// ---- reading the server's objects ------------------------------------------------------

	static long long peerKey (const tl::Val &p)
	{
		if (p.is ("peerUser")) return pkey (P_USER, p["user_id"].i ());
		if (p.is ("peerChat")) return pkey (P_CHAT, p["chat_id"].i ());
		if (p.is ("peerChannel")) return pkey (P_CHANNEL, p["channel_id"].i ());
		return 0;
	}

	void readStatus (User *x, const tl::Val &s)
	{
		const char *n = s.name ();
		x->status = !strcmp (n, "userStatusOnline") ? ST_ONLINE : !strcmp (n, "userStatusOffline") ? ST_OFFLINE :
			    !strcmp (n, "userStatusRecently") ? ST_RECENTLY : !strcmp (n, "userStatusLastWeek") ? ST_WEEK :
			    !strcmp (n, "userStatusLastMonth") ? ST_MONTH : s.ok () ? ST_LONGAGO : ST_UNKNOWN;
		if (x->status == ST_ONLINE) x->expires = (int) s["expires"].i ();
		if (x->status == ST_OFFLINE) x->wasOnline = (int) s["was_online"].i ();
	}

public:
	void addUser (const tl::Val &u)
	{
		if (!u.is ("user")) return;
		long long id = u["id"].i ();
		User *x = users.add (id);
		bool min = u["min"].b ();
		x->id = id;
		if (!min || !x->access) x->access = u["access_hash"].i ();
		if (!min || !x->first) { sset (x->first, u["first_name"].str ()); sset (x->last, u["last_name"].str ()); }
		if (u["username"].ok () || !x->username) sset (x->username, u["username"].str ());
		if (u["phone"].ok () || !x->phone) sset (x->phone, u["phone"].str ());
		if (u["status"].ok ()) readStatus (x, u["status"]);
		const tl::Val &ph = u["photo"];
		if (ph.is ("userProfilePhoto")) { x->photoId = ph["photo_id"].i (); x->photoDc = (int) ph["dc_id"].i (); }
		else if (ph.ok ()) x->photoId = 0;
		x->self = u["self"].b () || x->self;
		if (!min) x->contact = u["contact"].b ();
		x->bot = u["bot"].b ();
		x->deleted = u["deleted"].b ();
		if (x->self) selfId = id;
	}
	void addUsers (const tl::Val &v) { for (int i = 0; i < v.count (); i++) addUser (v[i]); rev++; }

	void addChat (const tl::Val &c)
	{
		bool ch = c.is ("channel") || c.is ("channelForbidden");
		if (!c.is ("chat") && !c.is ("chatForbidden") && !ch) return;
		long long key = ch ? pkey (P_CHANNEL, c["id"].i ()) : pkey (P_CHAT, c["id"].i ());
		Chat *x = chats.add (pid (key));
		x->id = c["id"].i ();
		if (ch && (!c["min"].b () || !x->access)) x->access = c["access_hash"].i ();
		sset (x->title, c["title"].str ());
		x->kind = !ch ? 1 : c["broadcast"].b () ? 3 : 2;
		x->left = c["left"].b () || c.is ("chatForbidden") || c.is ("channelForbidden");
		const tl::Val &ph = c["photo"];
		if (ph.is ("chatPhoto")) { x->photoId = ph["photo_id"].i (); x->photoDc = (int) ph["dc_id"].i (); }
		else if (ph.ok ()) x->photoId = 0;
		if (c["participants_count"].ok ()) x->members = (int) c["participants_count"].i ();
	}
	void addChats (const tl::Val &v) { for (int i = 0; i < v.count (); i++) addChat (v[i]); rev++; }
private:

	// A Message -> m (its strings malloc'd) and its conversation's peer. False: an empty one.
	bool readMessage (const tl::Val &v, Msg &m, long long &peer)
	{
		memset (&m, 0, sizeof m);
		if (!v.is ("message") && !v.is ("messageService")) return false;
		m.id = (int) v["id"].i ();
		m.date = (int) v["date"].i ();
		m.out = v["out"].b ();
		peer = peerKey (v["peer_id"]);
		m.from = v["from_id"].ok () ? peerKey (v["from_id"]) : m.out ? pkey (P_USER, selfId) : peer;
		if (v["reply_to"].ok ()) m.replyTo = (int) v["reply_to"]["reply_to_msg_id"].i ();
		m.edited = v["edit_date"].ok () && !v["edit_hide"].b ();
		const char *extra = "";
		if (v.is ("messageService"))
		{
			m.media = M_SERVICE;
			const tl::Val &a = v["action"];
			const char *n = a.name ();
			m.service = !strcmp (n, "messageActionChatCreate") || !strcmp (n, "messageActionChannelCreate") ? SV_CREATE :
				    !strcmp (n, "messageActionChatEditTitle") ? SV_TITLE :
				    !strcmp (n, "messageActionChatEditPhoto") || !strcmp (n, "messageActionChatDeletePhoto") ? SV_PHOTO :
				    !strcmp (n, "messageActionChatAddUser") ? SV_ADD :
				    !strcmp (n, "messageActionChatDeleteUser") ? SV_REMOVE :
				    !strcmp (n, "messageActionChatJoinedByLink") || !strcmp (n, "messageActionChatJoinedByRequest") ? SV_JOINED_LINK :
				    !strcmp (n, "messageActionPinMessage") ? SV_PIN :
				    !strcmp (n, "messageActionPhoneCall") || !strcmp (n, "messageActionGroupCall") ? SV_CALL :
				    !strcmp (n, "messageActionContactSignUp") ? SV_JOINED_TG :
				    !strcmp (n, "messageActionScreenshotTaken") ? SV_SCREENSHOT : SV_OTHER;
			extra = a["title"].str ();
			if (m.service == SV_ADD && a["users"].count ()) m.svcUser = a["users"][0].i ();
			if (m.service == SV_REMOVE) { m.svcUser = a["user_id"].i (); if (pkey (P_USER, m.svcUser) == m.from) m.service = SV_LEFT; }
			m.text = sdup ("");
			m.extra = sdup (extra);
			return true;
		}
		m.text = sdup (v["message"].str ());
		const tl::Val &md = v["media"];
		if (md.ok ())
		{
			const char *n = md.name ();
			if (!strcmp (n, "messageMediaPhoto")) { m.media = M_PHOTO; readPhoto (md["photo"], m); }
			else if (!strcmp (n, "messageMediaGeo") || !strcmp (n, "messageMediaVenue") || !strcmp (n, "messageMediaGeoLive")) { m.media = M_GEO; extra = md["title"].str (); }
			else if (!strcmp (n, "messageMediaContact")) { m.media = M_CONTACT; extra = md["first_name"].str (); }
			else if (!strcmp (n, "messageMediaPoll")) { m.media = M_POLL; extra = md["poll"]["question"]["text"].str (); }
			else if (!strcmp (n, "messageMediaWebPage")) m.media = M_NONE;	// (the link's preview: the text says it)
			else if (!strcmp (n, "messageMediaDocument"))
			{
				m.media = md["voice"].b () ? M_VOICE : md["video"].b () || md["round"].b () ? M_VIDEO : M_FILE;
				const tl::Val &atts = md["document"]["attributes"];
				for (int i = 0; i < atts.count (); i++)
				{
					const char *an = atts[i].name ();
					if (!strcmp (an, "documentAttributeSticker")) { m.media = M_STICKER; extra = atts[i]["alt"].str (); }
					else if (!strcmp (an, "documentAttributeAnimated") && m.media != M_STICKER) m.media = M_GIF;
					else if (!strcmp (an, "documentAttributeAudio") && m.media == M_FILE) { m.media = atts[i]["voice"].b () ? M_VOICE : M_AUDIO; if (atts[i]["title"].ok ()) extra = atts[i]["title"].str (); }
					else if (!strcmp (an, "documentAttributeVideo") && m.media == M_FILE) m.media = M_VIDEO;
					else if (!strcmp (an, "documentAttributeFilename") && (m.media == M_FILE || m.media == M_AUDIO) && !extra[0]) extra = atts[i]["file_name"].str ();
				}
			}
			else if (strcmp (n, "messageMediaEmpty")) m.media = M_OTHER;
		}
		m.extra = sdup (extra);
		return true;
	}

	static void freeMsg (Msg &m) { free (m.text); free (m.extra); free (m.photoRef); free (m.local); m.text = m.extra = 0; m.photoRef = m.local = 0; }

	// A message into its conversation (kept sorted by id; the same id replaced).
	void insertMsg (Conv *c, Msg &m)
	{
		for (int i = c->n - 1; i >= 0; i--)
			if (c->m[i].id == m.id)
			{
				if (c->m[i].local && !m.local) { m.local = c->m[i].local; c->m[i].local = 0; }	// (our photo's file: still shown)
				freeMsg (c->m[i]); c->m[i] = m; return;
			}
		if (c->n == c->cap) { c->cap = c->cap ? c->cap * 2 : 32; c->m = (Msg *) realloc (c->m, sizeof (Msg) * (size_t) c->cap); }
		int i = c->n;
		while (i > 0 && c->m[i - 1].id > m.id) { c->m[i] = c->m[i - 1]; i--; }
		c->m[i] = m;
		c->n++;
	}
	void sortMsgs (Conv *c)
	{
		for (int i = 1; i < c->n; i++)
		{
			Msg k = c->m[i]; int j = i - 1;
			while (j >= 0 && c->m[j].id > k.id) { c->m[j + 1] = c->m[j]; j--; }
			c->m[j + 1] = k;
		}
	}
	void removeMsg (Conv *c, int id)
	{
		for (int i = 0; i < c->n; i++)
			if (c->m[i].id == id)
			{
				freeMsg (c->m[i]);
				memmove (c->m + i, c->m + i + 1, sizeof (Msg) * (size_t) (c->n - i - 1));
				c->n--; c->rev++; rev++;
				return;
			}
	}

	// A message that came (isNew: a new one, else an edit).
	void newMessage (long long peer, Msg &m, bool isNew)
	{
		if (!peer) { freeMsg (m); return; }
		Conv *c = convs.add (peer);
		c->peer = peer;
		if (isNew && m.out)				// (our own, sent from here: the pending one replaced)
			for (int i = 0; i < c->n; i++)
				if (c->m[i].pending && c->m[i].media == m.media && !strcmp (c->m[i].text, m.text))
				{
					if (c->m[i].local && !m.local) { m.local = c->m[i].local; c->m[i].local = 0; }
					freeMsg (c->m[i]); c->m[i] = m; sortMsgs (c); goto placed;
				}
		if (!isNew)
		{
			bool have = false;
			for (int i = 0; i < c->n; i++) if (c->m[i].id == m.id) have = true;
			if (!have) { freeMsg (m); return; }
		}
		if (c->loaded || !c->n || m.id >= c->topId) insertMsg (c, m);
		else { freeMsg (m); return; }
	placed:
		if (isNew)
		{
			if (m.id >= c->topId) { c->topId = m.id; c->topDate = m.date; }
			if (!m.out && m.media != M_SERVICE)
			{
				c->unread++;
				if (ninbox < 32) { inbox[ninbox].peer = peer; inbox[ninbox].id = m.id; ninbox++; }
			}
			if (c->typingUser == m.from) { c->typingUntil = 0; c->typingUser = 0; }
			if (!c->inList) c->inList = true;
		}
		c->rev++; rev++;
		dialogs ();
	}

	// ---- the conversations' list --------------------------------------------------------------

	void loadDialogs ()
	{
		tl::Arena a;
		tl::Val *q = tl::make (a, "messages.getDialogs");
		q->set ("offset_date", tl::I (0));
		q->set ("offset_id", tl::I (0));
		q->set ("offset_peer", tl::obj (a, "inputPeerEmpty"));
		q->set ("limit", tl::I (100));
		q->set ("hash", tl::L (0));
		call (m_home, *q, R_DIALOGS);
	}
	void gotDialogs (const tl::Val &v, const Pend &pe)
	{
		(void) pe;
		addUsers (v["users"]); addChats (v["chats"]);
		const tl::Val &ms = v["messages"];
		const tl::Val &ds = v["dialogs"];
		for (int i = 0; i < ds.count (); i++)
		{
			const tl::Val &d = ds[i];
			if (!d.is ("dialog")) continue;
			long long peer = peerKey (d["peer"]);
			if (!peer) continue;
			Conv *c = convs.add (peer);
			c->peer = peer; c->inList = true;
			c->pinned = d["pinned"].b ();
			c->unread = (int) d["unread_count"].i ();
			c->readInMax = (int) d["read_inbox_max_id"].i ();
			c->readOutMax = (int) d["read_outbox_max_id"].i ();
			c->topId = (int) d["top_message"].i ();
			c->rev++;
		}
		for (int i = 0; i < ms.count (); i++)
		{
			Msg m; long long peer;
			if (!readMessage (ms[i], m, peer)) continue;
			Conv *c = conv (peer);
			if (!c) { freeMsg (m); continue; }
			if (m.id == c->topId) c->topDate = m.date;
			insertMsg (c, m);
		}
		dialogsLoaded = true;
		dialogs ();
		rev++;
	}
	void gotContacts (const tl::Val &v)
	{
		addUsers (v["users"]);
		const tl::Val &cs = v["contacts"];
		for (int i = 0; i < cs.count (); i++) { User *u = user (cs[i]["user_id"].i ()); if (u) u->contact = true; }
		contactsLoaded = true;
		rev++;
	}

	void loadHistory (Conv *c, int offsetId)
	{
		tl::Arena a;
		tl::Val *q = tl::make (a, "messages.getHistory");
		q->set ("peer", inputPeer (a, c->peer));
		q->set ("offset_id", tl::I (offsetId));
		q->set ("offset_date", tl::I (0));
		q->set ("add_offset", tl::I (0));
		q->set ("limit", tl::I (50));
		q->set ("max_id", tl::I (0));
		q->set ("min_id", tl::I (0));
		q->set ("hash", tl::L (0));
		if (call (m_home, *q, R_HISTORY, c->peer, offsetId)) c->loading = true;
		c->rev++; rev++;
	}
	void gotHistory (const tl::Val &v, const Pend &pe)
	{
		Conv *c = conv (pe.a);
		if (!c) return;
		addUsers (v["users"]); addChats (v["chats"]);
		const tl::Val &ms = v["messages"];
		int got = 0;
		for (int i = 0; i < ms.count (); i++)
		{
			Msg m; long long peer;
			if (!readMessage (ms[i], m, peer)) continue;
			if (peer != c->peer) { freeMsg (m); continue; }
			insertMsg (c, m);
			got++;
		}
		c->loading = false;
		c->loaded = true;
		if (got < 50 || v.is ("messages.messages")) c->complete = true;
		if (c->n && c->m[c->n - 1].id > c->topId && !c->m[c->n - 1].pending) { c->topId = c->m[c->n - 1].id; c->topDate = c->m[c->n - 1].date; }
		c->rev++; rev++;
	}
	void gotSent (const tl::Val &v, const Pend &pe)
	{
		Conv *c = conv (pe.a);
		if (v.is ("updateShortSentMessage"))
		{
			for (int i = 0; c && i < c->n; i++)
				if (c->m[i].pending && c->m[i].randomId == pe.b)
				{
					c->m[i].id = (int) v["id"].i (); c->m[i].date = (int) v["date"].i ();
					c->m[i].pending = false; c->m[i].randomId = 0;
					sortMsgs (c);
					if (c->m[c->n - 1].id >= c->topId) { c->topId = c->m[c->n - 1].id; c->topDate = c->m[c->n - 1].date; }
					break;
				}
			bumpPts ((int) v["pts"].i ());
			if (c) c->rev++;
			rev++;
			dialogs ();
			return;
		}
		updates (v);
	}

	// ---- input peers ------------------------------------------------------------------------

	tl::Val inputPeer (tl::Arena &a, long long peer)
	{
		long long id = pid (peer);
		switch (ptype (peer))
		{
		case P_USER:
		{
			if (id == selfId) return tl::obj (a, "inputPeerSelf");
			User *u = user (id);
			tl::Val *p = tl::make (a, "inputPeerUser");
			p->set ("user_id", tl::L (id)); p->set ("access_hash", tl::L (u ? u->access : 0));
			return *p;
		}
		case P_CHAT: { tl::Val *p = tl::make (a, "inputPeerChat"); p->set ("chat_id", tl::L (id)); return *p; }
		default:
		{
			Chat *c = chat (id);
			tl::Val *p = tl::make (a, "inputPeerChannel");
			p->set ("channel_id", tl::L (id)); p->set ("access_hash", tl::L (c ? c->access : 0));
			return *p;
		}
		}
	}
	tl::Val inputChannel (tl::Arena &a, long long id)
	{
		Chat *c = chat (id);
		tl::Val *p = tl::make (a, "inputChannel");
		p->set ("channel_id", tl::L (id)); p->set ("access_hash", tl::L (c ? c->access : 0));
		return *p;
	}

	// A Photo: its id, access hash, reference, data centre, and the size to show -- the largest up to 800 px
	// ("x"), else the largest.
	void readPhoto (const tl::Val &ph, Msg &m)
	{
		if (!ph.is ("photo")) return;
		m.photoId = ph["id"].i ();
		m.photoAccess = ph["access_hash"].i ();
		const tl::Val &ref = ph["file_reference"];
		m.photoRefN = ref.len ();
		m.photoRef = (char *) malloc ((size_t) (m.photoRefN ? m.photoRefN : 1));
		if (m.photoRef && m.photoRefN) memcpy (m.photoRef, ref.s, (size_t) m.photoRefN);
		m.photoDc = (int) ph["dc_id"].i ();
		const tl::Val &sz = ph["sizes"];
		int best = -1, bestS = 0, any = -1, anyS = 0;
		for (int i = 0; i < sz.count (); i++)
		{
			const tl::Val &z = sz[i];
			if (!z.is ("photoSize") && !z.is ("photoSizeProgressive")) continue;
			int w = (int) z["w"].i (), h = (int) z["h"].i (), s = w > h ? w : h;
			if (s <= 800 && s > bestS) { best = i; bestS = s; }
			if (s > anyS) { any = i; anyS = s; }
		}
		if (best < 0) best = any;
		if (best < 0) return;
		const tl::Val &z = sz[best];
		snprintf (m.thumb, sizeof m.thumb, "%s", z["type"].str ());
		m.pw = (short) z["w"].i (); m.ph = (short) z["h"].i ();
	}

	// the next part of the photo being sent, or sendMedia once all are there
	void nextPart ()
	{
		Upload &u = m_up[0];
		tl::Arena a;
		if (u.next < u.parts)
		{
			int off = u.next * PART, n = u.n - off < PART ? u.n - off : PART;
			tl::Val *q = tl::make (a, "upload.saveFilePart");
			q->set ("file_id", tl::L (u.fileId));
			q->set ("file_part", tl::I (u.next));
			q->set ("bytes", tl::S (a, u.data + off, n));
			if (call (m_home, *q, R_PART, u.peer, u.rid)) m_upBusy = true;
			return;
		}
		tl::Val *f = tl::make (a, "inputFile");
		f->set ("id", tl::L (u.fileId));
		f->set ("parts", tl::I (u.parts));
		f->set ("name", tl::S (a, "photo.jpg"));
		f->set ("md5_checksum", tl::S (a, ""));
		tl::Val *media = tl::make (a, "inputMediaUploadedPhoto");
		media->set ("file", *f);
		tl::Val *q = tl::make (a, "messages.sendMedia");
		q->set ("peer", inputPeer (a, u.peer));
		q->set ("media", *media);
		q->set ("message", tl::S (a, u.caption));
		q->set ("random_id", tl::L (u.rid));
		if (call (m_home, *q, R_SENDMEDIA, u.peer, u.rid)) m_upBusy = true;
	}
	void uploadProgress ()
	{
		Upload &u = m_up[0];
		Conv *c = conv (u.peer);
		for (int i = 0; c && i < c->n; i++)
			if (c->m[i].randomId == u.rid && c->m[i].pending) { c->m[i].progress = (signed char) (u.next * 100 / (u.parts ? u.parts : 1)); c->rev++; rev++; }
	}
	void popUpload ()
	{
		if (!m_nup) return;
		free (m_up[0].data); free (m_up[0].caption);
		memmove (m_up, m_up + 1, sizeof (Upload) * (size_t) (m_nup - 1));
		m_nup--;
	}

	// ---- the photos: one at a time, from the data centre they are on ------------------------

	void queuePhoto (PhotoReq &r)
	{
		if (m_nphotoQ >= 512) { free (r.ref); return; }
		m_photoQ = (PhotoReq *) realloc (m_photoQ, sizeof (PhotoReq) * (size_t) (m_nphotoQ + 1));
		m_photoQ[m_nphotoQ++] = r;
	}
	void nextPhoto ()
	{
		if (!m_nphotoQ) return;
		free (m_curPhoto.ref);
		m_curPhoto = m_photoQ[m_nphotoQ - 1];		// (the latest asked first: what is on the screen)
		m_nphotoQ--;
		int dc = m_curPhoto.dc > 0 && m_curPhoto.dc < MAXDC ? m_curPhoto.dc : m_home;
		m_photoBusy = true;
		free (m_fileBuf); m_fileBuf = 0; m_fileN = 0;
		Pend pe; memset (&pe, 0, sizeof pe);
		pe.kind = R_FILE; pe.dc = dc;
		repostFile (pe);
	}
	void repostFile (Pend &pe)
	{
		int dc = pe.dc;
		free (pe.body); pe.body = 0;
		if (dc != m_home && !m_auth[dc])		// (that data centre's authorization: exported from the home one)
		{
			connect (dc);
			if (!m_exporting[dc])
			{
				m_exporting[dc] = true;
				tl::Arena a;
				tl::Val *q = tl::make (a, "auth.exportAuthorization");
				q->set ("dc_id", tl::I (dc));
				call (m_home, *q, R_EXPORT, dc);
			}
			m_waitDc = dc;
			return;
		}
		getFilePart (dc, m_fileN);
	}
	int m_waitDc = 0;
	void flushDc (int dc) { if (m_photoBusy && m_waitDc == dc) { m_waitDc = 0; getFilePart (dc, m_fileN); } }
	void gotExport (const tl::Val &v, const Pend &pe)
	{
		int dc = (int) pe.a;
		tl::Arena a;
		tl::Val *q = tl::make (a, "auth.importAuthorization");
		q->set ("id", tl::L (v["id"].i ()));
		q->set ("bytes", tl::S (a, v["bytes"].s, v["bytes"].len ()));
		call (dc, *q, R_IMPORT, dc);
	}
	void getFilePart (int dc, int offset)
	{
		tl::Arena a;
		tl::Val *loc;
		if (m_curPhoto.kind == 1)
		{
			loc = tl::make (a, "inputPhotoFileLocation");
			loc->set ("id", tl::L (m_curPhoto.id));
			loc->set ("access_hash", tl::L (m_curPhoto.access));
			loc->set ("file_reference", tl::S (a, m_curPhoto.ref ? m_curPhoto.ref : (const unsigned char *) "", m_curPhoto.refn));
			loc->set ("thumb_size", tl::S (a, m_curPhoto.thumb));
		}
		else
		{
			loc = tl::make (a, "inputPeerPhotoFileLocation");
			loc->set ("peer", inputPeer (a, m_curPhoto.peer));
			loc->set ("photo_id", tl::L (m_curPhoto.id));
		}
		tl::Val *q = tl::make (a, "upload.getFile");
		q->set ("location", *loc);
		q->set ("offset", tl::L (offset));
		q->set ("limit", tl::I (131072));
		if (!call (dc, *q, R_FILE, m_curPhoto.peer, offset)) m_photoBusy = false;
	}
	void gotFile (const tl::Val &v, const Pend &pe)
	{
		const tl::Val &b = v["bytes"];
		if (!v.is ("upload.file")) { m_photoBusy = false; return; }
		m_fileBuf = (unsigned char *) realloc (m_fileBuf, (size_t) (m_fileN + b.len () + 1));
		memcpy (m_fileBuf + m_fileN, b.s, (size_t) b.len ());
		m_fileN += b.len ();
		if (b.len () == 131072 && m_fileN < (4 << 20)) { getFilePart (pe.dc, m_fileN); return; }
		char path[160];
		if (m_curPhoto.kind == 1)
		{
			snprintf (path, sizeof path, "%scache/m%llx%s.jpg", dir, (unsigned long long) m_curPhoto.id, m_curPhoto.thumb);
			if (m_fileN > 0 && tg_save (path, m_fileBuf, m_fileN)) m_haveMsgFile.put (m_curPhoto.id, 1);
		}
		else
		{
			snprintf (path, sizeof path, "%scache/%llx.jpg", dir, (unsigned long long) m_curPhoto.id);
			if (m_fileN > 0 && tg_save (path, m_fileBuf, m_fileN)) m_haveFile.put (m_curPhoto.id, 1);
		}
		free (m_fileBuf); m_fileBuf = 0; m_fileN = 0;
		m_photoBusy = false;
		rev++;
	}

	// ---- the session file ---------------------------------------------------------------------

	void loadSession ()
	{
		char path[160];
		snprintf (path, sizeof path, "%ssession.dat", dir);
		int n = 0;
		unsigned char *b = tg_load (path, &n);
		if (!b) return;
		char *line = (char *) b;
		while (*line)
		{
			char *e = line;
			while (*e && *e != '\n') e++;
			char save = *e; *e = 0;
			char *eq = strchr (line, '=');
			if (eq)
			{
				*eq = 0;
				const char *k = line, *val = eq + 1;
				int dc = 0;
				if (!strcmp (k, "home")) m_home = atoi (val) > 0 && atoi (val) < MAXDC ? atoi (val) : 2;
				else if (!strcmp (k, "user")) selfId = atoll (val);
				else if (!strcmp (k, "test")) { if (atoi (val) != (int) test) { free (b); return; } }	// (another server set)
				else if (sscanf (k, "key%d", &dc) == 1 && dc > 0 && dc < MAXDC && strlen (val) == 512)
				{
					unsigned char key[256];
					for (int i = 0; i < 256; i++) { unsigned x = 0; sscanf (val + i * 2, "%2x", &x); key[i] = (unsigned char) x; }
					m_s[dc].setKey (key, 0);
				}
				else if (sscanf (k, "auth%d", &dc) == 1 && dc > 0 && dc < MAXDC) m_auth[dc] = atoi (val) != 0;
				else if (sscanf (k, "addr%d", &dc) == 1 && dc > 0 && dc < MAXDC)
				{
					char ip[48] = { 0 }; int port = 443;
					if (sscanf (val, "%47[^:]:%d", ip, &port) >= 1) { snprintf (m_addr[dc].ip, sizeof m_addr[dc].ip, "%s", ip); m_addr[dc].port = port; }
				}
			}
			*e = save;
			line = *e ? e + 1 : e;
		}
		free (b);
		if (!m_auth[m_home]) selfId = 0;
	}
	void saveSession ()
	{
		char *t = (char *) malloc (8192);
		if (!t) return;
		int o = snprintf (t, 8192, "# Onyx Telegram: the session (the authorization keys are secrets)\nhome=%d\nuser=%lld\ntest=%d\n", m_home, state == AS_READY ? selfId : 0, (int) test);
		for (int i = 1; i < MAXDC; i++)
		{
			if (m_addr[i].ip[0]) o += snprintf (t + o, (size_t) (8192 - o), "addr%d=%s:%d\n", i, m_addr[i].ip, m_addr[i].port);
			if (!m_s[i].haveKey) continue;
			o += snprintf (t + o, (size_t) (8192 - o), "key%d=", i);
			for (int k = 0; k < 256; k++) o += snprintf (t + o, (size_t) (8192 - o), "%02x", m_s[i].key[k]);
			o += snprintf (t + o, (size_t) (8192 - o), "\nauth%d=%d\n", i, (int) (m_auth[i] && state == AS_READY));
		}
		char path[160];
		snprintf (path, sizeof path, "%ssession.dat", dir);
		tg_save (path, t, o);
		free (t);
	}
};

inline void DcListener::onResult (int req, const tl::Val &v) { c->result (dc, req, v); }
inline void DcListener::onError (int req, int code, const char *m) { c->failed (dc, req, code, m); }
inline void DcListener::onUpdates (const tl::Val &v) { c->updates (v); }
inline void DcListener::onKeyReady () { c->keyReady (dc); }
inline void DcListener::onNewSession () { c->newSession (dc); }

} // namespace tg

#endif
