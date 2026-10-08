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
#include <kern/kapi_abi.h>		// KAPI_FRAME_* (the frame's metrics, shared with the apps)
#include <circle/spinlock.h>
#include <circle/types.h>

class CSynchronizationEvent;

// Screen dimensions (the framebuffer we request). Shared by the kernel + kapi.
// Default framebuffer size; overridable at boot via cmdline.txt (width=/height=).
// The actual size in use is published in g_nScreenWidth / g_nScreenHeight below.
#define SCREEN_WIDTH		1024
#define SCREEN_HEIGHT		768

// Actual framebuffer size in use, set at boot (from cmdline width=/height= or the
// defaults above) and changed by ScreenResizeRequest. Runtime code should read these
// (not the compile-time defaults) each time it needs the size.
extern int g_nScreenWidth;
extern int g_nScreenHeight;

// A new screen size, now (kapi_screen_set, v66): done by the compositor between two frames
// (C2DGraphics::Resize: the firmware gives a frame buffer of that size), then
// CWindowManager::OnScreenResized. Waits for it -> 0; -1 a size out of bounds; -2 not now (a
// full-screen app owns the display, the debug console); -3 the firmware refused it (the old
// size kept). (kernel.cpp)
#define SCREEN_MIN_W	640
#define SCREEN_MIN_H	480
#define SCREEN_MAX_W	2560
#define SCREEN_MAX_H	1600
int ScreenResizeRequest (int nW, int nH);

// Before a task other than the compositor sends a frame to the display by itself
// (C2DGraphics::UpdateDisplay): waits, yielding, until the compositor's own display DMA is over
// -- else that task waits for the DMA without yielding and the compositor never ends it: core 0
// stopped. Task context only (it yields). (kernel.cpp)
void DisplayPresentIdle (void);

// Screen damage: everything that changes what the compositor would draw (an app's present,
// a window added / removed / raised / moved / resized / faded, the cursor, the wallpaper)
// says WHERE: ScreenDirtyRect (a part) or ScreenDirty (all of it). The compositor redraws
// and sends to the display only those parts (dirty rectangles); g_nScreenGen is bumped each
// time, so it (and kapi_screen_grab) can tell "unchanged" -- an idle desktop costs almost
// no CPU, a small window animating costs its own area, not the whole screen.
extern volatile unsigned g_nScreenGen;
void ScreenDirty (void);
void ScreenDirtyRect (int x, int y, int w, int h);
#define SCREEN_DAMAGE_MAX	16
struct TScreenDamage
{
	boolean	bFull;
	int	n;
	int	x0[SCREEN_DAMAGE_MAX], y0[SCREEN_DAMAGE_MAX], x1[SCREEN_DAMAGE_MAX], y1[SCREEN_DAMAGE_MAX];	// [x0, x1) x [y0, y1)
};
void ScreenTakeDamage (TScreenDamage *pOut);	// (the compositor) the damage so far, and forget it

// Window-chrome theme, applied at boot (SD:/etc/theme.txt). The two tints are baked
// into the window skin (active/inactive); the text colour is the title text.
extern u32 g_WinTitleTextColor;

// The frame's metrics (the modernised CDE desktop, v64: kapi_abi.h KAPI_FRAME_*): a 28 px title
// bar on top, 4 px borders, corners rounded (radius KAPI_FRAME_RADIUS). The app draws the frame
// (uikit: user/Kits/uikit/skin.cpp); the kernel hit-tests its title buttons and blends its corners.
#define WIN_TITLEBAR_H		KAPI_FRAME_TITLE_H
#define WIN_BORDER		KAPI_FRAME_BORDER
#define WIN_COLOR_FRAME		0x00202028
#define WIN_COLOR_TITLE		0x000000AA
#define WIN_COLOR_TITLE_ACT	0x000000FF
#define WIN_COLOR_DESKTOP	0x00204060

// Multiply-tint applied to the grayscale window skin (wings.bmp). The active window
// gets a warm gold/amber accent; inactive windows a muted slate so focus reads at a
// glance. 0x00FFFFFF would leave the skin untouched.
#define WIN_SKIN_TINT_ACTIVE	0x00FFC878
#define WIN_SKIN_TINT_INACTIVE	0x008090A0

#define WM_MAX_WINDOWS		64		// (= core.h EL_WINDOWS_MAX; 16 until v94)
#define WIN_EVENT_QUEUE		64		// (v94; 32 before)

// Window creation flags (kept numerically identical to user/kapi.h).
#define WIN_FLAG_BORDERLESS	(1u << 0)	// no title bar / border / close box
#define WIN_FLAG_BACKMOST	(1u << 1)	// pinned to the BOTTOM of the z-order (the shell
						// desktop): never raised above other windows, so
						// floating windows always stay on top of it
