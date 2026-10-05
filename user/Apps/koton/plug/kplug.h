//
// kplug.h -- the plugin side of Koton's plugins (the protocol: kplug_proto.h). A plugin -- an
// instrument, an effect or a generator -- is a small program: its parameters, a few callbacks, and
//
//     static const KpParamDef P[] = { { "cutoff", "Cutoff", 20, 18000, 2000, "Hz" }, ... };
//     static void render (float *l, float *r, int n) { ... kp_param (0) ... }
//     static const KpDesc desc = { .name = "My synth", .kind = KP_INSTRUMENT, .params = P,
//                                  .nparams = 3, .noteOn = on, .noteOff = off, .render = render };
//     KPLUG_MAIN (desc)
//
// and this runtime does the rest: the shared region, the handshake, the render thread (real-time
// priority, asleep on the region's `kick` word between two passes: it renders ahead, sample-
// accurately, the events the engine sent), the parameters (from the host, from the editor), the
// state (JSON: {"v":1, "params": {id: value...}} + what saveState adds), a generator's requests,
// and an EDITOR drawn into the host's surface as a Control Panel applet (applet_proto.h): its own
// (`editor`), or one made from the parameters (knobs, drop-downs, check boxes).
//
// Threads: prepare / setParam / noteOn / noteOff / allOff / cc / render / process run on the RENDER
// thread (prepare and the first setParam before it starts) -- they must not allocate, lock or wait;
// kp_param (i) reads a parameter there. generate / saveState / loadState / editor run on the main
// thread (they may allocate). A plugin never calls the kernel from its DSP.
//
// Build: an Onyx newlib app with the FPU (user/Makefile: Apps/kp_<name>/main.cpp ->
// Apps/kp_<name>/kp_<name>.elf, staged as SD:/koton/plugins/<name>/main + plugin.json). For the PC
// tests (tools/tests/koton/plug_run.sh) define KPLUG_DSP_ONLY and KPLUG_TEST_SYM=<a name>: no
// kernel, no uikit; KPLUG_MAIN then exports a KpTestApi under that name.
//
#ifndef _kplug_h
#define _kplug_h

#include "kplug_proto.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "json.hpp"

namespace uikit { class Widget; }

// ---- the description ----------------------------------------------------------------------------------
struct KpParamDef
{
	const char *id, *name;			// the id is saved in the states: never change it
	float min, max, def;
	const char *unit;			// "Hz", "ms", "dB", "%"... (0: none)
	float step;				// 0: continuous; 1: whole numbers
	const char *const *choices;		// an enumeration: its names, 0-terminated (min 0, max n - 1)
};

struct KpNote { double start, len; int note, vel; };	// a generator's note: beats from the block's start, MIDI

class KpNotes
{
public:
	KpNotes () : m_v (0), m_n (0), m_cap (0) {}
	~KpNotes () { free (m_v); }
	bool add (double start, double len, int note, int vel)
	{
		if (note < 0 || note > 127 || len <= 0) return false;
		if (m_n == m_cap)
		{
			int nc = m_cap ? m_cap * 2 : 256;
			KpNote *v = (KpNote *) realloc (m_v, sizeof (KpNote) * (size_t) nc);
			if (!v) return false;
			m_v = v; m_cap = nc;
		}
		KpNote &x = m_v[m_n++]; x.start = start; x.len = len; x.note = note; x.vel = vel < 1 ? 1 : vel > 127 ? 127 : vel;
		return true;
	}
	int size () const { return m_n; }
	const KpNote &operator[] (int i) const { return m_v[i]; }
	void clear () { m_n = 0; }
private:
	KpNotes (const KpNotes &);
	KpNotes &operator= (const KpNotes &);
	KpNote *m_v; int m_n, m_cap;
};

// A chord of the chord track under a generator's block (the host's kt::segments).
struct KpChord
{
	double start, len;			// beats from the block's start
	int root;				// its pitch class 0..11
	int quality;				// Koton's quality (0..34: Major, Minor, Diminished... 7#5)
	int iv[8], niv;				// its intervals from the root (the whole chord: 9ths, 11ths...)
	int basic;				// Koton's plugin contract (KotonChordQuality 0..13: Major, Minor,
	int biv[4], nbiv;			//   Diminished, Augmented, Sus2, Sus4, Power, Dom7, Maj7, Min7,
						//   MinMaj7, Dim7, HalfDim7, Aug7) and its intervals
	int bass;				// the pitch class at the bass (an inversion), = root otherwise
};

// What a generator is asked: the notes of [from, to) beats of its block, knowing the song.
struct KpContext
{
	double from, to, length;		// beats from the block's start (the whole block: 0, its length)
	double blockStart;			// the block's absolute position (beats)
	double bpm, pickup;
	int tonic;				// the key: its tonic's pitch class
	int mode;				// 0 major, 1 minor (the key's scale below says more)
	int scale[12], nscale;			// the key's scale: pitch classes from the tonic (7 for a mode)
	int meterNum, meterDen;
	bool ternary;				// a compound meter (6/8, 9/8, 12/8)
	const KpChord *chords; int nchords;	// by time
	unsigned seed;				// the module's own (stable across renders)
	const json::Value *state;		// the module's whole state (a generator's extras)
	// the chord at a beat of the block, 0: none (a hole in the chord track)
	const KpChord *chordAt (double beat) const
	{
		for (int i = 0; i < nchords; i++) if (beat >= chords[i].start - 1e-9 && beat < chords[i].start + chords[i].len - 1e-9) return &chords[i];
		return 0;
	}
};

struct KpDesc				// (designated initialisers -- .name = ..., in this order -- as GCC takes them in
				//  C++17; what is left out is 0)
{
	const char *name = 0;
	int kind = 0;				// KP_INSTRUMENT / KP_EFFECT / KP_GENERATOR
	const KpParamDef *params = 0; int nparams = 0;
	int editorW = 0, editorH = 0;		// the editor's size it prefers (0: from its parameters)
	void (*prepare) (int rate) = 0;
	void (*noteOn) (int note, int vel) = 0;
	void (*noteOff) (int note) = 0;
	void (*allOff) (bool hard) = 0;		// hard: cut every voice at once (a seek); else release them
	void (*cc) (int controller, int value) = 0;
	void (*render) (float *l, float *r, int n) = 0;		// an instrument: WRITE n frames
	void (*process) (float *l, float *r, int n) = 0;	// an effect: in place
	bool (*generate) (const KpContext &ctx, const float *params, KpNotes &out) = 0;	// a generator
	void (*setParam) (int index, float value) = 0;	// a parameter changed (the render thread, between blocks)
	void (*saveState) (json::Writer &w) = 0;	// its own keys, beyond "params" (optional)
	void (*loadState) (const json::Value &state) = 0;
	void (*editor) (uikit::Widget &root, int w, int h) = 0;	// its own editor (0: made from the parameters)
};

