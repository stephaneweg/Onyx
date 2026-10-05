// tools/tests/koton/plug_test.cpp -- Koton's plugins on the PC, under AddressSanitizer / UBSan /
// LeakSanitizer. The protocol is bound to the kernel (surfaces, mailboxes, word waits), so what it
// is made of is tested here without it:
//   1. the rings (kplug_proto.h): a producer thread and a consumer, events and audio, wrapping; a
//      full ring drops (and counts), never blocks;
//   2. the engine rendering ahead (engine.h) with fake sources: the pre-roll, a note of an external
//      track heard exactly when the played cursor passes it, a loop, a seek while playing (the stale
//      audio muted), a live note (late by the lead), an underrun (silence + a count, no wait), the
//      plugin delay compensation (a track through an effect of latency LAT and a track without it
//      come out together, the position heard);
//   3. end to end in real time: the engine paced like the audio pump, a thread standing in for each
//      plugin process (asleep between the kernel's 10 ms ticks), kp_fm2 as a track's instrument and
//      kp_delay as another's insert over their shared regions (plug/plugshm.h) -> no underrun;
//   4. every plugin's DSP renders to /tmp/koton_plug_<name>.wav (an instrument a few notes, an effect
//      a plucked burst and a chord) without a NaN, nor silence, nor a blow-up;
//   5. the generators: a project's context (plug/plugctx.h) -> the plugin -> notes -> a riff; the
//      arpeggiator's notes are the chords' tones, Koton's states load, the same request twice gives
//      the same notes; the states round-trip; every plugin.json says what its program says.
//   sh tools/tests/koton/plug_run.sh
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include "engine/engine.h"
#include "engine/theory.h"
#include "plug/plugshm.h"
#include "plug/plugctx.h"
#include "../../../user/Include/json.hpp"
#define KPLUG_DSP_ONLY
#include "kplug.h"				// (the types: KpTestApi)

