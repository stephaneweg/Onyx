//
// gpucomp.c -- the GPU compositing service (gpucomp.h): layers composited by the V3D through kapi
// gpu_texture / gpu_texture_rect / gpu_render, or by the CPU when the GPU is not there.
//
// The GPU path. A texture is one kernel texture, or past 2048 texels a side a grid of them: tiles
// whose inner part (2046 texels a side) is what each one draws, with a border of one texel of its
// neighbours' around it, so that bilinear filtering near a seam reads the right texels (and at the
// texture's own edges the sampler clamps, as the CPU does). A composite makes one gpu_render
// frame: for each layer and each tile it covers, the tile's inner rectangle (within the layer's
// source rectangle) is mapped by the layer's matrix into the target, clipped there against the
// clip rectangle and the target's edges (Sutherland-Hodgman: the texture coordinates are affine
// in the target, they are interpolated exactly), and drawn as a fan of triangles -- a batch with
// that tile's texture, bilinear or nearest, clamped, the opacity as the vertex colour (the
// kernel's FS_TEX: texel x colour), premultiplied source-over (ONE, ONE_MINUS_SRC_ALPHA) or no
// blending for an opaque layer at full opacity, no depth test. The vertices are in clip space
// (NOMATRIX): x = 2 X / W - 1, y = 1 - 2 Y / H. A target past 2048 pixels a side is drawn in parts.
//
// The CPU path, the same result pixel for pixel but for rounding: for each target pixel whose
// centre falls in the layer's quad (the row's span solved from the inverse matrix), its centre
// mapped back into the texture, sampled bilinearly around it (texel centres at i + 0.5, edges
// clamped) in 16.16 fixed point, scaled by the opacity, blended over. Whole-texel translations
// (a scrolled page) skip the filter: rows blended (or copied) as they are.
//
// A layer whose texture is not on the GPU (the GPU refused it: no handle / memory) is drawn by
// the CPU in its place in the order: the layers make runs, GPU runs are gpu_render frames that
// keep the target (KAPI_GPU_F_KEEP), CPU runs are drawn between them.
//
#include "appkit/appkit.h"
#include "gpucomp.h"

#define GPC_TILE	2048			// the kernel's KAPI_GPU_MAX_TEXSIZE
#define GPC_INNER	(GPC_TILE - 2)		// a tile's own texels when a texture has several
#define GPC_MAX_TARGET	2048			// the kernel's frame (MAX_W / MAX_H in sys/v3d.cpp)
#define GPC_MAX_BATCHES	4096			// KAPI_GPU_MAX_BATCHES
#define GPC_MAX_VERTS	(3 * 65536)		// KAPI_GPU_MAX_VERTS

typedef struct gpc_tile
{
	int handle;				// the kernel's texture (-1: none)
	int tx0, ty0, tw, th;			// the texels it holds
	int ix0, iy0, ix1, iy1;			// the texels it draws (its inner part)
} gpc_tile;

struct gpc_tex
{
	int w, h;
	int onGpu;				// its pixels are in tiles (else in px)
	int lost;
	unsigned *px;				// the CPU's copy (CPU textures)
	int ncx, ncy;
	gpc_tile *tiles;
	gpc_tex *next;
};

typedef struct gpc_job
{
	gpc_target t;
	gpc_layer *layers;
	int n, cap;
	unsigned clear, flags;
	int result, fence;
} gpc_job;

struct gpc_ctx
{
	gpc_config cfg;
	int gpu;				// 1: the GPU is used
	char info[96];
	gpc_tex *texs;
	struct kapi_gpu_vertex3 *v; unsigned nv, vcap;
	struct kapi_gpu_batch *b; unsigned nb, bcap;
	gpc_stats st;
	unsigned *vbufs[8]; int nvbufs;		// gpc_target_alloc's gpu_vbuf blocks (never freed)
	unsigned *row; int rowcap;			// the CPU path's scratch row
	// GPC_F_ASYNC: one composite in flight on a thread
	int thread, evGo, evDone, pending, fence, quit;
	gpc_job job;
};

// ---- small helpers ---------------------------------------------------------------------------------------
// (memcpy / memset: every Onyx app has them -- newlib's, or kapi's aliased by the freestanding link)
static void gpc_copy (void *d, const void *s, unsigned long n) { __builtin_memcpy (d, s, n); }
static void gpc_zero (void *d, unsigned long n) { __builtin_memset (d, 0, n); }
static void gpc_strcpy (char *d, int cap, const char *s)
{
	int i = 0; for (; s[i] && i + 1 < cap; i++) d[i] = s[i];
	d[i] = 0;
}
static int gpc_ifloor (double x) { int i = (int) x; return i - (x < (double) i); }
static int gpc_iceil (double x) { int i = (int) x; return i + (x > (double) i); }
static double gpc_min (double a, double b) { return a < b ? a : b; }
static double gpc_max (double a, double b) { return a > b ? a : b; }

#ifdef GPC_NO_CLOCK
static unsigned gpc_now_us (void) { return 0; }		// (tools/tests/gpucomp/neontest.c: no counter in qemu)
#else
static unsigned gpc_now_us (void) { return kapi_clock_us (); }
#endif

// ---- matrices -----------------------------------------------------------------------------------------------
void gpc_sincos (float r, float *ps, float *pc)
{
	// the angle brought to [-pi/4, pi/4] + a quadrant, then Taylor polynomials to x^13 (|err| < 1e-7)
	const double PI_2 = 1.57079632679489661923;
	double q = (double) r / PI_2;
	int k = gpc_ifloor (q + 0.5);
	double x = (double) r - k * PI_2, x2 = x * x;
	double s = x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110 * (1 - x2 / 156))))));
	double c = 1 - x2 / 2 * (1 - x2 / 12 * (1 - x2 / 30 * (1 - x2 / 56 * (1 - x2 / 90 * (1 - x2 / 132)))));
	switch (k & 3)
	{
	case 0: *ps = (float) s; *pc = (float) c; break;
	case 1: *ps = (float) c; *pc = (float) -s; break;
	case 2: *ps = (float) -s; *pc = (float) -c; break;
	default: *ps = (float) -c; *pc = (float) s; break;
	}
}

void gpc_matrix_identity (gpc_matrix *m) { m->a = 1; m->b = 0; m->c = 0; m->d = 1; m->e = 0; m->f = 0; }

