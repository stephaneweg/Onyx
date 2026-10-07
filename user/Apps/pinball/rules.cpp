//
// rules.cpp -- Pinball's game (rules.h): the world's events into points, bonus, lamps and the table's rules; the
// balls of a game, the ball save, the extra ball, multiball, the tilt.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "rules.h"

namespace pinball {

void Game::emit (int kind, int index, long value)
{
	if (nev >= MAXGEV) return;
	GEvent &e = ev[nev++];
	e.kind = kind; e.index = index; e.value = value;
}

void Game::start (const Table &tt, unsigned seed)
{
	t = &tt;
	w.reset (tt, seed);
	state = G_READY;
	score = bonus = lastBonus = 0;
	mult = 1; ball = 1; extraBalls = 0;
	multiballQueue = queueTimer = 0;
	frameNo = 0; ballSaveUntil = 0; saveArmed = true;
	nudgeAt[0] = nudgeAt[1] = nudgeAt[2] = -1;
	tilted = false; endTimer = 0;
	for (int i = 0; i < MAXRULE; i++) { ruleCount[i] = 0; ruleDone[i] = false; }
	nev = 0;
	w.addOnPlunger ();
}

bool Game::ballSaveActive () const { return state == G_PLAY && frameNo < ballSaveUntil; }
int Game::nudges () const
{
	int n = 0;
	for (int i = 0; i < 3; i++) if (nudgeAt[i] >= 0 && frameNo - nudgeAt[i] < TILT_FRAMES) n++;
	return n;
}

// Points scored by a toy: none after a tilt; a tenth of them (rounded down to 10) to the ball's bonus
void Game::points (long s)
{
	if (tilted) return;
	score += s;
	bonus += s / 10 / 10 * 10;
}

void Game::fire (const Action &a, int rule)
{
	switch (a.kind)
	{
	case A_SCORE: score += a.n; break;
	case A_BONUS: bonus += a.n; break;
	case A_MULT: if (mult < MAXMULT) mult++; break;
	case A_MULTIBALL:
	{
		int active = w.liveBalls () + multiballQueue;
		if (active >= 2 || a.n <= active) break;		// not again while it runs
		multiballQueue = (int) a.n - active;
		if (multiballQueue > MAXBALL - active) multiballQueue = MAXBALL - active;
		queueTimer = 1;
		emit (GE_MULTIBALL, rule);
		break;
	}
	case A_EXTRABALL: extraBalls++; emit (GE_EXTRABALL, rule); break;
	case A_BALLSAVE: ballSaveUntil = frameNo + a.n * FPS; saveArmed = false; break;
	}
}

// An event of the table counted by the rules that wait for it; a rule at its count fires its actions and its message
void Game::count (int when, int ref)
{
	if (tilted) return;
	for (int r = 0; r < t->nrule; r++)
	{
		const Rule &R = t->rule[r];
		if (R.when != when || R.ref != ref || ruleDone[r]) continue;
		if (++ruleCount[r] < R.count) continue;
		ruleCount[r] = 0;
		if (R.once) ruleDone[r] = true;
		for (int k = 0; k < R.nact; k++) fire (R.act[k], r);
		if (R.message.en[0]) emit (GE_MESSAGE, r);
	}
}

void Game::handle (const PEvent &e)
{
	const Table &T = *t;
	switch (e.kind)
	{
	case P_BUMPER: points (T.circ[e.index].score); count (EV_HIT, H_BUMPER << 16 | e.index); break;
	case P_SLING: points (T.sling[e.index].score); count (EV_HIT, H_SLING << 16 | e.index); break;
	case P_TARGET:
	case P_DROP: points (T.tgt[e.index].score); count (EV_HIT, H_TARGET << 16 | e.index); break;
	case P_BANK: count (EV_BANK, e.index); break;
	case P_LANE: points (T.lane[e.index].score); count (EV_HIT, H_LANE << 16 | e.index); break;
	case P_LANES: count (EV_LANES, e.index); break;
	case P_RAMP: points (T.ramp[e.index].score); count (EV_RAMP, e.index); break;
	case P_SAUCER: points (T.saucer[e.index].score); count (EV_SAUCER, e.index); break;
	case P_FLIPPER_UP: if (!tilted) w.rotateLanes (e.index == SIDE_LEFT ? -1 : 1); break;
	case P_LAUNCH:
		if (state == G_READY)
		{
			state = G_PLAY;
			if (saveArmed) { ballSaveUntil = frameNo + (long) (T.ballsave * FPS + 0.5); saveArmed = false; }
		}
		break;
	}
	switch (e.kind)					// the sounds
	{
	case P_BUMPER: case P_SLING: case P_TARGET: case P_DROP: case P_LANE: case P_RAMP: case P_SAUCER: case P_EJECT:
	case P_FLIPPER_UP: case P_LAUNCH: case P_DRAIN: case P_BANK: case P_LANES:
		emit (GE_SOUND, e.kind);
		break;
	}
}

// The last ball in play lost: saved, or the ball's end (its bonus)
void Game::drained ()
{
	if (frameNo < ballSaveUntil)
	{
		ballSaveUntil = 0;
		w.addOnPlunger ();
		state = G_READY;
		emit (GE_BALLSAVED, -1);
		return;
	}
	lastBonus = tilted ? 0 : bonus * mult;
	score += lastBonus;
	state = G_BALLEND;
	endTimer = BALLEND_FRAMES;
	emit (GE_BALLEND, -1, lastBonus);
}

void Game::nextBall ()
{
	bool again = extraBalls > 0;
	if (again) extraBalls--;
	else if (++ball > t->balls)
	{
		ball = t->balls;
		state = G_OVER;
		emit (GE_GAMEOVER, -1, score);
		return;
	}
	bonus = 0; mult = 1; tilted = false;
	w.flipperDead = false;
	nudgeAt[0] = nudgeAt[1] = nudgeAt[2] = -1;
	ballSaveUntil = 0; saveArmed = true;
	w.addOnPlunger ();
	state = G_READY;
	emit (GE_NEWBALL, -1, again ? 1 : 0);
}

void Game::skip ()
{
	if (state == G_BALLEND) nextBall ();
}

void Game::frame (const Input &in, bool nudge)
{
	nev = 0;
	if (state == G_OVER) return;
	frameNo++;
	if (nudge && !tilted && (state == G_PLAY || state == G_READY))
	{
		w.nudge ();
		nudgeAt[0] = nudgeAt[1]; nudgeAt[1] = nudgeAt[2]; nudgeAt[2] = frameNo;
		int n = nudges ();
		if (n >= 3) { tilted = true; w.flipperDead = true; emit (GE_TILT, -1); }
		else if (n == 2) emit (GE_TILTWARN, -1);
	}
	w.step (in);
	for (int i = 0; i < w.nev; i++) handle (w.ev[i]);
	if (multiballQueue > 0 && state == G_PLAY && --queueTimer <= 0 && !w.plungerBusy ())
	{
		if (w.addOnPlunger () >= 0 && w.launch (t->plungerAuto))
		{
			emit (GE_SOUND, P_LAUNCH);
			multiballQueue--;
		}
		else multiballQueue = 0;
		queueTimer = MULTIBALL_FRAMES;
	}
	if (state == G_PLAY && w.liveBalls () == 0 && multiballQueue == 0) drained ();
	else if (state == G_BALLEND && --endTimer <= 0) nextBall ();
}

}
