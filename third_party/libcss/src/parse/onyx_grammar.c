/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: the matcher of the CSS value grammars (onyx_grammar.h). A backtracking matcher in
 * continuation-passing style: matching a node at a position calls the continuation (what must
 * follow) for each way the node can match, until one leads to the whole value -- so every
 * combinator (juxtaposition, &&, ||, |, the multipliers) is matched exactly as the Value
 * Definition Syntax defines it, the omissible commas as the CSS Values spec says. The steps
 * are bounded (a pathological value is rejected, not a hang). The numeric primitives take
 * the math functions, type-checked by CSS's arithmetic rules (css__onyx_math_type).
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <libwapcaplet/libwapcaplet.h>

#include "parse/onyx_grammar.h"
#include "parse/language.h"

#define G_MAX_TOKENS 512
#define G_MAX_STEPS 200000

/* a token of the value (whitespace dropped); pair: the matching bracket's index, else -1
 * (n, the value's end, for a function left open at the end: CSS closes it there); rel: in
 * the arguments of a relative colour (rgb(from ...)), whose channel keywords are numbers */
typedef struct {
	const css_token *t;
	int32_t pair;
	uint8_t rel;
} gtok;

/* the position after the bracket closing at `close` (a function closed by the value's end
 * has none) */
static inline int g_after(int close, int n)
{
	return close >= n ? n : close + 1;
}

typedef struct {
	const gtok *tok;
	int n;
	unsigned steps;
} gm;

typedef struct gk gk;
typedef bool (*gk_fn)(gm *m, int pos, const gk *k);
struct gk {
	gk_fn fn;
	const gk *next;
	uint16_t node;
	uint16_t i;
	uint32_t aux;
	int32_t end;
};

static bool g_match(gm *m, uint16_t node, int pos, const gk *k);

/* ---- tokens ------------------------------------------------------------------------------ */

static const char *tdata(const css_token *t, size_t *len)
{
	if (t->idata == NULL) {
		*len = 0;
		return "";
	}
	*len = lwc_string_length(t->idata);
	return lwc_string_data(t->idata);
}

static bool is_char(const css_token *t, char c)
{
	size_t len;
	const char *d;
	if (t == NULL || t->type != CSS_TOKEN_CHAR)
		return false;
	d = tdata(t, &len);
	return len == 1 && d[0] == c;
}

static bool is_ident(const css_token *t, const char *s)
{
	size_t len;
	const char *d;
	if (t == NULL || t->type != CSS_TOKEN_IDENT)
		return false;
	d = tdata(t, &len);
	return strlen(s) == len && strncasecmp(d, s, len) == 0;
}

static bool is_function(const css_token *t, const char *s)
{
	size_t len;
	const char *d;
	if (t == NULL || t->type != CSS_TOKEN_FUNCTION)
		return false;
	d = tdata(t, &len);
	return strlen(s) == len && strncasecmp(d, s, len) == 0;
}

static bool is_open(const css_token *t)
{
	return t->type == CSS_TOKEN_FUNCTION || is_char(t, '(') || is_char(t, '[') ||
			is_char(t, '{');
}

static bool is_close(const css_token *t)
{
	return is_char(t, ')') || is_char(t, ']') || is_char(t, '}');
}

/* The tokens of [start, end) without whitespace, the brackets paired. -1: too many, or
 * unbalanced. */
static int g_tokens(const parserutils_vector *vector, int32_t start, int32_t end,
		gtok *out, int max)
{
	int32_t stack[64];
	int sp = 0, n = 0;
	int32_t i;

	for (i = start; end < 0 || i < end; i++) {
		const css_token *t = parserutils_vector_peek(vector, i);
		if (t == NULL)
			break;
		if (t->type == CSS_TOKEN_S || t->type == CSS_TOKEN_COMMENT)
			continue;
		if (t->type == CSS_TOKEN_EOF)
			break;
		if (n == max)
			return -1;
		out[n].t = t;
		out[n].pair = -1;
		out[n].rel = 0;
		if (is_open(t)) {
			if (sp == 64)
				return -1;
			stack[sp++] = n;
		} else if (is_close(t)) {
			if (sp == 0)
				return -1;
			sp--;
			out[n].pair = stack[sp];
			out[stack[sp]].pair = n;
		}
		n++;
	}
	/* (an unclosed function at the end of the value: closed there, as CSS does) */
	while (sp > 0)
		out[stack[--sp]].pair = n;

	/* the relative colours: rgb(from <color> r g b), lch(from ... l c h)... */
	for (i = 0; i < n; i++) {
		static const char *const fns[] = { "rgb", "rgba", "hsl", "hsla", "hwb", "lab",
				"lch", "oklab", "oklch", "color" };
		size_t k;
		if (out[i].t->type != CSS_TOKEN_FUNCTION || i + 1 >= n ||
				!is_ident(out[i + 1].t, "from"))
			continue;
		for (k = 0; k < sizeof(fns) / sizeof(fns[0]); k++) {
			if (is_function(out[i].t, fns[k])) {
				int32_t j;
				for (j = i + 1; j < out[i].pair && j < n; j++)
					out[j].rel = 1;
				break;
			}
		}
	}
	return n;
}

