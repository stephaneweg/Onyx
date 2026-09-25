//
// vncd -- remote desktop: a minimal VNC (RFB 3.8) server for the Onyx screen.
//   usage: vncd [port]          (default 5900; e.g. `vncd` in SD:/etc/autostart)
//
// Any VNC viewer works (TigerVNC, RealVNC, UltraVNC, TightVNC, Remmina...): connect to
// <pi-ip> (display :0 / port 5900). One client at a time, NO password ("None"
// security) and no encryption -- trusted LAN only, like telnetd.
//
// Screen: kapi_screen_grab (ABI v38) composites the desktop into our buffer; it is
// cut into 64x64 tiles and only the tiles that changed since the last update are sent
// (so an idle desktop costs nothing), in the client's pixel format, with the standard
// Zlib encoding when the viewer offers it (else Raw). Input: pointer and key events
// are injected with kapi_inject_pointer / kapi_inject_key -- the same path as the USB
// mouse and keyboard. Keys arrive as X11 keysyms (already shifted by the viewer's own
// layout) and are turned into the cooked key strings Onyx apps receive.
//
// newlib program (malloc for the two screen buffers) linked with zlib.
//
#include <stdlib.h>
#include <string.h>
#include "zlib.h"
#include "kapi.h"

#define TILE		64
#define MIN_FRAME_TICKS	5		// >= 50 ms between screen grabs (<= 20 updates/s)

static int  g_sock;
static int  g_W, g_H;
static unsigned *g_cur, *g_prev;	// current + last-sent screen (0x00RRGGBB)

// ---- output buffering -------------------------------------------------------------

static unsigned char g_out[32768];
static int g_outlen, g_dead;

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
static void put16 (unsigned v) { unsigned char b[2] = { (unsigned char) (v >> 8), (unsigned char) v }; put (b, 2); }
static void put32 (unsigned v) { unsigned char b[4] = { (unsigned char) (v >> 24), (unsigned char) (v >> 16), (unsigned char) (v >> 8), (unsigned char) v }; put (b, 4); }

// ---- input: blocking read of exactly n bytes (with the socket's non-blocking recv) ----

static unsigned char g_in[4096];
static int g_inlen;

// Pull whatever is available into g_in. Returns 0 if the peer is gone.
static int fill_in (void)
{
	if (g_inlen >= (int) sizeof g_in) return 1;
	int n = kapi_tcp_recv (g_sock, g_in + g_inlen, (unsigned) ((int) sizeof g_in - g_inlen));
	if (n < 0) return 0;
	g_inlen += n;
	return 1;
}
static void consume (int n) { memmove (g_in, g_in + n, (size_t) (g_inlen - n)); g_inlen -= n; }

// Wait until n bytes are buffered (handshake only). 0 on disconnect / 10 s timeout.
static int need (int n)
{
	for (int t = 0; g_inlen < n; t++)
	{
		if (!fill_in () || t > 1000) return 0;
		if (g_inlen < n) kapi_msleep (10);
	}
	return 1;
}
static unsigned get16 (const unsigned char *p) { return ((unsigned) p[0] << 8) | p[1]; }
static unsigned get32 (const unsigned char *p) { return ((unsigned) p[0] << 24) | ((unsigned) p[1] << 16) | ((unsigned) p[2] << 8) | p[3]; }

// ---- client pixel format ----------------------------------------------------------

static struct
{
	int bpp, bigendian, truecolor;
	unsigned rmax, gmax, bmax, rshift, gshift, bshift;
} g_pf;

static void pf_default (void)
{
	g_pf.bpp = 32; g_pf.bigendian = 0; g_pf.truecolor = 1;
	g_pf.rmax = g_pf.gmax = g_pf.bmax = 255;
	g_pf.rshift = 16; g_pf.gshift = 8; g_pf.bshift = 0;	// == our 0x00RRGGBB
}

static void put_pf (void)
{
	unsigned char b[16] = { 32, 24, 0, 1, 0, 255, 0, 255, 0, 255, 16, 8, 0, 0, 0, 0 };
	put (b, 16);
}

