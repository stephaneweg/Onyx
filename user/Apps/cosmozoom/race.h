//
// race.h -- a race: the ships on the track (their physics in the track's coordinates: s along, l across -- round
// a tube --, h above), the computer's pilots, the item capsules and the gadgets, the star bits, the laps and the
// places. No drawing, no sound: what happens is told as events (the game makes the sounds, the sparkles and the
// messages). Plain C++, the same on the Pi and the PC (a race with the same seed and the same inputs runs the
// same: the tests). docs/cosmozoom/DESIGN.md sections 4.5 and 6.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef CZ_RACE_H
#define CZ_RACE_H

#include "track.h"
#include "parts.h"

namespace cz {

enum { MAXCRAFT = 8, MAXPROJ = 32, MAXHAZ = 32, MAXBOX = 80, MAXBIT = 48, MAXEV = 96 };

struct Input
{
	float steer;			// -1 left .. 1 right
	float throttle;			// 0..1
	bool brake, drift, use;		// held
	bool back;			// held: a gadget is thrown backward
};

// What happened (for the sounds, the sparkles, the messages)
enum
{
	EV_NONE, EV_COUNT, EV_GO, EV_BOOSTPAD, EV_BOX, EV_ITEM_GOT, EV_USE, EV_HIT, EV_BLOCKED, EV_WALL, EV_BUMP,
	EV_LAP, EV_FINALLAP, EV_FINISH, EV_RAMP, EV_LAND, EV_MINITURBO, EV_POP, EV_PLACE_UP, EV_STARBIT, EV_DRIFTLVL,
	EV_START_BOOST, EV_EMP, EV_NEBULA, EV_WARP, EV_BEAM, EV_WELL
};
struct Event { int kind, craft, arg; V3 at; };

// what a hit does: a bubble, a spin, a slow (goo), the nebula's drizzle
enum { HIT_BUBBLE, HIT_SPIN, HIT_GOO, HIT_DRIZZLE };

struct Craft
{
	// who
	int pilot; Build build; Stats st;
	bool human; int player;			// player 0..3 (human)
	// on the track
	float s, l, h, vh;			// along, across, above (metres)
	float v;				// speed (m/s)
	float ya;				// heading relative to the track (rad, > 0: to the right)
	float yawRate;				// (seen: the lean)
	int lap; float dist;			// laps begun (0 before the start line); the race's distance
	float lastS;
	bool finished; float finishTime; int place;
	float lapStart, bestLap, lapTimes[8];
	bool wrongWay; float wrongT;
	// the drift
	int driftDir, driftLevel; float driftCharge;
	// what is on it
	float boostT, boostPower, stunT, bubbleT, slowT, shieldT, warpT, drizzleT, beamT, empT, invulT, wallT, airT;
	int item, itemCount; float rollT;	// the gadget held (IT_NONE), its charges; the roulette's time left
	int bits;				// star bits (0..10)
	float energy;				// the shield's energy 0..100 (the hits take some; it comes back; at 0 the ship sputters)
	// the computer's
	float skill, laneBias, wobblePh, useDelay;
	// seen
	float bob, squash, spin, roll;
	bool useHeld;
	float startCharge;			// (the throttle held at the right time: a boost at GO)
};

struct Proj { bool on; int kind, owner, target; float s, l, h, v, life; };
struct Hazard { bool on; int kind, owner; float s, l, life, r, spin; };
struct Box { float s, l, respawn; };
struct Bit { float s, l, respawn; };

struct Race
{
	const Track *T;
	Craft c[MAXCRAFT]; int n;
	Proj proj[MAXPROJ];
	Hazard haz[MAXHAZ];
	Box box[MAXBOX]; int nbox;
	Bit bit[MAXBIT]; int nbit;
	Event ev[MAXEV]; int nev;
	int laps;
	int cls;				// the speed class (CLASSES)
	bool items;				// gadgets on
	float time;				// since GO (< 0: the countdown)
	int count;				// the countdown's number (3 2 1, 0 GO)
	bool over;				// every human finished (and a little time passed)
	float overT;
	Rng rng;
	int nfinished;
	float assist;				// steering help for the humans (0..1)
	bool autoThrottle;			// the humans accelerate by themselves (the youngest)
	int nebulaOn; float nebulaS, nebulaL; int nebulaTarget; float nebulaT;	// the grumpy cloud on its way

	void emit (int kind, int craft, const V3 &at, int arg = 0) { if (nev < MAXEV) { Event &e = ev[nev++]; e.kind = kind; e.craft = craft; e.at = at; e.arg = arg; } }
	V3 posOf (int i) const { return T->pointAt (c[i].s, c[i].l, c[i].h + 0.7f); }
	float classTop () const { return CLASSES[cls].top; }

