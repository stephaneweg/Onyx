//
// doom_sound.c -- Doom's sound on Onyx: the doomgeneric sound and music modules.
//
//   * Sound effects: the WAD's DS* lumps (8-bit, 11025 Hz) mixed here -- 16 channels, the
//     volume and stereo separation Doom gives -- into the kernel's PCM stream
//     (kapi_sound_write, 44100 Hz stereo), a little ahead of the output, at each game tic.
//   * Music: the MUS (Doom) or MIDI (Freedoom) songs played on the kernel's FM voices (kapi_sound_instrument, OPL2-
//     style 2-operator), with the instruments of the WAD's GENMIDI lump -- the same OPL
//     patches the original Adlib / Sound Blaster driver used, so it sounds like DOS Doom.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "kapi.h"
#include "doomtype.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"

int use_libsamplerate = 0;				// (i_sound.c's settings for the SDL module)
float libsamplerate_scale = 0.65f;

static int s_audio = 0;					// 1: the output is ours

static int audio_ok (void)
{
	if (!s_audio) s_audio = kapi_sound_acquire () == 1 ? 1 : -1;
	return s_audio == 1;
}

// ---- sound effects -------------------------------------------------------------------------------
#define NCH	16
#define RATE	SOUND_RATE
struct chan { const unsigned char *data; unsigned len, pos, step; int vl, vr; int on; };
static struct chan s_ch[NCH];
static boolean s_prefix;

static boolean sfx_init (boolean use_sfx_prefix)
{
	s_prefix = use_sfx_prefix;
	memset (s_ch, 0, sizeof s_ch);
	return audio_ok () ? true : false;
}
static void sfx_shutdown (void) {}

static int sfx_lump (sfxinfo_t *sfx)
{
	char name[16];
	if (sfx->link) sfx = sfx->link;
	snprintf (name, sizeof name, s_prefix ? "ds%s" : "%s", sfx->name);
	return W_CheckNumForName (name);
}

static void set_params (int c, int vol, int sep)
{
	if (vol < 0) vol = 0; if (vol > 127) vol = 127;
	if (sep < 0) sep = 0; if (sep > 254) sep = 254;
	s_ch[c].vl = vol * (254 - sep) / 127;		// 0..254
	s_ch[c].vr = vol * sep / 127;
}

static int sfx_start (sfxinfo_t *sfx, int c, int vol, int sep)
{
	if (c < 0 || c >= NCH || sfx->lumpnum < 0) return -1;
	int lump = sfx->lumpnum;
	const unsigned char *d = W_CacheLumpNum (lump, PU_STATIC);
	int size = W_LumpLength (lump);
	if (size < 8 || d[0] != 3 || d[1] != 0) return -1;	// (a DMX digital sound)
	unsigned rate = d[2] | (d[3] << 8), len = d[4] | (d[5] << 8) | (d[6] << 16) | ((unsigned) d[7] << 24);
	if (len > (unsigned) size - 8) len = (unsigned) size - 8;
	if (len <= 32) return -1;
	struct chan *h = &s_ch[c];
	h->data = d + 8 + 16; h->len = len - 32;		// (16 padding samples at each end)
	h->pos = 0; h->step = (unsigned) (((unsigned long long) rate << 16) / RATE);
	set_params (c, vol, sep);
	h->on = 1;
	return c;
}
static void sfx_stop (int c) { if (c >= 0 && c < NCH) s_ch[c].on = 0; }
static boolean sfx_playing (int c) { return c >= 0 && c < NCH && s_ch[c].on ? true : false; }
static void sfx_params (int c, int vol, int sep) { if (c >= 0 && c < NCH) set_params (c, vol, sep); }
static void sfx_cache (sfxinfo_t *s, int n) { (void) s; (void) n; }

