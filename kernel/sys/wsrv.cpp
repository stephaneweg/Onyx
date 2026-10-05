//
// wsrv.cpp -- the kernel's mechanisms for the graphics server (kern/wsrv.h), kapi v89.
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
#include <kern/wsrv.h>
#include <kern/kapi_abi.h>
#include <kern/addrspace.h>
#include <kern/gui/window.h>		// g_nScreenWidth / Height, ScreenDirty, the full-screen window
#include <kern/iowait.h>
#include <kern/ipc.h>			// IpcPidAlive
#include <kern/uaccess.h>
#include <kern/layout.h>
#include <kern/applaunch.h>		// ExecPath (the trial's start)
#include <fatfs/ff.h>
#include <circle/2dgraphics.h>
#include <circle/new.h>
#include <circle/sched/synchronizationevent.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/spinlock.h>
#include <circle/timer.h>
#include <circle/util.h>

extern C2DGraphics *g_pGraphics;		// (kernel.cpp)

#define WS_RING		256			// raw input events waiting for the server
#define WS_SILENT_MS	5000			// a server that calls nothing for so long loses the display
#define WS_WAIT_MAX_MS	1000			// (so a waiting server is not a silent one)

static volatile unsigned s_nServerPid = 0;	// the display server, 0: none
static volatile boolean  s_bOwned = FALSE;	// it owns the display and the input
static volatile unsigned s_nLastCall = 0;	// (ticks) its last call

static struct kapi_ws_input s_Ring[WS_RING];
static volatile unsigned s_nHead = 0, s_nTail = 0;	// written at head, read at tail
static volatile unsigned s_nDropped = 0;
static CSpinLock s_RingLock;

static volatile unsigned s_nFocusPid = 0;	// (KAPI_WS_FOCUS)

unsigned WsFocusPid (void)
{
	return s_bOwned ? s_nFocusPid : 0;
}

static unsigned MyPid (void)
{
	CAddressSpace *pAS = (CAddressSpace *) CScheduler::Get ()->GetCurrentTask ()->GetUserData (TASK_USER_DATA_USER);
	return pAS != 0 ? pAS->GetPid () : 0;
}

static boolean IsServer (void)
{
	unsigned nPid = MyPid ();
	return nPid != 0 && nPid == s_nServerPid;
}

boolean WsDisplayOwned (void)
{
	return s_bOwned;
}

static void Release (const char *pWhy)
{
	if (!s_bOwned) return;
	s_bOwned = FALSE;
	s_RingLock.Acquire ();
	s_nHead = s_nTail = 0;
	s_RingLock.Release ();
	ScreenDirty ();				// the kernel's compositor draws the whole screen again
	if (pWhy != 0) CLogger::Get ()->Write ("wsrv", LogWarning, "the display taken back: %s", pWhy);
}

// The server started by the kernel ended (WsOnProcessGone: nothing may be done there): started again
// here, from the compositor's loop -- a few times at most (a server that cannot stay up: the
// kernel's window manager keeps the display). Its programs' windows: their pixels are kept (the
// buffers), each program asks the new server for its window again (AppKit).
static volatile boolean s_bBootMode = FALSE;	// the kernel started the server (WsBootStart)
static volatile boolean s_bRelaunch = FALSE;
static unsigned s_nRelaunches = 0;
#define WS_RELAUNCH_MAX	5

void WsPoll (void)
{
	if (!s_bRelaunch) return;
	s_bRelaunch = FALSE;
	if (s_nRelaunches >= WS_RELAUNCH_MAX)
	{
		CLogger::Get ()->Write ("wsrv", LogError, "the graphics server ended %u times: not started again", s_nRelaunches);
		return;
	}
	s_nRelaunches++;
	CLogger::Get ()->Write ("wsrv", LogWarning, "the graphics server ended: started again (%u)", s_nRelaunches);
	if (!ExecPath ("SD:bin/elegant", "--serve --restart"))
		CLogger::Get ()->Write ("wsrv", LogError, "cannot start the graphics server again");
}

void WsWatch (void)
{
	if (!s_bOwned) return;
	if (CTimer::Get ()->GetTicks () - s_nLastCall > WS_SILENT_MS * HZ / 1000)
		Release ("the graphics server is silent");
}

// ---- the raw input ------------------------------------------------------------------------------

// Queue one event (an interrupt is allowed). A pointer MOVE (no button changed, no wheel) after an
// unread pointer move replaces it -- never a press or a release: where the button went down is
// what a drag starts from. A full ring drops the event (counted).
static unsigned s_nPushButtons = 0;		// the buttons of the last pointer event queued
static boolean  s_bLastMove = FALSE;		// the last event queued is a pointer move

