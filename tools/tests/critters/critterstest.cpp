//
// critterstest.cpp -- Critters' core on the PC (user/Apps/critters/terrain.h, level.h, world.h, solution.h,
// progress.h), the acceptance criteria of autodev/rounds/05-critters/02-product-analysis.md §12 as mapped by
// 03-technical-analysis.md §9.1:
//   A. the levels: the shipped ones (given on the command line), the format's example, every load error with its line,
//      the terrain built from the shapes (AC-1 ... AC-5);
//   B. the creatures: release, walking, falling, floating, hazards and edges, exits (AC-6 ... AC-11);
//   C. the roles: climber, blocker, builder, digger, exploder, giving roles and the cursor, the nuke (AC-12 ... AC-18);
//   D. the rules: the end and the result, determinism, the clock, the .sol reader (AC-19 ... AC-22);
//   E. the progress (AC-23, AC-24);
//   F. the shipped levels: each won by its recorded solution, each lost when nothing is done, each Training level lost
//      without its role (AC-30, AC-31).
// Small rule levels are written here as text and run with World directly, creatures placed where needed. The random
// choices (AC-21's clock) come from a fixed seed. Run by tools/tests/run_critters_test.sh (from the repository's root:
// it reads tools/tests/critters/example.level and tools/tests/critters/solutions/<level>.sol).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "critters/terrain.h"
#include "critters/level.h"
#include "critters/world.h"
#include "critters/solution.h"
#include "critters/progress.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

using namespace critters;
static int fails = 0, checks = 0;
static long totalSteps = 0;
static unsigned long long fingerprint = 1469598103934665603ull;	// every shipped level's checksum stream (the two builds compare it)
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf ("FAIL %s:%d ", __FILE__, __LINE__); printf (__VA_ARGS__); printf ("\n"); } } while (0)

// ---- helpers -----------------------------------------------------------------------------------------------------------
static char *read_file (const char *path)
{
	FILE *f = fopen (path, "rb");
	if (!f) return 0;
	fseek (f, 0, SEEK_END);
	long n = ftell (f);
	fseek (f, 0, SEEK_SET);
	char *b = (char *) malloc ((size_t) n + 1);
	if (fread (b, 1, (size_t) n, f) != (size_t) n) { free (b); fclose (f); return 0; }
	b[n] = 0;
	fclose (f);
	return b;
}
static unsigned g_seed = 12345;
static unsigned rnd () { unsigned x = g_seed; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_seed = x; }

// A rule level from its text, and a world on it (count creatures; they are placed by drop, so nothing is released)
struct Bench
{
	Level *lv; World *w;
	Bench (const char *body, int count = 1, const char *head = "", int width = 320, int height = 100) : lv (new Level), w (new World)
	{
		static char text[65536];
		snprintf (text, sizeof text, "[level]\nformat = 1\nname = Test\nsize = %d %d\ncount = %d\n%s%s%s\n%s\n%s",
			  width, height, count, strstr (head, "save") ? "" : "save = 1\n", strstr (head, "time") ? "" : "time = 1200\n", head, body,
			  strstr (body, "[hatch]") ? "" : "[hatch]\nat = 2 2\n[exit]\nat = 317 2\n");
		LoadError e;
		bool ok = load_level (text, *lv, e);
		CHECK (ok, "rule level refused: line %d: %s\n%s", e.line, e.reason, text);
		if (ok) w->reset (*lv);
	}
	~Bench () { delete w; delete lv; }
	Critter &drop (int x, int y, int dir = 1)
	{
		Critter &k = w->c[w->nout++];
		memset (&k, 0, sizeof k);
		k.x = (int16_t) x; k.y = (int16_t) y; k.dir = (int8_t) dir; k.fuse = -1; k.fallFrom = (int16_t) y;
		k.state = w->t.solid (x, y + 1) ? S_WALK : S_FALL;
		return k;
	}
	void run (int n) { for (int i = 0; i < n && w->result == PLAYING; i++) w->tick (); }
};
static const char *FLOOR = "[shape]\nrect = 0 80 320 20\n";	// a floor whose top is row 80: the feet at y 79
static int count_mat (const Terrain &t, int mat) { int n = 0; for (int i = 0; i < t.w * t.h; i++) n += t.m[i] == mat; return n; }
static bool alive_state (int s) { return s == S_WALK || s == S_FALL || s == S_CLIMB || s == S_BLOCK || s == S_BUILD || s == S_SHRUG || s == S_DIG; }

// ---- A. the levels -----------------------------------------------------------------------------------------------------
static const char *const TAUGHT[6] = { "digger", "builder", "blocker", "climber", "floater", "exploder" };	// Training 1-6
static const char *base_of (const char *path) { const char *b = strrchr (path, '/'); return b ? b + 1 : path; }

static void test_shipped (int n, char **files)
{
	int training = 0, expedition = 0, lastT = 0, lastE = 0;
	for (int i = 0; i < n; i++)
	{
		const char *b = base_of (files[i]);
		char *text = read_file (files[i]);
		CHECK (text, "%s: cannot read", files[i]);
		if (!text) continue;
		CHECK (strlen (text) <= MAXFILE, "%s: over 64 KB", b);
		Level *lv = new Level;
		LoadError e;
		bool ok = load_level (text, *lv, e);
		CHECK (ok, "%s refused: line %d: %s", b, e.line, e.reason);
		if (ok)
		{
			CHECK (lv->name.en[0] && lv->name.fr[0], "%s: name / name.fr", b);
			CHECK (lv->hint.en[0] && lv->hint.fr[0], "%s: hint / hint.fr", b);
			CHECK (lv->nhatch >= 1 && lv->nhatch <= 4 && lv->nexit >= 1 && lv->nexit <= 4, "%s: hatches / exits", b);
			CHECK (lv->save >= 1 && lv->save <= lv->count && lv->count <= 80, "%s: save %d count %d", b, lv->save, lv->count);
			CHECK (lv->w >= 320 && lv->w <= 1600 && lv->h <= 160, "%s: size %d x %d", b, lv->w, lv->h);
			for (int k = 0; k < lv->nlabel; k++) CHECK (lv->label[k].text.fr[0], "%s: label %d has no text.fr", b, k);
		}
		int nn = 0;
		if (!strncmp (b, "training-", 9)) { training++; nn = atoi (b + 9); CHECK (nn == lastT + 1, "%s: out of order", b); lastT = nn; }
		else if (!strncmp (b, "expedition-", 11)) { expedition++; nn = atoi (b + 11); CHECK (nn == lastE + 1, "%s: out of order", b); lastE = nn; }
		else CHECK (false, "%s: neither training- nor expedition-", b);
		delete lv;
		free (text);
	}
	CHECK (training == 6 && expedition == 6, "%d training, %d expedition levels", training, expedition);
}

static char *g_example;
static Level *g_lv;

// The example with `from` replaced by `to` (first occurrence; "" from = appended) must be refused at line, reason
static void bad (const char *from, const char *to, int line, const char *reason, bool diggers2 = false)
{
	static char text[200000];
	const char *p = *from ? strstr (g_example, from) : 0;
	CHECK (!*from || p, "case '%s' not in the example", from);
	if (*from && !p) return;
	if (!*from) snprintf (text, sizeof text, "%s%s", g_example, to);
	else snprintf (text, sizeof text, "%.*s%s%s", (int) (p - g_example), g_example, to, p + strlen (from));
	LoadError e;
	bool ok = load_level (text, *g_lv, e, diggers2);
	CHECK (!ok, "'%s' -> '%s' loaded", from, to);
	CHECK (e.line == line, "'%s' -> '%s': line %d, not %d (%s)", from, to, e.line, line, e.reason);
	CHECK (!strcmp (e.reason, reason), "'%s' -> '%s': '%s', not '%s'", from, to, e.reason, reason);
	CHECK (g_lv->nshape == 0 && g_lv->count == 0 && g_lv->name.en[0] == 0 && g_lv->nhatch == 0, "'%s': the level not cleared", from);
	CHECK (e.fmt && e.fmt[0], "'%s': no format", from);
}

