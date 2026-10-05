//
// props.h -- QBStudio's properties: the chosen element's (its name, its text, its layout, its look), each a row --
// a text typed in place, a box ticked, a choice from a list --, and its events: each event's SUB (written, or a
// double click writes it). A change is made to the element, then told to the app (g_onFormEdited).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _qbstudio_props_h
#define _qbstudio_props_h

#include "designer.h"
#include "gen.h"

namespace qs {
using namespace uikit;

enum { PR_NAME, PR_TEXT, PR_VALUE, PR_FLAG, PR_CHOICE, PR_HEAD };
struct PropRow { int type; const char *key; const char *label; const char *tip; };
static bool (*g_subExists) (const char *name);		// the app: is there such a SUB in the code?
static void (*g_openSub) (const char *name);		// ... show it (written first if it is not)
static const char *g_formName = "Main";

// The rows an element has
static void rows_of (const El *e, Vec<PropRow> &r)
{
	auto add = [&] (int t, const char *k, const char *l, const char *tip) { PropRow p = { t, k, l, tip }; r.push (p); };
	int k = e->kind;
	add (PR_HEAD, 0, "Common", 0);
	if (k != K_SPACER && k != K_MENU && k != K_SEP && k != K_COMMENT) add (PR_NAME, "name", "Name", "the object in the code; its events' SUBs start so");
	if (k == K_WINDOW || k == K_LABEL || k == K_BUTTON || k == K_TEXTBOX || k == K_CHECKBOX || k == K_GROUP || k == K_STATUSBAR || k == K_MENUTITLE || k == K_MENUITEM)
		add (PR_TEXT, "text", k == K_WINDOW ? "Title" : "Text", 0);
	if (k == K_LISTBOX || k == K_DROPDOWN) add (PR_VALUE, "items", "Items", "a|b|c");
	if (k == K_MENUITEM) add (PR_VALUE, "key", "Shortcut", "Ctrl+Q");
	if (k == K_CHECKBOX) add (PR_FLAG, "checked", "Checked", 0);
	if (k == K_TEXTBOX) add (PR_FLAG, "readonly", "Read-only", 0);
	if (k == K_BUTTON) { add (PR_FLAG, "default", "Default", "Enter clicks it"); add (PR_FLAG, "cancel", "Cancel", "Esc clicks it"); }
	if (k == K_SLIDER) { add (PR_VALUE, "max", "Maximum", 0); add (PR_VALUE, "value", "Value", 0); }
	if (k == K_PROGRESS) add (PR_VALUE, "value", "Value", "0..100");
	if (k == K_HOST) add (PR_VALUE, "content", "Shows", "a user control's name (or by code: host.Content = Name)");
	if (is_control (k)) { add (PR_FLAG, "disabled", "Disabled", 0); add (PR_FLAG, "hidden", "Hidden", 0); }
	if (k == K_WINDOW && e->uc) { add (PR_HEAD, 0, "User control", 0); add (PR_VALUE, "size", "Size", "320x200: as drawn (a Host gives it its own)"); }
	else if (k == K_WINDOW) { add (PR_HEAD, 0, "Window", 0); add (PR_VALUE, "size", "Size", "380x260 (empty: as its contents)"); add (PR_VALUE, "min", "Smallest", "320x220"); add (PR_FLAG, "resizable", "Resizable", 0); }
	if (k == K_MENU || k == K_MENUTITLE || k == K_SEP || k == K_COMMENT || k == K_WINDOW) return;
	add (PR_HEAD, 0, "Layout", 0);
	// (a container too: a Column of 200 pixels in a Row, another that fills what is left)
	add (PR_VALUE, "width", "Width", is_container (k) ? "(as its contents)" : "(as its text)"); add (PR_VALUE, "height", "Height", is_container (k) ? "(as its contents)" : "(as its text)");
	add (PR_FLAG, "fill", "Fill", "the room left (a Row's), the width (a Column's)"); add (PR_VALUE, "grow", "Grow", "its share of the room");
	if (is_control (k) || k == K_SPACER || k == K_CANVAS) add (PR_CHOICE, "align", "Align", 0);
	// its place across its container (a Column: halign; a Row: valign; a Grid's cell: both)
	add (PR_VALUE, "halign", "H. align", "left, center, right, stretch");
	add (PR_VALUE, "valign", "V. align", "top, center, bottom, stretch");
	if (k == K_COLUMN || k == K_ROW || k == K_GROUP || k == K_GRID || k == K_TOOLBAR) { add (PR_VALUE, "padding", "Padding", 0); add (PR_VALUE, "gap", "Gap", 0); }
	if (k == K_ROW) { add (PR_CHOICE, "align", "Align", "its children packed"); add (PR_VALUE, "widths", "Widths", "200,*,100: its children's, in order"); }
	if (k == K_COLUMN || k == K_GROUP) add (PR_VALUE, "heights", "Heights", "40,*,auto: its children's, in order");
	if (k == K_GRID) { add (PR_VALUE, "cols", "Columns", 0); add (PR_VALUE, "widths", "Widths", "200,*,2*: pixels, shares of the rest, auto"); add (PR_VALUE, "heights", "Heights", "auto,*: the rows, the same way"); }
	if (k == K_CANVAS) add (PR_VALUE, "size", "Size", "200x120");
	if (e->parent && e->parent->kind == K_GRID) add (PR_VALUE, "cell", "Cell", "column,row");
	if (e->parent && e->parent->kind == K_CANVAS) add (PR_VALUE, "at", "At", "x,y");
}
static const char *const ALIGNS[] = { "(none)", "left", "center", "right" };

// The events of an element: their SUBs' names
static int events_of (const El *e, char names[][64], const char *labels[], int cap)
{
	int n = 0;
	if (e->kind == K_WINDOW)
	{
		static const char *const W[] = { "Load", "Resize", "Close" }, *const U[] = { "Load", "Resize", "Show" };	// (a user control: made, placed, shown in a Host)
		for (int i = 0; i < 3 && n < cap; i++) { const char *w = e->uc ? U[i] : W[i]; snprintf (names[n], 64, "%s_%s", g_formName, w); labels[n++] = w; }
		return n;
	}
	const char *ev = event_of (e->kind);
	if (!ev || !e->name[0]) return 0;
	snprintf (names[n], 64, "%s_%s", e->name, ev); labels[n++] = ev;
	return n;
}

class PropGrid : public Widget
{
public:
	El *el;
	int tab;					// 0 properties, 1 events
	Textbox *edit; int editRow;
	Vec<PropRow> rows;
	PropGrid (int l, int t, int w, int h) : Widget (l, t, w, h), el (0), tab (0), edit (0), editRow (-1), m_lastClick (0), m_lastRow (-1), m_top (0), m_pend (false) { canFocus = true; }
	int rowH () { return uk_fh () + 8; }
	int headH () { return uk_fh () * 2 + 30; }
	static const int TABS_H = 30;
	int labelW () { return width * 42 / 100; }

