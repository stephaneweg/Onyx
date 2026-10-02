//
// ui/audio.h -- Koton's sound: the SoundFont (found on the card, else fetched once), the engine
// (engine/engine.h) and where it runs:
//
//   * on app core 2 when the kernel gives one (kapi_core_acquire): a loop that makes no kernel
//     call and allocates nothing -- it renders 256-frame blocks and writes them into the kernel's
//     mapped PCM ring (kapi_sound_map, v68) as long as the ring holds less than the target, then
//     spins a little (the core is ours);
//   * else on a real-time thread of the app (kapi_thread_create + kapi_thread_priority), the same
//     loop sleeping on the ring's read index (kapi_wait_word) between blocks;
//   * else (an old kernel, the PC simulator) from the UI's tick, with kapi_sound_write.
//
// The song is compiled on the UI thread when the document changed (compileSong is fast: a few ms
// for a song of a few minutes) and handed to the engine, which swaps it at a block boundary and
// hands the old one back (retired) to be freed here.
//
#ifndef _koton_audio_h
#define _koton_audio_h
#ifndef __aarch64__
#include <chrono>			// (the PC builds: the DSP thread's load)
#endif

#include "engine/engine.h"
#include "ui/doc.h"

namespace kui {

enum { AUDIO_BLOCK = 256, AUDIO_TARGET = 1024 };	// frames kept in the ring ahead of the output

struct AudioHost
{
	Engine engine;
	ms::SoundFont *sf;
	char sfPath[256], sfName[64], status[160];
	int core;				// the app core running the engine, -1 none
	int thread;				// ... or the thread, -1 none
	struct kapi_sound_ring *ring;
	bool haveSound, running;
	volatile int stop, stopped;
	unsigned compiledRev;			// the document revision the engine plays
	unsigned char *stack;
	float bufL[AUDIO_BLOCK], bufR[AUDIO_BLOCK];
	short pcm[AUDIO_BLOCK * 2];
	volatile unsigned long long busyUs, spanUs;	// the engine's load (core loop)
	volatile unsigned underruns;

	AudioHost () : sf (0), core (-1), thread (-1), ring (0), haveSound (false), running (false), stop (0), stopped (1), compiledRev (0),
		stack (0), busyUs (0), spanUs (1), underruns (0)
	{ sfPath[0] = sfName[0] = 0; snprintf (status, sizeof status, "no sound yet"); }

	// ---- the SoundFont --------------------------------------------------------------------------------------------
	static bool exists (const char *p) { void *f = kapi_open (p); if (f) { kapi_close (f); return true; } return false; }
	// the first .sf2 of a folder -> out
	static bool firstSf2 (const char *dir, char *out, int cap)
	{
		void *d = kapi_opendir (dir);
		if (!d) return false;
		struct kapi_dirent e; bool found = false;
		while (!found && kapi_readdir (d, &e) > 0)
		{
			int n = (int) strlen (e.name);
			if (n > 4 && (!strcmp (e.name + n - 4, ".sf2") || !strcmp (e.name + n - 4, ".SF2"))) { snprintf (out, cap, "%s/%s", dir, e.name); found = true; }
		}
		kapi_closedir (d);
		return found;
	}
	bool findSoundFont (const char *preferred)
	{
		if (preferred && preferred[0] && exists (preferred)) { snprintf (sfPath, sizeof sfPath, "%s", preferred); return true; }
		static const char *const dirs[] = { "SD:/res/soundfonts", "SD:/koton/soundfonts", "SD:/music/soundfonts", "SD:/music", "SD:/apps/koton.app" };
		for (unsigned i = 0; i < sizeof dirs / sizeof dirs[0]; i++) if (firstSf2 (dirs[i], sfPath, sizeof sfPath)) return true;
		return false;
	}
	bool loadSoundFont ()
	{
		void *f = kapi_open (sfPath);
		if (!f) { snprintf (status, sizeof status, "cannot open %s", sfPath); return false; }
		unsigned long long n = kapi_fsize64 (f);
		unsigned char *buf = (unsigned char *) malloc ((size_t) n);
		if (!buf) { kapi_close (f); snprintf (status, sizeof status, "not enough memory for the SoundFont (%llu MB)", n >> 20); return false; }
		unsigned long long got = 0;
		while (got < n)
		{
			unsigned want = n - got > (1u << 24) ? (1u << 24) : (unsigned) (n - got);
			int r = kapi_read (f, buf + got, want);
			if (r <= 0) break;
			got += (unsigned) r;
		}
		kapi_close (f);
		char err[128];
		sf = ms::soundfont_load (buf, (size_t) got, err, sizeof err);
		free (buf);						// (the SoundFont keeps its own copy)
		if (!sf) { snprintf (status, sizeof status, "SoundFont: %s", err); return false; }
		snprintf (sfName, sizeof sfName, "%s", ms::soundfont_name (sf));
		// its names: the GM programs (bank 0) and the drum kits (bank 128), by patch
		g_kitPrograms.clear (); g_names.nKits = 0;
		int cnt = ms::soundfont_preset_count (sf);
		for (int patch = 0; patch < 128; patch++)
			for (int i = 0; i < cnt; i++)
			{
				int b = ms::soundfont_preset_bank (sf, i), pt = ms::soundfont_preset_patch (sf, i);
				if (b == 0 && pt == patch) snprintf (g_names.gm[patch], sizeof g_names.gm[patch], "%s", ms::soundfont_preset_name (sf, i));
				if (b == 128 && pt == patch && g_names.nKits < 64)
				{
					snprintf (g_names.kit[g_names.nKits], sizeof g_names.kit[0], "%s", ms::soundfont_preset_name (sf, i));
					g_names.nKits++;
					g_kitPrograms.push (patch);
				}
			}
		return true;
	}

