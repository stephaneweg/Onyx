//
// window.h -- what the kernel keeps of the windows (2026-10-05).
//
// The windows are no longer the kernel's: the window manager, the compositor and the routing of
// the input are Elegant's, the graphics server, a user process (user/Servers/elegant; its window
// manager is the one that was here: user/Servers/elegant/wm). The kernel gives it mechanisms
// (kern/wsrv.h) and keeps, here:
//
//  - CWindow: what a program's pump reads -- its queue of events, the request to end, the wake of
//    kapi_pump_wait, the modifiers of the key being handled. One a program whose windows are
//    Elegant's ("attached": KAPI_WS_ATTACH); Elegant puts the events in it (KAPI_WS_POST). The name
//    is historical: it holds no pixels and is on no screen.
//  - CWindowManager: the keys' state (the modifiers, the held keys: kapi_get_modifiers,
//    kapi_key_held) and the full screen (the program that has it, its buffer: kapi_fullscreen_begin,
//    kapi_present_fb -- the kernel shows that buffer itself, no round trip a frame).
//  - the screen's size and its change (kapi_screen_set), the display's sharing between the tasks
//    that send to it.
//
#ifndef _kern_gui_window_h
#define _kern_gui_window_h

#include <kern/gui/gimage.h>
#include <kern/kapi_abi.h>
#include <circle/spinlock.h>
#include <circle/types.h>

class CSynchronizationEvent;

// Default framebuffer size; overridable at boot via cmdline.txt (width=/height=). The size in use
// is g_nScreenWidth / g_nScreenHeight (set at boot, changed by ScreenResizeRequest).
#define SCREEN_WIDTH		1024
#define SCREEN_HEIGHT		768
extern int g_nScreenWidth;
extern int g_nScreenHeight;

// A new screen size, now (kapi_screen_set): done by the display task between two frames
// (C2DGraphics::Resize: the firmware gives a frame buffer of that size), then the graphics server is
// told. Waits for it -> 0; -1 a size out of bounds; -2 not now (a full-screen program owns the
// display, the debug console); -3 the firmware refused it (the old size kept). (kernel.cpp)
#define SCREEN_MIN_W	640
#define SCREEN_MIN_H	480
#define SCREEN_MAX_W	2560
#define SCREEN_MAX_H	1600
int ScreenResizeRequest (int nW, int nH);

// Before a task sends a frame to the display by itself (C2DGraphics::UpdateDisplay): waits,
// yielding, until another task's display DMA is over. Task context only. (kernel.cpp)
void DisplayPresentIdle (void);

// The screen changed (a present of the graphics server's, of a full-screen program's): g_nScreenGen
// is bumped, so that kapi_screen_grab can tell "unchanged".
extern volatile unsigned g_nScreenGen;
void ScreenDirty (void);

#define WIN_EVENT_QUEUE		64		// (v94; 32 before)

// Window flags and event kinds: the programs' (kern/kapi_abi.h has the frame's metrics; these are
// kept numerically identical to user/Kits/appkit/appkit.h).
#define WIN_FLAG_BORDERLESS	(1u << 0)
#define GUI_EVENT_KEY		5
#define MOD_CTRL		1
#define MOD_SHIFT		2
#define MOD_ALT			4

// Logical key codes (the held keys: kapi_key_held).
#define KEY_ENTER		13
#define KEY_UP			0x100
#define KEY_DOWN		0x101
#define KEY_LEFT		0x102
#define KEY_RIGHT		0x103
#define KEY_DEL			0x108

// An event queued for a program's pump.
struct GUIEvent
{
	u64	ulHandler;		// the program's function to call
	u64	ulSender;
	int	nEvent;			// GUI_EVENT_*
	long	lValue;
	unsigned nMods = 0;		// GUI_EVENT_KEY: MOD_* held when the key was typed
};

class CWindow
{
public:
	CWindow (void);

