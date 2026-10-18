//
// pc/3DForge/core/shim/shim.cpp -- forgeshim.h's code: the canvas, the painter's shapes over uikit/vpaint.cpp (built with
// this), the text by GDI+ (Segoe UI), the clock, a picture read by GDI+.
//
// MIT licence (Onyx).
//
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include "forgeshim.h"

namespace uikit {

unsigned C_ACCENT = 0x2B6CD4, C_TEXT = 0x1E2026, C_BG = 0xF3F4F7, C_FIELD = 0xFFFFFF, C_FIELD_TEXT = 0x1E2026, C_DIS = 0xA0A4AC,
	 C_BUTTON = 0xFBFBFC, C_BUTTON_TEXT = 0x1E2026, C_FACE = 0xF3F4F7;

// ---- the canvas -----------------------------------------------------------------------------------------------------------
void Canvas::clear (unsigned c) { for (int y = 0; y < h; y++) { unsigned *r = px + (size_t) y * stride; for (int x = 0; x < w; x++) r[x] = c; } }
void Canvas::fillRect (int x, int y, int rw, int rh, unsigned c)
{
	int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + rw > w ? w : x + rw, y1 = y + rh > h ? h : y + rh;
	for (int j = y0; j < y1; j++) { unsigned *r = px + (size_t) j * stride; for (int i = x0; i < x1; i++) r[i] = c; }
}
void Canvas::frameRect (int x, int y, int rw, int rh, unsigned c)
{
	fillRect (x, y, rw, 1, c); fillRect (x, y + rh - 1, rw, 1, c); fillRect (x, y, 1, rh, c); fillRect (x + rw - 1, y, 1, rh, c);
}

// ---- colours ----------------------------------------------------------------------------------------------------------------
void uk_paint_alpha (bool) {}
unsigned uk_mix (unsigned a, unsigned b, int t)
{
	if (t < 0) t = 0; if (t > 256) t = 256;
	unsigned rb = ((a & 0xFF00FF) * (256 - t) + (b & 0xFF00FF) * t) >> 8 & 0xFF00FF, g = ((a & 0x00FF00) * (256 - t) + (b & 0x00FF00) * t) >> 8 & 0x00FF00;
	return rb | g;
}
unsigned uk_tone (unsigned c, int level)
{
	if (level >= 128) return uk_mix (c & 0xFFFFFF, 0xFFFFFF, (level - 128) * 2);
	return uk_mix (0, c & 0xFFFFFF, level * 2);
}
void uk_blend_px (Canvas &cv, int x, int y, unsigned c, int a)
{
	if (x < 0 || y < 0 || x >= cv.w || y >= cv.h || a <= 0) return;
	unsigned &d = cv.px[(size_t) y * cv.stride + x];
	d = a >= 255 ? c & 0xFFFFFF : uk_mix (d & 0xFFFFFF, c & 0xFFFFFF, a + (a >> 7));
}

// ---- shapes -----------------------------------------------------------------------------------------------------------------
// A rounded box's outline, its points in 1/16 px, the box [x, x + w) x [y, y + h) inset by `in` (1/16 px).
static void rr_points (std::vector<int> &xy, int x, int y, int w, int h, int r, int in, int corners)
{
	xy.clear ();
	int X0 = x * 16 + in, Y0 = y * 16 + in, X1 = (x + w) * 16 - in, Y1 = (y + h) * 16 - in, R = r * 16 - in;
	if (R < 0) R = 0;
	if (R * 2 > X1 - X0) R = (X1 - X0) / 2; if (R * 2 > Y1 - Y0) R = (Y1 - Y0) / 2;
	struct { int cx, cy, a0, bit; } c[4] = { { X1 - R, Y0 + R, 0, UK_TR }, { X0 + R, Y0 + R, 90, UK_TL }, { X0 + R, Y1 - R, 180, UK_BL }, { X1 - R, Y1 - R, 270, UK_BR } };
	const int sx[4] = { X1, X0, X0, X1 }, sy[4] = { Y0, Y0, Y1, Y1 };
	for (int k = 0; k < 4; k++)
	{
		if (!(corners & c[k].bit) || R < 8) { xy.push_back (sx[k]); xy.push_back (sy[k]); continue; }
		for (int s = 0; s <= 8; s++)
		{
			int d = c[k].a0 + s * 90 / 8;
			xy.push_back (c[k].cx + R * uk_cos (d) / 16384); xy.push_back (c[k].cy - R * uk_sin (d) / 16384);
		}
	}
}
void uk_rbox (Canvas &cv, int x, int y, int w, int h, int r, unsigned top, unsigned bottom, int alpha, int corners)
{
	if (w <= 0 || h <= 0) return;
	std::vector<int> xy; rr_points (xy, x, y, w, h, r, 0, corners);
	VPath p; p.poly (&xy[0], (int) xy.size () / 2);
	p.fill (cv, top == bottom ? top : uk_mix (top, bottom, 128), alpha);
}
void uk_rline (Canvas &cv, int x, int y, int w, int h, int r, unsigned c, int alpha, int corners)
{
	if (w <= 0 || h <= 0) return;
	std::vector<int> xy; rr_points (xy, x, y, w, h, r, 8, corners);
	VPath p; p.polyline (&xy[0], (int) xy.size () / 2, 16, true); p.fill (cv, c, alpha);
}
void uk_sunken (Canvas &cv, int x, int y, int w, int h, int r, unsigned bg, bool focus)
{
	uk_rbox (cv, x, y, w, h, r, bg, bg); uk_rline (cv, x, y, w, h, r, focus ? C_ACCENT : uk_tone (C_BG, 92));
}
void uk_etch_h (Canvas &cv, int x, int y, int w, unsigned face) { cv.fillRect (x, y, w, 1, uk_tone (face, 100)); cv.fillRect (x, y + 1, w, 1, uk_tone (face, 160)); }
void uk_etch_v (Canvas &cv, int x, int y, int h, unsigned face) { cv.fillRect (x, y, 1, h, uk_tone (face, 100)); cv.fillRect (x + 1, y, 1, h, uk_tone (face, 160)); }
void uk_glyph (Canvas &cv, int kind, int cx, int cy, int size, unsigned c)
{
	VPath p; int s = size * 16 / 2, w = size * 16 / 6 < 20 ? 20 : size * 16 / 6, X = cx * 16, Y = cy * 16;
	int xy[6];
	switch (kind)
	{
	case WKG_CHEV_DOWN: xy[0] = X - s; xy[1] = Y - s / 2; xy[2] = X; xy[3] = Y + s / 2; xy[4] = X + s; xy[5] = Y - s / 2; break;
	case WKG_CHEV_UP: xy[0] = X - s; xy[1] = Y + s / 2; xy[2] = X; xy[3] = Y - s / 2; xy[4] = X + s; xy[5] = Y + s / 2; break;
	case WKG_CHEV_LEFT: xy[0] = X + s / 2; xy[1] = Y - s; xy[2] = X - s / 2; xy[3] = Y; xy[4] = X + s / 2; xy[5] = Y + s; break;
	case WKG_CHEV_RIGHT: xy[0] = X - s / 2; xy[1] = Y - s; xy[2] = X + s / 2; xy[3] = Y; xy[4] = X - s / 2; xy[5] = Y + s; break;
	case WKG_CHECK: xy[0] = X - s; xy[1] = Y; xy[2] = X - s / 3; xy[3] = Y + s * 2 / 3; xy[4] = X + s; xy[5] = Y - s * 2 / 3; break;
	default: p.circle (X, Y, s / 2); p.fill (cv, c); return;
	}
	p.polyline (xy, 3, w); p.fill (cv, c);
}
void uk_tool_glyph (Canvas &, int, int, int, int, unsigned) {}		// (New, Open... are the Windows toolbar's own)

// ---- text ---------------------------------------------------------------------------------------------------------------------
TextFace *uk_face_ = 0;
static ULONG_PTR g_gdip;
static void gdip ()
{
	static bool on = false;
	if (on) return;
	Gdiplus::GdiplusStartupInput in; Gdiplus::GdiplusStartup (&g_gdip, &in, 0); on = true;
}
static Gdiplus::Font *font (int px, bool bold)
{
	static Gdiplus::Font *cache[64][2];
	if (px < 6) px = 6; if (px > 63) px = 63;
	Gdiplus::Font *&f = cache[px][bold ? 1 : 0];
	if (!f) { gdip (); f = new Gdiplus::Font (L"Segoe UI", (Gdiplus::REAL) px, bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular, Gdiplus::UnitPixel); }
	return f;
}
static int face_px () { return uk_face_ ? uk_face_->px : 13; }
static std::wstring wide (const char *s)
{
	int n = MultiByteToWideChar (CP_UTF8, 0, s, -1, 0, 0); std::wstring w (n > 0 ? n - 1 : 0, 0);
	if (n > 1) MultiByteToWideChar (CP_UTF8, 0, s, -1, &w[0], n);
	return w;
}
static Gdiplus::Graphics *measurer ()
{
	static Gdiplus::Bitmap *b; static Gdiplus::Graphics *g;
	if (!g) { gdip (); b = new Gdiplus::Bitmap (4, 4, PixelFormat32bppRGB); g = new Gdiplus::Graphics (b); g->SetTextRenderingHint (Gdiplus::TextRenderingHintAntiAlias); }
	return g;
}
static const Gdiplus::StringFormat *typo ()
{
	static Gdiplus::StringFormat *f;
	if (!f) { gdip (); f = Gdiplus::StringFormat::GenericTypographic ()->Clone (); f->SetFormatFlags (f->GetFormatFlags () | Gdiplus::StringFormatFlagsMeasureTrailingSpaces | Gdiplus::StringFormatFlagsNoWrap); }
	return f;
}
int TextFace::height () const { Gdiplus::Font *f = font (px, false); return (int) ceil (f->GetHeight (measurer ())); }
int uk_fh () { return TextFace (face_px ()).height (); }
int uk_tw (const char *s, int style)
{
	if (!s || !*s) return 0;
	std::wstring w = wide (s); Gdiplus::RectF r;
	measurer ()->MeasureString (w.c_str (), (INT) w.size (), font (face_px (), style == 2), Gdiplus::PointF (0, 0), typo (), &r);
	return (int) ceil (r.Width);
}
void uk_text (Canvas &cv, int x, int y, const char *s, unsigned c, int style)
{
	if (!s || !*s || !cv.px) return;
	static Gdiplus::Bitmap *bm; static Gdiplus::Graphics *g; static unsigned *at; static int aw, ah, as;
	if (!g || at != cv.px || aw != cv.w || ah != cv.h || as != cv.stride)
	{
		delete g; delete bm; gdip ();
		bm = new Gdiplus::Bitmap (cv.w, cv.h, cv.stride * 4, PixelFormat32bppRGB, (BYTE *) cv.px);
		g = new Gdiplus::Graphics (bm); g->SetTextRenderingHint (Gdiplus::TextRenderingHintAntiAliasGridFit);
		at = cv.px; aw = cv.w; ah = cv.h; as = cv.stride;
	}
	std::wstring w = wide (s);
	Gdiplus::SolidBrush br (Gdiplus::Color (255, (BYTE) (c >> 16), (BYTE) (c >> 8), (BYTE) c));
	g->DrawString (w.c_str (), (INT) w.size (), font (face_px (), style == 2), Gdiplus::PointF ((Gdiplus::REAL) x, (Gdiplus::REAL) y), typo (), &br);
}
int uk_text_fit (const char *s, int w, char *out, int cap, int style)
{
	snprintf (out, cap, "%s", s ? s : "");
	if (uk_tw (out, style) <= w) return (int) strlen (out);
	int n = (int) strlen (out);
	while (n > 0)
	{
		n--; while (n > 0 && ((unsigned char) out[n] & 0xC0) == 0x80) n--;		// (whole characters)
		char t[400]; snprintf (t, sizeof t, "%.*s\xE2\x80\xA6", n, out);
		if (uk_tw (t, style) <= w) { snprintf (out, cap, "%s", t); return n; }
	}
	out[0] = 0; return 0;
}

} // namespace uikit

