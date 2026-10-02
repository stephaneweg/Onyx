//
// hostkapi.cpp -- a stand-in kernel for the GPU compositing service (user/gpucomp) on the PC: the
// kapi table at its fixed address with what gpucomp and gpcdemo call, and -- GPC_SOFTGPU=1 -- a
// software V3D behind gpu_info / gpu_texture / gpu_texture_rect / gpu_render / gpu_vbuf, made as
// the kernel's (sys/v3d.cpp) v53 frame is: the viewport of its binning list (x y scaled by the
// target's integer half sizes, 24.8 fixed point), the pixel centres covered by each triangle (a
// top-left rule for the edges), the ten varyings interpolated (w = 1), the kernel's own FS_TEX
// (kernel/sys/v3d_shaders.inc) run in the QPU simulator (tools/qpu/qpusim: texel x colour, the
// TMU's bilinear / nearest, clamp), the blend of the batch (none, premultiplied) in the tile
// buffer, and the store as the direct mode does (the top byte kept unless KAPI_GPU_F_ALPHA; a
// cleared target's is 0, or clear's with F_ALPHA). GPC_SOFTGPU=hang: gpu_render answers -3 (the
// GPU hung) from its second frame on. hostkapi_refuse (n): the next n new textures refused (-4). Arguments: /proc/self/cmdline (kapi get_args).
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/mman.h>
#include <pthread.h>
#include <vector>
#include <map>
#include <assert.h>
#include <kern/kapi_abi.h>
#include <kern/v3d_cl.h>		// (the kernel's packets: its target's load and store, decoded below)
#include "qpusim.h"

#include "../../../kernel/sys/v3d_shaders.inc"

static TKApiTable *T;
static int g_soft = 0, g_hang = 0, g_frames = 0, g_refuse = 0;

static void unimplemented (void) { fprintf (stderr, "hostkapi: an unimplemented kapi was called\n"); exit (2); }

static unsigned h_get_ticks (void)
{
	struct timespec ts; clock_gettime (CLOCK_MONOTONIC, &ts);
	return (unsigned) (ts.tv_sec * 100 + ts.tv_nsec / 10000000);
}
static int h_stdout_write (const void *b, unsigned n) { return (int) fwrite (b, 1, n, stdout); }
static int h_get_args (char *b, unsigned n)
{
	FILE *f = fopen ("/proc/self/cmdline", "rb"); if (!f) { if (n) b[0] = 0; return 0; }
	char buf[4096]; size_t k = fread (buf, 1, sizeof buf - 1, f); fclose (f); buf[k] = 0;
	size_t i = strlen (buf) + 1, o = 0;				// (past the program's name)
	for (; i < k && o + 1 < n; i++) b[o++] = buf[i] ? buf[i] : ' ';
	while (o > 0 && b[o - 1] == ' ') o--;
	if (n) b[o] = 0;
	return (int) o;
}
static void *h_memcpy (void *d, const void *s, unsigned long n) { return memcpy (d, s, n); }
static void *h_memset (void *d, int c, unsigned long n) { return memset (d, c, n); }
static void *h_memmove (void *d, const void *s, unsigned long n) { return memmove (d, s, n); }
static void *h_sbrk (long inc) { static char *brk = 0; if (!brk) brk = (char *) malloc (1 << 30); char *p = brk; brk += inc; return p; }
static unsigned *g_canvas; static int g_cw, g_ch;
static unsigned *h_create_window (int w, int h, const char *) { g_cw = w; g_ch = h; g_canvas = (unsigned *) calloc ((size_t) w * h, 4); return g_canvas; }
static void h_present (void) {}
static void h_pump (void) {}
static int g_exitAfter = 0;
static int h_should_exit (void) { return ++g_exitAfter > 120; }
static void h_msleep (unsigned) {}
static void h_yield (void) {}

// ---- the software V3D ----------------------------------------------------------------------------------------
struct STex { int w, h; std::vector<unsigned> px; };	// 0xAARRGGBB as given
static std::map<int, STex> g_tex;
static int g_nextTex = 0;

