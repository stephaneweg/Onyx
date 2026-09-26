//
// host_main.cpp -- Onyx BASIC on a PC, for the host tests (tools/tests/run_basic_test.sh).
// A console bas::Host: PRINT -> stdout, INPUT <- stdin, files in the current directory,
// graphics / GUI calls logged as text lines ("[pset 1 2 3]") so a test can check them.
//   basic_host prog.bas [args]
//
#include "basic/bas.h"
#include <cstdio>
#include <cstring>
#include <ctime>

struct ConsoleHost : bas::Host
{
	int col = 1; const char *cmd = "";
	void out (const char *s, int n) override
	{
		fwrite (s, 1, n, stdout);
		for (int i = 0; i < n; i++) col = s[i] == '\n' ? 1 : col + 1;
	}
	int inputLine (char *buf, int cap) override
	{
		fflush (stdout);
		if (!fgets (buf, cap, stdin)) return -1;
		int n = (int) strlen (buf);
		while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
		printf ("%s\n", buf); col = 1;			// echo (the transcript reads like a session)
		return n;
	}
	int column () override { return col; }
	void cls (int m) override { if (m >= 0) printf ("[cls %d]\n", m); else printf ("[cls]\n"); col = 1; }
	void locate (int r, int c) override { printf ("[locate %d %d]", r, c); }
	void color (int f, int b) override { printf ("[color %d %d]", f, b); }
	void line (int a, int b, int c, int d, int e, int f, int st) override
	{ if (st >= 0) printf ("[line %d %d %d %d %d %d style %d]\n", a, b, c, d, e, f, st); else printf ("[line %d %d %d %d %d %d]\n", a, b, c, d, e, f); }
	void screen (int m, int ap, int vp) override { printf ("[screen %d %d %d]\n", m, ap, vp); }
	void paint (int x, int y, int c, int b) override { printf ("[paint %d %d %d %d]\n", x, y, c, b); }
	void setClip (int a, int b, int c, int d) override { printf ("[clip %d %d %d %d]\n", a, b, c, d); }
	void palette (int a, int c) override { printf ("[palette %d %06x]\n", a, c); }
	void pcopy (int a, int b) override { printf ("[pcopy %d %d]\n", a, b); }
	// a 64x64 pixel store for GET / PUT / POINT
	int pix[64 * 64] = {};
	void pset (int x, int y, int c) override { printf ("[pset %d %d %d]\n", x, y, c); if (x >= 0 && y >= 0 && x < 64 && y < 64) pix[y * 64 + x] = c; }
	int point (int x, int y) override { return x >= 0 && y >= 0 && x < 64 && y < 64 ? pix[y * 64 + x] : -1; }
	void readRect (int x, int y, int w, int h, int *o, bool) override { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) o[j * w + i] = point (x + i, y + j); }
	void writeRect (int x, int y, int w, int h, const int *in, bool) override
	{ printf ("[put %d %d %dx%d]\n", x, y, w, h); for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) if (x + i < 64 && y + j < 64 && x + i >= 0 && y + j >= 0) pix[(y + j) * 64 + x + i] = in[j * w + i]; }
	bool chdir (const char *p) override { printf ("[chdir %s]\n", p); return true; }
	int shell (const char *c) override { printf ("[shell %s]\n", c); return 0; }
	int listDir (const char *d, int i, char *o, int cap) override
	{ static const char *n[] = { "A.BAS", "B.TXT", "C.BAS", 0 }; (void) d; for (int k = 0; k <= i; k++) if (!n[k]) return 0; strncpy (o, n[i], cap); return (int) strlen (o); }
	void circle (int x, int y, int r, int c, int f) override { printf ("[circle %d %d %d %d %d]\n", x, y, r, c, f); }
	int note (int v, double f, int w, int vol) override { printf ("[note %d %.1f %d %d]", v, f, w, vol); return 0; }
	int nbg = 0;
	bool bgNote (double f, int on, int off, int w) override { printf ("[bg %.1f %d %d %d]", f, on, off, w); nbg++; return true; }
	int bgNotes () override { return nbg; }
	bool keyDown (const char *k, int n) override { return n == 4 && k[0] == 'L'; }	// "LEFT" held
	void sleepMs (int ms) override { if (!quietSleep) printf ("(%d)", ms); vms += ms; }
	// A virtual clock (advanced by the sleeps) and scripted keys (<prog>.keys: one key per
	// 100 ms of that clock; "\1" + letter = an extended key: \1H up, \1; F1 ...).
	double vms = 0; bool quietSleep = false;
	char keys[256] = ""; int nkeys = 0, kpos = 0;
	int keyAt (char *o)
	{
		if (kpos >= nkeys || vms < (kpos + 1) * 100.0) return 0;
		if (keys[kpos] == 1 && kpos + 1 < nkeys) { o[0] = 0; o[1] = keys[kpos + 1]; return 2; }
		o[0] = keys[kpos]; return 1;
	}
	int keyPending (char *o) override { return keyAt (o); }
	int inkey (char *o) override { int n = keyAt (o); kpos += n; return n; }
	void window (const char *t, int w, int h) override { printf ("[window %s %d %d]\n", t, w, h); }
	int nextId = 1;
	int control (int k, int x, int y, int w, int h, const char *t, int v) override
	{ printf ("[control %d %d %d %d %d \"%s\" %d -> %d]\n", k, x, y, w, h, t, v, nextId); return nextId++; }
	void setText (int id, const char *s) override { printf ("[settext %d \"%s\"]\n", id, s); }
	int  event (bool) override { static int n = 0; return ++n <= 2 ? 1 : -1; }
	void notify (const char *t, const char *m) override { printf ("[notify %s: %s]\n", t, m); }
	double timer () override { return 3600.5 + vms / 1000.0; }
	void date (char *o) override { strcpy (o, "01-02-2026"); }
	void time (char *o) override { strcpy (o, "12:34:56"); }
	char *load (const char *path, int *len) override
	{
		FILE *f = fopen (path, "rb"); if (!f) return 0;
		fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
		char *b = new char[n + 1]; *len = (int) fread (b, 1, n, f); fclose (f); return b;
	}
	bool save (const char *path, const char *d, int n) override
	{ FILE *f = fopen (path, "wb"); if (!f) return false; fwrite (d, 1, n, f); fclose (f); return true; }
	bool exists (const char *p) override { FILE *f = fopen (p, "rb"); if (f) fclose (f); return f != 0; }
	bool remove (const char *p) override { return ::remove (p) == 0; }
	const char *command () override { return cmd; }
};

