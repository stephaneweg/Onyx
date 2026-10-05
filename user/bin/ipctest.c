//
// ipctest -- IPC between processes, kapi v76 (WP-IPC: docs/POSIX-PLAN.md §14, docs/02 §8 "v76: IPC"),
// at the kapi level: local sockets (sock_pair STREAM / SEQPACKET / DGRAM, the sock_* calls, poll,
// sock_sendmsg / sock_recvmsg), handles carried between processes, shared memory objects (shm_create,
// shm_ctl, shm_map, shm_open), spawn_ex2 / get_handles. Built on libonyxposix (printf, pthreads) so
// it also runs on the PC bench (PROG=user/bin/ipctest.c sh tools/tests/posixsim/run.sh). Every check
// prints a PASS, FAIL or SKIP line; the last line is "ipctest: PASS" or "ipctest: FAIL (n)", and the
// exit status the number of failures.
//
//   ipctest           every check (a few seconds; children of its own: "ipctest child <what>")
//   ipctest net       ALSO an IP socket passed to a child (the network up)
//
// The checks: socketpair STREAM (merging, EAGAIN, POLLOUT, SO_RCVBUF), SEQPACKET and DGRAM (the
// boundaries, MSG_TRUNC, a zero-length packet, EMSGSIZE), the end (0 after the queue, EPIPE,
// POLLHUP), a blocked recv woken by another thread, poll's wake; a child spawned with a socket end at
// fd 5 (spawn_ex2) echoing; a file (its offset shared), a pipe end, a shm object, a local socket and
// (net) an IP socket sent to the child and used there; shared memory the child wrote, read after it
// exited; a futex across processes on a shm word; a 1 MB packet and 8 MB through a stream; 253
// handles in one message; MSG_CTRUNC; seals; a child touching beyond its object (killed); the free
// memory back after 64 MB of shared pages and their processes are gone.
//
// ---------------------------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// ---------------------------------------------------------------------------------------------
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include "appkit/appkit.h"

#define PAGE		65536ULL
#define RW		(KAPI_PROT_READ | KAPI_PROT_WRITE)
#define CHILD_FD	5		// where the child finds its socket end

static int s_nFail, s_nPass, s_nSkip;
static const char *s_pSelf = "SD:/bin/ipctest";

static void check (const char *pWhat, int bOK)
{
	printf ("%s  %s\n", bOK ? "PASS" : "FAIL", pWhat);
	if (bOK) s_nPass++; else s_nFail++;
}

static void skip (const char *pWhat, const char *pWhy)
{
	printf ("SKIP  %s (%s)\n", pWhat, pWhy);
	s_nSkip++;
}

static unsigned long long now_ms (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned long long) t.tv_sec * 1000 + (unsigned long long) t.tv_nsec / 1000000;
}

// ---- helpers -------------------------------------------------------------------------------------

static long long xsend (int s, const void *p, unsigned long long n, const struct kapi_handle_xfer *x, unsigned nx)
{
	struct kapi_iovec v = { (unsigned long long) (unsigned long) p, n };
	struct kapi_msghdr m;
	memset (&m, 0, sizeof m);
	m.iov = &v;
	m.iovcnt = 1;
	m.handles = (struct kapi_handle_xfer *) x;
	m.nhandles = nx;
	return kapi_sock_sendmsg (s, &m, 0);
}

static long long xrecv (int s, void *p, unsigned long long n, struct kapi_handle_xfer *x, unsigned *pnx, unsigned *pfl)
{
	struct kapi_iovec v = { (unsigned long long) (unsigned long) p, n };
	struct kapi_msghdr m;
	memset (&m, 0, sizeof m);
	m.iov = &v;
	m.iovcnt = 1;
	m.handles = x;
	m.nhandles = pnx ? *pnx : 0;
	long long r = kapi_sock_recvmsg (s, &m, 0);
	if (pnx) *pnx = m.nhandles;
	if (pfl) *pfl = m.flags;
	return r;
}

static struct kapi_handle_xfer X (long long h, int kind, unsigned tag, int fd)
{
	struct kapi_handle_xfer x;
	memset (&x, 0, sizeof x);
	x.h = h; x.kind = kind; x.tag = tag; x.fd = fd;
	return x;
}

static short poll1 (int s, short ev, int ms)
{
	struct kapi_pollfd p;
	memset (&p, 0, sizeof p);
	p.kind = KAPI_PK_SOCKET;
	p.h = s;
	p.events = ev;
	return kapi_poll (&p, 1, ms) > 0 ? p.revents : 0;
}

// Spawn "ipctest child <what>" with handles -> the process handle (0: failed).
static void *spawn_child (const char *pWhat, const struct kapi_handle_xfer *x, unsigned n)
{
	char argv[160];
	unsigned o = 0;
	const char *a[3] = { s_pSelf, "child", pWhat };
	for (int i = 0; i < 3; i++)
	{
		size_t l = strlen (a[i]) + 1;
		memcpy (argv + o, a[i], l);
		o += (unsigned) l;
	}
	argv[o] = 0;
	struct kapi_spawn_attr A;
	memset (&A, 0, sizeof A);
	A.path = s_pSelf;
	A.argv = argv;
	long long h = kapi_spawn_ex2 (&A, x, n);
	return h > 0 ? (void *) (unsigned long) h : 0;
}

