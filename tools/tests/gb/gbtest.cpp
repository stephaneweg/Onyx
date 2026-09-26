#include <cstdlib>
//
// gbtest -- run a Game Boy ROM on the PC with the Onyx core (user/gb):
//   gbtest <rom> <seconds> [out.ppm] [keys]
// prints what the ROM sent on the serial port (the test ROMs report there), saves the last
// frame (GB_AUDIO=file: the sound too, raw s16 stereo 44100 Hz). keys: "t:mask,t:mask,..." -- at t seconds, press the buttons of mask (gb.h BTN_*).
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gb/gb.h"

int main (int argc, char **argv)
{
	if (argc < 3) { fprintf (stderr, "usage: gbtest <rom> <seconds> [out.ppm] [keys]\n"); return 1; }
	FILE *f = fopen (argv[1], "rb");
	if (!f) { perror (argv[1]); return 1; }
	fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
	unsigned char *rom = (unsigned char *) malloc (n);
	if (fread (rom, 1, n, f) != (size_t) n) return 1;
	fclose (f);
	gb::Machine *m = new gb::Machine;
	if (!m->load (rom, (int) n)) { fprintf (stderr, "not a ROM\n"); return 1; }
	static const unsigned GREY[4] = { 0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000 };
	if (getenv ("GB_GREY")) m->setDmgPalette (GREY);	// the DMG reference pictures' shades
	double secs = atof (argv[2]);
	int frames = (int) (secs * 59.73);
	const char *keys = argc > 4 ? argv[4] : "";
	short tmp[4096];
	FILE *au = getenv ("GB_AUDIO") ? fopen (getenv ("GB_AUDIO"), "wb") : 0;	// raw s16 stereo, 44100 Hz
	m->setAudioRate (44100);
	for (int i = 0; i < frames; i++)
	{
		double t = i / 59.73; int mask = 0;
		for (const char *k = keys; *k; )
		{
			double at = atof (k); const char *c = strchr (k, ':'); if (!c) break;
			int mk = atoi (c + 1);
			if (t >= at && t < at + 0.15) mask |= mk;
			const char *nx = strchr (c, ','); if (!nx) break; k = nx + 1;
		}
		m->setButtons (mask);
		m->runFrame ();
		int na = m->audioRead (tmp, 2048);
		if (au && na > 0) fwrite (tmp, 4, (size_t) na, au);
	}
	if (au) fclose (au);
	printf ("title \"%s\" cgb %d\n", m->title, m->cgb ? 1 : 0);
	if (m->serialLen) printf ("serial:\n%s\n", m->serial);
	if (argc > 3)
	{
		FILE *o = fopen (argv[3], "wb");
		fprintf (o, "P6\n%d %d\n255\n", gb::W, gb::H);
		for (int i = 0; i < gb::W * gb::H; i++) { unsigned c = m->fb[i]; fputc (c >> 16, o); fputc (c >> 8, o); fputc (c, o); }
		fclose (o);
	}
	return 0;
}
