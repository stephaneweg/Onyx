/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 *
 * Copyright 2015 Michael Drake <tlsa@netsurf-browser.org>
 */

#include <string.h>

#include "select/arena.h"
#include "select/arena_hash.h"
#include "select/computed.h"
#include "select/onyx_propbits.h"

#define TU_SIZE 3037
#define TS_SIZE 5101

struct css_computed_style *table_s[TS_SIZE];


static inline uint32_t css__arena_hash_style(struct css_computed_style *s)
{
	return css__arena_hash((const uint8_t *) &s->i, sizeof(s->i));
}


static inline bool arena__compare_computed_content_item(
		const struct css_computed_content_item *a,
		const struct css_computed_content_item *b)
{
	if (a == NULL && b == NULL) {
		return true;

	} else if (a == NULL || b == NULL) {
		return false;
	}

	if (a->type != b->type) {
		return false;
	}

	return memcmp(a, b, sizeof(struct css_computed_content_item)) == 0;
}


static inline bool arena__compare_css_computed_counter(
		const struct css_computed_counter *a,
		const struct css_computed_counter *b)
{
	bool match;

	if (a == NULL && b == NULL) {
		return true;

	} else if (a == NULL || b == NULL) {
		return false;
	}

	if (a->value == b->value &&
			lwc_string_isequal(a->name, b->name,
					&match) == lwc_error_ok &&
			match == true) {
		return true;
	}

	return false;
}

static inline bool arena__compare_string_list(
		lwc_string **a,
		lwc_string **b)
{
	if (a == NULL && b == NULL) {
		return true;

	} else if (a == NULL || b == NULL) {
		return false;
	}

	while (*a != NULL && *b != NULL) {
		bool match;

		if (lwc_string_isequal(*a, *b, &match) != lwc_error_ok ||
				match == false) {
			return false;
		}

		a++;
		b++;
	}

	if (*a != *b) {
		return false;
	}

	return true;
}


static inline bool css__arena_style_is_equal(
		struct css_computed_style *a,
		struct css_computed_style *b)
{
	if (memcmp(&a->i, &b->i, sizeof(struct css_computed_style_i)) != 0) {
		return false;
	}

	if (!arena__compare_string_list(
			a->font_family,
			b->font_family)) {
		return false;
	}

	if (!arena__compare_css_computed_counter(
			a->counter_increment,
			b->counter_increment)) {
		return false;
	}

	if (!arena__compare_css_computed_counter(
			a->counter_reset,
			b->counter_reset)) {
		return false;
	}

	if (!arena__compare_computed_content_item(
			a->content,
			b->content)) {
		return false;
	}

	if (!arena__compare_string_list(
			a->cursor,
			b->cursor)) {
		return false;
	}

	if (!arena__compare_string_list(
			a->quotes,
			b->quotes)) {
		return false;
	}

	return true;
}


/* Internally exported function, documented in src/select/arena.h */
css_error css__arena_intern_style(struct css_computed_style **style)
{
	struct css_computed_style *s = *style;
	uint32_t hash, index;

	/* Don't try to intern an already-interned computed style */
	if (s->count != 0) {
		return CSS_BADPARM;
	}

	/* Need to intern the style block */
	hash = css__arena_hash_style(s);
	index = hash % TS_SIZE;
	s->bin = index;

	if (table_s[index] == NULL) {
		/* Can just insert */
		table_s[index] = s;
		s->count = 1;
	} else {
		/* Check for existing */
		struct css_computed_style *l = table_s[index];
		struct css_computed_style *existing = NULL;

		do {
			if (css__arena_style_is_equal(l, s)) {
				existing = l;
				break;
			}
			l = l->next;
		} while (l != NULL);

		if (existing != NULL) {
			css_computed_style_destroy(s);
			existing->count++;
			*style = existing;
		} else {
			/* Add to list */
			s->next = table_s[index];
			table_s[index] = s;
			s->count = 1;
		}
	}

	return CSS_OK;
}


/* Internally exported function, documented in src/select/arena.h */
enum css_error css__arena_remove_style(struct css_computed_style *style)
{
	uint32_t index = style->bin;

