/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Onyx: the framebuffer's compositing groups (netsurf/onyx_paint.h's struct onyx_layer) --
 * CSS opacity, transform, filter, backdrop-filter and mix-blend-mode.
 *
 * In place (an opacity alone): what is under the group is copied, the core paints the group
 * where it is, then the copy is blended back, at 1 - opacity: exact (the group over the page
 * at a, blended with the page at 1 - a, is the group at opacity a over the page), one pass.
 *
 * Apart (a transform, a filter, a blend mode): the core paints the group twice into a RAM
 * surface of the layer's size, over black then over white; each pixel's coverage is 1 less
 * the difference, its premultiplied colour the black pass -- exact, as every plotter blends
 * linearly (no plotter tracks an alpha channel). The layer (0xAARRGGBB premultiplied) is
 * then filtered -- the colour functions per pixel (tables, matrices), blur as three box
 * blurs, drop-shadow a blurred copy of its alpha -- and drawn onto the surface under it:
 * through the inverse of its matrix, bilinear, at its opacity, blended.
 *
 * The surfaces, the copies and the work buffers are kept from one group to the next (one set
 * a nesting level).
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libnsfb.h>
#include <libnsfb_plot.h>

#include "utils/utils.h"
#include "netsurf/types.h"
#include "netsurf/onyx_paint.h"

struct fbtk_bitmap;	/* (framebuffer.h names it) */
#include "framebuffer/framebuffer.h"
#include "framebuffer/onyx_paint.h"

#define LAYER_DEPTH 8

/* ---- a surface's 32 bpp pixels ---------------------------------------------------------- */

struct surf {
	uint8_t *base;
	int stride;		/* bytes a row */
	int w, h;
	bool bgr;		/* 0x..BBGGRR pixels (else 0x..RRGGBB) */
	enum nsfb_format_e fmt;
	nsfb_bbox_t clip;	/* the plot clip, within the surface */
};

static bool surf_get(nsfb_t *nsfb, struct surf *s)
{
	if (nsfb == NULL || nsfb_get_geometry(nsfb, &s->w, &s->h, &s->fmt) != 0)
		return false;
	if (nsfb_get_buffer(nsfb, &s->base, &s->stride) != 0 || s->base == NULL)
		return false;
	switch (s->fmt) {
	case NSFB_FMT_XRGB8888:
	case NSFB_FMT_ARGB8888:
		s->bgr = false;
		break;
	case NSFB_FMT_XBGR8888:
	case NSFB_FMT_ABGR8888:
		s->bgr = true;
		break;
	default:
		return false;
	}
	if (!nsfb_plot_get_clip(nsfb, &s->clip)) {
		s->clip.x0 = s->clip.y0 = 0;
		s->clip.x1 = s->w;
		s->clip.y1 = s->h;
	}
	if (s->clip.x0 < 0) s->clip.x0 = 0;
	if (s->clip.y0 < 0) s->clip.y0 = 0;
	if (s->clip.x1 > s->w) s->clip.x1 = s->w;
	if (s->clip.y1 > s->h) s->clip.y1 = s->h;
	return true;
}

static inline uint32_t *surf_at(const struct surf *s, int x, int y)
{
	return (uint32_t *) (s->base + (size_t) y * s->stride) + x;
}

/** A surface pixel as 0x00RRGGBB. */
static inline uint32_t px_rgb(bool bgr, uint32_t p)
{
	if (bgr)
		return ((p & 0xff) << 16) | (p & 0xff00) | ((p >> 16) & 0xff);
	return p & 0xffffff;
}

/** 0x00RRGGBB back into a surface pixel (its top byte kept). */
static inline uint32_t rgb_px(bool bgr, uint32_t old, uint32_t rgb)
{
	if (bgr)
		rgb = ((rgb & 0xff) << 16) | (rgb & 0xff00) | ((rgb >> 16) & 0xff);
	return (old & 0xff000000) | rgb;
}

/** v / 255, rounded (v <= 255 * 255) */
static inline unsigned div255(unsigned v)
{
	v += 128;
	return (v + (v >> 8)) >> 8;
}

/* ---- the levels --------------------------------------------------------------------------- */

struct fb_layer {
	nsfb_t *pass[2];	/* the black and the white pass (kept) */
	nsfb_t *under;		/* the surface the group is drawn onto */
	int x0, y0, x1, y1;	/* in place: the copy's rectangle */
	uint32_t *save;		/* in place: what was under */
	size_t save_cap;
	uint32_t *argb;		/* apart: the layer, premultiplied 0xAARRGGBB */
	size_t argb_cap;
	uint32_t *tmp;		/* the filters' work */
	size_t tmp_cap;
};

static struct fb_layer levels[LAYER_DEPTH];
static int depth;
static uint32_t *line_a, *line_b;	/* a blur's line */
static size_t line_cap;
static uint32_t *bd_buf;		/* backdrop-filter's copy of what is under */
static size_t bd_cap;
static uint32_t *col_tab;		/* a scaled layer's columns (source, weight) */
static size_t col_cap;

static bool grow(uint32_t **buf, size_t *cap, size_t n)
{
	uint32_t *p;

	if (n <= *cap)
		return true;
	p = realloc(*buf, n * sizeof(uint32_t));
	if (p == NULL)
		return false;
	*buf = p;
	*cap = n;
	return true;
}

/* ---- blur: three box blurs (Filter Effects' approximation of a Gaussian) --------------- */

/** dst[i] = the mean of src[i - lo .. i - lo + size - 1], outside the line 0 (or its end
 * pixels, clamp) -- four 8-bit channels at once */
static void box_line(const uint32_t *src, uint32_t *dst, int n, int size, int lo, bool clamp)
{
	uint32_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
	uint64_t inv = ((uint64_t) 1 << 32) / (unsigned) size;
	int i, j;

#define ADD(p) do { uint32_t v_ = (p); s0 += v_ & 0xff; s1 += (v_ >> 8) & 0xff; \
		s2 += (v_ >> 16) & 0xff; s3 += v_ >> 24; } while (0)