	// ---- the start ----------------------------------------------------------------------------------------
	void start (const Track *track, int ncraft, int nlaps, int speedClass, unsigned seed, bool withItems = true)
	{
		T = track; n = ncraft > MAXCRAFT ? MAXCRAFT : ncraft; laps = nlaps; cls = clampi (speedClass, 0, NCLASS - 1);
		items = withItems; rng = Rng (seed);
		time = -4.0f; count = 4; over = false; overT = 0; nev = 0; nfinished = 0; assist = 0.5f; autoThrottle = false;
		memset (proj, 0, sizeof proj); memset (haz, 0, sizeof haz); nebulaOn = 0;
		for (int i = 0; i < n; i++)
		{
			Craft &C = c[i];
			C.st = statsOf (C.build);
			int row = i / 2, col = i & 1;
			C.s = T->wrapS (-8.0f - (float) row * 8.0f);
			float w = T->widthAt (C.s); if (w > 16) w = 16;
			C.l = (col ? 1 : -1) * w * 0.22f + (row & 1 ? 0.8f : -0.8f);
			C.h = 0; C.vh = 0; C.v = 0; C.ya = 0; C.yawRate = 0;
			C.lap = 0; C.dist = C.s - T->length; C.lastS = C.s;
			C.finished = false; C.finishTime = 0; C.place = i;
			C.lapStart = 0; C.bestLap = 0; for (int k = 0; k < 8; k++) C.lapTimes[k] = 0;
			C.wrongWay = false; C.wrongT = 0;
			C.driftDir = 0; C.driftLevel = 0; C.driftCharge = 0;
			C.boostT = C.boostPower = C.stunT = C.bubbleT = C.slowT = C.shieldT = C.warpT = C.drizzleT = C.beamT = C.empT = C.invulT = C.wallT = C.airT = 0;
			C.item = IT_NONE; C.itemCount = 0; C.rollT = 0; C.bits = 0; C.energy = 100;
			if (C.skill <= 0) C.skill = CLASSES[cls].aiSkill;
			C.wobblePh = rng.range (0, 6.28f); C.useDelay = rng.range (1, 4);
			C.laneBias = rng.range (-0.25f, 0.25f);
			C.bob = rng.range (0, 6.28f); C.squash = 0; C.spin = 0; C.roll = 0; C.useHeld = false; C.startCharge = 0;
		}
		// the item capsules: a row of 4 or 5 across the road at each "items"
		nbox = 0;
		for (int i = 0; i < T->n && items; i++)
			if (T->seg[i].flags & F_ITEMS)
			{
				int k5 = T->seg[i].w > 20 ? 6 : T->seg[i].w > 12 ? 5 : 4;
				for (int k = 0; k < k5 && nbox < MAXBOX; k++)
				{
					Box &B = box[nbox++];
					B.s = (float) i * SEG + SEG * 0.5f;
					float span = fminf (T->seg[i].w * 0.5f - 2.0f, 9.0f);
					B.l = (-1.0f + 2.0f * (float) k / (float) (k5 - 1)) * span;
					B.respawn = 0;
				}
			}
		// the star bits: ten along the lap, weaving across
		nbit = 0;
		for (int k = 0; k < 10 && nbit < MAXBIT; k++)
		{
			float s = T->length * (0.06f + 0.09f * (float) k);
			for (int j = 0; j < 3 && nbit < MAXBIT; j++)
			{
				Bit &B = bit[nbit++];
				B.s = T->wrapS (s + (float) j * 3.0f);
				B.l = sinf ((float) k * 1.7f) * T->widthAt (s) * 0.28f;
				B.respawn = 0;
			}
		}
	}

	// ---- one step (dt seconds; in[i]: craft i's input, the computer's are made here) ------------------------
	void step (float dt, const Input *in)
	{
		nev = 0;
		float before = time;
		time += dt;
		if (time < 0)
		{
			int k = (int) ceilf (-time);
			if (k != count && k <= 3) { count = k; emit (EV_COUNT, -1, V3 (), k); }
			// the throttle held when "1" shows: a boost at GO (held too early: none)
			for (int i = 0; i < n; i++)
			{
				c[i].bob += dt * 3;
				if (!c[i].human || !in) continue;
				if (in[i].throttle > 0.5f || autoThrottle) c[i].startCharge += dt; else c[i].startCharge = 0;
			}
			return;
		}
		if (before < 0)
		{
			count = 0; emit (EV_GO, -1, V3 ());
			for (int i = 0; i < n; i++)
			{
				Craft &C = c[i];
				bool good = C.human ? (C.startCharge > 0.05f && C.startCharge < 1.1f) : rng.f () < 0.35f;
				if (good) { C.boostT = 1.1f; C.boostPower = 1.3f; emit (EV_START_BOOST, i, posOf (i)); }
				C.lapStart = 0;
			}
		}
		for (int i = 0; i < n; i++)
		{
			Input I = in ? in[i] : Input ();
			if (!c[i].human || c[i].finished || c[i].warpT > 0) I = ai (i, dt, c[i].human ? &I : 0);
			if (c[i].human && autoThrottle && !c[i].finished) I.throttle = I.brake ? 0 : 1;
			drive (i, I, dt);
		}
		collide ();
		capsules (dt);
		starBits (dt);
		projectiles (dt);
		hazards (dt);
		nebula (dt);
		places ();
		bool allHumans = true, anyHuman = false;
		for (int i = 0; i < n; i++) if (c[i].human) { anyHuman = true; if (!c[i].finished) allHumans = false; }
		if (!anyHuman) { allHumans = true; for (int i = 0; i < n; i++) if (!c[i].finished) allHumans = false; }
		if (allHumans) { overT += dt; if (overT > 3.5f) over = true; }
		// everybody home after the humans: the others' places from where they are
		if (over) for (int i = 0; i < n; i++) if (!c[i].finished) { c[i].finished = true; c[i].finishTime = time + 1000.0f + (float) c[i].place; }
	}

