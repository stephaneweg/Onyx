//
// stickies/main.cpp -- Stickies: the notes pinned in Notes (user/Apps/notes), shown on the desktop as paper
// cards at its top right (AutoDev round 1: autodev/rounds/01-notes/, the design in 04-ux-design.md section 8).
// The agenda's kind of window (user/Apps/agenda): borderless, back-most -- every window covers it --, part of
// the wallpaper (see-through, WIN_FLAG_ALPHA; its header's ink chosen from the wallpaper under it). It only
// reads: SD:/Notes (the notes, notes.ini: which ones are pinned, their colours) through Notes' model
// (Apps/notes/notesmodel.cpp), every ~3 s and at once when Notes says so (the "stickies" service:
// STK_MSG_RELOAD; STK_MSG_QUIT ends it -- View > Hide Stickies in Notes; stickies_proto.h).
//   * Click a card: Notes opens on that note. Click the empty place or "+N more": Notes opens.
//   * Drag the header: the widget moves; its place is kept in SD:/apps/stickies.app/config.ini (x, y).
// At most 6 cards, the most recently changed pinned notes, each its title and up to 6 word-wrapped lines.
// It does not start when Notes' config.ini says stickies = 0 (and quits when it comes to say so).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include "systemkit/systemkit.h"
#include "fontkit/uikitface.h"
#include "Apps/notes/notesmodel.h"
#include "Apps/notes/stickies_proto.h"
#include <stdio.h>
#include <string.h>

using namespace uikit;

// The geometry, in px (04-ux-design.md section 8.1).
enum { W = 240, HDR = 34, CX = 12, CW = 216, CPAD = 10, CBAND = 5, CGAP = 12, CR = 6, TOP = 44,
       LINES = 6, LINE_H = 17, TITLE_H = 18, CARDS = 6, EMPTY_H = 62, MORE_H = 22 };
enum { POLL = 300 };					// ticks (1/100 s): the poll of SD:/Notes
static const unsigned CATCH = 0xFE000000u;		// almost see-through: the clicks still land (the agenda's)
static const unsigned CLEAR = 0xFF000000u;		// wholly see-through: the clicks fall to the desktop

// ---- the notes shown ---------------------------------------------------------------------------------------
static Notes g_notes;					// (~140 KB: static)
static int   g_idx[CARDS], g_n;			// the cards: their notes' rows in g_notes, newest first
static int   g_pinned;				// how many notes are pinned (> g_n: "+N more in Notes")
static char  g_head[CARDS][NOTE_HEAD + 1];		// each card's note: its first bytes
static int   g_headN[CARDS];
static unsigned g_sig;					// what the poll compares (the folder, notes.ini, the heads)

