//
// pinballtest.cpp -- Pinball's core on the PC (user/Apps/pinball/table.h, physics.h, rules.h, scores.h), the acceptance
// criteria 1-27 of autodev/rounds/04-pinball/02-product-analysis.md §12 as mapped by 03-technical-analysis.md §9.1:
//   A. the tables: the shipped ones (given on the command line), the format's example, every load error with its line;
//   B. the physics: determinism, containment, no dead spot, no tunnelling through walls or flippers, the drain between
//      the flippers, a flipper shot, the plunger;
//   C. the toys: bumper, sling, drop targets and banks, lanes and their rotation, gate, ramp, saucer;
//   D. the rules: the balls of a game, ball save, bonus x multiplier, [rule] blocks, multiball, extra ball, tilt;
//   E. the high scores.
// Every random choice comes from a fixed seed. --quick: fewer launches (the ASan build). Run by
// tools/tests/run_pinball_test.sh (from the repository's root: it reads tools/tests/pinball/{example,rules}.table).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "pinball/table.h"
#include "pinball/physics.h"
#include "pinball/rules.h"
#include "pinball/scores.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

using namespace pinball;
static int fails = 0, checks = 0;
static bool quick = false;
static long launches = 0, relaunches = 0;
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
static Table *load (const char *text, const char *what)
{
	Table *t = new Table;
	LoadError e;
	bool ok = load_table (text, *t, e);
	CHECK (ok, "%s refused: line %d: %s", what, e.line, e.reason);
	if (!ok) { delete t; return 0; }
	return t;
}
static Vec V (double x, double y) { Vec v = { x, y }; return v; }
static double speed (Vec v) { return sqrt (v.x * v.x + v.y * v.y); }
static unsigned g_seed = 1;
static unsigned rnd () { unsigned x = g_seed; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_seed = x; }
static int rnd_n (int n) { return (int) (rnd () % (unsigned) n); }
static const Input NOINPUT = { false, false, false, false };
static int count_ev (const World &w, int kind, int index = -1)
{
	int n = 0;
	for (int i = 0; i < w.nev; i++) if (w.ev[i].kind == kind && (index < 0 || w.ev[i].index == index)) n++;
	return n;
}
static int count_gev (const Game &g, int kind)
{
	int n = 0;
	for (int i = 0; i < g.nev; i++) if (g.ev[i].kind == kind) n++;
	return n;
}
static const char *table_name (const Table &t) { return t.name.en; }
// The lower left and right flippers (the largest pivot y of each side)
static int lower_flipper (const Table &t, int side)
{
	int best = -1;
	for (int f = 0; f < t.nflip; f++)
		if (t.flip[f].side == side && (best < 0 || t.flip[f].pivot.y > t.flip[best].pivot.y)) best = f;
	return best;
}
// The local frame of flipper f at angle a: x along it, y across it
static Vec local (const Table &t, int f, double a, Vec p)
{
	Vec d = V (p.x - t.flip[f].pivot.x, p.y - t.flip[f].pivot.y);
	double c = pb_cos (a), s = pb_sin (a);
	return V (d.x * c + d.y * s, -d.x * s + d.y * c);
}
// The side of a flipper's axis that faces up (towards -y on the table): -1 or +1 in its local frame
static double top_side (double a) { return pb_cos (a) >= 0 ? -1 : 1; }
// A game with no ball in it, ready for balls placed by hand (no drain logic in G_READY)
static void bare_game (Game &g, const Table &t, unsigned seed = 7)
{
	g.start (t, seed);
	for (int b = 0; b < MAXBALL; b++) g.w.ball[b].state = B_OFF;
	g.state = G_READY;
}
// One frame of the game with a given input; the world's events of the frame stay in g.w.ev
static void gframe (Game &g, const Input &in = NOINPUT, bool nudge = false) { g.frame (in, nudge); }
static void launch (Game &g)
{
	Input in = NOINPUT; in.tap = true;
	gframe (g, in);
}
// Every ball in play parked at a quiet place (no toy there) with no speed
static void park (Game &g, Vec at)
{
	for (int b = 0; b < MAXBALL; b++)
		if (g.w.ball[b].state == B_PLAY) { g.w.ball[b].p = V (at.x + 30 * b, at.y); g.w.ball[b].v = V (0, 0); }
}
static void park_frames (Game &g, int n, Vec at) { for (int i = 0; i < n; i++) { park (g, at); gframe (g); } }
// Ball b sent into the drain
static void drain_ball (Game &g, int b)
{
	g.w.ball[b].p = V (238, g.t->drain - 4);
	g.w.ball[b].v = V (0, 300);
}
static void drain_all (Game &g)
{
	for (int b = 0; b < MAXBALL; b++) if (g.w.ball[b].state == B_PLAY) drain_ball (g, b);
	for (int i = 0; i < 5 && g.state == G_PLAY; i++) gframe (g);
}
static void until_ready (Game &g) { for (int i = 0; i < 200 && g.state == G_BALLEND; i++) gframe (g); }

// The text of `base` with its line `line` (1-based) replaced by `with` (which may hold several lines)
static char *replace_line (const char *base, int line, const char *with)
{
	size_t n = strlen (base) + strlen (with) + 2;
	char *o = (char *) malloc (n), *w = o;
	int l = 1;
	for (const char *p = base; *p; )
	{
		const char *e = strchr (p, '\n');
		size_t len = e ? (size_t) (e - p) : strlen (p);
		if (l == line) { memcpy (w, with, strlen (with)); w += strlen (with); }
		else { memcpy (w, p, len); w += len; }
		*w++ = '\n';
		if (!e) break;
		p = e + 1; l++;
	}
	*w = 0;
	return o;
}

// ---- A. the tables ------------------------------------------------------------------------------------------------------
static void test_shipped (const Table &t, const char *path)
{
	const char *nm = table_name (t);
	CHECK (t.nflip >= 2 && t.nflip <= 3, "%s: %d flippers", nm, t.nflip);
	if (strstr (path, "volcano")) CHECK (t.nflip == 3, "Volcano: %d flippers", t.nflip);
	int bumpers = 0, rot = 0, drops = 0, mb = 0;
	for (int i = 0; i < t.ncirc; i++) bumpers += t.circ[i].kind == C_BUMPER;
	for (int i = 0; i < t.nlane; i++) rot += t.rotate >= 0 && t.lane[i].group == t.rotate;
	for (int i = 0; i < t.ntgt; i++) drops += t.tgt[i].drop;
	CHECK (bumpers >= 3, "%s: %d bumpers", nm, bumpers);
	CHECK (t.nsling == 2, "%s: %d slings", nm, t.nsling);
	CHECK (rot >= 3, "%s: %d lanes in the rotate group", nm, rot);
	CHECK (t.nbank >= 1 && drops >= 2, "%s: %d banks", nm, t.nbank);
	CHECK (t.nramp >= 1, "%s: no ramp", nm);
	CHECK (t.nsaucer >= 1, "%s: no saucer", nm);
	CHECK (t.noutline >= 3, "%s: no outline", nm);
	for (int r = 0; r < t.nrule; r++)
	{
		for (int k = 0; k < t.rule[r].nact; k++) mb += t.rule[r].act[k].kind == A_MULTIBALL;
		if (t.rule[r].message.en[0]) CHECK (t.rule[r].message.fr[0], "%s: rule %d has no message.fr", nm, r);
		CHECK (strlen (t.rule[r].message.en) <= 31 && strlen (t.rule[r].message.fr) <= 31, "%s: rule %d's message over 31 bytes", nm, r);
	}
	CHECK (mb >= 1, "%s: no multiball rule", nm);
	CHECK (t.name.fr[0] && t.goal.en[0] && t.goal.fr[0], "%s: name.fr / goal / goal.fr missing", nm);
	for (int i = 0; i < t.nlabel; i++) CHECK (t.label[i].text.fr[0], "%s: label %d has no text.fr", nm, i);
}