static void test_example ()
{
	g_example = read_file ("tools/tests/critters/example.level");
	CHECK (g_example, "tools/tests/critters/example.level missing");
	if (!g_example) return;
	g_lv = new Level;
	Level &lv = *g_lv;
	LoadError e;
	bool ok = load_level (g_example, lv, e);
	CHECK (ok, "the example refused: line %d: %s", e.line, e.reason);
	// AC-2: its facts and the defaults
	CHECK (!strcmp (lv.name.en, "My First Level") && !strcmp (lv.name.fr, "Mon premier niveau"), "name");
	CHECK (!strcmp (lv.name.get (1), "Mon premier niveau") && !strcmp (lv.name.get (0), "My First Level"), "Text::get");
	CHECK (!strncmp (lv.hint.en, "Dig down", 8) && !strncmp (lv.hint.fr, "Creusez", 7), "hint");
	CHECK (lv.w == 480 && lv.h == 160 && lv.count == 10 && lv.save == 7 && lv.timeSec == 180 && lv.rate == 50, "counts");
	CHECK (lv.roles[R_DIGGER] == 2 && lv.roles[R_BUILDER] == 3 && lv.roles[R_CLIMBER] == 0 && lv.roles[R_EXPLODER] == 0, "roles");
	CHECK (lv.bg == 0x142040 && lv.brick == 0xC8A060 && lv.start == -1, "colours / start defaults");
	CHECK (lv.nshape == 6 && lv.nhatch == 1 && lv.nexit == 1 && lv.nlabel == 1, "blocks %d %d %d %d", lv.nshape, lv.nhatch, lv.nexit, lv.nlabel);
	if (lv.nshape == 6)
	{
		CHECK (lv.shape[0].kind == SH_RECT && lv.shape[0].mat == M_EARTH && lv.shape[0].colour == 0x8A5A34 && lv.shape[0].tex == TX_SPECKLE,
		       "shape 0 defaults");
		CHECK (lv.shape[0].colour2 == ((0x8A - 0x8A / 4) << 16 | (0x5A - 0x5A / 4) << 8 | (0x34 - 0x34 / 4)), "colour2 default %06X", lv.shape[0].colour2);
		CHECK (lv.shape[2].colour == 0x6E8A3C, "shape 2 colour");
		CHECK (lv.shape[3].kind == SH_POLY && lv.shape[3].n == 6 && lv.shape[3].mat == M_LAVA && lv.shape[3].colour == 0xE05020, "shape 3");
		CHECK (lv.shape[4].mat == M_STEEL && lv.shape[4].colour == 0x8890A0, "shape 4");
		CHECK (lv.shape[5].kind == SH_CIRCLE && lv.shape[5].mat == MAT_ERASE, "shape 5");
	}
	CHECK (lv.hatch[0].at.x == 40 && lv.hatch[0].at.y == 60 && lv.hatch[0].dir == 1, "hatch");
	CHECK (lv.exit[0].x == 440 && lv.exit[0].y == 119, "exit");
	CHECK (!strcmp (lv.label[0].text.en, "DIG") && !strcmp (lv.label[0].text.fr, "CREUSEZ") && lv.label[0].colour == 0xFFFFFF, "label");
	// AC-4: an unknown key in a known block loads
	{
		static char t2[8192];
		snprintf (t2, sizeof t2, "%s", g_example);
		char *p = strstr (t2, "texture = speckle");
		memcpy (p, "sparkle = 1      ", 17);
		CHECK (load_level (t2, lv, e), "sparkle = 1 refused: line %d: %s", e.line, e.reason);
	}
	// material_at = the built terrain, on every pixel of the example
	CHECK (load_level (g_example, lv, e), "reload");
	{
		Terrain t;
		CHECK (build_terrain (lv, t), "build");
		int diff = 0;
		for (int y = 0; y < lv.h; y++) for (int x = 0; x < lv.w; x++) diff += material_at (lv, x, y) != t.m[y * t.w + x];
		CHECK (diff == 0, "material_at differs from the built map on %d pixels", diff);
		CHECK (t.at (130, 75) == M_STEEL && t.at (60, 140) == M_EMPTY && t.at (220, 150) == M_LAVA && t.at (10, 125) == M_EARTH, "example pixels");
		CHECK (t.col[5] == 0x142040, "background colour");
	}

	// AC-3: each error from the example, one line changed
	bad ("[shape]\nrect    = 0 70 200 12", "[shap]\nrect    = 0 70 200 12", 24, "unknown block [shap]");
	bad ("[level]", "[shape]", 2, "[level] must be the first block, once");
	bad ("", "\n[level]\nformat = 1\n", 51, "[level] must be the first block, once");
	bad ("[level]", "count = 3\n[level]", 2, "[level] must be the first block, once");
	bad ("count      = 10\n", "", 2, "[level] needs count");
	bad ("name       = My First Level\n", "", 2, "[level] needs name");
	bad ("size       = 480 160\n", "", 2, "[level] needs size");
	bad ("at  = 40 60\n", "", 39, "[hatch] needs at");
	bad ("text    = DIG\n", "", 46, "[label] needs text");
	bad ("circle   = 60 140 10", "radius = 10", 35, "[shape] needs one of rect, points, circle");
	bad ("rect     = 120 70 16 12", "rect     = 120 70 16 12\ncircle = 1 1 1", 32, "[shape] needs one of rect, points, circle");
	bad ("count      = 10", "count      = ten", 9, "bad value for count");
	bad ("size       = 480 160", "size       = 480", 8, "bad value for size");
	bad ("colour  = #6E8A3C", "colour  = #6E8A3", 26, "bad value for colour");
	bad ("colour  = #6E8A3C", "colour  = #6E8A3G", 26, "bad value for colour");
	bad ("material = lava", "material = magma", 29, "bad value for material");
	bad ("texture = speckle", "texture = dots", 20, "bad value for texture");
	bad ("dir = right", "dir = up", 41, "bad value for dir");
	bad ("points  = 200 160, 200 120, 210 140, 230 140, 240 120, 240 160", "points  = 200 160, 200 120", 28, "bad value for points");
	bad ("points  = 200 160, 200 120, 210 140, 230 140, 240 120, 240 160", "points  = 200 160, 200 120, 210", 28, "bad value for points");
	bad ("rect    = 0 120 200 40", "rect    = 0 120 200 4.5", 19, "bad value for rect");
	bad ("count      = 10", "count      = 81", 9, "count out of range");
	bad ("count      = 10", "count      = 0", 9, "count out of range");
	bad ("save       = 7", "save       = 11", 10, "save out of range");
	bad ("time       = 180", "time       = 10", 11, "time out of range");
	bad ("rate       = 50", "rate       = 0", 12, "rate out of range");
	bad ("digger     = 2", "digger     = 100", 13, "digger out of range");
	bad ("format     = 1", "format     = 2", 3, "format out of range");
	bad ("size       = 480 160", "size       = 2000 160", 8, "size out of range");
	bad ("size       = 480 160", "size       = 480 161", 8, "size out of range");
	bad ("name       = My First Level", "name       = A name that is far too long for it", 4, "name out of range");
	bad ("at  = 40 60", "at  = 500 60", 40, "at out of range");
	bad ("at  = 440 119", "at  = 440 -1", 44, "at out of range");
	bad ("rect    = 0 120 200 40", "rect    = 0 120 0 40", 19, "rect out of range");
	bad ("circle   = 60 140 10", "circle   = 60 140 401", 36, "circle out of range");
	bad ("[hatch]\nat  = 40 60\ndir = right\n", "", 2, "the level needs 1 to 4 [hatch]");
	bad ("[exit]\nat  = 440 119\n", "", 2, "the level needs 1 to 4 [exit]");
	bad ("", "[exit]\nat = 400 50\n[exit]\nat = 400 50\n[exit]\nat = 400 50\n[exit]\nat = 400 50\n", 2, "the level needs 1 to 4 [exit]");
	bad ("", "[hatch]\nat = 400 50\n[hatch]\nat = 400 50\n[hatch]\nat = 400 50\n[hatch]\nat = 400 50\n", 2, "the level needs 1 to 4 [hatch]");
	bad ("at  = 40 60", "at  = 40 130", 40, "[hatch] is inside the terrain");
	bad ("at  = 440 119", "at  = 440 120", 44, "[exit] is inside the terrain");
	bad ("at  = 40 60", "at  = 125 75", 40, "[hatch] is inside the terrain");	// in the steel post
	bad ("at  = 40 60", "at  = 220 150", 40, "[hatch] is inside the terrain");	// in the lava
	bad ("builder    = 3", "builder    = 3\nbasher     = 1", 15, "role basher is not available");
	bad ("builder    = 3", "builder    = 3\nminer      = 2", 15, "role miner is not available");
	{
		static char many[70000];
		int o = 0;
		for (int i = 0; i < 251; i++) o += snprintf (many + o, sizeof many - (size_t) o, "[shape]\nrect = 0 0 1 1\n");
		bad ("", many, 50 + 250 * 2, "too many shapes (max 256)");
		o = snprintf (many, sizeof many, "points  =");
		for (int i = 0; i < 65; i++) o += snprintf (many + o, sizeof many - (size_t) o, " %d %d", i, i % 2 ? 100 : 150);
		bad ("points  = 200 160, 200 120, 210 140, 230 140, 240 120, 240 160", many, 28, "too many points (max 64)");
		o = 0;
		for (int i = 0; i < 32; i++) o += snprintf (many + o, sizeof many - (size_t) o, "[label]\nat = 10 10\ntext = x\n");
		bad ("", many, 50 + 31 * 3, "too many labels (max 32)");
	}
	// with the basher and the miner built, their counts load
	{
		static char t2[8192];
		snprintf (t2, sizeof t2, "%s", g_example);
		char *p = strstr (t2, "builder    = 3");
		memcpy (p, "basher     = 3", 14);
		CHECK (load_level (t2, lv, e, true) && lv.roles[R_BASHER] == 3, "basher with withDiggers2: %s", e.reason);
	}
	// the file itself
	{
		char *big = (char *) malloc (MAXFILE + 100);
		memset (big, '#', MAXFILE + 99); big[MAXFILE + 99] = 0;
		CHECK (!load_level (big, lv, e) && e.line == 0 && !strcmp (e.reason, "the file is too big"), "a big file: %d %s", e.line, e.reason);
		free (big);
		load_error_file (e, false);
		CHECK (e.line == 0 && !strcmp (e.reason, "cannot read the file") && !strcmp (e.fmt, "cannot read the file"), "unreadable");
		CHECK (!load_level ("", lv, e) && e.line == 1 && !strcmp (e.reason, "[level] must be the first block, once"), "empty file");
	}
}

