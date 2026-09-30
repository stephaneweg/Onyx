/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: what CSS transitions and animations need of the computed styles (NetSurf's
 * html/onyx_anim.c runs the timeline):
 *
 *   - a keyframe's (or a script's keyframe's) declarations applied over an element's style,
 *     as the cascade would (css_computed_style_onyx_apply);
 *   - a property's value between two styles' at a progress p, into a style being made
 *     (css_computed_style_onyx_blend): colours in premultiplied RGBA, lengths of one unit,
 *     numbers, the transform texts function by function (else through their 2D matrices),
 *     the filters' function lists,
 *     box-shadow, visibility; any other property flips at p = 0.5 (discrete);
 *   - whether two styles have the same value of a property (css_computed_style_onyx_same);
 *   - the @keyframes of a selection context's sheets (css_select_ctx_onyx_keyframes, in
 *     select.c) and an inline sheet's declarations (css_stylesheet_onyx_inline_decls).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/arena.h"
#include "select/computed.h"
#include "select/dispatch.h"
#include "select/propget.h"
#include "select/propset.h"
#include "select/select.h"
#include "parse/onyx_atrules.h"
#include "parse/onyx_vars.h"
#include "stylesheet.h"
#include "utils/utils.h"

/* exported function documented in include/libcss/computed.h */
css_computed_style *css_computed_style_onyx_ref(const css_computed_style *style)
{
	css_computed_style *s = (css_computed_style *) style;

	if (s != NULL && s->count > 0)
		s->count++;
	return s;
}

/* exported function documented in include/libcss/computed.h */
css_error css_computed_style_onyx_clone(const css_computed_style *base,
		css_computed_style **work)
{
	if (base == NULL || work == NULL)
		return CSS_BADPARM;
	return css__computed_style_clone(base, work);
}

/* exported function documented in include/libcss/computed.h */
css_error css_computed_style_onyx_intern(css_computed_style **work)
{
	if (work == NULL || *work == NULL)
		return CSS_BADPARM;
	return css__arena_intern_style(work);
}

/* exported function documented in include/libcss/computed.h */
css_error css_computed_style_onyx_apply(const css_computed_style *base,
		const css_computed_style *parent, const void *decls,
		const css_unit_ctx *unit_ctx, css_computed_style **out, uint8_t *set)
{
	css_select_state *state;
	css_computed_style *s = NULL;
	css_style st;
	css_error error;
	uint32_t i;

	if (base == NULL || out == NULL || unit_ctx == NULL)
		return CSS_BADPARM;
	if (set != NULL)
		memset(set, 0, CSS_N_PROPERTIES);
	error = css__computed_style_clone(base, &s);
	if (error != CSS_OK)
		return error;
	state = calloc(1, sizeof(*state));
	if (state == NULL) {
		css_computed_style_destroy(s);
		return CSS_NOMEM;
	}
	state->computed = s;
	state->unit_ctx = unit_ctx;
	state->current_pseudo = CSS_PSEUDO_ELEMENT_NONE;
	state->current_origin = CSS_ORIGIN_AUTHOR;
	state->current_specificity = 0;

	if (decls != NULL) {
		st = *(const css_style *) decls;
		while (st.used > 0) {
			css_code_t opv = *st.bytecode;
			opcode_t op;

			advance_bytecode(&st, sizeof(opv));
			op = getOpcode(opv);
			if (op >= CSS_N_PROPERTIES) {
				if (op == CSS_ONYX_OP_GENERIC)
					continue;
				break;		/* (a custom property, var(): not taken) */
			}
			error = prop_dispatch[op].cascade(opv, &st, state);
			if (error != CSS_OK)
				break;
		}
	}

	/* the CSS-wide keywords: from the parent, or the initial value */
	for (i = 0; i < CSS_N_PROPERTIES; i++) {
		prop_state *ps = &state->props[i][CSS_PSEUDO_ELEMENT_NONE];
		enum flag_value f;

		if (!ps->set)
			continue;
		if (set != NULL)
			set[i] = 1;
		f = ps->explicit_default;
		if (f == FLAG_VALUE_UNSET)
			f = prop_dispatch[i].inherited ? FLAG_VALUE_INHERIT : FLAG_VALUE_INITIAL;
		if (f == FLAG_VALUE_INHERIT && parent != NULL)
			prop_dispatch[i].copy(parent, s);
		else if (f == FLAG_VALUE_INHERIT || f == FLAG_VALUE_INITIAL ||
				f == FLAG_VALUE_REVERT)
			prop_dispatch[i].initial(state);
	}
	free(state);

	error = css__compute_absolute_values(parent, s, unit_ctx);
	if (error != CSS_OK) {
		css_computed_style_destroy(s);
		return error;
	}
	error = css__arena_intern_style(&s);
	if (error != CSS_OK)
		return error;
	*out = s;
	return CSS_OK;
}

/* exported function documented in include/libcss/computed.h */
const void *css_stylesheet_onyx_inline_decls(const css_stylesheet *sheet)
{
	const css_rule *r;

	if (sheet == NULL)
		return NULL;
	for (r = sheet->rule_list; r != NULL; r = r->next)
		if (r->type == CSS_RULE_SELECTOR)
			return ((const css_rule_selector *) r)->style;
	return NULL;
}

/* ---- @keyframes -------------------------------------------------------------------------------- */

static bool onyx_name_is(lwc_string *a, lwc_string *b)
{
	bool match = false;

	if (a == NULL || b == NULL)
		return false;
	if (a == b)
		return true;
	/* (a name from a string or an identifier: case-sensitive) */
	return lwc_string_isequal(a, b, &match) == lwc_error_ok && match;
}

