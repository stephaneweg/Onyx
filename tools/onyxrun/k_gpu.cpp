//
// k_gpu.cpp -- the GPU's stock calls (kapi v52 / v53 / v70: gpu_info, gpu_draw, gpu_texture, gpu_texture_rect,
// gpu_render), drawn by the PC itself: what kernel/sys/v3d.cpp asks the V3D for, done by a rasteriser in native
// code, the frame cut in bands, one a host thread -- so a program's GPU path runs at the PC's speed instead of its
// CPU path emulated. (docs/APP-RUNNER-STUDY.md section 4: G1, natively; the apps' own QPU programs, gpu_program /
// gpu_render2 / render3, are not here yet: gpu_program answers -1, a program then takes its CPU path.)
//
// As the kernel's: vertices transformed by the batch's matrix into clip space (row by row), clipped (the near and
// far planes; the screen by the scissor), divided by w, y up; the depth test (LESS by default) on a depth buffer of
// the frame; the colour = texel x vertex colour + colour 2 (v54), clamped; alpha test; the blend presets of
// v3d.cpp's table (the V3D's factors and equations), the target's alpha kept with KAPI_GPU_F_ALPHA.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <algorithm>

// ---- the textures (a process's) -----------------------------------------------------------------------------------
struct Tex { int w = 0, h = 0; std::vector<u32> px; };	// 0xAARRGGBB
struct GpuState { std::mutex m; std::map<int, Tex> tex; int next = 0; };
static std::mutex s_GM;
static std::map<int, GpuState *> s_Gpu;				// by pid

static GpuState &GS (Proc *P)
{
	std::lock_guard<std::mutex> L (s_GM);
	GpuState *&g = s_Gpu[P->pid];
	if (!g) g = new GpuState ();
	return *g;
}

void gpu_proc_gone (Proc *P)
{
	std::lock_guard<std::mutex> L (s_GM);
	auto it = s_Gpu.find (P->pid);
	if (it != s_Gpu.end ()) { delete it->second; s_Gpu.erase (it); }
}

#define GPU_MAX_TEXSIZE	4096

static int k_gpu_info (u64 buf, unsigned cap)
{
	gstr_out (buf, cap, "V3D 4.2 (onyxrun: drawn by the PC)");
	return 1;
}

static int k_gpu_texture (int handle, u64 pixels, int w, int h, int stride)
{
	Proc *P = cur ();
	GpuState &G = GS (P);
	std::lock_guard<std::mutex> L (G.m);
	if (pixels == 0)
	{
		if (handle >= 0) G.tex.erase (handle);
		return 0;
	}
	if (w < 1 || h < 1 || w > GPU_MAX_TEXSIZE || h > GPU_MAX_TEXSIZE || stride < w) return -2;
	const u32 *src = (const u32 *) GB (pixels, ((u64) (h - 1) * stride + w) * 4, MEM_R);
	if (!src) return -2;
	if (handle < 0)
	{
		if (G.tex.size () >= KAPI_GPU_MAX_TEXTURES) return -4;
		while (G.tex.count (G.next)) G.next++;
		handle = G.next++;
	}
	else if (!G.tex.count (handle)) return -2;
	Tex &t = G.tex[handle];
	t.w = w; t.h = h;
	t.px.resize ((size_t) w * h);
	for (int y = 0; y < h; y++) memcpy (&t.px[(size_t) y * w], src + (size_t) y * stride, (size_t) w * 4);
	return handle;
}

static int k_gpu_texture_rect (int handle, int x, int y, int w, int h, u64 pixels, int stride)
{
	Proc *P = cur ();
	GpuState &G = GS (P);
	std::lock_guard<std::mutex> L (G.m);
	auto it = G.tex.find (handle);
	if (it == G.tex.end ()) return -2;
	Tex &t = it->second;
	if (x < 0 || y < 0 || w < 1 || h < 1 || x + w > t.w || y + h > t.h || stride < w) return -2;
	const u32 *src = (const u32 *) GB (pixels, ((u64) (h - 1) * stride + w) * 4, MEM_R);
	if (!src) return -2;
	for (int r = 0; r < h; r++) memcpy (&t.px[(size_t) (y + r) * t.w + x], src + (size_t) r * stride, (size_t) w * 4);
	return 0;
}

