/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 *
 * Onyx: calc() / min() / max() / clamp() for the properties whose computed style keeps no
 * calc expression (all but width, which libcss resolves at layout time): the expression is
 * folded when its declaration is cascaded. The absolute units, rem and the viewport units
 * become px then (the unit context is known); em (and ex / ch / lh, as em) and % stay, each
 * as its own term. The result must be a single term -- px, em, % or a number: a mix such as
 * calc(100% - 2rem) cannot be folded now, and the declaration is then ignored (as libcss
 * ignored every calc() outside width).
 */

#include <string.h>

#include "bytecode/bytecode.h"
#include "stylesheet.h"
#include "select/select.h"
#include "select/onyx_calc.h"
#include "utils/utils.h"

enum { K_PX = 1, K_EM = 2, K_PCT = 4, K_NUM = 8 };

typedef struct {
	css_fixed px, em, pct, num;
	uint8_t k;			/* the terms present (K_*) */
} onyx_lin;

#define ONYX_CALC_STACK 32

/* One value of the expression: v of unit u as a term. */
static bool onyx_calc_term(const css_select_state *state, css_fixed v, uint32_t u,
		onyx_lin *out)
{
	const css_unit_ctx *ctx = state->unit_ctx;

	memset(out, 0, sizeof(*out));
	if (u == UNIT_PCT) {
		out->pct = v;
		out->k = K_PCT;
		return true;
	}
	if (u == UNIT_CALC_NUMBER) {
		out->num = v;
		out->k = K_NUM;
		return true;
	}
	if ((u & UNIT_LENGTH) == 0)
		return false;		/* angles, times, ...: not in a length */

	out->k = K_PX;
	switch (u) {
	case UNIT_EM: out->em = v; out->k = K_EM; break;
	case UNIT_EX: out->em = FMUL(v, FLTTOFIX(0.6)); out->k = K_EM; break;
	case UNIT_CH: out->em = FMUL(v, FLTTOFIX(0.4)); out->k = K_EM; break;
	case UNIT_LH: out->em = FMUL(v, FLTTOFIX(1.2)); out->k = K_EM; break;
	case UNIT_PX: out->px = v; break;
	case UNIT_IN: out->px = FMUL(v, F_96); break;
	case UNIT_CM: out->px = FDIV(FMUL(v, F_96), FLTTOFIX(2.54)); break;
	case UNIT_MM: out->px = FDIV(FMUL(v, F_96), FLTTOFIX(25.4)); break;
	case UNIT_Q:  out->px = FDIV(FMUL(v, F_96), FLTTOFIX(101.6)); break;
	case UNIT_PT: out->px = FDIV(FMUL(v, F_96), F_72); break;
	case UNIT_PC: out->px = FMUL(v, INTTOFIX(16)); break;
	case UNIT_REM:
		out->px = FMUL(v, ctx != NULL ?
				css_unit_len2css_px(NULL, ctx, F_1, CSS_UNIT_REM) :
				INTTOFIX(16));
		break;
	case UNIT_VW:
	case UNIT_VI:
	case UNIT_VH:
	case UNIT_VB:
	case UNIT_VMIN:
	case UNIT_VMAX: {
		css_fixed vw, vh, base;
		if (ctx == NULL)
			return false;
		vw = ctx->viewport_width;
		vh = ctx->viewport_height;
		if (u == UNIT_VW || u == UNIT_VI)
			base = vw;
		else if (u == UNIT_VH || u == UNIT_VB)
			base = vh;
		else if (u == UNIT_VMIN)
			base = (vw < vh) ? vw : vh;
		else
			base = (vw > vh) ? vw : vh;
		out->px = FDIV(FMUL(v, base), F_100);
		break;
	}
	default:
		return false;
	}
	return true;
}

static void onyx_calc_scale(onyx_lin *a, css_fixed f, bool divide)
{
	if (divide) {
		a->px = FDIV(a->px, f);
		a->em = FDIV(a->em, f);
		a->pct = FDIV(a->pct, f);
		a->num = FDIV(a->num, f);
	} else {
		a->px = FMUL(a->px, f);
		a->em = FMUL(a->em, f);
		a->pct = FMUL(a->pct, f);
		a->num = FMUL(a->num, f);
	}
}

/* The terms present, zero ones dropped (but for a value that is all zero). */
static uint8_t onyx_calc_kinds(const onyx_lin *a)
{
	uint8_t k = a->k;
	if ((k & K_PX) && a->px == 0 && k != K_PX)
		k &= ~K_PX;
	if ((k & K_EM) && a->em == 0 && k != K_EM)
		k &= ~K_EM;
	if ((k & K_PCT) && a->pct == 0 && k != K_PCT)
		k &= ~K_PCT;
	return k;
}

