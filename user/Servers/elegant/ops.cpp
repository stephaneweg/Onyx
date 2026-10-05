//
// ops.cpp -- the programs' requests answered (the protocol: appkit/elegant.h): each one does what
// the kernel's window call of the same name did (kernel/sys/kapi.cpp), on Elegant's window manager.
//
// With core.cpp, the only code that sees the window manager's classes (kern/gui/window.h); what
// needs the kernel goes through core.h's el_sys_* (server.cpp).
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
#include <circle/util.h>
#include "appkit/elegant.h"
#include "core.h"

static unsigned StrLen (const char *s)			{ unsigned n = 0; while (s[n] != '\0') n++; return n; }
static boolean StrEq (const char *a, const char *b)	{ while (*a != '\0' && *a == *b) { a++; b++; } return *a == *b; }

// A program's name as the lists of the open programs give it: its task's, without its path.
static const char *ProcName (unsigned nPid)
{
	static char s_Name[64];
	if (el_sys_name (nPid, s_Name, sizeof s_Name) <= 0) return "";
	const char *pBase = s_Name;
	for (const char *p = s_Name; *p != '\0'; p++) if (*p == '/' || *p == ':') pBase = p + 1;
	return pBase;
}

static CWindow *WinOf (unsigned nPid)
{
	for (int i = 0; i < EL_WINDOWS_MAX; i++)
		if (g_pElWin[i] != 0 && g_pElWin[i]->OwnerPid () == nPid) return g_pElWin[i];
	return 0;
}

static CWindow *WinById (unsigned nId)
{
	for (int i = 0; i < EL_WINDOWS_MAX; i++)
		if (g_pElWin[i] != 0 && g_pElWin[i]->Id () == nId) return g_pElWin[i];
	return 0;
}

static unsigned StateOf (CWindow *pW)
{
	return (g_pElWM->HasKeyFocus (pW) ? KAPI_WIN_KEYS : 0) | (pW->Minimised () ? KAPI_WIN_MINIMISED : 0)
	     | (pW->OffDesk () ? KAPI_WIN_OFFDESK : 0) | (unsigned) ((pW->Desk () + 1) & 0xFF) << 8;
}

// The caller's window made (kapi.cpp CreateWindow: no bigger than the screen; placed in the work
// area, clear of the screen's left fifth, when the caller gives no place).
static long OpCreate (unsigned nPid, const long *a, const u8 *pIn, unsigned nInLen)
{
	if (WinOf (nPid) != 0) return 1;				// (one window a program)
	if (nInLen < sizeof (struct el_create)) return 0;
	struct el_create C;
	memcpy (&C, pIn, sizeof C);
	C.title[sizeof C.title - 1] = '\0';
	int x = (int) a[0], y = (int) a[1], w = (int) a[2], h = (int) a[3];
	if (w <= 0 || h <= 0 || w > g_nScreenWidth || h > g_nScreenHeight) return 0;
	boolean bBorderless = (C.flags & WIN_FLAG_BORDERLESS) != 0;
	int nOuterW = w + (bBorderless ? 0 : 2 * WIN_BORDER);
	int nOuterH = h + (bBorderless ? 0 : WIN_TITLEBAR_H + WIN_BORDER);
	if (x < 0 || y < 0)
	{
		static unsigned s_nRng = 0;
		if (s_nRng == 0) s_nRng = el_port_ticks () | 1u;
		int nXMin = g_nScreenWidth / 5;
		int ax = 0, ay = 0, aw = g_nScreenWidth, ah = g_nScreenHeight;
		g_pElWM->WorkArea (&ax, &ay, &aw, &ah);
		int nXRange = g_nScreenWidth - nOuterW - nXMin;
		int nYRange = ay + ah - nOuterH - ay;
		s_nRng = s_nRng * 1103515245u + 12345u;
		x = nXRange > 0 ? nXMin + (int) (s_nRng % (unsigned) nXRange) : 0;
		s_nRng = s_nRng * 1103515245u + 12345u;
		y = ay + (nYRange > 0 ? (int) (s_nRng % (unsigned) nYRange) : 0);
	}
	if (!el_sys_attach (nPid)) return 0;
	el_core_owner (nPid);
	int id = el_core_window_add (x, y, w, h, C.title[0] != '\0' ? C.title : "app", C.flags, nPid);
	el_core_owner (0);
	return id >= 0 ? 1 : 0;
}