// ---- the rasteriser ------------------------------------------------------------------------------------------------
struct V { float x, y, z, w; float a[10]; };		// clip position, then s t r g b a r2 g2 b2 a2
#define NA 10

struct Batch
{
	const Tex *tex;
	unsigned flags;
	std::vector<V> tris;		// clipped, in screen space: x y in pixels, z in 0..1 (ndc z / 2 + 0.5), w = 1/w_clip, a[] / w_clip
};

struct Target
{
	u32 *px; int w, h, stride;		// (the frame's own buffer -- the tile buffer --; dst its place in the program)
	u32 *dst; int dstStride;
	std::vector<u32> buf;
	bool alpha;			// KAPI_GPU_F_ALPHA
	std::vector<float> depth;
};

static inline float clamp01 (float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static void wrap (int &i, int n, int mode)
{
	if (mode == KAPI_GPU_WRAP_CLAMP) { i = i < 0 ? 0 : i >= n ? n - 1 : i; return; }
	if (mode == KAPI_GPU_WRAP_MIRROR)
	{
		int p = n * 2;
		i %= p; if (i < 0) i += p;
		if (i >= n) i = p - 1 - i;
		return;
	}
	i %= n; if (i < 0) i += n;
}

static inline void unpack (u32 c, float o[4])		// 0xAARRGGBB -> r g b a
{
	o[0] = ((c >> 16) & 255) / 255.f; o[1] = ((c >> 8) & 255) / 255.f; o[2] = (c & 255) / 255.f; o[3] = (c >> 24) / 255.f;
}

static void sample (const Tex &t, unsigned flags, float s, float tt, float out[4])
{
	int ws = (flags >> 13) & 3, wt = (flags >> 15) & 3;
	if (!(flags & KAPI_GPU_B_LINEAR))
	{
		int x = (int) floorf (s * t.w), y = (int) floorf (tt * t.h);
		wrap (x, t.w, ws); wrap (y, t.h, wt);
		unpack (t.px[(size_t) y * t.w + x], out);
		return;
	}
	float fx = s * t.w - 0.5f, fy = tt * t.h - 0.5f;
	int x0 = (int) floorf (fx), y0 = (int) floorf (fy);
	float ax = fx - x0, ay = fy - y0;
	int x1 = x0 + 1, y1 = y0 + 1;
	wrap (x0, t.w, ws); wrap (x1, t.w, ws); wrap (y0, t.h, wt); wrap (y1, t.h, wt);
	float c00[4], c10[4], c01[4], c11[4];
	unpack (t.px[(size_t) y0 * t.w + x0], c00); unpack (t.px[(size_t) y0 * t.w + x1], c10);
	unpack (t.px[(size_t) y1 * t.w + x0], c01); unpack (t.px[(size_t) y1 * t.w + x1], c11);
	for (int k = 0; k < 4; k++)
		out[k] = (c00[k] * (1 - ax) + c10[k] * ax) * (1 - ay) + (c01[k] * (1 - ax) + c11[k] * ax) * ay;
}

// The V3D's blend: factor codes 0 zero, 1 one, 2 src colour, 3 1-src colour, 4 dst colour, 5 1-dst colour, 6 src
// alpha, 7 1-src alpha, 8 dst alpha, 9 1-dst alpha; equations 0 add, 1 sub, 2 reverse sub, 3 min, 4 max.
static inline float factor (int f, float s, float d, float sa, float da)
{
	switch (f)
	{
	case 0: return 0; case 1: return 1; case 2: return s; case 3: return 1 - s; case 4: return d; case 5: return 1 - d;
	case 6: return sa; case 7: return 1 - sa; case 8: return da; case 9: return 1 - da;
	}
	return 1;
}
static inline float equation (int e, float s, float fs, float d, float fd)
{
	switch (e)
	{
	case 1: return s * fs - d * fd;
	case 2: return d * fd - s * fs;
	case 3: return std::min (s, d);
	case 4: return std::max (s, d);
	}
	return s * fs + d * fd;
}

// colour src, dst factors, equation, alpha src, dst factors, equation (v3d.cpp's table)
static const u8 s_Fac[KAPI_GPU_BLEND_LAST + 1][6] = {
	{ 1, 0, 0, 1, 0, 0 }, { 6, 7, 0, 1, 7, 0 }, { 6, 1, 0, 6, 1, 0 }, { 4, 0, 0, 4, 0, 0 }, { 1, 7, 0, 1, 7, 0 },
	{ 4, 7, 0, 0, 1, 0 }, { 9, 1, 0, 1, 7, 0 }, { 1, 3, 0, 1, 7, 0 }, { 1, 1, 0, 1, 7, 0 }, { 1, 1, 2, 0, 1, 0 },
	{ 1, 1, 4, 1, 7, 0 }, { 0, 6, 0, 0, 6, 0 }, { 0, 7, 0, 0, 7, 0 } };

static inline bool ztest (int f, float z, float d)
{
	switch (f)
	{
	case 1: return z < d; case 2: return z == d; case 3: return z <= d; case 4: return z > d; case 5: return z != d; case 6: return z >= d;
	case 7: return true;
	}
	return z < d;
}

static void shade (Target &T, const Batch &B, int x, int y, float z, const float *att)
{
	unsigned fl = B.flags;
	int zf = KAPI_GPU_B_ZFUNC (fl);
	if (zf == 0) zf = 1;
	float &dz = T.depth[(size_t) y * T.w + x];
	if (zf != 7 && !ztest (zf, z, dz)) return;
	float c[4] = { att[2], att[3], att[4], att[5] };
	if (B.tex)
	{
		float t[4];
		sample (*B.tex, fl, att[0], att[1], t);
		for (int k = 0; k < 4; k++) c[k] *= t[k];
	}
	for (int k = 0; k < 4; k++) c[k] = clamp01 (c[k] + att[6 + k]);
	if ((fl & (1u << 18)) && c[3] * 255.f < (float) ((fl >> 19) & 255)) return;	// (alpha test)
	u32 &p = T.px[(size_t) y * T.stride + x];
	int bm = (fl >> 8) & 15;
	float o[4];
	if (bm == 0) { o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = c[3]; }
	else
	{
		const u8 *f = s_Fac[bm <= KAPI_GPU_BLEND_LAST ? bm : 1];
		float d[4];
		unpack (p, d);
		if (!T.alpha) d[3] = 1;
		for (int k = 0; k < 3; k++)
			o[k] = clamp01 (equation (f[2], c[k], factor (f[0], c[k], d[k], c[3], d[3]), d[k], factor (f[1], c[k], d[k], c[3], d[3])));
		o[3] = clamp01 (equation (f[5], c[3], factor (f[3], c[3], d[3], c[3], d[3]), d[3], factor (f[4], c[3], d[3], c[3], d[3])));
	}
	u32 a = T.alpha ? (u32) (o[3] * 255.f + 0.5f) : 0;
	p = (a << 24) | ((u32) (o[0] * 255.f + 0.5f) << 16) | ((u32) (o[1] * 255.f + 0.5f) << 8) | (u32) (o[2] * 255.f + 0.5f);
	if (!(fl & KAPI_GPU_B_NOZWRITE) && zf != 7) dz = z;
}

// One triangle (screen space) into the band [y0, y1). Pixel centres at + 0.5; a pixel exactly on an edge is drawn
// when the edge is a top or a left one (each pixel of two triangles sharing an edge drawn once).
static inline bool top_left (float ex, float ey) { return (ey == 0 && ex < 0) || ey > 0; }

static void raster (Target &T, const Batch &B, const V &a0, const V &b0, const V &c0, int y0, int y1)
{
	float area = (b0.x - a0.x) * (c0.y - a0.y) - (b0.y - a0.y) * (c0.x - a0.x);
	if (area == 0) return;
	// culling: front = counter-clockwise in NDC (y up) -- clockwise on the screen (y down): area < 0
	bool front = area < 0;
	if ((B.flags & KAPI_GPU_B_CULL_BACK) && !front) return;
	if ((B.flags & KAPI_GPU_B_CULL_FRONT) && front) return;
	const V &a = a0, &b = area > 0 ? b0 : c0, &c = area > 0 ? c0 : b0;	// (a positive area from here on)
	if (area < 0) area = -area;
	int minx = (int) floorf (std::min ({ a.x, b.x, c.x })), maxx = (int) ceilf (std::max ({ a.x, b.x, c.x }));
	int miny = (int) floorf (std::min ({ a.y, b.y, c.y })), maxy = (int) ceilf (std::max ({ a.y, b.y, c.y }));
	minx = std::max (minx, 0); maxx = std::min (maxx, T.w - 1);
	miny = std::max (miny, y0); maxy = std::min (maxy, y1 - 1);
	if (minx > maxx || miny > maxy) return;
	// edge functions E_bc (weight of a), E_ca (b), E_ab (c): positive inside
	float e0x = c.x - b.x, e0y = c.y - b.y, e1x = a.x - c.x, e1y = a.y - c.y, e2x = b.x - a.x, e2y = b.y - a.y;
	bool t0 = top_left (e0x, -e0y), t1 = top_left (e1x, -e1y), t2 = top_left (e2x, -e2y);
	float inv = 1.f / area;
	for (int y = miny; y <= maxy; y++)
	{
		float py = y + 0.5f;
		for (int x = minx; x <= maxx; x++)
		{
			float px = x + 0.5f;
			float E0 = e0x * (py - b.y) - e0y * (px - b.x);
			float E1 = e1x * (py - c.y) - e1y * (px - c.x);
			float E2 = e2x * (py - a.y) - e2y * (px - a.x);
			if (E0 < 0 || E1 < 0 || E2 < 0) continue;
			if ((E0 == 0 && !t0) || (E1 == 0 && !t1) || (E2 == 0 && !t2)) continue;
			float w0 = E0 * inv, w1 = E1 * inv, w2 = E2 * inv;
			float z = w0 * a.z + w1 * b.z + w2 * c.z;
			float iw = w0 * a.w + w1 * b.w + w2 * c.w;		// 1/w
			if (iw <= 0) continue;
			float att[NA];
			for (int k = 0; k < NA; k++) att[k] = (w0 * a.a[k] + w1 * b.a[k] + w2 * c.a[k]) / iw;
			shade (T, B, x, y, z, att);
		}
	}
}

// A clip-space polygon clipped by the plane d (v) = dot >= 0.
static void clip_plane (std::vector<V> &poly, int coord, float sign)
{
	std::vector<V> out;
	size_t n = poly.size ();
	auto dist = [&] (const V &v) { float c = coord == 0 ? v.x : coord == 1 ? v.y : v.z; return v.w + sign * c; };
	for (size_t i = 0; i < n; i++)
	{
		const V &p = poly[i], &q = poly[(i + 1) % n];
		float dp = dist (p), dq = dist (q);
		if (dp >= 0) out.push_back (p);
		if ((dp >= 0) != (dq >= 0))
		{
			float t = dp / (dp - dq);
			V r;
			r.x = p.x + (q.x - p.x) * t; r.y = p.y + (q.y - p.y) * t; r.z = p.z + (q.z - p.z) * t; r.w = p.w + (q.w - p.w) * t;
			for (int k = 0; k < NA; k++) r.a[k] = p.a[k] + (q.a[k] - p.a[k]) * t;
			out.push_back (r);
		}
	}
	poly.swap (out);
}

// A triangle (clip space) clipped against near and far (z = -w, z = w) and w > 0, then put in screen space.
static void setup (Batch &B, const V tri[3], int W, int H)
{
	std::vector<V> poly (tri, tri + 3);
	bool inside = true;
	for (int i = 0; i < 3; i++) if (tri[i].z < -tri[i].w || tri[i].z > tri[i].w || tri[i].w <= 0) inside = false;
	if (!inside)
	{
		clip_plane (poly, 2, 1);		// z >= -w
		clip_plane (poly, 2, -1);		// z <= w
		if (poly.size () < 3) return;
	}
	std::vector<V> s (poly.size ());
	for (size_t i = 0; i < poly.size (); i++)
	{
		const V &p = poly[i];
		if (p.w <= 1e-6f) return;
		float iw = 1.f / p.w;
		V &q = s[i];
		q.x = (p.x * iw * 0.5f + 0.5f) * W;
		q.y = (0.5f - p.y * iw * 0.5f) * H;
		q.z = p.z * iw * 0.5f + 0.5f;
		q.w = iw;
		for (int k = 0; k < NA; k++) q.a[k] = p.a[k] * iw;
	}
	for (size_t i = 1; i + 1 < s.size (); i++) { B.tris.push_back (s[0]); B.tris.push_back (s[i]); B.tris.push_back (s[i + 1]); }
}

static void draw_all (Target &T, std::vector<Batch> &batches)
{
	unsigned n = std::thread::hardware_concurrency ();
	if (n < 1) n = 1;
	if (n > 16) n = 16;
	size_t work = 0;
	for (auto &b : batches) work += b.tris.size ();
	if (T.h < 64 || work < 30 || getenv ("ONYXRUN_GPU1")) n = 1;
	auto band = [&] (int y0, int y1)
	{
		for (auto &B : batches)
			for (size_t i = 0; i + 2 < B.tris.size (); i += 3) raster (T, B, B.tris[i], B.tris[i + 1], B.tris[i + 2], y0, y1);
	};
	if (n == 1) { band (0, T.h); return; }
	std::vector<std::thread> th;
	int step = (T.h + (int) n - 1) / (int) n;
	for (unsigned i = 0; i < n; i++)
	{
		int y0 = (int) i * step, y1 = std::min (T.h, y0 + step);
		if (y0 < y1) th.emplace_back (band, y0, y1);
	}
	for (auto &t : th) t.join ();
}

static bool target_of (Target &T, u64 pixels, int w, int h, int stride, unsigned clear, bool keep, bool alpha)
{
	if (w < 1 || h < 1 || w > 4096 || h > 4096 || stride < w) return false;
	// drawn into a buffer of its own, stored into the program's pixels when the frame is done (as the V3D's tile
	// buffer is): another process reading them meanwhile (the graphics server composing) sees no frame half drawn
	T.dst = (u32 *) GB (pixels, ((u64) (h - 1) * stride + w) * 4, MEM_R | MEM_W);
	if (!T.dst) return false;
	T.dstStride = stride;
	T.w = w; T.h = h; T.stride = w; T.alpha = alpha;
	T.buf.resize ((size_t) w * h);
	T.px = T.buf.data ();
	T.depth.assign ((size_t) w * h, 1.0f);
	if (keep)
		for (int y = 0; y < h; y++) memcpy (T.px + (size_t) y * w, T.dst + (size_t) y * stride, (size_t) w * 4);
	else
		std::fill (T.buf.begin (), T.buf.end (), alpha ? clear : (clear & 0x00FFFFFF));
	return true;
}

static void target_store (Target &T)
{
	for (int y = 0; y < T.h; y++) memcpy (T.dst + (size_t) y * T.dstStride, T.px + (size_t) y * T.w, (size_t) T.w * 4);
}

static int k_gpu_draw (u64 vv, unsigned n, unsigned clear, u64 pixels, int w, int h, int stride)
{
	if (n % 3 || n > KAPI_GPU_MAX_VERTS) return -2;
	const struct kapi_gpu_vertex *v = n ? (const struct kapi_gpu_vertex *) GB (vv, (u64) n * sizeof *v, MEM_R) : 0;
	if (n && !v) return -2;
	Target T;
	if (!target_of (T, pixels, w, h, stride, clear, false, false)) return -2;
	std::vector<Batch> bs (1);
	Batch &B = bs[0];
	B.tex = 0; B.flags = 0;
	for (unsigned i = 0; i + 2 < n; i += 3)
	{
		V tri[3];
		for (int k = 0; k < 3; k++)
		{
			const struct kapi_gpu_vertex &s = v[i + k];
			V &q = tri[k];
			q.x = s.x; q.y = s.y; q.z = s.z; q.w = 1;
			q.a[0] = q.a[1] = 0;
			q.a[2] = s.r / 255.f; q.a[3] = s.g / 255.f; q.a[4] = s.b / 255.f; q.a[5] = s.a / 255.f;
			q.a[6] = q.a[7] = q.a[8] = q.a[9] = 0;
		}
		setup (B, tri, w, h);
	}
	if (getenv ("ONYXRUN_GPUDEBUG"))
	{
		int in = 0, ou = 0;
		for (unsigned i = 0; i + 2 < n; i += 3) { bool ok = true; for (int k = 0; k < 3; k++) if (v[i + k].z < -1 || v[i + k].z > 1) ok = false; ok ? in++ : ou++; }
		rlog ("gpu_draw: %u vertices, %d triangles inside, %d to clip, %u after setup, %dx%d", n, in, ou, (unsigned) (B.tris.size () / 3), w, h);
	}
	draw_all (T, bs);
	target_store (T);
	return 0;
}

static int k_gpu_render (u64 uf, u64 vv, unsigned nv, u64 ub, unsigned nb)
{
	const struct kapi_gpu_frame *F = G<const struct kapi_gpu_frame> (uf, MEM_R);
	if (!F || nb > KAPI_GPU_MAX_BATCHES || nv > KAPI_GPU_MAX_VERTS) return -2;
	const struct kapi_gpu_vertex3 *v = nv ? (const struct kapi_gpu_vertex3 *) GB (vv, (u64) nv * sizeof *v, MEM_R) : 0;
	const struct kapi_gpu_batch *b = nb ? (const struct kapi_gpu_batch *) GB (ub, (u64) nb * sizeof *b, MEM_R) : 0;
	if ((nv && !v) || (nb && !b)) return -2;
	Target T;
	if (!target_of (T, (u64) F->pixels, F->w, F->h, F->stride, F->clear, (F->flags & KAPI_GPU_F_KEEP) != 0, (F->flags & KAPI_GPU_F_ALPHA) != 0))
		return -2;
	Proc *P = cur ();
	GpuState &GSt = GS (P);
	std::lock_guard<std::mutex> L (GSt.m);
	std::vector<Batch> bs (nb);
	for (unsigned i = 0; i < nb; i++)
	{
		const struct kapi_gpu_batch &bt = b[i];
		Batch &B = bs[i];
		B.flags = bt.flags;
		B.tex = 0;
		if (bt.texture >= 0)
		{
			auto it = GSt.tex.find (bt.texture);
			if (it == GSt.tex.end ()) return -2;
			B.tex = &it->second;
		}
		if ((u64) bt.first + bt.count > nv || bt.count % 3) return -2;
		const float *m = bt.matrix;
		bool ident = (bt.flags & KAPI_GPU_B_NOMATRIX) != 0;
		for (unsigned k = 0; k + 2 < bt.count; k += 3)
		{
			V tri[3];
			for (int j = 0; j < 3; j++)
			{
				const struct kapi_gpu_vertex3 &s = v[bt.first + k + j];
				V &q = tri[j];
				if (ident) { q.x = s.x; q.y = s.y; q.z = s.z; q.w = s.w; }
				else
				{
					q.x = m[0] * s.x + m[1] * s.y + m[2] * s.z + m[3] * s.w;
					q.y = m[4] * s.x + m[5] * s.y + m[6] * s.z + m[7] * s.w;
					q.z = m[8] * s.x + m[9] * s.y + m[10] * s.z + m[11] * s.w;
					q.w = m[12] * s.x + m[13] * s.y + m[14] * s.z + m[15] * s.w;
				}
				q.a[0] = s.s; q.a[1] = s.t;
				q.a[2] = s.r / 255.f; q.a[3] = s.g / 255.f; q.a[4] = s.b / 255.f; q.a[5] = s.a / 255.f;
				q.a[6] = s.r2 / 255.f; q.a[7] = s.g2 / 255.f; q.a[8] = s.b2 / 255.f; q.a[9] = s.a2 / 255.f;
			}
			setup (B, tri, F->w, F->h);
		}
	}
	draw_all (T, bs);
	target_store (T);
	return 0;
}

static int k_gpu_program (int, u64) { return -1; }	// (the apps' QPU programs: not yet -- their CPU path)

KAPI (gpu_info, k_gpu_info);
KAPI (gpu_draw, k_gpu_draw);
KAPI (gpu_texture, k_gpu_texture);
KAPI (gpu_texture_rect, k_gpu_texture_rect);
KAPI (gpu_render, k_gpu_render);
KAPI (gpu_program, k_gpu_program);
