/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: the cascade of SVG's presentation properties (parse/properties/onyx_svg.c): fill,
 * stroke, stroke-width, stroke-dashoffset, stroke-miterlimit, stroke-dasharray, fill-rule,
 * stroke-linecap, stroke-linejoin (inherited), stop-color, stop-opacity (not inherited).
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propset.h"
#include "select/propget.h"
#include "utils/utils.h"

#include "select/properties/properties.h"
#include "select/properties/helpers.h"
#include "select/onyx_calc.h"

/* ---- fill, stroke ---------------------------------------------------------------------------- */

static css_error svg_cascade_paint(uint32_t opv, css_style *style, css_select_state *state,
		css_error (*set)(css_computed_style *, uint8_t, css_color, lwc_string *))
{
	uint8_t value = CSS_PAINT_INHERIT;
	css_color color = 0;
	lwc_string *url = NULL;

	if (hasFlagValue(opv) == false) {
		uint16_t v = getValue(opv);
		if (v & PAINT_URL) {
			css__stylesheet_string_get(style->sheet,
					*((css_code_t *) style->bytecode), &url);
			advance_bytecode(style, sizeof(css_code_t));
		}
		if (v & PAINT_COLOR) {
			color = *((css_color *) style->bytecode);
			advance_bytecode(style, sizeof(color));
		}
		if (v & PAINT_URL) {
			value = (v & PAINT_COLOR) ? CSS_PAINT_URL_COLOR :
				((v & 0x3f) == PAINT_CURRENT_COLOR) ?
					CSS_PAINT_URL_CURRENT_COLOR : CSS_PAINT_URL;
		} else if (v & PAINT_COLOR) {
			value = CSS_PAINT_COLOR;
		} else {
			switch (v) {
			case PAINT_CURRENT_COLOR: value = CSS_PAINT_CURRENT_COLOR; break;
			case PAINT_CONTEXT_FILL: value = CSS_PAINT_CONTEXT_FILL; break;
			case PAINT_CONTEXT_STROKE: value = CSS_PAINT_CONTEXT_STROKE; break;
			default: value = CSS_PAINT_NONE; break;
			}
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv)))
		return set(state->computed, value, color, url);
	return CSS_OK;
}

#define SVG_PAINT(pname, INITIAL, INITIAL_COLOR)					\
css_error css__cascade_##pname(uint32_t opv, css_style *style,			\
		css_select_state *state)					\
{										\
	return svg_cascade_paint(opv, style, state, set_##pname);		\
}										\
css_error css__set_##pname##_from_hint(const css_hint *hint,			\
		css_computed_style *style)					\
{										\
	return set_##pname(style, hint->status, hint->data.color, NULL);	\
}										\
css_error css__initial_##pname(css_select_state *state)			\
{										\
	return set_##pname(state->computed, INITIAL, INITIAL_COLOR, NULL);	\
}										\
css_error css__copy_##pname(const css_computed_style *from,			\
		css_computed_style *to)						\
{										\
	css_color color;							\
	lwc_string *url;							\
	uint8_t type = get_##pname(from, &color, &url);			\
	if (from == to)								\
		return CSS_OK;							\
	return set_##pname(to, type, color, url);				\
}										\
css_error css__compose_##pname(const css_computed_style *parent,		\
		const css_computed_style *child,				\
		css_computed_style *result)					\
{										\
	css_color color;							\
	lwc_string *url;							\
	uint8_t type = get_##pname(child, &color, &url);			\
	return css__copy_##pname(type == CSS_PAINT_INHERIT ? parent : child,	\
			result);						\
}

SVG_PAINT(fill, CSS_PAINT_COLOR, 0xff000000)
SVG_PAINT(stroke, CSS_PAINT_NONE, 0)

/* ---- stroke-width, stroke-dashoffset ------------------------------------------------------ */

static css_error svg_cascade_length(uint32_t opv, css_style *style, css_select_state *state,
		css_error (*set)(css_computed_style *, uint8_t, css_fixed, css_unit))
{
	uint8_t value = 0;		/* (INHERIT) */
	css_fixed length = 0;
	uint32_t unit = UNIT_PX;

	if (hasFlagValue(opv) == false) {
		if (getValue(opv) == VALUE_IS_CALC) {
			if (css__onyx_calc_fold(style, state, &length, &unit) != CSS_OK ||
					unit == UNIT_CALC_NUMBER)
				return CSS_OK;
		} else {
			length = *((css_fixed *) style->bytecode);
			advance_bytecode(style, sizeof(length));
			unit = *((uint32_t *) style->bytecode);
			advance_bytecode(style, sizeof(unit));
		}
		value = 1;		/* (SET) */
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv)))
		return set(state->computed, value, length, css__to_css_unit(unit));
	return CSS_OK;
}