void gpc_matrix_multiply (gpc_matrix *m, const gpc_matrix *o)
{
	gpc_matrix r;
	r.a = m->a * o->a + m->c * o->b;
	r.b = m->b * o->a + m->d * o->b;
	r.c = m->a * o->c + m->c * o->d;
	r.d = m->b * o->c + m->d * o->d;
	r.e = m->a * o->e + m->c * o->f + m->e;
	r.f = m->b * o->e + m->d * o->f + m->f;
	*m = r;
}
void gpc_matrix_translate (gpc_matrix *m, float tx, float ty)
{ gpc_matrix o = { 1, 0, 0, 1, tx, ty }; gpc_matrix_multiply (m, &o); }
void gpc_matrix_scale (gpc_matrix *m, float sx, float sy)
{ gpc_matrix o = { sx, 0, 0, sy, 0, 0 }; gpc_matrix_multiply (m, &o); }
void gpc_matrix_rotate (gpc_matrix *m, float r)
{
	float s, c; gpc_sincos (r, &s, &c);
	gpc_matrix o = { c, s, -s, c, 0, 0 };
	gpc_matrix_multiply (m, &o);
}
void gpc_matrix_skew (gpc_matrix *m, float ax, float ay)
{
	float sx, cx, sy, cy; gpc_sincos (ax, &sx, &cx); gpc_sincos (ay, &sy, &cy);
	gpc_matrix o = { 1, cy != 0 ? sy / cy : 0, cx != 0 ? sx / cx : 0, 1, 0, 0 };
	gpc_matrix_multiply (m, &o);
}
int gpc_matrix_invert (const gpc_matrix *m, gpc_matrix *r)
{
	double det = (double) m->a * m->d - (double) m->b * m->c;
	if (det > -1e-12 && det < 1e-12) return 0;
	double id = 1.0 / det;
	r->a = (float) (m->d * id); r->b = (float) (-m->b * id);
	r->c = (float) (-m->c * id); r->d = (float) (m->a * id);
	r->e = (float) (((double) m->c * m->f - (double) m->d * m->e) * id);
	r->f = (float) (((double) m->b * m->e - (double) m->a * m->f) * id);
	return 1;
}

void gpc_layer_init (gpc_layer *L, gpc_tex *t)
{
	gpc_zero (L, sizeof *L);
	L->tex = t;
	if (t) { L->src_w = (float) t->w; L->src_h = (float) t->h; }
	gpc_matrix_identity (&L->m);
	L->opacity = 255;
}

// ---- the context ----------------------------------------------------------------------------------------------
gpc_ctx *gpc_create (const gpc_config *cfg)
{
	if (cfg == 0 || cfg->alloc == 0 || cfg->free == 0) return 0;
	gpc_ctx *g = (gpc_ctx *) cfg->alloc (sizeof (gpc_ctx));
	if (g == 0) return 0;
	gpc_zero (g, sizeof *g);
	g->cfg = *cfg;
	g->thread = -1;
	char buf[80]; buf[0] = 0;
	if (cfg->flags & GPC_F_CPU) gpc_strcpy (g->info, sizeof g->info, "CPU: asked for");
	else if (kapi_abi_version () < 70) gpc_strcpy (g->info, sizeof g->info, "CPU: the kernel is older than kapi v70");
	else if (kapi_gpu_info (buf, sizeof buf) != 1)
	{
		gpc_strcpy (g->info, sizeof g->info, "CPU: no GPU (");
		int n = 0; while (g->info[n]) n++;
		gpc_strcpy (g->info + n, (int) sizeof g->info - n - 1, buf);
		n = 0; while (g->info[n]) n++;
		if (n + 1 < (int) sizeof g->info) { g->info[n] = ')'; g->info[n + 1] = 0; }
	}
	else
	{
		g->gpu = 1;
		gpc_strcpy (g->info, sizeof g->info, "GPU: ");
		gpc_strcpy (g->info + 5, (int) sizeof g->info - 5, buf);
	}
	return g;
}

int gpc_backend (const gpc_ctx *g) { return g && g->gpu ? GPC_BACKEND_GPU : GPC_BACKEND_CPU; }
const char *gpc_info (const gpc_ctx *g) { return g ? g->info : "CPU"; }
void gpc_get_stats (const gpc_ctx *g, gpc_stats *s) { if (g && s) *s = g->st; }

static void gpc_stop_thread (gpc_ctx *g);

void gpc_destroy (gpc_ctx *g)
{
	if (g == 0) return;
	gpc_stop_thread (g);
	while (g->texs) gpc_tex_destroy (g, g->texs);
	if (g->v) g->cfg.free (g->v);
	if (g->b) g->cfg.free (g->b);
	if (g->job.layers) g->cfg.free (g->job.layers);
	if (g->row) g->cfg.free (g->row);
	g->cfg.free (g);
}

// the GPU stopped (gpu_render / gpu_texture -1 or -3): the CPU from now on, the GPU's textures lost
static void gpc_gpu_lost (gpc_ctx *g)
{
	if (!g->gpu) return;
	g->gpu = 0;
	gpc_strcpy (g->info, sizeof g->info, "CPU: the GPU stopped (kmsg says why)");
	for (gpc_tex *t = g->texs; t; t = t->next)
		if (t->onGpu)
		{
			t->onGpu = 0; t->lost = 1;
			for (int i = 0; i < t->ncx * t->ncy; i++) t->tiles[i].handle = -1;
		}
}

// ---- textures -------------------------------------------------------------------------------------------------
// the tiles along one axis of n texels: count, and tile k's inner / held ranges
static int gpc_ntiles (int n) { return n <= GPC_TILE ? 1 : (n + GPC_INNER - 1) / GPC_INNER; }
static void gpc_tile_range (int n, int k, int *i0, int *i1, int *t0, int *t1)
{
	if (n <= GPC_TILE) { *i0 = *t0 = 0; *i1 = *t1 = n; return; }
	*i0 = k * GPC_INNER; *i1 = *i0 + GPC_INNER; if (*i1 > n) *i1 = n;
	*t0 = *i0 > 0 ? *i0 - 1 : 0; *t1 = *i1 < n ? *i1 + 1 : n;
}

static void gpc_free_tiles (gpc_tex *t)
{
	for (int i = 0; t->tiles && i < t->ncx * t->ncy; i++)
		if (t->tiles[i].handle >= 0) { kapi_gpu_texture (t->tiles[i].handle, 0, 0, 0, 0); t->tiles[i].handle = -1; }
}

// the texture's tiles made on the GPU from pixels (0: transparent) -> 1, 0 refused (-4: no handle /
// memory; the texture then stays the CPU's), -1 the GPU stopped
static int gpc_make_tiles (gpc_ctx *g, gpc_tex *t, const unsigned *px, int stride)
{
	unsigned *zero = 0;
	int ok = 1;
	for (int cy = 0; cy < t->ncy && ok == 1; cy++)
		for (int cx = 0; cx < t->ncx && ok == 1; cx++)
		{
			gpc_tile *T = &t->tiles[cy * t->ncx + cx];
			int tx1, ty1;
			gpc_tile_range (t->w, cx, &T->ix0, &T->ix1, &T->tx0, &tx1);
			gpc_tile_range (t->h, cy, &T->iy0, &T->iy1, &T->ty0, &ty1);
			T->tw = tx1 - T->tx0; T->th = ty1 - T->ty0;
			const unsigned *src; int sstride;
			if (px) { src = px + (long) T->ty0 * stride + T->tx0; sstride = stride; }
			else
			{
				if (zero == 0)
				{
					zero = (unsigned *) g->cfg.alloc ((unsigned long) GPC_TILE * 4 * (unsigned long) (t->h < GPC_TILE ? t->h : GPC_TILE));
					if (zero == 0) { ok = 0; break; }
					gpc_zero (zero, (unsigned long) GPC_TILE * 4 * (unsigned long) (t->h < GPC_TILE ? t->h : GPC_TILE));
				}
				src = zero; sstride = GPC_TILE;
			}
			int h = kapi_gpu_texture (-1, src, T->tw, T->th, sstride);
			if (h >= 0) { T->handle = h; g->st.upload_bytes += (unsigned long long) T->tw * T->th * 4; }
			else ok = (h == -1 || h == -3) ? -1 : 0;
		}
	if (zero) g->cfg.free (zero);
	if (ok != 1) gpc_free_tiles (t);
	return ok;
}

