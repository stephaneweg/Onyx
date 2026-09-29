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
 * Onyx: the framebuffer's CSS3 painting (netsurf/onyx_paint.h) -- rounded boxes, rings
 * (borders), blurred shadows, gradients, rounded clips and gradient text -- drawn straight
 * into the surface's 32 bpp pixels, anti-aliased from each pixel's signed distance to the
 * shape's edge.
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

#include "framebuffer/onyx_paint.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- the surface's pixels ------------------------------------------------------------- */

struct fbpix {
	uint8_t *base;
	int stride;		/* bytes a row */
	int w, h;
	bool bgr;		/* 0xAABBGGRR pixels (else 0xAARRGGBB / 0x00RRGGBB) */
	nsfb_bbox_t clip;	/* the plot clip, within the surface */
};

static bool fbpix_get(nsfb_t *nsfb, struct fbpix *f)
{
	enum nsfb_format_e fmt;

	if (nsfb_get_geometry(nsfb, &f->w, &f->h, &fmt) != 0)
		return false;
	if (nsfb_get_buffer(nsfb, &f->base, &f->stride) != 0 || f->base == NULL)
		return false;
	switch (fmt) {
	case NSFB_FMT_XRGB8888:
	case NSFB_FMT_ARGB8888:
		f->bgr = false;
		break;
	case NSFB_FMT_XBGR8888:
	case NSFB_FMT_ABGR8888:
		f->bgr = true;
		break;
	default:
		return false;
	}
	if (!nsfb_plot_get_clip(nsfb, &f->clip)) {
		f->clip.x0 = f->clip.y0 = 0;
		f->clip.x1 = f->w;
		f->clip.y1 = f->h;
	}
	if (f->clip.x0 < 0) f->clip.x0 = 0;
	if (f->clip.y0 < 0) f->clip.y0 = 0;
	if (f->clip.x1 > f->w) f->clip.x1 = f->w;
	if (f->clip.y1 > f->h) f->clip.y1 = f->h;
	return true;
}

static inline uint32_t *fbpix_at(const struct fbpix *f, int x, int y)
{
	return (uint32_t *) (f->base + (size_t) y * f->stride) + x;
}

/** Blend (r, g, b) at opacity a (0..255) over the pixel. */
static inline void fbpix_blend(const struct fbpix *f, uint32_t *p,
		unsigned r, unsigned g, unsigned b, unsigned a)
{
	uint32_t d = *p;
	unsigned dr, dg, db;

	if (a == 0)
		return;
	if (f->bgr) {
		dr = d & 0xff;
		dg = (d >> 8) & 0xff;
		db = (d >> 16) & 0xff;
	} else {
		dr = (d >> 16) & 0xff;
		dg = (d >> 8) & 0xff;
		db = d & 0xff;
	}
	if (a >= 255) {
		dr = r;
		dg = g;
		db = b;
	} else {
		dr = (r * a + dr * (255 - a) + 127) / 255;
		dg = (g * a + dg * (255 - a) + 127) / 255;
		db = (b * a + db * (255 - a) + 127) / 255;
	}
	if (f->bgr)
		*p = (d & 0xff000000) | (db << 16) | (dg << 8) | dr;
	else
		*p = (d & 0xff000000) | (dr << 16) | (dg << 8) | db;
}

/* ---- colours ------------------------------------------------------------------------- */

/** NetSurf's colour (0xTTBBGGRR, TT the transparency) as 0xAARRGGBB. */
static inline uint32_t ns_to_argb(colour c)
{
	uint32_t a = 0xff - ((c >> 24) & 0xff);
	return (a << 24) | ((c & 0xff) << 16) | (c & 0xff00) | ((c >> 16) & 0xff);
}

