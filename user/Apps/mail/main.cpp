//
// mail -- Onyx's mail client (docs/mail/README.md). Gmail (an app password), Outlook.com / Hotmail (Microsoft's
// sign-in by a code), any IMAP account, POP3 + SMTP. Three columns, as the mock-ups:
//   * at the left the unified inbox (every account's Inbox), the starred messages, then each account and its folders
//     (Inbox, Sent, Drafts, Archive, Junk, Trash and its own, the unread counted); Contacts; Accounts and settings;
//     what the worker is doing;
//   * in the middle the conversations (grouped as Gmail does -- View > Conversations --, or one message a row), by
//     day: who, the subject, the text's start, when, how many, a paper clip, a star, the account's stripe; All /
//     Unread; the search (who, subject, text, every account);
//   * at the right the conversation (read.h), or what is being written (compose.h), or the contacts
//     (contactsui.h).
// The tool bar: New message, Reply, Reply all, Forward, Archive, Delete, Junk, Star, the search, Check now. New
// mail is looked for every few minutes (each account's setting) and told by a notification. The messages, the
// folders, the accounts live on the card (store.h, accounts.h; the passwords and tokens encrypted); the servers are
// talked to by a worker thread (sync.h) while the window stays live. HTML messages are drawn by Mail's own renderer
// (user/mail/html.h: HTML 4, CSS 2; no scripts, the remote pictures held back until asked).
// "mail SD:/x.eml" shows a message file (fileassoc.ini: eml = mail).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#include "Apps/mail/app.h"
#include "Apps/mail/read.h"
#include "Apps/mail/compose.h"
#include "Apps/mail/wizard.h"
#include "Apps/mail/contactsui.h"

using namespace mailapp;

