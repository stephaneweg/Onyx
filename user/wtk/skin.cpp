//
// wtk/skin.cpp -- Skin (9-slice BMP) + window decoration. Compiled into libwtk.a.
//
#include "wtk/skin.h"
#include "wtk/font.h"		// wtk::init (load the global font family for app-drawn windows)
#include "wtk/paint.h"		// the painter (the frame is drawn by code)
#include "wtk/theme.h"		// the frame's colours
#include "wtk/text.h"		// (the frame bypasses an app's text face)
#include "bmp.hpp"		// ui::bmp_decode
#include "kapi.h"		// kapi_get_chrome, kapi_draw_text_buf, kapi_font_height
// operator new[]/delete[] resolve at link from the app's onyxpp.hpp (see canvas.cpp).

namespace wtk {

Skin::Skin () : pix (0), imgW (0), imgH (0), count (1), sw (0), sh (0),
		ml (0), mr (0), mt (0), mb (0) {}
Skin::~Skin () { delete [] pix; }

bool Skin::load (const char *path, int cnt, int l, int r, int t, int b)
{
	delete [] pix; pix = ui::bmp_decode (path, &imgW, &imgH);
	if (pix == 0) { sh = 0; return false; }
	count = cnt < 1 ? 1 : cnt; sw = imgW; sh = imgH / count;
	ml = l; mr = r; mt = t; mb = b;
	return sh > 0;
}

void Skin::blit (unsigned *fb, int W, int H, int sx, int sy, int bw, int bh,
		 int dx, int dy, unsigned tint)
{
	for (int yy = 0; yy < bh; yy++)
	{
		int py = dy + yy, syy = sy + yy;
		if (py < 0 || py >= H || syy < 0 || syy >= imgH) continue;
		for (int xx = 0; xx < bw; xx++)
		{
			int px = dx + xx, sxx = sx + xx;
			if (px < 0 || px >= W || sxx < 0 || sxx >= imgW) continue;
			unsigned c = pix[syy * imgW + sxx];
			if (c == WK_TRANSPARENT_KEY) continue;		// transparent key
			fb[py * W + px] = wk_tint (c, tint);
		}
	}
}

void Skin::drawOn (unsigned *fb, int W, int H, int state, int x, int y, int w, int h,
		   unsigned tint)
{
	if (!valid ()) return;
	int sy = sh * (state % count);
	if (w == sw && h == sh) { blit (fb, W, H, 0, sy, sw, sh, x, y, tint); return; }	// exact

	int midW = sw - ml - mr, midH = sh - mt - mb;		// source middle band
	int outW = w  - ml - mr, outH = h  - mt - mb;		// dest middle band

	if (outW > 0 && outH > 0)				// centre fill (single middle pixel)
	{
		unsigned c = wk_tint (pix[(sy + sh / 2) * imgW + (sw / 2)], tint);
		for (int yy = y + mt; yy < y + mt + outH; yy++) if (yy >= 0 && yy < H)
			for (int xx = x + ml; xx < x + ml + outW; xx++) if (xx >= 0 && xx < W)
				fb[yy * W + xx] = c;
	}
	if (midW > 0 && outW > 0)				// top/bottom edges, tiled
	{
		int fillR = x + w - mr;
		for (int tx = x + ml; tx < fillR; tx += midW)
		{
			int tcw = midW; if (tx + tcw > fillR) tcw = fillR - tx;
			if (mt > 0) blit (fb, W, H, ml, sy,           tcw, mt, tx, y,         tint);
			if (mb > 0) blit (fb, W, H, ml, sy + sh - mb, tcw, mb, tx, y + h - mb, tint);
		}
	}
	if (midH > 0 && outH > 0)				// left/right edges, tiled
	{
		int fillB = y + h - mb;
		for (int ty = y + mt; ty < fillB; ty += midH)
		{
			int tch = midH; if (ty + tch > fillB) tch = fillB - ty;
			if (ml > 0) blit (fb, W, H, 0,       sy + mt, ml, tch, x,          ty, tint);
			if (mr > 0) blit (fb, W, H, sw - mr, sy + mt, mr, tch, x + w - mr, ty, tint);
		}
	}
	if (ml > 0 && mt > 0) blit (fb, W, H, 0,       sy,           ml, mt, x,          y,          tint);
	if (mr > 0 && mt > 0) blit (fb, W, H, sw - mr, sy,           mr, mt, x + w - mr, y,          tint);
	if (ml > 0 && mb > 0) blit (fb, W, H, 0,       sy + sh - mb, ml, mb, x,          y + h - mb, tint);
	if (mr > 0 && mb > 0) blit (fb, W, H, sw - mr, sy + sh - mb, mr, mb, x + w - mr, y + h - mb, tint);
}

// ---- the window frame (the modernised CDE, kapi v64) ------------------------------------------
static int s_winFlags;				// WK_WIN_* (Root: the window menu, resizable, maximised)
void wk_window_state (int flags) { s_winFlags = flags; }
int  wk_window_flags () { return s_winFlags; }

// One copy of the frame (W x H, the client's insets l, t, r, b), from the frame's colour fc:
// a gradient (the title bar lighter), the edge and the theme's outline on the rounded shape, a
// light line along the top, the title buttons (the window menu; minimise, maximise, close), the
// title in bold; then the corners' outside made see-through (the top byte: kapi_abi.h).
// The frame keeps the desktop's bitmap font whatever face the app installed (wtk/text.h): every
// window's title looks the same.
struct BitmapText { TextFace *keep; BitmapText () : keep (wk_face_) { wk_face_ = 0; } ~BitmapText () { wk_face_ = keep; } };

static void draw_frame (unsigned *fb, int W, int H, int T, const char *title, unsigned fc, bool active)
{
	BitmapText bitmap;
	const int R = KAPI_FRAME_RADIUS;
	Canvas cv; cv.adopt (fb, W, H);
	// one continuous gradient: the title bar lighter, the borders going on from it (no line
	// between them)
	wk_rbox (cv, 0, 0, W, T, 0, wk_tone (fc, 166), wk_tone (fc, 134));		// the title bar
	wk_rbox (cv, 0, T, W, H - T, 0, wk_tone (fc, 133), wk_tone (fc, 112));	// the borders
	wk_rline (cv, 0, 0, W, H, R, wk_tone (fc, 70), 170);				// the edge
	if (WK_OUTLINE) wk_rline (cv, 0, 0, W, H, R, WK_OUTLINE == 2 ? 0 : wk_tone (fc, 28), 255);
	for (int i = R; i < W - R; i++) { unsigned *p = fb + W + i; *p = wk_over (*p, 0x00FFFFFF, 110); }	// the top light

	unsigned tbg = wk_tone (fc, 140);						// the title's ink
	unsigned ink = wk_ink_on (tbg);
	if (!active) ink = wk_mix (ink, tbg, 70);
	// the title buttons: the window menu at the left, close / maximise / minimise from the right
	int by = KAPI_FRAME_BTN_Y, bw = KAPI_FRAME_BTN_W, bh = KAPI_FRAME_BTN_H;
	for (int b = 0; b < 4 && !(s_winFlags & WK_WIN_FIXED); b++)	// (a fixed window: none, kapi v69)
	{
		int bx = b == KAPI_FRAME_MENU ? KAPI_FRAME_BTN_EDGE
		       : W - KAPI_FRAME_BTN_EDGE - bw - (b == KAPI_FRAME_CLOSE ? 0 : b == KAPI_FRAME_MAXIMISE ? 1 : 2) * KAPI_FRAME_BTN_STEP;
		if (bx < 0) continue;
		wk_rbox (cv, bx, by, bw, bh, 5, wk_tone (fc, 176), wk_tone (fc, 120), 235);
		wk_rline (cv, bx, by, bw, bh, 5, wk_tone (fc, 64), 150);
		for (int i = bx + 4; i < bx + bw - 4; i++) { unsigned *p = fb + (by + 1) * W + i; *p = wk_over (*p, 0x00FFFFFF, 80); }
		int cx = bx + bw / 2, cy = by + bh / 2;
		unsigned gc = ink;
		switch (b)
		{
		case KAPI_FRAME_MENU:
			if (!(s_winFlags & WK_WIN_MENU)) gc = wk_mix (ink, wk_tone (fc, 150), 150);	// (no menu)
			wk_glyph (cv, WKG_MENU, cx, cy, 12, gc);
			break;
		case KAPI_FRAME_CLOSE:    wk_glyph (cv, WKG_CLOSE, cx, cy, 10, gc); break;
		case KAPI_FRAME_MINIMISE: wk_glyph (cv, WKG_MIN, cx, cy + 1, 11, gc); break;
		case KAPI_FRAME_MAXIMISE:
			if (!(s_winFlags & WK_WIN_RESIZABLE)) gc = wk_mix (ink, wk_tone (fc, 150), 150);	// (it cannot)
			wk_glyph (cv, (s_winFlags & WK_WIN_MAXIMISED) ? WKG_RESTORE : WKG_MAX, cx, cy, 11, gc);
			break;
		}
	}
	// the title, bold, centred on the window if it can be, else between the buttons
	char t[48]; int n = 0;
	for (; title && title[n] && n < 47; n++) t[n] = title[n];
	t[n] = '\0';
	int lo = KAPI_FRAME_BTN_EDGE + bw + 8, hi = W - KAPI_FRAME_BTN_EDGE - 2 * KAPI_FRAME_BTN_STEP - bw - 8;
	int fw = wk_text_w ("M", 2);
	if (fw < 1) fw = 8;
	int maxc = (hi - lo) / fw;
	if (maxc < 0) maxc = 0;
	if (n > maxc) { n = maxc; if (n >= 2) { t[n - 1] = '.'; t[n - 2] = '.'; } t[n] = '\0'; }
	int tw = wk_text_w (t, 2), tx = (W - tw) / 2;
	if (tx < lo) tx = lo;
	if (tx + tw > hi) tx = hi - tw;
	if (n > 0) wk_text_l (cv, tx, 0, T, t, ink, 2);

	// the rounded corners' outside: see-through (its partly covered pixels: in part)
	const WkCorner &c = wk_corner (R);
	for (int j = 0; j < R && j < H / 2; j++)
		for (int i = 0; i < R && i < W / 2; i++)
		{
			unsigned a = i < c.off[j] ? 0 : i < c.off[j] + c.n[j] ? c.a[j][i - c.off[j]] : 255;
			if (a == 255) break;
			unsigned tt = (255 - a) << 24;
			unsigned *q[4] = { fb + j * W + i, fb + j * W + W - 1 - i, fb + (H - 1 - j) * W + i, fb + (H - 1 - j) * W + W - 1 - i };
			for (int k = 0; k < 4; k++) *q[k] = (*q[k] & 0x00FFFFFFu) | tt;
		}
}

void wk_draw_frame (unsigned *fb, int W, int H, int T, const char *title, unsigned frame, bool active)
{
	draw_frame (fb, W, H, T, title, frame, active);
}

void wk_decorate_window ()
{
	static int s_w = -1, s_h = -1;			// what was drawn (redrawn when it changes)
	static unsigned s_sig = 0;

	init ();					// the fonts and the theme (every app-drawn window calls
							// this; Root apps init () too -- idempotent)
	struct kapi_chrome c;
	if (!kapi_get_chrome (&c) || c.active == 0) return;	// no window / borderless
	unsigned sig = C_FRAME_ACTIVE * 31u + C_FRAME_INACTIVE * 7u + (unsigned) WK_OUTLINE * 3u + (unsigned) s_winFlags;
	for (int i = 0; c.title[i]; i++) sig = sig * 33u + (unsigned char) c.title[i];
	if (c.chrome_w == s_w && c.chrome_h == s_h && sig == s_sig) return;
	s_w = c.chrome_w; s_h = c.chrome_h; s_sig = sig;
	draw_frame (c.active, c.chrome_w, c.chrome_h, c.inset_t, c.title, C_FRAME_ACTIVE, true);
	if (c.inactive) draw_frame (c.inactive, c.chrome_w, c.chrome_h, c.inset_t, c.title, C_FRAME_INACTIVE, false);
}

} // namespace wtk
