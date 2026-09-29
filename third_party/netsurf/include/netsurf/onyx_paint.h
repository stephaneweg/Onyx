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

#endif