/* a relative colour's channel keyword (a number there) */
static bool g_channel(const gtok *g)
{
	static const char *const ch[] = { "r", "g", "b", "h", "s", "l", "w", "a", "c", "x",
			"y", "z", "alpha" };
	size_t k;
	if (!g->rel || g->t->type != CSS_TOKEN_IDENT)
		return false;
	for (k = 0; k < sizeof(ch) / sizeof(ch[0]); k++)
		if (is_ident(g->t, ch[k]))
			return true;
	return false;
}

/* ---- numbers and units ---------------------------------------------------------------------- */

static const struct { const char *u; uint8_t kind; } g_units[] = {
	{ "px", ONYX_MT_LENGTH }, { "cm", ONYX_MT_LENGTH }, { "mm", ONYX_MT_LENGTH },
	{ "q", ONYX_MT_LENGTH }, { "in", ONYX_MT_LENGTH }, { "pt", ONYX_MT_LENGTH },
	{ "pc", ONYX_MT_LENGTH }, { "em", ONYX_MT_LENGTH }, { "rem", ONYX_MT_LENGTH },
	{ "ex", ONYX_MT_LENGTH }, { "rex", ONYX_MT_LENGTH }, { "cap", ONYX_MT_LENGTH },
	{ "rcap", ONYX_MT_LENGTH }, { "ch", ONYX_MT_LENGTH }, { "rch", ONYX_MT_LENGTH },
	{ "ic", ONYX_MT_LENGTH }, { "ric", ONYX_MT_LENGTH }, { "lh", ONYX_MT_LENGTH },
	{ "rlh", ONYX_MT_LENGTH },
	{ "vw", ONYX_MT_LENGTH }, { "vh", ONYX_MT_LENGTH }, { "vi", ONYX_MT_LENGTH },
	{ "vb", ONYX_MT_LENGTH }, { "vmin", ONYX_MT_LENGTH }, { "vmax", ONYX_MT_LENGTH },
	{ "svw", ONYX_MT_LENGTH }, { "svh", ONYX_MT_LENGTH }, { "svi", ONYX_MT_LENGTH },
	{ "svb", ONYX_MT_LENGTH }, { "svmin", ONYX_MT_LENGTH }, { "svmax", ONYX_MT_LENGTH },
	{ "lvw", ONYX_MT_LENGTH }, { "lvh", ONYX_MT_LENGTH }, { "lvi", ONYX_MT_LENGTH },
	{ "lvb", ONYX_MT_LENGTH }, { "lvmin", ONYX_MT_LENGTH }, { "lvmax", ONYX_MT_LENGTH },
	{ "dvw", ONYX_MT_LENGTH }, { "dvh", ONYX_MT_LENGTH }, { "dvi", ONYX_MT_LENGTH },
	{ "dvb", ONYX_MT_LENGTH }, { "dvmin", ONYX_MT_LENGTH }, { "dvmax", ONYX_MT_LENGTH },
	{ "cqw", ONYX_MT_LENGTH }, { "cqh", ONYX_MT_LENGTH }, { "cqi", ONYX_MT_LENGTH },
	{ "cqb", ONYX_MT_LENGTH }, { "cqmin", ONYX_MT_LENGTH }, { "cqmax", ONYX_MT_LENGTH },
	{ "deg", ONYX_MT_ANGLE }, { "grad", ONYX_MT_ANGLE }, { "rad", ONYX_MT_ANGLE },
	{ "turn", ONYX_MT_ANGLE },
	{ "s", ONYX_MT_TIME }, { "ms", ONYX_MT_TIME },
	{ "hz", ONYX_MT_FREQ }, { "khz", ONYX_MT_FREQ },
	{ "dpi", ONYX_MT_RES }, { "dpcm", ONYX_MT_RES }, { "dppx", ONYX_MT_RES },
	{ "x", ONYX_MT_RES },
	{ "fr", ONYX_MT_FLEX },
};

/* Documented in onyx_grammar.h */
int css__onyx_unit_kind(const char *unit, size_t len)
{
	size_t i;
	for (i = 0; i < sizeof(g_units) / sizeof(g_units[0]); i++) {
		if (strlen(g_units[i].u) == len && strncasecmp(g_units[i].u, unit, len) == 0)
			return g_units[i].kind;
	}
	return ONYX_MT_BAD;
}

