//
// wm.cpp -- PocketUI's policy (../common/policy.h): where the windows go and which one is shown, in the pocket and
// console modes (docs/POCKETUI-TECH-STUDY.md sections 4.3, 7.3; docs/COMPACT-SHELL-STUDY.md section 6). The window
// store, the compositor, the routing and the requests' decoding are the code PocketUI shares with Elegant
// (../common/); this file decides:
//
//   - the work area: the screen less the status band at the top (pocket: band.cpp; console: the whole screen);
//   - a program's normal window is a CARD -- framed (UIKit's skin draws its Milk title), centred in the work area --
//     when it fits there with its frame; else it is FILLED: frameless, at the work area's top left. A window that
//     says it is resizable (EL_OP_RESIZABLE) is filled: its frame dropped, GUI_EVENT_WINRESIZE to the work area's
//     size (UIKit's Root applies it as a frame dragged to that size);
//   - a borderless or topmost window (a popup, a toast) stays where it asked, kept on the screen; the desktop's
//     bands (a backmost window, a topmost one on the screen's top or bottom edge: the menu bar, the dock) are
//     refused -- the pocket shell's are its own programs (phase P5);
//   - one workspace; filled windows do not move;
//   - ONE APP IN FRONT: the program whose window is the topmost is shown, every other program's windows are set
//     aside (hidden until brought to the front); Alt+Tab brings the program at the back to the front (Alt+Shift+
//     Tab: the one just behind the front one) -- a minimal cycling until the shell's switcher (phase P5).
//
// Console (--mode console) is the same policy for now, without the band (docs/POCKETUI-TECH-STUDY.md section 7.5).
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
#include "uikit/port/pocket.h"
#include "uikit/win.h"			// (UK_SC_*: the size classes uk_win_server gives)
#include "policy.h"
#include "pocketui.h"

int g_nPkMode = PK_MODE_POCKET;
unsigned g_nPkSelf;

// What the policy made of each window (by its number in the store); a window it has not seen yet (made by a
// request: the kind create chose; given back to a server started again: from its flags) is looked at by tick.
static struct TState { CWindow *pWin; int nKind; int nSentW, nSentH; unsigned nSentAt; boolean bMaxSent; } s_St[EL_WINDOWS_MAX];
#define FILL_WAIT	25		// ticks (0.25 s) a program has to apply GUI_EVENT_WINRESIZE before it is asked to maximise
static struct { unsigned nPid; int nWin, nKind; } s_Pending;	// (create's choice, for the window about to be made)
static unsigned s_nFront;					// the program in front (0: none)

static void Say (const char *s1, const char *s2 = "", const char *s3 = "")
{
	char line[160];
	unsigned n = 0;
	const char *Part[5] = { "pocketui: ", s1, s2, s3, "\n" };
	for (int k = 0; k < 5; k++)
		for (const char *s = Part[k]; *s && n + 1 < sizeof line; s++) line[n++] = *s;
	line[n] = 0;
	ax_puts (line);
}

static void Area (int *x, int *y, int *w, int *h)
{
	if (g_pElWM != 0) g_pElWM->WorkArea (x, y, w, h);
	else { *x = 0; *y = 0; *w = g_nScreenWidth; *h = g_nScreenHeight; }
}

static void Clamp (int *x, int *y, int w, int h)		// a window of w x h (outer) kept on the screen
{
	if (*x + w > g_nScreenWidth) *x = g_nScreenWidth - w;
	if (*y + h > g_nScreenHeight) *y = g_nScreenHeight - h;
	if (*x < 0) *x = 0;
	if (*y < 0) *y = 0;
}

static int Classify (int id, CWindow *p);
static void Fill (int id);

// A window's state, up to date: one the policy had not seen (made since the last turn, given back to a server
// started again) is classified now.
static int KindOf (int id)
{
	if (id < 0 || id >= EL_WINDOWS_MAX) return PK_KIND_NONE;
	CWindow *p = g_pElWin[id];
	if (p != s_St[id].pWin)
	{
		s_St[id].pWin = p; s_St[id].nSentW = s_St[id].nSentH = -1; s_St[id].bMaxSent = FALSE;
		s_St[id].nKind = p != 0 ? Classify (id, p) : PK_KIND_NONE;
		if (s_St[id].nKind == PK_KIND_FILL && p->Resizable ()) Fill (id);
	}
	return s_St[id].nKind;
}

