//
// gen_chords.cpp -- chords as notes (see gen.h): PatternGenerator.cs (the 28 accompaniment styles,
// the custom voice grid, the melodic cell, the cadence), Harmony.cs + ChordArticulation.cs (the
// chord track read at any beat, a chord articulation cut at the chord changes and voice-led).
//
#include "gen.h"
#include <stdio.h>

namespace kt {

bool g_ternary = false;

const char *const g_styleNames[STYLE_COUNT] = {
	"Block chords (held)", "Block chords (quarters)", "Block chords (eighths)", "Arpeggio up", "Arpeggio up-down",
	"Alberti (C-G-E-G)", "Jazz comping (Charleston)", "Rock (eighths)", "Pop (bass + chord)", "Blues shuffle (triplets)",
	"Arpeggio down", "Arpeggio (eighths)", "Waltz (bass-chord-chord)", "Reggae skank (off-beats)", "March (bass-chord)",
	"Tango (staccato)", "Bossa nova / Latin", "Funk (16th stabs)", "Habanera (bass)", "Ballad (held arpeggio)",
	"Country (alternating bass)", "Slow rock (12/8 triplets)", "Arpeggio: 2 eighths + quarter", "Arpeggio: 3 eighths + dotted quarter",
	"Arpeggio: 4 eighths + half", "Arpeggio: triplet + quarter", "Arpeggio: 4 eighths + quarter", "Harp (rolled arpeggio)",
	"Custom..." };
const char *const g_customVoiceNames[CUSTOM_VOICE_COUNT] = { "Bass", "1", "3", "5", "7", "1'", "9", "3'", "5'", "7'", "9'" };

int qualityIntervals (int quality, int *out);		// theory.cpp

namespace notes {
Vec<RiffNote> fromSlices (const Vec<Slice> &slices)
{
	Vec<RiffNote> out;
	int total = slices.size ();
	for (int note = 0; note < 96; note++)
	{
		int s = 0;
		while (s < total)
		{
			if (!slices[s].on (note)) { s++; continue; }
			int s0 = s;
			while (s < total && slices[s].on (note)) s++;
			out.push (RiffNote (note, s0, s - s0));
		}
	}
	out.sort ([] (const RiffNote &a, const RiffNote &b) { return a.start != b.start ? a.start < b.start : a.note < b.note; });
	return out;
}
int lengthOf (const Vec<RiffNote> &n) { int len = 0; for (int i = 0; i < n.size (); i++) if (n[i].end () > len) len = n[i].end (); return len; }
Vec<Slice> toSlices (const Vec<RiffNote> &n, int length)
{
	if (length < 1) length = 1;
	Vec<Slice> s; s.resize (length);
	for (int i = 0; i < n.size (); i++)
	{
		if (n[i].note < 0 || n[i].note >= 96) continue;
		for (int c = imax (0, n[i].start); c < n[i].end () && c < length; c++) s[c].set (n[i].note, true);
	}
	return s;
}
}

// ---- voice events --------------------------------------------------------------------------------------
struct VoiceEvent { int voice, start, len; };
static void addV (Vec<VoiceEvent> &g, int start, int len, int voice) { if (voice >= 0 && len > 0 && start >= 0) { VoiceEvent e = { voice, start, len < 1 ? 1 : len }; g.push (e); } }
static void addAll (Vec<VoiceEvent> &g, int start, int len, int vc) { for (int v = 0; v < vc; v++) addV (g, start, len, v); }

static int albertiIdx (int i, int vc)
{
	int third = imin (1, vc - 1), fifth = imin (2, vc - 1);
	int ext = imin (3, vc - 1), ext2 = imin (4, vc - 1);
	int last = (vc >= 5 && (i / 4) % 2 == 1) ? ext2 : ext;
	int pat[4] = { 0, fifth, third, last };
	return pat[i % 4];
}

static void arpHeld (Vec<VoiceEvent> &g, int barSlices, int vc, int nNotes, int noteDur, int heldLen, int heldMode, int climbMode, int heldVoice, bool halve, int cellOffset)
{
	if (halve) { noteDur = imax (1, noteDur / 2); heldLen = imax (1, heldLen / 2); }
	int cell = nNotes * imax (1, noteDur) + imax (1, heldLen);
	if (cell < 1) return;
	int cellIdx = cellOffset;
	for (int c0 = 0; c0 < barSlices; c0 += cell, cellIdx++)
	{
		int vlStart = (climbMode == 3 && heldVoice >= 0) ? imax (0, imin (heldVoice, vc - 1)) : -1;
		bool vlUp = vlStart <= (vc - 1) / 2;
		int pat = climbMode == 3 ? imod (cellIdx, 3) : (climbMode == 1 ? 1 : climbMode == 2 ? 2 : 0);
		for (int i = 0; i < nNotes; i++)
		{
			int pos = c0 + i * noteDur;
			if (pos >= barSlices) break;
			int v;
			if (vlStart >= 0) v = vlUp ? (vlStart + i) % vc : imod (vlStart - i, vc);
			else v = pat == 2 ? albertiIdx (i, vc) : pat == 1 ? imod (vc - 1 - (i % vc), vc) : (i % vc);
			addV (g, pos, imin (noteDur, barSlices - pos), v);
		}
		int hs = c0 + nNotes * noteDur;
		if (hs >= barSlices) continue;
		int hl = imin (heldLen, barSlices - hs);
		switch (heldMode)
		{
		case 1: addAll (g, hs, hl, vc); break;
		case 2: addV (g, hs, hl, 0); if (vc > 2) addV (g, hs, hl, 2); break;
		case 3: addV (g, hs, hl, 0); if (vc > 1) addV (g, hs, hl, 1); break;
		default: addV (g, hs, hl, (heldVoice >= 0 && heldVoice < vc) ? heldVoice : vc - 1); break;
		}
	}
}

static Vec<VoiceEvent> buildVoiceEvents (int style, int beats, int vc, int heldMode, int climbMode, int heldVoice, bool halve, int cellOffset, bool ternary)
{
	int q = SPQ, half = q / 2, six = q / 4;
	int sub = ternary ? 3 : 2;
	int eighth = ternary ? q / 3 : q / 2;
	int sixt = ternary ? q / 6 : q / 4;
	if (vc < 1) vc = 1;
	Vec<VoiceEvent> g;
	switch (style)
	{
	case 0: addAll (g, 0, beats * q, vc); break;
	case 1: for (int b = 0; b < beats; b++) addAll (g, b * q, q, vc); break;
	case 2: for (int e = 0; e < beats * sub; e++) addAll (g, e * eighth, eighth, vc); break;
	case 3: for (int b = 0; b < beats; b++) addV (g, b * q, q, b % vc); break;
	case 4:
	{
		Vec<int> seq;
		if (vc <= 1) seq.push (0);
		else { for (int i = 0; i < vc; i++) seq.push (i); for (int i = vc - 2; i >= 1; i--) seq.push (i); }
		for (int b = 0; b < beats; b++) addV (g, b * q, q, seq[b % seq.size ()]);
	} break;
	case 5: for (int e = 0; e < beats * sub; e++) addV (g, e * eighth, eighth, albertiIdx (e, vc)); break;
	case 6:
		for (int u = 0; u * 2 < beats; u++) { int c = u * 2 * q; addAll (g, c, q + half, vc); if (c + q + half < beats * q) addAll (g, c + q + half, half, vc); }
		if (beats % 2 == 1) addAll (g, (beats - 1) * q, q, vc);
		break;
	case 7: for (int e = 0; e < beats * sub; e++) addAll (g, e * eighth, eighth, vc); break;
	case 8: for (int b = 0; b < beats; b++) { if (b % 2 == 0) addV (g, b * q, q, 0); else addAll (g, b * q, q, vc); } break;
	case 9: { int t = 2 * q / 3; for (int b = 0; b < beats; b++) { addAll (g, b * q, t, vc); addAll (g, b * q + t, q - t, vc); } } break;
	case 10: for (int b = 0; b < beats; b++) addV (g, b * q, q, imod (vc - 1 - (b % vc), vc)); break;
	case 11: for (int e = 0; e < beats * sub; e++) addV (g, e * eighth, eighth, e % vc); break;
	case 12: for (int b = 0; b < beats; b++) { if (b == 0) addV (g, 0, q, 0); else addAll (g, b * q, q, vc); } break;
	case 13: for (int b = 0; b < beats; b++) addAll (g, b * q + half, half, vc); break;
	case 14: for (int b = 0; b < beats; b++) { addV (g, b * q, half, 0); addAll (g, b * q + half, half, vc); } break;
	case 15: for (int b = 0; b < beats; b++) addAll (g, b * q, half, vc); break;
	case 16:
		for (int b = 0; b < beats; b += 2) addV (g, b * q, q, 0);
		addAll (g, 0, half, vc);
		for (int b = 0; b < beats; b++) addAll (g, b * q + half, half, vc);
		break;
	case 17: for (int b = 0; b < beats; b++) { addAll (g, b * q, six, vc); addAll (g, b * q + 2 * six, six, vc); addAll (g, b * q + 3 * six, six, vc); } break;
	case 18:
		for (int b = 0; b + 1 < imax (2, beats); b += 2)
		{
			int c = b * q, d8 = q / 2 + q / 4;
			addV (g, c, d8, 0); addV (g, c + d8, six, 0); addV (g, c + q, half, 0); addV (g, c + q + half, half, 0);
			addAll (g, c, half, vc);
		}
		if (beats % 2 == 1) addAll (g, (beats - 1) * q, q, vc);
		break;
	case 19: { int span = beats * q; for (int v = 0; v < vc; v++) { int start = (int) ((long long) v * span / vc); addV (g, start, span - start, v); } } break;
	case 20: { int fifth = vc >= 3 ? vc / 2 : (vc - 1); for (int b = 0; b < beats; b++) { if (b % 2 == 0) addV (g, b * q, q, (b % 4 == 0) ? 0 : fifth); else addAll (g, b * q, half, vc); } } break;
	case 21: { int t = q / 3; for (int b = 0; b < beats; b++) for (int k = 0; k < 3; k++) addV (g, b * q + k * t, t, (b * 3 + k) % vc); } break;
	case 22: arpHeld (g, beats * q, vc, 2, eighth, q, heldMode, climbMode, heldVoice, halve, cellOffset); break;
	case 23: arpHeld (g, beats * q, vc, 3, eighth, q + eighth, heldMode, climbMode, heldVoice, halve, cellOffset); break;
	case 24: arpHeld (g, beats * q, vc, 4, eighth, 2 * q, heldMode, climbMode, heldVoice, halve, cellOffset); break;
	case 25: arpHeld (g, beats * q, vc, 3, q / 3, q, heldMode, climbMode, heldVoice, halve, cellOffset); break;
	case 26: arpHeld (g, beats * q, vc, 4, eighth, q, heldMode, climbMode, heldVoice, halve, cellOffset); break;
	case 27:
	{
		int dur = halve ? sixt / 2 : sixt;
		int span = imax (2, vc * 2);
		int cyc = 2 * (span - 1);
		for (int i = 0, n = (beats * q) / dur; i < n; i++) { int p = i % cyc; addV (g, i * dur, dur, p < span ? p : cyc - p); }
	} break;
	default: addAll (g, 0, beats * q, vc); break;
	}
	return g;
}

// ---- the custom voice grid -----------------------------------------------------------------------------------
enum { SKIP_VOICE = -2147483647 - 1 };
static const int s_degreeGuide[5] = { 0, 4, 7, 10, 14 };
static const int s_customVoiceMap[10][2] = { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 1, 0 }, { 0, 4 }, { 1, 1 }, { 1, 2 }, { 1, 3 }, { 1, 4 } };

