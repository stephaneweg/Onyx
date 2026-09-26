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
static void (*host_tick_hook) (unsigned ticks) = 0;	// a test's script: keys, screenshots
static void h_msleep (unsigned ms) { host_ticks += ms / 10 + 1; if (host_tick_hook) host_tick_hook (host_ticks); }
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

static void *h_memcpy (void *d, const void *s, unsigned long n) { return memcpy (d, s, n); }
static void *h_memmove (void *d, const void *s, unsigned long n) { return memmove (d, s, n); }
static void *h_memset (void *d, int c, unsigned long n) { return memset (d, c, n); }

// Windows / full screen: plain heap buffers the test can dump.
static unsigned *host_win = 0; static int host_ww, host_wh;
static unsigned *h_create_window (int w, int h, const char *) { host_ww = w; host_wh = h; host_win = (unsigned *) calloc ((size_t) w * h, 4); return host_win; }
static unsigned *h_resize_window (int w, int h) { free (host_win); return h_create_window (w, h, 0); }
static unsigned *host_fs = 0; static int host_fsw = 800, host_fsh = 480;
static unsigned *h_fullscreen_begin (int *w, int *h) { *w = host_fsw; *h = host_fsh; host_fs = (unsigned *) calloc ((size_t) host_fsw * host_fsh, 4); return host_fs; }
static int h_get_datetime (int *y, int *mo, int *d, int *h, int *mi, int *s) { if (y) *y = 2026; if (mo) *mo = 9; if (d) *d = 26; if (h) *h = 12; if (mi) *mi = 0; if (s) *s = 0; return 1; }
// Text: the kernel font is not here -- a hook the test sets (the wtk font).
static void (*host_text_hook) (unsigned *, int, int, int, int, const char *, unsigned) = 0;
static void h_draw_text_buf (unsigned *d, int w, int h, int x, int y, const char *s, unsigned c) { if (host_text_hook) host_text_hook (d, w, h, x, y, s, c); }

// The app list (the .app folders of the sdcard) and a fixed set of open windows.
#include <dirent.h>
static int h_list_apps (char *b, unsigned cap)
{
	char d[512]; snprintf (d, sizeof d, "%s/apps", host_sd);
	DIR *dir = opendir (d); unsigned n = 0; b[0] = 0;
	if (!dir) return 0;
	struct dirent *e;
	while ((e = readdir (dir)))
	{
		int l = (int) strlen (e->d_name);
		if (l < 5 || strcmp (e->d_name + l - 4, ".app")) continue;
		if (n + l < cap - 2) { memcpy (b + n, e->d_name, l - 4); n += l - 4; b[n++] = '\n'; b[n] = 0; }
	}
	closedir (dir);
	return (int) n;
}
static const char *host_windows = "menubar\ntinypad\narkanoid\nqbasic\n";
static int h_list_windows (char *b, unsigned cap) { snprintf (b, cap, "%s", host_windows); return (int) strlen (b); }

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
	t->get_ticks = h_ticks; t->msleep = h_msleep; t->font_width = h_fw; t->font_height = h_fh;
	t->sbrk = h_sbrk; t->key_held = h_key_held;
	t->memcpy = (decltype (t->memcpy)) h_memcpy; t->memmove = (decltype (t->memmove)) h_memmove; t->memset = (decltype (t->memset)) h_memset;
	t->create_window = h_create_window; t->resize_window = h_resize_window; t->fullscreen_begin = h_fullscreen_begin;
	t->get_datetime = h_get_datetime; t->draw_text_buf = h_draw_text_buf;
	t->sound_acquire = h_sound_acquire; t->sound_start = h_sound_start;
	t->list_apps = h_list_apps; t->list_windows = h_list_windows;
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
