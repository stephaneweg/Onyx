//
// Apps/screenshot/main.cpp -- Screenshot, Onyx's screen capture tool (docs/screenshot/README.md), laid out
// as Windows' Snipping Tool: one toolbar -- New, the mode (a rectangle, a window, the full screen) and the
// delay as drop-down buttons; once a capture is made, Copy and Save As at the left and the drawing tools
// at the right (a pen, a marker -- their colour and size under their arrow --, an eraser that takes a
// stroke away, a crop, Undo / Redo) --, the capture below, fitted in the window.
//
// A capture: the window hides itself (minimised), the screen is grabbed (kapi_screen_grab: what the
// display shows), then shown frozen and darkened full screen (kapi_fullscreen_begin: every pointer and
// key event comes here) while a rectangle is dragged or a window clicked; the full screen is taken at
// once. A delay counts down in the window first. The picture is copied to the clipboard at once (clipd:
// an image item) and notifyd says so; it is saved only on Save As (PNG, JPEG or BMP).
//
// Print Screen (the kernel, any app in front): the "screenshot" service is told (this app running), else
// the app is started with "--now" -- a capture with the last mode and delay; Alt+Print Screen takes the
// window that had the keyboard at once ("--window <id>").
//
// A newlib uikit app with FreeType's text (user/Makefile's screenshot.elf rule). Its settings: SD:/etc/
// screenshot.ini (the mode, the delay, the pen's and the marker's colour and size, the last folder).
//
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "appkit/appkit.h"
#include "systemkit/notify.h"
#include "fontkit/uikitface.h"
#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include "imagekit/img/pngsave.hpp"
#include "systemkit/clipboard.h"

using namespace uikit;

#define SHOT_SERVICE	"screenshot"
#define SHOT_MSG_NOW	1		// the kernel's: Print Screen (payload "now", or "window <id>")
#define SETTINGS	"SD:/etc/screenshot.ini"
#define SHOTS_DIR	"SD:/Pictures/Screenshots"

enum { MODE_RECT, MODE_WINDOW, MODE_FULL };
static const char *const MODE_NAME[3] = { "Rectangle", "Window", "Full screen" };
static const int DELAYS[4] = { 0, 3, 5, 10 };
enum { TOOL_PEN, TOOL_MARKER, TOOL_ERASER, TOOL_CROP };

static const unsigned PEN_COLOURS[8] = { 0x202020, 0xFFFFFF, 0xDC3C32, 0xF0962A, 0xFADC28, 0x46AA5A, 0x3C82DC, 0x965AC8 };
static const unsigned MARK_COLOURS[6] = { 0xFADC28, 0x8CE650, 0xFF82BE, 0x64C8FF, 0xFFA03C, 0xC8A0FF };
static const int PEN_SIZES[4] = { 2, 4, 7, 12 };
static const int MARK_SIZES[4] = { 10, 16, 24, 34 };
static const int MARK_ALPHA = 118;

// ---- the settings ------------------------------------------------------------------------------------------
static int g_mode = MODE_RECT, g_delay = 0;			// (g_delay: an index in DELAYS)
static unsigned g_penColour = 0xDC3C32, g_markColour = 0xFADC28;
static int g_penSize = 1, g_markSize = 1;			// (indexes in PEN_SIZES / MARK_SIZES)
static char g_lastDir[256] = SHOTS_DIR;

static int ini_int (const char *text, const char *key, int def)
{
	int kl = (int) strlen (key);
	for (const char *s = text; s && *s;)
	{
		while (*s == ' ' || *s == '\t') s++;
		if (!strncmp (s, key, kl) && (s[kl] == ' ' || s[kl] == '='))
		{
			const char *v = s + kl; while (*v == ' ' || *v == '=') v++;
			return (int) strtol (v, 0, 0);
		}
		s = strchr (s, '\n'); if (s) s++;
	}
	return def;
}
static void ini_str (const char *text, const char *key, char *out, int cap)
{
	int kl = (int) strlen (key);
	for (const char *s = text; s && *s;)
	{
		while (*s == ' ' || *s == '\t') s++;
		if (!strncmp (s, key, kl) && (s[kl] == ' ' || s[kl] == '='))
		{
			const char *v = s + kl; while (*v == ' ' || *v == '=') v++;
			int k = 0; while (v[k] && v[k] != '\n' && v[k] != '\r' && k < cap - 1) { out[k] = v[k]; k++; }
			out[k] = 0; return;
		}
		s = strchr (s, '\n'); if (s) s++;
	}
}
static void settings_load ()
{
	void *f = kapi_open (SETTINGS);
	if (!f) return;
	static char buf[1024];
	int n = kapi_read (f, buf, sizeof buf - 1); kapi_close (f);
	if (n <= 0) return;
	buf[n] = 0;
	g_mode = ini_int (buf, "mode", g_mode); if (g_mode < 0 || g_mode > 2) g_mode = MODE_RECT;
	int d = ini_int (buf, "delay", 0); g_delay = 0; for (int i = 0; i < 4; i++) if (DELAYS[i] == d) g_delay = i;
	g_penColour = (unsigned) ini_int (buf, "pen_colour", (int) g_penColour) & 0xFFFFFF;
	g_markColour = (unsigned) ini_int (buf, "marker_colour", (int) g_markColour) & 0xFFFFFF;
	g_penSize = ini_int (buf, "pen_size", g_penSize) & 3;
	g_markSize = ini_int (buf, "marker_size", g_markSize) & 3;
	ini_str (buf, "folder", g_lastDir, sizeof g_lastDir);
}
static void settings_save ()
{
	char b[512];
	int n = snprintf (b, sizeof b, "# Screenshot's settings (the app writes them)\nmode = %d\ndelay = %d\npen_colour = 0x%06X\n"
			  "pen_size = %d\nmarker_colour = 0x%06X\nmarker_size = %d\nfolder = %s\n",
			  g_mode, DELAYS[g_delay], g_penColour, g_penSize, g_markColour, g_markSize, g_lastDir);
	if (n > 0) kapi_save_file (SETTINGS, b, (unsigned) n);
}

// ---- the capture and what is drawn on it ---------------------------------------------------------------------
// The picture as grabbed (g_base), the part of it kept (the crop), the strokes (in the base's 1/16 px),
// the operations done (undo / redo), and the result: g_doc, the crop with the strokes over it.
struct Stroke { int tool; unsigned colour; int size; int n, cap; int *xy; bool alive; };
struct Op { int kind; int stroke; int r0[4], r1[4]; };		// kind: OP_ADD / OP_ERASE / OP_CROP
enum { OP_ADD, OP_ERASE, OP_CROP };

static unsigned *g_base; static int g_bw, g_bh;
static int g_crop[4];						// x, y, w, h in the base
static Stroke *g_st; static int g_nst, g_stcap;
static Op *g_ops; static int g_nops, g_opcap, g_opPos;		// done: [0, g_opPos); redo: [g_opPos, g_nops)
static unsigned *g_doc; static int g_dw, g_dh;			// g_dw x g_dh = the crop's size
static unsigned g_docGen;					// bumped at every change (the view's cache)
static char g_what[96];						// "Window 'terminal'" / "Rectangle" / "Full screen"
static char g_savedAs[160];					// the file saved to ("" not saved)
static bool g_dirty;						// changed since saved / copied
static bool g_copyPending;
static unsigned g_copiedGen = ~0u;				// the g_docGen copied to the clipboard					// a new capture: copied at the next tick