int pk_kind (int id)		{ return KindOf (id); }
unsigned pk_front_pid (void)	{ return s_nFront; }

// A program's window, normal (not the band, not a toast, not the desktop's): what "one app in front" counts.
static boolean AppWindow (CWindow *p)
{
	return p != 0 && p->OwnerPid () != 0 && p->OwnerPid () != g_nPkSelf && !p->System () && !p->Topmost () && !p->Backmost ();
}

// ---- a window made ------------------------------------------------------------------------------------

static int Create (unsigned nPid, int nWin, int *px, int *py, int *pw, int *ph, unsigned *pnFlags)
{
	unsigned f = *pnFlags;
	int nKind;
	if (f & WIN_FLAG_BACKMOST)
	{
		Say ("a desktop's backmost window refused (the pocket shell's are its own)");
		return 0;
	}
	if (f & WIN_FLAG_TOPMOST)
	{
		boolean bTop = *py == 0, bBottom = *py > 0 && *py + *ph >= g_nScreenHeight && *pw >= g_nScreenWidth / 2;
		if (bTop || bBottom)
		{
			Say ("a desktop's band refused (", bTop ? "the screen's top edge" : "the screen's bottom edge", "): the status band is PocketUI's");
			return 0;
		}
	}
	int ax, ay, aw, ah;
	Area (&ax, &ay, &aw, &ah);
	if (f & (WIN_FLAG_TOPMOST | WIN_FLAG_BORDERLESS))		// a popup, a toast: where it asked, on the screen
	{
		if (*px < 0 || *py < 0) { *px = ax + (aw - *pw) / 2; *py = ay + (ah - *ph) / 2; }
		Clamp (px, py, *pw, *ph);
		nKind = PK_KIND_POPUP;
	}
	else
	{
		int ow = *pw + 2 * WIN_BORDER, oh = *ph + WIN_TITLEBAR_H + WIN_BORDER;
		if (ow <= aw && oh <= ah)				// a card: framed, centred
		{
			*px = ax + (aw - ow) / 2; *py = ay + (ah - oh) / 2;
			nKind = PK_KIND_CARD;
		}
		else							// too big for a card: filled, frameless
		{
			*pnFlags = f | WIN_FLAG_BORDERLESS;
			*px = ax; *py = ay;
			nKind = PK_KIND_FILL;
		}
	}
	s_Pending.nPid = nPid; s_Pending.nWin = nWin; s_Pending.nKind = nKind;
	// A window bigger than the screen (an app made for the desktop, ~1000 x 700, on an 800 x 480 screen) is made
	// all the same -- Elegant refuses it --: filled, what does not fit is cut (a scrolling viewport: phase P6).
	return *pw <= SCREEN_MAX_W && *ph <= SCREEN_MAX_H ? 2 : 1;
}

// A window the policy had not seen: its kind (create's choice, else from what it is -- a server started again).
static int Classify (int id, CWindow *p)
{
	unsigned nPid = p->OwnerPid ();
	if (nPid == g_nPkSelf || nPid == 0) return PK_KIND_OWN;
	if (s_Pending.nPid == nPid && s_Pending.nWin == el_core_window_win (id) && s_Pending.nKind != PK_KIND_NONE)
	{
		int k = s_Pending.nKind;
		s_Pending.nKind = PK_KIND_NONE;
		return k;
	}
	int ax, ay, aw, ah;
	Area (&ax, &ay, &aw, &ah);
	if (p->Topmost () || p->Backmost ()) return PK_KIND_POPUP;
	if (p->Borderless ()) return p->Resizable () || (p->X () == ax && p->Y () == ay && !p->AlphaCanvas ()) ? PK_KIND_FILL : PK_KIND_POPUP;
	return p->Resizable () && !p->Fixed () ? PK_KIND_FILL : PK_KIND_CARD;
}