// Spawn with a SEQPACKET pair: the child's end at CHILD_FD -> our end (*ph: the process).
static int spawn_with_socket (const char *pWhat, int nType, void **ph)
{
	int sv[2];
	*ph = 0;
	if (kapi_sock_pair (nType, 0, sv) != 0) return -1;
	struct kapi_handle_xfer x = X (sv[1], KAPI_HK_LSOCK, 0, CHILD_FD);
	*ph = spawn_child (pWhat, &x, 1);
	kapi_sock_close (sv[1]);			// (the child holds its own now)
	if (*ph == 0) { kapi_sock_close (sv[0]); return -1; }
	kapi_sock_setopt (sv[0], KAPI_SO_RCVTIMEO_MS, 20000);	// (a child gone wrong: no hang)
	return sv[0];
}

static int wait_child (void *h, int *pReason)
{
	struct kapi_proc_status S;
	memset (&S, 0, sizeof S);
	if (h == 0 || kapi_proc_wait (h, 0, &S) != 1) return -1000;
	if (pReason) *pReason = S.reason;
	return S.code;
}

static unsigned long free_kb (void)
{
	unsigned long t = 0, f = 0, a = 0;
	unsigned p = 0;
	kapi_meminfo (&t, &f, &a, &p);
	return f;
}

// ---- the children ---------------------------------------------------------------------------------

// The child's socket end (at CHILD_FD, from get_handles).
static int child_socket (void)
{
	struct kapi_handle_xfer x[8];
	int n = kapi_get_handles (x, 8);
	for (int i = 0; i < n && i < 8; i++)
		if (x[i].fd == CHILD_FD && x[i].kind == KAPI_HK_LSOCK) return (int) x[i].h;
	return -1;
}

