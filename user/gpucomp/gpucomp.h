//
// gpucomp.h -- the GPU compositing service (stage 1 of the browser's GPU compositing, docs/07 §6):
// an app uploads layers (ARGB premultiplied pixels) as textures once, then has a list of them
// assembled into a target -- its window's canvas, or any ARGB buffer -- by the V3D (VideoCore VI):
// each layer with a 2D affine transform (translate / scale / rotate / skew), a clip rectangle, an
// opacity, premultiplied source-over blending and bilinear filtering; scrolling a layer = moving
// its source rectangle over the texture (nothing uploaded again). When the GPU is not there (the
// PC, an older kernel, the GPU left off after a hang) the same calls are done by the CPU, so a
// caller never breaks: only the time differs.
//
// Over kapi v53 gpu_texture / gpu_render and v70 gpu_texture_rect / KAPI_GPU_F_ALPHA (docs/02 §15;
// kernel/sys/v3d.cpp). Pure C, no libc: the memory comes from the caller's allocator (config), the
// FPU is used (build with FP/SIMD: user/Makefile's gpucomp/libgpucomp.a). One context per program
// (the textures belong to it; the kernel frees the GPU's copies when the program ends).
//
//   gpc_config cfg = { my_alloc, my_free, 0 };
//   gpc_ctx *g = gpc_create (&cfg);                        // GPU if usable, else the CPU
//   gpc_tex *page = gpc_tex_create (g, 1920, 4000, pixels, 1920);
//   gpc_layer L; gpc_layer_init (&L, page);                 // the whole texture, identity, opaque
//   L.src_y = scroll; L.src_h = 1080;                        // a window of it: scrolling = src_y
//   gpc_target t = { canvas, w, h, stride, 0 };
//   gpc_composite (g, &t, &L, 1, 0xFFFFFF, GPC_C_CLEAR);     // (pixels in the canvas when it returns)
//   gpc_tex_update (g, page, x, y, w2, h2, newpx, stride2); // a damaged rectangle only
//
#ifndef ONYX_GPUCOMP_H
#define ONYX_GPUCOMP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gpc_ctx gpc_ctx;
typedef struct gpc_tex gpc_tex;

// ---- the context -------------------------------------------------------------------------------------
typedef struct gpc_config
{
	void *(*alloc) (unsigned long bytes);	// the caller's allocator (malloc, umm_malloc...): required
	void (*free) (void *p);
	unsigned flags;				// GPC_F_*
} gpc_config;
#define GPC_F_CPU	(1u << 0)	// never the GPU (tests, comparisons)
#define GPC_F_ASYNC	(1u << 1)	// gpc_submit composites on a thread of the program (kapi v67):
					// the caller goes on until gpc_wait (without threads: at once)

gpc_ctx *gpc_create (const gpc_config *cfg);	// 0: no memory
void gpc_destroy (gpc_ctx *g);			// its textures too
#define GPC_BACKEND_CPU	0
#define GPC_BACKEND_GPU	1
int gpc_backend (const gpc_ctx *g);		// what the next composite uses
const char *gpc_info (const gpc_ctx *g);	// "GPU: V3D 4.2 (1 core)" / "CPU: <why not the GPU>"

// ---- textures: w x h pixels 0xAARRGGBB, premultiplied (r, g, b <= a) ---------------------------------
// Any size: past the GPU's 2048 x 2048 a texture is kept as tiles (with a border of one texel
// between them: bilinear filtering does not see the seams). pixels 0: transparent.
gpc_tex *gpc_tex_create (gpc_ctx *g, int w, int h, const unsigned *pixels, int stride);
// a rectangle replaced (clipped to the texture) -> 0, < 0 GPC_E*
int gpc_tex_update (gpc_ctx *g, gpc_tex *t, int x, int y, int w, int h, const unsigned *pixels, int stride);
void gpc_tex_destroy (gpc_ctx *g, gpc_tex *t);
void gpc_tex_size (const gpc_tex *t, int *w, int *h);
// the texture's pixels were lost with the GPU (gpc_composite returned GPC_LOST): until updated
// again (the whole of it) it draws nothing
int gpc_tex_lost (const gpc_tex *t);

// ---- layers --------------------------------------------------------------------------------------------
// A 2D affine transform, the CSS / canvas matrix(a, b, c, d, e, f): x' = a x + c y + e, y' = b x + d y + f
typedef struct gpc_matrix { float a, b, c, d, e, f; } gpc_matrix;

