//
// synth_test.cpp -- tests of Koton's SoundFont synthesizer (user/Apps/koton/synth, the MeltySynth
// port) on the PC. Built by synth_run.sh under ASan / UBSan / LSan (a leak fails the run).
//
//   synth_test [file.sf2]          (default /opt/sf2/GeneralUser-GS.sf2)
//     - loads the SoundFont (the file buffer is freed right after: the SoundFont must not keep
//       pointers into it), lists a few presets;
//     - renders ~8 s of music: a piano arpeggio + chords (ch 0, sustain pedal, RPN bend range),
//       a string pad (ch 1, program 48, volume / pan / modulation / pitch-bend moves, reverb and
//       chorus sends) and a drum groove (ch 9), in odd-sized render calls; writes
//       /tmp/koton_synth_test.wav (16-bit stereo 44.1 kHz);
//     - checks: no NaN / Inf, a sane peak, not silent, the voices back to 0 after noteOffAll +
//       the release time, noteOffAll (true) / All Sound Off / reset, two synths at once;
//     - robustness: garbage, truncated and byte-flipped SoundFonts must load as nullptr (or load
//       and play) without crashing.
//   synth_test --bench [file.sf2]  the real-time factor with 32 simultaneous voices (build it
//                                  without sanitizers, -O2)
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "../../../user/Apps/koton/synth/meltysynth.h"

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf ("FAIL %s:%d: ", __FILE__, __LINE__); printf (__VA_ARGS__); printf ("\n"); } } while (0)

static unsigned char *readFile (const char *path, size_t *len)
{
	FILE *f = fopen (path, "rb");
	if (!f) return nullptr;
	fseek (f, 0, SEEK_END);
	long n = ftell (f);
	fseek (f, 0, SEEK_SET);
	unsigned char *buf = (unsigned char *) malloc (n > 0 ? n : 1);
	if (buf && fread (buf, 1, n, f) != (size_t) n) { free (buf); buf = nullptr; }
	fclose (f);
	*len = (size_t) n;
	return buf;
}

static void writeWav (const char *path, const float *l, const float *r, int frames, int rate)
{
	FILE *f = fopen (path, "wb");
	if (!f) { printf ("cannot write %s\n", path); return; }
	auto u32 = [&] (unsigned v) { unsigned char b[4] = { (unsigned char) v, (unsigned char) (v >> 8), (unsigned char) (v >> 16), (unsigned char) (v >> 24) }; fwrite (b, 1, 4, f); };
	auto u16 = [&] (unsigned v) { unsigned char b[2] = { (unsigned char) v, (unsigned char) (v >> 8) }; fwrite (b, 1, 2, f); };
	unsigned dataBytes = (unsigned) frames * 4;
	fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); fwrite ("WAVE", 1, 4, f);
	fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 (2); u32 (rate); u32 (rate * 4); u16 (4); u16 (16);
	fwrite ("data", 1, 4, f); u32 (dataBytes);
	for (int i = 0; i < frames; i++)
	{
		float s[2] = { l[i], r[i] };
		for (int c = 0; c < 2; c++)
		{
			float v = s[c] * 32767.0f;
			if (v > 32767.0f) v = 32767.0f;
			if (v < -32768.0f) v = -32768.0f;
			u16 ((unsigned) (short) lrintf (v) & 0xFFFF);
		}
	}
	fclose (f);
}

// ---- The music -------------------------------------------------------------------------------

struct Ev { double t; int ch, cmd, d1, d2; };

struct Score
{
	Ev ev[2048];
	int n = 0;
	void add (double t, int ch, int cmd, int d1, int d2) { if (n < 2048) ev[n++] = { t, ch, cmd, d1, d2 }; }
	void note (double t, double dur, int ch, int key, int vel) { add (t, ch, 0x90, key, vel); add (t + dur, ch, 0x80, key, 0); }
	void sort ()		// stable insertion sort by time
	{
		for (int i = 1; i < n; i++)
		{
			Ev e = ev[i]; int j = i - 1;
			while (j >= 0 && ev[j].t > e.t) { ev[j + 1] = ev[j]; j--; }
			ev[j + 1] = e;
		}
	}
};

