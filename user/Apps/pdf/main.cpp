//
// Apps/pdf/main.cpp -- PDF Viewer, Onyx's reader of PDF documents (docs/pdf/README.md: the mock-ups and the user's
// decisions), in the way of Acrobat Reader / Edge / Evince: a tab a document; the pages continuous, one at a time
// or two side by side; the zoom (fit the page / the width, 50 to 400 %), turned; the side panel -- the pages'
// thumbnails, the contents (the outline), the hits of a search with their context --; the text selected and
// copied, the links followed (inside the document; the web in Jet Browser); passwords; the document's
// Properties; full screen (a presentation). With no document: the home -- the recent ones (reopened where they
// were left), the folders holding PDFs.
//
// MuPDF draws (engine.h: its documents, the worker thread rendering and searching); a newlib uikit app with
// FreeType's text (user/Makefile's pdf.elf rule: mupdf.mk). This app is AGPL-3.0 because of MuPDF
// (docs/LICENSING.md). Its files: SD:/etc/pdf/settings.ini, SD:/etc/pdf/recent.tsv.
//
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include "appkit/appkit.h"
#include "engine.h"
#include "ui.h"
#include "clipboard.h"
#include "docguard.h"
#include "printerkit/printerkit.h"

using namespace uikit;
using namespace pdfv;

#define CONF_DIR "SD:/etc/pdf"
#define SETTINGS "SD:/etc/pdf/settings.ini"
#define RECENT "SD:/etc/pdf/recent.tsv"

// ---- the layout's sizes, colours ------------------------------------------------------------------------------
enum { TABS_H = 34, TB_H = 46, SIDE_W = 224, MARGIN = 16, GAP = 14, SB_W = 12 };
static const unsigned CANVAS_BG = 0x847E7A;	// behind the pages
static unsigned col_side () { return uk_mix (C_BG, C_FIELD, 70); }
static unsigned col_dim () { return uk_mix (C_FIELD, C_FIELD_TEXT, 150); }
static unsigned col_dim_bg () { return uk_mix (C_BG, C_TEXT, 150); }

// ---- a tab: a document and how it is shown ------------------------------------------------------------------------
enum { L_SINGLE, L_SCROLL, L_TWO };
enum { Z_WIDTH, Z_PAGE, Z_CUSTOM };
enum { S_PAGES, S_CONTENTS, S_FIND };
struct Tab
{
	Doc *doc;				// 0: the home
	int layout, zoomMode; float zoom; int rot;
	int sx, sy;				// the view's scroll (px)
	int cur;				// the current page (the one most shown)
	int side, thumbScroll, olScroll, findScroll;
	// the selection (on one page): its ends in the page's points
	int selPage; float ax, ay, bx, by; bool sel;
	// the search: its hits so far (in the pages' order), the current one
	char needle[200]; int opts, gen; SearchHit *hits; int nhits, capHits, searched; bool done; int hit;
	int pendingPage; float pendingY;	// (a page to show once the layout is known: an opened recent document)
};
enum { MAX_TABS = 12 };
static Tab g_tabs[MAX_TABS]; static int g_ntabs, g_cur;
static Tab &tab () { return g_tabs[g_cur]; }
static Worker g_worker;
static int g_sideOn = 1, g_defLayout = L_SCROLL, g_defZoom = Z_WIDTH;
static unsigned g_tick;

class TabBar; class ToolBar; class SidePanel; class View; class Home; class SearchBox;
static Root *g_root;
static TabBar *g_tabbar; static ToolBar *g_tb; static SidePanel *g_side; static View *g_view; static Home *g_home;

static void refresh_all ();
static void layout_parts ();
static void open_path (const char *path, int page = -1, bool newTab = true);
static void save_settings ();
static void recent_note (Tab &t);
static void search_start (Tab &t, const char *needle);
static void goto_hit (Tab &t, int k);
static void show_page (Tab &t, int pg, float y = -1, bool top = true);

// ---- the bitmaps: the pages drawn (the worker's), kept while there is room ------------------------------------------
struct Bmp { int docId, page, skey, rot, cx, cy, cw, ch; bool thumb; unsigned *px; unsigned use; };
enum { MAX_BMP = 160 };
static Bmp g_bmp[MAX_BMP]; static int g_nbmp; static long g_bmpPx; static unsigned g_bmpClock;
static const long BMP_BUDGET = 18L << 20;	// pixels (x 4 bytes)
static inline int skey (float s) { return (int) (s * 4096 + 0.5f); }
static void bmp_drop (int i) { g_bmpPx -= (long) g_bmp[i].cw * g_bmp[i].ch; free (g_bmp[i].px); g_bmp[i] = g_bmp[--g_nbmp]; }
static void bmp_forget_doc (int docId) { for (int i = g_nbmp - 1; i >= 0; i--) if (g_bmp[i].docId == docId) bmp_drop (i); }
static void bmp_add (const Job &j, unsigned *px)
{
	while (g_nbmp && (g_nbmp >= MAX_BMP || g_bmpPx + (long) j.cw * j.ch > BMP_BUDGET))
	{
		int old = 0; for (int i = 1; i < g_nbmp; i++) if (g_bmp[i].use < g_bmp[old].use) old = i;
		bmp_drop (old);
	}
	Bmp &b = g_bmp[g_nbmp++];
	b.docId = j.docId; b.page = j.page; b.skey = skey (j.scale); b.rot = j.rot; b.cx = j.cx; b.cy = j.cy; b.cw = j.cw; b.ch = j.ch;
	b.thumb = j.kind == J_THUMB; b.px = px; b.use = ++g_bmpClock;
	g_bmpPx += (long) j.cw * j.ch;
}
// the best bitmap of a page: exactly this scale covering the part (x, y, w, h); else any of the page (scaled)
static Bmp *bmp_find (int docId, int page, float s, int rot, int x, int y, int w, int h, bool *exact, bool thumb = false)
{
	Bmp *best = 0; int k = skey (s); *exact = false;
	for (int i = 0; i < g_nbmp; i++)
	{
		Bmp &b = g_bmp[i];
		if (b.docId != docId || b.page != page || b.rot != rot) continue;
		if (b.skey == k && b.thumb == thumb && b.cx <= x && b.cy <= y && b.cx + b.cw >= x + w && b.cy + b.ch >= y + h) { b.use = ++g_bmpClock; *exact = true; return &b; }
		// a whole page of another scale (the nearest scale)
		if (b.cx == 0 && b.cy == 0 && (!best || abs (b.skey - k) < abs (best->skey - k))) best = &b;
	}
	if (best) best->use = ++g_bmpClock;
	return best;
}
// draw bitmap b (its part's top left at (bx, by) of the page's px at its own scale) onto the canvas: the page's
// rectangle there is (px, py, pw, ph) at the wanted scale; clipped to (clx, cly, clw, clh)
static void blit_page (Canvas &cv, const Bmp &b, float bs, float s, int px, int py, int clx, int cly, int clw, int clh)
{
	// the bitmap's part, in the canvas: its place at scale s
	float r = s / bs;
	int dx0 = px + (int) (b.cx * r), dy0 = py + (int) (b.cy * r);
	int dw = (int) (b.cw * r + 0.5f), dh = (int) (b.ch * r + 0.5f);
	int x0 = dx0 > clx ? dx0 : clx, y0 = dy0 > cly ? dy0 : cly;
	int x1 = dx0 + dw < clx + clw ? dx0 + dw : clx + clw, y1 = dy0 + dh < cly + clh ? dy0 + dh : cly + clh;
	if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > cv.w) x1 = cv.w; if (y1 > cv.h) y1 = cv.h;
	if (x1 <= x0 || y1 <= y0) return;
	if (dw == b.cw && dh == b.ch)
	{
		for (int y = y0; y < y1; y++) memcpy (cv.px + (size_t) y * cv.stride + x0, b.px + (size_t) (y - dy0) * b.cw + (x0 - dx0), (size_t) (x1 - x0) * 4);
		return;
	}
	int fx = (int) (65536.0f / r);
	for (int y = y0; y < y1; y++)
	{
		int sy = (int) ((y - dy0) / r); if (sy >= b.ch) sy = b.ch - 1;
		const unsigned *src = b.px + (size_t) sy * b.cw;
		unsigned *dst = cv.px + (size_t) y * cv.stride;
		int acc = (x0 - dx0) * fx;
		for (int x = x0; x < x1; x++, acc += fx) { int sx = acc >> 16; dst[x] = src[sx < b.cw ? sx : b.cw - 1]; }
	}
}

// ---- the layout of a tab's pages --------------------------------------------------------------------------------
struct PagePos { int x, y, w, h; };
static PagePos *g_pos; static int g_posCap;
static int g_layW, g_layH, g_layDoc = -1, g_layMode = -1, g_layRot = -1, g_layNp; static float g_layS = -1;
static int g_contentW, g_contentH;	// the whole layout's size
static float g_scale;			// px a point, now
static float fit_scale (Tab &t, int W, int H, int zm)
{
	Doc *D = t.doc; float mw = 1, mh = 1;
	for (int i = 0; i < D->npages; i++) { float a = D->pw[i], b = D->ph[i]; if (t.rot % 180) { float x = a; a = b; b = x; } if (a > mw) mw = a; if (b > mh) mh = b; }
	int cols = t.layout == L_TWO ? 2 : 1;
	float sw = (W - 2 * MARGIN - (cols - 1) * GAP) / (cols * mw);
	if (zm == Z_WIDTH) return sw;
	float sh = (H - 2 * MARGIN) / mh;
	return sw < sh ? sw : sh;
}
static float tab_scale (Tab &t, int W, int H)
{
	if (t.zoomMode == Z_CUSTOM) return t.zoom;
	float s = fit_scale (t, W, H, t.zoomMode);
	return s < 0.05f ? 0.05f : s;
}
static void lay_out (Tab &t, int W, int H)
{
	Doc *D = t.doc; if (!D) return;
	float s = tab_scale (t, W, H);
	if (g_layDoc == D->id && g_layW == W && g_layH == H && g_layMode == t.layout && g_layRot == t.rot && g_layS == s && g_layNp == D->npages) { g_scale = s; return; }
	g_layDoc = D->id; g_layW = W; g_layH = H; g_layMode = t.layout; g_layRot = t.rot; g_layS = s; g_layNp = D->npages; g_scale = s;
	if (g_posCap < D->npages) { g_posCap = D->npages; g_pos = (PagePos *) realloc (g_pos, sizeof (PagePos) * g_posCap); }
	int y = MARGIN, maxw = 0;
	if (t.layout == L_TWO)
	{	// the first page alone (a cover, at the right), then the pages two by two: the left one ends at the middle
		int half = 0;
		for (int i = 0; i < D->npages; i++) { int w, h; page_px (D, i, s, t.rot, &w, &h); g_pos[i] = PagePos { 0, 0, w, h }; if (w > half) half = w; }
		int cw = 2 * half + GAP + 2 * MARGIN; if (cw < W) cw = W;
		int mid = cw / 2;
		for (int i = 0; i < D->npages; )
		{
			if (i == 0) { g_pos[0].y = y; g_pos[0].x = D->npages == 1 ? (cw - g_pos[0].w) / 2 : mid + GAP / 2; y += g_pos[0].h + GAP; i = 1; continue; }
			int b = i + 1 < D->npages ? i + 1 : -1, rowH = g_pos[i].h;
			g_pos[i].y = y; g_pos[i].x = mid - GAP / 2 - g_pos[i].w;
			if (b >= 0) { g_pos[b].y = y; g_pos[b].x = mid + GAP / 2; if (g_pos[b].h > rowH) rowH = g_pos[b].h; }
			y += rowH + GAP; i = b >= 0 ? b + 1 : i + 1;
		}
		g_contentW = cw;
	}
	else
	{
		for (int i = 0; i < D->npages; i++) { int w, h; page_px (D, i, s, t.rot, &w, &h); g_pos[i] = PagePos { 0, y, w, h }; if (w > maxw) maxw = w; y += h + GAP; }
		int cw = maxw + 2 * MARGIN; if (cw < W) cw = W;
		for (int i = 0; i < D->npages; i++) g_pos[i].x = (cw - g_pos[i].w) / 2;
		g_contentW = cw;
	}
	g_contentH = y - GAP + MARGIN;
}
static void invalidate_layout () { g_layDoc = -1; }
// the scroll range of a tab (single page: that page's)
static void scroll_range (Tab &t, int W, int H, int *y0, int *y1)
{
	if (t.layout == L_SINGLE && t.doc)
	{
		const PagePos &p = g_pos[t.cur];
		*y0 = p.y - MARGIN; *y1 = p.y + p.h + MARGIN - H; if (*y1 < *y0) { int c = p.y - (H - p.h) / 2; *y0 = *y1 = c; }
		return;
	}
	*y0 = 0; *y1 = g_contentH - H; if (*y1 < 0) *y1 = 0;
	(void) W;
}
static void clamp_scroll (Tab &t, int W, int H)
{
	int y0, y1; scroll_range (t, W, H, &y0, &y1);
	if (t.sy < y0) t.sy = y0; if (t.sy > y1) t.sy = y1;
	int x1 = g_contentW - W; if (x1 < 0) x1 = 0;
	if (t.sx < 0) t.sx = 0; if (t.sx > x1) t.sx = x1;
}
// the page most shown in a view of height H at scroll sy
static int page_at (Tab &t, int H)
{
	Doc *D = t.doc; int best = t.cur, bestA = -1;
	for (int i = 0; i < D->npages; i++)
	{
		int a = g_pos[i].y > t.sy ? g_pos[i].y : t.sy, b = g_pos[i].y + g_pos[i].h < t.sy + H ? g_pos[i].y + g_pos[i].h : t.sy + H;
		if (g_pos[i].y > t.sy + H) break;
		if (b - a > bestA) { bestA = b - a; best = i; }
	}
	return best;
}


// ---- the pages' text on the window's thread (selection), the few last ------------------------------------------------
struct StCache { int docId, page; fz_stext_page *t; unsigned use; };
static StCache g_st[4]; static unsigned g_stClock;
static fz_stext_page *text_of (Doc *D, int pg)
{
	int old = 0;
	for (int i = 0; i < 4; i++)
	{
		if (g_st[i].t && g_st[i].docId == D->id && g_st[i].page == pg) { g_st[i].use = ++g_stClock; return g_st[i].t; }
		if (!g_st[i].t || g_st[i].use < g_st[old].use) old = i;
	}
	if (g_st[old].t) fz_drop_stext_page (g_mu, g_st[old].t);
	g_st[old].t = page_text (g_mu, D, pg); g_st[old].docId = D->id; g_st[old].page = pg; g_st[old].use = ++g_stClock;
	return g_st[old].t;
}
static void text_forget (int docId) { for (int i = 0; i < 4; i++) if (g_st[i].t && g_st[i].docId == docId) { fz_drop_stext_page (g_mu, g_st[i].t); g_st[i].t = 0; } }

