//
// shell/main.cpp -- Onyx activity shell (PHASE 0: empty home-screen frame).
//
// The shell is a fullscreen, borderless wtk app that owns the whole screen and
// replaces the old panel (taskbar) + voronoy (wallpaper). It draws the persistent
// chrome of the activity model:
//   - a top TITLE bar  : current activity name ("Accueil") | clock | Home button
//   - a bottom TIMELINE bar : Journal button | (last documents -- empty for now)
//   - a collapsible LEFT panel : activity widgets (empty placeholder for now)
//   - a central CONTENT area  : the activity viewport (hosts apps in surfaces)
//
// It already runs its OWN event loop (not Root::run) because the shell grows a timer +
// the SHELL_REQUEST/mailbox IPC here. The pointer/key trampolines mirror wtk::Root::run's
// routing; this loop is the seam where the shell diverges.
//
#include "wtk/wtk.h"
#include "applib.h"		// should_exit, pump_events, msleep, present
#include "shell_proto.h"	// activity-shell IPC protocol (hosted apps draw into surfaces)

using namespace wtk;

#define TOPBAR_H	32
#define TIMELINE_H	28
#define LEFTPANEL_W	200
#define LEFTPANEL_MIN	18		// collapsed strip width

// Palette (matches the activity-shell mockups: dark slate frame, subtle accents).
#define COL_DESKTOP	0x00374249	// central content background
#define COL_BAR		0x00222A30	// top / bottom bar background
#define COL_PANEL	0x002C353C	// left panel background
#define COL_DIM		0x0090A0AC	// muted text

static Root  *g_root;
static Label *g_clock;
static Panel *g_left;
static Label *g_left_title;
static Button *g_collapse;
static VSplitter *g_main;		// central area: principal (top) / secondary (bottom) split
static TabHost *g_mainHost;		// principal section (editor / browser / ... )
static TabHost *g_secHost;		// secondary section (terminal / calculator / ...)
static int    g_sw, g_sh;
static bool   g_collapsed = false;
static int    g_last_min  = -1;

// ---- a hosted app's viewport ------------------------------------------------
// RemoteView is a wtk widget whose canvas IS a shared surface the hosted app draws into;
// the shell just composites it. Forwarded input (wtk already converts to view-local
// coords) is shipped to that app over the mailbox.
class RemoteView : public Widget
{
public:
	int pid;				// the hosted app to forward input to
	RemoteView (int w, int h, unsigned *surface, int stride, int pid_) : Widget (0, 0, w, h), pid (pid_)
	{
		canvas.adopt (surface, w, h, stride);	// the app's pixels live here (over-allocated buffer)
		canFocus = true;
	}
	void onDraw () override {}		// the remote app fills our canvas
	void resizeTo (int w, int h) override	// the section resized: show more/less of the surface + tell the app
	{
		if (w == width && h == height) return;
		if (w > canvas.stride) w = canvas.stride;	// can't exceed the surface buffer
		if (h < 1) h = 1;
		setBounds (w, h);			// logical resize (no realloc; stride kept)
		ShResize r; r.w = w; r.h = h;
		kapi_mailbox_send (pid, SH_RESIZE, &r, sizeof r);	// the app re-fits its tree (anchors)
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		ShPtr p;
		p.event   = (mx < 0) ? SH_CLOSE : 0;		// (informational; the app keys off x<0)
		p.x = mx; p.y = my;
		p.buttons = (bl ? 1 : 0) | (br ? 2 : 0) | (bm ? 4 : 0);
		p.changed = 0; p.wheel = wheel;
		if (mx >= 0 && (bl || br || bm)) setFocus ();	// click -> take focus so keys route here
		kapi_mailbox_send (pid, SH_PTR, &p, sizeof p);
		return true;
	}
	bool onKey (long k) override
	{
		int kk = (int) k;
		kapi_mailbox_send (pid, SH_KEY, &kk, sizeof kk);
		return true;
	}
};

// ---- hosted-app registry ----------------------------------------------------
// Each hosted app owns a surface + a RemoteView tab (in its role's section). Several may
// run at once -- including multiple instances of the SAME app (each gets its own tab).
struct Hosted { int pid, surface; RemoteView *view; char name[32]; };
static Hosted g_hosted[16];
static int    g_nhosted = 0;

static RemoteView *view_for (int pid)
{
	for (int i = 0; i < g_nhosted; i++) if (g_hosted[i].pid == pid) return g_hosted[i].view;
	return 0;
}

static bool name_eq (const char *a, const char *b)
{ int i = 0; for (; a[i] && b[i]; i++) if (a[i] != b[i]) return false; return a[i] == b[i]; }

// Tab title for a new instance: the bare name, or "name N" for the Nth concurrent copy,
// so multiple sessions of one app are distinguishable in the section's popup.
static void make_title (const char *base, char *out)
{
	int seq = 0;
	for (int i = 0; i < g_nhosted; i++) if (name_eq (g_hosted[i].name, base)) seq++;
	int k = 0; for (; base[k] && k < 31; k++) out[k] = base[k];
	if (seq > 0 && k < 36)
	{
		int n = seq + 1;
		out[k++] = ' ';
		if (n >= 10) out[k++] = (char) ('0' + (n / 10) % 10);
		out[k++] = (char) ('0' + n % 10);
	}
	out[k] = '\0';
}