	// the speed it aims at, all things considered
	float topSpeed (const Craft &C) const
	{
		float top = classTop () * C.st.topMul * (1.0f + 0.01f * (float) C.bits);
		if (C.slowT > 0) top *= 0.5f;
		if (C.drizzleT > 0) top *= 0.75f;
		if (C.warpT > 0) top *= 1.6f;
		else if (C.boostT > 0) top *= C.boostPower;
		if (C.beamT > 0) top *= 1.2f;
		return top;
	}
	float rubber (int i) const
	{
		// the computer's pilots: a little faster behind the leading human, a little slower ahead of them
		const Craft &C = c[i];
		if (C.human) return 1.0f;
		float best = -1e9f; bool any = false;
		for (int k = 0; k < n; k++) if (c[k].human && !c[k].finished) { any = true; if (c[k].dist > best) best = c[k].dist; }
		float f = C.skill, r = CLASSES[cls].rubber;
		if (any) f *= 1.0f + clampf ((best - C.dist) / 400.0f, -r, r);
		return f;
	}

	void drive (int i, const Input &I, float dt)
	{
		Craft &C = c[i];
		C.bob += dt * 8.2f;					// (1.3 Hz)
		C.squash = approach (C.squash, 0, dt * 3);
		float *timers[] = { &C.boostT, &C.stunT, &C.bubbleT, &C.slowT, &C.shieldT, &C.warpT, &C.drizzleT, &C.beamT, &C.empT, &C.invulT, &C.wallT };
		bool hadBubble = C.bubbleT > 0;
		for (float *t : timers) if (*t > 0) { *t -= dt; if (*t < 0) *t = 0; }
		if (hadBubble && C.bubbleT <= 0) emit (EV_POP, i, posOf (i));
		C.energy = fminf (100, C.energy + dt * (C.boostT > 0 ? 2.0f : 5.0f));
		if (C.rollT > 0) { C.rollT -= dt; if (C.rollT <= 0) { C.rollT = 0; emit (EV_ITEM_GOT, i, posOf (i), C.item); } }
		bool stunned = C.stunT > 0 || C.bubbleT > 0;
		const Seg &S = T->seg[T->segAt (C.s)];
		bool closed = T->closedAt (C.s);
		// ---- speed
		float top = topSpeed (C) * rubber (i);
		float thr = stunned ? 0 : clampf (I.throttle, 0, 1);
		if (C.finished && C.human) thr = 0.6f;
		float target = top * thr;
		if (I.brake && !stunned) target = 0;
		float acc = classTop () * C.st.topMul / C.st.toTop * 1.6f;	// (the curve below takes ~ toTop to the top)
		if (C.v < target) C.v = approach (C.v, target, acc * dt * (C.boostT > 0 || C.warpT > 0 ? 3.0f : 1.0f - 0.6f * C.v / (top + 1)));
		else C.v = approach (C.v, target, (I.brake ? 2.5f : C.v - target > 8 ? 1.8f : 0.7f) * acc * dt);
		if (C.bubbleT > 0) C.v = approach (C.v, top * 0.4f, acc * 3 * dt);
		if (C.stunT > 0) C.v *= 1.0f - 1.5f * dt;
		// ---- steering
		float steer = stunned ? 0 : clampf (I.steer, -1, 1);
		float curv = S.curv;
		// the drift: held while turning -- tighter, charging a mini-boost (blue, orange, pink)
		if (I.drift && !stunned && C.h < 0.6f && C.v > 10)
		{
			if (!C.driftDir && fabsf (steer) > 0.3f) { C.driftDir = steer > 0 ? 1 : -1; C.driftCharge = 0; C.driftLevel = 0; C.vh = 2.2f; }
		}
		if (C.driftDir && (!I.drift || stunned || C.v < 8))
		{
			if (C.driftLevel > 0 && !stunned)
			{
				static const float DUR[4] = { 0, 0.5f, 1.0f, 1.6f };
				C.boostT = fmaxf (C.boostT, DUR[C.driftLevel] * (C.st.longBoost ? 1.1f : 1.0f)); C.boostPower = 1.25f;
				emit (EV_MINITURBO, i, posOf (i), C.driftLevel);
			}
			C.driftDir = 0; C.driftCharge = 0; C.driftLevel = 0;
		}
		float turn = C.st.turn;
		float yr;
		if (C.driftDir)
		{
			// the drift turns its way: harder with the stick into it, wider against it
			float k = 0.55f + 0.72f * (steer * (float) C.driftDir + 1) * 0.5f;
			yr = (float) C.driftDir * turn * k;
			C.driftCharge += dt * (0.6f + 0.6f * fabsf (steer)) * (C.st.fastCharge ? 1.15f : 1.0f);
			int lvl = C.driftCharge > 2.6f ? 3 : C.driftCharge > 1.6f ? 2 : C.driftCharge > 0.8f ? 1 : 0;
			if (lvl != C.driftLevel) { C.driftLevel = lvl; emit (EV_DRIFTLVL, i, posOf (i), lvl); }
		}
		else yr = steer * turn * (C.v > 3 ? 1.0f : C.v / 3);
		if (C.human) yr += assist * curv * C.v * 0.7f;	// the help: the track's turn followed in part
		yr -= C.ya * (C.human ? 1.1f : 1.6f);		// it re-aligns with the road by itself a little (a toy)
		if (C.bubbleT > 0) yr = 0;
		C.yawRate = yr;
		// ---- moving
		float k = closed ? 1.0f : 1.0f - curv * C.l; if (k < 0.3f) k = 0.3f;
		float ds = C.v * cosf (C.ya) * dt / k;
		C.ya += yr * dt - curv * ds;
		C.ya = clampf (C.ya, -1.2f, 1.2f);
		C.l += C.v * sinf (C.ya) * dt;
		if (C.beamT > 0)
		{
			int t = nextAhead (i);
			if (t >= 0) C.l += clampf (T->across (C.s, C.l, c[t].l), -4.0f * dt, 4.0f * dt);
		}
		C.lastS = C.s;
		C.s = T->wrapS (C.s + ds);
		C.dist += ds;
		if (C.stunT > 0) C.spin += dt * 14; else C.spin = approach (C.spin, roundf (C.spin / (2 * PI)) * 2 * PI, dt * 6);
		// the wrong way (more than 100° off the track's way, moving)
		if (cosf (C.ya) < -0.2f && C.v > 3) { C.wrongT += dt; if (C.wrongT > 1.0f) C.wrongWay = true; } else { C.wrongT = 0; C.wrongWay = false; }
		// ---- the edges (a whole tube has none: across goes round it)
		float half = T->widthAt (C.s) * 0.5f - 0.9f;
		if (closed || T->closedAt (C.s)) C.l = T->wrapL (C.s, C.l);
		else if (fabsf (C.l) > half)
		{
			float sgn = C.l > 0 ? 1.0f : -1.0f;
			C.l = sgn * half;
			if (C.ya * sgn > 0) C.ya = -C.ya * 0.4f;
			if (C.wallT <= 0 && C.v > 6) { emit (EV_WALL, i, posOf (i)); C.wallT = 0.4f; C.v *= 0.85f; C.squash = 0.5f; drain (i, 4); }
			if (C.driftDir) C.driftCharge *= 0.6f;
		}
		// ---- up and down: the hover, the kickers, the gaps
		const Seg &S2 = T->seg[T->segAt (C.s)];
		if ((S2.flags & F_RAMP) && C.h < 0.5f && C.v > 8 && C.airT <= 0)
		{
			C.vh = (5.0f + C.v * 0.16f) * (C.st.longJump ? 1.15f : 1.0f); C.airT = 0.4f;
			emit (EV_RAMP, i, posOf (i));
		}
		if (C.airT > 0) C.airT -= dt;
		if ((S2.flags & F_BOOST) && C.h < 1.0f)
		{
			float lane = S2.lane * S2.w * 0.5f;
			if (fabsf (C.l - lane) < 2.6f && C.boostT < 0.9f)
			{
				C.boostT = 1.2f; C.boostPower = 1.4f; C.energy = fminf (100, C.energy + 8);
				emit (EV_BOOSTPAD, i, posOf (i));
			}
		}
		float floorH = C.bubbleT > 0 ? 1.6f : 0.0f;
		if (C.h > floorH || C.vh > 0)
		{
			C.vh -= 24.0f * dt;
			C.h += C.vh * dt;
			if (C.h < floorH)
			{
				if (C.vh < -6) { C.squash = clampf (-C.vh * 0.07f, 0.2f, 0.8f); emit (EV_LAND, i, posOf (i)); }
				C.h = floorH; C.vh = 0;
			}
		}
		else C.h = approach (C.h, floorH, dt * 3);
		// ---- the laps
		if (C.lastS > T->length * 0.75f && C.s < T->length * 0.25f) newLap (i);
		else if (C.s > T->length * 0.75f && C.lastS < T->length * 0.25f && C.lap > 0) C.lap--;	// (backwards over the line)
		// ---- the gadget
		bool usePress = I.use && !C.useHeld;
		C.useHeld = I.use;
		if (usePress && C.item != IT_NONE && C.rollT <= 0 && !stunned && time > 0 && C.empT <= 0) useItem (i, I.back);
	}