/* A numeric token's value, and where its unit starts. */
static double g_number(const css_token *t, size_t *consumed)
{
	size_t len, i = 0;
	const char *d = tdata(t, &len);
	char buf[64];
	double v;
	char *e;

	/* the number: [+-]? digits [. digits]? [e [+-]? digits]? */
	if (i < len && (d[i] == '+' || d[i] == '-'))
		i++;
	while (i < len && d[i] >= '0' && d[i] <= '9')
		i++;
	if (i + 1 < len && d[i] == '.' && d[i + 1] >= '0' && d[i + 1] <= '9') {
		i++;
		while (i < len && d[i] >= '0' && d[i] <= '9')
			i++;
	}
	if (i + 1 < len && (d[i] == 'e' || d[i] == 'E')) {
		size_t j = i + 1;
		if (j < len && (d[j] == '+' || d[j] == '-'))
			j++;
		if (j < len && d[j] >= '0' && d[j] <= '9') {
			while (j < len && d[j] >= '0' && d[j] <= '9')
				j++;
			i = j;
		}
	}
	if (i >= sizeof(buf))
		i = sizeof(buf) - 1;
	memcpy(buf, d, i);
	buf[i] = '\0';
	v = strtod(buf, &e);
	*consumed = i;
	return v;
}

static bool g_is_integer(const css_token *t)
{
	size_t len, i;
	const char *d = tdata(t, &len);
	for (i = 0; i < len; i++) {
		if (d[i] == '.' || d[i] == 'e' || d[i] == 'E')
			return false;
	}
	return true;
}

/* ---- the math functions: their type -------------------------------------------------------- */

static const char *const g_math_fns[] = {
	"calc", "min", "max", "clamp", "round", "mod", "rem", "sin", "cos", "tan", "asin",
	"acos", "atan", "atan2", "pow", "sqrt", "hypot", "log", "exp", "abs", "sign",
	"-webkit-calc"
};

/* Documented in onyx_grammar.h */
bool css__onyx_is_math_function(const css_token *t)
{
	size_t i;
	for (i = 0; i < sizeof(g_math_fns) / sizeof(g_math_fns[0]); i++) {
		if (is_function(t, g_math_fns[i]))
			return true;
	}
	return false;
}

typedef struct {
	const gtok *tok;
	int end;		/* the ')' of the function being read */
	int pct;
	int n;			/* the value's end */
	bool size_kw;		/* calc-size(): the keyword size is a length */
} gmath;

static int gm_sum(gmath *x, int *i);
static int gm_fn(gmath *x, int i, int *next);

static int gm_value(gmath *x, int *i)
{
	const css_token *t;
	size_t used, len;
	const char *d;
	int r;

	if (*i >= x->end)
		return ONYX_MT_BAD;
	t = x->tok[*i].t;
	switch (t->type) {
	case CSS_TOKEN_NUMBER:
		(*i)++;
		return ONYX_MT_NUMBER;
	case CSS_TOKEN_PERCENTAGE:
		(*i)++;
		return x->pct;
	case CSS_TOKEN_DIMENSION:
		g_number(t, &used);
		d = tdata(t, &len);
		(*i)++;
		return css__onyx_unit_kind(d + used, len - used);
	case CSS_TOKEN_IDENT:
		if (is_ident(t, "e") || is_ident(t, "pi") || is_ident(t, "infinity") ||
				is_ident(t, "-infinity") || is_ident(t, "nan") ||
				g_channel(&x->tok[*i])) {
			(*i)++;
			return ONYX_MT_NUMBER;
		}
		if (x->size_kw && is_ident(t, "size")) {
			(*i)++;
			return ONYX_MT_LENGTH;
		}
		return ONYX_MT_BAD;
	case CSS_TOKEN_FUNCTION:
		r = gm_fn(x, *i, i);
		return r;
	default:
		break;
	}
	if (is_char(t, '(')) {
		int close = x->tok[*i].pair, save = x->end;
		(*i)++;
		x->end = close;
		r = gm_sum(x, i);
		x->end = save;
		if (r == ONYX_MT_BAD || *i != close)
			return ONYX_MT_BAD;
		*i = g_after(close, x->n);
		return r;
	}
	return ONYX_MT_BAD;
}

static int gm_product(gmath *x, int *i)
{
	int a = gm_value(x, i);
	while (a != ONYX_MT_BAD && *i < x->end) {
		const css_token *t = x->tok[*i].t;
		int b;
		if (is_char(t, '*')) {
			(*i)++;
			b = gm_value(x, i);
			if (b == ONYX_MT_BAD)
				return ONYX_MT_BAD;
			if (a == ONYX_MT_NUMBER)
				a = b;
			else if (b != ONYX_MT_NUMBER)
				return ONYX_MT_BAD;	/* (length * length: not a CSS value) */
		} else if (is_char(t, '/')) {
			(*i)++;
			b = gm_value(x, i);
			if (b != ONYX_MT_NUMBER)
				return ONYX_MT_BAD;
		} else {
			break;
		}
	}
	return a;
}