int main (int argc, char **argv)
{
	if (argc < 2) { fprintf (stderr, "usage: basic_host prog.bas\n"); return 2; }
	ConsoleHost h;
	if (argc > 2) h.cmd = argv[2];
	{
		char kf[512]; snprintf (kf, sizeof kf, "%s", argv[1]);
		char *dot = strrchr (kf, '.'); if (dot) strcpy (dot, ".keys");
		FILE *f = fopen (kf, "rb");
		if (f) { h.nkeys = (int) fread (h.keys, 1, sizeof h.keys - 1, f); fclose (f); while (h.nkeys && h.keys[h.nkeys - 1] == '\n') h.nkeys--; h.quietSleep = true; }
	}
	int len = 0; char *src = h.load (argv[1], &len);
	if (!src) { fprintf (stderr, "cannot read %s\n", argv[1]); return 2; }
	src[len] = 0;
	bas::Error e;
	bas::Program *p = bas::compile (src, &e);
	if (!p) { printf ("COMPILE ERROR line %d: %s\n", e.line, e.msg); delete [] src; return 1; }
	int r = bas::run (p, h, &e);
	if (r) printf ("RUNTIME ERROR line %d: %s\n", e.line, e.msg);
	bas::destroy (p);
	delete [] src;
	return r ? 1 : 0;
}
