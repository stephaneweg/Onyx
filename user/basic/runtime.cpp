//
// basic/runtime.cpp -- /bin/basic, the Onyx BASIC runtime:  basic <program.bas> [args]
//
// Compiles the program (basic/bascomp.cpp) and runs it on the VM (basic/basvm.cpp) with an
// Onyx bas::Host:
//   * Started from a terminal (it has a stdout): PRINT / INPUT use the console -- until the
//     program opens its window (SCREEN, graphics, WINDOW, a control): then the window.
//   * Started as an app (a .app bundle with main.bas, the qbasic editor's Run, the File
//     Viewer): the first PRINT opens the window.
// The window is a QBasic-like screen (80 x 25 text cells of the kernel font = 640 x 400 by
// default; SCREEN 12 = 640 x 480, SCREEN 13 = 320 x 200) that text and graphics share, with
// wtk controls (BUTTON, TEXTBOX, ...) on top. The program's folder becomes the current
// directory, so it finds its files by relative names.
//
#include "kapi.h"
#include "applib.h"
#include "notify.h"
#include "wtk/wtk.h"
#include "basic/bas.h"

using namespace wtk;

static const unsigned PALETTE[16] = {
	0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
	0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF };

static int slen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void scpy (char *d, const char *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }

class OnyxHost;
static OnyxHost *g_host = 0;
static void on_control (Widget &w);

// ---- the screen window ---------------------------------------------------------------------------
class ScreenRoot : public Root
{
public:
	unsigned *fb; int fw, fh;
	enum { KEYQ = 64 };
	long keys[KEYQ]; int kh, kt;
	int mx, my, mb;
	ScreenRoot (int w, int h, const char *title) : Root (w, h, title), fb (0), kh (0), kt (0), mx (0), my (0), mb (0)
	{
		fb = new unsigned[w * h];
		for (int i = 0; i < w * h; i++) fb[i] = 0;
		fw = kapi_font_width (); if (fw < 1) fw = 8;
		fh = kapi_font_height (); if (fh < 1) fh = 16;
	}
	void onDraw () override
	{
		for (int y = 0; y < height; y++)
		{
			unsigned *d = canvas.px + (long) y * canvas.stride, *s = fb + (long) y * width;
			for (int x = 0; x < width; x++) d[x] = s[x];
		}
	}
	bool onKey (long k) override
	{
		int n = (kt + 1) % KEYQ;
		if (n != kh) { keys[kt] = k; kt = n; }
		return true;
	}
	bool onMouse (int x, int y, int bl, int br, int bm, int) override
	{
		if (x >= 0) { mx = x; my = y; }
		mb = (bl ? 1 : 0) | (br ? 2 : 0) | (bm ? 4 : 0);
		return true;
	}
	bool popKey (long *k) { if (kh == kt) return false; *k = keys[kh]; kh = (kh + 1) % KEYQ; return true; }
};

// ---- the host --------------------------------------------------------------------------------------
class OnyxHost : public bas::Host
{
public:
	ScreenRoot *root; bool console, windowCmd, textUsed;
	int W, H, cols, rows, crow, ccol;		// text cursor (0-based)
	unsigned fg, bg, gfg;				// text colours, graphics default colour
	unsigned lastPresent;
	char title[64], args[256];
	double tBase; unsigned tTicks0;
	enum { MAXCTL = 128 };
	Widget *ctl[MAXCTL]; int ctlKind[MAXCTL]; int nctl;
	char *ddItems[MAXCTL]; const char *ddPtr[MAXCTL][32];
	enum { EVQ = 64 };
	int evq[EVQ]; int eh, et;

	OnyxHost () : root (0), console (false), windowCmd (false), textUsed (false), W (640), H (400), cols (80), rows (25),
		crow (0), ccol (0), fg (7), bg (0), gfg (15), lastPresent (0), nctl (0), eh (0), et (0)
	{
		title[0] = 0; args[0] = 0;
		int h = 0, m = 0, s = 0;
		kapi_get_datetime (0, 0, 0, &h, &m, &s);
		tBase = h * 3600.0 + m * 60 + s; tTicks0 = kapi_get_ticks ();
		for (int i = 0; i < MAXCTL; i++) { ctl[i] = 0; ddItems[i] = 0; }
	}

