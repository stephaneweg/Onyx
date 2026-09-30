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
 * Onyx: a box's compositing effects -- opacity, transform (beyond a translation, which the
 * layout applies: onyx_box_translate), filter, backdrop-filter, mix-blend-mode -- resolved
 * for the redraw (redraw.c's groups), the layout's bounds, the hit test and the scripts'
 * rectangles (onyx_fx.c).
 *
 * Matrices are [a b c d e f]: x' = a x + c y + e, y' = b x + d y + f. A box's own matrix is
 * box-local: its origin the box's (x, y) (its padding box's top-left), in px.
 */

#ifndef NETSURF_HTML_ONYX_FX_H
#define NETSURF_HTML_ONYX_FX_H

#include <stdbool.h>
#include <libcss/libcss.h>

#include "netsurf/onyx_paint.h"

struct box;
struct html_content;

/** A box's effects, resolved. */
struct onyx_fx {
	float opacity;			/* 0 .. 1 */
	bool matrix;			/* a transform beyond a translation */
	float m[6];			/* (box-local) */
	int nfilter;
	struct onyx_filter filter[ONYX_FILTER_MAX];
	int nbackdrop;
	struct onyx_filter backdrop[ONYX_FILTER_MAX];
	enum onyx_blend blend;
};

/**
 * Whether the style makes its box a group -- a stacking context painted as a whole: opacity
 * below 1, a transform (a translation too), a filter, a backdrop-filter, a blend mode.
 */
bool onyx_fx_style(const css_computed_style *style);

/** Whether the box is an element's own box, which takes the effects (not a text, an
 * inline's pieces, a marker's style). */
bool onyx_fx_box(const struct box *box);

/**
 * The box's effects (scale: the redraw's). False when there is nothing to do: opacity 1, no
 * transform beyond a translation, no filter, no backdrop-filter, no blend mode.
 */
bool onyx_box_fx(const struct html_content *html, const struct box *box, float scale,
		struct onyx_fx *fx);

/**
 * The box's transform when it is more than a translation (box-local, scale applied), with
 * its transform-origin. False when it has none (or a translation only).
 */
bool onyx_box_matrix(const css_computed_style *style, const css_unit_ctx *unit_len_ctx,
		const struct box *box, float scale, float m[6]);

/** How far filters paint past what they filter: out[0..3] left, top, right, bottom, px. */
void onyx_filter_outset(const struct onyx_filter *f, int n, float out[4]);

/** The box's filter reach (its style's filter), px (0 when none). */
void onyx_box_filter_outset(const css_computed_style *style,
		const css_unit_ctx *unit_len_ctx, float out[4]);

/** r = p . q (q applied first). */
void onyx_matrix_mul(const float p[6], const float q[6], float r[6]);

/** The inverse; false when m is singular. */
bool onyx_matrix_invert(const float m[6], float inv[6]);

/** The bounding box of rectangle (x0, y0)-(x1, y1) mapped through m. */
void onyx_matrix_bbox(const float m[6], float x0, float y0, float x1, float y1,
		float *bx0, float *by0, float *bx1, float *by1);

/**
 * A box's painted bounds as its parent sees them (relative to the parent's origin): its
 * border box and descendants' bounds (as the layout has them, unless its overflow is
 * clipped), grown by its filters' reach, mapped through its transform.
 */
void onyx_fx_child_bounds(const css_unit_ctx *unit_len_ctx, const struct box *box,
		int *x0, int *y0, int *x1, int *y1);

/**
 * A rectangle of the box (page px, as box_coords places the box: r = x0, y0, x1, y1) where it
 * is painted: through the box's own transform (self) and its ancestors', grown by their
 * filters' reach (filters). False when nothing on the way changes it.
 */
bool onyx_fx_page_rect(const struct html_content *html, const struct box *box, bool self,
		bool filters, float r[4]);

/** Whether the effects of two styles differ (a restyle: the box and all it holds redrawn). */
bool onyx_fx_style_differs(const css_computed_style *a, const css_computed_style *b);

#endif