gpc_tex *gpc_tex_create (gpc_ctx *g, int w, int h, const unsigned *px, int stride)
{
	if (g == 0 || w <= 0 || h <= 0 || (px && stride < w)) return 0;
	gpc_wait (g, 0);
	gpc_tex *t = (gpc_tex *) g->cfg.alloc (sizeof (gpc_tex));
	if (t == 0) return 0;
	gpc_zero (t, sizeof *t);
	t->w = w; t->h = h;
	t->ncx = gpc_ntiles (w); t->ncy = gpc_ntiles (h);
	if (g->gpu)
	{
		t->tiles = (gpc_tile *) g->cfg.alloc (sizeof (gpc_tile) * (unsigned long) (t->ncx * t->ncy));
		if (t->tiles)
		{
			for (int i = 0; i < t->ncx * t->ncy; i++) t->tiles[i].handle = -1;
			int r = gpc_make_tiles (g, t, px, stride);
			if (r == 1) t->onGpu = 1;
			else if (r < 0) gpc_gpu_lost (g);
		}
	}
	if (!t->onGpu)						// the CPU's texture
	{
		t->px = (unsigned *) g->cfg.alloc ((unsigned long) w * (unsigned long) h * 4);
		if (t->px == 0) { if (t->tiles) g->cfg.free (t->tiles); g->cfg.free (t); return 0; }
		if (px) for (int y = 0; y < h; y++) gpc_copy (t->px + (long) y * w, px + (long) y * stride, (unsigned long) w * 4);
		else gpc_zero (t->px, (unsigned long) w * (unsigned long) h * 4);
	}
	t->next = g->texs; g->texs = t;
	return t;
}

int gpc_tex_update (gpc_ctx *g, gpc_tex *t, int x, int y, int w, int h, const unsigned *px, int stride)
{
	if (g == 0 || t == 0 || px == 0 || stride < w) return GPC_EINVAL;
	gpc_wait (g, 0);
	if (x < 0) { px -= x; w += x; x = 0; }
	if (y < 0) { px -= (long) y * stride; h += y; y = 0; }
	if (x + w > t->w) w = t->w - x;
	if (y + h > t->h) h = t->h - y;
	if (w <= 0 || h <= 0) return GPC_OK;
	if (t->lost)							// (lost with the GPU: a CPU texture again)
	{
		if (t->px == 0)
		{
			t->px = (unsigned *) g->cfg.alloc ((unsigned long) t->w * (unsigned long) t->h * 4);
			if (t->px == 0) return GPC_ENOMEM;
			gpc_zero (t->px, (unsigned long) t->w * (unsigned long) t->h * 4);
		}
		if (x == 0 && y == 0 && w == t->w && h == t->h) t->lost = 0;
	}
	if (t->onGpu)
	{
		for (int i = 0; i < t->ncx * t->ncy; i++)
		{
			gpc_tile *T = &t->tiles[i];
			int x0 = x > T->tx0 ? x : T->tx0, y0 = y > T->ty0 ? y : T->ty0;
			int x1 = x + w < T->tx0 + T->tw ? x + w : T->tx0 + T->tw, y1 = y + h < T->ty0 + T->th ? y + h : T->ty0 + T->th;
			if (x1 <= x0 || y1 <= y0) continue;
			int r = kapi_gpu_texture_rect (T->handle, x0 - T->tx0, y0 - T->ty0, x1 - x0, y1 - y0,
						       px + (long) (y0 - y) * stride + (x0 - x), stride);
			if (r == -1 || r == -3) { gpc_gpu_lost (g); return GPC_LOST; }
			if (r < 0) return GPC_EINVAL;
			g->st.upload_bytes += (unsigned long long) (x1 - x0) * (y1 - y0) * 4;
		}
		return GPC_OK;
	}
	for (int r = 0; r < h; r++)
		gpc_copy (t->px + (long) (y + r) * t->w + x, px + (long) r * stride, (unsigned long) w * 4);
	return GPC_OK;
}

void gpc_tex_destroy (gpc_ctx *g, gpc_tex *t)
{
	if (g == 0 || t == 0) return;
	gpc_wait (g, 0);
	for (gpc_tex **pp = &g->texs; *pp; pp = &(*pp)->next)
		if (*pp == t) { *pp = t->next; break; }
	if (t->onGpu) gpc_free_tiles (t);
	if (t->tiles) g->cfg.free (t->tiles);
	if (t->px) g->cfg.free (t->px);
	g->cfg.free (t);
}

void gpc_tex_size (const gpc_tex *t, int *w, int *h) { if (w) *w = t ? t->w : 0; if (h) *h = t ? t->h : 0; }
int gpc_tex_lost (const gpc_tex *t) { return t ? t->lost : 0; }

// ---- targets -----------------------------------------------------------------------------------------------------
unsigned *gpc_target_alloc (gpc_ctx *g, int w, int h, int *stride)
{
	if (g == 0 || w <= 0 || h <= 0) return 0;
	unsigned long n = (unsigned long) w * (unsigned long) h * 4;
	unsigned *p = 0;
	if (g->gpu && g->nvbufs < 8 && n <= (64ul << 20))
	{
		p = (unsigned *) kapi_gpu_vbuf ((unsigned) n);
		if (p) g->vbufs[g->nvbufs++] = p;
	}
	if (p == 0) p = (unsigned *) g->cfg.alloc (n);
	if (p && stride) *stride = w;
	return p;
}

void gpc_target_free (gpc_ctx *g, unsigned *p)
{
	if (g == 0 || p == 0) return;
	for (int i = 0; i < g->nvbufs; i++) if (g->vbufs[i] == p) return;	// (kept until the program ends)
	g->cfg.free (p);
}

// ---- the layer's geometry, shared by both paths ----------------------------------------------------------------------
// The part of layer space drawn: the source rectangle within the texture (the texels past the
// texture are not drawn, by the GPU nor the CPU): [*lx0, *lx1) x [*ly0, *ly1); 0: nothing.
static int gpc_layer_extent (const gpc_layer *L, double *lx0, double *ly0, double *lx1, double *ly1)
{
	const gpc_tex *t = L->tex;
	if (t == 0 || L->opacity == 0 || L->src_w <= 0 || L->src_h <= 0) return 0;
	*lx0 = gpc_max (0, -(double) L->src_x); *lx1 = gpc_min (L->src_w, t->w - (double) L->src_x);
	*ly0 = gpc_max (0, -(double) L->src_y); *ly1 = gpc_min (L->src_h, t->h - (double) L->src_y);
	return *lx1 > *lx0 && *ly1 > *ly0;
}