// ---- AppKit ---------------------------------------------------------------------------------------------------------------
unsigned kapi_get_ticks () { return (unsigned) (GetTickCount64 () / 10); }

// ---- a picture: GDI+ reads it; 0x00RRGGBB, the see-through pixels magenta (UIKit's key) ----------------------------------------
namespace ui {
unsigned *icon_load (const char *path, int *pw, int *ph);
unsigned *icon_load (const char *path, int *pw, int *ph)
{
	*pw = *ph = 0; if (!path || !*path) return 0;
	uikit::gdip ();
	std::wstring w = uikit::wide (path);
	Gdiplus::Bitmap *b = Gdiplus::Bitmap::FromFile (w.c_str ());
	if (!b || b->GetLastStatus () != Gdiplus::Ok) { delete b; return 0; }
	int W = (int) b->GetWidth (), H = (int) b->GetHeight ();
	Gdiplus::Rect r (0, 0, W, H); Gdiplus::BitmapData d;
	if (W < 1 || H < 1 || b->LockBits (&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &d) != Gdiplus::Ok) { delete b; return 0; }
	unsigned *px = new unsigned[(size_t) W * H];
	for (int y = 0; y < H; y++)
	{
		const unsigned *s = (const unsigned *) ((const BYTE *) d.Scan0 + (size_t) y * d.Stride);
		for (int x = 0; x < W; x++) { unsigned v = s[x]; px[(size_t) y * W + x] = (v >> 24) < 128 ? 0xFF00FF : (v & 0xFFFFFF) == 0xFF00FF ? 0xFE00FE : v & 0xFFFFFF; }
	}
	b->UnlockBits (&d); delete b;
	*pw = W; *ph = H; return px;
}
}