// ---- what the worker is asked: the view's pages, their neighbours, the thumbnails, the recent documents' --------------
static Job g_wantView[24]; static int g_nwantView;
static Job g_wantSide[24]; static int g_nwantSide;
static Job g_wantHome[12]; static int g_nwantHome;
static void send_wants ()
{
	Job all[WANT_MAX]; int n = 0;
	for (int i = 0; i < g_nwantView && n < WANT_MAX; i++) all[n++] = g_wantView[i];
	for (int i = 0; i < g_nwantHome && n < WANT_MAX; i++) all[n++] = g_wantHome[i];
	for (int i = 0; i < g_nwantSide && n < WANT_MAX; i++) all[n++] = g_wantSide[i];
	g_worker.set_wants (all, n);
}
static Job page_job (Tab &t, int pg, float s, int cx, int cy, int cw, int ch, int kind = J_PAGE)
{
	Job j; memset (&j, 0, sizeof j);
	j.kind = kind; j.doc = t.doc; j.docId = t.doc->id; j.page = pg; j.scale = s; j.rot = t.rot; j.cx = cx; j.cy = cy; j.cw = cw; j.ch = ch;
	return j;
}

// ---- small drawing helpers ---------------------------------------------------------------------------------------------
static void blend_rect (Canvas &cv, int x, int y, int w, int h, unsigned c, int a)
{
	int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + w > cv.w ? cv.w : x + w, y1 = y + h > cv.h ? cv.h : y + h;
	for (int yy = y0; yy < y1; yy++) { unsigned *r = cv.px + (size_t) yy * cv.stride; for (int xx = x0; xx < x1; xx++) r[xx] = uk_over (r[xx], c, a); }
}
// a rectangle of a page's points -> the canvas (the page's top left at (px, py))
static void quad_to_px (Doc *D, int pg, float s, int rot, Quad q, int px, int py, int *x, int *y, int *w, int *h)
{
	fz_rect r = fz_transform_rect (fz_make_rect (q.x0, q.y0, q.x1, q.y1), page_ctm (D, pg, s, rot));
	*x = px + (int) r.x0; *y = py + (int) r.y0; *w = (int) (r.x1 - r.x0 + 0.99f); *h = (int) (r.y1 - r.y0 + 0.99f);
}
static fz_point px_to_page (Doc *D, int pg, float s, int rot, int x, int y)
{
	return fz_transform_point (fz_make_point ((float) x, (float) y), fz_invert_matrix (page_ctm (D, pg, s, rot)));
}
static void bubble (Canvas &cv, int cx, int y, const char *s)
{
	int w = tw (s, F_UI, 2) + 24;
	uk_rbox (cv, cx - w / 2, y, w, 26, 13, 0x3A3634, 0x3A3634);
	text_c (cv, cx - w / 2, y, w, 26, s, 0xFFFFFF, F_UI, 2);
}

// ---- the view: the pages ---------------------------------------------------------------------------------------------
enum { TILE_LIMIT = 2600 * 2600 };
static unsigned g_pillT;			// when the page's pill was shown (scrolling)
static void url_open (const char *uri);
static void context_menu (int mx, int my);
static void copy_selection ();
class View : public Widget
{
public:
	int dragMode;				// 0 none, 1 selecting, 2 the scroll bar, 3 panning (middle button), 4 a link pressed
	int pressX, pressY, pressSy, pressSx, linkPage, linkIdx;
	unsigned lastClickT; int clicks, lastCX, lastCY;
	bool wasDown, wasMid, wasRight;
	View (int l, int t, int w, int h) : Widget (l, t, w, h), dragMode (0), lastClickT (0), clicks (0), wasDown (false), wasMid (false), wasRight (false) {}
	unsigned bgColor () override { return CANVAS_BG; }
	int vw () { return width - SB_W; }
	int vh () { return height; }

	void onDraw () override
	{
		Tab &t = tab ();
		canvas.clear (CANVAS_BG);
		g_nwantView = 0;
		if (!t.doc) { send_wants (); return; }
		Doc *D = t.doc;
		int W = vw (), H = vh ();
		lay_out (t, W, H);
		if (t.pendingPage >= 0) { int p = t.pendingPage; float y = t.pendingY; t.pendingPage = -1; show_page (t, p, y); }
		clamp_scroll (t, W, H);
		float s = g_scale;
		int firstVis = -1;
		for (int i = 0; i < D->npages; i++)
		{
			const PagePos &p = g_pos[i];
			if (t.layout == L_SINGLE && i != t.cur) continue;
			int x = p.x - t.sx, y = p.y - t.sy;
			if (y > H || y + p.h < 0 || x > W || x + p.w < 0) continue;
			if (firstVis < 0) firstVis = i;
			// its shadow, the paper
			blend_rect (canvas, x + 2, y + 3, p.w, p.h, 0x000000, 60);
			canvas.fillRect (x, y, p.w, p.h, 0xFFFFFF);
			// the bitmap: the whole page, or (large) the part seen, on a 256-px grid, with a margin
			int cx = 0, cy = 0, cw = p.w, ch = p.h;
			if ((long) p.w * p.h > TILE_LIMIT)
			{
				int vx0 = (t.sx - p.x) - 128, vy0 = (t.sy - p.y) - 128, vx1 = vx0 + W + 256, vy1 = vy0 + H + 256;
				vx0 = vx0 < 0 ? 0 : vx0 & ~255; vy0 = vy0 < 0 ? 0 : vy0 & ~255;
				vx1 = vx1 > p.w ? p.w : (vx1 + 255) & ~255; vy1 = vy1 > p.h ? p.h : (vy1 + 255) & ~255;
				if (vx1 > p.w) vx1 = p.w; if (vy1 > p.h) vy1 = p.h;
				cx = vx0; cy = vy0; cw = vx1 - vx0; ch = vy1 - vy0;
			}
			bool exact; Bmp *b = bmp_find (D->id, i, s, t.rot, cx, cy, cw, ch, &exact);
			if (b) blit_page (canvas, *b, b->skey / 4096.0f, s, x, y, x > 0 ? x : 0, y > 0 ? y : 0, W, H);
			if (!exact && g_nwantView < 24) g_wantView[g_nwantView++] = page_job (t, i, s, cx, cy, cw, ch);
			// the search's hits, the selection
			for (int k = 0; k < t.nhits; k++)
			{
				if (t.hits[k].page != i) continue;
				for (int q = 0; q < t.hits[k].nq; q++)
				{
					int hx, hy, hw, hh; quad_to_px (D, i, s, t.rot, t.hits[k].q[q], x, y, &hx, &hy, &hw, &hh);
					blend_rect (canvas, hx - 1, hy - 1, hw + 2, hh + 2, k == t.hit ? 0xFF8C1E : 0xFFD640, k == t.hit ? 150 : 120);
				}
			}
			if (t.sel && t.selPage == i)
			{
				fz_stext_page *st = text_of (D, i);
				if (st)
				{
					static fz_quad qs[1024];
					int n = fz_highlight_selection (g_mu, st, fz_make_point (t.ax, t.ay), fz_make_point (t.bx, t.by), qs, 1024);
					for (int k = 0; k < n; k++)
					{
						int hx, hy, hw, hh; quad_to_px (D, i, s, t.rot, quad_box (qs[k]), x, y, &hx, &hy, &hw, &hh);
						blend_rect (canvas, hx, hy, hw, hh + 1, C_ACCENT, 90);
					}
				}
			}
		}
		// the pages near, drawn ahead (small enough)
		int near[4] = { t.cur + 1, t.cur - 1, t.cur + 2, t.cur + 3 };
		for (int k = 0; k < 4; k++)
		{
			int i = near[k]; if (i < 0 || i >= D->npages || g_nwantView >= 24) continue;
			const PagePos &p = g_pos[i]; if ((long) p.w * p.h > TILE_LIMIT) continue;
			bool exact; bmp_find (D->id, i, s, t.rot, 0, 0, p.w, p.h, &exact);
			if (!exact) g_wantView[g_nwantView++] = page_job (t, i, s, 0, 0, p.w, p.h);
		}
		send_wants ();
		// the scroll bars
		int y0, y1; scroll_range (t, W, H, &y0, &y1);
		long total = (long) (y1 - y0) + H, pos = t.sy - y0;
		canvas.fillRect (W, 0, SB_W, H, uk_mix (CANVAS_BG, 0xFFFFFF, 60));
		UkThumb th = uk_thumb (total, H, pos, H - 4);
		if (th.show) uk_rbox (canvas, W + 2, 2 + th.y, SB_W - 4, th.h, (SB_W - 4) / 2, 0xD8D2CC, 0xC8C0BA);
		if (g_contentW > W)
		{
			UkThumb hz = uk_thumb (g_contentW, W, t.sx, W - 4);
			blend_rect (canvas, 0, H - 9, W, 9, 0xFFFFFF, 60);
			if (hz.show) uk_rbox (canvas, 2 + hz.y, H - 8, hz.h, 6, 3, 0xD8D2CC, 0xC8C0BA);
		}
		// while it scrolls: the page
		if (g_tick - g_pillT < 150)
		{
			char b[64]; snprintf (b, sizeof b, "Page %d of %d", t.cur + 1, D->npages);
			bubble (canvas, W / 2, H - 46, b);
		}
	}

	// the page under (mx, my) of the view -> its index, the point in it; -1 none
	int page_under (int mx, int my, fz_point *pt)
	{
		Tab &t = tab (); Doc *D = t.doc;
		if (!D) return -1;
		for (int i = 0; i < D->npages; i++)
		{
			if (t.layout == L_SINGLE && i != t.cur) continue;
			const PagePos &p = g_pos[i];
			int x = p.x - t.sx, y = p.y - t.sy;
			if (mx >= x && my >= y && mx < x + p.w && my < y + p.h) { if (pt) *pt = px_to_page (D, i, g_scale, t.rot, mx - x, my - y); return i; }
		}
		return -1;
	}
	int link_at (int pg, fz_point pt)
	{
		Doc *D = tab ().doc;
		if (!D->links[pg].loaded) return -1;
		for (int k = 0; k < D->links[pg].n; k++) { Quad r = D->links[pg].l[k].r; if (pt.x >= r.x0 && pt.x <= r.x1 && pt.y >= r.y0 && pt.y <= r.y1) return k; }
		return -1;
	}
	void scroll_by (int dx, int dy)
	{
		Tab &t = tab (); if (!t.doc) return;
		int W = vw (), H = vh ();
		if (t.layout == L_SINGLE && dy)
		{	// one page at a time: past its end, the next one
			int y0, y1; scroll_range (t, W, H, &y0, &y1);
			if (dy > 0 && t.sy >= y1 && t.cur + 1 < t.doc->npages) { t.cur++; scroll_range (t, W, H, &y0, &y1); t.sy = y0; invalidate (true); g_pillT = g_tick; refresh_all (); return; }
			if (dy < 0 && t.sy <= y0 && t.cur > 0) { t.cur--; scroll_range (t, W, H, &y0, &y1); t.sy = y1; invalidate (true); g_pillT = g_tick; refresh_all (); return; }
		}
		t.sx += dx; t.sy += dy;
		clamp_scroll (t, W, H);
		int c = t.layout == L_SINGLE ? t.cur : page_at (t, H);
		g_pillT = g_tick;
		if (c != t.cur) { t.cur = c; refresh_all (); }
		else invalidate (true);
	}
	void zoom_at (float f, int mx, int my)
	{
		Tab &t = tab (); if (!t.doc) return;
		int W = vw (), H = vh ();
		float s0 = g_scale, s1 = s0 * f;
		if (s1 < 0.25f * 0.75f) s1 = 0.25f * 0.75f; if (s1 > 6.0f) s1 = 6.0f;
		// the point under the pointer stays there
		float fx = (t.sx + mx) / s0, fy = (t.sy + my) / s0;
		t.zoomMode = Z_CUSTOM; t.zoom = s1;
		lay_out (t, W, H);
		t.sx = (int) (fx * g_scale) - mx; t.sy = (int) (fy * g_scale) - my;
		clamp_scroll (t, W, H);
		save_settings ();
		refresh_all ();
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		Tab &t = tab ();
		if (!t.doc) return false;
		int W = vw (), H = vh ();
		unsigned mods = kapi_get_modifiers ();
		if (wheel)
		{
			if (mods & MOD_CTRL) zoom_at (wheel > 0 ? 1.15f : 1 / 1.15f, mx, my);
			else if (mods & MOD_SHIFT) scroll_by (-wheel * 60, 0);
			else scroll_by (0, -wheel * 60);
			return true;
		}
		bool down = bl && !wasDown, up = !bl && wasDown;
		bool mdown = bm && !wasMid, rdown = br && !wasRight;
		wasDown = bl; wasMid = bm; wasRight = br;
		if (rdown) { context_menu (mx, my); return true; }
		if (mdown) { dragMode = 3; pressX = mx; pressY = my; pressSx = t.sx; pressSy = t.sy; return true; }
		if (dragMode == 3) { if (!bm) dragMode = 0; else { t.sx = pressSx - (mx - pressX); t.sy = pressSy - (my - pressY); scroll_by (0, 0); } return true; }
		if (down)
		{
			pressed = true; pressX = mx; pressY = my;
			if (mx >= W)
			{	// the scroll bar: the thumb follows the pointer
				dragMode = 2; int y0, y1; scroll_range (t, W, H, &y0, &y1);
				UkThumb th = uk_thumb ((long) (y1 - y0) + H, H, t.sy - y0, H - 4);
				t.sy = y0 + (int) uk_thumb_pos (my - 2, H - 4, (long) (y1 - y0) + H, H, th.h); scroll_by (0, 0);
				return true;
			}
			fz_point pt; int pg = page_under (mx, my, &pt);
			int lk = pg >= 0 ? link_at (pg, pt) : -1;
			if (lk >= 0) { dragMode = 4; linkPage = pg; linkIdx = lk; return true; }
			// a click, a double click (a word), a triple (a line)
			if (g_tick - lastClickT < 45 && abs (mx - lastCX) < 5 && abs (my - lastCY) < 5) clicks++; else clicks = 1;
			lastClickT = g_tick; lastCX = mx; lastCY = my;
			if (pg < 0) { if (t.sel) { t.sel = false; invalidate (true); } dragMode = 0; return true; }
			if ((mods & MOD_SHIFT) && t.sel && t.selPage == pg) { t.bx = pt.x; t.by = pt.y; dragMode = 1; invalidate (true); return true; }
			t.selPage = pg; t.ax = t.bx = pt.x; t.ay = t.by = pt.y; t.sel = false; dragMode = 1;
			if (clicks >= 2)
			{
				fz_stext_page *st = text_of (t.doc, pg);
				if (st) { fz_point a = pt, b = pt; fz_snap_selection (g_mu, st, &a, &b, clicks == 2 ? FZ_SELECT_WORDS : FZ_SELECT_LINES); t.ax = a.x; t.ay = a.y; t.bx = b.x; t.by = b.y; t.sel = true; }
				dragMode = 0;
			}
			invalidate (true);
			return true;
		}
		if (bl && pressed)
		{
			if (dragMode == 2)
			{
				int y0, y1; scroll_range (t, W, H, &y0, &y1);
				UkThumb th = uk_thumb ((long) (y1 - y0) + H, H, t.sy - y0, H - 4);
				t.sy = y0 + (int) uk_thumb_pos (my - 2, H - 4, (long) (y1 - y0) + H, H, th.h); scroll_by (0, 0);
			}
			else if (dragMode == 1)
			{
				const PagePos &p = g_pos[t.selPage];
				fz_point pt = px_to_page (t.doc, t.selPage, g_scale, t.rot, mx - (p.x - t.sx), my - (p.y - t.sy));
				t.bx = pt.x; t.by = pt.y;
				if (abs (mx - pressX) > 2 || abs (my - pressY) > 2) t.sel = true;
				// at the edges: the view scrolls
				if (my < 0) scroll_by (0, -20); else if (my > H) scroll_by (0, 20); else invalidate (true);
			}
			return true;
		}
		if (up)
		{
			pressed = false;
			if (dragMode == 4)
			{
				fz_point pt; int pg = page_under (mx, my, &pt);
				if (pg == linkPage && link_at (pg, pt) == linkIdx)
				{
					LinkInfo &li = t.doc->links[pg].l[linkIdx];
					if (li.uri) url_open (li.uri);
					else if (li.page >= 0) show_page (t, li.page, li.y);
				}
			}
			dragMode = 0;
			return true;
		}
		return mx >= 0 && my >= 0 && mx < width && my < height;
	}
};

