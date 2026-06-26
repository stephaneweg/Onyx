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
// Default framebuffer size; overridable at boot via cmdline.txt (width=/height=).
// The actual size in use is published in g_nScreenWidth / g_nScreenHeight below.
#define SCREEN_WIDTH		1024
#define SCREEN_HEIGHT		768

// Actual framebuffer size in use, set once at boot (from cmdline width=/height= or
// the defaults above). Runtime code should prefer these over the compile-time
// defaults so a cmdline override takes effect everywhere.
extern int g_nScreenWidth;
extern int g_nScreenHeight;

// Window-chrome theme, applied at boot (SD:skins/theme.txt). The two tints are baked
// into the window skin (active/inactive); the text colour is the title text.
extern u32 g_WinTitleTextColor;

// Theme + chrome metrics. Match the window skin (wings.bmp, margins 7/7/32/7):
// a 32 px title-bar region on top and 7 px borders. Used both skinned and flat.
#define WIN_TITLEBAR_H		32
#define WIN_BORDER		7
#define WIN_COLOR_FRAME		0x00202028
#define WIN_COLOR_TITLE		0x000000AA
#define WIN_COLOR_TITLE_ACT	0x000000FF
#define WIN_COLOR_DESKTOP	0x00204060

// Multiply-tint applied to the grayscale window skin (wings.bmp). The active window
// gets a warm gold/amber accent; inactive windows a muted slate so focus reads at a
// glance. 0x00FFFFFF would leave the skin untouched.
#define WIN_SKIN_TINT_ACTIVE	0x00FFC878
#define WIN_SKIN_TINT_INACTIVE	0x008090A0

#define WM_MAX_WINDOWS		16
#define WIN_EVENT_QUEUE		32

// Window creation flags (kept numerically identical to user/kapi.h).
#define WIN_FLAG_BORDERLESS	(1u << 0)	// no title bar / border / close box

// Event kinds delivered to an app's pump. Kept numerically identical to the
// values in user/kapi.h so the app and the kernel agree. (The kernel-drawn widget
// events 1..4 are gone -- apps build their UI with the user-side uikit toolkit.)
#define GUI_EVENT_KEY		5	// key pressed (lValue = char or KEY_* code)
#define GUI_EVENT_CANVAS_CLICK	6	// press in the client area, no widget hit
#define GUI_EVENT_CANVAS_MOTION	7	// drag (button held) over the client area
// Full pointer stream for app-side widget toolkits (ABI v22, opt-in via
// set_pointer_handler). lValue packs (wheel<<48)|(changed<<40)|(buttons<<32)|(x<<16)|y,
// all client-relative; `changed` = the button (1/2/4) for DOWN/UP, 0 otherwise; `wheel`
// is a signed 8-bit notch delta, nonzero only on GUI_EVENT_PTR_WHEEL.
#define GUI_EVENT_PTR_MOVE	8	// cursor moved over the client area
#define GUI_EVENT_PTR_DOWN	9	// a button went down
#define GUI_EVENT_PTR_UP	10	// a button went up
#define GUI_EVENT_PTR_ENTER	11	// cursor entered the client area
#define GUI_EVENT_PTR_LEAVE	12	// cursor left the client area
#define GUI_EVENT_PTR_WHEEL	13	// scroll wheel turned (lValue wheel field = signed delta)
					// all: lValue = (wheel<<48)|(buttons<<32)|(clientX<<16)|clientY
					// buttons bit0 = left, bit1 = right; wheel +forward / -back

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

// An event queued for the owning app's pump (key, canvas-click/motion, pointer stream).
struct GUIEvent
{
	u64	ulHandler;		// app callback address to invoke
	u64	ulSender;		// 0 (kernel widgets are gone)
	int	nEvent;			// GUI_EVENT_*
	long	lValue;			// event payload
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

	// Window-chrome buffers (user-drawn decorations): two SEPARATE OuterW x OuterH
	// copies (active [0], inactive [1] = client + chrome insets), mapped into the app
	// at USER_WINDOW_CHROME / _INACTIVE. The app draws its title bar / borders / close
	// box into both; the compositor picks the copy matching focus and blits it (magenta
	// = transparent). Separate (not one combined) so each stays under the heap's top
	// bucket. 0 for a borderless window (no chrome) or if allocation failed.
	boolean HasChrome (void) const		{ return m_ulChromePhys[0] != 0; }
	u64 ChromePhys (int i) const		{ return m_ulChromePhys[i]; }
	unsigned ChromePages (int i) const	{ return m_nChromePages[i]; }
	int OuterW (void) const			{ return m_nOuterW; }
	int OuterH (void) const			{ return m_nOuterH; }

	int X (void) const		{ return m_nX; }
	int Y (void) const		{ return m_nY; }
	void Move (int x, int y)	{ m_nX = x; m_nY = y; }
	const char *Title (void) const	{ return m_Title; }

	// Blit the (app-drawn) chrome + client canvas onto the screen image.
	void DrawTo (GImage *pScreen, boolean bActive);

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

	// Full pointer-event handler (GUI_EVENT_PTR_*) for app-side widget toolkits.
	// Opt-in: when set, the WM streams enter/leave/move/down/up (client coords) here.
	void SetPointerHandler (u64 ulHandler)	{ m_ulPointerHandler = ulHandler; }
	u64  PointerHandler (void) const	{ return m_ulPointerHandler; }

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

