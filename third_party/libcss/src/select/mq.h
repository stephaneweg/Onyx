/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 *
 * Copyright 2018 Michael Drake <tlsa@netsurf-browser.org>
 */

#ifndef css_select_mq_h_
#define css_select_mq_h_

#include <string.h>
#include <strings.h>

#include "select/helpers.h"
#include "select/strings.h"
#include "select/unit.h"

static inline bool mq_match_feature_range_length_op1(
		css_mq_feature_op op,
		const css_mq_value *value,
		const css_fixed client_len,
		const css_unit_ctx *unit_ctx)
{
	css_fixed v;

	if (op == CSS_MQ_FEATURE_OP_BOOL) {
		return client_len != 0;		/* (Onyx: "(width)") */
	}
	if (value->type != CSS_MQ_VALUE_TYPE_DIM) {
		return false;
	}

	if (value->data.dim.unit != UNIT_PX) {
		v = css_unit_len2px_mq(unit_ctx,
				value->data.dim.len,
				css__to_css_unit(value->data.dim.unit));
	} else {
		v = value->data.dim.len;
	}

	switch (op) {
	case CSS_MQ_FEATURE_OP_LT:   return v <  client_len;
	case CSS_MQ_FEATURE_OP_LTE:  return v <= client_len;
	case CSS_MQ_FEATURE_OP_EQ:   return v == client_len;
	case CSS_MQ_FEATURE_OP_GTE:  return v >= client_len;
	case CSS_MQ_FEATURE_OP_GT:   return v >  client_len;
	default:
		return false;
	}
}

static inline bool mq_match_feature_range_length_op2(
		css_mq_feature_op op,
		const css_mq_value *value,
		const css_fixed client_len,
		const css_unit_ctx *unit_ctx)
{
	css_fixed v;

	if (op == CSS_MQ_FEATURE_OP_UNUSED) {
		return true;
	}
	if (value->type != CSS_MQ_VALUE_TYPE_DIM) {
		return false;
	}

	if (value->data.dim.unit != UNIT_PX) {
		v = css_unit_len2px_mq(unit_ctx,
				value->data.dim.len,
				css__to_css_unit(value->data.dim.unit));
	} else {
		v = value->data.dim.len;
	}

	switch (op) {
	case CSS_MQ_FEATURE_OP_LT:  return client_len <  v;
	case CSS_MQ_FEATURE_OP_LTE: return client_len <= v;
	case CSS_MQ_FEATURE_OP_EQ:  return client_len == v;
	case CSS_MQ_FEATURE_OP_GTE: return client_len >= v;
	case CSS_MQ_FEATURE_OP_GT:  return client_len >  v;
	default:
		return false;
	}
}

static inline bool mq_match_feature_eq_ident_op1(
		css_mq_feature_op op,
		const css_mq_value *value,
		const lwc_string *client_value)
{
	bool is_match;

	if (value->type != CSS_MQ_VALUE_TYPE_IDENT) {
		return false;
	}

	if (value->data.ident == NULL || client_value == NULL) {
		return false;
	}

	switch (op) {
	case CSS_MQ_FEATURE_OP_EQ:
		return (lwc_string_isequal(value->data.ident,
				client_value, &is_match) == lwc_error_ok) &&
				is_match;
	default:
		return false;
	}
}

/* ---- Onyx: the other media features of Media Queries 4 / 5 -------------------------------
 * libcss answered width, height and prefers-color-scheme only; every other feature never
 * matched ("(orientation: landscape)", "(hover: hover)", "(aspect-ratio > 1)", "(color)"...).
 * NetSurf on Onyx: a screen media->width x media->height, a CSS px a device pixel, 8 bits a
 * colour, a mouse, the user's preferences left at their defaults. */

static inline bool mq_onyx_is(const lwc_string *s, const char *name)
{
	size_t len = strlen(name);
	return lwc_string_length(s) == len &&
			strncasecmp(lwc_string_data(s), name, len) == 0;
}

