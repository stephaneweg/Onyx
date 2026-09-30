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
 * Onyx: an element's style selection kept from one box tree to the next
 * (html/onyx_restyle.c) -- a rebox selects again only the elements a DOM change can
 * have restyled.
 */

#ifndef NETSURF_HTML_ONYX_RESTYLE_H
#define NETSURF_HTML_ONYX_RESTYLE_H

#include <stdbool.h>
#include <dom/dom.h>
#include <libcss/libcss.h>

struct html_content;

/** A box tree is being built: its selections are numbered by a new serial */
void onyx_restyle_begin(struct html_content *c);

/** The counts of the last box tree's selections kept / made (NS_PERF) */
void onyx_restyle_end(struct html_content *c);

/**
 * The element's selection kept from an earlier box tree, when no change can have
 * altered it (a copy the caller owns: css_select_results_destroy), else NULL -- the
 * caller then selects and gives the result to onyx_restyle_store().
 */
css_select_results *onyx_restyle_lookup(struct html_content *c, dom_node *n,
		const css_computed_style *parent_style,
		const css_computed_style *root_style);

/**
 * An element's new selection (the cascade's, before transitions and animations) kept.
 * \a cacheable false: never reused (a shadow tree's...); kept with the :hover state of
 * the nodes it tried :hover on; \a structural: NSCSS_STRUCT_* bits (css/select.h), how
 * it depends on the tree's structure.
 */
void onyx_restyle_store(struct html_content *c, dom_node *n,
		const css_select_results *res,
		const css_computed_style *parent_style,
		const css_computed_style *root_style,
		bool cacheable, unsigned int structural);

/** A selection starts (box_get_style): onyx_restyle_hover_note, the selection's
 * nscss_hover_note, records the nodes it tries :hover on (and tells onyx_hover.c) */
void onyx_restyle_select_begin(void);
void onyx_restyle_hover_note(void *ctx, struct dom_node *tested, struct dom_node *styled);

/** Every element's selection made again at the next box tree (the sheets, media) */
void onyx_restyle_invalidate_all(struct html_content *c);

/** The selection context made again (html_css_restyle, before old_ctx goes): when
 * sheets were only added, a kept selection stays for the elements none of theirs
 * matches; anything else selects every element again */
void onyx_restyle_sheets_changed(struct html_content *c, css_select_ctx *old_ctx,
		css_select_ctx *new_ctx);

/** The PC bench's check (NS_RESTYLE_CHECK=1): each kept selection compared with a
 * selection made again (fresh: freed here), a difference printed (RESTYLE-MISMATCH) */
bool onyx_restyle_checking(void);
void onyx_restyle_check(struct html_content *c, dom_node *n, const css_select_results *kept,
		css_select_results *fresh);

/** The content goes */
void onyx_restyle_fini(struct html_content *c);

/** DOM changes (dom_event.c): an element's attribute, a node inserted or removed from
 * its parent, a text node's data */
void onyx_restyle_attr_changed(struct html_content *c, dom_node *el);
void onyx_restyle_child_changed(struct html_content *c, dom_node *child, bool inserted);
void onyx_restyle_text_changed(struct html_content *c, dom_node *text);

#endif
