//
// wm.cpp -- PocketUI's policy (../common/policy.h): where the windows go and which one is shown, in the pocket and
// console modes (docs/POCKETUI-TECH-STUDY.md sections 4.3, 7.3; docs/COMPACT-SHELL-STUDY.md section 6). The window
// store, the compositor, the routing and the requests' decoding are the code PocketUI shares with Elegant
// (../common/); this file decides:
//
//   - the work area: the screen less the status band at the top (pocket: band.cpp; console: the whole screen);
//   - a program's MAIN window (its first) is full screen (the user's rule, 2026-10-08): FILLED -- frameless, at the work
//     area's top left (bigger than the work area: cut) --, or, of a fixed size smaller than the work area, CENTRED over
//     a matte of its background colour (PK_KIND_CENTRE); a window that says it is resizable (EL_OP_RESIZABLE) is
//     filled: its frame dropped, GUI_EVENT_WINRESIZE to the work area's size (UIKit's Root applies it as a frame
//     dragged to that size). A CARD -- framed (UIKit's skin draws its Milk title), centred -- is a program's other
//     window (a dialog, a second window) when it fits, and the main window of an app listed in SD:/etc/pocketui.ini
//     [cards] or whose app.txt says "pocket = card";
//   - a borderless or topmost window (a popup, a toast) stays where it asked, kept on the screen; the desktop's
//     backmost windows (the agenda, the stickies) and a topmost window on the screen's bottom edge (the dock, of
//     any width) are refused;
//   - THE SHELL (phase P5: pocketshell, uk_shell_register): its backmost window is its HOME (the launcher, at the work
//     area), its topmost / borderless ones its overlays and toasts (where asked); it gets the system keys it named
//     (PK_OP_KEYS; every key while it grabs them), the windows' news (PK_OP_EVENTS), the tasks and their pictures
//     (PK_OP_TASKS, PK_OP_THUMB), and sends a program to the front or goes HOME (PK_OP_FRONT: no app in front);
//   - the keys follow the front program (its topmost window; home: the shell's), whatever stands above it;
//   - THE GLOBAL MENU BAR (pocket; docs/COMPACT-SHELL-STUDY.md section 6.4): a topmost window across the screen's
//     top edge (the desktop's menubar) is the top band -- PocketUI's own band (band.cpp) is taken away while it is
//     there and made again when it goes; the work area starts under it; its menus (EL_OP_MENU_GET / _COMMAND) are
//     the FRONT app's (Elegant's "active window" skips borderless ones: a filled window is). Console: refused (P9);
//   - one workspace; cards and filled windows do not move (their title bar does not drag them, a program's
//     move request is ignored: CWindow::SetPinned);
//   - ONE APP IN FRONT, kept by the policy (not read back from the z-order): a program that opens a window, or
//     that is raised (EL_OP_WIN_RAISE, EL_OP_APP_RAISE -- `run` of a running app --, its status icon opened,
//     Alt+Tab) becomes the front one, its windows raised; every other program's windows are set aside (hidden
//     until brought to the front), and kept under the front one's whatever raised them. The front program gone
//     (or minimised): the one fronted before it. Alt+Tab brings the program at the back to the front (Alt+Shift+
//     Tab: the one just behind the front one) when no shell runs -- with one, the shell's switcher.
//
// Console (--mode console) is the same policy for now, without the band nor the menu bar (docs/POCKETUI-TECH-STUDY.md
// section 7.5).
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
// (A window is known by its pointer AND its id: a slot freed then given to a new window may get the same memory.)
static struct TState { CWindow *pWin; unsigned nWinId; int nKind; int nSentW, nSentH; unsigned nSentAt; boolean bMaxSent;
		       int nRx, nRy, nRw, nRh;			// (P8) the part of the work area a card was centred in
		       int nVx, nVy;				// (P6) the viewport: a window bigger than the work area, scrolled by
		       boolean bFocus; int nFx, nFy, nFw, nFh;	// ... its focused control (PK_OP_FOCUS_RECT), to show
		       int nHint, nUnits; } s_St[EL_WINDOWS_MAX];	// ... its focused field's type, its units (P10's)
#define FILL_WAIT	25		// ticks (0.25 s) a program has to apply GUI_EVENT_WINRESIZE before it is asked to maximise
static struct { unsigned nPid; int nWin, nKind; } s_Pending;	// (create's choice, for the window about to be made)
static unsigned s_nFront;					// the program in front (0: none)
static unsigned s_Mru[EL_WINDOWS_MAX];				// the programs, the most recently fronted first
static int s_nMru;
static unsigned s_nPromote;					// a program raised by a request: fronted at the next tick
static boolean s_bReady;					// (the first tick done: the windows a restart gave back are known)
static int s_nAreaX = -1, s_nAreaY, s_nAreaW, s_nAreaH;		// the work area at the last tick

// The shell (phase P5: pocketshell; uikit/port/pocket.h PK_OP_SHELL..): its pid, its events' handler, the system keys it
// takes, the grab (an overlay is up: every key its), home (no app in front: its home shown).
static unsigned s_nShell;
static u64 s_ulShellEv;
static int s_ShellKeys[32], s_nShellKeys = -1;			// (-1: it never said: the minimal Alt+Tab stays)
static boolean s_bGrab, s_bHome;
static unsigned s_nTaskSig;					// what the shell was last told of the tasks
static unsigned s_nMods;					// the modifiers held (Super too)
static boolean s_bSuperAlone;					// a Super pressed, no key since

// (P8) SPLIT VIEW: two programs share the work area -- the front one (it has the keys, the menu bar shows its menus)
// and the one beside it -- each on its half, the divider between them (docs/COMPACT-SHELL-STUDY.md section 6.7).
// Super+Left / Right: the front program to that half, the program fronted before it on the other; Super+Up: the front
// one whole again; Super+[ / ]: the divider at 40 / 50 / 60 %; Super+Tab or a click: the other half in front. The
// shell asks the same with PK_OP_SPLIT (its switcher's S). Only on a landscape work area of 640 px and more.
enum { SPLIT_MIN_W = 640, SPLIT_GAP = 4 };
static unsigned s_nSide;					// the program on the other half (0: one program at a time)
static int s_nSplit = 50;					// the left half's share of the work area, in %
static boolean s_bFrontLeft = TRUE;				// the front program has the left half
static int s_nDivider = -1;					// the divider's window (PocketUI's own), -1: none

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

static boolean SplitOn (void)					// two programs share the work area now
{
	if (s_nSide == 0 || s_nFront == 0 || s_nSide == s_nFront || s_bHome || g_pElWM == 0 || g_pElWM->FullscreenWindow () != 0) return FALSE;
	int x, y, w, h;
	Area (&x, &y, &w, &h);
	return w >= SPLIT_MIN_W && w > h;
}
int pk_split (void) { return SplitOn () ? 1 : 0; }

