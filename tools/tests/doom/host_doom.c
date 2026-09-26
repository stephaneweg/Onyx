//
// host_doom -- Doom (doomgeneric + user/doom/doom_sound.c) on the PC, headless, on a
// virtual clock: the picture to PPM files, the sound effects to a raw PCM file, the music's
// FM notes counted -- to check the Onyx port without the Pi.
//   host_doom <iwad> <seconds> [keys "t:key,t:key,..." (Doom key codes, held 0.2 s)]
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomgeneric.h"

static unsigned long long s_us = 0;			// the virtual clock
unsigned long long mus_now_us (void) { return s_us; }
uint32_t DG_GetTicksMs (void) { return (uint32_t) (s_us / 1000); }
void onyx_music_poll (void);
void DG_SleepMs (uint32_t ms) { s_us += (ms ? ms : 1) * 1000ull; onyx_music_poll (); }
void DG_SetWindowTitle (const char *t) { (void) t; }
void DG_Init (void) {}
int onyx_file_exists (const char *p) { FILE *f = fopen (p, "rb"); if (!f) return 0; fclose (f); return 1; }

static int s_frames = 0; static double s_end; static const char *s_keys = "";
static FILE *s_pcm; static long s_pcmFrames = 0; static long s_notes = 0, s_instr = 0; static int s_maxVoice = -1;
static unsigned s_queued = 0; static unsigned long long s_audioT = 0;

int kapi_sound_acquire (void) { return 1; }
int kapi_sound_status (unsigned *rate, unsigned *freeFrames, unsigned *owner)
{
	// the output drains 44100 frames a second of virtual time
	unsigned long long drained = (s_us - s_audioT) * 44100 / 1000000;
	s_audioT = s_us;
	s_queued = drained >= s_queued ? 0 : s_queued - (unsigned) drained;
	*rate = 44100; *freeFrames = 22049 - s_queued; *owner = 1;
	return 1;
}
int kapi_sound_write (const short *f, unsigned n) { fwrite (f, 4, n, s_pcm); s_pcmFrames += n; s_queued += n; return (int) n; }
int kapi_sound_instrument (int v, const struct kapi_fm_instrument *i) { (void) v; (void) i; s_instr++; return 0; }
int kapi_sound_start (int v, unsigned mhz, int wave, int vol) { (void) mhz; (void) wave; (void) vol; s_notes++; if (v > s_maxVoice) s_maxVoice = v; return 0; }
int kapi_sound_stop (int v) { (void) v; return 0; }

void DG_DrawFrame (void)
{
	s_frames++;
	if (s_us / 1000000.0 >= s_end)
	{
		FILE *o = fopen ("doom_last.ppm", "wb");
		fprintf (o, "P6\n%d %d\n255\n", DOOMGENERIC_RESX, DOOMGENERIC_RESY);
		for (int i = 0; i < DOOMGENERIC_RESX * DOOMGENERIC_RESY; i++) { unsigned c = DG_ScreenBuffer[i]; fputc (c >> 16, o); fputc (c >> 8, o); fputc (c, o); }
		fclose (o);
		fclose (s_pcm);
		printf ("frames %d in %.1f s of game time, sound %ld frames (%.1f s), music: %ld notes, %ld patches, voices up to %d\n",
			s_frames, s_us / 1e6, s_pcmFrames, s_pcmFrames / 44100.0, s_notes, s_instr, s_maxVoice);
		exit (0);
	}
	s_us += 1000;						// (drawing takes a little time)
}

int DG_GetKey (int *pressed, unsigned char *key)
{
	static char done[64]; static char up[64];
	double t = s_us / 1e6; int i = 0;
	for (const char *k = s_keys; *k; i++)
	{
		double at = atof (k); const char *c = strchr (k, ':'); if (!c) break;
		int code = atoi (c + 1);
		if (i < 64 && !done[i] && t >= at) { done[i] = 1; *pressed = 1; *key = (unsigned char) code; return 1; }
		if (i < 64 && done[i] && !up[i] && t >= at + 0.2) { up[i] = 1; *pressed = 0; *key = (unsigned char) code; return 1; }
		const char *nx = strchr (c, ','); if (!nx) break; k = nx + 1;
	}
	return 0;
}

int main (int argc, char **argv)
{
	if (argc < 3) { fprintf (stderr, "usage: host_doom <iwad> <seconds> [keys]\n"); return 1; }
	s_end = atof (argv[2]); if (argc > 3) s_keys = argv[3];
	s_pcm = fopen ("doom_sfx.raw", "wb");
	char *av[] = { "doom", "-mb", "32", "-iwad", argv[1], 0 };
	doomgeneric_Create (5, av);
	for (;;) doomgeneric_Tick ();
}