static void strokes_free ()
{
	for (int i = 0; i < g_nst; i++) delete[] g_st[i].xy;
	g_nst = 0; g_nops = g_opPos = 0;
}
static void doc_render ()
{
	int w = g_crop[2], h = g_crop[3];
	if (w != g_dw || h != g_dh || !g_doc)
	{
		delete[] g_doc; g_doc = new unsigned[(unsigned) w * (unsigned) h];
		g_dw = w; g_dh = h;
	}
	for (int y = 0; y < h; y++) memcpy (g_doc + (long) y * w, g_base + (long) (g_crop[1] + y) * g_bw + g_crop[0], (size_t) w * 4);
	Canvas cv; cv.adopt (g_doc, w, h);
	int ox = g_crop[0] * 16, oy = g_crop[1] * 16;
	for (int i = 0; i < g_nst; i++)
	{
		Stroke &s = g_st[i];
		if (!s.alive || s.n == 0) continue;
		VPath p;
		if (s.n == 1) p.circle (s.xy[0] - ox, s.xy[1] - oy, V (s.size) / 2);
		else
		{
			int *q = new int[s.n * 2];
			for (int k = 0; k < s.n; k++) { q[2 * k] = s.xy[2 * k] - ox; q[2 * k + 1] = s.xy[2 * k + 1] - oy; }
			p.polyline (q, s.n, V (s.size));
			delete[] q;
		}
		p.fill (cv, s.colour, s.tool == TOOL_MARKER ? MARK_ALPHA : 255);
	}
	g_docGen++;
}
static void op_push (const Op &o)
{
	g_nops = g_opPos;					// the redo branch: gone (its strokes stay dead)
	if (g_nops == g_opcap)
	{
		int nc = g_opcap ? g_opcap * 2 : 64; Op *n = new Op[nc];
		if (g_nops) memcpy (n, g_ops, sizeof (Op) * g_nops);
		delete[] g_ops; g_ops = n; g_opcap = nc;
	}
	g_ops[g_nops++] = o; g_opPos = g_nops;
}
static bool can_undo () { return g_base && g_opPos > 0; }
static bool can_redo () { return g_base && g_opPos < g_nops; }
static void op_apply (const Op &o, bool undo)
{
	if (o.kind == OP_ADD) g_st[o.stroke].alive = !undo;
	else if (o.kind == OP_ERASE) g_st[o.stroke].alive = undo;
	else memcpy (g_crop, undo ? o.r0 : o.r1, sizeof g_crop);
}
static Stroke *stroke_new (int tool)
{
	if (g_nst == g_stcap)
	{
		int nc = g_stcap ? g_stcap * 2 : 64; Stroke *n = new Stroke[nc];
		if (g_nst) memcpy (n, g_st, sizeof (Stroke) * g_nst);
		delete[] g_st; g_st = n; g_stcap = nc;
	}
	Stroke &s = g_st[g_nst++];
	s.tool = tool; s.colour = tool == TOOL_MARKER ? g_markColour : g_penColour;
	s.size = tool == TOOL_MARKER ? MARK_SIZES[g_markSize] : PEN_SIZES[g_penSize];
	s.n = 0; s.cap = 64; s.xy = new int[s.cap * 2]; s.alive = false;
	return &s;
}
static void stroke_add (Stroke *s, int x, int y)
{
	if (s->n && abs (s->xy[2 * s->n - 2] - x) < 8 && abs (s->xy[2 * s->n - 1] - y) < 8) return;	// (half a pixel)
	if (s->n == s->cap)
	{
		int *n = new int[s->cap * 4]; memcpy (n, s->xy, sizeof (int) * 2 * s->n);
		delete[] s->xy; s->xy = n; s->cap *= 2;
	}
	s->xy[2 * s->n] = x; s->xy[2 * s->n + 1] = y; s->n++;
}
// A hand-drawn stroke smoothed (twice 1-2-1 over its points, the ends kept): the mouse's steps and jitter
// gone, as a commercial tool's ink. A straight line (two points) stays as it is.
static void stroke_smooth (Stroke *s)
{
	if (s->n < 4) return;
	int *t = new int[s->n * 2];
	for (int pass = 0; pass < 2; pass++)
	{
		memcpy (t, s->xy, sizeof (int) * 2 * s->n);
		for (int k = 1; k < s->n - 1; k++)
			for (int c = 0; c < 2; c++) s->xy[2 * k + c] = (t[2 * (k - 1) + c] + 2 * t[2 * k + c] + t[2 * (k + 1) + c] + 2) / 4;
	}
	delete[] t;
}
// The topmost stroke under (x, y) (the base's 1/16 px), within `slack` 1/16 px of its edge: -1 none.
static int stroke_at (int x, int y, int slack)
{
	for (int i = g_nst - 1; i >= 0; i--)
	{
		Stroke &s = g_st[i];
		if (!s.alive) continue;
		long long r = V (s.size) / 2 + slack, r2 = r * r;
		for (int k = 0; k < s.n; k++)
		{
			long long ax = s.xy[2 * k], ay = s.xy[2 * k + 1];
			long long bx = k + 1 < s.n ? s.xy[2 * k + 2] : ax, by = k + 1 < s.n ? s.xy[2 * k + 3] : ay;
			long long dx = bx - ax, dy = by - ay, px = x - ax, py = y - ay, l2 = dx * dx + dy * dy;
			long long cx = px, cy = py;
			if (l2 > 0)
			{
				long long t = (px * dx + py * dy) * 1024 / l2;
				if (t < 0) t = 0; else if (t > 1024) t = 1024;
				cx = px - dx * t / 1024; cy = py - dy * t / 1024;
			}
			if (cx * cx + cy * cy <= r2) return i;
		}
	}
	return -1;
}

// ---- the widgets ------------------------------------------------------------------------------------------
class ShotView;
class StatusBar;
class Sep;
static Root *g_root;
static Sep *g_sep2;
static ToolBar *g_bar;
static ShotView *g_view;
static StatusBar *g_status;
static ToolButton *g_bNew, *g_bMode, *g_bDelay, *g_bCopy, *g_bSave, *g_bPen, *g_bMark, *g_bErase, *g_bCrop, *g_bUndo, *g_bRedo;
static int g_tool = TOOL_PEN, g_prevTool = TOOL_PEN;
static FtTextFace *g_big, *g_small;

// The app's state: idle (a capture shown, or none), the delay counting, the window hiding before the grab.
enum { ST_IDLE, ST_COUNT, ST_HIDE };
static int g_state = ST_IDLE;
static unsigned g_t0;						// (ticks: 1/100 s)
static int g_pending = -1;					// the capture to make once hidden: a mode, or -2 a window (g_pendWin)
static unsigned g_pendWin;

static void refresh_ui ();
static void set_tool (int t);

// ---- the icons (drawn from their geometry at any size: uikit/vpaint.h) --------------------------------------
enum { IC_RECT, IC_WINDOW, IC_SCREEN, IC_CLOCK, IC_CLOCK_OFF, IC_PEN, IC_MARKER, IC_ERASER, IC_CROP };
static void draw_icon (Canvas &cv, int id, int x, int y, int s, unsigned ink, bool off)
{
	(void) off;
	VPath p;
	int X = V (x), Y = V (y), S = V (s), u = S / 18;			// u: 1/18 of the box
	switch (id)
	{
	case IC_RECT:							// a dashed box, a filled corner
		for (int i = 0; i < 18; i += 4)
		{
			p.rect (X + i * u, Y + u, 2 * u, 2 * u); p.rect (X + i * u, Y + 16 * u, 2 * u, 2 * u);
			p.rect (X, Y + u + i * u, 2 * u, 2 * u); p.rect (X + 16 * u, Y + u + i * u, 2 * u, 2 * u);
		}
		p.rect (X + 11 * u, Y + 12 * u, 7 * u, 6 * u);
		break;
	case IC_WINDOW:							// a frame, its title bar
		p.rect (X + u, Y + 2 * u, 16 * u, 5 * u); p.rect (X + u, Y + 2 * u, 2 * u, 14 * u);
		p.rect (X + 15 * u, Y + 2 * u, 2 * u, 14 * u); p.rect (X + u, Y + 14 * u, 16 * u, 2 * u);
		break;
	case IC_SCREEN:							// a monitor on its foot
		p.rect (X + u, Y + 2 * u, 16 * u, 2 * u); p.rect (X + u, Y + 11 * u, 16 * u, 2 * u);
		p.rect (X + u, Y + 2 * u, 2 * u, 11 * u); p.rect (X + 15 * u, Y + 2 * u, 2 * u, 11 * u);
		p.rect (X + 8 * u, Y + 13 * u, 2 * u, 3 * u); p.rect (X + 5 * u, Y + 15 * u, 8 * u, 2 * u);
		break;
	case IC_CLOCK: case IC_CLOCK_OFF:
		p.arc (X + 9 * u, Y + 9 * u, 7 * u, 0, 360, 2 * u);
		{ int h[] = { X + 9 * u, Y + 4 * u, X + 9 * u, Y + 9 * u, X + 12 * u, Y + 11 * u }; p.polyline (h, 3, 2 * u); }
		if (id == IC_CLOCK_OFF) { p.fill (cv, ink); p.clear (); p.line (X + 2 * u, Y + 2 * u, X + 16 * u, Y + 16 * u, 4 * u); p.fill (cv, C_BG);
					  p.clear (); p.line (X + 2 * u, Y + 2 * u, X + 16 * u, Y + 16 * u, 2 * u); }
		break;
	case IC_PEN: case IC_MARKER:
		{
			bool m = id == IC_MARKER;
			unsigned col = m ? g_markColour : g_penColour;
			int body[] = { X + 4 * u, Y + 12 * u, X + 12 * u, Y + 2 * u, X + 16 * u, Y + 6 * u, X + 7 * u, Y + 15 * u };
			p.poly (body, 4); p.fill (cv, m ? 0x5A5A60 : 0x46464C);
			p.clear ();
			int tip[] = { X + 2 * u, Y + 15 * u, X + 4 * u, Y + 12 * u, X + 7 * u, Y + 15 * u };
			if (m) { int t2[] = { X + 2 * u, Y + 13 * u, X + 5 * u, Y + 10 * u, X + 9 * u, Y + 14 * u, X + 6 * u, Y + 16 * u }; p.poly (t2, 4); }
			else p.poly (tip, 3);
			p.fill (cv, col);
			p.clear (); p.rect (X + u, Y + 16 * u + u / 2, 16 * u, 2 * u); p.fill (cv, col);	// its colour, underneath
			if (uk_bright (col) > 200) { VPath o; o.rect (X + u, Y + 16 * u + u / 2, 16 * u, u / 2); o.fill (cv, uk_mix (C_BG, 0, 80)); }
			return;
		}
	case IC_ERASER:
		{
			int a[] = { X + 2 * u, Y + 11 * u, X + 10 * u, Y + 3 * u, X + 16 * u, Y + 9 * u, X + 8 * u, Y + 17 * u };
			p.poly (a, 4); p.fill (cv, 0xEB8296);
			p.clear ();
			int b[] = { X + 2 * u, Y + 11 * u, X + 5 * u, Y + 8 * u, X + 11 * u, Y + 14 * u, X + 8 * u, Y + 17 * u };
			p.poly (b, 4); p.fill (cv, 0xF8F8F8);
			p.clear (); p.polyline (a, 4, u + u / 2, true); p.fill (cv, ink, 170);
			return;
		}
	case IC_CROP:
		p.rect (X + 4 * u, Y, 2 * u, 14 * u); p.rect (X + 4 * u, Y + 12 * u, 14 * u, 2 * u);
		p.rect (X, Y + 4 * u, 14 * u, 2 * u); p.rect (X + 12 * u, Y + 4 * u, 2 * u, 14 * u);
		break;
	}
	p.fill (cv, ink);
}

