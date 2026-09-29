//
// wtk/text.cpp -- the text-rendering hook (wtk/text.h): the installed face, and the measures and
// draws every text path of wtk goes through (the bitmap fonts' when no face is installed).
//
#include "wtk/text.h"
#include "wtk/widget.h"
#include "wtk/font.h"
#include "kapi.h"		// kapi_font_width / height (the bitmap cell)
// operator new[]/delete[] resolve at link from the app's onyxpp.hpp (see canvas.cpp).

namespace wtk {

TextFace *wk_face_ = 0;
int	  wk_face_fw_ = 8;

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

void wk_set_textface (TextFace *f)
{
	wk_face_ = f;
	wk_face_fw_ = f ? f->width ("0", 0) : 8;
	if (wk_face_fw_ < 1) wk_face_fw_ = 1;
}

int wk_bfw () { int f = kapi_font_width  (); return f < 1 ? 8  : f; }
int wk_bfh () { int f = kapi_font_height (); return f < 1 ? 16 : f; }

int wk_tw (const char *s, int style)
{
	if (wk_face_) return s ? wk_face_->width (s, style) : 0;
	return wk_len (s) * wk_fw ();
}

int wk_tw_n (const char *s, int n, int style)
{
	if (s == 0 || n <= 0) return 0;
	if (wk_face_) return wk_face_->widthN (s, n, style);
	return n * wk_fw ();
}

int wk_tpos (const char *s, int n, int x, int style)
{
	if (s == 0 || n <= 0 || x <= 0) return 0;
	TextFace *t = wk_face_;
	if (t == 0) { int fw = wk_fw (), i = (x + fw / 2) / fw; return i > n ? n : i; }
	int pi = 0, px = 0;					// the previous place and its x
	for (int i = wk_u8_next (s, 0, n); ; i = wk_u8_next (s, i, n))
	{
		int w = t->widthN (s, i, style);
		if (w >= x) return x - px <= w - x ? pi : i;
		if (i >= n) return n;
		pi = i; px = w;
	}
}

void wk_text (Canvas &cv, int x, int y, const char *s, unsigned c, int style)
{
	if (s == 0) return;
	if (wk_face_) { wk_face_->draw (cv, x, y, s, c, style); return; }
	Font &f = font ();
	if (f.valid ()) cv.drawFont (x, y, s, f, c, 1, style);
	else cv.text (x, y, s, c);
}

void wk_text_clip (Canvas &cv, int x, int y, const char *s, unsigned c, int style,
		   int cx, int cy, int cw, int ch)
{
	if (cx < 0) { cw += cx; cx = 0; }
	if (cy < 0) { ch += cy; cy = 0; }
	if (cx + cw > cv.w) cw = cv.w - cx;
	if (cy + ch > cv.h) ch = cv.h - cy;
	if (s == 0 || cw <= 0 || ch <= 0) return;
	Canvas sub;						// the box: a window on the canvas's pixels
	sub.adopt (cv.px + cy * cv.stride + cx, cw, ch, cv.stride);
	wk_text (sub, x - cx, y - cy, s, c, style);
}

int wk_text_fit (const char *s, int w, char *out, int cap, int style)
{
	if (out == 0 || cap < 1) return 0;
	int n = 0;
	for (; s && s[n] && n < cap - 1; n++) out[n] = s[n];
	out[n] = '\0';
	int tw = wk_tw (out, style);
	if (tw <= w) return tw;
	int dots = wk_tw ("...", style), k = 0;
	for (int i = wk_u8_next (out, 0, n); i <= n; i = wk_u8_next (out, i, n))
	{
		if (wk_tw_n (out, i, style) + dots > w) break;
		k = i;
		if (i >= n) break;
	}
	if (k + 3 < cap && w >= dots) { out[k] = out[k + 1] = out[k + 2] = '.'; out[k + 3] = '\0'; }
	else out[k] = '\0';
	return wk_tw (out, style);
}

} // namespace wtk