/* The last @keyframes <name> in a rule list (and in its @media / @supports / @layer
 * groups, the @imports' sheets). */
static void onyx_find_keyframes(const css_rule *r, lwc_string *name, int depth,
		const css_rule_media **found)
{
	if (depth > 16)
		return;
	for (; r != NULL; r = r->next) {
		if (r->type == CSS_RULE_IMPORT) {
			const css_rule_import *im = (const css_rule_import *) r;
			if (im->sheet != NULL)
				onyx_find_keyframes(im->sheet->rule_list, name, depth + 1, found);
		} else if (r->type == CSS_RULE_MEDIA) {
			const css_rule_media *m = (const css_rule_media *) r;
			if (m->onyx_kind == ONYX_AT_KEYFRAMES) {
				if (onyx_name_is(m->onyx_name, name))
					*found = m;
			} else if (m->onyx_kind == ONYX_AT_NONE ||
					m->onyx_kind == ONYX_AT_SCOPE) {
				onyx_find_keyframes(m->first_child, name, depth + 1, found);
			}
		}
	}
}

/* Documented in select/select.h */
void css__onyx_keyframes_in_sheet(const css_stylesheet *sheet, lwc_string *name,
		const void **found)
{
	if (sheet != NULL)
		onyx_find_keyframes(sheet->rule_list, name, 0, (const css_rule_media **) found);
}

static int onyx_kf_cmp(const void *a, const void *b)
{
	const css_onyx_keyframe *x = a, *y = b;
	if (x->offset < y->offset)
		return -1;
	if (x->offset > y->offset)
		return 1;
	return x->order < y->order ? -1 : x->order > y->order;
}

/* Documented in select/select.h */
css_error css__onyx_keyframes_list(const void *rule, css_onyx_keyframe **out, uint32_t *n)
{
	const css_rule_media *kfs = rule;
	const css_rule *r;
	css_onyx_keyframe *list;
	uint32_t count = 0, k = 0, i;

	*out = NULL;
	*n = 0;
	if (kfs == NULL)
		return CSS_OK;
	for (r = kfs->first_child; r != NULL; r = r->next)
		if (r->type == CSS_RULE_MEDIA)
			count += ((const css_rule_media *) r)->onyx_n_offsets;
	if (count == 0)
		return CSS_OK;
	list = calloc(count, sizeof(*list));
	if (list == NULL)
		return CSS_NOMEM;
	for (r = kfs->first_child; r != NULL; r = r->next) {
		const css_rule_media *kf = (const css_rule_media *) r;
		if (r->type != CSS_RULE_MEDIA)
			continue;
		for (i = 0; i < kf->onyx_n_offsets && k < count; i++) {
			list[k].offset = kf->onyx_offsets[i];
			list[k].decls = kf->onyx_style;
			list[k].order = k;
			k++;
		}
	}
	qsort(list, k, sizeof(*list), onyx_kf_cmp);
	*out = list;
	*n = k;
	return CSS_OK;
}

/* ---- values between two styles ------------------------------------------------------------ */

static float lerpf(float a, float b, float p)
{
	return a + (b - a) * p;
}

static css_fixed lerpx(css_fixed a, css_fixed b, float p)
{
	return FLTTOFIX(lerpf(FIXTOFLT(a), FIXTOFLT(b), p));
}

/* colours: premultiplied RGBA */
static css_color blend_color(css_color a, css_color b, float p)
{
	float aa = ((a >> 24) & 0xff) / 255.0f, ba = ((b >> 24) & 0xff) / 255.0f;
	float oa = lerpf(aa, ba, p);
	unsigned int out = 0;
	int sh;

	if (oa <= 0)
		return 0;
	if (oa > 1)
		oa = 1;
	for (sh = 0; sh <= 16; sh += 8) {
		float ca = ((a >> sh) & 0xff) * aa, cb = ((b >> sh) & 0xff) * ba;
		float v = lerpf(ca, cb, p) / oa;
		int iv = (int) (v + 0.5f);
		if (iv < 0) iv = 0;
		if (iv > 255) iv = 255;
		out |= (unsigned int) iv << sh;
	}
	out |= (unsigned int) (oa * 255 + 0.5f) << 24;
	return out;
}

#define ONYX_COLOR(P, OK)							\
	case CSS_PROP_##P: {							\
		css_color ca = 0, cb = 0;					\
		uint8_t ta = get_##P##_lc(a, &ca), tb = get_##P##_lc(b, &cb);	\
		if (ta == (OK) && tb == (OK)) {					\
			if (same) return ca == cb;				\
			return set_##P##_lc(work, OK, blend_color(ca, cb, p));	\
		}								\
		break;								\
	}

/* the lengths: (type SET, a unit) -- both of one unit, interpolated */
#define ONYX_LENGTH(P, OK)							\
	case CSS_PROP_##P: {							\
		css_fixed la = 0, lb = 0;					\
		css_unit ua = CSS_UNIT_PX, ub = CSS_UNIT_PX;			\
		uint8_t ta = get_##P##_lc(a, &la, &ua), tb = get_##P##_lc(b, &lb, &ub);\
		if (same) return ta == tb && (ta != (OK) || (la == lb && ua == ub)); \
		if (ta == (OK) && tb == (OK) && ua == ub)			\
			return set_##P##_lc(work, OK, lerpx(la, lb, p), ua);	\
		break;								\
	}

