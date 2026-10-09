//
// buddylist.h -- the left of the window, Messenger's contact list: on top "me" on the sky (my picture in its
// glass frame, my name and my status, a click on it to change it), the search field, then the groups --
// Favourites (the pinned conversations), Conversations (the others, the latest first: the picture, the
// name, the time, the last message, the unread count), Contacts (the buddy in the status colour, the name,
// the last seen) -- each group folded or not by a click on its title.
//
// MIT licence.
//
#ifndef TG_BUDDYLIST_H
#define TG_BUDDYLIST_H

#include "look.h"
#include "rich.h"

// From main.cpp
static tg::Client g_c;
// A conversation's pane: what shows one conversation (its header, its messages, the line to write in...)
// -- the main window's right side, or a conversation's own window (main.cpp, ConvWindow). The pane being
// served (its widgets' drawing, their events, their window's tick) is g_pane: the widgets of a pane name
// theirs (m_pane) and make it the current one before they work; what reads g_open, g_chat... then reads
// that pane's.
class ConvHeader; class PeerBar; class ChatView; class DpColumn; class InputBar; class EmoPicker;
// The picture to send with the next message (attached by the button, Ctrl+V, a drop): the JPEG made of it,
// its size, a small view of it, its name.
struct Attach { unsigned char *jpg; int n, w, h; unsigned *thumb; int tw, th; char name[80]; };
struct TgPane
{
	long long open;				// the conversation shown (0: none)
	Attach att;
	ConvHeader *head; PeerBar *bar; ChatView *chat; InputBar *input; DpColumn *dp; EmoPicker *picker;
	uikit::Root *root;			// its window
};
static TgPane g_mainPane;			// the main window's
static TgPane *g_pane = &g_mainPane;
static inline void tg_use (TgPane *p) { if (p) g_pane = p; }
#define g_open		(g_pane->open)
#define g_att		(g_pane->att)
#define g_head		(g_pane->head)
#define g_bar		(g_pane->bar)
#define g_chat		(g_pane->chat)
#define g_input		(g_pane->input)
#define g_dp		(g_pane->dp)
#define g_picker	(g_pane->picker)
static void open_conversation (long long peer);
static void status_menu (int x, int y);
static bool g_hasMenuBar = true;
static void add_contact_dialog ();

// The last message of a conversation in a line ("You: ok", "Bob: Lunch?", "[Photo]").
static void preview (tg::Conv *cv, char *out, int cap)
{
	out[0] = 0;
	if (!cv || !cv->n) return;
	tg::Msg &m = cv->m[cv->n - 1];
	char who[80] = "";
	if (m.out) snprintf (who, sizeof who, "%s: ", TR ("You"));
	else if (tg::ptype (cv->peer) != tg::P_USER && m.from && tg::ptype (m.from) == tg::P_USER)
	{
		tg::User *u = g_c.user (tg::pid (m.from));
		if (u && u->first) snprintf (who, sizeof who, "%s: ", u->first);
	}
	const char *what = m.text;
	char tmp[96];
	if (m.media && m.media != tg::M_SERVICE && (!m.text || !m.text[0]))
	{
		static const char *names[] = { "", TRN ("Photo"), TRN ("Video"), TRN ("Voice message"), TRN ("Audio"), TRN ("File"), TRN ("Sticker"), TRN ("GIF"), TRN ("Location"), TRN ("Contact"), TRN ("Poll"), TRN ("Link"), TRN ("Message") };
		snprintf (tmp, sizeof tmp, "[%s]%s%s", TR (names[m.media < 13 ? m.media : 12]), m.media == tg::M_STICKER ? " " : "", m.media == tg::M_STICKER ? m.extra : "");
		what = tmp;
	}
	else if (m.media == tg::M_SERVICE) { snprintf (tmp, sizeof tmp, "%s", TR ("(group event)")); what = tmp; who[0] = 0; }
	else if (m.media == tg::M_PHOTO) { snprintf (tmp, sizeof tmp, "[%s] %s", TR ("Photo"), m.text); what = tmp; }
	snprintf (out, (size_t) cap, "%s%s", who, what ? what : "");
	for (char *p = out; *p; p++) if (*p == '\n') *p = ' ';
}