	void newLap (int i)
	{
		Craft &C = c[i];
		C.lap++;
		if (C.lap >= 2)
		{
			float lt = time - C.lapStart;
			if (C.lap - 2 < 8) C.lapTimes[C.lap - 2] = lt;
			if (C.bestLap <= 0 || lt < C.bestLap) C.bestLap = lt;
		}
		C.lapStart = time;
		if (C.lap > laps && !C.finished)
		{
			C.finished = true; C.finishTime = time; nfinished++;
			emit (EV_FINISH, i, posOf (i), nfinished);
		}
		else if (C.lap == laps && laps > 1) emit (EV_FINALLAP, i, posOf (i));
		else if (C.lap > 1) emit (EV_LAP, i, posOf (i), C.lap);
	}

	int nextAhead (int i, float within = 1e9f) const
	{
		int best = -1; float bd = within;
		for (int k = 0; k < n; k++)
		{
			if (k == i) continue;
			float d = c[k].dist - c[i].dist;
			if (d > 0 && d < bd) { bd = d; best = k; }
		}
		return best;
	}
	int leader () const { for (int k = 0; k < n; k++) if (c[k].place == 0) return k; return 0; }

	// ---- the ships against each other ---------------------------------------------------------------------------
	void collide ()
	{
		for (int a = 0; a < n; a++)
			for (int b = a + 1; b < n; b++)
			{
				Craft &A = c[a], &B = c[b];
				float ds = T->ahead (B.s, A.s), dl = T->across (A.s, A.l, B.l);
				if (fabsf (ds) > 3.0f || fabsf (dl) > 2.2f || fabsf (A.h - B.h) > 1.5f) continue;
				// the warp star and the shield push the others aside
				bool aBig = A.warpT > 0, bBig = B.warpT > 0;
				if (aBig && !bBig && B.invulT <= 0) { hit (b, a, HIT_SPIN); continue; }
				if (bBig && !aBig && A.invulT <= 0) { hit (a, b, HIT_SPIN); continue; }
				float push = (2.2f - fabsf (dl)) * 0.5f;
				float wa = A.st.weight * (A.shieldT > 0 ? 2 : 1), wb = B.st.weight * (B.shieldT > 0 ? 2 : 1);
				float sgn = dl >= 0 ? 1.0f : -1.0f;
				A.l -= sgn * push * wb / (wa + wb) * 1.6f;
				B.l += sgn * push * wa / (wa + wb) * 1.6f;
				if (fabsf (ds) < 2.4f)
				{
					// the one behind loses a little speed (the lighter more)
					if (ds > 0) A.v *= 1.0f - 0.03f * wb / wa; else B.v *= 1.0f - 0.03f * wa / wb;
				}
				if (A.wallT <= 0 && B.wallT <= 0) { emit (EV_BUMP, a, (posOf (a) + posOf (b)) * 0.5f); A.wallT = B.wallT = 0.3f; A.squash = B.squash = 0.3f; }
			}
	}

