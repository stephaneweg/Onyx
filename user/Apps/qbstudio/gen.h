//
// gen.h -- what QBStudio makes of a form: <Form>.form.bas, the window's code (generated, never edited by hand):
//   - DIM SHARED: each control an object of the controls' library (Control: Text, Value, Checked, Enabled, Visible,
//     Move, Focus, AddItem), the window an object (Window: Width, Height, Close);
//   - <Form>_Create: WINDOW, the controls made where the layout puts them at the window's size, the menus;
//   - <Form>_Layout (w, h): every control placed for a size -- the layout computed at two sizes, each place an
//     affine function of the size (a Column, a Row, a Grid share the room in proportion);
//   - <Form>_Run: the event loop -- a control's event calls your SUB <name>_<Event> (Click, Change), the window's
//     <Form>_Load after it is made, <Form>_Resize after a resize, <Form>_Close before it ends.
// The controls' library (LIBRARY) is BASIC too, PROPERTY over the runtime's words. The app's program: the library,
// the forms' code, your files, then "<Main>_Run" (build_program: the lines of each part kept, an error found back).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _qbstudio_gen_h
#define _qbstudio_gen_h

#include "form.h"

namespace qs {

// The controls' library: the objects the generated code and yours use
static const char *const LIBRARY =
"' QBStudio's controls (made by QBStudio: the objects of a window's controls)\n"
"TYPE Control\n"
"  id AS INTEGER\n"
"  off AS INTEGER\n"
"  hide AS INTEGER\n"
"END TYPE\n"
"PROPERTY Control.Text AS STRING\n"
"  RETURN GETTEXT$ (this.id)\n"
"END PROPERTY\n"
"PROPERTY Control.Text (v AS STRING)\n"
"  SETTEXT this.id, v\n"
"END PROPERTY\n"
"PROPERTY Control.Value AS DOUBLE\n"
"  RETURN VALUE (this.id)\n"
"END PROPERTY\n"
"PROPERTY Control.Value (v AS DOUBLE)\n"
"  SETVALUE this.id, v\n"
"END PROPERTY\n"
"PROPERTY Control.Checked AS INTEGER\n"
"  IF VALUE (this.id) THEN RETURN -1 ELSE RETURN 0\n"
"END PROPERTY\n"
"PROPERTY Control.Checked (v AS INTEGER)\n"
"  SETVALUE this.id, v\n"
"END PROPERTY\n"
"PROPERTY Control.Enabled AS INTEGER\n"
"  IF this.off THEN RETURN 0 ELSE RETURN -1\n"
"END PROPERTY\n"
"PROPERTY Control.Enabled (v AS INTEGER)\n"
"  IF v THEN this.off = 0 ELSE this.off = -1\n"
"  ENABLECONTROL this.id, v\n"
"END PROPERTY\n"
"PROPERTY Control.Visible AS INTEGER\n"
"  IF this.hide THEN RETURN 0 ELSE RETURN -1\n"
"END PROPERTY\n"
"PROPERTY Control.Visible (v AS INTEGER)\n"
"  IF v THEN this.hide = 0 ELSE this.hide = -1\n"
"  SHOWCONTROL this.id, v\n"
"END PROPERTY\n"
"SUB Control.Move (x, y, w, h)\n"
"  MOVECONTROL this.id, x, y, w, h\n"
"END SUB\n"
"SUB Control.Focus\n"
"  FOCUSCONTROL this.id\n"
"END SUB\n"
"SUB Control.AddItem (s AS STRING)\n"
"  SETTEXT this.id, s\n"
"END SUB\n"
"TYPE Window\n"
"  id AS INTEGER\n"
"END TYPE\n"
"PROPERTY Window.Width AS INTEGER\n"
"  RETURN WINDOWWIDTH\n"
"END PROPERTY\n"
"PROPERTY Window.Height AS INTEGER\n"
"  RETURN WINDOWHEIGHT\n"
"END PROPERTY\n"
"SUB Window.Close\n"
"  END\n"
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
	o.printf ("DIM SHARED %s AS Window\n", form);
	for (int i = 0; i < ctl.n; i++) o.printf ("DIM SHARED %s AS Control\n", ctl[i].var);
	o.puts ("\n");
	// _Create
	o.printf ("SUB %s_Create\n  WINDOW ", form); bstr_keep (o, win->hasText ? win->text : form); o.printf (", %d, %d%s\n", W0, H0, win->flag ("resizable") ? ", 1" : "");
	for (int i = 0; i < ctl.n; i++)
	{
		El *e = ctl[i].e; const char *v = ctl[i].var;
		int x = a[4 * i], y = a[4 * i + 1], w = a[4 * i + 2], h = a[4 * i + 3];
		switch (e->kind)
		{
		case K_SEP:
			if (e->parent && e->parent->kind == K_MENUTITLE) { o.printf ("  %s.id = MENUITEM (", v); bstr (o, e->parent->text); o.puts (", \"-\")\n"); }
			continue;
		case K_MENUITEM:
		{
			const El *t = e->parent; if (!t) break;
			o.printf ("  %s.id = MENUITEM (", v); bstr (o, t->text); o.puts (", "); bstr (o, e->text);
			const char *k = e->get ("key"); if (k) { o.puts (", "); bstr_keep (o, k); }
			o.puts (")\n");
			continue;
		}
		case K_LABEL: case K_STATUSBAR: o.printf ("  %s.id = LABEL (%d, %d, %d, %d, ", v, x, y, w, h); bstr_keep (o, e->text); o.puts (")\n"); break;
		case K_BUTTON: o.printf ("  %s.id = BUTTON (%d, %d, %d, %d, ", v, x, y, w, h); bstr_keep (o, e->text); o.puts (")\n"); break;
		case K_TEXTBOX: o.printf ("  %s.id = TEXTBOX (%d, %d, %d, %d, ", v, x, y, w, h); bstr_keep (o, e->text); o.puts (")\n"); break;
		case K_CHECKBOX: o.printf ("  %s.id = CHECKBOX (%d, %d, %d, %d, ", v, x, y, w, h); bstr_keep (o, e->text); o.printf (", %d)\n", e->flag ("checked") ? 1 : 0); break;
		case K_LISTBOX: o.printf ("  %s.id = LISTBOX (%d, %d, %d, %d, ", v, x, y, w, h); bstr_keep (o, e->get ("items") ? e->get ("items") : ""); o.puts (")\n"); break;
		case K_DROPDOWN: o.printf ("  %s.id = DROPDOWN (%d, %d, %d, %d, ", v, x, y, w, h); bstr_keep (o, e->get ("items") ? e->get ("items") : e->text); o.puts (")\n"); break;
		case K_SLIDER: o.printf ("  %s.id = SLIDER (%d, %d, %d, %d, %d)\n", v, x, y, w, h, e->num ("max", 100)); if (e->get ("value")) o.printf ("  SETVALUE %s.id, %d\n", v, e->num ("value", 0)); break;
		case K_PROGRESS: o.printf ("  %s.id = PROGRESS (%d, %d, %d, %d)\n", v, x, y, w, h); if (e->get ("value")) o.printf ("  SETVALUE %s.id, %d\n", v, e->num ("value", 0)); break;
		default: break;
		}
		if (e->flag ("hidden")) o.printf ("  %s.Visible = 0\n", v);
		if (e->flag ("disabled") || e->flag ("readonly")) o.printf ("  %s.Enabled = 0\n", v);
	}
	o.puts ("END SUB\n\n");
	// _Layout
	o.printf ("' the window's layout: where each control goes for a size w x h\nSUB %s_Layout (w, h)\n", form);
	if (minW) o.printf ("  IF w < %d THEN w = %d\n", minW, minW);
	if (minH) o.printf ("  IF h < %d THEN h = %d\n", minH, minH);
	for (int i = 0; i < ctl.n; i++)
	{
		if (ctl[i].e->kind == K_MENUITEM || ctl[i].e->kind == K_SEP) continue;
		o.printf ("  MOVECONTROL %s.id, ", ctl[i].var);
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
	// _Run: the events
	char sn[96];
	o.printf ("' the events -> your SUBs\nSUB %s_Run\n  %s_Create\n", form, form);
	snprintf (sn, sizeof sn, "%s_Load", form); if (has_sub (subs, sn)) o.printf ("  %s\n", sn);
	o.puts ("  DO\n    e = WAITEVENT\n    IF e = -1 THEN\n");
	snprintf (sn, sizeof sn, "%s_Close", form); if (has_sub (subs, sn)) o.printf ("      %s\n", sn);
	o.puts ("      EXIT DO\n    ELSEIF e = -2 THEN\n");
	o.printf ("      %s_Layout WINDOWWIDTH, WINDOWHEIGHT\n", form);
	snprintf (sn, sizeof sn, "%s_Resize", form); if (has_sub (subs, sn)) o.printf ("      %s\n", sn);
	for (int i = 0; i < ctl.n; i++)
	{
		const char *ev = event_of (ctl[i].e->kind); if (!ev) continue;
		snprintf (sn, sizeof sn, "%s_%s", ctl[i].var, ev);
		if (!has_sub (subs, sn)) continue;
		o.printf ("    ELSEIF e = %s.id THEN\n      %s\n", ctl[i].var, sn);
	}
	o.puts ("    END IF\n  LOOP\nEND SUB\n");
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
