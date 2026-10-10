//
// n3dstest.cpp -- the 3DS core (user/Emulators/n3ds) run without a screen: loads a program, runs it frame after
// frame until it ends, prints what it says (svcOutputDebugString) and how it ended. Built for AArch64 with
// Onyx's toolchain and run under qemu-aarch64 (tools/tests/run_n3ds_test.sh): the JIT is the real one.
//   n3dstest <program.elf> [frames [picture.ppm]]		(600 frames at most by default)
// The exit status is 0 when the program ended by itself without an error of the emulator. The picture is the two
// screens as they are at the end, the bottom one under the top one; its checksum is printed ("screens <crc>").
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "n3ds/n3ds.h"

static void debugOut (void *, const char *text, n3ds::u32 len) { fwrite (text, 1, len, stdout); }

static unsigned crc32 (const unsigned char *p, size_t n)
{
	unsigned c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1))); }
	return ~c;
}

// Both screens in one picture of TOP_W x 2 SCREEN_H (the bottom screen centred), 3 bytes a pixel.
static unsigned char *screens (const n3ds::Machine *m)
{
	using namespace n3ds;
	static u32 top[TOP_W * SCREEN_H], bottom[BOTTOM_W * SCREEN_H];
	static unsigned char rgb[TOP_W * SCREEN_H * 2 * 3];
	m->screenImage (SCREEN_TOP, top); m->screenImage (SCREEN_BOTTOM, bottom);
	memset (rgb, 0, sizeof rgb);
	for (int y = 0; y < SCREEN_H; y++) for (int x = 0; x < TOP_W; x++)
	{
		unsigned char *d = rgb + (y * TOP_W + x) * 3; u32 v = top[y * TOP_W + x];
		d[0] = (unsigned char) (v >> 16); d[1] = (unsigned char) (v >> 8); d[2] = (unsigned char) v;
	}
	for (int y = 0; y < SCREEN_H; y++) for (int x = 0; x < BOTTOM_W; x++)
	{
		unsigned char *d = rgb + ((y + SCREEN_H) * TOP_W + x + (TOP_W - BOTTOM_W) / 2) * 3; u32 v = bottom[y * BOTTOM_W + x];
		d[0] = (unsigned char) (v >> 16); d[1] = (unsigned char) (v >> 8); d[2] = (unsigned char) v;
	}
	return rgb;
}

static void (*g_onMachine) (n3ds::Machine *m);
static bool (*g_openSource) (const char *path, n3ds::Source *src);	// (the host's own files instead of stdio's)		// (the host's own set-up: Onyx's app cores)

static bool fileRead (void *user, n3ds::u64 offset, void *dst, n3ds::u32 n)
{
	FILE *f = (FILE *) user;
	return fseek (f, (long) offset, SEEK_SET) == 0 && fread (dst, 1, n, f) == n;
}

