//
// gbatest -- run a Game Boy Advance ROM on the PC with the Onyx core (user/gba):
//   gbatest <rom> <seconds> [out.ppm] [keys]
// prints the title / save type and how fast it ran, saves the last frame (GBA_AUDIO=file:
// the sound too, raw s16 stereo 32768 Hz; GBA_SAVE=file: the save memory at the end; GBA_LOAD=file: a save to start with;
// GBA_SHOTS="t,t,...": shot_<frame>.ppm at those times).
// keys: "t:mask,t:mask,..." -- at t seconds, press the buttons of mask (gba.h BTN_*) 0.15 s.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gba/gba.h"

#ifdef GBA_DEBUG
namespace gba { extern unsigned g_swiCount[256]; extern u32 g_watchAddr; extern void (*g_watchHit) (u32, u32, const u32 *, int); }
static int s_hits = 0;
static void hit (unsigned a, unsigned v, const unsigned *r, int n)
{
	if (s_hits++ >= 400) return;
	printf ("write%d %08x = %08x  pc %08x  lr %08x", n * 8, a, v, r[15], r[14]);
	if (getenv ("GBA_WATCHREGS")) for (int i = 0; i < 13; i++) printf (" r%d=%x", i, r[i]);
	printf ("\n");
}
#endif

int main (int argc, char **argv)
{
	if (argc < 3) { fprintf (stderr, "usage: gbatest <rom> <seconds> [out.ppm] [keys]\n"); return 1; }
	FILE *f = fopen (argv[1], "rb");
	if (!f) { perror (argv[1]); return 1; }
	fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
	unsigned char *rom = (unsigned char *) malloc (n);
	if (fread (rom, 1, n, f) != (size_t) n) return 1;
	fclose (f);
	gba::Machine *m = new gba::Machine;
	if (!m->load (rom, (int) n)) { fprintf (stderr, "not a ROM\n"); return 1; }
	m->setAudioRate (32768);
	if (getenv ("GBA_LOAD")) { FILE *sf = fopen (getenv ("GBA_LOAD"), "rb"); if (sf) { static unsigned char sb[0x20000]; int sn = (int) fread (sb, 1, sizeof sb, sf); fclose (sf); m->setSaveRam (sb, sn); } }
#ifdef GBA_DEBUG
	if (getenv ("GBA_WATCH")) { gba::g_watchAddr = (unsigned) strtoul (getenv ("GBA_WATCH"), 0, 16) & ~3u; gba::g_watchHit = hit; }
#endif
	FILE *au = getenv ("GBA_AUDIO") ? fopen (getenv ("GBA_AUDIO"), "wb") : 0;
	double secs = atof (argv[2]);
	int frames = (int) (secs * 59.73);
	const char *keys = argc > 4 ? argv[4] : "";
	short tmp[8192];
	clock_t c0 = clock ();
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
		if (getenv ("GBA_SHOTS"))				// GBA_SHOTS="5,10.5,...": shot_<frame>.ppm at those times
			for (const char *k = getenv ("GBA_SHOTS"); *k; )
			{
				if ((int) (atof (k) * 59.73) == i)
				{
					char nm[64]; snprintf (nm, sizeof nm, "shot_%05d.ppm", i);
					FILE *o = fopen (nm, "wb");
					fprintf (o, "P6\n%d %d\n255\n", gba::W, gba::H);
					for (int p = 0; p < gba::W * gba::H; p++) { unsigned c = m->fb[p]; fputc (c >> 16, o); fputc (c >> 8, o); fputc (c, o); }
					fclose (o);
				}
				const char *nx = strchr (k, ','); if (!nx) break; k = nx + 1;
			}
		if (getenv ("GBA_DUMP") && atoi (getenv ("GBA_DUMP")) == i)	// GBA_DUMP="frame,addr,len" -> dump.bin
		{
			const char *d = strchr (getenv ("GBA_DUMP"), ',');
			unsigned a = (unsigned) strtoul (d + 1, 0, 16), len = (unsigned) strtoul (strchr (d + 1, ',') + 1, 0, 16);
			FILE *o = fopen ("dump.bin", "wb");
			for (unsigned j = 0; j < len; j++) fputc (m->debugRead8 (a + j), o);
			fclose (o);
		}
		int na = m->audioRead (tmp, 4096);
		if (au && na > 0) fwrite (tmp, 4, (size_t) na, au);
	}
	double el = (double) (clock () - c0) / CLOCKS_PER_SEC;
	if (au) fclose (au);
	printf ("title \"%s\" code %s save %d (%d bytes)  %d frames in %.2f s (%.0f fps)\n", m->title, m->code, m->saveType, m->saveSize, frames, el, frames / (el > 0 ? el : 1));
	if (argc > 3 && argv[3][0])
	{
		FILE *o = fopen (argv[3], "wb");
		fprintf (o, "P6\n%d %d\n255\n", gba::W, gba::H);
		for (int i = 0; i < gba::W * gba::H; i++) { unsigned c = m->fb[i]; fputc (c >> 16, o); fputc (c >> 8, o); fputc (c, o); }
		fclose (o);
	}
#ifdef GBA_DEBUG
	for (int i = 0; i < 256; i++) if (gba::g_swiCount[i]) printf ("swi %02x: %u\n", i, gba::g_swiCount[i]);
#endif
	if (getenv ("GBA_REGS")) { unsigned rr[16], ps; m->debugRegs (rr, &ps); for (int i = 0; i < 16; i++) printf ("r%d=%08x%s", i, rr[i], i % 8 == 7 ? "\n" : " "); printf ("cpsr=%08x\n", ps); }
	if (getenv ("GBA_SAVE") && m->saveSize) { FILE *s = fopen (getenv ("GBA_SAVE"), "wb"); fwrite (m->save, 1, m->saveSize, s); fclose (s); }
	return 0;
}
