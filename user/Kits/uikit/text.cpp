//
// uikit/text.cpp -- the text-rendering hook (uikit/text.h): the installed face, and the measures and
// draws every text path of uikit goes through (the bitmap fonts' when no face is installed).
//
#include "uikit/text.h"
#include "uikit/widget.h"
#include "uikit/font.h"
#include "uikit/paint.h"		// uk_blend_px, uk_tone (uk_text_over)
#include "appkit/appkit.h"		// kapi_font_width / height (the bitmap cell)
// operator new[]/delete[] resolve at link from the app's onyxpp.hpp (see canvas.cpp).

namespace uikit {

// (uk_face_, uk_face_fw_: uikit/globals.cpp)

int TextFace::widthN (const char *s, int n, int style)
{
	if (s == 0 || n <= 0) return 0;
	char b[256];
	char *p = n < (int) sizeof b ? b : new char[n + 1];
	if (p == 0) return 0;
	int i = 0;
	for (; i < n && s[i]; i++) p[i] = s[i];
	p[i] = '\0';
	int w = width (p, style);
	if (p != b) delete [] p;
	return w;
}

void uk_set_textface (TextFace *f)
{
	uk_face_ = f;
	uk_face_fw_ = f ? f->width ("0", 0) : 8;
	if (uk_face_fw_ < 1) uk_face_fw_ = 1;
}

int uk_bfw () { int f = kapi_font_width  (); return f < 1 ? 8  : f; }
int uk_bfh () { int f = kapi_font_height (); return f < 1 ? 16 : f; }

int uk_tw (const char *s, int style)
{
	if (uk_face_) return s ? uk_face_->width (s, style) : 0;
	return uk_len (s) * uk_fw ();
}

int uk_tw_n (const char *s, int n, int style)
{
	if (s == 0 || n <= 0) return 0;
	if (uk_face_) return uk_face_->widthN (s, n, style);
	return n * uk_fw ();
}

int uk_tpos (const char *s, int n, int x, int style)
{
	if (s == 0 || n <= 0 || x <= 0) return 0;
	TextFace *t = uk_face_;
	if (t == 0) { int fw = uk_fw (), i = (x + fw / 2) / fw; return i > n ? n : i; }
	int pi = 0, px = 0;					// the previous place and its x
	for (int i = uk_u8_next (s, 0, n); ; i = uk_u8_next (s, i, n))
	{
		int w = t->widthN (s, i, style);
		if (w >= x) return x - px <= w - x ? pi : i;
		if (i >= n) return n;
		pi = i; px = w;
	}
}

void uk_text (Canvas &cv, int x, int y, const char *s, unsigned c, int style)
{
	if (s == 0) return;
	if (uk_face_) { uk_face_->draw (cv, x, y, s, c, style); return; }
	Font &f = font ();
	if (f.valid ()) cv.drawFont (x, y, s, f, c, 1, style);
	else cv.text (x, y, s, c);
}

void uk_text_clip (Canvas &cv, int x, int y, const char *s, unsigned c, int style,
		   int cx, int cy, int cw, int ch)
{
	if (cx < 0) { cw += cx; cx = 0; }
	if (cy < 0) { ch += cy; cy = 0; }
	if (cx + cw > cv.w) cw = cv.w - cx;
	if (cy + ch > cv.h) ch = cv.h - cy;
	if (s == 0 || cw <= 0 || ch <= 0) return;
	Canvas sub;						// the box: a window on the canvas's pixels
	sub.adopt (cv.px + cy * cv.stride + cx, cw, ch, cv.stride);
	uk_text (sub, x - cx, y - cy, s, c, style);
}

int uk_text_fit (const char *s, int w, char *out, int cap, int style)
{
	if (out == 0 || cap < 1) return 0;
	int n = 0;
	for (; s && s[n] && n < cap - 1; n++) out[n] = s[n];
	out[n] = '\0';
	int tw = uk_tw (out, style);
	if (tw <= w) return tw;
	int dots = uk_tw ("...", style), k = 0;
	for (int i = uk_u8_next (out, 0, n); i <= n; i = uk_u8_next (out, i, n))
	{
		if (uk_tw_n (out, i, style) + dots > w) break;
		k = i;
		if (i >= n) break;
	}
	if (k + 3 < cap && w >= dots) { out[k] = out[k + 1] = out[k + 2] = '.'; out[k + 3] = '\0'; }
	else out[k] = '\0';
	return uk_tw (out, style);
}

static bool wrap_blank (char c) { return c == ' ' || c == '\t' || c == '\r'; }

int uk_text_wrap (const char *s, int n, int w, int maxLines, int *start, int *len, bool *more, int style)
{
	if (more) *more = false;
	if (s == 0 || n <= 0) return 0;
	int k = 0, i = 0;
	while (i < n)
	{
		if (k >= maxLines || start == 0 || len == 0)
		{
			for (int j = i; more && j < n; j++) if (!wrap_blank (s[j]) && s[j] != '\n') { *more = true; break; }
			break;
		}
		int le = i;					// this paragraph's end (its '\n' or n)
		while (le < n && s[le] != '\n') le++;
		int e = le, next = le + 1;			// the line [i, e), the next one from next
		if (uk_tw_n (s + i, le - i, style) > w)		// too wide: break it
		{
			int fit = i, sp = -1;			// the last character's end that fits; the last space that does
			for (int j = i; j < le; )
			{
				int jn = uk_u8_next (s, j, le);
				if (s[j] == ' ' && j > i) sp = j;
				if (uk_tw_n (s + i, jn - i, style) > w) break;
				fit = jn; j = jn;
			}
			if (fit < le && s[fit] == ' ') sp = fit;		// (the break right at a space)
			if (sp > i)				// at the space: the spaces around it dropped
			{
				e = sp; next = sp;
				while (next < le && s[next] == ' ') next++;
			}
			else					// a word wider than the line: cut at a character
			{
				if (fit == i) fit = uk_u8_next (s, i, le);	// (one at least)
				e = fit; next = fit;
			}
			while (e > i && wrap_blank (s[e - 1])) e--;
			if (next >= le) next = le + 1;		// (the paragraph done: past its '\n')
		}
		else while (e > i && wrap_blank (s[e - 1])) e--;
		start[k] = i; len[k] = e - i; k++;
		i = next;
	}
	return k;
}

void uk_text_over (Canvas &cv, int x, int y, const char *s, unsigned ink, int style, int shade, unsigned back)
{
	if (s == 0 || !*s) return;
	// the glyphs in white on black in a scratch canvas (whatever draws them: the face, the bitmap fonts),
	// then their coverage blended over the canvas -- which heeds the alpha mode (uk_blend_px)
	static Canvas sc;
	int w = uk_tw (s, style) + 2, h = uk_fh () + 2;
	if (w < 1 || h < 1 || !sc.resize (w, h)) return;
	sc.clear (0);
	uk_text (sc, 0, 0, s, 0x00FFFFFF, style);
	unsigned edge = uk_tone (back, 205);
	for (int pass = shade ? 0 : 1; pass < 2; pass++)
		for (int ry = 0; ry < h; ry++)
			for (int rx = 0; rx < w; rx++)
			{
				int a = (int) ((sc.px[ry * sc.stride + rx] >> 8) & 255);	// (the green: the coverage)
				if (a == 0) continue;
				int px = x + rx, py = y + ry;
				if (pass == 1) uk_blend_px (cv, px, py, ink, a);
				else if (shade == 2) uk_blend_px (cv, px, py + 1, edge, a * 200 / 255);	// engraved
				else						// a soft shadow (the agenda's)
				{
					uk_blend_px (cv, px + 1, py + 1, 0, a * 150 / 255);
					uk_blend_px (cv, px + 2, py + 2, 0, a * 60 / 255);
					uk_blend_px (cv, px, py + 2, 0, a * 40 / 255);
					uk_blend_px (cv, px + 2, py, 0, a * 40 / 255);
				}
			}
}

} // namespace uikit
