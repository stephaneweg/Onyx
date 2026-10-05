//
// kp_eq3 -- a Koton effect plugin (user/Include/kplug.h): a three-band equaliser. A low shelf, a peaking middle
// (its frequency and width), a high shelf -- the RBJ cookbook's biquads -- and an output gain.
//
#include "kplug.h"

enum { P_LG, P_LF, P_MG, P_MF, P_MQ, P_HG, P_HF, P_OUT, NP };
static const KpParamDef P[NP] = {
	{ "low_gain", "Low", -18, 18, 0, "dB", 0, 0 },
	{ "low_freq", "Low freq", 40, 800, 150, "Hz", 0, 0 },
	{ "mid_gain", "Mid", -18, 18, 0, "dB", 0, 0 },
	{ "mid_freq", "Mid freq", 200, 8000, 1000, "Hz", 0, 0 },
	{ "mid_q", "Mid width", 0.3f, 6, 0.8f, "Q", 0, 0 },
	{ "high_gain", "High", -18, 18, 0, "dB", 0, 0 },
	{ "high_freq", "High freq", 1500, 16000, 5000, "Hz", 0, 0 },
	{ "output", "Output", -18, 12, 0, "dB", 0, 0 },
};

static KpBiquad s_b[3][2];			// [band][channel]
static float s_out = 1;
static int s_rate = 44100;

static void prepare (int rate) { s_rate = rate; for (int b = 0; b < 3; b++) for (int c = 0; c < 2; c++) s_b[b][c].reset (); }

static void setParam (int i, float)
{
	int band = i <= P_LF ? 0 : i <= P_MQ ? 1 : i <= P_HF ? 2 : -1;
	for (int c = 0; c < 2; c++)
	{
		if (band == 0) s_b[0][c].set (KpBiquad::LOWSHELF, kp_param (P_LF), 0.707f, kp_param (P_LG), s_rate);
		if (band == 1) s_b[1][c].set (KpBiquad::PEAK, kp_param (P_MF), kp_param (P_MQ), kp_param (P_MG), s_rate);
		if (band == 2) s_b[2][c].set (KpBiquad::HIGHSHELF, kp_param (P_HF), 0.707f, kp_param (P_HG), s_rate);
	}
	if (i == P_OUT) s_out = kp_db (kp_param (P_OUT));
}

static void process (float *l, float *r, int n)
{
	for (int k = 0; k < n; k++)
	{
		float a = l[k], b = r[k];
		for (int band = 0; band < 3; band++) { a = s_b[band][0].tick (a); b = s_b[band][1].tick (b); }
		l[k] = a * s_out; r[k] = b * s_out;
	}
}

static const KpDesc desc = {
	.name = "EQ 3-band", .kind = KP_EFFECT, .params = P, .nparams = NP,
	.prepare = prepare, .process = process, .setParam = setParam,
};

KPLUG_MAIN (desc)
