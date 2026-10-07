//
// physics.cpp -- Pinball's world (physics.h): one frame = the flippers and the plunger, then sub-steps (gravity, the
// move, the ball-ball contacts, the collisions in a fixed order, the sensors), then the timers. A collision pushes the
// ball out along the contact's normal and reflects its velocity; the side a ball is pushed back to is the side it came
// from (its position at the sub-step's start), so a ball never ends on the far side of a wall.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "physics.h"
#include <math.h>				// (sqrt only: exact on every machine)

namespace pinball {

// ---- vectors ----------------------------------------------------------------------------------------------------------
static inline Vec V (double x, double y) { Vec v = { x, y }; return v; }
static inline Vec operator+ (Vec a, Vec b) { return V (a.x + b.x, a.y + b.y); }
static inline Vec operator- (Vec a, Vec b) { return V (a.x - b.x, a.y - b.y); }
static inline Vec operator- (Vec a) { return V (-a.x, -a.y); }
static inline Vec operator* (Vec a, double k) { return V (a.x * k, a.y * k); }
static inline double dot (Vec a, Vec b) { return a.x * b.x + a.y * b.y; }
static inline double cross (Vec a, Vec b) { return a.x * b.y - a.y * b.x; }
static inline double len2 (Vec a) { return dot (a, a); }
static inline double dmax (double a, double b) { return a > b ? a : b; }
static inline double dabs (double a) { return a < 0 ? -a : a; }

// The ball's speed kept under MAXSPEED
static inline void cap (Vec &v)
{
	double s2 = len2 (v);
	if (s2 > MAXSPEED * MAXSPEED) v = v * (MAXSPEED / sqrt (s2));
}
// A contact's answer: the velocity reflected along n with the restitution e (none for a slow contact: no jitter at
// rest), the tangential speed cut by the friction (on an approaching contact only)
static void respond (Vec &v, Vec n, double e, double friction)
{
	double vn = dot (v, n);
	if (vn >= 0) return;
	if (vn > -30) e = 0;
	Vec vt = v - n * vn;
	if (vn < -5) vt = vt * (1 - friction);
	v = vt - n * (e * vn);
}
// A ball (centre p, radius r; p0 at the sub-step's start) against the segment a-b with round ends: pushed out -> true
// and the normal n (pointing to the ball)
static bool seg_contact (Vec a, Vec b, Vec p0, Vec &p, double r, Vec &n)
{
	Vec d = b - a;
	double L2 = len2 (d);
	double t = dot (p - a, d) / L2;
	if (t < 0) t = 0; else if (t > 1) t = 1;
	Vec q = a + d * t, dv = p - q;
	double dist2 = len2 (dv);
	if (dist2 >= r * r) return false;
	if (t > 0 && t < 1)
	{
		double L = sqrt (L2);
		Vec nn = V (-d.y / L, d.x / L);
		double s0 = dot (p0 - a, nn);
		if (s0 < 0 || (s0 == 0 && dot (p - a, nn) < 0)) nn = -nn;
		p = p + nn * (r - dot (p - a, nn));
		n = nn;
		return true;
	}
	double dist = sqrt (dist2);
	if (dist == 0)
	{
		double L = sqrt (L2);
		n = V (-d.y / L, d.x / L);
		if (dot (p0 - a, n) < 0) n = -n;
	}
	else n = dv * (1 / dist);
	p = q + n * r;
	return true;
}
// Is the direction u (a unit vector from the arc's centre) within the arc?
static bool arc_has (const Arc &a, Vec u)
{
	if (a.span >= 360) return true;
	if (a.span <= 180) return cross (a.fu, u) >= 0 && cross (u, a.tu) >= 0;
	return !(cross (a.tu, u) > 0 && cross (u, a.fu) > 0);
}
// The unit normal of the segment a-b on the side of `pass` (a gate's or a ramp entry's crossing direction): a slanted
// gate is crossed through its own line
static Vec cross_normal (Vec a, Vec b, Vec pass)
{
	Vec d = b - a;
	double L = sqrt (len2 (d));
	Vec n = V (-d.y / L, d.x / L);
	return dot (n, pass) < 0 ? -n : n;
}
static unsigned lanes_at (const Table &t, Vec p)
{
	unsigned m = 0;
	for (int i = 0; i < t.nlane; i++)
	{
		const Lane &l = t.lane[i];
		if (p.x >= l.x && p.x <= l.x + l.w && p.y >= l.y && p.y <= l.y + l.h) m |= 1u << i;
	}
	return m;
}
static int frames_of (double seconds) { return (int) (seconds * FPS + 0.5); }

// ---- the world ----------------------------------------------------------------------------------------------------------
unsigned World::random ()
{
	unsigned x = rng;
	x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	return rng = x;
}
void World::emit (int kind, int index, int b, double speed)
{
	if (nev >= MAXEV) return;
	PEvent &e = ev[nev++];
	e.kind = kind; e.index = index; e.ball = b; e.speed = speed;
}
void World::reset (const Table &tt, unsigned seed)
{
	memset (this, 0, sizeof *this);			// (the test's hook too: set it after reset)
	t = &tt;
	for (int f = 0; f < tt.nflip; f++) flipAng[f] = tt.flip[f].rest;
	for (int b = 0; b < MAXBALL; b++) ball[b].state = B_OFF;
	rng = seed ? seed : 0x9E3779B9u;
}
int World::addBall (Vec p, Vec v)
{
	for (int b = 0; b < MAXBALL; b++)
		if (ball[b].state == B_OFF)
		{
			Ball &B = ball[b];
			memset (&B, 0, sizeof B);
			B.state = B_PLAY; B.p = p; B.v = v;
			B.ramp = B.saucer = B.ignoreSaucer = -1;
			B.lanes = lanes_at (*t, p);
			B.still = p;
			return b;
		}
	return -1;
}
bool World::plungerBusy () const
{
	for (int b = 0; b < MAXBALL; b++) if (ball[b].state == B_PLUNGER) return true;
	return false;
}
int World::addOnPlunger ()
{
	if (plungerBusy ()) return -1;
	int b = addBall (t->plunger, V (0, 0));
	if (b >= 0) ball[b].state = B_PLUNGER;
	return b;
}
bool World::launch (double speed)
{
	for (int b = 0; b < MAXBALL; b++)
		if (ball[b].state == B_PLUNGER)
		{
			ball[b].state = B_PLAY;
			ball[b].v = V (0, -speed);
			ball[b].still = ball[b].p; ball[b].stillFrames = 0;
			emit (P_LAUNCH, 0, b, speed);
			return true;
		}
	return false;
}
int World::liveBalls () const { int n = 0; for (int b = 0; b < MAXBALL; b++) n += ball[b].state != B_OFF; return n; }
int World::playBalls () const { int n = 0; for (int b = 0; b < MAXBALL; b++) n += ball[b].state == B_PLAY; return n; }

Vec World::flipTip (int f) const
{
	const Flip &F = t->flip[f];
	return F.pivot + V (pb_cos (flipAng[f]), pb_sin (flipAng[f])) * F.len;
}
Vec World::rampPos (int b) const
{
	const Ball &B = ball[b];
	if (B.state != B_RAMP) return B.p;
	const Ramp &r = t->ramp[B.ramp];
	int total = frames_of (r.time);
	double f = total > 0 ? 1 - (double) B.timer / total : 1, L = 0;
	for (int i = 1; i < r.npath; i++) L += sqrt (len2 (r.path[i] - r.path[i - 1]));
	double want = f * L;
	for (int i = 1; i < r.npath; i++)
	{
		double s = sqrt (len2 (r.path[i] - r.path[i - 1]));
		if (want <= s && s > 0) return r.path[i - 1] + (r.path[i] - r.path[i - 1]) * (want / s);
		want -= s;
	}
	return r.path[r.npath - 1];
}
bool World::onFlipper (int b) const
{
	const Ball &B = ball[b];
	for (int f = 0; f < t->nflip; f++)
	{
		const Flip &F = t->flip[f];
		double c = pb_cos (flipAng[f]), s = pb_sin (flipAng[f]);
		Vec d = B.p - F.pivot;
		double x = d.x * c + d.y * s, y = -d.x * s + d.y * c;
		double tt = x / F.len;
		if (tt < 0) tt = 0; else if (tt > 1) tt = 1;
		double rad = F.r0 + (F.r1 - F.r0) * tt;
		if (len2 (V (x - tt * F.len, y)) < (t->ball + rad + 3) * (t->ball + rad + 3)) return true;
	}
	return false;
}

void World::rotateLanes (int dir)
{
	int g = t->rotate;
	if (g < 0) return;
	int idx[MAXLANE], n = 0;
	bool old[MAXLANE];
	for (int i = 0; i < t->nlane; i++) if (t->lane[i].group == g) { idx[n] = i; old[n] = laneLit[i]; n++; }
	if (n < 2) return;
	for (int i = 0; i < n; i++) laneLit[idx[i]] = dir < 0 ? old[(i + 1) % n] : old[(i + n - 1) % n];
}
void World::nudge ()
{
	double side = random () & 1 ? 1 : -1;
	for (int b = 0; b < MAXBALL; b++)
		if (ball[b].state == B_PLAY) ball[b].v = ball[b].v + V (side * 0.6 * NUDGE_SPEED, -0.8 * NUDGE_SPEED);
}

// ---- one sub-step's collisions, for one ball --------------------------------------------------------------------------------
void World::collide (int b, Vec p0, const double *a0, const double *a1, double h)
{
	const Table &T = *t;
	Ball &B = ball[b];
	const double r = T.ball;
	Vec n;
	// the segments: the toys' faces first (a sling's face lies on its body's wall), then the walls
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < T.nseg; i++)
		{
			const Seg &s = T.seg[i];
			if ((s.kind == S_WALL) != (pass == 1)) continue;
			if (B.p.x + r < s.lo.x || B.p.x - r > s.hi.x || B.p.y + r < s.lo.y || B.p.y - r > s.hi.y) continue;
			if (s.kind == S_TARGET && T.tgt[s.elem].drop && tgtDown[s.elem]) continue;
			if (!seg_contact (s.a, s.b, p0, B.p, r, n)) continue;
			double vn = dot (B.v, n);
			if (s.kind == S_SLING && vn <= -SLING_MIN)
			{
				// (its kick varies a little -- 1 to 1.15 times its strength, turned by up to 10 degrees, the ball's speed
				//  along the face halved: the two slings cannot keep a ball bouncing between them for ever)
				const Sling &sl = T.sling[s.elem];
				Vec vt = B.v - n * vn;
				double kick = dmax (s.bounce * -vn, sl.kick * (1 + 0.15 * (double) (random () & 1023) / 1023));
				double turn = (((double) (random () & 1023) / 1023) * 2 - 1) * (10 * PB_PI / 180);
				double c = pb_cos (turn), sn = pb_sin (turn);
				B.v = vt * 0.5 + V (n.x * c - n.y * sn, n.x * sn + n.y * c) * kick;
				slingFlash[s.elem] = SLING_FRAMES;
				emit (P_SLING, s.elem, b, sqrt (len2 (B.v)));
				continue;
			}
			respond (B.v, n, s.bounce, s.friction);
			if (s.kind == S_TARGET && vn < -50)
			{
				int k = s.elem;
				const Target &g = T.tgt[k];
				if (!g.drop) { tgtLit[k] = true; emit (P_TARGET, k, b, -vn); continue; }
				tgtDown[k] = true;
				emit (P_DROP, k, b, -vn);
				const Bank &bk = T.bank[g.bank];
				bool all = true;
				for (int j = 0; j < bk.n; j++) if (!tgtDown[bk.tgt[j]]) all = false;
				if (all && !bankTimer[g.bank]) { bankTimer[g.bank] = BANK_FRAMES; emit (P_BANK, g.bank, b); }
			}
		}
	// the arcs: the inner or the outer face (the side it came from), the round ends
	for (int i = 0; i < T.narc; i++)
	{
		const Arc &a = T.arc[i];
		Vec dv = B.p - a.c;
		double d2 = len2 (dv);
		if (d2 > (a.r + r) * (a.r + r)) continue;
		if (a.r > r && d2 < (a.r - r) * (a.r - r)) continue;
		double d = sqrt (d2);
		if (d > 0)
		{
			Vec u = dv * (1 / d);
			if (arc_has (a, u))
			{
				bool inside = len2 (p0 - a.c) < a.r * a.r;
				if (inside && a.r - d < r) { B.p = a.c + u * (a.r - r); respond (B.v, -u, a.bounce, a.friction); continue; }
				if (!inside && d - a.r < r) { B.p = a.c + u * (a.r + r); respond (B.v, u, a.bounce, a.friction); continue; }
			}
		}
		if (a.span >= 360) continue;
		for (int k = 0; k < 2; k++)
		{
			Vec e = a.c + (k ? a.tu : a.fu) * a.r, de = B.p - e;
			double e2 = len2 (de);
			if (e2 >= r * r || e2 == 0) continue;
			Vec ne = de * (1 / sqrt (e2));
			B.p = e + ne * r;
			respond (B.v, ne, a.bounce, a.friction);
		}
	}
	// the posts and the bumpers
	for (int i = 0; i < T.ncirc; i++)
	{
		const Circle &c = T.circ[i];
		Vec dv = B.p - c.c;
		double d2 = len2 (dv), R = c.r + r;
		if (d2 >= R * R) continue;
		double d = sqrt (d2);
		n = d > 0 ? dv * (1 / d) : V (0, -1);
		B.p = c.c + n * R;
		double vn = dot (B.v, n);
		if (c.kind == C_BUMPER && vn < 0)
		{
			Vec vt = B.v - n * vn;
			B.v = vt + n * dmax (c.bounce * -vn, c.kick);
			bumperFlash[i] = FLASH_FRAMES;
			emit (P_BUMPER, i, b, sqrt (len2 (B.v)));
		}
		else respond (B.v, n, c.bounce, 0);
	}
	// the flippers, in their rotating frame: the ball's place at the sub-step's start against the flipper's start angle,
	// its place now against its angle now -- a ball the flipper swept past is pushed back to the side it was on
	for (int f = 0; f < T.nflip; f++)
	{
		const Flip &F = T.flip[f];
		double reach = F.len + F.r0 + r + 2;
		if (len2 (B.p - F.pivot) > reach * reach && len2 (p0 - F.pivot) > reach * reach) continue;
		double ca = pb_cos (a0[f]), sa = pb_sin (a0[f]), cb = pb_cos (a1[f]), sb = pb_sin (a1[f]);
		Vec d0 = p0 - F.pivot, d1 = B.p - F.pivot;
		Vec l0 = V (d0.x * ca + d0.y * sa, -d0.x * sa + d0.y * ca);
		Vec l1 = V (d1.x * cb + d1.y * sb, -d1.x * sb + d1.y * cb);
		double side = l0.y > 0 ? 1 : l0.y < 0 ? -1 : (l1.y >= 0 ? 1 : -1);
		double tt = l1.x / F.len;
		if (tt < 0) tt = 0; else if (tt > 1) tt = 1;
		double rad = F.r0 + (F.r1 - F.r0) * tt;
		Vec ql = V (tt * F.len, 0), nl;
		if (tt > 0 && tt < 1)
		{
			if (l1.y * side >= 0 && dabs (l1.y) >= r + rad) continue;
			nl = V (0, side);
			l1.y = side * (r + rad);
		}
		else
		{
			Vec dq = l1 - ql;
			double dd2 = len2 (dq);
			if (dd2 >= (r + rad) * (r + rad)) continue;
			double dd = sqrt (dd2);
			nl = dd > 0 ? dq * (1 / dd) : V (0, side);
			l1 = ql + nl * (r + rad);
		}
		n = V (nl.x * cb - nl.y * sb, nl.x * sb + nl.y * cb);
		B.p = F.pivot + V (l1.x * cb - l1.y * sb, l1.x * sb + l1.y * cb);
		Vec cl = ql + nl * rad;			// the contact point, from the pivot
		Vec cw = V (cl.x * cb - cl.y * sb, cl.x * sb + cl.y * cb);
		double w = (a1[f] - a0[f]) / h;
		Vec u = V (-w * cw.y, w * cw.x);	// its velocity
		Vec vrel = B.v - u;
		double vn = dot (vrel, n);
		if (vn < 0)
		{
			double e = vn > -30 ? 0 : F.bounce;
			B.v = u + vrel - n * ((1 + e) * vn);
		}
	}
	// the one-way gates: ignored until the ball crossed them the allowed way, then a wall seen from that side
	for (int i = 0; i < T.ngate; i++)
	{
		const Gate &g = T.gate[i];
		Vec gn = cross_normal (g.a, g.b, g.pass);
		if (dot (p0 - g.a, gn) < 0 || dot (B.v, gn) >= 0) continue;
		if (seg_contact (g.a, g.b, p0, B.p, r, n)) respond (B.v, n, 0.5, 0.1);
	}
	cap (B.v);
}

