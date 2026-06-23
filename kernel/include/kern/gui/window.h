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

// Theme + chrome metrics. Match the window skin (wings.bmp, margins 7/7/32/7):
// a 32 px title-bar region on top and 7 px borders. Used both skinned and flat.
#define WIN_TITLEBAR_H		32
#define WIN_BORDER		7
#define WIN_COLOR_FRAME		0x00202028
#define WIN_COLOR_TITLE		0x000000AA
#define WIN_COLOR_TITLE_ACT	0x000000FF
#define WIN_COLOR_DESKTOP	0x00204060

#define WM_MAX_WINDOWS		16
#define WIN_MAX_WIDGETS		40	// calculator needs a full scientific button grid
#define WIN_EVENT_QUEUE		32

// Window creation flags (kept numerically identical to user/kapi.h).
#define WIN_FLAG_BORDERLESS	(1u << 0)	// no title bar / border / close box

// Widget kinds.
#define GW_BUTTON		1	// fires CLICK on release-inside
#define GW_LABEL		2	// static text
#define GW_CHECKBOX		3	// box + label, toggles on click
#define GW_TEXTBOX		4	// editable single-line text (keyboard focus)
#define GW_PROGRESS		5	// progress bar (nState = 0..100, display only)
#define GW_SLIDER		6	// horizontal slider (nState = 0..100, draggable)
#define GW_TEXTAREA		7	// editable multi-line text (focus; nState = top line)
#define GW_SCROLLV		8	// vertical scrollbar   (nState = 0..100, draggable)
#define GW_SCROLLH		9	// horizontal scrollbar (nState = 0..100, draggable)
#define GW_ICON			10	// clickable image (pIcon) + optional label; fires CLICK
					// (nState != 0 => draw an "open/running" triangle badge)

// Event kinds delivered to an app's pump. Kept numerically identical to the
// values in user/kapi.h so the app and the kernel agree.
#define GUI_EVENT_CLICK		1
#define GUI_EVENT_CHECK_CHANGED	2
#define GUI_EVENT_TEXT_CHANGED	3
#define GUI_EVENT_VALUE_CHANGED	4	// slider moved (lValue = 0..100)
#define GUI_EVENT_KEY		5	// key pressed (lValue = char or KEY_* code)
#define GUI_EVENT_CANVAS_CLICK	6	// click in the client area, no widget hit
					// (lValue = (clientX << 16) | clientY)

// Logical key codes delivered as GUI_EVENT_KEY lValue. Printable keys are their
// ASCII value (32..126); these are the special keys (Circle cooked-mode escapes).
#define KEY_BACKSPACE		8
#define KEY_TAB			9
#define KEY_ENTER		13
#define KEY_UP			0x100
#define KEY_DOWN		0x101
#define KEY_LEFT		0x102
#define KEY_RIGHT		0x103
#define KEY_HOME		0x104
#define KEY_END			0x105
#define KEY_PGUP		0x106
#define KEY_PGDN		0x107
#define KEY_DEL			0x108

#define GW_TEXT_MAX		48	// label / textbox content capacity

// A kernel-owned widget belonging to a CWindow. The compositor draws it (over
// the app's canvas) and the window manager hit-tests it. The handler is an app
// callback ADDRESS (opaque to the kernel): void (*)(unsigned long sender,
// int event, long value). The kernel never calls it -- it enqueues an event to
// the owning app, whose pump dispatches it in the app's own context.
#define GW_AREA_CAP		512	// textarea content capacity (heap-allocated)

struct GWidget
{
	int	 nType;
	int	 nX, nY, nW, nH;	// relative to the client (canvas) origin
	char	 Label[GW_TEXT_MAX];	// button/checkbox label, or textbox content
	char	*pText;			// textarea content (heap, GW_AREA_CAP); else 0
	void	*pIcon;			// GW_ICON: heap GImage* (owns its pixels); else 0
	u64	 ulHandler;		// app callback address
	int	 nState;		// checkbox: 0/1; slider/progress/scrollbar: 0..100
	boolean	 bMouseOver;		// cursor is over this widget (hover)
	boolean	 bMousePressed;		// left button held down on this widget
	boolean	 bFocused;		// has keyboard focus (textbox / textarea)
	boolean	 bUsed;
};

// An event queued for the owning app's pump.
struct GUIEvent
{
	u64	ulHandler;		// callback to invoke (app address)
	u64	ulSender;		// widget handle the app received (a GWidget *)
	int	nEvent;			// GUI_EVENT_*
	long	lValue;			// event payload (0 for a click)
};

