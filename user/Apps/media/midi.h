//
// Apps/media/midi.h -- MIDI files in Media Player: a Standard MIDI File read (formats 0 and 1, the tempo
// map, running status), its events in time order (MidiSong), played by MeltySynth -- Koton's
// synthesizer (Apps/koton/synth) -- through a SoundFont (MidiDecoder, a Decoder of decode.h); and its
// notes for the view (a line a note, a colour a channel) with the channels' General MIDI instruments.
// The SoundFont: SD:/etc/media/settings.ini's `soundfont`, else the first .sf2 of SD:/res/soundfonts (the package generaluser-gs; Koton's old SD:/koton/soundfonts too),
// loaded once, the first time a MIDI file plays (it may be big: GeneralUser GS is 30 MB).
//
#ifndef _media_midi_h
#define _media_midi_h

#include "Apps/koton/synth/meltysynth.h"

namespace media {

struct MidiEv { i64 at; unsigned char st, d1, d2; };	// at: in frames (SOUND_RATE)
struct MidiNote { i64 on, off; unsigned char key, ch, vel; };

static const char *const GM_NAMES[128] = {
	"Acoustic Grand Piano", "Bright Piano", "Electric Grand", "Honky-tonk Piano", "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
	"Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
	"Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
	"Nylon Guitar", "Steel Guitar", "Jazz Guitar", "Clean Guitar", "Muted Guitar", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
	"Acoustic Bass", "Finger Bass", "Pick Bass", "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
	"Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Harp", "Timpani",
	"Strings", "Slow Strings", "Synth Strings 1", "Synth Strings 2", "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
	"Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
	"Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet",
	"Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
	"Square Lead", "Saw Lead", "Calliope", "Chiff Lead", "Charang", "Voice Lead", "Fifths Lead", "Bass + Lead",
	"New Age Pad", "Warm Pad", "Polysynth", "Choir Pad", "Bowed Pad", "Metallic Pad", "Halo Pad", "Sweep Pad",
	"Rain", "Soundtrack", "Crystal", "Atmosphere", "Brightness", "Goblins", "Echoes", "Sci-fi",
	"Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai",
	"Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
	"Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone Ring", "Helicopter", "Applause", "Gunshot" };

// ---- the file -----------------------------------------------------------------------------------------------
class MidiSong
{
public:
	MidiEv *ev; int nev;			// in time order
	MidiNote *notes; int nnotes;		// (the view)
	i64 length;				// frames, to the last event
	char title[96];				// the first track's name (FF 03), "" if none
	int program[16]; bool used[16];		// each channel's first instrument; channels with notes
	int lowKey, highKey;
	int bpm;				// the first tempo (quarter notes a minute)
	i64 *bars; int nbars;			// each bar's start, in frames (the view's lines and numbers)
	MidiSong () : ev (0), nev (0), notes (0), nnotes (0), length (0), lowKey (127), highKey (0), bpm (120), bars (0), nbars (0)
	{ title[0] = 0; for (int i = 0; i < 16; i++) { program[i] = 0; used[i] = false; } }
	~MidiSong () { delete[] ev; delete[] notes; delete[] bars; }