#define WIN_FLAG_TOPMOST	(1u << 2)	// pinned to the TOP of the z-order (the system menu
						// bar): never the active app, never gets the keys; a
						// topmost window at y=0 reserves its height at the
						// top of the screen (placement / drag keep clear)
#define WIN_FLAG_TRANSPARENT	(1u << 3)	// client canvas blitted with the magenta key (the
						// menu bar's drop-down floats over the desktop)
#define WIN_FLAG_SYSTEM		(1u << 4)	// a system component (menu bar, notifications,
						// panel, app list, shelf): left out of the open-app
						// list (uk_win_apps -> the panel's taskbar)
#define WIN_FLAG_ALPHA		(1u << 5)	// (v64; borderless windows) the canvas's top byte is a
						// transparency (0 = opaque, 255 = see-through): each
						// pixel blended over what lies below; a click on a
						// see-through pixel goes to what is below (the dock,
						// the agenda widget on the wallpaper)
#define WIN_FLAG_FIXED		(1u << 6)	// (v69) not movable by its title bar, no minimise /
						// maximise / close / menu buttons, centred again when
						// the screen's resolution changes (the first-run wizard)

#define WIN_MENU_MAX		2048	// max menu spec length (uk_win_menu_set)

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
#define GUI_EVENT_MENU		14	// menu command chosen in the menu bar (lValue = item id)
// Drag & drop (ABI v42), delivered to the pointer handler:
#define GUI_EVENT_DROP		15	// dropped on us: lValue = (flags << 32) | (x << 16) | y,
					// client coords; payload via uk_win_drag_data
#define GUI_EVENT_DRAG_OVER	16	// a drag hovers us: same layout (flags DND_F_LEAVE = gone)
#define GUI_EVENT_DRAG_DONE	17	// to the source: lValue = (flags << 32) | target pid
#define GUI_EVENT_WINCTL	18	// (v64) a title button for the app: lValue = KAPI_FRAME_MENU
					// (the window menu) or KAPI_FRAME_MAXIMISE (also a double
					// click on the title bar)
#define GUI_EVENT_TRAY		21	// (v95) the program's icon of the status area (lValue = KAPI_TRAY_*)
#define GUI_EVENT_WINRESIZE	20	// (v82) the frame was dragged to a new size: lValue = (x << 48) |
					// (y << 32) | (client w << 16) | client h -- the app applies it
#define GUI_EVENT_DISPLAY_RESIZE 19	// (v66) the screen's size changed (kapi_screen_set): lValue =
					// (width << 16) | height -- to every window's pointer handler
#define DND_F_COPY		1	// Ctrl held at the drop (copy instead of move)
#define DND_F_CANCEL		2	// DRAG_DONE: cancelled (Esc)
#define DND_F_DESKTOP		4	// DRAG_DONE: dropped on the desktop / no window
#define DND_F_LEAVE		8	// DRAG_OVER: the drag left this window
#define DND_LABEL_MAX		48

// Keyboard modifiers (kapi_get_modifiers): USB report / VNC.
#define MOD_CTRL		1
#define MOD_SHIFT		2
#define MOD_ALT			4
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
#define KEY_F1			0x110	// .. KEY_F12 = 0x11B (KEY_F1 + n - 1)
#define KEY_F12			0x11B

// The memory of a window's pixels: its canvas (nPart 0) and its frame's two copies (1 active, 2
// inactive), nBytes with a spare 64 KB page (the window aligns its start on a page) -> the block
// (WinPixelsFree's argument), 0: none -- ZEROED, unless it is memory the window's program already
// draws in (the graphics server started again: the pixels are kept). window.cpp's are the heap's; the graphics server builds
// window.cpp with WIN_PIXELS_HOOK and gives memory shared with the window's program.
void *WinPixelsAlloc (int nPart, unsigned nBytes);
void WinPixelsFree (void *pRaw);

// An event queued for the owning app's pump (key, canvas-click/motion, pointer stream).
struct GUIEvent
{
	u64	ulHandler;		// app callback address to invoke
	u64	ulSender;		// 0 (kernel widgets are gone)
	int	nEvent;			// GUI_EVENT_*
	long	lValue;			// event payload
	unsigned nMods = 0;		// GUI_EVENT_KEY: MOD_* held when the key was typed
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
	boolean Backmost (void) const	{ return (m_nFlags & WIN_FLAG_BACKMOST) != 0; }
	boolean Topmost (void) const	{ return (m_nFlags & WIN_FLAG_TOPMOST) != 0; }
	boolean Transparent (void) const { return (m_nFlags & WIN_FLAG_TRANSPARENT) != 0; }
	boolean System (void) const	{ return (m_nFlags & WIN_FLAG_SYSTEM) != 0; }
	boolean AlphaCanvas (void) const { return (m_nFlags & WIN_FLAG_ALPHA) != 0 && Borderless (); }
	boolean Fixed (void) const	{ return (m_nFlags & WIN_FLAG_FIXED) != 0; }

