//
// uikit/root.h -- the top widget, bound to the kapi WINDOW canvas. Runs the event loop:
// feeds the kapi pointer/key streams into handleMouse/handleKey, and recomposes +
// kapi_present()s only when the tree is dirty (valid==false).
//
#ifndef _uikit_root_h
#define _uikit_root_h

#include "uikit/widget.h"

namespace uikit {

// The loop's three steps, the same for a window and for an applet (a Control Panel applet:
// applet_proto.h -- the app started with "--applet <surface> <host>" draws into the host's pane
// instead of a window of its own; a Root made then adopts that surface). Root::run and the
// modal dialogs go through them; an app with its own loop should too.
bool uk_applet ();				// running as an applet? (its arguments said so)
void uk_pump ();				// the events: the window's (pump_events), an applet's host's
void uk_present ();			// what was drawn shown: kapi_present, or told to the host
bool uk_quit ();				// time to end: the close box, or the host's AP_CLOSE / its end
bool uk_applet_send (int type, const void *data = 0, unsigned len = 0);	// a message to the host
// The host's messages other than AP_PTR / AP_KEY / AP_CLOSE (an applet's own protocol: Jet's web view,
// Apps/jet/webview_proto.h) given to fn (data NUL-terminated after its len bytes), from uk_pump.
// A host other than the Control Panel names its IPC service: "--applet <surface> <host> <service>"
// (the applet ends when that service is gone).
void uk_applet_on_message (void (*fn) (int type, const void *data, int len));

// (v94) A program's other windows (docs/MULTI-WINDOW-STUDY.md): a Root made with NewWindow is one
// more window of the program (AppKit's kapi_win_new), beside the first Root (its main window). The
// first Root's run () / step () serves every one: their events (routed by the window they are for),
// their onTick, their drawing. Its close box asks onClose () (the default: closeWindow ()); the
// program's end is still the first window's close.
struct NewWindow {};

class Root : public Widget
{
public:
	unsigned bg;				// client-area background colour
	Root (int w, int h, const char *title);				// decorated window
	Root (int x, int y, int w, int h, const char *title, unsigned flags); // positioned / borderless
	// (v94) Another window of the program (x, y negative: placed by the system). If it cannot be
	// made (no graphics server's windows left, no memory), winOpened () says false (nothing is drawn).
	Root (NewWindow, int x, int y, int w, int h, const char *title, unsigned flags = 0);
	int winNumber () const { return (int) m_reserved[0]; }	// its number in the program (0: the first)
	bool winOpened () const { return m_reserved[1] == 0; }	// (false: not made, or closed)
	void closeWindow ();			// this window closed (not the first: the program ends then)
	void winSelect ();				// AppKit's window calls act on this window (kapi_win_select)
	static Root *winFirst ();			// the program's first window
	static int winCount ();			// how many windows are open
	static void paintAll ();		// every window that changed drawn and shown
	void setBg (unsigned c) { bg = c; invalidate (true); }
	unsigned bgColor () override { return bg; }
	void onDraw () override;
	void run ();
	bool step ();				// one round of run (): false once it is time to end (attach () first)
	void attach ();				// hook the kapi pointer / key streams (run () does it); for
						// an app that pumps its own loop (pump_events + draw + present)
	virtual void onTick () {}		// called once per run() loop (~60 Hz): polling, timers

	// Drag & drop (ABI v42). onDrop: something was dropped at (x,y) (client coords) --
	// type DND_TEXT / DND_FILES ('\n'-separated paths), data NUL-terminated, flags
	// DND_F_COPY (Ctrl held). onDragOver: a drag hovers (x,y); leave = it went away
	// (highlight a drop target). onDragDone: our own drag (kapi_drag_begin) ended --
	// targetPid (0 = none), flags DND_F_COPY / DND_F_CANCEL / DND_F_DESKTOP.
	virtual void onDrop (int x, int y, int type, const char *data, int len, unsigned flags)
	{ (void) x; (void) y; (void) type; (void) data; (void) len; (void) flags; }
	virtual void onDragOver (int x, int y, bool leave, unsigned flags) { (void) x; (void) y; (void) leave; (void) flags; }
	virtual void onDragDone (int targetPid, unsigned flags) { (void) targetPid; (void) flags; }

