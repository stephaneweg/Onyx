//
// window.h
//
// Minimal window + compositor for the windowed-process GUI (see ARCHITECTURE.md
// §10). A CWindow owns a client-area GImage ("canvas") that its owner (a kernel
// thread now, an EL0 process later) draws into. The CWindowManager composites all
// windows (frame + title bar + canvas) onto the screen GImage; a compositor thread
// presents the result via C2DGraphics. Widgets/text come in later layers.
//
#ifndef _kern_gui_window_h
#define _kern_gui_window_h

#include <kern/gui/gimage.h>
#include <circle/types.h>

// Theme (from SimpleOS): blue title bar.
#define WIN_TITLEBAR_H		24
#define WIN_BORDER		2
#define WIN_COLOR_FRAME		0x00202028
#define WIN_COLOR_TITLE		0x000000AA
#define WIN_COLOR_TITLE_ACT	0x000000FF
#define WIN_COLOR_DESKTOP	0x00204060

#define WM_MAX_WINDOWS		16

class CWindow
{
public:
	// Create a window whose top-left (including title bar) is at (x,y); the client
	// canvas is nClientW x nClientH. The canvas buffer is allocated here; if it is
	// to be shared with an EL0 process, the loader maps this buffer into the
	// process's address space (drawing model: shared buffer + present).
	CWindow (int x, int y, int nClientW, int nClientH, const char *pTitle);
	~CWindow (void);

	boolean IsValid (void) const	{ return m_Canvas.IsValid (); }

	GImage *Canvas (void)		{ return &m_Canvas; }	// the client-area buffer
	u32 *CanvasBuffer (void)	{ return m_Canvas.Buffer (); }
	int ClientWidth (void) const	{ return m_Canvas.Width (); }
	int ClientHeight (void) const	{ return m_Canvas.Height (); }

	int X (void) const		{ return m_nX; }
	int Y (void) const		{ return m_nY; }
	void Move (int x, int y)	{ m_nX = x; m_nY = y; }

	// Blit frame + title bar + canvas onto the screen image.
	void DrawTo (GImage *pScreen, boolean bActive);

private:
	int		m_nX;		// outer position (title bar top-left)
	int		m_nY;
	const char     *m_pTitle;
	GImage		m_Canvas;	// client-area pixel buffer (owned)
};

class CWindowManager
{
public:
	CWindowManager (void);

	static CWindowManager *Get (void)	{ return s_pThis; }

	// Register a window (the topmost added is drawn last = on top + active).
	void Add (CWindow *pWindow);
	void Remove (CWindow *pWindow);

	// Clear the desktop and draw every window onto the screen image.
	void Composite (GImage *pScreen);

private:
	CWindow	  *m_pWindows[WM_MAX_WINDOWS];
	unsigned   m_nWindows;

	static CWindowManager *s_pThis;
};

#endif