// ---- a drop-down's menu: an icon, a label, a tick at the one chosen ----------------------------------------
class PickMenu : public Modal
{
public:
	enum { MAXI = 6 };
	const char *label[MAXI], *hint[MAXI]; int icon[MAXI]; int n, sel, hot;
	PickMenu (int x, int y, int selected) : Modal (10, 10), n (0), sel (selected), hot (-1) { left = x; top = y; transparent = true; }
	void add (const char *l, int ic, const char *h = 0) { if (n < MAXI) { label[n] = l; icon[n] = ic; hint[n] = h; n++; } }
	int rowH () const { return uk_fh () + 14; }
	int run ()
	{
		int w = 0;
		for (int i = 0; i < n; i++) { int a = uk_tw (label[i]) + (hint[i] ? uk_tw (hint[i]) + 28 : 0); if (a > w) w = a; }
		w += 34 + (icon[0] >= 0 ? 28 : 0) + 16; if (w < 170) w = 170;
		int h = n * rowH () + 10;
		Root *r = Root::current ();
		if (r) { if (left + w > r->width) left = r->width - w; if (top + h > r->height) top = r->height - h; if (left < 0) left = 0; if (top < 0) top = 0; }
		resizeTo (w, h);
		int res = Modal::run ();
		return res > 0 ? res - 1 : -1;
	}
	void onDraw () override
	{
		canvas.clear (UK_TRANSPARENT_KEY);
		uk_popup (canvas, 0, 0, width, height, 8, C_FIELD);
		for (int i = 0; i < n; i++)
		{
			int y = 5 + i * rowH ();
			bool h = i == hot;
			if (h) uk_hilite (canvas, 5, y, width - 10, rowH (), 5, true);
			unsigned ink = h ? C_SEL_TEXT : C_FIELD_TEXT;
			if (i == sel) uk_glyph (canvas, WKG_CHECK, 19, y + rowH () / 2, 11, ink);
			int x = 34;
			if (icon[i] >= 0) { draw_icon (canvas, icon[i], x, y + (rowH () - 18) / 2, 18, ink, false); x += 28; }
			uk_text_l (canvas, x, y, rowH (), label[i], ink);
			if (hint[i]) uk_text_l (canvas, width - 14 - uk_tw (hint[i]), y, rowH (), hint[i], h ? C_SEL_TEXT : uk_mix (C_FIELD, C_FIELD_TEXT, 150));
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int h = mx >= 5 && mx < width - 5 && my >= 5 && my < height - 5 ? (my - 5) / rowH () : -1;
		if (h >= n) h = -1;
		if (h != hot) { hot = h; invalidate (true); }
		if (bl && !pressed) { pressed = true; if (mx < 0 || my < 0 || mx >= width || my >= height) close (0); }
		else if (!bl && pressed) { pressed = false; if (hot >= 0) close (hot + 1); }
		return true;
	}
	bool onKey (long k) override
	{
		if (k == 27) { close (0); return true; }
		if (k == KEY_DOWN) { hot = hot + 1 >= n ? 0 : hot + 1; invalidate (true); }
		else if (k == KEY_UP) { hot = hot <= 0 ? n - 1 : hot - 1; invalidate (true); }
		else if (k == KEY_ENTER && hot >= 0) close (hot + 1);
		return true;
	}
};

// ---- the pen's / the marker's palette: its colours, its sizes ------------------------------------------------
class Palette : public Modal
{
public:
	bool marker; int hotC, hotS;
	const unsigned *cols; int ncols; const int *sizes;
	Palette (int x, int y, bool m) : Modal (10, 10), marker (m), hotC (-1), hotS (-1)
	{
		left = x; top = y; transparent = true;
		cols = m ? MARK_COLOURS : PEN_COLOURS; ncols = m ? 6 : 8; sizes = m ? MARK_SIZES : PEN_SIZES;
		int w = 16 + ncols * 30 + 6, h = 132;
		Root *r = Root::current ();
		if (r) { if (left + w > r->width) left = r->width - w; if (left < 0) left = 0; }
		resizeTo (w, h);
	}
	unsigned &colour () { return marker ? g_markColour : g_penColour; }
	int &size () { return marker ? g_markSize : g_penSize; }
	int colX (int i) const { return 16 + i * 30 + 11; }
	int sizeX (int i) const { return 16 + i * ((width - 32) / 4) + (width - 32) / 8; }
	void onDraw () override
	{
		canvas.clear (UK_TRANSPARENT_KEY);
		uk_popup (canvas, 0, 0, width, height, 9, C_FIELD);
		unsigned dim = uk_mix (C_FIELD, C_FIELD_TEXT, 150), ink = C_FIELD_TEXT;
		{ UkFaceScope f (g_small); uk_text_l (canvas, 14, 6, 18, "COLOUR", dim, 2); uk_text_l (canvas, 14, 66, 18, "SIZE", dim, 2); }
		for (int i = 0; i < ncols; i++)
		{
			int cx = colX (i), cy = 42;
			if (cols[i] == colour ()) { VPath r; r.arc (V (cx), V (cy), V (13), 0, 360, V (2) + 8); r.fill (canvas, C_ACCENT); }
			else if (i == hotC) { VPath r; r.arc (V (cx), V (cy), V (13), 0, 360, V (1) + 8); r.fill (canvas, dim); }
			VPath d; d.circle (V (cx), V (cy), V (10)); d.fill (canvas, cols[i], marker ? 200 : 255);
			VPath o; o.arc (V (cx), V (cy), V (10), 0, 360, 12); o.fill (canvas, uk_mix (C_FIELD, ink, 90));
		}
		for (int i = 0; i < 4; i++)
		{
			int cx = sizeX (i), cy = 104, bw = (width - 32) / 4 - 6;
			if (i == size ()) uk_rbox (canvas, cx - bw / 2, cy - 18, bw, 36, 6, uk_mix (C_FIELD, C_ACCENT, 60), uk_mix (C_FIELD, C_ACCENT, 76));
			else if (i == hotS) uk_rline (canvas, cx - bw / 2, cy - 18, bw, 36, 6, dim, 150);
			int d = marker ? 6 + i * 5 : 3 + i * 4;
			VPath l; l.line (V (cx - bw / 2 + 12), V (cy), V (cx + bw / 2 - 12), V (cy), V (d)); l.fill (canvas, colour (), marker ? MARK_ALPHA + 60 : 255);
			if (uk_bright (colour ()) > 225) { VPath o; o.line (V (cx - bw / 2 + 12), V (cy), V (cx + bw / 2 - 12), V (cy), V (d) + 16); o.fill (canvas, dim, 90); }
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int hc = -1, hs = -1;
		if (my >= 28 && my < 58) for (int i = 0; i < ncols; i++) if (abs (mx - colX (i)) <= 14) hc = i;
		if (my >= 84 && my < 124) for (int i = 0; i < 4; i++) if (abs (mx - sizeX (i)) <= (width - 32) / 8) hs = i;
		if (hc != hotC || hs != hotS) { hotC = hc; hotS = hs; invalidate (true); }
		if (bl && !pressed) { pressed = true; if (mx < 0 || my < 0 || mx >= width || my >= height) close (0); }
		else if (!bl && pressed)
		{
			pressed = false;
			if (hotC >= 0) { colour () = cols[hotC]; invalidate (true); close (1); }
			else if (hotS >= 0) { size () = hotS; invalidate (true); close (1); }
		}
		return true;
	}
	bool onKey (long k) override { if (k == 27 || k == KEY_ENTER) close (0); return true; }
};

// ---- a separator in the toolbar (one that moves: after the delay's button, as wide as its text) -----------
class Sep : public Widget
{
public:
	Sep (int h) : Widget (0, 0, 9, h) {}
	unsigned bgColor () override { return parent ? parent->bgColor () : C_BG; }
	void onDraw () override { canvas.clear (bgColor ()); uk_etch_v (canvas, 4, 4, height - 8, bgColor ()); }
};

// ---- the status bar -----------------------------------------------------------------------------------
class StatusBar : public Widget
{
public:
	char left_[200], right_[120];
	StatusBar (int l, int t, int w, int h) : Widget (l, t, w, h) { left_[0] = right_[0] = 0; }
	void set (const char *l, const char *r)
	{
		if (strcmp (l, left_) || strcmp (r, right_)) { snprintf (left_, sizeof left_, "%s", l); snprintf (right_, sizeof right_, "%s", r); invalidate (true); }
	}
	void onDraw () override
	{
		canvas.clear (C_BG);
		uk_etch_h (canvas, 0, 0, width, C_BG);
		unsigned dim = uk_mix (C_BG, C_TEXT, 150);
		int rw = right_[0] ? uk_tw (right_) : 0;
		if (left_[0])
		{
			VPath d; d.circle (V (12), V (height / 2) + 8, V (4)); d.fill (canvas, g_savedAs[0] ? 0x4EA05C : g_dirty ? 0xE2A23A : 0x4EA05C);
			char t[200]; uk_text_fit (left_, width - 40 - rw - 20, t, sizeof t);
			uk_text_l (canvas, 22, 1, height, t, C_TEXT);
		}
		if (rw) uk_text_l (canvas, width - 10 - rw, 1, height, right_, dim);
	}
};

// ---- the view: the capture fitted in the window, the drawing on it; or the welcome, the countdown --------
class ShotView : public Widget
{
public:
	unsigned *scaled; int sw, sh, scap; unsigned scaledGen;		// the doc fitted (a box filter)
	int ox, oy;							// where it is
	Stroke *live; bool liveStraight;				// the stroke being drawn
	bool dragging; int ax, ay, bx, by;				// a crop being dragged (view px)
	int eraseHot;							// the stroke the eraser points at
	int mx_, my_; bool inside_;
	ShotView (int l, int t, int w, int h) : Widget (l, t, w, h), scaled (0), sw (0), sh (0), scap (0), scaledGen (~0u),
		ox (0), oy (0), live (0), liveStraight (false), dragging (false), ax (0), ay (0), bx (0), by (0), eraseHot (-1),
		mx_ (0), my_ (0), inside_ (false) {}
	unsigned backdrop () { return uk_mix (C_BG, uk_bright (C_BG) > 128 ? 0x000000 : 0xFFFFFF, 18); }
	unsigned bgColor () override { return backdrop (); }
	// the doc's size in the view (never enlarged)
	void fit (int *w, int *h)
	{
		int aw = width - 40, ah = height - 40;
		if (aw < 16) aw = 16; if (ah < 16) ah = 16;
		long long s = 1 << 16;
		if ((long long) g_dw << 16 > (long long) aw << 16) s = ((long long) aw << 16) / g_dw;
		long long s2 = (long long) ah << 16; if ((long long) g_dh * s > s2) s = s2 / g_dh;
		*w = (int) (g_dw * s >> 16); *h = (int) (g_dh * s >> 16);
		if (*w < 1) *w = 1; if (*h < 1) *h = 1;
	}
	int zoom () { int w, h; fit (&w, &h); return (int) ((long long) w * 100 + g_dw / 2) / g_dw; }
	void rescale ()
	{
		int w, h; fit (&w, &h);
		if (w == sw && h == sh && scaledGen == g_docGen) return;
		if (w * h > scap) { delete[] scaled; scaled = new unsigned[(unsigned) w * h]; scap = w * h; }
		sw = w; sh = h; scaledGen = g_docGen;
		if (w == g_dw && h == g_dh) { memcpy (scaled, g_doc, (size_t) w * h * 4); return; }
		for (int y = 0; y < h; y++)					// the area of each pixel averaged
		{
			int y0 = (int) ((long long) y * g_dh / h), y1 = (int) ((long long) (y + 1) * g_dh / h); if (y1 <= y0) y1 = y0 + 1;
			for (int x = 0; x < w; x++)
			{
				int x0 = (int) ((long long) x * g_dw / w), x1 = (int) ((long long) (x + 1) * g_dw / w); if (x1 <= x0) x1 = x0 + 1;
				unsigned r = 0, g = 0, b = 0, n = 0;
				for (int yy = y0; yy < y1; yy++)
				{
					const unsigned *row = g_doc + (long) yy * g_dw;
					for (int xx = x0; xx < x1; xx++) { unsigned c = row[xx]; r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++; }
				}
				scaled[(long) y * w + x] = ((r / n) << 16) | ((g / n) << 8) | (b / n);
			}
		}
	}
	// view px <-> the base's 1/16 px
	int toX (int vx) { return g_crop[0] * 16 + (int) ((long long) ((vx - ox) * 16 + 8) * g_dw / sw); }
	int toY (int vy) { return g_crop[1] * 16 + (int) ((long long) ((vy - oy) * 16 + 8) * g_dh / sh); }
	int fromX (int x16) { return ox * 16 + (int) ((long long) (x16 - g_crop[0] * 16) * sw / g_dw); }
	int fromY (int y16) { return oy * 16 + (int) ((long long) (y16 - g_crop[1] * 16) * sh / g_dh); }

