//
// snestest -- the Onyx SNES core (user/snes) on the PC.
//   snestest <rom> <seconds> [out.ppm] [keys]
//       runs a ROM (a test ROM, a game) without a screen and saves the last frame;
//       keys: "t:mask,..." -- at t seconds, press the buttons of mask (snes.h BTN_*, hex) 0.2 s
//   SNES_AUDIO=file: the sound too (raw s16 stereo 32000 Hz); SNES_SHOTS=n: a frame every n
//   seconds (out.ppm -> out_<t>.ppm); SNES_TRACE=n: the CPU's last n instructions at the end
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define private public
#include "snes/snes.h"
#undef private

static unsigned char *slurp (const char *path, long *n)
{
	FILE *f = fopen (path, "rb");
	if (!f) { perror (path); exit (1); }
	fseek (f, 0, SEEK_END); *n = ftell (f); fseek (f, 0, SEEK_SET);
	unsigned char *d = (unsigned char *) malloc (*n + 1);
	if (fread (d, 1, *n, f) != (size_t) *n) exit (1);
	fclose (f);
	return d;
}

static void ppm (snes::Machine *m, const char *path)
{
	FILE *f = fopen (path, "wb");
	if (!f) return;
	fprintf (f, "P6\n256 %d\n255\n", m->height);
	for (int i = 0; i < 256 * m->height; i++) { unsigned c = m->fb[i]; fputc (c >> 16, f); fputc ((c >> 8) & 255, f); fputc (c & 255, f); }
	fclose (f);
}

int main (int argc, char **argv)
{
	if (argc < 3) { fprintf (stderr, "snestest <rom> <seconds> [out.ppm] [keys]\n"); return 1; }
	long n;
	unsigned char *d = slurp (argv[1], &n);
	snes::Machine *m = new snes::Machine;
	if (!m->load (d, (int) n)) { printf ("load failed\n"); return 1; }
	m->setAudioRate (32000);
	int fps = m->pal ? 50 : 60;
	int frames = (int) (atof (argv[2]) * fps);
	const char *keys = argc > 4 ? argv[4] : "";
	FILE *au = getenv ("SNES_AUDIO") ? fopen (getenv ("SNES_AUDIO"), "wb") : 0;
	int shots = getenv ("SNES_SHOTS") ? atoi (getenv ("SNES_SHOTS")) : 0;
	clock_t t0 = clock ();
	for (int f = 0; f < frames; f++)
	{
		double t = (double) f / fps;
		int mask = 0;
		for (const char *k = keys; *k; )
		{
			double kt = atof (k); const char *c = strchr (k, ':'); if (!c) break;
			int km = (int) strtol (c + 1, 0, 16);
			if (t >= kt && t < kt + 0.2) mask |= km;
			const char *e = strchr (c, ','); if (!e) break; k = e + 1;
		}
		m->setButtons (mask);
		m->runFrame ();
		short pcm[4096 * 2];
		int k = m->audioRead (pcm, 4096);
		if (au && k > 0) fwrite (pcm, 4, k, au);
		if (shots && argc > 3 && f % (shots * fps) == 0 && f)
		{
			char p[512]; snprintf (p, sizeof p, "%.*s_%03d.ppm", (int) strlen (argv[3]) - 4, argv[3], f / fps);
			ppm (m, p);
		}
	}
	double el = (double) (clock () - t0) / CLOCKS_PER_SEC;
	if (au) fclose (au);
	if (argc > 3) ppm (m, argv[3]);
	printf ("\"%s\" %s %s, %d frames in %.2f s (PC=%02X:%04X)\n", m->title, m->hirom ? "HiROM" : "LoROM", m->pal ? "PAL" : "NTSC", frames, el, m->PB, m->PC);
	return 0;
}