static void broken (const char *base, int line, const char *with, int expLine, const char *expReason)
{
	char *text = line > 0 ? replace_line (base, line, with) : (char *) malloc (strlen (base) + strlen (with) + 1);
	if (line <= 0) { strcpy (text, base); strcat (text, with); }
	Table *t = new Table;
	LoadError e;
	bool ok = load_table (text, *t, e);
	CHECK (!ok, "broken '%s' at %d loaded", with, line);
	CHECK (!strcmp (e.reason, expReason), "broken '%s': reason \"%s\", expected \"%s\"", with, e.reason, expReason);
	CHECK (e.line == expLine, "broken '%s': line %d, expected %d", with, e.line, expLine);
	CHECK (t->nseg == 0 && t->nflip == 0 && t->ncirc == 0 && t->nrule == 0, "broken '%s': the table not cleared", with);
	char re[256];
	snprintf (re, sizeof re, e.fmt, e.arg[0], e.arg[1]);
	CHECK (!strcmp (re, e.reason), "broken '%s': fmt + args \"%s\" != reason", with, re);
	delete t;
	free (text);
}

static void test_example (const char *ex)
{
	Table *t = load (ex, "the example");
	if (!t) return;
	int bumpers = 0;
	for (int i = 0; i < t->ncirc; i++) bumpers += t->circ[i].kind == C_BUMPER;
	CHECK (bumpers == 2 && t->nsling == 2 && t->ntgt == 3 && t->nbank == 1 && t->bank[0].n == 3 && t->nlane == 3
	       && t->ngate == 1 && t->nflip == 2 && t->nrule == 2 && t->nlabel == 1 && t->narc == 1 && t->nwall == 4,
	       "example counts: %d bumpers %d slings %d targets %d banks %d lanes %d gates %d flippers %d rules %d labels %d arcs %d walls",
	       bumpers, t->nsling, t->ntgt, t->nbank, t->nlane, t->ngate, t->nflip, t->nrule, t->nlabel, t->narc, t->nwall);
	CHECK (!strcmp (t->name.get (1), "Ma première table") && !strcmp (t->name.get (0), "My First Table"), "example name");
	CHECK (t->rotate == 0 && t->ballsave == 10 && t->drain == 1010 && t->ball == 13 && t->balls == 3, "example values");
	CHECK (t->background == 0x10203A && t->plungerMax == 2600 && t->plungerAuto == 2300, "example defaults");
	CHECK (t->rule[1].when == EV_BANK && t->rule[1].count == 2 && t->rule[1].nact == 1 && t->rule[1].act[0].kind == A_MULTIBALL
	       && t->rule[1].act[0].n == 2, "example rule 2");
	CHECK (t->rule[0].when == EV_LANES && t->rule[0].nact == 2 && t->rule[0].act[0].kind == A_MULT && t->rule[0].act[1].kind == A_SCORE
	       && t->rule[0].act[1].n == 1000, "example rule 1");
	CHECK (fabs (t->flip[0].rest - 30 * PB_PI / 180) < 1e-12 && fabs (t->flip[1].rest - 150 * PB_PI / 180) < 1e-12, "flipper angles");
	CHECK (fabs (pb_sin (PB_PI / 6) - 0.5) < 1e-9 && fabs (pb_cos (PB_PI / 3) - 0.5) < 1e-9 && fabs (pb_sin (-2.5) - sin (-2.5)) < 1e-9,
	       "pb_sin / pb_cos");
	CHECK (inside_outline (*t, V (100, 100)) && !inside_outline (*t, V (600, 100)), "inside_outline");
	delete t;

	// AC 4: an unknown key in a known block loads
	char *s = replace_line (ex, 55, "radius = 28\nsparkle = 1");
	t = load (s, "sparkle");
	delete t; free (s);

	// AC 3: every error of 02 §7.1.4, one line changed
	broken (ex, 63, "[slingg]", 63, "unknown block [slingg]");
	broken (ex, 2, "[post]", 2, "[table] must be the first block, once");
	broken (ex, 15, "[table]", 15, "[table] must be the first block, once");
	broken (ex, 1, "format = 1", 1, "[table] must be the first block, once");
	broken (ex, 4, "# no name", 2, "[table] needs name");
	broken (ex, 66, "# no b", 63, "[sling] needs b");
	broken (ex, 9, "gravity = abc", 9, "bad value for gravity");
	broken (ex, 9, "gravity = 1e3", 9, "bad value for gravity");
	broken (ex, 56, "colour = red", 56, "bad value for colour");
	broken (ex, 18, "points = 0 0, 520 0, 520 1040, 0", 18, "bad value for points");
	broken (ex, 9, "gravity = 9000", 9, "gravity out of range");
	broken (ex, 3, "format = 2", 3, "format out of range");
	broken (ex, 54, "at = 600 330", 54, "at out of range");
	broken (ex, 114, "message = a message far too long for the panel", 114, "message out of range");
	broken (ex, 58, "id = b1", 58, "id b1 used twice");
	broken (ex, 16, "id = rim", 2, "the table needs one closed wall with id = outline");
	broken (ex, 17, "closed = 0", 15, "the table needs one closed wall with id = outline");
	broken (ex, 45, "side  = left", 2, "the table needs 2 or 3 flippers, a left one and a right one");
	broken (ex, 0, "[flipper]\nside = left\npivot = 100 905\nlength = 70\n[flipper]\nside = right\npivot = 380 905\nlength = 70\n",
		127, "the table needs 2 or 3 flippers, a left one and a right one");
	broken (ex, 49, "# no plunger", 2, "the table needs one [plunger]");
	broken (ex, 50, "at = 498 980\n[plunger]\nat = 498 980", 51, "the table needs one [plunger]");
	broken (ex, 111, "when    = bank trip", 111, "unknown trip");
	broken (ex, 111, "when    = banks trio", 111, "bad value for when");
	broken (ex, 113, "do      = multiballs 2", 113, "bad value for do");
	broken (ex, 113, "do      = multiball 4", 113, "bad value for do");
	broken (ex, 12, "rotate     = tops", 12, "unknown tops");
	broken (ex, 100, "bank = duo", 100, "bank duo needs 2 to 8 targets");
	{
		char more[4096] = "", *w = more;
		for (int k = 0; k < 30; k++) w += sprintf (w, "[lane]\nid = x%d\nrect = 10 10 5 5\n", k);
		broken (ex, 0, more, 123 + 3 * 29, "too many lanes (max 32)");
	}
	{
		char *big = (char *) malloc (MAXFILE + 200);
		strcpy (big, ex);
		size_t n = strlen (big);
		while (n < MAXFILE + 100) big[n++] = '#';
		big[n] = 0;
		Table *tb = new Table; LoadError e;
		CHECK (!load_table (big, *tb, e) && e.line == 0 && !strcmp (e.reason, "the file is too big"), "too big: line %d %s", e.line, e.reason);
		load_error_file (e, false);
		CHECK (e.line == 0 && !strcmp (e.reason, "cannot read the file"), "unreadable");
		delete tb; free (big);
	}
}

