/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2011 Things Made Out Of Other Things Ltd.
 * Written by James Montgomerie <jamie@th.ingsmadeoutofotherthin.gs>
 */

#include "parse/font_face.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "parse/propstrings.h"
#include "parse/properties/utils.h"
#include "select/font_face.h"

static bool font_rule_font_family_reserved(css_language *c,
		const css_token *ident)
{
	bool match;

	return (lwc_string_caseless_isequal(ident->idata, c->strings[SERIF],
			&match) == lwc_error_ok && match) ||
		(lwc_string_caseless_isequal(ident->idata,
			c->strings[SANS_SERIF], &match) == lwc_error_ok &&
			match) ||
		(lwc_string_caseless_isequal(ident->idata, c->strings[CURSIVE],
			&match) == lwc_error_ok && match) ||
		(lwc_string_caseless_isequal(ident->idata, c->strings[FANTASY],
			&match) == lwc_error_ok && match) ||
		(lwc_string_caseless_isequal(ident->idata,
			c->strings[MONOSPACE], &match) == lwc_error_ok &&
			match) ||
		(lwc_string_caseless_isequal(ident->idata, c->strings[INHERIT],
			&match) == lwc_error_ok && match) ||
		(lwc_string_caseless_isequal(ident->idata, c->strings[INITIAL],
			&match) == lwc_error_ok && match) ||
		(lwc_string_caseless_isequal(ident->idata, c->strings[DEFAULT],
			&match) == lwc_error_ok && match);
}

static css_error font_face_parse_font_family(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_font_face *font_face)
{
	css_error error;
	lwc_string *string;

	error = css__ident_list_or_string_to_string(c, vector, ctx,
				font_rule_font_family_reserved, &string);
	if (error != CSS_OK)
		return error;

	css__font_face_set_font_family(font_face, string);

	lwc_string_unref(string);

	return CSS_OK;
}

static css_error font_face_src_parse_format(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_font_face_format *format)
{
	bool match;
	const css_token *token;

	*format = CSS_FONT_FACE_FORMAT_UNSPECIFIED;

	/* 'format(' STRING [ ',' STRING ]* ')'
	 *
	 * 'format(' already consumed
	 */

	do {
		consumeWhitespace(vector, ctx);

		token = parserutils_vector_iterate(vector, ctx);
		/* (Onyx: CSS Fonts 4's keywords too: format(woff2)) */
		if (token == NULL || (token->type != CSS_TOKEN_STRING &&
				token->type != CSS_TOKEN_IDENT))
			return CSS_INVALID;

		if (lwc_string_caseless_isequal(token->idata,
				c->strings[WOFF2], &match) == lwc_error_ok &&
				match) {
			*format |= CSS_FONT_FACE_FORMAT_WOFF2;	/* Onyx */
		} else if (lwc_string_isequal(token->idata,
				c->strings[WOFF], &match) == lwc_error_ok &&
				match) {
		    	*format |= CSS_FONT_FACE_FORMAT_WOFF;
		} else if ((lwc_string_isequal(token->idata,
				c->strings[TRUETYPE], &match) == lwc_error_ok &&
				match) ||
			(lwc_string_isequal(token->idata,
				c->strings[OPENTYPE], &match) == lwc_error_ok &&
				match)) {
			*format |= CSS_FONT_FACE_FORMAT_OPENTYPE;
		} else if (lwc_string_isequal(token->idata,
				c->strings[EMBEDDED_OPENTYPE],
				&match) == lwc_error_ok && match) {
			*format |= CSS_FONT_FACE_FORMAT_EMBEDDED_OPENTYPE;
		} else if (lwc_string_isequal(token->idata,
				c->strings[SVG], &match) == lwc_error_ok &&
				match) {
			*format |= CSS_FONT_FACE_FORMAT_SVG;
		} else {
			/* The spec gives a list of possible strings, which
			 * hints that unknown strings should be parse errors,
			 * but it also talks about "unknown font formats",
			 * so we treat any string we don't know not as a parse
			 * error, but as indicating an "unknown font format".
			 */
			*format |= CSS_FONT_FACE_FORMAT_UNKNOWN;
		}

		consumeWhitespace(vector, ctx);
		token = parserutils_vector_iterate(vector, ctx);
	} while (token != NULL && tokenIsChar(token, ','));

	if (token == NULL || tokenIsChar(token, ')') == false)
		return CSS_INVALID;

	return CSS_OK;
}