static void buildScore (Score &s)
{
	// Piano (ch 0, program 0): RPN pitch-bend range = 12 semitones, a C-major arpeggio, chords.
	s.add (0.0, 0, 0xC0, 0, 0);
	s.add (0.0, 0, 0xB0, 0x65, 0); s.add (0.0, 0, 0xB0, 0x64, 0); s.add (0.0, 0, 0xB0, 0x06, 12); s.add (0.0, 0, 0xB0, 0x26, 0);
	s.add (0.0, 0, 0xB0, 0x5B, 60);				// reverb send
	int arp[8] = { 60, 64, 67, 72, 76, 72, 67, 64 };
	for (int i = 0; i < 16; i++) s.note (0.125 * i, 0.2, 0, arp[i % 8], 70 + (i % 4) * 12);
	int chords[4][3] = { { 60, 64, 67 }, { 65, 69, 72 }, { 67, 71, 74 }, { 60, 64, 67 } };
	for (int c = 0; c < 4; c++)
		for (int k = 0; k < 3; k++) s.note (2.0 + 0.75 * c, 0.6, 0, chords[c][k], 100);
	s.add (5.0, 0, 0xB0, 0x40, 127);				// sustain pedal down
	s.note (5.0, 0.1, 0, 48, 110); s.note (5.0, 0.1, 0, 55, 100); s.note (5.0, 0.1, 0, 64, 90);
	s.add (5.5, 0, 0xE0, 0, 80);					// bend up (range 12)
	s.add (5.8, 0, 0xE0, 0, 64);
	s.add (6.2, 0, 0xB0, 0x40, 0);				// pedal up: the notes release

	// Strings (ch 1, program 48): a held pad with volume / pan / modulation / bend moves.
	s.add (0.0, 1, 0xC0, 48, 0);
	s.add (0.0, 1, 0xB0, 0x5D, 80);				// chorus send
	s.add (0.0, 1, 0xB0, 0x5B, 90);				// reverb send
	s.add (1.0, 1, 0xB0, 0x07, 40);
	s.note (1.0, 5.0, 1, 48, 90); s.note (1.0, 5.0, 1, 55, 90); s.note (1.0, 5.0, 1, 60, 90);
	for (int i = 0; i <= 20; i++)
	{
		double t = 1.0 + 0.1 * i;
		s.add (t, 1, 0xB0, 0x07, 40 + i * 4);			// fade in
		s.add (t, 1, 0xB0, 0x0A, 64 + (int) (50 * sin (i * 0.5)));	// pan sweep
	}
	s.add (3.0, 1, 0xB0, 0x01, 100);				// modulation wheel (vibrato)
	s.add (4.0, 1, 0xB0, 0x01, 0);
	for (int i = 0; i <= 20; i++)				// pitch bend sweep up and back
	{
		int v = 8192 + (int) (4000 * sin (3.14159 * i / 20));
		s.add (4.0 + 0.05 * i, 1, 0xE0, v & 0x7F, (v >> 7) & 0x7F);
	}
	s.add (5.0, 1, 0xB0, 0x0B, 70);				// expression

	// Drums (ch 9): kick / snare / closed + open hi-hat (exclusive class), 8ths at 120 bpm.
	for (int i = 0; i < 28; i++)
	{
		double t = 0.25 * i;
		s.note (t, 0.1, 9, (i % 8 == 7) ? 46 : 42, 80 + (i % 2) * 20);
		if (i % 4 == 0) s.note (t, 0.1, 9, 36, 120);
		if (i % 4 == 2) s.note (t, 0.1, 9, 38, 110);
	}
	s.sort ();
}

// Renders [from, to) seconds, applying the due events; odd-sized render calls (partial blocks).
static void renderSpan (ms::Synthesizer &syn, const Score &sc, int *next, float *L, float *R, int rate, int f0, int f1)
{
	int f = f0;
	int chunk = 37;
	while (f < f1)
	{
		while (*next < sc.n && (int) (sc.ev[*next].t * rate) <= f)
		{
			const Ev &e = sc.ev[*next];
			syn.processMidiMessage (e.ch, e.cmd, e.d1, e.d2);
			(*next)++;
		}
		int until = f1;
		if (*next < sc.n) { int te = (int) (sc.ev[*next].t * rate); if (te < until) until = te; }
		if (until <= f) until = f + 1;
		int n = until - f;
		if (n > chunk) n = chunk;
		syn.render (L + f, R + f, n);
		f += n;
		chunk = chunk == 37 ? 211 : chunk == 211 ? 64 : 37;
	}
}

static void stats (const float *L, const float *R, int n, float *peak, double *rms, bool *finite)
{
	*peak = 0; *rms = 0; *finite = true;
	double acc = 0;
	for (int i = 0; i < n; i++)
	{
		float a = L[i], b = R[i];
		if (!isfinite (a) || !isfinite (b)) *finite = false;
		if (fabsf (a) > *peak) *peak = fabsf (a);
		if (fabsf (b) > *peak) *peak = fabsf (b);
		acc += (double) a * a + (double) b * b;
	}
	*rms = n ? sqrt (acc / (2.0 * n)) : 0;
}