static const char *ci_find (const char *s, const char *n)
{
	int ln = (int) strlen (n); if (!ln) return 0;
	for (; *s; s++) if (!strncasecmp (s, n, ln)) return s;
	return 0;
}

// ---- the side panel: the pages, the contents, the search's hits ---------------------------------------------------------
enum { SP_TAB = 1, SP_THUMB, SP_OL_TOGGLE, SP_OL, SP_CASE, SP_WORDS, SP_HIT };
static void find_box_changed (Widget &);
class SidePanel : public Widget
{
public:
	HitList hits; int hot; Textbox *findBox; int listTop, listH, contentH;
	SidePanel (int l, int t, int w, int h) : Widget (l, t, w, h), hot (-1), listTop (0), listH (0), contentH (0)
	{
		findBox = new Textbox (10, 52, w - 20, 28, "", find_box_changed); findBox->maxLen = 190; addChild (findBox);
	}
	unsigned bgColor () override { return col_side (); }
	int &scroll () { Tab &t = tab (); return t.side == S_PAGES ? t.thumbScroll : t.side == S_CONTENTS ? t.olScroll : t.findScroll; }
	float thumb_scale (Doc *D)
	{
		float mw = 1; for (int i = 0; i < D->npages; i++) { float a = tab ().rot % 180 ? D->ph[i] : D->pw[i]; if (a > mw) mw = a; }
		return 96.0f / mw;
	}
	void segs ()
	{
		static const char *L[3] = { "Pages", "Contents", "Find" };
		int x = 10, w = width - 20, sw = w / 3;
		uk_rbox (canvas, x, 8, w, 28, 6, C_FIELD, C_FIELD); uk_rline (canvas, x, 8, w, 28, 6, 0x000000, 50);
		for (int k = 0; k < 3; k++)
		{
			bool on = tab ().side == k;
			if (on) uk_rbox (canvas, x + 2 + k * sw, 10, sw - 4, 24, 5, C_ACCENT, C_ACCENT);
			text_c (canvas, x + k * sw, 8, sw, 28, L[k], on ? C_SEL_TEXT : C_FIELD_TEXT);
			hits.add (x + k * sw, 8, sw, 28, SP_TAB, k);
		}
	}
	void onDraw () override
	{
		Tab &t = tab (); Doc *D = t.doc;
		canvas.clear (col_side ());
		canvas.fillRect (width - 1, 0, 1, height, uk_tone (C_BG, 100));
		hits.clear (); g_nwantSide = 0;
		findBox->hidden = !D || t.side != S_FIND;
		if (!D) return;
		segs ();
		if (t.side == S_PAGES) draw_pages (t, D);
		else if (t.side == S_CONTENTS) draw_contents (t, D);
		else draw_find (t, D);
		// the list's scroll bar
		if (contentH > listH)
		{
			UkThumb th = uk_thumb (contentH, listH, scroll (), listH - 4);
			uk_rbox (canvas, width - 10, listTop + 2 + th.y, 6, th.h, 3, uk_mix (col_side (), C_TEXT, 60), uk_mix (col_side (), C_TEXT, 70));
		}
	}
	void clamp () { int m = contentH - listH; if (m < 0) m = 0; if (scroll () > m) scroll () = m; if (scroll () < 0) scroll () = 0; }
	void draw_pages (Tab &t, Doc *D)
	{
		listTop = 44; listH = height - listTop;
		float s = thumb_scale (D);
		int y = listTop + 6 - t.thumbScroll, total = 6;
		for (int i = 0; i < D->npages; i++)
		{
			int w, h; page_px (D, i, s, t.rot, &w, &h);
			int x = (width - w) / 2, ih = h + 30;
			if (y + ih > listTop && y < height)
			{
				if (i == t.cur) { uk_rbox (canvas, x - 6, y - 6, w + 12, h + 12, 6, uk_mix (col_side (), C_ACCENT, 70), uk_mix (col_side (), C_ACCENT, 70)); uk_rline (canvas, x - 6, y - 6, w + 12, h + 12, 6, C_ACCENT, 255); }
				blend_rect (canvas, x + 1, y + 2, w, h, 0x000000, 50);
				canvas.fillRect (x, y, w, h, 0xFFFFFF);
				bool exact; Bmp *b = bmp_find (D->id, i, s, t.rot, 0, 0, w, h, &exact, true);
				if (b) blit_page (canvas, *b, b->skey / 4096.0f, s, x, y, 0, listTop, width, listH);
				if (!exact && g_nwantSide < 24) g_wantSide[g_nwantSide++] = page_job (t, i, s, 0, 0, w, h, J_THUMB);
				char n[16]; snprintf (n, sizeof n, "%d", i + 1);
				text_c (canvas, 0, y + h + 4, width, 18, n, i == t.cur ? C_ACCENT : col_dim_bg (), F_SMALL, i == t.cur ? 2 : 0);
				hits.add (0, y - 4, width, ih, SP_THUMB, i);
			}
			y += ih; total += ih;
		}
		contentH = total;
		// (the cover strip over the list's top: the tabs)
		canvas.fillRect (0, 0, width - 1, listTop, col_side ()); segs ();
		send_wants ();
	}
	bool ol_visible (Doc *D, int i) { for (int p = D->ol[i].parent; p >= 0; p = D->ol[p].parent) if (!D->ol[p].open) return false; return true; }
	int ol_current (Tab &t, Doc *D)
	{	// the section read: the last entry at or before the current page (shown: its first visible parent)
		int best = -1;
		for (int i = 0; i < D->nol; i++) if (D->ol[i].page >= 0 && D->ol[i].page <= t.cur) best = i;
		while (best >= 0 && !ol_visible (D, best)) best = D->ol[best].parent;
		return best;
	}
	void draw_contents (Tab &t, Doc *D)
	{
		listTop = 44; listH = height - listTop;
		if (!D->nol) { text_c (canvas, 0, listTop + 20, width, 22, "This document has no contents.", col_dim_bg ()); contentH = 0; return; }
		int cur = ol_current (t, D);
		int y = listTop + 4 - t.olScroll, total = 8;
		for (int i = 0; i < D->nol; i++)
		{
			if (!ol_visible (D, i)) continue;
			OutlineItem &o = D->ol[i];
			if (y + 26 > listTop && y < height)
			{
				int ind = 24 + o.depth * 14;		// (the title's x; its arrow before it)
				bool on = i == cur;
				if (on) uk_rbox (canvas, ind - 4, y + 1, width - ind - 6, 24, 5, C_ACCENT, C_ACCENT);
				else if (hits.n == hot) uk_rbox (canvas, ind - 4, y + 1, width - ind - 6, 24, 5, uk_mix (col_side (), C_ACCENT, 40), uk_mix (col_side (), C_ACCENT, 40));
				unsigned ink = on ? C_SEL_TEXT : C_TEXT;
				if (o.kids)
				{
					icon (canvas, o.open ? I_DOWN : I_RIGHT, ind - 19, y + 6, 14, col_dim_bg ());
					hits.add (ind - 22, y, 18, 26, SP_OL_TOGGLE, i);
				}
				char pg[16] = ""; if (o.page >= 0) snprintf (pg, sizeof pg, "%d", o.page + 1);
				int pw = tw (pg, F_SMALL) + 18;
				text_v (canvas, ind + 2, y, 26, o.title, ink, F_UI, on ? 2 : 0, width - ind - 2 - pw - 6);
				text_r (canvas, width - 16, y, 26, pg, on ? C_SEL_TEXT : col_dim_bg (), F_SMALL);
				hits.add (ind - 4, y, width - ind - 6, 26, SP_OL, i);
			}
			y += 26; total += 26;
		}
		contentH = total;
		canvas.fillRect (0, 0, width - 1, listTop, col_side ()); segs ();
	}
	void check (int x, int y, const char *label, bool on, int kind)
	{
		uk_check_mark (canvas, x, y, 16, on, hits.n == hot ? UK_HOT : UK_NORMAL);
		text_v (canvas, x + 24, y - 2, 20, label, C_TEXT);
		hits.add (x, y - 2, tw (label) + 30, 20, kind);
	}
	void draw_find (Tab &t, Doc *D)
	{
		(void) D;
		check (12, 90, "Match case", (t.opts & 1) != 0, SP_CASE);
		check (12, 114, "Whole words", (t.opts & 2) != 0, SP_WORDS);
		char sum[96] = "";
		int pages = 0, last = -1; for (int k = 0; k < t.nhits; k++) if (t.hits[k].page != last) { pages++; last = t.hits[k].page; }
		if (t.needle[0])
		{
			if (!t.done) snprintf (sum, sizeof sum, "Searching... %d result%s", t.nhits, t.nhits == 1 ? "" : "s");
			else if (!t.nhits) snprintf (sum, sizeof sum, "No results");
			else snprintf (sum, sizeof sum, "%d result%s on %d page%s", t.nhits, t.nhits == 1 ? "" : "s", pages, pages == 1 ? "" : "s");
		}
		text (canvas, 12, 138, sum, col_dim_bg (), F_SMALL, 2);
		listTop = 160; listH = height - listTop;
		int y = listTop - t.findScroll, total = 0;
		for (int k = 0; k < t.nhits; k++)
		{
			SearchHit &h = t.hits[k];
			if (k == 0 || t.hits[k - 1].page != h.page)
			{
				int n = 0; for (int j = k; j < t.nhits && t.hits[j].page == h.page; j++) n++;
				if (y + 22 > listTop && y < height)
				{
					char a[32], b[16]; snprintf (a, sizeof a, "Page %d", h.page + 1); snprintf (b, sizeof b, "%d", n);
					text_v (canvas, 12, y, 22, a, C_TEXT, F_SMALL, 2); text_r (canvas, width - 16, y, 22, b, col_dim_bg (), F_SMALL);
				}
				y += 22; total += 22;
			}
			if (y + 22 > listTop && y < height)
			{
				bool on = k == t.hit;
				if (on) uk_rbox (canvas, 6, y, width - 14, 22, 5, C_ACCENT, C_ACCENT);
				else if (hits.n == hot) uk_rbox (canvas, 6, y, width - 14, 22, 5, uk_mix (col_side (), C_ACCENT, 40), uk_mix (col_side (), C_ACCENT, 40));
				snippet (h.ctx ? h.ctx : "", t.needle, 14, y, width - 24, on);
				hits.add (6, y, width - 14, 22, SP_HIT, k);
			}
			y += 22; total += 22;
			if (k + 1 < t.nhits && t.hits[k + 1].page != h.page) { y += 4; total += 4; }
		}
		contentH = total + 8;
		canvas.fillRect (0, 0, width - 1, listTop, col_side ());
		segs ();
		check (12, 90, "Match case", (t.opts & 1) != 0, SP_CASE);
		check (12, 114, "Whole words", (t.opts & 2) != 0, SP_WORDS);
		text (canvas, 12, 138, sum, col_dim_bg (), F_SMALL, 2);
	}
	// a hit's line, the word in bold
	void snippet (const char *s, const char *needle, int x, int y, int w, bool on)
	{
		const char *p = ci_find (s, needle);
		unsigned dim = on ? C_SEL_TEXT : col_dim_bg (), ink = on ? C_SEL_TEXT : C_TEXT;
		int n = (int) strlen (needle), xe = x + w;
		char a[200];
		if (!p) { text_v (canvas, x, y, 22, s, dim, F_SMALL, 0, w); return; }
		int la = (int) (p - s); if (la > 190) la = 190;
		memcpy (a, s, la); a[la] = 0;
		text_v (canvas, x, y, 22, a, dim, F_SMALL); x += tw (a, F_SMALL);
		int lb = n < 190 ? n : 190; memcpy (a, p, lb); a[lb] = 0;
		if (x < xe) { text_v (canvas, x, y, 22, a, ink, F_SMALL, 2, xe - x); x += tw (a, F_SMALL, 2); }
		if (x < xe) text_v (canvas, x, y, 22, p + n, dim, F_SMALL, 0, xe - x);
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		Tab &t = tab ();
		if (!t.doc) return false;
		if (wheel) { scroll () -= wheel * 48; clamp (); invalidate (true); return true; }
		const Hit *ht = hits.at (mx, my);
		int nh = ht ? (int) (ht - hits.h) : -1;
		if (nh != hot) { hot = nh; invalidate (true); }
		if (bl && !pressed) pressed = true;
		else if (!bl && pressed)
		{
			pressed = false;
			if (!ht) return true;
			Doc *D = t.doc;
			switch (ht->kind)
			{
			case SP_TAB: t.side = ht->a; if (t.side == S_FIND) findBox->setFocus (); refresh_all (); break;
			case SP_THUMB: show_page (t, ht->a); break;
			case SP_OL_TOGGLE: D->ol[ht->a].open = !D->ol[ht->a].open; invalidate (true); break;
			case SP_OL: if (D->ol[ht->a].page >= 0) show_page (t, D->ol[ht->a].page, D->ol[ht->a].y); if (D->ol[ht->a].kids && !D->ol[ht->a].open) D->ol[ht->a].open = true; refresh_all (); break;
			case SP_CASE: t.opts ^= 1; { char n[200]; scopy (n, t.needle, sizeof n); t.needle[0] = 0; search_start (t, n); } break;
			case SP_WORDS: t.opts ^= 2; { char n[200]; scopy (n, t.needle, sizeof n); t.needle[0] = 0; search_start (t, n); } break;
			case SP_HIT: goto_hit (t, ht->a); break;
			}
		}
		return mx >= 0 && my >= 0 && mx < width && my < height;
	}
	// the current page / hit kept in sight in the list
	void follow ()
	{
		Tab &t = tab (); if (!t.doc) return;
		if (t.side == S_PAGES)
		{
			float s = thumb_scale (t.doc); int y = 6;
			for (int i = 0; i < t.cur; i++) { int w, h; page_px (t.doc, i, s, t.rot, &w, &h); y += h + 30; }
			int w, h; page_px (t.doc, t.cur, s, t.rot, &w, &h);
			int lh = height - 44;
			if (y < t.thumbScroll) t.thumbScroll = y - 8; else if (y + h + 30 > t.thumbScroll + lh) t.thumbScroll = y + h + 30 - lh;
		}
	}
};

