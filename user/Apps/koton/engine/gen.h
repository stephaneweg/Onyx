//
// gen.h -- Koton's generators, ported from MusicTracker/Engine: every module becomes a Riff (notes
// in slices) -- PatternGenerator (a chord in one of 28 accompaniment styles or a custom voice grid,
// its melodic cell, a cadence), ChordArticulation (the chord track's harmony played in a style),
// DrumPattern (16 grooves, fills, the catalog's motifs), the euclidean rings (PolyDrum,
// MelodicEuclid, PolyChord with its emergent melody) and MelodicLineEngine (pitches chosen from
// the harmony: chord tones on the strong beats, passing tones between, a contour).
// Harmony: chordAt / segments answer "which chord sounds at this beat" from the chord track.
//
#ifndef _koton_gen_h
#define _koton_gen_h

#include "theory.h"

namespace kt {

enum { SPQ = 24 };			// the canonical slices per quarter

// ---- chord styles ------------------------------------------------------------------------------------------
enum { STYLE_COUNT = 29, CUSTOM_STYLE = STYLE_COUNT - 1 };
extern const char *const g_styleNames[STYLE_COUNT];
enum { CUSTOM_VOICE_COUNT = 11, MELODIC_ROW_COUNT = 21 };
extern const char *const g_customVoiceNames[CUSTOM_VOICE_COUNT];	// "Bass", "1", "3", "5", "7", "1'", "9", "3'", "5'", "7'", "9'"
extern bool g_ternary;			// the project's meter is compound (x/8): arpeggio figures divide beats by 3

Riff generatePattern (const PatternModule &m);
Riff generateMelodic (const PatternModule &m, const Key &key);	// empty notes when there is none
Riff generateCadence (const CadenceModule &m);
Vec<Slice> voiceBarForCustom (int style, int beats, int chordLen);	// a built-in style seeding the custom grid
Vec<RiffNote> voiceNotesForCustom (int style, int beats, int chordLen, int spb);	// the same, as notes at spb slices / beat

// ---- harmony ------------------------------------------------------------------------------------------------
struct ChordSeg { double start, len; int root, quality, inversion; bool chordOpen; int chordVoiceLead; };
bool chordAt (const Project &p, double beat, int *root, int *quality, int *inversion);
Vec<ChordSeg> segments (const Project &p, double from, double length);

// ---- chord articulation -----------------------------------------------------------------------------------------
double articulationTotalBeats (const ArticulationModule &m);
Riff generateArticulation (const ArticulationModule &m, const Project &p, double startBeat);

// ---- drums -----------------------------------------------------------------------------------------------------
enum { DRUM_LANES = 47, DRUM_STYLE_COUNT = 17, DRUM_CUSTOM_STYLE = DRUM_STYLE_COUNT - 1 };
extern const char *const g_laneNames[DRUM_LANES];
extern const char *const g_drumStyleNames[DRUM_STYLE_COUNT];
extern const char *const g_densityNames[4];
int keyForLane (int lane);
int laneForKey (int key);
Riff generateDrums (const DrumModule &m);
Vec<RiffNote> laneNotesForStyle (int style, int beats);

// ---- euclidean rings -------------------------------------------------------------------------------------------
void euclidPattern (int k, int n, int rotation, Vec<bool> &out);	// E(k,n) rotated
void effectivePattern (bool customMode, const Vec<int> &customHits, int hits, int steps, int rotation, Vec<bool> &out);
const char *euclidName (const Vec<bool> &p);				// "tresillo", "cinquillo"... or 0
double polyDrumTotalBeats (const PolyDrumModule &m);
Riff generatePolyDrum (const PolyDrumModule &m);
double melodicPolyTotalBeats (const MelodicPolyModule &m);
Riff generateMelodicPoly (const MelodicPolyModule &m, const Project &p, double startBeat, int *carry);
double polyChordTotalBeats (const PolyChordModule &m);
Riff generatePolyChord (const PolyChordModule &m, const Project &p, double startBeat);
bool polyChordAt (const PolyChordModule &m, double localBeat, const PolyChordItem **item, double *itemStart);

// ---- melodic lines -----------------------------------------------------------------------------------------------
extern const char *const g_contourNames[9];
extern const char *const g_anchorNames[6];
extern const char *const g_variationNames[5];
Riff generateLine (const MelodicLineModule &m, const Project &p, double startBeat, int *carry);	// carry: 9 ints or 0

// ---- any module ---------------------------------------------------------------------------------------------------
// The notes a module plays at startBeat (absolute, on its track): what the player flattens, what the
// block thumbnails and the editors show. A chord's melodic cell (a 2nd voice, at its own resolution)
// goes to *cell when given. A generator plugin's block asks g_generatorHook (the IPC host).
Riff renderModule (const Module &m, const Project &p, double startBeat, int *carry, Riff *cell = 0);
typedef bool (*GeneratorHook) (const GeneratorModule &m, const Project &p, double startBeat, Riff &out);
extern GeneratorHook g_generatorHook;

} // namespace kt

#endif