	// ---- starting ---------------------------------------------------------------------------------------------------------
	bool start (const char *preferredSf)
	{
		if (!findSoundFont (preferredSf)) { snprintf (status, sizeof status, "no SoundFont: install the package GeneralUser GS, or put a .sf2 in SD:/res/soundfonts"); return false; }
		if (!loadSoundFont ()) return false;
		engine.init (sf, SOUND_RATE);
		engine.ensureTracks (8);
		if (kapi_sound_acquire () != 1) { snprintf (status, sizeof status, "the sound output is used by another app"); return false; }
		haveSound = true;
		kapi_sound_config (256, 2);				// ~12 ms in the kernel
		ring = kapi_sound_map ();
		stop = 0; stopped = 0;
		if (ring)
		{
			stack = new unsigned char[256 * 1024];
			core = kapi_core_acquire ();
			if (core >= 0 && kapi_core_run (core, coreLoop, this, stack + 256 * 1024) != 0) { kapi_core_release (core); core = -1; }
			if (core < 0)
			{
				thread = kapi_thread_create (threadLoop, this, 256 * 1024, "koton audio");
				if (thread >= 0) kapi_thread_priority (thread, 1);
			}
		}
		running = true;
		snprintf (status, sizeof status, "%s - engine on %s", sfName[0] ? sfName : "SoundFont",
			  core >= 0 ? "core 2" : thread >= 0 ? "a real-time thread" : "the UI thread");
		return true;
	}
	void shutdown ()
	{
		if (!running) { if (sf) { ms::soundfont_free (sf); sf = 0; } return; }
		stop = 1;
		if (core >= 0 || thread >= 0)
		{
			unsigned t0 = kapi_get_ticks ();
			while (!stopped && kapi_get_ticks () - t0 < 100) kapi_pump_wait (5);
		}
		if (core >= 0) { kapi_core_release (core); core = -1; }
		if (thread >= 0) { int code; kapi_thread_join (thread, 1000, &code); thread = -1; }
		if (haveSound) kapi_sound_release ();
		running = false;
		for (CompiledSong *s; (s = engine.retired ()); ) delete s;
		// (the engine is destroyed with the host; the SoundFont after it: see ~AudioHost)
	}
	~AudioHost () { }
	void freeSoundFont () { if (sf) { ms::soundfont_free (sf); sf = 0; } }