static int child_main (const char *pWhat)
{
	int s = child_socket ();
	if (s < 0) return 90;
	char b[256];
	if (strcmp (pWhat, "echo") == 0)		// every packet back, until the end
	{
		long long n;
		while ((n = kapi_sock_recv (s, b, sizeof b, 0, 0)) > 0)
			if (kapi_sock_send (s, b, (unsigned long long) n, 0, 0) != n) return 91;
		return n == 0 ? 0 : 92;
	}
	if (strcmp (pWhat, "use") == 0)			// use what the parent sends, report
	{
		for (;;)
		{
			static struct kapi_handle_xfer x[256];
			unsigned nx = 256, fl = 0;
			long long n = xrecv (s, b, sizeof b - 1, x, &nx, &fl);
			if (n <= 0) return n == 0 ? 0 : 93;
			b[n] = 0;
			char r[64] = "bad";
			if (strcmp (b, "file") == 0 && nx == 1 && x[0].kind == KAPI_HK_OFILE)
			{
				char t[8] = "";
				long long k = kapi_file_read (x[0].h, t, 5, -1);	// (at the shared offset)
				snprintf (r, sizeof r, "%lld:%.5s", k, t);
				kapi_file_close (x[0].h);
			}
			else if (strcmp (b, "pipe") == 0 && nx == 1 && x[0].kind == KAPI_HK_STREAM)
			{
				int k = kapi_stream_write ((void *) (unsigned long) x[0].h, "pipe!", 5);
				snprintf (r, sizeof r, "%d", k);
				kapi_stream_close ((void *) (unsigned long) x[0].h);
			}
			else if (strcmp (b, "shm") == 0 && nx == 1 && x[0].kind == KAPI_HK_SHM)
			{
				long long size = kapi_shm_ctl (x[0].h, KAPI_SHM_GET_SIZE, 0);
				long long a = kapi_shm_map (x[0].h, 0, (unsigned long long) size, RW, 0, 0);
				int ok = a > 0;
				unsigned char *p = (unsigned char *) (unsigned long) a;
				for (long long i = 0; ok && i < size; i += 4093) ok = p[i] == (unsigned char) (i * 7);
				if (a > 0) { memset (p + size / 2, 0xC5, 1000); kapi_vm_unmap ((unsigned long long) a, (unsigned long long) size); }
				kapi_handle_close (x[0].h);
				snprintf (r, sizeof r, "%s", ok ? "shm-ok" : "shm-bad");
			}
			else if (strcmp (b, "lsock") == 0 && nx == 1 && x[0].kind == KAPI_HK_LSOCK)
			{
				long long k = kapi_sock_send ((int) x[0].h, "via-lsock", 9, 0, 0);
				snprintf (r, sizeof r, "%lld", k);
				kapi_sock_close ((int) x[0].h);
			}
			else if (strcmp (b, "ip") == 0 && nx == 1 && x[0].kind == KAPI_HK_SOCKET)
			{
				struct kapi_sockaddr a;
				int k = kapi_sock_name ((int) x[0].h, 0, &a);
				snprintf (r, sizeof r, "%d:%u", k, (unsigned) a.port);
				kapi_sock_close ((int) x[0].h);
			}
			else if (strcmp (b, "many") == 0)
			{
				int ok = 0;
				for (unsigned i = 0; i < nx; i++) ok += x[i].kind == KAPI_HK_SHM && x[i].tag == i;
				snprintf (r, sizeof r, "%u:%d", nx, ok);
				for (unsigned i = 0; i < nx; i++) kapi_handle_close (x[i].h);
			}
			else if (strncmp (b, "many", 4) == 0)
				snprintf (r, sizeof r, "short %u", nx);
			kapi_sock_send (s, r, strlen (r), 0, 0);
		}
	}
	if (strcmp (pWhat, "writer") == 0)		// a shm object made, written, sent; then exit
	{
		long long h = kapi_shm_create (3 * PAGE + 123, 0);
		long long a = kapi_shm_map (h, 0, 4 * PAGE, RW, 0, 0);
		if (h <= 0 || a <= 0) return 94;
		unsigned char *p = (unsigned char *) (unsigned long) a;
		for (unsigned i = 0; i < 3 * PAGE + 123; i++) p[i] = (unsigned char) (i ^ 0x5A);
		struct kapi_handle_xfer x = X (h, KAPI_HK_SHM, 77, 0);
		return xsend (s, "w", 1, &x, 1) == 1 ? 0 : 95;	// (exit: its mapping and handle gone)
	}
	if (strcmp (pWhat, "futex") == 0)		// sleep on a shm word until the parent wakes it
	{
		struct kapi_handle_xfer x[1];
		unsigned nx = 1;
		if (xrecv (s, b, sizeof b, x, &nx, 0) <= 0 || nx != 1) return 96;
		long long a = kapi_shm_map (x[0].h, 0, PAGE, RW, 0, 0);
		if (a <= 0) return 97;
		volatile unsigned *w = (volatile unsigned *) (unsigned long) a;
		kapi_sock_send (s, "ready", 5, 0, 0);
		unsigned long long t0 = now_ms ();
		while (*w == 0) kapi_wait_word (w, 0, 5000);
		char r[32];
		snprintf (r, sizeof r, "%u:%llu", *w, now_ms () - t0);
		kapi_sock_send (s, r, strlen (r), 0, 0);
		return 0;
	}
	if (strcmp (pWhat, "beyond") == 0)		// touch past the object's end: killed
	{
		struct kapi_handle_xfer x[1];
		unsigned nx = 1;
		if (xrecv (s, b, sizeof b, x, &nx, 0) <= 0 || nx != 1) return 96;
		long long a = kapi_shm_map (x[0].h, 0, 2 * PAGE, RW, 0, 0);
		if (a <= 0) return 97;
		volatile unsigned char *p = (volatile unsigned char *) (unsigned long) a;
		p[0] = 1;
		p[PAGE + 8] = 2;				// (not reached)
		return 98;
	}
	if (strcmp (pWhat, "toucher") == 0)		// map and touch a whole object, then exit
	{
		struct kapi_handle_xfer x[1];
		unsigned nx = 1;
		if (xrecv (s, b, sizeof b, x, &nx, 0) <= 0 || nx != 1) return 96;
		long long size = kapi_shm_ctl (x[0].h, KAPI_SHM_GET_SIZE, 0);
		long long a = kapi_shm_map (x[0].h, 0, (unsigned long long) size, RW, 0, 0);
		if (a <= 0) return 97;
		for (long long i = 0; i < size; i += (long long) PAGE) ((volatile char *) (unsigned long) a)[i] = 1;
		kapi_sock_send (s, "t", 1, 0, 0);
		return 0;
	}
	if (strcmp (pWhat, "sink") == 0)		// a stream: count and sum until the end
	{
		static unsigned char big[65536];
		unsigned long long total = 0, sum = 0;
		long long n;
		while ((n = kapi_sock_recv (s, big, sizeof big, 0, 0)) > 0)
		{
			for (long long i = 0; i < n; i++) sum += big[i];
			total += (unsigned long long) n;
		}
		char r[64];
		snprintf (r, sizeof r, "%llu:%llu", total, sum);
		kapi_sock_send (s, r, strlen (r), 0, 0);
		return 0;
	}
	return 99;
}

// ---- the tests --------------------------------------------------------------------------------------