// ---- B. the physics -----------------------------------------------------------------------------------------------------
struct Watch
{
	const Table *t;
	long outside, fast, crossed, subs;
	bool track;			// segments crossed (AC 8)
	Vec prev[MAXBALL]; int prevState[MAXBALL];
};
static bool seg_cross (Vec p1, Vec p2, Vec a, Vec b)
{
	double o1 = (b.x - a.x) * (p1.y - a.y) - (b.y - a.y) * (p1.x - a.x);
	double o2 = (b.x - a.x) * (p2.y - a.y) - (b.y - a.y) * (p2.x - a.x);
	double o3 = (p2.x - p1.x) * (a.y - p1.y) - (p2.y - p1.y) * (a.x - p1.x);
	double o4 = (p2.x - p1.x) * (b.y - p1.y) - (p2.y - p1.y) * (b.x - p1.x);
	return o1 * o2 < 0 && o3 * o4 < 0;
}
static void watch_hook (const World &w, void *d)
{
	Watch &W = *(Watch *) d;
	W.subs++;
	for (int b = 0; b < MAXBALL; b++)
	{
		const Ball &B = w.ball[b];
		if (B.state == B_PLAY)
		{
			if (!inside_outline (*W.t, B.p)) W.outside++;
			if (speed (B.v) > MAXSPEED + 1e-6) W.fast++;
			if (W.track && W.prevState[b] == B_PLAY)
				for (int i = 0; i < W.t->nseg; i++)
				{
					const Seg &s = W.t->seg[i];
					if (s.kind == S_TARGET) continue;
					if (seg_cross (W.prev[b], B.p, s.a, s.b)) W.crossed++;
				}
		}
		W.prev[b] = B.p; W.prevState[b] = B.state;
	}
}
// AC 11's watch: the ball's centre in flipper f's frame at every sub-step
struct FlipWatch { const Table *t; int f; bool have; double prevSide; int crossed, inside; };
static void flip_hook (const World &w, void *d)
{
	FlipWatch &W = *(FlipWatch *) d;
	const Flip &F = W.t->flip[W.f];
	if (w.ball[0].state != B_PLAY) { W.have = false; return; }
	Vec l = local (*W.t, W.f, w.flipAng[W.f], w.ball[0].p);
	double tt = l.x / F.len, rad = F.r0 + (F.r1 - F.r0) * (tt < 0 ? 0 : tt > 1 ? 1 : tt);
	bool over = l.x >= 0 && l.x <= F.len;
	double side = l.y * top_side (w.flipAng[W.f]);
	if (over && fabs (l.y) < rad) W.inside++;
	if (over && W.have && W.prevSide > 0 && side < 0 && fabs (l.y) < W.t->ball + rad + 20) W.crossed++;
	W.prevSide = side; W.have = over;
}
static unsigned long long fnv (unsigned long long h, const void *p, size_t n)
{
	const unsigned char *c = (const unsigned char *) p;
	for (size_t i = 0; i < n; i++) { h ^= c[i]; h *= 1099511628211ull; }
	return h;
}
// AC 5: a scripted 2-minute game -> the hash of every ball's place and speed at every frame, and the score
static unsigned long long scripted_game (const Table &t, long &score)
{
	Game *g = new Game;
	g->start (t, 0x5EED0001);
	g_seed = 0xC0FFEE;
	unsigned long long h = 1469598103934665603ull;
	Input in = NOINPUT;
	for (int f = 0; f < 7200 && g->state != G_OVER; f++)
	{
		in.tap = false;
		if (rnd_n (12) == 0) in.left = !in.left;
		if (rnd_n (12) == 0) in.right = !in.right;
		in.plunger = g->w.plungerBusy () && (f % 90) < 40;
		bool nudge = f % 1500 == 700;
		if (g->state == G_BALLEND && f % 50 == 0) g->skip ();
		g->frame (in, nudge);
		for (int b = 0; b < MAXBALL; b++) { h = fnv (h, &g->w.ball[b].p, sizeof (Vec)); h = fnv (h, &g->w.ball[b].v, sizeof (Vec)); }
		h = fnv (h, &g->score, sizeof g->score);
	}
	score = g->score;
	delete g;
	return h;
}

// One launch on a fresh game (AC 6, 7): the plunger pulled `pull` frames, then played (flippers at random moments or
// never) until the ball is over or the time is up; a ball back on the plunger is launched again with a tap. -> the
// longest time a ball stayed in play (frames).
static int one_launch (const Table &t, unsigned seed, bool flippers, int maxFrames, Watch &W)
{
	Game *g = new Game;
	g->start (t, seed);
	g->w.onSubStep = watch_hook; g->w.hookData = &W;
	int pull = 12 + rnd_n (49);
	Input in = NOINPUT;
	in.plunger = true;
	for (int i = 0; i < pull; i++) g->frame (in, false);
	in.plunger = false;
	launches++;
	int inPlay = 0, longest = 0;
	for (int f = 0; f < maxFrames && g->state != G_BALLEND && g->state != G_OVER; f++)
	{
		in.tap = false;
		if (flippers)
		{
			if (rnd_n (15) == 0) in.left = !in.left;
			if (rnd_n (15) == 0) in.right = !in.right;
		}
		if (g->w.plungerBusy () && g->w.playBalls () == 0 && g->multiballQueue == 0 && inPlay == 0 && rnd_n (20) == 0)
		{
			in.tap = true;
			relaunches++;
		}
		g->frame (in, false);
		int n = 0;
		for (int b = 0; b < MAXBALL; b++) n += g->w.ball[b].state != B_OFF && g->w.ball[b].state != B_PLUNGER;
		inPlay = n ? inPlay + 1 : 0;
		if (inPlay > longest) longest = inPlay;
	}
	delete g;
	return longest;
}