	if (table_s[index] == NULL) {
		return CSS_BADPARM;

	} else {
		/* Check for existing */
		struct css_computed_style *l = table_s[index];
		struct css_computed_style *existing = NULL;
		struct css_computed_style *prev = NULL;

		do {
			if (css__arena_style_is_equal(l, style)) {
				existing = l;
				break;
			}
			prev = l;
			l = l->next;
		} while (l != NULL);

		if (existing != NULL) {
			if (prev != NULL) {
				prev->next = existing->next;
			} else {
				table_s[index] = existing->next;
			}
		} else {
			return CSS_BADPARM;
		}
	}

	return CSS_OK;
}

/* Onyx: a paint property's bits (and value fields) of b copied into t */
#define PAINT_BITS(P) (t.bits[ONYX_##P##_INDEX] = 		(t.bits[ONYX_##P##_INDEX] & ~(uint32_t) ONYX_##P##_MASK) | 		(bi->bits[ONYX_##P##_INDEX] & (uint32_t) ONYX_##P##_MASK))

/* exported function documented in include/libcss/computed.h */
bool css_computed_style_paint_only_change(const css_computed_style *a,
		const css_computed_style *b, bool *moved)
{
	struct css_computed_style_i t;
	const struct css_computed_style_i *bi;

	if (moved != NULL)
		*moved = false;
	if (a == b)
		return true;
	if (a == NULL || b == NULL)
		return false;
	bi = &b->i;
	t = a->i;

	PAINT_BITS(COLOR);			t.color = bi->color;
	PAINT_BITS(BACKGROUND_COLOR);		t.background_color = bi->background_color;
	PAINT_BITS(BACKGROUND_IMAGE);		t.background_image = bi->background_image;
	PAINT_BITS(BACKGROUND_ATTACHMENT);
	PAINT_BITS(BACKGROUND_CLIP);
	PAINT_BITS(BACKGROUND_REPEAT);
	PAINT_BITS(BACKGROUND_POSITION);
	t.background_position_a = bi->background_position_a;
	t.background_position_b = bi->background_position_b;
	PAINT_BITS(BACKGROUND_SIZE);
	t.background_size_a = bi->background_size_a;
	t.background_size_b = bi->background_size_b;
	PAINT_BITS(BORDER_TOP_COLOR);		t.border_top_color = bi->border_top_color;
	PAINT_BITS(BORDER_RIGHT_COLOR);		t.border_right_color = bi->border_right_color;
	PAINT_BITS(BORDER_BOTTOM_COLOR);	t.border_bottom_color = bi->border_bottom_color;
	PAINT_BITS(BORDER_LEFT_COLOR);		t.border_left_color = bi->border_left_color;
	PAINT_BITS(BORDER_TOP_LEFT_RADIUS);
	t.border_top_left_radius = bi->border_top_left_radius;
	PAINT_BITS(BORDER_TOP_RIGHT_RADIUS);
	t.border_top_right_radius = bi->border_top_right_radius;
	PAINT_BITS(BORDER_BOTTOM_LEFT_RADIUS);
	t.border_bottom_left_radius = bi->border_bottom_left_radius;
	PAINT_BITS(BORDER_BOTTOM_RIGHT_RADIUS);
	t.border_bottom_right_radius = bi->border_bottom_right_radius;
	PAINT_BITS(BOX_SHADOW);
	t.box_shadow_a = bi->box_shadow_a;
	t.box_shadow_b = bi->box_shadow_b;
	t.box_shadow_c = bi->box_shadow_c;
	t.box_shadow_d = bi->box_shadow_d;
	t.box_shadow_e = bi->box_shadow_e;
	PAINT_BITS(TEXT_SHADOW);
	t.text_shadow_a = bi->text_shadow_a;
	t.text_shadow_b = bi->text_shadow_b;
	t.text_shadow_c = bi->text_shadow_c;
	t.text_shadow_d = bi->text_shadow_d;
	PAINT_BITS(COLUMN_RULE_COLOR);		t.column_rule_color = bi->column_rule_color;
	PAINT_BITS(OUTLINE_COLOR);		t.outline_color = bi->outline_color;
	PAINT_BITS(OUTLINE_STYLE);
	PAINT_BITS(OUTLINE_WIDTH);		t.outline_width = bi->outline_width;
	PAINT_BITS(TEXT_DECORATION);
	PAINT_BITS(VISIBILITY);
	PAINT_BITS(OPACITY);			t.opacity = bi->opacity;
	PAINT_BITS(FILL_OPACITY);		t.fill_opacity = bi->fill_opacity;
	PAINT_BITS(STROKE_OPACITY);		t.stroke_opacity = bi->stroke_opacity;
	/* Onyx: SVG's presentation properties */
	PAINT_BITS(FILL);	t.fill_a = bi->fill_a;		t.fill_b = bi->fill_b;
	PAINT_BITS(STROKE);	t.stroke_a = bi->stroke_a;	t.stroke_b = bi->stroke_b;
	PAINT_BITS(STROKE_WIDTH);		t.stroke_width = bi->stroke_width;
	PAINT_BITS(STROKE_DASHOFFSET);		t.stroke_dashoffset = bi->stroke_dashoffset;
	PAINT_BITS(STROKE_DASHARRAY);		t.stroke_dasharray = bi->stroke_dasharray;
	PAINT_BITS(STROKE_MITERLIMIT);		t.stroke_miterlimit = bi->stroke_miterlimit;
	PAINT_BITS(STROKE_LINECAP);
	PAINT_BITS(STROKE_LINEJOIN);
	PAINT_BITS(FILL_RULE);
	PAINT_BITS(STOP_COLOR);			t.stop_color = bi->stop_color;
	PAINT_BITS(STOP_OPACITY);		t.stop_opacity = bi->stop_opacity;
	PAINT_BITS(Z_INDEX);			t.z_index = bi->z_index;
	/* Onyx: transitions and animations (they change no box) */
	PAINT_BITS(TRANSITION_PROPERTY);	t.transition_property = bi->transition_property;
	PAINT_BITS(TRANSITION_DURATION);	t.transition_duration = bi->transition_duration;
	PAINT_BITS(TRANSITION_TIMING_FUNCTION);	t.transition_timing_function = bi->transition_timing_function;
	PAINT_BITS(TRANSITION_DELAY);	t.transition_delay = bi->transition_delay;
	PAINT_BITS(ANIMATION_NAME);	t.animation_name = bi->animation_name;
	PAINT_BITS(ANIMATION_DURATION);	t.animation_duration = bi->animation_duration;
	PAINT_BITS(ANIMATION_TIMING_FUNCTION);	t.animation_timing_function = bi->animation_timing_function;
	PAINT_BITS(ANIMATION_DELAY);	t.animation_delay = bi->animation_delay;
	PAINT_BITS(ANIMATION_ITERATION_COUNT);	t.animation_iteration_count = bi->animation_iteration_count;
	PAINT_BITS(ANIMATION_DIRECTION);	t.animation_direction = bi->animation_direction;
	PAINT_BITS(ANIMATION_FILL_MODE);	t.animation_fill_mode = bi->animation_fill_mode;
	PAINT_BITS(ANIMATION_PLAY_STATE);	t.animation_play_state = bi->animation_play_state;
	PAINT_BITS(CURSOR);

	PAINT_BITS(ROTATE);			t.rotate = bi->rotate;	/* (not drawn) */
	PAINT_BITS(SCALE);			t.scale = bi->scale;	/* (not drawn) */
	if (moved != NULL) {
		/* a translation: the box moved, its layout the same */
		PAINT_BITS(TRANSFORM);		t.transform = bi->transform;
		PAINT_BITS(TRANSLATE);		t.translate = bi->translate;
		*moved = t.transform != a->i.transform || t.translate != a->i.translate ||
			(t.bits[ONYX_TRANSFORM_INDEX] & ONYX_TRANSFORM_MASK) !=
			(a->i.bits[ONYX_TRANSFORM_INDEX] & ONYX_TRANSFORM_MASK) ||
			(t.bits[ONYX_TRANSLATE_INDEX] & ONYX_TRANSLATE_MASK) !=
			(a->i.bits[ONYX_TRANSLATE_INDEX] & ONYX_TRANSLATE_MASK);
	}

	if (memcmp(&t, bi, sizeof t) != 0)
		return false;
	/* the lists: all but the cursor's */
	return arena__compare_string_list(a->font_family, b->font_family) &&
		arena__compare_css_computed_counter(a->counter_increment,
				b->counter_increment) &&
		arena__compare_css_computed_counter(a->counter_reset,
				b->counter_reset) &&
		arena__compare_computed_content_item(a->content, b->content) &&
		arena__compare_string_list(a->quotes, b->quotes);
}
