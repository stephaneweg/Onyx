//
// crsim.cpp -- Critters' headless runner (03 §4.2): a level run with or without a solution, on the PC, with the app's
// own core (user/Apps/critters/{terrain,level,world,solution}.cpp) -- the way the shipped levels' solutions are made
// and kept valid. Built by tools/tests/run_critters_test.sh (so it never rots), or by hand:
//
//   g++ -std=c++17 -O2 -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Apps -I kernel/include
//       tools/critters/crsim.cpp user/Apps/critters/{terrain,level,world,solution,progress}.cpp -o crsim
//
//   crsim LEVEL [SOL]                      run to the end (no SOL: nothing done): "won|lost saved/needed time step refused"
//   crsim LEVEL [SOL] --trace              + a line per event and per change of state: "412 c3 walk 130,119"
//   crsim LEVEL [SOL] --at STEP --ppm F    the terrain and the creatures (numbered) at that step as a PPM picture, x2
//   crsim LEVEL [SOL] --where STEP         every creature's number, state, x, y, direction at that step
//   crsim LEVEL [SOL] --search LINE A B    LINE ("builder 0") added at each step from A to B: the results, by ranges
//   crsim LEVEL [SOL] --no ROLE            that role's count set to 0 and its lines removed (AC-31's check)
//   crsim --check LEVEL...                 load only: "OK" or "line N: reason"
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "critters/level.h"
#include "critters/world.h"
#include "critters/solution.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace critters;

static const char *const STATE[] = { "fall", "walk", "climb", "block", "build", "shrug", "dig", "bash", "mine", "exit",
				     "splat", "drown", "burn", "burst", "saved", "dead" };
static const char *const EVENT[] = { "hatch-open", "out", "role", "refuse", "brick-warn", "splat", "drown", "burn", "tick",
				     "burst", "saved", "end", "void" };

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

// ---- a 3 x 5 digit font for the picture's numbers ----------------------------------------------------------------------
static const char *const DIG[10] = { "111101101101111", "010110010010111", "111001111100111", "111001111001111",
				     "101101111001001", "111100111001111", "111100111101111", "111001001001001",
				     "111101111101111", "111101111001111" };
struct Pic
{
	int w, h; unsigned char *p;
	void put (int x, int y, uint32_t c)
	{
		if (x < 0 || y < 0 || x >= w || y >= h) return;
		unsigned char *q = p + 3 * (y * w + x);
		q[0] = (unsigned char) (c >> 16); q[1] = (unsigned char) (c >> 8); q[2] = (unsigned char) c;
	}
	void num (int x, int y, int v, uint32_t c)
	{
		char s[8]; snprintf (s, sizeof s, "%d", v);
		for (int i = 0; s[i]; i++)
			for (int r = 0; r < 5; r++)
				for (int k = 0; k < 3; k++)
					if (DIG[s[i] - '0'][r * 3 + k] == '1') put (x + i * 4 + k, y + r, c);
	}
};

static void picture (const World &w, const char *path)
{
	const Level &lv = *w.lv;
	Pic pc; pc.w = lv.w * 2; pc.h = lv.h * 2; pc.p = (unsigned char *) malloc ((size_t) pc.w * pc.h * 3);
	for (int y = 0; y < pc.h; y++) for (int x = 0; x < pc.w; x++) pc.put (x, y, w.t.col[(y / 2) * lv.w + x / 2]);
	for (int k = 0; k < lv.nhatch; k++)
		for (int dx = -6; dx <= 6; dx++) for (int dy = -4; dy <= 0; dy++) pc.put (2 * (lv.hatch[k].at.x + dx), 2 * (lv.hatch[k].at.y - 10 + dy), 0xFFD040);
	for (int k = 0; k < lv.nexit; k++)
		for (int dx = -4; dx <= 4; dx++) for (int dy = -14; dy <= 0; dy++)
			if (dx == -4 || dx == 4 || dy == -14) pc.put (2 * (lv.exit[k].x + dx), 2 * (lv.exit[k].y + dy), 0x60F0D8);
	for (int i = 0; i < w.nout; i++)
	{
		const Critter &k = w.c[i];
		if (k.state == S_SAVED || k.state == S_DEAD) continue;
		uint32_t col = k.state == S_BLOCK ? 0xFF4040 : k.state == S_BUILD ? 0xFFFF40 : k.state == S_DIG ? 0x40FF40
			: k.state >= S_SPLAT ? 0x808080 : (k.flags & F_FLOATER) ? 0x40C0FF : (k.flags & F_CLIMBER) ? 0xFF80FF : 0xFFFFFF;
		for (int dy = -9; dy <= 0; dy++) for (int dx = -3; dx <= 3; dx++) pc.put (2 * k.x + dx, 2 * k.y + dy * 2, col);
		pc.put (2 * k.x + 4 * k.dir, 2 * k.y - 14, 0);
		pc.num (2 * k.x - 4, 2 * k.y - 28, i, 0xFFFFFF);
	}
	FILE *f = fopen (path, "wb");
	if (!f) { fprintf (stderr, "crsim: cannot write %s\n", path); exit (1); }
	fprintf (f, "P6\n%d %d\n255\n", pc.w, pc.h);
	fwrite (pc.p, 1, (size_t) pc.w * pc.h * 3, f);
	fclose (f);
	free (pc.p);
}

