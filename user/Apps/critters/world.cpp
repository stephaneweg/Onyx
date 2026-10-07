//
// world.cpp -- Critters' world (world.h): one step of the rules, giving roles, the cursor's pick, the nuke, the end,
// the checksum, the clock. Every pixel rule is written next to its code (03 §3.3; the host test checks them pixel by
// pixel: AC-6 ... AC-19).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "world.h"
#include <string.h>

namespace critters {

int interval_for (int rate) { return 4 + (99 - rate) * 40 / 98; }

static bool dying (int s) { return s == S_SPLAT || s == S_DROWN || s == S_BURN || s == S_BURST; }

bool World::reset (const Level &l)
{
	lv = &l;
	memset (c, 0, sizeof c);
	nout = saved = dead = step = 0;
	rate = l.rate; nextRelease = FIRST_OUT; hatchTurn = 0;
	for (int r = 0; r < NROLES; r++) roles[r] = l.roles[r];
	nuking = false; nukeNext = 0;
	result = PLAYING; endStep = 0;
	nev = 0;
	return build_terrain (l, t);
}

void World::emit (int kind, int who, int role)
{
	if (nev >= MAXEV) return;
	Event &e = ev[nev++];
	e.kind = (uint8_t) kind; e.role = (int8_t) role; e.who = (int16_t) who;
	e.x = who >= 0 && who < nout ? c[who].x : 0;
	e.y = who >= 0 && who < nout ? c[who].y : 0;
}

void World::die (int i, int state, int kind)
{
	Critter &k = c[i];
	k.state = (uint8_t) state; k.timer = 0; k.fuse = -1;
	emit (kind, i);
}

int World::inPlay () const
{
	int n = 0;
	for (int i = 0; i < nout; i++) if (c[i].state != S_SAVED && c[i].state != S_DEAD) n++;
	return n;
}

bool World::alive (int i) const
{
	if (i < 0 || i >= nout) return false;
	int s = c[i].state;
	return s != S_SAVED && s != S_DEAD && !dying (s);
}

bool World::onlyBlockersLeft () const
{
	if (!lv || nuking || result != PLAYING || nout < lv->count) return false;
	int n = 0;
	for (int i = 0; i < nout; i++)
	{
		int s = c[i].state;
		if (s == S_SAVED || s == S_DEAD) continue;
		if (s != S_BLOCK || c[i].fuse >= 0) return false;
		n++;
	}
	return n > 0;
}

int World::timeLeft () const { return lv ? lv->timeSec * STEPS_PER_SEC - step : 0; }

// A blocker within reach, ahead (02 #12): |xb - x| <= 6, |yb - y| <= 10, xb - x of the sign of dir
bool World::blocked (int i) const
{
	const Critter &k = c[i];
	for (int b = 0; b < nout; b++)
	{
		if (b == i || c[b].state != S_BLOCK) continue;
		int dx = c[b].x - k.x, dy = c[b].y - k.y;
		if (dx * k.dir > 0 && dx * k.dir <= BLOCK_DX && dy >= -BLOCK_DY && dy <= BLOCK_DY) return true;
	}
	return false;
}

// Walking, 1 px a step: the next column; solid at the feet -> up to 6 px up to the first empty pixel, else a wall
// (a climber climbs it, except the map's side edges; the others turn); empty -> down at most 3 px, deeper: a fall.
void World::walk (int i)
{
	Critter &k = c[i];
	if (blocked (i)) { k.dir = (int8_t) -k.dir; return; }
	int nx = k.x + k.dir;
	if (t.solid (nx, k.y))
	{
		for (int up = 1; up <= WALK_UP; up++)
			if (!t.solid (nx, k.y - up)) { k.x = (int16_t) nx; k.y = (int16_t) (k.y - up); return; }
		if ((k.flags & F_CLIMBER) && nx >= 0 && nx < t.w) { k.state = S_CLIMB; k.timer = 0; }
		else k.dir = (int8_t) -k.dir;
		return;
	}
	for (int d = 0; d <= WALK_DOWN; d++)
		if (t.solid (nx, k.y + 1 + d)) { k.x = (int16_t) nx; k.y = (int16_t) (k.y + d); return; }
	k.x = (int16_t) nx; k.state = S_FALL; k.fallFrom = k.y;
}

// Falling, pixel by pixel (a 2-px brick is never passed through): 3 px a step (a floater: 1 px once 12 px fallen);
// landing after more than 60 px kills, unless a floater
void World::fall (int i)
{
	Critter &k = c[i];
	int n = (k.flags & F_FLOATER) && k.y - k.fallFrom >= FLOAT_FAST ? 1 : FALL_SPEED;
	for (int s = 0; s < n; s++)
	{
		if (t.solid (k.x, k.y + 1) || t.hazard (k.x, k.y + 1) || k.y >= t.h) break;
		k.y++;
	}
	if (!t.solid (k.x, k.y + 1)) return;
	if (k.y - k.fallFrom > FALL_KILL && !(k.flags & F_FLOATER)) { die (i, S_SPLAT, E_SPLAT); return; }
	k.state = S_WALK; k.timer = 0;
}

// Climbing, 1 px a step: a blocker within reach or something solid above the head (x, y-12) -> let go, turn round,
// fall; else up; the wall's top reached (the column ahead empty at the feet) -> over it, walking
void World::climb (int i)
{
	Critter &k = c[i];
	if (blocked (i) || t.solid (k.x, k.y - HEAD))
	{
		k.dir = (int8_t) -k.dir; k.state = S_FALL; k.fallFrom = k.y;
		return;
	}
	k.y--;
	if (!t.solid (k.x + k.dir, k.y)) { k.x = (int16_t) (k.x + k.dir); k.state = S_WALK; }
}

// Building, a brick every 8 steps: the column 3 px ahead solid from y-4 to y-9 -> turn and walk; else the brick
// (columns x-dir ... x+4dir, rows y-1 ... y), then 3 px forward and 2 px up; the last 3 warn; none left -> shrug
void World::build (int i)
{
	Critter &k = c[i];
	if (!t.solid (k.x, k.y + 1)) { k.state = S_FALL; k.fallFrom = k.y; return; }
	if (++k.timer < BRICK_EVERY) return;
	k.timer = 0;
	for (int r = k.y - BUILD_HEAD1; r <= k.y - BUILD_HEAD0; r++)
		if (t.solid (k.x + 3 * k.dir, r)) { k.dir = (int8_t) -k.dir; k.state = S_WALK; return; }
	if (k.bricks <= BRICK_WARN) emit (E_BRICK_WARN, i, k.bricks);
	t.brick (k.x, k.y, k.dir);
	k.x = (int16_t) (k.x + 3 * k.dir); k.y = (int16_t) (k.y - 2);
	if (--k.bricks <= 0) { k.bricks = 0; k.state = S_SHRUG; k.timer = 0; }
}

// Digging, a row every 2 steps: the row under the feet (x-4 ... x+4) -- a steel pixel in it -> walk on; no earth in it
// -> fall; else cleared, and 1 px down
void World::dig (int i)
{
	Critter &k = c[i];
	if (!t.solid (k.x, k.y + 1)) { k.state = S_FALL; k.fallFrom = k.y; return; }
	if (++k.timer < DIG_EVERY) return;
	k.timer = 0;
	int y = k.y + 1;
	if (t.any_steel (k.x - DIG_HALF, y, k.x + DIG_HALF, y)) { k.state = S_WALK; return; }
	if (!t.dig_rect (k.x - DIG_HALF, y, k.x + DIG_HALF, y)) { k.state = S_FALL; k.fallFrom = k.y; return; }
	k.y = (int16_t) y;
}

void World::tick ()
{
	if (!lv || result != PLAYING) return;
	const Level &l = *lv;
	nev = 0;
	if (step == 0) emit (E_HATCH_OPEN, -1);
	// the release (a rate change counts from the next one)
	if (!nuking && nout < l.count && step >= nextRelease)
	{
		const Hatch &h = l.hatch[hatchTurn];
		Critter &k = c[nout];
		memset (&k, 0, sizeof k);
		k.x = h.at.x; k.y = h.at.y; k.dir = h.dir; k.state = S_FALL; k.fallFrom = k.y; k.fuse = -1;
		nout++;
		emit (E_OUT, nout - 1);
		hatchTurn = (hatchTurn + 1) % l.nhatch;
		nextRelease = step + interval_for (rate);
	}
	// the nuke: one more fuse a step, in release order
	if (nuking)
	{
		while (nukeNext < nout && (!alive (nukeNext) || c[nukeNext].state == S_EXIT || c[nukeNext].fuse >= 0)) nukeNext++;
		if (nukeNext < nout) c[nukeNext++].fuse = FUSE;
	}
	for (int i = 0; i < nout; i++)
	{
		Critter &k = c[i];
		if (k.state == S_SAVED || k.state == S_DEAD) continue;
		k.frame++;
		if (dying (k.state))
		{
			if (++k.timer >= DIE_STEPS) { k.state = S_DEAD; dead++; }
			continue;
		}
		if (k.state == S_EXIT)
		{
			if (++k.timer >= EXIT_STEPS) { k.state = S_SAVED; saved++; emit (E_SAVED, i); }
			continue;
		}
		if (k.fuse >= 0)
		{
			if (k.fuse > 0 && k.fuse % FUSE_TICK == 0) emit (E_TICK, i, k.fuse / FUSE_TICK);
			if (--k.fuse <= 0)
			{
				t.dig_disc (k.x, k.y - BURST_UP, BURST_R);
				die (i, S_BURST, E_BURST);
				continue;
			}
		}
		switch (k.state)
		{
		case S_WALK: walk (i); break;
		case S_FALL: fall (i); break;
		case S_CLIMB: climb (i); break;
		case S_BUILD: build (i); break;
		case S_DIG: dig (i); break;
		case S_BLOCK:
			if (!t.solid (k.x, k.y + 1)) { k.state = S_FALL; k.fallFrom = k.y; }
			break;
		case S_SHRUG:
			if (!t.solid (k.x, k.y + 1)) { k.state = S_FALL; k.fallFrom = k.y; }
			else if (++k.timer >= SHRUG) { k.state = S_WALK; k.timer = 0; }
			break;
		default: break;
		}
		if (dying (k.state)) continue;
		// the void, the hazards, the exits
		if (k.y >= t.h) { k.state = S_DEAD; k.fuse = -1; dead++; emit (E_VOID, i); continue; }
		int a = t.at (k.x, k.y), b = t.at (k.x, k.y + 1);
		if (a == M_WATER || b == M_WATER) { die (i, S_DROWN, E_DROWN); continue; }
		if (a == M_LAVA || b == M_LAVA) { die (i, S_BURN, E_BURN); continue; }
		if (k.state == S_BLOCK) continue;
		for (int e = 0; e < l.nexit; e++)
		{
			int dx = k.x - l.exit[e].x, dy = k.y - l.exit[e].y;
			if (dx >= -EXIT_DX && dx <= EXIT_DX && dy >= -EXIT_DY && dy <= EXIT_DY)
			{
				k.state = S_EXIT; k.timer = 0; k.fuse = -1;
				break;
			}
		}
	}
	step++;
	// the end (02 #24)
	bool end = false;
	if ((nout == l.count || nuking) && inPlay () == 0) end = true;
	else if (step >= l.timeSec * STEPS_PER_SEC)
	{
		for (int i = 0; i < nout; i++)
			if (c[i].state != S_SAVED && c[i].state != S_DEAD) { c[i].state = S_DEAD; c[i].fuse = -1; dead++; }
		end = true;
	}
	if (end)
	{
		result = saved >= l.save ? WON : LOST;
		endStep = step;
		emit (E_END, -1);
	}
}

int World::can_take (int who, int role) const
{
	if (role < 0 || role >= NROLES || roles[role] <= 0) return NO_COUNT;
	if (who < 0 || who >= nout || result != PLAYING) return NOT_ALIVE;
	const Critter &k = c[who];
	if (k.state == S_SAVED || k.state == S_DEAD || dying (k.state)) return NOT_ALIVE;
	if (k.state == S_EXIT) return LEAVING;
	switch (role)
	{
	case R_CLIMBER: return k.flags & F_CLIMBER ? ALREADY : OK;
	case R_FLOATER: return k.flags & F_FLOATER ? ALREADY : OK;
	case R_EXPLODER: return k.fuse >= 0 ? COUNTING_DOWN : OK;
	default: break;
	}
	if (k.state == S_BLOCK) return IS_BLOCKER;
	bool ground = k.state == S_WALK || k.state == S_BUILD || k.state == S_DIG || k.state == S_BASH || k.state == S_MINE
		|| (role == R_BUILDER && k.state == S_SHRUG);
	if (!ground) return NOT_ON_GROUND;
	if ((role == R_DIGGER && k.state == S_DIG) || (role == R_BASHER && k.state == S_BASH) || (role == R_MINER && k.state == S_MINE))
		return ALREADY;
	return OK;
}

int World::assign (int who, int role)
{
	int r = can_take (who, role);
	if (r != OK) { emit (E_REFUSE, who, role); return r; }
	roles[role]--;
	Critter &k = c[who];
	switch (role)
	{
	case R_CLIMBER: k.flags |= F_CLIMBER; break;
	case R_FLOATER: k.flags |= F_FLOATER; break;
	case R_BLOCKER: k.state = S_BLOCK; k.timer = 0; break;
	case R_BUILDER:
		if (k.state != S_BUILD) k.timer = 0;
		k.state = S_BUILD;
		k.bricks = (int8_t) (k.bricks + BRICKS > 99 ? 99 : k.bricks + BRICKS);
		break;
	case R_DIGGER: k.state = S_DIG; k.timer = 0; break;
	case R_EXPLODER: k.fuse = FUSE; break;
	default: break;					// (the basher and the miner: not built -- their counts are always 0)
	}
	emit (E_ROLE, who, role);
	return OK;
}

int World::pick (int x, int y, int role) const
{
	int best = -1, bestd = 0;
	for (int i = 0; i < nout; i++)
	{
		const Critter &k = c[i];
		if (k.state == S_SAVED || k.state == S_DEAD) continue;
		if (x < k.x - BOX_DX || x > k.x + BOX_DX || y < k.y - BOX_UP || y > k.y) continue;
		if (can_take (i, role) == OK) return i;
		int dx = x - k.x, dy = y - (k.y - BOX_UP / 2);
		int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
		if (best < 0 || d < bestd) { best = i; bestd = d; }
	}
	return best;
}

void World::set_rate (int r)
{
	int lo = lv ? lv->rate : 1;
	rate = r < lo ? lo : r > 99 ? 99 : r;
}

void World::nuke ()
{
	if (nuking || result != PLAYING) return;
	nuking = true; nukeNext = 0;
}

static uint64_t mixin (uint64_t h, uint64_t v) { return mix64 (h ^ (v + 0x9E3779B97F4A7C15ull)); }

uint64_t World::checksum () const
{
	uint64_t h = t.hash;
	h = mixin (h, (uint64_t) (uint32_t) nout | (uint64_t) (uint32_t) saved << 16 | (uint64_t) (uint32_t) dead << 32);
	h = mixin (h, (uint64_t) (uint32_t) step | (uint64_t) (uint32_t) rate << 32);
	h = mixin (h, (uint64_t) (uint32_t) nextRelease | (uint64_t) (uint32_t) hatchTurn << 32);
	h = mixin (h, (uint64_t) nuking | (uint64_t) (uint32_t) nukeNext << 8 | (uint64_t) (uint32_t) result << 40);
	for (int r = 0; r < NROLES; r++) h = mixin (h, (uint64_t) (uint32_t) roles[r]);
	for (int i = 0; i < nout; i++)
	{
		const Critter &k = c[i];
		h = mixin (h, (uint64_t) (uint16_t) k.x | (uint64_t) (uint16_t) k.y << 16 | (uint64_t) (uint8_t) k.dir << 32
			   | (uint64_t) k.state << 40 | (uint64_t) k.flags << 48);
		h = mixin (h, (uint64_t) (uint16_t) k.fallFrom | (uint64_t) (uint16_t) k.timer << 16 | (uint64_t) (uint8_t) k.bricks << 32
			   | (uint64_t) (uint16_t) k.fuse << 40);
		h = mixin (h, k.frame);
	}
	return h;
}

int Clock::due (unsigned dt)
{
	if (paused) { acc3 = 0; return 0; }
	if (dt > 1000) dt = 1000;
	acc3 += (int) dt * 3;
	int cost = fast ? 50 : 150, cap = fast ? 12 : 4;
	int n = acc3 / cost;
	if (n > cap) { n = cap; acc3 = 0; }
	else acc3 -= n * cost;
	return n;
}

}
