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
 * Onyx: CSS3's rounded, graded and shadowed boxes for the HTML redraw (onyx_paint.c).
 */

#ifndef NETSURF_HTML_ONYX_PAINT_H
#define NETSURF_HTML_ONYX_PAINT_H

#include <stdbool.h>
#include <libcss/libcss.h>

#include "netsurf/types.h"
#include "netsurf/onyx_paint.h"

/** A box-shadow (the first of the list), in px. */
struct onyx_box_shadow {
	float x, y, blur, spread;
	colour colour;
	bool inset;
};

/**
 * A box's corner radii: r's rectangle (its border box) set by the caller; its radii set
 * (reduced as CSS says when they overlap). False when no corner is round.
 */
bool onyx_box_radii(const css_computed_style *style, const css_unit_ctx *unit_len_ctx,
		float scale, struct onyx_rrect *r);

/** A rounded box less its sides (l, t, rt, b): the inner edge of a border. */
void onyx_rrect_inset(const struct onyx_rrect *o, float l, float t, float rt, float b,
		struct onyx_rrect *i);

/** A rounded box grown by d (a shadow's spread; d < 0 shrinks it). */
void onyx_rrect_outset(const struct onyx_rrect *o, float d, struct onyx_rrect *r);

/** The style's box-shadow, if any (and not transparent). */
bool onyx_box_shadow(const css_computed_style *style, const css_unit_ctx *unit_len_ctx,
		float scale, struct onyx_box_shadow *sh);

/** The style's background gradient (its spec, past "onyx-gradient:"), or NULL. */
const char *onyx_background_gradient(const css_computed_style *style);

/** Whether a background image's URL is a gradient (not to fetch). */
bool onyx_is_gradient_url(const char *url);

/**
 * A gradient spec resolved for its box (bx, by, bw, bh, plotter px).
 * False for a bad spec.
 */
bool onyx_gradient_resolve(const char *spec, const css_computed_style *style,
		const css_unit_ctx *unit_len_ctx, float scale,
		float bx, float by, float bw, float bh, struct onyx_gradient *g);

/**
 * The box's translation (transform's translate() / matrix(), the translate property), px,
 * its percentages of the box's border box (w x h). False when it has none.
 */
bool onyx_box_translate(const css_computed_style *style, const css_unit_ctx *unit_len_ctx,
		float w, float h, float *tx, float *ty);

#endif