// ---- clock -----------------------------------------------------------------
static void fmt2 (char *d, int v) { d[0] = (char) ('0' + (v / 10) % 10); d[1] = (char) ('0' + v % 10); }

static void refresh_clock (void)
{
	int hh = 0, mm = 0;
	kapi_get_datetime (0, 0, 0, &hh, &mm, 0);
	if (mm == g_last_min) return;			// repaint only on the minute
	g_last_min = mm;
	char b[6]; fmt2 (b, hh); b[2] = ':'; fmt2 (b + 3, mm); b[5] = '\0';
	g_clock->setText (b);
}

// ---- left-panel collapse ---------------------------------------------------
static void on_collapse (Widget &)
{
	g_collapsed = !g_collapsed;
	int w = g_collapsed ? LEFTPANEL_MIN : LEFTPANEL_W;
	g_left->resizeTo (w, g_left->height);		// the panel clips its title when narrow
	g_main->left = w;				// main content starts right of the (collapsed) panel
	g_main->resizeTo (g_sw - w, g_main->height);	// ... and fills the freed width (splitter re-flows)
	g_collapse->left = w - 14;			// keep the chevron at the panel's right edge
	g_collapse->text[0] = g_collapsed ? '>' : '<';	// Button exposes text[]; no setText()
	g_collapse->text[1] = '\0';
	g_collapse->invalidate (true);
	g_main->invalidate (true);
	g_left->invalidate (true);
}

// ---- stubs (wired in later phases) -----------------------------------------
static void on_home    (Widget &) { /* TODO phase 2: zoom-out to Accueil */ }
static void on_journal (Widget &) { /* TODO phase 4: open the activity Journal */ }
static void on_apps    (Widget &) { kapi_launch ("applist"); }	// app launcher, centred on screen

// A placeholder "task" pane (stands in for an app viewport until a real app is hosted).
static Panel *placeholder (const char *label, unsigned bg)
{
	Panel *p = new Panel (0, 0, 10, 10, bg);
	p->addChild (new Label (10, 10, 280, wk_fh (), label, COL_DIM, bg));
	return p;
}

// ---- hosted-app IPC (shell side) -------------------------------------------
// A hosted app registered: pick the section for its role, give it a surface sized to that
// section, wrap it in a RemoteView tab (selected so it comes to front + the section popup
// updates), and reply SH_SURFACE so it can map + draw.
static void attach_app (int pid, const char *name, int role)
{
	TabHost *host = (role == ROLE_PRINCIPAL) ? g_mainHost : g_secHost;	// applet -> secondary for now
	if (host == 0 || g_nhosted >= (int) (sizeof g_hosted / sizeof g_hosted[0])) return;
	// Over-allocate the surface at SCREEN size so the viewport can grow/shrink with no
	// realloc/remap (stride = screen width); the logical size is just the section content.
	int id = kapi_surface_create (g_sw, g_sh);
	if (id <= 0) return;
	unsigned *px = kapi_surface_map (id);
	if (px == 0) { kapi_surface_destroy (id); return; }
	int w = host->width;
	int h = host->height - host->headerH;
	if (w < 16) w = 200;
	if (h < 16) h = 150;
	RemoteView *view = new RemoteView (w, h, px, g_sw, pid);	// stride = screen width
	char title[40]; make_title (name, title);		// "name" or "name N" for repeat sessions
	int idx = host->addTab (title, view);
	if (idx < 0) { kapi_surface_destroy (id); delete view; return; }	// section full
	host->select (idx);					// bring the new app to front + sync the popup
	Hosted &e = g_hosted[g_nhosted++];
	e.pid = pid; e.surface = id; e.view = view;
	int k = 0; for (; name[k] && k < 31; k++) e.name[k] = name[k]; e.name[k] = '\0';
	ShSurface s; s.surface_id = id; s.w = w; s.h = h; s.stride = g_sw;
	kapi_mailbox_send (pid, SH_SURFACE, &s, sizeof s);
}

// Drain the shell mailbox: SH_REGISTER (a new hosted app), SH_PRESENT (it drew a frame).
static void poll_ipc (void)
{
	int from, type; unsigned char buf[128];
	while (kapi_mailbox_recv (&from, &type, buf, sizeof buf, 0) >= 0)	// non-blocking drain
	{
		if (type == SH_REGISTER)
		{
			ShRegister *r = (ShRegister *) buf;
			attach_app (from, r->name, r->role);
		}
		else if (type == SH_PRESENT)
		{
			RemoteView *v = view_for (from);
			if (v != 0) v->invalidate (false);		// recomposite (the app drew)
		}
	}
}

