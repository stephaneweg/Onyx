//
// designer.h -- QBStudio's designer: the form's window drawn as Onyx draws it (its controls are uikit's own widgets, at
// the places the layout gives them), its layout's boxes over it (the containers dashed and named), the selection
// and its handles; a click chooses an element, a drag moves it -- to another place of its container or into another
// --, the toolbox's controls are dragged in (a line shows where they go: no x, no y to give), the window's corner and
// a control's edges are dragged to size them. The toolbox: the elements, by group (layout, controls, the window's
// parts). Every change is made to the form's tree, then told to the app (g_onFormEdited), which writes the text.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _qbstudio_designer_h
#define _qbstudio_designer_h

#include "uikit/uikit.h"
#include "form.h"

namespace qs {
using namespace uikit;

static void (*g_onSelect) (El *e);			// an element chosen (0: none)
static void (*g_onFormEdited) ();			// the tree changed: the app writes the text again
static void (*g_onElementOpen) (El *e);		// a double click: its event's SUB
static void (*g_onDesignHint) (const char *s);		// a line for the status bar

static const unsigned C_BLUEPRINT = 0x003C8CD8, C_DROP = 0x00E0307A;
// The grid: dots every GRID px in the window (View > Grid); the sizes dragged (a control's width and height, the
// window's) are multiples of it and a Canvas's children are placed on it (View > Snap to Grid; Alt held: not)
static const int GRID = 5;
static bool g_showGrid = true, g_snap = true;
static inline int snap (int v) { return g_snap && !(kapi_get_modifiers () & MOD_ALT) ? (v + GRID / 2) / GRID * GRID : v; }

// ---- elements: made, named, found ------------------------------------------------------------------------------------
static const char *default_base (int k)
{
	switch (k)
	{
	case K_BUTTON: return "button"; case K_TEXTBOX: return "text"; case K_CHECKBOX: return "check"; case K_LISTBOX: return "list";
	case K_DROPDOWN: return "choice"; case K_SLIDER: return "slider"; case K_PROGRESS: return "progress"; case K_STATUSBAR: return "status"; case K_HOST: return "host";
	default: return 0;
	}
}
static void unique_name (Form &f, const char *base, char *out, int cap)
{
	for (int i = 1; i < 1000; i++) { snprintf (out, cap, "%s%d", base, i); if (!f.named (out)) return; }
}
static El *new_element (Form &f, int kind)
{
	El *e = new El; e->kind = kind;
	const char *b = default_base (kind);
	if (b) unique_name (f, b, e->name, sizeof e->name);
	switch (kind)
	{
	case K_LABEL: cpy (e->text, "Label", sizeof e->text); e->hasText = true; break;
	case K_BUTTON: cpy (e->text, "Button", sizeof e->text); e->hasText = true; break;
	case K_TEXTBOX: e->hasText = true; break;
	case K_CHECKBOX: cpy (e->text, "CheckBox", sizeof e->text); e->hasText = true; break;
	case K_LISTBOX: case K_DROPDOWN: e->set ("items", "One|Two|Three"); break;
	case K_SLIDER: e->set ("max", "100"); break;
	case K_PROGRESS: e->set ("value", "40"); break;
	case K_STATUSBAR: cpy (e->text, "Ready", sizeof e->text); e->hasText = true; break;
	case K_GROUP: cpy (e->text, "Group", sizeof e->text); e->hasText = true; break;
	case K_ROW: case K_COLUMN: e->set ("gap", "8"); break;
	case K_GRID: e->set ("cols", "2"); e->set ("gap", "8"); break;
	case K_CANVAS: e->set ("size", "200x120"); break;
	case K_HOST: e->setFlag ("fill", true); e->set ("grow", "1"); break;
	case K_MENU:
	{
		El *t = new El; t->kind = K_MENUTITLE; cpy (t->text, "&File", sizeof t->text); t->hasText = true; e->add (t);
		El *q = new El; q->kind = K_MENUITEM; cpy (q->text, "&Quit", sizeof q->text); q->hasText = true; q->set ("key", "Ctrl+Q");
		char n[32]; unique_name (f, "mnuQuit", n, sizeof n); cpy (q->name, n, sizeof q->name);
		t->add (q);
		break;
	}
	}
	return e;
}
static bool is_inside (const El *e, const El *anc) { for (; e; e = e->parent) if (e == anc) return true; return false; }
// An element's path (the indexes from the window down) and back
static int path_of (const El *e, int *out, int cap)
{
	int tmp[32], n = 0;
	for (; e && e->parent && n < 32; e = e->parent) tmp[n++] = e->index ();
	int m = 0; while (n && m < cap) out[m++] = tmp[--n];
	return m;
}
static El *el_by_path (const Form &f, const int *p, int n)
{
	El *e = f.root;
	for (int i = 0; e && i < n; i++) e = p[i] >= 0 && p[i] < e->kids.n ? e->kids[p[i]] : 0;
	return e;
}
static El *el_at_line (El *e, int line)
{
	if (!e) return 0;
	if (e->line == line && e->kind != K_COMMENT) return e;
	for (int i = 0; i < e->kids.n; i++) { El *r = el_at_line (e->kids[i], line); if (r) return r; }
	return 0;
}
static El *child_of_kind (El *w, int k) { if (w) for (int i = 0; i < w->kids.n; i++) if (w->kids[i]->kind == k) return w->kids[i]; return 0; }
// A menu's title without its '&'
static void plain (const char *s, char *o, int cap) { int k = 0; for (; *s && k < cap - 1; s++) if (*s != '&') o[k++] = *s; o[k] = 0; }

// ---- the window's parts drawn by the designer: a menu's strip, a toolbar, a status bar --------------------------------
static void dashed_rect (Canvas &cv, int x, int y, int w, int h, unsigned c);
class Strip : public Widget
{
public:
	El *e;
	Strip (El *el) : Widget (el->x, el->y, imax (1, el->w), imax (1, el->h)), e (el) {}
	void onDraw () override
	{
		canvas.clear (e->kind == K_STATUSBAR ? uk_mix (C_BG, C_FACE, 128) : C_BG);
		if (e->kind == K_MENU)
		{
			canvas.fillRect (0, height - 1, width, 1, uk_mix (C_BG, C_TEXT, 40));
			int x = 10;
			for (int i = 0; i < e->kids.n; i++)
			{
				if (e->kids[i]->kind != K_MENUTITLE) continue;
				char t[64]; plain (e->kids[i]->text, t, sizeof t);
				uk_text (canvas, x, (height - uk_fh ()) / 2, t, C_TEXT);
				x += uk_tw (t) + 18;
			}
		}
		else if (e->kind == K_HOST)			// the area a user control is shown in: its frame, what it shows
		{
			dashed_rect (canvas, 0, 0, width, height, uk_mix (C_BG, C_TEXT, 110));
			char t[96]; const char *c = e->get ("content");
			if (c && c[0]) snprintf (t, sizeof t, "%s: %s", e->name[0] ? e->name : "Host", c); else snprintf (t, sizeof t, "%s (a user control goes here)", e->name[0] ? e->name : "Host");
			if (uk_tw (t) + 12 > width) cpy (t, e->name[0] ? e->name : "Host", sizeof t);
			uk_text (canvas, imax (4, (width - uk_tw (t)) / 2), imax (2, (height - uk_fh ()) / 2), t, uk_mix (C_BG, C_TEXT, 150));
		}
		else if (e->kind == K_TOOLBAR) uk_etch_h (canvas, 0, height - 2, width, C_BG);
		else if (e->kind == K_STATUSBAR)
		{
			uk_etch_h (canvas, 0, 0, width, C_BG);
			uk_text (canvas, 8, (height - uk_fh ()) / 2 + 1, e->text, C_TEXT);
		}
	}
};

// The window's client area in the designer: its background, the grid's dots under the controls
class GridPanel : public Panel
{
public:
	GridPanel (int l, int t, int w, int h) : Panel (l, t, w, h, C_BG) {}
	void onDraw () override
	{
		canvas.clear (C_BG);
		if (!g_showGrid) return;
		unsigned dot = uk_mix (C_BG, C_TEXT, 38), strong = uk_mix (C_BG, C_TEXT, 80);
		for (int y = 0; y < height; y += GRID)
			for (int x = 0; x < width; x += GRID)
				canvas.fillRect (x, y, 1, 1, x % (GRID * 10) == 0 && y % (GRID * 10) == 0 ? strong : dot);
	}
};

// ---- the designer ---------------------------------------------------------------------------------------------------------
class Designer;
class Overlay : public Widget
{
public:
	Designer *d;
	Overlay (Designer *dd, int w, int h) : Widget (0, 0, w, h), d (dd) { transparent = true; canFocus = true; }
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
};

class Designer : public Widget
{
public:
	Form *form;
	El *sel;
	int ox, oy;					// the window's client area, in the designer
	int winW, winH;					// its size (the form's, without the menu)
	int sx, sy;					// the scroll
	// a drag
	enum { DR_NONE, DR_PRESS, DR_MOVE, DR_NEW, DR_SIZE_WIN, DR_SIZE_W, DR_SIZE_H };
	int drag, newKind, px, py, startW, startH, startX, startY, grabX, grabY;
	El *dropPar; int dropIdx; int dropLine[4]; bool dropOk; int dropAtX, dropAtY;
	int hoverX, hoverY;
	Panel *client;
	Overlay *ov;
	Vec<char *> pool;				// the drop-downs' options (alive while their widgets are)
	Vec<const char **> optsPool;
	unsigned lastClick;

