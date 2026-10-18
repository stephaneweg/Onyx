//
// audio.h -- the game's sound: the music (made here, from a world's style and a seed: drums, bass, chords, an
// arpeggio and a tune, played live on AudioKit's General MIDI synthesizer), the effects (short notes and slides
// on AudioKit's FM voices) and the engine's hum (a voice whose pitch follows the speed). No file is read.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef CZ_AUDIO_H
#define CZ_AUDIO_H

#include "audiokit/audiokit.h"
#include "appkit/appkit.h"
#include "race.h"

namespace cz {

// a world's music: tempo, General MIDI programs (lead, chords, bass, arpeggio), its mode
struct MusicStyle { int bpm, lead, pad, bass, arp; int minor; int swing; };
static const MusicStyle STYLES[] = {
	{ 128, 81, 89, 38, 98, 0, 0 },		// 0 the menus: space pop (square lead, new-age pad, synth bass, crystal)
	{ 132, 80, 90, 38, 11, 0, 0 },		// 1 sunny (square lead, polysynth, synth bass, vibraphone)
	{ 116, 86, 88, 35, 12, 1, 0 },		// 2 dreamy (fifths lead, new-age, fretless, marimba)
	{ 126, 82, 81, 39, 7, 1, 0 },		// 3 robot funk (saw lead, square, synth bass 2, clavinet)
	{ 122, 79, 89, 33, 108, 0, 1 },		// 4 tribal (ocarina, pad, finger bass, kalimba)
	{ 110, 9, 49, 38, 8, 0, 0 },		// 5 winter (glockenspiel, strings, synth bass, celesta)
	{ 140, 30, 61, 34, 81, 1, 0 },		// 6 heroic rock (overdrive, brass, picked bass, square)
	{ 136, 62, 53, 38, 98, 1, 0 },		// 7 epic finale (synth brass, choir, synth bass, crystal)
	{ 120, 56, 61, 32, 11, 0, 0 },		// 8 the podium (trumpet, brass, acoustic bass, vibraphone)
};
enum { NSTYLES = sizeof STYLES / sizeof STYLES[0] };

struct Music
{
	bool on = true, playing = false, ok = true;
	int style = 0; unsigned seed = 1;
	float pos = 0;				// in sixteenths
	float tempoMul = 1;
	int lastStep = -1;
	int melody[64], bassPat[16], arpPat[16];
	int prog[4];				// the chords' roots (scale degrees), one a bar
	int held[16][4];			// the keys held per channel (to stop them)
	int nheld[16];

