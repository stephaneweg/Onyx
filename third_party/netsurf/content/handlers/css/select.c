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

#include <assert.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>

#include "utils/nsoption.h"
#include "utils/corestrings.h"
#include "utils/log.h"
#include "utils/nsurl.h"
#include "netsurf/plot_style.h"
#include "netsurf/url_db.h"
#include "desktop/system_colour.h"

#include "css/internal.h"
#include "css/hints.h"
#include "css/select.h"

#include <dom/bindings/hubbub/parser.h>	/* Onyx: dom_onyx_shadow_host */

/* Onyx: the selection looked at node's structure */
static const css_qname *nscss_state_qname;	/* the selected node's name in libcss's state */
#define NSCSS_STRUCT(node, self) (nscss_struct_used |= \
		(void *) (node) == (void *) nscss_styled_node ? \
		(self) : NSCSS_STRUCT_ANC)

static css_error node_name(void *pw, void *node, css_qname *qname);
static css_error node_classes(void *pw, void *node,
		lwc_string ***classes, uint32_t *n_classes);
static css_error node_id(void *pw, void *node, lwc_string **id);
static css_error named_parent_node(void *pw, void *node,
		const css_qname *qname, void **parent);
static css_error named_sibling_node(void *pw, void *node,
		const css_qname *qname, void **sibling);
static css_error named_generic_sibling_node(void *pw, void *node,
		const css_qname *qname, void **sibling);
static css_error parent_node(void *pw, void *node, void **parent);
static css_error sibling_node(void *pw, void *node, void **sibling);
static css_error node_has_name(void *pw, void *node,
		const css_qname *qname, bool *match);
static css_error node_has_class(void *pw, void *node,
		lwc_string *name, bool *match);
static css_error node_has_id(void *pw, void *node,
		lwc_string *name, bool *match);
static css_error node_has_attribute(void *pw, void *node,
		const css_qname *qname, bool *match);
static css_error node_has_attribute_equal(void *pw, void *node,
		const css_qname *qname, lwc_string *value,
		bool *match);
static css_error node_has_attribute_dashmatch(void *pw, void *node,
		const css_qname *qname, lwc_string *value,
		bool *match);
static css_error node_has_attribute_includes(void *pw, void *node,
		const css_qname *qname, lwc_string *value,
		bool *match);
static css_error node_has_attribute_prefix(void *pw, void *node,
		const css_qname *qname, lwc_string *value,
		bool *match);
static css_error node_has_attribute_suffix(void *pw, void *node,
		const css_qname *qname, lwc_string *value,
		bool *match);
static css_error node_has_attribute_substring(void *pw, void *node,
		const css_qname *qname, lwc_string *value,
		bool *match);
static css_error node_is_root(void *pw, void *node, bool *match);
static css_error node_count_siblings(void *pw, void *node,
		bool same_name, bool after, int32_t *count);
static css_error node_is_empty(void *pw, void *node, bool *match);
static css_error node_is_link(void *pw, void *node, bool *match);
static css_error node_is_hover(void *pw, void *node, bool *match);
static css_error node_onyx_state(void *pw, void *node, lwc_string *state, bool *match);
static css_error node_is_active(void *pw, void *node, bool *match);
static css_error node_is_focus(void *pw, void *node, bool *match);
static css_error node_is_enabled(void *pw, void *node, bool *match);
static css_error node_is_disabled(void *pw, void *node, bool *match);
static css_error node_is_checked(void *pw, void *node, bool *match);
static css_error node_is_target(void *pw, void *node, bool *match);
static css_error node_is_lang(void *pw, void *node,
		lwc_string *lang, bool *match);
static css_error ua_default_for_property(void *pw, uint32_t property,
		css_hint *hint);
static css_error set_libcss_node_data(void *pw, void *node,
		void *libcss_node_data);
static css_error get_libcss_node_data(void *pw, void *node,
		void **libcss_node_data);

/**
 * Selection callback table for libcss
 */
static css_select_handler selection_handler = {
	CSS_SELECT_HANDLER_VERSION_1,

	node_name,
	node_classes,
	node_id,
	named_ancestor_node,
	named_parent_node,
	named_sibling_node,
	named_generic_sibling_node,
	parent_node,
	sibling_node,
	node_has_name,
	node_has_class,
	node_has_id,
	node_has_attribute,
	node_has_attribute_equal,
	node_has_attribute_dashmatch,
	node_has_attribute_includes,
	node_has_attribute_prefix,
	node_has_attribute_suffix,
	node_has_attribute_substring,
	node_is_root,
	node_count_siblings,
	node_is_empty,
	node_is_link,
	node_is_visited,
	node_is_hover,
	node_is_active,
	node_is_focus,
	node_is_enabled,
	node_is_disabled,
	node_is_checked,
	node_is_target,
	node_is_lang,
	node_presentational_hint,
	ua_default_for_property,
	set_libcss_node_data,
	get_libcss_node_data,
	NULL,			/* (Onyx: onyx_node_is_scope_host, onyx_host_pw: none) */
	NULL,
	node_onyx_state,	/* (Onyx: :popover-open, :modal) */
};

/* Onyx: the states only the scripts know (nscss_node_state_set): the node's user data under
 * this key, a mask of NSCSS_STATE_* */
#define ONYX_SLEN(s) (sizeof(s) - 1)

static dom_string *nscss_state_key(void)
{
	static dom_string *key = NULL;

	if (key == NULL)
		dom_string_create((const uint8_t *) "__ns_onyx_state",
				ONYX_SLEN("__ns_onyx_state"), &key);
	return key;
}

/* exported function documented in css/select.h (Onyx) */
void nscss_node_state_set(struct dom_node *n, unsigned int state, bool on)
{
	dom_string *key = nscss_state_key();
	void *old = NULL;
	uintptr_t bits = 0;

	if (key == NULL || n == NULL)
		return;
	if (dom_node_get_user_data(n, key, &old) == DOM_NO_ERR)
		bits = (uintptr_t) old;
	bits = on ? (bits | state) : (bits & ~(uintptr_t) state);
	dom_node_set_user_data(n, key, (void *) bits, NULL, &old);
}

/** Onyx: css_select_handler's onyx_node_state */
static css_error node_onyx_state(void *pw, void *node, lwc_string *state, bool *match)
{
	dom_string *key = nscss_state_key();
	void *bits = NULL;
	unsigned int want;

	(void) pw;
	*match = false;
	if (key == NULL)
		return CSS_OK;
	if (lwc_string_length(state) == ONYX_SLEN("popover-open") &&
	    memcmp(lwc_string_data(state), "popover-open", ONYX_SLEN("popover-open")) == 0)
		want = NSCSS_STATE_POPOVER_OPEN;
	else if (lwc_string_length(state) == ONYX_SLEN("modal") &&
		 memcmp(lwc_string_data(state), "modal", ONYX_SLEN("modal")) == 0)
		want = NSCSS_STATE_MODAL;
	else
		return CSS_OK;
	if (dom_node_get_user_data(node, key, &bits) == DOM_NO_ERR)
		*match = ((uintptr_t) bits & want) != 0;
	return CSS_OK;
}

/**
 * Create an inline style
 *
 * \param data          Source data
 * \param len           Length of data in bytes
 * \param charset       Charset of data, or NULL if unknown
 * \param url           Base URL of document containing data
 * \param allow_quirks  True to permit CSS parsing quirks
 * \return Pointer to stylesheet, or NULL on failure.
 */
/* exported function documented in css/select.h (Onyx) */
bool nscss_text_kept(const char *text, size_t len, bool inline_style, uint32_t *rules,
		uint32_t *decl_words)
{
	css_stylesheet_params params;
	css_stylesheet *sheet;
	css_error error;
	bool ok = false;

	memset(&params, 0, sizeof params);
	params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
	params.level = CSS_LEVEL_DEFAULT;
	params.charset = "UTF-8";
	params.url = "about:blank";
	params.inline_style = inline_style;
	params.resolve = nscss_resolve_url;
	params.color = ns_system_colour;

	if (css_stylesheet_create(&params, &sheet) != CSS_OK)
		return false;
	error = css_stylesheet_append_data(sheet, (const uint8_t *) text, len);
	if ((error == CSS_OK || error == CSS_NEEDDATA) &&
	    css_stylesheet_data_done(sheet) == CSS_OK &&
	    css_stylesheet_onyx_kept(sheet, rules, decl_words) == CSS_OK)
		ok = true;
	css_stylesheet_destroy(sheet);
	return ok;
}

