/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 *
 * Onyx: shadow DOM in the box tree -- the flat tree the boxes are built from (a shadow
 * host's children are its shadow root's; a slot's are the nodes assigned to it, else its
 * own), the style scoping of the shadow trees (each tree its own style sheets: the
 * document's do not match inside a shadow tree; :host, ::slotted(), ::part() across), and
 * display: contents (no box: the children are the parent's; the style kept for them).
 *
 * Nothing of this runs for a document without shadow roots but display: contents (the
 * libdom document says whether it has any: dom_onyx_has_shadow).
 */

#ifndef NETSURF_HTML_ONYX_SHADOW_H
#define NETSURF_HTML_ONYX_SHADOW_H

#include <stdbool.h>
#include <dom/dom.h>
#include <libcss/libcss.h>

struct html_content;
struct nscss_select_ctx;

/** A box tree is built: the caches of the last one emptied, whether the document has
 * shadow roots read again (c->onyx_shadow) */
void onyx_shadow_begin(struct html_content *c);

/** The document goes: everything freed */
void onyx_shadow_destroy(struct html_content *c);

/** The flat tree (references returned, NULL: none) */
dom_node *onyx_flat_first_child(struct html_content *c, dom_node *n);
dom_node *onyx_flat_next_sibling(struct html_content *c, dom_node *n);
dom_node *onyx_flat_parent(struct html_content *c, dom_node *n);

/** display: contents -- the element's box (made, not in the tree: it holds the style,
 * freed with the box tree) kept for its children's inheritance, and its style read back */
struct box;
void onyx_contents_keep(struct html_content *c, dom_node *n, struct box *box);
const css_computed_style *onyx_contents_style(struct html_content *c, dom_node *n);

/**
 * The style of an element of a document with shadow roots: in its own tree's context (its
 * style sheets), with the :host rules of its shadow tree, the ::slotted() rules of the
 * slots it is assigned to, the ::part() rules of the tree around its host. ctx: the
 * document's selection context (its parent and root styles, quirks...).
 */
css_select_results *onyx_shadow_style(struct html_content *c, struct nscss_select_ctx *ctx,
		dom_node *n, const css_stylesheet *inline_style);

/** Whether the node is a <slot> element (display: contents by default) */
bool onyx_box_is_slot(dom_node *n);

/** Whether a <style> element is in a shadow tree (its sheet is that tree's alone) */
bool onyx_shadow_in_shadow_tree(dom_node *n);

/** The texts of a shadow root's adopted style sheets (adoptedStyleSheets), set by the
 * scripts: kept on the root (copied) */
void onyx_shadow_set_adopted(dom_node *root, const char *const *texts, unsigned int n);

#endif
