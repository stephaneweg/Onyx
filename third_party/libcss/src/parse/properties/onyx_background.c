/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: CSS3 backgrounds -- gradients as background images, several layers, the CSS3
 * background shorthand (position / size, the box keywords), background-repeat's two-value
 * syntax.
 *
 * A gradient is kept as the background image's "URL", a string the client recognises by
 * its prefix and draws itself (NetSurf: content/handlers/html/onyx_gradient.c):
 *
 *   onyx-gradient:<kind>;<param>=<value>;...;S=<stop>,<stop>,...
 *
 *   kind   L linear, R radial, C conic; "rL" etc. when repeating
 *   L      a=<deg> (the angle), or c=<tl|tr|bl|br> (to a corner: depends on the box)
 *   R      sh=<c|e> (circle, ellipse), sz=<cs|cc|fs|fc> (closest/farthest side/corner) or
 *          rx=<len>;ry=<len> (explicit radii), x=<len>;y=<len> (the centre)
 *   C      f=<deg> (from), x=<len>;y=<len>
 *   stop   <AARRGGBB, or "cc" for currentColor>@<len, or "-" for automatic>
 *   len    a number and its unit: % px em rem vw vh pt deg
 *
 * Several layers: the first (topmost) image is kept, with its position, size and repeat;
 * the colour is the last layer's.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/* ---- a text buffer ------------------------------------------------------------------------ */

typedef struct {
	char *p;
	size_t n, cap;
	bool oom;
} onyx_buf;

static void ob_puts(onyx_buf *b, const char *s)
{
	size_t n = strlen(s);
	if (b->oom)
		return;
	if (b->n + n + 1 > b->cap) {
		size_t cap = b->cap ? b->cap : 128;
		char *p;
		while (b->n + n + 1 > cap)
			cap *= 2;
		p = realloc(b->p, cap);
		if (p == NULL) {
			b->oom = true;
			return;
		}
		b->p = p;
		b->cap = cap;
	}
	memcpy(b->p + b->n, s, n + 1);
	b->n += n;
}

static void ob_printf(onyx_buf *b, const char *fmt, float v)
{
	char tmp[48];
	snprintf(tmp, sizeof(tmp), fmt, v);
	ob_puts(b, tmp);
}

/* A number, trimmed ("50", "12.5") */
static void ob_num(onyx_buf *b, float v)
{
	char tmp[48], *e;
	snprintf(tmp, sizeof(tmp), "%.3f", v);	/* (the fixed point's precision) */
	e = tmp + strlen(tmp) - 1;
	while (e > tmp && *e == '0')
		*e-- = '\0';
	if (*e == '.')
		*e = '\0';
	ob_puts(b, tmp);
}

/* ---- tokens ------------------------------------------------------------------------------ */

static bool onyx_word(const css_token *t, const char *w)
{
	size_t n = strlen(w);
	return t != NULL && t->type == CSS_TOKEN_IDENT && lwc_string_length(t->idata) == n &&
			strncasecmp(lwc_string_data(t->idata), w, n) == 0;
}

/* A function's name, without a vendor prefix; "" if not a function. */
static const char *onyx_fn_name(const css_token *t, bool *prefixed)
{
	const char *s;
	*prefixed = false;
	if (t == NULL || t->type != CSS_TOKEN_FUNCTION)
		return "";
	s = lwc_string_data(t->idata);
	if (s[0] == '-') {
		const char *d = strchr(s + 1, '-');
		if (d != NULL) {
			*prefixed = true;
			return d + 1;
		}
	}
	return s;
}

static bool onyx_is(const char *name, const char *w)
{
	return strcasecmp(name, w) == 0;
}