static int h_gpu_info (char *b, unsigned n)
{
	if (b && n) snprintf (b, n, g_soft ? "V3D 4.2 (software, the PC)" : "no GPU (the PC)");
	return g_soft ? 1 : 0;
}
static int h_gpu_texture (int handle, const unsigned *px, int w, int h, int stride)
{
	if (!g_soft) return -1;
	if (handle >= 0)
	{
		if (!g_tex.count (handle)) return -2;
		if (!px) { g_tex.erase (handle); return 0; }
	}
	if (!px || w <= 0 || h <= 0 || w > KAPI_GPU_MAX_TEXSIZE || h > KAPI_GPU_MAX_TEXSIZE || stride < w) return -2;
	if (handle < 0 && g_refuse > 0) { g_refuse--; return -4; }	// (hostkapi_refuse: no handle / memory)
	if (handle < 0) handle = g_nextTex++;
	STex &t = g_tex[handle];
	t.w = w; t.h = h; t.px.resize ((size_t) w * h);
	for (int y = 0; y < h; y++) memcpy (&t.px[(size_t) y * w], px + (long) y * stride, (size_t) w * 4);
	return handle;
}
static int h_gpu_texture_rect (int handle, int x, int y, int w, int h, const unsigned *px, int stride)
{
	if (!g_soft) return -1;
	if (!g_tex.count (handle)) return -2;
	STex &t = g_tex[handle];
	if (!px || w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > t.w || y + h > t.h || stride < w) return -2;
	for (int r = 0; r < h; r++) memcpy (&t.px[(size_t) (y + r) * t.w + x], px + (long) r * stride, (size_t) w * 4);
	return 0;
}
static void *h_gpu_vbuf (unsigned n) { return g_soft ? calloc (1, n) : 0; }

// a packet's field at the bits the V3D reads it from (Mesa's v3d_packet.xml: the bits after the opcode)
template <class P> static unsigned long long pk_field (const P &p, int start, int size)
{
	unsigned char b[sizeof p]; memcpy (b, &p, sizeof p);
	unsigned long long v = 0;
	for (int i = 0; i < size; i++)
	{
		int k = start + i;
		if (1 + k / 8 < (int) sizeof p && ((b[1 + k / 8] >> (k % 8)) & 1)) v |= 1ull << i;
	}
	return v;
}

// the edge a -> b (1/256 pixel units), the point p: > 0 inside for a counter-clockwise triangle in y down
static long long edge (long long ax, long long ay, long long bx, long long by, long long px, long long py)
{ return (bx - ax) * (py - ay) - (by - ay) * (px - ax); }