	// Minimised (v64): not drawn, not hit, never the active window or the keys' target, until
	// raised (CWindowManager::Raise). Its area is damaged as it goes and as it comes back.
	boolean Minimised (void) const	{ return m_bMinimised; }
	void SetMinimised (boolean bOn)
	{
		if (bOn == m_bMinimised) return;
		if (bOn) { Damage (); m_bMinimised = TRUE; }
		else { m_bMinimised = FALSE; Damage (); }
	}
	// Workspaces (v65): the window's desk (0 .. KAPI_DESK_MAX - 1), or -1: on every desk (the menu
	// bar, the dock, the desktop, a system component). A window of another desk than the current
	// one is off-desk: hidden as a minimised one (not drawn, not hit, never active nor the keys'
	// target) until its desk comes back.
	int Desk (void) const		{ return m_nDesk; }
	void SetDesk (int n)		{ m_nDesk = n; }
	boolean OffDesk (void) const	{ return m_bOffDesk; }
	void SetOffDesk (boolean bOn)
	{
		if (bOn == m_bOffDesk) return;
		if (bOn) { Damage (); m_bOffDesk = TRUE; }
		else { m_bOffDesk = FALSE; Damage (); }
	}
	// Set aside (PocketUI, user/Servers/common/: an app that is not the one in front): hidden as an off-desk
	// window until brought back. Elegant never sets it.
	boolean Aside (void) const	{ return m_bAside; }
	void SetAside (boolean bOn)
	{
		if (bOn == m_bAside) return;
		if (bOn) { Damage (); m_bAside = TRUE; }
		else { m_bAside = FALSE; Damage (); }
	}
	boolean Hidden (void) const	{ return m_bMinimised || m_bOffDesk || m_bAside; }	// (minimised, off-desk or aside)