	void drawWelcome ()
	{
		unsigned dim = uk_mix (C_BG, C_TEXT, 150);
		const char *a = "Press", *b = "to start a capture", *k = "Print Screen";
		int kw = uk_tw (k, 2) + 20, tot = uk_tw (a) + 10 + kw + 10 + uk_tw (b);
		int x = (width - tot) / 2, y = height / 2 - 26;
		uk_text_l (canvas, x, y, 26, a, C_TEXT); x += uk_tw (a) + 10;
		uk_rbox (canvas, x, y + 2, kw, 26, 5, uk_tone (C_BG, 100), uk_tone (C_BG, 96));
		uk_rbox (canvas, x, y, kw, 25, 5, uk_mix (C_FIELD, 0xFFFFFF, 80), C_FIELD);
		uk_rline (canvas, x, y, kw, 25, 5, uk_tone (C_BG, 90));
		uk_text_c (canvas, x, y, kw, 25, k, C_FIELD_TEXT, 2); x += kw + 10;
		uk_text_l (canvas, x, y, 26, b, C_TEXT);
		UkFaceScope f (g_small);
		uk_text_c (canvas, 0, y + 34, width, 18, "or click New \xE2\x80\x94 the capture is copied at once, saved only with Save As", dim);
	}
	void drawCountdown ()
	{
		int total = DELAYS[g_delay] * 100, el = (int) (kapi_get_ticks () - g_t0), left = (total - el + 99) / 100;
		if (left < 1) left = 1;
		int cx = width / 2, cy = height / 2 - 12, r = 46;
		VPath ring; ring.arc (V (cx), V (cy), V (r), 0, 360, V (6)); ring.fill (canvas, uk_mix (C_BG, C_TEXT, 40));
		int ang = 360 - (int) ((long long) el * 360 / (total ? total : 1)); if (ang < 1) ang = 1;
		VPath arc; arc.arc (V (cx), V (cy), V (r), 90, 90 + ang, V (6)); arc.fill (canvas, C_ACCENT);
		char t[8]; snprintf (t, sizeof t, "%d", left);
		{ UkFaceScope f (g_big); uk_text_c (canvas, cx - r, cy - r, 2 * r, 2 * r, t, C_TEXT, 2); }
		char m[80]; snprintf (m, sizeof m, "%s in %d s  \xE2\x80\x94  Esc: cancel", g_mode == MODE_RECT ? "Choose a rectangle" : g_mode == MODE_WINDOW ? "Choose a window" : "The screen is taken", left);
		uk_text_c (canvas, 0, cy + r + 14, width, 20, m, uk_mix (C_BG, C_TEXT, 170));
	}
	void onDraw () override
	{
		canvas.clear (g_base ? backdrop () : C_BG);
		if (g_state == ST_COUNT) { canvas.clear (C_BG); drawCountdown (); return; }
		if (!g_base) { drawWelcome (); return; }
		rescale ();
		ox = (width - sw) / 2; oy = (height - sh) / 2;
		uk_rbox (canvas, ox + 2, oy + 4, sw, sh, 2, 0, 0, 40);		// a soft shadow
		Canvas src; src.adopt (scaled, sw, sh);
		canvas.putOther (src, ox, oy, false);
		uk_rline (canvas, ox - 1, oy - 1, sw + 2, sh + 2, 0, uk_mix (backdrop (), 0, 120), 160);
		if (live && live->n)						// the stroke being drawn, over the view
		{
			int s = (int) ((long long) V (live->size) * sw / g_dw); if (s < 12) s = 12;
			VPath p;
			if (live->n == 1) p.circle (fromX (live->xy[0]), fromY (live->xy[1]), s / 2);
			else
			{
				int *q = new int[live->n * 2];
				for (int k = 0; k < live->n; k++) { q[2 * k] = fromX (live->xy[2 * k]); q[2 * k + 1] = fromY (live->xy[2 * k + 1]); }
				p.polyline (q, live->n, s);
				delete[] q;
			}
			p.fill (canvas, live->colour, live->tool == TOOL_MARKER ? MARK_ALPHA : 255);
		}
		if (g_tool == TOOL_ERASER && eraseHot >= 0)			// the stroke the eraser would take
		{
			Stroke &st = g_st[eraseHot];
			int s = (int) ((long long) V (st.size) * sw / g_dw) + V (4);
			int *q = new int[st.n * 2 + 2];
			for (int k = 0; k < st.n; k++) { q[2 * k] = fromX (st.xy[2 * k]); q[2 * k + 1] = fromY (st.xy[2 * k + 1]); }
			VPath p; if (st.n == 1) p.circle (q[0], q[1], s / 2); else p.polyline (q, st.n, s);
			p.fill (canvas, C_ACCENT, 90);
			delete[] q;
		}
		if (g_tool == TOOL_CROP && dragging)				// the part kept: bright, the rest dimmed
		{
			int x0 = ax < bx ? ax : bx, x1 = ax < bx ? bx : ax, y0 = ay < by ? ay : by, y1 = ay < by ? by : ay;
			if (x0 < ox) x0 = ox; if (y0 < oy) y0 = oy; if (x1 > ox + sw) x1 = ox + sw; if (y1 > oy + sh) y1 = oy + sh;
			for (int y = oy; y < oy + sh; y++)
			{
				unsigned *row = canvas.px + (long) y * canvas.stride;
				for (int x = ox; x < ox + sw; x++)
					if (y < y0 || y >= y1 || x < x0 || x >= x1) { unsigned c = row[x]; row[x] = (c >> 1) & 0x7F7F7F; }
			}
			uk_rline (canvas, x0, y0, x1 - x0, y1 - y0, 0, 0xFFFFFF);
			for (int i = 1; i < 3; i++)					// thirds, as a camera's
			{
				canvas.fillRect (x0 + (x1 - x0) * i / 3, y0, 1, y1 - y0, 0xC0C0C0);
				canvas.fillRect (x0, y0 + (y1 - y0) * i / 3, x1 - x0, 1, 0xC0C0C0);
			}
			char t[32]; snprintf (t, sizeof t, "%d x %d", (toX (x1) - toX (x0)) / 16, (toY (y1) - toY (y0)) / 16);
			UkFaceScope f (g_small);
			int tw = uk_tw (t) + 14, ty = y1 + 6 + 20 <= height ? y1 + 6 : y0 - 26;
			uk_rbox (canvas, x0, ty, tw, 20, 6, 0x202024, 0x202024, 215);
			uk_text_c (canvas, x0, ty, tw, 20, t, 0xFFFFFF);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (!g_base || g_state != ST_IDLE) return false;
		bool in = mx >= ox && my >= oy && mx < ox + sw && my < oy + sh;
		bool down = bl && !pressed, up = !bl && pressed;
		if (down) pressed = true;
		if (up) pressed = false;
		mx_ = mx; my_ = my; inside_ = in;
		int x = toX (mx), y = toY (my);
		if (g_tool == TOOL_PEN || g_tool == TOOL_MARKER)
		{
			if (down && in)
			{
				live = stroke_new (g_tool);
				stroke_add (live, x, y);
				liveStraight = (kapi_get_modifiers () & MOD_SHIFT) != 0;
				invalidate (true);
			}
			else if (live && pressed)
			{
				if (liveStraight || (kapi_get_modifiers () & MOD_SHIFT)) { live->n = 1; liveStraight = true; }
				stroke_add (live, x, y);
				invalidate (true);
			}
			else if (live && up)
			{
				stroke_smooth (live);
				live->alive = true;
				Op o; o.kind = OP_ADD; o.stroke = (int) (live - g_st);
				live = 0;
				op_push (o);
				g_dirty = true; doc_render (); refresh_ui (); invalidate (true);
			}
		}
		else if (g_tool == TOOL_ERASER)
		{
			int slack = (int) ((long long) V (5) * g_dw / (sw ? sw : 1));
			int h = in ? stroke_at (x, y, slack) : -1;
			if (h != eraseHot) { eraseHot = h; invalidate (true); }
			if ((down || pressed) && h >= 0)
			{
				Op o; o.kind = OP_ERASE; o.stroke = h; op_push (o);
				g_st[h].alive = false; eraseHot = -1;
				g_dirty = true; doc_render (); refresh_ui (); invalidate (true);
			}
		}
		else if (g_tool == TOOL_CROP)
		{
			if (down && in) { dragging = true; ax = bx = mx; ay = by = my; invalidate (true); }
			else if (dragging && pressed) { bx = mx < ox ? ox : mx > ox + sw ? ox + sw : mx; by = my < oy ? oy : my > oy + sh ? oy + sh : my; invalidate (true); }
			else if (dragging && up)
			{
				dragging = false;
				int x0 = toX (ax < bx ? ax : bx) / 16, x1 = toX (ax < bx ? bx : ax) / 16, y0 = toY (ay < by ? ay : by) / 16, y1 = toY (ay < by ? by : ay) / 16;
				if (x1 - x0 >= 8 && y1 - y0 >= 8 && (x1 - x0 < g_crop[2] || y1 - y0 < g_crop[3]))
				{
					Op o; o.kind = OP_CROP; o.stroke = -1;
					memcpy (o.r0, g_crop, sizeof g_crop);
					o.r1[0] = x0; o.r1[1] = y0; o.r1[2] = x1 - x0; o.r1[3] = y1 - y0;
					op_push (o); memcpy (g_crop, o.r1, sizeof g_crop);
					g_dirty = true; doc_render ();
					set_tool (g_prevTool);				// (cropped: back to the tool before)
				}
				invalidate (true);
			}
		}
		return in || pressed;
	}
};

// ---- the actions ----------------------------------------------------------------------------------------
static void copy_doc (bool quiet)
{
	if (!g_doc) return;
	if (!clip_set_image (g_doc, g_dw, g_dh)) { if (!quiet) uk_messagebox ("Screenshot", "The picture could not be copied: the clipboard service (clipd) is not there.", MB_OK); return; }
	g_copiedGen = g_docGen;
	char t[80]; snprintf (t, sizeof t, "Copied to the clipboard (%d x %d)", g_dw, g_dh);
	notify ("Screenshot", t);
}
static void stamp_name (char *out, int cap)
{
	int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0; kapi_get_datetime (&y, &mo, &d, &h, &mi, &s);
	snprintf (out, cap, "Screenshot %04d-%02d-%02d %02d-%02d-%02d.png", y, mo, d, h, mi, s);
}
static bool ends_with (const char *s, const char *e)
{
	int a = (int) strlen (s), b = (int) strlen (e);
	if (b > a) return false;
	for (int i = 0; i < b; i++) { char c = s[a - b + i]; if (c >= 'A' && c <= 'Z') c += 32; if (c != e[i]) return false; }
	return true;
}
static void save_as ()
{
	if (!g_doc) return;
	kapi_mkdir ("SD:/Pictures"); kapi_mkdir (SHOTS_DIR);
	char name[96]; stamp_name (name, sizeof name);
	char path[300];
	if (!uk_file_save (path, sizeof path, g_lastDir, name)) return;
	if (!ends_with (path, ".png") && !ends_with (path, ".jpg") && !ends_with (path, ".jpeg") && !ends_with (path, ".bmp"))
		strncat (path, ".png", sizeof path - strlen (path) - 1);
	unsigned n = (unsigned) g_dw * g_dh;
	unsigned *px = new unsigned[n];
	for (unsigned i = 0; i < n; i++) px[i] = g_doc[i] | 0xFF000000u;
	unsigned len = 0; unsigned char *b;
	if (ends_with (path, ".jpg") || ends_with (path, ".jpeg")) b = pngsave::jpeg_encode (px, g_dw, g_dh, 92, &len);
	else if (ends_with (path, ".bmp")) b = pngsave::bmp_encode (px, g_dw, g_dh, &len);
	else b = pngsave::png_encode (px, g_dw, g_dh, false, &len);
	delete[] px;
	bool ok = b && kapi_save_file (path, b, len) == (int) len;
	delete[] b;
	if (!ok) { char m[360]; snprintf (m, sizeof m, "The picture could not be saved in %s.", path); uk_messagebox ("Screenshot", m, MB_OK); return; }
	snprintf (g_savedAs, sizeof g_savedAs, "%s", path);
	g_dirty = false;
	char *sl = strrchr (path, '/'); if (sl) { *sl = 0; snprintf (g_lastDir, sizeof g_lastDir, "%s", path); }
	settings_save ();
	refresh_ui ();
}
static void undo () { if (!can_undo ()) return; op_apply (g_ops[--g_opPos], true); g_dirty = true; doc_render (); refresh_ui (); g_view->invalidate (true); }
static void redo () { if (!can_redo ()) return; op_apply (g_ops[g_opPos++], false); g_dirty = true; doc_render (); refresh_ui (); g_view->invalidate (true); }
static void set_tool (int t) { if (t == TOOL_CROP && g_tool != TOOL_CROP) g_prevTool = g_tool; g_tool = t; g_view->eraseHot = -1; g_view->dragging = false; refresh_ui (); g_view->invalidate (true); }

// The window: small with no capture; else as large as the capture needs, in the work area.
static void resize_root (int cw, int ch)
{
	struct kapi_win_geom g;
	if (kapi_win_geometry (&g) != 0) return;
	int fw = g.w - g.cw, fh = g.h - g.ch;
	if (g.aw > 0 && cw > g.aw - fw) cw = g.aw - fw;
	if (g.ah > 0 && ch > g.ah - fh) ch = g.ah - fh;
	if (cw == g_root->width && ch == g_root->height) return;
	if (g_root->maximised ()) return;
	int stride = cw;
	unsigned *fb = kapi_resize_window2 (cw, ch, &stride);
	if (!fb) return;
	g_root->canvas.adopt (fb, cw, ch, stride);
	g_root->width = cw; g_root->height = ch;
	g_root->layout (); g_root->invalidate (true);
	uk_decorate_window ();
	int x = g.x, y = g.y;
	if (g.aw > 0)
	{
		if (x + cw + fw > g.ax + g.aw) x = g.ax + g.aw - cw - fw;
		if (y + ch + fh > g.ay + g.ah) y = g.ay + g.ah - ch - fh;
		if (x < g.ax) x = g.ax; if (y < g.ay) y = g.ay;
	}
	if (x != g.x || y != g.y) kapi_move_window (x, y);
}

// ---- the capture: hiding, grabbing, choosing -----------------------------------------------------------------
// The screen frozen, full screen; a rectangle dragged or a window clicked. -> 1 and the part chosen
// (and its name), 0 cancelled.
struct Overlay
{
	unsigned *frozen, *dim; int W, H;
	unsigned *fb;
	int mode;
	int mx, my; unsigned btn; bool down, dragging; int ax, ay;
	bool done; int result; int rx, ry, rw, rh;
	kapi_win_info wins[48]; int nwin; int hotWin;
	int keyQ[8]; int nkey;
	int barX, barY, barW, barH;
	char title[64];
};
static Overlay *g_ov;

static void ov_ptr (unsigned long, int ev, gui_value v)
{
	Overlay &o = *g_ov;
	o.mx = GUI_PTR_X (v); o.my = GUI_PTR_Y (v);
	if (ev == GUI_EVENT_PTR_DOWN && (GUI_PTR_CHANGED (v) & 1)) o.btn |= 1, o.down = true;
	if (ev == GUI_EVENT_PTR_UP && (GUI_PTR_CHANGED (v) & 1)) o.btn &= ~1u;
	if (ev == GUI_EVENT_PTR_DOWN && (GUI_PTR_CHANGED (v) & 2)) { if (o.nkey < 8) o.keyQ[o.nkey++] = 27; }	// a right click: cancel
}
static void ov_key (unsigned long, int ev, gui_value v) { if (ev == GUI_EVENT_KEY && g_ov->nkey < 8) g_ov->keyQ[g_ov->nkey++] = (int) v; }

static int ov_bar_item (Overlay &o, int x, int y)
{
	if (y < o.barY || y >= o.barY + o.barH || x < o.barX || x >= o.barX + o.barW) return -1;
	int i = (x - o.barX - 8) / 44;
	if (i >= 0 && i < 3) return i;
	if (x >= o.barX + o.barW - 44) return 3;			// close
	return -2;
}
static void ov_windows (Overlay &o)
{
	int pid = 0;
	{	// (mine: the one minimised)
		kapi_win_info all[48]; int n = kapi_win_list (all, 48);
		o.nwin = 0;
		for (int i = n - 1; i >= 0 && o.nwin < 48; i--)	// top first
		{
			kapi_win_info &w = all[i];
			if (w.id == KAPI_WIN_DESKTOP) continue;
			if (w.flags & (WIN_FLAG_BACKMOST | WIN_FLAG_SYSTEM)) continue;
			if (w.state & (KAPI_WIN_MINIMISED | KAPI_WIN_OFFDESK | KAPI_WIN_FULLSCREEN)) continue;
			if (w.alpha == 0 || w.w <= 0 || w.h <= 0) continue;
			if (w.ow > 0) { w.x -= w.il; w.y -= w.it; w.w = w.ow; w.h = w.oh; }
			o.wins[o.nwin++] = w;
		}
	}
	(void) pid;
}
static int ov_window_at (Overlay &o, int x, int y)
{
	for (int i = 0; i < o.nwin; i++) { kapi_win_info &w = o.wins[i]; if (x >= w.x && y >= w.y && x < w.x + w.w && y < w.y + w.h) return i; }
	return -1;
}
static void ov_bright (Overlay &o, int x0, int y0, int w, int h)
{
	if (x0 < 0) { w += x0; x0 = 0; } if (y0 < 0) { h += y0; y0 = 0; }
	if (x0 + w > o.W) w = o.W - x0; if (y0 + h > o.H) h = o.H - y0;
	if (w <= 0 || h <= 0) return;
	for (int y = y0; y < y0 + h; y++) memcpy (o.fb + (long) y * o.W + x0, o.frozen + (long) y * o.W + x0, (size_t) w * 4);
}
static void ov_pill (Canvas &cv, int cx, int y, const char *s, unsigned bg, int alpha, int style = 0)
{
	int w = uk_tw (s, style) + 32;
	uk_rbox (cv, cx - w / 2, y, w, 32, 16, bg, bg, alpha);
	uk_text_c (cv, cx - w / 2, y, w, 32, s, 0xFFFFFF, style);
}
static void ov_cursor (Canvas &cv, Overlay &o, bool arrow)
{
	int x = o.mx, y = o.my;
	if (arrow)
	{
		int a[] = { V (x), V (y), V (x), V (y + 18), V (x + 4) + 8, V (y + 14), V (x + 8), V (y + 21), V (x + 11), V (y + 20), V (x + 7) + 8, V (y + 13), V (x + 13), V (y + 13) };
		VPath p; p.poly (a, 7); p.fill (cv, 0x000000);
		VPath q; q.polyline (a, 7, 18, true); q.fill (cv, 0xFFFFFF);
		return;
	}
	for (int d = 0; d < 2; d++)
	{
		VPath b, w;
		int dx = d == 0, dy = d == 1;
		b.line (V (x - 12 * dx), V (y - 12 * dy), V (x - 3 * dx), V (y - 3 * dy), V (3)); b.line (V (x + 3 * dx), V (y + 3 * dy), V (x + 12 * dx), V (y + 12 * dy), V (3));
		w.line (V (x - 12 * dx), V (y - 12 * dy), V (x - 3 * dx), V (y - 3 * dy), 20); w.line (V (x + 3 * dx), V (y + 3 * dy), V (x + 12 * dx), V (y + 12 * dy), 20);
		b.fill (cv, 0x000000, 200); w.fill (cv, 0xFFFFFF);
	}
}
static void ov_draw (Overlay &o)
{
	memcpy (o.fb, o.dim, (size_t) o.W * o.H * 4);
	Canvas cv; cv.adopt (o.fb, o.W, o.H);
	bool overBar = ov_bar_item (o, o.mx, o.my) != -1;
	if (o.mode == MODE_RECT)
	{
		if (o.dragging)
		{
			int x0 = o.ax < o.mx ? o.ax : o.mx, y0 = o.ay < o.my ? o.ay : o.my;
			int w = abs (o.mx - o.ax) + 1, h = abs (o.my - o.ay) + 1;
			ov_bright (o, x0, y0, w, h);
			uk_rline (cv, x0 - 1, y0 - 1, w + 2, h + 2, 0, 0xFFFFFF);
			uk_rline (cv, x0 - 2, y0 - 2, w + 4, h + 4, 0, C_ACCENT, 160);
			char t[32]; snprintf (t, sizeof t, "%d x %d", w, h);
			UkFaceScope f (g_small);
			int tw = uk_tw (t, 2) + 16, ty = y0 + h + 8 + 22 <= o.H ? y0 + h + 8 : y0 - 30; if (ty < 0) ty = y0 + 6;
			uk_rbox (cv, x0, ty, tw, 22, 6, 0x202024, 0x202024, 215);
			uk_text_c (cv, x0, ty, tw, 22, t, 0xFFFFFF, 2);
		}
		if (!overBar)							// a magnifier at the pointer
		{
			const int Z = 6, N = 21, M = N * Z;
			int mx0 = o.mx + 24, my0 = o.my + 24;
			if (mx0 + M + 4 > o.W) mx0 = o.mx - 24 - M; if (my0 + M + 28 > o.H) my0 = o.my - 24 - M - 22;
			for (int j = 0; j < N; j++)
				for (int i = 0; i < N; i++)
				{
					int sx = o.mx - N / 2 + i, sy = o.my - N / 2 + j;
					unsigned c = sx >= 0 && sy >= 0 && sx < o.W && sy < o.H ? o.frozen[(long) sy * o.W + sx] : 0x000000;
					cv.fillRect (mx0 + i * Z, my0 + j * Z, Z, Z, c);
				}
			cv.fillRect (mx0 + (N / 2) * Z, my0, Z, M, 0); cv.fillRect (mx0, my0 + (N / 2) * Z, M, Z, 0);	// (the centre's cross, see-through)
			for (int k = 0; k < N; k++) if (k != N / 2) { cv.fillRect (mx0 + (N / 2) * Z, my0 + k * Z, Z, Z, o.frozen[(long) (o.my - N / 2 + k < 0 ? 0 : o.my - N / 2 + k >= o.H ? o.H - 1 : o.my - N / 2 + k) * o.W + o.mx]); }
			for (int k = 0; k < N; k++) if (k != N / 2) { cv.fillRect (mx0 + k * Z, my0 + (N / 2) * Z, Z, Z, o.frozen[(long) o.my * o.W + (o.mx - N / 2 + k < 0 ? 0 : o.mx - N / 2 + k >= o.W ? o.W - 1 : o.mx - N / 2 + k)]); }
			uk_rline (cv, mx0 + (N / 2) * Z - 1, my0 + (N / 2) * Z - 1, Z + 2, Z + 2, 0, 0xFFFFFF);
			uk_rline (cv, mx0 - 1, my0 - 1, M + 2, M + 2, 0, 0xFFFFFF);
			char t[32]; snprintf (t, sizeof t, "%d, %d", o.mx, o.my);
			UkFaceScope f (g_small);
			uk_rbox (cv, mx0 - 1, my0 + M + 1, M + 2, 20, 0, 0x202024, 0x202024, 220);
			uk_text_c (cv, mx0, my0 + M + 1, M, 20, t, 0xFFFFFF);
		}
	}
	else if (o.mode == MODE_WINDOW && o.hotWin >= 0)
	{
		kapi_win_info &w = o.wins[o.hotWin];
		ov_bright (o, w.x, w.y, w.w, w.h);
		for (int i = 0; i < 3; i++) uk_rline (cv, w.x - 2 - i, w.y - 2 - i, w.w + 4 + 2 * i, w.h + 4 + 2 * i, 6 + i, C_ACCENT, 255 - i * 50);
		char t[96]; snprintf (t, sizeof t, "%s  -  %d x %d", w.title[0] ? w.title : "Window", w.w, w.h);
		int cy = w.y + w.h / 2 - 16; if (cy < 70) cy = 70;
		ov_pill (cv, w.x + w.w / 2, cy, t, C_ACCENT, 235, 2);
	}
	// the bar: the mode, and close
	uk_rbox (cv, o.barX, o.barY, o.barW, o.barH, 12, C_FIELD, C_FIELD, 245);
	uk_rline (cv, o.barX, o.barY, o.barW, o.barH, 12, uk_mix (C_FIELD, 0, 110));
	int hot = ov_bar_item (o, o.mx, o.my);
	static const int IC[3] = { IC_RECT, IC_WINDOW, IC_SCREEN };
	for (int i = 0; i < 3; i++)
	{
		int bx = o.barX + 8 + i * 44, by = o.barY + 6;
		if (i == o.mode) uk_rbox (cv, bx, by, 40, 36, 8, C_ACCENT, C_ACCENT);
		else if (i == hot) uk_rbox (cv, bx, by, 40, 36, 8, uk_mix (C_FIELD, C_ACCENT, 50), uk_mix (C_FIELD, C_ACCENT, 50));
		draw_icon (cv, IC[i], bx + 11, by + 9, 18, i == o.mode ? C_SEL_TEXT : C_FIELD_TEXT, false);
	}
	int cx = o.barX + o.barW - 40;
	cv.fillRect (cx - 4, o.barY + 10, 1, o.barH - 20, uk_mix (C_FIELD, 0, 60));
	if (hot == 3) uk_rbox (cv, cx, o.barY + 6, 34, 36, 8, uk_mix (C_FIELD, 0xE04030, 70), uk_mix (C_FIELD, 0xE04030, 70));
	uk_glyph (cv, WKG_CLOSE, cx + 17, o.barY + o.barH / 2, 12, C_FIELD_TEXT);
	// the hint
	const char *h = o.mode == MODE_RECT ? "Drag a rectangle   -   Enter: the whole screen   -   Esc: cancel"
		      : "Click a window   -   Tab: the next one   -   Esc: cancel";
	ov_pill (cv, o.W / 2, o.H - 110, h, 0x18181C, 200);
	ov_cursor (cv, o, overBar || o.mode == MODE_WINDOW);
	kapi_present_fb ();
}
static bool ov_run (Overlay &o)
{
	int w, h; unsigned *fb = kapi_fullscreen_begin (&w, &h);
	if (!fb || w != o.W || h != o.H) { if (fb) kapi_fullscreen_end (); return false; }
	o.fb = fb;
	o.barH = 48; o.barW = 8 + 3 * 44 + 6 + 44; o.barX = (o.W - o.barW) / 2; o.barY = 12;
	kapi_cursor_pos (&o.mx, &o.my);
	g_ov = &o;
	kapi_set_pointer_handler (ov_ptr);
	kapi_set_key_handler (ov_key);
	ov_windows (o);
	o.hotWin = ov_window_at (o, o.mx, o.my);
	int lmx = -1, lmy = -1, lmode = -1, lhot = -2; bool ldrag = false;
	unsigned lastBtn = 0;
	while (!o.done && !kapi_should_exit ())
	{
		kapi_pump_events ();
		for (int i = 0; i < o.nkey; i++)
		{
			int k = o.keyQ[i];
			if (k == 27) { o.done = true; o.result = 0; }
			else if (k == KEY_ENTER)
			{
				if (o.mode == MODE_WINDOW && o.hotWin >= 0) { kapi_win_info &wi = o.wins[o.hotWin]; o.rx = wi.x; o.ry = wi.y; o.rw = wi.w; o.rh = wi.h; snprintf (o.title, sizeof o.title, "%s", wi.title); }
				else { o.rx = o.ry = 0; o.rw = o.W; o.rh = o.H; o.mode = MODE_FULL; }
				o.done = true; o.result = 1;
			}
			else if (k == KEY_TAB && o.mode == MODE_WINDOW && o.nwin) { o.hotWin = (o.hotWin + 1) % o.nwin; lhot = -3; }
		}
		o.nkey = 0;
		if (o.done) break;
		if (o.mode == MODE_WINDOW && (o.mx != lmx || o.my != lmy)) o.hotWin = ov_window_at (o, o.mx, o.my);
		if (o.down)
		{
			o.down = false;
			int b = ov_bar_item (o, o.mx, o.my);
			if (b >= 0 && b < 3)
			{
				if (b == MODE_FULL) { o.rx = o.ry = 0; o.rw = o.W; o.rh = o.H; o.mode = MODE_FULL; o.done = true; o.result = 1; break; }
				o.mode = b; g_mode = b; o.hotWin = ov_window_at (o, o.mx, o.my);
			}
			else if (b == 3) { o.done = true; o.result = 0; break; }
			else if (b == -1)
			{
				if (o.mode == MODE_RECT) { o.dragging = true; o.ax = o.mx; o.ay = o.my; }
				else if (o.hotWin >= 0)
				{
					kapi_win_info &wi = o.wins[o.hotWin];
					o.rx = wi.x; o.ry = wi.y; o.rw = wi.w; o.rh = wi.h; snprintf (o.title, sizeof o.title, "%s", wi.title);
					o.done = true; o.result = 1; break;
				}
			}
		}
		if (o.dragging && !(o.btn & 1))
		{
			o.dragging = false;
			int x0 = o.ax < o.mx ? o.ax : o.mx, y0 = o.ay < o.my ? o.ay : o.my;
			int w2 = abs (o.mx - o.ax) + 1, h2 = abs (o.my - o.ay) + 1;
			if (w2 >= 4 && h2 >= 4) { o.rx = x0; o.ry = y0; o.rw = w2; o.rh = h2; o.done = true; o.result = 1; break; }
		}
		if (o.mx != lmx || o.my != lmy || o.mode != lmode || o.hotWin != lhot || o.dragging != ldrag || o.btn != lastBtn)
		{
			lmx = o.mx; lmy = o.my; lmode = o.mode; lhot = o.hotWin; ldrag = o.dragging; lastBtn = o.btn;
			ov_draw (o);
		}
		else kapi_msleep (8);
	}
	kapi_fullscreen_end ();
	g_ov = 0;
	g_root->attach ();						// (the window's event streams again)
	// clip to the screen
	if (o.result)
	{
		if (o.rx < 0) { o.rw += o.rx; o.rx = 0; } if (o.ry < 0) { o.rh += o.ry; o.ry = 0; }
		if (o.rx + o.rw > o.W) o.rw = o.W - o.rx; if (o.ry + o.rh > o.H) o.rh = o.H - o.ry;
		if (o.rw <= 0 || o.rh <= 0) o.result = 0;
	}
	return o.result != 0;
}

static void after_capture ()
{
	// the window back, as large as the capture wants
	kapi_raise_app ("screenshot");
	if (g_base)
	{
		refresh_ui ();						// (Copy, Save As placed)
		int minW = g_bSave->left + g_bSave->width + 24 + (g_bar->width - g_bPen->left);	// the whole toolbar
		int cw = g_dw + 48, ch = g_bar->height + g_dh + 48 + g_status->height;
		if (cw < minW) cw = minW; if (cw < 820) cw = 820; if (ch < 380) ch = 380;
		resize_root (cw, ch);
		g_copyPending = true;					// (once the window is drawn: clipd may take a while to start)
	}
	refresh_ui ();
	g_root->invalidate (true);
}
// grab the screen now (the window is hidden): the full screen, a window (id), or the overlay's choice
static void grab_now (int mode, unsigned winId)
{
	int W = 0, H = 0; kapi_screen_size (&W, &H);
	if (W <= 0 || H <= 0) return;
	unsigned *frozen = new unsigned[(unsigned) W * H];
	if (!kapi_screen_grab (frozen, W, H)) { delete[] frozen; return; }
	Overlay *o = new Overlay;
	memset (o, 0, sizeof *o);
	o->frozen = frozen; o->W = W; o->H = H; o->mode = mode;
	bool ok = false;
	if (mode == -2)							// a window given (Alt+Print Screen)
	{
		ov_windows (*o);
		for (int i = 0; i < o->nwin; i++)
			if (o->wins[i].id == winId) { kapi_win_info &w = o->wins[i]; o->rx = w.x; o->ry = w.y; o->rw = w.w; o->rh = w.h; snprintf (o->title, sizeof o->title, "%s", w.title); ok = true; }
		if (!ok) { o->rx = o->ry = 0; o->rw = W; o->rh = H; ok = true; mode = MODE_FULL; }
		else mode = MODE_WINDOW;
		if (o->rx < 0) { o->rw += o->rx; o->rx = 0; } if (o->ry < 0) { o->rh += o->ry; o->ry = 0; }
		if (o->rx + o->rw > W) o->rw = W - o->rx; if (o->ry + o->rh > H) o->rh = H - o->ry;
		ok = o->rw > 0 && o->rh > 0;
	}
	else if (mode == MODE_FULL) { o->rx = o->ry = 0; o->rw = W; o->rh = H; ok = true; }
	else
	{
		o->dim = new unsigned[(unsigned) W * H];
		for (long i = 0; i < (long) W * H; i++) { unsigned c = frozen[i]; o->dim[i] = ((c >> 1) & 0x7F7F7F) - ((c >> 3) & 0x1F1F1F); }
		ok = ov_run (*o);
		mode = o->mode;
		delete[] o->dim;
	}
	if (ok)
	{
		strokes_free ();
		delete[] g_base;
		g_base = new unsigned[(unsigned) o->rw * o->rh]; g_bw = o->rw; g_bh = o->rh;
		for (int y = 0; y < o->rh; y++) memcpy (g_base + (long) y * o->rw, frozen + (long) (o->ry + y) * W + o->rx, (size_t) o->rw * 4);
		g_crop[0] = g_crop[1] = 0; g_crop[2] = g_bw; g_crop[3] = g_bh;
		if (mode == MODE_WINDOW) snprintf (g_what, sizeof g_what, "Window \xE2\x80\x98%s\xE2\x80\x99", o->title[0] ? o->title : "untitled");
		else snprintf (g_what, sizeof g_what, "%s", MODE_NAME[mode]);
		g_savedAs[0] = 0; g_dirty = true;
		doc_render ();
	}
	delete[] frozen; delete o;
	settings_save ();
	after_capture ();
}

// New / Print Screen: count down (in the window), then hide, then grab.
static void start_capture (int mode, unsigned winId)
{
	if (g_state != ST_IDLE) return;
	g_view->live = 0; g_view->dragging = false;
	g_pending = mode; g_pendWin = winId;
	if (mode != -2 && DELAYS[g_delay] > 0)
	{
		g_state = ST_COUNT; g_t0 = kapi_get_ticks ();
		kapi_raise_app ("screenshot");
		refresh_ui (); g_view->invalidate (true);
		return;
	}
	g_state = ST_HIDE; g_t0 = kapi_get_ticks ();
	kapi_win_minimise (0);
	refresh_ui ();
}
static void cancel_countdown () { if (g_state == ST_COUNT) { g_state = ST_IDLE; refresh_ui (); g_view->invalidate (true); } }

// ---- the toolbar's callbacks ------------------------------------------------------------------------------
static void abs_pos (Widget *w, int *x, int *y) { *x = *y = 0; for (Widget *p = w; p && p != g_root; p = p->parent) { *x += p->left; *y += p->top; } }
static void b_new (Widget &) { start_capture (g_mode, 0); }
static void mode_menu (Widget &w)
{
	int x, y; abs_pos (&w, &x, &y);
	PickMenu m (x, y + w.height + 2, g_mode);
	m.add ("Rectangle", IC_RECT); m.add ("Window", IC_WINDOW); m.add ("Full screen", IC_SCREEN);
	int r = m.run ();
	if (r >= 0) { g_mode = r; settings_save (); refresh_ui (); }
}
static void mode_arrow (ToolButton &b) { mode_menu (b); }
static void delay_menu (Widget &w)
{
	int x, y; abs_pos (&w, &x, &y);
	PickMenu m (x, y + w.height + 2, g_delay);
	m.add ("No delay", -1); m.add ("3 seconds", -1); m.add ("5 seconds", -1); m.add ("10 seconds", -1);
	int r = m.run ();
	if (r >= 0) { g_delay = r; settings_save (); refresh_ui (); }
}
static void delay_arrow (ToolButton &b) { delay_menu (b); }
static void b_copy (Widget &) { copy_doc (false); refresh_ui (); }
static void b_save (Widget &) { save_as (); }
static void palette (ToolButton &b, bool marker)
{
	set_tool (marker ? TOOL_MARKER : TOOL_PEN);
	int x, y; abs_pos (&b, &x, &y);
	Palette p (x - 20, y + b.height + 2, marker);
	p.run ();
	settings_save ();
	refresh_ui ();
}
static void pen_arrow (ToolButton &b) { palette (b, false); }
static void mark_arrow (ToolButton &b) { palette (b, true); }
static void b_pen (Widget &) { set_tool (TOOL_PEN); }
static void b_mark (Widget &) { set_tool (TOOL_MARKER); }
static void b_erase (Widget &) { set_tool (TOOL_ERASER); }
static void b_crop (Widget &) { set_tool (g_tool == TOOL_CROP ? g_prevTool : TOOL_CROP); }
static void b_undo (Widget &) { undo (); }
static void b_redo (Widget &) { redo (); }

static void refresh_ui ()
{
	bool has = g_base != 0, idle = g_state == ST_IDLE;
	static const int MODE_IC[3] = { IC_RECT, IC_WINDOW, IC_SCREEN };
	g_bMode->setIcon (draw_icon, MODE_IC[g_mode]);
	g_bMode->tip = g_mode == MODE_RECT ? "Mode: a rectangle" : g_mode == MODE_WINDOW ? "Mode: a window" : "Mode: the full screen";
	g_bDelay->setIcon (draw_icon, g_delay ? IC_CLOCK : IC_CLOCK_OFF);
	char d[8]; snprintf (d, sizeof d, "%d s", DELAYS[g_delay]);
	g_bDelay->setText (g_delay ? d : "");
	g_bDelay->fitWidth ();
	int x = g_bDelay->left + g_bDelay->width + 2;			// Copy, Save As: after it
	g_sep2->left = x; x += g_sep2->width + 2;
	g_bCopy->left = x; g_bSave->left = x + g_bCopy->width + 2;
	g_sep2->hidden = !has;
	g_bNew->setDisabled (!idle);
	ToolButton *cap[] = { g_bCopy, g_bSave, g_bPen, g_bMark, g_bErase, g_bCrop, g_bUndo, g_bRedo };
	for (ToolButton *b : cap) { b->hidden = !has; b->setDisabled (!idle); }
	g_bPen->setOn (g_tool == TOOL_PEN); g_bMark->setOn (g_tool == TOOL_MARKER);
	g_bErase->setOn (g_tool == TOOL_ERASER); g_bCrop->setOn (g_tool == TOOL_CROP);
	g_bUndo->setDisabled (!can_undo () || !idle); g_bRedo->setDisabled (!can_redo () || !idle);
	g_bPen->invalidate (true); g_bMark->invalidate (true);
	g_bar->invalidate (true);
	g_status->hidden = !has;
	if (has)
	{
		char l[220], r[120];
		const char *state = g_copiedGen == g_docGen ? "copied to the clipboard  -  not saved" : g_savedAs[0] ? "changed since saved" : "not saved";
		const char *sa = g_savedAs[0] ? strrchr (g_savedAs, '/') : 0;
		if (g_savedAs[0] && !g_dirty) snprintf (l, sizeof l, "%s  -  %d x %d  -  saved as %s", g_what, g_dw, g_dh, sa ? sa + 1 : g_savedAs);
		else snprintf (l, sizeof l, "%s  -  %d x %d  -  %s", g_what, g_dw, g_dh, state);
		const char *hint = g_tool == TOOL_PEN || g_tool == TOOL_MARKER ? "Drag to draw, Shift: a straight line" :
				   g_tool == TOOL_ERASER ? "Click a stroke to take it away" : "Drag the part to keep";
		snprintf (r, sizeof r, "%s     %d %%", hint, g_view->zoom ());
		g_status->set (l, r);
	}
	g_view->top = g_bar->height;
	int vh = g_root->height - g_bar->height - (has ? g_status->height : 0);
	if (vh != g_view->height) { g_view->resizeTo (g_view->width, vh); }
	g_root->invalidate (false);
}

// ---- the window -------------------------------------------------------------------------------------------
class ShotRoot : public Root
{
public:
	ShotRoot (int w, int h) : Root (w, h, "Screenshot") {}
	void onResized () override { refresh_ui (); g_view->invalidate (true); }
	void onTick () override
	{
		if (g_copyPending && valid) { g_copyPending = false; copy_doc (true); }
		// the kernel's Print Screen
		int from, type; char buf[64]; int n;
		while ((n = kapi_mailbox_recv (&from, &type, buf, sizeof buf - 1, 0)) >= 0)
		{
			if (type != SHOT_MSG_NOW) continue;
			buf[n] = 0;
			if (!strncmp (buf, "window ", 7)) start_capture (-2, (unsigned) strtoul (buf + 7, 0, 0));
			else start_capture (g_mode, 0);
		}
		if (g_state == ST_COUNT)
		{
			int el = (int) (kapi_get_ticks () - g_t0);
			if (el >= DELAYS[g_delay] * 100) { g_state = ST_HIDE; g_t0 = kapi_get_ticks (); kapi_win_minimise (0); }
			g_view->invalidate (true);
		}
		else if (g_state == ST_HIDE && kapi_get_ticks () - g_t0 >= 35)	// (the compositor has drawn the screen without us)
		{
			g_state = ST_IDLE;
			grab_now (g_pending, g_pendWin);
		}
	}
	bool onKey (long k) override
	{
		if (k == 27 && g_state == ST_COUNT) { cancel_countdown (); return true; }
		if (k == 27 && g_view->dragging) { g_view->dragging = false; g_view->invalidate (true); return true; }
		if (!g_base || g_state != ST_IDLE) return false;
		if (k == 'p' || k == 'P') { set_tool (TOOL_PEN); return true; }
		if (k == 'm' || k == 'M') { set_tool (TOOL_MARKER); return true; }
		if (k == 'e' || k == 'E') { set_tool (TOOL_ERASER); return true; }
		if (k == 'r' || k == 'R') { set_tool (TOOL_CROP); return true; }
		return false;
	}
};

static void m_new () { start_capture (g_mode, 0); }
static void m_rect () { g_mode = MODE_RECT; settings_save (); start_capture (g_mode, 0); }
static void m_window () { g_mode = MODE_WINDOW; settings_save (); start_capture (g_mode, 0); }
static void m_full () { g_mode = MODE_FULL; settings_save (); start_capture (g_mode, 0); }
static void m_save () { if (g_base) save_as (); }
static void m_copy () { if (g_base) { copy_doc (false); refresh_ui (); } }
static void m_undo () { undo (); }
static void m_redo () { redo (); }
static void m_pen () { if (g_base) set_tool (TOOL_PEN); }
static void m_mark () { if (g_base) set_tool (TOOL_MARKER); }
static void m_erase () { if (g_base) set_tool (TOOL_ERASER); }
static void m_crop () { if (g_base) set_tool (TOOL_CROP); }
static void m_clear ()
{
	if (!g_base) return;
	bool any = false;
	for (int i = 0; i < g_nst; i++) if (g_st[i].alive) any = true;
	if (!any) return;
	for (int i = 0; i < g_nst; i++) if (g_st[i].alive) { Op o; o.kind = OP_ERASE; o.stroke = i; op_push (o); g_st[i].alive = false; }
	g_dirty = true; doc_render (); refresh_ui (); g_view->invalidate (true);
}

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);
	g_small = new FtTextFace; if (!g_small->open ("DejaVu Sans", 11)) { delete g_small; g_small = 0; }
	g_big = new FtTextFace; if (!g_big->open ("DejaVu Sans", 40)) { delete g_big; g_big = 0; }
	settings_load ();