/* exported function documented in css/select.h (Onyx) */
bool nscss_media_match(css_select_ctx *sctx, const css_media *media,
		const css_unit_ctx *unit_ctx, const char *query, size_t len, bool *match,
		uint32_t *n_queries, uint32_t *invalid)
{
	static const char pre[] = "@media ", post[] = " { a { color: red } }";
	css_stylesheet_params params;
	static css_select_ctx *own = NULL;	/* (before the page's: its strings only) */
	css_stylesheet *sheet;
	css_error error;
	bool ok = false;

	if (sctx == NULL) {
		if (own == NULL && css_select_ctx_create(&own) != CSS_OK)
			return false;
		sctx = own;
	}
	memset(&params, 0, sizeof params);
	params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
	params.level = CSS_LEVEL_DEFAULT;
	params.charset = "UTF-8";
	params.url = "about:blank";
	params.resolve = nscss_resolve_url;
	params.color = ns_system_colour;
	if (css_stylesheet_create(&params, &sheet) != CSS_OK)
		return false;
	error = css_stylesheet_append_data(sheet, (const uint8_t *) pre, sizeof pre - 1);
	if (error == CSS_OK || error == CSS_NEEDDATA)
		error = css_stylesheet_append_data(sheet, (const uint8_t *) query, len);
	if (error == CSS_OK || error == CSS_NEEDDATA)
		error = css_stylesheet_append_data(sheet, (const uint8_t *) post,
				sizeof post - 1);
	if ((error == CSS_OK || error == CSS_NEEDDATA) &&
	    css_stylesheet_data_done(sheet) == CSS_OK &&
	    css_select_onyx_media_match(sctx, sheet, unit_ctx, media, match, n_queries,
			invalid) == CSS_OK)
		ok = true;
	css_stylesheet_destroy(sheet);
	return ok;
}

css_stylesheet *nscss_create_inline_style(const uint8_t *data, size_t len,
		const char *charset, const char *url, bool allow_quirks)
{
	css_stylesheet_params params;
	css_stylesheet *sheet;
	css_error error;

	params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
	params.level = CSS_LEVEL_DEFAULT;
	params.charset = charset;
	params.url = url;
	params.title = NULL;
	params.allow_quirks = allow_quirks;
	params.inline_style = true;
	params.resolve = nscss_resolve_url;
	params.resolve_pw = NULL;
	params.import = NULL;
	params.import_pw = NULL;
	params.color = ns_system_colour;
	params.color_pw = NULL;
	params.font = NULL;
	params.font_pw = NULL;

	error = css_stylesheet_create(&params, &sheet);
	if (error != CSS_OK) {
		NSLOG(netsurf, INFO, "Failed creating sheet: %d", error);
		return NULL;
	}

	error = css_stylesheet_append_data(sheet, data, len);
	if (error != CSS_OK && error != CSS_NEEDDATA) {
		NSLOG(netsurf, INFO, "failed appending data: %d", error);
		css_stylesheet_destroy(sheet);
		return NULL;
	}

	error = css_stylesheet_data_done(sheet);
	if (error != CSS_OK) {
		NSLOG(netsurf, INFO, "failed completing parse: %d", error);
		css_stylesheet_destroy(sheet);
		return NULL;
	}

	return sheet;
}

/* Handler for libcss_node_data, stored as libdom node user data */
static void nscss_dom_user_data_handler(dom_node_operation operation,
		dom_string *key, void *data, struct dom_node *src,
		struct dom_node *dst)
{
	css_error error;

	if (dom_string_isequal(corestring_dom___ns_key_libcss_node_data,
			key) == false || data == NULL) {
		return;
	}

	switch (operation) {
	case DOM_NODE_CLONED:
		error = css_libcss_node_data_handler(&selection_handler,
				CSS_NODE_CLONED,
				NULL, src, dst, data);
		if (error != CSS_OK)
			NSLOG(netsurf, INFO,
			      "Failed to clone libcss_node_data.");
		break;

	case DOM_NODE_RENAMED:
		error = css_libcss_node_data_handler(&selection_handler,
				CSS_NODE_MODIFIED,
				NULL, src, NULL, data);
		if (error != CSS_OK)
			NSLOG(netsurf, INFO,
			      "Failed to update libcss_node_data.");
		break;

	case DOM_NODE_IMPORTED:
	case DOM_NODE_ADOPTED:
	case DOM_NODE_DELETED:
		error = css_libcss_node_data_handler(&selection_handler,
				CSS_NODE_DELETED,
				NULL, src, NULL, data);
		if (error != CSS_OK)
			NSLOG(netsurf, INFO,
			      "Failed to delete libcss_node_data.");
		break;

	default:
		NSLOG(netsurf, INFO, "User data operation not handled.");
		assert(0);
	}
}

/**
 * Get style selection results for an element
 *
 * \param ctx             CSS selection context
 * \param n               Element to select for
 * \param media           Permitted media types
 * \param unit_unit_len_ctx    Unit length conversion context
 * \param inline_style    Inline style associated with element, or NULL
 * \return Pointer to selection results (containing computed styles),
 *         or NULL on failure
 */
static css_select_results *nscss_compose(nscss_select_ctx *ctx,
		const css_unit_ctx *unit_len_ctx, css_select_results *styles);
static css_select_handler onyx_scoped_handler;

css_select_results *nscss_get_style(nscss_select_ctx *ctx, dom_node *n,
		const css_media *media,
		const css_unit_ctx *unit_len_ctx,
		const css_stylesheet *inline_style)
{
	css_select_results *styles;
	css_error error;

	/* Select style for node */
	error = css_select_style(ctx->ctx, n, unit_len_ctx, media, inline_style,
			&selection_handler, ctx, &styles);

	if (error != CSS_OK || styles == NULL) {
		/* Failed selecting partial style -- bail out */
		return NULL;
	}
	return nscss_compose(ctx, unit_len_ctx, styles);
}

/* exported function documented in css/select.h (Onyx) */
bool nscss_node_visited(struct dom_node *n, struct nsurl *base_url)
{
	nscss_select_ctx ctx;
	bool match = false;

	memset(&ctx, 0, sizeof(ctx));
	ctx.base_url = base_url;
	if (node_is_visited(&ctx, n, &match) != CSS_OK)
		return false;
	return match;
}

/* exported function documented in css/select.h (Onyx) */
bool nscss_probe_style(nscss_select_ctx *ctx, css_select_ctx *probe, dom_node *n,
		const css_media *media, const css_unit_ctx *unit_len_ctx)
{
	bool matched = true;

	if (css_select_style_onyx_probe(probe, n, unit_len_ctx, media,
			&selection_handler, ctx, &matched) != CSS_OK)
		return true;
	return matched;
}

/* exported function documented in css/select.h (Onyx) */
css_select_results *nscss_get_style_onyx(nscss_select_ctx *ctx, dom_node *n,
		const css_media *media, const css_unit_ctx *unit_len_ctx,
		const css_stylesheet *inline_style, dom_node *inherit_parent, bool no_share,
		const css_select_onyx_scope *scopes, uint32_t n_scopes)
{
	css_select_results *styles;
	css_error error;

	error = css_select_style_onyx(ctx->ctx, n, unit_len_ctx, media, inline_style,
			&onyx_scoped_handler, ctx, inherit_parent, no_share, scopes,
			n_scopes, &styles);
	if (error != CSS_OK || styles == NULL)
		return NULL;
	return nscss_compose(ctx, unit_len_ctx, styles);
}

/* the partial styles completed: the element's with its parent's, the pseudo elements'
 * with the element's */