// ---- the tabs' strip --------------------------------------------------------------------------------------------------
enum { TS_TAB = 1, TS_CLOSE, TS_NEW };
static void close_tab (int k);
static void new_home_tab ();
class TabBar : public Widget
{
public:
	HitList hits; int hot; bool wasMid;
	TabBar (int l, int t, int w, int h) : Widget (l, t, w, h), hot (-1), wasMid (false) {}
	unsigned bgColor () override { return uk_tone (C_BG, 112); }
	void onDraw () override
	{
		unsigned bg = uk_tone (C_BG, 112);
		canvas.clear (bg);
		hits.clear ();
		int x = 8, avail = width - 60;
		int each = g_ntabs ? avail / g_ntabs : avail; if (each > 220) each = 220;
		for (int k = 0; k < g_ntabs; k++)
		{
			Tab &t = g_tabs[k];
			const char *name = t.doc ? t.doc->name : "Home";
			int w = tw (name, F_UI, 2) + 64; if (w > each) w = each; if (w < 90) w = 90;
			bool on = k == g_cur, h = hits.n == hot || hits.n + 1 == hot;
			unsigned face = on ? C_BG : (h ? uk_tone (C_BG, 124) : uk_tone (C_BG, 118));
			uk_rbox (canvas, x, 5, w, height - 5, 7, face, face);
			canvas.fillRect (x, height - 4, w, 4, face);
			if (on) uk_rline (canvas, x, 5, w, height + 6, 7, 0x000000, 40);
			icon (canvas, t.doc ? I_PDF : I_HOME, x + 10, 12, 15, t.doc ? 0 : C_TEXT);
			text_v (canvas, x + 32, 5, height - 5, name, on ? C_TEXT : col_dim_bg (), F_UI, on ? 2 : 0, w - 32 - 26);
			hits.add (x, 5, w - 24, height - 5, TS_TAB, k);
			bool hc = hits.n == hot;
			if (hc) uk_rbox (canvas, x + w - 24, 11, 18, 18, 9, uk_tone (face, 112), uk_tone (face, 112));
			icon (canvas, I_CLOSE, x + w - 22, 13, 14, col_dim_bg ());
			hits.add (x + w - 24, 9, 22, 22, TS_CLOSE, k);
			x += w + 4;
		}
		bool hn = hits.n == hot;
		if (hn) uk_rbox (canvas, x + 4, 8, 24, 24, 12, uk_tone (bg, 120), uk_tone (bg, 120));
		icon (canvas, I_PLUS, x + 8, 12, 16, col_dim_bg ());
		hits.add (x + 2, 6, 28, 28, TS_NEW);
	}
	bool onMouse (int mx, int my, int bl, int, int bm, int) override
	{
		const Hit *ht = hits.at (mx, my);
		int nh = ht ? (int) (ht - hits.h) : -1;
		if (nh != hot) { hot = nh; invalidate (true); }
		if (bm && !wasMid && ht && (ht->kind == TS_TAB || ht->kind == TS_CLOSE)) { wasMid = true; close_tab (ht->a); return true; }
		wasMid = bm != 0;
		if (bl && !pressed) pressed = true;
		else if (!bl && pressed)
		{
			pressed = false;
			if (!ht) return true;
			if (ht->kind == TS_TAB) { if (g_cur != ht->a) { g_cur = ht->a; refresh_all (); } }
			else if (ht->kind == TS_CLOSE) close_tab (ht->a);
			else if (ht->kind == TS_NEW) new_home_tab ();
		}
		return mx >= 0 && my >= 0 && mx < width && my < height;
	}
};

// ---- the tool bar ----------------------------------------------------------------------------------------------------
enum { TB_SIDE = 1, TB_OPEN, TB_PREV, TB_NEXT, TB_ZOUT, TB_ZOOM, TB_ZIN, TB_SINGLE, TB_SCROLL, TB_TWO, TB_ROT, TB_FULL, TB_MORE };
static void page_field_enter (Widget &);
static void search_enter (Widget &);
static void zoom_menu (int x, int y);
static void more_menu (int x, int y);
static void set_layout (int l);
static void zoom_step (int dir);
static void rotate ();
static void full_screen ();
static void m_open ();
static void toggle_side ();
class SearchBox : public Textbox
{
public:
	SearchBox (int l, int t, int w, int h) : Textbox (l, t, w, h, "", search_enter) { maxLen = 190; padR = 70; }
	void onDraw () override
	{
		Textbox::onDraw ();
		unsigned dim = uk_mix (C_FIELD, C_FIELD_TEXT, 110);
		if (!text[0] && !hasFocus) { canvas.fillRect (2, 2, width - 4, height - 4, C_FIELD); text_v (canvas, 32, 0, height, "Find in the document", dim); icon (canvas, I_SEARCH, 9, (height - 16) / 2, 16, dim); }
		Tab &t = tab ();
		if (text[0] && t.doc && t.needle[0])
		{
			char b[32];
			if (t.nhits) snprintf (b, sizeof b, "%d / %d%s", t.hit + 1, t.nhits, t.done ? "" : "+"); else snprintf (b, sizeof b, t.done ? "0" : "...");
			text_r (canvas, width - 10, 0, height, b, dim, F_SMALL);
		}
	}
};
class ToolBar : public Widget
{
public:
	HitList hits; int hot; Textbox *pageBox; SearchBox *search; int zoomX;
	ToolBar (int l, int t, int w, int h) : Widget (l, t, w, h), hot (-1), zoomX (0)
	{
		pageBox = new Textbox (0, 9, 46, 28, "1", page_field_enter); pageBox->maxLen = 6; addChild (pageBox);
		search = new SearchBox (w - 280, 9, 230, 28); addChild (search);
	}
	unsigned bgColor () override { return C_BG; }
	int btn (int x, int ic, int kind, bool on = false, bool dis = false)
	{
		bool h = hits.n == hot && !dis;
		if (on) { uk_rbox (canvas, x, 7, 32, 32, 6, uk_mix (C_BG, C_ACCENT, 60), uk_mix (C_BG, C_ACCENT, 60)); uk_rline (canvas, x, 7, 32, 32, 6, C_ACCENT, 140); }
		else if (h) { uk_rbox (canvas, x, 7, 32, 32, 6, uk_tone (C_BG, 150), uk_tone (C_BG, 140)); uk_rline (canvas, x, 7, 32, 32, 6, 0x000000, 50); }
		icon (canvas, ic, x + 7, 14, 18, dis ? C_DIS : C_TEXT);
		if (!dis) hits.add (x, 7, 32, 32, kind);
		return x + 34;
	}
	int sep (int x) { canvas.fillRect (x + 4, 10, 1, 26, uk_tone (C_BG, 108)); return x + 10; }
	void onDraw () override
	{
		Tab &t = tab (); Doc *D = t.doc;
		canvas.clear (C_BG);
		canvas.fillRect (0, height - 1, width, 1, uk_tone (C_BG, 108));
		hits.clear ();
		int x = 8;
		x = btn (x, I_SIDEBAR, TB_SIDE, g_sideOn != 0);
		x = btn (x, I_OPEN, TB_OPEN);
		x = sep (x);
		x = btn (x, I_UP, TB_PREV, false, !D || t.cur <= 0);
		x = btn (x, I_DOWN, TB_NEXT, false, !D || t.cur + 1 >= D->npages);
		pageBox->left = x + 4; x += 54;
		char n[24]; snprintf (n, sizeof n, "/ %d", D ? D->npages : 0);
		text_v (canvas, x + 4, 7, 32, n, col_dim_bg ()); x += tw (n) + 14;
		x = sep (x);
		x = btn (x, I_MINUS, TB_ZOUT);
		// the zoom's drop-down
		char z[32];
		if (t.zoomMode == Z_WIDTH) scopy (z, "Fit width", sizeof z); else if (t.zoomMode == Z_PAGE) scopy (z, "Fit page", sizeof z);
		else snprintf (z, sizeof z, "%d %%", (int) (t.zoom * 72.0f / 96.0f * 100 + 0.5f));
		{
			bool h = hits.n == hot; zoomX = x + 2;
			uk_rbox (canvas, x + 2, 8, 116, 30, 5, h ? uk_tone (C_BUTTON, 150) : uk_tone (C_BUTTON, 142), uk_tone (C_BUTTON, 124));
			uk_rline (canvas, x + 2, 8, 116, 30, 5, 0x000000, 70);
			text_v (canvas, x + 12, 8, 30, z, C_BUTTON_TEXT);
			icon (canvas, I_DOWN, x + 96, 16, 14, C_BUTTON_TEXT);
			hits.add (x + 2, 8, 116, 30, TB_ZOOM);
			x += 122;
		}
		x = btn (x, I_PLUS, TB_ZIN);
		x = sep (x);
		x = btn (x, I_SINGLE, TB_SINGLE, t.layout == L_SINGLE);
		x = btn (x, I_SCROLL, TB_SCROLL, t.layout == L_SCROLL);
		x = btn (x, I_TWO, TB_TWO, t.layout == L_TWO);
		x = btn (x, I_ROTATE, TB_ROT);
		x = btn (x, I_FULL, TB_FULL);
		// at the right: the search, the menu
		int sw = width - 8 - 40 - 6 - (x + 12); if (sw > 240) sw = 240;
		search->hidden = sw < 120;
		if (!search->hidden) { search->left = width - 48 - sw; search->resizeTo (sw, 28); }
		btn (width - 42, I_MORE, TB_MORE);
		if (!pageBox->hasFocus) { char p[16]; snprintf (p, sizeof p, "%d", D ? t.cur + 1 : 0); if (strcmp (p, pageBox->text)) pageBox->setText (p); }
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		const Hit *ht = hits.at (mx, my);
		int nh = ht ? (int) (ht - hits.h) : -1;
		if (nh != hot) { hot = nh; invalidate (true); }
		if (bl && !pressed) pressed = true;
		else if (!bl && pressed)
		{
			pressed = false;
			if (!ht) return false;
			Tab &t = tab ();
			switch (ht->kind)
			{
			case TB_SIDE: toggle_side (); break;
			case TB_OPEN: m_open (); break;
			case TB_PREV: if (t.doc && t.cur > 0) show_page (t, t.cur - 1); break;
			case TB_NEXT: if (t.doc && t.cur + 1 < t.doc->npages) show_page (t, t.cur + 1); break;
			case TB_ZOUT: zoom_step (-1); break;
			case TB_ZIN: zoom_step (1); break;
			case TB_ZOOM: zoom_menu (zoomX, 40); break;
			case TB_SINGLE: set_layout (L_SINGLE); break;
			case TB_SCROLL: set_layout (L_SCROLL); break;
			case TB_TWO: set_layout (L_TWO); break;
			case TB_ROT: rotate (); break;
			case TB_FULL: full_screen (); break;
			case TB_MORE: more_menu (width - 230, 40); break;
			}
		}
		return ht != 0;
	}
};

