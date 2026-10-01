//
// rdpdhost.c -- rdpd (user/bin/rdpd.c) on the PC, for testing its protocol without a Pi: a
// stand-in kernel table mapped at KAPI_TABLE_VA with real TCP sockets (tcp_listen / accept /
// send / recv) and a few fake windows, then rdpd's own main. See build_host.sh, ../run_rdpd_pipeline_test.sh.
//   RDPD_SRC   the rdpd source to build (default user/bin/rdpd.c; an older one to test
//              compatibility)
//   RDPD_ANIM=1  window 2 redrawn every 40 ms (a demo running), else everything static
// The injected input is printed on stderr ("host: ptr x y b w", "host: key ...").
//
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "kapi.h"

static unsigned long long t0;
static unsigned long long now_us (void)
{
	struct timespec ts; clock_gettime (CLOCK_MONOTONIC, &ts);
	return (unsigned long long) ts.tv_sec * 1000000ull + (unsigned long long) ts.tv_nsec / 1000ull;
}
static unsigned h_ticks (void) { return (unsigned) ((now_us () - t0) / 10000ull); }	// 100 Hz
static void h_msleep (unsigned ms) { usleep (ms * 1000u); }
static int h_write (int fd, const void *b, unsigned n) { (void) fd; fprintf (stderr, "kmsg: %.*s\n", (int) n, (const char *) b); return (int) n; }
static int h_stdout (const void *b, unsigned n) { return (int) fwrite (b, 1, n, stdout); }
static char g_args[64];
static int h_args (char *b, unsigned n) { snprintf (b, n, "%s", g_args); return (int) strlen (b); }
static void h_screen (int *w, int *h) { *w = 1024; *h = 768; }
static int h_net (char *ip, unsigned cap) { snprintf (ip, cap, "127.0.0.1"); return 1; }