static void test_stream (void)
{
	int sv[2];
	check ("sock_pair STREAM", kapi_sock_pair (KAPI_SOCK_STREAM, KAPI_SOCKF_NONBLOCK, sv) == 0
	       && sv[0] >= KAPI_SOCK_LOCAL_BASE && sv[1] >= KAPI_SOCK_LOCAL_BASE);
	char e4[4];
	check ("stream: empty -> EAGAIN", kapi_sock_recv (sv[1], e4, 4, 0, 0) == -KAPI_EAGAIN);
	check ("stream: POLLOUT, no POLLIN", poll1 (sv[0], KAPI_POLLIN | KAPI_POLLOUT, 0) == KAPI_POLLOUT);
	kapi_sock_send (sv[0], "abc", 3, 0, 0);
	kapi_sock_send (sv[0], "defg", 4, 0, 0);
	char b[64];
	check ("stream: POLLIN", (poll1 (sv[1], KAPI_POLLIN, 0) & KAPI_POLLIN) != 0);
	long long n = kapi_sock_recv (sv[1], b, sizeof b, 0, 0);
	check ("stream: two sends read as one (merged)", n == 7 && memcmp (b, "abcdefg", 7) == 0);
	int v = 0;
	check ("SO_DOMAIN / SO_TYPE", kapi_sock_getopt (sv[0], KAPI_SO_DOMAIN, &v) == 0 && v == KAPI_AF_UNIX
	       && kapi_sock_getopt (sv[0], KAPI_SO_TYPE, &v) == 0 && v == KAPI_SOCK_STREAM);
	check ("SO_PEERPID", kapi_sock_getopt (sv[0], KAPI_SO_PEERPID, &v) == 0 && v == kapi_getpid (0));
	// full: what fits, then EAGAIN, no POLLOUT
	kapi_sock_setopt (sv[1], KAPI_SO_RCVBUF, 8192);
	kapi_sock_setopt (sv[0], KAPI_SO_SNDBUF, 8192);
	static char big[100000];
	long long w = kapi_sock_send (sv[0], big, sizeof big, 0, 0);
	check ("stream: a send takes what fits (SO_RCVBUF)", w > 0 && w < (long long) sizeof big);
	check ("stream: then EAGAIN and no POLLOUT", kapi_sock_send (sv[0], big, 10, 0, 0) == -KAPI_EAGAIN
	       && (poll1 (sv[0], KAPI_POLLOUT, 0) & KAPI_POLLOUT) == 0);
	while (kapi_sock_recv (sv[1], big, sizeof big, 0, 0) > 0) { }
	check ("stream: drained -> POLLOUT", (poll1 (sv[0], KAPI_POLLOUT, 0) & KAPI_POLLOUT) != 0);
	// the end
	kapi_sock_send (sv[1], "last", 4, 0, 0);
	kapi_sock_close (sv[1]);
	short ev = poll1 (sv[0], KAPI_POLLIN, 0);
	check ("peer closed: POLLIN | POLLHUP", (ev & (KAPI_POLLIN | KAPI_POLLHUP)) == (KAPI_POLLIN | KAPI_POLLHUP));
	check ("peer closed: the data, then 0", kapi_sock_recv (sv[0], b, sizeof b, 0, 0) == 4
	       && kapi_sock_recv (sv[0], b, sizeof b, 0, 0) == 0);
	check ("peer closed: send -> EPIPE", kapi_sock_send (sv[0], "x", 1, 0, 0) == -KAPI_EPIPE);
	check ("connect / bind on a local socket -> EOPNOTSUPP", kapi_sock_listen (sv[0], 1) == -KAPI_EOPNOTSUPP);
	kapi_sock_close (sv[0]);
	check ("closed: EBADF", kapi_sock_send (sv[0], "x", 1, 0, 0) == -KAPI_EBADF);
}

static void test_packets (int nType, const char *pName)
{
	char what[96];
	int sv[2];
	snprintf (what, sizeof what, "sock_pair %s", pName);
	check (what, kapi_sock_pair (nType, KAPI_SOCKF_NONBLOCK, sv) == 0);
	xsend (sv[0], "one", 3, 0, 0);
	xsend (sv[0], "", 0, 0, 0);
	xsend (sv[0], "three!", 6, 0, 0);
	char b[64];
	unsigned fl = 0;
	long long a = xrecv (sv[1], b, sizeof b, 0, 0, &fl);
	long long z = xrecv (sv[1], b + 10, sizeof b - 10, 0, 0, 0);
	long long c = xrecv (sv[1], b + 20, 4, 0, 0, &fl);
	snprintf (what, sizeof what, "%s: boundaries kept, a zero-length packet, MSG_TRUNC", pName);
	check (what, a == 3 && memcmp (b, "one", 3) == 0 && z == 0 && c == 4 && memcmp (b + 20, "thre", 4) == 0
	       && fl == KAPI_MSG_TRUNC && kapi_sock_recv (sv[1], b, sizeof b, 0, 0) == -KAPI_EAGAIN);
	static char big[1 << 20];
	snprintf (what, sizeof what, "%s: over SO_SNDBUF -> EMSGSIZE; 1 MB with SO_SNDBUF 4 MB", pName);
	for (unsigned i = 0; i < sizeof big; i++) big[i] = (char) (i * 13);
	long long e = kapi_sock_send (sv[0], big, sizeof big, 0, 0);
	kapi_sock_setopt (sv[0], KAPI_SO_SNDBUF, 4 << 20);
	kapi_sock_setopt (sv[1], KAPI_SO_RCVBUF, 4 << 20);
	long long s1 = kapi_sock_send (sv[0], big, sizeof big, 0, 0);
	static char got[1 << 20];
	long long r1 = kapi_sock_recv (sv[1], got, sizeof got, 0, 0);
	check (what, e == -KAPI_EMSGSIZE && s1 == (long long) sizeof big && r1 == s1 && memcmp (big, got, sizeof big) == 0);
	kapi_sock_shutdown (sv[0], KAPI_SHUT_WR);
	snprintf (what, sizeof what, "%s: SHUT_WR -> the peer reads 0, our send EPIPE", pName);
	if (nType == KAPI_SOCK_SEQPACKET)		/* (a DGRAM pair: Linux keeps the peer open) */
		check (what, kapi_sock_recv (sv[1], b, sizeof b, 0, 0) == 0 && kapi_sock_send (sv[0], "x", 1, 0, 0) == -KAPI_EPIPE);
	kapi_sock_close (sv[0]);
	kapi_sock_close (sv[1]);
}