static boolean Push (const struct kapi_ws_input &Ev)
{
	if (!s_bOwned) return FALSE;
	s_RingLock.Acquire ();
	boolean bMove = Ev.type == KAPI_WS_IN_POINTER && Ev.a == 0 && Ev.buttons == s_nPushButtons;
	if (bMove && s_bLastMove && s_nHead != s_nTail)
	{
		s_Ring[(s_nHead + WS_RING - 1) % WS_RING] = Ev;
	}
	else
	{
		unsigned nNext = (s_nHead + 1) % WS_RING;
		if (nNext == s_nTail) s_nDropped++;
		else { s_Ring[s_nHead] = Ev; s_nHead = nNext; s_bLastMove = bMove; }
	}
	if (Ev.type == KAPI_WS_IN_POINTER) s_nPushButtons = Ev.buttons;
	s_RingLock.Release ();
	IoWake ();				// (the server's KAPI_WS_WAIT)
	return TRUE;
}

boolean WsInputPointer (int x, int y, unsigned nButtons, int nWheel)
{
	if (!s_bOwned) return FALSE;
	struct kapi_ws_input Ev;
	memset (&Ev, 0, sizeof Ev);
	Ev.type = KAPI_WS_IN_POINTER; Ev.x = x; Ev.y = y; Ev.buttons = nButtons; Ev.a = nWheel;
	return Push (Ev);
}

// The cooked string, cut into events of at most sizeof keys - 1 characters -- never inside an
// escape sequence (an arrow, Home...: ESC and what follows it stay together).
boolean WsInputKey (const char *pString)
{
	if (!s_bOwned || pString == 0) return FALSE;
	const unsigned nMax = sizeof ((struct kapi_ws_input *) 0)->keys - 1;
	while (*pString != '\0')
	{
		struct kapi_ws_input Ev;
		memset (&Ev, 0, sizeof Ev);
		Ev.type = KAPI_WS_IN_KEY;
		unsigned n = 0;
		while (n < nMax && pString[n] != '\0') n++;
		if (pString[n] != '\0')				// cut: not after an ESC of the last 8 characters
			for (unsigned k = n; k > 0 && k + 8 > n; k--)
				if (pString[k - 1] == '\x1b') { if (k > 1) n = k - 1; break; }
		memcpy (Ev.keys, pString, n);
		pString += n;
		if (!Push (Ev)) return FALSE;
	}
	return TRUE;
}

boolean WsInputMods (unsigned nMods)
{
	if (!s_bOwned) return FALSE;
	struct kapi_ws_input Ev;
	memset (&Ev, 0, sizeof Ev);
	Ev.type = KAPI_WS_IN_MODS; Ev.a = (int) nMods;
	return Push (Ev);
}

boolean WsInputHeldUsb (const unsigned char RawKeys[6])
{
	if (!s_bOwned) return FALSE;
	struct kapi_ws_input Ev;
	memset (&Ev, 0, sizeof Ev);
	Ev.type = KAPI_WS_IN_HELD_USB;
	memcpy (Ev.keys, RawKeys, 6);
	return Push (Ev);
}

boolean WsInputHeld (int nKey, boolean bDown)
{
	if (!s_bOwned) return FALSE;
	struct kapi_ws_input Ev;
	memset (&Ev, 0, sizeof Ev);
	Ev.type = KAPI_WS_IN_HELD; Ev.a = nKey; Ev.buttons = bDown ? 1 : 0;
	return Push (Ev);
}

void WsScreenResized (int nW, int nH)
{
	if (!s_bOwned) return;
	struct kapi_ws_input Ev;
	memset (&Ev, 0, sizeof Ev);
	Ev.type = KAPI_WS_IN_SCREEN; Ev.x = nW; Ev.y = nH;
	Push (Ev);
}

void WsFullscreen (unsigned nPid, boolean bOn)
{
	if (!s_bOwned) return;
	struct kapi_ws_input Ev;
	memset (&Ev, 0, sizeof Ev);
	Ev.type = KAPI_WS_IN_FULLSCREEN; Ev.a = (int) nPid; Ev.buttons = bOn ? 1 : 0;
	Push (Ev);
}

// ---- the server's operations --------------------------------------------------------------------

// The role is for the program named "elegant" (its task's name, with or without its path), when no
// live process has it. (Stage 3: the kernel starts the server itself and gives the role to that
// process alone.)
static long Register (void)
{
	unsigned nPid = MyPid ();
	if (nPid == 0) return -KAPI_EPERM;
	if (s_nServerPid == nPid) return 1;
	if (s_nServerPid != 0 && IpcPidAlive (s_nServerPid)) return 0;
	const char *pName = CScheduler::Get ()->GetCurrentTask ()->GetName ();
	const char *pBase = pName;
	for (const char *p = pName; *p != '\0'; p++) if (*p == '/' || *p == ':') pBase = p + 1;
	if (strcmp (pBase, "elegant") != 0) return -KAPI_EPERM;
	Release (0);
	s_nServerPid = nPid;
	return 1;
}