static css_select_results *nscss_compose(nscss_select_ctx *ctx,
		const css_unit_ctx *unit_len_ctx, css_select_results *styles)
{
	css_computed_style *composed;
	int pseudo_element;
	css_error error;

	/* If there's a parent style, compose with partial to obtain
	 * complete computed style for element */
	if (ctx->parent_style != NULL) {
		/* Complete the computed style, by composing with the parent
		 * element's style */
		error = css_computed_style_compose(ctx->parent_style,
				styles->styles[CSS_PSEUDO_ELEMENT_NONE],
				unit_len_ctx, &composed);
		if (error != CSS_OK) {
			css_select_results_destroy(styles);
			return NULL;
		}

		/* Replace select_results style with composed style */
		css_computed_style_destroy(
				styles->styles[CSS_PSEUDO_ELEMENT_NONE]);
		styles->styles[CSS_PSEUDO_ELEMENT_NONE] = composed;
	}

	for (pseudo_element = CSS_PSEUDO_ELEMENT_NONE + 1;
			pseudo_element < CSS_PSEUDO_ELEMENT_COUNT;
			pseudo_element++) {

		if (pseudo_element == CSS_PSEUDO_ELEMENT_FIRST_LETTER ||
				pseudo_element == CSS_PSEUDO_ELEMENT_FIRST_LINE)
			/* TODO: Handle first-line and first-letter pseudo
			 *       element computed style completion */
			continue;

		if (styles->styles[pseudo_element] == NULL)
			/* There were no rules concerning this pseudo element */
			continue;

		/* Complete the pseudo element's computed style, by composing
		 * with the base element's style */
		error = css_computed_style_compose(
				styles->styles[CSS_PSEUDO_ELEMENT_NONE],
				styles->styles[pseudo_element],
				unit_len_ctx, &composed);
		if (error != CSS_OK) {
			/* TODO: perhaps this shouldn't be quite so
			 * catastrophic? */
			css_select_results_destroy(styles);
			return NULL;
		}

		/* Replace select_results style with composed style */
		css_computed_style_destroy(styles->styles[pseudo_element]);
		styles->styles[pseudo_element] = composed;
	}

	return styles;
}

/**
 * Get a blank style
 *
 * \param ctx           CSS selection context
 * \param unit_unit_len_ctx  Unit length conversion context
 * \param parent        Parent style to cascade inherited properties from
 * \return Pointer to blank style, or NULL on failure
 */
css_computed_style *nscss_get_blank_style(nscss_select_ctx *ctx,
		const css_unit_ctx *unit_len_ctx,
		const css_computed_style *parent)
{
	css_computed_style *partial, *composed;
	css_error error;

	error = css_select_default_style(ctx->ctx,
			&selection_handler, ctx, &partial);
	if (error != CSS_OK) {
		return NULL;
	}

	/* TODO: Do we really need to compose?  Initial style shouldn't
	 * have any inherited properties. */
	error = css_computed_style_compose(parent, partial,
			unit_len_ctx, &composed);
	css_computed_style_destroy(partial);
	if (error != CSS_OK) {
		css_computed_style_destroy(composed);
		return NULL;
	}

	return composed;
}

/******************************************************************************
 * Style selection callbacks                                                  *
 ******************************************************************************/

/**
 * Callback to retrieve a node's name.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param qname  Pointer to location to receive node name
 * \return CSS_OK on success,
 *         CSS_NOMEM on memory exhaustion.
 */
css_error node_name(void *pw, void *node, css_qname *qname)
{
	dom_node *n = node;
	dom_string *name;
	dom_exception err;

	if (node == (void *) nscss_styled_node)
		nscss_state_qname = qname;	/* (Onyx: libcss's state) */

	err = dom_node_get_node_name(n, &name);
	if (err != DOM_NO_ERR)
		return CSS_NOMEM;

	qname->ns = NULL;

	err = dom_string_intern(name, &qname->name);
	if (err != DOM_NO_ERR) {
		dom_string_unref(name);
		return CSS_NOMEM;
	}

	dom_string_unref(name);

	return CSS_OK;
}

/**
 * Callback to retrieve a node's classes.
 *
 * \param pw         HTML document
 * \param node       DOM node
 * \param classes    Pointer to location to receive class name array
 * \param n_classes  Pointer to location to receive length of class name array
 * \return CSS_OK on success,
 *         CSS_NOMEM on memory exhaustion.
 *
 * \note The returned array will be destroyed by libcss. Therefore, it must
 *       be allocated using the same allocator as used by libcss during style
 *       selection.
 */
css_error node_classes(void *pw, void *node,
		lwc_string ***classes, uint32_t *n_classes)
{
	dom_node *n = node;
	dom_exception err;

	*classes = NULL;
	*n_classes = 0;

	err = dom_element_get_classes(n, classes, n_classes);
	if (err != DOM_NO_ERR)
		return CSS_NOMEM;

	return CSS_OK;
}

/**
 * Callback to retrieve a node's ID.
 *
 * \param pw    HTML document
 * \param node  DOM node
 * \param id    Pointer to location to receive id value
 * \return CSS_OK on success,
 *         CSS_NOMEM on memory exhaustion.
 */
css_error node_id(void *pw, void *node, lwc_string **id)
{
	dom_node *n = node;
	dom_string *attr;
	dom_exception err;

	*id = NULL;

	/** \todo Assumes an HTML DOM */
	err = dom_html_element_get_id(n, &attr);
	if (err != DOM_NO_ERR)
		return CSS_NOMEM;

	if (attr != NULL) {
		err = dom_string_intern(attr, id);
		if (err != DOM_NO_ERR) {
			dom_string_unref(attr);
			return CSS_NOMEM;
		}
		dom_string_unref(attr);
	}

	return CSS_OK;
}

/**
 * Callback to find a named ancestor node.
 *
 * \param pw        HTML document
 * \param node      DOM node
 * \param qname     Node name to search for
 * \param ancestor  Pointer to location to receive ancestor
 * \return CSS_OK.
 *
 * \post \a ancestor will contain the result, or NULL if there is no match
 */
css_error named_ancestor_node(void *pw, void *node,
		const css_qname *qname, void **ancestor)
{
	dom_element_named_ancestor_node(node, qname->name,
			(struct dom_element **)ancestor);
	dom_node_unref(*ancestor);

	return CSS_OK;
}

/**
 * Callback to find a named parent node
 *
 * \param pw      HTML document
 * \param node    DOM node
 * \param qname   Node name to search for
 * \param parent  Pointer to location to receive parent
 * \return CSS_OK.
 *
 * \post \a parent will contain the result, or NULL if there is no match
 */
css_error named_parent_node(void *pw, void *node,
		const css_qname *qname, void **parent)
{
	dom_element_named_parent_node(node, qname->name,
			(struct dom_element **)parent);
	dom_node_unref(*parent);

	return CSS_OK;
}

/**
 * Callback to find a named sibling node.
 *
 * \param pw       HTML document
 * \param node     DOM node
 * \param qname    Node name to search for
 * \param sibling  Pointer to location to receive sibling
 * \return CSS_OK.
 *
 * \post \a sibling will contain the result, or NULL if there is no match
 */
css_error named_sibling_node(void *pw, void *node,
		const css_qname *qname, void **sibling)
{
	NSCSS_STRUCT(node, NSCSS_STRUCT_SIB);	/* (Onyx) */
	dom_node *n = node;
	dom_node *prev;
	dom_exception err;

	*sibling = NULL;

	/* Find sibling element */
	err = dom_node_get_previous_sibling(n, &n);
	if (err != DOM_NO_ERR)
		return CSS_OK;

	while (n != NULL) {
		dom_node_type type;

		err = dom_node_get_node_type(n, &type);
		if (err != DOM_NO_ERR) {
			dom_node_unref(n);
			return CSS_OK;
		}

		if (type == DOM_ELEMENT_NODE)
			break;

		err = dom_node_get_previous_sibling(n, &prev);
		if (err != DOM_NO_ERR) {
			dom_node_unref(n);
			return CSS_OK;
		}

		dom_node_unref(n);
		n = prev;
	}

	if (n != NULL) {
		dom_string *name;

		err = dom_node_get_node_name(n, &name);
		if (err != DOM_NO_ERR) {
			dom_node_unref(n);
			return CSS_OK;
		}

		dom_node_unref(n);

		if (dom_string_caseless_lwc_isequal(name, qname->name)) {
			*sibling = n;
		}

		dom_string_unref(name);
	}

	return CSS_OK;
}

/**
 * Callback to find a named generic sibling node.
 *
 * \param pw       HTML document
 * \param node     DOM node
 * \param qname    Node name to search for
 * \param sibling  Pointer to location to receive ancestor
 * \return CSS_OK.
 *
 * \post \a sibling will contain the result, or NULL if there is no match
 */
