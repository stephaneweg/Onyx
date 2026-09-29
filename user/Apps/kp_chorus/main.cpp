//
// kp_chorus -- a Koton effect plugin (user/kplug.h): a chorus. One to three voices per channel read a
// short delay line whose length a sine LFO sweeps (their phases spread; the right channel's LFOs
// shifted by `spread`); feedback makes it a flanger, the mix blends it with the dry sound.
//
#include "kplug.h"

enum { P_RATE, P_DEPTH, P_DELAY, P_FEEDBACK, P_VOICES, P_SPREAD, P_MIX, NP };
static const KpParamDef P[NP] = {
	{ "rate", "Rate", 0.05f, 5, 0.8f, "Hz", 0, 0 },
	{ "depth", "Depth", 0, 10, 3, "ms", 0, 0 },
	{ "delay", "Delay", 2, 30, 12, "ms", 0, 0 },
	{ "feedback", "Feedback", -0.9f, 0.9f, 0, "", 0, 0 },
	{ "voices", "Voices", 1, 3, 2, "", 1, 0 },
	{ "spread", "Spread", 0, 1, 1, "", 0, 0 },
	{ "mix", "Mix", 0, 1, 0.5f, "", 0, 0 },
};

enum { LEN = 8192 };
static float s_l[LEN], s_r[LEN];
static unsigned s_w;
static float s_phase, s_fbL, s_fbR;
static int s_rate = 44100;

static void prepare (int rate) { s_rate = rate; memset (s_l, 0, sizeof s_l); memset (s_r, 0, sizeof s_r); s_w = 0; s_phase = 0; s_fbL = s_fbR = 0; }

static inline float tap (const float *b, float d)
{
	float pos = (float) s_w - d; while (pos < 0) pos += (float) LEN;
	int i = (int) pos; float t = pos - (float) i;
	return b[i & (LEN - 1)] * (1 - t) + b[(i + 1) & (LEN - 1)] * t;
}

static void process (float *l, float *r, int n)
{
	float rate = kp_param (P_RATE) / s_rate, depth = kp_param (P_DEPTH) * 0.001f * s_rate, base = kp_param (P_DELAY) * 0.001f * s_rate;
	float fb = kp_param (P_FEEDBACK), mix = kp_param (P_MIX), spread = kp_param (P_SPREAD) * 0.25f;
	int nv = (int) kp_param (P_VOICES); if (nv < 1) nv = 1;
	float norm = 1.0f / (float) nv;
	for (int k = 0; k < n; k++)
	{
		float inL = l[k], inR = r[k];
		s_l[s_w] = inL + s_fbL * fb; s_r[s_w] = inR + s_fbR * fb;
		float wl = 0, wr = 0;
		for (int v = 0; v < nv; v++)
		{
			float ph = s_phase + (float) v / (float) nv;
			wl += tap (s_l, base + depth * 0.5f * (1 + kp_sin (ph)));
			wr += tap (s_r, base + depth * 0.5f * (1 + kp_sin (ph + spread)));
		}
		wl *= norm; wr *= norm;
		s_fbL = wl; s_fbR = wr;
		s_w = (s_w + 1) & (LEN - 1);
		s_phase += rate; if (s_phase >= 1) s_phase -= 1;
		l[k] = inL * (1 - mix * 0.5f) + wl * mix;
		r[k] = inR * (1 - mix * 0.5f) + wr * mix;
	}
}

static const KpDesc desc = {
	.name = "Chorus", .kind = KP_EFFECT, .params = P, .nparams = NP,
	.prepare = prepare, .process = process,
};

KPLUG_MAIN (desc)