// Mix enough to keep ~70 ms queued (the output drains it while Doom renders).
static void sfx_update (void)
{
	if (!audio_ok ()) return;
	unsigned rate, freeFrames, owner;
	kapi_sound_status (&rate, &freeFrames, &owner);
	static unsigned cap = 0;
	if (freeFrames > cap) cap = freeFrames;
	unsigned queued = cap - freeFrames, target = RATE * 70 / 1000;
	if (queued >= target) return;
	unsigned n = target - queued;
	static short buf[4096 * 2];
	if (n > 4096) n = 4096;
	for (unsigned i = 0; i < n; i++)
	{
		int l = 0, r = 0;
		for (int c = 0; c < NCH; c++)
		{
			struct chan *h = &s_ch[c];
			if (!h->on) continue;
			unsigned idx = h->pos >> 16;
			if (idx >= h->len) { h->on = 0; continue; }
			int s = ((int) h->data[idx] - 128) << 8;	// s16
			l += s * h->vl >> 8; r += s * h->vr >> 8;
			h->pos += h->step;
		}
		l = l > 32767 ? 32767 : l < -32768 ? -32768 : l;
		r = r > 32767 ? 32767 : r < -32768 ? -32768 : r;
		buf[i * 2] = (short) l; buf[i * 2 + 1] = (short) r;
	}
	kapi_sound_write (buf, n);
}

static snddevice_t s_devices[] = { SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER,
				   SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32, SNDDEVICE_GENMIDI, SNDDEVICE_ADLIB };

sound_module_t DG_sound_module = {
	s_devices, sizeof s_devices / sizeof s_devices[0],
	sfx_init, sfx_shutdown, sfx_lump, sfx_update, sfx_params, sfx_start, sfx_stop, sfx_playing, sfx_cache };

// ---- music: MUS / MIDI on the FM voices ----------------------------------------------------------------------
// GENMIDI: "#OPL_II#", then 175 instruments (128 melodic, then percussion for keys 35..81).
typedef struct { byte tremolo, attack, sustain, waveform, scale, level; } __attribute__ ((packed)) gm_op;
typedef struct { gm_op mod; byte feedback; gm_op car; byte unused; short base_note; } __attribute__ ((packed)) gm_voice;
typedef struct { unsigned short flags; byte fine_tuning, fixed_note; gm_voice voices[2]; } __attribute__ ((packed)) gm_instr;

#define NVOICES	SOUND_VOICES
static const gm_instr *s_gm = 0;
static struct { int on, chan, note, key, age; const gm_instr *ins; } s_v[NVOICES];
static const gm_instr *s_voiceIns[NVOICES];			// the patch each voice holds
static struct { int instr, volume, bend; } s_mch[16];
// A song, MUS or MIDI, turned into one list of timed events when it is registered.
enum { EV_OFF, EV_ON, EV_PROGRAM, EV_VOLUME, EV_BEND, EV_ALLOFF, EV_END };
struct mev { unsigned long long us; unsigned char type, ch, a, b; };
struct song { struct mev *ev; int n; };
static struct song *s_song = 0;
static int s_pos = 0, s_playing = 0, s_looping = 0, s_paused = 0;
static int s_musVol = 100;					// 0..127
static unsigned long long s_songUs = 0, s_last = 0;		// the song's time, the clock at the last poll
static int s_age = 0;

#ifdef __aarch64__
static unsigned long long mus_now_us (void)
{
	unsigned long long c, f;
	__asm__ volatile ("mrs %0, cntpct_el0" : "=r" (c));
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (f));
	return f ? c * 1000000ull / f : 0;
}
#else
unsigned long long mus_now_us (void);			// (the PC test: tools/tests/doom)
#endif

static void fm_op (struct kapi_fm_op *o, const gm_op *g)
{
	o->mult = g->tremolo & 15;
	o->flags = (unsigned char) (((g->tremolo & 0x20) ? FM_SUSTAINED : 0) | ((g->tremolo & 0x80) ? FM_TREMOLO : 0) |
				    ((g->tremolo & 0x40) ? FM_VIBRATO : 0));
	o->attack = g->attack >> 4; o->decay = g->attack & 15;
	o->sustain = g->sustain >> 4; o->release = g->sustain & 15;
	o->wave = g->waveform & 3;
	o->ksl = g->scale >> 6;
	o->level = g->level & 63;
}

static void voice_off (int v)
{
	if (s_v[v].on) kapi_sound_stop (v);
	s_v[v].on = 0;
}

static void all_off (void)
{
	for (int v = 0; v < NVOICES; v++) voice_off (v);
}

static void note_off (int ch, int key)
{
	for (int v = 0; v < NVOICES; v++)
		if (s_v[v].on && s_v[v].chan == ch && s_v[v].key == key) voice_off (v);
}

static unsigned note_mhz (int note, int bend)			// bend: 128 = none, 64 per semitone
{
	float semis = (float) (note - 69) + (float) (bend - 128) / 64.0f;
	return (unsigned) (440000.0f * powf (2.0f, semis / 12.0f));
}

