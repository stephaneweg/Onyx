/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: selection of more CSS3 properties -- aspect-ratio, object-fit, object-position, the
 * text properties (transform, translate, scale, rotate, the grid templates, auto tracks and
 * lines: a canonical text, see src/parse/properties/onyx_css3b.c), grid-auto-flow.
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propset.h"
#include "select/propget.h"
#include "utils/utils.h"

#include "select/properties/properties.h"
#include "select/properties/helpers.h"

/* ---- the text properties -------------------------------------------------------------------- */

static css_error onyx_cascade_text(uint32_t opv, css_style *style,
		css_select_state *state,
		css_error (*set)(css_computed_style *, uint8_t, lwc_string *))
{
	uint16_t value = CSS_ONYX_TEXT_INHERIT;
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
			getFlagValue(opv))) {
		return set(state->computed, value, text);
	}

	return CSS_OK;
}

#define ONYX_TEXT_PROP(pname)							\
css_error css__cascade_##pname(uint32_t opv, css_style *style,			\
		css_select_state *state)					\
{									\
	return onyx_cascade_text(opv, style, state, set_##pname);		\
}									\
									\
css_error css__set_##pname##_from_hint(const css_hint *hint,			\
		css_computed_style *style)				\
{									\
	return set_##pname(style, hint->status, hint->data.string);		\
}									\
									\
css_error css__initial_##pname(css_select_state *state)			\
{									\
	return set_##pname(state->computed, CSS_ONYX_TEXT_NONE, NULL);	\
}									\
									\
css_error css__copy_##pname(const css_computed_style *from,		\
		css_computed_style *to)					\
{									\
	lwc_string *text = NULL;					\
	uint8_t type = get_##pname(from, &text);			\
									\
	if (from == to) {						\
		return CSS_OK;						\
	}								\
									\
	return set_##pname(to, type, text);				\
}									\
									\
css_error css__compose_##pname(const css_computed_style *parent,		\
		const css_computed_style *child,			\
		css_computed_style *result)				\
{									\
	lwc_string *text = NULL;					\
	uint8_t type = get_##pname(child, &text);			\
									\
	return css__copy_##pname(						\
			type == CSS_ONYX_TEXT_INHERIT ? parent : child,	\
			result);					\
}

ONYX_TEXT_PROP(transform)
ONYX_TEXT_PROP(translate)
ONYX_TEXT_PROP(scale)
ONYX_TEXT_PROP(rotate)
ONYX_TEXT_PROP(grid_template_columns)
ONYX_TEXT_PROP(grid_template_rows)
ONYX_TEXT_PROP(grid_template_areas)
ONYX_TEXT_PROP(grid_auto_columns)
ONYX_TEXT_PROP(grid_auto_rows)
ONYX_TEXT_PROP(grid_row_start)
ONYX_TEXT_PROP(grid_row_end)
ONYX_TEXT_PROP(grid_column_start)
ONYX_TEXT_PROP(grid_column_end)
ONYX_TEXT_PROP(mask_image)	/* Onyx: mask */
ONYX_TEXT_PROP(mask_size)
ONYX_TEXT_PROP(mask_position)
ONYX_TEXT_PROP(mask_repeat)
ONYX_TEXT_PROP(transition_property)	/* Onyx: transitions, animations */
ONYX_TEXT_PROP(transition_duration)
ONYX_TEXT_PROP(transition_timing_function)
ONYX_TEXT_PROP(transition_delay)
ONYX_TEXT_PROP(animation_name)
ONYX_TEXT_PROP(animation_duration)
ONYX_TEXT_PROP(animation_timing_function)
ONYX_TEXT_PROP(animation_delay)
ONYX_TEXT_PROP(animation_iteration_count)
ONYX_TEXT_PROP(animation_direction)
ONYX_TEXT_PROP(animation_fill_mode)
ONYX_TEXT_PROP(animation_play_state)

/* ---- aspect-ratio ---------------------------------------------------------------------------- */