static int h_gpu_render (const kapi_gpu_frame *f, const kapi_gpu_vertex3 *v, unsigned nv, const kapi_gpu_batch *b, unsigned nb)
{
	if (!g_soft) return -1;
	if (g_hang && g_frames++ >= 1) return -3;
	int W = f->w, H = f->h;
	if (!f->pixels || W <= 0 || H <= 0 || W > 2048 || H > 2048 || f->stride < W) return -2;
	bool alpha = (f->flags & KAPI_GPU_F_ALPHA) != 0, keep = (f->flags & KAPI_GPU_F_KEEP) != 0;
	// the target's load and store: the kernel's own packets (kern/v3d_cl.h V3dLoadTarget / V3dStoreTarget,
	// as sys/v3d.cpp BuildRCL emits them for a canvas), their stride and format read where the V3D reads
	// them -- a packet laid out wrong loads / stores the wrong rows here too
	LoadTileBufferGeneral ldp = V3dLoadTarget (true, alpha, (u32) f->stride * 4, 0);
	StoreTileBufferGeneral stp = V3dStoreTarget (true, (u32) f->stride * 4, 0);
	long ldStride = (long) pk_field (ldp, 28, 20) / 4, stStride = (long) pk_field (stp, 28, 20) / 4;
	if (pk_field (ldp, 12, 6) != V3D_OUTPUT_IMAGE_FORMAT_RGBA8 || pk_field (stp, 12, 6) != V3D_OUTPUT_IMAGE_FORMAT_RGBA8 ||
	    pk_field (ldp, 4, 3) != V3D_TILING_RASTER || pk_field (stp, 4, 3) != V3D_TILING_RASTER ||
	    pk_field (ldp, 20, 1) != 1 || pk_field (stp, 20, 1) != 1)
	{ fprintf (stderr, "hostkapi: the kernel's target packets are not raster RGBA8, R / B swapped\n"); return -2; }
	if (stStride < W) { fprintf (stderr, "hostkapi: the kernel's store packet: a stride of %ld pixels\n", stStride); return -2; }
	// the tile buffer: RGBA floats 0..1
	std::vector<float> tb ((size_t) W * H * 4);
	std::vector<unsigned char> top ((size_t) W * H);		// (the stored top byte when alpha is not written)
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
		{
			unsigned c = keep ? f->pixels[(long) y * ldStride + x] : (alpha ? f->clear : f->clear & 0xFFFFFF);
			float *p = &tb[((size_t) y * W + x) * 4];
			p[0] = ((c >> 16) & 255) / 255.0f; p[1] = ((c >> 8) & 255) / 255.0f; p[2] = (c & 255) / 255.0f; p[3] = (c >> 24) / 255.0f;
			top[(size_t) y * W + x] = (unsigned char) (c >> 24);
		}
	float hw = (float) (W / 2), hh = (float) (H / 2);			// (the kernel's integer halves)
	for (unsigned bi = 0; bi < nb; bi++)
	{
		const kapi_gpu_batch &B = b[bi];
		if (!(B.flags & KAPI_GPU_B_NOMATRIX) || B.texture < 0 || !g_tex.count (B.texture) || B.first + B.count > nv) return -2;
		const STex &st = g_tex[B.texture];
		qpusim::Run run;
		run.uniforms.push_back (0x10000u | 3); run.uniforms.push_back (0x20000u);
		qpusim::Texture qt; qt.w = st.w; qt.h = st.h;
		qt.wrapS = (int) ((B.flags >> 13) & 3); qt.wrapT = (int) ((B.flags >> 15) & 3);
		qt.linear = (B.flags & KAPI_GPU_B_LINEAR) != 0;
		for (unsigned c : st.px) qt.px.push_back ((c & 0xFF00FF00) | ((c >> 16) & 255) | ((c & 255) << 16));
		run.textures[0x10000u] = qt;
		unsigned blend = (B.flags >> 8) & 15;
		for (unsigned t = 0; t + 3 <= B.count; t += 3)
		{
			const kapi_gpu_vertex3 *p[3] = { &v[B.first + t], &v[B.first + t + 1], &v[B.first + t + 2] };
			long long X[3], Y[3];
			for (int k = 0; k < 3; k++)
			{
				X[k] = llrintf ((p[k]->x * hw + hw) * 256.0f);
				Y[k] = llrintf ((-p[k]->y * hh + hh) * 256.0f);
			}
			long long area = edge (X[0], Y[0], X[1], Y[1], X[2], Y[2]);
			if (area == 0) continue;
			int o[3] = { 0, 1, 2 };
			if (area < 0) { o[1] = 2; o[2] = 1; area = -area; }
			long long x0 = std::min (X[0], std::min (X[1], X[2])), x1 = std::max (X[0], std::max (X[1], X[2]));
			long long y0 = std::min (Y[0], std::min (Y[1], Y[2])), y1 = std::max (Y[0], std::max (Y[1], Y[2]));
			int px0 = (int) std::max (0LL, x0 / 256 - 1), px1 = (int) std::min ((long long) W, x1 / 256 + 2);
			int py0 = (int) std::max (0LL, y0 / 256 - 1), py1 = (int) std::min ((long long) H, y1 / 256 + 2);
			std::vector<qpusim::Pixel> pix; std::vector<int> at;
			for (int y = py0; y < py1; y++)
				for (int x = px0; x < px1; x++)
				{
					long long cx = x * 256LL + 128, cy = y * 256LL + 128;
					long long e[3]; bool in = true;
					for (int k = 0; k < 3; k++)
					{
						int a = o[k], c = o[(k + 1) % 3];
						e[k] = edge (X[a], Y[a], X[c], Y[c], cx, cy);
						// top-left rule (y down, this winding): a top edge (horizontal, going right)
						// or a left edge (going up) owns its pixel centres
						long long dx = X[c] - X[a], dy = Y[c] - Y[a];
						bool tl = (dy == 0 && dx > 0) || dy < 0;
						if (e[k] < 0 || (e[k] == 0 && !tl)) { in = false; break; }
					}
					if (!in) continue;
					// barycentrics: the weight of vertex o[k] is the edge opposite to it
					double l[3];
					l[o[2]] = (double) e[0] / area; l[o[0]] = (double) e[1] / area; l[o[1]] = (double) e[2] / area;
					qpusim::Pixel q; q.nTlb = 0; q.written = false;
					float va[10];
					for (int j = 0; j < 10; j++) va[j] = 0;
					for (int k = 0; k < 3; k++)
					{
						const kapi_gpu_vertex3 *s = p[k];
						float in10[10] = { s->s, s->t, s->r / 255.0f, s->g / 255.0f, s->b / 255.0f, s->a / 255.0f,
								   s->r2 / 255.0f, s->g2 / 255.0f, s->b2 / 255.0f, s->a2 / 255.0f };
						for (int j = 0; j < 10; j++) va[j] += (float) (l[k] * in10[j]);
					}
					for (int j = 0; j < 10; j++) q.vary.push_back (va[j]);
					pix.push_back (q); at.push_back (y * W + x);
				}
			if (pix.empty ()) continue;
			if (!qpusim::runFragment ((const uint64_t *) FS_TEX, (int) (sizeof FS_TEX / 8), pix, run))
			{ fprintf (stderr, "hostkapi: the simulator: %s\n", run.error.c_str ()); return -3; }
			for (size_t i = 0; i < pix.size (); i++)
			{
				if (!pix[i].written) continue;
				float s4[4]; for (int c = 0; c < 4; c++) s4[c] = (float) ((pix[i].rgba >> (c * 8)) & 255) / 255.0f;
				float *d = &tb[(size_t) at[i] * 4];
				float r4[4];
				// the kernel's table (sys/v3d.cpp): colour src, dst, equation, alpha src, dst, equation
				static const unsigned char Fac[KAPI_GPU_BLEND_LAST + 1][6] = {
					{ 1, 0, 0, 1, 0, 0 }, { 6, 7, 0, 1, 7, 0 }, { 6, 1, 0, 6, 1, 0 }, { 4, 0, 0, 4, 0, 0 },
					{ 1, 7, 0, 1, 7, 0 }, { 4, 7, 0, 0, 1, 0 }, { 9, 1, 0, 1, 7, 0 }, { 1, 3, 0, 1, 7, 0 },
					{ 1, 1, 0, 1, 7, 0 }, { 1, 1, 2, 0, 1, 0 }, { 1, 1, 4, 1, 7, 0 }, { 0, 6, 0, 0, 6, 0 },
					{ 0, 7, 0, 0, 7, 0 } };
				if (blend > KAPI_GPU_BLEND_LAST || blend == 1 || blend == 2 || blend == 3) return -2;
				if (blend == KAPI_GPU_BLEND_NONE) for (int c = 0; c < 4; c++) r4[c] = s4[c];
				else
				{
					float dd[4] = { d[0], d[1], d[2], alpha ? d[3] : 1.0f };
					auto fac = [&] (int f, int c) -> float {
						switch (f) { case 0: return 0; case 1: return 1; case 3: return 1 - s4[c]; case 4: return dd[c];
							     case 6: return s4[3]; case 7: return 1 - s4[3]; case 9: return 1 - dd[3]; }
						return 0; };
					for (int c = 0; c < 4; c++)
					{
						const unsigned char *f = Fac[blend] + (c < 3 ? 0 : 3);
						float a = s4[c] * fac (f[0], c), b = dd[c] * fac (f[1], c);
						r4[c] = f[2] == 2 ? b - a : f[2] == 4 ? std::max (s4[c], dd[c]) : a + b;
					}
				}
				for (int c = 0; c < 4; c++) { float x = r4[c] < 0 ? 0 : r4[c] > 1 ? 1 : r4[c]; r4[c] = roundf (x * 255.0f) / 255.0f; }
				d[0] = r4[0]; d[1] = r4[1]; d[2] = r4[2];
				if (alpha) d[3] = r4[3];				// (else the write mask keeps it)
			}
		}
	}
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
		{
			const float *p = &tb[((size_t) y * W + x) * 4];
			unsigned c = (unsigned) lrintf (p[0] * 255) << 16 | (unsigned) lrintf (p[1] * 255) << 8 | (unsigned) lrintf (p[2] * 255);
			c |= (alpha ? (unsigned) lrintf (p[3] * 255) : top[(size_t) y * W + x]) << 24;
			f->pixels[(long) y * stStride + x] = c;
		}
	return 0;
}

