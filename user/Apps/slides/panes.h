//
// panes.h -- what is round the slide: the slides' thumbnails at the left (grouped in sections, folded with
// their arrow; a click goes to a slide, a drag moves it, a right click its menu), the slide sorter (the
// whole deck by section: each slide's transition and timing, several selected, dragged), the speaker's
// notes under the slide, the status bar (the slide, its section, the theme; the notes, the views, the zoom).
// The thumbnails are drawn by the same layers as the slide (render.h), composited by the CPU into small
// pictures kept until their slide changes.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _slides_panes_h
#define _slides_panes_h

#include "view.h"
namespace ss { static const unsigned AUTO = 0xFF000000u; }
#include "Apps/sheet/ui_base.h"

namespace sl {

using ss::ListPopup; using ss::PickBox; using ss::ColorPopup; using ss::ToolButton; using ss::ToolBar;

static const unsigned PANEL_C = 0xE2D8D1;	// the panes' face (the mock-ups')

// UTF-8 (the deck's names) -> Latin-1 (uikit's text): four buffers in turn
static const char *L1 (const char *s)
{
	static char buf[4][256]; static int k;
	char *o = buf[k++ & 3]; int n = 0, len = (int) strlen (s);
	for (int i = 0; i < len && n < 255; ) { int l; unsigned c = ss::u8_dec (s + i, len - i, &l); i += l > 0 ? l : 1; o[n++] = (char) (c < 256 ? c : '?'); }
	o[n] = 0;
	return o;
}

// ---- the thumbnails' cache ------------------------------------------------------------------------------------------
static unsigned slide_hash (const Deck &d, const Slide &s, int index)
{
	Hash h;
	h.addi (index); h.addi (s.layout); h.addi (s.masterObjects); h.add (&s.bg, sizeof s.bg); h.addi (s.obj.n);
	h.add (&d.masterBg, sizeof d.masterBg); h.add (d.footerText, (int) strlen (d.footerText)); h.addi (d.footer); h.addi (d.number); h.addi (d.sw); h.addi (d.sh);
	for (int i = 0; i < s.obj.n; i++) { const Object &o = *s.obj[i]; h.addi ((int) obj_hash (d, o, 0, false)); h.addi (o.x); h.addi (o.y); h.addi (o.rot); h.addi (o.flipH); h.addi (o.flipV); }
	for (int i = 0; i < d.decor.n; i++) { h.addi ((int) obj_hash (d, *d.decor[i], 0, false)); h.addi (d.decor[i]->x); h.addi (d.decor[i]->y); }
	return h.h;
}
struct Thumb { Slide *s; unsigned hash; int w, h; unsigned *px; };
struct ThumbCache
{
	Compositor C;
	Vec<Thumb> t;
	ThumbCache () {}
	unsigned *get (int index, int w, int h)
	{
		Slide *s = g_deck.slides[index];
		unsigned hs = slide_hash (g_deck, *s, index);
		for (int i = 0; i < t.n; i++)
			if (t[i].s == s && t[i].w == w)
			{
				if (t[i].hash == hs) return t[i].px;
				render (t[i], index, hs); return t[i].px;
			}
		Thumb n; n.s = s; n.w = w; n.h = h; n.px = (unsigned *) malloc ((size_t) w * h * 4); n.hash = 0;
		render (n, index, hs);
		t.push (n);
		return t[t.n - 1].px;
	}
	void render (Thumb &th, int index, unsigned hs)
	{
		C.init (true);
		C.frame++;
		flatten_slide (C, g_deck, *g_deck.slides[index], index, th.px, th.w, th.h, th.w);
		C.sweep (0);
		th.hash = hs;
		// the master view's layouts: their placeholders outlined (empty, they draw nothing)
		if (g_master && index > 0)
		{
			const Slide &s = *g_deck.slides[index];
			float k = (float) th.w / g_deck.sw;
			for (int i = 0; i < s.obj.n; i++)
			{
				const Object &o = *s.obj[i];
				if (o.ph == PH_NONE || !o.tb.empty ()) continue;
				int x0 = imax (0, (int) (o.x * k)), y0 = imax (0, (int) (o.y * k)), x1 = imin (th.w - 1, (int) ((o.x + o.w) * k)), y1 = imin (th.h - 1, (int) ((o.y + o.h) * k));
				for (int y = y0; y <= y1; y++)
					for (int x = x0; x <= x1; x++)
					{
						unsigned &p = th.px[y * th.w + x];
						bool edge = x == x0 || x == x1 || y == y0 || y == y1;
						if (edge) { if (((x + y) & 3) < 2) p = 0xFF8A949C; }
						else if (o.ph == PH_TITLE && y < y0 + (y1 - y0) / 2 && y > y0 + (y1 - y0) / 4 && x < x0 + (x1 - x0) * 2 / 3) p = 0xFFC8CED3;
						else if (o.ph != PH_TITLE && o.ph != PH_PICTURE && (y - y0) % 6 == 3 && x < x1 - (x1 - x0) / 4) p = 0xFFD5DADE;
						else if (o.ph == PH_PICTURE) p = 0xFFE6EAEE;
					}
			}
		}
	}
	void prune ()		// the slides gone
	{
		for (int i = 0; i < t.n; )
		{
			bool alive = g_deck.slides.find (t[i].s) >= 0;
			if (!alive) { free (t[i].px); t.erase (i); } else i++;
		}
	}
	void clear () { for (int i = 0; i < t.n; i++) free (t[i].px); t.clear (); }
};
static ThumbCache g_thumbs;

// The slide's picture into a canvas at (x, y), w x h (its frame drawn).
static void thumb_blit (Canvas &cv, int index, int x, int y, int w, int h)
{
	unsigned *px = g_thumbs.get (index, w, h);
	for (int j = 0; j < h; j++)
	{
		int yy = y + j; if (yy < 0 || yy >= cv.h) continue;
		for (int i = 0; i < w; i++) { int xx = x + i; if (xx >= 0 && xx < cv.w) cv.px[(long) yy * cv.stride + xx] = px[j * w + i]; }
	}
}
static bool section_folded (const char *name);

// ---- the slides' column -------------------------------------------------------------------------------------------
static void (*g_onSlideMenu) (int x, int y);	// its right click (window coordinates)
static char g_folded[16][48]; static int g_nfolded;
static bool section_folded (const char *name) { for (int i = 0; i < g_nfolded; i++) if (!strcmp (g_folded[i], name)) return true; return false; }
static void section_fold (const char *name)
{
	for (int i = 0; i < g_nfolded; i++) if (!strcmp (g_folded[i], name)) { g_folded[i][0] = 0; memmove (g_folded[i], g_folded[i + 1], sizeof g_folded[0] * (g_nfolded - i - 1)); g_nfolded--; return; }
	if (g_nfolded < 16) scpy (g_folded[g_nfolded++], name, 48);
}
// The section a slide belongs to (the last one started at or before it).
static const char *section_of (int i)
{
	for (int k = i; k >= 0; k--) if (g_deck.slides[k]->section[0]) return g_deck.slides[k]->section;
	return "";
}

class SlideList : public Widget
{
public:
	enum { HEAD = 22, GAP = 12 };
	SlideList (int l, int t, int w, int h) : Widget (l, t, w, h), m_top (0), m_dragFrom (-1), m_dropAt (-1), m_down (false), m_hot (-1) { canFocus = true; }
	unsigned bgColor () override { return PANEL_C; }
	int tw () const { return width - 46; }
	int th () const { return tw () * g_deck.sh / g_deck.sw; }
	// Each row: a section's head (slide = -1 - i, the section starting at i) or a slide.
	struct Row { int slide; int y, h; bool head; };
	void rows (Vec<Row> &r)
	{
		r.clear ();
		int y = 8;
		bool folded = false;
		for (int i = 0; i < g_deck.slides.n; i++)
		{
			Slide *s = g_deck.slides[i];
			if (s->section[0]) { Row h; h.slide = i; h.y = y; h.h = HEAD; h.head = true; r.push (h); y += HEAD; folded = section_folded (s->section); }
			if (folded && i != g_cur) continue;
			Row q; q.slide = i; q.y = y; q.h = th () + GAP; q.head = false; r.push (q); y += q.h;
		}
		m_total = y + 8;
	}
	void onDraw () override
	{
		canvas.clear (PANEL_C);
		Vec<Row> r; rows (r);
		int maxTop = imax (0, m_total - height); if (m_top > maxTop) m_top = maxTop;
		int W = tw (), H = th ();
		for (int k = 0; k < r.n; k++)
		{
			Row &q = r[k]; int y = q.y - m_top;
			if (y + q.h < 0 || y > height) continue;
			Slide *s = g_deck.slides[q.slide];
			if (q.head)
			{
				bool f = section_folded (s->section);
				uk_glyph (canvas, f ? WKG_CHEV_RIGHT : WKG_CHEV_DOWN, 13, y + q.h / 2, 7, 0x786C64);
				uk_text_l (canvas, 22, y, q.h, L1 (s->section), 0x5A504A, 1);
				continue;
			}
			int x = 30;
			bool cur = q.slide == g_cur;
			if (cur) uk_rbox (canvas, x - 5, y - 4, W + 10, H + 8, 5, HANDLE_C, HANDLE_C);
			else if (q.slide == m_hot) uk_rbox (canvas, x - 5, y - 4, W + 10, H + 8, 5, uk_mix (PANEL_C, HANDLE_C, 70), uk_mix (PANEL_C, HANDLE_C, 70));
			thumb_blit (canvas, q.slide, x, y, W, H);
			canvas.frameRect (x - 1, y - 1, W + 2, H + 2, uk_mix (PANEL_C, 0, 90));
			if (s->hidden) { for (int j = 0; j < H; j++) for (int i = 0; i < W; i++) { unsigned &p = canvas.px[(long) (y + j) * canvas.stride + x + i]; if (y + j >= 0 && y + j < canvas.h) p = uk_mix (p, PANEL_C, 150); } }
			char n[8]; snprintf (n, sizeof n, "%d", q.slide + 1);
			int nw = uk_text_w (n);
			uk_text_l (canvas, 24 - nw, y, 18, n, cur ? HANDLE_C : 0x786C64, cur ? 1 : 0);
			if (s->hidden) { canvas.fillRect (24 - nw - 2, y + 9, nw + 4, 1, 0x786C64); }
			if (s->anim.n) uk_glyph (canvas, WKG_DOT, 16, y + 30, 6, 0x9A8C80);
		}
		// the drop place
		if (m_dragFrom >= 0 && m_dropAt >= 0)
		{
			int y = -1;
			for (int k = 0; k < r.n; k++) if (!r[k].head && r[k].slide == m_dropAt) y = r[k].y - m_top - 6;
			if (m_dropAt >= g_deck.slides.n && r.n) y = r[r.n - 1].y + r[r.n - 1].h - m_top - 6;
			if (y >= 0) uk_rbox (canvas, 20, y - 1, W + 16, 4, 2, HANDLE_C, HANDLE_C);
		}
		// the scroll bar
		if (m_total > height) { UkThumb t = uk_thumb (m_total, height, m_top, height - 8); uk_draw_vscroll (canvas, width - UK_SBW - 2, 4, UK_SBW, height - 8, t, PANEL_C); }
		uk_etch_v (canvas, width - 1, 0, height, PANEL_C);
	}
	int row_at (int my, bool *head)
	{
		Vec<Row> r; rows (r);
		for (int k = 0; k < r.n; k++) if (my + m_top >= r[k].y && my + m_top < r[k].y + r[k].h) { *head = r[k].head; return r[k].slide; }
		*head = false; return -1;
	}
	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (m_bar.mouse (in ? mx : -1, my, bl, width - UK_SBW - 2, UK_SBW, 4, height - 8, m_total, height, &m_top)) { invalidate (true); return true; }
		if (wheel && in) { m_top = imax (0, imin (m_total - height, m_top - wheel * 60)); invalidate (true); return true; }
		bool head; int i = in ? row_at (my, &head) : -1;
		int hot = in && !head ? i : -1;
		if (hot != m_hot) { m_hot = hot; invalidate (true); }
		if (br && in && !m_rdown)
		{
			m_rdown = true; setFocus ();
			if (i >= 0) { go_slide (i); invalidate (true); }
			if (g_onSlideMenu) { int ax = mx, ay = my; for (Widget *w = this; w && w->parent; w = w->parent) { ax += w->left; ay += w->top; } g_onSlideMenu (ax, ay); }
			return true;
		}
		if (!br) m_rdown = false;
		if (bl && !m_down && in)
		{
			m_down = true; setFocus ();
			if (i >= 0 && head) { section_fold (g_deck.slides[i]->section); invalidate (true); m_down = true; m_dragFrom = -1; return true; }
			if (i >= 0) { go_slide (i); m_dragFrom = i; m_y0 = my; }
			invalidate (true);
			return true;
		}
		if (bl && m_down && m_dragFrom >= 0)
		{
			if (abs (my - m_y0) > 6)
			{
				// the drop: before the slide under the pointer's middle
				Vec<Row> r; rows (r);
				int at = g_deck.slides.n;
				for (int k = 0; k < r.n; k++) if (!r[k].head && my + m_top < r[k].y + r[k].h / 2) { at = r[k].slide; break; }
				if (at != m_dropAt) { m_dropAt = at; invalidate (true); }
				if (my < 10) { m_top = imax (0, m_top - 12); invalidate (true); }
				if (my > height - 10) { m_top = imin (imax (0, m_total - height), m_top + 12); invalidate (true); }
			}
			return true;
		}
		if (!bl && m_down)
		{
			m_down = false;
			if (m_dragFrom >= 0 && m_dropAt >= 0)
			{
				int to = m_dropAt > m_dragFrom ? m_dropAt - 1 : m_dropAt;
				if (to != m_dragFrom) move_slide (m_dragFrom, iclamp (to, 0, g_deck.slides.n - 1));
			}
			m_dragFrom = m_dropAt = -1;
			invalidate (true);
			return true;
		}
		return in;
	}
	bool onKey (long k) override
	{
		if (k == KEY_UP || k == KEY_LEFT || k == KEY_PGUP) { go_slide (g_cur - 1); show_cur (); return true; }
		if (k == KEY_DOWN || k == KEY_RIGHT || k == KEY_PGDN) { go_slide (g_cur + 1); show_cur (); return true; }
		if (k == KEY_HOME) { go_slide (0); show_cur (); return true; }
		if (k == KEY_END) { go_slide (g_deck.slides.n - 1); show_cur (); return true; }
		if (k == KEY_DEL || k == KEY_BACKSPACE) { cmd_delete_slide (); show_cur (); return true; }
		if (k == KEY_ENTER) { cmd_new_slide (); show_cur (); return true; }
		return false;
	}
	void show_cur ()
	{
		Vec<Row> r; rows (r);
		for (int k = 0; k < r.n; k++) if (!r[k].head && r[k].slide == g_cur)
		{
			if (r[k].y < m_top) m_top = r[k].y - 8;
			else if (r[k].y + r[k].h > m_top + height) m_top = r[k].y + r[k].h - height + 8;
		}
		if (m_top < 0) m_top = 0;
		invalidate (true);
	}
	long m_top; int m_total = 0, m_dragFrom, m_dropAt, m_y0 = 0; bool m_down, m_rdown = false; int m_hot;
	UkBarDrag m_bar;
};