	unsigned rgb (int c, unsigned def)
	{
		if (c < 0) return def;
		if (c & 0x1000000) return (unsigned) c & 0xFFFFFF;
		return PALETTE[c & 15];
	}

	// The window: created on first need (text in app mode, graphics, controls, WINDOW).
	bool ensureWindow ()
	{
		if (root) return true;
		root = new ScreenRoot (W, H, title[0] ? title : "BASIC");
		if (root->canvas.px == 0) { delete root; root = 0; return false; }
		cols = W / root->fw; rows = H / root->fh;
		root->attach ();				// (Root::run is not used: we pump ourselves)
		fillRect (0, 0, W, H, rgb (bg, 0));
		root->invalidate (true);
		present (true);
		return true;
	}
	bool windowText () { return root != 0 || !console; }

	void fillRect (int x, int y, int w, int h, unsigned c)
	{
		if (!root) return;
		if (x < 0) { w += x; x = 0; }
		if (y < 0) { h += y; y = 0; }
		if (x + w > W) w = W - x;
		if (y + h > H) h = H - y;
		for (int j = 0; j < h; j++) { unsigned *p = root->fb + (long) (y + j) * W + x; for (int i = 0; i < w; i++) p[i] = c; }
	}
	void dirty () { if (root) root->invalidate (true); }
	void present (bool force = false)
	{
		if (!root) return;
		unsigned now = kapi_get_ticks ();
		if (!force && now - lastPresent < 2) return;	// ~50 Hz at most
		lastPresent = now;
		if (!root->valid) { root->draw (); kapi_present (); }
	}
	void pump ()
	{
		pump_events ();
		if (root) { root->tooltipTick (); present (); }
	}