// ---- the recent documents ---------------------------------------------------------------------------------------------
struct Recent { char path[300]; int page, npages; long long when; unsigned *px; int w, h; bool asked; };
enum { MAX_RECENT = 20 };
static Recent g_rec[MAX_RECENT]; static int g_nrec;
static long long now_stamp ()
{
	int y, mo, d, h, mi, se;
	if (!kapi_get_datetime (&y, &mo, &d, &h, &mi, &se)) return 0;
	return ((((long long) y * 100 + mo) * 100 + d) * 100 + h) * 100 + mi;
}
static void recent_load ()
{
	char *buf = file_read (RECENT, 0);
	if (!buf) return;
	for (char *line = buf, *nx; line && *line && g_nrec < MAX_RECENT; line = nx)
	{
		nx = strchr (line, '\n'); if (nx) *nx++ = 0;
		if (line[0] == '#' || !line[0]) continue;
		char *f[4] = { line, 0, 0, 0 }; int k = 1;
		for (char *p = line; *p && k < 4; p++) if (*p == '\t') { *p = 0; f[k++] = p + 1; }
		if (!f[1]) continue;
		Recent &r = g_rec[g_nrec++]; memset (&r, 0, sizeof r);
		scopy (r.path, f[0], sizeof r.path); r.page = atoi (f[1]); r.npages = f[2] ? atoi (f[2]) : 0; r.when = f[3] ? atoll (f[3]) : 0;
	}
	free (buf);
}
static void recent_save ()
{
	kapi_mkdir (CONF_DIR);
	int cap = 200 + g_nrec * 360; char *b = (char *) malloc (cap); if (!b) return;
	int o = snprintf (b, cap, "# path\tpage\tpages\twhen (YYYYMMDDhhmm) -- the PDF Viewer's recent documents\n");
	for (int i = 0; i < g_nrec; i++) o += snprintf (b + o, cap - o, "%s\t%d\t%d\t%lld\n", g_rec[i].path, g_rec[i].page, g_rec[i].npages, g_rec[i].when);
	kapi_save_file (RECENT, b, o); free (b);
}
static int recent_find (const char *p) { for (int i = 0; i < g_nrec; i++) if (!strcmp (g_rec[i].path, p)) return i; return -1; }
static void recent_note (Tab &t)
{
	if (!t.doc) return;
	int k = recent_find (t.doc->path);
	Recent r; memset (&r, 0, sizeof r);
	if (k >= 0) { r = g_rec[k]; memmove (g_rec + k, g_rec + k + 1, sizeof (Recent) * (g_nrec - k - 1)); g_nrec--; }
	else if (g_nrec == MAX_RECENT) { free (g_rec[g_nrec - 1].px); g_nrec--; }
	scopy (r.path, t.doc->path, sizeof r.path); r.page = t.cur; r.npages = t.doc->npages; r.when = now_stamp ();
	memmove (g_rec + 1, g_rec, sizeof (Recent) * g_nrec); g_rec[0] = r; g_nrec++;
	recent_save ();
}
static void recent_remove (int k)
{
	if (k < 0 || k >= g_nrec) return;
	free (g_rec[k].px);
	memmove (g_rec + k, g_rec + k + 1, sizeof (Recent) * (g_nrec - k - 1)); g_nrec--;
	recent_save ();
}
static long days_of (long long ymd)		// YYYYMMDD -> days (a count: only differences matter)
{
	int y = (int) (ymd / 10000), m = (int) (ymd / 100 % 100), d = (int) (ymd % 100);
	if (m < 3) { y--; m += 12; }
	return 365L * y + y / 4 - y / 100 + y / 400 + (153 * (m - 3) + 2) / 5 + d;
}
static void fmt_when (long long w, char *b, int cap)
{
	static const char *MO[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	if (!w) { b[0] = 0; return; }
	long long nw = now_stamp ();
	int mi = (int) (w % 100), h = (int) (w / 100 % 100), d = (int) (w / 10000 % 100), mo = (int) (w / 1000000 % 100);
	long dw = days_of (w / 10000), dn = days_of (nw / 10000);
	if (dw == dn) snprintf (b, cap, "Today, %02d:%02d", h, mi);
	else if (dn - dw == 1) snprintf (b, cap, "Yesterday");
	else snprintf (b, cap, "%d %s", d, MO[(mo + 11) % 12]);
}

// the folders shown on the home: those that are there, their PDFs counted
struct Folder { const char *name, *path; int count; bool there; };
static Folder g_folders[] = { { "Manuals", "SD:/manuals", 0, false }, { "Documents", "SD:/Documents", 0, false },
			      { "Downloads", "SD:/Downloads", 0, false }, { "Second partition", "SD1:/", 0, false } };
enum { NFOLDERS = sizeof g_folders / sizeof g_folders[0] };
static int count_pdfs (const char *dir, int depth)
{
	void *d = kapi_opendir (dir);
	if (!d) return -1;
	int n = 0, seen = 0; struct kapi_dirent e;
	while (kapi_readdir (d, &e) && seen++ < 2000)
	{
		if (e.name[0] == '.') continue;
		if (e.is_dir) { if (depth < 3) { char p[400]; int l = (int) strlen (dir); snprintf (p, sizeof p, "%s%s%s", dir, l && dir[l - 1] == '/' ? "" : "/", e.name); int k = count_pdfs (p, depth + 1); if (k > 0) n += k; } }
		else { int l = (int) strlen (e.name); if (l > 4 && !strcasecmp (e.name + l - 4, ".pdf")) n++; }
	}
	kapi_closedir (d);
	return n;
}
static void folders_count () { for (int i = 0; i < NFOLDERS; i++) { int n = count_pdfs (g_folders[i].path, 0); g_folders[i].there = n >= 0; g_folders[i].count = n > 0 ? n : 0; } }

// ---- the home ----------------------------------------------------------------------------------------------------------
enum { HM_OPEN = 1, HM_RECENT, HM_RMORE, HM_FOLDER };
class Home : public Widget
{
public:
	HitList hits; int hot;
	Home (int l, int t, int w, int h) : Widget (l, t, w, h), hot (-1) {}
	unsigned bgColor () override { return C_FIELD; }
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		hits.clear (); g_nwantHome = 0;
		int ax = 40, ay = 26;
		text (canvas, ax, ay, "Recent documents", C_FIELD_TEXT, F_H1, 2);
		{	// Open a file...
			int bw = 160, bx = width - 40 - bw; bool h = hits.n == hot;
			uk_rbox (canvas, bx, ay, bw, 36, 6, uk_tone (C_ACCENT, h ? 150 : 140), uk_tone (C_ACCENT, h ? 128 : 118));
			icon (canvas, I_OPEN, bx + 12, ay + 9, 18, 0);
			text_v (canvas, bx + 40, ay, 36, "Open a file...", 0xFFFFFF, F_UI, 2);
			hits.add (bx, ay, bw, 36, HM_OPEN);
		}
		int S = 148, G = 30, th = (int) (S * 1.414f);
		int cols = (width - 2 * ax + G) / (S + G); if (cols < 1) cols = 1;
		int y = ay + 62, shown = 0;
		if (!g_nrec) text_v (canvas, ax, y, 30, "The documents you open will be here.", col_dim (), F_MID);
		for (int k = 0; k < g_nrec && shown < cols; k++)
		{
			Recent &r = g_rec[k];
			int x = ax + shown * (S + G);
			int ph = r.px ? r.h * S / (r.w ? r.w : 1) : th; if (ph > th + 40) ph = th + 40;
			blend_rect (canvas, x + 2, y + 3, S, ph, 0x000000, 50);
			canvas.fillRect (x, y, S, ph, 0xFFFFFF);
			if (r.px)
			{
				Bmp b; memset (&b, 0, sizeof b); b.px = r.px; b.cw = r.w; b.ch = r.h;
				blit_page (canvas, b, 1.0f, (float) S / r.w, x, y, x, y, S, ph);
			}
			else if (!r.asked && g_nwantHome < 12)
			{
				Job j; memset (&j, 0, sizeof j); j.kind = J_RECENT; j.cw = S; j.page = k; scopy (j.path, r.path, sizeof j.path);
				g_wantHome[g_nwantHome++] = j;
			}
			canvas.frameRect (x, y, S, ph, uk_mix (C_FIELD, C_FIELD_TEXT, 60));
			if (r.npages > 0) canvas.fillRect (x, y + ph - 4, S * (r.page + 1) / r.npages, 4, C_ACCENT);
			bool h = hits.n == hot || hits.n + 1 == hot;
			if (h)
			{
				blend_rect (canvas, x, y, S, ph, C_ACCENT, 40); uk_rline (canvas, x - 1, y - 1, S + 2, ph + 2, 2, C_ACCENT, 255);
			}
			hits.add (x, y, S, ph + 60, HM_RECENT, k);
			if (h || hits.n == hot)
			{
				uk_rbox (canvas, x + S - 28, y + 6, 22, 22, 11, 0xFFFFFF, 0xF0F0F0);
				icon (canvas, I_MORE, x + S - 26, y + 8, 18, C_TEXT);
			}
			hits.add (x + S - 28, y + 6, 22, 22, HM_RMORE, k);
			const char *nm = r.path; for (const char *p = r.path; *p; p++) if (*p == '/' || *p == ':') nm = p + 1;
			text (canvas, x, y + ph + 10, nm, C_FIELD_TEXT, F_UI, 2, S);
			char b[64]; snprintf (b, sizeof b, r.npages ? "Page %d of %d" : "Page %d", r.page + 1, r.npages);
			text (canvas, x, y + ph + 28, b, col_dim (), F_SMALL);
			fmt_when (r.when, b, sizeof b); text (canvas, x, y + ph + 44, b, uk_mix (C_FIELD, C_FIELD_TEXT, 110), F_SMALL);
			shown++;
		}
		send_wants ();
		int fy = y + th + 92;
		if (fy + 100 < height)
		{
			text (canvas, ax, fy, "Folders", C_FIELD_TEXT, F_H2, 2);
			int fx = ax, cw = 270;
			for (int i = 0; i < NFOLDERS; i++)
			{
				if (!g_folders[i].there) continue;
				if (fx + cw > width - 20) break;
				bool h = hits.n == hot;
				uk_rbox (canvas, fx, fy + 34, cw, 56, 8, h ? uk_mix (C_FIELD, C_ACCENT, 30) : 0xFFFFFF, h ? uk_mix (C_FIELD, C_ACCENT, 30) : 0xFFFFFF);
				uk_rline (canvas, fx, fy + 34, cw, 56, 8, 0x000000, 30);
				icon (canvas, I_FOLDER, fx + 14, fy + 48, 28, 0);
				text (canvas, fx + 56, fy + 45, g_folders[i].name, C_FIELD_TEXT, F_UI, 2);
				char b[96]; snprintf (b, sizeof b, "%s  \xC2\xB7  %d document%s", g_folders[i].path, g_folders[i].count, g_folders[i].count == 1 ? "" : "s");
				text (canvas, fx + 56, fy + 65, b, col_dim (), F_SMALL, 0, cw - 66);
				hits.add (fx, fy + 34, cw, 56, HM_FOLDER, i);
				fx += cw + 20;
			}
		}
		text_c (canvas, 0, height - 50, width, 20, "Or drop a PDF file here from the File Viewer.", uk_mix (C_FIELD, C_FIELD_TEXT, 110));
	}
	bool onMouse (int mx, int my, int bl, int br, int, int) override
	{
		const Hit *ht = hits.at (mx, my);
		int nh = ht ? (int) (ht - hits.h) : -1;
		if (nh != hot) { hot = nh; invalidate (true); }
		if (br && ht && (ht->kind == HM_RECENT || ht->kind == HM_RMORE)) { recent_menu (ht->a, mx, my); return true; }
		if (bl && !pressed) pressed = true;
		else if (!bl && pressed)
		{
			pressed = false;
			if (!ht) return true;
			if (ht->kind == HM_OPEN) m_open ();
			else if (ht->kind == HM_RMORE) recent_menu (ht->a, mx, my);
			else if (ht->kind == HM_RECENT)
			{
				Recent &r = g_rec[ht->a];
				if (!file_exists (r.path)) { uk_messagebox ("PDF Viewer", "This document is no longer there (moved or deleted). It is taken off the list.", MB_OK); recent_remove (ht->a); invalidate (true); return true; }
				char p[300]; scopy (p, r.path, sizeof p);
				open_path (p, r.page, false);
			}
			else if (ht->kind == HM_FOLDER)
			{
				char p[300];
				if (uk_file_open (p, sizeof p, g_folders[ht->a].path)) open_path (p, -1, false);
			}
		}
		return mx >= 0 && my >= 0 && mx < width && my < height;
	}
	void recent_menu (int k, int mx, int my)
	{
		PopupMenu m (left + mx, top + my);
		m.add ("Open", 1); m.add ("Open in a New Tab", 2); m.separator (); m.add ("Remove from the List", 3);
		int r = m.run ();
		if (r == 1 || r == 2) { char p[300]; scopy (p, g_rec[k].path, sizeof p); open_path (p, g_rec[k].page, r == 2); }
		else if (r == 3) { recent_remove (k); invalidate (true); }
	}
};

// ---- the dialogs ---------------------------------------------------------------------------------------------------------
static void dlg_btn (Widget &w) { if (w.parent) ((Modal *) w.parent)->onButton (w.tag); }
static void dlg_enter (Widget &w) { if (w.parent) ((Modal *) w.parent)->onButton (1); }
static void centre (Modal *m) { Root *r = Root::current (); if (!r) return; m->left = (r->width - m->width) / 2; m->top = (r->height - m->height) / 2; }
class AskBox : public Modal
{
	const char *m_title; char m_msg[300], m_err[120];
public:
	Textbox *tb;
	AskBox (const char *title, const char *msg, const char *init, bool password, const char *ok) : Modal (440, 170), m_title (title)
	{
		centre (this); scopy (m_msg, msg, sizeof m_msg); m_err[0] = 0;
		tb = new Textbox (16, titleH () + 44, width - 32, 28, init, dlg_enter);
		tb->maxLen = 120; tb->password = password; tb->setText (init); tb->caret = (int) strlen (init); tb->hasFocus = true; addChild (tb);
		Button *b = new Button (width - 196, height - 44, 86, 30, ok, dlg_btn); b->tag = 1; addChild (b);
		b = new Button (width - 102, height - 44, 86, 30, "Cancel", dlg_btn); b->tag = 0; addChild (b);
	}
	void error (const char *e) { scopy (m_err, e, sizeof m_err); tb->setText (""); tb->caret = 0; invalidate (true); }
	void onButton (int tag) override { close (tag); }
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }
	void onDraw () override
	{
		drawBox (m_title);
		text (canvas, 16, titleH () + 14, m_msg, C_TEXT, F_UI, 0, width - 32);
		if (m_err[0]) text (canvas, 16, height - 38, m_err, 0xC03C30, F_UI, 2, width - 230);
	}
};

