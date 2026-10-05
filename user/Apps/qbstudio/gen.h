//
// gen.h -- what QBStudio makes of a form: <Form>.form.bas, the window's code (generated, never edited by hand):
//   - DIM SHARED: each control an object of the controls' library (Control: Text, Value, Checked, Enabled, Visible,
//     Move, Focus, AddItem), the window an object (Window: Width, Height, Close);
//   - <Form>_Create: the window (UIKit.window), the controls made where the layout puts them at the window's size --
//     each with your SUB as what it calls (ADDRESSOF) --, the menus;
//   - <Form>_Layout (w, h): every control placed for a size -- the layout computed at two sizes, each place an
//     affine function of the size (a Column, a Row, a Grid share the room in proportion);
//   - <Form>_Sized: what the window calls when it was resized (the layout again, then your <Form>_Resize);
//   - <Form>_Run: the window made, <Form>_Load, the events until it is closed -- a control's event calls your SUB
//     <name>_<Event> (Click, Change) --, then <Form>_Close.
// The controls' library (LIBRARY) is BASIC too, PROPERTY over UIKit's functions (#import UIKit). The app's program: the library,
// the forms' code, your files, then "<Main>_Run" (build_program: the lines of each part kept, an error found back).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _qbstudio_gen_h
#define _qbstudio_gen_h

#include "form.h"