static void test_physics (const Table &t, bool isExample)
{
	const char *nm = table_name (t);
	// AC 5: determinism
	long s1, s2;
	unsigned long long h1 = scripted_game (t, s1), h2 = scripted_game (t, s2);
	CHECK (h1 == h2 && s1 == s2, "%s: two runs differ (score %ld / %ld)", nm, s1, s2);

	// AC 6: containment, the speed cap
	if (!isExample)
	{
		Watch W; memset (&W, 0, sizeof W); W.t = &t;
		int n = quick ? 50 : 500;
		g_seed = 0x5EED0006;
		for (int k = 0; k < n; k++) one_launch (t, 0x5EED0001 + k, true, 120 * FPS, W);
		CHECK (W.outside == 0, "%s: a ball outside the outline at %ld sub-steps", nm, W.outside);
		CHECK (W.fast == 0, "%s: a ball over %g units/s at %ld sub-steps", nm, MAXSPEED, W.fast);

		// AC 7: no dead spot (flippers never pressed)
		Watch W2; memset (&W2, 0, sizeof W2); W2.t = &t;
		int n2 = quick ? 20 : 200, worst = 0;
		g_seed = 0x5EED0007;
		for (int k = 0; k < n2; k++)
		{
			int l = one_launch (t, 0x5EED1001 + k, false, 70 * FPS, W2);
			if (l > worst) worst = l;
		}
		CHECK (worst < 60 * FPS, "%s: a ball still in play after %d frames without flippers", nm, worst);
		// the ball search must not be needed: one more run per launch counting it
		long searches = 0;
		g_seed = 0x5EED0007;
		for (int k = 0; k < (quick ? 10 : 50); k++)
		{
			Game *g = new Game;
			g->start (t, 0x5EED1001 + k);
			Input in = NOINPUT; in.plunger = true;
			int pull = 12 + rnd_n (49);
			for (int i = 0; i < pull; i++) g->frame (in, false);
			in.plunger = false;
			for (int f = 0; f < 60 * FPS && g->state != G_OVER; f++)
			{
				in.tap = g->w.plungerBusy () && g->w.playBalls () == 0 && f % 40 == 0;
				if (g->state == G_BALLEND) g->skip ();
				g->frame (in, false);
			}
			searches += g->w.searches;
			delete g;
		}
		CHECK (searches == 0, "%s: the ball search fired %ld times", nm, searches);
	}

	// AC 8: wall tunnelling at full speed (the example)
	if (isExample)
	{
		Watch W; memset (&W, 0, sizeof W); W.t = &t; W.track = true;
		for (int k = 0; k < 64; k++)
		{
			World *w = new World;
			w->reset (t, 1);
			w->onSubStep = watch_hook; w->hookData = &W;
			double a = 2 * PB_PI * k / 64;
			for (int b = 0; b < MAXBALL; b++) W.prevState[b] = B_OFF;
			w->addBall (V (238, 520), V (MAXSPEED * pb_cos (a), MAXSPEED * pb_sin (a)));
			for (int f = 0; f < 120; f++) w->step (NOINPUT);
			delete w;
		}
		CHECK (W.crossed == 0, "example: a ball crossed a wall %ld times", W.crossed);
		CHECK (W.outside == 0, "example: outside the outline %ld times", W.outside);
	}

	// AC 9: a ball at rest above the gap between the lower flippers drains between their tips
	int fl = lower_flipper (t, SIDE_LEFT), fr = lower_flipper (t, SIDE_RIGHT);
	{
		World *w = new World;
		w->reset (t, 1);
		Vec tl = w->flipTip (fl), tr = w->flipTip (fr);
		Vec m = V ((tl.x + tr.x) / 2, (tl.y + tr.y) / 2);
		int b = w->addBall (V (m.x, m.y - 120), V (0, 0));
		int drainedAt = -1; double x = 0;
		for (int f = 0; f < 120 && drainedAt < 0; f++)
		{
			Vec before = w->ball[b].p;
			w->step (NOINPUT);
			if (count_ev (*w, P_DRAIN)) { drainedAt = f; x = before.x; }
		}
		CHECK (drainedAt >= 0, "%s: the ball between the flippers did not drain in 2 s", nm);
		CHECK (x > tl.x && x < tr.x, "%s: drained at x %.1f, not between the tips %.1f..%.1f", nm, x, tl.x, tr.x);
		delete w;
	}

	// AC 10: a flipper shot from a ball at rest on the lowered flipper, 2/3 of the way to its tip
	for (int side = 0; side < 2; side++)
	{
		int f = side ? fr : fl;
		const Flip &F = t.flip[f];
		World *w = new World;
		w->reset (t, 1);
		double a = F.rest, ts = top_side (a), x = F.len * 2 / 3, rad = F.r0 + (F.r1 - F.r0) * 2 / 3, off = ts * (t.ball + rad + 0.5);
		Vec p = V (F.pivot.x + x * pb_cos (a) - off * pb_sin (a), F.pivot.y + x * pb_sin (a) + off * pb_cos (a));
		int b = w->addBall (p, V (0, 0));
		Input in = NOINPUT;
		(side ? in.right : in.left) = true;
		bool fastUp = false, high = false;
		for (int k = 0; k < 90; k++)
		{
			w->step (in);
			if (k < 15 && w->ball[b].v.y < -1000) fastUp = true;
			if (w->ball[b].state != B_OFF && w->ball[b].p.y < t.h / 2) high = true;
			if (w->ball[b].state == B_RAMP || w->ball[b].state == B_SAUCER) high = true;
		}
		CHECK (fastUp, "%s: the %s flipper's shot is not fast (vy %.0f)", nm, side ? "right" : "left", w->ball[b].v.y);
		CHECK (high, "%s: the %s flipper's shot does not reach the upper half", nm, side ? "right" : "left");
		delete w;
	}

	// AC 11: no flipper tunnelling: a ball dropped onto each flipper, the press 0..10 frames before the contact; at every
	// sub-step, its centre never passes from the flipper's top side to its bottom side across it, never inside it
	for (int f = 0; f < t.nflip; f++)
	{
		const Flip &F = t.flip[f];
		// (dropped from 60 units straight above the point where it rests on the lowered flipper, 2/3 of the way to its tip)
		double a = F.rest, ts = top_side (a), x = F.len * 2 / 3, off0 = ts * (t.ball + F.r0 + (F.r1 - F.r0) * 2 / 3);
		Vec p = V (F.pivot.x + x * pb_cos (a) - off0 * pb_sin (a), F.pivot.y + x * pb_sin (a) + off0 * pb_cos (a) - 60);
		// the contact's frame, flippers down
		World *w = new World;
		w->reset (t, 1);
		int b = w->addBall (p, V (0, 0)), contact = -1;
		for (int k = 0; k < 120 && contact < 0; k++)
		{
			w->step (NOINPUT);
			Vec l = local (t, f, w->flipAng[f], w->ball[b].p);
			double tt = l.x / F.len; tt = tt < 0 ? 0 : tt > 1 ? 1 : tt;
			if (fabs (l.y) < t.ball + F.r0 + (F.r1 - F.r0) * tt + 1) contact = k;
		}
		delete w;
		CHECK (contact > 10, "%s: flipper %d: no contact for the drop test (%d)", nm, f, contact);
		if (contact <= 10) continue;
		for (int off = 0; off <= 10; off++)
		{
			w = new World;
			w->reset (t, 1);
			FlipWatch FW = { &t, f, false, 0, 0, 0 };
			w->onSubStep = flip_hook; w->hookData = &FW;
			b = w->addBall (p, V (0, 0));
			Input in = NOINPUT;
			for (int k = 0; k < contact + 30 && w->ball[b].state == B_PLAY; k++)
			{
				if (k >= contact - off) (F.side ? in.right : in.left) = true;
				w->step (in);
			}
			CHECK (!FW.crossed && !FW.inside, "%s: flipper %d, pressed %d frames before the contact: crossed %d, inside %d", nm, f, off,
			       FW.crossed, FW.inside);
			delete w;
		}
	}

	// AC 12: the plunger
	{
		struct { int hold; double want, tol; } C[3] = { { 60, t.plungerMax, 0.01 }, { 30, t.plungerMax / 2, 0.02 }, { 2, t.plungerAuto, 0.001 } };
		for (int c = 0; c < 3; c++)
		{
			World *w = new World;
			w->reset (t, 1);
			w->addOnPlunger ();
			Input in = NOINPUT; in.plunger = true;
			for (int k = 0; k < C[c].hold; k++) w->step (in);
			in.plunger = false;
			w->step (in);
			double sp = -1;
			for (int i = 0; i < w->nev; i++) if (w->ev[i].kind == P_LAUNCH) sp = w->ev[i].speed;
			CHECK (fabs (sp - C[c].want) <= C[c].want * C[c].tol, "%s: plunger held %d frames: %.1f, expected %.1f", nm, C[c].hold, sp, C[c].want);
			delete w;
		}
		World *w = new World;
		w->reset (t, 1);
		int b = w->addOnPlunger ();
		Input in = NOINPUT; in.tap = true;
		w->step (in);
		CHECK (count_ev (*w, P_LAUNCH) == 1 && fabs (w->ev[0].speed - t.plungerAuto) < 1e-9, "%s: a tap launches at auto", nm);
		delete w;
		// a full launch passes the shooter gate and never comes back into the lane from above
		const Gate *g = 0;
		for (int i = 0; i < t.ngate; i++)
			if (t.plunger.x > t.gate[i].a.x - 1e-9 && t.plunger.x < t.gate[i].b.x + 1e-9 && t.gate[i].pass.y < 0) g = &t.gate[i];
		CHECK (g != 0, "%s: no shooter gate", nm);
		if (!g) return;
		double gx0 = g->a.x < g->b.x ? g->a.x : g->b.x, gx1 = g->a.x < g->b.x ? g->b.x : g->a.x, gy = g->a.y;
		w = new World;
		w->reset (t, 1);
		b = w->addOnPlunger ();
		in = NOINPUT; in.plunger = true;
		for (int k = 0; k < 60; k++) w->step (in);
		in.plunger = false;
		bool passed = false, back = false;
		for (int k = 0; k < 600 && w->ball[b].state != B_OFF; k++)
		{
			w->step (in);
			Vec q = w->ball[b].p;
			if (w->ball[b].state != B_PLAY) continue;
			if (q.y < gy - t.ball) passed = true;
			else if (passed && q.x > gx0 && q.x < gx1 && q.y > gy) back = true;
		}
		CHECK (passed && !back, "%s: the full launch: passed %d, back in the lane %d", nm, passed, back);
		delete w;
	}
}

