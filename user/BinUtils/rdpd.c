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
// client's choice; the frames and the see-through windows -- WIN_FLAG_ALPHA: the menu bar and
// its menus, the dock, the notifications -- always in 32, their top byte a transparency). A
// window's frame is sent when the app redraws it; a hidden window's pixels (minimised, on
// another workspace) only when it is shown again. Moving, raising,
// overlapping windows costs no pixels. The pointer arrives in window coordinates and is put
// back on the Pi's screen here (the window's place + the point; the window is raised first
// when clicked), the keys as X11 keysyms (remotekeys.h, as vncd).
// One client at a time, NO password and no encryption -- trusted LAN only, like vncd.
//
// Protocol (TCP, little-endian). Hello: the server sends "ONYXRDP1", u16 screen w, h, u16 the
// kernel's kapi version (below 56: no window can be listed -- an old kernel image); the
// client answers "ONYXRDP1", u8 options (bit 0: 16-bit pixels, bit 1: no frames -- the client
// shows the contents in native windows, bit 2: pipelined -- see below). Then messages:
//   server -> client: u8 type, u32 length, payload
//     1 WIN     u32 id, s16 x y (client area on the Pi's screen), u16 w h (client area),
//               u16 ow oh il it (the whole window with its frame, the client area's place
//               in it; 0 = no frame), u32 flags (WIN_FLAG_*), u8 alpha, u8 state
//               (1 keyboard, 2 full screen, 4 minimised, 8 on another workspace: 4 and 8
//               not shown), u8 n, title[n]
//     2 GONE    u32 id
//     3 ZORDER  u16 n, u32 id[n] (bottom to top)
//     4 PIXELS  u32 id, u8 part (0 content, 1 frame active, 2 frame inactive), u8 bpp
//               (32: B G R T -- T the transparency, 0 opaque .. 255 see-through, heeded in
//               a WIN_FLAG_ALPHA window and in a frame's corners --, 16: RGB565), u8 lz4,
//               u16 x y w h, the pixels (an LZ4 block when lz4 = 1)
//     5 END     (one round of updates is complete: the client shows it, then asks again)
//     8 SCREEN  u16 w h (the screen's new size: kapi_screen_set, kernel v66; an older client
//               skips it)
//     9 CAPS    u8 protocol (1; 2: MOVE understood), u8 rounds in flight (the window: sent once, first, only to a
//               client that set option bit 2)
//    10 PING    u32 the server's clock in ms (between rounds, never inside one; a client
//               skips an unknown type, so an older one ignores it)
//    11 CURSOR  u8 the pointer's shape now (KAPI_CURSOR_*: 0 arrow, 1 hand, 2 text, 3 move, 4 .. 7
//               the size arrows, 8 cell, 9 cross, 10 wait, 11 no), sent when it changes, between
//               rounds -- the client shows its own pointer of that shape (uk_win_cursor_shown: known
//               only when Elegant, the graphics server, has the display)
//   client -> server: u8 type, payload
//     1 READY   (send the next round)
//     2 PTR     u32 id, s16 x y (in the window's client area: negative on its frame, e.g.
//               a title button), u8 buttons (1 left, 2 right, 4 middle), s8 wheel (the id
//               KAPI_WIN_DESKTOP: the screen's coordinates, the desktop sent or not)
//     3 KEY     u8 flags (1 down, 2 held only: a letter / digit / space typed as CHAR, its
//               down / up only tracked for the games), u32 X11 keysym
//     6 CHAR    u32 the character typed (Latin-1; the PC's keyboard layout applied)
//     7 DESKTOP u8 on (send the desktop: the window KAPI_WIN_DESKTOP = 0xFFFFFFFF, the
//               wallpaper + the backmost windows, screen-sized; off by default)
//     4 RAISE   u32 id (the PC window got the focus: the Onyx window takes the keyboard)
//     5 CLOSE   u32 id (its close box)
//     8 PONG    u32 the PING's value echoed (pipelined clients only: an older rdpd would
//               end the session on it)
//     9 MOVE    u32 id, s16 x y: the window's client area put there on the Pi's screen (its copy was
//               dragged on the PC; only to an rdpd whose CAPS said protocol 2: an older one would end
//               the session on it)
//
// Rounds and credits. A round is sent only when something changed (no empty rounds: an idle
// screen costs no traffic), and only while the server has credit: each READY gives one, each
// round (... END) takes one. Lock-step (an older client): its first READY, then one per END
// handled -- one round in flight. Pipelined (option bit 2): the server answers CAPS and counts
// PIPE_ROUNDS - 1 more credits itself; the client still sends one READY per END handled, so up
// to PIPE_ROUNDS rounds are in flight and one lost READY no longer stops the screen (its
// following READYs, once TCP has the hole filled, catch up).
// Loss recovery. Over a lossy link (Wi-Fi) a lost TCP segment is resent either after a timeout
// (Circle: 1 s at least, doubling; Windows: ~300 ms, doubling) or, much sooner, once the sender
// sees duplicate ACKs -- which only come when MORE data follows the lost segment. A lock-step
// protocol is the worst case (each side sends one small message, then waits), so when a round
// is in flight and nothing came back for PROBE_QUIET, rdpd sends a few small PINGs: if its own
// last segment was lost they arrive out of order and the PC's dup ACKs make Circle resend it at
// once; a pipelined client answers each with a PONG, whose segments do the same for a READY the
// PC lost (Windows resends after the Pi's dup ACKs). A pipelined client also gets a PING every
// 2 s while idle (it answers: the session is alive) and is dropped after SILENT_LIMIT without a
// byte (a half-open connection: the PC gone) -- so a reconnecting client is accepted soon.
//
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "appkit/appkit.h"
#include "uikit/win.h"		// the window API (UIKit's: uk_win_*)
#include "remotekeys.h"