// the layer's clip rectangle within the target's [0, w) x [0, h) -> 0 empty
static int gpc_layer_clip (const gpc_layer *L, int w, int h, int *c)
{
	c[0] = 0; c[1] = 0; c[2] = w; c[3] = h;
	if (L->clip[2] > 0 && L->clip[3] > 0)
	{
		if (L->clip[0] > c[0]) c[0] = L->clip[0];
		if (L->clip[1] > c[1]) c[1] = L->clip[1];
		if (L->clip[0] + L->clip[2] < c[2]) c[2] = L->clip[0] + L->clip[2];
		if (L->clip[1] + L->clip[3] < c[3]) c[3] = L->clip[1] + L->clip[3];
	}
	return c[2] > c[0] && c[3] > c[1];
}

// ---- the CPU path --------------------------------------------------------------------------------------------------
// premultiplied colour x k / 255 (k 0..256: 256 = x 1), the two channel pairs at once
static inline unsigned gpc_scale (unsigned c, unsigned k)
{
	unsigned rb = ((c & 0x00FF00FF) * k + 0x00800080) >> 8 & 0x00FF00FF;
	unsigned ag = (((c >> 8) & 0x00FF00FF) * k + 0x00800080) & 0xFF00FF00;
	return rb | ag;
}
// s over d, premultiplied: d = s + d (255 - sa) / 255 (exact rounding of x / 255)
static inline unsigned gpc_over (unsigned s, unsigned d)
{
	unsigned ia = 255 - (s >> 24);
	unsigned rb = (d & 0x00FF00FF) * ia + 0x00800080;
	rb = ((rb + ((rb >> 8) & 0x00FF00FF)) >> 8) & 0x00FF00FF;
	unsigned ag = ((d >> 8) & 0x00FF00FF) * ia + 0x00800080;
	ag = (ag + ((ag >> 8) & 0x00FF00FF)) & 0xFF00FF00;
	return s + (rb | ag);		// (no carry: s + d (1 - sa) <= 255 per channel when s is premultiplied)
}
// the row s[0..n) (premultiplied) blended into the target's d[0..n) at k / 256: straight loops
// without branches, which the compiler makes NEON code of (-O3). An opaque target (a canvas)
// keeps its top byte; an opaque layer at full opacity is copied.
static void gpc_row (unsigned *d, const unsigned *s, int n, unsigned k, int alpha, int opaque)
{
	int i;
	if (opaque)
	{
		if (alpha) for (i = 0; i < n; i++) d[i] = s[i];
		else for (i = 0; i < n; i++) d[i] = (d[i] & 0xFF000000) | (s[i] & 0x00FFFFFF);
	}
	else if (k >= 256)
	{
		if (alpha) for (i = 0; i < n; i++) d[i] = gpc_over (s[i], d[i]);
		else for (i = 0; i < n; i++) d[i] = (d[i] & 0xFF000000) | (gpc_over (s[i], d[i] | 0xFF000000) & 0x00FFFFFF);
	}
	else
	{
		if (alpha) for (i = 0; i < n; i++) d[i] = gpc_over (gpc_scale (s[i], k), d[i]);
		else for (i = 0; i < n; i++) d[i] = (d[i] & 0xFF000000) | (gpc_over (gpc_scale (s[i], k), d[i] | 0xFF000000) & 0x00FFFFFF);
	}
}

// x y / 255, rounded (x, y 0..255)
static inline unsigned gpc_mul8 (unsigned x, unsigned y) { unsigned t = x * y + 128; return (t + (t >> 8)) >> 8; }
// one pixel of a blend mode (gpucomp.h): s premultiplied at its opacity, d the target's (alpha: its
// alpha too, else opaque -- da 255, the top byte kept)
unsigned gpc_blend_pixel (unsigned s, unsigned d, unsigned blend, int alpha)
{
	unsigned sa = s >> 24, da = alpha ? d >> 24 : 255, oa = sa + gpc_mul8 (da, 255 - sa), o = 0;
	if (blend == GPC_B_NORMAL)
	{
		if (alpha) return gpc_over (s, d);
		return (d & 0xFF000000) | (gpc_over (s, d | 0xFF000000) & 0x00FFFFFF);
	}
	if (blend == GPC_B_MASK || blend == GPC_B_CUTOUT)
	{
		unsigned k = blend == GPC_B_MASK ? sa : 255 - sa;
		if (!alpha) return (d & 0xFF000000) | (gpc_scale (d, k + (k >> 7)) & 0x00FFFFFF);
		for (int sh = 0; sh < 32; sh += 8) o |= gpc_mul8 ((d >> sh) & 255, k) << sh;
		return o;
	}
	for (int sh = 0; sh < 24; sh += 8)
	{
		int sc = (int) ((s >> sh) & 255), dc = (int) ((d >> sh) & 255), c;
		switch (blend)
		{
		case GPC_B_MULTIPLY: c = (int) (gpc_mul8 ((unsigned) sc, (unsigned) dc) + gpc_mul8 ((unsigned) dc, 255 - sa) + gpc_mul8 ((unsigned) sc, 255 - da)); break;
		case GPC_B_SCREEN: c = sc + dc - (int) gpc_mul8 ((unsigned) sc, (unsigned) dc); break;
		case GPC_B_ADD: c = sc + dc; break;
		case GPC_B_SUBTRACT: c = (dc > sc ? dc - sc : 0) + (int) gpc_mul8 ((unsigned) sc, 255 - da); break;
		case GPC_B_LIGHTEN: c = sc > dc ? sc : dc; break;
		default: c = sc + (int) gpc_mul8 ((unsigned) dc, 255 - sa); break;
		}
		if (c > 255) c = 255;
		o |= (unsigned) c << sh;
	}
	return o | (alpha ? (oa > 255 ? 255 : oa) << 24 : d & 0xFF000000);
}
// a row in a blend mode other than normal (k: the opacity 0..256; a mask's unused)
static void gpc_row_blend (unsigned *d, const unsigned *s, int n, unsigned k, int alpha, unsigned blend)
{
	if (blend == GPC_B_MASK) k = 256;
	for (int i = 0; i < n; i++) d[i] = gpc_blend_pixel (k >= 256 ? s[i] : gpc_scale (s[i], k), d[i], blend, alpha);
}

