//
// plug/plugshm.h -- the engine's side of a plugin process (kplug_proto.h): kt::ExternalSource for an
// instrument, kt::Effect for an effect, over the plugin's shared region. Pure memory: no kernel call,
// no allocation, no lock -- they run inside the engine (app core 2). The plugin host (plughost.h)
// makes them; the PC tests (tools/tests/koton/plug_test.cpp) drive them with a thread standing in for
// the plugin process.
//
//   ShmSource   the engine stamps the notes `lead` frames ahead (engine.h) -> the event ring, `want`
//               (and `kick`, the word the plugin's render thread sleeps on); render (at) reads the
//               output ring's frames [at, at + n) -- not rendered yet: silence and `underruns`.
//   ShmEffect   processAt (at): the dry block into the input ring (want = at + n), the processed
//               frames [at - latency, ...) back from the output ring. Before its first block went in:
//               silence (not an underrun).
//
#ifndef _koton_plugshm_h
#define _koton_plugshm_h

#include "../engine/engine.h"
#include "kplug_proto.h"

namespace kt {

class ShmSource : public ExternalSource
{
public:
	ShmSource (KpShm *s, int lead) : m_s (s), m_lead (lead), m_want ((long long) s->want), m_quiet (0), m_under (0) {}
	int lead () const override { return m_lead; }
	void noteOn (long long at, int note, int vel) override { push (KPE_NOTE_ON, at, note, vel); }
	void noteOff (long long at, int note) override { push (KPE_NOTE_OFF, at, note, 0); }
	void allOff (long long at) override { push (KPE_ALL_OFF, at, 0, 0); }
	void reset (long long at) override { push (KPE_RESET, at, 0, 0); m_quiet = at; }
	void flushTo (long long upTo) override
	{
		if (upTo <= m_want) return;			// (never backward)
		m_want = upTo;
		kp_st64 (&m_s->want, (unsigned long long) upTo);
		kp_st32 (&m_s->kick, (unsigned) upTo);
	}
	bool render (float *l, float *r, int n, long long at) override
	{
		kp_st64 (&m_s->readPos, (unsigned long long) at);
		long long done = (long long) kp_ld64 (&m_s->done);
		if (done >= at + n && done - KP_AU_RING <= at) { kp_au_read (m_s->outL, m_s->outR, (unsigned long long) at, l, r, n); return true; }
		int have = done > at && done - KP_AU_RING <= at ? (int) (done - at) : 0;
		if (have) kp_au_read (m_s->outL, m_s->outR, (unsigned long long) at, l, r, have);
		for (int k = have; k < n; k++) l[k] = r[k] = 0;
		if (at < m_quiet) return true;			// (the frames before a reset: muted by the engine anyway)
		m_under++; m_s->underruns = m_s->underruns + 1;
		return false;
	}
	unsigned underruns () const { return m_under; }
	KpShm *shm () const { return m_s; }
private:
	void push (int type, long long at, int a, int b)
	{
		KpEvent e;
		e.at = (unsigned long long) (at < 0 ? 0 : at);
		e.type = (unsigned char) type; e.a = (unsigned char) a; e.b = (unsigned char) b; e.c = 0; e.v = 0;
		kp_ev_push (m_s, &e);
	}
	KpShm *m_s;
	int m_lead;
	long long m_want, m_quiet;
	unsigned m_under;
};

class ShmEffect : public Effect
{
public:
	ShmEffect (KpShm *s, int latency) : m_s (s), m_lat (latency < 1 ? 1 : latency), m_first (-1), m_under (0) {}
	int latency () const override { return m_lat; }
	void process (float *l, float *r, int n) override { (void) l; (void) r; (void) n; }	// (the engine calls processAt)
	bool processAt (float *l, float *r, int n, long long at) override
	{
		if (m_first < 0 || at < m_first) m_first = at;
		kp_au_write (m_s->inL, m_s->inR, (unsigned long long) at, l, r, n);
		long long rp = at - m_lat;
		kp_st64 (&m_s->readPos, (unsigned long long) (rp > m_first ? rp : m_first));
		kp_st64 (&m_s->want, (unsigned long long) (at + n));
		kp_st32 (&m_s->kick, (unsigned) (at + n));
		// the output of the frames [rp, rp + n): before the first frame that went in, silence
		int k0 = 0;
		if (rp < m_first) { k0 = (int) (m_first - rp); if (k0 > n) k0 = n; for (int k = 0; k < k0; k++) l[k] = r[k] = 0; }
		if (k0 == n) return true;
		long long from = rp + k0, done = (long long) kp_ld64 (&m_s->done);
		int want = n - k0;
		int have = done >= from + want ? want : done > from ? (int) (done - from) : 0;
		if (done - KP_AU_RING > from) have = 0;		// (garbage: it ran past us)
		if (have) kp_au_read (m_s->outL, m_s->outR, (unsigned long long) from, l + k0, r + k0, have);
		for (int k = k0 + have; k < n; k++) l[k] = r[k] = 0;
		if (have == want) return true;
		m_under++; m_s->underruns = m_s->underruns + 1;
		return false;
	}
	unsigned underruns () const { return m_under; }
	KpShm *shm () const { return m_s; }
private:
	KpShm *m_s;
	int m_lat;
	long long m_first;
	unsigned m_under;
};

} // namespace kt

#endif