static int run (int argc, char **argv)
{
	if (argc < 2) { fprintf (stderr, "usage: n3dstest <program.elf> [frames [picture.ppm]]\n"); return 2; }
	int frames = argc > 2 ? atoi (argv[2]) : 600;
	// (the file is read piece by piece: a game is hundreds of MB)
	n3ds::Source src = { 0, 0, 0 };
	if (!g_openSource || !g_openSource (argv[1], &src))
	{
		FILE *f = fopen (argv[1], "rb");
		if (!f) { fprintf (stderr, "cannot open %s\n", argv[1]); return 2; }
		fseek (f, 0, SEEK_END); long size = ftell (f); fseek (f, 0, SEEK_SET);
		src.user = f; src.size = (n3ds::u64) size; src.read = fileRead;
	}

	n3ds::Machine *m = new n3ds::Machine;
	if (!m->init ()) { fprintf (stderr, "not enough memory for the machine\n"); return 2; }
	m->debugOut = debugOut;
	if (g_onMachine) g_onMachine (m);
	// N3DS_WORKERS=<n>  the rasterizer's work cut in n parts as for n cores, done here one after the other: the
	// pictures must be the same
	if (const char *w = getenv ("N3DS_WORKERS"))
		if (!m->parallel && atoi (w) > 1)
		{
			m->workers = atoi (w);
			m->parallel = [] (void *, void (*fn) (void *, int), void *arg, int workers) { for (int i = 0; i < workers; i++) fn (arg, i); };
		}
	if (argc > 5) m->gpuSkip = (n3ds::u32) strtoul (argv[5], 0, 16);	// (a 5th argument: parts of the GPU left out, to time them)
	m->trace = getenv ("N3DS_TRACE") != 0;
	// N3DS_FONT=<sysfont.bcfnt>  the shared system font (tools/n3ds/mkfont.py makes ours)
	if (const char *fontPath = argc > 4 ? argv[4] : getenv ("N3DS_FONT"))		// (or the 4th argument: where there is no environment)
	{
		FILE *ff = fopen (fontPath, "rb");
		if (ff)
		{
			static unsigned char font[0x332000];
			size_t fn = fread (font, 1, sizeof font, ff); fclose (ff);
			if (!m->setSharedFont (font, (n3ds::u32) fn)) fprintf (stderr, "%s is not a font (BCFNT)\n", fontPath);
		}
	}
	if (!m->loadFrom (src)) { fprintf (stderr, "%s\n", m->lastError); return 2; }
	if (m->title[0]) fprintf (stderr, "%s [%s]\n", m->title, m->productCode);
	// N3DS_SAVE=<file>  what the program writes (its saves, the SD card): read at the start, written back at the end
	const char *savePath = getenv ("N3DS_SAVE");
	if (savePath)
	{
		FILE *sf = fopen (savePath, "rb");
		if (sf)
		{
			fseek (sf, 0, SEEK_END); long sn = ftell (sf); fseek (sf, 0, SEEK_SET);
			unsigned char *sb = (unsigned char *) malloc ((size_t) sn + 1);
			if (sb && fread (sb, 1, (size_t) sn, sf) == (size_t) sn && !m->storageImport (sb, (n3ds::u32) sn)) fprintf (stderr, "%s is not a save file of ours\n", savePath);
			free (sb); fclose (sf);
		}
	}
	// N3DS_KEYS=100-110:1;200-210:8  buttons held over frame ranges, a hex mask each (n3ds.h's BTN_*)
	// N3DS_TOUCH=300-305:160,120      the touch screen pressed there over frames
	// N3DS_SHOTS=<prefix> N3DS_SHOTEVERY=<n>  a picture every n frames (default 60)
	struct Range { int f0, f1; unsigned a, b; } keys[32], touch[32]; int nk = 0, nt = 0;
	for (const char *e = getenv ("N3DS_KEYS"); e && *e && nk < 32; )
	{
		Range r; char *end;
		r.f0 = (int) strtol (e, &end, 10); if (*end != '-') break;
		r.f1 = (int) strtol (end + 1, &end, 10); if (*end != ':') break;
		r.a = (unsigned) strtoul (end + 1, &end, 16); r.b = 0;
		keys[nk++] = r; e = *end == ';' ? end + 1 : end; if (*end != ';') break;
	}
	for (const char *e = getenv ("N3DS_TOUCH"); e && *e && nt < 32; )
	{
		Range r; char *end;
		r.f0 = (int) strtol (e, &end, 10); if (*end != '-') break;
		r.f1 = (int) strtol (end + 1, &end, 10); if (*end != ':') break;
		r.a = (unsigned) strtoul (end + 1, &end, 10); if (*end != ',') break;
		r.b = (unsigned) strtoul (end + 1, &end, 10);
		touch[nt++] = r; e = *end == ';' ? end + 1 : end; if (*end != ';') break;
	}
	const char *shots = getenv ("N3DS_SHOTS");
	const int shotEvery = getenv ("N3DS_SHOTEVERY") ? atoi (getenv ("N3DS_SHOTEVERY")) : 60;
	int n = 0;
	struct timeval t0; gettimeofday (&t0, 0);
	while (n < frames && !m->exited)
	{
		unsigned b = 0; bool down = false; int tx = 0, ty = 0;
		for (int k = 0; k < nk; k++) if (n >= keys[k].f0 && n <= keys[k].f1) b |= keys[k].a;
		for (int k = 0; k < nt; k++) if (n >= touch[k].f0 && n <= touch[k].f1) { down = true; tx = (int) touch[k].a; ty = (int) touch[k].b; }
		m->setInput (b, 0, 0, down, tx, ty);
		if (const char *g = getenv ("N3DS_GPUTRACE")) m->traceGpu = n == atoi (g);	// (the GPU's draws of that frame)
		m->runFrame (); n++;
		if (shots && shotEvery > 0 && n % shotEvery == 0)
		{
			char name[512]; snprintf (name, sizeof name, "%s%05d.ppm", shots, n);
			FILE *o = fopen (name, "wb");
			if (o) { fprintf (o, "P6\n%d %d\n255\n", n3ds::TOP_W, n3ds::SCREEN_H * 2); fwrite (screens (m), 1, (size_t) n3ds::TOP_W * n3ds::SCREEN_H * 2 * 3, o); fclose (o); }
		}
	}
	fflush (stdout);
	struct timeval t1; gettimeofday (&t1, 0);
	const double secs = (double) (t1.tv_sec - t0.tv_sec) + (double) (t1.tv_usec - t0.tv_usec) / 1e6;
	if (n && secs > 0) fprintf (stderr, "%d frames in %.2f s: %.1f frames a second (the console: 59.8)%c", n, secs, (double) n / secs, 10);
	fprintf (stderr, "%s after %d frames (%llu ticks): %llu system calls, %llu thread switches, %u memory faults\n",
		 m->exited ? "ended" : "still running", n, (unsigned long long) m->now, (unsigned long long) m->svcCount,
		 (unsigned long long) m->switchCount, (unsigned) m->mem.faults);
	if (m->workers > 1) fprintf (stderr, "the rasterizer on %d cores%c", m->workers, 10);
	fprintf (stderr, "the GPU's share: %.2f s in command lists, %.2f s in transfers, %.2f s in fills%c", (double) m->gsp.usLists / 1e6, (double) m->gsp.usTransfers / 1e6, (double) m->gsp.usFills / 1e6, 10);
	fprintf (stderr, "of the lists: %.2f s rasterizing %llu triangles, %llu pixels; the rest for %llu vertices (%llu shader instructions, %llu triangles)%c", (double) m->gsp.usRaster / 1e6, (unsigned long long) m->gsp.trianglesDrawn, (unsigned long long) m->gsp.pixelsDrawn, (unsigned long long) m->gsp.vertices, (unsigned long long) m->gsp.shaderSteps, (unsigned long long) m->gsp.trianglesIn, 10);
	const unsigned char *rgb = screens (m);
	const size_t rgbSize = (size_t) n3ds::TOP_W * n3ds::SCREEN_H * 2 * 3;
	fprintf (stderr, "screens %08x (%llu VBlanks; the GPU was asked %u fills, %u transfers, %u command lists)\n", crc32 (rgb, rgbSize),
		 (unsigned long long) m->gsp.frames, (unsigned) m->gsp.fills, (unsigned) m->gsp.transfers, (unsigned) m->gsp.cmdLists);
	if (argc > 3 && strcmp (argv[3], "-") != 0)
	{
		FILE *o = fopen (argv[3], "wb");
		if (o) { fprintf (o, "P6\n%d %d\n255\n", n3ds::TOP_W, n3ds::SCREEN_H * 2); fwrite (rgb, 1, rgbSize, o); fclose (o); }
	}
	if (savePath && m->storageDirty)
	{
		const n3ds::u32 need = m->storageExport (0, 0);
		unsigned char *sb = (unsigned char *) malloc (need + 1);
		FILE *sf = sb ? fopen (savePath, "wb") : 0;
		if (sf) { m->storageExport (sb, need); fwrite (sb, 1, need, sf); fclose (sf); fprintf (stderr, "saved %u bytes to %s\n", (unsigned) need, savePath); }
		free (sb);
	}
	if (m->notes[0]) fprintf (stderr, "not emulated: %s\n", m->notes);
	if (m->mem.faults) fprintf (stderr, "the last memory fault: %08x\n", (unsigned) m->mem.faultAddr);
	if (m->lastError[0]) fprintf (stderr, "error: %s\n", m->lastError);
	return m->exited && !m->lastError[0] && m->exitCode == 0 ? 0 : 1;
}

