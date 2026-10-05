//
// gcrun.cpp -- a headless run of a GameCube disc image with the Onyx core (Windows, MinGW-w64): the
// status line every [every] fields, pictures, the sound, a memory card, an input script.
//   g++ -std=c++17 -O2 -I user -I user/Kits -I user/Runtime -I user/Include -I user/Libs -I user/Emulators -I user/Ports -o gcrun.exe tools/tests/gc/gcrun.cpp user/Emulators/gc/*.cpp
//       pc/NintendoEMU/core/gxgl.cpp -lopengl32 -lgdi32          (-DGC_NV: an Optimus laptop's NVIDIA GPU)
//   gcrun <iso> <fields> [every]
//   GC_JIT=1                  the x86-64 JIT (else the interpreter); GC_JITOFF=n: the interpreter from field n
//   GC_GL=<scale>             the GX drawn by the GPU (gxgl.cpp, a hidden window's OpenGL context)
//   GC_SNAP=n                 pictures every n fields: snap_<n>_xfb.ppm (MEM1's XFB), snap_<n>_gx.ppm
//                             (the GX: the GPU's with GC_GL, else the BASIC 3D's software renderer)
//   GC_PAD="f0-f1:hex;..."    the pad's buttons (Machine::PAD_*) held from field f0 to f1; 0x10000 /
//                             0x20000 / 0x40000 / 0x80000: the stick left / right / up / down
//   GC_CARD=file              a memory card in slot A (read, written back if it changed; none: erased)
//   GC_WAV=file               the sound (48 kHz stereo), and its level on the status lines
//   GC_PROF=file              a sampling profiler (GC_PROF_FROM=n: from field n) -> tools: nm, see below
//   GC_DUMP=file              MEM1 at the end; GC_STEPS=n: a few runs by hand, where the CPU goes
// The samples of GC_PROF: "J" (the JIT's code), "M <dll>", or an offset in the executable (nm -C).
#include "gc/gc.h"
#include "basic/bas3d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include <GL/gl.h>
using namespace gc;

// (the NVIDIA GPU of an Optimus laptop, not the integrated one: GC_NV=1 at build time)
#ifdef GC_NV
extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 1;
extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif
// GC_GL=scale: the GX on the GPU (pc/NintendoEMU/core/gxgl.cpp) in a hidden window's OpenGL context
gc::GxGpu *gxgl_create (int scale, char *err, int cap);
int gxgl_read (gc::GxGpu *g, unsigned *px, int cap, int *w, int *h);
const char *gxgl_name (gc::GxGpu *g);
static bool glSetup ()
{
	WNDCLASSA wc = {}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandle (0); wc.lpszClassName = "gcrun";
	RegisterClassA (&wc);
	HWND w = CreateWindowA ("gcrun", "gcrun", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, 0, 0, wc.hInstance, 0);
	HDC dc = GetDC (w);
	PIXELFORMATDESCRIPTOR pfd = {}; pfd.nSize = sizeof pfd; pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER; pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24;
	if (!SetPixelFormat (dc, ChoosePixelFormat (dc, &pfd), &pfd)) return false;
	HGLRC rc = wglCreateContext (dc);
	return rc && wglMakeCurrent (dc, rc);
}

static u8 *jitMem; static u32 jitMemSize;
static void *hostCode (u32 size) { jitMem = (u8 *) VirtualAlloc (0, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE); jitMemSize = size; return jitMem; }

// GC_PROF=file: a sampling profiler -- the main thread's rip every ~1 ms: "J" in the JIT's code, else
// its offset in the executable (resolved with nm afterwards)
static HANDLE profMain; static volatile bool profStop, profOn = true; static FILE *profOut;
static DWORD WINAPI profThread (LPVOID)
{
	u64 base = (u64) GetModuleHandle (0);
	while (!profStop)
	{
		if (!profOn) { Sleep (1); continue; }
		Sleep (1);
		if (SuspendThread (profMain) == (DWORD) -1) continue;
		CONTEXT c; c.ContextFlags = CONTEXT_CONTROL;
		if (GetThreadContext (profMain, &c))
		{
			u64 ip = c.Rip;
			HMODULE mod = 0;
			if (jitMem && ip >= (u64) jitMem && ip < (u64) jitMem + jitMemSize) fprintf (profOut, "J\n");
			else if (GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR) ip, &mod) && (u64) mod != base)
			{
				char name[MAX_PATH]; GetModuleFileNameA (mod, name, sizeof name);
				const char *b = strrchr (name, 92);
				fprintf (profOut, "M %s\n", b ? b + 1 : name);
			}
			else fprintf (profOut, "%llx\n", (unsigned long long) (ip - base));
		}
		ResumeThread (profMain);
	}
	return 0;
}

