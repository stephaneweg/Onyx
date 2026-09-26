//
// graphcalc/main.cpp -- a graphing calculator. Type up to four functions of x on the left
// (y1..y4, e.g. "sin(x)", "x^2-2", "2x+1", "sqrt(abs(x))"); they are drawn live in their
// colour as you type (a red mark = a syntax error). The plot: drag to move, the wheel or
// + / - to zoom around the pointer, the arrows to pan; the pointer TRACES the curves (the
// panel shows x and each y). View menu: Standard (-10..10), Trig (-2 pi..2 pi), Zoom In /
// Out, Square (same scale on both axes), Grid. The functions are kept in
// SD:/apps/graphcalc.app/functions.txt. Double-precision FP (the BASIC core's math).
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "Apps/graphcalc/expr.h"

using namespace wtk;

#define W	760
#define H	500
#define PANEL	236
#define NF	4
#define SAVE	"SD:/apps/graphcalc.app/functions.txt"

static const unsigned FCOL[NF] = { 0x00E03030, 0x002070E0, 0x0020A040, 0x00C060D0 };

struct Func { Checkbox *on; Textbox *tb; char src[64]; gc::Program prog; bool err; };
static Func g_f[NF];
static Label *g_readout[NF + 1];
static double g_xmin = -10, g_xmax = 10, g_ymin = -7, g_ymax = 7;
static bool   g_grid = true;
static int    g_mx = -1, g_my = -1;	// pointer in the plot (-1 = away)

static void set_view (double x0, double x1, double y0, double y1) { g_xmin = x0; g_xmax = x1; g_ymin = y0; g_ymax = y1; }

// A "nice" grid step (1, 2 or 5 x 10^k) giving about n lines across span.
static double nice_step (double span, int n)
{
	double raw = span / n, p = 1;
	while (p * 10 <= raw) p *= 10;
	while (p > raw) p /= 10;
	double m = raw / p;
	return (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10) * p;
}
static int decimals_for (double step)
{
	int d = 0; double s = step;
	while (s < 0.999 && d < 8) { s *= 10; d++; }
	return d;
}

class Plot : public Widget
{
public:
	bool drag; int dx0, dy0; double vx0, vx1, vy0, vy1;
	Plot (int l, int t, int w, int h) : Widget (l, t, w, h), drag (false) { canFocus = true; }

	int  px (double x) const { return (int) ((x - g_xmin) / (g_xmax - g_xmin) * width); }
	int  py (double y) const { return (int) ((g_ymax - y) / (g_ymax - g_ymin) * height); }
	double wx (int p) const { return g_xmin + (p + 0.5) * (g_xmax - g_xmin) / width; }
	double wy (int p) const { return g_ymax - (p + 0.5) * (g_ymax - g_ymin) / height; }

	void dot (int x, int y, unsigned c) { canvas.fillRect (x - 1, y - 1, 2, 2, c); }
	void line (int x0, int y0, int x1, int y1, unsigned c)
	{
		int ddx = x1 > x0 ? x1 - x0 : x0 - x1, ddy = y1 > y0 ? y1 - y0 : y0 - y1;
		int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = ddx - ddy;
		for (int guard = 0; guard < 4000; guard++)
		{
			dot (x0, y0, c);
			if (x0 == x1 && y0 == y1) break;
			int e2 = 2 * err;
			if (e2 > -ddy) { err -= ddy; x0 += sx; }
			if (e2 < ddx) { err += ddx; y0 += sy; }
		}
	}