	// The pid of the process owning this window (0 = kernel), for drag & drop results.
	void SetOwnerPid (unsigned nPid)	{ m_nOwnerPid = nPid; }
	unsigned OwnerPid (void) const		{ return m_nOwnerPid; }
	int MinLogicalHeight (void) const { return m_nMinLogicalH; }	// smallest logical height so far
	void SetAlpha (int a)		{ m_nAlpha = a < 0 ? 0 : a > 255 ? 255 : a; Damage (); }	// 255 = opaque
	int  Alpha (void) const		{ return m_nAlpha; }
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
	void Move (int x, int y)	{ Damage (); m_nX = x; m_nY = y; Damage (); }
	// The whole window (chrome + client) on screen, and marking it damaged.
	// (the chrome copy is blitted whole: its allocated size when larger)
	int OuterWidth (void) const	{ int w = ChromeL () + m_nLogicalW + ChromeR (); return HasChrome () && m_nOuterW > w ? m_nOuterW : w; }
	int OuterHeight (void) const	{ int h = ChromeT () + m_nLogicalH + ChromeB (); return HasChrome () && m_nOuterH > h ? m_nOuterH : h; }
	void Damage (void) const	{ m_nGen++; if (!Hidden ()) ScreenDirtyRect (m_nX, m_nY, OuterWidth (), OuterHeight ()); }
	// An app's present: its client area only -- so a window refreshing alone (an emulator)
	// stays wholly opaque to the compositor (CoversOpaque) whatever its frame's corners -- or
	// the whole window when its frame was redrawn since (get_chrome, a resize) or it is faded.
	// (The first present after a frame's redraw bumps ChromeGen once more: the remote desktop,
	// which may have read the frame while the app was still drawing it, reads it again whole.)
	void PresentDamage (void)
	{
		if (m_nChromeGen != m_nChromeGenShown || m_nAlpha < 255 || Borderless ())
		{
			if (m_nChromeGen != m_nChromeGenShown) m_nChromeGen++;
			m_nChromeGenShown = m_nChromeGen;
			Damage ();
			return;
		}
		m_nGen++;
		if (!Hidden ()) ScreenDirtyRect (m_nX + ChromeL (), m_nY + ChromeT (), m_nLogicalW, m_nLogicalH);
	}
	// For the remote desktop (rdpd, kapi v56): a serial never reused, and a counter bumped
	// whenever the window changes (Damage: drawn, moved, resized...; Touch: full screen).
	unsigned Id (void) const	{ return m_nId; }
	unsigned Gen (void) const	{ return m_nGen; }
	// (kapi v81) the pointer's shape over this window's client area (KAPI_CURSOR_*)
	unsigned CursorShape (void) const	{ return m_nCursorShape; }
	void SetCursorShape (unsigned nShape)	{ m_nCursorShape = nShape; }
	// (kapi v82) resized by its frame: the edges under a point of the screen (WIN_EDGE_* bits, 0:
	// none -- not resizable, or not on an edge), the smallest client area
	boolean Resizable (void) const		{ return m_bResizable; }
	void SetResizable (boolean bOn, int nMinW, int nMinH)
	{
		m_bResizable = bOn;
		m_nMinW = nMinW < 64 ? 64 : nMinW;
		m_nMinH = nMinH < 32 ? 32 : nMinH;
	}
	int MinClientW (void) const		{ return m_nMinW; }
	int MinClientH (void) const		{ return m_nMinH; }
	unsigned HitResizeEdge (int sx, int sy) const;
	void Touch (void) const		{ m_nGen++; }
	unsigned ChromeGen (void) const	{ return m_nChromeGen; }
	void ChromeTouch (void)		{ m_nChromeGen++; }
	unsigned Flags (void) const	{ return m_nFlags; }
	// Does the window paint every pixel of [x0, x1) x [y0, y1) opaquely? (Then what lies
	// below it there need not be drawn.)
	// (A framed window's rounded corners are see-through: a rectangle reaching one is not.)
	boolean CoversOpaque (int x0, int y0, int x1, int y1) const
	{
		if (m_nAlpha < 255 || Transparent () || AlphaCanvas () || Hidden ()) return FALSE;
		if (!Borderless () && !HasChrome ()) return FALSE;
		if (!(x0 >= m_nX && y0 >= m_nY && x1 <= m_nX + OuterWidth () && y1 <= m_nY + OuterHeight ())) return FALSE;
		return Borderless () || !CornersIn (x0, y0, x1, y1);
	}
	// Does [x0, x1) x [y0, y1) (screen) reach a see-through pixel of the frame's corners?
	boolean CornersIn (int x0, int y0, int x1, int y1) const;
	const char *Title (void) const	{ return m_Title; }

	// Blit the (app-drawn) chrome + client canvas onto the screen image.
	void DrawTo (GImage *pScreen, boolean bActive);

	// --- event queue (WM pushes, app pump pops) --------------------------
	void PushEvent (const GUIEvent &Event);
	boolean PopEvent (GUIEvent *pEvent);

	// Modifiers of the key event being dispatched right now (the user-side pump sets it,
	// kapi_event_mods, around the app's key handler; kapi_get_modifiers reports it), 0xFFFFFFFF = none.
	volatile unsigned m_nKeyEventMods = 0xFFFFFFFF;

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

	// --- menu (system menu bar) ------------------------------------------
	// The app's menu spec (see uk_win_menu_set) + the callback that receives
	// GUI_EVENT_MENU. MenuGen() changes whenever the menu is replaced.
	void SetMenu (const char *pSpec, u64 ulHandler);
	const char *Menu (void) const		{ return m_Menu; }
	u64  MenuHandler (void) const		{ return m_ulMenuHandler; }
	unsigned MenuGen (void) const		{ return m_nMenuGen; }

	// --- diagnostics (GUI watchdog) --------------------------------------
	// Events waiting in the ring, events dropped because it was full, and the tick
	// (CTimer::GetTicks) of the owner's last pump (PopEvent call), 0 = never pumped.
	unsigned QueuedEvents (void) const	{ return (m_nEvHead + WIN_EVENT_QUEUE - m_nEvTail) % WIN_EVENT_QUEUE; }
	unsigned DroppedEvents (void) const	{ return m_nEvDropped; }
	unsigned LastPumpTicks (void) const	{ return m_nLastPump; }

	// --- lifecycle -------------------------------------------------------
	void RequestExit (void);
	// (v67) Pulsed on every event pushed and on RequestExit: the owner's kapi_pump_wait
	// sleeps on it (kern/thread.h). 0: none.
	void SetWake (CSynchronizationEvent *pEv)	{ m_pWake = pEv; }
	boolean ShouldExit (void) const	{ return m_bExitRequested; }
	void ClearExit (void)			{ m_bExitRequested = FALSE; }	// (v94: a program's other window, its close told)