// Convert one 0x00RRGGBB pixel to the client format, append its bytes to *pp.
static unsigned char *conv (unsigned char *p, unsigned c)
{
	unsigned r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
	unsigned v = ((r * g_pf.rmax + 127) / 255) << g_pf.rshift
		   | ((g * g_pf.gmax + 127) / 255) << g_pf.gshift
		   | ((b * g_pf.bmax + 127) / 255) << g_pf.bshift;
	int nb = g_pf.bpp / 8;
	for (int i = 0; i < nb; i++)
		p[i] = (unsigned char) (g_pf.bigendian ? v >> (8 * (nb - 1 - i)) : v >> (8 * i));
	return p + nb;
}

// ---- framebuffer updates -----------------------------------------------------------

static int g_use_zlib;
static z_stream g_z;
static unsigned char g_pix[TILE * TILE * 4];
static unsigned char g_zbuf[TILE * TILE * 4 + 1024];

static void send_rect (int x, int y, int w, int h)
{
	unsigned char *p = g_pix;
	for (int j = 0; j < h; j++)
	{
		const unsigned *row = g_cur + (y + j) * g_W + x;
		for (int i = 0; i < w; i++) p = conv (p, row[i]);
	}
	int n = (int) (p - g_pix);

	put16 ((unsigned) x); put16 ((unsigned) y); put16 ((unsigned) w); put16 ((unsigned) h);
	if (g_use_zlib)
	{
		put32 (6);					// Zlib encoding: one stream per connection
		g_z.next_in = g_pix; g_z.avail_in = (uInt) n;
		g_z.next_out = g_zbuf; g_z.avail_out = sizeof g_zbuf;
		deflate (&g_z, Z_SYNC_FLUSH);
		unsigned zn = (unsigned) (sizeof g_zbuf - g_z.avail_out);
		put32 (zn); put (g_zbuf, (int) zn);
	}
	else
	{
		put32 (0);					// Raw
		put (g_pix, n);
	}
}

static int tile_changed (int x, int y, int w, int h)
{
	for (int j = 0; j < h; j++)
		if (memcmp (g_cur + (y + j) * g_W + x, g_prev + (y + j) * g_W + x, (size_t) w * 4) != 0)
			return 1;
	return 0;
}

// Send one FramebufferUpdate. full = the whole requested region, else only tiles that
// changed. Returns 1 if something was sent (0 = nothing changed, keep the request).
static int send_update (int full, int rx, int ry, int rw, int rh)
{
	static unsigned char dirty[(4096 / TILE) * (4096 / TILE)];
	int tx0 = rx / TILE, ty0 = ry / TILE;
	int tx1 = (rx + rw + TILE - 1) / TILE, ty1 = (ry + rh + TILE - 1) / TILE;
	int count = 0, ncols = (g_W + TILE - 1) / TILE;

	for (int ty = ty0; ty < ty1; ty++)
		for (int tx = tx0; tx < tx1; tx++)
		{
			int x = tx * TILE, y = ty * TILE;
			int w = g_W - x < TILE ? g_W - x : TILE, h = g_H - y < TILE ? g_H - y : TILE;
			int d = full || tile_changed (x, y, w, h);
			dirty[ty * ncols + tx] = (unsigned char) d;
			count += d;
		}
	if (count == 0) return 0;

	put8 (0); put8 (0); put16 ((unsigned) count);		// FramebufferUpdate
	for (int ty = ty0; ty < ty1; ty++)
		for (int tx = tx0; tx < tx1; tx++)
		{
			if (!dirty[ty * ncols + tx]) continue;
			int x = tx * TILE, y = ty * TILE;
			int w = g_W - x < TILE ? g_W - x : TILE, h = g_H - y < TILE ? g_H - y : TILE;
			send_rect (x, y, w, h);
			for (int j = 0; j < h; j++)			// the client now has this tile
				memcpy (g_prev + (y + j) * g_W + x, g_cur + (y + j) * g_W + x, (size_t) w * 4);
		}
	flush_out ();
	return 1;
}

// ---- input events ------------------------------------------------------------------

static int g_ctrl;
static unsigned g_btn;			// Onyx button mask of the last pointer event
static int g_px, g_py;

