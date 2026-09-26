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
	void cls () override { printf ("[cls]\n"); col = 1; }
	void locate (int r, int c) override { printf ("[locate %d %d]", r, c); }
	void color (int f, int b) override { printf ("[color %d %d]", f, b); }
	void pset (int x, int y, int c) override { printf ("[pset %d %d %d]\n", x, y, c); }
	void line (int a, int b, int c, int d, int e, int f) override { printf ("[line %d %d %d %d %d %d]\n", a, b, c, d, e, f); }
	void circle (int x, int y, int r, int c, int f) override { printf ("[circle %d %d %d %d %d]\n", x, y, r, c, f); }
	int note (int v, double f, int w, int vol) override { printf ("[note %d %.1f %d %d]", v, f, w, vol); return 0; }
	void sleepMs (int ms) override { printf ("(%d)", ms); }
	void window (const char *t, int w, int h) override { printf ("[window %s %d %d]\n", t, w, h); }
	int nextId = 1;
	int control (int k, int x, int y, int w, int h, const char *t, int v) override
	{ printf ("[control %d %d %d %d %d \"%s\" %d -> %d]\n", k, x, y, w, h, t, v, nextId); return nextId++; }
	void setText (int id, const char *s) override { printf ("[settext %d \"%s\"]\n", id, s); }
	int  event (bool) override { static int n = 0; return ++n <= 2 ? 1 : -1; }
	void notify (const char *t, const char *m) override { printf ("[notify %s: %s]\n", t, m); }
	double timer () override { return 3600.5; }
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