	// ---- hits ------------------------------------------------------------------------------------------------------
	bool hit (int i, int by, int kind)
	{
		Craft &C = c[i];
		if (C.warpT > 0 || C.invulT > 0) return false;
		if (C.shieldT > 0) { C.shieldT = 0; C.invulT = 0.6f; emit (EV_BLOCKED, i, posOf (i), by); return false; }
		switch (kind)
		{
		case HIT_BUBBLE: C.bubbleT = 1.2f; C.vh = 3; break;
		case HIT_SPIN: C.stunT = 0.9f; break;
		case HIT_GOO: C.slowT = 1.0f; C.stunT = 0.4f; break;
		case HIT_DRIZZLE: C.drizzleT = 3.0f; break;
		}
		drain (i, kind == HIT_BUBBLE ? 22 : kind == HIT_GOO ? 14 : kind == HIT_SPIN ? 18 : 8);
		if (kind != HIT_DRIZZLE)
		{
			C.invulT = 1.8f; C.driftDir = 0; C.boostT = 0;
			int drop = C.bits < 3 ? C.bits : 3; C.bits -= drop;
		}
		emit (EV_HIT, i, posOf (i), kind);
		return true;
	}

	// the shield's energy spent; at nothing, the ship sputters for a moment and gets half back
	void drain (int i, float e)
	{
		Craft &C = c[i];
		if (C.warpT > 0) return;
		C.energy -= e;
		if (C.energy <= 0) { C.energy = 50; C.stunT = fmaxf (C.stunT, 1.0f); C.v *= 0.6f; emit (EV_HIT, i, posOf (i), HIT_SPIN); }
	}

