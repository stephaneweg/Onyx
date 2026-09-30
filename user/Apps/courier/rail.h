//
// rail.h -- Courier's sidebar: a rail of three places (Collections, Environments, History) and the
// list of the place chosen -- the collections as a tree (folders, requests with their method), the
// environments (the globals first, the one chosen ticked), the history by day -- a filter above it,
// each row's menu (a right click, or its "..." button).
//
#ifndef _courier_rail_h
#define _courier_rail_h

#include "views.h"

namespace cr {

enum { SB_COLL, SB_ENV, SB_HIST };
enum { R_COLL, R_FOLDER, R_REQ, R_GLOBALS, R_ENV, R_DAY, R_HIST, R_EMPTY };

struct SideRow { int kind; void *ptr; int idx; int depth; };

// the short name of a method (a badge)
static inline const char *method_short (const char *m)
{
	if (s_eqi (m, "DELETE")) return "DEL";
	if (s_eqi (m, "OPTIONS")) return "OPT";
	if (s_eqi (m, "PATCH")) return "PTCH";
	return m;
}
static inline void draw_method_badge (Canvas &cv, int x, int y, int h, const char *m, int w)
{
	char b[8]; s_copy (b, method_short (m), sizeof b);
	for (int i = 0; b[i]; i++) if (b[i] >= 'a' && b[i] <= 'z') b[i] -= 32;
	unsigned c = method_color (m);
	int tw_ = wk_tw (b, 2);
	text_v (cv, x + w - tw_, y, h, b, c, 2);
}
// the rail's icons
static inline void icon_collections (Canvas &cv, int cx, int cy, unsigned c)
{
	wk_rline (cv, cx - 10, cy - 6, 20, 15, 3, c);
	wk_rbox (cv, cx - 10, cy - 9, 9, 4, 2, c, c, 255, WK_TL | WK_TR);
	cv.fillRect (cx - 7, cy - 1, 14, 2, c);
}
static inline void icon_env (Canvas &cv, int cx, int cy, unsigned c)
{
	wk_rline (cv, cx - 11, cy - 6, 22, 13, 6, c);
	wk_rline (cv, cx - 4, cy - 4, 9, 9, 4, c);
	wk_glyph (cv, WKG_DOT, cx, cy, 4, c);
}

class Sidebar : public Widget
{
public:
	enum { RAIL_W = 78, HEAD_H = 48, ROW_H = 30 };
	int mode;
	Btn *add, *clear;
	LineEdit *filter;
	Vec<SideRow> rows;
	int top;

	Sidebar () : Widget (0, 0, 300, 400), mode (SB_COLL), add (0), clear (0), filter (0), top (0), m_hot (-1), m_hotRail (-1), m_hotMore (false), m_lastClick (0), m_lastRow (-1)
	{
		add = new Btn (0, 0, 30, 28, "", BTN_GHOST, onAdd); add->setGlyph (WKG_PLUS); add->tip = "New"; addChild (add);
		clear = new Btn (0, 0, 60, 28, "Clear", BTN_GHOST, [] (Widget &) { app_clear_history (); }); clear->hidden = true; addChild (clear);
		filter = new LineEdit (0, 0, 10, 30); filter->placeholder = "Filter"; filter->vars = false;
		filter->onChange = [] (Widget &w) { Sidebar *s = (Sidebar *) w.parent; s->top = 0; s->rebuild (); };
		addChild (filter);
	}
	unsigned bgColor () override { return c_panel (); }
	void layout () override
	{
		place (add, width - 12 - 30, 10, 30, 28);
		place (clear, width - 12 - 60, 10, 60, 28);
		place (filter, RAIL_W + 10, HEAD_H, width - RAIL_W - 20, 30);
	}
	void setMode (int m) { mode = m; top = 0; show (add, m != SB_HIST); show (clear, m == SB_HIST); filter->setText (""); rebuild (); }
	int listY () { return HEAD_H + 30 + 8; }
	int visRows () { return imax (1, (height - listY ()) / ROW_H); }

