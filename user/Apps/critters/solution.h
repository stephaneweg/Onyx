//
// solution.h -- Critters' solutions and replays: the ".sol" text (02 §7.3: "<step> <action> [<arg>]" a line), the
// replay driver applying the actions due before each step, the recorder writing the player's actions in the same form,
// and a level run headless to its end with a solution (the host test, crsim, the window's --replay ... --until).
// No user interface, no file, integers only.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _critters_solution_h
#define _critters_solution_h

#include "world.h"

namespace critters {

enum ActKind { A_ROLE, A_RATE, A_NUKE, A_PAUSE, A_FAST };
enum { MAXACT = 1024 };
struct Action { int step; uint8_t kind; int8_t role; int16_t arg /* the creature, or the rate */; int line; };
struct Solution { Action a[MAXACT]; int n; };

// The text read into s -> true; false: e says why ("line 3: bad step"). A "#" ends a line; blank lines are skipped;
// steps ascend (equal steps allowed: two roles in one step).
bool load_solution (const char *text, Solution &s, LoadError &e);
// TR: bad step
// TR: bad action
// TR: bad creature
// TR: too many actions (max 1024)

// Applied before each tick: every action with step == w.step (A_PAUSE / A_FAST are a watcher's: ignored here); an
// assignment the creature refuses is counted.
struct Replay
{
	const Solution *s; int next; int refused;
	Replay () : s (0), next (0), refused (0) {}
	void start (const Solution *sol) { s = sol; next = 0; refused = 0; }
	void apply_due (World &w);
};

// The player's actions, recorded by creature number (a replay is exact); text () writes them as a .sol
struct Recorder
{
	Solution s;
	Recorder () { s.n = 0; }
	void clear () { s.n = 0; }
	void add (int step, int kind, int role, int arg);
	int  text (char *out, int cap) const;		// -> the length written (cut at cap - 1)
};

struct RunResult { int result, saved, needed, endStep, refused; uint64_t lastChecksum; bool hashOk; };
// The level run headless to its end with the solution (0: nothing done); perStep (may be 0) gets the checksum after
// every tick; w (may be 0) is the world used (left at the end, for a look). hashOk: the terrain's incremental hash
// equals its full recount at the end.
RunResult run_solution (const Level &lv, const Solution *s, void (*perStep) (int step, uint64_t sum, void *u), void *u,
			World *w = 0);

}
#endif