#define ONYX_FIXED(P, OK)							\
	case CSS_PROP_##P: {							\
		css_fixed fa = 0, fb = 0;					\
		uint8_t ta = get_##P##_lc(a, &fa), tb = get_##P##_lc(b, &fb);	\
		if (same) return ta == tb && (ta != (OK) || fa == fb);		\
		if (ta == (OK) && tb == (OK))					\
			return set_##P##_lc(work, OK, lerpx(fa, fb, p));	\
		break;								\
	}

/* lower-case aliases of the generated accessors (the macros paste the property's name) */
#define get_COLOR_lc get_color
#define set_COLOR_lc set_color
#define get_BACKGROUND_COLOR_lc get_background_color
#define set_BACKGROUND_COLOR_lc set_background_color
#define get_BORDER_TOP_COLOR_lc get_border_top_color
#define set_BORDER_TOP_COLOR_lc set_border_top_color
#define get_BORDER_RIGHT_COLOR_lc get_border_right_color
#define set_BORDER_RIGHT_COLOR_lc set_border_right_color
#define get_BORDER_BOTTOM_COLOR_lc get_border_bottom_color
#define set_BORDER_BOTTOM_COLOR_lc set_border_bottom_color
#define get_BORDER_LEFT_COLOR_lc get_border_left_color
#define set_BORDER_LEFT_COLOR_lc set_border_left_color
#define get_OUTLINE_COLOR_lc get_outline_color
#define set_OUTLINE_COLOR_lc set_outline_color
#define get_COLUMN_RULE_COLOR_lc get_column_rule_color
#define set_COLUMN_RULE_COLOR_lc set_column_rule_color
#define get_STOP_COLOR_lc get_stop_color
#define set_STOP_COLOR_lc set_stop_color
#define get_MARGIN_TOP_lc get_margin_top
#define set_MARGIN_TOP_lc set_margin_top
#define get_MARGIN_RIGHT_lc get_margin_right
#define set_MARGIN_RIGHT_lc set_margin_right
#define get_MARGIN_BOTTOM_lc get_margin_bottom
#define set_MARGIN_BOTTOM_lc set_margin_bottom
#define get_MARGIN_LEFT_lc get_margin_left
#define set_MARGIN_LEFT_lc set_margin_left
#define get_PADDING_TOP_lc get_padding_top
#define set_PADDING_TOP_lc set_padding_top
#define get_PADDING_RIGHT_lc get_padding_right
#define set_PADDING_RIGHT_lc set_padding_right
#define get_PADDING_BOTTOM_lc get_padding_bottom
#define set_PADDING_BOTTOM_lc set_padding_bottom
#define get_PADDING_LEFT_lc get_padding_left
#define set_PADDING_LEFT_lc set_padding_left
#define get_TOP_lc get_top
#define set_TOP_lc set_top
#define get_RIGHT_lc get_right
#define set_RIGHT_lc set_right
#define get_BOTTOM_lc get_bottom
#define set_BOTTOM_lc set_bottom
#define get_LEFT_lc get_left
#define set_LEFT_lc set_left
#define get_HEIGHT_lc get_height
#define set_HEIGHT_lc set_height
#define get_MIN_WIDTH_lc get_min_width
#define set_MIN_WIDTH_lc set_min_width
#define get_MIN_HEIGHT_lc get_min_height
#define set_MIN_HEIGHT_lc set_min_height
#define get_MAX_WIDTH_lc get_max_width
#define set_MAX_WIDTH_lc set_max_width
#define get_MAX_HEIGHT_lc get_max_height
#define set_MAX_HEIGHT_lc set_max_height
#define get_BORDER_TOP_WIDTH_lc get_border_top_width
#define set_BORDER_TOP_WIDTH_lc set_border_top_width
#define get_BORDER_RIGHT_WIDTH_lc get_border_right_width
#define set_BORDER_RIGHT_WIDTH_lc set_border_right_width
#define get_BORDER_BOTTOM_WIDTH_lc get_border_bottom_width
#define set_BORDER_BOTTOM_WIDTH_lc set_border_bottom_width
#define get_BORDER_LEFT_WIDTH_lc get_border_left_width
#define set_BORDER_LEFT_WIDTH_lc set_border_left_width
#define get_OUTLINE_WIDTH_lc get_outline_width
#define set_OUTLINE_WIDTH_lc set_outline_width
#define get_BORDER_TOP_LEFT_RADIUS_lc get_border_top_left_radius
#define set_BORDER_TOP_LEFT_RADIUS_lc set_border_top_left_radius
#define get_BORDER_TOP_RIGHT_RADIUS_lc get_border_top_right_radius
#define set_BORDER_TOP_RIGHT_RADIUS_lc set_border_top_right_radius
#define get_BORDER_BOTTOM_LEFT_RADIUS_lc get_border_bottom_left_radius
#define set_BORDER_BOTTOM_LEFT_RADIUS_lc set_border_bottom_left_radius
#define get_BORDER_BOTTOM_RIGHT_RADIUS_lc get_border_bottom_right_radius
#define set_BORDER_BOTTOM_RIGHT_RADIUS_lc set_border_bottom_right_radius
#define get_LETTER_SPACING_lc get_letter_spacing
#define set_LETTER_SPACING_lc set_letter_spacing
#define get_WORD_SPACING_lc get_word_spacing
#define set_WORD_SPACING_lc set_word_spacing
#define get_TEXT_INDENT_lc get_text_indent
#define set_TEXT_INDENT_lc set_text_indent
#define get_FONT_SIZE_lc get_font_size
#define set_FONT_SIZE_lc set_font_size
#define get_COLUMN_GAP_lc get_column_gap
#define set_COLUMN_GAP_lc set_column_gap
#define get_ROW_GAP_lc get_row_gap
#define set_ROW_GAP_lc set_row_gap
#define get_FLEX_BASIS_lc get_flex_basis
#define set_FLEX_BASIS_lc set_flex_basis
#define get_STROKE_WIDTH_lc get_stroke_width
#define set_STROKE_WIDTH_lc set_stroke_width
#define get_OPACITY_lc get_opacity
#define set_OPACITY_lc set_opacity
#define get_FILL_OPACITY_lc get_fill_opacity
#define set_FILL_OPACITY_lc set_fill_opacity
#define get_STROKE_OPACITY_lc get_stroke_opacity
#define set_STROKE_OPACITY_lc set_stroke_opacity
#define get_STOP_OPACITY_lc get_stop_opacity
#define set_STOP_OPACITY_lc set_stop_opacity
#define get_FLEX_GROW_lc get_flex_grow
#define set_FLEX_GROW_lc set_flex_grow
#define get_FLEX_SHRINK_lc get_flex_shrink
#define set_FLEX_SHRINK_lc set_flex_shrink