struct waiter { int s; long long n; unsigned long long ms; };
static void *recv_thread (void *p)
{
	struct waiter *w = (struct waiter *) p;
	char b[16];
	unsigned long long t0 = now_ms ();
	w->n = kapi_sock_recv (w->s, b, sizeof b, 0, 0);	// (blocks)
	w->ms = now_ms () - t0;
	return 0;
}

static void test_blocking (void)
{
	int sv[2];
	kapi_sock_pair (KAPI_SOCK_SEQPACKET, 0, sv);		// (blocking)
	struct waiter W = { sv[1], -1, 0 };
	pthread_t t;
	pthread_create (&t, 0, recv_thread, &W);
	usleep (100000);
	kapi_sock_send (sv[0], "wake", 4, 0, 0);
	pthread_join (t, 0);
	char what[96];
	snprintf (what, sizeof what, "a blocked recv woken by a send from another thread (%llu ms)", W.ms);
	check (what, W.n == 4 && W.ms >= 80 && W.ms < 1000);
	// poll's wake
	struct kapi_pollfd p;
	memset (&p, 0, sizeof p);
	p.kind = KAPI_PK_SOCKET; p.h = sv[1]; p.events = KAPI_POLLIN;
	unsigned long long t0 = now_ms ();
	int r = kapi_poll (&p, 1, 150);
	unsigned long long dt = now_ms () - t0;
	snprintf (what, sizeof what, "poll timeout on a local socket (%llu ms for 150)", dt);
	check (what, r == 0 && dt >= 140 && dt < 400);
	// SO_RCVTIMEO
	kapi_sock_setopt (sv[1], KAPI_SO_RCVTIMEO_MS, 100);
	t0 = now_ms ();
	char b[8];
	long long n = kapi_sock_recv (sv[1], b, sizeof b, 0, 0);
	dt = now_ms () - t0;
	snprintf (what, sizeof what, "SO_RCVTIMEO 100 ms -> EAGAIN (%llu ms)", dt);
	check (what, n == -KAPI_EAGAIN && dt >= 90 && dt < 400);
	kapi_sock_close (sv[0]);
	kapi_sock_close (sv[1]);
}

static int expect_reply (int s, const char *pWant, char *pGot, unsigned nCap)
{
	long long n = kapi_sock_recv (s, pGot, nCap - 1, 0, 0);
	if (n < 0) n = 0;
	pGot[n] = 0;
	return pWant == 0 || strcmp (pGot, pWant) == 0;
}

static void test_child_use (int bNet)
{
	void *hp;
	int s = spawn_with_socket ("echo", KAPI_SOCK_SEQPACKET, &hp);
	check ("spawn_ex2: a child with a socket end at fd 5", s >= 0);
	if (s < 0) return;
	char b[64];
	kapi_sock_send (s, "ping", 4, 0, 0);
	check ("the child echoes through it", expect_reply (s, "ping", b, sizeof b));
	kapi_sock_close (s);
	int reason = -1;
	check ("the child sees the end and exits 0", wait_child (hp, &reason) == 0 && reason == KAPI_PROC_EXITED);

	s = spawn_with_socket ("use", KAPI_SOCK_SEQPACKET, &hp);
	if (s < 0) { check ("spawn the user child", 0); return; }
	// a file: the description (and its offset) shared
	long long hf = kapi_file_open ("RAM:/ipctest.txt", KAPI_O_RDWR | KAPI_O_CREAT | KAPI_O_TRUNC, 0644);
	kapi_file_write (hf, "hello world", 11, -1);
	kapi_file_seek (hf, 0, KAPI_SEEK_SET);
	struct kapi_handle_xfer x = X (hf, KAPI_HK_OFILE, 1, 0);
	xsend (s, "file", 4, &x, 1);
	check ("a file sent: the child reads it", expect_reply (s, "5:hello", b, sizeof b));
	check ("... at the shared offset (ours moved to 5)", kapi_file_seek (hf, 0, KAPI_SEEK_CUR) == 5);
	kapi_file_close (hf);
	kapi_path_unlink ("RAM:/ipctest.txt", 0);
	// a pipe end: the child writes, we read
	void *hpipe = kapi_pipe ();
	x = X ((long long) (unsigned long) hpipe, KAPI_HK_STREAM, 2, 0);
	x.flags = KAPI_HXF_WRITER;			// (the child writes: a write end)
	xsend (s, "pipe", 4, &x, 1);
	int ok = expect_reply (s, "5", b, sizeof b);
	char pb[8] = "";
	int k = kapi_stream_read_nb (hpipe, pb, 5);
	check ("a pipe sent: the child writes into it, we read it", ok && k == 5 && memcmp (pb, "pipe!", 5) == 0);
	kapi_stream_close (hpipe);
	// a shm object: both see the same pages
	long long hs = kapi_shm_create (PAGE * 16 + 1000, 0);
	long long a = kapi_shm_map (hs, 0, PAGE * 17, RW, 0, 0);
	unsigned char *p = (unsigned char *) (unsigned long) a;
	long long size = PAGE * 16 + 1000;
	for (long long i = 0; a > 0 && i < size; i += 4093) p[i] = (unsigned char) (i * 7);
	x = X (hs, KAPI_HK_SHM, 3, 0);
	xsend (s, "shm", 3, &x, 1);
	ok = expect_reply (s, "shm-ok", b, sizeof b);
	check ("a shm object sent: the child sees our pattern", ok);
	check ("... and we see what the child wrote", a > 0 && p[size / 2] == 0xC5 && p[size / 2 + 999] == 0xC5);
	struct kapi_vm_region R;
	check ("vm_query: a KAPI_VMK_SHM region", a > 0 && kapi_vm_query ((unsigned long long) a, &R) == 0 && R.kind == KAPI_VMK_SHM);
	kapi_vm_unmap ((unsigned long long) a, PAGE * 17);
	kapi_handle_close (hs);
	// a local socket: the child sends through it
	int sv[2];
	kapi_sock_pair (KAPI_SOCK_STREAM, 0, sv);
	x = X (sv[1], KAPI_HK_LSOCK, 4, 0);
	xsend (s, "lsock", 5, &x, 1);
	kapi_sock_close (sv[1]);
	ok = expect_reply (s, "9", b, sizeof b);
	char lb[16] = "";
	long long ln = kapi_sock_recv (sv[0], lb, sizeof lb, 0, 0);
	check ("a local socket sent: the child talks through it", ok && ln == 9 && memcmp (lb, "via-lsock", 9) == 0);
	check ("... it closed its end: we read the end", kapi_sock_recv (sv[0], lb, sizeof lb, 0, 0) == 0);
	kapi_sock_close (sv[0]);
	// an IP socket (adopted by the child)
	int ip = bNet ? kapi_sock_open (KAPI_SOCK_DGRAM, 0) : -1;
	if (ip >= 0)
	{
		struct kapi_sockaddr A;
		memset (&A, 0, sizeof A);
		A.family = KAPI_AF_INET;
		kapi_sock_bind (ip, &A);
		struct kapi_sockaddr Me;
		kapi_sock_name (ip, 0, &Me);
		x = X (ip, KAPI_HK_SOCKET, 5, 0);
		xsend (s, "ip", 2, &x, 1);
		char want[32];
		snprintf (want, sizeof want, "0:%u", (unsigned) Me.port);
		check ("an IP socket sent: the child uses it (adopted)", expect_reply (s, want, b, sizeof b));
	}
	else
		skip ("an IP socket sent to the child", bNet ? "no network" : "ipctest net");
	// 253 handles in one message (WebKit's most: attachmentMaxAmount - 1; Linux's SCM_MAX_FD)
	long long h1 = kapi_shm_create (4096, 0);
	static struct kapi_handle_xfer many[256];
	for (unsigned i = 0; i < 256; i++) many[i] = X (h1, KAPI_HK_SHM, i, 0);
	xsend (s, "many", 4, many, 253);
	check ("253 handles in one message", expect_reply (s, "253:253", b, sizeof b));
	kapi_handle_close (h1);
	kapi_sock_close (s);
	check ("the user child exits 0", wait_child (hp, 0) == 0);
}

