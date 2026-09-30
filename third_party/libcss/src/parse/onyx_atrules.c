/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: the CSS3 at-rules whose content applies like a media rule's:
 *
 *   @supports <condition> { ... }   the condition is evaluated -- each (property: value)
 *                                   by the property's own parser -- and the rules apply
 *                                   if it holds (else the block is dropped);
 *   @layer [names] { ... }          the rules apply (the layers' order of precedence is
 *                                   not modelled: the rules keep their source order);
 *   @container [name] <query> { }   the query is evaluated as a media query, against the
 *                                   viewport (an approximation: the container is taken as
 *                                   the viewport).
 *
 * language.c's handleStartAtRule makes them media rules (with the media "all" when the
 * content applies unconditionally).
 */

#include <string.h>
#include <strings.h>

#include "bytecode/bytecode.h"
#include "parse/onyx_atrules.h"
#include "parse/onyx_vars.h"
#include "parse/onyx_grammar.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

static bool onyx_is_ident(css_language *c, const css_token *t, int which)
{
	bool match;
	return t != NULL && t->type == CSS_TOKEN_IDENT &&
			lwc_string_caseless_isequal(t->idata, c->strings[which], &match) ==
				lwc_error_ok && match;
}

/* The index of the ')' closing the parenthesis opened just before `i` (or -1). */
static int32_t onyx_matching_paren(const parserutils_vector *vector, int32_t i)
{
	const css_token *t;
	int depth = 1;

	for (; (t = parserutils_vector_peek(vector, i)) != NULL; i++) {
		if (t->type == CSS_TOKEN_FUNCTION || tokenIsChar(t, '('))
			depth++;
		else if (tokenIsChar(t, ')') && --depth == 0)
			return i;
	}
	return -1;
}

/* Is "property: value" (tokens [s, e)) a declaration this parser keeps? The property's own
 * parser, else its grammar (parseProperty: css__onyx_declaration_valid). */
static bool onyx_supports_decl(css_language *c, const parserutils_vector *vector,
		int32_t s, int32_t e)
{
	const css_token *prop, *t;
	parserutils_vector *sub = NULL;
	int32_t ctx = s, sctx = 0, i;
	bool ok = false;

	consumeWhitespace(vector, &ctx);
	prop = parserutils_vector_iterate(vector, &ctx);
	if (prop == NULL || prop->type != CSS_TOKEN_IDENT || ctx > e)
		return false;
	consumeWhitespace(vector, &ctx);
	t = parserutils_vector_iterate(vector, &ctx);
	if (t == NULL || !tokenIsChar(t, ':') || ctx > e)
		return false;

	if (parserutils_vector_create(sizeof(css_token), 8, &sub) != PARSERUTILS_OK)
		return false;
	for (i = ctx; i < e; i++) {
		css_token tok = *((const css_token *) parserutils_vector_peek(vector, i));
		if (parserutils_vector_append(sub, &tok) != PARSERUTILS_OK)
			goto done;
	}
	consumeWhitespace(sub, &sctx);
	if (parserutils_vector_peek(sub, sctx) == NULL)
		goto done;			/* no value */
	ok = css__onyx_declaration_valid(c, prop, sub, sctx);

done:
	parserutils_vector_destroy(sub);
	return ok;
}

static bool onyx_supports_condition(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, bool *valid);