namespace mailapp {

static char g_note[300]; static unsigned g_noteT;
static bool g_emlMode;			// a .eml file shown alone ("mail SD:/x.eml"): read only
static void status_note (const char *s) { scpy (g_note, s, sizeof g_note); g_noteT = kapi_get_ticks (); refresh_all (); }

// ---- the tool bar ------------------------------------------------------------------------------------------------------------
static void search_changed ();
class SearchBox : public HintBox
{
public:
	SearchBox (int l, int t, int w, int h) : HintBox (l, t, w, h, "Search the mail") { maxLen = 190; }
	bool onKey (long k) override
	{
		if (k == 27) { setText (""); search_changed (); return true; }
		bool r = HintBox::onKey (k);
		return r;
	}
};
class ToolBar : public Widget
{
public:
	SearchBox *search;
	HitList hits;
	int hot;
	enum { T_NEW = 1, T_REPLY, T_REPLYALL, T_FORWARD, T_ARCHIVE, T_DELETE, T_JUNK, T_STAR, T_CHECK, T_BACK };
	ToolBar (int l, int t, int w, int h) : Widget (l, t, w, h), hot (0)
	{
		search = new SearchBox (w - 300, 10, 240, 30); addChild (search);
	}
	void place () { int sw = width > 900 ? 260 : 180; search->left = width - sw - 54; search->resizeTo (sw, 30); }
	void onDraw () override
	{
		canvas.clear (C_BG);
		hits.clear ();
		canvas.fillRect (0, height - 1, width, 1, wk_mix (C_BG, C_TEXT, 40));
		// New message: the accent pill
		int x = 12;
		{
			const char *l = "New message"; int w = tw (l, F_UI, 1) + 50;
			wk_fill_round (canvas, x, 8, w, 34, 6, hot == T_NEW ? wk_mix (C_ACCENT, 0xFFFFFF, 30) : C_ACCENT);
			icon (canvas, I_PEN, x + 12, 15, 20, C_SEL_TEXT);
			text_v (canvas, x + 38, 8, 34, l, C_SEL_TEXT, F_UI, 1);
			hits.add (x, 8, w, 34, T_NEW); x += w + 14;
		}
		bool have = g_conv >= 0 && !g_showContacts;
		bool composing = g_compose && !g_compose->hidden;
		struct B { int k, ic; const char *l; };
		static const B BS[] = { { T_REPLY, I_REPLY, "Reply" }, { T_REPLYALL, I_REPLYALL, "Reply all" }, { T_FORWARD, I_FORWARD, "Forward" },
			{ 0, 0, 0 }, { T_ARCHIVE, I_ARCHIVE, 0 }, { T_DELETE, I_TRASH, 0 }, { T_JUNK, I_JUNK, 0 }, { T_STAR, I_STAR_O, 0 } };
		int room = search->left - 20;
		for (unsigned i = 0; i < sizeof BS / sizeof BS[0]; i++)
		{
			const B &b = BS[i];
			if (!b.k) { canvas.fillRect (x + 4, 12, 1, 26, wk_mix (C_BG, C_TEXT, 50)); x += 14; continue; }
			bool on = have && !composing;
			bool labels = width > 860;
			int w = 34 + (b.l && labels ? tw (b.l) + 8 : 0);
			if (x + w > room) break;
			if (hot == b.k && on) wk_fill_round (canvas, x, 8, w, 34, 6, wk_mix (C_BG, C_TEXT, 25));
			unsigned c = on ? C_TEXT : wk_mix (C_BG, C_TEXT, 90);
			int ic = b.ic;
			if (b.k == T_STAR && have) { Ref r[60]; int n = conv_refs (r, 60); bool st = false; for (int k = 0; k < n; k++) if (g_m.msg (r[k]).flags & F_FLAGGED) st = true; if (st) { ic = I_STAR; c = 0xF2A600; } }
			icon (canvas, ic, x + 8, 15, 20, c);
			if (b.l && labels) text_v (canvas, x + 34, 8, 34, b.l, c);
			hits.add (x, 8, w, 34, b.k);
			x += w + 4;
		}
		// the search's lens, Check now
		icon (canvas, I_SEARCH, search->left - 24, 16, 18, wk_mix (C_BG, C_TEXT, 150));
		int rx = width - 44;
		if (hot == T_CHECK) wk_fill_round (canvas, rx, 8, 34, 34, 6, wk_mix (C_BG, C_TEXT, 25));
		bool busy = g_m.worker.busy || g_m.worker.head;
		icon (canvas, I_REFRESH, rx + 7, 15, 20, busy ? C_ACCENT : C_TEXT);
		hits.add (rx, 8, 34, 34, T_CHECK);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		const Hit *h = hits.at (mx, my);
		int nh = h ? h->kind : 0;
		if (nh != hot) { hot = nh; invalidate (true); }
		static bool was; bool down = bl && !was; was = bl;
		if (!down || !h) return h != 0;
		bool composing = g_compose && !g_compose->hidden;
		switch (h->kind)
		{
		case T_NEW: compose_new (0); break;
		case T_CHECK: act_check (); break;
		default:
			if (composing || g_conv < 0) break;
			{
				Ref r[60]; int n = conv_refs (r, 60); if (!n) break;
				if (h->kind == T_REPLY) compose_new (1, &r[n - 1]);
				else if (h->kind == T_REPLYALL) compose_new (2, &r[n - 1]);
				else if (h->kind == T_FORWARD) compose_new (3, &r[n - 1]);
				else if (h->kind == T_ARCHIVE) act_archive ();
				else if (h->kind == T_DELETE) act_delete ();
				else if (h->kind == T_JUNK) act_junk ();
				else if (h->kind == T_STAR) act_star ();
			}
		}
		return true;
	}
};

// ---- the left column ---------------------------------------------------------------------------------------------------------------
static bool g_open[12] = { true, true, true, true, true, true, true, true, true, true, true, true };	// an account's folders shown
class Sidebar : public Widget
{
public:
	HitList hits; int sy, contentH, hot;
	enum { S_UNIFIED = 1, S_STARRED, S_ACCOUNT, S_FOLDER, S_CONTACTS, S_SETTINGS, S_ADD };
	Sidebar (int l, int t, int w, int h) : Widget (l, t, w, h), sy (0), contentH (0), hot (-1) {}
	unsigned bgColor () override { return col_side (); }
	void row (int y, int kind, int a, int b, int ic, unsigned icc, const char *label, int count, bool selected, int indent = 0, bool bold = false)
	{
		int x = 10 + indent, w = width - 20 - indent;
		if (selected) wk_fill_round (canvas, x - 4, y, w + 8, 30, 6, C_ACCENT);
		else if (hot == hits.n) wk_fill_round (canvas, x - 4, y, w + 8, 30, 6, wk_mix (col_side (), C_TEXT, 20));
		unsigned tc = selected ? C_SEL_TEXT : C_TEXT;
		if (ic >= 0) icon (canvas, ic, x + 4, y + 6, 18, selected ? C_SEL_TEXT : icc);
		char cnt[16] = ""; if (count > 0) snprintf (cnt, sizeof cnt, "%d", count > 999 ? 999 : count);
		int cw = cnt[0] ? tw (cnt, F_SMALL, 1) + 14 : 0;
		text_v (canvas, x + 30, y, 30, label, tc, F_UI, bold || selected ? 1 : 0, w - 34 - cw - 6);
		if (cnt[0])
		{
			wk_fill_round (canvas, x + w - cw - 2, y + 7, cw, 16, 8, selected ? 0xFFFFFF : wk_mix (col_side (), C_ACCENT, 200));
			text_c (canvas, x + w - cw - 2, y + 7, cw, 16, cnt, selected ? C_ACCENT : 0xFFFFFF, F_SMALL, 1);
		}
		hits.add (0, y, width, 30, kind, a, b);
	}
	void onDraw () override
	{
		canvas.clear (col_side ());
		hits.clear ();
		canvas.fillRect (width - 1, 0, 1, height, wk_mix (col_side (), C_TEXT, 40));
		int bottomH = 108;
		int y = 10 - sy;
		const Selection &s = g_m.sel;
		bool cont = g_showContacts;
		row (y, S_UNIFIED, 0, 0, I_UNIFIED, 0x3C8DA8, "All inboxes", g_m.unread_inboxes (), !cont && s.kind == SEL_UNIFIED, 0, true); y += 32;
		row (y, S_STARRED, 0, 0, I_STAR, 0xF2A600, "Starred", 0, !cont && s.kind == SEL_STARRED); y += 40;
		for (int a = 0; a < g_m.accts.n; a++)
		{
			Account &A = g_m.accts.a[a];
			// the account: its mark, its label, its address
			int x = 10;
			icon (canvas, g_open[a] ? I_CHEV_D : I_CHEV_R, x - 4, y + 9, 14, wk_mix (col_side (), C_TEXT, 150));
			wk_fill_round (canvas, x + 12, y + 7, 22, 22, 5, 0xFF000000u | A.colour);
			char ini[8]; initials (A.label, ini); ini[(unsigned char) ini[0] >= 0xC0 ? 2 : 1] = 0;
			text_c (canvas, x + 12, y + 7, 22, 22, ini, 0xFFFFFF, F_SMALL, 1);
			text (canvas, x + 42, y + 2, A.label, C_TEXT, F_UI, 1, width - x - 52);
			text (canvas, x + 42, y + 19, A.email, wk_mix (col_side (), C_TEXT, 150), F_SMALL, 0, width - x - 52);
			hits.add (0, y, width, 36, S_ACCOUNT, a);
			y += 40;
			if (!g_open[a]) continue;
			Store &st = *g_m.stores[a];
			for (int f = 0; f < st.folders.n; f++)
			{
				Folder &F = st.folders[f];
				if (F.noselect) continue;
				if (F.special == SP_ALL && A.provider == PV_GMAIL) {}
				int depth = 0; if (F.delim) for (const char *p = F.name; *p; p++) if (*p == F.delim) depth++;
				// ([Gmail]/x: the [Gmail] level not counted)
				if (A.provider == PV_GMAIL && !strncmp (F.name, "[Gmail]", 7)) depth--;
				if (depth < 0) depth = 0; if (depth > 3) depth = 3;
				Buf nm; mutf7_decode (nm, folder_label (F));
				int cnt = F.special == SP_DRAFTS ? F.msgs.n : (F.special == SP_SENT || F.special == SP_TRASH || F.special == SP_ALL) ? 0 : F.unread;
				row (y, S_FOLDER, a, f, folder_icon (F), wk_mix (col_side (), C_TEXT, 170), nm.c (), cnt, !cont && s.kind == SEL_FOLDER && s.acct == a && s.folder == f, 18 + depth * 12);
				y += 31;
			}
			y += 8;
		}
		if (!g_m.accts.n)
		{
			row (y, S_ADD, 0, 0, I_PLUS, C_ACCENT, "Add an account...", 0, false); y += 32;
		}
		contentH = y + sy + bottomH;
		// the bottom: contacts, settings, what is going on
		int by = height - bottomH;
		canvas.fillRect (0, by, width - 1, bottomH, col_side ());
		canvas.fillRect (10, by, width - 20, 1, wk_mix (col_side (), C_TEXT, 40));
		row (by + 6, S_CONTACTS, 0, 0, I_PERSON, wk_mix (col_side (), C_TEXT, 170), "Contacts", 0, cont);
		row (by + 38, S_SETTINGS, 0, 0, I_GEAR, wk_mix (col_side (), C_TEXT, 170), "Accounts and settings", 0, false);
		const char *st = g_m.worker.current[0] ? g_m.worker.current : (g_note[0] && kapi_get_ticks () - g_noteT < 800) ? g_note : g_m.lastError;
		bool isErr = !g_m.worker.current[0] && !(g_note[0] && kapi_get_ticks () - g_noteT < 800) && g_m.lastError[0];
		if (st && st[0])
		{
			if (isErr) icon (canvas, I_WARN, 12, by + 78, 14, 0xC5221F);
			text (canvas, isErr ? 32 : 14, by + 77, st, wk_mix (col_side (), C_TEXT, 150), F_SMALL, 0, width - (isErr ? 40 : 24));
			if (isErr) hits.add (0, by + 72, width, 26, 99);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel) { int mxs = contentH - height; sy -= wheel * 40; if (sy > mxs) sy = mxs; if (sy < 0) sy = 0; invalidate (true); return true; }
		int nh = -1; for (int i = hits.n - 1; i >= 0; i--) { const Hit &h = hits.h[i]; if (mx >= h.x && my >= h.y && mx < h.x + h.w && my < h.y + h.h) { nh = i; break; } }
		if (nh != hot) { hot = nh; invalidate (true); }
		static bool was; bool down = bl && !was; was = bl;
		if (!down) return true;
		const Hit *h = hits.at (mx, my); if (!h) return true;
		switch (h->kind)
		{
		case S_UNIFIED: show_contacts (false); select_view (SEL_UNIFIED); break;
		case S_STARRED: show_contacts (false); select_view (SEL_STARRED); break;
		case S_ACCOUNT: g_open[h->a] = !g_open[h->a]; invalidate (true); break;
		case S_FOLDER: show_contacts (false); select_view (SEL_FOLDER, h->a, h->b); break;
		case S_CONTACTS: show_contacts (!g_showContacts); break;
		case S_SETTINGS: open_settings (); break;
		case S_ADD: open_wizard (); break;
		case 99: wk_messagebox ("Mail", g_m.lastError, MB_OK); g_m.lastError[0] = 0; invalidate (true); break;
		}
		return true;
	}
};

// ---- the middle column ------------------------------------------------------------------------------------------------------
class ListPane : public Widget
{
public:
	HitList hits; int sy, contentH, hot; bool barDrag;
	enum { L_ROW = 1, L_ALL, L_UNREAD, L_STAR, L_MORE };
	static const int HEAD = 54, ROW_H = 78;
	ListPane (int l, int t, int w, int h) : Widget (l, t, w, h), sy (0), contentH (0), hot (-1), barDrag (false) {}
	unsigned bgColor () override { return col_list (); }
	const char *title ()
	{
		static char t[160];
		switch (g_m.sel.kind)
		{
		case SEL_UNIFIED: return "All inboxes";
		case SEL_STARRED: return "Starred";
		case SEL_SEARCH: snprintf (t, sizeof t, "\xE2\x80\x9C%s\xE2\x80\x9D", g_m.search); return t;
		default:
		{
			if (g_m.sel.acct >= g_m.accts.n) return "";
			Store &s = *g_m.stores[g_m.sel.acct];
			if (g_m.sel.folder >= s.folders.n) return "";
			Buf b; mutf7_decode (b, folder_label (s.folders[g_m.sel.folder]));
			scpy (t, b.c (), sizeof t); return t;
		}
		}
	}
	void onDraw () override
	{
		canvas.clear (col_list ());
		hits.clear ();
		int W = width - 10;
		canvas.fillRect (width - 1, 0, 1, height, col_line ());
		View &v = g_m.view;
		// the rows
		static const char *const GROUPS[5] = { "TODAY", "YESTERDAY", "THIS WEEK", "THIS MONTH", "OLDER" };
		int y = HEAD - sy, lastG = -1;
		for (int i = 0; i < v.convs.n; i++)
		{
			const Conv &c = v.convs[i];
			int g = day_group (c.date);
			if (g != lastG) { lastG = g; if (y > HEAD - 30 && y < height) text (canvas, 16, y + 10, GROUPS[g], col_dim (), F_TINY, 1); y += 30; }
			if (y + ROW_H > HEAD && y < height) draw_row (i, y, W);
			hits.add (0, y, W, ROW_H, L_ROW, i);
			y += ROW_H + 2;
		}
		contentH = y + sy + 10;
		if (!v.convs.n)
		{
			const char *t = g_m.accts.n == 0 ? "No account yet." : g_m.search[0] ? "Nothing found." : g_m.unreadOnly ? "Nothing unread." : (g_m.worker.busy ? "Getting the mail..." : "No messages.");
			text_c (canvas, 0, HEAD + 60, W, 24, t, col_dim (), F_MID);
		}
		// the head: the title, All / Unread
		canvas.fillRect (0, 0, width - 1, HEAD, col_list ());
		int segW = 120;
		text_v (canvas, 16, 6, 44, title (), C_FIELD_TEXT, F_H2, 1, W - segW - 30);
		int sx = W - segW - 6, syy = 16;
		wk_fill_round (canvas, sx, syy, segW, 24, 6, C_ACCENT);
		wk_fill_round (canvas, sx + 1, syy + 1, segW - 2, 22, 5, col_list ());
		int half = segW / 2;
		wk_fill_round (canvas, g_m.unreadOnly ? sx + half : sx, syy, half, 24, 6, C_ACCENT);
		text_c (canvas, sx, syy, half, 24, "All", g_m.unreadOnly ? C_FIELD_TEXT : C_SEL_TEXT, F_SMALL, 1);
		text_c (canvas, sx + half, syy, half, 24, "Unread", g_m.unreadOnly ? C_SEL_TEXT : C_FIELD_TEXT, F_SMALL, 1);
		hits.add (sx, syy, half, 24, L_ALL); hits.add (sx + half, syy, half, 24, L_UNREAD);
		// the scroll bar
		int vh = height - HEAD;
		WkThumb t = wk_thumb (contentH - HEAD, vh, sy, vh);
		if (t.show) wk_draw_vscroll (canvas, width - WK_SBW - 1, HEAD, WK_SBW, vh, t, col_list (), barDrag);
	}
	void draw_row (int i, int y, int W)
	{
		const Conv &c = g_m.view.convs[i];
		const Ref &last = g_m.view.refs[c.first + c.n - 1];
		const Msg &m = g_m.msg (last);
		bool sel = i == g_conv && !g_showContacts;
		int x = 6;
		if (sel) wk_fill_round (canvas, x, y, W - 6, ROW_H, 8, col_sel ());
		else if (hot == i) wk_fill_round (canvas, x, y, W - 6, ROW_H, 8, col_hover ());
		// the account's stripe
		unsigned ac = g_m.accts.a[last.acct].colour;
		canvas.fillRect (W - 6, y + 10, 3, ROW_H - 20, 0xFF000000u | ac);
		// the unread dot
		if (c.unread) icon (canvas, I_DOT, x - 1, y + 25, 12, C_ACCENT);
		// who: in a sent folder, to whom
		const Folder &F = g_m.stores[last.acct]->folders[last.folder];
		bool sent = F.special == SP_SENT || F.special == SP_DRAFTS;
		char name[200];
		// a conversation: its people ("Anna, me, Björn")
		const Msg &first = g_m.msg (g_m.view.refs[c.first]);
		who (sent ? m.to : first.from, name, sizeof name);
		if (sent) { char t[220]; snprintf (t, sizeof t, "To: %s", name[0] ? name : "(nobody)"); scpy (name, t, sizeof name); }
		char em[160]; first_email (sent ? m.to : first.from, em, sizeof em);
		avatar (canvas, x + 30, y + 30, 19, name[0] ? (sent ? name + 4 : name) : "?", em);
		int tx = x + 58, tw0 = W - tx - 14;
		char date[40]; fmt_short (c.date, date, sizeof date);
		int dw = tw (date, F_SMALL);
		// the name, the count
		char cnt[12] = ""; if (c.n > 1) snprintf (cnt, sizeof cnt, "%d", c.n);
		int cntW = cnt[0] ? tw (cnt, F_SMALL, 1) + 12 : 0;
		int nameMax = tw0 - dw - 10 - cntW;
		text (canvas, tx, y + 8, name, C_FIELD_TEXT, F_MID, c.unread ? 1 : 0, nameMax);
		if (cnt[0])
		{
			int nw = tw (name, F_MID, c.unread ? 1 : 0); if (nw > nameMax) nw = nameMax;
			wk_fill_round (canvas, tx + nw + 6, y + 10, cntW, 16, 8, col_line ());
			text_c (canvas, tx + nw + 6, y + 10, cntW, 16, cnt, col_dim (), F_SMALL, 1);
		}
		text (canvas, tx + tw0 - dw, y + 10, date, c.unread ? C_ACCENT : col_dim (), F_SMALL, c.unread ? 1 : 0);
		// the subject, the paper clip, the star
		int iconsW = (c.attach ? 18 : 0) + (c.flagged ? 18 : 0);
		text (canvas, tx, y + 30, m.subject && m.subject[0] ? m.subject : "(no subject)", C_FIELD_TEXT, F_UI, c.unread ? 1 : 0, tw0 - iconsW - 4);
		int ix = tx + tw0 - iconsW;
		if (c.attach) { icon (canvas, I_CLIP, ix, y + 30, 16, col_dim ()); ix += 18; }
		if (c.flagged) icon (canvas, I_STAR, ix, y + 29, 16, 0xF2A600);
		// the text's start
		text (canvas, tx, y + 52, m.preview ? m.preview : "", col_dim (), F_SMALL, 0, tw0);
	}
	void scroll_to (int v) { int mx = contentH - height; if (v > mx) v = mx; if (v < 0) v = 0; if (v != sy) { sy = v; invalidate (true); } }
	// the row of a conversation in view
	void reveal (int i)
	{
		int y = 0, lastG = -1;
		for (int k = 0; k <= i && k < g_m.view.convs.n; k++) { int g = day_group (g_m.view.convs[k].date); if (g != lastG) { lastG = g; y += 30; } if (k < i) y += ROW_H + 2; }
		if (y < sy) scroll_to (y - 30);
		else if (y + ROW_H > sy + height - HEAD) scroll_to (y + ROW_H - (height - HEAD) + 6);
	}
	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		if (wheel) { scroll_to (sy - wheel * 60); return true; }
		static bool was, wasR; bool down = bl && !was; was = bl; bool rdown = br && !wasR; wasR = br;
		if (barDrag)
		{
			if (!bl) { barDrag = false; invalidate (true); return true; }
			int vh = height - HEAD; WkThumb t = wk_thumb (contentH - HEAD, vh, sy, vh);
			scroll_to ((int) wk_thumb_pos (my - HEAD, vh, contentH - HEAD, vh, t.h)); return true;
		}
		const Hit *h = hits.at (mx, my);
		int nh = h && h->kind == L_ROW ? h->a : -1;
		if (nh != hot) { hot = nh; invalidate (true); }
		if (down && mx >= width - WK_SBW - 2 && my > HEAD) { barDrag = true; return true; }
		if (!h || (!down && !rdown)) return true;
		switch (h->kind)
		{
		case L_ROW: open_conv (h->a); if (rdown) row_menu (mx, my); break;
		case L_ALL: g_m.unreadOnly = false; select_view (g_m.sel.kind, g_m.sel.acct, g_m.sel.folder); break;
		case L_UNREAD: g_m.unreadOnly = true; select_view (g_m.sel.kind, g_m.sel.acct, g_m.sel.folder); break;
		}
		return true;
	}
	void row_menu (int mx, int my)
	{
		Ref r[60]; int n = conv_refs (r, 60); if (!n) return;
		bool unread = false, starred = false; for (int i = 0; i < n; i++) { if (!(g_m.msg (r[i]).flags & F_SEEN)) unread = true; if (g_m.msg (r[i]).flags & F_FLAGGED) starred = true; }
		PopupMenu pm (left + mx, top + my);
		pm.add ("Reply", 1); pm.add ("Forward", 2); pm.separator ();
		pm.add (unread ? "Mark as read" : "Mark as unread", 3); pm.add (starred ? "Remove the star" : "Star", 4); pm.separator ();
		pm.add ("Archive", 5); pm.add ("Junk", 6); pm.add ("Delete", 7, true, "Del"); pm.separator (); pm.add ("Move to...", 8);
		switch (pm.run ())
		{
		case 1: compose_new (1, &r[n - 1]); break;
		case 2: compose_new (3, &r[n - 1]); break;
		case 3: g_m.set_flag (r, n, F_SEEN, unread); refresh_all (); break;
		case 4: act_star (); break;
		case 5: act_archive (); break;
		case 6: act_junk (); break;
		case 7: act_delete (); break;
		case 8:
		{
			// the account's folders
			Store &s = *g_m.stores[r[0].acct];
			PopupMenu fm (left + mx, top + my);
			int ids[64]; int k = 0;
			for (int f = 0; f < s.folders.n && k < 30; f++) { if (s.folders[f].noselect || f == r[0].folder) continue; Buf b; mutf7_decode (b, folder_label (s.folders[f])); static char labs[30][120]; scpy (labs[k], b.c (), 120); fm.add (labs[k], k + 1); ids[k++] = f; }
			int c = fm.run ();
			if (c > 0 && c <= k) { g_m.move (r, n, -1, ids[c - 1]); g_conv = -1; select_view (g_m.sel.kind, g_m.sel.acct, g_m.sel.folder); }
			break;
		}
		}
	}
	bool onKey (long k) override
	{
		int n = g_m.view.convs.n;
		if (k == KEY_DOWN && g_conv < n - 1) { open_conv (g_conv + 1); reveal (g_conv); return true; }
		if (k == KEY_UP && g_conv > 0) { open_conv (g_conv - 1); reveal (g_conv); return true; }
		if (k == KEY_DEL) { act_delete (); return true; }
		return false;
	}
};

