// mock_rdpd.h -- host mock of the kapi and of UIKit's window API (uk_win_*) rdpd uses beyond mock_net_kapi.h: two windows (a framed
// 100 x 70 one, a borderless 50 x 20 one); from the 3rd list on, a pixel of the first changes
// (its counter too); the input rdpd injects is logged to stdout.
// MOCK_POCKET=1 (the environment): the graphics server is PocketUI's (pocket mode) and the windows are its: the
// shell's home (backmost), PocketUI's matte, an app's centred main window and its popup, the menu bar (topmost,
// see-through); MOCK_POCKET=2: the home alone (no app: home), the menu bar.
#ifndef MOCK_RDPD_H
#define MOCK_RDPD_H
#define MOD_CTRL 1
#define MOD_SHIFT 2
#define MOD_ALT 4
#define KEY_ENTER 13
#define KEY_UP 0x100
#define KEY_DOWN 0x101
#define KEY_LEFT 0x102
#define KEY_RIGHT 0x103
#define KAPI_WIN_KEYS 1
#define KAPI_WIN_FULLSCREEN 2
#define KAPI_WIN_MINIMISED 4
#define KAPI_WIN_OFFDESK 8
#define KAPI_WIN_DESKTOP 0xFFFFFFFFu
#define WIN_FLAG_BORDERLESS 1
#define WIN_FLAG_BACKMOST 2
#define WIN_FLAG_TOPMOST 4
#define WIN_FLAG_SYSTEM 16
#define WIN_FLAG_ALPHA 32
#define UK_MODE_DESKTOP 0
#define UK_MODE_POCKET 1
struct uk_win_server_info { unsigned size; char name[16]; int mode; int screen_w, screen_h; int work_x, work_y, work_w, work_h; int scale; int size_class; int reserved[8]; };
static inline int mock_pocket (void) { const char *e = getenv ("MOCK_POCKET"); return e ? atoi (e) : 0; }
static inline int uk_win_server (struct uk_win_server_info *o) { o->mode = mock_pocket () ? UK_MODE_POCKET : UK_MODE_DESKTOP; return 1; }
static inline unsigned kapi_clock_us (void) { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return (unsigned) (t.tv_sec * 1000000ull + t.tv_nsec / 1000); }
static inline int kapi_write (int fd, const void *b, unsigned n) { (void) fd; fprintf (stderr, "%.*s\n", (int) n, (const char *) b); return (int) n; }
struct kapi_win_info { unsigned id, pid; int x, y, w, h; unsigned flags; int alpha; unsigned gen, state; char title[48]; int ow, oh, il, it; unsigned chromeGen; };
static int mock_lists;
static unsigned mock_px (unsigned id, int part, int x, int y)
{
	if (mock_lists >= 3 && id == 7 && part == 0 && x == 70 && y == 10) return 0x00FF00FF;	// (the change)
	if (part) return (unsigned) (0x202020 + part * 0x100000 + (x & 7));
	return id == 7 ? (unsigned) ((x * 2) << 16 | (y * 3) << 8 | ((x ^ y) & 0xFF)) : 0x00123456;
}
static inline int uk_win_list (struct kapi_win_info *o, int max)
{
	mock_lists++;
	if (mock_pocket ())		// home 20 (shell 5), matte 21 (PocketUI 2), app 22 + its popup 23 (pid 3), menu bar 24 (pid 6)
	{
		struct kapi_win_info W[5] = {
			{ 20, 5, 0, 30, 50, 20, 2 | 1 | 16, 255, 1, 0, "pocketshell", 0, 0, 0, 0, 0 },
			{ 21, 2, 0, 30, 50, 20, 1 | 16, 255, 1, 0, "matte", 0, 0, 0, 0, 0 },
			{ 22, 3, 10, 35, 30, 10, 1, 255, 1, 1, "App", 0, 0, 0, 0, 0 },
			{ 23, 3, 12, 40, 8, 4, 1, 255, 1, 0, "popup", 0, 0, 0, 0, 0 },
			{ 24, 6, 0, 0, 50, 30, 1 | 4 | 16 | 32, 255, 1, 0, "menubar", 0, 0, 0, 0, 0 } };
		int k = 0;
		for (int i = 0; i < 5 && k < max; i++)
			if (mock_pocket () == 1 || W[i].id == 20 || W[i].id == 24) o[k++] = W[i];
		return k;
	}
	struct kapi_win_info a = { 7, 3, 107, 132, 100, 70, 0, 255, mock_lists >= 3 ? 2u : 1u, 1, "Test A", 114, 109, 7, 32, 1 };
	struct kapi_win_info b = { 9, 4, 0, 0, 50, 20, 1 | 4, 255, 1, 0, "Bar", 0, 0, 0, 0, 0 };
	if (max < 2) return 0;
	o[0] = b; o[1] = a;
	return 2;
}
static inline int uk_win_read (unsigned id, int part, int x, int y, int w, int h, unsigned *dst, int stride)
{
	int W = id == 7 ? (part ? 114 : 100) : 50, H = id == 7 ? (part ? 109 : 70) : 20;
	if (id != 7 && part) return -1;
	if (id >= 20) { W = 50; H = 30; }
	for (int j = 0; j < h && y + j < H; j++) for (int i = 0; i < w && x + i < W; i++) dst[j * stride + i] = mock_px (id, part, x + i, y + j);
	return 0;
}
static inline int uk_win_raise (unsigned id) { printf ("RAISE %u\n", id); fflush (stdout); return 0; }
static inline int uk_win_close (unsigned id) { printf ("CLOSE %u\n", id); fflush (stdout); return 0; }
static inline int uk_win_place (unsigned id, int x, int y) { printf ("MOVE %u %d %d\n", id, x, y); fflush (stdout); return 0; }
static inline int uk_win_cursor_shown (void) { return -1; }
static inline void kapi_inject_pointer (int x, int y, unsigned b, int w) { printf ("PTR %d %d %u %d\n", x, y, b, w); fflush (stdout); }
static inline void kapi_inject_key (const char *s) { printf ("KEY %d\n", (unsigned char) s[0]); fflush (stdout); }
static inline void kapi_inject_key_held (int k, int d) { printf ("HELD %d %d\n", k, d); fflush (stdout); }
static inline void kapi_inject_modifiers (unsigned m) { printf ("MODS %u\n", m); fflush (stdout); }
struct mock_kt { unsigned version; }; static const struct mock_kt mock_kt_ = { 56 };
#define KT (&mock_kt_)
static inline void kapi_screen_size (int *w, int *h) { *w = 1024; *h = 768; }
static inline unsigned kapi_abi_version (void) { return KT->version; }
static inline int kapi_thread_create (int (*fn) (void *), void *arg, unsigned stack, const char *name) { (void) fn; (void) arg; (void) stack; (void) name; return -3; }	// (no threads: rdpd accepts in its loop)
#endif