// AC-5: the terrain from the shapes
static bool even_odd (const Shape &s, int x, int y)	// (independent of level.cpp: crossings strictly left of the centre)
{
	long long Y = 2LL * y + 1, P = 2LL * x + 1;
	int in = 0;
	for (int j = 0; j < s.n; j++)
	{
		int k = (j + 1) % s.n;
		long long x1 = 2LL * s.p[2 * j], y1 = 2LL * s.p[2 * j + 1], x2 = 2LL * s.p[2 * k], y2 = 2LL * s.p[2 * k + 1];
		if ((y1 < Y) == (y2 < Y)) continue;
		// Xc < P  <=>  x1 + (Y - y1) (x2 - x1) / (y2 - y1) < P
		long long lhs = x1 * (y2 - y1) + (Y - y1) * (x2 - x1), rhs = P * (y2 - y1);
		if (y2 > y1 ? lhs < rhs : lhs > rhs) in ^= 1;
	}
	return in;
}
static void test_terrain_build ()
{
	{
		Bench b ("[shape]\nrect = 10 20 30 5\n");
		int n = count_mat (b.w->t, M_EARTH), in = 0;
		for (int y = 20; y <= 24; y++) for (int x = 10; x <= 39; x++) in += b.w->t.at (x, y) == M_EARTH;
		CHECK (n == 150 && in == 150, "rect: %d earth, %d in the box", n, in);
	}
	{
		Bench b ("[shape]\ncircle = 100 50 10\n");
		int n = count_mat (b.w->t, M_EARTH), good = 0;
		for (int y = 0; y < 100; y++) for (int x = 0; x < 320; x++)
		{
			int dx = x - 100, dy = y - 50;
			good += (b.w->t.at (x, y) == M_EARTH) == (dx * dx + dy * dy <= 100);
		}
		CHECK (n == 317 && good == 320 * 100, "circle r 10: %d pixels, %d agree", n, good);
	}
	const char *polys[] = { "200 10, 300 40, 220 90", "10 10, 60 10, 60 60, 10 60",	/* a square with exact edges */
				"150 5, 165 40, 200 45, 172 62, 185 95, 150 75, 115 95, 128 62, 100 45, 135 40",	/* a star */
				"20 20, 120 80, 120 20, 20 80" /* a bow-tie (self-crossing) */, "0 0, 2 2, 0 4" };
	for (int pi = 0; pi < 5; pi++)
	{
		char body[256];
		snprintf (body, sizeof body, "[shape]\npoints = %s\n", polys[pi]);
		Bench b (body);
		const Shape &s = b.lv->shape[0];
		int diff = 0, n = 0;
		for (int y = 0; y < 100; y++) for (int x = 0; x < 320; x++)
		{
			bool e = even_odd (s, x, y);
			n += e;
			diff += e != (b.w->t.at (x, y) == M_EARTH);
		}
		CHECK (diff == 0 && n > 0, "polygon %d: %d pixels differ from the even-odd count (%d inside)", pi, diff, n);
	}
	{
		Bench b ("[shape]\nrect = 0 0 100 50\n[shape]\ncircle = 50 25 10\nmaterial = erase\n[shape]\nrect = 80 0 40 20\nmaterial = steel\n"
			 "[shape]\nrect = -50 60 100 100\n[shape]\ncircle = 319 99 30\nmaterial = water\n[shape]\npoints = -100 -100, 400 -100, 400 5, -100 5\n[hatch]\nat = 200 30\n[exit]\nat = 210 30\n");
		const Terrain &t = b.w->t;
		CHECK (t.at (50, 25) == M_EMPTY && t.at (50, 35) == M_EMPTY && t.at (50, 36) == M_EARTH, "erase");
		CHECK (t.at (90, 10) == M_STEEL && t.at (79, 10) == M_EARTH && t.at (110, 10) == M_STEEL && t.at (110, 25) == M_EMPTY, "steel over earth");
		CHECK (t.at (0, 60) == M_EARTH && t.at (49, 99) == M_EARTH && t.at (50, 99) == M_EMPTY, "clipped rect");
		CHECK (t.at (319, 99) == M_WATER && t.at (289, 99) == M_WATER && t.at (288, 99) == M_EMPTY, "clipped circle");
		CHECK (t.at (0, 0) == M_EARTH && t.at (319, 5) == M_EMPTY && t.at (319, 4) == M_EARTH, "clipped polygon");
		int diff = 0;
		for (int y = 0; y < 100; y++) for (int x = 0; x < 320; x++) diff += material_at (*b.lv, x, y) != t.at (x, y);
		CHECK (diff == 0, "material_at differs on %d pixels", diff);
	}
	{	// the textures: stripes and bricks by rows / columns, the speckle about 1 in 6, the steel's rivets
		Bench b ("[shape]\nrect = 0 0 80 40\ntexture = stripes\ncolour = #102030\ncolour2 = #405060\n"
			 "[shape]\nrect = 100 0 80 40\ntexture = speckle\ncolour = #102030\ncolour2 = #405060\n"
			 "[shape]\nrect = 200 0 80 40\ntexture = bricks\nmaterial = steel\ncolour = #808080\ncolour2 = #404040\n[hatch]\nat = 300 50\n[exit]\nat = 310 50\n");
		const Terrain &t = b.w->t;
		CHECK (t.col[3 * 320 + 5] == 0x405060 && t.col[2 * 320 + 5] == 0x102030, "stripes");
		int sp = 0;
		for (int y = 0; y < 40; y++) for (int x = 100; x < 180; x++) sp += t.col[y * 320 + x] == 0x405060;
		CHECK (sp > 3200 / 9 && sp < 3200 / 4, "speckle: %d of 3200", sp);
		CHECK (t.col[3 * 320 + 201] == 0x404040 && t.col[1 * 320 + 207] == 0x404040 && t.col[5 * 320 + 203] == 0x404040, "bricks' mortar");
		CHECK (t.col[0 * 320 + 200] != 0x808080 && t.col[1 * 320 + 201] == 0x808080, "rivets");
	}
	// the build time of the biggest map (1600 x 160, 60 shapes)
	{
		static char body[30000];
		int o = 0;
		for (int i = 0; i < 20; i++)
			o += snprintf (body + o, sizeof body - (size_t) o, "[shape]\nrect = %d 100 70 60\ntexture = speckle\n[shape]\ncircle = %d 90 40\ntexture = bricks\n"
				       "[shape]\npoints = %d 160, %d 40, %d 70, %d 30, %d 160\nmaterial = steel\n", i * 80, i * 80 + 30, i * 80, i * 80 + 20, i * 80 + 40, i * 80 + 60, i * 80 + 70);
		clock_t c0 = clock ();
		Bench b (body, 1, "", 1600, 160);
		clock_t c1 = clock ();
		printf ("     terrain build 1600 x 160, 60 shapes: %ld us\n", (long) ((c1 - c0) * 1000000 / CLOCKS_PER_SEC));
	}
}

// The terrain's own rules (03 step 2): edges, digging, bursts, bricks, the hash
static void test_terrain ()
{
	Bench b ("[shape]\nrect = 0 50 320 50\n[shape]\nrect = 100 60 20 20\nmaterial = steel\n[shape]\nrect = 140 60 20 20\nmaterial = water\n");
	Terrain &t = b.w->t;
	CHECK (t.at (-1, 10) == M_STEEL && t.at (320, 10) == M_STEEL && t.at (10, -1) == M_STEEL && t.at (10, 100) == M_EMPTY && t.at (-1, 100) == M_STEEL, "edges");
	CHECK (t.solid (-1, 5) && t.solid (5, -1) && !t.solid (5, 100) && t.hazard (150, 70) && !t.solid (150, 70), "solid / hazard");
	t.take_dirty ();
	int n = t.dig_rect (90, 55, 170, 75);
	CHECK (t.at (110, 70) == M_STEEL && t.at (150, 70) == M_WATER && t.at (95, 70) == M_EMPTY && t.at (165, 56) == M_EMPTY, "dig_rect keeps steel / water");
	CHECK (n == 81 * 21 - 20 * 16 - 20 * 16, "dig_rect removed %d", n);
	Rect d = t.take_dirty ();
	CHECK (d.x0 == 90 && d.y0 == 55 && d.x1 == 170 && d.y1 == 75, "dirty %d %d %d %d", d.x0, d.y0, d.x1, d.y1);
	d = t.take_dirty ();
	CHECK (d.x1 < d.x0, "dirty emptied");
	CHECK (t.any_steel (90, 60, 100, 60) && !t.any_steel (90, 60, 99, 90) && !t.any_steel (-20, -20, -1, -1), "any_steel");
	t.dig_disc (200, 50, 12);
	CHECK (t.at (200, 62) == M_EMPTY && t.at (200, 63) == M_EARTH && t.at (212, 50) == M_EMPTY && t.at (213, 50) == M_EARTH, "dig_disc");
	t.dig_disc (5, 99, 30);					// at the map's corner: clipped
	int k = t.brick (250, 40, 1);
	CHECK (k == 12 && t.at (249, 40) == M_EARTH && t.at (254, 39) == M_EARTH && t.at (255, 40) == M_EMPTY && t.at (248, 40) == M_EMPTY, "brick right");
	k = t.brick (250, 30, -1);
	CHECK (k == 12 && t.at (251, 30) == M_EARTH && t.at (246, 29) == M_EARTH && t.at (245, 30) == M_EMPTY, "brick left");
	CHECK (t.brick (250, 40, 1) == 0, "a brick fills only empty pixels");
	CHECK (t.col[39 * 320 + 250] != t.col[40 * 320 + 250] && t.col[40 * 320 + 250] == b.lv->brick, "the bricks' colours");
	for (int i = 0; i < 2000; i++)
	{
		int x = (int) (rnd () % 340) - 10, y = (int) (rnd () % 120) - 10;
		switch (rnd () % 4)
		{
		case 0: t.set (x, y, (uint8_t) (rnd () % 5), 0); break;
		case 1: t.dig_rect (x, y, x + (int) (rnd () % 9), y + 1); break;
		case 2: t.dig_disc (x, y, 1 + (int) (rnd () % 12)); break;
		default: t.brick (x, y, rnd () % 2 ? 1 : -1); break;
		}
	}
	CHECK (t.hash == t.full_hash (), "the incremental hash differs from the full one");
}

