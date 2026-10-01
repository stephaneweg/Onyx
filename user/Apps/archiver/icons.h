//
// archiver/icons.h -- the Archiver's icons, drawn from shapes at any size (wtk/vpaint.h, anti-aliased):
// a folder, a file with its kind's colour band, the archive (a crate with a zipper), and the
// toolbar's -- Open, New, Add, Extract, Extract All, Delete, Test, Properties. The mock-ups'
// (tools/screenshot/mockup_archiver.py).
//
#ifndef _archiver_icons_h
#define _archiver_icons_h

#include "wtk/wtk.h"

namespace ui {

using namespace wtk;

enum { IC_OPEN, IC_NEW, IC_ADD, IC_EXTRACT, IC_EXTRACT_ALL, IC_DELETE, IC_TEST, IC_INFO };

static const unsigned FOLDER = 0xE2B45C, CRATE = 0xBA8856, ZIPPER = 0xF5ECD2;
static const unsigned GREEN = 0x4EA05C, RED = 0xC84A40, BLUE = 0x4A80C8, AMBER = 0xE2A03A;

static inline unsigned lighter (unsigned c, int k) { return wk_mix (c, 0xFFFFFF, k); }	// k / 256 toward white
static inline unsigned darker (unsigned c, int k) { return wk_mix (c, 0x000000, k); }

// a rounded box in 1/16 px units made of pixel arguments
static inline void rbox (Canvas &cv, int x, int y, int w, int h, int r, unsigned c, int alpha = 255)
{
	VPath p; p.rrect (V (x), V (y), V (w), V (h), V (r)); p.fill (cv, c, alpha);
}
static inline void rboxf (Canvas &cv, int x16, int y16, int w16, int h16, int r16, unsigned c, int alpha = 255)
{
	VPath p; p.rrect (x16, y16, w16, h16, r16); p.fill (cv, c, alpha);
}

static inline void icon_folder (Canvas &cv, int x, int y, int s, unsigned c = FOLDER)
{
	int X = V (x), Y = V (y), S = V (s);
	rboxf (cv, X, Y + S * 12 / 100, S * 45 / 100, S * 25 / 100, S / 12, darker (c, 36));
	rboxf (cv, X, Y + S * 22 / 100, S, S * 68 / 100, S / 12, c);
	rboxf (cv, X, Y + S * 32 / 100, S, S * 58 / 100, S / 12, lighter (c, 56));
}

// The file kinds: the band's colour by the extension
static inline unsigned kind_colour (const char *name)
{
	static const struct { const char *e; unsigned c; } k[] = {
		{ "c", 0x4A80C8 }, { "cpp", 0x4A80C8 }, { "cc", 0x4A80C8 }, { "h", 0x7860C4 }, { "hpp", 0x7860C4 }, { "py", 0x3A76A8 },
		{ "js", 0xD8B030 }, { "json", 0xD8B030 }, { "html", 0xE06030 }, { "htm", 0xE06030 }, { "css", 0x3A70D0 },
		{ "png", 0x4EA05C }, { "jpg", 0x4EA05C }, { "jpeg", 0x4EA05C }, { "gif", 0x4EA05C }, { "bmp", 0x4EA05C },
		{ "webp", 0x4EA05C }, { "svg", 0x4EA05C }, { "mp3", 0xB050B0 }, { "wav", 0xB050B0 }, { "ogg", 0xB050B0 },
		{ "fms", 0xB050B0 }, { "mid", 0xB050B0 }, { "pdf", 0xC83C3C }, { "doc", 0x3060B0 }, { "docx", 0x3060B0 },
		{ "rtf", 0x3060B0 }, { "odt", 0x3060B0 }, { "xls", 0x30905A }, { "xlsx", 0x30905A }, { "csv", 0x30905A },
		{ "md", 0x60687A }, { "txt", 0x8C8C8C }, { "ini", 0x96785A }, { "cfg", 0x96785A }, { "mk", 0xBE783C },
		{ "zip", 0xBA8856 }, { "7z", 0xBA8856 }, { "rar", 0xBA8856 }, { "gz", 0xBA8856 }, { "tar", 0xBA8856 },
		{ "img", 0x3C3C3C }, { "bin", 0x3C3C3C }, { "elf", 0x3C3C3C }, { "exe", 0x3C3C3C }, { "bas", 0x2A8A8A },
		{ 0, 0 } };
	const char *sl = 0, *dot = 0;
	for (const char *p = name; *p; p++) { if (*p == '/') sl = p; if (*p == '.') dot = p; }
	if (!dot || (sl && dot < sl)) return 0x8C8C8C;
	char e[8]; int n = 0;
	for (dot++; *dot && n < 7; dot++) e[n++] = (char) (*dot >= 'A' && *dot <= 'Z' ? *dot + 32 : *dot);
	e[n] = 0;
	if (*dot) return 0x8C8C8C;
	for (int i = 0; k[i].e; i++) { const char *a = k[i].e; int j = 0; while (a[j] && a[j] == e[j]) j++; if (!a[j] && !e[j]) return k[i].c; }
	return 0x8C8C8C;
}

static inline void icon_file (Canvas &cv, int x, int y, int s, unsigned band)
{
	int S = V (s), W = S * 78 / 100, X = V (x) + (S - W) / 2, Y = V (y), F = S * 28 / 100;
	int pg[] = { X, Y, X + W - F, Y, X + W, Y + F, X + W, Y + S, X, Y + S };
	VPath out; out.poly (pg, 5); out.fill (cv, 0x8A7E76);
	int in[] = { X + 16, Y + 16, X + W - F - 6, Y + 16, X + W - 16, Y + F + 6, X + W - 16, Y + S - 16, X + 16, Y + S - 16 };
	VPath p; p.poly (in, 5); p.fill (cv, 0xFFFFFF);
	int fold[] = { X + W - F, Y, X + W - F, Y + F, X + W, Y + F };
	VPath f; f.poly (fold, 3); f.fill (cv, 0xE2DAD4);
	rboxf (cv, X - 8, Y + S * 55 / 100, W * 88 / 100, S * 30 / 100, 16, band);
}

// The archive: a crate with a zipper down its middle
static inline void icon_archive (Canvas &cv, int x, int y, int s, unsigned c = CRATE)
{
	int X = V (x), Y = V (y), S = V (s), R = S / 8;
	rboxf (cv, X, Y + S / 10, S, S * 85 / 100, R, darker (c, 90));
	rboxf (cv, X + 16, Y + S / 10 + 16, S - 32, S * 85 / 100 - 32, R, c);
	rboxf (cv, X + 16, Y + S / 10 + 16, S - 32, S * 22 / 100, R, lighter (c, 64));
	int zx = X + S / 2, zw = S * 16 / 100;
	for (int i = 0; i < 4; i++) rboxf (cv, zx - zw / 2, Y + S * (18 + i * 16) / 100, zw, S * 8 / 100, 4, ZIPPER);
	rboxf (cv, zx - S * 12 / 100, Y + S * 76 / 100, S * 24 / 100, S * 12 / 100, 8, ZIPPER);
}

static inline void arrow_down (Canvas &cv, int cx, int y0, int y1, int half, unsigned c)
{
	VPath p; p.rect (V (cx) - V (2), V (y0), V (5), V (y1 - y0 - half)); p.fill (cv, c);
	int t[] = { V (cx) - V (half), V (y1 - half - 1), V (cx) + V (half) + 16, V (y1 - half - 1), V (cx) + 8, V (y1) };
	VPath a; a.poly (t, 3); a.fill (cv, c);
}
static inline void plus_badge (Canvas &cv, int cx, int cy, int r, unsigned c)
{
	VPath p; p.circle (V (cx), V (cy), V (r)); p.fill (cv, c);
	VPath w; w.rect (V (cx - r + 3), V (cy) - 20, V (2 * r - 6), 40); w.rect (V (cx) - 20, V (cy - r + 3), 40, V (2 * r - 6)); w.fill (cv, 0xFFFFFF);
}

// The toolbar's icons in a 32 x 32 box at (x, y); off: greyed
static inline void tool_icon (Canvas &cv, int kind, int x, int y, bool off)
{
	unsigned grey = 0xA89C94;
	auto g = [&] (unsigned c) { return off ? grey : c; };
	switch (kind)
	{
	case IC_OPEN: icon_folder (cv, x + 2, y + 2, 28, g (FOLDER)); break;
	case IC_NEW:
		icon_archive (cv, x + 3, y + 3, 24, g (CRATE)); plus_badge (cv, x + 24, y + 24, 7, g (GREEN)); break;
	case IC_ADD:
		icon_file (cv, x + 2, y + 2, 26, g (0x8C8C8C)); plus_badge (cv, x + 23, y + 23, 8, g (GREEN)); break;
	case IC_EXTRACT:
	case IC_EXTRACT_ALL:
		icon_archive (cv, x + 2, y + 9, 20, g (CRATE));
		arrow_down (cv, x + 24, y + 3, y + 27, 7, g (BLUE));
		if (kind == IC_EXTRACT_ALL) arrow_down (cv, x + 15, y + 1, y + 16, 5, g (BLUE));
		break;
	case IC_DELETE:
	{
		unsigned c = g (RED);
		rbox (cv, x + 7, y + 9, 18, 21, 3, c); rbox (cv, x + 4, y + 5, 24, 3, 1, c); rbox (cv, x + 12, y + 2, 8, 3, 1, c);
		for (int i = 0; i < 3; i++) rbox (cv, x + 11 + i * 4, y + 13, 2, 13, 1, 0xFFFFFF);
		break;
	}
	case IC_TEST:
	{
		int sh[] = { V (x + 16), V (y + 2), V (x + 28), V (y + 7), V (x + 26), V (y + 23), V (x + 16), V (y + 30), V (x + 6), V (y + 23), V (x + 4), V (y + 7) };
		VPath p; p.poly (sh, 6); p.fill (cv, g (GREEN));
		int ck[] = { V (x + 10), V (y + 16), V (x + 14), V (y + 20), V (x + 22), V (y + 11) };
		VPath k; k.polyline (ck, 3, V (3)); k.fill (cv, 0xFFFFFF);
		break;
	}
	case IC_INFO:
	{
		VPath p; p.circle (V (x + 16), V (y + 16), V (13)); p.fill (cv, g (BLUE));
		rbox (cv, x + 14, y + 13, 4, 11, 1, 0xFFFFFF); rbox (cv, x + 14, y + 7, 4, 4, 2, 0xFFFFFF);
		break;
	}
	}
}

} // namespace ui

#endif
