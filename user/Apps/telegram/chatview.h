//
// chatview.h -- the right of the window, Messenger's conversation: the header on the sky (the picture in its
// glass frame, the name, the status or "... is typing a message"), the messages in the way of Messenger --
// "Alice says:" over what she wrote, the time on the right (and for ours the ticks: sent, read), a day's
// line, a group's events in grey, the emoticons as pictures --, the display pictures' column (theirs on
// top, mine at the bottom), and the line to write in with its emoticons (chatinput.h).
//
// MIT licence.
//
#ifndef TG_CHATVIEW_H
#define TG_CHATVIEW_H

#include "buddylist.h"

static void open_link (const char *url);
static void open_picture (const char *path);

// "HH:MM" of a server time, in the local time.
static void hhmm (int t, char *out, int cap)
{
	struct kapi_clock_info ci;
	int tz = kapi_clock_info (&ci) == 0 ? ci.tz_minutes * 60 : 0;
	long lt = t + tz;
	snprintf (out, (size_t) cap, "%02ld:%02ld", (lt % 86400 + 86400) % 86400 / 3600, (lt % 3600 + 3600) % 3600 / 60);
}
static long local_day (int t)
{
	struct kapi_clock_info ci;
	int tz = kapi_clock_info (&ci) == 0 ? ci.tz_minutes * 60 : 0;
	return ((long) t + tz) / 86400;
}
// A day's title: Today, Yesterday, or the date.
static void day_title (int t, char *out, int cap)
{
	long d = local_day (t), today = local_day (g_c.serverTime ());
	if (d == today) { snprintf (out, (size_t) cap, "%s", TR ("Today")); return; }
	if (d == today - 1) { snprintf (out, (size_t) cap, "%s", TR ("Yesterday")); return; }
	long z = d + 719468, era = z / 146097, doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153, dd = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
	long y = yoe + era * 400 + (m <= 2);
	static const char *mon[] = { TRN ("January"), TRN ("February"), TRN ("March"), TRN ("April"), TRN ("May"), TRN ("June"), TRN ("July"), TRN ("August"), TRN ("September"), TRN ("October"), TRN ("November"), TRN ("December") };
	static const char *wd[] = { TRN ("Thursday"), TRN ("Friday"), TRN ("Saturday"), TRN ("Sunday"), TRN ("Monday"), TRN ("Tuesday"), TRN ("Wednesday") };
	snprintf (out, (size_t) cap, "%s %ld %s %ld", TR (wd[((d % 7) + 7) % 7]), dd, TR (mon[m - 1]), y);
}

// A name short: a user's first name, a chat's title.
static void short_name (long long peer, char *out, int cap)
{
	if (tg::ptype (peer) == tg::P_USER)
	{
		tg::User *u = g_c.user (tg::pid (peer));
		if (u && u->first && u->first[0]) { snprintf (out, (size_t) cap, "%s", u->first); return; }
	}
	g_c.peerName (peer, out, cap);
}

// ---- the header -------------------------------------------------------------------------------------------

class ConvHeader : public Widget
{
public:
	ConvHeader (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		Canvas &cv = canvas;
		sky (cv, 0, 0, width, height);
		if (!g_open) return;
		int now = g_c.serverTime ();
		tg::Conv *c = g_c.conv (g_open);
		bool user = tg::ptype (g_open) == tg::P_USER;
		tg::User *u = user ? g_c.user (tg::pid (g_open)) : 0;
		unsigned st = user ? ts_colour (user_ts (u, now)) : ts_colour (TS_GROUP);
		avatar_framed (cv, g_c, g_open, 16, 12, 50, st);
		char name[160];
		g_c.peerName (g_open, name, sizeof name);
		ftext (cv, g_face.title, 84, 12, name, TC_TITLE, 2, width - 100);
		char line[200] = "";
		unsigned col = TC_GREY;
		if (c && c->typingUntil)
		{
			char who[80];
			short_name (c->typingUser, who, sizeof who);
			snprintf (line, sizeof line, c->typingKind == 1 ? TR ("%s is recording a voice message...") : TR ("%s is typing a message..."), who);
			col = TC_LINK;
		}
		else if (user) status_text (g_c, u, line, sizeof line);
		else
		{
			tg::Chat *ch = g_c.chat (tg::pid (g_open));
			if (ch && ch->members) snprintf (line, sizeof line, ch->kind == 3 ? TR ("%d subscribers") : TR ("%d members"), ch->members);
			else snprintf (line, sizeof line, "%s", ch && ch->kind == 3 ? TR ("channel") : TR ("group"));
		}
		if (user && u && u->username && u->username[0] && !(c && c->typingUntil))
		{
			char l2[240]; snprintf (l2, sizeof l2, "%s  -  @%s", line, u->username); snprintf (line, sizeof line, "%s", l2);
		}
		ftext (cv, g_face.ui, 84, 40, line, col, c && c->typingUntil ? 1 : 0, width - 100);
	}
};

