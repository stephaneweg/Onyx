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
 * Onyx: a box's mask-image, for the HTML redraw (html/onyx_mask.c).
 */

#ifndef NETSURF_HTML_ONYX_MASK_H
#define NETSURF_HTML_ONYX_MASK_H

#include <stdbool.h>

struct box;
struct rect;
struct redraw_context;
struct html_content;

/**
 * Whether a box has a mask-image: its background and its object are not painted, the
 * mask (onyx_mask_redraw) is -- nothing while the image is loading or if it failed.
 */
bool onyx_mask_set(const struct box *box);

/**
 * Paint a box's mask: its background colour through its mask-image's alpha.
 *
 * \param html   the document
 * \param box    the box (box->mask ready, else nothing painted)
 * \param x, y   its border box's top left (device px)
 * \param width, height  its border box's size (device px)
 * \param scale  the redraw's scale
 * \param clip   the redraw's clip (set again on return)
 * \param ctx    the redraw's context
 * \return false on a plot failure
 */
bool onyx_mask_redraw(const struct html_content *html, struct box *box, int x, int y,
		int width, int height, float scale, const struct rect *clip,
		const struct redraw_context *ctx);

#endif
