//
// rules.h -- Pinball's game: the balls of a game, the score and the bonus, the multiplier, the ball save, the extra
// ball, multiball, the tilt, and the table's [rule] blocks (an event counted; at its count, actions and a message).
// One call a frame: Game::frame turns the inputs into a World step, the world's events into points and rules, and
// says what the window should show or play as GEvents. No user interface here.
//
// A game: ball 1 on the plunger (G_READY) -> launched (G_PLAY) -> the last ball lost: saved (back to G_READY) or the
// ball's end (G_BALLEND: the bonus x the multiplier added, 1.5 s or skip ()) -> the next ball, or G_OVER.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _pinball_rules_h
#define _pinball_rules_h

#include "physics.h"

namespace pinball {

enum GState { G_READY, G_PLAY, G_BALLEND, G_OVER };
enum {
	MAXMULT = 5,				// the bonus multiplier's top
	BALLEND_FRAMES = 90,			// the bonus count (1.5 s)
	MULTIBALL_FRAMES = 30,			// between two balls put in play by a multiball (0.5 s)
	TILT_FRAMES = 300,			// the nudges counted (5 s): the 2nd a warning, the 3rd the tilt
	MAXGEV = 64
};
enum GEvKind {
	GE_SOUND,				// index: the PEvKind to play
	GE_MESSAGE,				// index: the rule whose message to show
	GE_MULTIBALL,				// multiball started; index: the rule (-1: none, the window says "MULTIBALL!")
	GE_EXTRABALL,				// an extra ball won; index: the rule (-1: none)
	GE_BALLSAVED,				// the ball lost came back on the plunger
	GE_TILTWARN,				// the 2nd nudge in 5 s
	GE_TILT,				// the 3rd: flippers dead, no score, no bonus until the drain
	GE_BALLEND,				// the ball is over; value: the bonus added (bonus x multiplier, 0 after a tilt)
	GE_NEWBALL,				// the next ball is on the plunger; value: 1 = the same ball again (an extra ball)
	GE_GAMEOVER				// value: the final score
};
struct GEvent { int kind; int index; long value; };

struct Game
{
	World w;
	const Table *t;
	int state;				// GState
	long score;
	long bonus;				// the ball's bonus so far
	long lastBonus;				// the bonus added at the last ball's end
	int mult;				// 1..MAXMULT
	int ball;				// 1-based
	int extraBalls;				// extra balls pending ("SHOOT AGAIN" lit)
	int multiballQueue, queueTimer;		// balls still to put in play, frames until the next
	long frameNo;				// frames of this game
	long ballSaveUntil;			// the ball save runs while frameNo < this
	bool saveArmed;				// the ball's first launch starts the ball save
	long nudgeAt[3];			// the frames of the last three nudges (-1: none)
	bool tilted;
	int endTimer;				// G_BALLEND's frames left
	int ruleCount[MAXRULE];			// the rules' counters (the whole game)
	bool ruleDone[MAXRULE];			// a "once" rule fired
	GEvent ev[MAXGEV]; int nev;		// this frame's

	void start (const Table &t, unsigned seed);	// a new game: ball 1 on the plunger
	void frame (const Input &in, bool nudge);	// one frame (nudge: a nudge pressed this frame)
	void skip ();					// the bonus count skipped: the next ball now
	void fire (const Action &a, int rule = -1);	// an action (the rules'; public for the tests and --start)
	void handle (const PEvent &e);			// one world event: points, lamps, rules (public for the tests)
	bool ballSaveActive () const;			// the ball save runs now ("SHOOT AGAIN" blinks)
	int nudges () const;				// the nudges counted in the last 5 s (0..3: the panel's tilt dots)
	int ruleGoal (int r) const { return t->rule[r].count; }
private:
	void emit (int kind, int index, long value = 0);
	void points (long s);
	void count (int when, int ref);
	void drained ();
	void nextBall ();
};

}

#endif