css_error named_generic_sibling_node(void *pw, void *node,
		const css_qname *qname, void **sibling)
{
	/* (Onyx: not libcss's search of a sibling whose style to share -- the node's
	 * own name, the selection state's: no dependence) */
	if (qname != nscss_state_qname)
		NSCSS_STRUCT(node, NSCSS_STRUCT_SIB);
	dom_node *n = node;
	dom_node *prev;
	dom_exception err;

	*sibling = NULL;

	err = dom_node_get_previous_sibling(n, &n);
	if (err != DOM_NO_ERR)
		return CSS_OK;

	while (n != NULL) {
		dom_node_type type;
		dom_string *name;

		err = dom_node_get_node_type(n, &type);
		if (err != DOM_NO_ERR) {
			dom_node_unref(n);
			return CSS_OK;
		}

		if (type == DOM_ELEMENT_NODE) {
			err = dom_node_get_node_name(n, &name);
			if (err != DOM_NO_ERR) {
				dom_node_unref(n);
				return CSS_OK;
			}

			if (dom_string_caseless_lwc_isequal(name,
					qname->name)) {
				dom_string_unref(name);
				dom_node_unref(n);
				*sibling = n;
				break;
			}
			dom_string_unref(name);
		}

		err = dom_node_get_previous_sibling(n, &prev);
		if (err != DOM_NO_ERR) {
			dom_node_unref(n);
			return CSS_OK;
		}

		dom_node_unref(n);
		n = prev;
	}

	return CSS_OK;
}

/**
 * Callback to retrieve the parent of a node.
 *
 * \param pw      HTML document
 * \param node    DOM node
 * \param parent  Pointer to location to receive parent
 * \return CSS_OK.
 *
 * \post \a parent will contain the result, or NULL if there is no match
 */
css_error parent_node(void *pw, void *node, void **parent)
{
	dom_element_parent_node(node, (struct dom_element **)parent);
	dom_node_unref(*parent);

	return CSS_OK;
}

/**
 * Callback to retrieve the preceding sibling of a node.
 *
 * \param pw       HTML document
 * \param node     DOM node
 * \param sibling  Pointer to location to receive sibling
 * \return CSS_OK.
 *
 * \post \a sibling will contain the result, or NULL if there is no match
 */
css_error sibling_node(void *pw, void *node, void **sibling)
{
	NSCSS_STRUCT(node, NSCSS_STRUCT_SIB);	/* (Onyx) */
	dom_node *n = node;
	dom_node *prev;
	dom_exception err;

	*sibling = NULL;

	/* Find sibling element */
	err = dom_node_get_previous_sibling(n, &n);
	if (err != DOM_NO_ERR)
		return CSS_OK;

	while (n != NULL) {
		dom_node_type type;

		err = dom_node_get_node_type(n, &type);
		if (err != DOM_NO_ERR) {
			dom_node_unref(n);
			return CSS_OK;
		}

		if (type == DOM_ELEMENT_NODE)
			break;

		err = dom_node_get_previous_sibling(n, &prev);
		if (err != DOM_NO_ERR) {
			dom_node_unref(n);
			return CSS_OK;
		}

		dom_node_unref(n);
		n = prev;
	}

	if (n != NULL) {
		/** \todo Sort out reference counting */
		dom_node_unref(n);

		*sibling = n;
	}

	return CSS_OK;
}

/**
 * Callback to determine if a node has the given name.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param qname  Name to match
 * \param match  Pointer to location to receive result
 * \return CSS_OK.
 *
 * \post \a match will contain true if the node matches and false otherwise.
 */
css_error node_has_name(void *pw, void *node,
		const css_qname *qname, bool *match)
{
	nscss_select_ctx *ctx = pw;
	dom_node *n = node;

	if (lwc_string_isequal(qname->name, ctx->universal, match) ==
			lwc_error_ok && *match == false) {
		dom_string *name;
		dom_exception err;

		err = dom_node_get_node_name(n, &name);
		if (err != DOM_NO_ERR)
			return CSS_OK;

		/* Element names are case insensitive in HTML */
		*match = dom_string_caseless_lwc_isequal(name, qname->name);

		dom_string_unref(name);
	}

	return CSS_OK;
}

/**
 * Callback to determine if a node has the given class.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param name   Name to match
 * \param match  Pointer to location to receive result
 * \return CSS_OK.
 *
 * \post \a match will contain true if the node matches and false otherwise.
 */
css_error node_has_class(void *pw, void *node,
		lwc_string *name, bool *match)
{
	dom_node *n = node;
	dom_exception err;

	/** \todo: Ensure that libdom performs case-insensitive
	 * matching in quirks mode */
	err = dom_element_has_class(n, name, match);

	assert(err == DOM_NO_ERR);

	return CSS_OK;
}

/**
 * Callback to determine if a node has the given id.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param name   Name to match
 * \param match  Pointer to location to receive result
 * \return CSS_OK.
 *
 * \post \a match will contain true if the node matches and false otherwise.
 */
css_error node_has_id(void *pw, void *node,
		lwc_string *name, bool *match)
{
	dom_node *n = node;
	dom_string *attr;
	dom_exception err;

	*match = false;

	/** \todo Assumes an HTML DOM */
	err = dom_html_element_get_id(n, &attr);
	if (err != DOM_NO_ERR)
		return CSS_OK;

	if (attr != NULL) {
		*match = dom_string_lwc_isequal(attr, name);

		dom_string_unref(attr);
	}

	return CSS_OK;
}

/**
 * Callback to determine if a node has an attribute with the given name.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param qname  Name to match
 * \param match  Pointer to location to receive result
 * \return CSS_OK on success,
 *         CSS_NOMEM on memory exhaustion.
 *
 * \post \a match will contain true if the node matches and false otherwise.
 */
css_error node_has_attribute(void *pw, void *node,
		const css_qname *qname, bool *match)
{
	dom_node *n = node;
	dom_string *name;
	dom_exception err;

	err = dom_string_create_interned(
			(const uint8_t *) lwc_string_data(qname->name),
			lwc_string_length(qname->name), &name);
	if (err != DOM_NO_ERR)
		return CSS_NOMEM;

	err = dom_element_has_attribute(n, name, match);
	if (err != DOM_NO_ERR) {
		dom_string_unref(name);
		return CSS_OK;
	}

	dom_string_unref(name);

	return CSS_OK;
}

/**
 * Callback to determine if a node has an attribute with given name and value.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param qname  Name to match
 * \param value  Value to match
 * \param match  Pointer to location to receive result
 * \return CSS_OK on success,
 *         CSS_NOMEM on memory exhaustion.
 *
 * \post \a match will contain true if the node matches and false otherwise.
 */
css_error node_has_attribute_equal(void *pw, void *node,
		const css_qname *qname, lwc_string *value,
		bool *match)
{
	dom_node *n = node;
	dom_string *name;
	dom_string *atr_val;
	dom_exception err;

	size_t vlen = lwc_string_length(value);

	if (vlen == 0) {
		*match = false;
		return CSS_OK;
	}

	err = dom_string_create_interned(
		(const uint8_t *) lwc_string_data(qname->name),
		lwc_string_length(qname->name), &name);
	if (err != DOM_NO_ERR)
		return CSS_NOMEM;

	err = dom_element_get_attribute(n, name, &atr_val);
	if ((err != DOM_NO_ERR) || (atr_val == NULL)) {
		dom_string_unref(name);
		*match = false;
		return CSS_OK;
	}

	dom_string_unref(name);

	*match = dom_string_caseless_lwc_isequal(atr_val, value);

	dom_string_unref(atr_val);

	return CSS_OK;
}

/**
 * Callback to determine if a node has an attribute with the given name whose
 * value dashmatches that given.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param qname  Name to match
 * \param value  Value to match
 * \param match  Pointer to location to receive result
 * \return CSS_OK on success,
 *         CSS_NOMEM on memory exhaustion.
 *
 * \post \a match will contain true if the node matches and false otherwise.
 */
css_error node_has_attribute_dashmatch(void *pw, void *node,
		const css_qname *qname, lwc_string *value,
		bool *match)
{
	dom_node *n = node;
	dom_string *name;
	dom_string *atr_val;
	dom_exception err;

	size_t vlen = lwc_string_length(value);

	if (vlen == 0) {
		*match = false;
		return CSS_OK;
	}

	err = dom_string_create_interned(
		(const uint8_t *) lwc_string_data(qname->name),
		lwc_string_length(qname->name), &name);
	if (err != DOM_NO_ERR)
		return CSS_NOMEM;

	err = dom_element_get_attribute(n, name, &atr_val);
	if ((err != DOM_NO_ERR) || (atr_val == NULL)) {
		dom_string_unref(name);
		*match = false;
		return CSS_OK;
	}

	dom_string_unref(name);

	/* check for exact match */
	*match = dom_string_caseless_lwc_isequal(atr_val, value);

	/* check for dashmatch */
	if (*match == false) {
		const char *vdata = lwc_string_data(value);
		const char *data = (const char *) dom_string_data(atr_val);
		size_t len = dom_string_byte_length(atr_val);

		if (len > vlen && data[vlen] == '-' &&
		    strncasecmp(data, vdata, vlen) == 0) {
				*match = true;
		}
	}

	dom_string_unref(atr_val);

	return CSS_OK;
}