/* <supports-in-parens>: ( <condition> ) | ( <declaration> ) | selector(...) | other */
static bool onyx_supports_in_parens(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, bool *valid)
{
	const css_token *t;
	int32_t close, inner;
	bool r;

	consumeWhitespace(vector, ctx);
	t = parserutils_vector_iterate(vector, ctx);
	if (t == NULL) {
		*valid = false;
		return false;
	}
	if (t->type == CSS_TOKEN_FUNCTION) {
		/* selector(...): taken as supported; any other function: not */
		bool match;
		bool sel = lwc_string_caseless_isequal(t->idata,
				c->strings[FN_SELECTOR], &match) == lwc_error_ok && match;
		close = onyx_matching_paren(vector, *ctx);
		if (close < 0) {
			*valid = false;
			return false;
		}
		*ctx = close + 1;
		return sel;
	}
	if (!tokenIsChar(t, '(')) {
		*valid = false;
		return false;
	}
	close = onyx_matching_paren(vector, *ctx);
	if (close < 0) {
		*valid = false;
		return false;
	}

	/* a declaration: IDENT ws* ':' ... */
	inner = *ctx;
	consumeWhitespace(vector, &inner);
	t = parserutils_vector_peek(vector, inner);
	if (t != NULL && t->type == CSS_TOKEN_IDENT) {
		int32_t k = inner + 1;
		const css_token *colon;
		consumeWhitespace(vector, &k);
		colon = parserutils_vector_peek(vector, k);
		if (colon != NULL && tokenIsChar(colon, ':')) {
			r = onyx_supports_decl(c, vector, *ctx, close);
			*ctx = close + 1;
			return r;
		}
	}

	/* a nested condition */
	r = onyx_supports_condition(c, vector, ctx, valid);
	consumeWhitespace(vector, ctx);
	if (*ctx != close) {
		/* (anything else in parentheses: "general enclosed", false) */
		*ctx = close + 1;
		return false;
	}
	*ctx = close + 1;
	return r;
}

/* <supports-condition>: not <in-parens> | <in-parens> [ and|or <in-parens> ]* */
static bool onyx_supports_condition(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, bool *valid)
{
	const css_token *t;
	bool r;

	consumeWhitespace(vector, ctx);
	t = parserutils_vector_peek(vector, *ctx);
	if (onyx_is_ident(c, t, NOT)) {
		parserutils_vector_iterate(vector, ctx);
		return !onyx_supports_in_parens(c, vector, ctx, valid);
	}
	r = onyx_supports_in_parens(c, vector, ctx, valid);
	for (;;) {
		int32_t k = *ctx;
		bool and_op;
		consumeWhitespace(vector, &k);
		t = parserutils_vector_peek(vector, k);
		if (onyx_is_ident(c, t, AND))
			and_op = true;
		else if (onyx_is_ident(c, t, OR))
			and_op = false;
		else
			break;
		*ctx = k + 1;
		if (and_op)
			r = onyx_supports_in_parens(c, vector, ctx, valid) && r;
		else
			r = onyx_supports_in_parens(c, vector, ctx, valid) || r;
	}
	return r;
}

/* Documented in parse/onyx_atrules.h */
bool css__onyx_supports(css_language *c, const parserutils_vector *vector, int32_t *ctx)
{
	bool valid = true;
	bool r = onyx_supports_condition(c, vector, ctx, &valid);
	consumeWhitespace(vector, ctx);
	if (parserutils_vector_peek(vector, *ctx) != NULL)
		valid = false;		/* trailing tokens */
	return valid && r;
}

/* Documented in parse/onyx_atrules.h */
css_error css__onyx_container_media(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_mq_query **media)
{
	const css_token *t;

	/* an optional container name (an identifier, not a query keyword) */
	consumeWhitespace(vector, ctx);
	t = parserutils_vector_peek(vector, *ctx);
	if (t != NULL && t->type == CSS_TOKEN_IDENT && !onyx_is_ident(c, t, NOT) &&
			!onyx_is_ident(c, t, ONLY)) {
		parserutils_vector_iterate(vector, ctx);
		consumeWhitespace(vector, ctx);
	}
	return css__mq_parse_media_list(c->strings, vector, ctx, media);
}

/* ---- the at-rules kept without applying their content (onyx_atrules.h) --------------------- */

static bool onyx_atname(const css_token *t, const char *name)
{
	size_t len;
	if (t == NULL || t->type != CSS_TOKEN_ATKEYWORD || t->idata == NULL)
		return false;
	len = lwc_string_length(t->idata);
	return strlen(name) == len && strncasecmp(lwc_string_data(t->idata), name, len) == 0;
}

