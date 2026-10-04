// app.cpp -- the program of the shared libraries' compatibility test (tools/tests/shlib/compat.sh;
// docs/SHARED-LIBS-PLAN.md section 6, "the compatibility test of D5"): a small wtk app that builds a
// window with widgets of the library and classes of its own, draws it once, and prints what it saw
// as "key=value" lines. Built twice: against wtk N (plain) and against wtk N+1 (-DCOMPAT_N1: it then
// also uses what N+1 added). MIT (as Onyx).
#include "wtk/wtk.h"

using namespace wtk;

static void put (const char *k, long v)
{
	char b[64]; int i = 0;
	for (; k[i]; i++) b[i] = k[i];
	b[i++] = '=';
	if (v < 0) { b[i++] = '-'; v = -v; }
	char d[24]; int n = 0;
	do { d[n++] = (char) ('0' + v % 10); v /= 10; } while (v != 0);
	while (n > 0) b[i++] = d[--n];
	b[i++] = '\n';
	kapi_stdout_write (b, (unsigned) i);
}

static int g_drawn, g_clicked, g_compat;

// A class of the program derived from a library class: its vtable is the program's.
class MyButton : public Button
{
public:
	MyButton (int l, int t, int w, int h) : Button (l, t, w, h, "Mine", 0) {}
	void onDraw () override { Button::onDraw (); g_drawn++; }
#ifdef COMPAT_N1
	void onCompat () override { g_compat++; }		// the virtual N+1 put in a reserved slot
#endif
};

static void onClick (Widget &) { g_clicked++; }

int main (void)
{
	Root root (320, 200, "compat");
	Label *l = new Label (10, 10, 300, 20, "compatibility");
	Button *b = new Button (10, 40, 120, 28, "Library's", onClick);
	MyButton *m = new MyButton (10, 80, 120, 28);
	Checkbox *c = new Checkbox (10, 120, 200, 22, "a checkbox", false, 0);
	root.addChild (l); root.addChild (b); root.addChild (m); root.addChild (c);
	root.draw ();						// every widget painted by the library
	m->setFocus ();						// (N+1 calls the new virtual from here)
	b->handleMouse (5, 5, 1, 0, 0, 0);			// a click on the library's button ...
	b->handleMouse (5, 5, 0, 0, 0, 0);			// ... released: the program's callback
	root.draw ();

	put ("bfw", wk_bfw ());					// N: the font's cell; N+1: 1234 (the "fix")
	put ("drawn", g_drawn);					// the program's override ran (>= 1)
	put ("clicked", g_clicked);				// 1
	put ("width", b->width + m->left + (int) sizeof (Widget));	// the classes' layout, as the program sees it
	put ("reserve", (long) m->reserved_[0]);		// N: 0; N+1's library writes 49374 there
#ifdef COMPAT_N1
	put ("added", wk_compat_added ());			// a function N+1 appended: 77
	put ("compat", g_compat);				// the reserved virtual, overridden: 1
#endif
	put ("done", 1);
	return 0;
}
