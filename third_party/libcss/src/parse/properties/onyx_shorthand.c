/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: shorthands made of their longhands' own parsers. A shorthand's value is split into
 * its top-level components (a token, or a function with its arguments), and each component
 * is parsed by the parser of the longhand it gives -- so every value a longhand accepts
 * (keywords, lengths, calc(), min(), max(), clamp()) works in the shorthand too. The result
 * is written for each longhand the component goes to (the component's bytecode, its opcode
 * changed).
 *
 * The box shorthands (margin, padding, border-width / -style / -color, inset, border-radius,
 * gap), the logical ones for horizontal-tb, left-to-right text (margin-block, padding-inline,
 * inset-block, border-inline-width, ...: the block axis is top / bottom, the inline axis left
 * / right), border-block / border-inline, and place-items / -content / -self.
 */

#include <assert.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

#define ONYX_MAX_COMPONENTS 8

typedef struct {
	int32_t s, e;			/* tokens [s, e) */
} onyx_range;

/* The top-level components of the value at *ctx, up to the end, a '!' (!important) or a
 * `stop` char (0: none): their count (or -1 if more than max); *ctx left at the end. */
static int onyx_components(const parserutils_vector *vector, int32_t *ctx,
		onyx_range *out, int max, char stop)
{
	const css_token *t;
	int n = 0;

	for (;;) {
		int32_t s;
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_peek(vector, *ctx);
		if (t == NULL || tokenIsChar(t, '!') || (stop != 0 && tokenIsChar(t, stop)))
			break;
		if (n == max)
			return -1;
		s = *ctx;
		parserutils_vector_iterate(vector, ctx);
		if (t->type == CSS_TOKEN_FUNCTION || tokenIsChar(t, '(')) {
			int depth = 1;
			while (depth > 0 && (t = parserutils_vector_iterate(vector, ctx)) != NULL) {
				if (t->type == CSS_TOKEN_FUNCTION || tokenIsChar(t, '('))
					depth++;
				else if (tokenIsChar(t, ')'))
					depth--;
			}
			if (depth > 0)
				return -1;	/* unbalanced */
		}
		out[n].s = s;
		out[n].e = *ctx;
		n++;
	}
	return n;
}

/* Parse the tokens r with a (longhand's) parser into a new style of the sheet; all the
 * tokens must be used. */
static css_error onyx_parse_range(css_language *c, const parserutils_vector *vector,
		onyx_range r, css_prop_handler parse, css_style **out)
{
	parserutils_vector *sub;
	css_style *style = NULL;
	int32_t i, sctx = 0;
	css_error error;

	*out = NULL;
	if (parserutils_vector_create(sizeof(css_token), 8, &sub) != PARSERUTILS_OK)
		return CSS_NOMEM;
	for (i = r.s; i < r.e; i++) {
		css_token t = *((const css_token *) parserutils_vector_peek(vector, i));
		if (parserutils_vector_append(sub, &t) != PARSERUTILS_OK) {
			parserutils_vector_destroy(sub);
			return CSS_NOMEM;
		}
	}
	error = css__stylesheet_style_create(c->sheet, &style);
	if (error == CSS_OK)
		error = parse(c, sub, &sctx, style);
	if (error == CSS_OK) {
		consumeWhitespace(sub, &sctx);
		if (parserutils_vector_peek(sub, sctx) != NULL || style->used == 0)
			error = CSS_INVALID;	/* (trailing tokens) */
	}
	parserutils_vector_destroy(sub);
	if (error != CSS_OK) {
		if (style != NULL)
			css__stylesheet_style_destroy(style);
		return error;
	}
	*out = style;
	return CSS_OK;
}

/* Append a longhand's value (one opv and its operands) as property op. */
static css_error onyx_emit_as(css_style *result, const css_style *v, opcode_t op)
{
	css_error error;
	uint32_t k;

	error = css__stylesheet_style_append(result,
			buildOPV(op, getFlags(v->bytecode[0]), getValue(v->bytecode[0])));
	for (k = 1; k < v->used && error == CSS_OK; k++)
		error = css__stylesheet_style_append(result, v->bytecode[k]);
	return error;
}