static long Display (boolean bTake, struct kapi_ws_display *pOut)
{
	if (!bTake) { Release (0); return 0; }
	CWindowManager *pWM = CWindowManager::Get ();
	if (g_pGraphics == 0 || pWM == 0 || pWM->FullscreenWindow () != 0) return -KAPI_EBUSY;
	if (pOut != 0)
	{
		struct kapi_ws_display D;
		memset (&D, 0, sizeof D);
		D.w = g_nScreenWidth; D.h = g_nScreenHeight;
		if (!UserPut (pOut, D)) return -KAPI_EFAULT;
	}
	s_nLastCall = CTimer::Get ()->GetTicks ();
	s_bOwned = TRUE;
	return 0;
}

// The rectangle copied from the server's pixels into the off-screen buffer, then sent to the
// display. The display's DMA may be the compositor's last frame's: waited for first (it yields --
// the display may be lost meanwhile: checked again).
static long Present (const struct kapi_ws_present *pUser)
{
	struct kapi_ws_present P;
	if (!s_bOwned || g_pGraphics == 0) return -KAPI_EPERM;
	if (!UserGet (&P, pUser)) return -KAPI_EFAULT;
	CWindowManager *pKWM = CWindowManager::Get ();
	if (pKWM != 0 && pKWM->FullscreenWindow () != 0) return -KAPI_EBUSY;	// (a full-screen program shows itself)
	int nW = (int) g_pGraphics->GetWidth (), nH = (int) g_pGraphics->GetHeight ();
	if (nW != g_nScreenWidth || nH != g_nScreenHeight) return -KAPI_EBUSY;
	if (P.w <= 0 || P.h <= 0) { P.x = 0; P.y = 0; P.w = nW; P.h = nH; }
	if (P.x < 0 || P.y < 0 || P.x >= nW || P.y >= nH || P.stride < nW) return -KAPI_EINVAL;
	if (P.w > nW - P.x) P.w = nW - P.x;
	if (P.h > nH - P.y) P.h = nH - P.y;
	// the server's screen: stride pixels a row, the rows 0 .. y + h - 1 readable
	const u32 *pSrc = (const u32 *) P.pixels;
	if (pSrc == 0 || !UserReadable (pSrc, ((u64) (P.y + P.h - 1) * P.stride + P.x + P.w) * 4)) return -KAPI_EFAULT;
	DisplayPresentIdle ();
	if (!s_bOwned || !IsServer ()) return -KAPI_EPERM;
	if (pKWM != 0 && pKWM->FullscreenWindow () != 0) return -KAPI_EBUSY;
	u32 *pDst = (u32 *) g_pGraphics->GetBuffer ();
	for (int y = P.y; y < P.y + P.h; y++)
		memcpy (pDst + (size_t) y * nW + P.x, pSrc + (size_t) y * P.stride + P.x, (size_t) P.w * 4);
	DisplayPresentRect ((unsigned) P.x, (unsigned) P.y, (unsigned) P.w, (unsigned) P.h);
	ScreenDirty ();				// (kapi_screen_grab: vncd must see the new frame)
	return 0;
}

static long Input (struct kapi_ws_input *pOut, long nMax)
{
	if (pOut == 0 || nMax <= 0) return 0;
	long n = 0;
	while (n < nMax)
	{
		struct kapi_ws_input Ev;
		s_RingLock.Acquire ();
		boolean bGot = s_nTail != s_nHead;
		if (bGot) { Ev = s_Ring[s_nTail]; s_nTail = (s_nTail + 1) % WS_RING; }
		s_RingLock.Release ();
		if (!bGot) break;
		if (!UserPut (pOut + n, Ev)) return -KAPI_EFAULT;
		n++;
	}
	return n;
}

static boolean ReqPending (void);

// Until an event or a program's request is there, or the timeout. The wait is on the system's I/O generation, which moves
// for everyone's I/O (the network's, the pipes'): what is not for the server is slept through here,
// not returned to it.
static long Wait (unsigned nTimeoutMs)
{
	if (nTimeoutMs > WS_WAIT_MAX_MS) nTimeoutMs = WS_WAIT_MAX_MS;
	unsigned nStart = CTimer::Get ()->GetClockTicks ();
	while (s_bOwned)
	{
		u32 nGen = IoGen ();		// (taken before the look: a push after it changes it)
		if (s_nHead != s_nTail || ReqPending ()) break;
		unsigned nGoneMs = (CTimer::Get ()->GetClockTicks () - nStart) / 1000;
		if (nGoneMs >= nTimeoutMs) break;
		IoWait (nGen, nTimeoutMs - nGoneMs);
	}
	s_nLastCall = CTimer::Get ()->GetTicks ();
	return (s_nHead != s_nTail ? KAPI_WS_PENDING_INPUT : 0) | (ReqPending () ? KAPI_WS_PENDING_CALL : 0);
}