class CWindow
{
public:
	// Create a window whose top-left (including title bar) is at (x,y); the client
	// canvas is nClientW x nClientH. The canvas buffer is allocated here; if it is
	// to be shared with an EL0 process, the loader maps this buffer into the
	// process's address space (drawing model: shared buffer + present).
	// nFlags: WIN_FLAG_* (e.g. WIN_FLAG_BORDERLESS for a panel/popup).
	CWindow (int x, int y, int nClientW, int nClientH, const char *pTitle,
		 unsigned nFlags = 0);
	~CWindow (void);

	boolean IsValid (void) const	{ return m_Canvas.IsValid (); }

	// Chrome insets: a normal window has a title bar on top and borders around the
	// client; a borderless window has none (client area == whole window). The WM and
	// the renderer use these instead of the raw WIN_* constants so both kinds work.
	boolean Borderless (void) const	{ return (m_nFlags & WIN_FLAG_BORDERLESS) != 0; }
	int ChromeL (void) const	{ return Borderless () ? 0 : WIN_BORDER; }
	int ChromeR (void) const	{ return Borderless () ? 0 : WIN_BORDER; }
	int ChromeT (void) const	{ return Borderless () ? 0 : WIN_TITLEBAR_H; }
	int ChromeB (void) const	{ return Borderless () ? 0 : WIN_BORDER; }

	GImage *Canvas (void)		{ return &m_Canvas; }	// the client-area buffer
	u32 *CanvasBuffer (void)	{ return m_Canvas.Buffer (); }

	// The client (composited + hit-tested) size. This is the LOGICAL size, which may
	// be smaller than the allocated canvas: a window can shrink/grow within its
	// over-allocated buffer (e.g. the taskbar panel resizing as apps open/close)
	// without reallocating or remapping. The app keeps drawing into the full canvas
	// (row stride = allocated width); only the top-left logical area is shown.
	int ClientWidth (void) const	{ return m_nLogicalW; }
	int ClientHeight (void) const	{ return m_nLogicalH; }

	// Resize the logical client area (clamped to the allocated canvas). Width/height
	// in pixels; the canvas buffer is not touched.
	void SetLogicalSize (int w, int h);

	// The canvas is a page-aligned, physically-contiguous region (identity-mapped
	// in the kernel) so it can be both composited here and mapped into a process's
	// address space (shared-buffer drawing model). PA == kernel VA (identity).
	u64 CanvasPhys (void) const	{ return m_ulCanvasPhys; }
	unsigned CanvasPages (void) const { return m_nCanvasPages; }

	int X (void) const		{ return m_nX; }
	int Y (void) const		{ return m_nY; }
	void Move (int x, int y)	{ m_nX = x; m_nY = y; }

	// Blit frame + title bar + close box + canvas + widgets onto the screen image.
	void DrawTo (GImage *pScreen, boolean bActive);

	// --- widgets ---------------------------------------------------------
	// Add a widget (coords relative to the client area). Returns the widget
	// (the app uses the pointer as an opaque handle / event sender), or 0 if full.
	GWidget *AddWidget (int nType, int x, int y, int w, int h,
			    const char *pLabel, u64 ulHandler);

	// Hit-test widgets at client-relative (cx,cy). Returns the widget or 0.
	GWidget *HitWidget (int cx, int cy);

	// --- event queue (WM pushes, app pump pops) --------------------------
	void PushEvent (const GUIEvent &Event);
	boolean PopEvent (GUIEvent *pEvent);

	// --- keyboard --------------------------------------------------------
	// An app-level key handler (callback address). When this window is topmost and
	// no editable widget is focused, the WM posts GUI_EVENT_KEY events here. Used by
	// app-drawn UIs that manage their own text (e.g. the editor).
	void SetKeyHandler (u64 ulHandler)	{ m_ulKeyHandler = ulHandler; }
	u64  KeyHandler (void) const		{ return m_ulKeyHandler; }

	// App-level click handler: GUI_EVENT_CANVAS_CLICK with client coords when a
	// press lands in the client area on no widget (for app-drawn mouse UIs).
	void SetClickHandler (u64 ulHandler)	{ m_ulClickHandler = ulHandler; }
	u64  ClickHandler (void) const		{ return m_ulClickHandler; }

	// --- lifecycle -------------------------------------------------------
	void RequestExit (void)		{ m_bExitRequested = TRUE; }
	boolean ShouldExit (void) const	{ return m_bExitRequested; }

	// Close box hit-test (screen coords). True if (sx,sy) is on the [x] box.
	boolean HitCloseBox (int sx, int sy) const;

private:
	void CloseBoxRect (int *px0, int *py0, int *px1, int *py1) const;