	// ---- the item capsules, the star bits ------------------------------------------------------------------------
	void capsules (float dt)
	{
		for (int b = 0; b < nbox; b++)
		{
			Box &B = box[b];
			if (B.respawn > 0) { B.respawn -= dt; continue; }
			for (int i = 0; i < n; i++)
			{
				Craft &C = c[i];
				if (fabsf (T->ahead (C.s, B.s)) < 1.9f && fabsf (T->across (C.s, C.l, B.l)) < 1.9f && C.h < 2)
				{
					B.respawn = 2.0f;
					emit (EV_BOX, i, T->pointAt (B.s, B.l, 1.3f));
					if (C.item == IT_NONE && C.rollT <= 0)
					{
						C.item = rollItem (i); C.itemCount = ITEMS[C.item].count;
						C.rollT = C.human ? 1.5f : 0.5f;
						C.useDelay = rng.range (0.6f, 3.5f);
					}
					break;
				}
			}
		}
	}
	void starBits (float dt)
	{
		for (int b = 0; b < nbit; b++)
		{
			Bit &B = bit[b];
			if (B.respawn > 0) { B.respawn -= dt; continue; }
			for (int i = 0; i < n; i++)
			{
				Craft &C = c[i];
				if (fabsf (T->ahead (C.s, B.s)) < 1.6f && fabsf (T->across (C.s, C.l, B.l)) < 1.6f && C.h < 2)
				{
					B.respawn = 10.0f;
					if (C.bits < 10) C.bits++;
					emit (EV_STARBIT, i, T->pointAt (B.s, B.l, 1.0f), C.bits);
					break;
				}
			}
		}
	}
	int rollItem (int i)
	{
		// by place (DESIGN.md 6.2): the leaders get small things, the last ones the big helps
		int p = c[i].place, nn = n;
		float f = nn > 1 ? (float) p * 7.0f / (float) (nn - 1) : 0;		// (on the 8-racer scale)
		int col = f < 0.5f ? 0 : f < 2.5f ? 1 : f < 4.5f ? 2 : f < 6.5f ? 3 : 4;
		int tot = 0; for (int k = 1; k < IT_COUNT; k++) tot += ITEMS[k].odds[col];
		if (tot <= 0) return IT_BOOST;
		int r = rng.n (tot);
		for (int k = 1; k < IT_COUNT; k++) { if (r < ITEMS[k].odds[col]) return k; r -= ITEMS[k].odds[col]; }
		return IT_BOOST;
	}

	// ---- using the gadget -----------------------------------------------------------------------------------------
	Proj *newProj () { for (int k = 0; k < MAXPROJ; k++) if (!proj[k].on) { memset (&proj[k], 0, sizeof proj[k]); proj[k].on = true; return &proj[k]; } return 0; }
	Hazard *newHaz () { for (int k = 0; k < MAXHAZ; k++) if (!haz[k].on) { memset (&haz[k], 0, sizeof haz[k]); haz[k].on = true; return &haz[k]; } return 0; }

	void useItem (int i, bool back)
	{
		Craft &C = c[i];
		int it = C.item;
		emit (EV_USE, i, posOf (i), it);
		switch (ITEMS[it].effect)
		{
		case FX_BOOST: C.boostT = (it == IT_TRIPLE ? 1.2f : 1.5f) * (C.st.longBoost ? 1.1f : 1.0f); C.boostPower = 1.5f; emit (EV_MINITURBO, i, posOf (i), 3); break;
		case FX_SHIELD: C.shieldT = 8; break;
		case FX_SHOT:
		{
			Proj *P = newProj (); if (!P) break;
			P->kind = it; P->owner = i; P->l = C.l; P->h = 0.9f; P->life = 4;
			if (back) { P->s = T->wrapS (C.s - 3); P->v = fmaxf (C.v - 30, 8) ; }
			else { P->s = T->wrapS (C.s + 3); P->v = fmaxf (C.v + 20, 60); }
			P->target = -1;
			break;
		}
		case FX_DROP:
		{
			Hazard *H = newHaz (); if (!H) break;
			H->kind = it; H->owner = i; H->s = T->wrapS (C.s - 3.5f); H->l = C.l; H->life = 20; H->r = 1.5f;
			break;
		}
		case FX_WELL:
		{
			Hazard *H = newHaz (); if (!H) break;
			H->kind = it; H->owner = i; H->s = T->wrapS (back ? C.s - 4 : C.s + 20); H->l = C.l; H->life = 6; H->r = 6;
			emit (EV_WELL, i, T->pointAt (H->s, H->l, 0.5f));
			break;
		}
		case FX_BEAM:
		{
			int t = nextAhead (i, 80);
			if (t >= 0) { C.beamT = 3; emit (EV_BEAM, i, posOf (i), t); }
			else { C.boostT = 1.0f; C.boostPower = 1.3f; }	// (nobody ahead: a little boost all the same)
			break;
		}
		case FX_EMP:
			for (int k = 0; k < n; k++)
			{
				float d = C.dist - c[k].dist;
				if (k != i && d > 0 && d < 40 && c[k].shieldT <= 0 && c[k].warpT <= 0) { c[k].empT = 2.5f; emit (EV_EMP, k, posOf (k), i); }
			}
			break;
		case FX_NEBULA:
		{
			int t = leader ();
			if (t == i) { C.boostT = 1.0f; C.boostPower = 1.3f; break; }
			nebulaOn = 1; nebulaTarget = t; nebulaS = C.s; nebulaL = C.l; nebulaT = 0;
			emit (EV_NEBULA, i, posOf (i), t);
			break;
		}
		case FX_WARP: C.warpT = 5; C.stunT = C.bubbleT = C.slowT = C.drizzleT = 0; emit (EV_WARP, i, posOf (i)); break;
		}
		if (--C.itemCount <= 0) { C.item = IT_NONE; C.itemCount = 0; }
	}

