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
// (N3DS_GPUDUMP: a frame recorded for a GPU written to a file; never drawn here)
static int g_frameNow, g_dumpN;
static bool dumpDraw (void *, const n3ds::GpuFrame *f, n3ds::u32 *pixels)
{
	const char *at = getenv ("N3DS_GPUDUMPAT");
	if (!at || g_frameNow != atoi (at)) return false;
	char name[512]; snprintf (name, sizeof name, "%s_%d.gpf", getenv ("N3DS_GPUDUMP"), g_dumpN++);
	FILE *o = fopen (name, "wb");
	if (!o) return false;
	const n3ds::u32 head[8] = { 0x31465047, (n3ds::u32) f->w, (n3ds::u32) f->h, f->nPrograms, f->nTextures, f->nb, f->nu, f->nFloats };
	fwrite (head, 4, 8, o);
	for (n3ds::u32 i = 0; i < f->nPrograms; i++) { fwrite (&f->programs[i].nWords, 4, 1, o); fwrite (&f->programs[i].nVary, 4, 1, o); fwrite (f->programs[i].words, 8, f->programs[i].nWords, o); }
	for (n3ds::u32 i = 0; i < f->nTextures; i++)
	{
		bool used = false;
		for (n3ds::u32 b = 0; b < f->nb; b++) for (int k = 0; k < 3; k++) if (f->b[b].tex[k] == (int) i) used = true;
		const n3ds::u32 wh[2] = { used && f->textures[i].px ? f->textures[i].w : 0, f->textures[i].h };
		fwrite (wh, 4, 2, o);
		if (wh[0]) fwrite (f->textures[i].px, 4, (size_t) wh[0] * wh[1], o);
	}
	fwrite (f->b, sizeof (n3ds::GpuBatch), f->nb, o);
	fwrite (f->u, 4, f->nu, o);
	fwrite (f->v, 4, f->nFloats, o);
	fwrite (pixels, 4, (size_t) f->w * (size_t) f->h, o);
	fclose (o);
	fprintf (stderr, "GPU frame %s: %dx%d, %u batches, %u floats%c", name, f->w, f->h, (unsigned) f->nb, (unsigned) f->nFloats, 10);
	return false;
}
static void dumpSoft (void *, const n3ds::u32 *px, int w, int h)
{
	const char *at = getenv ("N3DS_GPUDUMPAT");
	if (!at || g_frameNow != atoi (at) || !g_dumpN) return;
	char name[512]; snprintf (name, sizeof name, "%s_%d_soft.ppm", getenv ("N3DS_GPUDUMP"), g_dumpN - 1);
	FILE *o = fopen (name, "wb");
	if (!o) return;
	fprintf (o, "P6%c%d %d%c255%c", 10, w, h, 10, 10);
	for (int i = 0; i < w * h; i++) { const unsigned char c[3] = { (unsigned char) (px[i] >> 16), (unsigned char) (px[i] >> 8), (unsigned char) px[i] }; fwrite (c, 1, 3, o); }
	fclose (o);
}
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
	// N3DS_ASIDE=<n>  the rasterizer's work "on other cores" played here: a list's triangles are drawn only when
	// the machine has asked n times whether they are done, or when it waits for them -- the program goes on meanwhile
	if (const char *w = getenv ("N3DS_ASIDE"))
		if (!m->parallelBegin)
		{
			static struct { void (*fn) (void *, int); void *arg; int left, every; } aside;
			aside.every = atoi (w);
			m->parallelBegin = [] (void *, void (*fn) (void *, int), void *arg) { aside.fn = fn; aside.arg = arg; aside.left = aside.every; return true; };
			m->parallelDone = [] (void *) { if (aside.fn && --aside.left <= 0) { aside.fn (aside.arg, 0); aside.fn = 0; } return aside.fn == 0; };
			m->parallelEnd = [] (void *, void (*fn) (void *, int), void *arg) { fn (arg, 1); aside.fn = 0; };
			m->helpers = 1;
		}
	// N3DS_GPUDUMP=<prefix> N3DS_GPUDUMPAT=<frame>  the frames recorded for a GPU during that frame, each into
	// <prefix>_<k>.gpf (tools/tests/n3ds/gpuview.cpp draws one on the PC, the generated shaders in the QPU
	// simulator), and the same frame as the software renderer drew it into <prefix>_<k>_soft.ppm
	if (getenv ("N3DS_GPUDUMP")) { m->gpuDraw = dumpDraw; m->gpuSoft = dumpSoft; }
	// N3DS_SHADERCHECK=1  every vertex through the compiled shader and the interpreter: their outputs compared
	if (getenv ("N3DS_SHADERCHECK")) m->shaderCheck = true;
	// N3DS_MONO=1  the right eye's picture is not drawn (what a host with one picture a screen asks)
	if (getenv ("N3DS_MONO")) m->monoOnly = true;
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
	// (a 7th argument: that many frames first -- a game's loading -- before the time and the GPU's counters are taken)
	const int warm = argc > 7 ? atoi (argv[7]) : 0;
	struct timeval t0; gettimeofday (&t0, 0);
	while (n < frames && !m->exited)
	{
		if (warm > 0 && n == warm)
		{
			m->gpuSync ();
			gettimeofday (&t0, 0);
			m->gsp.usLists = m->gsp.usTransfers = m->gsp.usFills = m->gsp.usRaster = m->gsp.usRasterAside = 0;
			m->gsp.hostTransfers = m->gsp.usHostTransfers = m->gsp.usGpu = m->gsp.gpuFrames = m->gsp.softFrames = 0; m->gsp.transfers = 0;
			m->gsp.usRead = m->gsp.usShade = m->gsp.usAssemble = 0;
			m->gsp.vertices = m->gsp.trianglesDrawn = m->gsp.pixelsDrawn = m->gsp.shaderSteps = m->gsp.trianglesIn = 0;
		}
		unsigned b = 0; bool down = false; int tx = 0, ty = 0;
		for (int k = 0; k < nk; k++) if (n >= keys[k].f0 && n <= keys[k].f1) b |= keys[k].a;
		for (int k = 0; k < nt; k++) if (n >= touch[k].f0 && n <= touch[k].f1) { down = true; tx = (int) touch[k].a; ty = (int) touch[k].b; }
		m->setInput (b, 0, 0, down, tx, ty);
		if (const char *g = getenv ("N3DS_GPUTRACE")) m->traceGpu = n == atoi (g);	// (the GPU's draws of that frame)
		g_frameNow = n;
		m->runFrame (); n++;
		if (shots && shotEvery > 0 && n % shotEvery == 0)
		{
			m->gpuSync ();
			char name[512]; snprintf (name, sizeof name, "%s%05d.ppm", shots, n);
			FILE *o = fopen (name, "wb");
			if (o) { fprintf (o, "P6\n%d %d\n255\n", n3ds::TOP_W, n3ds::SCREEN_H * 2); fwrite (screens (m), 1, (size_t) n3ds::TOP_W * n3ds::SCREEN_H * 2 * 3, o); fclose (o); }
		}
	}
	m->gpuSync ();
	fflush (stdout);
	struct timeval t1; gettimeofday (&t1, 0);
	const double secs = (double) (t1.tv_sec - t0.tv_sec) + (double) (t1.tv_usec - t0.tv_usec) / 1e6;
	const int timed = warm > 0 && n > warm ? n - warm : n;
	if (timed && secs > 0) fprintf (stderr, "%d frames in %.2f s: %.1f frames a second (the console: 59.8)%c", timed, secs, (double) timed / secs, 10);
	fprintf (stderr, "%s after %d frames (%llu ticks): %llu system calls, %llu thread switches, %u memory faults\n",
		 m->exited ? "ended" : "still running", n, (unsigned long long) m->now, (unsigned long long) m->svcCount,
		 (unsigned long long) m->switchCount, (unsigned) m->mem.faults);
	if (m->gpuDraw) fprintf (stderr, "the targets' frames: %llu drawn by the GPU (%.2f s with their copies), %llu by the software renderer%c", (unsigned long long) m->gsp.gpuFrames, (double) m->gsp.usGpu / 1e6, (unsigned long long) m->gsp.softFrames, 10);
	if (m->shaderCheck) fprintf (stderr, "the compiled vertex shaders: %llu vertices checked against the interpreter, %llu differ%c", (unsigned long long) m->gsp.shaderChecked, (unsigned long long) m->gsp.shaderWrong, 10);
	if (m->monoOnly) fprintf (stderr, "one picture a screen: %llu draws of the right eye left out%c", (unsigned long long) m->gsp.eyeDraws, 10);
	if (m->helpers) fprintf (stderr, "the rasterizer has %d other core%s: %.2f s of its time while the program went on%c", m->helpers, m->helpers > 1 ? "s" : "", (double) m->gsp.usRasterAside / 1e6, 10);
	fprintf (stderr, "the GPU's share: %.2f s in command lists, %.2f s in transfers, %.2f s in fills%c", (double) m->gsp.usLists / 1e6, (double) m->gsp.usTransfers / 1e6, (double) m->gsp.usFills / 1e6, 10);
	fprintf (stderr, "of the lists: %.2f s rasterizing %llu triangles, %llu pixels; the rest for %llu vertices (%llu shader instructions, %llu triangles)%c", (double) (m->gsp.usRaster - m->gsp.usRasterAside) / 1e6, (unsigned long long) m->gsp.trianglesDrawn, (unsigned long long) m->gsp.pixelsDrawn, (unsigned long long) m->gsp.vertices, (unsigned long long) m->gsp.shaderSteps, (unsigned long long) m->gsp.trianglesIn, 10);
	if (m->gpuDraw) fprintf (stderr, "of the %u transfers: %llu from the host's pixels in %.2f s%c", (unsigned) m->gsp.transfers, (unsigned long long) m->gsp.hostTransfers, (double) m->gsp.usHostTransfers / 1e6, 10);
	if (m->gsp.usShade) fprintf (stderr, "of the draws of many vertices: %.2f s reading attributes, %.2f s shading, %.2f s assembling triangles%c", (double) m->gsp.usRead / 1e6, (double) m->gsp.usShade / 1e6, (double) m->gsp.usAssemble / 1e6, 10);
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
#include "v3d/qpu.h"
#include "v3d/shaders.h"

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
static bool parBegin (void *, void (*fn) (void *, int), void *arg)
{
	g_par.fn = fn; g_par.arg = arg;
	__asm__ volatile ("dmb ish" ::: "memory");
	g_par.req++;
	__asm__ volatile ("dsb ish; sev" ::: "memory");
	return true;
}
static bool parDone (void *)
{
	for (int i = 0; i < g_par.n; i++) if (g_par.done[i] != g_par.req) return false;
	return true;
}
static void parEnd (void *, void (*fn) (void *, int), void *arg)
{
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

// ---- the V3D: a frame the core recorded (n3ds.h's GpuFrame) drawn by the kernel's GPU calls -- the programs made
// (the stock vertex and coordinate shaders with the core's fragment shader), the textures given, one gpu_render3.
static int g_useGpu = 1;
static struct { int prog[256]; int tex[96]; unsigned texSerial[96]; bool texMade[96]; bool off; int said; unsigned calls, refused[4]; } g_gpu;
static bool onyxDraw (void *, const n3ds::GpuFrame *f, n3ds::u32 *pixels)
{
	g_gpu.calls++;
	if (g_gpu.off || f->nb > 1024 || f->nu > (1u << 15)) { g_gpu.refused[0]++; return false; }
	static qpu::Prog vs, cs; static bool csMade;
	static struct kapi_gpu_batch3 rb[1024];
	static unsigned ru[4 + (1 << 15)];
	static unsigned *swap;
	if (!csMade) { qpu::passCS (cs); csMade = true; }
	if (!swap) swap = (unsigned *) malloc (4u * 1024 * 1024);
	if (!swap) return false;
	unsigned vu[4]; qpu::viewUniforms (f->w, f->h, vu);
	for (int k = 0; k < 4; k++) ru[k] = vu[k];
	memcpy (ru + 4, f->u, (size_t) f->nu * 4);
	for (n3ds::u32 i = 0; i < f->nb; i++)
	{
		const n3ds::GpuBatch &b = f->b[i];
		if (b.program >= 256) return false;
		if (!g_gpu.prog[b.program])
		{
			const n3ds::GpuProgramInfo &P = f->programs[b.program];
			vs.n = 0; vs.bad = false; qpu::passVS (vs, 4 + (int) P.nVary);
			struct kapi_gpu_program kp;
			memset (&kp, 0, sizeof kp);
			kp.vs = vs.words (); kp.nvs = (unsigned) vs.count (); kp.cs = cs.words (); kp.ncs = (unsigned) cs.count ();
			kp.fs = P.words; kp.nfs = P.nWords;
			kp.inputs = 4 + P.nVary; kp.csInputs = 4; kp.csOutputs = 6; kp.varyings = P.nVary; kp.flags = qpu::programFlags (P.flags);
			const int h = kapi_gpu_program (-1, &kp);
			g_gpu.prog[b.program] = h >= 0 ? h + 1 : -1;
			if (h < 0) fprintf (stderr, "gpu_program: %d (%u instructions, %u varyings)%c", h, (unsigned) P.nWords, (unsigned) P.nVary, 10);
		}
		if (g_gpu.prog[b.program] < 0) { g_gpu.refused[1]++; return false; }
		struct kapi_gpu_batch3 &o = rb[i];
		memset (&o, 0, sizeof o);
		o.b.count = b.count; o.b.program = g_gpu.prog[b.program] - 1; o.b.flags = b.flags; o.b.blend = b.blend; o.b.wmask = b.wmask;
		for (int k = 0; k < 4; k++) o.b.scissor[k] = b.scissor[k];
		o.b.vsUni = 0; o.b.vsNUni = 4; o.b.csUni = 0; o.b.csNUni = 2; o.b.fsUni = 4 + b.uni; o.b.fsNUni = b.nUni;
		for (int k = 0; k < 8; k++) { o.b.tex[k] = -1; o.b.texUni[k] = -1; }
		for (int k = 0; k < 3; k++)
		{
			const int sl = b.tex[k];
			if (sl < 0 || sl >= 96 || b.texUni[k] < 0) continue;
			const n3ds::GpuTextureInfo &T = f->textures[sl];
			if (!T.px || T.w > 1024 || T.h > 1024) { g_gpu.refused[2]++; return false; }
			if (!g_gpu.texMade[sl] || g_gpu.texSerial[sl] != T.serial)
			{
				for (n3ds::u32 n = 0; n < T.w * T.h; n++) { const unsigned c = T.px[n]; swap[n] = (c & 0xFF00FF00u) | (c >> 16 & 255) | (c & 255) << 16; }	// (r g b a bytes -> 0xAARRGGBB)
				int h = kapi_gpu_texture (g_gpu.texMade[sl] ? g_gpu.tex[sl] : -1, swap, (int) T.w, (int) T.h, (int) T.w);
				if (h < 0 && g_gpu.texMade[sl]) h = kapi_gpu_texture (-1, swap, (int) T.w, (int) T.h, (int) T.w);
				if (h < 0) { if (!g_gpu.said++) fprintf (stderr, "gpu_texture: %d (%ux%u)%c", h, (unsigned) T.w, (unsigned) T.h, 10); return false; }
				g_gpu.tex[sl] = h; g_gpu.texMade[sl] = true; g_gpu.texSerial[sl] = T.serial;
			}
			o.b.tex[k] = g_gpu.tex[sl]; o.b.texFlags[k] = b.texFlags[k]; o.b.texUni[k] = b.texUni[k];
		}
		o.off = b.off; o.stride = b.stride;
	}
	struct kapi_gpu_frame fr = { pixels, f->w, f->h, f->w, 0, KAPI_GPU_F_KEEP | KAPI_GPU_F_ALPHA };
	const int ret = kapi_gpu_render3 (&fr, f->v, f->nFloats, rb, f->nb, ru, 4 + f->nu, 0);
	if (ret != 0)
	{
		if (g_gpu.said++ < 4) fprintf (stderr, "gpu_render3: %d (%u batches, %u floats)%c", ret, (unsigned) f->nb, (unsigned) f->nFloats, 10);
		if (ret == -1 || ret == -3) g_gpu.off = true;			// (no GPU, or it did not finish: left alone from now on)
		g_gpu.refused[3]++;
		return false;
	}
	return true;
}

static void gpuSay (void) { if (g_gpu.calls) fprintf (stderr, "the GPU was given %u frames; refused: %u too big or off, %u a program, %u a texture, %u the render%c", g_gpu.calls, g_gpu.refused[0], g_gpu.refused[1], g_gpu.refused[2], g_gpu.refused[3], 10); }
static void (*g_atEnd) (void);
static void parSetup (n3ds::Machine *m)
{
	g_atEnd = gpuSay;
	m->monoOnly = true;							// (Onyx shows one picture a screen)
	char gpu[96];
	if (g_useGpu && kapi_gpu_info (gpu, sizeof gpu) > 0 && qpu::setVersion (qpu::versionOf (gpu)))
	{
		m->gpuDraw = onyxDraw;
		fprintf (stderr, "the fragments on the GPU: %s%c", gpu, 10);
	}
	codeFresh ();
	for (int i = 0; i < 2; i++)
	{
		const int c = kapi_core_acquire ();
		if (c < 0) break;
		unsigned char *stack = (unsigned char *) malloc (256 * 1024);
		if (!stack || kapi_core_run (c, parCore, (void *) (long) g_par.n, stack + 256 * 1024) != 0) { kapi_core_release (c); break; }
		g_par.cores[g_par.n++] = c;
	}
	if (g_par.n) { m->helpers = g_par.n; m->parallelBegin = parBegin; m->parallelDone = parDone; m->parallelEnd = parEnd; }
	fprintf (stderr, "%d application cores taken%s%c", g_par.n, g_parMain ? ", the main thread rasterizes too" : "", 10);
}

int main (void)
{
	g_onMachine = parSetup; g_openSource = kfileOpen;
	static char line[512]; static char *argv[12];
	int argc = 0;
	argv[argc++] = (char *) "n3dstest";
	kapi_get_args (line, sizeof line);
	for (char *p = line; *p && argc < 12; )
	{
		while (*p == ' ') p++;
		if (!*p) break;
		argv[argc++] = p;
		while (*p && *p != ' ') p++;
		if (*p) *p++ = 0;
	}
	if (argc > 8) g_useGpu = atoi (argv[8]);			// (an 8th argument 0: the software renderer alone)
	if (argc > 6) g_parMain = atoi (argv[6]);			// (a 6th argument 0: the application cores alone rasterize)
	const int r = run (argc, argv);
	g_par.stop = 1;
	__asm__ volatile ("dsb ish; sev" ::: "memory");
	if (g_atEnd) g_atEnd ();
	for (int i = 0; i < g_par.n; i++) kapi_core_release (g_par.cores[i]);
	return r;
}
#else
int main (int argc, char **argv) { return run (argc, argv); }
#endif