// Two balls (equal masses): pushed apart half each, the restitution 0.9
void World::ballBall ()
{
	const double r = t->ball;
	for (int i = 0; i < MAXBALL; i++)
		for (int j = i + 1; j < MAXBALL; j++)
		{
			Ball &A = ball[i], &B = ball[j];
			if (A.state != B_PLAY || B.state != B_PLAY) continue;
			Vec d = B.p - A.p;
			double d2 = len2 (d);
			if (d2 >= 4 * r * r) continue;
			double dist = sqrt (d2);
			Vec n = dist > 0 ? d * (1 / dist) : V (1, 0);
			double over = 2 * r - dist;
			A.p = A.p - n * (over / 2);
			B.p = B.p + n * (over / 2);
			double vr = dot (B.v - A.v, n);
			if (vr < 0)
			{
				double k = -(1 + 0.9) * vr / 2;
				A.v = A.v - n * k;
				B.v = B.v + n * k;
				if (vr < -100) emit (P_BALLHIT, i, j, -vr);
			}
		}
}

// The sensors on the ball's path this sub-step: lanes, ramp entries, saucers, the plunger, the drain
void World::sensors (int b, Vec p0)
{
	const Table &T = *t;
	Ball &B = ball[b];
	const double r = T.ball;
	unsigned now = lanes_at (T, B.p), in = now & ~B.lanes;
	B.lanes = now;
	for (int i = 0; in && i < T.nlane; i++)
		if (in & 1u << i)
		{
			laneLit[i] = true;
			emit (P_LANE, i, b);
			int g = T.lane[i].group;
			if (g < 0) continue;
			bool all = true;
			for (int k = 0; k < T.nlane; k++) if (T.lane[k].group == g && !laneLit[k]) all = false;
			if (!all) continue;
			for (int k = 0; k < T.nlane; k++) if (T.lane[k].group == g) laneLit[k] = false;
			emit (P_LANES, g, b);
		}
	for (int i = 0; i < T.nramp; i++)
	{
		const Ramp &R = T.ramp[i];
		Vec rn = cross_normal (R.a, R.b, R.pass);
		double d0 = dot (p0 - R.a, rn), d1 = dot (B.p - R.a, rn);
		if (!(d0 < 0 && d1 >= 0)) continue;
		Vec x = p0 + (B.p - p0) * (d0 / (d0 - d1)), ab = R.b - R.a;
		double s = dot (x - R.a, ab) / len2 (ab);
		if (s < 0 || s > 1) continue;
		B.state = B_RAMP; B.ramp = i; B.timer = frames_of (R.time);
		rampFlash[i] = RAMP_FRAMES;
		emit (P_RAMP, i, b);
		return;
	}
	if (B.ignoreSaucer >= 0)
	{
		const Saucer &S = T.saucer[B.ignoreSaucer];
		if (len2 (B.p - S.c) > (S.r + r) * (S.r + r)) B.ignoreSaucer = -1;
	}
	for (int i = 0; i < T.nsaucer; i++)
	{
		const Saucer &S = T.saucer[i];
		if (i == B.ignoreSaucer || len2 (B.p - S.c) >= S.r * S.r || len2 (B.v) >= SAUCER_MAX * SAUCER_MAX) continue;
		bool busy = false;
		for (int k = 0; k < MAXBALL; k++) if (ball[k].state == B_SAUCER && ball[k].saucer == i) busy = true;
		if (busy) continue;
		B.state = B_SAUCER; B.saucer = i; B.p = S.c; B.v = V (0, 0); B.timer = frames_of (S.hold);
		emit (P_SAUCER, i, b);
		return;
	}
	if (dabs (B.p.x - T.plunger.x) < r && B.p.y >= T.plunger.y && B.v.y >= 0 && !plungerBusy ())
	{
		B.state = B_PLUNGER; B.p = T.plunger; B.v = V (0, 0);
		emit (P_PLUNGER, 0, b);
		return;
	}
	if (B.p.y > T.drain)
	{
		B.state = B_OFF;
		emit (P_DRAIN, 0, b);
	}
}