// ---- Robustness ------------------------------------------------------------------------------

static void tryLoadAndPlay (const unsigned char *d, size_t len, int *loaded, int *rejected)
{
	char err[128];
	err[0] = 'x';
	ms::SoundFont *sf = ms::soundfont_load (d, len, err, sizeof err);
	if (!sf) { (*rejected)++; CHECK (err[0] != '\0', "no error message"); return; }
	(*loaded)++;
	ms::SynthSettings st;
	st.maxPolyphony = 16;
	ms::Synthesizer syn (sf, st);
	if (syn.ok ())
	{
		float l[512], r[512];
		for (int p = 0; p < 3; p++)
		{
			syn.processMidiMessage (0, 0xC0, p * 20, 0);
			for (int k = 24; k < 108; k += 7) syn.noteOn (0, k, 100);
			syn.noteOn (9, 36 + p, 100);
			for (int i = 0; i < 20; i++) syn.render (l, r, 512);
			syn.noteOffAll (false);
			for (int i = 0; i < 5; i++) syn.render (l, r, 512);
		}
	}
	ms::soundfont_free (sf);
}

static void robustness (const unsigned char *file, size_t len)
{
	int loaded = 0, rejected = 0;
	char err[128];

	// Nothing / garbage.
	CHECK (ms::soundfont_load (nullptr, 0, err, sizeof err) == nullptr, "null buffer");
	CHECK (ms::soundfont_load (file, 0, nullptr, 0) == nullptr, "empty buffer");
	unsigned char *junk = (unsigned char *) malloc (1 << 16);
	unsigned seed = 12345;
	for (int i = 0; i < (1 << 16); i++) { seed = seed * 1103515245 + 12345; junk[i] = (unsigned char) (seed >> 16); }
	tryLoadAndPlay (junk, 1 << 16, &loaded, &rejected);
	memcpy (junk, "RIFF\xff\xff\xff\x7fsfbkLIST\xff\xff\xff\x7fINFO", 24);	// a lying header
	tryLoadAndPlay (junk, 1 << 16, &loaded, &rejected);
	CHECK (loaded == 0, "garbage loaded");

	// Truncations of the real file (the sample data dominates: most cuts land in it or in pdta).
	size_t cuts[] = { 1, 4, 11, 12, 20, 100, 1000, len / 3, len / 2, len - 5000, len - 1000, len - 200, len - 47, len - 1 };
	for (size_t c : cuts) if (c < len) tryLoadAndPlay (file, c, &loaded, &rejected);
	CHECK (loaded == 0, "a truncated file loaded");
	printf ("robustness: garbage + %d truncations rejected\n", (int) (sizeof cuts / sizeof cuts[0]));

	// Byte flips in the pdta chunk (the headers, regions and generators): each variant must be
	// rejected or load and play without an out-of-range access (ASan would catch it).
	size_t pdta = 0;
	for (size_t i = len > 4 ? len - 4 : 0; i > 0; i--) if (memcmp (file + i, "pdta", 4) == 0) { pdta = i; break; }
	if (pdta)
	{
		unsigned char *copy = (unsigned char *) malloc (len);
		memcpy (copy, file, len);
		int l0 = loaded, r0 = rejected;
		for (int it = 0; it < 150; it++)
		{
			size_t pos[8]; unsigned char old[8];
			int nflip = 1 + it % 8;
			for (int k = 0; k < nflip; k++)
			{
				seed = seed * 1103515245 + 12345;
				pos[k] = pdta + (seed >> 8) % (len - pdta);
				old[k] = copy[pos[k]];
				seed = seed * 1103515245 + 12345;
				copy[pos[k]] = (it & 1) ? (unsigned char) (seed >> 16) : (unsigned char) (old[k] ^ (1 << (seed % 8)));
			}
			tryLoadAndPlay (copy, len, &loaded, &rejected);
			for (int k = nflip - 1; k >= 0; k--) copy[pos[k]] = old[k];
		}
		printf ("robustness: 150 pdta byte-flip variants: %d loaded and played, %d rejected\n", loaded - l0, rejected - r0);
		free (copy);
	}
	free (junk);
}

// ---- Benchmark -------------------------------------------------------------------------------