	void start (int st, unsigned sd)
	{
		stop ();
		style = st < 0 ? 0 : st >= NSTYLES ? NSTYLES - 1 : st; seed = sd ? sd : 1;
		Rng r (seed * 2654435761u + (unsigned) style);
		static const int PROGS[][4] = { { 0, 5, 3, 4 }, { 0, 3, 4, 4 }, { 5, 3, 0, 4 }, { 0, 4, 5, 3 }, { 0, 6, 5, 4 } };
		const int *pg = PROGS[r.n (5)];
		for (int k = 0; k < 4; k++) prog[k] = pg[k];
		// the tune: 4 bars of eighths and sixteenths on the chord's notes and their neighbours
		int last = 7;
		for (int i = 0; i < 64; i++)
		{
			bool sound = (i % 4 == 0) ? r.f () < 0.85f : (i % 2 == 0) ? r.f () < 0.55f : r.f () < 0.15f;
			if (!sound) { melody[i] = -1; continue; }
			int step = r.n (5) - 2; last += step; if (last < 2) last = 4; if (last > 12) last = 10;
			melody[i] = last;
		}
		for (int i = 0; i < 16; i++) { bassPat[i] = (i % 4 == 0) ? 0 : (i % 4 == 2) ? (r.f () < 0.6f ? 0 : 7) : (r.f () < 0.25f ? 12 : -100); arpPat[i] = r.n (4); }
		ok = true;
		if (!on) return;
		ak_play_keep_output (1);
		const MusicStyle &S = STYLES[style];
		ak_program (0, S.lead); ak_program (1, S.pad); ak_program (2, S.bass); ak_program (3, S.arp);
		ak_control (0, 7, 72); ak_control (1, 7, 56); ak_control (2, 7, 96); ak_control (3, 7, 58); ak_control (9, 7, 92);
		pos = 0; lastStep = -1; playing = true;
		for (int c = 0; c < 16; c++) nheld[c] = 0;
	}
	void stop ()
	{
		if (playing) for (int c = 0; c < 16; c++) { for (int k = 0; k < nheld[c]; k++) ak_note_off (c, held[c][k]); nheld[c] = 0; }
		playing = false;
	}
	void off (int ch) { for (int k = 0; k < nheld[ch]; k++) ak_note_off (ch, held[ch][k]); nheld[ch] = 0; }
	void note (int ch, int key, int vel)
	{
		if (!ok) return;
		if (ak_note_on (ch, key, vel) < 0) { ok = false; return; }	// (no SoundFont: no music)
		if (ch != 9 && nheld[ch] < 4) held[ch][nheld[ch]++] = key;
	}
	// the scale's degree d (0 = the root) -> a key
	int key (int d, int base) const
	{
		static const int MAJ[7] = { 0, 2, 4, 5, 7, 9, 11 }, MIN[7] = { 0, 2, 3, 5, 7, 8, 10 };
		const int *sc = STYLES[style].minor ? MIN : MAJ;
		int o = d >= 0 ? d / 7 : (d - 6) / 7, dd = d - o * 7;
		return base + 12 * o + sc[dd];
	}
	void update (float dt)
	{
		if (!playing || !on || !ok) return;
		const MusicStyle &S = STYLES[style];
		pos += dt * (float) S.bpm * tempoMul / 60.0f * 4.0f;
		int stepI = (int) pos;
		while (lastStep < stepI)
		{
			lastStep++;
			int st = lastStep % 64, bar = (lastStep / 16) % 4, s16 = lastStep % 16;
			int root = prog[bar];
			// drums: kick on the beats, snare on 2 and 4, hats on the off-beats
			if (s16 % 4 == 0) note (9, 36, 110);
			if (s16 == 4 || s16 == 12) note (9, 38, 100);
			if (s16 % 2 == 1) note (9, 42, 70);
			if (s16 == 14 && bar == 3) note (9, 49, 90);
			// bass
			if (bassPat[s16] > -50) { off (2); note (2, key (root, 36) + bassPat[s16], 100); }
			// chords at each bar
			if (s16 == 0) { off (1); for (int k = 0; k < 3; k++) note (1, key (root + 2 * k, 60), 60); }
			// arpeggio
			if (s16 % 2 == 0) { off (3); note (3, key (root + 2 * arpPat[s16] % 7, 72), 64); }
			// the tune (from the second time round)
			if (lastStep >= 64 && melody[st] >= 0) { off (0); note (0, key (melody[st] + root % 2, 72), 92); }
			else if (lastStep >= 64 && st % 4 == 3) off (0);
		}
	}
};

struct Sfx
{
	bool on = true;
	unsigned endMs[6] = { 0 };
	int next = 0;
	unsigned humVoice = 11; bool humOn = false;
	struct Q { unsigned at, mhz, ms; int wave, vol; } q[24]; int nq = 0;
	static unsigned ms () { return kapi_clock_us () / 1000; }
	void play (unsigned hz, unsigned dur, int wave = SOUND_SQUARE, int vol = 90)
	{
		if (!on || !hz) return;
		int v = next; next = (next + 1) % 6;
		ak_fm_start (12 + (v % 4), hz * 1000u, wave, vol);
		endMs[v % 4] = ms () + dur;
	}
	void later (unsigned delay, unsigned hz, unsigned dur, int wave = SOUND_SQUARE, int vol = 90)
	{
		if (!on || nq >= 24) return;
		q[nq++] = Q { ms () + delay, hz, dur, wave, vol };
	}
	void tick ()
	{
		unsigned t = ms ();
		for (int v = 0; v < 4; v++) if (endMs[v] && (int) (t - endMs[v]) >= 0) { ak_fm_stop (12 + v); endMs[v] = 0; }
		for (int i = 0; i < nq; )
		{
			if ((int) (t - q[i].at) >= 0) { play (q[i].mhz, q[i].ms, q[i].wave, q[i].vol); q[i] = q[--nq]; }
			else i++;
		}
	}
	// the engine: a low hum whose pitch follows the speed (0: off)
	void hum (float speed, bool boost)
	{
		if (!on || speed <= 0.5f) { if (humOn) { ak_fm_stop ((int) humVoice); humOn = false; } return; }
		unsigned hz = 55 + (unsigned) (speed * 3.2f) + (boost ? 40 : 0);
		ak_fm_start ((int) humVoice, hz * 1000u, SOUND_SAW, 26);
		humOn = true;
	}
	void silence () { ak_fm_stop (-1); humOn = false; nq = 0; for (int v = 0; v < 4; v++) endMs[v] = 0; }
	// the game's events -> sounds (only those of the human players, and what is near them)
	void events (const Race &R, int listener)
	{
		for (int e = 0; e < R.nev; e++)
		{
			const Event &E = R.ev[e];
			bool mine = E.craft >= 0 && R.c[E.craft].human;
			bool near = listener >= 0 && E.craft >= 0 && fabsf (R.T->ahead (R.c[E.craft].s, R.c[listener].s)) < 30;
			switch (E.kind)
			{
			case EV_COUNT: play (440, 180, SOUND_SQUARE, 110); break;
			case EV_GO: play (880, 420, SOUND_SQUARE, 120); later (0, 1320, 420, SOUND_SQUARE, 80); break;
			case EV_BOOSTPAD: if (mine) { play (300, 90, SOUND_SAW, 90); later (60, 600, 160, SOUND_SAW, 90); } break;
			case EV_BOX: if (mine) { play (1568, 70, SOUND_SQUARE, 90); later (70, 2093, 120, SOUND_SQUARE, 90); } break;
			case EV_ITEM_GOT: if (mine) play (1046, 140, SOUND_TRIANGLE, 110); break;
			case EV_USE: if (mine || near) { play (700, 80, SOUND_SQUARE, 90); later (50, 1000, 120, SOUND_SQUARE, 80); } break;
			case EV_HIT: if (mine || near) { play (180, 220, SOUND_SAW, 120); later (120, 120, 200, SOUND_SAW, 100); } break;
			case EV_BLOCKED: if (mine || near) play (2400, 160, SOUND_TRIANGLE, 100); break;
			case EV_WALL: if (mine) play (90, 110, SOUND_NOISE, 90); break;
			case EV_BUMP: if (mine) play (140, 90, SOUND_NOISE, 80); break;
			case EV_LAP: if (mine) { play (1318, 120, SOUND_SQUARE, 100); later (130, 1318, 160, SOUND_SQUARE, 100); } break;
			case EV_FINALLAP: if (mine) { play (784, 150, SOUND_SQUARE, 110); later (160, 1046, 150, SOUND_SQUARE, 110); later (320, 1318, 300, SOUND_SQUARE, 110); } break;
			case EV_FINISH: if (mine) { play (1046, 160, SOUND_SQUARE, 120); later (170, 1318, 160, SOUND_SQUARE, 120); later (340, 1568, 160, SOUND_SQUARE, 120); later (510, 2093, 500, SOUND_SQUARE, 120); } break;
			case EV_RAMP: if (mine) play (400, 250, SOUND_TRIANGLE, 80); break;
			case EV_LAND: if (mine) play (110, 120, SOUND_NOISE, 90); break;
			case EV_MINITURBO: case EV_START_BOOST: if (mine) { play (500, 80, SOUND_SAW, 100); later (60, 900, 200, SOUND_SAW, 100); } break;
			case EV_POP: if (mine || near) play (1700, 60, SOUND_TRIANGLE, 90); break;
			case EV_STARBIT: if (mine) play (2637, 70, SOUND_TRIANGLE, 80); break;
			case EV_DRIFTLVL: if (mine) play (E.arg == 3 ? 1760 : E.arg == 2 ? 1318 : 988, 70, SOUND_TRIANGLE, 90); break;
			case EV_PLACE_UP: if (mine) play (1568, 60, SOUND_TRIANGLE, 70); break;
			case EV_WARP: if (mine) { for (int k = 0; k < 6; k++) later ((unsigned) k * 70, 1046u + (unsigned) k * 262u, 90, SOUND_TRIANGLE, 100); } break;
			case EV_EMP: case EV_NEBULA: case EV_WELL: case EV_BEAM: if (mine || near) { play (300, 300, SOUND_SAW, 80); later (100, 250, 300, SOUND_SAW, 70); } break;
			}
		}
	}
};

} // namespace cz

#endif
