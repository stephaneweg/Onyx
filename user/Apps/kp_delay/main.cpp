//
// kp_delay -- a Koton effect plugin (user/Include/kplug.h): an echo. A delay time in milliseconds (not tied to
// the tempo), its feedback through a low-pass (each repeat darker), ping-pong (the repeats bounce
// between the left and the right), the stereo width of the repeats, a dry / wet mix. A change of the
// time glides (no zipper noise).
//
#include "kplug.h"

static const char *const ONOFF[] = { "Off", "On", 0 };
enum { P_TIME, P_FEEDBACK, P_TONE, P_PINGPONG, P_WIDTH, P_MIX, NP };
static const KpParamDef P[NP] = {
	{ "time", "Time", 1, 2000, 350, "ms", 0, 0 },
	{ "feedback", "Feedback", 0, 0.95f, 0.4f, "", 0, 0 },
	{ "tone", "Tone", 500, 18000, 6000, "Hz", 0, 0 },
	{ "ping_pong", "Ping-pong", 0, 1, 1, "", 1, ONOFF },
	{ "width", "Width", 0, 1, 1, "", 0, 0 },
	{ "mix", "Mix", 0, 1, 0.3f, "", 0, 0 },
};

enum { LEN = 1 << 17 };				// 2.97 s at 44.1 kHz, 2.73 s at 48 kHz
static float s_l[LEN], s_r[LEN];
static unsigned s_w;
static float s_time, s_lpL, s_lpR, s_lpCoef;
static int s_rate = 44100;

static void prepare (int rate) { s_rate = rate; memset (s_l, 0, sizeof s_l); memset (s_r, 0, sizeof s_r); s_w = 0; s_time = -1; s_lpL = s_lpR = 0; }
static void setParam (int i, float v) { if (i == P_TONE) s_lpCoef = 1 - expf (-2 * KP_PI * v / s_rate); }

static inline float tap (const float *b, float d)		// the line d samples back (linear interpolation)
{
	float pos = (float) s_w - d; while (pos < 0) pos += (float) LEN;
	int i = (int) pos; float t = pos - (float) i;
	return b[i & (LEN - 1)] * (1 - t) + b[(i + 1) & (LEN - 1)] * t;
}

static void process (float *l, float *r, int n)
{
	float target = kp_param (P_TIME) * 0.001f * s_rate;
	if (target > LEN - 4) target = LEN - 4;
	if (s_time < 0) s_time = target;
	float fb = kp_param (P_FEEDBACK), mix = kp_param (P_MIX), w = kp_param (P_WIDTH);
	bool pp = kp_param (P_PINGPONG) >= 0.5f;
	for (int k = 0; k < n; k++)
	{
		s_time += (target - s_time) * 0.0005f;			// (a glide to a new time)
		float dl = tap (s_l, s_time), dr = tap (s_r, s_time);
		s_lpL += s_lpCoef * (dl - s_lpL); s_lpR += s_lpCoef * (dr - s_lpR);
		float inL = l[k], inR = r[k];
		if (pp) { s_l[s_w] = (inL + inR) * 0.5f + s_lpR * fb; s_r[s_w] = s_lpL * fb; }
		else { s_l[s_w] = inL + s_lpL * fb; s_r[s_w] = inR + s_lpR * fb; }
		s_w = (s_w + 1) & (LEN - 1);
		// the width: the wet signal's sides narrowed toward its middle
		float m = (dl + dr) * 0.5f, sd = (dl - dr) * 0.5f * w;
		l[k] = inL * (1 - mix) + (m + sd) * mix;
		r[k] = inR * (1 - mix) + (m - sd) * mix;
	}
}

static const KpDesc desc = {
	.name = "Delay", .kind = KP_EFFECT, .params = P, .nparams = NP,
	.prepare = prepare, .process = process, .setParam = setParam,
};

KPLUG_MAIN (desc)