/* inherit / initial / revert / unset for all of props (then *ctx past it). */
static bool onyx_flag_all(css_language *c, const parserutils_vector *vector, int32_t *ctx,
		css_style *result, const opcode_t *props, int n, css_error *error)
{
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	enum flag_value flag;
	int i;

	if (t == NULL)
		return false;
	flag = get_css_flag_value(c, t);
	if (flag == FLAG_VALUE__NONE)
		return false;
	parserutils_vector_iterate(vector, ctx);
	*error = CSS_OK;
	for (i = 0; i < n && *error == CSS_OK; i++)
		*error = css_stylesheet_style_flag_value(result, flag, props[i]);
	return true;
}

/* A shorthand of n longhands (props), from 1 to n components; the components go to the
 * longhands by `map[count - 1][longhand]`, each parsed by `parse` (the first longhand's
 * parser: its opcode replaced). */
static css_error onyx_multi(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result, css_prop_handler parse,
		const opcode_t *props, int n, const uint8_t (*map)[4])
{
	int32_t orig_ctx = *ctx;
	onyx_range r[ONYX_MAX_COMPONENTS];
	css_style *v[4] = { NULL, NULL, NULL, NULL };
	css_error error = CSS_OK;
	int count, i;

	if (onyx_flag_all(c, vector, ctx, result, props, n, &error))
		return error;

	count = onyx_components(vector, ctx, r, n, 0);
	if (count <= 0) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	for (i = 0; i < count && error == CSS_OK; i++)
		error = onyx_parse_range(c, vector, r[i], parse, &v[i]);
	for (i = 0; i < n && error == CSS_OK; i++)
		error = onyx_emit_as(result, v[map[count - 1][i]], props[i]);
	for (i = 0; i < count; i++) {
		if (v[i] != NULL)
			css__stylesheet_style_destroy(v[i]);
	}
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

/* the CSS box: 1 value for all four, 2: vertical horizontal, 3: top horizontal bottom,
 * 4: top right bottom left */
static const uint8_t box_map[4][4] = {
	{ 0, 0, 0, 0 }, { 0, 1, 0, 1 }, { 0, 1, 2, 1 }, { 0, 1, 2, 3 }
};
/* two longhands: 1 value for both, 2: each its own */
static const uint8_t pair_map[2][4] = { { 0, 0 }, { 0, 1 } };

#define ONYX_BOX(fn, parse, t, r, b, l)						\
css_error fn(css_language *c, const parserutils_vector *vector, int32_t *ctx,	\
		css_style *result)						\
{										\
	static const opcode_t props[4] = { t, r, b, l };			\
	return onyx_multi(c, vector, ctx, result, parse, props, 4, box_map);	\
}

#define ONYX_PAIR(fn, parse, a, b)						\
css_error fn(css_language *c, const parserutils_vector *vector, int32_t *ctx,	\
		css_style *result)						\
{										\
	static const opcode_t props[2] = { a, b };				\
	return onyx_multi(c, vector, ctx, result, parse, props, 2, pair_map);	\
}

/* ---- the box shorthands ------------------------------------------------------------------- */

ONYX_BOX(css__onyx_parse_margin, css__parse_margin_top, CSS_PROP_MARGIN_TOP,
		CSS_PROP_MARGIN_RIGHT, CSS_PROP_MARGIN_BOTTOM, CSS_PROP_MARGIN_LEFT)
ONYX_BOX(css__onyx_parse_padding, css__parse_padding_top, CSS_PROP_PADDING_TOP,
		CSS_PROP_PADDING_RIGHT, CSS_PROP_PADDING_BOTTOM, CSS_PROP_PADDING_LEFT)
ONYX_BOX(css__onyx_parse_border_width, css__parse_border_top_width,
		CSS_PROP_BORDER_TOP_WIDTH, CSS_PROP_BORDER_RIGHT_WIDTH,
		CSS_PROP_BORDER_BOTTOM_WIDTH, CSS_PROP_BORDER_LEFT_WIDTH)
ONYX_BOX(css__onyx_parse_border_style, css__parse_border_top_style,
		CSS_PROP_BORDER_TOP_STYLE, CSS_PROP_BORDER_RIGHT_STYLE,
		CSS_PROP_BORDER_BOTTOM_STYLE, CSS_PROP_BORDER_LEFT_STYLE)
ONYX_BOX(css__onyx_parse_border_color, css__parse_border_top_color,
		CSS_PROP_BORDER_TOP_COLOR, CSS_PROP_BORDER_RIGHT_COLOR,
		CSS_PROP_BORDER_BOTTOM_COLOR, CSS_PROP_BORDER_LEFT_COLOR)
ONYX_BOX(css__parse_inset, css__parse_top, CSS_PROP_TOP, CSS_PROP_RIGHT,
		CSS_PROP_BOTTOM, CSS_PROP_LEFT)

/* ---- the logical shorthands (horizontal-tb, ltr) ------------------------------------------ */

ONYX_PAIR(css__parse_margin_block, css__parse_margin_top,
		CSS_PROP_MARGIN_TOP, CSS_PROP_MARGIN_BOTTOM)
ONYX_PAIR(css__parse_margin_inline, css__parse_margin_left,
		CSS_PROP_MARGIN_LEFT, CSS_PROP_MARGIN_RIGHT)
ONYX_PAIR(css__parse_padding_block, css__parse_padding_top,
		CSS_PROP_PADDING_TOP, CSS_PROP_PADDING_BOTTOM)
ONYX_PAIR(css__parse_padding_inline, css__parse_padding_left,
		CSS_PROP_PADDING_LEFT, CSS_PROP_PADDING_RIGHT)
ONYX_PAIR(css__parse_inset_block, css__parse_top, CSS_PROP_TOP, CSS_PROP_BOTTOM)
ONYX_PAIR(css__parse_inset_inline, css__parse_left, CSS_PROP_LEFT, CSS_PROP_RIGHT)
ONYX_PAIR(css__parse_border_block_width, css__parse_border_top_width,
		CSS_PROP_BORDER_TOP_WIDTH, CSS_PROP_BORDER_BOTTOM_WIDTH)
ONYX_PAIR(css__parse_border_block_style, css__parse_border_top_style,
		CSS_PROP_BORDER_TOP_STYLE, CSS_PROP_BORDER_BOTTOM_STYLE)
ONYX_PAIR(css__parse_border_block_color, css__parse_border_top_color,
		CSS_PROP_BORDER_TOP_COLOR, CSS_PROP_BORDER_BOTTOM_COLOR)
ONYX_PAIR(css__parse_border_inline_width, css__parse_border_left_width,
		CSS_PROP_BORDER_LEFT_WIDTH, CSS_PROP_BORDER_RIGHT_WIDTH)
ONYX_PAIR(css__parse_border_inline_style, css__parse_border_left_style,
		CSS_PROP_BORDER_LEFT_STYLE, CSS_PROP_BORDER_RIGHT_STYLE)
ONYX_PAIR(css__parse_border_inline_color, css__parse_border_left_color,
		CSS_PROP_BORDER_LEFT_COLOR, CSS_PROP_BORDER_RIGHT_COLOR)
ONYX_PAIR(css__onyx_parse_overflow, css__parse_overflow_x, CSS_PROP_OVERFLOW_X,
		CSS_PROP_OVERFLOW_Y)
ONYX_PAIR(css__parse_gap, css__parse_row_gap, CSS_PROP_ROW_GAP,
		CSS_PROP_COLUMN_GAP)

/* border-block / border-inline: a border for both sides of the axis */
static css_error onyx_two_sides(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result, css_prop_handler a, css_prop_handler b)
{
	int32_t start = *ctx, bctx;
	css_error error = a(c, vector, ctx, result);
	if (error != CSS_OK)
		return error;
	bctx = start;
	error = b(c, vector, &bctx, result);
	if (error != CSS_OK)
		*ctx = start;
	return error;
}

css_error css__parse_border_block(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return onyx_two_sides(c, vector, ctx, result, css__parse_border_top,
			css__parse_border_bottom);
}

css_error css__parse_border_inline(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return onyx_two_sides(c, vector, ctx, result, css__parse_border_left,
			css__parse_border_right);
}

/* ---- border-radius and its corners --------------------------------------------------------- */

/* A corner: <radius> [<vertical radius>] (the vertical one read, not kept: circular) */
static css_error onyx_corner(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result, opcode_t op)
{
	int32_t orig_ctx = *ctx;
	onyx_range r[2];
	css_style *v = NULL;
	css_error error = CSS_OK;
	int count;

	if (onyx_flag_all(c, vector, ctx, result, &op, 1, &error))
		return error;
	count = onyx_components(vector, ctx, r, 2, 0);
	if (count <= 0) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	error = onyx_parse_range(c, vector, r[0], css__parse_border_radius_value, &v);
	if (error == CSS_OK)
		error = onyx_emit_as(result, v, op);
	if (v != NULL)
		css__stylesheet_style_destroy(v);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

css_error css__parse_border_top_left_radius(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	return onyx_corner(c, vector, ctx, result, CSS_PROP_BORDER_TOP_LEFT_RADIUS);
}

css_error css__parse_border_top_right_radius(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	return onyx_corner(c, vector, ctx, result, CSS_PROP_BORDER_TOP_RIGHT_RADIUS);
}

css_error css__parse_border_bottom_right_radius(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	return onyx_corner(c, vector, ctx, result, CSS_PROP_BORDER_BOTTOM_RIGHT_RADIUS);
}

css_error css__parse_border_bottom_left_radius(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	return onyx_corner(c, vector, ctx, result, CSS_PROP_BORDER_BOTTOM_LEFT_RADIUS);
}

/* border-radius: 1 to 4 radii [ / 1 to 4 vertical radii (read, not kept) ] */
css_error css__parse_border_radius(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	static const opcode_t props[4] = {
		CSS_PROP_BORDER_TOP_LEFT_RADIUS, CSS_PROP_BORDER_TOP_RIGHT_RADIUS,
		CSS_PROP_BORDER_BOTTOM_RIGHT_RADIUS, CSS_PROP_BORDER_BOTTOM_LEFT_RADIUS
	};
	int32_t orig_ctx = *ctx;
	onyx_range r[ONYX_MAX_COMPONENTS];
	css_style *v[4] = { NULL, NULL, NULL, NULL };
	css_error error = CSS_OK;
	const css_token *t;
	int count, i;

	if (onyx_flag_all(c, vector, ctx, result, props, 4, &error))
		return error;

	count = onyx_components(vector, ctx, r, 4, '/');
	if (count <= 0) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	t = parserutils_vector_peek(vector, *ctx);
	if (t != NULL && tokenIsChar(t, '/')) {
		onyx_range rv[ONYX_MAX_COMPONENTS];
		parserutils_vector_iterate(vector, ctx);
		if (onyx_components(vector, ctx, rv, 4, 0) <= 0) {
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
	}
	for (i = 0; i < count && error == CSS_OK; i++)
		error = onyx_parse_range(c, vector, r[i], css__parse_border_radius_value,
				&v[i]);
	for (i = 0; i < 4 && error == CSS_OK; i++)
		error = onyx_emit_as(result, v[box_map[count - 1][i]], props[i]);
	for (i = 0; i < count; i++) {
		if (v[i] != NULL)
			css__stylesheet_style_destroy(v[i]);
	}
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

/* ---- place-items / place-content / place-self: <align> [<justify>] ----------------------- */

static css_error onyx_place(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result, css_prop_handler align, opcode_t align_op,
		css_prop_handler justify, opcode_t justify_op)
{
	const opcode_t props[2] = { align_op, justify_op };
	int32_t orig_ctx = *ctx;
	onyx_range r[2];
	css_style *a = NULL, *j = NULL;
	css_error error = CSS_OK;
	int count;

	if (onyx_flag_all(c, vector, ctx, result, props, 2, &error))
		return error;
	count = onyx_components(vector, ctx, r, 2, 0);
	if (count <= 0) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	error = onyx_parse_range(c, vector, r[0], align, &a);
	if (error == CSS_OK)
		error = onyx_parse_range(c, vector, r[count - 1], justify, &j);
	if (error == CSS_OK)
		error = onyx_emit_as(result, a, align_op);
	if (error == CSS_OK)
		error = onyx_emit_as(result, j, justify_op);
	if (a != NULL)
		css__stylesheet_style_destroy(a);
	if (j != NULL)
		css__stylesheet_style_destroy(j);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

css_error css__parse_place_items(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return onyx_place(c, vector, ctx, result, css__parse_align_items,
			CSS_PROP_ALIGN_ITEMS, css__parse_justify_items, CSS_PROP_JUSTIFY_ITEMS);
}

css_error css__parse_place_content(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return onyx_place(c, vector, ctx, result, css__parse_align_content,
			CSS_PROP_ALIGN_CONTENT, css__parse_justify_content,
			CSS_PROP_JUSTIFY_CONTENT);
}

css_error css__parse_place_self(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return onyx_place(c, vector, ctx, result, css__parse_align_self,
			CSS_PROP_ALIGN_SELF, css__parse_justify_self, CSS_PROP_JUSTIFY_SELF);
}