	int		m_nX;		// outer position (title bar top-left)
	int		m_nY;
	unsigned	m_nFlags;	// WIN_FLAG_* (borderless, ...)
	int		m_nLogicalW;	// composited/hit-tested client size (<= canvas alloc)
	int		m_nLogicalH;
	char		m_Title[48];	// owned copy of the title (caller's may be transient)
	GImage		m_Canvas;	// client-area pixel buffer (wraps m_pRawAlloc)
	void	       *m_pRawAlloc;	// the over-allocated block (freed on destroy)
	u64		m_ulCanvasPhys;	// 64 KB-aligned start of the canvas (== kernel VA)
	unsigned	m_nCanvasPages;	// 64 KB pages spanned by the canvas

	u64		m_ulKeyHandler;	// app key callback (GUI_EVENT_KEY), or 0
	u64		m_ulClickHandler; // app canvas-click callback, or 0

	GWidget		m_Widgets[WIN_MAX_WIDGETS];
	unsigned	m_nWidgets;

	// Event ring: the WM (input thread) pushes, the owning app's pump pops.
	GUIEvent	m_Events[WIN_EVENT_QUEUE];
	volatile unsigned m_nEvHead;	// next slot to write
	volatile unsigned m_nEvTail;	// next slot to read
	CSpinLock	m_EvLock;

	volatile boolean m_bExitRequested;
};

class CWindowManager
{
public:
	CWindowManager (void);

	static CWindowManager *Get (void)	{ return s_pThis; }

	// Register a window (the topmost added is drawn last = on top + active).
	void Add (CWindow *pWindow);
	void Remove (CWindow *pWindow);

	// Raise a window to the top of the z-order (e.g. a taskbar click). No-op if the
	// window isn't registered.
	void Raise (CWindow *pWindow);

	// Clear the desktop and draw every window onto the screen image.
	void Composite (GImage *pScreen);

	// Set the desktop wallpaper (takes ownership of pImage; deletes any previous).
	// Pass 0 to clear it (back to the solid desktop colour).
	void SetWallpaper (GImage *pImage);

	// Generate a screen-sized toroidal-Voronoi wallpaper (cellular "distance to the
	// nearest of nPoints seeds", tinted onto nBaseColor) and install it. Ported from
	// SimpleOS (temp/Background.bas). nSeed must be non-zero (the RNG seed).
	void GenerateWallpaper (u32 nBaseColor, int nPoints, unsigned nSeed);

	// Mouse input (called from the input thread). Cursor at (x,y); buttons is a
	// bitmask (bit0 = left). Handles raise-on-click, title-bar dragging, widget
	// hover/press/release (click fires on release-inside), and focus.
	void OnMouse (int x, int y, unsigned nButtons);

	// Keyboard input (called from the input thread): route a key string to the
	// focused textbox (printable chars append; backspace deletes).
	void OnKey (const char *pString);

private:
	// Hit-test top-down; returns the topmost window containing (x,y) and whether the
	// hit landed on its title bar. Caller must hold m_SpinLock. Returns ~0u if none.
	unsigned HitTest (int x, int y, boolean *pbOnTitleBar);

	// Widget under the cursor in the topmost window (client area only). Returns the
	// widget and, via ppWindow, its window; 0 if none. Caller holds m_SpinLock.
	GWidget *WidgetUnderCursor (int x, int y, CWindow **ppWindow);

	// Move keyboard focus to pW (a textbox) in pWin, or clear it. Caller holds lock.
	void SetFocusWidget (GWidget *pW, CWindow *pWindow);

	CWindow	  *m_pWindows[WM_MAX_WINDOWS];
	unsigned   m_nWindows;

	GImage	  *m_pWallpaper;	// desktop background (owned), or 0 for the solid colour

	// Cursor + drag state (mutated from the input thread, read by Composite).
	int	   m_nCursorX;
	int	   m_nCursorY;
	boolean	   m_bCursorShown;
	unsigned   m_nLastButtons;	// previous button bitmask (for press/release edges)
	CWindow	  *m_pDragWindow;	// window being dragged by its title bar, or 0
	int	   m_nDragDX;		// cursor-to-window offset captured at drag start
	int	   m_nDragDY;

	// Widget interaction state.
	GWidget	  *m_pHoverWidget;	// widget currently under the cursor (or 0)
	GWidget	  *m_pPressedWidget;	// widget the left button was pressed on (or 0)
	CWindow	  *m_pPressedWindow;	// its window (for event delivery)
	GWidget	  *m_pFocusWidget;	// textbox with keyboard focus (or 0)
	CWindow	  *m_pFocusWindow;	// its window

	// Protects the window list against concurrent Add (app threads) / Remove
	// (process teardown, in scheduler context) / Composite (compositor thread).
	// A spin lock, not a mutex: Remove runs in scheduler context where blocking
	// is illegal; the lock only ever masks IRQ briefly (Composite snapshots the
	// list under the lock, then blits outside it).
	CSpinLock  m_SpinLock;

	static CWindowManager *s_pThis;
};

#endif