static bool rd (void *ctx, u32 off, u32 len, u8 *dst)
{
	FILE *f = (FILE *) ctx;
	if (_fseeki64 (f, off, SEEK_SET)) return false;
	return fread (dst, 1, len, f) == len;
}

static Machine m;
static void glPpm (const char *path)
{
	static unsigned px[2560 * 2112]; int w = 0, h = 0;
	if (!m.gpu || !gxgl_read (m.gpu, px, 2560 * 2112, &w, &h)) { printf ("  (no GPU picture)\n"); return; }
	FILE *f = fopen (path, "wb"); if (!f) return;
	fprintf (f, "P6\n%d %d\n255\n", w, h);
	for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) { unsigned c = px[y * w + x]; fputc ((int) (c >> 16) & 255, f); fputc ((int) (c >> 8) & 255, f); fputc ((int) c & 255, f); }
	fclose (f);
	printf ("  GPU picture %dx%d -> %s\n", w, h, path);
}

static void ppm (const char *path, const u32 *px, int w, int h)
{
	FILE *f = fopen (path, "wb"); if (!f) return;
	fprintf (f, "P6\n%d %d\n255\n", w, h);
	for (int i = 0; i < w * h; i++) { u32 c = px[i]; fputc ((int) (c >> 16) & 255, f); fputc ((int) (c >> 8) & 255, f); fputc ((int) c & 255, f); }
	fclose (f);
}

// the last GX frame, drawn by the BASIC 3D's software renderer (as the GPU would), at scale x
static void gfxPpm (const char *path, int scale)
{
	if (m.gfxReady < 0) { printf ("  (no GX frame)\n"); return; }
	const GFrame &F = m.gfxFrame[m.gfxReady];
	int W = F.width * scale, H = F.height * scale;
	static unsigned px[2048 * 2048]; static float zb[2048 * 2048];
	static bas::G3Batch bt[GFrame::MAXB];
	for (int i = 0; i < F.nb; i++) { __builtin_memcpy (&bt[i], &F.b[i], sizeof bt[i]); bt[i].texture = F.b[i].tex >= 0 ? F.b[i].tex + 1 : 0; }
	bas::swRender (px, W, H, W, zb, (const bas::G3Vertex *) F.v, F.nv, bt, F.nb, F.clear, false, [] (int t) -> const bas::G3Texture *
	{
		static bas::G3Texture T;
		T.px = m.tex[t - 1].px; T.w = m.tex[t - 1].w; T.h = m.tex[t - 1].h;
		return &T;
	});
	ppm (path, px, W, H);
	printf ("  GX frame: %d vertices, %d batches, %dx%d -> %s\n", F.nv, F.nb, W, H, path);
}

// GC_PAD: "f0-f1:hex;f0-f1:hex..."
static u32 padAt (int field)
{
	const char *s = getenv ("GC_PAD");
	u32 b = 0;
	while (s && *s)
	{
		int f0 = (int) strtol (s, (char **) &s, 10), f1 = f0;
		if (*s == '-') f1 = (int) strtol (s + 1, (char **) &s, 10);
		if (*s == ':') s++;
		u32 v = (u32) strtoul (s, (char **) &s, 16);
		if (field >= f0 && field <= f1) b |= v;
		while (*s == ';' || *s == ' ') s++;
	}
	return b;
}

// GC_HASH=n: every n fields a line "hash <field> <MEM1's FNV-1a> pc <pc> cycles <cycles>" (two runs compared)
static void hashLine (Machine &m, int field)
{
	const char *h = getenv ("GC_HASH");
	if (!h || atoi (h) <= 0 || field % atoi (h)) return;
	unsigned long long x = 1469598103934665603ull;
	const unsigned long long *p = (const unsigned long long *) m.mem1;
	for (u32 i = 0; i < MEM1_SIZE / 8; i++) { x ^= p[i]; x *= 1099511628211ull; }
	printf ("hash %d %016llX pc %08X cycles %llu\n", field, x, m.pc, (unsigned long long) m.cycles);
	fflush (stdout);
}

