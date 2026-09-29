/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: CSS custom properties (--name: value) and var() -- the parsing side
 * (src/parse/onyx_vars.c). The selection side, which substitutes the variables when an
 * element's cascade is known, is src/select/onyx_vars.c.
 *
 * Two pseudo-opcodes, outside the property range (so never in prop_dispatch):
 *
 *   CSS_ONYX_OP_CUSTOM  OPV(flags), name, value
 *       a custom property declaration: its name ("--foo") and its value's text (string
 *       indices in the sheet)
 *
 *   CSS_ONYX_OP_VAR     OPV(flags, n), text, property, longhand[n]
 *       a declaration whose value uses var(): the value's text, the property (its index
 *       from FIRST_PROP in the propstrings), and the n longhand properties it sets (a
 *       shorthand sets several), whose values are pending until the cascade substitutes
 *       the variables and parses the text.
 */

#ifndef css_css__parse_onyx_vars_h_
#define css_css__parse_onyx_vars_h_

#include <parserutils/utils/vector.h>

#include "stylesheet.h"
#include "parse/language.h"
#include "parse/properties/properties.h"

#define CSS_ONYX_OP_CUSTOM	0x3f0
#define CSS_ONYX_OP_VAR		0x3f1

/** Is the name a custom property's, "--" something? */
bool css__onyx_is_custom_name(lwc_string *name);

/** Does the value at ctx (to the end of the vector) use var()? */
bool css__onyx_value_has_var(css_language *c, const parserutils_vector *vector,
		int32_t ctx);

/** Parse a custom property declaration (--name: value) into the rule. */
css_error css__onyx_parse_custom(css_language *c, const css_token *property,
		const parserutils_vector *vector, int32_t *ctx, css_rule *rule);

/** Parse a declaration using var() (property index i from FIRST_PROP, its handler) into
 * the rule: kept as text, with the longhands it sets. */
css_error css__onyx_parse_var_decl(css_language *c, int i, css_prop_handler handler,
		const parserutils_vector *vector, int32_t *ctx, css_rule *rule);

/** The text of the tokens [start, end) of the vector, as CSS source that lexes back to
 * them (whitespace collapsed, trimmed). Returns a new reference. */
css_error css__onyx_tokens_to_text(const parserutils_vector *vector, int32_t start,
		int32_t end, lwc_string **text);

#endif
