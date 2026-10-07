//
// demo.h -- "telegram --demo": the window filled with made-up people and conversations, no network, no
// account -- to look at the app before signing in, and for the documentation's screenshots
// (tools/tests/desktop_sim/shots.sh telegram). Everyone here is invented.
//
// MIT licence.
//
#ifndef TG_DEMO_H
#define TG_DEMO_H

#include "signin.h"

static void demo_user (long long id, const char *first, const char *last, const char *username, int status, int when, bool contact)
{
	tg::User *u = g_c.users.add (id);
	u->id = id; u->access = id * 3;
	tg::sset (u->first, first); tg::sset (u->last, last); tg::sset (u->username, username); tg::sset (u->phone, "");
	u->status = status; u->contact = contact;
	if (status == tg::ST_ONLINE) u->expires = g_c.serverTime () + 3600;
	u->wasOnline = when;
}

static void demo_msg (long long peer, int id, long long from, bool out, int date, const char *text, int media = tg::M_NONE, const char *extra = "")
{
	tg::Conv *c = g_c.convs.add (peer);
	c->peer = peer; c->inList = true; c->loaded = true; c->complete = true;
	tg::Msg m; memset (&m, 0, sizeof m);
	m.id = id; m.from = from; m.out = out; m.date = date; m.media = (unsigned char) media;
	m.text = tg::sdup (text); m.extra = tg::sdup (extra);
	if (c->n == c->cap) { c->cap = c->cap ? c->cap * 2 : 32; c->m = (tg::Msg *) realloc (c->m, sizeof (tg::Msg) * (size_t) c->cap); }
	c->m[c->n++] = m;
	if (id >= c->topId) { c->topId = id; c->topDate = date; }
}