static int gm_sum(gmath *x, int *i)
{
	int a = gm_product(x, i);
	while (a != ONYX_MT_BAD && *i < x->end) {
		const css_token *t = x->tok[*i].t;
		int b;
		if (!is_char(t, '+') && !is_char(t, '-'))
			break;
		(*i)++;
		b = gm_product(x, i);
		if (b != a)
			return ONYX_MT_BAD;
	}
	return a;
}

/* the comma separated arguments of a function: their types in ty (up to max); the count, or
 * -1. An argument may be one of the keywords kws (its type then ONYX_MT_BAD - 1). */
#define GM_KW (ONYX_MT_BAD - 1)
static int gm_args(gmath *x, int *i, int *ty, int max, const char *const *kws)
{
	int n = 0;
	for (;;) {
		const css_token *t;
		int k, r = ONYX_MT_BAD;
		if (*i >= x->end || n == max)
			return -1;
		t = x->tok[*i].t;
		for (k = 0; kws != NULL && kws[k] != NULL; k++) {
			if (is_ident(t, kws[k])) {
				r = GM_KW;
				(*i)++;
				break;
			}
		}
		if (r != GM_KW) {
			r = gm_sum(x, i);
			if (r == ONYX_MT_BAD)
				return -1;
		}
		ty[n++] = r;
		if (*i == x->end)
			return n;
		if (!is_char(x->tok[*i].t, ','))
			return -1;
		(*i)++;
	}
}

static int gm_fn(gmath *x, int i, int *next)
{
	static const char *const rounding[] = { "nearest", "up", "down", "to-zero", NULL };
	static const char *const none[] = { "none", NULL };
	const css_token *t = x->tok[i].t;
	int close = x->tok[i].pair, save = x->end, ty[32], n, k, r = ONYX_MT_BAD;
	int p = i + 1;

	if (close < 0 || close > save)
		return ONYX_MT_BAD;
	x->end = close;
	if (is_function(t, "calc") || is_function(t, "-webkit-calc")) {
		r = gm_sum(x, &p);
		if (p != close)
			r = ONYX_MT_BAD;
	} else if (is_function(t, "min") || is_function(t, "max") || is_function(t, "hypot")) {
		n = gm_args(x, &p, ty, 32, NULL);
		r = n > 0 ? ty[0] : ONYX_MT_BAD;
		for (k = 1; k < n; k++)
			if (ty[k] != r)
				r = ONYX_MT_BAD;
	} else if (is_function(t, "clamp")) {
		n = gm_args(x, &p, ty, 3, none);
		if (n == 3 && ty[1] != GM_KW) {
			r = ty[1];
			if ((ty[0] != GM_KW && ty[0] != r) || (ty[2] != GM_KW && ty[2] != r))
				r = ONYX_MT_BAD;
		}
	} else if (is_function(t, "round")) {
		n = gm_args(x, &p, ty, 3, rounding);
		k = (n > 0 && ty[0] == GM_KW) ? 1 : 0;
		if (n - k == 1 && ty[k] != GM_KW)
			r = ty[k];
		else if (n - k == 2 && ty[k] != GM_KW && ty[k] == ty[k + 1])
			r = ty[k];
	} else if (is_function(t, "mod") || is_function(t, "rem") || is_function(t, "atan2")) {
		n = gm_args(x, &p, ty, 2, NULL);
		if (n == 2 && ty[0] == ty[1])
			r = is_function(t, "atan2") ? ONYX_MT_ANGLE : ty[0];
	} else if (is_function(t, "sin") || is_function(t, "cos") || is_function(t, "tan")) {
		n = gm_args(x, &p, ty, 1, NULL);
		if (n == 1 && (ty[0] == ONYX_MT_NUMBER || ty[0] == ONYX_MT_ANGLE))
			r = ONYX_MT_NUMBER;
	} else if (is_function(t, "asin") || is_function(t, "acos") || is_function(t, "atan")) {
		n = gm_args(x, &p, ty, 1, NULL);
		if (n == 1 && ty[0] == ONYX_MT_NUMBER)
			r = ONYX_MT_ANGLE;
	} else if (is_function(t, "pow")) {
		n = gm_args(x, &p, ty, 2, NULL);
		if (n == 2 && ty[0] == ONYX_MT_NUMBER && ty[1] == ONYX_MT_NUMBER)
			r = ONYX_MT_NUMBER;
	} else if (is_function(t, "sqrt") || is_function(t, "exp")) {
		n = gm_args(x, &p, ty, 1, NULL);
		if (n == 1 && ty[0] == ONYX_MT_NUMBER)
			r = ONYX_MT_NUMBER;
	} else if (is_function(t, "log")) {
		n = gm_args(x, &p, ty, 2, NULL);
		if (n >= 1 && ty[0] == ONYX_MT_NUMBER && (n == 1 || ty[1] == ONYX_MT_NUMBER))
			r = ONYX_MT_NUMBER;
	} else if (is_function(t, "abs")) {
		n = gm_args(x, &p, ty, 1, NULL);
		if (n == 1)
			r = ty[0];
	} else if (is_function(t, "sign")) {
		n = gm_args(x, &p, ty, 1, NULL);
		if (n == 1)
			r = ONYX_MT_NUMBER;
	}
	x->end = save;
	if (r == GM_KW)
		r = ONYX_MT_BAD;
	*next = g_after(close, x->n);
	return r;
}