// ---- threads and events (kapi v67), on pthreads: what gpucomp's GPC_F_ASYNC uses -----------------------------
struct HEvent { pthread_mutex_t m; pthread_cond_t c; int set, manual; };
static std::vector<HEvent *> g_events (1, (HEvent *) 0);		// (handles from 1)
static int h_event_create (int manual, int initial)
{
	HEvent *e = new HEvent; pthread_mutex_init (&e->m, 0); pthread_cond_init (&e->c, 0); e->set = initial; e->manual = manual;
	g_events.push_back (e); return (int) g_events.size () - 1;
}
static int h_event_set (int h)
{
	HEvent *e = g_events[h]; pthread_mutex_lock (&e->m); e->set = 1; pthread_cond_broadcast (&e->c); pthread_mutex_unlock (&e->m); return 0;
}
static int h_event_wait (int h, unsigned)
{
	HEvent *e = g_events[h]; pthread_mutex_lock (&e->m);
	while (!e->set) pthread_cond_wait (&e->c, &e->m);
	if (!e->manual) e->set = 0;
	pthread_mutex_unlock (&e->m); return 0;
}
static int h_sync_close (int) { return 0; }
struct HThread { pthread_t th; int (*fn) (void *); void *arg; int code; };
static std::vector<HThread *> g_threads (2, (HThread *) 0);		// (tids from 2)
static void *h_thread_main (void *p) { HThread *t = (HThread *) p; t->code = t->fn (t->arg); return 0; }
static int h_thread_create (int (*fn) (void *), void *arg, unsigned, const char *)
{
	HThread *t = new HThread; t->fn = fn; t->arg = arg; t->code = 0;
	if (pthread_create (&t->th, 0, h_thread_main, t) != 0) { delete t; return -1; }
	g_threads.push_back (t); return (int) g_threads.size () - 1;
}
static int h_thread_join (int tid, unsigned, int *code)
{
	HThread *t = g_threads[tid]; pthread_join (t->th, 0); if (code) *code = t->code; return 0;
}