/** The gradient's colour at t (0xAARRGGBB), interpolated premultiplied. */
static uint32_t grad_at(const struct onyx_gradient *g, float t)
{
	int n = g->nstops, i;
	float f;
	uint32_t c0, c1;
	float a0, a1, a, r, gg, b;

	if (n <= 0)
		return 0;
	if (g->repeating && n > 1) {
		float span = g->pos[n - 1] - g->pos[0];
		if (span > 1e-6f) {
			t = fmodf(t - g->pos[0], span);
			if (t < 0)
				t += span;
			t += g->pos[0];
		}
	}
	if (t <= g->pos[0] || n == 1)
		return g->argb[0];
	if (t >= g->pos[n - 1])
		return g->argb[n - 1];
	for (i = 0; i < n - 2 && t > g->pos[i + 1]; i++)
		;
	if (g->pos[i + 1] - g->pos[i] <= 1e-6f)
		return g->argb[i + 1];
	f = (t - g->pos[i]) / (g->pos[i + 1] - g->pos[i]);
	c0 = g->argb[i];
	c1 = g->argb[i + 1];
	a0 = (c0 >> 24) / 255.0f;
	a1 = (c1 >> 24) / 255.0f;
	a = a0 + (a1 - a0) * f;
	if (a <= 0.0001f)
		return 0;
	r = (((c0 >> 16) & 0xff) * a0 * (1 - f) + ((c1 >> 16) & 0xff) * a1 * f) / a;
	gg = (((c0 >> 8) & 0xff) * a0 * (1 - f) + ((c1 >> 8) & 0xff) * a1 * f) / a;
	b = ((c0 & 0xff) * a0 * (1 - f) + (c1 & 0xff) * a1 * f) / a;
	return ((uint32_t) (a * 255 + 0.5f) << 24) | ((uint32_t) (r + 0.5f) << 16) |
		((uint32_t) (gg + 0.5f) << 8) | (uint32_t) (b + 0.5f);
}

/** The gradient's colour at the pixel centre (px, py). */
static uint32_t grad_colour(const struct onyx_gradient *g, float px, float py)
{
	float t = 0;

	switch (g->kind) {
	case ONYX_GRAD_LINEAR: {
		float dd = g->dx * g->dx + g->dy * g->dy;
		if (dd > 0)
			t = ((px - g->x0) * g->dx + (py - g->y0) * g->dy) / dd;
		break;
	}
	case ONYX_GRAD_RADIAL: {
		float nx = g->rx > 0 ? (px - g->cx) / g->rx : 0;
		float ny = g->ry > 0 ? (py - g->cy) / g->ry : 0;
		t = sqrtf(nx * nx + ny * ny);
		break;
	}
	case ONYX_GRAD_CONIC: {
		float a = atan2f(px - g->cx, -(py - g->cy));	/* 0 up, clockwise */
		t = a / (2 * (float) M_PI);
		if (t < 0)
			t += 1;
		t -= g->from;
		t -= floorf(t);
		break;
	}
	}
	return grad_at(g, t);
}

/* ---- shapes ---------------------------------------------------------------------------- */

/** The signed distance from (px, py) to the rounded box's edge (< 0 inside). */
static float rrect_dist(const struct onyx_rrect *r, float px, float py)
{
	float cx = 0, cy = 0, rx = 0, ry = 0, dx, dy;
	bool corner = false;

	if (px < r->x0 + r->rx[ONYX_TL] && py < r->y0 + r->ry[ONYX_TL]) {
		rx = r->rx[ONYX_TL]; ry = r->ry[ONYX_TL];
		cx = r->x0 + rx; cy = r->y0 + ry;
		corner = true;
	} else if (px > r->x1 - r->rx[ONYX_TR] && py < r->y0 + r->ry[ONYX_TR]) {
		rx = r->rx[ONYX_TR]; ry = r->ry[ONYX_TR];
		cx = r->x1 - rx; cy = r->y0 + ry;
		corner = true;
	} else if (px > r->x1 - r->rx[ONYX_BR] && py > r->y1 - r->ry[ONYX_BR]) {
		rx = r->rx[ONYX_BR]; ry = r->ry[ONYX_BR];
		cx = r->x1 - rx; cy = r->y1 - ry;
		corner = true;
	} else if (px < r->x0 + r->rx[ONYX_BL] && py > r->y1 - r->ry[ONYX_BL]) {
		rx = r->rx[ONYX_BL]; ry = r->ry[ONYX_BL];
		cx = r->x0 + rx; cy = r->y1 - ry;
		corner = true;
	}
	if (corner && rx > 0.01f && ry > 0.01f) {
		float nx = (px - cx) / rx, ny = (py - cy) / ry;
		float k = sqrtf(nx * nx + ny * ny);
		float gx = nx / rx, gy = ny / ry;
		float gl = sqrtf(gx * gx + gy * gy);
		if (k < 1e-4f || gl < 1e-9f)
			return -(rx < ry ? rx : ry);
		/* (the ellipse's implicit function over its gradient: exact for a circle) */
		return (k - 1) * k / gl;
	}
	dx = r->x0 - px;
	if (px - r->x1 > dx) dx = px - r->x1;
	dy = r->y0 - py;
	if (py - r->y1 > dy) dy = py - r->y1;
	if (dx > 0 && dy > 0)
		return sqrtf(dx * dx + dy * dy);
	return dx > dy ? dx : dy;
}

