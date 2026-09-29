/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: selection of the CSS3 properties libcss lacked -- row-gap, border-*-radius,
 * box-shadow, text-shadow, background-size, text-overflow. Their parsing:
 * src/parse/properties/onyx_css3.c; their bytecode: src/bytecode/opcodes.h.
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propset.h"
#include "select/propget.h"
#include "utils/utils.h"

#include "select/properties/properties.h"
#include "select/properties/helpers.h"

/* ---- row-gap (column-gap's twin) --------------------------------------------------------- */

css_error css__cascade_row_gap(uint32_t opv, css_style *style,
		css_select_state *state)
{
	/* ROW_GAP_* == LETTER_SPACING_*, CSS_ROW_GAP_* == CSS_LETTER_SPACING_* */
	return css__cascade_length_normal(opv, style, state, set_row_gap);
}

css_error css__set_row_gap_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_row_gap(style, hint->status,
			hint->data.length.value, hint->data.length.unit);
}

css_error css__initial_row_gap(css_select_state *state)
{
	return set_row_gap(state->computed, CSS_ROW_GAP_NORMAL,
			INTTOFIX(1), CSS_UNIT_EM);
}

css_error css__copy_row_gap(
		const css_computed_style *from,
		css_computed_style *to)
{
	css_fixed length = INTTOFIX(1);
	css_unit unit = CSS_UNIT_EM;
	uint8_t type = get_row_gap(from, &length, &unit);

	if (from == to) {
		return CSS_OK;
	}

	return set_row_gap(to, type, length, unit);
}

css_error css__compose_row_gap(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_fixed length = INTTOFIX(1);
	css_unit unit = CSS_UNIT_EM;
	uint8_t type = get_row_gap(child, &length, &unit);

	return css__copy_row_gap(
			type == CSS_ROW_GAP_INHERIT ? parent : child,
			result);
}

/* ---- border-{top-left,top-right,bottom-right,bottom-left}-radius -------------------------- */

/* BORDER_RADIUS_SET == MIN_HEIGHT_SET, CSS_BORDER_RADIUS_SET == CSS_MIN_HEIGHT_SET */
#define ONYX_RADIUS(corner)							\
css_error css__cascade_border_##corner##_radius(uint32_t opv, css_style *style,\
		css_select_state *state)					\
{									\
	return css__cascade_length(opv, style, state,			\
			set_border_##corner##_radius);			\
}									\
									\
css_error css__set_border_##corner##_radius_from_hint(const css_hint *hint,	\
		css_computed_style *style)				\
{									\
	return set_border_##corner##_radius(style, hint->status,		\
			hint->data.length.value, hint->data.length.unit);	\
}									\
									\
css_error css__initial_border_##corner##_radius(css_select_state *state)	\
{									\
	return set_border_##corner##_radius(state->computed,		\
			CSS_BORDER_RADIUS_SET, 0, CSS_UNIT_PX);		\
}									\
									\
css_error css__copy_border_##corner##_radius(				\
		const css_computed_style *from,				\
		css_computed_style *to)					\
{									\
	css_fixed length = 0;						\
	css_unit unit = CSS_UNIT_PX;					\
	uint8_t type = get_border_##corner##_radius(from, &length, &unit);	\
									\
	if (from == to) {						\
		return CSS_OK;						\
	}								\
									\
	return set_border_##corner##_radius(to, type, length, unit);	\
}									\
									\
css_error css__compose_border_##corner##_radius(			\
		const css_computed_style *parent,			\
		const css_computed_style *child,			\
		css_computed_style *result)				\
{									\
	css_fixed length = 0;						\
	css_unit unit = CSS_UNIT_PX;					\
	uint8_t type = get_border_##corner##_radius(child, &length, &unit);	\
									\
	return css__copy_border_##corner##_radius(			\
			type == CSS_BORDER_RADIUS_INHERIT ? parent : child,	\
			result);					\
}

ONYX_RADIUS(top_left)
ONYX_RADIUS(top_right)
ONYX_RADIUS(bottom_right)
ONYX_RADIUS(bottom_left)