int main (int argc, char **argv)
{
	if (argc < 3) { fprintf (stderr, "gcrun <iso> <fields> [every]\n"); return 2; }
	FILE *f = fopen (argv[1], "rb");
	if (!f) { perror (argv[1]); return 1; }
	_fseeki64 (f, 0, SEEK_END); long long sz = _ftelli64 (f);
	m.discRead = rd; m.discCtx = f;
	static u8 cardImg[Machine::CARD_SIZE];
	if (getenv ("GC_CARD"))					// a memory card in slot A (created erased)
	{
		for (u32 i = 0; i < sizeof cardImg; i++) cardImg[i] = 0xFF;
		FILE *cf = fopen (getenv ("GC_CARD"), "rb");
		if (cf) { size_t n = fread (cardImg, 1, sizeof cardImg, cf); fclose (cf); printf ("card: %u bytes read\n", (u32) n); }
		m.cardInsert (0, cardImg, sizeof cardImg);
	}
	if (!m.loadDiscImage ((u32) sz)) { printf ("load failed (pc %08X halted %d %s)\n", m.pc, m.halted, m.haltMsg); return 1; }
	printf ("title '%s' id %s pal %d entry %08X\n", m.title, m.gameId, m.pal, m.pc);
	if (getenv ("GC_JIT") && atoi (getenv ("GC_JIT"))) { codeAlloc = hostCode; printf ("JIT: %s\n", m.jitEnable () ? "on" : "FAILED"); }
	if (getenv ("GC_GL"))
	{
		char err[512] = "no OpenGL context";
		if (glSetup ()) m.gpu = gxgl_create (atoi (getenv ("GC_GL")), err, sizeof err);
		printf ("GPU: %s", m.gpu ? gxgl_name (m.gpu) : err); putchar (10);
	}
	int frames = atoi (argv[2]), every = argc > 3 ? atoi (argv[3]) : 30;
	int snap = getenv ("GC_SNAP") ? atoi (getenv ("GC_SNAP")) : 0;
	FILE *wav = 0; u32 wavFrames = 0; double wavSq = 0; u32 wavN = 0;		// GC_WAV=file: the sound (48 kHz)
	if (getenv ("GC_WAV")) { wav = fopen (getenv ("GC_WAV"), "wb"); static u8 hdr[44]; fwrite (hdr, 1, 44, wav); m.setAudioRate (48000); }
	HANDLE prof = 0;
	if (getenv ("GC_PROF"))
	{
		profOut = fopen (getenv ("GC_PROF"), "w");
		DuplicateHandle (GetCurrentProcess (), GetCurrentThread (), GetCurrentProcess (), &profMain, 0, FALSE, DUPLICATE_SAME_ACCESS);
		prof = CreateThread (0, 0, profThread, 0, 0, 0);
	}
	clock_t t0 = clock ();
	for (int i = 0; i < frames && !m.halted; i++)
	{
		{
			u32 p = padAt (i);					// (0x10000 / 0x20000 / 0x40000 / 0x80000: the stick left / right / up / down)
			int sx = (p & 0x10000) ? -100 : (p & 0x20000) ? 100 : 0, sy = (p & 0x40000) ? 100 : (p & 0x80000) ? -100 : 0;
			m.setPad (0, p & 0xFFFF, sx, sy, 0, 0, 0, 0);
		}
		if (getenv ("GC_JITOFF") && i == atoi (getenv ("GC_JITOFF"))) { m.jit = 0; printf ("(the interpreter from here)\n"); }
		if (getenv ("GC_PROF_FROM")) profOn = i >= atoi (getenv ("GC_PROF_FROM"));
		m.runFrame ();
		hashLine (m, i + 1);
		if (wav)
		{
			static s16 pcm[48000 * 2];
			int n = m.audioRead (pcm, 48000);
			fwrite (pcm, 4, (size_t) n, wav); wavFrames += (u32) n;
			for (int k = 0; k < n * 2; k++) wavSq += (double) pcm[k] * pcm[k];
			wavN += (u32) n * 2;
		}
		if (snap && i > 0 && i % snap == 0)
		{
			char p[256]; snprintf (p, sizeof p, "snap_%04d_xfb.ppm", i); ppm (p, m.fb, m.fbW, m.fbH);
			snprintf (p, sizeof p, "snap_%04d_gx.ppm", i);
			if (m.gpu) glPpm (p); else gfxPpm (p, 1);
		}
		if (i % every == 0 || m.halted)
		{
			char s[600]; m.status (s, sizeof s);
			printf ("%4d (%.0f s): %s", i, (double) (clock () - t0) / CLOCKS_PER_SEC, s);
			if (wav) { printf (" | sound %u frames, rms %.0f", wavFrames, wavN ? __builtin_sqrt (wavSq / wavN) : 0.0); wavSq = 0; wavN = 0; }
			printf ("\n"); fflush (stdout);
		}
	}
	if (wav)
	{
		u32 bytes = wavFrames * 4, rate = 48000;
		u8 h[44] = { 'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 2,0, 0,0,0,0, 0,0,0,0, 4,0, 16,0, 'd','a','t','a', 0,0,0,0 };
		auto le = [&] (int o, u32 v) { h[o] = (u8) v; h[o + 1] = (u8) (v >> 8); h[o + 2] = (u8) (v >> 16); h[o + 3] = (u8) (v >> 24); };
		le (4, 36 + bytes); le (24, rate); le (28, rate * 4); le (40, bytes);
		fseek (wav, 0, SEEK_SET); fwrite (h, 1, 44, wav); fclose (wav);
		printf ("sound: %u frames (%.1f s) -> %s\n", wavFrames, wavFrames / 48000.0, getenv ("GC_WAV"));
	}
	if (prof) { profStop = true; WaitForSingleObject (prof, INFINITE); fclose (profOut); }
	if (getenv ("GC_CARD") && m.card[0].dirty)
	{
		FILE *cf = fopen (getenv ("GC_CARD"), "wb");
		if (cf) { fwrite (cardImg, 1, sizeof cardImg, cf); fclose (cf); printf ("card: written\n"); }
	}
	if (getenv ("GC_STEPS"))					// a few runs by hand: where the CPU goes
		for (int k = 0; k < atoi (getenv ("GC_STEPS")); k++)
		{
			u64 c0 = m.cycles;
			m.run (m.cycles + 20000); m.events ();
			printf ("  run: pc %08X, cycles +%llu, until %llu, msr %08X, reads %08X x%u, dec %08X at %lld\n", m.pc, m.cycles - c0, m.jitUntil, m.msr, m.hwLastRead, m.hwLastReadN, m.decRead (), (long long) (m.decAt == ~0ull ? -1 : (long long) (m.decAt - m.cycles)));
		}
	char s[600]; m.status (s, sizeof s); printf ("end: %s\n", s);
	printf ("PI irqs:"); for (int i = 0; i < 14; i++) printf (" %u", m.irqCount[i]); printf ("\n");
	printf ("gx cmds %u prims %u copies %u indirect %u, textures decoded %u, idle skips %u, JIT blocks %u (%u translated)", m.gxCmds, m.gxPrims, m.gxCopies, m.gxIndirect, m.texDecodes, m.idleSkips, m.jitBlocks, m.jitCompiles); putchar (10);
	printf ("regs: lr %08X ctr %08X msr %08X srr0 %08X srr1 %08X cr %08X\n", m.lr, m.ctr, m.msr, m.srr0, m.srr1, m.cr);
	for (int i = 0; i < 32; i++) printf ("r%-2d %08X%s", i, m.gpr[i], (i & 7) == 7 ? "\n" : "  ");
	printf ("VI:"); for (int i = 0; i < 0x40; i++) printf ("%s%04X", (i & 15) == 0 ? "\n  " : " ", m.vi[i]); printf ("\n  viLine %d / %d, cycles/line %llu\n", m.viLine, m.viLinesFrame, m.viCyclesLine);
	printf ("DSP regs:"); for (int i = 0; i < 0x20; i++) printf (" %04X", m.dspReg[i]); printf ("\n");
	if (getenv ("GC_JITDUMP"))					// a block's host code -> jitblock.bin
	{
		const u32 *code; u32 words;
		if (m.jitCode ((u32) strtoul (getenv ("GC_JITDUMP"), 0, 16), code, words))
		{
			FILE *jf = fopen ("jitblock.bin", "wb"); fwrite (code, 4, words, jf); fclose (jf);
			printf ("block %s: %u bytes at %p -> jitblock.bin\n", getenv ("GC_JITDUMP"), words * 4, (const void *) code);
		}
		else printf ("block %s: none\n", getenv ("GC_JITDUMP"));
	}
	ppm ("last_xfb.ppm", m.fb, m.fbW, m.fbH);
	if (m.gpu) glPpm ("last_gx.ppm"); else gfxPpm ("last_gx.ppm", 1);
	if (getenv ("GC_DUMP")) { FILE *d = fopen (getenv ("GC_DUMP"), "wb"); fwrite (m.mem1, 1, MEM1_SIZE, d); fclose (d); printf ("mem1 -> %s\n", getenv ("GC_DUMP")); }
	return 0;
}
