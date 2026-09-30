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
 * Onyx: what the core asks a plotter for CSS3's rounded, graded and shadowed boxes -- the
 * plotter_table's onyx_shape, onyx_round_clip and onyx_text_paint (a plotter without them
 * leaves them NULL: the core then draws plain rectangles).
 *
 * All coordinates are in the plotter's pixels (as the other plot operations'), floats so a
 * shape's anti-aliased edge can fall between pixels.
 */

#ifndef NETSURF_ONYX_PAINT_H
#define NETSURF_ONYX_PAINT_H

#include <stdbool.h>
#include <stdint.h>

#include "netsurf/types.h"

#define ONYX_GRAD_MAX_STOPS 16

/** Corners, in the order of CSS's border-radius. */
enum onyx_corner { ONYX_TL = 0, ONYX_TR = 1, ONYX_BR = 2, ONYX_BL = 3 };

/**
 * A rounded rectangle: (x0, y0) to (x1, y1) (x1, y1 exclusive) and its corners' radii
 * (elliptical: rx across, ry down), already reduced so adjacent corners do not overlap.
 */
struct onyx_rrect {
	float x0, y0, x1, y1;
	float rx[4], ry[4];
};

enum onyx_grad_kind {
	ONYX_GRAD_LINEAR,
	ONYX_GRAD_RADIAL,
	ONYX_GRAD_CONIC
};

/**
 * A gradient resolved for a box: its colour at a point p (plotter px) is its stops'
 * colour at t --
 *   linear  t = ((p - p0) . d) / (d . d)
 *   radial  t = |((p - c).x / rx, (p - c).y / ry)|
 *   conic   t = the angle of (p - c), clockwise from up, less `from`, in turns
 * A repeating gradient repeats its stops' span; else t is clamped to its first / last.
 */
struct onyx_gradient {
	enum onyx_grad_kind kind;
	bool repeating;
	float x0, y0, dx, dy;		/* linear */
	float cx, cy, rx, ry;		/* radial, conic (cx, cy) */
	float from;			/* conic, in turns */
	int nstops;
	float pos[ONYX_GRAD_MAX_STOPS];		/* increasing */
	uint32_t argb[ONYX_GRAD_MAX_STOPS];	/* 0xAARRGGBB (css_color) */
};

/** What a shape (or text) is painted with: a gradient, else a colour. */
struct onyx_paint {
	const struct onyx_gradient *gradient;
	colour colour;			/* NetSurf's 0xTTBBGGRR (TT: transparency) */
};

/**
 * A shape: a rounded box filled; or its ring, between it and an inner rounded box (a
 * border, each side in its own colour if per_side); or its shadow, blurred over `blur` px
 * (a Gaussian's 2 sigma) -- not where `hole` is, when set (the box casting it).
 */
struct onyx_shape {
	struct onyx_rrect outer;
	bool ring;
	struct onyx_rrect inner;
	bool per_side;
	colour side_colour[4];		/* TOP, RIGHT, BOTTOM, LEFT (box_side) */
	float blur;
	bool hole;
	struct onyx_rrect hole_rect;
	struct onyx_paint paint;
};

/* ---- compositing layers (CSS opacity, transform, filter, backdrop-filter, mix-blend-mode) ----
 * The core paints a box and what it holds (its stacking context) as a group: the plotter's
 * onyx_layer_begin / onyx_layer_end around it. A group is either painted in place, over a
 * copy of what is under it, then blended with it (opacity alone: exact, one pass), or -- when
 * it is transformed, filtered or blended -- painted apart into a layer of its own, twice
 * (over black, then over white: the difference gives each pixel's coverage), filtered, then
 * drawn through its matrix onto what is under it.
 */

#define ONYX_FILTER_MAX 8

enum onyx_filter_op {
	ONYX_FILTER_BLUR,		/* v: the standard deviation, px */
	ONYX_FILTER_BRIGHTNESS,		/* v: the amounts (1: unchanged) */
	ONYX_FILTER_CONTRAST,
	ONYX_FILTER_GRAYSCALE,
	ONYX_FILTER_INVERT,
	ONYX_FILTER_OPACITY,
	ONYX_FILTER_SATURATE,
	ONYX_FILTER_SEPIA,
	ONYX_FILTER_HUE_ROTATE,		/* v: degrees */
	ONYX_FILTER_DROP_SHADOW		/* dx, dy, v the blur radius (px), argb */
};