// ---- C. the toys --------------------------------------------------------------------------------------------------------
// The frames until an event (or -1), the game stepping
static int frames_until (Game &g, int kind, int max, int *at = 0)
{
	for (int f = 0; f < max; f++)
	{
		gframe (g);
		if (count_ev (g.w, kind)) { if (at) *at = f; return f; }
	}
	return -1;
}
static void test_toys (const Table &ex, const Table &rt)
{
	Game *g = new Game;
	// AC 13: bumper (the example's b1)
	{
		bare_game (*g, ex);
		const Circle &c = ex.circ[0];
		CHECK (c.kind == C_BUMPER, "the example's first circle is a bumper");
		g->w.addBall (V (c.c.x, c.c.y - c.r - ex.ball - 30), V (0, 0));
		int f = frames_until (*g, P_BUMPER, 60);
		CHECK (f >= 0, "bumper: no hit");
		double sp = 0;
		for (int i = 0; i < g->w.nev; i++) if (g->w.ev[i].kind == P_BUMPER) sp = g->w.ev[i].speed;
		CHECK (g->score == c.score, "bumper: score %ld", g->score);
		CHECK (sp >= c.kick, "bumper: left at %.1f < kick %.1f", sp, c.kick);
		int lit = 1;
		while (g->w.bumperFlash[0] > 0 && lit < 30) { gframe (*g); if (g->w.bumperFlash[0] > 0) lit++; CHECK (!count_ev (g->w, P_BUMPER), "bumper hit again"); }
		CHECK (lit == FLASH_FRAMES, "bumper: flash on %d frames", lit);
	}
	// AC 14: sling (the example's sl), at 250 and at 100 units/s
	for (int k = 0; k < 2; k++)
	{
		bare_game (*g, ex);
		const Seg &s = ex.seg[ex.sling[0].seg];
		Vec d = V (s.b.x - s.a.x, s.b.y - s.a.y);
		double L = speed (d);
		Vec n = V (d.y / L, -d.x / L);			// towards the playfield (+x)
		if (n.x < 0) n = V (-n.x, -n.y);
		Vec m = V ((s.a.x + s.b.x) / 2, (s.a.y + s.b.y) / 2);
		double v = k ? 100 : 250;
		int b = g->w.addBall (V (m.x + n.x * (ex.ball + 1), m.y + n.y * (ex.ball + 1)), V (-n.x * v, -n.y * v));
		int hits = 0; double sp = 0;
		for (int f = 0; f < 10; f++)
		{
			gframe (*g);
			for (int i = 0; i < g->w.nev; i++) if (g->w.ev[i].kind == P_SLING) { hits++; sp = g->w.ev[i].speed; }
		}
		if (!k)
		{
			CHECK (hits == 1 && sp >= ex.sling[0].kick, "sling at 250: %d hits, %.1f", hits, sp);
			CHECK (g->score == ex.sling[0].score, "sling at 250: score %ld", g->score);
		}
		else
		{
			CHECK (hits == 0 && g->score == 0, "sling at 100: %d hits, score %ld", hits, g->score);
			CHECK (g->w.ball[b].v.x * n.x + g->w.ball[b].v.y * n.y > 0, "sling at 100: no bounce");
		}
	}
	// AC 15: drop targets (the example's bank trio)
	{
		bare_game (*g, ex);
		int downs[3] = { 0, 0, 0 }, banks = 0, bankFrame = -1, frame = 0;
		long before = 0;
		for (int k = 0; k < 3; k++)
		{
			const Seg &s = ex.seg[ex.tgt[k].seg];
			double my = (s.a.y + s.b.y) / 2;
			int b = g->w.addBall (V (s.a.x + ex.ball + 3, my), V (-800, 0));
			before = g->score;
			for (int f = 0; f < 20; f++)
			{
				gframe (*g); frame++;
				for (int i = 0; i < g->w.nev; i++)
				{
					if (g->w.ev[i].kind == P_DROP) downs[g->w.ev[i].index]++;
					if (g->w.ev[i].kind == P_BANK) { banks++; bankFrame = frame; }
				}
			}
			CHECK (downs[k] == 1 && g->w.tgtDown[k], "target %d: %d downs", k, downs[k]);
			CHECK (g->score - before == ex.tgt[k].score, "target %d scored %ld", k, g->score - before);
			g->w.ball[b].state = B_OFF;
			if (k == 0)
			{
				// a ball crossing the fallen target passes through
				b = g->w.addBall (V (s.a.x + ex.ball + 3, my), V (-800, 0));
				bool through = false;
				for (int f = 0; f < 10; f++) { gframe (*g); frame++; if (g->w.ball[b].p.x < s.a.x - ex.ball) through = true; }
				CHECK (through, "a fallen target stopped the ball");
				CHECK (downs[0] == 1, "a fallen target scored again");
				g->w.ball[b].state = B_OFF;
			}
		}
		CHECK (banks == 1, "bank: %d events", banks);
		int up = -1;
		for (int f = 0; f < 80 && up < 0; f++) { gframe (*g); frame++; if (!g->w.tgtDown[0] && !g->w.tgtDown[1] && !g->w.tgtDown[2]) up = frame; }
		CHECK (up >= 0 && abs (up - bankFrame - BANK_FRAMES) <= 1, "bank: up %d frames after it cleared", up - bankFrame);
	}
	// AC 16: lanes (the example's top lanes l1 l2 l3)
	{
		bare_game (*g, ex);
		const Lane &l = ex.lane[0];
		int b = g->w.addBall (V (l.x + l.w / 2, l.y - 20), V (0, 200));
		int f = frames_until (*g, P_LANE, 20);
		CHECK (f >= 0 && g->w.laneLit[0] && g->score == l.score, "lane: lit %d, score %ld", g->w.laneLit[0], g->score);
		g->w.ball[b].state = B_OFF;
		struct { bool in[3]; int dir; bool out[3]; } R[4] = {
			{ { 1, 1, 0 }, -1, { 1, 0, 1 } }, { { 1, 1, 0 }, 1, { 0, 1, 1 } },
			{ { 1, 0, 0 }, -1, { 0, 0, 1 } }, { { 0, 0, 1 }, 1, { 1, 0, 0 } } };
		for (int k = 0; k < 4; k++)
		{
			for (int i = 0; i < 3; i++) g->w.laneLit[i] = R[k].in[i];
			Input in = NOINPUT;
			(R[k].dir < 0 ? in.left : in.right) = true;
			gframe (*g, in);
			gframe (*g);
			CHECK (g->w.laneLit[0] == R[k].out[0] && g->w.laneLit[1] == R[k].out[1] && g->w.laneLit[2] == R[k].out[2],
			       "lane rotation %d: %d%d%d", k, g->w.laneLit[0], g->w.laneLit[1], g->w.laneLit[2]);
		}
		g->w.laneLit[0] = g->w.laneLit[1] = true; g->w.laneLit[2] = false;
		const Lane &l3 = ex.lane[2];
		b = g->w.addBall (V (l3.x + l3.w / 2, l3.y - 20), V (0, 200));
		int n = 0;
		for (int k = 0; k < 20; k++) { gframe (*g); n += count_ev (g->w, P_LANES); }
		CHECK (n == 1 && !g->w.laneLit[0] && !g->w.laneLit[1] && !g->w.laneLit[2], "lanes complete: %d events, lit %d%d%d", n,
		       g->w.laneLit[0], g->w.laneLit[1], g->w.laneLit[2]);
	}
	// AC 17: the gate (the example's shooter gate, pass up)
	{
		bare_game (*g, ex);
		const Gate &gt = ex.gate[0];
		double x = (gt.a.x + gt.b.x) / 2;
		int b = g->w.addBall (V (x, gt.a.y + 40), V (0, -1500));
		bool through = false;
		for (int f = 0; f < 10; f++) { gframe (*g); if (g->w.ball[b].p.y < gt.a.y - ex.ball) through = true; }
		CHECK (through, "gate: the allowed way stopped");
		g->w.ball[b].state = B_OFF;
		b = g->w.addBall (V (x, gt.a.y - 15), V (0, 500));
		bool crossed = false;
		for (int f = 0; f < 30; f++) { gframe (*g); if (g->w.ball[b].p.y > gt.a.y) crossed = true; }
		CHECK (!crossed, "gate: crossed the wrong way");
	}
	// AC 18: the ramp (rules.table)
	{
		bare_game (*g, rt);
		const Ramp &r = rt.ramp[0];
		double x = (r.a.x + r.b.x) / 2;
		int b = g->w.addBall (V (x, r.a.y + 30), V (0, -900));
		int in = -1, out = -1;
		for (int f = 0; f < 200 && out < 0; f++)
		{
			gframe (*g);
			if (count_ev (g->w, P_RAMP)) { in = f; CHECK (g->w.ball[b].state == B_RAMP, "ramp: the ball still in play"); }
			if (count_ev (g->w, P_RAMP_OUT)) out = f;
		}
		int want = (int) (r.time * FPS + 0.5);
		CHECK (in >= 0 && out >= 0 && abs (out - in - want) <= 1, "ramp: in %d, out %d (%d frames, want %d)", in, out, out - in, want);
		CHECK (g->score == r.score, "ramp: score %ld", g->score);
		const Vec &e = r.path[r.npath - 1];
		CHECK (g->w.ball[b].p.x == e.x && g->w.ball[b].p.y == e.y && g->w.ball[b].v.x == r.out.x && g->w.ball[b].v.y == r.out.y,
		       "ramp: came back at (%.1f %.1f) at (%.1f %.1f)", g->w.ball[b].p.x, g->w.ball[b].p.y, g->w.ball[b].v.x, g->w.ball[b].v.y);
		g->w.ball[b].state = B_OFF;
		long before = g->score;
		b = g->w.addBall (V (x, r.a.y - 30), V (0, 600));
		int n = 0;
		for (int f = 0; f < 20; f++) { gframe (*g); n += count_ev (g->w, P_RAMP); }
		CHECK (n == 0 && g->score == before && g->w.ball[b].state == B_PLAY && g->w.ball[b].p.y > r.a.y, "ramp: the other way entered it");
	}
	// AC 19: the saucer (rules.table)
	{
		bare_game (*g, rt);
		const Saucer &s = rt.saucer[0];
		int b = g->w.addBall (V (s.c.x, s.c.y - 30), V (0, 200));
		int in = -1, out = -1, catches = 0;
		for (int f = 0; f < 300 && out < 0; f++)
		{
			gframe (*g);
			if (count_ev (g->w, P_SAUCER)) { in = f; catches++; }
			if (count_ev (g->w, P_EJECT)) out = f;
		}
		int want = (int) (s.hold * FPS + 0.5);
		CHECK (catches == 1 && in >= 0 && out >= 0 && abs (out - in - want) <= 1, "saucer: in %d, out %d (want %d)", in, out, want);
		CHECK (g->score == s.score, "saucer: score %ld", g->score);
		CHECK (g->w.ball[b].v.x == s.out.x && g->w.ball[b].v.y == s.out.y, "saucer: ejected at (%.1f %.1f)", g->w.ball[b].v.x, g->w.ball[b].v.y);
		for (int f = 0; f < 30; f++) { gframe (*g); catches += count_ev (g->w, P_SAUCER); }
		CHECK (catches == 1, "saucer: caught again after the eject");
		g->w.ball[b].state = B_OFF;
		b = g->w.addBall (V (s.c.x, s.c.y - 60), V (0, 1600));
		int n = 0;
		for (int f = 0; f < 10; f++) { gframe (*g); n += count_ev (g->w, P_SAUCER); }
		CHECK (n == 0, "saucer: caught a ball at 1600");
	}
	delete g;
}

