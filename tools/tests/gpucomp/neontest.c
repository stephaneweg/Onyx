//
// neontest.c -- gpucomp's CPU path built for the Pi (aarch64, -O3 -mcpu=cortex-a72: its loops made
// NEON code by the compiler) against the same code built for the PC: scenes composited by the CPU
// path, a hash of each picture printed; the two runs must print the same lines. On the PC the
// aarch64 build runs under qemu-aarch64 (user mode): no libc, Linux system calls (write, mmap,
// exit), the kapi table at its address with gpu_info saying "no GPU". sh tools/tests/run_gpucomp_test.sh
//
#include "kapi.h"
#include "gpucomp/gpucomp.h"

#ifdef __aarch64__
static long sys (long n, long a, long b, long c, long d, long e, long f)
{
	register long x8 __asm__ ("x8") = n, x0 __asm__ ("x0") = a, x1 __asm__ ("x1") = b, x2 __asm__ ("x2") = c;
	register long x3 __asm__ ("x3") = d, x4 __asm__ ("x4") = e, x5 __asm__ ("x5") = f;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x8), "r" (x1), "r" (x2), "r" (x3), "r" (x4), "r" (x5) : "memory");
	return x0;
}
static void out (const char *s) { long n = 0; while (s[n]) n++; sys (64, 1, (long) s, n, 0, 0, 0); }
static void *map (void *at, unsigned long n) { return (void *) sys (222, (long) at, (long) n, 3, at ? 0x32 : 0x22, -1, 0); }
__attribute__ ((optimize ("no-tree-loop-distribute-patterns"))) void *memset (void *d, int c, unsigned long n) { unsigned char *p = (unsigned char *) d; while (n--) *p++ = (unsigned char) c; return d; }
__attribute__ ((optimize ("no-tree-loop-distribute-patterns"))) void *memcpy (void *d, const void *s, unsigned long n) { unsigned char *p = (unsigned char *) d; const unsigned char *q = (const unsigned char *) s; while (n--) *p++ = *q++; return d; }
#else
#include <stdio.h>
#include <sys/mman.h>
static void out (const char *s) { fputs (s, stdout); }
static void *map (void *at, unsigned long n) { void *p = mmap (at, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | (at ? MAP_FIXED : 0), -1, 0); return p == MAP_FAILED ? 0 : p; }
#endif

static char *s_heap, *s_brk;
static void *al (unsigned long n) { void *p = s_brk; s_brk += (n + 15) & ~15ul; return p; }
static void fr (void *p) { (void) p; }
static int noGpu (char *b, unsigned n) { if (b && n) b[0] = 0; return 0; }
static unsigned ticks (void) { return 0; }

static void hex (unsigned v) { char b[10]; for (int i = 0; i < 8; i++) b[i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15]; b[8] = '\n'; b[9] = 0; out (b); }

static unsigned s_seed = 7;
static unsigned rnd (void) { s_seed = s_seed * 1103515245u + 12345u; return s_seed >> 8; }
static unsigned *tex (int w, int h, int kind)
{
	unsigned *p = (unsigned *) al ((unsigned long) w * h * 4);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			unsigned a = kind == 0 ? 255 : kind == 1 ? (unsigned) ((x * 3 + y * 5) & 255) : (((x >> 3) + (y >> 3)) & 1) ? 255 : 60;
			unsigned r = (unsigned) (x * 255 / w), g = (unsigned) (y * 255 / h), b = (rnd () & 255);
			p[y * w + x] = a << 24 | (r * a / 255) << 16 | (g * a / 255) << 8 | (b * a / 255);
		}
	return p;
}

int main (void)
{
	struct TKApiTable *T = (struct TKApiTable *) map ((void *) KAPI_TABLE_VA, 65536);
	s_heap = s_brk = (char *) map (0, 256ul << 20);
	if (T == 0 || s_heap == 0) { out ("no memory\n"); return 1; }
	T->version = KAPI_ABI_VERSION; T->gpu_info = noGpu; T->get_ticks = ticks;
	gpc_config cfg = { al, fr, GPC_F_CPU };
	gpc_ctx *g = gpc_create (&cfg);
	int W = 700, H = 500;
	unsigned *pic = (unsigned *) al ((unsigned long) W * H * 4);
	gpc_tex *t0 = gpc_tex_create (g, 800, 900, tex (800, 900, 0), 800);
	gpc_tex *t1 = gpc_tex_create (g, 300, 200, tex (300, 200, 1), 300);
	gpc_tex *t2 = gpc_tex_create (g, 128, 128, tex (128, 128, 2), 128);
	for (int sc = 0; sc < 8; sc++)
	{
		gpc_layer L[4];
		gpc_layer_init (&L[0], t0); L[0].src_y = sc * 13.25f; L[0].src_h = 500; L[0].flags = sc & 1 ? GPC_L_OPAQUE : 0;
		gpc_layer_init (&L[1], t1); gpc_matrix_translate (&L[1].m, 350, 250); gpc_matrix_rotate (&L[1].m, sc * 0.4f);
		gpc_matrix_translate (&L[1].m, -150, -100); L[1].opacity = 100 + sc * 20;
		gpc_layer_init (&L[2], t2); gpc_matrix_translate (&L[2].m, 20.5f + sc, 30); gpc_matrix_scale (&L[2].m, 1.0f + sc * 0.3f, 1.2f);
		L[2].flags = sc == 3 ? GPC_L_NEAREST : 0;
		gpc_layer_init (&L[3], t1); L[3].src_x = 10.5f; L[3].src_w = 200; gpc_matrix_translate (&L[3].m, 400.25f, 300); L[3].opacity = 128;
		L[3].clip[0] = 420; L[3].clip[1] = 310; L[3].clip[2] = 100; L[3].clip[3] = 90;
		gpc_target tg = { pic, W, H, W, sc == 5 ? GPC_T_ALPHA : 0u };
		for (int i = 0; i < W * H; i++) pic[i] = 0x11223344u * (unsigned) i;
		gpc_composite (g, &tg, L, 4, 0x80406020u, sc == 2 ? 0 : GPC_C_CLEAR);
		unsigned hsh = 2166136261u;
		for (int i = 0; i < W * H; i++) { hsh ^= pic[i]; hsh *= 16777619u; }
		hex (hsh);
	}
	return 0;
}

#ifdef __aarch64__
__asm__ (".global _start\n_start:\n\tbl main\n\tmov x8, #93\n\tsvc 0\n");
#endif