/* ---- the transform texts ---- */

#define TF_MAX 16

typedef struct tf_fn {
	char name[12];
	int n;
	float v[6];
	char unit[6][4];
} tf_fn;

/* the canonical text (src/parse/properties/onyx_css3b.c) into functions; -1 if unread */
static int tf_parse(const char *t, tf_fn *fns)
{
	int n = 0;

	while (t != NULL && *t != '\0') {
		const char *open = strchr(t, '(');
		tf_fn *f;
		size_t len;

		while (*t == ' ')
			t++;
		if (*t == '\0')
			break;
		if (open == NULL || n == TF_MAX)
			return -1;
		f = &fns[n];
		len = (size_t) (open - t);
		if (len == 0 || len >= sizeof(f->name))
			return -1;
		memcpy(f->name, t, len);
		f->name[len] = '\0';
		f->n = 0;
		t = open + 1;
		while (*t != ')' && *t != '\0') {
			char *e;
			size_t ul = 0;
			if (f->n == 6)
				return -1;
			f->v[f->n] = strtof(t, &e);
			if (e == t)
				return -1;
			t = e;
			while (t[ul] != ',' && t[ul] != ')' && t[ul] != '\0' && ul < 3)
				ul++;
			memcpy(f->unit[f->n], t, ul);
			f->unit[f->n][ul] = '\0';
			t += ul;
			f->n++;
			if (*t == ',')
				t++;
		}
		if (*t != ')')
			return -1;
		t++;
		n++;
	}
	return n;
}

/* the identity function of f's kind (for a list against none, or a shorter list) */
static void tf_identity(const tf_fn *f, tf_fn *id)
{
	int i;

	*id = *f;
	for (i = 0; i < id->n; i++)
		id->v[i] = 0;
	if (strcmp(f->name, "scale") == 0 || strcmp(f->name, "brightness") == 0 ||
			strcmp(f->name, "contrast") == 0 || strcmp(f->name, "opacity") == 0 ||
			strcmp(f->name, "saturate") == 0) {
		/* (and the filters whose identity is 1) */
		for (i = 0; i < id->n; i++)
			id->v[i] = 1;
	} else if (strcmp(f->name, "matrix") == 0) {
		id->v[0] = id->v[3] = 1;
	}
}

static bool tf_same_kind(const tf_fn *a, const tf_fn *b)
{
	int i;

	if (strcmp(a->name, b->name) != 0 || a->n != b->n)
		return false;
	for (i = 0; i < a->n; i++) {
		/* (a length 0 in any unit matches the other's unit) */
		if (strcmp(a->unit[i], b->unit[i]) != 0 && a->v[i] != 0 && b->v[i] != 0)
			return false;
	}
	return true;
}

/* a function list's 2D matrix (false: a length that is not in px) */
static bool tf_matrix(const tf_fn *fns, int n, float m[6])
{
	int k;

	m[0] = 1; m[1] = 0; m[2] = 0; m[3] = 1; m[4] = 0; m[5] = 0;
	for (k = 0; k < n; k++) {
		const tf_fn *f = &fns[k];
		float t[6] = { 1, 0, 0, 1, 0, 0 }, r[6];
		if (strcmp(f->name, "translate") == 0 && f->n == 2) {
			int i;
			for (i = 0; i < 2; i++)
				if (f->v[i] != 0 && strcmp(f->unit[i], "px") != 0)
					return false;
			t[4] = f->v[0];
			t[5] = f->v[1];
		} else if (strcmp(f->name, "scale") == 0 && f->n == 2) {
			t[0] = f->v[0];
			t[3] = f->v[1];
		} else if (strcmp(f->name, "rotate") == 0 && f->n == 1) {
			float a = f->v[0] * (float) M_PI / 180;
			t[0] = cosf(a); t[1] = sinf(a); t[2] = -sinf(a); t[3] = cosf(a);
		} else if (strcmp(f->name, "skew") == 0 && f->n == 2) {
			t[2] = tanf(f->v[0] * (float) M_PI / 180);
			t[1] = tanf(f->v[1] * (float) M_PI / 180);
		} else if (strcmp(f->name, "matrix") == 0 && f->n == 6) {
			memcpy(t, f->v, sizeof(t));
		} else {
			return false;
		}
		/* m = m * t */
		r[0] = m[0] * t[0] + m[2] * t[1];
		r[1] = m[1] * t[0] + m[3] * t[1];
		r[2] = m[0] * t[2] + m[2] * t[3];
		r[3] = m[1] * t[2] + m[3] * t[3];
		r[4] = m[0] * t[4] + m[2] * t[5] + m[4];
		r[5] = m[1] * t[4] + m[3] * t[5] + m[5];
		memcpy(m, r, sizeof(r));
	}
	return true;
}