/** A pixel's coverage by a shape whose edge is at distance d from its centre. */
static inline float aa_cov(float d)
{
	float c = 0.5f - d;
	return c <= 0 ? 0 : c >= 1 ? 1 : c;
}

/** erfc(x) (Numerical Recipes' Chebyshev fit, |error| < 1.2e-7). */
static float erfc_f(float x)
{
	float z = fabsf(x), t = 1.0f / (1.0f + 0.5f * z), r;

	r = t * expf(-z * z - 1.26551223f + t * (1.00002368f + t * (0.37409196f +
		t * (0.09678418f + t * (-0.18628806f + t * (0.27886807f +
		t * (-1.13520398f + t * (1.48851587f + t * (-0.82215223f +
		t * 0.17087277f)))))))));
	return x >= 0 ? r : 2 - r;
}

/** A blurred edge's coverage (a Gaussian of sigma blur / 2 across it). */
static inline float blur_cov(float d, float blur)
{
	float sigma = blur * 0.5f;

	if (sigma < 0.5f)
		return aa_cov(d);
	if (d < -3 * sigma)
		return 1;
	if (d > 3 * sigma)
		return 0;
	return 0.5f * erfc_f(d / (sigma * 1.41421356f));
}

/** A ring pixel's side (TOP, RIGHT, BOTTOM, LEFT): the side it is least deep into. */
static int ring_side(const struct onyx_shape *s, float px, float py)
{
	const struct onyx_rrect *o = &s->outer, *i = &s->inner;
	float w[4] = { i->y0 - o->y0, o->x1 - i->x1, o->y1 - i->y1, i->x0 - o->x0 };
	float d[4] = { py - o->y0, o->x1 - px, o->y1 - py, px - o->x0 };
	int best = 0;
	float bv = 1e30f;

	for (int k = 0; k < 4; k++) {
		float v;
		if (w[k] <= 0.001f)
			continue;
		v = d[k] / w[k];
		if (v < bv) {
			bv = v;
			best = k;
		}
	}
	return best;
}

