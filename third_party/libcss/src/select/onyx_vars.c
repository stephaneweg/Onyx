/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: CSS custom properties and var() -- the selection side. See select/onyx_vars.h for
 * the model, parse/onyx_vars.h for the bytecode.
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <libcss/stylesheet.h>

#include "bytecode/bytecode.h"
#include "parse/onyx_vars.h"
#include "parse/propstrings.h"
#include "select/dispatch.h"
#include "select/onyx_vars.h"
#include "utils/utils.h"

/* How deep var() may nest (through custom properties): a cycle stops here too. */
#define ONYX_VAR_DEPTH 24

/* ---- the variables ---------------------------------------------------------------------- */

css_onyx_vars *css__onyx_vars_ref(css_onyx_vars *vars)
{
	if (vars != NULL)
		vars->refcount++;
	return vars;
}

void css__onyx_vars_unref(css_onyx_vars *vars)
{
	uint32_t i;

	if (vars == NULL || --vars->refcount > 0)
		return;
	for (i = 0; i < vars->n; i++) {
		lwc_string_unref(vars->v[i].name);
		lwc_string_unref(vars->v[i].value);
	}
	free(vars);
}

static lwc_string *onyx_vars_get(const css_onyx_vars *vars, lwc_string *name)
{
	uint32_t i;

	if (vars == NULL)
		return NULL;
	for (i = 0; i < vars->n; i++) {
		if (vars->v[i].name == name)	/* (interned: same text, same string) */
			return vars->v[i].value;
	}
	return NULL;
}

/* ---- a text buffer ------------------------------------------------------------------------ */

typedef struct {
	char *p;
	size_t n, cap;
	bool oom;
} onyx_buf;