	void	       *m_pChromeRaw[2];	// over-allocated chrome blocks (active, inactive); 0 = none
	u64		m_ulChromePhys[2]; // 64 KB-aligned chrome starts (== kernel VA); [0]=0 means none
	unsigned	m_nChromePages[2]; // 64 KB pages per chrome copy
	int		m_nOuterW;	// chrome (outer window) dims = client + chrome insets
	int		m_nOuterH;

	u64		m_ulKeyHandler;	// app key callback (GUI_EVENT_KEY), or 0
	u64		m_ulClickHandler; // app canvas-click callback, or 0
	u64		m_ulPointerHandler; // app pointer-stream callback (GUI_EVENT_PTR_*), or 0

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

	// Current cursor position (screen coords). For gadgets that track the mouse.
	int CursorX (void) const	{ return m_nCursorX; }
	int CursorY (void) const	{ return m_nCursorY; }

	// Clear the desktop and draw every window onto the screen image.
	void Composite (GImage *pScreen);

	// Install the mouse-cursor image (a transparent GImage; takes ownership). If
	// unset, the compositor falls back to a drawn arrow.
	void SetCursor (GImage *pImage)	{ m_pCursor = pImage; }

	// Shared wallpaper buffer for a wallpaper-writer app. EnsureWallpaperBuffer
	// allocates (once) a frame-backed, page-aligned screen-sized buffer and returns
	// its physical (== kernel VA) address + page count so the kapi layer can map it
	// into the app. The app draws into it; CommitWallpaper makes it the live desktop
	// background. The frames are kernel-owned, so the wallpaper outlives the app.
	u32 *EnsureWallpaperBuffer (int nW, int nH, u64 *pPhys, unsigned *pnPages);
	void CommitWallpaper (void)	{ m_bLiveWall = TRUE; }

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

	// Scroll-wheel input (called from the input thread). Routes a signed notch delta
	// (+forward / -back) to the pointer handler of the window under (x,y), as a
	// GUI_EVENT_PTR_WHEEL pointer event.
	void OnMouseWheel (int x, int y, int nWheel);

	// Keyboard input (called from the input thread): route a key string to the
	// focused textbox (printable chars append; backspace deletes).
	void OnKey (const char *pString);

private:
	// Hit-test top-down; returns the topmost window containing (x,y) and whether the
	// hit landed on its title bar. Caller must hold m_SpinLock. Returns ~0u if none.
	unsigned HitTest (int x, int y, boolean *pbOnTitleBar);

	// Move a window to the top of the z-order. Caller holds m_SpinLock.
	void RaiseLocked (CWindow *pWindow);

	// Push one GUI_EVENT_PTR_* event (client coords) to a window's pointer handler.
	// nWheel is the signed wheel delta (only meaningful for GUI_EVENT_PTR_WHEEL).
	void EmitPointer (CWindow *pWin, int nEvent, int cx, int cy,
			  unsigned nButtons, unsigned nChanged, int nWheel = 0);

	CWindow	  *m_pWindows[WM_MAX_WINDOWS];
	unsigned   m_nWindows;

	GImage	  *m_pWallpaper;	// desktop background (owned), or 0 for the solid colour
	GImage	  *m_pCursor;		// mouse cursor bitmap (owned), or 0 for a drawn arrow

	// App-writable wallpaper (mapped into a writer app; kernel-owned frames).
	u8	  *m_pWallRaw;		// raw allocation backing the buffer (0 = none yet)
	u64	   m_ulWallPhys;	// 64 KB-aligned start (== kernel VA, identity region)
	unsigned   m_nWallPages;	// 64 KB pages spanned
	GImage	   m_WallImage;		// wraps the buffer
	boolean	   m_bLiveWall;		// committed? (drawn instead of m_pWallpaper)

	// Cursor + drag state (mutated from the input thread, read by Composite).
	int	   m_nCursorX;
	int	   m_nCursorY;
	int	   m_nPrevX;		// previous cursor pos (drag-motion dedup)
	int	   m_nPrevY;
	boolean	   m_bCursorShown;
	unsigned   m_nLastButtons;	// previous button bitmask (for press/release edges)
	CWindow	  *m_pDragWindow;	// window being dragged by its title bar, or 0
	int	   m_nDragDX;		// cursor-to-window offset captured at drag start
	int	   m_nDragDY;

	// Pointer-stream state for app-side toolkits (windows with a PointerHandler).
	CWindow	  *m_pPtrOverWindow;	// window the cursor is currently over (for enter/leave)
	CWindow	  *m_pPtrCaptureWindow;	// window holding pointer capture during a button-drag

	// Protects the window list against concurrent Add (app threads) / Remove
	// (process teardown, in scheduler context) / Composite (compositor thread).
	// A spin lock, not a mutex: Remove runs in scheduler context where blocking
	// is illegal; the lock only ever masks IRQ briefly (Composite snapshots the
	// list under the lock, then blits outside it).
	CSpinLock  m_SpinLock;

	static CWindowManager *s_pThis;
};

#endif