// A window becomes a filled one: its frame dropped (a card's), at the work area's top left; told its size by tick.
static void Fill (int id)
{
	CWindow *p = g_pElWin[id];
	if (p == 0) return;
	if (!p->Borderless ()) p->DropChrome ();
	int ax, ay, aw, ah;
	Area (&ax, &ay, &aw, &ah);
	if (p->X () != ax || p->Y () != ay) p->Move (ax, ay);
	s_St[id].nKind = PK_KIND_FILL;
	s_St[id].nSentW = s_St[id].nSentH = -1;
	ScreenDirty ();
}

// ---- the requests PocketUI answers its own way ------------------------------------------------------------

static void ServerInfo (struct pk_server *s)
{
	memset (s, 0, sizeof *s);
	const char *n = PK_PROTO_NAME;
	for (unsigned i = 0; n[i] != 0 && i + 1 < sizeof s->name; i++) s->name[i] = n[i];
	s->mode = g_nPkMode;
	s->screen_w = g_nScreenWidth; s->screen_h = g_nScreenHeight;
	Area (&s->work_x, &s->work_y, &s->work_w, &s->work_h);
	s->scale = 100;
	s->size_class = g_nPkMode == PK_MODE_CONSOLE ? UK_SC_CONSOLE : g_nScreenHeight > g_nScreenWidth ? UK_SC_NARROW : UK_SC_COMPACT;
	s->band_h = s->work_y;
}

static int IdOfWin (unsigned nPid, int nWin)		{ return el_core_window_of_win (nPid, nWin); }

static int IdOfWinId (unsigned nId)			// (a window's id, as uk_win_list gives it)
{
	for (int i = 0; i < EL_WINDOWS_MAX; i++)
		if (g_pElWin[i] != 0 && g_pElWin[i]->Id () == nId) return i;
	return -1;
}

static int Op (unsigned nPid, int nOp, int nWin, const long *a, const unsigned char *pIn, unsigned nInLen,
	       unsigned char *pOut, unsigned *pnOutLen, long *pnStatus)
{
	switch (nOp)
	{
	case PK_OP_HELLO:
		{
			struct pk_hello H;
			memset (&H, 0, sizeof H);
			memcpy (&H, pIn, nInLen < sizeof H ? nInLen : sizeof H);
			H.proto[sizeof H.proto - 1] = '\0';
			const char *n = PK_PROTO_NAME;
			unsigned i = 0;
			while (n[i] != 0 && H.proto[i] == n[i]) i++;
			if (n[i] != 0 || H.proto[i] != 0) { *pnStatus = EL_E_BADOP; return 1; }	// (another protocol's hello)
			struct pk_server S;
			ServerInfo (&S);
			memcpy (pOut, &S, sizeof S);
			*pnOutLen = sizeof S;
			*pnStatus = PK_PROTO_VERSION;
			return 1;
		}
	case PK_OP_SERVER:
		{
			struct pk_server S;
			ServerInfo (&S);
			memcpy (pOut, &S, sizeof S);
			*pnOutLen = sizeof S;
			*pnStatus = 1;
			return 1;
		}
	case PK_OP_SHELL: case PK_OP_EVENTS: case PK_OP_KEYS: case PK_OP_THUMB:
	case PK_OP_FRONT: case PK_OP_SPLIT: case PK_OP_DIM:
		*pnStatus = -KAPI_ENOSYS;			// (the shell's: phase P5)
		return 1;

	case EL_OP_DESK:					// one workspace
		if (g_pElWM->DeskInfo () >> 8 != 1) g_pElWM->SetDesk (0, 1);
		*pnStatus = g_pElWM->DeskInfo ();
		return 1;
	case EL_OP_WIN_DESK:					// (every window on the one desk)
		*pnStatus = (a[0] == 0 ? IdOfWin (nPid, nWin) : IdOfWinId ((unsigned) a[0])) >= 0 ? 0 : -3;
		return 1;

	case EL_OP_MOVE:					// a filled window stays at the work area's top left
		{
			int id = IdOfWin (nPid, nWin);
			if (pk_kind (id) != PK_KIND_FILL) return 0;
			*pnStatus = 1;
			return 1;
		}
	case EL_OP_WIN_MOVE:
		{
			int id = a[0] == 0 ? IdOfWin (nPid, nWin) : IdOfWinId ((unsigned) a[0]);
			if (pk_kind (id) != PK_KIND_FILL) return 0;
			*pnStatus = 0;
			return 1;
		}
	case EL_OP_RESIZABLE:					// resizable: the window fills the work area
		{
			int id = IdOfWin (nPid, nWin);
			int k = pk_kind (id);
			if (a[0] == 0 || id < 0 || (k != PK_KIND_CARD && k != PK_KIND_FILL)) return 0;
			CWindow *p = g_pElWin[id];
			if (p->Fixed ()) return 0;
			p->SetResizable (TRUE, (int) a[1], (int) a[2]);
			Fill (id);
			*pnStatus = 0;
			return 1;
		}
	}
	return 0;
}