// ---- small DSP helpers (float, no allocation) --------------------------------------------------------------
static const float KP_PI = 3.14159265358979f;
static inline float kp_mtof (float note) { return 440.0f * powf (2.0f, (note - 69.0f) / 12.0f); }
static inline float kp_db (float db) { return powf (10.0f, db / 20.0f); }
static inline float kp_clampf (float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

// sin (2 pi x), x in turns, from a table (linear interpolation)
static const int KP_SIN_N = 2048;
static float kp_sinTab[KP_SIN_N + 1];
static inline void kp_dsp_init ()
{
	for (int i = 0; i <= KP_SIN_N; i++) kp_sinTab[i] = sinf (2.0f * KP_PI * (float) i / KP_SIN_N);
}
static inline float kp_sin (float x)
{
	x -= floorf (x);
	float f = x * KP_SIN_N; int i = (int) f; if (i >= KP_SIN_N) i = KP_SIN_N - 1;
	float t = f - (float) i;
	return kp_sinTab[i] + (kp_sinTab[i + 1] - kp_sinTab[i]) * t;
}

// a fast noise (xorshift32) in [-1, 1)
struct KpNoise { unsigned s; KpNoise () : s (0x9E3779B9u) {} float next () { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int) s * (1.0f / 2147483648.0f); } };

// ADSR: a linear attack, exponential decay and release (times in seconds, sustain 0..1)
struct KpAdsr
{
	enum { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };
	int stage; float v, aStep, dMul, s, rMul;
	KpAdsr () : stage (IDLE), v (0), aStep (1), dMul (0.999f), s (1), rMul (0.999f) {}
	void set (float a, float d, float sus, float r, int rate)
	{
		aStep = 1.0f / (a * rate + 1.0f);
		dMul = expf (-6.9f / (d * rate + 1.0f));		// -60 dB in the time given
		rMul = expf (-6.9f / (r * rate + 1.0f));
		s = kp_clampf (sus, 0, 1);
	}
	void on () { stage = ATTACK; }
	void off () { if (stage != IDLE) stage = RELEASE; }
	void kill () { stage = IDLE; v = 0; }
	bool active () const { return stage != IDLE; }
	float next ()
	{
		switch (stage)
		{
		case ATTACK: v += aStep; if (v >= 1) { v = 1; stage = DECAY; } break;
		case DECAY: v = s + (v - s) * dMul; if (v - s < 1e-4f) { v = s; stage = SUSTAIN; } break;
		case SUSTAIN: v = s; if (s <= 0) stage = IDLE; break;
		case RELEASE: v *= rMul; if (v < 1e-4f) { v = 0; stage = IDLE; } break;
		default: v = 0; break;
		}
		return v;
	}
};

// a state-variable filter (the trapezoidal "TPT" one: stable when modulated)
struct KpSvf
{
	float g, k, a1, a2, a3, ic1, ic2;
	KpSvf () : g (0), k (1), a1 (0), a2 (0), a3 (0), ic1 (0), ic2 (0) {}
	void set (float hz, float q, int rate)
	{
		hz = kp_clampf (hz, 10, rate * 0.45f);
		g = tanf (KP_PI * hz / rate); k = 1.0f / (q < 0.5f ? 0.5f : q);
		a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
	}
	void reset () { ic1 = ic2 = 0; }
	// mode 0 low-pass, 1 band-pass, 2 high-pass
	float tick (float v0, int mode)
	{
		float v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
		return mode == 0 ? v2 : mode == 1 ? v1 : v0 - k * v1 - v2;
	}
};