	Designer (int l, int t, int w, int h) : Widget (l, t, w, h), form (0), sel (0), ox (0), oy (0), winW (0), winH (0), sx (0), sy (0),
		drag (DR_NONE), newKind (-1), px (0), py (0), startW (0), startH (0), startX (0), startY (0), grabX (0), grabY (0), dropPar (0), dropIdx (0), dropOk (false), dropAtX (0), dropAtY (0),
		hoverX (-1), hoverY (-1), client (0), lastClick (0)
	{
		ov = new Overlay (this, w, h); addChild (ov);
	}
	~Designer () { freePool (); }
	void freePool () { for (int i = 0; i < pool.n; i++) free (pool[i]); pool.clear (); for (int i = 0; i < optsPool.n; i++) delete[] optsPool[i]; optsPool.clear (); }
	void resizeTo (int w, int h) override { Widget::resizeTo (w, h); ov->resizeTo (w, h); rebuild (); }
	int menuH () const { return form && form->root && child_of_kind (form->root, K_MENU) ? MENU_H : 0; }
	static const int TITLE_H = 28;

	// The form shown again (a new tree, a change): laid out, its widgets made again
	void setForm (Form *f) { form = f; sel = 0; rebuild (); }
	void rebuild ()
	{
		if (client) { removeChild (client); delete client; client = 0; }
		freePool ();
		if (!form || !form->root) { invalidate (true); return; }
		form_size (*form, &winW, &winH);
		int mh = menuH ();
		form_layout (*form, winW, winH + mh, true);
		ox = imax (24, (width - winW) / 2) - sx; oy = 24 + TITLE_H - sy;
		if (width - winW < 48) ox = 24 - sx;
		client = new GridPanel (ox, oy, winW, winH + mh);
		make (form->root);
		removeChild (ov); addChild (client); addChild (ov);
		redraw ();
		invalidate (true);
	}
	void make (El *e)
	{
		Widget *w = 0;
		int x = e->x, y = e->y, ww = imax (1, e->w), hh = imax (1, e->h);
		switch (e->kind)
		{
		case K_LABEL: w = new Label (x, y, ww, hh, e->text, C_TEXT, C_BG); break;
		case K_BUTTON: w = new Button (x, y, ww, hh, e->text); break;
		case K_TEXTBOX: w = new Textbox (x, y, ww, hh, e->text); break;
		case K_CHECKBOX: w = new Checkbox (x, y, ww, hh, e->text, e->flag ("checked"), 0, C_BG); break;
		case K_LISTBOX:
		{
			ListBox *l = new ListBox (x, y, ww, hh); w = l;
			const char *it = e->get ("items");
			if (it) { char t[64]; int k = 0; for (const char *p = it; ; p++) { if (*p == '|' || !*p) { t[k] = 0; l->add (t); k = 0; if (!*p) break; } else if (k < 63) t[k++] = *p; } }
			break;
		}
		case K_DROPDOWN:
		{
			const char *it = e->get ("items"); if (!it) it = e->text;
			char *c = strdup (it); pool.push (c);
			int n = 1; for (char *p = c; *p; p++) if (*p == '|') n++;
			const char **o = new const char *[n]; optsPool.push (o);
			int k = 0; o[k++] = c; for (char *p = c; *p; p++) if (*p == '|') { *p = 0; o[k++] = p + 1; }
			w = new Dropdown (x, y, ww, hh, o, n, 0, 0);
			break;
		}
		case K_SLIDER: w = new Slider (x, y, ww, hh, 0, imax (1, e->num ("max", 100)), e->num ("value", 0), 0, C_BG); break;
		case K_PROGRESS: w = new Progress (x, y, ww, hh, 0, 100, e->num ("value", 0)); break;
		case K_GROUP: w = new GroupBox (x, y, ww, hh, e->text, C_BG); break;
		case K_MENU: case K_TOOLBAR: case K_STATUSBAR: case K_HOST: w = new Strip (e); break;
		default: break;
		}
		if (w) { if (e->flag ("disabled")) w->disabled = true; client->addChild (w); }
		if (e->kind != K_MENU) for (int i = 0; i < e->kids.n; i++) make (e->kids[i]);
	}
	void redraw () { invalidate (true); ov->invalidate (true); }
	void select (El *e, bool tell = true) { sel = e; redraw (); if (tell && g_onSelect) g_onSelect (e); }