/* CSS Transforms 1's 2D decomposition: translate, scale, angle (deg), the m11..m22 left */
typedef struct tf_dec {
	float tx, ty, sx, sy, angle, m11, m12, m21, m22;
} tf_dec;

static bool tf_decompose(const float m[6], tf_dec *d)
{
	float row0x = m[0], row0y = m[1], row1x = m[2], row1y = m[3];
	float det;

	d->tx = m[4];
	d->ty = m[5];
	d->sx = sqrtf(row0x * row0x + row0y * row0y);
	d->sy = sqrtf(row1x * row1x + row1y * row1y);
	det = row0x * row1y - row0y * row1x;
	if (det < 0) {
		if (row0x < row1y)
			d->sx = -d->sx;
		else
			d->sy = -d->sy;
	}
	if (d->sx != 0) {
		row0x /= d->sx;
		row0y /= d->sx;
	}
	if (d->sy != 0) {
		row1x /= d->sy;
		row1y /= d->sy;
	}
	d->angle = atan2f(row0y, row0x);
	if (d->angle != 0) {
		float sn = -row0y, cs = row0x;
		float m11 = row0x, m12 = row0y, m21 = row1x, m22 = row1y;
		row0x = cs * m11 + sn * m21;
		row0y = cs * m12 + sn * m22;
		row1x = -sn * m11 + cs * m21;
		row1y = -sn * m12 + cs * m22;
	}
	d->m11 = row0x; d->m12 = row0y; d->m21 = row1x; d->m22 = row1y;
	d->angle *= 180 / (float) M_PI;
	return true;
}

static void tf_recompose(const tf_dec *d, float m[6])
{
	float a = d->angle * (float) M_PI / 180, cs = cosf(a), sn = sinf(a);
	float r0x = d->m11, r0y = d->m12, r1x = d->m21, r1y = d->m22;
	/* rotate(angle) * [m11 m12; m21 m22] then scale */
	float n11 = cs * r0x - sn * r0y, n12 = sn * r0x + cs * r0y;
	float n21 = cs * r1x - sn * r1y, n22 = sn * r1x + cs * r1y;

	m[0] = n11 * d->sx;
	m[1] = n12 * d->sx;
	m[2] = n21 * d->sy;
	m[3] = n22 * d->sy;
	m[4] = d->tx;
	m[5] = d->ty;
}

typedef struct tf_out {
	char buf[512];
	size_t n;
} tf_out;

static void tf_put(tf_out *o, const char *s)
{
	size_t l = strlen(s);
	if (o->n + l + 1 >= sizeof(o->buf))
		return;
	memcpy(o->buf + o->n, s, l + 1);
	o->n += l;
}

static void tf_num(tf_out *o, float v)
{
	char tmp[48], *e;
	snprintf(tmp, sizeof(tmp), "%.4f", v);
	e = tmp + strlen(tmp) - 1;
	while (e > tmp && *e == '0')
		*e-- = '\0';
	if (*e == '.')
		*e = '\0';
	if (strcmp(tmp, "-0") == 0)
		strcpy(tmp, "0");
	tf_put(o, tmp);
}

static void tf_write(tf_out *o, const tf_fn *f)
{
	int i;

	if (o->n > 0)
		tf_put(o, " ");
	tf_put(o, f->name);
	tf_put(o, "(");
	for (i = 0; i < f->n; i++) {
		if (i > 0)
			tf_put(o, ",");
		tf_num(o, f->v[i]);
		tf_put(o, f->unit[i]);
	}
	tf_put(o, ")");
}

/* the text between a's and b's transform texts (NULL: none) at p; false if they cannot be
 * interpolated (then discrete) */
static bool tf_blend(const char *ta, const char *tb, float p, tf_out *o)
{
	tf_fn fa[TF_MAX], fb[TF_MAX];
	int na = ta != NULL ? tf_parse(ta, fa) : 0, nb = tb != NULL ? tf_parse(tb, fb) : 0;
	int n, k;
	bool match = true;

	o->n = 0;
	o->buf[0] = '\0';
	if (na < 0 || nb < 0)
		return false;
	n = na > nb ? na : nb;
	/* the shorter list completed with identity functions of the other's kinds */
	for (k = 0; k < n; k++) {
		if (k >= na)
			tf_identity(&fb[k], &fa[k]);
		else if (k >= nb)
			tf_identity(&fa[k], &fb[k]);
		if (!tf_same_kind(&fa[k], &fb[k]))
			match = false;
	}
	if (match) {
		for (k = 0; k < n; k++) {
			tf_fn f = fa[k];
			int i;
			for (i = 0; i < f.n; i++) {
				f.v[i] = lerpf(fa[k].v[i], fb[k].v[i], p);
				if (fa[k].v[i] == 0)
					strcpy(f.unit[i], fb[k].unit[i]);
			}
			tf_write(o, &f);
		}
		return true;
	} else {
		/* through the matrices (px only) */
		float ma[6], mb[6], m[6];
		tf_dec da, db, d;
		tf_fn f;
		if (!tf_matrix(fa, n, ma) || !tf_matrix(fb, n, mb))
			return false;
		tf_decompose(ma, &da);
		tf_decompose(mb, &db);
		if ((da.sx < 0 && db.sy < 0) || (da.sy < 0 && db.sx < 0)) {
			da.sx = -da.sx;
			da.sy = -da.sy;
			da.angle += da.angle < 0 ? 180 : -180;
		}
		if (fabsf(da.angle - db.angle) > 180) {
			if (da.angle > db.angle)
				da.angle -= 360;
			else
				db.angle -= 360;
		}
		d.tx = lerpf(da.tx, db.tx, p);
		d.ty = lerpf(da.ty, db.ty, p);
		d.sx = lerpf(da.sx, db.sx, p);
		d.sy = lerpf(da.sy, db.sy, p);
		d.angle = lerpf(da.angle, db.angle, p);
		d.m11 = lerpf(da.m11, db.m11, p);
		d.m12 = lerpf(da.m12, db.m12, p);
		d.m21 = lerpf(da.m21, db.m21, p);
		d.m22 = lerpf(da.m22, db.m22, p);
		tf_recompose(&d, m);
		strcpy(f.name, "matrix");
		f.n = 6;
		for (k = 0; k < 6; k++) {
			f.v[k] = m[k];
			f.unit[k][0] = '\0';
		}
		tf_write(o, &f);
		return true;
	}
}