	// ---- text screen ---------------------------------------------------------------------------------
	void scroll ()
	{
		int fh = root->fh, n = (rows - 1) * fh * W;
		unsigned *p = root->fb;
		for (int i = 0; i < n; i++) p[i] = p[i + fh * W];
		fillRect (0, (rows - 1) * fh, W, H - (rows - 1) * fh, rgb (bg, 0));
	}
	void newline () { ccol = 0; if (++crow >= rows) { scroll (); crow = rows - 1; } }
	void putch (char c)
	{
		if (c == '\n') { newline (); return; }
		if (c == '\r') { ccol = 0; return; }
		if (c == '\t') { do putch (' '); while (ccol % 8); return; }
		if (ccol >= cols) newline ();
		int x = ccol * root->fw, y = crow * root->fh;
		fillRect (x, y, root->fw, root->fh, rgb (bg, 0));
		char s[2] = { c, 0 };
		kapi_draw_text_buf (root->fb, W, H, x, y, s, rgb (fg, 0xAAAAAA));
		ccol++;
	}
	void out (const char *s, int n) override
	{
		if (!windowText ()) { kapi_stdout_write (s, (unsigned) n); for (int i = 0; i < n; i++) ccol = s[i] == '\n' ? 0 : ccol + 1; return; }
		if (!ensureWindow ()) return;
		textUsed = true;
		for (int i = 0; i < n; i++) putch (s[i]);
		dirty ();
	}
	int inputLine (char *buf, int cap) override
	{
		if (!windowText ())
		{
			int n = 0;
			for (;;)
			{
				char c;
				if (kapi_stdin_read (&c, 1) <= 0) return n ? n : -1;
				if (c == 4) return n ? n : -1;
				if (c == '\r') continue;
				if (c == '\n') break;
				if (n < cap - 1) buf[n++] = c;
			}
			buf[n] = 0; ccol = 0;
			return n;
		}
		if (!ensureWindow ()) return -1;
		textUsed = true;
		int n = 0;
		unsigned blink = 0;
		for (;;)
		{
			// caret: an underline at the cursor
			int cx = ccol * root->fw, cy = crow * root->fh;
			bool on = ((kapi_get_ticks () - blink) / 50) % 2 == 0;
			fillRect (cx, cy + root->fh - 2, root->fw, 2, on ? rgb (fg, 0xAAAAAA) : rgb (bg, 0));
			dirty ();
			pump ();
			if (should_exit ()) return -1;
			long k;
			while (root->popKey (&k))
			{
				fillRect (cx, cy + root->fh - 2, root->fw, 2, rgb (bg, 0));
				if (k == KEY_ENTER || k == '\n') { buf[n] = 0; newline (); dirty (); return n; }
				if (k == KEY_BACKSPACE || k == 8)
				{
					if (n > 0)
					{
						n--;
						if (ccol == 0 && crow > 0) { crow--; ccol = cols; }
						ccol--;
						fillRect (ccol * root->fw, crow * root->fh, root->fw, root->fh, rgb (bg, 0));
					}
				}
				else if (k >= 32 && k < 127 && n < cap - 1) { buf[n++] = (char) k; putch ((char) k); }
				blink = kapi_get_ticks ();
				cx = ccol * root->fw; cy = crow * root->fh;
			}
			kapi_msleep (10);
		}
	}
	int inkey (char *o) override
	{
		if (!root)
		{
			if (!console) return 0;
			char c;
			if (kapi_kbd_ready () && kapi_stdin_read (&c, 1) > 0) { o[0] = c; return 1; }
			return 0;
		}
		pump ();
		long k;
		if (!root->popKey (&k)) return 0;
		int code = 0;
		switch (k)
		{
		case KEY_UP: code = 72; break; case KEY_DOWN: code = 80; break;
		case KEY_LEFT: code = 75; break; case KEY_RIGHT: code = 77; break;
		case KEY_HOME: code = 71; break; case KEY_END: code = 79; break;
		case KEY_PGUP: code = 73; break; case KEY_PGDN: code = 81; break;
		case KEY_DEL: code = 83; break;
		}
		if (code) { o[0] = 0; o[1] = (char) code; return 2; }
		if (k == KEY_BACKSPACE) { o[0] = 8; return 1; }
		if (k == KEY_ENTER) { o[0] = 13; return 1; }
		if (k > 0 && k < 256) { o[0] = (char) k; return 1; }
		return 0;
	}
	void cls () override
	{
		if (!windowText ()) { kapi_stdout_write ("\n", 1); return; }
		if (!ensureWindow ()) return;
		fillRect (0, 0, W, H, rgb (bg, 0));
		crow = ccol = 0; dirty ();
	}
	void locate (int r, int c) override
	{
		if (r > 0) crow = r - 1 < rows ? r - 1 : rows - 1;
		if (c > 0) ccol = c - 1 < cols ? c - 1 : cols - 1;
	}
	int column () override { return ccol + 1; }
	int row () override { return crow + 1; }
	void color (int f, int b) override { if (f >= 0) { fg = (unsigned) f; gfg = (unsigned) f; } if (b >= 0) bg = (unsigned) b; }
	int width () override { return cols; }