typedef struct gpc_layer
{
	gpc_tex *tex;
	float src_x, src_y, src_w, src_h;	// the texture's rectangle shown (texels; fractions: smooth
						// scrolling). Layer space = this rectangle, its top-left at 0, 0
	gpc_matrix m;				// layer space -> target pixels
	int clip[4];				// x, y, w, h in the target (w <= 0: none)
	unsigned opacity;			// 0..255 (the layer's pixels scaled by it)
	unsigned flags;				// GPC_L_*
} gpc_layer;
#define GPC_L_NEAREST	(1u << 0)	// nearest texel (else bilinear)
#define GPC_L_OPAQUE	(1u << 1)	// every pixel's alpha is 255: no blending (faster) at opacity 255

void gpc_layer_init (gpc_layer *L, gpc_tex *t);	// the whole texture, identity, no clip, opacity 255

// matrices: m = m * op (op applied first, as CSS transform lists read left to right)
void gpc_matrix_identity (gpc_matrix *m);
void gpc_matrix_translate (gpc_matrix *m, float tx, float ty);
void gpc_matrix_scale (gpc_matrix *m, float sx, float sy);
void gpc_matrix_rotate (gpc_matrix *m, float radians);	// clockwise on the screen (y down)
void gpc_matrix_skew (gpc_matrix *m, float ax, float ay);	// radians (CSS skew(ax, ay))
void gpc_matrix_multiply (gpc_matrix *m, const gpc_matrix *op);
int gpc_matrix_invert (const gpc_matrix *m, gpc_matrix *inv);	// 0: not invertible
void gpc_sincos (float radians, float *s, float *c);		// (no libm needed)

// ---- compositing ---------------------------------------------------------------------------------------
typedef struct gpc_target
{
	unsigned *pixels;
	int w, h, stride;		// stride: pixels a row
	unsigned flags;			// GPC_T_*
} gpc_target;
#define GPC_T_ALPHA	(1u << 0)	// the target is 0xAARRGGBB premultiplied (an off-screen layer): its
					// alpha is blended too. Else 0x00RRGGBB (a window's canvas): the top
					// byte is left as it is (0 where cleared)

#define GPC_C_CLEAR	(1u << 0)	// the target cleared to `clear` first (0xRRGGBB; GPC_T_ALPHA: 0xAARRGGBB)
					// else the layers are drawn over what it holds

// n layers, bottom first, into t -> 0 / GPC_LOST / < 0. The pixels are in t when it returns.
// For a damaged rectangle only: a target of that rectangle (pixels + y * stride + x, w, h) and
// the layers' matrices translated by -x, -y. The GPU renders straight into targets it can reach
// (physically contiguous below 1 GB: a window canvas, gpc_target_alloc's buffers); any other
// buffer costs a copy there and back in the kernel.
int gpc_composite (gpc_ctx *g, const gpc_target *t, const gpc_layer *layers, int n, unsigned clear, unsigned flags);
// the same, returned at once when GPC_F_ASYNC (the layers are copied; do not touch the target,
// nor update / destroy the textures -- those calls wait -- until gpc_wait) -> a fence (> 0) / < 0
int gpc_submit (gpc_ctx *g, const gpc_target *t, const gpc_layer *layers, int n, unsigned clear, unsigned flags);
int gpc_wait (gpc_ctx *g, int fence);		// -> that composite's result

#define GPC_OK		0
#define GPC_LOST	1	// done, but the GPU stopped: the CPU from now on; the textures that
				// were on the GPU are lost (gpc_tex_lost): upload them again
#define GPC_EINVAL	(-2)
#define GPC_ENOMEM	(-4)

// A w x h buffer the GPU renders into directly (kapi v63 gpu_vbuf: low, contiguous -- 8 at most
// a program, 64 MB each, kept until the program ends); the caller's allocator when the GPU is
// not used -> the pixels (*stride = w), 0 none.
unsigned *gpc_target_alloc (gpc_ctx *g, int w, int h, int *stride);
void gpc_target_free (gpc_ctx *g, unsigned *pixels);	// (a gpu_vbuf's: kept, nothing done)

// ---- measures --------------------------------------------------------------------------------------------
typedef struct gpc_stats
{
	unsigned last_us;		// the last composite, whole (with the kernel's work)
	unsigned last_gpu_calls;	// its gpu_render calls (0: all on the CPU)
	unsigned last_cpu_layers;	// its layers drawn by the CPU
	unsigned frames, gpu_frames;
	unsigned long long upload_bytes;	// texture pixels given to the GPU
} gpc_stats;
void gpc_get_stats (const gpc_ctx *g, gpc_stats *s);

#ifdef __cplusplus
}
#endif
#endif
