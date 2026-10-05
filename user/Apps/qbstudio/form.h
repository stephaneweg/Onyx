//
// form.h -- QBStudio's forms: the .form text (a control a line, its parent given by the indentation; then its name,
// its "text", its key=value properties, its flags) read into a tree of elements and written back; the layout of a
// window (Column / Row / Grid / Group / Spacer / Canvas, a toolbar, a status bar) computed for a size.
//
//   Window Main "Temperature converter" size=380x260 min=320x220 resizable
//     Menu
//       "&File"
//         "&Quit" name=mnuQuit key=Ctrl+Q
//     Column padding=14 gap=10
//       Row gap=8
//         Label "Celsius:" width=90
//         TextBox celsius "20" fill
//       Spacer
//       Row align=right
//         Button convert "Convert" default
//     StatusBar status "Ready"
//
// The layout's rules: a Column stacks its children down, a Row across; "fill" takes the width (in a Row: the room
// left, shared by grow=n; in a Column: the whole width); a Spacer (or grow=n in a Column) takes the free height;
// a container (Row, Grid, Group...) in a Column takes the whole width; align= left / center / right packs a Row's
// children; a Grid places its children in cols=n columns (cell=c,r), its columns as wide as their widest (one
// with a "fill" child takes the room left); a Group frames a Column with its title; a Canvas places its children at
// at=x,y (size=WxH). Every position is an affine function of the window's size: the generator (gen.h) computes the
// layout at two sizes and writes the code that places the controls for any.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _qbstudio_form_h
#define _qbstudio_form_h

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