// The frame's timers: the ramps and the saucers give their balls back, the cleared banks rise, the ball search
void World::timers ()
{
	const Table &T = *t;
	for (int b = 0; b < MAXBALL; b++)
	{
		Ball &B = ball[b];
		if (B.state == B_RAMP && --B.timer <= 0)
		{
			const Ramp &R = T.ramp[B.ramp];
			B.state = B_PLAY; B.p = R.path[R.npath - 1]; B.v = R.out;
			B.lanes = lanes_at (T, B.p); B.still = B.p; B.stillFrames = 0;
			emit (P_RAMP_OUT, B.ramp, b);
		}
		else if (B.state == B_SAUCER && --B.timer <= 0)
		{
			const Saucer &S = T.saucer[B.saucer];
			B.state = B_PLAY; B.p = S.c; B.v = S.out; B.ignoreSaucer = B.saucer;
			B.still = B.p; B.stillFrames = 0;
			emit (P_EJECT, B.saucer, b);
		}
		else if (B.state == B_PLAY)
		{
			if (len2 (B.p - B.still) >= 4) { B.still = B.p; B.stillFrames = 0; }
			else if (++B.stillFrames >= SEARCH_FRAMES)
			{
				B.stillFrames = 0;
				if (!onFlipper (b))
				{
					double side = random () & 1 ? 1 : -1;
					B.v = B.v + V (side * 0.6 * SEARCH_SPEED, -0.8 * SEARCH_SPEED);
					searches++;
					emit (P_SEARCH, 0, b);
				}
			}
		}
	}
	for (int k = 0; k < T.nbank; k++)
		if (bankTimer[k] > 0 && --bankTimer[k] == 0)
			for (int j = 0; j < T.bank[k].n; j++) tgtDown[T.bank[k].tgt[j]] = false;
}