static void demo_fill ()
{
	int now = g_c.serverTime ();
	if (now < 1700000000) now = 1790000000;
	int day = now - now % 86400;
	const long long ME = 1000, ALICE = 1001, BOB = 1002, CHLOE = 1003, DAVID = 1004, EMMA = 1005, FELIX = 1006, GINA = 1007, HUGO = 1008;
	g_c.selfId = ME;
	demo_user (ME, "Sam", "Taylor", "samtaylor", tg::ST_ONLINE, now, false);
	g_c.users.find (ME)->self = true;
	demo_user (ALICE, "Alice", "Martin", "alice_m", tg::ST_ONLINE, now, true);
	demo_user (BOB, "Bob", "Durand", "", tg::ST_OFFLINE, now - 600, true);
	demo_user (CHLOE, "Chlo\xc3\xa9", "Bernard", "chloe", tg::ST_ONLINE, now, true);
	demo_user (DAVID, "David", "Petit", "", tg::ST_RECENTLY, now - 7200, true);
	demo_user (EMMA, "Emma", "Leroy", "emma_l", tg::ST_OFFLINE, now - 3 * 86400, true);
	demo_user (FELIX, "Felix", "Moreau", "", tg::ST_WEEK, now - 6 * 86400, true);
	demo_user (GINA, "Gina", "Rossi", "", tg::ST_ONLINE, now, true);
	demo_user (HUGO, "Hugo", "Lambert", "", tg::ST_MONTH, now - 20 * 86400, true);
	tg::Chat *team = g_c.chats.add (500);
	team->id = 500; tg::sset (team->title, TR ("Onyx builders")); team->kind = 2; team->members = 14;
	tg::Chat *club = g_c.chats.add (501);
	club->id = 501; tg::sset (club->title, TR ("Saturday hikers")); club->kind = 1; club->members = 6;

	long long pa = tg::pkey (tg::P_USER, ALICE), pb = tg::pkey (tg::P_USER, BOB), pc = tg::pkey (tg::P_USER, CHLOE), pd = tg::pkey (tg::P_USER, DAVID);
	long long pe = tg::pkey (tg::P_USER, EMMA), pteam = tg::pkey (tg::P_CHANNEL, 500), pclub = tg::pkey (tg::P_CHAT, 501);
	long long me = tg::pkey (tg::P_USER, ME);

	// Alice: the conversation shown
	demo_msg (pa, 1, pa, false, day - 86400 + 19 * 3600, TR ("Did you get the Pi to boot from the new card?"));
	demo_msg (pa, 2, me, true, day - 86400 + 19 * 3600 + 120, TR ("Yes! Onyx starts in a few seconds now :D"));
	demo_msg (pa, 3, pa, false, day - 86400 + 19 * 3600 + 300, TR ("Great (Y)"));
	demo_msg (pa, 4, pa, false, day + 9 * 3600 + 5 * 60, TR ("Good morning! Are we still on for lunch today?"));
	demo_msg (pa, 5, me, true, day + 9 * 3600 + 7 * 60, TR ("Of course :) The usual place at 12:30?"));
	demo_msg (pa, 6, pa, false, day + 9 * 3600 + 8 * 60, TR ("Perfect. I'll bring the photos from the trip"));
	demo_msg (pa, 7, pa, false, day + 9 * 3600 + 8 * 60 + 20, "", tg::M_PHOTO);
	demo_msg (pa, 8, me, true, day + 9 * 3600 + 11 * 60, TR ("Wow, the lake looks amazing \xf0\x9f\x98\x8d"));
	demo_msg (pa, 9, me, true, day + 9 * 3600 + 11 * 60 + 30, TR ("I'm writing this from the Telegram app on Onyx, by the way \xf0\x9f\x98\x8e"));
	demo_msg (pa, 10, pa, false, day + 9 * 3600 + 14 * 60, TR ("No way! It looks just like the old Messenger \xf0\x9f\x98\x82"));
	demo_msg (pa, 11, pa, false, day + 9 * 3600 + 14 * 60 + 10, "\xf0\x9f\x91\x8d", tg::M_STICKER, "\xf0\x9f\x91\x8d");
	g_c.conv (pa)->readOutMax = 9;
	g_c.conv (pa)->typingUser = pa; g_c.conv (pa)->typingUntil = (long long) 1 << 60;

	// the others
	demo_msg (pteam, 20, pc, false, day + 8 * 3600 + 40 * 60, TR ("The new kernel is on the package server <3"));
	demo_msg (pteam, 21, pd, false, day + 8 * 3600 + 52 * 60, TR ("Installed, everything works here"));
	demo_msg (pteam, 22, pc, false, day + 9 * 3600 + 2 * 60, TR ("Who wants to test the Bluetooth audio?"));
	g_c.conv (pteam)->pinned = true; g_c.conv (pteam)->unread = 3;
	demo_msg (pclub, 30, pd, false, day + 8 * 3600, TR ("Rain is forecast on Saturday :("));
	demo_msg (pclub, 31, pe, false, day + 8 * 3600 + 15 * 60, TR ("Then the museum? (*)"));
	demo_msg (pb, 40, me, true, day - 2 * 86400 + 15 * 3600, TR ("Thanks for the help with the router!"));
	demo_msg (pb, 41, pb, false, day - 2 * 86400 + 15 * 3600 + 600, TR ("Any time ;)"));
	g_c.conv (pb)->readOutMax = 41;
	demo_msg (pc, 50, pc, false, day + 7 * 3600 + 30 * 60, TR ("Can you send me the slides from yesterday?"));
	g_c.conv (pc)->unread = 1;
	demo_msg (pd, 60, me, true, day - 86400 + 11 * 3600, TR ("Happy birthday David! (G)"));
	demo_msg (pd, 61, pd, false, day - 86400 + 12 * 3600, TR ("Thank you so much!"));
	g_c.conv (pd)->readOutMax = 61;
	demo_msg (pe, 70, pe, false, day - 4 * 86400 + 18 * 3600, "", tg::M_VOICE);

	g_c.dialogsLoaded = g_c.contactsLoaded = true;
	g_c.online = true;
	g_c.state = tg::AS_READY;
	g_c.rev++;
	g_c.dialogs ();
}

#endif
