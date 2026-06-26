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
	Root (int w, int h, const char *title);
	void onDraw () override;
	void run ();

private:
	static Root *&active ();		// single active window per app (reachable from C callbacks)
	static void ptrEvent (unsigned long, int ev, long v);
	static void keyEvent (unsigned long, int ev, long v);
};

} // namespace wtk

#endif