// ---- the bar of a person who is not a contact: "Add to contacts", "Block" (or "Unblock"), the cross ---------

class PeerBar : public Widget
{
public:
	PeerBar (int l, int t, int w, int h) : Widget (l, t, w, h), m_hot (-1), m_down (false) {}
	void onDraw () override
	{
		Canvas &cv = canvas;
		fill_grad (cv, 0, 0, width, height, 0xFFF9DB, 0xFFF1B8);
		cv.fillRect (0, height - 1, width, 1, 0xE6CF7A);
		tg::Conv *c = g_open ? g_c.conv (g_open) : 0;
		if (!c) return;
		char name[80], t[200];
		short_name (g_open, name, sizeof name);
		// the information sign
		VPath p; p.circle (V (16), V (height / 2), V (8)); p.fill (cv, 0xE0A100);
		ftext (cv, g_face.ui, 13, (height - g_face.ui->height ()) / 2, "!", 0xFFFFFF, 2);
		if (c->blocked) snprintf (t, sizeof t, TR ("You blocked %s: they cannot write to you."), name);
		else snprintf (t, sizeof t, TR ("%s is not in your contacts."), name);
		int nb = c->blocked ? 1 : 3;
		const char *lab[3] = { c->blocked ? TR ("Unblock") : TR ("Add to contacts"), TR ("Block"), "" };
		// the buttons from the right
		int x = width - 8;
		for (int i = nb - 1; i >= 0; i--)
		{
			int bw = i == 2 ? 24 : ftw (g_face.ui, lab[i], 2) + 22;
			x -= bw;
			m_bx[i] = x; m_bw[i] = bw;
			if (i == 2) { uk_glyph (cv, WKG_CLOSE, x + 12, height / 2, 9, m_hot == 2 ? TC_BUSY : 0x8A7020); x -= 6; continue; }
			uk_rbox (cv, x, 5, bw, height - 10, 4, m_hot == i ? 0xFFFFFF : 0xFFFDF2, m_hot == i ? 0xF2F7FD : 0xF6EDCF);
			uk_rline (cv, x, 5, bw, height - 10, 4, m_hot == i ? TC_SEL_RIM : 0xD7BE6A);
			ftext (cv, g_face.ui, x + 11, (height - g_face.ui->height ()) / 2, lab[i], i == 1 ? TC_BUSY : TC_LINK, 2);
			x -= 8;
		}
		for (int i = nb; i < 3; i++) m_bw[i] = 0;
		ftext (cv, g_face.ui, 32, (height - g_face.ui->height ()) / 2, t, 0x5A4500, 0, x - 36);
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) br; (void) bm; (void) wheel; (void) my;
		int hot = -1;
		for (int i = 0; i < 3; i++) if (m_bw[i] && mx >= m_bx[i] && mx < m_bx[i] + m_bw[i]) hot = i;
		if (mx < 0) hot = -1;
		if (hot != m_hot) { m_hot = hot; invalidate (true); }
		if (hot >= 0) uk_cursor (KAPI_CURSOR_HAND);
		if (bl && !m_down && hot >= 0 && g_open)
		{
			tg::Conv *c = g_c.conv (g_open);
			if (c && c->blocked) g_c.block (g_open, false);
			else if (hot == 0) g_c.addToContacts (g_open);
			else if (hot == 1)
			{
				char name[80], q[200];
				short_name (g_open, name, sizeof name);
				snprintf (q, sizeof q, TR ("Block %s? They will not be able to write to you or call you."), name);
				if (ft_messagebox (TR ("Block"), q, MB_YESNO) == 1) g_c.block (g_open, true);
			}
			else if (hot == 2) g_c.hideBar (g_open);
		}
		m_down = bl != 0;
		return mx >= 0;
	}