css_error css__cascade_aspect_ratio(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_ASPECT_RATIO_INHERIT;
	css_fixed w = 0, h = INTTOFIX(1);

	if (hasFlagValue(opv) == false) {
		uint16_t v = getValue(opv);
		if (v & ASPECT_RATIO_SET) {
			w = *((css_fixed *) style->bytecode);
			advance_bytecode(style, sizeof(w));
			h = *((css_fixed *) style->bytecode);
			advance_bytecode(style, sizeof(h));
			value = (v & ASPECT_RATIO_AUTO_FLAG) ? CSS_ASPECT_RATIO_AUTO_SET :
					CSS_ASPECT_RATIO_SET;
		} else {
			value = CSS_ASPECT_RATIO_AUTO;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_aspect_ratio(state->computed, value, w, h);
	}

	return CSS_OK;
}

css_error css__set_aspect_ratio_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_aspect_ratio(style, hint->status, 0, INTTOFIX(1));
}

css_error css__initial_aspect_ratio(css_select_state *state)
{
	return set_aspect_ratio(state->computed, CSS_ASPECT_RATIO_AUTO, 0, INTTOFIX(1));
}

css_error css__copy_aspect_ratio(const css_computed_style *from,
		css_computed_style *to)
{
	css_fixed w, h;
	uint8_t type = get_aspect_ratio(from, &w, &h);

	if (from == to) {
		return CSS_OK;
	}

	return set_aspect_ratio(to, type, w, h);
}

css_error css__compose_aspect_ratio(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_fixed w, h;
	uint8_t type = get_aspect_ratio(child, &w, &h);

	return css__copy_aspect_ratio(
			type == CSS_ASPECT_RATIO_INHERIT ? parent : child,
			result);
}

/* ---- object-fit ----------------------------------------------------------------------------- */

css_error css__cascade_object_fit(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_OBJECT_FIT_INHERIT;

	UNUSED(style);

	if (hasFlagValue(opv) == false) {
		/* OBJECT_FIT_x + 1 == CSS_OBJECT_FIT_x */
		value = getValue(opv) + 1;
		if (value > CSS_OBJECT_FIT_SCALE_DOWN)
			value = CSS_OBJECT_FIT_FILL;
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_object_fit(state->computed, value);
	}

	return CSS_OK;
}

css_error css__set_object_fit_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_object_fit(style, hint->status);
}

css_error css__initial_object_fit(css_select_state *state)
{
	return set_object_fit(state->computed, CSS_OBJECT_FIT_FILL);
}

css_error css__copy_object_fit(const css_computed_style *from,
		css_computed_style *to)
{
	if (from == to) {
		return CSS_OK;
	}

	return set_object_fit(to, get_object_fit(from));
}

css_error css__compose_object_fit(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	uint8_t type = get_object_fit(child);

	return css__copy_object_fit(
			type == CSS_OBJECT_FIT_INHERIT ? parent : child,
			result);
}

/* ---- object-position (background-position's bytecode) ------------------------------------ */

css_error css__cascade_object_position(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_OBJECT_POSITION_INHERIT;
	css_fixed hlength = INTTOFIX(50), vlength = INTTOFIX(50);
	uint32_t hunit = UNIT_PCT, vunit = UNIT_PCT;

	if (hasFlagValue(opv) == false) {
		value = CSS_OBJECT_POSITION_SET;

		switch (getValue(opv) & 0xf0) {
		case BACKGROUND_POSITION_HORZ_SET:
			hlength = *((css_fixed *) style->bytecode);
			advance_bytecode(style, sizeof(hlength));
			hunit = *((uint32_t *) style->bytecode);
			advance_bytecode(style, sizeof(hunit));
			break;
		case BACKGROUND_POSITION_HORZ_RIGHT:
			hlength = INTTOFIX(100);
			break;
		case BACKGROUND_POSITION_HORZ_LEFT:
			hlength = 0;
			break;
		default:
			break;
		}

		switch (getValue(opv) & 0x0f) {
		case BACKGROUND_POSITION_VERT_SET:
			vlength = *((css_fixed *) style->bytecode);
			advance_bytecode(style, sizeof(vlength));
			vunit = *((uint32_t *) style->bytecode);
			advance_bytecode(style, sizeof(vunit));
			break;
		case BACKGROUND_POSITION_VERT_BOTTOM:
			vlength = INTTOFIX(100);
			break;
		case BACKGROUND_POSITION_VERT_TOP:
			vlength = 0;
			break;
		default:
			break;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_object_position(state->computed, value,
				hlength, css__to_css_unit(hunit),
				vlength, css__to_css_unit(vunit));
	}

	return CSS_OK;
}

css_error css__set_object_position_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_object_position(style, hint->status,
		hint->data.position.h.value, hint->data.position.h.unit,
		hint->data.position.v.value, hint->data.position.v.unit);
}

