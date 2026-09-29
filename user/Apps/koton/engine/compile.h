//
// compile.h -- a project flattened for playback (Koton's TimelinePlayer constructor): every module
// rendered at its place (renderModule), then its notes put on one global grid of 24 slices a beat
// as note-on / note-off events, the performance feel applied on the way (the metric velocity,
// swing, humanize, agogic lengthening, glides), and the tempo map turned into a sample position per
// slice. Built on the UI side (the compile thread), then handed to the audio engine, which only
// READS it: it holds no pointer into the project and is freed as a whole.
//
#ifndef _koton_compile_h
#define _koton_compile_h

#include "gen.h"

namespace kt {

enum { CSPB = 24 };			// the player's slices per beat

enum { EV_OFF = 0, EV_ON = 1 };
struct CEvent
{
	int slice;
	unsigned char kind, note, vel, pad;	// note: MIDI
	float glideFrom, glideSec;		// glideSec > 0: a glissando from glideFrom
};

struct CTrack
{
	int srcIndex;				// the project track it comes from
	int channel;				// 0, or 9 for the drums
	int program, bank;			// GM program; drums: the kit's program, bank 128
	bool drum, silent;			// silent: the chord track (a harmony source, never heard)
	Str pluginId;				// an IPC instrument plugin instead of the SoundFont
	Vec<CEvent> events;			// by slice; at one slice the offs come before the ons
	Vec<VolumePoint> volume;		// the volume lane (sorted)
	Vec<AutomationPoint> lane[9];		// the automation lanes by parameter (AP_*), sorted; empty = none
};

struct CompiledSong
{
	int sampleRate;
	int totalSlices;
	Vec<long long> sliceSample;		// totalSlices + 1 entries: the first sample of each slice
	Vec<CTrack> tracks;
	long long totalSamples () const { return sliceSample.size () ? sliceSample.back () : 0; }
	int sliceAtSample (long long s) const;	// binary search
	double beatAtSample (long long s) const;
	long long sampleAtBeat (double beat) const;
};

extern Vec<int> g_kitPrograms;		// the SoundFont's drum kits (bank 128) by kit index: set by the app

// The notes of every module, flattened. `onlyTrack` >= 0: that track only (a preview).
CompiledSong *compileSong (const Project &p, int sampleRate, int onlyTrack = -1);

// the gain of a volume lane at a beat (TimelinePlayer.TrackGain) and a generic lane (SampleCurve)
double trackGain (const Vec<VolumePoint> &a, double baseVol, double beat);
double sampleCurve (const Vec<AutomationPoint> &pts, double def, double beat);

// The one-riff preview of a module (the editors' "Listen"): the module rendered at startBeat, as a
// song of its own with the track's sound, looping.
CompiledSong *compileModulePreview (const Project &p, int track, int item, int sampleRate);

// A riff (notes at `spq`) on its own, with a GM program: the riff editor's preview.
CompiledSong *compileRiffPreview (const Project &p, const Riff &r, int program, bool drum, int sampleRate);

} // namespace kt

#endif
