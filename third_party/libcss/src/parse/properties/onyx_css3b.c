/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: more CSS3 properties -- aspect-ratio, object-fit, object-position, transform and the
 * individual translate / scale / rotate, the grid properties (and their shorthands), the
 * overflow shorthand's two values, text-decoration's CSS3 shorthand.
 *
 * transform, translate, scale, rotate and the grid templates / placements are kept as a
 * canonical text (a string of the sheet), which the client parses (NetSurf:
 * content/handlers/html/onyx_grid.c, onyx_transform.c):
 *
 *   transform     functions separated by spaces, their arguments by commas, in px % em rem
 *                 vw vh (lengths), deg (angles) or plain numbers: translate(x,y) scale(x,y)
 *                 rotate(a) skew(ax,ay) matrix(a,b,c,d,e,f) (Onyx: the 3D functions flattened
 *                 -- rotateX/Y a scale, rotate3d and matrix3d their 2D part, perspective and
 *                 the z parts dropped)
 *   track list    space separated: <len> (px % em rem vw vh fr) auto min-content
 *                 max-content minmax(a,b) fit-content(len) repeat(<n>|auto-fill|auto-fit,
 *                 <tracks>) [line names]
 *   areas         the rows' strings joined by '/': "a a b/c c b"
 *   grid line     auto | <n> | span <n> | <name> | span <name> | <n> <name>
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/* ---- text ---------------------------------------------------------------------------------- */

typedef struct {
	char *p;
	size_t n, cap;
	bool oom;
} onyx_buf;

static void ob_putn(onyx_buf *b, const char *s, size_t n)
{
	if (b->oom)
		return;
	if (b->n + n + 1 > b->cap) {
		size_t cap = b->cap ? b->cap : 64;
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
	memcpy(b->p + b->n, s, n);
	b->n += n;
	b->p[b->n] = '\0';
}

static void ob_puts(onyx_buf *b, const char *s)
{
	ob_putn(b, s, strlen(s));
}

static void ob_num(onyx_buf *b, float v)
{
	char tmp[48], *e;
	snprintf(tmp, sizeof(tmp), "%.3f", v);	/* (the fixed point's precision) */
	e = tmp + strlen(tmp) - 1;
	while (e > tmp && *e == '0')
		*e-- = '\0';
	if (*e == '.')
		*e = '\0';
	if (strcmp(tmp, "-0") == 0)
		strcpy(tmp, "0");
	ob_puts(b, tmp);
}

static bool onyx_word(const css_token *t, const char *w)
{
	size_t n = strlen(w);
	return t != NULL && t->type == CSS_TOKEN_IDENT && lwc_string_length(t->idata) == n &&
			strncasecmp(lwc_string_data(t->idata), w, n) == 0;
}

static bool onyx_fn(const css_token *t, const char *w)
{
	size_t n = strlen(w);
	return t != NULL && t->type == CSS_TOKEN_FUNCTION && lwc_string_length(t->idata) == n &&
			strncasecmp(lwc_string_data(t->idata), w, n) == 0;
}

/* What a value may be: a length (with %), an angle, a number, a flex (fr). */
enum { ONYX_LEN = 1, ONYX_ANGLE = 2, ONYX_NUM = 4, ONYX_FR = 8, ONYX_PCTNUM = 16 };

/* A value token of one of the kinds: appended (canonical units). */
static bool onyx_value(css_language *c, const parserutils_vector *vector, int32_t *ctx,
		int kinds, onyx_buf *out)
{
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	int32_t save = *ctx;
	css_fixed len;
	uint32_t unit;
	const char *u = "";
	float v;

	if (t == NULL)
		return false;
	if (t->type == CSS_TOKEN_NUMBER) {
		size_t consumed;
		v = FIXTOFLT(css__number_from_lwc_string(t->idata, false, &consumed));
		if (consumed != lwc_string_length(t->idata))
			return false;
		if ((kinds & ONYX_NUM) == 0 && !(v == 0 && (kinds & (ONYX_LEN | ONYX_ANGLE))))
			return false;
		parserutils_vector_iterate(vector, ctx);
		ob_num(out, v);
		if ((kinds & ONYX_NUM) == 0)
			ob_puts(out, (kinds & ONYX_LEN) ? "px" : "deg");
		return true;
	}
	if (t->type == CSS_TOKEN_DIMENSION && (kinds & ONYX_FR)) {
		size_t consumed, ul;
		const char *d = lwc_string_data(t->idata);
		v = FIXTOFLT(css__number_from_lwc_string(t->idata, false, &consumed));
		ul = lwc_string_length(t->idata) - consumed;
		if (ul == 2 && strncasecmp(d + consumed, "fr", 2) == 0) {
			parserutils_vector_iterate(vector, ctx);
			ob_num(out, v);
			ob_puts(out, "fr");
			return true;
		}
	}
	if (t->type != CSS_TOKEN_DIMENSION && t->type != CSS_TOKEN_PERCENTAGE)
		return false;
	if (css__parse_unit_specifier(c, vector, ctx, UNIT_PX, &len, &unit) != CSS_OK) {
		*ctx = save;
		return false;
	}
	v = FIXTOFLT(len);
	if (unit == UNIT_PCT && (kinds & ONYX_PCTNUM)) {
		ob_num(out, v / 100);		/* scale(50%) = scale(0.5) */
		return true;
	}
	if ((unit & UNIT_ANGLE) && (kinds & ONYX_ANGLE)) {
		switch (unit) {
		case UNIT_RAD: v *= 57.29578f; break;
		case UNIT_GRAD: v *= 0.9f; break;
		case UNIT_TURN: v *= 360; break;
		default: break;
		}
		u = "deg";
	} else if ((unit & UNIT_LENGTH || unit == UNIT_PCT) && (kinds & ONYX_LEN)) {
		switch (unit) {
		case UNIT_PX: u = "px"; break;
		case UNIT_PCT: u = "%"; break;
		case UNIT_EM: u = "em"; break;
		case UNIT_REM: u = "rem"; break;
		case UNIT_VW: u = "vw"; break;
		case UNIT_VH: u = "vh"; break;
		case UNIT_EX: u = "em"; v *= 0.5f; break;
		case UNIT_CH: u = "em"; v *= 0.5f; break;
		case UNIT_PT: u = "px"; v *= 96.0f / 72; break;
		case UNIT_PC: u = "px"; v *= 16; break;
		case UNIT_IN: u = "px"; v *= 96; break;
		case UNIT_CM: u = "px"; v *= 96 / 2.54f; break;
		case UNIT_MM: u = "px"; v *= 96 / 25.4f; break;
		case UNIT_VMIN: u = "vw"; break;		/* (approximations) */
		case UNIT_VMAX: u = "vw"; break;
		default:
			*ctx = save;
			return false;
		}
	} else {
		*ctx = save;
		return false;
	}
	ob_num(out, v);
	ob_puts(out, u);
	return true;
}

/* Store the text as property op's value: OPV(op, SET) + the string's index. */
static css_error onyx_emit_text(css_language *c, css_style *result, opcode_t op,
		onyx_buf *b)
{
	lwc_string *s;
	uint32_t idx;
	css_error error;

	if (b->oom)
		return CSS_NOMEM;
	if (lwc_intern_string(b->p != NULL ? b->p : "", b->n, &s) != lwc_error_ok)
		return CSS_NOMEM;
	error = css__stylesheet_string_add(c->sheet, s, &idx);	/* (takes s) */
	if (error == CSS_OK)
		error = css__stylesheet_style_appendOPV(result, op, 0, ONYX_TEXT_SET);
	if (error == CSS_OK)
		error = css__stylesheet_style_append(result, idx);
	return error;
}

/* The CSS-wide keywords, and `none` (NONE): shared start of the text properties. */
static bool onyx_text_start(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result, opcode_t op, bool none_ok, css_error *error)
{
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	enum flag_value flag;

	if (t == NULL) {
		*error = CSS_INVALID;
		return true;
	}
	flag = get_css_flag_value(c, t);
	if (flag != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		*error = css_stylesheet_style_flag_value(result, flag, op);
		return true;
	}
	if (none_ok && onyx_word(t, "none")) {
		parserutils_vector_iterate(vector, ctx);
		*error = css__stylesheet_style_appendOPV(result, op, 0, ONYX_TEXT_NONE);
		return true;
	}
	return false;
}

static bool onyx_at_end(const parserutils_vector *vector, int32_t *ctx)
{
	const css_token *t;
	consumeWhitespace(vector, ctx);
	t = parserutils_vector_peek(vector, *ctx);
	return t == NULL || tokenIsChar(t, '!');
}

/* ',' between a function's arguments (whitespace around); true if one was there. */
static bool onyx_comma(const parserutils_vector *vector, int32_t *ctx)
{
	const css_token *t;
	consumeWhitespace(vector, ctx);
	t = parserutils_vector_peek(vector, *ctx);
	if (t != NULL && tokenIsChar(t, ',')) {
		parserutils_vector_iterate(vector, ctx);
		consumeWhitespace(vector, ctx);
		return true;
	}
	return false;
}

static bool onyx_close(const parserutils_vector *vector, int32_t *ctx)
{
	const css_token *t;
	consumeWhitespace(vector, ctx);
	t = parserutils_vector_iterate(vector, ctx);
	return t != NULL && tokenIsChar(t, ')');
}

/* ---- transform ------------------------------------------------------------------------------ */

/* The arguments of a transform function: n values of `kinds` (min..max of them, separated by
 * commas -- or, for the individual properties, by spaces), then ')'. */
static int onyx_args(css_language *c, const parserutils_vector *vector, int32_t *ctx,
		int kinds, int min, int max, onyx_buf *vals, bool commas)
{
	int n = 0;
	consumeWhitespace(vector, ctx);
	while (n < max) {
		if (n > 0) {
			if (commas && !onyx_comma(vector, ctx))
				break;
			consumeWhitespace(vector, ctx);
		}
		if (!onyx_value(c, vector, ctx, kinds, &vals[n]))
			break;
		n++;
	}
	if (n < min || (commas && !onyx_close(vector, ctx)))
		return -1;
	return n;
}

static void onyx_free_vals(onyx_buf *v, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		free(v[i].p);
		v[i].p = NULL;
		v[i].n = v[i].cap = 0;
	}
}

