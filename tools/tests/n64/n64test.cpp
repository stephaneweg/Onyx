//
// n64test -- the Onyx N64 core (user/n64) on the PC.
//   n64test <rom> <frames> [out.ppm]
//       runs a ROM without a screen and saves the last picture (the VI's framebuffer).
//   N64_TRACE=n: the last n instructions (pc) when it stops
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define private public
#include "n64/n64.h"
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

static void ppm (n64::Machine *m, const char *path)
{
	FILE *f = fopen (path, "wb");
	if (!f) return;
	fprintf (f, "P6\n%d %d\n255\n", m->fbW, m->fbH);
	for (int i = 0; i < m->fbW * m->fbH; i++) { unsigned c = m->fb[i]; fputc (c >> 16, f); fputc ((c >> 8) & 255, f); fputc (c & 255, f); }
	fclose (f);
}

int main (int argc, char **argv)
{
	if (argc < 3) { fprintf (stderr, "n64test <rom> <frames> [out.ppm]\n"); return 1; }
	long n;
	unsigned char *d = slurp (argv[1], &n);
	n64::Machine *m = new n64::Machine;
	if (!m->load (d, (unsigned) n)) { printf ("load failed\n"); return 1; }
	int frames = atoi (argv[2]);
	clock_t t0 = clock ();
	for (int i = 0; i < frames && !m->halted; i++)
	{
#ifdef N64_TRACE
		m->traceOn = i == frames - 1;
#endif
		m->runFrame ();
	}
	double s = (double) (clock () - t0) / CLOCKS_PER_SEC;
	printf ("%s: CIC %u, %s, %d frames, %.2f s (%.1f fps), pc %08X, %dx%d%s%s\n", m->title, m->cic, m->pal ? "PAL" : "NTSC",
		frames, s, frames / (s > 0 ? s : 1), m->pc, m->fbW, m->fbH, m->halted ? " HALTED: " : "", m->haltMsg);
	if (getenv ("N64_STATE"))
	{
		printf ("  status %08llX cause %08llX epc %08llX count %08X compare %08llX\n", m->cp0[12], m->cp0[13], m->cp0[14], m->count (), m->cp0[11]);
		printf ("  MI intr %02X mask %02X   VI ctrl %08X origin %08X width %u intr %u sync %u  SP status %08X  DP %08X %08X\n",
			m->mi[2], m->mi[3], m->vi[0], m->vi[1], m->vi[2], m->vi[3], m->vi[6], m->sp[4], m->dp[0], m->dp[1]);
		printf ("  AI len %u  PI status %X  SI status %X  RSP tasks: gfx %u audio %u other %u\n", m->ai[1], m->pi[4], m->si[6],
			m->rspTasks[1], m->rspTasks[2], m->rspTasks[0] + m->rspTasks[3] + m->rspTasks[4] + m->rspTasks[5] + m->rspTasks[6] + m->rspTasks[7]);
		printf ("  IRQs: SP %u SI %u AI %u VI %u PI %u DP %u   last task type %08X, sp pc %X\n", m->irqCount[0], m->irqCount[1], m->irqCount[2], m->irqCount[3], m->irqCount[4], m->irqCount[5], m->lastTask, m->spPc);
		for (int i = 0; i < 16; i++) printf ("  dmem[%03X] %08X%s", 0xFC0 + 4 * i, m->dmem[(0xFC0 >> 2) + i], (i & 3) == 3 ? "\n" : "");
		for (int i = 0; i < 32; i++) printf ("  r%-2d %016llX%s", i, m->r[i], (i & 3) == 3 ? "\n" : "");
		n64::u64 v; for (int k = -4; k < 8; k++) { if (m->rd32 (m->pc + 4 * k, v)) printf ("  %08X: %08llX\n", m->pc + 4 * k, v & 0xFFFFFFFF); }
	}
#ifdef N64_TRACE
	{	// the last frame's instructions, the idle loop and repeats left out
		unsigned long long n = m->traceN < 4096 ? m->traceN : 4096, start = m->traceN - n;
		unsigned last = 0; int shown = 0;
		for (unsigned long long i = start; i < m->traceN && shown < 3000; i++)
		{
			unsigned pc = m->traceBuf[i % 4096];
			if (pc == 0x800007C0 || pc == 0x800007C4) continue;
			if (pc == last + 4 || pc == last) { last = pc; continue; }
			printf ("  -> %08X\n", pc); shown++; last = pc;
		}
	}
#endif
	if (getenv ("N64_THREADS"))
	{
		printf ("  exceptions:"); for (int i = 0; i < 32; i++) if (m->excCount[i]) printf (" %d:%u", i, m->excCount[i]);
		printf ("  last (not an interrupt): code %d at %08X, badvaddr %08llX\n", m->lastExcCode, m->lastExcPc, m->cp0[8]);
	}
	if (getenv ("N64_THREADS"))		// libultra's OSThread structures found in RDRAM
	{
		auto W = [&] (unsigned a) { return m->rdram[(a & 0x7FFFFF) >> 2]; };
		for (unsigned a = 0x1000; a < 0x7FF000; a += 8)
		{
			unsigned st = W (a + 0x10) >> 16, id = W (a + 0x14), pri = W (a + 4), pc = W (a + 0x11C), ra = W (a + 0x104), sp = W (a + 0xF4);
			if (!(st == 1 || st == 2 || st == 4 || st == 8) || id > 100 || pri > 255) continue;
			if ((pc >> 24) != 0x80) continue;
			unsigned tl = W (a + 0x0C);
			(void) tl; (void) ra; (void) sp;
			printf ("  thread @%08X id %u pri %u state %u pc %08X ra %08X sp %08X a0 %08X\n", 0x80000000 | a, id, pri, st, pc, ra, sp, W (a + 0x3C));
			if (st == 8 && (sp >> 24) == 0x80)
			{
				printf ("     stack:");
				for (unsigned k = 0; k < 64; k++) { unsigned w = W (sp + 4 * k); if ((w >> 20) >= 0x800 && (w >> 20) <= 0x802 && (w & 3) == 0) printf (" %08X", w); }
				printf ("\n");
			}
		}
	}
	if (getenv ("N64_WHO"))			// the words that point to a given address
	{
		unsigned t = (unsigned) strtoul (getenv ("N64_WHO"), 0, 16);
		for (unsigned a = 0; a < 0x800000; a += 4) if (m->rdram[a >> 2] == t) printf ("  %08X -> %08X  (%08X %08X %08X %08X %08X %08X)\n", 0x80000000 | a, t,
			m->rdram[a >> 2], m->rdram[(a >> 2) + 1], m->rdram[(a >> 2) + 2], m->rdram[(a >> 2) + 3], m->rdram[(a >> 2) + 4], m->rdram[(a >> 2) + 5]);
	}
	if (getenv ("N64_MEM"))
	{
		unsigned a = (unsigned) strtoul (getenv ("N64_MEM"), 0, 16);
		for (int k = 0; k < 16; k++) printf ("  %08X: %08X %08X %08X %08X\n", a + 16 * k, m->rdram[((a & 0x7FFFFF) >> 2) + 4 * k], m->rdram[((a & 0x7FFFFF) >> 2) + 4 * k + 1], m->rdram[((a & 0x7FFFFF) >> 2) + 4 * k + 2], m->rdram[((a & 0x7FFFFF) >> 2) + 4 * k + 3]);
	}
	if (argc > 3) ppm (m, argv[3]);
	return 0;
}