static int customVoiceFor (int oct, int deg)
{
	for (int i = 0; i < 10; i++) if (s_customVoiceMap[i][0] == oct && s_customVoiceMap[i][1] == deg) return i + 1;
	return -1;
}

static int customVoiceNote (const int *chord, int cn, int rootMidi, int v)
{
	if (v == 0) return rootMidi - 12;
	if (v - 1 >= 10) return SKIP_VOICE;
	int oct = s_customVoiceMap[v - 1][0], deg = s_customVoiceMap[v - 1][1];
	if (deg < cn) return chord[deg] + 12 * oct;
	if (cn == 0) return SKIP_VOICE;
	int target = rootMidi + s_degreeGuide[imin (deg, 4)] + 12 * oct;
	int best = SKIP_VOICE, bestDist = 0x7fffffff;
	for (int i = 0; i < cn; i++)
		for (int k = -1; k <= 2; k++)
		{
			int p = chord[i] + 12 * k;
			int dist = iabs (p - target);
			if (dist < bestDist || (dist == bestDist && p > best)) { bestDist = dist; best = p; }
		}
	return best;
}

Vec<Slice> voiceBarForCustom (int style, int beats, int chordLen)
{
	chordLen = imax (1, chordLen);
	beats = imax (1, beats);
	Vec<VoiceEvent> ev = buildVoiceEvents (style, beats, chordLen * 2, 0, 0, -1, false, 0, false);
	Vec<Slice> dst; dst.resize (beats * SPQ);
	for (int i = 0; i < ev.size (); i++)
	{
		int len = ev[i].len > 1 ? ev[i].len - 1 : ev[i].len;		// a display gap between same-voice notes
		int row = customVoiceFor (ev[i].voice / chordLen, ev[i].voice % chordLen);
		if (row < 1 || row >= CUSTOM_VOICE_COUNT) continue;
		for (int s = imax (0, ev[i].start); s < ev[i].start + len && s < dst.size (); s++) dst[s].set (row, true);
	}
	return dst;
}

