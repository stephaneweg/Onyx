//
// theory.cpp -- Koton's harmony (see theory.h): MusicalMode.cs, MusicTheory.cs, ChordDegrees.cs,
// HarmonySuggest.cs, ChordModelOps.cs, ported line for line where the maths matter (same seeds, same
// tie-breaks: the same cadence comes out of the same seed as in Koton for Windows).
//
#include "theory.h"
#include <stdio.h>

namespace kt {

// ---- modes -------------------------------------------------------------------------------------------------
const char *const g_modeNames[MODE_COUNT] = {
	"Major (Ionian)", "Natural minor (Aeolian)", "Harmonic minor", "Melodic minor", "Dorian",
	"Phrygian", "Lydian", "Mixolydian", "Locrian" };
static const int s_scales[MODE_COUNT][7] = {
	{ 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 3, 5, 7, 9, 11 },
	{ 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 4, 5, 7, 9, 10 },
	{ 0, 1, 3, 5, 6, 8, 10 } };
const int *modeScale (int mode) { return s_scales[(mode >= 0 && mode < MODE_COUNT) ? mode : 0]; }
int effectiveMode (const Key &k) { return (k.fullMode >= 0 && k.fullMode < MODE_COUNT) ? k.fullMode : (k.mode == 1 ? 1 : 0); }

static const int s_letterPc[7] = { 0, 2, 4, 5, 7, 9, 11 };
int tonicPc (const Key &k) { int l = iclamp (k.tonicLetter, 0, 6); return imod (s_letterPc[l] + k.accidental, 12); }

const char *const g_rootNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const char *const g_rootNamesFlat[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };

int keyFifths (const Key &k)
{
	static const int letterFifths[7] = { 0, 2, 4, -1, 1, 3, 5 };
	int f = letterFifths[iclamp (k.tonicLetter, 0, 6)] + 7 * iclamp (k.accidental, -1, 1) - (k.mode == 1 ? 3 : 0);
	while (f > 7) f -= 12;
	while (f < -7) f += 12;
	return f;
}
const char *spellPc (int pc, const Key &k) { return keyFifths (k) < 0 ? g_rootNamesFlat[imod (pc, 12)] : g_rootNames[imod (pc, 12)]; }

void keyName (const Key &k, char *buf, int cap)
{
	static const char letters[] = "CDEFGAB";
	char t[4]; int n = 0;
	t[n++] = letters[iclamp (k.tonicLetter, 0, 6)];
	if (k.accidental > 0) t[n++] = '#'; else if (k.accidental < 0) t[n++] = 'b';
	t[n] = 0;
	int m = effectiveMode (k);
	if (m == 0) snprintf (buf, cap, "%s major", t);
	else if (m == 1) snprintf (buf, cap, "%s minor (Aeolian)", t);
	else snprintf (buf, cap, "%s %s", t, g_modeNames[m]);
}

// ---- chords ----------------------------------------------------------------------------------------------
const char *const g_qualityNames[QUALITY_COUNT] = {
	"Major", "Minor", "Diminished", "Augmented", "Sus2", "Sus4", "Maj7", "Min7", "7 (dom)", "m7b5", "dim7",
	"6", "m6", "add9", "m(add9)", "9 (dom)", "Maj9", "m9", "7b9", "7#9", "11 (dom)", "13 (dom)", "Maj7#11",
	"7sus4", "7sus2", "9sus4", "9sus2", "6sus4", "6sus2", "Maj7sus4", "Maj7sus2", "Maj9sus4", "Maj9sus2", "add9sus4",
	"7#5" };
// Koton's French names, for the files and the AI's replies ("quality": "Majeur")
static const char *const s_qualityFr[QUALITY_COUNT] = {
	"Majeur", "Mineur", "Diminué", "Augmenté", "Sus2", "Sus4", "Maj7", "Min7", "7 (dom)", "m7♭5", "dim7",
	"6", "m6", "add9", "m(add9)", "9 (dom)", "Maj9", "m9", "7♭9", "7♯9", "11 (dom)", "13 (dom)", "Maj7♯11",
	"7sus4", "7sus2", "9sus4", "9sus2", "6sus4", "6sus2", "Maj7sus4", "Maj7sus2", "Maj9sus4", "Maj9sus2", "add9sus4",
	"7♯5" };
const char *const g_qualitySymbols[QUALITY_COUNT] = {
	"", "m", "dim", "aug", "sus2", "sus4", "maj7", "m7", "7", "m7b5", "dim7",
	"6", "m6", "add9", "m(add9)", "9", "maj9", "m9", "7b9", "7#9", "11", "13", "maj7#11",
	"7sus4", "7sus2", "9sus4", "9sus2", "6sus4", "6sus2", "maj7sus4", "maj7sus2", "maj9sus4", "maj9sus2", "add9sus4",
	"7#5" };
static const int s_iv[QUALITY_COUNT][7] = {		// semitones from the root, -1 ends
	{ 0, 4, 7, -1 }, { 0, 3, 7, -1 }, { 0, 3, 6, -1 }, { 0, 4, 8, -1 }, { 0, 2, 7, -1 }, { 0, 5, 7, -1 },
	{ 0, 4, 7, 11, -1 }, { 0, 3, 7, 10, -1 }, { 0, 4, 7, 10, -1 }, { 0, 3, 6, 10, -1 }, { 0, 3, 6, 9, -1 },
	{ 0, 4, 7, 9, -1 }, { 0, 3, 7, 9, -1 }, { 0, 4, 7, 14, -1 }, { 0, 3, 7, 14, -1 }, { 0, 4, 7, 10, 14, -1 },
	{ 0, 4, 7, 11, 14, -1 }, { 0, 3, 7, 10, 14, -1 }, { 0, 4, 7, 10, 13, -1 }, { 0, 4, 7, 10, 15, -1 },
	{ 0, 4, 7, 10, 14, 17, -1 }, { 0, 4, 7, 10, 14, 21, -1 }, { 0, 4, 7, 11, 18, -1 },
	{ 0, 5, 7, 10, -1 }, { 0, 2, 7, 10, -1 }, { 0, 5, 7, 10, 14, -1 }, { 0, 2, 7, 10, 14, -1 },
	{ 0, 5, 7, 9, -1 }, { 0, 2, 7, 9, -1 }, { 0, 5, 7, 11, -1 }, { 0, 2, 7, 11, -1 }, { 0, 5, 7, 11, 14, -1 },
	{ 0, 2, 7, 11, 14, -1 }, { 0, 5, 7, 14, -1 }, { 0, 4, 8, 10, -1 } };

// a quality's intervals (the ones QualityIntervals holds), their count
int qualityIntervals (int quality, int *out)
{
	const int *iv = s_iv[iclamp (quality, 0, QUALITY_COUNT - 1)];
	int n = 0; while (n < 7 && iv[n] >= 0) { out[n] = iv[n]; n++; }
	return n;
}

// Koton's NormQual: lower case, ♭/b -> b, ♯/# -> s, letters and digits only
static void normQual (const char *s, char *out, int cap)
{
	int o = 0;
	const unsigned char *p = (const unsigned char *) s;
	while (*p && o + 1 < cap)
	{
		if (p[0] == 0xE2 && p[1] == 0x99 && p[2] == 0xAD) { out[o++] = 'b'; p += 3; continue; }	// ♭
		if (p[0] == 0xE2 && p[1] == 0x99 && p[2] == 0xAF) { out[o++] = 's'; p += 3; continue; }	// ♯
		char c = (char) *p++;
		if (c >= 'A' && c <= 'Z') c += 32;
		if (c == '#') { out[o++] = 's'; continue; }
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[o++] = c;
		else if ((unsigned char) c >= 0x80) out[o++] = c;		// accented letters (Diminué) kept as bytes
	}
	out[o] = 0;
}
int qualityIndex (const char *name)
{
	if (!name || !*name) return -1;
	char n[48], q[48]; normQual (name, n, sizeof n);
	for (int i = 0; i < QUALITY_COUNT; i++)
	{
		normQual (s_qualityFr[i], q, sizeof q); if (!strcmp (q, n)) return i;
		normQual (g_qualityNames[i], q, sizeof q); if (!strcmp (q, n)) return i;
	}
	return -1;
}

int chordNotes (int root, int octave, int quality, int inversion, bool open, int *out)
{
	int iv[8]; int n = qualityIntervals (quality, iv);
	int rootMidi = imod (root, 12) + 12 * (octave + 1);
	int notes[16]; int c = 0;
	for (int i = 0; i < n; i++) notes[c++] = rootMidi + iv[i];
	for (int k = 0; k < inversion && c > 0; k++)			// raise the bottom note an octave
	{
		int low = notes[0];
		for (int i = 0; i + 1 < c; i++) notes[i] = notes[i + 1];
		notes[c - 1] = low + 12;
	}
	for (int i = 1; i < c; i++) { int v = notes[i], j = i; while (j > 0 && notes[j - 1] > v) { notes[j] = notes[j - 1]; j--; } notes[j] = v; }
	if (open && c >= 3)
	{
		notes[1] += 12;
		for (int i = 1; i < c; i++) { int v = notes[i], j = i; while (j > 0 && notes[j - 1] > v) { notes[j] = notes[j - 1]; j--; } notes[j] = v; }
	}
	for (int i = 0; i < c; i++) out[i] = notes[i];
	return c;
}

void chordLabel (int root, int quality, const Key &k, char *buf, int cap)
{
	snprintf (buf, cap, "%s%s", spellPc (root, k), g_qualitySymbols[iclamp (quality, 0, QUALITY_COUNT - 1)]);
}

const char *const g_colourNames[5] = { "Triad", "Sixth", "7th", "9th (7+9)", "9th (add9)" };
const char *const g_suspensionNames[3] = { "None", "Sus2", "Sus4" };
const char *const g_modeOverrideNames[6] = { "Auto", "Major", "Minor", "Augmented", "Diminished", "Dominant" };

static const int s_majorPcs[7] = { 0, 2, 4, 5, 7, 9, 11 };
static const int s_minorPcs[7] = { 0, 2, 3, 5, 7, 8, 10 };
static const int s_majorQual[7] = { 0, 7, 7, 6, 8, 7, 9 };
static const int s_minorQual[7] = { 1, 9, 6, 7, 8, 6, 8 };
static const int s_majorTonic[3] = { 0, 11, 13 };
static const int s_minorTonic[3] = { 1, 12, 14 };
static const int s_domColour[3] = { 8, 15, 21 };

int degreeOf (const Key &k, int rootPc)
{
	int tonic = tonicPc (k);
	const int *pcs = k.mode == 1 ? s_minorPcs : s_majorPcs;
	int best = 0, bd = 99;
	for (int d = 0; d < 7; d++)
	{
		int dpc = (tonic + pcs[d]) % 12;
		int dist = imin (imod (dpc - rootPc, 12), imod (rootPc - dpc, 12));
		if (dist < bd) { bd = dist; best = d; }
	}
	return best;
}

static int triadQualityFromScale (const int *scale, int degree)
{
	int d = imod (degree, 7);
	int r = scale[d];
	int t = scale[(d + 2) % 7]; if (t < r) t += 12;
	int f = scale[(d + 4) % 7]; if (f < r) f += 12;
	int third = t - r, fifth = f - r;
	if (third == 3 && fifth == 6) return 2;
	if (third == 4 && fifth == 8) return 3;
	if (third == 3 && fifth == 7) return 1;
	return 0;
}

RootQ diatonicChord (const Key &k, int degree)
{
	const int *scale = modeScale (effectiveMode (k));
	int d = imod (degree, 7);
	RootQ r; r.root = (tonicPc (k) + scale[d]) % 12; r.quality = triadQualityFromScale (scale, d);
	return r;
}

RootQ diatonicChord (const Key &k, int degree, int colour, int suspension, int mode)
{
	bool minor = k.mode == 1;
	const int *scale = modeScale (effectiveMode (k));
	int d = imod (degree, 7);
	int root = (tonicPc (k) + scale[d]) % 12;
	int triad = triadQualityFromScale (scale, d);
	int seventh = (d == 0) ? (minor ? 7 : 6) : (minor ? s_minorQual : s_majorQual)[d];
	if (mode == 1) { triad = 0; seventh = 6; }
	else if (mode == 2) { triad = 1; seventh = 7; }
	else if (mode == 3) { triad = 3; seventh = 34; }
	else if (mode == 4) { triad = 2; seventh = 10; }
	else if (mode == 5) { triad = 0; seventh = 8; }
	bool maj7 = seventh == 6;
	int q;
	if (suspension == 0)
	{
		switch (colour)
		{
		case 1: q = triad == 0 ? 11 : triad == 1 ? 12 : triad; break;
		case 2: q = seventh; break;
		case 3: q = seventh == 6 ? 16 : seventh == 7 ? 17 : seventh == 8 ? 15 : seventh; break;
		case 4: q = triad == 0 ? 13 : triad == 1 ? 14 : triad; break;
		default: q = triad; break;
		}
	}
	else
	{
		bool s4 = suspension == 2;
		switch (colour)
		{
		case 1: q = s4 ? 27 : 28; break;
		case 2: q = maj7 ? (s4 ? 29 : 30) : (s4 ? 23 : 24); break;
		case 3: q = maj7 ? (s4 ? 31 : 32) : (s4 ? 25 : 26); break;
		case 4: q = s4 ? 33 : 4; break;
		default: q = s4 ? 5 : 4; break;
		}
	}
	RootQ r; r.root = root; r.quality = q;
	return r;
}

void chordShape (int quality, bool *minThird, bool *dimFifth, bool *augFifth, bool *dom7)
{
	int n[16]; int c = chordNotes (0, 4, quality, 0, false, n);
	unsigned set = 0; int b = c > 0 ? n[0] : 0;
	for (int i = 0; i < c; i++) set |= 1u << imod (n[i] - b, 12);
	bool has3 = set & (1 << 3), has4 = set & (1 << 4), has6 = set & (1 << 6), has7 = set & (1 << 7), has8 = set & (1 << 8), has10 = set & (1 << 10);
	bool mt = has3 && !has4, df = has6 && !has7;
	if (minThird) *minThird = mt;
	if (dimFifth) *dimFifth = df;
	if (augFifth) *augFifth = has8 && !has7 && !has3;
	if (dom7) *dom7 = has10 && !mt && !df;
}

int diatonicDegreeOf (const Key &k, int pc)
{
	int tonic = tonicPc (k); const int *scale = modeScale (effectiveMode (k));
	for (int d = 0; d < 7; d++) if (imod (tonic + scale[d], 12) == imod (pc, 12)) return d;
	return -1;
}
int diatonicThird (const Key &k, int degree)
{
	const int *s = modeScale (effectiveMode (k)); int d = imod (degree, 7);
	return imod (s[(d + 2) % 7] - s[d], 12);
}
bool diatonicIsDim (const Key &k, int degree)
{
	const int *s = modeScale (effectiveMode (k)); int d = imod (degree, 7);
	return imod (s[(d + 4) % 7] - s[d], 12) == 6;
}
int secondaryDominantTarget (const Key &k, int rootPc, int quality)
{
	bool minThird, dimFifth, dom7;
	chordShape (quality, &minThird, &dimFifth, 0, &dom7);
	if (minThird || dimFifth) return -1;
	int root = imod (rootPc, 12);
	int deg = diatonicDegreeOf (k, root);
	bool diatonicMajorHere = deg >= 0 && diatonicThird (k, deg) == 4;
	bool actsAsSecondary = dom7 ? deg != 4 : !diatonicMajorHere;
	if (!actsAsSecondary) return -1;
	int target = diatonicDegreeOf (k, imod (root - 7, 12));
	if (target < 0 || diatonicIsDim (k, target)) return -1;
	return target;
}
int secondaryLeadingToneTarget (const Key &k, int rootPc, int quality)
{
	bool dimFifth; chordShape (quality, 0, &dimFifth, 0, 0);
	int root = imod (rootPc, 12);
	if (!dimFifth || diatonicDegreeOf (k, root) >= 0) return -1;
	int target = diatonicDegreeOf (k, (root + 1) % 12);
	if (target < 0 || diatonicIsDim (k, target)) return -1;
	return target;
}
int secondaryDominantRoot (const Key &k, int targetDegree) { return imod (diatonicChord (k, targetDegree).root + 7, 12); }

static const char *const s_romanU[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };
static const char *const s_romanL[7] = { "i", "ii", "iii", "iv", "v", "vi", "vii" };

static void romanOfDegree (const Key &k, int deg, char *buf, int cap)
{
	snprintf (buf, cap, "%s", diatonicThird (k, deg) == 4 ? s_romanU[imod (deg, 7)] : s_romanL[imod (deg, 7)]);
}

void romanNumeral (const Key &k, int rootPc, int quality, int degreeHint, char *buf, int cap)
{
	bool minThird, dimFifth;
	chordShape (quality, &minThird, &dimFifth, 0, 0);
	int sd = secondaryDominantTarget (k, rootPc, quality);
	if (sd >= 0 && degreeHint < 0)
	{
		char t[8]; romanOfDegree (k, sd, t, sizeof t);
		snprintf (buf, cap, "V/%s", t);
		return;
	}
	int lt = secondaryLeadingToneTarget (k, rootPc, quality);
	if (lt >= 0 && degreeHint < 0)
	{
		char t[8]; romanOfDegree (k, lt, t, sizeof t);
		snprintf (buf, cap, "vii\xC2\xB0/%s", t);
		return;
	}
	int deg = degreeHint >= 0 ? degreeHint : diatonicDegreeOf (k, rootPc);
	if (deg >= 0)
	{
		const char *r = minThird || dimFifth ? s_romanL[deg] : s_romanU[deg];
		snprintf (buf, cap, "%s%s", r, dimFifth ? "\xC2\xB0" : "");
		return;
	}
	// a chromatic root: named from the major scale's degrees, flattened (bVII, bVI, bIII, bII, #IV)
	static const int deg12[12] = { 0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6 };
	static const int acc12[12] = { 0, -1, 0, -1, 0, 0, 1, 0, -1, 0, -1, 0 };
	int off = imod (rootPc - tonicPc (k), 12);
	const char *r = minThird || dimFifth ? s_romanL[deg12[off]] : s_romanU[deg12[off]];
	snprintf (buf, cap, "%s%s%s", acc12[off] < 0 ? "\xE2\x99\xAD" : acc12[off] > 0 ? "\xE2\x99\xAF" : "", r, dimFifth ? "\xC2\xB0" : "");
}

int chordFunction (const Key &k, int rootPc, int quality)
{
	if (secondaryDominantTarget (k, rootPc, quality) >= 0 || secondaryLeadingToneTarget (k, rootPc, quality) >= 0) return FUNC_DOMINANT;
	int deg = diatonicDegreeOf (k, rootPc);
	if (deg >= 0)
	{
		if (deg == 0 || deg == 2 || deg == 5) return FUNC_TONIC;
		if (deg == 1 || deg == 3) return FUNC_SUBDOMINANT;
		return FUNC_DOMINANT;
	}
	int off = imod (rootPc - tonicPc (k), 12);
	if (off == 1 || off == 5 || off == 8 || off == 10 || off == 3) return FUNC_SUBDOMINANT;	// bII, iv, bVI, bVII, bIII: borrowed colours
	return FUNC_OTHER;
}

DegColour degColour (const Key &k, int rootPc, int quality)
{
	int rpc = imod (rootPc, 12);
	int deg = degreeOf (k, rpc);
	for (int mode = 0; mode <= 5; mode++)
		for (int susp = 0; susp <= 2; susp++)
			for (int col = 0; col <= 4; col++)
			{
				RootQ d = diatonicChord (k, deg, col, susp, mode);
				if (d.root == rpc && d.quality == quality) { DegColour r = { deg, col, susp, mode }; return r; }
			}
	DegColour r = { -1, 0, 0, 0 };
	return r;
}
DegColour colourForQuality (int quality)
{
	Key c;
	for (int mode = 0; mode <= 5; mode++)
		for (int susp = 0; susp <= 2; susp++)
			for (int col = 0; col <= 4; col++)
				if (diatonicChord (c, 0, col, susp, mode).quality == quality) { DegColour r = { 0, col, susp, mode }; return r; }
	DegColour r = { 0, 0, 0, 0 };
	return r;
}
int qualityForColour (int colour, int suspension, int mode) { Key c; return diatonicChord (c, 0, colour, suspension, mode).quality; }

// ---- cadences ------------------------------------------------------------------------------------------------
const char *const g_cadenceStyles[CADENCE_STYLE_COUNT] = {
	"Auto (rich)", "Authentic (V - I)", "Plagal (IV - I)", "Jazz (ii - V - I)", "Turnaround (I-vi-ii-V)",
	"Pop (I-V-vi-IV)", "Doo-wop (I-vi-IV-V)", "EDM (vi-IV-I-V)", "Royal road (IV-V-iii-vi)", "Pachelbel (canon)",
	"Circle of fifths, descending", "Blues (I-IV-V)", "Minor blues", "Andalusian (i-bVII-bVI-V)", "Phrygian / Spanish",
	"Dorian (i-IV)", "Mixolydian (I-bVII)", "Chromatic mediants", "Backdoor (ii-bVII7-I)", "Tritone substitution",
	"Half cadence (-> V)", "Deceptive (V -> vi)", "Whole piece (auto)", "Coltrane (Giant Steps)", "AABA form",
	"Circle of fifths, ascending", "Baroque (cycle -> V-I, Picardy)", "Modal / mediants (harp)",
	"Aeolian / epic (bVI-bVII-i)", "Rich (modal mixture)" };

int autoRhythmStyle (int cs)
{
	switch (cs)
	{
	case 1: return 1;
	case 2: return 0;
	case 3: case 4: case 18: case 19: case 23: case 24: return 6;
	case 5: case 8: case 22: return 8;
	case 6: return 21;
	case 7: case 16: return 7;
	case 9: return 3;
	case 10: case 25: return 5;
	case 26: return 3;
	case 27: return 27;
	case 28: return 1;
	case 29: return 8;
	case 11: case 12: return 9;
	case 13: case 14: return 18;
	case 15: return 11;
	case 17: return 19;
	case 20: case 21: return 1;
	default: return 8;
	}
}

static const int s_next[7][6] = { { 1, 2, 3, 4, 5, 6 }, { 4, 6 }, { 3, 5 }, { 0, 1, 4, 6 }, { 0, 5 }, { 1, 3, 4 }, { 0 } };
static const int s_nextLen[7] = { 6, 2, 2, 4, 2, 3, 1 };

Vec<RootQ> cadence (const Key &k, int startDegree, int numChords, int style, int seed)
{
	numChords = imax (1, numChords);
	NetRandom rng (seed);
	bool minor = k.mode == 1;
	int tonic = tonicPc (k);
	const int *pcs = minor ? s_minorPcs : s_majorPcs;
	const int *qual = minor ? s_minorQual : s_majorQual;
	int s0 = imod (startDegree, 7);

	auto D = [&] (int deg) { int d = imod (deg, 7); RootQ r = { pcs[d], qual[d] }; return r; };
	auto C = [&] (int off, int q) { RootQ r = { imod (off, 12), q }; return r; };
	auto Triad = [] (int q) { switch (q) { case 6: case 8: return 0; case 7: return 1; case 9: return 2; default: return q; } };
	auto Dt = [&] (int deg) { RootQ c = D (deg); c.quality = Triad (c.quality); return c; };
	auto Func = [&] (const int *tail, int tn) {
		Vec<int> degs; degs.push (s0);
		int headLen = imax (1, numChords - tn);
		for (int i = 1; i < headLen; i++) { int last = degs.back (); degs.push (s_next[last][rng.next (s_nextLen[last])]); }
		for (int i = 0; i < tn; i++) degs.push (tail[i]);
		while (degs.size () > numChords) degs.removeAt (0);
		Vec<RootQ> l; for (int i = 0; i < degs.size (); i++) l.push (D (degs[i]));
		return l;
	};
	auto Tile = [&] (const RootQ *pat, int pn) { Vec<RootQ> l; for (int i = 0; i < numChords; i++) l.push (pat[i % pn]); return l; };

	Vec<RootQ> cells; bool toTonic = true, triadic = false, picardy = false;
	switch (style)
	{
	case 1: { int t[] = { 4, 0 }; cells = Func (t, 2); } break;
	case 2: { int t[] = { 3, 0 }; cells = Func (t, 2); } break;
	case 3: { int t[] = { 1, 4, 0 }; cells = Func (t, 3); } break;
	case 4: { RootQ p[] = { D (0), D (5), D (1), D (4) }; cells = Tile (p, 4); } break;
	case 5: { RootQ p[] = { D (0), D (4), D (5), D (3) }; cells = Tile (p, 4); } break;
	case 6: { RootQ p[] = { D (0), D (5), D (3), D (4) }; cells = Tile (p, 4); } break;
	case 7: { RootQ p[] = { D (5), D (3), D (0), D (4) }; cells = Tile (p, 4); } break;
	case 8: { RootQ p[] = { D (3), D (4), D (2), D (5) }; cells = Tile (p, 4); } break;
	case 9: { RootQ p[] = { D (0), D (4), D (5), D (2), D (3), D (0), D (3), D (4) }; cells = Tile (p, 8); } break;
	case 10: { int d = s0; for (int i = 0; i < numChords; i++) { cells.push (Dt (d)); d = (d + 3) % 7; } triadic = true; } break;
	case 11: { RootQ p[] = { D (0), D (0), D (0), D (0), D (3), D (3), D (0), D (0), D (4), D (3), D (0), D (4) }; cells = Tile (p, 12); } break;
	case 12: { RootQ p[] = { C (0, 1), C (0, 1), C (0, 1), C (0, 1), C (5, 1), C (5, 1), C (0, 1), C (0, 1), C (7, 8), C (5, 1), C (0, 1), C (7, 8) }; cells = Tile (p, 12); } break;
	case 13: { RootQ p[] = { C (0, 1), C (10, 0), C (8, 0), C (7, 8) }; cells = Tile (p, 4); } break;
	case 14: { RootQ p[] = { C (0, 1), C (1, 0), C (0, 1), C (7, 8) }; cells = Tile (p, 4); } break;
	case 15: { RootQ p[] = { C (0, 1), C (5, 0) }; cells = Tile (p, 2); } break;
	case 16: { RootQ p[] = { C (0, 0), C (10, 0) }; cells = Tile (p, 2); } break;
	case 17: { RootQ p[] = { C (0, 0), C (3, 0), C (8, 0) }; cells = Tile (p, 3); } break;
	case 18: { RootQ p[] = { D (1), C (10, 8), D (0) }; cells = Tile (p, 3); } break;
	case 19: { RootQ p[] = { D (1), C (1, 8), D (0) }; cells = Tile (p, 3); } break;
	case 20: { int t[] = { 4 }; cells = Func (t, 1); toTonic = false; } break;
	case 21: { int t[] = { 4, 5 }; cells = Func (t, 2); toTonic = false; } break;
	case 22:
	{
		// a loose song form: two loops, ending on ii - V - (I)
		RootQ loops[5][8] = {
			{ D (0), D (4), D (5), D (3) }, { D (0), D (5), D (3), D (4) }, { D (5), D (3), D (0), D (4) },
			{ D (3), D (4), D (2), D (5) }, { D (0), D (4), D (5), D (2), D (3), D (0), D (3), D (4) } };
		int lens[5] = { 4, 4, 4, 4, 8 };
		int verse = rng.next (5), chorus = rng.next (5);
		int n = numChords;
		while (cells.size () < n)
		{
			int sec = cells.size () < n / 2 ? verse : chorus;
			for (int i = 0; i < lens[sec] && cells.size () < n; i++) cells.push (loops[sec][i]);
		}
		if (cells.size () >= 3) { cells[cells.size () - 3] = D (1); cells[cells.size () - 2] = D (4); }
	} break;
	case 23: { RootQ p[] = { C (0, 6), C (3, 8), C (8, 6), C (11, 8), C (4, 6), C (7, 8) }; cells = Tile (p, 6); } break;
	case 24:
	{
		RootQ A[] = { D (0), D (5), D (1), D (4) }, B[] = { D (3), D (3), D (4), D (4) };
		const RootQ *secs[] = { A, A, B, A };
		int per = imax (1, numChords / 4);
		for (int si = 0; si < 4; si++) for (int i = 0; i < per; i++) cells.push (secs[si][i % 4]);
		while (cells.size () < numChords) cells.push (D (0));
		while (cells.size () > numChords) cells.pop ();
	} break;
	case 25: { int d = s0; for (int i = 0; i < numChords; i++) { cells.push (Dt (d)); d = (d + 4) % 7; } triadic = true; } break;
	case 26:
	{
		int d = s0;
		for (int i = 0; i < numChords; i++) { cells.push (Dt (d)); d = (d + 3) % 7; }
		if (numChords >= 2) cells[numChords - 2] = D (4);
		triadic = true; picardy = minor;
	} break;
	case 27: { RootQ p[] = { D (0), D (5), C (3, 0), D (3) }; cells = Tile (p, 4); } break;
	case 28: { RootQ p[] = { C (0, minor ? 1 : 0), C (8, 0), C (10, 0) }; cells = Tile (p, 3); } break;
	case 29: { RootQ p[] = { D (0), D (5), C (8, 0), D (4) }; cells = Tile (p, 4); } break;
	default: { int t[] = { rng.next (2) == 0 ? 1 : 3, 4, 0 }; cells = Func (t, 3); } break;
	}
	if (toTonic && cells.size () > 0) { RootQ t = { pcs[0], minor ? 1 : 0 }; cells[cells.size () - 1] = t; }

	Vec<RootQ> out;
	for (int i = 0; i < cells.size (); i++)
	{
		int off = imod (cells[i].root, 12), q = cells[i].quality;
		if (off == 0) q = triadic ? (minor ? 1 : 0) : (minor ? s_minorTonic : s_majorTonic)[rng.next (3)];
		else if (q == 8) q = s_domColour[rng.next (3)];
		RootQ r = { (tonic + off) % 12, q };
		out.push (r);
	}
	if (picardy && out.size () > 0 && out.back ().root == tonic) out.back ().quality = 0;
	return out;
}

static double avgOf (const int *n, int c) { if (c == 0) return 0; double s = 0; for (int i = 0; i < c; i++) s += n[i]; return s / c; }
static double voicingCost (const int *prev, int np, const int *cur, int nc)
{
	double sum = 0;
	for (int i = 0; i < nc; i++)
	{
		int best = 0x7fffffff;
		for (int j = 0; j < np; j++) best = imin (best, iabs (cur[i] - prev[j]));
		sum += best;
	}
	return sum;
}

Vec<ShiftInv> voiceLead (const Vec<RootQ> &chords, int baseOctave, int anchor)
{
	Vec<ShiftInv> result; result.resize (chords.size ());
	int prev[16]; int np = 0; bool havePrev = false;
	double target = 0; bool haveTarget = false;
	const double Band = 7;
	for (int i = 0; i < chords.size (); i++)
	{
		int root = chords[i].root, quality = chords[i].quality;
		int tmp[16];
		int voices = imax (1, chordNotes (root, baseOctave, quality, 0, false, tmp));
		int bestShift = 0, bestInv = 0; double bestCost = 1e300;
		for (int sh = -1; sh <= 1; sh++)
			for (int k = 0; k < voices; k++)
			{
				int notes[16]; int nn = chordNotes (root, baseOctave + sh, quality, k, false, notes);
				if (nn == 0) continue;
				double avg = avgOf (notes, nn), cost;
				if (!haveTarget) cost = iabs (notes[0] - (baseOctave + 1) * 12);
				else
				{
					cost = voicingCost (prev, np, notes, nn);
					if (anchor == 1) cost += 100 * iabs (notes[0] - prev[0]);
					else if (anchor == 2) cost += 100 * iabs (notes[nn - 1] - prev[np - 1]);
					if (imod ((notes[0] % 12) - (root % 12), 12) == 7) cost += 3;
					if (dabs (avg - target) > Band) cost += 1000;
				}
				if (cost < bestCost) { bestCost = cost; bestShift = sh; bestInv = k; }
			}
		result[i].shift = bestShift; result[i].inversion = bestInv;
		np = chordNotes (root, baseOctave + bestShift, quality, bestInv, false, prev); havePrev = true;
		if (!haveTarget) { target = avgOf (prev, np); haveTarget = true; }
	}
	(void) havePrev;
	return result;
}

InvOct voiceLeadStep (const int *prevNotes, int nPrev, int root, int quality, int baseOctave, int anchor, int direction)
{
	InvOct r = { 0, baseOctave };
	int tmp[16];
	int voices = imax (1, chordNotes (root, baseOctave, quality, 0, false, tmp));
	if (!prevNotes || nPrev == 0) return r;
	double bestCost = 1e300;
	for (int sh = -1; sh <= 1; sh++)
		for (int k = 0; k < voices; k++)
		{
			int notes[16]; int nn = chordNotes (root, baseOctave + sh, quality, k, false, notes);
			if (nn == 0) continue;
			double cost = voicingCost (prevNotes, nPrev, notes, nn);
			if (anchor == 1) cost += 100 * iabs (notes[0] - prevNotes[0]);
			else if (anchor == 2) cost += 100 * iabs (notes[nn - 1] - prevNotes[nPrev - 1]);
			if (imod ((notes[0] % 12) - (root % 12), 12) == 7) cost += 3;
			if (direction != 0)
			{
				double delta = anchor == 1 ? notes[0] - prevNotes[0]
					     : anchor == 2 ? notes[nn - 1] - prevNotes[nPrev - 1]
					     : avgOf (notes, nn) - avgOf (prevNotes, nPrev);
				if (delta * direction < 0) cost += 25;
				else if (delta * direction > 0) cost -= 5;
			}
			if (cost < bestCost) { bestCost = cost; r.inversion = k; r.octave = baseOctave + sh; }
		}
	return r;
}

int nearestInterval (int fromPc, int toPc, int direction)
{
	int up = imod (toPc - fromPc, 12);
	if (direction == 1) return up;
	if (direction == 2) return up == 0 ? 0 : up - 12;
	return up > 6 ? up - 12 : up;
}
bool modeDelta (int srcMode, int tgtMode, int delta[12])
{
	if (srcMode == tgtMode) return false;
	const int *src = modeScale (srcMode), *tgt = modeScale (tgtMode);
	for (int i = 0; i < 12; i++) delta[i] = 0;
	for (int i = 0; i < 7; i++) delta[imod (src[i], 12)] = tgt[i] - src[i];
	return true;
}

// ---- the next chord ----------------------------------------------------------------------------------------------
const char *const g_moodNames[MOOD_COUNT] = { "Auto", "Joyful", "Serene", "Melancholic", "Nostalgic", "Epic", "Bright", "Jazzy" };

struct SBase { int deg, rootOff, quality; const char *label, *effect; };
#define SD(d, e) { d, 0, 0, 0, e }
#define SC(o, q, l, e) { -1, o, q, l, e }
static const SBase s_t0[] = { SD (3, "Opening"), SD (4, "Tension"), SD (5, "Rest"), SD (1, "Pre-dominant"), SC (2, 8, "V/V", "Bright tension"), SC (10, 0, "\xE2\x99\xADVII", "Floating colour") };
static const SBase s_t1[] = { SD (4, "Tension"), SD (3, "Pre-dominant"), SD (6, "Tension"), SC (1, 0, "\xE2\x99\xADII", "Dramatic tension") };
static const SBase s_t2[] = { SD (5, "Rest"), SD (3, "Opening"), SC (8, 0, "\xE2\x99\xADVI", "Distant colour") };
static const SBase s_t3[] = { SD (4, "Tension"), SD (0, "Plagal cadence"), SD (1, "Pre-dominant"), SC (5, 1, "iv", "Melancholy"), SC (1, 0, "\xE2\x99\xADII", "Dramatic tension") };
static const SBase s_t4[] = { SD (0, "Resolution"), SD (5, "Deceptive cadence"), SD (3, "Colour"), SD (2, "Colour"), SC (8, 0, "\xE2\x99\xADVI", "Dark surprise") };
static const SBase s_t5[] = { SD (1, "Pre-dominant"), SD (3, "Opening"), SD (4, "Tension"), SC (2, 8, "V/V", "Bright tension"), SC (10, 0, "\xE2\x99\xADVII", "Floating colour") };
static const SBase s_t6[] = { SD (0, "Resolution"), SD (5, "Colour"), SD (2, "Colour") };
static const SBase *const s_table[7] = { s_t0, s_t1, s_t2, s_t3, s_t4, s_t5, s_t6 };
static const int s_tableLen[7] = { 6, 4, 3, 5, 5, 5, 3 };

static bool eq (const char *a, const char *b) { return a && b && !strcmp (a, b); }

static double contextMul (const SBase &b, int cur, int prev2, bool nearCadence, bool phraseStart)
{
	double m = 1.0;
	if (b.deg == cur && b.deg >= 0) m *= 0.30;
	bool preDomOrDom = cur == 1 || cur == 3 || cur == 4;
	if (preDomOrDom && b.deg == 0) m *= 1.6;
	if (cur == 4 && b.deg == 5) m *= 1.25;
	if (prev2 == 1 && cur == 4 && b.deg == 0) m *= 1.8;
	if (cur == 1 && b.deg == 4) m *= 1.3;
	if (nearCadence) { if (b.deg == 0) m *= 1.6; else if (b.deg == 4) m *= 1.2; else if (eq (b.effect, "Opening")) m *= 0.7; }
	if (phraseStart && (eq (b.effect, "Opening") || b.deg == 3 || b.deg == 4)) m *= 1.15;
	return m;
}
static double moodMul (const SBase &b, int mood)
{
	bool chromatic = b.deg < 0;
	bool seventhy = b.quality == 8 || b.deg == 1 || b.deg == 4;
	switch (mood)
	{
	case MOOD_JAZZY: return seventhy ? 1.5 : (chromatic ? 1.2 : 0.9);
	case MOOD_EPIC: return chromatic ? 1.7 : (eq (b.effect, "Deceptive cadence") || eq (b.effect, "Dark surprise") ? 1.4 : 0.9);
	case MOOD_NOSTALGIC: return (b.deg == 5 || b.deg == 3 || b.deg == 2) ? 1.4 : (chromatic ? 0.8 : 1.0);
	case MOOD_MELANCHOLIC: return (b.deg == 5 || b.deg == 1 || (chromatic && b.rootOff == 5)) ? 1.4 : (b.deg == 0 ? 0.85 : 1.0);
	case MOOD_BRIGHT:
	case MOOD_JOYFUL: return (b.deg == 0 || b.deg == 3 || b.deg == 4 || b.deg == 5) ? 1.25 : (chromatic ? 0.5 : 0.9);
	case MOOD_SERENE: return (b.deg == 3 || b.deg == 0 || eq (b.effect, "Plagal cadence")) ? 1.35 : (eq (b.effect, "Tension") ? 0.75 : (chromatic ? 0.8 : 1.0));
	default: return 1.0;
	}
}
static int suggestColour (int prevDeg, int nextDeg)
{
	if (nextDeg == 4 || nextDeg == 6) return 2;
	if (nextDeg == 1) return 2;
	if (nextDeg == 0) return (prevDeg == 4 || prevDeg == 6) ? 0 : 4;
	return 0;
}

Vec<Suggestion> suggestNext (const int *prevDegrees, int n, int barIndex, int phraseLen, int mood, const Key &k)
{
	int cur = (n > 0 && prevDegrees[n - 1] >= 0) ? iclamp (prevDegrees[n - 1], 0, 6) : 0;
	int prev2 = n > 1 ? prevDegrees[n - 2] : -1;
	if (phraseLen < 1) phraseLen = 4;
	bool nearCadence = ((barIndex + 1) % phraseLen) == 0;
	bool phraseStart = (barIndex % phraseLen) == 0;
	Vec<Suggestion> out;
	for (int i = 0; i < s_tableLen[cur]; i++)
	{
		const SBase &b = s_table[cur][i];
		double w = 1.0 - i * 0.12;
		w *= contextMul (b, cur, prev2, nearCadence, phraseStart);
		w *= moodMul (b, mood);
		Suggestion s;
		s.deg = b.deg; s.rootOff = b.rootOff; s.quality = b.quality; s.effect = b.effect; s.weight = dmax (0.01, w);
		s.recommended = false;
		s.colour = b.deg >= 0 ? suggestColour (cur, b.deg) : 0;
		if (b.deg >= 0)
		{
			RootQ ch = diatonicChord (k, b.deg);
			int q = ch.quality;
			bool minorish = q == 1 || q == 7 || q == 14 || q == 17 || q == 2 || q == 9 || q == 10;
			bool dim = q == 2 || q == 9 || q == 10;
			snprintf (s.label, sizeof s.label, "%s%s", minorish ? s_romanL[b.deg] : s_romanU[b.deg], dim ? "\xC2\xB0" : "");
		}
		else snprintf (s.label, sizeof s.label, "%s", b.label);
		out.push (s);
	}
	out.sort ([] (const Suggestion &a, const Suggestion &b) { return a.weight > b.weight; });	// stable, as OrderByDescending
	if (out.size () > 0) out[0].recommended = true;
	if (out.size () > 1 && out[1].weight >= out[0].weight * 0.85) out[1].recommended = true;
	return out;
}

RootQ suggestionChord (const Suggestion &s, const Key &k)
{
	if (s.deg >= 0) return diatonicChord (k, s.deg, s.colour, 0, 0);
	RootQ r = { imod (tonicPc (k) + s.rootOff, 12), s.quality };
	return r;
}

// ---- the chord track ---------------------------------------------------------------------------------------------
static int polyVoiced (const PolyChordItem &c, int baseOct, bool open, int *out)
{
	return chordNotes (c.root, baseOct + c.octaveShift, c.quality, c.inversion, open, out);
}

void revoicePolyChord (PolyChordModule &m, Vec<int> &prev, bool &havePrev, int baseOct)
{
	int anchor = iclamp (m.voiceLeadAnchor, 0, 2);
	for (int i = 0; i < m.chords.size (); i++)
	{
		PolyChordItem &c = m.chords[i];
		if (!havePrev) { c.inversion = 0; c.octaveShift = 0; }
		else
		{
			InvOct v = voiceLeadStep (prev.data (), prev.size (), c.root, c.quality, baseOct, anchor, 0);
			c.inversion = v.inversion; c.octaveShift = v.octave - baseOct;
		}
		int n[16]; int c2 = polyVoiced (c, baseOct, m.openVoicing, n);
		prev.clear (); for (int j = 0; j < c2; j++) prev.push (n[j]);
		havePrev = true;
	}
}

void revoiceTrack (Track &t)
{
	Vec<int> prev; bool havePrev = false;
	int baseOct = 4; bool haveBase = false;
	for (int i = 0; i < t.items.size (); i++)
	{
		Module *m = t.items[i].module;
		if (!m) continue;
		if (m->kind == M_PATTERN)
		{
			PatternModule *pg = (PatternModule *) m;
			if (!haveBase) { baseOct = pg->octave; haveBase = true; }
			if (pg->voiceLeadMode != 0 && havePrev)
			{
				InvOct v = voiceLeadStep (prev.data (), prev.size (), pg->root, pg->quality, baseOct, pg->voiceLeadMode - 1, 0);
				pg->inversion = v.inversion; pg->octave = v.octave;
			}
			int n[16]; int c = chordNotes (pg->root, pg->octave, pg->quality, pg->inversion, false, n);
			prev.clear (); for (int j = 0; j < c; j++) prev.push (n[j]);
			havePrev = true;
		}
		else if (m->kind == M_POLYCHORD)
		{
			PolyChordModule *pc = (PolyChordModule *) m;
			if (!haveBase) { baseOct = pc->octave; haveBase = true; }
			revoicePolyChord (*pc, prev, havePrev, pc->octave);
		}
	}
}

bool resolveChordDegrees (Project &p, const Key &oldKey)
{
	const Key &newKey = p.key;
	bool any = false;
	int tonicShift = imod (tonicPc (newKey) - tonicPc (oldKey), 12);
	auto newCadenceQuality = [&] (int degree, int curQuality) {
		for (int col = 0; col < 5; col++)
			if (diatonicChord (oldKey, degree, col, 0, 0).quality == curQuality) return diatonicChord (newKey, degree, col, 0, 0).quality;
		return curQuality;
	};
	for (int ti = 0; ti < p.tracks.size (); ti++)
	{
		Track &t = p.tracks[ti];
		if (t.type == TRACK_DRUM) continue;
		for (int i = 0; i < t.items.size (); i++)
		{
			Module *m = t.items[i].module;
			if (!m) continue;
			if (m->kind == M_PATTERN)
			{
				PatternModule *pg = (PatternModule *) m;
				if (pg->degree >= 0)
				{
					RootQ nd = diatonicChord (newKey, pg->degree, pg->diatonicColour, pg->suspension, pg->modeOverride);
					if (pg->root != nd.root || pg->quality != nd.quality) { pg->root = nd.root; pg->quality = nd.quality; any = true; }
				}
				else if (tonicShift != 0) { pg->root = imod (pg->root + tonicShift, 12); any = true; }
			}
			else if (m->kind == M_CADENCE)
			{
				CadenceModule *cm = (CadenceModule *) m;
				for (int j = 0; j < cm->chords.size (); j++)
				{
					CadenceChord &c = cm->chords[j];
					if (c.degree >= 0)
					{
						RootQ nd = diatonicChord (newKey, c.degree);
						int nq = newCadenceQuality (c.degree, c.quality);
						if (c.root != nd.root || c.quality != nq) { c.root = nd.root; c.quality = nq; any = true; }
					}
					else if (tonicShift != 0) { c.root = imod (c.root + tonicShift, 12); any = true; }
				}
			}
			else if (m->kind == M_POLYCHORD)
			{
				PolyChordModule *pc = (PolyChordModule *) m;
				for (int j = 0; j < pc->chords.size (); j++)
				{
					PolyChordItem &c = pc->chords[j];
					if (c.degree >= 0)
					{
						RootQ nd = diatonicChord (newKey, c.degree, c.diatonicColour, c.suspension, c.modeOverride);
						if (c.root != nd.root || c.quality != nd.quality) { c.root = nd.root; c.quality = nd.quality; any = true; }
					}
					else if (tonicShift != 0) { c.root = imod (c.root + tonicShift, 12); any = true; }
				}
			}
		}
	}
	return any;
}

bool transposeProject (Project &p, const Key &to, int direction, int targetMode)
{
	Key cur = p.key;
	int interval = nearestInterval (tonicPc (cur), tonicPc (to), direction);
	int srcMode = effectiveMode (cur);
	int delta[12]; bool hasDelta = modeDelta (srcMode, targetMode, delta);
	if (interval == 0 && !hasDelta) return false;
	int toPc = tonicPc (to);
	Vec<Riff *> seen;
	for (int ti = 0; ti < p.tracks.size (); ti++)
	{
		Track &t = p.tracks[ti];
		if (t.type == TRACK_DRUM) continue;
		for (int i = 0; i < t.items.size (); i++)
		{
			Module *m = t.items[i].module;
			if (!m) continue;
			if (m->kind == M_PLAYRIFF)
			{
				Riff *r = p.riffById (((PlayRiffModule *) m)->riffId);
				if (r && !seen.contains (r))
				{
					seen.push (r);
					for (int j = 0; j < r->notes.size (); j++)
					{
						int n = r->notes[j].note + interval;
						if (hasDelta) n += delta[imod (imod (n, 12) - toPc, 12)];
						while (n < 0) n += 12;
						while (n > 95) n -= 12;
						r->notes[j].note = n;
					}
				}
			}
			else if (m->kind == M_PATTERN) { PatternModule *pg = (PatternModule *) m; if (pg->degree < 0) pg->root = imod (pg->root + interval, 12); }
			else if (m->kind == M_CADENCE) { CadenceModule *cm = (CadenceModule *) m; for (int j = 0; j < cm->chords.size (); j++) if (cm->chords[j].degree < 0) cm->chords[j].root = imod (cm->chords[j].root + interval, 12); }
			else if (m->kind == M_POLYCHORD) { PolyChordModule *pc = (PolyChordModule *) m; for (int j = 0; j < pc->chords.size (); j++) if (pc->chords[j].degree < 0) pc->chords[j].root = imod (pc->chords[j].root + interval, 12); }
		}
	}
	p.key = to; p.key.fullMode = targetMode;
	resolveChordDegrees (p, cur);
	return true;
}

} // namespace kt