	void onDraw () override
	{
		Canvas &c = canvas;
		c.clear (0x00FFFFFF);
		double sx = nice_step (g_xmax - g_xmin, 10), sy = nice_step (g_ymax - g_ymin, 8);
		int ax = px (0), ay = py (0);
		// grid
		if (g_grid)
		{
			for (double x = bas::nfloor (g_xmin / sx) * sx; x <= g_xmax; x += sx) c.fillRect (px (x), 0, 1, height, 0x00E4E8EE);
			for (double y = bas::nfloor (g_ymin / sy) * sy; y <= g_ymax; y += sy) c.fillRect (0, py (y), width, 1, 0x00E4E8EE);
		}
		// axes (pinned to an edge when out of view, for the labels)
		int lax = ax < 0 ? 0 : ax > width - 1 ? width - 1 : ax, lay = ay < 0 ? 0 : ay > height - 1 ? height - 1 : ay;
		if (ax >= 0 && ax < width) c.fillRect (ax, 0, 1, height, 0x00404850);
		if (ay >= 0 && ay < height) c.fillRect (0, ay, width, 1, 0x00404850);
		char t[32];
		int dxs = decimals_for (sx), dys = decimals_for (sy);
		for (double x = bas::nfloor (g_xmin / sx) * sx; x <= g_xmax; x += sx)
		{
			int p = px (x);
			if (p < 2 || p > width - 20 || (x < sx / 2 && x > -sx / 2)) continue;
			c.fillRect (p, lay - 3, 1, 7, 0x00404850);
			gc::fmt (x, dxs, t);
			int ty = lay + 5; if (ty > height - 18) ty = lay - 20;
			c.text (p - wk_len (t) * wk_fw () / 2, ty, t, 0x00606870);
		}
		for (double y = bas::nfloor (g_ymin / sy) * sy; y <= g_ymax; y += sy)
		{
			int p = py (y);
			if (p < 8 || p > height - 10 || (y < sy / 2 && y > -sy / 2)) continue;
			c.fillRect (lax - 3, p, 7, 1, 0x00404850);
			gc::fmt (y, dys, t);
			int tx = lax + 6; if (tx > width - wk_len (t) * wk_fw () - 2) tx = lax - 6 - wk_len (t) * wk_fw ();
			c.text (tx, p - wk_fh () / 2, t, 0x00606870);
		}
		// curves
		for (int f = 0; f < NF; f++)
		{
			Func &fn = g_f[f];
			if (!fn.on->checked || !fn.prog.ok) continue;
			bool have = false; int lx = 0, ly = 0;
			for (int x = 0; x < width; x++)
			{
				double y = fn.prog.eval (wx (x));
				if (gc::isnan_ (y) || y > 1e12 || y < -1e12) { have = false; continue; }
				double fy = (g_ymax - y) / (g_ymax - g_ymin) * height;
				if (fy < -4 * height) fy = -4 * height;
				if (fy > 5 * height) fy = 5 * height;
				int iy = (int) fy;
				if (have && (iy - ly > height || ly - iy > height)) have = false;	// an asymptote: don't join
				if (have) line (lx, ly, x, iy, FCOL[f]);
				else if (iy >= 0 && iy < height) dot (x, iy, FCOL[f]);
				lx = x; ly = iy; have = true;
			}
		}
		// trace
		if (g_mx >= 0 && !drag)
		{
			c.fillRect (g_mx, 0, 1, height, 0x00B0B8C4);
			for (int f = 0; f < NF; f++)
			{
				Func &fn = g_f[f];
				if (!fn.on->checked || !fn.prog.ok) continue;
				double y = fn.prog.eval (wx (g_mx));
				if (gc::isnan_ (y)) continue;
				int iy = py (y);
				if (iy >= 0 && iy < height) { c.fillRect (g_mx - 3, iy - 3, 7, 7, FCOL[f]); c.fillRect (g_mx - 1, iy - 1, 3, 3, 0x00FFFFFF); }
			}
		}
		c.frameRect (0, 0, width, height, 0x00808890);
	}
	void zoom (double k, int cx, int cy)
	{
		double x = wx (cx), y = wy (cy);
		set_view (x - (x - g_xmin) * k, x + (g_xmax - x) * k, y - (y - g_ymin) * k, y + (g_ymax - y) * k);
		invalidate (true);
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		bool in = mx >= 0 && mx < width && my >= 0 && my < height;
		if (!bl) drag = false;
		if (!in && !drag) { if (g_mx >= 0) { g_mx = -1; invalidate (true); } return false; }
		if (wheel) { zoom (wheel > 0 ? 0.8 : 1.25, mx, my); return true; }
		if (bl && !drag) { drag = true; setFocus (); dx0 = mx; dy0 = my; vx0 = g_xmin; vx1 = g_xmax; vy0 = g_ymin; vy1 = g_ymax; }
		else if (bl && drag)
		{
			double ddx = (mx - dx0) * (vx1 - vx0) / width, ddy = (my - dy0) * (vy1 - vy0) / height;
			set_view (vx0 - ddx, vx1 - ddx, vy0 + ddy, vy1 + ddy);
		}
		else if (!bl) drag = false;
		g_mx = mx; g_my = my;
		invalidate (true);
		return true;
	}
	bool onKey (long k) override
	{
		double sx = (g_xmax - g_xmin) / 10, sy = (g_ymax - g_ymin) / 10;
		switch (k)
		{
		case KEY_LEFT:  set_view (g_xmin - sx, g_xmax - sx, g_ymin, g_ymax); break;
		case KEY_RIGHT: set_view (g_xmin + sx, g_xmax + sx, g_ymin, g_ymax); break;
		case KEY_UP:    set_view (g_xmin, g_xmax, g_ymin + sy, g_ymax + sy); break;
		case KEY_DOWN:  set_view (g_xmin, g_xmax, g_ymin - sy, g_ymax - sy); break;
		case '+': case '=': zoom (0.8, width / 2, height / 2); return true;
		case '-': zoom (1.25, width / 2, height / 2); return true;
		default: return false;
		}
		invalidate (true);
		return true;
	}
};