/* One transform function (ctx at its token): its canonical form appended. */
static bool onyx_transform_fn(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, onyx_buf *out)
{
	const css_token *t = parserutils_vector_iterate(vector, ctx);
	onyx_buf v[6];
	int n;
	bool ok = true;

	memset(v, 0, sizeof(v));
	if (onyx_fn(t, "translate") || onyx_fn(t, "translate3d")) {
		n = onyx_args(c, vector, ctx, ONYX_LEN, 1, onyx_fn(t, "translate") ? 2 : 3, v, true);
		if (n < 0) {
			ok = false;
		} else {
			ob_puts(out, "translate(");
			ob_puts(out, v[0].p);
			ob_puts(out, ",");
			ob_puts(out, n > 1 ? v[1].p : "0px");
			ob_puts(out, ")");
		}
		onyx_free_vals(v, 6);
	} else if (onyx_fn(t, "translatex") || onyx_fn(t, "translatey") ||
			onyx_fn(t, "translatez")) {
		bool y = onyx_fn(t, "translatey"), z = onyx_fn(t, "translatez");
		n = onyx_args(c, vector, ctx, ONYX_LEN, 1, 1, v, true);
		if (n < 0) {
			ok = false;
		} else if (!z) {
			ob_puts(out, "translate(");
			ob_puts(out, y ? "0px" : v[0].p);
			ob_puts(out, ",");
			ob_puts(out, y ? v[0].p : "0px");
			ob_puts(out, ")");
		}
		onyx_free_vals(v, 6);
	} else if (onyx_fn(t, "scale") || onyx_fn(t, "scale3d")) {
		n = onyx_args(c, vector, ctx, ONYX_NUM | ONYX_PCTNUM, 1,
				onyx_fn(t, "scale") ? 2 : 3, v, true);
		if (n < 0) {
			ok = false;
		} else {
			ob_puts(out, "scale(");
			ob_puts(out, v[0].p);
			ob_puts(out, ",");
			ob_puts(out, n > 1 ? v[1].p : v[0].p);
			ob_puts(out, ")");
		}
		onyx_free_vals(v, 6);
	} else if (onyx_fn(t, "scalex") || onyx_fn(t, "scaley") || onyx_fn(t, "scalez")) {
		bool y = onyx_fn(t, "scaley"), z = onyx_fn(t, "scalez");
		n = onyx_args(c, vector, ctx, ONYX_NUM | ONYX_PCTNUM, 1, 1, v, true);
		if (n < 0) {
			ok = false;
		} else if (!z) {
			ob_puts(out, "scale(");
			ob_puts(out, y ? "1" : v[0].p);
			ob_puts(out, ",");
			ob_puts(out, y ? v[0].p : "1");
			ob_puts(out, ")");
		}
		onyx_free_vals(v, 6);
	} else if (onyx_fn(t, "rotate") || onyx_fn(t, "rotatez")) {
		n = onyx_args(c, vector, ctx, ONYX_ANGLE, 1, 1, v, true);
		if (n < 0) {
			ok = false;
		} else {
			ob_puts(out, "rotate(");
			ob_puts(out, v[0].p);
			ob_puts(out, ")");
		}
		onyx_free_vals(v, 6);
	} else if (onyx_fn(t, "skew") || onyx_fn(t, "skewx") || onyx_fn(t, "skewy")) {
		bool y = onyx_fn(t, "skewy");
		n = onyx_args(c, vector, ctx, ONYX_ANGLE, 1, onyx_fn(t, "skew") ? 2 : 1, v, true);
		if (n < 0) {
			ok = false;
		} else {
			ob_puts(out, "skew(");
			ob_puts(out, y ? "0deg" : v[0].p);
			ob_puts(out, ",");
			ob_puts(out, y ? v[0].p : (n > 1 ? v[1].p : "0deg"));
			ob_puts(out, ")");
		}
		onyx_free_vals(v, 6);
	} else if (onyx_fn(t, "matrix")) {
		n = onyx_args(c, vector, ctx, ONYX_NUM, 6, 6, v, true);
		if (n < 0) {
			ok = false;
		} else {
			int i;
			ob_puts(out, "matrix(");
			for (i = 0; i < 6; i++) {
				if (i > 0)
					ob_puts(out, ",");
				ob_puts(out, v[i].p);
			}
			ob_puts(out, ")");
		}
		onyx_free_vals(v, 6);
	} else if (onyx_fn(t, "rotatex") || onyx_fn(t, "rotatey")) {
		/* Onyx: 3D flattened (no perspective): the plane's projection, a scale by
		 * the cosine across the axis */
		n = onyx_args(c, vector, ctx, ONYX_ANGLE, 1, 1, v, true);
		if (n < 0) {
			ok = false;
		} else {
			float k = cosf(strtof(v[0].p, NULL) * 0.01745329f);
			bool x = onyx_fn(t, "rotatex");
			ob_puts(out, "scale(");
			ob_num(out, x ? 1 : k);
			ob_puts(out, ",");
			ob_num(out, x ? k : 1);
			ob_puts(out, ")");
		}
		onyx_free_vals(v, 6);
	} else if (onyx_fn(t, "rotate3d")) {
		/* Onyx: the rotation's 2D part (Rodrigues' formula), flattened */
		for (n = 0; n < 3; n++) {
			consumeWhitespace(vector, ctx);
			if (!onyx_value(c, vector, ctx, ONYX_NUM, &v[n]) ||
			    !onyx_comma(vector, ctx))
				break;
		}
		if (n == 3) {
			float ax = strtof(v[0].p, NULL), ay = strtof(v[1].p, NULL),
					az = strtof(v[2].p, NULL), l, s, k, q;
			onyx_buf a = { NULL, 0, 0, false };
			l = sqrtf(ax * ax + ay * ay + az * az);
			if (!onyx_value(c, vector, ctx, ONYX_ANGLE, &a) ||
			    !onyx_close(vector, ctx) || l == 0) {
				ok = false;
			} else {
				float r = strtof(a.p, NULL) * 0.01745329f;
				ax /= l; ay /= l; az /= l;
				s = sinf(r);
				k = cosf(r);
				q = 1 - k;
				ob_puts(out, "matrix(");
				ob_num(out, k + ax * ax * q);
				ob_puts(out, ",");
				ob_num(out, ay * ax * q + az * s);
				ob_puts(out, ",");
				ob_num(out, ax * ay * q - az * s);
				ob_puts(out, ",");
				ob_num(out, k + ay * ay * q);
				ob_puts(out, ",0,0)");
			}
			free(a.p);
		} else {
			ok = false;
		}
		onyx_free_vals(v, 6);
	} else if (onyx_fn(t, "matrix3d")) {
		/* Onyx: flattened -- the 2D part of the 4x4 (column-major) matrix */
		onyx_buf m[16];
		memset(m, 0, sizeof(m));
		n = onyx_args(c, vector, ctx, ONYX_NUM, 16, 16, m, true);
		if (n < 0) {
			ok = false;
		} else {
			static const int pick[6] = { 0, 1, 4, 5, 12, 13 };
			int i;
			ob_puts(out, "matrix(");
			for (i = 0; i < 6; i++) {
				if (i > 0)
					ob_puts(out, ",");
				ob_puts(out, m[pick[i]].p);
			}
			ob_puts(out, ")");
		}
		onyx_free_vals(m, 16);
	} else if (onyx_fn(t, "perspective")) {
		/* 3D: accepted, not drawn (flattened: no perspective) */
		int depth = 1;
		while (depth > 0 && (t = parserutils_vector_iterate(vector, ctx)) != NULL) {
			if (t->type == CSS_TOKEN_FUNCTION || tokenIsChar(t, '('))
				depth++;
			else if (tokenIsChar(t, ')'))
				depth--;
		}
		ok = (depth == 0);
	} else {
		ok = false;
	}
	return ok;
}

