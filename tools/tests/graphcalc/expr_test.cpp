// expr_test.cpp -- host test of the graphing calculator's parser / evaluator
// (user/Apps/graphcalc/expr.h). Run: sh tools/tests/run_graphcalc_test.sh
#include <stdio.h>
#include <math.h>
#include "Apps/graphcalc/expr.h"

static int fails;
static void check (const char *src, double x, double want)
{
	gc::Program p; gc::Parser ps;
	bool ok = ps.compile (src, p);
	double got = ok ? p.eval (x) : NAN;
	bool good = (isnan (want) && isnan (got)) || fabs (got - want) <= 1e-9 * (1 + fabs (want));
	printf ("%s %-24s x=%-6g -> %.12g (want %.12g)\n", good ? "ok  " : "FAIL", src, x, got, want);
	if (!good) fails++;
}
static void bad (const char *src)
{
	gc::Program p; gc::Parser ps;
	bool ok = ps.compile (src, p);
	printf ("%s syntax error expected: \"%s\" (pos %d)\n", ok ? "FAIL" : "ok  ", src, p.errPos);
	if (ok) fails++;
}
int main ()
{
	check ("1+2*3", 0, 7);
	check ("2x+1", 3, 7);
	check ("x^2", -3, 9);
	check ("-x^2", 3, -9);
	check ("2^3^2", 0, 512);
	check ("2^-1", 0, 0.5);
	check ("(x+1)(x-1)", 4, 15);
	check ("3sin(x)", 1, 3 * sin (1));
	check ("sin x", 0.5, sin (0.5));
	check ("x(x+1)", 2, 6);
	check ("2pi", 0, 2 * M_PI);
	check ("e^x", 1, M_E);
	check ("exp(x)", 2, exp (2));
	check ("sqrt(x)", 2, sqrt (2));
	check ("sqrt(x)", -1, NAN);
	check ("1/x", 0, NAN);
	check ("ln(x)", 10, log (10));
	check ("log(1000)", 0, 3);
	check ("abs(x-5)", 2, 3);
	check ("asin(x)", 0.5, asin (0.5));
	check ("acos(x)", 0.5, acos (0.5));
	check ("atan(x)", 1, M_PI / 4);
	check ("tan(x)", 1, tan (1));
	check ("floor(x)", -1.5, -2);
	check ("ceil(x)", -1.5, -1);
	check ("round(2.5)", 0, 3);
	check ("sign(x)", -7, -1);
	check ("x^0.5", 9, 3);
	check ("1.5e3", 0, 1500);
	check ("2X + 3", 1, 5);
	check ("cos(x)^2+sin(x)^2", 0.7, 1);
	bad ("");
	bad ("2+");
	bad ("sin(");
	bad ("foo(x)");
	bad ("(x+1");
	char b[32];
	gc::fmt (3.14159, 2, b); printf ("%s fmt 3.14159,2 -> %s\n", b[0] == '3' && b[2] == '1' && b[3] == '4' && !b[4] ? "ok  " : "FAIL", b);
	gc::fmt (-0.5, -1, b); printf ("fmt -0.5 -> %s\n", b);
	gc::fmt (1200, 0, b); printf ("fmt 1200 -> %s\n", b);
	printf (fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails != 0;
}