	// Close box hit-test (screen coords). True if (sx,sy) is on the [x] box.
	boolean HitCloseBox (int sx, int sy) const;
	// The title button at (sx, sy) (screen): KAPI_FRAME_MENU / CLOSE / MAXIMISE / MINIMISE, or -1.
	int HitTitleButton (int sx, int sy) const;
	// For a WIN_FLAG_ALPHA window: is its pixel at (sx, sy) (screen) not wholly see-through?
	boolean OpaqueAt (int sx, int sy) const;

	// (v64 resize_window2) Grow the canvas (and the frame's copies) to hold w x h: new memory
	// (the old freed a few frames later: the compositor may be reading it), the process's
	// mappings moved to it by the caller. FALSE: no memory (nothing changed).
	boolean Grow (int w, int h);
	// (PocketUI: a card that becomes a filled window) its frame dropped: borderless from now on, the frame's
	// copies freed (its program sees insets 0 at its next uk_win_chrome). Elegant never calls it.
	void DropChrome (void);
	void FreeRetired (void);		// (the compositor) free what Grow retired, once safe

private:
	void CloseBoxRect (int *px0, int *py0, int *px1, int *py1) const;
	boolean AllocChrome (int nOuterW, int nOuterH, void *pRaw[2], u64 ulPhys[2], unsigned nPages[2]);

	unsigned	m_nId;		// (Id)
	mutable volatile unsigned m_nGen;	// (Gen)
	unsigned m_nCursorShape;	// (v81) KAPI_CURSOR_*: the pointer's shape over the client area
	boolean	 m_bResizable;		// (v82) its frame can be dragged
	int	 m_nMinW, m_nMinH;	// ... the smallest client area
	volatile unsigned m_nChromeGen;	// (ChromeGen)
	int		m_nX;		// outer position (title bar top-left)
	int		m_nY;
	unsigned	m_nFlags;	// WIN_FLAG_* (borderless, ...)
	unsigned	m_nOwnerPid;	// owning process (SetOwnerPid)
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
	int		m_nMinLogicalH;	// smallest logical height (topmost bar: its reserved strip)
	volatile int	m_nAlpha;	// whole-window opacity 0..255 (fades; 255 = opaque)

	char		m_Menu[WIN_MENU_MAX];	// menu spec ('' = none)
	u64		m_ulMenuHandler;	// GUI_EVENT_MENU callback, or 0
	unsigned	m_nMenuGen;		// bumped on SetMenu

	// Event ring: the WM (input thread) pushes, the owning app's pump pops.
	GUIEvent	m_Events[WIN_EVENT_QUEUE];
	volatile unsigned m_nEvHead;	// next slot to write
	volatile unsigned m_nEvTail;	// next slot to read
	CSpinLock	m_EvLock;
	volatile unsigned m_nEvDropped;	// events lost to a full ring (diagnostics)
	CSynchronizationEvent *m_pWake;	// (SetWake) the owner's pump, woken on a push
	volatile unsigned m_nLastPump;	// ticks of the last PopEvent (diagnostics)

	volatile boolean m_bExitRequested;
	volatile boolean m_bMinimised;		// (SetMinimised)
	int		m_nDesk;		// (v65) its workspace, -1 = every one (SetDesk)
	volatile boolean m_bOffDesk;		// on another workspace than the current one (SetOffDesk)
	volatile boolean m_bAside;		// (SetAside: PocketUI's)
	unsigned	m_nChromeGenShown;	// m_nChromeGen at the last whole-window present
	void	       *m_pRetired[3];		// memory Grow replaced (canvas, chrome x 2), freed later
	unsigned	m_nRetireFrame;		// the compositor's frame count when it was retired
};

#define HELD_KEYS	0x110		// logical key codes tracked as "held" (< KEY_DEL + 8)
#define HELD_WORDS	((HELD_KEYS + 31) / 32)