	void projectiles (float dt)
	{
		for (int k = 0; k < MAXPROJ; k++)
		{
			Proj &P = proj[k];
			if (!P.on) continue;
			P.life -= dt;
			if (P.life <= 0) { P.on = false; emit (EV_POP, -1, T->pointAt (P.s, P.l, P.h)); continue; }
			P.s = T->wrapS (P.s + P.v * dt);
			float half = T->widthAt (P.s) * 0.5f - 0.6f;
			P.l = T->wrapL (P.s, P.l);
			if (!T->closedAt (P.s) && fabsf (P.l) > half) { P.l = P.l > 0 ? half : -half; }	// (it slides along the rail)
			for (int i = 0; i < n; i++)
			{
				if (i == P.owner && P.life > 3.7f) continue;
				const Craft &C = c[i];
				if (fabsf (T->ahead (C.s, P.s)) < 2.0f && fabsf (T->across (C.s, C.l, P.l)) < 1.8f && C.h < 2.5f)
				{
					P.on = false;
					hit (i, P.owner, HIT_BUBBLE);
					emit (EV_POP, i, T->pointAt (P.s, P.l, P.h));
					break;
				}
			}
		}
	}
	void hazards (float dt)
	{
		for (int k = 0; k < MAXHAZ; k++)
		{
			Hazard &H = haz[k];
			if (!H.on) continue;
			H.life -= dt; H.spin += dt;
			if (H.life <= 0) { H.on = false; emit (EV_POP, -1, T->pointAt (H.s, H.l, 0.5f)); continue; }
			bool well = ITEMS[H.kind].effect == FX_WELL;
			for (int i = 0; i < n; i++)
			{
				Craft &C = c[i];
				if (C.h > 1.2f || C.warpT > 0) continue;
				float ds = T->ahead (C.s, H.s), dl = T->across (C.s, C.l, H.l);
				if (well)
				{
					// pulled toward its centre and slowed (the heavy ones less)
					float d = sqrtf (ds * ds + dl * dl);
					if (d < H.r && C.shieldT <= 0)
					{
						float pull = 8.0f * (1.5f - C.st.weight / 3.0f) * dt;
						C.l += clampf (dl, -pull, pull);
						C.v *= 1.0f - 0.5f * dt * (1.5f - C.st.weight / 3.0f);
					}
					continue;
				}
				if (fabsf (ds) < H.r && fabsf (dl) < H.r + 0.6f)
				{
					if (i == H.owner && H.life > 19.0f) continue;
					H.on = false;
					hit (i, H.owner, HIT_GOO);
					break;
				}
			}
		}
	}
	// the grumpy nebula: flies along the track to the leader, then rains on them
	void nebula (float dt)
	{
		if (!nebulaOn) return;
		Craft &L = c[nebulaTarget];
		float d = T->ahead (L.s, nebulaS);
		if (nebulaOn == 1)
		{
			nebulaS = T->wrapS (nebulaS + fminf (90 * dt, fmaxf (d, 0) + 1));
			nebulaL += clampf (T->across (nebulaS, nebulaL, L.l), -10 * dt, 10 * dt);
			if (fabsf (d) < 2) { nebulaOn = 2; nebulaT = 3; hit (nebulaTarget, -1, HIT_DRIZZLE); }
		}
		else
		{
			nebulaS = L.s; nebulaL = L.l;
			nebulaT -= dt;
			if (nebulaT <= 0 || L.drizzleT <= 0) nebulaOn = 0;
		}
	}

	// ---- the places ---------------------------------------------------------------------------------------------
	void places ()
	{
		int ord[MAXCRAFT]; for (int i = 0; i < n; i++) ord[i] = i;
		for (int a = 0; a < n; a++)
			for (int b = a + 1; b < n; b++)
			{
				const Craft &A = c[ord[a]], &B = c[ord[b]];
				bool swap;
				if (A.finished && B.finished) swap = B.finishTime < A.finishTime;
				else if (A.finished != B.finished) swap = B.finished;
				else swap = B.dist > A.dist;
				if (swap) { int t = ord[a]; ord[a] = ord[b]; ord[b] = t; }
			}
		for (int k = 0; k < n; k++)
		{
			Craft &C = c[ord[k]];
			if (k < C.place && C.human && time > 1 && !C.finished) emit (EV_PLACE_UP, ord[k], posOf (ord[k]), k);
			C.place = k;
		}
	}