/* a <number> / <ratio> / resolution value against the client's: "v op client" for the first
 * operator (first true), "client op v" for the second */
static inline bool mq_onyx_cmp(css_mq_feature_op op, css_fixed v, css_fixed client,
		bool first)
{
	css_fixed a = first ? v : client, b = first ? client : v;

	switch (op) {
	case CSS_MQ_FEATURE_OP_BOOL:	/* (== OP_UNUSED: the second operator's none) */
		return first ? client != 0 : true;
	case CSS_MQ_FEATURE_OP_LT:     return a <  b;
	case CSS_MQ_FEATURE_OP_LTE:    return a <= b;
	case CSS_MQ_FEATURE_OP_EQ:     return a == b;
	case CSS_MQ_FEATURE_OP_GTE:    return a >= b;
	case CSS_MQ_FEATURE_OP_GT:     return a >  b;
	default:                       return false;
	}
}

/* a value as a number (resolution: in dppx), or false */
static inline bool mq_onyx_value(const css_mq_value *value, bool resolution, css_fixed *v)
{
	if (value->type == CSS_MQ_VALUE_TYPE_NUM ||
			value->type == CSS_MQ_VALUE_TYPE_RATIO) {
		if (resolution)
			return false;
		*v = value->data.num_or_ratio;
		return true;
	}
	if (value->type == CSS_MQ_VALUE_TYPE_DIM && resolution) {
		switch (value->data.dim.unit) {
		case UNIT_DPPX: *v = value->data.dim.len; return true;
		case UNIT_DPI:  *v = FDIV(value->data.dim.len, INTTOFIX(96)); return true;
		case UNIT_DPCM: *v = FDIV(FMUL(value->data.dim.len, FLTTOFIX(2.54)),
					INTTOFIX(96)); return true;
		default: return false;
		}
	}
	return false;
}

static inline bool mq_onyx_range(const css_mq_feature *feat, css_fixed client, bool resolution)
{
	css_fixed v = 0, v2 = 0;

	if (feat->op == CSS_MQ_FEATURE_OP_BOOL)
		return client != 0;
	if (!mq_onyx_value(&feat->value, resolution, &v))
		return false;
	if (!mq_onyx_cmp(feat->op, v, client, true))
		return false;
	if (feat->op2 == CSS_MQ_FEATURE_OP_UNUSED)
		return true;
	if (!mq_onyx_value(&feat->value2, resolution, &v2))
		return false;
	return mq_onyx_cmp(feat->op2, v2, client, false);
}

/* a discrete feature: its value is the client's (a bare "(name)": the client's is not the
 * "none" one) */
static inline bool mq_onyx_discrete(const css_mq_feature *feat, const char *client,
		bool bool_value)
{
	if (feat->op == CSS_MQ_FEATURE_OP_BOOL)
		return bool_value;
	return feat->op == CSS_MQ_FEATURE_OP_EQ &&
			feat->value.type == CSS_MQ_VALUE_TYPE_IDENT &&
			feat->value.data.ident != NULL &&
			mq_onyx_is(feat->value.data.ident, client);
}