// ---- D. the rules -------------------------------------------------------------------------------------------------------
static PEvent pev (int kind, int index) { PEvent e = { kind, index, 0, 0 }; return e; }
static void test_rules (const char *ex, const Table &rt)
{
	char *nosave = replace_line (ex, 11, "ballsave = 0");
	Table *t = load (nosave, "the example, ballsave 0");
	free (nosave);
	if (!t) return;
	Game *g = new Game;
	const Vec P = V (238, 450);
	// AC 20: three drains -> game over; the score changed by the bonus only
	{
		g->start (*t, 1);
		CHECK (g->state == G_READY && g->ball == 1 && g->w.plungerBusy (), "start: ball 1 on the plunger");
		long bonusSum = 0;
		for (int k = 0; k < 3; k++)
		{
			launch (*g);
			CHECK (g->state == G_PLAY, "ball %d: not in play after the launch", k + 1);
			park_frames (*g, 10, P);
			g->bonus = 100 * (k + 1); bonusSum += g->bonus;
			drain_all (*g);
			CHECK (g->state == G_BALLEND && count_gev (*g, GE_BALLEND) == 1, "ball %d: no ball end (state %d)", k + 1, g->state);
			until_ready (*g);
		}
		CHECK (g->state == G_OVER && g->score == bonusSum, "3 drains: state %d, score %ld (bonus %ld)", g->state, g->score, bonusSum);
	}
	// AC 21: ball save (rules.table: ballsave 8)
	{
		g->start (rt, 1);
		launch (*g);
		park_frames (*g, 3 * FPS, P);
		CHECK (g->ballSaveActive (), "ball save not running at 3 s");
		drain_all (*g);
		CHECK (g->state == G_READY && g->ball == 1 && g->w.plungerBusy (), "drain at 3 s: not saved (state %d, ball %d)", g->state, g->ball);
		g->start (rt, 1);
		launch (*g);
		park_frames (*g, 9 * FPS, P);
		drain_all (*g);
		CHECK (g->state == G_BALLEND, "drain at 9 s: saved");
		until_ready (*g);
		CHECK (g->state == G_READY && g->ball == 2, "drain at 9 s: ball %d", g->ball);
	}
	// AC 22: the bonus
	{
		g->start (*t, 1);
		launch (*g);
		park_frames (*g, 5, P);
		g->handle (pev (P_BUMPER, 0));			// 100 -> bonus 10
		g->handle (pev (P_DROP, 0));			// 500 -> 50
		g->handle (pev (P_LANE, 0));			// 50 -> 0
		CHECK (g->score == 650 && g->bonus == 60, "bonus: score %ld, bonus %ld", g->score, g->bonus);
		Action m = { A_MULT, 1 };
		g->fire (m); g->fire (m);
		CHECK (g->mult == 3, "multiplier %d", g->mult);
		drain_all (*g);
		CHECK (g->lastBonus == 180 && g->score == 830, "bonus at the drain: %ld, score %ld", g->lastBonus, g->score);
		until_ready (*g);
		CHECK (g->mult == 1 && g->bonus == 0 && g->ball == 2, "next ball: mult %d, bonus %ld", g->mult, g->bonus);
		for (int k = 0; k < 7; k++) g->fire (m);
		CHECK (g->mult == MAXMULT, "multiplier over 5: %d", g->mult);
	}
	delete t;
	// AC 23: the rules (the example + a "once" rule)
	{
		char *s = (char *) malloc (strlen (ex) + 200);
		strcpy (s, ex);
		strcat (s, "\n[rule]\nwhen = hit b1\ncount = 2\nonce = 1\ndo = score 7\nmessage = Once!\nmessage.fr = Une fois !\n");
		t = load (s, "the example + a once rule");
		free (s);
		if (!t) return;
		g->start (*t, 1);
		launch (*g);
		park_frames (*g, 5, P);
		g->nev = 0;
		g->handle (pev (P_BANK, 0));
		CHECK (count_gev (*g, GE_MULTIBALL) == 0, "bank x1: multiball");
		g->handle (pev (P_BANK, 0));
		CHECK (count_gev (*g, GE_MULTIBALL) == 1 && count_gev (*g, GE_MESSAGE) == 1 && g->multiballQueue == 1, "bank x2: no multiball");
		long sc = g->score;
		g->nev = 0;
		g->handle (pev (P_LANES, 0));
		CHECK (g->mult == 2 && g->score == sc + 1000 && count_gev (*g, GE_MESSAGE) == 1, "lanes top: mult %d, +%ld", g->mult, g->score - sc);
		g->nev = 0;
		for (int k = 0; k < 6; k++) g->handle (pev (P_BUMPER, 0));
		int once = 0;
		for (int i = 0; i < g->nev; i++) once += g->ev[i].kind == GE_MESSAGE && g->ev[i].index == 2;
		CHECK (once == 1 && g->ruleDone[2], "once rule: fired %d times", once);
		// counters survive a drain
		g->start (*t, 1);
		launch (*g);
		park_frames (*g, 5, P);
		g->handle (pev (P_BANK, 0));
		drain_all (*g); until_ready (*g);
		CHECK (g->ball == 1 || g->ball == 2, "ball");
		launch (*g);
		park_frames (*g, 5, P);
		g->nev = 0;
		g->handle (pev (P_BANK, 0));
		CHECK (count_gev (*g, GE_MULTIBALL) == 1, "the bank counter did not survive the drain");
		delete t;
	}
	// AC 24: multiball (rules.table)
	{
		g->start (rt, 1);
		launch (*g);
		park_frames (*g, 20 * FPS, P);			// past the ball save
		Action mb = { A_MULTIBALL, 2 };
		g->nev = 0;
		g->fire (mb);
		CHECK (count_gev (*g, GE_MULTIBALL) == 1, "multiball 2: no event");
		int launched = 0;
		for (int f = 0; f < 5; f++) { park (*g, P); g->w.ball[0].p = P; gframe (*g); launched += count_ev (g->w, P_LAUNCH); }
		CHECK (launched == 1 && g->w.liveBalls () == 2, "multiball 2: %d launched, %d live", launched, g->w.liveBalls ());
		g->nev = 0;
		g->fire (mb);
		CHECK (count_gev (*g, GE_MULTIBALL) == 0 && g->multiballQueue == 0, "multiball during multiball");
		for (int f = 0; f < 60; f++) { park (*g, P); gframe (*g); }
		drain_ball (*g, 0);
		for (int f = 0; f < 5; f++) gframe (*g);
		CHECK (g->state == G_PLAY && g->ball == 1 && g->w.liveBalls () == 1, "one of two lost: state %d ball %d live %d", g->state, g->ball, g->w.liveBalls ());
		g->bonus = 40;
		int ends = 0;
		for (int b = 0; b < MAXBALL; b++) if (g->w.ball[b].state == B_PLAY) drain_ball (*g, b);
		for (int f = 0; f < 5; f++) { gframe (*g); ends += count_gev (*g, GE_BALLEND); }
		CHECK (ends == 1 && g->state == G_BALLEND && g->lastBonus == 40, "both lost: %d ball ends, bonus %ld", ends, g->lastBonus);
		// multiball 3
		g->start (rt, 1);
		launch (*g);
		park_frames (*g, 20 * FPS, P);
		Action mb3 = { A_MULTIBALL, 3 };
		g->fire (mb3);
		for (int f = 0; f < 80; f++) { park (*g, P); gframe (*g); }
		CHECK (g->w.liveBalls () == 3, "multiball 3: %d balls", g->w.liveBalls ());
	}
	// AC 25: extra ball
	{
		g->start (rt, 1);
		launch (*g);
		park_frames (*g, 20 * FPS, P);
		Action xb = { A_EXTRABALL, 1 };
		g->fire (xb);
		CHECK (g->extraBalls == 1, "extra ball pending");
		drain_all (*g); until_ready (*g);
		CHECK (g->ball == 1 && g->extraBalls == 0 && g->state == G_READY, "extra ball: ball %d", g->ball);
		launch (*g);
		park_frames (*g, 20 * FPS, P);
		drain_all (*g); until_ready (*g);
		CHECK (g->ball == 2, "after the extra ball: ball %d", g->ball);
	}
	// AC 26: tilt
	{
		g->start (rt, 1);
		launch (*g);
		park_frames (*g, 20 * FPS, P);
		g->bonus = 500;
		park (*g, P); gframe (*g, NOINPUT, true);
		park_frames (*g, 60, P);
		park (*g, P); gframe (*g, NOINPUT, true);
		CHECK (count_gev (*g, GE_TILTWARN) == 1 && !g->tilted, "2 nudges: no warning or a tilt");
		park_frames (*g, 60, P);
		park (*g, P); gframe (*g, NOINPUT, true);
		CHECK (count_gev (*g, GE_TILT) == 1 && g->tilted && g->w.flipperDead && g->nudges () == 3, "3 nudges in 5 s: no tilt");
		Input in = NOINPUT; in.left = in.right = true;
		for (int f = 0; f < 10; f++) { park (*g, P); gframe (*g, in); }
		CHECK (g->w.flipAng[0] == rt.flip[0].rest && g->w.flipAng[1] == rt.flip[1].rest, "tilt: a flipper moved");
		long sc = g->score;
		g->handle (pev (P_BUMPER, 0));
		CHECK (g->score == sc, "tilt: a bumper scored");
		drain_all (*g);
		CHECK (g->lastBonus == 0, "tilt: bonus %ld at the drain", g->lastBonus);
		until_ready (*g);
		launch (*g);
		for (int f = 0; f < 10; f++) { park (*g, P); gframe (*g, in); }
		CHECK (g->w.flipAng[0] == rt.flip[0].up && !g->tilted, "the next ball's flippers dead");
		// three nudges over 15 s: no tilt
		g->start (rt, 1);
		launch (*g);
		int warn = 0;
		for (int k = 0; k < 3; k++)
		{
			park (*g, P); gframe (*g, NOINPUT, true);
			warn += count_gev (*g, GE_TILTWARN) + count_gev (*g, GE_TILT);
			park_frames (*g, 6 * FPS, P);
		}
		CHECK (!g->tilted && warn == 0, "3 nudges over 15 s: tilted %d, %d warnings", g->tilted, warn);
		// a nudge changes the velocity by 150 units/s
		World *w = new World;
		w->reset (rt, 3);
		int b = w->addBall (P, V (10, -20));
		Vec v0 = w->ball[b].v;
		w->nudge ();
		double dv = speed (V (w->ball[b].v.x - v0.x, w->ball[b].v.y - v0.y));
		CHECK (fabs (dv - NUDGE_SPEED) <= 1, "nudge: |dv| %.3f", dv);
		delete w;
	}
	delete g;
}