// ---- no account yet ---------------------------------------------------------------------------------------------------------------
class Welcome : public Widget
{
public:
	Welcome (int l, int t, int w, int h) : Widget (l, t, w, h) { hidden = true; }
	unsigned bgColor () override { return C_FIELD; }
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		int cy = height / 2 - 120;
		icon (canvas, I_SEND, width / 2 - 36, cy, 72, C_ACCENT);
		text_c (canvas, 0, cy + 90, width, 34, "Welcome to Mail", C_FIELD_TEXT, F_H1, 1);
		text_c (canvas, 0, cy + 130, width, 22, "Gmail, Outlook.com, iCloud, Yahoo, any IMAP or POP3 account.", col_dim (), F_MID);
		const char *l = "Add an account"; int w = tw (l, F_MID, 1) + 48;
		wk_fill_round (canvas, (width - w) / 2, cy + 176, w, 42, 8, C_ACCENT);
		text_c (canvas, (width - w) / 2, cy + 176, w, 42, l, C_SEL_TEXT, F_MID, 1);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		static bool was; bool down = bl && !was; was = bl;
		int cy = height / 2 - 120; int w = tw ("Add an account", F_MID, 1) + 48;
		if (down && my >= cy + 176 && my < cy + 218 && mx >= (width - w) / 2 && mx < (width + w) / 2) open_wizard ();
		return true;
	}
};