// ---- B. the creatures --------------------------------------------------------------------------------------------------
static void test_release ()
{
	const int rates[3] = { 50, 99, 1 }, iv[3] = { 24, 4, 44 };
	for (int r = 0; r < 3; r++)
	{
		char head[32]; snprintf (head, sizeof head, "rate = %d", rates[r]);
		Bench b ("[shape]\nrect = 0 80 320 20\n[hatch]\nat = 20 70\n[exit]\nat = 317 2\n", 5, head);
		CHECK (interval_for (rates[r]) == iv[r], "interval_for (%d) = %d", rates[r], interval_for (rates[r]));
		int seen[5], ns = 0;
		while (b.w->step < 400 && ns < 5)
		{
			b.w->tick ();
			for (int k = 0; k < b.w->nev; k++) if (b.w->ev[k].kind == E_OUT && ns < 5) seen[ns++] = b.w->step - 1;
		}
		CHECK (ns == 5, "rate %d: %d out", rates[r], ns);
		for (int k = 0; k < ns; k++) CHECK (seen[k] == 40 + k * iv[r], "rate %d: creature %d out at %d", rates[r], k, seen[k]);
		CHECK (b.w->c[0].dir == 1 && b.w->c[0].y == 79, "from the hatch");
	}
	{
		Bench b ("[shape]\nrect = 0 80 320 20\n[hatch]\nat = 20 70\n[hatch]\nat = 200 70\ndir = left\n[exit]\nat = 317 2\n", 3, "rate = 50");
		b.run (100);
		CHECK (b.w->nout == 3 && b.w->c[0].dir == 1 && b.w->c[1].dir == -1 && b.w->c[2].dir == 1, "two hatches alternate");
		CHECK (b.w->c[2].x > 20 && b.w->c[2].x < 40 && b.w->c[1].x > 150 && b.w->c[1].x < 200, "two hatches' places: %d %d", b.w->c[1].x, b.w->c[2].x);
		b.w->set_rate (10); CHECK (b.w->rate == 50, "rate below the level's");
		b.w->set_rate (120); CHECK (b.w->rate == 99, "rate above 99");
		b.w->set_rate (70); CHECK (b.w->rate == 70, "rate 70");
	}
	{	// a rate change counts from the next release
		Bench b ("[shape]\nrect = 0 80 320 20\n[hatch]\nat = 20 70\n[exit]\nat = 317 2\n", 4, "rate = 50");
		b.run (41);						// creature 0 out at 40, the next due at 64
		b.w->set_rate (99);
		int seen[3], ns = 0;
		while (b.w->step < 200 && ns < 3)
		{
			b.w->tick ();
			for (int k = 0; k < b.w->nev; k++) if (b.w->ev[k].kind == E_OUT) seen[ns++] = b.w->step - 1;
		}
		CHECK (ns == 3 && seen[0] == 64 && seen[1] == 68 && seen[2] == 72, "rate change: %d %d %d", seen[0], seen[1], seen[2]);
	}
}

static void test_walk ()
{
	{
		Bench b (FLOOR);
		Critter &k = b.drop (50, 79);
		b.run (10);
		CHECK (k.x == 60 && k.y == 79 && k.state == S_WALK, "walk 1 px a step: %d,%d", k.x, k.y);
	}
	const int heights[4] = { 3, 6, 7, 40 };
	for (int i = 0; i < 4; i++)
	{
		char body[128]; snprintf (body, sizeof body, "%s[shape]\nrect = 100 %d 20 %d\n", FLOOR, 80 - heights[i], heights[i]);
		Bench b (body);
		Critter &k = b.drop (90, 79);
		int maxx = 0;
		for (int s = 0; s < 15; s++) { b.w->tick (); if (k.x > maxx) maxx = k.x; }
		if (heights[i] <= 6) CHECK (k.dir == 1 && k.y == 79 - heights[i] && k.x == 105, "step of %d: %d,%d dir %d", heights[i], k.x, k.y, k.dir);
		else CHECK (k.dir == -1 && maxx == 99 && k.y == 79, "wall of %d: turned (max x %d, dir %d)", heights[i], maxx, k.dir);
	}
	for (int drop = 3; drop <= 4; drop++)
	{
		char body[128]; snprintf (body, sizeof body, "[shape]\nrect = 0 80 150 20\n[shape]\nrect = 150 %d 170 20\n", 80 + drop);
		Bench b (body);
		Critter &k = b.drop (140, 79);
		bool fell = false;
		for (int s = 0; s < 20; s++) { b.w->tick (); fell |= k.state == S_FALL; }
		CHECK (fell == (drop == 4) && k.state == S_WALK && k.y == 79 + drop && k.x > 150, "a drop of %d: fell %d, %d,%d", drop, fell, k.x, k.y);
	}
}

static void test_fall ()
{
	for (int d = 60; d <= 61; d++)
	{
		char body[128]; snprintf (body, sizeof body, "[shape]\nrect = 0 20 100 5\n[shape]\nrect = 0 %d 320 5\n", 20 + d);
		Bench b (body);
		Critter &k = b.drop (95, 19);
		int lastY = -1, speedOk = 1;
		for (int s = 0; s < 60 && alive_state (k.state); s++)
		{
			b.w->tick ();
			if (k.state == S_FALL && lastY >= 0 && k.y - lastY != 3 && k.y - lastY != 0) speedOk = 0;
			lastY = k.state == S_FALL ? k.y : -1;
		}
		if (d == 60) CHECK (k.state == S_WALK && k.y == 79, "a fall of 60: %d at %d", k.state, k.y);
		else CHECK (k.state == S_SPLAT || k.state == S_DEAD, "a fall of 61: state %d", k.state);
		CHECK (speedOk, "falling 3 px a step");
	}
	// AC-9: a floater dropped 150 px: 3 px a step for 12 px, then 1 px a step; alive
	{
		Bench b ("[shape]\nrect = 0 5 100 5\n[shape]\nrect = 0 155 320 5\n", 1, "", 320, 160);
		Critter &k = b.drop (95, 4);
		k.flags = F_FLOATER;
		int ys[200], n = 0;
		for (int s = 0; s < 200 && !(n && k.state == S_WALK); s++) { b.w->tick (); if (k.state == S_FALL) ys[n++] = k.y; }
		CHECK (k.state == S_WALK && k.y == 154, "the floater landed alive: state %d at %d", k.state, k.y);
		bool ok = n > 20;
		// ys[0] is the first step after walking off (0 px); then 3 px steps until 12 px fallen, then 1 px
		for (int i = 1; i < n && ok; i++)
		{
			int fallen = ys[i - 1] - 4, dy = ys[i] - ys[i - 1];
			if (dy != (fallen < 12 ? 3 : 1)) { ok = false; printf ("  floater: step %d fallen %d dy %d\n", i, fallen, dy); }
		}
		CHECK (ok, "the floater's speeds");
		CHECK (n == 1 + 4 + 138 || n == 4 + 138, "the floater's fall took %d steps", n);
	}
}

static void test_hazards ()
{
	const char *bodies[3] = { "[shape]\nrect = 0 80 150 20\n[shape]\nrect = 150 80 60 20\nmaterial = water\n",
				  "[shape]\nrect = 0 80 150 20\n[shape]\nrect = 150 80 60 20\nmaterial = lava\n",
				  "[shape]\nrect = 0 80 150 20\n" };
	const int expect[3] = { S_DROWN, S_BURN, S_DEAD };
	for (int i = 0; i < 3; i++)
	{
		Bench b (bodies[i]);
		Critter &k = b.drop (140, 79);
		int seen = -1;
		for (int s = 0; s < 80 && b.w->result == PLAYING; s++) { b.w->tick (); if (seen < 0 && !alive_state (k.state)) seen = k.state; }
		CHECK (seen == expect[i], "hazard %d: state %d", i, seen);
		CHECK (b.w->result == LOST && b.w->dead == 1 && b.w->saved == 0, "hazard %d: lost, dead %d", i, b.w->dead);
	}
	{	// the map's edges turn a walker (and a climber: the side edges are not climbed)
		Bench b (FLOOR, 2);
		Critter &k = b.drop (310, 79, 1), &l = b.drop (8, 79, -1);
		l.flags = F_CLIMBER;
		int maxx = 0, minx = 999;
		for (int s = 0; s < 30; s++) { b.w->tick (); if (k.x > maxx) maxx = k.x; if (l.x < minx) minx = l.x; }
		CHECK (maxx == 319 && k.dir == -1 && minx == 0 && l.dir == 1 && l.state == S_WALK, "edges turn: %d %d %d %d", maxx, k.dir, minx, l.dir);
	}
}