#ifdef N3DS_ONYX
// On Onyx a program has no argc / argv: its arguments are one line, asked from the kernel (AppKit).
#include "appkit/appkit.h"

// The rasterizer's helpers: up to two application cores (2 and 3), each waiting for a request, doing its part
// and saying so. They make no system call. The main thread is worker 0.
static struct { void (*fn) (void *, int); void *arg; volatile unsigned req, done[2], stop; int n; int cores[2]; } g_par;
static void parCore (void *a)
{
	const int idx = (int) (long) a;
	unsigned seen = 0;
	while (!g_par.stop)
	{
		if (g_par.req != seen)
		{
			seen = g_par.req;
			__asm__ volatile ("dmb ish" ::: "memory");
			g_par.fn (g_par.arg, idx);
			g_par.done[idx] = seen;
			__asm__ volatile ("dsb ish; sev" ::: "memory");
		}
		__asm__ volatile ("wfe" ::: "memory");
	}
}
static int g_parMain = 1;			// (does the main thread rasterize too? It shares core 0 with the system)
static void parRun (void *, void (*fn) (void *, int), void *arg, int)
{
	g_par.fn = fn; g_par.arg = arg;
	__asm__ volatile ("dmb ish" ::: "memory");
	g_par.req++;
	__asm__ volatile ("dsb ish; sev" ::: "memory");
	if (g_parMain) fn (arg, g_par.n);
	// (a page of the program a core touches first -- code, a table -- is brought in by the kernel's pager, a task of
	// core 0: it must be given the processor, or the core waits for ever)
	for (int i = 0; i < g_par.n; i++)
		for (unsigned spins = 0; g_par.done[i] != g_par.req; )
		{
			__asm__ volatile ("wfe" ::: "memory");
			if (++spins >= 16)
			{
				kapi_yield (); spins = 0;
				const int st = kapi_core_state (g_par.cores[i]);
				if (st != 1 && g_par.done[i] != g_par.req) { fprintf (stderr, "the application core %d stopped (state %d) in the rasterizer%c", g_par.cores[i], st, 10); exit (3); }
			}
		}
}
// The program's file through Onyx's own calls (a seek, then one read of the whole piece).
static bool kfileRead (void *user, n3ds::u64 offset, void *dst, n3ds::u32 n)
{
	if (kapi_seek (user, offset) != 0) return false;
	unsigned char *d = (unsigned char *) dst;
	while (n) { const int k = kapi_read (user, d, n); if (k <= 0) return false; d += k; n -= (n3ds::u32) k; }
	return true;
}
static bool kfileOpen (const char *path, n3ds::Source *src)
{
	void *h = kapi_open (path);
	if (!h) return false;
	src->user = h; src->size = kapi_fsize64 (h); src->read = kfileRead;
	return true;
}

