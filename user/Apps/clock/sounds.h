//
// sounds.h -- the Clock's three alarm sounds (02 #24, 03 R-3): Chimes (a rising arpeggio, a bell-like 2-operator FM
// patch), Beeps (short high square notes), Marimba (a soft phrase, a woody FM patch) -- on AudioKit's FM voices: no
// file, no SoundFont, they start at once. A pattern is a list of notes stepped by the window's onTick from the ticks
// (100 a second); looped while an alarm rings, one round for the editor's Test. Before a sound, the output is probed
// (ak_out_open (0, 0): 1 ours -- closed at once, the player takes it with its own settings for the voices --, 0 busy:
// another program holds it, -1 no sound): no sound possible, the ring is silent and says so. Stored by their tokens:
// "chimes", "beeps", "marimba". One translation unit with main.cpp.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef CLOCK_SOUNDS_H
#define CLOCK_SOUNDS_H

struct SndNote { short at; short hz; short len; };		// its start and length, hundredths; its pitch
struct SndPattern { const char *token; int wave; int volume; int round; int n; SndNote note[8]; };	// round: hundredths
static const SndPattern SND_PATTERNS[3] =
{
	{ "chimes",  SOUND_FM,     200, 220, 4, { { 0, 1047, 90 }, { 25, 1319, 90 }, { 50, 1568, 90 }, { 75, 2093, 120 } } },
	{ "beeps",   SOUND_SQUARE,  70, 120, 4, { { 0, 2093, 9 }, { 18, 2093, 9 }, { 36, 2093, 9 }, { 54, 2093, 9 } } },
	{ "marimba", SOUND_FM,     220, 200, 5, { { 0, 784, 40 }, { 20, 1047, 40 }, { 40, 1319, 40 }, { 60, 1047, 40 }, { 100, 1568, 70 } } },
};
enum { SND_V0 = 4, SND_NV = 4 };				// the voices used (4..7)

static const SndPattern *g_snd;				// the pattern sounding (0: none)
static bool g_sndLoop;
static long g_sndStart;					// the tick its round started
static int g_sndNext;					// its next note
static long g_sndEnd[SND_NV];				// a voice's end (tick; 0 idle)
static int g_sndVoice;

// The FM patches (OPL2 style: op[0] the modulator, op[1] the carrier)
static struct kapi_fm_op snd_op (int mult, int level, int attack, int decay, int sustain, int release)
{
	struct kapi_fm_op o;
	memset (&o, 0, sizeof o);
	o.mult = (unsigned char) mult; o.level = (unsigned char) level; o.attack = (unsigned char) attack;
	o.decay = (unsigned char) decay; o.sustain = (unsigned char) sustain; o.release = (unsigned char) release;
	return o;
}
static void snd_instruments (const SndPattern &p)
{
	struct kapi_fm_instrument ins;
	memset (&ins, 0, sizeof ins);
	if (!strcmp (p.token, "chimes"))			// a bell: an inharmonic modulator, a slow decay
	{
		ins.op[0] = snd_op (7, 18, 15, 5, 15, 5);
		ins.op[1] = snd_op (2, 0, 15, 3, 15, 4);
		ins.feedback = 2; ins.connection = 0;
	}
	else							// a marimba: a short woody knock
	{
		ins.op[0] = snd_op (4, 24, 15, 8, 15, 8);
		ins.op[1] = snd_op (1, 0, 15, 6, 15, 7);
		ins.feedback = 1; ins.connection = 0;
	}
	for (int v = 0; v < SND_NV; v++) ak_fm_instrument (SND_V0 + v, &ins);
}

// Can a sound be heard now? -> 1 yes, 0 the output is another program's, -1 no sound at all.
static int sound_probe ()
{
	int r = ak_out_open (0, 0);
	if (r == 1) ak_out_close ();				// (the player takes it back with its own settings)
	return r > 0 ? 1 : r == 0 ? 0 : -1;
}
static void sound_stop ()
{
	if (!g_snd) return;
	ak_fm_silence ();
	for (int v = 0; v < SND_NV; v++) g_sndEnd[v] = 0;
	g_snd = 0;
}
// The sound of that token, looped or one round -> 1 playing, 0 busy, -1 no sound (logged: the PC tests read it)
static int sound_play (const char *token, bool loop)
{
	sound_stop ();
	int r = sound_probe ();
	if (r != 1) { say ("sound unavailable %s", r == 0 ? "busy" : "none"); return r; }
	const SndPattern *p = &SND_PATTERNS[0];
	for (int i = 0; i < 3; i++) if (!strcmp (token, SND_PATTERNS[i].token)) p = &SND_PATTERNS[i];
	if (p->wave == SOUND_FM) snd_instruments (*p);
	g_snd = p; g_sndLoop = loop; g_sndStart = (long) kapi_get_ticks (); g_sndNext = 0;
	say ("sound %s%s", p->token, loop ? " (looped)" : "");
	return 1;
}
static int sound_test (const char *token) { return sound_play (token, false); }
// Each turn of the loop: the notes due started, the ones done stopped, the round started again (looped).
static void sound_step ()
{
	if (!g_snd) return;
	long now = (long) kapi_get_ticks (), t = now - g_sndStart;
	while (g_sndNext < g_snd->n && g_snd->note[g_sndNext].at <= t)
	{
		const SndNote &n = g_snd->note[g_sndNext++];
		int v = g_sndVoice; g_sndVoice = (g_sndVoice + 1) % SND_NV;
		ak_fm_start (SND_V0 + v, (unsigned) n.hz * 1000u, g_snd->wave, g_snd->volume);
		g_sndEnd[v] = now + n.len;
	}
	for (int v = 0; v < SND_NV; v++)
		if (g_sndEnd[v] && now >= g_sndEnd[v]) { ak_fm_stop (SND_V0 + v); g_sndEnd[v] = 0; }
	if (t >= g_snd->round)
	{
		if (g_sndLoop) { g_sndStart += g_snd->round; g_sndNext = 0; }
		else { bool busy = false; for (int v = 0; v < SND_NV; v++) busy |= g_sndEnd[v] != 0; if (!busy) g_snd = 0; }
	}
}

#endif