	void onDraw () override
	{
		unsigned bg = uk_mix (C_BG, C_TEXT, 20);
		canvas.clear (bg);
		for (int y = 8; y < height; y += 16) for (int x = 8; x < width; x += 16) canvas.fillRect (x, y, 1, 1, uk_mix (bg, C_TEXT, 50));
		if (!form || !form->root) { uk_text_c (canvas, 0, 0, width, height, "The form has errors: see the text", uk_mix (bg, C_TEXT, 150)); return; }
		int W = winW, H = winH + menuH ();
		// the window: its shadow, its frame, its title
		for (int i = 1; i <= 6; i++) uk_rline (canvas, ox - 1 - i, oy - TITLE_H - 1 - i + 3, W + 2 + 2 * i, H + TITLE_H + 2 + 2 * i, 8 + i, 0, 30 - i * 4);
		canvas.fillRect (ox - 1, oy - TITLE_H - 1, W + 2, H + TITLE_H + 2, uk_mix (C_BG, C_TEXT, 90));
		const char *t = form->root->hasText ? form->root->text : form->root->name;
		uk_title_strip (canvas, ox, oy - TITLE_H, W, TITLE_H, t, 0);
		char sz[32]; snprintf (sz, sizeof sz, "%d x %d", winW, winH);
		uk_text (canvas, ox + W - uk_tw (sz), oy + H + 10, sz, uk_mix (bg, C_TEXT, 150));
	}