/* ---- box-shadow ----------------------------------------------------------------------------- */

/* Read n (length, unit) pairs from the bytecode. */
static void onyx_read_lengths(css_style *style, int n, css_fixed *len, css_unit *unit)
{
	int i;
	for (i = 0; i < n; i++) {
		uint32_t u;
		len[i] = *((css_fixed *) style->bytecode);
		advance_bytecode(style, sizeof(css_fixed));
		u = *((uint32_t *) style->bytecode);
		advance_bytecode(style, sizeof(u));
		unit[i] = css__to_css_unit(u);
	}
}

css_error css__cascade_box_shadow(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_BOX_SHADOW_INHERIT;
	css_fixed len[4] = { 0, 0, 0, 0 };
	css_unit unit[4] = { CSS_UNIT_PX, CSS_UNIT_PX, CSS_UNIT_PX, CSS_UNIT_PX };
	css_color color = 0;

	if (hasFlagValue(opv) == false) {
		uint16_t v = getValue(opv);
		if (v & BOX_SHADOW_SET) {
			bool inset = (v & BOX_SHADOW_INSET) != 0;
			onyx_read_lengths(style, 4, len, unit);
			if (v & BOX_SHADOW_COLOR) {
				color = *((css_color *) style->bytecode);
				advance_bytecode(style, sizeof(color));
				value = inset ? CSS_BOX_SHADOW_SET_INSET : CSS_BOX_SHADOW_SET;
			} else {
				value = inset ? CSS_BOX_SHADOW_SET_INSET_CURRENT_COLOR :
						CSS_BOX_SHADOW_SET_CURRENT_COLOR;
			}
		} else {
			value = CSS_BOX_SHADOW_NONE;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_box_shadow(state->computed, value,
				len[0], unit[0], len[1], unit[1],
				len[2], unit[2], len[3], unit[3], color);
	}

	return CSS_OK;
}

css_error css__set_box_shadow_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_box_shadow(style, hint->status, 0, CSS_UNIT_PX, 0, CSS_UNIT_PX,
			0, CSS_UNIT_PX, 0, CSS_UNIT_PX, 0);
}

css_error css__initial_box_shadow(css_select_state *state)
{
	return set_box_shadow(state->computed, CSS_BOX_SHADOW_NONE,
			0, CSS_UNIT_PX, 0, CSS_UNIT_PX,
			0, CSS_UNIT_PX, 0, CSS_UNIT_PX, 0);
}

css_error css__copy_box_shadow(
		const css_computed_style *from,
		css_computed_style *to)
{
	css_fixed len[4];
	css_unit unit[4];
	css_color color;
	uint8_t type = get_box_shadow(from, &len[0], &unit[0], &len[1], &unit[1],
			&len[2], &unit[2], &len[3], &unit[3], &color);

	if (from == to) {
		return CSS_OK;
	}

	return set_box_shadow(to, type, len[0], unit[0], len[1], unit[1],
			len[2], unit[2], len[3], unit[3], color);
}

css_error css__compose_box_shadow(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_fixed len[4];
	css_unit unit[4];
	css_color color;
	uint8_t type = get_box_shadow(child, &len[0], &unit[0], &len[1], &unit[1],
			&len[2], &unit[2], &len[3], &unit[3], &color);

	return css__copy_box_shadow(
			type == CSS_BOX_SHADOW_INHERIT ? parent : child,
			result);
}

/* ---- text-shadow (inherited) ------------------------------------------------------------- */