#define SUB(p) do { uint32_t v_ = (p); s0 -= v_ & 0xff; s1 -= (v_ >> 8) & 0xff; \
		s2 -= (v_ >> 16) & 0xff; s3 -= v_ >> 24; } while (0)
#define AT(k) ((k) < 0 ? (clamp ? src[0] : 0) : (k) >= n ? (clamp ? src[n - 1] : 0) : src[k])
	for (j = -lo; j < -lo + size; j++)
		ADD(AT(j));
	for (i = 0; i < n; i++) {
		dst[i] = (uint32_t) ((s0 * inv + ((uint64_t) 1 << 31)) >> 32) |
			((uint32_t) ((s1 * inv + ((uint64_t) 1 << 31)) >> 32) << 8) |
			((uint32_t) ((s2 * inv + ((uint64_t) 1 << 31)) >> 32) << 16) |
			((uint32_t) ((s3 * inv + ((uint64_t) 1 << 31)) >> 32) << 24);
		SUB(AT(i - lo));
		ADD(AT(i - lo + size));
	}
#undef ADD
#undef SUB
#undef AT
}

/** The three boxes' sizes and offsets for a standard deviation (false: too small). */
static bool blur_boxes(float sigma, int size[3], int lo[3])
{
	int d = (int) floorf(sigma * 3 * sqrtf(2 * 3.14159265f) / 4 + 0.5f);

	if (d <= 1)
		return false;
	if (d & 1) {
		size[0] = size[1] = size[2] = d;
		lo[0] = lo[1] = lo[2] = (d - 1) / 2;
	} else {
		size[0] = size[1] = d;
		lo[0] = d / 2;
		lo[1] = d / 2 - 1;
		size[2] = d + 1;
		lo[2] = d / 2;
	}
	return true;
}

/** buf (w x h) blurred, sigma px (clamp: the edges' pixels repeated, else transparent) */
static bool blur_full(uint32_t *buf, int w, int h, float sigma, bool clamp);

static uint32_t *small_buf;		/* a large blur's reduced copy */
static size_t small_cap;

/**
 * buf (w x h) blurred, sigma px (clamp: the edges' pixels repeated, else transparent). A
 * large blur is done on a copy reduced by s (a power of 2, sigma / s at least 2), then
 * brought back bilinearly: what a wide blur leaves is smooth, and it costs s^2 less.
 */
static bool blur(uint32_t *buf, int w, int h, float sigma, bool clamp)
{
	int s = 1, sw, sh;

	while (sigma / (s * 2) >= 1.5f && s < 16)
		s *= 2;
	if (s == 1 || w < 4 * s || h < 4 * s)
		return blur_full(buf, w, h, sigma, clamp);
	sw = (w + s - 1) / s;
	sh = (h + s - 1) / s;
	if (!grow(&small_buf, &small_cap, (size_t) sw * sh))
		return blur_full(buf, w, h, sigma, clamp);
	/* reduced: each s x s block's mean (the pixels in the layer) */
	for (int y = 0; y < sh; y++) {
		for (int x = 0; x < sw; x++) {
			uint32_t c0 = 0, c1 = 0, c2 = 0, c3 = 0, n = 0;
			for (int yy = y * s; yy < y * s + s && yy < h; yy++) {
				const uint32_t *p = buf + (size_t) yy * w + x * s;
				for (int xx = 0; xx < s && x * s + xx < w; xx++) {
					uint32_t v = p[xx];
					c0 += v & 0xff;
					c1 += (v >> 8) & 0xff;
					c2 += (v >> 16) & 0xff;
					c3 += v >> 24;
					n++;
				}
			}
			small_buf[(size_t) y * sw + x] = (c0 / n) | ((c1 / n) << 8) |
				((c2 / n) << 16) | ((c3 / n) << 24);
		}
	}
	/* (the block mean's own spread taken off) */
	{
		float v = sigma * sigma - (s * s - 1) / 12.0f;
		if (!blur_full(small_buf, sw, sh, sqrtf(v > 1 ? v : 1) / s, clamp))
			return false;
	}
	/* back: bilinear, the reduced pixels' centres at (x + 0.5) s */
	for (int y = 0; y < h; y++) {
		float fy = (y + 0.5f) / s - 0.5f;
		int y0 = (int) floorf(fy);
		unsigned ay = (unsigned) ((fy - y0) * 256);
		int ya = y0 < 0 ? 0 : y0 >= sh ? sh - 1 : y0;
		int yb = y0 + 1 < 0 ? 0 : y0 + 1 >= sh ? sh - 1 : y0 + 1;
		const uint32_t *ra = small_buf + (size_t) ya * sw, *rb = small_buf + (size_t) yb * sw;
		uint32_t *o = buf + (size_t) y * w;
		for (int x = 0; x < w; x++) {
			float fx = (x + 0.5f) / s - 0.5f;
			int x0 = (int) floorf(fx);
			unsigned ax = (unsigned) ((fx - x0) * 256);
			int xa = x0 < 0 ? 0 : x0 >= sw ? sw - 1 : x0;
			int xb = x0 + 1 < 0 ? 0 : x0 + 1 >= sw ? sw - 1 : x0 + 1;
			uint32_t p = ra[xa], q = ra[xb], r = rb[xa], t = rb[xb];
			uint32_t top, bot;
			top = ((((p & 0x00ff00ff) * (256 - ax) + (q & 0x00ff00ff) * ax) >> 8) &
					0x00ff00ff) |
				((((p >> 8) & 0x00ff00ff) * (256 - ax) +
				  ((q >> 8) & 0x00ff00ff) * ax) & 0xff00ff00);
			bot = ((((r & 0x00ff00ff) * (256 - ax) + (t & 0x00ff00ff) * ax) >> 8) &
					0x00ff00ff) |
				((((r >> 8) & 0x00ff00ff) * (256 - ax) +
				  ((t >> 8) & 0x00ff00ff) * ax) & 0xff00ff00);
			o[x] = ((((top & 0x00ff00ff) * (256 - ay) + (bot & 0x00ff00ff) * ay) >> 8) &
					0x00ff00ff) |
				((((top >> 8) & 0x00ff00ff) * (256 - ay) +
				  ((bot >> 8) & 0x00ff00ff) * ay) & 0xff00ff00);
		}
	}
	return true;
}