	// ---- the computer's pilots (and the warp star's autopilot: help != 0) -------------------------------------------
	Input ai (int i, float dt, const Input *help)
	{
		Craft &C = c[i];
		Input I; memset (&I, 0, sizeof I);
		I.throttle = 1;
		bool closed = T->closedAt (C.s);
		// the line: inside of the turns ahead, toward a speed pad, a capsule or star bits, away from the others
		float look = 12 + C.v * 0.6f, curvAhead = 0;
		for (float d = 6; d <= look + 20; d += 4) curvAhead += T->seg[T->segAt (C.s + d)].curv;
		float half = fminf (T->widthAt (C.s + look), T->widthAt (C.s)) * 0.5f - 2.6f;
		if (half > 9) half = 9;					// (a tube: stay near the top)
		if (half < 1) half = 1;
		float target = clampf (curvAhead * 18.0f, -1, 1) * half * 0.6f + C.laneBias * half * 0.6f;
		C.wobblePh += dt * 0.7f;
		target += sinf (C.wobblePh) * half * 0.1f;
		for (float d = 8; d < 40; d += 2)
		{
			const Seg &S = T->seg[T->segAt (C.s + d)];
			if (S.flags & F_BOOST) { target = S.lane * S.w * 0.5f; break; }
		}
		if (C.item == IT_NONE)
			for (int b = 0; b < nbox; b++)
			{
				float d = T->ahead (box[b].s, C.s);
				if (d > 6 && d < 35 && box[b].respawn <= 0 && fabsf (T->across (C.s, C.l, box[b].l)) < 6) { target = box[b].l; break; }
			}
		for (int k = 0; k < MAXHAZ; k++)
			if (haz[k].on && haz[k].owner != i)
			{
				float d = T->ahead (haz[k].s, C.s);
				float r = ITEMS[haz[k].kind].effect == FX_WELL ? 7.0f : 2.5f;
				if (d > 0 && d < 25 && fabsf (haz[k].l - target) < r) target += (haz[k].l > C.l ? -1.4f : 1.4f) * r * (C.skill > 0.95f ? 1.0f : 0.6f);
			}
		for (int k = 0; k < n; k++)
		{
			if (k == i) continue;
			float d = T->ahead (c[k].s, C.s);
			if (d > 1 && d < 10 && fabsf (T->across (C.s, C.l, c[k].l)) < 2.4f) target += c[k].l > C.l ? -2.5f : 2.5f;
		}
		if (!closed) target = clampf (target, -half, half);
		float wantYa = atan2f (T->across (C.s, C.l, target), look);
		const Seg &S = T->seg[T->segAt (C.s)];
		float ff = S.curv * C.v + C.ya * 1.6f;		// what holds the line
		float steer = (ff + (wantYa - C.ya) * 3.2f) / (C.st.turn + 0.01f);
		I.steer = clampf (steer, -1, 1);
		// too fast for the turn ahead: lift off (the computer is careful, more so in the easy class)
		{
			float maxCurv = 0;
			for (float d = 0; d <= 40; d += 4) maxCurv = fmaxf (maxCurv, fabsf (T->seg[T->segAt (C.s + d)].curv));
			float vmax = (C.st.turn * 0.85f) / (maxCurv + 1e-4f);
			if (C.v > vmax) I.throttle = 0.3f;
			if (C.v > vmax * 1.25f) I.brake = true;
		}
		if (help) return I;					// (the warp star drives: no drift, no gadget)
		// the computer drifts in the long turns
		if (fabsf (curvAhead) > 0.1f && C.v > 18 && C.skill > 0.95f && fabsf (C.l) < half * 0.5f) I.drift = true;
		// the gadget
		if (C.item != IT_NONE && C.rollT <= 0 && C.empT <= 0)
		{
			C.useDelay -= dt;
			if (C.useDelay <= 0)
			{
				int fx = ITEMS[C.item].effect;
				bool use = false;
				if (fx == FX_SHOT)
				{
					int t = nextAhead (i, 60);
					if (t >= 0 && fabsf (T->across (C.s, C.l, c[t].l)) < 2.5f) use = true;
					for (int k = 0; k < n && !use; k++) { float d = C.dist - c[k].dist; if (k != i && d > 2 && d < 15 && fabsf (T->across (C.s, C.l, c[k].l)) < 2) { use = true; I.back = true; } }
				}
				else if (fx == FX_DROP || fx == FX_WELL)
				{
					for (int k = 0; k < n; k++) { float d = C.dist - c[k].dist; if (k != i && d > 2 && d < 30) { use = true; I.back = fx == FX_WELL; } }
					if (fx == FX_WELL && nextAhead (i, 30) >= 0) { use = true; I.back = false; }
					if (C.useDelay < -6) use = true;
				}
				else if (fx == FX_SHIELD) use = C.useDelay < -2;
				else if (fx == FX_BEAM) use = nextAhead (i, 80) >= 0;
				else if (fx == FX_BOOST) use = fabsf (curvAhead) < 0.06f;
				else use = true;
				if (cls == 0 && C.useDelay > -3 && fx != FX_BOOST && fx != FX_SHIELD) use = false;	// (Orbit: gentle)
				if (use) { I.use = true; C.useDelay = rng.range (1, 3); }
			}
		}
		if (C.finished) { I.throttle = 0.7f; I.drift = false; I.use = false; }
		return I;
	}
};

} // namespace cz

#endif