static inline bool mq_onyx_match_feature(const css_mq_feature *feat,
		const css_unit_ctx *unit_ctx, const css_media *media, bool *known)
{
	static const struct { const char *name, *client; bool b; } discrete[] = {
		{ "hover", "hover", true }, { "any-hover", "hover", true },
		{ "pointer", "fine", true }, { "any-pointer", "fine", true },
		{ "prefers-reduced-motion", "no-preference", false },
		{ "prefers-reduced-transparency", "no-preference", false },
		{ "prefers-reduced-data", "no-preference", false },
		{ "prefers-contrast", "no-preference", false },
		{ "forced-colors", "none", false }, { "inverted-colors", "none", false },
		{ "display-mode", "browser", true }, { "scripting", "enabled", true },
		{ "update", "fast", true }, { "color-gamut", "srgb", true },
		{ "dynamic-range", "standard", true },
		{ "video-dynamic-range", "standard", true },
		{ "overflow-block", "scroll", true }, { "overflow-inline", "scroll", true },
	};
	const lwc_string *n = feat->name;
	size_t i;

	*known = true;
	if (mq_onyx_is(n, "device-width"))
		return mq_match_feature_range_length_op1(feat->op, &feat->value,
				media->width, unit_ctx) &&
			mq_match_feature_range_length_op2(feat->op2, &feat->value2,
				media->width, unit_ctx);
	if (mq_onyx_is(n, "device-height"))
		return mq_match_feature_range_length_op1(feat->op, &feat->value,
				media->height, unit_ctx) &&
			mq_match_feature_range_length_op2(feat->op2, &feat->value2,
				media->height, unit_ctx);
	if (mq_onyx_is(n, "aspect-ratio") || mq_onyx_is(n, "device-aspect-ratio")) {
		if (media->height <= 0)
			return false;
		return mq_onyx_range(feat, FDIV(media->width, media->height), false);
	}
	if (mq_onyx_is(n, "orientation"))
		return mq_onyx_discrete(feat, media->height >= media->width ?
				"portrait" : "landscape", true);
	if (mq_onyx_is(n, "resolution"))
		return mq_onyx_range(feat, INTTOFIX(1), true);
	if (mq_onyx_is(n, "-webkit-device-pixel-ratio"))
		return mq_onyx_range(feat, INTTOFIX(1), false);
	if (mq_onyx_is(n, "-webkit-min-device-pixel-ratio") ||
			mq_onyx_is(n, "-webkit-max-device-pixel-ratio")) {
		css_fixed v;
		if (feat->op != CSS_MQ_FEATURE_OP_EQ ||
				!mq_onyx_value(&feat->value, false, &v))
			return false;
		return lwc_string_data(n)[8] == 'm' && lwc_string_data(n)[9] == 'i' ?
				INTTOFIX(1) >= v : INTTOFIX(1) <= v;
	}
	if (mq_onyx_is(n, "color"))
		return mq_onyx_range(feat, INTTOFIX(8), false);
	if (mq_onyx_is(n, "color-index") || mq_onyx_is(n, "monochrome") ||
			mq_onyx_is(n, "grid"))
		return mq_onyx_range(feat, 0, false);
	for (i = 0; i < sizeof discrete / sizeof discrete[0]; i++)
		if (mq_onyx_is(n, discrete[i].name))
			return mq_onyx_discrete(feat, discrete[i].client, discrete[i].b);
	*known = false;
	return false;
}

/**
 * Match media query features.
 *
 * \param[in] feat      Condition to match.
 * \param[in] unit_ctx  Current unit conversion context.
 * \param[in] media     Current media spec, to check against feat.
 * \return true if condition matches, otherwise false.
 */
static inline int mq_match_feature(
		const css_mq_feature *feat,
		const css_unit_ctx *unit_ctx,
		const css_media *media,
		const css_select_strings *str)
{
	bool match;

	/* TODO: Use interned string for comparison. */
	if (lwc_string_isequal(feat->name,
			str->width, &match) == lwc_error_ok &&
			match == true) {
		if (!mq_match_feature_range_length_op1(feat->op, &feat->value,
					media->width, unit_ctx)) {
			return false;
		}
		return mq_match_feature_range_length_op2(feat->op2,
				&feat->value2, media->width, unit_ctx);

	} else if (lwc_string_isequal(feat->name,
			str->height, &match) == lwc_error_ok &&
			match == true) {
		if (!mq_match_feature_range_length_op1(feat->op, &feat->value,
				media->height, unit_ctx)) {
			return false;
		}

		return mq_match_feature_range_length_op2(feat->op2,
				&feat->value2, media->height, unit_ctx);

	} else if (lwc_string_isequal(feat->name,
			str->prefers_color_scheme, &match) == lwc_error_ok &&
			match == true) {
		if (mq_match_feature_eq_ident_op1(feat->op, &feat->value,
				media->prefers_color_scheme) ||
		    feat->op == CSS_MQ_FEATURE_OP_BOOL) {
			return true;
		}

		return false;
	}

	/* Onyx: the other feature names; -1 for a feature libcss does not know (unknown: Media
	 * Queries 4's three-valued logic -- "not (bogus)" is not true) */
	{
		bool known, r = mq_onyx_match_feature(feat, unit_ctx, media, &known);
		return known ? r : -1;
	}
}