static void key_event (int down, unsigned sym)
{
	if (sym == 0xFFE3 || sym == 0xFFE4) { g_ctrl = down; return; }	// Control L/R
	if (!down) return;

	const char *s = 0;
	char one[2] = { 0, 0 };
	switch (sym)
	{
	case 0xFF08: s = "\b"; break;			// BackSpace
	case 0xFF09: s = "\t"; break;			// Tab
	case 0xFF0D: case 0xFF8D: s = "\n"; break;	// Return / KP_Enter
	case 0xFF1B: s = "\x1b"; break;			// Escape
	case 0xFFFF: case 0xFF9F: s = "\x1b[3~"; break;	// Delete
	case 0xFF50: case 0xFF95: s = "\x1b[1~"; break;	// Home
	case 0xFF57: case 0xFF9C: s = "\x1b[4~"; break;	// End
	case 0xFF55: case 0xFF9A: s = "\x1b[5~"; break;	// Page Up
	case 0xFF56: case 0xFF9B: s = "\x1b[6~"; break;	// Page Down
	case 0xFF51: case 0xFF96: s = "\x1b[D"; break;	// Left
	case 0xFF52: case 0xFF97: s = "\x1b[A"; break;	// Up
	case 0xFF53: case 0xFF98: s = "\x1b[C"; break;	// Right
	case 0xFF54: case 0xFF99: s = "\x1b[B"; break;	// Down
	default:
		if (sym >= 0xFFB0 && sym <= 0xFFB9) sym = '0' + (sym - 0xFFB0);	// keypad digits
		else if (sym == 0xFFAA) sym = '*'; else if (sym == 0xFFAB) sym = '+';
		else if (sym == 0xFFAD) sym = '-'; else if (sym == 0xFFAE) sym = '.';
		else if (sym == 0xFFAF) sym = '/';
		if (sym >= 0x20 && sym <= 0xFF)		// ASCII + Latin-1
		{
			unsigned c = sym;
			if (g_ctrl && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) c &= 0x1F;
			one[0] = (char) c; s = one;
		}
	}
	if (s) kapi_inject_key (s);
}

static void pointer_event (unsigned mask, int x, int y)
{
	// RFB: bit0 left, bit1 middle, bit2 right, bit3 wheel up, bit4 wheel down.
	unsigned b = ((mask & 1) ? 1u : 0) | ((mask & 4) ? 2u : 0) | ((mask & 2) ? 4u : 0);
	int wheel = (mask & 8) ? 1 : (mask & 16) ? -1 : 0;
	if (b != g_btn || x != g_px || y != g_py || wheel)
		kapi_inject_pointer (x, y, b, wheel);
	g_btn = b; g_px = x; g_py = y;
}

// ---- one client ---------------------------------------------------------------------

static int handshake (void)
{
	put ("RFB 003.008\n", 12); flush_out ();
	if (!need (12)) return 0;
	int minor = (g_in[8] - '0') * 100 + (g_in[9] - '0') * 10 + (g_in[10] - '0');
	consume (12);

	if (minor >= 7) { put8 (1); put8 (1); }		// 1 security type: None
	else		put32 (1);			// RFB 3.3: server decides: None
	flush_out ();
	if (minor >= 7)
	{
		if (!need (1)) return 0;
		consume (1);				// client's choice (None)
		if (minor >= 8) { put32 (0); flush_out (); }	// SecurityResult OK
	}

	if (!need (1)) return 0;			// ClientInit (shared flag)
	consume (1);

	static const char name[] = "Onyx";
	put16 ((unsigned) g_W); put16 ((unsigned) g_H); put_pf ();
	put32 (sizeof name - 1); put (name, sizeof name - 1);
	flush_out ();
	return !g_dead;
}

