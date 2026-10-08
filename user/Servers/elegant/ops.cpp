//
// ops.cpp -- the programs' requests answered (the protocol: uikit/port/elegant.h): each one does what
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

extern "C" int memcmp (const void *a, const void *b, size_t n);
#include "uikit/port/elegant.h"
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

// A program's window: its first (0), or another one (1 .. EL_WINDOWS_MORE; uikit/port/elegant.h).
static CWindow *WinOf (unsigned nPid, int nWin = 0)
{
	int id = el_core_window_of_win (nPid, nWin);
	return id >= 0 ? g_pElWin[id] : 0;
}

static int IdOf (CWindow *pWin)
{
	for (int i = 0; i < EL_WINDOWS_MAX; i++) if (pWin != 0 && g_pElWin[i] == pWin) return i;
	return -1;
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

// What is kept in the kernel for a program's window (KAPI_WS_STATE): the window as Elegant knows it
// -- what an Elegant started again makes it anew from, without the program doing anything (its
// pixels: the same memory, the kernel kept the buffers).
struct TSaved
{
	unsigned nMagic;			// SAVED_MAGIC
	unsigned nFlags;			// WIN_FLAG_*
	int	 x, y, w, h;			// the frame's place, the client size
	int	 nCanvasW, nCanvasH;		// the canvas as allocated (its buffers' size)
	int	 nAlpha, nDesk, bMinimised;
	int	 bResizable, nMinW, nMinH;
	unsigned nCursor, nMenuGen;
	u64	 ulKey, ulClick, ulPointer, ulMenu;	// the program's handlers
	char	 Title[48];
	char	 Menu[WIN_MENU_MAX];
	u8	 Reserved[KAPI_WS_STATE_BYTES - 16 * 4 - 4 * 8 - 48 - WIN_MENU_MAX];
};
#define SAVED_MAGIC	0x456C6732u		// "Elg2"
static_assert (sizeof (TSaved) == KAPI_WS_STATE_BYTES, "a window's saved state is what the kernel keeps");

// (what changes often, compared at each turn; the rest when the menu's counter moved)
struct TSavedKey { int x, y, w, h, cw, ch, alpha, desk, min, sizable, minw, minh; unsigned cursor, menugen; u64 k, c, p; };
static TSavedKey s_Key[EL_WINDOWS_MAX];
static TSaved s_Save;				// (2.3 KB: not on the stack)

static void KeyOf (CWindow *pWin, TSavedKey *pK)
{
	memset (pK, 0, sizeof *pK);
	pK->x = pWin->X (); pK->y = pWin->Y (); pK->w = pWin->ClientWidth (); pK->h = pWin->ClientHeight ();
	pK->cw = pWin->Canvas ()->Width (); pK->ch = pWin->Canvas ()->Height ();
	pK->alpha = pWin->Alpha (); pK->desk = pWin->Desk (); pK->min = pWin->Minimised () ? 1 : 0;
	pK->sizable = pWin->Resizable () ? 1 : 0; pK->minw = pWin->MinClientW (); pK->minh = pWin->MinClientH ();
	pK->cursor = pWin->CursorShape (); pK->menugen = pWin->MenuGen ();
	pK->k = pWin->KeyHandler (); pK->c = pWin->ClickHandler (); pK->p = pWin->PointerHandler ();
}

static void Save (CWindow *pWin, const TSavedKey &K)
{
	TSaved &S = s_Save;
	memset (&S, 0, sizeof S);
	S.nMagic = SAVED_MAGIC; S.nFlags = pWin->Flags ();
	S.x = K.x; S.y = K.y; S.w = K.w; S.h = K.h; S.nCanvasW = K.cw; S.nCanvasH = K.ch;
	S.nAlpha = K.alpha; S.nDesk = K.desk; S.bMinimised = K.min;
	S.bResizable = K.sizable; S.nMinW = K.minw; S.nMinH = K.minh;
	S.nCursor = K.cursor; S.nMenuGen = K.menugen;
	S.ulKey = K.k; S.ulClick = K.c; S.ulPointer = K.p; S.ulMenu = pWin->MenuHandler ();
	const char *t = pWin->Title ();
	for (unsigned i = 0; i + 1 < sizeof S.Title && t[i] != '\0'; i++) S.Title[i] = t[i];
	const char *m = pWin->Menu ();
	for (unsigned i = 0; i + 1 < sizeof S.Menu && m[i] != '\0'; i++) S.Menu[i] = m[i];
	el_sys_state (pWin->OwnerPid (), &S, 1);
}

void el_core_save_states (unsigned nSelf)
{
	if (g_pElWM == 0 || g_pElWM->FullscreenWindow () != 0) return;	// (a full-screen window is at 0, 0 for a while)
	for (int i = 0; i < EL_WINDOWS_MAX; i++)
	{
		CWindow *pWin = g_pElWin[i];
		if (pWin == 0 || pWin->OwnerPid () == 0 || pWin->OwnerPid () == nSelf) continue;
		if (el_core_window_win (i) != 0) continue;	// (a program's other windows: AppKit makes them again)
		TSavedKey K;
		KeyOf (pWin, &K);
		if (memcmp (&K, &s_Key[i], sizeof K) == 0) continue;
		s_Key[i] = K;
		Save (pWin, K);
	}
}

// A window made anew from what was saved for its program -> its number, -1: nothing saved (or no memory).
static int Restore (unsigned nPid)
{
	TSaved &S = s_Save;
	if (!el_sys_state (nPid, &S, 0) || S.nMagic != SAVED_MAGIC || S.nCanvasW <= 0 || S.nCanvasH <= 0) return -1;
	S.Title[sizeof S.Title - 1] = '\0'; S.Menu[sizeof S.Menu - 1] = '\0';
	el_core_owner (nPid);
	int id = el_core_window_add (S.x, S.y, S.nCanvasW, S.nCanvasH, S.Title, S.nFlags, nPid);	// (its buffers: adopted)
	el_core_owner (0);
	if (id < 0) return -1;
	CWindow *pWin = g_pElWin[id];
	if (S.w > 0 && S.h > 0) pWin->SetLogicalSize (S.w, S.h);
	pWin->SetAlpha (S.nAlpha);
	pWin->SetKeyHandler (S.ulKey); pWin->SetClickHandler (S.ulClick); pWin->SetPointerHandler (S.ulPointer);
	if (S.Menu[0] != '\0' || S.ulMenu != 0) pWin->SetMenu (S.Menu, S.ulMenu);
	if (S.bResizable) pWin->SetResizable (TRUE, S.nMinW, S.nMinH);
	if (S.nCursor != 0 && S.nCursor < KAPI_CURSOR_COUNT) g_pElWM->SetWindowCursor (pWin, S.nCursor);
	if (S.nDesk != pWin->Desk ()) g_pElWM->MoveToDesk (pWin, S.nDesk);
	if (S.bMinimised) g_pElWM->Minimise (pWin);
	KeyOf (pWin, &s_Key[id]);
	return id;
}

// An Elegant started again: every attached program's window made anew -> how many.
int el_core_restore_all (void)
{
	static unsigned Pids[256];
	int n = el_sys_clients (Pids, 256), nMade = 0;
	for (int i = 0; i < n; i++)
		if (WinOf (Pids[i]) == 0 && Restore (Pids[i]) >= 0) nMade++;
	return nMade;
}

// The caller's window made (kapi.cpp CreateWindow: no bigger than the screen; placed in the work
// area, clear of the screen's left fifth, when the caller gives no place).
static long OpCreate (unsigned nPid, int nWin, const long *a, const u8 *pIn, unsigned nInLen)
{
	if (nWin < 0 || nWin > EL_WINDOWS_MORE) return 0;
	if (WinOf (nPid, nWin) != 0) return 1;				// (it has that one)
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
	el_core_owner (nPid, nWin);
	int id = el_core_window_add (x, y, w, h, C.title[0] != '\0' ? C.title : "app", C.flags, nPid);
	el_core_owner (0);
	if (id < 0) return 0;
	memset (&s_Key[id], 0, sizeof s_Key[id]);			// (saved at the next turn)
	CWindow *pFirst = nWin > 0 ? WinOf (nPid) : 0;			// (v94) another window: its program's menu
	if (pFirst != 0 && (pFirst->Menu ()[0] != '\0' || pFirst->MenuHandler () != 0)) g_pElWin[id]->SetMenu (pFirst->Menu (), pFirst->MenuHandler ());
	return 1;
}

// A program's shared memory that is not a window's: its copy of the wallpaper, its transfer buffer
// (the pixels of uk_win_read). One of each a program; freed when the program ends.
#define PROGS_MAX	32
static struct TProg { unsigned nPid; u32 *pWall; u32 *pXfer; unsigned nXferBytes; } s_Prog[PROGS_MAX];

static TProg *ProgOf (unsigned nPid, boolean bMake)
{
	TProg *pFree = 0;
	for (int i = 0; i < PROGS_MAX; i++)
	{
		if (s_Prog[i].nPid == nPid) return &s_Prog[i];
		if (s_Prog[i].nPid == 0 && pFree == 0) pFree = &s_Prog[i];
	}
	if (!bMake || pFree == 0 || !el_sys_attach (nPid)) return 0;	// (shared memory is an attached program's)
	memset (pFree, 0, sizeof *pFree);
	pFree->nPid = nPid;
	return pFree;
}

// The status area's icons (v95, uikit/port/elegant.h EL_OP_TRAY_*): one a program, shown by the menu bar.
static struct TTray { unsigned nPid, nGen; u64 ulHandler; struct el_tray T; } s_Tray[KAPI_TRAY_MAX];
static unsigned s_nTrayGen = 1;

static TTray *TrayOf (unsigned nPid, boolean bMake)
{
	TTray *pFree = 0;
	for (int i = 0; i < KAPI_TRAY_MAX; i++)
	{
		if (s_Tray[i].nPid == nPid) return &s_Tray[i];
		if (s_Tray[i].nPid == 0 && pFree == 0) pFree = &s_Tray[i];
	}
	if (!bMake || pFree == 0) return 0;
	memset (pFree, 0, sizeof *pFree);
	pFree->nPid = nPid;
	return pFree;
}

// A click on a program's icon: OPEN shows its first window (else its latest), back from minimised, and
// tells the program; MENU only tells it.
static long TrayActivate (unsigned nPid, int nKind)
{
	TTray *t = TrayOf (nPid, FALSE);
	if (t == 0) return 0;
	CWindow *pWin = WinOf (nPid);
	if (pWin == 0)
	{
		CWindow *List[WM_MAX_WINDOWS];
		unsigned n = g_pElWM->Snapshot (List, WM_MAX_WINDOWS);
		for (unsigned j = n; j-- > 0 && pWin == 0; ) if (List[j]->OwnerPid () == nPid) pWin = List[j];
	}
	if (pWin == 0) return 0;
	if (nKind == KAPI_TRAY_OPEN) { g_pElWM->Raise (pWin); ScreenDirty (); }
	if (t->ulHandler != 0)
	{
		GUIEvent Ev;
		Ev.ulHandler = t->ulHandler; Ev.ulSender = 0; Ev.nEvent = GUI_EVENT_TRAY; Ev.lValue = nKind; Ev.nMods = 0;
		pWin->PushEvent (Ev);
	}
	return 1;
}

void el_core_program_gone (unsigned nPid)
{
	TTray *pT = TrayOf (nPid, FALSE);
	if (pT != 0) { memset (pT, 0, sizeof *pT); s_nTrayGen++; }
	TProg *p = ProgOf (nPid, FALSE);
	if (p == 0) return;
	if (p->pWall != 0) el_shared_free (p->pWall);
	if (p->pXfer != 0) el_shared_free (p->pXfer);
	memset (p, 0, sizeof *p);
}

static u32 *XferOf (unsigned nPid, unsigned nBytes)
{
	TProg *p = ProgOf (nPid, TRUE);
	if (p == 0) return 0;
	if (p->pXfer != 0 && p->nXferBytes >= nBytes) return p->pXfer;
	u32 *pNew = (u32 *) el_shared_alloc (nPid, KAPI_WS_SLOT_XFER, nBytes);	// (it replaces the old one in the program)
	if (pNew == 0) return 0;
	if (p->pXfer != 0) el_shared_free (p->pXfer);
	p->pXfer = pNew; p->nXferBytes = nBytes;
	return pNew;
}

// (uk_win_read) -> 0: *pR says what is at the caller's transfer buffer; -1.
static long OpWinRead (unsigned nPid, const long *a, struct el_read *pR)
{
	CWindowManager *pWM = g_pElWM;
	unsigned nId = (unsigned) a[0];
	int nPart = (int) a[1];
	int x = (int) (a[2] >> 32), y = (int) (unsigned) a[2], w = (int) (a[3] >> 32), h = (int) (unsigned) a[3];
	pR->w = pR->h = 0;
	if (nId == KAPI_WIN_DESKTOP)			// the whole desktop, composed straight in
	{
		if (nPart != 0 || x != 0 || y != 0 || w != g_nScreenWidth || h != g_nScreenHeight) return -1;
		u32 *pDst = XferOf (nPid, (unsigned) w * h * 4);
		if (pDst == 0) return -1;
		GImage Img (pDst, w, h);
		pWM->CompositeDesktop (&Img);
		pR->w = w; pR->h = h;
		return 0;
	}
	CWindow *pW = 0;
	for (int i = 0; i < EL_WINDOWS_MAX; i++)
		if (g_pElWin[i] != 0 && g_pElWin[i]->Id () == nId) pW = g_pElWin[i];
	if (pW == 0) return -1;
	const u8 *pSrc; unsigned nPitch; int W, H;
	if (nPart == 1 || nPart == 2)
	{
		if (!pW->HasChrome ()) return -1;
		W = pW->OuterW (); H = pW->OuterH ();
		pSrc = (const u8 *) pW->ChromePhys (nPart - 1); nPitch = (unsigned) W * 4;
	}
	else if (nPart != 0) return -1;
	else
	{
		W = pW->ClientWidth (); H = pW->ClientHeight ();
		pSrc = (const u8 *) pW->CanvasBuffer (); nPitch = (unsigned) pW->Canvas ()->Width () * 4;
		if (pSrc == 0) return -1;
	}
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > W) w = W - x;
	if (y + h > H) h = H - y;
	if (w <= 0 || h <= 0) return 0;
	u32 *pDst = XferOf (nPid, (unsigned) w * h * 4);
	if (pDst == 0) return -1;
	pR->w = w; pR->h = h;
	if (nPart == 0)				// the window as last presented, when its program keeps that copy:
	{					// never a picture half painted (read again if a present came meanwhile)
		boolean bRead = FALSE;
		for (int nTry = 0; nTry < 3; nTry++)
		{
			unsigned nSeq = 0;
			const u8 *pShot = (const u8 *) el_core_shot_read (pSrc, nPitch * (unsigned) H, &nSeq);
			if (pShot == 0) break;
			for (int r = 0; r < h; r++)
				memcpy (pDst + (size_t) r * w, pShot + (size_t) (y + r) * nPitch + (size_t) x * 4, (size_t) w * 4);
			bRead = TRUE;
			if (el_core_shot_same (pSrc, nSeq)) break;
		}
		if (bRead) return 0;		// (after three presents in a row: rows of two whole pictures)
	}
	for (int r = 0; r < h; r++)
		memcpy (pDst + (size_t) r * w, pSrc + (size_t) (y + r) * nPitch + (size_t) x * 4, (size_t) w * 4);
	return 0;
}

