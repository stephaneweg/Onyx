// qbstudio_test.cpp -- QBStudio's core on the PC (user/Apps/qbstudio): the sample project's form read, written back the
// same, laid out (its places at its size and resized), its code generated, the whole program compiled by Onyx BASIC's
// compiler and run with a UIKit of the test's own (the functions of uikit/flat.h, by the places the real uikit.bi gives
// them: $UIKIT_BI, made by tools/kitbi/kitbi.py) that clicks "Convert", resizes the window, closes it.
//
//   sh tools/tests/run_qbstudio_test.sh        (SHOW=1: the generated code and the log; DUMP=file: the whole program)
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
#include "Apps/qbstudio/gen.h"
#include "basic/bas.h"
#include <stdio.h>

using namespace qs;
static int g_fail, g_checks;
static void check (bool ok, const char *what) { g_checks++; if (!ok) { g_fail++; printf ("FAIL %s\n", what); } }
static char *slurp (const char *p)
{
	FILE *f = fopen (p, "rb"); if (!f) return 0;
	fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
	char *b = (char *) malloc (n + 1); n = (long) fread (b, 1, n, f); b[n] = 0; fclose (f); return b;
}
// ---- a UIKit for the test: the widgets remembered, what is done to them logged, the events scripted ----------------
static Str g_log;
struct TW { int kind; char text[64]; int value, on, shown; void (*cb) (void *); };
static TW g_w[64]; static int g_nw;
static TW *g_convert; static void (*g_sized) (void *, int, int); static int g_step, g_winW = 380, g_winH = 260;
static TW *tw_new (int kind, const char *t, void (*cb) (void *), int x, int y, int w, int h)
{
	TW *k = &g_w[g_nw++]; k->kind = kind; cpy (k->text, t ? t : "", 64); k->value = 1; k->on = k->shown = 1; k->cb = cb;
	g_log.printf ("[%s %d,%d %dx%d \"%s\"%s]\n", kind == 1 ? "button" : kind == 2 ? "label" : kind == 3 ? "textbox" : kind == 4 ? "checkbox" : kind == 8 ? "slider" : "widget", x, y, w, h, k->text, cb ? " +event" : "");
	if (kind == 1 && !strcmp (k->text, "Convert")) g_convert = k;
	return k;
}
extern "C" {
static void *t_window (const char *t, int w, int h, int flags) { g_log.printf ("[window %s %dx%d flags %d]\n", t, w, h, flags); return tw_new (9, t, 0, 0, 0, w, h); }
static int t_window_wait (void *)
{
	g_step++;
	if (g_step == 1 && g_convert && g_convert->cb) g_convert->cb (g_convert);		// a click on Convert
	else if (g_step == 2 && g_sized) { g_winW = 500; g_winH = 300; g_sized (&g_w[0], 500, 300); }	// the window resized
	return g_step < 3;
}
static void t_window_close (void *) { g_log.puts ("[close]\n"); g_step = 99; }
static int t_window_width (void *) { return g_winW; }
static int t_window_height (void *) { return g_winH; }
static void t_window_min_size (void *, int w, int h) { g_log.printf ("[min %dx%d]\n", w, h); }
static void t_window_on_resize (void *, void (*fn) (void *, int, int)) { g_sized = fn; }
static void *t_label (void *, int x, int y, int w, int h, const char *t) { return tw_new (2, t, 0, x, y, w, h); }
static void *t_button (void *, int x, int y, int w, int h, const char *t, void (*cb) (void *)) { return tw_new (1, t, cb, x, y, w, h); }
static void *t_textbox (void *, int x, int y, int w, int h, const char *t, void (*cb) (void *)) { return tw_new (3, t, cb, x, y, w, h); }
static void *t_checkbox (void *, int x, int y, int w, int h, const char *t, int on, void (*cb) (void *)) { TW *k = tw_new (4, t, cb, x, y, w, h); k->value = on; return k; }
static void *t_listbox (void *, int x, int y, int w, int h, const char *t, void (*cb) (void *)) { return tw_new (5, t, cb, x, y, w, h); }
static void *t_dropdown (void *, int x, int y, int w, int h, const char *t, void (*cb) (void *)) { return tw_new (6, t, cb, x, y, w, h); }
static void *t_slider (void *, int x, int y, int w, int h, int max, int v, void (*cb) (void *)) { TW *k = tw_new (8, "", cb, x, y, w, h); k->value = v; (void) max; return k; }
static void *t_progress (void *, int x, int y, int w, int h, int max, int v) { TW *k = tw_new (7, "", 0, x, y, w, h); k->value = v; (void) max; return k; }
static void t_set_text (void *w, const char *t) { TW *k = (TW *) w; if (!k) return; cpy (k->text, t, 64); g_log.printf ("[settext %d \"%s\"]\n", (int) (k - g_w), t); }
static const char *t_get_text (void *w) { return w ? ((TW *) w)->text : ""; }
static void t_set_value (void *w, int v) { TW *k = (TW *) w; if (!k) return; k->value = v; g_log.printf ("[setvalue %d %d]\n", (int) (k - g_w), v); }
static int t_get_value (void *w) { return w ? ((TW *) w)->value : 0; }
static void t_move (void *w, int x, int y, int ww, int h) { g_log.printf ("[move %d %d,%d %dx%d]\n", (int) ((TW *) w - g_w), x, y, ww, h); }
static void t_show (void *w, int on) { if (w) ((TW *) w)->shown = on; }
static void t_enable (void *w, int on) { if (w) ((TW *) w)->on = on; g_log.printf ("[enable %d %d]\n", (int) ((TW *) w - g_w), on); }
static int t_shown (void *w) { return w ? ((TW *) w)->shown : 0; }
static int t_enabled (void *w) { return w ? ((TW *) w)->on : 0; }
static void t_focus (void *w) { g_log.printf ("[focus %d]\n", (int) ((TW *) w - g_w)); }
static void t_add_item (void *w, const char *t) { g_log.printf ("[additem %d \"%s\"]\n", (int) ((TW *) w - g_w), t); }
static void t_clear_items (void *) {}
static int t_item_count (void *) { return 0; }
static void t_menu_item (void *, const char *t, const char *i, const char *k, void (*cb) (void)) { g_log.printf ("[menu %s/%s %s%s]\n", t, i, k, cb ? " +event" : ""); }
}
static const struct { const char *name; void *fn; } T_UIKIT[] = {
	{ "uk_window", (void *) t_window }, { "uk_window_wait", (void *) t_window_wait }, { "uk_window_close", (void *) t_window_close },
	{ "uk_window_width", (void *) t_window_width }, { "uk_window_height", (void *) t_window_height }, { "uk_window_min_size", (void *) t_window_min_size },
	{ "uk_window_on_resize", (void *) t_window_on_resize }, { "uk_label", (void *) t_label }, { "uk_button", (void *) t_button },
	{ "uk_textbox", (void *) t_textbox }, { "uk_checkbox", (void *) t_checkbox }, { "uk_listbox", (void *) t_listbox }, { "uk_dropdown", (void *) t_dropdown },
	{ "uk_slider", (void *) t_slider }, { "uk_progress", (void *) t_progress }, { "uk_set_text", (void *) t_set_text }, { "uk_get_text", (void *) t_get_text },
	{ "uk_set_value", (void *) t_set_value }, { "uk_get_value", (void *) t_get_value }, { "uk_move", (void *) t_move }, { "uk_show", (void *) t_show },
	{ "uk_enable", (void *) t_enable }, { "uk_shown", (void *) t_shown }, { "uk_enabled", (void *) t_enabled }, { "uk_focus", (void *) t_focus },
	{ "uk_add_item", (void *) t_add_item }, { "uk_clear_items", (void *) t_clear_items }, { "uk_item_count", (void *) t_item_count },
	{ "uk_menu_item", (void *) t_menu_item }, { 0, 0 } };
