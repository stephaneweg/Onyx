//
// kp_euclid -- a Koton generator plugin (user/kplug.h): a euclidean melody. E(hits, steps) -- the hits
// spread as evenly as they can be over the steps (tresillo E(3,8), cinquillo E(5,8)...), rotated --
// is looped over the block, a step a fraction of a beat; each hit plays the next tone of a pool made
// of the chord under it (or the key's scale, or both) over a range of octaves, following a contour:
// up, down, up-down, a random walk, or at random. At a chord change the walk goes on from the tone
// nearest the last one (voice leading). An accent on the cycle's first hit.
//
#include "kplug.h"

static const char *const CONTOURS[] = { "Up", "Down", "Up-Down", "Random walk", "Random", 0 };
static const char *const TONES[] = { "Chord", "Scale", "Chord + scale", 0 };
static const char *const ARTIC[] = { "Legato", "Normal", "Detached", "Staccato", 0 };
static const char *const ONOFF[] = { "Off", "On", 0 };
enum { P_HITS, P_STEPS, P_ROT, P_NPB, P_CONTOUR, P_TONES, P_OCTAVE, P_RANGE, P_VEL, P_ACCENT, P_ARTIC, P_SEED, NP };
static const KpParamDef P[NP] = {
	{ "hits", "Hits", 1, 32, 5, "", 1, 0 },
	{ "steps", "Steps", 2, 32, 8, "", 1, 0 },
	{ "rotation", "Rotation", 0, 31, 0, "", 1, 0 },
	{ "notes_per_beat", "Steps / beat", 1, 8, 4, "", 1, 0 },
	{ "contour", "Contour", 0, 4, 2, "", 1, CONTOURS },
	{ "tones", "Tones", 0, 2, 0, "", 1, TONES },
	{ "octave", "Octave", 0, 8, 4, "", 1, 0 },
	{ "range", "Range", 1, 3, 2, "oct", 1, 0 },
	{ "velocity", "Velocity", 1, 127, 96, "", 1, 0 },
	{ "accent", "Accent", 0, 1, 1, "", 1, ONOFF },
	{ "articulation", "Articulation", 0, 3, 1, "", 1, ARTIC },
	{ "seed", "Seed", 0, 999, 1, "", 1, 0 },
};

static double gateFor (int a) { return a == 0 ? 1.0 : a == 2 ? 0.40 : a == 3 ? 0.15 : 0.75; }

// E(k, n): step i is a hit when (i k mod n) < k -- Bresenham's spreading, a hit on the first step
static bool isHit (int i, int k, int n) { return (i * k) % n < k; }

static void sortInts (int *a, int n) { for (int i = 1; i < n; i++) { int v = a[i], j = i; while (j > 0 && a[j - 1] > v) { a[j] = a[j - 1]; j--; } a[j] = v; } }

// the pool at a beat: MIDI notes, ascending, from the octave's C over `range` octaves
static int pool (const KpContext &c, double beat, int tones, int octave, int range, int *out)
{
	int pcs[24], n = 0;
	const KpChord *ch = c.chordAt (beat);
	if (tones != 1)						// the chord's tones (the key's tonic triad without one)
	{
		if (ch) for (int i = 0; i < ch->niv; i++) pcs[n++] = (ch->root + ch->iv[i]) % 12;
		else { pcs[n++] = c.tonic; pcs[n++] = (c.tonic + (c.mode ? 3 : 4)) % 12; pcs[n++] = (c.tonic + 7) % 12; }
	}
	if (tones != 0) for (int i = 0; i < c.nscale; i++) pcs[n++] = (c.tonic + c.scale[i]) % 12;
	bool used[12] = { false };
	int u[12], nu = 0;
	for (int i = 0; i < n; i++) if (!used[pcs[i]]) { used[pcs[i]] = true; u[nu++] = pcs[i]; }
	sortInts (u, nu);
	int base = 12 + octave * 12, k = 0;
	for (int o = 0; o < range; o++) for (int i = 0; i < nu; i++) { int m = base + o * 12 + u[i]; if (m >= 0 && m <= 127) out[k++] = m; }
	return k;
}

static bool generate (const KpContext &c, const float *p, KpNotes &out)
{
	int steps = (int) p[P_STEPS], hits = (int) p[P_HITS], rot = (int) p[P_ROT], npb = (int) p[P_NPB];
	if (steps < 2) steps = 2;
	if (hits > steps) hits = steps;
	if (hits < 1) hits = 1;
	if (npb < 1) npb = 1;
	int contour = (int) p[P_CONTOUR], tones = (int) p[P_TONES], octave = (int) p[P_OCTAVE], range = (int) p[P_RANGE];
	int vel = (int) p[P_VEL];
	bool accent = p[P_ACCENT] >= 0.5f;
	double tick = (c.ternary ? 1.5 : 1.0) / npb, gate = gateFor ((int) p[P_ARTIC]);
	unsigned rnd = c.seed * 2654435761u + (unsigned) p[P_SEED] * 40503u + 1;
	int idx = 0, dir = 1, last = -1, lastRoot = -1, hitNo = 0, lastCycle = -1;
	int nsteps = (int) (c.length / tick + 1e-6);
	for (int s = 0; s < nsteps; s++)
	{
		int i = ((s + rot) % steps + steps) % steps;
		if (!isHit (i, hits, steps)) continue;
		double onset = s * tick;
		// the length: up to the next hit (legato), shortened by the articulation
		int nx = s + 1; while (nx < nsteps && !isHit (((nx + rot) % steps + steps) % steps, hits, steps)) nx++;
		double len = (nx - s) * tick * gate;
		int pl[48];
		int np = pool (c, onset, tones, octave, range, pl);
		if (!np) continue;
		const KpChord *ch = c.chordAt (onset);
		int root = ch ? ch->root : -1;
		if (last >= 0 && root != lastRoot)			// voice leading: from the nearest tone
		{
			int best = 0, bd = 1 << 30;
			for (int k = 0; k < np; k++) { int d = pl[k] - last; if (d < 0) d = -d; if (d < bd) { bd = d; best = k; } }
			idx = best;
		}
		lastRoot = root;
		rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5;
		switch (contour)
		{
		case 0: idx = hitNo == 0 && last < 0 ? 0 : (idx + 1) % np; break;
		case 1: idx = hitNo == 0 && last < 0 ? np - 1 : (idx - 1 + np) % np; break;
		case 2:
			if (hitNo == 0 && last < 0) { idx = 0; dir = 1; break; }
			if (np == 1) { idx = 0; break; }
			if (idx + dir >= np || idx + dir < 0) dir = -dir;
			idx += dir;
			break;
		case 3: { int stp = (int) (rnd % 5) - 2; idx += stp; if (idx < 0) idx = -idx; if (idx >= np) idx = 2 * (np - 1) - idx; if (idx < 0) idx = 0; } break;
		default: idx = (int) (rnd % (unsigned) np); break;
		}
		if (idx < 0) idx = 0;
		if (idx >= np) idx = np - 1;
		int v = accent && s / steps != lastCycle ? vel + 20 : vel;	// (the cycle's first hit)
		lastCycle = s / steps;
		out.add (onset, len, pl[idx], v);
		last = pl[idx]; hitNo++;
	}
	return true;
}

static const KpDesc desc = {
	.name = "Euclidean melody", .kind = KP_GENERATOR, .params = P, .nparams = NP,
	.generate = generate,
};

KPLUG_MAIN (desc)