	// ---- graphics ----------------------------------------------------------------------------------
	void screen (int mode) override
	{
		int w = mode == 13 ? 320 : 640, h = mode == 12 ? 480 : mode == 13 ? 200 : 400;
		if (mode == 12 || mode == 13) gfg = 15;
		resize (w, h);
		ensureWindow ();
		cls ();
	}
	void resize (int w, int h)
	{
		W = w; H = h;
		if (!root) return;
		unsigned *nfb = new unsigned[w * h];
		for (int i = 0; i < w * h; i++) nfb[i] = 0;
		delete [] root->fb; root->fb = nfb;
		root->canvas.adopt (kapi_resize_window (w, h), w, h);
		root->width = w; root->height = h;
		cols = W / root->fw; rows = H / root->fh;
		crow = ccol = 0;
		root->invalidate (true);
	}
	void plot (int x, int y, unsigned c) { if (x >= 0 && y >= 0 && x < W && y < H) root->fb[(long) y * W + x] = c; }
	void pset (int x, int y, int c) override { if (!ensureWindow ()) return; plot (x, y, rgb (c, rgb (gfg, 0xFFFFFF))); dirty (); }
	int point (int x, int y) override
	{
		if (!root || x < 0 || y < 0 || x >= W || y >= H) return -1;
		unsigned p = root->fb[(long) y * W + x];
		for (int i = 0; i < 16; i++) if (PALETTE[i] == p) return i;
		return (int) (p | 0x1000000);
	}
	void line (int x1, int y1, int x2, int y2, int c, int box) override
	{
		if (!ensureWindow ()) return;
		unsigned col = rgb (c, rgb (gfg, 0xFFFFFF));
		if (box)
		{
			int l = x1 < x2 ? x1 : x2, r = x1 < x2 ? x2 : x1, t = y1 < y2 ? y1 : y2, b = y1 < y2 ? y2 : y1;
			if (box == 2) fillRect (l, t, r - l + 1, b - t + 1, col);
			else { fillRect (l, t, r - l + 1, 1, col); fillRect (l, b, r - l + 1, 1, col); fillRect (l, t, 1, b - t + 1, col); fillRect (r, t, 1, b - t + 1, col); }
		}
		else
		{
			int dx = x2 > x1 ? x2 - x1 : x1 - x2, dy = y2 > y1 ? y2 - y1 : y1 - y2;
			int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1, e = dx - dy;
			for (;;)
			{
				plot (x1, y1, col);
				if (x1 == x2 && y1 == y2) break;
				int e2 = 2 * e;
				if (e2 > -dy) { e -= dy; x1 += sx; }
				if (e2 < dx) { e += dx; y1 += sy; }
			}
		}
		dirty ();
	}
	void circle (int cx, int cy, int r, int c, int fill) override
	{
		if (!ensureWindow () || r < 0) return;
		unsigned col = rgb (c, rgb (gfg, 0xFFFFFF));
		int x = r, y = 0, e = 1 - r;
		while (x >= y)
		{
			if (fill)
			{
				fillRect (cx - x, cy + y, 2 * x + 1, 1, col); fillRect (cx - x, cy - y, 2 * x + 1, 1, col);
				fillRect (cx - y, cy + x, 2 * y + 1, 1, col); fillRect (cx - y, cy - x, 2 * y + 1, 1, col);
			}
			else
			{
				plot (cx + x, cy + y, col); plot (cx - x, cy + y, col); plot (cx + x, cy - y, col); plot (cx - x, cy - y, col);
				plot (cx + y, cy + x, col); plot (cx - y, cy + x, col); plot (cx + y, cy - x, col); plot (cx - y, cy - x, col);
			}
			y++;
			if (e < 0) e += 2 * y + 1; else { x--; e += 2 * (y - x) + 1; }
		}
		dirty ();
	}
	void drawText (int x, int y, const char *s, int c) override
	{
		if (!ensureWindow ()) return;
		kapi_draw_text_buf (root->fb, W, H, x, y, s, rgb (c, rgb (gfg, 0xFFFFFF)));
		dirty ();
	}
	int mouse (int what) override
	{
		if (!root) return 0;
		pump ();
		return what == 0 ? root->mx : what == 1 ? root->my : root->mb;
	}