Vec<RiffNote> voiceNotesForCustom (int style, int beats, int chordLen, int spb)
{
	chordLen = imax (1, chordLen);
	beats = imax (1, beats); spb = imax (1, spb);
	Vec<VoiceEvent> ev = buildVoiceEvents (style, beats, chordLen * 2, 0, 0, -1, false, 0, false);
	Vec<RiffNote> out;
	for (int i = 0; i < ev.size (); i++)
	{
		int row = customVoiceFor (ev[i].voice / chordLen, ev[i].voice % chordLen);
		if (row < 1 || row >= CUSTOM_VOICE_COUNT) continue;
		int s = iround ((double) ev[i].start * spb / SPQ), e = iround ((double) (ev[i].start + ev[i].len) * spb / SPQ);
		if (e <= s) e = s + 1;
		out.push (RiffNote (row, s, e - s));
	}
	return out;
}

// ---- a chord -------------------------------------------------------------------------------------------------------
static int rootMidiOf (int root, int octave) { return imod (root, 12) + 12 * (octave + 1); }

Riff generatePattern (const PatternModule &m)
{
	int repeats = imax (1, m.repeats);
	int chord[16]; int cn = chordNotes (m.root, m.octave, m.quality, m.inversion, m.openVoicing, chord);
	int rootMidi = rootMidiOf (m.root, m.octave);
	Riff r;
	char name[48]; snprintf (name, sizeof name, "%s %s", g_rootNames[imod (m.root, 12)], g_qualityNames[iclamp (m.quality, 0, QUALITY_COUNT - 1)]);
	r.name = name;

	bool custom = m.style == CUSTOM_STYLE && m.custom.slices.size () > 0;
	if (custom)
	{
		int barSpq = m.custom.spq > 0 ? m.custom.spq : SPQ;
		int barSlices = m.custom.slices.size ();
		if (m.custom.notes.size () > 0)
		{
			for (int bar = 0; bar < repeats; bar++)
			{
				int off = bar * barSlices;
				for (int i = 0; i < m.custom.notes.size (); i++)
				{
					const RiffNote &mn = m.custom.notes[i];
					if (mn.note < 0 || mn.note >= CUSTOM_VOICE_COUNT) continue;
					int mv = customVoiceNote (chord, cn, rootMidi, mn.note);
					if (mv == SKIP_VOICE) continue;
					int row = mv - 12;
					if (row >= 0 && row < 96) r.notes.push (RiffNote (row, off + mn.start, mn.length));
				}
			}
			r.spq = barSpq; r.lengthSlices = barSlices * repeats;
			return r;
		}
		Vec<Slice> slices; slices.resize (barSlices * repeats);
		for (int bar = 0; bar < repeats; bar++)
		{
			int off = bar * barSlices;
			for (int s = 0; s < barSlices; s++)
				for (int v = 0; v < CUSTOM_VOICE_COUNT; v++)
					if (m.custom.slices[s].on (v))
					{
						int mv = customVoiceNote (chord, cn, rootMidi, v);
						if (mv == SKIP_VOICE) continue;
						int row = mv - 12;
						if (row >= 0 && row < 96) slices[off + s].set (row, true);
					}
		}
		r.notes = notes::fromSlices (slices); r.lengthSlices = slices.size (); r.spq = barSpq;
		return r;
	}

	int q = SPQ;
	int beats = imax (1, m.beatsPerBar);
	int barSlices = beats * q;
	Vec<VoiceEvent> events = buildVoiceEvents (m.style, beats, imax (1, cn), m.heldMode, m.climbMode, m.heldVoiceOverride, m.halveDurations, m.patternCellOffset, g_ternary);
	int chordLow = cn > 0 ? chord[0] : rootMidi;
	int rpc = imod (m.root, 12);
	int bassMidi = chordLow - imod (chordLow - rpc, 12);
	if (bassMidi >= chordLow) bassMidi -= 12;
	int bassRow = bassMidi - 12;
	bool addBass = m.bass && bassRow >= 0 && bassRow < 96;
	for (int bar = 0; bar < repeats; bar++)
	{
		int off = bar * barSlices;
		for (int i = 0; i < events.size (); i++)
		{
			const VoiceEvent &ev = events[i];
			int cl = imax (1, cn);
			int row = chord[ev.voice % cl] + 12 * (ev.voice / cl) - 12;
			int len = imin (ev.len, barSlices - ev.start);
			if (row >= 0 && row < 96 && len > 0) r.notes.push (RiffNote (row, off + ev.start, len));
		}
		if (addBass)
		{
			if (m.bassPerBeat) for (int b = 0; b < beats; b++) r.notes.push (RiffNote (bassRow, off + b * q, q));
			else r.notes.push (RiffNote (bassRow, off, barSlices));
		}
	}
	r.lengthSlices = barSlices * repeats; r.spq = q;
	return r;
}

