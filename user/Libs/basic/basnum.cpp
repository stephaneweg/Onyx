//
// basic/basnum.cpp -- numbers for Onyx BASIC without a libc: formatting (PRINT / STR$),
// parsing (VAL, literals) and the math functions (SQR, SIN, COS, TAN, ATN, EXP, LOG, ^).
// Doubles throughout; the Onyx build compiles this with hardware FP (see user/Makefile).
//
#include "basic/bas.h"
#include "basic/basnum.h"

namespace bas {

static const double LN2 = 0.69314718055994530942, PI = 3.14159265358979323846;

double nfloor (double x) { return __builtin_floor (x); }
double nsqrt (double x)  { return x <= 0 ? 0 : __builtin_sqrt (x); }

// 2^k exactly (k in -1022..1023) by building the exponent bits.
static double pow2i (int k)
{
	if (k > 1023) k = 1023;
	if (k < -1022) return 0;
	union { double d; unsigned long long u; } v;
	v.u = (unsigned long long) (k + 1023) << 52;
	return v.d;
}

double nexp (double x)
{
	if (x > 709) return 1.7976931348623157e308;
	if (x < -745) return 0;
	int k = (int) nfloor (x / LN2 + 0.5);
	double r = x - k * LN2, term = 1, sum = 1;
	for (int i = 1; i < 30; i++) { term *= r / i; sum += term; if (term < 1e-17 && term > -1e-17) break; }
	return sum * pow2i (k);
}

double nlog (double x)
{
	if (x <= 0) return 0;
	union { double d; unsigned long long u; } v; v.d = x;
	int e = (int) ((v.u >> 52) & 0x7FF) - 1023;
	v.u = (v.u & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull;	// mantissa in [1, 2)
	double m = v.d;
	if (m > 1.41421356237309504880) { m *= 0.5; e++; }
	double s = (m - 1) / (m + 1), s2 = s * s, term = s, sum = 0;
	for (int i = 1; i < 60; i += 2) { sum += term / i; term *= s2; if (term < 1e-18 && term > -1e-18) break; }
	return 2 * sum + e * LN2;
}

double nsin (double x)
{
	x -= 2 * PI * nfloor (x / (2 * PI) + 0.5);		// [-pi, pi]
	if (x > PI / 2) x = PI - x;
	else if (x < -PI / 2) x = -PI - x;
	double x2 = x * x, term = x, sum = x;
	for (int i = 1; i < 20; i++) { term *= -x2 / ((2 * i) * (2 * i + 1)); sum += term; }
	return sum;
}
double ncos (double x) { return nsin (x + PI / 2); }
double ntan (double x) { double c = ncos (x); return c == 0 ? 1e308 : nsin (x) / c; }

double natan (double x)
{
	if (x < 0) return -natan (-x);
	if (x > 1) return PI / 2 - natan (1 / x);
	// Halve the angle twice: atan x = 2 atan (x / (1 + sqrt (1 + x^2))).
	int halvings = 0;
	while (x > 0.2 && halvings < 4) { x = x / (1 + nsqrt (1 + x * x)); halvings++; }
	double x2 = x * x, term = x, sum = 0;
	for (int i = 1; i < 80; i += 2) { sum += term / i; term *= -x2; if (term < 1e-18 && term > -1e-18) break; }
	return sum * (1 << halvings);
}

// x ^ y. ok = false: a negative number to a non-integer power.
double npow (double x, double y, bool *ok)
{
	*ok = true;
	if (y == 0) return 1;
	if (y == nfloor (y) && y >= -1024 && y <= 1024)
	{
		long long n = (long long) y; bool neg = n < 0; if (neg) n = -n;
		double r = 1, b = x;
		while (n) { if (n & 1) r *= b; b *= b; n >>= 1; }
		return neg ? (r == 0 ? 1e308 : 1 / r) : r;
	}
	if (x == 0) return 0;
	if (x < 0) { *ok = false; return 0; }
	return nexp (y * nlog (x));
}

// ---- formatting -------------------------------------------------------------------------------
// Integers (|v| < 1e15) in full; otherwise 7 significant digits (QBasic's single precision
// look): ".5", "-3.25", "1.234568E+08", "1E-10" -- or 15 with dbl ("1.23456789012345D+20").
int formatNum (double v, char *out, bool dbl)
{
	const int ND = dbl ? 15 : 7;
	int p = 0;
	if (v != v) { const char *s = "NaN"; while (*s) out[p++] = *s++; out[p] = 0; return p; }
	if (v < 0) { out[p++] = '-'; v = -v; }
	if (v == nfloor (v) && v < 1e15)
	{
		char t[24]; int n = 0;
		unsigned long long u = (unsigned long long) v;
		if (u == 0) t[n++] = '0';
		while (u) { t[n++] = (char) ('0' + u % 10); u /= 10; }
		while (n) out[p++] = t[--n];
		out[p] = 0;
		return p;
	}
	// Scale to ND significant digits: m in [1, 10), v = m * 10^e.
	int e = 0; double m = v;
	if (m >= 10) { while (m >= 1e16) { m /= 1e16; e += 16; } while (m >= 10) { m /= 10; e++; } }
	else if (m < 1) { while (m < 1e-16) { m *= 1e16; e -= 16; } while (m < 1) { m *= 10; e--; } }
	double scale = 1; for (int i = 1; i < ND; i++) scale *= 10;
	long long digits = (long long) (m * scale + 0.5);
	long long top = (long long) (scale * 10);
	if (digits >= top) { digits /= 10; e++; }
	char d[16];
	for (int i = ND - 1; i >= 0; i--) { d[i] = (char) ('0' + digits % 10); digits /= 10; }
	int nd = ND; while (nd > 1 && d[nd - 1] == '0') nd--;		// drop trailing zeros
	if (e >= ND || e < -8 - (dbl ? 8 : 0))				// scientific
	{
		out[p++] = d[0];
		if (nd > 1) { out[p++] = '.'; for (int i = 1; i < nd; i++) out[p++] = d[i]; }
		out[p++] = dbl ? 'D' : 'E'; out[p++] = e < 0 ? '-' : '+';
		int ae = e < 0 ? -e : e;
		if (ae >= 100) out[p++] = (char) ('0' + ae / 100);
		out[p++] = (char) ('0' + ae / 10 % 10); out[p++] = (char) ('0' + ae % 10);
	}
	else if (e < 0)							// .000ddd
	{
		out[p++] = '.';
		for (int i = -1; i > e; i--) out[p++] = '0';
		for (int i = 0; i < nd; i++) out[p++] = d[i];
	}
	else								// ddd.ddd
	{
		for (int i = 0; i <= e; i++) out[p++] = i < nd ? d[i] : '0';
		if (nd > e + 1) { out[p++] = '.'; for (int i = e + 1; i < nd; i++) out[p++] = d[i]; }
	}
	out[p] = 0;
	return p;
}

// VAL: optional blanks and sign, digits, '.', exponent (E/D), or &H / &O / &B prefixes.
double parseNum (const char *s, int *used)
{
	const char *p = s;
	while (*p == ' ' || *p == '\t') p++;
	if (p[0] == '&')
	{
		int base = 0; char c = p[1];
		if (c == 'H' || c == 'h') base = 16; else if (c == 'O' || c == 'o') base = 8; else if (c == 'B' || c == 'b') base = 2;
		if (base)
		{
			p += 2; double v = 0;
			for (;;)
			{
				int d = -1; char ch = *p;
				if (ch >= '0' && ch <= '9') d = ch - '0';
				else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
				else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
				if (d < 0 || d >= base) break;
				v = v * base + d; p++;
			}
			if (used) *used = (int) (p - s);
			return v;
		}
	}
	bool neg = false;
	if (*p == '-' || *p == '+') { neg = *p == '-'; p++; }
	double v = 0; bool any = false;
	while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; any = true; }
	if (*p == '.')
	{
		p++; double f = 0.1;
		while (*p >= '0' && *p <= '9') { v += (*p - '0') * f; f *= 0.1; p++; any = true; }
	}
	if (!any) { if (used) *used = 0; return 0; }
	if (*p == 'E' || *p == 'e' || *p == 'D' || *p == 'd')
	{
		const char *q = p + 1; bool en = false;
		if (*q == '-' || *q == '+') { en = *q == '-'; q++; }
		if (*q >= '0' && *q <= '9')
		{
			int ex = 0;
			while (*q >= '0' && *q <= '9') { if (ex < 400) ex = ex * 10 + (*q - '0'); q++; }
			double m = 1; for (int i = 0; i < ex; i++) m *= 10;
			v = en ? v / m : v * m;
			p = q;
		}
	}
	if (used) *used = (int) (p - s);
	return neg ? -v : v;
}

} // namespace bas