	// ---- time --------------------------------------------------------------------------------------
	void sleepMs (int ms) override
	{
		unsigned t0 = kapi_get_ticks ();
		while ((int) ((kapi_get_ticks () - t0) * 10) < ms)
		{
			if (root) { pump (); if (should_exit ()) return; }
			int left = ms - (int) ((kapi_get_ticks () - t0) * 10);
			kapi_msleep (left > 10 ? 10 : (left > 0 ? left : 1));
		}
		if (root) pump ();
	}
	bool poll () override
	{
		if (!root) return !(console ? false : should_exit ());
		pump ();
		return !should_exit ();
	}
	double timer () override
	{
		double t = tBase + (kapi_get_ticks () - tTicks0) / 100.0;
		while (t >= 86400) t -= 86400;
		return t;
	}
	static void two (char *p, int v) { p[0] = (char) ('0' + v / 10 % 10); p[1] = (char) ('0' + v % 10); }
	void date (char *o) override
	{
		int y = 0, mo = 0, d = 0; kapi_get_datetime (&y, &mo, &d, 0, 0, 0);
		two (o, mo); o[2] = '-'; two (o + 3, d); o[5] = '-'; two (o + 6, y / 100); two (o + 8, y % 100); o[10] = 0;
	}
	void time (char *o) override
	{
		int h = 0, m = 0, s = 0; kapi_get_datetime (0, 0, 0, &h, &m, &s);
		two (o, h); o[2] = ':'; two (o + 3, m); o[5] = ':'; two (o + 6, s); o[8] = 0;
	}
	unsigned seed () override { return kapi_get_ticks () * 2654435761u; }

