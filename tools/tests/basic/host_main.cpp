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
#include <cstddef>

#ifdef BAS_A64_SHIM
extern "C" void *shim_code_alloc (unsigned long size);
extern "C" unsigned long shim_clock_us (void);
#endif

// ---- a kit for the tests (#import testkit): C functions behind a table, as a shared library's ----------
extern "C" {
static int tk_add (int a, int b) { return a + b; }
static int tk_len (const char *s) { return s ? (int) strlen (s) : -1; }
static const char *tk_name (void) { return "the test kit"; }
static double tk_half (double x) { return x / 2; }
static float tk_scale (float x, int by) { return x * (float) by; }
static void tk_fill (int *i, double *d, long long *q, float *f) { *d = *i + 0.25; *i = *i * 2; *q = 0x123456789ALL; *f = 1.5f; }
static int tk_each (int n, int (*cb) (int i, void *user), void *user) { int t = 0; for (int i = 1; i <= n; i++) t += cb (i, user); return t; }
static char *tk_dup (const char *s) { char *d = new char[strlen (s) + 3]; sprintf (d, "<%s>", s); return d; }
static void tk_free (void *p) { delete [] (char *) p; }
static void tk_say (void (*cb) (const char *text, int n)) { cb ("first", 1); cb ("second", 2); }
static double tk_mix (int a, double x, int b, float y, const char *s) { return a + x + b + y + (double) strlen (s); }
static unsigned tk_big (void) { return 4000000000u; }
static int tk_neg (void) { return -5; }
static unsigned char tk_byte (void) { return 200; }
static void *tk_null (void) { return 0; }
// (structures: a kit's are TYPEs of the program, passed where a function takes a pointer)
struct tk_point { int x, y; };
struct tk_item { char name[16]; long long id; double weight; struct tk_point at; unsigned char flag; short level; float ratio; };
static_assert (sizeof (tk_item) == 48 && offsetof (tk_item, at) == 32 && offsetof (tk_item, level) == 42 && offsetof (tk_item, ratio) == 44, "testkit's .bi");
static int tk_item_next (struct tk_item *it)		// reads what it is given, writes every field
{
	int was = (int) strlen (it->name) + (int) it->id + it->at.x + it->at.y + it->flag + it->level;
	snprintf (it->name, sizeof it->name, "item %d", ((int) it->id + 1) % 1000);
	it->id += 0x100000000LL; it->weight += 0.5; it->at.x *= 2; it->at.y = -it->at.y; it->flag = 250; it->level = -3; it->ratio = 0.25f;
	return was;
}
static struct tk_item *tk_item_kept (void) { static tk_item k = { "kept", 7, 1.5, { 3, 4 }, 1, 2, 0.5f }; return &k; }
static int tk_point_sum (const struct tk_point *p) { return p ? p->x + p->y : -1; }
static int tk_points (struct tk_point *out, int max) { int t = 0; for (int i = 0; i < max; i++) { t += out[i].x; out[i].x = i * 10; out[i].y = i * 10 + 1; } return t; }
}
static void *const TK_TABLE[] = { (void *) tk_add, (void *) tk_len, (void *) tk_name, (void *) tk_half, (void *) tk_scale, (void *) tk_fill,
	(void *) tk_each, (void *) tk_dup, (void *) tk_free, (void *) tk_say, (void *) tk_mix, (void *) tk_big, (void *) tk_neg, (void *) tk_byte,
	(void *) tk_null, (void *) tk_item_next, (void *) tk_item_kept, (void *) tk_point_sum, (void *) tk_points };
static const char TK_BI[] =
	"# testkit.bi\nkit testkit 19\n"
	"struct point 8 tk_point\nfield x 0 i\nfield y 4 i\n"
	"struct item 48 tk_item\nfield name 0 a 16\nfield id 16 l\nfield weight 24 d\nfield at 32 t point\nfield flag 40 b\nfield level 42 h\n"
	"field ratio 44 f\nfield later 46 z\n"
	"item_next 15 i p tk_item_next\nitem_kept 16 l - tk_item_kept\npoint_sum 17 i p tk_point_sum\npoints 18 i pi tk_points\n"
	"add 0 i ii tk_add a,b\nlen 1 i s tk_len\nname 2 s - tk_name\nhalf 3 d d tk_half\nscale 4 f fi tk_scale\n"
	"fill 5 v IDLF tk_fill\neach 6 i icp tk_each\ndup 7 l s tk_dup\nfree 8 v p tk_free\nsay 9 v c tk_say\n"
	"mix 10 d idifs tk_mix\nbig 11 u - tk_big\nneg 12 i - tk_neg\nbyte 13 b - tk_byte\nnull 14 l - tk_null\n"
	"broken 99 q zz\n";
static char *tk_source (const char *name, int *len)
{
	if (strcmp (name, "testkit") != 0) return 0;
	*len = (int) strlen (TK_BI);
	char *b = new char[*len + 1]; memcpy (b, TK_BI, *len + 1);
	return b;
}

struct ConsoleHost : bas::Host
{
	void *const *kitOpen (const char *name, int minVersion, char *why, int cap) override
	{
		if (strcmp (name, "testkit") == 0 && minVersion <= 19) return TK_TABLE;
		snprintf (why, cap, "version %d asked", minVersion);
		return 0;
	}
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
	bas::setKitSource (tk_source);
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
