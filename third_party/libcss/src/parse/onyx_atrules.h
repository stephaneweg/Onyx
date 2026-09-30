/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: @supports, @layer, @container (src/parse/onyx_atrules.c).
 */

#ifndef css_css__parse_onyx_atrules_h_
#define css_css__parse_onyx_atrules_h_

#include <parserutils/utils/vector.h>

#include "parse/language.h"
#include "parse/mq.h"

/* Is "property: value" (the value from ctx to the end of the vector) a declaration the
 * parser keeps? (language.c) */
bool css__onyx_declaration_valid(css_language *c, const css_token *property,
		const parserutils_vector *vector, int32_t ctx);

/* Do the tokens [start, end) of the vector make a valid selector list? (language.c) */
bool css__onyx_selector_list_valid(css_language *c, const parserutils_vector *vector,
		int32_t start, int32_t end);

/** Does the @supports condition at *ctx (to the end of the vector) hold? */
bool css__onyx_supports(css_language *c, const parserutils_vector *vector, int32_t *ctx);

/** An @container's query (after its optional name), parsed as a media query list. */
css_error css__onyx_container_media(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_mq_query **media);

/* The at-rules libcss keeps without applying their content (a media rule whose media never
 * matches, its onyx_kind one of these): what the CSSOM, CSS.supports and the page's scripts
 * see of them. */
enum onyx_at_kind {
	ONYX_AT_NONE = 0,
	ONYX_AT_KEYFRAMES,		/* @keyframes <name> { <keyframe>* } */
	ONYX_AT_KEYFRAME,		/* from, 50% { <declarations> } */
	ONYX_AT_COUNTER_STYLE,		/* @counter-style <name> { <descriptors> } */
	ONYX_AT_PROPERTY,		/* @property --x { <descriptors> } */
	ONYX_AT_FONT_FEATURE_VALUES,	/* @font-feature-values <family># { @styleset {...} } */
	ONYX_AT_FFV_BLOCK,		/* @styleset { name: <integer>+ } ... */
	ONYX_AT_FONT_PALETTE_VALUES,	/* @font-palette-values --x { <descriptors> } */
	ONYX_AT_POSITION_TRY,		/* @position-try --x { <declarations> } */
	ONYX_AT_VIEW_TRANSITION,	/* @view-transition { <descriptors> } */
	ONYX_AT_SCOPE,			/* @scope [(<sel>)]? [to (<sel>)]? { <rules> } */
	ONYX_AT_STARTING_STYLE,		/* @starting-style { <rules> } */
	ONYX_AT_PAGE_MARGIN		/* @top-left ... in @page { <declarations> } */
};

/** The kind of the at-rule (its ATKEYWORD) nested in a rule of kind parent (ONYX_AT_NONE:
 * at the top level or in a media rule), or ONYX_AT_NONE if it is not one of them. */
int css__onyx_at_kind(const css_token *atkeyword, int parent);

/** Is the prelude (from ctx to the end of the vector) valid for an at-rule of that kind? */
bool css__onyx_at_prelude(css_language *c, int kind, const parserutils_vector *vector,
		int32_t ctx);

/** A declaration "name: value" (the vector) in an at-rule of that kind: is it valid? (the
 * at-rule's descriptor grammar, or the property's for @keyframes / @position-try) */
bool css__onyx_at_declaration(css_language *c, int kind, const parserutils_vector *vector);

/** A descriptor "name: value" (the vector) of @font-face or @page that libcss does not
 * parse itself: valid by the at-rule's descriptor grammar? (at: "@font-face", "@page") */
bool css__onyx_descriptor_valid(css_language *c, const char *at,
		const parserutils_vector *vector);

#endif