/** blur (above), at full size */
static bool blur_full(uint32_t *buf, int w, int h, float sigma, bool clamp)
{
	int size[3], lo[3], n = w > h ? w : h;

	if (!blur_boxes(sigma, size, lo))
		return true;
	if ((size_t) n > line_cap) {
		uint32_t *a = realloc(line_a, n * sizeof(uint32_t));
		uint32_t *b;
		if (a == NULL)
			return false;
		line_a = a;
		b = realloc(line_b, n * sizeof(uint32_t));
		if (b == NULL)
			return false;
		line_b = b;
		line_cap = n;
	}
	for (int y = 0; y < h; y++) {
		uint32_t *row = buf + (size_t) y * w;
		box_line(row, line_a, w, size[0], lo[0], clamp);
		box_line(line_a, line_b, w, size[1], lo[1], clamp);
		box_line(line_b, row, w, size[2], lo[2], clamp);
	}
	for (int x = 0; x < w; x++) {
		for (int y = 0; y < h; y++)
			line_a[y] = buf[(size_t) y * w + x];
		box_line(line_a, line_b, h, size[0], lo[0], clamp);
		box_line(line_b, line_a, h, size[1], lo[1], clamp);
		box_line(line_a, line_b, h, size[2], lo[2], clamp);
		for (int y = 0; y < h; y++)
			buf[(size_t) y * w + x] = line_b[y];
	}
	return true;
}

/* ---- the colour functions ---------------------------------------------------------------- */

struct cstep {
	int kind;		/* 0: a table per channel, 1: a matrix, 2: the alpha scaled */
	uint8_t lut[256];
	int m[9];		/* (x 1024) */
	unsigned alpha;		/* (x 256) */
};

static uint8_t clamp8f(float v)
{
	return v <= 0 ? 0 : v >= 255 ? 255 : (uint8_t) (v + 0.5f);
}

static void cstep_make(const struct onyx_filter *f, struct cstep *c)
{
	float v = f->v, s = 1 - f->v, m[9];
	int i;

	c->kind = 1;
	switch (f->op) {
	case ONYX_FILTER_BRIGHTNESS:
		c->kind = 0;
		for (i = 0; i < 256; i++)
			c->lut[i] = clamp8f(i * v);
		return;
	case ONYX_FILTER_CONTRAST:
		c->kind = 0;
		for (i = 0; i < 256; i++)
			c->lut[i] = clamp8f((i - 127.5f) * v + 127.5f);
		return;
	case ONYX_FILTER_INVERT:
		c->kind = 0;
		for (i = 0; i < 256; i++)
			c->lut[i] = clamp8f(i * (1 - v) + (255 - i) * v);
		return;
	case ONYX_FILTER_OPACITY:
		c->kind = 2;
		c->alpha = (unsigned) (v * 256 + 0.5f);
		return;
	case ONYX_FILTER_GRAYSCALE:
		m[0] = 0.2126f + 0.7874f * s; m[1] = 0.7152f - 0.7152f * s;
		m[2] = 0.0722f - 0.0722f * s;
		m[3] = 0.2126f - 0.2126f * s; m[4] = 0.7152f + 0.2848f * s;
		m[5] = 0.0722f - 0.0722f * s;
		m[6] = 0.2126f - 0.2126f * s; m[7] = 0.7152f - 0.7152f * s;
		m[8] = 0.0722f + 0.9278f * s;
		break;
	case ONYX_FILTER_SEPIA:
		m[0] = 0.393f + 0.607f * s; m[1] = 0.769f - 0.769f * s; m[2] = 0.189f - 0.189f * s;
		m[3] = 0.349f - 0.349f * s; m[4] = 0.686f + 0.314f * s; m[5] = 0.168f - 0.168f * s;
		m[6] = 0.272f - 0.272f * s; m[7] = 0.534f - 0.534f * s; m[8] = 0.131f + 0.869f * s;
		break;
	case ONYX_FILTER_SATURATE:
		m[0] = 0.213f + 0.787f * v; m[1] = 0.715f - 0.715f * v; m[2] = 0.072f - 0.072f * v;
		m[3] = 0.213f - 0.213f * v; m[4] = 0.715f + 0.285f * v; m[5] = 0.072f - 0.072f * v;
		m[6] = 0.213f - 0.213f * v; m[7] = 0.715f - 0.715f * v; m[8] = 0.072f + 0.928f * v;
		break;
	case ONYX_FILTER_HUE_ROTATE: {
		float a = v * 3.14159265f / 180, co = cosf(a), si = sinf(a);
		m[0] = 0.213f + co * 0.787f - si * 0.213f;
		m[1] = 0.715f - co * 0.715f - si * 0.715f;
		m[2] = 0.072f - co * 0.072f + si * 0.928f;
		m[3] = 0.213f - co * 0.213f + si * 0.143f;
		m[4] = 0.715f + co * 0.285f + si * 0.140f;
		m[5] = 0.072f - co * 0.072f - si * 0.283f;
		m[6] = 0.213f - co * 0.213f - si * 0.787f;
		m[7] = 0.715f - co * 0.715f + si * 0.715f;
		m[8] = 0.072f + co * 0.928f + si * 0.072f;
		break;
	}
	default:
		c->kind = 0;
		for (i = 0; i < 256; i++)
			c->lut[i] = i;
		return;
	}
	for (i = 0; i < 9; i++)
		c->m[i] = (int) lrintf(m[i] * 1024);
}