/* the type of the math function at tok[i]; *next: past its ')' */
static int g_math_type_at(const gtok *tok, int n, int i, int pct, int *next)
{
	gmath x;
	x.tok = tok;
	x.end = n;
	x.pct = pct;
	x.n = n;
	x.size_kw = false;
	if (tok[i].pair < 0)
		return ONYX_MT_BAD;
	return gm_fn(&x, i, next);
}

/* Documented in onyx_grammar.h */
int css__onyx_math_type(const parserutils_vector *vector, int32_t start, int32_t end, int pct)
{
	gtok tok[G_MAX_TOKENS];
	int n = g_tokens(vector, start, end, tok, G_MAX_TOKENS), next = 0, r;

	if (n <= 0 || !css__onyx_is_math_function(tok[0].t))
		return ONYX_MT_BAD;
	r = g_math_type_at(tok, n, 0, pct, &next);
	return next == n ? r : ONYX_MT_BAD;
}

/* ---- the primitives -------------------------------------------------------------------------- */

static bool g_in_range(uint16_t range, double v)
{
	if (range == 0)
		return true;
	return v >= onyx_grammar_ranges[range][0] && v <= onyx_grammar_ranges[range][1];
}

/* a numeric primitive: the type it wants, what a % stands for */
static bool g_numeric(uint16_t prim, int *want, int *pct)
{
	switch (prim) {
	case ONYX_P_LENGTH: *want = ONYX_MT_LENGTH; *pct = ONYX_MT_BAD; return true;
	case ONYX_P_PERCENTAGE: *want = ONYX_MT_PERCENT; *pct = ONYX_MT_PERCENT; return true;
	case ONYX_P_LENGTH_PERCENTAGE: *want = ONYX_MT_LENGTH; *pct = ONYX_MT_LENGTH; return true;
	case ONYX_P_NUMBER:
	case ONYX_P_INTEGER:
	case ONYX_P_ZERO:
	case ONYX_P_RATIO_NUMBER: *want = ONYX_MT_NUMBER; *pct = ONYX_MT_BAD; return true;
	case ONYX_P_NUMBER_PERCENTAGE: *want = ONYX_MT_NUMBER; *pct = ONYX_MT_NUMBER; return true;
	case ONYX_P_ANGLE: *want = ONYX_MT_ANGLE; *pct = ONYX_MT_BAD; return true;
	case ONYX_P_ANGLE_PERCENTAGE: *want = ONYX_MT_ANGLE; *pct = ONYX_MT_ANGLE; return true;
	case ONYX_P_TIME: *want = ONYX_MT_TIME; *pct = ONYX_MT_BAD; return true;
	case ONYX_P_TIME_PERCENTAGE: *want = ONYX_MT_TIME; *pct = ONYX_MT_TIME; return true;
	case ONYX_P_FREQUENCY: *want = ONYX_MT_FREQ; *pct = ONYX_MT_BAD; return true;
	case ONYX_P_FREQUENCY_PERCENTAGE: *want = ONYX_MT_FREQ; *pct = ONYX_MT_FREQ; return true;
	case ONYX_P_RESOLUTION: *want = ONYX_MT_RES; *pct = ONYX_MT_BAD; return true;
	case ONYX_P_FLEX: *want = ONYX_MT_FLEX; *pct = ONYX_MT_BAD; return true;
	default: return false;
	}
}

static bool g_css_wide(const css_token *t)
{
	return is_ident(t, "initial") || is_ident(t, "inherit") || is_ident(t, "unset") ||
			is_ident(t, "revert") || is_ident(t, "revert-layer") ||
			is_ident(t, "default");
}

