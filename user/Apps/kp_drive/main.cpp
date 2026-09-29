//
// kp_drive -- a Koton effect plugin (user/kplug.h): saturation, overdrive, distortion, fuzz -- Koton's
// Drive (Plugins/Effects/KotonPluginDrive), the same parameters and states. A low cut before the gain
// (the lows would turn to mush), the gain into a transfer curve at 4x the rate (clipping makes
// harmonics far above the audible band: they would fold back as noise without the oversampling;
// 4th-order Butterworth up and down), a tone low-pass after it, a level compensation (the grain is
// heard, not the volume), the mix and the level.
//
#include "kplug.h"

static const char *const TYPES[] = { "Soft", "Overdrive", "Tube", "Distortion", "Fuzz", "Wavefolder", 0 };
enum { P_TYPE, P_DRIVE, P_BASS, P_TONE, P_BIAS, P_MIX, P_LEVEL, NP };
static const KpParamDef P[NP] = {
	{ "type", "Character", 0, 5, 1, "", 1, TYPES },
	{ "drive", "Drive", 0, 40, 14, "dB", 0, 0 },
	{ "bass_cut", "Low cut", 20, 600, 90, "Hz", 0, 0 },
	{ "tone", "Tone", 0, 1, 0.6f, "", 0, 0 },
	{ "bias", "Asymmetry", 0, 1, 0.25f, "", 0, 0 },
	{ "mix", "Mix", 0, 1, 1, "", 0, 0 },
	{ "level", "Level", -30, 12, 0, "dB", 0, 0 },
};

enum { OS = 4 };
static KpBiquad s_up[2][2], s_down[2][2];	// [channel][stage]
static float s_hp[2], s_tone[2];
static int s_rate = 44100;

static void prepare (int rate)
{
	s_rate = rate;
	for (int c = 0; c < 2; c++)
		for (int i = 0; i < 2; i++)
		{
			float q = i == 0 ? 0.5412f : 1.3066f;		// two biquads: a 4th-order Butterworth
			s_up[c][i].set (KpBiquad::LOWPASS, rate * 0.45f, q, 0, rate * OS); s_up[c][i].reset ();
			s_down[c][i].set (KpBiquad::LOWPASS, rate * 0.45f, q, 0, rate * OS); s_down[c][i].reset ();
		}
	s_hp[0] = s_hp[1] = s_tone[0] = s_tone[1] = 0;
}

static inline float sgn (float x) { return x < 0 ? -1.0f : x > 0 ? 1.0f : 0.0f; }
static inline float shape (int type, float x, float bias)
{
	switch (type)
	{
	case 0: return tanhf (x * 0.7f);
	case 1:
	{
		float a = fabsf (x);
		if (a < 1.0f / 3) return 2 * x;
		if (a < 2.0f / 3) { float t = 2 - 3 * a; return sgn (x) * (3 - t * t) / 3; }
		return sgn (x);
	}
	case 2:
	{
		float s = x + bias * 0.5f;
		float y = s >= 0 ? tanhf (s * 0.8f) : tanhf (s * 1.4f);
		return y - tanhf (bias * 0.5f * 0.8f);
	}
	case 3:
	{
		float a = fabsf (x);
		float y = a < 0.7f ? x : sgn (x) * (0.7f + (a - 0.7f) / (1 + (a - 0.7f) * 4));
		return kp_clampf (y, -1, 1);
	}
	case 4: { float y = kp_clampf (x, -1, 1); return y * 0.85f + fabsf (y) * 0.15f - 0.075f; }
	default: { float t = (x + 1) * 0.25f; return 4 * fabsf (t - floorf (t + 0.5f)) - 1; }
	}
}

static void process (float *l, float *r, int n)
{
	int type = (int) (kp_param (P_TYPE) + 0.5f);
	float gain = kp_db (kp_param (P_DRIVE)), bias = kp_param (P_BIAS), mix = kp_param (P_MIX), out = kp_db (kp_param (P_LEVEL));
	float hpC = 1 - expf (-2 * KP_PI * kp_param (P_BASS) / s_rate);
	float tone = kp_param (P_TONE), toneHz = 800 + tone * tone * 11000;
	if (toneHz > s_rate * 0.45f) toneHz = s_rate * 0.45f;
	float toneC = 1 - expf (-2 * KP_PI * toneHz / s_rate);
	float comp = 1 / (1 + 0.7f * (gain - 1) / (1 + 0.25f * (gain - 1)));
	float *io[2] = { l, r };
	for (int c = 0; c < 2; c++)
	{
		float *x = io[c];
		for (int k = 0; k < n; k++)
		{
			float dry = x[k];
			s_hp[c] += hpC * (dry - s_hp[c]);
			float pre = dry - s_hp[c], y = 0;
			for (int o = 0; o < OS; o++)
			{
				float u = o == 0 ? pre * (float) OS : 0;
				u = s_up[c][1].tick (s_up[c][0].tick (u));
				u = shape (type, u * gain, bias);
				u = s_down[c][1].tick (s_down[c][0].tick (u));
				if (o == OS - 1) y = u;
			}
			s_tone[c] += toneC * (y - s_tone[c]);
			x[k] = (dry * (1 - mix) + s_tone[c] * comp * mix) * out;
		}
	}
}

static const KpDesc desc = {
	.name = "Drive", .kind = KP_EFFECT, .params = P, .nparams = NP,
	.prepare = prepare, .process = process,
};

KPLUG_MAIN (desc)