// ---- the programs' windows: the attached programs, their event queues ---------------------------
//
// An attached program's windows are the server's. The kernel keeps what its pump reads: an event
// queue, the exit request, the wake of kapi_pump_wait. They are a window's (CWindow) -- so an
// attached program has a kernel window that is only that: never given to the kernel's window
// manager, never drawn (one page of pixels). kapi_pop_event, kapi_should_exit, kapi_event_mods,
// kapi_pump_sleep and kapi_kill's clean close work on it as on any window.

#define WS_CLIENTS	64

static unsigned s_Client[WS_CLIENTS];		// the attached programs' pids (0: free)
static u8 s_ClientState[WS_CLIENTS][KAPI_WS_STATE_BYTES];	// (KAPI_WS_STATE) what the server keeps for each

static int ClientSlot (unsigned nPid)
{
	if (nPid == 0) return -1;
	for (int i = 0; i < WS_CLIENTS; i++) if (s_Client[i] == nPid) return i;
	return -1;
}

static long Attach (unsigned nPid)
{
	CAddressSpace *pAS = IpcFindAS (nPid);
	if (pAS == 0) return -KAPI_ESRCH;
	if (ClientSlot (nPid) >= 0) return 0;
	if (pAS->GetWindow () != 0) return -KAPI_EBUSY;		// (a window of the kernel's window manager)
	int nFree = -1;
	for (int i = 0; i < WS_CLIENTS && nFree < 0; i++) if (s_Client[i] == 0) nFree = i;
	if (nFree < 0) return -KAPI_ENOMEM;
	CWindow *pWin = new CWindow (0, 0, 1, 1, "ws", WIN_FLAG_BORDERLESS);
	if (pWin == 0 || !pWin->IsValid ()) { delete pWin; return -KAPI_ENOMEM; }
	pWin->SetOwnerPid (nPid);
	pAS->SetWindow (pWin);
	s_Client[nFree] = nPid;
	memset (s_ClientState[nFree], 0, KAPI_WS_STATE_BYTES);
	return 0;
}

static CWindow *ClientWindow (unsigned nPid)
{
	if (ClientSlot (nPid) < 0) return 0;
	CAddressSpace *pAS = IpcFindAS (nPid);
	return pAS != 0 ? pAS->GetWindow () : 0;
}

static long Post (unsigned nPid, const struct kapi_event *pUser)
{
	struct kapi_event E;
	if (!UserGet (&E, pUser)) return -KAPI_EFAULT;
	CWindow *pWin = ClientWindow (nPid);
	if (pWin == 0) return -KAPI_ESRCH;
	if (pWin->QueuedEvents () >= WIN_EVENT_QUEUE - 1) return 0;	// (full: the server keeps it)
	GUIEvent Ev;
	Ev.ulHandler = E.handler; Ev.ulSender = E.sender; Ev.nEvent = E.event; Ev.lValue = (long) E.value;
	Ev.nMods = E.mods;
	pWin->PushEvent (Ev);
	return 1;
}

static long State (unsigned nPid, void *pUser, boolean bSet)
{
	int nSlot = ClientSlot (nPid);
	if (nSlot < 0) return -KAPI_ESRCH;
	if (bSet) return UserCopyIn (s_ClientState[nSlot], pUser, KAPI_WS_STATE_BYTES) ? 0 : -KAPI_EFAULT;
	return UserCopyOut (pUser, s_ClientState[nSlot], KAPI_WS_STATE_BYTES) ? 0 : -KAPI_EFAULT;
}

static long ExitRequest (unsigned nPid)
{
	CWindow *pWin = ClientWindow (nPid);
	if (pWin == 0) return -KAPI_ESRCH;
	pWin->RequestExit ();
	return 0;
}

// ---- the windows' buffers ------------------------------------------------------------------------
//
// A buffer is physically contiguous (the GPU may render into a window's canvas: sys/v3d.cpp) and is
// mapped twice: in its program at the slot's fixed address (the addresses a program's canvas and
// frame always had), in the server at USER_WS_BASE + its number * USER_WS_SLOT. It lives until the
// server frees it AND its program no longer has it (the program ended, or the server gave the slot
// another buffer); a server that dies leaves its programs their buffers (their memory is not
// pulled from under them): freed as each one ends.

struct TWsBuf
{
	void	*pRaw;			// the heap block, 0: free
	u64	 ulPhys;		// its 64 KB aligned start (== kernel VA)
	unsigned nPages;
	unsigned nPid;			// its program
	int	 nSlot;
	boolean	 bProgram;		// mapped in its program
	boolean	 bServer;		// mapped in the server
};