static void test_shm_after_exit (void)
{
	void *hp;
	int s = spawn_with_socket ("writer", KAPI_SOCK_SEQPACKET, &hp);
	struct kapi_handle_xfer x[2];
	unsigned nx = 2;
	char b[8];
	long long n = s >= 0 ? xrecv (s, b, sizeof b, x, &nx, 0) : -1;
	int code = wait_child (hp, 0);
	check ("a child made, wrote and sent a shm object, then exited", n == 1 && nx == 1 && x[0].tag == 77 && code == 0);
	if (nx != 1) return;
	long long size = kapi_shm_ctl (x[0].h, KAPI_SHM_GET_SIZE, 0);
	long long a = kapi_shm_map (x[0].h, 0, (unsigned long long) size, KAPI_PROT_READ, 0, 0);
	int ok = a > 0 && size == (long long) (3 * PAGE + 123);
	const unsigned char *p = (const unsigned char *) (unsigned long) a;
	for (unsigned i = 0; ok && i < 3 * PAGE + 123; i++) ok = p[i] == (unsigned char) (i ^ 0x5A);
	check ("its data, read after its end", ok);
	check ("vm_protect of a shm region to RW (a read-write handle: allowed)",
	       a > 0 && kapi_vm_protect ((unsigned long long) a, PAGE, RW) == 0);
	kapi_vm_unmap ((unsigned long long) a, (unsigned long long) size);
	kapi_handle_close (x[0].h);
	kapi_sock_close (s);
}

static void test_futex (void)
{
	void *hp;
	int s = spawn_with_socket ("futex", KAPI_SOCK_SEQPACKET, &hp);
	long long h = kapi_shm_create (PAGE, 0);
	long long a = kapi_shm_map (h, 0, PAGE, RW, 0, 0);
	volatile unsigned *w = (volatile unsigned *) (unsigned long) a;
	struct kapi_handle_xfer x = X (h, KAPI_HK_SHM, 0, 0);
	xsend (s, "f", 1, &x, 1);
	char b[64];
	expect_reply (s, "ready", b, sizeof b);
	usleep (150000);				// (it sleeps on the word now)
	*w = 42;
	kapi_wake_word (w);
	expect_reply (s, 0, b, sizeof b);
	unsigned v = 0, ms = 0;
	sscanf (b, "%u:%u", &v, &ms);
	char what[96];
	snprintf (what, sizeof what, "a futex across processes on a shm word (woken after %u ms)", ms);
	check (what, v == 42 && ms >= 100 && ms < 2000);
	wait_child (hp, 0);
	kapi_sock_close (s);
	kapi_vm_unmap ((unsigned long long) a, PAGE);
	kapi_handle_close (h);
}

