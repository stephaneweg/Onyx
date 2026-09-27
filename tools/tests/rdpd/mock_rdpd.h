// mock_rdpd.h -- host mock of the kapi rdpd uses beyond mock_net_kapi.h: two windows (a framed
// 100 x 70 one, a borderless 50 x 20 one); from the 3rd list on, a pixel of the first changes
// (its counter too); the input rdpd injects is logged to stdout.
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
struct kapi_win_info { unsigned id, pid; int x, y, w, h; unsigned flags; int alpha; unsigned gen, state; char title[48]; int ow, oh, il, it; unsigned chromeGen; };
static int mock_lists;
static unsigned mock_px (unsigned id, int part, int x, int y)
{
	if (mock_lists >= 3 && id == 7 && part == 0 && x == 70 && y == 10) return 0x00FF00FF;	// (the change)
	if (part) return (unsigned) (0x202020 + part * 0x100000 + (x & 7));
	return id == 7 ? (unsigned) ((x * 2) << 16 | (y * 3) << 8 | ((x ^ y) & 0xFF)) : 0x00123456;
}
static inline int kapi_win_list (struct kapi_win_info *o, int max)
{
	mock_lists++;
	struct kapi_win_info a = { 7, 3, 107, 132, 100, 70, 0, 255, mock_lists >= 3 ? 2u : 1u, 1, "Test A", 114, 109, 7, 32, 1 };
	struct kapi_win_info b = { 9, 4, 0, 0, 50, 20, 1 | 4, 255, 1, 0, "Bar", 0, 0, 0, 0, 0 };
	if (max < 2) return 0;
	o[0] = b; o[1] = a;
	return 2;
}
static inline int kapi_win_read (unsigned id, int part, int x, int y, int w, int h, unsigned *dst, int stride)
{
	int W = id == 7 ? (part ? 114 : 100) : 50, H = id == 7 ? (part ? 109 : 70) : 20;
	if (id != 7 && part) return -1;
	for (int j = 0; j < h && y + j < H; j++) for (int i = 0; i < w && x + i < W; i++) dst[j * stride + i] = mock_px (id, part, x + i, y + j);
	return 0;
}
static inline int kapi_win_raise (unsigned id) { printf ("RAISE %u\n", id); fflush (stdout); return 0; }
static inline int kapi_win_close (unsigned id) { printf ("CLOSE %u\n", id); fflush (stdout); return 0; }
static inline void kapi_inject_pointer (int x, int y, unsigned b, int w) { printf ("PTR %d %d %u %d\n", x, y, b, w); fflush (stdout); }
static inline void kapi_inject_key (const char *s) { printf ("KEY %d\n", (unsigned char) s[0]); fflush (stdout); }
static inline void kapi_inject_key_held (int k, int d) { printf ("HELD %d %d\n", k, d); fflush (stdout); }
static inline void kapi_inject_modifiers (unsigned m) { printf ("MODS %u\n", m); fflush (stdout); }
static inline void kapi_screen_size (int *w, int *h) { *w = 1024; *h = 768; }
#endif