// the bilinear filter at fixed weights fx, fy (0..255) of texel pairs (a, b) above, (c, d) below
static inline unsigned gpc_lerp4 (unsigned a, unsigned b, unsigned c, unsigned d, unsigned fx, unsigned fy)
{
	unsigned rb0 = ((a & 0x00FF00FF) * (256 - fx) + (b & 0x00FF00FF) * fx) >> 8 & 0x00FF00FF;
	unsigned ag0 = (((a >> 8) & 0x00FF00FF) * (256 - fx) + ((b >> 8) & 0x00FF00FF) * fx) >> 8 & 0x00FF00FF;
	unsigned rb1 = ((c & 0x00FF00FF) * (256 - fx) + (d & 0x00FF00FF) * fx) >> 8 & 0x00FF00FF;
	unsigned ag1 = (((c >> 8) & 0x00FF00FF) * (256 - fx) + ((d >> 8) & 0x00FF00FF) * fx) >> 8 & 0x00FF00FF;
	unsigned rb = (rb0 * (256 - fy) + rb1 * fy + 0x00800080) >> 8 & 0x00FF00FF;
	unsigned ag = (ag0 * (256 - fy) + ag1 * fy + 0x00800080) & 0xFF00FF00;
	return rb | ag;
}

// bilinear: the 4 texels around (u, v) (16.16, texel centres already taken off: 0 = texel 0's
// centre), clamped to the texture
static inline unsigned gpc_bilinear (const unsigned *px, int w, int h, long long u, long long v)
{
	int x0 = (int) (u >> 16), y0 = (int) (v >> 16);
	unsigned fx = (unsigned) (u >> 8) & 255, fy = (unsigned) (v >> 8) & 255;
	int x1 = x0 + 1, y1 = y0 + 1;
	if (x0 < 0) x0 = 0; else if (x0 >= w) x0 = w - 1;
	if (x1 < 0) x1 = 0; else if (x1 >= w) x1 = w - 1;
	if (y0 < 0) y0 = 0; else if (y0 >= h) y0 = h - 1;
	if (y1 < 0) y1 = 0; else if (y1 >= h) y1 = h - 1;
	const unsigned *r0 = px + (long) y0 * w, *r1 = px + (long) y1 * w;
	unsigned a = r0[x0], b = r0[x1], c = r1[x0], d = r1[x1];
	if (a == b && a == c && a == d) return a;
	return gpc_lerp4 (a, b, c, d, fx, fy);
}

// [*x0, *x1): the integers x with lo <= p + q x < hi (x0 >= x1: none). A pixel centre on the
// edge belongs to the lo side, as the GPU's top-left rule gives it on the whole-pixel edges of
// axis-aligned layers (the slack: the rounding of the inverse)
static void gpc_span (double p, double q, double lo, double hi, int *x0, int *x1)
{
	lo -= 1e-7; hi -= 1e-7;
	if (q > -1e-12 && q < 1e-12)
	{
		if (!(p >= lo && p < hi)) { *x0 = 0; *x1 = 0; }
		return;
	}
	double a = (lo - p) / q, b = (hi - p) / q;
	int s, e;
	if (q > 0) { s = gpc_iceil (a); e = gpc_iceil (b); }		// x >= a, x < b
	else { s = gpc_ifloor (b) + 1; e = gpc_ifloor (a) + 1; }	// x > b, x <= a
	if (s > *x0) *x0 = s;
	if (e < *x1) *x1 = e;
}

// the layer into the target; row: a scratch row of the target's width
static void gpc_cpu_layer (const gpc_target *T, const gpc_layer *L, unsigned *row)
{
	double lx0, ly0, lx1, ly1;
	int C[4];
	const gpc_tex *t = L->tex;
	if (!gpc_layer_extent (L, &lx0, &ly0, &lx1, &ly1) || !gpc_layer_clip (L, T->w, T->h, C) || t->px == 0) return;
	const gpc_matrix *m = &L->m;
	double det = (double) m->a * m->d - (double) m->b * m->c;	// (the inverse in doubles)
	if (det > -1e-12 && det < 1e-12) return;
	struct { double a, b, c, d, e, f; } inv;
	inv.a = m->d / det; inv.b = -m->b / det; inv.c = -m->c / det; inv.d = m->a / det;
	inv.e = ((double) m->c * m->f - (double) m->d * m->e) / det;
	inv.f = ((double) m->b * m->e - (double) m->a * m->f) / det;
	int alpha = (T->flags & GPC_T_ALPHA) != 0, nearest = (L->flags & GPC_L_NEAREST) != 0;
	unsigned op = L->opacity > 255 ? 255 : L->opacity, k = op + (op >> 7);	// (k: 0..256)
	int opaque = (L->flags & GPC_L_OPAQUE) && op == 255;
	const int tw = t->w, th = t->h;
	// the quad's rows in the target
	double lxs[4] = { lx0, lx1, lx1, lx0 }, lys[4] = { ly0, ly0, ly1, ly1 };
	double ymin = 1e30, ymax = -1e30;
	for (int i = 0; i < 4; i++)
	{
		double Y = m->b * lxs[i] + m->d * lys[i] + m->f;
		ymin = gpc_min (ymin, Y); ymax = gpc_max (ymax, Y);
	}
	int y0 = gpc_iceil (ymin - 0.5), y1 = gpc_iceil (ymax - 0.5);
	if (y0 < C[1]) y0 = C[1];
	if (y1 > C[3]) y1 = C[3];
	double su = L->src_x, sv = L->src_y;
	// a translation (a = d = 1, b = c = 0): texel space = target pixel + (du, dv) -- whole texels
	// (or nearest): the texels as they are; else fixed bilinear weights along the whole layer
	int shift = m->a == 1 && m->d == 1 && m->b == 0 && m->c == 0;
	double du = su - m->e, dv = sv - m->f;
	long long DU = 0, DV = 0;
	if (shift)
	{
		if (nearest) { DU = (long long) gpc_ifloor (du + 0.5) << 16; DV = (long long) gpc_ifloor (dv + 0.5) << 16; }
		else
		{
			DU = (long long) (du * 65536.0 + (du < 0 ? -0.5 : 0.5)); DV = (long long) (dv * 65536.0 + (dv < 0 ? -0.5 : 0.5));
			if ((DU & 0xFF00) == 0 && (DV & 0xFF00) == 0) { DU &= ~0xFFFFll; DV &= ~0xFFFFll; nearest = 1; }	// (whole)
		}
	}
	for (int y = y0; y < y1; y++)
	{
		double py = y + 0.5;
		// layer coordinates along the row: lx = p + q x, ly = r + s x (x the pixel index)
		double p = inv.a * 0.5 + inv.c * py + inv.e, q = inv.a;
		double r = inv.b * 0.5 + inv.d * py + inv.f, s = inv.b;
		int x0 = C[0], x1 = C[2];
		gpc_span (p, q, lx0, lx1, &x0, &x1);
		gpc_span (r, s, ly0, ly1, &x0, &x1);
		if (x1 <= x0) continue;
		unsigned *d = T->pixels + (long) y * T->stride + x0;
		int n = x1 - x0;
		const unsigned *src = row;
		if (shift)
		{
			// texel (x + DU, y + DV) in 16.16
			long long V = ((long long) y << 16) + DV;
			int tx0 = x0 + (int) (DU >> 16), fx = (int) ((DU >> 8) & 255), fy = (int) ((V >> 8) & 255);
			int ty0 = (int) (V >> 16), ty1 = ty0 + 1;
			if (ty0 < 0) ty0 = 0; else if (ty0 >= th) ty0 = th - 1;
			if (ty1 < 0) ty1 = 0; else if (ty1 >= th) ty1 = th - 1;
			const unsigned *r0 = t->px + (long) ty0 * tw, *r1 = t->px + (long) ty1 * tw;
			if (nearest)
			{
				if (tx0 >= 0 && tx0 + n <= tw) src = r0 + tx0;		// (straight from the texture)
				else for (int i = 0; i < n; i++) { int tx = tx0 + i; row[i] = r0[tx < 0 ? 0 : tx >= tw ? tw - 1 : tx]; }
			}
			else
			{
				// the pixels whose two texels are inside: one loop at fixed weights; the others clamped
				int a = -tx0 > 0 ? -tx0 : 0, b = tw - 1 - tx0 < n ? tw - 1 - tx0 : n;
				if (b < a) b = a;
				for (int i = 0; i < a && i < n; i++) row[i] = gpc_bilinear (t->px, tw, th, (long long) (tx0 + i) * 65536 + (fx << 8), V);
				const unsigned *p0 = r0 + tx0, *p1 = r1 + tx0;
				for (int i = a; i < b; i++) row[i] = gpc_lerp4 (p0[i], p0[i + 1], p1[i], p1[i + 1], (unsigned) fx, (unsigned) fy);
				for (int i = b; i < n; i++) row[i] = gpc_bilinear (t->px, tw, th, (long long) (tx0 + i) * 65536 + (fx << 8), V);
			}
		}
		else
		{
			// texel space (u, v) = source + layer (- 0.5 for bilinear: the texel centres), 16.16
			double u = su + p + q * x0, v = sv + r + s * x0;
			if (nearest)
			{
				long long U = (long long) (u * 65536.0), V = (long long) (v * 65536.0);
				long long dU = (long long) (q * 65536.0), dV = (long long) (s * 65536.0);
				for (int i = 0; i < n; i++, U += dU, V += dV)
				{
					int tx = (int) (U >> 16), ty = (int) (V >> 16);
					if (tx < 0) tx = 0; else if (tx >= tw) tx = tw - 1;
					if (ty < 0) ty = 0; else if (ty >= th) ty = th - 1;
					row[i] = t->px[(long) ty * tw + tx];
				}
			}
			else
			{
				long long U = (long long) ((u - 0.5) * 65536.0 + (u < 0.5 ? -0.5 : 0.5)), V = (long long) ((v - 0.5) * 65536.0 + (v < 0.5 ? -0.5 : 0.5));
				long long dU = (long long) (q * 65536.0 + (q < 0 ? -0.5 : 0.5)), dV = (long long) (s * 65536.0 + (s < 0 ? -0.5 : 0.5));
				for (int i = 0; i < n; i++, U += dU, V += dV) row[i] = gpc_bilinear (t->px, tw, th, U, V);
			}
		}
		if (L->blend != GPC_B_NORMAL && L->blend < GPC_B_COUNT) gpc_row_blend (d, src, n, k, alpha, L->blend);
		else gpc_row (d, src, n, k, alpha, opaque);
	}
}