static void test_exit ()
{
	{
		Bench b ("[shape]\nrect = 0 80 320 20\n[hatch]\nat = 2 2\n[exit]\nat = 200 79\n");
		Critter &k = b.drop (150, 79);
		int enter = -1, saved = -1;
		for (int s = 0; s < 100 && b.w->result == PLAYING; s++)
		{
			b.w->tick ();
			if (enter < 0 && k.state == S_EXIT) enter = b.w->step;
			if (saved < 0 && k.state == S_SAVED) saved = b.w->step;
		}
		CHECK (enter > 0 && k.x == 198 && saved - enter == 8 && b.w->saved == 1 && b.w->result == WON, "exit: enter %d saved %d x %d", enter, saved, k.x);
	}
	{	// 3 px beside: a wall turns it 3 px short of the exit
		Bench b ("[shape]\nrect = 0 80 320 20\n[shape]\nrect = 198 60 1 20\n[hatch]\nat = 2 2\n[exit]\nat = 200 79\n");
		Critter &k = b.drop (150, 79);
		int maxx = 0;
		for (int s = 0; s < 300; s++) { b.w->tick (); if (k.x > maxx) maxx = k.x; }
		CHECK (maxx == 197 && b.w->saved == 0 && k.state == S_WALK, "3 px beside: max x %d, saved %d", maxx, b.w->saved);
	}
	{	// on a shelf 5 px above
		Bench b ("[shape]\nrect = 0 80 320 20\n[shape]\nrect = 170 75 70 1\n[hatch]\nat = 2 2\n[exit]\nat = 200 79\n");
		Critter &k = b.drop (172, 74);
		bool in = false;
		for (int s = 0; s < 60; s++) { b.w->tick (); in |= k.state == S_EXIT; }
		CHECK (!in && k.x == 232 && k.y == 74, "a shelf 5 px above: entered %d at %d,%d", in, k.x, k.y);
	}
}

// ---- C. the roles ------------------------------------------------------------------------------------------------------
static void test_climber ()
{
	{
		Bench b ("[shape]\nrect = 0 80 320 20\n[shape]\nrect = 150 40 10 40\n");
		Critter &k = b.drop (140, 79);
		k.flags = F_CLIMBER;
		int lastY = -1; bool climbOk = true, onTop = false, climbed = false;
		for (int s = 0; s < 120; s++)
		{
			b.w->tick ();
			if (k.state == S_CLIMB) { climbed = true; if (lastY >= 0 && lastY - k.y != 1) climbOk = false; lastY = k.y; }
			else lastY = -1;
			if (k.state == S_WALK && k.y == 39 && k.x >= 150 && k.x <= 159) onTop = true;
		}
		CHECK (climbed && climbOk && onTop, "a climber up a 40 px wall: climbed %d at 1 px %d, walked the top %d", climbed, climbOk, onTop);
		CHECK (k.x > 160 && k.y == 79 && k.state == S_WALK, "and down the far side: %d,%d", k.x, k.y);
	}
	{	// under an overhang: lets go, faces the other way, falls (43 px: alive)
		Bench b ("[shape]\nrect = 0 80 320 20\n[shape]\nrect = 150 20 10 60\n[shape]\nrect = 140 20 10 5\n");
		Critter &k = b.drop (140, 79);
		k.flags = F_CLIMBER;
		int minY = 999; bool letGo = false;
		for (int s = 0; s < 120; s++)
		{
			int st = k.state;
			b.w->tick ();
			if (k.y < minY) minY = k.y;
			if (st == S_CLIMB && k.state == S_FALL) letGo = true;
		}
		CHECK (letGo && minY == 36 && k.dir == -1 && k.state == S_WALK && k.x < 149, "overhang: let go %d at %d, dir %d, %d,%d", letGo, minY, k.dir, k.x, k.y);
	}
	{
		Bench b ("[shape]\nrect = 0 80 320 20\n[shape]\nrect = 150 40 10 40\n");
		Critter &k = b.drop (140, 79);
		int maxx = 0;
		for (int s = 0; s < 40; s++) { b.w->tick (); if (k.x > maxx) maxx = k.x; }
		CHECK (maxx == 149 && k.dir == -1 && k.state == S_WALK, "a walker turns at the wall");
	}
}

static void test_blocker ()
{
	{
		Bench b (FLOOR, 3, "blocker = 1");
		Critter &bl = b.drop (160, 79), &a = b.drop (100, 79, 1), &c = b.drop (220, 79, -1);
		CHECK (b.w->assign (0, R_BLOCKER) == OK && bl.state == S_BLOCK, "blocker given");
		int maxA = 0, minC = 999;
		for (int s = 0; s < 600; s++) { b.w->tick (); if (a.x > maxA) maxA = a.x; if (c.x < minC) minC = c.x; }
		CHECK (maxA == 154 && minC == 166, "turned within 6 px: %d %d", maxA, minC);
		CHECK (bl.state == S_BLOCK && bl.x == 160, "the blocker stays");
	}
	{	// the floor dug away: it falls, then walks
		Bench b ("[shape]\nrect = 0 80 320 20\n[shape]\nrect = 0 90 320 10\n", 1, "blocker = 1");
		Critter &bl = b.drop (160, 79);
		b.w->assign (0, R_BLOCKER);
		b.run (5);
		b.w->t.dig_rect (150, 80, 170, 89);
		bool fell = false;
		for (int s = 0; s < 20; s++) { b.w->tick (); fell |= bl.state == S_FALL; }
		CHECK (fell && bl.state == S_WALK && bl.y == 89, "a blocker on nothing falls: %d at %d", bl.state, bl.y);
	}
	{	// an exploder frees the way
		Bench b (FLOOR, 2, "blocker = 1\nexploder = 1");
		Critter &bl = b.drop (160, 79), &a = b.drop (100, 79, 1);
		b.w->assign (0, R_BLOCKER);
		b.run (50);
		CHECK (a.x <= 154, "held back");
		CHECK (b.w->assign (0, R_EXPLODER) == OK, "an exploder given to the blocker");
		int maxA = 0;
		for (int s = 0; s < 400; s++) { b.w->tick (); if (a.x > maxA) maxA = a.x; }
		CHECK (bl.state == S_DEAD && maxA > 200 && a.state == S_WALK, "the way freed: max x %d, state %d", maxA, a.state);
	}
}

static void test_builder ()
{
	{	// 12 bricks on a flat floor, pixel by pixel
		Bench b (FLOOR, 1, "builder = 1");
		Critter &k = b.drop (100, 79);
		Terrain before; before.bg = b.w->t.bg;
		before.alloc (320, 100);
		memcpy (before.m, b.w->t.m, 320 * 100);
		b.w->assign (0, R_BUILDER);
		int at[13], nb = 0, lastX = k.x, sh = -1, walkAt = -1;
		for (int s = 0; s < 140; s++)
		{
			b.w->tick ();
			if (k.x != lastX && nb < 13 && (k.state == S_BUILD || k.state == S_SHRUG)) { at[nb] = b.w->step - 1; nb++;
				CHECK (k.x == 100 + 3 * nb && k.y == 79 - 2 * nb, "brick %d: at %d,%d", nb, k.x, k.y); }
			lastX = k.x;
			if (sh < 0 && k.state == S_SHRUG) sh = b.w->step - 1;
			if (sh >= 0 && walkAt < 0 && k.state == S_WALK) walkAt = b.w->step - 1;
		}
		CHECK (nb == 12, "%d bricks", nb);
		CHECK (nb > 0 && at[0] == 7, "first brick at step %d", nb > 0 ? at[0] : -1);
		for (int i = 1; i < nb; i++) CHECK (at[i] - at[i - 1] == 8, "brick %d after %d steps", i, at[i] - at[i - 1]);
		CHECK (walkAt - sh == 10, "shrugs 10 steps (%d)", walkAt - sh);
		int diff = 0, added = 0;
		for (int y = 0; y < 100; y++) for (int x = 0; x < 320; x++)
		{
			bool want = false;
			for (int j = 0; j < 12; j++) if (x >= 100 + 3 * j - 1 && x <= 100 + 3 * j + 4 && y >= 79 - 2 * j - 1 && y <= 79 - 2 * j) want = true;
			int now = b.w->t.m[y * 320 + x], was = before.m[y * 320 + x];
			if (want) { added++; diff += !(was == M_EMPTY && now == M_EARTH); }
			else diff += now != was;
		}
		CHECK (diff == 0 && added == 12 * 12 - 11 * 0, "the bricks' pixels: %d differ (%d expected)", diff, added);
		CHECK (b.w->roles[R_BUILDER] == 0, "builder count");
	}
	{	// a wall in its path: it stops early and turns
		Bench b ("[shape]\nrect = 0 80 320 20\n[shape]\nrect = 120 30 10 50\n", 1, "builder = 1");
		Critter &k = b.drop (100, 79);
		b.w->assign (0, R_BUILDER);
		int nb = 0, lastX = k.x;
		for (int s = 0; s < 120 && k.state == S_BUILD; s++) { b.w->tick (); if (k.x != lastX && k.state == S_BUILD) nb++; lastX = k.x; }
		CHECK (k.state == S_WALK && k.dir == -1 && k.bricks == 6 && nb == 6, "the early stop: state %d dir %d, %d bricks laid, %d left", k.state, k.dir, nb, k.bricks);
	}
	{	// the 30 px gap over a 100 px drop, crossed by the followers on its stair
		Bench b ("[shape]\nrect = 0 60 150 100\n[shape]\nrect = 180 60 140 100\n[hatch]\nat = 2 2\n[exit]\nat = 250 59\n", 4, "builder = 1\nsave = 4", 320, 160);
		Critter &bu = b.drop (148, 59);
		b.drop (10, 59, -1); b.drop (20, 59, -1); b.drop (30, 59, -1);
		b.w->assign (0, R_BUILDER);
		b.run (500);
		CHECK (b.w->saved == 4 && b.w->dead == 0 && b.w->result == WON, "the gap crossed: %d of 4 saved beyond it, %d dead (builder at %d,%d)",
		       b.w->saved, b.w->dead, bu.x, bu.y);
	}
}