	// ---- GUI -----------------------------------------------------------------------------------------
	void window (const char *t, int w, int h) override
	{
		windowCmd = true;
		if (!root) { scpy (title, t, sizeof title); if (w > 0 && h > 0) { W = w; H = h; } ensureWindow (); return; }
		if (w > 0 && h > 0 && (w != W || h != H)) resize (w, h);
		fillRect (0, 0, W, H, rgb (bg, 0)); dirty ();
	}
	int control (int kind, int x, int y, int w, int h, const char *text, int val) override
	{
		if (!ensureWindow () || nctl >= MAXCTL - 1) return 0;
		int id = nctl + 1;
		Widget *wd = 0;
		switch (kind)
		{
		case bas::CTL_BUTTON:   wd = new Button (x, y, w, h, text, on_control); break;
		case bas::CTL_LABEL:    wd = new Label (x, y, w, h, text, C_TEXT, rgb (bg, 0)); break;
		case bas::CTL_TEXTBOX:  wd = new Textbox (x, y, w, h, text, on_control); break;
		case bas::CTL_CHECKBOX: wd = new Checkbox (x, y, w, h, text, val != 0, on_control, rgb (bg, 0)); break;
		case bas::CTL_PROGRESS: wd = new Progress (x, y, w, h, 0, 100, val); break;
		case bas::CTL_SLIDER:   wd = new Slider (x, y, w, h, 0, val > 0 ? val : 100, 0, on_control, rgb (bg, 0)); break;
		case bas::CTL_LISTBOX:
		{
			ListBox *lb = new ListBox (x, y, w, h, on_control, on_control);
			char item[128]; int n = 0;
			for (int i = 0; ; i++)
			{
				char c = text[i];
				if (c == '|' || c == 0) { item[n] = 0; if (n || c == '|') lb->add (item); n = 0; if (!c) break; continue; }
				if (n < 127) item[n++] = c;
			}
			wd = lb; break;
		}
		case bas::CTL_DROPDOWN:
		{
			int len = slen (text);
			char *copy = new char[len + 1]; scpy (copy, text, len + 1);
			ddItems[id] = copy;
			int n = 0; ddPtr[id][n++] = copy;
			for (int i = 0; i < len && n < 32; i++) if (copy[i] == '|') { copy[i] = 0; ddPtr[id][n++] = copy + i + 1; }
			wd = new Dropdown (x, y, w, h, ddPtr[id], n, 0, on_control);
			break;
		}
		default: return 0;
		}
		wd->tag = id;
		ctl[id] = wd; ctlKind[id] = kind; nctl++;
		root->addChild (wd);
		dirty ();
		return id;
	}
	Widget *get (int id) { return id > 0 && id <= nctl ? ctl[id] : 0; }
	void setText (int id, const char *s) override
	{
		Widget *w = get (id); if (!w) return;
		switch (ctlKind[id])
		{
		case bas::CTL_BUTTON: { Button *b = (Button *) w; scpy (b->text, s, sizeof b->text); b->invalidate (true); break; }
		case bas::CTL_LABEL: ((Label *) w)->setText (s); break;
		case bas::CTL_TEXTBOX: ((Textbox *) w)->setText (s); break;
		case bas::CTL_CHECKBOX: { Checkbox *c = (Checkbox *) w; scpy (c->text, s, sizeof c->text); c->invalidate (true); break; }
		case bas::CTL_LISTBOX: ((ListBox *) w)->add (s); break;		// SETTEXT on a list = add an item
		}
		dirty ();
	}
	int getText (int id, char *buf, int cap) override
	{
		Widget *w = get (id); buf[0] = 0; if (!w) return 0;
		switch (ctlKind[id])
		{
		case bas::CTL_BUTTON: scpy (buf, ((Button *) w)->text, cap); break;
		case bas::CTL_LABEL: scpy (buf, ((Label *) w)->text, cap); break;
		case bas::CTL_TEXTBOX: scpy (buf, ((Textbox *) w)->text, cap); break;
		case bas::CTL_CHECKBOX: scpy (buf, ((Checkbox *) w)->text, cap); break;
		case bas::CTL_LISTBOX: { ListBox *l = (ListBox *) w; scpy (buf, l->item (l->sel), cap); break; }
		case bas::CTL_DROPDOWN: { Dropdown *d = (Dropdown *) w; if (d->sel >= 0 && d->sel < d->nopts) scpy (buf, d->opts[d->sel], cap); break; }
		}
		return slen (buf);
	}
	int getValue (int id) override
	{
		Widget *w = get (id); if (!w) return 0;
		switch (ctlKind[id])
		{
		case bas::CTL_CHECKBOX: return ((Checkbox *) w)->checked ? -1 : 0;
		case bas::CTL_LISTBOX: return ((ListBox *) w)->sel;
		case bas::CTL_DROPDOWN: return ((Dropdown *) w)->sel;
		case bas::CTL_PROGRESS: return ((Progress *) w)->value;
		case bas::CTL_SLIDER: return ((Slider *) w)->value;
		}
		return 0;
	}
	void setValue (int id, int v) override
	{
		Widget *w = get (id); if (!w) return;
		switch (ctlKind[id])
		{
		case bas::CTL_CHECKBOX: ((Checkbox *) w)->checked = v != 0; w->invalidate (true); break;
		case bas::CTL_LISTBOX: ((ListBox *) w)->setSel (v); break;
		case bas::CTL_DROPDOWN: { Dropdown *d = (Dropdown *) w; if (v >= 0 && v < d->nopts) { d->sel = v; d->invalidate (true); } break; }
		case bas::CTL_PROGRESS: ((Progress *) w)->setValue (v); break;
		case bas::CTL_SLIDER: { Slider *s = (Slider *) w; s->value = v < s->vmin ? s->vmin : v > s->vmax ? s->vmax : v; s->invalidate (true); break; }
		}
		dirty ();
	}
	void pushEvent (int id) { int n = (et + 1) % EVQ; if (n != eh) { evq[et] = id; et = n; } }
	int event (bool wait) override
	{
		if (!ensureWindow ()) return -1;
		for (;;)
		{
			pump ();
			if (should_exit ()) return -1;
			if (eh != et) { int id = evq[eh]; eh = (eh + 1) % EVQ; return id; }
			if (!wait) return 0;
			kapi_msleep (10);
		}
	}