private:
	int m_hot;
	bool m_down;
	int m_bx[3] = { 0, 0, 0 }, m_bw[3] = { 0, 0, 0 };
};

// ---- the messages ------------------------------------------------------------------------------------------

class ChatView : public Widget
{
public:
	ChatView (int l, int t, int w, int h) : Widget (l, t, w, h), m_it (0), m_n (0), m_cap (0), m_total (0), m_scroll (0), m_stick (true),
						    m_peer (0), m_rev (0), m_w (0), m_down (false), m_hotLink (false) {}

	void reset () { m_peer = 0; m_stick = true; m_scroll = 0; refresh (); }
	void tick ()
	{
		tg::Conv *c = g_open ? g_c.conv (g_open) : 0;
		unsigned r = c ? c->rev : 0;
		if (g_open != m_peer || r != m_rev || width != m_w) refresh ();
	}
	void refresh ()
	{
		tg::Conv *c = g_open ? g_c.conv (g_open) : 0;
		if (g_open != m_peer) { m_stick = true; m_scroll = 0; }
		int oldTotal = m_total, oldScroll = m_scroll;
		long long oldFirst = m_n ? firstId () : 0;
		m_peer = g_open; m_rev = c ? c->rev : 0; m_w = width;
		layout (c);
		if (m_stick) m_scroll = m_total - height;
		else if (oldFirst && m_n && firstId () < oldFirst) m_scroll = oldScroll + (m_total - oldTotal);	// (older ones came above)
		clampScroll ();
		invalidate (true);
	}
	void scrollBy (int d) { m_scroll += d; clampScroll (); m_stick = m_scroll >= m_total - height - 4; invalidate (true); maybeOlder (); }
	void pageUp () { scrollBy (-(height - 40)); }
	void pageDown () { scrollBy (height - 40); }

	void onDraw () override
	{
		Canvas &cv = canvas;
		cv.clear (TC_BG);
		if (!g_open) return;
		tg::Conv *c = g_c.conv (g_open);
		for (int i = 0; i < m_n; i++)
		{
			Item &it = m_it[i];
			int y = it.y - m_scroll;
			if (y + it.h < 0 || y > height) continue;
			drawItem (cv, c, it, y);
		}
		if (c && c->loading) drawNote (cv, 8, TR ("Loading the messages..."));
		else if (c && !c->n && c->loaded) drawNote (cv, height / 2 - 10, TR ("No messages here yet. Say hello!"));
		if (m_total > height)
		{
			UkThumb t = uk_thumb (m_total, height, m_scroll, height - 4);
			uk_draw_vscroll (cv, width - UK_SBW - 1, 2, UK_SBW, height - 4, t, TC_BG);
		}
	}

	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) br; (void) bm;
		if (mx < 0) { m_down = false; return false; }
		if (wheel) { scrollBy (-wheel * 48); return true; }
		char url[512], pic[160];
		bool link = linkAt (mx, my + m_scroll, url, sizeof url);
		bool picture = !link && pictureAt (mx, my + m_scroll, pic, sizeof pic);
		uk_cursor (link || picture ? KAPI_CURSOR_HAND : KAPI_CURSOR_ARROW);
		if (bl && !m_down)
		{
			m_down = true;
			if (link) open_link (url);
			else if (picture) open_picture (pic);
		}
		else if (!bl) m_down = false;
		return true;
	}
	void resizeTo (int w, int h) override { Widget::resizeTo (w, h); refresh (); }