/* Documented in parse/onyx_atrules.h */
int css__onyx_at_kind(const css_token *atkeyword, int parent)
{
	static const char *const ffv[] = { "stylistic", "historical-forms", "styleset",
			"character-variant", "swash", "ornaments", "annotation" };
	static const char *const margins[] = { "top-left-corner", "top-left", "top-center",
			"top-right", "top-right-corner", "bottom-left-corner", "bottom-left",
			"bottom-center", "bottom-right", "bottom-right-corner", "left-top",
			"left-middle", "left-bottom", "right-top", "right-middle", "right-bottom" };
	size_t i;

	if (parent == ONYX_AT_FONT_FEATURE_VALUES) {
		for (i = 0; i < sizeof(ffv) / sizeof(ffv[0]); i++)
			if (onyx_atname(atkeyword, ffv[i]))
				return ONYX_AT_FFV_BLOCK;
		return ONYX_AT_NONE;
	}
	if (parent == -1) {		/* (in @page) */
		for (i = 0; i < sizeof(margins) / sizeof(margins[0]); i++)
			if (onyx_atname(atkeyword, margins[i]))
				return ONYX_AT_PAGE_MARGIN;
		return ONYX_AT_NONE;
	}
	if (parent != ONYX_AT_NONE && parent != ONYX_AT_SCOPE &&
			parent != ONYX_AT_STARTING_STYLE)
		return ONYX_AT_NONE;
	if (onyx_atname(atkeyword, "keyframes") || onyx_atname(atkeyword, "-webkit-keyframes"))
		return ONYX_AT_KEYFRAMES;
	if (onyx_atname(atkeyword, "counter-style"))
		return ONYX_AT_COUNTER_STYLE;
	if (onyx_atname(atkeyword, "property"))
		return ONYX_AT_PROPERTY;
	if (onyx_atname(atkeyword, "font-feature-values"))
		return ONYX_AT_FONT_FEATURE_VALUES;
	if (onyx_atname(atkeyword, "font-palette-values"))
		return ONYX_AT_FONT_PALETTE_VALUES;
	if (onyx_atname(atkeyword, "position-try"))
		return ONYX_AT_POSITION_TRY;
	if (onyx_atname(atkeyword, "view-transition"))
		return ONYX_AT_VIEW_TRANSITION;
	if (onyx_atname(atkeyword, "scope"))
		return ONYX_AT_SCOPE;
	if (onyx_atname(atkeyword, "starting-style"))
		return ONYX_AT_STARTING_STYLE;
	return ONYX_AT_NONE;
}

static bool onyx_tok_ident(const css_token *t, const char *s)
{
	size_t len;
	if (t == NULL || t->type != CSS_TOKEN_IDENT)
		return false;
	len = lwc_string_length(t->idata);
	return strlen(s) == len && strncasecmp(lwc_string_data(t->idata), s, len) == 0;
}

static bool onyx_css_wide(const css_token *t)
{
	return onyx_tok_ident(t, "initial") || onyx_tok_ident(t, "inherit") ||
			onyx_tok_ident(t, "unset") || onyx_tok_ident(t, "revert") ||
			onyx_tok_ident(t, "revert-layer") || onyx_tok_ident(t, "default");
}

/* the prelude's tokens, whitespace dropped (up to max): their count */
static int onyx_prelude_tokens(const parserutils_vector *vector, int32_t ctx,
		const css_token **out, int max, int32_t *index)
{
	const css_token *t;
	int n = 0;
	while ((t = parserutils_vector_peek(vector, ctx)) != NULL) {
		if (t->type != CSS_TOKEN_S) {
			if (n == max)
				return max + 1;
			if (index != NULL)
				index[n] = ctx;
			out[n++] = t;
		}
		ctx++;
	}
	return n;
}