/* The single term of a (its kind in *k), for a comparison; false if a has several. */
static bool onyx_calc_single(const onyx_lin *a, uint8_t *k, css_fixed *v)
{
	*k = onyx_calc_kinds(a);
	switch (*k) {
	case K_PX:  *v = a->px; return true;
	case K_EM:  *v = a->em; return true;
	case K_PCT: *v = a->pct; return true;
	case K_NUM: *v = a->num; return true;
	default:    return false;
	}
}

/* Evaluate the expression (the stack machine of css__parse_calc: select/calc.c's). */
static css_error onyx_calc_eval(const css_select_state *state, lwc_string *expr,
		onyx_lin *result)
{
	onyx_lin stack[ONYX_CALC_STACK];
	int sp = 0;
	const css_code_t *codeptr = (const css_code_t *)(const void *)lwc_string_data(expr);

	while (*codeptr != CALC_FINISH) {
		css_code_t op = *codeptr++;
		switch (op) {
		case CALC_PUSH_VALUE: {
			css_fixed v = (css_fixed)(*codeptr++);
			uint32_t u = (uint32_t)(*codeptr++);
			if (sp == ONYX_CALC_STACK || !onyx_calc_term(state, v, u, &stack[sp]))
				return CSS_INVALID;
			sp++;
			break;
		}
		case CALC_PUSH_NUMBER:
			if (sp == ONYX_CALC_STACK)
				return CSS_INVALID;
			memset(&stack[sp], 0, sizeof(stack[sp]));
			stack[sp].num = (css_fixed)(*codeptr++);
			stack[sp].k = K_NUM;
			sp++;
			break;
		case CALC_ADD:
		case CALC_SUBTRACT: {
			onyx_lin *a, *b;
			if (sp < 2)
				return CSS_INVALID;
			a = &stack[sp - 2];
			b = &stack[sp - 1];
			if ((a->k == K_NUM) != (b->k == K_NUM))
				return CSS_INVALID;	/* a number and a length */
			if (op == CALC_ADD) {
				a->px += b->px; a->em += b->em; a->pct += b->pct; a->num += b->num;
			} else {
				a->px -= b->px; a->em -= b->em; a->pct -= b->pct; a->num -= b->num;
			}
			a->k |= b->k;
			sp--;
			break;
		}
		case CALC_MULTIPLY:
		case CALC_DIVIDE: {
			onyx_lin *a, *b;
			if (sp < 2)
				return CSS_INVALID;
			a = &stack[sp - 2];
			b = &stack[sp - 1];
			if (op == CALC_MULTIPLY && a->k == K_NUM && b->k != K_NUM) {
				onyx_lin t = *a;	/* 2 * 10px: the number on the right */
				*a = *b;
				*b = t;
			}
			if (b->k != K_NUM)
				return CSS_INVALID;
			if (op == CALC_DIVIDE && b->num == 0)
				return CSS_INVALID;
			onyx_calc_scale(a, b->num, op == CALC_DIVIDE);
			sp--;
			break;
		}
		case CALC_MIN:
		case CALC_MAX: {
			uint8_t ka, kb;
			css_fixed va, vb;
			if (sp < 2)
				return CSS_INVALID;
			if (!onyx_calc_single(&stack[sp - 2], &ka, &va) ||
			    !onyx_calc_single(&stack[sp - 1], &kb, &vb) || ka != kb)
				return CSS_INVALID;	/* e.g. min(10em, 50%): not now */
			if ((op == CALC_MIN) ? (vb < va) : (vb > va))
				stack[sp - 2] = stack[sp - 1];
			sp--;
			break;
		}
		default:
			return CSS_INVALID;
		}
	}

	if (sp != 1)
		return CSS_INVALID;
	*result = stack[0];
	return CSS_OK;
}

/* Documented in select/onyx_calc.h */
css_error css__onyx_calc_fold(css_style *style, const css_select_state *state,
		css_fixed *length, uint32_t *unit)
{
	lwc_string *expr = NULL;
	uint32_t snum;
	onyx_lin r;
	uint8_t k;

	advance_bytecode(style, sizeof(css_code_t));	/* the unit kind */
	snum = *((uint32_t *) style->bytecode);
	advance_bytecode(style, sizeof(snum));

	if (css__stylesheet_string_get(style->sheet, snum, &expr) != CSS_OK ||
	    expr == NULL)
		return CSS_INVALID;
	if (onyx_calc_eval(state, expr, &r) != CSS_OK)
		return CSS_INVALID;

	k = onyx_calc_kinds(&r);
	switch (k) {
	case 0:
	case K_PX:  *length = r.px;  *unit = UNIT_PX; break;
	case K_EM:  *length = r.em;  *unit = UNIT_EM; break;
	case K_PCT: *length = r.pct; *unit = UNIT_PCT; break;
	case K_NUM: *length = r.num; *unit = UNIT_CALC_NUMBER; break;
	default:
		return CSS_INVALID;	/* a mix of terms: needs the layout */
	}
	return CSS_OK;
}