namespace qs {

// The controls' library: the objects the generated code and yours use. They are UIKit's widgets (#import UIKit:
// uikit/flat.h), each object holding its widget's handle.
static const char *const LIBRARY =
"' QBStudio's controls (made by QBStudio: the objects of a window's controls, UIKit's widgets)\n"
"#import UIKit\n"
"CLASS Control\n"
"  handle AS DOUBLE\n"
"END CLASS\n"
"PROPERTY Control.Text AS STRING\n"
"  RETURN UIKit.get_text (this.handle)\n"
"END PROPERTY\n"
"PROPERTY Control.Text (v AS STRING)\n"
"  UIKit.set_text this.handle, v\n"
"END PROPERTY\n"
"PROPERTY Control.Value AS DOUBLE\n"
"  RETURN UIKit.get_value (this.handle)\n"
"END PROPERTY\n"
"PROPERTY Control.Value (v AS DOUBLE)\n"
"  UIKit.set_value this.handle, v\n"
"END PROPERTY\n"
"PROPERTY Control.Checked AS INTEGER\n"
"  IF UIKit.get_value (this.handle) THEN RETURN -1 ELSE RETURN 0\n"
"END PROPERTY\n"
"PROPERTY Control.Checked (v AS INTEGER)\n"
"  UIKit.set_value this.handle, v\n"
"END PROPERTY\n"
"PROPERTY Control.Enabled AS INTEGER\n"
"  IF UIKit.enabled (this.handle) THEN RETURN -1 ELSE RETURN 0\n"
"END PROPERTY\n"
"PROPERTY Control.Enabled (v AS INTEGER)\n"
"  UIKit.enable this.handle, v\n"
"END PROPERTY\n"
"PROPERTY Control.Visible AS INTEGER\n"
"  IF UIKit.shown (this.handle) THEN RETURN -1 ELSE RETURN 0\n"
"END PROPERTY\n"
"PROPERTY Control.Visible (v AS INTEGER)\n"
"  UIKit.show this.handle, v\n"
"END PROPERTY\n"
"PROPERTY Control.Count AS INTEGER\n"
"  RETURN UIKit.item_count (this.handle)\n"
"END PROPERTY\n"
"SUB Control.Move (x, y, w, h)\n"
"  UIKit.move this.handle, x, y, w, h\n"
"END SUB\n"
"SUB Control.Focus\n"
"  UIKit.focus this.handle\n"
"END SUB\n"
"SUB Control.AddItem (s AS STRING)\n"
"  UIKit.add_item this.handle, s\n"
"END SUB\n"
"SUB Control.Clear\n"
"  UIKit.clear_items this.handle\n"
"END SUB\n"
"CLASS Window\n"
"  handle AS DOUBLE\n"
"END CLASS\n"
"PROPERTY Window.Width AS INTEGER\n"
"  RETURN UIKit.window_width (this.handle)\n"
"END PROPERTY\n"
"PROPERTY Window.Height AS INTEGER\n"
"  RETURN UIKit.window_height (this.handle)\n"
"END PROPERTY\n"
"SUB Window.Close\n"
"  UIKit.window_close this.handle\n"
"END SUB\n";

// A BASIC string literal (a quote: CHR$(34))
static void bstr (Str &o, const char *s)
{
	o.put ('"');
	for (; *s; s++)
	{
		if (*s == '"') { o.puts ("\" + CHR$(34) + \""); continue; }
		if (*s == '&') continue;				// (a menu's mnemonic: not shown)
		o.put (*s);
	}
	o.put ('"');
}
static void bstr_keep (Str &o, const char *s) { o.put ('"'); for (; *s; s++) { if (*s == '"') { o.puts ("\" + CHR$(34) + \""); continue; } o.put (*s); } o.put ('"'); }

// The controls of a form, in order (the menus' items after them), with their variables' names
struct GenCtl { El *e; char var[40]; };
static void collect (El *e, const char *form, Vec<GenCtl> &out, int *anon)
{
	if (e->kind == K_COMMENT) return;
	if (is_control (e->kind) || e->kind == K_MENUITEM || e->kind == K_SEP)
	{
		GenCtl c; c.e = e;
		if (e->name[0]) cpy (c.var, e->name, sizeof c.var);
		else snprintf (c.var, sizeof c.var, "%s_c%d", form, ++*anon);
		out.push (c);
	}
	for (int i = 0; i < e->kids.n; i++) collect (e->kids[i], form, out, anon);
}
static bool has_sub (const Vec<char *> &subs, const char *name) { for (int i = 0; i < subs.n; i++) if (ieq (subs[i], name)) return true; return false; }
static const char *event_of (int kind) { return kind == K_BUTTON || kind == K_CHECKBOX || kind == K_MENUITEM ? "Click" : kind == K_LABEL || kind == K_STATUSBAR || kind == K_PROGRESS ? 0 : "Change"; }
// The number for BASIC (a few decimals, no exponent)
static void bnum (Str &o, double v)
{
	char t[32]; snprintf (t, sizeof t, "%.4f", v);
	int l = (int) strlen (t); while (l > 0 && t[l - 1] == '0') t[--l] = 0; if (l > 0 && t[l - 1] == '.') t[--l] = 0;
	o.puts (t);
}
// An affine place: v0 + k * (s - s0)
static void affine (Str &o, int v0, double k, const char *s, int s0)
{
	if (k > -1e-6 && k < 1e-6) { o.printf ("%d", v0); return; }
	o.printf ("%d + INT((%s - %d) * ", v0, s, s0); bnum (o, k); o.puts (")");
}

// The form's code: subs = the SUBs your files hold (the events dispatched to them)
static void generate (Str &o, Form &f, const char *form, const Vec<char *> &subs)
{
	El *win = f.root; if (!win) return;
	Vec<GenCtl> ctl; int anon = 0;
	collect (win, form, ctl, &anon);
	int W0, H0; form_size (f, &W0, &H0);
	int minW = 0, minH = 0; win->size2 ("min", &minW, &minH);
	// the layout at two sizes: the places' coefficients
	enum { D = 1000 };
	Vec<int> a, b;
	form_layout (f, W0 + D, H0 + D);
	for (int i = 0; i < ctl.n; i++) { El *e = ctl[i].e; b.push (e->x); b.push (e->y); b.push (e->w); b.push (e->h); }
	form_layout (f, W0, H0);
	for (int i = 0; i < ctl.n; i++) { El *e = ctl[i].e; a.push (e->x); a.push (e->y); a.push (e->w); a.push (e->h); }

	o.printf ("' %s.form.bas -- made by QBStudio from %s.form: do not edit (it is made again at each change of the form)\n", form, form);
	o.printf ("DIM SHARED %s AS Window ()\n", form);			// (objects: made here)
	for (int i = 0; i < ctl.n; i++) o.printf ("DIM SHARED %s AS Control ()\n", ctl[i].var);
	o.puts ("\n");
	// what a control calls: your SUB <name>_<Event>, if you wrote it
	char sn[96];
	auto handler = [&] (const GenCtl &c)
	{
		const char *ev = event_of (c.e->kind);
		if (ev) { snprintf (sn, sizeof sn, "%s_%s", c.var, ev); if (has_sub (subs, sn)) { o.printf ("ADDRESSOF (%s)", sn); return; } }
		o.puts ("0");
	};
	// _Create
	o.printf ("SUB %s_Create\n  %s.handle = UIKit.window (", form, form); bstr_keep (o, win->hasText ? win->text : form);
	o.printf (", %d, %d, %d)\n", W0, H0, win->flag ("resizable") ? 1 : 0);
	if (minW || minH) o.printf ("  UIKit.window_min_size %s.handle, %d, %d\n", form, minW, minH);
	for (int i = 0; i < ctl.n; i++)
	{
		El *e = ctl[i].e; const char *v = ctl[i].var;
		int x = a[4 * i], y = a[4 * i + 1], w = a[4 * i + 2], h = a[4 * i + 3];
		const char *maker = 0;
		switch (e->kind)
		{
		case K_SEP:
			if (e->parent && e->parent->kind == K_MENUTITLE) { o.printf ("  UIKit.menu_item %s.handle, ", form); bstr (o, e->parent->text); o.puts (", \"-\", \"\", 0\n"); }
			continue;
		case K_MENUITEM:
		{
			const El *t = e->parent; if (!t) break;
			o.printf ("  UIKit.menu_item %s.handle, ", form); bstr (o, t->text); o.puts (", "); bstr (o, e->text); o.puts (", ");
			const char *k = e->get ("key"); bstr_keep (o, k ? k : "");
			o.puts (", "); handler (ctl[i]); o.puts ("\n");
			continue;
		}
		case K_LABEL: case K_STATUSBAR: maker = "label"; break;
		case K_BUTTON: maker = "button"; break;
		case K_TEXTBOX: maker = "textbox"; break;
		case K_CHECKBOX: maker = "checkbox"; break;
		case K_LISTBOX: maker = "listbox"; break;
		case K_DROPDOWN: maker = "dropdown"; break;
		case K_SLIDER: maker = "slider"; break;
		case K_PROGRESS: maker = "progress"; break;
		default: break;
		}
		if (!maker) continue;
		o.printf ("  %s.handle = UIKit.%s (%s.handle, %d, %d, %d, %d", v, maker, form, x, y, w, h);
		switch (e->kind)
		{
		case K_LABEL: case K_STATUSBAR: o.puts (", "); bstr_keep (o, e->text); break;
		case K_BUTTON: case K_TEXTBOX: o.puts (", "); bstr_keep (o, e->text); o.puts (", "); handler (ctl[i]); break;
		case K_CHECKBOX: o.puts (", "); bstr_keep (o, e->text); o.printf (", %d, ", e->flag ("checked") ? 1 : 0); handler (ctl[i]); break;
		case K_LISTBOX: o.puts (", "); bstr_keep (o, e->get ("items") ? e->get ("items") : ""); o.puts (", "); handler (ctl[i]); break;
		case K_DROPDOWN: o.puts (", "); bstr_keep (o, e->get ("items") ? e->get ("items") : e->text); o.puts (", "); handler (ctl[i]); break;
		case K_SLIDER: o.printf (", %d, %d, ", e->num ("max", 100), e->num ("value", 0)); handler (ctl[i]); break;
		case K_PROGRESS: o.printf (", 100, %d", e->num ("value", 0)); break;
		default: break;
		}
		o.puts (")\n");
		if (e->flag ("hidden")) o.printf ("  %s.Visible = 0\n", v);
		if (e->flag ("disabled") || e->flag ("readonly")) o.printf ("  %s.Enabled = 0\n", v);
	}
	o.printf ("  UIKit.window_on_resize %s.handle, ADDRESSOF (%s_Sized)\n", form, form);
	o.puts ("END SUB\n\n");
	// _Layout
	o.printf ("' the window's layout: where each control goes for a size w x h\nSUB %s_Layout (w, h)\n", form);
	if (minW) o.printf ("  IF w < %d THEN w = %d\n", minW, minW);
	if (minH) o.printf ("  IF h < %d THEN h = %d\n", minH, minH);
	for (int i = 0; i < ctl.n; i++)
	{
		if (ctl[i].e->kind == K_MENUITEM || ctl[i].e->kind == K_SEP) continue;
		o.printf ("  UIKit.move %s.handle, ", ctl[i].var);
		static const char *const S[4] = { "w", "h", "w", "h" };
		for (int k = 0; k < 4; k++)
		{
			int v0 = a[4 * i + k], v1 = b[4 * i + k];
			affine (o, v0, (double) (v1 - v0) / D, S[k], k % 2 ? H0 : W0);
			if (k < 3) o.puts (", ");
		}
		o.puts ("\n");
	}
	o.puts ("END SUB\n\n");
	// _Sized: the window was resized
	o.printf ("' the window was resized (what it calls): the layout again\nSUB %s_Sized (win, w, h)\n  %s_Layout w, h\n", form, form);
	snprintf (sn, sizeof sn, "%s_Resize", form); if (has_sub (subs, sn)) o.printf ("  %s\n", sn);
	o.puts ("END SUB\n\n");
	// _Run: the window, its events until it is closed (each control calls your SUB: _Create)
	o.printf ("' the window made and its events, until it is closed\nSUB %s_Run\n  %s_Create\n", form, form);
	snprintf (sn, sizeof sn, "%s_Load", form); if (has_sub (subs, sn)) o.printf ("  %s\n", sn);
	o.printf ("  DO WHILE UIKit.window_wait (%s.handle)\n  LOOP\n", form);
	snprintf (sn, sizeof sn, "%s_Close", form); if (has_sub (subs, sn)) o.printf ("  %s\n", sn);
	o.puts ("END SUB\n");
}

// The SUB / FUNCTION names a BASIC text defines ("SUB name", "FUNCTION name" at a line's start)
static void scan_subs (const char *src, Vec<char *> &out)
{
	for (const char *p = src; *p; )
	{
		while (*p == ' ' || *p == '\t') p++;
		int kw = 0;
		if (!strncasecmp (p, "SUB ", 4)) kw = 4; else if (!strncasecmp (p, "FUNCTION ", 9)) kw = 9;
		if (kw)
		{
			const char *n = p + kw; while (*n == ' ') n++;
			char t[64]; int k = 0;
			while ((*n == '_' || *n == '.' || *n == '$' || (*n >= '0' && *n <= '9') || (*n >= 'a' && *n <= 'z') || (*n >= 'A' && *n <= 'Z')) && k < 63) t[k++] = *n++;
			t[k] = 0;
			if (k) { char *c = (char *) malloc (k + 1); memcpy (c, t, k + 1); out.push (c); }
		}
		while (*p && *p != '\n') p++;
		if (*p) p++;
	}
}

// ---- the whole program ------------------------------------------------------------------------------------------------
// Its parts and where they start (an error's line found back in its file)
struct Part { char file[64]; int first, lines; };
static int count_lines (const char *s) { int n = 0; for (; *s; s++) if (*s == '\n') n++; return n; }
static void add_part (Str &o, Vec<Part> &parts, const char *file, const char *text)
{
	Part p; cpy (p.file, file, sizeof p.file);
	p.first = count_lines (o.str ()) + 1;
	o.puts (text);
	if (o.n && o.b[o.n - 1] != '\n') o.put ('\n');
	p.lines = count_lines (o.str ()) + 1 - p.first;
	parts.push (p);
}
// A line of the program -> its file and its line there (false: none)
static bool part_of (const Vec<Part> &parts, int line, const char **file, int *at)
{
	for (int i = 0; i < parts.n; i++)
		if (line >= parts[i].first && line < parts[i].first + parts[i].lines) { *file = parts[i].file; *at = line - parts[i].first + 1; return true; }
	return false;
}

} // namespace qs

#endif