Riff generateCadence (const CadenceModule &m)
{
	int barSlices = imax (1, m.beatsPerBar) * SPQ;
	Riff out; out.name = "Cadence"; out.spq = SPQ;
	for (int i = 0; i < m.chords.size (); i++)
	{
		const CadenceChord &c = m.chords[i];
		PatternModule pg;
		pg.root = c.root; pg.quality = c.quality; pg.inversion = c.inversion;
		pg.octave = m.octave + c.octaveShift;
		pg.style = m.style; pg.bass = m.bass; pg.bassPerBeat = m.bassPerBeat; pg.heldMode = m.heldMode; pg.climbMode = m.climbMode;
		pg.halveDurations = m.halveDurations; pg.heldVoiceOverride = c.heldVoice; pg.patternCellOffset = i;
		pg.beatsPerBar = imax (1, m.beatsPerBar); pg.repeats = 1;
		pg.custom = m.custom; pg.openVoicing = m.openVoicing;
		Riff r = generatePattern (pg);
		int off = i * barSlices;
		for (int j = 0; j < r.notes.size (); j++) out.notes.push (RiffNote (r.notes[j].note, off + r.notes[j].start, r.notes[j].length));
	}
	out.lengthSlices = barSlices * imax (1, m.chords.size ());
	return out;
}