// A time for a list: 14:05 today, "Mon" this week, 03/10 before.
static void short_time (int t, char *out, int cap)
{
	out[0] = 0;
	if (!t) return;
	int now = g_c.serverTime ();
	int tz = 0;
	struct kapi_clock_info ci;
	if (kapi_clock_info (&ci) == 0) tz = ci.tz_minutes * 60;
	long lt = t + tz, ln = now + tz;
	long day = lt / 86400, today = ln / 86400;
	if (day == today) snprintf (out, (size_t) cap, "%02ld:%02ld", (lt % 86400) / 3600, (lt % 3600) / 60);
	else if (today - day < 7) { static const char *wd[] = { TRN ("Thu"), TRN ("Fri"), TRN ("Sat"), TRN ("Sun"), TRN ("Mon"), TRN ("Tue"), TRN ("Wed") }; snprintf (out, (size_t) cap, "%s", TR (wd[day % 7])); }
	else
	{
		// the civil date from the day number
		long z = day + 719468, era = z / 146097, doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
		long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
		snprintf (out, (size_t) cap, "%02ld/%02ld", d, m);
	}
}

// The search field: "Search..." in grey when it is empty, a magnifying glass.
class SearchBox : public Textbox
{
public:
	SearchBox (int l, int t, int w, int h) : Textbox (l, t, w, h, "") { padR = 22; }
	void onDraw () override
	{
		Textbox::onDraw ();
		if (!text[0] && !hasFocus) ftext (canvas, g_face.ui, 8, (height - g_face.ui->height ()) / 2, TR ("Search..."), TC_LIGHT, 1, width - 30);
		VPath p;
		int cx = width - 14, cy = height / 2 - 1;
		p.arc (V (cx), V (cy), V (5), 0, 360, V (1) + 8);
		p.line (V (cx + 3) + 8, V (cy + 3) + 8, V (cx + 7), V (cy + 7), V (2));
		p.fill (canvas, TC_GREY);
	}
};

class BuddyList : public Widget
{
public:
	enum { HEAD_H = 92, SEARCH_H = 34, CONV_H = 50, CONTACT_H = 26, GROUP_H = 26 };
	enum { G_FAV, G_CONV, G_CONTACTS, G_N };
	SearchBox *search;
	bool folded[G_N];

	BuddyList (int l, int t, int w, int h) : Widget (l, t, w, h), m_n (0), m_cap (0), m_rows (0), m_scroll (0), m_hot (-1), m_down (false),
						      m_meHot (false), m_rev (0), m_lastClick (0), m_lastRow (-1)
	{
		folded[0] = folded[1] = folded[2] = false;
		search = new SearchBox (10, HEAD_H + 5, w - 52, 24);
		search->maxLen = 60;
		search->changed = searchChanged;
		search->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
		addChild (search);
	}

	void refresh () { rebuild (); invalidate (true); }
	void tick () { if (m_rev != g_c.rev) { m_rev = g_c.rev; refresh (); } }
	void scrollToOpen ()
	{
		for (int i = 0; i < m_n; i++) if (m_rows[i].peer == g_mainPane.open) { ensure (i); break; }
	}
	// keys from the window: up / down move the selection, Enter opens
	bool moveSel (int d)
	{
		int cur = -1;
		for (int i = 0; i < m_n; i++) if (m_rows[i].kind != R_GROUP && m_rows[i].peer == g_mainPane.open) cur = i;
		for (int i = cur + d; i >= 0 && i < m_n; i += d)
			if (m_rows[i].kind != R_GROUP) { open_conversation (m_rows[i].peer); ensure (i); return true; }
		return false;
	}

