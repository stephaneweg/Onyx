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
	renders (0), streamClock (0), pluginUnderruns (0), extLead (0), pdcFrames (0),
	m_sf (0), m_rate (44100), m_nSynth (0), m_preview (0), m_bufL (0), m_bufR (0), m_mixL (0), m_mixR (0), m_wr (0), m_rd (0),
	m_song (0), m_pos (0), m_playing (false), m_loop (false), m_metronome (false), m_loopA (0), m_loopB (0), m_lastBeatClick (-1),
	m_prevSong (0), m_prevEvent (0), m_prevPos (0),
	m_clock (0), m_lead (0), m_extCount (0), m_aheadPos (0), m_preroll (0), m_pdcD (0), m_pdcClock (-1), m_heardN (0), m_playFrom (0)
{
	for (int i = 0; i < ENGINE_MAX_TRACKS; i++)
	{
		m_synth[i] = 0; m_ext[i] = 0; m_nextEvent[i] = 0; peakL[i] = peakR[i] = 0;
		for (int k = 0; k < 4; k++) m_fx[i][k] = 0;
		m_aheadEvent[i] = 0; m_extMuteUntil[i] = 0; m_extGain[i] = 1;
	}
	for (int i = 0; i < RETIRE; i++) m_retire[i] = 0;
	for (int b = 0; b < PDC_BUCKETS; b++) { m_pdcL[b] = m_pdcR[b] = m_busL[b] = m_busR[b] = 0; m_pdcLat[b] = b ? -1 : 0; }
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
	for (int b = 0; b < PDC_BUCKETS; b++) { delete [] m_pdcL[b]; delete [] m_pdcR[b]; delete [] m_busL[b]; delete [] m_busR[b]; }
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
	if (e && e->latency () > 0 && !m_pdcL[0])
	{
		// the delay compensation's buffers, once (the engine reads m_pdcL[0] last: published after the others)
		for (int b = PDC_BUCKETS - 1; b >= 0; b--)
		{
			float *l = new float[PDC_MAX], *r = new float[PDC_MAX];
			memset (l, 0, sizeof (float) * PDC_MAX); memset (r, 0, sizeof (float) * PDC_MAX);
			m_busL[b] = new float[ENGINE_BLOCK]; m_busR[b] = new float[ENGINE_BLOCK];
			m_pdcR[b] = r;
			__atomic_store_n (&m_pdcL[b], l, __ATOMIC_RELEASE);
		}
	}
	__atomic_store_n (&m_fx[track][slot], e, __ATOMIC_RELEASE);
}

// ---- the song -------------------------------------------------------------------------------------------------
static int firstEventAt (const CompiledSong *s, const CTrack &t, long long pos)
{
	int lo = 0, hi = t.events.size ();
	while (lo < hi) { int mid = (lo + hi) / 2; if (s->sliceSample[t.events[mid].slice] < pos) lo = mid + 1; else hi = mid; }
	return lo;
}

// (the SoundFont's synths: the external sources are told on the stream clock -- externalsOff)
void Engine::allNotesOff ()
{
	for (int i = 0; i < m_nSynth; i++) if (m_synth[i]) m_synth[i]->noteOffAll (false);
}

// ---- the external sources: rendered ahead ------------------------------------------------------------------------
// Every external source gets its events lead frames ahead of the frame played: m_clock + m_lead is
// the "ahead point", which m_aheadPos (a song position) maps. A reset there cuts its voices; its
// audio before that is stale (rendered before the change): muted, with a short fade.
void Engine::externalsOff (bool hard)
{
	long long at = m_clock + m_lead;
	for (int i = 0; i < ENGINE_MAX_TRACKS; i++)
	{
		ExternalSource *x = m_ext[i];
		if (!x) continue;
		if (hard) { x->reset (at); m_extMuteUntil[i] = at; }
		else x->allOff (at);
	}
}

