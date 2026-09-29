/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: CSS custom properties and var() -- the selection side (src/select/onyx_vars.c).
 *
 * During an element's cascade, the custom property declarations that apply are ranked like
 * any property (origin, importance, specificity), and a declaration whose value uses var()
 * wins its longhands as "pending". When the cascade is done, the element's variables are
 * made -- its parent's (kept in the parent's node data), with its own on top -- and each
 * pending value has its variables substituted and is parsed with its property's parser,
 * then cascaded with the rank it won. A value that cannot be completed (an undefined
 * variable without a fallback, a result the property rejects) makes its longhands "unset".
 */

#ifndef css_select_onyx_vars_h_
#define css_select_onyx_vars_h_

#include <libcss/types.h>

#include "select/select.h"

/** An element's custom properties, their var() already substituted: shared (reference
 * counted) by the elements that inherit them unchanged. */
typedef struct css_onyx_var {
	lwc_string *name;		/**< "--foo" */
	lwc_string *value;		/**< its value's text */
} css_onyx_var;

typedef struct css_onyx_vars {
	uint32_t refcount;
	uint32_t n;
	css_onyx_var v[];
} css_onyx_vars;

css_onyx_vars *css__onyx_vars_ref(css_onyx_vars *vars);
void css__onyx_vars_unref(css_onyx_vars *vars);

/** A custom property declaration that applies to the element (its best one so far). */
typedef struct css_onyx_custom {
	lwc_string *name;
	lwc_string *value;		/**< its text, as declared */
	prop_state rank;
	uint8_t pseudo;
} css_onyx_custom;

/** A declaration using var() that won some longhands. */
typedef struct css_onyx_pending {
	const css_stylesheet *sheet;	/**< its sheet (url, colour and font resolvers) */
	lwc_string *text;		/**< its value's text, var() and all */
	uint16_t prop;			/**< its property, from FIRST_PROP */
	bool important;
	uint8_t origin;
	uint32_t specificity;
} css_onyx_pending;

/** The var() state of one selection (select state's `onyx`, made on first use). */
struct css_onyx_state {
	css_onyx_custom *custom;
	uint32_t n_custom, a_custom;
	css_onyx_pending *pending;
	uint32_t n_pending, a_pending;
	/** per longhand and pseudo element: 1 + its pending declaration, 0 if none */
	uint16_t pending_of[CSS_N_PROPERTIES][CSS_PSEUDO_ELEMENT_COUNT];
	/** while a pending declaration is resolved: 1 + its index (only its longhands take) */
	uint16_t filter;
};

/** Cascade a CSS_ONYX_OP_CUSTOM / CSS_ONYX_OP_VAR (opv read, style past it). */
css_error css__onyx_cascade(uint32_t opv, css_style *style, css_select_state *state);

/** At the end of the element's cascade: make its variables (stored in its node data) and
 * complete its pending values. */
css_error css__onyx_resolve(css_select_state *state, void *parent);

/** Drop the cache of parsed var() values (their small sheets): when a selection context
 * goes (the values are parsed again when needed). */
void css__onyx_parsed_flush(void);

/** Release a selection's var() state. */
void css__onyx_state_destroy(css_select_state *state);

#endif