namespace qs {

// ---- a growing array ---------------------------------------------------------------------------------------------
template <class T> struct Vec
{
	T *a; int n, cap;
	Vec () : a (0), n (0), cap (0) {}
	~Vec () { free (a); }
	void reserve (int m) { if (m <= cap) return; int c = cap ? cap * 2 : 8; while (c < m) c *= 2; a = (T *) realloc (a, sizeof (T) * c); cap = c; }
	void push (const T &v) { reserve (n + 1); a[n++] = v; }
	void insert (int i, const T &v) { reserve (n + 1); memmove (a + i + 1, a + i, sizeof (T) * (n - i)); a[i] = v; n++; }
	void erase (int i) { memmove (a + i, a + i + 1, sizeof (T) * (n - i - 1)); n--; }
	void clear () { n = 0; }
	T &operator[] (int i) { return a[i]; }
	const T &operator[] (int i) const { return a[i]; }
	int find (const T &v) const { for (int i = 0; i < n; i++) if (a[i] == v) return i; return -1; }
private:
	Vec (const Vec &);
	Vec &operator= (const Vec &);
};
// A growing text
struct Str
{
	char *b; int n, cap;
	Str () : b (0), n (0), cap (0) {}
	~Str () { free (b); }
	void need (int m) { if (m + 1 <= cap) return; int c = cap ? cap * 2 : 256; while (c < m + 1) c *= 2; b = (char *) realloc (b, c); cap = c; }
	void put (char c) { need (n + 1); b[n++] = c; b[n] = 0; }
	void puts (const char *s) { int l = (int) strlen (s); need (n + l); memcpy (b + n, s, l); n += l; b[n] = 0; }
	void putn (const char *s, int l) { need (n + l); memcpy (b + n, s, l); n += l; b[n] = 0; }
	void printf (const char *f, ...) __attribute__ ((format (printf, 2, 3)));
	void clear () { n = 0; if (b) b[0] = 0; }
	const char *str () const { return b ? b : ""; }
private:
	Str (const Str &);
	Str &operator= (const Str &);
};
}
#include <stdarg.h>
namespace qs {
inline void Str::printf (const char *f, ...)
{
	char t[1024];
	va_list ap; va_start (ap, f); int l = vsnprintf (t, sizeof t, f, ap); va_end (ap);
	if (l > (int) sizeof t - 1) l = (int) sizeof t - 1;
	if (l > 0) putn (t, l);
}
static inline void cpy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static inline bool ieq (const char *a, const char *b)
{
	for (; *a && *b; a++, b++) { char x = *a >= 'a' && *a <= 'z' ? (char) (*a - 32) : *a, y = *b >= 'a' && *b <= 'z' ? (char) (*b - 32) : *b; if (x != y) return false; }
	return *a == *b;
}
static inline int imin (int a, int b) { return a < b ? a : b; }
static inline int imax (int a, int b) { return a > b ? a : b; }

// ---- the elements ------------------------------------------------------------------------------------------------
enum Kind
{
	K_WINDOW, K_MENU, K_MENUTITLE, K_MENUITEM, K_SEP,
	K_COLUMN, K_ROW, K_GRID, K_GROUP, K_SPACER, K_CANVAS,
	K_LABEL, K_BUTTON, K_TEXTBOX, K_CHECKBOX, K_LISTBOX, K_DROPDOWN, K_SLIDER, K_PROGRESS,
	K_TOOLBAR, K_STATUSBAR, K_COMMENT,
	K_HOST,						// an area a user control is shown in (host.Content = Settings)
	K_COUNT
};
static const char *const KIND_NAMES[K_COUNT] = { "Window", "Menu", "", "", "-", "Column", "Row", "Grid", "Group", "Spacer", "Canvas",
	"Label", "Button", "TextBox", "CheckBox", "ListBox", "DropDown", "Slider", "Progress", "ToolBar", "StatusBar", "#", "Host" };
static inline bool is_container (int k) { return k == K_WINDOW || k == K_COLUMN || k == K_ROW || k == K_GRID || k == K_GROUP || k == K_CANVAS || k == K_TOOLBAR; }
static inline bool is_control (int k) { return (k >= K_LABEL && k <= K_PROGRESS) || k == K_STATUSBAR || k == K_HOST; }
// What takes the room it is given, as a container does (a Host: the user control it shows fills it)
static inline bool is_area (int k) { return is_container (k) || k == K_HOST; }
// The flags each kind knows (a word alone)
static const char *const FLAGS[] = { "fill", "readonly", "checked", "resizable", "default", "cancel", "hidden", "disabled", 0 };
static bool is_flag (const char *w) { for (int i = 0; FLAGS[i]; i++) if (ieq (w, FLAGS[i])) return true; return false; }

struct Prop { char key[24]; char val[96]; };
struct El
{
	int kind;
	char name[32];
	char text[128]; bool hasText;
	bool uc;				// the root of a user control's form ("UserControl": a panel of controls, shown in a Host)
	Vec<Prop> props;
	Vec<El *> kids;
	El *parent;
	int line;				// in the text it was read from (1-based)
	int x, y, w, h;				// the layout's (the window's client coordinates)
	El () : kind (K_LABEL), hasText (false), uc (false), parent (0), line (0), x (0), y (0), w (0), h (0) { name[0] = 0; text[0] = 0; }
	~El () { for (int i = 0; i < kids.n; i++) delete kids[i]; }
	const char *get (const char *k) const { for (int i = 0; i < props.n; i++) if (ieq (props[i].key, k)) return props[i].val; return 0; }
	int num (const char *k, int def) const { const char *v = get (k); return v ? atoi (v) : def; }
	bool flag (const char *f) const { const char *v = get (f); return v && !v[0]; }	// (a flag: a property with no value)
	void set (const char *k, const char *v)
	{
		for (int i = 0; i < props.n; i++) if (ieq (props[i].key, k)) { if (!v) props.erase (i); else cpy (props[i].val, v, sizeof props[i].val); return; }
		if (!v) return;
		Prop p; cpy (p.key, k, sizeof p.key); cpy (p.val, v, sizeof p.val); props.push (p);
	}
	void setFlag (const char *f, bool on) { set (f, on ? "" : 0); }
	void setNum (const char *k, int v) { char t[16]; snprintf (t, sizeof t, "%d", v); set (k, t); }
	// "380x260" -> 380, 260
	bool size2 (const char *k, int *a, int *b) const { const char *v = get (k); if (!v) return false; *a = atoi (v); const char *x = strchr (v, 'x'); if (!x) x = strchr (v, ','); *b = x ? atoi (x + 1) : *a; return true; }
	void add (El *k, int at = -1) { k->parent = this; if (at < 0 || at > kids.n) kids.push (k); else kids.insert (at, k); }
	int index () const { return parent ? parent->kids.find ((El *) this) : -1; }
};

static int kind_of (const char *w)
{
	for (int k = 0; k < K_COUNT; k++) if (KIND_NAMES[k][0] && ieq (w, KIND_NAMES[k])) return k;
	if (ieq (w, "Stack") || ieq (w, "VBox")) return K_COLUMN;
	if (ieq (w, "HBox")) return K_ROW;
	return -1;
}

// ---- reading -------------------------------------------------------------------------------------------------------
struct FormError { int line; char msg[96]; };
struct Form
{
	El *root;					// the Window -- or a user control's root (root->uc), read and laid out as one
	bool userControl () const { return root && root->uc; }
	char header[128];				// the comment before it (written back)
	Vec<FormError> errors;
	Form () : root (0) { header[0] = 0; }
	~Form () { delete root; }
	void clear () { delete root; root = 0; errors.clear (); header[0] = 0; }
	void error (int line, const char *m) { FormError e; e.line = line; cpy (e.msg, m, sizeof e.msg); errors.push (e); }
	// The element named n (the tree searched), else 0
	El *named (const char *n, El *from = 0) const
	{
		if (!from) from = root;
		if (!from || !n || !n[0]) return 0;
		if (ieq (from->name, n)) return from;
		for (int i = 0; i < from->kids.n; i++) { El *e = named (n, from->kids[i]); if (e) return e; }
		return 0;
	}
};

// One line's words: "quoted text" (with "" for a quote), key=value (the value quoted or not), words
static int words (const char *s, char out[][128], int max)
{
	int n = 0;
	while (*s && n < max)
	{
		while (*s == ' ' || *s == '\t') s++;
		if (!*s) break;
		int k = 0; bool q = false;
		while (*s && (q || (*s != ' ' && *s != '\t')))
		{
			if (*s == '"')
			{
				if (q && s[1] == '"') { if (k < 126) out[n][k++] = '"'; s += 2; continue; }
				q = !q; if (k < 126) out[n][k++] = '"'; s++; continue;
			}
			if (k < 126) out[n][k++] = *s;
			s++;
		}
		out[n][k] = 0; n++;
	}
	return n;
}
static void unquote (const char *w, char *out, int cap)
{
	int k = 0;
	if (*w == '"') w++;
	for (; *w && k < cap - 1; w++) { if (*w == '"' && !w[1]) break; out[k++] = *w; }
	out[k] = 0;
}

// A word of the language (no control may be named so): the app gives BASIC's list
static bool (*g_isWord) (const char *name);
static bool form_read (Form &f, const char *src)
{
	f.clear ();
	El *stack[64]; int indent[64]; int sp = 0;
	int line = 0;
	for (const char *p = src; *p; )
	{
		const char *e = p; while (*e && *e != '\n') e++;
		line++;
		char ln[512]; int L = imin ((int) (e - p), 511); memcpy (ln, p, L); ln[L] = 0;
		if (L && ln[L - 1] == '\r') ln[--L] = 0;
		p = *e ? e + 1 : e;
		int ind = 0, i = 0;
		while (ln[i] == ' ' || ln[i] == '\t') { ind += ln[i] == '\t' ? 2 : 1; i++; }
		const char *body = ln + i;
		if (!*body) continue;
		El *el = new El; el->line = line;
		if (*body == '#')
		{
			el->kind = K_COMMENT; cpy (el->text, body + 1, sizeof el->text);
		}
		else
		{
			char w[24][128]; int nw = words (body, w, 24);
			int wi = 0;
			if (w[0][0] == '"')
			{
				// a menu's title (under Menu) or one of its items (under a title)
				El *par = sp ? stack[sp - 1] : 0;
				while (sp && indent[sp - 1] >= ind) { sp--; par = sp ? stack[sp - 1] : 0; }
				el->kind = par && par->kind == K_MENU ? K_MENUTITLE : K_MENUITEM;
				unquote (w[0], el->text, sizeof el->text); el->hasText = true; wi = 1;
			}
			else if (!strcmp (w[0], "-")) { el->kind = K_SEP; wi = 1; }
				else if (ieq (w[0], "UserControl")) { el->kind = K_WINDOW; el->uc = true; wi = 1; }
			else
			{
				int k = kind_of (w[0]);
				if (k < 0) { char m[96]; snprintf (m, sizeof m, "Unknown element: %.40s", w[0]); f.error (line, m); k = K_LABEL; }
				el->kind = k; wi = 1;
			}
			for (; wi < nw; wi++)
			{
				char *t = w[wi];
				char *eq = 0; bool inq = false;
				for (char *c = t; *c; c++) { if (*c == '"') inq = !inq; else if (*c == '=' && !inq) { eq = c; break; } }
				if (t[0] == '"' && !el->hasText) { unquote (t, el->text, sizeof el->text); el->hasText = true; }
				else if (eq)
				{
					*eq = 0; char v[96]; unquote (eq + 1, v, sizeof v);
					if (ieq (t, "name")) cpy (el->name, v, sizeof el->name); else el->set (t, v);
				}
				else if (is_flag (t)) el->setFlag (t, true);
				else if (!el->name[0] && !el->hasText) cpy (el->name, t, sizeof el->name);
				else { char m[96]; snprintf (m, sizeof m, "Unknown word: %.40s", t); f.error (line, m); }
			}
		}
		if (el->name[0] && g_isWord && g_isWord (el->name)) { char m[96]; snprintf (m, sizeof m, "%.30s is a word of BASIC: another name, please", el->name); f.error (line, m); }
		// its parent: the last line less indented
		while (sp && indent[sp - 1] >= ind) sp--;
		if (!sp)
		{
			if (el->kind == K_COMMENT && !f.root) { if (!f.header[0]) cpy (f.header, el->text, sizeof f.header); delete el; continue; }	// (the header)
			if (f.root || el->kind != K_WINDOW) { f.error (line, f.root ? "A second window: one form, one window" : "The form starts with a Window (or a UserControl)"); delete el; continue; }
			f.root = el;
		}
		else
		{
			El *par = stack[sp - 1];
			bool ok = el->kind == K_COMMENT || is_container (par->kind) || par->kind == K_MENU || par->kind == K_MENUTITLE;
			if (par->kind == K_MENU && el->kind != K_MENUTITLE && el->kind != K_COMMENT) ok = false;
			if (!ok) { char m[96]; snprintf (m, sizeof m, "A %s holds no elements", KIND_NAMES[par->kind]); f.error (line, m); delete el; continue; }
			par->add (el);
		}
		if (el->kind != K_COMMENT && sp < 64) { stack[sp] = el; indent[sp] = ind; sp++; }
	}
	if (!f.root) { f.error (1, "The form starts with a Window (or a UserControl)"); return false; }
	return f.errors.n == 0;
}

// ---- writing ---------------------------------------------------------------------------------------------------------
static void quoted (Str &o, const char *s) { o.put ('"'); for (; *s; s++) { if (*s == '"') o.put ('"'); o.put (*s); } o.put ('"'); }
static bool plain_value (const char *v) { if (!*v) return false; for (; *v; v++) if (*v == ' ' || *v == '"' || *v == '=') return false; return true; }
static void el_write (Str &o, const El *e, int depth)
{
	for (int i = 0; i < depth; i++) o.puts ("  ");
	if (e->kind == K_COMMENT) { o.put ('#'); o.puts (e->text); o.put ('\n'); return; }
	bool first = true;
	if (e->kind == K_MENUTITLE || e->kind == K_MENUITEM) { quoted (o, e->text); first = false; }
	else { o.puts (e->uc ? "UserControl" : KIND_NAMES[e->kind]); first = false; }
	if (e->name[0] && (e->kind == K_MENUITEM || e->kind == K_MENUTITLE)) { o.puts (" name="); o.puts (e->name); }
	else if (e->name[0]) { o.put (' '); o.puts (e->name); }
	if (e->hasText && e->kind != K_MENUTITLE && e->kind != K_MENUITEM) { o.put (' '); quoted (o, e->text); }
	for (int i = 0; i < e->props.n; i++)
	{
		const Prop &p = e->props[i];
		if (!p.val[0]) continue;
		o.put (' '); o.puts (p.key); o.put ('=');
		if (plain_value (p.val)) o.puts (p.val); else quoted (o, p.val);
	}
	for (int i = 0; i < e->props.n; i++) if (!e->props[i].val[0]) { o.put (' '); o.puts (e->props[i].key); }
	(void) first;
	o.put ('\n');
	for (int i = 0; i < e->kids.n; i++) el_write (o, e->kids[i], depth + 1);
}
static void form_write (Str &o, const Form &f, const char *file)
{
	if (f.header[0]) o.printf ("#%s\n", f.header);
	else o.printf ("# %s -- %s's %s (QBStudio writes it; it reads as you see it)\n", file, f.root && f.root->name[0] ? f.root->name : "the", f.userControl () ? "controls" : "window");
	if (f.root) el_write (o, f.root, 0);
}

// ---- the layout ----------------------------------------------------------------------------------------------------------
// The width of a text in the controls' font (the app gives the real one; 7 px a character otherwise)
static int (*g_textW) (const char *s);
static int text_w (const char *s) { if (g_textW) return g_textW (s); return (int) strlen (s) * 7; }

static const int MENU_H = 22, TOOLBAR_H = 34, STATUS_H = 22;
// An element's own size (its text, its kind), before the room is shared
static void natural (const El *e, int *w, int *h)
{
	const char *t = e->text;
	int tw = text_w (t);
	switch (e->kind)
	{
	case K_LABEL: *w = tw + 4; *h = 22; break;
	case K_BUTTON: *w = imax (tw + 28, 80); *h = 30; break;
	case K_TEXTBOX: *w = 160; *h = 26; break;
	case K_CHECKBOX: *w = tw + 30; *h = 22; break;
	case K_LISTBOX: *w = 160; *h = 120; break;
	case K_DROPDOWN: *w = 160; *h = 26; break;
	case K_SLIDER: *w = 160; *h = 24; break;
	case K_PROGRESS: *w = 160; *h = 18; break;
	case K_STATUSBAR: *w = tw; *h = STATUS_H; break;
	case K_HOST: *w = 80; *h = 60; break;
	default: *w = 0; *h = 0; break;
	}
	int a, b;
	if (e->size2 ("size", &a, &b)) { *w = a; *h = b; }
	*w = e->num ("width", *w); *h = e->num ("height", *h);
}
static int pad_of (const El *e) { return e->num ("padding", e->kind == K_GROUP ? 10 : e->kind == K_TOOLBAR ? 2 : 0); }
static int gap_of (const El *e) { return e->num ("gap", e->kind == K_ROW || e->kind == K_TOOLBAR ? 8 : e->kind == K_COLUMN || e->kind == K_GROUP ? 8 : 6); }
static int grow_of (const El *e) { return e->kind == K_SPACER ? imax (1, e->num ("grow", 1)) : e->num ("grow", 0); }
// An element's place across its container: halign = left | right | center | stretch (align: the same, the older
// word), valign = top | bottom | center | stretch. Not said: the container's habit.
enum { AL_NONE = 0, AL_START, AL_END, AL_CENTER, AL_STRETCH };
static int align_word (const char *v)
{
	if (!v || !v[0]) return AL_NONE;
	if (ieq (v, "left") || ieq (v, "top")) return AL_START;
	if (ieq (v, "right") || ieq (v, "bottom")) return AL_END;
	if (ieq (v, "center") || ieq (v, "centre")) return AL_CENTER;
	if (ieq (v, "stretch")) return AL_STRETCH;
	return AL_NONE;
}
static int halign_of (const El *e) { int a = align_word (e->get ("halign")); return a ? a : align_word (e->get ("align")); }
static int valign_of (const El *e) { return align_word (e->get ("valign")); }
// size in room by an alignment -> where it starts and how long it is (stretch: the whole room)
static void aligned (int al, int start, int room, int size, int *at, int *len)
{
	if (al == AL_STRETCH) { *at = start; *len = room; return; }
	*len = imin (size, room);
	*at = al == AL_END ? start + room - *len : al == AL_CENTER ? start + (room - *len) / 2 : start;
}
// A Grid's columns (widths=) and rows (heights=): "200,*,2*,auto" -- pixels, a share of what is left (*: one
// share, 2*: two), auto (what the cells need: as when nothing is said). -> how many were read.
struct Track { int px; double star; };			// px >= 0: fixed; star > 0: shares; else auto
static int tracks_of (const char *v, Track *t, int max)
{
	int n = 0;
	for (const char *p = v; p && *p && n < max; )
	{
		while (*p == ' ' || *p == ',') p++;
		if (!*p) break;
		const char *e = p; while (*e && *e != ',' && *e != ' ') e++;
		t[n].px = -1; t[n].star = 0;
		if (e[-1] == '*') t[n].star = e - p > 1 ? atof (p) : 1;
		else if (*p >= '0' && *p <= '9') t[n].px = atoi (p);
		if (t[n].star < 0) t[n].star = 0;
		n++; p = e;
	}
	return n;
}
// A Row's widths= and a Column's heights=, as a Grid's: the size of its i-th child along it. Pixels: that size,
// whatever the child's own; *: a share of the room left and nothing of its own; auto, or nothing said for it:
// the child's own size and its own grow / fill.
static void along (const El *par, int i, bool placing, int own, double ownGrow, int *size, double *grow)
{
	Track t[32];
	int n = tracks_of (par->get (par->kind == K_ROW || par->kind == K_TOOLBAR ? "widths" : "heights"), t, 32);
	*size = own; *grow = ownGrow;
	if (i >= n) return;
	if (t[i].px >= 0) { *size = t[i].px; *grow = 0; }
	else if (t[i].star > 0) { if (placing) *size = 0; *grow = t[i].star; }
}
static void measure (const El *e, int *w, int *h);
// The minimum size of a container (its children at their sizes)
static void measure (const El *e, int *w, int *h)
{
	if (!is_container (e->kind) || e->kind == K_WINDOW) { natural (e, w, h); return; }
	int pad = pad_of (e), gap = gap_of (e), mw = 0, mh = 0, n = 0;
	int top = e->kind == K_GROUP ? 18 : 0;
	if (e->kind == K_COLUMN || e->kind == K_GROUP)
	{
		for (int i = 0; i < e->kids.n; i++) { const El *k = e->kids[i]; if (k->kind == K_COMMENT) continue; int a, b; measure (k, &a, &b); double g; along (e, n, false, b, 0, &b, &g); mw = imax (mw, a); mh += b + (n ? gap : 0); n++; }
	}
	else if (e->kind == K_ROW || e->kind == K_TOOLBAR)
	{
		for (int i = 0; i < e->kids.n; i++) { const El *k = e->kids[i]; if (k->kind == K_COMMENT) continue; int a, b; measure (k, &a, &b); double g; along (e, n, false, a, 0, &a, &g); mh = imax (mh, b); mw += a + (n ? gap : 0); n++; }
	}
	else if (e->kind == K_GRID)
	{
		int cols = imax (1, e->num ("cols", 2)); int cw[16] = { 0 }, rh[64] = { 0 }, nr = 0, idx = 0;
		{ Track t0[16]; int n0 = tracks_of (e->get ("widths"), t0, 16); if (n0 > cols || (n0 > 0 && !e->get ("cols"))) cols = n0; }	// (widths= says how many columns)
		for (int i = 0; i < e->kids.n; i++)
		{
			const El *k = e->kids[i]; if (k->kind == K_COMMENT) continue;
			int c = idx % cols, r = idx / cols; idx++;
			int cc, rr; if (k->size2 ("cell", &cc, &rr)) { c = cc; r = rr; }
			if (c > 15 || r > 63) continue;
			int a, b; measure (k, &a, &b); cw[c] = imax (cw[c], a); rh[r] = imax (rh[r], b); nr = imax (nr, r + 1);
		}
		Track tw[16], th[64]; int ntw = tracks_of (e->get ("widths"), tw, 16), nth = tracks_of (e->get ("heights"), th, 64);
		cols = imax (cols, ntw); nr = imin (64, imax (nr, nth));
		for (int c = 0; c < ntw; c++) if (tw[c].px >= 0) cw[c] = tw[c].px;
		for (int r = 0; r < nth; r++) if (th[r].px >= 0) rh[r] = th[r].px;
		for (int c = 0; c < cols && c < 16; c++) mw += cw[c] + (c ? gap : 0);
		for (int r = 0; r < nr; r++) mh += rh[r] + (r ? gap : 0);
	}
	else if (e->kind == K_CANVAS) { natural (e, &mw, &mh); }
	int a, b; natural (e, &a, &b);
	*w = imax (a, mw + 2 * pad); *h = imax (b, mh + 2 * pad + top);
}
// Place e's children inside e's box (x, y, w, h already set)
static void place (El *e)
{
	int pad = pad_of (e), gap = gap_of (e);
	int top = e->kind == K_GROUP ? 18 : 0;
	int X = e->x + pad, Y = e->y + pad + top, W = imax (0, e->w - 2 * pad), H = imax (0, e->h - 2 * pad - top);
	Vec<El *> ks; for (int i = 0; i < e->kids.n; i++) if (e->kids[i]->kind != K_COMMENT) ks.push (e->kids[i]);
	if (e->kind == K_COLUMN || e->kind == K_GROUP)
	{
		int used = 0; double grows = 0;
		for (int i = 0; i < ks.n; i++) { int a, b; measure (ks[i], &a, &b); double g; along (e, i, true, b, grow_of (ks[i]), &b, &g); used += b + (i ? gap : 0); grows += g; }
		int free = H - used;
		double share = grows > 0 ? (double) free / grows : 0;
		double y = Y;
		for (int i = 0; i < ks.n; i++)
		{
			El *k = ks[i]; int a, b; measure (k, &a, &b);
			double g; along (e, i, true, b, grow_of (k), &b, &g);
			double hh = b + share * g;
			bool wide = is_area (k->kind) || k->flag ("fill") || k->kind == K_SPACER;
			int al = halign_of (k);
			if (al == AL_NONE || (wide && !align_word (k->get ("halign")))) al = wide ? AL_STRETCH : AL_START;	// (halign said: it wins over the kind's habit)
			int xx, ww; aligned (al, X, W, a, &xx, &ww);
			k->x = xx; k->y = (int) (y + 0.5); k->w = ww; k->h = (int) (hh + 0.5);
			y += hh + gap;
			if (is_container (k->kind)) place (k);
		}
	}
	else if (e->kind == K_ROW || e->kind == K_TOOLBAR)
	{
		// each child's width along the row: its own (width=), a share of what is left (fill, grow=n) -- or what the
		// row's widths= says for it (200,*,100)
		int used = 0; double grows = 0;
		for (int i = 0; i < ks.n; i++)
		{
			int a, b; measure (ks[i], &a, &b);
			double g; along (e, i, true, a, ks[i]->flag ("fill") ? imax (1, ks[i]->num ("grow", 1)) : grow_of (ks[i]), &a, &g);
			used += a + (i ? gap : 0); grows += g;
		}
		int free = W - used;
		double share = grows > 0 ? (double) free / grows : 0;
		const char *al = e->get ("align");
		double x = X;
		if (grows <= 0 && al && ieq (al, "right")) x = X + free;
		else if (grows <= 0 && al && (ieq (al, "center") || ieq (al, "centre"))) x = X + free / 2.0;
		for (int i = 0; i < ks.n; i++)
		{
			El *k = ks[i]; int a, b; measure (k, &a, &b);
			double g; along (e, i, true, a, k->flag ("fill") ? imax (1, k->num ("grow", 1)) : grow_of (k), &a, &g);
			double ww = a + share * g;
			int va = valign_of (k);
			if (va == AL_NONE) va = is_area (k->kind) ? AL_STRETCH : AL_CENTER;
			int yy, hh; aligned (va, Y, H, b, &yy, &hh);
			k->x = (int) (x + 0.5); k->w = (int) (ww + 0.5); k->h = hh; k->y = yy;
			x += ww + gap;
			if (is_container (k->kind)) place (k);
		}
	}
	else if (e->kind == K_GRID)
	{
		int cols = imax (1, imin (16, e->num ("cols", 2)));
		{ Track t0[16]; int n0 = tracks_of (e->get ("widths"), t0, 16); if (n0 > cols || (n0 > 0 && !e->get ("cols"))) cols = n0; }	// (widths= says how many columns)
		int cw[16] = { 0 }, rh[64] = { 0 }, nr = 0; bool cfill[16] = { false };
		int idx = 0;
		int cellC[256], cellR[256];
		for (int i = 0; i < ks.n && i < 256; i++)
		{
			El *k = ks[i];
			int c = idx % cols, r = idx / cols; idx++;
			int cc, rr; if (k->size2 ("cell", &cc, &rr)) { c = imin (cc, 15); r = imin (rr, 63); }
			cellC[i] = c; cellR[i] = r;
			int a, b; measure (k, &a, &b); cw[c] = imax (cw[c], a); rh[r] = imax (rh[r], b); nr = imax (nr, r + 1);
			if (k->flag ("fill")) cfill[c] = true;
		}
		// the columns: widths= (pixels, shares of what is left, auto), else a column with a `fill` cell takes the rest
		Track tw[16], th[64]; int ntw = tracks_of (e->get ("widths"), tw, 16), nth = tracks_of (e->get ("heights"), th, 64);
		cols = imin (16, imax (cols, ntw)); nr = imin (64, imax (nr, nth));
		double cstar[16], rstar[64], stars = 0;
		for (int c = 0; c < cols; c++)
		{
			cstar[c] = c < ntw ? tw[c].star : (ntw ? 0 : (cfill[c] ? 1 : 0));
			if (c < ntw && tw[c].px >= 0) cw[c] = tw[c].px;
			if (c < ntw && cstar[c] > 0) cw[c] = 0;
			stars += cstar[c];
		}
		int used = 0; for (int c = 0; c < cols; c++) used += cw[c] + (c ? gap : 0);
		double extra = stars > 0 ? imax (0, W - used) / stars : 0;
		double cx[17]; cx[0] = X; for (int c = 0; c < cols; c++) cx[c + 1] = cx[c] + cw[c] + cstar[c] * extra + gap;
		// the rows: heights=, the same way (nothing said: what the cells need)
		stars = 0;
		for (int r = 0; r < nr; r++)
		{
			rstar[r] = r < nth ? th[r].star : 0;
			if (r < nth && th[r].px >= 0) rh[r] = th[r].px;
			if (rstar[r] > 0) rh[r] = 0;
			stars += rstar[r];
		}
		used = 0; for (int r = 0; r < nr; r++) used += rh[r] + (r ? gap : 0);
		double extraH = stars > 0 ? imax (0, H - used) / stars : 0;
		double ryd[65]; ryd[0] = Y; for (int r = 0; r < nr; r++) ryd[r + 1] = ryd[r] + rh[r] + rstar[r] * extraH + gap;
		for (int i = 0; i < ks.n && i < 256; i++)
		{
			El *k = ks[i]; int c = cellC[i], r = cellR[i];
			int a, b; measure (k, &a, &b);
			int colW = (int) (cx[c + 1] - cx[c] - gap + 0.5), rowH = (int) (ryd[r + 1] - ryd[r] - gap + 0.5);
			int ha = halign_of (k), va = valign_of (k);
			if (ha == AL_NONE) ha = k->flag ("fill") || is_area (k->kind) ? AL_STRETCH : AL_START;
			if (va == AL_NONE) va = is_area (k->kind) && rstar[r] > 0 ? AL_STRETCH : AL_CENTER;
			aligned (ha, (int) (cx[c] + 0.5), colW, a, &k->x, &k->w);
			aligned (va, (int) (ryd[r] + 0.5), rowH, b, &k->y, &k->h);
			if (is_container (k->kind)) place (k);
		}
	}
	else if (e->kind == K_CANVAS)
	{
		for (int i = 0; i < ks.n; i++)
		{
			El *k = ks[i]; int a, b; measure (k, &a, &b);
			int px = 0, py = 0; k->size2 ("at", &px, &py);
			k->x = e->x + px; k->y = e->y + py; k->w = a; k->h = b;
			if (is_container (k->kind)) place (k);
		}
	}
}
// The window laid out for a client size w x h: the menu (Onyx: the system's bar -- not in the window), the toolbar
// at the top, the status bar at the bottom, the body (the first container, else a Column of the rest) between.
static void form_layout (Form &f, int w, int h, bool menuInWindow = false)
{
	El *win = f.root; if (!win) return;
	win->x = 0; win->y = 0; win->w = w; win->h = h;
	int top = 0, bottom = h;
	for (int i = 0; i < win->kids.n; i++)
	{
		El *k = win->kids[i];
		if (k->kind == K_MENU) { k->x = 0; k->y = 0; k->w = w; k->h = menuInWindow ? MENU_H : 0; if (menuInWindow) top += MENU_H; }
	}
	for (int i = 0; i < win->kids.n; i++)
	{
		El *k = win->kids[i];
		if (k->kind == K_TOOLBAR) { k->x = 0; k->y = top; k->w = w; k->h = TOOLBAR_H; top += TOOLBAR_H; place (k); }
		else if (k->kind == K_STATUSBAR) { bottom -= STATUS_H; k->x = 0; k->y = bottom; k->w = w; k->h = STATUS_H; }
	}
	// the body: the window's other children, as a Column
	El body; body.kind = K_COLUMN; body.set ("gap", "0");
	body.x = 0; body.y = top; body.w = w; body.h = imax (0, bottom - top);
	for (int i = 0; i < win->kids.n; i++) { El *k = win->kids[i]; if (k->kind != K_MENU && k->kind != K_TOOLBAR && k->kind != K_STATUSBAR && k->kind != K_COMMENT) body.kids.push (k); }
	if (body.kids.n == 1 && is_area (body.kids[0]->kind))
	{
		El *k = body.kids[0];
		k->x = body.x; k->y = body.y; k->w = body.w; k->h = body.h;
		place (k);
	}
	else place (&body);
	body.kids.clear ();				// (not its: the window's)
}
// The window's size from the form (size=, else what its contents need)
static void form_size (const Form &f, int *w, int *h)
{
	*w = 360; *h = 240;
	if (!f.root) return;
	if (f.root->size2 ("size", w, h)) return;
	int mw = 0, mh = 0;
	for (int i = 0; i < f.root->kids.n; i++)
	{
		const El *k = f.root->kids[i];
		if (k->kind == K_MENU || k->kind == K_COMMENT) continue;
		int a, b; measure (k, &a, &b); mw = imax (mw, a); mh += b;
	}
	*w = imax (200, mw); *h = imax (120, mh);
}

} // namespace qs

#endif
