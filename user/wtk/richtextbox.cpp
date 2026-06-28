//
// wtk/richtextbox.cpp -- RichTextBox implementation (compiled into libwtk.a).
// operator new[]/delete[] resolve at link from the app's onyxpp.hpp (see canvas.cpp);
// do NOT include onyxpp.hpp here.
//
#include "wtk/richtextbox.h"
#include "wtk/font.h"		// styled glyphs from the global ns-sans family (SD:/fonts)

namespace wtk {

// Text metrics come from the active face (the global font family) so the layout follows
// the loaded font's glyph size; they fall back to the kernel font when no .fnt is present.
static int rt_fw () { return font ().valid () ? font ().width ()  : wk_fw (); }
static int rt_fh () { return font ().valid () ? font ().height () : wk_fh (); }

// ---- palette + style packing -------------------------------------------------

static const unsigned RT_PAL[16] = {
	0x000000, 0xFFFFFF, 0x808080, 0xC0C0C0,
	0xD00000, 0x008000, 0x0000D0, 0xFFE000,
	0x00C0C0, 0xC000C0, 0xF07000, 0x8000C0,
	0x800000, 0x004000, 0x000080, 0x008080,
};
unsigned rt_color (int i) { return RT_PAL[(i < 0 || i > 15) ? 0 : i]; }

unsigned rt_pack (const RtStyle &s)
{
	int sz = s.size < 1 ? 1 : s.size;
	return ((unsigned) s.flags & 0xFF)
	     | (((unsigned) s.fg & 0xF) << 8)
	     | (((unsigned) s.bg & 0xF) << 12)
	     | (((unsigned) sz & 0xFF) << 16);
}
RtStyle rt_unpack (unsigned a)
{
	RtStyle s;
	s.flags = (unsigned char) (a & 0xFF);
	s.fg    = (unsigned char) ((a >> 8) & 0xF);
	s.bg    = (unsigned char) ((a >> 12) & 0xF);
	s.size  = (unsigned char) ((a >> 16) & 0xFF); if (s.size < 1) s.size = 1;
	return s;
}

// ---- glyph raster helpers ----------------------------------------------------

#define RT_MAXW 40
#define RT_MAXH 64
#define RT_SEL  0x00B5D0FFu	// selection band colour

// Per-character raw 1-bit glyph mask cache (process-wide; one bitmap font).
static unsigned char *g_raw[256];
static unsigned      *g_tmp;
static int            g_cw, g_ch;

static const unsigned char *rawMask (unsigned char ch)
{
	int fw = wk_fw (), fh = wk_fh ();
	if (fw != g_cw || fh != g_ch)			// font cell changed: drop the cache
	{
		for (int i = 0; i < 256; i++) { delete [] g_raw[i]; g_raw[i] = 0; }
		delete [] g_tmp; g_tmp = 0; g_cw = fw; g_ch = fh;
	}
	if (fw < 1 || fh < 1 || fw > RT_MAXW || fh > RT_MAXH) return 0;
	if (g_tmp == 0) g_tmp = new unsigned[fw * fh];
	if (g_tmp == 0) return 0;
	if (g_raw[ch] == 0)
	{
		for (int i = 0; i < fw * fh; i++) g_tmp[i] = 0;
		char s[2] = { (char) ch, 0 };
		kapi_draw_text_buf (g_tmp, fw, fh, 0, 0, s, 0xFFFFFF);
		unsigned char *m = new unsigned char[fw * fh];
		if (m == 0) return 0;
		for (int i = 0; i < fw * fh; i++) m[i] = g_tmp[i] ? 1 : 0;
		g_raw[ch] = m;
	}
	return g_raw[ch];
}

static inline int samp (const unsigned char *wm, int W, int H, int x, int y)
{ return (x < 0 || y < 0 || x >= W || y >= H) ? 0 : wm[y * W + x]; }

static inline unsigned blend (unsigned bg, unsigned fg, int a)	// a in 0..255
{
	int r1 = (bg >> 16) & 0xFF, g1 = (bg >> 8) & 0xFF, b1 = bg & 0xFF;
	int r2 = (fg >> 16) & 0xFF, g2 = (fg >> 8) & 0xFF, b2 = fg & 0xFF;
	int r = (r2 * a + r1 * (255 - a)) / 255;
	int g = (g2 * a + g1 * (255 - a)) / 255;
	int b = (b2 * a + b1 * (255 - a)) / 255;
	return ((unsigned) r << 16) | ((unsigned) g << 8) | (unsigned) b;
}

// Rasterise one styled glyph at (px,py) into this widget's canvas. The base mask comes
// from the matching ns-sans face (real designed bold / italic / bold-italic); if a face
// failed to load it falls back to synthesising from the kernel font (bold = OR with the
// left neighbour, italic = per-row right shear). size = integer bilinear upscale (the
// "lissage"); colour blended over whatever is already there (page/highlight/selection).
void RichTextBox::drawGlyph (int px, int py, char ch, const RtStyle &st)
{
	bool bold = (st.flags & RT_BOLD) != 0;
	bool ital = (st.flags & RT_ITALIC) != 0;

	Font &face = font ();					// the global family
	int   styleIdx = (bold ? 2 : 0) | (ital ? 1 : 0);

	unsigned char wm[RT_MAXW * RT_MAXH];
	int Wm, fh;
	if (face.valid ())					// real designed glyph for this style
	{
		Wm = face.width (); fh = face.height ();
		if (Wm > RT_MAXW) Wm = RT_MAXW;
		if (fh > RT_MAXH) fh = RT_MAXH;
		for (int i = 0; i < Wm * fh; i++) wm[i] = 0;
		const unsigned char *gl = face.glyph ((unsigned char) ch, styleIdx);
		if (gl)
			for (int y = 0; y < fh; y++)
			{
				unsigned char b = gl[y];
				for (int x = 0; x < Wm; x++) if (b & (0x80 >> x)) wm[y * Wm + x] = 255;
			}
	}
	else							// fallback: synthesise from the kernel font
	{
		int fw = wk_fw (); fh = wk_fh ();
		const unsigned char *m = rawMask ((unsigned char) ch);
		if (m == 0) return;
		int shearMax = ital ? (fh - 1) / 3 : 0;
		Wm = fw + shearMax + (bold ? 1 : 0);
		if (Wm > RT_MAXW) Wm = RT_MAXW;
		for (int i = 0; i < Wm * fh; i++) wm[i] = 0;
		for (int y = 0; y < fh; y++)
		{
			int sh = ital ? (fh - 1 - y) / 3 : 0;
			for (int xx = 0; xx < fw; xx++)
			{
				int v = m[y * fw + xx];
				if (bold && xx > 0) v |= m[y * fw + xx - 1];
				if (v) { int ox = xx + sh; if (ox >= 0 && ox < Wm) wm[y * Wm + ox] = 255; }
			}
		}
	}

	int s = st.size < 1 ? 1 : st.size;
	int ow = Wm * s, oh = fh * s;
	unsigned fg = rt_color (st.fg);
	int cw = canvas.w, chh = canvas.h, cs = canvas.stride; unsigned *P = canvas.px;	// clip to w/h, step by stride
	for (int oy = 0; oy < oh; oy++)
	{
		int dy = py + oy; if (dy < 0 || dy >= chh) continue;
		int fyv = ((oy * 256 + 128) / s) - 128;
		int y0 = fyv >> 8, fracy = fyv - (y0 << 8);
		for (int ox = 0; ox < ow; ox++)
		{
			int dx = px + ox; if (dx < 0 || dx >= cw) continue;
			int fxv = ((ox * 256 + 128) / s) - 128;
			int x0 = fxv >> 8, fracx = fxv - (x0 << 8);
			int s00 = samp (wm, Wm, fh, x0,     y0),     s01 = samp (wm, Wm, fh, x0 + 1, y0);
			int s10 = samp (wm, Wm, fh, x0,     y0 + 1), s11 = samp (wm, Wm, fh, x0 + 1, y0 + 1);
			int tv = s00 * (256 - fracx) + s01 * fracx;
			int bv = s10 * (256 - fracx) + s11 * fracx;
			int cov = (tv * (256 - fracy) + bv * fracy) >> 16;
			if (cov <= 0) continue;
			if (cov > 255) cov = 255;
			unsigned *q = &P[dy * cs + dx];
			*q = blend (*q, fg, cov);
		}
	}
}

// ---- construction ------------------------------------------------------------

RichTextBox::RichTextBox (int l, int t, int w, int h, int capacity)
  : Widget (l, t, w, h), cap (capacity < 32 ? 32 : capacity),
    len (0), caret (0), sel (-1), top (0), goalX (0), readonly (false),
    rowStart (0), rowH (0), rowY (0), rowN (0), rowCap (0), lastWrapW (-1), layoutDirty (true),
    barDrag (false)
{
	canFocus = true;
	buf  = new char[cap];      buf[0] = '\0';
	attr = new unsigned[cap];  attr[0] = rt_pack (RtStyle ());
}

RichTextBox::~RichTextBox ()
{ delete [] buf; delete [] attr; delete [] rowStart; delete [] rowH; delete [] rowY; }

bool RichTextBox::grow (int need)
{
	if (need + 1 <= cap) return true;
	int nc = cap * 2; if (nc < need + 1) nc = need + 1;
	char *nb = new char[nc]; unsigned *na = new unsigned[nc];
	if (nb == 0 || na == 0) { delete [] nb; delete [] na; return false; }
	for (int i = 0; i <= len; i++) { nb[i] = buf[i]; na[i] = attr[i]; }
	delete [] buf; delete [] attr; buf = nb; attr = na; cap = nc; return true;
}

void RichTextBox::setContent (const char *s)
{
	int n = 0; while (s && s[n]) n++;
	if (!grow (n)) n = cap - 1;
	unsigned d = rt_pack (RtStyle ());
	for (int i = 0; i < n; i++) { buf[i] = s[i]; attr[i] = d; }
	len = n; buf[len] = '\0';
	caret = 0; sel = -1; top = 0; goalX = 0; layoutDirty = true; invalidate (true);
}

// ---- editing -----------------------------------------------------------------

int RichTextBox::glyphAdvance (int i) const
{ int sz = (int) ((attr[i] >> 16) & 0xFF); if (sz < 1) sz = 1; return rt_fw () * sz; }

void RichTextBox::selRange (int &a, int &b) const
{ if (sel < caret) { a = sel; b = caret; } else { a = caret; b = sel; } }

void RichTextBox::insertChar (char ch)
{
	if (!grow (len + 1)) return;
	for (int i = len; i > caret; i--) { buf[i] = buf[i - 1]; attr[i] = attr[i - 1]; }
	buf[caret] = ch; attr[caret] = rt_pack (cur);
	len++; caret++; buf[len] = '\0';
	sel = -1; layoutDirty = true;
}

void RichTextBox::deleteRange (int a, int b)
{
	if (a < 0) a = 0; if (b > len) b = len; if (a >= b) return;
	int d = b - a;
	for (int i = b; i <= len; i++) { buf[i - d] = buf[i]; attr[i - d] = attr[i]; }
	len -= d;
	if (caret >= b) caret -= d; else if (caret > a) caret = a;
	layoutDirty = true;
}

void RichTextBox::deleteSelection ()
{ int a, b; selRange (a, b); deleteRange (a, b); caret = a; sel = -1; }

// ---- word-wrap layout --------------------------------------------------------

bool RichTextBox::growRows (int need)
{
	if (need <= rowCap) return true;
	int nc = rowCap * 2; if (nc < need) nc = need; if (nc < 64) nc = 64;
	int *ns = new int[nc], *nh = new int[nc], *ny = new int[nc + 1];
	if (ns == 0 || nh == 0 || ny == 0) { delete [] ns; delete [] nh; delete [] ny; return false; }
	for (int i = 0; i < rowN; i++) { ns[i] = rowStart[i]; nh[i] = rowH[i]; ny[i] = rowY[i]; }
	delete [] rowStart; delete [] rowH; delete [] rowY;
	rowStart = ns; rowH = nh; rowY = ny; rowCap = nc; return true;
}

// One visual row starting at char s: walk to the wrap point or the paragraph '\n'.
void RichTextBox::rowAt (int s, int wrapW, int &end, int &h, int &nextStart, bool &hard) const
{
	int fw = rt_fw (), fh = rt_fh ();
	int pe = s; while (pe < len && buf[pe] != '\n') pe++;
	int x = 0, hh = fh, i = s;
	for (; i < pe; i++)
	{
		int sz = (int) ((attr[i] >> 16) & 0xFF); if (sz < 1) sz = 1;
		int adv = fw * sz, gh = fh * sz;
		if (i > s && x + adv > wrapW) break;	// soft-wrap before i (never on the first char)
		x += adv; if (gh > hh) hh = gh;
	}
	if (i < pe) { end = i; nextStart = i; hard = false; }
	else        { end = pe; nextStart = (pe < len) ? pe + 1 : pe; hard = true; }
	h = hh;
}

void RichTextBox::relayout (int wrapW)
{
	rowN = 0;
	int i = 0, y = 0;
	while (true)
	{
		int end, h, ns; bool hard;
		rowAt (i, wrapW, end, h, ns, hard);
		if (!growRows (rowN + 1)) break;
		rowStart[rowN] = i; rowH[rowN] = h; rowY[rowN] = y;
		rowN++; y += h;
		if (end >= len)
		{
			if (hard && len > 0 && buf[len - 1] == '\n' && i != len) { i = ns; continue; }
			break;
		}
		i = ns;
	}
	if (rowN == 0) { growRows (1); rowStart[0] = 0; rowH[0] = rt_fh (); rowY[0] = 0; rowN = 1; y = rt_fh (); }
	rowY[rowN] = y;					// total content height (sentinel)
}

void RichTextBox::ensureLayout ()
{
	const int pad = 4;
	int wrapW = width - 2 * pad - WK_SBW;		// reserve the right-edge scrollbar gutter
	if (wrapW < rt_fw ()) wrapW = rt_fw ();
	if (!layoutDirty && wrapW == lastWrapW) return;
	relayout (wrapW); lastWrapW = wrapW; layoutDirty = false;
	if (top > rowN - 1) top = rowN - 1; if (top < 0) top = 0;
	if (caret > len) caret = len; if (caret < 0) caret = 0;
}

int RichTextBox::rowOfChar (int c)
{
	ensureLayout ();
	int lo = 0, hi = rowN - 1, res = 0;
	while (lo <= hi) { int m = (lo + hi) / 2; if (rowStart[m] <= c) { res = m; lo = m + 1; } else hi = m - 1; }
	return res;
}

int RichTextBox::xInRow (int pos)
{
	int r = rowOfChar (pos), x = 0;
	for (int i = rowStart[r]; i < pos; i++) { if (buf[i] == '\n') break; x += glyphAdvance (i); }
	return x;
}

void RichTextBox::ensureVisible ()
{
	ensureLayout ();
	const int pad = 4;
	int viewH = height - 2 * pad; if (viewH < 1) viewH = 1;
	int cr = rowOfChar (caret);
	if (cr < top) { top = cr; return; }
	while (top < cr && rowY[cr + 1] - rowY[top] > viewH) top++;
}

int RichTextBox::rowCount () { ensureLayout (); return rowN; }

void RichTextBox::setTopRow (int r)
{
	ensureLayout ();
	if (r > rowN - 1) r = rowN - 1; if (r < 0) r = 0;
	if (r != top) { top = r; invalidate (true); }
}

void RichTextBox::moveVert (int dir)
{
	ensureLayout ();
	int r = rowOfChar (caret), nr = r + dir;
	if (nr < 0 || nr >= rowN) return;
	int s = rowStart[nr], e = (nr + 1 < rowN) ? rowStart[nr + 1] : len;
	int ve = s; while (ve < e && buf[ve] != '\n') ve++;
	int x = 0, c = ve;
	for (int i = s; i < ve; i++) { int adv = glyphAdvance (i); if (goalX < x + adv / 2) { c = i; break; } x += adv; c = i + 1; }
	caret = c; sel = -1;
}

int RichTextBox::hitTest (int mx, int my)
{
	ensureLayout ();
	const int pad = 4;
	int targetY = (my - pad) + rowY[top];
	int k = top;
	if (targetY < rowY[top]) k = top;
	else while (k + 1 < rowN && rowY[k + 1] <= targetY) k++;
	int s = rowStart[k], e = (k + 1 < rowN) ? rowStart[k + 1] : len;
	int ve = s; while (ve < e && buf[ve] != '\n') ve++;
	int x = pad, c = ve;
	for (int i = s; i < ve; i++) { int adv = glyphAdvance (i); if (mx < x + adv / 2) { c = i; break; } x += adv; c = i + 1; }
	return c;
}

// ---- styling -----------------------------------------------------------------

void RichTextBox::toggleFlag (unsigned flag)
{
	if (hasSel ())
	{
		int a, b; selRange (a, b);
		bool all = true;
		for (int i = a; i < b; i++) if (buf[i] != '\n' && !(attr[i] & flag)) { all = false; break; }
		for (int i = a; i < b; i++)
		{ if (buf[i] == '\n') continue; if (all) attr[i] &= ~flag; else attr[i] |= flag; }
	}
	else cur.flags = (unsigned char) (cur.flags ^ flag);
	invalidate (true);
}

void RichTextBox::setFg (int idx)
{
	if (hasSel ())
	{
		int a, b; selRange (a, b);
		for (int i = a; i < b; i++) { RtStyle s = rt_unpack (attr[i]); s.fg = (unsigned char) idx; attr[i] = rt_pack (s); }
	}
	else cur.fg = (unsigned char) idx;
	invalidate (true);
}

void RichTextBox::setHilite (int idx)
{
	if (hasSel ())
	{
		int a, b; selRange (a, b);
		for (int i = a; i < b; i++) { RtStyle s = rt_unpack (attr[i]); s.bg = (unsigned char) idx; s.flags |= RT_HILITE; attr[i] = rt_pack (s); }
	}
	else { cur.bg = (unsigned char) idx; cur.flags |= RT_HILITE; }
	invalidate (true);
}

void RichTextBox::clearHilite ()
{
	if (hasSel ())
	{
		int a, b; selRange (a, b);
		for (int i = a; i < b; i++) attr[i] &= ~(unsigned) RT_HILITE;
	}
	else cur.flags = (unsigned char) (cur.flags & ~RT_HILITE);
	invalidate (true);
}

void RichTextBox::setSize (int mult)
{
	if (mult < 1) mult = 1; if (mult > 8) mult = 8;
	if (hasSel ())
	{
		int a, b; selRange (a, b);
		for (int i = a; i < b; i++) { RtStyle s = rt_unpack (attr[i]); s.size = (unsigned char) mult; attr[i] = rt_pack (s); }
	}
	else cur.size = (unsigned char) mult;
	layoutDirty = true; invalidate (true);
}

void RichTextBox::setLevel (int level)
{
	int sz; bool bold;
	switch (level)
	{
	case RT_TITLE1: sz = 3; bold = true;  break;
	case RT_TITLE2: sz = 2; bold = true;  break;
	case RT_TITLE3: sz = 1; bold = true;  break;
	default:        sz = 1; bold = false; break;	// RT_NORMAL
	}
	int a, b; if (hasSel ()) selRange (a, b); else { a = caret; b = caret; }
	int ps = paraStart (a), pe = paraEnd (b);
	for (int i = ps; i < pe; i++)
	{
		if (buf[i] == '\n') continue;
		RtStyle s = rt_unpack (attr[i]); s.size = (unsigned char) sz;
		if (bold) s.flags |= RT_BOLD; else s.flags &= ~(unsigned) RT_BOLD;
		attr[i] = rt_pack (s);
	}
	cur.size = (unsigned char) sz;
	if (bold) cur.flags |= RT_BOLD; else cur.flags = (unsigned char) (cur.flags & ~RT_BOLD);
	layoutDirty = true; invalidate (true);
}

void RichTextBox::selectAll () { sel = 0; caret = len; invalidate (true); }

RtStyle RichTextBox::caretStyle () const
{ return (caret < len) ? rt_unpack (attr[caret]) : cur; }

// ---- paint -------------------------------------------------------------------

void RichTextBox::onDraw ()
{
	ensureLayout ();
	const int pad = 4;
	canvas.clear (disabled ? 0x00E8E8E8 : 0x00FFFFFF);
	canvas.frameRect (0, 0, width, height, C_BORDER);
	if (hasFocus && !disabled) canvas.frameRect (1, 1, width - 2, height - 2, C_ACCENT);

	int viewTop = rowY[top];
	int selA = -1, selB = -1; if (hasSel ()) selRange (selA, selB);

	for (int k = top; k < rowN; k++)
	{
		int ry = pad + rowY[k] - viewTop;
		if (ry >= height - pad) break;
		int rh = rowH[k];
		int s = rowStart[k], e = (k + 1 < rowN) ? rowStart[k + 1] : len;
		int x = pad;
		for (int i = s; i < e; i++)
		{
			if (buf[i] == '\n') break;
			RtStyle st = rt_unpack (attr[i]);
			int adv = rt_fw () * st.size, gh = rt_fh () * st.size;
			int cy = ry + (rh - gh);			// baseline (bottom) align
			if (st.flags & RT_HILITE)          canvas.fillRect (x, cy, adv, gh, rt_color (st.bg));
			if (selA >= 0 && i >= selA && i < selB) canvas.fillRect (x, ry, adv, rh, RT_SEL);
			if (buf[i] != ' ')                 drawGlyph (x, cy, buf[i], st);
			if (st.flags & RT_UNDER)           canvas.fillRect (x, cy + gh - st.size, adv, st.size, rt_color (st.fg));
			if (st.flags & RT_STRIKE)          canvas.fillRect (x, cy + gh / 2,       adv, st.size, rt_color (st.fg));
			x += adv;
			if (x >= width - pad) break;
		}
	}

	if (hasFocus && !disabled && !hasSel ())
	{
		int cr = rowOfChar (caret);
		if (cr >= top)
		{
			int cyr = pad + rowY[cr] - viewTop;
			if (cyr < height - pad)
			{
				int cx = pad + xInRow (caret);
				if (cx < width - 1) canvas.fillRect (cx, cyr, 1, rowH[cr], 0x00000000);
			}
		}
	}

	// Auto vertical scrollbar: shown only when the document is taller than the view.
	int trackH = height - 2;
	WkThumb th = wk_thumb (rowY[rowN], (long) (height - 2 * pad), rowY[top], trackH);
	if (th.show)
		wk_draw_vscroll (canvas, width - WK_SBW - 1, 1, WK_SBW, trackH, th,
				 0x00DCE0E6, barDrag ? 0x00808898 : 0x00A0A8B6);
}

// ---- input -------------------------------------------------------------------

bool RichTextBox::onMouse (int mx, int my, int bl, int br, int bm, int wheel)
{
	(void) br; (void) bm;
	if (mx < 0) { pressed = false; barDrag = false; return false; }
	if (disabled) return true;
	if (wheel) { setTopRow (top - wheel); return true; }

	ensureLayout ();
	const int pad = 4;
	long total = rowY[rowN], view = (long) (height - 2 * pad);
	int trackH = height - 2;
	WkThumb th = wk_thumb (total, view, rowY[top], trackH);
	bool overBar = th.show && mx >= width - WK_SBW - 1;

	if (bl && barDrag)				// continue an in-progress thumb drag
	{
		long pp = wk_thumb_pos (my - 1, trackH, total, view, th.h);
		int r = 0; while (r + 1 < rowN && rowY[r + 1] <= pp) r++;
		setTopRow (r);
		return true;
	}
	if (bl && !pressed)
	{
		pressed = true; setFocus ();
		if (overBar)				// grab the thumb
		{
			barDrag = true;
			long pp = wk_thumb_pos (my - 1, trackH, total, view, th.h);
			int r = 0; while (r + 1 < rowN && rowY[r + 1] <= pp) r++;
			setTopRow (r);
			return true;
		}
		int c = hitTest (mx, my); sel = c; caret = c; goalX = xInRow (caret);
		ensureVisible (); invalidate (true);
	}
	else if (bl && pressed)
	{
		int c = hitTest (mx, my); caret = c; goalX = xInRow (caret);
		ensureVisible (); invalidate (true);
	}
	else if (!bl) { pressed = false; barDrag = false; }
	return true;
}

bool RichTextBox::onKey (long k)
{
	if (disabled) return false;
	ensureLayout ();
	if (k >= 32 && k <= 126)     { if (readonly) return true; if (hasSel ()) deleteSelection (); insertChar ((char) k); }
	else if (k == KEY_ENTER)     { if (readonly) return true; if (hasSel ()) deleteSelection (); insertChar ('\n'); }
	else if (k == KEY_TAB)       { if (readonly) return true; if (hasSel ()) deleteSelection (); insertChar (' '); insertChar (' '); }
	else if (k == KEY_BACKSPACE) { if (readonly) return true; if (hasSel ()) deleteSelection (); else if (caret > 0) { deleteRange (caret - 1, caret); caret--; } }
	else if (k == KEY_DEL)       { if (readonly) return true; if (hasSel ()) deleteSelection (); else deleteRange (caret, caret + 1); }
	else if (k == KEY_LEFT)      { sel = -1; if (caret > 0) caret--; goalX = xInRow (caret); }
	else if (k == KEY_RIGHT)     { sel = -1; if (caret < len) caret++; goalX = xInRow (caret); }
	else if (k == KEY_HOME)      { sel = -1; caret = rowStart[rowOfChar (caret)]; goalX = 0; }
	else if (k == KEY_END)
	{
		sel = -1; int r = rowOfChar (caret), s = rowStart[r], e = (r + 1 < rowN) ? rowStart[r + 1] : len;
		int ve = s; while (ve < e && buf[ve] != '\n') ve++; caret = ve; goalX = xInRow (caret);
	}
	else if (k == KEY_UP)        { moveVert (-1); }
	else if (k == KEY_DOWN)      { moveVert (1); }
	else return false;
	if (k != KEY_UP && k != KEY_DOWN) goalX = xInRow (caret);	// keep the up/down goal column fresh
	ensureLayout (); ensureVisible (); invalidate (true);
	return true;
}

} // namespace wtk