static css_error font_face_src_parse_spec_or_name(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		lwc_string **location,
		css_font_face_location_type *location_type,
		css_font_face_format *format)
{
	const css_token *token;
	css_error error;
	bool match;

 	/* spec-or-name    ::= font-face-spec | font-face-name
 	 * font-face-spec  ::= URI [ 'format(' STRING [ ',' STRING ]* ')' ]?
 	 * font-face-name  ::= 'local(' ident-list-or-string ')'
 	 * ident-list-or-string ::= IDENT IDENT* | STRING
 	 */

	consumeWhitespace(vector, ctx);

	token = parserutils_vector_iterate(vector, ctx);
	if (token == NULL)
		return CSS_INVALID;

	if (token->type == CSS_TOKEN_URI) {
		error = c->sheet->resolve(c->sheet->resolve_pw,
				c->sheet->url, token->idata,
				location);
		if (error != CSS_OK)
			return error;

		*location_type = CSS_FONT_FACE_LOCATION_TYPE_URI;

		consumeWhitespace(vector, ctx);

		token = parserutils_vector_peek(vector, *ctx);
		if (token != NULL && token->type == CSS_TOKEN_FUNCTION &&
				lwc_string_caseless_isequal(token->idata,
				c->strings[FORMAT], &match) == lwc_error_ok &&
				match) {
			parserutils_vector_iterate(vector, ctx);

			error = font_face_src_parse_format(c, vector, ctx,
					format);
			if (error != CSS_OK) {
				lwc_string_unref(*location);
				return error;
			}
		}
	} else if (token->type == CSS_TOKEN_FUNCTION &&
			lwc_string_caseless_isequal(token->idata,
					c->strings[LOCAL],
					&match) == lwc_error_ok && match) {
		consumeWhitespace(vector, ctx);

		error = css__ident_list_or_string_to_string(
				c, vector, ctx, NULL, location);
		if (error != CSS_OK)
			return error;

		consumeWhitespace(vector, ctx);

		token = parserutils_vector_iterate(vector, ctx);
		if (token == NULL || tokenIsChar(token, ')') == false) {
			lwc_string_unref(*location);
			return CSS_INVALID;
		}

		*location_type = CSS_FONT_FACE_LOCATION_TYPE_LOCAL;
	} else {
		return CSS_INVALID;
	}

	return CSS_OK;
}

static css_error font_face_parse_src(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
	     	css_font_face *font_face)
{
	int32_t orig_ctx = *ctx;
	css_error error = CSS_OK;
	const css_token *token;
	css_font_face_src *srcs = NULL, *new_srcs = NULL;
	uint32_t n_srcs = 0;

	/* src             ::= spec-or-name [ ',' spec-or-name ]*
 	 * spec-or-name    ::= font-face-spec | font-face-name
 	 * font-face-spec  ::= URI [ 'format(' STRING [ ',' STRING ]* ')' ]?
 	 * font-face-name  ::= 'local(' ident-list-or-string ')'
 	 * ident-list-or-string ::= IDENT IDENT* | STRING
 	 */

	/* Create one css_font_face_src for each consecutive location and
	 * [potentially] type pair in the comma-separated list
	 */
	do {
		lwc_string *location;
		css_font_face_location_type location_type =
				CSS_FONT_FACE_LOCATION_TYPE_UNSPECIFIED;
		css_font_face_format format =
				CSS_FONT_FACE_FORMAT_UNSPECIFIED;

		error = font_face_src_parse_spec_or_name(c, vector, ctx,
				&location, &location_type, &format);
		if (error != CSS_OK)
			goto cleanup;

		/* This will be inefficient if there are a lot of locations -
		 * probably not a problem in practice.
		 */
		new_srcs = realloc(srcs,
				(n_srcs + 1) * sizeof(css_font_face_src));
		if (new_srcs == NULL) {
			error = CSS_NOMEM;
			goto cleanup;
		}
		srcs = new_srcs;

		srcs[n_srcs].location = location;
		srcs[n_srcs].bits[0] = format << 2 | location_type;

		++n_srcs;

		consumeWhitespace(vector, ctx);
		token = parserutils_vector_iterate(vector, ctx);
	} while (token != NULL && tokenIsChar(token, ','));

	error = css__font_face_set_srcs(font_face, srcs, n_srcs);

cleanup:
	if (error != CSS_OK) {
		*ctx = orig_ctx;
		if (srcs != NULL)
			free(srcs);
	}

	return error;
}