	// ---- the render loops ------------------------------------------------------------------------------------------
	static inline unsigned long long nowUs ()
	{
#ifdef __aarch64__
		unsigned long long c, f;
		__asm__ volatile ("mrs %0, cntpct_el0" : "=r" (c));
		__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (f));
		return f ? c * 1000000ull / f : 0;
#else
		return (unsigned long long) std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now ().time_since_epoch ()).count ();
#endif
	}
	static inline void cpuRelax ()
	{
#ifdef __aarch64__
		__asm__ volatile ("yield");
#endif
	}
	void renderOne ()
	{
		unsigned long long t0 = nowUs ();
		engine.render (bufL, bufR, AUDIO_BLOCK);
		toS16 (bufL, bufR, pcm, AUDIO_BLOCK, 1.0f);
		kapi_sound_ring_write (ring, pcm, AUDIO_BLOCK);
		unsigned long long t1 = nowUs ();
		busyUs = busyUs + (t1 - t0);
		engine.renderUs = (unsigned) (t1 - t0);
	}
	static void coreLoop (void *arg)			// on the app core: no kapi call, no allocation
	{
		AudioHost *h = (AudioHost *) arg;
		unsigned long long w0 = nowUs ();
		unsigned lastDry = h->ring->dry;
		while (!h->stop)
		{
			unsigned fill = h->ring->wr - h->ring->rd;
			if (fill + AUDIO_BLOCK <= AUDIO_TARGET) h->renderOne ();
			else for (volatile int i = 0; i < 2000; i++) cpuRelax ();
			unsigned long long now = nowUs ();
			if (now - w0 > 500000) { h->spanUs = now - w0; w0 = now; h->busyUs = 0; }
			if (h->ring->dry != lastDry) { lastDry = h->ring->dry; h->underruns++; }
		}
		__sync_synchronize ();
		h->stopped = 1;
	}
	static int threadLoop (void *arg)
	{
		AudioHost *h = (AudioHost *) arg;
		unsigned long long w0 = nowUs ();
		while (!h->stop)
		{
			unsigned fill = h->ring->wr - h->ring->rd;
			if (fill + AUDIO_BLOCK <= AUDIO_TARGET) { h->renderOne (); continue; }
			kapi_wait_word (&h->ring->rd, h->ring->rd, 3);
			unsigned long long now = nowUs ();
			if (now - w0 > 500000) { h->spanUs = now - w0; w0 = now; h->busyUs = 0; }
		}
		h->stopped = 1;
		return 0;
	}
	// the UI's tick: the fallback render (no ring), the retired songs freed
	void tick ()
	{
		for (CompiledSong *s; (s = engine.retired ()); ) delete s;
		if (!running || ring) return;
		// no mapped ring: push through kapi_sound_write what the output has room for
		unsigned rate, freeF, owner;
		if (kapi_sound_status (&rate, &freeF, &owner) <= 0) return;
		int budget = 8;
		while (freeF >= AUDIO_BLOCK && budget-- > 0)
		{
			engine.render (bufL, bufR, AUDIO_BLOCK);
			toS16 (bufL, bufR, pcm, AUDIO_BLOCK, 1.0f);
			kapi_sound_write (pcm, AUDIO_BLOCK);
			freeF -= AUDIO_BLOCK;
		}
	}
	int loadPercent () const { return spanUs ? (int) (busyUs * 100 / spanUs) : 0; }
	int latencyMs () const { return ring ? (AUDIO_TARGET + 3 * 256) * 1000 / SOUND_RATE : 120; }

	// ---- the song ----------------------------------------------------------------------------------------------------
	// compile the document if it changed since, give it to the engine (the mixer follows the tracks)
	void sync (Doc &d)
	{
		if (!sf) return;
		for (int t = 0; t < d.p.tracks.size () && t < ENGINE_MAX_TRACKS; t++)
		{
			const Track &tr = d.p.tracks[t];
			engine.mix[t].volume = (float) tr.volume; engine.mix[t].pan = (float) tr.pan;
			engine.mix[t].mute = tr.mute; engine.mix[t].solo = tr.solo; engine.mix[t].reverb = tr.reverbSend ();
		}
		if (compiledRev == d.revision) return;
		CompiledSong *cs = compileSong (d.p, SOUND_RATE);
		engine.ensureTracks (cs->tracks.size ());
		engine.post (CMD_SONG, 0, 0, 0, 0, cs);
		compiledRev = d.revision;
	}
	void play (Doc &d, double beat)
	{
		sync (d);
		CompiledSong tmp; (void) tmp;
		long long s = engine.song () ? engine.song ()->sampleAtBeat (beat) : 0;
		// (the song just posted is not the engine's yet: compute from the document's tempo)
		if (!engine.song () || compiledRev != d.revision) s = (long long) (beatToSeconds (d.p, beat) * SOUND_RATE);
		engine.post (CMD_PLAY, s);
	}
	void stopPlay () { engine.post (CMD_STOP); }
	void seek (Doc &d, double beat) { engine.post (CMD_SEEK, (long long) (beatToSeconds (d.p, beat) * SOUND_RATE)); }
	void loop (Doc &d, bool on, double a, double b)
	{
		engine.post (CMD_LOOP, (long long) (beatToSeconds (d.p, a) * SOUND_RATE), (long long) (beatToSeconds (d.p, b) * SOUND_RATE), on ? 1 : 0);
	}
	static double beatToSeconds (const Project &p, double beat)
	{
		// the tempo map, as the compiler builds it (slice by slice)
		double s = 0; int slices = (int) (beat * CSPB);
		for (int i = 0; i < slices; i++) s += 60.0 / p.bpmAt (i / (double) CSPB) / CSPB;
		s += (beat * CSPB - slices) * 60.0 / p.bpmAt (beat) / CSPB;
		return s;
	}
	double playheadBeat () const { const CompiledSong *s = engine.song (); return s ? s->beatAtSample (engine.position) : 0; }
	bool isPlaying () const { return engine.playing != 0; }

	// a preview (an editor's Listen): a module or a riff looping on the preview synth
	void preview (CompiledSong *cs) { if (sf) engine.post (CMD_PREVIEW, 0, 0, 0, 0, cs); else delete cs; }
	void stopPreview () { if (sf) engine.post (CMD_PREVIEW, 0, 0, 0, 0, 0); }
	void noteOn (int note, int vel, int program, bool drum) { if (sf) engine.post (CMD_NOTE_ON, 0, 0, note, vel, 0, program, drum ? 1 : 0); }
	void noteOff (int note) { if (sf) engine.post (CMD_NOTE_OFF, 0, 0, note); }
	void metronome (bool on) { engine.post (CMD_METRONOME, 0, 0, on ? 1 : 0); }

	// ---- the WAV export: the song rendered off line by a second engine (the playing one untouched)
	bool exportWav (Doc &d, const char *path, void (*progress) (int pct))
	{
		if (!sf) return false;
		Engine *e = new Engine;
		e->init (sf, SOUND_RATE);
		CompiledSong *cs = compileSong (d.p, SOUND_RATE);
		e->ensureTracks (cs->tracks.size ());
		for (int t = 0; t < d.p.tracks.size () && t < ENGINE_MAX_TRACKS; t++)
		{
			e->mix[t].volume = (float) d.p.tracks[t].volume; e->mix[t].pan = (float) d.p.tracks[t].pan;
			e->mix[t].mute = d.p.tracks[t].mute; e->mix[t].solo = d.p.tracks[t].solo; e->mix[t].reverb = d.p.tracks[t].reverbSend ();
		}
		long long frames = cs->totalSamples () + 2 * SOUND_RATE;
		e->post (CMD_SONG, 0, 0, 0, 0, cs);
		e->post (CMD_PLAY, 0);
		void *out = kapi_file_out (path, 0);
		if (!out) { delete e; return false; }
		unsigned char hdr[44];
		unsigned data = (unsigned) (frames * 4), riff = 36 + data, rate = SOUND_RATE, br = SOUND_RATE * 4;
		memcpy (hdr, "RIFF", 4); memcpy (hdr + 4, &riff, 4); memcpy (hdr + 8, "WAVEfmt ", 8);
		unsigned fl = 16; unsigned short pcmf = 1, ch = 2, al = 4, bits = 16;
		memcpy (hdr + 16, &fl, 4); memcpy (hdr + 20, &pcmf, 2); memcpy (hdr + 22, &ch, 2); memcpy (hdr + 24, &rate, 4);
		memcpy (hdr + 28, &br, 4); memcpy (hdr + 32, &al, 2); memcpy (hdr + 34, &bits, 2); memcpy (hdr + 36, "data", 4); memcpy (hdr + 40, &data, 4);
		kapi_stream_write (out, hdr, 44);
		enum { N = 4096 };
		float *L = new float[N], *R = new float[N]; short *s = new short[N * 2];
		int lastPct = -1;
		for (long long done = 0; done < frames; )
		{
			int n = frames - done > N ? N : (int) (frames - done);
			e->render (L, R, n);
			toS16 (L, R, s, n, 1.0f);
			kapi_stream_write (out, s, n * 4);
			done += n;
			int pct = (int) (done * 100 / frames);
			if (progress && pct != lastPct) { lastPct = pct; progress (pct); }
		}
		kapi_stream_close (out);
		delete [] L; delete [] R; delete [] s;
		delete e;
		return true;
	}
};

extern AudioHost g_audio;

} // namespace kui

#endif
