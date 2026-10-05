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
#include "corepriv.h"
#include <kern/layout.h>
#include <circle/util.h>
#include "core.h"

CWindowManager *g_pElWM = 0;
CWindow *g_pElWin[EL_WINDOWS_MAX];
static unsigned s_nLastGen = 0;
static boolean s_bFirst = TRUE;

static unsigned s_nOwner = 0;			// (el_core_owner)

// A window's pixels (kern/gui/window.h, WIN_PIXELS_HOOK): a program's are shared with it. The
// window asks for a spare page to align its start; shared memory is aligned already.
void *WinPixelsAlloc (int nPart, unsigned nBytes)
{
	if (s_nOwner != 0) return el_shared_alloc (s_nOwner, nPart, nBytes > KPAGE_SIZE ? nBytes - KPAGE_SIZE : nBytes);
	return new u8[nBytes];
}

void WinPixelsFree (void *pRaw)
{
	if (el_shared_is (pRaw)) el_shared_free (pRaw);
	else delete [] (u8 *) pRaw;
}

void el_core_owner (unsigned pid)
{
	s_nOwner = pid;
}

// (peek, then drop: the kernel's queue of the program may be full -- the event then waits here)
static GUIEvent s_Peek[EL_WINDOWS_MAX];
static boolean s_bPeek[EL_WINDOWS_MAX];

static CWindow *Win (int id)
{
	return id >= 0 && id < EL_WINDOWS_MAX ? g_pElWin[id] : 0;
}

// The pointer's shapes (kapi_set_cursor): the kernel's art (kernel/gui/cursors.inc), built as
// kernel.cpp does; the arrow is the window manager's own drawn one.
#include "../../../kernel/gui/cursors.inc"

// The arrow: white with a black edge (the kernel's own is black with a white edge), its tip the hot spot.
#define ARROW_W	13
#define ARROW_H	19
static const char s_Arrow[] =
	"B............"
	"BB..........."
	"BWB.........."
	"BWWB........."
	"BWWWB........"
	"BWWWWB......."
	"BWWWWWB......"
	"BWWWWWWB....."
	"BWWWWWWWB...."
	"BWWWWWWWWB..."
	"BWWWWWWWWWB.."
	"BWWWWWWWWWWB."
	"BWWWWWWBBBBBB"
	"BWWWBWWB....."
	"BWWB.BWWB...."
	"BWB..BWWB...."
	"BB....BWWB..."
	"B.....BWWB..."
	".......BB....";

static GImage *Art (const char *pRows, int nW, int nH)
{
	GImage *pImg = new GImage;
	pImg->SetSize (nW, nH);
	if (!pImg->IsValid ()) { delete pImg; return 0; }
	for (int y = 0; y < nH; y++)
		for (int x = 0; x < nW; x++)
		{
			char c = pRows[y * nW + x];
			pImg->SetPixel (x, y, c == 'W' ? 0x00FFFFFF : c == 'B' ? 0x00000000 : GIMAGE_TRANSPARENT);
		}
	return pImg;
}

static void CursorShapes (CWindowManager *pWM)
{
	GImage *pArrow = Art (s_Arrow, ARROW_W, ARROW_H);
	if (pArrow != 0) pWM->SetCursor (pArrow);
	for (unsigned n = 1; n < sizeof s_CursorArt / sizeof s_CursorArt[0] && n < KAPI_CURSOR_COUNT; n++)
	{
		const TCursorArt &A = s_CursorArt[n];
		GImage *pImg = new GImage;
		pImg->SetSize (A.nW, A.nH);
		if (!pImg->IsValid ()) { delete pImg; return; }
		for (int y = 0; y < A.nH; y++)
			for (int x = 0; x < A.nW; x++)
			{
				char c = A.pRows[y * A.nW + x];
				pImg->SetPixel (x, y, c == 'W' ? 0x00FFFFFF : c == 'B' ? 0x00000000 : GIMAGE_TRANSPARENT);
			}
		pWM->SetCursorImage (n, pImg, A.nHotX, A.nHotY);
	}
}

int el_core_start (int w, int h)
{
	g_nScreenWidth = w; g_nScreenHeight = h;
	if (g_pElWM == 0) { g_pElWM = new CWindowManager; CursorShapes (g_pElWM); }
	s_nLastGen = g_nScreenGen - 1;				// (the first frame: always)
	s_bFirst = TRUE;
	return g_pElWM != 0;
}

void el_core_wallpaper (unsigned base, int points, unsigned seed)
{
	if (g_pElWM != 0) g_pElWM->GenerateWallpaper ((u32) base, points, seed != 0 ? seed : 1);
}

