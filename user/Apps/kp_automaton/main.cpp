//
// kp_automaton -- a Koton generator plugin (user/kplug.h): an elementary (one-dimensional, Wolfram)
// cellular automaton, a port of Koton Studio's (Plugins/Generators/KotonPluginCellularAutomata): the
// same parameters and state (flat: {id: value..., "_dur"}), the same notes -- its first row drawn
// from .NET's seeded Random (kt::NetRandom), as Koton does. A row of `width` cells evolves by the
// rule (0..255: rule 30 chaotic, 90 Sierpinski, 110...); at each tick every live cell plays a note:
// its place in the row mapped onto a pool -- the scale (Koton's fixed scales from C, or, on Onyx,
// the song's key), or, chord-aware, the chord under it -- over a range of octaves.
//
#include "kplug.h"
#include "Apps/koton/engine/kbase.h"		// kt::NetRandom

static const char *const SCALES[] = { "Chromatic", "Major", "Minor", "Pentatonic major", "Pentatonic minor", "Key's scale", 0 };
static const char *const SEEDS[] = { "Centre cell", "Random density", "All on", 0 };
static const char *const ARTIC[] = { "Legato", "Normal", "Detached", "Staccato", 0 };
static const char *const ONOFF[] = { "Off", "On", 0 };
enum { P_NPB, P_RULE, P_WIDTH, P_SCALE, P_BASEOCT, P_RANGE, P_SEED, P_SEEDMODE, P_DENSITY, P_VEL, P_ARTIC, P_CHORD, NP };
static const KpParamDef P[NP] = {
	{ "notes_per_beat", "Notes/beat", 1, 8, 4, "", 1, 0 },
	{ "rule", "Rule", 0, 255, 90, "", 1, 0 },
	{ "width", "Width", 8, 32, 16, "cells", 1, 0 },
	{ "scale", "Scale", 0, 5, 5, "", 1, SCALES },
	{ "base_octave", "Octave", 0, 8, 4, "", 1, 0 },
	{ "oct_range", "Range", 1, 3, 2, "oct", 1, 0 },
	{ "seed", "Seed", 0, 999, 1, "", 1, 0 },
	{ "seed_mode", "First row", 0, 2, 1, "", 1, SEEDS },
	{ "density", "Density", 0, 1, 0.3f, "", 0, 0 },
	{ "velocity", "Velocity", 1, 127, 90, "", 1, 0 },
	{ "articulation", "Articulation", 0, 3, 2, "", 1, ARTIC },
	{ "chord_aware", "Chord-aware", 0, 1, 1, "", 1, ONOFF },
};

static const int SC_CHROM[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, SC_MAJ[7] = { 0, 2, 4, 5, 7, 9, 11 }, SC_MIN[7] = { 0, 2, 3, 5, 7, 8, 10 };
static const int SC_PMAJ[5] = { 0, 2, 4, 7, 9 }, SC_PMIN[5] = { 0, 3, 5, 7, 10 };
static double gateFor (int a) { return a == 0 ? 1.0 : a == 2 ? 0.4 : a == 3 ? 0.15 : 0.75; }

static bool generate (const KpContext &c, const float *p, KpNotes &out)
{
	int npb = (int) p[P_NPB]; if (npb < 1) npb = 1;
	int rule = (int) p[P_RULE] & 255, width = (int) p[P_WIDTH];
	width = width < 8 ? 8 : width > 32 ? 32 : width;
	int scale = (int) p[P_SCALE], baseOct = (int) p[P_BASEOCT], range = (int) p[P_RANGE];
	range = range < 1 ? 1 : range > 3 ? 3 : range;
	int seed = (int) p[P_SEED], mode = (int) p[P_SEEDMODE], vel = (int) p[P_VEL];
	double density = p[P_DENSITY], gate = gateFor ((int) p[P_ARTIC]);
	bool chordAware = p[P_CHORD] >= 0.5f;
	double tick = (c.ternary ? 1.5 : 1.0) / npb, duration = c.length < 0.25 ? 0.25 : c.length;
	int baseMidi = 12 + baseOct * 12;
	// the scale's pool: Koton's scales are from C; "Key's scale" is the song's (Onyx)
	int deg[12], nd = 0, from = 0;
	switch (scale)
	{
	case 0: for (int i = 0; i < 12; i++) deg[nd++] = SC_CHROM[i]; break;
	case 1: for (int i = 0; i < 7; i++) deg[nd++] = SC_MAJ[i]; break;
	case 2: for (int i = 0; i < 7; i++) deg[nd++] = SC_MIN[i]; break;
	case 3: for (int i = 0; i < 5; i++) deg[nd++] = SC_PMAJ[i]; break;
	case 4: for (int i = 0; i < 5; i++) deg[nd++] = SC_PMIN[i]; break;
	default: for (int i = 0; i < c.nscale; i++) deg[nd++] = c.scale[i]; from = c.tonic; break;
	}
	int scalePool[36], nScale = 0;
	for (int o = 0; o < range; o++) for (int d = 0; d < nd; d++) scalePool[nScale++] = baseMidi + from + o * 12 + deg[d];
	// the first row (InitRow)
	unsigned char row[32], next[32];
	for (int i = 0; i < width; i++) row[i] = 0;
	kt::NetRandom rng (seed);
	if (mode == 0) row[width / 2] = 1;
	else if (mode == 2) for (int i = 0; i < width; i++) row[i] = 1;
	else for (int i = 0; i < width; i++) row[i] = rng.nextDouble () < density ? 1 : 0;
	bool any = false; for (int i = 0; i < width; i++) if (row[i]) any = true;
	if (!any) row[width / 2] = 1;
	for (double t = 0; t < duration - 1e-9; t += tick)
	{
		const int *pl = scalePool; int np = nScale, chordPool[24];
		if (chordAware)
		{
			const KpChord *ch = c.chordAt (t);
			if (ch)		// Koton's contract: the chord's basic notes from base + its root, per octave
			{
				np = 0;
				for (int o = 0; o < range; o++) for (int i = 0; i < ch->nbiv; i++) chordPool[np++] = baseMidi + ch->root + ch->biv[i] + o * 12;
				pl = chordPool;
			}
		}
		for (int i = 0; i < width && np > 0; i++)
		{
			if (!row[i]) continue;
			int idx = (i * np) / width; if (idx >= np) idx = np - 1;
			out.add (t, tick * gate, pl[idx], vel);
		}
		for (int i = 0; i < width; i++)		// the rule: the three cells above -> a bit of it
		{
			int pat = (row[(i - 1 + width) % width] << 2) | (row[i] << 1) | row[(i + 1) % width];
			next[i] = (unsigned char) ((rule >> pat) & 1);
		}
		for (int i = 0; i < width; i++) row[i] = next[i];
	}
	return true;
}

static const KpDesc desc = {
	.name = "Cellular automaton", .kind = KP_GENERATOR, .params = P, .nparams = NP,
	.generate = generate,
};

KPLUG_MAIN (desc)