	// ---- the rows ------------------------------------------------------------------------------------------------------
	bool matches (const char *s) { return filter->text.empty () || s_findi (s, (int) strlen (s), filter->text.c ()) >= 0; }
	bool anyMatch (Item *it)
	{
		if (matches (it->name.c ())) return true;
		for (int i = 0; i < it->kids.size (); i++) if (anyMatch (it->kids[i])) return true;
		return false;
	}
	void addItems (Item *parent, int depth)
	{
		bool filt = !filter->text.empty ();
		// the folders first, as Postman lists them
		for (int pass = 0; pass < 2; pass++)
			for (int i = 0; i < parent->kids.size (); i++)
			{
				Item *it = parent->kids[i];
				if ((pass == 0) != it->folder) continue;
				if (filt && !anyMatch (it)) continue;
				SideRow r; r.kind = it->folder ? R_FOLDER : R_REQ; r.ptr = it; r.idx = 0; r.depth = depth;
				rows.push (r);
				if (it->folder && (it->open || filt)) addItems (it, depth + 1);
			}
	}
	void rebuild ()
	{
		rows.clear ();
		SideRow r; r.idx = 0; r.depth = 0; r.ptr = 0;
		if (mode == SB_COLL)
		{
			bool filt = !filter->text.empty ();
			for (int i = 0; i < g_store.colls.size (); i++)
			{
				Collection *c = g_store.colls[i];
				if (filt && !matches (c->name.c ()) && !anyMatch (&c->root)) continue;
				r.kind = R_COLL; r.ptr = c; r.idx = i; r.depth = 0; rows.push (r);
				if (c->root.open || filt) addItems (&c->root, 1);
			}
			if (!rows.size ()) { r.kind = R_EMPTY; r.ptr = 0; rows.push (r); }
		}
		else if (mode == SB_ENV)
		{
			if (matches ("Globals")) { r.kind = R_GLOBALS; r.idx = -1; rows.push (r); }
			for (int i = 0; i < g_store.envs.size (); i++)
				if (matches (g_store.envs[i]->name.c ())) { r.kind = R_ENV; r.idx = i; r.ptr = g_store.envs[i]; rows.push (r); }
		}
		else
		{
			int lastDay = -1;
			long long now = now_unix ();
			for (int i = 0; i < g_store.history.size (); i++)
			{
				HistoryEntry &h = g_store.history[i];
				if (!matches (h.req.url.c ()) && !matches (h.req.method.c ())) continue;
				int day = (int) (h.when / 86400);
				if (day != lastDay) { r.kind = R_DAY; r.idx = day; r.depth = (int) (now / 86400) - day; rows.push (r); lastDay = day; }
				r.kind = R_HIST; r.idx = i; r.depth = 0; rows.push (r);
			}
			if (!g_store.history.size ()) { r.kind = R_EMPTY; rows.push (r); }
		}
		top = iclamp (top, 0, imax (0, rows.size () - visRows ()));
		invalidate (true);
	}

	// is this row the tab shown's?
	bool current (const SideRow &r)
	{
		Tab *t = cur_tab (); if (!t) return false;
		if (r.kind == R_REQ && t->kind == TAB_REQUEST) return t->itemId.eq (((Item *) r.ptr)->id.c ());
		if (r.kind == R_COLL && t->kind == TAB_COLL) return t->collId.eq (((Collection *) r.ptr)->id.c ());
		if (r.kind == R_ENV && t->kind == TAB_ENV) return t->envId.eq (((Environment *) r.ptr)->id.c ());
		if (r.kind == R_GLOBALS && t->kind == TAB_ENV) return t->envId.eq ("globals");
		return false;
	}

