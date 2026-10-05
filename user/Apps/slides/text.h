//
// text.h -- the text of a box: laid out in lines (in hmm, the same at every zoom: a font's advances are
// the design's, measured at a reference size), drawn with FreeType's glyphs at any scale into a layer
// (0xAARRGGBB premultiplied: gpucomp's textures) or an opaque canvas; the caret's place, a point's
// position; the edits (typing, deleting, Enter, a format over a range, a paragraph's format).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _slides_text_h
#define _slides_text_h

#include "model.h"
#include "fontkit/fonts.h"

namespace sl {

// ---- fonts: a family by name (cached), the reference size ------------------------------------------------------
static const double REF_HMM_PER_PX = 8.8194444;	// a font measured at 4 px a point: 1 px = 35.278 / 4 hmm
static int g_famCache[64]; static char g_famName[64][48]; static int g_nfamCache;
static int family_of (const char *name)
{
	for (int i = 0; i < g_nfamCache; i++) if (!strcmp (g_famName[i], name)) return g_famCache[i];
	int f = fnt::find (name);
	if (f < 0) f = fnt::find ("Liberation Sans");
	if (f < 0) f = fnt::find ("DejaVu Sans");
	if (f < 0) f = 0;
	if (g_nfamCache < 64) { scpy (g_famName[g_nfamCache], name, 48); g_famCache[g_nfamCache++] = f; }
	return f;
}
static inline int fstyle (unsigned flags) { return (flags & CF_BOLD ? fnt::BOLD : 0) | (flags & CF_ITALIC ? fnt::ITALIC : 0); }
// The size (1/10 pt) a character is drawn at: superscript, subscript smaller; the box's autofit.
static inline int eff_size (const CharFmt &r, int scale) { int s = r.size * scale / 1000; if (r.flags & (CF_SUPER | CF_SUB)) s = s * 62 / 100; return s < 10 ? 10 : s; }
// The reference font (4 px a point)
static fnt::Font *ref_font (const Deck &d, const CharFmt &r, int scale)
{
	int px64 = eff_size (r, scale) * 4 * 64 / 10;
	return fnt::get (family_of (d.font_name (r.font)), fstyle (r.flags), px64 < 64 ? 64 : px64);
}

// ---- the layout ----------------------------------------------------------------------------------------------------
struct LLine
{
	int para, a, b;			// its characters [a, b) of the paragraph
	float x;			// where the text starts (alignment), hmm from the box's inner left
	float base;			// its baseline, hmm from the box's inner top
	float asc, desc;		// above, below
	float width;			// its text's width
	bool last;			// the paragraph's last line
};
struct LPara
{
	float *cx;			// each character's x (hmm, from the line's start: LLine::x added when drawn), len + 1
	CharFmt *rf;			// each character's format, resolved
	ParaFmt pf;			// resolved
	float left;			// the text's left (hmm, the bullet's hang included)
	float bulletX;			// where the bullet goes
	unsigned bullet;		// its character (0: none); a number's text in num[]
	char num[8];
	CharFmt bf;			// the bullet's format (the first character's)
	int line0, nline;
	float top, bottom;		// its lines' extent
};
struct TextLayout
{
	Vec<LLine> line;
	LPara *pp; int npp;
	float height;			// the text's height (hmm)
	float boxW;			// the width it was laid out in
	int scale;			// the autofit scale it was laid out with
	TextLayout () : pp (0), npp (0), height (0), boxW (0), scale (1000) {}
	~TextLayout () { reset (); }
	void reset () { for (int i = 0; i < npp; i++) { free (pp[i].cx); free (pp[i].rf); } free (pp); pp = 0; npp = 0; line.clear (); height = 0; }
private:
	TextLayout (const TextLayout &);
	TextLayout &operator= (const TextLayout &);
};

static void roman (int n, char *o, bool upper)
{
	static const int v[] = { 10, 9, 5, 4, 1 }; static const char *s[] = { "x", "ix", "v", "iv", "i" };
	o[0] = 0;
	for (int i = 0; i < 5; i++) while (n >= v[i] && strlen (o) < 6) { strcat (o, s[i]); n -= v[i]; }
	if (upper) for (char *p = o; *p; p++) *p = (char) (*p - 32);
}

// Lays out tb (in a box w hmm wide inside its insets) at the given autofit scale.
static void layout_text (const Deck &d, const TextBody &tb, int ph, float w, int scale, TextLayout &L)
{
	L.reset ();
	L.boxW = w; L.scale = scale;
	L.npp = tb.p.n;
	L.pp = (LPara *) calloc (L.npp ? L.npp : 1, sizeof (LPara));
	float y = 0;
	int numbers[5] = { 0 };
	for (int pi = 0; pi < tb.p.n; pi++)
	{
		const Para *q = tb.p[pi];
		LPara &P = L.pp[pi];
		P.pf = pf_resolve (d, ph, q->pf);
		int lvl = iclamp (q->pf.level, 0, 4);
		P.cx = (float *) malloc (sizeof (float) * (q->len + 1));
		P.rf = (CharFmt *) malloc (sizeof (CharFmt) * (q->len + 1));
		for (int i = 0; i < q->len; i++) P.rf[i] = cf_resolve (d, ph, lvl, q->cf[i]);
		P.rf[q->len] = cf_resolve (d, ph, lvl, q->len ? q->cf[q->len - 1] : q->end);
		P.bf = q->len ? P.rf[0] : P.rf[q->len];
		// the bullet, the indent
		const TextStyle &st = style_for (d, ph, lvl);
		float step = st.indent ? st.indent : 800;
		P.bulletX = (ph == PH_BODY || ph == PH_BODY2 || P.pf.bullet != BU_NONE) ? lvl * step : 0;
		P.bullet = 0; P.num[0] = 0;
		if (P.pf.bullet == BU_BULLET) { P.bullet = st.bullet ? st.bullet : 0x2022; for (int k = lvl; k < 5; k++) numbers[k] = 0; }
		else if (P.pf.bullet == BU_NUMBER)
		{
			for (int k = lvl + 1; k < 5; k++) numbers[k] = 0;
			numbers[lvl]++;
			if (lvl % 3 == 1) { P.num[0] = (char) ('a' + (numbers[lvl] - 1) % 26); P.num[1] = '.'; P.num[2] = 0; }
			else if (lvl % 3 == 2) { roman (numbers[lvl], P.num, false); strcat (P.num, "."); }
			else snprintf (P.num, sizeof P.num, "%d.", numbers[lvl]);
		}
		else for (int k = lvl; k < 5; k++) numbers[k] = 0;	// (a plain paragraph: the lists at its level and deeper start again)
		P.left = P.bulletX;
		if (P.pf.bullet != BU_NONE)
		{
			// the hang: the bullet's (the number's) width and half an em, at least the level's step x 0.6
			fnt::Font *bf = fnt::get (family_of (P.bullet ? "DejaVu Sans" : d.font_name (P.bf.font)), fstyle (P.bf.flags), eff_size (P.bf, scale) * 4 * 64 / 10);
			float bw = 0;
			if (P.pf.bullet == BU_BULLET) bw = fnt::advance (bf, P.bullet ? P.bullet : 0x2022) / 64.0f * REF_HMM_PER_PX;
			else for (const char *c = P.num; *c; c++) bw += fnt::advance (bf, (unsigned char) *c) / 64.0f * REF_HMM_PER_PX;
			float em = eff_size (P.bf, scale) * 3.5277778f;
			P.left = P.bulletX + fmaxf (step * 0.6f, bw + em * 0.45f);
		}
		// the characters' advances
		float x = 0;
		for (int i = 0; i < q->len; i++)
		{
			P.cx[i] = x;
			unsigned c = q->ch[i];
			fnt::Font *f = ref_font (d, P.rf[i], scale);
			float a = c == '\t' ? 0 : fnt::advance (f, c == 0xA0 ? ' ' : c) / 64.0f * REF_HMM_PER_PX;
			if (i + 1 < q->len && cf_same (q->cf[i], q->cf[i + 1])) a += fnt::kern (f, c, q->ch[i + 1]) / 64.0f * REF_HMM_PER_PX;
			if (c == '\t') { float tab = 1250; a = tab - fmodf (x, tab); if (a < 100) a += tab; }
			x += a;
		}
		P.cx[q->len] = x;
		// the lines
		y += P.pf.before * 3.5277778f;
		P.top = y; P.line0 = L.line.n; P.nline = 0;
		float avail = w - P.left; if (avail < 200) avail = 200;
		int a = 0;
		do
		{
			int b = q->len;
			if (tb.wrap)
			{
				// the last break that fits
				int brk = -1;
				for (int i = a; i < q->len; i++)
				{
					if (q->ch[i] == '\n') { b = i + 1; brk = -2; break; }
					float right = P.cx[i + 1] - P.cx[a];
					if (q->ch[i] == ' ' || q->ch[i] == '-' || q->ch[i] == '\t') { if (right - (q->ch[i] == ' ' ? (P.cx[i + 1] - P.cx[i]) : 0) <= avail) brk = i + 1; }
					if (right > avail && i > a)
					{
						b = brk > a ? brk : i;		// a word longer than the line: cut
						break;
					}
				}
				(void) brk;
			}
			else { for (int i = a; i < q->len; i++) if (q->ch[i] == '\n') { b = i + 1; break; } }
			LLine ln; ln.para = pi; ln.a = a; ln.b = b; ln.last = b >= q->len;
			// its height: the biggest font on it (an empty line: the paragraph's end format)
			float asc = 0, desc = 0;
			int e0 = a, e1 = b > a ? b : a + 1;
			for (int i = e0; i < e1 && i <= q->len; i++)
			{
				fnt::Font *f = ref_font (d, P.rf[i], scale);
				if (!f) continue;
				float A = f->ascent / 64.0f * REF_HMM_PER_PX, D = f->descent / 64.0f * REF_HMM_PER_PX;
				if (P.rf[i].flags & CF_SUPER) A += A * 0.6f;
				if (P.rf[i].flags & CF_SUB) D += A * 0.3f;
				if (A > asc) asc = A;
				if (D > desc) desc = D;
			}
			float lh = (asc + desc) * P.pf.spacing / 100.0f;
			float extra = lh - (asc + desc);
			ln.asc = asc; ln.desc = desc;
			ln.base = y + asc + (extra > 0 ? extra * 0.5f : extra);
			// its width (trailing spaces and the line break left out)
			int e = b;
			while (e > a && (q->ch[e - 1] == ' ' || q->ch[e - 1] == '\n')) e--;
			ln.width = P.cx[e] - P.cx[a];
			float room = avail - ln.width;
			ln.x = P.left - P.cx[a];
			if (P.pf.align == AL_CENTER) ln.x += room / 2;
			else if (P.pf.align == AL_RIGHT) ln.x += room;
			L.line.push (ln);
			P.nline++;
			y += lh;
			a = b;
		} while (a < q->len);
		P.bottom = y;
		y += P.pf.after * 3.5277778f;
	}
	L.height = y;
}

// The box's inner size (hmm): its insets taken off.
static inline float inner_w (const Object &o) { return (float) (o.w - o.tb.inset[0] - o.tb.inset[2]); }
static inline float inner_h (const Object &o) { return (float) (o.h - o.tb.inset[1] - o.tb.inset[3]); }

// The layout of an object's text, the autofit's shrink found (tb.scale set).
static void layout_object (const Deck &d, Object &o, TextLayout &L)
{
	float w = inner_w (o), h = inner_h (o);
	int scale = 1000;
	layout_text (d, o.tb, o.ph, w, scale, L);
	if (o.tb.fit == FIT_SHRINK && L.height > h && h > 0)
	{
		while (scale > 300 && L.height > h) { scale -= scale > 600 ? 75 : 50; layout_text (d, o.tb, o.ph, w, scale, L); }
	}
	o.tb.scale = scale;
}
// Where the text's top is (hmm from the box's inner top): its anchor.
static inline float text_top (const Object &o, const TextLayout &L)
{
	float room = inner_h (o) - L.height;
	if (o.tb.anchor == AN_MIDDLE) return room / 2;
	if (o.tb.anchor == AN_BOTTOM) return room;
	return 0;
}

// ---- drawing ---------------------------------------------------------------------------------------------------
// A surface: an opaque canvas (0x00RRGGBB: blends over it) or a layer (0xAARRGGBB premultiplied).
struct Surf
{
	unsigned *px; int w, h, stride; bool pm;
	int cx0, cy0, cx1, cy1;		// the clip
	void clip_all () { cx0 = 0; cy0 = 0; cx1 = w; cy1 = h; }
};
static inline void blend_pm (unsigned &d, unsigned c, int a)	// c (opaque rgb) at coverage a over premultiplied d
{
	if (a <= 0) return;
	if (a >= 255) { d = 0xFF000000u | (c & 0xFFFFFF); return; }
	unsigned ia = 255 - (unsigned) a;
	unsigned A = (unsigned) a + ((d >> 24) * ia + 127) / 255;
	unsigned r = (((c >> 16) & 255) * (unsigned) a + ((d >> 16) & 255) * ia + 127) / 255;
	unsigned g = (((c >> 8) & 255) * (unsigned) a + ((d >> 8) & 255) * ia + 127) / 255;
	unsigned b = ((c & 255) * (unsigned) a + (d & 255) * ia + 127) / 255;
	d = A << 24 | r << 16 | g << 8 | b;
}
static inline void blend_op (unsigned &d, unsigned c, int a)	// over an opaque pixel
{
	if (a <= 0) return;
	if (a >= 255) { d = c & 0xFFFFFF; return; }
	unsigned ia = 255 - (unsigned) a;
	unsigned r = (((c >> 16) & 255) * (unsigned) a + ((d >> 16) & 255) * ia + 127) / 255;
	unsigned g = (((c >> 8) & 255) * (unsigned) a + ((d >> 8) & 255) * ia + 127) / 255;
	unsigned b = ((c & 255) * (unsigned) a + (d & 255) * ia + 127) / 255;
	d = r << 16 | g << 8 | b;
}
static inline void surf_blend (Surf &s, int x, int y, unsigned c, int a)
{
	if (x < s.cx0 || y < s.cy0 || x >= s.cx1 || y >= s.cy1) return;
	unsigned &d = s.px[(long) y * s.stride + x];
	if (s.pm) blend_pm (d, c, a); else blend_op (d, c, a);
}
static void surf_rect (Surf &s, float x, float y, float w, float h, unsigned c, int alpha = 255)
{
	int x0 = (int) floorf (x), y0 = (int) floorf (y), x1 = (int) ceilf (x + w), y1 = (int) ceilf (y + h);
	for (int j = y0; j < y1; j++)
	{
		float cy = fminf ((float) j + 1, y + h) - fmaxf ((float) j, y);
		if (cy <= 0) continue;
		for (int i = x0; i < x1; i++)
		{
			float cx = fminf ((float) i + 1, x + w) - fmaxf ((float) i, x);
			if (cx > 0) surf_blend (s, i, j, c, (int) (cx * cy * alpha + 0.5f));
		}
	}
}
// A character at x (px, a fraction: a quarter pixel), on the baseline y.
static void surf_glyph (Surf &s, fnt::Font *f, float x, int y, unsigned cp, unsigned c, int alpha = 255)
{
	if (!f || cp <= 32 || cp == 0xA0) return;
	fnt::Glyph *g = fnt::glyph (f, cp);
	int x64 = (int) (x * 64);
	int ph = (x64 & 63) >> 4, xi = x64 >> 6;
	if (g->gi == 0)
	{
		int bw = g->adv >> 6, bh = f->ascent * 7 / 10 >> 6;
		for (int i = 1; i < bw - 1; i++) { surf_blend (s, xi + i, y, c, 160 * alpha / 255); surf_blend (s, xi + i, y - bh, c, 160 * alpha / 255); }
		for (int j = 0; j <= bh; j++) { surf_blend (s, xi + 1, y - j, c, 160 * alpha / 255); surf_blend (s, xi + bw - 2, y - j, c, 160 * alpha / 255); }
		return;
	}
	if (!g->bmp[ph]) fnt::render (f, g, ph);
	const unsigned char *b = g->bmp[ph];
	if (!b) return;
	int gx = xi + g->bl[ph], gy = y - g->bt[ph], w = g->bw[ph], h = g->bh[ph];
	int i0 = imax (0, s.cx0 - gx), i1 = imin (w, s.cx1 - gx), j0 = imax (0, s.cy0 - gy), j1 = imin (h, s.cy1 - gy);
	for (int j = j0; j < j1; j++)
	{
		unsigned *d = s.px + (long) (gy + j) * s.stride + gx;
		const unsigned char *r = b + j * w;
		for (int i = i0; i < i1; i++)
			if (r[i])
			{
				int a = fnt::g_gamma[r[i]] * alpha / 255;
				if (s.pm) blend_pm (d[i], c, a); else blend_op (d[i], c, a);
			}
	}
}

// The text of o drawn: its box's top-left at (ox, oy) px, sc px a hmm. L: its layout (layout_object).
// dim: the empty placeholder's prompt is drawn instead (grey), when prompt is given.
static void draw_text (const Deck &d, const Object &o, const TextLayout &L, Surf &s, float ox, float oy, float sc, const char *prompt = 0, int alpha = 255)
{
	float ix = ox + o.tb.inset[0] * sc, iy = oy + (o.tb.inset[1] + text_top (o, L)) * sc;
	if (prompt && o.tb.empty ())
	{
		// the prompt: the placeholder's first style, greyed
		CharFmt f = cf_resolve (d, o.ph, 0, cf_inherit ());
		int sz = eff_size (f, 1000);
		fnt::Font *ft = fnt::get (family_of (d.font_name (f.font)), fstyle (f.flags), (int) (sz * 3.5277778f * sc * 64));
		if (!ft) return;
		float asc = ft->ascent / 64.0f;
		float pw = 0; for (const char *p = prompt; *p; p++) pw += fnt::advance (ft, (unsigned char) *p) / 64.0f;
		ParaFmt pf = pf_resolve (d, o.ph, pf_inherit ());
		float x = ix, room = inner_w (o) * sc - pw;
		if (pf.align == AL_CENTER) x += room / 2; else if (pf.align == AL_RIGHT) x += room;
		float lh = (ft->ascent + ft->descent) / 64.0f;
		float y = oy + o.tb.inset[1] * sc;
		float ih = inner_h (o) * sc;
		if (o.tb.anchor == AN_MIDDLE) y += (ih - lh) / 2; else if (o.tb.anchor == AN_BOTTOM) y += ih - lh;
		for (const char *p = prompt; *p; p++) { surf_glyph (s, ft, x, (int) (y + asc), (unsigned char) *p, 0x8A8A8A, alpha); x += fnt::advance (ft, (unsigned char) *p) / 64.0f; }
		return;
	}
	for (int li = 0; li < L.line.n; li++)
	{
		const LLine &ln = L.line[li];
		const Para *q = o.tb.p[ln.para];
		const LPara &P = L.pp[ln.para];
		int by = (int) (iy + ln.base * sc + 0.5f);
		// the bullet (on its paragraph's first line)
		if (ln.a == 0 && (P.bullet || P.num[0]))
		{
			CharFmt bf = P.bf; bf.flags &= ~(CF_UNDER | CF_STRIKE | CF_SUPER | CF_SUB);
			int sz = eff_size (bf, L.scale);
			fnt::Font *f = fnt::get (family_of (P.bullet ? "DejaVu Sans" : d.font_name (bf.font)), fstyle (bf.flags), (int) (sz * 3.5277778f * sc * 64));
			unsigned col = d.rgb (bf.color);
			float bx = ix + P.bulletX * sc;
			if (P.bullet) surf_glyph (s, f, bx, by, P.bullet, col, alpha);
			else for (const char *p = P.num; *p; p++) { surf_glyph (s, f, bx, by, (unsigned char) *p, col, alpha); bx += fnt::advance (f, (unsigned char) *p) / 64.0f; }
		}
		for (int i = ln.a; i < ln.b; i++)
		{
			unsigned c = q->ch[i];
			const CharFmt &rf = P.rf[i];
			int sz = eff_size (rf, L.scale);
			fnt::Font *f = fnt::get (family_of (d.font_name (rf.font)), fstyle (rf.flags), (int) (sz * 3.5277778f * sc * 64));
			float x = ix + (ln.x + P.cx[i]) * sc;
			int y = by;
			if (rf.flags & CF_SUPER) y -= (int) (rf.size * L.scale / 1000 * 3.5277778f * sc * 0.36f);
			if (rf.flags & CF_SUB) y += (int) (rf.size * L.scale / 1000 * 3.5277778f * sc * 0.14f);
			unsigned col = d.rgb (rf.color);
			if (c != '\n' && c != '\t') surf_glyph (s, f, x, y, c, col, alpha);
			if ((rf.flags & (CF_UNDER | CF_STRIKE)) && f && c != '\n')
			{
				float w = (P.cx[i + 1] - P.cx[i]) * sc;
				float th = fmaxf (1.0f, f->ulThick / 64.0f);
				if (rf.flags & CF_UNDER) surf_rect (s, x, y + f->ulPos / 64.0f, w, th, col, alpha);
				if (rf.flags & CF_STRIKE) surf_rect (s, x, y - f->xHeight / 128.0f - th / 2, w, th, col, alpha);
			}
		}
	}
}

// ---- positions in the text ------------------------------------------------------------------------------------------
struct TPos { int p, o; };
static inline TPos tpos (int p, int o) { TPos t; t.p = p; t.o = o; return t; }
static inline int tcmp (TPos a, TPos b) { return a.p != b.p ? a.p - b.p : a.o - b.o; }
static inline TPos tmin (TPos a, TPos b) { return tcmp (a, b) <= 0 ? a : b; }
static inline TPos tmax (TPos a, TPos b) { return tcmp (a, b) >= 0 ? a : b; }

// The line a position is on.
static int line_of (const TextLayout &L, TPos t)
{
	if (t.p < 0 || t.p >= L.npp) return 0;
	const LPara &P = L.pp[t.p];
	for (int k = 0; k < P.nline; k++)
	{
		const LLine &ln = L.line[P.line0 + k];
		if (t.o < ln.b || ln.last) return P.line0 + k;
		if (t.o == ln.b && ln.b > ln.a && ln.b > 0) { /* at a wrapped line's end: the next one */ }
	}
	return P.line0 + P.nline - 1;
}
// The caret's place: x, the line's top and bottom (hmm, from the box's inner top-left, the anchor included).
static void caret_xy (const Object &o, const TextLayout &L, TPos t, float *x, float *y0, float *y1)
{
	float top = text_top (o, L);
	if (!L.line.n) { *x = 0; *y0 = top; *y1 = top + 500; return; }
	int li = line_of (L, t);
	const LLine &ln = L.line[li];
	const LPara &P = L.pp[ln.para];
	int k = iclamp (t.o, ln.a, ln.b);
	*x = ln.x + P.cx[k];
	*y0 = top + ln.base - ln.asc; *y1 = top + ln.base + ln.desc;
}
// The position nearest a point (hmm from the box's inner top-left).
static TPos pos_at (const Object &o, const TextLayout &L, float x, float y)
{
	if (!L.line.n) return tpos (0, 0);
	float top = text_top (o, L);
	int li = L.line.n - 1;
	for (int i = 0; i < L.line.n; i++) if (y < top + L.line[i].base + L.line[i].desc) { li = i; break; }
	const LLine &ln = L.line[li];
	const LPara &P = L.pp[ln.para];
	int end = ln.b;
	if (!ln.last && end > ln.a) end--;			// (a wrapped line: before its break)
	int best = ln.a; float bd = 1e9f;
	for (int k = ln.a; k <= end; k++) { float dx = fabsf (ln.x + P.cx[k] - x); if (dx < bd) { bd = dx; best = k; } }
	return tpos (ln.para, best);
}

// ---- edits --------------------------------------------------------------------------------------------------------
static void tb_delete (TextBody &tb, TPos a, TPos b)
{
	if (tcmp (a, b) >= 0) return;
	Para *pa = tb.p[a.p];
	if (a.p == b.p) { pa->remove (a.o, b.o); return; }
	Para *pb = tb.p[b.p];
	pa->len = a.o;
	pa->insert (a.o, pb->ch + b.o, pb->len - b.o, CharFmt ());
	for (int i = 0; i < pb->len - b.o; i++) pa->cf[a.o + i] = pb->cf[b.o + i];
	for (int k = a.p + 1; k <= b.p; k++) delete tb.p[k];
	for (int k = a.p + 1; k <= b.p; k++) tb.p.erase (a.p + 1);
}
// Text typed at t in format f: the position after it.
static TPos tb_insert (TextBody &tb, TPos t, const unsigned *s, int n, const CharFmt &f)
{
	tb.ensure ();
	for (int i = 0; i < n; i++)
	{
		if (s[i] == '\r') continue;
		if (s[i] == '\n')
		{
			Para *q = tb.p[t.p];
			Para *r = new Para;
			r->pf = q->pf; r->end = q->fmt_at (t.o);
			r->insert (0, q->ch + t.o, q->len - t.o, CharFmt ());
			for (int k = 0; k < q->len - t.o; k++) r->cf[k] = q->cf[t.o + k];
			q->len = t.o;
			tb.p.insert (t.p + 1, r);
			t = tpos (t.p + 1, 0);
			continue;
		}
		tb.p[t.p]->insert (t.o, s + i, 1, f);
		t.o++;
	}
	return t;
}
// A format change over [a, b): what = the fields to set.
enum { FC_FONT = 1, FC_SIZE = 2, FC_COLOR = 4, FC_FLAGS = 8 };
struct FmtChange { int what; short font, size; unsigned color; unsigned short flags, mask; };
static void cf_apply (CharFmt &c, const FmtChange &ch)
{
	if (ch.what & FC_FONT) c.font = ch.font;
	if (ch.what & FC_SIZE) c.size = ch.size;
	if (ch.what & FC_COLOR) c.color = ch.color;
	if (ch.what & FC_FLAGS) { c.flags = (unsigned short) ((c.flags & ~ch.mask) | (ch.flags & ch.mask)); c.set |= ch.mask; if (ch.mask & CF_SUPER && ch.flags & CF_SUPER) c.flags &= ~CF_SUB; if (ch.mask & CF_SUB && ch.flags & CF_SUB) c.flags &= ~CF_SUPER; }
}
static void tb_format (TextBody &tb, TPos a, TPos b, const FmtChange &ch)
{
	for (int p = a.p; p <= b.p && p < tb.p.n; p++)
	{
		Para *q = tb.p[p];
		int o0 = p == a.p ? a.o : 0, o1 = p == b.p ? b.o : q->len;
		for (int i = o0; i < o1; i++) cf_apply (q->cf[i], ch);
		if (o1 >= q->len) cf_apply (q->end, ch);
	}
}
// The whole text's format changed (an object selected, not edited).
static void tb_format_all (TextBody &tb, const FmtChange &ch)
{
	tb.ensure ();
	int lp = tb.p.n - 1;
	tb_format (tb, tpos (0, 0), tpos (lp, tb.p[lp]->len), ch);
}
// The resolved format at a position (what the toolbar shows).
static CharFmt fmt_at (const Deck &d, const Object &o, TPos t)
{
	const Para *q = o.tb.p.n ? o.tb.p[iclamp (t.p, 0, o.tb.p.n - 1)] : 0;
	if (!q) return cf_resolve (d, o.ph, 0, cf_inherit ());
	return cf_resolve (d, o.ph, q->pf.level, q->fmt_at (iclamp (t.o, 0, q->len)));
}
// Word boundaries (double click)
static inline bool word_ch (unsigned c) { return c > ' ' && c != '.' && c != ',' && c != ';' && c != ':' && c != '!' && c != '?' && c != '(' && c != ')' && c != '"'; }

} // namespace sl

#endif
