/* bench.c -- an integer benchmark for the Gekko (run_gc_test.sh: the interpreter / the JIT): a sort, a CRC, copies, calls */
typedef unsigned int u32;
static u32 seed = 1;
static u32 rnd (void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }
static u32 arr[3000];
static unsigned char buf[65536], buf2[65536];
static u32 crc (const unsigned char *p, int n)
{
	u32 c = 0xFFFFFFFF;
	for (int i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1)); }
	return ~c;
}
static int fib (int n) { return n < 2 ? n : fib (n - 1) + fib (n - 2); }
u32 run_bench (u32 *out)
{
	for (int i = 0; i < 3000; i++) arr[i] = rnd ();
	for (int i = 0; i < 3000; i++) for (int j = 0; j + 1 < 3000 - i; j++) if (arr[j] > arr[j + 1]) { u32 t = arr[j]; arr[j] = arr[j + 1]; arr[j + 1] = t; }
	for (int i = 0; i < 65536; i++) buf[i] = (unsigned char) rnd ();
	u32 c = 0;
	for (int r = 0; r < 8; r++) { for (int i = 0; i < 65536; i++) buf2[i] = buf[(i * 7 + r) & 65535]; c += crc (buf2, 65536); }
	c += (u32) fib (24);
	for (int i = 0; i < 3000; i += 100) c += arr[i];
	out[0] = c;
	return c;
}