static Plot *g_plot;

static void compile (int i)
{
	Func &f = g_f[i];
	int k = 0; for (; f.tb->text[k] && k < 63; k++) f.src[k] = f.tb->text[k];
	f.src[k] = 0;
	gc::Parser p;
	bool empty = true; for (int j = 0; f.src[j]; j++) if (f.src[j] != ' ') empty = false;
	f.err = !p.compile (f.src, f.prog) && !empty;
	if (empty) f.prog.ok = false;
}

static void update_readout ()
{
	char t[96], v[32];
	if (g_mx < 0) { g_readout[0]->setText ("Move the pointer over the graph"); for (int i = 0; i < NF; i++) g_readout[i + 1]->setText (""); return; }
	double x = g_plot->wx (g_mx);
	t[0] = 0; int n = 0;
	auto cat = [&] (const char *s) { while (*s && n < 94) t[n++] = *s++; t[n] = 0; };
	cat ("x = "); gc::fmt (x, -1, v); cat (v); cat ("   y = "); gc::fmt (g_plot->wy (g_my), -1, v); cat (v);
	g_readout[0]->setText (t);
	for (int i = 0; i < NF; i++)
	{
		Func &f = g_f[i];
		n = 0; t[0] = 0;
		if (f.prog.ok && f.on->checked) { cat ("y"); char d[2] = { (char) ('1' + i), 0 }; cat (d); cat (" = "); gc::fmt (f.prog.eval (x), -1, v); cat (v); }
		else if (f.err) { cat ("y"); char d[2] = { (char) ('1' + i), 0 }; cat (d); cat (": syntax error"); }
		g_readout[i + 1]->setText (t);
	}
}

// The coloured marker left of each function (red border = syntax error).
class Swatch : public Widget
{
public:
	int idx;
	Swatch (int l, int t, int i) : Widget (l, t, 14, 26), idx (i) {}
	void onDraw () override
	{
		canvas.clear (C_BG);
		canvas.fillRect (2, 6, 10, 14, FCOL[idx]);
		if (g_f[idx].err) canvas.frameRect (0, 4, 14, 18, 0x00FF4040);
	}
};
static Swatch *g_sw[NF];

class GcRoot : public Root
{
public:
	GcRoot () : Root (W, H, "Graphing Calculator") {}
	void onTick () override
	{
		bool changed = false;
		for (int i = 0; i < NF; i++)
		{
			Func &f = g_f[i];
			int k = 0; while (f.tb->text[k] && f.tb->text[k] == f.src[k]) k++;
			if (f.tb->text[k] != f.src[k]) { compile (i); g_sw[i]->invalidate (true); changed = true; }
		}
		static int lmx = -2, lmy = -2;
		if (changed || g_mx != lmx || g_my != lmy) { lmx = g_mx; lmy = g_my; update_readout (); }
		if (changed) g_plot->invalidate (true);
	}
};

static void on_toggle (Widget &) { g_plot->invalidate (true); update_readout (); }
static void view_standard () { set_view (-10, 10, -7, 7); g_plot->invalidate (true); }
static void view_trig () { set_view (-2 * gc::PI, 2 * gc::PI, -2.5, 2.5); g_plot->invalidate (true); }
static void view_in () { g_plot->zoom (0.5, g_plot->width / 2, g_plot->height / 2); }
static void view_out () { g_plot->zoom (2, g_plot->width / 2, g_plot->height / 2); }
static void view_square ()
{
	double ppx = (g_xmax - g_xmin) / g_plot->width, cy = (g_ymin + g_ymax) / 2, hh = ppx * g_plot->height / 2;
	set_view (g_xmin, g_xmax, cy - hh, cy + hh); g_plot->invalidate (true);
}
static void view_grid () { g_grid = !g_grid; g_plot->invalidate (true); }
static void on_clear ()
{
	for (int i = 0; i < NF; i++) { g_f[i].tb->setText (""); }
	g_f[0].tb->setFocus ();
}
static void btn_std (Widget &) { view_standard (); }
static void btn_trig (Widget &) { view_trig (); }
static void btn_sq (Widget &) { view_square (); }

