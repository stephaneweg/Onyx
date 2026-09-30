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
 * Onyx: CSS :hover without building the boxes again (html/onyx_hover.c).
 */

#ifndef NETSURF_HTML_ONYX_HOVER_H
#define NETSURF_HTML_ONYX_HOVER_H

#include <stdbool.h>

struct html_content;
struct dom_node;

/**
 * Selection's note (css/select.h nscss_hover_note): a :hover selector was tried on `tested`
 * while `styled`'s style was selected. ctx is the html_content.
 */
void onyx_hover_note(void *ctx, struct dom_node *tested, struct dom_node *styled);

/** The notes forgotten: the document's styles are all selected again (a rebox). */
void onyx_hover_reset(struct html_content *c);

/** The style results hovers replaced freed (the box tree that may see them is gone). */
void onyx_hover_release(struct html_content *c);

/** The notes' memory freed (the document destroyed). */
void onyx_hover_fini(struct html_content *c);

/**
 * The node under the pointer changed from old_node to c->hover_node (either may be NULL):
 * the elements whose :hover state changed styled again, their boxes given the new styles
 * and redrawn -- when that changes only how they are painted. False: the boxes must be
 * built again and laid out (a layout property changed, or a case this does not handle).
 */
bool onyx_hover_restyle(struct html_content *c, struct dom_node *old_node);

/**
 * Onyx: an animation's frame (html/onyx_anim.c) -- each element's boxes given its style
 * (its pseudo-elements' styles kept) without a selection; deep: its subtree styled again
 * too (an inherited property changed: the selection gives the element its animated
 * style). When the changes are only in how boxes are painted (or translated), they are
 * redrawn; layout: any change is taken, nothing redrawn -- the caller lays the boxes out
 * again. False: nothing done (a layout property without layout, a pseudo-element appears,
 * an image to fetch, an element without a box, a case this does not handle).
 */
struct css_computed_style;
struct onyx_restyle_item {
	struct dom_node *node;
	const struct css_computed_style *style;
	bool deep;
};
bool onyx_hover_restyle_elements(struct html_content *c,
		const struct onyx_restyle_item *items, int n, bool layout);

/**
 * The style results replaced (by hovers, by animations' frames) that no box points at any
 * more freed; the others kept until the next rebox.
 */
void onyx_hover_collect(struct html_content *c);

#endif
