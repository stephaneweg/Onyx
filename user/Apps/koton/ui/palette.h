//
// ui/palette.h -- Koton's look: a dark studio inside Onyx's frame (docs/daw/mockups). The wtk widgets
// get it through the theme (wk_theme_set: the window, buttons, fields, accent -- the text colours
// follow by brightness); the hand-drawn views (the lanes, the grids, the rings) use the colours
// below and the small drawing helpers here (fills with opacity, rounded boxes, text).
//
#ifndef _koton_palette_h
#define _koton_palette_h

#include "wtk/wtk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

namespace kui {

using namespace wtk;

// ---- the colours (0x00RRGGBB) ---------------------------------------------------------------------------------
static const unsigned
	BG = 0x181B21,
	PANEL = 0x21252D,
	PANEL2 = 0x2A2F39,
	FACE = 0x363C48,
	LINE = 0x3A404C,
	LANE = 0x1C1F26,
	LANE2 = 0x1F232A,
	TEXT = 0xDADFE7,
	DIM = 0x8C96A5,
	FAINT = 0x5C6472,
	ACC = 0x49B0C4,
	ACC2 = 0x348092,
	PLAY = 0xFFC646,
	REC = 0xE2504C,
	FIELD = 0x14171C,
	GRID_BEAT = 0x2E343E,
	GRID_BAR = 0x46505E,
	GRID_SUB = 0x252A32,
	NOTE = 0x3CD6E2,
	NOTE_SEL = 0xFFD66E,
	PAD_ON = 0x2FC7D4,		// a note in the piano roll (Koton Studio's teal)
	GREEN = 0x56CE96,
	BLUE_NOTE = 0x78BEEC,
	FUNC_T = 0x4888E2,
	FUNC_S = 0x46AA84,
	FUNC_D = 0xE28844,
	FUNC_O = 0x8A7AC0;

// the track colours, by track index (a stable, pleasant cycle)
static const unsigned s_trackCols[] = { 0xE2A454, 0xCE627C, 0x76BA70, 0xDE7052, 0xA884DE, 0x5CB8C8, 0xD8C25A, 0x8C9CE0 };
static inline unsigned trackColour (int i) { return s_trackCols[(i < 0 ? 0 : i) % 8]; }
static inline unsigned funcColour (int f) { return f == 0 ? FUNC_T : f == 1 ? FUNC_S : f == 2 ? FUNC_D : FUNC_O; }

static inline unsigned mixc (unsigned a, unsigned b, int t256) { return wk_mix (a, b, t256); }
static inline unsigned lighter (unsigned c, int t256) { return wk_mix (c, 0xFFFFFF, t256); }
static inline unsigned darker (unsigned c, int t256) { return wk_mix (c, 0x000000, t256); }

// The studio as a wtk theme: every widget of the app draws dark.
static inline void applyTheme ()
{
	WkTheme t;
	wk_theme_get (t);
	t.window = PANEL; t.button = FACE; t.field = FIELD; t.accent = ACC; t.menubar = WK_AUTO;
	wk_theme_set (t);
}

// ---- drawing ---------------------------------------------------------------------------------------------------------
// a rectangle of c at opacity a (0..255) over what the canvas holds, clipped
static inline void blendRect (Canvas &cv, int x, int y, int w, int h, unsigned c, int a)
{
	if (a >= 255) { cv.fillRect (x, y, w, h, c); return; }
	if (a <= 0) return;
	int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + w > cv.w ? cv.w : x + w, y1 = y + h > cv.h ? cv.h : y + h;
	unsigned cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255;
	for (int yy = y0; yy < y1; yy++)
	{
		unsigned *p = cv.px + (long) yy * cv.stride;
		for (int xx = x0; xx < x1; xx++)
		{
			unsigned d = p[xx];
			unsigned r = ((d >> 16) & 255) * (255 - a) + cr * a, g = ((d >> 8) & 255) * (255 - a) + cg * a, b = (d & 255) * (255 - a) + cb * a;
			p[xx] = ((r / 255) << 16) | ((g / 255) << 8) | (b / 255);
		}
	}
}
static inline void hline (Canvas &cv, int x0, int x1, int y, unsigned c) { if (x1 > x0) cv.fillRect (x0, y, x1 - x0, 1, c); }
static inline void vline (Canvas &cv, int x, int y0, int y1, unsigned c) { if (y1 > y0) cv.fillRect (x, y0, 1, y1 - y0, c); }
static inline void box (Canvas &cv, int x, int y, int w, int h, int r, unsigned c) { if (w > 0 && h > 0) wk_rbox (cv, x, y, w, h, r, c, c); }
static inline void gbox (Canvas &cv, int x, int y, int w, int h, int r, unsigned top, unsigned bottom) { if (w > 0 && h > 0) wk_rbox (cv, x, y, w, h, r, top, bottom); }
static inline void frame (Canvas &cv, int x, int y, int w, int h, int r, unsigned c, int a = 255) { if (w > 0 && h > 0) wk_rline (cv, x, y, w, h, r, c, a); }

// text: its height, width; left / centred / right in a box of height h
static inline int th () { return wk_fh (); }
static inline int tw (const char *s, int style = 0) { return wk_text_w (s, style); }
static inline void textL (Canvas &cv, int x, int y, int h, const char *s, unsigned c, int style = 0) { wk_text_l (cv, x, y, h, s, c, style); }
static inline void textC (Canvas &cv, int x, int y, int w, int h, const char *s, unsigned c, int style = 0) { wk_text_c (cv, x, y, w, h, s, c, style); }
static inline void textR (Canvas &cv, int xr, int y, int h, const char *s, unsigned c, int style = 0) { wk_text_l (cv, xr - wk_text_w (s, style), y, h, s, c, style); }
// a string cut to fit w px ("..." when cut)
static inline void textFit (Canvas &cv, int x, int y, int w, int h, const char *s, unsigned c, int style = 0)
{
	if (w <= 4) return;
	if (wk_text_w (s, style) <= w) { textL (cv, x, y, h, s, c, style); return; }
	char b[160]; int n = 0;
	while (s[n] && n < 150) { b[n] = s[n]; n++; }
	b[n] = 0;
	while (n > 0)
	{
		b[--n] = 0;
		while (n > 0 && ((unsigned char) b[n - 1] & 0xC0) == 0x80) b[--n] = 0;	// a whole UTF-8 character
		char t[164]; snprintf (t, sizeof t, "%s...", b);
		if (wk_text_w (t, style) <= w || n == 0) { textL (cv, x, y, h, t, c, style); return; }
	}
}

// text wrapped at word boundaries in w px, lines lineH apart -> the height used
static inline int wrapText (Canvas &cv, int x, int y, int w, const char *s, unsigned c, int lineH, int style = 0)
{
	int yy = y;
	while (*s)
	{
		char line[400]; int n = 0, lastSpace = -1;
		const char *p = s;
		while (*p && *p != '\n')
		{
			if (n >= 398) { if (lastSpace > 0) { n = lastSpace; p = s + lastSpace + 1; } break; }
			line[n] = *p; line[n + 1] = 0;
			if (*p == ' ') lastSpace = n;
			if (wk_text_w (line, style) > w && n > 0)
			{
				if (lastSpace > 0) { n = lastSpace; p = s + lastSpace + 1; }
				break;
			}
			n++; p++;
		}
		line[n] = 0;
		textL (cv, x, yy, lineH, line, c, style);
		yy += lineH;
		if (*p == '\n') p++;
		if (p == s) p++;			// (a word longer than the line)
		s = p;
	}
	return yy - y;
}

// a small filled triangle (a play mark, a disclosure arrow): dir 0 right, 1 down, 2 left, 3 up
static inline void tri (Canvas &cv, int cx, int cy, int s, int dir, unsigned c)
{
	for (int i = 0; i <= s; i++)
	{
		int len = s - i;
		switch (dir)
		{
		case 0: vline (cv, cx - s / 2 + i, cy - len, cy + len + 1, c); break;
		case 1: hline (cv, cx - len, cx + len + 1, cy - s / 2 + i, c); break;
		case 2: vline (cv, cx + s / 2 - i, cy - len, cy + len + 1, c); break;
		default: hline (cv, cx - len, cx + len + 1, cy + s / 2 - i, c); break;
		}
	}
}

// an anti-aliased filled circle and a ring (the polyrhythm wheels, the knobs)
static inline void disc (Canvas &cv, int cx, int cy, int r, unsigned c, int a = 255)
{
	VPath p; p.circle (V (cx), V (cy), V (r)); p.fill (cv, c, a);
}
static inline void ringArc (Canvas &cv, int cx, int cy, int r, int a0, int a1, int w, unsigned c, int a = 255)
{
	VPath p; p.arc (V (cx), V (cy), V (r), a0, a1, V (w)); p.fill (cv, c, a);
}
static inline void aline (Canvas &cv, int x0, int y0, int x1, int y1, int w, unsigned c, int a = 255)
{
	VPath p; p.line (V (x0), V (y0), V (x1), V (y1), V (w < 1 ? 1 : w)); p.fill (cv, c, a);
}

// ---- a drag helper: the mouse's buttons as edges (wtk gives their state) ------------------------------
struct Buttons
{
	int l, r;
	Buttons () : l (0), r (0) {}
	// -> 1 pressed now, -1 released now, 0 no change (left); rightDown: the right one pressed now
	int edge (int bl, int br, bool *rightDown = 0)
	{
		int e = (bl && !l) ? 1 : (!bl && l) ? -1 : 0;
		if (rightDown) *rightDown = br && !r;
		l = bl; r = br;
		return e;
	}
};

} // namespace kui

#endif
