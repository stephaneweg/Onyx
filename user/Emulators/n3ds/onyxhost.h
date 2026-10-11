//
// n3ds/onyxhost.h -- the Nintendo 3DS core's host on Onyx: what a program that runs the core there gives it
// (the emulator's app user/Apps/n3dsemu, the test runner tools/tests/n3ds/n3dstest.cpp). One translation unit
// includes it.
//
//   kfileOpen (path, &source)   the game's file read piece by piece through AppKit (Machine::loadFrom)
//   n3ds_onyx_setup (machine)   one picture a screen (Machine::monoOnly); the fragments on the V3D when there is
//                               one (Machine::gpuDraw: the recorded frames drawn with kapi_gpu_render3, the stock
//                               vertex shaders with the core's generated fragment shaders, the decoded textures
//                               given as they change; a draw's own vertex and coordinate shaders when the core made
//                               them -- Machine::gpuVS, g_useVS = 0: never); the free application cores as the renderer's helpers
//                               (Machine::parallelBegin / Done / End) -- g_useGpu = 0 before it: no GPU
//   n3ds_onyx_shutdown ()       the application cores given back
// The machine itself runs on the calling thread (its processor's JIT and its services allocate and call the
// system: an application core can do neither).
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _n3ds_onyxhost_h
#define _n3ds_onyxhost_h
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "appkit/appkit.h"
#include "v3d/qpu.h"
#include "v3d/shaders.h"
#include "n3ds/n3ds.h"

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
static unsigned long long hostMicros (void) { struct timeval tv; gettimeofday (&tv, 0); return (unsigned long long) tv.tv_sec * 1000000ull + (unsigned long long) tv.tv_usec; }
static int g_useGpu = 1, g_useVS = 1;		// (g_useVS 2: the vertex programs' batches not indexed)
static unsigned g_vsDebug;			// (Machine::vsDebug)
static int g_vsOn71;				// (the vertex programs on a V3D 7.1 too: not tried on a Pi 5 yet -- off unless asked)
// (prog, vprog: 0 not made, -1 refused, else the kernel's handle + 1; their `last`: the frame that used them)
static struct { int prog[256]; unsigned progLast[256]; int vprog[512]; unsigned vprogLast[512]; int tex[96]; unsigned texSerial[96]; bool texMade[96]; bool off; int said; unsigned calls, refused[4], replaced; unsigned long long usPrep, usKernel, floats, batches; } g_gpu;
// A program made. The kernel's table is full: the one used longest ago (not in this frame) gives its place.
static int onyxProgram (const struct kapi_gpu_program *kp)
{
	int h = kapi_gpu_program (-1, kp);
	if (h != -4) return h;
	int *victim = 0; unsigned oldest = g_gpu.calls;
	for (int i = 0; i < 256; i++) if (g_gpu.prog[i] > 0 && g_gpu.progLast[i] < oldest) { oldest = g_gpu.progLast[i]; victim = &g_gpu.prog[i]; }
	for (int i = 0; i < 512; i++) if (g_gpu.vprog[i] > 0 && g_gpu.vprogLast[i] < oldest) { oldest = g_gpu.vprogLast[i]; victim = &g_gpu.vprog[i]; }
	if (!victim) return h;
	h = kapi_gpu_program (*victim - 1, kp);
	if (h >= 0) { *victim = 0; g_gpu.replaced++; }
	return h;
}
static bool onyxDraw (void *, const n3ds::GpuFrame *f, n3ds::u32 *pixels)
{
	g_gpu.calls++;
	const unsigned long long tIn = hostMicros ();
	if (g_gpu.off || f->nb > 2048 || f->nu > (1u << 18)) { g_gpu.refused[0]++; return false; }
	static qpu::Prog vs, cs; static bool csMade;
	static struct kapi_gpu_batch3 rb[2048];
	static unsigned ru[4 + (1 << 18)];
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
		struct kapi_gpu_batch3 &o = rb[i];
		memset (&o, 0, sizeof o);
		if (b.vprog >= 0)						// its own vertex and coordinate shaders, with this fragment shader
		{
			if (b.vprog >= 512 || (n3ds::u32) b.vprog >= f->nVPrograms) return false;
			if (!g_gpu.vprog[b.vprog])
			{
				const n3ds::GpuProgramInfo &P = f->programs[b.program];
				const n3ds::GpuVProgramInfo &V = f->vprograms[b.vprog];
				struct kapi_gpu_program kp;
				memset (&kp, 0, sizeof kp);
				kp.vs = V.vs; kp.nvs = V.nvs; kp.cs = V.cs; kp.ncs = V.ncs; kp.fs = P.words; kp.nfs = P.nWords;
				kp.inputs = V.inputs; kp.csInputs = V.csInputs; kp.csOutputs = 6; kp.varyings = P.nVary; kp.flags = qpu::programFlags (P.flags | KAPI_GPU_P_VS_1WAY);
				const int h = onyxProgram (&kp);
				g_gpu.vprog[b.vprog] = h >= 0 ? h + 1 : -1;
				if (h < 0) fprintf (stderr, "gpu_program: %d (a vertex shader of %u instructions, %u inputs)%c", h, (unsigned) V.nvs, (unsigned) V.inputs, 10);
			}
			if (g_gpu.vprog[b.vprog] < 0) { g_gpu.refused[1]++; return false; }
			g_gpu.vprogLast[b.vprog] = g_gpu.calls;
			o.b.program = g_gpu.vprog[b.vprog] - 1;
			o.b.vsUni = 4 + b.vsUni; o.b.vsNUni = b.vsNUni; o.b.csUni = 4 + b.csUni; o.b.csNUni = b.csNUni;
		}
		else
		{
		if (!g_gpu.prog[b.program])
		{
			const n3ds::GpuProgramInfo &P = f->programs[b.program];
			vs.n = 0; vs.bad = false; qpu::passVS (vs, 4 + (int) P.nVary);
			struct kapi_gpu_program kp;
			memset (&kp, 0, sizeof kp);
			kp.vs = vs.words (); kp.nvs = (unsigned) vs.count (); kp.cs = cs.words (); kp.ncs = (unsigned) cs.count ();
			kp.fs = P.words; kp.nfs = P.nWords;
			kp.inputs = 4 + P.nVary; kp.csInputs = 4; kp.csOutputs = 6; kp.varyings = P.nVary; kp.flags = qpu::programFlags (P.flags);
			const int h = onyxProgram (&kp);
			g_gpu.prog[b.program] = h >= 0 ? h + 1 : -1;
			if (h < 0) fprintf (stderr, "gpu_program: %d (%u instructions, %u varyings)%c", h, (unsigned) P.nWords, (unsigned) P.nVary, 10);
		}
		if (g_gpu.prog[b.program] < 0) { g_gpu.refused[1]++; return false; }
		g_gpu.progLast[b.program] = g_gpu.calls;
		o.b.program = g_gpu.prog[b.program] - 1;
		o.b.vsUni = 0; o.b.vsNUni = 4; o.b.csUni = 0; o.b.csNUni = 2;
		}
		o.b.first = b.nVerts;					// (an indexed batch's vertices: KAPI_GPU_B_INDEXED)
		o.b.count = b.count; o.b.flags = b.flags; o.b.blend = b.blend; o.b.wmask = b.wmask;
		for (int k = 0; k < 4; k++) o.b.scissor[k] = b.scissor[k];
		if (g_vsDebug & 128) { o.b.scissor[0] = o.b.scissor[1] = 0; o.b.scissor[2] = o.b.scissor[3] = 1; }	// (to time what is not the fragments: one pixel drawn)
		o.b.fsUni = 4 + b.uni; o.b.fsNUni = b.nUni;
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
	const unsigned long long tCall = hostMicros ();
	const int ret = kapi_gpu_render3 (&fr, f->v, f->nFloats, rb, f->nb, ru, 4 + f->nu, 0);
	g_gpu.usPrep += tCall - tIn; g_gpu.usKernel += hostMicros () - tCall; g_gpu.floats += f->nFloats; g_gpu.batches += f->nb;
	if (ret != 0)
	{
		if (g_gpu.said++ < 4) fprintf (stderr, "gpu_render3: %d (%u batches, %u floats)%c", ret, (unsigned) f->nb, (unsigned) f->nFloats, 10);
		if (ret == -1 || ret == -3) g_gpu.off = true;			// (no GPU, or it did not finish: left alone from now on)
		g_gpu.refused[3]++;
		return false;
	}
	return true;
}