// ---- E. the high scores -------------------------------------------------------------------------------------------------
static void test_scores ()
{
	fk_kv *kv = fk_kv_new (FK_KV_ESCAPES);
	fk_kv_set (kv, "other", "keep", "this");
	const char *S = "1-space-station";
	struct { long s; const char *n; } G[6] = { { 10, "Ten" }, { 50, "First" }, { 30, "Thirty" }, { 50, "Second" }, { 20, "Twenty" }, { 5, "Five" } };
	int ranks[6];
	for (int k = 0; k < 6; k++) ranks[k] = scores_add (kv, S, G[k].s, G[k].n);
	CHECK (ranks[0] == 0 && ranks[1] == 0 && ranks[2] == 1 && ranks[3] == 1 && ranks[4] == 3 && ranks[5] == -1,
	       "ranks %d %d %d %d %d %d", ranks[0], ranks[1], ranks[2], ranks[3], ranks[4], ranks[5]);
	ScoreLine l[TOPN];
	int n = scores_read (kv, S, l);
	CHECK (n == 5 && l[0].score == 50 && !strcmp (l[0].name, "First") && l[1].score == 50 && !strcmp (l[1].name, "Second")
	       && l[2].score == 30 && l[3].score == 20 && l[4].score == 10, "top 5: %d lines", n);
	CHECK (!strcmp (fk_kv_get (kv, S, "1", ""), "50 First") && !strcmp (fk_kv_get (kv, S, "5", ""), "10 Ten"), "lines as written");
	CHECK (!scores_qualifies (kv, S, 10) && scores_qualifies (kv, S, 11) && !scores_qualifies (kv, "x", 0) && scores_qualifies (kv, "x", 1), "qualifies");
	// written and read back
	int len = 0;
	const char *text = fk_kv_text (kv, "Pinball -- high scores (written by the game)", &len);
	fk_kv *kv2 = fk_kv_parse (text, FK_KV_ESCAPES);
	for (int k = 1; k <= 5; k++)
	{
		char key[12]; snprintf (key, sizeof key, "%d", k);
		CHECK (!strcmp (fk_kv_get (kv, S, key, "?"), fk_kv_get (kv2, S, key, "!")), "line %d read back", k);
	}
	CHECK (!strcmp (fk_kv_get (kv2, "other", "keep", ""), "this"), "an unknown section lost");
	// a name with '=', a broken line skipped and replaced
	CHECK (scores_add (kv2, "user.my-table", 95210, " L=a  ") == 0 && !strcmp (fk_kv_get (kv2, "user.my-table", "1", ""), "95210 L a"),
	       "a name with '=': %s", fk_kv_get (kv2, "user.my-table", "1", ""));
	fk_kv_set (kv2, S, "3", "not a score");
	n = scores_read (kv2, S, l);
	CHECK (n == 4 && l[2].score == 20, "a broken line: %d lines", n);
	scores_add (kv2, S, 40, "Forty");
	n = scores_read (kv2, S, l);
	CHECK (n == 5 && l[2].score == 40 && l[3].score == 20 && l[4].score == 10, "after a broken line: %d lines", n);
	CHECK (!strcmp (fk_kv_get (kv2, "other", "keep", ""), "this"), "an unknown section lost after a write");
	char nm[NAMEL];
	scores_clean_name ("A very long name over sixteen", nm);
	CHECK (!strcmp (nm, "A very long name"), "name cut: '%s'", nm);
	scores_clean_name ("Éléonore Lévêque-Dupont", nm);
	CHECK (!strcmp (nm, "Éléonore Lévêque"), "name cut (UTF-8): '%s'", nm);
	scores_clean_name ("  ", nm);
	CHECK (!strcmp (nm, "Player"), "empty name: '%s'", nm);
	// sections and settings
	char sec[64];
	scores_section ("SD:/apps/pinball.app/tables/1-space-station.table", true, sec, sizeof sec);
	CHECK (!strcmp (sec, "1-space-station"), "section %s", sec);
	scores_section ("SD:/docs/pinball/My-Table.TABLE", false, sec, sizeof sec);
	CHECK (!strcmp (sec, "user.My-Table"), "section %s", sec);
	CHECK (!strcmp (scores_setting (kv2, "name", "Player"), "Player"), "no name yet");
	scores_set_setting (kv2, "name", "Steph");
	scores_set_setting (kv2, "sound", "0");
	scores_set_setting (kv2, "table", "1-space-station");
	CHECK (!strcmp (scores_setting (kv2, "name", ""), "Steph") && !strcmp (scores_setting (kv2, "sound", ""), "0")
	       && !strcmp (scores_setting (kv2, "table", ""), "1-space-station"), "settings");
	fk_kv_free (kv); fk_kv_free (kv2);
}