/* A length / percentage / angle token: its value in the unit written (angles in degrees). */
static bool onyx_len(css_language *c, const parserutils_vector *vector, int32_t *ctx,
		onyx_buf *out, bool angle_ok)
{
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	int32_t save = *ctx;
	css_fixed len;
	uint32_t unit;
	const char *u;
	float v;

	if (t == NULL || !(t->type == CSS_TOKEN_DIMENSION || t->type == CSS_TOKEN_PERCENTAGE ||
			t->type == CSS_TOKEN_NUMBER))
		return false;
	if (css__parse_unit_specifier(c, vector, ctx, UNIT_PX, &len, &unit) != CSS_OK) {
		*ctx = save;
		return false;
	}
	if (t->type == CSS_TOKEN_NUMBER && len != 0) {
		*ctx = save;		/* (a unitless number: only 0) */
		return false;
	}
	v = FIXTOFLT(len);
	switch (unit) {
	case UNIT_PX: u = "px"; break;
	case UNIT_PCT: u = "%"; break;
	case UNIT_EM: u = "em"; break;
	case UNIT_REM: u = "rem"; break;
	case UNIT_VW: u = "vw"; break;
	case UNIT_VH: u = "vh"; break;
	case UNIT_PT: u = "pt"; break;
	case UNIT_EX: u = "em"; v *= 0.5f; break;
	case UNIT_CH: u = "em"; v *= 0.5f; break;
	case UNIT_IN: u = "px"; v *= 96; break;
	case UNIT_CM: u = "px"; v *= 96 / 2.54f; break;
	case UNIT_MM: u = "px"; v *= 96 / 25.4f; break;
	case UNIT_DEG: u = "deg"; break;
	case UNIT_RAD: u = "deg"; v *= 57.29578f; break;
	case UNIT_GRAD: u = "deg"; v *= 0.9f; break;
	case UNIT_TURN: u = "deg"; v *= 360; break;
	default:
		*ctx = save;
		return false;
	}
	if (!angle_ok && strcmp(u, "deg") == 0) {
		*ctx = save;
		return false;
	}
	ob_num(out, v);
	ob_puts(out, u);
	return true;
}

/* An angle: its degrees. */
static bool onyx_angle(css_language *c, const parserutils_vector *vector, int32_t *ctx,
		float *deg)
{
	onyx_buf b = { NULL, 0, 0, false };
	bool ok = onyx_len(c, vector, ctx, &b, true);
	if (ok && b.p != NULL && strstr(b.p, "deg") != NULL) {
		*deg = strtof(b.p, NULL);
	} else if (ok && b.p != NULL && strcmp(b.p, "0px") == 0) {
		*deg = 0;			/* 0 */
	} else {
		ok = false;
	}
	free(b.p);
	return ok;
}

/* A position (1 or 2 values: keywords, lengths): "x=..;y=.." */
static bool onyx_position(css_language *c, const parserutils_vector *vector, int32_t *ctx,
		onyx_buf *out)
{
	onyx_buf v[2] = { { NULL, 0, 0, false }, { NULL, 0, 0, false } };
	int axis[2] = { -1, -1 };	/* 0: x, 1: y, -1: either */
	int n = 0, i;
	bool ok = true;

	while (n < 2) {
		const css_token *t;
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_peek(vector, *ctx);
		if (onyx_word(t, "left")) {
			ob_puts(&v[n], "0%"); axis[n] = 0;
		} else if (onyx_word(t, "right")) {
			ob_puts(&v[n], "100%"); axis[n] = 0;
		} else if (onyx_word(t, "top")) {
			ob_puts(&v[n], "0%"); axis[n] = 1;
		} else if (onyx_word(t, "bottom")) {
			ob_puts(&v[n], "100%"); axis[n] = 1;
		} else if (onyx_word(t, "center")) {
			ob_puts(&v[n], "50%");
		} else if (onyx_len(c, vector, ctx, &v[n], false)) {
			n++;
			continue;
		} else {
			break;
		}
		parserutils_vector_iterate(vector, ctx);
		n++;
	}
	if (n == 0) {
		ok = false;
	} else {
		const char *x = "50%", *y = "50%";
		if (n == 1) {
			if (axis[0] == 1)
				y = v[0].p;
			else
				x = v[0].p;
		} else if (axis[0] == 1 || axis[1] == 0) {
			y = v[0].p;		/* "top left" */
			x = v[1].p;
		} else {
			x = v[0].p;
			y = v[1].p;
		}
		ob_puts(out, ";x=");
		ob_puts(out, x);
		ob_puts(out, ";y=");
		ob_puts(out, y);
	}
	for (i = 0; i < 2; i++)
		free(v[i].p);
	return ok;
}

