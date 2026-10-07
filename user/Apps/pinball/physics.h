//
// physics.h -- Pinball's world: the balls, the flippers, the plunger and the table's toys moving by fixed frames of
// 1/60 s, each cut into sub-steps short enough that a ball never crosses a wall (at least 8; a ball moves at most half
// its radius in one). Collisions with segments, arcs, circles, the flippers (swept: a fast flipper never passes through
// a ball), one-way gates, the other balls; sensors: lanes, ramp entries, saucers, the plunger, the drain.
//
// Deterministic: the same table, seed and inputs give the same run, bit for bit (doubles, + - * / and sqrt only, the
// core's own sine; built with -ffp-contract=off on the Pi and the PC). The world says what happened as events (a
// bumper hit, a target down, a ball drained...); the game (rules.h) decides the points.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _pinball_physics_h
#define _pinball_physics_h

#include "table.h"

namespace pinball {

enum {
	MAXBALL = 3,
	MAXEV = 64,				// events a frame (more are dropped)
	FPS = 60,				// frames a second
	MINSUB = 8,				// sub-steps a frame, at least
	FLASH_FRAMES = 9,			// a bumper's flash (0.15 s)
	SLING_FRAMES = 6,			// a sling's body lit (0.1 s)
	RAMP_FRAMES = 30,			// a ramp's arrow lit (0.5 s)
	BANK_FRAMES = 60,			// a cleared bank rises again (1 s)
	SEARCH_FRAMES = 240			// a ball still for 4 s (not on a flipper): the ball search kicks it
};
static const double MAXSPEED = 4000;		// units/s, the ball's speed cap
static const double SLING_MIN = 200;		// a sling kicks a ball hitting it at least this fast
static const double SAUCER_MAX = 1500;		// a saucer catches a ball slower than this
static const double NUDGE_SPEED = 150;		// a nudge's push
static const double SEARCH_SPEED = 300;		// the ball search's kick

enum BallState { B_OFF, B_PLAY, B_PLUNGER, B_RAMP, B_SAUCER };
struct Ball
{
	Vec p, v;
	int state;				// BallState
	int ramp, saucer;			// the ramp it travels / the saucer holding it
	int timer;				// frames left on the ramp / in the saucer
	int ignoreSaucer;			// the saucer that just ejected it (-1): not caught again until it is out
	unsigned lanes;				// the lanes its centre is in (bit per lane)
	Vec still; int stillFrames;		// the ball search: where it was, for how long
	bool live () const { return state != B_OFF; }
};

// One frame's controls: the flippers' keys held, the plunger's key held, a tap (a plunger key event shorter than a frame)
struct Input { bool left, right, plunger, tap; };

enum PEvKind {
	P_BUMPER,				// index: the circle
	P_SLING,				// index: the sling
	P_TARGET,				// a standup hit; index: the target
	P_DROP,					// a drop target down; index: the target
	P_BANK,					// a bank cleared; index: the bank
	P_LANE,					// a lane crossed; index: the lane
	P_LANES,				// a lane group all lit (then dark); index: the group
	P_RAMP,					// a ball entered a ramp; index: the ramp
	P_RAMP_OUT,				// it came back; index: the ramp
	P_SAUCER,				// a ball caught; index: the saucer
	P_EJECT,				// a ball ejected; index: the saucer
	P_FLIPPER_UP,				// a flipper key pressed; index: the side (0 left, 1 right)
	P_LAUNCH,				// a ball launched; speed: its speed
	P_DRAIN,				// a ball lost
	P_PLUNGER,				// a ball fell back onto the plunger
	P_BALLHIT,				// two balls hit
	P_SEARCH,				// the ball search kicked a still ball
	P_COUNT_
};
struct PEvent { int kind; int index; int ball; double speed; };

struct World
{
	const Table *t;
	Ball ball[MAXBALL];
	double flipAng[MAXFLIP];		// radians, world
	double flipW[MAXFLIP];			// rad/s this frame (0 at an end stop)
	bool flipperDead;			// tilt: the flippers fall and stay down
	bool held[2];				// the flipper keys of the last frame (the press edges)
	double pull; int pullFrames;		// the plunger: 0..1, frames held
	bool tgtDown[MAXTGT], tgtLit[MAXTGT];	// drop targets down; standups lit
	int bankTimer[MAXBANK];			// frames until a cleared bank rises (0: up)
	bool laneLit[MAXLANE];
	int bumperFlash[MAXCIRC], slingFlash[MAXSLING], rampFlash[MAXRAMP];	// frames left
	PEvent ev[MAXEV]; int nev;		// this frame's events
	unsigned rng;				// xorshift32
	long frame;				// frames stepped
	long subSteps;				// sub-steps stepped (statistics)
	int searches;				// ball searches done
	// a test's hook, called after every sub-step (0 in the game)
	void (*onSubStep) (const World &w, void *data);
	void *hookData;

	void reset (const Table &t, unsigned seed);	// no ball, flippers at rest, every lamp off, no hook
	int addBall (Vec p, Vec v);		// a ball in play -> its index, -1 if MAXBALL are live
	int addOnPlunger ();			// a ball resting on the plunger -> its index, -1 (full)
	bool plungerBusy () const;		// a ball rests on the plunger
	bool launch (double speed);		// the ball on the plunger launched upwards -> false: none there
	void step (const Input &in);		// one frame (1/60 s)
	void rotateLanes (int dir);		// the rotate group's lit lamps shifted one place (-1 left, +1 right), wrapping
	void nudge ();				// every ball in play pushed by NUDGE_SPEED, up and towards a random side
	int liveBalls () const;			// balls not lost (in play, on a ramp, in a saucer, on the plunger)
	int playBalls () const;			// balls moving on the table (B_PLAY)
	unsigned random ();			// the world's generator
	// geometry the window draws with
	Vec flipTip (int f) const;		// the tip's centre now
	Vec rampPos (int b) const;		// where a ball on a ramp is drawn (along the path)
	bool onFlipper (int b) const;		// a ball touching a flipper
private:
	void emit (int kind, int index, int ball, double speed = 0);
	void collide (int b, Vec p0, const double *a0, const double *a1, double h);
	void sensors (int b, Vec p0);
	void ballBall ();
	void timers ();
};

}

#endif