/**
 * Callback to determine if a node has an attribute with the given name whose
 * value includes that given.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param qname  Name to match
 * \param value  Value to match
 * \param match  Pointer to location to receive result
 * \return CSS_OK on success,
 *         CSS_NOMEM on memory exhaustion.
 *
 * \post \a match will contain true if the node matches and false otherwise.
 */
css_error node_has_attribute_includes(void *pw, void *node,
		const css_qname *qname, lwc_string *value,
		bool *match)
{
	dom_node *n = node;
	dom_string *name;
	dom_string *atr_val;
	dom_exception err;
	size_t vlen = lwc_string_length(value);
	const char *p;
	const char *start;
	const char *end;

	*match = false;

	if (vlen == 0) {
		return CSS_OK;
	}

	err = dom_string_create_interned(
		(const uint8_t *) lwc_string_data(qname->name),
		lwc_string_length(qname->name), &name);
	if (err != DOM_NO_ERR)
		return CSS_NOMEM;

	err = dom_element_get_attribute(n, name, &atr_val);
	if ((err != DOM_NO_ERR) || (atr_val == NULL)) {
		dom_string_unref(name);
		*match = false;
		return CSS_OK;
	}

	dom_string_unref(name);

	/* check for match */
	start = (const char *) dom_string_data(atr_val);
	end = start + dom_string_byte_length(atr_val);

	for (p = start; p <= end; p++) {
		if (*p == ' ' || *p == '\0') {
			if ((size_t) (p - start) == vlen &&
			    strncasecmp(start,
					lwc_string_data(value),
					vlen) == 0) {
				*match = true;
				break;
			}

			start = p + 1;
		}
	}

	dom_string_unref(atr_val);

	return CSS_OK;
}

/**
 * Callback to determine if a node has an attribute with the given name whose
 * value has the prefix given.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param qname  Name to match
 * \param value  Value to match
 * \param match  Pointer to location to receive result
 * \return CSS_OK on success,
 *         CSS_NOMEM on memory exhaustion.
 *
 * \post \a match will contain true if the node matches and false otherwise.
 */
css_error node_has_attribute_prefix(void *pw, void *node,
		const css_qname *qname, lwc_string *value,
		bool *match)
{
	dom_node *n = node;
	dom_string *name;
	dom_string *atr_val;
	dom_exception err;

	size_t vlen = lwc_string_length(value);

	if (vlen == 0) {
		*match = false;
		return CSS_OK;
	}

	err = dom_string_create_interned(
		(const uint8_t *) lwc_string_data(qname->name),
		lwc_string_length(qname->name), &name);
	if (err != DOM_NO_ERR)
		return CSS_NOMEM;

	err = dom_element_get_attribute(n, name, &atr_val);
	if ((err != DOM_NO_ERR) || (atr_val == NULL)) {
		dom_string_unref(name);
		*match = false;
		return CSS_OK;
	}

	dom_string_unref(name);

	/* check for exact match */
	*match = dom_string_caseless_lwc_isequal(atr_val, value);

	/* check for prefix match */
	if (*match == false) {
		const char *data = (const char *) dom_string_data(atr_val);
		size_t len = dom_string_byte_length(atr_val);

		if ((len >= vlen) &&
		    (strncasecmp(data, lwc_string_data(value), vlen) == 0)) {
			*match = true;
		}
	}

	dom_string_unref(atr_val);

	return CSS_OK;
}

/**
 * Callback to determine if a node has an attribute with the given name whose
 * value has the suffix given.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param qname  Name to match
 * \param value  Value to match
 * \param match  Pointer to location to receive result
 * \return CSS_OK on success,
 *         CSS_NOMEM on memory exhaustion.
 *
 * \post \a match will contain true if the node matches and false otherwise.
 */
css_error node_has_attribute_suffix(void *pw, void *node,
		const css_qname *qname, lwc_string *value,
		bool *match)
{
	dom_node *n = node;
	dom_string *name;
	dom_string *atr_val;
	dom_exception err;

	size_t vlen = lwc_string_length(value);

	if (vlen == 0) {
		*match = false;
		return CSS_OK;
	}

	err = dom_string_create_interned(
		(const uint8_t *) lwc_string_data(qname->name),
		lwc_string_length(qname->name), &name);
	if (err != DOM_NO_ERR)
		return CSS_NOMEM;

	err = dom_element_get_attribute(n, name, &atr_val);
	if ((err != DOM_NO_ERR) || (atr_val == NULL)) {
		dom_string_unref(name);
		*match = false;
		return CSS_OK;
	}

	dom_string_unref(name);

	/* check for exact match */
	*match = dom_string_caseless_lwc_isequal(atr_val, value);

	/* check for prefix match */
	if (*match == false) {
		const char *data = (const char *) dom_string_data(atr_val);
		size_t len = dom_string_byte_length(atr_val);

		const char *start = (char *) data + len - vlen;

		if ((len >= vlen) &&
		    (strncasecmp(start, lwc_string_data(value), vlen) == 0)) {
			*match = true;
		}


	}

	dom_string_unref(atr_val);

	return CSS_OK;
}

/**
 * Callback to determine if a node has an attribute with the given name whose
 * value contains the substring given.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param qname  Name to match
 * \param value  Value to match
 * \param match  Pointer to location to receive result
 * \return CSS_OK on success,
 *         CSS_NOMEM on memory exhaustion.
 *
 * \post \a match will contain true if the node matches and false otherwise.
 */
css_error node_has_attribute_substring(void *pw, void *node,
		const css_qname *qname, lwc_string *value,
		bool *match)
{
	dom_node *n = node;
	dom_string *name;
	dom_string *atr_val;
	dom_exception err;

	size_t vlen = lwc_string_length(value);

	if (vlen == 0) {
		*match = false;
		return CSS_OK;
	}

	err = dom_string_create_interned(
		(const uint8_t *) lwc_string_data(qname->name),
		lwc_string_length(qname->name), &name);
	if (err != DOM_NO_ERR)
		return CSS_NOMEM;

	err = dom_element_get_attribute(n, name, &atr_val);
	if ((err != DOM_NO_ERR) || (atr_val == NULL)) {
		dom_string_unref(name);
		*match = false;
		return CSS_OK;
	}

	dom_string_unref(name);

	/* check for exact match */
	*match = dom_string_caseless_lwc_isequal(atr_val, value);

	/* check for prefix match */
	if (*match == false) {
		const char *vdata = lwc_string_data(value);
		const char *start = (const char *) dom_string_data(atr_val);
		size_t len = dom_string_byte_length(atr_val);
		const char *last_start = start + len - vlen;

		if (len >= vlen) {
			while (start <= last_start) {
				if (strncasecmp(start, vdata,
						vlen) == 0) {
					*match = true;
					break;
				}

				start++;
			}
		}
	}

	dom_string_unref(atr_val);

	return CSS_OK;
}

/**
 * Callback to determine if a node is the root node of the document.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param match  Pointer to location to receive result
 * \return CSS_OK.
 *
 * \post \a match will contain true if the node matches and false otherwise.
 */
css_error node_is_root(void *pw, void *node, bool *match)
{
	dom_node *n = node;
	dom_node *parent;
	dom_node_type type;
	dom_exception err;

	err = dom_node_get_parent_node(n, &parent);
	if (err != DOM_NO_ERR) {
		return CSS_NOMEM;
	}

	if (parent != NULL) {
		err = dom_node_get_node_type(parent, &type);

		dom_node_unref(parent);

		if (err != DOM_NO_ERR)
			return CSS_NOMEM;

		if (type != DOM_DOCUMENT_NODE) {
			*match = false;
			return CSS_OK;
		}
	}

	*match = true;

	return CSS_OK;
}