/* exported function documented in framebuffer/onyx_paint.h */
bool onyx_fb_shape(nsfb_t *nsfb, const struct onyx_shape *s)
{
	struct fbpix f;
	uint32_t solid = 0;
	uint32_t sides[4];
	float ext;
	int x0, y0, x1, y1;
	const struct onyx_rrect *o = &s->outer;

	if (!fbpix_get(nsfb, &f))
		return false;
	if (s->paint.gradient == NULL) {
		solid = ns_to_argb(s->paint.colour);
		if ((solid >> 24) == 0 && !s->per_side)
			return true;
	}
	if (s->per_side)
		for (int k = 0; k < 4; k++)
			sides[k] = ns_to_argb(s->side_colour[k]);

	ext = s->blur > 0 ? s->blur * 1.5f + 1 : 1;
	x0 = (int) floorf(o->x0 - ext);
	y0 = (int) floorf(o->y0 - ext);
	x1 = (int) ceilf(o->x1 + ext);
	y1 = (int) ceilf(o->y1 + ext);
	if (x0 < f.clip.x0) x0 = f.clip.x0;
	if (y0 < f.clip.y0) y0 = f.clip.y0;
	if (x1 > f.clip.x1) x1 = f.clip.x1;
	if (y1 > f.clip.y1) y1 = f.clip.y1;
	if (x0 >= x1 || y0 >= y1)
		return true;

	for (int y = y0; y < y1; y++) {
		float py = y + 0.5f;
		uint32_t *row = fbpix_at(&f, 0, y);
		/* a row of the box's straight part: its inside is fully covered, and for a
		 * ring the inner box's inside not at all */
		float top = o->y0 + (o->ry[ONYX_TL] > o->ry[ONYX_TR] ? o->ry[ONYX_TL] : o->ry[ONYX_TR]);
		float bot = o->y1 - (o->ry[ONYX_BL] > o->ry[ONYX_BR] ? o->ry[ONYX_BL] : o->ry[ONYX_BR]);
		int in0 = x1, in1 = x1;	/* [in0, in1): fully inside (fill) */
		int sk0 = x1, sk1 = x1;	/* [sk0, sk1): nothing (a ring's hole) */

		if (s->blur <= 0 && py - 0.5f >= o->y0 && py + 0.5f <= o->y1 &&
		    py >= top && py <= bot) {
			in0 = (int) ceilf(o->x0 + 0.5f);
			in1 = (int) floorf(o->x1 - 0.5f);
		}
		if (s->ring) {
			const struct onyx_rrect *i = &s->inner;
			float itop = i->y0 + (i->ry[ONYX_TL] > i->ry[ONYX_TR] ? i->ry[ONYX_TL] : i->ry[ONYX_TR]);
			float ibot = i->y1 - (i->ry[ONYX_BL] > i->ry[ONYX_BR] ? i->ry[ONYX_BL] : i->ry[ONYX_BR]);
			if (py - 0.5f >= i->y0 && py + 0.5f <= i->y1 && py >= itop && py <= ibot) {
				sk0 = (int) ceilf(i->x0 + 0.5f);
				sk1 = (int) floorf(i->x1 - 0.5f);
			}
		}

		for (int x = x0; x < x1; x++) {
			float px = x + 0.5f, cov;
			uint32_t c;
			unsigned a;

			if (x >= sk0 && x < sk1) {
				x = sk1 - 1;
				continue;
			}
			if (x >= in0 && x < in1 && !s->ring) {
				cov = 1;
			} else if (s->blur > 0) {
				cov = blur_cov(rrect_dist(o, px, py), s->blur);
				if (cov > 0 && s->hole)
					cov *= 1 - aa_cov(rrect_dist(&s->hole_rect, px, py));
			} else {
				cov = aa_cov(rrect_dist(o, px, py));
				if (s->ring && cov > 0)
					cov -= aa_cov(rrect_dist(&s->inner, px, py));
			}
			if (cov <= 0.004f)
				continue;
			if (s->per_side)
				c = sides[ring_side(s, px, py)];
			else if (s->paint.gradient != NULL)
				c = grad_colour(s->paint.gradient, px, py);
			else
				c = solid;
			a = (unsigned) ((c >> 24) * cov + 0.5f);
			fbpix_blend(&f, row + x, (c >> 16) & 0xff, (c >> 8) & 0xff,
					c & 0xff, a);
		}
	}
	return true;
}

/* ---- rounded clips ----------------------------------------------------------------------- */

#define RCLIP_DEPTH 16

struct rclip {
	struct onyx_rrect r;
	bool bgr;
	struct {
		int x0, y0, w, h;
		uint32_t *px;
	} c[4];
};

static struct rclip rclip_stack[RCLIP_DEPTH];
static int rclip_depth;		/* (may pass RCLIP_DEPTH: those are not kept) */

