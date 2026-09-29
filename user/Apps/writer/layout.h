//
// layout.h -- Writer's layout: the paragraphs broken into lines at the zoom (1/64 px, the fonts'
// design advances -- a line breaks at the same word at every zoom), aligned (left, centred, right,
// justified), the tab stops (left, centred, right, decimal; their leaders), the fields shown as their
// text of the moment, the lists' markers; the tables (their columns, their cells' paragraphs laid out
// in the cells, the rows' heights); the header and the footer of each page (the body's top and foot
// follow them); then the lines put on pages (A4 by default, the page setup's margins; a paragraph
// kept with the next one, kept whole, no widow nor orphan line; a table's rows whole -- with the rows
// a cell of theirs spans --, its heading row repeated on each page it runs on; "page break before").
//
// Each paragraph keeps its box (its left edge on the page, its width: the text's, or a cell's inside),
// its lines and the x of each character in its box (xs[]: indent, alignment and justification in),
// each line its page and its top on the page -- so drawing, hit-testing and the caret agree.
//
#ifndef _writer_layout_h
#define _writer_layout_h

#include "ft/fonts.h"
#include "doc.h"

namespace wr {

// A table of the body as laid out: its paragraphs [p0, p1).
struct TRun { int p0, p1; Table *t; };

// ---- the layout's state -----------------------------------------------------------------------------------
struct Layout
{
	Doc *d;
	int zoom;			// %
	int pageW, pageH;		// px
	int ml64, mr64, mt64, mb64;	// margins, 1/64 px
	int textW64;			// the text's width
	int bodyTop64[2], bodyBot64[2];	// the body's top and foot, from the page's top edge ([1]: the first page's, with its own header and footer)
	int hfH64[SY_COUNT];		// each header's / footer's height
	int npages;
	fnt::Font **cff; int ncff;	// each character format's font at the zoom (0: not yet)
	int *cffRaise;			// ... its baseline's shift (superscript < 0 < subscript)
	int *fam; int nfam;		// each font of the document's font table: its family on the card
	struct PL { int p, l; } *all; int nall, allcap;	// every line of the body, in order
	int *pageFirst; int pfcap;	// each page's first line (in all[])
	TRun *runs; int nruns, runcap;	// the body's tables
	int fieldPage;			// the page (from 0) the fields show while a paragraph is laid out
	int hfPage;			// the page whose header / footer is edited (d->cur is not the body)
};
static Layout L;

static inline int tw2px64 (int tw) { return (int) ((long long) tw * 64 * L.zoom / 1500); }
static inline int px642tw (int p) { return (int) ((long long) p * 1500 / (64 * (L.zoom > 0 ? L.zoom : 100))); }

// The page's own header and footer (the first page's, when it has them), the body's edges.
static inline int first_own (int pg) { return pg == 0 && L.d->page.titlePg ? 1 : 0; }
static inline int body_top (int pg) { return L.bodyTop64[first_own (pg)]; }
static inline int body_bot (int pg) { return L.bodyBot64[first_own (pg)]; }
static inline int hf_story (bool footer, int pg) { return first_own (pg) ? (footer ? SY_FOOTER1 : SY_HEADER1) : (footer ? SY_FOOTER : SY_HEADER); }
static inline bool is_footer (int s) { return s == SY_FOOTER || s == SY_FOOTER1; }

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

// Every paragraph of every story to be laid out again.
static void doc_dirty_all (Doc &d)
{
	for (int s = 0; s < SY_COUNT; s++) { int n; Para **p = story_p (d, s, &n); for (int i = 0; i < n; i++) p[i]->dirty = true; }
}

static void set_zoom (int z)
{
	L.zoom = wclamp (z, 10, 500);
	layout_reset_fonts ();
	doc_dirty_all (*L.d);
}

// ---- a paragraph's lines ----------------------------------------------------------------------------------
static int *para_xs (Para *q, int n)			// the paragraph's x array (n + 1 entries; its leaders' too)
{
	if (q->xscap < n + 1)
	{
		delete[] q->xs; delete[] q->lead;
		q->xscap = n + 17; q->xs = new int[q->xscap]; q->lead = new unsigned char[q->xscap];
	}
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

// A field's text as page `page` (from 0) shows it.
static int field_chars (const Para *q, int i, int page, unsigned *out, int cap)
{
	return field_text (*L.d, L.d->fmt[q->cf[i]].fld, page + L.d->page.start, L.npages, out, cap);
}
static int field_adv (const Para *q, int i)
{
	unsigned t[80]; int n = field_chars (q, i, L.fieldPage, t, 80);
	fnt::Font *f = cf_font (q->cf[i]);
	int w = 0;
	for (int k = 0; k < n && f; k++) w += fnt::advance (f, t[k]);
	return w;
}

static int char_adv (Para *q, int i, int x64);
// A tab at x (from the box's left edge): to its stop -- the text after it (to the next tab) left of a
// right stop, centred on a centred one, its decimal separator on a decimal one --; past the stops,
// one every 1.25 cm. Its stop's leader kept in q->lead[i].
static int tab_adv (Para *q, int i, int x64)
{
	const ParaFmt &pf = q->pf;
	q->lead[i] = TL_NONE;
	for (int k = 0; k < pf.ntab; k++)
	{
		int pos = tw2px64 (pf.tab[k].pos);
		if (pos <= x64) continue;
		q->lead[i] = pf.tab[k].leader;
		int al = pf.tab[k].align;
		if (al == TA_LEFT) return pos - x64;
		int w = 0;
		for (int j = i + 1; j < q->len; j++)
		{
			unsigned c = q->ch[j];
			if (c == '\t' || c == 0x0B || (al == TA_DECIMAL && (c == '.' || c == ','))) break;
			w += char_adv (q, j, 0);
		}
		int a = al == TA_CENTER ? pos - x64 - w / 2 : pos - x64 - w;
		return a > 0 ? a : 0;
	}
	int stop = wmax (tw2px64 (709), 64);
	return (x64 / stop + 1) * stop - x64;
}

// A character's advance at x (from the box's left edge).
static int char_adv (Para *q, int i, int x64)
{
	unsigned c = q->ch[i];
	if (c == '\t') return tab_adv (q, i, x64);
	if (c == 0x0B) return 0;
	const CharFmt &cf = L.d->fmt[q->cf[i]];
	if (c == OBJ_CHAR && cf.obj) return tw2px64 (cf.ow);		// (an image)
	if (c == FIELD_CHAR && cf.fld) return field_adv (q, i);	// (a field: its text)
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

// Break a paragraph into lines in a box w64 wide.
static void layout_para (Para *q, int w64)
{
	int *xs = para_xs (q, q->len);
	const ParaFmt &pf = q->pf;
	q->w64 = w64;
	int left = tw2px64 (pf.left), right = tw2px64 (pf.right), first = tw2px64 (pf.first);
	int full = w64 - left - right;
	if (full < 16 * 64) full = 16 * 64;
	int markerW = 0;
	if (pf.list)
	{
		unsigned mk[16]; int mn = list_marker (q, mk);
		fnt::Font *f = cf_font (marker_font_cf (q));
		for (int i = 0; i < mn && f; i++) markerW += fnt::advance (f, mk[i]);
		markerW += tw2px64 (90);
	}
	// (its fields of the page: laid out again on another page)
	bool usesPage = false, usesPages = false;
	for (int i = 0; i < q->len; i++)
		if (q->ch[i] == FIELD_CHAR)
		{
			int f = L.d->fmt[q->cf[i]].fld;
			if (f > 0 && f <= L.d->nfld) { usesPage |= L.d->fld[f - 1].kind == FK_PAGE; usesPages |= L.d->fld[f - 1].kind == FK_PAGES; }
		}
	q->fpage = usesPage ? L.fieldPage : -1; q->fpages = usesPages ? L.npages : -1;
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
			if ((c == OBJ_CHAR || c == FIELD_CHAR) && i > s) { brk = i; brkX = x; }	// (an image, a field: a break before it, and after)
			int a = char_adv (q, i, x0 + x);
			if (c != ' ' && c != '\t' && x + a > avail && i > s)
			{
				if (brk > s) { i = brk; x = brkX; }
				break;
			}
			xs[i] = x;
			x += a;
			if (c == ' ' || c == '\t' || c == '-' || c == 0x2013 || c == 0x2014 || c == '/' || c == OBJ_CHAR || c == FIELD_CHAR)	// (a break after it)
			{ brk = i + 1; brkX = x; }
		}
		int e = i;
		if (e == s && s < q->len) { xs[s] = 0; x = char_adv (q, s, x0); e = s + 1; }	// (one character at least)
		// Its natural width: the trailing spaces hang.
		int w = x, k = e;
		while (k > s && (q->ch[k - 1] == ' ' || q->ch[k - 1] == 0x0B)) { k--; w = xs[k]; }
		// Aligned (justified: the spaces after the line's last tab stretched).
		int shift = 0, spaces = 0, extra = 0, from = s;
		if (pf.align == AL_CENTER) shift = (avail - w) / 2;
		else if (pf.align == AL_RIGHT) shift = avail - w;
		else if (pf.align == AL_JUSTIFY && e < q->len && !hard)
		{
			for (int j = s; j < k; j++) if (q->ch[j] == '\t') from = j + 1;
			for (int j = from; j < k; j++) if (q->ch[j] == ' ') spaces++;
			if (spaces && avail > w) extra = avail - w;
		}
		if (shift < 0) shift = 0;
		int add = 0, done = 0;
		for (int j = s; j < e; j++)
		{
			xs[j] += x0 + shift + add;
			if (spaces && j >= from && j < k && q->ch[j] == ' ') { done++; add = (int) ((long long) extra * done / spaces); }
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
		ln.xEnd64 = left + (pf.align == AL_CENTER ? full / 2 : pf.align == AL_RIGHT ? full : 0);
		line_height (q, q->len, q->len, &ln.h64, &ln.base64);
		ln.y64 = y; ln.page = 0; ln.py64 = 0;
		y += ln.h64;
	}
	xs[q->len] = q->nln ? q->ln[q->nln - 1].xEnd64 : left;
	if (q->len == 0 && q->nln)
	{
		int x0 = left + first;
		if (pf.list) x0 = wmax (left, x0 + markerW);
		int avail = left + full - x0;
		xs[0] = x0 + (pf.align == AL_CENTER ? avail / 2 : pf.align == AL_RIGHT ? avail : 0);
		q->ln[0].xEnd64 = xs[0];
	}
	q->h64 = y;
	q->dirty = false;
}

// Laid out again when changed, in another width, or its fields showing another page.
static inline bool para_stale (const Para *q, int w64)
{
	return q->dirty || !q->nln || q->w64 != w64 || (q->fpage >= 0 && q->fpage != L.fieldPage) || (q->fpages >= 0 && q->fpages != L.npages);
}
static inline void para_ensure (Para *q, int w64) { if (para_stale (q, w64)) layout_para (q, w64); }

// The numbered lists' numbers (a number's width moves the text).
static void number_lists (Para **p, int n)
{
	int counter[8] = { 0 };
	for (int i = 0; i < n; i++)
	{
		Para *q = p[i];
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
}

// ---- the page's frame -------------------------------------------------------------------------------------
static void layout_geometry ()
{
	const PageSetup &ps = L.d->page;
	L.pageW = tw2px64 (ps.w) >> 6; L.pageH = tw2px64 (ps.h) >> 6;
	L.ml64 = tw2px64 (ps.left); L.mr64 = tw2px64 (ps.right);
	L.mt64 = tw2px64 (ps.top); L.mb64 = tw2px64 (ps.bottom);
	L.textW64 = wmax (tw2px64 (ps.w - ps.left - ps.right), 64 * 32);
}

// A header's / footer's paragraphs laid out for page pg and put on it (a header from its distance to
// the page's top edge down, a footer up from its distance to the foot): its height (0: empty).
static int hf_place (int s, int pg)
{
	int n; Para **p = story_p (*L.d, s, &n);
	if (n == 0 || (story_empty (*L.d, s) && L.d->cur != s)) return 0;
	L.fieldPage = pg;
	number_lists (p, n);
	int h = 0;
	for (int i = 0; i < n; i++)
	{
		Para *q = p[i];
		para_ensure (q, L.textW64);
		q->x64 = L.ml64;
		h += (i ? tw2px64 (q->pf.before) : 0) + q->h64 + tw2px64 (q->pf.after);
	}
	const PageSetup &ps = L.d->page;
	int y = is_footer (s) ? tw2px64 (ps.h - ps.ftr) - h : tw2px64 (ps.hdr);
	for (int i = 0; i < n; i++)
	{
		Para *q = p[i];
		if (i) y += tw2px64 (q->pf.before);
		for (int l = 0; l < q->nln; l++) { q->ln[l].page = pg; q->ln[l].py64 = y + q->ln[l].y64; }
		y += q->h64 + tw2px64 (q->pf.after);
	}
	return h;
}

// ---- the pages ------------------------------------------------------------------------------------------
struct Pager
{
	int page, y, npf;
	bool fresh;			// nothing on the page yet
	void start () { page = 0; y = body_top (0); npf = 0; fresh = true; L.nall = 0; L.nruns = 0; }
	void newPage () { page++; y = body_top (page); fresh = true; }
	int bottom () const { return body_bot (page); }
	void mark (int pg)			// (the pages up to pg start at the next line)
	{
		while (npf <= pg)
		{
			if (npf >= L.pfcap) { int c = L.pfcap * 2 + 64; int *t = new int[c]; for (int k = 0; k < npf; k++) t[k] = L.pageFirst[k]; delete[] L.pageFirst; L.pageFirst = t; L.pfcap = c; }
			L.pageFirst[npf++] = L.nall;
		}
	}
	void add (int pi, int l, int pg)
	{
		mark (pg);
		if (L.nall >= L.allcap) { int c = L.allcap * 2 + 256; Layout::PL *t = new Layout::PL[c]; for (int k = 0; k < L.nall; k++) t[k] = L.all[k]; delete[] L.all; L.all = t; L.allcap = c; }
		L.all[L.nall].p = pi; L.all[L.nall].l = l; L.nall++;
	}
};

// The owner of cell (r, c): the cell whose span covers it.
static void cell_owner (const Table *t, int r, int c, int *orow, int *ocol)
{
	*orow = r; *ocol = c;
	if (!tcell (t, r, c).covered) return;
	for (int rr = r; rr >= 0; rr--)
		for (int cc = c; cc >= 0; cc--)
		{
			const TCell &k = tcell (t, rr, cc);
			if (!k.covered && rr + k.rs > r && cc + k.cs > c) { *orow = rr; *ocol = cc; return; }
		}
}

// A table (its paragraphs from p[i]): its columns, its cells' paragraphs laid out in them, its rows'
// heights, its rows put on pages; the paragraph after it returned.
static int place_table (Para **p, int n, int i, Pager &P)
{
	Doc &d = *L.d;
	Table *t = para_table (d, p[i]);
	int tb = p[i]->pf.tbl, j = i + 1;
	while (j < n && p[j]->pf.tbl == tb) j++;
	int nr = t->nrows, nc = t->ncols;
	t->colX64[0] = 0;
	for (int c = 0; c < nc; c++) t->colX64[c + 1] = t->colX64[c] + wmax (tw2px64 (t->colW[c]), 64 * 4);
	t->w64 = t->colX64[nc];
	int x = t->align == AL_CENTER ? (L.textW64 - t->w64) / 2 : t->align == AL_RIGHT ? L.textW64 - t->w64 : tw2px64 (t->indent);
	t->x64 = L.ml64 + x;
	int padX = tw2px64 (CELL_PAD_X), padY = tw2px64 (CELL_PAD_Y);
	if (p[i]->pf.pageBreak && i > 0 && !P.fresh) P.newPage ();
	// the cells' paragraphs in their cells; each cell's height
	int *ch = new int[nr * nc];
	for (int k = 0; k < nr * nc; k++) ch[k] = 0;
	for (int k = i; k < j; k++)
	{
		Para *q = p[k];
		int r = wclamp ((int) q->pf.row, 0, nr - 1), c = wclamp ((int) q->pf.col, 0, nc - 1);
		int cs = wclamp ((int) tcell (t, r, c).cs, 1, nc - c);
		L.fieldPage = P.page;
		para_ensure (q, wmax (t->colX64[c + cs] - t->colX64[c] - 2 * padX, 64 * 8));
		q->x64 = t->x64 + t->colX64[c] + padX;
		bool first = k == i || !same_cell (p[k - 1], q);
		ch[r * nc + c] += (first ? 0 : tw2px64 (q->pf.before)) + q->h64 + tw2px64 (q->pf.after);
	}
	// the rows' heights: their cells', at least as set; a cell over several rows grows the last one
	for (int r = 0; r < nr; r++)
	{
		int h = tw2px64 (t->rowH[r]);
		for (int c = 0; c < nc; c++)
		{
			const TCell &cl = tcell (t, r, c);
			if (!cl.covered && cl.rs <= 1) h = wmax (h, ch[r * nc + c] + 2 * padY);
		}
		t->rowH64[r] = wmax (h, 2 * padY + 64);
	}
	for (int r = 0; r < nr; r++)
		for (int c = 0; c < nc; c++)
		{
			const TCell &cl = tcell (t, r, c);
			if (cl.covered || cl.rs <= 1) continue;
			int last = wmin (r + (int) cl.rs, nr) - 1, have = 0;
			for (int rr = r; rr <= last; rr++) have += t->rowH64[rr];
			int need = ch[r * nc + c] + 2 * padY;
			if (need > have) t->rowH64[last] += need - have;
		}
	// the rows on pages: a row and the ones its cells span kept together; the heading row repeated
	for (int r = 0; r < nr; )
	{
		int g = r + 1;
		for (int rr = r; rr < g && rr < nr; rr++)
			for (int c = 0; c < nc; c++) { const TCell &cl = tcell (t, rr, c); if (!cl.covered) g = wmax (g, rr + (int) cl.rs); }
		g = wmin (g, nr);
		int gh = 0;
		for (int rr = r; rr < g; rr++) gh += t->rowH64[rr];
		if (!P.fresh && P.y + gh > P.bottom ())
		{
			P.newPage ();
			if (t->header && r > 0 && t->rowH64[0] + gh <= P.bottom () - P.y) P.y += t->rowH64[0];
		}
		for (int rr = r; rr < g; rr++) { t->rowPage[rr] = P.page; t->rowY64[rr] = P.y; P.y += t->rowH64[rr]; }
		P.fresh = false;
		r = g;
	}
	// the cells' lines: their pages and places
	for (int k = 0; k < nr * nc; k++) ch[k] = 0;
	for (int k = i; k < j; k++)
	{
		Para *q = p[k];
		int r = wclamp ((int) q->pf.row, 0, nr - 1), c = wclamp ((int) q->pf.col, 0, nc - 1);
		bool first = k == i || !same_cell (p[k - 1], q);
		int &o = ch[r * nc + c];
		if (!first) o += tw2px64 (q->pf.before);
		int y0 = t->rowY64[r] + padY + o;
		for (int l = 0; l < q->nln; l++) { q->ln[l].page = t->rowPage[r]; q->ln[l].py64 = y0 + q->ln[l].y64; P.add (k, l, t->rowPage[r]); }
		o += q->h64 + tw2px64 (q->pf.after);
	}
	delete[] ch;
	if (L.nruns >= L.runcap) { int c = L.runcap * 2 + 8; TRun *nt = new TRun[c]; for (int k = 0; k < L.nruns; k++) nt[k] = L.runs[k]; delete[] L.runs; L.runs = nt; L.runcap = c; }
	TRun &run = L.runs[L.nruns++];
	run.p0 = i; run.p1 = j; run.t = t;
	return j;
}

// The height the first line of paragraph i asks for (keep with next).
static int first_line_need (Para **p, int n, int i)
{
	if (i >= n) return 0;
	Para *q = p[i];
	if (in_table (q)) return (q->nln ? q->ln[0].h64 : 20 * 64) + 2 * tw2px64 (CELL_PAD_Y);
	para_ensure (q, L.textW64);
	return tw2px64 (q->pf.before) + (q->nln ? q->ln[0].h64 : 0);
}

static void paginate (Para **p, int n)
{
	Doc &d = *L.d;
	Pager P; P.start ();
	for (int i = 0; i < n; )
	{
		Para *q = p[i];
		if (in_table (q) && para_table (d, q)) { i = place_table (p, n, i, P); continue; }
		L.fieldPage = P.page;
		para_ensure (q, L.textW64);
		q->x64 = L.ml64;
		int before = tw2px64 (q->pf.before), after = tw2px64 (q->pf.after);
		if (q->pf.pageBreak && i > 0 && !P.fresh) P.newPage ();
		int room = body_bot (P.page) - body_top (P.page);
		if (!P.fresh && q->pf.keepNext && i + 1 < n)			// (a heading: with the next one's first line)
		{
			int need = before + q->h64 + after + first_line_need (p, n, i + 1);
			if (P.y + need > P.bottom () && need <= room) { P.newPage (); L.fieldPage = P.page; para_ensure (q, L.textW64); }
		}
		if (!P.fresh && q->pf.keepLines && P.y + before + q->h64 > P.bottom () && q->h64 <= room) { P.newPage (); L.fieldPage = P.page; para_ensure (q, L.textW64); }
		if (!P.fresh) P.y += before;					// (no space above at a page's top)
		for (int l = 0; l < q->nln; )
		{
			int k = l, yy = P.y;
			while (k < q->nln && yy + q->ln[k].h64 <= P.bottom ()) { yy += q->ln[k].h64; k++; }
			int fit = k - l;
			if (fit < q->nln - l && q->pf.widow && q->nln > 1)		// (no orphan, no widow line)
			{
				if (l == 0 && fit == 1) fit = 0;
				else if (fit > 0 && q->nln - (l + fit) == 1) { fit--; if (l == 0 && fit == 1) fit = 0; }
			}
			if (fit == 0 && P.fresh) fit = wmax (1, k - l);
			for (int m = l; m < l + fit; m++) { q->ln[m].page = P.page; q->ln[m].py64 = P.y; P.y += q->ln[m].h64; P.add (i, m, P.page); P.fresh = false; }
			l += fit;
			if (l < q->nln) P.newPage ();
		}
		P.y += after;
		i++;
	}
	L.npages = P.page + 1;
	P.mark (L.npages - 1);						// (a page with no line)
}

// Lay out what changed, then put every line on its page.
static void layout_all ()
{
	Doc &d = *L.d;
	layout_geometry ();
	const PageSetup &ps = d.page;
	if (L.npages < 1) L.npages = 1;
	for (int s = SY_HEADER; s < SY_COUNT; s++) L.hfH64[s] = hf_place (s, s == SY_HEADER1 || s == SY_FOOTER1 ? 0 : ps.titlePg ? 1 : 0);
	for (int k = 0; k < 2; k++)
	{
		int hh = L.hfH64[k ? SY_HEADER1 : SY_HEADER], fh = L.hfH64[k ? SY_FOOTER1 : SY_FOOTER];
		L.bodyTop64[k] = wmax (L.mt64, tw2px64 (ps.hdr) + hh);
		L.bodyBot64[k] = wmin (tw2px64 (ps.h - ps.bottom), tw2px64 (ps.h - ps.ftr) - fh);
		if (L.bodyBot64[k] - L.bodyTop64[k] < 64 * 32) L.bodyBot64[k] = L.bodyTop64[k] + 64 * 32;
	}
	int n; Para **p = story_p (d, SY_BODY, &n);
	number_lists (p, n);
	for (int round = 0; round < 3; round++)
	{
		paginate (p, n);
		bool again = false;					// (a field of the page's number landed on another page)
		for (int i = 0; i < n; i++)
		{
			Para *q = p[i];
			if ((q->fpage >= 0 && q->nln && q->ln[0].page != q->fpage) || (q->fpages >= 0 && q->fpages != L.npages)) { q->dirty = true; again = true; }
		}
		if (!again) break;
	}
}

// ---- places in a paragraph ----------------------------------------------------------------------------------
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
// The x of a place (1/64 px from the paragraph's box's left edge).
static int x_of (const Para *q, int o, bool atEnd = false)
{
	int l = line_of (q, o, atEnd);
	if (q->nln && o == q->ln[l].end && (atEnd || l == q->nln - 1) && o > q->ln[l].start) return q->ln[l].xEnd64;
	return q->xs[o];
}
// The place nearest x (from the box's left edge) on line l of a paragraph (-> its offset; atEnd set
// when it is the line's end).
static int offset_at_x (const Para *q, int l, int x64, bool *atEnd)
{
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
