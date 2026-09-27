//
// rdpd -- the window-level remote desktop server ("Onyx Remote"): the client (pc/OnyxRemote,
// .NET) shows each Onyx window as a window of its own on the PC -- its content in a native
// window, or with its Onyx frame (title bar, borders, close box) as on the Pi -- instead of
// a picture of the screen.
//   usage: rdpd [port]          (default 3390; e.g. `rdpd` in SD:/etc/autostart)
//          rdpd list            (the windows the kernel lists, as rdpd sees them: a check)
//
// Nothing is composited for it: the windows' own buffers are read (kapi v56 win_list /
// win_read), and a window is looked at only when it changed (its counter); then its
// 64 x 64 tiles are compared with what the client has and the changed ones are sent,
// compressed with LZ4 (fast: a fraction of zlib's time), in 32 or 16 bits a pixel (the
// client's choice). A window's frame is sent when the app redraws it. Moving, raising,
// overlapping windows costs no pixels. The pointer arrives in window coordinates and is put
// back on the Pi's screen here (the window's place + the point; the window is raised first
// when clicked), the keys as X11 keysyms (remotekeys.h, as vncd).
// One client at a time, NO password and no encryption -- trusted LAN only, like vncd.
//
// Protocol (TCP, little-endian). Hello: the server sends "ONYXRDP1", u16 screen w, h, u16 the
// kernel's kapi version (below 56: no window can be listed -- an old kernel image); the
// client answers "ONYXRDP1", u8 options (bit 0: 16-bit pixels, bit 1: no frames -- the client
// shows the contents in native windows). Then messages:
//   server -> client: u8 type, u32 length, payload
//     1 WIN     u32 id, s16 x y (client area on the Pi's screen), u16 w h (client area),
//               u16 ow oh il it (the whole window with its frame, the client area's place
//               in it; 0 = no frame), u32 flags (WIN_FLAG_*), u8 alpha, u8 state
//               (1 keyboard, 2 full screen), u8 n, title[n]
//     2 GONE    u32 id
//     3 ZORDER  u16 n, u32 id[n] (bottom to top)
//     4 PIXELS  u32 id, u8 part (0 content, 1 frame active, 2 frame inactive), u8 bpp
//               (32: B G R x, 16: RGB565), u8 lz4, u16 x y w h, the pixels (an LZ4 block
//               when lz4 = 1)
//     5 END     (one round of updates is complete: the client shows it, then asks again)
//   client -> server: u8 type, payload
//     1 READY   (send the next round)
//     2 PTR     u32 id, s16 x y (in the window's client area), u8 buttons (1 left, 2 right,
//               4 middle), s8 wheel
//     3 KEY     u8 flags (1 down, 2 held only: a letter / digit / space typed as CHAR, its
//               down / up only tracked for the games), u32 X11 keysym
//     6 CHAR    u32 the character typed (Latin-1; the PC's keyboard layout applied)
//     7 DESKTOP u8 on (send the desktop: the window KAPI_WIN_DESKTOP = 0xFFFFFFFF, the
//               wallpaper + the backmost windows, screen-sized; off by default)
//     4 RAISE   u32 id (the PC window got the focus: the Onyx window takes the keyboard)
//     5 CLOSE   u32 id (its close box)
//
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "kapi.h"
#include "remotekeys.h"

#define TILE		64
#define MAXWIN		20		// (the window manager's 16 + the desktop)
#define MIN_ROUND_TICKS	2		// >= 20 ms between rounds (<= 50 a second)
#define BUSY_FACTOR	2		// ... and twice the last round's time (core 0 kept for the apps)

static int g_sock, g_dead, g_W, g_H, g_bpp16, g_desktop, g_noFrames;

// ---- output -------------------------------------------------------------------------------

static unsigned char g_out[65536];
static int g_outlen;

