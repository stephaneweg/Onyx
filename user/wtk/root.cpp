#include "wtk/root.h"
#include "wtk/skin.h"		// wk_decorate_window
#include "wtk/font.h"		// wtk::init (load the global font family at startup)
#include "applib.h"		// should_exit, msleep, pump_events

namespace wtk {

Root::Root (int w, int h, const char *title) : Widget (0, 0, w, h), bg (C_BG)
{ init (kapi_create_window (w, h, title)); }

Root::Root (int x, int y, int w, int h, const char *title, unsigned flags)
  : Widget (0, 0, w, h), bg (C_BG)
{ init (kapi_create_window_ex (x, y, w, h, title, flags)); }

void Root::init (unsigned *fb)
{
	canvas.adopt (fb, width, height);		// the root draws straight into the window canvas
	wk_decorate_window ();				// title bar / borders / close box (no-op if borderless)
	wtk::init ();					// load the global font family once (SD:/fonts/ns-sans.fnt)
	active () = this;
	hasFocus = true;
}

void Root::onDraw () { canvas.clear (bg); }		// client-area background

void Root::run ()
{
	kapi_set_pointer_handler (ptrEvent);
	kapi_set_key_handler (keyEvent);
	while (!should_exit ())
	{
		pump_events ();
		if (!valid) { draw (); kapi_present (); }
		msleep (16);
	}
}

Root *&Root::active () { static Root *p = 0; return p; }
Root *Root::current () { return active (); }

void Root::ptrEvent (unsigned long, int ev, long v)
{
	static int bl = 0, br = 0, bm = 0;		// persistent button state across events
	Root *r = active ();
	if (r == 0) return;
	int c = GUI_PTR_CHANGED (v);
	switch (ev)
	{
	case GUI_EVENT_PTR_DOWN:
		if (c & 1) bl = 1;
		if (c & 2) br = 1;
		if (c & 4) bm = 1;
		break;
	case GUI_EVENT_PTR_UP:
		if (c & 1) bl = 0;
		if (c & 2) br = 0;
		if (c & 4) bm = 0;
		break;
	case GUI_EVENT_PTR_LEAVE:
		r->handleMouse (-1, -1, 0, 0, 0, 0);
		return;
	case GUI_EVENT_PTR_WHEEL:		// route a scroll notch to the widget under the cursor
		r->handleMouse (GUI_PTR_X (v), GUI_PTR_Y (v), bl, br, bm, GUI_PTR_WHEEL (v));
		return;
	default:
		break;
	}
	r->handleMouse (GUI_PTR_X (v), GUI_PTR_Y (v), bl, br, bm, 0);
}

void Root::keyEvent (unsigned long, int ev, long v)
{ Root *r = active (); if (r && ev == GUI_EVENT_KEY) r->handleKey (v); }

} // namespace wtk