	// ---- system -----------------------------------------------------------------------------------------
	void notify (const char *t, const char *m) override { ::notify (t, m); }
	int msgbox (const char *t, const char *m, int b) override
	{
		if (!ensureWindow ()) return 0;
		int r = wk_messagebox (t, m, b);
		dirty (); present (true);
		return r;
	}
	int clipboard (char *buf, int cap) override
	{
		int type = 0; unsigned serial = 0;
		int n = kapi_clipboard_get (&type, buf, (unsigned) cap - 1, &serial);
		if (n < 0 || type != CLIP_TEXT) n = 0;
		if (n > cap - 1) n = cap - 1;
		buf[n] = 0;
		return n;
	}
	void setClipboard (const char *s) override { kapi_clipboard_set (CLIP_TEXT, s, (unsigned) slen (s)); }
	bool fileDialog (bool save, const char *dir, char *o, int cap) override
	{
		if (!ensureWindow ()) return false;
		char name[128]; scpy (name, o, sizeof name);
		bool ok = save ? wk_file_save (o, (unsigned) cap, dir[0] ? dir : "SD:/", name[0] ? name : "untitled.txt")
			       : wk_file_open (o, (unsigned) cap, dir[0] ? dir : "SD:/");
		dirty (); present (true);
		return ok;
	}
	bool exec (const char *p, const char *a) override { return kapi_exec (p, a) != 0; }
	bool launch (const char *app) override { return kapi_launch (app) != 0; }

	// ---- files ---------------------------------------------------------------------------------------------
	char *load (const char *path, int *len) override
	{
		*len = 0;
		void *f = kapi_open (path);
		if (!f) return 0;
		unsigned n = kapi_fsize (f);
		char *b = new char[n + 1];
		int r = kapi_read (f, b, n);
		kapi_close (f);
		*len = r > 0 ? r : 0; b[*len] = 0;
		return b;
	}
	bool save (const char *path, const char *d, int n) override { return kapi_save_file (path, d, (unsigned) n) >= 0; }
	bool remove (const char *p) override { return kapi_remove (p) == 0; }
	bool rename (const char *a, const char *b) override { return kapi_rename (a, b) == 0; }
	bool makeDir (const char *p) override { return kapi_mkdir (p) == 0; }
	bool exists (const char *p) override
	{
		void *f = kapi_open (p); if (f) { kapi_close (f); return true; }
		void *d = kapi_opendir (p); if (d) { kapi_closedir (d); return true; }
		return false;
	}
	int listDir (const char *dir, int index, char *o, int cap) override
	{
		o[0] = 0;
		void *d = kapi_opendir (dir[0] ? dir : ".");
		if (!d) return 0;
		struct kapi_dirent e; int i = 0;
		while (kapi_readdir (d, &e))
		{
			if (e.name[0] == '.') continue;
			if (i++ == index) { scpy (o, e.name, cap); if (e.is_dir) { int n = slen (o); if (n < cap - 1) { o[n] = '/'; o[n + 1] = 0; } } break; }
		}
		kapi_closedir (d);
		return slen (o);
	}
	const char *command () override { return args; }

	// End of the program: a text-only window program keeps its window until a key.
	void finished (bool error) override
	{
		if (!root || error || windowCmd || !textUsed) return;
		const char *msg = "Press any key to continue";
		int save = fg; fg = 15;
		crow = rows - 1; ccol = 0;
		fillRect (0, crow * root->fh, W, root->fh, rgb (bg, 0));
		for (int i = 0; msg[i]; i++) putch (msg[i]);
		fg = (unsigned) save;
		dirty ();
		long k;
		while (!should_exit ()) { pump (); if (root->popKey (&k)) break; kapi_msleep (10); }
	}
};

static void on_control (Widget &w) { if (g_host) g_host->pushEvent (w.tag); }