css_error css__cascade_text_shadow(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_TEXT_SHADOW_INHERIT;
	css_fixed len[3] = { 0, 0, 0 };
	css_unit unit[3] = { CSS_UNIT_PX, CSS_UNIT_PX, CSS_UNIT_PX };
	css_color color = 0;

	if (hasFlagValue(opv) == false) {
		uint16_t v = getValue(opv);
		if (v & TEXT_SHADOW_SET) {
			onyx_read_lengths(style, 3, len, unit);
			if (v & TEXT_SHADOW_COLOR) {
				color = *((css_color *) style->bytecode);
				advance_bytecode(style, sizeof(color));
				value = CSS_TEXT_SHADOW_SET;
			} else {
				value = CSS_TEXT_SHADOW_SET_CURRENT_COLOR;
			}
		} else {
			value = CSS_TEXT_SHADOW_NONE;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_text_shadow(state->computed, value,
				len[0], unit[0], len[1], unit[1],
				len[2], unit[2], color);
	}

	return CSS_OK;
}

css_error css__set_text_shadow_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_text_shadow(style, hint->status, 0, CSS_UNIT_PX, 0, CSS_UNIT_PX,
			0, CSS_UNIT_PX, 0);
}

css_error css__initial_text_shadow(css_select_state *state)
{
	return set_text_shadow(state->computed, CSS_TEXT_SHADOW_NONE,
			0, CSS_UNIT_PX, 0, CSS_UNIT_PX, 0, CSS_UNIT_PX, 0);
}

css_error css__copy_text_shadow(
		const css_computed_style *from,
		css_computed_style *to)
{
	css_fixed len[3];
	css_unit unit[3];
	css_color color;
	uint8_t type = get_text_shadow(from, &len[0], &unit[0], &len[1], &unit[1],
			&len[2], &unit[2], &color);

	if (from == to) {
		return CSS_OK;
	}

	return set_text_shadow(to, type, len[0], unit[0], len[1], unit[1],
			len[2], unit[2], color);
}

css_error css__compose_text_shadow(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_fixed len[3];
	css_unit unit[3];
	css_color color;
	uint8_t type = get_text_shadow(child, &len[0], &unit[0], &len[1], &unit[1],
			&len[2], &unit[2], &color);

	return css__copy_text_shadow(
			type == CSS_TEXT_SHADOW_INHERIT ? parent : child,
			result);
}

/* ---- background-size ----------------------------------------------------------------------- */

css_error css__cascade_background_size(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_BACKGROUND_SIZE_INHERIT;
	css_fixed len[2] = { 0, 0 };
	css_unit unit[2] = { CSS_UNIT_PX, CSS_UNIT_PX };

	if (hasFlagValue(opv) == false) {
		uint16_t v = getValue(opv);
		if (v & BACKGROUND_SIZE_SET) {
			if ((v & BACKGROUND_SIZE_W_AUTO) == 0)
				onyx_read_lengths(style, 1, &len[0], &unit[0]);
			if ((v & BACKGROUND_SIZE_H_AUTO) == 0)
				onyx_read_lengths(style, 1, &len[1], &unit[1]);
			if (v & BACKGROUND_SIZE_W_AUTO)
				value = CSS_BACKGROUND_SIZE_SET_HEIGHT;
			else if (v & BACKGROUND_SIZE_H_AUTO)
				value = CSS_BACKGROUND_SIZE_SET_WIDTH;
			else
				value = CSS_BACKGROUND_SIZE_SET;
		} else if (v == BACKGROUND_SIZE_COVER) {
			value = CSS_BACKGROUND_SIZE_COVER;
		} else if (v == BACKGROUND_SIZE_CONTAIN) {
			value = CSS_BACKGROUND_SIZE_CONTAIN;
		} else {
			value = CSS_BACKGROUND_SIZE_AUTO;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_background_size(state->computed, value,
				len[0], unit[0], len[1], unit[1]);
	}

	return CSS_OK;
}

css_error css__set_background_size_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_background_size(style, hint->status, 0, CSS_UNIT_PX, 0, CSS_UNIT_PX);
}

css_error css__initial_background_size(css_select_state *state)
{
	return set_background_size(state->computed, CSS_BACKGROUND_SIZE_AUTO,
			0, CSS_UNIT_PX, 0, CSS_UNIT_PX);
}

css_error css__copy_background_size(
		const css_computed_style *from,
		css_computed_style *to)
{
	css_fixed len[2];
	css_unit unit[2];
	uint8_t type = get_background_size(from, &len[0], &unit[0], &len[1], &unit[1]);

	if (from == to) {
		return CSS_OK;
	}

	return set_background_size(to, type, len[0], unit[0], len[1], unit[1]);
}