/* The colour stops, up to the gradient's ')' (consumed): ";S=stop,stop..." */
static bool onyx_stops(css_language *c, const parserutils_vector *vector, int32_t *ctx,
		onyx_buf *out, bool angles)
{
	int nstops = 0;

	ob_puts(out, ";S=");
	for (;;) {
		const css_token *t;
		uint16_t value = 0;
		uint32_t color = 0;
		char col[16];
		int npos = 0;

		consumeWhitespace(vector, ctx);
		t = parserutils_vector_peek(vector, *ctx);
		if (t == NULL)
			return false;
		if (css__parse_colour_specifier(c, vector, ctx, &value, &color) == CSS_OK) {
			if (value == 0x0001)
				snprintf(col, sizeof(col), "cc");
			else if (value == 0x0000)
				snprintf(col, sizeof(col), "00000000");
			else
				snprintf(col, sizeof(col), "%08x", color);
			/* up to two positions: the colour at each */
			for (;;) {
				onyx_buf pos = { NULL, 0, 0, false };
				consumeWhitespace(vector, ctx);
				if (npos == 2 || !onyx_len(c, vector, ctx, &pos, angles)) {
					free(pos.p);
					break;
				}
				if (nstops++ > 0)
					ob_puts(out, ",");
				ob_puts(out, col);
				ob_puts(out, "@");
				ob_puts(out, pos.p);
				free(pos.p);
				npos++;
			}
			if (npos == 0) {
				if (nstops++ > 0)
					ob_puts(out, ",");
				ob_puts(out, col);
				ob_puts(out, "@-");
			}
		} else {
			/* a colour hint (a lone position): skipped */
			onyx_buf pos = { NULL, 0, 0, false };
			bool hint = onyx_len(c, vector, ctx, &pos, angles);
			free(pos.p);
			if (!hint)
				return false;
		}
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_iterate(vector, ctx);
		if (t == NULL)
			return false;
		if (tokenIsChar(t, ')'))
			break;
		if (!tokenIsChar(t, ','))
			return false;
	}
	return nstops >= 1 && !out->oom;
}