static void test_digger ()
{
	{
		Bench b ("[shape]\nrect = 0 60 320 20\n[shape]\nrect = 0 80 320 20\nmaterial = steel\n", 1, "digger = 1");
		Critter &k = b.drop (100, 59);
		int steel = count_mat (b.w->t, M_STEEL);
		b.w->assign (0, R_DIGGER);
		int lastY = k.y, lastStep = 0; bool rhythm = true;
		for (int s = 0; s < 60 && k.state == S_DIG; s++)
		{
			b.w->tick ();
			if (k.y != lastY) { if (k.y != lastY + 1 || (lastStep && b.w->step - lastStep != 2)) rhythm = false; lastStep = b.w->step; lastY = k.y; }
		}
		CHECK (rhythm && k.y == 79 && k.state == S_WALK, "the digger: 1 px every 2 steps down to the steel: %d at %d", k.state, k.y);
		int diff = 0;
		for (int y = 60; y < 80; y++) for (int x = 0; x < 320; x++) diff += (b.w->t.at (x, y) == M_EMPTY) != (x >= 96 && x <= 104);
		CHECK (diff == 0 && count_mat (b.w->t, M_STEEL) == steel, "a 9 px shaft, the steel intact (%d differ)", diff);
	}
	{
		Bench b ("[shape]\nrect = 0 60 320 20\n[shape]\nrect = 0 120 320 20\n", 1, "digger = 1", 320, 160);
		Critter &k = b.drop (100, 59);
		b.w->assign (0, R_DIGGER);
		bool fell = false;
		for (int s = 0; s < 100; s++) { b.w->tick (); fell |= k.state == S_FALL; }
		CHECK (fell && k.state == S_WALK && k.y == 119, "through to a cave: falls and lands: %d at %d", k.state, k.y);
	}
}

static void test_exploder ()
{
	{
		Bench b ("[shape]\nrect = 0 60 320 40\n[shape]\nrect = 105 62 3 3\nmaterial = steel\n[shape]\nrect = 92 62 3 3\nmaterial = water\n", 1,
			 "blocker = 1\nexploder = 1");
		Critter &k = b.drop (100, 59);
		b.w->assign (0, R_BLOCKER);
		Terrain before; before.bg = b.w->t.bg; before.alloc (320, 100);
		memcpy (before.m, b.w->t.m, 320 * 100);
		b.w->assign (0, R_EXPLODER);
		int ticks = 0, burst = -1;
		for (int s = 0; s < 120; s++)
		{
			b.w->tick ();
			for (int e = 0; e < b.w->nev; e++)
			{
				if (b.w->ev[e].kind == E_TICK) ticks++;
				if (b.w->ev[e].kind == E_BURST) burst = s + 1;
			}
		}
		CHECK (burst == 100 && ticks == 5, "the burst after %d steps, %d ticks", burst, ticks);
		int diff = 0;
		for (int y = 0; y < 100; y++) for (int x = 0; x < 320; x++)
		{
			int dx = x - 100, dy = y - 55, was = before.m[y * 320 + x], now = b.w->t.m[y * 320 + x];
			int want = was == M_EARTH && dx * dx + dy * dy <= 144 ? M_EMPTY : was;
			diff += now != want;
		}
		CHECK (diff == 0, "the burst's disc: %d pixels differ", diff);
		CHECK (count_mat (b.w->t, M_STEEL) == 9 && count_mat (b.w->t, M_WATER) == 9, "steel and water kept");
		CHECK (k.state == S_DEAD && b.w->dead == 1 && b.w->result == LOST, "the exploder dies: %d", k.state);
	}
	{	// in the air (a floater, given Exploder while falling)
		Bench b ("[shape]\nrect = 0 150 320 10\n", 1, "exploder = 1", 320, 160);
		Critter &k = b.drop (100, 5);
		k.flags = F_FLOATER;
		CHECK (b.w->assign (0, R_EXPLODER) == OK, "an exploder while falling");
		b.run (99);
		CHECK (k.state == S_FALL, "still falling");
		b.run (1);
		CHECK (k.state == S_BURST && !b.w->t.solid (k.x, k.y + 1), "bursts in the air at %d", k.y);
	}
}

static void test_assign ()
{
	Bench b (FLOOR, 5, "climber = 2\nfloater = 1\nblocker = 1\nbuilder = 1\ndigger = 1\nexploder = 2");
	World &w = *b.w;
	b.drop (50, 79); b.drop (100, 30); b.drop (200, 79);
	CHECK (w.assign (0, R_CLIMBER) == OK && w.roles[R_CLIMBER] == 1, "climber: count - 1");
	CHECK (w.assign (0, R_CLIMBER) == ALREADY && w.roles[R_CLIMBER] == 1, "climber twice refused");
	CHECK (w.assign (0, R_FLOATER) == OK && w.c[0].flags == (F_CLIMBER | F_FLOATER), "climber + floater");
	CHECK (w.assign (1, R_FLOATER) == NO_COUNT, "a role at 0");
	CHECK (w.assign (1, R_DIGGER) == NOT_ON_GROUND && w.roles[R_DIGGER] == 1, "digger on a faller refused");
	CHECK (w.assign (2, R_BLOCKER) == OK && w.roles[R_BLOCKER] == 0, "blocker");
	CHECK (w.assign (2, R_BUILDER) == IS_BLOCKER && w.roles[R_BUILDER] == 1, "builder on a blocker refused");
	CHECK (w.assign (0, R_EXPLODER) == OK && w.roles[R_EXPLODER] == 1, "exploder");
	CHECK (w.assign (0, R_EXPLODER) == COUNTING_DOWN && w.roles[R_EXPLODER] == 1, "exploder twice refused");
	CHECK (w.assign (7, R_CLIMBER) == NOT_ALIVE && w.assign (-1, R_CLIMBER) == NOT_ALIVE, "nobody there");
	CHECK (w.assign (0, R_BASHER) == NO_COUNT && w.assign (0, 99) == NO_COUNT, "no such role");
	CHECK (w.can_take (0, R_DIGGER) == OK && w.roles[R_DIGGER] == 1, "can_take does not act");
	// the cursor: two creatures on the same spot, the first can not take Climber
	b.drop (150, 79); b.drop (150, 79);
	w.c[3].flags = F_CLIMBER;
	CHECK (w.pick (150, 75, R_CLIMBER) == 4, "pick: the first that can take it (%d)", w.pick (150, 75, R_CLIMBER));
	CHECK (w.pick (150, 75, R_DIGGER) == 3, "pick: the first in release order");
	w.c[4].flags = F_CLIMBER;
	CHECK (w.pick (152, 70, R_CLIMBER) == 3, "pick: none can, the nearest (lowest index)");
	CHECK (w.pick (160, 75, R_DIGGER) == -1 && w.pick (150, 81, R_DIGGER) == -1 && w.pick (150, 67, R_DIGGER) == -1, "pick: outside the boxes");
	CHECK (w.pick (154, 68, R_DIGGER) == 3 && w.pick (146, 79, R_DIGGER) == 3, "pick: the box's corners");
	// leaving, dead
	w.c[3].state = S_EXIT;
	CHECK (w.can_take (3, R_CLIMBER) == LEAVING, "leaving");
	w.c[3].state = S_SPLAT;
	CHECK (w.can_take (3, R_EXPLODER) == NOT_ALIVE, "dying");
	w.c[4].state = S_SHRUG;
	CHECK (w.can_take (4, R_BUILDER) == OK && w.can_take (4, R_DIGGER) == NOT_ON_GROUND, "a shrugging builder");
}