static void note_on (int ch, int key, int vel)
{
	if (!s_gm || !audio_ok ()) return;
	const gm_instr *ins;
	int note = key;
	if (ch == 15)
	{
		if (key < 35 || key > 81) return;
		ins = &s_gm[128 + key - 35];
	}
	else ins = &s_gm[s_mch[ch].instr & 127];
	if (ins->flags & 1) note = ins->fixed_note;		// a fixed pitch (drums)
	note += ins->voices[0].base_note;
	if (note < 0) note = 0;
	if (note > 127) note = 127;
	// a free voice, else the oldest
	int best = -1;
	for (int v = 0; v < NVOICES && best < 0; v++) if (!s_v[v].on) best = v;
	if (best < 0)
	{
		best = 0;
		for (int v = 1; v < NVOICES; v++) if (s_v[v].age < s_v[best].age) best = v;
		voice_off (best);
	}
	if (s_voiceIns[best] != ins)
	{
		struct kapi_fm_instrument fi;
		fm_op (&fi.op[0], &ins->voices[0].mod);
		fm_op (&fi.op[1], &ins->voices[0].car);
		fi.feedback = (ins->voices[0].feedback >> 1) & 7;
		fi.connection = ins->voices[0].feedback & 1;
		kapi_sound_instrument (best, &fi);
		s_voiceIns[best] = ins;
	}
	int vol = vel * s_mch[ch].volume / 127 * s_musVol / 127;	// 0..127
	vol = vol * 2 > 255 ? 255 : vol * 2;
	s_v[best].on = 1; s_v[best].chan = ch; s_v[best].key = key; s_v[best].note = note; s_v[best].age = ++s_age; s_v[best].ins = ins;
	kapi_sound_start (best, note_mhz (note, ch == 15 ? 128 : s_mch[ch].bend), SOUND_FM, vol);
}

static void bend (int ch, int b)
{
	s_mch[ch].bend = b;
	for (int v = 0; v < NVOICES; v++)
		if (s_v[v].on && s_v[v].chan == ch && ch != 15)
		{
			// (a restart with the new pitch: the kernel voices have no pitch-only change)
			int vol = 127 * s_mch[ch].volume / 127 * s_musVol / 127 * 2;
			kapi_sound_start (v, note_mhz (s_v[v].note, b), SOUND_FM, vol > 255 ? 255 : vol);
		}
}

static void reset_channels (void)
{
	for (int c = 0; c < 16; c++) { s_mch[c].instr = 0; s_mch[c].volume = 100; s_mch[c].bend = 128; }
}

static void ev_add (struct song *g, int *cap, unsigned long long us, int type, int ch, int a, int b)
{
	if (g->n == *cap)
	{
		*cap = *cap ? *cap * 2 : 1024;
		struct mev *e = realloc (g->ev, (size_t) *cap * sizeof *e);
		if (!e) return;
		g->ev = e;
	}
	struct mev *e = &g->ev[g->n++];
	e->us = us; e->type = (unsigned char) type; e->ch = (unsigned char) ch; e->a = (unsigned char) a; e->b = (unsigned char) b;
}

// MUS (Doom's own format): 140 ticks a second, channel 15 = the drums.
static struct song *parse_mus (const byte *d, int len)
{
	int scoreLen = d[4] | (d[5] << 8), start = d[6] | (d[7] << 8), end = start + scoreLen;
	if (end > len) end = len;
	struct song *g = calloc (1, sizeof *g); int cap = 0;
	unsigned long long tick = 0; int vel[16];
	for (int c = 0; c < 16; c++) vel[c] = 100;
	int p = start;
	while (p < end)
	{
		byte ev = d[p++];
		int type = (ev >> 4) & 7, ch = ev & 15;
		unsigned long long us = tick * 1000000ull / 140;
		if (type == 6) break;						// score end
		if (p >= end) break;
		switch (type)
		{
		case 0: ev_add (g, &cap, us, EV_OFF, ch, d[p++] & 127, 0); break;
		case 1: { byte k = d[p++]; if ((k & 0x80) && p < end) vel[ch] = d[p++] & 127; ev_add (g, &cap, us, EV_ON, ch, k & 127, vel[ch]); break; }
		case 2: ev_add (g, &cap, us, EV_BEND, ch, d[p++], 0); break;
		case 3: { byte c = d[p++]; if (c == 10 || c == 11) ev_add (g, &cap, us, EV_ALLOFF, ch, 0, 0); break; }
		case 4:
		{
			byte c = d[p++], v = p < end ? d[p++] : 0;
			if (c == 0) ev_add (g, &cap, us, EV_PROGRAM, ch, v & 127, 0);
			else if (c == 3) ev_add (g, &cap, us, EV_VOLUME, ch, v & 127, 0);
			break;
		}
		default: p++; break;
		}
		if (ev & 0x80)							// a delay follows
		{
			unsigned long long dl = 0; byte b;
			do { b = d[p++]; dl = dl * 128 + (b & 127); } while ((b & 0x80) && p < end);
			tick += dl;
		}
	}
	ev_add (g, &cap, tick * 1000000ull / 140, EV_END, 0, 0, 0);
	return g;
}

