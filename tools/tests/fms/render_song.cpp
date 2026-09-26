//
// render_song.cpp -- render a whole .FMS song to a WAV with the Onyx synthesizer and the
// same sequencing as fmsplayer / fmtracker (one pass, no loop). Prints its length and level.
//   render_song song.FMS out.wav
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
#include "../../../user/Apps/fmtracker/fms.h"

int main (int argc, char **argv)
{
	FILE *f = fopen (argv[1], "rb"); if (!f) return 2;
	static unsigned char d[1 << 20]; int n = (int) fread (d, 1, sizeof d, f); fclose (f);
	static FmsSong s; s.npat = 0;
	if (!fms_parse (d, n, &s)) { printf ("not an FMS\n"); return 1; }
	SoundAcquire (1);
	for (int c = 0; c < FMS_CH; c++) { kapi_fm_instrument k; fms_to_kapi (&s.ins[c], &k); SoundInstrument (1, c, &k); }
	long total = 0; for (int p = 0; p < s.npat; p++) total += (long) s.pat[p].rows * s.pat[p].speed * (SND_RATE / 20);
	total += SND_RATE;					// + 1 s of release
	s16 *out = (s16 *) calloc (total * 2, sizeof (s16)); long pos = 0; int notes = 0;
	for (int p = 0; p < s.npat; p++)
		for (int r = 0; r < s.pat[p].rows; r++)
		{
			for (int c = 0; c < FMS_CH; c++)
			{
				unsigned char v = s.pat[p].n[c * s.pat[p].rows + r];
				if (v & 128) SoundStop (1, c);
				else if ((v & 7) == 0) continue;
				else if (s.pat[p].mute[c]) SoundStop (1, c);
				else { SoundStart (1, c, fms_note_mhz (v), SOUND_FM, 220); notes++; }
			}
			int fr = s.pat[p].speed * (SND_RATE / 20);
			Render (out + pos * 2, fr); pos += fr;
		}
	SoundStop (1, -1); Render (out + pos * 2, SND_RATE); pos += SND_RATE;
	double sum = 0; int peak = 0, clip = 0;
	for (long i = 0; i < pos * 2; i++) { int x = out[i]; sum += (double) x * x; if (abs (x) > peak) peak = abs (x); if (abs (x) >= 32767) clip++; }
	printf ("%s: %d patterns, %d notes, %.1f s, peak %d, rms %.0f, clipped %d\n", argv[1], s.npat, notes, pos / 44100.0, peak, __builtin_sqrt (sum / (pos * 2)), clip);
	if (argc > 2)
	{
		FILE *w = fopen (argv[2], "wb"); u32 data = (u32) pos * 4, v; u16 x;
		fwrite ("RIFF", 1, 4, w); v = 36 + data; fwrite (&v, 4, 1, w); fwrite ("WAVEfmt ", 1, 8, w);
		v = 16; fwrite (&v, 4, 1, w); x = 1; fwrite (&x, 2, 1, w); x = 2; fwrite (&x, 2, 1, w);
		v = 44100; fwrite (&v, 4, 1, w); v = 44100 * 4; fwrite (&v, 4, 1, w); x = 4; fwrite (&x, 2, 1, w); x = 16; fwrite (&x, 2, 1, w);
		fwrite ("data", 1, 4, w); fwrite (&data, 4, 1, w); fwrite (out, 4, pos, w); fclose (w);
	}
	return 0;
}
