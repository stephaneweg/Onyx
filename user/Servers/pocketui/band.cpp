//
// band.cpp -- PocketUI's status band (pocket mode): a placeholder at the top of the screen until the pocket shell's
// status bar (pocketshell, phase P5 of docs/POCKETUI-TECH-STUDY.md) -- the Onyx gem, the name of the app in front,
// the time. A window of PocketUI's own (topmost, borderless, a system one: never the keys', never an app), standing
// on the screen's top edge: the window manager keeps its height out of the work area, as the desktop's menu bar.
// Its pixels are PocketUI's memory; its text is the kernel's bitmap font (kapi_draw_text_buf), the server staying
// free of FreeType. The look: the Milk menu bar's light gradient (docs/COMPACT-SHELL-STUDY.md section 12).
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
#include "core.h"
#include "pocketui.h"

static int s_nId = -1;				// the band's window, -1: none
static char s_Title[48];			// what it shows now
static int s_nMinute = -1;

int pk_band_id (void)	{ return s_nId; }

static unsigned Mix (unsigned a, unsigned b, int t, int n)	// a .. b, t / n of the way
{
	unsigned r = 0;
	for (int s = 0; s < 24; s += 8)
	{
		int ca = (int) ((a >> s) & 0xFF), cb = (int) ((b >> s) & 0xFF);
		r |= (unsigned) (ca + (cb - ca) * t / n) << s;
	}
	return r;
}

static void Draw (void)
{
	int w = 0, h = 0;
	unsigned *p = el_core_window_canvas (s_nId, &w, &h);
	if (p == 0) return;
	for (int y = 0; y < h; y++)				// the Milk menu bar: a light gradient, a line under it
	{
		unsigned c = y == h - 1 ? 0x00B4B8C0 : Mix (0x00FAFAFB, 0x00DDE0E5, y, h - 1);
		for (int x = 0; x < w; x++) p[y * w + x] = c;
	}
	// the Onyx gem: a small Aqua diamond
	int cx = 14, cy = h / 2;
	for (int y = -6; y <= 6; y++)
		for (int x = -6; x <= 6; x++)
		{
			int d = (x < 0 ? -x : x) + (y < 0 ? -y : y);
			if (d > 6) continue;
			unsigned c = d == 6 ? 0x002A64B0 : y < 0 ? Mix (0x0078B4F0, 0x003D86DA, y + 6, 6) : 0x003D86DA;
			p[(cy + y) * w + cx + x] = c;
		}
	int ty = (h - 16) / 2;
	if (s_Title[0] != 0)					// the app in front, in bold (twice, one pixel apart)
	{
		kapi_draw_text_buf (p, w, h, 30, ty, s_Title, 0x00202024);
		kapi_draw_text_buf (p, w, h, 31, ty, s_Title, 0x00202024);
	}
	int Y = 0, Mo = 0, D = 0, H = 0, Mi = 0, S = 0;
	kapi_get_datetime (&Y, &Mo, &D, &H, &Mi, &S);		// (the clock not set yet: the time since the boot)
	{
		char t[6] = { (char) ('0' + H / 10 % 10), (char) ('0' + H % 10), ':', (char) ('0' + Mi / 10 % 10), (char) ('0' + Mi % 10), 0 };
		kapi_draw_text_buf (p, w, h, w - 12 - 5 * 8, ty, t, 0x00202024);
	}
	el_core_window_present (s_nId);
}

int pk_band_make (int w)
{
	if (s_nId >= 0) { el_core_window_remove (s_nId); s_nId = -1; }
	if (w <= 0) return -1;
	el_core_owner (0);					// (its pixels: PocketUI's own memory)
	s_nId = el_core_window_add (0, 0, w, PK_BAND_H, "status", WIN_FLAG_TOPMOST | WIN_FLAG_BORDERLESS | WIN_FLAG_SYSTEM, g_nPkSelf);
	s_nMinute = -1;
	if (s_nId >= 0) Draw ();
	return s_nId;
}

void pk_band_update (const char *title)
{
	if (s_nId < 0) return;
	int Y = 0, Mo = 0, D = 0, H = 0, Mi = 0, S = 0;
	kapi_get_datetime (&Y, &Mo, &D, &H, &Mi, &S);
	int nMinute = H * 60 + Mi;
	unsigned i = 0;
	int bSame = 1;
	for (; i + 1 < sizeof s_Title && title[i] != 0; i++) if (s_Title[i] != title[i]) bSame = 0;
	if (s_Title[i] != 0) bSame = 0;
	if (bSame && nMinute == s_nMinute) return;
	for (i = 0; i + 1 < sizeof s_Title && title[i] != 0; i++) s_Title[i] = title[i];
	s_Title[i] = 0;
	s_nMinute = nMinute;
	Draw ();
}