int el_core_window_add (int x, int y, int w, int h, const char *title, unsigned flags, unsigned owner_pid)
{
	if (g_pElWM == 0) return -1;
	for (int id = 0; id < EL_WINDOWS_MAX; id++)
	{
		if (g_pElWin[id] != 0) continue;
		CWindow *pWin = new CWindow (x, y, w, h, title != 0 ? title : "app", flags);
		if (pWin == 0 || !pWin->IsValid ()) { delete pWin; return -1; }
		pWin->SetOwnerPid (owner_pid);
		g_pElWin[id] = pWin;
		g_pElWM->Add (pWin);
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
	g_pElWM->Remove (pWin);
	g_pElWin[id] = 0;
	s_bPeek[id] = FALSE;
	delete pWin;						// (one thread: no composition is reading it)
}

int el_core_window_of (unsigned pid)
{
	for (int id = 0; id < EL_WINDOWS_MAX; id++)
		if (g_pElWin[id] != 0 && g_pElWin[id]->OwnerPid () == pid) return id;
	return -1;
}

unsigned el_core_window_pid (int id)
{
	CWindow *pWin = Win (id);
	return pWin != 0 ? pWin->OwnerPid () : 0;
}

int el_core_window_frame_info (int id, struct el_core_frame *out)
{
	CWindow *pWin = Win (id);
	if (pWin == 0 || out == 0) return 0;
	memset (out, 0, sizeof *out);
	pWin->Damage (); pWin->ChromeTouch ();			// (kapi_get_chrome: the frame is about to be drawn)
	out->content_w = pWin->ClientWidth (); out->content_h = pWin->ClientHeight ();
	if (pWin->HasChrome ())
	{
		out->frame_w = pWin->OuterW (); out->frame_h = pWin->OuterH ();
		out->inset_l = pWin->ChromeL (); out->inset_r = pWin->ChromeR ();
		out->inset_t = pWin->ChromeT (); out->inset_b = pWin->ChromeB ();
	}
	const char *pTitle = pWin->Title ();
	unsigned i = 0;
	for (; i + 1 < sizeof out->title && pTitle[i] != '\0'; i++) out->title[i] = pTitle[i];
	out->title[i] = '\0';
	return 1;
}

void el_core_window_handler (int id, int kind, unsigned long long fn)
{
	CWindow *pWin = Win (id);
	if (pWin == 0) return;
	if (kind == 0) pWin->SetKeyHandler (fn);
	else if (kind == 1) pWin->SetClickHandler (fn);
	else if (kind == 2) pWin->SetPointerHandler (fn);
}

void el_core_window_move (int id, int x, int y)
{
	CWindow *pWin = Win (id);
	if (pWin != 0) pWin->Move (x, y);
}

int el_core_window_event_peek (int id, struct el_core_event *out)
{
	CWindow *pWin = Win (id);
	if (pWin == 0 || out == 0) return 0;
	if (!s_bPeek[id])
	{
		if (!pWin->PopEvent (&s_Peek[id])) return 0;
		s_bPeek[id] = TRUE;
	}
	out->handler = s_Peek[id].ulHandler; out->sender = s_Peek[id].ulSender;
	out->value = s_Peek[id].lValue; out->event = s_Peek[id].nEvent; out->mods = s_Peek[id].nMods;
	return 1;
}

void el_core_window_event_drop (int id)
{
	if (id >= 0 && id < EL_WINDOWS_MAX) s_bPeek[id] = FALSE;
}

// A program took or gave back the full screen (the kernel shows its buffer: kapi_present_fb): its
// window at 0, 0 and all the input its own, as the kernel's window manager did; nothing composed
// meanwhile; the whole screen drawn when it ends.
static int s_nFsX = 0, s_nFsY = 0;

void el_core_fullscreen (unsigned pid, int on)
{
	if (g_pElWM == 0) return;
	CWindow *pFs = g_pElWM->FullscreenWindow ();
	if (on)
	{
		int id = el_core_window_of (pid);
		CWindow *pWin = Win (id);
		if (pWin == 0 || pWin == pFs) return;
		s_nFsX = pWin->X (); s_nFsY = pWin->Y ();
		pWin->Move (0, 0);
		g_pElWM->SetFullscreen (pWin);
	}
	else if (pFs != 0 && pFs->OwnerPid () == pid)
	{
		g_pElWM->SetFullscreen (0);
		pFs->Move (s_nFsX, s_nFsY);
		ScreenDirty ();
		s_bFirst = TRUE;
	}
}

void el_core_screen (int w, int h)
{
	g_nScreenWidth = w; g_nScreenHeight = h;
	if (g_pElWM != 0) g_pElWM->OnScreenResized (w, h);
	ScreenDirty ();
	s_bFirst = TRUE;
}

void el_core_redraw (void)
{
	s_bFirst = TRUE;
}

void el_core_key (const char *keys)
{
	if (g_pElWM != 0 && keys != 0) g_pElWM->OnKey (keys);
}

void el_core_modifiers (unsigned mods)
{
	if (g_pElWM != 0) g_pElWM->SetModifiers (mods);
}

void el_core_pointer (int x, int y, unsigned buttons, int wheel)
{
	if (g_pElWM == 0) return;
	g_pElWM->OnMouse (x, y, buttons);
	if (wheel != 0) g_pElWM->OnMouseWheel (x, y, wheel);
}

// (the kernel's compositor task, kernel/kernel.cpp: only what changed, by damaged rectangles)
int el_core_compose (unsigned *screen, int w, int h, int *rects)
{
	if (g_pElWM == 0 || screen == 0) return 0;
	if (g_pElWM->FullscreenWindow () != 0) { s_bFirst = TRUE; return 0; }	// (its program shows itself)
	unsigned nGen = g_nScreenGen;
	if (nGen == s_nLastGen && !s_bFirst) return 0;
	s_nLastGen = nGen;
	TScreenDamage Damage;
	ScreenTakeDamage (&Damage);
	GImage Screen ((u32 *) screen, w, h);
	if (Damage.bFull || s_bFirst)
	{
		s_bFirst = FALSE;
		g_pElWM->Composite (&Screen);
		return -1;
	}
	for (int i = 0; i < Damage.n; i++)
	{
		Screen.SetClip (Damage.x0[i], Damage.y0[i], Damage.x1[i], Damage.y1[i]);
		g_pElWM->Composite (&Screen, i == 0);
		if (rects != 0)
		{
			rects[i * 4] = Damage.x0[i]; rects[i * 4 + 1] = Damage.y0[i];
			rects[i * 4 + 2] = Damage.x1[i] - Damage.x0[i]; rects[i * 4 + 3] = Damage.y1[i] - Damage.y0[i];
		}
	}
	return Damage.n;
}
