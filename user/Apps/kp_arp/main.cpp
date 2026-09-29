//
// kp_arp -- a Koton generator plugin (user/kplug.h): the arpeggiator, a port of Koton Studio's
// reference generator (Plugins/Generators/KotonPluginArpeggiator/Arpeggiator.cs): the same parameters,
// the same state ({"v":1, "duration", "params": {...}, "rhythm": {beats, spb, starts, lens}}), the same
// notes -- its "Random" pattern draws from .NET's seeded Random (kt::NetRandom), as Koton does.
//
// At each tick (notes per beat; a dotted beat in 6/8, 9/8, 12/8), or at each note of a custom rhythm
// looped over the block, it takes the chord under it (Koton's plugin contract: the basic quality; no
// chord: the key's tonic triad), voices it at the octave asked (close, drop-2, wide, one per octave),
// repeats it over 1 + extend octaves (3 at most) and plays the pool Up, Down, Up-Down, Down-Up, at
// Random, or as a Chord; voice leading: at a chord change, from the note nearest the last one.
//
#include "kplug.h"
#include "Apps/koton/engine/kbase.h"		// kt::NetRandom: .NET's seeded Random, bit for bit

static const char *const PATTERNS[] = { "Up", "Down", "Up-Down", "Down-Up", "Random", "Chord", 0 };
static const char *const ARTIC[] = { "Legato", "Normal", "Detached", "Staccato", 0 };
static const char *const SPREADS[] = { "Close", "Drop-2", "Wide", "One per octave", 0 };
static const char *const RHYTHMS[] = { "Regular", "Custom", 0 };
static const char *const ONOFF[] = { "Off", "On", 0 };
enum { P_PATTERN, P_NPB, P_EXTEND, P_ARTIC, P_VEL, P_OCTAVE, P_VL, P_SPREAD, P_RHYTHM, NP };
static const KpParamDef P[NP] = {
	{ "pattern", "Pattern", 0, 5, 0, "", 1, PATTERNS },
	{ "notes_per_beat", "Notes / beat", 1, 8, 2, "", 1, 0 },
	{ "extend", "Extend", 0, 4, 0, "oct", 1, 0 },
	{ "articulation", "Articulation", 0, 3, 1, "", 1, ARTIC },
	{ "velocity", "Velocity", 1, 127, 100, "", 1, 0 },
	{ "octave", "Octave", 0, 8, 4, "", 1, 0 },
	{ "voice_leading", "Voice leading", 0, 1, 0, "", 1, ONOFF },
	{ "spread", "Spread", 0, 3, 0, "", 1, SPREADS },
	{ "rhythm_mode", "Rhythm", 0, 1, 0, "", 1, RHYTHMS },
};

// the custom rhythm (the "rhythm" key of the state): note starts / lengths in slices, `spb` a beat
enum { RMAX = 512 };
struct Rhythm { int beats, spb, n, start[RMAX], len[RMAX]; };
static Rhythm s_rhythm = { 2, 4, 0, {}, {} };		// the process's own state (its editor's)

static void readRhythm (const json::Value &st, Rhythm &r)
{
	r.beats = 2; r.spb = 4; r.n = 0;
	const json::Value &e = st["rhythm"];
	if (!e.isObj ()) return;
	r.beats = e["beats"].asInt (2); if (r.beats < 1) r.beats = 1;
	r.spb = e["spb"].asInt (4); if (r.spb < 1) r.spb = 1;
	const json::Value *s = e["starts"].first (), *l = e["lens"].first ();
	for (; s && r.n < RMAX; s = s->next, l = l ? l->next : 0)
	{
		r.start[r.n] = s->asInt (0);
		int len = l ? l->asInt (1) : 1;
		r.len[r.n++] = len < 1 ? 1 : len;
	}
}
static void loadState (const json::Value &st) { readRhythm (st, s_rhythm); }
static void saveState (json::Writer &w)
{
	w.key ("rhythm"); w.beginObj (true);
	w.key ("beats"); w.num (s_rhythm.beats); w.key ("spb"); w.num (s_rhythm.spb);
	w.key ("starts"); w.beginArr (true); for (int i = 0; i < s_rhythm.n; i++) w.num (s_rhythm.start[i]); w.endArr ();
	w.key ("lens"); w.beginArr (true); for (int i = 0; i < s_rhythm.n; i++) w.num (s_rhythm.len[i]); w.endArr ();
	w.endObj ();
}

static double gateFor (int a) { return a == 0 ? 1.0 : a == 2 ? 0.40 : a == 3 ? 0.15 : 0.75; }

static int snapToOctave (int baseMidi, int pc)
{
	int basePc = ((baseMidi % 12) + 12) % 12, delta = ((pc - basePc) + 12) % 12;
	if (delta >= 7) delta -= 12;
	int v = baseMidi + delta;
	return v < 0 ? 0 : v > 127 ? 127 : v;
}

// KotonChordExtensions.ApplyVoicing: the chord's notes from rootMidi, spread, ascending
static int voicing (const int *iv, int niv, int rootMidi, int spread, int *out)
{
	for (int i = 0; i < niv; i++) out[i] = rootMidi + iv[i];
	int n = niv;
	if (spread == 1 && n >= 2) out[n - 2] += 12;
	else if (spread == 2 && n >= 4) { out[n - 2] += 12; out[n - 4] += 12; }
	else if (spread == 2 && n == 3) out[1] += 12;
	else if (spread >= 3) for (int i = 1; i < n; i++) out[i] += 12 * i;
	for (int i = 1; i < n; i++) { int v = out[i], j = i; while (j > 0 && out[j - 1] > v) { out[j] = out[j - 1]; j--; } out[j] = v; }
	return n;
}

