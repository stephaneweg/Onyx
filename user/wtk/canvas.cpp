//
// wtk/canvas.cpp -- Canvas implementation (compiled into libwtk.a).
//
// Hot pixel paths (clear / fillRect / putOther) are NEON-accelerated: 4 pixels per
// iteration, branchless transparent-key blit via compare+select. This TU is built
// with FP/SIMD enabled (see user/Makefile: wtk/canvas.o drops -mgeneral-regs-only),
// while the rest of wtk and every app stay integer-only -- the NEON is confined here
// and Canvas's public signatures use no FP regs, so the calling ABI is unchanged. The
// kernel saves the full FP/SIMD state on every trap, so SIMD is safe across switches.
// A scalar fallback is kept for builds without NEON (__ARM_NEON undefined).
//
#include "canvas.h"
#include "wtk/font.h"		// wtk::font (route text through the global Sans family)
#include "kapi.h"		// kapi_draw_text_buf (fallback when no font is loaded)
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif
// operator new[]/delete[] resolve at link from the app's onyxpp.hpp (do NOT include it
// here: its non-inline __cxa_* symbols are "include once per app").

namespace wtk {

// ---- SIMD span helpers (file-local) -----------------------------------------

// Fill n pixels at p with the solid colour c.
static inline void fillSpan (unsigned *p, int n, unsigned c)
{
#ifdef __ARM_NEON
	uint32x4_t v = vdupq_n_u32 (c);
	int i = 0;
	for (; i + 16 <= n; i += 16)			// 16 px / iter
	{
		vst1q_u32 (p + i,      v);
		vst1q_u32 (p + i + 4,  v);
		vst1q_u32 (p + i + 8,  v);
		vst1q_u32 (p + i + 12, v);
	}
	for (; i + 4 <= n; i += 4) vst1q_u32 (p + i, v);
	for (; i < n; i++) p[i] = c;
#else
	for (int i = 0; i < n; i++) p[i] = c;
#endif
}

// Opaque copy of n pixels src -> dst.
static inline void copySpan (unsigned *d, const unsigned *s, int n)
{
#ifdef __ARM_NEON
	int i = 0;
	for (; i + 16 <= n; i += 16)			// 16 px / iter
	{
		vst1q_u32 (d + i,      vld1q_u32 (s + i));
		vst1q_u32 (d + i + 4,  vld1q_u32 (s + i + 4));
		vst1q_u32 (d + i + 8,  vld1q_u32 (s + i + 8));
		vst1q_u32 (d + i + 12, vld1q_u32 (s + i + 12));
	}
	for (; i + 4 <= n; i += 4) vst1q_u32 (d + i, vld1q_u32 (s + i));
	for (; i < n; i++) d[i] = s[i];
#else
	for (int i = 0; i < n; i++) d[i] = s[i];
#endif
}

// Transparent copy of n pixels: write src except where it equals the magenta key
// (then keep dst). Branchless: result = (src==key) ? dst : src, via compare+bitselect.
static inline void keySpan (unsigned *d, const unsigned *s, int n)
{
#ifdef __ARM_NEON
	uint32x4_t key = vdupq_n_u32 (WK_TRANSPARENT_KEY);
	int i = 0;
	for (; i + 4 <= n; i += 4)
	{
		uint32x4_t sv = vld1q_u32 (s + i);
		uint32x4_t dv = vld1q_u32 (d + i);
		uint32x4_t m  = vceqq_u32 (sv, key);	// all-ones lanes where transparent
		vst1q_u32 (d + i, vbslq_u32 (m, dv, sv));	// m ? dst : src
	}
	for (; i < n; i++) { unsigned p = s[i]; if (p != WK_TRANSPARENT_KEY) d[i] = p; }
#else
	for (int i = 0; i < n; i++) { unsigned p = s[i]; if (p != WK_TRANSPARENT_KEY) d[i] = p; }
#endif
}

// ---- Canvas -----------------------------------------------------------------

Canvas::Canvas () : px (0), w (0), h (0), stride (0), owns (false), capH (0) {}
Canvas::~Canvas () { release (); }

void Canvas::release () { if (owns && px) delete [] px; px = 0; w = h = stride = capH = 0; owns = false; }

bool Canvas::alloc (int W, int H)
{
	release ();
	if (W < 1) W = 1;
	if (H < 1) H = 1;
	px = new unsigned[(unsigned) (W * H)];
	w = W; h = H; stride = W; capH = H; owns = (px != 0);
	return px != 0;
}

void Canvas::adopt (unsigned *p, int W, int H) { release (); px = p; w = W; h = H; stride = W; capH = H; owns = false; }
void Canvas::adopt (unsigned *p, int W, int H, int Stride)
{ release (); px = p; w = W; h = H; stride = Stride < W ? W : Stride; capH = H; owns = false; }

bool Canvas::resize (int W, int H)		// grow-only for owned buffers (see canvas.h)
{
	if (W < 1) W = 1;
	if (H < 1) H = 1;
	if (owns && px && W <= stride && H <= capH) { w = W; h = H; return true; }	// fits: reuse
	int nsw = (W > stride) ? (W > stride * 2 ? W : stride * 2) : stride;	// grow ~2x
	int nch = (H > capH)   ? (H > capH * 2   ? H : capH * 2)   : capH;
	if (nsw < W) nsw = W;
	if (nch < H) nch = H;
	unsigned *np = new unsigned[(unsigned) (nsw * nch)];
	if (np == 0) return false;
	if (owns && px) delete [] px;		// (no-op for an adopted buffer -- we never free that)
	px = np; w = W; h = H; stride = nsw; capH = nch; owns = true;
	return true;
}

void Canvas::setLogical (int W, int H)		// keep the buffer + stride; change only the visible size
{ if (W < 1) W = 1; if (H < 1) H = 1; w = W; h = H; }

void Canvas::clear (unsigned c)
{
	if (stride == w) { fillSpan (px, w * h, c); return; }	// contiguous: one span
	for (int yy = 0; yy < h; yy++) fillSpan (px + yy * stride, w, c);
}

void Canvas::pixel (int x, int y, unsigned c)
{ if (x >= 0 && x < w && y >= 0 && y < h) px[y * stride + x] = c; }

void Canvas::fillRect (int x, int y, int rw, int rh, unsigned c)
{
	int x0 = x < 0 ? 0 : x;
	int y0 = y < 0 ? 0 : y;
	int x1 = x + rw; if (x1 > w) x1 = w;
	int y1 = y + rh; if (y1 > h) y1 = h;
	int n = x1 - x0;
	if (n <= 0) return;
	for (int yy = y0; yy < y1; yy++)
		fillSpan (px + yy * stride + x0, n, c);
}

void Canvas::frameRect (int x, int y, int rw, int rh, unsigned c)
{
	fillRect (x, y, rw, 1, c);
	fillRect (x, y + rh - 1, rw, 1, c);
	fillRect (x, y, 1, rh, c);
	fillRect (x + rw - 1, y, 1, rh, c);
}

// All widget text renders through the global Sans family (so the toolkit uses the loaded
// bitmap font, not the kernel's). Falls back to the kernel font if no .fnt was loaded.
void Canvas::text (int x, int y, const char *s, unsigned c)
{
	Font &f = font ();
	if (f.valid ()) drawFont (x, y, s, f, c, 1, 0);
	else            kapi_draw_text_buf (px, stride, h, x, y, s, c);	// stride = real row width
}

void Canvas::putOther (const Canvas &src, int dx, int dy, bool transparent)
{
	// Clip the source rectangle to this canvas once (per-span), not per-pixel.
	int sx0 = dx < 0 ? -dx : 0;			// first src col to copy
	int sy0 = dy < 0 ? -dy : 0;			// first src row
	int sx1 = src.w; if (dx + sx1 > w) sx1 = w - dx;	// one past last src col
	int sy1 = src.h; if (dy + sy1 > h) sy1 = h - dy;	// one past last src row
	int n = sx1 - sx0;
	if (n <= 0) return;
	for (int sy = sy0; sy < sy1; sy++)
	{
		const unsigned *s = src.px + sy * src.stride + sx0;
		unsigned       *d = px + (dy + sy) * stride + (dx + sx0);
		if (transparent) keySpan (d, s, n);
		else             copySpan (d, s, n);
	}
}

} // namespace wtk
