//
// kp_pluck -- a Koton instrument plugin (user/kplug.h): plucked strings, Karplus-Strong. A burst of
// noise (its brightness, the pick's position: a comb) is loaded into a delay line one period long;
// the line feeds back through a one-zero low-pass (the damping) and an all-pass (the fine tuning)
// with the gain that makes it fall 60 dB in the decay time -- a kora, a harp, a guitar. A released
// key damps its string. 12 voices, spread across the stereo field by pitch.
//
#include "kplug.h"

enum { P_DECAY, P_DAMP, P_BRIGHT, P_PICK, P_RELEASE, P_WIDTH, P_VELSENS, P_VOLUME, NP };
static const KpParamDef P[NP] = {
	{ "decay", "Decay", 0.1f, 20, 4, "s", 0, 0 },
	{ "damping", "Damping", 0, 1, 0.35f, "", 0, 0 },
	{ "brightness", "Brightness", 0, 1, 0.7f, "", 0, 0 },
	{ "pick", "Pick point", 0.02f, 0.5f, 0.13f, "", 0, 0 },
	{ "release", "Release", 0.02f, 2, 0.25f, "s", 0, 0 },
	{ "width", "Width", 0, 1, 0.6f, "", 0, 0 },
	{ "vel_sens", "Velocity", 0, 1, 0.6f, "", 0, 0 },
	{ "volume", "Volume", -30, 6, -3, "dB", 0, 0 },
};

enum { NV = 12, LINE = 4096 };
struct Voice
{
	float buf[LINE];
	int len, pos, note, age;
	float frac, ap, apX, apY, z1, gain, relGain, level, panL, panR;
	bool on, held;
};
static Voice V[NV];
static KpNoise s_noise;
static int s_rate = 44100, s_age;

static void prepare (int rate) { s_rate = rate; for (int i = 0; i < NV; i++) V[i].on = false; }

// the loop's gain per period for a fall of 60 dB in t seconds
static inline float loopGain (float freq, float t) { return powf (0.001f, 1.0f / (freq * (t < 0.01f ? 0.01f : t))); }

static void noteOn (int note, int vel)
{
	int best = -1;
	for (int i = 0; i < NV && best < 0; i++) if (!V[i].on) best = i;
	if (best < 0) { float lo = 1e9f; for (int i = 0; i < NV; i++) if (V[i].level < lo) { lo = V[i].level; best = i; } }
	Voice &v = V[best];
	float f = kp_mtof ((float) note);
	float damp = kp_param (P_DAMP) * 0.5f;
	// the period = the line + the low-pass's delay (damp) + the all-pass's (frac)
	float period = (float) s_rate / f - damp;
	if (period < 2) period = 2;
	if (period > LINE - 2) period = LINE - 2;
	v.len = (int) period; v.frac = period - (float) v.len;
	if (v.frac < 0.1f) { v.len--; v.frac += 1; }			// (the all-pass is best between 0.1 and 1.1)
	v.ap = (1 - v.frac) / (1 + v.frac); v.apX = v.apY = 0; v.z1 = 0;
	v.gain = loopGain (f, kp_param (P_DECAY));
	v.relGain = loopGain (f, kp_param (P_RELEASE));
	// the excitation: noise, low-passed (brightness), minus itself a pick position later (a comb)
	float sens = kp_param (P_VELSENS), amp = (1 - sens) + sens * vel / 127.0f;
	float b = 0.05f + 0.95f * kp_param (P_BRIGHT) * (0.5f + 0.5f * amp), lp = 0;
	int pick = (int) (kp_param (P_PICK) * v.len); if (pick < 1) pick = 1;
	for (int k = 0; k < v.len; k++) { lp += b * (s_noise.next () - lp); v.buf[k] = lp; }
	for (int k = v.len - 1; k >= pick; k--) v.buf[k] -= v.buf[k - pick];
	float mean = 0; for (int k = 0; k < v.len; k++) mean += v.buf[k];
	mean /= (float) v.len;
	for (int k = 0; k < v.len; k++) v.buf[k] = (v.buf[k] - mean) * amp;
	v.pos = 0; v.note = note; v.age = ++s_age; v.on = true; v.held = true; v.level = amp;
	float pan = 0.5f + kp_param (P_WIDTH) * kp_clampf (((float) note - 60.0f) / 48.0f, -0.5f, 0.5f);
	v.panL = cosf (pan * KP_PI * 0.5f) * 1.41f; v.panR = sinf (pan * KP_PI * 0.5f) * 1.41f;
}

static void noteOff (int note) { for (int i = 0; i < NV; i++) if (V[i].on && V[i].note == note) V[i].held = false; }
static void allOff (bool hard) { for (int i = 0; i < NV; i++) { if (hard) V[i].on = false; else V[i].held = false; } }

static void render (float *l, float *r, int n)
{
	for (int k = 0; k < n; k++) l[k] = r[k] = 0;
	float damp = kp_param (P_DAMP) * 0.5f, g0 = kp_db (kp_param (P_VOLUME)) * 0.5f;
	for (int i = 0; i < NV; i++)
	{
		Voice &v = V[i];
		if (!v.on) continue;
		float g = v.held ? v.gain : v.relGain, peak = 0;
		for (int k = 0; k < n; k++)
		{
			float x = v.buf[v.pos];
			float y = (1 - damp) * x + damp * v.z1;			// the damping: a one-zero low-pass
			v.z1 = x;
			float a = v.ap * (y - v.apY) + v.apX;			// the fine tuning: an all-pass
			v.apX = y; v.apY = a;
			v.buf[v.pos] = a * g;
			if (++v.pos >= v.len) v.pos = 0;
			l[k] += x * v.panL * g0; r[k] += x * v.panR * g0;
			float ax = x < 0 ? -x : x; if (ax > peak) peak = ax;
		}
		v.level = v.level * 0.9f + peak * 0.1f;
		if (peak < 1e-4f && v.level < 1e-4f) v.on = false;
	}
}

static const KpDesc desc = {
	.name = "Plucked strings", .kind = KP_INSTRUMENT, .params = P, .nparams = NP,
	.prepare = prepare, .noteOn = noteOn, .noteOff = noteOff, .allOff = allOff, .render = render,
};

KPLUG_MAIN (desc)