int main (void)
{
	static char argbuf[512];
	kapi_get_args (argbuf, sizeof argbuf);
	char path[256], cwd[256] = ""; int i = 0, n = 0;
	bool ide = false;
	// Options: -d <dir> (current directory, default: the program's folder), -i (report a
	// syntax / runtime error to the qbasic editor over IPC: service "qbasic").
	for (;;)
	{
		while (argbuf[i] == ' ') i++;
		if (argbuf[i] == '-' && argbuf[i + 1] == 'i' && (argbuf[i + 2] == ' ' || !argbuf[i + 2])) { ide = true; i += 2; continue; }
		if (argbuf[i] == '-' && argbuf[i + 1] == 'd' && argbuf[i + 2] == ' ')
		{
			i += 3; while (argbuf[i] == ' ') i++;
			int k = 0;
			if (argbuf[i] == '"') { i++; while (argbuf[i] && argbuf[i] != '"' && k < 255) cwd[k++] = argbuf[i++]; if (argbuf[i]) i++; }
			else while (argbuf[i] && argbuf[i] != ' ' && k < 255) cwd[k++] = argbuf[i++];
			cwd[k] = 0;
			continue;
		}
		break;
	}
	if (argbuf[i] == '"') { i++; while (argbuf[i] && argbuf[i] != '"' && n < 255) path[n++] = argbuf[i++]; if (argbuf[i]) i++; }
	else while (argbuf[i] && argbuf[i] != ' ' && n < 255) path[n++] = argbuf[i++];
	path[n] = 0;
	while (argbuf[i] == ' ') i++;

	static OnyxHost host;
	g_host = &host;
	host.console = kapi_stdout () != 0;
	scpy (host.args, argbuf + i, sizeof host.args);
	if (!path[0])
	{
		ax_putln ("usage: basic <program.bas> [arguments]");
		return 1;
	}

	// Window title: the app name (apps/<name>.app/main.bas) or the file name.
	{
		int e = slen (path), s = e; while (s > 0 && path[s - 1] != '/' && path[s - 1] != ':') s--;
		const char *base = path + s;
		if ((base[0] == 'm' || base[0] == 'M') && s >= 5 && path[s - 5] == '.')		// "<name>.app/main.bas"
		{
			int ps = s - 1; while (ps > 0 && path[ps - 1] != '/' && path[ps - 1] != ':') ps--;
			int k = 0; for (int j = ps; j < s - 5 && k < 63; j++) host.title[k++] = path[j];
			host.title[k] = 0;
		}
		else scpy (host.title, base, sizeof host.title);
	}

	int len = 0;
	char *src = host.load (path, &len);
	if (!src)
	{
		if (host.console) { ax_puts ("basic: cannot read "); ax_putln (path); }
		else notify ("BASIC", "Cannot read the program file.");
		return 1;
	}
	// The program's folder becomes the current directory (relative file names).
	if (cwd[0]) kapi_chdir (cwd);
	else
	{
		char dir[256]; scpy (dir, path, sizeof dir);
		int e = slen (dir); while (e > 0 && dir[e - 1] != '/') e--;
		if (e > 0) { dir[e - 1 > 0 && dir[e - 2] != ':' ? e - 1 : e] = 0; kapi_chdir (dir); }
	}

	bas::Error err;
	bas::Program *prog = bas::compile (src, &err);
	delete [] src;
	char msg[200];
	auto report = [&] (const char *kind)
	{
		int k = 0;
		for (int j = 0; kind[j] && k < 190; j++) msg[k++] = kind[j];
		char num[12]; int nl = bas::formatNum (err.line, num);
		for (int j = 0; j < nl && k < 190; j++) msg[k++] = num[j];
		msg[k++] = ':'; msg[k++] = ' ';
		for (int j = 0; err.msg[j] && k < 198; j++) msg[k++] = err.msg[j];
		msg[k] = 0;
		if (ide)						// tell the editor: line \0 message
		{
			int pid = kapi_ipc_lookup ("qbasic");
			if (pid)
			{
				char m[180]; int q = bas::formatNum (err.line, m); m[q++] = 0;
				for (int j = 0; err.msg[j] && q < 178; j++) m[q++] = err.msg[j];
				m[q++] = 0;
				kapi_mailbox_send (pid, 1, m, (unsigned) q);
			}
		}
		if (host.root) { wk_messagebox ("BASIC", msg, MB_OK); }
		else if (host.console) ax_putln (msg);
		else notify (host.title, msg);
	};
	if (!prog) { report ("Syntax error in line "); return 2; }
	int r = bas::run (prog, host, &err);
	if (r) report ("Error in line ");
	bas::destroy (prog);
	return r ? 3 : 0;
}