void World::step (const Input &in)
{
	const Table &T = *t;
	nev = 0;
	frame++;
	for (int i = 0; i < T.ncirc; i++) if (bumperFlash[i] > 0) bumperFlash[i]--;
	for (int i = 0; i < T.nsling; i++) if (slingFlash[i] > 0) slingFlash[i]--;
	for (int i = 0; i < T.nramp; i++) if (rampFlash[i] > 0) rampFlash[i]--;
	// the flippers: up while their key is held (not after a tilt), the press edges
	for (int s = 0; s < 2; s++)
	{
		bool h = (s ? in.right : in.left) && !flipperDead;
		if (h && !held[s]) emit (P_FLIPPER_UP, s, -1);
		held[s] = h;
	}
	double target[MAXFLIP], wmax = 0;
	for (int f = 0; f < T.nflip; f++)
	{
		const Flip &F = T.flip[f];
		target[f] = held[F.side] ? F.up : F.rest;
		flipW[f] = flipAng[f] == target[f] ? 0 : target[f] > flipAng[f] ? F.speed : -F.speed;
		if (flipW[f]) wmax = dmax (wmax, F.speed * (F.len + F.r0));
	}
	// the plunger: pulled while held (full in 1 s); released, it launches the ball on it; a tap: the auto launch
	if (in.plunger)
	{
		pullFrames++;
		pull = pullFrames >= FPS ? 1 : (double) pullFrames / FPS;
	}
	else if (pullFrames > 0)
	{
		launch (pullFrames > 6 ? T.plungerMax * pull : T.plungerAuto);
		pullFrames = 0; pull = 0;
	}
	else if (in.tap) launch (T.plungerAuto);
	// the sub-steps: at least MINSUB, each moving a ball (or a flipper's surface) at most half a ball's radius
	double vmax = 0;
	for (int b = 0; b < MAXBALL; b++) if (ball[b].state == B_PLAY) vmax = dmax (vmax, sqrt (len2 (ball[b].v)));
	double travel = (vmax + wmax + T.gravity / FPS) / FPS, room = 0.5 * T.ball;
	int n = (int) (travel / room);
	if (n * room < travel) n++;
	if (n < MINSUB) n = MINSUB;
	double h = 1.0 / FPS / n;
	for (int s = 0; s < n; s++)
	{
		double a0[MAXFLIP], a1[MAXFLIP];
		for (int f = 0; f < T.nflip; f++)
		{
			a0[f] = flipAng[f];
			double a = flipAng[f] + flipW[f] * h;
			if ((flipW[f] > 0 && a > target[f]) || (flipW[f] < 0 && a < target[f])) a = target[f];
			flipAng[f] = a1[f] = a;
		}
		Vec p0[MAXBALL];
		for (int b = 0; b < MAXBALL; b++)
		{
			Ball &B = ball[b];
			p0[b] = B.p;
			if (B.state != B_PLAY) continue;
			B.v.y += T.gravity * h;
			cap (B.v);
			B.p = B.p + B.v * h;
		}
		ballBall ();
		for (int b = 0; b < MAXBALL; b++) if (ball[b].state == B_PLAY) collide (b, p0[b], a0, a1, h);
		for (int b = 0; b < MAXBALL; b++) if (ball[b].state == B_PLAY) sensors (b, p0[b]);
		subSteps++;
		if (onSubStep) onSubStep (*this, hookData);
	}
	for (int f = 0; f < T.nflip; f++) if (flipAng[f] == target[f]) flipW[f] = 0;
	timers ();
}

}
