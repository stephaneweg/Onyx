/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: SVG's presentation properties (SVG 2, CSS Fill and Stroke 3), so a page's rules
 * reach inline <svg> (an icon coloured by ".icon { fill: currentColor }"):
 *
 *   fill, stroke        none | <color> | <url> [ none | <color> ]? | context-fill |
 *                       context-stroke  (currentColor kept as such: it inherits as a keyword)
 *   stroke-width        <length-percentage> | <number> (a number: px)
 *   stroke-dashoffset   the same
 *   stroke-miterlimit   <number [1,inf]>
 *   stroke-dasharray    none | [ <length-percentage> | <number> ]+# -- kept as a canonical
 *                       text: the values separated by commas, with their units ("5px,10%")
 *   fill-rule           nonzero | evenodd
 *   stroke-linecap      butt | round | square
 *   stroke-linejoin     miter | miter-clip | round | bevel | arcs
 *   stop-color          <color>
 *   stop-opacity        <number> | <percentage> (clamped to [0, 1])
 *
 * The computed values: css_computed_fill() ... (select/computed.c).
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

static bool svg_word(const css_token *t, const char *w)
{
	size_t len;
	if (t == NULL || t->type != CSS_TOKEN_IDENT)
		return false;
	len = lwc_string_length(t->idata);
	return strlen(w) == len && strncasecmp(lwc_string_data(t->idata), w, len) == 0;
}

/* the value is over (only whitespace, or its "!important", left) */
static bool svg_end(const parserutils_vector *vector, int32_t *ctx)
{
	const css_token *t;
	consumeWhitespace(vector, ctx);
	t = parserutils_vector_peek(vector, *ctx);
	return t == NULL || tokenIsChar(t, '!');
}

/* a CSS-wide keyword alone: its flag value set, true */
static bool svg_flag(css_language *c, const parserutils_vector *vector, int32_t *ctx,
		css_style *result, opcode_t op, css_error *error)
{
	int32_t k = *ctx;
	const css_token *t;
	enum flag_value flag;

	consumeWhitespace(vector, &k);
	t = parserutils_vector_iterate(vector, &k);
	if (t == NULL || t->type != CSS_TOKEN_IDENT)
		return false;
	flag = get_css_flag_value(c, t);
	if (flag == FLAG_VALUE__NONE || !svg_end(vector, &k))
		return false;
	*ctx = k;
	*error = css_stylesheet_style_flag_value(result, flag, op);
	return true;
}

/* ---- fill, stroke ---------------------------------------------------------------------------- */

