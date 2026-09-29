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

#include "bytecode/bytecode.h"
#include "parse/onyx_atrules.h"
#include "parse/onyx_vars.h"
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

/* Is "property: value" (tokens [s, e)) a declaration this parser accepts? */
static bool onyx_supports_decl(css_language *c, const parserutils_vector *vector,
		int32_t s, int32_t e)
{
	const css_token *prop, *t;
	parserutils_vector *sub = NULL;
	css_style *style = NULL;
	int32_t ctx = s, sctx = 0, i;
	int p;
	bool ok = false, match;
	css_error error;

	consumeWhitespace(vector, &ctx);
	prop = parserutils_vector_iterate(vector, &ctx);
	if (prop == NULL || prop->type != CSS_TOKEN_IDENT || ctx > e)
		return false;
	consumeWhitespace(vector, &ctx);
	t = parserutils_vector_iterate(vector, &ctx);
	if (t == NULL || !tokenIsChar(t, ':') || ctx > e)
		return false;

	if (css__onyx_is_custom_name(prop->idata))
		return true;
	for (p = FIRST_PROP; p <= LAST_PROP; p++) {
		if (lwc_string_caseless_isequal(prop->idata, c->strings[p], &match) ==
				lwc_error_ok && match)
			break;
	}
	if (p > LAST_PROP)
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
	if (css__onyx_value_has_var(c, sub, sctx)) {
		ok = true;			/* (known when the variables are) */
		goto done;
	}
	if (css__stylesheet_style_create(c->sheet, &style) != CSS_OK)
		goto done;
	error = property_handlers[p - FIRST_PROP](c, sub, &sctx, style);
	if (error == CSS_OK) {
		consumeWhitespace(sub, &sctx);
		ok = (parserutils_vector_peek(sub, sctx) == NULL);
	}

done:
	if (style != NULL)
		css__stylesheet_style_destroy(style);
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