// a biquad (the RBJ cookbook's)
struct KpBiquad
{
	float b0, b1, b2, a1, a2, z1, z2;
	KpBiquad () : b0 (1), b1 (0), b2 (0), a1 (0), a2 (0), z1 (0), z2 (0) {}
	enum { LOWPASS, HIGHPASS, PEAK, LOWSHELF, HIGHSHELF };
	void set (int type, float hz, float q, float gainDb, int rate)
	{
		hz = kp_clampf (hz, 10, rate * 0.45f);
		float A = powf (10.0f, gainDb / 40.0f), w = 2 * KP_PI * hz / rate, c = cosf (w), s = sinf (w), al = s / (2 * (q < 0.1f ? 0.1f : q));
		float B0, B1, B2, A0, A1, A2;
		switch (type)
		{
		case LOWPASS: B0 = (1 - c) / 2; B1 = 1 - c; B2 = B0; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
		case HIGHPASS: B0 = (1 + c) / 2; B1 = -(1 + c); B2 = B0; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
		case PEAK: B0 = 1 + al * A; B1 = -2 * c; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * c; A2 = 1 - al / A; break;
		case LOWSHELF:
		{
			float sa = 2 * sqrtf (A) * al;
			B0 = A * ((A + 1) - (A - 1) * c + sa); B1 = 2 * A * ((A - 1) - (A + 1) * c); B2 = A * ((A + 1) - (A - 1) * c - sa);
			A0 = (A + 1) + (A - 1) * c + sa; A1 = -2 * ((A - 1) + (A + 1) * c); A2 = (A + 1) + (A - 1) * c - sa;
		} break;
		default:
		{
			float sa = 2 * sqrtf (A) * al;
			B0 = A * ((A + 1) + (A - 1) * c + sa); B1 = -2 * A * ((A - 1) + (A + 1) * c); B2 = A * ((A + 1) + (A - 1) * c - sa);
			A0 = (A + 1) - (A - 1) * c + sa; A1 = 2 * ((A - 1) - (A + 1) * c); A2 = (A + 1) - (A - 1) * c - sa;
		} break;
		}
		b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
	}
	void reset () { z1 = z2 = 0; }
	float tick (float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

// Koton's basic chord qualities (KotonChordQuality) and their intervals
static const int KP_BASIC_IV[14][4] = {
	{ 0, 4, 7, -1 }, { 0, 3, 7, -1 }, { 0, 3, 6, -1 }, { 0, 4, 8, -1 }, { 0, 2, 7, -1 }, { 0, 5, 7, -1 }, { 0, 7, -1, -1 },
	{ 0, 4, 7, 10 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 3, 7, 11 }, { 0, 3, 6, 9 }, { 0, 3, 6, 10 }, { 0, 4, 8, 10 } };

// ---- the runtime's core (no kernel call: the PC tests run it too) ---------------------------------------
struct KpRuntime
{
	KpShm *shm;
	const KpDesc *desc;
	int rate;
	float cur[KP_MAX_PARAMS];		// the render thread's values (kp_param)
	float want[KP_MAX_PARAMS];		// the main thread's (the host, the editor): applied between blocks
	volatile unsigned long long dirty;	// want[i] changed
	unsigned long long done;		// the render thread's position
	bool started, threaded;
	volatile int quit;
};
static KpRuntime g_kp;

static inline float kp_param (int i) { return i >= 0 && i < KP_MAX_PARAMS ? g_kp.cur[i] : 0; }
static inline int kp_rate () { return g_kp.rate; }
static inline int kp_nparams () { return g_kp.desc ? (g_kp.desc->nparams < KP_MAX_PARAMS ? g_kp.desc->nparams : KP_MAX_PARAMS) : 0; }
static inline int kp_param_index (const char *id)
{
	for (int i = 0; i < kp_nparams (); i++) if (!strcmp (g_kp.desc->params[i].id, id)) return i;
	return -1;
}

// hooks the kernel side fills (the editor follows a change; the host is told)
static void (*kp__onParam) (int i, bool fromHost) = 0;
static void (*kp__onState) () = 0;
static void (*kp__wake) (volatile unsigned *w) = 0;

static inline float kp__clampParam (int i, float v)
{
	const KpParamDef &p = g_kp.desc->params[i];
	if (!(v == v)) v = p.def;
	if (v < p.min) v = p.min;
	if (v > p.max) v = p.max;
	if (p.step > 0) v = p.min + p.step * floorf ((v - p.min) / p.step + 0.5f);
	return v;
}

// a parameter from the main thread (the host, the editor, a state): for the render thread's next block
static inline void kp__set (int i, float v, bool fromHost)
{
	if (i < 0 || i >= kp_nparams ()) return;
	v = kp__clampParam (i, v);
	g_kp.want[i] = v;
	if (g_kp.shm) g_kp.shm->param[i] = v;
	if (g_kp.threaded) __atomic_or_fetch (&g_kp.dirty, 1ull << i, __ATOMIC_RELEASE);
	else { g_kp.cur[i] = v; if (g_kp.started && g_kp.desc->setParam) g_kp.desc->setParam (i, v); }
	if (kp__onParam) kp__onParam (i, fromHost);
}
// a plugin's own editor moves a parameter (the host is told)
static inline void kp_set_param (int i, float v) { kp__set (i, v, false); }

// the render thread: the parameters changed since the last block
static inline void kp__apply ()
{
	unsigned long long m = __atomic_exchange_n (&g_kp.dirty, 0ull, __ATOMIC_ACQUIRE);
	for (int i = 0; m && i < KP_MAX_PARAMS; i++, m >>= 1)
		if (m & 1) { g_kp.cur[i] = g_kp.want[i]; if (g_kp.desc->setParam) g_kp.desc->setParam (i, g_kp.cur[i]); }
}

static inline void kp__event (const KpEvent &e)
{
	const KpDesc *d = g_kp.desc;
	switch (e.type)
	{
	case KPE_NOTE_ON: if (d->noteOn) { if (e.b) d->noteOn (e.a, e.b); else if (d->noteOff) d->noteOff (e.a); } break;
	case KPE_NOTE_OFF: if (d->noteOff) d->noteOff (e.a); break;
	case KPE_ALL_OFF: if (d->allOff) d->allOff (false); break;
	case KPE_RESET: if (d->allOff) d->allOff (true); break;
	case KPE_CC: if (d->cc) d->cc (e.a, e.b); break;
	case KPE_PARAM:
		if (e.a < kp_nparams ()) { float v = kp__clampParam (e.a, e.v); g_kp.cur[e.a] = v; if (d->setParam) d->setParam (e.a, v); }
		break;
	default: break;
	}
}

static inline void kp__clean (float *l, float *r, int n)
{
	for (int k = 0; k < n; k++)
	{
		if (!(l[k] == l[k]) || l[k] > 64 || l[k] < -64) l[k] = 0;	// (a NaN, a blow-up: never into the mix)
		if (!(r[k] == r[k]) || r[k] > 64 || r[k] < -64) r[k] = 0;
	}
}

// One render pass: the frames [done, want) -> the output ring. Frames rendered.
static inline int kp__serve ()
{
	KpShm *s = g_kp.shm;
	const KpDesc *d = g_kp.desc;
	if (!s || d->kind == KP_GENERATOR) return 0;
	unsigned long long want = kp_ld64 (&s->want), rp = kp_ld64 (&s->readPos), pos = g_kp.done;
	if (pos < rp)					// too late for those: from where the engine reads
	{
		if (g_kp.started && pos) s->lateFrames = s->lateFrames + (unsigned) (rp - pos);
		pos = rp;
		kp_st64 (&s->done, pos);
	}
	g_kp.started = true;
	float bl[KP_BLOCK], br[KP_BLOCK];
	int frames = 0;
	while (pos < want)
	{
		kp__apply ();
		unsigned long long end = want - pos > KP_BLOCK ? pos + KP_BLOCK : want;
		if (d->kind == KP_INSTRUMENT)
		{
			const KpEvent *e;
			while ((e = kp_ev_peek (s)) && e->at <= pos) { kp__event (*e); kp_ev_pop (s); }
			if ((e = kp_ev_peek (s)) && e->at < end) end = e->at;	// (sample-accurate: cut there)
			int n = (int) (end - pos);
			if (d->render) d->render (bl, br, n); else for (int k = 0; k < n; k++) bl[k] = br[k] = 0;
			kp__clean (bl, br, n);
			kp_au_write (s->outL, s->outR, pos, bl, br, n);
		}
		else
		{
			int n = (int) (end - pos);
			kp_au_read (s->inL, s->inR, pos, bl, br, n);
			if (d->process) d->process (bl, br, n);
			kp__clean (bl, br, n);
			kp_au_write (s->outL, s->outR, pos, bl, br, n);
		}
		frames += (int) (end - pos);
		pos = end;
		kp_st64 (&s->done, pos);
	}
	g_kp.done = pos;
	return frames;
}

// ---- the state and the parameter list (JSON) ----
static inline void kp__writeParams (json::Writer &w)
{
	const KpDesc *d = g_kp.desc;
	w.beginObj ();
	w.key ("name"); w.str (d->name ? d->name : "");
	w.key ("kind"); w.str (d->kind == KP_INSTRUMENT ? "instrument" : d->kind == KP_EFFECT ? "effect" : "generator");
	w.key ("version"); w.num (KP_PROTO_VERSION);
	if (d->editorW > 0 && d->editorH > 0) { w.key ("editor"); w.beginObj (true); w.key ("w"); w.num (d->editorW); w.key ("h"); w.num (d->editorH); w.endObj (); }
	w.key ("params"); w.beginArr ();
	for (int i = 0; i < kp_nparams (); i++)
	{
		const KpParamDef &p = d->params[i];
		w.beginObj (true);
		w.key ("id"); w.str (p.id); w.key ("name"); w.str (p.name);
		w.key ("min"); w.num ((double) p.min); w.key ("max"); w.num ((double) p.max); w.key ("default"); w.num ((double) p.def);
		w.key ("unit"); w.str (p.unit ? p.unit : "");
		if (p.step > 0) { w.key ("step"); w.num ((double) p.step); }
		if (p.choices) { w.key ("choices"); w.beginArr (true); for (int c = 0; p.choices[c]; c++) w.str (p.choices[c]); w.endArr (); }
		w.endObj ();
	}
	w.endArr ();
	w.endObj ();
}

static inline void kp__writeState (json::Writer &w)
{
	w.beginObj ();
	w.key ("v"); w.num (1);
	w.key ("params"); w.beginObj ();
	for (int i = 0; i < kp_nparams (); i++) { w.key (g_kp.desc->params[i].id); w.num ((double) g_kp.want[i]); }
	w.endObj ();
	if (g_kp.desc->saveState) g_kp.desc->saveState (w);
	w.endObj ();
}

// the parameters of a state into vals[] (missing: what vals holds already). "params": {id: v}, or the
// ids at the top (Koton's flat states).
static inline void kp__readParams (const json::Value &st, float *vals)
{
	const json::Value &p = st["params"];
	for (int i = 0; i < kp_nparams (); i++)
	{
		const char *id = g_kp.desc->params[i].id;
		const json::Value *v = p.isObj () ? p.find (id) : 0;
		if (!v || !v->isNum ()) v = st.find (id);
		if (v && (v->isNum () || v->isBool ())) vals[i] = kp__clampParam (i, (float) v->asDouble (vals[i]));
	}
}

static inline bool kp__loadState (const char *text, unsigned len)
{
	json::Doc doc;
	if (!doc.parse (text, len, json::TOLERANT) || !doc.root ().isObj ()) return false;
	float v[KP_MAX_PARAMS];
	for (int i = 0; i < kp_nparams (); i++) v[i] = g_kp.want[i];
	kp__readParams (doc.root (), v);
	for (int i = 0; i < kp_nparams (); i++) if (v[i] != g_kp.want[i]) kp__set (i, v[i], true);
	if (g_kp.desc->loadState) g_kp.desc->loadState (doc.root ());
	if (kp__onState) kp__onState ();
	return true;
}

// a generator's request: the context (JSON) -> the notes' reply ({"notes": [[start, len, note, vel]...]})
static inline bool kp__generate (const char *text, unsigned len, json::Writer &out)
{
	const KpDesc *d = g_kp.desc;
	if (!d->generate) return false;
	json::Doc doc;
	if (!doc.parse (text, len, json::TOLERANT) || !doc.root ().isObj ()) return false;
	const json::Value &r = doc.root ();
	KpContext c;
	c.from = r["from"].asDouble (0); c.length = r["length"].asDouble (4); c.to = r["to"].asDouble (c.length);
	c.blockStart = r["blockStart"].asDouble (0); c.bpm = r["bpm"].asDouble (120); c.pickup = r["pickup"].asDouble (0);
	const json::Value &k = r["key"];
	c.tonic = ((k["tonic"].asInt (0) % 12) + 12) % 12; c.mode = k["mode"].asInt (0);
	c.nscale = 0;
	for (const json::Value *e = k["scale"].first (); e && c.nscale < 12; e = e->next) c.scale[c.nscale++] = ((e->asInt (0) % 12) + 12) % 12;
	if (!c.nscale) { static const int maj[7] = { 0, 2, 4, 5, 7, 9, 11 }, min[7] = { 0, 2, 3, 5, 7, 8, 10 }; for (int i = 0; i < 7; i++) c.scale[i] = c.mode ? min[i] : maj[i]; c.nscale = 7; }
	c.meterNum = r["meter"][0].asInt (4); c.meterDen = r["meter"][1].asInt (4);
	if (c.meterNum < 1) c.meterNum = 4;
	if (c.meterDen < 1) c.meterDen = 4;
	c.ternary = c.meterDen == 8 && c.meterNum % 3 == 0;
	c.seed = (unsigned) r["seed"].asU64 (1);
	c.state = &r["state"];
	int nch = (int) r["chords"].size ();
	KpChord *ch = nch ? (KpChord *) calloc ((size_t) nch, sizeof (KpChord)) : 0;
	c.nchords = 0;
	for (const json::Value *e = r["chords"].first (); e && ch; e = e->next)
	{
		KpChord &x = ch[c.nchords++];
		x.start = (*e)["start"].asDouble (0); x.len = (*e)["len"].asDouble (0);
		x.root = (((*e)["root"].asInt (0) % 12) + 12) % 12; x.quality = (*e)["quality"].asInt (0);
		x.niv = 0; for (const json::Value *v = (*e)["iv"].first (); v && x.niv < 8; v = v->next) x.iv[x.niv++] = v->asInt (0);
		if (!x.niv) { x.iv[0] = 0; x.iv[1] = 4; x.iv[2] = 7; x.niv = 3; }
		x.basic = (*e)["basic"].asInt (0); if (x.basic < 0 || x.basic > 13) x.basic = 0;
		x.nbiv = 0; for (int q = 0; q < 4 && KP_BASIC_IV[x.basic][q] >= 0; q++) x.biv[x.nbiv++] = KP_BASIC_IV[x.basic][q];
		x.bass = (((*e)["bass"].asInt (x.root) % 12) + 12) % 12;
	}
	c.chords = ch;
	// the module's parameters: the defaults, then its state's
	float vals[KP_MAX_PARAMS];
	for (int i = 0; i < kp_nparams (); i++) vals[i] = d->params[i].def;
	if (c.state->isObj ()) kp__readParams (*c.state, vals);
	KpNotes notes;
	bool ok = d->generate (c, vals, notes);
	free (ch);
	if (!ok) return false;
	out.beginObj (); out.key ("notes"); out.beginArr ();
	for (int i = 0; i < notes.size (); i++)
	{
		const KpNote &n = notes[i];
		if (n.start < c.from - 1e-9 || n.start >= c.to - 1e-9) continue;
		out.beginArr (true); out.num (n.start, 5); out.num (n.len, 5); out.num (n.note); out.num (n.vel); out.endArr ();
	}
	out.endArr (); out.endObj ();
	return true;
}

// the request in the channel (the host posted it: kp_req_post): handled, answered
static inline void kp__request ()
{
	KpShm *s = g_kp.shm;
	unsigned seq = kp_ld32 (&s->reqSeq), type = s->reqType, len = s->reqLen;
	if (len > KP_DATA_BYTES) len = KP_DATA_BYTES;
	unsigned status = KP_OK;
	json::Writer w (false);
	bool reply = false;
	switch (type)
	{
	case KP_STATE_GET: kp__writeState (w); reply = true; break;
	case KP_STATE_SET: if (!kp__loadState (s->data, len)) status = KP_ERR_BAD; break;
	case KP_PARAMS: kp__writeParams (w); reply = true; break;
	case KP_GENERATE:
		if (!g_kp.desc->generate) status = KP_ERR_UNSUPPORTED;
		else if (!kp__generate (s->data, len, w)) status = KP_ERR_BAD;
		else reply = true;
		break;
	default: status = KP_ERR_UNSUPPORTED; break;
	}
	unsigned out = 0;
	if (reply && status == KP_OK)
	{
		if (!w.ok () || w.size () > KP_DATA_BYTES) status = KP_ERR_TOOBIG;
		else { memcpy (s->data, w.data (), w.size ()); out = (unsigned) w.size (); }
	}
	s->repStatus = status; s->repLen = out;
	kp_st32 (&s->repSeq, seq);
	if (kp__wake) kp__wake (&s->repSeq);
}

// Taking the region: the defaults, the initial state (data[], initLen), prepare, every parameter set,
// the parameter list into data[]. The render thread may start after it.
static inline bool kp__attach (KpShm *s, const KpDesc *d)
{
	if (!s || s->magic != KP_MAGIC || s->version < 1 || s->size < sizeof (KpShm)) return false;
	g_kp.shm = s; g_kp.desc = d; g_kp.rate = s->rate ? (int) s->rate : 44100;
	g_kp.dirty = 0; g_kp.started = false; g_kp.threaded = false; g_kp.quit = 0;
	kp_dsp_init ();
	for (int i = 0; i < KP_MAX_PARAMS; i++) g_kp.cur[i] = g_kp.want[i] = 0;
	for (int i = 0; i < kp_nparams (); i++) { g_kp.cur[i] = g_kp.want[i] = kp__clampParam (i, d->params[i].def); s->param[i] = g_kp.cur[i]; }
	if (s->initLen && s->initLen <= KP_DATA_BYTES) kp__loadState (s->data, s->initLen);
	if (d->prepare) d->prepare (g_kp.rate);
	g_kp.started = true;
	for (int i = 0; i < kp_nparams (); i++) { g_kp.cur[i] = g_kp.want[i]; if (d->setParam) d->setParam (i, g_kp.cur[i]); }
	g_kp.started = false;
	json::Writer w (false);
	kp__writeParams (w);
	if (w.ok () && w.size () <= KP_DATA_BYTES) { memcpy (s->data, w.data (), w.size ()); s->paramsLen = (unsigned) w.size (); }
	s->nparams = (unsigned) kp_nparams ();
	s->pluginVersion = KP_PROTO_VERSION;
	kp_st32 (&s->evRd, kp_ld32 (&s->evWr));		// (what an earlier process of this region left)
	g_kp.done = kp_ld64 (&s->done);
	return true;
}

// the value of a parameter as text ("440 Hz", "Saw", "0.35")
static inline void kp_format (int i, float v, char *out, int cap)
{
	if (cap <= 0) return;
	out[0] = 0;
	if (i < 0 || i >= kp_nparams ()) return;
	const KpParamDef &p = g_kp.desc->params[i];
	if (p.choices)
	{
		int n = 0; while (p.choices[n]) n++;
		int c = (int) floorf (v - p.min + 0.5f);
		const char *s = n ? p.choices[c < 0 ? 0 : c >= n ? n - 1 : c] : "";
		int k = 0; while (s[k] && k + 1 < cap) { out[k] = s[k]; k++; } out[k] = 0;
		return;
	}
	char t[40]; int k = 0;
	float a = v < 0 ? -v : v;
	int dec = p.step >= 1 ? 0 : a >= 100 ? 0 : a >= 10 ? 1 : 2;
	long long scale = dec == 0 ? 1 : dec == 1 ? 10 : 100;
	long long x = (long long) floorf (a * (float) scale + 0.5f);
	if (v < 0 && x) t[k++] = '-';
	long long ip = x / scale, fp = x % scale;
	char d[24]; int nd = 0; do { d[nd++] = (char) ('0' + ip % 10); ip /= 10; } while (ip);
	while (nd) t[k++] = d[--nd];
	if (dec) { t[k++] = '.'; if (dec == 2) { t[k++] = (char) ('0' + fp / 10); t[k++] = (char) ('0' + fp % 10); } else t[k++] = (char) ('0' + fp); }
	if (p.unit && p.unit[0]) { t[k++] = ' '; for (int u = 0; p.unit[u] && k < 38; u++) t[k++] = p.unit[u]; }
	t[k] = 0;
	int j = 0; while (t[j] && j + 1 < cap) { out[j] = t[j]; j++; } out[j] = 0;
}

#ifdef KPLUG_DSP_ONLY
// ---- the PC tests: the runtime without the kernel ----------------------------------------------------------
struct KpTestApi
{
	const KpDesc *desc;
	bool (*attach) (KpShm *s);		// as a process starting on that region
	int (*serve) ();			// one render pass
	void (*request) ();			// the request posted in the region, answered
	void (*setParam) (int i, float v);	// from the host
	float (*param) (int i);
};
#define KPLUG_MAIN(d) \
	static bool kp__t_attach (KpShm *s) { return kp__attach (s, &(d)); } \
	static void kp__t_set (int i, float v) { kp__set (i, v, true); } \
	extern "C" const KpTestApi *KPLUG_TEST_SYM () \
	{ static const KpTestApi a = { &(d), kp__t_attach, kp__serve, kp__request, kp__t_set, kp_param }; return &a; }

#else
// ---- the process: the kernel side ------------------------------------------------------------------------
#include "appkit/appkit.h"
#include "systemkit/systemkit.h"
#include "uikit/uikit.h"

static int kp__host, kp__shmId, kp__renderTid = -1;
static unsigned long long kp__notify;		// parameters to report to the host (KP_PARAM_CHANGED)
static bool kp__dirtyNotify, kp__quitAll;

// ---- the editor: a Control Panel applet in the host's surface ----
static int kp__edSid = -1, kp__edW, kp__edH;
static unsigned *kp__edPx;
static uikit::Widget *kp__edRoot;
static bool kp__edPresent;
static int kp__bl, kp__br, kp__bm;
static void kp__edSync ();

// (the classes below read this plugin's g_kp: kept to this translation unit -- two plugins linked into
// one program, as the PC tests do, must not share their code)
namespace {

// A knob: a 270-degree dial (the parameter's name above, its value under it). Drag up / down (the
// range in 200 px, Shift: 1000), the wheel (a step), a double click: the default.
class KpKnob : public uikit::Widget
{
public:
	int index; float value;
	KpKnob (int l, int t, int w, int h, int i) : Widget (l, t, w, h), index (i), value (0), m_drag (false), m_y0 (0), m_v0 (0), m_last (0) { canFocus = true; }
	void onDraw () override
	{
		using namespace uikit;
		const KpParamDef &p = g_kp.desc->params[index];
		unsigned bg = parent ? parent->bgColor () : C_BG;
		canvas.clear (bg);
		char nm[40]; int k = 0;
		while (p.name[k] && k < 39) { nm[k] = p.name[k]; k++; }
		nm[k] = 0;
		while (k > 2 && uk_text_w (nm) > width - 2) { nm[--k] = 0; nm[k - 1] = '.'; }	// (a long name: elided)
		uk_text_c (canvas, 0, 0, width, uk_fh () + 2, nm, C_TEXT);
		int top = uk_fh () + 4, bottom = height - uk_fh () - 4;
		int d = bottom - top; if (d > width - 8) d = width - 8;
		if (d < 16) d = 16;
		int cx = width / 2, cy = top + d / 2, r = d / 2 - 3;
		float f = p.max > p.min ? (value - p.min) / (p.max - p.min) : 0;
		f = kp_clampf (f, 0, 1);
		int a0 = 225 - (int) (f * 270.0f + 0.5f), a1 = 225;
		if (p.min < 0 && p.max > 0)			// (a bipolar one: the arc from its zero)
		{
			int az = 225 - (int) (-p.min / (p.max - p.min) * 270.0f + 0.5f);
			if (a0 < az) a1 = az; else { a1 = a0; a0 = az; }
		}
		bool dark = uk_bright (bg) < 110;			// (a dark face: the track and the cap lighter than it)
		unsigned cap = (C_BUTTON & 0xFFFFFF) != (bg & 0xFFFFFF) ? C_BUTTON : uk_tone (bg, dark ? 165 : 150);
		if (hover || m_drag) cap = uk_tone (cap, dark ? 150 : 170);
		VPath path;
		path.arc (V (cx), V (cy), V (r), -45, 225, V (3)); path.fill (canvas, uk_tone (bg, dark ? 185 : 96));
		if (a0 < a1) { path.clear (); path.arc (V (cx), V (cy), V (r), a0, a1, V (3)); path.fill (canvas, disabled ? C_DIS : C_ACCENT); }
		path.clear (); path.circle (V (cx), V (cy), V (r - 6)); path.fill (canvas, cap);
		path.clear (); path.circle (V (cx), V (cy), V (r - 6)); path.hole (V (cx), V (cy), V (r - 7)); path.fill (canvas, dark ? uk_tone (cap, 180) : C_BORDER, 140);
		int av = 225 - (int) (f * 270.0f + 0.5f);
		int px = cx + ((r - 9) * uk_cos (av)) / 16384, py = cy - ((r - 9) * uk_sin (av)) / 16384;
		path.clear (); path.line (V (cx), V (cy), V (px), V (py), V (2)); path.fill (canvas, uk_ink_on (cap));
		char t[48]; kp_format (index, value, t, sizeof t);
		uk_text_c (canvas, 0, height - uk_fh () - 2, width, uk_fh () + 2, t, hasFocus ? C_ACCENT : C_TEXT);
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		const KpParamDef &p = g_kp.desc->params[index];
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (in != hover) { hover = in; invalidate (true); }
		float range = p.max - p.min;
		if (wheel && in)
		{
			float st = p.step > 0 ? p.step : range / 100.0f;
			set (value + (wheel > 0 ? st : -st));
			return true;
		}
		if (bl && !m_drag && in)
		{
			unsigned now = kapi_get_ticks ();
			if (now - m_last < 35) { set (p.def); m_last = 0; return true; }	// a double click
			m_last = now;
			m_drag = true; catchOutside = true; m_y0 = my; m_v0 = value; setFocus ();
			return true;
		}
		if (m_drag && !bl) { m_drag = false; catchOutside = false; invalidate (true); return true; }
		if (m_drag)
		{
			float px = (kapi_get_modifiers () & MOD_SHIFT) ? 1000.0f : 200.0f;
			set (m_v0 + (float) (m_y0 - my) / px * range);
			return true;
		}
		return in;
	}
	bool onKey (long k) override
	{
		const KpParamDef &p = g_kp.desc->params[index];
		float st = p.step > 0 ? p.step : (p.max - p.min) / 100.0f;
		if (k == KEY_UP || k == KEY_RIGHT) set (value + st);
		else if (k == KEY_DOWN || k == KEY_LEFT) set (value - st);
		else return false;
		return true;
	}
	void set (float v) { v = kp__clampParam (index, v); if (v == value) return; value = v; invalidate (true); kp__set (index, v, false); }
private:
	bool m_drag; int m_y0; float m_v0; unsigned m_last;
};

} // namespace

// The controls bound to parameters (the editor follows a change from the host, a state). A plugin's
// own editor makes them with kp_knob / kp_choice / kp_toggle, and any other widget it likes.
enum { KP_CTL_KNOB = 1, KP_CTL_CHOICE, KP_CTL_TOGGLE };
static uikit::Widget *kp__ctl[KP_MAX_PARAMS];
static unsigned char kp__ctlType[KP_MAX_PARAMS];

static void kp__onDropdown (uikit::Widget &w) { uikit::Dropdown &d = (uikit::Dropdown &) w; kp__set (w.tag, g_kp.desc->params[w.tag].min + (float) d.sel, false); }
static void kp__onCheck (uikit::Widget &w) { uikit::Checkbox &c = (uikit::Checkbox &) w; kp__set (w.tag, c.checked ? 1.0f : 0.0f, false); }

// a parameter shown as a check box: its choices are Off / On
static inline bool kp__isToggle (const KpParamDef &p)
{
	return p.choices && p.choices[0] && p.choices[1] && !p.choices[2] && !strcmp (p.choices[0], "Off") && !strcmp (p.choices[1], "On");
}

// a knob for parameter i (w x h: its name, the dial, its value)
static inline uikit::Widget *kp_knob (uikit::Widget &parent, int x, int y, int w, int h, int i)
{
	if (i < 0 || i >= kp_nparams ()) return 0;
	KpKnob *k = new KpKnob (x, y, w, h, i);
	k->value = g_kp.want[i]; k->tag = i;
	parent.addChild (k);
	kp__ctl[i] = k; kp__ctlType[i] = KP_CTL_KNOB;
	return k;
}
// a drop-down for an enumerated parameter i (its name above it): w wide, 2 lines high
static inline uikit::Widget *kp_choice (uikit::Widget &parent, int x, int y, int w, int i)
{
	using namespace uikit;
	if (i < 0 || i >= kp_nparams () || !g_kp.desc->params[i].choices) return 0;
	const KpParamDef &p = g_kp.desc->params[i];
	int n = 0; while (p.choices[n]) n++;
	int fh = uk_fh ();
	Label *l = new Label (x, y, w, fh + 4, p.name, C_TEXT, parent.bgColor ());
	l->tag = -1;
	parent.addChild (l);
	Dropdown *dd = new Dropdown (x, y + fh + 6, w, fh + 10, p.choices, n, (int) (g_kp.want[i] - p.min + 0.5f), kp__onDropdown);
	dd->tag = i;
	parent.addChild (dd);
	kp__ctl[i] = dd; kp__ctlType[i] = KP_CTL_CHOICE;
	return dd;
}
// a check box for an Off / On parameter i
static inline uikit::Widget *kp_toggle (uikit::Widget &parent, int x, int y, int w, int i)
{
	using namespace uikit;
	if (i < 0 || i >= kp_nparams ()) return 0;
	Checkbox *c = new Checkbox (x, y, w, uk_fh () + 6, g_kp.desc->params[i].name, g_kp.want[i] >= 0.5f, kp__onCheck, parent.bgColor ());
	c->tag = i;
	parent.addChild (c);
	kp__ctl[i] = c; kp__ctlType[i] = KP_CTL_TOGGLE;
	return c;
}

// the editor made from the parameters: a title, then knobs, drop-downs and check boxes in rows
static inline void kp__autoEditor (uikit::Widget &root, int w, int h)
{
	using namespace uikit;
	(void) h;
	const KpDesc *d = g_kp.desc;
	int fh = uk_fh ();
	Label *title = new Label (10, 6, w - 20, fh + 6, d->name ? d->name : "", C_TEXT, C_BG);
	title->tag = -1;
	root.addChild (title);
	int x = 10, y = fh + 18, rowH = 0, KW = 96, KH = 2 * fh + 58;
	for (int i = 0; i < kp_nparams (); i++)
	{
		const KpParamDef &p = d->params[i];
		bool tog = kp__isToggle (p), cho = p.choices && !tog;
		int cw = cho ? 170 : tog ? 150 : KW, chh = cho ? 2 * fh + 20 : tog ? fh + 12 : KH;
		if (x + cw > w - 6 && x > 10) { x = 10; y += rowH + 8; rowH = 0; }
		if (cho) kp_choice (root, x, y, cw - 8, i);
		else if (tog) kp_toggle (root, x, y + 4, cw - 8, i);
		else kp_knob (root, x, y, cw - 4, chh, i);
		x += cw; if (chh > rowH) rowH = chh;
	}
}

static void kp__edSync ()
{
	if (!kp__edRoot) return;
	for (int i = 0; i < kp_nparams (); i++)
	{
		uikit::Widget *c = kp__ctl[i];
		if (!c) continue;
		const KpParamDef &p = g_kp.desc->params[i];
		switch (kp__ctlType[i])
		{
		case KP_CTL_TOGGLE: { uikit::Checkbox *b = (uikit::Checkbox *) c; bool on = g_kp.want[i] >= 0.5f; if (b->checked != on) { b->checked = on; b->invalidate (true); } } break;
		case KP_CTL_CHOICE: { uikit::Dropdown *dd = (uikit::Dropdown *) c; int s = (int) (g_kp.want[i] - p.min + 0.5f); if (!dd->open && s != dd->sel) { dd->sel = s; dd->invalidate (true); } } break;
		case KP_CTL_KNOB: { KpKnob *k = (KpKnob *) c; if (k->value != g_kp.want[i]) { k->value = g_kp.want[i]; k->invalidate (true); } } break;
		default: break;
		}
	}
}

namespace {
class KpEditorRoot : public uikit::Panel
{
public:
	KpEditorRoot (int w, int h) : Panel (0, 0, w, h, uikit::C_BG) { hasFocus = true; tag = -1; }
};
} // namespace

static void kp__edClose (bool tell)
{
	if (!kp__edRoot) return;
	for (int i = 0; i < KP_MAX_PARAMS; i++) { kp__ctl[i] = 0; kp__ctlType[i] = 0; }
	delete kp__edRoot; kp__edRoot = 0;		// (the surface stays mapped: kp__edSid, reused)
	if (tell) kapi_mailbox_send (kp__host, AP_EXIT, 0, 0);
}

static void kp__edOpen (const KpEditor &e)
{
	kp__edClose (false);
	if (e.surface != kp__edSid)
	{
		int w = 0, h = 0;
		unsigned *px = kapi_surface_map (e.surface);
		if (!px || !kapi_surface_size (e.surface, &w, &h)) return;
		kp__edSid = e.surface; kp__edPx = px; kp__edW = w; kp__edH = h;
	}
	int w = e.w > 0 && e.w <= kp__edW ? e.w : kp__edW, h = e.h > 0 && e.h <= kp__edH ? e.h : kp__edH;
	uikit::init ();
	if (e.themed)					// (the host's colours: the editor looks like a part of it)
	{
		uikit::UkTheme t; uikit::uk_theme_get (t);
		t.window = e.window; t.button = e.button; t.field = e.field; t.accent = e.accent;
		uikit::uk_theme_set (t);
	}
	KpEditorRoot *root = new KpEditorRoot (w, h);
	root->canvas.adopt (kp__edPx, w, h, kp__edW);
	kp__edRoot = root;
	for (int i = 0; i < KP_MAX_PARAMS; i++) { kp__ctl[i] = 0; kp__ctlType[i] = 0; }
	if (g_kp.desc->editor) g_kp.desc->editor (*root, w, h);
	else kp__autoEditor (*root, w, h);
	kp__bl = kp__br = kp__bm = 0;
	int m[2] = { w, h };
	kapi_mailbox_send (kp__host, AP_HELLO, m, sizeof m);
	root->invalidate (true);
}

static void kp__edPtr (const ApPtr &e)
{
	if (!kp__edRoot) return;
	int c = e.changed;
	switch (e.event)
	{
	case GUI_EVENT_PTR_DOWN: if (c & 1) kp__bl = 1; if (c & 2) kp__br = 1; if (c & 4) kp__bm = 1; break;
	case GUI_EVENT_PTR_UP: if (c & 1) kp__bl = 0; if (c & 2) kp__br = 0; if (c & 4) kp__bm = 0; break;
	case GUI_EVENT_PTR_LEAVE: kp__edRoot->handleMouse (-1, -1, 0, 0, 0, 0); return;
	case GUI_EVENT_PTR_WHEEL: kp__edRoot->handleMouse (e.x, e.y, kp__bl, kp__br, kp__bm, e.wheel); return;
	default: break;
	}
	kp__edRoot->handleMouse (e.x < 0 ? 0 : e.x, e.y < 0 ? 0 : e.y, kp__bl, kp__br, kp__bm, 0);
}

// the hooks of the core
static void kp__paramHook (int i, bool fromHost)
{
	if (!fromHost && i >= 0 && i < 64) kp__notify |= 1ull << i;	// its editor: the host is told
	if (fromHost) kp__edSync ();
}
static void kp__stateHook () { kp__edSync (); }
static void kp__wakeHook (volatile unsigned *w) { kapi_wake_word (w); }
// a plugin's own editor changed something that is not a parameter: the host saves the state again
static inline void kp_dirty () { kp__dirtyNotify = true; }

// ---- the render thread ----
static int kp__renderMain (void *)
{
	kapi_thread_priority (0, 1);			// "real time": first whenever it is ready
	KpShm *s = g_kp.shm;
	unsigned sec = kapi_get_ticks ();
	while (!g_kp.quit)
	{
		unsigned seen = kp_ld32 (&s->kick);
		unsigned t0 = kapi_clock_us ();
		int n = kp__serve ();
		if (n)
		{
			unsigned us = kapi_clock_us () - t0;
			s->dspUs = us;
			if (us > s->dspMaxUs) s->dspMaxUs = us;
		}
		s->dspBeat = s->dspBeat + 1;
		unsigned now = kapi_get_ticks ();
		if (now - sec >= 100) { sec = now; s->dspMaxUs = 0; }
		if (g_kp.quit) break;
		kapi_wait_word (&s->kick, seen, 10);	// (the tick wakes it when the engine moved `want`)
	}
	return 0;
}

static inline int kp__num (const char *&p)
{
	while (*p == ' ') p++;
	int v = 0; bool any = false;
	while (*p >= '0' && *p <= '9') { v = v * 10 + (*p++ - '0'); any = true; }
	return any ? v : -1;
}

// Run the plugin: "--kplug <shm id> <host pid>" (argv, else the process's arguments) -> the exit code.
static inline int kplug_main (int argc, char **argv, const KpDesc *d)
{
	char args[128]; args[0] = 0;
	if (argc > 2 && argv) { int n = 0; for (int a = 1; a < argc && n < 120; a++) { for (int k = 0; argv[a][k] && n < 120; k++) args[n++] = argv[a][k]; args[n++] = ' '; } args[n] = 0; }
	else kapi_get_args (args, sizeof args);
	const char *key = "--kplug ", *p = args;
	for (int k = 0; key[k]; k++, p++) if (*p != key[k]) { kapi_stdout_write ("kplug: a Koton plugin, started by Koton\n", 40); return 2; }
	kp__shmId = kp__num (p); kp__host = kp__num (p);
	KpShm *s = kp__shmId > 0 ? (KpShm *) kapi_surface_map (kp__shmId) : 0;
	int sw = 0, sh = 0;
	if (!s || !kapi_surface_size (kp__shmId, &sw, &sh) || (unsigned) (sw * sh * 4) < sizeof (KpShm)) return 3;
	if (s->magic != KP_MAGIC || (int) s->kind != d->kind) { s->ready = KP_FAILED; kapi_wake_word (&s->ready); return 4; }
	kp__onParam = kp__paramHook; kp__onState = kp__stateHook; kp__wake = kp__wakeHook;
	if (!kp__attach (s, d)) { s->ready = KP_FAILED; kapi_wake_word (&s->ready); return 4; }
	if (d->kind != KP_GENERATOR)
	{
		g_kp.threaded = true;
		kp__renderTid = kapi_thread_create (kp__renderMain, 0, 64 * 1024, "render");
		if (kp__renderTid < 0) { g_kp.threaded = false; s->ready = KP_FAILED; kapi_wake_word (&s->ready); return 5; }
	}
	kp_st32 (&s->ready, KP_READY);
	kapi_wake_word (&s->ready);
	KpHello hi = { KP_PROTO_VERSION, (unsigned) d->kind, (unsigned) kp_nparams (), kp__shmId };
	kapi_mailbox_send (kp__host, KP_HELLO, &hi, sizeof hi);
	unsigned lastReq = kp_ld32 (&s->reqSeq), alive = kapi_get_ticks ();
	s->repSeq = lastReq;
	while (!kp__quitAll)
	{
		s->beat = s->beat + 1;
		int from = 0, type = 0, n;
		unsigned char buf[64];
		while ((n = kapi_mailbox_recv (&from, &type, buf, sizeof buf, 0)) >= 0)
		{
			if (from != kp__host) continue;
			switch (type)
			{
			case KP_SET_PARAM: if (n >= (int) sizeof (KpParamMsg)) { KpParamMsg m; memcpy (&m, buf, sizeof m); kp__set (m.index, m.value, true); } break;
			case KP_EDITOR: if (n >= 12) { KpEditor e; memset (&e, 0, sizeof e); memcpy (&e, buf, n < (int) sizeof e ? n : sizeof e); kp__edOpen (e); } break;
			case AP_PTR: if (n >= (int) sizeof (ApPtr)) { ApPtr e; memcpy (&e, buf, sizeof e); kp__edPtr (e); } break;
			case AP_KEY: if (n >= (int) sizeof (ApKey) && kp__edRoot) { ApKey k; memcpy (&k, buf, sizeof k); kp__edRoot->handleKey (k.key); } break;
			case AP_CLOSE: kp__edClose (true); break;
			case KP_BYE: kp__quitAll = true; break;
			default: break;		// (KP_STATE_GET...: doorbells -- the request is seen below)
			}
		}
		if (kp_ld32 (&s->reqSeq) != lastReq) { lastReq = kp_ld32 (&s->reqSeq); kp__request (); }
		// the host: told of what the editor changed (again next time if its mailbox is full)
		for (int i = 0; kp__notify && i < 64; i++)
			if (kp__notify & (1ull << i))
			{
				KpParamMsg m = { i, g_kp.want[i] };
				if (kapi_mailbox_send (kp__host, KP_PARAM_CHANGED, &m, sizeof m) > 0) kp__notify &= ~(1ull << i);
				else break;
			}
		if (kp__dirtyNotify && kapi_mailbox_send (kp__host, KP_DIRTY, 0, 0) > 0) kp__dirtyNotify = false;
		if (kp__edRoot && (!kp__edRoot->valid || kp__edPresent))
		{
			kp__edRoot->draw ();
			int r[4] = { 0, 0, 0, 0 };
			kp__edPresent = kapi_mailbox_send (kp__host, AP_PRESENT, r, sizeof r) <= 0;
		}
		// the host gone (killed, crashed): end
		unsigned now = kapi_get_ticks ();
		if (now - alive >= 50) { alive = now; if (kapi_ipc_lookup (KP_SERVICE) != kp__host) break; }
		kapi_wait_word (&s->reqSeq, lastReq, 16);
	}
	kp_st32 (&s->ready, KP_ENDING);
	g_kp.quit = 1;
	if (kp__renderTid >= 0) kapi_thread_join (kp__renderTid, 1000, 0);
	kp__edClose (true);
	kapi_mailbox_send (kp__host, KP_BYE, 0, 0);
	return 0;
}

#define KPLUG_MAIN(d) int main (void) { return kplug_main (0, 0, &(d)); }
#endif

#endif
