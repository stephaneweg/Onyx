//
// tgmodel_test.cpp -- the Telegram app's client model offline: the server's objects made here (the TL
// codec's make / encode / decode, as they come over the wire), fed to the client as its sessions would
// -- the conversations' list, a history, the updates (a short message, a chat message, an edit, a
// deletion, typing, statuses, read receipts, updateMessageID), getDifference, a message sent and
// confirmed -- and SRP checked against the server's side of it (computed here as the server does).
//
// MIT licence.
//
#define TG_FAKE_NET
#include <initializer_list>
#include "tgplat_host.h"
#define private public				// (the test reaches the client's handlers)
#include "client.h"
#undef private

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (c) printf ("  ok   " __VA_ARGS__); else { fails++; printf ("  FAIL " __VA_ARGS__); } printf ("\n"); } while (0)

static tl::Arena A;

// (through the wire: encoded, decoded back -- what the client gets is what a server sends)
static tl::Val wire (const tl::Val &v)
{
	tl::Buf b;
	if (!tl::encode (b, v)) { printf ("  (encode failed: %s)\n", v.name ()); return tl::none (); }
	tl::Val r;
	if (!tl::decode (A, b.d, b.n, r)) { printf ("  (decode failed: %s)\n", v.name ()); return tl::none (); }
	return r;
}
static tl::Val vec (std::initializer_list<tl::Val> l) { tl::Val v = tl::Vec (A, (int) l.size ()); int i = 0; for (const tl::Val &x : l) v.v[i++] = x; return v; }
static tl::Val peerUser (long long id) { tl::Val *p = tl::make (A, "peerUser"); p->set ("user_id", tl::L (id)); return *p; }
static tl::Val peerChat (long long id) { tl::Val *p = tl::make (A, "peerChat"); p->set ("chat_id", tl::L (id)); return *p; }
static tl::Val user (long long id, const char *first, const char *last, const char *status, bool self = false)
{
	tl::Val *u = tl::make (A, "user");
	u->set ("id", tl::L (id)); u->set ("access_hash", tl::L (id * 7 + 1));
	u->set ("first_name", tl::S (A, first)); u->set ("last_name", tl::S (A, last));
	if (self) u->set ("self", tl::T ());
	if (status)
	{
		tl::Val *s = tl::make (A, status);
		if (!strcmp (status, "userStatusOnline")) s->set ("expires", tl::I (2000000000));
		if (!strcmp (status, "userStatusOffline")) s->set ("was_online", tl::I (1790000000));
		u->set ("status", *s);
	}
	tl::Val *ph = tl::make (A, "userProfilePhoto");
	ph->set ("photo_id", tl::L (id * 1000)); ph->set ("dc_id", tl::I (4));
	u->set ("photo", *ph);
	return *u;
}
static tl::Val chat (long long id, const char *title, int members)
{
	tl::Val *c = tl::make (A, "chat");
	c->set ("id", tl::L (id)); c->set ("title", tl::S (A, title)); c->set ("photo", tl::obj (A, "chatPhotoEmpty"));
	c->set ("participants_count", tl::I (members)); c->set ("date", tl::I (1)); c->set ("version", tl::I (1));
	return *c;
}
static tl::Val message (int id, tl::Val peer, long long from, bool out, const char *text, int date)
{
	tl::Val *m = tl::make (A, "message");
	m->set ("id", tl::I (id)); m->set ("peer_id", peer); m->set ("date", tl::I (date)); m->set ("message", tl::S (A, text));
	if (out) m->set ("out", tl::T ());
	if (from) m->set ("from_id", peerUser (from));
	return *m;
}