// ---- the slide sorter --------------------------------------------------------------------------------------------
static const char *const TR_NAMES[TR_COUNT] = { "None", "Fade", "Push", "Wipe", "Cover", "Uncover", "Split", "Zoom", "Dissolve" };
static Vec<int> g_sorterSel;
static void (*g_onOpenSlide) (int i);		// a double click in the sorter: back to the normal view on it
class Sorter : public Widget
{
public:
	Sorter (int l, int t, int w, int h) : Widget (l, t, w, h), m_top (0), m_down (false), m_drag (-1), m_drop (-1), m_lastT (0) { canFocus = true; }
	unsigned bgColor () override { return 0xECE6E2; }
	int cols () const { return width >= 900 ? 5 : width >= 640 ? 4 : 3; }
	int cw () const { return (width - 60 - (cols () - 1) * 22) / cols (); }
	int ch () const { return cw () * g_deck.sh / g_deck.sw; }
	struct Cell { int slide, x, y; };
	struct Head { int slide, y; };
	void cells (Vec<Cell> &c, Vec<Head> &hd)
	{
		c.clear (); hd.clear ();
		int y = 12, col = 0, W = cw (), H = ch ();
		for (int i = 0; i < g_deck.slides.n; i++)
		{
			Slide *s = g_deck.slides[i];
			if (s->section[0] || (i == 0))
			{
				if (col) { y += H + 34; col = 0; }
				if (s->section[0]) { Head h; h.slide = i; h.y = y; hd.push (h); y += 28; }
			}
			Cell k; k.slide = i; k.x = 30 + col * (W + 22); k.y = y; c.push (k);
			if (++col == cols ()) { col = 0; y += H + 34; }
		}
		m_total = y + (col ? H + 34 : 0) + 12;
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		Vec<Cell> c; Vec<Head> hd; cells (c, hd);
		int W = cw (), H = ch ();
		for (int k = 0; k < hd.n; k++)
		{
			int y = hd[k].y - (int) m_top; Slide *s = g_deck.slides[hd[k].slide];
			uk_glyph (canvas, WKG_CHEV_DOWN, 18, y + 9, 7, 0x786C64);
			uk_text_l (canvas, 30, y, 18, L1 (s->section), 0x2A2420, 1);
			int n = 0; for (int i = hd[k].slide; i < g_deck.slides.n; i++) { if (i > hd[k].slide && g_deck.slides[i]->section[0]) break; n++; }
			char t[24]; snprintf (t, sizeof t, "%d slide%s", n, n == 1 ? "" : "s");
			int x = 30 + uk_text_w (L1 (s->section), 1) + 10;
			uk_text_l (canvas, x, y, 18, t, 0x786C64);
			canvas.fillRect (x + uk_text_w (t) + 12, y + 9, width - 30 - (x + uk_text_w (t) + 12), 1, 0xC8BCB2);
		}
		for (int k = 0; k < c.n; k++)
		{
			int x = c[k].x, y = c[k].y - (int) m_top, i = c[k].slide;
			if (y + H + 30 < 0 || y > height) continue;
			Slide *s = g_deck.slides[i];
			bool sel = g_sorterSel.find (i) >= 0 || i == g_cur;
			if (sel) uk_rbox (canvas, x - 5, y - 5, W + 10, H + 10, 6, i == g_cur ? HANDLE_C : uk_mix (bgColor (), HANDLE_C, 140), i == g_cur ? HANDLE_C : uk_mix (bgColor (), HANDLE_C, 140));
			thumb_blit (canvas, i, x, y, W, H);
			canvas.frameRect (x - 1, y - 1, W + 2, H + 2, 0xB8ACA4);
			if (s->hidden) for (int j = 0; j < H; j++) for (int q = 0; q < W; q++) { int yy = y + j; if (yy >= 0 && yy < canvas.h) { unsigned &p = canvas.px[(long) yy * canvas.stride + x + q]; p = uk_mix (p, bgColor (), 140); } }
			char n[8]; snprintf (n, sizeof n, "%d", i + 1);
			uk_text_l (canvas, x, y + H + 4, 18, n, sel ? HANDLE_C : 0x2A2420, 1);
			char t[48]; snprintf (t, sizeof t, "%s", TR_NAMES[(int) s->tr.type]);
			uk_text_l (canvas, x + 22, y + H + 4, 18, t, 0x786C64);
			char r[32] = "";
			if (s->tr.after >= 0) snprintf (r, sizeof r, "%d:%02d", s->tr.after / 60000, s->tr.after / 1000 % 60);
			else if (s->anim.n) snprintf (r, sizeof r, "%d effect%s", s->anim.n, s->anim.n == 1 ? "" : "s");
			if (r[0]) uk_text_l (canvas, x + W - uk_text_w (r), y + H + 4, 18, r, 0x786C64);
			if (s->hidden) { uk_rbox (canvas, x + W - 64, y + 6, 58, 18, 9, 0x303030, 0x303030); uk_text_c (canvas, x + W - 64, y + 6, 58, 18, "hidden", 0xFFFFFF, 1); }
		}
		if (m_drag >= 0 && m_drop >= 0)
		{
			for (int k = 0; k < c.n; k++) if (c[k].slide == m_drop) uk_rbox (canvas, c[k].x - 13, c[k].y - (int) m_top, 4, H, 2, HANDLE_C, HANDLE_C);
			if (m_drop >= g_deck.slides.n && c.n) uk_rbox (canvas, c[c.n - 1].x + W + 9, c[c.n - 1].y - (int) m_top, 4, H, 2, HANDLE_C, HANDLE_C);
		}
		if (m_total > height) { UkThumb t = uk_thumb (m_total, height, m_top, height - 8); uk_draw_vscroll (canvas, width - UK_SBW - 2, 4, UK_SBW, height - 8, t, bgColor ()); }
	}
	int at (int mx, int my)
	{
		Vec<Cell> c; Vec<Head> hd; cells (c, hd);
		for (int k = 0; k < c.n; k++) if (mx >= c[k].x - 6 && mx < c[k].x + cw () + 6 && my + m_top >= c[k].y - 6 && my + m_top < c[k].y + ch () + 26) return c[k].slide;
		return -1;
	}
	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (m_bar.mouse (in ? mx : -1, my, bl, width - UK_SBW - 2, UK_SBW, 4, height - 8, m_total, height, &m_top)) { invalidate (true); return true; }
		if (wheel && in) { m_top = imax (0, imin (m_total - height, (int) m_top - wheel * 80)); invalidate (true); return true; }
		if (br && in && !m_rdown) { m_rdown = true; int i = at (mx, my); if (i >= 0 && g_sorterSel.find (i) < 0) { g_sorterSel.clear (); go_slide (i); } if (g_onSlideMenu) { int ax = mx, ay = my; for (Widget *w = this; w && w->parent; w = w->parent) { ax += w->left; ay += w->top; } g_onSlideMenu (ax, ay); } invalidate (true); return true; }
		if (!br) m_rdown = false;
		if (bl && !m_down && in)
		{
			m_down = true; setFocus ();
			int i = at (mx, my);
			unsigned now = kapi_get_ticks ();
			if (i >= 0 && i == g_cur && now - m_lastT < 40) { if (g_onOpenSlide) g_onOpenSlide (i); m_down = false; return true; }
			m_lastT = now;
			unsigned mods = kapi_get_modifiers ();
			if (i >= 0)
			{
				if (mods & MOD_CTRL) { int f = g_sorterSel.find (i); if (f >= 0) g_sorterSel.erase (f); else g_sorterSel.push (i); if (g_sorterSel.find (g_cur) < 0 && g_cur != i) g_sorterSel.push (g_cur); }
				else if (mods & MOD_SHIFT) { int a = imin (g_cur, i), b = imax (g_cur, i); g_sorterSel.clear (); for (int k = a; k <= b; k++) g_sorterSel.push (k); }
				else { if (g_sorterSel.find (i) < 0) g_sorterSel.clear (); }
				g_cur = i; notify ();
				m_drag = i; m_x0 = mx; m_y0 = my;
			}
			else g_sorterSel.clear ();
			invalidate (true);
			return true;
		}
		if (bl && m_down && m_drag >= 0)
		{
			if (abs (mx - m_x0) + abs (my - m_y0) > 8)
			{
				Vec<Cell> c; Vec<Head> hd; cells (c, hd);
				int best = g_deck.slides.n;
				for (int k = 0; k < c.n; k++) if (my + m_top >= c[k].y - 10 && my + m_top < c[k].y + ch () + 30 && mx < c[k].x + cw () / 2) { best = c[k].slide; break; }
				if (best != m_drop) { m_drop = best; invalidate (true); }
			}
			return true;
		}
		if (!bl && m_down)
		{
			m_down = false;
			if (m_drag >= 0 && m_drop >= 0)
			{
				// the slides selected (or the one dragged) moved before m_drop
				Vec<int> v; if (g_sorterSel.n > 1) for (int i = 0; i < g_sorterSel.n; i++) v.push (g_sorterSel[i]); else v.push (m_drag);
				for (int i = 1; i < v.n; i++) for (int j = i; j > 0 && v[j] < v[j - 1]; j--) { int t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
				begin_change ();
				Vec<Slide *> moving; for (int i = 0; i < v.n; i++) moving.push (g_deck.slides[v[i]]);
				Slide *before = m_drop < g_deck.slides.n ? g_deck.slides[m_drop] : 0;
				for (int i = 0; i < moving.n; i++) g_deck.slides.erase (g_deck.slides.find (moving[i]));
				int at = before ? g_deck.slides.find (before) : g_deck.slides.n;
				if (at < 0) at = g_deck.slides.n;
				for (int i = 0; i < moving.n; i++) g_deck.slides.insert (at + i, moving[i]);
				g_cur = at; g_sorterSel.clear (); for (int i = 0; i < moving.n; i++) g_sorterSel.push (at + i);
				done_change (); notify_slides ();
			}
			m_drag = m_drop = -1;
			invalidate (true);
			return true;
		}
		return in;
	}
	bool onKey (long k) override
	{
		int c = cols ();
		int d = k == KEY_LEFT ? -1 : k == KEY_RIGHT ? 1 : k == KEY_UP ? -c : k == KEY_DOWN ? c : 0;
		if (d) { g_sorterSel.clear (); go_slide (iclamp (g_cur + d, 0, g_deck.slides.n - 1)); invalidate (true); return true; }
		if (k == KEY_ENTER) { if (g_onOpenSlide) g_onOpenSlide (g_cur); return true; }
		if (k == KEY_DEL || k == KEY_BACKSPACE)
		{
			if (g_sorterSel.n > 1)
			{
				begin_change ();
				for (int i = 1; i < g_sorterSel.n; i++) for (int j = i; j > 0 && g_sorterSel[j] > g_sorterSel[j - 1]; j--) { int t = g_sorterSel[j]; g_sorterSel[j] = g_sorterSel[j - 1]; g_sorterSel[j - 1] = t; }
				for (int i = 0; i < g_sorterSel.n && g_deck.slides.n > 1; i++) { delete g_deck.slides[g_sorterSel[i]]; g_deck.slides.erase (g_sorterSel[i]); }
				g_sorterSel.clear (); g_cur = iclamp (g_cur, 0, g_deck.slides.n - 1);
				done_change (); notify_slides ();
			}
			else cmd_delete_slide ();
			invalidate (true);
			return true;
		}
		return false;
	}
	long m_top; int m_total = 0; bool m_down, m_rdown = false; int m_drag, m_drop, m_x0 = 0, m_y0 = 0; unsigned m_lastT;
	UkBarDrag m_bar;
};

