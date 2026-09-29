//
// layout.h -- Writer's layout: the paragraphs broken into lines at the zoom (1/64 px, the fonts'
// design advances -- a line breaks at the same word at every zoom), aligned (left, centred, right,
// justified), the lists' markers, then the lines put on pages (A4 by default, the margins of the
// page setup; a heading kept with the paragraph after it; a paragraph with "page break before"
// starts one).
//
// Each paragraph keeps its lines and the x of each character (xs[]: from the text area's left edge,
// indent, alignment and justification in), so drawing, hit-testing and the caret agree.
//
#ifndef _writer_layout_h
#define _writer_layout_h

#include "ft/fonts.h"
#include "doc.h"

namespace wr {

struct Line
{
	int start, end;			// the paragraph's characters [start, end)
	int y64, h64, base64;		// top (from the paragraph's first line), height, baseline (from its top)
	int xEnd64;			// the x of its end
	int page, py64;			// its page, its top in the page's text area
};

static void para_free (Para *q)
{
	delete[] q->ch; delete[] q->cf; delete[] q->ln; delete[] q->xs;
	delete q;
}

// ---- the layout's state -----------------------------------------------------------------------------------
struct Layout
{
	Doc *d;
	int zoom;			// %
	int pageW, pageH;		// px
	int ml64, mr64, mt64, mb64;	// margins, 1/64 px
	int textW64, textH64;
	int npages;
	fnt::Font **cff; int ncff;	// each character format's font at the zoom (0: not yet)
	int *cffRaise;			// ... its baseline's shift (superscript < 0 < subscript)
	int *fam; int nfam;		// each font of the document's font table: its family on the card
	struct PL { int p, l; } *all; int nall, allcap;	// every line in order
	int *pageFirst; int pfcap;	// each page's first line (in all[])
};
static Layout L;

static inline int tw2px64 (int tw) { return (int) ((long long) tw * 64 * L.zoom / 1500); }
static inline int px642tw (int p) { return (int) ((long long) p * 1500 / (64 * (L.zoom > 0 ? L.zoom : 100))); }

// A font name -> a family on the card: itself, a metric twin (Arial: Liberation Sans...), or by kind.
static int resolve_font (const char *name)
{
	int f = fnt::find (name);
	if (f >= 0) return f;
	static const char *const alias[][2] = {
		{ "Times New Roman", "Liberation Serif" }, { "Times", "Liberation Serif" }, { "Tinos", "Liberation Serif" },
		{ "Cambria", "Liberation Serif" }, { "Book Antiqua", "Liberation Serif" }, { "Palatino", "Liberation Serif" },
		{ "Garamond", "Liberation Serif" }, { "Arial", "Liberation Sans" }, { "Helvetica", "Liberation Sans" },
		{ "Arimo", "Liberation Sans" }, { "Calibri", "Liberation Sans" }, { "Carlito", "Liberation Sans" },
		{ "Trebuchet MS", "Liberation Sans" }, { "Verdana", "DejaVu Sans" }, { "Tahoma", "DejaVu Sans" },
		{ "Bitstream Vera Sans", "DejaVu Sans" }, { "Georgia", "Gelasio" }, { "Segoe UI", "Selawik" },
		{ "Courier New", "DejaVu Sans Mono" }, { "Courier", "DejaVu Sans Mono" }, { "Consolas", "DejaVu Sans Mono" },
		{ "Lucida Console", "DejaVu Sans Mono" }, { "Liberation Mono", "DejaVu Sans Mono" }, { "Cousine", "DejaVu Sans Mono" },
	};
	for (unsigned i = 0; i < sizeof alias / sizeof alias[0]; i++)
		if (sicmp (name, alias[i][0]) == 0 && (f = fnt::find (alias[i][1])) >= 0) return f;
	char lo[48]; int n = 0;
	for (; name[n] && n < 47; n++) lo[n] = (char) lower ((unsigned char) name[n]);
	lo[n] = 0;
	auto has = [&] (const char *w) { int k = slen (w); for (int i = 0; i + k <= n; i++) { int j = 0; while (j < k && lo[i + j] == w[j]) j++; if (j == k) return true; } return false; };
	const char *kind = has ("mono") || has ("courier") || has ("code") || has ("console") || has ("typewriter") ? "DejaVu Sans Mono"
			 : has ("sans") || has ("arial") || has ("helvet") || has ("gothic") || has ("grotesk") || has ("segoe") ? "Liberation Sans"
			 : "Liberation Serif";
	f = fnt::find (kind);
	if (f < 0) f = fnt::find ("DejaVu Sans");
	return f < 0 ? 0 : f;
}

// The document's fonts, formats, zoom changed: the fonts looked up again (and the cache's sizes no
// longer used dropped -- trim () calls layout_forget_fonts first).
static void layout_forget_fonts () { delete[] L.cff; delete[] L.cffRaise; L.cff = 0; L.cffRaise = 0; L.ncff = 0; }
static void layout_reset_fonts ()
{
	layout_forget_fonts ();
	delete[] L.fam; L.fam = 0; L.nfam = 0;
	fnt::g_onTrim = layout_forget_fonts;
	fnt::trim (0, 16);
}
static int font_family (int docFont)
{
	if (docFont >= L.nfam)
	{
		int n = L.d->nfont + 8;
		int *t = new int[n];
		for (int i = 0; i < n; i++) t[i] = i < L.nfam ? L.fam[i] : -1;
		delete[] L.fam; L.fam = t; L.nfam = n;
	}
	if (L.fam[docFont] < 0) L.fam[docFont] = resolve_font (L.d->fontName[docFont]);
	return L.fam[docFont];
}
// A character format's font (and baseline shift) at the zoom.
static fnt::Font *cf_font (int cf, int *raise = 0)
{
	if (cf >= L.ncff)
	{
		int n = L.d->nfmt + 32;
		fnt::Font **t = new fnt::Font *[n]; int *r = new int[n];
		for (int i = 0; i < n; i++) { t[i] = i < L.ncff ? L.cff[i] : 0; r[i] = i < L.ncff ? L.cffRaise[i] : 0; }
		delete[] L.cff; delete[] L.cffRaise; L.cff = t; L.cffRaise = r; L.ncff = n;
	}
	if (!L.cff[cf])
	{
		const CharFmt &f = L.d->fmt[cf];
		int size64 = (int) ((long long) f.size * L.zoom * 64 / 150);
		int rs = 0;
		if (f.flags & (CF_SUPER | CF_SUB))
		{
			rs = f.flags & CF_SUPER ? -size64 * 33 / 100 : size64 * 14 / 100;
			size64 = size64 * 58 / 100;
		}
		if (size64 < 64) size64 = 64;
		int st = (f.flags & CF_BOLD ? fnt::BOLD : 0) | (f.flags & CF_ITALIC ? fnt::ITALIC : 0);
		L.cff[cf] = fnt::get (font_family (f.font), st, size64);
		L.cffRaise[cf] = rs;
	}
	if (raise) *raise = L.cffRaise[cf];
	return L.cff[cf];
}

static void set_zoom (int z)
{
	L.zoom = wclamp (z, 10, 500);
	layout_reset_fonts ();
	for (int i = 0; i < L.d->n; i++) L.d->p[i]->dirty = true;
}

// ---- a paragraph's lines ----------------------------------------------------------------------------------
static int *para_xs (Para *q, int n)			// the paragraph's x array (n + 1 entries)
{
	if (q->xscap < n + 1) { delete[] q->xs; q->xscap = n + 17; q->xs = new int[q->xscap]; }
	return q->xs;
}

// A list's marker ("•", "1.", "a."...), as code points.
static int list_marker (const Para *q, unsigned *out)
{
	if (q->pf.list == LS_BULLET)
	{
		static const unsigned B[3] = { 0x2022, 0x25E6, 0x25AA };
		out[0] = B[q->pf.level % 3];
		return 1;
	}
	int n = q->num < 1 ? 1 : q->num, m = 0;
	char b[16]; int k = 0;
	switch (q->pf.level % 3)
	{
	case 0: { char t[12]; int j = 0; while (n) { t[j++] = (char) ('0' + n % 10); n /= 10; } while (j) b[k++] = t[--j]; break; }
	case 1: { char t[8]; int j = 0; while (n > 0 && j < 7) { n--; t[j++] = (char) ('a' + n % 26); n /= 26; } while (j) b[k++] = t[--j]; break; }
	default:
	{
		static const char *const R[] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
		static const int V[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
		for (int i = 0; i < 13 && k < 12; i++) while (n >= V[i] && k < 12) { for (const char *r = R[i]; *r; r++) b[k++] = *r; n -= V[i]; }
	}
	}
	b[k++] = '.';
	for (int i = 0; i < k; i++) out[m++] = (unsigned char) b[i];
	return m;
}
static int marker_font_cf (const Para *q) { return q->len ? q->cf[0] : q->endCf; }

static void para_lines_reserve (Para *q, int n)
{
	if (n <= q->lncap) return;
	int c = wmax (n, q->lncap * 2 + 4);
	Line *t = new Line[c];
	for (int i = 0; i < q->nln; i++) t[i] = q->ln[i];
	delete[] q->ln; q->ln = t; q->lncap = c;
}

// A character's advance (a tab: to the next stop, every 1.25 cm from the text area's left edge).
static int char_adv (const Para *q, int i, int x64)
{
	unsigned c = q->ch[i];
	if (c == '\t')
	{
		int stop = tw2px64 (709);
		if (stop < 64) stop = 64;
		return (x64 / stop + 1) * stop - x64;
	}
	if (c == 0x0B) return 0;
	if (c == OBJ_CHAR && L.d->fmt[q->cf[i]].obj) return tw2px64 (L.d->fmt[q->cf[i]].ow);	// (an image)
	fnt::Font *f = cf_font (q->cf[i]);
	if (!f) return 0;
	int a = fnt::advance (f, c == 0xA0 ? ' ' : c);
	if (i > 0 && q->cf[i - 1] == q->cf[i] && c > ' ' && q->ch[i - 1] > ' ') a += fnt::kern (f, q->ch[i - 1], c);
	return a;
}

// The height of a line of characters [a, b) (the paragraph mark's font for an empty one).
static void line_height (const Para *q, int a, int b, int *h64, int *base64)
{
	int asc = 0, desc = 0; int last = -1;
	for (int i = a; i <= b; i++)
	{
		int cf;
		if (i < b) cf = q->cf[i]; else if (a == b || (b == q->len && q->len == 0)) cf = q->endCf; else break;
		if (cf == last) continue;
		last = cf;
		if (i < b && q->ch[i] == OBJ_CHAR && L.d->fmt[cf].obj)	// (an image: on the baseline)
		{
			int ih = tw2px64 (L.d->fmt[cf].oh);
			if (ih > asc) asc = ih;
			CharFmt t = L.d->fmt[cf]; t.obj = 0; t.ow = t.oh = 0;
			cf = doc_fmt (*L.d, t);
		}
		int raise = 0;
		fnt::Font *f = cf_font (cf, &raise);
		if (!f) continue;
		int gap = f->height - f->ascent - f->descent;
		int fa = f->ascent + gap / 2 - raise, fd = f->descent + (gap - gap / 2) + raise;
		if (fa > asc) asc = fa;
		if (fd > desc) desc = fd;
	}
	if (asc + desc <= 0) { asc = 12 * 64; desc = 4 * 64; }
	int h = (asc + desc) * wclamp ((int) q->pf.line, 50, 400) / 100;
	*h64 = h; *base64 = asc;
}

// Break paragraph pi into lines at the text width.
static void layout_para (int pi)
{
	Para *q = L.d->p[pi];
	int *xs = para_xs (q, q->len);
	const ParaFmt &pf = q->pf;
	int left = tw2px64 (pf.left), right = tw2px64 (pf.right), first = tw2px64 (pf.first);
	int full = L.textW64 - left - right;
	if (full < 16 * 64) full = 16 * 64;
	int markerW = 0;
	if (pf.list)
	{
		unsigned mk[16]; int mn = list_marker (q, mk);
		fnt::Font *f = cf_font (marker_font_cf (q));
		for (int i = 0; i < mn && f; i++) markerW += fnt::advance (f, mk[i]);
		markerW += tw2px64 (90);
	}
	q->nln = 0;
	int y = 0, s = 0;
	do
	{
		bool firstLine = q->nln == 0;
		int x0 = left + (firstLine ? first : 0);	// where the line starts
		if (firstLine && pf.list) x0 = wmax (left, x0 + markerW);	// (the marker in the hanging indent)
		int avail = left + full - x0;
		if (avail < 8 * 64) avail = 8 * 64;
		int x = 0, i = s, brk = -1, brkX = 0;
		bool hard = false;
		for (; i < q->len; i++)
		{
			unsigned c = q->ch[i];
			if (c == 0x0B) { xs[i] = x; i++; hard = true; break; }
			if (c == OBJ_CHAR && i > s) { brk = i; brkX = x; }	// (an image: a break before it, and after)
			int a = char_adv (q, i, x0 + x);
			if (c != ' ' && c != '\t' && x + a > avail && i > s)
			{
				if (brk > s) { i = brk; x = brkX; }
				break;
			}
			xs[i] = x;
			x += a;
			if (c == ' ' || c == '\t' || c == '-' || c == 0x2013 || c == 0x2014 || c == '/' || c == OBJ_CHAR)	// (a break after it)
			{ brk = i + 1; brkX = x; }
		}
		int e = i;
		if (e == s && s < q->len) { xs[s] = 0; x = char_adv (q, s, x0); e = s + 1; }	// (one character at least)
		// Its natural width: the trailing spaces hang.
		int w = x, k = e;
		while (k > s && (q->ch[k - 1] == ' ' || q->ch[k - 1] == 0x0B)) { k--; w = xs[k]; }
		// Aligned.
		int shift = 0, spaces = 0, extra = 0;
		if (pf.align == AL_CENTER) shift = (avail - w) / 2;
		else if (pf.align == AL_RIGHT) shift = avail - w;
		else if (pf.align == AL_JUSTIFY && e < q->len && !hard)
		{
			for (int j = s; j < k; j++) if (q->ch[j] == ' ') spaces++;
			if (spaces && avail > w) extra = avail - w;
		}
		if (shift < 0) shift = 0;
		int add = 0, done = 0;
		for (int j = s; j < e; j++)
		{
			xs[j] += x0 + shift + add;
			if (spaces && j < k && q->ch[j] == ' ') { done++; add = (int) ((long long) extra * done / spaces); }
		}
		para_lines_reserve (q, q->nln + 1);
		Line &ln = q->ln[q->nln++];
		ln.start = s; ln.end = e;
		ln.xEnd64 = x0 + shift + x + add;
		line_height (q, s, e, &ln.h64, &ln.base64);
		ln.y64 = y; ln.page = 0; ln.py64 = 0;
		y += ln.h64;
		s = e;
	} while (s < q->len);
	if (q->len > 0 && q->ch[q->len - 1] == 0x0B)		// (ends with a line break: an empty line after it)
	{
		para_lines_reserve (q, q->nln + 1);
		Line &ln = q->ln[q->nln++];
		ln.start = ln.end = q->len;
		int x0 = left;
		ln.xEnd64 = x0 + (pf.align == AL_CENTER ? (L.textW64 - left - right) / 2 : pf.align == AL_RIGHT ? L.textW64 - left - right : 0);
		line_height (q, q->len, q->len, &ln.h64, &ln.base64);
		ln.y64 = y; y += ln.h64;
	}
	xs[q->len] = q->nln ? q->ln[q->nln - 1].xEnd64 : left;
	if (q->len == 0 && q->nln)
	{
		int x0 = left + first;
		if (pf.list) x0 = wmax (left, x0 + markerW);
		int avail = left + L.textW64 - left - right - x0;
		xs[0] = x0 + (pf.align == AL_CENTER ? avail / 2 : pf.align == AL_RIGHT ? avail : 0);
		q->ln[0].xEnd64 = xs[0];
	}
	q->h64 = y;
	q->dirty = false;
}

// ---- pages --------------------------------------------------------------------------------------------
static void layout_geometry ()
{
	const PageSetup &ps = L.d->page;
	L.pageW = tw2px64 (ps.w) >> 6; L.pageH = tw2px64 (ps.h) >> 6;
	L.ml64 = tw2px64 (ps.left); L.mr64 = tw2px64 (ps.right);
	L.mt64 = tw2px64 (ps.top); L.mb64 = tw2px64 (ps.bottom);
	int tw = tw2px64 (ps.w - ps.left - ps.right);
	if (tw != L.textW64) for (int i = 0; i < L.d->n; i++) L.d->p[i]->dirty = true;
	L.textW64 = tw;
	L.textH64 = tw2px64 (ps.h - ps.top - ps.bottom);
	if (L.textH64 < 64 * 32) L.textH64 = 64 * 32;
}

// Lay out what changed, then put every line on its page.
static void layout_all ()
{
	layout_geometry ();
	Doc &d = *L.d;
	// The lists' numbers first (a number's width moves the text).
	int counter[8] = { 0 };
	for (int i = 0; i < d.n; i++)
	{
		Para *q = d.p[i];
		int lv = q->pf.level & 7, num = 0;
		if (q->pf.list == LS_NUMBER)
		{
			counter[lv]++;
			for (int k = lv + 1; k < 8; k++) counter[k] = 0;
			num = counter[lv];
		}
		else if (q->pf.list == LS_NONE) for (int k = 0; k < 8; k++) counter[k] = 0;
		if (q->num != num) { q->num = num; if (q->pf.list == LS_NUMBER) q->dirty = true; }
	}
	for (int i = 0; i < d.n; i++) if (d.p[i]->dirty || !d.p[i]->nln) layout_para (i);
	// The pages.
	int total = 0;
	for (int i = 0; i < d.n; i++) total += d.p[i]->nln;
	if (total > L.allcap) { delete[] L.all; L.allcap = total + 256; L.all = new Layout::PL[L.allcap]; }
	L.nall = 0;
	int page = 0, y = 0;
	int npf = 0;
	auto newPage = [&] () {
		page++; y = 0;
	};
	if (L.pfcap < 16) { delete[] L.pageFirst; L.pfcap = 64; L.pageFirst = new int[L.pfcap]; }
	L.pageFirst[npf++] = 0;
	for (int i = 0; i < d.n; i++)
	{
		Para *q = d.p[i];
		int before = tw2px64 (q->pf.before), after = tw2px64 (q->pf.after);
		if (q->pf.pageBreak && i > 0 && y > 0) newPage ();
		if (q->pf.keepNext && i + 1 < d.n && y > 0)		// (a heading: with the next one's first line)
		{
			const Para *nx = d.p[i + 1];
			int need = before + q->h64 + after + tw2px64 (nx->pf.before) + (nx->nln ? nx->ln[0].h64 : 0);
			if (y + need > L.textH64 && q->h64 + need < L.textH64) newPage ();
		}
		if (y > 0) y += before;
		for (int l = 0; l < q->nln; l++)
		{
			Line &ln = q->ln[l];
			if (y + ln.h64 > L.textH64 && y > 0) newPage ();
			if (page >= npf)
			{
				if (npf >= L.pfcap) { int c = L.pfcap * 2; int *t = new int[c]; for (int k = 0; k < npf; k++) t[k] = L.pageFirst[k]; delete[] L.pageFirst; L.pageFirst = t; L.pfcap = c; }
				L.pageFirst[npf++] = L.nall;
			}
			ln.page = page; ln.py64 = y;
			y += ln.h64;
			L.all[L.nall].p = i; L.all[L.nall].l = l; L.nall++;
		}
		y += after;
	}
	L.npages = page + 1;
	while (npf < L.npages) L.pageFirst[npf++] = L.nall;
}

// The line (its index in the paragraph) holding a place; atEnd: a place at a line's end is on
// that line (not at the start of the next one).
static int line_of (const Para *q, int o, bool atEnd = false)
{
	for (int l = 0; l < q->nln; l++)
	{
		const Line &ln = q->ln[l];
		if (o < ln.end || (o == ln.end && (l == q->nln - 1 || (atEnd && o > ln.start)))) return l;
		if (o == ln.end && ln.end == ln.start) return l;
	}
	return q->nln ? q->nln - 1 : 0;
}
// The x of a place (1/64 px from the text area's left edge).
static int x_of (int pi, int o, bool atEnd = false)
{
	const Para *q = L.d->p[pi];
	int l = line_of (q, o, atEnd);
	if (q->nln && o == q->ln[l].end && (atEnd || l == q->nln - 1) && o > q->ln[l].start) return q->ln[l].xEnd64;
	if (q->nln && o == q->ln[l].end && q->ln[l].end == q->ln[l].start) return q->xs[o];
	return q->xs[o];
}
// The place nearest x on line l of paragraph pi (-> its offset; atEnd set when it is the line's end).
static int offset_at_x (int pi, int l, int x64, bool *atEnd)
{
	const Para *q = L.d->p[pi];
	const Line &ln = q->ln[l];
	const int *xs = q->xs;
	*atEnd = false;
	int best = ln.start;
	for (int i = ln.start; i < ln.end; i++)
	{
		int mid = (xs[i] + (i + 1 < ln.end ? xs[i + 1] : ln.xEnd64)) / 2;
		if (q->ch[i] == 0x0B) break;
		if (x64 >= mid) best = i + 1; else break;
	}
	if (best == ln.end && l < q->nln - 1)
	{
		// (a line's end: before its trailing space, or at its end with the caret there)
		if (ln.end > ln.start && (q->ch[ln.end - 1] == ' ' || q->ch[ln.end - 1] == 0x0B)) best = ln.end - 1;
		else *atEnd = true;
	}
	return best;
}

} // namespace wr

#endif