#define ONYX_TEXT_BLEND(P, lp)							\
	case CSS_PROP_##P: {							\
		lwc_string *sa = NULL, *sb = NULL, *s;				\
		uint8_t ta = get_##lp(a, &sa), tb = get_##lp(b, &sb);		\
		tf_out o;							\
		if (ta != CSS_ONYX_TEXT_SET) sa = NULL;				\
		if (tb != CSS_ONYX_TEXT_SET) sb = NULL;				\
		if (same) return sa == sb;					\
		if (!tf_blend(sa != NULL ? lwc_string_data(sa) : NULL,		\
				sb != NULL ? lwc_string_data(sb) : NULL, p, &o))	\
			break;							\
		if (o.n == 0)							\
			return set_##lp(work, CSS_ONYX_TEXT_NONE, NULL);	\
		if (lwc_intern_string(o.buf, o.n, &s) != lwc_error_ok)		\
			return CSS_NOMEM;					\
		{								\
			css_error e = set_##lp(work, CSS_ONYX_TEXT_SET, s);	\
			lwc_string_unref(s);					\
			return e;						\
		}								\
	}

static css_color shadow_color(const css_computed_style *s, uint8_t type, css_color c)
{
	if (type == CSS_BOX_SHADOW_SET_CURRENT_COLOR ||
			type == CSS_BOX_SHADOW_SET_INSET_CURRENT_COLOR)
		get_color(s, &c);
	return c;
}