#define SVG_LENGTH(pname, INITIAL)							\
css_error css__cascade_##pname(uint32_t opv, css_style *style,			\
		css_select_state *state)					\
{										\
	return svg_cascade_length(opv, style, state, set_##pname);		\
}										\
css_error css__set_##pname##_from_hint(const css_hint *hint,			\
		css_computed_style *style)					\
{										\
	return set_##pname(style, hint->status, hint->data.length.value,	\
			hint->data.length.unit);				\
}										\
css_error css__initial_##pname(css_select_state *state)			\
{										\
	return set_##pname(state->computed, 1, INITIAL, CSS_UNIT_PX);		\
}										\
css_error css__copy_##pname(const css_computed_style *from,			\
		css_computed_style *to)						\
{										\
	css_fixed length = 0;							\
	css_unit unit = CSS_UNIT_PX;						\
	uint8_t type = get_##pname(from, &length, &unit);			\
	if (from == to)								\
		return CSS_OK;							\
	return set_##pname(to, type, length, unit);				\
}										\
css_error css__compose_##pname(const css_computed_style *parent,		\
		const css_computed_style *child,				\
		css_computed_style *result)					\
{										\
	css_fixed length = 0;							\
	css_unit unit = CSS_UNIT_PX;						\
	uint8_t type = get_##pname(child, &length, &unit);			\
	return css__copy_##pname(type == 0 ? parent : child, result);		\
}

SVG_LENGTH(stroke_width, INTTOFIX(1))
SVG_LENGTH(stroke_dashoffset, 0)

/* ---- stroke-miterlimit, stop-opacity ------------------------------------------------------ */

static css_error svg_cascade_number(uint32_t opv, css_style *style, css_select_state *state,
		css_error (*set)(css_computed_style *, uint8_t, css_fixed))
{
	uint8_t value = 0;
	css_fixed num = 0;

	if (hasFlagValue(opv) == false) {
		num = *((css_fixed *) style->bytecode);
		advance_bytecode(style, sizeof(num));
		value = 1;
	}
	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv)))
		return set(state->computed, value, num);
	return CSS_OK;
}

#define SVG_NUMBER(pname, INITIAL)							\
css_error css__cascade_##pname(uint32_t opv, css_style *style,			\
		css_select_state *state)					\
{										\
	return svg_cascade_number(opv, style, state, set_##pname);		\
}										\
css_error css__set_##pname##_from_hint(const css_hint *hint,			\
		css_computed_style *style)					\
{										\
	return set_##pname(style, hint->status, hint->data.fixed);		\
}										\
css_error css__initial_##pname(css_select_state *state)			\
{										\
	return set_##pname(state->computed, 1, INITIAL);			\
}										\
css_error css__copy_##pname(const css_computed_style *from,			\
		css_computed_style *to)						\
{										\
	css_fixed num = 0;							\
	uint8_t type = get_##pname(from, &num);				\
	if (from == to)								\
		return CSS_OK;							\
	return set_##pname(to, type, num);					\
}										\
css_error css__compose_##pname(const css_computed_style *parent,		\
		const css_computed_style *child,				\
		css_computed_style *result)					\
{										\
	css_fixed num = 0;							\
	uint8_t type = get_##pname(child, &num);				\
	return css__copy_##pname(type == 0 ? parent : child, result);		\
}

SVG_NUMBER(stroke_miterlimit, INTTOFIX(4))
SVG_NUMBER(stop_opacity, INTTOFIX(1))

/* ---- the keyword properties (the bytecode value + 1: the computed type) ------------------ */