static inline unsigned clamp8i(int v)
{
	return v < 0 ? 0 : v > 255 ? 255 : (unsigned) v;
}

/** The colour functions f[0 .. n - 1] on buf's pixels (premultiplied 0xAARRGGBB). */
static void colour_steps(uint32_t *buf, size_t npx, const struct onyx_filter *f, int n)
{
	struct cstep *st = malloc(n * sizeof(*st));
	bool colour = false;

	if (st == NULL)
		return;
	for (int k = 0; k < n; k++) {
		cstep_make(&f[k], &st[k]);
		if (st[k].kind != 2)
			colour = true;
	}
	for (size_t i = 0; i < npx; i++) {
		uint32_t p = buf[i];
		unsigned a = p >> 24, r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;

		if (a == 0)
			continue;
		if (colour && a < 255) {
			/* (unpremultiplied) */
			r = r * 255 / a;
			g = g * 255 / a;
			b = b * 255 / a;
			if (r > 255) r = 255;
			if (g > 255) g = 255;
			if (b > 255) b = 255;
		}
		for (int k = 0; k < n; k++) {
			const struct cstep *c = &st[k];
			if (c->kind == 0) {
				r = c->lut[r];
				g = c->lut[g];
				b = c->lut[b];
			} else if (c->kind == 1) {
				int nr = (c->m[0] * (int) r + c->m[1] * (int) g + c->m[2] * (int) b +
						512) >> 10;
				int ng = (c->m[3] * (int) r + c->m[4] * (int) g + c->m[5] * (int) b +
						512) >> 10;
				int nb = (c->m[6] * (int) r + c->m[7] * (int) g + c->m[8] * (int) b +
						512) >> 10;
				r = clamp8i(nr);
				g = clamp8i(ng);
				b = clamp8i(nb);
			} else {
				a = (a * c->alpha + 128) >> 8;
				if (a > 255)
					a = 255;
				if (!colour) {
					/* (premultiplied: the colour scaled too) */
					r = (r * c->alpha + 128) >> 8;
					g = (g * c->alpha + 128) >> 8;
					b = (b * c->alpha + 128) >> 8;
				}
			}
		}
		if (colour && a < 255) {
			r = div255(r * a);
			g = div255(g * a);
			b = div255(b * a);
		}
		if (r > a) r = a;
		if (g > a) g = a;
		if (b > a) b = a;
		buf[i] = (a << 24) | (r << 16) | (g << 8) | b;
	}
	free(st);
}

/** drop-shadow: buf over its alpha blurred (sigma: half the radius), coloured, moved */
static bool drop_shadow(struct fb_layer *L, uint32_t *buf, int w, int h,
		const struct onyx_filter *f)
{
	size_t npx = (size_t) w * h;
	int dx = (int) lrintf(f->dx), dy = (int) lrintf(f->dy);
	unsigned ca = f->argb >> 24, cr = (f->argb >> 16) & 0xff, cg = (f->argb >> 8) & 0xff,
		cb = f->argb & 0xff;

	if (!grow(&L->tmp, &L->tmp_cap, npx))
		return false;
	for (size_t i = 0; i < npx; i++)
		L->tmp[i] = buf[i] & 0xff000000;
	if (!blur(L->tmp, w, h, f->v / 2, false))
		return false;
	for (int y = 0; y < h; y++) {
		int sy = y - dy;
		for (int x = 0; x < w; x++) {
			int sx = x - dx;
			uint32_t p = buf[(size_t) y * w + x], s;
			unsigned sa, pa = p >> 24, k;

			if (pa == 255 || sx < 0 || sy < 0 || sx >= w || sy >= h)
				continue;
			sa = div255((L->tmp[(size_t) sy * w + sx] >> 24) * ca);
			if (sa == 0)
				continue;
			/* the shadow (premultiplied) under the pixel */
			k = 255 - pa;
			s = (div255(sa * k) << 24) | (div255(div255(cr * sa) * k) << 16) |
				(div255(div255(cg * sa) * k) << 8) | div255(div255(cb * sa) * k);
			buf[(size_t) y * w + x] = p + s;
		}
	}
	return true;
}

/** The filters on a layer (premultiplied, w x h). */
static void apply_filters(struct fb_layer *L, uint32_t *buf, int w, int h,
		const struct onyx_filter *f, int n, bool clamp)
{
	int i = 0;

	while (i < n) {
		if (f[i].op == ONYX_FILTER_BLUR) {
			blur(buf, w, h, f[i].v, clamp);
			i++;
		} else if (f[i].op == ONYX_FILTER_DROP_SHADOW) {
			drop_shadow(L, buf, w, h, &f[i]);
			i++;
		} else {
			int j = i;
			while (j < n && f[j].op != ONYX_FILTER_BLUR &&
					f[j].op != ONYX_FILTER_DROP_SHADOW)
				j++;
			colour_steps(buf, (size_t) w * h, f + i, j - i);
			i = j;
		}
	}
}

/* ---- blending ------------------------------------------------------------------------------ */