/* A gradient function (ctx at its token): its description into out. */
static bool onyx_gradient(css_language *c, const parserutils_vector *vector, int32_t *ctx,
		onyx_buf *out)
{
	const css_token *t = parserutils_vector_iterate(vector, ctx);
	bool prefixed, repeating = false;
	const char *name = onyx_fn_name(t, &prefixed);

	if (strncasecmp(name, "repeating-", 10) == 0) {
		repeating = true;
		name += 10;
	}
	ob_puts(out, "onyx-gradient:");
	if (repeating)
		ob_puts(out, "r");

	if (onyx_is(name, "linear-gradient")) {
		float deg = 180;
		bool corner = false;
		char cc[3] = "";
		int32_t save;

		consumeWhitespace(vector, ctx);
		save = *ctx;
		t = parserutils_vector_peek(vector, *ctx);
		if (onyx_angle(c, vector, ctx, &deg)) {
			if (prefixed)
				deg = 90 - deg;		/* (legacy: 0deg is east, anticlockwise) */
		} else {
			bool to = !prefixed;
			int h = 0, v = 0, k;
			if (!prefixed) {
				to = onyx_word(t, "to");
				if (to)
					parserutils_vector_iterate(vector, ctx);
			}
			for (k = 0; to || prefixed; k++) {
				consumeWhitespace(vector, ctx);
				t = parserutils_vector_peek(vector, *ctx);
				if (onyx_word(t, "left")) h = -1;
				else if (onyx_word(t, "right")) h = 1;
				else if (onyx_word(t, "top")) v = -1;
				else if (onyx_word(t, "bottom")) v = 1;
				else break;
				parserutils_vector_iterate(vector, ctx);
			}
			if (prefixed) {		/* (legacy: the start side) */
				h = -h;
				v = -v;
			}
			if (h == 0 && v == 0) {
				if (to)
					return false;	/* "to" and nothing */
				*ctx = save;
			} else if (h != 0 && v != 0) {
				corner = true;
				cc[0] = (v < 0) ? 't' : 'b';
				cc[1] = (h < 0) ? 'l' : 'r';
				cc[2] = '\0';
			} else {
				deg = (v < 0) ? 0 : (h > 0) ? 90 : (v > 0) ? 180 : 270;
			}
		}
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_peek(vector, *ctx);
		if (*ctx != save) {
			if (t == NULL || !tokenIsChar(t, ','))
				return false;
			parserutils_vector_iterate(vector, ctx);
		}
		ob_puts(out, "L");
		if (corner) {
			ob_puts(out, ";c=");
			ob_puts(out, cc);
		} else {
			ob_puts(out, ";a=");
			ob_num(out, deg);
		}
		return onyx_stops(c, vector, ctx, out, false);
	}

	if (onyx_is(name, "radial-gradient")) {
		onyx_buf geo = { NULL, 0, 0, false };
		onyx_buf rad[2] = { { NULL, 0, 0, false }, { NULL, 0, 0, false } };
		int32_t save;
		int nrad = 0;
		bool circle = false, ellipse = false, any = false, ok = true;
		const char *size = "fc";

		consumeWhitespace(vector, ctx);
		save = *ctx;
		for (;;) {
			consumeWhitespace(vector, ctx);
			t = parserutils_vector_peek(vector, *ctx);
			if (onyx_word(t, "circle")) {
				circle = true;
			} else if (onyx_word(t, "ellipse")) {
				ellipse = true;
			} else if (onyx_word(t, "closest-side")) {
				size = "cs";
			} else if (onyx_word(t, "closest-corner")) {
				size = "cc";
			} else if (onyx_word(t, "farthest-side")) {
				size = "fs";
			} else if (onyx_word(t, "farthest-corner")) {
				size = "fc";
			} else if (onyx_word(t, "contain")) {		/* (legacy) */
				size = "cs";
			} else if (onyx_word(t, "cover")) {
				size = "fc";
			} else if (onyx_word(t, "at")) {
				parserutils_vector_iterate(vector, ctx);
				if (!onyx_position(c, vector, ctx, &geo))
					ok = false;
				any = true;
				break;
			} else if (nrad < 2 && onyx_len(c, vector, ctx, &rad[nrad], false)) {
				nrad++;
				any = true;
				continue;
			} else {
				break;
			}
			parserutils_vector_iterate(vector, ctx);
			any = true;
		}
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_peek(vector, *ctx);
		if (any) {
			if (!ok || t == NULL || !tokenIsChar(t, ',')) {
				free(geo.p); free(rad[0].p); free(rad[1].p);
				if (!prefixed)
					return false;
				/* Onyx: forgotten, not freed again below (a double free crashed) */
				geo.p = rad[0].p = rad[1].p = NULL;
				nrad = 0;
				circle = ellipse = false;
				size = "fc";
				*ctx = save;	/* (legacy: "center, circle, ..." -- as stops) */
			} else {
				parserutils_vector_iterate(vector, ctx);
			}
		}
		ob_puts(out, "R;sh=");
		ob_puts(out, (circle || (nrad == 1 && !ellipse)) ? "c" : "e");
		if (nrad > 0) {
			ob_puts(out, ";rx=");
			ob_puts(out, rad[0].p);
			ob_puts(out, ";ry=");
			ob_puts(out, rad[nrad - 1].p);
		} else {
			ob_puts(out, ";sz=");
			ob_puts(out, size);
		}
		if (geo.p != NULL)
			ob_puts(out, geo.p);
		else
			ob_puts(out, ";x=50%;y=50%");
		free(geo.p); free(rad[0].p); free(rad[1].p);
		return onyx_stops(c, vector, ctx, out, false);
	}

	if (onyx_is(name, "conic-gradient")) {
		onyx_buf geo = { NULL, 0, 0, false };
		float from = 0;
		bool any = false;

		consumeWhitespace(vector, ctx);
		t = parserutils_vector_peek(vector, *ctx);
		if (onyx_word(t, "from")) {
			parserutils_vector_iterate(vector, ctx);
			consumeWhitespace(vector, ctx);
			if (!onyx_angle(c, vector, ctx, &from))
				return false;
			any = true;
			consumeWhitespace(vector, ctx);
			t = parserutils_vector_peek(vector, *ctx);
		}
		if (onyx_word(t, "at")) {
			parserutils_vector_iterate(vector, ctx);
			if (!onyx_position(c, vector, ctx, &geo)) {
				free(geo.p);
				return false;
			}
			any = true;
		}
		if (any) {
			consumeWhitespace(vector, ctx);
			t = parserutils_vector_iterate(vector, ctx);
			if (t == NULL || !tokenIsChar(t, ',')) {
				free(geo.p);
				return false;
			}
		}
		ob_puts(out, "C;f=");
		ob_num(out, from);
		ob_puts(out, geo.p != NULL ? geo.p : ";x=50%;y=50%");
		free(geo.p);
		return onyx_stops(c, vector, ctx, out, true);
	}

	return false;
}