static css_error font_face_parse_font_style(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_font_face *font_face)
{
	int32_t orig_ctx = *ctx;
	css_error error = CSS_OK;
	const css_token *token;
	enum css_font_style_e style = 0;
	bool match;

	/* IDENT(normal, italic, oblique) */

	token = parserutils_vector_iterate(vector, ctx);
	if ((token == NULL) || ((token->type != CSS_TOKEN_IDENT))) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}

	if ((lwc_string_caseless_isequal(token->idata,
			c->strings[NORMAL], &match) == lwc_error_ok && match)) {
		style = CSS_FONT_STYLE_NORMAL;
	} else if ((lwc_string_caseless_isequal(token->idata,
			c->strings[ITALIC], &match) == lwc_error_ok && match)) {
		style = CSS_FONT_STYLE_ITALIC;
	} else if ((lwc_string_caseless_isequal(token->idata,
			c->strings[OBLIQUE], &match) == lwc_error_ok &&
			match)) {
		style = CSS_FONT_STYLE_OBLIQUE;
	} else {
		error = CSS_INVALID;
	}

	if (error == CSS_OK) {
		font_face->bits[0] = (font_face->bits[0] & 0xfc) | style;
	} else {
		*ctx = orig_ctx;
	}

	return error;
}

/** Onyx: a font-weight value: a number 1..1000, normal (400) or bold (700) */
static bool font_face_weight_value(css_language *c, const css_token *token,
		int *w)
{
	bool match;

	if (token == NULL)
		return false;
	if (token->type == CSS_TOKEN_NUMBER) {
		size_t consumed = 0;
		css_fixed num = css__number_from_lwc_string(token->idata,
				false, &consumed);

		/* Invalid if there are trailing characters */
		if (consumed != lwc_string_length(token->idata))
			return false;
		*w = FIXTOINT(num);
		return *w >= 1 && *w <= 1000;
	}
	if (token->type != CSS_TOKEN_IDENT)
		return false;
	if (lwc_string_caseless_isequal(token->idata, c->strings[NORMAL],
			&match) == lwc_error_ok && match) {
		*w = 400;
		return true;
	}
	if (lwc_string_caseless_isequal(token->idata, c->strings[BOLD],
			&match) == lwc_error_ok && match) {
		*w = 700;
		return true;
	}
	return false;
}

/**
 * font-weight: one weight, or a range (a variable font: 100 900) -- Onyx: any number
 * from 1 to 1000 (upstream: only the hundreds).
 */
