//
// kwin.cpp -- what the kernel keeps of the windows (kern/gui/window.h): a program's queue of events,
// the keys' state, the full screen. The window manager itself is the graphics server's (user/Servers/common/wm).
//
#include <kern/gui/window.h>
#include <kern/layout.h>		// KPAGE_SIZE / KPAGE_MASK
#include <circle/util.h>
#include <circle/timer.h>
#include <circle/new.h>
#include <circle/sched/synchronizationevent.h>
#include <assert.h>

int g_nScreenWidth  = SCREEN_WIDTH;
int g_nScreenHeight = SCREEN_HEIGHT;
volatile unsigned g_nScreenGen = 1;

void ScreenDirty (void)
{
	g_nScreenGen++;
}

// ---- a program's queue of events ------------------------------------------------------------------

CWindow::CWindow (void)
:	m_nOwnerPid (0), m_nEvHead (0), m_nEvTail (0), m_nEvDropped (0), m_pWake (0), m_nLastPump (0),
	m_bExitRequested (FALSE)
{
}

void CWindow::PushEvent (const GUIEvent &Event)
{
	m_EvLock.Acquire ();
	unsigned nNext = (m_nEvHead + 1) % WIN_EVENT_QUEUE;
	if (nNext != m_nEvTail)			// drop if the ring is full
	{
		m_Events[m_nEvHead] = Event;
		m_nEvHead = nNext;
	}
	else
	{
		m_nEvDropped++;
	}
	m_EvLock.Release ();
	if (m_pWake != 0) { m_pWake->Set (); m_pWake->Clear (); }	// (wakes the owner's kapi_pump_wait)
}

void CWindow::RequestExit (void)
{
	m_bExitRequested = TRUE;
	if (m_pWake != 0) { m_pWake->Set (); m_pWake->Clear (); }
}

boolean CWindow::PopEvent (GUIEvent *pEvent)
{
	boolean bGot = FALSE;
	m_nLastPump = CTimer::Get ()->GetTicks ();	// the owner is alive and pumping
	m_EvLock.Acquire ();
	if (m_nEvTail != m_nEvHead)
	{
		*pEvent = m_Events[m_nEvTail];
		m_nEvTail = (m_nEvTail + 1) % WIN_EVENT_QUEUE;
		bGot = TRUE;
	}
	m_EvLock.Release ();
	return bGot;
}

// ---- the keys' state, the full screen ---------------------------------------------------------------

CWindowManager *CWindowManager::s_pThis = 0;

CWindowManager::CWindowManager (void)
:	m_nFrames (0), m_pFullscreen (0), m_pFsRaw (0), m_ulFsPhys (0), m_nFsPages (0), m_nModifiers (0)
{
	assert (s_pThis == 0);
	s_pThis = this;
	for (unsigned i = 0; i < HELD_WORDS; i++) { m_UsbHeld[i] = 0; m_VncHeld[i] = 0; }
}

void CWindowManager::Remove (CWindow *pWindow)
{
	if (pWindow != 0 && m_pFullscreen == pWindow) { m_pFullscreen = 0; ScreenDirty (); }	// its program ended
}

// USB HID usage -> logical key (letters by their US-layout position).
static int UsageToKey (unsigned char u)
{
	if (u >= 0x04 && u <= 0x1D) return 'a' + (u - 0x04);
	if (u >= 0x1E && u <= 0x26) return '1' + (u - 0x1E);
	switch (u)
	{
	case 0x27: return '0';
	case 0x28: case 0x58: return KEY_ENTER;
	case 0x29: return 27;
	case 0x2C: return ' ';
	case 0x4F: return KEY_RIGHT;
	case 0x50: return KEY_LEFT;
	case 0x51: return KEY_DOWN;
	case 0x52: return KEY_UP;
	}
	return 0;
}

void CWindowManager::SetUsbHeld (const unsigned char RawKeys[6])
{
	u32 Held[HELD_WORDS];
	for (unsigned i = 0; i < HELD_WORDS; i++) Held[i] = 0;
	for (unsigned i = 0; i < 6; i++)
	{
		int k = UsageToKey (RawKeys[i]);
		if (k > 0 && k < HELD_KEYS) Held[k >> 5] |= 1u << (k & 31);
	}
	for (unsigned i = 0; i < HELD_WORDS; i++) m_UsbHeld[i] = Held[i];
}

void CWindowManager::SetInjectedHeld (int nKey, boolean bDown)
{
	if (nKey >= 'A' && nKey <= 'Z') nKey += 'a' - 'A';
	if (nKey <= 0 || nKey >= HELD_KEYS) return;
	if (bDown) m_VncHeld[nKey >> 5] |= 1u << (nKey & 31);
	else       m_VncHeld[nKey >> 5] &= ~(1u << (nKey & 31));
}

boolean CWindowManager::KeyHeldAny (int nKey)
{
	if (nKey >= 'A' && nKey <= 'Z') nKey += 'a' - 'A';
	if (nKey == '\n' || nKey == '\r') nKey = KEY_ENTER;
	if (nKey <= 0 || nKey >= HELD_KEYS) return FALSE;
	return ((m_UsbHeld[nKey >> 5] | m_VncHeld[nKey >> 5]) >> (nKey & 31)) & 1 ? TRUE : FALSE;
}

u32 *CWindowManager::EnsureFullscreenBuffer (int nW, int nH, u64 *pPhys, unsigned *pnPages)
{
	unsigned nBytes = (unsigned) (nW * nH) * sizeof (u32);
	unsigned nNeed = (nBytes + KPAGE_MASK) / KPAGE_SIZE;
	if (nNeed == 0) nNeed = 1;
	if (m_pFsRaw != 0 && nNeed > m_nFsPages)
	{
		// The screen grew since the buffer was made (kapi_screen_set): a new one of the new size,
		// else fullscreen_begin's clear, the program and present_fb run past its end. The old one
		// is left allocated: a program may still have it mapped.
		m_pFsRaw = 0; m_ulFsPhys = 0; m_nFsPages = 0;
	}
	if (m_pFsRaw == 0)
	{
		m_nFsPages = nNeed;
		m_pFsRaw = new u8[m_nFsPages * KPAGE_SIZE + KPAGE_SIZE];
		if (m_pFsRaw == 0) { m_nFsPages = 0; return 0; }
		m_ulFsPhys = ((uintptr) m_pFsRaw + KPAGE_MASK) & ~((uintptr) KPAGE_MASK);
		memset ((void *) m_ulFsPhys, 0, m_nFsPages * KPAGE_SIZE);
	}
	if (pPhys)   *pPhys   = m_ulFsPhys;
	if (pnPages) *pnPages = m_nFsPages;
	return (u32 *) m_ulFsPhys;
}