// The kit's description (the real uikit.bi) and, from it, the test's functions at the places it gives them
static char *g_bi; static void *g_table[2048];
static char *kit_source (const char *name, int *len)
{
	if (strcmp (name, "uikit") != 0 || !g_bi) return 0;
	*len = (int) strlen (g_bi);
	char *b = new char[*len + 1]; memcpy (b, g_bi, *len + 1);
	return b;
}
static bool kit_table ()
{
	int found = 0;
	for (const char *l = g_bi; l && *l; l = strchr (l, '\n') ? strchr (l, '\n') + 1 : 0)
	{
		char n[64], r[8], a[32], c[64]; int slot;
		if (sscanf (l, "%63s %d %7s %31s %63s", n, &slot, r, a, c) != 5 || slot < 0 || slot >= 2048) continue;
		for (int i = 0; T_UIKIT[i].name; i++) if (!strcmp (T_UIKIT[i].name, c)) { g_table[slot] = T_UIKIT[i].fn; found++; }
	}
	int want = 0; while (T_UIKIT[want].name) want++;
	return found == want;
}
struct Host : bas::Host
{
	void out (const char *s, int n) override { g_log.putn (s, n); }
	int inputLine (char *b, int) override { b[0] = 0; return -1; }
	void *const *kitOpen (const char *name, int, char *, int) override { return strcmp (name, "uikit") == 0 ? g_table : 0; }
	void time (char *o) override { strcpy (o, "12:00:00"); }
};

