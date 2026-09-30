/*
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 *
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

#ifndef NETSURF_CSS_SELECT_H_
#define NETSURF_CSS_SELECT_H_

#include <stdint.h>

#include <dom/dom.h>

#include <libcss/libcss.h>

struct content;
struct nsurl;

/**
 * Selection context
 */
typedef struct nscss_select_ctx
{
	css_select_ctx *ctx;
	bool quirks;
	struct nsurl *base_url;
	lwc_string *universal;
	const css_computed_style *root_style;
	const css_computed_style *parent_style;
	/* Onyx shadow DOM (html/onyx_shadow.c; NULL in the document's own context): the
	 * shadow host of the tree matched -- featureless there but for :host --, and the
	 * context of the host's own tree */
	struct dom_node *scope_host;
	struct nscss_select_ctx *host_ctx;
} nscss_select_ctx;

/**
 * Onyx: whether libcss keeps something of the text: a declaration list parsed as an inline
 * style (inline true: "prop: value") or a whole style sheet ("selector { ... }") -- *rules,
 * *decl_words as css_stylesheet_onyx_kept says. false: the text could not be parsed at all.
 */
bool nscss_text_kept(const char *text, size_t len, bool inline_style, uint32_t *rules,
		uint32_t *decl_words);

/**
 * Onyx: the states of an element only the scripts know, as the style sheets' pseudo-classes
 * see them (css_select_handler's onyx_node_state): NSCSS_STATE_POPOVER_OPEN (:popover-open,
 * the Popover API), NSCSS_STATE_MODAL (:modal, dialog.showModal()). Kept on the node
 * (libdom's user data); the caller lays the page out again.
 */
#define NSCSS_STATE_POPOVER_OPEN 1u
#define NSCSS_STATE_MODAL 2u
void nscss_node_state_set(struct dom_node *n, unsigned int state, bool on);

/**
 * Onyx: matchMedia -- the media query list text as libcss reads and evaluates it for media
 * (css_select_onyx_media_match): false if it could not be parsed at all.
 */
bool nscss_media_match(css_select_ctx *sctx, const css_media *media,
		const css_unit_ctx *unit_ctx, const char *query, size_t len, bool *match,
		uint32_t *n_queries, uint32_t *invalid);

css_stylesheet *nscss_create_inline_style(const uint8_t *data, size_t len,
		const char *charset, const char *url, bool allow_quirks);

/**
 * Onyx: whether a selector of the sheets of probe (a context of the sheets added since an
 * element's selection) matches it -- true on failure (html/onyx_restyle.c)
 */
bool nscss_probe_style(nscss_select_ctx *ctx, css_select_ctx *probe, dom_node *n,
		const css_media *media, const css_unit_ctx *unit_len_ctx);

css_select_results *nscss_get_style(nscss_select_ctx *ctx, dom_node *n,
		const css_media *media,
		const css_unit_ctx *unit_len_ctx,
		const css_stylesheet *inline_style);

/**
 * Onyx shadow DOM: nscss_get_style for an element of a document with shadow trees: ctx
 * its own tree's context (ctx->scope_host set in a shadow tree), inherit_parent its flat
 * tree parent, the other trees' rules it takes (libcss's css_select_style_onyx).
 */
css_select_results *nscss_get_style_onyx(nscss_select_ctx *ctx, dom_node *n,
		const css_media *media, const css_unit_ctx *unit_len_ctx,
		const css_stylesheet *inline_style, dom_node *inherit_parent, bool no_share,
		const css_select_onyx_scope *scopes, uint32_t n_scopes);

css_computed_style *nscss_get_blank_style(nscss_select_ctx *ctx,
		const css_unit_ctx *unit_len_ctx,
		const css_computed_style *parent);


css_error named_ancestor_node(void *pw, void *node,
		const css_qname *qname, void **ancestor);

css_error node_is_visited(void *pw, void *node, bool *match);

/* Onyx: the node under the pointer while a document's styles are selected (CSS :hover),
 * and whether a :hover selector was tried (the document then has :hover rules). */
extern struct dom_node *nscss_hover_node;
extern bool nscss_hover_used;
/* Onyx: told each node a :hover selector is tried on (tested) while `styled`'s style is
 * selected (html/onyx_hover.c keeps them: only their hover changes restyle anything) */
extern void (*nscss_hover_note)(void *ctx, struct dom_node *tested, struct dom_node *styled);
extern void *nscss_hover_note_ctx;
extern struct dom_node *nscss_styled_node;
/* Onyx: how the selection of nscss_styled_node depended on the tree's structure (the
 * siblings of a node, a node's emptiness): NSCSS_STRUCT_SELF -- its own siblings or
 * children, NSCSS_STRUCT_ANC -- another node's (an ancestor's, a sibling's); set by the
 * selection callbacks, cleared by the caller (html/onyx_restyle.c keeps it) */
#define NSCSS_STRUCT_SELF 1u
#define NSCSS_STRUCT_ANC 2u
extern unsigned int nscss_struct_used;

#endif
