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
#include <circle/2dgraphics.h>
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

void WsWatch (void)
{
	if (!s_bOwned) return;
	if (CTimer::Get ()->GetTicks () - s_nLastCall > WS_SILENT_MS * HZ / 1000)
		Release ("the graphics server is silent");
}

void WsOnProcessGone (unsigned nPid)
{
	if (nPid == 0 || nPid != s_nServerPid) return;
	s_nServerPid = 0;
	if (s_bOwned)				// (no log here: interrupts are masked)
	{
		s_bOwned = FALSE;
		s_nHead = s_nTail = 0;
		ScreenDirty ();
	}
}

// ---- the raw input ------------------------------------------------------------------------------

// Queue one event (an interrupt is allowed). A pointer move after an unread pointer move with the
// same buttons replaces it; a full ring drops the event (counted).
static boolean Push (const struct kapi_ws_input &Ev)
{
	if (!s_bOwned) return FALSE;
	s_RingLock.Acquire ();
	unsigned nLast = (s_nHead + WS_RING - 1) % WS_RING;
	if (   Ev.type == KAPI_WS_IN_POINTER && Ev.a == 0 && s_nHead != s_nTail
	    && s_Ring[nLast].type == KAPI_WS_IN_POINTER && s_Ring[nLast].a == 0
	    && s_Ring[nLast].buttons == Ev.buttons)
	{
		s_Ring[nLast] = Ev;
	}
	else
	{
		unsigned nNext = (s_nHead + 1) % WS_RING;
		if (nNext == s_nTail) s_nDropped++;
		else { s_Ring[s_nHead] = Ev; s_nHead = nNext; }
	}
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

static long Wait (unsigned nTimeoutMs)
{
	if (nTimeoutMs > WS_WAIT_MAX_MS) nTimeoutMs = WS_WAIT_MAX_MS;
	u32 nGen = IoGen ();			// (taken before the look: a push after it changes it)
	if (s_nHead == s_nTail && nTimeoutMs != 0) IoWait (nGen, nTimeoutMs);
	s_nLastCall = CTimer::Get ()->GetTicks ();
	return s_nHead != s_nTail ? KAPI_WS_PENDING_INPUT : 0;
}

extern "C" long kapi_ws_ctl (int nOp, long a0, long a1, long a2)
{
	(void) a2;
	if (nOp == KAPI_WS_ACTIVE) return s_bOwned ? (long) s_nServerPid : 0;
	if (nOp == KAPI_WS_REGISTER) return Register ();
	if (!IsServer ()) return -KAPI_EPERM;
	s_nLastCall = CTimer::Get ()->GetTicks ();
	switch (nOp)
	{
	case KAPI_WS_DISPLAY:	return Display (a0 != 0, (struct kapi_ws_display *) a1);
	case KAPI_WS_PRESENT:	return Present ((const struct kapi_ws_present *) a0);
	case KAPI_WS_INPUT:	return Input ((struct kapi_ws_input *) a0, a1);
	case KAPI_WS_WAIT:	return Wait ((unsigned) a0);
	}
	return -KAPI_ENOSYS;
}
