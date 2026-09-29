/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: parsers for CSS3 properties libcss lacked -- box-shadow, text-shadow,
 * background-size (gap and border-radius: onyx_shorthand.c). Their bytecode:
 * src/bytecode/opcodes.h; their selection: src/select/properties/onyx_css3.c.
 *
 * Simplification: box-shadow and text-shadow keep the first shadow of a list.
 */

#include <assert.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/* A length (not an angle, a time, a frequency; a percentage when pct): its value and unit. */
static css_error onyx_length(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, bool pct, css_fixed *len, uint32_t *unit)
{
	int32_t orig_ctx = *ctx;
	css_error error = css__parse_unit_specifier(c, vector, ctx, UNIT_PX, len, unit);
	if (error != CSS_OK)
		return error;
	if ((*unit & UNIT_ANGLE) || (*unit & UNIT_TIME) || (*unit & UNIT_FREQ) ||
	    (*unit & UNIT_RESOLUTION) || (!pct && (*unit & UNIT_PCT))) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	return CSS_OK;
}

/* The next token is a ',' / '/' char. */
static bool onyx_is_char(const parserutils_vector *vector, int32_t ctx, char ch)
{
	const css_token *token = parserutils_vector_peek(vector, ctx);
	return token != NULL && tokenIsChar(token, ch);
}

/* Skip to the end of the declaration's value (the rest of a list we do not keep). */
static void onyx_skip_rest(const parserutils_vector *vector, int32_t *ctx)
{
	const css_token *token;
	while ((token = parserutils_vector_peek(vector, *ctx)) != NULL) {
		if (tokenIsChar(token, '!'))
			break;		/* (!important: the core parser reads it) */
		parserutils_vector_iterate(vector, ctx);
	}
}

/* ---- box-shadow / text-shadow ---------------------------------------------------------- */

/* One shadow: [inset]? && <length>{2,max} && <color>? in any order. Reads up to `max`
 * lengths (4: box-shadow's x y blur spread; 3: text-shadow's x y blur). */
static css_error onyx_shadow(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, bool allow_inset, int max,
		bool *inset, css_fixed *len, uint32_t *unit, int *nlen,
		bool *has_color, uint32_t *color)
{
	const css_token *token;
	bool match;

	*inset = false;
	*has_color = false;
	*nlen = 0;
	for (;;) {
		consumeWhitespace(vector, ctx);
		token = parserutils_vector_peek(vector, *ctx);
		if (token == NULL || tokenIsChar(token, ',') || tokenIsChar(token, '!'))
			break;
		if (allow_inset && !*inset && token->type == CSS_TOKEN_IDENT &&
		    lwc_string_caseless_isequal(token->idata, c->strings[INSET],
				&match) == lwc_error_ok && match) {
			parserutils_vector_iterate(vector, ctx);
			*inset = true;
			continue;
		}
		if (*nlen < max && (token->type == CSS_TOKEN_DIMENSION ||
				    token->type == CSS_TOKEN_NUMBER)) {
			if (onyx_length(c, vector, ctx, false, &len[*nlen], &unit[*nlen]) != CSS_OK)
				return CSS_INVALID;
			(*nlen)++;
			continue;
		}
		if (!*has_color) {
			uint16_t value = 0;
			if (css__parse_colour_specifier(c, vector, ctx, &value, color) != CSS_OK)
				return CSS_INVALID;
			/* currentColor: as no colour at all (resolved when computed) */
			*has_color = (value != 0x0001);
			if (value == 0x0000)		/* transparent */
				*color = 0;
			continue;
		}
		return CSS_INVALID;
	}
	if (*nlen < 2)
		return CSS_INVALID;
	while (*nlen < max) {			/* blur / spread default to 0 */
		len[*nlen] = 0;
		unit[*nlen] = UNIT_PX;
		(*nlen)++;
	}
	return CSS_OK;
}

