//
// uikit/sidepanel.cpp -- SidePanel (uikit/sidepanel.h): the items and the free content, shown as the mode wants -- the
// desktop's sidebar (UK_SP_FULL), pocket's rail, drawer, slide-over and sheet, console's column (docs/POCKETUI-TECH-
// STUDY.md section 6.8). Its state is behind Widget::ext (uikit/internal/adapt_int.h); its geometry is the app's (the
// "home" rectangle: what the app made it and its anchors did since) seen through the presentation: a rail narrows it, an
// open overlay covers its parent (the content dimmed behind the panel), a closed one leaves a small tab at the edge.
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
#include "uikit/sidepanel.h"
#include "uikit/adapt.h"
#include "uikit/paint.h"
#include "uikit/root.h"
#include "uikit/lang.h"
#include "uikit/internal/adapt_int.h"
#include "appkit/appkit.h"

namespace uikit {

namespace {
enum { SP_MAXPAGES = 8, TAB_W = 20, TAB_H = 60, HDR_H = 30, CON_BG = 0x00142038, CON_GLOW = 0x0060C8FF };
struct SpItem
{
	int	 id, icon, level, under, badge, toggle;
	unsigned flags;
	char	 label[64], trailing[20], sub[96];
	unsigned *thumb; int tw, th;
};
struct SpPage { char label[32]; int icon; Widget *w; };
struct SpExt : internal::ExtHead
{
	SpItem	*items = 0; int n = 0, cap = 0;
	int	 nextId = 1000000;
	int	 sel = -1, hot = -1, cursor = -1;	// the chosen item (its id); under the pointer, the keys' (indexes)
	int	 top = 0;			// the items scrolled by (px)
	SpPage	 pages[SP_MAXPAGES]; int npages = 0, page = 0;
	Widget	*content = 0, *footer = 0; int footerH = 0;
	Widget	*header = 0; int headerH = 0;		// (setHeader: over the items and the pages)
	SpIconFn iconFn = 0; TextFace *fItem = 0, *fHead = 0;
	unsigned bg = UK_AUTO, edge = UK_AUTO;
	int	 minW = 0, prefW = 0, maxW = 0;
	int	 homeL = 0, homeT = 0, homeW = 0, homeH = 0;	// the app's rectangle
	int	 setL = 0, setT = 0, setW = 0, setH = 0;	// what the panel made itself last
	int	 told = -1;			// the presentation the app was last told
	bool	 open = false, sheetFull = false, applying = false;
	Widget	*toggle = 0;
	unsigned *scrim = 0; int scrimW = 0, scrimH = 0;
	int	 dragIdx = -1, dragY0 = 0, dropBefore = -1; bool dragOn = false;
	bool	 bl = false, br = false;
	unsigned lastClick = 0; int lastIdx = -1;
	int	 hotPage = -1, hotTab = 0;	// the page tab under the pointer; 1: the edge's tab / the scrim
	bool	 noReopen = false;
	bool	 noRail = false;		// (setRail (false))
	bool	 subOn = false;		// (the subtitles shown now: sp_subs)
};

SpExt *sp (SidePanel *p) { return (SpExt *) internal::ext_head (p); }

// The toggle button the app may place (toggleButton): a "menu" glyph for a navigation, a panel's for an inspector.
class SpToggle : public Widget
{
public:
	SidePanel *panel; bool nav;
	SpToggle (SidePanel *p, bool n) : Widget (0, 0, 30, 28), panel (p), nav (n) { tip = n ? "Places" : "Panel"; }
	unsigned bgColor () override { return parent ? parent->bgColor () : C_BG; }
	void onDraw () override
	{
		unsigned b = bgColor ();
		canvas.clear (b);
		if (hover || pressed || panel->isOpen ()) uk_rbox (canvas, 0, 0, width, height, 6, uk_tone (b, pressed ? 112 : 140), uk_tone (b, pressed ? 118 : 126));
		unsigned ink = uk_ink_for (b);
		if (nav) uk_glyph (canvas, WKG_MENU, width / 2, height / 2, 14, ink);
		else { canvas.frameRect (width / 2 - 8, height / 2 - 7, 16, 14, ink); canvas.fillRect (width / 2 + 2, height / 2 - 7, 6, 14, ink); }
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (in != hover) { hover = in; invalidate (true); }
		if (bl && in && !pressed) { pressed = true; invalidate (true); }
		else if (!bl && pressed) { pressed = false; invalidate (true); if (in) panel->open (!panel->isOpen ()); }
		return in;
	}
};

bool sp_step (Widget *w, int dir);
}

// ---- the colours, the faces ------------------------------------------------------------------------------------
static bool console () { return uk_size_class () == UK_SC_CONSOLE; }
static unsigned sp_bg (SpExt *e) { return console () ? (unsigned) CON_BG : e->bg != UK_AUTO ? e->bg : uk_mix (C_BG, C_FIELD, 70); }
static unsigned sp_ink (SpExt *e) { return console () ? 0x00E4ECF8 : uk_ink_for (sp_bg (e)); }
static unsigned sp_dim (SpExt *) { return console () ? 0x007C94B8 : uk_mix (C_BG, C_TEXT, 150); }

SidePanel::SidePanel (int l, int t, int w, int h, int side, int role)
  : Widget (l, t, w, h), onSelect (0), onToggle (0), onItemMenu (0), onDrop (0), onReorder (0), onPresentation (0),
    m_side (side == UK_SP_RIGHT ? UK_SP_RIGHT : UK_SP_LEFT), m_role (role == UK_SP_INSPECTOR ? UK_SP_INSPECTOR : UK_SP_NAVIGATION)
{
	SpExt *e = internal::ext_of<SpExt> (this, internal::EXT_SIDEPANEL);
	e->homeL = e->setL = l; e->homeT = e->setT = t; e->homeW = e->setW = w; e->homeH = e->setH = h;
	e->minW = w / 2; e->prefW = w; e->maxW = w * 2;
	e->onClass = [] (Widget *x, internal::ExtHead *) { ((SidePanel *) x)->layout (); x->invalidate (true); };
	e->destroy = [] (internal::ExtHead *x)
	{
		SpExt *s = (SpExt *) x;
		internal::nav_unregister (x->owner);
		for (int i = 0; i < s->n; i++) delete [] s->items[i].thumb;
		delete [] s->items; delete [] s->scrim;
		delete s;
	};
	internal::adaptive_add (e);
	if (m_role == UK_SP_NAVIGATION) internal::nav_register (this, 1, sp_step);
	canFocus = true;
}

SidePanel::~SidePanel () {}

// ---- the items ---------------------------------------------------------------------------------------------------
static SpItem *sp_new (SpExt *e)
{
	if (e->n >= e->cap)
	{
		int nc = e->cap ? e->cap * 2 : 32;
		SpItem *ni = new SpItem[nc];
		for (int i = 0; i < e->n; i++) ni[i] = e->items[i];
		delete [] e->items;
		e->items = ni; e->cap = nc;
	}
	SpItem &it = e->items[e->n++];
	it.id = -1; it.icon = -1; it.level = 0; it.under = -1; it.badge = 0; it.toggle = -1; it.flags = 0;
	it.label[0] = 0; it.trailing[0] = 0; it.sub[0] = 0; it.thumb = 0; it.tw = it.th = 0;
	return &it;
}
static void sp_copy (char *d, const char *s, int cap) { int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static int sp_index (SpExt *e, int id) { for (int i = 0; i < e->n; i++) if (e->items[i].id == id) return i; return -1; }
static bool sp_heading (const SpItem &it) { return (it.flags & (UK_SPI_HEADING | UK_SPI_SEPARATOR)) != 0; }
static bool sp_pickable (const SpItem &it) { return !sp_heading (it) && !(it.flags & UK_SPI_DISABLED); }

int SidePanel::addHeading (const char *label, unsigned fl)
{
	SpExt *e = sp (this);
	SpItem *it = sp_new (e);
	it->id = e->nextId++; it->flags = UK_SPI_HEADING | (fl & ~(unsigned) UK_SPI_SEPARATOR);
	sp_copy (it->label, label, sizeof it->label);
	invalidate (true);
	return it->id;
}

int SidePanel::addSeparator ()
{
	SpExt *e = sp (this);
	SpItem *it = sp_new (e);
	it->id = e->nextId++; it->flags = UK_SPI_SEPARATOR;
	invalidate (true);
	return it->id;
}

int SidePanel::addItem (int id, const char *label, int icon, int level, int under)
{
	SpExt *e = sp (this);
	SpItem *it = sp_new (e);
	it->id = id; it->icon = icon; it->level = level < 0 ? 0 : level;
	if (under < 0) for (int i = e->n - 2; i >= 0; i--) if (e->items[i].flags & UK_SPI_HEADING) { under = e->items[i].id; break; }
	it->under = under;
	sp_copy (it->label, label, sizeof it->label);
	invalidate (true);
	return id;
}

void SidePanel::setLabel (int id, const char *label) { SpExt *e = sp (this); int i = sp_index (e, id); if (i >= 0) { sp_copy (e->items[i].label, label, sizeof e->items[i].label); invalidate (true); } }
void SidePanel::setIcon (int id, int icon) { SpExt *e = sp (this); int i = sp_index (e, id); if (i >= 0) { e->items[i].icon = icon; invalidate (true); } }
void SidePanel::setBadge (int id, int count) { SpExt *e = sp (this); int i = sp_index (e, id); if (i >= 0 && e->items[i].badge != count) { e->items[i].badge = count; invalidate (true); } }
void SidePanel::setTrailing (int id, const char *t) { SpExt *e = sp (this); int i = sp_index (e, id); if (i >= 0) { sp_copy (e->items[i].trailing, t, sizeof e->items[i].trailing); invalidate (true); } }
void SidePanel::setSubtitle (int id, const char *t) { SpExt *e = sp (this); int i = sp_index (e, id); if (i >= 0) { sp_copy (e->items[i].sub, t, sizeof e->items[i].sub); invalidate (true); } }
void SidePanel::setToggle (int id, int on) { SpExt *e = sp (this); int i = sp_index (e, id); if (i >= 0) { e->items[i].toggle = on < 0 ? -1 : on ? 1 : 0; invalidate (true); } }
void SidePanel::setFlags (int id, unsigned f) { SpExt *e = sp (this); int i = sp_index (e, id); if (i >= 0) { e->items[i].flags = (e->items[i].flags & (UK_SPI_HEADING | UK_SPI_SEPARATOR)) | (f & ~(unsigned) (UK_SPI_HEADING | UK_SPI_SEPARATOR)); invalidate (true); } }
unsigned SidePanel::flags (int id) { SpExt *e = sp (this); int i = sp_index (e, id); return i >= 0 ? e->items[i].flags : 0; }

void SidePanel::setThumb (int id, const unsigned *px, int w, int h)
{
	SpExt *e = sp (this);
	int i = sp_index (e, id);
	if (i < 0) return;
	SpItem &it = e->items[i];
	delete [] it.thumb; it.thumb = 0; it.tw = it.th = 0;
	if (px && w > 0 && h > 0 && w <= 256 && h <= 256)
	{
		it.thumb = new unsigned[w * h];
		for (int k = 0; k < w * h; k++) it.thumb[k] = px[k];
		it.tw = w; it.th = h;
	}
	invalidate (true);
}

void SidePanel::moveItem (int id, int beforeId)
{
	SpExt *e = sp (this);
	int i = sp_index (e, id);
	if (i < 0) return;
	SpItem keep = e->items[i];
	for (int k = i; k + 1 < e->n; k++) e->items[k] = e->items[k + 1];
	e->n--;
	int b = beforeId < 0 ? e->n : sp_index (e, beforeId);
	if (b < 0) b = e->n;
	for (int k = e->n; k > b; k--) e->items[k] = e->items[k - 1];
	e->items[b] = keep; e->n++;
	invalidate (true);
}

void SidePanel::remove (int id)
{
	SpExt *e = sp (this);
	int i = sp_index (e, id);
	if (i < 0) return;
	delete [] e->items[i].thumb;
	for (int k = i; k + 1 < e->n; k++) e->items[k] = e->items[k + 1];
	e->n--;
	if (e->sel == id) e->sel = -1;
	e->hot = e->cursor = -1;
	invalidate (true);
}

void SidePanel::clear ()
{
	SpExt *e = sp (this);
	for (int i = 0; i < e->n; i++) delete [] e->items[i].thumb;
	e->n = 0; e->sel = e->hot = e->cursor = -1; e->top = 0;
	invalidate (true);
}

void SidePanel::select (int id, bool fire)
{
	SpExt *e = sp (this);
	if (e->sel == id) return;
	e->sel = id;
	int i = sp_index (e, id);
	if (i >= 0) e->cursor = i;
	invalidate (true);
	if (fire && onSelect && id >= 0) onSelect (*this, id);
}
int SidePanel::selected () { return sp (this)->sel; }

namespace { bool sp_step (Widget *w, int dir)	// (the console's L1 / R1: the next / previous section's first item)
{
	SidePanel *p = (SidePanel *) w;
	SpExt *e = sp (p);
	int starts[64], ns = 0;
	bool fresh = true;
	for (int i = 0; i < e->n && ns < 64; i++)
	{
		if (sp_heading (e->items[i])) { fresh = true; continue; }
		if (fresh && sp_pickable (e->items[i])) { starts[ns++] = i; fresh = false; }
	}
	if (ns <= 1)						// (one section: the items one by one)
	{
		int i = sp_index (e, e->sel);
		for (int k = i + dir; k >= 0 && k < e->n; k += dir) if (sp_pickable (e->items[k])) { p->select (e->items[k].id, true); return true; }
		if (i < 0) for (int k = 0; k < e->n; k++) if (sp_pickable (e->items[k])) { p->select (e->items[k].id, true); return true; }
		return false;
	}
	int cur = sp_index (e, e->sel), s = -1;
	for (int k = 0; k < ns; k++) if (starts[k] <= cur) s = k;
	int t;
	if (dir > 0) t = s + 1;
	else t = s < 0 ? 0 : cur == starts[s] ? s - 1 : s;
	if (t < 0 || t >= ns) return false;
	p->select (e->items[starts[t]].id, true);
	return true;
} }

// ---- the free content ------------------------------------------------------------------------------------------------
int SidePanel::addPage (const char *label, int icon, Widget *content)
{
	SpExt *e = sp (this);
	if (e->npages >= SP_MAXPAGES || content == 0) return -1;
	SpPage &pg = e->pages[e->npages];
	sp_copy (pg.label, label, sizeof pg.label); pg.icon = icon; pg.w = content;
	addChild (content);
	int k = e->npages++;
	layout ();
	return k;
}
void SidePanel::setPage (int pgn) { SpExt *e = sp (this); if (pgn >= 0 && pgn < e->npages && pgn != e->page) { e->page = pgn; layout (); invalidate (true); } }
int SidePanel::page () { return sp (this)->page; }
void SidePanel::setContent (Widget *w) { SpExt *e = sp (this); if (e->content) removeChild (e->content); e->content = w; if (w) addChild (w); layout (); }
void SidePanel::setHeader (Widget *w, int h) { SpExt *e = sp (this); if (e->header) removeChild (e->header); e->header = w; e->headerH = w ? h : 0; if (w) addChild (w); layout (); }
void SidePanel::setFooter (Widget *w, int h) { SpExt *e = sp (this); if (e->footer) removeChild (e->footer); e->footer = w; e->footerH = w ? h : 0; if (w) addChild (w); layout (); }

void SidePanel::setIconFn (SpIconFn fn) { sp (this)->iconFn = fn; invalidate (true); }
void SidePanel::setFaces (TextFace *item, TextFace *heading) { SpExt *e = sp (this); e->fItem = item; e->fHead = heading; invalidate (true); }
void SidePanel::setColors (unsigned b, unsigned ed) { SpExt *e = sp (this); e->bg = b; e->edge = ed; invalidate (true); }
void SidePanel::setWidths (int minW, int prefW, int maxW) { SpExt *e = sp (this); e->minW = minW; e->prefW = prefW; e->maxW = maxW; }
void SidePanel::setRail (bool allowed) { sp (this)->noRail = !allowed; layout (); invalidate (true); }
unsigned SidePanel::bgColor () { return sp_bg (sp (this)); }

// ---- the presentation --------------------------------------------------------------------------------------------------
static bool overlay (int p) { return p == UK_SP_DRAWER || p == UK_SP_SLIDEOVER || p == UK_SP_SHEET; }

int SidePanel::presentation ()
{
	int sc = uk_size_class ();
	if (sc == UK_SC_REGULAR) return UK_SP_FULL;
	SpExt *e = sp (this);
	int items = 0;
	for (int i = 0; i < e->n; i++) if (!sp_heading (e->items[i])) items++;
	if (sc == UK_SC_CONSOLE) return m_role == UK_SP_NAVIGATION && items > 0 ? UK_SP_COLUMN : m_role == UK_SP_NAVIGATION ? UK_SP_DRAWER : UK_SP_SLIDEOVER;
	int pw = parent ? parent->width : e->homeW * 4;
	if (sc == UK_SC_COMPACT && (pw >= e->homeW * 4 || (e->noRail && m_role == UK_SP_NAVIGATION))) return UK_SP_FULL;	// (a wide pocket screen, no rail wanted: whole)
	if (m_role == UK_SP_INSPECTOR) return sc == UK_SC_NARROW ? UK_SP_SHEET : UK_SP_SLIDEOVER;
	if (sc == UK_SC_NARROW || items == 0) return UK_SP_DRAWER;
	return UK_SP_RAIL;
}

static int rail_w () { int r = uk_metrics ().rail; return r < 40 ? 40 : r; }
static int column_w (SpExt *e) { int c = uk_metrics ().rail; return c > e->homeW + 80 ? e->homeW + 80 : c; }

static void sp_check (SidePanel *p)		// (the presentation changed since the panel placed itself: placed now)
{
	SpExt *e = sp (p);
	if (e->told != p->presentation () && !e->applying) p->layout ();
}

int SidePanel::reservedWidth ()
{
	sp_check (this);
	switch (presentation ())
	{
	case UK_SP_FULL: return sp (this)->homeW;
	case UK_SP_RAIL: return rail_w ();
	case UK_SP_COLUMN: return column_w (sp (this));
	default: return 0;
	}
}

// What the parent's anchors did to the panel since it placed itself: the app's rectangle follows.
static void sp_sync (SidePanel *p, SpExt *e)
{
	if (e->applying) return;
	if (e->open && overlay (p->presentation ())) { p->left = e->setL; p->top = e->setT; return; }	// (open over its parent: kept)
	e->homeL += p->left - e->setL; e->homeT += p->top - e->setT;
	e->setL = p->left; e->setT = p->top;
}

// The panel's rectangle now (in its parent): what the presentation makes of the app's.
static void sp_rect (SidePanel *p, SpExt *e, int pres, int side, int *x, int *y, int *w, int *h)
{
	int pw = p->parent ? p->parent->width : e->homeW, ph = p->parent ? p->parent->height : e->homeH;
	*x = e->homeL; *y = e->homeT; *w = e->homeW; *h = e->homeH;
	switch (pres)
	{
	case UK_SP_RAIL:
		if (e->open) { *w = e->homeW < 200 ? 200 : e->homeW; if (side == UK_SP_RIGHT) *x = e->homeL + e->homeW - *w; }
		else { *w = rail_w (); if (side == UK_SP_RIGHT) *x = e->homeL + e->homeW - *w; }
		break;
	case UK_SP_COLUMN:
		*w = column_w (e); if (side == UK_SP_RIGHT) *x = e->homeL + e->homeW - *w;
		break;
	case UK_SP_DRAWER: case UK_SP_SLIDEOVER: case UK_SP_SHEET:
		if (e->open) { *x = 0; *y = 0; *w = pw; *h = ph; break; }
		if (e->toggle) { *w = 0; *h = 0; break; }	// (the app's button opens it: no tab)
		if (pres == UK_SP_SHEET) { *w = TAB_H + 20; *h = 16; *x = (pw - *w) / 2; *y = ph - *h; }
		else { *w = TAB_W; *h = TAB_H; *y = e->homeT + (e->homeH - TAB_H) / 2; *x = side == UK_SP_RIGHT ? e->homeL + e->homeW - TAB_W : e->homeL; }
		break;
	}
}

// The panel's own drawing box when it covers its parent (an open overlay): where the panel is in it.
static void sp_box (SidePanel *p, SpExt *e, int pres, int side, int *x, int *y, int *w, int *h)
{
	*x = 0; *y = 0; *w = p->width; *h = p->height;
	if (!overlay (pres) || !e->open) return;
	if (pres == UK_SP_SHEET)
	{
		int sh = e->sheetFull ? p->height - 24 : p->height / 2;
		*y = p->height - sh; *h = sh;
		return;
	}
	int pw = e->homeW + (pres == UK_SP_DRAWER ? 40 : 0);
	if (pw > p->width - 40) pw = p->width - 40;
	if (pw < 160) pw = p->width < 160 ? p->width : 160;
	*w = pw; *x = side == UK_SP_RIGHT ? p->width - pw : 0;
}

// The header's height in the box now: 0 without one, and while the panel shows no box (a rail not expanded, an overlay
// closed) -- it is hidden then.
static int sp_head (SpExt *e, int pres)
{
	if (!e->header || (pres == UK_SP_RAIL && !e->open) || (overlay (pres) && !e->open)) return 0;
	return e->headerH;
}

static void sp_apply (SidePanel *p, SpExt *e, int pres, int side)
{
	int x, y, w, h;
	sp_rect (p, e, pres, side, &x, &y, &w, &h);
	e->applying = true;
	p->left = x; p->top = y;
	e->setL = x; e->setT = y;
	if (w != p->width || h != p->height) { e->setW = w; e->setH = h; p->Widget::resizeTo (w > 0 ? w : 1, h > 0 ? h : 1); }
	else p->layout ();
	p->transparent = overlay (pres) && !e->open;		// (the edge's tab: its rounded corners see-through)
	p->hidden = overlay (pres) && !e->open && e->toggle != 0;
	if ((overlay (pres) && e->open) || (pres == UK_SP_RAIL && e->open)) p->bringToFront ();
	e->applying = false;
	if (p->parent) p->parent->invalidate (true);
}

void SidePanel::resizeTo (int w, int h)
{
	SpExt *e = sp (this);
	if (e == 0 || e->applying) { Widget::resizeTo (w, h); return; }
	sp_sync (this, e);
	// The app's own call gives the panel's size (whatever the presentation made of it); the parent's anchors (its
	// Widget::layout, the parent's new size not yet its lytW / lytH) give the panel's size now plus their growth.
	bool anchors = parent && (parent->width != parent->lytW || parent->height != parent->lytH);
	if (anchors) { e->homeW += w - e->setW; e->homeH += h - e->setH; }
	else { e->homeW = w; e->homeH = h; }
	if (e->homeW < 1) e->homeW = 1;
	if (e->homeH < 1) e->homeH = 1;
	e->setW = w; e->setH = h;
	if (presentation () == UK_SP_FULL && !overlay (presentation ())) { e->applying = true; Widget::resizeTo (e->homeW, e->homeH); e->setW = e->homeW; e->setH = e->homeH; e->applying = false; }
	else sp_apply (this, e, presentation (), m_side);
}

void SidePanel::place (int l, int t, int w, int h)
{
	SpExt *e = sp (this);
	e->homeL = l; e->homeT = t; e->homeW = w > 0 ? w : 1; e->homeH = h > 0 ? h : 1;
	int pres = presentation ();
	if (pres == UK_SP_FULL)
	{
		e->applying = true;
		left = l; top = t; e->setL = l; e->setT = t;
		if (width != e->homeW || height != e->homeH) Widget::resizeTo (e->homeW, e->homeH); else layout ();
		e->setW = width; e->setH = height;
		e->applying = false;
	}
	else sp_apply (this, e, pres, m_side);
}

void SidePanel::open (bool on)
{
	SpExt *e = sp (this);
	int pres = presentation ();
	if (pres == UK_SP_FULL || pres == UK_SP_COLUMN) on = false;
	if (on == e->open) return;
	e->open = on;
	if (on && overlay (pres) && parent)			// the content behind, dimmed (a copy, made once)
	{
		int pw = parent->width, ph = parent->height;
		parent->draw ();
		delete [] e->scrim;
		e->scrim = new unsigned[(unsigned long) pw * ph];
		e->scrimW = pw; e->scrimH = ph;
		Canvas c; c.adopt (e->scrim, pw, ph);
		internal::dim_copy (c, parent, 0, 0, pw, ph, console () ? 150 : 100);
		c.release ();
	}
	if (!on) { delete [] e->scrim; e->scrim = 0; e->sheetFull = false; }
	e->hot = -1;
	sp_apply (this, e, pres, m_side);
	if (on && pres != UK_SP_RAIL && internal::ring_on ()) setFocus ();
	if (e->toggle) e->toggle->invalidate (true);
	invalidate (true);
}
bool SidePanel::isOpen () { return sp (this)->open; }

Widget *SidePanel::toggleButton ()
{
	SpExt *e = sp (this);
	if (e->toggle == 0) { e->toggle = new SpToggle (this, m_role == UK_SP_NAVIGATION); layout (); }
	return e->toggle;
}

// ---- the rows ---------------------------------------------------------------------------------------------------------
// Each item's row in the box (x0, y0, w): its top and height (-1: not shown -- folded); -> the rows' bottom.
static int sp_rows (SpExt *e, int pres, int y0, int *ry, int *rh)
{
	bool rail = pres == UK_SP_RAIL && !e->open, col = pres == UK_SP_COLUMN || console ();
	bool thumbs = false;
	for (int i = 0; i < e->n; i++) if (e->items[i].thumb) thumbs = true;
	int row = col ? uk_metrics ().row : uk_size_class () == UK_SC_REGULAR ? 28 : uk_metrics ().row > 28 ? uk_metrics ().row : 28;
	if (thumbs) row = row < 54 ? 54 : row;
	if (e->subOn && !rail) { int two = 2 * uk_fh () + 14; if (row < two) row = two; }	// (a label and its line of help)
	int y = y0 + (rail ? 8 : 10), folded = -1;
	for (int i = 0; i < e->n && i < 512; i++)
	{
		const SpItem &it = e->items[i];
		if (it.flags & UK_SPI_HEADING)
		{
			folded = (it.flags & UK_SPI_FOLDED) ? it.id : -1;
			if (rail) { ry[i] = y; rh[i] = 9; y += 9; continue; }
			y += 8; ry[i] = y; rh[i] = col ? 28 : 20; y += rh[i];
			continue;
		}
		if (it.flags & UK_SPI_SEPARATOR) { folded = -1; ry[i] = y; rh[i] = 9; y += 9; continue; }
		if (folded >= 0 && it.under == folded) { ry[i] = -1; rh[i] = 0; continue; }
		if (rail) { ry[i] = y; rh[i] = 40; y += 44; continue; }
		ry[i] = y; rh[i] = row; y += row + 2;
	}
	return y;
}

// The subtitles shown now: some item has one, the panel shown whole (not a rail) at 240 px or more, and every item
// fits at two lines in its height (a big screen) -> e->subOn.
static void sp_box (SidePanel *p, SpExt *e, int pres, int side, int *x, int *y, int *w, int *h);
static void sp_subs (SidePanel *p, SpExt *e, int pres, int side)
{
	bool any = false;
	for (int i = 0; i < e->n && !any; i++) if (e->items[i].sub[0]) any = true;
	e->subOn = false;
	if (!any || (pres == UK_SP_RAIL && !e->open) || e->npages > 0) return;
	int bx, by, bw, bh;
	sp_box (p, e, pres, side, &bx, &by, &bw, &bh);
	if (bw < 240) return;
	e->subOn = true;
	int ry[512], rh[512];
	if (sp_rows (e, pres, 0, ry, rh) + 8 > bh - e->footerH - sp_head (e, pres)) e->subOn = false;
}

static void sp_icon (SpExt *e, Canvas &cv, int icon, int x, int y, int s, unsigned ink, bool selected)
{
	if (icon == -1) return;
	if (e->iconFn) { e->iconFn (cv, icon, x, y, s, ink, selected); return; }
	if (icon >= 0) uk_glyph (cv, icon, x + s / 2, y + s / 2, s, ink);
	else if (icon == UK_SP_TOGGLE_ON) { uk_glyph (cv, WKG_RING, x + s / 2, y + s / 2, s - 2, ink); uk_glyph (cv, WKG_DOT, x + s / 2, y + s / 2, s / 2, ink); }
	else if (icon == UK_SP_TOGGLE_OFF) uk_glyph (cv, WKG_RING, x + s / 2, y + s / 2, s - 2, uk_mix (ink, C_BG, 140));
}

static void sp_badge (Canvas &cv, int xr, int cy, int count, unsigned bg)
{
	if (count == -1) { uk_glyph (cv, WKG_DOT, xr - 5, cy, 8, C_ACCENT); return; }
	char b[8]; int n = 0, v = count > 999 ? 999 : count, d[4], k = 0;
	do { d[k++] = v % 10; v /= 10; } while (v && k < 3);
	while (k) b[n++] = (char) ('0' + d[--k]);
	if (count > 999) b[n++] = '+';
	b[n] = 0;
	int tw = uk_tw (b, 2), w = tw + 12 < 20 ? 20 : tw + 12;
	unsigned pill = bg == C_SEL_TEXT ? uk_mix (C_ACCENT, 0x00FFFFFF, 60) : C_ACCENT;
	uk_rbox (cv, xr - w, cy - 9, w, 18, 9, pill, pill);
	uk_text_c (cv, xr - w, cy - 9, w, 18, b, uk_ink_on (pill), 2);
}

// The items drawn in the box (bx, by, bw, bh) of the canvas.
static void sp_draw_items (SidePanel *p, SpExt *e, int pres, int bx, int by, int bw, int bh)
{
	Canvas &cv = p->canvas;
	bool rail = pres == UK_SP_RAIL && !e->open, col = pres == UK_SP_COLUMN || console ();
	unsigned bg = sp_bg (e), ink = sp_ink (e), dim = sp_dim (e);
	int ry[512], rh[512];
	int n = e->n < 512 ? e->n : 512;
	sp_rows (e, pres, by - e->top, ry, rh);
	int limit = by + bh;
	for (int i = 0; i < n; i++)
	{
		const SpItem &it = e->items[i];
		int y = ry[i], h = rh[i];
		if (y < 0 || y + h <= by || y >= limit) continue;
		if (it.flags & UK_SPI_SEPARATOR) { cv.fillRect (bx + 12, y + 4, bw - 24, 1, uk_mix (bg, ink, 40)); continue; }
		if (it.flags & UK_SPI_HEADING)
		{
			if (rail) { cv.fillRect (bx + 12, y + 4, bw - 24, 1, uk_mix (bg, ink, 50)); continue; }
			UkFaceScope fs (e->fHead);
			uk_text (cv, bx + 16, y + (col ? 6 : 0), it.label, dim, 2);
			if (it.flags & UK_SPI_FOLDABLE) uk_glyph (cv, (it.flags & UK_SPI_FOLDED) ? WKG_CHEV_RIGHT : WKG_CHEV_DOWN, bx + bw - 20, y + (col ? 6 : 0) + uk_fh () / 2, 7, dim);
			continue;
		}
		bool on = it.id == e->sel, hot = i == e->hot || (i == e->cursor && p->hasFocus && uk_size_class () != UK_SC_REGULAR);
		bool off = (it.flags & UK_SPI_DISABLED) != 0;
		if (rail)
		{
			int x = bx + (bw - 40) / 2;
			if (on) uk_rbox (cv, x, y, 40, h, 8, C_ACCENT, C_ACCENT);
			else if (hot) uk_rbox (cv, x, y, 40, h, 8, uk_mix (bg, C_ACCENT, 40), uk_mix (bg, C_ACCENT, 40));
			unsigned ik = on ? C_SEL_TEXT : off ? uk_mix (bg, ink, 110) : ink;
			sp_icon (e, cv, it.icon >= -1 ? it.icon : -1, x + 11, y + 11, 18, ik, on);
			if (it.icon == -1) { char b[2] = { it.label[0], 0 }; uk_text_c (cv, x, y, 40, h, b, ik, 2); }
			if (it.badge) sp_badge (cv, x + 42, y + 6, -1, on ? C_SEL_TEXT : bg);	// (a rail: a dot)
			continue;
		}
		int rx = bx + 8, rw = bw - 16;
		unsigned rowInk = off ? uk_mix (bg, ink, 110) : ink;
		if (col)
		{
			if (on) { uk_rbox (cv, rx, y, rw, h, 8, 0x002E5FA8, 0x00224A88); uk_rline (cv, rx, y, rw, h, 8, CON_GLOW, 255); }
			else if (hot) uk_rbox (cv, rx, y, rw, h, 8, 0x001E3258, 0x001A2C4E);
		}
		else if (on) { uk_rbox (cv, rx, y, rw, h, 6, C_ACCENT, C_ACCENT); rowInk = C_SEL_TEXT; }
		else if (hot) uk_rbox (cv, rx, y, rw, h, 6, uk_mix (bg, C_ACCENT, 40), uk_mix (bg, C_ACCENT, 40));
		int x = rx + 10 + it.level * 14;
		if (it.toggle >= 0) { sp_icon (e, cv, it.toggle ? UK_SP_TOGGLE_ON : UK_SP_TOGGLE_OFF, x - 2, y + (h - 18) / 2, 18, rowInk, on); x += 26; }
		if (it.thumb)
		{
			int tw = 54, th = 36;
			if (it.tw * th > it.th * tw) th = it.th * tw / (it.tw ? it.tw : 1); else tw = it.tw * th / (it.th ? it.th : 1);
			if (tw < 4) tw = 4;
			if (th < 4) th = 4;
			int ox = x + (54 - tw) / 2, oy = y + (h - th) / 2;
			for (int j = 0; j < th; j++) for (int k = 0; k < tw; k++) cv.pixel (ox + k, oy + j, it.thumb[(j * it.th / th) * it.tw + k * it.tw / tw] & 0x00FFFFFF);
			cv.frameRect (ox - 1, oy - 1, tw + 2, th + 2, uk_mix (bg, 0, 90));
			x += 64;
		}
		else if (it.icon != -1)						// (the app's pictures: as big as the row lets them)
		{
			int is = e->iconFn ? (h - 6 < 16 ? 16 : h - 6 > 24 ? 24 : h - 6) : 16;
			sp_icon (e, cv, it.icon, x, y + (h - is) / 2, is, rowInk, on); x += is + 10;
		}
		int right = rx + rw - 8;
		if (it.badge) { sp_badge (cv, right, y + h / 2, it.badge, on ? C_SEL_TEXT : bg); right -= it.badge > 0 ? 34 : 14; }
		if (it.trailing[0]) { int tw = uk_tw (it.trailing); uk_text_l (cv, right - tw, y, h, it.trailing, on ? rowInk : uk_mix (bg, rowInk, 150)); right -= tw + 8; }
		UkFaceScope fs (e->fItem);
		char b[96];
		if (e->subOn && it.sub[0])					// the label bold, its line of help under it
		{
			int fh = uk_fh (), ty = y + (h - 2 * fh - 2) / 2;
			uk_text_fit (it.label, right - x, b, sizeof b, 2);
			uk_text (cv, x, ty, b, rowInk, 2);
			uk_text_fit (it.sub, right - x, b, sizeof b);
			uk_text (cv, x, ty + fh + 2, b, on ? uk_mix (C_ACCENT, rowInk, 200) : uk_mix (bg, rowInk, 150));
		}
		else
		{
			uk_text_fit (it.label, right - x, b, sizeof b, on ? 2 : 0);
			uk_text (cv, x, y + (h - uk_fh ()) / 2, b, rowInk, on ? 2 : 0);
		}
		if (e->dragOn && e->dropBefore == i) cv.fillRect (rx, y - 2, rw, 2, C_ACCENT);
	}
	if (e->dragOn && e->dropBefore == e->n && n > 0) { int y = ry[n - 1] + rh[n - 1]; cv.fillRect (bx + 8, y, bw - 16, 2, C_ACCENT); }
}

// ---- the geometry of the children ----------------------------------------------------------------------------------------
void SidePanel::layout ()
{
	SpExt *e = sp (this);
	if (e == 0) { Widget::layout (); return; }
	int pres = presentation ();
	if (e->told != pres && !e->applying)			// (the presentation changed: the panel placed, the app told)
	{
		int was = e->told;
		e->told = pres;
		if (pres != UK_SP_RAIL && !overlay (pres)) e->open = false;
		if (was >= 0 || pres != UK_SP_FULL) { sp_apply (this, e, pres, m_side); if (onPresentation) onPresentation (*this, pres); }
	}
	sp_subs (this, e, pres, m_side);
	int bx, by, bw, bh;
	sp_box (this, e, pres, m_side, &bx, &by, &bw, &bh);
	bool shown = !((pres == UK_SP_RAIL && !e->open) || (overlay (pres) && !e->open));
	int hdr = e->npages >= 2 ? HDR_H : 0;
	if (pres == UK_SP_SHEET) { by += 18; bh -= 18; }		// (the grabber)
	if (e->header)							// the header: at the box's top, the rest under it
	{
		Widget *w = e->header;
		int hh = sp_head (e, pres);
		w->hidden = hh == 0;
		if (hh) { w->left = bx; w->top = by; if (bw != w->width || hh != w->height) w->resizeTo (bw > 1 ? bw : 1, hh); by += hh; bh -= hh; }
	}
	int foot = e->footerH, rowsEnd = 0;
	if (e->content && e->npages == 0)
	{
		int ry[512], rh[512];
		rowsEnd = e->n ? sp_rows (e, pres, by, ry, rh) : by;
	}
	for (int k = 0; k < e->npages; k++)
	{
		Widget *w = e->pages[k].w;
		w->hidden = !shown || k != e->page;
		if (!w->hidden)
		{
			w->left = bx; w->top = by + hdr;
			int ww = bw, wh = bh - hdr - foot;
			if (ww != w->width || wh != w->height) w->resizeTo (ww > 1 ? ww : 1, wh > 1 ? wh : 1);
		}
	}
	if (e->content)
	{
		Widget *w = e->content;
		w->hidden = !shown;
		int y = e->npages ? by + hdr : rowsEnd + 4;
		if (!w->hidden) { w->left = bx; w->top = y; int wh = by + bh - foot - y; if (bw != w->width || wh != w->height) w->resizeTo (bw, wh > 1 ? wh : 1); }
	}
	if (e->footer)
	{
		Widget *w = e->footer;
		w->hidden = !shown;
		if (!w->hidden) { w->left = bx; w->top = by + bh - foot; if (bw != w->width || foot != w->height) w->resizeTo (bw, foot > 1 ? foot : 1); }
	}
	if (e->toggle) e->toggle->hidden = !(overlay (pres));
	lytW = width; lytH = height;
	invalidate (true);
}

// The edge's tab: its rounded corners cut out sharp (no anti-aliasing against the see-through key: no fringe).
static void sp_tab (Canvas &cv, unsigned f, unsigned corners)
{
	const int R = 7;
	for (int y = 0; y < cv.h; y++)
		for (int x = 0; x < cv.w; x++)
		{
			int cx = -1, cy = -1;
			if ((corners & UK_TL) && x < R && y < R) { cx = R; cy = R; }
			else if ((corners & UK_TR) && x >= cv.w - R && y < R) { cx = cv.w - R - 1; cy = R; }
			else if ((corners & UK_BL) && x < R && y >= cv.h - R) { cx = R; cy = cv.h - R - 1; }
			else if ((corners & UK_BR) && x >= cv.w - R && y >= cv.h - R) { cx = cv.w - R - 1; cy = cv.h - R - 1; }
			bool in = cx < 0 || (x - cx) * (x - cx) + (y - cy) * (y - cy) <= R * R;
			if (in) cv.px[(long) y * cv.stride + x] = f;
		}
}

// ---- drawing ---------------------------------------------------------------------------------------------------------------
void SidePanel::onDraw ()
{
	SpExt *e = sp (this);
	sp_sync (this, e);
	int pres = presentation ();
	if (e->told != pres) { layout (); internal::redraw_soon (this); }	// (placed while drawn: the window drawn again)
	sp_subs (this, e, pres, m_side);
	unsigned bg = sp_bg (e);
	if (overlay (pres) && !e->open)					// closed: the edge's tab
	{
		canvas.clear (UK_TRANSPARENT_KEY);
		if (width < 4 || height < 4) return;
		unsigned f = console () ? 0x00243A66 : uk_tone (bg, 96);
		unsigned ink = console () ? (unsigned) CON_GLOW : uk_ink_for (f);
		bool hot = e->hotTab == 1;
		if (hot) f = uk_mix (f, C_ACCENT, 80);
		if (pres == UK_SP_SHEET)
		{
			sp_tab (canvas, f, UK_TL | UK_TR);
			uk_rbox (canvas, width / 2 - 16, 6, 32, 4, 2, ink, ink);
		}
		else
		{
			bool right = m_side == UK_SP_RIGHT;
			sp_tab (canvas, f, right ? UK_TL | UK_BL : UK_TR | UK_BR);
			if (m_role == UK_SP_NAVIGATION) uk_glyph (canvas, WKG_MENU, width / 2, height / 2, 12, ink);
			else uk_glyph (canvas, right ? WKG_CHEV_LEFT : WKG_CHEV_RIGHT, width / 2, height / 2, 9, ink);
		}
		return;
	}
	int bx, by, bw, bh;
	sp_box (this, e, pres, m_side, &bx, &by, &bw, &bh);
	if (overlay (pres) && e->open)					// the content dimmed behind the panel
	{
		for (int y = 0; y < height; y++)
			for (int x = 0; x < width; x++)
				canvas.px[(long) y * canvas.stride + x] = e->scrim && x < e->scrimW && y < e->scrimH ? e->scrim[(long) y * e->scrimW + x] : 0x00303438;
		if (pres == UK_SP_SHEET)
		{
			uk_rbox (canvas, bx, by, bw, bh + 12, 12, bg, bg, 255, UK_TL | UK_TR);
			uk_rbox (canvas, bx + bw / 2 - 20, by + 7, 40, 5, 2, uk_mix (bg, sp_ink (e), 90), uk_mix (bg, sp_ink (e), 90));
			by += 18; bh -= 18;
		}
		else
		{
			canvas.fillRect (bx, by, bw, bh, bg);
			int sx = m_side == UK_SP_RIGHT ? bx - 1 : bx + bw;	// (its shadow on the content)
			for (int k = 0; k < 6; k++) for (int y = by; y < by + bh; y++)
			{
				int x = m_side == UK_SP_RIGHT ? sx - k : sx + k;
				if (x >= 0 && x < width) canvas.px[(long) y * canvas.stride + x] = uk_mix (canvas.px[(long) y * canvas.stride + x] & 0x00FFFFFF, 0, 60 - k * 10);
			}
		}
	}
	else
	{
		canvas.clear (bg);
		unsigned ed = e->edge != UK_AUTO ? e->edge : console () ? 0x00304870 : uk_tone (C_BG, 100);
		if (m_side == UK_SP_LEFT) canvas.fillRect (width - 1, 0, 1, height, ed); else canvas.fillRect (0, 0, 1, height, ed);
		if (pres == UK_SP_RAIL && e->open)			// (the rail expanded over the content: its shadow)
			for (int k = 1; k <= 4; k++) canvas.fillRect (m_side == UK_SP_LEFT ? width - 1 - k : k, 0, 1, height, uk_mix (bg, 0, 40 - k * 8));
	}
	{ int hh = sp_head (e, pres); by += hh; bh -= hh; }		// (the header: a child, drawn by itself)
	if (e->npages >= 2)						// the pages' tabs
	{
		int n = e->npages, tw = (bw - 16) / n;
		uk_rbox (canvas, bx + 8, by + 4, bw - 16, HDR_H - 8, (HDR_H - 8) / 2, uk_tone (bg, 92), uk_tone (bg, 100));
		for (int k = 0; k < n; k++)
		{
			bool on = k == e->page;
			int x = bx + 10 + k * tw;
			if (on) uk_rbox (canvas, x, by + 6, tw - 4, HDR_H - 12, (HDR_H - 12) / 2, console () ? 0x002E5FA8 : C_FIELD, console () ? 0x00224A88 : C_FIELD);
			char b[32]; uk_text_fit (e->pages[k].label, tw - 10, b, sizeof b, on ? 2 : 0);
			uk_text_c (canvas, x, by + 6, tw - 4, HDR_H - 12, b, console () ? 0x00F2F6FF : on ? C_FIELD_TEXT : sp_ink (e), on ? 2 : 0);
		}
	}
	if (e->npages == 0 && e->n > 0)
	{
		int ih = bh - e->footerH;
		sp_draw_items (this, e, pres, bx, by, bw, ih);
		int ry[512], rh[512];
		int total = sp_rows (e, pres, 0, ry, rh) + 8;
		if (total > ih && !(e->content))			// (more items than room: an overlay bar)
		{
			UkThumb t = uk_thumb (total, ih, e->top, ih - 8);
			if (t.show) uk_scroll_bar (canvas, bx + bw - UK_SBW - 2, by + 4, UK_SBW, ih - 8, true, t.y, t.h, bg, UK_NORMAL);
		}
	}
}

// ---- the pointer, the keys ---------------------------------------------------------------------------------------------------
static int sp_item_at (SidePanel *p, SpExt *e, int pres, int side, int mx, int my)
{
	int bx, by, bw, bh;
	sp_box (p, e, pres, side, &bx, &by, &bw, &bh);
	if (pres == UK_SP_SHEET && e->open) { by += 18; bh -= 18; }
	{ int hh = sp_head (e, pres); by += hh; bh -= hh; }
	if (e->npages > 0 || mx < bx || mx >= bx + bw || my < by || my >= by + bh - e->footerH) return -1;
	int ry[512], rh[512];
	int n = e->n < 512 ? e->n : 512;
	sp_rows (e, pres, by - e->top, ry, rh);
	for (int i = 0; i < n; i++) if (ry[i] >= 0 && my >= ry[i] && my < ry[i] + rh[i]) return i;
	return -1;
}

int SidePanel::itemAt (int x, int y)
{
	SpExt *e = sp (this);
	sp_subs (this, e, presentation (), m_side);
	int i = sp_item_at (this, e, presentation (), m_side, x, y);
	return i >= 0 && !sp_heading (e->items[i]) ? e->items[i].id : -1;
}

bool SidePanel::dropAt (int x, int y, int type, const char *data, int len)
{
	int id = itemAt (x, y);
	return id >= 0 && onDrop && onDrop (*this, id, type, data, len);
}

static void sp_choose (SidePanel *p, SpExt *e, int pres, int i)
{
	SpItem &it = e->items[i];
	if (it.flags & UK_SPI_HEADING)
	{
		if (it.flags & UK_SPI_FOLDABLE) { it.flags ^= UK_SPI_FOLDED; p->invalidate (true); }
		return;
	}
	if (!sp_pickable (it)) return;
	int id = it.id;
	e->cursor = i;
	if (overlay (pres) || pres == UK_SP_RAIL) { if (pres == UK_SP_RAIL) e->noReopen = true; p->open (false); }	// (a place chosen: the drawer, the rail close)
	p->select (id, false);
	if (p->onSelect) p->onSelect (*p, id);
}

bool SidePanel::onMouse (int mx, int my, int bl, int br, int, int wheel)
{
	SpExt *e = sp (this);
	sp_sync (this, e);
	int pres = presentation ();
	sp_subs (this, e, pres, m_side);
	bool in = mx >= 0 && my >= 0 && mx < width && my < height;
	bool press = bl && !e->bl, release = !bl && e->bl, rpress = br && !e->br;
	e->bl = bl != 0; e->br = br != 0;
	if (overlay (pres) && !e->open)					// the edge's tab
	{
		int h = in ? 1 : 0;
		if (h != e->hotTab) { e->hotTab = h; invalidate (true); }
		if (release && in) open (true);
		return in;
	}
	if (pres == UK_SP_RAIL)						// the rail: expanded while the pointer is over it
	{
		if (!in) e->noReopen = false;
		if (in && !e->open && !bl && !e->noReopen) open (true);
		else if (!in && e->open && !bl && !e->dragOn) { open (false); return false; }
	}
	int bx, by, bw, bh;
	sp_box (this, e, pres, m_side, &bx, &by, &bw, &bh);
	if (overlay (pres) && e->open)
	{
		bool inBox = mx >= bx && mx < bx + bw && my >= by && my < by + bh;
		if (pres == UK_SP_SHEET && inBox && my < by + 18 && release) { e->sheetFull = !e->sheetFull; layout (); invalidate (true); return true; }
		if (!inBox) { if (release && in) open (false); return in; }	// (the dimmed content: closed)
	}
	{ int hh = sp_head (e, pres); by += hh; bh -= hh; }
	if (e->npages >= 2 && my >= by && my < by + HDR_H && mx >= bx && mx < bx + bw)	// the pages' tabs
	{
		if (release) { int k = (mx - bx - 10) * e->npages / (bw - 16 > 1 ? bw - 16 : 1); if (k >= 0 && k < e->npages) setPage (k); }
		return true;
	}
	if (e->npages > 0) return in;
	int i = in ? sp_item_at (this, e, pres, m_side, mx, my) : -1;
	if (i != e->hot) { e->hot = i; invalidate (true); }
	if (wheel && in)
	{
		int ry[512], rh[512];
		int total = sp_rows (e, pres, 0, ry, rh) + 8, room = bh - e->footerH;
		int t = e->top - wheel * 30, most = total - room;
		if (t > most) t = most;
		if (t < 0) t = 0;
		if (t != e->top) { e->top = t; invalidate (true); }
		return true;
	}
	if (rpress && i >= 0 && sp_pickable (e->items[i]) && onItemMenu)
	{
		int ax, ay; internal::abs_pos (this, &ax, &ay);
		select (e->items[i].id, false);
		onItemMenu (*this, e->items[i].id, ax + mx, ay + my);
		e->br = false;
		return true;
	}
	if (press && i >= 0)
	{
		e->dragIdx = sp_pickable (e->items[i]) ? i : -1; e->dragY0 = my; e->dragOn = false;
		catchOutside = true;
		if (e->items[i].toggle >= 0 && mx < bx + 8 + 10 + e->items[i].level * 14 + 24 && onToggle)	// its toggle
		{
			e->items[i].toggle = !e->items[i].toggle;
			e->dragIdx = -1;
			invalidate (true);
			onToggle (*this, e->items[i].id, e->items[i].toggle);
			return true;
		}
		return true;
	}
	if (bl && e->dragIdx >= 0 && onReorder && (my - e->dragY0 > 6 || e->dragY0 - my > 6))	// an item dragged
	{
		e->dragOn = true;
		int ry[512], rh[512];
		int n = e->n < 512 ? e->n : 512;
		sp_rows (e, pres, by - e->top, ry, rh);
		int b = n;
		for (int k = 0; k < n; k++) if (ry[k] >= 0 && my < ry[k] + rh[k] / 2) { b = k; break; }
		if (b != e->dropBefore) { e->dropBefore = b; invalidate (true); }
		return true;
	}
	if (release)
	{
		catchOutside = false;
		if (e->dragOn)
		{
			int id = e->items[e->dragIdx].id, before = e->dropBefore < e->n ? e->items[e->dropBefore].id : -1;
			e->dragOn = false; e->dragIdx = -1; e->dropBefore = -1;
			if (before != id) onReorder (*this, id, before);
			invalidate (true);
			return true;
		}
		e->dragIdx = -1;
		if (i >= 0) { if (internal::ring_on ()) setFocus (); sp_choose (this, e, pres, i); }
	}
	return in;
}

bool SidePanel::onKey (long k)
{
	SpExt *e = sp (this);
	int pres = presentation ();
	if (k == 27 && e->open) { open (false); return true; }
	if (e->npages > 0)
	{
		if ((k == KEY_LEFT || k == KEY_RIGHT) && e->npages >= 2) { setPage ((e->page + (k == KEY_LEFT ? e->npages - 1 : 1)) % e->npages); return true; }
		return false;
	}
	if (k == KEY_UP || k == KEY_DOWN)
	{
		int d = k == KEY_DOWN ? 1 : -1, i = e->cursor >= 0 ? e->cursor : sp_index (e, e->sel);
		for (int s = i + d; s >= 0 && s < e->n; s += d)
			if (sp_pickable (e->items[s])) { e->cursor = s; e->hot = s; invalidate (true); return true; }
		return true;
	}
	if ((k == KEY_ENTER || k == ' ') && e->cursor >= 0 && e->cursor < e->n) { sp_choose (this, e, pres, e->cursor); return true; }
	return false;
}

} // namespace uikit