/** One channel of a separable blend mode (backdrop b, source s, 0..255). */
static inline unsigned blend_ch(enum onyx_blend mode, unsigned b, unsigned s)
{
	switch (mode) {
	case ONYX_BLEND_MULTIPLY:
		return div255(b * s);
	case ONYX_BLEND_SCREEN:
		return b + s - div255(b * s);
	case ONYX_BLEND_OVERLAY:
		return b < 128 ? div255(2 * b * s) : 255 - div255(2 * (255 - b) * (255 - s));
	case ONYX_BLEND_DARKEN:
		return b < s ? b : s;
	case ONYX_BLEND_LIGHTEN:
		return b > s ? b : s;
	case ONYX_BLEND_COLOR_DODGE:
		if (b == 0)
			return 0;
		if (s >= 255)
			return 255;
		return b * 255 / (255 - s) > 255 ? 255 : b * 255 / (255 - s);
	case ONYX_BLEND_COLOR_BURN:
		if (b >= 255)
			return 255;
		if (s == 0)
			return 0;
		return (255 - b) * 255 / s > 255 ? 0 : 255 - (255 - b) * 255 / s;
	case ONYX_BLEND_HARD_LIGHT:
		return s < 128 ? div255(2 * b * s) : 255 - div255(2 * (255 - b) * (255 - s));
	case ONYX_BLEND_SOFT_LIGHT: {
		float fb = b / 255.0f, fs = s / 255.0f, d, r;
		if (fs <= 0.5f) {
			r = fb - (1 - 2 * fs) * fb * (1 - fb);
		} else {
			d = fb <= 0.25f ? ((16 * fb - 12) * fb + 4) * fb : sqrtf(fb);
			r = fb + (2 * fs - 1) * (d - fb);
		}
		return clamp8f(r * 255);
	}
	case ONYX_BLEND_DIFFERENCE:
		return b > s ? b - s : s - b;
	case ONYX_BLEND_EXCLUSION:
		return b + s - 2 * div255(b * s);
	case ONYX_BLEND_PLUS_LIGHTER:
		return b + s > 255 ? 255 : b + s;
	case ONYX_BLEND_PLUS_DARKER:
		return b + s < 255 ? 0 : b + s - 255;
	default:
		return s;
	}
}

/** The premultiplied source s over the surface pixel *p (opaque), blended. */
static inline void over(const struct surf *d, uint32_t *p, uint32_t s, enum onyx_blend mode)
{
	unsigned sa = s >> 24, ia = 255 - sa;
	uint32_t o = px_rgb(d->bgr, *p);
	unsigned br = (o >> 16) & 0xff, bg = (o >> 8) & 0xff, bb = o & 0xff;
	unsigned r, g, b;

	if (sa == 0)
		return;
	if (sa == 255 && mode == ONYX_BLEND_NORMAL) {
		*p = rgb_px(d->bgr, *p, s & 0xffffff);
		return;
	}
	if (mode == ONYX_BLEND_NORMAL) {
		r = ((s >> 16) & 0xff) + div255(br * ia);
		g = ((s >> 8) & 0xff) + div255(bg * ia);
		b = (s & 0xff) + div255(bb * ia);
	} else {
		/* (1 - as) Cb + as B(Cb, Cs), Cs unpremultiplied */
		unsigned sr = ((s >> 16) & 0xff) * 255 / sa, sg = ((s >> 8) & 0xff) * 255 / sa,
			sb = (s & 0xff) * 255 / sa;
		if (sr > 255) sr = 255;
		if (sg > 255) sg = 255;
		if (sb > 255) sb = 255;
		r = div255(br * ia) + div255(blend_ch(mode, br, sr) * sa);
		g = div255(bg * ia) + div255(blend_ch(mode, bg, sg) * sa);
		b = div255(bb * ia) + div255(blend_ch(mode, bb, sb) * sa);
	}
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	*p = rgb_px(d->bgr, *p, (r << 16) | (g << 8) | b);
}

/** A premultiplied pixel at opacity op (x 256). */
static inline uint32_t fade(uint32_t s, unsigned op)
{
	uint32_t rb = ((s & 0x00ff00ff) * op >> 8) & 0x00ff00ff;
	uint32_t ag = (((s >> 8) & 0x00ff00ff) * op) & 0xff00ff00;
	return rb | ag;
}

/** Linear interpolation of two premultiplied pixels (f: 0..256 towards b). */
static inline uint32_t lerp_px(uint32_t a, uint32_t b, unsigned f)
{
	uint32_t rb = (((a & 0x00ff00ff) * (256 - f) + (b & 0x00ff00ff) * f) >> 8) & 0x00ff00ff;
	uint32_t ag = ((((a >> 8) & 0x00ff00ff) * (256 - f) + ((b >> 8) & 0x00ff00ff) * f)) &
		0xff00ff00;
	return rb | ag;
}

/** The layer drawn onto the surface d, within rectangle (cx0..cy1) (an intersection of the
 * layer's clip and the surface's). */