#define SVG_KEYWORD(pname, INITIAL)							\
css_error css__cascade_##pname(uint32_t opv, css_style *style,			\
		css_select_state *state)					\
{										\
	uint8_t value = 0;							\
	UNUSED(style);								\
	if (hasFlagValue(opv) == false)						\
		value = (uint8_t) (getValue(opv) + 1);				\
	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,	\
			getFlagValue(opv)))					\
		return set_##pname(state->computed, value);			\
	return CSS_OK;								\
}										\
css_error css__set_##pname##_from_hint(const css_hint *hint,			\
		css_computed_style *style)					\
{										\
	return set_##pname(style, hint->status);				\
}										\
css_error css__initial_##pname(css_select_state *state)			\
{										\
	return set_##pname(state->computed, INITIAL);				\
}										\
css_error css__copy_##pname(const css_computed_style *from,			\
		css_computed_style *to)						\
{										\
	uint8_t type = get_##pname(from);					\
	if (from == to)								\
		return CSS_OK;							\
	return set_##pname(to, type);						\
}										\
css_error css__compose_##pname(const css_computed_style *parent,		\
		const css_computed_style *child,				\
		css_computed_style *result)					\
{										\
	uint8_t type = get_##pname(child);					\
	return css__copy_##pname(type == 0 ? parent : child, result);		\
}

SVG_KEYWORD(fill_rule, CSS_FILL_RULE_NONZERO)
SVG_KEYWORD(pointer_events, CSS_POINTER_EVENTS_AUTO)	/* (Onyx) */
SVG_KEYWORD(stroke_linecap, CSS_STROKE_LINECAP_BUTT)
SVG_KEYWORD(stroke_linejoin, CSS_STROKE_LINEJOIN_MITER)

/* ---- stop-color ------------------------------------------------------------------------------ */

css_error css__cascade_stop_color(uint32_t opv, css_style *style, css_select_state *state)
{
	uint8_t value = CSS_STOP_COLOR_INHERIT;
	css_color color = 0;

	if (hasFlagValue(opv) == false) {
		if (getValue(opv) == STOP_COLOR_SET) {
			color = *((css_color *) style->bytecode);
			advance_bytecode(style, sizeof(color));
			value = CSS_STOP_COLOR_COLOR;
		} else {
			value = CSS_STOP_COLOR_CURRENT_COLOR;
		}
	}
	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv)))
		return set_stop_color(state->computed, value, color);
	return CSS_OK;
}

css_error css__set_stop_color_from_hint(const css_hint *hint, css_computed_style *style)
{
	return set_stop_color(style, hint->status, hint->data.color);
}

css_error css__initial_stop_color(css_select_state *state)
{
	return set_stop_color(state->computed, CSS_STOP_COLOR_COLOR, 0xff000000);
}

css_error css__copy_stop_color(const css_computed_style *from, css_computed_style *to)
{
	css_color color = 0;
	uint8_t type = get_stop_color(from, &color);
	if (from == to)
		return CSS_OK;
	return set_stop_color(to, type, color);
}

css_error css__compose_stop_color(const css_computed_style *parent,
		const css_computed_style *child, css_computed_style *result)
{
	css_color color = 0;
	uint8_t type = get_stop_color(child, &color);
	return css__copy_stop_color(type == CSS_STOP_COLOR_INHERIT ? parent : child, result);
}

/* ---- stroke-dasharray (a text) ---------------------------------------------------------------- */

css_error css__cascade_stroke_dasharray(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint8_t value = CSS_ONYX_TEXT_INHERIT;
	lwc_string *text = NULL;

	if (hasFlagValue(opv) == false) {
		if (getValue(opv) == ONYX_TEXT_SET) {
			value = CSS_ONYX_TEXT_SET;
			css__stylesheet_string_get(style->sheet,
					*((css_code_t *) style->bytecode), &text);
			advance_bytecode(style, sizeof(css_code_t));
		} else {
			value = CSS_ONYX_TEXT_NONE;
		}
	}
	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv)))
		return set_stroke_dasharray(state->computed, value, text);
	return CSS_OK;
}

css_error css__set_stroke_dasharray_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_stroke_dasharray(style, hint->status, hint->data.string);
}

css_error css__initial_stroke_dasharray(css_select_state *state)
{
	return set_stroke_dasharray(state->computed, CSS_ONYX_TEXT_NONE, NULL);
}

css_error css__copy_stroke_dasharray(const css_computed_style *from, css_computed_style *to)
{
	lwc_string *text = NULL;
	uint8_t type = get_stroke_dasharray(from, &text);
	if (from == to)
		return CSS_OK;
	return set_stroke_dasharray(to, type, text);
}

css_error css__compose_stroke_dasharray(const css_computed_style *parent,
		const css_computed_style *child, css_computed_style *result)
{
	lwc_string *text = NULL;
	uint8_t type = get_stroke_dasharray(child, &text);
	return css__copy_stroke_dasharray(type == CSS_ONYX_TEXT_INHERIT ? parent : child,
			result);
}