/**
 * Match media query conditions.
 *
 * \param[in] cond      Condition to match.
 * \param[in] unit_ctx  Current unit conversion context.
 * \param[in] media     Current media spec, to check against cond.
 * \return true if condition matches, otherwise false.
 */
static inline int mq_match_condition(
		const css_mq_cond *cond,
		const css_unit_ctx *unit_ctx,
		const css_media *media,
		const css_select_strings *str)
{
	bool matched = !cond->op;
	bool unknown = false;	/* (Onyx: a part unknown, -1 -- Kleene's logic) */

	for (uint32_t i = 0; i < cond->nparts; i++) {
		int part_matched;
		if (cond->parts[i]->type == CSS_MQ_FEATURE) {
			part_matched = mq_match_feature(
					cond->parts[i]->data.feat,
					unit_ctx, media, str);
		} else {
			assert(cond->parts[i]->type == CSS_MQ_COND);
			part_matched = mq_match_condition(
					cond->parts[i]->data.cond,
					unit_ctx, media, str);
		}

		if (part_matched < 0) {
			unknown = true;
			continue;
		}
		if (cond->op) {
			/* OR */
			matched |= part_matched;
			if (matched) {
				unknown = false;
				break; /* Short-circuit */
			}
		} else {
			/* AND */
			matched &= part_matched;
			if (!matched) {
				unknown = false;
				break; /* Short-circuit */
			}
		}
	}

	if (unknown)
		return -1;
	return matched != cond->negate;
}

/**
 * Test whether media query list matches current media.
 *
 * If anything in the list matches, the list matches.  If none match
 * it doesn't match.
 *
 * \param[in] m         Media query list.
 * \param[in] unit_ctx  Current unit conversion context.
 * \param[in] media     Current media spec, to check against m.
 * \return true if media query list matches media
 */
static inline bool mq__list_match(
		const css_mq_query *m,
		const css_unit_ctx *unit_ctx,
		const css_media *media,
		const css_select_strings *str)
{
	for (; m != NULL; m = m->next) {
		/* Onyx: "not" negates the whole query, its type and its
		 * condition ("not all and (min-color: 1)" matches a screen
		 * without colours); an unknown condition matches neither way */
		int r = 1;

		if (m->type & media->type) {
			if (m->cond != NULL)
				r = mq_match_condition(m->cond, unit_ctx, media,
						str);
		} else {
			r = 0;
		}
		if (r < 0)
			continue;
		if ((r == 1) != !!m->negate_type) {
			/* We have a match, no need to look further. */
			return true;
		}
	}

	return false;
}

/**
 * Test whether the rule applies for current media.
 *
 * \param rule      Rule to test
 * \param unit_ctx  Current unit conversion context.
 * \param media     Current media spec
 * \return true iff chain's rule applies for media
 */
static inline bool mq_rule_good_for_media(
		const css_rule *rule,
		const css_unit_ctx *unit_ctx,
		const css_media *media,
		const css_select_strings *str)
{
	bool applies = true;
	const css_rule *ancestor = rule;

	while (ancestor != NULL) {
		const css_rule_media *m = (const css_rule_media *) ancestor;

		if (ancestor->type == CSS_RULE_MEDIA) {
			applies = mq__list_match(m->media,
					unit_ctx, media, str);
			if (applies == false) {
				break;
			}
		}

		if (ancestor->ptype != CSS_RULE_PARENT_STYLESHEET) {
			ancestor = ancestor->parent;
		} else {
			ancestor = NULL;
		}
	}

	return applies;
}

#endif