static void composite(const struct onyx_layer *l, const uint32_t *src, const struct surf *d,
		int cx0, int cy0, int cx1, int cy1)
{
	int w = l->x1 - l->x0, h = l->y1 - l->y0;
	unsigned op = (unsigned) (l->opacity * 256 + 0.5f);

	if (op > 256)
		op = 256;
	if (op == 0)
		return;
	if (!l->transformed) {
		int x0 = l->x0 > cx0 ? l->x0 : cx0, y0 = l->y0 > cy0 ? l->y0 : cy0;
		int x1 = l->x1 < cx1 ? l->x1 : cx1, y1 = l->y1 < cy1 ? l->y1 : cy1;

		for (int y = y0; y < y1; y++) {
			const uint32_t *s = src + (size_t) (y - l->y0) * w + (x0 - l->x0);
			uint32_t *p = surf_at(d, x0, y);
			for (int x = x0; x < x1; x++, s++, p++) {
				uint32_t v = *s;
				if (v == 0)
					continue;
				if (op < 256)
					v = fade(v, op);
				over(d, p, v, l->blend);
			}
		}
		return;
	}
	{
		/* through the matrix: each pixel's centre mapped back into the layer */
		const float *m = l->m;
		float det = m[0] * m[3] - m[1] * m[2], iv[6];
		float xs[4], ys[4], bx0, by0, bx1, by1;
		int x0, y0, x1, y1;

		if (fabsf(det) < 1e-9f)
			return;
		iv[0] = m[3] / det;
		iv[1] = -m[1] / det;
		iv[2] = -m[2] / det;
		iv[3] = m[0] / det;
		iv[4] = -(iv[0] * m[4] + iv[2] * m[5]);
		iv[5] = -(iv[1] * m[4] + iv[3] * m[5]);
		xs[0] = xs[3] = l->x0;
		xs[1] = xs[2] = l->x1;
		ys[0] = ys[1] = l->y0;
		ys[2] = ys[3] = l->y1;
		bx0 = by0 = INFINITY;
		bx1 = by1 = -INFINITY;
		for (int i = 0; i < 4; i++) {
			float x = m[0] * xs[i] + m[2] * ys[i] + m[4];
			float y = m[1] * xs[i] + m[3] * ys[i] + m[5];
			if (x < bx0) bx0 = x;
			if (x > bx1) bx1 = x;
			if (y < by0) by0 = y;
			if (y > by1) by1 = y;
		}
		x0 = (int) floorf(bx0);
		y0 = (int) floorf(by0);
		x1 = (int) ceilf(bx1) + 1;
		y1 = (int) ceilf(by1) + 1;
		if (x0 < cx0) x0 = cx0;
		if (y0 < cy0) y0 = cy0;
		if (x1 > cx1) x1 = cx1;
		if (y1 > cy1) y1 = cy1;
		if (m[1] == 0 && m[2] == 0 && x1 > x0 && grow(&col_tab, &col_cap, x1 - x0)) {
			/* a scale (a large blur's layer brought back): each column's source
			 * and weight once, each row's once */
			for (int x = x0; x < x1; x++) {
				float fu = iv[0] * (x + 0.5f) + iv[4] - l->x0 - 0.5f;
				int iu = (int) floorf(fu);
				col_tab[x - x0] = ((uint32_t) (iu + 1) << 8) |
					(uint32_t) ((fu - iu) * 256);
			}
			for (int y = y0; y < y1; y++) {
				float fv = iv[3] * (y + 0.5f) + iv[5] - l->y0 - 0.5f;
				int ivv = (int) floorf(fv);
				unsigned av = (unsigned) ((fv - ivv) * 256);
				const uint32_t *r0 = ivv >= 0 && ivv < h ? src + (size_t) ivv * w : NULL;
				const uint32_t *r1 = ivv + 1 >= 0 && ivv + 1 < h ?
					src + (size_t) (ivv + 1) * w : NULL;
				uint32_t *p = surf_at(d, x0, y);

				if (r0 == NULL && r1 == NULL)
					continue;
				for (int x = x0; x < x1; x++, p++) {
					uint32_t c = col_tab[x - x0];
					int iu = (int) (c >> 8) - 1;
					unsigned au = c & 0xff;
					uint32_t p00, p10, p01, p11, s;

					if (iu < -1 || iu >= w)
						continue;
					p00 = r0 != NULL && iu >= 0 ? r0[iu] : 0;
					p10 = r0 != NULL && iu + 1 < w ? r0[iu + 1] : 0;
					p01 = r1 != NULL && iu >= 0 ? r1[iu] : 0;
					p11 = r1 != NULL && iu + 1 < w ? r1[iu + 1] : 0;
					if ((p00 | p10 | p01 | p11) == 0)
						continue;
					s = lerp_px(lerp_px(p00, p10, au), lerp_px(p01, p11, au), av);
					if (op < 256)
						s = fade(s, op);
					over(d, p, s, l->blend);
				}
			}
			return;
		}
		for (int y = y0; y < y1; y++) {
			/* (16.16 fixed point, stepped along the row) */
			float fu = iv[0] * (x0 + 0.5f) + iv[2] * (y + 0.5f) + iv[4] - l->x0 - 0.5f;
			float fv = iv[1] * (x0 + 0.5f) + iv[3] * (y + 0.5f) + iv[5] - l->y0 - 0.5f;
			int64_t u = (int64_t) (fu * 65536), v = (int64_t) (fv * 65536);
			int64_t du = (int64_t) (iv[0] * 65536), dv = (int64_t) (iv[1] * 65536);
			uint32_t *p = surf_at(d, x0, y);

			for (int x = x0; x < x1; x++, p++, u += du, v += dv) {
				int iu = (int) (u >> 16), ivv = (int) (v >> 16);
				unsigned au = (unsigned) ((u >> 8) & 0xff), av = (unsigned) ((v >> 8) & 0xff);
				uint32_t p00, p10, p01, p11, s;

				if (iu < -1 || ivv < -1 || iu >= w || ivv >= h)
					continue;
				p00 = (iu >= 0 && ivv >= 0) ? src[(size_t) ivv * w + iu] : 0;
				p10 = (iu + 1 < w && ivv >= 0) ? src[(size_t) ivv * w + iu + 1] : 0;
				p01 = (iu >= 0 && ivv + 1 < h) ? src[(size_t) (ivv + 1) * w + iu] : 0;
				p11 = (iu + 1 < w && ivv + 1 < h) ?
					src[(size_t) (ivv + 1) * w + iu + 1] : 0;
				if ((p00 | p10 | p01 | p11) == 0)
					continue;
				s = lerp_px(lerp_px(p00, p10, au), lerp_px(p01, p11, au), av);
				if (op < 256)
					s = fade(s, op);
				over(d, p, s, l->blend);
			}
		}
	}
}

/* ---- backdrop-filter ----------------------------------------------------------------------- */

