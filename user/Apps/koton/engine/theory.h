//
// theory.h -- Koton's harmony, ported from MusicTracker/Engine: MusicalMode (the scales), MusicTheory
// (the diatonic chord of a degree with its colour / suspension / mode, cadences in 30 styles, voice
// leading), ChordDegrees (a chord back to its degree, the chord track's voicing chain),
// HarmonySuggest (the next-chord co-pilot) and ChordModelOps (a key change, a transposition).
// The name tables are append-only: their indices are saved in the files. The UI shows them in
// English (Onyx's language); the order is Koton's.
//
#ifndef _koton_theory_h
#define _koton_theory_h

#include "model.h"

namespace kt {

// ---- modes, keys -------------------------------------------------------------------------------------------
enum { MODE_COUNT = 9 };
extern const char *const g_modeNames[MODE_COUNT];		// "Major (Ionian)", "Natural minor (Aeolian)"...
const int *modeScale (int mode);				// 7 semitone offsets
int effectiveMode (const Key &k);				// fullMode if set, else from mode
int tonicPc (const Key &k);
extern const char *const g_rootNames[12];			// "C", "C#"... (sharps)
extern const char *const g_rootNamesFlat[12];
const char *spellPc (int pc, const Key &k);			// a pitch class named for the key (flats in flat keys)
void keyName (const Key &k, char *buf, int cap);		// "F# minor (Aeolian)"
int keyFifths (const Key &k);					// the signature: sharps > 0, flats < 0

// ---- chords --------------------------------------------------------------------------------------------------
enum { QUALITY_COUNT = 35 };
extern const char *const g_qualityNames[QUALITY_COUNT];	// "Major", "Minor", ... (append-only)
extern const char *const g_qualitySymbols[QUALITY_COUNT];	// "", "m", "dim", ... for chord labels
int qualityIndex (const char *name);			// by Koton's French name or the English one, -1 none
int chordNotes (int root, int octave, int quality, int inversion, bool open, int *out);	// MIDI, ascending; count
void chordLabel (int root, int quality, const Key &k, char *buf, int cap);			// "F#m9"

extern const char *const g_colourNames[5];
extern const char *const g_suspensionNames[3];
extern const char *const g_modeOverrideNames[6];
struct RootQ { int root, quality; };
RootQ diatonicChord (const Key &k, int degree);					// the triad (quality from the scale)
RootQ diatonicChord (const Key &k, int degree, int colour, int suspension, int mode);
int degreeOf (const Key &k, int rootPc);						// the nearest degree
int diatonicDegreeOf (const Key &k, int pc);						// -1: not diatonic
int diatonicThird (const Key &k, int degree);
bool diatonicIsDim (const Key &k, int degree);
void chordShape (int quality, bool *minThird, bool *dimFifth, bool *augFifth, bool *dom7);
int secondaryDominantTarget (const Key &k, int rootPc, int quality);
int secondaryLeadingToneTarget (const Key &k, int rootPc, int quality);
int secondaryDominantRoot (const Key &k, int targetDegree);
void romanNumeral (const Key &k, int rootPc, int quality, int degreeHint, char *buf, int cap);	// "iv", "V/III", "♭VII"
enum { FUNC_TONIC = 0, FUNC_SUBDOMINANT = 1, FUNC_DOMINANT = 2, FUNC_OTHER = 3 };
int chordFunction (const Key &k, int rootPc, int quality);

// (degree, colour, suspension, mode) of a chord; degree -1 when it is not a diatonic colour of its degree
struct DegColour { int degree, colour, suspension, mode; };
DegColour degColour (const Key &k, int rootPc, int quality);
DegColour colourForQuality (int quality);		// degree unused
int qualityForColour (int colour, int suspension, int mode);

// ---- cadences, voice leading ----------------------------------------------------------------------------------
enum { CADENCE_STYLE_COUNT = 30 };
extern const char *const g_cadenceStyles[CADENCE_STYLE_COUNT];
int autoRhythmStyle (int cadenceStyle);
Vec<RootQ> cadence (const Key &k, int startDegree, int numChords, int style, int seed);
struct ShiftInv { int shift, inversion; };
Vec<ShiftInv> voiceLead (const Vec<RootQ> &chords, int baseOctave, int anchor);
struct InvOct { int inversion, octave; };
InvOct voiceLeadStep (const int *prev, int nPrev, int root, int quality, int baseOctave, int anchor, int direction);
int nearestInterval (int fromPc, int toPc, int direction);
bool modeDelta (int srcMode, int tgtMode, int delta[12]);	// false: same mode

// ---- the next chord ----------------------------------------------------------------------------------------------
enum { MOOD_AUTO, MOOD_JOYFUL, MOOD_SERENE, MOOD_MELANCHOLIC, MOOD_NOSTALGIC, MOOD_EPIC, MOOD_BRIGHT, MOOD_JAZZY, MOOD_COUNT };
extern const char *const g_moodNames[MOOD_COUNT];
struct Suggestion
{
	int deg;			// 0..6, or -1 chromatic (rootOff + quality)
	int rootOff, quality, colour;
	char label[16]; const char *effect;
	double weight; bool recommended;
};
Vec<Suggestion> suggestNext (const int *prevDegrees, int n, int barIndex, int phraseLen, int mood, const Key &k);
RootQ suggestionChord (const Suggestion &s, const Key &k);

// ---- the chord track --------------------------------------------------------------------------------------------
void revoiceTrack (Track &t);					// ChordDegrees.Revoice
void revoicePolyChord (PolyChordModule &m, Vec<int> &prev, bool &havePrev, int baseOct);
bool resolveChordDegrees (Project &p, const Key &oldKey);	// after a key change
bool transposeProject (Project &p, const Key &to, int direction, int targetMode);

} // namespace kt

#endif