#define WIN_EDGE_L	1		// (v82) HitResizeEdge: the frame's edges (a corner: two of them)
#define WIN_EDGE_R	2
#define WIN_EDGE_T	4
#define WIN_EDGE_B	8
#define WIN_EDGE_BAND	6		// how far inside the frame an edge is taken
#define WIN_EDGE_CORNER	18		// ... and how far along an edge its corner reaches

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
	// bCountFrame = FALSE for an off-screen composite (remote screen grab), so the
	// watchdog's frame counter keeps measuring the real compositor only.
	void Composite (GImage *pScreen, boolean bCountFrame = TRUE);

	// Install the mouse-cursor image (a transparent GImage; takes ownership). If
	// unset, the compositor falls back to a drawn arrow.
	void SetCursor (GImage *pImage)	{ m_pCursor = pImage; }
	// (kapi v81) The pointer's other shapes: an image and its hot spot for each KAPI_CURSOR_* but
	// the arrow (kernel.cpp builds them from gui/cursors.inc); a window's shape changed.
	void SetCursorImage (unsigned nShape, GImage *pImage, int nHotX, int nHotY);
	void SetWindowCursor (CWindow *pWindow, unsigned nShape);
	unsigned CursorShown (void) const	{ return m_nShape; }	// the shape shown now (KAPI_CURSOR_*)

	// Shared wallpaper buffer for a wallpaper-writer app. EnsureWallpaperBuffer
	// allocates (once) a frame-backed, page-aligned screen-sized buffer and returns
	// its physical (== kernel VA) address + page count so the kapi layer can map it
	// into the app. The app draws into it; CommitWallpaper makes it the live desktop
	// background. The frames are kernel-owned, so the wallpaper outlives the app.
	u32 *EnsureWallpaperBuffer (int nW, int nH, u64 *pPhys, unsigned *pnPages);

	// The screen's size changed (the compositor, kapi_screen_set; g_nScreenWidth / Height are
	// the new one): the cursor and every window kept on the screen, the app-written wallpaper
	// dropped (its size is the old one), GUI_EVENT_DISPLAY_RESIZE to every window.
	void OnScreenResized (int nW, int nH);
	void CommitWallpaper (void)	{ m_bLiveWall = TRUE; m_nWallGen++; ScreenDirty (); }

	// The desktop alone (the wallpaper + the backmost windows, no other window, no cursor),
	// for the remote desktop (rdpd: uk_win_read of KAPI_WIN_DESKTOP); its change counter.
	void CompositeDesktop (GImage *pScreen);
	unsigned DesktopGen (void);

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
	// GUI_EVENT_PTR_WHEEL pointer event, scaled by the wheel speed.
	void OnMouseWheel (int x, int y, int nWheel);

	// System-wide wheel speed = lines scrolled per notch (clamped to [1,16]). Set by
	// the theme editor (uk_win_wheel_set) and restored from theme.txt at boot.
	void SetWheelSpeed (int nLinesPerNotch);
	int  GetWheelSpeed (void) const { return m_nWheelSpeed; }

	// Keyboard input (called from the input thread): route a key string to the
	// focused textbox (printable chars append; backspace deletes).
	void OnKey (const char *pString);

	// Drag & drop (ABI v42). DragBegin: pSrc starts a drag session (the left button must
	// be held); a badge with pLabel follows the cursor. While it lasts, the window under
	// the cursor gets GUI_EVENT_DRAG_OVER; at the left-button release it gets
	// GUI_EVENT_DROP and the source GUI_EVENT_DRAG_DONE (target pid + DND_F_* flags).
	// Esc cancels. The payload itself is kept by the kapi layer (uk_win_drag_data).
	boolean DragBegin (CWindow *pSrc, const char *pLabel);
	boolean DragActive (void) const		{ return m_bDnd; }

	// Held keys (ABI v48, for games): logical codes (KEY_* arrows, ' ', Enter, Esc,
	// 'a'..'z', '0'..'9'). SetUsbHeld: the USB keyboard's raw report (usage codes, which
	// replace the previous USB set); SetInjectedHeld: vncd's key down / up. KeyHeld: 1 if
	// held and pWin is the window that has the keyboard.
	void SetUsbHeld (const unsigned char RawKeys[6]);
	void SetInjectedHeld (int nKey, boolean bDown);
	boolean KeyHeld (int nKey, CWindow *pWin);
	boolean KeyHeldAny (int nKey);		// ... whoever has the keyboard (kern/wsrv.h: the server says who)
	CWindow *KeyTarget (void);		// the window that has the keyboard, 0: none
	boolean HasKeyFocus (CWindow *pWin);	// pWin has the keyboard (gamepads, ABI v50)

	// Keyboard modifiers (MOD_*), from the USB keyboard's raw report or vncd.
	void SetModifiers (unsigned nMods)	{ if (m_bDnd && ((m_nModifiers ^ nMods) & MOD_CTRL)) ScreenDirty (); m_nModifiers = nMods; }	// (the drag badge's '+')
	unsigned Modifiers (void) const		{ return m_nModifiers; }

	// Diagnostics for the GUI watchdog: counters bumped by Composite / OnMouse /
	// OnKey, and a snapshot of the window list (bottom -> top). Window pointers stay
	// valid after Remove (see Composite), so the caller may inspect them unlocked.
	unsigned FrameCount (void) const	{ return m_nFrames; }
	// The compositor paused under a full-screen app: alive all the same (the watchdog reads
	// FrameCount -- a full-screen app that presents nothing is not a stalled compositor).
	void CompositorAlive (void)		{ m_nFrames++; }
	unsigned MouseCount (void) const	{ return m_nMouseEvents; }
	unsigned KeyCount (void) const		{ return m_nKeyEvents; }
	unsigned Snapshot (CWindow **ppOut, unsigned nMax);

	// ---- system menu bar --------------------------------------------------
	// The ACTIVE app window = the topmost window that is not topmost-flagged (the
	// bar), not backmost (the desktop) and not borderless (panel / popups).
	// GetActiveMenu copies its menu spec + title; returns a serial that changes when
	// the active window or its menu changes (0 = no active window).
	// SendMenuCommand queues GUI_EVENT_MENU(id) to it (id -1 = ask it to close).
	unsigned GetActiveMenu (char *pBuf, unsigned nCap, char *pTitle, unsigned nTitleCap);
	boolean SendMenuCommand (int nID);

	// Height reserved at the top of the screen by a topmost window at y=0 (the menu
	// bar); window auto-placement and title-bar drags keep clear of it.
	int TopInset (void);
	// The work area (v64): the screen less the menu bar at the top and the topmost windows
	// standing on its bottom edge (the dock) -- where a maximised window goes.
	void WorkArea (int *px, int *py, int *pw, int *ph);

	// Minimise a window (v64): hidden until raised; the keys go to the next one.
	void Minimise (CWindow *pWindow);
	// (PocketUI) A window set aside or brought back (CWindow::SetAside): the pointer's references to it dropped.
	void SetAside (CWindow *pWindow, boolean bOn);

	// ---- workspaces (v65: virtual desktops) -------------------------------------------------
	// nCount desks (1 .. KAPI_DESK_MAX), one shown at a time: a new window opens on the current
	// one (a topmost, backmost or system window is on every desk); the others' windows are
	// hidden (CWindow::OffDesk). SetDesk shows desk n (-1 keeps it) and sets the count (0 keeps
	// it: the windows of the desks dropped go to the last one) -> DeskInfo: the current desk |
	// the count << 8 | a counter of the changes << 16. MoveToDesk: a window to desk n (-1: every
	// desk). Raising a window of another desk shows its desk (Raise). Ctrl+Alt+Left / Right: the
	// previous / next desk (with Shift: the active window goes along).
	int SetDesk (int n, int nCount);
	int DeskInfo (void);
	int MoveToDesk (CWindow *pWindow, int n);

	// ---- full-screen apps (ABI v41) ---------------------------------------
	// While a window is full-screen, the compositor stops drawing (the app presents
	// its own buffer) and ALL pointer / key input goes to that window (screen
	// coordinates; the window sits at 0,0). Cleared by SetFullscreen(0) or Remove().
	void SetFullscreen (CWindow *pWindow);
	CWindow *FullscreenWindow (void) const	{ return m_pFullscreen; }
	u32 *EnsureFullscreenBuffer (int nW, int nH, u64 *pPhys, unsigned *pnPages);
	u32 *FullscreenBuffer (void) const	{ return (u32 *) m_ulFsPhys; }