static int
node_count_siblings_check(dom_node *node,
			  bool check_name,
			  dom_string *name)
{
	dom_node_type type;
	int ret = 0;
	dom_exception exc;

	if (node == NULL)
		return 0;

	exc = dom_node_get_node_type(node, &type);
	if ((exc != DOM_NO_ERR) || (type != DOM_ELEMENT_NODE)) {
		return 0;
	}

	if (check_name) {
		dom_string *node_name = NULL;
		exc = dom_node_get_node_name(node, &node_name);

		if ((exc == DOM_NO_ERR) && (node_name != NULL)) {

			if (dom_string_caseless_isequal(name,
							node_name)) {
				ret = 1;
			}
			dom_string_unref(node_name);
		}
	} else {
		ret = 1;
	}

	return ret;
}

/**
 * Callback to count a node's siblings.
 *
 * \param pw         HTML document
 * \param n          DOM node
 * \param same_name  Only count siblings with the same name, or all
 * \param after      Count anteceding instead of preceding siblings
 * \param count      Pointer to location to receive result
 * \return CSS_OK.
 *
 * \post \a count will contain the number of siblings
 */
/*
 * Onyx: the element siblings before and after each element, found for all the children of
 * a parent at once and kept until the DOM changes (nscss_dom_changed, from the mutation
 * events): :nth-child() and :last-child counted the siblings for each element -- a list of
 * 2500 rows striped by :nth-child(odd) took 3 million steps a box tree.
 */
struct nscss_sib {
	const void *node;
	unsigned int gen;
	int32_t before, after;
};
static struct nscss_sib *nscss_sibs;
static size_t nscss_sibs_size, nscss_sibs_used;
static unsigned int nscss_dom_gen = 1, nscss_sibs_gen;

/* exported function documented in css/select.h (Onyx) */
void nscss_dom_changed(void)
{
	nscss_dom_gen++;
	if (nscss_dom_gen == 0)
		nscss_dom_gen = 1;
}

static struct nscss_sib *nscss_sib_slot(const void *node)
{
	size_t i = ((uintptr_t) node >> 4) * 0x9E3779B1u & (nscss_sibs_size - 1);

	while (nscss_sibs[i].gen == nscss_sibs_gen && nscss_sibs[i].node != node)
		i = (i + 1) & (nscss_sibs_size - 1);
	return &nscss_sibs[i];
}

/** The siblings of n from the table (filled for its parent's children): false if none */
static bool nscss_sib_count(dom_node *n, bool after, int32_t *count)
{
	struct nscss_sib *e;
	dom_node *parent = NULL, *c = NULL, *next;
	int32_t total = 0, k;

	if (nscss_sibs_gen != nscss_dom_gen) {
		nscss_sibs_gen = nscss_dom_gen;
		nscss_sibs_used = 0;
	}
	if (nscss_sibs != NULL) {
		e = nscss_sib_slot(n);
		if (e->gen == nscss_sibs_gen) {
			*count = after ? e->after : e->before;
			return true;
		}
	}
	/* all the parent's element children: counted, then numbered */
	if (dom_node_get_parent_node(n, &parent) != DOM_NO_ERR || parent == NULL)
		return false;
	for (dom_node_get_first_child(parent, &c); c != NULL; c = next) {
		total += node_count_siblings_check(c, false, NULL);
		next = NULL;
		dom_node_get_next_sibling(c, &next);
		dom_node_unref(c);
	}
	if ((nscss_sibs_used + total + 1) * 2 > nscss_sibs_size) {
		size_t size = nscss_sibs_size ? nscss_sibs_size : 1024;
		while ((nscss_sibs_used + total + 1) * 2 > size)
			size *= 2;
		if (size != nscss_sibs_size || nscss_sibs_used > 0) {
			/* (a new table: the old entries dropped) */
			struct nscss_sib *t = calloc(size, sizeof(*t));
			if (t == NULL) {
				dom_node_unref(parent);
				return false;
			}
			free(nscss_sibs);
			nscss_sibs = t;
			nscss_sibs_size = size;
			nscss_sibs_used = 0;
			nscss_sibs_gen = nscss_dom_gen;
		}
	}
	k = 0;
	for (dom_node_get_first_child(parent, &c); c != NULL; c = next) {
		if (node_count_siblings_check(c, false, NULL)) {
			e = nscss_sib_slot(c);
			e->node = c;
			e->gen = nscss_sibs_gen;
			e->before = k;
			e->after = total - k - 1;
			k++;
			nscss_sibs_used++;
		}
		next = NULL;
		dom_node_get_next_sibling(c, &next);
		dom_node_unref(c);
	}
	dom_node_unref(parent);
	e = nscss_sib_slot(n);
	if (e->gen != nscss_sibs_gen)
		return false;
	*count = after ? e->after : e->before;
	return true;
}

css_error node_count_siblings(void *pw, void *n, bool same_name,
		bool after, int32_t *count)
{
	NSCSS_STRUCT(n, NSCSS_STRUCT_SELF);	/* (Onyx) */
	int32_t cnt = 0;
	dom_exception exc;
	dom_string *node_name = NULL;

	if (!same_name && nscss_sib_count(n, after, count))
		return CSS_OK;		/* (Onyx) */

	if (same_name) {
		dom_node *node = n;
		exc = dom_node_get_node_name(node, &node_name);
		if ((exc != DOM_NO_ERR) || (node_name == NULL)) {
			return CSS_NOMEM;
		}
	}

	if (after) {
		dom_node *node = dom_node_ref(n);
		dom_node *next;

		do {
			exc = dom_node_get_next_sibling(node, &next);
			if ((exc != DOM_NO_ERR))
				break;

			dom_node_unref(node);
			node = next;

			cnt += node_count_siblings_check(node, same_name, node_name);
		} while (node != NULL);
	} else {
		dom_node *node = dom_node_ref(n);
		dom_node *next;

		do {
			exc = dom_node_get_previous_sibling(node, &next);
			if ((exc != DOM_NO_ERR))
				break;

			dom_node_unref(node);
			node = next;

			cnt += node_count_siblings_check(node, same_name, node_name);

		} while (node != NULL);
	}

	if (node_name != NULL) {
		dom_string_unref(node_name);
	}

	*count = cnt;
	return CSS_OK;
}

/**
 * Callback to determine if a node is empty.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param match  Pointer to location to receive result
 * \return CSS_OK.
 *
 * \post \a match will contain true if the node is empty and false otherwise.
 */
css_error node_is_empty(void *pw, void *node, bool *match)
{
	NSCSS_STRUCT(node, NSCSS_STRUCT_SELF);	/* (Onyx) */
	dom_node *n = node, *next;
	dom_exception err;

	*match = true;

	err = dom_node_get_first_child(n, &n);
	if (err != DOM_NO_ERR) {
		return CSS_BADPARM;
	}

	while (n != NULL) {
		dom_node_type ntype;
		err = dom_node_get_node_type(n, &ntype);
		if (err != DOM_NO_ERR) {
			dom_node_unref(n);
			return CSS_BADPARM;
		}

		if (ntype == DOM_ELEMENT_NODE ||
		    ntype == DOM_TEXT_NODE) {
			*match = false;
			dom_node_unref(n);
			break;
		}

		err = dom_node_get_next_sibling(n, &next);
		if (err != DOM_NO_ERR) {
			dom_node_unref(n);
			return CSS_BADPARM;
		}
		dom_node_unref(n);
		n = next;
	}

	return CSS_OK;
}

/**
 * Callback to determine if a node is a linking element.
 *
 * \param pw     HTML document
 * \param n      DOM node
 * \param match  Pointer to location to receive result
 * \return CSS_OK.
 *
 * \post \a match will contain true if the node matches and false otherwise.
 */
css_error node_is_link(void *pw, void *n, bool *match)
{
	dom_node *node = n;
	dom_exception exc;
	dom_string *node_name = NULL;

	exc = dom_node_get_node_name(node, &node_name);
	if ((exc != DOM_NO_ERR) || (node_name == NULL)) {
		return CSS_NOMEM;
	}

	if (dom_string_caseless_lwc_isequal(node_name, corestring_lwc_a)) {
		bool has_href;
		exc = dom_element_has_attribute(node, corestring_dom_href,
				&has_href);
		if ((exc == DOM_NO_ERR) && (has_href)) {
			*match = true;
		} else {
			*match = false;
		}
	} else {
		*match = false;
	}
	dom_string_unref(node_name);

	return CSS_OK;
}