/* exported function documented in framebuffer/onyx_paint.h */
bool onyx_fb_round_clip(nsfb_t *nsfb, const struct onyx_rrect *r)
{
	struct fbpix f;

	if (!fbpix_get(nsfb, &f))
		return false;

	if (r != NULL) {
		/* push: keep the pixels of the corners' squares */
		struct rclip *rc;

		if (rclip_depth >= RCLIP_DEPTH) {
			rclip_depth++;
			return true;
		}
		rc = &rclip_stack[rclip_depth++];
		rc->r = *r;
		rc->bgr = f.bgr;
		for (int k = 0; k < 4; k++) {
			float rx = r->rx[k], ry = r->ry[k];
			int cx0, cy0, cx1, cy1;

			rc->c[k].px = NULL;
			if (rx < 0.5f || ry < 0.5f)
				continue;
			cx0 = (int) floorf(k == ONYX_TL || k == ONYX_BL ? r->x0 : r->x1 - rx);
			cx1 = (int) ceilf(k == ONYX_TL || k == ONYX_BL ? r->x0 + rx : r->x1);
			cy0 = (int) floorf(k == ONYX_TL || k == ONYX_TR ? r->y0 : r->y1 - ry);
			cy1 = (int) ceilf(k == ONYX_TL || k == ONYX_TR ? r->y0 + ry : r->y1);
			if (cx0 < f.clip.x0) cx0 = f.clip.x0;
			if (cy0 < f.clip.y0) cy0 = f.clip.y0;
			if (cx1 > f.clip.x1) cx1 = f.clip.x1;
			if (cy1 > f.clip.y1) cy1 = f.clip.y1;
			if (cx0 >= cx1 || cy0 >= cy1)
				continue;
			rc->c[k].px = malloc(sizeof(uint32_t) * (cx1 - cx0) * (cy1 - cy0));
			if (rc->c[k].px == NULL)
				continue;
			rc->c[k].x0 = cx0;
			rc->c[k].y0 = cy0;
			rc->c[k].w = cx1 - cx0;
			rc->c[k].h = cy1 - cy0;
			for (int y = 0; y < cy1 - cy0; y++)
				memcpy(rc->c[k].px + y * (cx1 - cx0),
						fbpix_at(&f, cx0, cy0 + y),
						sizeof(uint32_t) * (cx1 - cx0));
		}
		return true;
	}

	/* pop: outside the rounded box, back to what was there */
	if (rclip_depth <= 0)
		return true;
	if (rclip_depth-- > RCLIP_DEPTH)
		return true;
	{
		struct rclip *rc = &rclip_stack[rclip_depth];
		for (int k = 0; k < 4; k++) {
			if (rc->c[k].px == NULL)
				continue;
			for (int y = 0; y < rc->c[k].h; y++) {
				int yy = rc->c[k].y0 + y;
				if (yy < 0 || yy >= f.h)
					continue;
				for (int x = 0; x < rc->c[k].w; x++) {
					int xx = rc->c[k].x0 + x;
					uint32_t saved, *p;
					float cov;
					unsigned a;

					if (xx < 0 || xx >= f.w)
						continue;
					cov = aa_cov(rrect_dist(&rc->r, xx + 0.5f, yy + 0.5f));
					if (cov >= 0.999f)
						continue;
					p = fbpix_at(&f, xx, yy);
					saved = rc->c[k].px[y * rc->c[k].w + x];
					a = (unsigned) ((1 - cov) * 255 + 0.5f);
					/* (saved over what was drawn, by what is outside) */
					if (f.bgr)
						fbpix_blend(&f, p, saved & 0xff, (saved >> 8) & 0xff,
								(saved >> 16) & 0xff, a);
					else
						fbpix_blend(&f, p, (saved >> 16) & 0xff,
								(saved >> 8) & 0xff, saved & 0xff, a);
				}
			}
			free(rc->c[k].px);
			rc->c[k].px = NULL;
		}
	}
	return true;
}

/* ---- gradient text ---------------------------------------------------------------------- */

/* exported function documented in framebuffer/onyx_paint.h */
bool onyx_fb_glyph(nsfb_t *nsfb, const nsfb_bbox_t *loc, const uint8_t *pixels,
		int pitch, bool mono, const struct onyx_paint *paint)
{
	struct fbpix f;
	uint32_t solid = 0;

	if (!fbpix_get(nsfb, &f))
		return false;
	if (paint->gradient == NULL)
		solid = ns_to_argb(paint->colour);
	for (int y = loc->y0; y < loc->y1; y++) {
		const uint8_t *src;
		if (y < f.clip.y0 || y >= f.clip.y1)
			continue;
		src = pixels + (size_t) (y - loc->y0) * pitch;
		for (int x = loc->x0; x < loc->x1; x++) {
			int i = x - loc->x0;
			unsigned v, a;
			uint32_t c;

			if (x < f.clip.x0 || x >= f.clip.x1)
				continue;
			v = mono ? ((src[i >> 3] & (0x80 >> (i & 7))) ? 255 : 0) : src[i];
			if (v == 0)
				continue;
			c = paint->gradient ? grad_colour(paint->gradient, x + 0.5f, y + 0.5f)
					    : solid;
			a = ((c >> 24) * v + 127) / 255;
			fbpix_blend(&f, fbpix_at(&f, x, y), (c >> 16) & 0xff,
					(c >> 8) & 0xff, c & 0xff, a);
		}
	}
	return true;
}