// The drag and drop's payload (kapi_drag_begin / kapi_drag_data), kept here.
#define DND_MAX		(KAPI_WS_DATA_MAX - (unsigned) sizeof (struct el_drag))
static u8 s_Dnd[KAPI_WS_DATA_MAX];
static unsigned s_nDndLen = 0;
static int s_nDndType = 0;

long el_op (unsigned nPid, int nOp, const long *a, const unsigned char *pIn, unsigned nInLen,
	    unsigned char *pOut, unsigned *pnOutLen)
{
	*pnOutLen = 0;
	CWindowManager *pWM = g_pElWM;
	if (pWM == 0) return EL_E_BADOP;
	CWindow *pWin = WinOf (nPid);

	switch (nOp)
	{
	// ---- what needs no window of the caller's ----
	case EL_OP_CREATE:
		return OpCreate (nPid, a, pIn, nInLen);

	case EL_OP_MENU_GET:
		{
			struct el_menu *pM = (struct el_menu *) pOut;
			memset (pM, 0, sizeof *pM);
			unsigned nSerial = pWM->GetActiveMenu (pM->spec, sizeof pM->spec, pM->title, sizeof pM->title);
			pM->serial = nSerial;
			*pnOutLen = (unsigned) (sizeof *pM - sizeof pM->spec) + StrLen (pM->spec) + 1;
			return (long) nSerial;
		}
	case EL_OP_MENU_COMMAND:
		return pWM->SendMenuCommand ((int) a[0]) ? 1 : 0;

	case EL_OP_WIN_LIST:
		{
			int nMax = (int) a[0], k = 0;
			struct kapi_win_info *pList = (struct kapi_win_info *) pOut;
			if (nMax > (int) (KAPI_WS_DATA_MAX / sizeof (struct kapi_win_info)))
				nMax = (int) (KAPI_WS_DATA_MAX / sizeof (struct kapi_win_info));
			if (nMax <= 0) return 0;
			{	// the desktop first (it is not a window: the wallpaper + the backmost windows)
				struct kapi_win_info &I = pList[k++];
				memset (&I, 0, sizeof I);
				I.id = KAPI_WIN_DESKTOP; I.w = g_nScreenWidth; I.h = g_nScreenHeight;
				I.flags = WIN_FLAG_BACKMOST | WIN_FLAG_BORDERLESS; I.alpha = 255; I.gen = pWM->DesktopGen ();
				memcpy (I.title, "Onyx Desktop", 13);
			}
			CWindow *List[WM_MAX_WINDOWS];
			unsigned n = pWM->Snapshot (List, WM_MAX_WINDOWS);	// (bottom to top)
			for (unsigned i = 0; i < n && k < nMax; i++)
			{
				CWindow *pW = List[i];
				struct kapi_win_info &I = pList[k++];
				memset (&I, 0, sizeof I);
				I.id = pW->Id (); I.pid = pW->OwnerPid ();
				I.x = pW->X () + pW->ChromeL (); I.y = pW->Y () + pW->ChromeT ();
				I.w = pW->ClientWidth (); I.h = pW->ClientHeight ();
				I.flags = pW->Flags (); I.alpha = pW->Alpha (); I.gen = pW->Gen ();
				I.state = StateOf (pW);
				I.chromeGen = pW->ChromeGen ();
				if (pW->HasChrome ()) { I.ow = pW->OuterW (); I.oh = pW->OuterH (); I.il = pW->ChromeL (); I.it = pW->ChromeT (); }
				const char *t = pW->Title ();
				unsigned j = 0;
				for (; j + 1 < sizeof I.title && t[j]; j++) I.title[j] = t[j];
			}
			*pnOutLen = (unsigned) k * sizeof (struct kapi_win_info);
			return k;
		}
	case EL_OP_WIN_RAISE:
		{
			CWindow *pW = WinById ((unsigned) a[0]);
			if (pW == 0) return -1;
			pWM->Raise (pW);
			ScreenDirty ();
			return 0;
		}
	case EL_OP_WIN_CLOSE:
		{
			CWindow *pW = WinById ((unsigned) a[0]);
			if (pW == 0) return -1;
			pW->RequestExit ();
			return 0;
		}
	case EL_OP_WIN_MINIMISE:
		{
			CWindow *pW = a[0] == 0 ? pWin : WinById ((unsigned) a[0]);
			if (pW == 0) return -1;
			pWM->Minimise (pW);
			return 0;
		}
	case EL_OP_WIN_DESK:
		{
			CWindow *pW = a[0] == 0 ? pWin : WinById ((unsigned) a[0]);
			if (pW == 0) return -3;
			return (int) a[1] < -1 ? pW->Desk () : pWM->MoveToDesk (pW, (int) a[1]);
		}
	case EL_OP_DESK:
		return (int) a[0] < 0 && (int) a[1] <= 0 ? pWM->DeskInfo () : pWM->SetDesk ((int) a[0], (int) a[1]);

	case EL_OP_WHEEL:
		if ((int) a[0] >= 0) pWM->SetWheelSpeed ((int) a[0]);
		return pWM->GetWheelSpeed ();

	case EL_OP_APP_LIST:			// the open programs' names, one a line (kapi_list_windows)
		{
			unsigned nPos = 0; int nCount = 0;
			unsigned Seen[EL_WINDOWS_MAX]; int nSeen = 0;
			CWindow *List[WM_MAX_WINDOWS];
			unsigned n = pWM->Snapshot (List, WM_MAX_WINDOWS);
			for (unsigned i = 0; i < n; i++)
			{
				CWindow *pW = List[i];
				if (pW->System () || pW->OffDesk () || pW->OwnerPid () == 0) continue;
				boolean bSeen = FALSE;
				for (int k = 0; k < nSeen; k++) if (Seen[k] == pW->OwnerPid ()) bSeen = TRUE;
				if (bSeen) continue;
				Seen[nSeen++] = pW->OwnerPid ();
				const char *pName = ProcName (pW->OwnerPid ());
				if (pName[0] == '\0') continue;
				for (unsigned j = 0; pName[j] != '\0'; j++)
					if (nPos + 2 < KAPI_WS_DATA_MAX) pOut[nPos++] = (u8) pName[j];
				if (nPos + 1 < KAPI_WS_DATA_MAX) pOut[nPos++] = '\n';
				nCount++;
			}
			pOut[nPos] = '\0';
			*pnOutLen = nPos + 1;
			return nCount;
		}
	case EL_OP_APP_RAISE:			// a running program's window to the front, by its name
	case EL_OP_APP_CLOSE:			// ... asked to close
		{
			if (nInLen == 0 || nInLen > 63) return 0;
			char Name[64];
			memcpy (Name, pIn, nInLen); Name[nInLen] = '\0';
			CWindow *List[WM_MAX_WINDOWS];
			unsigned n = pWM->Snapshot (List, WM_MAX_WINDOWS);
			for (unsigned i = 0; i < n; i++)
			{
				CWindow *pW = List[i];
				if (pW->OwnerPid () == 0 || !StrEq (ProcName (pW->OwnerPid ()), Name)) continue;
				if (nOp == EL_OP_APP_CLOSE) { pW->RequestExit (); return 1; }
				if (pW->OffDesk ()) continue;		// (an instance on another desk is not raised)
				pWM->Raise (pW);
				return 1;
			}
			return 0;
		}
	case EL_OP_DRAG_DATA:
		{
			struct el_drag *pD = (struct el_drag *) pOut;
			memset (pD, 0, sizeof *pD);
			pD->type = s_nDndType;
			memcpy (pOut + sizeof *pD, s_Dnd, s_nDndLen);
			*pnOutLen = (unsigned) sizeof *pD + s_nDndLen;
			return (long) s_nDndLen;
		}
	case EL_OP_WALLPAPER_GEN:
		pWM->GenerateWallpaper ((u32) a[0], (int) a[1], (unsigned) a[2] != 0 ? (unsigned) a[2] : (el_port_ticks () | 1u));
		return 1;

	case EL_OP_POINTER:			// the pointer, from the caller's client area's corner
		{
			int pt[2] = { pWM->CursorX (), pWM->CursorY () };
			if (pWin != 0) { pt[0] -= pWin->X () + pWin->ChromeL (); pt[1] -= pWin->Y () + pWin->ChromeT (); }
			memcpy (pOut, pt, sizeof pt);
			*pnOutLen = sizeof pt;
			return 1;
		}
	}

	// ---- the caller's window ----
	if (pWin == 0) return EL_E_NOWINDOW;
	switch (nOp)
	{
	case EL_OP_FRAME:
		{
			struct el_core_frame F;
			if (!el_core_window_frame_info (el_core_window_of (nPid), &F)) return 0;
			memcpy (pOut, &F, sizeof F);			// (struct el_frame: the same fields)
			*pnOutLen = sizeof (struct el_frame);
			return 1;
		}
	case EL_OP_HANDLER:
		if (a[0] == EL_HANDLER_KEY) pWin->SetKeyHandler ((u64) a[1]);
		else if (a[0] == EL_HANDLER_CLICK) pWin->SetClickHandler ((u64) a[1]);
		else if (a[0] == EL_HANDLER_POINTER) pWin->SetPointerHandler ((u64) a[1]);
		return 1;
	case EL_OP_MOVE:
		pWin->Move ((int) a[0], (int) a[1]);
		return 1;
	case EL_OP_RESIZE:
		pWin->SetLogicalSize ((int) a[0], (int) a[1]);
		return 1;
	case EL_OP_GROW:			// (kapi_resize_window2)
		{
			int w = (int) a[0], h = (int) a[1];
			if (w <= 0 || h <= 0) return 0;
			if (w > g_nScreenWidth) w = g_nScreenWidth;
			if (h > g_nScreenHeight) h = g_nScreenHeight;
			if (w > pWin->Canvas ()->Width () || h > pWin->Canvas ()->Height ())
			{
				el_core_owner (nPid);
				boolean bOK = pWin->Grow (w, h);
				el_core_owner (0);
				if (!bOK) return 0;
			}
			pWin->SetLogicalSize (w, h);
			int nStride = pWin->Canvas ()->Width ();
			memcpy (pOut, &nStride, sizeof nStride);
			*pnOutLen = sizeof nStride;
			return 1;
		}
	case EL_OP_ALPHA:
		pWin->SetAlpha ((int) a[0]);
		return 1;
	case EL_OP_RESIZABLE:
		if (pWin->Borderless () || pWin->Fixed ()) return -1;
		pWin->SetResizable (a[0] != 0, (int) a[1], (int) a[2]);
		return 0;
	case EL_OP_GEOMETRY:
		{
			struct kapi_win_geom G;
			memset (&G, 0, sizeof G);
			G.x = pWin->X (); G.y = pWin->Y ();
			G.w = pWin->OuterWidth (); G.h = pWin->OuterHeight ();
			G.cw = pWin->ClientWidth (); G.ch = pWin->ClientHeight ();
			pWM->WorkArea (&G.ax, &G.ay, &G.aw, &G.ah);
			G.state = StateOf (pWin);
			memcpy (pOut, &G, sizeof G);
			*pnOutLen = sizeof G;
			return 0;
		}
	case EL_OP_CURSOR:
		{
			int nShape = (int) a[0];
			if (nShape < 0 || nShape >= KAPI_CURSOR_COUNT) return -1;
			int nWas = (int) pWin->CursorShape ();
			if (nWas != nShape) pWM->SetWindowCursor (pWin, (unsigned) nShape);
			return nWas;
		}
	case EL_OP_MENU_SET:
		{
			static char s_Spec[WIN_MENU_MAX];
			unsigned n = nInLen < WIN_MENU_MAX - 1 ? nInLen : WIN_MENU_MAX - 1;
			memcpy (s_Spec, pIn, n); s_Spec[n] = '\0';
			pWin->SetMenu (s_Spec, (u64) a[0]);
			return 1;
		}
	case EL_OP_DRAG_BEGIN:
		{
			if (nInLen < sizeof (struct el_drag)) return 0;
			struct el_drag D;
			memcpy (&D, pIn, sizeof D);
			D.label[sizeof D.label - 1] = '\0';
			unsigned nLen = nInLen - (unsigned) sizeof D;
			if (!pWM->DragBegin (pWin, D.label)) return 0;
			memcpy (s_Dnd, pIn + sizeof D, nLen);
			s_nDndLen = nLen; s_nDndType = D.type;
			return 1;
		}
	}
	return EL_E_BADOP;
}

// The program that has the keyboard (0: none), for the kernel's kapi_key_held and the pads.
unsigned el_core_focus_pid (void)
{
	CWindow *pWin = g_pElWM != 0 ? g_pElWM->KeyTarget () : 0;
	return pWin != 0 ? pWin->OwnerPid () : 0;
}