static void test_stream_big (void)
{
	void *hp;
	int s = spawn_with_socket ("sink", KAPI_SOCK_STREAM, &hp);
	static unsigned char chunk[100000];
	unsigned long long sum = 0, total = 0;
	for (unsigned i = 0; i < sizeof chunk; i++) chunk[i] = (unsigned char) (i * 31 + 7);
	unsigned long long t0 = now_ms ();
	while (total < (8ULL << 20))
	{
		unsigned long long n = sizeof chunk;
		if (total + n > (8ULL << 20)) n = (8ULL << 20) - total;
		if (kapi_sock_send (s, chunk, n, 0, 0) != (long long) n) break;
		for (unsigned long long i = 0; i < n; i++) sum += chunk[i];
		total += n;
	}
	kapi_sock_shutdown (s, KAPI_SHUT_WR);
	char b[64], want[64];
	expect_reply (s, 0, b, sizeof b);
	unsigned long long ms = now_ms () - t0;
	snprintf (want, sizeof want, "%llu:%llu", total, sum);
	char what[128];
	snprintf (what, sizeof what, "8 MB through a stream to a child, checksum (%llu ms, %llu KB/s)", ms,
		  ms ? (total / 1024) * 1000 / ms : 0);
	check (what, strcmp (b, want) == 0);
	wait_child (hp, 0);
	kapi_sock_close (s);
}

static void test_trunc_discard_seals (void)
{
	int sv[2];
	kapi_sock_pair (KAPI_SOCK_SEQPACKET, KAPI_SOCKF_NONBLOCK, sv);
	long long h = kapi_shm_create (PAGE, 0);
	struct kapi_handle_xfer x[5];
	for (int i = 0; i < 5; i++) x[i] = X (h, KAPI_HK_SHM, (unsigned) i, 0);
	xsend (sv[0], "c", 1, x, 5);
	struct kapi_handle_xfer in[2];
	unsigned nx = 2, fl = 0;
	char b[4];
	long long n = xrecv (sv[1], b, sizeof b, in, &nx, &fl);
	check ("MSG_CTRUNC: 5 handles, room for 2", n == 1 && nx == 2 && (fl & KAPI_MSG_CTRUNC));
	kapi_handle_close (in[0].h);
	kapi_handle_close (in[1].h);
	struct kapi_handle_xfer bad = X (12345, KAPI_HK_STREAM, 0, 0);
	check ("a bad handle: EBADF, nothing sent", xsend (sv[0], "x", 1, &bad, 1) == -KAPI_EBADF
	       && kapi_sock_recv (sv[1], b, sizeof b, 0, 0) == -KAPI_EAGAIN);
	struct kapi_handle_xfer self = X (sv[1], KAPI_HK_LSOCK, 0, 0);
	check ("the receiving end over its own connection: EINVAL", xsend (sv[0], "x", 1, &self, 1) == -KAPI_EINVAL);
	kapi_sock_close (sv[0]);
	kapi_sock_close (sv[1]);
	kapi_handle_close (h);
	// seals (memfd's)
	long long s = kapi_shm_create (0, KAPI_SHM_ALLOW_SEALING);
	int ok = s > 0 && kapi_shm_ctl (s, KAPI_SHM_SET_SIZE, 4096) == 0
		&& kapi_shm_ctl (s, KAPI_SHM_ADD_SEALS, KAPI_SEAL_SHRINK | KAPI_SEAL_GROW | KAPI_SEAL_SEAL) == 0
		&& kapi_shm_ctl (s, KAPI_SHM_SET_SIZE, 8192) == -KAPI_EPERM
		&& kapi_shm_ctl (s, KAPI_SHM_GET_SEALS, 0) == (KAPI_SEAL_SHRINK | KAPI_SEAL_GROW | KAPI_SEAL_SEAL);
	check ("seals: SHRINK | GROW | SEAL, then ftruncate EPERM", ok);
	kapi_handle_close (s);
	long long u = kapi_shm_create (10, 0);
	check ("no ALLOW_SEALING: SEAL_SEAL already", kapi_shm_ctl (u, KAPI_SHM_GET_SEALS, 0) == KAPI_SEAL_SEAL
	       && kapi_shm_ctl (u, KAPI_SHM_ADD_SEALS, KAPI_SEAL_GROW) == -KAPI_EPERM);
	kapi_handle_close (u);
	// named
	kapi_shm_unlink ("/ipctest");
	long long a = kapi_shm_open ("/ipctest", KAPI_O_RDWR | KAPI_O_CREAT | KAPI_O_EXCL, 0600);
	long long b2 = kapi_shm_open ("/ipctest", KAPI_O_RDONLY, 0);
	ok = a > 0 && b2 > 0 && kapi_shm_ctl (a, KAPI_SHM_SET_SIZE, 1000) == 0 && kapi_shm_ctl (b2, KAPI_SHM_GET_SIZE, 0) == 1000
	     && kapi_shm_open ("/ipctest", KAPI_O_RDWR | KAPI_O_CREAT | KAPI_O_EXCL, 0600) == -KAPI_EEXIST
	     && kapi_shm_map (b2, 0, PAGE, RW, 0, 0) == -KAPI_EACCES;
	check ("shm_open: create, open read-only (no PROT_WRITE), EEXIST", ok);
	check ("shm_unlink", kapi_shm_unlink ("/ipctest") == 0 && kapi_shm_open ("/ipctest", KAPI_O_RDWR, 0) == -KAPI_ENOENT);
	kapi_handle_close (a);
	kapi_handle_close (b2);
}