static bool onyx_dashed(const css_token *t)
{
	return t != NULL && t->type == CSS_TOKEN_IDENT && lwc_string_length(t->idata) > 2 &&
			lwc_string_data(t->idata)[0] == '-' && lwc_string_data(t->idata)[1] == '-';
}

/* @scope's "( <selector-list> )" at tokens [i, ...): its end past the ')', or -1 */
static int onyx_scope_part(css_language *c, const parserutils_vector *vector,
		const css_token **tok, const int32_t *index, int n, int i)
{
	int depth = 0, j;
	if (i >= n || !tokenIsChar(tok[i], '('))
		return -1;
	for (j = i; j < n; j++) {
		if (tokenIsChar(tok[j], '(') || tok[j]->type == CSS_TOKEN_FUNCTION)
			depth++;
		else if (tokenIsChar(tok[j], ')') && --depth == 0)
			break;
	}
	if (j == n || j == i + 1)
		return -1;
	if (!css__onyx_selector_list_valid(c, vector, index[i] + 1, index[j]))
		return -1;
	return j + 1;
}

/* Documented in parse/onyx_atrules.h */
bool css__onyx_at_prelude(css_language *c, int kind, const parserutils_vector *vector,
		int32_t ctx)
{
	const css_token *tok[64];
	int32_t index[64];
	int n = onyx_prelude_tokens(vector, ctx, tok, 64, index), i;

	switch (kind) {
	case ONYX_AT_KEYFRAMES:
		/* <keyframes-name>: a custom identifier or a string */
		return n == 1 && ((tok[0]->type == CSS_TOKEN_IDENT && !onyx_css_wide(tok[0]) &&
				!onyx_tok_ident(tok[0], "none")) || tok[0]->type == CSS_TOKEN_STRING);
	case ONYX_AT_COUNTER_STYLE:
		return n == 1 && tok[0]->type == CSS_TOKEN_IDENT && !onyx_css_wide(tok[0]) &&
				!onyx_tok_ident(tok[0], "none") && !onyx_tok_ident(tok[0], "decimal") &&
				!onyx_tok_ident(tok[0], "disc") && !onyx_tok_ident(tok[0], "square") &&
				!onyx_tok_ident(tok[0], "circle") &&
				!onyx_tok_ident(tok[0], "disclosure-open") &&
				!onyx_tok_ident(tok[0], "disclosure-closed");
	case ONYX_AT_PROPERTY:
	case ONYX_AT_FONT_PALETTE_VALUES:
	case ONYX_AT_POSITION_TRY:
		return n == 1 && onyx_dashed(tok[0]);
	case ONYX_AT_FONT_FEATURE_VALUES: {
		/* <family-name>#: strings, or runs of identifiers, comma separated */
		bool item = false;
		if (n == 0 || n > 64)
			return false;
		for (i = 0; i < n; i++) {
			if (tokenIsChar(tok[i], ',')) {
				if (!item)
					return false;
				item = false;
			} else if (tok[i]->type == CSS_TOKEN_STRING) {
				if (item)
					return false;
				item = true;
			} else if (tok[i]->type == CSS_TOKEN_IDENT && !onyx_css_wide(tok[i])) {
				item = true;
			} else {
				return false;
			}
		}
		return item;
	}
	case ONYX_AT_FFV_BLOCK:
	case ONYX_AT_VIEW_TRANSITION:
	case ONYX_AT_STARTING_STYLE:
	case ONYX_AT_PAGE_MARGIN:
		return n == 0;
	case ONYX_AT_SCOPE:
		/* [ ( <scope-start> ) ]? [ to ( <scope-end> ) ]? */
		if (n > 64)
			return false;
		i = 0;
		if (i < n && tokenIsChar(tok[i], '(')) {
			i = onyx_scope_part(c, vector, tok, index, n, i);
			if (i < 0)
				return false;
		}
		if (i < n && onyx_tok_ident(tok[i], "to")) {
			i = onyx_scope_part(c, vector, tok, index, n, i + 1);
			if (i < 0)
				return false;
		}
		return i == n;
	default:
		return false;
	}
}