// The drag and drop's payload (uk_win_drag_begin / uk_win_drag_data), kept here.
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
	int nWin = (int) ((unsigned) nOp >> EL_OP_WINDOW_SHIFT) & 0xFF;	// (v94) which of the caller's windows
	nOp &= EL_OP_MASK;
	CWindow *pWin = WinOf (nPid, nWin);

	switch (nOp)
	{
	// ---- what needs no window of the caller's ----
	case EL_OP_CREATE:
		return OpCreate (nPid, nWin, a, pIn, nInLen);

	case EL_OP_DESTROY:			// (v94) one of the caller's windows closed (not its first)
		{
			int id = IdOf (pWin);
			if (id < 0 || nWin == 0) return 0;
			el_core_window_remove (id);
			ScreenDirty ();
			return 1;
		}

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
				if (pW == pWM->FullscreenWindow ()) { I.x = I.y = 0; I.w = g_nScreenWidth; I.h = g_nScreenHeight; I.state |= KAPI_WIN_FULLSCREEN; }
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
			CWindow *pW = a[0] == 0 ? pWin : WinById ((unsigned) a[0]);	// (0: the caller's, v94)
			if (pW == 0) return -1;
			pWM->Raise (pW);
			ScreenDirty ();
			return 0;
		}
	case EL_OP_WIN_MOVE:			// (v96) a window put there (the remote desktop's)
		{
			CWindow *pW = a[0] == 0 ? pWin : WinById ((unsigned) a[0]);
			if (pW == 0 || pW->Backmost () || pW->Topmost () || pW == pWM->FullscreenWindow ()) return -1;
			int x = (int) a[1] - pW->ChromeL (), y = (int) a[2] - pW->ChromeT ();
			if (x != pW->X () || y != pW->Y ()) { pW->Move (x, y); ScreenDirty (); }
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

	case EL_OP_APP_LIST:			// the open programs' names, one a line (uk_win_apps)
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
			for (unsigned j = n; j-- > 0; )		// (from the top: a program's latest window raised)
			{
				CWindow *pW = List[j];
				if (pW->OwnerPid () == 0 || !StrEq (ProcName (pW->OwnerPid ()), Name)) continue;
				if (nOp == EL_OP_APP_CLOSE)		// (its first window: the program ends)
				{
					CWindow *pFirst = WinOf (pW->OwnerPid ());
					(pFirst != 0 ? pFirst : pW)->RequestExit ();
					return 1;
				}
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

	case EL_OP_WALLPAPER_BUF:		// the caller's copy of the wallpaper: what is shown now
		{
			TProg *p = ProgOf (nPid, TRUE);
			int W = g_nScreenWidth, H = g_nScreenHeight;
			if (p == 0) return 0;
			if (p->pWall == 0) p->pWall = (u32 *) el_shared_alloc (nPid, KAPI_WS_SLOT_WALLPAPER, (unsigned long) W * H * 4);
			if (p->pWall == 0) return 0;
			GImage Img (p->pWall, W, H);
			pWM->CompositeDesktop (&Img);
			int wh[2] = { W, H };
			memcpy (pOut, wh, sizeof wh);
			*pnOutLen = sizeof wh;
			return 1;
		}
	case EL_OP_WALLPAPER_COMMIT:
		{
			TProg *p = ProgOf (nPid, FALSE);
			int W = g_nScreenWidth, H = g_nScreenHeight;
			u64 ulPhys = 0; unsigned nPages = 0;
			u32 *pLive = p != 0 && p->pWall != 0 ? pWM->EnsureWallpaperBuffer (W, H, &ulPhys, &nPages) : 0;
			if (pLive == 0) return 0;
			memcpy (pLive, p->pWall, (size_t) W * H * 4);
			pWM->CommitWallpaper ();
			return 1;
		}
	case EL_OP_WIN_READ:
		{
			struct el_read R;
			long nResult = OpWinRead (nPid, a, &R);
			memcpy (pOut, &R, sizeof R);
			*pnOutLen = sizeof R;
			return nResult;
		}
	case EL_OP_CURSOR_SHOWN:
		return (long) pWM->CursorShown ();

	case EL_OP_TRAY_SET:			// (v95) the caller's icon of the status area
		{
			if (nInLen < sizeof (struct el_tray)) return 0;
			TTray *t = TrayOf (nPid, TRUE);
			if (t == 0) return 0;
			memcpy (&t->T, pIn, sizeof t->T);
			t->T.tip[sizeof t->T.tip - 1] = '\0';
			t->ulHandler = (u64) a[0];
			t->nGen = ++s_nTrayGen;
			return 1;
		}
	case EL_OP_TRAY_CLEAR:
		{
			TTray *t = TrayOf (nPid, FALSE);
			if (t != 0) { memset (t, 0, sizeof *t); s_nTrayGen++; }
			return 1;
		}
	case EL_OP_TRAY_LIST:
		{
			int nMax = (int) a[0], k = 0;
			struct kapi_tray_info *pList = (struct kapi_tray_info *) pOut;
			if (nMax > (int) (KAPI_WS_DATA_MAX / sizeof (struct kapi_tray_info))) nMax = (int) (KAPI_WS_DATA_MAX / sizeof (struct kapi_tray_info));
			for (int i = 0; i < KAPI_TRAY_MAX && k < nMax; i++)
			{
				if (s_Tray[i].nPid == 0) continue;
				struct kapi_tray_info &I = pList[k++];
				memset (&I, 0, sizeof I);
				I.pid = s_Tray[i].nPid; I.gen = s_Tray[i].nGen;
				memcpy (I.tip, s_Tray[i].T.tip, sizeof I.tip);
				I.tip[sizeof I.tip - 1] = '\0';
			}
			*pnOutLen = (unsigned) k * sizeof (struct kapi_tray_info);
			return k;
		}
	case EL_OP_TRAY_ICON:
		{
			TTray *t = TrayOf ((unsigned) a[0], FALSE);
			if (t == 0 || a[0] == 0) return 0;
			memcpy (pOut, t->T.px, sizeof t->T.px);
			*pnOutLen = sizeof t->T.px;
			return 1;
		}
	case EL_OP_TRAY_ACTIVATE:
		return a[0] != 0 ? TrayActivate ((unsigned) a[0], (int) a[1]) : 0;

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
			if (!el_core_window_frame_info (IdOf (pWin), &F)) return 0;
			memcpy (pOut, &F, sizeof F);			// (struct el_frame: the same fields)
			*pnOutLen = sizeof (struct el_frame);
			return 1;
		}
	case EL_OP_HANDLER:
		if (a[0] == EL_HANDLER_KEY) pWin->SetKeyHandler ((u64) a[1]);
		else if (a[0] == EL_HANDLER_CLICK) pWin->SetClickHandler ((u64) a[1]);
		else if (a[0] == EL_HANDLER_POINTER) pWin->SetPointerHandler ((u64) a[1]);
		return 1;
	case EL_OP_SHOT:
		{
			struct el_shot_info I;
			if (!el_core_shot_info (pWin->CanvasBuffer (), &I.ctl_off, &I.copy_off, &I.cap)) return 0;
			memcpy (pOut, &I, sizeof I);
			*pnOutLen = sizeof I;
			return 1;
		}
	case EL_OP_MOVE:
		pWin->Move ((int) a[0], (int) a[1]);
		return 1;
	case EL_OP_RESIZE:
		pWin->SetLogicalSize ((int) a[0], (int) a[1]);
		return 1;
	case EL_OP_GROW:			// (uk_win_resize2)
		{
			int w = (int) a[0], h = (int) a[1];
			if (w <= 0 || h <= 0) return 0;
			if (w > g_nScreenWidth) w = g_nScreenWidth;
			if (h > g_nScreenHeight) h = g_nScreenHeight;
			if (w > pWin->Canvas ()->Width () || h > pWin->Canvas ()->Height ())
			{
				el_core_owner (nPid, nWin);
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
			if (nWin == 0)				// (v94) the program's other windows show its menu too
				for (int w = 1; w <= EL_WINDOWS_MORE; w++)
				{
					CWindow *pW = WinOf (nPid, w);
					if (pW != 0) pW->SetMenu (s_Spec, (u64) a[0]);
				}
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

unsigned el_core_active_id (void)
{
	CWindow *pWin = g_pElWM != 0 ? g_pElWM->KeyTarget () : 0;
	return pWin != 0 ? pWin->Id () : 0;
}

void el_core_wheel (int lines)
{
	if (g_pElWM != 0 && lines > 0) g_pElWM->SetWheelSpeed (lines);
}

// The program that has the keyboard (0: none), for the kernel's kapi_key_held and the pads.
unsigned el_core_focus_pid (void)
{
	CWindow *pWin = g_pElWM != 0 ? g_pElWM->KeyTarget () : 0;
	return pWin != 0 ? pWin->OwnerPid () : 0;
}