static void print_result (const RunResult &r)
{
	printf ("%s %d/%d %d:%02d step %d refused %d\n", r.result == WON ? "won" : "lost", r.saved, r.needed,
		r.endStep / 20 / 60, r.endStep / 20 % 60, r.endStep, r.refused);
}

int main (int argc, char **argv)
{
	if (argc >= 2 && !strcmp (argv[1], "--check"))
	{
		int bad = 0;
		for (int i = 2; i < argc; i++)
		{
			char *t = read_file (argv[i]);
			Level *lv = new Level;
			LoadError e;
			if (!t) load_error_file (e, false);
			if (t && load_level (t, *lv, e)) printf ("%s: OK\n", argv[i]);
			else { printf ("%s: line %d: %s\n", argv[i], e.line, e.reason); bad++; }
			delete lv; free (t);
		}
		return bad ? 1 : 0;
	}
	const char *levelPath = 0, *solPath = 0, *ppm = 0, *searchLine = 0, *noRole = 0;
	int at = -1, where = -1, sa = 0, sb = 0;
	bool trace = false;
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp (argv[i], "--trace")) trace = true;
		else if (!strcmp (argv[i], "--at") && i + 1 < argc) at = atoi (argv[++i]);
		else if (!strcmp (argv[i], "--ppm") && i + 1 < argc) ppm = argv[++i];
		else if (!strcmp (argv[i], "--where") && i + 1 < argc) where = atoi (argv[++i]);
		else if (!strcmp (argv[i], "--no") && i + 1 < argc) noRole = argv[++i];
		else if (!strcmp (argv[i], "--search") && i + 3 < argc) { searchLine = argv[++i]; sa = atoi (argv[++i]); sb = atoi (argv[++i]); }
		else if (!levelPath) levelPath = argv[i];
		else if (!solPath) solPath = argv[i];
		else { fprintf (stderr, "crsim: what is %s?\n", argv[i]); return 2; }
	}
	if (!levelPath) { fprintf (stderr, "usage: crsim LEVEL [SOL] [--trace] [--at STEP --ppm F] [--where STEP] [--search LINE A B] [--no ROLE] | --check LEVEL...\n"); return 2; }
	char *text = read_file (levelPath);
	Level *lv = new Level;
	LoadError e;
	if (!text || !load_level (text, *lv, e)) { printf ("%s: line %d: %s\n", levelPath, text ? e.line : 0, text ? e.reason : "cannot read the file"); return 1; }
	Solution *sol = new Solution;
	sol->n = 0;
	if (solPath)
	{
		char *st = read_file (solPath);
		if (!st || !load_solution (st, *sol, e)) { printf ("%s: line %d: %s\n", solPath, st ? e.line : 0, st ? e.reason : "cannot read"); return 1; }
		free (st);
	}
	if (noRole)
	{
		int r = -1;
		for (int k = 0; k < NROLES; k++) if (!strcmp (noRole, ROLE_WORD[k])) r = k;
		if (r < 0) { fprintf (stderr, "crsim: no role %s\n", noRole); return 2; }
		lv->roles[r] = 0;
		int o = 0;
		for (int k = 0; k < sol->n; k++) if (!(sol->a[k].kind == A_ROLE && sol->a[k].role == r)) sol->a[o++] = sol->a[k];
		sol->n = o;
	}
	if (searchLine)
	{
		// LINE's step tried from A to B: the solution with the line inserted in step order
		Solution *s2 = new Solution;
		int prevKey = -1; RunResult prev; memset (&prev, 0, sizeof prev); int from = sa;
		for (int st = sa; st <= sb + 1; st++)
		{
			RunResult r; memset (&r, 0, sizeof r);
			int key = -1;
			if (st <= sb)
			{
				char buf[64]; snprintf (buf, sizeof buf, "%d %s\n", st, searchLine);
				Solution one; LoadError e2;
				if (!load_solution (buf, one, e2) || one.n != 1) { printf ("bad line: %s\n", e2.reason); return 1; }
				int o = 0; bool put = false;
				for (int k = 0; k < sol->n; k++)
				{
					if (!put && sol->a[k].step > st) { s2->a[o++] = one.a[0]; put = true; }
					s2->a[o++] = sol->a[k];
				}
				if (!put) s2->a[o++] = one.a[0];
				s2->n = o;
				r = run_solution (*lv, s2, 0, 0);
				key = r.result * 100000 + r.saved * 1000 + r.refused;
			}
			if (st > sa && key != prevKey)
			{
				printf ("%5d..%-5d %s %d/%d refused %d (end %d)\n", from, st - 1, prev.result == WON ? "won " : "lost", prev.saved,
					prev.needed, prev.refused, prev.endStep);
				from = st;
			}
			prevKey = key; prev = r;
		}
		return 0;
	}
	World *w = new World;
	if (!w->reset (*lv)) { printf ("no memory\n"); return 1; }
	Replay rp; rp.start (sol);
	Critter last[MAXCRIT];
	memset (last, 0, sizeof last);
	int lastOut = 0;
	while (w->result == PLAYING)
	{
		if (w->step == at && ppm) picture (*w, ppm);
		if (w->step == where)
		{
			printf ("step %d: out %d saved %d dead %d\n", w->step, w->nout, w->saved, w->dead);
			for (int i = 0; i < w->nout; i++)
				printf ("  c%-2d %-6s %4d,%-4d %s%s%s fuse %d\n", i, STATE[w->c[i].state], w->c[i].x, w->c[i].y, w->c[i].dir > 0 ? ">" : "<",
					w->c[i].flags & F_CLIMBER ? " climber" : "", w->c[i].flags & F_FLOATER ? " floater" : "", w->c[i].fuse);
		}
		int before = rp.refused, nev0 = w->nev;
		rp.apply_due (*w);
		if (trace)
		{
			for (int k = nev0; k < w->nev; k++)		// (assign's events: the refusals and the roles)
				printf ("%d c%d %s %s\n", w->step, w->ev[k].who, EVENT[w->ev[k].kind], w->ev[k].kind == E_ROLE || w->ev[k].kind == E_REFUSE ? ROLE_WORD[w->ev[k].role & 7] : "");
			if (rp.refused != before) printf ("%d REFUSED\n", w->step);
		}
		w->tick ();
		if (trace)
		{
			for (int k = 0; k < w->nev; k++)
				if (w->ev[k].kind != E_TICK) printf ("%d c%d %s %d,%d\n", w->step - 1, w->ev[k].who, EVENT[w->ev[k].kind], w->ev[k].x, w->ev[k].y);
			for (int i = 0; i < w->nout; i++)
			{
				const Critter &k = w->c[i];
				if (i >= lastOut || k.state != last[i].state || k.dir != last[i].dir)
					printf ("%d c%d %s %d,%d %s\n", w->step - 1, i, STATE[k.state], k.x, k.y, k.dir > 0 ? ">" : "<");
				last[i] = k;
			}
			lastOut = w->nout;
		}
	}
	if (at >= w->step && ppm) picture (*w, ppm);
	RunResult r; memset (&r, 0, sizeof r);
	r.result = w->result; r.saved = w->saved; r.needed = lv->save; r.endStep = w->endStep; r.refused = rp.refused;
	print_result (r);
	return 0;
}