	void onDraw () override
	{
		Canvas &cv = canvas;
		cv.clear (TC_BG);
		int top = HEAD_H + SEARCH_H, y = top - m_scroll;
		for (int i = 0; i < m_n; i++)
		{
			Row &r = m_rows[i];
			int h = rowH (r);
			if (y + h > top && y < height) drawRow (cv, r, i, y, h);
			y += h;
		}
		// over the rows scrolled under them: me, the search field's band
		drawMe (cv);
		fill_grad (cv, 0, HEAD_H, width, SEARCH_H, TC_SKY_BOT, 0xFFFFFF);
		cv.fillRect (0, HEAD_H + SEARCH_H - 1, width, 1, TC_LINE);
		// "+": a contact added by phone number
		int ax = width - 38, ay = HEAD_H + 4;
		if (m_addHot) { uk_rbox (cv, ax, ay, 28, 26, 5, TC_HOT_TOP, TC_HOT_BOT); uk_rline (cv, ax, ay, 28, 26, 5, TC_HOT_RIM); }
		buddy (cv, ax + 3, ay + 5, 16, TC_ONLINE);
		{ VPath p; p.circle (V (ax + 21), V (ay + 17), V (6)); p.fill (cv, 0xFFFFFF); p.clear (); p.circle (V (ax + 21), V (ay + 17), V (5)); p.fill (cv, 0x2F8CE0);
		  p.clear (); p.line (V (ax + 18), V (ay + 17), V (ax + 24), V (ay + 17), V (1) + 8); p.line (V (ax + 21), V (ay + 14), V (ax + 21), V (ay + 20), V (1) + 8); p.fill (cv, 0xFFFFFF); }
		// the scroll bar
		int total = contentH (), view = height - top;
		if (total > view)
		{
			UkThumb t = uk_thumb (total, view, m_scroll, view - 4);
			uk_draw_vscroll (cv, width - UK_SBW - 1, top + 2, UK_SBW, view - 4, t, TC_BG);
		}
		cv.fillRect (width - 1, 0, 1, height, TC_LINE);
		if (!m_n && g_c.dialogsLoaded)
			ftext (cv, g_face.ui, 16, top + 16, search->text[0] ? TR ("Nobody by that name.") : TR ("No conversations yet."), TC_GREY);
		else if (!g_c.dialogsLoaded)
			ftext (cv, g_face.ui, 16, top + 16, TR ("Loading your conversations..."), TC_GREY);
	}

	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) br; (void) bm;
		if (mx < 0) { setHot (-1); m_meHot = m_addHot = false; invalidate (true); return false; }
		int top = HEAD_H + SEARCH_H;
		if (wheel) { scrollBy (-wheel * 3 * CONTACT_H); return true; }
		bool me = my < HEAD_H && mx >= 76 && mx < 76 + m_meW && my >= 30 && my < 52;
		if (me != m_meHot) { m_meHot = me; invalidate (true); }
		bool add = my >= HEAD_H + 4 && my < HEAD_H + 30 && mx >= width - 38 && mx < width - 10;
		if (add != m_addHot) { m_addHot = add; invalidate (true); }
		if (add) { uk_cursor (KAPI_CURSOR_HAND); if (bl && !m_down) { m_down = true; add_contact_dialog (); } else if (!bl) m_down = false; return true; }
		if (my < top) { setHot (-1); if (bl && !m_down && me) { m_down = true; status_menu (76, 52); } if (!bl) m_down = false; return my >= HEAD_H ? false : true; }
		int row = rowAt (my);
		setHot (row);
		if (bl && !m_down)
		{
			m_down = true;
			if (row >= 0)
			{
				Row &r = m_rows[row];
				if (r.kind == R_GROUP) { folded[r.group] = !folded[r.group]; refresh (); }
				else open_conversation (r.peer);
			}
		}
		else if (!bl) m_down = false;
		return true;
	}

	void resizeTo (int w, int h) override { Widget::resizeTo (w, h); clampScroll (); }

