/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef libcss_select_h_
#define libcss_select_h_

#ifdef __cplusplus
extern "C"
{
#endif

#include <libcss/errors.h>
#include <libcss/functypes.h>
#include <libcss/hint.h>
#include <libcss/types.h>
#include <libcss/computed.h>
#include <libcss/unit.h>

typedef enum css_pseudo_element {
	CSS_PSEUDO_ELEMENT_NONE         = 0,
	CSS_PSEUDO_ELEMENT_FIRST_LINE   = 1,
	CSS_PSEUDO_ELEMENT_FIRST_LETTER = 2,
	CSS_PSEUDO_ELEMENT_BEFORE       = 3,
	CSS_PSEUDO_ELEMENT_AFTER        = 4,

	CSS_PSEUDO_ELEMENT_COUNT	= 5	/**< Number of pseudo elements */
} css_pseudo_element;

/**
 * Style selection result set
 */
typedef struct css_select_results {
	/**
	 * Array of pointers to computed styles,
	 * indexed by css_pseudo_element. If there
	 * was no styling for a given pseudo element,
	 * then no computed style will be created and
	 * the corresponding pointer will be set to NULL
	 */
	css_computed_style *styles[CSS_PSEUDO_ELEMENT_COUNT];
} css_select_results;

typedef enum css_select_handler_version {
	CSS_SELECT_HANDLER_VERSION_1 = 1
} css_select_handler_version;

typedef struct css_select_handler {
	/** ABI version of this structure */
	uint32_t handler_version;

	css_error (*node_name)(void *pw, void *node,
			css_qname *qname);
	css_error (*node_classes)(void *pw, void *node,
			lwc_string ***classes,
			uint32_t *n_classes);
	css_error (*node_id)(void *pw, void *node,
			lwc_string **id);

	css_error (*named_ancestor_node)(void *pw, void *node,
			const css_qname *qname, void **ancestor);
	css_error (*named_parent_node)(void *pw, void *node,
			const css_qname *qname, void **parent);
	css_error (*named_sibling_node)(void *pw, void *node,
			const css_qname *qname, void **sibling);
	css_error (*named_generic_sibling_node)(void *pw, void *node,
			const css_qname *qname, void **sibling);

	css_error (*parent_node)(void *pw, void *node, void **parent);
	css_error (*sibling_node)(void *pw, void *node, void **sibling);

	css_error (*node_has_name)(void *pw, void *node,
			const css_qname *qname, bool *match);
	css_error (*node_has_class)(void *pw, void *node,
			lwc_string *name, bool *match);
	css_error (*node_has_id)(void *pw, void *node,
			lwc_string *name, bool *match);
	css_error (*node_has_attribute)(void *pw, void *node,
			const css_qname *qname, bool *match);
	css_error (*node_has_attribute_equal)(void *pw, void *node,
			const css_qname *qname, lwc_string *value,
			bool *match);
	css_error (*node_has_attribute_dashmatch)(void *pw, void *node,
			const css_qname *qname, lwc_string *value,
			bool *match);
	css_error (*node_has_attribute_includes)(void *pw, void *node,
			const css_qname *qname, lwc_string *value,
			bool *match);
	css_error (*node_has_attribute_prefix)(void *pw, void *node,
			const css_qname *qname, lwc_string *value,
			bool *match);
	css_error (*node_has_attribute_suffix)(void *pw, void *node,
			const css_qname *qname, lwc_string *value,
			bool *match);
	css_error (*node_has_attribute_substring)(void *pw, void *node,
			const css_qname *qname, lwc_string *value,
			bool *match);

	css_error (*node_is_root)(void *pw, void *node, bool *match);
	css_error (*node_count_siblings)(void *pw, void *node,
			bool same_name, bool after, int32_t *count);
	css_error (*node_is_empty)(void *pw, void *node, bool *match);

	css_error (*node_is_link)(void *pw, void *node, bool *match);
	css_error (*node_is_visited)(void *pw, void *node, bool *match);
	css_error (*node_is_hover)(void *pw, void *node, bool *match);
	css_error (*node_is_active)(void *pw, void *node, bool *match);
	css_error (*node_is_focus)(void *pw, void *node, bool *match);

	css_error (*node_is_enabled)(void *pw, void *node, bool *match);
	css_error (*node_is_disabled)(void *pw, void *node, bool *match);
	css_error (*node_is_checked)(void *pw, void *node, bool *match);

	css_error (*node_is_target)(void *pw, void *node, bool *match);
	css_error (*node_is_lang)(void *pw, void *node,
			lwc_string *lang, bool *match);

	css_error (*node_presentational_hint)(void *pw, void *node,
			uint32_t *nhints, css_hint **hints);

	css_error (*ua_default_for_property)(void *pw, uint32_t property,
			css_hint *hint);

	/**
	 * Set libcss_node_data on a DOM node.
	 *
	 * Replaces any existing libcss_node_data.  If node is deleted, cloned,
	 * or its ancestors are modified, call css_libcss_node_data_handler for
	 * any non-NULL libcss_node_data.
	 *
	 * \param pw			Client data
	 * \param node			DOM node to set data for
	 * \param libcss_node_data	Data to set on node, or NULL
	 * \return CSS_OK on success, or appropriate error otherwise
	 */
	css_error (*set_libcss_node_data)(void *pw, void *node,
			void *libcss_node_data);
	/**
	 * Get libcss_node_data from a DOM node.
	 *
	 * \param pw			Client data
	 * \param node			DOM node to get data from
	 * \param libcss_node_data	Updated to node data, else set to NULL.
	 * \return CSS_OK on success, or appropriate error otherwise
	 */
	css_error (*get_libcss_node_data)(void *pw, void *node,
			void **libcss_node_data);

	/* Onyx: shadow trees -- appended, NULL in a handler without them (:host then
	 * never matches) */
	/**
	 * Whether node is the shadow host of the tree whose rules pw matches: the host is
	 * featureless there (the handler answers no to its name, classes, attributes...;
	 * its parent is NULL) but for :host, :host(), :host-context().
	 */
	css_error (*onyx_node_is_scope_host)(void *pw, void *node, bool *match);
	/**
	 * The handler data matching in the tree of pw's shadow host (its own tree, where it
	 * has its features: :host()'s argument), or NULL.
	 */
	void *(*onyx_host_pw)(void *pw);
} css_select_handler;

/**
 * Onyx: shadow DOM -- the rules of another tree a node's style also takes (CSS Scoping)
 */
typedef enum css_select_onyx_scope_kind {
	CSS_ONYX_SCOPE_HOST = 0,	/**< a shadow tree's :host rules; node: the host */
	CSS_ONYX_SCOPE_SLOTTED = 1,	/**< a shadow tree's ::slotted() rules; node: the
					 * slot the element is assigned to */
	CSS_ONYX_SCOPE_PART = 2		/**< an outer tree's ::part() rules; node: the host
					 * whose shadow tree holds the element */
} css_select_onyx_scope_kind;

typedef struct css_select_onyx_scope {
	css_select_ctx *ctx;	/**< that tree's style sheets (the author ones apply) */
	void *pw;		/**< handler data matching in that tree */
	void *node;		/**< the node its selectors are matched on */
	uint8_t kind;		/**< css_select_onyx_scope_kind */
	int8_t level;		/**< its encapsulation context against the element's own
				 * tree (0): < 0 outer (its normal declarations win over
				 * the element's tree's, its !important ones lose), > 0
				 * inner (the other way round) */
	lwc_string *const *parts;	/**< PART: the element's part names */
	uint32_t n_parts;
} css_select_onyx_scope;

/**
 * Font face selection result set
 */
typedef struct css_select_font_faces_results {
	/**
	 * Array of pointers to computed font faces.
	 */
	css_font_face **font_faces;
	uint32_t n_font_faces;
} css_select_font_faces_results;

typedef enum {
	CSS_NODE_DELETED,
	CSS_NODE_MODIFIED,
	CSS_NODE_ANCESTORS_MODIFIED,
	CSS_NODE_CLONED
} css_node_data_action;

/**
 * Handle libcss_node_data on DOM changes/deletion.
 *
 * When a DOM node is deleted, if it has libcss_node_data, call with
 * action CSS_NODE_DELETED, to ensure the libcss_node_data is not leaked.
 * Does not call handler->set_libcss_node_data.
 *
 * When a DOM node is modified, if the node has libcss_node_data,
 * call with CSS_NODE_MODIFIED.  This will result in a call to
 * handler->set_libcss_node_data for the node.
 *
 * When a DOM node's ancestors are modified, if the node has libcss_node_data,
 * call with CSS_NODE_ANCESTORS_MODIFIED.  This will result in a call to
 * handler->set_libcss_node_data for the node.
 *
 * When a DOM node with libcss_node_data is cloned, and its ancestors are
 * also clones, call with CSS_NODE_CLONED.  This will result in a call to
 * handler->set_libcss_node_data for the clone node.
 *
 * \param handler		Selection handler vtable
 * \param action		Type of node action.
 * \param pw			Client data
 * \param node			DOM node to get data from
 * \param clone_node		Clone node, or NULL
 * \param libcss_node_data	Node data (non-NULL)
 * \return CSS_OK on success, or appropriate error otherwise
 */
css_error css_libcss_node_data_handler(css_select_handler *handler,
		css_node_data_action action, void *pw, void *node,
		void *clone_node, void *libcss_node_data);

/**
 * Onyx: a custom property's value on an element (its var() already substituted), from
 * the node data its selection stored; NULL if it has none. The caller unrefs it.
 * (Inline <svg>: `fill: var(--x)` resolved when the SVG text is made.)
 */
lwc_string *css_onyx_node_var(void *libcss_node_data, const char *name, size_t len);

/**
 * Onyx: the custom properties an element's selection stored in its node data (its own
 * and those it inherits): an opaque handle with a reference -- the same handle as long as
 * they are the same (NetSurf's kept selections compare their parent's); NULL if none.
 * css_onyx_vars_release drops the reference.
 */
const void *css_onyx_node_vars_ref(void *libcss_node_data);
void css_onyx_vars_release(const void *vars);
/** Onyx: whether two such handles hold the same custom properties (in the same order) */
bool css_onyx_vars_same(const void *a, const void *b);

css_error css_select_ctx_create(css_select_ctx **result);
css_error css_select_ctx_destroy(css_select_ctx *ctx);

css_error css_select_ctx_append_sheet(css_select_ctx *ctx,
		const css_stylesheet *sheet,
		css_origin origin, const char *media);
css_error css_select_ctx_insert_sheet(css_select_ctx *ctx,
		const css_stylesheet *sheet, uint32_t index,
		css_origin origin, const char *media);
css_error css_select_ctx_remove_sheet(css_select_ctx *ctx,
		const css_stylesheet *sheet);

/*
 * Onyx: a @keyframes' keyframes (NetSurf's animations): the last @keyframes <name> of the
 * context's sheets (their imports, @media / @supports / @layer groups too), one entry per
 * keyframe selector, sorted by offset (0..1; equal offsets in source order); decls the
 * keyframe's declarations (css_computed_style_onyx_apply), alive as long as the sheet. The
 * array is malloc'd (the caller frees it); *n 0 when there is no such @keyframes.
 */
typedef struct css_onyx_keyframe {
	float offset;
	uint32_t order;
	const void *decls;
} css_onyx_keyframe;
css_error css_select_ctx_onyx_keyframes(const css_select_ctx *ctx, lwc_string *name,
		css_onyx_keyframe **out, uint32_t *n);
/* Onyx: an inline sheet's declarations (element.animate()'s keyframes), or NULL */
const void *css_stylesheet_onyx_inline_decls(const css_stylesheet *sheet);

css_error css_select_ctx_count_sheets(css_select_ctx *ctx, uint32_t *count);
css_error css_select_ctx_get_sheet(css_select_ctx *ctx, uint32_t index,
		const css_stylesheet **sheet);

css_error css_select_default_style(css_select_ctx *ctx,
		css_select_handler *handler, void *pw,
		css_computed_style **style);
css_error css_select_style(css_select_ctx *ctx, void *node,
		const css_unit_ctx *unit_ctx,
		const css_media *media, const css_stylesheet *inline_style,
		css_select_handler *handler, void *pw,
		css_select_results **result);
/**
 * Onyx: whether any selector of the context's sheets matches the element (or one of its
 * pseudo-elements) in the media -- NetSurf's kept selections after sheets were added:
 * the context holds the new sheets only. The node's own node data is left as it is.
 */
css_error css_select_style_onyx_probe(css_select_ctx *ctx, void *node,
		const css_unit_ctx *unit_ctx, const css_media *media,
		css_select_handler *handler, void *pw, bool *matched);
/**
 * Onyx: css_select_style for an element of a document with shadow trees: ctx and pw are
 * its own tree's; inherit_parent the element whose custom properties it inherits (its
 * flat tree parent: a slot, a host; NULL: the handler's parent_node); no_share: its style
 * is its own (a host, an element assigned to a slot...); scopes: the other trees' rules
 * it takes.
 */
css_error css_select_style_onyx(css_select_ctx *ctx, void *node,
		const css_unit_ctx *unit_ctx,
		const css_media *media, const css_stylesheet *inline_style,
		css_select_handler *handler, void *pw,
		void *inherit_parent, bool no_share,
		const css_select_onyx_scope *scopes, uint32_t n_scopes,
		css_select_results **result);
css_error css_select_results_destroy(css_select_results *results);

css_error css_select_font_faces(css_select_ctx *ctx,
		const css_media *media,
		const css_unit_ctx *unit_ctx,
		lwc_string *font_family,
		css_select_font_faces_results **result);
css_error css_select_font_faces_results_destroy(
		css_select_font_faces_results *results);

#ifdef __cplusplus
}
#endif

#endif
