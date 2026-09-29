//
// model.h -- Koton's song model, a C++ port of Koton Studio's (MusicTracker/Engine/Timeline/
// TimelineProject.cs, Flow/FlowModule.cs, Riff.cs, RiffNote.cs): a project = a tempo map, a key, a
// meter, tracks; a track = a list of items, each a MODULE placed after a silence (positions are
// RELATIVE: an item starts where the previous one ended plus its SilenceBefore); a module is a
// generator (a riff, a chord, a chord articulation, a cadence, a melodic line, drums, euclidean
// rings...). The chord track (Type = Chord) is the harmony every other part reads.
//
// Files: Koton's .sq (JSON, System.Text.Json: PascalCase names, enums as numbers, modules with a
// "$type" discriminator) is read as is; what Onyx does not support is dropped. Onyx saves .kson,
// the same JSON (a superset: a .kson opens in Koton for Windows too) plus its own fields (the
// tracks' IPC plugins).
//
// Units: a beat = a quarter note; riffs count in slices (SlicesPerQuarter per beat, 24 by
// default); a note number is 0..95 with 0 = C0 = MIDI 12 (Koton's convention).
//
#ifndef _koton_model_h
#define _koton_model_h

#include "kbase.h"

namespace json { struct Value; class Writer; }

namespace kt {

typedef unsigned long long u64;

// ---- notes, riffs ---------------------------------------------------------------------------------------
struct BendPoint { int off; float semis; };

struct RiffNote
{
	int note, start, length;		// note 0..95 (MIDI - 12); start / length in slices
	int voice;				// the notation voice (kept for round trips)
	int glideFrom, glideDur;		// glissando from glideFrom over glideDur slices (0: none)
	Vec<BendPoint> bend;			// an optional pitch-bend curve, offsets from the start
	RiffNote () : note (0), start (0), length (1), voice (0), glideFrom (0), glideDur (0) {}
	RiffNote (int n, int s, int l) : note (n), start (s), length (l < 1 ? 1 : l), voice (0), glideFrom (n), glideDur (0) {}
	int end () const { return start + length; }
	float bendAt (int off) const;
};

// A slice of the legacy binary grid: bit n = note (or chord voice, drum lane) n on.
struct Slice
{
	u64 lo, hi;
	Slice () : lo (0), hi (0) {}
	bool on (int n) const { return n < 64 ? ((lo >> n) & 1) != 0 : n < 128 ? ((hi >> (n - 64)) & 1) != 0 : false; }
	void set (int n, bool v)
	{
		if (n < 0 || n >= 128) return;
		u64 &w = n < 64 ? lo : hi; u64 m = (u64) 1 << (n & 63);
		if (v) w |= m; else w &= ~m;
	}
};

namespace notes {
Vec<RiffNote> fromSlices (const Vec<Slice> &slices);
int lengthOf (const Vec<RiffNote> &n);
Vec<Slice> toSlices (const Vec<RiffNote> &n, int length);
}

struct Riff
{
	Str id, name;
	Vec<RiffNote> notes;
	int lengthSlices, spq;
	Riff () : name ("Riff"), lengthSlices (96), spq (24) {}
	double beats () const { return spq > 0 ? (double) lengthSlices / spq : 4; }
};

// ---- the key ---------------------------------------------------------------------------------------------
struct Key
{
	int tonicLetter;			// 0 = C .. 6 = B
	int accidental;				// -1 flat, 0, +1 sharp
	int mode;				// 0 major, 1 minor
	int fullMode;				// MusicalMode index (church modes, harmonic / melodic minor), -1 = from mode
	Key () : tonicLetter (0), accidental (0), mode (0), fullMode (-1) {}
};

// ---- modules ---------------------------------------------------------------------------------------------
enum ModuleKind
{
	M_PLAYRIFF, M_PATTERN, M_DRUMKIT, M_CADENCE, M_MELODICLINE, M_POLYDRUM, M_MELODICPOLY,
	M_POLYCHORD, M_ARTICULATION, M_GENERATOR, M_KINDS
};
const char *moduleTypeName (int kind);		// the .sq "$type" ("PlayRiff", "Pattern"...)

struct Module
{
	int kind;
	Str id;					// a Guid, as text
	double x, y, widthHint;
	bool collapsed;
	explicit Module (int k);
	virtual ~Module () {}
	virtual Module *clone () const = 0;
	const char *typeName () const { return moduleTypeName (kind); }
};

struct PlayRiffModule : Module
{
	Str riffId;
	PlayRiffModule () : Module (M_PLAYRIFF) {}
	Module *clone () const { return new PlayRiffModule (*this); }
};

// the realisation fields a chord, an articulation and a cadence share (a custom voice grid, a melodic cell)
struct CustomGrid
{
	Vec<Slice> slices;			// the OR-merged grid (length carrier)
	int spq;				// its slices per quarter
	Vec<RiffNote> notes;			// the note-list form: the source of truth when not empty
	bool hasNotes;				// "notes" was present (vs null)
	CustomGrid () : spq (4), hasNotes (false) {}
	bool present () const { return slices.size () > 0 || notes.size () > 0; }
	void setNotes (const Vec<RiffNote> &n, int slicesPerQuarter, int lengthSlices);
};

struct PatternModule : Module		// ONE chord (degree-locked or absolute) + how it was played
{
	int root, octave, quality, inversion, style, beatsPerBar, repeats;
	bool bass, bassPerBeat;
	int heldMode, climbMode;
	bool halveDurations;
	int degree;				// -1 absolute, 0..6 locked to a degree of the key
	int voiceLeadMode, diatonicColour, suspension, modeOverride;
	bool openVoicing;
	int melodicOctave, melodicAnchor;
	bool melodicOpenVoicing;
	int melodicVoiceLead;
	bool melodicPreserve;
	CustomGrid custom;			// style "Personnalisé": voice rows (0 bass, then degrees)
	Str userStyleName;
	CustomGrid melodic;			// the melodic cell: diatonic degree rows
	// transient (not saved): set by a cadence while rendering
	int heldVoiceOverride, patternCellOffset;
	PatternModule () : Module (M_PATTERN), root (0), octave (4), quality (0), inversion (0), style (0), beatsPerBar (4), repeats (1),
		bass (false), bassPerBeat (false), heldMode (0), climbMode (0), halveDurations (false), degree (-1), voiceLeadMode (0),
		diatonicColour (0), suspension (0), modeOverride (0), openVoicing (false), melodicOctave (5), melodicAnchor (0),
		melodicOpenVoicing (false), melodicVoiceLead (0), melodicPreserve (false), heldVoiceOverride (-1), patternCellOffset (0)
	{ melodic.spq = 4; }
	bool hasMelodic () const { return melodic.notes.size () > 0; }
	Module *clone () const { return new PatternModule (*this); }
};

struct ArticulationModule : Module	// "how to play" the chord of the chord track, whatever it is
{
	double beats;				// the cell (the repeated motif), in beats
	double lengthBeats;			// the whole module (0: one cell)
	int style, octave, inversion, voiceLeadMode, voiceLeadDirection, openVoicingMode;
	bool openVoicing, bass, bassPerBeat;
	int heldMode, climbMode;
	bool halveDurations;
	CustomGrid custom;
	Str userStyleName;
	int melodicOctave, melodicAnchor;
	bool melodicOpenVoicing;
	int melodicVoiceLead;
	CustomGrid melodic;
	ArticulationModule () : Module (M_ARTICULATION), beats (4), lengthBeats (0), style (0), octave (4), inversion (0),
		voiceLeadMode (0), voiceLeadDirection (0), openVoicingMode (0), openVoicing (false), bass (false), bassPerBeat (false),
		heldMode (0), climbMode (0), halveDurations (false), melodicOctave (5), melodicAnchor (0), melodicOpenVoicing (false),
		melodicVoiceLead (0) {}
	bool hasMelodic () const { return melodic.notes.size () > 0 || melodic.slices.size () > 0; }
	Module *clone () const { return new ArticulationModule (*this); }
};

struct DrumModule : Module
{
	int style, density, kit;
	bool fillLast;
	int beatsPerBar, repeats;
	Str catCategory, catMotif;
	CustomGrid custom;			// Note = drum lane
	DrumModule () : Module (M_DRUMKIT), style (0), density (0), kit (0), fillLast (false), beatsPerBar (4), repeats (4) {}
	Module *clone () const { return new DrumModule (*this); }
};

struct CadenceChord { int root, quality, inversion, octaveShift, heldVoice, degree; CadenceChord () : root (0), quality (0), inversion (0), octaveShift (0), heldVoice (-1), degree (-1) {} };

struct CadenceModule : Module
{
	int octave, style, beatsPerBar;
	bool bass, bassPerBeat;
	int heldMode, climbMode;
	bool halveDurations;
	int cadenceStyle, startDegree, measures, chordsPerMeasure, voiceLeadMode;
	bool openVoicing;
	Vec<CadenceChord> chords;
	CustomGrid custom;
	CadenceModule () : Module (M_CADENCE), octave (4), style (0), beatsPerBar (1), bass (false), bassPerBeat (false), heldMode (0),
		climbMode (0), halveDurations (false), cadenceStyle (0), startDegree (0), measures (4), chordsPerMeasure (1),
		voiceLeadMode (1), openVoicing (false) {}
	Module *clone () const { return new CadenceModule (*this); }
};

struct MelodicLineModule : Module	// the RHYTHM of up to 3 voices; the engine picks the pitches from the harmony
{
	enum { MaxVoices = 3 };
	int beatsPerBar;			// the line's TOTAL beats
	int voiceCount;
	Str lineName;
	bool preserve;
	CustomGrid rhythm;			// Note = voice row
	int registerShift, contour, anchor, continuity, variation, tensionSlope, amplitude, ornaments, waveLength;
	MelodicLineModule () : Module (M_MELODICLINE), beatsPerBar (4), voiceCount (1), preserve (false), registerShift (0), contour (0),
		anchor (0), continuity (0), variation (0), tensionSlope (0), amplitude (12), ornaments (0), waveLength (0) {}
	Module *clone () const { return new MelodicLineModule (*this); }
};

struct EuclidLayer			// a polyrhythmic drum layer: a lane and its E(k,n)
{
	int lane, accentLane, hits, steps, rotation;
	bool muted, collapsed, customMode;
	Vec<int> customHits;
	EuclidLayer () : lane (0), accentLane (-1), hits (3), steps (8), rotation (0), muted (false), collapsed (false), customMode (false) {}
};

struct PolyDrumModule : Module
{
	int kit, beats, repeats, beatsPerBar, durationBeats;
	Vec<EuclidLayer> layers;
	PolyDrumModule () : Module (M_POLYDRUM), kit (0), beats (4), repeats (4), beatsPerBar (4), durationBeats (0) {}
	Module *clone () const { return new PolyDrumModule (*this); }
};

struct EuclidVoice			// a melodic ring: a voice and its E(k,n)
{
	int voice, hits, steps, rotation, octave;
	bool muted, collapsed, legato, customMode;
	Vec<int> customHits;
	EuclidVoice () : voice (0), hits (3), steps (8), rotation (0), octave (0), muted (false), collapsed (false), legato (false), customMode (false) {}
};

struct MelodicPolyModule : Module
{
	int beats, repeats, beatsPerBar, durationBeats;
	Vec<EuclidVoice> layers;
	MelodicPolyModule () : Module (M_MELODICPOLY), beats (4), repeats (4), beatsPerBar (4), durationBeats (0) {}
	Module *clone () const { return new MelodicPolyModule (*this); }
};

struct PolyChordItem
{
	int root, quality, degree, diatonicColour, suspension, modeOverride, beats, inversion, octaveShift;
	PolyChordItem () : root (0), quality (0), degree (-1), diatonicColour (0), suspension (0), modeOverride (0), beats (4), inversion (0), octaveShift (0) {}
};

struct EuclidChordLayer
{
	int hits, steps, rotation, octave, toneIndex, contour, randomSeed;
	bool muted, collapsed, customMode, legato;
	Vec<int> customHits;
	EuclidChordLayer () : hits (3), steps (8), rotation (0), octave (0), toneIndex (0), contour (0), randomSeed (0), muted (false),
		collapsed (false), customMode (false), legato (false) {}
};

enum { PC_ONE_RING_PER_TONE = 0, PC_ONE_RING_SWEEP = 1 };
enum { RESTART_NEAREST = 0, RESTART_GRAVE, RESTART_AIGU, RESTART_TONIC, RESTART_TIERCE, RESTART_QUINTE };
enum { MONO_HIGHEST = 0, MONO_LOWEST, MONO_AUTO, MONO_RANDOM };

struct PolyChordModule : Module
{
	int octave, cycleBeats;
	double beats;				// 0: legacy (the sum of the inner chords)
	bool openVoicing;
	int voiceLeadAnchor, mode, restart;
	bool monodicPick;
	int monodicSeed;
	bool monodicAvoidRepeat;
	int monodicStrategy;
	Vec<PolyChordItem> chords;
	Vec<EuclidChordLayer> layers;
	PolyChordModule () : Module (M_POLYCHORD), octave (4), cycleBeats (4), beats (0), openVoicing (false), voiceLeadAnchor (0),
		mode (PC_ONE_RING_PER_TONE), restart (RESTART_NEAREST), monodicPick (false), monodicSeed (42), monodicAvoidRepeat (true),
		monodicStrategy (MONO_AUTO) {}
	Module *clone () const { return new PolyChordModule (*this); }
};

// A generator plugin's block (Koton's "KotonGenerator"; on Onyx: an IPC generator plugin): its id,
// its opaque state (base64 in the file), its length.
struct GeneratorModule : Module
{
	Str generatorId, state;			// state: base64, as in the file
	double durationBeats;
	GeneratorModule () : Module (M_GENERATOR), durationBeats (4) {}
	Module *clone () const { return new GeneratorModule (*this); }
};

// ---- tracks, the project ----------------------------------------------------------------------------------
struct Item
{
	double silenceBefore;
	Module *module;				// owned (0: an empty item)
	Item () : silenceBefore (0), module (0) {}
	Item (double s, Module *m) : silenceBefore (s), module (m) {}
	Item (const Item &o) : silenceBefore (o.silenceBefore), module (o.module ? o.module->clone () : 0) {}
	Item (Item &&o) : silenceBefore (o.silenceBefore), module (o.module) { o.module = 0; }
	~Item () { delete module; }
	Item &operator= (const Item &o) { if (this != &o) { delete module; silenceBefore = o.silenceBefore; module = o.module ? o.module->clone () : 0; } return *this; }
	Item &operator= (Item &&o) { if (this != &o) { delete module; silenceBefore = o.silenceBefore; module = o.module; o.module = 0; } return *this; }
};

struct VolumePoint { double beat, volume; };
struct AutomationPoint { double beat, value; };
enum { AP_VOLUME, AP_PAN, AP_EXPRESSION, AP_MODULATION, AP_SUSTAIN, AP_REVERB, AP_CHORUS, AP_PITCHBEND, AP_STACCATO };
struct AutomationLane { int param; bool enabled; Vec<AutomationPoint> points; AutomationLane () : param (0), enabled (true) {} };

// An Onyx plugin in a track's chain (an IPC process): its id (the plugin folder's name) and its state
struct PluginSlot { Str id; bool enabled; Str state; PluginSlot () : enabled (true) {} };

enum { TRACK_INSTRUMENT = 0, TRACK_DRUM = 1, TRACK_CHORD = 2 };
enum { DEFAULT_REVERB = 40 };

struct Track
{
	Str name;
	int type, instrument, drumKit;
	double volume, pan;
	bool mute, solo, collapsed;
	int reverbOffset;
	double reverbBusSend;
	Vec<VolumePoint> volumeAutomation;
	Vec<AutomationLane> lanes;
	Vec<Item> items;
	// Onyx
	PluginSlot instrumentPlugin;		// id empty: the SoundFont (instrument = the GM program)
	Vec<PluginSlot> inserts;
	Track () : name ("Piste"), type (TRACK_INSTRUMENT), instrument (0), drumKit (0), volume (1), pan (0), mute (false), solo (false),
		collapsed (false), reverbOffset (0), reverbBusSend (0) {}
	int reverbSend () const { return iclamp (DEFAULT_REVERB + reverbOffset, 0, 127); }
};

struct TempoChange { double beat, bpm; };
struct Marker { double beat; Str name; };
struct UserStyle { Str name; int spb, beats; Vec<Slice> slices; Vec<RiffNote> notes; bool hasNotes; UserStyle () : spb (4), beats (4), hasNotes (false) {} };
struct ChordCell { int root, quality; };

// the "structure" chord grid an auto-composer left (Koton's ComposedArrangement): only its chords
// are used here (the harmony); the rest of the recipe is dropped
struct Arrangement
{
	int slicesPerQuarter, chordSlices;
	Vec<ChordCell> chords;
	Arrangement () : slicesPerQuarter (24), chordSlices (96) {}
};

struct Project
{
	Vec<TempoChange> tempo;
	Vec<Track> tracks;
	Vec<UserStyle> userChordStyles, userMelodicLines, userDrumStyles;
	Key key;
	int timeSigNum, timeSigDen;
	double pickupBeats, timeSigScale, minBeats, swingPercent;
	int humanizePercent, agogicPercent;
	Vec<Marker> markers;
	Vec<Riff> riffs;			// saved at the document level ("Riffs")
	Arrangement *arrangement;		// owned, 0 = none
	double reverbBusMix;			// Onyx: the reverb bus return level