// ---- what the parts call --------------------------------------------------------------------------------------------------------
static void layout_parts ()
{
	if (!g_root) return;
	int W = g_root->width, H = g_root->height;
	int side = W < 860 ? 180 : SIDE_W, list = W < 860 ? 260 : (W > 1300 ? 380 : LIST_W);
	g_tb->resizeTo (W, TB_H); g_tb->place ();
	g_side->top = TB_H; g_side->resizeTo (side, H - TB_H);
	g_list->left = side; g_list->top = TB_H; g_list->resizeTo (list, H - TB_H);
	g_read->left = side + list; g_read->top = TB_H; g_read->resizeTo (W - side - list, H - TB_H); g_read->place ();
	g_compose->left = side; g_compose->top = TB_H; g_compose->resizeTo (W - side, H - TB_H); g_compose->place ();
	g_contacts->left = side; g_contacts->top = TB_H; g_contacts->resizeTo (W - side, H - TB_H);
	g_welcome->left = side; g_welcome->top = TB_H; g_welcome->resizeTo (W - side, H - TB_H);
	bool composing = !g_compose->hidden;
	g_welcome->hidden = g_m.accts.n > 0 || composing || g_showContacts;
	g_list->hidden = g_read->hidden = composing || g_showContacts || !g_welcome->hidden;
	g_contacts->hidden = !g_showContacts || composing;
	g_read->relayout ();
}
static void refresh_all ()
{
	if (!g_root) return;
	g_tb->invalidate (true); g_side->invalidate (true); g_list->invalidate (true); g_read->invalidate (true);
	if (!g_contacts->hidden) g_contacts->invalidate (true);
}
static void select_view (int kind, int acct, int folder)
{
	bool same = g_m.sel.kind == kind && g_m.sel.acct == acct && g_m.sel.folder == folder;
	g_m.sel.kind = kind; g_m.sel.acct = acct; g_m.sel.folder = folder;
	if (kind == SEL_FOLDER && acct < g_m.accts.n)
	{
		Folder &f = g_m.stores[acct]->folders[folder];
		bool first = !f.opened;
		f.opened = true;
		g_m.stores[acct]->load_index (f);
		if (first || !same) g_m.sync (acct, false, folder);
	}
	g_m.build ();
	if (!same) { g_conv = -1; g_read->clear (); g_list->sy = 0; }
	if (g_compose->hidden) layout_parts ();
	refresh_all ();
}
// the view rebuilt (a sync came): the conversation shown kept if it is still there
static void rebuild_keep ()
{
	Ref keep; bool had = false;
	if (g_conv >= 0 && g_conv < g_m.view.convs.n) { const Conv &c = g_m.view.convs[g_conv]; keep = g_m.view.refs[c.first + c.n - 1]; had = true; }
	long keepUid = had ? g_m.msg (keep).uid : 0; int keepA = keep.acct, keepF = keep.folder;
	g_m.build ();
	int found = -1;
	if (had)
		for (int i = 0; i < g_m.view.convs.n && found < 0; i++)
			for (int k = 0; k < g_m.view.convs[i].n; k++) { const Ref &r = g_m.view.refs[g_m.view.convs[i].first + k]; if (r.acct == keepA && r.folder == keepF && g_m.msg (r).uid == keepUid) { found = i; break; } }
	if (found != g_conv)
	{
		g_conv = found;
		if (found < 0) g_read->clear (); else { Ref r[60]; int n = conv_refs (r, 60); g_read->load (r, n); }
	}
	else if (found >= 0)
	{	// the refs may have moved (a message deleted above): its Shown kept, its ref renewed
		Ref r[60]; int n = conv_refs (r, 60);
		if (n != g_read->nsh) g_read->load (r, n);
		else for (int i = 0; i < n; i++) g_read->sh[i].ref = r[i];
	}
}
static void open_conv (int k)
{
	if (k < 0 || k >= g_m.view.convs.n) return;
	if (!g_compose->hidden) return;
	g_conv = k;
	Ref r[60]; int n = conv_refs (r, 60);
	g_read->load (r, n);
	// read: its unread messages marked
	Ref un[60]; int nu = 0; for (int i = 0; i < n; i++) if (!(g_m.msg (r[i]).flags & F_SEEN)) un[nu++] = r[i];
	if (nu) { g_m.set_flag (un, nu, F_SEEN, true); g_m.view.convs[k].unread = false; }
	refresh_all ();
}
// the messages an action on the conversation moves: not the replies shown from Sent (unless Sent is the list)
static int action_refs (Ref *out, int max)
{
	Ref r[60]; int n = conv_refs (r, 60), k = 0;
	bool inSent = g_m.sel.kind == SEL_FOLDER && g_m.stores[g_m.sel.acct]->folders[g_m.sel.folder].special == SP_SENT;
	for (int i = 0; i < n && k < max; i++) if (inSent || g_m.stores[r[i].acct]->folders[r[i].folder].special != SP_SENT) out[k++] = r[i];
	return k;
}
static void after_change ()
{
	g_conv = -1; g_read->clear ();
	g_m.build ();
	refresh_all ();
}
static void act_archive ()
{
	Ref r[60]; int n = action_refs (r, 60); if (!n) return;
	int keep = g_conv;
	if (!g_m.move (r, n, SP_ARCHIVE)) { status_note ("This account has no Archive folder."); return; }
	after_change (); status_note ("Archived.");
	if (keep < g_m.view.convs.n) open_conv (keep);
}
static void act_delete ()
{
	Ref r[60]; int n = action_refs (r, 60); if (!n) return;
	int keep = g_conv;
	g_m.remove (r, n);
	after_change (); status_note ("Deleted.");
	if (keep < g_m.view.convs.n) open_conv (keep);
}
static void act_junk ()
{
	Ref r[60]; int n = action_refs (r, 60); if (!n) return;
	int keep = g_conv;
	if (!g_m.move (r, n, SP_JUNK)) { status_note ("This account has no Junk folder."); return; }
	after_change (); status_note ("Moved to Junk.");
	if (keep < g_m.view.convs.n) open_conv (keep);
}
static void act_star ()
{
	Ref r[60]; int n = conv_refs (r, 60); if (!n) return;
	bool any = false; for (int i = 0; i < n; i++) if (g_m.msg (r[i]).flags & F_FLAGGED) any = true;
	if (any) g_m.set_flag (r, n, F_FLAGGED, false); else g_m.set_flag (&r[n - 1], 1, F_FLAGGED, true);
	g_m.build (); refresh_all ();
}
static void act_unread ()
{
	Ref r[60]; int n = conv_refs (r, 60); if (!n) return;
	g_m.set_flag (&r[n - 1], 1, F_SEEN, false);
	g_m.build (); refresh_all ();
}
static void act_check () { g_m.sync_all (); g_m.newMail = 0; refresh_all (); }
static void show_contacts (bool on)
{
	if (on && !g_compose->hidden) return;
	g_showContacts = on;
	if (on) { g_m.contacts.load (); g_contacts->reload (); }
	layout_parts (); refresh_all ();
}
static void search_changed ()
{
	const char *q = g_tb->search->text;
	if (!strcmp (q, g_m.search)) return;
	static Selection before; static bool searching;
	if (q[0] && !searching) { before = g_m.sel; searching = true; }
	scpy (g_m.search, q, sizeof g_m.search);
	if (q[0]) { show_contacts (false); g_m.sel.kind = SEL_SEARCH; }
	else if (searching) { g_m.sel = before; searching = false; }
	g_conv = -1; g_read->clear (); g_list->sy = 0;
	g_m.build (); refresh_all ();
}