// ---- the melodic cell --------------------------------------------------------------------------------------------
static int melodicPitch (const int *scale, int tonicPc_, int anchorPc, int melodicOctave, int row, const int *civ, int ncv)
{
	int degree = imod (row, 7), oct = row / 7;
	anchorPc = imod (anchorPc, 12); tonicPc_ = imod (tonicPc_, 12);
	int anchorMidi = 12 * (melodicOctave + 1) + anchorPc;
	int chordIdx = degree / 2;
	if (degree % 2 == 0 && civ && chordIdx < ncv) return iclamp (anchorMidi + civ[chordIdx] + oct * 12, 0, 127);
	int idx = 0;
	for (int i = 0; i < 7; i++) if (imod (tonicPc_ + scale[i], 12) == anchorPc) { idx = i; break; }
	int pos = idx + degree + 7 * oct;
	int delta = (scale[pos % 7] + 12 * (pos / 7)) - scale[idx];
	return iclamp (anchorMidi + delta, 0, 127);
}

Riff generateMelodic (const PatternModule &m, const Key &key)
{
	Riff out; out.name = "Melody";
	if (m.melodic.notes.size () == 0) { out.notes.clear (); out.lengthSlices = 0; return out; }
	const int *scale = modeScale (effectiveMode (key));
	int tpc = tonicPc (key);
	int rootPc = imod (m.root, 12), anchorPc = rootPc;
	int iv[8]; int ni = qualityIntervals (m.quality, iv);
	if (m.melodicAnchor == 1) anchorPc = (rootPc + iv[m.inversion % ni]) % 12;
	int anchorInterval = m.melodicAnchor == 1 ? iv[m.inversion % ni] : 0;
	int civ[8];
	for (int i = 0; i < ni; i++) civ[i] = imod (iv[i] - anchorInterval, 12);
	for (int i = 1; i < ni; i++) { int v = civ[i], j = i; while (j > 0 && civ[j - 1] > v) { civ[j] = civ[j - 1]; j--; } civ[j] = v; }
	int melSpq = m.melodic.spq > 0 ? m.melodic.spq : SPQ;
	int barSlices = imax (1, m.beatsPerBar) * melSpq;
	int repeats = imax (1, m.repeats);
	for (int bar = 0; bar < repeats; bar++)
	{
		int off = bar * barSlices;
		for (int i = 0; i < m.melodic.notes.size (); i++)
		{
			const RiffNote &mn = m.melodic.notes[i];
			if (mn.note < 0 || mn.note >= MELODIC_ROW_COUNT) continue;
			int row = melodicPitch (scale, tpc, anchorPc, m.melodicOctave, mn.note, civ, ni) - 12;
			if (row >= 0 && row < 96) out.notes.push (RiffNote (row, off + mn.start, mn.length));
		}
	}
	out.lengthSlices = barSlices * repeats; out.spq = melSpq;
	return out;
}

// ---- harmony: which chord sounds at a beat -----------------------------------------------------------------------------
static bool covers (double beat, double start, double len) { return beat >= start - 1e-9 && beat < start + len - 1e-9; }

