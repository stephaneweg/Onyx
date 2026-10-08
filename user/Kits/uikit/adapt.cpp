//
// uikit/adapt.cpp -- the adaptive layer (uikit/adapt.h, uikit/internal/adapt_int.h): the size class and the metrics
// asked of the port (the desktop's says nothing: regular, today's sizes), the widgets' extensions, the focus ring and
// the arrows (pocket, console), the console's L1 / R1, the focused control told to the server (its viewport follows).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include "uikit/adapt.h"
#include "uikit/internal/adapt_int.h"
#include "uikit/root.h"
#include "uikit/textbox.h"
#include "uikit/port/port.h"
#include "appkit/appkit.h"

namespace uikit {

// ---- the size class, the profile ------------------------------------------------------------------------------
static bool s_loaded, s_dirty;
static int s_mode = UK_MODE_DESKTOP, s_cls = UK_SC_REGULAR, s_profile = UK_PROFILE_REGULAR, s_scale = 100;
static int s_fMode = -1, s_fCls = -1, s_fProfile = -1;	// (forced by a test)
static int s_told = UK_SC_REGULAR;			// the class the Roots were last told
static bool s_logical;

static void load ()
{
	if (s_loaded && !s_dirty) return;
	s_loaded = true; s_dirty = false;
	int mode = UK_MODE_DESKTOP, cls = UK_SC_REGULAR, prof = UK_PROFILE_REGULAR, scale = 100;
	port::adapt_info (&mode, &cls, &prof, &scale);
	if (s_fMode >= 0) mode = s_fMode;
	if (s_fCls >= 0) cls = s_fCls;
	if (s_fProfile >= 0) prof = s_fProfile;
	if (mode == UK_MODE_DESKTOP) { cls = UK_SC_REGULAR; prof = UK_PROFILE_REGULAR; scale = 100; }	// (the desktop: today's)
	else if (mode == UK_MODE_CONSOLE) { cls = UK_SC_CONSOLE; prof = UK_PROFILE_CONSOLE; }
	else if (cls == UK_SC_REGULAR || cls == UK_SC_CONSOLE) cls = UK_SC_COMPACT;
	if (scale < 50 || scale > 400) scale = 100;
	s_mode = mode; s_cls = cls; s_profile = prof; s_scale = scale;
}

int uk_size_class () { load (); return s_cls; }
int uk_mode () { load (); return s_mode; }

static int sc (int v) { return v * s_scale / 100; }
static int mx (int a, int b) { return a > b ? a : b; }

const UkMetrics &uk_metrics ()
{
	static UkMetrics m;
	load ();
	int fh = uk_fh ();
	for (int i = 0; i < 8; i++) m.reserved[i] = 0;
	m.profile = s_profile; m.scale = s_scale;
	switch (s_profile)
	{
	default:						// regular: today's sizes (the desktop)
		m.row = fh + 8; m.menuRow = fh + 10; m.button = 28; m.field = 26; m.tab = 30; m.toolbar = 34;
		m.rail = 48; m.scrollbar = UK_SBW; m.gutter = s_cls == UK_SC_REGULAR ? UK_SBW : 0; m.pad = 8; m.touch = 0;
		break;
	case UK_PROFILE_COMPACT:				// a small screen with a keyboard and a pointer: dense, overlay bars
		m.row = fh + 8; m.menuRow = mx (fh + 10, 26); m.button = 28; m.field = 26; m.tab = 30; m.toolbar = 34;
		m.rail = sc (48); m.scrollbar = 4; m.gutter = 0; m.pad = sc (8); m.touch = 0;
		break;
	case UK_PROFILE_TOUCH:					// a finger: 36 px targets (7 mm at the 7" display's 133 dpi)
		m.row = mx (fh + 16, sc (36)); m.menuRow = mx (fh + 16, sc (36)); m.button = sc (38); m.field = sc (34);
		m.tab = sc (40); m.toolbar = sc (44); m.rail = sc (56); m.scrollbar = 4; m.gutter = 0; m.pad = sc (10); m.touch = sc (36);
		break;
	case UK_PROFILE_CONSOLE:				// a television, a pad: big rows, the focus glowing
		m.row = mx (fh + 20, sc (40)); m.menuRow = mx (fh + 18, sc (40)); m.button = sc (40); m.field = sc (36);
		m.tab = sc (44); m.toolbar = 0; m.rail = sc (260); m.scrollbar = 3; m.gutter = 0; m.pad = sc (12); m.touch = 0;
		break;
	}
	return m;
}

int uk_scroll_gutter () { load (); return s_cls == UK_SC_REGULAR ? UK_SBW : 0; }
int uk_lp (int v) { load (); return sc (v); }

void uk_logical_units (bool on) { s_logical = on; port::logical_units (on ? 1 : 0); }
bool uk_logical_units_on () { return s_logical; }

// ---- a field's type -------------------------------------------------------------------------------------------
void uk_set_input_type (Widget *w, int type)
{
	if (w == 0 || type < UK_IN_TEXT || type > UK_IN_SEARCH) return;
	internal::ExtHead *h = internal::ext_head (w);
	if (h == 0) { h = new internal::ExtHead (); internal::ext_set (w, h, internal::EXT_PLAIN); }
	h->inType = (short) (type + 1);
}

int uk_input_type (Widget *w)
{
	internal::ExtHead *h = internal::ext_head (w);
	if (h != 0 && h->inType > 0) return h->inType - 1;
	return w != 0 && w->isField () && ((Textbox *) w)->password ? UK_IN_PASSWORD : UK_IN_TEXT;	// (only Textbox is a field)
}

namespace internal {

// ---- the extensions -------------------------------------------------------------------------------------------
ExtHead *ext_head (Widget *w)
{
	if (w == 0 || w->ext == 0) return 0;
	ExtHead *h = (ExtHead *) w->ext;
	return h->magic == EXT_MAGIC ? h : 0;
}

void ext_set (Widget *w, ExtHead *h, unsigned short kind)
{
	ExtHead *old = ext_head (w);
	h->kind = kind; h->owner = w;
	if (old != 0)
	{
		if (h->inType == 0) h->inType = old->inType;
		adaptive_remove (old);
		if (old->destroy) old->destroy (old); else delete old;
	}
	w->ext = h;
}

void ext_free (Widget *w)
{
	ExtHead *h = ext_head (w);
	if (h == 0) return;
	adaptive_remove (h);
	w->ext = 0;
	if (h->destroy) h->destroy (h); else delete h;
}

static ExtHead *s_adaptive;

void adaptive_add (ExtHead *h)
{
	for (ExtHead *p = s_adaptive; p; p = p->nextAdaptive) if (p == h) return;
	h->nextAdaptive = s_adaptive; s_adaptive = h;
}

void adaptive_remove (ExtHead *h)
{
	for (ExtHead **pp = &s_adaptive; *pp; pp = &(*pp)->nextAdaptive)
		if (*pp == h) { *pp = h->nextAdaptive; h->nextAdaptive = 0; return; }
}

// ---- the size class -------------------------------------------------------------------------------------------
void class_dirty () { s_dirty = true; }

void force_class (int mode, int size_class, int profile)
{
	s_fMode = mode; s_fCls = size_class; s_fProfile = profile;
	s_dirty = true;
	class_check ();
}

bool ring_on () { load (); return s_cls != UK_SC_REGULAR; }

static void invalidate_all (Widget *w)
{
	w->invalidate (true);
	for (Widget *c = w->firstChild; c; c = c->nextSib) invalidate_all (c);
}

static void tell_root (Root *r) { r->onSizeClass (s_cls); invalidate_all (r); }

void class_check ()
{
	if (!s_dirty) return;
	load ();
	if (s_cls == s_told) return;
	s_told = s_cls;
	for (ExtHead *h = s_adaptive; h; )			// (an onClass may change the list: the next one taken first)
	{
		ExtHead *n = h->nextAdaptive;
		if (h->onClass) h->onClass (h->owner, h);
		h = n;
	}
	each_root (tell_root);
}

// ---- where a widget is ------------------------------------------------------------------------------------------
Root *root_of (Widget *w)
{
	while (w && w->parent) w = w->parent;
	return (Root *) w;
}

void abs_pos (Widget *w, int *x, int *y)
{
	int ax = 0, ay = 0;
	for (Widget *p = w; p && p->parent; p = p->parent) { ax += p->left - p->parent->scrollX; ay += p->top - p->parent->scrollY; }
	*x = ax; *y = ay;
}

void dim_copy (Canvas &cv, Widget *from, int x, int y, int w, int h, int amount)
{
	unsigned *src = from ? from->canvas.px : 0;
	int sw = from ? from->canvas.w : 0, sh = from ? from->height : 0, ss = from ? from->canvas.stride : 0;
	if (ss <= 0) ss = sw;
	for (int j = 0; j < h && j < cv.h; j++)
		for (int i = 0; i < w && i < cv.w; i++)
		{
			int X = x + i, Y = y + j;
			unsigned c = src && X >= 0 && Y >= 0 && X < sw && Y < sh ? src[(long) Y * ss + X] & 0x00FFFFFF : 0x00808080;
			cv.px[(long) j * (cv.stride > 0 ? cv.stride : cv.w) + i] = uk_mix (c, 0x00101420, amount);
		}
}

// ---- the console's L1 / R1, F6 -------------------------------------------------------------------------------------
struct NavOwner { Widget *w; int prio; bool (*step) (Widget *, int); };
static NavOwner s_nav[16];

void nav_register (Widget *w, int prio, bool (*step) (Widget *, int))
{
	for (int i = 0; i < 16; i++) if (s_nav[i].w == w) { s_nav[i].prio = prio; s_nav[i].step = step; return; }
	for (int i = 0; i < 16; i++) if (s_nav[i].w == 0) { s_nav[i].w = w; s_nav[i].prio = prio; s_nav[i].step = step; return; }
}

void nav_unregister (Widget *w)
{
	for (int i = 0; i < 16; i++) if (s_nav[i].w == w) s_nav[i].w = 0;
}

static bool shown (Widget *w)
{
	for (; w; w = w->parent) if (w->hidden) return false;
	return true;
}

bool key_fallback (Root *r, long k)
{
	if (!ring_on ()) return false;				// (the desktop: nothing more than today)
	unsigned mods = kapi_get_modifiers ();
	if ((k == KEY_PGUP || k == KEY_PGDN) && (mods & MOD_CTRL))	// L1 / R1: the window's tabs, else its sections
	{
		NavOwner *best = 0;
		for (int i = 0; i < 16; i++)
			if (s_nav[i].w && root_of (s_nav[i].w) == r && shown (s_nav[i].w) && (best == 0 || s_nav[i].prio > best->prio)) best = &s_nav[i];
		return best && best->step && best->step (best->w, k == KEY_PGUP ? -1 : 1);
	}
	if ((k == KEY_PGUP || k == KEY_PGDN) && sheet_scroll (r, 0, k == KEY_PGUP ? r->height * 3 / 4 : -r->height * 3 / 4)) return true;
	if (k == KEY_UP) return uk_focus_move (r, 0, -1);
	if (k == KEY_DOWN) return uk_focus_move (r, 0, 1);
	if (k == KEY_LEFT) return uk_focus_move (r, -1, 0);
	if (k == KEY_RIGHT) return uk_focus_move (r, 1, 0);
	return false;
}

// ---- the focus ring --------------------------------------------------------------------------------------------
static bool focused_leaf (Widget *c)
{
	if (!c->hasFocus || !c->canFocus || c->hidden) return false;
	for (Widget *d = c->firstChild; d; d = d->nextSib) if (d->hasFocus && !d->hidden) return false;
	return true;
}

void focus_ring (Canvas &cv, Widget *c, int x, int y)
{
	if (!focused_leaf (c) || !ring_on ()) return;
	bool console = s_cls == UK_SC_CONSOLE;
	unsigned a = console ? 0x0060C8FF : C_ACCENT;
	int w = c->width, h = c->height;
	if (w < 6 || h < 6) return;
	int r = h / 2 < 6 ? h / 2 : 6;
	uk_rline (cv, x, y, w, h, r, a, 255);			// (inside its rectangle: the next composite puts it back)
	uk_rline (cv, x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 0, a, console ? 200 : 140);
	if (console) uk_rline (cv, x + 2, y + 2, w - 4, h - 4, r > 2 ? r - 2 : 0, a, 90);
}

// ---- the focused control, told to the server -------------------------------------------------------------------------
static Widget *leaf (Widget *w)
{
	for (Widget *c = w->firstChild; c; c = c->nextSib)
	{
		if (c->hidden || !c->hasFocus) continue;
		Widget *d = leaf (c);
		if (d) return d;
		if (c->canFocus) return c;
	}
	return 0;
}

static Widget *modal_or (Widget *r)
{
	for (Widget *n = r->lastChild; n; n = n->prevSib) if (n->modal) return n;
	return r;
}

static Root *s_later;
void redraw_soon (Widget *w) { s_later = root_of (w); }

void root_tick (Root *r)
{
	if (s_later) { Root *l = s_later; s_later = 0; l->invalidate (true); }
	if (!ring_on () || r == 0) return;
	static Root *s_r; static int s_x = -1, s_y, s_w, s_h, s_type = -2;
	Widget *f = leaf (modal_or (r));
	int x = -1, y = 0, w = 0, h = 0, type = -1;
	if (f)
	{
		abs_pos (f, &x, &y); w = f->width; h = f->height;
		ExtHead *e = ext_head (f);
		if (e && e->inType > 0) type = e->inType - 1;
		else if (f->isField ()) type = ((Textbox *) f)->password ? UK_IN_PASSWORD : UK_IN_TEXT;
	}
	if (r != s_r || x != s_x || y != s_y || w != s_w || h != s_h)
	{
		s_r = r; s_x = x; s_y = y; s_w = w; s_h = h;
		if (x >= 0) port::focus_rect (x, y, w, h);
	}
	if (type != s_type) { s_type = type; port::text_hint (type); }
}

} // namespace internal

// ---- the arrows --------------------------------------------------------------------------------------------------
static void focusables (Widget *w, Widget **list, int &n, int cap)
{
	for (Widget *c = w->firstChild; c; c = c->nextSib)
	{
		if (c->hidden || c->disabled) continue;
		if (c->canFocus && n < cap) list[n++] = c;
		focusables (c, list, n, cap);
	}
}

bool uk_focus_move (Widget *root, int dx, int dy)
{
	if (root == 0) return false;
	Widget *scope = internal::modal_or (root);
	Widget *list[160]; int n = 0;
	focusables (scope, list, n, 160);
	if (n == 0) return false;
	Widget *cur = internal::leaf (scope);
	if (cur == 0) { list[0]->setFocus (); list[0]->onTabFocus (); return true; }
	int cx, cy; internal::abs_pos (cur, &cx, &cy);
	cx += cur->width / 2; cy += cur->height / 2;
	Widget *best = 0; long bestD = 0;
	for (int i = 0; i < n; i++)
	{
		Widget *c = list[i];
		if (c == cur) continue;
		int x, y; internal::abs_pos (c, &x, &y);
		int mxx = x + c->width / 2, myy = y + c->height / 2;
		int along = dx ? (mxx - cx) * dx : (myy - cy) * dy;	// how far in the direction asked
		int across = dx ? (myy - cy) : (mxx - cx);
		if (along <= 0) continue;
		if (across < 0) across = -across;
		long d = (long) along * along + 4L * across * across;	// (straight ahead first)
		if (best == 0 || d < bestD) { best = c; bestD = d; }
	}
	if (best == 0) return false;
	best->setFocus ();
	best->onTabFocus ();
	best->invalidate (true);
	return true;
}

} // namespace uikit