/** The filters applied to what is under the rounded box (within the clip). */
static void backdrop(struct fb_layer *L, const struct onyx_layer *l, const struct surf *d)
{
	const struct onyx_rrect *r = &l->backdrop_box;
	float out[4] = { 0, 0, 0, 0 };
	int qx0, qy0, qx1, qy1, ex0, ey0, ex1, ey1, w, h;

	/* the box within the clips, and what its blur reads around it */
	qx0 = (int) floorf(r->x0);
	qy0 = (int) floorf(r->y0);
	qx1 = (int) ceilf(r->x1);
	qy1 = (int) ceilf(r->y1);
	if (qx0 < l->cx0) qx0 = l->cx0;
	if (qy0 < l->cy0) qy0 = l->cy0;
	if (qx1 > l->cx1) qx1 = l->cx1;
	if (qy1 > l->cy1) qy1 = l->cy1;
	if (qx0 < d->clip.x0) qx0 = d->clip.x0;
	if (qy0 < d->clip.y0) qy0 = d->clip.y0;
	if (qx1 > d->clip.x1) qx1 = d->clip.x1;
	if (qy1 > d->clip.y1) qy1 = d->clip.y1;
	if (qx0 >= qx1 || qy0 >= qy1)
		return;
	for (int i = 0; i < l->nbackdrop; i++)
		if (l->backdrop[i].op == ONYX_FILTER_BLUR)
			out[0] += ceilf(l->backdrop[i].v * 3) + 1;
	ex0 = qx0 - (int) out[0];
	ey0 = qy0 - (int) out[0];
	ex1 = qx1 + (int) out[0];
	ey1 = qy1 + (int) out[0];
	if (ex0 < 0) ex0 = 0;
	if (ey0 < 0) ey0 = 0;
	if (ex1 > d->w) ex1 = d->w;
	if (ey1 > d->h) ey1 = d->h;
	w = ex1 - ex0;
	h = ey1 - ey0;
	if (!grow(&bd_buf, &bd_cap, (size_t) w * h))
		return;
	for (int y = 0; y < h; y++) {
		const uint32_t *p = surf_at(d, ex0, ey0 + y);
		for (int x = 0; x < w; x++)
			bd_buf[(size_t) y * w + x] = 0xff000000 | px_rgb(d->bgr, p[x]);
	}
	apply_filters(L, bd_buf, w, h, l->backdrop, l->nbackdrop, true);
	for (int y = qy0; y < qy1; y++) {
		uint32_t *p = surf_at(d, qx0, y);
		const uint32_t *b = bd_buf + (size_t) (y - ey0) * w + (qx0 - ex0);
		/* (the row's span clear of the corners and edges: copied) */
		float rl = r->rx[ONYX_TL] > r->rx[ONYX_BL] ? r->rx[ONYX_TL] : r->rx[ONYX_BL];
		float rr = r->rx[ONYX_TR] > r->rx[ONYX_BR] ? r->rx[ONYX_TR] : r->rx[ONYX_BR];
		bool mid = y >= r->y0 + (r->ry[ONYX_TL] > r->ry[ONYX_TR] ? r->ry[ONYX_TL] :
				r->ry[ONYX_TR]) && y + 1 <= r->y1 - (r->ry[ONYX_BL] > r->ry[ONYX_BR] ?
				r->ry[ONYX_BL] : r->ry[ONYX_BR]);
		for (int x = qx0; x < qx1; x++, p++, b++) {
			float cov;
			uint32_t s;
			if (mid && x >= r->x0 + rl && x + 1 <= r->x1 - rr) {
				*p = rgb_px(d->bgr, *p, *b & 0xffffff);
				continue;
			}
			cov = onyx_fb_rrect_cov(r, x + 0.5f, y + 0.5f);
			if (cov <= 0)
				continue;
			s = *b;
			if (cov < 1)
				s = fade(s, (unsigned) (cov * 256));
			over(d, p, s, ONYX_BLEND_NORMAL);
		}
	}
}

/* ---- the passes ------------------------------------------------------------------------------ */

/** A RAM surface of w x h in the format (kept and reused). */
static nsfb_t *pass_surface(nsfb_t *s, int w, int h, enum nsfb_format_e fmt)
{
	if (s == NULL) {
		s = nsfb_new(NSFB_SURFACE_RAM);
		if (s == NULL)
			return NULL;
		if (nsfb_set_geometry(s, w, h, fmt) != 0 || nsfb_init(s) != 0) {
			nsfb_free(s);
			return NULL;
		}
		return s;
	}
	if (nsfb_set_geometry(s, w, h, fmt) != 0)
		return NULL;
	return s;
}

/* exported function documented in framebuffer/onyx_paint.h */
bool onyx_fb_layer_begin(nsfb_t *nsfb, const struct onyx_layer *l, int pass)
{
	struct fb_layer *L;
	struct surf d;

	if (pass == 0) {
		if (depth >= LAYER_DEPTH || !surf_get(nsfb, &d))
			return false;
		L = &levels[depth];
		L->under = nsfb;
	} else {
		if (depth <= 0)
			return false;
		L = &levels[depth - 1];
	}

	if (!l->isolated) {
		int x0 = l->x0 < 0 ? 0 : l->x0, y0 = l->y0 < 0 ? 0 : l->y0;
		int x1 = l->x1 > d.w ? d.w : l->x1, y1 = l->y1 > d.h ? d.h : l->y1;

		if (x1 < x0) x1 = x0;
		if (y1 < y0) y1 = y0;
		L->x0 = x0;
		L->y0 = y0;
		L->x1 = x1;
		L->y1 = y1;
		if (l->opacity < 1) {
			/* what is under, kept */
			if (!grow(&L->save, &L->save_cap, (size_t) (x1 - x0) * (y1 - y0)))
				return false;
			for (int y = y0; y < y1; y++)
				memcpy(L->save + (size_t) (y - y0) * (x1 - x0), surf_at(&d, x0, y),
						sizeof(uint32_t) * (x1 - x0));
		}
		if (l->nbackdrop > 0)
			backdrop(L, l, &d);
		depth++;
		return true;
	}

	if (pass == 0) {
		int w = l->x1 - l->x0, h = l->y1 - l->y0;
		enum nsfb_format_e fmt = d.bgr ? NSFB_FMT_XBGR8888 : NSFB_FMT_XRGB8888;

		if (w <= 0 || h <= 0)
			return false;
		L->pass[0] = pass_surface(L->pass[0], w, h, fmt);
		if (!l->single)
			L->pass[1] = pass_surface(L->pass[1], w, h, fmt);
		if (L->pass[0] == NULL || (!l->single && L->pass[1] == NULL))
			return false;
		depth++;
	}
	{
		nsfb_t *s = L->pass[pass];
		struct surf p;
		nsfb_bbox_t all;

		if (!surf_get(s, &p))
			return false;
		for (int y = 0; y < p.h; y++)
			memset(p.base + (size_t) y * p.stride, pass == 0 ? 0x00 : 0xff,
					(size_t) p.w * 4);
		all.x0 = all.y0 = 0;
		all.x1 = p.w;
		all.y1 = p.h;
		nsfb_plot_set_clip(s, &all);
		framebuffer_set_surface(s);
	}
	return true;
}