// ---- diagnostics (kmsg, "app: rdpd ..." lines) ----------------------------------------------
// Every 5 s of a session: the rounds sent, the bytes, where the time went (reading and
// comparing the windows, compressing, sending -- kapi_tcp_send blocks while the network
// queue is full), the client's answer time (an END sent -> its READY back: the network's
// round trip + the PC's drawing), the input events; and at once a send, a round or a client
// answer much slower than it should be. To see why a session slows down.
static struct {
	unsigned t0, rounds, bytes, sends, partial, input;
	unsigned send_us, send_max, round_us, round_max, read_us, lz_us;
	unsigned ans_us, ans_max, ans_n, rect, px;
	unsigned stall_us, inflight_max, probes, ping_us, ping_max, ping_n, idle;
} g_st;
static void rdlog (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
static void rdlog (const char *fmt, ...)
{
	char b[128];
	va_list a; va_start (a, fmt);
	int n = vsnprintf (b, sizeof b, fmt, a);
	va_end (a);
	if (n > 0) kapi_write (2, b, (unsigned) (n < (int) sizeof b ? n : (int) sizeof b - 1));
}
// rounds in flight: the window (1 lock-step, PIPE_ROUNDS pipelined), the credit left, the
// send times of the ENDs not answered yet (oldest first: a READY answers the oldest)
#define PIPE_ROUNDS	3
#define PROBE_QUIET	250000u		// us: a round in flight and nothing heard -> probe
#define PROBE_FAST	4		// ... that many PINGs PROBE_STEP apart, then one every PROBE_SLOW
#define PROBE_STEP	50000u
#define PROBE_SLOW	500000u
#define IDLE_PING	2000000u	// us: a pipelined client idle -> a PING (liveness)
#define SILENT_LIMIT	12000000u	// us: a pipelined client silent that long -> the session ends
static int g_window, g_credit, g_endn;
static unsigned g_endq[PIPE_ROUNDS];
static unsigned g_lastRx, g_lastEnd, g_lastPing;
static int g_probes;			// PINGs since the client was last heard

static void stats_tick (int force)
{
	unsigned now = kapi_clock_us (), el = now - g_st.t0;
	if (!force && el < 5000000u) return;
	if (g_st.rounds || g_st.input || force)
	{
		unsigned ms = el / 1000 ? el / 1000 : 1;
		rdlog ("rdpd %us: %u rounds %uKB (%uKB/s) send %ums max %ums, %u partial; round %ums max %ums",
			ms / 1000, g_st.rounds, g_st.bytes / 1024, (unsigned) ((unsigned long) g_st.bytes * 1000 / ms / 1024),
			g_st.send_us / 1000, g_st.send_max / 1000, g_st.partial, g_st.round_us / 1000, g_st.round_max / 1000);
		rdlog ("rdpd   read+cmp %ums lz4 %ums, %u rects %uKpx; client answer avg %ums max %ums; %u input",
			g_st.read_us / 1000, g_st.lz_us / 1000, g_st.rect, g_st.px / 1000,
			g_st.ans_n ? g_st.ans_us / g_st.ans_n / 1000 : 0, g_st.ans_max / 1000, g_st.input);
		rdlog ("rdpd   credit %d of %d, in flight max %u, no credit %ums; %u idle checks, %u probes, ping avg %ums max %ums",
			g_credit, g_window, g_st.inflight_max, g_st.stall_us / 1000, g_st.idle, g_st.probes,
			g_st.ping_n ? g_st.ping_us / g_st.ping_n / 1000 : 0, g_st.ping_max / 1000);
	}
	memset (&g_st, 0, sizeof g_st);
	g_st.t0 = now;
}

#define TILE		64
#define MAXWIN		40		// (uk_win_list gives 37 at most: the desktop + 36 windows; Elegant has 64 since v94)
#define MIN_ROUND_TICKS	2		// >= 20 ms between rounds (<= 50 a second)
#define BUSY_FACTOR	2		// ... and twice the last round's time (core 0 kept for the apps)

static int g_sock, g_dead, g_W, g_H, g_bpp16, g_desktop, g_noFrames, g_pipe;
static volatile int g_next = -1;	// a new client accepted while a session runs (-1 none): acceptor ()
static int g_roundMsgs;			// messages in this round but its END (0: nothing changed)

// ---- output -------------------------------------------------------------------------------

static unsigned char g_out[65536];
static int g_outlen;

static void flush_out (void)
{
	int off = 0;
	while (!g_dead && off < g_outlen)
	{
		unsigned t = kapi_clock_us ();
		int n = kapi_tcp_send (g_sock, g_out + off, (unsigned) (g_outlen - off));
		t = kapi_clock_us () - t;
		g_st.sends++; g_st.send_us += t; if (t > g_st.send_max) g_st.send_max = t;
		if (t > 500000u) rdlog ("rdpd: a send of %d bytes took %u ms (-> %d)", g_outlen - off, t / 1000, n);
		if (n <= 0) { rdlog ("rdpd: send failed (%d): the session ends", n); g_dead = 1; break; }
		g_st.bytes += (unsigned) n;
		if (n < g_outlen - off)
		{
			// A short count: the send timed out (5 s with Circle's queue full) part-way. The
			// count is exact (the bytes queued, those of the kernel's earlier 32 KB requests and
			// Circle's earlier chunks included), so the rest is sent next. A send that queues
			// nothing at all fails (n <= 0, above): the session ends there.
			g_st.partial++;
			rdlog ("rdpd: a send stopped after %d of %d bytes (timed out?): the rest follows", n, g_outlen - off);
		}
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
static void msg (unsigned type, unsigned len) { if (type != 5 && type != 10 && type != 11) g_roundMsgs++; put8 (type); put32 (len); }

// ---- input --------------------------------------------------------------------------------

static unsigned char g_in[4096];
static int g_inlen;

static int fill_in (void)
{
	if (g_inlen >= (int) sizeof g_in) { g_lastRx = kapi_clock_us (); return 1; }	// (full: the client is talking)
	int n = kapi_tcp_recv (g_sock, g_in + g_inlen, (unsigned) ((int) sizeof g_in - g_inlen));
	if (n < 0) return 0;
	if (n > 0) { g_lastRx = kapi_clock_us (); g_probes = 0; }
	g_inlen += n;
	return 1;
}
static void consume (int n) { memmove (g_in, g_in + n, (size_t) (g_inlen - n)); g_inlen -= n; }
static int need (int n)
{
	for (int t = 0; g_inlen < n; t++)
	{
		if (!fill_in () || t > 1000) return 0;
		if (__atomic_load_n (&g_next, __ATOMIC_ACQUIRE) >= 0) return 0;	// (a newer client)
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
	unsigned flags;				// the flags it told (pocket_flags)
	int stale;				// its pixels not sent yet (new, or hidden until now)
	int alive;
};
static struct Win g_win[MAXWIN];
static unsigned g_order[MAXWIN]; static int g_norder = -1;
static unsigned char *g_pack, *g_lz;		// a strip packed, then compressed
static int g_packCap;

static struct Win *find (unsigned id) { for (int i = 0; i < MAXWIN; i++) if (g_win[i].id == id) return &g_win[i]; return 0; }
static void drop (struct Win *w) { free (w->prev); free (w->cur); memset (w, 0, sizeof *w); }

// Send a rectangle of pixels (src, stride in pixels) of a window's part: in 16 bits when the
// client asked for it, unless deep (a transparency in the top byte: 32).
static void send_rect (unsigned id, int part, const unsigned *src, int stride, int x, int y, int w, int h, int deep)
{
	int b16 = g_bpp16 && !deep, bpp = b16 ? 2 : 4, n = w * h * bpp;
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
	unsigned tz = kapi_clock_us ();
	int z = lz4_compress (g_pack, n, g_lz);
	g_st.lz_us += kapi_clock_us () - tz; g_st.rect++; g_st.px += (unsigned) (w * h);
	int useLz = z < n;
	msg (4, 4 + 3 + 8 + (unsigned) (useLz ? z : n));
	put32 (id); put8 ((unsigned) part); put8 (b16 ? 16 : 32); put8 ((unsigned) useLz);
	put16 ((unsigned) x); put16 ((unsigned) y); put16 ((unsigned) w); put16 ((unsigned) h);
	put (useLz ? g_lz : g_pack, useLz ? z : n);
}

// ---- PocketUI (the pocket and console modes): the flags told to the client ------------------
// Onyx Remote sends the PC's keys only from a child window of its own (RemoteWindow), and makes one only of a
// framed window: a borderless one becomes a see-through overlay or a picture on the desktop (a popup, the dock:
// never the keyboard's) and a backmost one is skipped (it is in the desktop's picture). Under PocketUI every app's
// main window is frameless (filled, or centred over a matte) and the shell's home is backmost -- so no window of
// the PC could take the keyboard and nothing typed was sent (the Pi's report on 2026.10.126: typing from Onyx
// Remote did nothing, in the apps and in the launcher; the mouse worked, the Pi's own keyboards too). There, rdpd
// tells the client:
//   - a program's MAIN window (its lowest borderless one, not topmost, not see-through, not a system one) as a
//     plain window without a frame: a child window that takes the focus and sends the keys -- its popups (above
//     it, borderless) stay overlays, the see-through ones (the menu bar, the shell's overlays, toasts) too;
//   - the shell's HOME (its backmost window) the same way while no app shows (the keys are the home's then);
//     behind an app it stays backmost (not shown by the client: the app covers it on the Pi);
//   - PocketUI's matte (its own borderless window under a centred app) a picture on the desktop (as it was) when
//     the client asked for the desktop, else not shown (an overlay would cover the app's window).
// Elegant's windows are told as they are. (The server asked every 2 s: the mode switched.)
static int g_pocket;			// the graphics server is PocketUI's (uk_win_server's mode)
static unsigned g_pocketAt;

static void pocket_poll (void)
{
	unsigned now = kapi_get_ticks ();
	if (g_pocketAt != 0 && now - g_pocketAt < 200) return;
	g_pocketAt = now | 1;
	struct uk_win_server_info si;
	memset (&si, 0, sizeof si);
	si.size = sizeof si;
	int was = g_pocket;
	g_pocket = uk_win_server (&si) > 0 && si.mode != UK_MODE_DESKTOP;
	if (g_pocket != was) rdlog ("rdpd: the graphics server is %s", g_pocket ? "PocketUI's: its frameless main windows and its home sent as plain ones (the keys)" : "the desktop's");
}

#define POCKET_SHOWN(I)	(!((I)->state & (KAPI_WIN_MINIMISED | KAPI_WIN_OFFDESK)))

// The flags told for each of the n windows L (bottom to top) -> F.
static void pocket_flags (const struct kapi_win_info *L, int n, unsigned *F)
{
	const unsigned B = WIN_FLAG_BORDERLESS, K = WIN_FLAG_BACKMOST;
	int app = 0;					// an app's window shows (not the home's, the matte, a band)
	for (int i = 0; i < n; i++)
	{
		F[i] = L[i].flags;
		if (L[i].id != KAPI_WIN_DESKTOP && POCKET_SHOWN (&L[i]) && !(L[i].flags & (WIN_FLAG_TOPMOST | WIN_FLAG_SYSTEM | K))) app = 1;
	}
	if (!g_pocket) return;
	for (int i = 0; i < n; i++)
	{
		unsigned f = L[i].flags;
		if (L[i].id == KAPI_WIN_DESKTOP || !(f & B) || (f & (WIN_FLAG_TOPMOST | WIN_FLAG_ALPHA))) continue;
		if (f & K)					// the shell's home
		{
			if (!app) F[i] = f & ~(B | K);
			continue;
		}
		if (f & WIN_FLAG_SYSTEM)			// PocketUI's matte
		{
			if (!g_desktop) F[i] = f | K;
			continue;
		}
		int lowest = 1;					// its program's main window: none of its own below it
		for (int j = 0; j < i && lowest; j++)
			if (L[j].pid == L[i].pid && L[j].id != KAPI_WIN_DESKTOP && POCKET_SHOWN (&L[j]) && (L[j].flags & B)
			    && !(L[j].flags & (WIN_FLAG_TOPMOST | WIN_FLAG_ALPHA | WIN_FLAG_SYSTEM | K))) lowest = 0;
		if (lowest) F[i] = f & ~B;
	}
}

// A program with the full screen (the servers list its window at 0, 0, the screen's size, no frame, state
// KAPI_WIN_FULLSCREEN -- an emulator, a BASIC game): the Pi shows it alone, so it is told alone, as a plain window
// without a frame that takes the keys -- the client made a window in the FULLSCREEN state a native framed one, a PC
// window with a title bar in the middle of the others (the user's report, 2026-10-08: "shown as a normal client
// window"); the others come back when it gives the screen back. -> how many windows are told.
static int full_only (struct kapi_win_info *L, int n)
{
	for (int i = n - 1; i >= 0; i--)
		if (L[i].id != KAPI_WIN_DESKTOP && (L[i].state & KAPI_WIN_FULLSCREEN))
		{
			L[0] = L[i];
			L[0].state &= ~KAPI_WIN_FULLSCREEN;
			L[0].x = L[0].y = 0;
			L[0].ow = L[0].oh = L[0].il = L[0].it = 0;
			L[0].flags = (L[0].flags & ~(WIN_FLAG_BORDERLESS | WIN_FLAG_TOPMOST | WIN_FLAG_BACKMOST | WIN_FLAG_ALPHA | WIN_FLAG_SYSTEM));
			return 1;
		}
	return n;
}

// Under PocketUI a system window (the matte, the shell's home) is never raised from the PC: the matte would cover
// the app (PocketUI keeps only the apps' windows in order).
static int raisable (const struct Win *w)
{
	return w != 0 && !(w->info.flags & (WIN_FLAG_BACKMOST | WIN_FLAG_TOPMOST)) && !(g_pocket && (w->info.flags & WIN_FLAG_SYSTEM));
}

static void send_win (const struct kapi_win_info *I, unsigned flags)
{
	int tn = (int) strlen (I->title);
	msg (1, 4 + 4 + 4 + 8 + 4 + 3 + (unsigned) tn);
	put32 (I->id); put16 ((unsigned) (short) I->x); put16 ((unsigned) (short) I->y);
	put16 ((unsigned) I->w); put16 ((unsigned) I->h);
	put16 ((unsigned) I->ow); put16 ((unsigned) I->oh); put16 ((unsigned) I->il); put16 ((unsigned) I->it);
	put32 (flags); put8 ((unsigned) I->alpha); put8 (I->state); put8 ((unsigned) tn); put (I->title, tn);
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
	unsigned tr = kapi_clock_us ();
	if (uk_win_read (w->id, 0, 0, 0, W, H, w->cur, W) != 0) return;
	g_st.read_us += kapi_clock_us () - tr;
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
				send_rect (w->id, 0, w->cur, W, run, ty, rw, th, (w->info.flags & WIN_FLAG_ALPHA) != 0);
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
		if (uk_win_read (w->id, part, 0, 0, W, H, b, W) == 0)
			for (int y = 0; y < H; y += TILE) send_rect (w->id, part, b, W, 0, y, W, H - y < TILE ? H - y : TILE, 1);
	free (b);
}

// One round: what changed in the windows since the last one -> 1 sent, 0 nothing changed
// (nothing sent: the credit is kept).
static int round_send (void)
{
	g_roundMsgs = 0;
	{								// the screen's new size (v66)
		int w = g_W, h = g_H;
		kapi_screen_size (&w, &h);
		if (w != g_W || h != g_H) { g_W = w; g_H = h; msg (8, 4); put16 ((unsigned) w); put16 ((unsigned) h); }
	}
	pocket_poll ();
	struct kapi_win_info L[MAXWIN];
	int n = uk_win_list (L, MAXWIN);
	if (!g_desktop)							// (only when asked for)
	{
		int k = 0;
		for (int i = 0; i < n; i++) if (L[i].id != KAPI_WIN_DESKTOP) L[k++] = L[i];
		n = k;
	}
	n = full_only (L, n);
	unsigned F[MAXWIN];
	pocket_flags (L, n, F);
	for (int i = 0; i < MAXWIN; i++) g_win[i].alive = 0;
	for (int i = 0; i < n; i++)
	{
		struct Win *w = find (L[i].id);
		if (!w) { w = find (0); if (!w) continue; memset (w, 0, sizeof *w); w->id = L[i].id; w->stale = 1; }
		w->alive = 1;
		struct kapi_win_info old = w->info;
		w->info = L[i];
		int moved = !w->sent || old.x != L[i].x || old.y != L[i].y || old.w != L[i].w || old.h != L[i].h
			  || old.ow != L[i].ow || old.oh != L[i].oh || old.flags != L[i].flags || old.alpha != L[i].alpha
			  || old.state != L[i].state || strcmp (old.title, L[i].title) != 0 || w->flags != F[i];
		if (moved) send_win (&L[i], F[i]);
		w->sent = 1; w->flags = F[i];
		if (L[i].state & (KAPI_WIN_MINIMISED | KAPI_WIN_OFFDESK)) { w->stale = 1; continue; }	// (not shown)
		int newChrome = w->stale || w->chromeGen != L[i].chromeGen || old.ow != L[i].ow || old.oh != L[i].oh;
		if (newChrome && !g_noFrames) send_chrome (w);
		if (w->stale || w->gen != L[i].gen) send_content (w, 0);
		w->gen = L[i].gen; w->chromeGen = L[i].chromeGen; w->stale = 0;
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
	if (g_roundMsgs == 0) { g_outlen = 0; return 0; }		// (nothing put: an empty round)
	msg (5, 0);
	flush_out ();
	g_lastEnd = kapi_clock_us ();
	if (g_endn < PIPE_ROUNDS) g_endq[g_endn++] = g_lastEnd;
	if ((unsigned) g_endn > g_st.inflight_max) g_st.inflight_max = (unsigned) g_endn;
	return 1;
}

// A round in flight and the client quiet: PINGs (see the top); a pipelined client idle: one
// every IDLE_PING.
static void keepalive (unsigned now)
{
	unsigned from = g_endn && (int) (g_lastEnd - g_lastRx) > 0 ? g_lastEnd : g_lastRx;
	int due = 0;
	if (g_endn && now - from >= PROBE_QUIET)
		due = g_probes == 0 || now - g_lastPing >= (g_probes < PROBE_FAST ? PROBE_STEP : PROBE_SLOW);
	else if (g_pipe && now - g_lastRx >= IDLE_PING)
		due = now - g_lastPing >= IDLE_PING;
	if (!due) return;
	msg (10, 4); put32 (now / 1000); flush_out ();
	g_lastPing = now; g_probes++;
	if (g_endn) g_st.probes++;
}

// The pointer's shape, looked at 20 times a second and sent when it changed (message 11).
#define CURSOR_POLL	50000u		// us
static int g_cursor = -1;		// the shape the client has (-1: none sent)
static unsigned g_cursorAt;

static void cursor_poll (unsigned now)
{
	if (now - g_cursorAt < CURSOR_POLL) return;
	g_cursorAt = now;
	int shape = uk_win_cursor_shown ();
	if (shape < 0 || shape == g_cursor) return;
	g_cursor = shape;
	msg (11, 1); put8 ((unsigned) shape); flush_out ();
}

// ---- input: the pointer back on the Pi's screen -------------------------------------------

static unsigned g_btn;
static unsigned char g_held[0x110];		// the held-key codes down (released when the session ends)

static void held (int code, int down)
{
	if (code <= 0 || code >= (int) sizeof g_held) return;
	g_held[code] = (unsigned char) (down != 0);
	kapi_inject_key_held (code, down);
}
static void release_all (void)
{
	for (int i = 1; i < (int) sizeof g_held; i++) if (g_held[i]) kapi_inject_key_held (i, 0);
	memset (g_held, 0, sizeof g_held);
	if (g_mods) { g_mods = 0; g_ctrl = 0; kapi_inject_modifiers (0); }
	if (g_btn) { kapi_inject_pointer (0, 0, 0, 0); g_btn = 0; }	// (no button left held)
}

static void pointer (unsigned id, int x, int y, unsigned buttons, int wheel)
{
	struct Win *w = find (id);
	if (!w && id != KAPI_WIN_DESKTOP) return;		// (the desktop: the screen, sent or not)
	if (w && id != KAPI_WIN_DESKTOP && buttons && !g_btn && !(w->info.state & KAPI_WIN_KEYS)
	    && !(w->info.flags & WIN_FLAG_TOPMOST) && raisable (w))	// (not the menu bar, the dock; PocketUI's matte)
		uk_win_raise (id);				// clicked: on top on the Pi too
	kapi_inject_pointer (w ? w->info.x + x : x, w ? w->info.y + y : y, buttons, wheel);
	g_btn = buttons;
}

// ---- one client ---------------------------------------------------------------------------

// A new client while a session runs (the PC reconnecting after a drop the Pi has not seen, a
// second PC): taken at once by an accepting thread and the session in course ended -- the new
// client wins. Without it a dead session (its peer gone without a word) held rdpd up to
// SILENT_LIMIT and the reconnections waited or were refused.
static char g_nextPeer[32];
static int g_lsock;

static int acceptor (void *arg)
{
	(void) arg;
	for (;;)
	{
		char peer[32];
		int s = kapi_tcp_accept (g_lsock, peer, sizeof peer);
		if (s < 0) { kapi_msleep (500); continue; }
		while (__atomic_load_n (&g_next, __ATOMIC_ACQUIRE) >= 0) kapi_msleep (20);	// (one waiting)
		memcpy (g_nextPeer, peer, sizeof peer);
		__atomic_store_n (&g_next, s, __ATOMIC_RELEASE);
	}
	return 0;
}

static void session (void)
{
	g_inlen = g_outlen = 0; g_dead = 0; g_desktop = 0; g_ctrl = 0; g_mods = 0; g_btn = 0; g_norder = -1; g_pocketAt = 0;
	memset (g_held, 0, sizeof g_held);
	for (int i = 0; i < MAXWIN; i++) drop (&g_win[i]);
	put ("ONYXRDP1", 8); put16 ((unsigned) g_W); put16 ((unsigned) g_H); put16 (kapi_abi_version ()); flush_out ();
	if (!need (9) || memcmp (g_in, "ONYXRDP1", 8) != 0) return;
	g_bpp16 = g_in[8] & 1; g_noFrames = (g_in[8] & 2) != 0; g_pipe = (g_in[8] & 4) != 0;
	consume (9);
	g_window = g_pipe ? PIPE_ROUNDS : 1;
	g_credit = g_window - 1;			// (+ the client's first READY)
	g_endn = 0; g_probes = 0;
	g_lastRx = g_lastEnd = g_lastPing = kapi_clock_us ();
	if (g_pipe) { msg (9, 2); put8 (2); put8 ((unsigned) g_window); flush_out (); }	// (protocol 2: MOVE)
	memset (&g_st, 0, sizeof g_st); g_st.t0 = kapi_clock_us ();
	rdlog ("rdpd: session start (%d bits a pixel%s, %s)", g_bpp16 ? 16 : 32, g_noFrames ? ", no frames" : "",
	       g_pipe ? "pipelined: 3 rounds in flight" : "lock-step: an older client");
	unsigned last = kapi_get_ticks () - 100, wait = MIN_ROUND_TICKS, loopAt = kapi_clock_us ();
	while (!g_dead)
	{
		if (!fill_in ()) { rdlog ("rdpd: the client closed the connection"); break; }
		for (;;)
		{
			if (g_inlen < 1) break;
			int t = g_in[0], len;
			if (t == 1) len = 1;
			else if (t == 2) len = 11;
			else if (t == 3) len = 6;
			else if (t == 4 || t == 5 || t == 6) len = 5;
			else if (t == 7) len = 2;
			else if (t == 8 && g_pipe) len = 5;
			else if (t == 9 && g_pipe) len = 9;
			else { rdlog ("rdpd: unknown message %d: the session ends", t); g_dead = 1; break; }
			if (g_inlen < len) break;
			const unsigned char *m = g_in + 1;
			if (t != 1 && t != 8) g_st.input++;
			if (t == 1)
			{
				if (g_credit < g_window) g_credit++;
				if (g_endn > 0)				// (it answers the oldest round in flight)
				{
					unsigned a = kapi_clock_us () - g_endq[0];
					g_st.ans_us += a; g_st.ans_n++; if (a > g_st.ans_max) g_st.ans_max = a;
					if (a > 2000000u) rdlog ("rdpd: the client answered after %u ms", a / 1000);
					memmove (g_endq, g_endq + 1, (size_t) (--g_endn) * sizeof g_endq[0]);
				}
			}
			else if (t == 8)				// PONG: the round trip, retransmissions included
			{
				unsigned a = (kapi_clock_us () / 1000 - get32 (m)) * 1000;
				if (a < 60000000u) { g_st.ping_us += a; g_st.ping_n++; if (a > g_st.ping_max) g_st.ping_max = a; }
			}
			else if (t == 2) pointer (get32 (m), (short) get16 (m + 4), (short) get16 (m + 6), m[8], (signed char) m[9]);
			else if (t == 3 && (m[0] & 2)) held (held_code (get32 (m + 1)), m[0] & 1);
			else if (t == 3)
			{
				int hc = held_code (get32 (m + 1));
				if (hc > 0 && hc < (int) sizeof g_held) g_held[hc] = (unsigned char) (m[0] & 1);
				key_event (m[0] & 1, get32 (m + 1));
			}
			else if (t == 6) { unsigned c = get32 (m); char one[2] = { (char) c, 0 }; if (c > 0 && c < 256) kapi_inject_key (one); }
			else if (t == 4) { struct Win *w = find (get32 (m)); if (raisable (w)) uk_win_raise (get32 (m)); }
			else if (t == 5) uk_win_close (get32 (m));
			else if (t == 9)				// MOVE: dragged on the PC, put there on the Pi too
			{
				struct Win *w = find (get32 (m));
				if (w && !(w->info.flags & 6)) uk_win_place (get32 (m), (short) get16 (m + 4), (short) get16 (m + 6));
			}
			else if (t == 7) g_desktop = m[0] != 0;
			consume (len);
		}
		if (g_dead) break;
		unsigned now = kapi_get_ticks (), us = kapi_clock_us ();
		if (g_credit == 0) g_st.stall_us += us - loopAt;
		loopAt = us;
		if (g_credit > 0 && (int) (now - last) >= (int) wait)
		{
			unsigned tr = kapi_clock_us ();
			int sent = round_send ();
			tr = kapi_clock_us () - tr;
			if (sent)
			{
				g_credit--;
				g_st.rounds++; g_st.round_us += tr; if (tr > g_st.round_max) g_st.round_max = tr;
				if (tr > 1000000u) rdlog ("rdpd: a round took %u ms", tr / 1000);
			}
			else g_st.idle++;
			unsigned took = kapi_get_ticks () - now;
			wait = took * BUSY_FACTOR > MIN_ROUND_TICKS ? took * BUSY_FACTOR : MIN_ROUND_TICKS;
			last = now;
		}
		if (g_dead) break;
		if (__atomic_load_n (&g_next, __ATOMIC_ACQUIRE) >= 0)
		{
			rdlog ("rdpd: a new client (%s): this session ends", g_nextPeer);
			break;
		}
		us = kapi_clock_us ();
		keepalive (us);
		cursor_poll (us);
		if (g_pipe && us - g_lastRx > SILENT_LIMIT)
		{
			if (!fill_in ()) break;			// (what came during a long send counts)
			us = kapi_clock_us ();
			if (us - g_lastRx > SILENT_LIMIT)
			{
				rdlog ("rdpd: the client was silent %u s: the session ends", (us - g_lastRx) / 1000000u);
				break;
			}
		}
		stats_tick (0);
		kapi_msleep (5);
	}
	stats_tick (1);
	release_all ();					// (no button, key or modifier left held)
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
		int n = uk_win_list (L, MAXWIN);
		printf ("kapi v%u, screen %d x %d, %d windows listed:\n", kapi_abi_version (), g_W, g_H, n);
		for (int i = 0; i < n; i++)
			printf ("  %08X pid %u  %d,%d %dx%d  frame %dx%d  flags %X alpha %d gen %u state %u  %s\n", L[i].id, L[i].pid,
				L[i].x, L[i].y, L[i].w, L[i].h, L[i].ow, L[i].oh, L[i].flags, L[i].alpha, L[i].gen, L[i].state, L[i].title);
		if (n > 0)
		{
			unsigned px[16];
			int r = uk_win_read (L[n - 1].id, 0, 0, 0, 4, 4, px, 4);
			printf ("read of the top one: %d, first pixel %06X\n", r, r == 0 ? px[0] : 0);
		}
		return 0;
	}
	g_packCap = (g_W > 2560 ? g_W : 2560) * TILE * 4;	// (a strip of the widest screen: kapi_screen_set)
	g_pack = (unsigned char *) malloc ((size_t) g_packCap);
	g_lz = (unsigned char *) malloc ((size_t) g_packCap + g_packCap / 255 + 64);
	if (!g_pack || !g_lz) { kapi_stdout_write ("rdpd: out of memory\n", 20); return 1; }

	char ip[32];
	while (!kapi_net_status (ip, sizeof ip)) kapi_msleep (1000);
	int lsock = kapi_tcp_listen (port);
	if (lsock < 0) { kapi_stdout_write ("rdpd: cannot listen\n", 20); return 1; }
	g_lsock = lsock;
	int threaded = kapi_thread_create (acceptor, 0, 0, "accept") >= 0;	// (kernel v67)
	for (;;)
	{
		char peer[32];
		if (threaded)
		{
			while ((g_sock = __atomic_load_n (&g_next, __ATOMIC_ACQUIRE)) < 0) kapi_msleep (20);
			memcpy (peer, g_nextPeer, sizeof peer);
			__atomic_store_n (&g_next, -1, __ATOMIC_RELEASE);
		}
		else
		{
			g_sock = kapi_tcp_accept (lsock, peer, sizeof peer);
			if (g_sock < 0) { kapi_msleep (500); continue; }
		}
		rdlog ("rdpd: client %s", peer);
		session ();
		rdlog ("rdpd: session end");
		kapi_tcp_close (g_sock);
	}
}
