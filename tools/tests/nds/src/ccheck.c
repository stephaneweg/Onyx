// ccheck.c -- the processors' differential check: kernels whose results only depend on C (fixed-size
// types, no undefined behaviour). Built for the PC (the reference) and for the ARM9 / ARM7 in ARM and
// Thumb modes (CCHECK names the function); the core must give the same 64 words.
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;

#ifndef CCHECK
#define CCHECK ccheck
#endif
#define NOINLINE __attribute__ ((noinline))

static u32 seed;
static NOINLINE u32 rnd (void) { seed = seed * 1664525u + 1013904223u; return seed; }
static u32 mix (u32 h, u32 v) { h ^= v; h *= 0x01000193u; h ^= h >> 15; return h; }

static NOINLINE u32 k_crc32 (void)
{
	u32 tab[256];
	for (u32 i = 0; i < 256; i++) { u32 c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; tab[i] = c; }
	u32 crc = 0xFFFFFFFFu;
	for (int i = 0; i < 2000; i++) crc = tab[(crc ^ (u8) rnd ()) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

static NOINLINE u32 k_mul64 (void)
{
	u64 acc = 0; s64 sacc = 0;
	for (int i = 0; i < 300; i++)
	{
		u32 a = rnd (), b = rnd ();
		acc += (u64) a * b;
		sacc += (s64) (s32) a * (s32) b;
		acc ^= acc >> 7;
	}
	return (u32) acc ^ (u32) (acc >> 32) ^ (u32) sacc ^ (u32) ((u64) sacc >> 32);
}

static NOINLINE u32 k_div (void)
{
	u32 h = 0;
	for (int i = 0; i < 200; i++)
	{
		u32 a = rnd (), b = (rnd () >> (rnd () & 31)) | 1;
		s32 sa = (s32) a, sb = (s32) b;
		if (sb == -1) sb = 3;
		h = mix (h, a / b); h = mix (h, a % b);
		h = mix (h, (u32) (sa / sb)); h = mix (h, (u32) (sa % sb));
		u64 la = ((u64) rnd () << 32) | rnd (), lb = ((u64) (rnd () >> 4) << (rnd () & 15)) | 1;
		h = mix (h, (u32) (la / lb)); h = mix (h, (u32) (la % lb));
	}
	return h;
}

static NOINLINE u32 k_shifts (void)
{
	u32 h = 0;
	for (int i = 0; i < 400; i++)
	{
		u32 x = rnd (); int n = (int) (rnd () & 31); int k = 1 + (int) (rnd () % 31);
		h = mix (h, x << n); h = mix (h, x >> n); h = mix (h, (u32) ((s32) x >> n));
		h = mix (h, (x >> k) | (x << (32 - k)));
		h = mix (h, (x & 0xFF00FF00u) >> 8 | (x & 0x00FF00FFu) << 8);
	}
	return h;
}

static NOINLINE u32 k_bits (void)
{
	u32 h = 0;
	for (int i = 0; i < 300; i++)
	{
		u32 x = rnd () >> (rnd () & 15);
		int pc = 0; for (u32 y = x; y; y &= y - 1) pc++;
		int lz = 0; if (!x) lz = 32; else for (u32 y = x; !(y & 0x80000000u); y <<= 1) lz++;
		u32 rev = 0; for (int k = 0; k < 32; k++) if (x & (1u << k)) rev |= 1u << (31 - k);
		h = mix (h, (u32) pc); h = mix (h, (u32) lz); h = mix (h, rev); h = mix (h, (u32) __builtin_clz (x | 1));
	}
	return h;
}

static NOINLINE u32 k_sort (void)
{
	s32 a[64];
	for (int i = 0; i < 64; i++) a[i] = (s32) rnd ();
	for (int i = 1; i < 64; i++) { s32 v = a[i]; int j = i - 1; while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; } a[j + 1] = v; }
	u32 h = 0; for (int i = 0; i < 64; i++) h = mix (h, (u32) a[i]);
	u32 b[48];
	for (int i = 0; i < 48; i++) b[i] = rnd ();
	for (int i = 1; i < 48; i++) { u32 v = b[i]; int j = i - 1; while (j >= 0 && b[j] < v) { b[j + 1] = b[j]; j--; } b[j + 1] = v; }
	for (int i = 0; i < 48; i++) h = mix (h, b[i]);
	return h;
}

struct S { u32 a, b, c, d, e, f; s16 g; s8 h; u8 i; };
static NOINLINE void scopy (struct S *d, const struct S *s) { *d = *s; }
static NOINLINE u32 k_structs (void)
{
	struct S s[8], t[8];
	for (int i = 0; i < 8; i++) { s[i].a = rnd (); s[i].b = rnd (); s[i].c = rnd (); s[i].d = rnd (); s[i].e = rnd (); s[i].f = rnd (); s[i].g = (s16) rnd (); s[i].h = (s8) rnd (); s[i].i = (u8) rnd (); }
	for (int i = 0; i < 8; i++) scopy (&t[7 - i], &s[i]);
	u32 h = 0;
	for (int i = 0; i < 8; i++) { h = mix (h, t[i].a + t[i].f); h = mix (h, (u32) (s32) t[i].g); h = mix (h, (u32) (s32) t[i].h); h = mix (h, t[i].i); }
	volatile u8 buf[64];
	for (int i = 0; i < 64; i++) buf[i] = (u8) rnd ();
	for (int i = 0; i < 60; i++) h = mix (h, (u32) buf[i] | ((u32) buf[i + 1] << 8) | ((u32) buf[i + 2] << 16) | ((u32) buf[i + 3] << 24));
	volatile s16 hb[32];
	for (int i = 0; i < 32; i++) hb[i] = (s16) rnd ();
	for (int i = 0; i < 32; i++) h = mix (h, (u32) (s32) hb[i]);
	return h;
}

static NOINLINE u32 fib (u32 n) { return n < 2 ? n : fib (n - 1) + fib (n - 2); }
static u32 f0 (u32 x) { return x * 3; }
static u32 f1 (u32 x) { return x ^ 0x5555; }
static u32 f2 (u32 x) { return x + 77; }
static u32 f3 (u32 x) { return ~x; }
static NOINLINE u32 sw (u32 x, u32 v)
{
	switch (x & 15)
	{
	case 0: return v + 1; case 1: return v * 5; case 2: return v ^ 0xAA; case 3: return v - 9;
	case 4: return v << 3; case 5: return v >> 2; case 6: return v | 0x100; case 7: return v & 0xFFF0;
	case 8: return v * v; case 9: return ~v; case 10: return v + 1000; case 11: return v - v / 3;
	case 12: return v ^ (v >> 5); case 13: return v * 9 + 1; case 14: return v + (v << 4); default: return 0;
	}
}
static NOINLINE u32 k_control (void)
{
	u32 (*fs[4]) (u32) = { f0, f1, f2, f3 };
	u32 h = fib (18);
	for (int i = 0; i < 300; i++) { u32 r = rnd (); h = mix (h, fs[r & 3] (r)); h = mix (h, sw (r >> 4, r)); }
	return h;
}

static NOINLINE u32 k_wide (void)
{
	u64 a = 0x0123456789ABCDEFull; s64 b = -1234567890123ll; u32 h = 0;
	for (int i = 0; i < 300; i++)
	{
		u64 r = ((u64) rnd () << 32) | rnd ();
		a += r; b -= (s64) r >> 3;
		h = mix (h, (u32) a); h = mix (h, (u32) (a >> 32));
		h = mix (h, a < r); h = mix (h, b < (s64) r); h = mix (h, (u32) ((u64) b >> 40));
		h = mix (h, (u32) (a >> (rnd () & 63))); h = mix (h, (u32) (a << (rnd () & 63)));
		h = mix (h, (u32) ((s64) a >> (rnd () & 63)));
	}
	return h;
}

static NOINLINE u32 k_fixed (void)
{
	s32 m[16], n[16], o[16];
	for (int i = 0; i < 16; i++) { m[i] = (s32) (rnd () >> 12) - 0x80000; n[i] = (s32) (rnd () >> 12) - 0x80000; }
	u32 h = 0;
	for (int it = 0; it < 20; it++)
	{
		for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++)
		{
			s64 v = 0; for (int k = 0; k < 4; k++) v += (s64) m[r * 4 + k] * n[k * 4 + c];
			o[r * 4 + c] = (s32) (v >> 12);
		}
		for (int i = 0; i < 16; i++) { h = mix (h, (u32) o[i]); m[i] = (o[i] >> 4) + (s32) (rnd () >> 20); }
	}
	return h;
}