/* "name ':' value": the name token and the value's start (ctx), false if not that form */
static bool onyx_decl_split(const parserutils_vector *vector, const css_token **name,
		int32_t *ctx)
{
	const css_token *t;
	*ctx = 0;
	consumeWhitespace(vector, ctx);
	*name = parserutils_vector_iterate(vector, ctx);
	if (*name == NULL || (*name)->type != CSS_TOKEN_IDENT)
		return false;
	consumeWhitespace(vector, ctx);
	t = parserutils_vector_iterate(vector, ctx);
	if (t == NULL || !tokenIsChar(t, ':'))
		return false;
	consumeWhitespace(vector, ctx);
	return parserutils_vector_peek(vector, *ctx) != NULL;
}

/* a descriptor's value [ctx, end) against the grammar "@rule/name" */
static bool onyx_desc_grammar(const char *at, const css_token *name,
		const parserutils_vector *vector, int32_t ctx)
{
	char key[96];
	size_t al = strlen(at), nl = lwc_string_length(name->idata);
	int g;

	if (al + 1 + nl >= sizeof(key))
		return false;
	memcpy(key, at, al);
	key[al] = '/';
	memcpy(key + al + 1, lwc_string_data(name->idata), nl);
	key[al + 1 + nl] = '\0';
	g = css__onyx_grammar_descriptor(key, al + 1 + nl);
	if (g < 0)
		return false;
	return css__onyx_grammar_match(onyx_grammar_descs[g].root, vector, ctx, -1);
}

/* Documented in parse/onyx_atrules.h */
bool css__onyx_descriptor_valid(css_language *c, const char *at,
		const parserutils_vector *vector)
{
	const css_token *name;
	int32_t ctx;
	(void) c;
	if (!onyx_decl_split(vector, &name, &ctx))
		return false;
	return onyx_desc_grammar(at, name, vector, ctx);
}

/* Documented in parse/onyx_atrules.h */
bool css__onyx_at_declaration(css_language *c, int kind, const parserutils_vector *vector)
{
	const css_token *name;
	int32_t ctx;

	if (!onyx_decl_split(vector, &name, &ctx))
		return false;
	switch (kind) {
	case ONYX_AT_KEYFRAME:
	case ONYX_AT_POSITION_TRY:
	case ONYX_AT_PAGE_MARGIN:
		/* property declarations */
		return css__onyx_declaration_valid(c, name, vector, ctx);
	case ONYX_AT_COUNTER_STYLE:
		return onyx_desc_grammar("@counter-style", name, vector, ctx);
	case ONYX_AT_PROPERTY:
		if (onyx_tok_ident(name, "initial-value"))
			return true;	/* (any value: checked against the syntax when used) */
		return onyx_desc_grammar("@property", name, vector, ctx);
	case ONYX_AT_FONT_PALETTE_VALUES:
		return onyx_desc_grammar("@font-palette-values", name, vector, ctx);
	case ONYX_AT_VIEW_TRANSITION:
		return onyx_desc_grammar("@view-transition", name, vector, ctx);
	case ONYX_AT_FONT_FEATURE_VALUES:
		return onyx_desc_grammar("@font-feature-values", name, vector, ctx);
	case ONYX_AT_FFV_BLOCK: {
		/* <feature-value-name>: <integer [0,∞]>+ */
		const css_token *t;
		int n = 0;
		while ((t = parserutils_vector_iterate(vector, &ctx)) != NULL) {
			size_t len, i;
			const char *d;
			if (t->type == CSS_TOKEN_S)
				continue;
			if (t->type != CSS_TOKEN_NUMBER)
				return false;
			d = lwc_string_data(t->idata);
			len = lwc_string_length(t->idata);
			for (i = 0; i < len; i++)
				if (d[i] < '0' || d[i] > '9')
					return false;
			n++;
		}
		return n > 0;
	}
	default:
		return false;
	}
}