/**
 * Callback to determine if a node is a linking element whose target has been
 * visited.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param match  Pointer to location to receive result
 * \return CSS_OK.
 *
 * \post \a match will contain true if the node matches and false otherwise.
 */
css_error node_is_visited(void *pw, void *node, bool *match)
{
	nscss_select_ctx *ctx = pw;
	nsurl *url;
	nserror error;
	const struct url_data *data;

	dom_exception exc;
	dom_node *n = node;
	dom_string *s = NULL;

	*match = false;

	exc = dom_node_get_node_name(n, &s);
	if ((exc != DOM_NO_ERR) || (s == NULL)) {
		return CSS_NOMEM;
	}

	if (!dom_string_caseless_lwc_isequal(s, corestring_lwc_a)) {
		/* Can't be visited; not ancher element */
		dom_string_unref(s);
		return CSS_OK;
	}

	/* Finished with node name string */
	dom_string_unref(s);
	s = NULL;

	exc = dom_element_get_attribute(n, corestring_dom_href, &s);
	if ((exc != DOM_NO_ERR) || (s == NULL)) {
		/* Can't be visited; not got a URL */
		return CSS_OK;
	}

	/* Make href absolute */
	/* TODO: this duplicates what we do for box->href
	 *       should we put the absolute URL on the dom node? */
	error = nsurl_join(ctx->base_url, dom_string_data(s), &url);

	/* Finished with href string */
	dom_string_unref(s);

	if (error != NSERROR_OK) {
		/* Couldn't make nsurl object */
		return CSS_NOMEM;
	}

	data = urldb_get_url_data(url);

	/* Visited if in the db and has
	 * non-zero visit count */
	if (data != NULL && data->visits > 0)
		*match = true;

	nsurl_unref(url);

	/* (Onyx: a kept selection depends on it: html/onyx_restyle.c) */
	if (node != (void *) nscss_styled_node)
		nscss_visited_seen = 3;		/* (an ancestor's: not kept) */
	else if (nscss_visited_seen != 3)
		nscss_visited_seen = *match ? 2 : 1;

	return CSS_OK;
}

/**
 * Callback to determine if a node is currently being hovered over.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param match  Pointer to location to receive result
 * \return CSS_OK.
 *
 * \post \a match will contain true if the node matches and false otherwise.
 */
struct dom_node *nscss_hover_node = NULL;	/* (Onyx: see select.h) */
bool nscss_hover_used = false;
void (*nscss_hover_note)(void *ctx, struct dom_node *tested, struct dom_node *styled);
void *nscss_hover_note_ctx;
struct dom_node *nscss_styled_node;
unsigned int nscss_struct_used;	/* (Onyx: see select.h) */
int nscss_visited_seen;		/* (Onyx: see select.h) */

css_error node_is_hover(void *pw, void *node, bool *match)
{
	/* Onyx: the node under the pointer and its ancestors are hovered (its shadow
	 * hosts' too: the shadow-including ancestors) */
	dom_node *n = nscss_hover_node;

	(void) pw;
	nscss_hover_used = true;
	if (nscss_hover_note != NULL)
		nscss_hover_note(nscss_hover_note_ctx, node, nscss_styled_node);
	*match = false;
	if (n == NULL)
		return CSS_OK;
	dom_node_ref(n);
	while (n != NULL) {
		dom_node *parent = NULL;
		if (n == (dom_node *) node) {
			*match = true;
			dom_node_unref(n);
			break;
		}
		if (dom_node_get_parent_node(n, &parent) != DOM_NO_ERR)
			parent = NULL;
		dom_node_unref(n);
		n = parent;
		if (n != NULL) {
			dom_node *host = dom_onyx_shadow_host(n);
			if (host != NULL) {
				dom_node_unref(n);
				n = dom_node_ref(host);
			}
		}
	}

	return CSS_OK;
}

/**
 * Callback to determine if a node is currently activated.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param match  Pointer to location to receive result
 * \return CSS_OK.
 *
 * \post \a match will contain true if the node matches and false otherwise.
 */
css_error node_is_active(void *pw, void *node, bool *match)
{
	/** \todo Support active nodes */

	*match = false;

	return CSS_OK;
}

/**
 * Callback to determine if a node has the input focus.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param match  Pointer to location to receive result
 * \return CSS_OK.
 *
 * \post \a match will contain true if the node matches and false otherwise.
 */
css_error node_is_focus(void *pw, void *node, bool *match)
{
	/** \todo Support focussed nodes */

	*match = false;

	return CSS_OK;
}

/**
 * Callback to determine if a node is enabled.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param match  Pointer to location to receive result
 * \return CSS_OK.
 *
 * \post \a match with contain true if the node is enabled and false otherwise.
 */
css_error node_is_enabled(void *pw, void *node, bool *match)
{
	/** \todo Support enabled nodes */

	*match = false;

	return CSS_OK;
}

/**
 * Callback to determine if a node is disabled.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param match  Pointer to location to receive result
 * \return CSS_OK.
 *
 * \post \a match with contain true if the node is disabled and false otherwise.
 */
css_error node_is_disabled(void *pw, void *node, bool *match)
{
	/** \todo Support disabled nodes */

	*match = false;

	return CSS_OK;
}

/**
 * Callback to determine if a node is checked.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param match  Pointer to location to receive result
 * \return CSS_OK.
 *
 * \post \a match with contain true if the node is checked and false otherwise.
 */
css_error node_is_checked(void *pw, void *node, bool *match)
{
	/** \todo Support checked nodes */

	*match = false;

	return CSS_OK;
}

/**
 * Callback to determine if a node is the target of the document URL.
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param match  Pointer to location to receive result
 * \return CSS_OK.
 *
 * \post \a match with contain true if the node matches and false otherwise.
 */
css_error node_is_target(void *pw, void *node, bool *match)
{
	/** \todo Support target */

	*match = false;

	return CSS_OK;
}

/**
 * Callback to determine if a node has the given language
 *
 * \param pw     HTML document
 * \param node   DOM node
 * \param lang   Language specifier to match
 * \param match  Pointer to location to receive result
 * \return CSS_OK.
 *
 * \post \a match will contain true if the node matches and false otherwise.
 */
css_error node_is_lang(void *pw, void *node,
		lwc_string *lang, bool *match)
{
	/** \todo Support languages */

	*match = false;

	return CSS_OK;
}

/**
 * Callback to retrieve the User-Agent defaults for a CSS property.
 *
 * \param pw        HTML document
 * \param property  Property to retrieve defaults for
 * \param hint      Pointer to hint object to populate
 * \return CSS_OK       on success,
 *         CSS_INVALID  if the property should not have a user-agent default.
 */
css_error ua_default_for_property(void *pw, uint32_t property, css_hint *hint)
{
	if (property == CSS_PROP_COLOR) {
		hint->data.color = 0xff000000;
		hint->status = CSS_COLOR_COLOR;
	} else if (property == CSS_PROP_FONT_FAMILY) {
		hint->data.strings = NULL;
		switch (nsoption_int(font_default)) {
		case PLOT_FONT_FAMILY_SANS_SERIF:
			hint->status = CSS_FONT_FAMILY_SANS_SERIF;
			break;
		case PLOT_FONT_FAMILY_SERIF:
			hint->status = CSS_FONT_FAMILY_SERIF;
			break;
		case PLOT_FONT_FAMILY_MONOSPACE:
			hint->status = CSS_FONT_FAMILY_MONOSPACE;
			break;
		case PLOT_FONT_FAMILY_CURSIVE:
			hint->status = CSS_FONT_FAMILY_CURSIVE;
			break;
		case PLOT_FONT_FAMILY_FANTASY:
			hint->status = CSS_FONT_FAMILY_FANTASY;
			break;
		}
	} else if (property == CSS_PROP_QUOTES) {
		/** \todo Not exactly useful :) */
		hint->data.strings = NULL;
		hint->status = CSS_QUOTES_NONE;
	} else if (property == CSS_PROP_VOICE_FAMILY) {
		/** \todo Fix this when we have voice-family done */
		hint->data.strings = NULL;
		hint->status = 0;
	} else {
		return CSS_INVALID;
	}

	return CSS_OK;
}