static void flush_out (void)
{
	int off = 0;
	while (!g_dead && off < g_outlen)
	{
		int n = kapi_tcp_send (g_sock, g_out + off, (unsigned) (g_outlen - off));
		if (n <= 0) { g_dead = 1; break; }
		off += n;
	}
	g_outlen = 0;
}
static void put (const void *p, int n)
{
	const unsigned char *b = (const unsigned char *) p;
	while (n > 0)
	{
		if (g_outlen == (int) sizeof g_out) flush_out ();
		int k = (int) sizeof g_out - g_outlen;
		if (k > n) k = n;
		memcpy (g_out + g_outlen, b, (size_t) k);
		g_outlen += k; b += k; n -= k;
	}
}
static void put8 (unsigned v)  { unsigned char b = (unsigned char) v; put (&b, 1); }
static void put16 (unsigned v) { unsigned char b[2] = { (unsigned char) v, (unsigned char) (v >> 8) }; put (b, 2); }
static void put32 (unsigned v) { unsigned char b[4] = { (unsigned char) v, (unsigned char) (v >> 8), (unsigned char) (v >> 16), (unsigned char) (v >> 24) }; put (b, 4); }
static void msg (unsigned type, unsigned len) { put8 (type); put32 (len); }

// ---- input --------------------------------------------------------------------------------

static unsigned char g_in[4096];
static int g_inlen;

static int fill_in (void)
{
	if (g_inlen >= (int) sizeof g_in) return 1;
	int n = kapi_tcp_recv (g_sock, g_in + g_inlen, (unsigned) ((int) sizeof g_in - g_inlen));
	if (n < 0) return 0;
	g_inlen += n;
	return 1;
}
static void consume (int n) { memmove (g_in, g_in + n, (size_t) (g_inlen - n)); g_inlen -= n; }
static int need (int n)
{
	for (int t = 0; g_inlen < n; t++)
	{
		if (!fill_in () || t > 1000) return 0;
		if (g_inlen < n) kapi_msleep (10);
	}
	return 1;
}
static unsigned get16 (const unsigned char *p) { return (unsigned) p[0] | (unsigned) p[1] << 8; }
static unsigned get32 (const unsigned char *p) { return (unsigned) p[0] | (unsigned) p[1] << 8 | (unsigned) p[2] << 16 | (unsigned) p[3] << 24; }

// ---- LZ4 (the block format: tokens of literals + a match of >= 4 bytes back <= 64 KB) -----

static int g_ht[1 << 12];
static inline unsigned rd32 (const unsigned char *p) { unsigned v; memcpy (&v, p, 4); return v; }
static unsigned char *lz4_len (unsigned char *op, int n) { while (n >= 255) { *op++ = 255; n -= 255; } *op++ = (unsigned char) n; return op; }

// src (n bytes) -> dst (room: n + n / 255 + 16) -> the compressed size
static int lz4_compress (const unsigned char *src, int n, unsigned char *dst)
{
	unsigned char *op = dst;
	int ip = 0, anchor = 0, miss = 0;
	memset (g_ht, 0xFF, sizeof g_ht);
	while (ip < n - 12)
	{
		unsigned seq = rd32 (src + ip), h = (seq * 2654435761u) >> 20;
		int ref = g_ht[h];
		g_ht[h] = ip;
		if (ref < 0 || ip - ref > 65535 || rd32 (src + ref) != seq) { ip += 1 + (miss++ >> 5); continue; }
		miss = 0;
		int ml = 4, lim = n - 5;
		while (ip + ml < lim && src[ref + ml] == src[ip + ml]) ml++;
		int lit = ip - anchor;
		unsigned char *tok = op++;
		*tok = (unsigned char) ((lit < 15 ? lit : 15) << 4 | (ml - 4 < 15 ? ml - 4 : 15));
		if (lit >= 15) op = lz4_len (op, lit - 15);
		memcpy (op, src + anchor, (size_t) lit); op += lit;
		*op++ = (unsigned char) (ip - ref); *op++ = (unsigned char) ((ip - ref) >> 8);
		if (ml - 4 >= 15) op = lz4_len (op, ml - 4 - 15);
		ip += ml; anchor = ip;
	}
	int lit = n - anchor;
	*op++ = (unsigned char) ((lit < 15 ? lit : 15) << 4);
	if (lit >= 15) op = lz4_len (op, lit - 15);
	memcpy (op, src + anchor, (size_t) lit); op += lit;
	return (int) (op - dst);
}

// ---- the windows --------------------------------------------------------------------------