static void gpuSay (void) { if (g_gpu.calls) fprintf (stderr, "the GPU was given %u frames; refused: %u too big or off, %u a program, %u a texture, %u the render; %u programs gave their place to another; %.2f s preparing them, %.2f s in the kernel's call; %llu batches and %llu floats a frame%c", g_gpu.calls, g_gpu.refused[0], g_gpu.refused[1], g_gpu.refused[2], g_gpu.refused[3], g_gpu.replaced, (double) g_gpu.usPrep / 1e6, (double) g_gpu.usKernel / 1e6, g_gpu.batches / g_gpu.calls, g_gpu.floats / g_gpu.calls, 10); }
static void (*g_atEnd) (void);
// The machine set up for Onyx: one picture a screen, the GPU when there is one (g_useGpu), the free application cores.
static void n3ds_onyx_setup (n3ds::Machine *m)
{
	g_atEnd = gpuSay;
	m->monoOnly = true;							// (Onyx shows one picture a screen)
	char gpu[96];
	if (g_useGpu && kapi_gpu_info (gpu, sizeof gpu) > 0 && qpu::setVersion (qpu::versionOf (gpu)))
	{
		m->gpuDraw = onyxDraw;
		// (the batches with their own vertex shaders: a kernel that knows them -- kapi v98)
		m->gpuVS = g_useVS != 0 && kapi_abi_version () >= 98 && (qpu::version () < 71 || g_vsOn71); m->gpuIndexed = m->gpuVS && g_useVS == 1; m->vsDebug = g_vsDebug; m->gpuSubpixels = qpu::version () >= 71 ? 64 : 256;
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

// The application cores given back.
static void n3ds_onyx_shutdown (void)
{
	g_par.stop = 1;
	__asm__ volatile ("dsb ish; sev" ::: "memory");
	for (int i = 0; i < g_par.n; i++) kapi_core_release (g_par.cores[i]);
	g_par.n = 0;
}

#endif
