//
// uikit/skin.cpp -- Skin (9-slice BMP) + window decoration. Compiled into libuikit.a.
//
#include "uikit/skin.h"
#include "uikit/font.h"		// uikit::init (load the global font family for app-drawn windows)
#include "uikit/paint.h"		// the painter (the frame is drawn by code)
#include "uikit/theme.h"		// the frame's colours
#include "uikit/text.h"		// (the frame bypasses an app's text face)
#include "bmp.h"		// ui::bmp_decode
#include "appkit/appkit.h"		// kapi_get_chrome, kapi_draw_text_buf, kapi_font_height
// operator new[]/delete[] resolve at link from the app's onyxpp.hpp (see canvas.cpp).

namespace uikit {

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
			if (c == UK_TRANSPARENT_KEY) continue;		// transparent key
			fb[py * W + px] = uk_tint (c, tint);
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
		unsigned c = uk_tint (pix[(sy + sh / 2) * imgW + (sw / 2)], tint);
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
static int s_winFlags;				// UK_WIN_* (Root: the window menu, resizable, maximised)
void uk_window_state (int flags) { s_winFlags = flags; }
int  uk_window_flags () { return s_winFlags; }

// One copy of the frame (W x H, the client's insets l, t, r, b), from the frame's colour fc:
// a gradient (the title bar lighter; Milk's ends on the window's colour), the edge and the
// theme's outline on the rounded shape, a light line along the top, the title buttons (the
// window menu; minimise, maximise, close), the title in bold; then the corners' outside made
// see-through (the top byte: kapi_abi.h).
// The title's face: SD:/res/fonts/title.aaf -- DejaVu Sans Bold at 13 px, rendered by the apps' FreeType
// (tools/title_font/gen_title_font.cpp: its format) and kept as anti-aliased bitmaps, read once by every
// app's frame: the same title in every window, an app with FreeType or not. Without the file: the
// desktop's bitmap font, whatever face the app installed (uikit/text.h).
class AafFace : public TextFace
{
public:
	struct G { unsigned short adv; signed char l, t; unsigned char w, h; unsigned off; };
	bool ok, tried;
	AafFace () : ok (false), tried (false), m_h (16), m_asc (12), m_n (0), m_nk (0), m_g (0), m_k (0), m_data (0) {}
	void load ()
	{
		if (tried) return;
		tried = true;
		void *f = kapi_open ("SD:/res/fonts/title.aaf");
		if (!f) return;
		unsigned n = kapi_fsize (f);
		unsigned char *b = n >= 12 && n < (1u << 20) ? new unsigned char[n] : 0;
		int r = b ? kapi_read (f, b, n) : -1;
		kapi_close (f);
		if (!b || r != (int) n || b[0] != 'A' || b[1] != 'A' || b[2] != 'F' || b[3] != '1') { delete [] b; return; }
		m_h = b[4] | b[5] << 8; m_asc = b[6] | b[7] << 8; m_n = b[8] | b[9] << 8; m_nk = b[10] | b[11] << 8;
		unsigned tab = 12, kern = tab + (unsigned) m_n * 10, data = kern + (unsigned) m_nk * 4;
		if (data > n || m_n <= 0) { delete [] b; return; }
		m_g = new G[m_n];
		for (int i = 0; i < m_n; i++)
		{
			const unsigned char *q = b + tab + i * 10;
			G &g = m_g[i];
			g.adv = (unsigned short) (q[0] | q[1] << 8); g.l = (signed char) q[2]; g.t = (signed char) q[3]; g.w = q[4]; g.h = q[5];
			g.off = data + (q[6] | q[7] << 8 | q[8] << 16 | (unsigned) q[9] << 24);
			if (g.off + (unsigned) g.w * g.h > n) g.w = g.h = 0;
		}
		m_k = b + kern; m_data = b;
		ok = true;
	}
	int height () override { return m_h; }
	int ascent () override { return m_asc; }
	int width (const char *s, int style) override { return widthN (s, 1 << 30, style); }
	int widthN (const char *s, int n, int) override
	{
		long x = 0; unsigned prev = 0;
		int len = 0; while (len < n && s && s[len]) len++;
		for (int i = 0; i < len; )
		{
			int k; unsigned cp = uk_u8_get (s + i, len - i, &k); i += k;
			x += kern (prev, cp) + glyph (cp).adv; prev = cp;
		}
		return (int) ((x + 32) >> 6);
	}
	void draw (Canvas &cv, int x, int yTop, const char *s, unsigned c, int) override
	{
		long x64 = (long) x * 64; unsigned prev = 0;
		int len = 0; while (s && s[len]) len++;
		int base = yTop + m_asc;
		for (int i = 0; i < len; )
		{
			int k; unsigned cp = uk_u8_get (s + i, len - i, &k); i += k;
			x64 += kern (prev, cp); prev = cp;
			const G &g = glyph (cp);
			int gx = (int) ((x64 + 32) >> 6) + g.l, gy = base - g.t;
			const unsigned char *b = m_data + g.off;
			for (int j = 0; j < g.h; j++)
				for (int i2 = 0; i2 < g.w; i2++)
					if (b[j * g.w + i2]) uk_blend_px (cv, gx + i2, gy + j, c, b[j * g.w + i2]);
			x64 += g.adv;
		}
	}
private:
	int m_h, m_asc, m_n, m_nk; G *m_g; const unsigned char *m_k, *m_data;
	const G &glyph (unsigned cp) const
	{
		int i = (int) cp - 32;
		if (i < 0 || i >= m_n) i = '?' - 32;
		return m_g[i];
	}
	int kern (unsigned a, unsigned b) const
	{
		if (!a || a > 126 || b > 126) return 0;
		for (int i = 0; i < m_nk; i++) if (m_k[4 * i] == a && m_k[4 * i + 1] == b) return (short) (m_k[4 * i + 2] | m_k[4 * i + 3] << 8);
		return 0;
	}
};
static AafFace s_titleFace;
static TextFace *title_face () { s_titleFace.load (); return s_titleFace.ok ? &s_titleFace : 0; }
// For the time of the frame: the title's face (or the bitmap font), the app's back after it.
struct TitleText
{
	TextFace *keep; int keepFw;
	TitleText () : keep (uk_face_), keepFw (uk_face_fw_)
	{
		TextFace *t = title_face ();
		uk_face_ = t;
		if (t) { uk_face_fw_ = t->width ("0", 0); if (uk_face_fw_ < 1) uk_face_fw_ = 1; }
	}
	~TitleText () { uk_face_ = keep; uk_face_fw_ = keepFw; }
};

static void draw_frame (unsigned *fb, int W, int H, int T, const char *title, unsigned fc, bool active)
{
	TitleText titleText;
	const int R = KAPI_FRAME_RADIUS;
	Canvas cv; cv.adopt (fb, W, H);
	// one continuous gradient: the title bar lighter, the borders going on from it (no line
	// between them)
	// (Milk: the frame melts into the window -- the title bar from a light tone of the frame's
	// colour down to the content's own, C_BG, the borders that colour: nothing between the frame
	// and what the window shows)
	unsigned tbg = uk_tone (fc, 140);						// (under the title)
	if (UK_STYLE == UK_STYLE_MILK)
	{
		unsigned top = uk_bright (fc) < 110 ? fc : uk_tone (fc, 230);	// (a dark frame: darker at the top)
		uk_rbox (cv, 0, 0, W, T, 0, top, C_BG);
		cv.fillRect (0, T, W, H - T, uk_bright (C_BG) < 110 ? 0x00000000u : C_BG);	// (a dark window: black borders)
		tbg = uk_mix (top, C_BG, 128);
	}
	else
	{
		uk_rbox (cv, 0, 0, W, T, 0, uk_tone (fc, 166), uk_tone (fc, 134));	// the title bar
		uk_rbox (cv, 0, T, W, H - T, 0, uk_tone (fc, 133), uk_tone (fc, 112));	// the borders
	}
	uk_rline (cv, 0, 0, W, H, R, uk_tone (fc, 70), 170);				// the edge
	if (UK_OUTLINE) uk_rline (cv, 0, 0, W, H, R, UK_OUTLINE == 2 ? 0 : uk_tone (fc, 28), 255);
	for (int i = R; i < W - R; i++) { unsigned *p = fb + W + i; *p = uk_over (*p, 0x00FFFFFF, 110); }	// the top light

	unsigned ink = uk_ink_on (tbg);							// the title's ink
	if (!active) ink = uk_mix (ink, tbg, 70);
	// the title buttons: the window menu at the left, close / maximise / minimise from the right
	int by = KAPI_FRAME_BTN_Y, bw = KAPI_FRAME_BTN_W, bh = KAPI_FRAME_BTN_H;
	for (int b = 0; b < 4 && !(s_winFlags & UK_WIN_FIXED); b++)	// (a fixed window: none, kapi v69)
	{
		int bx = b == KAPI_FRAME_MENU ? KAPI_FRAME_BTN_EDGE
		       : W - KAPI_FRAME_BTN_EDGE - bw - (b == KAPI_FRAME_CLOSE ? 0 : b == KAPI_FRAME_MAXIMISE ? 1 : 2) * KAPI_FRAME_BTN_STEP;
		if (bx < 0) continue;
		if (UK_STYLE == UK_STYLE_MILK)				// Milk: OS X's beads (greyed: behind, or
		{							// a button the window cannot use)
			const int d = 14;
			int cx = bx + bw / 2, cy = T / 2;
			bool off = !active || (b == KAPI_FRAME_MAXIMISE && !(s_winFlags & UK_WIN_RESIZABLE))
				   || (b == KAPI_FRAME_MENU && !(s_winFlags & UK_WIN_MENU));
			unsigned c = off ? 0x00C2C3C8
				   : b == KAPI_FRAME_CLOSE ? 0x00E8564E : b == KAPI_FRAME_MINIMISE ? 0x00F0B43A
				   : b == KAPI_FRAME_MAXIMISE ? 0x004CB653 : 0x009AA8BA;
			uk_bead (cv, cx - d / 2, cy - d / 2, d, c);
			if (b == KAPI_FRAME_MENU) uk_glyph (cv, WKG_MENU, cx, cy, 8, uk_tone (c, 34));	// (its bar)
			continue;
		}
		uk_rbox (cv, bx, by, bw, bh, 5, uk_tone (fc, 176), uk_tone (fc, 120), 235);
		uk_rline (cv, bx, by, bw, bh, 5, uk_tone (fc, 64), 150);
		for (int i = bx + 4; i < bx + bw - 4; i++) { unsigned *p = fb + (by + 1) * W + i; *p = uk_over (*p, 0x00FFFFFF, 80); }
		int cx = bx + bw / 2, cy = by + bh / 2;
		unsigned gc = ink;
		switch (b)
		{
		case KAPI_FRAME_MENU:
			if (!(s_winFlags & UK_WIN_MENU)) gc = uk_mix (ink, uk_tone (fc, 150), 150);	// (no menu)
			uk_glyph (cv, WKG_MENU, cx, cy, 12, gc);
			break;
		case KAPI_FRAME_CLOSE:    uk_glyph (cv, WKG_CLOSE, cx, cy, 10, gc); break;
		case KAPI_FRAME_MINIMISE: uk_glyph (cv, WKG_MIN, cx, cy + 1, 11, gc); break;
		case KAPI_FRAME_MAXIMISE:
			if (!(s_winFlags & UK_WIN_RESIZABLE)) gc = uk_mix (ink, uk_tone (fc, 150), 150);	// (it cannot)
			uk_glyph (cv, (s_winFlags & UK_WIN_MAXIMISED) ? WKG_RESTORE : WKG_MAX, cx, cy, 11, gc);
			break;
		}
	}
	// the title, bold, centred on the window if it can be, else between the buttons
	int lo = KAPI_FRAME_BTN_EDGE + bw + 8, hi = W - KAPI_FRAME_BTN_EDGE - 2 * KAPI_FRAME_BTN_STEP - bw - 8;
	char t[160]; t[0] = '\0';
	if (title && hi > lo) uk_text_fit (title, hi - lo, t, sizeof t, 2);
	int n = 0; while (t[n]) n++;
	int tw = uk_text_w (t, 2), tx = (W - tw) / 2;
	if (tx < lo) tx = lo;
	if (tx + tw > hi) tx = hi - tw;
	if (n > 0) uk_text_l (cv, tx, 0, T, t, ink, 2);

	// the rounded corners' outside: see-through (its partly covered pixels: in part)
	const UkCorner &c = uk_corner (R);
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

void uk_draw_frame (unsigned *fb, int W, int H, int T, const char *title, unsigned frame, bool active)
{
	draw_frame (fb, W, H, T, title, frame, active);
}

void uk_decorate_window ()
{
	static int s_w = -1, s_h = -1;			// what was drawn (redrawn when it changes)
	static unsigned s_sig = 0;

	init ();					// the fonts and the theme (every app-drawn window calls
							// this; Root apps init () too -- idempotent)
	struct kapi_chrome c;
	if (!kapi_get_chrome (&c) || c.active == 0) return;	// no window / borderless
	unsigned sig = C_FRAME_ACTIVE * 31u + C_FRAME_INACTIVE * 7u + (unsigned) UK_OUTLINE * 3u + (unsigned) s_winFlags
		       + (UK_STYLE == UK_STYLE_MILK ? 101u + C_BG * 13u : 0u);	// (Milk's: the window's colour too)
	for (int i = 0; c.title[i]; i++) sig = sig * 33u + (unsigned char) c.title[i];
	if (c.chrome_w == s_w && c.chrome_h == s_h && sig == s_sig) return;
	s_w = c.chrome_w; s_h = c.chrome_h; s_sig = sig;
	draw_frame (c.active, c.chrome_w, c.chrome_h, c.inset_t, c.title, C_FRAME_ACTIVE, true);
	if (c.inactive) draw_frame (c.inactive, c.chrome_w, c.chrome_h, c.inset_t, c.title, C_FRAME_INACTIVE, false);
}

} // namespace uikit