// ---- the keys ------------------------------------------------------------------------------------------

// The programs in the z-order, the frontmost first (each once): -> how many.
static int Programs (unsigned *pPids, int nMax, boolean bMinimised)
{
	CWindow *List[WM_MAX_WINDOWS];
	unsigned n = g_pElWM->Snapshot (List, WM_MAX_WINDOWS);	// (bottom to top)
	int k = 0;
	for (unsigned j = n; j-- > 0; )
	{
		CWindow *p = List[j];
		if (!AppWindow (p) || (!bMinimised && p->Minimised ())) continue;
		boolean bSeen = FALSE;
		for (int i = 0; i < k; i++) if (pPids[i] == p->OwnerPid ()) bSeen = TRUE;
		if (!bSeen && k < nMax) pPids[k++] = p->OwnerPid ();
	}
	return k;
}

static void Front (unsigned nPid)				// a program's windows to the front (their order kept)
{
	CWindow *List[WM_MAX_WINDOWS];
	unsigned n = g_pElWM->Snapshot (List, WM_MAX_WINDOWS);
	for (unsigned j = 0; j < n; j++)
		if (AppWindow (List[j]) && List[j]->OwnerPid () == nPid) g_pElWM->Raise (List[j]);
	ScreenDirty ();
}

static int Key (const char *keys, unsigned mods)
{
	if (keys[0] != '\t' || keys[1] != '\0' || (mods & MOD_ALT) == 0 || (mods & MOD_CTRL) != 0) return 0;
	if (g_pElWM->FullscreenWindow () != 0) return 0;	// (a full-screen program's keys are its own)
	unsigned Pids[EL_WINDOWS_MAX];
	int n = Programs (Pids, EL_WINDOWS_MAX, TRUE);
	if (n < 2) return 1;
	Front ((mods & MOD_SHIFT) ? Pids[1] : Pids[n - 1]);	// Alt+Tab: the one at the back; Alt+Shift+Tab: the one just behind
	return 1;
}

// ---- each turn ------------------------------------------------------------------------------------------