static css_error font_face_parse_font_weight(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_font_face *font_face)
{
	int32_t orig_ctx = *ctx;
	const css_token *token;
	enum css_font_weight_e weight = 0;
	int w1, w2;

	token = parserutils_vector_iterate(vector, ctx);
	if (!font_face_weight_value(c, token, &w1)) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	w2 = w1;
	consumeWhitespace(vector, ctx);
	token = parserutils_vector_peek(vector, *ctx);
	if (token != NULL) {
		if (!font_face_weight_value(c, token, &w2)) {
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
		parserutils_vector_iterate(vector, ctx);
		if (w2 < w1) {
			int t = w1;
			w1 = w2;
			w2 = t;
		}
	}

	font_face->weight_min = w1;
	font_face->weight_max = w2;

	/* the nearest of the keywords, for css_font_face_font_weight */
	switch ((w1 + 50) / 100) {
	case 0: case 1: weight = CSS_FONT_WEIGHT_100; break;
	case 2: weight = CSS_FONT_WEIGHT_200; break;
	case 3: weight = CSS_FONT_WEIGHT_300; break;
	case 4: weight = CSS_FONT_WEIGHT_400; break;
	case 5: weight = CSS_FONT_WEIGHT_500; break;
	case 6: weight = CSS_FONT_WEIGHT_600; break;
	case 7: weight = CSS_FONT_WEIGHT_700; break;
	case 8: weight = CSS_FONT_WEIGHT_800; break;
	default: weight = CSS_FONT_WEIGHT_900; break;
	}
	font_face->bits[0] = (font_face->bits[0] & 0xc3) | (weight << 2);

	return CSS_OK;
}

/** Onyx: a hexadecimal code point of a unicode-range ('?': any digit, lo 0 / hi F) */
static bool font_face_hex(const char *s, size_t len, bool hi, uint32_t *v)
{
	size_t i;

	if (len == 0 || len > 6)
		return false;
	*v = 0;
	for (i = 0; i < len; i++) {
		char ch = s[i];
		uint32_t d;

		if (ch >= '0' && ch <= '9')
			d = ch - '0';
		else if (ch >= 'a' && ch <= 'f')
			d = ch - 'a' + 10;
		else if (ch >= 'A' && ch <= 'F')
			d = ch - 'A' + 10;
		else if (ch == '?')
			d = hi ? 15 : 0;
		else
			return false;
		*v = *v * 16 + d;
	}
	return true;
}

/**
 * Onyx: unicode-range -- U+0000-00FF, U+0131, U+4?? ...: the characters a face
 * covers (a font split in subsets: the page fetches those it needs).
 */
static css_error font_face_parse_unicode_range(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_font_face *font_face)
{
	int32_t orig_ctx = *ctx;
	const css_token *token;
	uint32_t (*ranges)[2] = NULL;
	uint32_t n = 0;

	do {
		const char *s;
		size_t len, dash;
		uint32_t first, last;
		uint32_t (*nr)[2];

		consumeWhitespace(vector, ctx);
		token = parserutils_vector_iterate(vector, ctx);
		if (token == NULL || token->type != CSS_TOKEN_UNICODE_RANGE ||
				token->idata == NULL)
			goto invalid;
		s = lwc_string_data(token->idata);
		len = lwc_string_length(token->idata);
		for (dash = 0; dash < len && s[dash] != '-'; dash++)
			;
		if (dash < len) {
			if (!font_face_hex(s, dash, false, &first) ||
			    !font_face_hex(s + dash + 1, len - dash - 1, true,
					&last))
				goto invalid;
		} else if (!font_face_hex(s, len, false, &first) ||
			   !font_face_hex(s, len, true, &last)) {
			goto invalid;
		}
		if (last > 0x10ffff)
			last = 0x10ffff;
		if (first > last)
			goto invalid;

		nr = realloc(ranges, (n + 1) * sizeof(*ranges));
		if (nr == NULL) {
			free(ranges);
			*ctx = orig_ctx;
			return CSS_NOMEM;
		}
		ranges = nr;
		ranges[n][0] = first;
		ranges[n][1] = last;
		n++;

		consumeWhitespace(vector, ctx);
		token = parserutils_vector_iterate(vector, ctx);
	} while (token != NULL && tokenIsChar(token, ','));

	if (token != NULL)
		goto invalid;

	free(font_face->ranges);
	font_face->ranges = ranges;
	font_face->n_ranges = n;
	return CSS_OK;

invalid:
	free(ranges);
	*ctx = orig_ctx;
	return CSS_INVALID;
}

/**
 * Parse a descriptor in an @font-face rule
 *
 * \param c           Parsing context
 * \param descriptor  Token for this descriptor
 * \param vector      Vector of tokens to process
 * \param ctx	      Pointer to vector iteration context
 * \param rule	      Rule to process descriptor into
 * \return CSS_OK on success,
 *         CSS_BADPARM on bad parameters,
 *         CSS_INVALID on invalid syntax,
 *         CSS_NOMEM on memory exhaustion
 */
css_error css__parse_font_descriptor(css_language *c,
		const css_token *descriptor, const parserutils_vector *vector,
		int32_t *ctx, css_rule_font_face *rule)
{
	css_font_face *font_face = rule->font_face;
	css_error error;
	bool match;

	if (font_face == NULL) {
		error = css__font_face_create(&font_face);
		if (error != CSS_OK) {
			return error;
		}

		rule->font_face = font_face;
	}

	if (lwc_string_caseless_isequal(descriptor->idata,
			c->strings[FONT_FAMILY], &match) == lwc_error_ok &&
			match) {
		return font_face_parse_font_family(c, vector, ctx, font_face);
	} else if (lwc_string_caseless_isequal(descriptor->idata,
			c->strings[SRC], &match) == lwc_error_ok && match) {
		return font_face_parse_src(c, vector, ctx, font_face);
	} else if (lwc_string_caseless_isequal(descriptor->idata,
			c->strings[FONT_STYLE], &match) == lwc_error_ok &&
			match) {
		return font_face_parse_font_style(c, vector, ctx, font_face);
	} else if (lwc_string_caseless_isequal(descriptor->idata,
			c->strings[FONT_WEIGHT], &match) == lwc_error_ok &&
			match) {
		return font_face_parse_font_weight(c, vector, ctx, font_face);
	} else if (lwc_string_caseless_isequal(descriptor->idata,
			c->strings[UNICODE_RANGE], &match) == lwc_error_ok &&
			match) {
		/* Onyx */
		return font_face_parse_unicode_range(c, vector, ctx, font_face);
	}

	return CSS_INVALID;
}

