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
#include <circle/spinlock.h>
#include <circle/types.h>

// Screen dimensions (the framebuffer we request). Shared by the kernel + kapi.
#define SCREEN_WIDTH		640
#define SCREEN_HEIGHT		480

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

	// The canvas is a page-aligned, physically-contiguous region (identity-mapped
	// in the kernel) so it can be both composited here and mapped into a process's
	// address space (shared-buffer drawing model). PA == kernel VA (identity).
	u64 CanvasPhys (void) const	{ return m_ulCanvasPhys; }
	unsigned CanvasPages (void) const { return m_nCanvasPages; }

	int X (void) const		{ return m_nX; }
	int Y (void) const		{ return m_nY; }
	void Move (int x, int y)	{ m_nX = x; m_nY = y; }

	// Blit frame + title bar + canvas onto the screen image.
	void DrawTo (GImage *pScreen, boolean bActive);

private:
	int		m_nX;		// outer position (title bar top-left)
	int		m_nY;
	char		m_Title[48];	// owned copy of the title (caller's may be transient)
	GImage		m_Canvas;	// client-area pixel buffer (wraps m_pRawAlloc)
	void	       *m_pRawAlloc;	// the over-allocated block (freed on destroy)
	u64		m_ulCanvasPhys;	// 64 KB-aligned start of the canvas (== kernel VA)
	unsigned	m_nCanvasPages;	// 64 KB pages spanned by the canvas
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

	// Mouse input (called from the input thread). Cursor at (x,y); buttons is a
	// bitmask (bit0 = left). Handles raise-on-click and title-bar dragging.
	void OnMouse (int x, int y, unsigned nButtons);

private:
	// Hit-test top-down; returns the topmost window containing (x,y) and whether the
	// hit landed on its title bar. Caller must hold m_SpinLock. Returns ~0u if none.
	unsigned HitTest (int x, int y, boolean *pbOnTitleBar);

	CWindow	  *m_pWindows[WM_MAX_WINDOWS];
	unsigned   m_nWindows;

	// Cursor + drag state (mutated from the input thread, read by Composite).
	int	   m_nCursorX;
	int	   m_nCursorY;
	boolean	   m_bCursorShown;
	unsigned   m_nLastButtons;	// previous button bitmask (for press/release edges)
	CWindow	  *m_pDragWindow;	// window being dragged by its title bar, or 0
	int	   m_nDragDX;		// cursor-to-window offset captured at drag start
	int	   m_nDragDY;

	// Protects the window list against concurrent Add (app threads) / Remove
	// (process teardown, in scheduler context) / Composite (compositor thread).
	// A spin lock, not a mutex: Remove runs in scheduler context where blocking
	// is illegal; the lock only ever masks IRQ briefly (Composite snapshots the
	// list under the lock, then blits outside it).
	CSpinLock  m_SpinLock;

	static CWindowManager *s_pThis;
};

#endif
