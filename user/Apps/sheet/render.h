//
// render.h -- the cells drawn: each style's font (FreeType, the card's TrueType families; the Office ones
// a file names mapped to theirs: Calibri or Arial -> Liberation Sans...), the value as its number format
// shows it (General: as many digits as the column holds; a number too wide: ####), aligned (numbers right,
// texts left, TRUE / errors centred, or as the style says; indented; top, middle, bottom), a text wrapped
// in its cell or flowing over the empty cells beside it (the grid's lines hidden under it), the fill, the
// borders, the merged cells drawn as one. Sizes: the sheet's pixels (at 100 %) times the zoom.
//
#ifndef _sheet_render_h
#define _sheet_render_h

#include "condfmt.h"
#include "ui_base.h"
#include "pdf/pdfwrite.h"
#include "print/pdfprint.h"

namespace ss {

// File > Export as PDF: the cells drawn again into a PDF (pdf/pdfwrite.h) instead of the screen -- each sheet px at
// (g_pdfX + x * g_pdfS, g_pdfY + y * g_pdfS) points.
static pdfw::Writer *g_pdf;
static float g_pdfS = 0.75f, g_pdfX, g_pdfY;
static inline void out_rect (Canvas &cv, int x, int y, int w, int h, unsigned c)
{
	if (g_pdf) { g_pdf->fill_rect (g_pdfX + x * g_pdfS, g_pdfY + y * g_pdfS, w * g_pdfS, h * g_pdfS, c); return; }
	cv.fillRect (x, y, w, h, c);
}
static inline void out_glyph (Canvas &cv, fnt::Font *f, int x64, int y, unsigned cp, unsigned c, int cx0, int cy0, int cx1, int cy1)
{
	if (!g_pdf) { fnt::draw (cv, f, x64, y, cp, c, cx0, cy0, cx1, cy1); return; }
	if (cp <= ' ') return;
	if ((x64 >> 6) < cx0 - 2 || (x64 >> 6) >= cx1) return;		// (outside its cell's box: clipped, as on the screen)
	fnt::Glyph *g = fnt::glyph (f, cp);
	fnt::Font *src = g && g->src ? g->src : f;
	fnt::FaceFile &ff = fnt::g_face[src->face];
	if (!g || !ff.data) return;
	int k = g_pdf->add_font (ff.data, ff.len);
	if (k < 0) return;
	g_pdf->glyph (k, src->size64 / 64.0f * g_pdfS, g_pdfX + x64 / 64.0f * g_pdfS, g_pdfY + y * g_pdfS, (unsigned) g->gi, cp, c, src->fakeBold, src->fakeItalic);
}

// ---- fonts -------------------------------------------------------------------------------------------------
static int g_famOf[256];					// the book's font -> a family on the card (-1: not yet)
static int g_famSans = -1, g_famUI = -1;
static void fonts_reset () { for (int i = 0; i < 256; i++) g_famOf[i] = -1; }
static int family_for (const char *name)
{
	int f = fnt::find (name);
	if (f >= 0) return f;
	static const struct { const char *from, *to; } MAP[] = {
		{ "Arial", "Liberation Sans" }, { "Helvetica", "Liberation Sans" }, { "Calibri", "Liberation Sans" }, { "Carlito", "Liberation Sans" },
		{ "Verdana", "DejaVu Sans" }, { "Tahoma", "DejaVu Sans" }, { "Segoe UI", "Selawik" }, { "Aptos", "Liberation Sans" },
		{ "Times New Roman", "Liberation Serif" }, { "Times", "Liberation Serif" }, { "Cambria", "Liberation Serif" }, { "Caladea", "Liberation Serif" },
		{ "Georgia", "Gelasio" }, { "Courier New", "DejaVu Sans Mono" }, { "Courier", "DejaVu Sans Mono" }, { "Consolas", "DejaVu Sans Mono" },
		{ "Liberation Mono", "DejaVu Sans Mono" }, { "Lucida Console", "DejaVu Sans Mono" } };
	for (unsigned i = 0; i < sizeof MAP / sizeof MAP[0]; i++) if (ci_eq (name, MAP[i].from)) { f = fnt::find (MAP[i].to); if (f >= 0) return f; }
	// a serif / mono by the name, else the sans
	char low[64]; int k = 0; for (const char *p = name; *p && k < 63; p++) low[k++] = (char) (*p >= 'A' && *p <= 'Z' ? *p + 32 : *p); low[k] = 0;
	if (strstr (low, "mono") || strstr (low, "courier") || strstr (low, "code")) { f = fnt::find ("DejaVu Sans Mono"); if (f >= 0) return f; }
	if (strstr (low, "serif") && !strstr (low, "sans")) { f = fnt::find ("Liberation Serif"); if (f >= 0) return f; }
	return g_famSans;
}
static int book_family (Book &b, int font)
{
	if (font < 0 || font >= b.nfonts) font = 0;
	if (g_famOf[font] < 0) g_famOf[font] = family_for (b.fonts[font]);
	return g_famOf[font];
}
// The style's font at the zoom (z: percent).
static fnt::Font *style_font (Book &b, const Style &st, int z)
{
	int fam = book_family (b, st.font);
	int size64 = (int) ((long long) st.size * z * 6144 / 72000);	// (1/10 pt -> 1/64 px at 96 dpi, zoomed)
	if (size64 < 4 * 64) size64 = 4 * 64;
	return fnt::get (fam, (st.bold ? fnt::BOLD : 0) | (st.italic ? fnt::ITALIC : 0), size64);
}

// ---- UTF-8 text through a font --------------------------------------------------------------------------
static int u8_width (fnt::Font *f, const char *s, int n)
{
	int x = 0, i = 0, l; unsigned prev = 0;
	while (i < n) { unsigned cp = u8_dec (s + i, n - i, &l); i += l; if (prev) x += fnt::kern (f, prev, cp); x += fnt::advance (f, cp); prev = cp; }
	return x;
}
static int u8_draw (Canvas &cv, fnt::Font *f, int x64, int y, const char *s, int n, unsigned c, int cx0, int cy0, int cx1, int cy1)
{
	int i = 0, l; unsigned prev = 0;
	while (i < n)
	{
		unsigned cp = u8_dec (s + i, n - i, &l); i += l;
		if (prev) x64 += fnt::kern (f, prev, cp);
		if ((x64 >> 6) > cx1) break;
		out_glyph (cv, f, x64, y, cp == '\t' ? ' ' : cp, c, cx0, cy0, cx1, cy1);
		x64 += fnt::advance (f, cp);
		prev = cp;
	}
	return x64;
}
// The lines of a text wrapped at w (1/64 px): [start, end) byte ranges; the count.
static int wrap_lines (fnt::Font *f, const char *s, int n, int w, int *st, int *en, int maxLines)
{
	int k = 0, i = 0;
	while (i <= n && k < maxLines)
	{
		int ls = i, lastSpace = -1, x = 0, j = i, l;
		bool broke = false;
		while (j < n)
		{
			if (s[j] == '\n') break;
			unsigned cp = u8_dec (s + j, n - j, &l);
			int a = fnt::advance (f, cp);
			if (x + a > w && j > ls)
			{
				if (lastSpace > ls) { st[k] = ls; en[k] = lastSpace; k++; i = lastSpace + 1; }
				else { st[k] = ls; en[k] = j; k++; i = j; }
				broke = true;
				break;
			}
			if (cp == ' ') lastSpace = j;
			x += a; j += l;
		}
		if (broke) continue;
		st[k] = ls; en[k] = j; k++;
		if (j >= n) break;
		i = j + 1;					// (past the '\n')
	}
	return k;
}

// ---- a cell's look ------------------------------------------------------------------------------------------
struct Look
{
	char text[256]; int n;
	unsigned ink;
	fnt::Font *f;
	int ha, va;						// HA_LEFT / CENTER / RIGHT (General resolved), VA_*
	bool wrap, number, spill;				// spill: may flow over the empty cells beside
	const Style *st;
	Style own;						// (the style under a conditional format's look)
};
static bool g_showFormulas;				// View > Formulas: a formula cell shows its formula
static void cell_look (Book &b, Sheet *s, Cell *x, int r, int c, int cellW, int z, Look &L)
{
	const Style *sp = &cell_style (b, s, x, r, c);
	CfLook cl; cflook_init (cl);
	if (s->ncf) { cf_cell (b, s, x, r, c, cl); if (cf_style (*sp, cl, L.own)) sp = &L.own; }
	const Style &st = *sp;
	L.st = &st;
	L.f = style_font (b, st, z);
	L.n = 0; L.text[0] = 0; L.ink = st.color == AUTO ? 0x000000 : st.color;
	L.wrap = st.wrap; L.va = st.va; L.number = false; L.spill = false;
	int digit = fnt::advance (L.f, '0') >> 6; if (digit < 1) digit = 7;
	int maxc = imax (1, (cellW - 5) / digit);
	Shown sh; cell_shown (b, s, x, sh, maxc);
	scpy (L.text, sh.text, sizeof L.text);
	L.n = (int) strlen (L.text);
	if (sh.color != AUTO) L.ink = sh.color;
	if (cl.color != AUTO) L.ink = cl.color;				// (a rule's colour over the format's)
	L.number = sh.number;
	int ha = st.ha;
	if (ha == HA_GENERAL) ha = sh.number ? HA_RIGHT : (sh.err || sh.boolean) ? HA_CENTER : HA_LEFT;
	if (ha == HA_JUSTIFY || ha == HA_FILL) ha = HA_LEFT;
	L.ha = ha;
	L.spill = !L.wrap && !sh.number && !sh.err && L.n > 0;
	if (g_showFormulas && x && x->kind == K_FORM)
	{
		Buf ft; formula_print (b, x->f, ft);
		scpy (L.text, ft.str (), sizeof L.text); L.n = (int) strlen (L.text);
		L.ha = HA_LEFT; L.number = false; L.spill = true; L.ink = 0x000000;
		return;
	}
	// a number wider than its cell: ####
	if (sh.number && !L.wrap)
	{
		int w = u8_width (L.f, L.text, L.n) >> 6;
		if (w > cellW - 4)
		{
			int hw = imax (1, fnt::advance (L.f, '#') >> 6);
			int k = imin ((int) sizeof L.text - 1, imax (1, (cellW - 4) / hw));
			for (int i = 0; i < k; i++) L.text[i] = '#';
			L.text[k] = 0; L.n = k;
		}
	}
}

// ---- borders ---------------------------------------------------------------------------------------------
static void hline (Canvas &cv, int x0, int x1, int y, unsigned c, int bs, const Rect &clip)
{
	if (y < clip.r0 || y > clip.r1) return;
	x0 = imax (x0, clip.c0); x1 = imin (x1, clip.c1);
	int run = -1;
	for (int x = x0; x <= x1 + 1; x++)
	{
		bool on = x <= x1 && !((bs == BS_DOTTED || bs == BS_HAIR) && (x & 1)) && !(bs == BS_DASHED && (x % 6) >= 4);
		if (!g_pdf) { if (on) cv.px[y * cv.stride + x] = c; continue; }
		if (on && run < 0) run = x;
		if (!on && run >= 0) { out_rect (cv, run, y, x - run, 1, c); run = -1; }
	}
}
static void vline (Canvas &cv, int x, int y0, int y1, unsigned c, int bs, const Rect &clip)
{
	if (x < clip.c0 || x > clip.c1) return;
	y0 = imax (y0, clip.r0); y1 = imin (y1, clip.r1);
	int run = -1;
	for (int y = y0; y <= y1 + 1; y++)
	{
		bool on = y <= y1 && !((bs == BS_DOTTED || bs == BS_HAIR) && (y & 1)) && !(bs == BS_DASHED && (y % 6) >= 4);
		if (!g_pdf) { if (on) cv.px[y * cv.stride + x] = c; continue; }
		if (on && run < 0) run = y;
		if (!on && run >= 0) { out_rect (cv, x, run, 1, y - run, c); run = -1; }
	}
}
// A cell's borders over its box (x, y, w, h) -- its edges on the grid's lines.
static void draw_borders (Canvas &cv, const Style &st, int x, int y, int w, int h, const Rect &clip)
{
	for (int sd = 0; sd < 4; sd++)
	{
		int bs = st.bs[sd];
		if (!bs) continue;
		unsigned c = st.bc[sd] == AUTO ? 0 : st.bc[sd];
		int th = bs == BS_MEDIUM ? 2 : bs == BS_THICK ? 3 : 1;
		switch (sd)
		{
		case B_LEFT:
			for (int t = 0; t < th; t++) vline (cv, x - 1 + t - (th > 1 ? 1 : 0), y - 1, y + h - 1, c, bs, clip);
			if (bs == BS_DOUBLE) { vline (cv, x - 1, y - 1, y + h - 1, c, BS_THIN, clip); vline (cv, x + 1, y + 1, y + h - 3, c, BS_THIN, clip); }
			break;
		case B_RIGHT:
			for (int t = 0; t < th; t++) vline (cv, x + w - 1 - t + (th > 1 ? 1 : 0), y - 1, y + h - 1, c, bs, clip);
			if (bs == BS_DOUBLE) { vline (cv, x + w - 1, y - 1, y + h - 1, c, BS_THIN, clip); vline (cv, x + w - 3, y + 1, y + h - 3, c, BS_THIN, clip); }
			break;
		case B_TOP:
			for (int t = 0; t < th; t++) hline (cv, x - 1, x + w - 1, y - 1 + t - (th > 1 ? 1 : 0), c, bs, clip);
			if (bs == BS_DOUBLE) { hline (cv, x - 1, x + w - 1, y - 1, c, BS_THIN, clip); hline (cv, x + 1, x + w - 3, y + 1, c, BS_THIN, clip); }
			break;
		case B_BOTTOM:
			for (int t = 0; t < th; t++) hline (cv, x - 1, x + w - 1, y + h - 1 - t + (th > 1 ? 1 : 0), c, bs, clip);
			if (bs == BS_DOUBLE) { hline (cv, x - 1, x + w - 1, y + h - 1, c, BS_THIN, clip); hline (cv, x + 1, x + w - 3, y + h - 3, c, BS_THIN, clip); }
			break;
		}
	}
}

// ---- a pane of the grid -----------------------------------------------------------------------------------
// The visible columns / rows of a pane: their sheet index, place and size on the canvas.
struct Span { int i, at, len; };
struct PaneView
{
	Span col[512]; int nc;
	Span row[512]; int nr;
	int x0, y0, x1, y1;					// the pane's box on the canvas
};
static void pane_layout (Sheet *s, int z, int c0, int cEnd, int r0, int rEnd, int x0, int y0, int x1, int y1, PaneView &pv)
{
	pv.x0 = x0; pv.y0 = y0; pv.x1 = x1; pv.y1 = y1;
	pv.nc = 0;
	for (int c = c0, x = x0; c < cEnd && x < x1 && pv.nc < 512; c++)
	{
		int w = col_w (s, c) * z / 100;
		if (w <= 0) continue;
		pv.col[pv.nc].i = c; pv.col[pv.nc].at = x; pv.col[pv.nc].len = w; pv.nc++;
		x += w;
	}
	pv.nr = 0;
	for (int r = r0, y = y0; r < rEnd && y < y1 && pv.nr < 512; r++)
	{
		int h = row_h (s, r) * z / 100;
		if (h <= 0) continue;
		pv.row[pv.nr].i = r; pv.row[pv.nr].at = y; pv.row[pv.nr].len = h; pv.nr++;
		y += h;
	}
}
// The x of a column's left edge in the pane (it may be off to the left / right: counted on).
static int pane_col_x (Sheet *s, int z, const PaneView &pv, int c)
{
	if (pv.nc == 0) return pv.x0;
	if (c >= pv.col[0].i && c <= pv.col[pv.nc - 1].i) { for (int i = 0; i < pv.nc; i++) if (pv.col[i].i >= c) return pv.col[i].at; }
	if (c < pv.col[0].i) return pv.col[0].at - (col_x (s, pv.col[0].i) - col_x (s, c)) * z / 100;
	return pv.col[pv.nc - 1].at + pv.col[pv.nc - 1].len + (col_x (s, c) - col_x (s, pv.col[pv.nc - 1].i + 1)) * z / 100;
}
static int pane_row_y (Sheet *s, int z, const PaneView &pv, int r)
{
	if (pv.nr == 0) return pv.y0;
	if (r >= pv.row[0].i && r <= pv.row[pv.nr - 1].i) { for (int i = 0; i < pv.nr; i++) if (pv.row[i].i >= r) return pv.row[i].at; }
	if (r < pv.row[0].i) return pv.row[0].at - (int) ((row_y (s, pv.row[0].i) - row_y (s, r)) * z / 100);
	return pv.row[pv.nr - 1].at + pv.row[pv.nr - 1].len + (int) ((row_y (s, r) - row_y (s, pv.row[pv.nr - 1].i + 1)) * z / 100);
}

static const unsigned GRIDLINE = 0xDADCE0;

// The text of a cell laid in its box (or the box its text flows over).
static void draw_text (Canvas &cv, Look &L, int x, int y, int w, int h, int tx0, int tx1, int z, const Rect &clip)
{
	if (!L.n) return;
	fnt::Font *f = L.f;
	int pad = 3 * z / 100 + 1;
	int ind = L.st->indent * 9 * z / 100;
	int asc = f->ascent >> 6, desc = f->descent >> 6, lh = f->height >> 6;
	if (lh < asc + desc) lh = asc + desc;
	int cx0 = imax (clip.c0, tx0), cx1 = imin (clip.c1 + 1, tx1), cy0 = imax (clip.r0, y), cy1 = imin (clip.r1 + 1, y + h);
	if (cx1 <= cx0 || cy1 <= cy0) return;
	auto base_for = [&] (int lines) {
		int th = lines * lh;
		if (L.va == VA_TOP) return y + 2 + asc;
		if (L.va == VA_CENTER) return y + (h - th) / 2 + asc;
		return y + h - 2 - desc - (lines - 1) * lh;
	};
	auto one = [&] (const char *s, int n, int base) {
		int tw = u8_width (f, s, n);
		int xs;
		if (L.ha == HA_RIGHT) xs = (x + w - pad - ind) * 64 - tw;
		else if (L.ha == HA_CENTER) xs = (x * 64 + (w * 64 - tw) / 2);
		else xs = (x + pad + ind) * 64;
		// (flowing: the text keeps its place, the clip is the flow's box)
		u8_draw (cv, f, xs, base, s, n, L.ink, cx0, cy0, cx1, cy1);
		int x0p = xs >> 6, x1p = (xs + tw) >> 6;
		if (L.st->under)
		{
			int uy = base + imax (1, (f->ulPos >> 6)), ut = imax (1, f->ulThick >> 6);
			for (int k = 0; k < ut; k++) hline (cv, imax (x0p, cx0), imin (x1p, cx1 - 1), uy + k, L.ink, BS_THIN, Rect { cy0, cx0, cy1 - 1, cx1 - 1 });
			if (L.st->under == 2) hline (cv, imax (x0p, cx0), imin (x1p, cx1 - 1), uy + ut + 1, L.ink, BS_THIN, Rect { cy0, cx0, cy1 - 1, cx1 - 1 });
		}
		if (L.st->strike)
		{
			int sy = base - (f->xHeight >> 7);
			hline (cv, imax (x0p, cx0), imin (x1p, cx1 - 1), sy, L.ink, BS_THIN, Rect { cy0, cx0, cy1 - 1, cx1 - 1 });
		}
	};
	if (L.wrap || memchr (L.text, '\n', L.n))
	{
		int st[64], en[64];
		int k = L.wrap ? wrap_lines (f, L.text, L.n, imax (64, (w - 2 * pad - ind) * 64), st, en, 64) : 0;
		if (!L.wrap)					// (line breaks only)
		{
			int i = 0;
			while (i <= L.n && k < 64) { int j = i; while (j < L.n && L.text[j] != '\n') j++; st[k] = i; en[k] = j; k++; i = j + 1; if (j >= L.n) break; }
		}
		int base = base_for (k);
		for (int i = 0; i < k; i++) one (L.text + st[i], en[i] - st[i], base + i * lh);
		return;
	}
	one (L.text, L.n, base_for (1));
}

// A pane painted: the fills, the grid's lines, the texts (flowing over their empty neighbours), the
// borders, the merged cells as one. clip: the pane's box.
static void paint_pane (Canvas &cv, Book &b, Sheet *s, int z, const PaneView &pv, bool gridlines)
{
	Rect clip = { pv.y0, pv.x0, pv.y1 - 1, pv.x1 - 1 };
	if (pv.nc == 0 || pv.nr == 0) return;
	int rc0 = pv.col[0].i, rc1 = pv.col[pv.nc - 1].i, rr0 = pv.row[0].i, rr1 = pv.row[pv.nr - 1].i;
	// the merges met here
	int mi[256]; int nm = 0;
	for (int i = 0; i < s->nmerge && nm < 256; i++)
	{
		const Rect &m = s->merges[i];
		if (m.r1 >= rr0 && m.r0 <= rr1 && m.c1 >= rc0 && m.c0 <= rc1) mi[nm++] = i;
	}
	auto merged = [&] (int r, int c) -> int { for (int i = 0; i < nm; i++) if (rect_has (s->merges[mi[i]], r, c)) return mi[i]; return -1; };
	// 1. the fills (the conditional formats' over the cells'; their data bars)
	for (int ri = 0; ri < pv.nr; ri++)
		for (int ci = 0; ci < pv.nc; ci++)
		{
			int r = pv.row[ri].i, c = pv.col[ci].i;
			Cell *x = s->cells.get (r, c);
			const Style &st = cell_style (b, s, x, r, c);
			unsigned fill = st.fill;
			CfLook cl; cflook_init (cl);
			if (s->ncf) { cf_cell (b, s, x, r, c, cl); if (cl.fill != AUTO) fill = cl.fill; }
			if (fill == AUTO && cl.bar < 0) continue;
			if (nm && merged (r, c) >= 0) continue;
			int X0 = imax (pv.col[ci].at, clip.c0), Y0 = imax (pv.row[ri].at, clip.r0);
			int X1 = imin (pv.col[ci].at + pv.col[ci].len, clip.c1 + 1), Y1 = imin (pv.row[ri].at + pv.row[ri].len, clip.r1 + 1);
			if (fill != AUTO && X1 > X0 && Y1 > Y0) out_rect (cv, X0, Y0, X1 - X0, Y1 - Y0, fill);
			if (cl.bar >= 0)					// a data bar: a gradient fading to the right, its edge
			{
				int bx = pv.col[ci].at + 2, by = pv.row[ri].at + 2, bh = pv.row[ri].len - 5;
				int bw = (int) ((pv.col[ci].len - 5) * cl.bar + 0.5);
				for (int xx = imax (bx, clip.c0); xx < imin (bx + bw, clip.c1 + 1); xx++)
				{
					unsigned col = mix_rgb (cl.barColor, 0xFFFFFF, bw > 1 ? 0.85 * (xx - bx) / (bw - 1) : 0);
					{ int ya = imax (by, clip.r0), yb = imin (by + bh, clip.r1 + 1); if (g_pdf) { if (yb > ya) out_rect (cv, xx, ya, 1, yb - ya, col); } else for (int yy = ya; yy < yb; yy++) cv.px[yy * cv.stride + xx] = col; }
				}
				if (bw > 0)
				{
					hline (cv, bx, bx + bw - 1, by, cl.barColor, BS_THIN, clip); hline (cv, bx, bx + bw - 1, by + bh - 1, cl.barColor, BS_THIN, clip);
					vline (cv, bx, by, by + bh - 1, cl.barColor, BS_THIN, clip); vline (cv, bx + bw - 1, by, by + bh - 1, cl.barColor, BS_THIN, clip);
				}
			}
		}
	// 2. the grid's lines
	if (gridlines)
	{
		for (int ci = 0; ci < pv.nc; ci++) vline (cv, pv.col[ci].at + pv.col[ci].len - 1, pv.y0, pv.y1 - 1, GRIDLINE, BS_THIN, clip);
		for (int ri = 0; ri < pv.nr; ri++) hline (cv, pv.x0, pv.x1 - 1, pv.row[ri].at + pv.row[ri].len - 1, GRIDLINE, BS_THIN, clip);
	}
	// 3. the merged cells: one box each (their inner lines covered)
	for (int k = 0; k < nm; k++)
	{
		const Rect &m = s->merges[mi[k]];
		int x0 = pane_col_x (s, z, pv, m.c0), x1 = pane_col_x (s, z, pv, m.c1 + 1);
		int y0 = pane_row_y (s, z, pv, m.r0), y1 = pane_row_y (s, z, pv, m.r1 + 1);
		Cell *x = s->cells.get (m.r0, m.c0);
		const Style &st = cell_style (b, s, x, m.r0, m.c0);
		unsigned fill = st.fill;
		if (s->ncf) { CfLook cl; cf_cell (b, s, x, m.r0, m.c0, cl); if (cl.fill != AUTO) fill = cl.fill; }
		int X0 = imax (x0, clip.c0), Y0 = imax (y0, clip.r0), X1 = imin (x1 - 1, clip.c1 + 1), Y1 = imin (y1 - 1, clip.r1 + 1);
		if (X1 > X0 && Y1 > Y0 && !(g_pdf && fill == AUTO)) out_rect (cv, X0, Y0, X1 - X0, Y1 - Y0, fill == AUTO ? 0xFFFFFF : fill);
		if (x && x->kind != K_NONE)
		{
			Look L; cell_look (b, s, x, m.r0, m.c0, x1 - x0, z, L);
			draw_text (cv, L, x0, y0, x1 - x0, y1 - y0, x0, x1 - 1, z, clip);
		}
		draw_borders (cv, st, x0, y0, x1 - x0, y1 - y0, clip);
	}
	// 4. the texts, row by row (a text flows over the empty cells beside it; the lines under it hidden)
	sheet_bounds (s);
	for (int ri = 0; ri < pv.nr; ri++)
	{
		int r = pv.row[ri].i, y = pv.row[ri].at, h = pv.row[ri].len;
		if (r > s->maxR) break;
		// cells of this row in view (and just outside: a text from beyond the edge may flow in)
		int left = rc0, right = rc1;
		for (int c = rc0 - 1; c >= 0 && c >= rc0 - 40; c--) { Cell *x = s->cells.get (r, c); if (x && x->kind != K_NONE) { left = c; break; } }
		for (int c = left; c <= imin (s->maxC, right + 40); c++)
		{
			Cell *x = s->cells.get (r, c);
			if (!x || x->kind == K_NONE) continue;
			if (nm && merged (r, c) >= 0) continue;
			int cx = pane_col_x (s, z, pv, c), cw = col_w (s, c) * z / 100;
			if (cw <= 0) continue;
			if (c > right && cx >= pv.x1) break;
			Look L; cell_look (b, s, x, r, c, cw, z, L);
			if (!L.n) continue;
			int fx0 = cx, fx1 = cx + cw - 1;
			if (L.spill)
			{
				int tw = (u8_width (L.f, L.text, L.n) >> 6) + 6 + L.st->indent * 9 * z / 100;
				if (tw > cw)
				{
					auto empty = [&] (int cc) { if (cc < 0 || cc >= MAXC) return false; Cell *y2 = s->cells.get (r, cc); return !(y2 && y2->kind != K_NONE) && !(nm && merged (r, cc) >= 0); };
					int need = tw - cw;
					if (L.ha == HA_LEFT || L.ha == HA_CENTER)
					{
						int more = L.ha == HA_CENTER ? need / 2 : need;
						for (int cc = c + 1; more > 0 && empty (cc); cc++) { int w2 = col_w (s, cc) * z / 100; fx1 += w2; more -= w2; if (gridlines) vline (cv, fx1 - w2, y, y + h - 2, 0xFFFFFF, BS_THIN, clip); }
					}
					if (L.ha == HA_RIGHT || L.ha == HA_CENTER)
					{
						int more = L.ha == HA_CENTER ? need / 2 : need;
						for (int cc = c - 1; more > 0 && empty (cc); cc--) { int w2 = col_w (s, cc) * z / 100; fx0 -= w2; more -= w2; if (gridlines) vline (cv, fx0 + w2 - 1, y, y + h - 2, 0xFFFFFF, BS_THIN, clip); }
					}
					// (the fills under the flow stay: only the lines go)
				}
			}
			if (fx1 < pv.x0 || fx0 >= pv.x1) continue;
			draw_text (cv, L, cx, y, cw, h, fx0, fx1 + 1, z, clip);
		}
	}
	// 5. the borders
	for (int ri = 0; ri < pv.nr; ri++)
		for (int ci = 0; ci < pv.nc; ci++)
		{
			int r = pv.row[ri].i, c = pv.col[ci].i;
			Cell *x = s->cells.get (r, c);
			const Style &st = cell_style (b, s, x, r, c);
			if (!(st.bs[0] | st.bs[1] | st.bs[2] | st.bs[3])) continue;
			if (nm && merged (r, c) >= 0) continue;
			draw_borders (cv, st, pv.col[ci].at, pv.row[ri].at, pv.col[ci].len, pv.row[ri].len, clip);
		}
}

// A row's height for its tallest font and wrapped text (the rows whose height was not set by hand).
static int fit_row_height (Book &b, Sheet *s, int r)
{
	int need = s->defRowH;
	sheet_bounds (s);
	for (int c = 0; c <= s->maxC; c++)
	{
		Cell *x = s->cells.get (r, c);
		if (!x) continue;
		const Style &st = b.styles.s[x->style];
		fnt::Font *f = style_font (b, st, 100);
		int lh = (f->height >> 6);
		int h = lh + 2;
		if (x->kind != K_NONE && (st.wrap || (x->vt == V_STR && x->str && strchr (x->str, '\n'))))
		{
			Look L; cell_look (b, s, x, r, c, col_w (s, c), 100, L);
			int st2[64], en[64];
			int k = st.wrap ? wrap_lines (f, L.text, L.n, imax (64, (col_w (s, c) - 8) * 64), st2, en, 64) : 1;
			if (!st.wrap) { k = 1; for (int i = 0; i < L.n; i++) if (L.text[i] == '\n') k++; }
			h = k * lh + 3;
		}
		if (x->kind != K_NONE || st.size > 100) need = imax (need, h);
	}
	return imin (need, 2000);
}
static void autofit_row (Book &b, Sheet *s, int r)
{
	RowInfo *ri = row_info (s, r);
	if (ri && (ri->fl & RF_CUSTOM)) return;
	int h = fit_row_height (b, s, r);
	if (h <= s->defRowH) { if (ri) { ri->fl &= ~RF_AUTO; row_drop_if_plain (s, r); } rows_changed (s); return; }
	ri = row_add (s, r);
	ri->fl |= RF_AUTO; ri->h = (unsigned short) h;
	rows_changed (s);
}
// A column as wide as its texts need (a double click on its edge).
static int fit_col_width (Book &b, Sheet *s, int c)
{
	int w = 0;
	sheet_bounds (s);
	for (int r = 0; r <= s->maxR; r++)
	{
		Cell *x = s->cells.get (r, c);
		if (!x || x->kind == K_NONE || merge_at (s, r, c) >= 0) continue;
		Look L; cell_look (b, s, x, r, c, 4000, 100, L);
		if (L.wrap) continue;
		int tw = (u8_width (L.f, L.text, L.n) >> 6) + 10 + L.st->indent * 9;
		if (tw > w) w = tw;
	}
	return w ? imin (w, 1200) : s->defColW;
}

} // namespace ss

#endif