int main ()
{
	tg_quiet = true;
	tl::load ();
	tg::Client c;
	strcpy (c.dir, "/tmp/tgM/");
	if (system ("rm -rf /tmp/tgM; mkdir -p /tmp/tgM/cache")) {}
	c.apiId = 1; strcpy (c.apiHash, "x"); c.begin ();
	c.state = tg::AS_READY; c.selfId = 100;
	const long long ME = 100, ALICE = 201, BOB = 202, GROUP = 301;
	long long pa = tg::pkey (tg::P_USER, ALICE), pb = tg::pkey (tg::P_USER, BOB), pg = tg::pkey (tg::P_CHAT, GROUP);

	// ---- the conversations' list
	{
		tl::Val *d1 = tl::make (A, "dialog"), *d2 = tl::make (A, "dialog"), *d3 = tl::make (A, "dialog");
		tl::Val ns = tl::obj (A, "peerNotifySettings");
		d1->set ("peer", peerUser (ALICE)); d1->set ("top_message", tl::I (12)); d1->set ("unread_count", tl::I (2)); d1->set ("read_inbox_max_id", tl::I (10)); d1->set ("read_outbox_max_id", tl::I (9)); d1->set ("notify_settings", ns);
		d2->set ("peer", peerUser (BOB)); d2->set ("top_message", tl::I (20)); d2->set ("notify_settings", ns);
		d3->set ("peer", peerChat (GROUP)); d3->set ("top_message", tl::I (30)); d3->set ("pinned", tl::T ()); d3->set ("notify_settings", ns);
		for (tl::Val *d : { d1, d2, d3 }) for (const char *f : { "read_inbox_max_id", "read_outbox_max_id", "unread_count", "unread_mentions_count", "unread_reactions_count", "unread_poll_votes_count" }) if (!(*d)[f].ok ()) d->set (f, tl::I (0));
		tl::Val *ds = tl::make (A, "messages.dialogs");
		ds->set ("dialogs", vec ({ *d1, *d2, *d3 }));
		ds->set ("messages", vec ({ message (12, peerUser (ALICE), 0, false, "See you tomorrow \xf0\x9f\x98\x8a", 1790000300),
					    message (20, peerUser (BOB), 0, true, "ok", 1790000100),
					    message (30, peerChat (GROUP), BOB, false, "Lunch?", 1790000200) }));
		ds->set ("chats", vec ({ chat (GROUP, "Onyx team", 3) }));
		ds->set ("users", vec ({ user (ME, "Steph", "W", "userStatusOnline", true), user (ALICE, "Alice", "Martin", "userStatusOnline"), user (BOB, "Bob", "", "userStatusOffline") }));
		tg::Client::Pend pe; memset (&pe, 0, sizeof pe);
		c.gotDialogs (wire (*ds), pe);
	}
	CHECK (c.dialogsLoaded && c.norder == 3, "three conversations (%d)", c.norder);
	CHECK (c.norder == 3 && c.order[0] == pg && c.order[1] == pa && c.order[2] == pb, "the pinned group first, then the latest");
	CHECK (c.conv (pa) && c.conv (pa)->unread == 2 && c.conv (pa)->n == 1 && !strcmp (c.conv (pa)->m[0].text, "See you tomorrow \xf0\x9f\x98\x8a"), "Alice's: 2 unread, the last message");
	CHECK (c.conv (pg)->m[0].from == pb, "the group's message is Bob's");
	CHECK (c.user (ALICE)->status == tg::ST_ONLINE && c.user (BOB)->status == tg::ST_OFFLINE && c.user (BOB)->wasOnline == 1790000000, "the statuses");
	CHECK (c.user (ALICE)->photoId == ALICE * 1000 && c.user (ALICE)->photoDc == 4 && c.user (ALICE)->access == ALICE * 7 + 1, "the photo, the access hash");
	char name[64]; c.peerName (pa, name, sizeof name);
	CHECK (!strcmp (name, "Alice Martin"), "a name (%s)", name);
	c.peerName (pg, name, sizeof name);
	CHECK (!strcmp (name, "Onyx team"), "a group's title");

	// ---- a history
	{
		tg::Conv *cv = c.convs.add (pa); cv->loading = true;
		tl::Val *mm = tl::make (A, "messages.messagesSlice");
		tl::Val *svc = tl::make (A, "messageService");
		svc->set ("id", tl::I (5)); svc->set ("peer_id", peerUser (ALICE)); svc->set ("date", tl::I (1789000000)); svc->set ("action", tl::obj (A, "messageActionContactSignUp"));
		tl::Val *doc = tl::make (A, "document");
		tl::Val *st = tl::make (A, "documentAttributeSticker"); st->set ("alt", tl::S (A, "\xf0\x9f\x91\x8d")); st->set ("stickerset", tl::obj (A, "inputStickerSetEmpty"));
		doc->set ("attributes", vec ({ *st })); doc->set ("file_reference", tl::S (A, "")); doc->set ("mime_type", tl::S (A, "image/webp"));
		tl::Val *md = tl::make (A, "messageMediaDocument"); md->set ("document", *doc);
		tl::Val sticker = message (11, peerUser (ALICE), 0, false, "", 1790000250); sticker.set ("media", *md);
		tl::Val *ph = tl::make (A, "messageMediaPhoto");
		tl::Val photo = message (8, peerUser (ALICE), 0, true, "look", 1790000050); photo.set ("media", *ph);
		mm->set ("count", tl::I (60));
		mm->set ("messages", vec ({ message (12, peerUser (ALICE), 0, false, "See you tomorrow \xf0\x9f\x98\x8a", 1790000300), sticker,
					    message (10, peerUser (ALICE), 0, false, "Hello!", 1790000000), photo, *svc }));
		mm->set ("topics", tl::Vec (A, 0)); mm->set ("chats", tl::Vec (A, 0)); mm->set ("users", vec ({ user (ALICE, "Alice", "Martin", "userStatusOnline") }));
		tg::Client::Pend pe; memset (&pe, 0, sizeof pe); pe.a = pa;
		c.gotHistory (wire (*mm), pe);
		CHECK (cv->loaded && !cv->loading && cv->n == 5, "five messages (%d)", cv->n);
		CHECK (cv->m[0].id == 5 && cv->m[4].id == 12, "sorted by id");
		CHECK (cv->m[0].media == tg::M_SERVICE && cv->m[0].service == tg::SV_JOINED_TG, "a service message: joined Telegram");
		CHECK (cv->m[1].media == tg::M_PHOTO && cv->m[1].out && cv->m[1].from == tg::pkey (tg::P_USER, ME), "a photo, ours");
		CHECK (cv->m[3].media == tg::M_STICKER && !strcmp (cv->m[3].extra, "\xf0\x9f\x91\x8d"), "a sticker and its emoji");
		CHECK (cv->complete, "fewer than the 50 asked: the history is whole");
	}

	// ---- the updates
	unsigned r0 = c.rev;
	{
		tl::Val *u = tl::make (A, "updateShortMessage");
		u->set ("id", tl::I (13)); u->set ("user_id", tl::L (ALICE)); u->set ("message", tl::S (A, "Are you there?"));
		u->set ("pts", tl::I (50)); u->set ("pts_count", tl::I (1)); u->set ("date", tl::I (1790000400));
		c.updates (wire (*u));
	}
	tg::Conv *ca = c.conv (pa);
	CHECK (ca->n == 6 && ca->m[5].id == 13 && !ca->m[5].out && ca->m[5].from == pa, "updateShortMessage: Alice's new message");
	CHECK (ca->unread == 3 && c.ninbox == 1 && c.inbox[0].peer == pa && c.rev != r0, "unread 3, a notification");
	CHECK (c.order[0] == pg && c.order[1] == pa, "still behind the pinned group");
	CHECK (c.m_pts == 50, "the pts followed");
	{
		tl::Val *u = tl::make (A, "updateShortChatMessage");
		u->set ("id", tl::I (31)); u->set ("from_id", tl::L (ALICE)); u->set ("chat_id", tl::L (GROUP)); u->set ("message", tl::S (A, "Yes!"));
		u->set ("pts", tl::I (51)); u->set ("pts_count", tl::I (1)); u->set ("date", tl::I (1790000500));
		c.updates (wire (*u));
		tg::Conv *g = c.conv (pg);
		CHECK (g->n == 2 && g->m[1].from == pa && !strcmp (g->m[1].text, "Yes!"), "updateShortChatMessage in the group");
	}
	// typing, status, an edit, a deletion, read receipts: in one Updates
	{
		tl::Val *ty = tl::make (A, "updateUserTyping"); ty->set ("user_id", tl::L (BOB)); ty->set ("action", tl::obj (A, "sendMessageTypingAction"));
		tl::Val *usr = tl::make (A, "updateUserStatus"); usr->set ("user_id", tl::L (BOB));
		tl::Val *on = tl::make (A, "userStatusOnline"); on->set ("expires", tl::I (2000000000)); usr->set ("status", *on);
		tl::Val edited = message (12, peerUser (ALICE), 0, false, "See you on Friday", 1790000300); edited.set ("edit_date", tl::I (1790000600));
		tl::Val *ed = tl::make (A, "updateEditMessage"); ed->set ("message", edited); ed->set ("pts", tl::I (52)); ed->set ("pts_count", tl::I (1));
		tl::Val *del = tl::make (A, "updateDeleteMessages"); del->set ("messages", vec ({ tl::I (10) })); del->set ("pts", tl::I (53)); del->set ("pts_count", tl::I (1));
		tl::Val *ro = tl::make (A, "updateReadHistoryOutbox"); ro->set ("peer", peerUser (ALICE)); ro->set ("max_id", tl::I (11)); ro->set ("pts", tl::I (54)); ro->set ("pts_count", tl::I (1));
		tl::Val *ups = tl::make (A, "updates");
		ups->set ("updates", vec ({ *ty, *usr, *ed, *del, *ro })); ups->set ("users", tl::Vec (A, 0)); ups->set ("chats", tl::Vec (A, 0));
		ups->set ("date", tl::I (1790000600)); ups->set ("seq", tl::I (0));
		c.updates (wire (*ups));
		tg::Conv *cb = c.conv (pb);
		CHECK (cb->typingUser == pb && cb->typingUntil > 0, "Bob is typing");
		CHECK (c.user (BOB)->status == tg::ST_ONLINE, "Bob is online");
		bool edit = false, gone = true;
		for (int i = 0; i < ca->n; i++) { if (ca->m[i].id == 12) edit = !strcmp (ca->m[i].text, "See you on Friday") && ca->m[i].edited; if (ca->m[i].id == 10) gone = false; }
		CHECK (edit, "Alice's message edited");
		CHECK (gone && ca->n == 5, "message 10 deleted");
		CHECK (ca->readOutMax == 11, "Alice read what we sent");
	}
	// a message from someone new, through getDifference
	{
		tl::Val *df = tl::make (A, "updates.difference");
		df->set ("new_messages", vec ({ message (40, peerUser (303), 0, false, "Hi, it's Carol", 1790000700) }));
		df->set ("new_encrypted_messages", tl::Vec (A, 0)); df->set ("other_updates", tl::Vec (A, 0)); df->set ("chats", tl::Vec (A, 0));
		df->set ("users", vec ({ user (303, "Carol", "", "userStatusRecently") }));
		tl::Val *st = tl::make (A, "updates.state"); st->set ("pts", tl::I (60)); st->set ("qts", tl::I (0)); st->set ("date", tl::I (1790000700)); st->set ("seq", tl::I (1)); st->set ("unread_count", tl::I (1));
		df->set ("state", *st);
		c.gotDifference (wire (*df));
		long long pc = tg::pkey (tg::P_USER, 303);
		CHECK (c.conv (pc) && c.conv (pc)->n == 1 && c.user (303) && c.user (303)->status == tg::ST_RECENTLY, "getDifference: Carol and her message");
		CHECK (c.order[1] == pc, "her conversation comes up (behind the pinned one)");
		CHECK (c.m_pts == 60, "the state taken");
	}
	// a message sent: shown at once, then its id
	{
		c.send (pa, "On my way");
		CHECK (ca->n == 6 && ca->m[5].pending && ca->m[5].out, "sent: pending, shown");
		tl::Val *ss = tl::make (A, "updateShortSentMessage");
		ss->set ("out", tl::T ()); ss->set ("id", tl::I (14)); ss->set ("pts", tl::I (61)); ss->set ("pts_count", tl::I (1)); ss->set ("date", tl::I (1790000800));
		tg::Client::Pend pe; memset (&pe, 0, sizeof pe); pe.a = pa; pe.b = ca->m[5].randomId;
		c.gotSent (wire (*ss), pe);
		CHECK (!ca->m[5].pending && ca->m[5].id == 14 && ca->topId == 14, "the server's id 14");
		c.send (pa, "Second");
		long long rid = ca->m[6].randomId;
		tl::Val *mid = tl::make (A, "updateMessageID"); mid->set ("id", tl::I (15)); mid->set ("random_id", tl::L (rid));
		tl::Val nm = message (15, peerUser (ALICE), 0, true, "Second", 1790000900);
		tl::Val *un = tl::make (A, "updateNewMessage"); un->set ("message", nm); un->set ("pts", tl::I (62)); un->set ("pts_count", tl::I (1));
		tl::Val *ups = tl::make (A, "updates");
		ups->set ("updates", vec ({ *mid, *un })); ups->set ("users", tl::Vec (A, 0)); ups->set ("chats", tl::Vec (A, 0)); ups->set ("date", tl::I (1)); ups->set ("seq", tl::I (0));
		c.updates (wire (*ups));
		CHECK (ca->n == 7 && ca->m[6].id == 15 && !ca->m[6].pending, "updateMessageID + updateNewMessage: one message, id 15 (%d)", ca->n);
	}
	// a photo sent: shown at once (its file), its parts, sendMedia, the server's message; a photo received
	{
		unsigned char jpg[300000];
		for (int i = 0; i < (int) sizeof jpg; i++) jpg[i] = (unsigned char) (i * 13);
		int before = ca->n;
		c.sendPhoto (pa, jpg, sizeof jpg, 1280, 800, "the lake");
		tg::Msg &pm = ca->m[ca->n - 1];
		CHECK (ca->n == before + 1 && pm.pending && pm.media == tg::M_PHOTO && pm.local && pm.pw == 1280, "a photo sent: shown at once, pending");
		char path[160];
		CHECK (c.msgPhoto (pm, path, sizeof path) && strstr (path, "cache/out"), "its picture: the file kept (%s)", path);
		long long rid = pm.randomId;
		int parts = 0;
		for (int guard = 0; guard < 10; guard++)
		{
			c.nextPart ();
			CHECK (c.m_upBusy, "a request made (part %d)", parts);
			int req = 0, kind = 0;
			for (int i = 0; i < tg::MAXPEND; i++) if (c.m_pend[i].req && (c.m_pend[i].kind == tg::R_PART || c.m_pend[i].kind == tg::R_SENDMEDIA)) { req = c.m_pend[i].req; kind = c.m_pend[i].kind; }
			if (kind == tg::R_SENDMEDIA)
			{
				// the server: updateMessageID + updateNewMessage with the photo
				tl::Val *sz1 = tl::make (A, "photoSize"), *sz2 = tl::make (A, "photoSizeProgressive"), *sz3 = tl::make (A, "photoStrippedSize");
				sz1->set ("type", tl::S (A, "m")); sz1->set ("w", tl::I (320)); sz1->set ("h", tl::I (200)); sz1->set ("size", tl::I (9000));
				sz2->set ("type", tl::S (A, "x")); sz2->set ("w", tl::I (800)); sz2->set ("h", tl::I (500)); sz2->set ("sizes", vec ({ tl::I (1000), tl::I (40000) }));
				sz3->set ("type", tl::S (A, "i")); sz3->set ("bytes", tl::S (A, "abc"));
				tl::Val *ph = tl::make (A, "photo");
				ph->set ("id", tl::L (777)); ph->set ("access_hash", tl::L (888)); ph->set ("file_reference", tl::S (A, "ref!", 4));
				ph->set ("date", tl::I (1)); ph->set ("sizes", vec ({ *sz3, *sz1, *sz2 })); ph->set ("dc_id", tl::I (4));
				tl::Val *md = tl::make (A, "messageMediaPhoto"); md->set ("photo", *ph);
				tl::Val nm = message (16, peerUser (ALICE), 0, true, "the lake", 1790001000); nm.set ("media", *md);
				tl::Val *mid = tl::make (A, "updateMessageID"); mid->set ("id", tl::I (16)); mid->set ("random_id", tl::L (rid));
				tl::Val *un = tl::make (A, "updateNewMessage"); un->set ("message", nm); un->set ("pts", tl::I (63)); un->set ("pts_count", tl::I (1));
				tl::Val *ups = tl::make (A, "updates");
				ups->set ("updates", vec ({ *mid, *un })); ups->set ("users", tl::Vec (A, 0)); ups->set ("chats", tl::Vec (A, 0)); ups->set ("date", tl::I (1)); ups->set ("seq", tl::I (0));
				c.result (2, req, wire (*ups));
				break;
			}
			parts++;
			c.result (2, req, tl::B (true));
		}
		CHECK (parts == 3, "300 000 bytes: 3 parts of 128 KB (%d)", parts);
		tg::Msg &sm = ca->m[ca->n - 1];
		CHECK (ca->n == before + 1 && sm.id == 16 && !sm.pending && sm.photoId == 777 && sm.photoAccess == 888 && sm.photoDc == 4, "the server's photo: one message, id 16");
		CHECK (!strcmp (sm.thumb, "x") && sm.pw == 800 && sm.photoRefN == 4 && !memcmp (sm.photoRef, "ref!", 4), "the size shown: x (800 x 500), its file reference");
		CHECK (sm.local && !strcmp (sm.text, "the lake"), "our file still shown, the caption");
		CHECK (c.m_nup == 0, "the upload done");
		// a photo received: asked for once, from data centre 4
		tg::Msg rm = sm; rm.local = 0; rm.photoId = 999;
		int q0 = c.m_nphotoQ;
		CHECK (!c.msgPhoto (rm, path, sizeof path) && c.m_nphotoQ == q0 + 1 && c.m_photoQ[q0].kind == 1 && c.m_photoQ[q0].dc == 4 && !strcmp (c.m_photoQ[q0].thumb, "x"), "a photo received: asked for (inputPhotoFileLocation, data centre 4)");
		c.msgPhoto (rm, path, sizeof path);
		CHECK (c.m_nphotoQ == q0 + 1, "... once");
	}

	// a contact added by phone number: contacts.importContacts, the user found -> a contact, a conversation
	{
		c.addContact ("+33 6 12 34 56 78", "Dora", "");
		int req = 0;
		for (int i = 0; i < tg::MAXPEND; i++) if (c.m_pend[i].req && c.m_pend[i].kind == tg::R_ADDCONTACT) req = c.m_pend[i].req;
		CHECK (req && c.adding, "contacts.importContacts asked");
		tl::Val *ic = tl::make (A, "importedContact"); ic->set ("user_id", tl::L (404)); ic->set ("client_id", tl::L (1));
		tl::Val *r = tl::make (A, "contacts.importedContacts");
		r->set ("imported", vec ({ *ic })); r->set ("popular_invites", tl::Vec (A, 0)); r->set ("retry_contacts", tl::Vec (A, 0));
		r->set ("users", vec ({ user (404, "Dora", "", "userStatusRecently") }));
		c.result (2, req, wire (*r));
		long long pd = tg::pkey (tg::P_USER, 404);
		CHECK (!c.adding && c.added == pd && c.user (404) && c.user (404)->contact && c.conv (pd) && c.conv (pd)->inList, "Dora: a contact, her conversation");
		c.added = 0;
		c.addContact ("+33 6 00 00 00 00", "Nobody", "");
		for (int i = 0; i < tg::MAXPEND; i++) if (c.m_pend[i].req && c.m_pend[i].kind == tg::R_ADDCONTACT) req = c.m_pend[i].req;
		tl::Val *r2 = tl::make (A, "contacts.importedContacts");
		r2->set ("imported", tl::Vec (A, 0)); r2->set ("popular_invites", tl::Vec (A, 0)); r2->set ("retry_contacts", tl::Vec (A, 0)); r2->set ("users", tl::Vec (A, 0));
		c.result (2, req, wire (*r2));
		CHECK (c.added == -1, "a number without an account: said so");
		c.added = 0;
	}

	// the session file
	{
		unsigned char key[256]; for (int i = 0; i < 256; i++) key[i] = (unsigned char) (i * 7);
		c.m_s[2].setKey (key, 5); c.m_auth[2] = true; c.m_home = 2;
		c.saveSession ();
		tg::Client d; strcpy (d.dir, "/tmp/tgM/"); d.loadSession ();
		CHECK (d.m_s[2].haveKey && !memcmp (d.m_s[2].key, key, 256) && d.selfId == ME && d.m_home == 2, "the session file read back");
	}

	// ---- SRP against the server's side (RFC 5054's, as Telegram's server checks it)
	{
		tgc::rng_init ();
		static const char P[] = "c71caeb9c6b1c9048e6c522f70f13f73980d40238e3e21c14934d037563d930f48198a0aa7c14058229493d22530f4dbfa336f6e0ac925139543aed44cce7c3720fd51f69458705ac68cd4fe6b6b13abdc9746512969328454f18faf8c595f642477fe96bb2a941d5bcd1d4ac8cc49880708fa9b378e3c4f3a9060bee67cf9a4a4a695811051907e162753b56b0f6b410dba74d8a84b2a14b3144e0ef1284754fd17ed950d5965b4b9dd46582db1178d169c6bc465b0d6ff9ca3928fef5b9ae4e418fc15e83ebea0f87fa9ff5eed70050ded2849f47bf959d956850ce929851f0d8115f635b105ee2e4e15d04b2454bf6f4fadf034b10403119cd8e3b92fcc5b";
		unsigned char p[256], s1[40], s2[16], v[256], B[256], Aa[256], M1[32];
		for (int i = 0; i < 256; i++) { unsigned x; sscanf (P + 2 * i, "%2x", &x); p[i] = (unsigned char) x; }
		tgc::random (s1, 40); tgc::random (s2, 16);
		int g = 3;
		tgc::srp_verifier ("correct horse", s1, 40, s2, 16, g, p, v);
		// the server: b, B = k v + g^b
		tgc::Mpi Pm (p, 256), G, V (v, 256), b, gb, K, Bm, kv, tmp;
		mbedtls_mpi_lset (&G.m, g);
		unsigned char bb[256]; tgc::random (bb, 256); b.set (bb, 256);
		tgc::powmod (gb, G, b, Pm);
		unsigned char gpad[256], h[32]; G.out (gpad, 256);
		tgc::sha256 (h, tgc::Part { p, 256 }, tgc::Part { gpad, 256 }); K.set (h, 32);
		mbedtls_mpi_mul_mpi (&kv.m, &K.m, &V.m);
		mbedtls_mpi_add_mpi (&Bm.m, &kv.m, &gb.m);
		mbedtls_mpi_mod_mpi (&Bm.m, &Bm.m, &Pm.m);
		Bm.out (B, 256);
		CHECK (tgc::srp ("correct horse", s1, 40, s2, 16, g, p, 256, B, 256, Aa, M1), "the client's A and M1");
		// the server: u = H(A | B), S = (A v^u)^b, K = H(S), M1 = H(H(p) ^ H(g) | H(s1) | H(s2) | A | B | K)
		tgc::Mpi Am (Aa, 256), u, vu, S;
		tgc::sha256 (h, tgc::Part { Aa, 256 }, tgc::Part { B, 256 }); u.set (h, 32);
		tgc::powmod (vu, V, u, Pm);
		mbedtls_mpi_mul_mpi (&tmp.m, &Am.m, &vu.m); mbedtls_mpi_mod_mpi (&tmp.m, &tmp.m, &Pm.m);
		tgc::powmod (S, tmp, b, Pm);
		unsigned char spad[256], k[32], hp[32], hg[32], hs1[32], hs2[32], hx[32], m1[32];
		S.out (spad, 256);
		tgc::sha256 (k, tgc::Part { spad, 256 });
		tgc::sha256 (hp, tgc::Part { p, 256 }); tgc::sha256 (hg, tgc::Part { gpad, 256 });
		for (int i = 0; i < 32; i++) hx[i] = hp[i] ^ hg[i];
		tgc::sha256 (hs1, tgc::Part { s1, 40 }); tgc::sha256 (hs2, tgc::Part { s2, 16 });
		tgc::sha256 (m1, tgc::Part { hx, 32 }, tgc::Part { hs1, 32 }, tgc::Part { hs2, 32 }, tgc::Part { Aa, 256 }, tgc::Part { B, 256 }, tgc::Part { k, 32 });
		CHECK (!memcmp (m1, M1, 32), "the server accepts the password's proof");
		tgc::srp ("wrong horse", s1, 40, s2, 16, g, p, 256, B, 256, Aa, M1);
		CHECK (memcmp (m1, M1, 32) != 0, "... and not a wrong password's");
	}

	printf ("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