static void test_nuke ()
{
	Bench b ("[shape]\nrect = 0 80 320 20\n[hatch]\nat = 20 70\n[exit]\nat = 317 2\n", 10, "rate = 99\nexploder = 3");
	World &w = *b.w;
	b.run (58);
	int out = w.nout;
	CHECK (out == 5, "%d out before the nuke", out);
	w.nuke ();
	int fuseAt[10]; for (int i = 0; i < 10; i++) fuseAt[i] = -1;
	int lastBurst = -1;
	while (w.result == PLAYING && w.step < 1000)
	{
		int s = w.step;
		w.tick ();
		for (int i = 0; i < w.nout; i++) if (fuseAt[i] < 0 && w.c[i].fuse >= 0) fuseAt[i] = s;
		for (int e = 0; e < w.nev; e++) if (w.ev[e].kind == E_BURST) lastBurst = s;
	}
	CHECK (w.nout == out, "no more out after the nuke (%d)", w.nout);
	for (int i = 0; i < out; i++) CHECK (fuseAt[i] == 58 + i, "creature %d's fuse at %d", i, fuseAt[i]);
	CHECK (w.result == LOST && w.dead == out && w.endStep == lastBurst + DIE_STEPS + 1, "ends after the last burst (%d, %d)", w.endStep, lastBurst);
	CHECK (w.roles[R_EXPLODER] == 3, "no exploder count taken");
}

// ---- D. the rules ------------------------------------------------------------------------------------------------------
static void test_end ()
{
	for (int k = 1; k <= 3; k++)		// save = 2: k saved of 3
	{
		Bench b ("[shape]\nrect = 0 80 120 20\n[shape]\nrect = 160 80 160 20\n[hatch]\nat = 2 2\n[exit]\nat = 250 79\n", 3, "save = 2");
		for (int i = 0; i < 3; i++) b.drop (i < k ? 200 : 100, 79, i < k ? 1 : 1);
		b.run (400);
		CHECK (b.w->result == (k >= 2 ? WON : LOST) && b.w->saved == k && b.w->dead == 3 - k && b.w->endStep < 400, "%d saved of 3 (save 2): result %d", k, b.w->result);
	}
	{	// the time runs out: those in play are lost
		Bench b ("[shape]\nrect = 0 80 320 20\n[hatch]\nat = 2 2\n[exit]\nat = 250 79\n", 2, "time = 30");
		b.drop (200, 79); b.drop (100, 79, -1);
		b.w->c[1].state = S_BLOCK;
		b.run (700);
		CHECK (b.w->result == WON && b.w->endStep == 600 && b.w->saved == 1 && b.w->dead == 1 && b.w->inPlay () == 0, "time out, won: %d at %d", b.w->result, b.w->endStep);
	}
	{
		Bench b (FLOOR, 2, "time = 30");
		b.drop (200, 79); b.drop (100, 79);
		b.run (700);
		CHECK (b.w->result == LOST && b.w->endStep == 600 && b.w->dead == 2, "time out, lost");
		int before = b.w->step;
		b.w->tick ();
		CHECK (b.w->step == before, "nothing after the end");
	}
	{	// only blockers left (04 D13)
		Bench b (FLOOR, 2, "blocker = 1");
		b.drop (100, 79); b.drop (200, 79);
		CHECK (!b.w->onlyBlockersLeft (), "not yet");
		b.w->assign (0, R_BLOCKER);
		b.w->c[1].state = S_SAVED; b.w->saved = 1;
		CHECK (b.w->onlyBlockersLeft (), "only blockers left");
		b.w->nuke ();
		CHECK (!b.w->onlyBlockersLeft (), "not while nuking");
	}
}

static void test_clock ()
{
	Clock c;
	CHECK (c.due (50) == 1 && c.due (25) == 0 && c.due (25) == 1 && c.due (100) == 2, "50 ms a step");
	CHECK (c.due (1000) == 4 && c.acc3 == 0, "at most 4, the rest dropped");
	c.fast = true;
	CHECK (c.due (50) == 3 && c.due (500) == 12 && c.acc3 == 0, "fast: 3 per 50 ms, at most 12");
	c.paused = true;
	CHECK (c.due (100) == 0 && c.acc3 == 0, "paused");
}

// The same solution with the clock: random dt, pauses and fast -> the same final checksum (AC-21)
static void clock_run (const Level &lv, const Solution *s, const char *what)
{
	RunResult want = run_solution (lv, s, 0, 0);
	World *w = new World;
	w->reset (lv);
	Replay rp; rp.start (s);
	Clock c;
	int frames = 0;
	while (w->result == PLAYING && frames < 2000000)
	{
		frames++;
		unsigned dt = 10 + rnd () % 21;
		if (rnd () % 50 == 0) c.paused = !c.paused;
		if (rnd () % 40 == 0) c.fast = !c.fast;
		if (c.paused && rnd () % 5 == 0) c.paused = false;
		for (int n = c.due (dt); n > 0 && w->result == PLAYING; n--) { rp.apply_due (*w); w->tick (); }
	}
	CHECK (w->result == want.result && w->checksum () == want.lastChecksum && rp.refused == want.refused,
	       "%s: the clock's run differs (%d / %d)", what, w->result, want.result);
	delete w;
}

static void test_solution ()
{
	Solution *s = new Solution;
	LoadError e;
	const char *ex = "# training-02-mind-the-gap: 10 out, 8 needed\n60   builder  0          # at step 60, creature 0 (the first out) becomes a builder\n"
			 "156  builder  0\n300  rate     80         # the release rate set to 80\n410  digger   3\n900  nuke                # all explode\n";
	CHECK (load_solution (ex, *s, e) && s->n == 5, "the example: %s", e.reason);
	CHECK (s->a[0].step == 60 && s->a[0].kind == A_ROLE && s->a[0].role == R_BUILDER && s->a[0].arg == 0 && s->a[0].line == 2, "action 0");
	CHECK (s->a[2].kind == A_RATE && s->a[2].arg == 80 && s->a[3].role == R_DIGGER && s->a[3].arg == 3 && s->a[4].kind == A_NUKE && s->a[4].step == 900, "actions");
	CHECK (load_solution ("10 builder 0\n10 digger 1\n\n  # x\n20 pause\n20 fast\r\n", *s, e) && s->n == 4, "equal steps, blanks, pause / fast");
	struct { const char *t; int line; const char *r; } B[] = {
		{ "x builder 0", 1, "bad step" }, { "-5 builder 0", 1, "bad step" }, { "100 builder 0\n50 builder 1", 2, "bad step" },
		{ "10 jumper 0", 1, "bad action" }, { "10 rate 0", 1, "bad action" }, { "10 rate", 1, "bad action" }, { "10 nuke 3", 1, "bad action" },
		{ "10", 1, "bad action" }, { "10 rate 100", 1, "bad action" },
		{ "10 builder", 1, "bad creature" }, { "10 builder x", 1, "bad creature" }, { "\n\n10 builder 80", 3, "bad creature" }, { "10 digger 1 2", 1, "bad creature" } };
	for (unsigned i = 0; i < sizeof B / sizeof B[0]; i++)
	{
		bool ok = load_solution (B[i].t, *s, e);
		CHECK (!ok && e.line == B[i].line && !strcmp (e.reason, B[i].r) && s->n == 0, "'%s': line %d %s", B[i].t, e.line, e.reason);
	}
	{
		static char many[20000];
		int o = 0;
		for (int i = 0; i < 1025; i++) o += snprintf (many + o, sizeof many - (size_t) o, "%d nuke\n", i);
		CHECK (!load_solution (many, *s, e) && e.line == 1025 && !strcmp (e.reason, "too many actions (max 1024)"), "1025 actions: %d %s", e.line, e.reason);
	}
	// the recorder's text reads back the same
	{
		Recorder *r = new Recorder;
		r->add (40, A_ROLE, R_DIGGER, 0); r->add (40, A_ROLE, R_CLIMBER, 3); r->add (55, A_RATE, 0, 77); r->add (60, A_PAUSE, 0, 0);
		r->add (61, A_FAST, 0, 0); r->add (900, A_NUKE, 0, 0);
		char text[512];
		int n = r->text (text, sizeof text);
		CHECK (n == (int) strlen (text) && load_solution (text, *s, e) && s->n == 6, "the recorder's text: %s", e.reason);
		bool same = s->n == 6;
		for (int i = 0; i < s->n && same; i++)
			same = s->a[i].step == r->s.a[i].step && s->a[i].kind == r->s.a[i].kind && s->a[i].arg == r->s.a[i].arg
			       && (s->a[i].kind != A_ROLE || s->a[i].role == r->s.a[i].role);
		CHECK (same, "the recorder's actions read back");
		CHECK (r->text (text, 10) == 9 && strlen (text) == 9, "cut at the cap");
		delete r;
	}
	// a replay on a rule level: a digger at the right step, a refusal counted; AC-21's clock on it
	{
		Bench b ("[shape]\nrect = 0 80 320 20\n[shape]\nrect = 0 100 320 20\nmaterial = steel\n[shape]\nrect = 150 60 10 20\n[hatch]\nat = 20 70\n[exit]\nat = 200 79\n",
			 4, "digger = 2\nrate = 70", 320, 160);
		const char *sol = "60 digger 0\n60 digger 0\n70 digger 9\n";
		CHECK (load_solution (sol, *s, e), "rule solution");
		RunResult r = run_solution (*b.lv, s, 0, 0);
		CHECK (r.refused == 2 && r.hashOk, "refused %d", r.refused);
		clock_run (*b.lv, s, "rule level");
	}
	delete s;
}