static TWsBuf s_Buf[USER_WS_SLOTS];

static const u64 s_SlotVA[KAPI_WS_SLOTS] = { USER_WINDOW_CANVAS, USER_WINDOW_CHROME, USER_WINDOW_CHROME_INACTIVE,
						  USER_WALLPAPER_CANVAS, KAPI_WS_VA_XFER };
static_assert (KAPI_WS_VA_CANVAS == USER_WINDOW_CANVAS && KAPI_WS_VA_FRAME == USER_WINDOW_CHROME
	       && KAPI_WS_VA_FRAME_OFF == USER_WINDOW_CHROME_INACTIVE && KAPI_WS_VA_WALLPAPER == USER_WALLPAPER_CANVAS
	       && KAPI_WS_VA_XFER > USER_WALLPAPER_CANVAS && KAPI_WS_VA_XFER + USER_WS_SLOT <= USER_SURFACE_BASE, "the windows' addresses (kern/kapi_abi.h)");

static void BufFreeIfUnused (TWsBuf *b)
{
	if (b->pRaw == 0 || b->bProgram || b->bServer) return;
	delete [] (u8 *) b->pRaw;
	b->pRaw = 0;
}

static long BufMap (struct kapi_ws_buf *pUser)
{
	struct kapi_ws_buf B;
	if (!UserGet (&B, pUser)) return -KAPI_EFAULT;
	if (B.slot < 0 || B.slot >= KAPI_WS_SLOTS || B.bytes == 0 || B.bytes > USER_WS_SLOT) return -KAPI_EINVAL;
	if (ClientSlot (B.pid) < 0) return -KAPI_ESRCH;
	CAddressSpace *pProg = IpcFindAS (B.pid);
	CAddressSpace *pSrv = IpcFindAS (s_nServerPid);
	if (pProg == 0 || pSrv == 0) return -KAPI_ESRCH;
	unsigned nPages = (unsigned) ((B.bytes + KPAGE_MASK) / KPAGE_SIZE);
	TKPageAttr AdoptAttr = KPAGE_ATTR_APP_DATA;
	if (B.flags & KAPI_WS_BUF_ADOPT)		// what a server that ended left the program, as it is
		for (int i = 0; i < USER_WS_SLOTS; i++)
		{
			TWsBuf *o = &s_Buf[i];
			if (o->pRaw == 0 || !o->bProgram || o->bServer || o->nPid != B.pid || o->nSlot != B.slot || o->nPages != nPages) continue;
			u64 ulVA = USER_WS_BASE + (u64) i * USER_WS_SLOT;
			pSrv->MapContig (ulVA, o->ulPhys, o->nPages, AdoptAttr);
			pSrv->FlushTLB ();
			o->bServer = TRUE;
			B.id = (unsigned) i + 1; B.addr = ulVA;
			return UserPut (pUser, B) ? 0 : -KAPI_EFAULT;
		}
	int n = -1;
	for (int i = 0; i < USER_WS_SLOTS && n < 0; i++) if (s_Buf[i].pRaw == 0) n = i;
	if (n < 0) return -KAPI_ENOMEM;
	TWsBuf *b = &s_Buf[n];
	void *pRaw = new u8[(size_t) nPages * KPAGE_SIZE + KPAGE_SIZE];
	if (pRaw == 0) return -KAPI_ENOMEM;
	u64 ulPhys = ((u64) (uintptr) pRaw + KPAGE_MASK) & ~(u64) KPAGE_MASK;
	memset ((void *) (uintptr) ulPhys, 0, (size_t) nPages * KPAGE_SIZE);

	// the slot's buffer so far leaves the program (its pages past the new one's end too)
	for (int i = 0; i < USER_WS_SLOTS; i++)
	{
		TWsBuf *o = &s_Buf[i];
		if (o->pRaw == 0 || !o->bProgram || o->nPid != B.pid || o->nSlot != B.slot) continue;
		pProg->UnmapContig (s_SlotVA[B.slot], o->nPages);
		o->bProgram = FALSE;
		BufFreeIfUnused (o);
	}
	TKPageAttr Attr = KPAGE_ATTR_APP_DATA;
	pProg->MapContig (s_SlotVA[B.slot], ulPhys, nPages, Attr);
	pProg->FlushTLB ();
	u64 ulSrvVA = USER_WS_BASE + (u64) n * USER_WS_SLOT;
	pSrv->MapContig (ulSrvVA, ulPhys, nPages, Attr);
	pSrv->FlushTLB ();
	b->pRaw = pRaw; b->ulPhys = ulPhys; b->nPages = nPages; b->nPid = B.pid; b->nSlot = B.slot;
	b->bProgram = TRUE; b->bServer = TRUE;
	B.id = (unsigned) n + 1; B.addr = ulSrvVA;
	if (!UserPut (pUser, B)) return -KAPI_EFAULT;		// (the buffer stays: the server may free it by its address)
	return 0;
}