	Project ();
	Project (const Project &o);
	Project &operator= (const Project &o);
	~Project () { delete arrangement; }

	const Riff *riffById (const char *id) const;
	Riff *riffById (const char *id);
	double mainBpm () const { return tempo.size () ? tempo[0].bpm : 120; }
	double bpmAt (double beat) const;
	int barBeats () const;			// quarter-beats per bar (4/4 -> 4, 6/8 -> 3)

	double itemLength (const Item &it) const;
	double trackEnd (const Track &t) const;
	double itemStart (const Track &t, int index) const;
	double totalBeats () const;		// the longest track (at least minBeats)
	int chordTrackIndex () const;		// the (first) chord track, -1 none
};

double moduleBeats (const Module *m, const Project &p);	// ModuleDuration.Beats

// ---- files -------------------------------------------------------------------------------------------------
// Load a .sq / .kson document (JSON text); false with a message on error. What is not supported
// is dropped; missing fields keep their defaults.
bool loadProject (const char *text, unsigned long len, Project &out, char *err, int errcap);
// Write a project as .kson (JSON, pretty); the caller frees nothing: the Writer owns the text.
void saveProject (const Project &p, json::Writer &w);

// a new Guid-like id ("xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx")
Str newId ();
void seedIds (unsigned seed);

} // namespace kt

#endif