static css_error svg_parse_paint(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result, opcode_t op)
{
	int32_t orig_ctx = *ctx;
	const css_token *t;
	css_error error = CSS_OK;
	uint16_t value = 0;
	uint32_t url = 0, colour = 0;
	bool has_url = false;

	if (svg_flag(c, vector, ctx, result, op, &error))
		return error;

	consumeWhitespace(vector, ctx);
	t = parserutils_vector_peek(vector, *ctx);
	if (t == NULL)
		return CSS_INVALID;

	if (t->type == CSS_TOKEN_URI) {
		lwc_string *uri;
		parserutils_vector_iterate(vector, ctx);
		error = c->sheet->resolve(c->sheet->resolve_pw, c->sheet->url, t->idata, &uri);
		if (error != CSS_OK) {
			*ctx = orig_ctx;
			return error;
		}
		error = css__stylesheet_string_add(c->sheet, uri, &url);	/* (takes uri) */
		if (error != CSS_OK) {
			*ctx = orig_ctx;
			return error;
		}
		has_url = true;
		value = PAINT_URL;
		if (svg_end(vector, ctx))
			goto emit;		/* (no fallback: none) */
		t = parserutils_vector_peek(vector, *ctx);
	}

	if (svg_word(t, "none")) {
		parserutils_vector_iterate(vector, ctx);
		value |= PAINT_NONE;
	} else if (!has_url && svg_word(t, "context-fill")) {
		parserutils_vector_iterate(vector, ctx);
		value = PAINT_CONTEXT_FILL;
	} else if (!has_url && svg_word(t, "context-stroke")) {
		parserutils_vector_iterate(vector, ctx);
		value = PAINT_CONTEXT_STROKE;
	} else {
		uint16_t cv = 0;
		error = css__parse_colour_specifier(c, vector, ctx, &cv, &colour);
		if (error != CSS_OK) {
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
		if (cv == COLOR_CURRENT_COLOR)
			value |= PAINT_CURRENT_COLOR;
		else if (cv == COLOR_TRANSPARENT) {
			value |= PAINT_COLOR;
			colour = 0;
		} else
			value |= PAINT_COLOR;
	}
	if (!svg_end(vector, ctx)) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}

emit:
	error = css__stylesheet_style_appendOPV(result, op, 0, value);
	if (error == CSS_OK && has_url)
		error = css__stylesheet_style_append(result, url);
	if (error == CSS_OK && (value & PAINT_COLOR))
		error = css__stylesheet_style_append(result, colour);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

css_error css__parse_fill(css_language *c, const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return svg_parse_paint(c, vector, ctx, result, CSS_PROP_FILL);
}

css_error css__parse_stroke(css_language *c, const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return svg_parse_paint(c, vector, ctx, result, CSS_PROP_STROKE);
}

/* ---- stroke-width, stroke-dashoffset ------------------------------------------------------ */

/* <length-percentage> | <number> (px); calc() taken */
static css_error svg_parse_length(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result, opcode_t op, bool negative_ok)
{
	int32_t orig_ctx = *ctx;
	const css_token *t;
	css_fixed length = 0;
	uint32_t unit = 0;
	css_error error = CSS_OK;

	if (svg_flag(c, vector, ctx, result, op, &error))
		return error;
	consumeWhitespace(vector, ctx);
	t = parserutils_vector_peek(vector, *ctx);
	if (t != NULL && t->type == CSS_TOKEN_FUNCTION && css__is_calc_function(c, t)) {
		parserutils_vector_iterate(vector, ctx);
		error = css__parse_calc(c, vector, ctx, result,
				buildOPV(op, 0, VALUE_IS_CALC), UNIT_PX);
		if (error == CSS_OK && !svg_end(vector, ctx))
			error = CSS_INVALID;
		if (error != CSS_OK)
			*ctx = orig_ctx;
		return error;
	}
	if (t != NULL && t->type == CSS_TOKEN_NUMBER) {
		size_t consumed = 0;
		parserutils_vector_iterate(vector, ctx);
		length = css__number_from_lwc_string(t->idata, false, &consumed);
		if (consumed != lwc_string_length(t->idata)) {
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
		unit = UNIT_PX;
	} else {
		error = css__parse_unit_specifier(c, vector, ctx, UNIT_PX, &length, &unit);
		if (error != CSS_OK || (unit & (UNIT_LENGTH | UNIT_PCT)) == 0) {
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
	}
	if ((!negative_ok && length < 0) || !svg_end(vector, ctx)) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	error = css__stylesheet_style_appendOPV(result, op, 0, SVG_LENGTH_SET);
	if (error == CSS_OK)
		error = css__stylesheet_style_vappend(result, 2, length, unit);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

css_error css__parse_stroke_width(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return svg_parse_length(c, vector, ctx, result, CSS_PROP_STROKE_WIDTH, false);
}

css_error css__parse_stroke_dashoffset(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return svg_parse_length(c, vector, ctx, result, CSS_PROP_STROKE_DASHOFFSET, true);
}

/* ---- stroke-miterlimit, stop-opacity ------------------------------------------------------ */

static css_error svg_parse_number(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result, opcode_t op, bool opacity)
{
	int32_t orig_ctx = *ctx;
	const css_token *t;
	css_fixed num;
	size_t consumed = 0;
	css_error error = CSS_OK;

	if (svg_flag(c, vector, ctx, result, op, &error))
		return error;
	consumeWhitespace(vector, ctx);
	t = parserutils_vector_iterate(vector, ctx);
	if (t == NULL || (t->type != CSS_TOKEN_NUMBER &&
			!(opacity && t->type == CSS_TOKEN_PERCENTAGE))) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	num = css__number_from_lwc_string(t->idata, false, &consumed);
	if (consumed != lwc_string_length(t->idata) || !svg_end(vector, ctx)) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	if (t->type == CSS_TOKEN_PERCENTAGE)
		num = num / 100;
	if (opacity) {
		if (num < 0)
			num = 0;
		if (num > INTTOFIX(1))
			num = INTTOFIX(1);
	} else if (num < INTTOFIX(1)) {
		*ctx = orig_ctx;
		return CSS_INVALID;	/* (a miter limit below 1) */
	}
	error = css__stylesheet_style_appendOPV(result, op, 0, SVG_NUMBER_SET);
	if (error == CSS_OK)
		error = css__stylesheet_style_append(result, num);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

css_error css__parse_stroke_miterlimit(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return svg_parse_number(c, vector, ctx, result, CSS_PROP_STROKE_MITERLIMIT, false);
}

css_error css__parse_stop_opacity(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return svg_parse_number(c, vector, ctx, result, CSS_PROP_STOP_OPACITY, true);
}

/* ---- the keyword properties ------------------------------------------------------------------ */

static css_error svg_parse_keyword(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result, opcode_t op, const char *const *words)
{
	int32_t orig_ctx = *ctx;
	const css_token *t;
	css_error error = CSS_OK;
	uint16_t i;

	if (svg_flag(c, vector, ctx, result, op, &error))
		return error;
	consumeWhitespace(vector, ctx);
	t = parserutils_vector_iterate(vector, ctx);
	for (i = 0; words[i] != NULL; i++) {
		if (svg_word(t, words[i]) && svg_end(vector, ctx))
			return css__stylesheet_style_appendOPV(result, op, 0, i);
	}
	*ctx = orig_ctx;
	return CSS_INVALID;
}

css_error css__parse_fill_rule(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	static const char *const w[] = { "nonzero", "evenodd", NULL };
	return svg_parse_keyword(c, vector, ctx, result, CSS_PROP_FILL_RULE, w);
}

css_error css__parse_stroke_linecap(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	static const char *const w[] = { "butt", "round", "square", NULL };
	return svg_parse_keyword(c, vector, ctx, result, CSS_PROP_STROKE_LINECAP, w);
}

css_error css__parse_stroke_linejoin(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	static const char *const w[] = { "miter", "miter-clip", "round", "bevel", "arcs", NULL };
	return svg_parse_keyword(c, vector, ctx, result, CSS_PROP_STROKE_LINEJOIN, w);
}

/* ---- stop-color ------------------------------------------------------------------------------ */

css_error css__parse_stop_color(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	int32_t orig_ctx = *ctx;
	css_error error = CSS_OK;
	uint16_t cv = 0;
	uint32_t colour = 0;

	if (svg_flag(c, vector, ctx, result, CSS_PROP_STOP_COLOR, &error))
		return error;
	consumeWhitespace(vector, ctx);
	error = css__parse_colour_specifier(c, vector, ctx, &cv, &colour);
	if (error != CSS_OK || !svg_end(vector, ctx)) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	if (cv == COLOR_CURRENT_COLOR)
		return css__stylesheet_style_appendOPV(result, CSS_PROP_STOP_COLOR, 0,
				STOP_COLOR_CURRENT_COLOR);
	if (cv == COLOR_TRANSPARENT)
		colour = 0;
	error = css__stylesheet_style_appendOPV(result, CSS_PROP_STOP_COLOR, 0, STOP_COLOR_SET);
	if (error == CSS_OK)
		error = css__stylesheet_style_append(result, colour);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

/* ---- stroke-dasharray ------------------------------------------------------------------------ */

css_error css__parse_stroke_dasharray(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	int32_t orig_ctx = *ctx;
	const css_token *t;
	css_error error = CSS_OK;
	char text[512];
	size_t n = 0;
	lwc_string *s;
	uint32_t idx;
	int count = 0;

	if (svg_flag(c, vector, ctx, result, CSS_PROP_STROKE_DASHARRAY, &error))
		return error;
	consumeWhitespace(vector, ctx);
	t = parserutils_vector_peek(vector, *ctx);
	if (svg_word(t, "none")) {
		parserutils_vector_iterate(vector, ctx);
		if (!svg_end(vector, ctx)) {
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
		return css__stylesheet_style_appendOPV(result, CSS_PROP_STROKE_DASHARRAY, 0,
				ONYX_TEXT_NONE);
	}
	/* the values, separated by whitespace and / or commas */
	while (!svg_end(vector, ctx)) {
		size_t len, used;
		const char *d;
		t = parserutils_vector_iterate(vector, ctx);
		if (t != NULL && tokenIsChar(t, ',') && count > 0) {
			consumeWhitespace(vector, ctx);
			t = parserutils_vector_iterate(vector, ctx);
		}
		if (t == NULL || (t->type != CSS_TOKEN_NUMBER && t->type != CSS_TOKEN_DIMENSION &&
				t->type != CSS_TOKEN_PERCENTAGE)) {
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
		d = lwc_string_data(t->idata);
		len = lwc_string_length(t->idata);
		if (d[0] == '-') {
			*ctx = orig_ctx;
			return CSS_INVALID;	/* (no negative dash) */
		}
		if (t->type == CSS_TOKEN_DIMENSION) {
			uint32_t unit;
			css__number_from_lwc_string(t->idata, false, &used);
			if (css__parse_unit_keyword(d + used, len - used, &unit) != CSS_OK ||
					(unit & UNIT_LENGTH) == 0) {
				*ctx = orig_ctx;
				return CSS_INVALID;
			}
		}
		if (n + len + 3 >= sizeof(text)) {
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
		if (count++ > 0)
			text[n++] = ',';
		memcpy(text + n, d, len);
		n += len;
		if (t->type == CSS_TOKEN_PERCENTAGE)
			text[n++] = '%';
	}
	if (count == 0) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	if (lwc_intern_string(text, n, &s) != lwc_error_ok)
		return CSS_NOMEM;
	error = css__stylesheet_string_add(c->sheet, s, &idx);	/* (takes s) */
	if (error == CSS_OK)
		error = css__stylesheet_style_appendOPV(result, CSS_PROP_STROKE_DASHARRAY, 0,
				ONYX_TEXT_SET);
	if (error == CSS_OK)
		error = css__stylesheet_style_append(result, idx);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}