bool polyChordAt (const PolyChordModule &m, double localBeat, const PolyChordItem **item, double *itemStart)
{
	*item = 0; *itemStart = 0;
	if (m.chords.size () == 0) return false;
	double cur = 0;
	for (int i = 0; i < m.chords.size (); i++)
	{
		double len = imax (1, m.chords[i].beats);
		if (localBeat < cur + len - 1e-9) { *item = &m.chords[i]; *itemStart = cur; return true; }
		cur += len;
	}
	*item = &m.chords.back (); *itemStart = cur - imax (1, m.chords.back ().beats);
	return true;
}

static bool walkChordTrack (const Project &p, const Track &tr, double beat, int *root, int *quality, int *inversion)
{
	double cursor = 0;
	for (int i = 0; i < tr.items.size (); i++)
	{
		const Item &it = tr.items[i];
		cursor += it.silenceBefore;
		double len = p.itemLength (it);
		const Module *m = it.module;
		if (m && m->kind == M_PATTERN && covers (beat, cursor, len))
		{
			const PatternModule *pg = (const PatternModule *) m;
			*root = pg->root; *quality = pg->quality; *inversion = pg->inversion; return true;
		}
		if (m && m->kind == M_POLYCHORD && covers (beat, cursor, len))
		{
			const PolyChordItem *pi; double st;
			if (polyChordAt (*(const PolyChordModule *) m, beat - cursor, &pi, &st)) { *root = pi->root; *quality = pi->quality; *inversion = pi->inversion; return true; }
		}
		if (m && m->kind == M_CADENCE && covers (beat, cursor, len))
		{
			const CadenceModule *cm = (const CadenceModule *) m;
			if (cm->chords.size () > 0)
			{
				int cellBeats = imax (1, cm->beatsPerBar);
				int idx = ifloor ((beat - cursor) / cellBeats + 1e-9);
				idx = iclamp (idx, 0, cm->chords.size () - 1);
				*root = cm->chords[idx].root; *quality = cm->chords[idx].quality; *inversion = cm->chords[idx].inversion;
				return true;
			}
		}
		cursor += len;
	}
	return false;
}

bool chordAt (const Project &p, double beat, int *root, int *quality, int *inversion)
{
	int r = 0, q = 0, inv = 0;
	bool ok = false;
	double pickup = p.pickupBeats > 0 ? p.pickupBeats : 0;
	if (p.arrangement && p.arrangement->chords.size () > 0)
	{
		const Arrangement &a = *p.arrangement;
		int spq = imax (1, a.slicesPerQuarter);
		double cellBeats = dmax (1e-6, a.chordSlices / (double) spq);
		int idx = ifloor ((beat - pickup) / cellBeats + 1e-9);
		idx = iclamp (idx, 0, a.chords.size () - 1);
		r = a.chords[idx].root; q = a.chords[idx].quality; ok = true;
	}
	else
		for (int t = 0; t < p.tracks.size () && !ok; t++) ok = walkChordTrack (p, p.tracks[t], beat, &r, &q, &inv);
	if (root) *root = r;
	if (quality) *quality = q;
	if (inversion) *inversion = inv;
	return ok;
}

static void addSeg (Vec<ChordSeg> &list, double s, double e, double from, double to, int root, int quality, int inversion, bool chordOpen = false, int chordVl = 0)
{
	double cs = dmax (s, from), ce = dmin (e, to);
	if (ce - cs <= 1e-6) return;
	ChordSeg g = { cs, ce - cs, root, quality, inversion, chordOpen, chordVl };
	list.push (g);
}