static double now ()
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int bench (const ms::SoundFont *sf)
{
	for (int fx = 1; fx >= 0; fx--)
	{
		ms::SynthSettings st;
		st.enableReverbAndChorus = fx != 0;
		ms::Synthesizer syn (sf, st);
		if (!syn.ok ()) { printf ("bench: synth failed\n"); return 1; }
		// Sustaining string voices, added until 32 are sounding.
		syn.processMidiMessage (0, 0xC0, 48, 0);
		syn.processMidiMessage (1, 0xC0, 49, 0);
		float l[256], r[256];
		for (int k = 36; k < 100 && syn.activeVoiceCount () < 32; k++) { syn.noteOn (k & 1, k, 100); syn.render (l, r, 64); }
		int voices = syn.activeVoiceCount ();
		const int rate = st.sampleRate, seconds = 20;
		double t0 = now ();
		long vsum = 0, blocks = 0;
		for (int i = 0; i < rate * seconds / 256; i++) { syn.render (l, r, 256); vsum += syn.activeVoiceCount (); blocks++; }
		double dt = now () - t0;
		printf ("bench (%s): %d voices at start, %.1f on average: %d s of audio in %.3f s -> real-time factor %.1fx (%.2f%% of a core)\n",
			fx ? "reverb+chorus" : "dry", voices, (double) vsum / blocks, seconds, dt, seconds / dt, 100.0 * dt / seconds);
	}
	return 0;
}

// ---- Main ------------------------------------------------------------------------------------