// ---- sockets ----
static int h_listen (unsigned port)
{
	int s = socket (AF_INET, SOCK_STREAM, 0), one = 1;
	setsockopt (s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	struct sockaddr_in a; memset (&a, 0, sizeof a);
	a.sin_family = AF_INET; a.sin_port = htons ((unsigned short) port); a.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	if (bind (s, (struct sockaddr *) &a, sizeof a) < 0 || listen (s, 4) < 0) { perror ("host: listen"); return -6; }
	return s;
}
static int h_accept (int l, char *ip, unsigned cap)
{
	struct sockaddr_in a; socklen_t al = sizeof a;
	int s = accept (l, (struct sockaddr *) &a, &al);
	if (s < 0) return -1;
	int one = 1; setsockopt (s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
	fcntl (s, F_SETFL, fcntl (s, F_GETFL) | O_NONBLOCK);
	snprintf (ip, cap, "%s", inet_ntoa (a.sin_addr));
	return s;
}
static int h_send (int s, const void *b, unsigned n)		// blocking, as the kernel's
{
	unsigned off = 0;
	while (off < n)
	{
		ssize_t k = send (s, (const char *) b + off, n - off, MSG_NOSIGNAL);
		if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { usleep (1000); continue; }
		if (k <= 0) return off ? (int) off : -1;
		off += (unsigned) k;
	}
	return (int) off;
}
static int h_recv (int s, void *b, unsigned n)			// non-blocking: >0, 0 nothing, <0 closed
{
	ssize_t k = recv (s, b, n, 0);
	if (k > 0) return (int) k;
	if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
	return -1;
}
static void h_close (int s) { close (s); }

// ---- the windows: 1 a static framed one, 2 a framed one animated with RDPD_ANIM ----
static int g_anim;
static unsigned anim_gen (void) { return g_anim ? (unsigned) ((now_us () - t0) / 40000ull) : 1; }
static int h_win_list (struct kapi_win_info *o, int max)
{
	int n = 0;
	for (int i = 1; i <= 2 && n < max; i++)
	{
		struct kapi_win_info *w = &o[n++];
		memset (w, 0, sizeof *w);
		w->id = (unsigned) i; w->pid = 10u + (unsigned) i;
		w->x = 100 * i; w->y = 80 * i; w->w = 300; w->h = 200; w->alpha = 255;
		w->gen = i == 2 ? anim_gen () : 1; w->state = i == 2 ? KAPI_WIN_KEYS : 0;
		snprintf (w->title, sizeof w->title, "Window %d", i);
		w->ow = 304; w->oh = 230; w->il = 2; w->it = 28; w->chromeGen = 1;
	}
	return n;
}
static int h_win_read (unsigned id, int part, int x, int y, int w, int h, unsigned *dst, int stride)
{
	if (id < 1 || id > 2) return -1;
	unsigned g = id == 2 ? anim_gen () : 1;
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
		{
			int X = x + i, Y = y + j;
			unsigned c = part ? 0x303050u + (unsigned) part : (unsigned) (X * 3 + Y * 5) * 0x010203u;
			if (part == 0 && id == 2 && X / 16 == (int) (g % 18) && Y < 64) c = 0xFF2020u;	// (a moving bar)
			dst[(size_t) j * stride + i] = c & 0xFFFFFFu;
		}
	return 0;
}
static int h_win_raise (unsigned id) { fprintf (stderr, "host: raise %u\n", id); return 0; }
static int h_win_close (unsigned id) { fprintf (stderr, "host: close %u\n", id); return 0; }
static void h_ptr (int x, int y, unsigned b, int wh) { fprintf (stderr, "host: ptr %d %d %u %d\n", x, y, b, wh); }
static void h_key (const char *k) { fprintf (stderr, "host: key \"%s\"\n", k); }
static void h_held (int k, int d) { fprintf (stderr, "host: held %d %d\n", k, d); }
static void h_mods (unsigned m) { fprintf (stderr, "host: mods %u\n", m); }

// kapi_thread_create on a POSIX thread (rdpd's accepting thread)
#include <pthread.h>
struct h_thr { int (*fn) (void *); void *arg; };
static void *h_thr_run (void *p) { struct h_thr t = *(struct h_thr *) p; free (p); t.fn (t.arg); return 0; }
static int h_thread (int (*fn) (void *), void *arg, unsigned st, const char *name)
{
	(void) st; (void) name;
	struct h_thr *t = malloc (sizeof *t); pthread_t id;
	if (!t) return -1;
	t->fn = fn; t->arg = arg;
	if (pthread_create (&id, 0, h_thr_run, t) != 0) { free (t); return -1; }
	pthread_detach (id);
	return 2;
}

static void unimplemented (void) { fprintf (stderr, "host: an unimplemented kapi call\n"); abort (); }

#define main rdpd_main
#include RDPD_SRC
#undef main

int main (int argc, char **argv)
{
	t0 = now_us () - 1000000ull;
	g_anim = getenv ("RDPD_ANIM") && atoi (getenv ("RDPD_ANIM"));
	snprintf (g_args, sizeof g_args, "%s", argc > 1 ? argv[1] : "3390");
	void *p = mmap ((void *) KAPI_TABLE_VA, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (p == MAP_FAILED) { perror ("mmap"); return 1; }
	void **slots = (void **) p;
	for (size_t i = 0; i < sizeof (struct TKApiTable) / sizeof (void *); i++) slots[i] = (void *) unimplemented;
	struct TKApiTable *T = (struct TKApiTable *) p;
	T->version = KAPI_ABI_VERSION;
	T->get_ticks = h_ticks; T->msleep = h_msleep; T->write = h_write; T->stdout_write = h_stdout;
	T->get_args = h_args; T->screen_size = h_screen; T->net_status = h_net;
	T->tcp_listen = h_listen; T->tcp_accept = h_accept; T->tcp_send = h_send; T->tcp_recv = h_recv; T->tcp_close = h_close;
	T->win_list = h_win_list; T->win_read = h_win_read; T->win_raise = h_win_raise; T->win_close = h_win_close;
	T->thread_create = h_thread;
	T->inject_pointer = h_ptr; T->inject_key = h_key; T->inject_key_held = h_held; T->inject_modifiers = h_mods;
	setvbuf (stderr, 0, _IOLBF, 0);
	return rdpd_main ();
}