// ---- E. the progress ---------------------------------------------------------------------------------------------------
static void test_progress ()
{
	fk_kv *kv = fk_kv_parse ("# x\n[other]\nfoo = bar\n[training-01-straight-down]\nextra = 7\nsaved = abc\n", FK_KV_ESCAPES);
	const char *sec = "training-01-straight-down";
	Best b = progress_get (kv, sec);
	CHECK (!b.solved && b.saved == -1 && b.timeSec == -1, "a broken line ignored");
	CHECK (!progress_won (kv, sec, 8, 120), "the first win is no new best");
	CHECK (progress_won (kv, sec, 9, 140), "9 saved: a new best");
	// (a lost run writes nothing: the window does not call progress_won)
	b = progress_get (kv, sec);
	CHECK (b.solved && b.saved == 9 && b.timeSec == 120, "solved %d saved %d time %d", b.solved, b.saved, b.timeSec);
	CHECK (!progress_won (kv, sec, 9, 130) && progress_won (kv, sec, 2, 100), "best time on its own");
	progress_set_setting (kv, "sound", "0");
	progress_set_setting (kv, "last", sec);
	const char *text = fk_kv_text (kv, "# Critters -- progress (written by the game)", 0);
	CHECK (strstr (text, "foo = bar") && strstr (text, "extra = 7") && strstr (text, "[other]"), "unknown sections and keys kept:\n%s", text);
	fk_kv *kv2 = fk_kv_parse (text, FK_KV_ESCAPES);
	Best c = progress_get (kv2, sec);
	CHECK (c.solved && c.saved == 9 && c.timeSec == 100, "read back: %d %d %d", c.solved, c.saved, c.timeSec);
	CHECK (!strcmp (progress_setting (kv2, "sound", "1"), "0") && !strcmp (progress_setting (kv2, "last", ""), sec)
	       && !strcmp (progress_setting (kv2, "nothing", "d"), "d"), "settings");
	char out[64];
	progress_section ("SD:/apps/critters.app/levels/training-01-straight-down.level", true, out, sizeof out);
	CHECK (!strcmp (out, "training-01-straight-down"), "section %s", out);
	progress_section ("SD:/docs/critters/My.LEVEL", false, out, sizeof out);
	CHECK (!strcmp (out, "user.My"), "section %s", out);
	progress_section ("quick.lvl", false, out, sizeof out);
	CHECK (!strcmp (out, "user.quick.lvl"), "section %s", out);
	// AC-24: the chain
	const char *chain[12] = { "training-01-straight-down", "training-02-mind-the-gap", "training-03-hold-the-line", "training-04-up-the-wall",
				  "training-05-soft-landing", "training-06-blast-through", "expedition-01-two-ways", "expedition-02-steel-floor",
				  "expedition-03-the-climb", "expedition-04-lava-lake", "expedition-05-the-maze", "expedition-06-grand-tour" };
	fk_kv *p = fk_kv_new (FK_KV_ESCAPES);
	int open = 0;
	for (int k = 0; k < 12; k++) open += progress_open (p, chain, 12, k);
	CHECK (open == 1 && progress_open (p, chain, 12, 0), "nothing solved: only Training 1 open (%d)", open);
	for (int k = 0; k < 12; k++)
	{
		progress_won (p, chain[k], 5, 60);
		for (int j = 0; j < 12; j++) CHECK (progress_open (p, chain, 12, j) == (j <= k + 1), "solved up to %d: level %d", k, j);
	}
	fk_kv *q = fk_kv_new (FK_KV_ESCAPES);
	for (int k = 0; k < 6; k++) progress_won (q, chain[k], 5, 60);
	CHECK (progress_open (q, chain, 12, 6) && !progress_open (q, chain, 12, 7), "Expedition 1 opens with Training 6");
	fk_kv_free (kv); fk_kv_free (kv2); fk_kv_free (p); fk_kv_free (q);
}

// ---- F. the shipped levels with their solutions -------------------------------------------------------------------------
struct Stream { unsigned long long *v; int n, cap; };
static void collect (int step, uint64_t sum, void *u)
{
	Stream *s = (Stream *) u;
	(void) step;
	if (s->n < s->cap) s->v[s->n++] = sum;
}
static void test_solutions (int n, char **files)
{
	for (int i = 0; i < n; i++)
	{
		const char *b = base_of (files[i]);
		char *text = read_file (files[i]);
		Level *lv = new Level;
		LoadError e;
		if (!text || !load_level (text, *lv, e)) { free (text); delete lv; continue; }	// (refused: test_shipped said so)
		char path[512], base[128];
		snprintf (base, sizeof base, "%.*s", (int) (strlen (b) - 6), b);
		snprintf (path, sizeof path, "tools/tests/critters/solutions/%s.sol", base);
		char *st = read_file (path);
		CHECK (st, "%s missing", path);
		Solution *s = new Solution;
		s->n = 0;
		if (st) CHECK (load_solution (st, *s, e), "%s: line %d: %s", path, e.line, e.reason);
		// AC-30: won by its solution, nothing refused, before the time
		Stream s1 = { new unsigned long long[30000], 0, 30000 }, s2 = { new unsigned long long[30000], 0, 30000 };
		RunResult r = run_solution (*lv, s, collect, &s1);
		CHECK (r.result == WON && r.refused == 0 && r.endStep < lv->timeSec * 20, "%s: not won by its solution (%d/%d, refused %d, step %d)", base,
		       r.saved, r.needed, r.refused, r.endStep);
		printf ("     %-28s %s %2d/%-2d of %-2d  %d:%02d  (limit %d:%02d)\n", base, r.result == WON ? "won " : "LOST", r.saved, r.needed, lv->count,
			r.endStep / 20 / 60, r.endStep / 20 % 60, lv->timeSec / 60, lv->timeSec % 60);
		totalSteps += r.endStep;
		// AC-20: the same run again, step by step
		RunResult r2 = run_solution (*lv, s, collect, &s2);
		bool same = s1.n == s2.n && r2.lastChecksum == r.lastChecksum;
		for (int k = 0; k < s1.n && same; k++) same = s1.v[k] == s2.v[k];
		CHECK (same && r.hashOk, "%s: the two runs differ (or the hash: %d)", base, r.hashOk);
		for (int k = 0; k < s1.n; k++) { fingerprint ^= s1.v[k]; fingerprint *= 1099511628211ull; }
		// AC-21: the clock, pauses, fast
		clock_run (*lv, s, base);
		// AC-31: lost when nothing is done; a Training level lost without its role
		RunResult z = run_solution (*lv, 0, 0, 0);
		CHECK (z.result == LOST, "%s: won when nothing is done (%d/%d)", base, z.saved, z.needed);
		totalSteps += z.endStep;
		if (!strncmp (base, "training-", 9))
		{
			int t = atoi (base + 9) - 1;
			int role = -1;
			for (int k = 0; k < NROLES && t >= 0 && t < 6; k++) if (!strcmp (ROLE_WORD[k], TAUGHT[t])) role = k;
			CHECK (role >= 0 && lv->roles[role] > 0, "%s: does not give %s", base, t >= 0 && t < 6 ? TAUGHT[t] : "?");
			if (role >= 0)
			{
				Level *l2 = new Level;
				*l2 = *lv;
				l2->roles[role] = 0;
				Solution *s3 = new Solution;
				s3->n = 0;
				for (int k = 0; k < s->n; k++) if (!(s->a[k].kind == A_ROLE && s->a[k].role == role)) s3->a[s3->n++] = s->a[k];
				RunResult y = run_solution (*l2, s3, 0, 0);
				CHECK (y.result == LOST, "%s: won without %s (%d/%d)", base, TAUGHT[t], y.saved, y.needed);
				totalSteps += y.endStep;
				delete s3; delete l2;
			}
		}
		delete[] s1.v; delete[] s2.v;
		delete s; free (st); free (text); delete lv;
	}
}

int main (int argc, char **argv)
{
	int nfiles = argc - 1;
	test_terrain ();
	test_example ();
	test_terrain_build ();
	test_release ();
	test_walk ();
	test_fall ();
	test_hazards ();
	test_exit ();
	test_climber ();
	test_blocker ();
	test_builder ();
	test_digger ();
	test_exploder ();
	test_assign ();
	test_nuke ();
	test_end ();
	test_clock ();
	test_solution ();
	test_progress ();
	if (nfiles > 0)
	{
		test_shipped (nfiles, argv + 1);
		test_solutions (nfiles, argv + 1);
	}
	delete g_lv;
	free (g_example);
	printf ("determinism fingerprint %016llx\n", fingerprint);
	if (fails) { printf ("FAIL critters: %d of %d checks failed\n", fails, checks); return 1; }
	printf ("ok   critters (%d checks: %d levels, %d solutions, %ld steps)\n", checks, nfiles, nfiles, totalSteps);
	return 0;
}