void Engine::resyncExternals ()
{
	if (!m_extCount) { m_preroll = 0; return; }
	externalsOff (true);
	m_aheadPos = m_pos;
	if (m_song) for (int i = 0; i < m_song->tracks.size () && i < ENGINE_MAX_TRACKS; i++) m_aheadEvent[i] = firstEventAt (m_song, m_song->tracks[i], m_aheadPos);
	m_preroll = m_playing ? m_lead : 0;
}

// a source attached / detached / replaced on `track` (old: the one before)
void Engine::externalsChanged (int track, ExternalSource *old)
{
	if (old) old->reset (m_clock + old->lead ());
	int lead = 0, count = 0;
	for (int i = 0; i < ENGINE_MAX_TRACKS; i++)
		if (m_ext[i]) { count++; int l = m_ext[i]->lead (); if (l > lead) lead = l; }
	bool leadMoved = lead != m_lead;
	m_lead = lead; m_extCount = count;
	extLead = lead;
	ExternalSource *x = m_ext[track];
	if (x)
	{
		x->reset (m_clock + m_lead);
		m_extMuteUntil[track] = m_clock + m_lead;
		m_extGain[track] = 0;
		if (m_song && track < m_song->tracks.size ()) m_aheadEvent[track] = firstEventAt (m_song, m_song->tracks[track], m_aheadPos);
	}
	if (leadMoved && m_playing) resyncExternals ();	// the ahead cursor moves: a new pre-roll
	else if (count == 1 && x && m_playing) resyncExternals ();	// the first one: its cursor starts
}

