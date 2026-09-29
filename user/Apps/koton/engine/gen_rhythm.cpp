//
// gen_rhythm.cpp -- rhythm as notes (see gen.h): DrumPattern.cs (16 grooves, the fill, a drawn
// motif), EuclideanRhythm.cs (E(k,n), the traditional names), PolyDrum.cs (rings of drum lanes on
// one cycle), PolyChord.cs (rings playing the chord track's chords, the emergent melody).
//
#include "gen.h"

namespace kt {

// ---- drums ----------------------------------------------------------------------------------------------------
const char *const g_laneNames[DRUM_LANES] = {
	"Kick", "Snare", "Closed hi-hat", "Open hi-hat", "Pedal hi-hat", "Rim (side stick)", "Clap", "Low tom", "Mid tom", "High tom",
	"Crash", "Ride", "Acoustic kick", "Electric snare", "Low floor tom", "High floor tom", "Hi-mid tom", "Chinese cymbal", "Ride bell",
	"Splash", "Crash 2", "Ride 2", "Tambourine", "Cowbell", "Vibraslap", "High bongo", "Low bongo", "Muted high conga", "Open high conga",
	"Low conga", "High timbale", "Low timbale", "High agogo", "Low agogo", "Cabasa", "Maracas", "Short whistle", "Long whistle",
	"Short guiro", "Long guiro", "Claves", "High wood block", "Low wood block", "Muted cuica", "Open cuica", "Muted triangle",
	"Open triangle" };
static const int s_laneKeys[DRUM_LANES] = {
	36, 38, 42, 46, 44, 37, 39, 45, 47, 50, 49, 51, 35, 40, 41, 43, 48, 52, 53, 55, 57, 59, 54, 56, 58,
	60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81 };
const char *const g_drumStyleNames[DRUM_STYLE_COUNT] = {
	"Rock - basic", "Rock - driving", "Pop", "Funk (16th)", "Disco (four on the floor)", "Jazz swing", "Shuffle / Blues",
	"Bossa nova", "Half-time", "Hip-hop / boom-bap", "March", "Reggae one-drop", "Waltz", "Punk (fast)",
	"Ballad (cross-stick)", "Trap (rolling hats)", "Custom..." };
const char *const g_densityNames[4] = { "Auto", "Light", "Normal", "Dense" };

int keyForLane (int lane) { return (lane >= 0 && lane < DRUM_LANES) ? s_laneKeys[lane] : 38; }
int laneForKey (int key)
{
	for (int l = 0; l < DRUM_LANES; l++) if (s_laneKeys[l] == key) return l;
	int best = 0, bd = 0x7fffffff;
	for (int l = 0; l < DRUM_LANES; l++) { int d = iabs (s_laneKeys[l] - key); if (d < bd) { bd = d; best = l; } }
	return best;
}

enum { KICK = 0, SNARE = 1, CLOSED = 2, OPEN = 3, PEDAL = 4, RIM = 5, CLAP = 6, LOWTOM = 7, MIDTOM = 8, HITOM = 9, CRASH = 10, RIDE = 11 };

static void hit (Vec<Slice> &g, int slice, int lane) { if (lane < 0 || lane >= 96) return; if (slice >= 0 && slice < g.size ()) g[slice].set (lane, true); }
static void at (Vec<Slice> &g, double b, int lane) { hit (g, iround (b * SPQ), lane); }
static void layer (Vec<Slice> &g, int beats, int lane, double step, double off = 0)
{
	if (step <= 0) return;
	for (double t = off; t < beats - 1e-9; t += step) at (g, t, lane);
}
static double hatStep (int density) { return density == 1 ? 1.0 : density == 3 ? 0.25 : 0.5; }

static void renderStyle (Vec<Slice> &g, int beats, int style, int density, int bar)
{
	switch (style)
	{
	case 0: layer (g, beats, CLOSED, hatStep (density)); for (int b = 0; b < beats; b++) at (g, b, (b % 2 == 0) ? KICK : SNARE); break;
	case 1:
		layer (g, beats, CLOSED, hatStep (density)); at (g, 0, KICK); if (beats >= 3) at (g, 2.5, KICK);
		for (int b = 1; b < beats; b += 2) at (g, b, SNARE);
		if (bar == 0) at (g, 0, CRASH);
		break;
	case 2:
		layer (g, beats, CLOSED, hatStep (density)); at (g, 0, KICK); if (beats >= 3) { at (g, 2, KICK); at (g, 2.5, KICK); }
		for (int b = 1; b < beats; b += 2) at (g, b, SNARE);
		break;
	case 3:
		layer (g, beats, CLOSED, density == 1 ? 0.5 : 0.25); at (g, 0, KICK); at (g, 0.75, KICK); if (beats >= 3) at (g, 2.5, KICK);
		for (int b = 1; b < beats; b += 2) at (g, b, SNARE);
		if (density == 3 && beats >= 3) at (g, 2.25, SNARE);
		break;
	case 4:
		for (int b = 0; b < beats; b++) at (g, b, KICK);
		layer (g, beats, OPEN, 1.0, 0.5); layer (g, beats, CLOSED, 1.0);
		for (int b = 1; b < beats; b += 2) { at (g, b, SNARE); at (g, b, CLAP); }
		break;
	case 5:
		for (int b = 0; b < beats; b++) { at (g, b, RIDE); if (b % 2 == 1) { at (g, b + 2.0 / 3.0, RIDE); at (g, b, PEDAL); } }
		at (g, 0, KICK);
		break;
	case 6: for (int b = 0; b < beats; b++) { at (g, b, CLOSED); at (g, b + 2.0 / 3.0, CLOSED); at (g, b, (b % 2 == 0) ? KICK : SNARE); } break;
	case 7:
	{
		layer (g, beats, CLOSED, 0.5);
		const double ps[4] = { 0.0, 1.5, 2.0, 3.5 };
		for (int i = 0; i < 4; i++) if (ps[i] < beats) at (g, ps[i], RIM);
		at (g, 0, KICK); if (beats >= 3) at (g, 2.5, KICK);
	} break;
	case 8: layer (g, beats, CLOSED, hatStep (density)); at (g, 0, KICK); at (g, beats >= 3 ? 2 : beats / 2.0, SNARE); break;
	case 9:
		layer (g, beats, CLOSED, hatStep (density)); at (g, 0, KICK); if (beats >= 2) at (g, 1.5, KICK);
		for (int b = 1; b < beats; b += 2) at (g, b, SNARE);
		break;
	case 10:
		for (int e = 0; e < beats * 2; e++) at (g, e * 0.5, SNARE);
		for (int b = 0; b < beats; b += 2) at (g, b, KICK);
		if (bar == 0) at (g, 0, CRASH);
		break;
	case 11: { layer (g, beats, CLOSED, 1.0, 0.5); double drop = beats >= 3 ? 2 : beats / 2.0; at (g, drop, KICK); at (g, drop, SNARE); } break;
	case 12: at (g, 0, KICK); for (int b = 1; b < beats; b++) at (g, b, RIM); layer (g, beats, CLOSED, 1.0); break;
	case 13:
		layer (g, beats, CLOSED, 0.5);
		for (int e = 0; e < beats * 2; e++) at (g, e * 0.5, KICK);
		for (int b = 1; b < beats; b += 2) at (g, b, SNARE);
		if (bar == 0) at (g, 0, CRASH);
		break;
	case 14:
		layer (g, beats, RIDE, 1.0); at (g, 0, KICK); if (beats >= 3) at (g, 2, KICK);
		for (int b = 1; b < beats; b += 2) at (g, b, RIM);
		break;
	case 15:
		layer (g, beats, CLOSED, 0.25); at (g, 0, KICK); if (beats >= 2) at (g, 1.5, KICK); if (beats >= 3) at (g, 2.75, KICK);
		at (g, beats >= 3 ? 2 : beats / 2.0, SNARE);
		break;
	default: layer (g, beats, CLOSED, 0.5); for (int b = 0; b < beats; b++) at (g, b, (b % 2 == 0) ? KICK : SNARE); break;
	}
}

static void renderFill (Vec<Slice> &g, int beats)
{
	int eighths = beats * 2;
	const int ladder[8] = { SNARE, SNARE, SNARE, HITOM, HITOM, MIDTOM, MIDTOM, LOWTOM };
	for (int e = 0; e < eighths; e++) at (g, e * 0.5, ladder[imin (7, e * 8 / imax (1, eighths))]);
	at (g, 0, CRASH); at (g, 0, KICK);
}

static void mapBar (const Vec<Slice> &laneBar, Vec<Slice> &out, int offset)
{
	for (int s = 0; s < laneBar.size (); s++)
		for (int lane = 0; lane < DRUM_LANES; lane++)
			if (laneBar[s].on (lane))
			{
				int row = s_laneKeys[lane] - 12;
				if (row >= 0 && row < 96 && offset + s < out.size ()) out[offset + s].set (row, true);
			}
}

Vec<RiffNote> laneNotesForStyle (int style, int beats)
{
	int b = imax (1, beats);
	Vec<Slice> g; g.resize (b * SPQ);
	renderStyle (g, b, iclamp (style, 0, DRUM_STYLE_COUNT - 1), 0, 0);
	Vec<RiffNote> out;
	for (int s = 0; s < g.size (); s++)
		for (int lane = 0; lane < DRUM_LANES; lane++)
			if (g[s].on (lane)) out.push (RiffNote (lane, s, 1));
	return out;
}

static void sortNotes (Vec<RiffNote> &n)
{
	n.sort ([] (const RiffNote &a, const RiffNote &b) { return a.start != b.start ? a.start < b.start : a.note < b.note; });
}

Riff generateDrums (const DrumModule &m)
{
	Riff r; r.name = "Drums";
	if (m.custom.notes.size () > 0)
	{
		int spqN = m.custom.spq > 0 ? m.custom.spq : SPQ;
		int unit = m.custom.slices.size () > 0 ? m.custom.slices.size () : imax (1, notes::lengthOf (m.custom.notes));
		int reps = imax (1, m.repeats);
		int totalN = unit * reps;
		Vec<long long> seen;
		for (int rep = 0; rep < reps; rep++)
			for (int i = 0; i < m.custom.notes.size (); i++)
			{
				const RiffNote &n = m.custom.notes[i];
				if (n.note < 0 || n.note >= DRUM_LANES || n.start < 0 || n.start >= unit) continue;
				int row = s_laneKeys[n.note] - 12, atS = rep * unit + n.start;
				if (row < 0 || row >= 96) continue;
				long long key = (long long) row * totalN + atS;
				if (seen.contains (key)) continue;
				seen.push (key);
				r.notes.push (RiffNote (row, atS, 1));
			}
		sortNotes (r.notes);
		r.spq = spqN; r.lengthSlices = totalN;
		return r;
	}
	int repeats = imax (1, m.repeats);
	Vec<Slice> out; int spq;
	if (m.style == DRUM_CUSTOM_STYLE && m.custom.slices.size () > 0)
	{
		spq = m.custom.spq > 0 ? m.custom.spq : SPQ;
		out.resize (m.custom.slices.size () * repeats);
		for (int rep = 0; rep < repeats; rep++) mapBar (m.custom.slices, out, rep * m.custom.slices.size ());
	}
	else
	{
		int beats = imax (1, m.beatsPerBar);
		int barSlices = beats * SPQ;
		spq = SPQ;
		out.resize (barSlices * repeats);
		for (int bar = 0; bar < repeats; bar++)
		{
			Vec<Slice> g; g.resize (barSlices);
			if (m.fillLast && bar == repeats - 1 && repeats > 1) renderFill (g, beats);
			else renderStyle (g, beats, iclamp (m.style, 0, DRUM_STYLE_COUNT - 1), m.density, bar);
			mapBar (g, out, bar * barSlices);
		}
	}
	r.notes = notes::fromSlices (out); r.lengthSlices = out.size (); r.spq = spq;
	return r;
}

// ---- euclidean rhythms -----------------------------------------------------------------------------------------
void euclidPattern (int k, int n, int rotation, Vec<bool> &out)
{
	n = imax (1, n); k = imax (0, imin (k, n));
	Vec<bool> p; p.resize (n);
	for (int i = 0; i < n; i++) p[i] = ((i * k) % n) < k;
	int r = imod (rotation, n);
	out.resize (n);
	for (int i = 0; i < n; i++) out[(i + r) % n] = p[i];
}

void effectivePattern (bool customMode, const Vec<int> &customHits, int hits, int steps, int rotation, Vec<bool> &out)
{
	if (customMode)
	{
		int n = imax (1, steps);
		out.resize (n);
		for (int i = 0; i < n; i++) out[i] = false;
		for (int i = 0; i < customHits.size (); i++) if (customHits[i] >= 0 && customHits[i] < n) out[customHits[i]] = true;
	}
	else euclidPattern (hits, steps, rotation, out);
}

const char *euclidName (const Vec<bool> &p)
{
	struct KnownR { int n; int gaps[8]; int ng; const char *name; };
	static const KnownR known[] = {
		{ 8, { 3, 3, 2 }, 3, "tresillo" }, { 8, { 2, 1, 2, 1, 2 }, 5, "cinquillo" }, { 5, { 3, 2 }, 2, "tango" },
		{ 4, { 1, 1, 2 }, 3, "cumbia" }, { 9, { 2, 2, 2, 3 }, 4, "aksak" }, { 12, { 2, 3, 2, 2, 3 }, 5, "venda" },
		{ 16, { 2, 3, 2, 2, 3, 2, 2 }, 7, "samba" } };
	int gaps[64]; int ng = 0, n = p.size (), first = -1;
	for (int i = 0; i < n; i++) if (p[i]) { first = i; break; }
	if (first < 0) return 0;
	int prev = first;
	for (int k = 1; k <= n && ng < 64; k++)
	{
		int i = (first + k) % n;
		if (p[i]) { gaps[ng++] = k - (prev - first); prev = first + k; }
	}
	for (unsigned e = 0; e < sizeof known / sizeof known[0]; e++)
	{
		if (known[e].ng != ng) continue;
		for (int rot = 0; rot < ng; rot++)
		{
			bool ok = true;
			for (int j = 0; j < ng && ok; j++) if (gaps[(j + rot) % ng] != known[e].gaps[j]) ok = false;
			if (ok) return known[e].name;
		}
	}
	return 0;
}

static long long gcdl (long long a, long long b) { while (b != 0) { long long t = a % b; a = b; b = t; } return a < 0 ? -a : a; }
static long long lcml (long long a, long long b) { long long g = gcdl (a, b); return g == 0 ? 0 : a / g * b; }

// ---- polyrhythmic drums --------------------------------------------------------------------------------------
static int polyDrumCycleBeats (const PolyDrumModule &m) { return imax (1, m.beats > 0 ? m.beats : m.beatsPerBar); }
static int polyDrumSpq (const PolyDrumModule &m)
{
	long long lcm = 1;
	for (int i = 0; i < m.layers.size (); i++)
	{
		if (m.layers[i].muted) continue;
		lcm = lcml (lcm, imax (1, m.layers[i].steps));
		if (lcm > 4800) return 24;
	}
	long long spq = lcm / gcdl (polyDrumCycleBeats (m), lcm);
	return (int) (spq < 1 ? 1 : spq);
}
double polyDrumTotalBeats (const PolyDrumModule &m) { return polyDrumCycleBeats (m) * (double) imax (1, m.repeats); }
static int cellStart (int cellIndex, int steps, int beats, int spq) { long long num = (long long) cellIndex * beats * spq; return (int) ((num + steps / 2) / steps); }

Riff generatePolyDrum (const PolyDrumModule &m)
{
	int spq = polyDrumSpq (m), beats = polyDrumCycleBeats (m);
	int cycleSlices = beats * spq, repeats = imax (1, m.repeats), total = cycleSlices * repeats;
	Riff r; r.name = "PolyDrums"; r.spq = spq; r.lengthSlices = total;
	Vec<long long> seen;
	Vec<bool> pat;
	for (int li = 0; li < m.layers.size (); li++)
	{
		const EuclidLayer &l = m.layers[li];
		if (l.muted || l.lane < 0 || l.lane >= DRUM_LANES) continue;
		int row = keyForLane (l.lane) - 12;
		if (row < 0 || row >= 96) continue;
		int accentRow = -1;
		if (l.accentLane >= 0 && l.accentLane < DRUM_LANES) { int ar = keyForLane (l.accentLane) - 12; if (ar >= 0 && ar < 96) accentRow = ar; }
		effectivePattern (l.customMode, l.customHits, l.hits, l.steps, l.rotation, pat);
		int steps = pat.size (); if (steps == 0) continue;
		int firstOn = -1; for (int i = 0; i < steps; i++) if (pat[i]) { firstOn = i; break; }
		if (firstOn < 0) continue;
		for (int rep = 0; rep < repeats; rep++)
			for (int i = 0; i < steps; i++)
			{
				if (!pat[i]) continue;
				int s = rep * cycleSlices + cellStart (i, steps, beats, spq);
				if (s < 0 || s >= total) continue;
				int useRow = (accentRow >= 0 && i == firstOn) ? accentRow : row;
				long long key = (long long) useRow * total + s;
				if (seen.contains (key)) continue;
				seen.push (key);
				r.notes.push (RiffNote (useRow, s, 1));
			}
	}
	sortNotes (r.notes);
	return r;
}

// ---- polyrhythmic chords ---------------------------------------------------------------------------------------
double polyChordTotalBeats (const PolyChordModule &m)
{
	if (m.beats > 0) return m.beats;
	int sum = 0; for (int i = 0; i < m.chords.size (); i++) sum += imax (1, m.chords[i].beats);
	return sum;
}
static int polyChordCycleBeats (const PolyChordModule &m) { return imax (1, m.cycleBeats); }
static int polyChordSpq (const PolyChordModule &m)
{
	long long lcm = 1;
	for (int i = 0; i < m.layers.size (); i++)
	{
		if (m.layers[i].muted) continue;
		lcm = lcml (lcm, imax (1, m.layers[i].steps));
		if (lcm > 4800) return 24;
	}
	long long spq = lcm / gcdl (polyChordCycleBeats (m), lcm);
	return (int) (spq < 1 ? 1 : spq);
}

static int resolveStart (int restart, const int *voiced, int nv, const EuclidChordLayer &lay, int lastPlayed)
{
	if (nv == 0) return 0;
	switch (restart)
	{
	case RESTART_GRAVE: return 0;
	case RESTART_AIGU: return nv - 1;
	case RESTART_TONIC: return 0;
	case RESTART_TIERCE: return imin (1, nv - 1);
	case RESTART_QUINTE: return imin (2, nv - 1);
	default:
	{
		if (lastPlayed < 0) return 0;
		int best = 0, bestDist = 0x7fffffff, off = lay.octave * 12;
		for (int i = 0; i < nv; i++) { int d = iabs ((voiced[i] + off) - lastPlayed); if (d < bestDist) { bestDist = d; best = i; } }
		return best;
	}
	}
}

static int pickNext (int contour, int voicedLen, int oi, int onCount, int &curIdx, int &dir, NetRandom &rnd)
{
	if (voicedLen <= 1) { curIdx = 0; return 0; }
	switch (contour)
	{
	case 1: curIdx++; if (curIdx >= voicedLen) curIdx = 0; return curIdx;
	case 2: curIdx--; if (curIdx < 0) curIdx = voicedLen - 1; return curIdx;
	case 3: return curIdx;
	case 4:
		if (dir == 0) { curIdx--; if (curIdx <= 0) { curIdx = 0; dir = 1; } }
		else { curIdx++; if (curIdx >= voicedLen - 1) { curIdx = voicedLen - 1; dir = 0; } }
		return curIdx;
	case 5: curIdx = rnd.next (voicedLen); return curIdx;
	default:
	{
		double phase = (oi / (double) imax (1, onCount)) * 3.14159265358979323846 * 2;
		double norm = 0.5 * (1 - __builtin_cos (phase));
		curIdx = iround (norm * (voicedLen - 1));
		return curIdx;
	}
	}
}

static RiffNote octaveForVoiceLeading (const RiffNote &orig, int lastNote)
{
	if (lastNote < 0) return orig;
	int bestOct = 0, bestDist = iabs (orig.note - lastNote);
	for (int oct = -4; oct <= 4; oct++)
	{
		if (oct == 0) continue;
		int shifted = orig.note + oct * 12;
		if (shifted < 0 || shifted > 95) continue;
		int d = iabs (shifted - lastNote);
		if (d < bestDist) { bestDist = d; bestOct = oct; }
	}
	return bestOct == 0 ? orig : RiffNote (orig.note + bestOct * 12, orig.start, orig.length);
}

Riff generatePolyChord (const PolyChordModule &m, const Project &p, double startBeat)
{
	int spq = polyChordSpq (m);
	Riff out; out.name = "PolyChords"; out.spq = spq;
	// the chords: the chord track's under the module, else its own list (old files)
	Vec<PolyChordItem> chords;
	double total = polyChordTotalBeats (m);
	if (total > 0)
	{
		Vec<ChordSeg> segs = segments (p, startBeat, total);
		for (int i = 0; i < segs.size (); i++)
		{
			PolyChordItem it; it.root = segs[i].root; it.quality = segs[i].quality; it.inversion = segs[i].inversion;
			it.beats = imax (1, iround (segs[i].len));
			chords.push (it);
		}
	}
	if (chords.size () == 0) chords = m.chords;
	if (chords.size () == 0) { out.lengthSlices = 1; return out; }

	Vec<Vec<int> > voiced; voiced.resize (chords.size ());
	for (int i = 0; i < chords.size (); i++)
	{
		int n[16]; int c = chordNotes (chords[i].root, m.octave + chords[i].octaveShift, chords[i].quality, chords[i].inversion, m.openVoicing, n);
		for (int j = 0; j < c; j++) voiced[i].push (n[j]);
	}
	int cycleBeats = polyChordCycleBeats (m), cycleSlices = cycleBeats * spq;
	Vec<int> chordEnd; int acc = 0;
	for (int i = 0; i < chords.size (); i++) { acc += imax (1, chords[i].beats) * spq; chordEnd.push (acc); }
	int totalSlices = acc;
	int moduleEnd = imax (1, iround (total * spq));
	auto chordIndexAt = [&] (int sl) { for (int i = 0; i < chordEnd.size (); i++) if (sl < chordEnd[i]) return i; return chordEnd.size () - 1; };

	int nL = m.layers.size ();
	Vec<int> lastMidi, lastIdx, zigDir, lastChordSeen;
	lastMidi.resize (nL, -1); lastIdx.resize (nL, -1); zigDir.resize (nL, 0); lastChordSeen.resize (nL, -1);
	Vec<bool> pat;
	for (int li = 0; li < nL; li++)
	{
		const EuclidChordLayer &lay = m.layers[li];
		if (lay.muted) continue;
		effectivePattern (lay.customMode, lay.customHits, lay.hits, lay.steps, lay.rotation, pat);
		int steps = pat.size (); if (steps == 0) continue;
		Vec<int> onsets;
		for (int k = 0; k < steps; k++) if (pat[k]) { long long num = (long long) k * cycleBeats * spq; onsets.push ((int) ((num + steps / 2) / steps)); }
		if (onsets.size () == 0) continue;
		onsets.sort ([] (int a, int b) { return a < b; });
		Vec<int> absOn;
		int cycCount = (totalSlices + cycleSlices - 1) / cycleSlices;
		for (int cy = 0; cy < cycCount; cy++)
			for (int i = 0; i < onsets.size (); i++) { int s = cy * cycleSlices + onsets[i]; if (s >= totalSlices) break; absOn.push (s); }
		NetRandom rnd ((int) (unsigned) ((unsigned) lay.randomSeed * 1000003u + (unsigned) li * 17u));
		int cellLen = imax (1, cycleSlices / steps);
		for (int oi = 0; oi < absOn.size (); oi++)
		{
			int s = absOn[oi];
			int chIdx = chordIndexAt (s);
			const Vec<int> &v = voiced[chIdx];
			if (v.size () == 0) continue;
			bool chordChanged = lastChordSeen[li] != chIdx;
			lastChordSeen[li] = chIdx;
			int idx, extraOct = 0;
			if (m.mode == PC_ONE_RING_PER_TONE)
			{
				int ti = lay.toneIndex, n = v.size ();
				idx = imod (ti, n);
				extraOct = (ti - idx) / n;
			}
			else if (chordChanged) idx = resolveStart (m.restart, v.data (), v.size (), lay, lastMidi[li]);
			else idx = pickNext (lay.contour, v.size (), oi, absOn.size (), lastIdx[li], zigDir[li], rnd);
			int midi = v[idx] + (lay.octave + extraOct) * 12;
			int len = cellLen;
			if (lay.legato)
			{
				int nextS = oi + 1 < absOn.size () ? absOn[oi + 1] : totalSlices;
				len = imax (1, imin (nextS, chordEnd[chIdx]) - s);
			}
			int row = midi - 12;
			if (s < moduleEnd && len > moduleEnd - s) len = moduleEnd - s;
			if (row >= 0 && row < 96 && s < moduleEnd && len >= 1) out.notes.push (RiffNote (row, s, len));
			lastMidi[li] = midi; lastIdx[li] = idx;
		}
	}
	sortNotes (out.notes);

	if (m.monodicPick && out.notes.size () > 1)
	{
		NetRandom rndMono (m.monodicSeed);
		Vec<RiffNote> filtered;
		int lastNote = -1, repeatCount = 0, i0 = 0;
		const Vec<RiffNote> &on = out.notes;
		while (i0 < on.size ())
		{
			int i1 = i0 + 1;
			while (i1 < on.size () && on[i1].start == on[i0].start) i1++;
			RiffNote pick;
			switch (m.monodicStrategy)
			{
			case MONO_HIGHEST: { int b = i0; for (int k = i0 + 1; k < i1; k++) if (on[k].note > on[b].note) b = k; pick = octaveForVoiceLeading (on[b], lastNote); break; }
			case MONO_LOWEST: { int b = i0; for (int k = i0 + 1; k < i1; k++) if (on[k].note < on[b].note) b = k; pick = octaveForVoiceLeading (on[b], lastNote); break; }
			case MONO_RANDOM: pick = octaveForVoiceLeading (on[i0 + rndMono.next (i1 - i0)], lastNote); break;
			default:
				if (lastNote < 0) pick = on[i0 + rndMono.next (i1 - i0)];
				else
				{
					int bestIdx = i0, bestOct = 0; double bestScore = 1e300;
					for (int k = i0; k < i1; k++)
						for (int oct = -4; oct <= 4; oct++)
						{
							int shifted = on[k].note + oct * 12;
							if (shifted < 0 || shifted > 95) continue;
							double d = iabs (shifted - lastNote);
							if (m.monodicAvoidRepeat && shifted == lastNote) d += 3.0 + 3.0 * repeatCount;
							if (d < bestScore) { bestScore = d; bestIdx = k; bestOct = oct; }
						}
					pick = RiffNote (on[bestIdx].note + bestOct * 12, on[bestIdx].start, on[bestIdx].length);
				}
				break;
			}
			filtered.push (pick);
			if (pick.note == lastNote) repeatCount++; else repeatCount = 0;
			lastNote = pick.note;
			i0 = i1;
		}
		out.notes = move (filtered);
	}
	out.lengthSlices = moduleEnd;
	return out;
}

} // namespace kt