	void SetOwnerPid (unsigned nPid)	{ m_nOwnerPid = nPid; }
	unsigned OwnerPid (void) const		{ return m_nOwnerPid; }

	// The queue: the graphics server pushes (KAPI_WS_POST), the program's pump pops.
	void PushEvent (const GUIEvent &Event);
	boolean PopEvent (GUIEvent *pEvent);
	unsigned QueuedEvents (void) const	{ return (m_nEvHead + WIN_EVENT_QUEUE - m_nEvTail) % WIN_EVENT_QUEUE; }
	unsigned DroppedEvents (void) const	{ return m_nEvDropped; }
	unsigned LastPumpTicks (void) const	{ return m_nLastPump; }	// (0: never pumped)

	// Modifiers of the key event being handled right now (the user-side pump sets it around the
	// program's key handler; kapi_get_modifiers reports it), 0xFFFFFFFF = none.
	volatile unsigned m_nKeyEventMods = 0xFFFFFFFF;

	void RequestExit (void);
	// Pulsed on every event pushed and on RequestExit: the owner's kapi_pump_wait sleeps on it. 0: none.
	void SetWake (CSynchronizationEvent *pEv)	{ m_pWake = pEv; }
	boolean ShouldExit (void) const		{ return m_bExitRequested; }

private:
	unsigned	m_nOwnerPid;
	GUIEvent	m_Events[WIN_EVENT_QUEUE];
	volatile unsigned m_nEvHead, m_nEvTail;
	CSpinLock	m_EvLock;
	volatile unsigned m_nEvDropped;
	CSynchronizationEvent *m_pWake;
	volatile unsigned m_nLastPump;
	volatile boolean m_bExitRequested;
};

#define HELD_KEYS	0x110		// logical key codes tracked as "held" (< KEY_DEL + 8)
#define HELD_WORDS	((HELD_KEYS + 31) / 32)

class CWindowManager
{
public:
	CWindowManager (void);
	static CWindowManager *Get (void)	{ return s_pThis; }

	// A program's CWindow is going (its process ends): the full screen it had is given back.
	void Remove (CWindow *pWindow);

	// The keys' state: the modifiers (MOD_*), the held keys -- the USB keyboards' raw report
	// (usage codes, which replace the previous USB set), the injected ones (vncd, rdpd). Who has
	// the keyboard is the graphics server's to say (kern/wsrv.h WsFocusPid).
	void SetModifiers (unsigned nMods)	{ m_nModifiers = nMods; }
	unsigned Modifiers (void) const		{ return m_nModifiers; }
	void SetUsbHeld (const unsigned char RawKeys[6]);
	void SetInjectedHeld (int nKey, boolean bDown);
	boolean KeyHeldAny (int nKey);

	// The full screen: the program that has it (the kernel shows its buffer; the graphics server
	// sends it all the input and shows nothing meanwhile), the buffer.
	void SetFullscreen (CWindow *pWindow)	{ m_pFullscreen = pWindow; ScreenDirty (); }
	CWindow *FullscreenWindow (void) const	{ return m_pFullscreen; }
	u32 *EnsureFullscreenBuffer (int nW, int nH, u64 *pPhys, unsigned *pnPages);
	u32 *FullscreenBuffer (void) const	{ return (u32 *) m_ulFsPhys; }

	// The display task is alive (the hang watchdog reads FrameCount).
	unsigned FrameCount (void) const	{ return m_nFrames; }
	void CompositorAlive (void)		{ m_nFrames++; }

private:
	volatile unsigned m_nFrames;
	CWindow	  * volatile m_pFullscreen;
	u8	  *m_pFsRaw;		// the full-screen back buffer (64 KB aligned)
	u64	   m_ulFsPhys;
	unsigned   m_nFsPages;
	u32	   m_UsbHeld[HELD_WORDS];
	u32	   m_VncHeld[HELD_WORDS];
	volatile unsigned m_nModifiers;

	static CWindowManager *s_pThis;
};

#endif
