//
// ndstest.cpp -- the Nintendo DS core (user/Emulators/nds) on the PC: runs a ROM some frames and
// writes the two screens (one above the other) as a PPM; prints the speed and where the processors
// are. tools/tests/run_nds_test.sh builds it.
//
//   ndstest <rom.nds> <frames> [out.ppm]
//   NDS_KEYS="f0-f1:hex;..."   buttons held from frame f0 to f1 (nds.h's BTN_* mask)
//   NDS_TOUCH="f0-f1:x,y;..."  the stylus down on the bottom screen
//   NDS_SAV=<file>             the save, read before and written after
//   NDS_JIT=1                  the JIT (on an AArch64 host only)
//   NDS_TRACE=1                the two PCs every frame
//   NDS_WAV=<file>             the sound, 16-bit stereo 48 kHz
//   NDS_SHOTS=<prefix>         a picture every NDS_SHOTEVERY frames (default 60)
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "nds/nds.h"
#ifdef NDS_DEBUG
namespace nds { extern u32 g_watch[2][2]; extern bool g_watchHit; }
#endif
#if defined(__aarch64__)
namespace nds { extern unsigned g_jitNoKinds, g_jitNoDP; }
#endif
#if defined(__aarch64__) || defined(__x86_64__)
#include <sys/mman.h>
#endif

