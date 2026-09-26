//
// host_kapi.h -- run wtk code on the PC: maps a fake kapi table at KAPI_TABLE_VA (every
// slot = a stub returning 0, the few the toolkit needs implemented: files under the
// sdcard/ folder, sbrk, ticks, font size, held keys), so a game's view can be built,
// ticked and painted, and its canvas saved as an image (tools/tests/run_games_test.sh).
//
#ifndef ONYX_HOST_KAPI_H
#define ONYX_HOST_KAPI_H
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kern/kapi_abi.h"

static const char *host_sd = "sdcard";
static unsigned host_ticks = 1000;
static int host_held[0x200];
static int host_sound_calls;

static long host_stub (void) { return 0; }
static void h_path (const char *p, char *o) { if (!strncmp (p, "SD:", 3)) p += 3; while (*p == '/') p++; snprintf (o, 512, "%s/%s", host_sd, p); }
static void *h_open (const char *p) { char o[512]; h_path (p, o); return fopen (o, "rb"); }
static int h_read (void *f, void *b, unsigned n) { return (int) fread (b, 1, n, (FILE *) f); }
static unsigned h_fsize (void *f) { long c = ftell ((FILE *) f); fseek ((FILE *) f, 0, SEEK_END); long n = ftell ((FILE *) f); fseek ((FILE *) f, c, SEEK_SET); return (unsigned) n; }
static void h_close (void *f) { fclose ((FILE *) f); }
static unsigned h_ticks (void) { return host_ticks; }
static int h_fw (void) { return 8; }
static int h_fh (void) { return 16; }
static void *h_sbrk (long inc)
{
	static char *arena = 0; static long used = 0;
	if (!arena) arena = (char *) malloc (256L << 20);
	if (used + inc > (256L << 20)) return (void *) -1;
	void *p = arena + used; used += inc; return p;
}
static int h_key_held (int k) { return k >= 0 && k < 0x200 ? host_held[k] : 0; }
static int h_sound_acquire (void) { return 1; }
static int h_sound_start (int, unsigned, int, int) { host_sound_calls++; return 0; }

static void host_kapi_init (const char *sdroot)
{
	if (sdroot) host_sd = sdroot;
	void *p = mmap ((void *) KAPI_TABLE_VA, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (p != (void *) KAPI_TABLE_VA) { perror ("mmap kapi table"); exit (1); }
	TKApiTable *t = (TKApiTable *) p;
	void **slot = (void **) ((char *) t + 8);
	for (unsigned i = 0; i < (sizeof (TKApiTable) - 8) / sizeof (void *); i++) slot[i] = (void *) host_stub;
	t->version = KAPI_ABI_VERSION;
	t->open = h_open; t->read = h_read; t->fsize = h_fsize; t->close = h_close;
	t->get_ticks = h_ticks; t->font_width = h_fw; t->font_height = h_fh;
	t->sbrk = h_sbrk; t->key_held = h_key_held;
	t->sound_acquire = h_sound_acquire; t->sound_start = h_sound_start;
}

// Save a 0x00RRGGBB canvas as a binary PPM.
static void host_save_ppm (const char *path, const unsigned *px, int w, int h, int stride)
{
	FILE *f = fopen (path, "wb");
	if (!f) return;
	fprintf (f, "P6\n%d %d\n255\n", w, h);
	for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
	{
		unsigned c = px[y * stride + x];
		unsigned char rgb[3] = { (unsigned char) (c >> 16), (unsigned char) (c >> 8), (unsigned char) c };
		fwrite (rgb, 1, 3, f);
	}
	fclose (f);
}
#endif