	void show (El *e)
	{
		commit ();
		el = e; rows.clear (); m_top = 0;
		if (e) rows_of (e, rows);
		invalidate (true);
	}
	// what a row shows
	void value (const PropRow &r, char *o, int cap)
	{
		o[0] = 0;
		if (!el) return;
		if (r.type == PR_NAME) cpy (o, el->name, cap);
		else if (r.type == PR_TEXT) cpy (o, el->text, cap);
		else { const char *v = el->get (r.key); if (v) cpy (o, v, cap); }
	}
	void onDraw () override
	{
		canvas.clear (C_BG);
		unsigned dim = uk_mix (C_BG, C_TEXT, 140), line = uk_mix (C_BG, C_TEXT, 40);
		if (!el) { uk_text_c (canvas, 0, 0, width, 80, "Choose an element of the window", dim); return; }
		// the header: its name, its kind
		uk_rbox (canvas, 6, 6, width - 12, uk_fh () + 12, 5, C_FIELD, C_FIELD);
		uk_rline (canvas, 6, 6, width - 12, uk_fh () + 12, 5, line);
		char n[64]; snprintf (n, sizeof n, "%s", el->name[0] ? el->name : el->kind == K_MENUITEM || el->kind == K_MENUTITLE ? el->text : "(no name)");
		uk_text (canvas, 14, 12, n, C_FIELD_TEXT, 2);
		uk_text (canvas, 22 + uk_tw (n, 2), 12, el->kind == K_MENUTITLE ? "Menu" : el->kind == K_MENUITEM ? "Menu item" : KIND_NAMES[el->kind], dim);
		// the tabs
		int ty = uk_fh () + 24, tw = (width - 12) / 2;
		static const char *const T[2] = { "Properties", "Events" };
		for (int i = 0; i < 2; i++)
		{
			unsigned bg = i == tab ? C_ACCENT : C_FIELD;
			uk_rbox (canvas, 6 + i * tw, ty, tw, TABS_H - 6, 4, bg, bg);
			uk_text_c (canvas, 6 + i * tw, ty, tw, TABS_H - 6, T[i], i == tab ? uk_ink_on (C_ACCENT) : C_FIELD_TEXT);
		}
		int y0 = headH (), RH = rowH (), LW = labelW ();
		Canvas clip; clip.adopt (canvas.px + y0 * canvas.stride, width, imax (1, height - y0), canvas.stride);
		if (tab == 0)
		{
			for (int i = 0; i < rows.n; i++)
			{
				int y = i * RH - m_top; if (y + RH < 0 || y > clip.h) continue;
				const PropRow &r = rows[i];
				if (r.type == PR_HEAD) { uk_text (clip, 8, y + 4, r.label, C_TEXT, 2); continue; }
				if (i == editRow) clip.fillRect (0, y, width, RH, uk_mix (C_BG, C_ACCENT, 50));
				uk_text (clip, 16, y + 4, r.label, C_TEXT);
				char v[128]; value (r, v, sizeof v);
				int vx = LW;
				if (r.type == PR_FLAG) uk_check_mark (clip, vx, y + (RH - 14) / 2, 14, el->flag (r.key), UK_NORMAL);
				else if (!v[0] && r.tip) uk_text (clip, vx, y + 4, r.tip, dim, 1);
				else
				{
					if (y >= 0 && y + RH <= clip.h) { Canvas vc; vc.adopt (clip.px + y * clip.stride + vx, imax (1, width - vx - 6), RH, clip.stride); uk_text (vc, 0, 4, v, C_TEXT); }
					if (r.type == PR_CHOICE) uk_glyph (clip, WKG_CHEV_DOWN, width - 14, y + RH / 2, 7, dim);
				}
				clip.fillRect (8, y + RH - 1, width - 16, 1, uk_mix (C_BG, C_TEXT, 18));
			}
		}
		else
		{
			char names[4][64]; const char *labels[4];
			int n = events_of (el, names, labels, 4);
			if (!n) uk_text (clip, 12, 8, el->name[0] ? "No events" : "No events: give it a name", dim);
			for (int i = 0; i < n; i++)
			{
				int y = i * RH;
				uk_text (clip, 16, y + 4, labels[i], C_TEXT);
				bool has = g_subExists && g_subExists (names[i]);
				if (has) { uk_text (clip, LW, y + 4, names[i], C_ACCENT); clip.fillRect (LW, y + 4 + uk_fh () - 2, uk_tw (names[i]), 1, C_ACCENT); }
				else uk_text (clip, LW, y + 4, "(click: a new SUB)", dim, 1);
				clip.fillRect (8, y + RH - 1, width - 16, 1, uk_mix (C_BG, C_TEXT, 18));
			}
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx < 0) return false;
		if (wheel) { m_top = imax (0, imin (m_top - wheel * rowH (), rows.n * rowH () - (height - headH ()))); if (m_top < 0) m_top = 0; invalidate (true); return true; }
		if (!bl || pressed) { if (!bl) pressed = false; return true; }
		pressed = true;
		if (!el) return true;
		int ty = uk_fh () + 24;
		if (my >= ty && my < ty + TABS_H) { commit (); tab = mx < width / 2 ? 0 : 1; invalidate (true); return true; }
		int y0 = headH (); if (my < y0) return true;
		int i = (my - y0 + (tab == 0 ? m_top : 0)) / rowH ();
		if (tab == 1)
		{
			char names[4][64]; const char *labels[4];
			int n = events_of (el, names, labels, 4);
			if (i >= 0 && i < n && g_openSub) g_openSub (names[i]);
			return true;
		}
		if (i < 0 || i >= rows.n) { commit (); return true; }
		const PropRow &r = rows[i];
		if (r.type == PR_HEAD) return true;
		commit ();
		if (r.type == PR_FLAG) { el->setFlag (r.key, !el->flag (r.key)); changed (); return true; }
		if (r.type == PR_CHOICE)
		{
			int ax = 0, ay = 0; for (Widget *w = this; w; w = w->parent) { ax += w->left; ay += w->top; }
			PopupMenu pm (ax + labelW (), ay + y0 + (i + 1) * rowH () - m_top);
			for (int q = 0; q < 4; q++) pm.add (ALIGNS[q], q);
			int c = pm.run ();
			if (c >= 0) { el->set (r.key, c == 0 ? 0 : ALIGNS[c]); changed (); }
			return true;
		}
		// a text: typed in place
		char v[128]; value (r, v, sizeof v);
		editRow = i;
		edit = new Textbox (labelW () - 4, y0 + i * rowH () - m_top + 1, width - labelW () - 2, rowH () - 2, v, enter);
		edit->tag = i;
		addChild (edit); edit->setFocus (); edit->onTabFocus ();
		invalidate (true);
		return true;
	}
	// The value typed written to the element
	void commit ()
	{
		if (!edit) return;
		Textbox *t = edit; int i = editRow; edit = 0; editRow = -1;
		removeChild (t);
		if (el && i >= 0 && i < rows.n)
		{
			const PropRow &r = rows[i];
			char v[128]; value (r, v, sizeof v);
			if (strcmp (v, t->text))
			{
				if (r.type == PR_NAME)
				{
					bool ok = true; for (const char *p = t->text; *p; p++) if (!name_ch (*p)) ok = false;
					if (t->text[0] >= '0' && t->text[0] <= '9') ok = false;
					if (ok && (!g_isWord || !g_isWord (t->text))) cpy (el->name, t->text, sizeof el->name);
				}
				else if (r.type == PR_TEXT) { cpy (el->text, t->text, sizeof el->text); el->hasText = true; }
				else el->set (r.key, t->text[0] ? t->text : 0);
				delete t;
				m_pend = true; invalidate (true);	// (told at the next tick: the app reads the form again)
				return;
			}
		}
		delete t;
		invalidate (true);
	}
	// The text box lost: what was typed kept (the app's tick)
	void tick () { if (edit && !edit->hasFocus) commit (); if (m_pend) { m_pend = false; if (g_onFormEdited) g_onFormEdited (); } }
	static void enter (Widget &w) { PropGrid *g = (PropGrid *) w.parent; g->setFocus (); }
	void changed () { invalidate (true); if (g_onFormEdited) g_onFormEdited (); }
private:
	unsigned m_lastClick; int m_lastRow, m_top; bool m_pend;
	static bool name_ch (char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
};

} // namespace qs

#endif