// The external tracks' events for the stream frames [m_clock + m_lead, + n): their song positions
// follow m_aheadPos, which wraps at the loop's end exactly as the played cursor will.
void Engine::dispatchAhead (int n)
{
	if (!m_song) return;
	long long s = m_clock + m_lead;
	int done = 0, guard = 0;
	while (done < n && guard++ < n + 8)
	{
		if (m_loop && m_aheadPos >= m_loopB)
		{
			for (int i = 0; i < ENGINE_MAX_TRACKS; i++) if (m_ext[i]) m_ext[i]->allOff (s + done);
			m_aheadPos = m_loopA;
			for (int i = 0; i < m_song->tracks.size () && i < ENGINE_MAX_TRACKS; i++) m_aheadEvent[i] = firstEventAt (m_song, m_song->tracks[i], m_aheadPos);
		}
		long long end = m_aheadPos + (n - done);
		if (m_loop && m_loopB > m_aheadPos && m_loopB < end) end = m_loopB;
		for (int i = 0; i < m_song->tracks.size () && i < ENGINE_MAX_TRACKS; i++)
		{
			ExternalSource *x = m_ext[i];
			if (!x) continue;
			const CTrack &t = m_song->tracks[i];
			int &k = m_aheadEvent[i];
			while (k < t.events.size () && m_song->sliceSample[t.events[k].slice] < end)
			{
				const CEvent &e = t.events[k++];
				if (t.silent) continue;
				long long at = s + done + (m_song->sliceSample[e.slice] - m_aheadPos);
				if (at < s + done) at = s + done;
				if (e.kind == EV_ON) x->noteOn (at, e.note, e.vel); else x->noteOff (at, e.note);
			}
		}
		done += (int) (end - m_aheadPos);
		m_aheadPos = end;
	}
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
	externalsOff (false);			// (the notes they hold may end differently in the new song)
	applyPrograms ();
	if (m_song)
		for (int i = 0; i < m_song->tracks.size () && i < ENGINE_MAX_TRACKS; i++)
		{
			m_nextEvent[i] = firstEventAt (m_song, m_song->tracks[i], m_pos);
			m_aheadEvent[i] = firstEventAt (m_song, m_song->tracks[i], m_aheadPos);
		}
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
			m_heardN = 0; m_playFrom = m_pos;
			resyncExternals ();
			break;
		case CMD_STOP: m_playing = false; allNotesOff (); externalsOff (true); m_preroll = 0; break;
		case CMD_SEEK:
			allNotesOff ();
			m_pos = c.a < 0 ? 0 : c.a;
			if (m_song) for (int i = 0; i < m_song->tracks.size () && i < ENGINE_MAX_TRACKS; i++) m_nextEvent[i] = firstEventAt (m_song, m_song->tracks[i], m_pos);
			m_lastBeatClick = -1;
			m_heardN = 0; m_playFrom = m_pos;
			resyncExternals ();
			break;
		case CMD_LOOP:
		{
			bool was = m_loop; long long a = m_loopA, b = m_loopB;
			m_loopA = c.a; m_loopB = c.b; m_loop = c.i != 0 && c.b > c.a;
			// the ahead cursor may have gone past the new end already: start it again from here
			if (m_playing && m_extCount && (m_loop != was || (m_loop && (a != m_loopA || b != m_loopB)))) resyncExternals ();
		} break;
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
		case CMD_ALL_OFF: allNotesOff (); externalsOff (false); if (m_preview) m_preview->noteOffAll (false); break;
		case CMD_METRONOME: m_metronome = c.i != 0; break;
		case CMD_EXTERNAL:
			if (c.i >= 0 && c.i < ENGINE_MAX_TRACKS)
			{
				ExternalSource *old = m_ext[c.i];
				m_ext[c.i] = (ExternalSource *) c.p;
				if (old != m_ext[c.i]) externalsChanged (c.i, old);
			}
			break;
		case CMD_EXT_NOTE_ON:
			if (c.i >= 0 && c.i < ENGINE_MAX_TRACKS && m_ext[c.i]) m_ext[c.i]->noteOn (m_clock + m_lead, c.j & 127, c.k < 1 ? 1 : c.k > 127 ? 127 : c.k);
			break;
		case CMD_EXT_NOTE_OFF:
			if (c.i >= 0 && c.i < ENGINE_MAX_TRACKS && m_ext[c.i]) m_ext[c.i]->noteOff (m_clock + m_lead, c.j & 127);
			break;
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
		bool ext = m_ext[i] != 0;				// (its events go ahead: dispatchAhead)
		int &k = m_nextEvent[i];
		while (k < t.events.size () && m_song->sliceSample[t.events[k].slice] <= toSample)
		{
			const CEvent &e = t.events[k++];
			if (t.silent || ext) continue;
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

// the IPC effects' latency on a track (its inserts' sum)
int Engine::trackLatency (int i)
{
	int lat = 0;
	for (int s = 0; s < 4; s++)
	{
		Effect *e = __atomic_load_n (&m_fx[i][s], __ATOMIC_ACQUIRE);
		if (e) { int l = e->latency (); if (l > 0) lat += l; }
	}
	return lat;
}

// the delay compensation's bucket of the tracks with that latency (bucket 0: none)
int Engine::pdcBucket (int lat)
{
	if (lat <= 0) return 0;
	for (int b = 1; b < PDC_BUCKETS; b++) if (m_pdcLat[b] == lat) return b;
	for (int b = 1; b < PDC_BUCKETS; b++) if (m_pdcLat[b] < 0) { m_pdcLat[b] = lat; return b; }
	int best = 0, bestD = 1 << 30;			// more latencies than buckets: the nearest one
	for (int b = 0; b < PDC_BUCKETS; b++) { int d = iabs (m_pdcLat[b] - lat); if (d < bestD) { bestD = d; best = b; } }
	return best;
}

// The song position heard now: with a delay compensation the mix comes out m_pdcD frames after the
// played cursor passed -- the cursor's position then, from the history of the blocks played.
long long Engine::heardPos ()
{
	if (!m_pdcD || !m_playing) return m_pos;
	long long c = m_clock - m_pdcD;
	for (int k = 0; k < m_heardN && k < HEARD; k++)
	{
		const Heard &h = m_heard[(m_heardN - 1 - k) % HEARD];
		if (h.clock <= c) return h.pos + (c - h.clock);
	}
	return m_playFrom;
}

void Engine::renderBlock (float *L, float *R, int n)
{
	for (int i = 0; i < n; i++) L[i] = R[i] = 0;
	int voices = 0;
	int nt = m_song ? m_song->tracks.size () : 0;
	if (nt > ENGINE_MAX_TRACKS) nt = ENGINE_MAX_TRACKS;
	bool anySolo = false;
	for (int i = 0; i < nt; i++) { int si = m_song->tracks[i].srcIndex; if (si >= 0 && si < ENGINE_MAX_TRACKS && mix[si].solo) anySolo = true; }
	// the plugin delay compensation: the tracks through IPC effects come out late by their latency;
	// the others are delayed by D - theirs (D: the biggest), a delay ring per latency
	int D = 0;
	if (__atomic_load_n (&m_pdcL[0], __ATOMIC_ACQUIRE))
	{
		for (int i = 0; i < nt; i++) if (!m_song->tracks[i].silent) { int l = trackLatency (i); if (l > D) D = l; }
		if (D > PDC_MAX - ENGINE_BLOCK) D = PDC_MAX - ENGINE_BLOCK;
		if (D != m_pdcD || (D && m_pdcClock != m_clock))
		{
			for (int b = 0; b < PDC_BUCKETS; b++)	// a new compensation (or a gap): the rings silent again
			{
				memset (m_pdcL[b], 0, sizeof (float) * PDC_MAX); memset (m_pdcR[b], 0, sizeof (float) * PDC_MAX);
				m_pdcLat[b] = b ? -1 : 0;
			}
			if (D && !m_pdcD) m_heardN = 0;
			m_pdcD = D; pdcFrames = D;
		}
		if (D) for (int b = 0; b < PDC_BUCKETS; b++) for (int k = 0; k < n; k++) m_busL[b][k] = m_busR[b][k] = 0;
	}
	for (int i = 0; i < nt; i++)
	{
		const CTrack &t = m_song->tracks[i];
		int si = t.srcIndex >= 0 && t.srcIndex < ENGINE_MAX_TRACKS ? t.srcIndex : i;
		float pl = 0, pr = 0;
		if (t.silent) { peakL[si] = peakR[si] = 0; continue; }
		if (m_ext[i])
		{
			if (!m_ext[i]->render (m_bufL, m_bufR, n, m_clock) && m_clock >= m_extMuteUntil[i]) pluginUnderruns = pluginUnderruns + 1;
			// its audio before a reset's frame is stale (rendered before a play, a seek, a stop):
			// faded out over 128 frames, then silent up to that frame
			long long mu = m_extMuteUntil[i];
			float eg = m_extGain[i];
			if (m_clock < mu || eg < 1)
			{
				for (int k = 0; k < n; k++)
				{
					float gk;
					if (m_clock + k >= mu) gk = 1;
					else { eg -= 1.0f / 128; if (eg < 0) eg = 0; gk = eg; }
					m_bufL[k] *= gk; m_bufR[k] *= gk;
				}
				m_extGain[i] = m_clock + n >= mu ? 1 : eg;
			}
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
		int lat = 0;
		for (int s = 0; s < 4; s++)
		{
			Effect *e = __atomic_load_n (&m_fx[i][s], __ATOMIC_ACQUIRE);
			if (!e) continue;
			int l = e->latency ();
			if (l > 0) { if (!e->processAt (m_bufL, m_bufR, n, m_clock)) pluginUnderruns = pluginUnderruns + 1; lat += l; }
			else e->process (m_bufL, m_bufR, n);
		}
		float *dl = L, *dr = R;
		if (D) { int b = pdcBucket (lat); dl = m_busL[b]; dr = m_busR[b]; }
		for (int k = 0; k < n; k++)
		{
			dl[k] += m_bufL[k]; dr[k] += m_bufR[k];
			float a = fabsf (m_bufL[k]), b = fabsf (m_bufR[k]);
			if (a > pl) pl = a;
			if (b > pr) pr = b;
		}
		peakL[si] = pl; peakR[si] = pr;
	}
	if (D)
	{
		for (int b = 0; b < PDC_BUCKETS; b++)
		{
			if (m_pdcLat[b] < 0) continue;
			int delay = D - m_pdcLat[b]; if (delay < 0) delay = 0;
			float *rl = m_pdcL[b], *rr = m_pdcR[b];
			for (int k = 0; k < n; k++)
			{
				unsigned w = (unsigned) ((m_clock + k) & (PDC_MAX - 1)), r = (unsigned) ((m_clock + k - delay) & (PDC_MAX - 1));
				rl[w] = m_busL[b][k]; rr[w] = m_busR[b][k];
				L[k] += rl[r]; R[k] += rr[r];
			}
		}
		m_pdcClock = m_clock + n;
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
		if (m_song && m_playing && m_preroll > 0)
		{
			// the pre-roll of a play / seek with external sources: they get the song's first frames
			// ahead (dispatchAhead); the played cursor waits for them
			if (n > m_preroll) n = (int) m_preroll;
			dispatchAhead (n);
			for (int i = 0; i < ENGINE_MAX_TRACKS; i++) if (m_ext[i]) m_ext[i]->flushTo (m_clock + m_lead + n);
			renderBlock (L, R, n);
			m_preroll -= n;
		}
		else if (m_song && m_playing)
		{
			if (m_loop && m_pos >= m_loopB)
			{
				allNotesOff ();
				m_pos = m_loopA;
				for (int i = 0; i < m_song->tracks.size () && i < ENGINE_MAX_TRACKS; i++) m_nextEvent[i] = firstEventAt (m_song, m_song->tracks[i], m_pos);
				m_lastBeatClick = -1;
			}
			// cut the block at the next event (or the loop end); an external track's events go ahead
			long long next = m_pos + n;
			if (m_loop && m_loopB > m_pos && m_loopB < next) next = m_loopB;
			for (int i = 0; i < m_song->tracks.size () && i < ENGINE_MAX_TRACKS; i++)
			{
				const CTrack &t = m_song->tracks[i];
				int k = m_nextEvent[i];
				if (m_ext[i]) continue;
				if (k < t.events.size ()) { long long s = m_song->sliceSample[t.events[k].slice]; if (s > m_pos && s < next) next = s; }
			}
			n = (int) (next - m_pos); if (n < 1) n = 1;
			dispatchUpTo (m_pos);
			applyAutomation (m_song->beatAtSample (m_pos));
			if (m_metronome)
			{
				long long mp = m_pdcD ? heardPos () : m_pos;	// (the click with the mix heard)
				int beat = (int) floor (m_song->beatAtSample (mp) + 1e-9);
				if (beat != m_lastBeatClick && m_preview)
				{
					long long bs = m_song->sampleAtBeat (beat);
					if (bs >= mp - n)
					{
						m_lastBeatClick = beat;
						m_preview->processMidiMessage (9, 0xC0, 0, 0);
						m_preview->noteOn (9, 76 + (beat % 4 == 0 ? 0 : 1), beat % 4 == 0 ? 110 : 85);
					}
				}
			}
			if (m_extCount) dispatchAhead (n);
			for (int i = 0; i < ENGINE_MAX_TRACKS; i++) if (m_ext[i]) m_ext[i]->flushTo (m_clock + m_lead + n);
			Heard &h = m_heard[m_heardN % HEARD]; h.clock = m_clock; h.pos = m_pos; m_heardN++;
			if (m_heardN >= 2 * HEARD) m_heardN -= HEARD;
			renderBlock (L, R, n);
			m_pos += n;
			if (m_pos > m_song->totalSamples () + 3 * m_rate && !m_loop) { m_playing = false; allNotesOff (); externalsOff (false); }
		}
		else
		{
			// stopped: the tails of the notes still ring out (the external sources' too: a live note)
			for (int i = 0; i < ENGINE_MAX_TRACKS; i++) if (m_ext[i]) m_ext[i]->flushTo (m_clock + m_lead + n);
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
		m_clock += n;
		done += n;
	}
	position = heardPos ();
	playing = m_playing ? 1 : 0;
	streamClock = m_clock;
	__atomic_store_n (&renders, renders + 1, __ATOMIC_RELEASE);
}

} // namespace kt
