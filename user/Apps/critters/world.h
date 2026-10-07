//
// world.h -- Critters' world: the creatures, their release, their state machine and the six roles, giving a role, the
// cursor's pick, the nuke, the end and the result, the events for the window (sounds, pictures), the per-step checksum,
// and the clock that tells the window how many steps to run. One step = 1/20 s; integers only, no random number, no
// clock read: the same level and the same assignments give the same run, bit for bit (02 #2).
//
// The order of a step (World::tick) is part of the rules (03 §3.3): the release, the nuke's next fuse, then each
// creature in release order (its fuse, then its state, then the hazards, the void and the exits), then the end.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _critters_world_h
#define _critters_world_h

#include <stdint.h>
#include "level.h"

namespace critters {

// The rules' numbers (02 §4.2-4.3): a change is made here and in the tests together, and the recorded solutions
// re-checked (tools/critters/crsim --search, AC-30).
enum {
	STEPS_PER_SEC = 20,
	FIRST_OUT = 40,				// the first creature drops 40 steps after the start
	WALK_UP = 6, WALK_DOWN = 3,		// a walker steps up 6 px, down 3 px
	FALL_SPEED = 3, FALL_KILL = 60,		// 3 px a step; a fall of more than 60 px kills
	FLOAT_FAST = 12,			// a floater: 3 px a step for the first 12 px, then 1 px
	BLOCK_DX = 6, BLOCK_DY = 10,		// a blocker turns the walkers and climbers within 6 px across, 10 px up / down
	BRICK_EVERY = 8, BRICKS = 12, BRICK_WARN = 3, SHRUG = 10,
	DIG_EVERY = 2, DIG_HALF = 4,		// a digger: a row of 9 px (x-4 ... x+4) every 2 steps
	FUSE = 100, FUSE_TICK = 20, BURST_R = 12, BURST_UP = 4,	// the burst: a disc of radius 12 centred on (x, y-4)
	EXIT_DX = 2, EXIT_DY = 4, EXIT_STEPS = 8,
	DIE_STEPS = 16,				// a dying creature's animation
	HEAD = 12,				// a climber lets go when (x, y-12) is solid
	BUILD_HEAD0 = 4, BUILD_HEAD1 = 9,	// a builder stops when the column 3 px ahead is solid from y-4 to y-9
	BOX_DX = 4, BOX_UP = 11,		// a creature's body box for the cursor: x-4 ... x+4, y-11 ... y
	MAXEV = 256
};

enum State : uint8_t { S_FALL, S_WALK, S_CLIMB, S_BLOCK, S_BUILD, S_SHRUG, S_DIG, S_BASH, S_MINE,
		       S_EXIT /* entering, 8 steps */, S_SPLAT, S_DROWN, S_BURN, S_BURST /* dying: 16 steps */,
		       S_SAVED, S_DEAD };
enum { F_CLIMBER = 1, F_FLOATER = 2 };

struct Critter
{
	int16_t x, y;				// the feet point (02 #4): the empty pixel it stands in
	int8_t  dir;				// -1 left, +1 right
	uint8_t state, flags;			// State; F_CLIMBER | F_FLOATER
	int16_t fallFrom;			// y where the fall started
	int16_t timer;				// the state's step counter (build, dig, shrug, exit, dying)
	int8_t  bricks;				// a builder's bricks left
	int16_t fuse;				// the exploder's countdown in steps, -1 none
	uint16_t frame;				// animation (drawing only; part of the checksum all the same)
};

enum EvKind { E_HATCH_OPEN, E_OUT, E_ROLE, E_REFUSE, E_BRICK_WARN, E_SPLAT, E_DROWN, E_BURN, E_TICK, E_BURST,
	      E_SAVED, E_END, E_VOID };
struct Event { uint8_t kind; int8_t role; int16_t who, x, y; };	// E_TICK: role = the digit shown (5 ... 1)
enum Refusal { OK = 0, NO_COUNT, NOT_ALIVE, LEAVING, ALREADY, NOT_ON_GROUND, IS_BLOCKER, COUNTING_DOWN };
enum Result { PLAYING, WON, LOST };

struct World
{
	const Level *lv;
	Terrain t;
	Critter c[MAXCRIT];
	int nout;				// released so far (c[0 ... nout-1])
	int saved, dead;
	int step;				// steps run
	int rate, nextRelease, hatchTurn, roles[NROLES];
	bool nuking; int nukeNext;
	int result, endStep;
	Event ev[MAXEV]; int nev;		// this step's events (cleared at the start of tick), and assign's

	World () : lv (0), nout (0), saved (0), dead (0), step (0), rate (50), nextRelease (FIRST_OUT), hatchTurn (0),
		   nuking (false), nukeNext (0), result (PLAYING), endStep (0), nev (0) {}
	bool reset (const Level &lv);		// the terrain built, the counts set, nobody out; false: no memory
	void tick ();				// ONE step (1/20 s)
	int  assign (int who, int role);	// -> Refusal; OK takes one from the count (E_ROLE / E_REFUSE)
	int  can_take (int who, int role) const;	// -> Refusal, without acting (02 #16)
	int  pick (int x, int y, int role) const;	// 02 #21's cursor rule -> creature index, -1 none
	void set_rate (int r);			// clamped to [the level's rate, 99]
	void nuke ();				// all explode (02 #17)
	int  inPlay () const;			// out and not saved / dead (the dying ones count until their end)
	bool alive (int i) const;		// out, not saved / dead / dying
	bool onlyBlockersLeft () const;		// every creature is out (or the nuke runs) and those in play are all blockers
	int  timeLeft () const;			// steps before the time limit
	uint64_t checksum () const;		// the terrain's hash mixed with every field of every creature and the counters
private:
	void emit (int kind, int who, int role = 0);
	void walk (int i);
	void fall (int i);
	void climb (int i);
	void build (int i);
	void dig (int i);
	void die (int i, int state, int kind);
	bool blocked (int i) const;
};

int interval_for (int rate);			// 4 + (99 - rate) * 40 / 98

// How many steps the window runs now: dt in ms since the last call. A step costs 50 ms (fast: 50 / 3 ms); at most 4
// steps a call (fast: 12); the time owed beyond that is dropped (the game slows, it never skips a step -- 02 #2).
// Paused: 0 steps, nothing owed. Pause and fast change WHEN steps run, never what a step does (AC-21).
struct Clock
{
	int acc3;				// the time owed, in thirds of a millisecond
	bool fast, paused;
	Clock () : acc3 (0), fast (false), paused (false) {}
	int due (unsigned dt);
};

}
#endif
