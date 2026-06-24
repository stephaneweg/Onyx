//
// libctest -- proof that newlib (libc + libm) works on Onyx via the kapi syscall
// layer (user/libc/onyx_syscalls.c). Exercises stdio (printf + FILE* with fseek),
// stdlib (malloc/realloc/qsort), string.h, and libm (sqrt/sin/pow), with kapi_msleep
// yields interleaved so the work crosses cooperative context switches. Prints a
// final PASS/FAIL line.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "kapi.h"		// kapi_msleep, to yield mid-test

static int cmp_int (const void *a, const void *b)
{
	int x = *(const int *) a, y = *(const int *) b;
	return (x > y) - (x < y);
}

static int approx (double a, double b)
{
	double d = a - b;
	if (d < 0) d = -d;
	return d < 1e-6;
}

int main (void)
{
	int ok = 1;

	printf ("libctest: newlib over kapi\n");

	// --- stdlib: malloc / realloc / qsort -----------------------------------
	int n = 256;
	int *v = (int *) malloc (n * sizeof (int));
	if (!v) { printf ("malloc failed\n"); return 1; }
	unsigned seed = 12345;
	for (int i = 0; i < n; i++) { seed = seed * 1103515245u + 12345u; v[i] = (int) (seed >> 8) % 1000; }
	kapi_msleep (1);					// yield with heap data live
	qsort (v, n, sizeof (int), cmp_int);
	int sorted = 1;
	for (int i = 1; i < n; i++) if (v[i - 1] > v[i]) sorted = 0;
	printf ("  qsort sorted=%d  min=%d max=%d\n", sorted, v[0], v[n - 1]);
	if (!sorted) ok = 0;
	v = (int *) realloc (v, 2 * n * sizeof (int));
	if (!v) ok = 0; else free (v);

	// --- string.h -----------------------------------------------------------
	char buf[64];
	strcpy (buf, "Onyx");
	strcat (buf, "/");
	strcat (buf, "NetSurf");
	if (strcmp (buf, "Onyx/NetSurf") != 0) ok = 0;
	if (strlen (buf) != 12) ok = 0;
	printf ("  string: \"%s\" len=%u\n", buf, (unsigned) strlen (buf));

	// --- libm: hardware float ----------------------------------------------
	// Use a RUNTIME argument (so -O2 can't constant-fold the calls away) and check
	// identities that hold for any x: sin^2+cos^2==1, sqrt(x)^2==x, pow(x,2)==x*x.
	double x = 1.0 + (double) (kapi_get_ticks () % 1000) / 1000.0;	// in [1,2)
	double sx = sin (x), cx = cos (x);
	double id = sx * sx + cx * cx;				// == 1
	double rt = sqrt (x);
	double pw = pow (x, 2.0);
	kapi_msleep (1);					// yield with FP state live
	printf ("  libm: x=%.6f  sin^2+cos^2=%.6f  sqrt(x)^2=%.6f  pow(x,2)=%.6f\n",
		x, id, rt * rt, pw);
	if (!approx (id, 1.0))    ok = 0;
	if (!approx (rt * rt, x)) ok = 0;
	if (!approx (pw, x * x))  ok = 0;

	// --- stdio FILE*: write, then read back with fseek/ftell ----------------
	const char *path = "libctest.tmp";
	FILE *f = fopen (path, "w");
	if (!f) { printf ("  fopen(w) failed\n"); ok = 0; }
	else
	{
		fprintf (f, "pi=%.5f\nanswer=%d\n", 3.14159, 42);
		fclose (f);					// flush -> kapi_save_file
	}

	f = fopen (path, "r");
	if (!f) { printf ("  fopen(r) failed\n"); ok = 0; }
	else
	{
		fseek (f, 0, SEEK_END);
		long len = ftell (f);
		fseek (f, 0, SEEK_SET);
		char line1[64] = {0}, line2[64] = {0};
		fgets (line1, sizeof line1, f);
		double pi = 0; int answer = 0;
		fscanf (f, "answer=%d", &answer);		// 2nd line
		(void) line2;
		sscanf (line1, "pi=%lf", &pi);
		fclose (f);
		printf ("  file: len=%ld  pi=%.5f  answer=%d\n", len, pi, answer);
		if (len <= 0 || !approx (pi, 3.14159) || answer != 42) ok = 0;
	}
	remove (path);

	printf ("RESULT: %s\n", ok ? "PASS" : "FAIL");
	fflush (stdout);
	return ok ? 0 : 1;
}
