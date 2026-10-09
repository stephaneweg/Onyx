//
// uikit/toolbar.cpp -- ToolBar, ToolButton and their icons (uikit/toolbar.h). The icons are drawn in a
// 20 x 20 design box scaled to the size asked, from their geometry (uikit/vpaint.h: anti-aliased).
//
#include "uikit/toolbar.h"
#include "uikit/vpaint.h"
#include "uikit/adapt.h"
#include "uikit/dialog.h"
#include "uikit/root.h"
#include "uikit/internal/adapt_int.h"

namespace uikit {

// ---- the icons -------------------------------------------------------------------------------------------
// Design coordinates in 1/16 of the 20-px box (D (x) = x px of it), scaled to the canvas's 1/16 px.
namespace {
struct Pen
{
	VPath p; int X, Y, S; unsigned ink; Canvas &cv;
	Pen (Canvas &c, int x, int y, int size, unsigned k) : X (V (x)), Y (V (y)), S (size), ink (k), cv (c) {}
	int x (int d) const { return X + d * S / 20; }
	int y (int d) const { return Y + d * S / 20; }
	int w (int d) const { int v = d * S / 20; return v < 18 ? 18 : v; }	// (a stroke: 1.1 px at least)
	void fill () { p.fill (cv, ink); p.clear (); }
	void line (int x0, int y0, int x1, int y1, int wd) { p.line (x (x0), y (y0), x (x1), y (y1), w (wd)); }
	void rect (int x0, int y0, int ww, int hh) { p.rect (x (x0), y (y0), ww * S / 20, hh * S / 20); }
	void rrect (int x0, int y0, int ww, int hh, int r) { p.rrect (x (x0), y (y0), ww * S / 20, hh * S / 20, r * S / 20); }
	void circle (int cx, int cy, int r) { p.circle (x (cx), y (cy), r * S / 20); }
	void arc (int cx, int cy, int r, int a0, int a1, int wd) { p.arc (x (cx), y (cy), r * S / 20, a0, a1, w (wd)); }
	void poly (const int *d, int n) { int q[32]; for (int i = 0; i < n && i < 16; i++) { q[2 * i] = x (d[2 * i]); q[2 * i + 1] = y (d[2 * i + 1]); } p.poly (q, n); }
	void loop (const int *d, int n, int wd, bool closed = true)
	{ int q[32]; for (int i = 0; i < n && i < 16; i++) { q[2 * i] = x (d[2 * i]); q[2 * i + 1] = y (d[2 * i + 1]); } p.polyline (q, n, w (wd), closed); }
	void head (int tx, int ty, int deg, int len, int half) { p.arrowHead (x (tx), y (ty), deg, len * S / 20, half * S / 20); }
};
}

void uk_tool_glyph (Canvas &cv, int kind, int x, int y, int size, unsigned ink)
{
	if (size < 6) size = 6;
	Pen g (cv, x, y, size, ink);
	const int T = V (1) + 10;					// the strokes' width (design): 1.6 px
	switch (kind)
	{
	case WKT_NEW:
	{
		int pg[10] = { V (4), V (2), V (12), V (2), V (16), V (6), V (16), V (18), V (4), V (18) };
		g.loop (pg, 5, T); int f[6] = { V (12), V (2), V (12), V (6), V (16), V (6) }; g.loop (f, 3, T, false);
		g.fill (); break;
	}
	case WKT_OPEN:
	{
		int f[12] = { V (2), V (4), V (8), V (4), V (10), V (6), V (18), V (6), V (18), V (16), V (2), V (16) };
		g.loop (f, 6, T); g.line (V (2), V (9), V (18), V (9), T); g.fill (); break;
	}
	case WKT_SAVE:
	{
		int f[12] = { V (3), V (3), V (14), V (3), V (17), V (6), V (17), V (17), V (3), V (17), V (3), V (3) };
		g.loop (f, 6, T, false); g.rect (V (7), V (3), V (6), V (5)); g.fill ();
		int lab[8] = { V (6), V (11), V (14), V (11), V (14), V (17), V (6), V (17) }; g.loop (lab, 4, T); g.fill (); break;
	}
	case WKT_UNDO:
		g.arc (V (11), V (11), V (6), -40, 165, T); g.head (V (4) + 8, V (13), 250, V (4), V (3)); g.fill (); break;
	case WKT_REDO:
		g.arc (V (9), V (11), V (6), 220, 15, T); g.head (V (16) - 8, V (13), 290, V (4), V (3)); g.fill (); break;
	case WKT_CUT:
		g.arc (V (6), V (15), V (2) + 4, 0, 360, T); g.arc (V (14), V (15), V (2) + 4, 0, 360, T);
		g.line (V (7), V (12), V (14), V (2), T); g.line (V (13), V (12), V (6), V (2), T); g.fill (); break;
	case WKT_COPY:
	{
		int a[8] = { V (3), V (6), V (12), V (6), V (12), V (18), V (3), V (18) }; g.loop (a, 4, T);
		int b[10] = { V (8), V (6), V (8), V (2), V (17), V (2), V (17), V (14), V (12), V (14) }; g.loop (b, 5, T, false);
		g.fill (); break;
	}
	case WKT_PASTE:
	{
		int a[8] = { V (4), V (4), V (16), V (4), V (16), V (18), V (4), V (18) }; g.loop (a, 4, T);
		g.rrect (V (7), V (2), V (6), V (4), V (1)); g.fill (); break;
	}
	case WKT_PLAY:   { int t[6] = { V (6), V (4), V (16), V (10), V (6), V (16) }; g.poly (t, 3); g.fill (); break; }
	case WKT_PAUSE:  g.rect (V (5), V (4), V (3) + 8, V (12)); g.rect (V (11) + 8, V (4), V (3) + 8, V (12)); g.fill (); break;
	case WKT_STOP:   g.rrect (V (5), V (5), V (10), V (10), V (1)); g.fill (); break;
	case WKT_RECORD: g.circle (V (10), V (10), V (5) + 8); g.fill (); break;
	case WKT_TO_START:
	{ g.rect (V (4), V (5), V (2), V (10)); int t[6] = { V (16), V (5), V (16), V (15), V (7), V (10) }; g.poly (t, 3); g.fill (); break; }
	case WKT_TO_END:
	{ int t[6] = { V (4), V (5), V (13), V (10), V (4), V (15) }; g.poly (t, 3); g.rect (V (14), V (5), V (2), V (10)); g.fill (); break; }
	case WKT_REWIND:
	{
		int a[6] = { V (10), V (5), V (10), V (15), V (2), V (10) }, b[6] = { V (18), V (5), V (18), V (15), V (10), V (10) };
		g.poly (a, 3); g.poly (b, 3); g.fill (); break;
	}
	case WKT_FORWARD:
	{
		int a[6] = { V (2), V (5), V (10), V (10), V (2), V (15) }, b[6] = { V (10), V (5), V (18), V (10), V (10), V (15) };
		g.poly (a, 3); g.poly (b, 3); g.fill (); break;
	}
	case WKT_LOOP:
		g.arc (V (7), V (10), V (5), 90, 270, T); g.arc (V (13), V (10), V (5), -90, 90, T);
		g.line (V (7), V (5), V (10), V (5), T); g.line (V (13), V (15), V (10), V (15), T);
		g.head (V (14), V (5), 0, V (4), V (3)); g.head (V (6), V (15), 180, V (4), V (3));
		g.fill (); break;
	case WKT_METRONOME:
	{
		int b[8] = { V (5), V (17), V (15), V (17), V (12), V (3), V (8), V (3) }; g.loop (b, 4, T);
		g.line (V (10), V (14), V (15), V (5), T); g.circle (V (13) + 8, V (7) + 8, V (1) + 8);
		g.line (V (6), V (13), V (14), V (13), T); g.fill (); break;
	}
	case WKT_PLUS:  g.line (V (4), V (10), V (16), V (10), T + 4); g.line (V (10), V (4), V (10), V (16), T + 4); g.fill (); break;
	case WKT_MINUS: g.line (V (4), V (10), V (16), V (10), T + 4); g.fill (); break;
	case WKT_SEARCH:
		g.arc (V (8) + 8, V (8) + 8, V (5), 0, 360, T); g.line (V (12), V (12), V (17), V (17), T + 12); g.fill (); break;
	case WKT_MIXER:
		for (int i = 0; i < 3; i++) g.line (V (5 + 5 * i), V (3), V (5 + 5 * i), V (17), V (1));
		g.fill ();
		g.rrect (V (3), V (11), V (4), V (3), 8); g.rrect (V (8), V (5), V (4), V (3), 8); g.rrect (V (13), V (9), V (4), V (3), 8);
		g.fill (); break;
	case WKT_SPARK:
	{
		int s[16] = { V (9), V (2), V (11), V (8), V (17), V (10), V (11), V (12), V (9), V (18), V (7), V (12), V (1), V (10), V (7), V (8) };
		g.poly (s, 8);
		int t[16] = { V (16), V (1), V (16) + 12, V (3) + 4, V (19), V (4), V (16) + 12, V (4) + 12, V (16), V (7), V (15) + 4, V (4) + 12, V (13), V (4), V (15) + 4, V (3) + 4 };
		g.poly (t, 8); g.fill (); break;
	}
	case WKT_GEAR: uk_glyph (cv, WKG_GEAR, x + size / 2, y + size / 2, size * 16 / 20, ink); break;
	case WKT_TRASH:						// a waste bin: its lid and handle, the can, two ribs
	{
		g.line (V (3), V (5), V (17), V (5), T);
		int hd[8] = { V (8), V (5), V (8), V (2) + 8, V (12), V (2) + 8, V (12), V (5) }; g.loop (hd, 4, T, false);
		int can[8] = { V (5), V (5), V (6), V (18), V (14), V (18), V (15), V (5) }; g.loop (can, 4, T, false);
		g.line (V (8) + 8, V (8), V (8) + 12, V (15), T - 4); g.line (V (11) + 8, V (8), V (11) + 4, V (15), T - 4);
		g.fill (); break;
	}
	case WKT_PIN:						// a push pin, upright: its cap, body, collar, needle
	{
		g.rrect (V (6), V (2), V (8), V (3), V (1));
		int b[8] = { V (7) + 8, V (5), V (12) + 8, V (5), V (13) + 8, V (10), V (6) + 8, V (10) }; g.poly (b, 4);
		g.rrect (V (4), V (9), V (12), V (2) + 8, V (1));
		g.line (V (10), V (11), V (10), V (18), T);
		g.fill (); break;
	}
	}
}

// ---- ToolButton ------------------------------------------------------------------------------------------
ToolButton::ToolButton (int w, int h, const char *tip_, Action cb)
  : Widget (0, 0, w > 0 ? w : h, h), glyph (WKT_NONE), iconFn (0), iconId (0), iconSize (18), toggle (false), on (false),
    filled (false), raised (false), onColor (UK_AUTO), iconColor (UK_AUTO), onClick (cb), arrow (0), m_part (-1), m_down (-1)
{ tip = tip_; text[0] = '\0'; if (h < 24) iconSize = h - 6 < 8 ? 8 : h - 6; }

ToolButton *ToolButton::setGlyph (int kind) { glyph = kind; invalidate (true); return this; }
ToolButton *ToolButton::setIcon (ToolIconFn fn, int id) { iconFn = fn; iconId = id; invalidate (true); return this; }
ToolButton *ToolButton::setText (const char *s)
{ int i = 0; if (s) for (; s[i] && i < 31; i++) text[i] = s[i]; text[i] = '\0'; invalidate (true); return this; }
ToolButton *ToolButton::setToggle (bool t, bool o) { toggle = t; on = o; invalidate (true); return this; }
ToolButton *ToolButton::setSplit (void (*a) (ToolButton &))
{ if (!arrow && a) resizeTo (width + SPLIT_W, height); arrow = a; invalidate (true); return this; }
ToolButton *ToolButton::fitWidth ()
{
	bool icon = glyph != WKT_NONE || iconFn;
	int w = 12 + (icon ? iconSize : 0) + (text[0] ? (icon ? 5 : 0) + uk_tw (text) : 0) + (arrow ? SPLIT_W : 0);
	if (w < height) w = height;
	resizeTo (w, height);
	return this;
}
void ToolButton::setOn (bool v) { if (v != on) { on = v; invalidate (true); } }
void ToolButton::setDisabled (bool d) { if (d != disabled) { disabled = d; invalidate (true); } }

void ToolButton::onDraw ()
{
	unsigned bg = bgColor (), acc = onColor == UK_AUTO ? C_ACCENT : onColor;
	canvas.clear (bg);
	int bw = arrow ? width - SPLIT_W : width, st = m_part >= 0 && !disabled;
	bool dn = m_down >= 0 && !disabled, full = on && filled;
	unsigned under = bg;						// what the content sits on
	if (full)							// on: filled (a play button)
	{
		int lv = dn ? 110 : st ? 150 : 138;
		uk_rbox (canvas, 0, 0, bw, height, 5, uk_tone (acc, lv + 8), uk_tone (acc, lv - 20));
		uk_rline (canvas, 0, 0, bw, height, 5, uk_tone (acc, 80), 180);
		under = acc;
	}
	else if (raised || st)						// a face: always (raised), or pointed
	{
		unsigned f = raised ? uk_tone (bg, uk_bright (bg) < 100 ? 150 : 140) : bg;
		unsigned a = dn ? uk_tone (f, 110) : uk_tone (f, st ? 156 : 142), b = dn ? uk_tone (f, 118) : uk_tone (f, st ? 136 : 124);
		uk_rbox (canvas, 0, 0, width, height, 5, a, b);
		uk_rline (canvas, 0, 0, width, height, 5, uk_tone (bg, uk_bright (bg) < 100 ? 70 : 96), raised ? 200 : 150);
		under = f;
	}
	if (on && !filled)						// on: the accent's tint
	{
		uk_rbox (canvas, 0, 0, bw, height, 5, uk_mix (under, acc, 60), uk_mix (under, acc, 76));
		uk_rline (canvas, 0, 0, bw, height, 5, uk_mix (under, acc, 150));
		under = uk_mix (under, acc, 68);
	}
	if (arrow && (st || raised)) canvas.fillRect (bw, 4, 1, height - 8, uk_tone (under, 104));
	unsigned ink = full ? uk_ink_on (acc) : uk_ink_for (under);
	if (disabled) ink = uk_mix (under, ink, 100);
	unsigned ic = iconColor != UK_AUTO && !full ? (disabled ? uk_mix (under, iconColor, 110) : iconColor) : ink;
	int d = m_down == 0 ? 1 : 0;
	bool icon = glyph != WKT_NONE || iconFn;
	int tw = text[0] ? uk_tw (text) : 0, total = (icon ? iconSize : 0) + (icon && tw ? 5 : 0) + tw;
	int x = (bw - total) / 2 + d, iy = (height - iconSize) / 2 + d;
	if (icon)
	{
		if (iconFn) iconFn (canvas, iconId, x, iy, iconSize, ic, disabled);
		else uk_tool_glyph (canvas, glyph, x, iy, iconSize, ic);
		x += iconSize + (tw ? 5 : 0);
	}
	if (tw) uk_text_l (canvas, x, d, height, text, ink);
	if (arrow) uk_glyph (canvas, WKG_CHEV_DOWN, bw + SPLIT_W / 2, height / 2 + 1, 7, disabled ? uk_mix (bg, uk_ink_for (bg), 110) : uk_ink_for (bg));
}

bool ToolButton::onMouse (int mx, int my, int bl, int, int, int)
{
	int part = mx < 0 || my < 0 || mx >= width || my >= height ? -1 : arrow && mx >= width - SPLIT_W ? 1 : 0;
	if (part != m_part) { m_part = part; invalidate (true); }
	if (disabled) return part >= 0;
	if (bl && m_down < 0 && part >= 0) { m_down = part; invalidate (true); }
	else if (!bl && m_down >= 0)
	{
		int was = m_down; m_down = -1; invalidate (true);
		if (part == was)
		{
			if (was == 1 && arrow) arrow (*this);
			else { if (toggle) on = !on; if (onClick) onClick (*this); }
		}
	}
	return part >= 0;
}

// ---- ToolBar ---------------------------------------------------------------------------------------------
// (P6) What the bar knows of its tools beyond its fields (behind Widget::ext): each tool as it was added (its place
// on the desktop, the gap before it, a separator before it), its priority, rank, group, words; the bars folded into
// it; the "»" button. The desktop never moves a tool: the records only serve pocket and console.
namespace {
enum { TB_MAX = 64, MORE_W = 28 };
struct TbRec { Widget *w; ToolBar *home; int left, top, homeW, gap, prio, rank, group, pad; bool sepBefore, right; const char *label; };
struct TbExt : internal::ExtHead
{
	TbRec rec[TB_MAX]; int n = 0;
	int seps = 0;				// separators seen (the next tool's default group)
	bool sepPending = false;		// a separator before the next tool
	ToolBar *folded[4]; int nfolded = 0;	// the bars folded into this one
	ToolBar *into = 0;			// the bar this one is folded into
	Widget *more = 0;			// the "»" button (made when first needed)
	int sepX[32]; int nsep = 0;		// the separators where the compact row has them
	bool compact = false;			// (the tools are where compact placed them)
	bool merged = false;			// (the folded bars' tools are in this one)
	bool panel = false;			// (the "»" panel open: its tools are its own until it closes)
};

TbExt *tb_ext (ToolBar *t)
{
	TbExt *e = internal::ext_of<TbExt> (t, internal::EXT_TOOLBAR);
	if (e->onClass == 0)
	{
		e->onClass = [] (Widget *w, internal::ExtHead *) { ToolBar *b = (ToolBar *) w; b->layout (); b->invalidate (true); };
		internal::adaptive_add (e);
	}
	return e;
}

TbRec *tb_find (TbExt *e, Widget *w)
{
	for (int i = 0; i < e->n; i++) if (e->rec[i].w == w) return &e->rec[i];
	return 0;
}

// The "»" button: the overflow's panel.
class MoreButton : public Widget
{
public:
	ToolBar *bar;
	MoreButton (ToolBar *b, int h) : Widget (0, 0, MORE_W, h), bar (b) { tip = "More tools"; }
	unsigned bgColor () override { return parent ? parent->bgColor () : C_BG; }
	void onDraw () override
	{
		unsigned b = bgColor ();
		canvas.clear (b);
		if (hover || pressed) uk_rbox (canvas, 0, 0, width, height, 5, uk_tone (b, pressed ? 112 : 150), uk_tone (b, pressed ? 118 : 132));
		unsigned ink = uk_ink_for (b);
		uk_glyph (canvas, WKG_CHEV_RIGHT, width / 2 - 3, height / 2, 8, ink);
		uk_glyph (canvas, WKG_CHEV_RIGHT, width / 2 + 3, height / 2, 8, ink);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (in != hover) { hover = in; invalidate (true); }
		if (bl && in && !pressed) { pressed = true; invalidate (true); }
		else if (!bl && pressed) { pressed = false; invalidate (true); if (in) bar->showOverflow (); }
		return in;
	}
};

// The overflow: the tools that did not fit, hosted as themselves, a group a row; a tool used in it closes it (an icon
// tool: a drop-down, a box stays open until a click outside, Esc).
class OverPanel : public Modal
{
public:
	Widget *w[TB_MAX]; Widget *home[TB_MAX]; int n;
	OverPanel () : Modal (40, 40), n (0) {}
	unsigned bgColor () override { return uk_size_class () == UK_SC_CONSOLE ? 0x00182644 : C_FIELD; }
	void onDraw () override
	{
		canvas.clear (UK_TRANSPARENT_KEY);
		if (uk_size_class () == UK_SC_CONSOLE) { uk_rbox (canvas, 0, 0, width, height, 10, 0x00182644, 0x000C1428); uk_rline (canvas, 0, 0, width, height, 10, 0x0060C8FF, 200); }
		else uk_popup (canvas, 0, 0, width, height, 8, C_FIELD);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (bl && !pressed) { pressed = true; if (mx < 0 || my < 0 || mx >= width || my >= height) close (0); }
		else if (!bl) pressed = false;
		return true;
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }
	int runPanel ()
	{
		Root *r = Root::current ();
		if (r == 0) return 0;
		r->addChild (this);
		done = false; hasFocus = true; invalidate (true);
		unsigned rel = internal::ptr_release (0, 0);
		while (!done && !uk_quit ())
		{
			uk_pump ();
			int rx, ry;
			unsigned now = internal::ptr_release (&rx, &ry);
			if (now != rel)					// (a release: over an icon tool of the panel -- used, closed)
			{
				rel = now;
				int px, py; internal::abs_pos (this, &px, &py);
				for (int i = 0; i < n; i++)
				{
					Widget *c = w[i];
					if (c->parent != this || c->width > 64 || c->height > 48) continue;
					int x = px + c->left, y = py + c->top;
					if (rx >= x && ry >= y && rx < x + c->width && ry < y + c->height) { close (1); break; }
				}
			}
			Root::paintAll ();
			msleep (16);
		}
		r->removeChild (this);
		r->invalidate (true);
		return result;
	}
};
}

ToolBar::ToolBar (int l, int t, int w, int h)
  : Widget (l, t, w, h), bg (UK_AUTO), line (false), m_x (6), m_rx (w - 6), m_nsep (0) {}

static void tb_record (ToolBar *t, Widget *w, int gap, bool right)
{
	TbExt *e = tb_ext (t);
	if (e->n >= TB_MAX) return;
	TbRec &r = e->rec[e->n++];
	r.w = w; r.home = t; r.left = w->left; r.top = w->top; r.homeW = t->width; r.gap = gap; r.prio = UK_TB_ALWAYS; r.rank = 0;
	r.group = e->seps; r.pad = 0; r.sepBefore = e->sepPending && !right; r.right = right; r.label = 0;
	if (!right) e->sepPending = false;
}

void ToolBar::add (Widget *w, int gap)
{
	m_x += gap;
	w->left = m_x; w->top = (height - w->height) / 2;
	addChild (w);
	m_x += w->width;
	tb_record (this, w, gap, false);
	if (uk_size_class () != UK_SC_REGULAR) layout ();
}

void ToolBar::addRight (Widget *w, int gap)
{
	m_rx -= gap + w->width;
	w->left = m_rx; w->top = (height - w->height) / 2;
	w->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
	addChild (w);
	tb_record (this, w, gap, true);
}

void ToolBar::sep ()
{
	if (m_nsep < 24) m_sepX[m_nsep++] = m_x + 5;
	m_x += 11;
	TbExt *e = tb_ext (this);
	e->sepPending = true; e->seps++;
}

void ToolBar::setPriority (Widget *w, int prio, int rank)
{
	TbRec *r = tb_find (tb_ext (this), w);
	if (r) { r->prio = prio; r->rank = rank; }
	if (uk_size_class () != UK_SC_REGULAR) { layout (); invalidate (true); }
}
void ToolBar::setLabel (Widget *w, const char *label) { TbRec *r = tb_find (tb_ext (this), w); if (r) r->label = label; }
void ToolBar::setGroup (Widget *w, int group) { TbRec *r = tb_find (tb_ext (this), w); if (r) r->group = group; }
void ToolBar::setShortcut (Widget *w, int padButton) { TbRec *r = tb_find (tb_ext (this), w); if (r) r->pad = padButton; }

void ToolBar::foldInto (ToolBar *first)
{
	if (first == 0 || first == this) return;
	TbExt *f = tb_ext (first), *e = tb_ext (this);
	if (e->into || f->nfolded >= 4) return;
	e->into = first;
	f->folded[f->nfolded++] = this;
	if (uk_size_class () != UK_SC_REGULAR) { first->layout (); first->invalidate (true); }
}

int ToolBar::rows () const
{
	TbExt *e = tb_ext ((ToolBar *) this);
	if (uk_size_class () == UK_SC_REGULAR) return e->into ? 1 : 1 + e->nfolded;
	return e->into ? 0 : 1;
}

// The tools of this bar and of the bars folded into it, in their order (compact: they are all this bar's children).
static int tb_all (ToolBar *t, TbRec **out)
{
	TbExt *e = tb_ext (t);
	int k = 0;
	for (int i = 0; i < e->n && k < TB_MAX * 2; i++) out[k++] = &e->rec[i];
	for (int f = 0; f < e->nfolded; f++)
	{
		TbExt *g = tb_ext (e->folded[f]);
		for (int i = 0; i < g->n && k < TB_MAX * 2; i++) out[k++] = &g->rec[i];
	}
	return k;
}

void ToolBar::layout ()
{
	TbExt *e = tb_ext (this);
	bool compact = uk_size_class () != UK_SC_REGULAR;
	if (e->panel) return;					// (the "»" panel open: placed again when it closes)
	if (e->into)						// (folded into another bar: that one places its tools)
	{
		bool hide = compact;
		if (hidden != hide) { hidden = hide; if (parent) parent->invalidate (true); }
		if (!compact) Widget::layout ();
		return;
	}
	if (!compact && !e->compact) { Widget::layout (); return; }	// the desktop: as built (the anchors)
	TbRec *all[TB_MAX * 2];
	int n = tb_all (this, all);
	if (!compact)						// back from compact: every tool home, where it was built
	{
		for (int i = 0; i < n; i++)
		{
			TbRec *r = all[i];
			if (r->w->parent != r->home) { if (r->w->parent) r->w->parent->removeChild (r->w); r->home->addChild (r->w); }
			r->w->hidden = false;
			r->w->left = r->right ? r->home->width - (r->homeW - r->left) : r->left; r->w->top = r->top;
		}
		if (e->more) e->more->hidden = true;
		e->compact = false; e->merged = false;
		lytW = width; lytH = height;			// (placed for this width: no anchor to apply)
		for (int f = 0; f < e->nfolded; f++) { e->folded[f]->lytW = e->folded[f]->width; e->folded[f]->lytH = e->folded[f]->height; e->folded[f]->invalidate (true); }
		invalidate (true);
		return;
	}
	e->compact = true;
	for (int i = 0; i < n; i++)				// the folded bars' tools: this bar's children now
		if (all[i]->w->parent != this) { if (all[i]->w->parent) all[i]->w->parent->removeChild (all[i]->w); addChild (all[i]->w); }
	e->merged = true;
	int rightW = 0;
	for (int i = 0; i < n; i++) if (all[i]->right) rightW += all[i]->gap + all[i]->w->width;
	int avail = width - 6 - rightW - 4;
	bool vis[TB_MAX * 2];
	// the width of a set: its tools, their gaps, the separators between the shown ones
	auto widthOf = [&] () { int x = 0; bool any = false; for (int i = 0; i < n; i++) { if (!vis[i] || all[i]->right) continue; if (any && all[i]->sepBefore) x += 11; x += all[i]->gap + all[i]->w->width; any = true; } return x; };
	bool anyOver = false;
	for (int i = 0; i < n; i++) { vis[i] = all[i]->right || all[i]->prio != UK_TB_OVERFLOW; if (!vis[i]) anyOver = true; }
	if (anyOver || widthOf () > avail)			// not everything: keep room for "»", drop the IF_ROOMs by rank
	{
		int room = avail - MORE_W - 2;
		for (int i = 0; i < n; i++) if (!all[i]->right) vis[i] = all[i]->prio == UK_TB_ALWAYS;
		for (int rank = -100; ; )				// the IF_ROOMs by their rank (lower stays longer), in order
		{
			int best = 1 << 30;
			for (int i = 0; i < n; i++) if (!vis[i] && all[i]->prio == UK_TB_IF_ROOM && all[i]->rank > rank && all[i]->rank < best) best = all[i]->rank;
			if (best == (1 << 30)) break;
			for (int i = 0; i < n; i++)
				if (!vis[i] && all[i]->prio == UK_TB_IF_ROOM && all[i]->rank == best)
				{
					vis[i] = true;
					if (widthOf () > room) vis[i] = false;
				}
			rank = best;
		}
		while (widthOf () > room)				// (still too wide: the ALWAYS ones from the end)
		{
			int last = -1;
			for (int i = 0; i < n; i++) if (vis[i] && !all[i]->right) last = i;
			if (last <= 0) break;
			vis[last] = false;
		}
		anyOver = true;
	}
	int x = 6; bool any = false;
	e->nsep = 0;
	for (int i = 0; i < n; i++)
	{
		TbRec *r = all[i];
		if (r->right) { r->w->hidden = false; continue; }
		r->w->hidden = !vis[i];
		if (!vis[i]) continue;
		if (any && r->sepBefore) { if (e->nsep < 32) e->sepX[e->nsep++] = x + 5; x += 11; }
		x += r->gap;
		r->w->left = x; r->w->top = (height - r->w->height) / 2;
		x += r->w->width;
		any = true;
	}
	int rx = width - 6;
	for (int i = n - 1; i >= 0; i--)
		if (all[i]->right) { rx -= all[i]->gap + all[i]->w->width; all[i]->w->left = rx; all[i]->w->top = (height - all[i]->w->height) / 2; }
	if (anyOver)
	{
		if (e->more == 0) { e->more = new MoreButton (this, height - 6 > 34 ? 34 : height - 6); addChild (e->more); }
		e->more->hidden = false;
		e->more->left = rx - MORE_W - 2; e->more->top = (height - e->more->height) / 2;
	}
	else if (e->more) e->more->hidden = true;
	lytW = width; lytH = height;
	invalidate (true);
}

void ToolBar::showOverflow ()
{
	TbExt *e = tb_ext (this);
	Root *r = Root::current ();
	if (r == 0) return;
	TbRec *all[TB_MAX * 2];
	int n = tb_all (this, all);
	OverPanel p;
	int sc = uk_size_class ();
	int maxW = sc == UK_SC_NARROW ? r->width - 16 : r->width - 16 < 360 ? r->width - 16 : 360;
	int x = 10, y = 10, rowH = 0, w = 0, group = -99999;
	for (int i = 0; i < n && p.n < TB_MAX; i++)
	{
		TbRec *t = all[i];
		if (t->right || !t->w->hidden || t->w == e->more) continue;
		Widget *c = t->w;
		if ((group != -99999 && t->group != group) || x + c->width > maxW - 10)	// (a group a row; a full row wraps)
		{
			if (x > 10) { y += rowH + 8; x = 10; rowH = 0; }
		}
		group = t->group;
		removeChild (c);
		c->hidden = false;
		c->left = x; c->top = y;
		p.addChild (c);
		p.w[p.n] = c; p.home[p.n] = this; p.n++;
		x += c->width + 4;
		if (x > w) w = x;
		if (c->height > rowH) rowH = c->height;
	}
	if (p.n == 0) return;
	int ph = y + rowH + 10, pw = w + 6 < 120 ? 120 : w + 6;
	p.resizeTo (pw, ph);
	int ax, ay; internal::abs_pos (this, &ax, &ay);
	int mx = e->more ? ax + e->more->left + e->more->width : ax + width;
	p.left = mx - pw; p.top = ay + height;
	if (sc == UK_SC_NARROW) { p.left = (r->width - pw) / 2; p.top = r->height - ph - 8; }
	else if (sc == UK_SC_CONSOLE) { p.left = (r->width - pw) / 2; p.top = (r->height - ph) / 2; }
	if (p.left < 4) p.left = 4;
	if (p.top + ph > r->height) p.top = r->height - ph;
	if (p.top < 0) p.top = 0;
	e->panel = true;
	p.runPanel ();
	e->panel = false;
	for (int i = 0; i < p.n; i++)				// the tools back on the bar (hidden: placed by layout)
	{
		Widget *c = p.w[i];
		if (c->parent) c->parent->removeChild (c);
		addChild (c);
		c->hidden = true;
	}
	layout ();
	invalidate (true);
}

void ToolBar::onDraw ()
{
	unsigned b = bgColor ();
	canvas.clear (b);
	internal::ExtHead *h = internal::ext_head (this);
	TbExt *e = h && h->kind == internal::EXT_TOOLBAR ? (TbExt *) h : 0;
	if (e && e->compact && uk_size_class () != UK_SC_REGULAR)
		for (int i = 0; i < e->nsep; i++) uk_etch_v (canvas, e->sepX[i], 7, height - 14, b);
	else
		for (int i = 0; i < m_nsep; i++) uk_etch_v (canvas, m_sepX[i], 7, height - 14, b);
	if (line) uk_etch_h (canvas, 0, height - 2, width, b);
}

} // namespace uikit