// Properties: Description, Fonts, Security
class PropsBox : public Modal
{
	enum { MAXR = 22, MAXF = 64 };
	char m_k[MAXR][32], m_v[MAXR][160]; bool m_sepAfter[MAXR]; int m_n;
	FontInfo m_f[MAXF]; int m_nf;
	char m_sec[8][2][120]; int m_ns;
	int m_page, m_scroll;
public:
	PropsBox (Tab &t) : Modal (520, 500), m_n (0), m_nf (0), m_ns (0), m_page (0), m_scroll (0)
	{
		centre (this);
		Doc *D = t.doc; char b[200], c[200];
		const char *loc = D->path; char where[300]; scopy (where, loc, sizeof where);
		{ char *e = where; for (char *p = where; *p; p++) if (*p == '/') e = p; if (e != where) *e = 0; }
		add ("File", D->name); add ("Location", where);
		long long sz = file_size (D->path); if (sz < 0) sz = 0;
		if (sz >= 1048576) snprintf (b, sizeof b, "%.1f MB (%lld bytes)", sz / 1048576.0, sz); else snprintf (b, sizeof b, "%.0f KB (%lld bytes)", sz / 1024.0, sz);
		add ("Size", b, true);
		doc_meta (g_mu, D, FZ_META_INFO_TITLE, b, sizeof b); add ("Title", b[0] ? b : "\xE2\x80\x94");
		doc_meta (g_mu, D, FZ_META_INFO_AUTHOR, b, sizeof b); add ("Author", b[0] ? b : "\xE2\x80\x94");
		doc_meta (g_mu, D, FZ_META_INFO_SUBJECT, b, sizeof b); add ("Subject", b[0] ? b : "\xE2\x80\x94");
		doc_meta (g_mu, D, FZ_META_INFO_KEYWORDS, b, sizeof b); add ("Keywords", b[0] ? b : "\xE2\x80\x94", true);
		doc_meta (g_mu, D, FZ_META_INFO_CREATIONDATE, b, sizeof b); pdf_date (b, c, sizeof c); add ("Created", c[0] ? c : "\xE2\x80\x94");
		doc_meta (g_mu, D, FZ_META_INFO_MODIFICATIONDATE, b, sizeof b); pdf_date (b, c, sizeof c); add ("Modified", c[0] ? c : "\xE2\x80\x94");
		doc_meta (g_mu, D, FZ_META_INFO_CREATOR, b, sizeof b); app_name (b); add ("Application", b[0] ? b : "\xE2\x80\x94");
		doc_meta (g_mu, D, FZ_META_INFO_PRODUCER, b, sizeof b); add ("PDF producer", b[0] ? b : "\xE2\x80\x94", true);
		doc_meta (g_mu, D, FZ_META_FORMAT, b, sizeof b); add ("PDF version", strncmp (b, "PDF ", 4) ? b : b + 4);
		{
			float w = D->pw[0] / 72 * 25.4f, h = D->ph[0] / 72 * 25.4f;
			const char *nm = "";
			if (fabsf (w - 210) < 2 && fabsf (h - 297) < 2) nm = "A4, "; else if (fabsf (w - 216) < 2 && fabsf (h - 279) < 2) nm = "Letter, ";
			else if (fabsf (w - 148) < 2 && fabsf (h - 210) < 2) nm = "A5, "; else if (fabsf (w - 297) < 2 && fabsf (h - 420) < 2) nm = "A3, ";
			snprintf (b, sizeof b, "%d  \xC2\xB7  %s%.0f \xC3\x97 %.0f mm", D->npages, nm, w, h); add ("Pages", b);
		}
		m_nf = doc_fonts (g_mu, D, m_f, MAXF);
		doc_meta (g_mu, D, FZ_META_ENCRYPTION, b, sizeof b);
		sec ("Encryption", b[0] && strcmp (b, "None") ? b : "None");
		sec ("Printing", doc_allows (g_mu, D, FZ_PERMISSION_PRINT) ? "Allowed" : "Not allowed");
		sec ("Copying text and images", doc_allows (g_mu, D, FZ_PERMISSION_COPY) ? "Allowed" : "Not allowed");
		sec ("Changing the document", doc_allows (g_mu, D, FZ_PERMISSION_EDIT) ? "Allowed" : "Not allowed");
		sec ("Comments, filling forms", doc_allows (g_mu, D, FZ_PERMISSION_ANNOTATE) ? "Allowed" : "Not allowed");
		Button *bt = new Button (width - 106, height - 46, 90, 32, "Close", dlg_btn); bt->tag = 1; addChild (bt);
	}
	static void app_name (char *b)
	{	// "Mozilla/5.0 (...) HeadlessChrome/141.0.0.0 Safari/537.36" -> "HeadlessChrome 141"
		char *p = strstr (b, "HeadlessChrome/"); if (!p) p = strstr (b, "Chrome/");
		if (p && strstr (b, "Mozilla/")) { char t[64]; int i = 0; while (p[i] && p[i] != '/' && i < 40) { t[i] = p[i]; i++; } t[i++] = ' '; const char *v = p + (i); int k = 0; while (v[k] && v[k] != '.' && k < 8) t[i++] = v[k++]; t[i] = 0; strcpy (b, t); }
	}
	static void pdf_date (const char *s, char *out, int cap)
	{	// "D:20260929223430Z" -> "29 September 2026, 22:34"
		static const char *MO[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
		out[0] = 0;
		if (!strncmp (s, "D:", 2)) s += 2;
		int n = (int) strlen (s);
		if (n < 8) { scopy (out, s, cap); return; }
		int y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0'), mo = (s[4] - '0') * 10 + (s[5] - '0'), d = (s[6] - '0') * 10 + (s[7] - '0');
		if (mo < 1 || mo > 12) { scopy (out, s, cap); return; }
		if (n >= 12) snprintf (out, cap, "%d %s %d, %c%c:%c%c", d, MO[mo - 1], y, s[8], s[9], s[10], s[11]);
		else snprintf (out, cap, "%d %s %d", d, MO[mo - 1], y);
	}
	void add (const char *k, const char *v, bool sepAfter = false)
	{
		if (m_n >= MAXR) return;
		scopy (m_k[m_n], k, 32); scopy (m_v[m_n], v, 160); m_sepAfter[m_n] = sepAfter; m_n++;
	}
	void sec (const char *k, const char *v) { if (m_ns < 8) { scopy (m_sec[m_ns][0], k, 120); scopy (m_sec[m_ns][1], v, 120); m_ns++; } }
	void onButton (int tag) override { close (tag); }
	bool onKey (long k) override { if (k == 27 || k == KEY_ENTER) { close (1); return true; } return false; }
	void onDraw () override
	{
		drawBox ("Document Properties");
		static const char *T[3] = { "Description", "Fonts", "Security" };
		int x = 16, y = titleH () + 14;
		for (int k = 0; k < 3; k++)
		{
			int w = tw (T[k]) + 28; bool on = k == m_page;
			if (on) uk_rbox (canvas, x, y, w, 28, 6, C_ACCENT, C_ACCENT); else { uk_rbox (canvas, x, y, w, 28, 6, C_FIELD, C_FIELD); uk_rline (canvas, x, y, w, 28, 6, 0x000000, 50); }
			text_c (canvas, x, y, w, 28, T[k], on ? C_SEL_TEXT : C_FIELD_TEXT, F_UI, on ? 2 : 0);
			x += w + 4;
		}
		y += 44;
		unsigned dim = uk_mix (C_BG, C_TEXT, 150);
		if (m_page == 0)
			for (int i = 0; i < m_n; i++)
			{
				text_v (canvas, 24, y, 22, m_k[i], dim); text_v (canvas, 160, y, 22, m_v[i], C_TEXT, F_UI, !strcmp (m_k[i], "Title") ? 2 : 0, width - 176);
				y += 22;
				if (m_sepAfter[i]) { canvas.fillRect (16, y + 4, width - 32, 1, uk_tone (C_BG, 112)); y += 10; }
			}
		else if (m_page == 1)
		{
			if (!m_nf) text_v (canvas, 24, y, 22, "No fonts (or not a PDF).", dim);
			for (int i = m_scroll; i < m_nf && y < height - 70; i++)
			{
				text_v (canvas, 24, y, 20, m_f[i].name, C_TEXT, F_UI, 2, width - 48);
				char b[96]; snprintf (b, sizeof b, "%s  \xC2\xB7  %s", m_f[i].type, m_f[i].embedded ? (m_f[i].subset ? "Embedded subset" : "Embedded") : "Not embedded");
				text_v (canvas, 24, y + 19, 18, b, dim, F_SMALL);
				y += 42;
			}
		}
		else
			for (int i = 0; i < m_ns; i++) { text_v (canvas, 24, y, 24, m_sec[i][0], dim); text_v (canvas, 230, y, 24, m_sec[i][1], C_TEXT); y += 26; }
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel && m_page == 1) { m_scroll -= wheel; if (m_scroll > m_nf - 1) m_scroll = m_nf - 1; if (m_scroll < 0) m_scroll = 0; invalidate (true); return true; }
		if (bl && my >= titleH () + 14 && my < titleH () + 42)
		{
			static const char *T[3] = { "Description", "Fonts", "Security" };
			int x = 16; for (int k = 0; k < 3; k++) { int w = tw (T[k]) + 28; if (mx >= x && mx < x + w) { m_page = k; m_scroll = 0; invalidate (true); } x += w + 4; }
			return true;
		}
		return false;
	}
};

// ---- opening, closing ------------------------------------------------------------------------------------------------------
static void tab_init (Tab &t, Doc *D)
{
	memset (&t, 0, sizeof t);
	t.doc = D; t.layout = g_defLayout; t.zoomMode = g_defZoom; t.zoom = 96.0f / 72.0f; t.side = S_PAGES; t.hit = -1; t.pendingPage = -1; t.selPage = -1;
}
static int tab_of_doc (Doc *D) { for (int k = 0; k < g_ntabs; k++) if (g_tabs[k].doc == D) return k; return -1; }
static void open_path (const char *path, int page, bool newTab)
{
	for (int k = 0; k < g_ntabs; k++)
		if (g_tabs[k].doc && !strcmp (g_tabs[k].doc->path, path)) { g_cur = k; if (page >= 0) show_page (tab (), page); refresh_all (); return; }
	bool needPw; char err[200];
	Doc *D = doc_open (g_mu, path, &needPw, err, sizeof err);
	const char *nm = path; for (const char *p = path; *p; p++) if (*p == '/' || *p == ':') nm = p + 1;
	if (!D)
	{
		char m[400]; snprintf (m, sizeof m, "\xE2\x80\x9C%s\xE2\x80\x9D cannot be opened.\n%s", nm, err[0] ? err : "It is not a PDF document, or it is damaged.");
		uk_messagebox ("PDF Viewer", m, MB_OK); return;
	}
	if (needPw)
	{
		char m[300]; snprintf (m, sizeof m, "\xE2\x80\x9C%s\xE2\x80\x9D is protected. Its password:", nm);
		AskBox b ("Password", m, "", true, "Open");
		bool ok = false;
		while (b.run ())
		{
			if (doc_auth (g_mu, D, b.tb->text)) { ok = true; break; }
			b.error ("That is not the password.");
		}
		if (!ok) { doc_free (g_mu, D); return; }
	}
	if (!doc_load (g_mu, D, err, sizeof err))
	{
		char m[400]; snprintf (m, sizeof m, "\xE2\x80\x9C%s\xE2\x80\x9D cannot be read: %s", nm, err);
		uk_messagebox ("PDF Viewer", m, MB_OK); doc_free (g_mu, D); return;
	}
	int k;
	if (!newTab && !tab ().doc) k = g_cur;				// the home: replaced
	else if (g_ntabs < MAX_TABS) k = g_ntabs++;
	else { uk_messagebox ("PDF Viewer", "Too many documents are open: close one first.", MB_OK); doc_free (g_mu, D); return; }
	tab_init (g_tabs[k], D);
	if (page < 0) { int r = recent_find (D->path); if (r >= 0) page = g_rec[r].page; }
	if (page > 0 && page < D->npages) { g_tabs[k].pendingPage = page; g_tabs[k].pendingY = -1; g_tabs[k].cur = page; }
	g_cur = k;
	invalidate_layout ();
	recent_note (g_tabs[k]);
	refresh_all ();
}
static void new_home_tab ()
{
	for (int k = 0; k < g_ntabs; k++) if (!g_tabs[k].doc) { g_cur = k; refresh_all (); return; }
	if (g_ntabs >= MAX_TABS) return;
	tab_init (g_tabs[g_ntabs], 0); g_cur = g_ntabs++;
	folders_count ();
	refresh_all ();
}
static void close_tab (int k)
{
	if (k < 0 || k >= g_ntabs) return;
	Tab &t = g_tabs[k];
	if (t.doc)
	{
		recent_note (t);
		g_worker.forget (t.doc);
		bmp_forget_doc (t.doc->id); text_forget (t.doc->id);
		for (int i = 0; i < t.nhits; i++) free (t.hits[i].ctx);
		free (t.hits);
		doc_free (g_mu, t.doc);
	}
	memmove (g_tabs + k, g_tabs + k + 1, sizeof (Tab) * (g_ntabs - k - 1)); g_ntabs--;
	if (!g_ntabs) { tab_init (g_tabs[0], 0); g_ntabs = 1; folders_count (); }
	if (g_cur >= g_ntabs) g_cur = g_ntabs - 1; else if (g_cur > k) g_cur--;
	invalidate_layout ();
	refresh_all ();
}

// ---- moving about ----------------------------------------------------------------------------------------------------------
static int view_w () { return g_view ? g_view->vw () : 800; }
static int view_h () { return g_view ? g_view->vh () : 600; }
static void show_page (Tab &t, int pg, float y, bool top)
{
	if (!t.doc || pg < 0 || pg >= t.doc->npages) return;
	(void) top;
	int W = view_w (), H = view_h ();
	lay_out (t, W, H);
	t.cur = pg;
	int yy = g_pos[pg].y - MARGIN / 2;
	if (y > 0 && t.rot == 0) yy = g_pos[pg].y + (int) (y * g_scale) - 24;
	t.sy = yy;
	if (t.layout == L_SINGLE) { int y0, y1; scroll_range (t, W, H, &y0, &y1); t.sy = y > 0 ? yy : y0; }
	clamp_scroll (t, W, H);
	g_pillT = g_tick;
	if (g_side) g_side->follow ();
	refresh_all ();
}
static void set_layout (int l)
{
	Tab &t = tab (); if (!t.doc) return;
	int pg = t.cur; t.layout = l; g_defLayout = l; save_settings ();
	invalidate_layout (); show_page (t, pg);
}
static const float ZOOMS[] = { 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 4.0f };	// (x 96/72: the "100 %" is the page's real size)
static void set_zoom (int mode, float z)
{
	Tab &t = tab (); if (!t.doc) return;
	int pg = t.cur; t.zoomMode = mode; if (mode == Z_CUSTOM) t.zoom = z;
	g_defZoom = mode == Z_CUSTOM ? Z_WIDTH : mode; save_settings ();
	invalidate_layout (); show_page (t, pg);
}
static void zoom_step (int dir)
{
	Tab &t = tab (); if (!t.doc) return;
	float cur = g_scale * 72.0f / 96.0f, nz = cur;
	if (dir > 0) { nz = 4.0f; for (unsigned i = 0; i < sizeof ZOOMS / sizeof ZOOMS[0]; i++) if (ZOOMS[i] > cur + 0.01f) { nz = ZOOMS[i]; break; } }
	else { nz = 0.5f; for (int i = (int) (sizeof ZOOMS / sizeof ZOOMS[0]) - 1; i >= 0; i--) if (ZOOMS[i] < cur - 0.01f) { nz = ZOOMS[i]; break; } }
	set_zoom (Z_CUSTOM, nz * 96.0f / 72.0f);
}
static void rotate () { Tab &t = tab (); if (!t.doc) return; int pg = t.cur; t.rot = (t.rot + 90) % 360; invalidate_layout (); show_page (t, pg); }
static void toggle_side () { g_sideOn = !g_sideOn; save_settings (); invalidate_layout (); refresh_all (); }
static void zoom_menu (int x, int y)
{
	Tab &t = tab (); if (!t.doc) return;
	PopupMenu m (g_tb->left + x, g_tb->top + y);
	m.add (t.zoomMode == Z_PAGE ? "Fit page  \xE2\x9C\x93" : "Fit page", 1, true, "Ctrl+0");
	m.add (t.zoomMode == Z_WIDTH ? "Fit width  \xE2\x9C\x93" : "Fit width", 2);
	m.add ("Actual size", 3);
	m.separator ();
	static char lab[8][24];
	for (int i = 0; i < 8; i++)
	{
		bool on = t.zoomMode == Z_CUSTOM && fabsf (t.zoom - ZOOMS[i] * 96.0f / 72.0f) < 0.01f;
		snprintf (lab[i], sizeof lab[i], on ? "%d %%  \xE2\x9C\x93" : "%d %%", (int) (ZOOMS[i] * 100)); m.add (lab[i], 10 + i);
	}
	m.separator ();
	m.add ("Zoom in", 4, true, "Ctrl++"); m.add ("Zoom out", 5, true, "Ctrl+-");
	int r = m.run ();
	if (r == 1) set_zoom (Z_PAGE, 0); else if (r == 2) set_zoom (Z_WIDTH, 0); else if (r == 3) set_zoom (Z_CUSTOM, 96.0f / 72.0f);
	else if (r == 4) zoom_step (1); else if (r == 5) zoom_step (-1);
	else if (r >= 10) set_zoom (Z_CUSTOM, ZOOMS[r - 10] * 96.0f / 72.0f);
}

