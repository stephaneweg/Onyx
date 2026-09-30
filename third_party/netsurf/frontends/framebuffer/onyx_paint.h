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
 * Onyx: the framebuffer's CSS3 painting (onyx_paint.c), for the plotters (framebuffer.c).
 */

#ifndef NETSURF_FB_ONYX_PAINT_H
#define NETSURF_FB_ONYX_PAINT_H

struct onyx_shape;
struct onyx_rrect;
struct onyx_paint;

/** Fill a shape (netsurf/onyx_paint.h) within the surface's plot clip. */
bool onyx_fb_shape(nsfb_t *nsfb, const struct onyx_shape *shape);

/** Push a rounded clip (r), or pop the last one (r NULL). */
bool onyx_fb_round_clip(nsfb_t *nsfb, const struct onyx_rrect *r);

/** A glyph's coverage (8 bpp, or 1 bpp when mono) painted at loc. */
bool onyx_fb_glyph(nsfb_t *nsfb, const nsfb_bbox_t *loc, const uint8_t *pixels,
		int pitch, bool mono, const struct onyx_paint *paint);

/** A pixel's coverage (0..1) by a rounded box, its centre at (px, py). */
float onyx_fb_rrect_cov(const struct onyx_rrect *r, float px, float py);

struct onyx_layer;

/** A compositing group's pass begins / ends (onyx_layer.c; netsurf/onyx_paint.h). */
bool onyx_fb_layer_begin(nsfb_t *nsfb, const struct onyx_layer *layer, int pass);
bool onyx_fb_layer_end(nsfb_t *nsfb, const struct onyx_layer *layer, int pass);

#endif
