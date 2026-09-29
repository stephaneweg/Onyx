//
// wmtest.cpp -- the kernel's window manager and compositor (kernel/gui/window.cpp, gimage.cpp)
// built for the PC with stand-ins for Circle (kstub/), to check the modernised CDE desktop's
// windows without a Raspberry Pi (kapi v64): the frames' rounded corners (blended, and what the
// compositor may skip under a window: CoversOpaque), an app's present damaging its client area
// only (an emulator's window stays opaque), the title buttons (the window menu, minimise,
// maximise, close; a double click on the title), minimised windows, see-through windows
// (WIN_FLAG_ALPHA: their clear pixels let the clicks through), the work area (between the menu
// bar and the dock), a window's canvas growing (maximise), the workspaces (kapi v65: a window on
// another desk hidden, raising it shows its desk, the topmost windows on every desk, Ctrl+Alt+
// arrows) -- and a picture (wm-cde.ppm).
//
//   sh tools/tests/desktop_sim/run.sh     (builds it and runs it)
//
#include <kern/gui/window.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

unsigned g_nStubTicks = 1000;				// (kstub/circle/timer.h)
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { fprintf (stderr, "wmtest: FAILED %s (line %d)\n", #c, __LINE__); g_fail++; } } while (0)

static std::string g_dir;

// A frame as wtk draws it: opaque, but its rounded corners' outside see-through (the top byte:
// 255 - the pixel's coverage, 4 x 4 samples), a title band, the face below.
static void draw_frame (CWindow *w, u32 title, u32 face)
{
	for (int k = 0; k < 2; k++)
	{
		u32 *p = (u32 *) w->ChromePhys (k);
		int W = w->OuterW (), H = w->OuterH (), R = KAPI_FRAME_RADIUS;
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
			{
				int n = 0;
				for (int sy = 0; sy < 4; sy++)
					for (int sx = 0; sx < 4; sx++)
					{
						int px = x * 8 + sx * 2 + 1, py = y * 8 + sy * 2 + 1;	// (1/8 px)
						int cx = px < R * 8 ? R * 8 : px > (W - R) * 8 ? (W - R) * 8 : px;
						int cy = py < R * 8 ? R * 8 : py > (H - R) * 8 ? (H - R) * 8 : py;
						int dx = px - cx, dy = py - cy;
						if (dx * dx + dy * dy <= R * R * 64) n++;
					}
				unsigned cov = n * 255 / 16;
				u32 c = y < KAPI_FRAME_TITLE_H ? (k ? 0x00ACACB0 : title) : face;
				p[y * W + x] = ((255 - cov) << 24) | c;
			}
	}
}

static void paint (CWindow *w, u32 c)
{
	GImage *g = w->Canvas ();
	for (int i = 0; i < g->Width () * g->Height (); i++) g->Buffer ()[i] = c;
}

static u32 *g_screen = new u32[1024 * 768];
static void composite (CWindowManager &wm)
{
	GImage screen (g_screen, 1024, 768);
	screen.ResetClip ();
	wm.Composite (&screen);
}
static u32 at (int x, int y) { return g_screen[y * 1024 + x] & 0xFFFFFF; }

static void shot (CWindowManager &wm, const char *name)
{
	composite (wm);
	std::string p = g_dir + "/" + name;
	FILE *f = fopen (p.c_str (), "wb");
	fprintf (f, "P6\n1024 768\n255\n");
	for (int i = 0; i < 1024 * 768; i++) { u32 c = g_screen[i]; unsigned char rgb[3] = { (unsigned char) (c >> 16), (unsigned char) (c >> 8), (unsigned char) c }; fwrite (rgb, 1, 3, f); }
	fclose (f);
	fprintf (stderr, "wmtest: wrote %s\n", p.c_str ());
}

static int events (CWindow *w, int kind, long *pv = 0)
{
	GUIEvent e; int n = 0;
	while (w->PopEvent (&e)) if (e.nEvent == kind || kind == 0) { n++; if (pv) *pv = e.lValue; }
	return n;
}

// A press and a release of the left button at (x, y).
static void click (CWindowManager &wm, int x, int y)
{
	wm.OnMouse (x, y, 0); wm.OnMouse (x, y, 1); wm.OnMouse (x, y, 0);
}

// The centre of a title button (KAPI_FRAME_*).
static void button_at (CWindow *w, int b, int *px, int *py)
{
	*py = w->Y () + KAPI_FRAME_BTN_Y + KAPI_FRAME_BTN_H / 2;
	if (b == KAPI_FRAME_MENU) { *px = w->X () + KAPI_FRAME_BTN_EDGE + KAPI_FRAME_BTN_W / 2; return; }
	int i = b == KAPI_FRAME_CLOSE ? 0 : b == KAPI_FRAME_MAXIMISE ? 1 : 2;
	*px = w->X () + w->OuterWidth () - KAPI_FRAME_BTN_EDGE - KAPI_FRAME_BTN_W / 2 - i * KAPI_FRAME_BTN_STEP;
}

static bool damage_is (int x, int y, int w, int h)
{
	TScreenDamage d;
	ScreenTakeDamage (&d);
	if (d.bFull || d.n != 1) return false;
	return d.x0[0] == x && d.y0[0] == y && d.x1[0] == x + w && d.y1[0] == y + h;
}

int main (int argc, char **argv)
{
	g_dir = argc > 1 ? argv[1] : ".";
	g_nScreenWidth = 1024; g_nScreenHeight = 768;
	CWindowManager wm;
	wm.GenerateWallpaper (0x003A6EA5, 24, 7);			// the kernel's own Voronoi wallpaper

	// the menu bar (topmost, at the top) and the dock (topmost, see-through, on the bottom edge)
	CWindow *bar = new CWindow (0, 0, 1024, 30, "menubar", WIN_FLAG_BORDERLESS | WIN_FLAG_TOPMOST | WIN_FLAG_SYSTEM);
	paint (bar, 0x00E8E4E0);
	wm.Add (bar);
	CWindow *dock = new CWindow (162, 768 - 84, 700, 84, "dock", WIN_FLAG_BORDERLESS | WIN_FLAG_TOPMOST | WIN_FLAG_SYSTEM | WIN_FLAG_ALPHA);
	paint (dock, 0x00A4BACE);
	for (int y = 0; y < 20; y++) for (int x = 0; x < 700; x++) dock->Canvas ()->Buffer ()[y * 700 + x] = 0xFF000000;	// see-through strip
	wm.Add (dock);
	int ax, ay, aw, ah;
	wm.WorkArea (&ax, &ay, &aw, &ah);
	CHECK (ax == 0 && ay == 30 && aw == 1024 && ah == 768 - 30 - 84);

	// two framed windows
	CWindow *a = new CWindow (100, 100, 300, 200, "calc", 0);
	CHECK (a->ChromeT () == 28 && a->ChromeL () == 4 && a->OuterWidth () == 308 && a->OuterHeight () == 232);
	draw_frame (a, 0x00F0B07A, 0x00D0C2BA);
	paint (a, 0x00C04040);
	a->SetPointerHandler (0x1234);
	wm.Add (a);
	CWindow *b = new CWindow (300, 250, 320, 240, "term", 0);
	draw_frame (b, 0x00F0B07A, 0x00D0C2BA);
	paint (b, 0x00204050);
	b->SetPointerHandler (0x5678);
	wm.Add (b);

	// the title buttons' places
	int x, y;
	for (int k = 0; k < 4; k++) { button_at (a, k, &x, &y); CHECK (a->HitTitleButton (x, y) == k); }
	button_at (a, KAPI_FRAME_MENU, &x, &y);
	CHECK (a->HitTitleButton (x + KAPI_FRAME_BTN_W, y) == -1);		// (between the buttons)
	CHECK (a->HitTitleButton (a->X () + 150, y) == -1);			// (the title)
	CHECK (a->HitCloseBox (a->X () + a->OuterWidth () - KAPI_FRAME_BTN_EDGE - 3, y));

	// what the compositor may skip under a window: all but the see-through corners
	CHECK (a->CoversOpaque (104, 128, 404, 328));				// its client area
	CHECK (!a->CoversOpaque (100, 100, 408, 332));				// the whole: its corners
	CHECK (a->CoversOpaque (150, 100, 350, 128));				// the title bar's middle
	CHECK (!a->CoversOpaque (100, 100, 108, 108));				// a corner square
	CHECK (a->CoversOpaque (102, 110, 108, 320));				// the left border (below the corner)

	// the corners blended over the wallpaper, the frame and the client opaque
	composite (wm);
	static u32 wall[1024 * 768];
	{
		CWindowManager *pw = &wm; (void) pw;
		GImage w (wall, 1024, 768); w.ResetClip ();
		wm.CompositeDesktop (&w);
	}
	CHECK (at (100, 100) == (wall[100 * 1024 + 100] & 0xFFFFFF));		// the corner's outside
	CHECK (at (108, 108) == 0x00ACACB0);					// its title (inactive: b is in front)
	CHECK (at (308, 258) == 0x00F0B07A);					// b's (active)
	CHECK (at (110, 200) == 0x00C04040);					// the client
	CHECK (at (101, 331) == (wall[331 * 1024 + 101] & 0xFFFFFF));		// the bottom-left corner
	CHECK (at (104, 327) == 0x00C04040);					// the client's corner: inside
	shot (wm, "wm-cde.ppm");

	// an app's present: its client area only -- unless its frame changed
	TScreenDamage d; ScreenTakeDamage (&d);
	unsigned cg = a->ChromeGen ();
	a->ChromeTouch ();
	a->PresentDamage ();
	CHECK (damage_is (100, 100, 308, 232));					// the frame redrawn: all
	CHECK (a->ChromeGen () == cg + 2);					// (and read again whole: rdpd)
	a->PresentDamage ();
	CHECK (damage_is (104, 128, 300, 200));					// then the client only
	CHECK (a->ChromeGen () == cg + 2);

	// the window menu (at the press) and maximise (a double click on the title) go to the app
	events (a, 0);
	wm.Raise (a);
	button_at (a, KAPI_FRAME_MENU, &x, &y);
	click (wm, x, y);
	long v = -1;
	CHECK (events (a, GUI_EVENT_WINCTL, &v) == 1 && v == KAPI_FRAME_MENU);
	g_nStubTicks += 100;
	click (wm, a->X () + 150, a->Y () + 10);
	g_nStubTicks += 10;
	click (wm, a->X () + 150, a->Y () + 10);
	v = -1;
	CHECK (events (a, GUI_EVENT_WINCTL, &v) == 1 && v == KAPI_FRAME_MAXIMISE);
	button_at (a, KAPI_FRAME_MAXIMISE, &x, &y);
	g_nStubTicks += 100;
	click (wm, x, y);
	v = -1;
	CHECK (events (a, GUI_EVENT_WINCTL, &v) == 1 && v == KAPI_FRAME_MAXIMISE);

	// a button acts at its release over it only
	button_at (a, KAPI_FRAME_CLOSE, &x, &y);
	wm.OnMouse (x, y, 1); wm.OnMouse (x + 60, y + 60, 1); wm.OnMouse (x + 60, y + 60, 0);
	CHECK (!a->ShouldExit ());

	// minimise: gone (not drawn, not hit, not the keys'), back when raised
	button_at (b, KAPI_FRAME_MINIMISE, &x, &y);
	wm.Raise (b);
	click (wm, x, y);
	CHECK (b->Minimised ());
	composite (wm);
	CHECK (at (500, 400) == (wall[400 * 1024 + 500] & 0xFFFFFF));		// (where only b was)
	CHECK (!b->CoversOpaque (320, 300, 400, 400));
	CHECK (wm.HasKeyFocus (a) && !wm.HasKeyFocus (b));
	wm.Raise (b);
	CHECK (!b->Minimised () && wm.HasKeyFocus (b));
	composite (wm);
	CHECK (at (500, 400) == 0x00204050);

	// close: at the release over the button
	button_at (b, KAPI_FRAME_CLOSE, &x, &y);
	click (wm, x, y);
	CHECK (b->ShouldExit ());
	wm.Remove (b);								// (its process ends)

	// a see-through window's clear pixels let the clicks through (the dock's top strip)
	CWindow *under = new CWindow (400, 600, 200, 150, "under", 0);
	draw_frame (under, 0x00F0B07A, 0x00D0C2BA);
	under->SetPointerHandler (0x9ABC);
	wm.Add (under);
	wm.Raise (dock);
	events (under, 0);
	wm.OnMouse (450, 768 - 84 + 5, 0);
	wm.OnMouse (450, 768 - 84 + 5, 1);					// the dock's clear strip
	wm.OnMouse (450, 768 - 84 + 5, 0);
	CHECK (events (under, GUI_EVENT_PTR_DOWN) == 1);
	wm.OnMouse (450, 768 - 40, 1);						// the dock itself
	wm.OnMouse (450, 768 - 40, 0);
	CHECK (events (under, GUI_EVENT_PTR_DOWN) == 0);

	// maximise: the canvas grows (new memory; the old kept a few frames)
	u32 *old = a->CanvasBuffer ();
	CHECK (a->Grow (1016, 622));
	a->SetLogicalSize (1016, 622);
	CHECK (a->CanvasBuffer () != old && a->Canvas ()->Width () == 1016 && a->OuterWidth () == 1024);
	CHECK (a->OuterW () == 1024 && a->OuterH () == 622 + 32);
	draw_frame (a, 0x00F0B07A, 0x00D0C2BA);
	paint (a, 0x00C04040);
	a->Move (0, 30);
	wm.Raise (a);
	for (int i = 0; i < 4; i++) composite (wm);				// (the old memory freed)
	CHECK (at (512, 300) == 0x00C04040);

	// the workspaces (v65): 4 desks, the windows on the one current when they opened
	CHECK ((wm.DeskInfo () & 0xFF) == 0 && ((wm.DeskInfo () >> 8) & 0xFF) == 4);
	CHECK (a->Desk () == 0 && bar->Desk () == -1 && dock->Desk () == -1);	// (topmost: every desk)
	CWindow *c = new CWindow (600, 120, 200, 150, "edit", 0);
	draw_frame (c, 0x00F0B07A, 0x00D0C2BA);
	paint (c, 0x0030A060);
	c->SetPointerHandler (0x4321);
	wm.Add (c);
	CHECK (wm.MoveToDesk (c, 2) == 2 && c->OffDesk () && c->Hidden ());
	composite (wm);
	CHECK (at (760, 260) == 0x00C04040);					// (not drawn: a, below it)
	events (c, 0);
	click (wm, 700, 200);
	CHECK (events (c, GUI_EVENT_PTR_DOWN) == 0);				// (not hit)
	CHECK (!wm.HasKeyFocus (c));
	int info = wm.SetDesk (2, 0);						// desk 3: c shown, a hidden
	CHECK ((info & 0xFF) == 2 && !c->OffDesk () && a->OffDesk () && !bar->OffDesk () && !dock->OffDesk ());
	composite (wm);
	CHECK (at (760, 260) == 0x0030A060);					// (away from the pointer)
	CHECK (at (512, 300) == (wall[300 * 1024 + 512] & 0xFFFFFF));		// (a: on desk 1)
	wm.Raise (a);								// raising a window: its desk shown
	CHECK ((wm.DeskInfo () & 0xFF) == 0 && !a->OffDesk () && c->OffDesk ());
	wm.OnKey ("\x1b[1;7C");							// Ctrl+Alt+Right: desk 2
	CHECK ((wm.DeskInfo () & 0xFF) == 1 && a->OffDesk () && c->OffDesk ());
	wm.OnKey ("\x1b[1;7D");							// Ctrl+Alt+Left: desk 1 again
	CHECK ((wm.DeskInfo () & 0xFF) == 0 && !a->OffDesk ());
	info = wm.SetDesk (-1, 2);						// 2 desks: c (on desk 3) onto desk 2
	CHECK (((info >> 8) & 0xFF) == 2 && c->Desk () == 1 && c->OffDesk ());
	CHECK (wm.MoveToDesk (c, -1) == -1 && !c->OffDesk ());			// on every desk
	CHECK (wm.MoveToDesk (bar, 1) == -1);					// (the topmost stay on all)

	fprintf (stderr, g_fail ? "wmtest: %d FAILED\n" : "wmtest: all passed\n", g_fail);
	return g_fail ? 1 : 0;
}