// ---- the search ----------------------------------------------------------------------------------------------------------------
static void search_clear (Tab &t)
{
	for (int i = 0; i < t.nhits; i++) free (t.hits[i].ctx);
	free (t.hits); t.hits = 0; t.nhits = t.capHits = 0; t.hit = -1; t.done = false; t.searched = 0;
}
static void search_start (Tab &t, const char *needle)
{
	if (!t.doc || !strcmp (needle, t.needle)) return;
	search_clear (t);
	scopy (t.needle, needle, sizeof t.needle);
	t.gen++;
	if (!needle[0]) { g_worker.stop_search (); t.done = true; refresh_all (); return; }
	// whole words: a regular expression; else the words as they are (the case ignored unless asked)
	char q[440]; int opts = (t.opts & 1) ? 0 : FZ_SEARCH_IGNORE_CASE;
	if (t.opts & 2)
	{
		int o = 0; o += snprintf (q, sizeof q, "\\b");
		for (const char *p = needle; *p && o < (int) sizeof q - 8; p++) { if (strchr ("\\^$.|?*+()[]{}", *p)) q[o++] = '\\'; q[o++] = *p; }
		snprintf (q + o, sizeof q - o, "\\b"); opts |= FZ_SEARCH_REGEXP;
	}
	else scopy (q, needle, sizeof q);
	g_worker.start_search (t.doc, q, opts, t.gen);
	refresh_all ();
}
static void on_hits (void *p, long)
{
	HitPack *h = (HitPack *) p;
	int k = tab_of_doc (h->doc);
	if (k >= 0 && g_tabs[k].gen == h->gen)
	{
		Tab &t = g_tabs[k];
		if (t.nhits + h->n > t.capHits) { t.capHits = (t.nhits + h->n) * 2 + 16; t.hits = (SearchHit *) realloc (t.hits, sizeof (SearchHit) * t.capHits); }
		memcpy (t.hits + t.nhits, h->h, sizeof (SearchHit) * h->n);
		int was = t.nhits; t.nhits += h->n; t.searched = h->page + 1; if (h->last) t.done = true;
		if (t.hit < 0) for (int i = was; i < t.nhits; i++) if (t.hits[i].page >= t.cur) { if (k == g_cur) goto_hit (t, i); else t.hit = i; break; }
		if (t.done && t.hit < 0 && t.nhits) { if (k == g_cur) goto_hit (t, 0); else t.hit = 0; }
		if (k == g_cur) refresh_all ();
	}
	else for (int i = 0; i < h->n; i++) free (h->h[i].ctx);
	free (h->h); free (h);
}
static void goto_hit (Tab &t, int k)
{
	if (!t.doc || k < 0 || k >= t.nhits) return;
	t.hit = k;
	SearchHit &h = t.hits[k];
	int W = view_w (), H = view_h ();
	lay_out (t, W, H);
	if (t.layout == L_SINGLE) t.cur = h.page;
	const PagePos &p = g_pos[h.page];
	int hx, hy, hw, hh; quad_to_px (t.doc, h.page, g_scale, t.rot, h.q[0], p.x, p.y, &hx, &hy, &hw, &hh);
	if (hy < t.sy + 20 || hy + hh > t.sy + H - 20) t.sy = hy - H / 3;
	if (hx < t.sx || hx + hw > t.sx + W) t.sx = hx - W / 3;
	clamp_scroll (t, W, H);
	t.cur = t.layout == L_SINGLE ? h.page : page_at (t, H);
	// the side panel's list: the hit in sight
	int y = 0; for (int i = 0; i <= k; i++) { if (i == 0 || t.hits[i - 1].page != t.hits[i].page) y += 22 + (i ? 4 : 0); if (i < k) y += 22; }
	int lh = g_side ? g_side->height - 160 : 300;
	if (y < t.findScroll) t.findScroll = y - 30; else if (y + 22 > t.findScroll + lh) t.findScroll = y + 22 - lh + 30;
	if (t.findScroll < 0) t.findScroll = 0;
	refresh_all ();
}
static void find_next (int dir)
{
	Tab &t = tab (); if (!t.doc || !t.nhits) return;
	int k = t.hit + dir;
	if (k >= t.nhits) k = 0; if (k < 0) k = t.nhits - 1;
	goto_hit (t, k);
}
static unsigned g_queryT;
static void search_enter (Widget &)
{
	Tab &t = tab ();
	if (strcmp (g_tb->search->text, t.needle)) { search_start (t, g_tb->search->text); return; }
	find_next ((kapi_get_modifiers () & MOD_SHIFT) ? -1 : 1);
}
static void find_box_changed (Widget &w) { (void) w; search_enter (*g_tb->search); }
static void page_field_enter (Widget &w)
{
	Tab &t = tab (); if (!t.doc) return;
	int n = atoi (((Textbox &) w).text);
	if (n >= 1 && n <= t.doc->npages) show_page (t, n - 1);
	g_view->setFocus ();
	refresh_all ();
}

