//
// model.h -- Slides' presentation: the deck (its size, its theme, its master and layouts, its slides),
// a slide (its objects, its notes, its background, its transition, its effects, its section), an object
// (a text box, a shape, a picture, a table, a chart; a placeholder of the layout: title, body...), the
// text in it (paragraphs of characters, each character with its format; formats left to the master's
// styles: "inherit"), the pictures. Lengths in 1/100 mm (hmm), font sizes in 1/10 pt.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _slides_model_h
#define _slides_model_h

#include "Apps/sheet/core.h"
#include "imagekit/img/imgload.hpp"

namespace sl {

using ss::Buf; using ss::imin; using ss::imax; using ss::iclamp; using ss::scpy; using ss::scat; using ss::sdup;

// ---- a growing array ----------------------------------------------------------------------------------------
template <class T> struct Vec
{
	T *a; int n, cap;
	Vec () : a (0), n (0), cap (0) {}
	~Vec () { free (a); }
	void reserve (int m) { if (m <= cap) return; int c = cap ? cap * 2 : 8; while (c < m) c *= 2; a = (T *) realloc (a, sizeof (T) * c); cap = c; }
	void push (const T &v) { reserve (n + 1); a[n++] = v; }
	void insert (int i, const T &v) { reserve (n + 1); memmove (a + i + 1, a + i, sizeof (T) * (n - i)); a[i] = v; n++; }
	void erase (int i) { memmove (a + i, a + i + 1, sizeof (T) * (n - i - 1)); n--; }
	void clear () { n = 0; }
	T &operator[] (int i) { return a[i]; }
	const T &operator[] (int i) const { return a[i]; }
	int find (const T &v) const { for (int i = 0; i < n; i++) if (a[i] == v) return i; return -1; }
private:
	Vec (const Vec &);
	Vec &operator= (const Vec &);
};

// ---- units ----------------------------------------------------------------------------------------------------
static const int HMM_PER_PT10 = 0;			// (see pt10_hmm)
static inline double pt10_hmm (int pt10) { return pt10 * 3.5277778; }	// 1/10 pt -> 1/100 mm
static inline int hmm_pt10 (double hmm) { return (int) (hmm / 3.5277778 + 0.5); }

// ---- colours --------------------------------------------------------------------------------------------------
// 0x00RRGGBB, AUTO (inherit / automatic), or one of the theme's colours: THEME | index (TC_*).
static const unsigned AUTO = 0xFF000000u;
static const unsigned THEME = 0xFE000000u;
enum { TC_DK1, TC_LT1, TC_DK2, TC_LT2, TC_ACC1, TC_ACC2, TC_ACC3, TC_ACC4, TC_ACC5, TC_ACC6, TC_LINK, TC_COUNT };
static inline bool is_theme (unsigned c) { return (c & 0xFF000000u) == THEME; }

// ---- the text ---------------------------------------------------------------------------------------------------
enum { CF_BOLD = 1, CF_ITALIC = 2, CF_UNDER = 4, CF_STRIKE = 8, CF_SUPER = 16, CF_SUB = 32 };
enum { FONT_INHERIT = -1, FONT_MAJOR = -2, FONT_MINOR = -3 };	// (else an index of Deck::font)
struct CharFmt
{
	short font;			// FONT_* or a font of the deck's table
	short size;			// 1/10 pt; 0: inherit
	unsigned color;			// AUTO: inherit
	unsigned short flags;		// CF_*
	unsigned short set;		// the CF_* given here (the others: inherit)
};
static inline CharFmt cf_inherit () { CharFmt c; c.font = FONT_INHERIT; c.size = 0; c.color = AUTO; c.flags = 0; c.set = 0; return c; }
static inline bool cf_same (const CharFmt &a, const CharFmt &b) { return a.font == b.font && a.size == b.size && a.color == b.color && a.flags == b.flags && a.set == b.set; }

enum { AL_INHERIT = -1, AL_LEFT, AL_CENTER, AL_RIGHT, AL_JUSTIFY };
enum { BU_INHERIT = -1, BU_NONE, BU_BULLET, BU_NUMBER };
struct ParaFmt
{
	signed char align;		// AL_*
	signed char level;		// 0..4: the list's level (the master's body styles)
	signed char bullet;		// BU_*
	short before, after;		// space, 1/10 pt (-1: inherit)
	short spacing;			// the line's height, % (0: inherit)
};
static inline ParaFmt pf_inherit () { ParaFmt p; p.align = AL_INHERIT; p.level = 0; p.bullet = BU_INHERIT; p.before = p.after = -1; p.spacing = 0; return p; }

struct Para
{
	unsigned *ch; CharFmt *cf; int len, cap;
	ParaFmt pf;
	CharFmt end;			// the format of an empty paragraph / typed at its end
	// (layout: text.h)
	Para () : ch (0), cf (0), len (0), cap (0) { pf = pf_inherit (); end = cf_inherit (); }
	~Para () { free (ch); free (cf); }
	void reserve (int m) { if (m <= cap) return; int c = cap ? cap * 2 : 16; while (c < m) c *= 2; ch = (unsigned *) realloc (ch, 4 * c); cf = (CharFmt *) realloc (cf, sizeof (CharFmt) * c); cap = c; }
	void insert (int at, const unsigned *s, int n, const CharFmt &f)
	{
		reserve (len + n);
		memmove (ch + at + n, ch + at, 4 * (len - at)); memmove (cf + at + n, cf + at, sizeof (CharFmt) * (len - at));
		for (int i = 0; i < n; i++) { ch[at + i] = s[i]; cf[at + i] = f; }
		len += n;
	}
	void remove (int a, int b) { memmove (ch + a, ch + b, 4 * (len - b)); memmove (cf + a, cf + b, sizeof (CharFmt) * (len - b)); len -= b - a; }
	CharFmt fmt_at (int o) const { return len == 0 ? end : o > 0 ? cf[o - 1] : cf[0]; }
	void set_utf8 (const char *s, const CharFmt &f)
	{
		len = 0;
		int n = (int) strlen (s);
		reserve (n);
		for (int i = 0; i < n; )
		{
			int l; unsigned c = ss::u8_dec (s + i, n - i, &l);
			ch[len] = c; cf[len] = f; len++; i += l > 0 ? l : 1;
		}
		end = f;
	}
};
static Para *para_copy (const Para *p)
{
	Para *q = new Para;
	q->reserve (p->len);
	memcpy (q->ch, p->ch, 4 * p->len); memcpy (q->cf, p->cf, sizeof (CharFmt) * p->len);
	q->len = p->len; q->pf = p->pf; q->end = p->end;
	return q;
}

enum { AN_TOP, AN_MIDDLE, AN_BOTTOM };
enum { FIT_NONE, FIT_SHRINK, FIT_GROW };	// autofit: none, shrink the text, grow the box
struct TextBody
{
	Vec<Para *> p;
	signed char anchor;		// AN_*
	signed char fit;		// FIT_*
	bool wrap;			// lines broken at the box's width (else: one line each paragraph)
	short inset[4];			// left, top, right, bottom (hmm)
	int scale;			// autofit's text scale, 1/1000 (1000: as it is) -- computed
	TextBody () : anchor (AN_TOP), fit (FIT_NONE), wrap (true), scale (1000) { inset[0] = inset[2] = 250; inset[1] = inset[3] = 125; }
	~TextBody () { clear (); }
	void clear () { for (int i = 0; i < p.n; i++) delete p[i]; p.clear (); }
	bool empty () const { return p.n == 0 || (p.n == 1 && p[0]->len == 0); }
	void ensure () { if (!p.n) p.push (new Para); }
	void copy_from (const TextBody &o)
	{
		clear ();
		for (int i = 0; i < o.p.n; i++) p.push (para_copy (o.p[i]));
		anchor = o.anchor; fit = o.fit; wrap = o.wrap; scale = o.scale;
		for (int i = 0; i < 4; i++) inset[i] = o.inset[i];
	}
	// plain text ("\n" between paragraphs), the format f
	void set_text (const char *s, const CharFmt &f, const ParaFmt *pf = 0)
	{
		clear ();
		const char *b = s;
		for (;;)
		{
			const char *e = strchr (b, '\n');
			Para *q = new Para;
			Buf t; t.putn (b, e ? (int) (e - b) : (int) strlen (b));
			q->set_utf8 (t.str (), f);
			if (pf) q->pf = *pf;
			p.push (q);
			if (!e) break;
			b = e + 1;
		}
	}
	void text_utf8 (Buf &o) const
	{
		for (int i = 0; i < p.n; i++) { if (i) o.put ('\n'); for (int k = 0; k < p[i]->len; k++) o.putu (p[i]->ch[k]); }
	}
private:
	TextBody (const TextBody &);
	TextBody &operator= (const TextBody &);
};

// ---- fills, lines -----------------------------------------------------------------------------------------------
enum { FILL_NONE, FILL_SOLID, FILL_GRADIENT, FILL_INHERIT };
struct Fill
{
	signed char type;		// FILL_*
	unsigned c1, c2;		// colours (gradient: from c1 to c2)
	short angle;			// gradient: degrees (0: left to right, 90: top to bottom)
	unsigned char alpha;		// opacity 0..255
};
static inline Fill fill_none () { Fill f; f.type = FILL_NONE; f.c1 = f.c2 = 0xFFFFFF; f.angle = 90; f.alpha = 255; return f; }
static inline Fill fill_solid (unsigned c) { Fill f = fill_none (); f.type = FILL_SOLID; f.c1 = c; return f; }
enum { LN_NONE, LN_SOLID, LN_DASH, LN_DOT };
enum { AH_NONE, AH_ARROW, AH_OPEN, AH_DOT };	// arrow heads
struct Line
{
	signed char type;		// LN_*
	signed char head0, head1;	// AH_* at the start, at the end (lines)
	unsigned color;
	short width;			// hmm
};
static inline Line line_none () { Line l; l.type = LN_NONE; l.head0 = l.head1 = AH_NONE; l.color = 0x404040; l.width = 26; return l; }
static inline Line line_solid (unsigned c, int w) { Line l = line_none (); l.type = LN_SOLID; l.color = c; l.width = (short) w; return l; }

// ---- the objects ---------------------------------------------------------------------------------------------
enum { OB_TEXT, OB_SHAPE, OB_PICTURE, OB_TABLE, OB_CHART, OB_LINE };
// The shapes (OOXML's preset names in shapes.h).
enum { SH_RECT, SH_ROUND, SH_ELLIPSE, SH_TRIANGLE, SH_RTRIANGLE, SH_DIAMOND, SH_PENTAGON, SH_HEXAGON, SH_OCTAGON,
       SH_STAR5, SH_STAR4, SH_HEART, SH_ARROW_R, SH_ARROW_L, SH_ARROW_U, SH_ARROW_D, SH_ARROW_LR, SH_CHEVRON, SH_HOMEPLATE,
       SH_CALLOUT, SH_CLOUD, SH_PLUS, SH_PARALLELOGRAM, SH_TRAPEZOID, SH_CAN, SH_DONUT, SH_FLOW_DOC, SH_FLOW_TERM,
       SH_COUNT };
enum { PH_NONE, PH_TITLE, PH_SUBTITLE, PH_BODY, PH_BODY2, PH_PICTURE, PH_FOOTER, PH_NUMBER, PH_DATE, PH_COUNT };

struct Table
{
	int rows, cols;
	int *colW, *rowH;			// hmm
	TextBody *cell;			// rows * cols
	Fill *cfill;			// each cell's (FILL_INHERIT: the style's)
	bool header, banded;		// the style: a heading row, banded rows
	Table (int r, int c) : rows (r), cols (c)
	{
		colW = new int[c]; rowH = new int[r]; cell = new TextBody[r * c]; cfill = new Fill[r * c];
		for (int i = 0; i < r * c; i++) { cfill[i] = fill_none (); cfill[i].type = FILL_INHERIT; cell[i].ensure (); }
		header = true; banded = true;
	}
	~Table () { delete[] colW; delete[] rowH; delete[] cell; delete[] cfill; }
	TextBody &at (int r, int c) { return cell[r * cols + c]; }
};

enum { CH_COLUMN, CH_BAR, CH_LINE, CH_PIE, CH_AREA, CH_COUNT };
struct Chart
{
	int type;			// CH_*
	int ncat, nser;
	char title[64];
	char cat[24][24];		// the categories (a series' points)
	char ser[8][32];		// the series' names
	double val[8][24];
	bool legend, labels;
	Chart () : type (CH_COLUMN), ncat (0), nser (0), legend (true), labels (false) { title[0] = 0; }
};

// Effects (the Animate tab)
enum { AC_ENTRANCE, AC_EMPHASIS, AC_EXIT };
enum { FX_APPEAR, FX_FADE, FX_FLY, FX_WIPE, FX_ZOOM, FX_FLOAT, FX_GROW, FX_PULSE, FX_SPIN, FX_COLOR, FX_COUNT };
enum { ST_CLICK, ST_WITH, ST_AFTER };
enum { DIR_NONE, DIR_LEFT, DIR_RIGHT, DIR_UP, DIR_DOWN };	// fly from / wipe from / push from
struct Anim
{
	int obj;			// the object's id
	signed char cls, fx, start, dir;
	bool byPara;			// a text: paragraph by paragraph
	short delay, dur;		// ms
};

enum { TR_NONE, TR_FADE, TR_PUSH, TR_WIPE, TR_COVER, TR_UNCOVER, TR_SPLIT, TR_ZOOM, TR_DISSOLVE, TR_COUNT };
struct Transition
{
	signed char type, dir;		// TR_*, DIR_*
	short dur;			// ms
	int after;			// ms before going on by itself (-1: on a click)
};

struct Object
{
	int id;
	signed char kind;		// OB_*
	signed char shape;		// SH_* (OB_SHAPE, OB_TEXT: a rectangle)
	signed char ph;			// PH_*: a placeholder of the layout
	char name[32];
	int x, y, w, h;			// hmm
	short rot;			// degrees, clockwise
	bool flipH, flipV;		// (a line: its direction)
	short radius;			// SH_ROUND: the corners' radius, % of the short side (x 10)
	Fill fill; Line line;
	bool shadow;
	TextBody tb;			// any shape holds text
	int img;			// OB_PICTURE: the deck's picture
	short crop[4];			// its cropping, 1/1000 of each side
	Table *tbl; Chart *chart;
	Object () : id (0), kind (OB_TEXT), shape (SH_RECT), ph (PH_NONE), x (0), y (0), w (0), h (0), rot (0), flipH (false), flipV (false),
		    radius (160), shadow (false), img (-1), tbl (0), chart (0)
	{ name[0] = 0; fill = fill_none (); line = line_none (); crop[0] = crop[1] = crop[2] = crop[3] = 0; }
	~Object () { delete tbl; delete chart; }
private:
	Object (const Object &);
	Object &operator= (const Object &);
};

struct Slide
{
	int layout;			// the master's layout it follows
	Vec<Object *> obj;		// bottom first
	TextBody notes;
	Fill bg;			// FILL_INHERIT: the master's
	bool masterObjects;		// the master's decorations shown
	Transition tr;
	Vec<Anim> anim;
	bool hidden;
	char section[48];		// a section starts here ("": none)
	Slide () : layout (1), masterObjects (true), hidden (false) { bg = fill_none (); bg.type = FILL_INHERIT; tr.type = TR_NONE; tr.dir = DIR_LEFT; tr.dur = 700; tr.after = -1; section[0] = 0; }
	~Slide () { for (int i = 0; i < obj.n; i++) delete obj[i]; }
	Object *by_id (int id) const { for (int i = 0; i < obj.n; i++) if (obj[i]->id == id) return obj[i]; return 0; }
	int index_of (int id) const { for (int i = 0; i < obj.n; i++) if (obj[i]->id == id) return i; return -1; }
};

// The master's text styles: the title, the body's five levels, the other text (a text box, a shape)
struct TextStyle { CharFmt cf; ParaFmt pf; unsigned bullet; short indent; };	// bullet: its character; indent: hmm a level
enum { TS_TITLE, TS_BODY1, TS_BODY2, TS_BODY3, TS_BODY4, TS_BODY5, TS_OTHER, TS_SUBTITLE, TS_COUNT };

enum { LY_TITLE, LY_CONTENT, LY_TWO, LY_COMPARE, LY_SECTION, LY_TITLE_ONLY, LY_PICTURE, LY_BLANK, LY_COUNT };
struct Layout
{
	char name[32];
	Vec<Object *> ph;		// its placeholders (their place, their prompt)
	Layout () { name[0] = 0; }
	~Layout () { for (int i = 0; i < ph.n; i++) delete ph[i]; }
	Object *find (int kind, int nth = 0) const { for (int i = 0; i < ph.n; i++) if (ph[i]->ph == kind && nth-- == 0) return ph[i]; return 0; }
};

struct Theme
{
	char name[32];
	unsigned col[TC_COUNT];
	char major[48], minor[48];	// the headings' font, the body's
};

struct Picture
{
	char name[64];			// in the file: "Pictures/<name>"
	unsigned char *bytes; unsigned len;	// the file as it came (kept: saved as it is)
	unsigned *px; int w, h;		// decoded (0xAARRGGBB), lazily
	bool failed;
};

struct Deck
{
	int sw, sh;			// the slide's size, hmm
	Theme theme;
	Fill masterBg;
	Vec<Object *> decor;		// the master's own objects (on every slide)
	TextStyle style[TS_COUNT];
	Layout layout[LY_COUNT];
	Vec<Slide *> slides;
	Vec<char *> font;		// the fonts named by the text (CharFmt::font)
	int nextId;
	bool footer, number, date;	// on the slides: the footer, the slide's number, the date
	char footerText[96];
	unsigned changes;		// edits (the document "modified" when it differs from the saved count)
	Deck () : sw (28000), sh (15750), nextId (1), footer (true), number (true), date (false), changes (0) { masterBg = fill_solid (0xFFFFFF); footerText[0] = 0; }
	~Deck () { clear (); }
	void clear ()
	{
		for (int i = 0; i < slides.n; i++) delete slides[i]; slides.clear ();
		for (int i = 0; i < decor.n; i++) delete decor[i]; decor.clear ();
		for (int i = 0; i < font.n; i++) free (font[i]); font.clear ();
	}
	int font_index (const char *name)
	{
		for (int i = 0; i < font.n; i++) if (!strcmp (font[i], name)) return i;
		font.push (sdup (name)); return font.n - 1;
	}
	const char *font_name (int f) const { return f == FONT_MAJOR ? theme.major : f == FONT_MINOR ? theme.minor : f >= 0 && f < font.n ? font[f] : theme.minor; }
	unsigned rgb (unsigned c) const { return is_theme (c) ? theme.col[(c & 0xFF) % TC_COUNT] : c & 0xFFFFFF; }
};

// ---- the pictures (shared by every deck of the program: undo's copies keep the same indexes) ----------------
static Vec<Picture> g_pics;
static int pic_add (const char *name, const unsigned char *b, unsigned n)
{
	for (int i = 0; i < g_pics.n; i++) if (g_pics[i].len == n && !memcmp (g_pics[i].bytes, b, n)) return i;
	Picture p; memset (&p, 0, sizeof p);
	scpy (p.name, name, sizeof p.name);
	p.bytes = (unsigned char *) malloc (n ? n : 1); memcpy (p.bytes, b, n); p.len = n;
	g_pics.push (p);
	return g_pics.n - 1;
}
static Picture *pic (int i)
{
	if (i < 0 || i >= g_pics.n) return 0;
	Picture &p = g_pics[i];
	if (!p.px && !p.failed)
	{
		ImgFrames im;
		if (img_load_mem (p.bytes, p.len, &im) && im.n > 0) { p.px = im.px[0]; p.w = im.w; p.h = im.h; im.px[0] = 0; img_free (&im); }
		else p.failed = true;
	}
	return p.px ? &p : 0;
}

// ---- copies (undo, the clipboard, duplicate) ---------------------------------------------------------------
static Object *obj_copy (const Object *o)
{
	Object *c = new Object;
	c->id = o->id; c->kind = o->kind; c->shape = o->shape; c->ph = o->ph; memcpy (c->name, o->name, sizeof c->name);
	c->x = o->x; c->y = o->y; c->w = o->w; c->h = o->h; c->rot = o->rot; c->flipH = o->flipH; c->flipV = o->flipV;
	c->radius = o->radius; c->fill = o->fill; c->line = o->line; c->shadow = o->shadow;
	c->tb.copy_from (o->tb);
	c->img = o->img; memcpy (c->crop, o->crop, sizeof c->crop);
	if (o->tbl)
	{
		Table *t = new Table (o->tbl->rows, o->tbl->cols);
		memcpy (t->colW, o->tbl->colW, sizeof (int) * t->cols); memcpy (t->rowH, o->tbl->rowH, sizeof (int) * t->rows);
		for (int i = 0; i < t->rows * t->cols; i++) { t->cell[i].copy_from (o->tbl->cell[i]); t->cfill[i] = o->tbl->cfill[i]; }
		t->header = o->tbl->header; t->banded = o->tbl->banded;
		c->tbl = t;
	}
	if (o->chart) { c->chart = new Chart; *c->chart = *o->chart; }
	return c;
}
static Slide *slide_copy (const Slide *s)
{
	Slide *c = new Slide;
	c->layout = s->layout;
	for (int i = 0; i < s->obj.n; i++) c->obj.push (obj_copy (s->obj[i]));
	c->notes.copy_from (s->notes);
	c->bg = s->bg; c->masterObjects = s->masterObjects; c->tr = s->tr;
	for (int i = 0; i < s->anim.n; i++) c->anim.push (s->anim[i]);
	c->hidden = s->hidden; memcpy (c->section, s->section, sizeof c->section);
	return c;
}
static void deck_copy (Deck &d, const Deck &s)
{
	d.clear ();
	d.sw = s.sw; d.sh = s.sh; d.theme = s.theme; d.masterBg = s.masterBg;
	for (int i = 0; i < s.decor.n; i++) d.decor.push (obj_copy (s.decor[i]));
	for (int i = 0; i < TS_COUNT; i++) d.style[i] = s.style[i];
	for (int l = 0; l < LY_COUNT; l++)
	{
		Layout &a = d.layout[l]; const Layout &b = s.layout[l];
		for (int i = 0; i < a.ph.n; i++) delete a.ph[i];
		a.ph.clear (); memcpy (a.name, b.name, sizeof a.name);
		for (int i = 0; i < b.ph.n; i++) a.ph.push (obj_copy (b.ph[i]));
	}
	for (int i = 0; i < s.slides.n; i++) d.slides.push (slide_copy (s.slides[i]));
	for (int i = 0; i < s.font.n; i++) d.font.push (sdup (s.font[i]));
	d.nextId = s.nextId; d.footer = s.footer; d.number = s.number; d.date = s.date;
	memcpy (d.footerText, s.footerText, sizeof d.footerText);
	d.changes = s.changes;
}

// ---- the themes ------------------------------------------------------------------------------------------------
// Café (teal and peach), then the desktop's five colour themes (docs/gui-redesign).
struct ThemeDef { const char *name; unsigned dk2, acc1, acc2, acc3, acc4, lt2; };
static const ThemeDef THEMES[] = {
	{ "Café",  0x18404E, 0x2E6E80, 0xF0A86E, 0x96AABA, 0x5B8C5A, 0xF6F2EE },
	{ "Peach", 0x5A3A22, 0xB06E3C, 0xF0B07A, 0xC9A27E, 0x6E8FA8, 0xFBF3EA },
	{ "Steel", 0x22324A, 0x3C5478, 0x7A98C0, 0xB0BED0, 0xE0A040, 0xF0F3F7 },
	{ "Sage",  0x24361F, 0x466E40, 0x80AA76, 0xC9B26E, 0x6E8CA8, 0xF2F6EF },
	{ "Brick", 0x4A1C1A, 0x963834, 0xC45450, 0xE0A070, 0x5A6E80, 0xF8F0EE },
	{ "Slate", 0x1A1F2A, 0x28303E, 0x7882A0, 0xF0A86E, 0x70A8A0, 0xEEF0F4 },
};
enum { NTHEMES = (int) (sizeof THEMES / sizeof THEMES[0]) };
static void theme_set (Theme &t, int k)
{
	const ThemeDef &d = THEMES[k];
	scpy (t.name, d.name, sizeof t.name);
	t.col[TC_DK1] = 0x1F2A30; t.col[TC_LT1] = 0xFFFFFF; t.col[TC_DK2] = d.dk2; t.col[TC_LT2] = d.lt2;
	t.col[TC_ACC1] = d.acc1; t.col[TC_ACC2] = d.acc2; t.col[TC_ACC3] = d.acc3; t.col[TC_ACC4] = d.acc4;
	t.col[TC_ACC5] = 0x8E6CB0; t.col[TC_ACC6] = 0xC0504D; t.col[TC_LINK] = 0x2E6E80;
	scpy (t.major, "Liberation Sans", sizeof t.major); scpy (t.minor, "Liberation Sans", sizeof t.minor);
}
static int theme_find (const char *name) { for (int i = 0; i < NTHEMES; i++) if (!strcmp (THEMES[i].name, name)) return i; return -1; }

// ---- the master: its styles, its decorations, its layouts ------------------------------------------------------
static const char *const LAYOUT_NAMES[LY_COUNT] = { "Title", "Title and content", "Two contents", "Comparison", "Section header",
						    "Title only", "Picture and text", "Blank" };
static const char *const PH_PROMPTS[PH_COUNT] = { "", "Click to add a title", "Click to add a subtitle", "Click to add text",
						  "Click to add text", "Click to add a picture", "", "", "" };

static Object *ph_new (Deck &d, int kind, int x, int y, int w, int h)
{
	Object *o = new Object;
	o->id = d.nextId++; o->kind = OB_TEXT; o->ph = (signed char) kind;
	o->x = x; o->y = y; o->w = w; o->h = h;
	o->tb.ensure ();
	if (kind == PH_TITLE) { o->tb.anchor = AN_BOTTOM; o->tb.fit = FIT_SHRINK; }
	if (kind == PH_BODY || kind == PH_BODY2) o->tb.fit = FIT_SHRINK;
	if (kind == PH_SUBTITLE) o->tb.anchor = AN_TOP;
	return o;
}

static void master_default (Deck &d, int theme)
{
	theme_set (d.theme, theme);
	d.masterBg = fill_solid (THEME | TC_LT1);
	for (int i = 0; i < d.decor.n; i++) delete d.decor[i];
	d.decor.clear ();
	int W = d.sw, H = d.sh;
	// the decorations: a band of the first accent down the left edge, a thin line over the footer
	Object *band = new Object; band->id = d.nextId++; band->kind = OB_SHAPE; band->shape = SH_RECT;
	band->x = 0; band->y = 0; band->w = W * 11 / 1000; band->h = H; band->fill = fill_solid (THEME | TC_ACC1); scpy (band->name, "Band", sizeof band->name);
	d.decor.push (band);
	Object *rule = new Object; rule->id = d.nextId++; rule->kind = OB_LINE;
	rule->x = W * 57 / 1000; rule->y = H * 935 / 1000; rule->w = W - 2 * W * 57 / 1000; rule->h = 0; rule->line = line_solid (0xE2DEDA, 18);
	scpy (rule->name, "Rule", sizeof rule->name);
	d.decor.push (rule);
	// the styles
	for (int i = 0; i < TS_COUNT; i++) { d.style[i].cf = cf_inherit (); d.style[i].pf = pf_inherit (); d.style[i].bullet = 0x2022; d.style[i].indent = 0; }
	TextStyle &t = d.style[TS_TITLE];
	t.cf.font = FONT_MAJOR; t.cf.size = 400; t.cf.color = THEME | TC_DK2; t.cf.flags = CF_BOLD; t.cf.set = CF_BOLD;
	t.pf.align = AL_LEFT; t.pf.bullet = BU_NONE; t.pf.before = 0; t.pf.after = 0; t.pf.spacing = 90;
	static const short sz[5] = { 240, 200, 180, 160, 160 };
	for (int l = 0; l < 5; l++)
	{
		TextStyle &b = d.style[TS_BODY1 + l];
		b.cf.font = FONT_MINOR; b.cf.size = sz[l]; b.cf.color = THEME | TC_DK1; b.cf.flags = 0; b.cf.set = CF_BOLD | CF_ITALIC;
		b.pf.align = AL_LEFT; b.pf.bullet = BU_BULLET; b.pf.before = l ? 40 : 100; b.pf.after = 0; b.pf.spacing = 100;
		b.bullet = l % 2 ? 0x2013 : 0x2022; b.indent = 800;
	}
	TextStyle &o = d.style[TS_OTHER];
	o.cf.font = FONT_MINOR; o.cf.size = 180; o.cf.color = THEME | TC_DK1; o.cf.set = CF_BOLD | CF_ITALIC;
	o.pf.align = AL_LEFT; o.pf.bullet = BU_NONE; o.pf.before = 0; o.pf.after = 0; o.pf.spacing = 100;
	TextStyle &s = d.style[TS_SUBTITLE];
	s.cf.font = FONT_MINOR; s.cf.size = 240; s.cf.color = THEME | TC_ACC1; s.cf.set = CF_BOLD | CF_ITALIC;
	s.pf.align = AL_LEFT; s.pf.bullet = BU_NONE; s.pf.before = 0; s.pf.after = 0; s.pf.spacing = 100;
	// the layouts
	int mx = W * 57 / 1000, tw = W - 2 * mx;
	int ty = H * 60 / 1000, th = H * 120 / 1000, by = H * 230 / 1000, bh = H * 660 / 1000;
	for (int l = 0; l < LY_COUNT; l++)
	{
		Layout &L = d.layout[l];
		for (int i = 0; i < L.ph.n; i++) delete L.ph[i];
		L.ph.clear ();
		scpy (L.name, LAYOUT_NAMES[l], sizeof L.name);
		switch (l)
		{
		case LY_TITLE:
			L.ph.push (ph_new (d, PH_TITLE, mx, H * 280 / 1000, tw, H * 240 / 1000));
			L.ph.push (ph_new (d, PH_SUBTITLE, mx, H * 540 / 1000, tw, H * 160 / 1000));
			break;
		case LY_CONTENT:
			L.ph.push (ph_new (d, PH_TITLE, mx, ty, tw, th));
			L.ph.push (ph_new (d, PH_BODY, mx, by, tw, bh));
			break;
		case LY_TWO:
			L.ph.push (ph_new (d, PH_TITLE, mx, ty, tw, th));
			L.ph.push (ph_new (d, PH_BODY, mx, by, tw / 2 - 200, bh));
			L.ph.push (ph_new (d, PH_BODY2, mx + tw / 2 + 200, by, tw / 2 - 200, bh));
			break;
		case LY_COMPARE:
			L.ph.push (ph_new (d, PH_TITLE, mx, ty, tw, th));
			L.ph.push (ph_new (d, PH_SUBTITLE, mx, by, tw / 2 - 200, H * 80 / 1000));
			L.ph.push (ph_new (d, PH_BODY, mx, by + H * 100 / 1000, tw / 2 - 200, bh - H * 100 / 1000));
			L.ph.push (ph_new (d, PH_SUBTITLE, mx + tw / 2 + 200, by, tw / 2 - 200, H * 80 / 1000));
			L.ph.push (ph_new (d, PH_BODY2, mx + tw / 2 + 200, by + H * 100 / 1000, tw / 2 - 200, bh - H * 100 / 1000));
			break;
		case LY_SECTION:
			L.ph.push (ph_new (d, PH_TITLE, mx, H * 360 / 1000, tw, H * 200 / 1000));
			L.ph.push (ph_new (d, PH_SUBTITLE, mx, H * 580 / 1000, tw, H * 120 / 1000));
			break;
		case LY_TITLE_ONLY:
			L.ph.push (ph_new (d, PH_TITLE, mx, ty, tw, th));
			break;
		case LY_PICTURE:
			L.ph.push (ph_new (d, PH_PICTURE, 0, 0, W * 56 / 100, H));
			L.ph.push (ph_new (d, PH_TITLE, W * 61 / 100, H * 120 / 1000, W * 34 / 100, H * 160 / 1000));
			L.ph.push (ph_new (d, PH_BODY, W * 61 / 100, H * 320 / 1000, W * 34 / 100, H * 560 / 1000));
			break;
		case LY_BLANK: break;
		}
		if (l == LY_TITLE || l == LY_SECTION) { Object *t = L.find (PH_TITLE); if (t) t->tb.anchor = AN_BOTTOM; }
	}
	d.layout[LY_TITLE].find (PH_TITLE)->tb.anchor = AN_BOTTOM;
	// the footer's places (not placeholders a slide holds: drawn from the master)
}

// The placeholder's text style for paragraph level lvl.
static const TextStyle &style_for (const Deck &d, int ph, int lvl)
{
	if (ph == PH_TITLE) return d.style[TS_TITLE];
	if (ph == PH_SUBTITLE) return d.style[TS_SUBTITLE];
	if (ph == PH_BODY || ph == PH_BODY2) return d.style[TS_BODY1 + iclamp (lvl, 0, 4)];
	return d.style[TS_OTHER];
}
// The format resolved: f over the style's (the master's) -> every field set.
static CharFmt cf_resolve (const Deck &d, int ph, int lvl, const CharFmt &f)
{
	const CharFmt &s = style_for (d, ph, lvl).cf;
	CharFmt r;
	r.font = f.font != FONT_INHERIT ? f.font : s.font != FONT_INHERIT ? s.font : FONT_MINOR;
	r.size = f.size ? f.size : s.size ? s.size : 180;
	r.color = f.color != AUTO ? f.color : s.color != AUTO ? s.color : (THEME | TC_DK1);
	r.flags = (unsigned short) ((f.flags & f.set) | (s.flags & ~f.set));
	r.set = 0xFFFF;
	return r;
}
static ParaFmt pf_resolve (const Deck &d, int ph, const ParaFmt &p)
{
	const ParaFmt &s = style_for (d, ph, p.level).pf;
	ParaFmt r = p;
	if (r.align == AL_INHERIT) r.align = s.align == AL_INHERIT ? AL_LEFT : s.align;
	if (r.bullet == BU_INHERIT) r.bullet = s.bullet == BU_INHERIT ? BU_NONE : s.bullet;
	if (r.before < 0) r.before = s.before < 0 ? 0 : s.before;
	if (r.after < 0) r.after = s.after < 0 ? 0 : s.after;
	if (!r.spacing) r.spacing = s.spacing ? s.spacing : 100;
	return r;
}

// ---- making things ------------------------------------------------------------------------------------------------
// A new slide of layout l: its placeholders copied from the layout (empty: their prompts show).
static Slide *slide_new (Deck &d, int l)
{
	Slide *s = new Slide;
	s->layout = l;
	const Layout &L = d.layout[l];
	for (int i = 0; i < L.ph.n; i++) { Object *o = obj_copy (L.ph[i]); o->id = d.nextId++; s->obj.push (o); }
	s->notes.ensure ();
	return s;
}
// The slide's placeholders put back where its layout has them (Reset slide); a layout changed: the text kept.
static void slide_apply_layout (Deck &d, Slide *s, int l)
{
	const Layout &L = d.layout[l];
	int used[16] = { 0 };
	Vec<Object *> keep;
	for (int i = 0; i < s->obj.n; i++)
	{
		Object *o = s->obj[i];
		if (o->ph == PH_NONE) { keep.push (o); continue; }
		// its counterpart in the new layout (the same kind, in order)
		Object *t = 0;
		int k = o->ph;
		if (k < 16) { t = L.find (k, used[k]); if (t) used[k]++; }
		if (!t && (k == PH_BODY2)) { t = L.find (PH_BODY, used[PH_BODY]); if (t) used[PH_BODY]++; }
		if (t) { o->x = t->x; o->y = t->y; o->w = t->w; o->h = t->h; o->rot = 0; keep.push (o); }
		else if (o->tb.empty () && o->kind == OB_TEXT) delete o;
		else { o->ph = PH_NONE; keep.push (o); }			// (its text kept as a text box)
	}
	for (int i = 0; i < L.ph.n; i++)
	{
		int k = L.ph[i]->ph, nth = 0;
		for (int j = 0; j < i; j++) if (L.ph[j]->ph == k) nth++;
		if (k < 16 && nth < used[k]) continue;
		Object *o = obj_copy (L.ph[i]); o->id = d.nextId++;
		keep.insert (0, o);
	}
	s->obj.clear ();
	for (int i = 0; i < keep.n; i++) s->obj.push (keep[i]);
	s->layout = l;
}

static void deck_new (Deck &d, int theme = 0)
{
	d.clear ();
	d.sw = 28000; d.sh = 15750; d.nextId = 1;
	master_default (d, theme);
	d.slides.push (slide_new (d, LY_TITLE));
	d.changes = 0;
}

} // namespace sl

#endif
