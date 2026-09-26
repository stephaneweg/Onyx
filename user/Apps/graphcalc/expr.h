//
// graphcalc/expr.h -- the graphing calculator's expressions: parse y = f(x) into an RPN
// program, evaluate it for many x fast. Portable (host tests too): the math comes from the
// BASIC core (basic/basnum.h), no libm.
//
// Grammar: numbers (1, 2.5, 1e-3), x, pi, e, + - * / ^ (right-assoc; -x^2 = -(x^2)),
// parentheses, functions sin cos tan asin acos atan sqrt abs ln log (base 10) exp floor
// ceil round sign, implicit multiplication (2x, 3sin(x), (x+1)(x-1), 2pi, x(x+1)).
//
#ifndef _graphcalc_expr_h
#define _graphcalc_expr_h

#include "basic/basnum.h"

namespace gc {

enum Op { O_NUM, O_X, O_ADD, O_SUB, O_MUL, O_DIV, O_POW, O_NEG,
	  O_SIN, O_COS, O_TAN, O_ASIN, O_ACOS, O_ATAN, O_SQRT, O_ABS, O_LN, O_LOG, O_EXP,
	  O_FLOOR, O_CEIL, O_ROUND, O_SIGN };

struct Instr { unsigned char op; double v; };

static const double PI = 3.14159265358979323846;
static inline double nan_ () { volatile double z = 0.0; return z / z; }
static inline bool isnan_ (double v) { return v != v; }

struct Program
{
	enum { MAX = 128 };
	Instr code[MAX];
	int   n;
	bool  ok;
	int   errPos;			// where parsing failed
	bool  usesX;

	double eval (double x) const
	{
		double st[64]; int sp = 0;
		for (int i = 0; i < n; i++)
		{
			const Instr &in = code[i];
			switch (in.op)
			{
			case O_NUM: st[sp++] = in.v; break;
			case O_X:   st[sp++] = x; break;
			case O_NEG: st[sp - 1] = -st[sp - 1]; break;
			case O_ADD: sp--; st[sp - 1] += st[sp]; break;
			case O_SUB: sp--; st[sp - 1] -= st[sp]; break;
			case O_MUL: sp--; st[sp - 1] *= st[sp]; break;
			case O_DIV: sp--; st[sp - 1] = st[sp] == 0.0 ? nan_ () : st[sp - 1] / st[sp]; break;
			case O_POW:
			{
				sp--; bool ok2 = true;
				double b = st[sp - 1], e = st[sp];
				double r;
				if (e == 2.0) r = b * b;
				else if (e == 3.0) r = b * b * b;
				else r = bas::npow (b, e, &ok2);
				st[sp - 1] = ok2 ? r : nan_ ();
				break;
			}
			default:
			{
				double a = st[sp - 1], r;
				switch (in.op)
				{
				case O_SIN:  r = bas::nsin (a); break;
				case O_COS:  r = bas::ncos (a); break;
				case O_TAN:  r = bas::ntan (a); break;
				case O_ATAN: r = bas::natan (a); break;
				case O_ASIN: r = (a < -1 || a > 1) ? nan_ () : (a == 1 ? PI / 2 : a == -1 ? -PI / 2 : bas::natan (a / bas::nsqrt (1 - a * a))); break;
				case O_ACOS: r = (a < -1 || a > 1) ? nan_ () : PI / 2 - (a == 1 ? PI / 2 : a == -1 ? -PI / 2 : bas::natan (a / bas::nsqrt (1 - a * a))); break;
				case O_SQRT: r = a < 0 ? nan_ () : bas::nsqrt (a); break;
				case O_ABS:  r = a < 0 ? -a : a; break;
				case O_LN:   r = a <= 0 ? nan_ () : bas::nlog (a); break;
				case O_LOG:  r = a <= 0 ? nan_ () : bas::nlog (a) / 2.302585092994045684; break;
				case O_EXP:  r = a > 700 ? nan_ () : bas::nexp (a); break;
				case O_FLOOR: r = bas::nfloor (a); break;
				case O_CEIL: r = -bas::nfloor (-a); break;
				case O_ROUND: r = bas::nfloor (a + 0.5); break;
				case O_SIGN: r = a > 0 ? 1 : a < 0 ? -1 : 0; break;
				default: r = nan_ ();
				}
				st[sp - 1] = r;
			}
			}
		}
		return sp == 1 ? st[0] : nan_ ();
	}
};

class Parser
{
public:
	const char *s; int p; Program *pr; int depth; bool err;