// ---- writing -----------------------------------------------------------------------------------------------------------------------
static int default_account ()
{
	if (g_m.sel.kind == SEL_FOLDER && g_m.sel.acct < g_m.accts.n) return g_m.sel.acct;
	return 0;
}
static void compose_open (const char *title)
{
	g_compose->accounts_changed ();
	scpy (g_compose->title, title, sizeof g_compose->title);
	g_compose->hidden = false; g_showContacts = false;
	layout_parts ();
	g_compose->invalidate (true);
	refresh_all ();
}
// the quoted text of a message: its plain text, else its HTML's text
static void quoted_text (const Ref &r, Buf &out, bool forward)
{
	Store &st = *g_m.stores[r.acct]; Folder &f = st.folders[r.folder]; const Msg &m = f.msgs[r.msg];
	int len; char *raw = st.body (f, m.uid, &len);
	Buf t;
	if (raw)
	{
		Mime M; M.parse (raw, len);
		int p = M.body_part (false);
		if (p >= 0)
		{
			if (ieq (M.parts[p].sub, "html")) { Buf h; M.text (M.parts[p], h); html::Html x; x.parse (h.c (), h.n); g_host->pics = 0; x.layout (*g_host, 600); x.plain_text (t); }
			else M.text (M.parts[p], t);
		}
		free (raw);
	}
	else t.add (m.preview ? m.preview : "");
	char date[80]; fmt_long (m.date, date, sizeof date);
	char from[300]; who (m.from, from, sizeof from);
	if (forward)
	{
		out.add ("\n\n---------- Forwarded message ----------\n");
		out.addf ("From: %s\nDate: %s\nSubject: %s\nTo: %s\n\n", m.from ? m.from : "", date, m.subject ? m.subject : "", m.to ? m.to : "");
		out.add (t.c ());
		return;
	}
	out.addf ("\n\nOn %s, %s wrote:\n", date, from);
	const char *p = t.c ();
	while (*p)
	{
		const char *e = strchr (p, '\n'); int n = e ? (int) (e - p) : (int) strlen (p);
		out.add (n && *p == '>' ? ">" : "> "); out.add (p, n); out.addc ('\n');
		if (!e) break; p = e + 1;
	}
}
static void add_signature (Buf &b, int acct)
{
	if (acct < g_m.accts.n && g_m.accts.a[acct].signature[0]) { b.add ("\n\n-- \n"); b.add (g_m.accts.a[acct].signature); }
}
static void compose_new (int mode, const Ref *about)
{
	if (g_emlMode) { wk_messagebox ("Mail", "This message is a file. To answer it, open Mail with your accounts.", MB_OK); return; }
	if (!g_m.accts.n) { open_wizard (); return; }
	if (!g_compose->hidden && !g_compose->empty ())
	{
		if (wk_messagebox ("Mail", "Leave the message being written?", MB_YESNO) != 1) return;
	}
	g_compose->reset ();
	g_compose->mode = mode;
	int acct = about ? about->acct : default_account ();
	g_compose->accounts_changed (); g_compose->from->sel = acct; g_compose->from->invalidate (true);
	Buf body;
	if (mode == 0 || !about) { add_signature (body, acct); g_compose->body->setContent (body.c ()); g_compose->body->caret = 0; compose_open ("New message"); g_compose->to->setFocus (); return; }
	g_compose->about = *about; g_compose->haveAbout = true;
	const Msg &m = g_m.msg (*about);
	const char *subj = m.subject ? m.subject : "";
	char s[400];
	if (mode == 3) snprintf (s, sizeof s, istarts (subj, "Fwd:") ? "%s" : "Fwd: %s", subj);
	else snprintf (s, sizeof s, istarts (subj, "Re:") ? "%s" : "Re: %s", subj);
	g_compose->subject->setText (s);
	if (mode == 1 || mode == 2)
	{
		Buf to; to.add (m.replyTo && m.replyTo[0] ? m.replyTo : m.from ? m.from : "");
		const char *me = g_m.accts.a[acct].email;
		if (mode == 2)
		{
			Addr a[32]; Buf cc;
			const char *lists[2] = { m.to, m.cc };
			for (int l = 0; l < 2; l++) { int n = parse_addrs (lists[l] ? lists[l] : "", a, 32); for (int i = 0; i < n; i++) { if (ieq (a[i].email, me) || ifind (to.c (), a[i].email)) continue; if (cc.n) cc.add (", "); if (a[i].name[0]) cc.addf ("\"%s\" <%s>", a[i].name, a[i].email); else cc.add (a[i].email); } }
			if (cc.n) { g_compose->ccOn = true; g_compose->cc->setText (cc.c ()); }
		}
		// a reply to one's own sent message: to its recipients
		if (ifind (to.c (), me) && m.to) { to.clear (); to.add (m.to); }
		Buf t2; t2.add (to.c ()); if (t2.n) t2.add (", ");
		g_compose->to->setText (t2.c ());
		scpy (g_compose->inReplyTo, m.msgid ? m.msgid : "", sizeof g_compose->inReplyTo);
		Buf refs; if (m.refs && *m.refs) { refs.add (m.refs); refs.addc (' '); } if (m.msgid) refs.add (m.msgid);
		scpy (g_compose->references, refs.c (), sizeof g_compose->references);
	}
	add_signature (body, acct);
	quoted_text (*about, body, mode == 3);
	if (mode == 3)
	{	// the attachments carried over
		Store &st = *g_m.stores[about->acct]; Folder &f = st.folders[about->folder];
		int len; char *raw = st.body (f, m.uid, &len);
		if (raw)
		{
			Mime M; M.parse (raw, len); int at[16]; int n = M.attachments (at, 16);
			for (int i = 0; i < n && g_compose->natt < 16; i++)
			{
				Attach &A = g_compose->att[g_compose->natt++];
				scpy (A.name, M.parts[at[i]].name[0] ? M.parts[at[i]].name : "attachment", sizeof A.name);
				snprintf (A.type, sizeof A.type, "%s/%s", M.parts[at[i]].type, M.parts[at[i]].sub);
				Buf b; M.decoded (M.parts[at[i]], b); A.n = b.n; A.data = b.take ();
			}
			free (raw);
		}
	}
	g_compose->body->setContent (body.c ());
	g_compose->body->caret = 0;
	compose_open (mode == 3 ? "Forward" : mode == 2 ? "Reply all" : "Reply");
	if (mode == 3) g_compose->to->setFocus (); else g_compose->body->setFocus ();
}
static void compose_to (const char *name, const char *email)
{
	compose_new (0);
	if (g_compose->hidden) return;
	char t[400]; if (name && *name) snprintf (t, sizeof t, strpbrk (name, ",;<>@\"") ? "\"%s\" <%s>, " : "%s <%s>, ", name, email); else snprintf (t, sizeof t, "%s, ", email);
	g_compose->to->setText (t);
	g_compose->subject->setFocus ();
}
static void compose_close () { g_compose->reset (); g_compose->hidden = true; layout_parts (); refresh_all (); }
static void compose_discard ()
{
	if (!g_compose->empty () && wk_messagebox ("Mail", "Throw away this message?", MB_YESNO) != 1) return;
	compose_close ();
}
static void compose_ccbcc () { g_compose->ccOn = true; g_compose->place (); g_compose->invalidate (true); g_compose->cc->setFocus (); }
static bool compose_attach_path (const char *path);
static void compose_attach ()
{
	char path[300];
	if (!wk_file_open (path, sizeof path, "SD:/Documents")) return;
	compose_attach_path (path);
}
static bool compose_attach_path (const char *path)
{
	if (g_compose->natt >= 16) { wk_messagebox ("Mail", "16 attachments at most.", MB_OK); return false; }
	int len; char *b = file_read (path, &len);
	if (!b) { wk_messagebox ("Mail", "The file could not be read.", MB_OK); return false; }
	if (len > 20 * 1024 * 1024) { free (b); wk_messagebox ("Mail", "This file is too big to send by mail (20 MB at most).", MB_OK); return false; }
	Attach &A = g_compose->att[g_compose->natt++];
	const char *nm = strrchr (path, '/'); nm = nm ? nm + 1 : path;
	scpy (A.name, nm, sizeof A.name); scpy (A.type, mime_type_of (nm), sizeof A.type);
	A.data = b; A.n = len;
	g_compose->place (); g_compose->invalidate (true);
	return true;
}
// "mail --attach <list>": a new message with the files the list names (one path a line: Photos' Send by Mail)
static void compose_with_files (const char *list)
{
	int len; char *b = file_read (list, &len);
	if (!b) return;
	compose_new (0);
	if (!g_compose->hidden)
		for (char *l = b; l && *l; )
		{
			char *e = strchr (l, '\n'); if (e) *e = 0;
			int k = (int) strlen (l); if (k && l[k - 1] == '\r') l[--k] = 0;
			if (l[0]) compose_attach_path (l);
			l = e ? e + 1 : 0;
		}
	free (b);
}
// the message made: its bytes, its recipients
static bool compose_build (Buf &raw, Job *j, bool draft)
{
	ComposePane &c = *g_compose;
	int a = c.from->sel; if (a < 0 || a >= g_m.accts.n) a = 0;
	Account &A = g_m.accts.a[a];
	char from[400]; snprintf (from, sizeof from, A.name[0] ? "\"%s\" <%s>" : "%s%s", A.name[0] ? A.name : "", A.email);
	// the recipients: To, Cc, Bcc (Bcc not in the headers)
	char em[64][160]; int n = 0;
	const char *lists[3] = { c.to->text, c.cc->text, c.bcc->text };
	for (int l = 0; l < 3; l++) { char t[64][160]; int k = addr_emails (lists[l], t, 64 - n); for (int i = 0; i < k; i++) if (strchr (t[i], '@')) scpy (em[n++], t[i], 160); }
	if (!n && !draft) { wk_messagebox ("Mail", "Whom to? Type an address in To.", MB_OK); return false; }
	for (int i = 0; i < n; i++) { const char *at = strchr (em[i], '@'); if (!draft && (!at || !strchr (at, '.') || strchr (em[i], ' '))) { char q[300]; snprintf (q, sizeof q, "\"%s\" does not look like an e-mail address.", em[i]); wk_messagebox ("Mail", q, MB_OK); return false; } }
	if (!draft && !c.subject->text[0] && wk_messagebox ("Mail", "Send it without a subject?", MB_YESNO) != 1) return false;
	Buf html; text_to_html (c.body->content (), html);
	Attachment at[16];
	for (int i = 0; i < c.natt; i++) { at[i].name = c.att[i].name; at[i].type = c.att[i].type; at[i].data = c.att[i].data; at[i].n = c.att[i].n; at[i].cid = 0; }
	Outgoing o; memset (&o, 0, sizeof o);
	o.from = from; o.to = c.to->text; o.cc = c.cc->text; o.subject = c.subject->text;
	o.inReplyTo = c.inReplyTo; o.references = c.references;
	o.text = c.body->content (); o.html = html.c (); o.att = at; o.natt = c.natt;
	o.date = now_utc (); o.tzMin = tz_minutes ();
	build (raw, o);
	j->kind = draft ? J_DRAFT : J_SEND;
	j->acct = A; scpy (j->root, g_m.stores[a]->root, sizeof j->root);
	for (int i = 0; i < n; i++) scpy (j->rcpt[i], em[i], 160);
	j->nrcpt = n;
	Folder *dest = g_m.stores[a]->special (draft ? SP_DRAFTS : SP_SENT);
	if (dest) scpy (j->dest, dest->name, sizeof j->dest);
	j->appendSent = A.provider != PV_GMAIL && A.provider != PV_OUTLOOK;
	j->raw = (char *) malloc (raw.n + 1); memcpy (j->raw, raw.c (), raw.n); j->rawLen = raw.n;
	return true;
}
// a POP3 account's own Sent / Drafts: the message kept on the card
static void keep_local (int a, int special, const Buf &raw)
{
	Store &s = *g_m.stores[a]; Folder *f = s.special (special); if (!f) return;
	s.load_index (*f);
	Envelope e; envelope_of (raw.c (), raw.n, e);
	e.uid = f->uidnext > 0 ? f->uidnext : s.max_uid (*f) + 1; f->uidnext = e.uid + 1; e.flags = F_SEEN | (special == SP_DRAFTS ? F_DRAFT : 0);
	Msg m; msg_from_env (m, e);
	char pv[240]; preview_from_raw (raw.c (), raw.n, pv, sizeof pv); free (m.preview); m.preview = sdup (pv);
	s.insert (*f, m); s.keep_body (*f, e.uid, raw.c (), raw.n);
	Store::count (*f); s.save_index (*f); s.save_folders ();
}
static void compose_send ()
{
	if (g_compose->hidden) return;
	Buf raw; Job *j = new Job; memset (j, 0, sizeof *j);
	if (!compose_build (raw, j, false)) { job_free (j); return; }
	int a = g_compose->from->sel; if (a < 0 || a >= g_m.accts.n) a = 0;
	// the people written to: remembered for completion
	Addr ad[64]; const char *lists[3] = { g_compose->to->text, g_compose->cc->text, g_compose->bcc->text };
	for (int l = 0; l < 3; l++) { int k = parse_addrs (lists[l], ad, 64); for (int i = 0; i < k; i++) if (strchr (ad[i].email, '@')) g_m.contacts.wrote_to (ad[i].name, ad[i].email, now_utc ()); }
	// the message answered: \Answered / $Forwarded
	if (g_compose->haveAbout && g_compose->mode >= 1 && g_compose->mode <= 3)
	{
		Ref r = g_compose->about;
		if (r.acct < g_m.accts.n && r.folder < g_m.stores[r.acct]->folders.n && r.msg < g_m.stores[r.acct]->folders[r.folder].msgs.n)
			g_m.set_flag (&r, 1, g_compose->mode == 3 ? F_FORWARDED : F_ANSWERED, true);
	}
	if (g_m.accts.a[a].kind == K_POP3) keep_local (a, SP_SENT, raw);
	g_m.worker.push (j);
	compose_close ();
	status_note ("Sending...");
}
static void compose_draft ()
{
	if (g_compose->hidden) return;
	Buf raw; Job *j = new Job; memset (j, 0, sizeof *j);
	if (!compose_build (raw, j, true)) { job_free (j); return; }
	int a = g_compose->from->sel; if (a < 0 || a >= g_m.accts.n) a = 0;
	if (g_m.accts.a[a].kind == K_POP3) { keep_local (a, SP_DRAFTS, raw); job_free (j); }
	else g_m.worker.push (j);
	status_note ("The draft is kept in Drafts.");
}
static void quick_send ()
{
	if (!g_read->nsh || !g_read->quick->text[0]) return;
	Ref r = g_read->sh[g_read->nsh - 1].ref;
	char t[600]; scpy (t, g_read->quick->text, sizeof t);
	compose_new (1, &r);
	if (g_compose->hidden) return;
	// the quick reply's text above the quote, sent at once
	Buf b; b.add (t); b.add (g_compose->body->content ());
	g_compose->body->setContent (b.c ());
	g_read->quick->setText ("");
	compose_send ();
}