struct onyx_filter {
	enum onyx_filter_op op;
	float v;
	float dx, dy;
	uint32_t argb;			/* 0xAARRGGBB */
};

enum onyx_blend {
	ONYX_BLEND_NORMAL = 0,
	ONYX_BLEND_MULTIPLY,
	ONYX_BLEND_SCREEN,
	ONYX_BLEND_OVERLAY,
	ONYX_BLEND_DARKEN,
	ONYX_BLEND_LIGHTEN,
	ONYX_BLEND_COLOR_DODGE,
	ONYX_BLEND_COLOR_BURN,
	ONYX_BLEND_HARD_LIGHT,
	ONYX_BLEND_SOFT_LIGHT,
	ONYX_BLEND_DIFFERENCE,
	ONYX_BLEND_EXCLUSION,
	ONYX_BLEND_PLUS_LIGHTER,
	ONYX_BLEND_PLUS_DARKER
};

/**
 * A group. Its layer covers (x0, y0) to (x1, y1) (exclusive, plotter px): isolated, the
 * core paints the group into it with its origin moved to (x0, y0) -- pass 0, then pass 1 --
 * and the result is drawn within the clip (cx0, cy0)-(cx1, cy1), through m if transformed
 * (layer px, as plotter px, to plotter px: x' = m[0] x + m[2] y + m[4], y' = m[1] x +
 * m[3] y + m[5]), filtered, at the opacity, blended; in place (not isolated: pass 0 only),
 * the core paints the group where it is and (x0, y0)-(x1, y1) is blended back at the opacity
 * with what was there. backdrop: filters applied to what is under the rounded box
 * backdrop_box before the group is painted (backdrop-filter).
 */
struct onyx_layer {
	int x0, y0, x1, y1;
	int cx0, cy0, cx1, cy1;
	bool isolated;
	float opacity;
	bool transformed;
	float m[6];
	int nfilter;
	struct onyx_filter filter[ONYX_FILTER_MAX];
	enum onyx_blend blend;
	int nbackdrop;
	struct onyx_filter backdrop[ONYX_FILTER_MAX];
	struct onyx_rrect backdrop_box;
	/* isolated, one pass only: the group is known opaque inside the rounded box `opaque`
	 * (layer px) and clear outside it -- its coverage is the box's, the black pass alone
	 * gives the colours */
	bool single;
	struct onyx_rrect opaque;
	/* Onyx -- GPU compositing (docs/06 §22): a retained layer. The core offers a group the
	 * frontend can composite itself (an opacity and / or a transform: no filter, no blend
	 * mode, no backdrop) with onyx_layer_begin (ctx, l, ONYX_LAYER_OFFER): x0..y1 its whole
	 * rectangle (untransformed), m its matrix, lm the same about its box's origin (ox, oy),
	 * cx0..cy1 its clip, the opacity, key its box, tree the box tree. NSERROR_OK: the plotter
	 * keeps the group's pixels between redraws and composites them over what is painted
	 * under it (nothing of it is painted in place); it sets rx0..ry1, the part of its pixels
	 * to paint again (target px; empty: they are still good), which the core paints through
	 * the passes of an isolated group with retain set (the plotter keeps the passes' result
	 * instead of drawing it); then onyx_layer_end (ctx, l, ONYX_LAYER_OFFER). Any other
	 * answer: the group is painted as ever. */
	bool retain;
	const void *key;
	const void *tree;
	float lm[6];
	float ox, oy;
	int rx0, ry0, rx1, ry1;
};

#define ONYX_LAYER_OFFER (-1)	/* (onyx_layer_begin / _end's pass: a retained layer) */

/**
 * Onyx -- GPU compositing: set by a frontend that retains layers (framebuffer/onyx_comp.c).
 * A retained layer's matrix about its origin (lm, as onyx_layer's; NULL: none) or its opacity
 * changed, nothing else: composited again without painting -> true; false: the caller redraws
 * its rectangles as ever (html_redraw_layer_update, html/private.h).
 */
extern bool (*onyx_layer_props)(const void *key, const float *lm, float opacity);

#endif
