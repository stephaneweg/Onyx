//
// engine.h -- Koton's audio engine: plays a CompiledSong through one MeltySynth per track (Koton's
// "one synth per track": each track its own stereo buffer, its mixer strip, its plugin chain), mixes,
// soft-clips. Also a preview voice (the editors' "Listen", a live MIDI keyboard, the metronome).
//
// Threads: the engine runs on its own (Onyx: on app core 2, where no kernel call and no allocation
// is allowed). Everything it needs is allocated beforehand by the UI side (ensureTracks, the song).
// The UI talks to it through a single-producer ring of commands and a few shared values (the
// mixer), reads back its state (the playhead, the meters, the load); a song it no longer uses is
// handed back (retired ()) to be freed on the UI side.
//
//   UI thread                                  engine (render ())
//     e.ensureTracks (n)   (allocates)
//     e.post (CMD_SONG, song)          ->        takes the song at the next block
//     e.post (CMD_PLAY, sample)        ->        plays from there
//     e.mix[t].volume = 0.8           ->        read at every block
//     e.retired () -> delete it        <-        the song it dropped
//
#ifndef _koton_engine_h
#define _koton_engine_h

#include "compile.h"
#include "../synth/meltysynth.h"

namespace kt {

enum { ENGINE_MAX_TRACKS = 48, ENGINE_BLOCK = 256 };

enum
{
	CMD_SONG = 1,		// p = CompiledSong* (0: none)
	CMD_PLAY,		// a = the start sample
	CMD_STOP,
	CMD_SEEK,		// a = the sample
	CMD_LOOP,		// a = start sample, b = end sample, i = on
	CMD_PREVIEW,		// p = CompiledSong* to loop on the preview synth (0: stop the preview)
	CMD_NOTE_ON,		// i = note, j = velocity (the preview synth, its program k, drum if l)
	CMD_NOTE_OFF,		// i = note
	CMD_ALL_OFF,
	CMD_METRONOME,		// i = on
	CMD_EXTERNAL,		// i = track, p = ExternalSource* (0: back to the SoundFont)
};

struct Command { int type; long long a, b; int i, j, k, l; void *p; };

// the mixer strip of a track, written by the UI, read by the engine at every block
struct MixStrip
{
	volatile float volume, pan;		// 0..1+, -1..+1
	volatile int mute, solo, reverb;	// reverb: CC91 0..127
	MixStrip () : volume (1), pan (0), mute (0), solo (0), reverb (40) {}
};

// Another source of a track's audio (an IPC instrument plugin): the engine hands it the notes and
// takes its stereo blocks. Implemented by the plugin host (plughost.h); it must not allocate either.
struct ExternalSource
{
	virtual ~ExternalSource () {}
	virtual void noteOn (int note, int vel) = 0;
	virtual void noteOff (int note) = 0;
	virtual void allOff () = 0;
	virtual void render (float *l, float *r, int n) = 0;	// adds nothing: writes n frames
};

// An insert effect on a track (built in, or an IPC plugin): processes a block in place.
struct Effect
{
	virtual ~Effect () {}
	virtual void process (float *l, float *r, int n) = 0;
	virtual void reset () {}
};

class Engine
{
public:
	Engine ();
	~Engine ();

	// ---- the UI side (allocates) ----
	bool init (const ms::SoundFont *sf, int sampleRate);	// the SoundFont must outlive the engine
	bool ensureTracks (int n);				// synths for n tracks
	bool post (const Command &c);				// false: the ring is full
	bool post (int type, long long a = 0, long long b = 0, int i = 0, int j = 0, void *p = 0, int k = 0, int l = 0);
	CompiledSong *retired ();				// a song to delete (0: none)
	void setInsert (int track, int slot, Effect *e);	// UI side, while the engine is stopped or with care (see .cpp)

	MixStrip mix[ENGINE_MAX_TRACKS];
	volatile float masterGain;

	// ---- read by the UI ----
	volatile long long position;		// the sample being played (the song's time)
	volatile int playing;
	volatile float peakL[ENGINE_MAX_TRACKS], peakR[ENGINE_MAX_TRACKS];
	volatile float masterPeakL, masterPeakR;
	volatile int activeVoices;
	volatile unsigned renderUs;		// the last block's cost (set by the host that times it)

	// ---- the engine side (never allocates) ----
	void render (float *left, float *right, int frames);

	const CompiledSong *song () const { return m_song; }
	int sampleRate () const { return m_rate; }

private:
	Engine (const Engine &);
	Engine &operator= (const Engine &);
	void drain ();
	void takeSong (CompiledSong *s);
	void retire (CompiledSong *s);
	void applyPrograms ();
	void applyAutomation (double beat);
	void dispatchUpTo (long long toSample);
	void allNotesOff ();
	void renderBlock (float *L, float *R, int n);
	void previewBlock (float *L, float *R, int n);

	const ms::SoundFont *m_sf;
	int m_rate;
	ms::Synthesizer *m_synth[ENGINE_MAX_TRACKS];
	int m_nSynth;
	ms::Synthesizer *m_preview;
	ExternalSource *m_ext[ENGINE_MAX_TRACKS];
	Effect *m_fx[ENGINE_MAX_TRACKS][4];
	float *m_bufL, *m_bufR, *m_mixL, *m_mixR;

	// the command ring (single producer: the UI; single consumer: the engine)
	enum { RING = 256 };
	Command m_ring[RING];
	volatile unsigned m_wr, m_rd;
	// the songs handed back
	enum { RETIRE = 8 };
	CompiledSong *volatile m_retire[RETIRE];

	CompiledSong *m_song;
	int m_nextEvent[ENGINE_MAX_TRACKS];	// the next event per track
	long long m_pos;			// the song's sample position
	bool m_playing, m_loop, m_metronome;
	long long m_loopA, m_loopB;
	int m_lastBeatClick;
	// the preview
	CompiledSong *m_prevSong;
	int m_prevEvent;
	long long m_prevPos;
};

// the final soft limiter (Koton's AudioFormat.SoftClip), and a float block -> s16 interleaved
float softClip (float x);
void toS16 (const float *l, const float *r, short *out, int n, float gain);

} // namespace kt

#endif
