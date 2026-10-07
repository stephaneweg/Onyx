//
// kp_subsynth -- a Koton instrument plugin (user/Apps/koton/plug/kplug.h): a subtractive synthesiser. Two oscillators
// (saw, square with its pulse width, triangle, sine; anti-aliased by polyBLEP) and noise, mixed into
// a state-variable filter (low-, band- or high-pass) whose cutoff follows the key and its own ADSR;
// an ADSR on the amplitude. 8 voices.
//
#include "../koton/plug/kplug.h"

static const char *const WAVES[] = { "Saw", "Square", "Triangle", "Sine", 0 };
static const char *const MODES[] = { "Low-pass", "Band-pass", "High-pass", 0 };
enum { P_W1, P_W2, P_SEMI, P_FINE, P_MIX, P_NOISE, P_PW, P_MODE, P_CUT, P_RES, P_ENV, P_KEY,
       P_A, P_D, P_S, P_R, P_FA, P_FD, P_FS, P_FR, P_VOL, NP };
static const KpParamDef P[NP] = {
	{ "osc1", "Osc 1", 0, 3, 0, "", 1, WAVES },
	{ "osc2", "Osc 2", 0, 3, 1, "", 1, WAVES },
	{ "osc2_semi", "Osc 2 pitch", -24, 24, -12, "st", 1, 0 },
	{ "osc2_fine", "Osc 2 fine", -50, 50, 7, "ct", 0, 0 },
	{ "mix", "Osc mix", 0, 1, 0.4f, "", 0, 0 },
	{ "noise", "Noise", 0, 1, 0, "", 0, 0 },
	{ "pw", "Pulse width", 0.05f, 0.95f, 0.5f, "", 0, 0 },
	{ "filter", "Filter", 0, 2, 0, "", 1, MODES },
	{ "cutoff", "Cutoff", 20, 18000, 1200, "Hz", 0, 0 },
	{ "resonance", "Resonance", 0.5f, 12, 1.5f, "", 0, 0 },
	{ "env_amt", "Env amount", -4, 6, 2.5f, "oct", 0, 0 },
	{ "key_track", "Key track", 0, 1, 0.5f, "", 0, 0 },
	{ "attack", "Attack", 0.001f, 3, 0.01f, "s", 0, 0 },
	{ "decay", "Decay", 0.01f, 5, 0.3f, "s", 0, 0 },
	{ "sustain", "Sustain", 0, 1, 0.7f, "", 0, 0 },
	{ "release", "Release", 0.01f, 5, 0.3f, "s", 0, 0 },
	{ "f_attack", "F attack", 0.001f, 3, 0.005f, "s", 0, 0 },
	{ "f_decay", "F decay", 0.01f, 5, 0.4f, "s", 0, 0 },
	{ "f_sustain", "F sustain", 0, 1, 0.2f, "", 0, 0 },
	{ "f_release", "F release", 0.01f, 5, 0.3f, "s", 0, 0 },
	{ "volume", "Volume", -30, 6, -6, "dB", 0, 0 },
};

enum { NV = 8, CTL = 16 };			// the filter's coefficients every CTL frames
struct Voice { int note, age; float vel, p1, p2, tri1, tri2; KpAdsr amp, fenv; KpSvf svf; int ctl; };
static Voice V[NV];
static KpNoise s_noise;
static int s_rate = 44100, s_age;

static void prepare (int rate) { s_rate = rate; for (int i = 0; i < NV; i++) { V[i].amp.kill (); V[i].fenv.kill (); V[i].note = -1; } }

static inline float blep (float t, float dt)	// the polyBLEP residual at phase t (0..1), increment dt
{
	if (t < dt) { t /= dt; return t + t - t * t - 1; }
	if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
	return 0;
}