static void session (void)
{
	g_inlen = 0; g_outlen = 0; g_dead = 0; g_ctrl = 0; g_btn = 0; g_px = g_py = 0;
	g_use_zlib = 0;
	pf_default ();
	memset (&g_z, 0, sizeof g_z);
	if (deflateInit (&g_z, 1) != Z_OK) return;

	if (!handshake ()) { deflateEnd (&g_z); return; }

	int req = 0, req_full = 0, rx = 0, ry = 0, rw = 0, rh = 0;
	unsigned last_grab = kapi_get_ticks () - MIN_FRAME_TICKS;
	memset (g_prev, 0, (size_t) g_W * g_H * 4);

	while (!g_dead)
	{
		if (!fill_in ()) break;				// client closed

		// Parse every complete client message in the buffer.
		for (;;)
		{
			if (g_inlen < 1) break;
			int t = g_in[0], len = 0;
			if (t == 0) len = 20;					// SetPixelFormat
			else if (t == 2) { if (g_inlen < 4) break; len = 4 + 4 * (int) get16 (g_in + 2); }	// SetEncodings
			else if (t == 3) len = 10;				// FramebufferUpdateRequest
			else if (t == 4) len = 8;				// KeyEvent
			else if (t == 5) len = 6;				// PointerEvent
			else if (t == 6) { if (g_inlen < 8) break; len = 8 + (int) get32 (g_in + 4); }	// ClientCutText
			else { g_dead = 1; break; }				// unknown: drop the client
			if (t == 6 && len > (int) sizeof g_in)
			{
				// Oversized clipboard text: discard it as it streams in.
				int skip = len;
				while (skip > 0 && !g_dead)
				{
					int k = g_inlen < skip ? g_inlen : skip;
					consume (k); skip -= k;
					if (skip > 0) { if (!fill_in ()) g_dead = 1; else if (g_inlen == 0) kapi_msleep (5); }
				}
				continue;
			}
			if (g_inlen < len) break;
			const unsigned char *m = g_in;

			if (t == 0)
			{
				const unsigned char *f = m + 4;
				if (f[0] == 8 || f[0] == 16 || f[0] == 32)
				{
					g_pf.bpp = f[0]; g_pf.bigendian = f[2] != 0; g_pf.truecolor = f[3] != 0;
					g_pf.rmax = get16 (f + 4); g_pf.gmax = get16 (f + 6); g_pf.bmax = get16 (f + 8);
					g_pf.rshift = f[10]; g_pf.gshift = f[11]; g_pf.bshift = f[12];
				}
				if (!g_pf.truecolor) pf_default ();		// no colour maps: keep ours
				req_full = 1;
			}
			else if (t == 2)
			{
				g_use_zlib = 0;
				for (int i = 0; i < (int) get16 (m + 2); i++)
					if ((int) get32 (m + 4 + 4 * i) == 6) g_use_zlib = 1;
			}
			else if (t == 3)
			{
				req = 1;
				if (!m[1]) req_full = 1;
				rx = (int) get16 (m + 2); ry = (int) get16 (m + 4);
				rw = (int) get16 (m + 6); rh = (int) get16 (m + 8);
				if (rx >= g_W || ry >= g_H) { rx = ry = 0; rw = g_W; rh = g_H; }
				if (rx + rw > g_W) rw = g_W - rx;
				if (ry + rh > g_H) rh = g_H - ry;
			}
			else if (t == 4) key_event (m[1], get32 (m + 4));
			else if (t == 5) pointer_event (m[1], (int) get16 (m + 2), (int) get16 (m + 4));
			consume (len);
		}
		if (g_dead) break;

		// Answer a pending update request once enough time has passed since the last
		// grab. An incremental request with no change stays pending (standard RFB).
		unsigned now = kapi_get_ticks ();
		if (req && now - last_grab >= MIN_FRAME_TICKS)
		{
			last_grab = now;
			// 2 = the screen has not changed since the previous grab: an incremental
			// request just stays pending (no diff / encode); a full one is answered.
			int g = kapi_screen_grab (g_cur, g_W, g_H);
			if ((g != 2 || req_full) && send_update (req_full, rx, ry, rw, rh)) { req = 0; req_full = 0; }
		}
		kapi_msleep (10);
	}
	deflateEnd (&g_z);
}

int main (void)
{
	char args[64];
	kapi_get_args (args, sizeof args);
	unsigned port = 0;
	for (int i = 0; args[i] >= '0' && args[i] <= '9'; i++) port = port * 10 + (unsigned) (args[i] - '0');
	if (port == 0 || port > 65535) port = 5900;

	kapi_screen_size (&g_W, &g_H);
	g_cur  = (unsigned *) malloc ((size_t) g_W * g_H * 4);
	g_prev = (unsigned *) malloc ((size_t) g_W * g_H * 4);
	if (!g_cur || !g_prev) { kapi_stdout_write ("vncd: out of memory\n", 20); return 1; }

	char ip[32];
	while (!kapi_net_status (ip, sizeof ip)) kapi_msleep (1000);

	int lsock = kapi_tcp_listen (port);
	if (lsock < 0) { kapi_stdout_write ("vncd: cannot listen\n", 20); return 1; }

	for (;;)
	{
		char peer[32];
		g_sock = kapi_tcp_accept (lsock, peer, sizeof peer);	// blocks
		if (g_sock < 0) { kapi_msleep (500); continue; }
		session ();
		kapi_tcp_close (g_sock);
	}
}