static void Tick (unsigned self)
{
	g_nPkSelf = self;
	if (g_pElWM == 0) return;
	int ax, ay, aw, ah;
	Area (&ax, &ay, &aw, &ah);
	boolean bFull = g_pElWM->FullscreenWindow () != 0;
	for (int id = 0; id < EL_WINDOWS_MAX; id++)
	{
		CWindow *p = g_pElWin[id];
		int k = KindOf (id);					// (a window made since: classified)
		if (p == 0 || bFull || k != PK_KIND_FILL) continue;
		if (p->X () != ax || p->Y () != ay) { p->Move (ax, ay); ScreenDirty (); }
		// A resizable filled window: the work area's size, told once for each size of the work area (UIKit's Root
		// applies GUI_EVENT_WINRESIZE as a frame dragged). A program whose own pointer handler does not pass that
		// event on (the Terminal, the Media Player, the PDF Viewer, Screenshot: their frames do not resize on the
		// desktop either) is then asked to maximise, once (GUI_EVENT_WINCTL: Root::maximise fills the work area).
		int w = aw > p->MinClientW () ? aw : p->MinClientW (), h = ah > p->MinClientH () ? ah : p->MinClientH ();
		if (!p->Resizable () || p->PointerHandler () == 0) continue;
		boolean bFits = p->ClientWidth () == w && p->ClientHeight () == h;
		GUIEvent Ev;
		Ev.ulHandler = p->PointerHandler ();
		Ev.ulSender  = 0;
		if (s_St[id].nSentW != w || s_St[id].nSentH != h)
		{
			s_St[id].nSentW = w; s_St[id].nSentH = h; s_St[id].nSentAt = el_port_ticks ();
			if (bFits) continue;
			Ev.nEvent = GUI_EVENT_WINRESIZE;
			Ev.lValue = (long) (((u64) (u16) (s16) ax << 48) | ((u64) (u16) (s16) ay << 32) | ((u64) (u16) w << 16) | (u64) (u16) h);
			p->PushEvent (Ev);
			continue;
		}
		if (bFits || s_St[id].bMaxSent || el_port_ticks () - s_St[id].nSentAt < FILL_WAIT) continue;
		s_St[id].bMaxSent = TRUE;
		Ev.nEvent = GUI_EVENT_WINCTL;
		Ev.lValue = KAPI_FRAME_MAXIMISE;
		p->PushEvent (Ev);
	}

	// One app in front: the program of the topmost window (not minimised); the others' windows set aside.
	unsigned Pids[1];
	s_nFront = Programs (Pids, 1, FALSE) > 0 ? Pids[0] : 0;
	const char *pTitle = "";
	CWindow *List[WM_MAX_WINDOWS];
	unsigned n = g_pElWM->Snapshot (List, WM_MAX_WINDOWS);
	for (unsigned j = 0; j < n; j++)
	{
		CWindow *p = List[j];
		if (!AppWindow (p)) continue;
		boolean bAside = s_nFront != 0 && p->OwnerPid () != s_nFront && !bFull;
		if (p->Aside () != bAside) g_pElWM->SetAside (p, bAside);
		if (p->OwnerPid () == s_nFront && !p->Minimised ()) pTitle = p->Title ();	// (the front program's topmost window: the band's)
	}
	pk_band_update (pTitle);
}

// ---- the screen, the start ----------------------------------------------------------------------------------

static void Recentre (void)					// the cards in the middle of the work area again
{
	int ax, ay, aw, ah;
	Area (&ax, &ay, &aw, &ah);
	for (int id = 0; id < EL_WINDOWS_MAX; id++)
	{
		CWindow *p = g_pElWin[id];
		if (p == 0 || pk_kind (id) != PK_KIND_CARD) continue;
		int x = ax + (aw - p->OuterWidth ()) / 2, y = ay + (ah - p->OuterHeight ()) / 2;
		if (x < ax) x = ax;
		if (y < ay) y = ay;
		p->Move (x, y);
	}
	for (int id = 0; id < EL_WINDOWS_MAX; id++) s_St[id].nSentW = s_St[id].nSentH = -1;	// (the filled ones told again)
	ScreenDirty ();
}

static void Screen (int w, int)
{
	pk_band_make (g_nPkMode == PK_MODE_POCKET ? w : 0);
	Recentre ();
}

static void Start (int w, int, int restart)
{
	g_nPkSelf = (unsigned) kapi_getpid (0);
	g_pElWM->SetDesk (0, 1);					// one workspace
	if (!restart) el_core_wallpaper (0x00203050, 24, kapi_get_ticks () | 1);	// (the navy wallpaper behind the cards)
	pk_band_make (g_nPkMode == PK_MODE_POCKET ? w : 0);
	Recentre ();
}

void pk_main_registered (int restart);				// (main.cpp: the alias)

static const struct ws_policy s_Pocket =
{
	"pocketui",
	Create, Op, Key, Tick, Screen,
	pk_main_registered,
	Start,
	0, 0,							// (no demonstration)
};
const struct ws_policy *g_pWsPolicy = &s_Pocket;
