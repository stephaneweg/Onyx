/*
 * tools/tests/netsurf/wasm/bench.c -- a small C program compiled to WebAssembly for the
 * bench (pages/js-wasm-bench.wasm, used by pages/js-wasm.html): SHA-256, a sieve, a sort,
 * a recursion, a Mandelbrot set, an import called back, memory grown. No libc: what the
 * compiler may call (memset, memcpy) is here. Build (tools/tests/netsurf/wasm/build.sh):
 *
 *   clang --target=wasm32 -O2 -fno-builtin -nostdlib -Wl,--no-entry -o js-wasm-bench.wasm bench.c
 *
 * The same functions compiled natively (-DNATIVE) and run by Node give the comparison
 * timings of docs/06 (build.sh prints them).
 */
#include <stdint.h>
#include <stddef.h>

#ifdef NATIVE
#define EXPORT(name)
#else
#define EXPORT(name) __attribute__((export_name(name)))
__attribute__((import_module("env"), import_name("report"))) void report(int a, int b);
#endif

#ifdef NATIVE
#include <string.h>
#else
void *memset(void *d, int c, size_t n)
{
	unsigned char *p = d;
	while (n--)
		*p++ = (unsigned char) c;
	return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
	unsigned char *p = d;
	const unsigned char *q = s;
	while (n--)
		*p++ = *q++;
	return d;
}
#endif

/* ---- SHA-256 ---- */
static const uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_block(uint32_t h[8], const uint8_t *b)
{
	uint32_t w[64], a, bb, c, d, e, f, g, hh, t1, t2;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = (uint32_t) b[4 * i] << 24 | (uint32_t) b[4 * i + 1] << 16 |
			(uint32_t) b[4 * i + 2] << 8 | b[4 * i + 3];
	for (; i < 64; i++)
		w[i] = (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10)) + w[i - 7] +
			(ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 16];
	a = h[0]; bb = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; hh = h[7];
	for (i = 0; i < 64; i++) {
		t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
		t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & bb) ^ (a & c) ^ (bb & c));
		hh = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
	}
	h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

static uint8_t in_buf[4096], out_buf[32];

EXPORT("buffer_in") uint8_t *buffer_in(void) { return in_buf; }
EXPORT("buffer_out") uint8_t *buffer_out(void) { return out_buf; }

/* the digest of data[len] (len < 4000), computed `times` times (the last one kept) */
EXPORT("sha256") void sha256(const uint8_t *data, int len, int times)
{
	static uint8_t blk[128];
	while (times-- > 0) {
		uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
			0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
		int i = 0, n;
		for (; i + 64 <= len; i += 64)
			sha_block(h, data + i);
		n = len - i;
		memset(blk, 0, sizeof blk);
		memcpy(blk, data + i, n);
		blk[n] = 0x80;
		n = n < 56 ? 64 : 128;
		{
			uint64_t bits = (uint64_t) len * 8;
			int k;
			for (k = 0; k < 8; k++)
				blk[n - 1 - k] = (uint8_t) (bits >> (8 * k));
		}
		sha_block(h, blk);
		if (n == 128)
			sha_block(h, blk + 64);
		for (i = 0; i < 8; i++) {
			out_buf[4 * i] = h[i] >> 24; out_buf[4 * i + 1] = h[i] >> 16;
			out_buf[4 * i + 2] = h[i] >> 8; out_buf[4 * i + 3] = h[i];
		}
	}
}

/* ---- memory: a bump allocator on memory.grow ---- */
extern unsigned char __heap_base;
static uintptr_t heap_top;

static void *alloc(size_t n)
{
	uintptr_t p;
#ifdef NATIVE
	static unsigned char heap[64 << 20];
	if (!heap_top)
		heap_top = (uintptr_t) heap;
#else
	if (!heap_top)
		heap_top = (uintptr_t) &__heap_base;
#endif
	p = (heap_top + 15) & ~(uintptr_t) 15;
	heap_top = p + n;
#ifndef NATIVE
	{
		size_t have = __builtin_wasm_memory_size(0) * 65536;
		if (heap_top > have &&
		    __builtin_wasm_memory_grow(0, (heap_top - have + 65535) / 65536) < 0)
			return 0;
	}
#endif
	return (void *) p;
}

static void release(void *p) { heap_top = (uintptr_t) p; }

/* the number of primes below n */
EXPORT("sieve") int sieve(int n)
{
	uint8_t *s = alloc(n);
	int i, j, c = 0;

	memset(s, 1, n);
	for (i = 2; i < n; i++) {
		if (!s[i])
			continue;
		c++;
		for (j = 2 * i; j < n; j += i)
			s[j] = 0;
	}
	release(s);
	return c;
}

/* a quicksort of n pseudo-random numbers; 1 if the result is sorted */
static void qs(int32_t *a, int lo, int hi)
{
	while (lo < hi) {
		int32_t p = a[(lo + hi) / 2], t;
		int i = lo, j = hi;
		while (i <= j) {
			while (a[i] < p) i++;
			while (a[j] > p) j--;
			if (i <= j) { t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
		}
		if (j - lo < hi - i) { qs(a, lo, j); lo = i; } else { qs(a, i, hi); hi = j; }
	}
}

EXPORT("sort_check") int sort_check(int n)
{
	int32_t *a = alloc(n * sizeof *a);
	uint32_t x = 12345;
	int i, sorted = 1;

	for (i = 0; i < n; i++) {
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		a[i] = (int32_t) (x % 1000000);
	}
	qs(a, 0, n - 1);
	for (i = 1; i < n; i++)
		if (a[i - 1] > a[i])
			sorted = 0;
	release(a);
	return sorted;
}

EXPORT("fib") int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

/* the points inside the Mandelbrot set on a w x h grid */
EXPORT("mandel") int mandel(int w, int h, int iters)
{
	int x, y, inside = 0;

	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			double cr = -2.0 + 3.0 * x / w, ci = -1.2 + 2.4 * y / h, zr = 0, zi = 0;
			int k = 0;
			while (k < iters && zr * zr + zi * zi < 4.0) {
				double t = zr * zr - zi * zi + cr;
				zi = 2 * zr * zi + ci;
				zr = t;
				k++;
			}
			inside += k == iters;
		}
	return inside;
}

#ifndef NATIVE
EXPORT("call_report") void call_report(int a) { report(a, a * 2); }

/* the memory grown by n pages through the allocator; returns n when it worked */
EXPORT("grow_test") int grow_test(int n)
{
	return alloc((size_t) n * 65536) != 0 ? n : -1;
}
#endif

#ifdef NATIVE
#include <stdio.h>
#include <time.h>
static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
int main(void)
{
	double t = now();
	volatile int pr;
	memcpy(in_buf, "abc", 3);
	sha256(in_buf, 3, 20000);
	printf("native sha256x20000 %.1f ms, ", now() - t);
	t = now();
	pr = sieve(2000000);
	printf("sieve(2e6) %.1f ms (%d), ", now() - t, (int) pr);
	t = now();
	pr = fib(27);
	printf("fib(27) %.1f ms, ", now() - t);
	t = now();
	pr = mandel(320, 240, 256);
	printf("mandel 320x240x256 %.1f ms\n", now() - t);
	return 0;
}
#endif