// ---- the notes ----------------------------------------------------------------------------------------------------
class NotesPane : public Widget
{
public:
	Textarea *ta; int m_slide; unsigned m_len; Buf m_last;
	NotesPane (int l, int t, int w, int h) : Widget (l, t, w, h), m_slide (-1), m_len (0)
	{
		ta = new Textarea (8, 18, w - 12, h - 20, 8192);
		ta->anchor = ANCHOR_FILL;
		addChild (ta);
	}
	unsigned bgColor () override { return 0xFFFFFF; }
	void onDraw () override
	{
		canvas.clear (0xFFFFFF);
		canvas.fillRect (0, 0, width, 1, 0xC8BCB2);
		uk_rbox (canvas, width / 2 - 16, 3, 32, 3, 1, 0xC8BCB2, 0xC8BCB2);
		uk_text_l (canvas, 10, 2, 16, "Notes", 0x8A7C72);
	}
	// the slide's notes shown (another slide), or the typed text kept in it
	void sync ()
	{
		Slide *s = cur_slide (); if (!s) return;
		if (m_slide != g_cur || s != m_cur)
		{
			m_slide = g_cur; m_cur = s;
			Buf t; s->notes.text_utf8 (t);
			// (uikit's text is Latin-1: the others made near)
			Buf l; const char *u = t.str ();
			for (int i = 0; i < t.n; ) { int k; unsigned c = ss::u8_dec (u + i, t.n - i, &k); i += k > 0 ? k : 1;
				if (c < 256) l.put ((char) c); else if (c == 0x2014 || c == 0x2013 || c == 0x2212) l.put ('-'); else if (c == 0x201C || c == 0x201D) l.put ('"');
				else if (c == 0x2018 || c == 0x2019) l.put ('\''); else if (c == 0x2022) l.put ((char) 0xB7); else if (c == 0x2026) l.puts ("..."); else l.put ('?'); }
			ta->setContent (l.str ()); m_last.clear (); m_last.puts (l.str ());
			return;
		}
		if (strcmp (ta->content (), m_last.str ()))
		{
			begin_change (UK_TYPE);
			CharFmt f = cf_inherit ();
			Buf u; for (const char *p = ta->content (); *p; p++) u.putu ((unsigned char) *p);	// (Latin-1 -> UTF-8)
			s->notes.set_text (u.str (), f);
			m_last.clear (); m_last.puts (ta->content ());
			g_deck.changes++;
			notify ();
		}
	}
	void reset () { m_slide = -1; m_cur = 0; }
	Slide *m_cur = 0;
};