static bool g_prim(gm *m, const onyx_gnode *nd, int pos, const gk *k)
{
	const css_token *t;
	size_t len, used;
	const char *d;
	int want, pct, i;

	if (nd->a == ONYX_P_ANY_VALUE || nd->a == ONYX_P_DECLARATION_VALUE) {
		/* one or more tokens, brackets balanced: the longest first */
		int ends[G_MAX_TOKENS], ne = 0;
		for (i = pos; i < m->n; ) {
			t = m->tok[i].t;
			if (is_close(t) && m->tok[i].pair < pos)
				break;		/* (the enclosing function's end) */
			if (nd->a == ONYX_P_DECLARATION_VALUE &&
					(is_char(t, ';') || is_char(t, '!')))
				break;
			if (is_open(t))
				i = g_after(m->tok[i].pair, m->n);
			else
				i++;
			ends[ne++] = i;
		}
		while (ne > 0) {
			if (k->fn(m, ends[--ne], k))
				return true;
		}
		return false;
	}
	if (nd->a == ONYX_P_CALC_SUM) {
		/* a calculation (calc-size()'s, ...): to the next ',' or the function's end */
		gmath x;
		int end, p = pos, r;
		for (end = pos; end < m->n; ) {
			t = m->tok[end].t;
			if (is_char(t, ',') || (is_close(t) && m->tok[end].pair < pos))
				break;
			end = is_open(t) ? g_after(m->tok[end].pair, m->n) : end + 1;
		}
		if (end == pos)
			return false;
		x.tok = m->tok;
		x.end = end;
		x.pct = ONYX_MT_LENGTH;
		x.n = m->n;
		x.size_kw = true;
		r = gm_sum(&x, &p);
		if (r == ONYX_MT_BAD || p != end)
			return false;
		return k->fn(m, end, k);
	}
	if (pos >= m->n)
		return false;
	t = m->tok[pos].t;

	if (g_numeric(nd->a, &want, &pct)) {
		if (g_channel(&m->tok[pos]) && want == ONYX_MT_NUMBER)
			return k->fn(m, pos + 1, k);
		if (t->type == CSS_TOKEN_FUNCTION && css__onyx_is_math_function(t)) {
			int next, r = g_math_type_at(m->tok, m->n, pos, pct, &next);
			if (r == ONYX_MT_BAD || r != want)
				return false;
			if (nd->a == ONYX_P_ZERO)
				return false;
			return k->fn(m, next, k);
		}
		switch (t->type) {
		case CSS_TOKEN_NUMBER: {
			double v = g_number(t, &used);
			if (want == ONYX_MT_NUMBER) {
				if (nd->a == ONYX_P_INTEGER && !g_is_integer(t))
					return false;
				if (nd->a == ONYX_P_ZERO && v != 0)
					return false;
				if (!g_in_range(nd->b, v))
					return false;
				return k->fn(m, pos + 1, k);
			}
			/* a unitless zero is a length */
			if (want == ONYX_MT_LENGTH && v == 0)
				return k->fn(m, pos + 1, k);
			return false;
		}
		case CSS_TOKEN_PERCENTAGE: {
			double v = g_number(t, &used);
			if (pct == ONYX_MT_BAD || !g_in_range(nd->b, v))
				return false;
			return k->fn(m, pos + 1, k);
		}
		case CSS_TOKEN_DIMENSION: {
			double v = g_number(t, &used);
			d = tdata(t, &len);
			if (css__onyx_unit_kind(d + used, len - used) != want ||
					want == ONYX_MT_NUMBER || !g_in_range(nd->b, v))
				return false;
			return k->fn(m, pos + 1, k);
		}
		default:
			return false;
		}
	}

	switch (nd->a) {
	case ONYX_P_DIMENSION:
		if (t->type != CSS_TOKEN_DIMENSION)
			return false;
		break;
	case ONYX_P_STRING:
		if (t->type != CSS_TOKEN_STRING)
			return false;
		break;
	case ONYX_P_URL:
		if (t->type == CSS_TOKEN_URI)
			break;
		if ((is_function(t, "url") || is_function(t, "src")) && pos + 2 < m->n &&
				m->tok[pos + 1].t->type == CSS_TOKEN_STRING &&
				m->tok[pos].pair >= pos + 2) {
			/* url( <string> <url-modifier>* ): the modifiers taken as they come */
			return k->fn(m, g_after(m->tok[pos].pair, m->n), k);
		}
		return false;
	case ONYX_P_IDENT:
		if (t->type != CSS_TOKEN_IDENT)
			return false;
		break;
	case ONYX_P_CUSTOM_IDENT:
		if (t->type != CSS_TOKEN_IDENT || g_css_wide(t))
			return false;
		break;
	case ONYX_P_DASHED_IDENT:
	case ONYX_P_CUSTOM_PROPERTY_NAME:
		d = tdata(t, &len);
		if (t->type != CSS_TOKEN_IDENT || len < 3 || d[0] != '-' || d[1] != '-')
			return false;
		break;
	case ONYX_P_HEX_COLOR:
		d = tdata(t, &len);
		if (t->type != CSS_TOKEN_HASH || !(len == 3 || len == 4 || len == 6 || len == 8))
			return false;
		for (used = 0; used < len; used++) {
			char c = d[used];
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
					(c >= 'A' && c <= 'F')))
				return false;
		}
		break;
	case ONYX_P_ID:
		if (t->type != CSS_TOKEN_HASH)
			return false;
		break;
	case ONYX_P_URANGE:
	case ONYX_P_UNICODE_RANGE_TOKEN:
		if (t->type != CSS_TOKEN_UNICODE_RANGE)
			return false;
		break;
	case ONYX_P_DECIBEL:
	case ONYX_P_SEMITONES:
		if (t->type != CSS_TOKEN_DIMENSION)
			return false;
		g_number(t, &used);
		d = tdata(t, &len);
		if (len - used != 2 || strncasecmp(d + used,
				nd->a == ONYX_P_DECIBEL ? "db" : "st", 2) != 0)
			return false;
		break;
	case ONYX_P_FUNCTION_TOKEN:
		if (t->type != CSS_TOKEN_FUNCTION)
			return false;
		break;
	case ONYX_P_DASHED_FUNCTION:
		d = tdata(t, &len);
		if (t->type != CSS_TOKEN_FUNCTION || len < 3 || d[0] != '-' || d[1] != '-' ||
				m->tok[pos].pair < 0)
			return false;
		return k->fn(m, g_after(m->tok[pos].pair, m->n), k);
	default:
		return false;	/* (an+b, ...: not in a value) */
	}
	return k->fn(m, pos + 1, k);
}