css_error css__parse_transform(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	int32_t orig_ctx = *ctx;
	onyx_buf b = { NULL, 0, 0, false };
	css_error error;

	if (onyx_text_start(c, vector, ctx, result, CSS_PROP_TRANSFORM, true, &error))
		return error;
	while (!onyx_at_end(vector, ctx)) {
		size_t before = b.n;
		if (b.n > 0)
			ob_puts(&b, " ");
		if (!onyx_transform_fn(c, vector, ctx, &b)) {
			free(b.p);
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
		if (b.n == before + 1)
			b.n = before;		/* (a 3D function: nothing written) */
	}
	if (b.n == 0)
		ob_puts(&b, "");
	error = onyx_emit_text(c, result, CSS_PROP_TRANSFORM, &b);
	free(b.p);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

/* translate: none | <length-percentage> [<length-percentage> <length>?]? */
css_error css__parse_translate(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	int32_t orig_ctx = *ctx;
	onyx_buf v[3], b = { NULL, 0, 0, false };
	css_error error;
	int n;

	if (onyx_text_start(c, vector, ctx, result, CSS_PROP_TRANSLATE, true, &error))
		return error;
	memset(v, 0, sizeof(v));
	n = onyx_args(c, vector, ctx, ONYX_LEN, 1, 3, v, false);
	if (n < 0 || !onyx_at_end(vector, ctx)) {
		onyx_free_vals(v, 3);
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	ob_puts(&b, "translate(");
	ob_puts(&b, v[0].p);
	ob_puts(&b, ",");
	ob_puts(&b, n > 1 ? v[1].p : "0px");
	ob_puts(&b, ")");
	onyx_free_vals(v, 3);
	error = onyx_emit_text(c, result, CSS_PROP_TRANSLATE, &b);
	free(b.p);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

/* scale: none | [<number> | <percentage>]{1,3} */
css_error css__parse_scale(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	int32_t orig_ctx = *ctx;
	onyx_buf v[3], b = { NULL, 0, 0, false };
	css_error error;
	int n;

	if (onyx_text_start(c, vector, ctx, result, CSS_PROP_SCALE, true, &error))
		return error;
	memset(v, 0, sizeof(v));
	n = onyx_args(c, vector, ctx, ONYX_NUM | ONYX_PCTNUM, 1, 3, v, false);
	if (n < 0 || !onyx_at_end(vector, ctx)) {
		onyx_free_vals(v, 3);
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	ob_puts(&b, "scale(");
	ob_puts(&b, v[0].p);
	ob_puts(&b, ",");
	ob_puts(&b, n > 1 ? v[1].p : v[0].p);
	ob_puts(&b, ")");
	onyx_free_vals(v, 3);
	error = onyx_emit_text(c, result, CSS_PROP_SCALE, &b);
	free(b.p);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

/* rotate: none | <angle> | [x | y | z | <number>{3}] && <angle> (z drawn; x and y flattened
 * to a scale; an axis vector not drawn) */
css_error css__parse_rotate(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	int32_t orig_ctx = *ctx;
	onyx_buf a = { NULL, 0, 0, false }, b = { NULL, 0, 0, false }, dummy = { NULL, 0, 0, false };
	bool flat = true;
	char axis = 'z';	/* (Onyx: x and y flattened, a scale) */
	css_error error;

	if (onyx_text_start(c, vector, ctx, result, CSS_PROP_ROTATE, true, &error))
		return error;
	while (!onyx_at_end(vector, ctx)) {
		const css_token *t = parserutils_vector_peek(vector, *ctx);
		if (onyx_word(t, "x") || onyx_word(t, "y")) {
			axis = onyx_word(t, "x") ? 'x' : 'y';
			parserutils_vector_iterate(vector, ctx);
		} else if (onyx_word(t, "z")) {
			parserutils_vector_iterate(vector, ctx);
		} else if (a.n == 0 && onyx_value(c, vector, ctx, ONYX_ANGLE, &a)) {
			;
		} else if (onyx_value(c, vector, ctx, ONYX_NUM, &dummy)) {
			flat = false;		/* (an axis vector) */
		} else {
			free(a.p);
			free(dummy.p);
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
	}
	free(dummy.p);
	if (a.n == 0) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	if (flat && axis != 'z') {
		float k = cosf(strtof(a.p, NULL) * 0.01745329f);
		ob_puts(&b, "scale(");
		ob_num(&b, axis == 'x' ? 1 : k);
		ob_puts(&b, ",");
		ob_num(&b, axis == 'x' ? k : 1);
		ob_puts(&b, ")");
	} else {
		ob_puts(&b, "rotate(");
		ob_puts(&b, flat ? a.p : "0deg");
		ob_puts(&b, ")");
	}
	free(a.p);
	error = onyx_emit_text(c, result, CSS_PROP_ROTATE, &b);
	free(b.p);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

/* ---- aspect-ratio / object-fit / object-position ---------------------------------------- */

/* aspect-ratio: auto || <number> [ / <number> ]? */
css_error css__parse_aspect_ratio(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	int32_t orig_ctx = *ctx;
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	enum flag_value flag;
	bool is_auto = false, has_ratio = false;
	css_fixed w = 0, h = INTTOFIX(1);
	css_error error;

	if (t == NULL)
		return CSS_INVALID;
	flag = get_css_flag_value(c, t);
	if (flag != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		return css_stylesheet_style_flag_value(result, flag, CSS_PROP_ASPECT_RATIO);
	}
	while (!onyx_at_end(vector, ctx)) {
		size_t consumed;
		t = parserutils_vector_peek(vector, *ctx);
		if (!is_auto && onyx_word(t, "auto")) {
			is_auto = true;
			parserutils_vector_iterate(vector, ctx);
		} else if (!has_ratio && t->type == CSS_TOKEN_NUMBER) {
			w = css__number_from_lwc_string(t->idata, false, &consumed);
			parserutils_vector_iterate(vector, ctx);
			consumeWhitespace(vector, ctx);
			t = parserutils_vector_peek(vector, *ctx);
			if (t != NULL && tokenIsChar(t, '/')) {
				parserutils_vector_iterate(vector, ctx);
				consumeWhitespace(vector, ctx);
				t = parserutils_vector_iterate(vector, ctx);
				if (t == NULL || t->type != CSS_TOKEN_NUMBER) {
					*ctx = orig_ctx;
					return CSS_INVALID;
				}
				h = css__number_from_lwc_string(t->idata, false, &consumed);
			}
			if (w < 0 || h < 0) {
				*ctx = orig_ctx;
				return CSS_INVALID;
			}
			has_ratio = true;
		} else {
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
	}
	if (!is_auto && !has_ratio) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	if (!has_ratio)
		return css__stylesheet_style_appendOPV(result, CSS_PROP_ASPECT_RATIO, 0,
				ASPECT_RATIO_AUTO);
	error = css__stylesheet_style_appendOPV(result, CSS_PROP_ASPECT_RATIO, 0,
			ASPECT_RATIO_SET | (is_auto ? ASPECT_RATIO_AUTO_FLAG : 0));
	if (error == CSS_OK)
		error = css__stylesheet_style_vappend(result, 2, w, h);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

/* object-position: as background-position (its parser, its bytecode) */
css_error css__parse_object_position(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	css_style *tmp = NULL;
	css_error error;
	uint32_t k;

	error = css__stylesheet_style_create(c->sheet, &tmp);
	if (error != CSS_OK)
		return error;
	error = css__parse_background_position(c, vector, ctx, tmp);
	if (error == CSS_OK && tmp->used > 0) {
		error = css__stylesheet_style_append(result, buildOPV(CSS_PROP_OBJECT_POSITION,
				getFlags(tmp->bytecode[0]), getValue(tmp->bytecode[0])));
		for (k = 1; k < tmp->used && error == CSS_OK; k++)
			error = css__stylesheet_style_append(result, tmp->bytecode[k]);
	}
	css__stylesheet_style_destroy(tmp);
	return error;
}

/* ---- the grid ------------------------------------------------------------------------------ */

/* [ <line names> ]: "[a b]" appended */
static bool onyx_line_names(const parserutils_vector *vector, int32_t *ctx, onyx_buf *out)
{
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	bool first = true;

	if (t == NULL || !tokenIsChar(t, '['))
		return false;
	parserutils_vector_iterate(vector, ctx);
	ob_puts(out, "[");
	for (;;) {
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_iterate(vector, ctx);
		if (t == NULL)
			return false;
		if (tokenIsChar(t, ']'))
			break;
		if (t->type != CSS_TOKEN_IDENT)
			return false;
		if (!first)
			ob_puts(out, " ");
		ob_putn(out, lwc_string_data(t->idata), lwc_string_length(t->idata));
		first = false;
	}
	ob_puts(out, "]");
	return true;
}

/* A track breadth: <length-percentage> | <flex> | min-content | max-content | auto */
static bool onyx_breadth(css_language *c, const parserutils_vector *vector, int32_t *ctx,
		bool flex_ok, onyx_buf *out)
{
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	if (onyx_word(t, "auto") || onyx_word(t, "min-content") || onyx_word(t, "max-content")) {
		ob_putn(out, lwc_string_data(t->idata), lwc_string_length(t->idata));
		parserutils_vector_iterate(vector, ctx);
		return true;
	}
	return onyx_value(c, vector, ctx, ONYX_LEN | (flex_ok ? ONYX_FR : 0), out);
}

/* A track size: <breadth> | minmax(<breadth>, <breadth>) | fit-content(<length-%>) */
static bool onyx_track_size(css_language *c, const parserutils_vector *vector, int32_t *ctx,
		onyx_buf *out)
{
	const css_token *t = parserutils_vector_peek(vector, *ctx);

	if (onyx_fn(t, "minmax")) {
		parserutils_vector_iterate(vector, ctx);
		consumeWhitespace(vector, ctx);
		ob_puts(out, "minmax(");
		if (!onyx_breadth(c, vector, ctx, false, out) || !onyx_comma(vector, ctx))
			return false;
		ob_puts(out, ",");
		if (!onyx_breadth(c, vector, ctx, true, out) || !onyx_close(vector, ctx))
			return false;
		ob_puts(out, ")");
		return true;
	}
	if (onyx_fn(t, "fit-content")) {
		parserutils_vector_iterate(vector, ctx);
		consumeWhitespace(vector, ctx);
		ob_puts(out, "fit-content(");
		if (!onyx_value(c, vector, ctx, ONYX_LEN, out) || !onyx_close(vector, ctx))
			return false;
		ob_puts(out, ")");
		return true;
	}
	return onyx_breadth(c, vector, ctx, true, out);
}

/* A track list (up to its end, or a stop char): names, sizes, repeat(). */
static bool onyx_track_list(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, onyx_buf *out, char stop, bool in_repeat)
{
	int n = 0;

	for (;;) {
		const css_token *t;
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_peek(vector, *ctx);
		if (t == NULL || tokenIsChar(t, '!') || (stop && tokenIsChar(t, stop)))
			break;
		if (n > 0)
			ob_puts(out, " ");
		if (tokenIsChar(t, '[')) {
			if (!onyx_line_names(vector, ctx, out))
				return false;
		} else if (!in_repeat && onyx_fn(t, "repeat")) {
			parserutils_vector_iterate(vector, ctx);
			consumeWhitespace(vector, ctx);
			t = parserutils_vector_peek(vector, *ctx);
			ob_puts(out, "repeat(");
			if (onyx_word(t, "auto-fill") || onyx_word(t, "auto-fit")) {
				ob_putn(out, lwc_string_data(t->idata), lwc_string_length(t->idata));
				parserutils_vector_iterate(vector, ctx);
			} else if (t != NULL && t->type == CSS_TOKEN_NUMBER) {
				size_t consumed;
				css_fixed k = css__number_from_lwc_string(t->idata, true, &consumed);
				if (FIXTOINT(k) < 1)
					return false;
				ob_num(out, (float) FIXTOINT(k));
				parserutils_vector_iterate(vector, ctx);
			} else {
				return false;
			}
			if (!onyx_comma(vector, ctx))
				return false;
			ob_puts(out, ",");
			if (!onyx_track_list(c, vector, ctx, out, ')', true))
				return false;
			if (!onyx_close(vector, ctx))
				return false;
			ob_puts(out, ")");
		} else if (!onyx_track_size(c, vector, ctx, out)) {
			return false;
		}
		n++;
	}
	return n > 0 && !out->oom;
}

static css_error onyx_track_prop(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result, opcode_t op, bool none_ok)
{
	int32_t orig_ctx = *ctx;
	onyx_buf b = { NULL, 0, 0, false };
	css_error error;

	if (onyx_text_start(c, vector, ctx, result, op, none_ok, &error))
		return error;
	if (!onyx_track_list(c, vector, ctx, &b, 0, false) || !onyx_at_end(vector, ctx)) {
		free(b.p);
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	error = onyx_emit_text(c, result, op, &b);
	free(b.p);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

css_error css__parse_grid_template_columns(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	return onyx_track_prop(c, vector, ctx, result, CSS_PROP_GRID_TEMPLATE_COLUMNS, true);
}

css_error css__parse_grid_template_rows(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	return onyx_track_prop(c, vector, ctx, result, CSS_PROP_GRID_TEMPLATE_ROWS, true);
}

css_error css__parse_grid_auto_columns(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	return onyx_track_prop(c, vector, ctx, result, CSS_PROP_GRID_AUTO_COLUMNS, false);
}

css_error css__parse_grid_auto_rows(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	return onyx_track_prop(c, vector, ctx, result, CSS_PROP_GRID_AUTO_ROWS, false);
}

/* The cells of a grid-template-areas string, single spaced: names, and "." for each run of
 * dots (a null cell). */
static void onyx_area_cells(lwc_string *str, onyx_buf *out)
{
	const char *s = lwc_string_data(str);
	size_t n = lwc_string_length(str), i = 0;
	bool first = true;

	while (i < n) {
		size_t j;
		if (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == '\f') {
			i++;
			continue;
		}
		if (!first)
			ob_puts(out, " ");
		first = false;
		if (s[i] == '.') {
			while (i < n && s[i] == '.')
				i++;
			ob_puts(out, ".");
			continue;
		}
		for (j = i; j < n && s[j] != ' ' && s[j] != '\t' && s[j] != '\n' &&
				s[j] != '\r' && s[j] != '\f' && s[j] != '.'; j++)
			;
		ob_putn(out, s + i, j - i);
		i = j;
	}
}

/* grid-template-areas: none | <string>+ */
css_error css__parse_grid_template_areas(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	int32_t orig_ctx = *ctx;
	onyx_buf b = { NULL, 0, 0, false };
	css_error error;
	int rows = 0;

	if (onyx_text_start(c, vector, ctx, result, CSS_PROP_GRID_TEMPLATE_AREAS, true,
			&error))
		return error;
	while (!onyx_at_end(vector, ctx)) {
		const css_token *t = parserutils_vector_iterate(vector, ctx);
		if (t == NULL || t->type != CSS_TOKEN_STRING) {
			free(b.p);
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
		if (rows++ > 0)
			ob_puts(&b, "/");
		onyx_area_cells(t->idata, &b);
	}
	if (rows == 0) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	error = onyx_emit_text(c, result, CSS_PROP_GRID_TEMPLATE_AREAS, &b);
	free(b.p);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

/* grid-auto-flow: [ row | column ] || dense */
css_error css__parse_grid_auto_flow(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	int32_t orig_ctx = *ctx;
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	enum flag_value flag;
	bool column = false, dense = false, any = false;

	if (t == NULL)
		return CSS_INVALID;
	flag = get_css_flag_value(c, t);
	if (flag != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		return css_stylesheet_style_flag_value(result, flag, CSS_PROP_GRID_AUTO_FLOW);
	}
	while (!onyx_at_end(vector, ctx)) {
		t = parserutils_vector_peek(vector, *ctx);
		if (onyx_word(t, "row"))
			column = false;
		else if (onyx_word(t, "column"))
			column = true;
		else if (onyx_word(t, "dense"))
			dense = true;
		else {
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
		parserutils_vector_iterate(vector, ctx);
		any = true;
	}
	if (!any) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	return css__stylesheet_style_appendOPV(result, CSS_PROP_GRID_AUTO_FLOW, 0,
			(column ? GRID_AUTO_FLOW_COLUMN : GRID_AUTO_FLOW_ROW) |
			(dense ? GRID_AUTO_FLOW_DENSE : 0));
}

/* A grid line (up to a '/' or the end): auto | <n> | span <n> | <name> | span <name> |
 * <n> <name>. Writes nothing for auto; *is_name for a lone name. */
static bool onyx_grid_line(const parserutils_vector *vector, int32_t *ctx, onyx_buf *out,
		bool *is_name)
{
	bool span = false, any = false;
	int nums = 0, names = 0;

	*is_name = false;
	for (;;) {
		const css_token *t;
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_peek(vector, *ctx);
		if (t == NULL || tokenIsChar(t, '/') || tokenIsChar(t, '!'))
			break;
		if (onyx_word(t, "auto") && !any) {
			parserutils_vector_iterate(vector, ctx);
			consumeWhitespace(vector, ctx);
			t = parserutils_vector_peek(vector, *ctx);
			return t == NULL || tokenIsChar(t, '/') || tokenIsChar(t, '!');
		}
		if (onyx_word(t, "span") && !span) {
			span = true;
		} else if (t->type == CSS_TOKEN_NUMBER && nums == 0) {
			size_t consumed;
			css_fixed k = css__number_from_lwc_string(t->idata, true, &consumed);
			if (k == 0 || (span && k < 0))
				return false;
			nums++;
		} else if (t->type == CSS_TOKEN_IDENT && names == 0) {
			names++;
		} else {
			return false;
		}
		if (out->n > 0)
			ob_puts(out, " ");
		ob_putn(out, lwc_string_data(t->idata), lwc_string_length(t->idata));
		parserutils_vector_iterate(vector, ctx);
		any = true;
	}
	if (!any)
		return false;
	if (span && nums == 0 && names == 0)
		return false;
	*is_name = (!span && nums == 0 && names == 1);
	return !out->oom;
}

static css_error onyx_emit_line(css_language *c, css_style *result, opcode_t op,
		onyx_buf *b)
{
	if (b->n == 0)
		return css__stylesheet_style_appendOPV(result, op, 0, ONYX_TEXT_NONE);
	return onyx_emit_text(c, result, op, b);
}

static css_error onyx_line_prop(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result, opcode_t op)
{
	int32_t orig_ctx = *ctx;
	onyx_buf b = { NULL, 0, 0, false };
	bool is_name;
	css_error error;

	if (onyx_text_start(c, vector, ctx, result, op, false, &error))
		return error;
	if (!onyx_grid_line(vector, ctx, &b, &is_name) || !onyx_at_end(vector, ctx)) {
		free(b.p);
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	error = onyx_emit_line(c, result, op, &b);
	free(b.p);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

css_error css__parse_grid_row_start(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return onyx_line_prop(c, vector, ctx, result, CSS_PROP_GRID_ROW_START);
}

css_error css__parse_grid_row_end(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return onyx_line_prop(c, vector, ctx, result, CSS_PROP_GRID_ROW_END);
}

css_error css__parse_grid_column_start(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return onyx_line_prop(c, vector, ctx, result, CSS_PROP_GRID_COLUMN_START);
}

css_error css__parse_grid_column_end(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return onyx_line_prop(c, vector, ctx, result, CSS_PROP_GRID_COLUMN_END);
}

/* grid-row / grid-column: <line> [ / <line> ]? ; grid-area: <line> [ / <line> ]{0,3} */
static css_error onyx_lines_shorthand(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result, const opcode_t *props, int nprops)
{
	int32_t orig_ctx = *ctx;
	onyx_buf b[4];
	bool is_name[4] = { false, false, false, false };
	css_error error = CSS_OK;
	int n = 0, i;
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	enum flag_value flag;

	if (t == NULL)
		return CSS_INVALID;
	flag = get_css_flag_value(c, t);
	if (flag != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		for (i = 0; i < nprops && error == CSS_OK; i++)
			error = css_stylesheet_style_flag_value(result, flag, props[i]);
		return error;
	}
	memset(b, 0, sizeof(b));
	for (;;) {
		if (n == nprops || !onyx_grid_line(vector, ctx, &b[n], &is_name[n])) {
			error = CSS_INVALID;
			break;
		}
		n++;
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_peek(vector, *ctx);
		if (t != NULL && tokenIsChar(t, '/')) {
			parserutils_vector_iterate(vector, ctx);
			continue;
		}
		break;
	}
	if (error == CSS_OK && !onyx_at_end(vector, ctx))
		error = CSS_INVALID;
	if (error == CSS_OK) {
		/* the missing ones: a lone name repeats (its end is the name), else auto */
		for (i = n; i < nprops; i++) {
			int from = (nprops == 4) ? ((i == 1) ? 0 : i - 2) : 0;
			if (is_name[from]) {
				ob_puts(&b[i], b[from].p);
				is_name[i] = true;
			}
		}
		for (i = 0; i < nprops && error == CSS_OK; i++)
			error = onyx_emit_line(c, result, props[i], &b[i]);
	}
	for (i = 0; i < 4; i++)
		free(b[i].p);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

css_error css__parse_grid_row(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	static const opcode_t props[2] = { CSS_PROP_GRID_ROW_START, CSS_PROP_GRID_ROW_END };
	return onyx_lines_shorthand(c, vector, ctx, result, props, 2);
}

css_error css__parse_grid_column(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	static const opcode_t props[2] = { CSS_PROP_GRID_COLUMN_START,
			CSS_PROP_GRID_COLUMN_END };
	return onyx_lines_shorthand(c, vector, ctx, result, props, 2);
}

css_error css__parse_grid_area(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	/* row-start / column-start / row-end / column-end */
	static const opcode_t props[4] = { CSS_PROP_GRID_ROW_START,
			CSS_PROP_GRID_COLUMN_START, CSS_PROP_GRID_ROW_END,
			CSS_PROP_GRID_COLUMN_END };
	return onyx_lines_shorthand(c, vector, ctx, result, props, 4);
}

/* grid-template: none | <rows> / <columns> | [ <names>? <string> <size>? <names>? ]+
 * [ / <columns> ]? */
css_error css__parse_grid_template(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	static const opcode_t props[3] = { CSS_PROP_GRID_TEMPLATE_ROWS,
			CSS_PROP_GRID_TEMPLATE_COLUMNS, CSS_PROP_GRID_TEMPLATE_AREAS };
	int32_t orig_ctx = *ctx;
	onyx_buf rows = { NULL, 0, 0, false }, cols = { NULL, 0, 0, false };
	onyx_buf areas = { NULL, 0, 0, false };
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	enum flag_value flag;
	css_error error = CSS_OK;
	int i;

	if (t == NULL)
		return CSS_INVALID;
	flag = get_css_flag_value(c, t);
	if (flag != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		for (i = 0; i < 3 && error == CSS_OK; i++)
			error = css_stylesheet_style_flag_value(result, flag, props[i]);
		return error;
	}
	if (onyx_word(t, "none")) {
		parserutils_vector_iterate(vector, ctx);
		for (i = 0; i < 3 && error == CSS_OK; i++)
			error = css__stylesheet_style_appendOPV(result, props[i], 0,
					ONYX_TEXT_NONE);
		return error;
	}

	/* the areas form: strings, each with its row's size */
	{
		int32_t k = *ctx;
		consumeWhitespace(vector, &k);
		while ((t = parserutils_vector_peek(vector, k)) != NULL && tokenIsChar(t, '[')) {
			while ((t = parserutils_vector_iterate(vector, &k)) != NULL &&
					!tokenIsChar(t, ']'))
				;
			consumeWhitespace(vector, &k);
		}
		t = parserutils_vector_peek(vector, k);
	}
	if (t != NULL && t->type == CSS_TOKEN_STRING) {
		int nrows = 0;
		for (;;) {
			consumeWhitespace(vector, ctx);
			t = parserutils_vector_peek(vector, *ctx);
			if (t == NULL || tokenIsChar(t, '/') || tokenIsChar(t, '!'))
				break;
			if (tokenIsChar(t, '[')) {
				if (rows.n > 0)
					ob_puts(&rows, " ");
				if (!onyx_line_names(vector, ctx, &rows)) {
					error = CSS_INVALID;
					break;
				}
				continue;
			}
			if (t->type != CSS_TOKEN_STRING) {
				error = CSS_INVALID;
				break;
			}
			/* the row's string: its cells */
			if (nrows > 0)
				ob_puts(&areas, "/");
			onyx_area_cells(t->idata, &areas);
			parserutils_vector_iterate(vector, ctx);
			nrows++;
			/* its size */
			consumeWhitespace(vector, ctx);
			t = parserutils_vector_peek(vector, *ctx);
			if (rows.n > 0)
				ob_puts(&rows, " ");
			if (t == NULL || t->type == CSS_TOKEN_STRING || tokenIsChar(t, '[') ||
					tokenIsChar(t, '/') || tokenIsChar(t, '!') ||
					!onyx_track_size(c, vector, ctx, &rows))
				ob_puts(&rows, "auto");
		}
		if (error == CSS_OK && t != NULL && tokenIsChar(t, '/')) {
			parserutils_vector_iterate(vector, ctx);
			if (!onyx_track_list(c, vector, ctx, &cols, 0, false))
				error = CSS_INVALID;
		}
	} else {
		/* <rows> / <columns> */
		if (!onyx_track_list(c, vector, ctx, &rows, '/', false))
			error = CSS_INVALID;
		t = parserutils_vector_iterate(vector, ctx);
		if (error == CSS_OK && (t == NULL || !tokenIsChar(t, '/')))
			error = CSS_INVALID;
		if (error == CSS_OK && !onyx_track_list(c, vector, ctx, &cols, 0, false))
			error = CSS_INVALID;
	}
	if (error == CSS_OK && !onyx_at_end(vector, ctx))
		error = CSS_INVALID;

	if (error == CSS_OK) {
		error = onyx_emit_text(c, result, props[0], &rows);
		if (error == CSS_OK)
			error = (cols.n > 0) ? onyx_emit_text(c, result, props[1], &cols) :
					css__stylesheet_style_appendOPV(result, props[1], 0,
						ONYX_TEXT_NONE);
		if (error == CSS_OK)
			error = (areas.n > 0) ? onyx_emit_text(c, result, props[2], &areas) :
					css__stylesheet_style_appendOPV(result, props[2], 0,
						ONYX_TEXT_NONE);
	}
	free(rows.p);
	free(cols.p);
	free(areas.p);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

/* grid: <grid-template> | <rows> / auto-flow dense? <auto-columns>? |
 *       auto-flow dense? <auto-rows>? / <columns> */
css_error css__parse_grid(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	static const opcode_t props[6] = { CSS_PROP_GRID_TEMPLATE_ROWS,
			CSS_PROP_GRID_TEMPLATE_COLUMNS, CSS_PROP_GRID_TEMPLATE_AREAS,
			CSS_PROP_GRID_AUTO_ROWS, CSS_PROP_GRID_AUTO_COLUMNS,
			CSS_PROP_GRID_AUTO_FLOW };
	int32_t orig_ctx = *ctx, k;
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	enum flag_value flag;
	css_error error = CSS_OK;
	bool auto_flow = false;
	int i;

	if (t == NULL)
		return CSS_INVALID;
	flag = get_css_flag_value(c, t);
	if (flag != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		for (i = 0; i < 6 && error == CSS_OK; i++)
			error = css_stylesheet_style_flag_value(result, flag, props[i]);
		return error;
	}
	/* auto-flow anywhere? */
	for (k = *ctx; (t = parserutils_vector_peek(vector, k)) != NULL; k++) {
		if (onyx_word(t, "auto-flow"))
			auto_flow = true;
	}
	if (!auto_flow) {
		error = css__parse_grid_template(c, vector, ctx, result);
		if (error == CSS_OK)
			error = css__stylesheet_style_appendOPV(result, CSS_PROP_GRID_AUTO_ROWS,
					0, ONYX_TEXT_NONE);
		if (error == CSS_OK)
			error = css__stylesheet_style_appendOPV(result,
					CSS_PROP_GRID_AUTO_COLUMNS, 0, ONYX_TEXT_NONE);
		if (error == CSS_OK)
			error = css__stylesheet_style_appendOPV(result, CSS_PROP_GRID_AUTO_FLOW,
					0, GRID_AUTO_FLOW_ROW);
		return error;
	}

	{
		onyx_buf a = { NULL, 0, 0, false }, b = { NULL, 0, 0, false };
		bool dense = false, flow_first;
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_peek(vector, *ctx);
		flow_first = onyx_word(t, "auto-flow") || onyx_word(t, "dense");
		if (flow_first) {
			/* auto-flow dense? <auto-rows>? / <columns> */
			while ((t = parserutils_vector_peek(vector, *ctx)) != NULL &&
					(onyx_word(t, "auto-flow") || onyx_word(t, "dense"))) {
				if (onyx_word(t, "dense"))
					dense = true;
				parserutils_vector_iterate(vector, ctx);
				consumeWhitespace(vector, ctx);
			}
			t = parserutils_vector_peek(vector, *ctx);
			if (t != NULL && !tokenIsChar(t, '/'))
				(void) onyx_track_list(c, vector, ctx, &a, '/', false);
			t = parserutils_vector_iterate(vector, ctx);
			if (t == NULL || !tokenIsChar(t, '/') ||
					!onyx_track_list(c, vector, ctx, &b, 0, false))
				error = CSS_INVALID;
			if (error == CSS_OK) {
				error = css__stylesheet_style_appendOPV(result, props[0], 0,
						ONYX_TEXT_NONE);
				if (error == CSS_OK)
					error = onyx_emit_text(c, result, props[1], &b);
				if (error == CSS_OK)
					error = a.n > 0 ? onyx_emit_text(c, result, props[3], &a) :
						css__stylesheet_style_appendOPV(result, props[3], 0,
							ONYX_TEXT_NONE);
				if (error == CSS_OK)
					error = css__stylesheet_style_appendOPV(result, props[4], 0,
							ONYX_TEXT_NONE);
				if (error == CSS_OK)
					error = css__stylesheet_style_appendOPV(result, props[5], 0,
							GRID_AUTO_FLOW_ROW |
							(dense ? GRID_AUTO_FLOW_DENSE : 0));
			}
		} else {
			/* <rows> / auto-flow dense? <auto-columns>? */
			if (!onyx_track_list(c, vector, ctx, &a, '/', false))
				error = CSS_INVALID;
			t = parserutils_vector_iterate(vector, ctx);
			if (error == CSS_OK && (t == NULL || !tokenIsChar(t, '/')))
				error = CSS_INVALID;
			while (error == CSS_OK) {
				consumeWhitespace(vector, ctx);
				t = parserutils_vector_peek(vector, *ctx);
				if (onyx_word(t, "auto-flow") || onyx_word(t, "dense")) {
					if (onyx_word(t, "dense"))
						dense = true;
					parserutils_vector_iterate(vector, ctx);
					continue;
				}
				break;
			}
			if (error == CSS_OK && !onyx_at_end(vector, ctx))
				(void) onyx_track_list(c, vector, ctx, &b, 0, false);
			if (error == CSS_OK) {
				error = onyx_emit_text(c, result, props[0], &a);
				if (error == CSS_OK)
					error = css__stylesheet_style_appendOPV(result, props[1],
							0, ONYX_TEXT_NONE);
				if (error == CSS_OK)
					error = css__stylesheet_style_appendOPV(result, props[3],
							0, ONYX_TEXT_NONE);
				if (error == CSS_OK)
					error = b.n > 0 ? onyx_emit_text(c, result, props[4], &b) :
						css__stylesheet_style_appendOPV(result, props[4], 0,
							ONYX_TEXT_NONE);
				if (error == CSS_OK)
					error = css__stylesheet_style_appendOPV(result, props[5], 0,
							GRID_AUTO_FLOW_COLUMN |
							(dense ? GRID_AUTO_FLOW_DENSE : 0));
			}
		}
		if (error == CSS_OK)
			error = css__stylesheet_style_appendOPV(result, props[2], 0,
					ONYX_TEXT_NONE);
		if (error == CSS_OK && !onyx_at_end(vector, ctx))
			error = CSS_INVALID;
		free(a.p);
		free(b.p);
	}
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

/* ---- text-decoration: <line> || <style> || <color> || <thickness> (the line kept) -------- */

css_error css__onyx_parse_text_decoration(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	int32_t orig_ctx = *ctx;
	parserutils_vector *lines = NULL;
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	int32_t lctx = 0;
	css_error error;
	bool any_line = false;

	if (t == NULL)
		return CSS_INVALID;
	if (get_css_flag_value(c, t) != FLAG_VALUE__NONE)
		return css__parse_text_decoration(c, vector, ctx, result);

	if (parserutils_vector_create(sizeof(css_token), 8, &lines) != PARSERUTILS_OK)
		return CSS_NOMEM;
	while (!onyx_at_end(vector, ctx)) {
		uint16_t value;
		uint32_t colour;
		onyx_buf dummy = { NULL, 0, 0, false };
		t = parserutils_vector_peek(vector, *ctx);
		if (onyx_word(t, "none") || onyx_word(t, "underline") || onyx_word(t, "overline") ||
				onyx_word(t, "line-through") || onyx_word(t, "blink")) {
			css_token tok = *t;
			if (lctx++ > 0) {
				css_token sp;
				memset(&sp, 0, sizeof(sp));
				sp.type = CSS_TOKEN_S;
				parserutils_vector_append(lines, &sp);
			}
			parserutils_vector_append(lines, &tok);
			parserutils_vector_iterate(vector, ctx);
			any_line = true;
		} else if (onyx_word(t, "solid") || onyx_word(t, "double") ||
				onyx_word(t, "dotted") || onyx_word(t, "dashed") ||
				onyx_word(t, "wavy") || onyx_word(t, "auto") ||
				onyx_word(t, "from-font")) {
			parserutils_vector_iterate(vector, ctx);	/* (not kept) */
		} else if (onyx_value(c, vector, ctx, ONYX_LEN, &dummy)) {
			free(dummy.p);					/* (thickness) */
		} else if (css__parse_colour_specifier(c, vector, ctx, &value, &colour) ==
				CSS_OK) {
			;						/* (colour) */
		} else {
			parserutils_vector_destroy(lines);
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
	}
	if (!any_line) {
		/* a style / colour alone: the line is none */
		error = css__stylesheet_style_appendOPV(result, CSS_PROP_TEXT_DECORATION, 0,
				TEXT_DECORATION_NONE);
	} else {
		lctx = 0;
		error = css__parse_text_decoration(c, lines, &lctx, result);
	}
	parserutils_vector_destroy(lines);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}


/* ---- mask (Onyx) ---------------------------------------------------------------------------- */

/*
 * mask-image, mask-size, mask-position and mask-repeat are kept as text (their first layer):
 *   mask-image     the image's resolved URL (none: none; a gradient or another image: none)
 *   mask-size      "auto" | "cover" | "contain" | "<len> <len>" (auto: none)
 *   mask-position  "<len|keyword> <len|keyword>" (0% 0%: none)
 *   mask-repeat    the keywords ("no-repeat", "repeat-x"...; repeat: none)
 * The painting reads them (content/handlers/html/redraw.c).
 */

/* A value of a layer (up to ',' or the end): keywords lowercased, lengths canonical, '/'
 * kept; false at a token it does not take. */
static bool onyx_mask_tokens(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, onyx_buf *out, bool slash)
{
	for (;;) {
		const css_token *t;
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_peek(vector, *ctx);
		if (t == NULL || tokenIsChar(t, '!'))
			return true;
		if (tokenIsChar(t, ',')) {
			/* (the first layer only: the rest skipped) */
			while ((t = parserutils_vector_peek(vector, *ctx)) != NULL &&
					!tokenIsChar(t, '!'))
				parserutils_vector_iterate(vector, ctx);
			return true;
		}
		if (out->n > 0)
			ob_puts(out, " ");
		if (t->type == CSS_TOKEN_IDENT) {
			size_t i, n = lwc_string_length(t->idata);
			const char *d = lwc_string_data(t->idata);
			for (i = 0; i < n; i++) {
				char ch = d[i];
				if (ch >= 'A' && ch <= 'Z')
					ch += 'a' - 'A';
				ob_putn(out, &ch, 1);
			}
			parserutils_vector_iterate(vector, ctx);
		} else if (slash && tokenIsChar(t, '/')) {
			ob_puts(out, "/");
			parserutils_vector_iterate(vector, ctx);
		} else if (!onyx_value(c, vector, ctx, ONYX_LEN, out)) {
			return false;
		}
	}
}

static css_error onyx_mask_text(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result, opcode_t op)
{
	int32_t orig_ctx = *ctx;
	onyx_buf b = { NULL, 0, 0, false };
	css_error error;

	if (onyx_text_start(c, vector, ctx, result, op, false, &error))
		return error;
	if (!onyx_mask_tokens(c, vector, ctx, &b, false) || b.n == 0) {
		free(b.p);
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	error = onyx_emit_text(c, result, op, &b);
	free(b.p);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

/* An image token: a URL appended to out (resolved); another image (a gradient): skipped,
 * *none set. False when it is not an image. */
static bool onyx_mask_image_token(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, onyx_buf *out, bool *none, css_error *error)
{
	const css_token *t = parserutils_vector_peek(vector, *ctx);

	*error = CSS_OK;
	if (t == NULL)
		return false;
	if (t->type == CSS_TOKEN_URI) {
		lwc_string *uri;
		*error = c->sheet->resolve(c->sheet->resolve_pw, c->sheet->url,
				t->idata, &uri);
		if (*error != CSS_OK)
			return true;
		ob_putn(out, lwc_string_data(uri), lwc_string_length(uri));
		lwc_string_unref(uri);
		parserutils_vector_iterate(vector, ctx);
		return true;
	}
	if (t->type == CSS_TOKEN_FUNCTION) {
		/* (a gradient, image-set(), element()...: not drawn) */
		int depth = 0;
		do {
			t = parserutils_vector_iterate(vector, ctx);
			if (t == NULL)
				break;
			if (t->type == CSS_TOKEN_FUNCTION)
				depth++;
			else if (tokenIsChar(t, '('))
				depth++;
			else if (tokenIsChar(t, ')'))
				depth--;
		} while (depth > 0);
		*none = true;
		return true;
	}
	return false;
}

css_error css__parse_mask_image(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	int32_t orig_ctx = *ctx;
	onyx_buf b = { NULL, 0, 0, false };
	bool none = false;
	css_error error;
	const css_token *t;

	if (onyx_text_start(c, vector, ctx, result, CSS_PROP_MASK_IMAGE, true, &error))
		return error;
	consumeWhitespace(vector, ctx);
	if (!onyx_mask_image_token(c, vector, ctx, &b, &none, &error) ||
			error != CSS_OK) {
		free(b.p);
		*ctx = orig_ctx;
		return error != CSS_OK ? error : CSS_INVALID;
	}
	/* (the first layer only) */
	while ((t = parserutils_vector_peek(vector, *ctx)) != NULL && !tokenIsChar(t, '!'))
		parserutils_vector_iterate(vector, ctx);
	if (none || b.n == 0)
		error = css__stylesheet_style_appendOPV(result, CSS_PROP_MASK_IMAGE, 0,
				ONYX_TEXT_NONE);
	else
		error = onyx_emit_text(c, result, CSS_PROP_MASK_IMAGE, &b);
	free(b.p);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

css_error css__parse_mask_size(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return onyx_mask_text(c, vector, ctx, result, CSS_PROP_MASK_SIZE);
}

css_error css__parse_mask_position(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return onyx_mask_text(c, vector, ctx, result, CSS_PROP_MASK_POSITION);
}

css_error css__parse_mask_repeat(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return onyx_mask_text(c, vector, ctx, result, CSS_PROP_MASK_REPEAT);
}

static bool onyx_word_in(const css_token *t, const char *const *words)
{
	for (; *words != NULL; words++)
		if (onyx_word(t, *words))
			return true;
	return false;
}

/* mask: <image> || <position> [ / <size> ]? || <repeat> || the rest (mode, origin, clip,
 * composite: skipped) -- the first layer's longhands */
css_error css__parse_mask(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	static const char *const repeats[] = { "repeat", "no-repeat", "repeat-x",
			"repeat-y", "space", "round", NULL };
	static const char *const others[] = { "alpha", "luminance", "match-source",
			"border-box", "padding-box", "content-box", "margin-box", "fill-box",
			"stroke-box", "view-box", "no-clip", "add", "subtract", "intersect",
			"exclude", NULL };
	int32_t orig_ctx = *ctx;
	onyx_buf img = { NULL, 0, 0, false }, pos = { NULL, 0, 0, false },
		size = { NULL, 0, 0, false }, rep = { NULL, 0, 0, false };
	bool none = false, in_size = false;
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	enum flag_value flag;
	css_error error = CSS_OK;

	if (t == NULL)
		return CSS_INVALID;
	flag = get_css_flag_value(c, t);
	if (flag != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		error = css_stylesheet_style_flag_value(result, flag, CSS_PROP_MASK_IMAGE);
		if (error == CSS_OK)
			error = css_stylesheet_style_flag_value(result, flag, CSS_PROP_MASK_SIZE);
		if (error == CSS_OK)
			error = css_stylesheet_style_flag_value(result, flag,
					CSS_PROP_MASK_POSITION);
		if (error == CSS_OK)
			error = css_stylesheet_style_flag_value(result, flag,
					CSS_PROP_MASK_REPEAT);
		return error;
	}
	for (;;) {
		bool image;
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_peek(vector, *ctx);
		if (t == NULL || tokenIsChar(t, '!'))
			break;
		if (tokenIsChar(t, ',')) {
			while ((t = parserutils_vector_peek(vector, *ctx)) != NULL &&
					!tokenIsChar(t, '!'))
				parserutils_vector_iterate(vector, ctx);
			break;
		}
		if (onyx_word(t, "none")) {
			none = true;
			parserutils_vector_iterate(vector, ctx);
			continue;
		}
		image = onyx_mask_image_token(c, vector, ctx, &img, &none, &error);
		if (error != CSS_OK)
			goto fail;
		if (image)
			continue;
		if (onyx_word_in(t, repeats)) {
			if (rep.n > 0)
				ob_puts(&rep, " ");
			ob_putn(&rep, lwc_string_data(t->idata), lwc_string_length(t->idata));
			parserutils_vector_iterate(vector, ctx);
			continue;
		}
		if (onyx_word_in(t, others)) {
			parserutils_vector_iterate(vector, ctx);
			continue;
		}
		if (tokenIsChar(t, '/')) {
			in_size = true;
			parserutils_vector_iterate(vector, ctx);
			continue;
		}
		{
			onyx_buf *o = in_size ? &size : &pos;
			if (o->n > 0)
				ob_puts(o, " ");
			if (t->type == CSS_TOKEN_IDENT) {
				ob_putn(o, lwc_string_data(t->idata),
						lwc_string_length(t->idata));
				parserutils_vector_iterate(vector, ctx);
			} else if (!onyx_value(c, vector, ctx, ONYX_LEN, o)) {
				error = CSS_INVALID;
				goto fail;
			}
		}
	}
	if (none || img.n == 0)
		error = css__stylesheet_style_appendOPV(result, CSS_PROP_MASK_IMAGE, 0,
				ONYX_TEXT_NONE);
	else
		error = onyx_emit_text(c, result, CSS_PROP_MASK_IMAGE, &img);
	if (error == CSS_OK)
		error = size.n > 0 ? onyx_emit_text(c, result, CSS_PROP_MASK_SIZE, &size) :
				css__stylesheet_style_appendOPV(result, CSS_PROP_MASK_SIZE, 0,
				ONYX_TEXT_NONE);
	if (error == CSS_OK)
		error = pos.n > 0 ? onyx_emit_text(c, result, CSS_PROP_MASK_POSITION, &pos) :
				css__stylesheet_style_appendOPV(result, CSS_PROP_MASK_POSITION,
				0, ONYX_TEXT_NONE);
	if (error == CSS_OK)
		error = rep.n > 0 ? onyx_emit_text(c, result, CSS_PROP_MASK_REPEAT, &rep) :
				css__stylesheet_style_appendOPV(result, CSS_PROP_MASK_REPEAT, 0,
				ONYX_TEXT_NONE);
fail:
	free(img.p);
	free(pos.p);
	free(size.p);
	free(rep.p);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

/* ---- the compositing properties ------------------------------------------------------------
 * filter, backdrop-filter, transform-origin and mix-blend-mode are kept as text too, which the
 * painting reads (NetSurf: content/handlers/html/onyx_fx.c):
 *   filter / backdrop-filter  the functions separated by spaces: blur(<len>),
 *                  brightness(n) contrast(n) grayscale(n) invert(n) opacity(n) saturate(n)
 *                  sepia(n) (a percentage as a number, 1 when omitted), hue-rotate(<n>deg),
 *                  drop-shadow(<x>,<y>,<blur>,<#aarrggbb> | currentcolor); a url() is not
 *                  kept (the declaration is then checked by its grammar only)
 *   transform-origin  "<x> <y>": lengths or percentages, the keywords as percentages (the z
 *                  length dropped: 3D is flattened)
 *   mix-blend-mode    its keyword (normal: none)
 */

/* One filter function (ctx at its token): its canonical form appended. */
static bool onyx_filter_fn(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, onyx_buf *out)
{
	static const char *const amounts[] = { "brightness", "contrast", "grayscale",
			"invert", "opacity", "saturate", "sepia", NULL };
	const css_token *t = parserutils_vector_iterate(vector, ctx);
	onyx_buf v[1];
	int n, i;

	memset(v, 0, sizeof(v));
	if (onyx_fn(t, "blur")) {
		n = onyx_args(c, vector, ctx, ONYX_LEN, 0, 1, v, true);
		if (n < 0 || (n == 1 && (v[0].p[0] == '-' || strchr(v[0].p, '%') != NULL))) {
			onyx_free_vals(v, 1);
			return false;
		}
		ob_puts(out, "blur(");
		ob_puts(out, n == 1 ? v[0].p : "0px");
		ob_puts(out, ")");
		onyx_free_vals(v, 1);
		return true;
	}
	if (onyx_fn(t, "hue-rotate")) {
		n = onyx_args(c, vector, ctx, ONYX_ANGLE, 0, 1, v, true);
		if (n < 0)
			return false;
		ob_puts(out, "hue-rotate(");
		ob_puts(out, n == 1 ? v[0].p : "0deg");
		ob_puts(out, ")");
		onyx_free_vals(v, 1);
		return true;
	}
	for (i = 0; amounts[i] != NULL; i++) {
		float a;
		if (!onyx_fn(t, amounts[i]))
			continue;
		n = onyx_args(c, vector, ctx, ONYX_NUM | ONYX_PCTNUM, 0, 1, v, true);
		if (n < 0)
			return false;
		a = n == 1 ? strtof(v[0].p, NULL) : 1;
		onyx_free_vals(v, 1);
		if (a < 0)
			return false;
		/* (grayscale, invert, opacity and sepia are clamped to 1) */
		if (a > 1 && (i == 2 || i == 3 || i == 4 || i == 6))
			a = 1;
		ob_puts(out, amounts[i]);
		ob_puts(out, "(");
		ob_num(out, a);
		ob_puts(out, ")");
		return true;
	}
	if (onyx_fn(t, "drop-shadow")) {
		onyx_buf len[3];
		int nl = 0;
		bool have_colour = false, current = false;
		uint32_t colour = 0xff000000;
		char hex[16];

		memset(len, 0, sizeof(len));
		for (;;) {
			const css_token *p;
			consumeWhitespace(vector, ctx);
			p = parserutils_vector_peek(vector, *ctx);
			if (p == NULL) {
				onyx_free_vals(len, 3);
				return false;
			}
			if (tokenIsChar(p, ')')) {
				parserutils_vector_iterate(vector, ctx);
				break;
			}
			if (nl < 3 && onyx_value(c, vector, ctx, ONYX_LEN, &len[nl])) {
				if (strchr(len[nl].p, '%') != NULL ||
				    (nl == 2 && len[nl].p[0] == '-')) {
					onyx_free_vals(len, 3);
					return false;
				}
				nl++;
				continue;
			}
			if (!have_colour) {
				int32_t save = *ctx;
				uint16_t value = 0;
				if (css__parse_colour_specifier(c, vector, ctx, &value,
						&colour) == CSS_OK) {
					have_colour = true;
					current = (value == COLOR_CURRENT_COLOR);
					continue;
				}
				*ctx = save;
			}
			onyx_free_vals(len, 3);
			return false;
		}
		if (nl < 2) {
			onyx_free_vals(len, 3);
			return false;
		}
		ob_puts(out, "drop-shadow(");
		ob_puts(out, len[0].p);
		ob_puts(out, ",");
		ob_puts(out, len[1].p);
		ob_puts(out, ",");
		ob_puts(out, nl > 2 ? len[2].p : "0px");
		ob_puts(out, ",");
		if (!have_colour || current) {
			ob_puts(out, "currentcolor");
		} else {
			snprintf(hex, sizeof(hex), "#%08x", (unsigned) colour);
			ob_puts(out, hex);
		}
		ob_puts(out, ")");
		onyx_free_vals(len, 3);
		return true;
	}
	return false;
}

static css_error onyx_filter_prop(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result, opcode_t op)
{
	int32_t orig_ctx = *ctx;
	onyx_buf b = { NULL, 0, 0, false };
	css_error error;

	if (onyx_text_start(c, vector, ctx, result, op, true, &error))
		return error;
	while (!onyx_at_end(vector, ctx)) {
		if (b.n > 0)
			ob_puts(&b, " ");
		if (!onyx_filter_fn(c, vector, ctx, &b)) {
			free(b.p);
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
	}
	if (b.n == 0) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	error = onyx_emit_text(c, result, op, &b);
	free(b.p);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}

css_error css__parse_filter(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return onyx_filter_prop(c, vector, ctx, result, CSS_PROP_FILTER);
}

css_error css__parse_backdrop_filter(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return onyx_filter_prop(c, vector, ctx, result, CSS_PROP_BACKDROP_FILTER);
}

/* transform-origin: [ left | center | right | top | bottom | <length-percentage> ]{1,2}
 * <length>? (the keywords in either order when two) */
css_error css__parse_transform_origin(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	int32_t orig_ctx = *ctx;
	onyx_buf v[3], b = { NULL, 0, 0, false };
	/* each value: 'x' (left / right), 'y' (top / bottom), 'c' (center), 'l' (a length) */
	char kind[3];
	int n = 0;
	css_error error;

	if (onyx_text_start(c, vector, ctx, result, CSS_PROP_TRANSFORM_ORIGIN, false, &error))
		return error;
	memset(v, 0, sizeof(v));
	while (!onyx_at_end(vector, ctx)) {
		const css_token *t = parserutils_vector_peek(vector, *ctx);
		if (n == 3)
			goto invalid;
		if (onyx_word(t, "left") || onyx_word(t, "right")) {
			ob_puts(&v[n], onyx_word(t, "left") ? "0%" : "100%");
			kind[n] = 'x';
		} else if (onyx_word(t, "top") || onyx_word(t, "bottom")) {
			ob_puts(&v[n], onyx_word(t, "top") ? "0%" : "100%");
			kind[n] = 'y';
		} else if (onyx_word(t, "center")) {
			ob_puts(&v[n], "50%");
			kind[n] = 'c';
		} else if (onyx_value(c, vector, ctx, ONYX_LEN, &v[n])) {
			kind[n++] = 'l';
			continue;
		} else {
			goto invalid;
		}
		parserutils_vector_iterate(vector, ctx);
		n++;
	}
	if (n == 0 || (n == 3 && (kind[2] != 'l' || strchr(v[2].p, '%') != NULL)))
		goto invalid;
	if (n == 1) {
		if (kind[0] == 'y') {
			ob_puts(&b, "50% ");
			ob_puts(&b, v[0].p);
		} else {
			ob_puts(&b, v[0].p);
			ob_puts(&b, " 50%");
		}
	} else {
		/* vertical first: "top left", "bottom 10px"... */
		bool swap = kind[0] == 'y' || kind[1] == 'x';
		if ((kind[0] == 'x' && kind[1] == 'x') || (kind[0] == 'y' && kind[1] == 'y') ||
		    (swap && (kind[0] == 'l' || kind[1] == 'l')))
			goto invalid;
		ob_puts(&b, v[swap ? 1 : 0].p);
		ob_puts(&b, " ");
		ob_puts(&b, v[swap ? 0 : 1].p);
	}
	onyx_free_vals(v, 3);
	error = onyx_emit_text(c, result, CSS_PROP_TRANSFORM_ORIGIN, &b);
	free(b.p);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
invalid:
	onyx_free_vals(v, 3);
	free(b.p);
	*ctx = orig_ctx;
	return CSS_INVALID;
}

/* mix-blend-mode: normal | multiply | screen | ... (normal kept as none) */
css_error css__parse_mix_blend_mode(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	static const char *const modes[] = { "multiply", "screen", "overlay", "darken",
			"lighten", "color-dodge", "color-burn", "hard-light", "soft-light",
			"difference", "exclusion", "hue", "saturation", "color", "luminosity",
			"plus-darker", "plus-lighter", NULL };
	int32_t orig_ctx = *ctx;
	onyx_buf b = { NULL, 0, 0, false };
	const css_token *t;
	css_error error;
	int i;

	if (onyx_text_start(c, vector, ctx, result, CSS_PROP_MIX_BLEND_MODE, false, &error))
		return error;
	t = parserutils_vector_iterate(vector, ctx);
	if (onyx_word(t, "normal") && onyx_at_end(vector, ctx))
		return css__stylesheet_style_appendOPV(result, CSS_PROP_MIX_BLEND_MODE, 0,
				ONYX_TEXT_NONE);
	for (i = 0; modes[i] != NULL; i++)
		if (onyx_word(t, modes[i]))
			break;
	if (modes[i] == NULL || !onyx_at_end(vector, ctx)) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	ob_puts(&b, modes[i]);
	error = onyx_emit_text(c, result, CSS_PROP_MIX_BLEND_MODE, &b);
	free(b.p);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
}


/* ---- transitions and animations (Onyx) ------------------------------------------------------ */

/*
 * The transition-* and animation-* longhands are kept as text, the list's items separated by
 * commas (NetSurf's html/onyx_anim.c reads them):
 *   times            seconds: "0.3s,1s" (ms converted; a delay may be negative)
 *   timing functions "ease" | "linear" | "ease-in" | "ease-out" | "ease-in-out" | "step-start"
 *                    | "step-end" | "cubic-bezier(x1,y1,x2,y2)" | "steps(n,jump-end)" (the
 *                    position spelled jump-*) | "linear(v p% p%,v...)" (its stops)
 *   properties       the names, lowercase ("all", "none", "opacity"...)
 *   names            the @keyframes' names as written ("none": no animation)
 *   counts           a number or "infinite"
 *   the keywords     directions, fill modes, play states
 * The shorthands transition and animation set every longhand, one item each per layer.
 */

enum { OA_TIME, OA_NNTIME, OA_EASING, OA_PROPERTY, OA_NAME, OA_COUNT, OA_DIRECTION, OA_FILL,
	OA_PLAY };

static bool oa_word_in(const css_token *t, const char *const *words)
{
	for (; *words != NULL; words++)
		if (onyx_word(t, *words))
			return true;
	return false;
}

static const char *const oa_easings[] = { "ease", "linear", "ease-in", "ease-out",
		"ease-in-out", "step-start", "step-end", NULL };
static const char *const oa_directions[] = { "normal", "reverse", "alternate",
		"alternate-reverse", NULL };
static const char *const oa_fills[] = { "none", "forwards", "backwards", "both", NULL };
static const char *const oa_plays[] = { "running", "paused", NULL };
static const char *const oa_wide[] = { "initial", "inherit", "unset", "revert",
		"revert-layer", "default", NULL };

static void oa_lower(onyx_buf *out, const css_token *t)
{
	size_t i, n = lwc_string_length(t->idata);
	const char *d = lwc_string_data(t->idata);
	for (i = 0; i < n; i++) {
		char ch = d[i];
		if (ch >= 'A' && ch <= 'Z')
			ch += 'a' - 'A';
		ob_putn(out, &ch, 1);
	}
}

/* a number token (a percentage's too): its value, false if not one */
static bool oa_number(const css_token *t, bool integer, float *v)
{
	size_t consumed;
	if (t == NULL || (t->type != CSS_TOKEN_NUMBER && t->type != CSS_TOKEN_PERCENTAGE))
		return false;
	*v = FIXTOFLT(css__number_from_lwc_string(t->idata, integer, &consumed));
	return consumed == lwc_string_length(t->idata);
}

/* a <time>: seconds appended */
static bool oa_time(const parserutils_vector *vector, int32_t *ctx, bool nonneg, onyx_buf *out)
{
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	size_t consumed, ul;
	const char *d;
	float v;

	if (t == NULL)
		return false;
	if (t->type == CSS_TOKEN_NUMBER) {
		/* (a unitless 0, as the engines take it) */
		if (!oa_number(t, false, &v) || v != 0)
			return false;
		parserutils_vector_iterate(vector, ctx);
		ob_puts(out, "0s");
		return true;
	}
	if (t->type != CSS_TOKEN_DIMENSION)
		return false;
	d = lwc_string_data(t->idata);
	v = FIXTOFLT(css__number_from_lwc_string(t->idata, false, &consumed));
	ul = lwc_string_length(t->idata) - consumed;
	if (ul == 1 && (d[consumed] == 's' || d[consumed] == 'S'))
		;
	else if (ul == 2 && strncasecmp(d + consumed, "ms", 2) == 0)
		v /= 1000;
	else
		return false;
	if (nonneg && v < 0)
		return false;
	parserutils_vector_iterate(vector, ctx);
	ob_num(out, v);
	ob_puts(out, "s");
	return true;
}

/* a function's arguments: numbers (and percentages for linear()), commas between (a linear()
 * stop's parts separated by spaces); up to the ')' */
static bool oa_args(const parserutils_vector *vector, int32_t *ctx, onyx_buf *out, int min,
		int max, bool linear)
{
	int n = 0;
	bool part = false;

	for (;;) {
		const css_token *t;
		float v;
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_iterate(vector, ctx);
		if (t == NULL)
			return false;
		if (tokenIsChar(t, ')'))
			break;
		if (tokenIsChar(t, ',')) {
			if (!part)
				return false;
			ob_puts(out, ",");
			part = false;
			continue;
		}
		if (!oa_number(t, false, &v))
			return false;
		if (t->type == CSS_TOKEN_PERCENTAGE && !linear)
			return false;
		if (part) {
			if (!linear)
				return false;
			ob_puts(out, " ");
		} else {
			n++;
		}
		ob_num(out, v);
		if (t->type == CSS_TOKEN_PERCENTAGE)
			ob_puts(out, "%");
		part = true;
	}
	return part && n >= min && n <= max;
}

/* an <easing-function> appended */
static bool oa_easing(const parserutils_vector *vector, int32_t *ctx, onyx_buf *out)
{
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	int32_t save = *ctx;
	size_t mark = out->n;

	if (t == NULL)
		return false;
	if (oa_word_in(t, oa_easings)) {
		parserutils_vector_iterate(vector, ctx);
		oa_lower(out, t);
		return true;
	}
	if (onyx_fn(t, "cubic-bezier")) {
		parserutils_vector_iterate(vector, ctx);
		ob_puts(out, "cubic-bezier(");
		if (!oa_args(vector, ctx, out, 4, 4, false))
			goto fail;
		ob_puts(out, ")");
		return true;
	}
	if (onyx_fn(t, "linear")) {
		parserutils_vector_iterate(vector, ctx);
		ob_puts(out, "linear(");
		if (!oa_args(vector, ctx, out, 1, 256, true))
			goto fail;
		ob_puts(out, ")");
		return true;
	}
	if (onyx_fn(t, "steps")) {
		static const char *const pos[] = { "jump-start", "jump-end", "jump-none",
				"jump-both", "start", "end", NULL };
		float v;
		parserutils_vector_iterate(vector, ctx);
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_iterate(vector, ctx);
		if (!oa_number(t, true, &v) || t->type != CSS_TOKEN_NUMBER || v < 1)
			goto fail;
		ob_puts(out, "steps(");
		ob_num(out, v);
		ob_puts(out, ",");
		consumeWhitespace(vector, ctx);
		t = parserutils_vector_iterate(vector, ctx);
		if (t != NULL && tokenIsChar(t, ',')) {
			consumeWhitespace(vector, ctx);
			t = parserutils_vector_iterate(vector, ctx);
			if (!oa_word_in(t, pos))
				goto fail;
			if (onyx_word(t, "start"))
				ob_puts(out, "jump-start");
			else if (onyx_word(t, "end"))
				ob_puts(out, "jump-end");
			else
				oa_lower(out, t);
			consumeWhitespace(vector, ctx);
			t = parserutils_vector_iterate(vector, ctx);
		} else {
			ob_puts(out, "jump-end");
		}
		if (t == NULL || !tokenIsChar(t, ')'))
			goto fail;
		ob_puts(out, ")");
		return true;
	}
	return false;
fail:
	*ctx = save;
	if (!out->oom && out->p != NULL) {
		out->n = mark;
		out->p[mark] = '\0';
	}
	return false;
}

/* one item of a longhand's list appended */
static bool oa_item(const parserutils_vector *vector, int32_t *ctx, int kind, onyx_buf *out)
{
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	float v;

	if (t == NULL)
		return false;
	switch (kind) {
	case OA_TIME:
	case OA_NNTIME:
		if (kind == OA_NNTIME && onyx_word(t, "auto")) {	/* (animation-duration) */
			parserutils_vector_iterate(vector, ctx);
			ob_puts(out, "0s");
			return true;
		}
		return oa_time(vector, ctx, kind == OA_NNTIME, out);
	case OA_EASING:
		return oa_easing(vector, ctx, out);
	case OA_PROPERTY:
		if (t->type != CSS_TOKEN_IDENT || oa_word_in(t, oa_wide))
			return false;
		parserutils_vector_iterate(vector, ctx);
		oa_lower(out, t);
		return true;
	case OA_NAME:
		if ((t->type != CSS_TOKEN_IDENT && t->type != CSS_TOKEN_STRING) ||
				(t->type == CSS_TOKEN_IDENT && oa_word_in(t, oa_wide)))
			return false;
		parserutils_vector_iterate(vector, ctx);
		if (t->type == CSS_TOKEN_IDENT && onyx_word(t, "none"))
			ob_puts(out, "none");
		else
			ob_putn(out, lwc_string_data(t->idata), lwc_string_length(t->idata));
		return true;
	case OA_COUNT:
		if (onyx_word(t, "infinite")) {
			parserutils_vector_iterate(vector, ctx);
			ob_puts(out, "infinite");
			return true;
		}
		if (t->type != CSS_TOKEN_NUMBER || !oa_number(t, false, &v) || v < 0)
			return false;
		parserutils_vector_iterate(vector, ctx);
		ob_num(out, v);
		return true;
	case OA_DIRECTION:
	case OA_FILL:
	case OA_PLAY:
		if (!oa_word_in(t, kind == OA_DIRECTION ? oa_directions :
				kind == OA_FILL ? oa_fills : oa_plays))
			return false;
		parserutils_vector_iterate(vector, ctx);
		oa_lower(out, t);
		return true;
	default:
		return false;
	}
}

/* a longhand: its items, comma separated */
static css_error oa_longhand(css_language *c, const parserutils_vector *vector, int32_t *ctx,
		css_style *result, opcode_t op, int kind)
{
	int32_t orig_ctx = *ctx;
	onyx_buf b = { NULL, 0, 0, false };
	css_error error;

	if (onyx_text_start(c, vector, ctx, result, op, false, &error))
		return error;
	for (;;) {
		const css_token *t;
		consumeWhitespace(vector, ctx);
		if (!oa_item(vector, ctx, kind, &b))
			goto invalid;
		if (onyx_at_end(vector, ctx))
			break;
		t = parserutils_vector_iterate(vector, ctx);
		if (t == NULL || !tokenIsChar(t, ','))
			goto invalid;
		ob_puts(&b, ",");
	}
	error = onyx_emit_text(c, result, op, &b);
	free(b.p);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
invalid:
	free(b.p);
	*ctx = orig_ctx;
	return CSS_INVALID;
}

css_error css__parse_transition_property(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return oa_longhand(c, vector, ctx, result, CSS_PROP_TRANSITION_PROPERTY, OA_PROPERTY);
}

css_error css__parse_transition_duration(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return oa_longhand(c, vector, ctx, result, CSS_PROP_TRANSITION_DURATION, OA_NNTIME);
}

css_error css__parse_transition_timing_function(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	return oa_longhand(c, vector, ctx, result, CSS_PROP_TRANSITION_TIMING_FUNCTION,
			OA_EASING);
}

css_error css__parse_transition_delay(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return oa_longhand(c, vector, ctx, result, CSS_PROP_TRANSITION_DELAY, OA_TIME);
}

css_error css__parse_animation_name(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return oa_longhand(c, vector, ctx, result, CSS_PROP_ANIMATION_NAME, OA_NAME);
}

css_error css__parse_animation_duration(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return oa_longhand(c, vector, ctx, result, CSS_PROP_ANIMATION_DURATION, OA_NNTIME);
}

css_error css__parse_animation_timing_function(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	return oa_longhand(c, vector, ctx, result, CSS_PROP_ANIMATION_TIMING_FUNCTION,
			OA_EASING);
}

css_error css__parse_animation_delay(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return oa_longhand(c, vector, ctx, result, CSS_PROP_ANIMATION_DELAY, OA_TIME);
}

css_error css__parse_animation_iteration_count(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	return oa_longhand(c, vector, ctx, result, CSS_PROP_ANIMATION_ITERATION_COUNT,
			OA_COUNT);
}

css_error css__parse_animation_direction(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return oa_longhand(c, vector, ctx, result, CSS_PROP_ANIMATION_DIRECTION, OA_DIRECTION);
}

css_error css__parse_animation_fill_mode(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return oa_longhand(c, vector, ctx, result, CSS_PROP_ANIMATION_FILL_MODE, OA_FILL);
}

css_error css__parse_animation_play_state(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	return oa_longhand(c, vector, ctx, result, CSS_PROP_ANIMATION_PLAY_STATE, OA_PLAY);
}

/* a shorthand's CSS-wide keyword: set on each longhand */
static bool oa_flags(css_language *c, const parserutils_vector *vector, int32_t *ctx,
		css_style *result, const opcode_t *ops, int n, css_error *error)
{
	const css_token *t = parserutils_vector_peek(vector, *ctx);
	enum flag_value flag;
	int i;

	if (t == NULL) {
		*error = CSS_INVALID;
		return true;
	}
	flag = get_css_flag_value(c, t);
	if (flag == FLAG_VALUE__NONE)
		return false;
	parserutils_vector_iterate(vector, ctx);
	*error = CSS_OK;
	for (i = 0; i < n && *error == CSS_OK; i++)
		*error = css_stylesheet_style_flag_value(result, flag, ops[i]);
	return true;
}

/* transition: [ <property> || <time> || <easing> || <time> || <behavior> ]# */
css_error css__parse_transition(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	static const opcode_t ops[4] = { CSS_PROP_TRANSITION_PROPERTY,
			CSS_PROP_TRANSITION_DURATION, CSS_PROP_TRANSITION_TIMING_FUNCTION,
			CSS_PROP_TRANSITION_DELAY };
	static const char *const behaviors[] = { "normal", "allow-discrete", NULL };
	int32_t orig_ctx = *ctx;
	onyx_buf b[4];
	css_error error = CSS_OK;
	int i;

	if (oa_flags(c, vector, ctx, result, ops, 4, &error))
		return error;
	memset(b, 0, sizeof(b));
	for (;;) {
		onyx_buf item[4];
		int ntimes = 0;
		bool prop = false, easing = false, behavior = false;
		memset(item, 0, sizeof(item));
		for (;;) {
			const css_token *t;
			consumeWhitespace(vector, ctx);
			t = parserutils_vector_peek(vector, *ctx);
			if (t == NULL || tokenIsChar(t, '!') || tokenIsChar(t, ','))
				break;
			if (ntimes < 2 && oa_time(vector, ctx, ntimes == 0,
					&item[ntimes == 0 ? 1 : 3])) {
				ntimes++;
			} else if (!easing && oa_easing(vector, ctx, &item[2])) {
				easing = true;
			} else if (!behavior && oa_word_in(t, behaviors)) {
				parserutils_vector_iterate(vector, ctx);
				behavior = true;
			} else if (!prop && oa_item(vector, ctx, OA_PROPERTY, &item[0])) {
				prop = true;
			} else {
				for (i = 0; i < 4; i++)
					free(item[i].p);
				goto invalid;
			}
		}
		if (!prop) ob_puts(&item[0], "all");
		if (ntimes < 1) ob_puts(&item[1], "0s");
		if (!easing) ob_puts(&item[2], "ease");
		if (ntimes < 2) ob_puts(&item[3], "0s");
		for (i = 0; i < 4; i++) {
			if (b[i].n > 0)
				ob_puts(&b[i], ",");
			ob_puts(&b[i], item[i].p != NULL ? item[i].p : "");
			free(item[i].p);
		}
		if (onyx_at_end(vector, ctx))
			break;
		parserutils_vector_iterate(vector, ctx);	/* (the ',') */
	}
	for (i = 0; i < 4 && error == CSS_OK; i++)
		error = onyx_emit_text(c, result, ops[i], &b[i]);
	for (i = 0; i < 4; i++)
		free(b[i].p);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
invalid:
	for (i = 0; i < 4; i++)
		free(b[i].p);
	*ctx = orig_ctx;
	return CSS_INVALID;
}

/* animation: [ <time> || <easing> || <time> || <count> || <direction> || <fill-mode> ||
 * <play-state> || [ none | <name> ] ]# -- a keyword is taken by the first slot it fits,
 * "none" by the name first */
css_error css__parse_animation(css_language *c, const parserutils_vector *vector,
		int32_t *ctx, css_style *result)
{
	enum { NAME, DUR, EASE, DELAY, COUNT, DIR, FILL, PLAY, N };
	static const opcode_t ops[N] = { CSS_PROP_ANIMATION_NAME, CSS_PROP_ANIMATION_DURATION,
			CSS_PROP_ANIMATION_TIMING_FUNCTION, CSS_PROP_ANIMATION_DELAY,
			CSS_PROP_ANIMATION_ITERATION_COUNT, CSS_PROP_ANIMATION_DIRECTION,
			CSS_PROP_ANIMATION_FILL_MODE, CSS_PROP_ANIMATION_PLAY_STATE };
	static const char *const defaults[N] = { "none", "0s", "ease", "0s", "1", "normal",
			"none", "running" };
	int32_t orig_ctx = *ctx;
	onyx_buf b[N];
	css_error error = CSS_OK;
	int i;

	if (oa_flags(c, vector, ctx, result, ops, N, &error))
		return error;
	memset(b, 0, sizeof(b));
	for (;;) {
		onyx_buf item[N];
		bool set[N];
		int ntimes = 0;
		memset(item, 0, sizeof(item));
		memset(set, 0, sizeof(set));
		for (;;) {
			const css_token *t;
			bool none;
			consumeWhitespace(vector, ctx);
			t = parserutils_vector_peek(vector, *ctx);
			if (t == NULL || tokenIsChar(t, '!') || tokenIsChar(t, ','))
				break;
			none = onyx_word(t, "none");
			if (ntimes < 2 && oa_time(vector, ctx, ntimes == 0,
					&item[ntimes == 0 ? DUR : DELAY])) {
				set[ntimes == 0 ? DUR : DELAY] = true;
				ntimes++;
			} else if (!set[EASE] && oa_easing(vector, ctx, &item[EASE])) {
				set[EASE] = true;
			} else if (!set[COUNT] && oa_item(vector, ctx, OA_COUNT, &item[COUNT])) {
				set[COUNT] = true;
			} else if (!set[DIR] && oa_item(vector, ctx, OA_DIRECTION, &item[DIR])) {
				set[DIR] = true;
			} else if (!set[FILL] && !(none && !set[NAME]) &&
					oa_item(vector, ctx, OA_FILL, &item[FILL])) {
				set[FILL] = true;
			} else if (!set[PLAY] && oa_item(vector, ctx, OA_PLAY, &item[PLAY])) {
				set[PLAY] = true;
			} else if (!set[NAME] && oa_item(vector, ctx, OA_NAME, &item[NAME])) {
				set[NAME] = true;
			} else {
				for (i = 0; i < N; i++)
					free(item[i].p);
				goto invalid;
			}
		}
		for (i = 0; i < N; i++) {
			if (b[i].n > 0)
				ob_puts(&b[i], ",");
			ob_puts(&b[i], set[i] && item[i].p != NULL ? item[i].p : defaults[i]);
			free(item[i].p);
		}
		if (onyx_at_end(vector, ctx))
			break;
		parserutils_vector_iterate(vector, ctx);	/* (the ',') */
	}
	for (i = 0; i < N && error == CSS_OK; i++)
		error = onyx_emit_text(c, result, ops[i], &b[i]);
	for (i = 0; i < N; i++)
		free(b[i].p);
	if (error != CSS_OK)
		*ctx = orig_ctx;
	return error;
invalid:
	for (i = 0; i < N; i++)
		free(b[i].p);
	*ctx = orig_ctx;
	return CSS_INVALID;
}