struct Win
{
	unsigned id, gen, chromeGen;
	struct kapi_win_info info;
	unsigned *prev, *cur; int bw, bh;	// the content the client has / as read now
	int sent;				// its WIN message went out
	int alive;
};
static struct Win g_win[MAXWIN];
static unsigned g_order[MAXWIN]; static int g_norder = -1;
static unsigned char *g_pack, *g_lz;		// a strip packed, then compressed
static int g_packCap;

static struct Win *find (unsigned id) { for (int i = 0; i < MAXWIN; i++) if (g_win[i].id == id) return &g_win[i]; return 0; }
static void drop (struct Win *w) { free (w->prev); free (w->cur); memset (w, 0, sizeof *w); }

// Send a rectangle of pixels (src, stride in pixels) of a window's part.
static void send_rect (unsigned id, int part, const unsigned *src, int stride, int x, int y, int w, int h)
{
	int bpp = g_bpp16 ? 2 : 4, n = w * h * bpp;
	if (n > g_packCap) return;
	unsigned char *d = g_pack;
	for (int j = 0; j < h; j++)
	{
		const unsigned *s = src + (size_t) (y + j) * stride + x;
		if (bpp == 4) { memcpy (d, s, (size_t) w * 4); d += w * 4; }
		else for (int i = 0; i < w; i++)
		{
			unsigned c = s[i];
			unsigned v = ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F);
			*d++ = (unsigned char) v; *d++ = (unsigned char) (v >> 8);
		}
	}
	int z = lz4_compress (g_pack, n, g_lz);
	int useLz = z < n;
	msg (4, 4 + 3 + 8 + (unsigned) (useLz ? z : n));
	put32 (id); put8 ((unsigned) part); put8 (g_bpp16 ? 16 : 32); put8 ((unsigned) useLz);
	put16 ((unsigned) x); put16 ((unsigned) y); put16 ((unsigned) w); put16 ((unsigned) h);
	put (useLz ? g_lz : g_pack, useLz ? z : n);
}

static void send_win (const struct kapi_win_info *I)
{
	int tn = (int) strlen (I->title);
	msg (1, 4 + 4 + 4 + 8 + 4 + 3 + (unsigned) tn);
	put32 (I->id); put16 ((unsigned) (short) I->x); put16 ((unsigned) (short) I->y);
	put16 ((unsigned) I->w); put16 ((unsigned) I->h);
	put16 ((unsigned) I->ow); put16 ((unsigned) I->oh); put16 ((unsigned) I->il); put16 ((unsigned) I->it);
	put32 (I->flags); put8 ((unsigned) I->alpha); put8 (I->state); put8 ((unsigned) tn); put (I->title, tn);
}

// The content: the tiles that changed since the client's copy, a row of tiles at a time
// (the changed ones side by side sent as one rectangle).
static void send_content (struct Win *w, int full)
{
	int W = w->info.w, H = w->info.h;
	if (W <= 0 || H <= 0) return;
	if (!w->prev || w->bw != W || w->bh != H)
	{
		free (w->prev); free (w->cur);
		w->prev = (unsigned *) malloc ((size_t) W * H * 4); w->cur = (unsigned *) malloc ((size_t) W * H * 4);
		w->bw = W; w->bh = H; full = 1;
		if (!w->prev || !w->cur) { free (w->prev); free (w->cur); w->prev = w->cur = 0; return; }
	}
	if (kapi_win_read (w->id, 0, 0, 0, W, H, w->cur, W) != 0) return;
	for (int ty = 0; ty < H; ty += TILE)
	{
		int th = H - ty < TILE ? H - ty : TILE, run = -1;
		for (int tx = 0; tx < W + TILE; tx += TILE)		// (one step past the edge: ends a run)
		{
			int changed = 0;
			if (tx < W)
			{
				int tw = W - tx < TILE ? W - tx : TILE;
				if (full) changed = 1;
				else for (int j = 0; j < th && !changed; j++)
					changed = memcmp (w->cur + (size_t) (ty + j) * W + tx, w->prev + (size_t) (ty + j) * W + tx, (size_t) tw * 4) != 0;
			}
			if (changed && run < 0) run = tx;
			if (!changed && run >= 0)
			{
				int rw = (tx < W ? tx : W) - run;
				send_rect (w->id, 0, w->cur, W, run, ty, rw, th);
				for (int j = 0; j < th; j++)
					memcpy (w->prev + (size_t) (ty + j) * W + run, w->cur + (size_t) (ty + j) * W + run, (size_t) rw * 4);
				run = -1;
			}
		}
	}
}

