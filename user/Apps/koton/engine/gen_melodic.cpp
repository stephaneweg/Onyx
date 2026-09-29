//
// gen_melodic.cpp -- melodic lines (see gen.h): MelodicLineEngine.cs (a rhythm skeleton pitched from
// the harmony: downbeats first -- the contour shapes them --, then the half-strong and weak beats
// threaded between, then the passing tones; ornaments, a leap cap, variations), MelodicEuclid.cs
// (the same engine on euclidean rings), and renderModule / moduleBeats for every module kind.
//
#include "gen.h"
#include <math.h>

namespace kt {

GeneratorHook g_generatorHook = 0;

const char *const g_contourNames[9] = { "Wave (arcs)", "Rising", "Falling", "Static (pivot)", "Zigzag", "Random", "Thue-Morse", "L-system", "Fractal (1/f)" };
const char *const g_anchorNames[6] = { "Default (nearest)", "Root", "Third", "Fifth", "Seventh", "Ninth" };
const char *const g_variationNames[5] = { "None", "Split (cut the long notes)", "Gate (aerate)", "Retrograde", "Mirror (inversion)" };

// A set of pitch classes that remembers the order they were added in: C#'s HashSet<int> enumerates
// in insertion order, and the engine's tie-breaks (AnchorPc) depend on it.
struct PcSet
{
	unsigned mask; int order[12]; int n;
	PcSet () : mask (0), n (0) {}
	void add (int pc) { pc = imod (pc, 12); if (mask & (1u << pc)) return; mask |= 1u << pc; order[n++] = pc; }
	bool has (int pc) const { return (mask >> imod (pc, 12)) & 1; }
	int count () const { return n; }
};

static PcSet chordPcs (int root, int quality)
{
	PcSet s; int n[16]; int c = chordNotes (root, 4, quality, 0, false, n);
	for (int i = 0; i < c; i++) s.add (n[i]);
	return s;
}

static const int s_band[3] = { 74, 66, 58 };
static const int s_anchorGuide[5] = { 0, 4, 7, 10, 14 };
static int pcDist (int a, int b) { int d = imod (a - b, 12); return imin (d, 12 - d); }

static int classifyMetric (double phased, int num, int den)
{
	num = imax (1, num); den = imax (1, den);
	bool compound = den == 8 && num % 3 == 0;
	double mainPulseQ = compound ? 1.5 : 4.0 / den;
	int mainBeats = compound ? num / 3 : num;
	double pos = phased / mainPulseQ;
	double frac = pos - floor (pos + 1e-6);
	if (frac > 1e-4 && frac < 1 - 1e-4) return 3;
	int beat = imod (iround (pos) % mainBeats, mainBeats);
	if (beat == 0) return 0;
	if (mainBeats == 4 && beat == 2) return 1;
	return 2;
}

static int searchNearest (int prevMidi, const PcSet &pcs, int bandCenter, int halfBand)
{
	int anchor = prevMidi >= 0 ? prevMidi : bandCenter, best = -1, bestDist = 0x7fffffff;
	for (int mi = bandCenter - halfBand; mi <= bandCenter + halfBand; mi++)
	{
		if (!pcs.has (mi)) continue;
		int d = iabs (mi - anchor);
		if (d < bestDist) { bestDist = d; best = mi; }
	}
	return best;
}
static int nearestTone (int prevMidi, const PcSet &pcs, int bandCenter, int halfBand = 12)
{
	if (pcs.count () == 0) return -1;
	int best = searchNearest (prevMidi, pcs, bandCenter, halfBand);
	if (best < 0 && halfBand < 12) best = searchNearest (prevMidi, pcs, bandCenter, 12);
	return best;
}
static int nearestToneWithinLeap (int prevMidi, const PcSet &pcs, int maxLeap)
{
	if (pcs.count () == 0 || prevMidi < 0) return -1;
	int best = -1, bestDist = 0x7fffffff;
	for (int mi = imax (0, prevMidi - maxLeap); mi <= imin (127, prevMidi + maxLeap); mi++)
	{
		if (!pcs.has (mi)) continue;
		int d = iabs (mi - prevMidi);
		if (d < bestDist) { bestDist = d; best = mi; }
	}
	return best;
}
static int toneInDir (int prevMidi, const PcSet &pcs, int bandCenter, int dir, bool requireStep, int halfBand = 12)
{
	if (pcs.count () == 0) return -1;
	if (prevMidi < 0) return nearestTone (-1, pcs, bandCenter, halfBand);
	int lo = bandCenter - halfBand, hi = bandCenter + halfBand;
	for (int mi = prevMidi + dir; mi >= lo && mi <= hi; mi += dir)
		if (pcs.has (mi)) return (requireStep && iabs (mi - prevMidi) > 2) ? -1 : mi;
	return -1;
}
static int stepDownChordTone (int from, const PcSet &c) { for (int d = 1; d <= 2; d++) if (c.has (from - d)) return from - d; return -1; }
static int scaleStepNeighbor (int target, const PcSet &pcs, NetRandom &rng)
{
	int cand[4]; int n = 0; const int ds[4] = { 2, -2, 1, -1 };
	for (int i = 0; i < 4; i++) if (pcs.has (target + ds[i])) cand[n++] = target + ds[i];
	return n == 0 ? -1 : cand[rng.next (n)];
}
static int scaleStepAbove (int target, const PcSet &pcs) { for (int d = 1; d <= 2; d++) if (pcs.has (target + d)) return target + d; return -1; }
static int sgn (int v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
static int movedChordTone (int from, const PcSet &c, int band, int dir)
{
	int best = -1, bestScore = 0x7fffffff;
	for (int m = band - 24; m <= band + 24; m++)
	{
		if (m == from || !c.has (m)) continue;
		int score = iabs (m - from) + (dir != 0 && sgn (m - from) != dir ? 5 : 0);
		if (score < bestScore) { bestScore = score; best = m; }
	}
	return best;
}
static int anchorPc (int anchor, int root, const PcSet &c)
{
	if (c.count () == 0) return -1;
	int target = imod (root + s_anchorGuide[imin (anchor - 1, 4)], 12);
	int best = -1, bestD = 99;
	for (int i = 0; i < c.n; i++) { int d = pcDist (c.order[i], target); if (d < bestD) { bestD = d; best = c.order[i]; } }
	return best;
}
static int chordToneConnect (int left, int right, const PcSet &pcs, int band)
{
	if (pcs.count () == 0) return -1;
	bool both = left >= 0 && right >= 0;
	double target = both ? (left + right) / 2.0 : (left >= 0 ? left : (right >= 0 ? right : band));
	int best = -1, bestAny = -1; double bestD = 1e300, bestAnyD = 1e300;
	for (int mi = band - 24; mi <= band + 24; mi++)
	{
		if (!pcs.has (mi)) continue;
		double d = fabs (mi - target);
		if (d < bestAnyD) { bestAnyD = d; bestAny = mi; }
		if (mi == left || mi == right) continue;
		if (d < bestD) { bestD = d; best = mi; }
	}
	return best >= 0 ? best : bestAny;
}
static int passingBetween (int left, int right, const PcSet &chord, const PcSet &scale, int band, bool beatStartsWithNote, NetRandom &rng)
{
	const PcSet &fallback = chord.count () > 0 ? chord : scale;
	if (left < 0) return nearestTone (right >= 0 ? right : band, fallback, band);
	if (right >= 0 && right != left)
	{
		int dir = sgn (right - left);
		int pass = toneInDir (left, scale, band, dir, true, 12);
		if (pass >= 0 && (dir > 0 ? pass < right : pass > right)) return pass;
	}
	if (beatStartsWithNote)
	{
		int nb = scaleStepNeighbor (left, scale, rng);
		return (nb >= 0 && rng.nextDouble () < 0.6) ? nb : left;
	}
	if (right >= 0) { int appo = scaleStepNeighbor (right, scale, rng); if (appo >= 0) return appo; }
	return nearestTone (left, fallback, band);
}
static int thueMorse (int n) { int c = 0; while (n > 0) { c ^= (n & 1); n >>= 1; } return c; }
static void lsystem (int count, Str &out)
{
	Vec<char> s; s.push ('U');
	int guard = 0;
	while (s.size () < imax (1, count) && guard++ < 16)
	{
		Vec<char> t; t.reserve (s.size () * 3);
		for (int i = 0; i < s.size (); i++) { const char *r = s[i] == 'U' ? "UUD" : "UDD"; t.push (r[0]); t.push (r[1]); t.push (r[2]); }
		s = move (t);
	}
	s.push (0);
	out = s.data ();
}
static void fractalCurve (int n, int band, NetRandom &rng, int halfBand, Vec<int> &out)
{
	out.clear ();
	if (n <= 0) return;
	int size = 2; while (size + 1 < n) size *= 2; size += 1;
	Vec<double> h; h.resize (size, 0.0);
	double amp = dmax (2, halfBand * 2 / 3.0);
	h[0] = (rng.nextDouble () * 2 - 1) * amp; h[size - 1] = (rng.nextDouble () * 2 - 1) * amp;
	for (int seg = size - 1; seg >= 2; seg /= 2)
	{
		for (int i = 0; i + seg < size; i += seg) { int mid = i + seg / 2; h[mid] = (h[i] + h[i + seg]) * 0.5 + (rng.nextDouble () * 2 - 1) * amp; }
		amp *= 0.55;
	}
	for (int i = 0; i < n; i++)
	{
		int xi = iround ((double) i / imax (1, n - 1) * (size - 1));
		xi = iclamp (xi, 0, size - 1);
		double d = dmax (-halfBand, dmin (halfBand, h[xi]));
		out.push (band + iround (d));
	}
}

static int pickTone (int contour, int &dir, int &step, int arcLen, int prev, const PcSet &pcs, const PcSet &chord, int band, bool requireStep,
		     NetRandom &rng, int idx, const Str &moves, const Vec<int> &frac, int halfBand)
{
	int midi;
	switch (contour)
	{
	case 6:
		dir = thueMorse (idx) == 0 ? 1 : -1;
		midi = toneInDir (prev, pcs, band, dir, requireStep, halfBand);
		if (midi < 0) { dir = -dir; midi = toneInDir (prev, pcs, band, dir, requireStep, halfBand); }
		if (midi < 0) midi = nearestTone (prev, chord, band, halfBand);
		return midi;
	case 7:
		dir = (moves.len () > 0 && moves.c ()[idx % moves.len ()] == 'D') ? -1 : 1;
		midi = toneInDir (prev, pcs, band, dir, requireStep, halfBand);
		if (midi < 0) { dir = -dir; midi = toneInDir (prev, pcs, band, dir, requireStep, halfBand); }
		if (midi < 0) midi = nearestTone (prev, chord, band, halfBand);
		return midi;
	case 8:
	{
		int target = idx < frac.size () ? frac[idx] : band;
		midi = nearestTone (target, pcs, band, halfBand);
		if (midi < 0) midi = nearestTone (target, chord, band, halfBand);
		return midi;
	}
	case 1: case 2:
	{
		int d = contour == 1 ? 1 : -1;
		midi = toneInDir (prev, pcs, band, d, requireStep, halfBand);
		if (midi < 0) midi = nearestTone (contour == 1 ? band - halfBand : band + halfBand, pcs, band, halfBand);
		if (midi < 0) midi = nearestTone (prev, chord, band, halfBand);
		return midi;
	}
	case 3:
		midi = nearestTone (prev, pcs, band, halfBand);
		if (midi < 0) midi = nearestTone (prev, chord, band, halfBand);
		return midi;
	case 4:
		dir = -dir;
		midi = toneInDir (prev, pcs, band, dir, requireStep, halfBand);
		if (midi < 0) { dir = -dir; midi = toneInDir (prev, pcs, band, dir, requireStep, halfBand); }
		if (midi < 0) midi = nearestTone (prev, chord, band, halfBand);
		return midi;
	case 5:
		dir = rng.next (2) == 0 ? 1 : -1;
		midi = toneInDir (prev, pcs, band, dir, requireStep, halfBand);
		if (midi < 0) { dir = -dir; midi = toneInDir (prev, pcs, band, dir, requireStep, halfBand); }
		if (midi < 0) midi = nearestTone (prev, chord, band, halfBand);
		return midi;
	default:
		if (++step % arcLen == 0) dir = -dir;
		midi = toneInDir (prev, pcs, band, dir, requireStep, halfBand);
		if (midi < 0) { dir = -dir; midi = toneInDir (prev, pcs, band, dir, requireStep, halfBand); }
		if (midi < 0) midi = nearestTone (prev, chord, band, halfBand);
		return midi;
	}
}

struct Gen { int start, len, midi; };

Riff generateLine (const MelodicLineModule &m, const Project &p, double startBeat, int *carry)
{
	Riff out; out.name = "Line";
	const Vec<RiffNote> &mnotes = m.rhythm.notes;
	int spq = m.rhythm.spq > 0 ? m.rhythm.spq : 4;
	int totalSlices = imax (1, m.beatsPerBar) * spq;
	out.spq = spq; out.lengthSlices = totalSlices;
	if (mnotes.size () == 0) return out;
	double pickup = p.pickupBeats > 0 ? p.pickupBeats : 0;
	const int *scale = modeScale (effectiveMode (p.key));
	int tpc = tonicPc (p.key);
	PcSet scalePcs; for (int i = 0; i < 7; i++) scalePcs.add (tpc + scale[i]);
	int reg = m.registerShift, contour = m.contour, anchor = m.anchor;
	int continuity = iclamp (m.continuity, 0, 100), slope = m.tensionSlope, variation = m.variation;
	int amp = iclamp (m.amplitude > 0 ? m.amplitude : 12, 2, 24);
	int ornaments = iclamp (m.ornaments, 0, 100);
	NetRandom rng ((int) (unsigned) (1013u * (unsigned) (mnotes.size () + 7) + (unsigned) contour * 131u + (unsigned) iround (startBeat)));
	const int arcLen[3] = { 4, 6, 5 };
	int meterNum = p.timeSigNum > 0 ? p.timeSigNum : 4, meterDen = p.timeSigDen > 0 ? p.timeSigDen : 4;
	bool compound = meterDen == 8 && meterNum % 3 == 0;
	int beatSlices = imax (1, iround (spq * (compound ? 1.5 : 4.0 / meterDen)));
	bool hasFortCarry = carry != 0;		// the player hands 9 ints

	for (int v = 0; v < MelodicLineModule::MaxVoices; v++)
	{
		int band0 = s_band[v] + reg;
		Vec<RiffNote> vn;
		for (int i = 0; i < mnotes.size (); i++) if (mnotes[i].note == v) vn.push (mnotes[i]);
		vn.sort ([] (const RiffNote &a, const RiffNote &b) { return a.start < b.start; });
		if (variation == 1)
		{
			Vec<RiffNote> o;
			for (int i = 0; i < vn.size (); i++)
			{
				const RiffNote &n = vn[i];
				if (n.length >= spq && n.length >= 2) { int h = n.length / 2; o.push (RiffNote (n.note, n.start, h)); o.push (RiffNote (n.note, n.start + h, n.length - h)); }
				else o.push (n);
			}
			vn = move (o);
		}
		else if (variation == 2) { Vec<RiffNote> o; for (int i = 0; i < vn.size (); i += 2) o.push (vn[i]); vn = move (o); }
		int nc = vn.size ();
		if (nc == 0) continue;

		Vec<int> cls, roots, bandOf; Vec<PcSet> chords; Vec<bool> ok;
		cls.resize (nc, 0); roots.resize (nc, 0); bandOf.resize (nc, 0); chords.resize (nc); ok.resize (nc, false);
		for (int i = 0; i < nc; i++)
		{
			double absBeat = startBeat + vn[i].start / (double) spq;
			int root, quality;
			if (!chordAt (p, absBeat, &root, &quality, 0)) { ok[i] = false; continue; }
			ok[i] = true; roots[i] = root;
			chords[i] = chordPcs (root, quality);
			cls[i] = classifyMetric (absBeat - pickup, meterNum, meterDen);
			bandOf[i] = band0 + (nc > 1 ? slope * i / (nc - 1) : 0);
		}
		Vec<int> starts; for (int i = 0; i < nc; i++) if (!starts.contains (vn[i].start)) starts.push (vn[i].start);

		const int UNPLACED = -2147483647 - 1;
		Vec<int> pitch; pitch.resize (nc, UNPLACED);
		int carryPrev = carry ? carry[v] : -1;
		int lastFortMidi = hasFortCarry ? carry[3 + v] : -1;
		int lastFortRoot = hasFortCarry ? carry[6 + v] : -1;
		auto leftOf = [&] (int i) { for (int j = i - 1; j >= 0; j--) if (pitch[j] != UNPLACED) return pitch[j]; return carryPrev; };
		auto rightOf = [&] (int i) { for (int j = i + 1; j < nc; j++) if (pitch[j] != UNPLACED) return pitch[j]; return -1; };

		int dir = v == 1 ? -1 : 1, step = 0; bool anchorUsed = false;
		int waveLen = imax (2, m.waveLength > 0 ? m.waveLength : arcLen[v]);
		Str moves; if (contour == 7) lsystem (nc, moves);
		Vec<int> frac; if (contour == 8) fractalCurve (nc, band0, rng, amp, frac);

		int prevFort = carryPrev;
		for (int i = 0; i < nc; i++)
		{
			if (!ok[i] || cls[i] != 0) continue;
			int root = roots[i], band = bandOf[i]; const PcSet &pcs = chords[i];
			PcSet strong = pcs; bool anchorForced = false;
			if (anchor > 0 && !anchorUsed && pcs.count () > 0)
			{
				int apc = anchorPc (anchor, root, pcs);
				if (apc >= 0) { strong = PcSet (); strong.add (apc); anchorUsed = true; anchorForced = true; }
			}
			int choice = pickTone (contour, dir, step, waveLen, prevFort, strong, pcs, band, false, rng, i, moves, frac, amp);
			if (!anchorForced && choice >= 0 && pcs.count () > 1 && choice == lastFortMidi && root == lastFortRoot)
			{
				int alt = movedChordTone (choice, pcs, band, dir); if (alt >= 0) choice = alt;
			}
			pitch[i] = choice;
			if (choice >= 0) { prevFort = choice; lastFortMidi = choice; lastFortRoot = root; }
		}
		for (int level = 1; level <= 2; level++)
			for (int i = 0; i < nc; i++)
			{
				if (!ok[i] || cls[i] != level) continue;
				int band = bandOf[i]; const PcSet &pcs = chords[i]; int L = leftOf (i), R = rightOf (i);
				int choice = -1;
				if (anchor > 0 && !anchorUsed && level == 1 && pcs.count () > 0)
				{
					int apc = anchorPc (anchor, roots[i], pcs);
					if (apc >= 0) { PcSet one; one.add (apc); choice = nearestTone (L >= 0 && R >= 0 ? (L + R) / 2 : (L >= 0 ? L : band), one, band); anchorUsed = true; }
				}
				if (choice < 0) choice = chordToneConnect (L, R, pcs, band);
				pitch[i] = choice;
			}
		for (int i = 0; i < nc; i++)
		{
			if (!ok[i] || cls[i] != 3) continue;
			int band = bandOf[i]; const PcSet &pcs = chords[i]; int L = leftOf (i), R = rightOf (i);
			int nextBeat = (vn[i].start / beatSlices + 1) * beatSlices;
			bool syncope = (vn[i].start % beatSlices) != 0 && vn[i].start + vn[i].length > nextBeat;
			if (syncope)
			{
				PcSet s = pcs;
				double bAbs = startBeat + nextBeat / (double) spq;
				int br, bq;
				if (chordAt (p, bAbs, &br, &bq, 0)) { int nn[16]; int c = chordNotes (br, 4, bq, 0, false, nn); for (int k = 0; k < c; k++) s.add (nn[k]); }
				pitch[i] = nearestTone (L >= 0 ? L : band, s.count () > 0 ? s : pcs, band);
				continue;
			}
			int beatStartSlice = (vn[i].start / beatSlices) * beatSlices;
			pitch[i] = passingBetween (L, R, pcs, scalePcs, band, starts.contains (beatStartSlice), rng);
		}
		if (ornaments > 0)
			for (int i = 0; i < nc; i++)
			{
				if (!ok[i] || cls[i] > 1 || pitch[i] < 0) continue;
				int k = i + 1;
				if (k >= nc || !ok[k] || pitch[k] < 0 || cls[k] < cls[i]) continue;
				if (vn[k].start > vn[i].start + vn[i].length) continue;
				if (rng.nextDouble () >= (ornaments / 100.0) * (cls[i] == 0 ? 0.55 : 0.30)) continue;
				int chordTone = pitch[i], prevP = leftOf (i);
				int sus = prevP >= 0 ? stepDownChordTone (prevP, chords[i]) : -1;
				if (sus >= 0 && iabs (prevP - sus) <= 2) { pitch[i] = prevP; pitch[k] = sus; }
				else { int appo = scaleStepAbove (chordTone, scalePcs); if (appo >= 0) { pitch[i] = appo; pitch[k] = chordTone; } }
			}
		Vec<Gen> gen;
		int prevEmit = carryPrev;
		for (int i = 0; i < nc; i++)
		{
			if (!ok[i] || pitch[i] == UNPLACED || pitch[i] < 0) continue;
			int midi = pitch[i];
			if (prevEmit >= 0 && continuity > 0)
			{
				int maxLeap = 12 - continuity * 11 / 100;
				const PcSet &cap = (cls[i] == 3 || chords[i].count () == 0) ? scalePcs : chords[i];
				if (iabs (midi - prevEmit) > maxLeap)
				{
					int near = nearestToneWithinLeap (prevEmit, cap, maxLeap);
					if (near < 0) near = nearestTone (prevEmit, cap, bandOf[i]);
					if (near >= 0) midi = near;
				}
			}
			prevEmit = midi;
			Gen g = { vn[i].start, vn[i].length, midi }; gen.push (g);
		}
		if (variation == 3)
		{
			for (int i = 0; i < gen.size (); i++) { int ns = totalSlices - (gen[i].start + gen[i].len); gen[i].start = ns < 0 ? 0 : ns; }
			gen.sort ([] (const Gen &a, const Gen &b) { return a.start < b.start; });
		}
		if (variation == 4 && gen.size () > 0)
		{
			int pivot = gen[0].midi;
			for (int i = 0; i < gen.size (); i++) gen[i].midi = iclamp (2 * pivot - gen[i].midi, 40, 90);
		}
		if (variation == 3 || variation == 4)
			for (int i = 0; i < gen.size (); i++)
			{
				double absBeat = startBeat + gen[i].start / (double) spq;
				int root, quality;
				if (!chordAt (p, absBeat, &root, &quality, 0)) continue;
				PcSet cp = chordPcs (root, quality);
				double phased = absBeat - pickup;
				bool strong = fabs (phased - iround (phased)) < 1e-6;
				int snapped = nearestTone (gen[i].midi, strong ? cp : scalePcs, gen[i].midi);
				if (snapped >= 0) gen[i].midi = snapped;
			}
		for (int i = 0; i < gen.size (); i++)
		{
			int row = gen[i].midi - 12;
			if (row >= 0 && row < 96) { RiffNote rn (row, gen[i].start, gen[i].len); rn.voice = v; out.notes.push (rn); }
		}
		if (carry && gen.size () > 0)
		{
			int lastMidi = gen[0].midi, lastStart = gen[0].start;
			for (int i = 0; i < gen.size (); i++) if (gen[i].start >= lastStart) { lastStart = gen[i].start; lastMidi = gen[i].midi; }
			carry[v] = lastMidi;
			if (hasFortCarry) { carry[3 + v] = lastFortMidi; carry[6 + v] = lastFortRoot; }
		}
	}
	out.notes.sort ([] (const RiffNote &a, const RiffNote &b) { return a.start != b.start ? a.start < b.start : a.note < b.note; });
	return out;
}

// ---- melodic rings --------------------------------------------------------------------------------------------
static long long gcdl (long long a, long long b) { while (b != 0) { long long t = a % b; a = b; b = t; } return a < 0 ? -a : a; }
static long long lcml (long long a, long long b) { long long g = gcdl (a, b); return g == 0 ? 0 : a / g * b; }
static int melodicPolyCycleBeats (const MelodicPolyModule &m) { return imax (1, m.beats > 0 ? m.beats : m.beatsPerBar); }
static int melodicPolySpq (const MelodicPolyModule &m)
{
	long long lcm = 1;
	for (int i = 0; i < m.layers.size (); i++)
	{
		if (m.layers[i].muted) continue;
		lcm = lcml (lcm, imax (1, m.layers[i].steps));
		if (lcm > 4800) return 24;
	}
	long long spq = lcm / gcdl (melodicPolyCycleBeats (m), lcm);
	return (int) (spq < 1 ? 1 : spq);
}
double melodicPolyTotalBeats (const MelodicPolyModule &m) { return melodicPolyCycleBeats (m) * (double) imax (1, m.repeats); }

Riff generateMelodicPoly (const MelodicPolyModule &m, const Project &p, double startBeat, int *carry)
{
	int spq = melodicPolySpq (m), beats = melodicPolyCycleBeats (m);
	int cycleSlices = beats * spq, repeats = imax (1, m.repeats), total = cycleSlices * repeats;
	MelodicLineModule sk;
	int maxVoice = 0;
	Vec<bool> pat;
	for (int li = 0; li < m.layers.size (); li++)
	{
		const EuclidVoice &v = m.layers[li];
		if (v.voice < 0 || v.voice >= MelodicLineModule::MaxVoices) continue;
		maxVoice = imax (maxVoice, v.voice);
		if (v.muted) continue;
		effectivePattern (v.customMode, v.customHits, v.hits, v.steps, v.rotation, pat);
		int steps = pat.size (); if (steps == 0) continue;
		Vec<int> on;
		for (int i = 0; i < steps; i++) if (pat[i]) { long long num = (long long) i * beats * spq; on.push ((int) ((num + steps / 2) / steps)); }
		if (on.size () == 0) continue;
		on.sort ([] (int a, int b) { return a < b; });
		for (int rep = 0; rep < repeats; rep++)
			for (int idx = 0; idx < on.size (); idx++)
			{
				int s = rep * cycleSlices + on[idx];
				int nextS = idx + 1 < on.size () ? rep * cycleSlices + on[idx + 1] : rep * cycleSlices + cycleSlices;
				int len = v.legato ? (nextS - s) : imax (1, cycleSlices / steps);
				if (s < 0 || s >= total) continue;
				sk.rhythm.notes.push (RiffNote (v.voice, s, len));
			}
	}
	sk.rhythm.notes.sort ([] (const RiffNote &a, const RiffNote &b) { return a.start != b.start ? a.start < b.start : a.note < b.note; });
	sk.beatsPerBar = imax (1, total / spq);
	sk.voiceCount = iclamp (maxVoice + 1, 1, MelodicLineModule::MaxVoices);
	sk.rhythm.setNotes (sk.rhythm.notes, spq, total);
	Riff r = generateLine (sk, p, startBeat, carry);
	int shift[3] = { 0, 0, 0 }; bool any = false;
	for (int li = 0; li < m.layers.size (); li++)
		if (m.layers[li].voice >= 0 && m.layers[li].voice < 3 && m.layers[li].octave != 0) { shift[m.layers[li].voice] = m.layers[li].octave * 12; any = true; }
	if (any)
		for (int i = 0; i < r.notes.size (); i++)
		{
			int vv = r.notes[i].voice, d = (vv >= 0 && vv < 3) ? shift[vv] : 0;
			int row = r.notes[i].note + d;
			while (row < 0) row += 12;
			while (row > 95) row -= 12;
			r.notes[i].note = row;
		}
	return r;
}

// ---- any module -------------------------------------------------------------------------------------------------
Riff renderModule (const Module &m, const Project &p, double startBeat, int *carry, Riff *cell)
{
	if (cell) { cell->notes.clear (); cell->lengthSlices = 0; }
	switch (m.kind)
	{
	case M_PLAYRIFF:
	{
		const Riff *r = p.riffById (((const PlayRiffModule &) m).riffId);
		if (r) return *r;
		Riff e; e.lengthSlices = 4 * e.spq; return e;
	}
	case M_PATTERN:
	{
		const PatternModule &pg = (const PatternModule &) m;
		if (cell && pg.hasMelodic ()) *cell = generateMelodic (pg, p.key);
		return generatePattern (pg);
	}
	case M_DRUMKIT: return generateDrums ((const DrumModule &) m);
	case M_POLYDRUM: return generatePolyDrum ((const PolyDrumModule &) m);
	case M_CADENCE: return generateCadence ((const CadenceModule &) m);
	case M_POLYCHORD: return generatePolyChord ((const PolyChordModule &) m, p, startBeat);
	case M_ARTICULATION: return generateArticulation ((const ArticulationModule &) m, p, startBeat);
	case M_MELODICLINE: return generateLine ((const MelodicLineModule &) m, p, startBeat, carry);
	case M_MELODICPOLY: return generateMelodicPoly ((const MelodicPolyModule &) m, p, startBeat, carry);
	case M_GENERATOR:
	{
		Riff r; r.spq = SPQ; r.lengthSlices = iround (((const GeneratorModule &) m).durationBeats * SPQ);
		if (g_generatorHook) g_generatorHook ((const GeneratorModule &) m, p, startBeat, r);
		return r;
	}
	}
	return Riff ();
}

double moduleBeats (const Module *m, const Project &p)
{
	if (!m) return 0;
	switch (m->kind)
	{
	case M_PLAYRIFF:
	{
		const Riff *r = p.riffById (((const PlayRiffModule *) m)->riffId);
		return (r && r->spq > 0) ? (double) r->lengthSlices / r->spq : 4;
	}
	case M_PATTERN:
	{
		const PatternModule *pg = (const PatternModule *) m;
		if (pg->style == CUSTOM_STYLE && pg->custom.slices.size () > 0 && pg->custom.spq > 0)
			return (double) pg->custom.slices.size () / pg->custom.spq * pg->repeats;
		return (double) pg->beatsPerBar * pg->repeats;
	}
	case M_DRUMKIT:
	{
		const DrumModule *dp = (const DrumModule *) m;
		if (dp->style == DRUM_CUSTOM_STYLE && dp->custom.slices.size () > 0 && dp->custom.spq > 0)
			return (double) dp->custom.slices.size () / dp->custom.spq * dp->repeats;
		return (double) dp->beatsPerBar * dp->repeats;
	}
	case M_POLYDRUM: return polyDrumTotalBeats (*(const PolyDrumModule *) m);
	case M_CADENCE: { const CadenceModule *cm = (const CadenceModule *) m; return (double) cm->beatsPerBar * imax (1, cm->chords.size ()); }
	case M_MELODICLINE: return imax (1, ((const MelodicLineModule *) m)->beatsPerBar);
	case M_MELODICPOLY: return melodicPolyTotalBeats (*(const MelodicPolyModule *) m);
	case M_POLYCHORD: return polyChordTotalBeats (*(const PolyChordModule *) m);
	case M_ARTICULATION: return articulationTotalBeats (*(const ArticulationModule *) m);
	case M_GENERATOR: return ((const GeneratorModule *) m)->durationBeats;
	}
	return 4;
}

} // namespace kt
