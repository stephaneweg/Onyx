//
// kp_reverb -- a Koton effect plugin (user/kplug.h): Freeverb (Jezar at Dreampoint's public-domain
// reverb): eight damped comb filters in parallel then four all-passes in series, per channel, the
// right's delays 23 samples longer (the stereo); a room size, a damping, a width, a pre-delay, the wet
// and dry levels.
//
#include "kplug.h"

enum { P_ROOM, P_DAMP, P_WIDTH, P_PRE, P_WET, P_DRY, NP };
static const KpParamDef P[NP] = {
	{ "room", "Room size", 0, 1, 0.75f, "", 0, 0 },
	{ "damping", "Damping", 0, 1, 0.5f, "", 0, 0 },
	{ "width", "Width", 0, 1, 1, "", 0, 0 },
	{ "predelay", "Pre-delay", 0, 100, 10, "ms", 0, 0 },
	{ "wet", "Wet", 0, 1, 0.3f, "", 0, 0 },
	{ "dry", "Dry", 0, 1, 1, "", 0, 0 },
};

static const int COMBS[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 }, APS[4] = { 556, 441, 341, 225 };
enum { SPREAD = 23, CMAX = 4096, AMAX = 2048, PMAX = 8192 };

struct Comb { float buf[CMAX]; int len, i; float store; };
struct AllPass { float buf[AMAX]; int len, i; };
static Comb s_cL[8], s_cR[8];
static AllPass s_aL[4], s_aR[4];
static float s_pre[2][PMAX];
static int s_preW, s_rate = 44100;
static float s_fb, s_d1, s_d2;

static void prepare (int rate)
{
	s_rate = rate;
	float sc = rate / 44100.0f;
	for (int c = 0; c < 8; c++)
	{
		s_cL[c].len = (int) (COMBS[c] * sc); s_cR[c].len = (int) ((COMBS[c] + SPREAD) * sc);
		if (s_cL[c].len > CMAX) s_cL[c].len = CMAX;
		if (s_cR[c].len > CMAX) s_cR[c].len = CMAX;
		memset (s_cL[c].buf, 0, sizeof s_cL[c].buf); memset (s_cR[c].buf, 0, sizeof s_cR[c].buf);
		s_cL[c].i = s_cR[c].i = 0; s_cL[c].store = s_cR[c].store = 0;
	}
	for (int a = 0; a < 4; a++)
	{
		s_aL[a].len = (int) (APS[a] * sc); s_aR[a].len = (int) ((APS[a] + SPREAD) * sc);
		if (s_aL[a].len > AMAX) s_aL[a].len = AMAX;
		if (s_aR[a].len > AMAX) s_aR[a].len = AMAX;
		memset (s_aL[a].buf, 0, sizeof s_aL[a].buf); memset (s_aR[a].buf, 0, sizeof s_aR[a].buf);
		s_aL[a].i = s_aR[a].i = 0;
	}
	memset (s_pre, 0, sizeof s_pre); s_preW = 0;
}

static void setParam (int i, float v)
{
	if (i == P_ROOM) s_fb = v * 0.28f + 0.7f;
	if (i == P_DAMP) { s_d1 = v * 0.4f; s_d2 = 1 - s_d1; }
}

static inline float comb (Comb &c, float in)
{
	float out = c.buf[c.i];
	c.store = out * s_d2 + c.store * s_d1;
	c.buf[c.i] = in + c.store * s_fb;
	if (++c.i >= c.len) c.i = 0;
	return out;
}
static inline float allpass (AllPass &a, float in)
{
	float b = a.buf[a.i], out = b - in;
	a.buf[a.i] = in + b * 0.5f;
	if (++a.i >= a.len) a.i = 0;
	return out;
}

static void process (float *l, float *r, int n)
{
	float wet = kp_param (P_WET) * 3.0f, dry = kp_param (P_DRY), w = kp_param (P_WIDTH);
	float w1 = wet * (w * 0.5f + 0.5f), w2 = wet * ((1 - w) * 0.5f);
	int pre = (int) (kp_param (P_PRE) * 0.001f * s_rate); if (pre > PMAX - 1) pre = PMAX - 1;
	for (int k = 0; k < n; k++)
	{
		s_pre[0][s_preW] = l[k]; s_pre[1][s_preW] = r[k];
		int ri = (s_preW - pre) & (PMAX - 1);
		s_preW = (s_preW + 1) & (PMAX - 1);
		float in = (s_pre[0][ri] + s_pre[1][ri]) * 0.015f;
		float oL = 0, oR = 0;
		for (int c = 0; c < 8; c++) { oL += comb (s_cL[c], in); oR += comb (s_cR[c], in); }
		for (int a = 0; a < 4; a++) { oL = allpass (s_aL[a], oL); oR = allpass (s_aR[a], oR); }
		float dl = l[k], dr = r[k];
		l[k] = oL * w1 + oR * w2 + dl * dry;
		r[k] = oR * w1 + oL * w2 + dr * dry;
	}
}

static const KpDesc desc = {
	.name = "Reverb", .kind = KP_EFFECT, .params = P, .nparams = NP,
	.prepare = prepare, .process = process, .setParam = setParam,
};

KPLUG_MAIN (desc)