	static Root *current ();		// the active window (for modal dialogs)

	// The window's frame (kapi v64). setResizable (true): the app lays itself out at any size
	// (its anchors, its layout do) -- then its maximise button works: the window fills the
	// work area (between the menu bar and the dock) and back; onResized () follows (the new
	// size: width, height). The window menu (its button, top left): Restore / Maximise,
	// Minimise, Close.
	void setResizable (bool on);
	// The smallest client area the frame can be dragged to (kapi v82). Without it: half the size
	// the window has when it becomes resizable (160 x 100 at least). Before or after setResizable.
	void setMinSize (int w, int h);
	bool resizable () const { return m_resizable; }
	bool maximised () const { return m_maxed; }
	void maximise (bool on);
	// A resizable window taller (or wider) than the work area: shrunk to it and moved into it
	// (the dock no longer over its bottom) -- at the start, once its children are anchored.
	void fitWorkArea ();
	virtual void onResized () {}
	// The screen's size changed (GUI_EVENT_DISPLAY_RESIZE, kernel v66: the Control Panel's Display
	// applet): an app placed by the screen's size (the menu bar, the dock...) places itself again
	// here. Then, ~0.3 s later (the dock moved: the work area is the new one), run () fits the
	// window: maximised, to the whole work area again (the size it goes back to kept inside it);
	// else a window past the work area is moved into it, shrunk if it is resizable.
	virtual void onDisplayResize (int w, int h) { (void) w; (void) h; }
	// The reserve of virtual functions for the window (uikit/abi.h; Widget's own come before): a
	// virtual added to Root by a later version of the library takes one of these.
	virtual void onClose ();		// (v94) its close box (not the first window's): closeWindow ()
	void uk_rootReserved0 ();		// (the slot's former name: an entry of the table, never removed)
	virtual void uk_rootReserved1 () {}
	virtual void uk_rootReserved2 () {}
	virtual void uk_rootReserved3 () {}
	virtual void uk_rootReserved4 () {}
	virtual void uk_rootReserved5 () {}
	virtual void uk_rootReserved6 () {}
	virtual void uk_rootReserved7 () {}
	void windowMenu ();

	// Tooltips (Widget::tip): after the pointer rests ~0.6 s over a widget with a tip,
	// a small box shows the text next to it; any pointer event hides it.
	void tooltipTick ();
	void tooltipHide ();

private:
	Widget  *m_tipBox;			// the shown tooltip (a child), or 0
	int      m_mx, m_my;			// last pointer position (client coords)
	unsigned m_moveT;			// ticks of the last pointer event
	bool     m_tipDone;			// already shown for this rest
	bool     m_resizable, m_maxed;		// (setResizable, maximise)
	int      m_minW = 0, m_minH = 0;	// (setMinSize; 0: chosen by setResizable)
	void frameResize (int x, int y, int cw, int ch);	// the frame was dragged (GUI_EVENT_WINRESIZE)
	int      m_rx, m_ry, m_rw, m_rh;	// the window's place and size before it was maximised
	bool     m_dispPending;			// (GUI_EVENT_DISPLAY_RESIZE: displayTick fits the window)
	unsigned m_winFlags;			// (its WIN_FLAG_*: a borderless one places itself)
	unsigned m_dispT;
	void displayTick ();
	void init (unsigned *fb);		// shared ctor tail (adopt canvas + decorate + register)
	void initApplet ();			// ... an applet's: the host's surface, no window
	static Root *&active ();		// single active window per app (reachable from C callbacks)
	void	*m_ext = 0;			// the reserve (uikit/abi.h): the window's later fields
	unsigned long m_reserved[4] = { 0, 0, 0, 0 };	// [0] the window's number (v94), [1] 1: not opened / closed
public:
	static void ptrEvent (unsigned long, int ev, gui_value v);	// (the kernel's event streams; an
	static void keyEvent (unsigned long, int ev, gui_value v);	// applet's host's, re-packed alike)
};

} // namespace uikit

#endif