private:
	// Hit-test top-down; returns the topmost window containing (x,y) and whether the
	// hit landed on its title bar. Caller must hold m_SpinLock. Returns ~0u if none.
	unsigned HitTest (int x, int y, boolean *pbOnTitleBar);

	// Move a window to the top of its z-order band. Caller holds m_SpinLock.
	void RaiseLocked (CWindow *pWindow);

	// Caller holds m_SpinLock: the active app window (see GetActiveMenu) / the window
	// that receives the keys (topmost non-topmost-flagged window), or 0.
	CWindow *ActiveLocked (void);
	CWindow *KeyTargetLocked (void);
	int TopInsetLocked (void);
	int BottomInsetLocked (void);
	void MinimiseLocked (CWindow *pWindow);
	void SetDeskLocked (int n);		// show desk n (caller holds m_SpinLock)
	void ForgetHiddenLocked (void);		// the pointer's references to hidden windows dropped

	// Push one GUI_EVENT_PTR_* event (client coords) to a window's pointer handler.
	// nWheel is the signed wheel delta (only meaningful for GUI_EVENT_PTR_WHEEL).
	void EmitPointer (CWindow *pWin, int nEvent, int cx, int cy,
			  unsigned nButtons, unsigned nChanged, int nWheel = 0);

	CWindow	  *m_pWindows[WM_MAX_WINDOWS];
	unsigned   m_nWindows;

	GImage	  *m_pWallpaper;	// desktop background (owned), or 0 for the solid colour
	GImage	  *m_pCursor;		// mouse cursor bitmap (owned), or 0 for a drawn arrow