static long BufFree (unsigned nId)
{
	if (nId == 0 || nId > USER_WS_SLOTS) return -KAPI_EINVAL;
	TWsBuf *b = &s_Buf[nId - 1];
	if (b->pRaw == 0 || !b->bServer) return -KAPI_EINVAL;
	CAddressSpace *pSrv = IpcFindAS (s_nServerPid);
	if (pSrv != 0) pSrv->UnmapContig (USER_WS_BASE + (u64) (nId - 1) * USER_WS_SLOT, b->nPages);
	b->bServer = FALSE;
	if (b->bProgram)
	{
		CAddressSpace *pProg = IpcFindAS (b->nPid);
		if (pProg != 0) pProg->UnmapContig (s_SlotVA[b->nSlot], b->nPages);
		b->bProgram = FALSE;
	}
	BufFreeIfUnused (b);
	return 0;
}

// ---- the programs' requests ----------------------------------------------------------------------
// (the user-space file systems' pattern, sys/vfs.cpp: the caller sleeps until the server answers)

#define WS_REQS		16
#define WS_REQ_TIMEOUT_US	(10 * 1000000u)

enum { WREQ_FREE, WREQ_PENDING, WREQ_TAKEN, WREQ_DONE };

struct TWsReq
{
	volatile int nState;
	unsigned nId, nPid;
	int	 nOp;
	long	 a[4];
	u8	 In[KAPI_WS_DATA_MAX];  unsigned nInLen;
	u8	 Out[KAPI_WS_DATA_MAX]; unsigned nOutLen;
	long	 nStatus;
	CSynchronizationEvent *pDone;
};

static TWsReq *s_pReq = 0;			// WS_REQS of them, made at the first request
static unsigned s_nNextReq = 1;

static boolean ReqPending (void)
{
	if (s_pReq == 0) return FALSE;
	for (unsigned i = 0; i < WS_REQS; i++) if (s_pReq[i].nState == WREQ_PENDING) return TRUE;
	return FALSE;
}

static long Call (struct kapi_ws_call *pUser)
{
	struct kapi_ws_call C;
	unsigned nPid = MyPid ();
	if (nPid == 0 || !UserGet (&C, pUser)) return -KAPI_EFAULT;
	if (!s_bOwned || s_nServerPid == 0 || nPid == s_nServerPid) return -KAPI_ESRCH;
	if (C.in_len > KAPI_WS_DATA_MAX) return -KAPI_EINVAL;
	if (s_pReq == 0)
	{
		s_pReq = new TWsReq[WS_REQS];
		if (s_pReq == 0) return -KAPI_ENOMEM;
		memset (s_pReq, 0, sizeof (TWsReq) * WS_REQS);
	}
	TWsReq *r = 0;
	for (unsigned i = 0; i < WS_REQS && r == 0; i++) if (s_pReq[i].nState == WREQ_FREE) r = &s_pReq[i];
	if (r == 0) return -KAPI_EAGAIN;
	if (C.in_len > 0 && !UserCopyIn (r->In, C.in, C.in_len)) return -KAPI_EFAULT;
	r->nInLen = C.in_len; r->nOutLen = 0; r->nStatus = -KAPI_EIO;
	r->nId = s_nNextReq++; if (s_nNextReq == 0) s_nNextReq = 1;
	r->nPid = nPid; r->nOp = C.op;
	for (int i = 0; i < 4; i++) r->a[i] = C.a[i];
	if (r->pDone == 0) r->pDone = new CSynchronizationEvent;
	r->pDone->Clear ();
	unsigned nId = r->nId, nServer = s_nServerPid;
	r->nState = WREQ_PENDING;
	IoWake ();						// (the server's KAPI_WS_WAIT)

	unsigned nStart = CTimer::Get ()->GetClockTicks ();
	while (r->nState != WREQ_DONE || r->nId != nId)
	{
		r->pDone->WaitWithTimeout (100000);		// 100 ms, then look again
		r->pDone->Clear ();
		if (r->nState == WREQ_DONE && r->nId == nId) break;
		if (   r->nId != nId || s_nServerPid != nServer || !s_bOwned
		    || CTimer::Get ()->GetClockTicks () - nStart > WS_REQ_TIMEOUT_US)
		{
			if (r->nId == nId) r->nState = WREQ_FREE;	// (the server gone, or too slow)
			return -KAPI_ESRCH;
		}
	}
	long nStatus = r->nStatus;
	unsigned nOut = r->nOutLen;
	boolean bOK = TRUE;
	if (nOut > 0 && C.out != 0 && C.out_cap > 0)
		bOK = UserCopyOut (C.out, r->Out, nOut < C.out_cap ? nOut : C.out_cap);
	r->nState = WREQ_FREE;
	C.out_len = nOut;
	if (!bOK || !UserPut (pUser, C)) return -KAPI_EFAULT;
	return nStatus;
}