int main (int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "sdcard/projects/converter";
	char p[300]; snprintf (p, sizeof p, "%s/Main.form", dir);
	char *src = slurp (p); char *code;
	snprintf (p, sizeof p, "%s/Main.bas", dir); code = slurp (p);
	check (src && code, "the sample project read");
	if (!src || !code) return 1;
	g_bi = getenv ("UIKIT_BI") ? slurp (getenv ("UIKIT_BI")) : 0;
	check (g_bi != 0, "UIKit's description read ($UIKIT_BI)");
	if (!g_bi) return 1;
	check (kit_table (), "every function of the test's UIKit is in the description");
	bas::setKitSource (kit_source);
	// BASIC's words: no control named so
	static char words[8192]; bas::wordList (words, sizeof words);
	g_isWord = [] (const char *n) -> bool { const char *p = words; while (*p) { const char *e = p; while (*e && *e != ' ') e++; if ((int) strlen (n) == e - p && !strncasecmp (n, p, e - p)) return true; p = *e ? e + 1 : e; } return false; };
	{ Form bad; form_read (bad, "Window W\n  Button clear \"Clear\"\n"); check (bad.errors.n == 1 && strstr (bad.errors[0].msg, "word of BASIC"), "a control named as a word of BASIC: an error"); }
	Form f;
	check (form_read (f, src), "the form read without errors");
	for (int i = 0; i < f.errors.n; i++) printf ("  line %d: %s\n", f.errors[i].line, f.errors[i].msg);
	// written back: the same text
	Str w; form_write (w, f, "Main.form");
	check (!strcmp (w.str (), src), "the form written back the same");
	if (strcmp (w.str (), src)) printf ("--- written:\n%s", w.str ());
	// the layout
	int W, H; form_size (f, &W, &H);
	check (W == 380 && H == 260, "the window's size");
	form_layout (f, W, H);
	El *cv = f.named ("convert"), *ce = f.named ("celsius"), *st = f.named ("status"), *sc = f.named ("scale");
	check (cv && cv->x + cv->w == W - 14 && cv->y + cv->h == H - 22 - 14, "Convert: at the bottom right (above the status bar)");
	check (ce && ce->x == 14 + 90 + 8 && ce->x + ce->w == W - 14, "Celsius' box: after its label, to the right edge");
	check (st && st->y == H - 22 && st->w == W, "the status bar: the bottom line");
	check (sc && sc->w == W - 28, "the slider: the whole width");
	form_layout (f, W + 100, H + 50);
	check (cv && cv->x + cv->w == W + 100 - 14 && ce && ce->x + ce->w == W + 100 - 14, "resized: the right edge followed");
	// the code
	Vec<char *> subs; scan_subs (code, subs);
	check (subs.n == 7, "the SUBs of Main.bas found");
	Str gen; generate (gen, f, "Main", subs);
	if (getenv ("SHOW")) printf ("%s", gen.str ());
	check (strstr (gen.str (), "SUB Main_Layout (w, h)") && strstr (gen.str (), "ADDRESSOF (convert_Click)") && strstr (gen.str (), "UIKit.window_wait (Main.handle)"), "the window's code generated");
	Str prog; Vec<Part> parts;
	add_part (prog, parts, "(QBStudio's controls)", LIBRARY);
	add_part (prog, parts, "Main.form.bas", gen.str ());
	add_part (prog, parts, "Main.bas", code);
	add_part (prog, parts, "(the start)", "Main_Run\n");
	if (getenv ("DUMP")) { FILE *df = fopen (getenv ("DUMP"), "wb"); if (df) { fwrite (prog.str (), 1, strlen (prog.str ()), df); fclose (df); } }	// (the whole program, to try elsewhere)
	bas::Error err;
	bas::Program *pg = bas::compile (prog.str (), &err);
	if (!pg) { const char *file = "?"; int at = 0; part_of (parts, err.line, &file, &at); printf ("  compile error: %s line %d: %s\n", file, at, err.msg); }
	check (pg != 0, "the program compiled");
	if (pg)
	{
		Host h; bas::Error rerr;
		int r = bas::run (pg, h, &rerr);
		if (r) printf ("  runtime error line %d: %s\n", rerr.line, rerr.msg);
		if (getenv ("SHOW")) printf ("%s", g_log.str ());
		check (r == 0, "the program ran");
		check (strstr (g_log.str (), "[window ") != 0 && strstr (g_log.str (), "\"Convert\" +event]") != 0, "the window and its controls made, Convert with its event");
		check (strstr (g_log.str (), " \" 68\"]") != 0, "Convert clicked: 20 C = 68 F");
		check (strstr (g_log.str (), "[move ") != 0, "resized: the controls moved");
		check (strstr (g_log.str (), "[menu File/Quit Ctrl+Q +event]") != 0, "the menus made");
		bas::destroy (pg);
	}
	for (int i = 0; i < subs.n; i++) free (subs[i]);
	free (src); free (code);
	printf ("%d checks, %d failed\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