/* The blend, or (same) the comparison: CSS_OK / true when handled here, else a sentinel. */
#define ONYX_DISCRETE 0x7fff
static int onyx_blend(css_computed_style *work, uint32_t prop, const css_computed_style *a,
		const css_computed_style *b, float p, bool same)
{
	switch (prop) {
	ONYX_COLOR(COLOR, CSS_COLOR_COLOR)
	ONYX_COLOR(BACKGROUND_COLOR, CSS_BACKGROUND_COLOR_COLOR)
	ONYX_COLOR(BORDER_TOP_COLOR, CSS_BORDER_COLOR_COLOR)
	ONYX_COLOR(BORDER_RIGHT_COLOR, CSS_BORDER_COLOR_COLOR)
	ONYX_COLOR(BORDER_BOTTOM_COLOR, CSS_BORDER_COLOR_COLOR)
	ONYX_COLOR(BORDER_LEFT_COLOR, CSS_BORDER_COLOR_COLOR)
	ONYX_COLOR(OUTLINE_COLOR, CSS_OUTLINE_COLOR_COLOR)
	ONYX_COLOR(COLUMN_RULE_COLOR, CSS_COLUMN_RULE_COLOR_COLOR)
	ONYX_COLOR(STOP_COLOR, CSS_STOP_COLOR_COLOR)
	ONYX_LENGTH(MARGIN_TOP, CSS_MARGIN_SET)
	ONYX_LENGTH(MARGIN_RIGHT, CSS_MARGIN_SET)
	ONYX_LENGTH(MARGIN_BOTTOM, CSS_MARGIN_SET)
	ONYX_LENGTH(MARGIN_LEFT, CSS_MARGIN_SET)
	ONYX_LENGTH(PADDING_TOP, CSS_PADDING_SET)
	ONYX_LENGTH(PADDING_RIGHT, CSS_PADDING_SET)
	ONYX_LENGTH(PADDING_BOTTOM, CSS_PADDING_SET)
	ONYX_LENGTH(PADDING_LEFT, CSS_PADDING_SET)
	ONYX_LENGTH(TOP, CSS_TOP_SET)
	ONYX_LENGTH(RIGHT, CSS_TOP_SET)
	ONYX_LENGTH(BOTTOM, CSS_TOP_SET)
	ONYX_LENGTH(LEFT, CSS_TOP_SET)
	ONYX_LENGTH(HEIGHT, CSS_HEIGHT_SET)
	ONYX_LENGTH(MIN_WIDTH, CSS_MIN_WIDTH_SET)
	ONYX_LENGTH(MIN_HEIGHT, CSS_MIN_WIDTH_SET)
	ONYX_LENGTH(MAX_WIDTH, CSS_MAX_WIDTH_SET)
	ONYX_LENGTH(MAX_HEIGHT, CSS_MAX_WIDTH_SET)
	ONYX_LENGTH(BORDER_TOP_WIDTH, CSS_BORDER_WIDTH_WIDTH)
	ONYX_LENGTH(BORDER_RIGHT_WIDTH, CSS_BORDER_WIDTH_WIDTH)
	ONYX_LENGTH(BORDER_BOTTOM_WIDTH, CSS_BORDER_WIDTH_WIDTH)
	ONYX_LENGTH(BORDER_LEFT_WIDTH, CSS_BORDER_WIDTH_WIDTH)
	ONYX_LENGTH(OUTLINE_WIDTH, CSS_OUTLINE_WIDTH_WIDTH)
	ONYX_LENGTH(BORDER_TOP_LEFT_RADIUS, CSS_BORDER_RADIUS_SET)
	ONYX_LENGTH(BORDER_TOP_RIGHT_RADIUS, CSS_BORDER_RADIUS_SET)
	ONYX_LENGTH(BORDER_BOTTOM_LEFT_RADIUS, CSS_BORDER_RADIUS_SET)
	ONYX_LENGTH(BORDER_BOTTOM_RIGHT_RADIUS, CSS_BORDER_RADIUS_SET)
	ONYX_LENGTH(LETTER_SPACING, CSS_LETTER_SPACING_SET)
	ONYX_LENGTH(WORD_SPACING, CSS_WORD_SPACING_SET)
	ONYX_LENGTH(TEXT_INDENT, CSS_TEXT_INDENT_SET)
	ONYX_LENGTH(FONT_SIZE, CSS_FONT_SIZE_DIMENSION)
	ONYX_LENGTH(COLUMN_GAP, CSS_COLUMN_GAP_SET)
	ONYX_LENGTH(ROW_GAP, CSS_ROW_GAP_SET)
	ONYX_LENGTH(FLEX_BASIS, CSS_FLEX_BASIS_SET)
	ONYX_LENGTH(STROKE_WIDTH, CSS_STROKE_WIDTH_SET)
	ONYX_FIXED(OPACITY, CSS_OPACITY_SET)
	ONYX_FIXED(FILL_OPACITY, CSS_FILL_OPACITY_SET)
	ONYX_FIXED(STROKE_OPACITY, CSS_STROKE_OPACITY_SET)
	ONYX_FIXED(STOP_OPACITY, CSS_STOP_OPACITY_SET)
	ONYX_FIXED(FLEX_GROW, CSS_FLEX_GROW_SET)
	ONYX_FIXED(FLEX_SHRINK, CSS_FLEX_SHRINK_SET)
	ONYX_TEXT_BLEND(TRANSFORM, transform)
	ONYX_TEXT_BLEND(TRANSLATE, translate)
	ONYX_TEXT_BLEND(SCALE, scale)
	ONYX_TEXT_BLEND(ROTATE, rotate)
	ONYX_TEXT_BLEND(FILTER, filter)			/* (the same function lists) */
	ONYX_TEXT_BLEND(BACKDROP_FILTER, backdrop_filter)

	case CSS_PROP_WIDTH: {
		css_fixed_or_calc la, lb;
		css_unit ua = CSS_UNIT_PX, ub = CSS_UNIT_PX;
		uint8_t ta, tb;
		la.value = lb.value = 0;
		ta = get_width(a, &la, &ua);
		tb = get_width(b, &lb, &ub);
		if (same)
			return ta == tb && (ta != CSS_WIDTH_SET ||
					(ua == ub && (ua == CSS_UNIT_CALC ?
					la.calc == lb.calc : la.value == lb.value)));
		if (ta == CSS_WIDTH_SET && tb == CSS_WIDTH_SET && ua == ub &&
				ua != CSS_UNIT_CALC) {
			css_fixed_or_calc v;
			v.value = lerpx(la.value, lb.value, p);
			return set_width(work, CSS_WIDTH_SET, v, ua);
		}
		break;
	}
	case CSS_PROP_LINE_HEIGHT: {
		css_fixed la = 0, lb = 0;
		css_unit ua = CSS_UNIT_PX, ub = CSS_UNIT_PX;
		uint8_t ta = get_line_height(a, &la, &ua), tb = get_line_height(b, &lb, &ub);
		if (same)
			return ta == tb && (ta == CSS_LINE_HEIGHT_NORMAL ||
					(la == lb && (ta != CSS_LINE_HEIGHT_DIMENSION ||
					ua == ub)));
		if (ta == tb && ta == CSS_LINE_HEIGHT_NUMBER)
			return set_line_height(work, ta, lerpx(la, lb, p), CSS_UNIT_PX);
		if (ta == tb && ta == CSS_LINE_HEIGHT_DIMENSION && ua == ub)
			return set_line_height(work, ta, lerpx(la, lb, p), ua);
		break;
	}
	case CSS_PROP_Z_INDEX: {
		int32_t za = 0, zb = 0;
		uint8_t ta = get_z_index(a, &za), tb = get_z_index(b, &zb);
		if (same)
			return ta == tb && (ta != CSS_Z_INDEX_SET || za == zb);
		if (ta == CSS_Z_INDEX_SET && tb == CSS_Z_INDEX_SET)
			return set_z_index(work, ta, (int32_t) floorf(lerpf((float) za,
					(float) zb, p) + 0.5f));
		break;
	}
	case CSS_PROP_VISIBILITY: {
		uint8_t va = get_visibility(a), vb = get_visibility(b);
		if (same)
			return va == vb;
		/* visible for 0 < p < 1 when either end is */
		if ((va == CSS_VISIBILITY_VISIBLE) != (vb == CSS_VISIBILITY_VISIBLE))
			return set_visibility(work, p <= 0 ? va : p >= 1 ? vb :
					CSS_VISIBILITY_VISIBLE);
		break;
	}
	case CSS_PROP_BOX_SHADOW: {
		css_fixed la[4], lb[4];
		css_unit ua[4], ub[4];
		css_color ca = 0, cb = 0;
		uint8_t ta = get_box_shadow(a, &la[0], &ua[0], &la[1], &ua[1], &la[2], &ua[2],
				&la[3], &ua[3], &ca);
		uint8_t tb = get_box_shadow(b, &lb[0], &ub[0], &lb[1], &ub[1], &lb[2], &ub[2],
				&lb[3], &ub[3], &cb);
		bool ia = ta == CSS_BOX_SHADOW_SET_INSET ||
				ta == CSS_BOX_SHADOW_SET_INSET_CURRENT_COLOR;
		bool ib = tb == CSS_BOX_SHADOW_SET_INSET ||
				tb == CSS_BOX_SHADOW_SET_INSET_CURRENT_COLOR;
		int i;
		if (same) {
			if (ta != tb)
				return false;
			if (ta == CSS_BOX_SHADOW_NONE)
				return true;
			for (i = 0; i < 4; i++)
				if (la[i] != lb[i] || ua[i] != ub[i])
					return false;
			return ca == cb;
		}
		ca = shadow_color(a, ta, ca);
		cb = shadow_color(b, tb, cb);
		/* none: a transparent shadow of no size, as the other's */
		if (ta == CSS_BOX_SHADOW_NONE && tb != CSS_BOX_SHADOW_NONE) {
			for (i = 0; i < 4; i++) { la[i] = 0; ua[i] = ub[i]; }
			ca = cb & 0x00ffffff;
			ia = ib;
		} else if (tb == CSS_BOX_SHADOW_NONE && ta != CSS_BOX_SHADOW_NONE) {
			for (i = 0; i < 4; i++) { lb[i] = 0; ub[i] = ua[i]; }
			cb = ca & 0x00ffffff;
			ib = ia;
		} else if (ta == CSS_BOX_SHADOW_NONE) {
			break;
		}
		if (ia != ib)
			break;
		for (i = 0; i < 4; i++)
			if (ua[i] != ub[i] && la[i] != 0 && lb[i] != 0)
				break;
		if (i < 4)
			break;
		return set_box_shadow(work, ia ? CSS_BOX_SHADOW_SET_INSET : CSS_BOX_SHADOW_SET,
				lerpx(la[0], lb[0], p), la[0] != 0 ? ua[0] : ub[0],
				lerpx(la[1], lb[1], p), la[1] != 0 ? ua[1] : ub[1],
				lerpx(la[2], lb[2], p), la[2] != 0 ? ua[2] : ub[2],
				lerpx(la[3], lb[3], p), la[3] != 0 ? ua[3] : ub[3],
				blend_color(ca, cb, p));
	}
	case CSS_PROP_FILL:
	case CSS_PROP_STROKE: {
		css_color ca = 0, cb = 0;
		lwc_string *sa = NULL, *sb = NULL;
		uint8_t ta = prop == CSS_PROP_FILL ? get_fill(a, &ca, &sa) : get_stroke(a, &ca, &sa);
		uint8_t tb = prop == CSS_PROP_FILL ? get_fill(b, &cb, &sb) : get_stroke(b, &cb, &sb);
		if (same)
			return ta == tb && ca == cb && sa == sb;
		if (ta == CSS_PAINT_COLOR && tb == CSS_PAINT_COLOR)
			return prop == CSS_PROP_FILL ?
					set_fill(work, ta, blend_color(ca, cb, p), NULL) :
					set_stroke(work, ta, blend_color(ca, cb, p), NULL);
		break;
	}
	default:
		break;
	}
	return ONYX_DISCRETE;
}