static void gpc_cpu_clear (const gpc_target *T, unsigned clear)
{
	unsigned c = (T->flags & GPC_T_ALPHA) ? clear : clear & 0x00FFFFFF;
	for (int y = 0; y < T->h; y++)
	{
		unsigned *d = T->pixels + (long) y * T->stride;
		for (int x = 0; x < T->w; x++) d[x] = c;
	}
}

// ---- the GPU path ------------------------------------------------------------------------------------------------------
typedef struct gpc_pt { double x, y, u, v; } gpc_pt;

// the polygon p[n] clipped to one side (axis 0: x, 1: y; keep >= lim when ge, else <= lim) -> into o
static int gpc_clip_side (const gpc_pt *p, int n, gpc_pt *o, int axis, double lim, int ge)
{
	int m = 0;
	for (int i = 0; i < n; i++)
	{
		const gpc_pt *a = &p[i], *b = &p[(i + 1) % n];
		double va = axis ? a->y : a->x, vb = axis ? b->y : b->x;
		int ina = ge ? va >= lim : va <= lim, inb = ge ? vb >= lim : vb <= lim;
		if (ina) o[m++] = *a;
		if (ina != inb)
		{
			double f = (lim - va) / (vb - va);
			gpc_pt q;
			q.x = a->x + (b->x - a->x) * f; q.y = a->y + (b->y - a->y) * f;
			q.u = a->u + (b->u - a->u) * f; q.v = a->v + (b->v - a->v) * f;
			if (axis) q.y = lim; else q.x = lim;
			o[m++] = q;
		}
	}
	return m;
}

static int gpc_grow (gpc_ctx *g, unsigned nv, unsigned nb)
{
	if (g->nv + nv > g->vcap)
	{
		unsigned cap = g->vcap ? g->vcap * 2 : 4096;
		while (cap < g->nv + nv) cap *= 2;
		struct kapi_gpu_vertex3 *v = (struct kapi_gpu_vertex3 *) g->cfg.alloc (sizeof *v * (unsigned long) cap);
		if (v == 0) return 0;
		if (g->v) { gpc_copy (v, g->v, sizeof *v * (unsigned long) g->nv); g->cfg.free (g->v); }
		g->v = v; g->vcap = cap;
	}
	if (g->nb + nb > g->bcap)
	{
		unsigned cap = g->bcap ? g->bcap * 2 : 256;
		while (cap < g->nb + nb) cap *= 2;
		struct kapi_gpu_batch *b = (struct kapi_gpu_batch *) g->cfg.alloc (sizeof *b * (unsigned long) cap);
		if (b == 0) return 0;
		if (g->b) { gpc_copy (b, g->b, sizeof *b * (unsigned long) g->nb); g->cfg.free (g->b); }
		g->b = b; g->bcap = cap;
	}
	return 1;
}

// A frame of the batches so far into the target's part (ox, oy, w x h) -> 0 / kapi's error
static int gpc_flush (gpc_ctx *g, const gpc_target *T, int ox, int oy, int w, int h, int keep, unsigned clear)
{
	struct kapi_gpu_frame f;
	f.pixels = T->pixels + (long) oy * T->stride + ox;
	f.w = w; f.h = h; f.stride = T->stride;
	f.clear = (T->flags & GPC_T_ALPHA) ? clear : clear & 0x00FFFFFF;
	f.flags = (keep ? KAPI_GPU_F_KEEP : 0) | ((T->flags & GPC_T_ALPHA) ? KAPI_GPU_F_ALPHA : 0);
	int r = kapi_gpu_render (&f, g->v, g->nv, g->b, g->nb);
	g->st.last_gpu_calls++;
	g->nv = 0; g->nb = 0;
	return r;
}