// ---- the status bar -------------------------------------------------------------------------------------------------
enum { VIEW_NORMAL, VIEW_SORTER };
static int g_viewMode = VIEW_NORMAL;
static bool g_showNotes = true;
static void (*g_onStatus) (int what);		// 1 notes, 2 normal, 3 sorter, 4 show from here, 5 zoom out, 6 zoom in, 7 zoom box
class StatusBar : public Widget
{
public:
	char text[160]; int zoom;
	StatusBar (int l, int t, int w) : Widget (l, t, w, 24), zoom (100) { text[0] = 0; }
	void onDraw () override
	{
		canvas.clear (C_BG);
		uk_etch_h (canvas, 0, 0, width, C_BG);
		uk_text_l (canvas, 10, 1, height - 1, text, C_TEXT);
		int x = width - 10;
		char z[8]; snprintf (z, sizeof z, "%d%%", zoom);
		x -= 14; uk_text_c (canvas, x, 1, 14, height - 1, "+", C_TEXT, 1); m_plus = x;
		x -= uk_text_w (z) + 8; uk_text_l (canvas, x, 1, height - 1, z, C_TEXT); m_zoomX = x;
		x -= 18; uk_text_c (canvas, x, 1, 14, height - 1, "-", C_TEXT, 1); m_minus = x;
		x -= 14;
		// the views: normal, sorter, show
		for (int v = 2; v >= 0; v--)
		{
			x -= 26; m_viewX[v] = x;
			bool on = (v == 0 && g_viewMode == VIEW_NORMAL) || (v == 1 && g_viewMode == VIEW_SORTER);
			if (on) uk_rbox (canvas, x, 3, 24, height - 6, 4, uk_mix (C_BG, C_ACCENT, 70), uk_mix (C_BG, C_ACCENT, 70));
			unsigned ink = C_TEXT; int cx = x + 4, cy = 6;
			if (v == 0) { canvas.frameRect (cx, cy, 16, 12, ink); canvas.fillRect (cx, cy, 4, 12, ink); }
			else if (v == 1) { for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) canvas.fillRect (cx + i * 9, cy + j * 7, 7, 5, ink); }
			else { canvas.frameRect (cx, cy, 16, 12, ink); VPath p; int t[6] = { V (cx + 6), V (cy + 3), V (cx + 11), V (cy + 6), V (cx + 6), V (cy + 9) }; p.poly (t, 3); p.fill (canvas, ink); }
		}
		x -= 12; uk_etch_v (canvas, x, 4, height - 8, C_BG);
		x -= 70; m_notesX = x;
		if (g_showNotes && g_viewMode == VIEW_NORMAL) uk_rbox (canvas, x, 3, 66, height - 6, 4, uk_mix (C_BG, C_ACCENT, 70), uk_mix (C_BG, C_ACCENT, 70));
		canvas.frameRect (x + 6, 7, 12, 11, C_TEXT); for (int k = 0; k < 3; k++) canvas.fillRect (x + 8, 9 + k * 3, 8, 1, C_TEXT);
		uk_text_l (canvas, x + 24, 1, height - 1, "Notes", C_TEXT);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (bl && in && !pressed) pressed = true;
		else if (!bl && pressed)
		{
			pressed = false;
			if (!in || !g_onStatus) return in;
			if (mx >= m_notesX && mx < m_notesX + 66) g_onStatus (1);
			else if (mx >= m_viewX[0] && mx < m_viewX[0] + 24) g_onStatus (2);
			else if (mx >= m_viewX[1] && mx < m_viewX[1] + 24) g_onStatus (3);
			else if (mx >= m_viewX[2] && mx < m_viewX[2] + 24) g_onStatus (4);
			else if (mx >= m_minus && mx < m_minus + 14) g_onStatus (5);
			else if (mx >= m_plus && mx < m_plus + 14) g_onStatus (6);
			else if (mx >= m_zoomX && mx < m_minus + 30) g_onStatus (7);
		}
		return in;
	}
	int m_notesX = 0, m_viewX[3] = { 0, 0, 0 }, m_minus = 0, m_plus = 0, m_zoomX = 0;
};

} // namespace sl

#endif
