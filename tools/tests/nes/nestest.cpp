//
// nestest -- the Onyx NES core (user/nes) on the PC.
//   nestest cpu <nestest.nes> <nestest.log>   the CPU against nestest's reference trace
//                                             (automation mode at $C000: registers + cycles)
//   nestest run <rom> <seconds> [out.ppm] [keys]
//       runs a ROM; blargg's test ROMs report through $6000 (status) / $6004 (text): printed;
//       keys: "t:mask,..." -- at t seconds, press the buttons of mask (nes.h BTN_*) 0.15 s
//   NES_AUDIO=file: the sound too (raw s16 stereo 44100 Hz); NES_PAL=1: PAL timing
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define private public
#include "nes/nes.h"
#undef private

static unsigned char *slurp (const char *path, long *n)
{
	FILE *f = fopen (path, "rb");
	if (!f) { perror (path); exit (1); }
	fseek (f, 0, SEEK_END); *n = ftell (f); fseek (f, 0, SEEK_SET);
	unsigned char *d = (unsigned char *) malloc (*n + 1);
	if (fread (d, 1, *n, f) != (size_t) *n) exit (1);
	fclose (f); d[*n] = 0;
	return d;
}

static int cpu_test (const char *rom, const char *logPath)
{
	long n, ln;
	unsigned char *d = slurp (rom, &n);
	char *log = (char *) slurp (logPath, &ln);
	nes::Machine *m = new nes::Machine;
	if (!m->load (d, (int) n)) { printf ("load failed\n"); return 1; }
	m->pc = 0xC000; m->cycles = 7; m->p = 0x24; m->s = 0xFD;
	int lineNo = 0, bad = 0;
	for (char *l = log; *l && bad < 5; )
	{
		char *e = strchr (l, '\n'); if (e) *e = 0;
		unsigned pc, a, x, y, p, sp; unsigned long long cyc;
		char *pa = strstr (l, "A:"), *pcy = strstr (l, "CYC:");
		if (pa && pcy && sscanf (l, "%x", &pc) == 1 && sscanf (pa, "A:%x X:%x Y:%x P:%x SP:%x", &a, &x, &y, &p, &sp) == 5 && sscanf (pcy, "CYC:%llu", &cyc) == 1)
		{
			lineNo++;
			if (m->pc != pc || m->a != a || m->x != x || m->y != y || m->p != p || m->s != sp || m->cycles != cyc)
			{
				printf ("line %d: want %04X A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%llu\n", lineNo, pc, a, x, y, p, sp, cyc);
				printf ("         got %04X A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%llu\n", m->pc, m->a, m->x, m->y, m->p, m->s, (unsigned long long) m->cycles);
				bad++;
				m->pc = (unsigned short) pc; m->a = (unsigned char) a; m->x = (unsigned char) x; m->y = (unsigned char) y; m->p = (unsigned char) p; m->s = (unsigned char) sp; m->cycles = cyc;
			}
			m->cycles += (unsigned long long) m->step ();
		}
		if (!e) break;
		l = e + 1;
	}
	printf ("nestest: %d instructions, %s (result codes $02=%02X $03=%02X)\n", lineNo, bad ? "MISMATCH" : "all match", m->ram[2], m->ram[3]);
	return bad ? 1 : 0;
}

int main (int argc, char **argv)
{
	if (argc >= 4 && !strcmp (argv[1], "cpu")) return cpu_test (argv[2], argv[3]);
	if (argc < 4 || strcmp (argv[1], "run")) { fprintf (stderr, "usage: nestest cpu <rom> <log> | nestest run <rom> <seconds> [out.ppm] [keys]\n"); return 1; }
	long n;
	unsigned char *d = slurp (argv[2], &n);
	nes::Machine *m = new nes::Machine;
	if (!m->load (d, (int) n)) { printf ("load failed (mapper?)\n"); return 1; }
	if (getenv ("NES_PAL")) m->setPal (true);
	m->setAudioRate (44100);
	FILE *au = getenv ("NES_AUDIO") ? fopen (getenv ("NES_AUDIO"), "wb") : 0;
	double secs = atof (argv[3]);
	int fps = m->pal ? 50 : 60, frames = (int) (secs * fps);
	const char *keys = argc > 5 ? argv[5] : "";
	static short pcm[16384];
	clock_t t0 = clock ();
	int resetAt = -1;
	for (int f = 0; f < frames; f++)
	{
		int b = 0;
		for (const char *k = keys; *k; )
		{
			double t = atof (k); const char *c = strchr (k, ':'); if (!c) break;
			int mask = atoi (c + 1);
			if (f >= (int) (t * fps) && f < (int) (t * fps) + fps * 15 / 100) b |= mask;
			const char *nx = strchr (k, ','); if (!nx) break; k = nx + 1;
		}
		m->setButtons (b);
		m->runFrame ();
		int na = m->audioRead (pcm, 8192);
		if (au) fwrite (pcm, 4, (size_t) na, au);
		// blargg: $6000 = 0x81 asks for a reset (soon after)
		if (m->sram[1] == 0xDE && m->sram[2] == 0xB0 && m->sram[3] == 0x61)
		{
			if (m->sram[0] == 0x81 && resetAt < 0) resetAt = f + 6;
			if (f == resetAt) { m->pc = m->read16 (0xFFFC); m->s -= 3; m->p |= 4; resetAt = -2; }
			if (m->sram[0] < 0x80 && f > 10) break;			// done
		}
	}
	double el = (double) (clock () - t0) / CLOCKS_PER_SEC;
	if (m->sram[1] == 0xDE && m->sram[2] == 0xB0 && m->sram[3] == 0x61)
	{
		char text[1024]; int k = 0;
		for (int i = 4; i < 0x1000 && m->sram[i] && k < 1000; i++) text[k++] = (char) m->sram[i];
		text[k] = 0;
		printf ("status %02X: %s\n", m->sram[0], text);
	}
	printf ("mapper %d%s, %d frames in %.2f s\n", m->mapper, m->pal ? " PAL" : "", frames, el);
	if (argc > 4 && argv[4][0])
	{
		FILE *o = fopen (argv[4], "wb");
		fprintf (o, "P6 256 240 255\n");
		for (int i = 0; i < 256 * 240; i++) { unsigned c = m->fb[i]; fputc (c >> 16, o); fputc ((c >> 8) & 255, o); fputc (c & 255, o); }
		fclose (o);
	}
	if (au) fclose (au);
	return m->sram[0] && m->sram[1] == 0xDE && m->sram[0] < 0x80 ? 1 : 0;
}
