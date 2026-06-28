//
// wtk/skin.cpp -- Skin (9-slice BMP) + window decoration. Compiled into libwtk.a.
//
#include "wtk/skin.h"
#include "wtk/font.h"		// wtk::init (load the global font family for app-drawn windows)
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

// ---- shared skins ------------------------------------------------------------
Skin &wk_button_skin ()
{
	static Skin s; static bool tried = false;
	if (!tried) { tried = true; s.load ("SD:/skins/button.bmp", 3, 6, 6, 6, 6); }
	return s;
}
static Skin &windowSkin ()
{
	static Skin s; static bool t = false;
	if (!t) { t = true; s.load ("SD:/skins/wings.bmp", 1, 7, 7, 32, 7); }
	return s;
}
static Skin &closeSkin ()
{
	static Skin s; static bool t = false;
	if (!t) { t = true; s.load ("SD:/skins/closebgs.bmp", 3, 5, 5, 5, 5); }
	return s;
}

// Window-chrome tints (active = warm gold, inactive = muted slate) so focus reads at a
// glance -- matching uikit's UI_CHROME_TINT_*.
#define WK_CHROME_TINT_ACTIVE	0x00FFC878
#define WK_CHROME_TINT_INACTIVE	0x008090A0

// Clipped line into a raw 0x00RRGGBB buffer (the close-box X glyph).
static void wk_line (unsigned *fb, int W, int H, int x0, int y0, int x1, int y1, unsigned c)
{
	int dx = x1 - x0, dy = y1 - y0;
	int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
	int steps = adx > ady ? adx : ady; if (steps < 1) steps = 1;
	for (int i = 0; i <= steps; i++)
	{
		int px = x0 + dx * i / steps, py = y0 + dy * i / steps;
		if (px >= 0 && px < W && py >= 0 && py < H) fb[py * W + px] = c;
	}
}

void wk_decorate_window ()
{
	static bool s_done = false;
	if (s_done) return;
	s_done = true;

	init ();					// load the global font family (every app-drawn
							// window calls this; Root apps init() too -- idempotent)
	struct kapi_chrome c;
	if (!kapi_get_chrome (&c) || c.active == 0) return;	// no window / borderless
	Skin &ws = windowSkin ();
	Skin &cs = closeSkin ();
	int W = c.chrome_w, H = c.chrome_h, T = c.inset_t;
	int fh = kapi_font_height (); if (fh < 1) fh = 16;

	unsigned *bufs[2]  = { c.active, c.inactive };
	unsigned  tints[2] = { WK_CHROME_TINT_ACTIVE, WK_CHROME_TINT_INACTIVE };
	for (int k = 0; k < 2; k++)
	{
		unsigned *fb = bufs[k];
		if (fb == 0) continue;
		// Opaque background (square chrome), seeded with the skin's tinted border tone.
		unsigned base = ws.valid () ? wk_tint (ws.pix[(ws.sh / 2) * ws.imgW + 3], tints[k])
					    : 0x00384048;
		for (int i = 0; i < W * H; i++) fb[i] = base;
		if (ws.valid ()) ws.drawOn (fb, W, H, 0, 0, 0, W, H, tints[k]);

		// Close box [x] at the right of the title bar (matches the kernel CloseBoxRect).
		int nSize = T - 10; if (nSize < 6) nSize = 6;
		int cbx1 = W - 5, cbx0 = cbx1 - nSize, cby0 = 5, cby1 = cby0 + nSize;
		if (cs.valid ()) cs.drawOn (fb, W, H, 0, cbx0, cby0, cbx1 - cbx0 + 1, cby1 - cby0 + 1);
		wk_line (fb, W, H, cbx0 + 3, cby0 + 3, cbx1 - 3, cby1 - 3, 0x00FFFFFF);
		wk_line (fb, W, H, cbx1 - 3, cby0 + 3, cbx0 + 3, cby1 - 3, 0x00FFFFFF);

		// Title text, vertically centred in the title bar (inside the left border) -- in
		// the global font like everything else (kernel-font fallback if no .fnt loaded).
		draw_text (fb, W, H, c.inset_l + 2, (T - fh) / 2, c.title, 0x00FFFFFF);
	}
}

} // namespace wtk