// the layer's batches for the target's part (ox, oy, w x h) -> 1, 0 no memory
static int gpc_gpu_layer (gpc_ctx *g, const gpc_layer *L, int ox, int oy, int w, int h, int tw, int th, int alpha)
{
	double lx0, ly0, lx1, ly1;
	int C[4];
	const gpc_tex *t = L->tex;
	if (!gpc_layer_extent (L, &lx0, &ly0, &lx1, &ly1) || !gpc_layer_clip (L, tw, th, C)) return 1;
	// the clip in the part's coordinates
	double cx0 = gpc_max (C[0] - ox, 0), cy0 = gpc_max (C[1] - oy, 0), cx1 = gpc_min (C[2] - ox, w), cy1 = gpc_min (C[3] - oy, h);
	if (cx1 <= cx0 || cy1 <= cy0) return 1;
	const gpc_matrix *m = &L->m;
	unsigned op = L->opacity > 255 ? 255 : L->opacity;
	int opaque = (L->flags & GPC_L_OPAQUE) && op == 255;
	// the blend mode: one or two passes of the kernel's presets (v72); the second, UNDER, adds
	// s (1 - da) -- nothing on an opaque target, left out there
	unsigned pass[2] = { opaque ? KAPI_GPU_BLEND_NONE : KAPI_GPU_BLEND_PREMUL, 0 };
	switch (L->blend < GPC_B_COUNT ? L->blend : GPC_B_NORMAL)
	{
	case GPC_B_MULTIPLY: pass[0] = KAPI_GPU_BLEND_MULCOL; pass[1] = alpha ? KAPI_GPU_BLEND_UNDER : 0; break;
	case GPC_B_SCREEN: pass[0] = KAPI_GPU_BLEND_SCREEN; break;
	case GPC_B_ADD: pass[0] = KAPI_GPU_BLEND_PLUS; break;
	case GPC_B_SUBTRACT: pass[0] = KAPI_GPU_BLEND_RSUB; pass[1] = alpha ? KAPI_GPU_BLEND_UNDER : 0; break;
	case GPC_B_LIGHTEN: pass[0] = KAPI_GPU_BLEND_LIGHTEN; break;
	case GPC_B_MASK: pass[0] = KAPI_GPU_BLEND_DSTIN; op = 255; break;
	case GPC_B_CUTOUT: pass[0] = KAPI_GPU_BLEND_DSTOUT; break;
	default: break;
	}
	unsigned flags = KAPI_GPU_Z_ALWAYS | KAPI_GPU_B_NOZWRITE | KAPI_GPU_B_NOMATRIX
		       | KAPI_GPU_B_WRAP_S (KAPI_GPU_WRAP_CLAMP) | KAPI_GPU_B_WRAP_T (KAPI_GPU_WRAP_CLAMP)
		       | ((L->flags & GPC_L_NEAREST) ? 0 : KAPI_GPU_B_LINEAR);
	double u0 = L->src_x + lx0, u1 = L->src_x + lx1, v0 = L->src_y + ly0, v1 = L->src_y + ly1;	// (texels drawn)
	for (int i = 0; i < t->ncx * t->ncy; i++)
	{
		const gpc_tile *T = &t->tiles[i];
		double a0 = gpc_max (u0, T->ix0), a1 = gpc_min (u1, T->ix1), b0 = gpc_max (v0, T->iy0), b1 = gpc_min (v1, T->iy1);
		if (a1 <= a0 || b1 <= b0) continue;
		gpc_pt P[4], Q[16], R[16];
		double us[4] = { a0, a1, a1, a0 }, vs[4] = { b0, b0, b1, b1 };
		for (int k = 0; k < 4; k++)
		{
			double lx = us[k] - L->src_x, ly = vs[k] - L->src_y;
			P[k].x = m->a * lx + m->c * ly + m->e - ox; P[k].y = m->b * lx + m->d * ly + m->f - oy;
			P[k].u = us[k]; P[k].v = vs[k];
		}
		int n = gpc_clip_side (P, 4, Q, 0, cx0, 1);
		n = gpc_clip_side (Q, n, R, 0, cx1, 0);
		n = gpc_clip_side (R, n, Q, 1, cy0, 1);
		n = gpc_clip_side (Q, n, R, 1, cy1, 0);
		if (n < 3) continue;
		if (!gpc_grow (g, (unsigned) (n - 2) * 3, 2)) return 0;
		for (int ps = 0; ps < 2 && (ps == 0 || pass[ps]); ps++)	// (the passes: the same vertices)
		{
			struct kapi_gpu_batch *b = &g->b[g->nb++];
			b->first = g->nv; b->count = (unsigned) (n - 2) * 3;
			b->texture = T->handle; b->flags = flags | KAPI_GPU_B_BLEND (pass[ps]);
			for (int k = 0; k < 16; k++) b->matrix[k] = (k % 5) == 0 ? 1.0f : 0.0f;
		}
		for (int k = 1; k + 1 < n; k++)
		{
			const gpc_pt *tri[3] = { &R[0], &R[k], &R[k + 1] };
			for (int j = 0; j < 3; j++)
			{
				struct kapi_gpu_vertex3 *v = &g->v[g->nv++];
				// (the kernel's viewport: X = x (w / 2) + w / 2, Y = -y (h / 2) + h / 2, its halves integers)
				v->x = (float) (tri[j]->x / (w / 2) - 1.0); v->y = (float) (1.0 - tri[j]->y / (h / 2));
				v->z = 0; v->w = 1;
				v->s = (float) ((tri[j]->u - T->tx0) / T->tw); v->t = (float) ((tri[j]->v - T->ty0) / T->th);
				v->r = v->g = v->b = v->a = (unsigned char) op;
				v->r2 = v->g2 = v->b2 = v->a2 = 0;
			}
		}
	}
	return 1;
}

// layers [i0, i1) (all on the GPU) into the target, in equal parts of <= 2048 x 2048 -> 0 / kapi's error
static int gpc_gpu_run (gpc_ctx *g, const gpc_target *T, const gpc_layer *Ls, int i0, int i1, int keep, unsigned clear)
{
	int nx = (T->w + GPC_MAX_TARGET - 1) / GPC_MAX_TARGET, ny = (T->h + GPC_MAX_TARGET - 1) / GPC_MAX_TARGET;
	int pw = (T->w + nx - 1) / nx, ph = (T->h + ny - 1) / ny;
	for (int oy = 0; oy < T->h; oy += ph)
		for (int ox = 0; ox < T->w; ox += pw)
		{
			int w = T->w - ox < pw ? T->w - ox : pw;
			int h = T->h - oy < ph ? T->h - oy : ph;
			int k = keep;
			g->nv = 0; g->nb = 0;
			for (int i = i0; i < i1; i++)
			{
				const gpc_tex *t = Ls[i].tex;
				if (g->nb + 2u * (unsigned) (t->ncx * t->ncy) > GPC_MAX_BATCHES || g->nv + 18u * (unsigned) (t->ncx * t->ncy) > GPC_MAX_VERTS)
				{
					int r = gpc_flush (g, T, ox, oy, w, h, k, clear);
					if (r) return r;
					k = 1;
				}
				if (!gpc_gpu_layer (g, &Ls[i], ox, oy, w, h, T->w, T->h, (T->flags & GPC_T_ALPHA) != 0)) return -4;
			}
			if (g->nb || !k)
			{
				int r = gpc_flush (g, T, ox, oy, w, h, k, clear);
				if (r) return r;
			}
		}
	return 0;
}