css_error set_libcss_node_data(void *pw, void *node, void *libcss_node_data)
{
	dom_node *n = node;
	dom_exception err;
	void *old_node_data;

	/* Set this node's node data */
	err = dom_node_set_user_data(n,
			corestring_dom___ns_key_libcss_node_data,
			libcss_node_data, nscss_dom_user_data_handler,
			(void *) &old_node_data);
	if (err != DOM_NO_ERR) {
		return CSS_NOMEM;
	}

	assert(old_node_data == NULL);

	return CSS_OK;
}

css_error get_libcss_node_data(void *pw, void *node, void **libcss_node_data)
{
	dom_node *n = node;
	dom_exception err;

	/* Get this node's node data */
	err = dom_node_get_user_data(n,
			corestring_dom___ns_key_libcss_node_data,
			libcss_node_data);
	if (err != DOM_NO_ERR) {
		return CSS_NOMEM;
	}

	return CSS_OK;
}


/* ---- Onyx: shadow DOM -- the handler for the documents with shadow trees -------------- */

/* The node is the shadow host of the tree whose rules are matched: featureless there */
#define ONYX_FL(pw, node) (((nscss_select_ctx *) (pw))->scope_host != NULL && \
		(void *) (node) == (void *) ((nscss_select_ctx *) (pw))->scope_host)

/* the parent in the tree matched: a shadow tree's top-level elements' is its host (whose
 * own parent is none there) */
static css_error s_parent_node(void *pw, void *node, void **parent)
{
	nscss_select_ctx *ctx = pw;

	*parent = NULL;
	if (ONYX_FL(pw, node))
		return CSS_OK;
	parent_node(pw, node, parent);
	if (*parent == NULL && ctx->scope_host != NULL) {
		dom_node *p = NULL;
		if (dom_node_get_parent_node(node, &p) == DOM_NO_ERR && p != NULL) {
			if (dom_onyx_shadow_host(p) == ctx->scope_host)
				*parent = ctx->scope_host;
			dom_node_unref(p);
		}
	}
	return CSS_OK;
}

static bool s_named(void *node, const css_qname *qname)
{
	dom_string *name = NULL;
	bool m = false;
	if (dom_node_get_node_name(node, &name) == DOM_NO_ERR && name != NULL) {
		m = dom_string_caseless_lwc_isequal(name, qname->name);
		dom_string_unref(name);
	}
	return m;
}

static css_error s_named_ancestor_node(void *pw, void *node,
		const css_qname *qname, void **ancestor)
{
	void *n = node;

	*ancestor = NULL;
	if (((nscss_select_ctx *) pw)->scope_host == NULL)
		return named_ancestor_node(pw, node, qname, ancestor);
	for (;;) {
		s_parent_node(pw, n, &n);
		if (n == NULL || ONYX_FL(pw, n))
			return CSS_OK;
		if (s_named(n, qname)) {
			*ancestor = n;
			return CSS_OK;
		}
	}
}

static css_error s_named_parent_node(void *pw, void *node,
		const css_qname *qname, void **parent)
{
	void *p = NULL;

	*parent = NULL;
	s_parent_node(pw, node, &p);
	if (p != NULL && !ONYX_FL(pw, p) && s_named(p, qname))
		*parent = p;
	return CSS_OK;
}

static css_error s_named_sibling_node(void *pw, void *node,
		const css_qname *qname, void **sibling)
{
	NSCSS_STRUCT(node, NSCSS_STRUCT_SIB);	/* (Onyx) */
	*sibling = NULL;
	if (ONYX_FL(pw, node))
		return CSS_OK;
	return named_sibling_node(pw, node, qname, sibling);
}

static css_error s_named_generic_sibling_node(void *pw, void *node,
		const css_qname *qname, void **sibling)
{
	if (qname != nscss_state_qname)
		NSCSS_STRUCT(node, NSCSS_STRUCT_SIB);	/* (Onyx) */
	*sibling = NULL;
	if (ONYX_FL(pw, node))
		return CSS_OK;
	return named_generic_sibling_node(pw, node, qname, sibling);
}

static css_error s_sibling_node(void *pw, void *node, void **sibling)
{
	NSCSS_STRUCT(node, NSCSS_STRUCT_SIB);	/* (Onyx) */
	*sibling = NULL;
	if (ONYX_FL(pw, node))
		return CSS_OK;
	return sibling_node(pw, node, sibling);
}

#define S_MATCH1(fn) \
static css_error s_##fn(void *pw, void *node, bool *match) \
{ \
	if (ONYX_FL(pw, node)) { *match = false; return CSS_OK; } \
	return fn(pw, node, match); \
}
#define S_MATCH_Q(fn) \
static css_error s_##fn(void *pw, void *node, const css_qname *qname, bool *match) \
{ \
	if (ONYX_FL(pw, node)) { *match = false; return CSS_OK; } \
	return fn(pw, node, qname, match); \
}
#define S_MATCH_QV(fn) \
static css_error s_##fn(void *pw, void *node, const css_qname *qname, \
		lwc_string *value, bool *match) \
{ \
	if (ONYX_FL(pw, node)) { *match = false; return CSS_OK; } \
	return fn(pw, node, qname, value, match); \
}
#define S_MATCH_S(fn) \
static css_error s_##fn(void *pw, void *node, lwc_string *name, bool *match) \
{ \
	if (ONYX_FL(pw, node)) { *match = false; return CSS_OK; } \
	return fn(pw, node, name, match); \
}

S_MATCH_Q(node_has_name)
S_MATCH_S(node_has_class)
S_MATCH_S(node_has_id)
S_MATCH_Q(node_has_attribute)
S_MATCH_QV(node_has_attribute_equal)
S_MATCH_QV(node_has_attribute_dashmatch)
S_MATCH_QV(node_has_attribute_includes)
S_MATCH_QV(node_has_attribute_prefix)
S_MATCH_QV(node_has_attribute_suffix)
S_MATCH_QV(node_has_attribute_substring)
S_MATCH1(node_is_root)
S_MATCH1(node_is_empty)
S_MATCH1(node_is_link)
S_MATCH1(node_is_visited)
S_MATCH1(node_is_hover)
S_MATCH1(node_is_active)
S_MATCH1(node_is_focus)
S_MATCH1(node_is_enabled)
S_MATCH1(node_is_disabled)
S_MATCH1(node_is_checked)
S_MATCH1(node_is_target)
S_MATCH_S(node_is_lang)

static css_error s_node_count_siblings(void *pw, void *n, bool same_name,
		bool after, int32_t *count)
{
	NSCSS_STRUCT(n, NSCSS_STRUCT_SELF);	/* (Onyx) */
	*count = 0;
	if (ONYX_FL(pw, n))
		return CSS_OK;
	return node_count_siblings(pw, n, same_name, after, count);
}

static css_error s_onyx_node_is_scope_host(void *pw, void *node, bool *match)
{
	*match = ONYX_FL(pw, node);
	return CSS_OK;
}

static void *s_onyx_host_pw(void *pw)
{
	return ((nscss_select_ctx *) pw)->host_ctx;
}

static css_error s_node_onyx_state(void *pw, void *node, lwc_string *state, bool *match)
{
	*match = false;
	if (ONYX_FL(pw, node))
		return CSS_OK;
	return node_onyx_state(pw, node, state, match);
}

static css_select_handler onyx_scoped_handler = {
	CSS_SELECT_HANDLER_VERSION_1,

	node_name,
	node_classes,
	node_id,
	s_named_ancestor_node,
	s_named_parent_node,
	s_named_sibling_node,
	s_named_generic_sibling_node,
	s_parent_node,
	s_sibling_node,
	s_node_has_name,
	s_node_has_class,
	s_node_has_id,
	s_node_has_attribute,
	s_node_has_attribute_equal,
	s_node_has_attribute_dashmatch,
	s_node_has_attribute_includes,
	s_node_has_attribute_prefix,
	s_node_has_attribute_suffix,
	s_node_has_attribute_substring,
	s_node_is_root,
	s_node_count_siblings,
	s_node_is_empty,
	s_node_is_link,
	s_node_is_visited,
	s_node_is_hover,
	s_node_is_active,
	s_node_is_focus,
	s_node_is_enabled,
	s_node_is_disabled,
	s_node_is_checked,
	s_node_is_target,
	s_node_is_lang,
	node_presentational_hint,
	ua_default_for_property,
	set_libcss_node_data,
	get_libcss_node_data,
	s_onyx_node_is_scope_host,
	s_onyx_host_pw,
	s_node_onyx_state,
};