/* exported function documented in include/libcss/computed.h */
css_error css_computed_style_onyx_blend(css_computed_style *work, uint32_t prop,
		const css_computed_style *a, const css_computed_style *b, float p,
		bool *interpolated)
{
	int r;

	if (interpolated != NULL)
		*interpolated = true;
	if (work == NULL || a == NULL || b == NULL || prop >= CSS_N_PROPERTIES)
		return CSS_BADPARM;
	r = onyx_blend(work, prop, a, b, p, false);
	if (r != ONYX_DISCRETE)
		return (css_error) r;
	if (interpolated != NULL)
		*interpolated = false;
	return prop_dispatch[prop].copy(p < 0.5f ? a : b, work);
}

/* exported function documented in include/libcss/computed.h */
bool css_computed_style_onyx_same(const css_computed_style *a, const css_computed_style *b,
		uint32_t prop)
{
	int r;

	if (a == b)
		return true;
	if (a == NULL || b == NULL || prop >= CSS_N_PROPERTIES)
		return false;
	r = onyx_blend(NULL, prop, a, b, 0, true);
	if (r != ONYX_DISCRETE)
		return r != 0;
	/* (another property: b's value copied over a clone of a, interned: a itself if the
	 * values are the same -- a is interned) */
	{
		css_computed_style *t = NULL;
		bool eq;
		if (a->count == 0 || css__computed_style_clone(a, &t) != CSS_OK)
			return true;
		if (prop_dispatch[prop].copy(b, t) != CSS_OK ||
				css__arena_intern_style(&t) != CSS_OK) {
			css_computed_style_destroy(t);
			return true;
		}
		eq = (t == a);
		css_computed_style_destroy(t);
		return eq;
	}
}