// ---- the selection, the links ------------------------------------------------------------------------------------------------
static char *selection_text ()
{
	Tab &t = tab (); if (!t.doc || !t.sel) return 0;
	fz_stext_page *st = text_of (t.doc, t.selPage); if (!st) return 0;
	char *s = 0;
	fz_try (g_mu) s = fz_copy_selection (g_mu, st, fz_make_point (t.ax, t.ay), fz_make_point (t.bx, t.by), 0);
	fz_catch (g_mu) s = 0;
	return s;
}
static void copy_selection ()
{
	char *s = selection_text ();
	if (!s) return;
	if (!doc_allows (g_mu, tab ().doc, FZ_PERMISSION_COPY)) { uk_messagebox ("PDF Viewer", "This document does not allow copying its text.", MB_OK); fz_free (g_mu, s); return; }
	clip_set_text (s); fz_free (g_mu, s);
}
static void select_all ()
{
	Tab &t = tab (); if (!t.doc) return;
	t.selPage = t.cur; t.ax = -1e4f; t.ay = -1e4f; t.bx = 1e5f; t.by = 1e5f; t.sel = true; refresh_all ();
}
static void url_open (const char *uri)
{
	if (!strncmp (uri, "http://", 7) || !strncmp (uri, "https://", 8)) { kapi_exec ("SD:apps/jet.app/main", uri); return; }
	if (!strncmp (uri, "file:", 5) || strstr (uri, ".pdf")) { const char *p = uri; if (!strncmp (p, "file://", 7)) p += 7; open_path (p); return; }
	char m[400]; snprintf (m, sizeof m, "This link (%.300s) cannot be opened on Onyx.", uri);
	uk_messagebox ("PDF Viewer", m, MB_OK);
}
static void look_up (const char *s)
{
	char u[600]; int o = snprintf (u, sizeof u, "https://duckduckgo.com/?q=");
	for (const unsigned char *p = (const unsigned char *) s; *p && o < (int) sizeof u - 4; p++)
	{
		if (isalnum (*p) || *p == '-' || *p == '.' || *p == '_') u[o++] = (char) *p;
		else if (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') u[o++] = '+';
		else o += snprintf (u + o, sizeof u - o, "%%%02X", *p);
	}
	u[o] = 0;
	kapi_exec ("SD:apps/jet.app/main", u);
}
static void context_menu (int mx, int my)
{
	Tab &t = tab (); if (!t.doc) return;
	char *s = selection_text ();
	char lab[80] = "Find the Selection";
	if (s) { char w[40]; int n = 0; for (const char *p = s; *p && n < 24; p++) w[n++] = (*p == '\n' || *p == '\r') ? ' ' : *p; w[n] = 0; snprintf (lab, sizeof lab, "Find \xE2\x80\x9C%s%s\xE2\x80\x9D", w, strlen (s) > 24 ? "\xE2\x80\xA6" : ""); }
	PopupMenu m (g_view->left + mx, g_view->top + my);
	m.add ("Copy", 1, s != 0, "Ctrl+C"); m.add ("Select All", 2, true, "Ctrl+A");
	m.separator ();
	m.add (lab, 3, s != 0, "Ctrl+F"); m.add ("Look Up in Jet Browser", 4, s != 0);
	int r = m.run ();
	if (r == 1) copy_selection ();
	else if (r == 2) select_all ();
	else if ((r == 3 || r == 4) && s)
	{
		char q[200]; int n = 0; for (const char *p = s; *p && n < 190; p++) q[n++] = (*p == '\n' || *p == '\r') ? ' ' : *p; q[n] = 0;
		while (n && q[n - 1] == ' ') q[--n] = 0;
		if (r == 3) { g_tb->search->setText (q); g_side->findBox->setText (q); t.side = S_FIND; if (!g_sideOn) toggle_side (); search_start (t, q); }
		else look_up (q);
	}
	if (s) fz_free (g_mu, s);
}

// ---- the menu of the tool bar's ⋯, the files -----------------------------------------------------------------------------------
static void save_copy ()
{
	Tab &t = tab (); if (!t.doc) return;
	char out[300];
	if (!uk_file_save (out, sizeof out, "SD:/Documents", t.doc->name)) return;
	void *a = kapi_open (t.doc->path); void *b = a ? kapi_file_out (out, 0) : 0;
	bool ok = a && b;
	if (ok) { static char buf[32768]; int n; while ((n = kapi_read (a, buf, sizeof buf)) > 0) if (kapi_stream_write (b, buf, n) != n) { ok = false; break; } }
	if (a) kapi_close (a); if (b) kapi_stream_close (b);
	if (!ok) uk_messagebox ("PDF Viewer", "The copy could not be written.", MB_OK);
}
// File > Print: the Print dialog (the library's: printerkit/printerkit.h), then the pages drawn by MuPDF at the
// printer's 300 dots an inch, each a picture of the job's page (the document's own page size: fitted on the
// paper). To the PDF printer with every page: the file itself is copied -- it is already what is asked for.
static void print_doc ()
{
	Tab &t = tab (); if (!t.doc) return;
	Doc *D = t.doc;
	if (!doc_allows (g_mu, D, FZ_PERMISSION_PRINT)) { uk_messagebox ("PDF Viewer", "This document does not allow printing.", MB_OK); return; }
	PrintSetup ps; print_setup_default (&ps);
	PrintDialogInfo di = { sizeof di, D->name, D->npages, t.cur + 1, PRINT_DLG_OWN_PAPER, D->pw[t.cur], D->ph[t.cur] };
	if (!print_dialog (&ps, &di)) return;
	if (!strcmp (ps.printer, "PDF") && ps.from <= 0)
	{
		void *a = kapi_open (D->path); void *b = a ? kapi_file_out (ps.output, 0) : 0;
		bool ok = a && b;
		if (ok) { static char buf[32768]; int n; while ((n = kapi_read (a, buf, sizeof buf)) > 0) if (kapi_stream_write (b, buf, n) != n) { ok = false; break; } }
		if (a) kapi_close (a); if (b) kapi_stream_close (b);
		if (!ok) uk_messagebox ("PDF Viewer", "The copy could not be written.", MB_OK);
		return;
	}
	PrintJob *j = print_begin (&ps, D->name);
	for (int pg = 0; j && pg < D->npages; pg++)
	{
		if (!print_page (j, D->pw[pg], D->ph[pg])) continue;		// (not in the pages to print: not rendered)
		float s = 300.0f / 72.0f;
		if (D->pw[pg] * s > 3600 || D->ph[pg] * s > 3600) s = 3600 / (D->pw[pg] > D->ph[pg] ? D->pw[pg] : D->ph[pg]);	// (a poster: fewer dots)
		int w, h; page_px (D, pg, s, 0, &w, &h);
		unsigned *px = render_page (g_mu, D, pg, s, 0, 0, 0, w, h);
		if (!px) continue;
		print_image (j, px, w, h, 0, 0, D->pw[pg], D->ph[pg], 0);
		free (px);
	}
	if (!j || print_end (j) < 0) uk_messagebox ("PDF Viewer", "The document could not be put in the print queue.", MB_OK);
}
static void show_in_files ()
{
	Tab &t = tab (); if (!t.doc) return;
	char d[300]; scopy (d, t.doc->path, sizeof d);
	char *e = 0; for (char *p = d; *p; p++) if (*p == '/') e = p;
	if (e) *e = 0;
	kapi_exec ("SD:apps/fileviewer.app/main", d);
}
static void properties () { Tab &t = tab (); if (!t.doc) return; PropsBox b (t); b.run (); }
static void more_menu (int x, int y)
{
	Tab &t = tab ();
	PopupMenu m (g_tb->left + x, g_tb->top + y);
	m.add ("Properties...", 1, t.doc != 0, "Ctrl+D");
	m.add ("Save a Copy...", 2, t.doc != 0);
	m.add ("Show in the File Viewer", 3, t.doc != 0);
	m.separator ();
	m.add ("Close the Tab", 4, true, "Ctrl+W");
	int r = m.run ();
	if (r == 1) properties (); else if (r == 2) save_copy (); else if (r == 3) show_in_files (); else if (r == 4) close_tab (g_cur);
}
static void m_open ()
{
	char p[300];
	const char *start = tab ().doc ? tab ().doc->path : "SD:/";
	char dir[300]; scopy (dir, start, sizeof dir); { char *e = 0; for (char *q = dir; *q; q++) if (*q == '/') e = q; if (e && tab ().doc) *e = 0; }
	if (uk_file_open (p, sizeof p, dir)) open_path (p, -1, tab ().doc != 0);
}

// ---- full screen: the pages alone, on black (a presentation) ------------------------------------------------------------------
static volatile long g_presKey; static volatile int g_presClick;
static void pres_key (unsigned long, int ev, gui_value v) { if (ev == GUI_EVENT_KEY) g_presKey = (long) v; }
static void pres_ptr (unsigned long, int ev, gui_value v)
{
	if (ev == GUI_EVENT_PTR_DOWN) g_presClick = (GUI_PTR_CHANGED (v) & 2) ? -1 : 1;
	else if (ev == GUI_EVENT_PTR_WHEEL) g_presClick = GUI_PTR_WHEEL (v) > 0 ? -1 : 1;
}
static void full_screen ()
{
	Tab &t = tab (); if (!t.doc) return;
	int W, H;
	unsigned *fb = kapi_fullscreen_begin (&W, &H);
	if (!fb) return;
	kapi_set_key_handler (pres_key); kapi_set_pointer_handler (pres_ptr);
	int pg = t.cur, shown = -1;
	unsigned *next = 0; int nextPg = -1, nw = 0, nh = 0;
	for (;;)
	{
		if (pg != shown)
		{
			Doc *D = t.doc;
			float s = W / (t.rot % 180 ? D->ph[pg] : D->pw[pg]), s2 = H / (t.rot % 180 ? D->pw[pg] : D->ph[pg]); if (s2 < s) s = s2;
			int w, h; page_px (D, pg, s, t.rot, &w, &h);
			unsigned *px = (pg == nextPg && nw == w && nh == h) ? next : render_page (g_mu, D, pg, s, t.rot, 0, 0, w, h);
			if (px != next) free (next);
			next = 0; nextPg = -1;
			for (int i = 0; i < W * H; i++) fb[i] = 0;
			if (px) { int x0 = (W - w) / 2, y0 = (H - h) / 2; for (int y = 0; y < h && y0 + y < H; y++) memcpy (fb + (size_t) (y0 + y) * W + (x0 > 0 ? x0 : 0), px + (size_t) y * w, (size_t) (w < W ? w : W) * 4); free (px); }
			kapi_present_fb ();
			shown = pg;
			// the next page, ahead
			if (pg + 1 < D->npages)
			{
				float sn = W / (t.rot % 180 ? D->ph[pg + 1] : D->pw[pg + 1]), sn2 = H / (t.rot % 180 ? D->pw[pg + 1] : D->ph[pg + 1]); if (sn2 < sn) sn = sn2;
				page_px (D, pg + 1, sn, t.rot, &nw, &nh); next = render_page (g_mu, D, pg + 1, sn, t.rot, 0, 0, nw, nh); nextPg = pg + 1;
			}
		}
		kapi_pump_wait (30);
		long k = g_presKey; g_presKey = 0; int c = g_presClick; g_presClick = 0;
		if (k == 27 || k == KEY_F1 + 10 || k == 'q') break;
		if (k == KEY_RIGHT || k == KEY_DOWN || k == KEY_PGDN || k == ' ' || k == KEY_ENTER || c > 0) { if (pg + 1 < t.doc->npages) pg++; }
		else if (k == KEY_LEFT || k == KEY_UP || k == KEY_PGUP || k == KEY_BACKSPACE || c < 0) { if (pg > 0) pg--; }
		else if (k == KEY_HOME) pg = 0; else if (k == KEY_END) pg = t.doc->npages - 1;
		if (uk_quit ()) break;
	}
	free (next);
	kapi_fullscreen_end ();
	g_root->attach ();
	show_page (t, pg);
	g_root->invalidate (true);
}

// ---- settings --------------------------------------------------------------------------------------------------------------------
static void load_settings ()
{
	char *buf = file_read (SETTINGS, 0);
	if (!buf) return;
	for (char *l = buf, *nx; l && *l; l = nx)
	{
		nx = strchr (l, '\n'); if (nx) *nx++ = 0;
		char k[64]; int v;
		if (sscanf (l, " %63[a-z] = %d", k, &v) != 2) continue;
		if (!strcmp (k, "side")) g_sideOn = v != 0;
		else if (!strcmp (k, "layout") && v >= 0 && v <= 2) g_defLayout = v;
		else if (!strcmp (k, "zoom") && v >= 0 && v <= 1) g_defZoom = v;
	}
	free (buf);
}
static void save_settings ()
{
	kapi_mkdir (CONF_DIR);
	char b[400];
	int n = snprintf (b, sizeof b, "# PDF Viewer\n# side: the side panel shown (1) or not; layout: 0 one page, 1 continuous, 2 two pages; zoom: 0 fit the width, 1 the page\n"
			  "side = %d\nlayout = %d\nzoom = %d\n", g_sideOn, g_defLayout, g_defZoom);
	kapi_save_file (SETTINGS, b, n);
}

// ---- the window --------------------------------------------------------------------------------------------------------------------
static void layout_parts ()
{
	if (!g_root) return;
	int W = g_root->width, H = g_root->height;
	Tab &t = tab ();
	bool doc = t.doc != 0;
	g_tabbar->left = 0; g_tabbar->top = 0; g_tabbar->resizeTo (W, TABS_H);
	g_tb->hidden = !doc; g_side->hidden = !doc || !g_sideOn; g_view->hidden = !doc; g_home->hidden = doc;
	int y = TABS_H;
	if (doc) { g_tb->left = 0; g_tb->top = y; g_tb->resizeTo (W, TB_H); y += TB_H; }
	int sw = doc && g_sideOn ? SIDE_W : 0;
	if (sw) { g_side->left = 0; g_side->top = y; g_side->resizeTo (SIDE_W, H - y); g_side->findBox->resizeTo (SIDE_W - 20, 28); }
	g_view->left = sw; g_view->top = y; g_view->resizeTo (W - sw, H - y);
	g_home->left = 0; g_home->top = TABS_H; g_home->resizeTo (W, H - TABS_H);
}
static void refresh_all ()
{
	if (!g_root) return;
	layout_parts ();
	g_tabbar->invalidate (true); g_tb->invalidate (true); g_side->invalidate (true); g_view->invalidate (true); g_home->invalidate (true);
	g_root->invalidate (false);
}
static void on_done (void *p, long)
{
	Done *d = (Done *) p;
	if (d->job.kind == J_RECENT)
	{
		int k = recent_find (d->job.path);
		if (k >= 0) { g_rec[k].asked = true; if (d->px) { free (g_rec[k].px); g_rec[k].px = d->px; g_rec[k].w = d->w; g_rec[k].h = d->h; d->px = 0; if (d->npages) g_rec[k].npages = d->npages; } }
		else free (d->px);
		if (g_home && !g_home->hidden) g_home->invalidate (true), g_root->invalidate (false);
		free (d); return;
	}
	bool live = false;
	for (int k = 0; k < g_ntabs; k++) if (g_tabs[k].doc == d->job.doc && g_tabs[k].doc->id == d->job.docId) live = true;
	if (live && d->px)
	{
		bmp_add (d->job, d->px);
		if (d->job.doc == tab ().doc) { if (d->job.kind == J_THUMB) g_side->invalidate (true); else g_view->invalidate (true); g_root->invalidate (false); }
	}
	else free (d->px);
	free (d);
}

static void m_close () { close_tab (g_cur); }
static void m_copy () { copy_selection (); }
static void m_selall () { select_all (); }
static void m_find ()
{
	Tab &t = tab (); if (!t.doc) return;
	if (g_tb->search->hidden) { t.side = S_FIND; if (!g_sideOn) toggle_side (); g_side->findBox->setFocus (); }
	else g_tb->search->setFocus ();
	refresh_all ();
}
static void m_next () { find_next (1); }
static void m_prev () { find_next (-1); }
static void m_side () { toggle_side (); }
static void m_fitpage () { set_zoom (Z_PAGE, 0); }
static void m_fitwidth () { set_zoom (Z_WIDTH, 0); }
static void m_actual () { set_zoom (Z_CUSTOM, 96.0f / 72.0f); }
static void m_zin () { zoom_step (1); }
static void m_zout () { zoom_step (-1); }
static void m_single () { set_layout (L_SINGLE); }
static void m_scroll () { set_layout (L_SCROLL); }
static void m_two () { set_layout (L_TWO); }
static void m_rotate () { rotate (); }
static void m_full () { full_screen (); }
static void m_pgnext () { Tab &t = tab (); if (t.doc && t.cur + 1 < t.doc->npages) show_page (t, t.cur + 1); }
static void m_pgprev () { Tab &t = tab (); if (t.doc && t.cur > 0) show_page (t, t.cur - 1); }
static void m_first () { Tab &t = tab (); if (t.doc) show_page (t, 0); }
static void m_last () { Tab &t = tab (); if (t.doc) show_page (t, t.doc->npages - 1); }
static void m_goto () { if (tab ().doc) { g_tb->pageBox->setFocus (); g_tb->pageBox->caret = (int) strlen (g_tb->pageBox->text); refresh_all (); } }
static void m_nexttab () { if (g_ntabs > 1) { g_cur = (g_cur + 1) % g_ntabs; refresh_all (); } }
static void m_props () { properties (); }
static void m_savecopy () { save_copy (); }
static void m_home () { new_home_tab (); }

class PdfRoot : public Root
{
public:
	int lastW, lastH;
	PdfRoot (int w, int h) : Root (w, h, "PDF Viewer"), lastW (0), lastH (0) {}
	void onResized () override { invalidate_layout (); refresh_all (); }
	void onTick () override
	{
		g_tick = kapi_get_ticks ();
		Tab &t = tab ();
		// where it is read, kept (the Pi is often just switched off): every 10 s when the page changed
		static unsigned lastNote; static int notedPage = -1, notedDoc = -1;
		if (t.doc && g_tick - lastNote > 1000 && (t.cur != notedPage || t.doc->id != notedDoc)) { lastNote = g_tick; notedPage = t.cur; notedDoc = t.doc->id; recent_note (t); }
		// the page's pill fades
		static bool pill;
		bool p = g_tick - g_pillT < 150;
		if (p != pill) { pill = p; g_view->invalidate (true); }
		// the search, as it is typed (the two fields kept the same)
		if (t.doc)
		{
			static char lastTb[200], lastSide[200];
			const char *a = g_tb->search->text, *b = g_side->findBox->text;
			if (strcmp (a, lastTb)) { scopy (lastTb, a, sizeof lastTb); if (strcmp (b, a)) { g_side->findBox->setText (a); scopy (lastSide, a, sizeof lastSide); } g_queryT = g_tick; }
			else if (strcmp (b, lastSide)) { scopy (lastSide, b, sizeof lastSide); g_tb->search->setText (b); scopy (lastTb, b, sizeof lastTb); g_queryT = g_tick; }
			if (g_queryT && g_tick - g_queryT > 35)
			{
				g_queryT = 0;
				if (strcmp (lastTb, t.needle))
				{
					if (lastTb[0] && !g_sideOn) {}
					if (lastTb[0]) t.side = S_FIND;
					search_start (t, lastTb);
				}
			}
		}
	}
	bool onKey (long k) override
	{
		Tab &t = tab ();
		unsigned mods = kapi_get_modifiers ();
		bool ctrl = (mods & MOD_CTRL) != 0;
		// Ctrl with + - 0 (the zoom): the character comes, Ctrl is held
		if (ctrl && (k == '+' || k == '=')) { zoom_step (1); return true; }
		if (ctrl && k == '-') { zoom_step (-1); return true; }
		if (ctrl && k == '0') { set_zoom (Z_PAGE, 0); return true; }
		if (k == KEY_TAB && ctrl) { m_nexttab (); return true; }
		if (k == KEY_F1 + 2) { find_next ((mods & MOD_SHIFT) ? -1 : 1); return true; }
		if (k == KEY_F1 + 8) { toggle_side (); return true; }
		if (k == KEY_F1 + 10) { full_screen (); return true; }
		if (!t.doc) return false;
		if (g_tb->search->hasFocus || g_tb->pageBox->hasFocus || g_side->findBox->hasFocus)
		{
			if (k == 27) { g_view->setFocus (); refresh_all (); return true; }
			return false;
		}
		int H = view_h ();
		switch (k)
		{
		case KEY_DOWN: g_view->scroll_by (0, 50); return true;
		case KEY_UP: g_view->scroll_by (0, -50); return true;
		case KEY_RIGHT: if (g_contentW > view_w ()) g_view->scroll_by (50, 0); else m_pgnext (); return true;
		case KEY_LEFT: if (g_contentW > view_w ()) g_view->scroll_by (-50, 0); else m_pgprev (); return true;
		case KEY_PGDN: case ' ': g_view->scroll_by (0, (mods & MOD_SHIFT) && k == ' ' ? -(H - 40) : H - 40); return true;
		case KEY_PGUP: g_view->scroll_by (0, -(H - 40)); return true;
		case KEY_HOME: m_first (); return true;
		case KEY_END: m_last (); return true;
		case 27: if (t.sel) { t.sel = false; refresh_all (); return true; } if (t.needle[0]) { g_tb->search->setText (""); return true; } return false;
		}
		return false;
	}
	void onDrop (int, int, int type, const char *data, int len, unsigned) override
	{
		if (type != DND_FILES) return;
		// every PDF dropped: a tab each
		char path[300]; int o = 0;
		for (int i = 0; i <= len; i++)
		{
			char c = i < len ? data[i] : 0;
			if (c == '\n' || c == 0) { path[o] = 0; if (o > 4 && !strcasecmp (path + o - 4, ".pdf")) open_path (path, -1, tab ().doc != 0); o = 0; }
			else if (o < (int) sizeof path - 1) path[o++] = c;
		}
	}
};

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);
	faces_open ();
	if (!engine_init ()) { uk_messagebox ("PDF Viewer", "Not enough memory to start.", MB_OK); return 1; }
	load_settings ();
	recent_load ();
	folders_count ();
	tab_init (g_tabs[0], 0); g_ntabs = 1; g_cur = 0;

	PdfRoot root (1000, 640);
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	root.setResizable (true);
	root.setBg (C_BG);
	g_tabbar = new TabBar (0, 0, 1000, TABS_H); root.addChild (g_tabbar);
	g_tb = new ToolBar (0, TABS_H, 1000, TB_H); root.addChild (g_tb);
	g_side = new SidePanel (0, TABS_H + TB_H, SIDE_W, 640 - TABS_H - TB_H); root.addChild (g_side);
	g_view = new View (SIDE_W, TABS_H + TB_H, 1000 - SIDE_W, 640 - TABS_H - TB_H); root.addChild (g_view);
	g_home = new Home (0, TABS_H, 1000, 640 - TABS_H); root.addChild (g_home);
	g_worker.start (on_done, on_hits);

	static Menu menu;
	menu.menu ("File");
	menu.item ("Open...", "^O", UK_CTRL ('O'), m_open);
	menu.item ("Home", "", 0, m_home);
	menu.item ("Close the Tab", "^W", UK_CTRL ('W'), m_close);
	menu.separator ();
	menu.item ("Save a Copy...", "", 0, m_savecopy);
	menu.item ("Print...", "^P", UK_CTRL ('P'), print_doc);
	menu.item ("Show in the File Viewer", "", 0, show_in_files);
	menu.separator ();
	menu.item ("Properties...", "^D", UK_CTRL ('D'), m_props);
	menu.menu ("Edit");
	menu.item ("Copy", "^C", UK_CTRL ('C'), m_copy);
	menu.item ("Select All (the page)", "^A", UK_CTRL ('A'), m_selall);
	menu.separator ();
	menu.item ("Find...", "^F", UK_CTRL ('F'), m_find);
	menu.item ("Find Next", "F3", 0, m_next);
	menu.item ("Find Previous", "Shift+F3", 0, m_prev);
	menu.menu ("View");
	menu.item ("Side Panel", "F9", 0, m_side);
	menu.separator ();
	menu.item ("Fit Page", "^0", 0, m_fitpage);
	menu.item ("Fit Width", "", 0, m_fitwidth);
	menu.item ("Actual Size", "", 0, m_actual);
	menu.item ("Zoom In", "^+", 0, m_zin);
	menu.item ("Zoom Out", "^-", 0, m_zout);
	menu.separator ();
	menu.item ("One Page at a Time", "", 0, m_single);
	menu.item ("Continuous", "", 0, m_scroll);
	menu.item ("Two Pages", "", 0, m_two);
	menu.separator ();
	menu.item ("Rotate", "^R", UK_CTRL ('R'), m_rotate);
	menu.item ("Full Screen", "F11", 0, m_full);
	menu.menu ("Go");
	menu.item ("Next Page", "PgDn", 0, m_pgnext);
	menu.item ("Previous Page", "PgUp", 0, m_pgprev);
	menu.item ("First Page", "Home", 0, m_first);
	menu.item ("Last Page", "End", 0, m_last);
	menu.item ("Go to Page...", "^G", UK_CTRL ('G'), m_goto);
	menu.separator ();
	menu.item ("Next Tab", "^Tab", 0, m_nexttab);
	menu.publish ();

	refresh_all ();
	root.fitWorkArea ();

	// the files given (the File Viewer: a PDF opened)
	char args[400]; int na = kapi_get_args (args, sizeof args); args[na > 0 && na < 400 ? na : 0] = 0;
	char *a = args; while (*a == ' ') a++;
	int al = (int) strlen (a); while (al && a[al - 1] == ' ') a[--al] = 0;
	if (a[0] == '"') { a++; char *q = strchr (a, '"'); if (q) *q = 0; }
	if (a[0]) open_path (a, -1, false);

	root.run ();

	for (int k = 0; k < g_ntabs; k++) recent_note (g_tabs[k]);
	g_worker.stop ();
	return 0;
}
