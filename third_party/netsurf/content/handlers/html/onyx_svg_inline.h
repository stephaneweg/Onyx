/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 */

/**
 * \file
 * Onyx: inline <svg> in HTML documents (onyx_svg_inline.c).
 */

#ifndef NETSURF_HTML_ONYX_SVG_INLINE_H
#define NETSURF_HTML_ONYX_SVG_INLINE_H

#include <stdbool.h>

struct box;
struct html_content;
struct dom_node;

/**
 * An element's box, if the element is an SVG <svg>: a replaced box whose object is the
 * <svg> written out as an SVG image (its children not converted). Any other element:
 * nothing done.
 *
 * \return false on memory exhaustion
 */
bool onyx_svg_box(struct dom_node *n, struct html_content *content, struct box *box,
		bool *convert_children);

/** Whether a box's object is an inline <svg>'s (its colour is in its SVG text: a CSS
 * `color` change needs the boxes made again, not a restyle in place) */
bool onyx_svg_box_is_inline(const struct box *box);

#endif