#define WM_CURSOR_SHAPES 12		// (KAPI_CURSOR_COUNT)
#define WM_CURSOR_REACH	 12		// a shape's hot spot is at most this far from its top left
#define WM_CURSOR_BOX	 24		// ... and a shape at most this wide and high
	GImage	  *m_pShape[WM_CURSOR_SHAPES];	// (v81) the other shapes (owned), 0: the arrow instead
	int	   m_nShapeHotX[WM_CURSOR_SHAPES], m_nShapeHotY[WM_CURSOR_SHAPES];
	unsigned   m_nShape;		// the shape shown now
	void ShowShapeLocked (unsigned nShape);		// change it (the pointer's place made dirty)
	// (v82) a window being resized by its frame: which edges, the pointer and the frame at the
	// press, the frame as dragged (the outline Composite draws)
	CWindow	  *m_pSizeWindow;
	unsigned   m_nSizeEdge;
	int	   m_nSizePX, m_nSizePY, m_nSizeX0, m_nSizeY0, m_nSizeW0, m_nSizeH0;
	int	   m_nSizeX, m_nSizeY, m_nSizeW, m_nSizeH;
	void SizeDragLocked (int x, int y);
	void SizeEndLocked (void);
	void SizeOutlineDirty (void);
	void PickShapeLocked (void);			// ... to what is under the pointer now

	// App-writable wallpaper (mapped into a writer app; kernel-owned frames).
	u8	  *m_pWallRaw;		// raw allocation backing the buffer (0 = none yet)
	u64	   m_ulWallPhys;	// 64 KB-aligned start (== kernel VA, identity region)
	unsigned   m_nWallPages;	// 64 KB pages spanned
	GImage	   m_WallImage;		// wraps the buffer
	boolean	   m_bLiveWall;		// committed? (drawn instead of m_pWallpaper)
	volatile unsigned m_nWallGen;	// bumped when the wallpaper changes (DesktopGen)

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
	CWindow	  *m_pTitleClick;	// the last title-bar press (a double click: maximise)
	unsigned   m_nTitleClickTicks;
	CWindow	  *m_pBtnDown;		// a title button pressed (acted on at its release over it)
	int	   m_nBtnDown;

	// Pointer-stream state for app-side toolkits (windows with a PointerHandler).
	CWindow	  *m_pPtrOverWindow;	// window the cursor is currently over (for enter/leave)
	CWindow	  *m_pPtrCaptureWindow;	// window holding pointer capture during a button-drag
	int	   m_nWheelSpeed;	// lines per wheel notch (1..16); scales OnMouseWheel

	volatile unsigned m_nFrames;		// composited frames (watchdog)
	volatile unsigned m_nMouseEvents;	// OnMouse calls (watchdog)
	volatile unsigned m_nKeyEvents;		// OnKey calls (watchdog)

	CWindow	  * volatile m_pFullscreen;	// full-screen window, or 0
	u8	  *m_pFsRaw;		// full-screen back buffer (kernel-owned, 64 KB aligned)
	u64	   m_ulFsPhys;
	unsigned   m_nFsPages;

	CWindow	  *m_pMenuLast;		// active window at the last GetActiveMenu
	unsigned   m_nMenuLastGen;	// ... and its MenuGen
	unsigned   m_nMenuSerial;	// GetActiveMenu change counter

	u32	   m_UsbHeld[HELD_WORDS];	// held keys (bitsets over the logical codes)
	u32	   m_VncHeld[HELD_WORDS];

	volatile boolean m_bDnd;	// a drag session is in progress
	CWindow	  *m_pDndSrc;		// its source window
	CWindow	  *m_pDndOver;		// the window last sent DRAG_OVER (0 = none)
	char	   m_DndLabel[DND_LABEL_MAX];
	volatile unsigned m_nModifiers;	// MOD_*
	void DndFinishLocked (int x, int y, boolean bCancel);	// drop / cancel

	int	   m_nDesk;			// (v65) the current desk
	int	   m_nDesks;			// how many
	volatile unsigned m_nDeskGen;		// bumped at every change of either

	// Protects the window list against concurrent Add (app threads) / Remove
	// (process teardown, in scheduler context) / Composite (compositor thread).
	// A spin lock, not a mutex: Remove runs in scheduler context where blocking
	// is illegal; the lock only ever masks IRQ briefly (Composite snapshots the
	// list under the lock, then blits outside it).
	CSpinLock  m_SpinLock;

	static CWindowManager *s_pThis;
};

#endif