	bool compile (const char *src, Program &out)
	{
		s = src; p = 0; pr = &out; out.n = 0; out.ok = false; out.usesX = false; depth = 0; err = false;
		skip ();
		if (s[p] == '\0') { out.errPos = 0; return false; }
		expr ();
		skip ();
		if (err || s[p] != '\0') { out.errPos = p; return false; }
		out.ok = true;
		return true;
	}

private:
	void skip () { while (s[p] == ' ' || s[p] == '\t') p++; }
	void emit (int op, double v = 0)
	{
		if (pr->n >= Program::MAX) { err = true; return; }
		pr->code[pr->n].op = (unsigned char) op; pr->code[pr->n].v = v; pr->n++;
		if (op == O_X) pr->usesX = true;
	}
	static bool alpha (char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
	static char lower (char c) { return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c; }
	bool word (const char *w)
	{
		int i = 0;
		while (w[i] && lower (s[p + i]) == w[i]) i++;
		if (w[i]) return false;
		p += i; return true;
	}
	// Can the next token start a factor (for implicit multiplication)?
	bool startsFactor ()
	{
		skip ();
		char c = s[p];
		return (c >= '0' && c <= '9') || c == '.' || c == '(' || alpha (c);
	}

	void expr ()				// sum
	{
		term ();
		for (;;)
		{
			skip ();
			if (s[p] == '+') { p++; term (); emit (O_ADD); }
			else if (s[p] == '-') { p++; term (); emit (O_SUB); }
			else break;
		}
	}
	void term ()				// product (explicit or implicit)
	{
		unary ();
		for (;;)
		{
			skip ();
			if (s[p] == '*') { p++; unary (); emit (O_MUL); }
			else if (s[p] == '/') { p++; unary (); emit (O_DIV); }
			else if (!err && startsFactor ()) { power (); emit (O_MUL); }	// 2x, x(x+1), 3sin(x)
			else break;
		}
	}
	void unary ()
	{
		skip ();
		if (s[p] == '-') { p++; unary (); emit (O_NEG); return; }
		if (s[p] == '+') { p++; unary (); return; }
		power ();
	}
	void power ()
	{
		factor ();
		skip ();
		if (s[p] == '^') { p++; unary (); emit (O_POW); }	// right-assoc: 2^-x, 2^3^2
	}
	void factor ()
	{
		skip ();
		if (err) return;
		if (++depth > 40) { err = true; return; }
		char c = s[p];
		if ((c >= '0' && c <= '9') || c == '.')
		{
			double v = 0; bool any = false;
			while (s[p] >= '0' && s[p] <= '9') { v = v * 10 + (s[p++] - '0'); any = true; }
			if (s[p] == '.')
			{
				p++; double f = 0.1;
				while (s[p] >= '0' && s[p] <= '9') { v += (s[p++] - '0') * f; f *= 0.1; any = true; }
			}
			if (!any) { err = true; depth--; return; }
			if ((s[p] == 'e' || s[p] == 'E') && ((s[p + 1] >= '0' && s[p + 1] <= '9') || ((s[p + 1] == '-' || s[p + 1] == '+') && s[p + 2] >= '0' && s[p + 2] <= '9')))
			{
				p++; int sg = 1, e = 0;
				if (s[p] == '-') { sg = -1; p++; } else if (s[p] == '+') p++;
				while (s[p] >= '0' && s[p] <= '9') e = e * 10 + (s[p++] - '0');
				while (e-- > 0) v = sg > 0 ? v * 10 : v / 10;
			}
			emit (O_NUM, v);
		}
		else if (c == '(')
		{
			p++; expr (); skip ();
			if (s[p] != ')') err = true; else p++;
		}
		else if (alpha (c))
		{
			static const struct { const char *name; int op; } FN[] = {
				{ "asin", O_ASIN }, { "acos", O_ACOS }, { "atan", O_ATAN }, { "sin", O_SIN }, { "cos", O_COS },
				{ "tan", O_TAN }, { "sqrt", O_SQRT }, { "abs", O_ABS }, { "ln", O_LN }, { "log", O_LOG },
				{ "exp", O_EXP }, { "floor", O_FLOOR }, { "ceil", O_CEIL }, { "round", O_ROUND }, { "sign", O_SIGN } };
			int fn = -1;
			for (unsigned i = 0; i < sizeof FN / sizeof FN[0]; i++) if (word (FN[i].name)) { fn = FN[i].op; break; }
			if (fn >= 0)
			{
				skip ();
				if (s[p] == '(') { p++; expr (); skip (); if (s[p] != ')') err = true; else p++; }
				else power ();				// sin x, sqrt 2
				emit (fn);
			}
			else if (word ("pi")) emit (O_NUM, PI);
			else if (lower (c) == 'x') { p++; emit (O_X); }
			else if (lower (c) == 'e') { p++; emit (O_NUM, 2.71828182845904523536); }
			else err = true;
		}
		else err = true;
		depth--;
	}
};

// Format v with `dec` decimals (dec < 0: pick a precision), into out (>= 32 bytes).
static inline void fmt (double v, int dec, char *out)
{
	int n = 0;
	if (isnan_ (v)) { out[0] = '-'; out[1] = '-'; out[2] = 0; return; }
	if (v < 0) { out[n++] = '-'; v = -v; }
	if (v >= 1e15) { out[n++] = '*'; out[n] = 0; return; }	// out of range
	if (dec < 0) dec = v >= 1000 ? 1 : v >= 1 ? 4 : 6;
	double r = 0.5; for (int i = 0; i < dec; i++) r /= 10;
	v += r;
	unsigned long ip = (unsigned long) v;
	double fr = v - (double) ip;
	char t[24]; int k = 0;
	do { t[k++] = (char) ('0' + ip % 10); ip /= 10; } while (ip);
	while (k) out[n++] = t[--k];
	if (dec > 0)
	{
		out[n++] = '.';
		for (int i = 0; i < dec; i++) { fr *= 10; int d = (int) fr; if (d > 9) d = 9; out[n++] = (char) ('0' + d); fr -= d; }
		while (out[n - 1] == '0') n--;		// trim trailing zeros
		if (out[n - 1] == '.') n--;
	}
	if (n == 1 && out[0] == '-') n = 0;
	if (n == 0 || (n == 1 && out[0] == '-')) out[n++] = '0';
	out[n] = 0;
	if (out[0] == '-' && out[1] == '0' && out[2] == 0) { out[0] = '0'; out[1] = 0; }
}

} // namespace gc

#endif