// ---- the table -------------------------------------------------------------------------------------------------
extern "C" void hostkapi_soft (int on) { g_soft = on; g_hang = 0; g_frames = 0; }
extern "C" void hostkapi_hang (void) { g_hang = 1; g_frames = 0; }
extern "C" void hostkapi_refuse (int n) { g_refuse = n; }
extern "C" int hostkapi_textures (void) { return (int) g_tex.size (); }

// NetSurf on the desktop simulator (tools/tests/netsurf/host.mk SOFTGPU=1, -DHOSTKAPI_GPU_ONLY): fakekapi.cpp
// makes the table, then this puts the software V3D's calls into it (GPC_SOFTGPU=1 at run time)
extern "C" void hostkapi_install_gpu (TKApiTable *t)
{
	T = t;
	T->gpu_info = h_gpu_info; T->gpu_texture = h_gpu_texture; T->gpu_texture_rect = h_gpu_texture_rect;
	T->gpu_render = h_gpu_render; T->gpu_vbuf = h_gpu_vbuf;
	const char *e = getenv ("GPC_SOFTGPU");
	if (e && *e && strcmp (e, "0") != 0) { g_soft = 1; if (strcmp (e, "hang") == 0) g_hang = 1; }
}

#ifndef HOSTKAPI_GPU_ONLY
static struct Setup
{
	Setup ()
	{
		void *p = mmap ((void *) KAPI_TABLE_VA, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
		if (p == MAP_FAILED) { perror ("mmap"); exit (1); }
		T = (TKApiTable *) p;
		void **slots = (void **) T;
		for (size_t i = 0; i < sizeof (TKApiTable) / sizeof (void *); i++) slots[i] = (void *) unimplemented;
		T->version = KAPI_ABI_VERSION;
		T->get_ticks = h_get_ticks; T->stdout_write = h_stdout_write; T->get_args = h_get_args;
		T->memcpy = h_memcpy; T->memset = h_memset; T->memmove = h_memmove; T->sbrk = h_sbrk;
		T->create_window = h_create_window; T->present = h_present; T->pump_events = h_pump;
		T->should_exit = h_should_exit; T->msleep = h_msleep; T->yield = h_yield;
		T->gpu_info = h_gpu_info; T->gpu_texture = h_gpu_texture; T->gpu_texture_rect = h_gpu_texture_rect;
		T->gpu_render = h_gpu_render; T->gpu_vbuf = h_gpu_vbuf;
		T->event_create = h_event_create; T->event_set = h_event_set; T->event_wait = h_event_wait; T->sync_close = h_sync_close;
		T->thread_create = h_thread_create; T->thread_join = h_thread_join;
		const char *e = getenv ("GPC_SOFTGPU");
		if (e && *e && strcmp (e, "0") != 0) { g_soft = 1; if (strcmp (e, "hang") == 0) g_hang = 1; }
	}
} s_setup;
#endif
