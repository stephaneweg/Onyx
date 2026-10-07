/*
 * v3d/shim.c -- the few C library functions Mesa's QPU packer (tools/qpu/mesa) calls, for the
 * freestanding apps (built with -DNDEBUG: no assert).
 */
int memcmp (const void *a, const void *b, unsigned long n)
{
	const unsigned char *x = a, *y = b;
	for (unsigned long i = 0; i < n; i++) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
	return 0;
}
int ffs (int v) { return v ? __builtin_ctz ((unsigned) v) + 1 : 0; }
int ffsll (long long v) { return v ? __builtin_ctzll ((unsigned long long) v) + 1 : 0; }