// one sample of a wave at phase p (turns), increment dt; tri: a leaky integrator's state (triangle)
static inline float osc (int wave, float p, float dt, float pw, float &tri)
{
	switch (wave)
	{
	case 0: return 2 * p - 1 - blep (p, dt);
	case 1: case 2:
	{
		float q = p + (1 - pw); if (q >= 1) q -= 1;
		float sq = (p < pw ? 1.0f : -1.0f) + blep (p, dt) - blep (q, dt);
		if (wave == 1) return sq;
		tri = dt * 4 * sq + (1 - 0.002f) * tri;		// the integrated square: a triangle
		return tri;
	}
	default: return kp_sin (p);
	}
}

static void noteOn (int note, int vel)
{
	int best = -1;
	for (int i = 0; i < NV && best < 0; i++) if (!V[i].amp.active ()) best = i;
	if (best < 0) { int old = 1 << 30; for (int i = 0; i < NV; i++) if (V[i].age < old) { old = V[i].age; best = i; } }
	Voice &v = V[best];
	bool fresh = !v.amp.active ();
	v.note = note; v.vel = vel / 127.0f; v.age = ++s_age; v.ctl = 0;
	if (fresh) { v.p1 = v.p2 = 0; v.tri1 = v.tri2 = 0; v.svf.reset (); }
	v.amp.set (kp_param (P_A), kp_param (P_D), kp_param (P_S), kp_param (P_R), s_rate);
	v.fenv.set (kp_param (P_FA), kp_param (P_FD), kp_param (P_FS), kp_param (P_FR), s_rate);
	v.amp.on (); v.fenv.on ();
}

static void noteOff (int note)
{
	for (int i = 0; i < NV; i++) if (V[i].note == note && V[i].amp.active () && V[i].amp.stage != KpAdsr::RELEASE) { V[i].amp.off (); V[i].fenv.off (); }
}

static void allOff (bool hard)
{
	for (int i = 0; i < NV; i++) { if (hard) { V[i].amp.kill (); V[i].fenv.kill (); } else { V[i].amp.off (); V[i].fenv.off (); } }
}

static void render (float *l, float *r, int n)
{
	for (int k = 0; k < n; k++) l[k] = 0;
	int w1 = (int) kp_param (P_W1), w2 = (int) kp_param (P_W2), mode = (int) kp_param (P_MODE);
	float ratio2 = powf (2.0f, (kp_param (P_SEMI) * 100 + kp_param (P_FINE)) / 1200.0f);
	float mix = kp_param (P_MIX), noise = kp_param (P_NOISE), pw = kp_param (P_PW);
	float cut = kp_param (P_CUT), res = kp_param (P_RES), envAmt = kp_param (P_ENV), key = kp_param (P_KEY);
	float gain = kp_db (kp_param (P_VOL)) * 0.3f;
	for (int i = 0; i < NV; i++)
	{
		Voice &v = V[i];
		if (!v.amp.active ()) continue;
		float f = kp_mtof ((float) v.note), dt1 = f / s_rate, dt2 = dt1 * ratio2;
		if (dt2 > 0.45f) dt2 = 0.45f;
		float keyOct = key * ((float) v.note - 60.0f) / 12.0f;
		float g = gain * (0.3f + 0.7f * v.vel);
		for (int k = 0; k < n; k++)
		{
			float fe = v.fenv.next ();
			if (v.ctl-- <= 0) { v.ctl = CTL; v.svf.set (cut * powf (2.0f, keyOct + envAmt * fe), res, s_rate); }
			float s = osc (w1, v.p1, dt1, pw, v.tri1) * (1 - mix) + osc (w2, v.p2, dt2, pw, v.tri2) * mix;
			if (noise > 0) s += s_noise.next () * noise;
			l[k] += v.svf.tick (s, mode) * v.amp.next () * g;
			v.p1 += dt1; if (v.p1 >= 1) v.p1 -= 1;
			v.p2 += dt2; if (v.p2 >= 1) v.p2 -= 1;
		}
	}
	for (int k = 0; k < n; k++) r[k] = l[k];
}

static const KpDesc desc = {
	.name = "Subtractive", .kind = KP_INSTRUMENT, .params = P, .nparams = NP,
	.prepare = prepare, .noteOn = noteOn, .noteOff = noteOff, .allOff = allOff, .render = render,
};

KPLUG_MAIN (desc)
