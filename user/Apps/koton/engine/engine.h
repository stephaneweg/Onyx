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
enum { PDC_BUCKETS = 4, PDC_MAX = 16384 };	// the delay compensation: latencies at once, frames at most

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
	CMD_EXT_NOTE_ON,	// i = track, j = note, k = velocity: a live note on a plugin track (heard lead () late)
	CMD_EXT_NOTE_OFF,	// i = track, j = note
};

struct Command { int type; long long a, b; int i, j, k, l; void *p; };

// the mixer strip of a track, written by the UI, read by the engine at every block
struct MixStrip
{
	volatile float volume, pan;		// 0..1+, -1..+1
	volatile int mute, solo, reverb;	// reverb: CC91 0..127
	MixStrip () : volume (1), pan (0), mute (0), solo (0), reverb (40) {}
};

// Another source of a track's audio (an IPC instrument plugin: plug/plugshm.h), RENDERED AHEAD.
// The engine counts every frame it outputs on a STREAM CLOCK (frames since it was made; a seek or
// a loop does not move it). A source's notes are stamped on that clock lead () frames ahead of the
// frame being played -- its process renders them meanwhile -- and the engine reads the audio of a
// frame when it plays it. For each block of n frames at stream frame t, the engine calls:
//     noteOn / noteOff / allOff / reset (at, ...)    at >= t + lead ()  (a live note: t + lead ())
//     flushTo (t + lead () + n)                      every event before it has been given
//     render (l, r, n, t)                            the audio of the frames [t, t + n)
// The song's notes are dispatched by a second cursor running lead () frames ahead of the one
// played; a play or a seek waits lead () frames (a pre-roll) so that both start together. A
// source must not allocate, lock or call the kernel (the engine may run on an app core).
struct ExternalSource
{
	virtual ~ExternalSource () {}
	virtual int lead () const = 0;					// frames; fixed while attached
	virtual void noteOn (long long at, int note, int vel) = 0;
	virtual void noteOff (long long at, int note) = 0;
	virtual void allOff (long long at) = 0;				// every note released
	virtual void reset (long long at) = 0;				// every voice cut (play, seek, stop)
	virtual void flushTo (long long upTo) = 0;
	virtual bool render (float *l, float *r, int n, long long at) = 0;	// writes n frames; false: some
									// were not rendered in time (silence)
};

// An insert effect on a track (built in, or an IPC plugin): processes a block in place. An IPC
// effect (plug/plugshm.h) hands the block to its process and gives back the block of latency ()
// frames earlier (processAt knows the stream frame): the engine then delays every other track by
// as much -- plugin delay compensation -- so that the mix stays aligned. A built-in effect has no
// latency and is only called through process ().
struct Effect
{
	virtual ~Effect () {}
	virtual void process (float *l, float *r, int n) = 0;
	virtual void reset () {}
	virtual int latency () const { return 0; }
	virtual bool processAt (float *l, float *r, int n, long long at) { (void) at; process (l, r, n); return true; }	// false: late (silence)
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
	void setInsert (int track, int slot, Effect *e);	// UI side, while the engine is stopped or with care (see .cpp);
								// an effect with a latency () makes the compensation's buffers here
	// An effect or a source taken out (setInsert (t, s, 0), CMD_EXTERNAL 0) may still be in use until
	// the engine's next block: free it once `renders` moved by 2 since (or the engine is stopped).

	MixStrip mix[ENGINE_MAX_TRACKS];
	volatile float masterGain;

	// ---- read by the UI ----
	volatile long long position;		// the sample being played (the song's time)
	volatile int playing;
	volatile float peakL[ENGINE_MAX_TRACKS], peakR[ENGINE_MAX_TRACKS];
	volatile float masterPeakL, masterPeakR;
	volatile int activeVoices;
	volatile unsigned renderUs;		// the last block's cost (set by the host that times it)
	volatile unsigned renders;		// render () calls so far
	volatile long long streamClock;		// the stream clock (frames output so far)
	volatile unsigned pluginUnderruns;	// blocks an external source / an IPC effect did not have in time
	volatile int extLead;			// the external sources' lead in use (frames), 0: none
	volatile int pdcFrames;			// the delay compensation in use (the IPC effects' latency)

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
	// the external sources (rendered ahead) and the delay compensation
	void externalsChanged (int track, ExternalSource *old);
	void resyncExternals ();			// a play / seek: reset, mute, pre-roll
	void externalsOff (bool hard);			// every external source: allOff / reset at the ahead point
	void dispatchAhead (int n);
	int trackLatency (int track);
	int pdcBucket (int latency);
	long long heardPos ();

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
	// the external sources, rendered ahead; the delay compensation
	long long m_clock;			// the stream clock
	int m_lead, m_extCount;			// the lead in use (the sources' biggest), how many are attached
	long long m_aheadPos;			// the song position of the frame m_clock + m_lead
	int m_aheadEvent[ENGINE_MAX_TRACKS];	// its next event per track
	long long m_preroll;			// frames before the played cursor starts (a play / seek)
	long long m_extMuteUntil[ENGINE_MAX_TRACKS];	// a source's audio muted before this frame (stale after a reset)
	float m_extGain[ENGINE_MAX_TRACKS];
	float *m_pdcL[PDC_BUCKETS], *m_pdcR[PDC_BUCKETS];	// a delay ring per latency (PDC_MAX frames)
	float *m_busL[PDC_BUCKETS], *m_busR[PDC_BUCKETS];	// the block of the tracks with that latency
	int m_pdcLat[PDC_BUCKETS];		// the latency of each bucket (-1: free; bucket 0: none)
	int m_pdcD;				// the compensation in use
	long long m_pdcClock;			// the clock the rings were written up to
	struct Heard { long long clock, pos; };
	enum { HEARD = 512 };
	Heard m_heard[HEARD];			// (clock, song position) at each played block: the position heard
	int m_heardN;
	long long m_playFrom;
};

// the final soft limiter (Koton's AudioFormat.SoftClip), and a float block -> s16 interleaved
float softClip (float x);
void toS16 (const float *l, const float *r, short *out, int n, float gain);

} // namespace kt

#endif