// ---- the dialogs ---------------------------------------------------------------------------------------------------------------
static void open_wizard ()
{
	Wizard *w = new Wizard; g_wizard = w;
	w->left = (g_root->width - w->width) / 2; w->top = (g_root->height - w->height) / 2;
	int r = w->run ();
	g_wizard = 0; delete w;
	if (r == 1) { g_compose->accounts_changed (); select_view (SEL_UNIFIED); }
	layout_parts (); refresh_all ();
}
static void open_settings ()
{
	for (;;)
	{
		SettingsBox *s = new SettingsBox; g_settings = s;
		s->left = (g_root->width - s->width) / 2; s->top = (g_root->height - s->height) / 2;
		int r = s->run ();
		g_settings = 0; delete s;
		if (r == 2) { open_wizard (); continue; }
		break;
	}
	g_compose->accounts_changed ();
	select_view (SEL_UNIFIED);
}

// ---- the worker's results ------------------------------------------------------------------------------------------------------
static void on_result (void *ctx, long)
{
	Result *r = (Result *) ctx;
	if (g_wizard && (r->kind == J_CHECK || r->kind == J_OAUTH_START || r->kind == J_OAUTH_POLL)) { g_wizard->answer (r); job_free (r->job); result_free (r); return; }
	if (r->kind == J_PICTURE)
	{
		Remote *rm = remote_find (r->job->uids);
		if (rm)
		{
			ImgFrames im; memset (&im, 0, sizeof im);
			if (r->ok && r->data && img_load_mem (r->data, (unsigned) r->dataLen, &im) && im.n >= 1) { rm->px = im.px[0]; rm->w = im.w; rm->h = im.h; im.px[0] = 0; img_free (&im); }
			else rm->failed = true;
		}
		g_read->relayout ();
		job_free (r->job); result_free (r);
		return;
	}
	int changed = g_m.apply (r);
	if (r->kind == J_SEND)
	{
		if (r->ok) { status_note (r->err[0] ? r->err : "Sent."); int a = g_m.acct_index (r->acctId); if (a >= 0 && g_m.accts.a[a].kind == K_IMAP) g_m.sync (a, false, g_m.stores[a]->special (SP_SENT) ? g_m.stores[a]->index_of (g_m.stores[a]->special (SP_SENT)) : -1); }
		else wk_messagebox ("Mail: not sent", r->err[0] ? r->err : "The message could not be sent.", MB_OK);
	}
	if (r->kind == J_BODY && r->ok) g_read->body_came (r->acctId, r->folder, r->uid);
	if (changed & 1) rebuild_keep ();
	job_free (r->job); result_free (r);
	// new mail: told
	if (g_m.newMail > 0)
	{
		char t[200]; snprintf (t, sizeof t, g_m.newMail == 1 ? "A new message" : "%d new messages", g_m.newMail);
		notify_action ("Mail", t, "mail");
		g_m.newMail = 0;
	}
	refresh_all ();
}