static void send_chrome (struct Win *w)
{
	int W = w->info.ow, H = w->info.oh;
	if (W <= 0 || H <= 0) return;
	unsigned *b = (unsigned *) malloc ((size_t) W * H * 4);
	if (!b) return;
	for (int part = 1; part <= 2; part++)
		if (kapi_win_read (w->id, part, 0, 0, W, H, b, W) == 0)
			for (int y = 0; y < H; y += TILE) send_rect (w->id, part, b, W, 0, y, W, H - y < TILE ? H - y : TILE);
	free (b);
}

// One round: what changed in the windows since the last one.
static void round_send (void)
{
	struct kapi_win_info L[MAXWIN];
	int n = kapi_win_list (L, MAXWIN);
	if (!g_desktop)							// (only when asked for)
	{
		int k = 0;
		for (int i = 0; i < n; i++) if (L[i].id != KAPI_WIN_DESKTOP) L[k++] = L[i];
		n = k;
	}
	for (int i = 0; i < MAXWIN; i++) g_win[i].alive = 0;
	for (int i = 0; i < n; i++)
	{
		struct Win *w = find (L[i].id);
		if (!w) { w = find (0); if (!w) continue; memset (w, 0, sizeof *w); w->id = L[i].id; }
		w->alive = 1;
		struct kapi_win_info old = w->info;
		w->info = L[i];
		int moved = !w->sent || old.x != L[i].x || old.y != L[i].y || old.w != L[i].w || old.h != L[i].h
			  || old.ow != L[i].ow || old.oh != L[i].oh || old.flags != L[i].flags || old.alpha != L[i].alpha
			  || old.state != L[i].state || strcmp (old.title, L[i].title) != 0;
		if (moved) send_win (&L[i]);
		int newChrome = !w->sent || w->chromeGen != L[i].chromeGen || old.ow != L[i].ow || old.oh != L[i].oh;
		if (newChrome && !g_noFrames) send_chrome (w);
		if (!w->sent || w->gen != L[i].gen) send_content (w, !w->sent);
		w->gen = L[i].gen; w->chromeGen = L[i].chromeGen; w->sent = 1;
	}
	for (int i = 0; i < MAXWIN; i++)
		if (g_win[i].id && !g_win[i].alive) { msg (2, 4); put32 (g_win[i].id); drop (&g_win[i]); }
	int same = n == g_norder;
	for (int i = 0; same && i < n; i++) same = g_order[i] == L[i].id;
	if (!same)
	{
		msg (3, 2 + 4 * (unsigned) n); put16 ((unsigned) n);
		for (int i = 0; i < n; i++) { put32 (L[i].id); g_order[i] = L[i].id; }
		g_norder = n;
	}
	msg (5, 0);
	flush_out ();
}

// ---- input: the pointer back on the Pi's screen -------------------------------------------

static unsigned g_btn;

static void pointer (unsigned id, int x, int y, unsigned buttons, int wheel)
{
	struct Win *w = find (id);
	if (!w) return;
	if (buttons && !g_btn && !(w->info.state & KAPI_WIN_KEYS) && !(w->info.flags & 4))	// (not the menu bar)
		kapi_win_raise (id);				// clicked: on top on the Pi too
	kapi_inject_pointer (w->info.x + x, w->info.y + y, buttons, wheel);
	g_btn = buttons;
}

// ---- one client ---------------------------------------------------------------------------