css_error css__initial_object_position(css_select_state *state)
{
	return set_object_position(state->computed, CSS_OBJECT_POSITION_SET,
			INTTOFIX(50), CSS_UNIT_PCT, INTTOFIX(50), CSS_UNIT_PCT);
}

css_error css__copy_object_position(const css_computed_style *from,
		css_computed_style *to)
{
	css_fixed hlength = 0, vlength = 0;
	css_unit hunit = CSS_UNIT_PX, vunit = CSS_UNIT_PX;
	uint8_t type = get_object_position(from, &hlength, &hunit, &vlength, &vunit);

	if (from == to) {
		return CSS_OK;
	}

	return set_object_position(to, type, hlength, hunit, vlength, vunit);
}

css_error css__compose_object_position(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_fixed hlength = 0, vlength = 0;
	css_unit hunit = CSS_UNIT_PX, vunit = CSS_UNIT_PX;
	uint8_t type = get_object_position(child, &hlength, &hunit, &vlength, &vunit);

	return css__copy_object_position(
			type == CSS_OBJECT_POSITION_INHERIT ? parent : child,
			result);
}

/* ---- grid-auto-flow ------------------------------------------------------------------------- */

css_error css__cascade_grid_auto_flow(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_GRID_AUTO_FLOW_INHERIT;

	UNUSED(style);

	if (hasFlagValue(opv) == false) {
		uint16_t v = getValue(opv);
		if (v & GRID_AUTO_FLOW_COLUMN)
			value = (v & GRID_AUTO_FLOW_DENSE) ? CSS_GRID_AUTO_FLOW_COLUMN_DENSE :
					CSS_GRID_AUTO_FLOW_COLUMN;
		else
			value = (v & GRID_AUTO_FLOW_DENSE) ? CSS_GRID_AUTO_FLOW_ROW_DENSE :
					CSS_GRID_AUTO_FLOW_ROW;
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_grid_auto_flow(state->computed, value);
	}

	return CSS_OK;
}

css_error css__set_grid_auto_flow_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_grid_auto_flow(style, hint->status);
}

css_error css__initial_grid_auto_flow(css_select_state *state)
{
	return set_grid_auto_flow(state->computed, CSS_GRID_AUTO_FLOW_ROW);
}

css_error css__copy_grid_auto_flow(const css_computed_style *from,
		css_computed_style *to)
{
	if (from == to) {
		return CSS_OK;
	}

	return set_grid_auto_flow(to, get_grid_auto_flow(from));
}

css_error css__compose_grid_auto_flow(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	uint8_t type = get_grid_auto_flow(child);

	return css__copy_grid_auto_flow(
			type == CSS_GRID_AUTO_FLOW_INHERIT ? parent : child,
			result);
}


/* ---- background-clip (and -webkit-background-clip: text) ------------------------------------ */

css_error css__cascade_background_clip(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_BACKGROUND_CLIP_INHERIT;

	UNUSED(style);

	if (hasFlagValue(opv) == false) {
		/* BACKGROUND_CLIP_x + 1 == CSS_BACKGROUND_CLIP_x */
		value = getValue(opv) + 1;
		if (value > CSS_BACKGROUND_CLIP_TEXT)
			value = CSS_BACKGROUND_CLIP_BORDER_BOX;
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_background_clip(state->computed, value);
	}

	return CSS_OK;
}

css_error css__set_background_clip_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_background_clip(style, hint->status);
}

css_error css__initial_background_clip(css_select_state *state)
{
	return set_background_clip(state->computed, CSS_BACKGROUND_CLIP_BORDER_BOX);
}

css_error css__copy_background_clip(
		const css_computed_style *from,
		css_computed_style *to)
{
	if (from == to) {
		return CSS_OK;
	}

	return set_background_clip(to, get_background_clip(from));
}

css_error css__compose_background_clip(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	uint8_t type = get_background_clip(child);

	return css__copy_background_clip(
			type == CSS_BACKGROUND_CLIP_INHERIT ? parent : child,
			result);
}