static int buildPool (const int *notes, int n, int octaves, int *pool)
{
	if (octaves < 1) octaves = 1;
	if (octaves > 3) octaves = 3;			// (Koton's BuildPool caps it)
	int k = 0;
	for (int o = 0; o < octaves; o++) for (int i = 0; i < n; i++) { int p = notes[i] + o * 12; if (p >= 0 && p <= 127) pool[k++] = p; }
	for (int i = 1; i < k; i++) { int v = pool[i], j = i; while (j > 0 && pool[j - 1] > v) { pool[j] = pool[j - 1]; j--; } pool[j] = v; }
	return k;
}

static int nearestIndex (const int *pool, int n, int target)
{
	int best = 0, bd = 1 << 30;
	for (int i = 0; i < n; i++) { int d = pool[i] - target; if (d < 0) d = -d; if (d < bd) { bd = d; best = i; } }
	return best;
}

static bool generate (const KpContext &c, const float *p, KpNotes &out)
{
	int pattern = (int) p[P_PATTERN], npb = (int) p[P_NPB]; if (npb < 1) npb = 1;
	int ext = (int) p[P_EXTEND]; ext = ext < 0 ? 0 : ext > 4 ? 4 : ext;
	int octaves = 1 + ext, artic = (int) p[P_ARTIC], vel = (int) p[P_VEL], octave = (int) p[P_OCTAVE];
	double gate = gateFor (artic);
	int baseMidi = 12 + octave * 12;
	bool vl = p[P_VL] >= 0.5f;
	int spread = (int) p[P_SPREAD];
	double tick = (c.ternary ? 1.5 : 1.0) / npb;
	double duration = c.length < 0.25 ? 0.25 : c.length;
	bool custom = p[P_RHYTHM] >= 0.5f;
	Rhythm *rh = new Rhythm;				// (the request's own rhythm: its state)
	readRhythm (*c.state, *rh);
	int seed = (int) ((unsigned) pattern * 1315423911u ^ (unsigned) npb * 2654435761u ^ (unsigned) octaves * 40503u);
	kt::NetRandom rng (seed);
	int step = 0, last = -1, lastRoot = -1, lastQ = -1;
	// the ticks: regular, or the custom rhythm looped over the block
	double motif = custom && rh->n > 0 ? (double) (rh->beats * rh->spb) / rh->spb : 0;	// (TotalSlices / SlicesPerBeat)
	double motifStart = 0; int ri = 0;
	double t = 0;
	for (int guard = 0; guard < 100000; guard++)
	{
		double onset, tickLen;
		if (custom && rh->n > 0 && motif > 0)
		{
			if (motifStart >= duration - 1e-9) break;
			if (ri >= rh->n) { ri = 0; motifStart += motif; continue; }
			onset = motifStart + (double) rh->start[ri] / rh->spb;
			if (onset >= duration - 1e-9) { ri = rh->n; continue; }
			tickLen = (double) rh->len[ri] / rh->spb; if (tickLen < 0.01) tickLen = 0.01;
			if (onset + tickLen > duration) tickLen = duration - onset;
			ri++;
			if (tickLen <= 0) continue;
		}
		else
		{
			if (t >= duration - 1e-9) break;
			onset = t; tickLen = tick; t += tick;
		}
		const KpChord *ch = c.chordAt (onset);
		int root, q; const int *iv; int niv;
		static const int MAJ[3] = { 0, 4, 7 }, MIN[3] = { 0, 3, 7 };
		if (ch) { root = ch->root; q = ch->basic; iv = ch->biv; niv = ch->nbiv; }
		else { root = c.tonic; q = c.mode ? 1 : 0; iv = c.mode ? MIN : MAJ; niv = 3; }
		int notes[8], pool[32];
		int nn = voicing (iv, niv, snapToOctave (baseMidi, root), spread, notes);
		int np = buildPool (notes, nn, octaves, pool);
		if (!np) continue;
		bool changed = lastRoot >= 0 && (lastRoot != root || lastQ != q);
		if (vl && last >= 0 && changed && pattern != 5) step = nearestIndex (pool, np, last);
		lastRoot = root; lastQ = q;
		int note;
		switch (pattern)
		{
		case 0: note = pool[step % np]; step++; break;
		case 1: note = pool[np - 1 - (step % np)]; step++; break;
		case 2: case 3:
		{
			int period = 2 * (np - 1); if (period < 1) period = 1;
			int ph = ((step % period) + period) % period;
			int idx = ph < np ? ph : period - ph;
			if (pattern == 3) idx = np - 1 - idx;
			note = pool[idx < 0 ? 0 : idx >= np ? np - 1 : idx]; step++;
		} break;
		case 4: note = pool[rng.next (np)]; step++; break;
		case 5:
			for (int i = 0; i < np; i++) out.add (onset, tickLen * gate, pool[i], vel);
			last = pool[0];
			continue;
		default: note = pool[0]; break;
		}
		last = note;
		out.add (onset, tickLen * gate, note, vel);
	}
	delete rh;
	return true;
}

static const KpDesc desc = {
	.name = "Arpeggiator", .kind = KP_GENERATOR, .params = P, .nparams = NP,
	.generate = generate, .saveState = saveState, .loadState = loadState,
};

KPLUG_MAIN (desc)