// Standard MIDI files (Freedoom's music): every track, in time order, the tempo map applied.
// Channel 9 (the MIDI drums) becomes 15, as in MUS.
struct raw { unsigned long long tick; int order; unsigned char st, a, b; unsigned tempo; };
static int raw_cmp (const void *x, const void *y)
{
	const struct raw *p = x, *q = y;
	if (p->tick != q->tick) return p->tick < q->tick ? -1 : 1;
	return p->order - q->order;
}
static unsigned vlq (const byte *d, int *p, int end)
{
	unsigned v = 0; byte b;
	do { if (*p >= end) return v; b = d[(*p)++]; v = (v << 7) | (b & 127); } while (b & 0x80);
	return v;
}
static struct song *parse_midi (const byte *d, int len)
{
	if (len < 14) return 0;
	int ntracks = (d[10] << 8) | d[11], division = (d[12] << 8) | d[13];
	if (division & 0x8000) division = 96;				// (SMPTE time: rare, approximated)
	int cap = 4096, n = 0, order = 0;
	struct raw *r = malloc ((size_t) cap * sizeof *r);
	int p = 8 + ((d[4] << 24) | (d[5] << 16) | (d[6] << 8) | d[7]);
	for (int t = 0; t < ntracks && p + 8 <= len; t++)
	{
		if (memcmp (d + p, "MTrk", 4)) break;
		int tl = (d[p + 4] << 24) | (d[p + 5] << 16) | (d[p + 6] << 8) | d[p + 7];
		int q = p + 8, end = q + tl > len ? len : q + tl;
		unsigned long long tick = 0; unsigned char status = 0;
		while (q < end)
		{
			tick += vlq (d, &q, end);
			if (q >= end) break;
			unsigned char c = d[q];
			if (c & 0x80) { status = c; q++; }
			struct raw e = { tick, order++, status, 0, 0, 0 };
			if (status == 0xFF)
			{
				unsigned char type = d[q++];
				unsigned l = vlq (d, &q, end);
				if (type == 0x51 && l >= 3) { e.tempo = ((unsigned) d[q] << 16) | (d[q + 1] << 8) | d[q + 2]; }
				q += (int) l;
				if (type == 0x2F) break;
				if (!e.tempo) continue;
			}
			else if (status == 0xF0 || status == 0xF7) { q += (int) vlq (d, &q, end); continue; }
			else
			{
				int hi = status & 0xF0;
				e.a = d[q++] & 127;
				if (hi != 0xC0 && hi != 0xD0) e.b = q < end ? d[q++] & 127 : 0;
			}
			if (n == cap) { cap *= 2; struct raw *nr = realloc (r, (size_t) cap * sizeof *r); if (!nr) break; r = nr; }
			r[n++] = e;
		}
		p = 8 + tl + p;
	}
	qsort (r, (size_t) n, sizeof *r, raw_cmp);
	struct song *g = calloc (1, sizeof *g); int gcap = 0;
	unsigned long long us = 0, lastTick = 0; unsigned tempo = 500000;
	for (int i = 0; i < n; i++)
	{
		us += (r[i].tick - lastTick) * tempo / (unsigned) division;
		lastTick = r[i].tick;
		if (r[i].st == 0xFF) { tempo = r[i].tempo; continue; }
		int ch = r[i].st & 15, hi = r[i].st & 0xF0;
		ch = ch == 9 ? 15 : ch == 15 ? 9 : ch;
		if (hi == 0x90 && r[i].b) ev_add (g, &gcap, us, EV_ON, ch, r[i].a, r[i].b);
		else if (hi == 0x80 || hi == 0x90) ev_add (g, &gcap, us, EV_OFF, ch, r[i].a, 0);
		else if (hi == 0xC0) ev_add (g, &gcap, us, EV_PROGRAM, ch, r[i].a, 0);
		else if (hi == 0xB0 && r[i].a == 7) ev_add (g, &gcap, us, EV_VOLUME, ch, r[i].b, 0);
		else if (hi == 0xB0 && (r[i].a == 120 || r[i].a == 123)) ev_add (g, &gcap, us, EV_ALLOFF, ch, 0, 0);
		else if (hi == 0xE0) ev_add (g, &gcap, us, EV_BEND, ch, ((r[i].b << 7) | r[i].a) >> 6, 0);
	}
	ev_add (g, &gcap, us, EV_END, 0, 0, 0);
	free (r);
	return g;
}