	void onDraw () override
	{
		unsigned bg = c_panel ();
		canvas.clear (bg);
		// the rail
		unsigned rail = C_BG;
		canvas.fillRect (0, 0, RAIL_W, height, rail);
		canvas.fillRect (RAIL_W, 0, 1, height, c_bgline ());
		static const char *const NAMES[] = { "Collections", "Environments", "History" };
		unsigned ink = wk_ink_for (rail);
		for (int i = 0; i < 3; i++)
		{
			int y = 10 + i * 66;
			bool s = i == mode, h = i == m_hotRail;
			if (s) { wk_rbox (canvas, 6, y, RAIL_W - 12, 60, 8, wk_mix (rail, C_ACCENT, 60), wk_mix (rail, C_ACCENT, 60)); }
			else if (h) wk_rbox (canvas, 6, y, RAIL_W - 12, 60, 8, wk_mix (rail, ink, 20), wk_mix (rail, ink, 20));
			unsigned c = s ? ink : wk_mix (rail, ink, 180);
			int cx = RAIL_W / 2, cy = y + 22;
			if (i == 0) icon_collections (canvas, cx, cy, c);
			else if (i == 1) icon_env (canvas, cx, cy, c);
			else wk_glyph (canvas, WKG_HISTORY, cx, cy, 20, c);
			WkFaceScope fs (g_small);
			int tw_ = wk_tw (NAMES[i]);
			wk_text (canvas, cx - tw_ / 2, y + 38, NAMES[i], c, 0);
		}
		// the list's head
		static const char *const TITLES[] = { "Collections", "Environments", "History" };
		text_v (canvas, RAIL_W + 14, 10, 28, TITLES[mode], wk_ink_for (bg), 2);
		// the rows
		int y0 = listY ();
		canvas.fillRect (RAIL_W + 1, y0 - 4, width - RAIL_W - 1, 1, wk_mix (bg, wk_ink_for (bg), 30));
		Canvas clip; clip.adopt (canvas.px + y0 * canvas.stride, width, imax (1, height - y0), canvas.stride);
		int vr = visRows ();
		bool bar = rows.size () > vr;
		int rw = width - (bar ? WK_SBW : 0);
		for (int k = 0; k <= vr; k++)
		{
			int i = top + k;
			if (i >= rows.size ()) break;
			drawRow (clip, rows[i], k * ROW_H, rw, i == m_hot);
		}
		if (bar)
		{
			WkThumb t = wk_thumb (rows.size (), vr, top, height - y0);
			wk_draw_vscroll (canvas, width - WK_SBW, y0, WK_SBW, height - y0, t, bg);
		}
	}
	void drawRow (Canvas &cv, SideRow &r, int y, int rw, bool hot)
	{
		unsigned bg = c_panel (), ink = wk_ink_for (bg), dim = wk_mix (bg, ink, 150);
		int x = RAIL_W + 8;
		bool cur = current (r);
		if (r.kind != R_DAY && r.kind != R_EMPTY)
		{
			if (cur) wk_rbox (cv, x - 2, y + 1, rw - x - 4, ROW_H - 2, 6, wk_mix (bg, C_ACCENT, 60), wk_mix (bg, C_ACCENT, 60));
			else if (hot) wk_rbox (cv, x - 2, y + 1, rw - x - 4, ROW_H - 2, 6, wk_mix (bg, ink, 18), wk_mix (bg, ink, 18));
		}
		int more = hot && r.kind != R_DAY && r.kind != R_EMPTY ? 26 : 0;
		switch (r.kind)
		{
		case R_COLL: case R_FOLDER:
		{
			bool coll = r.kind == R_COLL;
			const char *nm = coll ? ((Collection *) r.ptr)->name.c () : ((Item *) r.ptr)->name.c ();
			bool open = coll ? ((Collection *) r.ptr)->root.open : ((Item *) r.ptr)->open;
			if (!filter->text.empty ()) open = true;
			int ix = x + r.depth * 16;
			wk_glyph (cv, open ? WKG_CHEV_DOWN : WKG_CHEV_RIGHT, ix + 8, y + ROW_H / 2, 8, dim);
			if (coll) icon_collections (cv, ix + 28, y + ROW_H / 2 + 1, wk_mix (bg, C_ACCENT, 200));
			else
			{
				unsigned fc = wk_mix (bg, ink, 130);
				wk_rline (cv, ix + 19, y + ROW_H / 2 - 5, 18, 13, 2, fc);
				wk_rbox (cv, ix + 19, y + ROW_H / 2 - 8, 8, 4, 2, fc, fc, 255, WK_TL | WK_TR);
			}
			text_fit (cv, ix + 44, y, ROW_H, rw - ix - 44 - 8 - more, nm, ink, coll ? 2 : 0);
			break;
		}
		case R_REQ:
		{
			Item *it = (Item *) r.ptr;
			int ix = x + r.depth * 16;
			draw_method_badge (cv, ix, y, ROW_H, it->req.method.c (), 38);
			text_fit (cv, ix + 46, y, ROW_H, rw - ix - 46 - 8 - more, it->name.c (), ink);
			break;
		}
		case R_GLOBALS: case R_ENV:
		{
			bool glob = r.kind == R_GLOBALS;
			const char *nm = glob ? "Globals" : ((Environment *) r.ptr)->name.c ();
			if (glob)
			{
				// a globe
				unsigned gc = wk_mix (bg, ink, 150);
				wk_rline (cv, x + 6, y + ROW_H / 2 - 7, 15, 15, 7, gc);
				wk_rline (cv, x + 10, y + ROW_H / 2 - 7, 7, 15, 3, gc);
				cv.fillRect (x + 7, y + ROW_H / 2, 13, 1, gc);
			}
			else icon_env (cv, x + 14, y + ROW_H / 2, wk_mix (bg, ink, 150));
			text_fit (cv, x + 32, y, ROW_H, rw - x - 32 - 40 - more, nm, ink, glob ? 2 : 0);
			if (!glob && r.idx == g_store.activeEnv) wk_glyph (cv, WKG_CHECK, rw - 20 - more, y + ROW_H / 2, 12, C_ACCENT);
			break;
		}
		case R_DAY:
		{
			char b[48];
			if (r.depth == 0) s_copy (b, "Today", sizeof b);
			else if (r.depth == 1) s_copy (b, "Yesterday", sizeof b);
			else
			{
				static const char *const MON[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
				int yy, mm, dd; civil_from_days (r.idx, &yy, &mm, &dd);
				snprintf (b, sizeof b, "%s %d, %d", MON[(mm - 1) % 12], dd, yy);
			}
			text_v (cv, x + 4, y + 4, ROW_H - 4, b, dim, 2);
			break;
		}
		case R_HIST:
		{
			HistoryEntry &h = g_store.history[r.idx];
			draw_method_badge (cv, x, y, ROW_H, h.req.method.c (), 38);
			char st[16]; st[0] = 0;
			if (h.status > 0) snprintf (st, sizeof st, "%d", h.status); else if (h.status < 0) s_copy (st, "ERR", sizeof st);
			int sw = st[0] ? wk_tw (st) + 6 : 0;
			const char *u = h.req.url.c ();
			if (s_startsi (u, "https://")) u += 8; else if (s_startsi (u, "http://")) u += 7;
			text_fit (cv, x + 46, y, ROW_H, rw - x - 46 - 10 - sw - more, u, ink);
			if (st[0] && !more) text_v (cv, rw - 10 - sw, y, ROW_H, st, h.status >= 0 ? status_color (h.status) : on_field (C_BAD));
			break;
		}
		case R_EMPTY:
		{
			const char *a = mode == SB_COLL ? "No collections yet" : "Nothing sent yet";
			const char *b = mode == SB_COLL ? "Save a request (Ctrl+S), click +, or import a Postman collection." : "The requests sent are listed here, by day.";
			text_at (cv, x + 6, y + 14, a, ink, 2);
			text_wrap (cv, x + 6, y + 14 + wk_fh () + 8, width - x - 20, b, dim);
			break;
		}
		}
		if (more)
		{
			unsigned mc = wk_mix (bg, ink, m_hotMore ? 230 : 150);
			int mx = rw - 18;
			for (int d = -5; d <= 5; d += 5) wk_rbox (cv, mx + d - 1, y + ROW_H / 2 - 1, 3, 3, 1, mc, mc);
		}
	}

	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		// the children first (the filter, the buttons)
		if (mx < 0) { if (m_hot >= 0 || m_hotRail >= 0) { m_hot = m_hotRail = -1; invalidate (true); } return false; }
		if (wheel) { top = iclamp (top + wheel * 2, 0, imax (0, rows.size () - visRows ())); invalidate (true); return true; }
		int hr = mx < RAIL_W && my >= 10 && my < 10 + 3 * 66 ? (my - 10) / 66 : -1;
		int y0 = listY ();
		int h = mx > RAIL_W && my >= y0 ? top + (my - y0) / ROW_H : -1;
		if (h >= rows.size ()) h = -1;
		bool bar = rows.size () > visRows ();
		int rw = width - (bar ? WK_SBW : 0);
		bool more = h >= 0 && mx >= rw - 30 && mx < rw - 4;
		if (h != m_hot || hr != m_hotRail || more != m_hotMore) { m_hot = h; m_hotRail = hr; m_hotMore = more; invalidate (true); }
		// the history's full URL as a tooltip
		tip = 0;
		if (h >= 0 && rows[h].kind == R_HIST) { m_tip = g_store.history[rows[h].idx].req.url; tip = m_tip.c (); }
		if ((bl || br) && !pressed)
		{
			pressed = true;
			if (hr >= 0 && bl) { setMode (hr); return true; }
			if (bar && mx >= width - WK_SBW && my >= y0)
			{
				WkThumb t = wk_thumb (rows.size (), visRows (), top, height - y0);
				top = (int) wk_thumb_pos (my - y0, height - y0, rows.size (), visRows (), t.h); invalidate (true);
				return true;
			}
			if (h < 0) return true;
			SideRow r = rows[h];
			if (br || more) { menu (r, mx, my); return true; }
			unsigned now = kapi_get_ticks ();
			bool dbl = now - m_lastClick < 35 && m_lastRow == h;
			m_lastClick = now; m_lastRow = h;
			click (r, mx, dbl);
		}
		else if (!bl && !br) pressed = false;
		return true;
	}
	void click (const SideRow &r, int mx, bool dbl)
	{
		switch (r.kind)
		{
		case R_COLL:
		{
			Collection *c = (Collection *) r.ptr;
			if (dbl) { app_open_coll (c); return; }
			c->root.open = !c->root.open; rebuild ();
			break;
		}
		case R_FOLDER: { Item *it = (Item *) r.ptr; it->open = !it->open; rebuild (); break; }
		case R_REQ: app_open_item ((Item *) r.ptr); break;
		case R_GLOBALS: app_open_env (-1); break;
		case R_ENV:
			if (mx >= width - 50 && mx < width - 30) app_set_env (r.idx == g_store.activeEnv ? -1 : r.idx);
			else app_open_env (r.idx);
			break;
		case R_HIST: app_open_history (r.idx); break;
		}
		invalidate (true);
	}
	void menu (const SideRow &r, int mx, int my)
	{
		int ax = mx, ay = my;
		for (Widget *w = this; w && w->parent; w = w->parent) { ax += w->left; ay += w->top; }
		PopupMenu m (ax, ay);
		enum { OPEN = 1, NEWREQ, NEWFOLDER, RENAME, DUP, DEL, EXPORT, UP, DOWN, SETENV, UNSETENV, CLEARH };
		switch (r.kind)
		{
		case R_COLL:
			m.add ("Open (variables, auth)", OPEN); m.separator ();
			m.add ("Add Request", NEWREQ); m.add ("Add Folder", NEWFOLDER); m.separator ();
			m.add ("Rename...", RENAME); m.add ("Duplicate", DUP); m.add ("Export...", EXPORT); m.separator ();
			m.add ("Delete...", DEL);
			break;
		case R_FOLDER:
			m.add ("Add Request", NEWREQ); m.add ("Add Folder", NEWFOLDER); m.separator ();
			m.add ("Rename...", RENAME); m.add ("Duplicate", DUP); m.add ("Move Up", UP); m.add ("Move Down", DOWN); m.separator ();
			m.add ("Delete...", DEL);
			break;
		case R_REQ:
			m.add ("Open", OPEN); m.separator ();
			m.add ("Rename...", RENAME); m.add ("Duplicate", DUP); m.add ("Move Up", UP); m.add ("Move Down", DOWN); m.separator ();
			m.add ("Delete...", DEL);
			break;
		case R_GLOBALS: m.add ("Open", OPEN); m.add ("Export...", EXPORT); break;
		case R_ENV:
			m.add ("Open", OPEN);
			if (r.idx == g_store.activeEnv) m.add ("Stop Using It", UNSETENV); else m.add ("Use It (Set Active)", SETENV);
			m.separator ();
			m.add ("Rename...", RENAME); m.add ("Duplicate", DUP); m.add ("Export...", EXPORT); m.separator ();
			m.add ("Delete...", DEL);
			break;
		case R_HIST: m.add ("Open", OPEN); m.add ("Delete", DEL); m.separator (); m.add ("Clear the History...", CLEARH); break;
		default: return;
		}
		int id = m.run ();
		if (id < 0) return;
		Item *it = (r.kind == R_FOLDER || r.kind == R_REQ) ? (Item *) r.ptr : 0;
		Collection *c = r.kind == R_COLL ? (Collection *) r.ptr : 0;
		switch (id)
		{
		case OPEN:
			if (c) app_open_coll (c); else if (r.kind == R_REQ) app_open_item (it);
			else if (r.kind == R_GLOBALS) app_open_env (-1); else if (r.kind == R_ENV) app_open_env (r.idx);
			else if (r.kind == R_HIST) app_open_history (r.idx);
			break;
		case NEWREQ: app_new_request (c ? &c->root : it); break;
		case NEWFOLDER: app_new_folder (c ? &c->root : it); break;
		case RENAME: if (c) app_rename_coll (c); else if (it) app_rename_item (it); else if (r.kind == R_ENV) app_rename_env (r.idx); break;
		case DUP: if (c) app_duplicate_coll (c); else if (it) app_duplicate_item (it); else if (r.kind == R_ENV) app_duplicate_env (r.idx); break;
		case DEL:
			if (c) app_delete_coll (c); else if (it) app_delete_item (it); else if (r.kind == R_ENV) app_delete_env (r.idx);
			else if (r.kind == R_HIST) app_delete_history (r.idx);
			break;
		case EXPORT: if (c) app_export_coll (c); else if (r.kind == R_ENV) app_export_env (r.idx); else if (r.kind == R_GLOBALS) app_export_env (-1); break;
		case UP: app_move_item (it, -1); break;
		case DOWN: app_move_item (it, 1); break;
		case SETENV: app_set_env (r.idx); break;
		case UNSETENV: app_set_env (-1); break;
		case CLEARH: app_clear_history (); break;
		}
	}
	static void onAdd (Widget &w)
	{
		Sidebar *s = (Sidebar *) w.parent;
		if (s->mode == SB_COLL) app_new_collection ();
		else if (s->mode == SB_ENV) app_new_env ();
	}
	static TextFace *g_small;		// the rail's labels (a smaller face)
private:
	int m_hot, m_hotRail; bool m_hotMore;
	unsigned m_lastClick; int m_lastRow;
	Str m_tip;
};
TextFace *Sidebar::g_small = 0;

} // namespace cr

#endif
