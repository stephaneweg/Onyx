//
// core.cpp -- Elegant's window manager: the kernel's CWindowManager, in a user process (core.h).
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
#include <kern/gui/window.h>
#include "core.h"

static CWindowManager *s_pWM = 0;
static CWindow *s_Win[EL_WINDOWS_MAX];
static unsigned s_nLastGen = 0;
static boolean s_bFirst = TRUE;

static CWindow *Win (int id)
{
	return id >= 0 && id < EL_WINDOWS_MAX ? s_Win[id] : 0;
}

int el_core_start (int w, int h)
{
	g_nScreenWidth = w; g_nScreenHeight = h;
	if (s_pWM == 0) s_pWM = new CWindowManager;
	s_nLastGen = g_nScreenGen - 1;				// (the first frame: always)
	s_bFirst = TRUE;
	return s_pWM != 0;
}

void el_core_wallpaper (unsigned base, int points, unsigned seed)
{
	if (s_pWM != 0) s_pWM->GenerateWallpaper ((u32) base, points, seed != 0 ? seed : 1);
}

int el_core_window_add (int x, int y, int w, int h, const char *title, unsigned flags, unsigned owner_pid)
{
	if (s_pWM == 0) return -1;
	for (int id = 0; id < EL_WINDOWS_MAX; id++)
	{
		if (s_Win[id] != 0) continue;
		CWindow *pWin = new CWindow (x, y, w, h, title != 0 ? title : "app", flags);
		if (pWin == 0 || !pWin->IsValid ()) { delete pWin; return -1; }
		pWin->SetOwnerPid (owner_pid);
		s_Win[id] = pWin;
		s_pWM->Add (pWin);
		return id;
	}
	return -1;
}

unsigned *el_core_window_canvas (int id, int *w, int *h)
{
	CWindow *pWin = Win (id);
	if (pWin == 0) return 0;
	if (w != 0) *w = pWin->Canvas ()->Width ();
	if (h != 0) *h = pWin->Canvas ()->Height ();
	return pWin->CanvasBuffer ();
}

unsigned *el_core_window_frame (int id, int active, int *w, int *h)
{
	CWindow *pWin = Win (id);
	if (pWin == 0 || !pWin->HasChrome ()) return 0;
	if (w != 0) *w = pWin->OuterW ();
	if (h != 0) *h = pWin->OuterH ();
	pWin->Damage (); pWin->ChromeTouch ();			// (the caller is about to draw it: kapi_get_chrome)
	return (unsigned *) pWin->ChromePhys (active ? 0 : 1);	// (a user process: the address is its own)
}

void el_core_window_present (int id)
{
	CWindow *pWin = Win (id);
	if (pWin != 0) pWin->PresentDamage ();
}

int el_core_window_closing (int id)
{
	CWindow *pWin = Win (id);
	return pWin != 0 && pWin->ShouldExit ();
}

void el_core_window_remove (int id)
{
	CWindow *pWin = Win (id);
	if (pWin == 0) return;
	s_pWM->Remove (pWin);
	s_Win[id] = 0;
	delete pWin;						// (one thread: no composition is reading it)
}

void el_core_pointer (int x, int y, unsigned buttons, int wheel)
{
	if (s_pWM == 0) return;
	s_pWM->OnMouse (x, y, buttons);
	if (wheel != 0) s_pWM->OnMouseWheel (x, y, wheel);
}

// (the kernel's compositor task, kernel/kernel.cpp: only what changed, by damaged rectangles)
int el_core_compose (unsigned *screen, int w, int h, int *rects)
{
	if (s_pWM == 0 || screen == 0) return 0;
	unsigned nGen = g_nScreenGen;
	if (nGen == s_nLastGen && !s_bFirst) return 0;
	s_nLastGen = nGen;
	TScreenDamage Damage;
	ScreenTakeDamage (&Damage);
	GImage Screen ((u32 *) screen, w, h);
	if (Damage.bFull || s_bFirst)
	{
		s_bFirst = FALSE;
		s_pWM->Composite (&Screen);
		return -1;
	}
	for (int i = 0; i < Damage.n; i++)
	{
		Screen.SetClip (Damage.x0[i], Damage.y0[i], Damage.x1[i], Damage.y1[i]);
		s_pWM->Composite (&Screen, i == 0);
		if (rects != 0)
		{
			rects[i * 4] = Damage.x0[i]; rects[i * 4 + 1] = Damage.y0[i];
			rects[i * 4 + 2] = Damage.x1[i] - Damage.x0[i]; rects[i * 4 + 3] = Damage.y1[i] - Damage.y0[i];
		}
	}
	return Damage.n;
}