static unsigned fnv (unsigned h, const void *p, int n)
{
	const unsigned char *b = (const unsigned char *) p;
	for (int i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
	return h;
}
// A note's first NOTE_HEAD bytes (BOM / UTF-16 fixed, '\r' made '\n'). -> its length, 0 none
static int read_head (const char *file, char *out)
{
	char p[FS_PATHL]; snprintf (p, sizeof p, "%s/%s", NOTES_DIR, file);
	void *f = kapi_open (p);
	int n = 0;
	if (f) { n = kapi_read (f, out, NOTE_HEAD); kapi_close (f); }
	if (n < 0) n = 0;
	n = fs_text_fix (out, n);
	int o = 0;
	for (int i = 0; i < n; i++)
	{
		if (out[i] == '\r') { out[o++] = '\n'; if (i + 1 < n && out[i + 1] == '\n') i++; }
		else out[o++] = out[i];
	}
	out[o] = 0;
	return o;
}
// The cheap signature of what is shown: the folder's names and sizes, notes.ini's bytes, the shown notes'
// first bytes (a note changed in place, the same size).
static unsigned signature ()
{
	unsigned h = 2166136261u;
	void *d = kapi_opendir (NOTES_DIR);
	if (d)
	{
		struct kapi_dirent de;
		while (kapi_readdir (d, &de) == 1) { h = fnv (h, de.name, (int) strlen (de.name)); h = fnv (h, &de.size, sizeof de.size); }
		kapi_closedir (d);
	}
	static char ini[16384];
	void *f = kapi_open (NOTES_INI);
	if (f) { int n = kapi_read (f, ini, sizeof ini); kapi_close (f); if (n > 0) h = fnv (h, ini, n); }
	static char head[NOTE_HEAD + 1];
	for (int k = 0; k < g_n; k++) { int n = read_head (g_notes.n[g_idx[k]].file, head); h = fnv (h, head, n); }
	return h ? h : 1;
}
// SD:/Notes read again: the pinned notes, their texts. -> true something changed
static bool reload (bool force)
{
	unsigned s = signature ();
	if (!force && s == g_sig) return false;
	notes_scan (g_notes);
	g_n = notes_pinned (g_notes, g_idx, CARDS);
	g_pinned = 0;
	for (int i = 0; i < g_notes.count; i++) if (g_notes.n[i].pinned && g_notes.n[i].saved) g_pinned++;
	for (int k = 0; k < g_n; k++) g_headN[k] = read_head (g_notes.n[g_idx[k]].file, g_head[k]);
	g_sig = signature ();
	return true;
}

// A card's body: the text after its title's line (the blank lines before them skipped).
static const char *body_of (int k, int *len)
{
	const char *s = g_head[k], *e = s + g_headN[k];
	bool title = false;
	while (s < e)
	{
		const char *l = s; while (l < e && *l != '\n') l++;
		bool blank = true; for (const char *c = s; c < l; c++) if (*c != ' ' && *c != '\t') blank = false;
		if (!blank && title) break;
		if (!blank) title = true;
		s = l < e ? l + 1 : e;
	}
	*len = (int) (e - s);
	return s;
}
// Card k's body lines (word-wrapped to the card: at least 1, at most 6) and its height.
static int card_lines (int k, int *st, int *ln, bool *more)
{
	int n; const char *b = body_of (k, &n);
	int lines = uk_text_wrap (b, n, CW - 2 * CPAD, LINES, st, ln, more);
	return lines;
}
static int card_h (int lines) { return CBAND + CPAD + TITLE_H + (lines < 1 ? 1 : lines) * LINE_H + CPAD; }

// ---- the ink, from the wallpaper under the widget (the agenda's rule) --------------------------------------
static bool     g_light = false;
static unsigned g_back = 0x00304058, g_ink = 0x00FAFCFF, g_dim = 0x00B8C4D0;
static bool read_back (int wx, int wy)
{
	int ww = 0, wh = 0;
	unsigned *wall = uk_win_wallpaper_buffer (&ww, &wh);
	unsigned r = 0, g = 0, b = 0, n = 0;
	for (int y = wy; wall && y < wy + HDR && y < wh; y += 4)
		for (int x = wx; x < wx + W && x < ww; x += 4)
		{
			if (x < 0 || y < 0) continue;
			unsigned c = wall[(long) y * ww + x];
			r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++;
		}
	unsigned back = n && (r | g | b) ? ((r / n) << 16) | ((g / n) << 8) | (b / n) : 0x00304058;
	if (back == g_back) return false;
	g_back = back;
	g_light = uk_bright (back) > 128;
	g_ink = g_light ? 0x00182232 : 0x00FAFCFF;
	g_dim = g_light ? uk_mix (g_ink, back, 97) : 0x00B8C4D0;
	return true;
}

// ---- Notes, opened ---------------------------------------------------------------------------------------
// On a note (`file` a bare name) or, file 0, as it was: the running Notes is told (NOTES_MSG_OPEN) and raised,
// else Notes is started (with the note's path).
static void open_notes (const char *file)
{
	char p[FS_PATHL] = "";
	if (file) snprintf (p, sizeof p, "%s/%s", NOTES_DIR, file);
	int pid = kapi_ipc_lookup (NOTES_SERVICE);
	if (pid > 0)
	{
		kapi_mailbox_send (pid, NOTES_MSG_OPEN, p, (unsigned) strlen (p) + 1);
		uk_win_app_raise (NOTES_SERVICE);
	}
	else lx_launch (NOTES_SERVICE, p[0] ? p : 0);
}

// ---- the widget ------------------------------------------------------------------------------------------
static void fill (Canvas &cv, int x, int y, int w, int h, unsigned c) { uk_rbox (cv, x, y, w, h, 0, c, c); }

// The header's icon: a small yellow note, its corner turned.
static void sticky_icon (Canvas &cv, int x, int y)
{
	unsigned paper = notes_colour_paper (NC_YELLOW);
	uk_rbox (cv, x, y, 16, 16, 2, uk_tone (paper, 140), paper);
	uk_rline (cv, x, y, 16, 16, 2, 0x00000000, 80);
	for (int k = 0; k < 5; k++) fill (cv, x + 11 + k, y + 15 - k, 1, k + 1, uk_tone (notes_colour_dot (NC_YELLOW), 100));
	fill (cv, x + 3, y + 5, 8, 1, 0x00A08850); fill (cv, x + 3, y + 8, 6, 1, 0x00A08850);
}

class StickiesRoot : public Root
{
public:
	int  hot = -1;					// the card under the pointer; MORE the empty place / "+N more"
	int  cardY[CARDS], cardH[CARDS], shown = 0;	// the cards drawn: their places (the clicks)
	int  moreY = -1;				// the "+N more" line, -1 none
	bool moving = false; int grabX = 0, grabY = 0, winX, winY;
	unsigned lastPoll = 0;
	enum { MORE = 100 };

	StickiesRoot (int x, int y, int h) : Root (x, y, W, h, "stickies",
		WIN_FLAG_BORDERLESS | WIN_FLAG_BACKMOST | WIN_FLAG_SYSTEM | WIN_FLAG_ALPHA), winX (x), winY (y) {}

	void card (int k, int y, int h, bool hov)
	{
		const NoteInfo &e = g_notes.n[g_idx[k]];
		unsigned p = notes_colour_paper (e.colour), dot = notes_colour_dot (e.colour);
		uk_rbox (canvas, CX + 1, y + 3, CW, h, CR + 1, 0, 0, hov ? 70 : 45);		// the shadow, soft
		uk_rbox (canvas, CX, y + 1, CW, h + 1, CR, 0, 0, 40);
		uk_rbox (canvas, CX, y, CW, h, CR, uk_tone (p, 140), p);			// the paper
		uk_rbox (canvas, CX, y, CW, CBAND, CR, uk_tone (dot, 150), uk_tone (dot, 140), 255, UK_TL | UK_TR);
		uk_rline (canvas, CX, y, CW, h, CR, hov ? 0x00FFFFFF : uk_tone (p, 80), hov ? 230 : 150);
		if (hov) uk_rline (canvas, CX + 1, y + 1, CW - 2, h - 2, CR - 1, 0x00FFFFFF, 140);
		char t[NOTE_TITLE + 8];
		int ty = y + CBAND + CPAD - 2;
		uk_text_fit (e.title[0] ? e.title : TR ("New Note"), CW - 2 * CPAD - (hov ? 18 : 0), t, sizeof t, 2);
		uk_text (canvas, CX + CPAD, ty, t, NOTE_INK, 2);
		if (hov) uk_glyph (canvas, WKG_CHEV_RIGHT, CX + CW - CPAD - 4, ty + uk_fh () / 2, 9, uk_mix (p, NOTE_INK, 150));
		int st[LINES], ln[LINES]; bool more = false;
		int n; const char *b = body_of (k, &n);
		int lines = uk_text_wrap (b, n, CW - 2 * CPAD, LINES, st, ln, &more);
		unsigned ink = uk_mix (p, NOTE_INK, 215);
		for (int i = 0; i < lines; i++)
		{
			char l[256]; int m = ln[i] < 240 ? ln[i] : 240;
			memcpy (l, b + st[i], (size_t) m); l[m] = 0;
			if (i == lines - 1 && more)				// the text goes on: "..." at the end
			{
				char c[260]; snprintf (c, sizeof c, "%s\xE2\x80\xA6", l);
				if (uk_tw (c) <= CW - 2 * CPAD) snprintf (l, sizeof l, "%s", c);
				else { uk_text_fit (c, CW - 2 * CPAD, l, sizeof l); }
			}
			uk_text (canvas, CX + CPAD, ty + TITLE_H + 2 + i * LINE_H, l, ink);
		}
	}

	void empty_place (bool hov)				// nothing pinned: a dashed place, a hint
	{
		int y = TOP, h = EMPTY_H;
		for (int x = CX + 6; x < CX + CW - 6; x += 8)
			for (int k = 0; k < 4; k++) { uk_blend_px (canvas, x + k, y, 0x00FFFFFF, 120); uk_blend_px (canvas, x + k, y + h - 1, 0x00FFFFFF, 120); }
		for (int yy = y + 6; yy < y + h - 6; yy += 8)
			for (int k = 0; k < 4; k++) { uk_blend_px (canvas, CX, yy + k, 0x00FFFFFF, 120); uk_blend_px (canvas, CX + CW - 1, yy + k, 0x00FFFFFF, 120); }
		uk_rbox (canvas, CX + 1, y + 1, CW - 2, h - 2, 6, 0x00FFFFFF, 0x00FFFFFF, hov ? 40 : 18);
		uk_tool_glyph (canvas, WKT_PIN, CX + 14, y + 12, 16, 0x00E8EEF4);
		uk_text_over (canvas, CX + 38, y + 10, TR ("No notes pinned"), 0x00FAFCFF, 2, 1, g_back);
		uk_text_over (canvas, CX + 38, y + 31, TR ("Click to open Notes"), 0x00B8C4D0, 0, 1, g_back);
	}

	void onDraw () override
	{
		// what catches the clicks: the header and the cards (below them the desktop's)
		int bottom = TOP;
		shown = 0; moreY = -1;
		int y = TOP;
		for (int k = 0; k < g_n; k++)
		{
			int st[LINES], ln[LINES]; bool more;
			int h = card_h (card_lines (k, st, ln, &more));
			if (y + h > height - MORE_H && k > 0) break;		// (no room: "+N more")
			cardY[k] = y; cardH[k] = h; shown++;
			y += h + CGAP;
		}
		if (shown) bottom = y - CGAP + 6;
		if (shown < g_pinned) { moreY = shown ? y - 4 : TOP; bottom = moreY + MORE_H; }
		if (!g_pinned) bottom = TOP + EMPTY_H + 6;
		if (bottom > height) bottom = height;
		canvas.clear (CLEAR);
		canvas.fillRect (0, 0, W, bottom, CATCH);
		uk_paint_alpha (true);
		sticky_icon (canvas, 14, 9);
		const char *a = TR ("Pinned notes");
		int fy = 5 + (26 - uk_fh ()) / 2, sh = g_light ? 2 : 1;
		uk_text_over (canvas, 38, fy, a, g_ink, 2, sh, g_back);
		if (g_pinned)
		{
			char c[16]; snprintf (c, sizeof c, "(%d)", g_pinned);
			uk_text_over (canvas, 38 + uk_tw (a, 2) + 6, fy, c, g_dim, 0, sh, g_back);
		}
		for (int i = 10; i < W - 10; i++)				// the etched line (the agenda's)
		{
			uk_blend_px (canvas, i, 33, g_light ? uk_tone (g_back, 90) : 0, g_light ? 200 : 110);
			uk_blend_px (canvas, i, 34, g_light ? uk_tone (g_back, 190) : 0x00FFFFFF, g_light ? 200 : 70);
		}
		if (!g_pinned) empty_place (hot == MORE);
		for (int k = 0; k < shown; k++) card (k, cardY[k], cardH[k], hot == k);
		if (moreY >= 0)
		{
			char m[96]; snprintf (m, sizeof m, TR ("+%d more in Notes"), g_pinned - shown);
			uk_text_over (canvas, CX + 4, moreY + (MORE_H - uk_fh ()) / 2, m, hot == MORE ? g_ink : g_dim, 0, sh, g_back);
		}
		uk_paint_alpha (false);
	}

	int hit (int mx, int my)
	{
		if (my < HDR || mx < CX || mx >= CX + CW) return -1;
		if (!g_pinned) return my >= TOP && my < TOP + EMPTY_H ? MORE : -1;
		for (int k = 0; k < shown; k++) if (my >= cardY[k] && my < cardY[k] + cardH[k]) return k;
		if (moreY >= 0 && my >= moreY && my < moreY + MORE_H) return MORE;
		return -1;
	}

	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0) { if (hot >= 0) { hot = -1; invalidate (true); } return false; }
		int sx = 0, sy = 0;
		uk_win_cursor_pos (&sx, &sy);				// screen coordinates (the window moves)
		if (moving)
		{
			uk_cursor (KAPI_CURSOR_MOVE);
			if (!bl)
			{
				moving = false;
				char cfg[64];
				int n = snprintf (cfg, sizeof cfg, "x = %d\ny = %d\n", winX, winY);
				kapi_save_file (STICKIES_CFG, cfg, (unsigned) n);
				if (read_back (winX, winY)) invalidate (true);	// (the wallpaper there)
			}
			else if (sx != grabX || sy != grabY)
			{
				winX += sx - grabX; winY += sy - grabY; grabX = sx; grabY = sy;
				uk_win_move (winX, winY);
			}
			return true;
		}
		if (my < HDR) uk_cursor (KAPI_CURSOR_MOVE);
		int h = hit (mx, my);
		if (h != hot) { hot = h; invalidate (true); }
		if (bl && !pressed)
		{
			pressed = true;
			if (my < HDR) { moving = true; grabX = sx; grabY = sy; }
			else if (h == MORE) open_notes (0);
			else if (h >= 0) open_notes (g_notes.n[g_idx[h]].file);
		}
		if (!bl) pressed = false;
		return true;
	}

	void onTick () override
	{
		// Notes' messages: re-read now, or quit (View > Hide Stickies from the Desktop)
		int from = 0, type = 0;
		char m[520];
		bool now = false;
		while (kapi_mailbox_recv (&from, &type, m, sizeof m, 0) >= 0)
		{
			if (type == STK_MSG_QUIT) kapi_exit (0);
			if (type == STK_MSG_RELOAD) now = true;
		}
		unsigned t = kapi_get_ticks ();
		if (!now && t - lastPoll < POLL) return;			// every ~3 s
		lastPoll = t;
		NotesCfg c; notes_cfg_load (c);
		if (!c.stickies) kapi_exit (0);					// (hidden while it ran)
		bool b = read_back (winX, winY);				// (a new wallpaper)
		if (reload (now) || b) { if (hot >= g_n && hot != MORE) hot = -1; invalidate (true); }
	}
};

int main (void)
{
	NotesCfg c;
	notes_cfg_load (c);
	if (!c.stickies) return 0;				// hidden (Notes' View menu): no window at all
	kapi_ipc_register (STICKIES_SERVICE);			// (failed: no reload message, the poll still runs)
	ft_uikit_install ("DejaVu Sans", 13);
	uk_lang_init ();					// the words in the system's language
	int sw = 1024, sh = 768;
	kapi_screen_size (&sw, &sh);
	int x = sw - W - 8, y = 40, h = sh - 40 - 120;		// top right, as tall as the room above the dock
	if (app_ini_load_path (STICKIES_CFG) >= 0) { x = app_ini_get_int (0, "x", x); y = app_ini_get_int (0, "y", y); }
	if (h < 200) h = 200;
	reload (true);
	read_back (x, y);
	StickiesRoot root (x, y, h);
	if (root.canvas.px == 0) return 1;
	root.run ();
	return 0;
}
