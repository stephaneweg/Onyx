//
// debugcon.h
//
// On-screen debug console for post-mortem visibility. Once the compositor owns
// the display, the boot console (m_Screen) is no longer scanned out, so logger
// output and exception dumps are invisible. When an app exits we "take over" the
// screen: stop the compositor, clear the displayed framebuffer, and route ALL
// logger output to it -- so every message (and the last line before a hang) is
// visible. See kapi_exit() for the trigger.
//
#ifndef _kern_debugcon_h
#define _kern_debugcon_h

#include <circle/device.h>
#include <circle/types.h>

class C2DGraphics;

// A text console that renders logger output directly onto the displayed
// framebuffer (the compositor's C2DGraphics back buffer + UpdateDisplay), with a
// software cursor + scrolling. Presents after each Write -> no hidden buffering.
class CFbConsole : public CDevice
{
public:
	CFbConsole (C2DGraphics *p2D);

	void Clear (void);				// wipe + home the cursor
	int Write (const void *pBuffer, size_t nCount) override;

private:
	C2DGraphics *m_p2D;
	int	     m_nX;			// text cursor (pixels)
	int	     m_nY;
};

// A CDevice that forwards logger writes to one of two targets: the normal boot
// console, or (after takeover) the framebuffer console. The logger points here.
class CLogSwitch : public CDevice
{
public:
	CLogSwitch (void);

	void SetNormal (CDevice *pNormal)	{ m_pNormal = pNormal; }
	void SwitchToDebug (CDevice *pDebug)	{ m_pDebug = pDebug; m_bDebug = TRUE; }

	int Write (const void *pBuffer, size_t nCount) override;

private:
	CDevice		*m_pNormal;
	CDevice		*m_pDebug;
	volatile boolean m_bDebug;
};

// Wiring (called once from CKernel) + the app-exit takeover + the compositor query.
void DebugConsoleRegister (CLogSwitch *pSwitch, CFbConsole *pConsole);
void DebugConsoleTakeover (void);		// called at app exit (kapi_exit)
boolean DebugConsoleActive (void);		// the compositor stops when TRUE

#endif // _kern_debugcon_h