static void ob_putn(onyx_buf *b, const char *s, size_t n)
{
	if (b->oom || n == 0)
		return;
	if (b->n + n + 1 > b->cap) {
		size_t cap = (b->cap == 0) ? 128 : b->cap;
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

static bool onyx_name_char(char ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
			(ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
			(unsigned char) ch >= 0x80;
}

static bool onyx_space(char ch)
{
	return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f';
}

/* The end of the string literal starting at p[i] (its quote). */
static size_t onyx_skip_string(const char *p, size_t n, size_t i)
{
	char q = p[i++];
	while (i < n && p[i] != q) {
		if (p[i] == '\\' && i + 1 < n)
			i++;
		i++;
	}
	return (i < n) ? i + 1 : n;
}

/* The ')' closing a parenthesis whose content starts at p[i], or n. */
static size_t onyx_close_paren(const char *p, size_t n, size_t i)
{
	int depth = 0;
	while (i < n) {
		char ch = p[i];
		if (ch == '"' || ch == '\'') {
			i = onyx_skip_string(p, n, i);
			continue;
		}
		if (ch == '\\') {
			i += 2;
			continue;
		}
		if (ch == '(')
			depth++;
		else if (ch == ')') {
			if (depth == 0)
				return i;
			depth--;
		}
		i++;
	}
	return n;
}

static bool onyx_has_var(lwc_string *s)
{
	const char *p = lwc_string_data(s);
	size_t n = lwc_string_length(s), i;
	for (i = 0; i + 4 <= n; i++) {
		if ((p[i] == 'v' || p[i] == 'V') && strncasecmp(p + i, "var(", 4) == 0)
			return true;
	}
	return false;
}

/* The value is one of the CSS-wide keywords? */
static bool onyx_is_keyword(lwc_string *s, const char *kw)
{
	size_t n = lwc_string_length(s), k = strlen(kw);
	return n == k && strncasecmp(lwc_string_data(s), kw, k) == 0;
}

/* ---- substitution ------------------------------------------------------------------------ */

/* Where var() looks its variables up: an element's own custom property declarations (being
 * resolved: they may use each other), over its inherited, resolved, variables. */
typedef struct {
	const css_onyx_vars *resolved;
	uint32_t n_own;
	lwc_string **own_name;
	lwc_string **own_value;		/* as declared; NULL: the guaranteed-invalid value */
	lwc_string **own_resolved;
	uint8_t *own_state;		/* 0: to do, 1: doing (a cycle if met), 2: done, 3: invalid */
} onyx_scope;

static bool onyx_subst(onyx_scope *sc, const char *p, size_t n, int depth, onyx_buf *out);

/* A variable's value, its own var() substituted; NULL if undefined or invalid. */
static lwc_string *onyx_lookup(onyx_scope *sc, lwc_string *name, int depth)
{
	uint32_t k;

	for (k = 0; k < sc->n_own; k++) {
		onyx_buf b = { NULL, 0, 0, false };
		bool ok;

		if (sc->own_name[k] != name)
			continue;
		if (sc->own_state[k] == 2)
			return sc->own_resolved[k];
		if (sc->own_state[k] != 0)
			return NULL;		/* invalid, or a cycle */
		if (sc->own_value[k] == NULL) {
			sc->own_state[k] = 3;
			return NULL;
		}
		if (!onyx_has_var(sc->own_value[k])) {
			sc->own_resolved[k] = lwc_string_ref(sc->own_value[k]);
			sc->own_state[k] = 2;
			return sc->own_resolved[k];
		}
		sc->own_state[k] = 1;
		ok = onyx_subst(sc, lwc_string_data(sc->own_value[k]),
				lwc_string_length(sc->own_value[k]), depth + 1, &b);
		if (ok && !b.oom && lwc_intern_string(b.p != NULL ? b.p : "", b.n,
				&sc->own_resolved[k]) == lwc_error_ok) {
			sc->own_state[k] = 2;
		} else {
			sc->own_state[k] = 3;
		}
		free(b.p);
		return (sc->own_state[k] == 2) ? sc->own_resolved[k] : NULL;
	}
	return onyx_vars_get(sc->resolved, name);
}

/* Append p[0..n) with its var() substituted; false if one cannot be. */
static bool onyx_subst(onyx_scope *sc, const char *p, size_t n, int depth, onyx_buf *out)
{
	size_t i = 0;

	if (depth > ONYX_VAR_DEPTH)
		return false;

	while (i < n) {
		char ch = p[i];

		if (ch == '"' || ch == '\'') {
			size_t j = onyx_skip_string(p, n, i);
			ob_putn(out, p + i, j - i);
			i = j;
			continue;
		}
		if (ch == '\\' && i + 1 < n) {
			ob_putn(out, p + i, 2);
			i += 2;
			continue;
		}
		if ((ch == 'v' || ch == 'V') && i + 4 <= n &&
				strncasecmp(p + i, "var(", 4) == 0 &&
				(i == 0 || !onyx_name_char(p[i - 1]))) {
			size_t j = i + 4, name_s, name_e, fb_s = 0, fb_e = 0, close;
			bool has_fb = false;
			lwc_string *name, *value;
			const char *vp;
			size_t vn;

			while (j < n && onyx_space(p[j]))
				j++;
			name_s = j;
			while (j < n && onyx_name_char(p[j]))
				j++;
			name_e = j;
			if (name_e - name_s < 3 || p[name_s] != '-' || p[name_s + 1] != '-')
				return false;
			while (j < n && onyx_space(p[j]))
				j++;
			if (j < n && p[j] == ',') {
				has_fb = true;
				fb_s = j + 1;
				close = onyx_close_paren(p, n, fb_s);
				if (close == n)
					return false;
				fb_e = close;
				while (fb_s < fb_e && onyx_space(p[fb_s]))
					fb_s++;
				while (fb_e > fb_s && onyx_space(p[fb_e - 1]))
					fb_e--;
			} else if (j < n && p[j] == ')') {
				close = j;
			} else {
				return false;
			}

			if (lwc_intern_string(p + name_s, name_e - name_s, &name) != lwc_error_ok)
				return false;
			value = onyx_lookup(sc, name, depth);
			lwc_string_unref(name);

			if (value != NULL) {
				vp = lwc_string_data(value);
				vn = lwc_string_length(value);
				/* (a value is spliced as tokens: not glued to a name next to it) */
				if (vn > 0 && out->n > 0 && onyx_name_char(out->p[out->n - 1]) &&
						onyx_name_char(vp[0]))
					ob_putn(out, " ", 1);
				ob_putn(out, vp, vn);
			} else if (has_fb) {
				size_t before = out->n;
				if (fb_e > fb_s && out->n > 0 &&
						onyx_name_char(out->p[out->n - 1]) &&
						onyx_name_char(p[fb_s]))
					ob_putn(out, " ", 1);
				if (!onyx_subst(sc, p + fb_s, fb_e - fb_s, depth + 1, out))
					return false;
				(void) before;
			} else {
				return false;		/* undefined, no fallback */
			}

			i = close + 1;
			if (i < n && onyx_name_char(p[i]) && out->n > 0 &&
					onyx_name_char(out->p[out->n - 1]))
				ob_putn(out, " ", 1);
			continue;
		}
		ob_putn(out, &ch, 1);
		i++;
	}
	return !out->oom;
}

/* ---- the selection state ------------------------------------------------------------------ */

static struct css_onyx_state *onyx_state(css_select_state *state)
{
	if (state->onyx == NULL)
		state->onyx = calloc(1, sizeof(struct css_onyx_state));
	return state->onyx;
}

void css__onyx_state_destroy(css_select_state *state)
{
	struct css_onyx_state *st = state->onyx;

	if (st == NULL)
		return;
	free(st->custom);
	free(st->pending);
	free(st);
	state->onyx = NULL;
}

static css_error onyx_cascade_custom(uint32_t opv, css_style *style,
		css_select_state *state)
{
	struct css_onyx_state *st;
	lwc_string *name = NULL, *value = NULL;
	css_onyx_custom *c = NULL;
	uint32_t name_idx, value_idx, k;

	name_idx = *((uint32_t *) style->bytecode);
	advance_bytecode(style, sizeof(name_idx));
	value_idx = *((uint32_t *) style->bytecode);
	advance_bytecode(style, sizeof(value_idx));

	if (css__stylesheet_string_get(style->sheet, name_idx, &name) != CSS_OK ||
			css__stylesheet_string_get(style->sheet, value_idx, &value) != CSS_OK)
		return CSS_OK;

	st = onyx_state(state);
	if (st == NULL)
		return CSS_NOMEM;

	for (k = 0; k < st->n_custom; k++) {
		if (st->custom[k].name == name &&
				st->custom[k].pseudo == state->current_pseudo) {
			c = &st->custom[k];
			break;
		}
	}
	if (c == NULL) {
		if (st->n_custom == st->a_custom) {
			uint32_t a = st->a_custom ? st->a_custom * 2 : 16;
			css_onyx_custom *n = realloc(st->custom, a * sizeof(*n));
			if (n == NULL)
				return CSS_NOMEM;
			st->custom = n;
			st->a_custom = a;
		}
		c = &st->custom[st->n_custom++];
		memset(c, 0, sizeof(*c));
		c->name = name;
		c->pseudo = state->current_pseudo;
	}

	/* (the strings are the sheets', which outlive the selection) */
	if (css__outranks_prop_state(&c->rank, isImportant(opv), state, FLAG_VALUE__NONE))
		c->value = value;

	return CSS_OK;
}

static css_error onyx_cascade_var(uint32_t opv, css_style *style,
		css_select_state *state)
{
	struct css_onyx_state *st;
	css_onyx_pending *pd;
	lwc_string *text = NULL;
	uint32_t text_idx, prop, n = getValue(opv), k;
	const css_code_t *longhand;

	text_idx = *((uint32_t *) style->bytecode);
	advance_bytecode(style, sizeof(text_idx));
	prop = *((uint32_t *) style->bytecode);
	advance_bytecode(style, sizeof(prop));
	longhand = style->bytecode;
	advance_bytecode(style, n * sizeof(css_code_t));

	if (css__stylesheet_string_get(style->sheet, text_idx, &text) != CSS_OK)
		return CSS_OK;

	st = onyx_state(state);
	if (st == NULL)
		return CSS_NOMEM;
	if (st->n_pending == st->a_pending) {
		uint32_t a = st->a_pending ? st->a_pending * 2 : 16;
		css_onyx_pending *np;
		if (a > 0xfffe)
			return CSS_OK;		/* (pending_of holds 16 bits) */
		np = realloc(st->pending, a * sizeof(*np));
		if (np == NULL)
			return CSS_NOMEM;
		st->pending = np;
		st->a_pending = a;
	}
	pd = &st->pending[st->n_pending];
	pd->sheet = style->sheet;
	pd->text = text;
	pd->prop = (uint16_t) prop;
	pd->important = isImportant(opv);
	pd->origin = state->current_origin;
	pd->specificity = state->current_specificity;
	st->n_pending++;

	for (k = 0; k < n; k++) {
		opcode_t op = getOpcode(longhand[k]);
		if (op >= CSS_N_PROPERTIES)
			continue;
		if (css__outranks_existing(op, pd->important, state, FLAG_VALUE__NONE))
			st->pending_of[op][state->current_pseudo] = (uint16_t) st->n_pending;
	}

	return CSS_OK;
}

/* Documented in select/onyx_vars.h */
css_error css__onyx_cascade(uint32_t opv, css_style *style, css_select_state *state)
{
	switch (getOpcode(opv)) {
	case CSS_ONYX_OP_CUSTOM:
		return onyx_cascade_custom(opv, style, state);
	case CSS_ONYX_OP_VAR:
		return onyx_cascade_var(opv, style, state);
	default:
		return CSS_INVALID;	/* (unknown opcode: stop the style) */
	}
}

/* ---- the element's variables ---------------------------------------------------------------- */

/* The variables of the element (pseudo NONE), or of its pseudo element: those inherited
 * (parent), with the element's own on top. A new reference in *out (maybe NULL). */
static css_error onyx_make_vars(struct css_onyx_state *st, uint8_t pseudo,
		css_onyx_vars *parent, css_onyx_vars **out)
{
	onyx_scope sc;
	uint32_t n_own = 0, k, i, n, same = 0;
	css_onyx_vars *vars;
	css_error error = CSS_OK;

	*out = NULL;
	for (k = 0; st != NULL && k < st->n_custom; k++) {
		if (st->custom[k].pseudo == pseudo && st->custom[k].rank.set)
			n_own++;
	}
	if (n_own == 0) {
		*out = css__onyx_vars_ref(parent);
		return CSS_OK;
	}

	memset(&sc, 0, sizeof(sc));
	sc.resolved = parent;
	sc.own_name = calloc(n_own, sizeof(lwc_string *));
	sc.own_value = calloc(n_own, sizeof(lwc_string *));
	sc.own_resolved = calloc(n_own, sizeof(lwc_string *));
	sc.own_state = calloc(n_own, 1);
	if (sc.own_name == NULL || sc.own_value == NULL || sc.own_resolved == NULL ||
			sc.own_state == NULL) {
		error = CSS_NOMEM;
		goto done;
	}
	for (k = 0; k < st->n_custom; k++) {
		css_onyx_custom *c = &st->custom[k];
		lwc_string *v = c->value;
		if (c->pseudo != pseudo || !c->rank.set)
			continue;
		/* the CSS-wide keywords: inherit / unset / revert keep the parent's value (a
		 * custom property is inherited), initial is the guaranteed-invalid value */
		if (v != NULL && (onyx_is_keyword(v, "inherit") || onyx_is_keyword(v, "unset") ||
				onyx_is_keyword(v, "revert") ||
				onyx_is_keyword(v, "revert-layer")))
			continue;
		if (v != NULL && onyx_is_keyword(v, "initial"))
			v = NULL;
		sc.own_name[sc.n_own] = c->name;
		sc.own_value[sc.n_own] = v;
		sc.n_own++;
	}

	/* resolve them all (their var() against each other and the inherited) */
	for (k = 0; k < sc.n_own; k++)
		(void) onyx_lookup(&sc, sc.own_name[k], 0);

	/* unchanged from the parent's? then share the parent's */
	for (k = 0; k < sc.n_own; k++) {
		lwc_string *pv = onyx_vars_get(parent, sc.own_name[k]);
		if ((sc.own_state[k] == 2 && pv == sc.own_resolved[k]) ||
				(sc.own_state[k] != 2 && pv == NULL))
			same++;
	}
	if (same == sc.n_own) {
		*out = css__onyx_vars_ref(parent);
		goto done;
	}

	n = (parent != NULL ? parent->n : 0) + sc.n_own;
	vars = malloc(sizeof(css_onyx_vars) + n * sizeof(css_onyx_var));
	if (vars == NULL) {
		error = CSS_NOMEM;
		goto done;
	}
	vars->refcount = 1;
	vars->n = 0;
	for (i = 0; parent != NULL && i < parent->n; i++) {
		for (k = 0; k < sc.n_own; k++) {
			if (sc.own_name[k] == parent->v[i].name)
				break;
		}
		if (k < sc.n_own)
			continue;		/* overridden */
		vars->v[vars->n].name = lwc_string_ref(parent->v[i].name);
		vars->v[vars->n].value = lwc_string_ref(parent->v[i].value);
		vars->n++;
	}
	for (k = 0; k < sc.n_own; k++) {
		if (sc.own_state[k] != 2)
			continue;		/* invalid: no value */
		vars->v[vars->n].name = lwc_string_ref(sc.own_name[k]);
		vars->v[vars->n].value = lwc_string_ref(sc.own_resolved[k]);
		vars->n++;
	}
	*out = vars;

done:
	for (k = 0; sc.own_resolved != NULL && k < sc.n_own; k++) {
		if (sc.own_resolved[k] != NULL)
			lwc_string_unref(sc.own_resolved[k]);
	}
	free(sc.own_name);
	free(sc.own_value);
	free(sc.own_resolved);
	free(sc.own_state);
	return error;
}

/* ---- parsing a completed value: a small inline sheet, cached ------------------------------- */

typedef struct onyx_parsed {
	struct onyx_parsed *next;
	lwc_string *text;		/* the completed value */
	uint16_t prop;
	bool important;
	bool quirks;
	char *url;
	css_url_resolution_fn resolve;
	void *resolve_pw;
	css_color_resolution_fn color;
	css_font_resolution_fn font;
	css_stylesheet *sheet;
	const css_style *style;		/* its declaration's bytecode, or NULL: invalid */
} onyx_parsed;

#define ONYX_PARSED_BUCKETS 256
#define ONYX_PARSED_MAX 512
static onyx_parsed *onyx_parsed_table[ONYX_PARSED_BUCKETS];
static uint32_t onyx_parsed_count;

/* Documented in select/onyx_vars.h */
void css__onyx_parsed_flush(void)
{
	uint32_t b;
	for (b = 0; b < ONYX_PARSED_BUCKETS; b++) {
		onyx_parsed *e = onyx_parsed_table[b], *next;
		for (; e != NULL; e = next) {
			next = e->next;
			css_stylesheet_destroy(e->sheet);
			lwc_string_unref(e->text);
			free(e->url);
			free(e);
		}
		onyx_parsed_table[b] = NULL;
	}
	onyx_parsed_count = 0;
}

static bool onyx_same_url(const char *a, const char *b)
{
	if (a == NULL || b == NULL)
		return a == b;
	return strcmp(a, b) == 0;
}

/* The bytecode of "property: text" parsed in the context of `from` (NULL if invalid). */
static const css_style *onyx_parse(const css_stylesheet *from, uint16_t prop,
		bool important, lwc_string *text)
{
	uint32_t b = (uint32_t) (((uintptr_t) text >> 4) ^ (prop * 31u) ^ important) %
			ONYX_PARSED_BUCKETS;
	onyx_parsed *e;
	css_stylesheet_params params;
	css_stylesheet *sheet = NULL;
	lwc_string *name;
	char *src;
	size_t len;
	css_error error;

	for (e = onyx_parsed_table[b]; e != NULL; e = e->next) {
		if (e->text == text && e->prop == prop && e->important == important &&
				e->quirks == from->quirks_allowed &&
				e->resolve == from->resolve && e->resolve_pw == from->resolve_pw &&
				e->color == from->color && e->font == from->font &&
				onyx_same_url(e->url, from->url))
			return e->style;
	}

	if (onyx_parsed_count >= ONYX_PARSED_MAX)
		css__onyx_parsed_flush();

	name = from->propstrings[FIRST_PROP + prop];
	len = lwc_string_length(name) + 2 + lwc_string_length(text) + 12;
	src = malloc(len);
	e = calloc(1, sizeof(*e));
	if (src == NULL || e == NULL) {
		free(src);
		free(e);
		return NULL;
	}
	len = 0;
	memcpy(src + len, lwc_string_data(name), lwc_string_length(name));
	len += lwc_string_length(name);
	memcpy(src + len, ": ", 2);
	len += 2;
	memcpy(src + len, lwc_string_data(text), lwc_string_length(text));
	len += lwc_string_length(text);
	if (important) {
		memcpy(src + len, " !important", 11);
		len += 11;
	}

	memset(&params, 0, sizeof(params));
	params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
	params.level = CSS_LEVEL_DEFAULT;
	params.charset = "UTF-8";
	params.url = (from->url != NULL) ? from->url : "";
	params.allow_quirks = from->quirks_allowed;
	params.inline_style = true;
	params.resolve = from->resolve;
	params.resolve_pw = from->resolve_pw;
	params.color = from->color;
	params.color_pw = from->color_pw;
	params.font = from->font;
	params.font_pw = from->font_pw;

	error = css_stylesheet_create(&params, &sheet);
	if (error == CSS_OK) {
		error = css_stylesheet_append_data(sheet, (const uint8_t *) src, len);
		if (error == CSS_NEEDDATA)
			error = CSS_OK;
	}
	if (error == CSS_OK)
		error = css_stylesheet_data_done(sheet);
	free(src);
	if (error != CSS_OK) {
		if (sheet != NULL)
			css_stylesheet_destroy(sheet);
		free(e);
		return NULL;
	}

	e->text = lwc_string_ref(text);
	e->prop = prop;
	e->important = important;
	e->quirks = from->quirks_allowed;
	e->url = (from->url != NULL) ? strdup(from->url) : NULL;
	e->resolve = from->resolve;
	e->resolve_pw = from->resolve_pw;
	e->color = from->color;
	e->font = from->font;
	e->sheet = sheet;
	e->style = NULL;
	if (sheet->rule_list != NULL && sheet->rule_list->type == CSS_RULE_SELECTOR)
		e->style = ((css_rule_selector *) sheet->rule_list)->style;
	e->next = onyx_parsed_table[b];
	onyx_parsed_table[b] = e;
	onyx_parsed_count++;
	return e->style;
}

/* ---- completing the pending values --------------------------------------------------------- */

/* Complete pending declaration p (1 + index) for pseudo element `pseudo`, with `vars`. */
static css_error onyx_complete(css_select_state *state, uint16_t p, uint8_t pseudo,
		css_onyx_vars *vars)
{
	struct css_onyx_state *st = state->onyx;
	css_onyx_pending *pd = &st->pending[p - 1];
	onyx_scope sc;
	onyx_buf b = { NULL, 0, 0, false };
	const css_style *parsed = NULL;
	lwc_string *text = NULL;
	css_origin origin = state->current_origin;
	uint32_t specificity = state->current_specificity;
	css_pseudo_element cur_pseudo = state->current_pseudo;
	css_computed_style *computed = state->computed;
	css_error error = CSS_OK;
	uint32_t op;

	memset(&sc, 0, sizeof(sc));
	sc.resolved = vars;
	if (onyx_subst(&sc, lwc_string_data(pd->text), lwc_string_length(pd->text), 0, &b) &&
			!b.oom &&
			lwc_intern_string(b.p != NULL ? b.p : "", b.n, &text) == lwc_error_ok) {
		parsed = onyx_parse(pd->sheet, pd->prop, pd->important, text);
		lwc_string_unref(text);
	}
	free(b.p);

	state->current_origin = pd->origin;
	state->current_specificity = pd->specificity;
	state->current_pseudo = pseudo;
	state->computed = state->results->styles[pseudo];
	st->filter = p;

	if (parsed != NULL)
		error = css__select_cascade_style(parsed, state);

	/* what is still pending -- the value was invalid, or did not set it: unset */
	for (op = 0; error == CSS_OK && op < CSS_N_PROPERTIES; op++) {
		if (st->pending_of[op][pseudo] == p) {
			css_style empty;
			memset(&empty, 0, sizeof(empty));
			empty.sheet = (css_stylesheet *) pd->sheet;
			error = prop_dispatch[op].cascade(
					buildOPV((opcode_t) op, FLAG_UNSET |
						(pd->important ? FLAG_IMPORTANT : 0), 0),
					&empty, state);
			st->pending_of[op][pseudo] = 0;
		}
	}

	st->filter = 0;
	state->current_origin = origin;
	state->current_specificity = specificity;
	state->current_pseudo = cur_pseudo;
	state->computed = computed;
	return error;
}

/* Documented in select/onyx_vars.h */
css_error css__onyx_resolve(css_select_state *state, void *parent)
{
	struct css_onyx_state *st = state->onyx;
	struct css_node_data *parent_data = NULL;
	css_onyx_vars *parent_vars = NULL, *vars = NULL;
	css_error error;
	uint32_t j, op;

	if (parent != NULL) {
		error = state->handler->get_libcss_node_data(state->pw, parent,
				(void **) (void *) &parent_data);
		if (error == CSS_OK && parent_data != NULL)
			parent_vars = parent_data->onyx_vars;
	}

	/* the element's variables: kept in its node data, for its children */
	error = onyx_make_vars(st, CSS_PSEUDO_ELEMENT_NONE, parent_vars, &vars);
	if (error != CSS_OK)
		return error;
	if (state->node_data != NULL) {
		css__onyx_vars_unref(state->node_data->onyx_vars);
		state->node_data->onyx_vars = css__onyx_vars_ref(vars);
	}

	for (j = 0; st != NULL && st->n_pending > 0 && j < CSS_PSEUDO_ELEMENT_COUNT; j++) {
		css_onyx_vars *pvars = NULL;

		if (state->results->styles[j] == NULL)
			continue;
		if (j == CSS_PSEUDO_ELEMENT_NONE) {
			pvars = css__onyx_vars_ref(vars);
		} else {
			error = onyx_make_vars(st, j, vars, &pvars);
			if (error != CSS_OK)
				break;
		}
		for (op = 0; op < CSS_N_PROPERTIES; op++) {
			uint16_t p = st->pending_of[op][j];
			if (p != 0) {
				/* (completes all of p's longhands at once) */
				error = onyx_complete(state, p, j, pvars);
				if (error != CSS_OK)
					break;
			}
		}
		css__onyx_vars_unref(pvars);
		if (error != CSS_OK)
			break;
	}

	css__onyx_vars_unref(vars);
	return error;
}