/* exported function documented in framebuffer/onyx_paint.h */
bool onyx_fb_layer_end(nsfb_t *nsfb, const struct onyx_layer *l, int pass)
{
	struct fb_layer *L;
	struct surf d, k, wt;
	int w, h;

	if (depth <= 0)
		return false;
	L = &levels[depth - 1];

	if (!l->isolated) {
		/* blended back with what was under, at 1 - opacity */
		unsigned op = (unsigned) (l->opacity * 256 + 0.5f);
		depth--;
		if (l->opacity >= 1 || !surf_get(nsfb, &d))
			return true;
		for (int y = L->y0; y < L->y1; y++) {
			const uint32_t *s = L->save + (size_t) (y - L->y0) * (L->x1 - L->x0);
			uint32_t *p = surf_at(&d, L->x0, y);
			for (int x = L->x0; x < L->x1; x++, s++, p++) {
				uint32_t now = *p, was = *s;
				if (((now ^ was) & 0xffffff) == 0)
					continue;
				*p = (now & 0xff000000) | (lerp_px(was, now, op) & 0xffffff);
			}
		}
		return true;
	}

	framebuffer_set_surface(L->under);
	if (pass == 0 && !l->single)
		return true;
	depth--;

	w = l->x1 - l->x0;
	h = l->y1 - l->y0;
	if (!surf_get(L->pass[0], &k) || !surf_get(L->under, &d) ||
	    !grow(&L->argb, &L->argb_cap, (size_t) w * h))
		return false;
	if (l->single) {
		/* one pass: the coverage is the opaque box's */
		const struct onyx_rrect *r = &l->opaque;
		for (int y = 0; y < h; y++) {
			const uint32_t *pk = surf_at(&k, 0, y);
			uint32_t *o = L->argb + (size_t) y * w;
			bool inside_y = y >= r->y0 + r->ry[ONYX_TL] && y >= r->y0 + r->ry[ONYX_TR] &&
				y + 1 <= r->y1 - r->ry[ONYX_BL] && y + 1 <= r->y1 - r->ry[ONYX_BR];
			for (int x = 0; x < w; x++) {
				uint32_t bk = px_rgb(k.bgr, pk[x]);
				unsigned a, kr, kg, kb;
				if (inside_y && x >= r->x0 && x + 1 <= r->x1) {
					o[x] = 0xff000000 | bk;
					continue;
				}
				a = (unsigned) (onyx_fb_rrect_cov(r, x + 0.5f, y + 0.5f) * 255 + 0.5f);
				if (a == 0) {
					o[x] = 0;
					continue;
				}
				kr = (bk >> 16) & 0xff;
				kg = (bk >> 8) & 0xff;
				kb = bk & 0xff;
				if (kr > a) kr = a;
				if (kg > a) kg = a;
				if (kb > a) kb = a;
				o[x] = (a << 24) | (kr << 16) | (kg << 8) | kb;
			}
		}
		goto filtered;
	}
	/* the coverage from the two passes */
	if (!surf_get(L->pass[1], &wt))
		return false;
	for (int y = 0; y < h; y++) {
		const uint32_t *pk = surf_at(&k, 0, y), *pw = surf_at(&wt, 0, y);
		uint32_t *o = L->argb + (size_t) y * w;
		for (int x = 0; x < w; x++) {
			uint32_t bk = px_rgb(k.bgr, pk[x]), wh = px_rgb(wt.bgr, pw[x]);
			int kr, kg, kb, a;

			if (bk == 0 && wh == 0xffffff) {
				o[x] = 0;
				continue;
			}
			kr = (bk >> 16) & 0xff;
			kg = (bk >> 8) & 0xff;
			kb = bk & 0xff;
			a = 255 - ((int) ((wh >> 16) & 0xff) - kr + (int) ((wh >> 8) & 0xff) - kg +
					(int) (wh & 0xff) - kb + 1) / 3;
			if (a <= 0) {
				o[x] = 0;
				continue;
			}
			if (a > 255)
				a = 255;
			if (kr > a) kr = a;
			if (kg > a) kg = a;
			if (kb > a) kb = a;
			o[x] = ((uint32_t) a << 24) | ((uint32_t) kr << 16) | ((uint32_t) kg << 8) |
				(uint32_t) kb;
		}
	}
filtered:
	if (l->nfilter > 0)
		apply_filters(L, L->argb, w, h, l->filter, l->nfilter, false);
	if (l->nbackdrop > 0 && !l->transformed)
		backdrop(L, l, &d);
	{
		int cx0 = l->cx0 > d.clip.x0 ? l->cx0 : d.clip.x0;
		int cy0 = l->cy0 > d.clip.y0 ? l->cy0 : d.clip.y0;
		int cx1 = l->cx1 < d.clip.x1 ? l->cx1 : d.clip.x1;
		int cy1 = l->cy1 < d.clip.y1 ? l->cy1 : d.clip.y1;
		if (cx0 < cx1 && cy0 < cy1)
			composite(l, L->argb, &d, cx0, cy0, cx1, cy1);
	}
	return true;
}