static css_error onyx_parse_shadow(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result, bool box)
{
	int32_t orig_ctx = *ctx;
	enum css_properties_e op = box ? CSS_PROP_BOX_SHADOW : CSS_PROP_TEXT_SHADOW;
	const css_token *token;
	enum flag_value flag_value;
	css_error error;
	bool inset, has_color, match;
	css_fixed len[4];
	uint32_t unit[4], color = 0;
	uint16_t value;
	int nlen, i;

	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;
	flag_value = get_css_flag_value(c, token);
	if (flag_value != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		return css_stylesheet_style_flag_value(result, flag_value, op);
	}
	if (token->type == CSS_TOKEN_IDENT &&
	    lwc_string_caseless_isequal(token->idata, c->strings[NONE],
			&match) == lwc_error_ok && match) {
		parserutils_vector_iterate(vector, ctx);
		return css__stylesheet_style_appendOPV(result, op, 0,
				box ? BOX_SHADOW_NONE : TEXT_SHADOW_NONE);
	}

	if (onyx_shadow(c, vector, ctx, box, box ? 4 : 3, &inset, len, unit, &nlen,
			&has_color, &color) != CSS_OK) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	if (onyx_is_char(vector, *ctx, ','))		/* the other shadows: dropped */
		onyx_skip_rest(vector, ctx);

	if (box)
		value = BOX_SHADOW_SET | (inset ? BOX_SHADOW_INSET : 0) |
				(has_color ? BOX_SHADOW_COLOR : 0);
	else
		value = TEXT_SHADOW_SET | (has_color ? TEXT_SHADOW_COLOR : 0);

	error = css__stylesheet_style_appendOPV(result, op, 0, value);
	for (i = 0; i < nlen && error == CSS_OK; i++)
		error = css__stylesheet_style_vappend(result, 2, len[i], unit[i]);
	if (error == CSS_OK && has_color)
		error = css__stylesheet_style_append(result, color);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

css_error css__parse_box_shadow(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	return onyx_parse_shadow(c, vector, ctx, result, true);
}

css_error css__parse_text_shadow(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	return onyx_parse_shadow(c, vector, ctx, result, false);
}

/* ---- background-size: cover | contain | [<length-percentage> | auto]{1,2} --------------- */

css_error css__parse_background_size(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	int32_t orig_ctx = *ctx;
	const css_token *token;
	enum flag_value flag_value;
	css_error error;
	bool match, is_auto[2] = { true, true };
	css_fixed len[2] = { 0, 0 };
	uint32_t unit[2] = { UNIT_PX, UNIT_PX };
	uint16_t value;
	int n;

	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;
	flag_value = get_css_flag_value(c, token);
	if (flag_value != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		return css_stylesheet_style_flag_value(result, flag_value,
				CSS_PROP_BACKGROUND_SIZE);
	}
	if (token->type == CSS_TOKEN_IDENT) {
		if (lwc_string_caseless_isequal(token->idata, c->strings[COVER],
				&match) == lwc_error_ok && match) {
			parserutils_vector_iterate(vector, ctx);
			return css__stylesheet_style_appendOPV(result,
					CSS_PROP_BACKGROUND_SIZE, 0, BACKGROUND_SIZE_COVER);
		}
		if (lwc_string_caseless_isequal(token->idata, c->strings[CONTAIN],
				&match) == lwc_error_ok && match) {
			parserutils_vector_iterate(vector, ctx);
			return css__stylesheet_style_appendOPV(result,
					CSS_PROP_BACKGROUND_SIZE, 0, BACKGROUND_SIZE_CONTAIN);
		}
	}

	for (n = 0; n < 2; n++) {
		consumeWhitespace(vector, ctx);
		token = parserutils_vector_peek(vector, *ctx);
		if (token == NULL || tokenIsChar(token, ',') || tokenIsChar(token, '!'))
			break;
		if (token->type == CSS_TOKEN_IDENT &&
		    lwc_string_caseless_isequal(token->idata, c->strings[AUTO],
				&match) == lwc_error_ok && match) {
			parserutils_vector_iterate(vector, ctx);
			is_auto[n] = true;
			continue;
		}
		if (onyx_length(c, vector, ctx, true, &len[n], &unit[n]) != CSS_OK ||
		    len[n] < 0) {
			if (n == 0) {
				*ctx = orig_ctx;
				return CSS_INVALID;
			}
			break;
		}
		is_auto[n] = false;
	}
	if (n == 0) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	if (onyx_is_char(vector, *ctx, ','))		/* the other layers: dropped */
		onyx_skip_rest(vector, ctx);

	if (is_auto[0] && is_auto[1]) {
		return css__stylesheet_style_appendOPV(result, CSS_PROP_BACKGROUND_SIZE, 0,
				BACKGROUND_SIZE_AUTO);
	}
	value = BACKGROUND_SIZE_SET | (is_auto[0] ? BACKGROUND_SIZE_W_AUTO : 0) |
			(is_auto[1] ? BACKGROUND_SIZE_H_AUTO : 0);
	error = css__stylesheet_style_appendOPV(result, CSS_PROP_BACKGROUND_SIZE, 0, value);
	if (error == CSS_OK && !is_auto[0])
		error = css__stylesheet_style_vappend(result, 2, len[0], unit[0]);
	if (error == CSS_OK && !is_auto[1])
		error = css__stylesheet_style_vappend(result, 2, len[1], unit[1]);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}