css_error css__compose_background_size(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_fixed len[2];
	css_unit unit[2];
	uint8_t type = get_background_size(child, &len[0], &unit[0], &len[1], &unit[1]);

	return css__copy_background_size(
			type == CSS_BACKGROUND_SIZE_INHERIT ? parent : child,
			result);
}

/* ---- text-overflow ------------------------------------------------------------------------- */

css_error css__cascade_text_overflow(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_TEXT_OVERFLOW_INHERIT;

	UNUSED(style);

	if (hasFlagValue(opv) == false) {
		value = (getValue(opv) == TEXT_OVERFLOW_ELLIPSIS) ?
				CSS_TEXT_OVERFLOW_ELLIPSIS : CSS_TEXT_OVERFLOW_CLIP;
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_text_overflow(state->computed, value);
	}

	return CSS_OK;
}

css_error css__set_text_overflow_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_text_overflow(style, hint->status);
}

css_error css__initial_text_overflow(css_select_state *state)
{
	return set_text_overflow(state->computed, CSS_TEXT_OVERFLOW_CLIP);
}

css_error css__copy_text_overflow(
		const css_computed_style *from,
		css_computed_style *to)
{
	if (from == to) {
		return CSS_OK;
	}

	return set_text_overflow(to, get_text_overflow(from));
}

css_error css__compose_text_overflow(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	uint8_t type = get_text_overflow(child);

	return css__copy_text_overflow(
			type == CSS_TEXT_OVERFLOW_INHERIT ? parent : child,
			result);
}

/* ---- justify-items / justify-self (align-items' / align-self's values) --------------------- */

css_error css__cascade_justify_items(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_JUSTIFY_ITEMS_INHERIT;

	UNUSED(style);

	if (hasFlagValue(opv) == false) {
		/* JUSTIFY_ITEMS_x + 1 == CSS_JUSTIFY_ITEMS_x */
		value = getValue(opv) + 1;
		if (value > CSS_JUSTIFY_ITEMS_BASELINE)
			value = CSS_JUSTIFY_ITEMS_STRETCH;
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_justify_items(state->computed, value);
	}

	return CSS_OK;
}

css_error css__set_justify_items_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_justify_items(style, hint->status);
}

css_error css__initial_justify_items(css_select_state *state)
{
	/* normal: stretch (in a grid; nothing for blocks) */
	return set_justify_items(state->computed, CSS_JUSTIFY_ITEMS_STRETCH);
}

css_error css__copy_justify_items(
		const css_computed_style *from,
		css_computed_style *to)
{
	if (from == to) {
		return CSS_OK;
	}

	return set_justify_items(to, get_justify_items(from));
}

css_error css__compose_justify_items(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	uint8_t type = get_justify_items(child);

	return css__copy_justify_items(
			type == CSS_JUSTIFY_ITEMS_INHERIT ? parent : child,
			result);
}

css_error css__cascade_justify_self(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_JUSTIFY_SELF_INHERIT;

	UNUSED(style);

	if (hasFlagValue(opv) == false) {
		/* JUSTIFY_SELF_x + 1 == CSS_JUSTIFY_SELF_x */
		value = getValue(opv) + 1;
		if (value > CSS_JUSTIFY_SELF_AUTO)
			value = CSS_JUSTIFY_SELF_AUTO;
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_justify_self(state->computed, value);
	}

	return CSS_OK;
}

css_error css__set_justify_self_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_justify_self(style, hint->status);
}

css_error css__initial_justify_self(css_select_state *state)
{
	return set_justify_self(state->computed, CSS_JUSTIFY_SELF_AUTO);
}

css_error css__copy_justify_self(
		const css_computed_style *from,
		css_computed_style *to)
{
	if (from == to) {
		return CSS_OK;
	}

	return set_justify_self(to, get_justify_self(from));
}

css_error css__compose_justify_self(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	uint8_t type = get_justify_self(child);

	return css__copy_justify_self(
			type == CSS_JUSTIFY_SELF_INHERIT ? parent : child,
			result);
}
