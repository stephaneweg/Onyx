// qbstudio_test.cpp -- QBStudio's core on the PC (user/Apps/qbstudio): the sample project's form read, written back the
// same, laid out (its places at its size and resized), its code generated, the whole program compiled by Onyx BASIC's
// compiler and run on a host that clicks "Convert", resizes the window, closes it.
//
//   sh tools/tests/run_qbstudio_test.sh
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
// A host: the controls remembered, the events scripted (a click on Convert, a resize, the end)
struct Host : bas::Host
{
	int next = 1, convert = 0, step = 0; Str log;
	char texts[64][64];
	void out (const char *s, int n) override { log.putn (s, n); }
	int inputLine (char *b, int) override { b[0] = 0; return -1; }
	int control (int kind, int x, int y, int w, int h, const char *t, int) override
	{
		if (kind == bas::CTL_BUTTON && !strcmp (t, "Convert")) convert = next;
		log.printf ("[control %d %d,%d %dx%d \"%s\" = %d]\n", kind, x, y, w, h, t, next);
		cpy (texts[next], t, 64);
		return next++;
	}
	int menuItem (const char *t, const char *i, const char *k) override { log.printf ("[menu %s/%s %s = %d]\n", t, i, k, next); return next++; }
	void setText (int id, const char *s) override { log.printf ("[settext %d \"%s\"]\n", id, s); if (id > 0 && id < 64) cpy (texts[id], s, 64); }
	int getText (int id, char *b, int cap) override { cpy (b, id > 0 && id < 64 ? texts[id] : "", cap); return (int) strlen (b); }
	int getValue (int) override { return 1; }
	void setValue (int id, int v) override { log.printf ("[setvalue %d %d]\n", id, v); }
	void moveControl (int id, int x, int y, int w, int h) override { log.printf ("[move %d %d,%d %dx%d]\n", id, x, y, w, h); }
	void enableControl (int id, bool on) override { log.printf ("[enable %d %d]\n", id, on); }
	void focusControl (int id) override { log.printf ("[focus %d]\n", id); }
	void windowFlags (int f) override { log.printf ("[flags %d]\n", f); }
	void window (const char *t, int w, int h) override { log.printf ("[window %s %dx%d]\n", t, w, h); }
	void screenSize (int *w, int *h) override { *w = step >= 2 ? 500 : 380; *h = step >= 2 ? 300 : 260; }
	int event (bool) override { step++; return step == 1 ? convert : step == 2 ? -2 : -1; }
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
	check (strstr (gen.str (), "SUB Main_Layout (w, h)") && strstr (gen.str (), "ELSEIF e = convert.id THEN"), "the window's code generated");
	Str prog; Vec<Part> parts;
	add_part (prog, parts, "(QBStudio's controls)", LIBRARY);
	add_part (prog, parts, "Main.form.bas", gen.str ());
	add_part (prog, parts, "Main.bas", code);
	add_part (prog, parts, "(the start)", "Main_Run\n");
	bas::Error err;
	bas::Program *pg = bas::compile (prog.str (), &err);
	if (!pg) { const char *file = "?"; int at = 0; part_of (parts, err.line, &file, &at); printf ("  compile error: %s line %d: %s\n", file, at, err.msg); }
	check (pg != 0, "the program compiled");
	if (pg)
	{
		Host h; bas::Error rerr;
		int r = bas::run (pg, h, &rerr);
		if (r) printf ("  runtime error line %d: %s\n", rerr.line, rerr.msg);
		if (getenv ("SHOW")) printf ("%s", h.log.str ());
		check (r == 0, "the program ran");
		check (strstr (h.log.str (), " \" 68\"]") != 0, "Convert clicked: 20 C = 68 F");
		check (strstr (h.log.str (), "[move ") != 0, "resized: the controls moved");
		check (strstr (h.log.str (), "[menu File/Quit Ctrl+Q") != 0, "the menus made");
		bas::destroy (pg);
	}
	for (int i = 0; i < subs.n; i++) free (subs[i]);
	free (src); free (code);
	printf ("%d checks, %d failed\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