	// ---- finding ---------------------------------------------------------------------------------------------------
	// The deepest element at a point of the client area
	El *hit (El *e, int cx, int cy)
	{
		if (e->kind == K_COMMENT || e->kind == K_MENUTITLE || e->kind == K_MENUITEM || e->kind == K_SEP) return 0;
		bool in = cx >= e->x && cy >= e->y && cx < e->x + e->w && cy < e->y + e->h;
		if (!in && e->kind != K_WINDOW) return 0;
		for (int i = e->kids.n - 1; i >= 0; i--) { El *r = hit (e->kids[i], cx, cy); if (r) return r; }
		return in ? e : 0;
	}
	// The boxes for the designer (the generator lays the form out at other sizes too)
	void relay () { if (form && form->root) form_layout (*form, winW, winH + menuH (), true); }
	El *at (int mx, int my)
	{
		if (!form || !form->root) return 0;
		relay ();
		int cx = mx - ox, cy = my - oy;
		if (cx >= 0 && cy >= -TITLE_H && cx < winW && cy < 0) return form->root;	// (the title)
		if (cx < 0 || cy < 0 || cx >= winW || cy >= winH + menuH ()) return 0;
		return hit (form->root, cx, cy);
	}
	// Where something dropped at (mx, my) goes: its container, its index there, the line that shows it
	bool findDrop (int mx, int my, El *moving, int kind)
	{
		dropOk = false; dropPar = 0;
		if (!form || !form->root) return false;
		relay ();
		El *win = form->root;
		int cx = mx - ox, cy = my - oy;
		if (cx < 0 || cy < 0 || cx >= winW || cy >= winH + menuH ()) return false;
		if (kind == K_MENU || kind == K_TOOLBAR || kind == K_STATUSBAR)
		{
			if (child_of_kind (win, kind) && (!moving || moving->kind != kind)) return false;
			dropPar = win;
			dropIdx = kind == K_STATUSBAR ? win->kids.n : kind == K_MENU ? 0 : (child_of_kind (win, K_MENU) ? child_of_kind (win, K_MENU)->index () + 1 : 0);
			int y = kind == K_STATUSBAR ? winH + menuH () - 2 : kind == K_MENU ? 0 : menuH ();
			dropLine[0] = 0; dropLine[1] = y; dropLine[2] = winW; dropLine[3] = 2;
			dropOk = true; return true;
		}
		// the deepest container under the point
		El *c = hit (win, cx, cy);
		while (c && (!is_container (c->kind) || (moving && is_inside (c, moving)))) c = c->parent;
		if (!c) return false;
		if (c->kind == K_TOOLBAR && kind != K_BUTTON) c = win;
		dropPar = c;
		int pad = pad_of (c);
		if (c->kind == K_CANVAS)
		{
			dropIdx = c->kids.n; dropAtX = imax (0, snap (cx - c->x - (moving ? grabX : 0))); dropAtY = imax (0, snap (cy - c->y - (moving ? grabY : 0)));
			dropLine[0] = cx; dropLine[1] = cy; dropLine[2] = 8; dropLine[3] = 8;
			dropOk = true; return true;
		}
		bool across = c->kind == K_ROW || c->kind == K_TOOLBAR;
		bool grid = c->kind == K_GRID;
		dropIdx = c->kids.n;
		El *before = 0, *after = 0;
		for (int i = 0; i < c->kids.n; i++)
		{
			El *k = c->kids[i];
			if (k == moving || k->kind == K_COMMENT) continue;
			if (c == win && (k->kind == K_MENU || k->kind == K_TOOLBAR)) { after = k; continue; }
			bool goesBefore;
			if (c == win && k->kind == K_STATUSBAR) goesBefore = true;
			else if (across) goesBefore = cx < k->x + k->w / 2;
			else if (grid) goesBefore = cy < k->y || (cy < k->y + k->h && cx < k->x + k->w / 2);
			else goesBefore = cy < k->y + k->h / 2;
			if (goesBefore) { dropIdx = i; before = k; break; }
			after = k;
		}
		// the line
		if (across || grid)
		{
			int x = before ? before->x - 4 : after ? after->x + after->w + 2 : c->x + pad;
			int y = before ? before->y : after ? after->y : c->y + pad, h = before ? before->h : after ? after->h : imax (20, c->h - 2 * pad);
			dropLine[0] = x; dropLine[1] = y; dropLine[2] = 2; dropLine[3] = h;
		}
		else
		{
			int y = before ? before->y - 4 : after ? after->y + after->h + 2 : c->y + pad + (c->kind == K_GROUP ? 18 : 0);
			if (c == win && !before && !after) y = menuH () + 4;
			dropLine[0] = c->x + pad; dropLine[1] = y; dropLine[2] = imax (10, c->w - 2 * pad); dropLine[3] = 2;
		}
		dropOk = true; return true;
	}
	// The element put where findDrop said (moving: taken from its place first)
	void doDrop (El *e, bool moving)
	{
		if (!dropOk || !dropPar) return;
		if (moving)
		{
			El *op = e->parent; int oi = e->index ();
			if (op) { op->kids.erase (oi); if (op == dropPar && oi < dropIdx) dropIdx--; }
		}
		if (dropPar->kind == K_CANVAS) { char t[24]; snprintf (t, sizeof t, "%d,%d", dropAtX, dropAtY); e->set ("at", t); }
		else e->set ("at", 0);
		if (dropPar->kind != K_GRID) e->set ("cell", 0);
		dropPar->add (e, dropIdx);
		sel = e;
		if (g_onDesignHint) { char h[120]; snprintf (h, sizeof h, "%s %s %s", KIND_NAMES[e->kind], e->name, moving ? "moved" : "added"); g_onDesignHint (h); }
		if (g_onFormEdited) g_onFormEdited ();
	}
	void removeSelected ()
	{
		if (!sel || sel == form->root || !sel->parent) return;
		El *p = sel->parent; p->kids.erase (sel->index ()); delete sel;
		sel = p;
		if (g_onFormEdited) g_onFormEdited ();
	}
	// From the toolbox: a drag over the designer (mx, my in the designer's coordinates; release: dropped)
	void toolDrag (int kind, int mx, int my, bool release)
	{
		newKind = kind;
		bool ok = findDrop (mx, my, 0, kind);
		hoverX = mx; hoverY = my;
		if (release)
		{
			newKind = -1; hoverX = -1;
			if (ok) { El *e = new_element (*form, kind); doDrop (e, false); }
			dropOk = false;
		}
		else if (g_onDesignHint)
		{
			char h[120];
			if (ok) snprintf (h, sizeof h, "Dropped here, the %s goes into the %s (place %d): no x, no y to give", KIND_NAMES[kind], KIND_NAMES[dropPar->kind], dropIdx + 1);
			else snprintf (h, sizeof h, "Drop it into the window");
			g_onDesignHint (h);
		}
		redraw ();
	}
	void toolLeave () { newKind = -1; hoverX = -1; dropOk = false; redraw (); }
	// A double click in the toolbox: added after the selection (or into the chosen container)
	void toolAdd (int kind)
	{
		if (!form || !form->root) return;
		El *win = form->root;
		if (kind == K_MENU || kind == K_TOOLBAR || kind == K_STATUSBAR)
		{
			if (child_of_kind (win, kind)) return;
			findDrop (ox + 2, oy + 2, 0, kind);
			El *e = new_element (*form, kind); doDrop (e, false); return;
		}
		El *s = sel ? sel : win;
		El *par; int idx;
		if (is_container (s->kind) && s->kind != K_TOOLBAR) { par = s; idx = s->kids.n; if (s == win) { El *sb = child_of_kind (win, K_STATUSBAR); if (sb) idx = sb->index (); } }
		else { par = s->parent ? s->parent : win; idx = s->index () + 1; }
		if (par == win) { El *body = 0; for (int i = 0; i < win->kids.n; i++) if (is_container (win->kids[i]->kind) && win->kids[i]->kind != K_TOOLBAR) body = win->kids[i]; if (body && s == win) { par = body; idx = body->kids.n; } }
		dropPar = par; dropIdx = idx; dropOk = true; dropAtX = 8; dropAtY = 8;
		El *e = new_element (*form, kind); doDrop (e, false);
	}
};

// ---- the layer over the window: the boxes, the selection, the drag --------------------------------------------------
static void dashed_rect (Canvas &cv, int x, int y, int w, int h, unsigned c)
{
	for (int i = 0; i < w; i += 6) { int l = imin (3, w - i); cv.fillRect (x + i, y, l, 1, c); cv.fillRect (x + i, y + h - 1, l, 1, c); }
	for (int i = 0; i < h; i += 6) { int l = imin (3, h - i); cv.fillRect (x, y + i, 1, l, c); cv.fillRect (x + w - 1, y + i, 1, l, c); }
}
static void draw_tag (Canvas &cv, int x, int y, const char *s, unsigned bg)
{
	int w = uk_tw (s, 2) + 8, h = uk_fh () - 1;
	cv.fillRect (x, y, w, h, bg);
	uk_text (cv, x + 4, y - 1, s, 0xFFFFFF, 2);
}
static void describe (const El *e, char *o, int cap)
{
	int k = snprintf (o, cap, "%s", KIND_NAMES[e->kind]);
	if (e->name[0]) k += snprintf (o + k, cap > k ? cap - k : 0, " %s", e->name);
	static const char *const P[] = { "padding", "gap", "align", "cols", 0 };
	for (int i = 0; P[i]; i++) { const char *v = e->get (P[i]); if (v && k < cap) k += snprintf (o + k, cap - k, "  %s %s", P[i], v); }
}
static void draw_boxes (Canvas &cv, const El *e, int ox, int oy, const El *sel)
{
	if (e->kind == K_COMMENT || e->kind == K_MENU) return;
	if (is_container (e->kind) && e->kind != K_WINDOW && e->w > 0)
	{
		unsigned c = uk_mix (C_BLUEPRINT, C_BG, 60);
		dashed_rect (cv, ox + e->x, oy + e->y, e->w, e->h, c);
		// its name: the chosen element's container's only (the others would hide the controls)
		if (e != sel && sel && sel->parent == e) { char d[96]; describe (e, d, sizeof d); draw_tag (cv, ox + e->x, oy + e->y - uk_fh () + 1, d, uk_mix (C_BLUEPRINT, C_BG, 70)); }
	}
	if (e->kind == K_SPACER && e->h > 4)
	{
		dashed_rect (cv, ox + e->x + 2, oy + e->y + 2, e->w - 4, e->h - 4, uk_mix (C_BLUEPRINT, C_BG, 120));
		if (e->h > 20 && e == sel) { const char *t = "Spacer: the free room"; int tw = uk_tw (t) + 8; draw_tag (cv, ox + e->x + (e->w - tw) / 2, oy + e->y + (e->h - uk_fh ()) / 2, t, uk_mix (C_BLUEPRINT, C_BG, 90)); }
	}
	for (int i = 0; i < e->kids.n; i++) draw_boxes (cv, e->kids[i], ox, oy, sel);
}
inline void Overlay::onDraw ()
{
	canvas.clear (UK_TRANSPARENT_KEY);
	if (!d->form || !d->form->root) return;
	d->relay ();
	int ox = d->ox, oy = d->oy;
	draw_boxes (canvas, d->form->root, ox, oy, d->sel);
	El *s = d->sel;
	if (s)
	{
		int x, y, w, h;
		if (s == d->form->root) { x = ox; y = oy - Designer::TITLE_H; w = d->winW; h = d->winH + d->menuH () + Designer::TITLE_H; }
		else { x = ox + s->x; y = oy + s->y; w = imax (4, s->w); h = imax (4, s->h); }
		canvas.fillRect (x - 1, y - 1, w + 2, 1, C_BLUEPRINT); canvas.fillRect (x - 1, y + h, w + 2, 1, C_BLUEPRINT);
		canvas.fillRect (x - 1, y - 1, 1, h + 2, C_BLUEPRINT); canvas.fillRect (x + w, y - 1, 1, h + 2, C_BLUEPRINT);
		int hx[8] = { x, x + w / 2, x + w, x + w, x + w, x + w / 2, x, x }, hy[8] = { y, y, y, y + h / 2, y + h, y + h, y + h, y + h / 2 };
		for (int i = 0; i < 8; i++) { canvas.fillRect (hx[i] - 3, hy[i] - 3, 7, 7, C_BLUEPRINT); canvas.fillRect (hx[i] - 2, hy[i] - 2, 5, 5, 0xFFFFFF); }
		char t[96]; describe (s, t, sizeof t);
		int ty = y + h + 4; if (s == d->form->root) ty = y + h + 26;
		draw_tag (canvas, x + w - uk_tw (t, 2) - 8, imin (ty, height - uk_fh ()), t, C_BLUEPRINT);
	}
	if (d->dropOk && (d->drag == Designer::DR_MOVE || d->newKind >= 0))
	{
		int *L = d->dropLine;
		canvas.fillRect (ox + L[0], oy + L[1], L[2], L[3], C_DROP);
		if (L[2] > L[3]) { canvas.fillRect (ox + L[0] - 3, oy + L[1] - 3, 6, 8, C_DROP); canvas.fillRect (ox + L[0] + L[2] - 3, oy + L[1] - 3, 6, 8, C_DROP); }
		else { canvas.fillRect (ox + L[0] - 3, oy + L[1] - 3, 8, 6, C_DROP); canvas.fillRect (ox + L[0] - 3, oy + L[1] + L[3] - 3, 8, 6, C_DROP); }
	}
	if (d->newKind >= 0 && d->hoverX >= 0)
	{
		const char *n = KIND_NAMES[d->newKind];
		int w = uk_tw (n) + 24, h = uk_fh () + 8, x = d->hoverX + 8, y = d->hoverY - h / 2;
		canvas.fillRect (x, y, w, h, uk_mix (C_FIELD, C_BLUEPRINT, 40));
		canvas.fillRect (x, y, w, 1, C_BLUEPRINT); canvas.fillRect (x, y + h - 1, w, 1, C_BLUEPRINT);
		canvas.fillRect (x, y, 1, h, C_BLUEPRINT); canvas.fillRect (x + w - 1, y, 1, h, C_BLUEPRINT);
		uk_text (canvas, x + 12, y + 4, n, C_FIELD_TEXT);
	}
}
inline bool Overlay::onMouse (int mx, int my, int bl, int, int, int wheel)
{
	Designer *D = d;
	if (mx == -1 && my == -1 && !bl && !wheel) return false;		// (the pointer left)
	if (wheel) { if ((kapi_get_modifiers () & MOD_SHIFT)) D->sx = imax (0, D->sx - wheel * 24); else D->sy = imax (0, D->sy - wheel * 24); D->rebuild (); return true; }
	if (!D->form || !D->form->root) return true;
	D->relay ();
	int W = D->winW, H = D->winH + D->menuH ();
	int cx = mx - D->ox, cy = my - D->oy;
	El *s = D->sel;
	bool onCorner = cx >= W - 6 && cx <= W + 8 && cy >= H - 6 && cy <= H + 8;
	bool onRight = s && s != D->form->root && is_control (s->kind) && abs (cx - (s->x + s->w)) <= 4 && cy > s->y && cy < s->y + s->h;
	bool onBottom = s && s != D->form->root && is_control (s->kind) && abs (cy - (s->y + s->h)) <= 4 && cx > s->x && cx < s->x + s->w;
	if (D->drag == Designer::DR_NONE)
	{
		if (onCorner) uk_cursor (KAPI_CURSOR_SIZE_NWSE);
		else if (onRight) uk_cursor (KAPI_CURSOR_SIZE_H);
		else if (onBottom) uk_cursor (KAPI_CURSOR_SIZE_V);
	}
	if (bl && !pressed)
	{
		pressed = true; catchOutside = true; setFocus ();
		D->px = mx; D->py = my;
		if (onCorner) { D->drag = Designer::DR_SIZE_WIN; D->startW = W; D->startH = D->winH; return true; }
		if (onRight) { D->drag = Designer::DR_SIZE_W; D->startW = s->w; D->startX = s->x; return true; }
		if (onBottom) { D->drag = Designer::DR_SIZE_H; D->startH = s->h; D->startY = s->y; return true; }
		El *e = D->at (mx, my);
		unsigned now = kapi_get_ticks ();
		bool dbl = e && e == D->sel && now - D->lastClick < 35;
		D->lastClick = now;
		D->select (e);
		if (e) { D->grabX = cx - e->x; D->grabY = cy - e->y; }
		if (dbl && g_onElementOpen) { D->lastClick = 0; g_onElementOpen (e); return true; }
		D->drag = e && e != D->form->root ? Designer::DR_PRESS : Designer::DR_NONE;
		return true;
	}
	if (bl && pressed)
	{
		int dx = mx - D->px, dy = my - D->py;
		switch (D->drag)
		{
		case Designer::DR_PRESS:
			if (abs (dx) + abs (dy) > 5) D->drag = Designer::DR_MOVE;
			else break;
			/* fall through */
		case Designer::DR_MOVE:
			D->findDrop (mx, my, D->sel, D->sel->kind);
			if (g_onDesignHint) g_onDesignHint (D->dropOk ? "Drop it: it goes where the line is" : "Not here");
			d->redraw ();
			break;
		case Designer::DR_SIZE_WIN:
		{
			char t[24]; snprintf (t, sizeof t, "%dx%d", imax (120, snap (D->startW + dx)), imax (60, snap (D->startH + dy)));
			D->form->root->set ("size", t); D->rebuild ();
			break;
		}
		case Designer::DR_SIZE_W: if (s) { s->setNum ("width", imax (GRID, snap (D->startW + dx))); s->setFlag ("fill", false); D->rebuild (); } break;
		case Designer::DR_SIZE_H: if (s) { s->setNum ("height", imax (GRID, snap (D->startH + dy))); D->rebuild (); } break;
		}
		return true;
	}
	if (!bl && pressed)
	{
		pressed = false; catchOutside = false;
		int dr = D->drag; D->drag = Designer::DR_NONE;
		if (dr == Designer::DR_MOVE && D->sel) { if (D->dropOk) D->doDrop (D->sel, true); D->dropOk = false; d->redraw (); }
		else if (dr == Designer::DR_SIZE_WIN || dr == Designer::DR_SIZE_W || dr == Designer::DR_SIZE_H) { if (g_onFormEdited) g_onFormEdited (); }
		return true;
	}
	return true;
}
inline bool Overlay::onKey (long k)
{
	if (k == KEY_DEL || k == KEY_BACKSPACE) { d->removeSelected (); return true; }
	if (k == 27) { if (d->sel && d->sel->parent) d->select (d->sel->parent); return true; }
	El *s = d->sel;
	if ((k == KEY_LEFT || k == KEY_RIGHT || k == KEY_UP || k == KEY_DOWN) && s && s->parent && s->parent->kind == K_CANVAS)
	{
		int step = (kapi_get_modifiers () & MOD_SHIFT) ? 1 : GRID, x = 0, y = 0;
		s->size2 ("at", &x, &y);
		if (k == KEY_LEFT) x -= step; else if (k == KEY_RIGHT) x += step; else if (k == KEY_UP) y -= step; else y += step;
		if (step > 1) { x = x / GRID * GRID; y = y / GRID * GRID; }
		char t[24]; snprintf (t, sizeof t, "%d,%d", imax (0, x), imax (0, y)); s->set ("at", t);
		if (g_onFormEdited) g_onFormEdited ();
		return true;
	}
	return false;
}

// ---- the toolbox --------------------------------------------------------------------------------------------------------
struct ToolItem { int kind; const char *label; };
static const ToolItem TOOLS[] = {
	{ -1, "Layout" }, { K_COLUMN, "Column" }, { K_ROW, "Row" }, { K_GRID, "Grid" }, { K_GROUP, "Group" }, { K_SPACER, "Spacer" }, { K_CANVAS, "Canvas" },
	{ K_HOST, "Host" },
	{ -1, "Controls" }, { K_LABEL, "Label" }, { K_BUTTON, "Button" }, { K_TEXTBOX, "TextBox" }, { K_CHECKBOX, "CheckBox" }, { K_LISTBOX, "ListBox" },
	{ K_DROPDOWN, "DropDown" }, { K_SLIDER, "Slider" }, { K_PROGRESS, "Progress" },
	{ -1, "Window" }, { K_MENU, "Menu" }, { K_TOOLBAR, "ToolBar" }, { K_STATUSBAR, "StatusBar" },
};
static const int NTOOLS = (int) (sizeof TOOLS / sizeof TOOLS[0]);
// A small picture of each kind
static void tool_icon (Canvas &cv, int kind, int x, int y, unsigned ink, unsigned acc)
{
	unsigned soft = uk_mix (ink, 0xFFFFFF, 150);
	switch (kind)
	{
	case K_COLUMN: for (int i = 0; i < 3; i++) cv.fillRect (x + 1, y + 1 + i * 5, 12, 4, i == 1 ? acc : soft); break;
	case K_ROW: for (int i = 0; i < 3; i++) cv.fillRect (x + 1 + i * 5, y + 1, 4, 12, i == 1 ? acc : soft); break;
	case K_GRID: for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) cv.fillRect (x + 1 + i * 7, y + 1 + j * 7, 5, 5, (i + j) % 2 ? acc : soft); break;
	case K_GROUP: dashed_rect (cv, x + 1, y + 3, 12, 10, ink); cv.fillRect (x + 3, y + 1, 6, 3, acc); break;
	case K_SPACER: cv.fillRect (x + 1, y + 7, 12, 1, ink); cv.fillRect (x + 1, y + 4, 1, 7, ink); cv.fillRect (x + 12, y + 4, 1, 7, ink); break;
	case K_CANVAS: cv.fillRect (x + 1, y + 2, 12, 10, soft); cv.fillRect (x + 3, y + 8, 3, 3, acc); cv.fillRect (x + 8, y + 4, 3, 3, ink); break;
	case K_HOST: dashed_rect (cv, x + 1, y + 2, 12, 10, ink); cv.fillRect (x + 4, y + 5, 6, 4, acc); break;
	case K_LABEL: uk_text (cv, x + 2, y - 1, "A", ink, 2); break;
	case K_BUTTON: uk_rbox (cv, x, y + 3, 14, 9, 3, soft, soft); uk_rline (cv, x, y + 3, 14, 9, 3, ink); break;
	case K_TEXTBOX: cv.fillRect (x, y + 3, 14, 9, 0xFFFFFF); uk_rline (cv, x, y + 3, 14, 9, 0, ink); cv.fillRect (x + 3, y + 5, 1, 5, ink); break;
	case K_CHECKBOX: uk_check_mark (cv, x + 1, y + 1, 12, true, UK_NORMAL); break;
	case K_LISTBOX: cv.fillRect (x, y + 1, 14, 12, 0xFFFFFF); uk_rline (cv, x, y + 1, 14, 12, 0, ink); cv.fillRect (x + 2, y + 3, 10, 2, acc); cv.fillRect (x + 2, y + 7, 10, 1, ink); cv.fillRect (x + 2, y + 10, 10, 1, ink); break;
	case K_DROPDOWN: cv.fillRect (x, y + 3, 14, 9, 0xFFFFFF); uk_rline (cv, x, y + 3, 14, 9, 0, ink); uk_glyph (cv, WKG_CHEV_DOWN, x + 10, y + 7, 5, ink); break;
	case K_SLIDER: cv.fillRect (x, y + 7, 14, 2, ink); cv.fillRect (x + 5, y + 3, 4, 10, acc); break;
	case K_PROGRESS: uk_rline (cv, x, y + 4, 14, 7, 0, ink); cv.fillRect (x + 1, y + 5, 7, 5, acc); break;
	case K_MENU: cv.fillRect (x, y + 2, 14, 3, ink); cv.fillRect (x + 2, y + 6, 8, 7, soft); break;
	case K_TOOLBAR: cv.fillRect (x, y + 3, 14, 8, soft); cv.fillRect (x + 2, y + 5, 3, 4, ink); cv.fillRect (x + 7, y + 5, 3, 4, acc); break;
	case K_STATUSBAR: cv.fillRect (x, y + 9, 14, 4, soft); cv.fillRect (x + 1, y + 10, 6, 2, ink); break;
	}
}
class Toolbox : public Widget
{
public:
	Designer *des;
	int hot, down, scroll; bool dragging; int pressX, pressY; unsigned lastClick; int lastIdx;
	void (*onPick) (int kind);			// a double click (the designer adds it)
	Toolbox (int l, int t, int w, int h, Designer *d) : Widget (l, t, w, h), des (d), hot (-1), down (-1), scroll (0), dragging (false), pressX (0), pressY (0), lastClick (0), lastIdx (-1), onPick (0) {}
	int rowH () { return uk_fh () + 6; }
	// item i's box (2 columns; the groups' titles across)
	void place (int i, int *x, int *y, int *w)
	{
		int RH = rowH (), yy = 4 - scroll, col = 0, cw = (width - 12) / 2;
		for (int k = 0; k <= i; k++)
		{
			if (TOOLS[k].kind < 0) { if (col) { yy += RH; col = 0; } if (k == i) { *x = 6; *y = yy; *w = width - 12; return; } yy += RH; continue; }
			if (k == i) { *x = 6 + col * cw; *y = yy; *w = cw; return; }
			if (++col == 2) { col = 0; yy += RH; }
		}
	}
	int itemAt (int mx, int my) { for (int i = 0; i < NTOOLS; i++) { if (TOOLS[i].kind < 0) continue; int x, y, w; place (i, &x, &y, &w); if (mx >= x && mx < x + w && my >= y && my < y + rowH ()) return i; } return -1; }
	void onDraw () override
	{
		canvas.clear (C_BG);
		int RH = rowH ();
		for (int i = 0; i < NTOOLS; i++)
		{
			int x, y, w; place (i, &x, &y, &w);
			if (y + RH < 0 || y > height) continue;
			if (TOOLS[i].kind < 0)
			{
				uk_text (canvas, x, y + 3, TOOLS[i].label, uk_mix (C_BG, C_TEXT, 170), 2);
				continue;
			}
			if (i == down || i == hot) uk_rbox (canvas, x, y + 1, w - 2, RH - 2, 4, uk_mix (C_BG, C_ACCENT, i == down ? 90 : 40), uk_mix (C_BG, C_ACCENT, i == down ? 90 : 40));
			tool_icon (canvas, TOOLS[i].kind, x + 4, y + (RH - 14) / 2, C_TEXT, C_ACCENT);
			uk_text (canvas, x + 24, y + 3, TOOLS[i].label, C_TEXT);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx == -1 && my == -1 && !bl && !wheel) { if (hot >= 0) { hot = -1; invalidate (true); } return false; }
		if (wheel) { scroll = imax (0, scroll - wheel * rowH ()); invalidate (true); return true; }
		bool inside = mx >= 0 && my >= 0 && mx < width && my < height;
		if (bl && !pressed)
		{
			if (!inside) return false;
			pressed = true; down = itemAt (mx, my); pressX = mx; pressY = my; dragging = false;
			if (down >= 0) catchOutside = true;
			unsigned now = kapi_get_ticks ();
			if (down >= 0 && down == lastIdx && now - lastClick < 35) { if (onPick) onPick (TOOLS[down].kind); down = -1; lastClick = 0; catchOutside = false; pressed = false; }
			else { lastClick = now; lastIdx = down; }
			invalidate (true); return true;
		}
		if (bl && pressed && down >= 0)
		{
			if (!dragging && abs (mx - pressX) + abs (my - pressY) > 6) dragging = true;
			if (dragging) { int dx, dy; toDesigner (mx, my, &dx, &dy); if (inDesigner (dx, dy)) des->toolDrag (TOOLS[down].kind, dx, dy, false); else des->toolLeave (); }
			return true;
		}
		if (!bl && pressed)
		{
			pressed = false; catchOutside = false;
			if (dragging && down >= 0) { int dx, dy; toDesigner (mx, my, &dx, &dy); if (inDesigner (dx, dy)) des->toolDrag (TOOLS[down].kind, dx, dy, true); else des->toolLeave (); }
			dragging = false; down = -1; invalidate (true);
			return true;
		}
		if (mx < 0) { if (hot >= 0) { hot = -1; invalidate (true); } return false; }
		int h = inside ? itemAt (mx, my) : -1;
		if (h != hot) { hot = h; invalidate (true); }
		return inside;
	}
	static void absPos (Widget *w, int *x, int *y) { *x = 0; *y = 0; for (; w; w = w->parent) { *x += w->left; *y += w->top; } }
	void toDesigner (int mx, int my, int *dx, int *dy) { int ax, ay, bx, by; absPos (this, &ax, &ay); absPos (des, &bx, &by); *dx = mx + ax - bx; *dy = my + ay - by; }
	bool inDesigner (int dx, int dy) { return !des->hidden && dx >= 0 && dy >= 0 && dx < des->width && dy < des->height; }
};

} // namespace qs

#endif