	bool load (const char *path, char *err, int cap)
	{
		Src s;
		if (!s.open (path) || s.size < 14 || s.size > (16u << 20)) { snprintf (err, cap, "The MIDI file cannot be read."); return false; }
		unsigned n = (unsigned) s.size;
		unsigned char *b = new unsigned char[n];
		bool ok = s.read (b, n) == n && parse (b, n);
		delete[] b;
		if (!ok) snprintf (err, cap, "This MIDI file is damaged, or not a Standard MIDI File.");
		return ok;
	}
	const char *instrument (int ch) const { return ch == 9 ? "Drums" : GM_NAMES[program[ch] & 127]; }

private:
	struct Raw { unsigned tick; int seq; unsigned char st, d1, d2, tempo; unsigned us; };	// tempo: a tempo change (us)
	static unsigned be32 (const unsigned char *p) { return (unsigned) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
	static unsigned be16 (const unsigned char *p) { return (unsigned) p[0] << 8 | p[1]; }
	static bool vlq (const unsigned char *b, unsigned end, unsigned *p, unsigned *v)
	{
		*v = 0;
		for (int i = 0; i < 4; i++) { if (*p >= end) return false; unsigned char c = b[(*p)++]; *v = *v << 7 | (c & 0x7F); if (!(c & 0x80)) return true; }
		return false;
	}
	bool parse (const unsigned char *b, unsigned n)
	{
		// RIFF RMID: the SMF inside its "data" chunk
		if (n > 20 && !memcmp (b, "RIFF", 4) && !memcmp (b + 8, "RMID", 4))
			for (unsigned p = 12; p + 8 <= n; )
			{
				unsigned cl = (unsigned) b[p + 4] | b[p + 5] << 8 | b[p + 6] << 16 | (unsigned) b[p + 7] << 24;
				if (!memcmp (b + p, "data", 4)) { b += p + 8; n -= p + 8; break; }
				if (cl > n) break;
				p += 8 + ((cl + 1) & ~1u);
			}
		if (n < 14 || memcmp (b, "MThd", 4)) return false;
		unsigned hl = be32 (b + 4), fmt = be16 (b + 8), ntr = be16 (b + 10), div = be16 (b + 12);
		(void) fmt;
		if (div == 0) return false;
		bool smpte = (div & 0x8000) != 0;
		int fps = smpte ? -(signed char) (div >> 8) : 0, tpf = smpte ? (div & 0xFF) : 0;
		unsigned p = 8 + hl;
		int cap = 4096, nr = 0; Raw *raw = (Raw *) malloc (sizeof (Raw) * cap);
		int tsNum = 0, tsDen = 2;
		int seq = 0;
		for (unsigned t = 0; t < ntr && p + 8 <= n; t++)
		{
			if (memcmp (b + p, "MTrk", 4)) { p += 8 + be32 (b + p + 4); t--; if (p >= n) break; continue; }
			unsigned len = be32 (b + p + 4), q = p + 8, end = q + len > n ? n : q + len;
			unsigned tick = 0; unsigned char rs = 0;
			while (q < end)
			{
				unsigned d; if (!vlq (b, end, &q, &d)) break;
				tick += d;
				if (q >= end) break;
				unsigned char st = b[q];
				if (st == 0xFF)
				{
					if (q + 2 > end) break;
					unsigned char type = b[q + 1]; q += 2; unsigned ml; if (!vlq (b, end, &q, &ml) || q + ml > end) break;
					if (type == 0x51 && ml == 3) { if (nr == cap) { cap *= 2; raw = (Raw *) realloc (raw, sizeof (Raw) * cap); }
						raw[nr++] = Raw { tick, seq++, 0, 0, 0, 1, (unsigned) b[q] << 16 | b[q + 1] << 8 | b[q + 2] }; }
					if (type == 0x58 && ml >= 2 && !tsNum) { tsNum = b[q]; tsDen = b[q + 1]; }
					if (type == 0x03 && t == 0 && !title[0]) { unsigned k = ml < sizeof title - 1 ? ml : sizeof title - 1; memcpy (title, b + q, k); title[k] = 0; }
					if (type == 0x2F) { q = end; break; }
					q += ml; continue;
				}
				if (st == 0xF0 || st == 0xF7) { q++; unsigned ml; if (!vlq (b, end, &q, &ml)) break; q += ml; continue; }
				if (st & 0x80) { rs = st; q++; } else if (!rs) break; else st = rs;
				int nd = (st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0 ? 1 : 2;
				if (q + nd > end) break;
				unsigned char d1 = b[q], d2 = nd > 1 ? b[q + 1] : 0; q += nd;
				if (nr == cap) { cap *= 2; raw = (Raw *) realloc (raw, sizeof (Raw) * cap); }
				raw[nr++] = Raw { tick, seq++, st, d1, d2, 0, 0 };
			}
			p += 8 + len;
		}
		// in time order (a stable sort: ticks, then the order read -- an insertion sort over runs is enough,
		// the tracks are each sorted already: merge them by a simple shell of qsort with seq as the tie)
		qsort (raw, nr, sizeof (Raw), [] (const void *a, const void *b) -> int {
			const Raw *x = (const Raw *) a, *y = (const Raw *) b;
			if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
			if (x->tempo != y->tempo) return x->tempo ? -1 : 1;		// (a tempo first at the same tick)
			return x->seq - y->seq; });
		// ticks -> frames through the tempo map
		ev = new MidiEv[nr > 0 ? nr : 1]; nev = 0;
		unsigned us = 500000, lastTick = 0; long double at = 0;
		long double perTick = smpte ? (long double) SOUND_RATE / (fps * tpf) : (long double) us * SOUND_RATE / 1e6L / div;
		for (int i = 0; i < nr; i++)
		{
			at += (long double) (raw[i].tick - lastTick) * perTick; lastTick = raw[i].tick;
			if (raw[i].tempo) { us = raw[i].us ? raw[i].us : 500000; if (!smpte) perTick = (long double) us * SOUND_RATE / 1e6L / div; continue; }
			ev[nev++] = MidiEv { (i64) at, raw[i].st, raw[i].d1, raw[i].d2 };
		}
		// the bars: their ticks (the time signature: 4/4 if none) through the tempo changes again
		if (!smpte)
		{
			unsigned perBar = (unsigned) div * 4 * (tsNum ? tsNum : 4) >> (tsDen < 6 ? tsDen : 2);
			unsigned lastT = raw && nr ? raw[nr - 1].tick : 0;
			nbars = perBar ? (int) (lastT / perBar) + 2 : 0;
			bars = new i64[nbars > 0 ? nbars : 1];
			unsigned u2 = 500000, lt = 0; long double a2 = 0, pt = (long double) u2 * SOUND_RATE / 1e6L / div; int ti = 0;
			bool firstTempo = true;
			for (int k = 0; k < nbars; k++)
			{
				unsigned tk = (unsigned) k * perBar;
				while (ti < nr && raw[ti].tick <= tk)
				{
					if (raw[ti].tempo) { a2 += (long double) (raw[ti].tick - lt) * pt; lt = raw[ti].tick; u2 = raw[ti].us ? raw[ti].us : 500000; pt = (long double) u2 * SOUND_RATE / 1e6L / div;
						if (firstTempo) { bpm = (int) (60000000u / u2); firstTempo = false; } }
					ti++;
				}
				bars[k] = (i64) (a2 + (long double) (tk - lt) * pt);
			}
		}
		free (raw);
		length = nev ? ev[nev - 1].at : 0;
		// the notes (on .. off), the instruments
		int ncap = 1024; notes = new MidiNote[ncap]; nnotes = 0;
		i64 openAt[16][128]; unsigned char openVel[16][128];
		for (int c = 0; c < 16; c++) for (int k = 0; k < 128; k++) openAt[c][k] = -1;
		bool progSet[16] = { false };
		for (int i = 0; i < nev; i++)
		{
			int c = ev[i].st & 15, ty = ev[i].st & 0xF0, k = ev[i].d1 & 127;
			if (ty == 0xC0 && !progSet[c]) { program[c] = ev[i].d1 & 127; progSet[c] = true; }
			bool on = ty == 0x90 && ev[i].d2 > 0, off = ty == 0x80 || (ty == 0x90 && ev[i].d2 == 0);
			if ((on || off) && openAt[c][k] >= 0)
			{
				if (nnotes == ncap) { MidiNote *nn = new MidiNote[ncap * 2]; memcpy (nn, notes, sizeof (MidiNote) * nnotes); delete[] notes; notes = nn; ncap *= 2; }
				notes[nnotes++] = MidiNote { openAt[c][k], ev[i].at, (unsigned char) k, (unsigned char) c, openVel[c][k] };
				openAt[c][k] = -1;
			}
			if (on) { openAt[c][k] = ev[i].at; openVel[c][k] = ev[i].d2; used[c] = true; if (c != 9) { if (k < lowKey) lowKey = k; if (k > highKey) highKey = k; } }
		}
		if (lowKey > highKey) { lowKey = 48; highKey = 72; }
		return nev > 0;
	}
};

// ---- the SoundFont, once ----------------------------------------------------------------------------------------
static ms::SoundFont *g_sf;
static char g_sfName[96];
static bool g_sfTried;
static char g_sfPath[256];					// (settings: "" = the first of SD:/res/soundfonts)
// the SoundFonts' folders: the shared one (the package generaluser-gs puts GeneralUser GS there), then Koton's old one
static const char *const SF_DIRS[] = { "SD:/res/soundfonts", "SD:/koton/soundfonts" };
static bool soundfont_find (char *out, int cap)
{
	if (g_sfPath[0]) { void *f = kapi_open (g_sfPath); if (f) { kapi_close (f); snprintf (out, cap, "%s", g_sfPath); return true; } }
	bool found = false;
	for (unsigned k = 0; k < sizeof SF_DIRS / sizeof SF_DIRS[0] && !found; k++)
	{
		void *d = kapi_opendir (SF_DIRS[k]);
		if (!d) continue;
		struct kapi_dirent e;
		while (kapi_readdir (d, &e) > 0)
			if (!e.is_dir && ext_is (e.name, "sf2")) { snprintf (out, cap, "%s/%s", SF_DIRS[k], e.name); found = true; break; }
		kapi_closedir (d);
	}
	return found;
}
#ifdef MEDIA_SOUNDFONT_AUDIOKIT					// (in AudioKit: its own, one for the process -- aksf.cpp)
extern "C" void *ak_soundfont_default (char *err, int cap);
static ms::SoundFont *soundfont (char *err, int cap)		{ return (ms::SoundFont *) ak_soundfont_default (err, cap); }
#else
static ms::SoundFont *soundfont (char *err, int cap)
{
	if (g_sf || g_sfTried) { if (!g_sf) snprintf (err, cap, "No SoundFont to play MIDI files (SD:/res/soundfonts: the package GeneralUser GS)."); return g_sf; }
	g_sfTried = true;
	char path[256];
	if (!soundfont_find (path, sizeof path)) { snprintf (err, cap, "No SoundFont to play MIDI files (SD:/res/soundfonts: the package GeneralUser GS)."); return 0; }
	Src s;
	if (!s.open (path) || s.size == 0 || s.size > (512u << 20)) { snprintf (err, cap, "The SoundFont %s cannot be read.", path); return 0; }
	unsigned char *b = new unsigned char[(size_t) s.size];
	bool ok = s.read (b, (size_t) s.size) == s.size;
	char e[128] = "";
	if (ok) g_sf = ms::soundfont_load (b, (size_t) s.size, e, sizeof e);
	delete[] b;
	if (!g_sf) { snprintf (err, cap, "The SoundFont %s cannot be used (%s).", path, e); return 0; }
	const char *nm = ms::soundfont_name (g_sf);
	const char *base = strrchr (path, '/');
	snprintf (g_sfName, sizeof g_sfName, "%s", nm && nm[0] ? nm : base ? base + 1 : path);
	return g_sf;
}
#endif

// ---- the player: a Decoder over the song and a synthesizer -------------------------------------------------
class MidiDecoder : public Decoder
{
public:
	MidiSong song; ms::Synthesizer *syn; i64 at; int next; i64 tail;
	float *L, *R;
	MidiDecoder () : syn (0), at (0), next (0), tail (SOUND_RATE * 2), L (new float[1024]), R (new float[1024]) { strcpy (format, "MIDI"); rate = SOUND_RATE; }
	~MidiDecoder () { delete syn; delete[] L; delete[] R; }
	bool open (const char *path, char *err, int cap)
	{
		if (!song.load (path, err, cap)) return false;
		ms::SoundFont *sf = soundfont (err, cap);
		if (!sf) return false;
		ms::SynthSettings st; st.sampleRate = SOUND_RATE;
		syn = new ms::Synthesizer (sf, st);
		if (!syn->ok ()) { snprintf (err, cap, "The synthesizer cannot start (memory)."); return false; }
		frames = song.length + tail; channels = 2; bits = 16;
		return true;
	}
	int read (short *out, int n) override
	{
		if (at >= frames) return 0;
		if (n > 1024) n = 1024;
		if (at + n > frames) n = (int) (frames - at);
		int done = 0;
		while (done < n)
		{
			// the events due before the next piece
			while (next < song.nev && song.ev[next].at <= at + done) { fire (song.ev[next]); next++; }
			int piece = n - done;
			if (next < song.nev) { i64 till = song.ev[next].at - (at + done); if (till < piece) piece = till < 1 ? 1 : (int) till; }
			syn->render (L + done, R + done, piece);
			done += piece;
		}
		for (int i = 0; i < n; i++)
		{
			int l = (int) (L[i] * 32767.0f), r = (int) (R[i] * 32767.0f);
			out[2 * i] = (short) (l > 32767 ? 32767 : l < -32768 ? -32768 : l);
			out[2 * i + 1] = (short) (r > 32767 ? 32767 : r < -32768 ? -32768 : r);
		}
		at += n;
		return n;
	}
	bool seek (i64 frame) override
	{
		syn->reset ();
		next = 0;
		// what sets the sound up (programs, controllers, bends) replayed up to there; no notes
		while (next < song.nev && song.ev[next].at < frame)
		{
			int ty = song.ev[next].st & 0xF0;
			if (ty != 0x90 && ty != 0x80) fire (song.ev[next]);
			next++;
		}
		at = frame;
		return true;
	}
private:
	void fire (const MidiEv &e) { syn->processMidiMessage (e.st & 15, e.st & 0xF0, e.d1, e.d2); }
};

} // namespace media

#endif
