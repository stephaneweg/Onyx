//
// fptest -- proof that hardware floating point works in a userland app.
//
// Built WITHOUT -mgeneral-regs-only (see bin/Makefile), so GCC emits real FP/SIMD.
// It computes with doubles ACROSS cooperative context switches: it yields via
// kapi_msleep() in the middle of the computation, so other tasks run (with their
// own FP state) while a running total is kept live in FP registers. This works
// because the kernel saves/restores the full FP/SIMD state on every trap
// (kern/trapframe.h) and FP is enabled at EL0 (CPACR_EL1). If FP state were not
// preserved across switches, the running total would be corrupted and the result
// would not match pi.
//
#include "kapi.h"
#include "applib.h"

// Format a non-negative double as "<int>.<6 decimals>" into b, using only the
// hardware int<->double conversions (no libm). Returns the length written.
static int fmt_fixed6 (double v, char *b)
{
	int p = 0;
	long ip = (long) v;				// fcvtzs
	double frac = v - (double) ip;			// scvtf + fsub
	long fd = (long) (frac * 1000000.0 + 0.5);	// round to 6 decimals
	if (fd >= 1000000) { fd -= 1000000; ip += 1; }	// carry
	p += ax_itoa ((int) ip, b + p);
	b[p++] = '.';
	for (long div = 100000; div >= 1; div /= 10)	// six zero-padded digits
		b[p++] = (char) ('0' + (int) ((fd / div) % 10));
	b[p] = '\0';
	return p;
}

int main (void)
{
	ax_putln ("fptest: hardware float under the scheduler");

	// Leibniz series: pi/4 = 1 - 1/3 + 1/5 - 1/7 + ...  (only + - / and int->double)
	const long N = 3000000;
	double sum = 0.0, sign = 1.0;
	for (long k = 0; k < N; k++)
	{
		sum  += sign / (double) (2 * k + 1);
		sign  = -sign;
		if ((k % 200000) == 0)
			kapi_msleep (1);		// yield mid-computation
	}
	double pi = 4.0 * sum;

	char line[64];
	int p = 0;
	ax_strcat (line, (int) sizeof (line), &p, "pi ~= ");
	fmt_fixed6 (pi, line + p);
	ax_putln (line);

	int ok = 1;

	// 1) the series must land near pi (Leibniz error ~ 1/(2N) ~ 1.7e-7 here).
	double diff = pi - 3.14159265358979;
	if (diff < 0.0) diff = -diff;
	if (diff > 0.001) ok = 0;

	// 2) a basic round-trip surviving the same yield path: (1/3)*3 ~= 1.
	double third = 1.0 / 3.0;
	kapi_msleep (1);
	double back = third * 3.0 - 1.0;
	if (back < 0.0) back = -back;
	if (back > 0.0001) ok = 0;

	ax_putln (ok ? "RESULT: PASS" : "RESULT: FAIL");
	return ok ? 0 : 1;
}