/* One <image> or none, at *ctx: its opv and operand appended to result. */
static css_error onyx_image(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	int32_t orig_ctx = *ctx;
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	bool prefixed;
	const char *fn = onyx_fn_name(t, &prefixed);
	lwc_string *str = NULL;
	uint32_t idx;
	css_error error;

	if (t == NULL)
		return CSS_INVALID;
	if (onyx_word(t, "none")) {
		parserutils_vector_iterate(vector, ctx);
		return css__stylesheet_style_appendOPV(result, CSS_PROP_BACKGROUND_IMAGE, 0,
				BACKGROUND_IMAGE_NONE);
	}
	if (t->type == CSS_TOKEN_URI) {
		parserutils_vector_iterate(vector, ctx);
		error = c->sheet->resolve(c->sheet->resolve_pw, c->sheet->url, t->idata, &str);
		if (error != CSS_OK) {
			*ctx = orig_ctx;
			return error;
		}
	} else if (strstr(fn, "gradient") != NULL) {
		onyx_buf b = { NULL, 0, 0, false };
		if (!onyx_gradient(c, vector, ctx, &b) || b.oom) {
			free(b.p);
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
		if (lwc_intern_string(b.p, b.n, &str) != lwc_error_ok) {
			free(b.p);
			*ctx = orig_ctx;
			return CSS_NOMEM;
		}
		free(b.p);
	} else if (onyx_is(fn, "image-set")) {
		/* image-set(url(a) 1x, ...): the first image */
		const css_token *u;
		parserutils_vector_iterate(vector, ctx);
		consumeWhitespace(vector, ctx);
		u = parserutils_vector_iterate(vector, ctx);
		if (u == NULL || (u->type != CSS_TOKEN_URI && u->type != CSS_TOKEN_STRING)) {
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
		error = c->sheet->resolve(c->sheet->resolve_pw, c->sheet->url, u->idata, &str);
		if (error != CSS_OK) {
			*ctx = orig_ctx;
			return error;
		}
		/* skip to the function's end */
		{
			int depth = 1;
			while (depth > 0 && (u = parserutils_vector_iterate(vector, ctx)) != NULL) {
				if (u->type == CSS_TOKEN_FUNCTION || tokenIsChar(u, '('))
					depth++;
				else if (tokenIsChar(u, ')'))
					depth--;
			}
		}
	} else {
		return CSS_INVALID;
	}

	/* (string_add takes the reference) */
	error = css__stylesheet_string_add(c->sheet, str, &idx);
	if (error == CSS_OK)
		error = css__stylesheet_style_appendOPV(result, CSS_PROP_BACKGROUND_IMAGE, 0,
				BACKGROUND_IMAGE_URI);
	if (error == CSS_OK)
		error = css__stylesheet_style_append(result, idx);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

/* Skip to the end of the declaration's value (the other layers), keeping a '!'. */
static void onyx_skip_layers(const parserutils_vector *vector, int32_t *ctx)
{
	const css_token *t;
	while ((t = parserutils_vector_peek(vector, *ctx)) != NULL && !tokenIsChar(t, '!'))
		parserutils_vector_iterate(vector, ctx);
}

/* background-image: none | <image> [, <image>]* -- the first layer */
css_error css__onyx_parse_background_image(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	enum flag_value flag;
	css_error error;

	if (t == NULL)
		return CSS_INVALID;
	flag = get_css_flag_value(c, t);
	if (flag != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		return css_stylesheet_style_flag_value(result, flag, CSS_PROP_BACKGROUND_IMAGE);
	}
	error = onyx_image(c, vector, ctx, result);
	if (error == CSS_OK) {
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_peek(vector, *ctx);
		if (t != NULL && tokenIsChar(t, ','))
			onyx_skip_layers(vector, ctx);
	}
	return error;
}

/* background-repeat: repeat-x | repeat-y | [repeat | space | round | no-repeat]{1,2}
 * (space and round: as repeat) [, ...]* */
css_error css__onyx_parse_background_repeat(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	int32_t orig_ctx = *ctx;
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	enum flag_value flag;
	int rep[2] = { -1, -1 }, n;		/* 1 repeat, 0 no-repeat */
	uint16_t value;

	if (t == NULL)
		return CSS_INVALID;
	flag = get_css_flag_value(c, t);
	if (flag != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		return css_stylesheet_style_flag_value(result, flag, CSS_PROP_BACKGROUND_REPEAT);
	}
	if (onyx_word(t, "repeat-x") || onyx_word(t, "repeat-y")) {
		value = onyx_word(t, "repeat-x") ? BACKGROUND_REPEAT_REPEAT_X :
				BACKGROUND_REPEAT_REPEAT_Y;
		parserutils_vector_iterate(vector, ctx);
	} else {
		for (n = 0; n < 2; n++) {
			consumeWhitespace(vector, ctx);
			t = parserutils_vector_peek(vector, *ctx);
			if (onyx_word(t, "repeat") || onyx_word(t, "space") || onyx_word(t, "round"))
				rep[n] = 1;
			else if (onyx_word(t, "no-repeat"))
				rep[n] = 0;
			else
				break;
			parserutils_vector_iterate(vector, ctx);
		}
		if (n == 0) {
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
		if (n == 1)
			rep[1] = rep[0];
		value = rep[0] ? (rep[1] ? BACKGROUND_REPEAT_REPEAT : BACKGROUND_REPEAT_REPEAT_X) :
				(rep[1] ? BACKGROUND_REPEAT_REPEAT_Y : BACKGROUND_REPEAT_NO_REPEAT);
	}
	consumeWhitespace(vector, ctx);
	t = parserutils_vector_peek(vector, *ctx);
	if (t != NULL && tokenIsChar(t, ','))
		onyx_skip_layers(vector, ctx);
	return css__stylesheet_style_appendOPV(result, CSS_PROP_BACKGROUND_REPEAT, 0, value);
}

/* ---- the background shorthand ------------------------------------------------------------- */

/* The end of the layer starting at ctx (a top-level ',' or the end / a '!'). */
static int32_t onyx_layer_end(const parserutils_vector *vector, int32_t ctx)
{
	const css_token *t;
	int depth = 0;
	for (; (t = parserutils_vector_peek(vector, ctx)) != NULL; ctx++) {
		if (depth == 0 && (tokenIsChar(t, ',') || tokenIsChar(t, '!')))
			break;
		if (t->type == CSS_TOKEN_FUNCTION || tokenIsChar(t, '('))
			depth++;
		else if (tokenIsChar(t, ')'))
			depth--;
	}
	return ctx;
}

/* A sub-vector of the tokens [s, e). */
static parserutils_vector *onyx_sub(const parserutils_vector *vector, int32_t s, int32_t e)
{
	parserutils_vector *sub;
	int32_t i;
	if (parserutils_vector_create(sizeof(css_token), 16, &sub) != PARSERUTILS_OK)
		return NULL;
	for (i = s; i < e; i++) {
		css_token t = *((const css_token *) parserutils_vector_peek(vector, i));
		if (parserutils_vector_append(sub, &t) != PARSERUTILS_OK) {
			parserutils_vector_destroy(sub);
			return NULL;
		}
	}
	return sub;
}

/* One layer (tokens [s, e)): each longhand into its style; the colour only when allowed. */
static css_error onyx_layer(css_language *c, const parserutils_vector *vector,
		int32_t s, int32_t e, bool colour_ok, css_style *st[6], bool got[6])
{
	parserutils_vector *sub = onyx_sub(vector, s, e);
	int32_t ctx = 0;
	css_error error = CSS_OK;
	const css_token *t;

	if (sub == NULL)
		return CSS_NOMEM;

	for (;;) {
		int32_t before;
		consumeWhitespace(sub, &ctx);
		t = parserutils_vector_peek(sub, ctx);
		if (t == NULL)
			break;
		before = ctx;
		if (!got[0] && css__parse_background_attachment(c, sub, &ctx, st[0]) == CSS_OK) {
			got[0] = true;
		} else if (colour_ok && !got[1] &&
				css__parse_background_color(c, sub, &ctx, st[1]) == CSS_OK) {
			got[1] = true;
		} else if (!got[2] && onyx_image(c, sub, &ctx, st[2]) == CSS_OK) {
			got[2] = true;
		} else if (!got[3] &&
				css__parse_background_position(c, sub, &ctx, st[3]) == CSS_OK) {
			got[3] = true;
			consumeWhitespace(sub, &ctx);
			t = parserutils_vector_peek(sub, ctx);
			if (t != NULL && tokenIsChar(t, '/')) {
				parserutils_vector_iterate(sub, &ctx);
				consumeWhitespace(sub, &ctx);
				if (css__parse_background_size(c, sub, &ctx, st[5]) != CSS_OK) {
					error = CSS_INVALID;
					break;
				}
				got[5] = true;
			}
		} else if (!got[4] &&
				css__onyx_parse_background_repeat(c, sub, &ctx, st[4]) == CSS_OK) {
			got[4] = true;
		} else if (onyx_word(t, "border-box") || onyx_word(t, "padding-box") ||
				onyx_word(t, "content-box") || onyx_word(t, "text")) {
			parserutils_vector_iterate(sub, &ctx);	/* (origin / clip: not kept) */
		} else {
			error = CSS_INVALID;
			break;
		}
		if (ctx == before) {
			error = CSS_INVALID;
			break;
		}
	}
	parserutils_vector_destroy(sub);
	return error;
}

css_error css__onyx_parse_background(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	static const opcode_t props[6] = {
		CSS_PROP_BACKGROUND_ATTACHMENT, CSS_PROP_BACKGROUND_COLOR,
		CSS_PROP_BACKGROUND_IMAGE, CSS_PROP_BACKGROUND_POSITION,
		CSS_PROP_BACKGROUND_REPEAT, CSS_PROP_BACKGROUND_SIZE
	};
	int32_t orig_ctx = *ctx, s, e, last_s, last_e, first_s = 0, first_e = 0;
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	css_style *st[6] = { NULL, NULL, NULL, NULL, NULL, NULL };
	css_style *cst[6] = { NULL, NULL, NULL, NULL, NULL, NULL };
	bool got[6] = { false, false, false, false, false, false };
	bool cgot[6] = { false, false, false, false, false, false };
	enum flag_value flag;
	css_error error = CSS_OK;
	int i, layers = 0;

	if (t == NULL)
		return CSS_INVALID;
	flag = get_css_flag_value(c, t);
	if (flag != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		for (i = 0; i < 6 && error == CSS_OK; i++)
			error = css_stylesheet_style_flag_value(result, flag, props[i]);
		return error;
	}

	for (i = 0; i < 6 && error == CSS_OK; i++) {
		error = css__stylesheet_style_create(c->sheet, &st[i]);
		if (error == CSS_OK)
			error = css__stylesheet_style_create(c->sheet, &cst[i]);
	}

	/* the layers: the first gives the image and its geometry, the last the colour */
	s = *ctx;
	last_s = last_e = s;
	for (;;) {
		e = onyx_layer_end(vector, s);
		if (layers == 0) {
			first_s = s;
			first_e = e;
		}
		last_s = s;
		last_e = e;
		layers++;
		t = parserutils_vector_peek(vector, e);
		if (t != NULL && tokenIsChar(t, ',')) {
			s = e + 1;
			continue;
		}
		*ctx = e;
		break;
	}
	if (error == CSS_OK)
		error = onyx_layer(c, vector, first_s, first_e, layers == 1, st, got);
	if (error == CSS_OK && layers > 1)
		error = onyx_layer(c, vector, last_s, last_e, true, cst, cgot);
	if (error == CSS_OK && layers == 1 && got[1]) {
		/* one layer: its colour is the last layer's */
		css_style *tmp = st[1];
		st[1] = cst[1];
		cst[1] = tmp;
		cgot[1] = true;
		got[1] = false;
	}

	/* the defaults of what was not given */
	if (error == CSS_OK) {
		if (!got[0])
			error = css__stylesheet_style_appendOPV(st[0], props[0], 0,
					BACKGROUND_ATTACHMENT_SCROLL);
		if (error == CSS_OK && !cgot[1])
			error = css__stylesheet_style_appendOPV(cst[1], props[1], 0,
					BACKGROUND_COLOR_TRANSPARENT);
		if (error == CSS_OK && !got[2])
			error = css__stylesheet_style_appendOPV(st[2], props[2], 0,
					BACKGROUND_IMAGE_NONE);
		if (error == CSS_OK && !got[3])
			error = css__stylesheet_style_appendOPV(st[3], props[3], 0,
					BACKGROUND_POSITION_HORZ_LEFT |
					BACKGROUND_POSITION_VERT_TOP);
		if (error == CSS_OK && !got[4])
			error = css__stylesheet_style_appendOPV(st[4], props[4], 0,
					BACKGROUND_REPEAT_REPEAT);
		if (error == CSS_OK && !got[5])
			error = css__stylesheet_style_appendOPV(st[5], props[5], 0,
					BACKGROUND_SIZE_AUTO);
	}

	if (error == CSS_OK) {
		for (i = 0; i < 6 && error == CSS_OK; i++)
			error = css__stylesheet_merge_style(result, i == 1 ? cst[1] : st[i]);
	}

	for (i = 0; i < 6; i++) {
		if (st[i] != NULL)
			css__stylesheet_style_destroy(st[i]);
		if (cst[i] != NULL)
			css__stylesheet_style_destroy(cst[i]);
	}
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}
