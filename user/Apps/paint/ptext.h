//
// ptext.h -- Paint's Text tool: the text typed on the canvas, in one of the card's TrueType families
// (fontkit/fonts.h: FreeType), its size, bold / italic / underline, aligned left / centred / right, its
// edges smooth (anti-aliased) or sharp, on colour 2 or on nothing. It is drawn into the overlay of
// the current layer while it is typed (moved by its box), and put down on the layer when it is done.
//
// MIT licence (Onyx).
//
#ifndef _paint_ptext_h
#define _paint_ptext_h

#include "fontkit/fonts.h"
#include "pdoc.h"

namespace pd {

static int g_tfam = -1, g_tsize = 32, g_talign = 0;		// family (fnt index), px, 0 left 1 centre 2 right
static bool g_tbold, g_titalic, g_tunder, g_tsmooth = true, g_tback;
static const char *g_tfamNames[fnt::MAXFAM]; static int g_tnfam;	// (the families' names, for the list)

static void text_init ()
{
	if (!fnt::init ()) return;
	g_tnfam = 0;
	for (int i = 0; i < fnt::count () && i < fnt::MAXFAM; i++) g_tfamNames[g_tnfam++] = fnt::name (i);
	if (g_tfam < 0) { g_tfam = fnt::find ("DejaVu Sans"); if (g_tfam < 0) g_tfam = 0; }
}

// The text being typed.
struct TextBox
{
	bool on;
	int x, y;				// its top-left in the picture
	char s[1024]; int n, caret;		// UTF-8
	int w, h;				// its size (the last layout)
};
static TextBox g_text;

static fnt::Font *text_font ()
{
	if (g_tnfam == 0) return 0;
	return fnt::get (g_tfam, (g_tbold ? fnt::BOLD : 0) | (g_titalic ? fnt::ITALIC : 0), g_tsize * 64);
}
// The width of the line s[a, b) in 1/64 px.
static long text_line_w (fnt::Font *f, const char *s, int a, int b)
{
	long x = 0; unsigned prev = 0;
	for (int i = a; i < b; )
	{
		int k; unsigned cp = uikit::uk_u8_get (s + i, b - i, &k); i += k;
		if (prev) x += fnt::kern (f, prev, cp);
		x += fnt::advance (f, cp);
		prev = cp;
	}
	return x;
}
// Lay the text out: its box's size; draw it into buf (D.w x D.h, straight ARGB) when buf, colour c
// (back: colour 2 behind its box), and the caret's place (x, y0, y1 in the picture) when caretOut.
static void text_layout (unsigned *buf, Rect *drawn, unsigned c, unsigned back, int *caret)
{
	fnt::Font *f = text_font ();
	TextBox &t = g_text;
	if (!f) { t.w = t.h = 0; return; }
	int lh = (f->height + 63) >> 6, asc = (f->ascent + 63) >> 6;
	if (lh < 1) lh = g_tsize;
	// the lines: their widths, the widest
	int starts[128], ends[128], nl = 0;
	for (int i = 0, a = 0; i <= t.n && nl < 128; i++)
		if (i == t.n || t.s[i] == '\n') { starts[nl] = a; ends[nl] = i; nl++; a = i + 1; }
	long wmax = 64;
	for (int l = 0; l < nl; l++) { long w = text_line_w (f, t.s, starts[l], ends[l]); if (w > wmax) wmax = w; }
	t.w = (int) ((wmax + 63) >> 6) + 2; t.h = nl * lh;
	if (caret) caret[0] = -1;
	if (buf && g_tback)
	{
		Rect r = mkrect (t.x, t.y, t.x + t.w, t.y + t.h); r.clip (D.w, D.h);
		for (int y = r.y0; y < r.y1; y++) for (int x = r.x0; x < r.x1; x++) buf[(unsigned) y * D.w + x] = back | 0xFF000000u;
		if (drawn) drawn->add (r);
	}
	for (int l = 0; l < nl; l++)
	{
		long lw = text_line_w (f, t.s, starts[l], ends[l]);
		long x64 = (long) t.x * 64 + (g_talign == 1 ? (wmax - lw) / 2 : g_talign == 2 ? wmax - lw : 0);
		int base = t.y + l * lh + asc;
		unsigned prev = 0;
		for (int i = starts[l]; ; )
		{
			if (caret && i == t.caret) { caret[0] = (int) (x64 >> 6); caret[1] = t.y + l * lh; caret[2] = t.y + (l + 1) * lh; }
			if (i >= ends[l]) break;
			int k; unsigned cp = uikit::uk_u8_get (t.s + i, ends[l] - i, &k); i += k;
			if (prev) x64 += fnt::kern (f, prev, cp);
			prev = cp;
			if (buf && cp > 32)
			{
				fnt::Glyph *g = fnt::glyph (f, cp);
				int ph = (int) ((x64 & 63) >> 4);
				if (g->gi && !g->bmp[ph]) fnt::render (f, g, ph);
				if (g->gi && g->bmp[ph])
				{
					int gx = (int) (x64 >> 6) + g->bl[ph], gy = base - g->bt[ph];
					for (int j = 0; j < g->bh[ph]; j++)
					{
						int Y = gy + j; if ((unsigned) Y >= (unsigned) D.h) continue;
						const unsigned char *row = g->bmp[ph] + j * g->bw[ph];
						for (int i2 = 0; i2 < g->bw[ph]; i2++)
						{
							int X = gx + i2; if ((unsigned) X >= (unsigned) D.w || !row[i2]) continue;
							int a = g_tsmooth ? row[i2] : row[i2] >= 128 ? 255 : 0;
							if (!a) continue;
							unsigned &d = buf[(unsigned) Y * D.w + X];
							d = over (d, c | 0xFF000000u, (unsigned) a);
							if (drawn) drawn->add (X, Y);
						}
					}
				}
			}
			x64 += fnt::advance (f, cp);
		}
		if (buf && g_tunder && lw > 0)
		{
			int uy = base + ((f->ulPos + 32) >> 6), th = pmax (1, (f->ulThick + 32) >> 6);
			int ux0 = (int) (((long) t.x * 64 + (g_talign == 1 ? (wmax - lw) / 2 : g_talign == 2 ? wmax - lw : 0)) >> 6);
			for (int yy = uy; yy < uy + th; yy++)
				for (int xx = ux0; xx < ux0 + (int) (lw >> 6); xx++)
					if ((unsigned) xx < (unsigned) D.w && (unsigned) yy < (unsigned) D.h) { buf[(unsigned) yy * D.w + xx] = c | 0xFF000000u; if (drawn) drawn->add (xx, yy); }
		}
	}
}

// ---- editing ----
static void text_insert (const char *u, int n)
{
	TextBox &t = g_text;
	if (t.n + n >= (int) sizeof t.s) return;
	for (int i = t.n; i >= t.caret; i--) t.s[i + n] = t.s[i];
	for (int i = 0; i < n; i++) t.s[t.caret + i] = u[i];
	t.n += n; t.caret += n;
}
static void text_back ()
{
	TextBox &t = g_text;
	if (t.caret <= 0) return;
	int p = uikit::uk_u8_prev (t.s, t.caret), d = t.caret - p;
	for (int i = p; i + d <= t.n; i++) t.s[i] = t.s[i + d];
	t.n -= d; t.caret = p;
}
static void text_del ()
{
	TextBox &t = g_text;
	if (t.caret >= t.n) return;
	int d = uikit::uk_u8_next (t.s, t.caret, t.n) - t.caret;
	for (int i = t.caret; i + d <= t.n; i++) t.s[i] = t.s[i + d];
	t.n -= d;
}

} // namespace pd

#endif