// ---- the window --------------------------------------------------------------------------------------------------------------------
class MailRoot : public Root
{
public:
	MailRoot (int w, int h) : Root (w, h, "Mail") {}
	void onResized () override { layout_parts (); refresh_all (); }
	void onTick () override
	{
		g_tick = kapi_get_ticks ();
		g_m.tick ();
		g_compose->tick ();
		g_contacts->tick ();
		static char lastQ[200]; static unsigned qT;
		if (strcmp (lastQ, g_tb->search->text)) { scpy (lastQ, g_tb->search->text, sizeof lastQ); qT = g_tick; }
		if (qT && g_tick - qT > 30) { qT = 0; search_changed (); }
		// the worker's line, the spinner
		static int lastBusy; static char lastCur[120];
		int b = g_m.worker.busy ? 1 : 0;
		if (b != lastBusy || strcmp (lastCur, g_m.worker.current)) { lastBusy = b; scpy (lastCur, g_m.worker.current, sizeof lastCur); g_side->invalidate (true); g_tb->invalidate (true); if (!g_m.view.convs.n) g_list->invalidate (true); }
		static unsigned noteT; if (g_note[0] && g_tick - g_noteT > 800 && noteT != g_noteT) { noteT = g_noteT; g_side->invalidate (true); }
	}
};

