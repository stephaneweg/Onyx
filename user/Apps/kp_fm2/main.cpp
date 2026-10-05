//
// kp_fm2 -- a Koton instrument plugin (user/Include/kplug.h): two-operator FM, the kernel's FM voices'
// algorithm in float. A modulator (its ratio to the note, a fine detune, self-feedback) bends the
// phase of a sine carrier; the modulation index follows its own envelope (the brightness that fades
// as a note rings), the amplitude the other. 16 voices.
//
#include "kplug.h"

enum { P_RATIO, P_INDEX, P_FEEDBACK, P_ATTACK, P_DECAY, P_SUSTAIN, P_RELEASE, P_MDECAY, P_MSUSTAIN, P_DETUNE, P_VELSENS, P_VOLUME, NP };
static const KpParamDef P[NP] = {
	{ "ratio", "Ratio", 0.5f, 16, 2, "", 0.5f, 0 },
	{ "index", "Index", 0, 12, 3, "", 0, 0 },
	{ "feedback", "Feedback", 0, 1, 0, "", 0, 0 },
	{ "attack", "Attack", 0.001f, 2, 0.005f, "s", 0, 0 },
	{ "decay", "Decay", 0.01f, 5, 0.8f, "s", 0, 0 },
	{ "sustain", "Sustain", 0, 1, 0.5f, "", 0, 0 },
	{ "release", "Release", 0.01f, 5, 0.4f, "s", 0, 0 },
	{ "mod_decay", "Mod decay", 0.01f, 5, 0.5f, "s", 0, 0 },
	{ "mod_sustain", "Mod sustain", 0, 1, 0.3f, "", 0, 0 },
	{ "detune", "Detune", -50, 50, 0, "ct", 0, 0 },
	{ "vel_sens", "Velocity", 0, 1, 0.7f, "", 0, 0 },
	{ "volume", "Volume", -30, 6, -6, "dB", 0, 0 },
};

enum { NV = 16 };
struct Voice { int note, age; float vel, pc, pm, fb1, fb2; KpAdsr amp, mod; };
static Voice V[NV];
static int s_rate = 44100, s_age;

static void prepare (int rate) { s_rate = rate; for (int i = 0; i < NV; i++) { V[i].amp.kill (); V[i].mod.kill (); V[i].note = -1; } }

static void noteOn (int note, int vel)
{
	int best = -1;
	for (int i = 0; i < NV && best < 0; i++) if (!V[i].amp.active ()) best = i;
	if (best < 0)				// steal: the quietest releasing one, else the oldest
	{
		float lo = 2; int old = 1 << 30;
		for (int i = 0; i < NV; i++) if (V[i].amp.stage == KpAdsr::RELEASE && V[i].amp.v < lo) { lo = V[i].amp.v; best = i; }
		if (best < 0) for (int i = 0; i < NV; i++) if (V[i].age < old) { old = V[i].age; best = i; }
	}
	Voice &v = V[best];
	v.note = note; v.vel = vel / 127.0f; v.age = ++s_age;
	v.pc = v.pm = v.fb1 = v.fb2 = 0;
	v.amp.set (kp_param (P_ATTACK), kp_param (P_DECAY), kp_param (P_SUSTAIN), kp_param (P_RELEASE), s_rate);
	v.mod.set (kp_param (P_ATTACK), kp_param (P_MDECAY), kp_param (P_MSUSTAIN), kp_param (P_RELEASE), s_rate);
	v.amp.on (); v.mod.on ();
}

static void noteOff (int note)
{
	for (int i = 0; i < NV; i++) if (V[i].note == note && V[i].amp.active () && V[i].amp.stage != KpAdsr::RELEASE) { V[i].amp.off (); V[i].mod.off (); }
}

static void allOff (bool hard)
{
	for (int i = 0; i < NV; i++) { if (hard) { V[i].amp.kill (); V[i].mod.kill (); } else { V[i].amp.off (); V[i].mod.off (); } }
}

static void render (float *l, float *r, int n)
{
	for (int k = 0; k < n; k++) l[k] = 0;
	float ratio = kp_param (P_RATIO) * powf (2.0f, kp_param (P_DETUNE) / 1200.0f);
	float index = kp_param (P_INDEX) / (2 * KP_PI), fbAmt = kp_param (P_FEEDBACK) * 0.25f, sens = kp_param (P_VELSENS);
	float gain = kp_db (kp_param (P_VOLUME)) * 0.25f;
	for (int i = 0; i < NV; i++)
	{
		Voice &v = V[i];
		if (!v.amp.active ()) continue;
		float fc = kp_mtof ((float) v.note) / s_rate, fm = fc * ratio;
		float vs = 1 - sens + sens * v.vel;
		float idx = index * (0.35f + 0.65f * vs), g = gain * vs;
		for (int k = 0; k < n; k++)
		{
			float m = kp_sin (v.pm + fbAmt * (v.fb1 + v.fb2));	// (the average of two: the feedback does not ring)
			v.fb2 = v.fb1; v.fb1 = m;
			float me = v.mod.next ();
			l[k] += kp_sin (v.pc + idx * me * m) * v.amp.next () * g;
			v.pc += fc; if (v.pc >= 1) v.pc -= 1;
			v.pm += fm; if (v.pm >= 1) v.pm -= 1;
		}
	}
	for (int k = 0; k < n; k++) r[k] = l[k];
}

static const KpDesc desc = {
	.name = "FM 2-op", .kind = KP_INSTRUMENT, .params = P, .nparams = NP, .editorW = 0, .editorH = 0,
	.prepare = prepare, .noteOn = noteOn, .noteOff = noteOff, .allOff = allOff, .cc = 0, .render = render,
};

KPLUG_MAIN (desc)