static long Next (struct kapi_ws_req *pUser)
{
	if (s_pReq == 0 || pUser == 0 || !UserRange (pUser, sizeof *pUser)) return 0;
	for (unsigned i = 0; i < WS_REQS; i++)
	{
		TWsReq *r = &s_pReq[i];
		if (r->nState != WREQ_PENDING) continue;
		// (the head, then the bytes sent: not the whole 4 KB each time)
		struct { unsigned id, pid; int op; unsigned in_len; long a[4]; } Head;
		Head.id = r->nId; Head.pid = r->nPid; Head.op = r->nOp; Head.in_len = r->nInLen;
		for (int k = 0; k < 4; k++) Head.a[k] = r->a[k];
		if (!UserCopyOut (pUser, &Head, sizeof Head)) return -KAPI_EFAULT;
		if (r->nInLen > 0 && !UserCopyOut (pUser->data, r->In, r->nInLen)) return -KAPI_EFAULT;
		r->nState = WREQ_TAKEN;
		return 1;
	}
	return 0;
}

static long Reply (const struct kapi_ws_reply *pUser)
{
	struct kapi_ws_reply R;
	if (!UserGet (&R, pUser)) return -KAPI_EFAULT;
	unsigned nId = R.id, nLen = R.len;
	long nStatus = R.status;
	const void *pData = R.data;
	if (s_pReq == 0) return -KAPI_EINVAL;
	for (unsigned i = 0; i < WS_REQS; i++)
	{
		TWsReq *r = &s_pReq[i];
		if (r->nState != WREQ_TAKEN || r->nId != nId) continue;
		r->nOutLen = 0;
		r->nStatus = nStatus;
		if (pData != 0 && nLen > 0)
		{
			if (nLen > KAPI_WS_DATA_MAX || !UserCopyIn (r->Out, pData, nLen)) r->nStatus = -KAPI_EIO;
			else r->nOutLen = nLen;
		}
		r->nState = WREQ_DONE;
		r->pDone->Set ();
		return 0;
	}
	return -KAPI_EINVAL;				// (its caller is gone)
}

// A live process's name (its main task's), for the server's lists of the open programs.
static long ProcName (unsigned nPid, char *pUser, unsigned nCap)
{
	CAddressSpace *pAS = IpcFindAS (nPid);
	if (pAS == 0 || pAS->GetMainTask () == 0 || nCap == 0) return -KAPI_ESRCH;
	const char *pName = pAS->GetMainTask ()->GetName ();
	unsigned n = strlen (pName);
	if (n >= nCap) n = nCap - 1;
	char Buf[64];
	if (n >= sizeof Buf) n = sizeof Buf - 1;
	memcpy (Buf, pName, n); Buf[n] = '\0';
	return UserCopyOut (pUser, Buf, n + 1) ? (long) n : -KAPI_EFAULT;
}

// A program's pixels changed: an event for the server (one waiting already is enough).
static long Kick (void)
{
	unsigned nPid = MyPid ();
	if (!s_bOwned || ClientSlot (nPid) < 0) return -KAPI_ESRCH;
	struct kapi_ws_input Ev;
	memset (&Ev, 0, sizeof Ev);
	Ev.type = KAPI_WS_IN_KICK; Ev.a = (int) nPid;
	Push (Ev);
	CScheduler::Get ()->Yield ();			// (the server composes now)
	return (long) s_nServerPid;
}

// A process is gone (its teardown: interrupts masked, nothing may wait).
void WsOnProcessGone (unsigned nPid)
{
	if (nPid == 0) return;
	if (nPid == s_nServerPid)
	{
		s_nServerPid = 0;
		if (s_bBootMode) s_bRelaunch = TRUE;	// (WsPoll, the compositor's loop)
		if (s_bOwned)				// (no log here)
		{
			s_bOwned = FALSE;
			s_nHead = s_nTail = 0;
			ScreenDirty ();
		}
		for (int i = 0; i < USER_WS_SLOTS; i++)	// its programs keep their buffers
		{
			s_Buf[i].bServer = FALSE;
			BufFreeIfUnused (&s_Buf[i]);
		}
		if (s_pReq != 0)			// its callers fail
			for (unsigned i = 0; i < WS_REQS; i++)
				if (s_pReq[i].nState != WREQ_FREE && s_pReq[i].pDone != 0) s_pReq[i].pDone->Set ();
		return;
	}
	int nSlot = ClientSlot (nPid);
	if (nSlot < 0) return;
	s_Client[nSlot] = 0;
	for (int i = 0; i < USER_WS_SLOTS; i++)
	{
		if (s_Buf[i].pRaw == 0 || s_Buf[i].nPid != nPid) continue;
		s_Buf[i].bProgram = FALSE;		// (its address space goes with it)
		BufFreeIfUnused (&s_Buf[i]);
	}
	if (s_pReq != 0)				// a request it was waiting for: nobody to answer
		for (unsigned i = 0; i < WS_REQS; i++)
			if (s_pReq[i].nPid == nPid && s_pReq[i].nState == WREQ_PENDING) s_pReq[i].nState = WREQ_FREE;
	struct kapi_ws_input Ev;
	memset (&Ev, 0, sizeof Ev);
	Ev.type = KAPI_WS_IN_GONE; Ev.a = (int) nPid;
	Push (Ev);
}

