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

/* the floating point: 4x4 matrices (single: fmadds...), a vector transform, a double series */
static float mat[4][4], acc[4][4], vec[256][4];
u32 run_fbench (u32 *out)
{
	for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { mat[i][j] = (float) (i * 4 + j) * 0.01f + 0.5f; acc[i][j] = i == j; }
	for (int i = 0; i < 256; i++) for (int k = 0; k < 4; k++) vec[i][k] = (float) (i + k) * 0.25f;
	for (int r = 0; r < 20000; r++)
	{
		float t[4][4];
		for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++)
			t[i][j] = acc[i][0] * mat[0][j] + acc[i][1] * mat[1][j] + acc[i][2] * mat[2][j] + acc[i][3] * mat[3][j];
		float s = 1.0f / (t[0][0] + t[1][1] + t[2][2] + t[3][3]);
		for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) acc[i][j] = t[i][j] * s;
	}
	for (int r = 0; r < 200; r++)
		for (int i = 0; i < 256; i++)
		{
			float x = vec[i][0], y = vec[i][1], z = vec[i][2];
			vec[i][0] = acc[0][0] * x + acc[0][1] * y + acc[0][2] * z + acc[0][3];
			vec[i][1] = acc[1][0] * x + acc[1][1] * y + acc[1][2] * z + acc[1][3];
			vec[i][2] = acc[2][0] * x + acc[2][1] * y + acc[2][2] * z + acc[2][3];
		}
	double e = 0.0, f = 1.0;
	for (int i = 1; i < 200000; i++) { e += f; f = f / (double) i; if (f < 1e-300) f = 1.0; }
	union { float f; u32 u; } c; union { double d; u32 u[2]; } g;
	u32 h = 0;
	for (int i = 0; i < 256; i++) for (int k = 0; k < 3; k++) { c.f = vec[i][k]; h = h * 31 + c.u; }
	g.d = e; h ^= g.u[0] ^ g.u[1];
	out[0] = h;
	return h;
}