static void load_funcs (const char **def)
{
	static char buf[512];
	void *f = kapi_open (SAVE);
	int n = 0;
	if (f) { n = kapi_read (f, buf, sizeof buf - 1); kapi_close (f); }
	if (n <= 0) { for (int i = 0; i < NF; i++) g_f[i].tb->setText (def[i]); return; }
	buf[n] = 0;
	char *p = buf;
	for (int i = 0; i < NF; i++)
	{
		char *l = p; while (*p && *p != '\n') p++;
		if (*p) *p++ = 0;
		int e = 0; while (l[e]) e++;
		if (e && l[e - 1] == '\r') l[e - 1] = 0;
		g_f[i].tb->setText (l);
	}
}
static void save_funcs ()
{
	char buf[512]; int n = 0;
	for (int i = 0; i < NF; i++) { for (int k = 0; g_f[i].tb->text[k] && n < 500; k++) buf[n++] = g_f[i].tb->text[k]; buf[n++] = '\n'; }
	kapi_save_file (SAVE, buf, (unsigned) n);
}

int main (void)
{
	GcRoot root;
	if (root.canvas.px == 0) return 1;
	root.setBg (C_BG);
	int fh = wk_fh ();
	root.addChild (new Label (10, 8, PANEL - 20, fh + 2, "Functions of x", C_ACCENT, C_BG));
	for (int i = 0; i < NF; i++)
	{
		int y = 34 + i * 36;
		g_sw[i] = new Swatch (8, y, i); root.addChild (g_sw[i]);
		static const char *const lab[NF] = { "y1=", "y2=", "y3=", "y4=" };
		g_f[i].on = new Checkbox (24, y + 2, 52, 22, lab[i], true, on_toggle); root.addChild (g_f[i].on);
		g_f[i].tb = new Textbox (78, y, PANEL - 86, 26, ""); root.addChild (g_f[i].tb);
		g_f[i].src[0] = 1;			// force the first compile
	}
	int by = 34 + NF * 36 + 6;
	root.addChild (new Button (8, by, 70, 26, "Standard", btn_std));
	root.addChild (new Button (82, by, 70, 26, "Trig", btn_trig));
	root.addChild (new Button (156, by, 70, 26, "Square", btn_sq));
	for (int i = 0; i <= NF; i++)
	{
		g_readout[i] = new Label (10, by + 42 + i * (fh + 6), PANEL - 16, fh + 2, "", i ? FCOL[i - 1] | 0x00303030 : C_TEXT, C_BG);
		root.addChild (g_readout[i]);
	}
	root.addChild (new Label (10, H - 3 * (fh + 4) - 6, PANEL - 16, fh + 2, "Drag: move   Wheel: zoom", C_DIS, C_BG));
	root.addChild (new Label (10, H - 2 * (fh + 4) - 6, PANEL - 16, fh + 2, "e.g. sin(x)  x^2-2  2x+1", C_DIS, C_BG));
	root.addChild (new Label (10, H - (fh + 4) - 6, PANEL - 16, fh + 2, "sqrt ln log exp abs pi e", C_DIS, C_BG));
	g_plot = new Plot (PANEL, 4, W - PANEL - 4, H - 8);
	root.addChild (g_plot);

	static const char *def[NF] = { "sin(x)", "x^2/4-3", "", "" };
	load_funcs (def);
	static Menu menu;
	menu.menu ("Edit");
	menu.item ("Clear Functions", "", 0, on_clear);
	menu.menu ("View");
	menu.item ("Standard",  "", 0, view_standard);
	menu.item ("Trig",      "", 0, view_trig);
	menu.item ("Zoom In",   "+", 0, view_in);
	menu.item ("Zoom Out",  "-", 0, view_out);
	menu.item ("Square",    "", 0, view_square);
	menu.separator ();
	menu.item ("Grid On / Off", "", 0, view_grid);
	menu.publish ();
	g_f[0].tb->setFocus ();
	update_readout ();
	root.run ();
	save_funcs ();
	return 0;
}