// ---- event routing (mirrors wtk::Root::run; diverges here for the shell loop) ----
static void ptr_evt (unsigned long, int ev, long v)
{
	static int bl = 0, br = 0, bm = 0;
	Root *r = g_root; if (r == 0) return;
	int c = GUI_PTR_CHANGED (v);
	switch (ev)
	{
	case GUI_EVENT_PTR_DOWN:  if (c & 1) bl = 1; if (c & 2) br = 1; if (c & 4) bm = 1; break;
	case GUI_EVENT_PTR_UP:    if (c & 1) bl = 0; if (c & 2) br = 0; if (c & 4) bm = 0; break;
	case GUI_EVENT_PTR_LEAVE: r->handleMouse (-1, -1, 0, 0, 0, 0); return;
	case GUI_EVENT_PTR_WHEEL: r->handleMouse (GUI_PTR_X (v), GUI_PTR_Y (v), bl, br, bm, GUI_PTR_WHEEL (v)); return;
	default: break;
	}
	r->handleMouse (GUI_PTR_X (v), GUI_PTR_Y (v), bl, br, bm, 0);
}

static void key_evt (unsigned long, int ev, long v)
{ if (g_root != 0 && ev == GUI_EVENT_KEY) g_root->handleKey (v); }

int main (void)
{
	g_sw = 800; g_sh = 600;
	kapi_screen_size (&g_sw, &g_sh);
	if (g_sw < 320) g_sw = 800;
	if (g_sh < 240) g_sh = 600;

	Root root (0, 0, g_sw, g_sh, "shell", WIN_FLAG_BORDERLESS | WIN_FLAG_BACKMOST);
	g_root = &root;
	root.setBg (COL_DESKTOP);

	int contentH = g_sh - TOPBAR_H - TIMELINE_H;

	// --- top title bar -------------------------------------------------
	Panel *top = new Panel (0, 0, g_sw, TOPBAR_H, COL_BAR);
	top->addChild (new Label (10, (TOPBAR_H - wk_fh ()) / 2, 240, wk_fh (), "Accueil", C_TEXT, COL_BAR));
	g_clock = new Label (g_sw - 60, (TOPBAR_H - wk_fh ()) / 2, 50, wk_fh (), "--:--", C_TEXT, COL_BAR);
	top->addChild (g_clock);
	top->addChild (new Button (g_sw - 150, 3, 80, TOPBAR_H - 6, "Accueil", on_home));
	root.addChild (top);

	// --- left activity panel (collapsible) -----------------------------
	g_left = new Panel (0, TOPBAR_H, LEFTPANEL_W, contentH, COL_PANEL);
	g_left_title = new Label (10, 10, LEFTPANEL_W - 16, wk_fh (), "Activite", COL_DIM, COL_PANEL);
	g_left->addChild (g_left_title);
	g_left->addChild (new Button (10, 14 + wk_fh (), LEFTPANEL_W - 20, 28, "Applications", on_apps));
	g_collapse = new Button (LEFTPANEL_W - 14, TOPBAR_H + contentH / 2 - 14, 12, 28, "<", on_collapse);
	root.addChild (g_left);
	root.addChild (g_collapse);

	// --- central area: principal (top) / secondary (bottom) draggable split ----
	g_main = new VSplitter (LEFTPANEL_W, TOPBAR_H, g_sw - LEFTPANEL_W, contentH,
				contentH * 2 / 3, COL_DESKTOP);
	// Each section hosts several "tasks", switched via a popup menu in its header. The
	// principal section keeps placeholders (no real editor/browser yet); the secondary
	// section starts empty and is filled by hosted apps as they register.
	g_mainHost = new TabHost (0, 0, 10, 10, 26, COL_BAR);
	g_mainHost->addTab ("Editeur",    placeholder ("Editeur de texte", COL_DESKTOP));
	g_mainHost->addTab ("Navigateur", placeholder ("Navigateur web",   COL_DESKTOP));
	g_secHost = new TabHost (0, 0, 10, 10, 26, COL_BAR);
	g_main->setPanes (g_mainHost, g_secHost);
	root.addChild (g_main);

	// --- bottom timeline bar -------------------------------------------
	Panel *bottom = new Panel (0, g_sh - TIMELINE_H, g_sw, TIMELINE_H, COL_BAR);
	bottom->addChild (new Button (4, 3, 90, TIMELINE_H - 6, "Journal", on_journal));
	root.addChild (bottom);

	// --- become THE shell + host the terminal as the default secondary app ----
	// (the calculator and others are launched on demand from Applications)
	kapi_register_shell ();			// the kernel routes hosted-app messages to us
	kapi_launch ("terminal");		// it registers (SH_REGISTER) -> attach_app gives it a surface

	// --- the shell's own event loop: input + clock + hosted-app IPC ----
	kapi_set_pointer_handler (ptr_evt);
	kapi_set_key_handler (key_evt);
	refresh_clock ();
	while (!should_exit ())
	{
		pump_events ();
		refresh_clock ();
		poll_ipc ();			// hosted-app register / present
		if (!root.valid) { root.draw (); kapi_present (); }
		msleep (16);
	}
	return 0;
}