int main (int argc, char **argv)
{
	bool doBench = false;
	const char *path = "/opt/sf2/GeneralUser-GS.sf2";
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp (argv[i], "--bench")) doBench = true;
		else path = argv[i];
	}

	size_t len = 0;
	unsigned char *file = readFile (path, &len);
	if (!file) { printf ("cannot read %s\n", path); return 1; }

	char err[256];
	double t0 = now ();
	ms::SoundFont *sf = ms::soundfont_load (file, len, err, sizeof err);
	double tl = now () - t0;
	if (!sf) { printf ("load failed: %s\n", err); free (file); return 1; }

	if (doBench)
	{
		free (file);
		int r = bench (sf);
		ms::soundfont_free (sf);
		return r;
	}

	// Robustness first (it needs the file bytes), then drop the file: the SoundFont must not
	// point into it (ASan: use-after-free).
	robustness (file, len);
	free (file);

	printf ("loaded '%s' (%.1f MB) in %.3f s: %d presets\n", ms::soundfont_name (sf), len / 1048576.0, tl, ms::soundfont_preset_count (sf));
	for (int i = 0; i < ms::soundfont_preset_count (sf) && i < 5; i++)
		printf ("  preset %d: %3d:%3d %s\n", i, ms::soundfont_preset_bank (sf, i), ms::soundfont_preset_patch (sf, i), ms::soundfont_preset_name (sf, i));
	CHECK (ms::soundfont_preset_count (sf) > 100, "too few presets");
	CHECK (ms::soundfont_preset_bank (sf, -1) == -1 && ms::soundfont_preset_name (sf, 1 << 20)[0] == '\0', "out-of-range preset");

	ms::SynthSettings st;
	ms::Synthesizer syn (sf, st);
	CHECK (syn.ok (), "synthesizer not ok");
	if (!syn.ok ()) { ms::soundfont_free (sf); return 1; }

	// Bad settings: not ok, and every call is a harmless no-op.
	{
		ms::SynthSettings bad; bad.blockSize = 3;
		ms::Synthesizer b (sf, bad);
		CHECK (!b.ok (), "bad settings accepted");
		float l[8], r[8];
		b.noteOn (0, 60, 100); b.render (l, r, 8);
		CHECK (l[0] == 0 && b.activeVoiceCount () == 0, "a bad synth plays");
		ms::Synthesizer c (nullptr, st);
		CHECK (!c.ok (), "no SoundFont accepted");
	}

	// The music.
	const int rate = st.sampleRate;
	const int musicFrames = 8 * rate, tailMax = 15 * rate;
	float *L = (float *) calloc (musicFrames + tailMax, sizeof (float));
	float *R = (float *) calloc (musicFrames + tailMax, sizeof (float));
	Score *sc = new Score;
	buildScore (*sc);
	int next = 0, maxVoices = 0;
	for (int f = 0; f < 7 * rate; f += rate / 10)
	{
		renderSpan (syn, *sc, &next, L, R, rate, f, f + rate / 10);
		if (syn.activeVoiceCount () > maxVoices) maxVoices = syn.activeVoiceCount ();
	}
	printf ("music: %d events, up to %d voices\n", sc->n, maxVoices);
	CHECK (maxVoices > 4, "too few voices (%d)", maxVoices);

	// 7 s: everything off (with the release), then render until the voices are gone.
	syn.noteOffAll (false);
	int f = 7 * rate, silentAt = -1;
	while (f < musicFrames + tailMax)
	{
		syn.render (L + f, R + f, 100);
		f += 100;
		if (syn.activeVoiceCount () == 0 && silentAt < 0) silentAt = f;
		if (silentAt >= 0 && f >= musicFrames) break;
	}
	int total = f;
	CHECK (silentAt >= 0, "voices still active %.1f s after noteOffAll (%d)", (f - 7.0 * rate) / rate, syn.activeVoiceCount ());
	if (silentAt >= 0) printf ("release: voices back to 0 %.2f s after noteOffAll\n", (silentAt - 7.0 * rate) / rate);

	float peak; double rms; bool finite;
	stats (L, R, total, &peak, &rms, &finite);
	printf ("output: %.2f s, peak %.3f, rms %.4f\n", (double) total / rate, peak, rms);
	CHECK (finite, "NaN / Inf in the output");
	CHECK (peak > 0.05f && peak < 2.0f, "peak out of range (%.3f)", peak);
	CHECK (rms > 0.005, "(nearly) silent (rms %.5f)", rms);
	for (int s = 0; s < 7; s++)		// every second of the music has sound
	{
		float p; double r2; bool fin;
		stats (L + s * rate, R + s * rate, rate, &p, &r2, &fin);
		CHECK (r2 > 0.002, "second %d is silent (rms %.5f)", s, r2);
	}
	writeWav ("/tmp/koton_synth_test.wav", L, R, total, rate);
	printf ("wrote /tmp/koton_synth_test.wav\n");

	// Immediate stops.
	{
		float l[256], r[256];
		for (int k = 40; k < 80; k += 3) syn.noteOn (1, k, 100);
		syn.render (l, r, 256);
		CHECK (syn.activeVoiceCount () > 0, "no voice");
		syn.noteOffAll (true);
		CHECK (syn.activeVoiceCount () == 0, "noteOffAll (true) left voices");
		for (int k = 40; k < 80; k += 3) syn.noteOn (2, k, 100);
		syn.noteOn (3, 60, 100);
		syn.render (l, r, 256);
		syn.processMidiMessage (2, 0xB0, 0x78, 0);		// All Sound Off on ch 2 only
		syn.render (l, r, 256);
		int left = syn.activeVoiceCount ();
		CHECK (left > 0 && left <= 2, "All Sound Off: %d voices left (ch 3 only expected)", left);
		syn.reset ();
		CHECK (syn.activeVoiceCount () == 0, "reset left voices");
		syn.render (l, r, 256);
		bool zero = true;
		for (int i = 0; i < 256; i++) if (l[i] != 0 || r[i] != 0) zero = false;
		CHECK (zero, "reset: the reverb / chorus tails are not muted");
	}

	// Two synthesizers at once (no shared state): the same notes give the same samples.
	{
		ms::SynthSettings s2; s2.sampleRate = 48000; s2.blockSize = 32; s2.maxPolyphony = 8;
		ms::Synthesizer a (sf, s2), b (sf, s2);
		CHECK (a.ok () && b.ok (), "two synths");
		float la[500], ra[500], lb[500], rb[500];
		a.noteOn (0, 60, 100); b.noteOn (0, 60, 100);
		b.noteOn (1, 30, 100);				// only b: must not touch a
		for (int i = 0; i < 10; i++) { a.render (la, ra, 500); b.render (lb, rb, 500); }
		b.noteOffAll (true); b.reset ();
		a.reset (); a.noteOn (0, 64, 90); b.noteOn (0, 64, 90);
		bool same = true;
		for (int i = 0; i < 10; i++)
		{
			a.render (la, ra, 500); b.render (lb, rb, 500);
			for (int k = 0; k < 500; k++) if (la[k] != lb[k] || ra[k] != rb[k]) same = false;
		}
		CHECK (same, "two synths with the same input differ");
		// Polyphony limit: voice stealing.
		for (int k = 30; k < 90; k++) a.noteOn (k % 16, k, 100);
		CHECK (a.activeVoiceCount () <= 8, "polyphony limit exceeded");
		a.render (la, ra, 500);
	}

	delete sc;
	free (L); free (R);
	ms::soundfont_free (sf);

	if (failures) { printf ("%d FAILURE(S)\n", failures); return 1; }
	printf ("all synth tests passed\n");
	return 0;
}