static void test_beyond (void)
{
	void *hp;
	int s = spawn_with_socket ("beyond", KAPI_SOCK_SEQPACKET, &hp);
	long long h = kapi_shm_create (100, 0);
	struct kapi_handle_xfer x = X (h, KAPI_HK_SHM, 0, 0);
	xsend (s, "b", 1, &x, 1);
	int reason = -1;
	int code = wait_child (hp, &reason);
	char what[96];
	snprintf (what, sizeof what, "a child touching beyond its object is killed (code %d, reason %d)", code, reason);
	check (what, code != 98 && code != 0 && reason == KAPI_PROC_FAULT);
	kapi_handle_close (h);
	kapi_sock_close (s);
}

static void test_memory_back (void)
{
	unsigned long f0 = free_kb ();
	void *hp;
	int s = spawn_with_socket ("toucher", KAPI_SOCK_SEQPACKET, &hp);
	long long h = kapi_shm_create (64ULL << 20, 0);
	struct kapi_handle_xfer x = X (h, KAPI_HK_SHM, 0, 0);
	xsend (s, "t", 1, &x, 1);
	char b[8];
	expect_reply (s, "t", b, sizeof b);
	unsigned long f1 = free_kb ();
	long long a = kapi_shm_map (h, 0, 64ULL << 20, KAPI_PROT_READ, 0, 0);
	int same = a > 0 && ((volatile char *) (unsigned long) a)[(32 << 20)] == 1;
	wait_child (hp, 0);
	kapi_sock_close (s);
	kapi_vm_unmap ((unsigned long long) a, 64ULL << 20);
	kapi_handle_close (h);
	unsigned long f2 = free_kb ();
	for (int i = 0; i < 40 && f2 + 1024 < f0; i++)	// (the child's teardown: the reaper, a moment later)
	{
		usleep (50000);
		f2 = free_kb ();
	}
	char what[160];
	snprintf (what, sizeof what, "64 MB of shared pages: used while mapped (%ld KB), back after (%ld KB from the start)",
		  (long) f0 - (long) f1, (long) f2 - (long) f0);
	check (what, same && (f1 + 60 * 1024 <= f0 || f0 == f1) && f2 + 1024 >= f0);
	// a message discarded with its handles: the object freed
	int sv[2];
	kapi_sock_pair (KAPI_SOCK_DGRAM, KAPI_SOCKF_NONBLOCK, sv);
	f0 = free_kb ();
	h = kapi_shm_create (16ULL << 20, 0);
	a = kapi_shm_map (h, 0, 16ULL << 20, RW, KAPI_MAP_POPULATE, 0);
	x = X (h, KAPI_HK_SHM, 0, 0);
	xsend (sv[0], "d", 1, &x, 1);
	kapi_vm_unmap ((unsigned long long) a, 16ULL << 20);
	kapi_handle_close (h);
	f1 = free_kb ();
	kapi_sock_close (sv[1]);				// (the message and its handle discarded)
	f2 = free_kb ();
	snprintf (what, sizeof what, "a queued message discarded frees the object it carried (%ld KB kept, %ld KB back)",
		  (long) f0 - (long) f1, (long) f2 - (long) f1);
	check (what, (f1 + 15 * 1024 <= f0 || f0 == f1) && f2 + 1024 >= f0);
	kapi_sock_close (sv[0]);
}

int main (int argc, char **argv)
{
	if (argc >= 1 && argv[0] != 0 && argv[0][0] != 0) s_pSelf = argv[0];
	if (argc >= 3 && strcmp (argv[1], "child") == 0) return child_main (argv[2]);
	int bNet = argc >= 2 && strcmp (argv[1], "net") == 0;
	int probe[2];
	if (kapi_sock_pair (KAPI_SOCK_STREAM, 0, probe) == -KAPI_ENOSYS)
	{
		printf ("ipctest: the kernel has no v76 IPC (kapi %u)\nipctest: FAIL (1)\n", kapi_abi_version ());
		return 1;
	}
	kapi_sock_close (probe[0]);
	kapi_sock_close (probe[1]);
	test_stream ();
	test_packets (KAPI_SOCK_SEQPACKET, "SEQPACKET");
	test_packets (KAPI_SOCK_DGRAM, "DGRAM");
	test_blocking ();
	test_child_use (bNet);
	test_shm_after_exit ();
	test_futex ();
	test_stream_big ();
	test_trunc_discard_seals ();
	test_beyond ();
	test_memory_back ();
	if (s_nFail == 0) printf ("ipctest: PASS (%d checks, %d skipped)\n", s_nPass, s_nSkip);
	else printf ("ipctest: FAIL (%d of %d; %d skipped)\n", s_nFail, s_nPass + s_nFail, s_nSkip);
	return s_nFail;
}
