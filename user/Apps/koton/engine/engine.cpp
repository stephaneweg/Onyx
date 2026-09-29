//
// engine.cpp -- Koton's audio engine (engine.h). The block loop: take the commands, dispatch the
// song's events up to the next one (sample-accurate at the slice grid: a block is cut where an event
// falls), render each track's synth, run its inserts, mix with its strip, add the preview voice,
// soft-clip. No allocation, no lock, no system call in render ().
//
#include "engine.h"
#include <math.h>
#include <string.h>

namespace kt {

static inline unsigned ldAcq (volatile unsigned *p) { return __atomic_load_n (p, __ATOMIC_ACQUIRE); }
static inline void stRel (volatile unsigned *p, unsigned v) { __atomic_store_n (p, v, __ATOMIC_RELEASE); }

float softClip (float x)
{
	const float T = 0.75f;
	if (x > T) return T + (1.0f - T) * tanhf ((x - T) / (1.0f - T));
	if (x < -T) return -T + (1.0f - T) * tanhf ((x + T) / (1.0f - T));
	return x;
}

void toS16 (const float *l, const float *r, short *out, int n, float gain)
{
	for (int i = 0; i < n; i++)
	{
		float a = softClip (l[i] * gain), b = softClip (r[i] * gain);
		int x = (int) (a * 32767.0f), y = (int) (b * 32767.0f);
		out[2 * i] = (short) (x > 32767 ? 32767 : x < -32768 ? -32768 : x);
		out[2 * i + 1] = (short) (y > 32767 ? 32767 : y < -32768 ? -32768 : y);
	}
}

Engine::Engine () : masterGain (1), position (0), playing (0), masterPeakL (0), masterPeakR (0), activeVoices (0), renderUs (0),
	m_sf (0), m_rate (44100), m_nSynth (0), m_preview (0), m_bufL (0), m_bufR (0), m_mixL (0), m_mixR (0), m_wr (0), m_rd (0),
	m_song (0), m_pos (0), m_playing (false), m_loop (false), m_metronome (false), m_loopA (0), m_loopB (0), m_lastBeatClick (-1),
	m_prevSong (0), m_prevEvent (0), m_prevPos (0)
{
	for (int i = 0; i < ENGINE_MAX_TRACKS; i++)
	{
		m_synth[i] = 0; m_ext[i] = 0; m_nextEvent[i] = 0; peakL[i] = peakR[i] = 0;
		for (int k = 0; k < 4; k++) m_fx[i][k] = 0;
	}
	for (int i = 0; i < RETIRE; i++) m_retire[i] = 0;
}

Engine::~Engine ()
{
	for (int i = 0; i < ENGINE_MAX_TRACKS; i++) delete m_synth[i];
	delete m_preview;
	delete m_song; delete m_prevSong;
	for (int i = 0; i < RETIRE; i++) delete m_retire[i];
	// songs still in the command ring
	while (m_rd != m_wr)
	{
		Command &c = m_ring[m_rd % RING];
		if (c.type == CMD_SONG || c.type == CMD_PREVIEW) delete (CompiledSong *) c.p;
		m_rd++;
	}
	delete [] m_bufL; delete [] m_bufR; delete [] m_mixL; delete [] m_mixR;
}

static ms::Synthesizer *newSynth (const ms::SoundFont *sf, int rate)
{
	ms::SynthSettings s; s.sampleRate = rate; s.blockSize = 64; s.maxPolyphony = 64; s.enableReverbAndChorus = true;
	ms::Synthesizer *y = new ms::Synthesizer (sf, s);
	if (!y->ok ()) { delete y; return 0; }
	y->masterVolume = 1.0f;			// Koton sets MasterVolume = the instrument boost (1 by default)
	return y;
}

bool Engine::init (const ms::SoundFont *sf, int sampleRate)
{
	m_sf = sf; m_rate = sampleRate;
	m_bufL = new float[ENGINE_BLOCK]; m_bufR = new float[ENGINE_BLOCK];
	m_mixL = new float[ENGINE_BLOCK]; m_mixR = new float[ENGINE_BLOCK];
	if (sf) m_preview = newSynth (sf, sampleRate);
	return true;
}

bool Engine::ensureTracks (int n)
{
	if (n > ENGINE_MAX_TRACKS) n = ENGINE_MAX_TRACKS;
	if (!m_sf) return false;
	for (int i = 0; i < n; i++)
		if (!m_synth[i])
		{
			ms::Synthesizer *s = newSynth (m_sf, m_rate);
			if (!s) return false;
			__atomic_store_n (&m_synth[i], s, __ATOMIC_RELEASE);
		}
	if (n > m_nSynth) __atomic_store_n (&m_nSynth, n, __ATOMIC_RELEASE);
	return true;
}

bool Engine::post (const Command &c)
{
	unsigned w = m_wr;
	if (w - ldAcq (&m_rd) >= RING) return false;
	m_ring[w % RING] = c;
	stRel (&m_wr, w + 1);
	return true;
}
bool Engine::post (int type, long long a, long long b, int i, int j, void *p, int k, int l)
{
	Command c; c.type = type; c.a = a; c.b = b; c.i = i; c.j = j; c.k = k; c.l = l; c.p = p;
	return post (c);
}

CompiledSong *Engine::retired ()
{
	for (int i = 0; i < RETIRE; i++)
	{
		CompiledSong *s = __atomic_exchange_n (&m_retire[i], (CompiledSong *) 0, __ATOMIC_ACQ_REL);
		if (s) return s;
	}
	return 0;
}

void Engine::retire (CompiledSong *s)
{
	if (!s) return;
	for (;;)
		for (int i = 0; i < RETIRE; i++)
		{
			CompiledSong *expected = 0;
			if (__atomic_compare_exchange_n (&m_retire[i], &expected, s, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
		}
	// (the UI empties the slots at every tick: they are never all full for long)
}

void Engine::setInsert (int track, int slot, Effect *e)
{
	if (track < 0 || track >= ENGINE_MAX_TRACKS || slot < 0 || slot >= 4) return;
	__atomic_store_n (&m_fx[track][slot], e, __ATOMIC_RELEASE);
}

// ---- the song -------------------------------------------------------------------------------------------------
void Engine::allNotesOff ()
{
	for (int i = 0; i < m_nSynth; i++) if (m_synth[i]) m_synth[i]->noteOffAll (false);
	for (int i = 0; i < ENGINE_MAX_TRACKS; i++) if (m_ext[i]) m_ext[i]->allOff ();
}

static int firstEventAt (const CompiledSong *s, const CTrack &t, long long pos)
{
	int lo = 0, hi = t.events.size ();
	while (lo < hi) { int mid = (lo + hi) / 2; if (s->sliceSample[t.events[mid].slice] < pos) lo = mid + 1; else hi = mid; }
	return lo;
}

void Engine::applyPrograms ()
{
	if (!m_song) return;
	for (int i = 0; i < m_song->tracks.size () && i < m_nSynth; i++)
	{
		ms::Synthesizer *y = m_synth[i];
		if (!y) continue;
		const CTrack &t = m_song->tracks[i];
		y->noteOffAll (true);
		y->resetAllControllers ();
		if (t.drum) y->processMidiMessage (9, 0xC0, t.program, 0);
		else { y->processMidiMessage (0, 0xB0, 0, 0); y->processMidiMessage (0, 0xC0, t.program, 0); }
	}
}

void Engine::takeSong (CompiledSong *s)
{
	retire (m_song);
	m_song = s;
	allNotesOff ();
	applyPrograms ();
	if (m_song)
		for (int i = 0; i < m_song->tracks.size () && i < ENGINE_MAX_TRACKS; i++) m_nextEvent[i] = firstEventAt (m_song, m_song->tracks[i], m_pos);
}

void Engine::drain ()
{
	unsigned w = ldAcq (&m_wr);
	while (m_rd != w)
	{
		const Command &c = m_ring[m_rd % RING];
		switch (c.type)
		{
		case CMD_SONG: takeSong ((CompiledSong *) c.p); break;
		case CMD_PLAY:
			allNotesOff ();
			m_pos = c.a < 0 ? 0 : c.a;
			if (m_song) for (int i = 0; i < m_song->tracks.size () && i < ENGINE_MAX_TRACKS; i++) m_nextEvent[i] = firstEventAt (m_song, m_song->tracks[i], m_pos);
			m_playing = true; m_lastBeatClick = -1;
			break;
		case CMD_STOP: m_playing = false; allNotesOff (); break;
		case CMD_SEEK:
			allNotesOff ();
			m_pos = c.a < 0 ? 0 : c.a;
			if (m_song) for (int i = 0; i < m_song->tracks.size () && i < ENGINE_MAX_TRACKS; i++) m_nextEvent[i] = firstEventAt (m_song, m_song->tracks[i], m_pos);
			m_lastBeatClick = -1;
			break;
		case CMD_LOOP: m_loopA = c.a; m_loopB = c.b; m_loop = c.i != 0 && c.b > c.a; break;
		case CMD_PREVIEW:
			retire (m_prevSong);
			m_prevSong = (CompiledSong *) c.p; m_prevEvent = 0; m_prevPos = 0;
			if (m_preview)
			{
				m_preview->noteOffAll (true);
				if (m_prevSong && m_prevSong->tracks.size ())
				{
					const CTrack &t = m_prevSong->tracks[0];
					if (t.drum) m_preview->processMidiMessage (9, 0xC0, t.program, 0);
					else { m_preview->processMidiMessage (0, 0xB0, 0, 0); m_preview->processMidiMessage (0, 0xC0, t.program, 0); }
				}
			}
			break;
		case CMD_NOTE_ON:
			if (m_preview)
			{
				int ch = c.l ? 9 : 0;
				m_preview->processMidiMessage (ch, 0xC0, c.k, 0);
				m_preview->noteOn (ch, c.i, c.j);
			}
			break;
		case CMD_NOTE_OFF: if (m_preview) { m_preview->noteOff (0, c.i); m_preview->noteOff (9, c.i); } break;
		case CMD_ALL_OFF: allNotesOff (); if (m_preview) m_preview->noteOffAll (false); break;
		case CMD_METRONOME: m_metronome = c.i != 0; break;
		case CMD_EXTERNAL: if (c.i >= 0 && c.i < ENGINE_MAX_TRACKS) { if (m_ext[c.i]) m_ext[c.i]->allOff (); m_ext[c.i] = (ExternalSource *) c.p; } break;
		}
		stRel (&m_rd, m_rd + 1);
	}
}

void Engine::applyAutomation (double beat)
{
	bool anySolo = false;
	for (int i = 0; i < m_song->tracks.size () && i < ENGINE_MAX_TRACKS; i++) if (mix[m_song->tracks[i].srcIndex >= 0 ? m_song->tracks[i].srcIndex : i].solo) anySolo = true;
	for (int i = 0; i < m_song->tracks.size () && i < m_nSynth; i++)
	{
		const CTrack &t = m_song->tracks[i];
		ms::Synthesizer *y = m_synth[i];
		if (!y) continue;
		int si = t.srcIndex >= 0 && t.srcIndex < ENGINE_MAX_TRACKS ? t.srcIndex : i;
		const MixStrip &ms_ = mix[si];
		double m = (ms_.mute || (anySolo && !ms_.solo)) ? 0.0 : 1.0;
		double gv = trackGain (t.volume, ms_.volume, beat) * m;
		int ch = t.channel;
		y->processMidiMessage (ch, 0xB0, 7, iround (dmax (0, dmin (1, gv)) * 127));
		double pan = t.lane[AP_PAN].size () ? sampleCurve (t.lane[AP_PAN], ms_.pan, beat) : ms_.pan;
		pan = dmax (-1, dmin (1, pan));
		y->processMidiMessage (ch, 0xB0, 10, iround ((pan + 1.0) * 0.5 * 127));
		int rv = t.lane[AP_REVERB].size () ? iround (dmax (0, dmin (1, sampleCurve (t.lane[AP_REVERB], ms_.reverb / 127.0, beat))) * 127) : ms_.reverb;
		y->processMidiMessage (ch, 0xB0, 91, iclamp (rv, 0, 127));
		if (t.lane[AP_CHORUS].size ()) y->processMidiMessage (ch, 0xB0, 93, iround (dmax (0, dmin (1, sampleCurve (t.lane[AP_CHORUS], 0, beat))) * 127));
		if (t.lane[AP_EXPRESSION].size ()) y->processMidiMessage (ch, 0xB0, 11, iround (dmax (0, dmin (1, sampleCurve (t.lane[AP_EXPRESSION], 1, beat))) * 127));
		if (t.lane[AP_MODULATION].size ()) y->processMidiMessage (ch, 0xB0, 1, iround (dmax (0, dmin (1, sampleCurve (t.lane[AP_MODULATION], 0, beat))) * 127));
		if (t.lane[AP_SUSTAIN].size ()) y->processMidiMessage (ch, 0xB0, 64, sampleCurve (t.lane[AP_SUSTAIN], 0, beat) >= 0.5 ? 127 : 0);
		if (t.lane[AP_PITCHBEND].size ())
		{
			int v = iround ((dmax (-1, dmin (1, sampleCurve (t.lane[AP_PITCHBEND], 0, beat))) + 1) * 8192);
			v = iclamp (v, 0, 16383);
			y->processMidiMessage (ch, 0xE0, v & 127, v >> 7);
		}
	}
}

void Engine::dispatchUpTo (long long toSample)
{
	for (int i = 0; i < m_song->tracks.size () && i < ENGINE_MAX_TRACKS; i++)
	{
		const CTrack &t = m_song->tracks[i];
		ms::Synthesizer *y = i < m_nSynth ? m_synth[i] : 0;
		ExternalSource *x = m_ext[i];
		int &k = m_nextEvent[i];
		while (k < t.events.size () && m_song->sliceSample[t.events[k].slice] <= toSample)
		{
			const CEvent &e = t.events[k++];
			if (t.silent) continue;
			if (x) { if (e.kind == EV_ON) x->noteOn (e.note, e.vel); else x->noteOff (e.note); continue; }
			if (!y) continue;
			if (e.kind == EV_ON)
			{
				if (e.glideSec > 0) y->noteOnGlide (t.channel, e.note, e.vel, e.glideFrom, e.glideSec);
				else y->noteOn (t.channel, e.note, e.vel);
			}
			else y->noteOff (t.channel, e.note);
		}
	}
}

// ---- rendering -------------------------------------------------------------------------------------------------
void Engine::previewBlock (float *L, float *R, int n)
{
	if (!m_preview) return;
	if (m_prevSong && m_prevSong->tracks.size ())
	{
		const CTrack &t = m_prevSong->tracks[0];
		long long total = m_prevSong->totalSamples ();
		int done = 0;
		while (done < n && total > 0)
		{
			while (m_prevEvent < t.events.size () && m_prevSong->sliceSample[t.events[m_prevEvent].slice] <= m_prevPos)
			{
				const CEvent &e = t.events[m_prevEvent++];
				if (e.kind == EV_ON) m_preview->noteOn (t.channel, e.note, e.vel); else m_preview->noteOff (t.channel, e.note);
			}
			long long next = m_prevEvent < t.events.size () ? m_prevSong->sliceSample[t.events[m_prevEvent].slice] : total;
			int seg = (int) (next - m_prevPos); if (seg > n - done) seg = n - done; if (seg < 1) seg = 1;
			m_preview->render (m_bufL, m_bufR, seg);
			for (int i = 0; i < seg; i++) { L[done + i] += m_bufL[i]; R[done + i] += m_bufR[i]; }
			done += seg; m_prevPos += seg;
			if (m_prevPos >= total) { m_preview->noteOffAll (false); m_prevPos = 0; m_prevEvent = 0; }	// loop
		}
		if (done < n) { m_preview->render (m_bufL, m_bufR, n - done); for (int i = done; i < n; i++) { L[i] += m_bufL[i - done]; R[i] += m_bufR[i - done]; } }
		return;
	}
	m_preview->render (m_bufL, m_bufR, n);
	for (int i = 0; i < n; i++) { L[i] += m_bufL[i]; R[i] += m_bufR[i]; }
}

void Engine::renderBlock (float *L, float *R, int n)
{
	for (int i = 0; i < n; i++) L[i] = R[i] = 0;
	int voices = 0;
	int nt = m_song ? m_song->tracks.size () : 0;
	if (nt > ENGINE_MAX_TRACKS) nt = ENGINE_MAX_TRACKS;
	bool anySolo = false;
	for (int i = 0; i < nt; i++) { int si = m_song->tracks[i].srcIndex; if (si >= 0 && si < ENGINE_MAX_TRACKS && mix[si].solo) anySolo = true; }
	for (int i = 0; i < nt; i++)
	{
		const CTrack &t = m_song->tracks[i];
		int si = t.srcIndex >= 0 && t.srcIndex < ENGINE_MAX_TRACKS ? t.srcIndex : i;
		float pl = 0, pr = 0;
		if (t.silent) { peakL[si] = peakR[si] = 0; continue; }
		if (m_ext[i])
		{
			m_ext[i]->render (m_bufL, m_bufR, n);
			// an external source has no CC7 / CC10: the strip is applied here, "analog"
			const MixStrip &s = mix[si];
			float g = (s.mute || (anySolo && !s.solo)) ? 0 : s.volume;
			float pan = s.pan < -1 ? -1 : s.pan > 1 ? 1 : s.pan;
			float gl = g * (pan > 0 ? 1 - pan : 1), gr = g * (pan < 0 ? 1 + pan : 1);
			for (int k = 0; k < n; k++) { m_bufL[k] *= gl; m_bufR[k] *= gr; }
		}
		else if (i < m_nSynth && m_synth[i])
		{
			m_synth[i]->render (m_bufL, m_bufR, n);
			voices += m_synth[i]->activeVoiceCount ();
		}
		else continue;
		for (int s = 0; s < 4; s++) { Effect *e = __atomic_load_n (&m_fx[i][s], __ATOMIC_ACQUIRE); if (e) e->process (m_bufL, m_bufR, n); }
		for (int k = 0; k < n; k++)
		{
			L[k] += m_bufL[k]; R[k] += m_bufR[k];
			float a = fabsf (m_bufL[k]), b = fabsf (m_bufR[k]);
			if (a > pl) pl = a;
			if (b > pr) pr = b;
		}
		peakL[si] = pl; peakR[si] = pr;
	}
	activeVoices = voices;
}

void Engine::render (float *left, float *right, int frames)
{
	drain ();
	int done = 0;
	while (done < frames)
	{
		int n = frames - done; if (n > ENGINE_BLOCK) n = ENGINE_BLOCK;
		float *L = left + done, *R = right + done;
		if (m_song && m_playing)
		{
			if (m_loop && m_pos >= m_loopB)
			{
				allNotesOff ();
				m_pos = m_loopA;
				for (int i = 0; i < m_song->tracks.size () && i < ENGINE_MAX_TRACKS; i++) m_nextEvent[i] = firstEventAt (m_song, m_song->tracks[i], m_pos);
				m_lastBeatClick = -1;
			}
			// cut the block at the next event (or the loop end)
			long long next = m_pos + n;
			if (m_loop && m_loopB > m_pos && m_loopB < next) next = m_loopB;
			for (int i = 0; i < m_song->tracks.size () && i < ENGINE_MAX_TRACKS; i++)
			{
				const CTrack &t = m_song->tracks[i];
				int k = m_nextEvent[i];
				if (k < t.events.size ()) { long long s = m_song->sliceSample[t.events[k].slice]; if (s > m_pos && s < next) next = s; }
			}
			n = (int) (next - m_pos); if (n < 1) n = 1;
			dispatchUpTo (m_pos);
			applyAutomation (m_song->beatAtSample (m_pos));
			if (m_metronome)
			{
				int beat = (int) floor (m_song->beatAtSample (m_pos) + 1e-9);
				if (beat != m_lastBeatClick && m_preview)
				{
					long long bs = m_song->sampleAtBeat (beat);
					if (bs >= m_pos - n)
					{
						m_lastBeatClick = beat;
						m_preview->processMidiMessage (9, 0xC0, 0, 0);
						m_preview->noteOn (9, 76 + (beat % 4 == 0 ? 0 : 1), beat % 4 == 0 ? 110 : 85);
					}
				}
			}
			renderBlock (L, R, n);
			m_pos += n;
			if (m_pos > m_song->totalSamples () + 3 * m_rate && !m_loop) { m_playing = false; allNotesOff (); }
		}
		else
		{
			// stopped: the tails of the notes still ring out
			if (m_song) renderBlock (L, R, n);
			else for (int i = 0; i < n; i++) L[i] = R[i] = 0;
		}
		previewBlock (L, R, n);
		float mg = masterGain * 0.85f, pl = 0, pr = 0;
		for (int i = 0; i < n; i++)
		{
			L[i] *= mg; R[i] *= mg;
			float a = fabsf (L[i]), b = fabsf (R[i]);
			if (a > pl) pl = a;
			if (b > pr) pr = b;
		}
		masterPeakL = pl; masterPeakR = pr;
		done += n;
	}
	position = m_pos;
	playing = m_playing ? 1 : 0;
}

} // namespace kt
