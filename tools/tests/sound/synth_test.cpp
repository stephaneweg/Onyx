//
// synth_test.cpp -- the kernel synthesizer (kernel/sys/sound.cpp) on a PC: renders notes of
// an FM instrument (.FMI, the fmtracker format) or a simple wave into a WAV file, and
// prints the level / pitch so a test can check them.
//   synth_test out.wav [instrument.FMI] [freq]
//
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
typedef int16_t s16; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t s32;
typedef uint64_t u64; typedef int64_t s64; typedef bool boolean;
#define TRUE true
#define FALSE false
#define IRQ_LEVEL 0
struct CSpinLock { CSpinLock (int) {} void Acquire () {} void Release () {} };
#define SND_RATE	44100
#define SND_VOICES	16
#define SOUND_HOST_TEST
#include "../../../kernel/sys/sound.cpp"

// .FMI: "fm-song instrument", "NAME", then 13 "a,b" lines: attack, decay, sustain, release,
// level, ksl, am(field "amplitudevibrato"), vib("pitchvibrato"), mult, ksr, wave,
// sustained, (feedback, connection). 1 = modulator (OPL slot +0), 2 = carrier (+3).
static bool loadFMI (const char *path, kapi_fm_instrument *ins)
{
	FILE *f = fopen (path, "rb"); if (!f) return false;
	char line[128]; int v[13][2]; int n = 0;
	if (!fgets (line, sizeof line, f) || !fgets (line, sizeof line, f)) { fclose (f); return false; }
	while (n < 13 && fgets (line, sizeof line, f)) if (sscanf (line, "%d,%d", &v[n][0], &v[n][1]) == 2) n++;
	fclose (f);
	if (n < 13) return false;
	for (int k = 0; k < 2; k++)
	{
		kapi_fm_op &o = ins->op[k];
		o.attack = v[0][k]; o.decay = v[1][k]; o.sustain = v[2][k]; o.release = v[3][k];
		o.level = v[4][k]; o.ksl = v[5][k]; o.mult = v[8][k]; o.wave = v[10][k];
		o.flags = (v[11][k] ? FM_SUSTAINED : 0) | (v[7][k] ? FM_TREMOLO : 0) | (v[6][k] ? FM_VIBRATO : 0) | (v[9][k] ? FM_KSR : 0);
	}
	ins->feedback = v[12][0]; ins->connection = v[12][1];
	return true;
}

int main (int argc, char **argv)
{
	const char *out = argc > 1 ? argv[1] : "out.wav";
	double hz = argc > 3 ? atof (argv[3]) : 261.63;
	kapi_fm_instrument ins;
	bool fm = argc > 2 && loadFMI (argv[2], &ins);
	SoundAcquire (1);
	if (fm) SoundInstrument (1, 0, &ins);
	static s16 buf[44100 * 3 * 2];
	SoundStart (1, 0, (unsigned) (hz * 1000), fm ? SOUND_FM : SOUND_SINE, 200);
	Render (buf, 44100);					// 1 s key down
	SoundStop (1, 0);
	Render (buf + 44100 * 2, 44100);			// 1 s release
	// report: peak / RMS of the first 0.5 s, of the last 0.25 s, zero crossings -> pitch
	auto stats = [&] (int from, int n, double *rms) { double s = 0; int pk = 0; for (int i = 0; i < n; i++) { int x = buf[(from + i) * 2]; s += (double) x * x; if (abs (x) > pk) pk = abs (x); } *rms = __builtin_sqrt (s / n); return pk; };
	double r1, r2; int p1 = stats (0, 22050, &r1), p2 = stats (44100 * 2 - 11025, 11025, &r2);
	int zc = 0; for (int i = 4411; i < 4411 + 22050; i++) if ((buf[(i - 1) * 2] < 0) != (buf[i * 2] < 0)) zc++;
	printf ("%s: peak %d rms %.0f | end peak %d rms %.0f | crossings/s %.0f\n", fm ? argv[2] : "sine", p1, r1, p2, r2, zc * 2.0);
	FILE *f = fopen (out, "wb");
	int frames = 88200; u32 data = frames * 4;
	fwrite ("RIFF", 1, 4, f); u32 v = 36 + data; fwrite (&v, 4, 1, f); fwrite ("WAVEfmt ", 1, 8, f);
	v = 16; fwrite (&v, 4, 1, f); u16 w = 1; fwrite (&w, 2, 1, f); w = 2; fwrite (&w, 2, 1, f);
	v = 44100; fwrite (&v, 4, 1, f); v = 44100 * 4; fwrite (&v, 4, 1, f); w = 4; fwrite (&w, 2, 1, f); w = 16; fwrite (&w, 2, 1, f);
	fwrite ("data", 1, 4, f); fwrite (&data, 4, 1, f); fwrite (buf, 4, frames, f); fclose (f);
	return 0;
}