/* ---- the combinators --------------------------------------------------------------------- */

static bool g_seq_step(gm *m, int pos, const gk *k)
{
	const onyx_gnode *nd = &onyx_grammar_nodes[k->node];
	gk k2;
	if (k->i == nd->b)
		return k->next->fn(m, pos, k->next);
	k2 = *k;
	k2.fn = g_seq_step;
	k2.i = k->i + 1;
	return g_match(m, onyx_grammar_kids[nd->a + k->i], pos, &k2);
}

static bool g_set_step(gm *m, int pos, const gk *k)
{
	const onyx_gnode *nd = &onyx_grammar_nodes[k->node];
	uint32_t mask = k->aux, full = (1u << nd->b) - 1;
	uint16_t i;
	for (i = 0; i < nd->b; i++) {
		gk k2;
		if (mask & (1u << i))
			continue;
		k2 = *k;
		k2.aux = mask | (1u << i);
		if (g_match(m, onyx_grammar_kids[nd->a + i], pos, &k2))
			return true;
	}
	if (nd->op == ONYX_G_ANY ? mask != 0 : mask == full)
		return k->next->fn(m, pos, k->next);
	return false;
}

static bool g_mult_step(gm *m, int pos, const gk *k)
{
	const onyx_gnode *nd = &onyx_grammar_nodes[k->node];
	uint32_t count = k->aux;
	bool progressed = (count == 0) || pos != k->end;

	if (progressed && (nd->c == 0xffff || count < nd->c)) {
		int p = pos;
		bool ok = true;
		if ((nd->flags & 1) && count > 0) {
			if (p < m->n && is_char(m->tok[p].t, ','))
				p++;
			else
				ok = false;
		}
		if (ok) {
			gk k2 = *k;
			k2.aux = count + 1;
			k2.end = p;
			if (g_match(m, nd->a, p, &k2))
				return true;
		}
	}
	if (count >= nd->b)
		return k->next->fn(m, pos, k->next);
	return false;
}

static bool g_func_end(gm *m, int pos, const gk *k)
{
	if (pos != k->end)
		return false;
	return k->next->fn(m, g_after(pos, m->n), k->next);
}

static bool g_bang_end(gm *m, int pos, const gk *k)
{
	if (pos == k->end)
		return false;
	return k->next->fn(m, pos, k->next);
}