// ---- the start ------------------------------------------------------------------------------------
// The one-boot trial (as the network's, sys/net.cpp): SD:/etc/elegant.trial is there -> it is
// removed (the next start is the kernel's window manager's again, whatever happens), the graphics
// server is started before init and waited for: the programs init starts have their windows there.
#define WS_TRIAL_FILE	"SD:/etc/elegant.trial"
#define WS_ON_FILE	"SD:/etc/elegant.on"
#define WS_SERVER_PATH	"SD:bin/elegant"

void WsBootStart (void)
{
	FIL File;
	boolean bTrial = f_open (&File, WS_TRIAL_FILE, FA_READ) == FR_OK;
	if (bTrial)
	{
		f_close (&File);
		if (f_unlink (WS_TRIAL_FILE) != FR_OK)		// (it must not come back at the next boot)
		{
			CLogger::Get ()->Write ("wsrv", LogWarning, "the trial file cannot be removed: no trial");
			bTrial = FALSE;
		}
	}
	// ... or every start (SD:/etc/elegant.on: kept; removed by hand to go back -- `rm etc/elegant.on`
	// over telnet --, which a start without the server leaves possible)
	boolean bOn = f_open (&File, WS_ON_FILE, FA_READ) == FR_OK;
	if (bOn) f_close (&File);
	if (!bTrial && !bOn) return;
	s_bBootMode = TRUE;
	if (!ExecPath (WS_SERVER_PATH, "--serve"))
	{
		CLogger::Get ()->Write ("wsrv", LogWarning, "cannot start " WS_SERVER_PATH);
		return;
	}
	for (unsigned t = 0; t < 250 && !s_bOwned; t++) CScheduler::Get ()->MsSleep (20);	// 5 s
	CLogger::Get ()->Write ("wsrv", s_bOwned ? LogNotice : LogWarning,
				s_bOwned ? "the graphics server has the display" : "the graphics server did not take the display: the kernel's window manager");
}

extern "C" long kapi_ws_ctl (int nOp, long a0, long a1, long a2)
{
	if (nOp == KAPI_WS_ACTIVE) return s_bOwned ? (long) s_nServerPid : 0;
	if (nOp == KAPI_WS_REGISTER) return Register ();
	if (nOp == KAPI_WS_CALL) return Call ((struct kapi_ws_call *) a0);
	if (nOp == KAPI_WS_KICK) return Kick ();
	if (!IsServer ()) return -KAPI_EPERM;
	s_nLastCall = CTimer::Get ()->GetTicks ();
	switch (nOp)
	{
	case KAPI_WS_DISPLAY:	return Display (a0 != 0, (struct kapi_ws_display *) a1);
	case KAPI_WS_PRESENT:	return Present ((const struct kapi_ws_present *) a0);
	case KAPI_WS_INPUT:	return Input ((struct kapi_ws_input *) a0, a1);
	case KAPI_WS_WAIT:	return Wait ((unsigned) a0);
	case KAPI_WS_ATTACH:	return Attach ((unsigned) a0);
	case KAPI_WS_POST:	return Post ((unsigned) a0, (const struct kapi_event *) a1);
	case KAPI_WS_EXIT:	return ExitRequest ((unsigned) a0);
	case KAPI_WS_BUF_MAP:	return BufMap ((struct kapi_ws_buf *) a0);
	case KAPI_WS_BUF_FREE:	return BufFree ((unsigned) a0);
	case KAPI_WS_NEXT:	return Next ((struct kapi_ws_req *) a0);
	case KAPI_WS_REPLY:	return Reply ((const struct kapi_ws_reply *) a0);
	case KAPI_WS_FOCUS:	s_nFocusPid = (unsigned) a0; return 0;
	case KAPI_WS_PROC_NAME:	return ProcName ((unsigned) a0, (char *) a1, (unsigned) a2);
	case KAPI_WS_STATE:	return State ((unsigned) a0, (void *) a1, a2 != 0);
	}
	return -KAPI_ENOSYS;
}
