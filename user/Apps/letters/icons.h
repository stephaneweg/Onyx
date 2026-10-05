//
// icons.h -- Letters' toolbar icons, drawn from their geometry (uikit/vpaint.h: anti-aliased, 20 x 20
// px) or from the fonts (the letters B, I, U, S, x², A...): a page, a folder, a floppy, the undo
// arrows, scissors, pages, a clipboard, a magnifier, the alignments' lines, the lists, the indents,
// a table.
// `ink` is the toolbar's text colour (the theme's); the colours of a folder, a floppy... are their own.
//
#ifndef _writer_icons_h
#define _writer_icons_h

#include "uikit/uikit.h"
#include "ft/fonts.h"

namespace wr {

using namespace uikit;

enum { IC_NEW, IC_OPEN, IC_SAVE, IC_UNDO, IC_REDO, IC_CUT, IC_COPY, IC_PASTE, IC_FIND, IC_BOLD,
       IC_ITALIC, IC_UNDER, IC_STRIKE, IC_SUPER, IC_SUB, IC_COLOR, IC_HILITE, IC_LEFT, IC_CENTER,
       IC_RIGHT, IC_JUSTIFY, IC_BULLETS, IC_NUMBERS, IC_OUTDENT, IC_INDENT, IC_PILCROW, IC_ZOOMIN,
       IC_ZOOMOUT, IC_SYMBOL, IC_PAGEBREAK, IC_IMAGE, IC_TABLE, IC_COUNT };

static int g_icFam = -1, g_icFamSerif = -1;	// the letters' fonts

// A letter from a font, centred on (cx, baseline).
static void icon_letter (Canvas &cv, int cx, int base, unsigned ch, int fam, int style, int size, unsigned c)
{
	fnt::Font *f = fnt::get (fam, style, size * 64);
	if (!f) return;
	int w = fnt::advance (f, ch);
	fnt::draw (cv, f, cx * 64 - w / 2, base, ch, c, 0, 0, cv.w, cv.h);
}

// The lines of a paragraph (the alignments, the lists): n lines from y0, dy apart, each [x0, x1).
static void icon_lines (VPath &p, const int *x0, const int *x1, int n, int y0, int dy, int x, int y)
{
	for (int i = 0; i < n; i++) p.rect (V (x + x0[i]), V (y + y0 + i * dy), V (x1[i] - x0[i]), V (2) - 8);
}

// Icon k in the 20 x 20 box at (x, y); off: greyed.
static void draw_icon (Canvas &cv, int k, int x, int y, unsigned ink, bool off = false)
{
	if (g_icFam < 0) { g_icFam = fnt::find ("DejaVu Sans"); g_icFamSerif = fnt::find ("DejaVu Serif"); if (g_icFamSerif < 0) g_icFamSerif = g_icFam; }
	unsigned dim = uk_mix (ink, C_BG, 150);
	if (off) ink = dim;
	int A = off ? 110 : 255;			// (the colours' opacity when greyed)
	VPath p;
	int X = V (x), Y = V (y);
	auto page = [&] (int px, int py, int w, int h, int fold) {	// a sheet with a folded corner
		int pts[10] = { X + V (px), Y + V (py), X + V (px + w - fold), Y + V (py), X + V (px + w), Y + V (py + fold),
				X + V (px + w), Y + V (py + h), X + V (px), Y + V (py + h) };
		p.clear (); p.poly (pts, 5); p.fill (cv, 0xFFFFFF, A);
		p.clear (); p.polyline (pts, 5, 22, true); p.fill (cv, ink);
		int f[6] = { X + V (px + w - fold), Y + V (py), X + V (px + w - fold), Y + V (py + fold), X + V (px + w), Y + V (py + fold) };
		p.clear (); p.polyline (f, 3, 20); p.fill (cv, ink);
	};
	switch (k)
	{
	case IC_NEW:
		page (4, 2, 12, 16, 4);
		break;
	case IC_OPEN:
	{
		int back[12] = { X + V (2), Y + V (5), X + V (7), Y + V (5), X + V (9), Y + V (7), X + V (16), Y + V (7), X + V (16), Y + V (16), X + V (2), Y + V (16) };
		p.poly (back, 6); p.fill (cv, 0xD9A23A, A);
		p.clear (); p.polyline (back, 6, 20, true); p.fill (cv, 0x8A5D12, A);
		int front[8] = { X + V (4), Y + V (10), X + V (19), Y + V (10), X + V (16), Y + V (16), X + V (2), Y + V (16) };
		p.clear (); p.poly (front, 4); p.fill (cv, 0xF2C45A, A);
		p.clear (); p.polyline (front, 4, 20, true); p.fill (cv, 0x8A5D12, A);
		break;
	}
	case IC_SAVE:
		p.rrect (X + V (3), Y + V (3), V (14), V (14), V (2)); p.fill (cv, 0x3569B5, A);
		p.clear (); p.rect (X + V (6), Y + V (3), V (8), V (5)); p.fill (cv, 0xDCE6F2, A);
		p.clear (); p.rect (X + V (11), Y + V (4), V (2), V (3)); p.fill (cv, 0x3569B5, A);
		p.clear (); p.rrect (X + V (5), Y + V (11), V (10), V (6), V (1)); p.fill (cv, 0xFFFFFF, A);
		p.clear (); p.rect (X + V (7), Y + V (13), V (6), 12); p.rect (X + V (7), Y + V (15), V (6), 12); p.fill (cv, 0x9AA8BC, A);
		break;
	case IC_UNDO: case IC_REDO:
	{
		bool r = k == IC_REDO;
		int cx = X + V (10), cy = Y + V (12);
		if (r) p.arc (cx, cy, V (6), 180, -60, 30); else p.arc (cx, cy, V (6), 0, 240, 30);
		p.fill (cv, ink);
		p.clear ();
		if (r) p.arrowHead (X + V (17), Y + V (13), -80, V (6), V (4));
		else p.arrowHead (X + V (3), Y + V (13), 260, V (6), V (4));
		p.fill (cv, ink);
		break;
	}
	case IC_CUT:
		p.line (X + V (6), Y + V (2), X + V (12), Y + V (13), 26);
		p.line (X + V (14), Y + V (2), X + V (8), Y + V (13), 26);
		p.fill (cv, uk_mix (ink, 0x808080, 90));
		p.clear (); p.circle (X + V (6), Y + V (15), V (3)); p.hole (X + V (6), Y + V (15), V (2) - 4);
		p.circle (X + V (14), Y + V (15), V (3)); p.hole (X + V (14), Y + V (15), V (2) - 4);
		p.fill (cv, 0xC0392B, A);
		break;
	case IC_COPY:
		page (2, 2, 10, 13, 3);
		page (8, 6, 10, 13, 3);
		break;
	case IC_PASTE:
		p.rrect (X + V (3), Y + V (3), V (14), V (16), V (2)); p.fill (cv, 0xB07A3E, A);
		p.clear (); p.rect (X + V (5), Y + V (6), V (10), V (11)); p.fill (cv, 0xFFFFFF, A);
		p.clear (); p.rrect (X + V (7), Y + V (1), V (6), V (4), V (1)); p.fill (cv, uk_mix (ink, 0x909090, 120), A);
		p.clear (); p.rect (X + V (7), Y + V (9), V (6), 14); p.rect (X + V (7), Y + V (12), V (6), 14); p.fill (cv, uk_mix (0xFFFFFF, ink, 120));
		break;
	case IC_FIND: case IC_ZOOMIN: case IC_ZOOMOUT:
		p.circle (X + V (8), Y + V (8), V (6)); p.hole (X + V (8), Y + V (8), V (5) - 8);
		p.line (X + V (12), Y + V (12), X + V (18), Y + V (18), 44);
		p.fill (cv, ink);
		p.clear (); p.circle (X + V (8), Y + V (8), V (5) - 8); p.fill (cv, 0xDDEBF7, A);
		if (k != IC_FIND)
		{
			p.clear (); p.rect (X + V (5), Y + V (7) + 6, V (6), 20);
			if (k == IC_ZOOMIN) p.rect (X + V (7) + 6, Y + V (5), 20, V (6));
			p.fill (cv, ink);
		}
		break;
	case IC_BOLD: icon_letter (cv, x + 10, y + 16, 'B', g_icFamSerif, fnt::BOLD, 16, ink); break;
	case IC_ITALIC: icon_letter (cv, x + 10, y + 16, 'I', g_icFamSerif, fnt::ITALIC, 16, ink); break;
	case IC_UNDER:
		icon_letter (cv, x + 10, y + 15, 'U', g_icFamSerif, 0, 15, ink);
		p.rect (X + V (5), Y + V (17), V (10), 24); p.fill (cv, ink);
		break;
	case IC_STRIKE:
		icon_letter (cv, x + 10, y + 16, 'S', g_icFamSerif, 0, 16, ink);
		p.rect (X + V (3), Y + V (10), V (14), 22); p.fill (cv, ink);
		break;
	case IC_SUPER: case IC_SUB:
		icon_letter (cv, x + 8, y + 15, 'x', g_icFamSerif, 0, 15, ink);
		icon_letter (cv, x + 16, k == IC_SUPER ? y + 9 : y + 18, '2', g_icFam, fnt::BOLD, 9, off ? dim : C_ACCENT);
		break;
	case IC_COLOR:
		icon_letter (cv, x + 10, y + 14, 'A', g_icFamSerif, fnt::BOLD, 14, ink);
		break;
	case IC_HILITE:
	{
		int body[8] = { X + V (9), Y + V (2), X + V (15), Y + V (8), X + V (9), Y + V (14), X + V (3), Y + V (8) };
		p.poly (body, 4); p.fill (cv, uk_mix (ink, 0xFFFFFF, 60), A);
		p.clear (); p.polyline (body, 4, 18, true); p.fill (cv, ink);
		int tip[6] = { X + V (3), Y + V (8), X + V (6), Y + V (11), X + V (2), Y + V (13) };
		p.clear (); p.poly (tip, 3); p.fill (cv, ink);
		break;
	}
	case IC_LEFT: case IC_CENTER: case IC_RIGHT: case IC_JUSTIFY:
	{
		int x0[5], x1[5]; const int wl[5] = { 16, 10, 16, 10, 16 }, wj[5] = { 16, 16, 16, 16, 16 };
		for (int i = 0; i < 5; i++)
		{
			int w = k == IC_JUSTIFY ? wj[i] : wl[i];
			if (k == IC_LEFT || k == IC_JUSTIFY) { x0[i] = 2; x1[i] = 2 + w; }
			else if (k == IC_RIGHT) { x0[i] = 18 - w; x1[i] = 18; }
			else { x0[i] = 10 - w / 2; x1[i] = 10 + w / 2; }
		}
		icon_lines (p, x0, x1, 5, 2, 4, x, y);
		p.fill (cv, ink);
		break;
	}
	case IC_BULLETS: case IC_NUMBERS:
	{
		const int x0[3] = { 8, 8, 8 }, x1[3] = { 18, 18, 18 };
		icon_lines (p, x0, x1, 3, 3, 6, x, y);
		p.fill (cv, ink);
		if (k == IC_BULLETS)
		{
			p.clear (); for (int i = 0; i < 3; i++) p.circle (X + V (4), Y + V (4 + i * 6), V (2) - 4);
			p.fill (cv, off ? dim : C_ACCENT);
		}
		else for (int i = 0; i < 3; i++) icon_letter (cv, x + 4, y + 7 + i * 6, (unsigned) ('1' + i), g_icFam, fnt::BOLD, 7, off ? dim : C_ACCENT);
		break;
	}
	case IC_OUTDENT: case IC_INDENT:
	{
		const int x0[4] = { 2, 9, 9, 2 }, x1[4] = { 18, 18, 18, 18 };
		icon_lines (p, x0, x1, 4, 2, 5, x, y);
		p.fill (cv, ink);
		p.clear ();
		if (k == IC_INDENT) p.arrowHead (X + V (7), Y + V (10), 0, V (5), V (3));
		else p.arrowHead (X + V (2), Y + V (10), 180, V (5), V (3));
		p.fill (cv, off ? dim : C_ACCENT);
		break;
	}
	case IC_PILCROW: icon_letter (cv, x + 10, y + 16, 0xB6, g_icFamSerif, fnt::BOLD, 16, ink); break;
	case IC_SYMBOL: icon_letter (cv, x + 10, y + 16, 0x3A9, g_icFamSerif, 0, 16, ink); break;
	case IC_IMAGE:
	{
		p.rrect (X + V (2), Y + V (3), V (16), V (14), V (2)); p.fill (cv, 0xFFFFFF, A);
		p.clear (); int fr[8] = { X + V (2), Y + V (3), X + V (18), Y + V (3), X + V (18), Y + V (17), X + V (2), Y + V (17) };
		p.polyline (fr, 4, 22, true); p.fill (cv, ink);
		p.clear (); p.circle (X + V (13), Y + V (7), V (2)); p.fill (cv, 0xF2B632, A);
		int hill[8] = { X + V (3), Y + V (16), X + V (8), Y + V (9), X + V (12), Y + V (13), X + V (3), Y + V (16) };
		p.clear (); p.poly (hill, 3); p.fill (cv, 0x4A9A55, A);
		int hill2[6] = { X + V (8), Y + V (16), X + V (13), Y + V (11), X + V (17), Y + V (16) };
		p.clear (); p.poly (hill2, 3); p.fill (cv, 0x2F7A3C, A);
		break;
	}
	case IC_TABLE:
	{
		p.rect (X + V (2), Y + V (3), V (16), V (4)); p.fill (cv, off ? dim : C_ACCENT, A);
		p.clear (); p.rect (X + V (2), Y + V (7), V (16), V (10)); p.fill (cv, 0xFFFFFF, A);
		int fr[8] = { X + V (2), Y + V (3), X + V (18), Y + V (3), X + V (18), Y + V (17), X + V (2), Y + V (17) };
		p.clear (); p.polyline (fr, 4, 20, true);
		p.rect (X + V (2), Y + V (7) - 10, V (16), 20); p.rect (X + V (2), Y + V (12) - 10, V (16), 20);
		p.rect (X + V (7) + 8, Y + V (3), 20, V (14)); p.rect (X + V (12) + 16, Y + V (3), 20, V (14));
		p.fill (cv, ink);
		break;
	}
	case IC_PAGEBREAK:
		page (4, 1, 12, 7, 0);
		page (4, 12, 12, 7, 0);
		p.clear (); for (int i = 0; i < 5; i++) p.rect (X + V (1 + i * 4), Y + V (10) - 4, V (2), 18);
		p.fill (cv, off ? dim : C_ACCENT);
		break;
	}
}

} // namespace wr

#endif