	const int W = 560, H = 200;
	ShotRoot root (W, H);
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	root.setResizable (true);

	g_bar = new ToolBar (0, 0, W, 50);
	g_bar->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	g_bar->line = true;
	g_bNew = (new ToolButton (92, 34, "New capture (Ctrl+N)", b_new))->setGlyph (WKT_PLUS)->setText ("New");
	g_bNew->on = true; g_bNew->filled = true;
	g_bar->add (g_bNew, 4);
	g_bar->sep ();
	g_bMode = (new ToolButton (40, 34, "Mode", mode_menu))->setIcon (draw_icon, IC_RECT)->setSplit (mode_arrow);
	g_bar->add (g_bMode);
	g_bDelay = (new ToolButton (40, 34, "Delay", delay_menu))->setIcon (draw_icon, IC_CLOCK_OFF)->setSplit (delay_arrow);
	g_bar->add (g_bDelay, 4);
	g_sep2 = new Sep (34);
	g_bar->add (g_sep2, 2);
	g_bCopy = (new ToolButton (0, 34, "Copy (Ctrl+C)", b_copy))->setGlyph (WKT_COPY)->setText ("Copy")->fitWidth ();
	g_bar->add (g_bCopy);
	g_bSave = (new ToolButton (0, 34, "Save As... (Ctrl+S) -- PNG, JPEG or BMP", b_save))->setGlyph (WKT_SAVE)->setText ("Save As...")->fitWidth ();
	g_bar->add (g_bSave, 2);
	// the drawing tools, from the right
	g_bRedo = (new ToolButton (34, 34, "Redo (Ctrl+Y)", b_redo))->setGlyph (WKT_REDO);
	g_bar->addRight (g_bRedo, 6);
	g_bUndo = (new ToolButton (34, 34, "Undo (Ctrl+Z)", b_undo))->setGlyph (WKT_UNDO);
	g_bar->addRight (g_bUndo, 2);
	g_bCrop = (new ToolButton (38, 34, "Crop (R)", b_crop))->setIcon (draw_icon, IC_CROP)->setToggle (true);
	g_bar->addRight (g_bCrop, 14);
	g_bErase = (new ToolButton (38, 34, "Eraser (E) -- a stroke at a time", b_erase))->setIcon (draw_icon, IC_ERASER)->setToggle (true);
	g_bar->addRight (g_bErase, 14);
	g_bMark = (new ToolButton (38, 34, "Marker (M)", b_mark))->setIcon (draw_icon, IC_MARKER)->setToggle (true)->setSplit (mark_arrow);
	g_bar->addRight (g_bMark, 2);
	g_bPen = (new ToolButton (38, 34, "Pen (P)", b_pen))->setIcon (draw_icon, IC_PEN)->setToggle (true)->setSplit (pen_arrow);
	g_bar->addRight (g_bPen, 2);
	ToolButton *tools[] = { g_bCrop, g_bErase, g_bMark, g_bPen };
	for (ToolButton *b : tools) b->toggle = false;		// (one tool at a time: set_tool)
	root.addChild (g_bar);

