//
// doc.h -- Writer's document: paragraphs of characters (Unicode code points), each character with
// a character format -- an index into the document's table of them (a font of its font table, a
// size, bold / italic / underline / strike-through / superscript / subscript, a colour, a
// highlight) -- and each paragraph with its paragraph format (a style, the alignment, the indents,
// the spacing, a list); the page's setup; the edits, undone and redone.
//
// A place in the text is a Pos: a paragraph and an offset in it (0 .. its length). An edit goes
// through begin () / end (): the paragraphs it touches are copied first, so undo () puts them back
// (and redo () the edited ones); typing a word, deleting a run of characters, are one edit each.
// The formats are only ever added to their table (an undone paragraph's indices stay good).
//
#ifndef _writer_doc_h
#define _writer_doc_h

namespace wr {

// ---- small helpers -----------------------------------------------------------------------------------
static inline int slen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline void scpy (char *d, const char *s, int cap) { int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static inline int lower (int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int sicmp (const char *a, const char *b)
{
	for (;; a++, b++) { int x = lower ((unsigned char) *a), y = lower ((unsigned char) *b); if (x != y || !x) return x - y; }
}
template <class T> static inline T wmin (T a, T b) { return a < b ? a : b; }
template <class T> static inline T wmax (T a, T b) { return a > b ? a : b; }
template <class T> static inline T wclamp (T v, T lo, T hi) { return v < lo ? lo : v > hi ? hi : v; }

// ---- formats -----------------------------------------------------------------------------------------
enum { CF_BOLD = 1, CF_ITALIC = 2, CF_UNDER = 4, CF_STRIKE = 8, CF_SUPER = 16, CF_SUB = 32 };
static const unsigned AUTO = 0xFF000000u;		// the automatic text colour (black) / no highlight

struct CharFmt
{
	short font;			// the document's font table's index
	short size;			// half-points (24: 12 pt)
	unsigned short flags;		// CF_*
	unsigned color, hilite;		// 0xRRGGBB, or AUTO
	int obj;			// an image's (the character U+FFFC): its index in the document's images + 1
	int ow, oh;			// ... its size on the page (twips)
	CharFmt () : font (0), size (24), flags (0), color (0xFF000000u), hilite (0xFF000000u), obj (0), ow (0), oh (0) {}
	bool same (const CharFmt &o) const
	{
		return font == o.font && size == o.size && flags == o.flags && color == o.color && hilite == o.hilite
		    && obj == o.obj && ow == o.ow && oh == o.oh;
	}
};
static const unsigned OBJ_CHAR = 0xFFFC;		// an image's place in the text

// An image of the document: its pixels, and its file (PNG, JPEG: written back as they came).
struct Image
{
	unsigned *px; int w, h;		// 0xAARRGGBB
	unsigned char *data; unsigned len; bool jpeg;	// (0: none -- written as PNG from the pixels)
	unsigned *cache; int cw, ch;	// (the view's: scaled to its size on the screen)
};

enum { AL_LEFT, AL_CENTER, AL_RIGHT, AL_JUSTIFY };
enum { LS_NONE, LS_BULLET, LS_NUMBER };
enum { ST_NORMAL, ST_H1, ST_H2, ST_H3, ST_TITLE, ST_SUBTITLE, ST_QUOTE, ST_CODE, ST_COUNT };

struct ParaFmt
{
	unsigned char style, align, list, level;	// ST_*, AL_*, LS_*, the list's level (0..5)
	short left, right, first;	// the indents, twips (first: the first line's, from left; < 0 hanging)
	short before, after;		// the spacing above / below, twips
	short line;			// line spacing, % of single (100, 115, 150, 200)
	bool pageBreak;			// starts a page
	bool keepNext;			// on the page of the next paragraph (the headings)
	bool same (const ParaFmt &o) const
	{
		return style == o.style && align == o.align && list == o.list && level == o.level && left == o.left
		    && right == o.right && first == o.first && before == o.before && after == o.after && line == o.line
		    && pageBreak == o.pageBreak && keepNext == o.keepNext;
	}
};

// The styles (a paragraph's; its characters take the style's font, size and weight).
struct Style
{
	const char *name, *font;	// its name (as RTF's stylesheet writes it), its font
	short size; unsigned short flags; unsigned color;
	unsigned char align; short before, after, line, left, right; bool keepNext;
};
static const Style STYLES[ST_COUNT] = {
	{ "Normal",     "Liberation Serif", 24, 0,                    AUTO,     AL_LEFT,   0,   120, 115, 0,   0,   false },
	{ "Heading 1",  "Liberation Sans",  36, CF_BOLD,              AUTO,     AL_LEFT,   360, 120, 100, 0,   0,   true },
	{ "Heading 2",  "Liberation Sans",  30, CF_BOLD,              AUTO,     AL_LEFT,   280, 120, 100, 0,   0,   true },
	{ "Heading 3",  "Liberation Sans",  26, CF_BOLD,              AUTO,     AL_LEFT,   240, 80,  100, 0,   0,   true },
	{ "Title",      "Liberation Sans",  56, CF_BOLD,              AUTO,     AL_CENTER, 240, 240, 100, 0,   0,   true },
	{ "Subtitle",   "Liberation Sans",  32, 0,                    0x595959, AL_CENTER, 0,   240, 100, 0,   0,   true },
	{ "Quote",      "Liberation Serif", 24, CF_ITALIC,            0x404040, AL_LEFT,   120, 240, 115, 720, 720, false },
	{ "Plain Text", "DejaVu Sans Mono", 20, 0,                    AUTO,     AL_LEFT,   0,   0,   100, 0,   0,   false },
};

static ParaFmt style_para (int st)
{
	const Style &s = STYLES[st];
	ParaFmt p;
	p.style = (unsigned char) st; p.align = s.align; p.list = LS_NONE; p.level = 0;
	p.left = s.left; p.right = s.right; p.first = 0; p.before = s.before; p.after = s.after; p.line = s.line;
	p.pageBreak = false; p.keepNext = s.keepNext;
	return p;
}

// ---- the page ----------------------------------------------------------------------------------------
struct PageSetup
{
	int w, h;			// twips (A4: 11906 x 16838)
	int top, bottom, left, right;	// margins, twips
	bool numbers;			// "page n" at the bottom, centred
};
static const int A4_W = 11906, A4_H = 16838;

// ---- positions -----------------------------------------------------------------------------------------
struct Pos
{
	int p, o;			// paragraph, offset
	bool operator== (const Pos &b) const { return p == b.p && o == b.o; }
	bool operator!= (const Pos &b) const { return !(*this == b); }
	bool operator< (const Pos &b) const { return p < b.p || (p == b.p && o < b.o); }
	bool operator<= (const Pos &b) const { return !(b < *this); }
};
static inline Pos mkpos (int p, int o) { Pos r; r.p = p; r.o = o; return r; }

// ---- a paragraph ---------------------------------------------------------------------------------------
struct Line;					// (layout.h)

struct Para
{
	unsigned *ch; unsigned short *cf;	// its characters and their formats
	int len, cap;
	unsigned short endCf;		// its mark's format: what is typed in an empty paragraph / at its end
	ParaFmt pf;
	// (the layout's: layout.h)
	Line *ln; int nln, lncap;	// its lines
	int *xs; int xscap;		// each character's x (and its end's), 1/64 px from the text's left edge
	int h64;			// its lines' height (1/64 px, at the zoom)
	int num;			// its number in a numbered list
	bool dirty;			// to be laid out again
};

static Para *para_new (int cap = 16)
{
	Para *q = new Para;
	q->cap = cap < 4 ? 4 : cap; q->len = 0;
	q->ch = new unsigned[q->cap]; q->cf = new unsigned short[q->cap];
	q->endCf = 0; q->pf = style_para (ST_NORMAL);
	q->ln = 0; q->nln = q->lncap = 0; q->xs = 0; q->xscap = 0; q->h64 = 0; q->num = 0; q->dirty = true;
	return q;
}
static void para_free (Para *q);		// (layout.h frees the lines too)
static void para_reserve (Para *q, int n)
{
	if (n <= q->cap) return;
	int c = q->cap * 2; if (c < n) c = n;
	unsigned *ch = new unsigned[c]; unsigned short *cf = new unsigned short[c];
	for (int i = 0; i < q->len; i++) { ch[i] = q->ch[i]; cf[i] = q->cf[i]; }
	delete[] q->ch; delete[] q->cf;
	q->ch = ch; q->cf = cf; q->cap = c;
}
static Para *para_copy (const Para *s)
{
	Para *q = para_new (s->len + 4);
	for (int i = 0; i < s->len; i++) { q->ch[i] = s->ch[i]; q->cf[i] = s->cf[i]; }
	q->len = s->len; q->endCf = s->endCf; q->pf = s->pf;
	return q;
}
// Insert n characters of format f at offset o.
static void para_insert (Para *q, int o, const unsigned *s, int n, unsigned short f)
{
	para_reserve (q, q->len + n);
	for (int i = q->len - 1; i >= o; i--) { q->ch[i + n] = q->ch[i]; q->cf[i + n] = q->cf[i]; }
	for (int i = 0; i < n; i++) { q->ch[o + i] = s[i]; q->cf[o + i] = f; }
	q->len += n; q->dirty = true;
}
static void para_erase (Para *q, int a, int b)
{
	if (b <= a) return;
	for (int i = b; i < q->len; i++) { q->ch[i - (b - a)] = q->ch[i]; q->cf[i - (b - a)] = q->cf[i]; }
	q->len -= b - a; q->dirty = true;
}

// ---- the document --------------------------------------------------------------------------------------
struct Undo
{
	int first, nOld, nNew;		// the paragraphs [first, first + nOld) became [first, first + nNew)
	Para **old;			// the former ones
	Pos caret0, anchor0, caret1, anchor1;	// the selection before / after
	int kind;			// ED_* (typing coalesces)
};
enum { ED_OTHER, ED_TYPE, ED_DELETE, ED_BACKSPACE };
enum { MAXUNDO = 200 };

struct Doc
{
	Para **p; int n, cap;
	CharFmt *fmt; int nfmt, fmtcap;
	char (*fontName)[48]; int nfont, fontcap;
	Image *img; int nimg, imgcap;	// the images (only ever added: an undone edit's stay unused)
	PageSetup page;
	Undo *undo; int nundo, undocap, uptr;	// [0, uptr): done ones (undo), [uptr, nundo): undone (redo)
	// (an edit under way)
	int edFirst, edN; Para **edOld; Pos edCaret, edAnchor; int edKind;
	unsigned changes;		// counts the edits (the saved state: its count)
};

static void doc_init (Doc &d)
{
	d.p = 0; d.n = d.cap = 0;
	d.fmt = 0; d.nfmt = d.fmtcap = 0;
	d.fontName = 0; d.nfont = d.fontcap = 0;
	d.img = 0; d.nimg = d.imgcap = 0;
	d.page.w = A4_W; d.page.h = A4_H; d.page.top = d.page.bottom = 1134; d.page.left = d.page.right = 1134;	// 2 cm
	d.page.numbers = false;
	d.undo = 0; d.nundo = d.undocap = d.uptr = 0;
	d.edFirst = -1; d.edN = 0; d.edOld = 0; d.edKind = ED_OTHER;
	d.changes = 0;
}

static void undo_free (Undo &u) { for (int i = 0; i < u.nOld; i++) para_free (u.old[i]); delete[] u.old; u.old = 0; u.nOld = 0; }

static void doc_clear (Doc &d)
{
	for (int i = 0; i < d.n; i++) para_free (d.p[i]);
	delete[] d.p; delete[] d.fmt; delete[] d.fontName;
	for (int i = 0; i < d.nimg; i++) { delete[] d.img[i].px; delete[] d.img[i].data; delete[] d.img[i].cache; }
	delete[] d.img;
	for (int i = 0; i < d.nundo; i++) undo_free (d.undo[i]);
	delete[] d.undo;
	if (d.edOld) { for (int i = 0; i < d.edN; i++) para_free (d.edOld[i]); delete[] d.edOld; }
	doc_init (d);
}

// The font table: a name's index (added if new).
static int doc_font (Doc &d, const char *name)
{
	for (int i = 0; i < d.nfont; i++) if (sicmp (d.fontName[i], name) == 0) return i;
	if (d.nfont == d.fontcap)
	{
		int c = d.fontcap ? d.fontcap * 2 : 8;
		char (*t)[48] = new char[c][48];
		for (int i = 0; i < d.nfont; i++) scpy (t[i], d.fontName[i], 48);
		delete[] d.fontName; d.fontName = t; d.fontcap = c;
	}
	scpy (d.fontName[d.nfont], name, 48);
	return d.nfont++;
}

// A format's index in the table (added if new).
static unsigned short doc_fmt (Doc &d, const CharFmt &f)
{
	for (int i = 0; i < d.nfmt; i++) if (d.fmt[i].same (f)) return (unsigned short) i;
	if (d.nfmt >= 65000) return 0;
	if (d.nfmt == d.fmtcap)
	{
		int c = d.fmtcap ? d.fmtcap * 2 : 32;
		CharFmt *t = new CharFmt[c];
		for (int i = 0; i < d.nfmt; i++) t[i] = d.fmt[i];
		delete[] d.fmt; d.fmt = t; d.fmtcap = c;
	}
	d.fmt[d.nfmt] = f;
	return (unsigned short) d.nfmt++;
}

// An image added (its pixels and file bytes taken over): its index.
static int doc_image (Doc &d, unsigned *px, int w, int h, unsigned char *data, unsigned len, bool jpeg)
{
	if (d.nimg == d.imgcap)
	{
		int c = d.imgcap ? d.imgcap * 2 : 4;
		Image *t = new Image[c];
		for (int i = 0; i < d.nimg; i++) t[i] = d.img[i];
		delete[] d.img; d.img = t; d.imgcap = c;
	}
	Image &im = d.img[d.nimg];
	im.px = px; im.w = w; im.h = h; im.data = data; im.len = len; im.jpeg = jpeg;
	im.cache = 0; im.cw = im.ch = 0;
	return d.nimg++;
}
static int doc_image_copy (Doc &d, const Image &s)
{
	unsigned *px = new unsigned[s.w * s.h];
	for (int i = 0; i < s.w * s.h; i++) px[i] = s.px[i];
	unsigned char *data = 0;
	if (s.data) { data = new unsigned char[s.len]; for (unsigned i = 0; i < s.len; i++) data[i] = s.data[i]; }
	return doc_image (d, px, s.w, s.h, data, s.len, s.jpeg);
}

// A style's character format.
static CharFmt style_fmt (Doc &d, int st)
{
	const Style &s = STYLES[st];
	CharFmt f; f.font = (short) doc_font (d, s.font); f.size = s.size; f.flags = s.flags; f.color = s.color; f.hilite = AUTO;
	return f;
}

static void doc_reserve (Doc &d, int n)
{
	if (n <= d.cap) return;
	int c = d.cap ? d.cap * 2 : 64; if (c < n) c = n;
	Para **t = new Para *[c];
	for (int i = 0; i < d.n; i++) t[i] = d.p[i];
	delete[] d.p; d.p = t; d.cap = c;
}
// Put paragraph q at index i (the ones from i shift down).
static void doc_put (Doc &d, int i, Para *q)
{
	doc_reserve (d, d.n + 1);
	for (int k = d.n; k > i; k--) d.p[k] = d.p[k - 1];
	d.p[i] = q; d.n++;
}
// Take paragraph i out (not freed).
static Para *doc_take (Doc &d, int i)
{
	Para *q = d.p[i];
	for (int k = i; k < d.n - 1; k++) d.p[k] = d.p[k + 1];
	d.n--;
	return q;
}

// An empty document: one Normal paragraph.
static void doc_new (Doc &d)
{
	doc_clear (d);
	Para *q = para_new ();
	CharFmt f = style_fmt (d, ST_NORMAL);
	q->endCf = doc_fmt (d, f);
	doc_put (d, 0, q);
}

static inline Pos doc_end (const Doc &d) { return mkpos (d.n - 1, d.p[d.n - 1]->len); }
static inline Pos doc_clamp (const Doc &d, Pos a)
{
	a.p = wclamp (a.p, 0, d.n - 1);
	a.o = wclamp (a.o, 0, d.p[a.p]->len);
	return a;
}

// The format at a place: the character's before it (the one typed there takes it), else the one
// after it, else the paragraph mark's.
static unsigned short doc_cf_at (Doc &d, Pos a)
{
	const Para *q = d.p[a.p];
	unsigned short f = a.o > 0 && a.o <= q->len ? q->cf[a.o - 1] : q->len > 0 ? q->cf[0] : q->endCf;
	if (d.fmt[f].obj)					// (after an image: its text's format, not the image)
	{
		CharFmt t = d.fmt[f]; t.obj = 0; t.ow = t.oh = 0;
		f = doc_fmt (d, t);
	}
	return f;
}

// ---- edits (and their undoing) ----------------------------------------------------------------------------
// begin: the paragraphs [first, first + count) are about to change (copied); end: they are now
// [first, first + nNew). kind: typing coalesces with the edit before it.
static void doc_begin (Doc &d, int first, int count, Pos caret, Pos anchor, int kind)
{
	if (d.edOld) { for (int i = 0; i < d.edN; i++) para_free (d.edOld[i]); delete[] d.edOld; }
	d.edFirst = first; d.edN = count; d.edKind = kind; d.edCaret = caret; d.edAnchor = anchor;
	d.edOld = new Para *[count > 0 ? count : 1];
	for (int i = 0; i < count; i++) d.edOld[i] = para_copy (d.p[first + i]);
}

static void doc_end_edit (Doc &d, int nNew, Pos caret, Pos anchor)
{
	d.changes++;
	for (int i = d.uptr; i < d.nundo; i++) undo_free (d.undo[i]);	// (the redo list is gone)
	d.nundo = d.uptr;
	// Typing: one edit with the one before it (the same single paragraph, just after it).
	if (d.nundo > 0 && (d.edKind == ED_TYPE || d.edKind == ED_DELETE || d.edKind == ED_BACKSPACE))
	{
		Undo &u = d.undo[d.nundo - 1];
		if (u.kind == d.edKind && u.first == d.edFirst && u.nNew == 1 && d.edN == 1 && nNew == 1 && u.caret1 == d.edCaret)
		{
			u.caret1 = caret; u.anchor1 = anchor;
			for (int i = 0; i < d.edN; i++) para_free (d.edOld[i]);
			delete[] d.edOld; d.edOld = 0;
			return;
		}
	}
	if (d.nundo == MAXUNDO)					// (the oldest forgotten)
	{
		undo_free (d.undo[0]);
		for (int i = 1; i < d.nundo; i++) d.undo[i - 1] = d.undo[i];
		d.nundo--;
	}
	if (d.nundo == d.undocap)
	{
		int c = d.undocap ? d.undocap * 2 : 32;
		Undo *t = new Undo[c];
		for (int i = 0; i < d.nundo; i++) t[i] = d.undo[i];
		delete[] d.undo; d.undo = t; d.undocap = c;
	}
	Undo &u = d.undo[d.nundo++];
	u.first = d.edFirst; u.nOld = d.edN; u.nNew = nNew; u.old = d.edOld; u.kind = d.edKind;
	u.caret0 = d.edCaret; u.anchor0 = d.edAnchor; u.caret1 = caret; u.anchor1 = anchor;
	d.edOld = 0;
	d.uptr = d.nundo;
}

// Break the typing coalescing (the caret moved).
static void doc_seal (Doc &d) { if (d.uptr > 0) d.undo[d.uptr - 1].kind = ED_OTHER; }

// Swap an edit's paragraphs with the current ones (undo, then redo, the same record).
static void undo_swap (Doc &d, Undo &u)
{
	Para **cur = new Para *[u.nNew > 0 ? u.nNew : 1];
	for (int i = 0; i < u.nNew; i++) cur[i] = doc_take (d, u.first);
	for (int i = 0; i < u.nOld; i++) doc_put (d, u.first + i, u.old[i]);
	delete[] u.old;
	u.old = cur;
	int t = u.nOld; u.nOld = u.nNew; u.nNew = t;
	Pos c = u.caret0; u.caret0 = u.caret1; u.caret1 = c;
	Pos a = u.anchor0; u.anchor0 = u.anchor1; u.anchor1 = a;
	for (int i = 0; i < u.nNew; i++) d.p[u.first + i]->dirty = true;
	u.kind = ED_OTHER;
	d.changes++;
}
static bool doc_undo (Doc &d, Pos &caret, Pos &anchor)
{
	if (d.uptr == 0) return false;
	Undo &u = d.undo[--d.uptr];
	undo_swap (d, u);
	caret = u.caret1; anchor = u.anchor1;		// (swapped: the "before")
	return true;
}
static bool doc_redo (Doc &d, Pos &caret, Pos &anchor)
{
	if (d.uptr >= d.nundo) return false;
	Undo &u = d.undo[d.uptr++];
	undo_swap (d, u);
	caret = u.caret1; anchor = u.anchor1;
	return true;
}

// ---- text edits (no undo record: the caller brackets them) -------------------------------------------------
// Insert s[0..n) at a with format f ('\n': a new paragraph, the format of the one split); the
// place after it returned.
static Pos doc_insert_raw (Doc &d, Pos a, const unsigned *s, int n, unsigned short f)
{
	int i = 0;
	while (i < n)
	{
		int j = i;
		while (j < n && s[j] != '\n') j++;
		if (j > i) { para_insert (d.p[a.p], a.o, s + i, j - i, f); a.o += j - i; }
		if (j < n)					// a paragraph break
		{
			Para *q = d.p[a.p], *r = para_new (q->len - a.o + 4);
			for (int k = a.o; k < q->len; k++) { r->ch[k - a.o] = q->ch[k]; r->cf[k - a.o] = q->cf[k]; }
			r->len = q->len - a.o; r->pf = q->pf; r->pf.pageBreak = false; r->endCf = q->endCf;
			q->len = a.o; q->dirty = true;
			if (a.o > 0) q->endCf = q->cf[a.o - 1];
			doc_put (d, a.p + 1, r);
			a = mkpos (a.p + 1, 0);
			j++;
		}
		i = j;
	}
	return a;
}

// Delete [a, b) (a <= b): the paragraphs joined (the first one's format kept).
static void doc_erase_raw (Doc &d, Pos a, Pos b)
{
	if (!(a < b)) return;
	if (a.p == b.p) { para_erase (d.p[a.p], a.o, b.o); return; }
	Para *q = d.p[a.p], *r = d.p[b.p];
	q->len = a.o;
	int tail = r->len - b.o;
	para_reserve (q, q->len + tail);
	for (int k = 0; k < tail; k++) { q->ch[q->len + k] = r->ch[b.o + k]; q->cf[q->len + k] = r->cf[b.o + k]; }
	q->len += tail; q->endCf = r->endCf; q->dirty = true;
	for (int k = b.p; k > a.p; k--) para_free (doc_take (d, k));
}

// ---- a piece of a document (the clipboard's) -----------------------------------------------------------
// The text [a, b) as a document of its own (its formats and fonts copied), and put back.
static void doc_extract (Doc &d, Pos a, Pos b, Doc &out)
{
	doc_clear (out);
	for (int p = a.p; p <= b.p; p++)
	{
		const Para *s = d.p[p];
		int o0 = p == a.p ? a.o : 0, o1 = p == b.p ? b.o : s->len;
		Para *q = para_new (o1 - o0 + 4);
		for (int k = o0; k < o1; k++)
		{
			CharFmt f = d.fmt[s->cf[k]];
			f.font = (short) doc_font (out, d.fontName[f.font]);
			if (f.obj) f.obj = doc_image_copy (out, d.img[f.obj - 1]) + 1;
			q->ch[k - o0] = s->ch[k]; q->cf[k - o0] = doc_fmt (out, f);
		}
		q->len = o1 - o0; q->pf = s->pf;
		CharFmt e = d.fmt[p == b.p && b.o > 0 ? s->cf[b.o - 1] : s->endCf];
		e.font = (short) doc_font (out, d.fontName[e.font]);
		e.obj = 0; e.ow = e.oh = 0;
		q->endCf = doc_fmt (out, e);
		doc_put (out, out.n, q);
	}
}

// Insert a document's text at a: its first paragraph joins the one at a, its last one takes the
// rest of it (whole paragraphs keep their formats); the place after it returned.
static Pos doc_insert_doc_raw (Doc &d, Pos a, const Doc &s)
{
	if (s.n == 0) return a;
	unsigned short *map = new unsigned short[s.nfmt > 0 ? s.nfmt : 1];
	for (int i = 0; i < s.nfmt; i++)
	{
		CharFmt f = s.fmt[i];
		f.font = (short) doc_font (d, s.fontName[f.font]);
		if (f.obj) f.obj = doc_image_copy (d, s.img[f.obj - 1]) + 1;
		map[i] = doc_fmt (d, f);
	}
	for (int p = 0; p < s.n; p++)
	{
		const Para *q = s.p[p];
		Para *t = d.p[a.p];
		if (p > 0)					// (a paragraph break first)
		{
			unsigned nl = '\n';
			a = doc_insert_raw (d, a, &nl, 1, doc_cf_at (d, a));
			t = d.p[a.p];
			t->pf = q->pf;
		}
		else if (a.o == 0 && t->len == 0 && s.n > 1) t->pf = q->pf;
		para_reserve (t, t->len + q->len);
		for (int k = t->len - 1; k >= a.o; k--) { t->ch[k + q->len] = t->ch[k]; t->cf[k + q->len] = t->cf[k]; }
		for (int k = 0; k < q->len; k++) { t->ch[a.o + k] = q->ch[k]; t->cf[a.o + k] = map[q->cf[k]]; }
		t->len += q->len; t->dirty = true;
		if (t->len == q->len) t->endCf = map[q->endCf];
		a.o += q->len;
	}
	delete[] map;
	return a;
}

// ---- formats over a range ---------------------------------------------------------------------------------
// A change of character format: what it sets.
enum { CH_FONT = 1, CH_SIZE = 2, CH_FLAGS = 4, CH_COLOR = 8, CH_HILITE = 16, CH_GROW = 32, CH_OBJSIZE = 64 };
struct CfChange
{
	int what;
	int ow, oh;			// CH_OBJSIZE: the images' size (twips)
	short font, size;		// CH_FONT, CH_SIZE (half-points); CH_GROW: size += size (a step)
	unsigned short setFlags, clearFlags;	// CH_FLAGS
	unsigned color, hilite;
};
static const short SIZES[] = { 16, 18, 20, 22, 24, 28, 32, 36, 40, 44, 48, 52, 56, 72, 96, 144 };	// (half-points)
static short grow_size (short s, int dir)
{
	int n = (int) (sizeof SIZES / sizeof SIZES[0]);
	if (dir > 0) { for (int i = 0; i < n; i++) if (SIZES[i] > s) return SIZES[i]; return (short) wmin (s + 24, 3276); }
	for (int i = n - 1; i >= 0; i--) if (SIZES[i] < s) return SIZES[i];
	return (short) wmax (s - 2, 2);
}
static CharFmt cf_apply (const CharFmt &f0, const CfChange &c)
{
	CharFmt f = f0;
	if (c.what & CH_FONT) f.font = c.font;
	if (c.what & CH_SIZE) f.size = c.size;
	if (c.what & CH_GROW) f.size = grow_size (f.size, c.size);
	if (c.what & CH_FLAGS)
	{
		f.flags = (unsigned short) ((f.flags & ~c.clearFlags) | c.setFlags);
		if (c.setFlags & CF_SUPER) f.flags &= (unsigned short) ~CF_SUB;
		if (c.setFlags & CF_SUB) f.flags &= (unsigned short) ~CF_SUPER;
	}
	if (c.what & CH_COLOR) f.color = c.color;
	if (c.what & CH_HILITE) f.hilite = c.hilite;
	if ((c.what & CH_OBJSIZE) && f.obj) { f.ow = c.ow; f.oh = c.oh; }
	return f;
}
// Apply it to [a, b) (a < b), or to the paragraph mark when a == b at a paragraph's end.
static void doc_format_raw (Doc &d, Pos a, Pos b, const CfChange &c)
{
	unsigned short memo[2] = { 0xFFFF, 0 };			// (the last format mapped)
	for (int p = a.p; p <= b.p; p++)
	{
		Para *q = d.p[p];
		int o0 = p == a.p ? a.o : 0, o1 = p == b.p ? b.o : q->len;
		for (int k = o0; k < o1; k++)
		{
			if (q->cf[k] != memo[0]) { memo[0] = q->cf[k]; memo[1] = doc_fmt (d, cf_apply (d.fmt[q->cf[k]], c)); }
			q->cf[k] = memo[1];
		}
		if (p < b.p || o1 == q->len) q->endCf = doc_fmt (d, cf_apply (d.fmt[q->endCf], c));
		q->dirty = true;
	}
}

// A paragraph's style applied: its format's, its characters' font, size, weight and slant (and a
// colour of the former style's taken off).
static void para_set_style (Doc &d, Para *q, int st)
{
	int old = q->pf.style;
	ParaFmt pf = style_para (st);
	pf.list = q->pf.list; pf.level = q->pf.level;
	if (q->pf.list) { pf.left = q->pf.left; pf.first = q->pf.first; }
	pf.pageBreak = q->pf.pageBreak;
	if (st == old) { pf.align = q->pf.align; }
	q->pf = pf;
	CharFmt sf = style_fmt (d, st);
	unsigned oldColor = STYLES[old].color;
	unsigned short memo[2] = { 0xFFFF, 0 };
	for (int k = -1; k < q->len; k++)
	{
		unsigned short &cf = k < 0 ? q->endCf : q->cf[k];
		if (cf != memo[0])
		{
			memo[0] = cf;
			CharFmt f = d.fmt[cf];
			f.font = sf.font; f.size = sf.size;
			f.flags = (unsigned short) ((f.flags & ~(CF_BOLD | CF_ITALIC)) | sf.flags);
			if (sf.color != AUTO || f.color == oldColor) f.color = sf.color;
			memo[1] = doc_fmt (d, f);
		}
		cf = memo[1];
	}
	q->dirty = true;
}

// ---- text ---------------------------------------------------------------------------------------------
static inline bool is_space (unsigned c) { return c == ' ' || c == '\t' || c == 0xA0 || c == 0x0B; }
static inline bool is_word (unsigned c)
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'
	    || (c >= 0xC0 && c != 0xD7 && c != 0xF7 && c < 0x2000) || c == '\'' || c == 0x2019;
}

// The words, characters (without / with spaces) and paragraphs of [a, b).
struct Counts { int words, chars, charsSp, paras; };
static Counts doc_count (const Doc &d, Pos a, Pos b)
{
	Counts c = { 0, 0, 0, 0 };
	for (int p = a.p; p <= b.p; p++)
	{
		const Para *q = d.p[p];
		int o0 = p == a.p ? a.o : 0, o1 = p == b.p ? b.o : q->len;
		bool in = false, any = false;
		for (int k = o0; k < o1; k++)
		{
			unsigned ch = q->ch[k];
			c.charsSp++;
			if (!is_space (ch) && ch != OBJ_CHAR) { c.chars++; any = true; }
			bool w = !is_space (ch) && ch != '-' && ch != 0x2013 && ch != 0x2014 && ch != OBJ_CHAR;
			if (w && !in) c.words++;
			in = w;
		}
		if (any) c.paras++;
	}
	return c;
}

// UTF-8 / Latin-1 text -> code points (valid UTF-8 with a multi-byte character: UTF-8; else Latin-1).
static int decode_text (const char *s, int n, unsigned *out, int cap)
{
	bool utf8 = true, multi = false;
	for (int i = 0; i < n; )
	{
		unsigned char c = (unsigned char) s[i];
		int k = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
		if (k < 0 || i + k >= n + (k ? 0 : 1)) { utf8 = false; break; }
		for (int j = 1; j <= k; j++) if (((unsigned char) s[i + j] & 0xC0) != 0x80) { utf8 = false; break; }
		if (!utf8) break;
		if (k) multi = true;
		i += k + 1;
	}
	int m = 0;
	for (int i = 0; i < n && m < cap; )
	{
		unsigned c = (unsigned char) s[i];
		if (utf8 && multi && c >= 0x80)
		{
			int k = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : 3;
			c &= k == 1 ? 0x1F : k == 2 ? 0x0F : 0x07;
			for (int j = 1; j <= k && i + j < n; j++) c = c << 6 | ((unsigned char) s[i + j] & 0x3F);
			i += k + 1;
		}
		else i++;
		if (c == '\r') { if (i < n && s[i] == '\n') continue; c = '\n'; }
		out[m++] = c;
	}
	return m;
}
// Code points -> Latin-1 when they all fit, else UTF-8 (the system's text is Latin-1). Returns the
// length (out: at most cap - 1, NUL-terminated).
static int encode_text (const unsigned *s, int n, char *out, int cap)
{
	bool wide = false;
	for (int i = 0; i < n; i++) if (s[i] > 0xFF && s[i] != OBJ_CHAR) { wide = true; break; }
	int m = 0;
	for (int i = 0; i < n; i++)
	{
		unsigned c = s[i];
		if (c == 0x0B) c = '\n';
		if (c == OBJ_CHAR) continue;			// (an image: not text)
		if (!wide || c < 0x80) { if (m + 1 >= cap) break; out[m++] = (char) c; continue; }
		if (c < 0x800) { if (m + 2 >= cap) break; out[m++] = (char) (0xC0 | c >> 6); out[m++] = (char) (0x80 | (c & 0x3F)); }
		else if (c < 0x10000) { if (m + 3 >= cap) break; out[m++] = (char) (0xE0 | c >> 12); out[m++] = (char) (0x80 | (c >> 6 & 0x3F)); out[m++] = (char) (0x80 | (c & 0x3F)); }
		else { if (m + 4 >= cap) break; out[m++] = (char) (0xF0 | c >> 18); out[m++] = (char) (0x80 | (c >> 12 & 0x3F)); out[m++] = (char) (0x80 | (c >> 6 & 0x3F)); out[m++] = (char) (0x80 | (c & 0x3F)); }
	}
	if (cap > 0) out[m < cap ? m : cap - 1] = 0;
	return m;
}

// The plain text of [a, b) (paragraphs ended by '\n').
static int doc_text (const Doc &d, Pos a, Pos b, unsigned *out, int cap)
{
	int m = 0;
	for (int p = a.p; p <= b.p && m < cap; p++)
	{
		const Para *q = d.p[p];
		int o0 = p == a.p ? a.o : 0, o1 = p == b.p ? b.o : q->len;
		for (int k = o0; k < o1 && m < cap; k++) out[m++] = q->ch[k];
		if (p < b.p && m < cap) out[m++] = '\n';
	}
	return m;
}
static int doc_text_len (const Doc &d, Pos a, Pos b)
{
	int m = 0;
	for (int p = a.p; p <= b.p; p++) m += (p == b.p ? b.o : d.p[p]->len) - (p == a.p ? a.o : 0) + (p < b.p);
	return m;
}

} // namespace wr

#endif