static NOINLINE u32 k_cond (void)
{
	u32 h = 0;
	for (int i = 0; i < 400; i++)
	{
		s32 a = (s32) rnd (), b = (s32) rnd ();
		u32 ua = (u32) a, ub = (u32) b;
		h = mix (h, (u32) (a < b ? a : b)); h = mix (h, ua > ub ? ua : ub);
		h = mix (h, (u32) (a == b) + (u32) (a >= 0) * 2 + (u32) (ua >= ub) * 4 + (u32) (a > b) * 8 + (u32) (ua <= ub) * 16);
		s32 s = (s32) ((s64) a + b > 0x7FFFFFFF ? 0x7FFFFFFF : (s64) a + b < -0x7FFFFFFF - 1 ? -0x7FFFFFFF - 1 : a + (s64) b);
		h = mix (h, (u32) s);
		s16 x = (s16) a, y = (s16) b;
		h = mix (h, (u32) ((s32) x * y)); h = mix (h, (u32) ((s32) x * y + a));
		h = mix (h, (u32) (((s64) a * (s16) b) >> 16));
	}
	return h;
}

static NOINLINE u32 k_mem (void)
{
	static u32 big[512];
	for (int i = 0; i < 512; i++) big[i] = rnd ();
	u32 h = 0;
	for (int k = 0; k < 4; k++)
	{
		for (int i = 0; i < 511; i++) big[i] = big[i + 1] - big[i] * 3;
		for (int i = 511; i > 0; i--) big[i] ^= big[i - 1] >> 1;
	}
	for (int i = 0; i < 512; i++) h = mix (h, big[i]);
	u8 *b = (u8 *) big;
	for (int i = 1; i < 300; i += 3) { b[i] = (u8) (b[i] + b[i + 7]); u16 *p = (u16 *) (b + (i & ~1)); *p = (u16) (*p * 3); }
	for (int i = 0; i < 512; i++) h = mix (h, big[i]);
	return h;
}

void CCHECK (volatile u32 *out)
{
	seed = 12345;
	out[0] = k_crc32 ();
	out[1] = k_mul64 ();
	out[2] = k_div ();
	out[3] = k_shifts ();
	out[4] = k_bits ();
	out[5] = k_sort ();
	out[6] = k_structs ();
	out[7] = k_control ();
	out[8] = k_wide ();
	out[9] = k_fixed ();
	out[10] = k_cond ();
	out[11] = k_mem ();
}
