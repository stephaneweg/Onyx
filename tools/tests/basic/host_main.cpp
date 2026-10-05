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
#include <cstdlib>

#ifdef BAS_A64_SHIM
extern "C" void *shim_code_alloc (unsigned long size);
extern "C" unsigned long shim_clock_us (void);
#endif

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
#ifdef BAS_A64_SHIM
	// (the AArch64 build under qemu, tools/tests/basic/a64: the machine code's memory; MANAGED=1: the VM)
	void *codeAlloc (unsigned size) override { return shim_code_alloc (size); }
#endif
	// PROF=1: where the time goes (bas::Profile), on stderr at the end
	bas::Profile profile;
	// (0 without PROF: the machine code then polls at every tick, whatever the real time -- the same run each time)
#ifdef BAS_A64_SHIM
	unsigned clockUs () override { return prof ? (unsigned) shim_clock_us () : 0; }
#else
	unsigned clockUs () override { if (!prof) return 0; timespec ts; clock_gettime (CLOCK_MONOTONIC, &ts); return (unsigned) (ts.tv_sec * 1000000ull + ts.tv_nsec / 1000); }
#endif
	// a gamepad 0 whose A (16) and right (8) are held on every other read, its stick x at +500
	int padReads = 0;
	unsigned padButtons (int pad) override { padReads++; return (pad == 0 || pad == -1) && (padReads & 1) ? 16 + 8 : 0; }
	int padAxis (int pad, int axis) override { return pad == 0 && axis == 0 ? 500 : 0; }
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
	double stopAt = 0;					// PROFVMS=n: the program is stopped after n ms of that clock
	bool poll () override { return stopAt <= 0 || vms < stopAt; }
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
	// the events: $EVENTS ("1 -2 3": then -1, the window closed; <prog>.events), else 1, 1, -1
	int  event (bool) override
	{
		static int n = 0; n++;
		const char *e = getenv ("EVENTS");
		if (!e) return n <= 2 ? 1 : -1;
		for (int k = 1; *e; k++) { long v = strtol (e, (char **) &e, 10); if (k == n) return (int) v; while (*e == ' ') e++; }
		return -1;
	}
	void moveControl (int id, int x, int y, int w, int h) override { printf ("[move %d %d %d %d %d]\n", id, x, y, w, h); }
	void showControl (int id, bool on) override { printf ("[show %d %d]\n", id, on); }
	void enableControl (int id, bool on) override { printf ("[enable %d %d]\n", id, on); }
	void focusControl (int id) override { printf ("[focus %d]\n", id); }
	void windowFlags (int f) override { printf ("[windowflags %d]\n", f); }
	int  menuItem (const char *t, const char *i, const char *k) override { printf ("[menu %s / %s %s -> %d]\n", t, i, k, nextId); return nextId++; }
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
	if (p && getenv ("BAX"))				// through a .bax: saved, loaded back, run
	{
		char *bytes; int n = bas::saveBax (p, &bytes);
		bas::destroy (p);
		// ... and as a standalone app would carry it: after a runtime, found back by the trailer
		char *all; int an = bas::attachBax ("(a runtime)", 11, bytes, n, &all);
		unsigned off = 0, len = 0;
		if (!bas::attachedBax (all + an - bas::BAX_TRAILER, (unsigned) an, &off, &len) || off != 11 || len != (unsigned) n)
		{ puts ("the attached program is not found back"); return 1; }
		p = bas::load (all + off, (int) len, &e);
		delete [] bytes; delete [] all;
	}
	if (!p) { printf ("COMPILE ERROR line %d: %s\n", e.line, e.msg); delete [] src; return 1; }
	if (getenv ("MANAGED")) h.managed = true;
	if (getenv ("PROF")) h.prof = &h.profile;
	if (getenv ("PROFVMS")) { h.stopAt = atof (getenv ("PROFVMS")); h.quietSleep = true; }
	int r = bas::run (p, h, &e);
	if (h.prof) { char t[320]; h.profile.text (t, sizeof t); fprintf (stderr, "%s\n", t); }
	if (r) printf ("RUNTIME ERROR line %d: %s\n", e.line, e.msg);
	bas::destroy (p);
	delete [] src;
	return r ? 1 : 0;
}