// The part of the work area a program's windows have: the whole of it, or its half in split view.
static void AreaOf (unsigned nPid, int *x, int *y, int *w, int *h)
{
	Area (x, y, w, h);
	if (!SplitOn () || (nPid != s_nFront && nPid != s_nSide)) return;
	int lw = *w * s_nSplit / 100 - SPLIT_GAP / 2;
	if ((nPid == s_nFront) == (s_bFrontLeft != FALSE)) *w = lw;
	else { *x += lw + SPLIT_GAP; *w -= lw + SPLIT_GAP; }
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
static void Promote (unsigned nPid);
static boolean Shown (unsigned nPid);
static void Front (unsigned nPid);
static boolean AppWindow (CWindow *p);
static void Promote (unsigned nPid);
static CWindow *pk_front_window (void);
static void Recentre (void);
static void Matte (CWindow *pFront, int ax, int ay, int aw, int ah);
static void Viewport (int id, CWindow *p, int ax, int ay, int aw, int ah, int *pvx, int *pvy);
static void Bars (int ax, int ay, int aw, int ah);
static void Divider (boolean bOn, int ax, int ay, int aw, int ah);

// A window's state, up to date: one the policy had not seen (made since the last turn, given back to a server
// started again) is classified now.
static int KindOf (int id)
{
	if (id < 0 || id >= EL_WINDOWS_MAX) return PK_KIND_NONE;
	CWindow *p = g_pElWin[id];
	if (p != s_St[id].pWin || (p != 0 && p->Id () != s_St[id].nWinId))
	{
		s_St[id].pWin = p; s_St[id].nWinId = p != 0 ? p->Id () : 0;
		s_St[id].nSentW = s_St[id].nSentH = -1; s_St[id].bMaxSent = FALSE;
		s_St[id].nVx = s_St[id].nVy = 0; s_St[id].bFocus = FALSE; s_St[id].nHint = -1; s_St[id].nUnits = 0;
		s_St[id].nKind = p != 0 ? Classify (id, p) : PK_KIND_NONE;
		if (p != 0) p->SetPinned (s_St[id].nKind == PK_KIND_CARD || s_St[id].nKind == PK_KIND_FILL || s_St[id].nKind == PK_KIND_CENTRE);
		if (p != 0) p->SetNoInset (s_St[id].nKind == PK_KIND_SHELL || s_St[id].nKind == PK_KIND_HOME);	// (not a band)
		if (s_St[id].nKind == PK_KIND_FILL && p->Resizable ()) Fill (id);
		if (p != 0 && s_bReady && AppWindow (p)) Promote (p->OwnerPid ());	// (a program that opens a window: in front)
	}
	return s_St[id].nKind;
}

int pk_kind (int id)		{ return KindOf (id); }
unsigned pk_front_pid (void)	{ return s_nFront; }
unsigned pk_shell_pid (void)	{ return s_nShell; }
int pk_home (void)		{ return s_bHome ? 1 : 0; }

// The shell still runs (its pid has a process).
static boolean ShellAlive (void)
{
	char n[24];
	return s_nShell != 0 && el_sys_name (s_nShell, n, sizeof n) > 0;
}

// An event to the shell's handler, through one of its windows (its first if it has it) -> sent.
static boolean ShellPush (int nEvent, long lValue)
{
	if (s_nShell == 0 || s_ulShellEv == 0) return FALSE;
	int id = el_core_window_of_win (s_nShell, 0);
	if (id < 0) id = el_core_window_of (s_nShell);
	if (id < 0 || g_pElWin[id] == 0) return FALSE;
	GUIEvent Ev;
	Ev.ulHandler = s_ulShellEv; Ev.ulSender = 0; Ev.nEvent = nEvent; Ev.lValue = lValue; Ev.nMods = s_nMods & 7;
	g_pElWin[id]->PushEvent (Ev);
	return TRUE;
}

// A program's window, normal (not the band, not a toast, not the desktop's): what "one app in front" counts.
static boolean AppWindow (CWindow *p)
{
	return p != 0 && p->OwnerPid () != 0 && p->OwnerPid () != g_nPkSelf && !p->System () && !p->Topmost () && !p->Backmost ()
	    && !(p->OwnerPid () == s_nShell && s_nShell != 0 && p->Borderless ());	// (the shell's home, overlays, toasts)
}

// The global menu bar's window (its number), -1: none.
int pk_bar_id (void)
{
	for (int id = 0; id < EL_WINDOWS_MAX; id++)
		if (g_pElWin[id] != 0 && s_St[id].pWin == g_pElWin[id] && s_St[id].nKind == PK_KIND_BAR) return id;
	for (int id = 0; id < EL_WINDOWS_MAX; id++)		// (made in this turn: not classified yet)
		if (g_pElWin[id] != 0 && s_St[id].pWin != g_pElWin[id] && KindOf (id) == PK_KIND_BAR) return id;
	return -1;
}

// ---- the apps shown as cards (SD:/etc/pocketui.ini [cards], app.txt "pocket = card") ------------------------
// A small file read with the kernel's calls (PocketUI has no C library): -> its bytes in b (cap - 1 at most), -1.
static int ReadFile (const char *path, char *b, int cap)
{
	void *h = kapi_open (path);
	if (h == 0) return -1;
	int n = kapi_read (h, b, (unsigned) (cap - 1));
	kapi_close (h);
	if (n < 0) return -1;
	b[n] = 0;
	return n;
}

static char Low (char c)	{ return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c; }

// Does the text's [section] (0: anywhere) hold a line "name" (sec "cards"), or "key = value" (key given)?
static boolean IniHas (const char *t, const char *sec, const char *key, const char *val)
{
	boolean bIn = sec == 0;
	for (const char *p = t; *p; )
	{
		const char *e = p; while (*e && *e != '\n') e++;
		const char *a = p, *z = e;
		while (a < z && (*a == ' ' || *a == '\t')) a++;
		while (z > a && (z[-1] == ' ' || z[-1] == '\t' || z[-1] == '\r')) z--;
		const char *c = a; while (c < z && *c != '#' && *c != ';') c++;	// (a comment)
		while (c > a && (c[-1] == ' ' || c[-1] == '\t')) c--;
		z = c;
		if (a < z && *a == '[')
		{
			const char *q = a + 1; int i = 0;
			while (q < z && *q != ']' && sec != 0 && sec[i] && Low (*q) == Low (sec[i])) { q++; i++; }
			bIn = sec != 0 && sec[i] == 0 && q < z && *q == ']';
		}
		else if (bIn && a < z)
		{
			const char *w = key != 0 ? key : val;		// (the line's first word: the key, or the name)
			int i = 0; const char *q = a;
			while (q < z && w[i] && Low (*q) == Low (w[i])) { q++; i++; }
			if (w[i] == 0 && (q == z || *q == ' ' || *q == '\t' || *q == '='))
			{
				if (key == 0) { if (q == z) return TRUE; }
				else
				{
					while (q < z && (*q == ' ' || *q == '\t' || *q == '=')) q++;
					int j = 0;
					while (q < z && val[j] && Low (*q) == Low (val[j])) { q++; j++; }
					if (val[j] == 0 && q == z) return TRUE;
				}
			}
		}
		p = *e ? e + 1 : e;
	}
	return FALSE;
}

int pk_card_app (const char *name)
{
	static char b[2048];
	if (name == 0 || name[0] == 0) return 0;
	if (ReadFile (PK_INI, b, sizeof b) > 0 && IniHas (b, "cards", 0, name)) return 1;
	char path[96]; int n = 0;
	const char *pre = "SD:/apps/", *post = ".app/app.txt";
	for (int i = 0; pre[i] && n < 90; i++) path[n++] = pre[i];
	for (int i = 0; name[i] && n < 70; i++) path[n++] = name[i];
	for (int i = 0; post[i] && n < 95; i++) path[n++] = post[i];
	path[n] = 0;
	return ReadFile (path, b, sizeof b) > 0 && IniHas (b, 0, "pocket", "card") ? 1 : 0;
}

static boolean HasAppWindow (unsigned nPid)			// the program has a window already (its main one)
{
	for (int id = 0; id < EL_WINDOWS_MAX; id++)
		if (g_pElWin[id] != 0 && g_pElWin[id]->OwnerPid () == nPid && AppWindow (g_pElWin[id])) return TRUE;
	return FALSE;
}

// ---- a window made ------------------------------------------------------------------------------------

static int Create (unsigned nPid, int nWin, int *px, int *py, int *pw, int *ph, unsigned *pnFlags)
{
	unsigned f = *pnFlags;
	int nKind;
	for (int id = 0; id < EL_WINDOWS_MAX; id++) KindOf (id);	// (a window made earlier in this turn: its kind taken now)
	if (nPid == s_nShell && s_nShell != 0 && (f & (WIN_FLAG_BACKMOST | WIN_FLAG_TOPMOST | WIN_FLAG_BORDERLESS)))
	{
		if (f & WIN_FLAG_BACKMOST)				// the shell's home: at the work area, frameless
		{
			int ax, ay, aw, ah;
			Area (&ax, &ay, &aw, &ah);
			*px = ax; *py = ay;
			*pnFlags = f | WIN_FLAG_BORDERLESS;
			nKind = PK_KIND_HOME;
		}
		else nKind = PK_KIND_SHELL;				// its overlays, its toasts: where it asks
		s_Pending.nPid = nPid; s_Pending.nWin = nWin; s_Pending.nKind = nKind;
		return 2;
	}
	if (f & WIN_FLAG_BACKMOST)
	{
		Say ("a desktop's backmost window refused (the pocket shell's are its own)");
		return 0;
	}
	if (f & WIN_FLAG_TOPMOST)
	{
		// The desktop's bands: on the screen's top edge (the menu bar), on its bottom edge (the dock -- whatever its
		// width: it is narrower than half a wide screen).
		boolean bTop = *py == 0, bBottom = *py > 0 && *py + *ph >= g_nScreenHeight;
		if (bTop && g_nPkMode == PK_MODE_POCKET && *px == 0 && *pw >= g_nScreenWidth && pk_bar_id () < 0)
		{
			Say ("the global menu bar: the top band (PocketUI's own band set aside)");
			s_Pending.nPid = nPid; s_Pending.nWin = nWin; s_Pending.nKind = PK_KIND_BAR;
			return 1;
		}
		if (bTop || bBottom)
		{
			Say ("a desktop's band refused (", bTop ? (g_nPkMode == PK_MODE_POCKET ? "the screen's top edge: a menu bar is there already" : "the screen's top edge: no menu bar in console")
							     : "the screen's bottom edge: the dock", ")");
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
		// A program's MAIN window (its first) is full screen -- filled, frameless; of a fixed size smaller than the work
		// area: centred over a matte --, but for the apps listed as cards (SD:/etc/pocketui.ini, app.txt "pocket =
		// card"); its other windows (a dialog, an about box, a second window) are cards when they fit.
		int ow = *pw + 2 * WIN_BORDER, oh = *ph + WIN_TITLEBAR_H + WIN_BORDER;
		boolean bMain = nWin == 0 && !HasAppWindow (nPid);
		char Name[32];
		if (bMain && el_sys_name (nPid, Name, sizeof Name) > 0 && pk_card_app (Name)) bMain = FALSE;
		if (!bMain && ow <= aw && oh <= ah)			// a card: framed, centred
		{
			*px = ax + (aw - ow) / 2; *py = ay + (ah - oh) / 2;
			nKind = PK_KIND_CARD;
		}
		else if (bMain && *pw < aw && *ph < ah && !(f & WIN_FLAG_ALPHA))	// fixed (or not yet resizable): centred
		{
			*pnFlags = f | WIN_FLAG_BORDERLESS;
			*px = ax + (aw - *pw) / 2; *py = ay + (ah - *ph) / 2;
			nKind = PK_KIND_CENTRE;
		}
		else							// filled, frameless (cut when bigger)
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
	if (nPid == s_nShell && s_nShell != 0 && (p->Backmost () || p->Topmost () || p->Borderless ()) && s_Pending.nPid != nPid)
		return p->Backmost () ? PK_KIND_HOME : PK_KIND_SHELL;	// (given back to a server started again)
	if (s_Pending.nPid == nPid && s_Pending.nWin == el_core_window_win (id) && s_Pending.nKind != PK_KIND_NONE)
	{
		int k = s_Pending.nKind;
		s_Pending.nKind = PK_KIND_NONE;
		return k;
	}
	int ax, ay, aw, ah;
	Area (&ax, &ay, &aw, &ah);
	if (p->Topmost () && p->X () == 0 && p->Y () == 0 && p->ClientWidth () >= g_nScreenWidth && g_nPkMode == PK_MODE_POCKET)
		return PK_KIND_BAR;					// (the menu bar, given back to a server started again)
	if (p->Topmost () || p->Backmost ()) return PK_KIND_POPUP;
	if (p->Borderless () && !p->Resizable () && !p->AlphaCanvas () && p->X () == ax + (aw - p->OuterWidth ()) / 2
	    && p->Y () == ay + (ah - p->OuterHeight ()) / 2 && (p->X () != ax || p->Y () != ay)) return PK_KIND_CENTRE;
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
	p->SetPinned (TRUE);
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

// What a program is told: in split view its own half as its work area, and "narrow" when the half is a slim one
// (a rail of icons would be a waste there: UIKit's panels become drawers).
static void ServerInfoFor (unsigned nPid, struct pk_server *s)
{
	ServerInfo (s);
	if (!SplitOn () || (nPid != s_nFront && nPid != s_nSide)) return;
	AreaOf (nPid, &s->work_x, &s->work_y, &s->work_w, &s->work_h);
	if (g_nPkMode != PK_MODE_CONSOLE) s->size_class = s->work_w < 480 ? UK_SC_NARROW : UK_SC_COMPACT;
}

static int IdOfWin (unsigned nPid, int nWin)		{ return el_core_window_of_win (nPid, nWin); }

static int IdOfWinId (unsigned nId)			// (a window's id, as uk_win_list gives it)
{
	for (int i = 0; i < EL_WINDOWS_MAX; i++)
		if (g_pElWin[i] != 0 && g_pElWin[i]->Id () == nId) return i;
	return -1;
}

static int Programs (unsigned *pPids, int nMax, boolean bMinimised);
static void Front (unsigned nPid);

// ---- the shell's operations (uikit/port/pocket.h) -----------------------------------------------------------

// Window id's client area scaled to w x h (a box filter) into the caller's transfer buffer -> 1, 0 no such window, -1.
static long Thumb (unsigned nPid, unsigned nId, int w, int h)
{
	int id = IdOfWinId (nId);
	if (w <= 0 || h <= 0 || w > 512 || h > 512) return -1;
	if (id < 0) return 0;
	CWindow *p = g_pElWin[id];
	const u32 *pSrc = p->CanvasBuffer ();
	int W = p->ClientWidth (), H = p->ClientHeight (), nPitch = (int) p->Canvas ()->Width ();
	if (pSrc == 0 || W <= 0 || H <= 0) return 0;
	unsigned *pDst = el_core_xfer (nPid, (unsigned) (w * h * 4));
	if (pDst == 0) return -1;
	for (int y = 0; y < h; y++)
	{
		int y0 = y * H / h, y1 = (y + 1) * H / h;
		if (y1 <= y0) y1 = y0 + 1;
		for (int x = 0; x < w; x++)
		{
			int x0 = x * W / w, x1 = (x + 1) * W / w;
			if (x1 <= x0) x1 = x0 + 1;
			int sx = (x1 - x0 + 3) / 4, sy = (y1 - y0 + 3) / 4;	// (at most 4 x 4 samples a pixel)
			unsigned r = 0, g = 0, b = 0, n = 0;
			for (int yy = y0; yy < y1; yy += sy)
				for (int xx = x0; xx < x1; xx += sx)
				{
					u32 c = pSrc[yy * nPitch + xx];
					r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++;
				}
			pDst[y * w + x] = ((r / n) << 16) | ((g / n) << 8) | (b / n);
		}
	}
	return 1;
}

// The running programs, the most recently fronted first (each its topmost window) -> how many.
static int Tasks (struct uk_shell_task *pOut, int nMax)
{
	unsigned Pids[EL_WINDOWS_MAX];
	int n = 0;
	for (int i = 0; i < s_nMru && n < EL_WINDOWS_MAX; i++) Pids[n++] = s_Mru[i];
	unsigned More[EL_WINDOWS_MAX];
	int m = Programs (More, EL_WINDOWS_MAX, TRUE);
	for (int j = 0; j < m; j++)
	{
		boolean bSeen = FALSE;
		for (int i = 0; i < n; i++) if (Pids[i] == More[j]) bSeen = TRUE;
		if (!bSeen && n < EL_WINDOWS_MAX) Pids[n++] = More[j];
	}
	CWindow *List[WM_MAX_WINDOWS];
	unsigned nw = g_pElWM->Snapshot (List, WM_MAX_WINDOWS);
	int k = 0;
	for (int i = 0; i < n && k < nMax; i++)
	{
		CWindow *pTop = 0, *pAny = 0;
		for (unsigned j = nw; j-- > 0; )
		{
			if (!AppWindow (List[j]) || List[j]->OwnerPid () != Pids[i]) continue;
			if (pAny == 0) pAny = List[j];
			if (pTop == 0 && !List[j]->Minimised ()) pTop = List[j];
		}
		if (pTop == 0) pTop = pAny;
		if (pTop == 0) continue;
		struct uk_shell_task *t = &pOut[k++];
		memset (t, 0, sizeof *t);
		t->id = pTop->Id (); t->pid = Pids[i];
		t->w = pTop->ClientWidth (); t->h = pTop->ClientHeight ();
		const char *ti = pTop->Title ();
		for (unsigned c = 0; ti != 0 && ti[c] != 0 && c + 1 < sizeof t->title; c++) t->title[c] = ti[c];
		el_sys_name (Pids[i], t->name, sizeof t->name);
		t->name[sizeof t->name - 1] = 0;
		int idw = IdOfWinId (pTop->Id ());
		if (Pids[i] == s_nFront && !s_bHome) t->flags |= UK_TASK_FRONT;
		if (idw >= 0 && s_St[idw].nKind == PK_KIND_CARD) t->flags |= UK_TASK_CARD;
		if (pTop->Minimised ()) t->flags |= UK_TASK_MINIMISED;
	}
	return k;
}

// Home: no app in front, every one set aside -- the shell's home shown, with the keys.
static void GoHome (void)
{
	if (!s_bHome) Say ("home");
	s_bHome = TRUE;
	s_nFront = 0;
	ScreenDirty ();
}

static long ShellOp (int nOp, const long *a, const unsigned char *pIn, unsigned nInLen)
{
	switch (nOp)
	{
	case PK_OP_EVENTS:
		s_ulShellEv = (u64) a[0];
		s_nTaskSig = 0;					// (told at the next tick)
		return 1;
	case PK_OP_KEYS:
		{
			int n = (int) a[0];
			if (n < 0) n = 0;
			if (n > 32) n = 32;
			if ((unsigned) n * sizeof (int) > nInLen) n = (int) (nInLen / sizeof (int));
			memcpy (s_ShellKeys, pIn, (unsigned) n * sizeof (int));
			s_nShellKeys = n;
			return n;
		}
	case PK_OP_GRAB:
		s_bGrab = a[0] != 0;
		return 1;
	case PK_OP_FRONT:
		{
			if (a[0] == 0) { if (a[1] == 0) GoHome (); return 1; }
			int id = IdOfWinId ((unsigned) a[0]);
			if (id < 0 || !AppWindow (g_pElWin[id])) return 0;
			unsigned nPid = g_pElWin[id]->OwnerPid ();
			if (a[1] != 0)
			{
				if (g_pElWin[id]->Minimised ()) g_pElWM->Raise (g_pElWin[id]);	// (back from minimised)
				s_nPromote = nPid;
				return 1;
			}
			int i = 0;					// behind the others: last of the recent ones
			while (i < s_nMru && s_Mru[i] != nPid) i++;
			if (i == s_nMru) return 1;
			for (; i + 1 < s_nMru; i++) s_Mru[i] = s_Mru[i + 1];
			s_Mru[s_nMru - 1] = nPid;
			if (nPid == s_nFront) s_nFront = 0;
			return 1;
		}
	}
	return -KAPI_EINVAL;
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
			ServerInfoFor (nPid, &S);
			memcpy (pOut, &S, sizeof S);
			*pnOutLen = sizeof S;
			*pnStatus = 1;
			return 1;
		}
	case PK_OP_SHELL:					// "I am the shell": the first, or any once that one ended
		if (s_nShell != 0 && s_nShell != nPid && ShellAlive ()) { *pnStatus = -KAPI_EBUSY; return 1; }
		if (s_nShell != nPid)
		{
			char Name[32];
			if (el_sys_name (nPid, Name, sizeof Name) <= 0) { Name[0] = '?'; Name[1] = 0; }
			Say ("the shell: ", Name);
			s_ulShellEv = 0; s_nShellKeys = -1; s_bGrab = FALSE; s_nTaskSig = 0;
		}
		s_nShell = nPid;
		for (int id = 0; id < EL_WINDOWS_MAX; id++)	// (its windows given back before it said so: taken again)
			if (g_pElWin[id] != 0 && g_pElWin[id]->OwnerPid () == nPid) s_St[id].pWin = 0;
		*pnStatus = 1;
		return 1;
	case PK_OP_EVENTS: case PK_OP_KEYS: case PK_OP_FRONT: case PK_OP_GRAB:
		if (nPid != s_nShell || s_nShell == 0) { *pnStatus = -KAPI_EPERM; return 1; }
		*pnStatus = ShellOp (nOp, a, pIn, nInLen);
		return 1;
	case PK_OP_THUMB:
		*pnStatus = Thumb (nPid, (unsigned) a[0], (int) a[1], (int) a[2]);
		return 1;
	case PK_OP_TASKS:
		{
			int nMax = (int) a[0];
			if (nMax > UK_TASKS_MAX) nMax = UK_TASKS_MAX;
			if (nMax * (int) sizeof (struct uk_shell_task) > KAPI_WS_DATA_MAX) nMax = KAPI_WS_DATA_MAX / (int) sizeof (struct uk_shell_task);
			int n = Tasks ((struct uk_shell_task *) pOut, nMax);
			*pnOutLen = (unsigned) n * sizeof (struct uk_shell_task);
			*pnStatus = n;
			return 1;
		}
	case PK_OP_SPLIT:					// (P8) a = the left window's id, the right one's; 0, 0: no split
		{
			if (nPid != s_nShell || s_nShell == 0) { *pnStatus = -KAPI_EINVAL; return 1; }
			if (a[0] == 0 && a[1] == 0) { s_nSide = 0; ScreenDirty (); *pnStatus = 1; return 1; }
			int l = IdOfWinId ((unsigned) a[0]), r = IdOfWinId ((unsigned) a[1]);
			if (l < 0 || r < 0 || !AppWindow (g_pElWin[l]) || !AppWindow (g_pElWin[r])
			    || g_pElWin[l]->OwnerPid () == g_pElWin[r]->OwnerPid ()) { *pnStatus = 0; return 1; }
			if (g_pElWin[l]->Minimised ()) g_pElWM->Raise (g_pElWin[l]);
			if (g_pElWin[r]->Minimised ()) g_pElWM->Raise (g_pElWin[r]);
			unsigned nL = g_pElWin[l]->OwnerPid (), nR = g_pElWin[r]->OwnerPid ();
			s_nSide = 0;
			Promote (nL);
			s_nSide = nR; s_bFrontLeft = TRUE;
			Front (nR); Front (nL);
			*pnStatus = SplitOn () ? 1 : 0;		// (0: the work area is too small, or upright)
			if (!*pnStatus) s_nSide = 0;
			return 1;
		}
	case PK_OP_DIM:
		*pnStatus = -KAPI_ENOSYS;			// (the overlays draw their own dim)
		return 1;

	// (P6) the adaptive layer's: the focused control (the viewport shows it), the focused field's type, the units
	case PK_OP_FOCUS_RECT: case PK_OP_TEXT_HINT: case PK_OP_UNITS:
		{
			int id = IdOfWin (nPid, nWin);
			if (id < 0) { *pnStatus = 0; return 1; }
			KindOf (id);
			if (nOp == PK_OP_FOCUS_RECT)
			{
				s_St[id].nFx = (int) a[0]; s_St[id].nFy = (int) a[1]; s_St[id].nFw = (int) a[2]; s_St[id].nFh = (int) a[3];
				s_St[id].bFocus = TRUE;
			}
			else if (nOp == PK_OP_TEXT_HINT) s_St[id].nHint = (int) a[0];
			else s_St[id].nUnits = a[0] != 0;
			*pnStatus = 1;
			return 1;
		}

	case EL_OP_DESK:					// one workspace
		if (((g_pElWM->DeskInfo () >> 8) & 0xFF) != 1) g_pElWM->SetDesk (0, 1);
		*pnStatus = g_pElWM->DeskInfo ();
		return 1;
	case EL_OP_WIN_DESK:					// (every window on the one desk)
		*pnStatus = (a[0] == 0 ? IdOfWin (nPid, nWin) : IdOfWinId ((unsigned) a[0])) >= 0 ? 0 : -3;
		return 1;

	case EL_OP_MOVE:					// a filled window stays at the work area's top left, a card centred
		{
			int k = pk_kind (IdOfWin (nPid, nWin));
			if (k != PK_KIND_FILL && k != PK_KIND_CARD && k != PK_KIND_BAR && k != PK_KIND_CENTRE) return 0;
			*pnStatus = 1;
			return 1;
		}
	case EL_OP_WIN_MOVE:
		{
			int k = pk_kind (a[0] == 0 ? IdOfWin (nPid, nWin) : IdOfWinId ((unsigned) a[0]));
			if (k != PK_KIND_FILL && k != PK_KIND_CARD && k != PK_KIND_BAR && k != PK_KIND_CENTRE) return 0;
			*pnStatus = 0;
			return 1;
		}

	// A program raised: the front one from the next tick (the common code raises its window meanwhile).
	case EL_OP_WIN_RAISE:
		{
			int id = a[0] == 0 ? IdOfWin (nPid, nWin) : IdOfWinId ((unsigned) a[0]);
			if (id >= 0 && AppWindow (g_pElWin[id])) s_nPromote = g_pElWin[id]->OwnerPid ();
			return 0;
		}
	case EL_OP_APP_RAISE:					// (by its name: `run` of a running app, the menu bar's...)
		{
			if (nInLen == 0 || nInLen > 63) return 0;
			char Name[64], Got[64];
			memcpy (Name, pIn, nInLen); Name[nInLen] = '\0';
			for (int id = 0; id < EL_WINDOWS_MAX; id++)
			{
				CWindow *p = g_pElWin[id];
				if (!AppWindow (p) || el_sys_name (p->OwnerPid (), Got, sizeof Got) <= 0) continue;
				unsigned i = 0;
				while (Name[i] != 0 && Got[i] == Name[i]) i++;
				if (Name[i] == 0 && Got[i] == 0) { s_nPromote = p->OwnerPid (); break; }
			}
			return 0;
		}
	case EL_OP_TRAY_ACTIVATE:				// (its status icon opened: its window shown)
		if (a[1] == KAPI_TRAY_OPEN && a[0] != 0) s_nPromote = (unsigned) a[0];
		return 0;

	// The global menu bar's menus: the FRONT app's (its topmost window; a filled one is borderless, which
	// Elegant's active window skips).
	case EL_OP_MENU_GET:
		{
			if (s_bHome) { *pnOutLen = 0; *pnStatus = 0; return 1; }	// (home: no app's menus -- the Onyx menu)
			CWindow *pF = pk_front_window ();
			if (pF == 0) return 0;
			struct el_menu *pM = (struct el_menu *) pOut;
			memset (pM, 0, sizeof *pM);
			unsigned nSerial = g_pElWM->GetActiveMenu (pM->spec, sizeof pM->spec, pM->title, sizeof pM->title, pF);
			pM->serial = nSerial;
			unsigned n = 0;
			while (n + 1 < sizeof pM->spec && pM->spec[n] != 0) n++;
			*pnOutLen = (unsigned) (sizeof *pM - sizeof pM->spec) + n + 1;
			*pnStatus = (long) nSerial;
			return 1;
		}
	case EL_OP_MENU_COMMAND:
		{
			CWindow *pF = pk_front_window ();
			if (pF == 0) return 0;
			*pnStatus = g_pElWM->SendMenuCommand ((int) a[0], pF) ? 1 : 0;
			return 1;
		}
	case EL_OP_RESIZABLE:					// resizable: the window fills the work area
		{
			int id = IdOfWin (nPid, nWin);
			int k = pk_kind (id);
			if (a[0] == 0 || id < 0 || (k != PK_KIND_CARD && k != PK_KIND_FILL && k != PK_KIND_CENTRE)) return 0;
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

// ---- the program in front ----------------------------------------------------------------------------

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

static boolean Shown (unsigned nPid)				// a program has a window that is not minimised
{
	for (int id = 0; id < EL_WINDOWS_MAX; id++)
	{
		CWindow *p = g_pElWin[id];
		if (AppWindow (p) && p->OwnerPid () == nPid && !p->Minimised ()) return TRUE;
	}
	return FALSE;
}

static void Front (unsigned nPid)				// a program's windows to the front (their order kept)
{
	CWindow *List[WM_MAX_WINDOWS];
	unsigned n = g_pElWM->Snapshot (List, WM_MAX_WINDOWS);
	for (unsigned j = 0; j < n; j++)
		if (AppWindow (List[j]) && List[j]->OwnerPid () == nPid) g_pElWM->Raise (List[j]);
	ScreenDirty ();
}

// A program becomes the one in front: first of the recent ones, its windows raised (and shown: the others set aside
// at the tick).
static void Promote (unsigned nPid)
{
	if (nPid == 0) return;
	if (s_nSide == nPid)					// (P8) the other half's program: the two swap their roles
	{
		unsigned nWas = s_nFront != 0 ? s_nFront : (s_nMru > 0 ? s_Mru[0] : 0);
		s_nSide = nWas != nPid ? nWas : 0;
		s_bFrontLeft = !s_bFrontLeft;
	}
	int i = 0;
	while (i < s_nMru && s_Mru[i] != nPid) i++;
	if (i == s_nMru && s_nMru < EL_WINDOWS_MAX) s_nMru++;
	if (i == EL_WINDOWS_MAX) i = EL_WINDOWS_MAX - 1;
	for (; i > 0; i--) s_Mru[i] = s_Mru[i - 1];
	s_Mru[0] = nPid;
	s_bHome = FALSE;
	if (nPid != s_nFront)						// (the log says who is in front: the Pi's kmsg)
	{
		char Name[32];
		if (el_sys_name (nPid, Name, sizeof Name) <= 0) { Name[0] = '?'; Name[1] = 0; }
		Say ("in front: ", Name);
	}
	s_nFront = nPid;
	Front (nPid);
}

// The front program's topmost window (not minimised): the menu bar's menus, the band's title.
static CWindow *pk_front_window (void)
{
	if (s_nFront == 0 || g_pElWM == 0) return 0;
	CWindow *List[WM_MAX_WINDOWS];
	unsigned n = g_pElWM->Snapshot (List, WM_MAX_WINDOWS);
	for (unsigned j = n; j-- > 0; )
		if (AppWindow (List[j]) && List[j]->OwnerPid () == s_nFront && !List[j]->Minimised ()) return List[j];
	return 0;
}

// The front program, up to date: the one fronted last that still shows a window (the others dropped from the
// recent ones once they have no window at all), else the topmost one (the windows a restart gave back).
static void FrontNow (void)
{
	int k = 0;
	for (int i = 0; i < s_nMru; i++)
	{
		boolean bAny = FALSE;
		for (int id = 0; id < EL_WINDOWS_MAX && !bAny; id++)
			bAny = AppWindow (g_pElWin[id]) && g_pElWin[id]->OwnerPid () == s_Mru[i];
		if (bAny) s_Mru[k++] = s_Mru[i];
	}
	s_nMru = k;
	if (s_bHome) { s_nFront = 0; return; }			// (home: no app in front until one is raised)
	for (int i = 0; i < s_nMru; i++)
		if (Shown (s_Mru[i]))
		{
			if (s_Mru[i] != s_nFront) Promote (s_Mru[i]);
			return;
		}
	unsigned Pids[1];
	if (Programs (Pids, 1, FALSE) > 0) Promote (Pids[0]);
	else { s_nFront = 0; if (s_nShell != 0 && !s_bHome) GoHome (); }	// (no app shown: the shell's home)
}

// ---- the keys ------------------------------------------------------------------------------------------

// The shell's keys (PK_OP_KEYS): each key of the string -> 1 when the shell took the string (grabbed: every key).
static int ShellKey (const char *keys, unsigned mods)
{
	if (s_nShell == 0 || s_ulShellEv == 0 || !ShellAlive ()) return -1;	// (no shell: the minimal Alt+Tab)
	if (g_pElWM->FullscreenWindow () != 0) return 0;	// (a full-screen program's keys are its own)
	if (mods & KAPI_WS_MOD_SUPER) s_bSuperAlone = FALSE;	// (Super+key: not Super alone)
	const char *p = keys;
	unsigned m = 0;
	int code = el_core_next_key (&p, &m);
	if (code == 0) return 0;
	if (s_bGrab)						// an overlay is up: every key the shell's
	{
		for (p = keys; m = 0, (code = el_core_next_key (&p, &m)) != 0; )
			ShellPush (UK_SHELL_KEYEV, UK_SHELL_KEY ((mods | m) & 15, code));
		return 1;
	}
	if (*p != 0) return 0;					// (a string of several keys: typed, the app's)
	unsigned k = (unsigned) UK_SHELL_KEY ((mods | m) & 15, code);
	if (code >= 'A' && code <= 'Z' && ((mods | m) & (MOD_ALT | MOD_CTRL | KAPI_WS_MOD_SUPER)))	// (Super+N as Super+n)
		k = (unsigned) UK_SHELL_KEY (((mods | m) & 15) & ~MOD_SHIFT, code + 32);
	for (int i = 0; i < s_nShellKeys; i++)
		if ((unsigned) s_ShellKeys[i] == k) { ShellPush (UK_SHELL_KEYEV, (long) k); return 1; }
	return s_nShellKeys < 0 ? -1 : 0;
}

static void Mods (unsigned mods)
{
	unsigned nOld = s_nMods;
	s_nMods = mods;
	if (s_nShell == 0 || s_ulShellEv == 0) return;
	if ((mods & KAPI_WS_MOD_SUPER) && !(nOld & KAPI_WS_MOD_SUPER)) s_bSuperAlone = (mods & 7) == 0;
	else if (mods & 7) s_bSuperAlone = FALSE;
	if (!(mods & KAPI_WS_MOD_SUPER) && (nOld & KAPI_WS_MOD_SUPER) && s_bSuperAlone)
	{
		s_bSuperAlone = FALSE;
		boolean bWanted = s_bGrab;
		for (int i = 0; i < s_nShellKeys && !bWanted; i++) bWanted = s_ShellKeys[i] == UK_SHELL_KEY (0, UK_SHELL_KEY_SUPER);
		if (bWanted && g_pElWM->FullscreenWindow () == 0) ShellPush (UK_SHELL_KEYEV, UK_SHELL_KEY (0, UK_SHELL_KEY_SUPER));
	}
	if (s_bGrab && mods != nOld) ShellPush (UK_SHELL_KEYEV, UK_SHELL_KEY (mods & 15, UK_SHELL_KEY_HELD));
}

// The window the keys go to: home, the shell's home; else the front program's topmost window -- whatever the window
// manager's own choice (the topmost non-topmost window shown) would be: a window of no app standing above it, a
// borderless one... -> 0: the window manager's choice (a full-screen program, no front program).
static CWindow *KeyWindow (void)
{
	if (g_pElWM->FullscreenWindow () != 0) return 0;
	if (s_bHome && s_nShell != 0)
	{
		for (int id = 0; id < EL_WINDOWS_MAX; id++)
			if (g_pElWin[id] != 0 && g_pElWin[id]->OwnerPid () == s_nShell && pk_kind (id) == PK_KIND_HOME) return g_pElWin[id];
		return 0;
	}
	return pk_front_window ();
}

// The keys typed: to the key window when the window manager would give them to another one (logged once).
static int KeysToFront (const char *keys, unsigned mods)
{
	CWindow *pW = KeyWindow ();
	if (pW == 0 || pW->KeyHandler () == 0 || g_pElWM->KeyTarget () == pW) return 0;
	static unsigned s_nSaid;
	if (s_nSaid != pW->Id ())
	{
		s_nSaid = pW->Id ();
		char Name[32];
		if (el_sys_name (pW->OwnerPid (), Name, sizeof Name) <= 0) { Name[0] = '?'; Name[1] = 0; }
		Say ("the keys to ", Name, g_pElWM->KeyTarget () != 0 ? " (the window manager had another window above it)" : " (the window manager had none)");
	}
	const char *p = keys;
	unsigned m;
	int code;
	while (m = 0, (code = el_core_next_key (&p, &m)) != 0)
	{
		GUIEvent Ev;
		Ev.nMods = (mods & 7) | m; Ev.ulHandler = pW->KeyHandler (); Ev.ulSender = 0;
		Ev.nEvent = GUI_EVENT_KEY; Ev.lValue = code;
		pW->PushEvent (Ev);
	}
	return 1;
}

// (P8) Split view's keys, PocketUI's own (before the shell's): -> 1 taken.
static int SplitKey (const char *keys, unsigned mods)
{
	if (!(mods & KAPI_WS_MOD_SUPER) || (mods & (MOD_ALT | MOD_CTRL)) || s_bGrab || g_pElWM->FullscreenWindow () != 0) return 0;
	const char *p = keys;
	unsigned m = 0;
	int code = el_core_next_key (&p, &m);
	if (code == 0 || *p != 0) return 0;
	if (code == KEY_LEFT || code == KEY_RIGHT)
	{
		if (s_nFront == 0 || s_bHome) return 0;
		int x, y, w, h;
		Area (&x, &y, &w, &h);
		if (w < SPLIT_MIN_W || w <= h) return 0;
		if (s_nSide == 0 || !Shown (s_nSide) || s_nSide == s_nFront)	// the other half: the program fronted before
		{
			s_nSide = 0;
			for (int i = 0; i < s_nMru && s_nSide == 0; i++) if (s_Mru[i] != s_nFront && Shown (s_Mru[i])) s_nSide = s_Mru[i];
			if (s_nSide == 0) return 0;			// (nothing to put beside it)
			Front (s_nSide); Front (s_nFront);
		}
		s_bFrontLeft = code == KEY_LEFT;
		Say (s_bFrontLeft ? "split view: the front program at the left" : "split view: the front program at the right");
	}
	else if (code == KEY_UP) { if (s_nSide == 0) return 0; s_nSide = 0; Say ("split view: left"); }
	else if ((code == '[' || code == ']') && SplitOn ()) { s_nSplit += code == '[' ? -10 : 10; if (s_nSplit < 40) s_nSplit = 40; if (s_nSplit > 60) s_nSplit = 60; }
	else if (code == '\t' && SplitOn ()) Promote (s_nSide);
	else return 0;
	s_bSuperAlone = FALSE;
	ScreenDirty ();
	return 1;
}

static int Key (const char *keys, unsigned mods)
{
	if (SplitKey (keys, mods)) return 1;
	int r = ShellKey (keys, mods);
	if (r >= 0) return r ? r : KeysToFront (keys, mods);
	if (keys[0] != '\t' || keys[1] != '\0' || (mods & MOD_ALT) == 0 || (mods & MOD_CTRL) != 0) return KeysToFront (keys, mods);
	if (g_pElWM->FullscreenWindow () != 0) return 0;	// (a full-screen program's keys are its own)
	unsigned Pids[EL_WINDOWS_MAX];
	int n = Programs (Pids, EL_WINDOWS_MAX, TRUE);
	if (n < 2) return 1;
	Promote ((mods & MOD_SHIFT) ? Pids[1] : Pids[n - 1]);	// Alt+Tab: the one at the back; Alt+Shift+Tab: the one just behind
	return 1;
}

// ---- each turn ------------------------------------------------------------------------------------------

static void Tick (unsigned self)
{
	g_nPkSelf = self;
	if (g_pElWM == 0) return;
	boolean bFull = g_pElWM->FullscreenWindow () != 0;
	static boolean s_bWasFull;
	if (bFull != s_bWasFull)					// (the Pi's kmsg: a full-screen program, its end)
	{
		s_bWasFull = bFull;
		char Name[32];
		if (!bFull || el_sys_name (g_pElWM->FullscreenWindow ()->OwnerPid (), Name, sizeof Name) <= 0) { Name[0] = '?'; Name[1] = 0; }
		Say (bFull ? "full screen: " : "the full screen given back", bFull ? Name : "");
	}
	for (int id = 0; id < EL_WINDOWS_MAX; id++) KindOf (id);	// (the windows made since: classified, a new app's fronted)

	// The global menu bar there: PocketUI's own band away; gone: the band again. The work area changed: the cards
	// centred again (the filled windows are told below).
	if (g_nPkMode == PK_MODE_POCKET)
	{
		boolean bBar = pk_bar_id () >= 0;
		if (bBar && pk_band_id () >= 0) pk_band_make (0);
		else if (!bBar && pk_band_id () < 0) pk_band_make (g_nScreenWidth);
	}
	// (P10) The shell's on-screen keyboard: its window that is opaque, as wide as the screen, on the screen's bottom
	// edge and no taller than half of it is a BAND -- the work area ends above it (the apps are laid out again in
	// what is left); its other windows (the overlays, the toasts; the keyboard parked off the screen) never are.
	for (int id = 0; id < EL_WINDOWS_MAX; id++)
	{
		CWindow *p = g_pElWin[id];
		if (p == 0 || s_St[id].pWin != p || s_St[id].nKind != PK_KIND_SHELL) continue;
		boolean bKeys = !(p->Flags () & WIN_FLAG_ALPHA) && !p->Hidden () && p->X () == 0 && p->OuterWidth () >= g_nScreenWidth
				&& p->Y () > 0 && p->Y () < g_nScreenHeight && p->Y () + p->OuterHeight () >= g_nScreenHeight
				&& p->OuterHeight () <= g_nScreenHeight / 2;
		if (bKeys == (p->NoInset () != FALSE)) { p->SetNoInset (!bKeys); ScreenDirty (); }
	}
	int ax, ay, aw, ah;
	Area (&ax, &ay, &aw, &ah);
	if (ax != s_nAreaX || ay != s_nAreaY || aw != s_nAreaW || ah != s_nAreaH)
	{
		boolean bFirst = s_nAreaX < 0;
		s_nAreaX = ax; s_nAreaY = ay; s_nAreaW = aw; s_nAreaH = ah;
		if (!bFirst) Recentre ();
		ShellPush (UK_SHELL_EVENT, UK_SHELL_EV_AREA);		// (the shell's home: the work area's size)
	}

	if (s_nShell != 0 && !ShellAlive ())			// (the shell ended: nobody's home)
	{
		Say ("the shell ended");
		s_nShell = 0; s_ulShellEv = 0; s_nShellKeys = -1; s_bGrab = FALSE; s_bHome = FALSE;
	}
	for (int id = 0; id < EL_WINDOWS_MAX; id++)
	{
		CWindow *p = g_pElWin[id];
		int k = KindOf (id);
		if (p != 0 && k == PK_KIND_HOME && (p->X () != ax || p->Y () != ay)) { p->Move (ax, ay); ScreenDirty (); }
		// (P8) its program's part of the work area: the whole of it, or a half in split view
		int wx = ax, wy = ay, ww = aw, wh = ah;
		if (p != 0) AreaOf (p->OwnerPid (), &wx, &wy, &ww, &wh);
		// A fixed-size main window that became smaller than its area (an emulator made at 3x then shrunk to 2x when
		// the screen took its game's size, 2026-10-09) is centred over the matte; one grown bigger is filled (cut).
		if (p != 0 && !bFull && (k == PK_KIND_FILL || k == PK_KIND_CENTRE) && !p->Resizable () && AppWindow (p))
		{
			int want = p->OuterWidth () < ww && p->OuterHeight () < wh ? PK_KIND_CENTRE : PK_KIND_FILL;
			if (want != k) { s_St[id].nKind = want; k = want; ScreenDirty (); }
		}
		if (p != 0 && k == PK_KIND_CENTRE && !bFull)		// (centred in the work area: a smaller one cut at its top left)
		{
			int cx = wx + (ww - p->OuterWidth ()) / 2, cy = wy + (wh - p->OuterHeight ()) / 2;
			if (cx < wx) cx = wx;
			if (cy < wy) cy = wy;
			if (p->X () != cx || p->Y () != cy) { p->Move (cx, cy); ScreenDirty (); }
		}
		if (p != 0 && k == PK_KIND_CARD && !bFull && AppWindow (p)
		    && (s_St[id].nRx != wx || s_St[id].nRy != wy || s_St[id].nRw != ww || s_St[id].nRh != wh))
		{	// a card: centred again when its part changed (split view begun, ended, the divider moved)
			boolean bFirst = s_St[id].nRw == 0;
			s_St[id].nRx = wx; s_St[id].nRy = wy; s_St[id].nRw = ww; s_St[id].nRh = wh;
			if (!bFirst || ww != aw)
			{
				int cx = wx + (ww - p->OuterWidth ()) / 2, cy = wy + (wh - p->OuterHeight ()) / 2;
				if (cx < wx) cx = wx;
				if (cy < wy) cy = wy;
				p->Move (cx, cy); ScreenDirty ();
			}
		}
		if (p == 0 || bFull || k != PK_KIND_FILL) continue;
		int vx = 0, vy = 0;
		Viewport (id, p, wx, wy, ww, wh, &vx, &vy);		// (P6: bigger than the work area: scrolled)
		if (p->X () != wx - vx || p->Y () != wy - vy) { p->Move (wx - vx, wy - vy); ScreenDirty (); }
		// A resizable filled window: the work area's size, told once for each size of the work area (UIKit's Root
		// applies GUI_EVENT_WINRESIZE as a frame dragged). A program whose own pointer handler does not pass that
		// event on (the Terminal, the Media Player, the PDF Viewer, Screenshot: their frames do not resize on the
		// desktop either) is then asked to maximise, once (GUI_EVENT_WINCTL: Root::maximise fills the work area).
		int w = ww > p->MinClientW () ? ww : p->MinClientW (), h = wh > p->MinClientH () ? wh : p->MinClientH ();
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
			Ev.lValue = (long) (((u64) (u16) (s16) wx << 48) | ((u64) (u16) (s16) wy << 32) | ((u64) (u16) w << 16) | (u64) (u16) h);
			p->PushEvent (Ev);
			continue;
		}
		if (bFits || s_St[id].bMaxSent || el_port_ticks () - s_St[id].nSentAt < FILL_WAIT) continue;
		s_St[id].bMaxSent = TRUE;
		Ev.nEvent = GUI_EVENT_WINCTL;
		Ev.lValue = KAPI_FRAME_MAXIMISE;
		p->PushEvent (Ev);
	}

	// One app in front: the one a request raised, else the one fronted last that still shows a window.
	if (s_nPromote != 0) { unsigned nPid = s_nPromote; s_nPromote = 0; if (Shown (nPid)) Promote (nPid); }
	FrontNow ();
	s_bReady = TRUE;
	if (s_nSide != 0 && (!Shown (s_nSide) || s_nSide == s_nFront)) { s_nSide = 0; ScreenDirty (); }	// (P8: the other half's program gone)
	boolean bSplit = SplitOn ();
	CWindow *List[WM_MAX_WINDOWS];
	unsigned n = g_pElWM->Snapshot (List, WM_MAX_WINDOWS);
	// Its windows above every other program's, whatever raised those (a click cannot reach a window set aside,
	// but a program's own request, a restored window... can): kept in front.
	boolean bFrontSeen = FALSE, bBuried = FALSE;
	for (unsigned j = n; j-- > 0 && !bFrontSeen; )
	{
		if (!AppWindow (List[j]) || List[j]->Minimised ()) continue;
		if (List[j]->OwnerPid () == s_nFront) bFrontSeen = TRUE;
		else if (!bSplit || List[j]->OwnerPid () != s_nSide) bBuried = TRUE;	// (the other half's program may be above: beside it)
	}
	if (bFrontSeen && bBuried) { Front (s_nFront); n = g_pElWM->Snapshot (List, WM_MAX_WINDOWS); }
	for (unsigned j = 0; j < n; j++)
	{
		CWindow *p = List[j];
		if (!AppWindow (p)) continue;
		boolean bAside = (s_bHome || (s_nFront != 0 && p->OwnerPid () != s_nFront && !(bSplit && p->OwnerPid () == s_nSide))) && !bFull;
		if (p->Aside () != bAside) g_pElWM->SetAside (p, bAside);
	}
	CWindow *pF = pk_front_window ();
	// The keyboard follows the front program: its topmost window raised when another (not set aside: no app's) has the
	// window manager's keys -- kapi_key_held and the pads ask who has them (KAPI_WS_FOCUS: route.cpp).
	if (pF != 0 && !bFull && !s_bHome)
	{
		CWindow *pT = g_pElWM->KeyTarget ();
		if (pT != 0 && pT != pF && pT->OwnerPid () != s_nFront) { g_pElWM->Raise (pF); ScreenDirty (); }
	}
	pk_band_update (pF != 0 ? pF->Title () : "");		// (the front program's topmost window: the band's title)
	int fx = ax, fy = ay, fw = aw, fh = ah;			// (the front program's part of the work area)
	AreaOf (s_nFront, &fx, &fy, &fw, &fh);
	Matte (pF != 0 && !bFull && !s_bHome ? pF : 0, fx, fy, fw, fh);
	Bars (fx, fy, fw, fh);					// (P6: the viewport's indicators)
	Divider (bSplit && !bFull, ax, ay, aw, ah);		// (P8)

	// The shell told when the tasks changed: a window opened, closed, retitled, minimised, the front one, home.
	if (s_nShell != 0 && s_ulShellEv != 0)
	{
		unsigned nSig = 2166136261u ^ s_nFront ^ (s_bHome ? 0x80000000u : 0) ^ (bSplit ? s_nSide * 31u : 0);
		for (unsigned j = 0; j < n; j++)
		{
			CWindow *p = List[j];
			if (!AppWindow (p)) continue;
			nSig = (nSig ^ p->Id ()) * 16777619u;
			nSig = (nSig ^ (p->Minimised () ? 1u : 0u)) * 16777619u;
			for (const char *t = p->Title (); t != 0 && *t; t++) nSig = (nSig ^ (unsigned char) *t) * 16777619u;
		}
		if (nSig == 0) nSig = 1;
		if (nSig != s_nTaskSig && ShellPush (UK_SHELL_EVENT, UK_SHELL_EV_TASKS)) s_nTaskSig = nSig;

		// (P10) ... and when the front program's focused text field changes (its type, as the pocket UIKit says it:
		// PK_OP_TEXT_HINT): the shell's on-screen keyboard comes and goes with it. Pocket only (console has none).
		static int s_nHintSent = -1;
		int nHint = -1;
		if (g_nPkMode == PK_MODE_POCKET && !s_bHome && s_nFront != 0 && !bFull)
			for (int id = 0; id < EL_WINDOWS_MAX && nHint < 0; id++)
				if (g_pElWin[id] != 0 && s_St[id].pWin == g_pElWin[id] && g_pElWin[id]->OwnerPid () == s_nFront
				    && !g_pElWin[id]->Hidden () && !g_pElWin[id]->Minimised ()) nHint = s_St[id].nHint;
		if (nHint > 254) nHint = 0;
		if (nHint != s_nHintSent && ShellPush (UK_SHELL_EVENT, UK_SHELL_EV_TEXT | ((long) (nHint + 1) << 8))) s_nHintSent = nHint;
	}
}

// ---- the matte: behind a fixed-size main window, the work area in that window's background colour ----------------
static int s_nMatte = -1;					// its window (PocketUI's own), -1: none
static unsigned s_nMatteFor, s_nMatteColour;
int pk_matte (void)	{ return s_nMatte >= 0 && g_pElWin[s_nMatte] != 0 && !g_pElWin[s_nMatte]->Aside () ? 1 : 0; }

static void Matte (CWindow *pFront, int ax, int ay, int aw, int ah)
{
	int idF = pFront != 0 ? IdOfWinId (pFront->Id ()) : -1;
	boolean bWant = idF >= 0 && s_St[idF].nKind == PK_KIND_CENTRE;
	CWindow *pM = s_nMatte >= 0 ? g_pElWin[s_nMatte] : 0;
	if (!bWant)
	{
		if (pM != 0 && !pM->Aside ()) g_pElWM->SetAside (pM, TRUE);
		s_nMatteFor = 0;
		return;
	}
	int w = 0, h = 0;
	if (pM != 0) el_core_window_canvas (s_nMatte, &w, &h);
	if (pM == 0 || w != aw || h != ah)			// (made, or made again for another work area)
	{
		if (s_nMatte >= 0) el_core_window_remove (s_nMatte);
		el_core_owner (0);
		s_nMatte = el_core_window_add (ax, ay, aw, ah, "matte", WIN_FLAG_BORDERLESS | WIN_FLAG_SYSTEM, g_nPkSelf);
		if (s_nMatte < 0) return;
		pM = g_pElWin[s_nMatte];
		s_nMatteFor = 0; s_nMatteColour = 0xFFFFFFFFu;
	}
	if (pM->X () != ax || pM->Y () != ay) pM->Move (ax, ay);
	// its colour: the window's own background (its client area's top left pixel), else Milk's face
	const u32 *pC = pFront->CanvasBuffer ();
	unsigned c = pC != 0 ? pC[0] & 0x00FFFFFF : 0x00E4E4E4;
	if (c != s_nMatteColour)
	{
		s_nMatteColour = c;
		unsigned *pPx = el_core_window_canvas (s_nMatte, &w, &h);
		for (int i = 0; pPx != 0 && i < w * h; i++) pPx[i] = c;
		el_core_window_present (s_nMatte);
	}
	if (s_nMatteFor != pFront->Id () || pM->Aside ())		// (just under the front program's windows)
	{
		s_nMatteFor = pFront->Id ();
		if (pM->Aside ()) g_pElWM->SetAside (pM, FALSE);
		g_pElWM->Raise (pM);
		Front (pFront->OwnerPid ());
	}
}

// ---- (P6) the viewport: a filled window bigger than the work area keeps its canvas; PocketUI shows the part that fits
// and scrolls it (docs/POCKETUI-TECH-STUDY.md section 6.3): the window moved up / left under the work area's edges (the
// band above it covers what passed the top), the wheel over the indicators at the work area's right and bottom edges or
// with Alt held anywhere, the indicators dragged, the focused control kept in view (PK_OP_FOCUS_RECT). No copy, the
// pointer's coordinates the window's own: it works for every program, UIKit or not.
enum { VP_BAR = 6, VP_ZONE = 14, VP_STEP = 48, VP_MARGIN = 8 };
static int s_nVBar = -1, s_nHBar = -1;				// the indicators' windows (PocketUI's own), -1: none
static int s_nDrag;						// 1: the vertical one dragged, 2: the horizontal one
static unsigned s_nPrevButtons;

// The window a viewport scrolls now: the front program's topmost window when it is filled and bigger -> its number, -1.
static int ViewportId (void)
{
	if (g_pElWM == 0 || g_pElWM->FullscreenWindow () != 0 || s_bHome || s_bGrab) return -1;	// (an overlay up: none)
	CWindow *pF = pk_front_window ();
	if (pF == 0) return -1;
	int id = IdOfWinId (pF->Id ());
	if (id < 0 || s_St[id].nKind != PK_KIND_FILL) return -1;
	int ax, ay, aw, ah;
	AreaOf (pF->OwnerPid (), &ax, &ay, &aw, &ah);
	return pF->ClientWidth () > aw || pF->ClientHeight () > ah ? id : -1;
}

int pk_viewport (int id, int *vx, int *vy)
{
	if (id < 0 || id >= EL_WINDOWS_MAX || g_pElWin[id] == 0) return 0;
	int ax, ay, aw, ah;
	CWindow *p = g_pElWin[id];
	AreaOf (p->OwnerPid (), &ax, &ay, &aw, &ah);
	if (s_St[id].nKind != PK_KIND_FILL || (p->ClientWidth () <= aw && p->ClientHeight () <= ah)) return 0;
	*vx = s_St[id].nVx; *vy = s_St[id].nVy;
	return 1;
}
int pk_text_hint (int id) { return id >= 0 && id < EL_WINDOWS_MAX ? s_St[id].nHint : -1; }

static void Viewport (int id, CWindow *p, int ax, int ay, int aw, int ah, int *pvx, int *pvy)
{
	int mw = p->ClientWidth () - aw, mh = p->ClientHeight () - ah;	// (how far it can scroll)
	if (mw < 0) mw = 0;
	if (mh < 0) mh = 0;
	TState &S = s_St[id];
	if (S.bFocus && (mw > 0 || mh > 0))			// its focused control shown (with a margin)
	{
		S.bFocus = FALSE;
		if (S.nFy < S.nVy + VP_MARGIN) S.nVy = S.nFy - VP_MARGIN;
		else if (S.nFy + S.nFh > S.nVy + ah - VP_MARGIN) S.nVy = S.nFy + S.nFh - ah + VP_MARGIN;
		if (S.nFx < S.nVx + VP_MARGIN) S.nVx = S.nFx - VP_MARGIN;
		else if (S.nFx + S.nFw > S.nVx + aw - VP_MARGIN) S.nVx = S.nFx + S.nFw - aw + VP_MARGIN;
	}
	if (S.nVx > mw) S.nVx = mw;
	if (S.nVy > mh) S.nVy = mh;
	if (S.nVx < 0) S.nVx = 0;
	if (S.nVy < 0) S.nVy = 0;
	*pvx = S.nVx; *pvy = S.nVy;
}

static void Scroll (int id, int dx, int dy)			// the viewport moved (its window placed at once)
{
	CWindow *p = g_pElWin[id];
	int ax, ay, aw, ah;
	AreaOf (p->OwnerPid (), &ax, &ay, &aw, &ah);
	s_St[id].nVx += dx; s_St[id].nVy += dy;
	int vx, vy;
	Viewport (id, p, ax, ay, aw, ah, &vx, &vy);
	if (p->X () != ax - vx || p->Y () != ay - vy) { p->Move (ax - vx, ay - vy); ScreenDirty (); }
	Bars (ax, ay, aw, ah);
}

static void BarDraw (int nBar, boolean bVertical, int nPos, int nLen, int nTotal)
{
	int w = 0, h = 0;
	unsigned *px = el_core_window_canvas (nBar, &w, &h);
	if (px == 0) return;
	int span = bVertical ? h : w;
	int t = nTotal > 0 ? span * nLen / nTotal : span, o = nTotal > 0 ? span * nPos / nTotal : 0;
	if (t < 24) t = 24;
	if (o + t > span) o = span - t;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			int along = bVertical ? y : x, across = bVertical ? x : y;
			boolean bThumb = along >= o && along < o + t && across >= 1 && across < VP_BAR - 1;
			px[y * w + x] = bThumb ? 0x00586070 : 0x00D8DCE2;
		}
	el_core_window_present (nBar);
}

static int BarMake (int nOld, int x, int y, int w, int h)
{
	if (nOld >= 0)
	{
		int cw = 0, ch = 0;
		el_core_window_canvas (nOld, &cw, &ch);
		CWindow *p = g_pElWin[nOld];
		if (p != 0 && cw == w && ch == h) { if (p->X () != x || p->Y () != y) p->Move (x, y); return nOld; }
		el_core_window_remove (nOld);
	}
	el_core_owner (0);
	int id = el_core_window_add (x, y, w, h, "viewport", WIN_FLAG_TOPMOST | WIN_FLAG_BORDERLESS | WIN_FLAG_SYSTEM, g_nPkSelf);
	if (id >= 0 && g_pElWin[id] != 0) g_pElWin[id]->SetNoInset (TRUE);	// (never a band of the work area)
	return id;
}

static void Bars (int ax, int ay, int aw, int ah)
{
	int id = ViewportId ();
	CWindow *p = id >= 0 ? g_pElWin[id] : 0;
	boolean bV = p != 0 && p->ClientHeight () > ah, bH = p != 0 && p->ClientWidth () > aw;
	if (bV)
	{
		s_nVBar = BarMake (s_nVBar, ax + aw - VP_BAR, ay, VP_BAR, ah - (bH ? VP_BAR : 0));
		if (s_nVBar >= 0) BarDraw (s_nVBar, TRUE, s_St[id].nVy, ah, p->ClientHeight ());
	}
	else if (s_nVBar >= 0) { el_core_window_remove (s_nVBar); s_nVBar = -1; ScreenDirty (); }
	if (bH)
	{
		s_nHBar = BarMake (s_nHBar, ax, ay + ah - VP_BAR, aw - (bV ? VP_BAR : 0), VP_BAR);
		if (s_nHBar >= 0) BarDraw (s_nHBar, FALSE, s_St[id].nVx, aw, p->ClientWidth ());
	}
	else if (s_nHBar >= 0) { el_core_window_remove (s_nHBar); s_nHBar = -1; ScreenDirty (); }
}

// The pointer before the window manager: the viewport's wheel and indicators -> 1 taken.
static int Pointer (int x, int y, unsigned buttons, int wheel)
{
	unsigned nPrev = s_nPrevButtons;
	s_nPrevButtons = buttons;
	if ((buttons & 1) && !(nPrev & 1) && SplitOn ())		// (P8) a press on the other half: its program in front
	{
		int sx, sy, sw, sh;
		AreaOf (s_nSide, &sx, &sy, &sw, &sh);
		if (x >= sx && x < sx + sw && y >= sy && y < sy + sh) Promote (s_nSide);
	}
	int id = ViewportId ();
	if (id < 0) { s_nDrag = 0; return 0; }
	CWindow *p = g_pElWin[id];
	int ax, ay, aw, ah;
	AreaOf (p->OwnerPid (), &ax, &ay, &aw, &ah);
	boolean bV = p->ClientHeight () > ah, bH = p->ClientWidth () > aw;
	boolean inV = bV && x >= ax + aw - VP_ZONE && x < ax + aw && y >= ay && y < ay + ah;
	boolean inH = bH && !inV && y >= ay + ah - VP_ZONE && y < ay + ah && x >= ax && x < ax + aw;
	if (s_nDrag != 0)					// an indicator dragged: the place under the pointer
	{
		if (!(buttons & 1)) { s_nDrag = 0; return 1; }
		if (s_nDrag == 1) Scroll (id, 0, (y - ay) * (p->ClientHeight () - ah) / (ah > 0 ? ah : 1) - s_St[id].nVy);
		else Scroll (id, (x - ax) * (p->ClientWidth () - aw) / (aw > 0 ? aw : 1) - s_St[id].nVx, 0);
		return 1;
	}
	if (wheel != 0 && (inV || inH || (s_nMods & MOD_ALT)))
	{
		boolean bSide = inH || (!bV && bH) || ((s_nMods & MOD_SHIFT) != 0 && bH);
		if (bSide) Scroll (id, -wheel * VP_STEP, 0); else Scroll (id, 0, -wheel * VP_STEP);
		return 1;
	}
	if ((buttons & 1) && !(nPrev & 1) && (inV || inH))
	{
		s_nDrag = inV ? 1 : 2;
		if (inV) Scroll (id, 0, (y - ay) * (p->ClientHeight () - ah) / (ah > 0 ? ah : 1) - s_St[id].nVy);
		else Scroll (id, (x - ax) * (p->ClientWidth () - aw) / (aw > 0 ? aw : 1) - s_St[id].nVx, 0);
		return 1;
	}
	return 0;
}

// ---- (P8) split view's divider: PocketUI's own window between the halves -- two pixels in the accent colour on the
// front program's side (which half has the keys), two grey ones on the other.
static void Divider (boolean bOn, int ax, int ay, int aw, int ah)
{
	static int s_nLeft = -1;
	if (!bOn)
	{
		if (s_nDivider >= 0) { el_core_window_remove (s_nDivider); s_nDivider = -1; s_nLeft = -1; ScreenDirty (); }
		return;
	}
	int x = ax + aw * s_nSplit / 100 - SPLIT_GAP / 2;
	int old = s_nDivider;
	s_nDivider = BarMake (s_nDivider, x, ay, SPLIT_GAP, ah);
	if (s_nDivider < 0) return;
	if (s_nDivider == old && s_nLeft == (s_bFrontLeft ? 1 : 0)) return;
	s_nLeft = s_bFrontLeft ? 1 : 0;
	int w = 0, h = 0;
	unsigned *px = el_core_window_canvas (s_nDivider, &w, &h);
	for (int j = 0; px != 0 && j < h; j++)
		for (int i = 0; i < w; i++)
			px[j * w + i] = ((i < w / 2) == (s_bFrontLeft != FALSE)) ? 0x003D86DA : 0x00A8AEB8;
	el_core_window_present (s_nDivider);
	ScreenDirty ();
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
	pk_band_make (g_nPkMode == PK_MODE_POCKET && pk_bar_id () < 0 ? w : 0);	// (under the menu bar: none)
	Recentre ();
}

static void Start (int w, int, int restart)
{
	g_nPkSelf = (unsigned) kapi_getpid (0);
	g_pElWM->SetDesk (0, 1);					// one workspace
	if (!restart) el_core_wallpaper (0x00203050, 24, kapi_get_ticks () | 1);	// (the navy wallpaper behind the cards)
	pk_band_make (g_nPkMode == PK_MODE_POCKET && pk_bar_id () < 0 ? w : 0);
	Recentre ();
}

void pk_main_registered (int restart);				// (main.cpp: the alias)

// (P9) Who has the keys, for the kernel (kapi_key_held, the pads): the shell while its overlay grabs them, and at
// home -- the console's menu over a game is moved with the pad, the game under it reads nothing meanwhile.
static unsigned Focus (unsigned pid)
{
	return s_nShell != 0 && (s_bGrab || s_bHome) ? s_nShell : pid;
}

static const struct ws_policy s_Pocket =
{
	"pocketui",
	Create, Op, Key, Tick, Screen,
	pk_main_registered,
	Start,
	0, 0,							// (no demonstration)
	Mods,
	Pointer,						// (P6: the viewport)
	Focus,							// (P9: the shell's overlay has the pads)
};
const struct ws_policy *g_pWsPolicy = &s_Pocket;