int main (int argc, char **argv)
{
	clock_t t0 = clock ();
	int ntables = 0;
	char *ex = read_file ("tools/tests/pinball/example.table"), *rtext = read_file ("tools/tests/pinball/rules.table");
	CHECK (ex && rtext, "tools/tests/pinball/{example,rules}.table not found (run from the repository's root)");
	if (!ex || !rtext) return 1;
	Table *exT = load (ex, "example.table"), *rT = load (rtext, "rules.table");
	if (!exT || !rT) return 1;
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp (argv[i], "--quick")) { quick = true; continue; }
		char *text = read_file (argv[i]);
		CHECK (text != 0, "%s: cannot read", argv[i]);
		if (!text) continue;
		Table *t = load (text, argv[i]);
		free (text);
		if (!t) continue;
		ntables++;
		test_shipped (*t, argv[i]);
		test_physics (*t, false);
		delete t;
	}
	test_example (ex);
	test_physics (*exT, true);
	test_toys (*exT, *rT);
	test_rules (ex, *rT);
	test_scores ();
	double secs = (double) (clock () - t0) / CLOCKS_PER_SEC;
	free (ex); free (rtext); delete exT; delete rT;
	if (fails) { printf ("FAIL pinball: %d of %d checks failed\n", fails, checks); return 1; }
	printf ("ok   pinball (%d checks: %d tables, %ld launches (%ld relaunched), %.1f s%s)\n", checks, ntables, launches, relaunches,
		secs, quick ? ", quick" : "");
	return 0;
}
