/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: the CSS Color 4 / 5 functions libcss lacked -- oklab(), oklch(), lab(), lch(),
 * color(), color-mix() -- converted to sRGB (0xAARRGGBB) when parsed. Out of gamut colours
 * are clipped. css__parse_colour_specifier (utils.c) calls this for the functions it does
 * not know itself.
 */

#include <math.h>
#include <string.h>
#include <strings.h>

#include "bytecode/bytecode.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
	float r, g, b, a;		/* sRGB, gamma encoded, 0..1 */
} onyx_rgba;

static bool onyx_fn_is(css_language *c, lwc_string *name, int which)
{
	bool match;
	return lwc_string_caseless_isequal(name, c->strings[which], &match) ==
			lwc_error_ok && match;
}

static float onyx_clamp01(float v)
{
	return v < 0 ? 0 : (v > 1 ? 1 : v);
}

static float onyx_gamma(float x)		/* linear -> sRGB */
{
	if (x <= 0.0031308f)
		return 12.92f * x;
	return 1.055f * powf(x, 1.0f / 2.4f) - 0.055f;
}

static float onyx_linear(float x)		/* sRGB -> linear */
{
	if (x <= 0.04045f)
		return x / 12.92f;
	return powf((x + 0.055f) / 1.055f, 2.4f);
}

static onyx_rgba onyx_from_linear(float r, float g, float b, float a)
{
	onyx_rgba o;
	o.r = onyx_clamp01(onyx_gamma(r));
	o.g = onyx_clamp01(onyx_gamma(g));
	o.b = onyx_clamp01(onyx_gamma(b));
	o.a = onyx_clamp01(a);
	return o;
}

static onyx_rgba onyx_oklab(float L, float A, float B, float alpha)
{
	float l_ = L + 0.3963377774f * A + 0.2158037573f * B;
	float m_ = L - 0.1055613458f * A - 0.0638541728f * B;
	float s_ = L - 0.0894841775f * A - 1.2914855480f * B;
	float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
	return onyx_from_linear(
			+4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
			-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
			-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s,
			alpha);
}