// The other cores may still hold, in their instruction caches, what an earlier program had at the same place
// (the kernel empties core 0's when it loads a program): the program's code is declared new to all of them.
extern "C" void _start (void);
static void codeFresh (void)
{
	struct kapi_vm_region r;
	const unsigned long long at = (unsigned long long) (void *) &_start;
	if (kapi_vm_query (at, &r) == 0 && r.end > at) __builtin___clear_cache ((char *) at, (char *) r.end);
}

static void parSetup (n3ds::Machine *m)
{
	codeFresh ();
	for (int i = 0; i < 2; i++)
	{
		const int c = kapi_core_acquire ();
		if (c < 0) break;
		unsigned char *stack = (unsigned char *) malloc (256 * 1024);
		if (!stack || kapi_core_run (c, parCore, (void *) (long) g_par.n, stack + 256 * 1024) != 0) { kapi_core_release (c); break; }
		g_par.cores[g_par.n++] = c;
	}
	if (g_par.n) { m->workers = g_par.n + g_parMain; m->parallel = parRun; }
	fprintf (stderr, "%d application cores taken%s%c", g_par.n, g_parMain ? ", the main thread rasterizes too" : "", 10);
}

int main (void)
{
	g_onMachine = parSetup; g_openSource = kfileOpen;
	static char line[512]; static char *argv[8];
	int argc = 0;
	argv[argc++] = (char *) "n3dstest";
	kapi_get_args (line, sizeof line);
	for (char *p = line; *p && argc < 8; )
	{
		while (*p == ' ') p++;
		if (!*p) break;
		argv[argc++] = p;
		while (*p && *p != ' ') p++;
		if (*p) *p++ = 0;
	}
	if (argc > 6) g_parMain = atoi (argv[6]);			// (a 6th argument 0: the application cores alone rasterize)
	const int r = run (argc, argv);
	g_par.stop = 1;
	__asm__ volatile ("dsb ish; sev" ::: "memory");
	for (int i = 0; i < g_par.n; i++) kapi_core_release (g_par.cores[i]);
	return r;
}
#else
int main (int argc, char **argv) { return run (argc, argv); }
#endif