private:
	enum { R_GROUP, R_CONV, R_CONTACT };
	struct Row { int kind, group, count; long long peer; };
	int m_n, m_cap;
	Row *m_rows;
	int m_scroll, m_hot;
	bool m_down, m_meHot, m_addHot = false;
	int m_meW = 100;
	unsigned m_rev;
	unsigned m_lastClick;
	int m_lastRow;

	static void searchChanged (Widget &w) { (void) w; extern BuddyList *g_list; g_list->m_scroll = 0; g_list->refresh (); }

	int rowH (const Row &r) const { return r.kind == R_GROUP ? GROUP_H : r.kind == R_CONV ? CONV_H : CONTACT_H; }
	int contentH () const { int h = 0; for (int i = 0; i < m_n; i++) h += rowH (m_rows[i]); return h + 8; }
	int rowAt (int my) const
	{
		int y = HEAD_H + SEARCH_H - m_scroll;
		for (int i = 0; i < m_n; i++) { int h = rowH (m_rows[i]); if (my >= y && my < y + h) return i; y += h; }
		return -1;
	}
	void ensure (int i)
	{
		int y = 0;
		for (int k = 0; k < i; k++) y += rowH (m_rows[k]);
		int view = height - HEAD_H - SEARCH_H, h = rowH (m_rows[i]);
		if (y < m_scroll) m_scroll = y;
		else if (y + h > m_scroll + view) m_scroll = y + h - view;
		clampScroll (); invalidate (true);
	}
	void scrollBy (int d) { m_scroll += d; clampScroll (); invalidate (true); }
	void clampScroll ()
	{
		int most = contentH () - (height - HEAD_H - SEARCH_H);
		if (m_scroll > most) m_scroll = most;
		if (m_scroll < 0) m_scroll = 0;
	}
	void setHot (int r) { if (r != m_hot) { m_hot = r; invalidate (true); } }

	bool matches (long long peer)
	{
		if (!search->text[0]) return true;
		char name[160];
		g_c.peerName (peer, name, sizeof name);
		const char *q = search->text;
		for (const char *s = name; *s; s++)
		{
			int k = 0;
			while (q[k] && s[k] && ((s[k] | 32) == (q[k] | 32))) k++;
			if (!q[k]) return true;
		}
		if (tg::ptype (peer) == tg::P_USER) { tg::User *u = g_c.user (tg::pid (peer)); if (u && u->username && strstr (u->username, q)) return true; }
		return false;
	}
	void push (int kind, int group, long long peer, int count = 0)
	{
		if (m_n == m_cap) { m_cap = m_cap ? m_cap * 2 : 128; m_rows = (Row *) realloc (m_rows, sizeof (Row) * (size_t) m_cap); }
		m_rows[m_n].kind = kind; m_rows[m_n].group = group; m_rows[m_n].peer = peer; m_rows[m_n].count = count;
		m_n++;
	}
	void rebuild ()
	{
		m_n = 0;
		g_c.dialogs ();
		// favourites and conversations
		int nf = 0, nc = 0;
		for (int i = 0; i < g_c.norder; i++)
		{
			tg::Conv *cv = g_c.conv (g_c.order[i]);
			if (!cv || !matches (cv->peer)) continue;
			if (cv->pinned) nf++; else nc++;
		}
		if (nf)
		{
			push (R_GROUP, G_FAV, 0, nf);
			if (!folded[G_FAV]) for (int i = 0; i < g_c.norder; i++) { tg::Conv *cv = g_c.conv (g_c.order[i]); if (cv && cv->pinned && matches (cv->peer)) push (R_CONV, G_FAV, cv->peer); }
		}
		push (R_GROUP, G_CONV, 0, nc);
		if (!folded[G_CONV]) for (int i = 0; i < g_c.norder; i++) { tg::Conv *cv = g_c.conv (g_c.order[i]); if (cv && !cv->pinned && matches (cv->peer)) push (R_CONV, G_CONV, cv->peer); }
		// the contacts: online first, then by name
		int start = m_n, nk = 0;
		push (R_GROUP, G_CONTACTS, 0, 0);
		int now = g_c.serverTime ();
		for (int i = 0; i < g_c.users.n; i++)
		{
			tg::User *u = g_c.users.a[i];
			if (!u->contact || u->id == g_c.selfId || u->deleted) continue;
			long long peer = tg::pkey (tg::P_USER, u->id);
			if (!matches (peer)) continue;
			nk++;
			if (!folded[G_CONTACTS]) push (R_CONTACT, G_CONTACTS, peer);
		}
		m_rows[start].count = nk;
		if (!nk && !search->text[0] && !g_c.contactsLoaded) { }
		// sort the contacts
		for (int i = start + 2; i < m_n; i++)
		{
			Row k = m_rows[i]; int j = i - 1;
			while (j > start && contactBefore (k.peer, m_rows[j].peer, now)) { m_rows[j + 1] = m_rows[j]; j--; }
			m_rows[j + 1] = k;
		}
		clampScroll ();
	}
	bool contactBefore (long long a, long long b, int now)
	{
		tg::User *ua = g_c.user (tg::pid (a)), *ub = g_c.user (tg::pid (b));
		int sa = user_ts (ua, now), sb = user_ts (ub, now);
		if (sa != sb) return sa < sb;
		char na[96], nb[96];
		g_c.peerName (a, na, sizeof na); g_c.peerName (b, nb, sizeof nb);
		return strcasecmp (na, nb) < 0;
	}

	void drawMe (Canvas &cv)
	{
		sky (cv, 0, 0, width, HEAD_H, false);
		long long me = tg::pkey (tg::P_USER, g_c.selfId);
		unsigned st = g_c.wantOnline () && g_c.online ? TC_ONLINE : TC_OFFLINE;
		avatar_framed (cv, g_c, me, 14, 16, 52, st);
		char name[128];
		g_c.peerName (me, name, sizeof name);
		ftext (cv, g_face.big, 78, 12, name, TC_TITLE, 2, width - 90);
		// "(Online)" and its arrow, the status to change
		const char *s = !g_c.online ? TR ("Connecting...") : g_c.wantOnline () ? TR ("Online") : TR ("Appear offline");
		char lab[80];
		snprintf (lab, sizeof lab, "(%s)", s);
		int lw = ftw (g_face.ui, lab);
		m_meW = lw + 20;
		if (m_meHot) uk_rbox (cv, 74, 32, lw + 22, 20, 3, TC_HOT_TOP, TC_HOT_BOT);
		buddy (cv, 78, 34, 14, st);
		ftext (cv, g_face.ui, 94, 34, lab, TC_GREY);
		uk_glyph (cv, WKG_CHEV_DOWN, 94 + lw + 6, 43, 7, TC_GREY);
		// my handle in grey
		tg::User *u = g_c.self ();
		char h[80] = "";
		if (u && u->username && u->username[0]) snprintf (h, sizeof h, "@%s", u->username);
		else if (u && u->phone && u->phone[0]) snprintf (h, sizeof h, "+%s", u->phone);
		ftext (cv, g_face.small, 78, 58, h, TC_GREY, 1, width - 90);
	}

	void drawRow (Canvas &cv, Row &r, int i, int y, int h)
	{
		int w = width - UK_SBW - 4;
		if (r.kind == R_GROUP)
		{
			static const char *titles[] = { TRN ("Favourites"), TRN ("Conversations"), TRN ("Contacts") };
			uk_glyph (cv, folded[r.group] ? WKG_RIGHT : WKG_DOWN, 14, y + h / 2, 8, TC_TITLE);
			char t[80];
			snprintf (t, sizeof t, "%s (%d)", TR (titles[r.group]), r.count);
			ftext (cv, g_face.ui, 24, y + (h - g_face.ui->height ()) / 2, t, i == m_hot ? TC_LINK : TC_TITLE, 2);
			return;
		}
		bool sel = r.peer == g_mainPane.open, hot = i == m_hot;
		if (sel) { uk_rbox (cv, 3, y + 1, w - 2, h - 2, 3, TC_SEL_TOP, TC_SEL_BOT); uk_rline (cv, 3, y + 1, w - 2, h - 2, 3, TC_SEL_RIM); }
		else if (hot) { uk_rbox (cv, 3, y + 1, w - 2, h - 2, 3, TC_HOT_TOP, TC_HOT_BOT); uk_rline (cv, 3, y + 1, w - 2, h - 2, 3, TC_HOT_RIM); }
		int now = g_c.serverTime ();
		char name[160];
		g_c.peerName (r.peer, name, sizeof name);
		if (r.kind == R_CONTACT)
		{
			tg::User *u = g_c.user (tg::pid (r.peer));
			int ts = user_ts (u, now);
			buddy (cv, 14, y + (h - 16) / 2, 16, ts_colour (ts));
			int nw = ftw (g_face.ui, name);
			if (nw > w - 50) nw = w - 50;
			ftext (cv, g_face.ui, 36, y + (h - g_face.ui->height ()) / 2, name, ts == TS_OFFLINE ? TC_GREY : TC_INK, 0, w - 50);
			char st[80];
			status_text (g_c, u, st, sizeof st);
			char dash[90];
			snprintf (dash, sizeof dash, "- %s", st);
			if (36 + nw + 8 < w - 20) ftext (cv, g_face.small, 36 + nw + 6, y + (h - g_face.small->height ()) / 2 + 1, dash, TC_LIGHT, 1, w - (36 + nw + 6) - 4);
			return;
		}
		tg::Conv *cv2 = g_c.conv (r.peer);
		avatar_round (cv, g_c, r.peer, 10, y + (h - 36) / 2, 36);
		if (tg::ptype (r.peer) == tg::P_USER)
		{
			tg::User *u = g_c.user (tg::pid (r.peer));
			if (u && user_ts (u, now) == TS_ONLINE)
			{
				VPath p; p.circle (V (10 + 31), V (y + (h - 36) / 2 + 31), V (6)); p.fill (cv, 0xFFFFFF);
				p.clear (); p.circle (V (10 + 31), V (y + (h - 36) / 2 + 31), V (4)); p.fill (cv, TC_ONLINE);
			}
		}
		int tx = 54;
		char tm[16];
		short_time (cv2 ? cv2->topDate : 0, tm, sizeof tm);
		int tmw = ftw (g_face.small, tm);
		bool group = tg::ptype (r.peer) != tg::P_USER;
		int nx = tx;
		if (group) { buddy (cv, tx, y + 7, 14, 0x3A8EE6, true); nx += 17; }
		ftext (cv, g_face.ui, nx, y + 7, name, TC_INK, 2, w - nx - tmw - 10);
		ftext (cv, g_face.small, w - tmw - 4, y + 9, tm, cv2 && cv2->unread ? TC_UNREAD : TC_LIGHT);
		// the preview (or "typing...")
		char pv[200];
		int unread = cv2 ? cv2->unread : 0;
		int pillW = 0;
		if (unread)
		{
			char u[16]; snprintf (u, sizeof u, "%d", unread > 999 ? 999 : unread);
			int uw = ftw (g_face.small, u, 2);
			pillW = uw + 12 < 20 ? 20 : uw + 12;
			uk_rbox (cv, w - pillW - 2, y + 27, pillW, 17, 8, uk_mix (TC_UNREAD, 0xFFFFFF, 60), TC_UNREAD);
			ftext (cv, g_face.small, w - pillW - 2 + (pillW - uw) / 2, y + 28, u, 0xFFFFFF, 2);
		}
		if (cv2 && cv2->typingUntil)
		{
			snprintf (pv, sizeof pv, "%s", TR ("typing..."));
			ftext (cv, g_face.ui, tx, y + 27, pv, TC_LINK, 1, w - tx - pillW - 8);
		}
		else
		{
			preview (cv2, pv, sizeof pv);
			// the first line of it, its emoticons drawn small, "..." if it goes on
			Rich rr;
			int lim = w - tx - pillW - 8, dots = ftw (g_face.ui, "...");
			rich_layout (rr, g_face.ui, pv, lim - dots, 15, true);
			char buf[256];
			int fh = g_face.ui->height ();
			for (int k = 0; k < rr.n; k++)
			{
				RichPiece &pc = rr.p[k];
				if (pc.line > 0) break;
				if (pc.kind == RP_EMO) { emo_draw (cv, pc.emo, tx + pc.x, y + 27 + (fh - 15) / 2, 15); continue; }
				int len = pc.len < 255 ? pc.len : 255;
				memcpy (buf, pv + pc.off, (size_t) len); buf[len] = 0;
				g_face.ui->draw (cv, tx + pc.x, y + 27, buf, TC_GREY, 0);
			}
			if (rr.lines > 1)
			{
				int ex = 0;
				for (int k = 0; k < rr.n && rr.p[k].line == 0; k++) ex = rr.p[k].x + rr.p[k].w;
				g_face.ui->draw (cv, tx + ex, y + 27, "...", TC_GREY, 0);
			}
		}
	}
};
BuddyList *g_list;

#endif