// ---- compositing ------------------------------------------------------------------------------------------------------
// the layer can be the GPU's: its texture is there, and its blend mode is normal or the kernel has
// the presets (v72)
static int gpc_on_gpu (const gpc_layer *L)
{
	return L->tex && L->tex->onGpu && (L->blend == GPC_B_NORMAL || L->blend >= GPC_B_COUNT || kapi_abi_version () >= 72);
}
static int gpc_do_composite (gpc_ctx *g, const gpc_target *T, const gpc_layer *Ls, int n, unsigned clear, unsigned flags)
{
	if (g == 0 || T == 0 || T->pixels == 0 || T->w <= 0 || T->h <= 0 || T->stride < T->w || n < 0 || (n && Ls == 0))
		return GPC_EINVAL;
	unsigned t0 = gpc_now_us ();
	int res = GPC_OK;
	if (T->w > g->rowcap)							// (the CPU path's row)
	{
		unsigned *r = (unsigned *) g->cfg.alloc ((unsigned long) T->w * 4);
		if (r == 0) return GPC_ENOMEM;
		if (g->row) g->cfg.free (g->row);
		g->row = r; g->rowcap = T->w;
	}
	g->st.last_gpu_calls = 0; g->st.last_cpu_layers = 0;
	int keep = !(flags & GPC_C_CLEAR);
	int i = 0;
	while (i < n || !keep)
	{
		// a run: layers of the same kind (on the GPU or not); the lost ones and the empty skipped
		int big = T->w >= 2 && T->h >= 2;		// (the kernel's viewport: halves of the size, integers)
		int gpu = g->gpu && big && i < n && gpc_on_gpu (&Ls[i]);
		int j = i;
		while (j < n && (g->gpu && big && gpc_on_gpu (&Ls[j])) == gpu) j++;
		if (gpu)
		{
			int r = gpc_gpu_run (g, T, Ls, i, j, keep, clear);
			if (r == -1 || r == -3)
			{
				gpc_gpu_lost (g); res = GPC_LOST;
				if (!keep) gpc_cpu_clear (T, clear);
				keep = 1;
				// (the layers after it: the CPU's, those of the lost textures skipped)
				for (; j < n; j++) if (Ls[j].tex && Ls[j].tex->px && !Ls[j].tex->lost) { gpc_cpu_layer (T, &Ls[j], g->row); g->st.last_cpu_layers++; }
				break;
			}
			if (r) { res = r == -4 ? GPC_ENOMEM : GPC_EINVAL; break; }
		}
		else
		{
			if (!keep) gpc_cpu_clear (T, clear);
			for (int k = i; k < j; k++)
				if (Ls[k].tex && Ls[k].tex->px && !Ls[k].tex->lost) { gpc_cpu_layer (T, &Ls[k], g->row); g->st.last_cpu_layers++; }
		}
		keep = 1;
		i = j;
	}
	g->st.last_us = gpc_now_us () - t0;
	g->st.frames++;
	if (g->st.last_gpu_calls) g->st.gpu_frames++;
	return res;
}

int gpc_composite (gpc_ctx *g, const gpc_target *t, const gpc_layer *layers, int n, unsigned clear, unsigned flags)
{
	if (g == 0) return GPC_EINVAL;
	gpc_wait (g, 0);
	return gpc_do_composite (g, t, layers, n, clear, flags);
}

// ---- GPC_F_ASYNC: a thread of the program does the composites -------------------------------------------------------------
static int gpc_worker (void *arg)
{
	gpc_ctx *g = (gpc_ctx *) arg;
	for (;;)
	{
		kapi_event_wait (g->evGo, KAPI_WAIT_FOREVER);
		if (g->quit) break;
		g->job.result = gpc_do_composite (g, &g->job.t, g->job.layers, g->job.n, g->job.clear, g->job.flags);
		kapi_event_set (g->evDone);				// (once a job: gpc_wait takes it)
	}
	return 0;
}

static void gpc_stop_thread (gpc_ctx *g)
{
	if (g->thread < 0) return;
	gpc_wait (g, 0);
	g->quit = 1;
	kapi_event_set (g->evGo);
	int code; kapi_thread_join (g->thread, KAPI_WAIT_FOREVER, &code);
	kapi_sync_close (g->evGo); kapi_sync_close (g->evDone);
	g->thread = -1;
}

int gpc_submit (gpc_ctx *g, const gpc_target *t, const gpc_layer *layers, int n, unsigned clear, unsigned flags)
{
	if (g == 0 || t == 0 || n < 0) return GPC_EINVAL;
	gpc_wait (g, 0);
	if ((g->cfg.flags & GPC_F_ASYNC) && g->thread < 0 && kapi_abi_version () >= 67)
	{
		g->evGo = kapi_event_create (0, 0); g->evDone = kapi_event_create (0, 0);
		if (g->evGo > 0 && g->evDone > 0) g->thread = kapi_thread_create (gpc_worker, g, 0, "gpucomp");
		if (g->thread < 0) { g->thread = -1; g->cfg.flags &= ~GPC_F_ASYNC; }
	}
	g->fence++;
	if (g->fence <= 0) g->fence = 1;
	if (g->thread < 0)					// (no thread: at once)
	{
		g->job.result = gpc_do_composite (g, t, layers, n, clear, flags);
		g->job.fence = g->fence;
		return g->fence;
	}
	if (n > g->job.cap)
	{
		gpc_layer *p = (gpc_layer *) g->cfg.alloc (sizeof (gpc_layer) * (unsigned long) n);
		if (p == 0) return GPC_ENOMEM;
		if (g->job.layers) g->cfg.free (g->job.layers);
		g->job.layers = p; g->job.cap = n;
	}
	gpc_copy (g->job.layers, layers, sizeof (gpc_layer) * (unsigned long) n);
	g->job.t = *t; g->job.n = n; g->job.clear = clear; g->job.flags = flags; g->job.fence = g->fence;
	g->pending = 1;
	kapi_event_set (g->evGo);
	return g->fence;
}

int gpc_wait (gpc_ctx *g, int fence)
{
	if (g == 0) return GPC_EINVAL;
	if (g->pending) { kapi_event_wait (g->evDone, KAPI_WAIT_FOREVER); g->pending = 0; }
	(void) fence;						// (one composite in flight at most: it is that one)
	return g->job.result;
}