Vec<ChordSeg> segments (const Project &p, double from, double length)
{
	Vec<ChordSeg> list;
	double to = from + length;
	if (length <= 0) return list;
	if (p.arrangement && p.arrangement->chords.size () > 0)
	{
		const Arrangement &a = *p.arrangement;
		double pickup = p.pickupBeats > 0 ? p.pickupBeats : 0;
		int aspq = imax (1, a.slicesPerQuarter);
		double cell = dmax (1e-6, a.chordSlices / (double) aspq);
		int first = ifloor ((from - pickup) / cell + 1e-9);
		for (int i = imax (0, first); i < a.chords.size (); i++)
		{
			double cs = pickup + i * cell, ce = cs + cell;
			if (ce <= from + 1e-9) continue;
			if (cs >= to - 1e-9) break;
			addSeg (list, cs, ce, from, to, a.chords[i].root, a.chords[i].quality, 0);
		}
		return list;
	}
	bool found = false;
	for (int pass = 0; pass < 2 && !found; pass++)
		for (int t = 0; t < p.tracks.size (); t++)
		{
			const Track &tr = p.tracks[t];
			if ((tr.type == TRACK_CHORD) != (pass == 0)) continue;
			double cursor = 0; bool any = false;
			for (int i = 0; i < tr.items.size (); i++)
			{
				const Item &it = tr.items[i];
				cursor += it.silenceBefore;
				double len = p.itemLength (it);
				double s = cursor, e = cursor + len;
				cursor = e;
				if (e <= from + 1e-9 || s >= to - 1e-9) continue;
				const Module *m = it.module;
				if (!m) continue;
				if (m->kind == M_PATTERN)
				{
					const PatternModule *pg = (const PatternModule *) m;
					addSeg (list, s, e, from, to, pg->root, pg->quality, pg->inversion, pg->openVoicing, pg->voiceLeadMode); any = true;
				}
				else if (m->kind == M_CADENCE)
				{
					const CadenceModule *cm = (const CadenceModule *) m;
					double cell = imax (1, cm->beatsPerBar);
					for (int j = 0; j < cm->chords.size (); j++)
					{
						double cs = s + j * cell, ce = cs + cell;
						if (ce <= from + 1e-9 || cs >= to - 1e-9) continue;
						addSeg (list, cs, ce, from, to, cm->chords[j].root, cm->chords[j].quality, cm->chords[j].inversion); any = true;
					}
				}
				else if (m->kind == M_POLYCHORD)
				{
					const PolyChordModule *pc = (const PolyChordModule *) m;
					double tt = dmax (s, from);
					while (tt < dmin (e, to) - 1e-9)
					{
						const PolyChordItem *pi; double itStart;
						if (!polyChordAt (*pc, tt - s, &pi, &itStart)) break;
						double cs = s + itStart, ce = cs + dmax (0.25, pi->beats);
						if (ce <= tt + 1e-9) break;
						addSeg (list, cs, ce, from, to, pi->root, pi->quality, pi->inversion); any = true;
						tt = ce;
					}
				}
			}
			if (any) { found = true; break; }
		}
	list.sort ([] (const ChordSeg &a, const ChordSeg &b) { return a.start < b.start; });
	return list;
}

// ---- chord articulation --------------------------------------------------------------------------------------------
enum { OPEN_NO = 0, OPEN_YES = 1, OPEN_FROM_CHORD = 2 };
enum { VL_NONE = 0, VL_FIXED_OR_FROM_CHORD = 4 };

double articulationTotalBeats (const ArticulationModule &m) { return dmax (0.25, m.lengthBeats > 0 ? m.lengthBeats : m.beats); }
static double articulationCellBeats (const ArticulationModule &m) { return dmax (0.25, m.beats); }

struct InvOctPair { int inv, oct; };

static Vec<InvOctPair> voicingChain (const ArticulationModule &m, const Vec<ChordSeg> &run)
{
	Vec<InvOctPair> chain;
	int prev[16]; int np = 0; bool havePrev = false;
	int dir = m.voiceLeadDirection == 1 ? 1 : m.voiceLeadDirection == 2 ? -1 : 0;
	for (int i = 0; i < run.size (); i++)
	{
		const ChordSeg &s = run[i];
		int inv, oct;
		int vl = m.voiceLeadMode == VL_FIXED_OR_FROM_CHORD ? s.chordVoiceLead : m.voiceLeadMode;
		if (vl == VL_FIXED_OR_FROM_CHORD) { inv = s.inversion; oct = m.octave; }
		else if (vl > VL_NONE && havePrev)
		{
			InvOct v = voiceLeadStep (prev, np, s.root, s.quality, m.octave, vl - 1, dir);
			inv = v.inversion; oct = v.octave;
		}
		else { inv = m.inversion; oct = m.octave; }
		InvOctPair pr = { inv, oct }; chain.push (pr);
		np = chordNotes (s.root, oct, s.quality, inv, false, prev); havePrev = true;
	}
	return chain;
}