	g_view = new ShotView (0, 50, W, H - 50);
	g_view->anchor = ANCHOR_FILL;
	root.addChild (g_view);
	g_status = new StatusBar (0, H - 26, W, 26);
	g_status->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	root.addChild (g_status);

	static Menu menu;
	menu.menu ("File");
	menu.item ("New Capture", "^N", UK_CTRL ('N'), m_new);
	menu.item ("New Rectangle", "", 0, m_rect);
	menu.item ("New Window", "", 0, m_window);
	menu.item ("New Full Screen", "", 0, m_full);
	menu.separator ();
	menu.item ("Save As...", "^S", UK_CTRL ('S'), m_save);
	menu.item ("Copy", "^C", UK_CTRL ('C'), m_copy);
	menu.menu ("Edit");
	menu.item ("Undo", "^Z", UK_CTRL ('Z'), m_undo);
	menu.item ("Redo", "^Y", UK_CTRL ('Y'), m_redo);
	menu.separator ();
	menu.item ("Clear the Drawing", "", 0, m_clear);
	menu.menu ("Tools");
	menu.item ("Pen", "P", 0, m_pen);
	menu.item ("Marker", "M", 0, m_mark);
	menu.item ("Eraser", "E", 0, m_erase);
	menu.item ("Crop", "R", 0, m_crop);
	menu.publish ();

	kapi_ipc_register (SHOT_SERVICE);
	refresh_ui ();

	char args[64]; int na = kapi_get_args (args, sizeof args); args[na > 0 && na < 64 ? na : 0] = 0;
	if (strstr (args, "--window")) start_capture (-2, (unsigned) strtoul (strstr (args, "--window") + 8, 0, 0));
	else if (strstr (args, "--now")) start_capture (g_mode, 0);

	root.run ();
	return 0;
}