static void session (void)
{
	g_inlen = g_outlen = 0; g_dead = 0; g_desktop = 0; g_ctrl = 0; g_mods = 0; g_btn = 0; g_norder = -1;
	for (int i = 0; i < MAXWIN; i++) drop (&g_win[i]);
	put ("ONYXRDP1", 8); put16 ((unsigned) g_W); put16 ((unsigned) g_H); put16 (KT->version); flush_out ();
	if (!need (9) || memcmp (g_in, "ONYXRDP1", 8) != 0) return;
	g_bpp16 = g_in[8] & 1; g_noFrames = (g_in[8] & 2) != 0;
	consume (9);
	int ready = 0;
	unsigned last = kapi_get_ticks () - 100, wait = MIN_ROUND_TICKS;
	while (!g_dead)
	{
		if (!fill_in ()) break;
		for (;;)
		{
			if (g_inlen < 1) break;
			int t = g_in[0], len;
			if (t == 1) len = 1;
			else if (t == 2) len = 11;
			else if (t == 3) len = 6;
			else if (t == 4 || t == 5 || t == 6) len = 5;
			else if (t == 7) len = 2;
			else { g_dead = 1; break; }
			if (g_inlen < len) break;
			const unsigned char *m = g_in + 1;
			if (t == 1) ready = 1;
			else if (t == 2) pointer (get32 (m), (short) get16 (m + 4), (short) get16 (m + 6), m[8], (signed char) m[9]);
			else if (t == 3 && (m[0] & 2)) { int hc = held_code (get32 (m + 1)); if (hc) kapi_inject_key_held (hc, m[0] & 1); }
			else if (t == 3) key_event (m[0] & 1, get32 (m + 1));
			else if (t == 6) { unsigned c = get32 (m); char one[2] = { (char) c, 0 }; if (c > 0 && c < 256) kapi_inject_key (one); }
			else if (t == 4) { struct Win *w = find (get32 (m)); if (w && !(w->info.flags & 6)) kapi_win_raise (get32 (m)); }
			else if (t == 5) kapi_win_close (get32 (m));
			else if (t == 7) g_desktop = m[0] != 0;
			consume (len);
		}
		if (g_dead) break;
		unsigned now = kapi_get_ticks ();
		if (ready && (int) (now - last) >= (int) wait)
		{
			ready = 0;
			round_send ();
			unsigned took = kapi_get_ticks () - now;
			wait = took * BUSY_FACTOR > MIN_ROUND_TICKS ? took * BUSY_FACTOR : MIN_ROUND_TICKS;
			last = now;
		}
		kapi_msleep (5);
	}
	if (g_btn) kapi_inject_pointer (0, 0, 0, 0);		// (no button left held)
}

int main (void)
{
	char args[64];
	kapi_get_args (args, sizeof args);
	unsigned port = 0;
	for (int i = 0; args[i] >= '0' && args[i] <= '9'; i++) port = port * 10 + (unsigned) (args[i] - '0');
	if (port == 0 || port > 65535) port = 3390;
	kapi_screen_size (&g_W, &g_H);
	if (args[0] == 'l')					// rdpd list
	{
		struct kapi_win_info L[MAXWIN];
		int n = kapi_win_list (L, MAXWIN);
		printf ("kapi v%u, screen %d x %d, %d windows listed:\n", KT->version, g_W, g_H, n);
		for (int i = 0; i < n; i++)
			printf ("  %08X pid %u  %d,%d %dx%d  frame %dx%d  flags %X alpha %d gen %u state %u  %s\n", L[i].id, L[i].pid,
				L[i].x, L[i].y, L[i].w, L[i].h, L[i].ow, L[i].oh, L[i].flags, L[i].alpha, L[i].gen, L[i].state, L[i].title);
		if (n > 0)
		{
			unsigned px[16];
			int r = kapi_win_read (L[n - 1].id, 0, 0, 0, 4, 4, px, 4);
			printf ("read of the top one: %d, first pixel %06X\n", r, r == 0 ? px[0] : 0);
		}
		return 0;
	}
	g_packCap = (g_W > 2048 ? g_W : 2048) * TILE * 4;
	g_pack = (unsigned char *) malloc ((size_t) g_packCap);
	g_lz = (unsigned char *) malloc ((size_t) g_packCap + g_packCap / 255 + 64);
	if (!g_pack || !g_lz) { kapi_stdout_write ("rdpd: out of memory\n", 20); return 1; }

	char ip[32];
	while (!kapi_net_status (ip, sizeof ip)) kapi_msleep (1000);
	int lsock = kapi_tcp_listen (port);
	if (lsock < 0) { kapi_stdout_write ("rdpd: cannot listen\n", 20); return 1; }
	for (;;)
	{
		char peer[32];
		g_sock = kapi_tcp_accept (lsock, peer, sizeof peer);
		if (g_sock < 0) { kapi_msleep (500); continue; }
		session ();
		kapi_tcp_close (g_sock);
	}
}
