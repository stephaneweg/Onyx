//
// uikit/flat.cpp -- UIKit behind plain C functions on handles (uikit/flat.h): for Onyx BASIC (#import
// uikit) and C programs. A handle is the widget itself; its tag says what it is.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "uikit/uikit.h"
#include "uikit/flat.h"

using namespace uikit;

namespace {

enum { FK_BUTTON = 1, FK_LABEL, FK_TEXTBOX, FK_CHECKBOX, FK_LISTBOX, FK_DROPDOWN, FK_PROGRESS, FK_SLIDER, FK_WINDOW };

int flen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
void fcpy (char *d, const char *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
bool fsame (const char *a, const char *b)			// (whatever the case)
{
	for (; *a && *b; a++, b++)
	{
		char x = *a >= 'a' && *a <= 'z' ? (char) (*a - 32) : *a, y = *b >= 'a' && *b <= 'z' ? (char) (*b - 32) : *b;
		if (x != y) return false;
	}
	return *a == *b;
}

// The program's window: a Root that says when it was resized, with its menus
class FlatWin : public Root
{
public:
	enum { MAXMENU = 64 };
	struct Item { char title[32], item[64], key[16]; uk_chosen fn; };
	bool closed; uk_resized onSize;
	Item items[MAXMENU]; int nitems; Menu *bar;
	FlatWin (int w, int h, const char *t) : Root (w, h, t), closed (false), onSize (0), nitems (0), bar (0) { tag = FK_WINDOW; }
	void onResized () override { if (onSize) onSize (this, width, height); }
	// the bar made again: the menus in the order of their first items
	void menus ()
	{
		delete bar; bar = new Menu;
		bool done[MAXMENU];
		for (int i = 0; i < nitems; i++) done[i] = false;
		for (int i = 0; i < nitems; i++)
		{
			if (done[i]) continue;
			bar->menu (items[i].title);
			for (int j = i; j < nitems; j++)
			{
				if (done[j] || !fsame (items[j].title, items[i].title)) continue;
				done[j] = true;
				if (items[j].item[0] == '-' && !items[j].item[1]) { bar->separator (); continue; }
				const char *k = items[j].key; long code = 0; char shown[8] = "";
				char lead[6]; fcpy (lead, k, sizeof lead);		// "Ctrl+S": the letter with Ctrl
				if (flen (k) == 6 && fsame ("CTRL+", lead))
				{
					char c = k[5] >= 'a' && k[5] <= 'z' ? (char) (k[5] - 32) : k[5];
					code = UK_CTRL (c); shown[0] = '^'; shown[1] = c; shown[2] = 0;
				}
				bar->item (items[j].item, shown, code, items[j].fn);
			}
		}
		bar->publish ();
	}
};

FlatWin *win_of (void *h) { Widget *w = (Widget *) h; return w && w->tag == FK_WINDOW ? (FlatWin *) w : 0; }
void changed (Widget *w) { w->invalidate (true); Root *r = Root::current (); if (r) r->invalidate (true); }
void *add (void *window, Widget *w, int kind)
{
	FlatWin *f = win_of (window);
	if (!f || !w) { delete w; return 0; }
	w->tag = kind;
	f->addChild (w);
	f->invalidate (true);
	return w;
}
// "one|two|three" -> a list's items
void list_items (ListBox *lb, const char *items)
{
	char item[128]; int n = 0;
	for (int i = 0; items; i++)
	{
		char c = items[i];
		if (c == '|' || c == 0) { item[n] = 0; if (n || c == '|') lb->add (item); n = 0; if (!c) break; continue; }
		if (n < 127) item[n++] = c;
	}
}
char s_path[512];
// (a widget's callback gets the widget: Action is void (*) (Widget &) -- a reference is its address)
Action act (uk_event fn) { return (Action) (void (*) (void)) fn; }

} // namespace

extern "C" {

// ---- the window -----------------------------------------------------------------------------------------
void *uk_window (const char *title, int w, int h, int flags)
{
	if (Root::current ()) return 0;				// (a program has one window)
	uikit::init ();
	if (w < 64) w = 64;
	if (h < 32) h = 32;
	FlatWin *f = new FlatWin (w, h, title ? title : "");
	if (f->canvas.px == 0) { delete f; return 0; }
	if (flags & UK_FLAT_RESIZABLE) f->setResizable (true);
	f->attach ();
	return f;
}
int uk_window_step (void *window)
{
	FlatWin *f = win_of (window);
	if (!f || f->closed) return 0;
	if (!f->step ()) f->closed = true;
	return f->closed ? 0 : 1;
}
int uk_window_wait (void *window)
{
	if (!uk_window_step (window)) return 0;
	kapi_msleep (16);
	return 1;
}
void uk_window_run (void *window)
{
	while (uk_window_wait (window)) {}
}
void uk_window_close (void *window) { FlatWin *f = win_of (window); if (f) f->closed = true; }
int  uk_window_width (void *window) { FlatWin *f = win_of (window); return f ? f->width : 0; }
int  uk_window_height (void *window) { FlatWin *f = win_of (window); return f ? f->height : 0; }
void uk_window_min_size (void *window, int w, int h) { FlatWin *f = win_of (window); if (f) f->setMinSize (w, h); }
void uk_window_on_resize (void *window, uk_resized fn) { FlatWin *f = win_of (window); if (f) f->onSize = fn; }

// ---- the widgets ----------------------------------------------------------------------------------------
void *uk_label (void *window, int x, int y, int w, int h, const char *text)
{ return win_of (window) ? add (window, new Label (x, y, w, h, text ? text : ""), FK_LABEL) : 0; }
void *uk_button (void *window, int x, int y, int w, int h, const char *text, uk_event on_click)
{ return win_of (window) ? add (window, new Button (x, y, w, h, text ? text : "", act (on_click)), FK_BUTTON) : 0; }
void *uk_textbox (void *window, int x, int y, int w, int h, const char *text, uk_event on_change)
{
	if (!win_of (window)) return 0;
	Textbox *t = new Textbox (x, y, w, h, "", act (on_change));
	t->maxLen = Textbox::TEXT_CAP - 1;
	t->setText (text ? text : "");
	return add (window, t, FK_TEXTBOX);
}
void *uk_checkbox (void *window, int x, int y, int w, int h, const char *text, int checked, uk_event on_click)
{ return win_of (window) ? add (window, new Checkbox (x, y, w, h, text ? text : "", checked != 0, act (on_click)), FK_CHECKBOX) : 0; }
void *uk_listbox (void *window, int x, int y, int w, int h, const char *items, uk_event on_change)
{
	if (!win_of (window)) return 0;
	ListBox *lb = new ListBox (x, y, w, h, act (on_change), act (on_change));
	list_items (lb, items);
	return add (window, lb, FK_LISTBOX);
}
void *uk_dropdown (void *window, int x, int y, int w, int h, const char *items, uk_event on_change)
{
	if (!win_of (window)) return 0;
	// its options: the text kept, cut at the bars (they live as long as the window)
	int len = flen (items), n = 1;
	for (int i = 0; i < len; i++) if (items[i] == '|') n++;
	char *copy = new char[len + 1]; fcpy (copy, items, len + 1);
	const char **opts = new const char *[n];
	int k = 0; opts[k++] = copy;
	for (int i = 0; i < len; i++) if (copy[i] == '|') { copy[i] = 0; opts[k++] = copy + i + 1; }
	return add (window, new Dropdown (x, y, w, h, opts, n, 0, act (on_change)), FK_DROPDOWN);
}
void *uk_slider (void *window, int x, int y, int w, int h, int max, int value, uk_event on_change)
{ return win_of (window) ? add (window, new Slider (x, y, w, h, 0, max > 0 ? max : 100, value, act (on_change)), FK_SLIDER) : 0; }
void *uk_progress (void *window, int x, int y, int w, int h, int max, int value)
{ return win_of (window) ? add (window, new Progress (x, y, w, h, 0, max > 0 ? max : 100, value), FK_PROGRESS) : 0; }
void uk_set_range (void *widget, int min, int max)
{
	Widget *w = (Widget *) widget; if (!w || max <= min) return;
	if (w->tag == FK_SLIDER) { Slider *s = (Slider *) w; s->vmin = min; s->vmax = max; if (s->value < min) s->value = min; if (s->value > max) s->value = max; }
	else if (w->tag == FK_PROGRESS) { Progress *p = (Progress *) w; p->vmin = min; p->vmax = max; }
	else return;
	changed (w);
}

void uk_set_text (void *widget, const char *text)
{
	Widget *w = (Widget *) widget; if (!w) return;
	if (!text) text = "";
	switch (w->tag)
	{
	case FK_BUTTON: fcpy (((Button *) w)->text, text, sizeof ((Button *) w)->text); break;
	case FK_LABEL: ((Label *) w)->setText (text); break;
	case FK_TEXTBOX: ((Textbox *) w)->setText (text); break;
	case FK_CHECKBOX: fcpy (((Checkbox *) w)->text, text, sizeof ((Checkbox *) w)->text); break;
	case FK_LISTBOX: { ListBox *l = (ListBox *) w; for (int i = 0; i < l->count; i++) if (fsame (l->item (i), text)) { l->setSel (i); break; } break; }
	case FK_DROPDOWN: { Dropdown *d = (Dropdown *) w; for (int i = 0; i < d->nopts; i++) if (fsame (d->opts[i], text)) { d->sel = i; break; } break; }
	default: return;
	}
	changed (w);
}
const char *uk_get_text (void *widget)
{
	Widget *w = (Widget *) widget; if (!w) return "";
	switch (w->tag)
	{
	case FK_BUTTON: return ((Button *) w)->text;
	case FK_LABEL: return ((Label *) w)->text;
	case FK_TEXTBOX: return ((Textbox *) w)->text;
	case FK_CHECKBOX: return ((Checkbox *) w)->text;
	case FK_LISTBOX: { ListBox *l = (ListBox *) w; return l->item (l->sel); }
	case FK_DROPDOWN: { Dropdown *d = (Dropdown *) w; return d->sel >= 0 && d->sel < d->nopts ? d->opts[d->sel] : ""; }
	}
	return "";
}
void uk_set_value (void *widget, int v)
{
	Widget *w = (Widget *) widget; if (!w) return;
	switch (w->tag)
	{
	case FK_CHECKBOX: ((Checkbox *) w)->checked = v != 0; break;
	case FK_LISTBOX: ((ListBox *) w)->setSel (v); break;
	case FK_DROPDOWN: { Dropdown *d = (Dropdown *) w; if (v >= 0 && v < d->nopts) d->sel = v; break; }
	case FK_PROGRESS: ((Progress *) w)->setValue (v); break;
	case FK_SLIDER: { Slider *s = (Slider *) w; s->value = v < s->vmin ? s->vmin : v > s->vmax ? s->vmax : v; break; }
	default: return;
	}
	changed (w);
}
int uk_get_value (void *widget)
{
	Widget *w = (Widget *) widget; if (!w) return 0;
	switch (w->tag)
	{
	case FK_CHECKBOX: return ((Checkbox *) w)->checked ? 1 : 0;
	case FK_LISTBOX: return ((ListBox *) w)->sel;
	case FK_DROPDOWN: return ((Dropdown *) w)->sel;
	case FK_PROGRESS: return ((Progress *) w)->value;
	case FK_SLIDER: return ((Slider *) w)->value;
	}
	return 0;
}
void uk_add_item (void *widget, const char *text)
{ Widget *w = (Widget *) widget; if (w && w->tag == FK_LISTBOX) { ((ListBox *) w)->add (text ? text : ""); changed (w); } }
void uk_clear_items (void *widget)
{ Widget *w = (Widget *) widget; if (w && w->tag == FK_LISTBOX) { ((ListBox *) w)->clear (); changed (w); } }
int uk_item_count (void *widget)
{
	Widget *w = (Widget *) widget; if (!w) return 0;
	return w->tag == FK_LISTBOX ? ((ListBox *) w)->count : w->tag == FK_DROPDOWN ? ((Dropdown *) w)->nopts : 0;
}
void uk_move (void *widget, int x, int y, int w, int h)
{
	Widget *wd = (Widget *) widget; if (!wd || wd->tag == FK_WINDOW) return;
	wd->left = x; wd->top = y; wd->resizeTo (w > 1 ? w : 1, h > 1 ? h : 1);
	changed (wd);
}
void uk_show (void *widget, int on) { Widget *w = (Widget *) widget; if (w && w->tag != FK_WINDOW) { w->hidden = !on; changed (w); } }
void uk_enable (void *widget, int on) { Widget *w = (Widget *) widget; if (w && w->tag != FK_WINDOW) { w->disabled = !on; changed (w); } }
int  uk_shown (void *widget) { Widget *w = (Widget *) widget; return w && !w->hidden ? 1 : 0; }
int  uk_enabled (void *widget) { Widget *w = (Widget *) widget; return w && !w->disabled ? 1 : 0; }
void uk_focus (void *widget) { Widget *w = (Widget *) widget; if (w && w->tag != FK_WINDOW) w->setFocus (); }

// ---- the menus -----------------------------------------------------------------------------------------
void uk_menu_item (void *window, const char *title, const char *item, const char *key, uk_chosen on_choose)
{
	FlatWin *f = win_of (window);
	if (!f || f->nitems >= FlatWin::MAXMENU || !title || !item) return;
	FlatWin::Item &m = f->items[f->nitems++];
	fcpy (m.title, title, sizeof m.title); fcpy (m.item, item, sizeof m.item); fcpy (m.key, key ? key : "", sizeof m.key);
	m.fn = on_choose;
	f->menus ();
}

// ---- the dialogs ---------------------------------------------------------------------------------------
int uk_message (const char *title, const char *text, int buttons)
{
	uikit::init ();
	return uk_messagebox (title ? title : "", text ? text : "", buttons);
}
const char *uk_ask_open (const char *folder)
{
	s_path[0] = 0;
	if (!uk_file_open (s_path, sizeof s_path, folder && folder[0] ? folder : "SD:/")) s_path[0] = 0;
	return s_path;
}
const char *uk_ask_save (const char *folder, const char *name)
{
	s_path[0] = 0;
	if (!uk_file_save (s_path, sizeof s_path, folder && folder[0] ? folder : "SD:/", name && name[0] ? name : "untitled.txt")) s_path[0] = 0;
	return s_path;
}

} // extern "C"