/* sRGB -> OKLab (for color-mix in oklab / oklch) */
static void onyx_to_oklab(onyx_rgba c, float *L, float *A, float *B)
{
	float r = onyx_linear(c.r), g = onyx_linear(c.g), b = onyx_linear(c.b);
	float l = cbrtf(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
	float m = cbrtf(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
	float s = cbrtf(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
	*L = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
	*A = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
	*B = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
}

static onyx_rgba onyx_lab(float L, float A, float B, float alpha)
{
	const float kappa = 24389.0f / 27.0f, eps = 216.0f / 24389.0f;
	float fy = (L + 16) / 116, fx = fy + A / 500, fz = fy - B / 200;
	float x = (fx * fx * fx > eps) ? fx * fx * fx : (116 * fx - 16) / kappa;
	float y = (L > kappa * eps) ? fy * fy * fy : L / kappa;
	float z = (fz * fz * fz > eps) ? fz * fz * fz : (116 * fz - 16) / kappa;
	float X, Y, Z, X65, Y65, Z65;

	/* D50 white */
	X = x * 0.96422f;
	Y = y;
	Z = z * 0.82521f;
	/* Bradford D50 -> D65 */
	X65 = 0.9554734527f * X - 0.0230985369f * Y + 0.0632593087f * Z;
	Y65 = -0.0283697070f * X + 1.0099954580f * Y + 0.0210413990f * Z;
	Z65 = 0.0123140017f * X - 0.0205076964f * Y + 1.3303659366f * Z;
	return onyx_from_linear(
			3.2409699419f * X65 - 1.5373831776f * Y65 - 0.4986107603f * Z65,
			-0.9692436363f * X65 + 1.8759675015f * Y65 + 0.0415550574f * Z65,
			0.0556300797f * X65 - 0.2039769589f * Y65 + 1.0569715142f * Z65,
			alpha);
}

static uint32_t onyx_pack(onyx_rgba c)
{
	uint32_t a = (uint32_t) (c.a * 255 + 0.5f), r = (uint32_t) (c.r * 255 + 0.5f);
	uint32_t g = (uint32_t) (c.g * 255 + 0.5f), b = (uint32_t) (c.b * 255 + 0.5f);
	return (a << 24) | (r << 16) | (g << 8) | b;
}

static onyx_rgba onyx_unpack(uint32_t v)
{
	onyx_rgba c;
	c.a = ((v >> 24) & 0xff) / 255.0f;
	c.r = ((v >> 16) & 0xff) / 255.0f;
	c.g = ((v >> 8) & 0xff) / 255.0f;
	c.b = (v & 0xff) / 255.0f;
	return c;
}

/* One component: a number, a percentage (scaled: pct100 is the value of 100%), an angle
 * (in degrees, for hues), or none (0). */
static bool onyx_component(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, float pct100, bool hue, float *out)
{
	const css_token *t;
	size_t consumed = 0;
	css_fixed num;
	bool match;

	consumeWhitespace(vector, ctx);
	t = parserutils_vector_iterate(vector, ctx);
	if (t == NULL)
		return false;
	if (t->type == CSS_TOKEN_IDENT && lwc_string_caseless_isequal(t->idata,
			c->strings[NONE], &match) == lwc_error_ok && match) {
		*out = 0;
		return true;
	}
	if (t->type != CSS_TOKEN_NUMBER && t->type != CSS_TOKEN_PERCENTAGE &&
			!(hue && t->type == CSS_TOKEN_DIMENSION))
		return false;
	num = css__number_from_lwc_string(t->idata, false, &consumed);
	*out = FIXTOFLT(num);
	if (t->type == CSS_TOKEN_PERCENTAGE) {
		*out = *out * pct100 / 100.0f;
	} else if (t->type == CSS_TOKEN_DIMENSION) {
		const char *unit = lwc_string_data(t->idata) + consumed;
		size_t ulen = lwc_string_length(t->idata) - consumed;
		if (ulen == 3 && strncasecmp(unit, "deg", 3) == 0)
			;
		else if (ulen == 3 && strncasecmp(unit, "rad", 3) == 0)
			*out = *out * 180.0f / (float) M_PI;
		else if (ulen == 4 && strncasecmp(unit, "grad", 4) == 0)
			*out = *out * 0.9f;
		else if (ulen == 4 && strncasecmp(unit, "turn", 4) == 0)
			*out = *out * 360.0f;
		else
			return false;
	}
	return true;
}

/* Three components, then [ / alpha ], then ')'. */
static bool onyx_components(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, const float pct100[3], const bool hue[3], float v[3], float *alpha)
{
	const css_token *t;
	int i;

	for (i = 0; i < 3; i++) {
		if (!onyx_component(c, vector, ctx, pct100[i], hue[i], &v[i]))
			return false;
	}
	*alpha = 1;
	consumeWhitespace(vector, ctx);
	t = parserutils_vector_peek(vector, *ctx);
	if (t != NULL && tokenIsChar(t, '/')) {
		parserutils_vector_iterate(vector, ctx);
		if (!onyx_component(c, vector, ctx, 1.0f, false, alpha))
			return false;
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_peek(vector, *ctx);
	}
	if (t == NULL || !tokenIsChar(t, ')'))
		return false;
	parserutils_vector_iterate(vector, ctx);
	return true;
}

static bool onyx_color_mix(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, uint32_t *result);

/* Documented in parse/properties/utils.h */
bool css__onyx_parse_colour_fn(css_language *c, lwc_string *fn,
		const parserutils_vector *vector, int32_t *ctx, uint32_t *result)
{
	float v[3], alpha;

	if (onyx_fn_is(c, fn, FN_OKLAB)) {
		static const float pct[3] = { 1.0f, 0.4f, 0.4f };
		static const bool hue[3] = { false, false, false };
		if (!onyx_components(c, vector, ctx, pct, hue, v, &alpha))
			return false;
		*result = onyx_pack(onyx_oklab(v[0], v[1], v[2], alpha));
		return true;
	}
	if (onyx_fn_is(c, fn, FN_OKLCH)) {
		static const float pct[3] = { 1.0f, 0.4f, 1.0f };
		static const bool hue[3] = { false, false, true };
		float h;
		if (!onyx_components(c, vector, ctx, pct, hue, v, &alpha))
			return false;
		h = v[2] * (float) M_PI / 180.0f;
		*result = onyx_pack(onyx_oklab(v[0], v[1] * cosf(h), v[1] * sinf(h), alpha));
		return true;
	}
	if (onyx_fn_is(c, fn, FN_LAB)) {
		static const float pct[3] = { 100.0f, 125.0f, 125.0f };
		static const bool hue[3] = { false, false, false };
		if (!onyx_components(c, vector, ctx, pct, hue, v, &alpha))
			return false;
		*result = onyx_pack(onyx_lab(v[0], v[1], v[2], alpha));
		return true;
	}
	if (onyx_fn_is(c, fn, FN_LCH)) {
		static const float pct[3] = { 100.0f, 150.0f, 1.0f };
		static const bool hue[3] = { false, false, true };
		float h;
		if (!onyx_components(c, vector, ctx, pct, hue, v, &alpha))
			return false;
		h = v[2] * (float) M_PI / 180.0f;
		*result = onyx_pack(onyx_lab(v[0], v[1] * cosf(h), v[1] * sinf(h), alpha));
		return true;
	}
	if (onyx_fn_is(c, fn, COLOR)) {
		/* color(<space> r g b [/ a]): srgb, srgb-linear; others as srgb */
		static const float pct[3] = { 1.0f, 1.0f, 1.0f };
		static const bool hue[3] = { false, false, false };
		const css_token *t;
		bool linear = false;
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_iterate(vector, ctx);
		if (t == NULL || t->type != CSS_TOKEN_IDENT)
			return false;
		linear = lwc_string_length(t->idata) == 11 &&
				strncasecmp(lwc_string_data(t->idata), "srgb-linear", 11) == 0;
		if (!onyx_components(c, vector, ctx, pct, hue, v, &alpha))
			return false;
		if (linear) {
			*result = onyx_pack(onyx_from_linear(v[0], v[1], v[2], alpha));
		} else {
			onyx_rgba o = { onyx_clamp01(v[0]), onyx_clamp01(v[1]),
					onyx_clamp01(v[2]), onyx_clamp01(alpha) };
			*result = onyx_pack(o);
		}
		return true;
	}
	if (onyx_fn_is(c, fn, FN_COLOR_MIX))
		return onyx_color_mix(c, vector, ctx, result);
	return false;
}

/* color-mix(in <space> [<hue method>]?, <colour> [<pct>]?, <colour> [<pct>]?): mixed in
 * sRGB, or in OKLab for the oklab / oklch / lab / lch spaces (premultiplied alpha). */
static bool onyx_color_mix(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, uint32_t *result)
{
	const css_token *t;
	onyx_rgba col[2], o;
	float p[2] = { -1, -1 }, scale = 1, w0, w1, a;
	bool oklab = false;
	int i;

	consumeWhitespace(vector, ctx);
	t = parserutils_vector_iterate(vector, ctx);
	if (t == NULL || t->type != CSS_TOKEN_IDENT || lwc_string_length(t->idata) != 2 ||
			strncasecmp(lwc_string_data(t->idata), "in", 2) != 0)
		return false;
	consumeWhitespace(vector, ctx);
	t = parserutils_vector_iterate(vector, ctx);
	if (t == NULL || t->type != CSS_TOKEN_IDENT)
		return false;
	oklab = strncasecmp(lwc_string_data(t->idata), "ok", 2) == 0 ||
			(lwc_string_length(t->idata) == 3 &&
			 (strncasecmp(lwc_string_data(t->idata), "lab", 3) == 0 ||
			  strncasecmp(lwc_string_data(t->idata), "lch", 3) == 0));
	/* (a hue interpolation method: skipped) */
	while ((t = parserutils_vector_peek(vector, *ctx)) != NULL && !tokenIsChar(t, ','))
		parserutils_vector_iterate(vector, ctx);
	if (t == NULL)
		return false;
	parserutils_vector_iterate(vector, ctx);

	for (i = 0; i < 2; i++) {
		uint16_t value = 0;
		uint32_t v = 0;
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_peek(vector, *ctx);
		if (t != NULL && t->type == CSS_TOKEN_PERCENTAGE) {
			if (!onyx_component(c, vector, ctx, 100.0f, false, &p[i]))
				return false;
			consumeWhitespace(vector, ctx);
		}
		if (css__parse_colour_specifier(c, vector, ctx, &value, &v) != CSS_OK)
			return false;
		if (value == 0x0001)
			v = 0xff000000;		/* currentColor: (black) */
		col[i] = onyx_unpack(v);
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_peek(vector, *ctx);
		if (t != NULL && t->type == CSS_TOKEN_PERCENTAGE && p[i] < 0) {
			if (!onyx_component(c, vector, ctx, 100.0f, false, &p[i]))
				return false;
			consumeWhitespace(vector, ctx);
			t = parserutils_vector_peek(vector, *ctx);
		}
		if (t == NULL)
			return false;
		parserutils_vector_iterate(vector, ctx);
		if (i == 0 && !tokenIsChar(t, ','))
			return false;
		if (i == 1 && !tokenIsChar(t, ')'))
			return false;
	}

	/* the percentages */
	if (p[0] < 0 && p[1] < 0) {
		p[0] = p[1] = 50;
	} else if (p[0] < 0) {
		p[0] = 100 - p[1];
	} else if (p[1] < 0) {
		p[1] = 100 - p[0];
	}
	if (p[0] + p[1] <= 0)
		return false;
	if (p[0] + p[1] < 100)
		scale = (p[0] + p[1]) / 100;
	w1 = p[1] / (p[0] + p[1]);
	w0 = 1 - w1;

	a = col[0].a * w0 + col[1].a * w1;
	if (oklab) {
		float L0, A0, B0, L1, A1, B1;
		onyx_to_oklab(col[0], &L0, &A0, &B0);
		onyx_to_oklab(col[1], &L1, &A1, &B1);
		if (a > 0) {
			float L = (L0 * col[0].a * w0 + L1 * col[1].a * w1) / a;
			float A = (A0 * col[0].a * w0 + A1 * col[1].a * w1) / a;
			float B = (B0 * col[0].a * w0 + B1 * col[1].a * w1) / a;
			o = onyx_oklab(L, A, B, a * scale);
		} else {
			o = onyx_oklab(L0 * w0 + L1 * w1, 0, 0, 0);
		}
	} else {
		if (a > 0) {
			o.r = (col[0].r * col[0].a * w0 + col[1].r * col[1].a * w1) / a;
			o.g = (col[0].g * col[0].a * w0 + col[1].g * col[1].a * w1) / a;
			o.b = (col[0].b * col[0].a * w0 + col[1].b * col[1].a * w1) / a;
		} else {
			o.r = o.g = o.b = 0;
		}
		o.a = onyx_clamp01(a * scale);
	}
	*result = onyx_pack(o);
	return true;
}