// Run the song's events up to now.
static void mus_poll (void)
{
	unsigned long long t = mus_now_us ();
	long long elapsed = (long long) (t - s_last);
	s_last = t;
	if (!s_playing || s_paused || !s_song) return;
	if (elapsed > 200000) elapsed = 200000;				// (a long stall: do not rush)
	s_songUs += (unsigned long long) elapsed;
	int guard = 0;
	while (s_pos < s_song->n && s_song->ev[s_pos].us <= s_songUs && guard++ < 4000)
	{
		const struct mev *e = &s_song->ev[s_pos++];
		switch (e->type)
		{
		case EV_OFF: note_off (e->ch, e->a); break;
		case EV_ON: note_on (e->ch, e->a, e->b); break;
		case EV_PROGRAM: s_mch[e->ch].instr = e->a; break;
		case EV_VOLUME: s_mch[e->ch].volume = e->a; break;
		case EV_BEND: bend (e->ch, e->a); break;
		case EV_ALLOFF: for (int v = 0; v < NVOICES; v++) if (s_v[v].on && s_v[v].chan == e->ch) voice_off (v); break;
		case EV_END:
			all_off ();
			if (!s_looping) { s_playing = 0; return; }
			s_pos = 0; s_songUs = 0; reset_channels ();
			return;
		}
	}
}

// Also called while Doom waits for its next tic: the notes then keep a finer time than 1/35 s.
void onyx_music_poll (void) { mus_poll (); }

static boolean mus_init (void) { reset_channels (); return true; }
static void mus_shutdown (void) { all_off (); }
static void mus_volume (int v) { s_musVol = v < 0 ? 0 : v > 127 ? 127 : v; }
static void mus_pause (void) { s_paused = 1; all_off (); }
static void mus_resume (void) { s_paused = 0; s_last = mus_now_us (); }

static void *mus_register (void *data, int len)
{
	const byte *d = data;
	if (!s_gm)
	{
		int lump = W_CheckNumForName ("GENMIDI");
		if (lump >= 0 && W_LumpLength (lump) >= 8 + 175 * (int) sizeof (gm_instr))
			s_gm = (const gm_instr *) ((const byte *) W_CacheLumpNum (lump, PU_STATIC) + 8);
	}
	if (len >= 16 && !memcmp (d, "MUS\x1a", 4)) return parse_mus (d, len);
	if (len >= 14 && !memcmp (d, "MThd", 4)) return parse_midi (d, len);
	return NULL;
}
static void mus_unregister (void *h)
{
	struct song *g = h;
	if (!g) return;
	if (g == s_song) { all_off (); s_song = 0; s_playing = 0; }
	free (g->ev); free (g);
}

static void mus_play (void *h, boolean looping)
{
	all_off ();
	s_song = h;
	if (!s_song) return;
	s_pos = 0; s_songUs = 0; s_looping = looping; s_playing = 1; s_paused = 0;
	s_last = mus_now_us ();
	reset_channels ();
}
static void mus_stop (void) { all_off (); s_playing = 0; }
static boolean mus_isplaying (void) { return s_playing ? true : false; }

music_module_t DG_music_module = {
	s_devices, sizeof s_devices / sizeof s_devices[0],
	mus_init, mus_shutdown, mus_volume, mus_pause, mus_resume, mus_register, mus_unregister,
	mus_play, mus_stop, mus_isplaying, mus_poll };