Riff generateArticulation (const ArticulationModule &m, const Project &p, double moduleStartBeat)
{
	int spq = SPQ;
	double total = articulationTotalBeats (m);
	int totalSlices = imax (1, iround (total * spq));
	Riff out; out.name = "Articulation"; out.spq = spq; out.lengthSlices = totalSlices;
	double cell = articulationCellBeats (m);
	int cellCount = imax (1, iceil (total / cell - 1e-9));
	int genBeats = imax (1, iceil (cell - 1e-9));

	Vec<ChordSeg> all = segments (p, moduleStartBeat, total), run;
	{
		int lr = -2147483647 - 1, lq = lr;
		for (int i = 0; i < all.size (); i++) { if (all[i].root == lr && all[i].quality == lq) continue; run.push (all[i]); lr = all[i].root; lq = all[i].quality; }
	}
	Vec<InvOctPair> chain = voicingChain (m, run);

	int lastRoot = -2147483647 - 1, lastQuality = lastRoot, chainIdx = -1;
	int curInv = m.inversion, curOct = m.octave;
	for (int c = 0; c < cellCount; c++)
	{
		double cellStart = c * cell;
		double cellLen = dmin (cell, total - cellStart);
		if (cellLen <= 1e-6) break;
		Vec<ChordSeg> segs = segments (p, moduleStartBeat + cellStart, cellLen);
		for (int si = 0; si < segs.size (); si++)
		{
			const ChordSeg &s = segs[si];
			double segBeats = s.len;
			if (segBeats <= 1e-6) continue;
			if (s.root != lastRoot || s.quality != lastQuality)
			{
				chainIdx++;
				if (chainIdx >= 0 && chainIdx < chain.size ()) { curInv = chain[chainIdx].inv; curOct = chain[chainIdx].oct; }
				lastRoot = s.root; lastQuality = s.quality;
			}
			bool open = m.openVoicingMode == OPEN_FROM_CHORD ? s.chordOpen : (m.openVoicingMode == OPEN_YES || (m.openVoicingMode == OPEN_NO && m.openVoicing));
			PatternModule pg;
			pg.root = s.root; pg.quality = s.quality; pg.inversion = curInv; pg.octave = curOct;
			pg.style = m.style; pg.bass = m.bass; pg.bassPerBeat = m.bassPerBeat;
			pg.heldMode = m.heldMode; pg.climbMode = m.climbMode; pg.halveDurations = m.halveDurations;
			pg.openVoicing = open; pg.patternCellOffset = c;
			pg.beatsPerBar = genBeats; pg.repeats = 1;
			pg.custom = m.custom;
			Riff r = generatePattern (pg);
			int srcSpq = r.spq > 0 ? r.spq : spq;
			double scale = (double) spq / srcSpq;
			int cellOff = iround (cellStart * spq);
			int winStart = iround ((s.start - (moduleStartBeat + cellStart)) * spq);
			int winEnd = winStart + iround (segBeats * spq);
			for (int ni = 0; ni < r.notes.size (); ni++)
			{
				const RiffNote &n = r.notes[ni];
				int nStart = iround (n.start * scale);
				int nLen = imax (1, iround (n.length * scale));
				if (nStart < winStart || nStart >= winEnd) continue;
				int len = imin (nLen, winEnd - nStart);
				int start = cellOff + nStart;
				if (len <= 0 || start >= totalSlices) continue;
				len = imin (len, totalSlices - start);
				if (len > 0) out.notes.push (RiffNote (n.note, start, len));
			}
		}
	}

	if (m.hasMelodic ())
	{
		int melGenBeats = imax (1, iceil (total - 1e-9));
		int mLastRoot = -2147483647 - 1, mLastQuality = mLastRoot, mIdx = -1;
		for (int si = 0; si < all.size (); si++)
		{
			const ChordSeg &s = all[si];
			if (s.root != mLastRoot || s.quality != mLastQuality) { mIdx++; mLastRoot = s.root; mLastQuality = s.quality; }
			int melInv = (mIdx >= 0 && mIdx < chain.size ()) ? chain[mIdx].inv : s.inversion;
			PatternModule mpg;
			mpg.root = s.root; mpg.quality = s.quality; mpg.inversion = melInv;
			mpg.melodicOctave = m.melodicOctave; mpg.melodicAnchor = m.melodicAnchor;
			mpg.melodic = m.melodic;
			mpg.beatsPerBar = melGenBeats; mpg.repeats = 1;
			Riff mel = generateMelodic (mpg, p.key);
			double mScale = (double) spq / (mel.spq > 0 ? mel.spq : spq);
			int winStart = iround ((s.start - moduleStartBeat) * spq);
			int winEnd = winStart + iround (s.len * spq);
			for (int ni = 0; ni < mel.notes.size (); ni++)
			{
				const RiffNote &n = mel.notes[ni];
				int nStart = iround (n.start * mScale);
				int nLen = imax (1, iround (n.length * mScale));
				if (nStart < winStart || nStart >= winEnd) continue;
				int len = imin (nLen, winEnd - nStart);
				if (len <= 0 || nStart >= totalSlices) continue;
				len = imin (len, totalSlices - nStart);
				if (len > 0) out.notes.push (RiffNote (n.note, nStart, len));
			}
		}
	}
	return out;
}

} // namespace kt
