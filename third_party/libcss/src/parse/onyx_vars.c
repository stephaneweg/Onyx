/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: CSS custom properties and var() -- the parsing side. A custom property's value and
 * a value using var() are kept as text (their tokens written back as CSS): the variables are
 * only known once an element's cascade is (src/select/onyx_vars.c substitutes them there and
 * parses the result with the property's own parser). See src/parse/onyx_vars.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "parse/onyx_vars.h"
#include "parse/properties/utils.h"
#include "utils/utils.h"

/* ---- a growable text buffer -------------------------------------------------------------- */

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
		size_t cap = (b->cap == 0) ? 64 : b->cap;
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

static void ob_putc(onyx_buf *b, char ch)
{
	ob_putn(b, &ch, 1);
}

static bool onyx_name_char(uint8_t ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
			(ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch >= 0x80;
}

/* An identifier (a function's, a hash's name): the name characters as they are, the others
 * escaped -- and a leading digit (or "-" digit) of an identifier, which would lex as a
 * number. */
static void ob_ident(onyx_buf *b, lwc_string *s, bool hash)
{
	const uint8_t *d = (const uint8_t *) lwc_string_data(s);
	size_t n = lwc_string_length(s), i;

	for (i = 0; i < n; i++) {
		uint8_t ch = d[i];
		bool esc = !onyx_name_char(ch);
		if (!hash && ch >= '0' && ch <= '9' &&
				(i == 0 || (i == 1 && d[0] == '-')))
			esc = true;
		if (esc) {
			char tmp[8];
			snprintf(tmp, sizeof(tmp), "\\%x ", ch);
			ob_puts(b, tmp);
		} else {
			ob_putc(b, (char) ch);
		}
	}
}

static void ob_string(onyx_buf *b, lwc_string *s)
{
	const char *d = lwc_string_data(s);
	size_t n = lwc_string_length(s), i;

	ob_putc(b, '"');
	for (i = 0; i < n; i++) {
		if (d[i] == '"' || d[i] == '\\') {
			ob_putc(b, '\\');
			ob_putc(b, d[i]);
		} else if (d[i] == '\n') {
			ob_puts(b, "\\a ");
		} else {
			ob_putc(b, d[i]);
		}
	}
	ob_putc(b, '"');
}

/* Would these two tokens, written side by side, lex as one? (Then a space goes between.) */
static bool onyx_would_merge(const css_token *a, const css_token *t)
{
	bool a_word = a->type == CSS_TOKEN_IDENT || a->type == CSS_TOKEN_NUMBER ||
			a->type == CSS_TOKEN_DIMENSION || a->type == CSS_TOKEN_HASH ||
			a->type == CSS_TOKEN_ATKEYWORD;
	bool t_word = t->type == CSS_TOKEN_IDENT || t->type == CSS_TOKEN_NUMBER ||
			t->type == CSS_TOKEN_DIMENSION || t->type == CSS_TOKEN_PERCENTAGE ||
			t->type == CSS_TOKEN_FUNCTION || t->type == CSS_TOKEN_URI;
	return a_word && t_word;
}

static void ob_token(onyx_buf *b, const css_token *t)
{
	switch (t->type) {
	case CSS_TOKEN_IDENT:
		ob_ident(b, t->idata, false);
		break;
	case CSS_TOKEN_ATKEYWORD:
		ob_putc(b, '@');
		ob_ident(b, t->idata, false);
		break;
	case CSS_TOKEN_HASH:
		ob_putc(b, '#');
		ob_ident(b, t->idata, true);
		break;
	case CSS_TOKEN_FUNCTION:
		ob_ident(b, t->idata, false);
		ob_putc(b, '(');
		break;
	case CSS_TOKEN_STRING:
	case CSS_TOKEN_INVALID_STRING:
		ob_string(b, t->idata);
		break;
	case CSS_TOKEN_URI:
		ob_puts(b, "url(");
		ob_string(b, t->idata);
		ob_putc(b, ')');
		break;
	case CSS_TOKEN_UNICODE_RANGE:
		ob_puts(b, "U+");
		ob_putn(b, lwc_string_data(t->idata), lwc_string_length(t->idata));
		break;
	case CSS_TOKEN_CHAR:
	case CSS_TOKEN_NUMBER:
	case CSS_TOKEN_DIMENSION:
		ob_putn(b, lwc_string_data(t->idata), lwc_string_length(t->idata));
		break;
	case CSS_TOKEN_PERCENTAGE:
		ob_putn(b, lwc_string_data(t->idata), lwc_string_length(t->idata));
		ob_putc(b, '%');
		break;
	case CSS_TOKEN_CDO:		ob_puts(b, "<!--"); break;
	case CSS_TOKEN_CDC:		ob_puts(b, "-->"); break;
	case CSS_TOKEN_INCLUDES:	ob_puts(b, "~="); break;
	case CSS_TOKEN_DASHMATCH:	ob_puts(b, "|="); break;
	case CSS_TOKEN_PREFIXMATCH:	ob_puts(b, "^="); break;
	case CSS_TOKEN_SUFFIXMATCH:	ob_puts(b, "$="); break;
	case CSS_TOKEN_SUBSTRINGMATCH:	ob_puts(b, "*="); break;
	default:
		break;
	}
}

/* Documented in parse/onyx_vars.h */
css_error css__onyx_tokens_to_text(const parserutils_vector *vector, int32_t start,
		int32_t end, lwc_string **text)
{
	onyx_buf b = { NULL, 0, 0, false };
	const css_token *prev = NULL, *t;
	bool space = false;
	lwc_error lerror;
	int32_t i;

	for (i = start; i < end && (t = parserutils_vector_peek(vector, i)) != NULL; i++) {
		if (t->type == CSS_TOKEN_S) {
			space = true;
			continue;
		}
		if (prev != NULL && (space || onyx_would_merge(prev, t)))
			ob_putc(&b, ' ');
		space = false;
		ob_token(&b, t);
		prev = t;
	}
	if (b.oom) {
		free(b.p);
		return CSS_NOMEM;
	}

	lerror = lwc_intern_string(b.p != NULL ? b.p : "", b.n, text);
	free(b.p);
	return css_error_from_lwc_error(lerror);
}

/* ---- the declarations --------------------------------------------------------------------- */

/* Documented in parse/onyx_vars.h */
bool css__onyx_is_custom_name(lwc_string *name)
{
	return name != NULL && lwc_string_length(name) > 2 &&
			lwc_string_data(name)[0] == '-' && lwc_string_data(name)[1] == '-';
}

/* Documented in parse/onyx_vars.h */
bool css__onyx_value_has_var(css_language *c, const parserutils_vector *vector,
		int32_t ctx)
{
	const css_token *t;
	bool match;

	while ((t = parserutils_vector_iterate(vector, &ctx)) != NULL) {
		if (t->type == CSS_TOKEN_FUNCTION &&
				lwc_string_caseless_isequal(t->idata, c->strings[FN_VAR],
					&match) == lwc_error_ok && match)
			return true;
	}
	return false;
}

/* The end of the value from ctx: the end of the vector, or its "! important" (then
 * *important). */
static int32_t onyx_value_end(css_language *c, const parserutils_vector *vector,
		int32_t ctx, bool *important)
{
	const css_token *t;
	int32_t i, n = ctx, last = -1, before_last = -1;
	bool match;

	while (parserutils_vector_peek(vector, n) != NULL)
		n++;
	for (i = ctx; i < n; i++) {
		t = parserutils_vector_peek(vector, i);
		if (t->type != CSS_TOKEN_S) {
			before_last = last;
			last = i;
		}
	}
	*important = false;
	if (last >= 0 && before_last >= 0) {
		const css_token *imp = parserutils_vector_peek(vector, last);
		const css_token *bang = parserutils_vector_peek(vector, before_last);
		if (imp->type == CSS_TOKEN_IDENT && tokenIsChar(bang, '!') &&
				lwc_string_caseless_isequal(imp->idata,
					c->strings[IMPORTANT], &match) == lwc_error_ok &&
				match) {
			*important = true;
			return before_last;
		}
	}
	return n;
}

/* Append a style of words to the rule (the style is the sheet's, or destroyed). */
static css_error onyx_append(css_language *c, css_rule *rule, const css_code_t *w,
		uint32_t n)
{
	css_style *style;
	css_error error;
	uint32_t i;

	error = css__stylesheet_style_create(c->sheet, &style);
	if (error != CSS_OK)
		return error;
	for (i = 0; i < n && error == CSS_OK; i++)
		error = css__stylesheet_style_append(style, w[i]);
	if (error == CSS_OK)
		error = css__stylesheet_rule_append_style(c->sheet, rule, style);
	if (error != CSS_OK)
		css__stylesheet_style_destroy(style);
	return error;
}

/* Documented in parse/onyx_vars.h */
css_error css__onyx_parse_custom(css_language *c, const css_token *property,
		const parserutils_vector *vector, int32_t *ctx, css_rule *rule)
{
	bool important;
	int32_t end = onyx_value_end(c, vector, *ctx, &important);
	lwc_string *text;
	uint32_t name_idx, text_idx;
	css_code_t w[3];
	css_error error;

	error = css__onyx_tokens_to_text(vector, *ctx, end, &text);
	if (error != CSS_OK)
		return error;
	/* (string_add takes the references, even on failure) */
	error = css__stylesheet_string_add(c->sheet, lwc_string_ref(property->idata),
			&name_idx);
	if (error != CSS_OK) {
		lwc_string_unref(text);
		return error;
	}
	error = css__stylesheet_string_add(c->sheet, text, &text_idx);
	if (error != CSS_OK)
		return error;

	w[0] = buildOPV((opcode_t) CSS_ONYX_OP_CUSTOM, important ? FLAG_IMPORTANT : 0, 0);
	w[1] = name_idx;
	w[2] = text_idx;
	error = onyx_append(c, rule, w, 3);
	if (error == CSS_OK) {
		while (parserutils_vector_iterate(vector, ctx) != NULL)
			;	/* all consumed */
	}
	return error;
}

/* Documented in parse/onyx_vars.h */
css_error css__onyx_parse_var_decl(css_language *c, int i, css_prop_handler handler,
		const parserutils_vector *vector, int32_t *ctx, css_rule *rule)
{
	bool important;
	int32_t end = onyx_value_end(c, vector, *ctx, &important);
	parserutils_vector *probe_vector = NULL;
	css_style *probe = NULL;
	css_token inherit;
	int32_t pctx = 0;
	css_code_t w[3 + 64];
	uint32_t n = 0, k, text_idx;
	lwc_string *text;
	css_error error;

	/* The longhands the property sets: what its parser writes for "inherit" (a flag
	 * value, without operands, for each of them) */
	memset(&inherit, 0, sizeof(inherit));
	inherit.type = CSS_TOKEN_IDENT;
	inherit.idata = c->strings[INHERIT];
	if (parserutils_vector_create(sizeof(css_token), 1, &probe_vector) != PARSERUTILS_OK)
		return CSS_NOMEM;
	if (parserutils_vector_append(probe_vector, &inherit) != PARSERUTILS_OK) {
		parserutils_vector_destroy(probe_vector);
		return CSS_NOMEM;
	}
	error = css__stylesheet_style_create(c->sheet, &probe);
	if (error == CSS_OK)
		error = handler(c, probe_vector, &pctx, probe);
	if (error == CSS_OK) {
		for (k = 0; k < probe->used && n < 64; k++)
			w[3 + n++] = getOpcode(probe->bytecode[k]);
	}
	if (probe != NULL)
		css__stylesheet_style_destroy(probe);
	parserutils_vector_destroy(probe_vector);
	if (error != CSS_OK || n == 0)
		return CSS_INVALID;

	error = css__onyx_tokens_to_text(vector, *ctx, end, &text);
	if (error != CSS_OK)
		return error;
	error = css__stylesheet_string_add(c->sheet, text, &text_idx);
	if (error != CSS_OK)
		return error;

	w[0] = buildOPV((opcode_t) CSS_ONYX_OP_VAR, important ? FLAG_IMPORTANT : 0, n);
	w[1] = text_idx;
	w[2] = (css_code_t) i;
	error = onyx_append(c, rule, w, 3 + n);
	if (error == CSS_OK) {
		while (parserutils_vector_iterate(vector, ctx) != NULL)
			;	/* all consumed */
	}
	return error;
}