static void m_new () { compose_new (0); }
static void m_reply () { Ref r[60]; int n = conv_refs (r, 60); if (n) compose_new (1, &r[n - 1]); }
static void m_replyall () { Ref r[60]; int n = conv_refs (r, 60); if (n) compose_new (2, &r[n - 1]); }
static void m_forward () { Ref r[60]; int n = conv_refs (r, 60); if (n) compose_new (3, &r[n - 1]); }
static void m_send () { compose_send (); }
static void m_find () { g_tb->search->setFocus (); }
static void m_conv () { g_m.grouped = !g_m.grouped; g_conv = -1; g_read->clear (); g_m.build (); refresh_all (); }
static void m_unread () { g_m.unreadOnly = !g_m.unreadOnly; select_view (g_m.sel.kind, g_m.sel.acct, g_m.sel.folder); }
static void m_contacts () { show_contacts (!g_showContacts); }
static void m_save_eml ()
{
	Ref r[60]; int n = conv_refs (r, 60); if (!n) return;
	Store &st = *g_m.stores[r[n - 1].acct]; Folder &f = st.folders[r[n - 1].folder]; const Msg &m = f.msgs[r[n - 1].msg];
	int len; char *raw = st.body (f, m.uid, &len);
	if (!raw) { wk_messagebox ("Mail", "Open the message first (it is fetched then).", MB_OK); return; }
	char nm[120]; ReadPane::safe_name (m.subject && m.subject[0] ? m.subject : "message", nm, 100); strcat (nm, ".eml");
	char out[300];
	if (wk_file_save (out, sizeof out, "SD:/Documents", nm) && kapi_save_file (out, raw, (unsigned) len) < 0) wk_messagebox ("Mail", "The file could not be written.", MB_OK);
	free (raw);
}

} // namespace mailapp

// "mail SD:/x.eml": the file shown as a message of a pseudo account (read only)
static bool open_eml (const char *path)
{
	int len; char *raw = file_read (path, &len);
	if (!raw) return false;
	// a store of its own, in RAM (nothing written: an account "eml" not saved)
	Account &a = g_m.accts.a[0]; account_defaults (a);
	scpy (a.id, "eml", sizeof a.id); scpy (a.label, "File", sizeof a.label); a.kind = K_POP3; a.checkMinutes = 0;
	g_m.accts.n = 1;
	Store *s = new Store; s->acct = &a; scpy (s->root, "RAM:/mail-eml", sizeof s->root);
	g_m.stores[0] = s;
	Folder &f = s->add_folder ("INBOX"); const char *bn = strrchr (path, '/'); scpy (f.show, bn ? bn + 1 : path, sizeof f.show); f.special = SP_INBOX; f.loaded = true;
	Envelope e; envelope_of (raw, len, e); e.uid = 1; e.flags = F_SEEN;
	Msg m; msg_from_env (m, e); s->insert (f, m);
	mkdirs ("RAM:/mail-eml"); s->keep_body (f, 1, raw, len);
	free (raw);
	g_emlMode = true;
	return true;
}

int main (void)
{
	ft_wtk_install ("DejaVu Sans", 13);
	faces_open ();
	static html::FtHost host; g_host = &host;

	char args[400]; int na = kapi_get_args (args, sizeof args); args[na > 0 && na < 400 ? na : 0] = 0;
	char *a = args; while (*a == ' ') a++;
	int al = (int) strlen (a); while (al && a[al - 1] == ' ') a[--al] = 0;
	char attachList[300] = "";
	if (!strncmp (a, "--attach ", 9)) { a += 9; while (*a == ' ') a++; scpy (attachList, a, sizeof attachList); a[0] = 0; }
	if (a[0] == '"') { a++; char *q = strchr (a, '"'); if (q) *q = 0; }
	if (a[0] && open_eml (a)) {}
	else g_m.open ();

	MailRoot root (1000, 640);
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	root.setResizable (true);
	root.setBg (C_BG);
	g_tb = new ToolBar (0, 0, 1000, TB_H); root.addChild (g_tb);
	g_side = new Sidebar (0, TB_H, SIDE_W, 640 - TB_H); root.addChild (g_side);
	g_list = new ListPane (SIDE_W, TB_H, LIST_W, 640 - TB_H); root.addChild (g_list);
	g_read = new ReadPane (SIDE_W + LIST_W, TB_H, 1000 - SIDE_W - LIST_W, 640 - TB_H); root.addChild (g_read);
	g_compose = new ComposePane (SIDE_W, TB_H, 1000 - SIDE_W, 640 - TB_H); root.addChild (g_compose);
	g_contacts = new ContactsPane (SIDE_W, TB_H, 1000 - SIDE_W, 640 - TB_H); root.addChild (g_contacts);
	g_welcome = new Welcome (SIDE_W, TB_H, 1000 - SIDE_W, 640 - TB_H); root.addChild (g_welcome);
	g_m.worker.start (on_result);

	static Menu menu;
	menu.menu ("File");
	menu.item ("New Message", "^N", WK_CTRL ('N'), m_new);
	menu.item ("Check for New Mail", "F5", KEY_F1 + 4, act_check);
	menu.separator ();
	menu.item ("Add an Account...", "", 0, open_wizard);
	menu.item ("Accounts and Settings...", "", 0, open_settings);
	menu.item ("Contacts", "", 0, m_contacts);
	menu.separator ();
	menu.item ("Save the Message as .eml...", "", 0, m_save_eml);
	menu.menu ("Edit");
	menu.item ("Find...", "^F", WK_CTRL ('F'), m_find);
	menu.menu ("View");
	menu.item ("Conversations (grouped)", "", 0, m_conv);
	menu.item ("Unread Only", "", 0, m_unread);
	menu.menu ("Message");
	menu.item ("Reply", "^R", WK_CTRL ('R'), m_reply);
	menu.item ("Reply All", "", 0, m_replyall);
	menu.item ("Forward", "^L", WK_CTRL ('L'), m_forward);
	menu.item ("Send", "Ctrl+Enter", 0, m_send);
	menu.separator ();
	menu.item ("Archive", "", 0, act_archive);
	menu.item ("Delete", "Del", 0, act_delete);
	menu.item ("Junk", "", 0, act_junk);
	menu.separator ();
	menu.item ("Star / Unstar", "^S", WK_CTRL ('S'), act_star);
	menu.item ("Mark as Unread", "^U", WK_CTRL ('U'), act_unread);
	menu.publish ();

	g_compose->accounts_changed ();
	g_m.build ();
	layout_parts ();
	refresh_all ();
	root.fitWorkArea ();
	if (g_emlMode) { g_m.sel.kind = SEL_FOLDER; g_m.sel.acct = 0; g_m.sel.folder = 0; g_m.build (); open_conv (0); }
	else
	{
		if (g_m.view.convs.n) open_conv (0);
		if (g_m.accts.n) g_m.sync_all ();
		if (attachList[0]) compose_with_files (attachList);
	}

	root.run ();

	g_m.worker.stop ();
	return 0;
}