static void *hostCode (unsigned size)
{
#if defined(__aarch64__)
	void *p = mmap (0, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? 0 : p;
#else
	(void) size; return 0;
#endif
}

static void writePpm (const char *path, nds::Machine &m)
{
	FILE *f = fopen (path, "wb");
	if (!f) return;
	fprintf (f, "P6\n256 384\n255\n");
	for (int s = 0; s < 2; s++)
		for (int i = 0; i < 256 * 192; i++)
		{
			unsigned c = m.screen[s][i];
			unsigned char px[3] = { (unsigned char) (c >> 16), (unsigned char) (c >> 8), (unsigned char) c };
			fwrite (px, 1, 3, f);
		}
	fclose (f);
}

struct Range { int f0, f1; unsigned v; int x, y; };
static int parseRanges (const char *s, Range *out, int max, bool touch)
{
	int n = 0;
	while (s && *s && n < max)
	{
		Range r = { 0, 0, 0, 0, 0 };
		r.f0 = (int) strtol (s, (char **) &s, 10);
		if (*s == '-') { s++; r.f1 = (int) strtol (s, (char **) &s, 10); } else r.f1 = r.f0;
		if (*s == ':') s++;
		if (touch) { r.x = (int) strtol (s, (char **) &s, 10); if (*s == ',') s++; r.y = (int) strtol (s, (char **) &s, 10); }
		else r.v = (unsigned) strtoul (s, (char **) &s, 16);
		out[n++] = r;
		while (*s == ';' || *s == ' ') s++;
	}
	return n;
}

int main (int argc, char **argv)
{
	if (argc < 3) { fprintf (stderr, "ndstest <rom.nds> <frames> [out.ppm]\n"); return 2; }
	FILE *f = fopen (argv[1], "rb");
	if (!f) { perror (argv[1]); return 1; }
	fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
	unsigned char *rom = (unsigned char *) malloc ((size_t) n);
	if (fread (rom, 1, (size_t) n, f) != (size_t) n) { fclose (f); return 1; }
	fclose (f);
	nds::Machine *m = new nds::Machine;
	const char *b7 = getenv ("NDS_BIOS7");
	if (b7) { FILE *bf = fopen (b7, "rb"); if (bf) { static unsigned char b[0x4000]; size_t k = fread (b, 1, sizeof b, bf); fclose (bf); m->setBios7 (b, (unsigned) k); } }
	if (!m->load (rom, (unsigned) n)) { fprintf (stderr, "not a DS ROM\n"); return 1; }
	printf ("%s [%s]\n", m->title, m->code);
	const char *sav = getenv ("NDS_SAV");
	if (sav)
	{
		FILE *sf = fopen (sav, "rb");
		if (sf) { fseek (sf, 0, SEEK_END); long k = ftell (sf); fseek (sf, 0, SEEK_SET); unsigned char *b = (unsigned char *) malloc ((size_t) k); if (fread (b, 1, (size_t) k, sf) == (size_t) k) m->setSaveData (b, (unsigned) k); fclose (sf); free (b); }
	}
#if defined(__aarch64__)
	if (getenv ("NDS_JITNODP")) nds::g_jitNoDP = (unsigned) strtoul (getenv ("NDS_JITNODP"), 0, 16);
	if (getenv ("NDS_JITNO")) nds::g_jitNoKinds = (unsigned) strtoul (getenv ("NDS_JITNO"), 0, 16);
#endif
	if (getenv ("NDS_JIT") && atoi (getenv ("NDS_JIT"))) printf ("JIT: %s\n", m->jitEnable (hostCode) ? "on" : "unavailable");
	m->setAudioRate (48000);
	Range keys[64], touch[64];
	int nk = parseRanges (getenv ("NDS_KEYS"), keys, 64, false), nt = parseRanges (getenv ("NDS_TOUCH"), touch, 64, true);
	int frames = atoi (argv[2]);
#ifdef NDS_DEBUG
	if (getenv ("NDS_WATCH9")) { char *e; nds::g_watch[0][0] = (unsigned) strtoul (getenv ("NDS_WATCH9"), &e, 16); nds::g_watch[0][1] = (unsigned) strtoul (e + 1, 0, 16); }
	if (getenv ("NDS_WATCH7")) { char *e; nds::g_watch[1][0] = (unsigned) strtoul (getenv ("NDS_WATCH7"), &e, 16); nds::g_watch[1][1] = (unsigned) strtoul (e + 1, 0, 16); }
#endif
	bool trace = getenv ("NDS_TRACE") != 0;
	const char *wavPath = getenv ("NDS_WAV");
	FILE *wav = wavPath ? fopen (wavPath, "wb") : 0;
	unsigned wavBytes = 0;
	if (wav) { unsigned char h[44] = { 0 }; fwrite (h, 1, 44, wav); }
	const char *shots = getenv ("NDS_SHOTS");
	int shotEvery = getenv ("NDS_SHOTEVERY") ? atoi (getenv ("NDS_SHOTEVERY")) : 60;
	clock_t t0 = clock ();
	static short pcm[8192 * 2];
	for (int i = 0; i < frames; i++)
	{
		unsigned b = 0;
		for (int k = 0; k < nk; k++) if (i >= keys[k].f0 && i <= keys[k].f1) b |= keys[k].v;
		m->setButtons ((int) b);
		bool down = false; int tx = 0, ty = 0;
		for (int k = 0; k < nt; k++) if (i >= touch[k].f0 && i <= touch[k].f1) { down = true; tx = touch[k].x; ty = touch[k].y; }
		m->setTouch (down, tx, ty);
		m->runFrame ();
		int got = m->audioRead (pcm, 8192);
		if (wav && got > 0) { fwrite (pcm, 4, (size_t) got, wav); wavBytes += (unsigned) got * 4; }
#ifdef NDS_DEBUG
		if (nds::g_watchHit) for (int c = 0; c < 2; c++)
		{
			nds::Arm &a = c ? m->arm7 : m->arm9;
			printf ("cpu %d pc=%08x; the last ones:\n", c, a.r[15]);
			int nh = getenv ("NDS_NHIST") ? atoi (getenv ("NDS_NHIST")) : 64;
			for (int k = 0; k < nh; k++) printf ("%08x%c", a.hist[(a.histN - (unsigned) nh + (unsigned) k) & 4095], k % 8 == 7 ? '\n' : ' ');
			if (c == 1) return 1;
		}
#endif
		if (trace) printf ("f%d arm9 pc=%08x cpsr=%08x %s  arm7 pc=%08x cpsr=%08x %s\n", i, m->arm9.r[15], m->arm9.cpsr, m->arm9.halted ? "H" : "-", m->arm7.r[15], m->arm7.cpsr, m->arm7.halted ? "H" : "-");
		if (shots && i % shotEvery == shotEvery - 1) { char p[512]; snprintf (p, sizeof p, "%s%05d.ppm", shots, i + 1); writePpm (p, *m); }
	}
	double secs = (double) (clock () - t0) / CLOCKS_PER_SEC;
	printf ("%d frames in %.2f s: %.1f fps (%.0f%% of a DS)\n", frames, secs, frames / (secs > 0 ? secs : 1), frames / (secs > 0 ? secs : 1) / 59.8261 * 100);
	printf ("arm9 pc=%08x cpsr=%08x  arm7 pc=%08x cpsr=%08x\n", m->arm9.r[15], m->arm9.cpsr, m->arm7.r[15], m->arm7.cpsr);
	printf ("3D polygons: %d  save: type %d, %u bytes\n", m->gpu3d.nPolys[m->gpu3d.rdSet], m->saveType (), m->saveSize);
	if (m->lastError[0]) printf ("note: %s\n", m->lastError);
	if (argc > 3) writePpm (argv[3], *m);
	if (getenv ("NDS_DUMP"))					// NDS_DUMP=<address>,<words>: main memory
	{
		char *e; unsigned a = (unsigned) strtoul (getenv ("NDS_DUMP"), &e, 16), k = *e == ',' ? (unsigned) atoi (e + 1) : 16;
		for (unsigned i = 0; i < k; i++) printf ("%08x%c", nds::rd32 (m->mainRam + ((a + i * 4) & 0x3FFFFF)), (i % 8 == 7) ? '\n' : ' ');
		printf ("\n");
	}
	if (wav)
	{
		unsigned char h[44] = { 'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 2,0, 0x80,0xBB,0,0, 0,0xEE,2,0, 4,0, 16,0, 'd','a','t','a', 0,0,0,0 };
		unsigned rl = wavBytes + 36; memcpy (h + 4, &rl, 4); memcpy (h + 40, &wavBytes, 4);
		fseek (wav, 0, SEEK_SET); fwrite (h, 1, 44, wav); fclose (wav);
	}
	if (sav && m->save && m->saveSize) { FILE *sf = fopen (sav, "wb"); if (sf) { fwrite (m->save, 1, m->saveSize, sf); fclose (sf); } }
	return 0;
}