private:
	enum { I_DAY, I_HEAD, I_MSG, I_SERVICE, I_GAP };
	struct Item { int kind, y, h, msg; Rich *rich; int textX, big, phW, phH; };
	Item *m_it;
	int m_n, m_cap, m_total, m_scroll;
	bool m_stick;
	long long m_peer;
	unsigned m_rev;
	int m_w;
	bool m_down, m_hotLink;
	struct Kept { int item; char *s; };
	Kept *m_kept = 0; int m_nkept = 0;

	enum { LEFT = 30, TIMEW = 64 };
	long long firstId () { tg::Conv *c = g_c.conv (m_peer); return c && c->n ? c->m[0].id : 0; }

	void clampScroll ()
	{
		int most = m_total - height;
		if (m_scroll > most) m_scroll = most;
		if (m_scroll < 0) m_scroll = 0;
	}
	void maybeOlder ()
	{
		if (m_scroll < 60 && g_open) g_c.loadOlder (g_open);
	}

	Item &push (int kind, int msg, int h)
	{
		if (m_n == m_cap) { m_cap = m_cap ? m_cap * 2 : 64; m_it = (Item *) realloc (m_it, sizeof (Item) * (size_t) m_cap); }
		Item &it = m_it[m_n++];
		memset (&it, 0, sizeof it);
		it.kind = kind; it.msg = msg; it.h = h; it.y = m_total;
		m_total += h;
		return it;
	}
	void freeItems ()
	{
		for (int i = 0; i < m_n; i++) delete m_it[i].rich;
		for (int k = 0; k < m_nkept; k++) free (m_kept[k].s);
		m_n = 0; m_total = 0; m_nkept = 0;
	}

	// The text of a message that is not text: "[Photo]", a sticker's emoji big, a service's sentence.
	static void mediaText (tg::Msg &m, long long peer, char *out, int cap)
	{
		out[0] = 0;
		if (m.media == tg::M_SERVICE)
		{
			char who[80], whom[80] = "";
			short_name (m.from, who, sizeof who);
			if (m.svcUser) short_name (tg::pkey (tg::P_USER, m.svcUser), whom, sizeof whom);
			switch (m.service)
			{
			case tg::SV_CREATE: snprintf (out, (size_t) cap, TR ("%s created the group \"%s\""), who, m.extra); break;
			case tg::SV_TITLE: snprintf (out, (size_t) cap, TR ("%s changed the group's name to \"%s\""), who, m.extra); break;
			case tg::SV_PHOTO: snprintf (out, (size_t) cap, TR ("%s changed the group's picture"), who); break;
			case tg::SV_ADD: snprintf (out, (size_t) cap, TR ("%s added %s"), who, whom); break;
			case tg::SV_LEFT: snprintf (out, (size_t) cap, TR ("%s left the group"), who); break;
			case tg::SV_REMOVE: snprintf (out, (size_t) cap, TR ("%s removed %s"), who, whom); break;
			case tg::SV_JOINED_LINK: snprintf (out, (size_t) cap, TR ("%s joined the group"), who); break;
			case tg::SV_PIN: snprintf (out, (size_t) cap, TR ("%s pinned a message"), who); break;
			case tg::SV_CALL: snprintf (out, (size_t) cap, "%s", TR ("A call")); break;
			case tg::SV_JOINED_TG: snprintf (out, (size_t) cap, TR ("%s joined Telegram"), who); break;
			case tg::SV_SCREENSHOT: snprintf (out, (size_t) cap, TR ("%s took a screenshot"), who); break;
			default: snprintf (out, (size_t) cap, "%s", TR ("(an event of the group)")); break;
			}
			(void) peer;
			return;
		}
		static const char *names[] = { "", TRN ("Photo"), TRN ("Video"), TRN ("Voice message"), TRN ("Audio"), TRN ("File"), TRN ("Sticker"), TRN ("GIF"), TRN ("Location"), TRN ("Contact"), TRN ("Poll"), TRN ("Link"), TRN ("Something Onyx cannot show yet") };
		if (m.extra && m.extra[0] && m.media != tg::M_STICKER) snprintf (out, (size_t) cap, "[%s: %s]", TR (names[m.media < 13 ? m.media : 12]), m.extra);
		else snprintf (out, (size_t) cap, "[%s]", TR (names[m.media < 13 ? m.media : 12]));
	}

	void layout (tg::Conv *c)
	{
		freeItems ();
		if (!c) return;
		int textW = width - LEFT - TIMEW - UK_SBW - 8;
		if (textW < 60) textW = 60;
		m_total = 8;
		long lastDay = -1;
		long long lastFrom = -1;
		int lastDate = 0;
		for (int i = 0; i < c->n; i++)
		{
			tg::Msg &m = c->m[i];
			long d = local_day (m.date);
			if (d != lastDay) { push (I_DAY, i, 34); lastDay = d; lastFrom = -1; }
			if (m.media == tg::M_SERVICE)
			{
				char t[300]; mediaText (m, c->peer, t, sizeof t);
				Item &it = push (I_SERVICE, i, 0);
				it.rich = new Rich; rich_layout (*it.rich, g_face.small, t, width - 60, 14, false);
				it.h = it.rich->height () + 10; m_total += it.h;
				lastFrom = -1;
				continue;
			}
			long long from = m.from ? m.from : c->peer;
			if (from != lastFrom || m.date - lastDate > 600) { push (I_HEAD, i, lastFrom == -1 ? 24 : 30); lastFrom = from; }
			lastDate = m.date;
			Item &it = push (I_MSG, i, 0);
			it.rich = new Rich;
			const char *text = m.text;
			char mt[300];
			if (isPicture (m))			// a photo: the picture, its caption under it
			{
				fitPicture (m, it.phW, it.phH);
				rich_layout (*it.rich, g_face.ui, text, textW, 19);
				it.textX = LEFT;
				it.h = it.phH + 6 + (text[0] ? it.rich->height () + 2 : 0);
				m_total += it.h;
				continue;
			}
			if (m.media && m.media != tg::M_SERVICE)
			{
				mediaText (m, c->peer, mt, sizeof mt);
				if (m.media == tg::M_STICKER && m.extra && m.extra[0] && emo_at (m.extra, (int) strlen (m.extra), &it.big, false) >= 0)
				{
					snprintf (mt, sizeof mt, "%s", m.extra);
					it.big = 48;
				}
				else it.big = 0;
				// the label, then the caption under it
				static char both[4400];
				if (text && text[0] && !it.big) { snprintf (both, sizeof both, "%s\n%s", mt, text); text = both; }
				else text = mt;
			}
			else it.big = rich_only_emo (text) ? 32 : 0;
			rich_layout (*it.rich, g_face.ui, text, textW, it.big ? it.big : 19);
			// (the text is kept by the conversation; a label is not: the item keeps its own)
			it.textX = LEFT;
			it.h = it.rich->height () + 3;
			if (it.h < 20) it.h = 20;
			m_total += it.h;
			// the label's text, kept for drawing
			if (text != m.text) storeText (it, text);
		}
		m_total += 10;
	}
	// (an item whose text is not the message's own: a copy kept with it)
	void storeText (Item &it, const char *s)
	{
		m_kept = (Kept *) realloc (m_kept, sizeof (Kept) * (size_t) (m_nkept + 1));
		m_kept[m_nkept].item = (int) (&it - m_it); m_kept[m_nkept].s = tg::sdup (s); m_nkept++;
	}
	const char *itemText (int idx, tg::Msg &m)
	{
		for (int k = m_nkept - 1; k >= 0; k--) if (m_kept[k].item == idx) return m_kept[k].s;
		return m.text;
	}

	void drawNote (Canvas &cv, int y, const char *s)
	{
		int w = ftw (g_face.ui, s);
		uk_rbox (cv, (width - w) / 2 - 12, y, w + 24, 22, 11, 0xF1F6FB, 0xE4EEF8);
		ftext (cv, g_face.ui, (width - w) / 2, y + 3, s, TC_GREY, 1);
	}

	void drawItem (Canvas &cv, tg::Conv *c, Item &it, int y)
	{
		tg::Msg &m = c->m[it.msg];
		switch (it.kind)
		{
		case I_DAY:
		{
			char t[80]; day_title (m.date, t, sizeof t);
			int w = ftw (g_face.small, t, 2), cx = (width - UK_SBW) / 2;
			cv.fillRect (20, y + 17, cx - w / 2 - 30, 1, TC_LINE);
			cv.fillRect (cx + w / 2 + 10, y + 17, width - UK_SBW - 20 - (cx + w / 2 + 10), 1, TC_LINE);
			ftext (cv, g_face.small, cx - w / 2, y + 10, t, TC_GREY, 2);
			return;
		}
		case I_HEAD:
		{
			long long from = m.from ? m.from : c->peer;
			char name[120];
			if (m.out) snprintf (name, sizeof name, "%s", g_c.self () ? (g_c.self ()->first ? g_c.self ()->first : "") : "");
			else short_name (from, name, sizeof name);
			int ty = y + it.h - g_face.ui->height () - 2;
			if (tg::ptype (c->peer) != tg::P_USER && !m.out) avatar_round (cv, g_c, from, 6, ty - 1, 18);
			else buddy (cv, 8, ty + 1, 14, m.out ? TC_ME : TC_THEM);
			int nw = ftw (g_face.ui, name, 2);
			unsigned nc = m.out ? TC_ME : TC_THEM;
			if (!m.out && tg::ptype (c->peer) != tg::P_USER) nc = uk_mix (tc_peer[peer_colour (tg::pid (from))][1], 0x000000, 70);	// (a group: each one's colour)
			ftext (cv, g_face.ui, LEFT, ty, name, nc, 2, width - LEFT - 120);
			ftext (cv, g_face.ui, LEFT + nw + 5, ty, TR ("says:"), TC_GREY);
			return;
		}
		case I_SERVICE:
		{
			const char *t = itemText ((int) (&it - m_it), m);
			char buf[300]; mediaText (m, c->peer, buf, sizeof buf); t = buf;
			int w = it.rich->widest, x = (width - UK_SBW - w) / 2;
			rich_draw (cv, *it.rich, g_face.small, t, x, y + 5, TC_GREY, 1);
			return;
		}
		case I_MSG:
		{
			const char *t = itemText ((int) (&it - m_it), m);
			unsigned ink = m.pending ? TC_GREY : m.failed ? TC_BUSY : TC_INK;
			if (it.phW)
			{
				drawPicture (cv, m, it.textX, y + 2, it.phW, it.phH);
				if (m.text && m.text[0]) rich_draw (cv, *it.rich, g_face.ui, m.text, it.textX, y + it.phH + 6, ink);
				drawTime (cv, c, m, y, 0, it.rich->lineH);
				return;
			}
			rich_draw (cv, *it.rich, g_face.ui, t, it.textX, y, ink, m.media && m.media != tg::M_SERVICE && !it.big ? 0 : 0);
			// the media label in grey italics over the black: drawn again (first line) when it is a label
			if (m.media && !it.big)
			{
				char lab[300]; mediaText (m, c->peer, lab, sizeof lab);
				int lw = ftw (g_face.ui, lab);
				cv.fillRect (it.textX, y + 1, lw + 2, it.rich->lineH - 2, TC_BG);
				int ic = it.textX;
				mediaIcon (cv, m.media, ic, y + (it.rich->lineH - 14) / 2);
				ftext (cv, g_face.ui, ic + 18, y + (it.rich->lineH - g_face.ui->height ()) / 2, lab, TC_GREY, 1, width - ic - TIMEW - 30);
			}
			drawTime (cv, c, m, y, it.big, it.rich->lineH);
			return;
		}
		}
	}

	// the time on the right, and for ours the ticks (a clock while it goes, a red ! if it could not)
	void drawTime (Canvas &cv, tg::Conv *c, tg::Msg &m, int y, int big, int lineH)
	{
		char tm[8]; hhmm (m.date, tm, sizeof tm);
		int tx = width - UK_SBW - TIMEW + 6, ty = y + (lineH - g_face.small->height ()) / 2;
		if (big) ty = y + 4;
		ftext (cv, g_face.small, tx, ty, tm, TC_LIGHT);
		if (m.out)
		{
			int kx = tx + ftw (g_face.small, tm) + 5, ky = ty + 3;
			if (m.failed) ftext (cv, g_face.small, kx, ty, "!", TC_BUSY, 2);
			else if (m.pending) { VPath p; p.arc (V (kx + 4), V (ky + 4), V (4), 0, 360, V (1)); p.line (V (kx + 4), V (ky + 4), V (kx + 4), V (ky + 1), V (1)); p.line (V (kx + 4), V (ky + 4), V (kx + 6), V (ky + 5), V (1)); p.fill (cv, TC_LIGHT); }
			else
			{
				bool read = c->readOutMax >= m.id;
				unsigned k = read ? 0x3FA34D : TC_LIGHT;
				VPath p;
				p.line (V (kx), V (ky + 4), V (kx + 3), V (ky + 7), V (1) + 6); p.line (V (kx + 3), V (ky + 7), V (kx + 8), V (ky), V (1) + 6);
				if (read) { p.line (V (kx + 5), V (ky + 6), V (kx + 6), V (ky + 7), V (1) + 6); p.line (V (kx + 6), V (ky + 7), V (kx + 11), V (ky), V (1) + 6); }
				p.fill (cv, k);
			}
		}
		if (m.edited) ftext (cv, g_face.small, tx - ftw (g_face.small, TR ("edited")) - 6, ty, TR ("edited"), TC_LIGHT, 1);
	}

	// ---- the pictures ----

	static bool isPicture (const tg::Msg &m) { return m.media == tg::M_PHOTO && (m.photoId || m.local); }
	void fitPicture (const tg::Msg &m, int &w, int &h)
	{
		int mw = width - LEFT - TIMEW - UK_SBW - 12, mh = 260;
		if (mw > 320) mw = 320;
		if (mw < 80) mw = 80;
		int pw = m.pw > 0 ? m.pw : 4, ph = m.ph > 0 ? m.ph : 3;
		w = mw; h = (int) ((long) ph * w / pw);
		if (h > mh) { h = mh; w = (int) ((long) pw * h / ph); }
		if (w < 40) w = 40;
		if (h < 30) h = 30;
	}
	struct Pic { char path[160]; int w, h; unsigned *px; };
	Pic m_pic[24];
	int m_npic = 0;
	unsigned *picture (const char *path, int w, int h)
	{
		for (int i = 0; i < m_npic; i++) if (m_pic[i].w == w && m_pic[i].h == h && !strcmp (m_pic[i].path, path)) return m_pic[i].px;
		ImgFrames im;
		if (!img_load (path, &im)) return 0;
		unsigned *px = scale_box (im.px[0], im.w, im.h, w, h);
		img_free (&im);
		if (m_npic == 24) { delete [] m_pic[0].px; memmove (m_pic, m_pic + 1, sizeof m_pic[0] * 23); m_npic--; }
		Pic &p = m_pic[m_npic++];
		snprintf (p.path, sizeof p.path, "%s", path); p.w = w; p.h = h; p.px = px;
		return px;
	}
	void drawPicture (Canvas &cv, tg::Msg &m, int x, int y, int w, int h)
	{
		char path[160];
		unsigned *px = g_c.msgPhoto (m, path, sizeof path) ? picture (path, w, h) : 0;
		if (px) blit_rect_round (cv, px, x, y, w, h, 6);
		else
		{
			uk_rbox (cv, x, y, w, h, 6, 0xE3ECF5, 0xCCD9E6);
			mediaIcon (cv, tg::M_PHOTO, x + w / 2 - 8, y + h / 2 - 14);
			const char *l = TR ("Loading the picture...");
			ftext (cv, g_face.small, x + (w - ftw (g_face.small, l)) / 2, y + h / 2 + 6, l, TC_GREY, 1);
		}
		uk_rline (cv, x, y, w, h, 6, TC_LINE);
		if (m.pending || m.failed)
		{
			char s[80];
			if (m.failed) snprintf (s, sizeof s, "%s", TR ("Not sent"));
			else snprintf (s, sizeof s, TR ("Sending... %d%%"), m.progress);
			int sw = ftw (g_face.small, s, 2) + 16;
			uk_rbox (cv, x + 8, y + h - 28, sw, 20, 10, 0x000000, 0x000000, 150);
			ftext (cv, g_face.small, x + 16, y + h - 26, s, m.failed ? 0xFF8A80 : 0xFFFFFF, 2);
			if (!m.failed)
			{
				int bw = w - 16;
				cv.fillRect (x + 8, y + h - 5, bw, 3, 0x7A8A9A);
				cv.fillRect (x + 8, y + h - 5, bw * m.progress / 100, 3, 0x6FD34A);
			}
		}
	}
	// the picture under (mx, my) (content coordinates) -> its file
	bool pictureAt (int mx, int my, char *path, int cap)
	{
		tg::Conv *c = g_open ? g_c.conv (g_open) : 0;
		if (!c) return false;
		for (int i = 0; i < m_n; i++)
		{
			Item &it = m_it[i];
			if (it.kind != I_MSG || !it.phW || my < it.y + 2 || my >= it.y + 2 + it.phH || mx < it.textX || mx >= it.textX + it.phW) continue;
			return g_c.msgPhoto (c->m[it.msg], path, cap);
		}
		return false;
	}

	static void mediaIcon (Canvas &cv, int media, int x, int y)
	{
		VPath p;
		switch (media)
		{
		case tg::M_PHOTO: case tg::M_GIF:
			uk_rbox (cv, x, y + 1, 15, 12, 2, 0x8EC5F0, 0x4A8FD0);
			p.circle (V (x + 4), V (y + 5), V (2)); p.fill (cv, 0xFFE27A);
			p.clear (); { int t[6] = { V (x + 2), V (y + 12), V (x + 7), V (y + 6), V (x + 12), V (y + 12) }; p.poly (t, 3); } p.fill (cv, 0x2E7D32);
			break;
		case tg::M_VIDEO:
			uk_rbox (cv, x, y + 2, 11, 10, 2, 0x6A7A8C, 0x3A4A5C);
			{ int t[6] = { V (x + 11), V (y + 7), V (x + 15), V (y + 3), V (x + 15), V (y + 11) }; p.poly (t, 3); p.fill (cv, 0x3A4A5C); }
			break;
		case tg::M_VOICE: case tg::M_AUDIO:
			p.rrect (V (x + 5), V (y), V (5), V (9), V (2) + 8); p.fill (cv, 0x5A6B7D);
			p.clear (); p.arc (V (x + 7) + 8, V (y + 6), V (5), 180, 360, V (1) + 4); p.line (V (x + 7) + 8, V (y + 11), V (x + 7) + 8, V (y + 14), V (1) + 4); p.fill (cv, 0x5A6B7D);
			break;
		case tg::M_GEO:
			p.circle (V (x + 7), V (y + 5), V (5)); { int t[6] = { V (x + 3), V (y + 8), V (x + 11), V (y + 8), V (x + 7), V (y + 14) }; p.poly (t, 3); } p.fill (cv, 0xE04848);
			p.clear (); p.circle (V (x + 7), V (y + 5), V (2)); p.fill (cv, 0xFFFFFF);
			break;
		default:
			{ int t[10] = { V (x + 2), V (y), V (x + 9), V (y), V (x + 13), V (y + 4), V (x + 13), V (y + 14), V (x + 2), V (y + 14) }; p.poly (t, 5); p.fill (cv, 0x8A96A3); }
			p.clear (); { int t[10] = { V (x + 3), V (y + 1), V (x + 9), V (y + 1), V (x + 12), V (y + 4), V (x + 12), V (y + 13), V (x + 3), V (y + 13) }; p.poly (t, 5); } p.fill (cv, 0xFFFFFF);
			break;
		}
	}

	bool linkAt (int mx, int my, char *url, int cap)
	{
		tg::Conv *c = g_open ? g_c.conv (g_open) : 0;
		if (!c) return false;
		for (int i = 0; i < m_n; i++)
		{
			Item &it = m_it[i];
			if (it.kind != I_MSG || my < it.y || my >= it.y + it.h) continue;
			return rich_link (*it.rich, itemText (i, c->m[it.msg]), mx - it.textX, my - it.y, url, cap);
		}
		return false;
	}
};

// ---- the display pictures' column ----------------------------------------------------------------------------

class DpColumn : public Widget
{
public:
	int inputH;
	DpColumn (int l, int t, int w, int h, int inH) : Widget (l, t, w, h), inputH (inH) {}
	void onDraw () override
	{
		Canvas &cv = canvas;
		fill_grad (cv, 0, 0, width, height, 0xF4F9FD, 0xE6F0F9);
		cv.fillRect (0, 0, 1, height, TC_LINE);
		if (!g_open) return;
		int now = g_c.serverTime ();
		int s = width - 32;
		if (s > 96) s = 96;
		bool user = tg::ptype (g_open) == tg::P_USER;
		unsigned st = user ? ts_colour (user_ts (g_c.user (tg::pid (g_open)), now)) : ts_colour (TS_GROUP);
		avatar_framed (cv, g_c, g_open, (width - s) / 2, 18, s, st);
		long long me = tg::pkey (tg::P_USER, g_c.selfId);
		int ms = s * 3 / 4;
		avatar_framed (cv, g_c, me, (width - ms) / 2, height - inputH + (inputH - ms) / 2, ms, g_c.wantOnline () && g_c.online ? TC_ONLINE : TC_OFFLINE);
	}
};

#endif