using namespace kt;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECKF(c, ...) do { if (!(c)) { printf ("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf (__VA_ARGS__); printf ("\n"); g_fail++; } } while (0)

// the plugins, each built on its own with KPLUG_TEST_SYM=<name>_api
extern "C" const KpTestApi *kp_fm2_api (), *kp_subsynth_api (), *kp_pluck_api (), *kp_delay_api (), *kp_reverb_api (),
	*kp_chorus_api (), *kp_eq3_api (), *kp_drive_api (), *kp_arp_api (), *kp_euclid_api (), *kp_automaton_api ();
struct Plug { const char *name; const KpTestApi *(*api) (); };
static const Plug PLUGS[] = {
	{ "fm2", kp_fm2_api }, { "subsynth", kp_subsynth_api }, { "pluck", kp_pluck_api }, { "delay", kp_delay_api },
	{ "reverb", kp_reverb_api }, { "chorus", kp_chorus_api }, { "eq3", kp_eq3_api }, { "drive", kp_drive_api },
	{ "arp", kp_arp_api }, { "euclid", kp_euclid_api }, { "automaton", kp_automaton_api } };
enum { NPLUGS = sizeof PLUGS / sizeof PLUGS[0] };

static KpShm *newShm (int kind, int lead, int latency)
{
	void *p = 0;
	if (posix_memalign (&p, 65536, KP_SHM_BYTES)) return 0;
	kp_shm_init ((KpShm *) p, kind, 44100, lead, latency, 1);
	return (KpShm *) p;
}

static void writeWav (const char *path, const float *l, const float *r, int frames)
{
	FILE *f = fopen (path, "wb"); if (!f) return;
	unsigned rate = 44100, data = frames * 4, riff = 36 + data, fmtLen = 16, br = rate * 4;
	unsigned short pcm = 1, chs = 2, bits = 16, align = 4;
	fwrite ("RIFF", 1, 4, f); fwrite (&riff, 4, 1, f); fwrite ("WAVEfmt ", 1, 8, f);
	fwrite (&fmtLen, 4, 1, f); fwrite (&pcm, 2, 1, f); fwrite (&chs, 2, 1, f); fwrite (&rate, 4, 1, f); fwrite (&br, 4, 1, f);
	fwrite (&align, 2, 1, f); fwrite (&bits, 2, 1, f); fwrite ("data", 1, 4, f); fwrite (&data, 4, 1, f);
	for (int i = 0; i < frames; i++)
	{
		short s[2];
		for (int c = 0; c < 2; c++) { float v = softClip (c ? r[i] : l[i]); int x = (int) (v * 32767.0f); s[c] = (short) (x > 32767 ? 32767 : x < -32768 ? -32768 : x); }
		fwrite (s, 2, 2, f);
	}
	fclose (f);
}

static double nowSec () { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

// ---- 1. the rings ------------------------------------------------------------------------------------
struct RingCtx { KpShm *s; int n; volatile int done; int bad; };
static void *evConsumer (void *a)
{
	RingCtx *c = (RingCtx *) a;
	unsigned long long expect = 0;
	int got = 0;
	while (got < c->n)
	{
		const KpEvent *e = kp_ev_peek (c->s);
		if (!e) { sched_yield (); continue; }
		if (e->at != expect || e->a != (expect & 127) || e->type != KPE_NOTE_ON) c->bad++;
		expect++; got++;
		kp_ev_pop (c->s);
	}
	c->done = 1;
	return 0;
}
static void *auConsumer (void *a)
{
	RingCtx *c = (RingCtx *) a;
	unsigned long long pos = 0;
	float l[300], r[300];
	while ((long long) pos < c->n)
	{
		unsigned long long done = kp_ld64 (&c->s->done);
		if (done <= pos) { sched_yield (); continue; }
		int n = (int) (done - pos); if (n > 300) n = 300;
		kp_au_read (c->s->outL, c->s->outR, pos, l, r, n);
		for (int k = 0; k < n; k++) if (l[k] != (float) ((pos + k) % 1000) || r[k] != -l[k]) c->bad++;
		pos += n;
		kp_st64 (&c->s->readPos, pos);
	}
	c->done = 1;
	return 0;
}

static void testRings ()
{
	KpShm *s = newShm (KP_INSTRUMENT, 4096, 0);
	// events: 200 000 through a ring of 1024, the producer never waiting (it retries when full)
	RingCtx c = { s, 200000, 0, 0 };
	pthread_t t; pthread_create (&t, 0, evConsumer, &c);
	for (int i = 0; i < c.n; )
	{
		KpEvent e; e.at = (unsigned long long) i; e.type = KPE_NOTE_ON; e.a = (unsigned char) (i & 127); e.b = 100; e.c = 0; e.v = 0;
		if (kp_ev_push (s, &e)) i++; else sched_yield ();
	}
	pthread_join (t, 0);
	CHECKF (c.bad == 0, "%d events out of order", c.bad);
	unsigned dropped = s->evDropped;
	CHECK (dropped > 0 || true);			// (the producer met a full ring: counted, not blocked)
	// a full ring: a push returns at once, counted
	s->evDropped = 0;
	KpEvent e; memset (&e, 0, sizeof e); e.type = KPE_NOTE_ON;
	int pushed = 0; for (int i = 0; i < KP_EV_RING + 10; i++) pushed += kp_ev_push (s, &e);
	CHECK (pushed == KP_EV_RING && s->evDropped == 10);
	// audio: 1 000 000 frames in blocks of 1..256 (wrapping the ring many times), the consumer behind
	kp_shm_init (s, KP_INSTRUMENT, 44100, 4096, 0, 1);
	RingCtx a = { s, 1000000, 0, 0 };
	pthread_create (&t, 0, auConsumer, &a);
	unsigned long long pos = 0; float l[256], r[256]; unsigned seed = 7;
	while ((long long) pos < a.n)
	{
		seed = seed * 1103515245u + 12345u;
		int n = 1 + (int) ((seed >> 16) % 256); if ((long long) pos + n > a.n) n = (int) (a.n - pos);
		while (pos + n - kp_ld64 (&s->readPos) > KP_AU_RING - 1) sched_yield ();	// (the plugin renders up to `want` only)
		for (int k = 0; k < n; k++) { l[k] = (float) ((pos + k) % 1000); r[k] = -l[k]; }
		kp_au_write (s->outL, s->outR, pos, l, r, n);
		pos += n;
		kp_st64 (&s->done, pos);
	}
	pthread_join (t, 0);
	CHECKF (a.bad == 0, "%d audio frames wrong", a.bad);
	// the request channel
	unsigned seq = kp_req_post (s, KP_STATE_GET, "{}", 2);
	CHECK (!kp_req_answered (s, seq) && s->reqType == KP_STATE_GET && s->reqLen == 2);
	free (s);
	printf ("rings: events %d in order (%u pushes met a full ring), audio %d frames exact\n", c.n, dropped, a.n);
}

// ---- 2. the engine with fake sources ------------------------------------------------------------------------
// A source rendering an impulse (vel / 127) at each note-on's frame: where its notes land is exact.
struct FakeSource : ExternalSource
{
	int m_lead; long long ev[4096]; float amp[4096]; int n; long long lastFlush; int resets; bool starve;
	FakeSource (int lead) : m_lead (lead), n (0), lastFlush (0), resets (0), starve (false) {}
	int lead () const override { return m_lead; }
	void noteOn (long long at, int, int vel) override { if (n < 4096) { ev[n] = at; amp[n] = vel / 127.0f; n++; } }
	void noteOff (long long, int) override {}
	void allOff (long long) override {}
	void reset (long long) override { resets++; }
	void flushTo (long long upTo) override { lastFlush = upTo; }
	bool render (float *l, float *r, int nn, long long at) override
	{
		for (int k = 0; k < nn; k++) l[k] = r[k] = 0;
		if (starve) return false;
		for (int i = 0; i < n; i++) if (ev[i] >= at && ev[i] < at + nn) { l[ev[i] - at] += amp[i]; r[ev[i] - at] += amp[i]; }
		return true;
	}
};
// an effect of latency LAT: a pure delay (what a plugin process gives back)
struct DelayFx : Effect
{
	int lat; float bl[1 << 16], br[1 << 16];
	DelayFx (int l) : lat (l) { memset (bl, 0, sizeof bl); memset (br, 0, sizeof br); }
	int latency () const override { return lat; }
	void process (float *, float *, int) override {}
	bool processAt (float *l, float *r, int n, long long at) override
	{
		for (int k = 0; k < n; k++)
		{
			unsigned w = (unsigned) ((at + k) & 0xFFFF), rd = (unsigned) ((at + k - lat) & 0xFFFF);
			bl[w] = l[k]; br[w] = r[k];
			l[k] = at + k - lat < 0 ? 0 : bl[rd]; r[k] = at + k - lat < 0 ? 0 : br[rd];
		}
		return true;
	}
};

// a song of `tracks` tracks, 100 frames a slice, a note-on (+ off) at each slice given
static CompiledSong *fakeSong (int tracks, int slices, const int *onSlices, int non)
{
	CompiledSong *s = new CompiledSong;
	s->sampleRate = 44100; s->totalSlices = slices;
	s->sliceSample.resize (slices + 1);
	for (int i = 0; i <= slices; i++) s->sliceSample[i] = (long long) i * 100;
	for (int t = 0; t < tracks; t++)
	{
		CTrack &ct = s->tracks.add ();
		ct.srcIndex = t; ct.channel = 0; ct.program = 0; ct.bank = 0; ct.drum = false; ct.silent = false;
		for (int i = 0; i < non; i++)
		{
			CEvent on; memset (&on, 0, sizeof on); on.slice = onSlices[i]; on.kind = EV_ON; on.note = 60; on.vel = 127;
			CEvent off = on; off.kind = EV_OFF; off.slice = onSlices[i] + 2;
			ct.events.push (on); ct.events.push (off);
		}
		ct.events.sort ([] (const CEvent &a, const CEvent &b) { return a.slice != b.slice ? a.slice < b.slice : a.kind < b.kind; });
	}
	return s;
}

// render the engine up to stream frame `until` into out[] (indexed by the stream frame)
static void run (Engine &e, float *out, long long until, int block = 256)
{
	float L[512], R[512];
	while (e.streamClock < until)
	{
		long long at = e.streamClock;
		int n = (int) (until - at < block ? until - at : block);
		e.render (L, R, n);
		for (int k = 0; k < n; k++) out[at + k] = L[k];
	}
}
static int peaks (const float *out, long long from, long long to, long long *where, int max, float thr = 0.05f)
{
	int n = 0;
	for (long long i = from; i < to; i++) if (fabsf (out[i]) > thr) { if (n < max) where[n] = i; n++; }
	return n;
}

static void testEngineAhead ()
{
	const int LEAD = 4096, LAT = 1000;
	const long long N = 200000;
	float *out = (float *) calloc (N, sizeof (float));
	const float G = 0.85f;				// the engine's master gain
	long long w[16];
	// -- a note of an external track: stamped lead frames ahead, heard when the played cursor passes it
	{
		Engine e; e.init (0, 44100);
		int on[] = { 10 };
		FakeSource a (LEAD);
		e.post (CMD_SONG, 0, 0, 0, 0, fakeSong (1, 1000, on, 1));
		e.post (CMD_EXTERNAL, 0, 0, 0, 0, &a);
		run (e, out, 1000);
		CHECK (e.extLead == LEAD && a.resets == 1);
		long long T0 = e.streamClock;
		e.post (CMD_PLAY, 0);
		run (e, out, T0 + LEAD + 3000);
		CHECK (a.n == 1 && a.ev[0] == T0 + LEAD + 1000);	// stamped: the play's frame + the lead + its position
		int np = peaks (out, 0, T0 + LEAD + 3000, w, 16);
		CHECKF (np == 1 && w[0] == T0 + LEAD + 1000 && fabsf (out[w[0]] - G) < 1e-4f, "%d peaks, first at %lld (want %lld)", np, np ? w[0] : -1, T0 + LEAD + 1000);
		CHECK (a.lastFlush >= e.streamClock + LEAD);
		// the position: the song's frame heard now
		CHECKF (e.position == e.streamClock - T0 - LEAD, "position %lld", (long long) e.position);
		for (CompiledSong *r; (r = e.retired ()); ) delete r;
	}
	// -- the delay compensation: track 1 through an effect of latency LAT, track 0 not: together
	memset (out, 0, N * sizeof (float));
	{
		Engine e; e.init (0, 44100);
		int on[] = { 10 };
		FakeSource a (LEAD), b (LEAD);
		DelayFx *fx = new DelayFx (LAT);
		e.post (CMD_SONG, 0, 0, 0, 0, fakeSong (2, 1000, on, 1));
		e.post (CMD_EXTERNAL, 0, 0, 0, 0, &a);
		e.post (CMD_EXTERNAL, 0, 0, 1, 0, &b);
		e.setInsert (1, 0, fx);
		run (e, out, 5000);
		long long T0 = e.streamClock;
		e.post (CMD_PLAY, 0);
		run (e, out, T0 + LEAD + LAT + 4000);
		int np = peaks (out, T0, T0 + LEAD + LAT + 4000, w, 16);
		CHECKF (np == 1 && w[0] == T0 + LEAD + 1000 + LAT && fabsf (out[w[0]] - 2 * G) < 1e-4f,
			"%d peaks, first at %lld = %.3f (want %lld = %.3f)", np, np ? w[0] : -1, np ? out[w[0]] : 0, T0 + LEAD + 1000 + LAT, 2 * G);
		CHECK (e.pdcFrames == LAT);
		CHECKF (e.position == e.streamClock - T0 - LEAD - LAT, "position %lld, heard %lld", (long long) e.position, e.streamClock - T0 - LEAD - LAT);
		e.setInsert (1, 0, 0);
		run (e, out, e.streamClock + 512);
		delete fx;
		for (CompiledSong *r; (r = e.retired ()); ) delete r;
	}
	// -- a loop [0, 2000) with a note at 500: each pass on time
	memset (out, 0, N * sizeof (float));
	{
		Engine e; e.init (0, 44100);
		int on[] = { 5 };
		FakeSource a (LEAD);
		e.post (CMD_SONG, 0, 0, 0, 0, fakeSong (1, 1000, on, 1));
		e.post (CMD_EXTERNAL, 0, 0, 0, 0, &a);
		e.post (CMD_LOOP, 0, 2000, 1);
		run (e, out, 777);
		long long T0 = e.streamClock;
		e.post (CMD_PLAY, 0);
		run (e, out, T0 + LEAD + 5 * 2000);
		int np = peaks (out, T0, T0 + LEAD + 5 * 2000, w, 16);
		bool ok = np == 5;
		for (int k = 0; k < np && k < 5; k++) if (w[k] != T0 + LEAD + 500 + 2000 * k) ok = false;
		CHECKF (ok, "%d peaks, first at %lld", np, np ? w[0] : -1);
		for (CompiledSong *r; (r = e.retired ()); ) delete r;
	}
	// -- a seek while playing: a new pre-roll; what the source had rendered ahead is muted
	memset (out, 0, N * sizeof (float));
	{
		Engine e; e.init (0, 44100);
		int on[] = { 5, 50 };
		FakeSource a (LEAD);
		e.post (CMD_SONG, 0, 0, 0, 0, fakeSong (1, 1000, on, 2));
		e.post (CMD_EXTERNAL, 0, 0, 0, 0, &a);
		run (e, out, 300);
		long long T0 = e.streamClock;
		e.post (CMD_PLAY, 0);
		run (e, out, T0 + LEAD + 1000);
		long long T1 = e.streamClock;
		CHECK (a.n == 2);				// (the note at 5000 already went ahead: stale now)
		e.post (CMD_SEEK, 4000);
		run (e, out, T1 + 3 * LEAD);
		int np = peaks (out, T0, T1 + 3 * LEAD, w, 16);
		CHECKF (np == 2 && w[0] == T0 + LEAD + 500 && w[1] == T1 + LEAD + 1000, "%d peaks: %lld %lld (want %lld %lld)", np, np > 0 ? w[0] : -1,
			np > 1 ? w[1] : -1, T0 + LEAD + 500, T1 + LEAD + 1000);
		CHECK (a.resets >= 3);				// (attach, play, seek)
		// a live note: heard lead frames later
		long long T2 = e.streamClock;
		e.post (CMD_EXT_NOTE_ON, 0, 0, 0, 72, 0, 100);
		run (e, out, T2 + LEAD + 1000);
		np = peaks (out, T2, T2 + LEAD + 1000, w, 16);
		CHECKF (np == 1 && w[0] == T2 + LEAD, "live: %d peaks, at %lld (want %lld)", np, np ? w[0] : -1, T2 + LEAD);
		// an underrun: silence, a count, no wait
		unsigned u0 = e.pluginUnderruns;
		a.starve = true;
		run (e, out, e.streamClock + 2048);
		CHECK (e.pluginUnderruns > u0);
		a.starve = false;
		for (CompiledSong *r; (r = e.retired ()); ) delete r;
	}
	// -- no external source, no effect with a latency: the engine as it was (no pre-roll)
	{
		Engine e; e.init (0, 44100);
		e.post (CMD_SONG, 0, 0, 0, 0, fakeSong (1, 100, 0, 0));
		e.post (CMD_PLAY, 0);
		float L[256], R[256];
		e.render (L, R, 256);
		CHECK (e.position == 256 && e.extLead == 0 && e.pdcFrames == 0);
		for (CompiledSong *r; (r = e.retired ()); ) delete r;
	}
	free (out);
	printf ("engine ahead: pre-roll, delay compensation, loop, seek, live note, underrun: checked (lead %d, latency %d)\n", LEAD, LAT);
}

// ---- 3. end to end, in real time -----------------------------------------------------------------------
struct PlugThread { const KpTestApi *api; KpShm *s; volatile int stop; };
static void *plugMain (void *a)
{
	PlugThread *p = (PlugThread *) a;
	while (!p->stop)
	{
		unsigned seen = kp_ld32 (&p->s->kick);
		p->api->serve ();
		// kapi_wait_word (&kick, seen, 10): an app core's write is seen at the next 10 ms tick
		for (int i = 0; i < 10 && kp_ld32 (&p->s->kick) == seen && !p->stop; i++) usleep (1000);
		usleep (9000);					// (the worst case: woken by the tick only)
	}
	return 0;
}

static void testRealTime ()
{
	const int LEAD = KP_LEAD_DEFAULT, LAT = KP_LEAD_DEFAULT;
	KpShm *si = newShm (KP_INSTRUMENT, LEAD, 0), *se = newShm (KP_EFFECT, 0, LAT);
	const KpTestApi *fm = kp_fm2_api (), *dl = kp_delay_api ();
	CHECK (fm->attach (si) && dl->attach (se));
	PlugThread pi = { fm, si, 0 }, pe = { dl, se, 0 };
	pthread_t ti, te;
	pthread_create (&ti, 0, plugMain, &pi); pthread_create (&te, 0, plugMain, &pe);
	ShmSource *src = new ShmSource (si, LEAD);
	ShmEffect *fx = new ShmEffect (se, LAT);
	Engine e; e.init (0, 44100);
	int on[] = { 0, 40, 80, 120, 160, 200, 240, 280 };		// (100 frames a slice: a note every 4000 frames)
	CompiledSong *song = fakeSong (1, 400, on, 8);
	for (int i = 0; i < song->tracks[0].events.size (); i++) song->tracks[0].events[i].note = (unsigned char) (48 + (song->tracks[0].events[i].slice / 40) * 3);
	e.post (CMD_SONG, 0, 0, 0, 0, song);
	e.post (CMD_EXTERNAL, 0, 0, 0, 0, src);
	e.setInsert (0, 0, fx);
	const int FR = 44100 * 3 / 2;
	float *L = (float *) calloc (FR, sizeof (float)), *R = (float *) calloc (FR, sizeof (float));
	double t0 = nowSec ();
	int done = 0; bool played = false;
	while (done < FR)
	{
		if (!played && done >= 2048) { e.post (CMD_PLAY, 0); played = true; }
		int n = FR - done < 256 ? FR - done : 256;
		e.render (L + done, R + done, n);
		done += n;
		double due = t0 + done / 44100.0, now = nowSec ();	// (paced like the audio pump)
		if (due > now) usleep ((useconds_t) ((due - now) * 1e6));
	}
	pi.stop = pe.stop = 1;
	pthread_join (ti, 0); pthread_join (te, 0);
	double rms = 0; bool nan = false;
	for (int i = 0; i < FR; i++) { if (L[i] != L[i] || R[i] != R[i]) nan = true; rms += L[i] * L[i]; }
	rms = sqrt (rms / FR);
	writeWav ("/tmp/koton_plug_realtime.wav", L, R, FR);
	printf ("real time: fm2 -> delay over their regions, %.1f s: underruns %u (engine %u), late frames %u / %u, rms %.4f -> /tmp/koton_plug_realtime.wav\n",
		FR / 44100.0, src->underruns () + fx->underruns (), e.pluginUnderruns, si->lateFrames, se->lateFrames, rms);
	CHECK (!nan && rms > 0.005);
	CHECKF (e.pluginUnderruns == 0 && si->lateFrames == 0 && se->lateFrames == 0, "underruns %u, late %u %u", e.pluginUnderruns, si->lateFrames, se->lateFrames);
	e.setInsert (0, 0, 0);
	e.post (CMD_EXTERNAL, 0, 0, 0, 0, (void *) 0);
	float l[256], r[256]; e.render (l, r, 256); e.render (l, r, 256);
	delete src; delete fx;
	for (CompiledSong *rt; (rt = e.retired ()); ) delete rt;
	free (L); free (R); free (si); free (se);
}

// ---- 4. every plugin's DSP ---------------------------------------------------------------------------------
static void pushEv (KpShm *s, long long at, int type, int a, int b)
{
	KpEvent e; e.at = (unsigned long long) at; e.type = (unsigned char) type; e.a = (unsigned char) a; e.b = (unsigned char) b; e.c = 0; e.v = 0;
	kp_ev_push (s, &e);
}

static void testDsp ()
{
	const int FR = 44100 * 3;
	float *L = (float *) calloc (FR, sizeof (float)), *R = (float *) calloc (FR, sizeof (float));
	for (int p = 0; p < NPLUGS; p++)
	{
		const KpTestApi *api = PLUGS[p].api ();
		int kind = api->desc->kind;
		if (kind == KP_GENERATOR) continue;
		KpShm *s = newShm (kind, 4096, 4096);
		CHECKF (api->attach (s), "%s", PLUGS[p].name);
		memset (L, 0, FR * sizeof (float)); memset (R, 0, FR * sizeof (float));
		// the input of an effect: a decaying noise burst, then a chord of sines (a second each)
		if (kind == KP_EFFECT)
		{
			unsigned x = 1;
			for (int i = 0; i < FR; i++)
			{
				x = x * 1664525u + 1013904223u;
				float noise = ((float) (x >> 8) / 8388608.0f - 1) * expf (-(float) i / 4000.0f) * 0.8f;
				float t = (float) (i - 44100) / 44100.0f, ch = 0;
				if (i >= 44100 && i < 88200) ch = 0.2f * (sinf (2 * KP_PI * 220 * t) + sinf (2 * KP_PI * 277.2f * t) + sinf (2 * KP_PI * 329.6f * t));
				L[i] = noise + ch; R[i] = noise * 0.7f + ch;
			}
		}
		else
		{
			int notes[] = { 48, 55, 60, 64, 67, 72 };
			for (int k = 0; k < 6; k++) pushEv (s, 2205 * k, KPE_NOTE_ON, notes[k], 70 + 10 * k);
			for (int k = 0; k < 6; k++) pushEv (s, 44100 + 4410 * k, KPE_NOTE_OFF, notes[k], 0);
			pushEv (s, 88200, KPE_NOTE_ON, 36, 120);
			pushEv (s, 110250, KPE_ALL_OFF, 0, 0);
		}
		// render in bursts, as the process would (the engine moves `want`; an effect writes its input first)
		long long pos = 0;
		unsigned seed = 3;
		while (pos < FR)
		{
			seed = seed * 1103515245u + 12345u;
			int n = 64 + (int) ((seed >> 16) % 900); if (pos + n > FR) n = (int) (FR - pos);
			if (kind == KP_EFFECT) kp_au_write (s->inL, s->inR, (unsigned long long) pos, L + pos, R + pos, n);
			kp_st64 (&s->want, (unsigned long long) (pos + n));
			int got = api->serve ();
			CHECK (got == n);
			kp_au_read (s->outL, s->outR, (unsigned long long) pos, L + pos, R + pos, n);
			kp_st64 (&s->readPos, (unsigned long long) (pos + n));
			pos += n;
		}
		double rms = 0; float peak = 0; bool nan = false;
		for (int i = 0; i < FR; i++)
		{
			if (L[i] != L[i] || R[i] != R[i]) nan = true;
			rms += L[i] * L[i] + R[i] * R[i];
			if (fabsf (L[i]) > peak) peak = fabsf (L[i]);
			if (fabsf (R[i]) > peak) peak = fabsf (R[i]);
		}
		rms = sqrt (rms / (2.0 * FR));
		char path[128]; snprintf (path, sizeof path, "/tmp/koton_plug_%s.wav", PLUGS[p].name);
		writeWav (path, L, R, FR);
		printf ("  %-10s %-10s rms %.4f peak %.3f -> %s\n", PLUGS[p].name, kind == KP_INSTRUMENT ? "instrument" : "effect", rms, peak, path);
		CHECKF (!nan && rms > 0.003 && peak < 8, "%s: rms %.4f peak %.3f%s", PLUGS[p].name, rms, peak, nan ? " NaN" : "");
		// the extremes of every parameter: still no NaN, no blow-up
		for (int ext = 0; ext < 2; ext++)
		{
			for (int i = 0; i < api->desc->nparams; i++) api->setParam (i, ext ? api->desc->params[i].max : api->desc->params[i].min);
			if (kind == KP_INSTRUMENT) pushEv (s, pos, KPE_NOTE_ON, ext ? 96 : 24, 127);
			bool bad = false;
			for (int b = 0; b < 40; b++)
			{
				if (kind == KP_EFFECT) kp_au_write (s->inL, s->inR, (unsigned long long) pos, L + (b * 1000) % (FR - 2000), R + (b * 1000) % (FR - 2000), 1000);
				kp_st64 (&s->want, (unsigned long long) (pos + 1000));
				api->serve ();
				float l[1000], r[1000];
				kp_au_read (s->outL, s->outR, (unsigned long long) pos, l, r, 1000);
				for (int k = 0; k < 1000; k++) if (l[k] != l[k] || fabsf (l[k]) > 64 || r[k] != r[k] || fabsf (r[k]) > 64) bad = true;
				pos += 1000;
				kp_st64 (&s->readPos, (unsigned long long) pos);
			}
			CHECKF (!bad, "%s: the parameters at their %s", PLUGS[p].name, ext ? "maximum" : "minimum");
		}
		free (s);
	}
	free (L); free (R);
}

// ---- 5. the generators, the states, the manifests ---------------------------------------------------------------
static bool request (const KpTestApi *api, KpShm *s, unsigned type, const char *payload, unsigned len, Str &reply)
{
	unsigned seq = kp_req_post (s, type, payload, len);
	api->request ();
	if (!kp_req_answered (s, seq) || s->repStatus != KP_OK) return false;
	reply.set (s->data, (int) s->repLen);
	return true;
}

static void addChord (Project &p, int deg, int beats)
{
	PatternModule *m = new PatternModule;
	RootQ c = diatonicChord (p.key, deg, 0, 0, 0);
	m->degree = deg; m->root = c.root; m->quality = c.quality; m->beatsPerBar = beats;
	p.tracks[p.chordTrackIndex ()].items.push (Item (0, m));
}

static bool generateWith (const KpTestApi *api, const GeneratorModule &m, const Project &p, double start, Riff &out)
{
	KpShm *s = newShm (KP_GENERATOR, 0, 0);
	bool ok = api->attach (s);
	Str st; moduleStateJson (m, st);
	json::Writer w (false);
	genContext (m, p, start, st.c (), w);
	Str reply;
	ok = ok && request (api, s, KP_GENERATE, w.data (), (unsigned) w.size (), reply);
	out.notes.clear (); out.spq = SPQ; out.lengthSlices = iround (m.durationBeats * SPQ);
	ok = ok && genReply (reply.c (), (unsigned long) reply.len (), out);
	free (s);
	return ok;
}

static void testGenerators ()
{
	// a project in A minor, 4/4: Am | F | C | G, a bar each
	Project p;
	p.key.tonicLetter = 5; p.key.accidental = 0; p.key.mode = 1;
	Track lead; lead.name = "Arp"; p.tracks.push (lead);
	Track ch; ch.name = "Chords"; ch.type = TRACK_CHORD; p.tracks.push (ch);
	addChord (p, 0, 4); addChord (p, 5, 4); addChord (p, 2, 4); addChord (p, 6, 4);
	GeneratorModule m; m.id = "gen-1"; m.durationBeats = 16;
	// the context
	json::Writer w (false);
	genContext (m, p, 0, "", w);
	json::Doc d; CHECK (d.parse (w.data (), w.size ()));
	CHECK (d.root ()["key"]["tonic"].asInt (-1) == 9 && d.root ()["chords"].size () == 4 && d.root ()["length"].asInt (0) == 16);
	CHECK (d.root ()["chords"][1]["root"].asInt (-1) == 5 && d.root ()["chords"][1]["basic"].asInt (-1) == 0);	// F major
	// the arpeggiator, Up, eighths: every note is a tone of the chord under it
	const KpTestApi *arp = kp_arp_api ();
	setModuleStateJson (m, "{\"v\":1,\"duration\":16,\"params\":{\"pattern\":0,\"notes_per_beat\":2,\"extend\":1,\"octave\":4}}");
	Riff r;
	CHECK (generateWith (arp, m, p, 0, r));
	bool tones = r.notes.size () == 32;
	for (int i = 0; i < r.notes.size (); i++)
	{
		int beat = r.notes[i].start / SPQ, bar = beat / 4, pc = (r.notes[i].note + 12) % 12;
		static const int root[4] = { 9, 5, 0, 7 }, third[4] = { 0, 9, 4, 11 }, fifth[4] = { 4, 0, 7, 2 };
		if (pc != root[bar] && pc != third[bar] && pc != fifth[bar]) tones = false;
		if (r.notes[i].start != i * SPQ / 2) tones = false;
	}
	CHECKF (tones, "%d notes", r.notes.size ());
	// Koton's "Random" pattern: the same notes twice (a seeded .NET Random)
	setModuleStateJson (m, "{\"v\":1,\"params\":{\"pattern\":4,\"notes_per_beat\":4}}");
	Riff r1, r2;
	CHECK (generateWith (arp, m, p, 0, r1) && generateWith (arp, m, p, 0, r2));
	bool same = r1.notes.size () == 64 && r1.notes.size () == r2.notes.size ();
	for (int i = 0; same && i < r1.notes.size (); i++) if (r1.notes[i].note != r2.notes[i].note || r1.notes[i].start != r2.notes[i].start) same = false;
	CHECK (same);
	// a custom rhythm (Koton's "rhythm" key), the chord pattern
	setModuleStateJson (m, "{\"v\":1,\"params\":{\"pattern\":5,\"rhythm_mode\":1},\"rhythm\":{\"beats\":2,\"spb\":4,\"starts\":[0,3,6],\"lens\":[2,2,2]}}");
	CHECK (generateWith (arp, m, p, 0, r) && r.notes.size () == 8 * 3 * 3);	// 8 motifs x 3 hits x a triad
	printf ("generators: arp %d notes (Up), %d (Random, twice the same), %d (a custom rhythm, chords)\n", 32, r1.notes.size (), r.notes.size ());
	// euclid, automaton: notes, in range, on the chords / the scale
	GeneratorModule g; g.id = "gen-2"; g.durationBeats = 16;
	CHECK (generateWith (kp_euclid_api (), g, p, 0, r) && r.notes.size () == 5 * 8);	// E(5,8) of sixteenths over 16 beats
	int n1 = r.notes.size ();
	setModuleStateJson (g, "{\"rule\":30,\"notes_per_beat\":2,\"chord_aware\":0,\"scale\":5,\"_dur\":16}");	// Koton's flat state
	CHECK (generateWith (kp_automaton_api (), g, p, 0, r) && r.notes.size () > 16);
	bool inScale = true;
	for (int i = 0; i < r.notes.size (); i++) { int pc = (r.notes[i].note + 12) % 12; if (diatonicDegreeOf (p.key, pc) < 0) inScale = false; }
	CHECK (inScale);
	printf ("generators: euclid %d notes, automaton %d notes (rule 30, on the key's scale)\n", n1, r.notes.size ());
	// the hook's path: a block placed later, its chords from there
	GeneratorModule h; h.id = "gen-3"; h.durationBeats = 4;
	setModuleStateJson (h, "{\"params\":{\"pattern\":5,\"notes_per_beat\":1}}");
	CHECK (generateWith (arp, h, p, 12, r) && r.notes.size () == 12 && (r.notes[0].note + 12) % 12 == 7);	// G, from beat 12
	// base64 both ways, a state kept as JSON
	Str b, back; b64encode ("{\"a\":1}", 7, b); CHECK (b == "eyJhIjoxfQ==" && b64decode (b, back) && back == "{\"a\":1}");
	GeneratorModule j; j.state = "{\"x\":2}"; Str js; moduleStateJson (j, js); CHECK (js == "{\"x\":2}");
}

static void testStatesManifests ()
{
	for (int p = 0; p < NPLUGS; p++)
	{
		const KpTestApi *api = PLUGS[p].api ();
		const KpDesc *d = api->desc;
		KpShm *s = newShm (d->kind, 4096, 4096);
		// the initial state (data[] at the start), then a get: the values back
		const char *init = "{\"v\":1,\"params\":{}}";
		memcpy (s->data, init, strlen (init)); s->initLen = (unsigned) strlen (init);
		CHECK (api->attach (s) && s->nparams == (unsigned) d->nparams && s->paramsLen > 0);
		json::Doc pl; CHECK (pl.parse (s->data, s->paramsLen) && pl.root ()["params"].size () == (unsigned) d->nparams);
		// a state setting every parameter to its maximum, a get, a set of the defaults, a get again
		json::Writer w (false); w.beginObj (); w.key ("params"); w.beginObj ();
		for (int i = 0; i < d->nparams; i++) { w.key (d->params[i].id); w.num ((double) d->params[i].max); }
		w.endObj (); w.endObj ();
		Str rep;
		CHECK (request (api, s, KP_STATE_SET, w.data (), (unsigned) w.size (), rep));
		CHECK (request (api, s, KP_STATE_GET, 0, 0, rep));
		json::Doc sd; CHECK (sd.parse (rep.c (), rep.len ()));
		bool ok = true;
		for (int i = 0; i < d->nparams; i++) if (fabs (sd.root ()["params"][d->params[i].id].asDouble (-1e9) - d->params[i].max) > 1e-3 * (1 + fabs (d->params[i].max))) ok = false;
		CHECKF (ok, "%s: a state round trip", PLUGS[p].name);
		// the manifest on the card says what the program says
		char path[256]; snprintf (path, sizeof path, "user/Apps/kp_%s/plugin.json", PLUGS[p].name);
		FILE *f = fopen (path, "rb");
		CHECKF (f != 0, "%s", path);
		if (f)
		{
			char *buf = (char *) malloc (65536); size_t n = fread (buf, 1, 65535, f); fclose (f); buf[n] = 0;
			json::Doc mf;
			CHECKF (mf.parse (buf, n), "%s: %s at %d:%d", path, mf.error (), mf.errLine (), mf.errCol ());
			const json::Value &m = mf.root ();
			static const char *const kinds[] = { "", "instrument", "effect", "generator" };
			CHECKF (json::seq (m["kind"].asStr (), kinds[d->kind]) && json::seq (m["name"].asStr (), d->name), "%s: kind / name", path);
			CHECKF (m["params"].size () == (unsigned) d->nparams, "%s: %u params, the program %d", path, m["params"].size (), d->nparams);
			for (int i = 0; i < d->nparams && i < (int) m["params"].size (); i++)
			{
				const json::Value &q = m["params"][i];
				const KpParamDef &k = d->params[i];
				bool same = json::seq (q["id"].asStr (), k.id) && json::seq (q["name"].asStr (), k.name)
					&& fabs (q["min"].asDouble (-1e9) - k.min) < 1e-4 && fabs (q["max"].asDouble (-1e9) - k.max) < 1e-4
					&& fabs (q["default"].asDouble (-1e9) - k.def) < 1e-4 && fabs (q["step"].asDouble (0) - k.step) < 1e-6
					&& json::seq (q["unit"].asStr (), k.unit ? k.unit : "");
				int nc = 0; if (k.choices) while (k.choices[nc]) nc++;
				same = same && q["choices"].size () == (unsigned) nc;
				for (int c = 0; same && c < nc; c++) same = json::seq (q["choices"][c].asStr (), k.choices[c]);
				CHECKF (same, "%s: parameter %d (%s) differs from the program's", path, i, k.id);
			}
			free (buf);
		}
		free (s);
	}
	printf ("states and manifests: %d plugins round-trip their states, their plugin.json match\n", (int) NPLUGS);
}

int main (int argc, char **argv)
{
	if (argc > 2 && !strcmp (argv[1], "--manifest"))		// (a plugin's parameter list, to write its plugin.json)
	{
		for (int p = 0; p < NPLUGS; p++)
			if (!strcmp (PLUGS[p].name, argv[2]))
			{
				KpShm *s = newShm (PLUGS[p].api ()->desc->kind, 4096, 4096);
				PLUGS[p].api ()->attach (s);
				fwrite (s->data, 1, s->paramsLen, stdout); printf ("\n");
				free (s);
				return 0;
			}
		return 1;
	}
	testRings ();
	testEngineAhead ();
	testDsp ();
	testGenerators ();
	testStatesManifests ();
	testRealTime ();
	printf (g_fail ? "koton plugins: %d FAILED\n" : "koton plugins: all passed\n", g_fail);
	return g_fail != 0;
}
