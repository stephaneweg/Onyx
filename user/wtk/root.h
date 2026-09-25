//
// wtk/root.h -- the top widget, bound to the kapi WINDOW canvas. Runs the event loop:
// feeds the kapi pointer/key streams into handleMouse/handleKey, and recomposes +
// kapi_present()s only when the tree is dirty (valid==false).
//
#ifndef _wtk_root_h
#define _wtk_root_h

#include "wtk/widget.h"

namespace wtk {

class Root : public Widget
{
public:
	unsigned bg;				// client-area background colour
	Root (int w, int h, const char *title);				// decorated window
	Root (int x, int y, int w, int h, const char *title, unsigned flags); // positioned / borderless
	void setBg (unsigned c) { bg = c; invalidate (true); }
	void onDraw () override;
	void run ();

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

private:
	void init (unsigned *fb);		// shared ctor tail (adopt canvas + decorate + register)
	static Root *&active ();		// single active window per app (reachable from C callbacks)
	static void ptrEvent (unsigned long, int ev, long v);
	static void keyEvent (unsigned long, int ev, long v);
};

} // namespace wtk

#endif