static bool g_match(gm *m, uint16_t node, int pos, const gk *k)
{
	const onyx_gnode *nd;
	const css_token *t;
	gk k2;

	if (node == 0xffff || ++m->steps > G_MAX_STEPS)
		return false;
	nd = &onyx_grammar_nodes[node];
	t = pos < m->n ? m->tok[pos].t : NULL;

	switch (nd->op) {
	case ONYX_G_KW: {
		size_t len;
		const char *d;
		const char *kw = onyx_grammar_strings + nd->a;
		if (t == NULL || t->type != CSS_TOKEN_IDENT)
			return false;
		d = tdata(t, &len);
		if (strlen(kw) != len || strncasecmp(d, kw, len) != 0)
			return false;
		return k->fn(m, pos + 1, k);
	}
	case ONYX_G_NUM: {
		size_t len;
		const char *d;
		const char *lit = onyx_grammar_strings + nd->a;
		if (t == NULL || (t->type != CSS_TOKEN_NUMBER && t->type != CSS_TOKEN_DIMENSION))
			return false;
		d = tdata(t, &len);
		if (strlen(lit) != len || strncasecmp(d, lit, len) != 0)
			return false;
		return k->fn(m, pos + 1, k);
	}
	case ONYX_G_CHAR:
		if (nd->a == ',') {
			/* omissible where nothing precedes it (in its function / the value) or
			 * nothing follows it (CSS Values 4, 2.6) */
			const css_token *prev = pos > 0 ? m->tok[pos - 1].t : NULL;
			bool start = prev == NULL || is_char(prev, ',') || is_open(prev);
			bool end = t == NULL || is_close(t);
			if (t != NULL && is_char(t, ','))
				return start ? false : k->fn(m, pos + 1, k);
			if (start || end)
				return k->fn(m, pos, k);
			return false;
		}
		if (t == NULL || !is_char(t, (char) nd->a))
			return false;
		return k->fn(m, pos + 1, k);
	case ONYX_G_PRIM:
		return g_prim(m, nd, pos, k);
	case ONYX_G_SEQ:
		memset(&k2, 0, sizeof(k2));
		k2.fn = g_seq_step;
		k2.next = k;
		k2.node = node;
		k2.i = 0;
		return g_seq_step(m, pos, &k2);
	case ONYX_G_ALL:
	case ONYX_G_ANY:
		memset(&k2, 0, sizeof(k2));
		k2.fn = g_set_step;
		k2.next = k;
		k2.node = node;
		k2.aux = 0;
		return g_set_step(m, pos, &k2);
	case ONYX_G_ONE: {
		uint16_t i;
		for (i = 0; i < nd->b; i++) {
			if (g_match(m, onyx_grammar_kids[nd->a + i], pos, k))
				return true;
		}
		return false;
	}
	case ONYX_G_MULT:
		memset(&k2, 0, sizeof(k2));
		k2.fn = g_mult_step;
		k2.next = k;
		k2.node = node;
		k2.aux = 0;
		k2.end = pos;
		return g_mult_step(m, pos, &k2);
	case ONYX_G_BANG:
		memset(&k2, 0, sizeof(k2));
		k2.fn = g_bang_end;
		k2.next = k;
		k2.end = pos;
		return g_match(m, nd->a, pos, &k2);
	case ONYX_G_FUNC: {
		size_t len;
		const char *d;
		const char *name = onyx_grammar_strings + nd->a;
		int close;
		if (t == NULL || t->type != CSS_TOKEN_FUNCTION)
			return false;
		d = tdata(t, &len);
		if (strlen(name) != len || strncasecmp(d, name, len) != 0)
			return false;
		close = m->tok[pos].pair;
		if (close < 0)
			return false;
		if (nd->b == 0xffff)
			return close == pos + 1 ? k->fn(m, g_after(close, m->n), k) : false;
		memset(&k2, 0, sizeof(k2));
		k2.fn = g_func_end;
		k2.next = k;
		k2.end = close;
		return g_match(m, nd->b, pos + 1, &k2);
	}
	case ONYX_G_REF:
		return nd->a == 0xffff ? false : g_match(m, nd->a, pos, k);
	default:
		return false;
	}
}

static bool g_final(gm *m, int pos, const gk *k)
{
	(void) k;
	return pos == m->n;
}

/* Documented in onyx_grammar.h */
bool css__onyx_grammar_match(uint16_t root, const parserutils_vector *vector,
		int32_t start, int32_t end)
{
	gtok *tok;
	gm m;
	gk k;
	bool r;

	tok = malloc(G_MAX_TOKENS * sizeof(gtok));
	if (tok == NULL)
		return false;
	m.n = g_tokens(vector, start, end, tok, G_MAX_TOKENS);
	m.tok = tok;
	m.steps = 0;
	if (m.n <= 0) {
		free(tok);
		return false;
	}
	memset(&k, 0, sizeof(k));
	k.fn = g_final;
	r = g_match(&m, root, 0, &k);
	free(tok);
	return r;
}

/* ---- the tables' lookups ---------------------------------------------------------------------- */

static int g_find(const onyx_gname *table, unsigned n, const char *name, size_t len)
{
	char buf[96];
	size_t i;
	int lo = 0, hi = (int) n - 1;

	if (len == 0 || len >= sizeof(buf))
		return -1;
	for (i = 0; i < len; i++) {
		char c = name[i];
		buf[i] = (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
	}
	buf[len] = '\0';
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		int c = strcmp(onyx_grammar_strings + table[mid].name, buf);
		if (c == 0)
			return mid;
		if (c < 0)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

/* Documented in onyx_grammar.h */
int css__onyx_grammar_property(const char *name, size_t len)
{
	return g_find(onyx_grammar_props, onyx_grammar_nprops, name, len);
}

/* Documented in onyx_grammar.h */
int css__onyx_grammar_descriptor(const char *rule_desc, size_t len)
{
	return g_find(onyx_grammar_descs, onyx_grammar_ndescs, rule_desc, len);
}
