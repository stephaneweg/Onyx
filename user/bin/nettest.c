//
// nettest -- checks the BSD sockets and poll of kapi v75 (docs/POSIX-PLAN.md §3.3; the kernel side:
// kernel/sys/bsdsock.cpp, kernel/sys/net.cpp). One line per check, "ok" or "FAIL", a summary;
// the exit code is the number of failures.
//
//   nettest                      the local checks, then the Internet ones (example.com, the DNS
//                                server: the Pi needs the network)
//   nettest local                only the checks that need no peer
//   nettest peer <pc-ip> [port]  + the checks against tools/tests/nettest_peer.py on the PC
//                                (default port 7777): a 100 KB stream read 10 bytes at a time,
//                                MSG_PEEK, MSG_WAITALL, the peer's close (EOF, POLLIN), echo,
//                                UDP echo, a closed port (ECONNREFUSED)
//   nettest serve <port>         a server for `nettest_peer.py --client <pi-ip> <port>`: a
//                                non-blocking listen + accept driven by poll, echo, the peers'
//                                closes (POLLIN then 0), three clients at once
//   nettest timeout              + a connect to an address that never answers: ETIMEDOUT
//                                (about a minute)
//
// Run it with netcore=0 and with netcore=1 (cmdline.txt): the two placements of the stack.
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
#include "kapi.h"
#include "applib.h"

static int s_pass, s_fail;

// ---- small helpers (freestanding: no libc) ----------------------------------------------------

static void put_num (long v)
{
	char b[24]; int n = 0;
	if (v < 0) { ax_puts ("-"); v = -v; }
	do { b[n++] = (char) ('0' + v % 10); v /= 10; } while (v != 0);
	while (n > 0) kapi_stdout_write (&b[--n], 1);
}

static void check (const char *what, int ok, long got)
{
	ax_puts (ok ? "  ok    " : "  FAIL  ");
	ax_puts (what);
	ax_puts (" (");
	put_num (got);
	ax_putln (")");
	if (ok) s_pass++; else s_fail++;
}

static void section (const char *name) { ax_puts ("-- "); ax_putln (name); }

static int mem_eq (const void *a, const void *b, int n)
{
	const unsigned char *p = (const unsigned char *) a, *q = (const unsigned char *) b;
	for (int i = 0; i < n; i++) if (p[i] != q[i]) return 0;
	return 1;
}

static int str_starts (const char *s, const char *prefix)
{
	while (*prefix) if (*s++ != *prefix++) return 0;
	return 1;
}

// "a.b.c.d" -> addr[4]; 1 ok
static int parse_ip (const char *s, unsigned char *ip)
{
	unsigned char t[4];				// (ip untouched unless all four parse)
	int k = 0, v = -1;
	for (;; s++)
	{
		if (*s >= '0' && *s <= '9') { v = (v < 0 ? 0 : v * 10) + (*s - '0'); if (v > 255) return 0; }
		else if (*s == '.' || *s == '\0' || *s == ' ' || *s == '\n' || *s == '\r')
		{
			if (v < 0 || k >= 4) return 0;
			t[k++] = (unsigned char) v; v = -1;
			if (*s != '.') break;
		}
		else return 0;
	}
	if (k != 4) return 0;
	for (int i = 0; i < 4; i++) ip[i] = t[i];
	return 1;
}

static struct kapi_sockaddr addr (const unsigned char *ip, unsigned port)
{
	struct kapi_sockaddr a;
	unsigned char *p = (unsigned char *) &a;
	for (unsigned i = 0; i < sizeof a; i++) p[i] = 0;
	a.family = KAPI_AF_INET;
	a.port = (unsigned short) port;
	for (int i = 0; i < 4; i++) a.addr[i] = ip[i];
	return a;
}

static unsigned ms_since (unsigned t0) { return (kapi_clock_us () - t0) / 1000; }

// poll one socket for ev, at most ms -> revents (0: timeout), <0 error
static int poll1 (int s, int ev, int ms)
{
	struct kapi_pollfd f = { KAPI_PK_SOCKET, s, (short) ev, 0, 0 };
	int r = kapi_poll (&f, 1, ms);
	return r < 0 ? r : (r == 0 ? 0 : f.revents);
}

static long send_all (int s, const void *buf, long n)
{
	long done = 0;
	while (done < n)
	{
		long long r = kapi_sock_send (s, (const char *) buf + done, (unsigned long long) (n - done), 0, 0);
		if (r <= 0) return r < 0 ? r : done;
		done += (long) r;
	}
	return done;
}

// A blocking TCP connection to ip:port -> socket, or -errno.
static int tcp_to (const unsigned char *ip, unsigned port)
{
	int s = kapi_sock_open (KAPI_SOCK_STREAM, 0);
	if (s < 0) return s;
	struct kapi_sockaddr a = addr (ip, port);
	int r = kapi_sock_connect (s, &a);
	if (r < 0) { kapi_sock_close (s); return r; }
	return s;
}

static unsigned char pattern (long i) { return (unsigned char) ((i * 7 + 3) & 0xFF); }

// ---- the checks that need no peer -----------------------------------------------------------------

static void test_local (void)
{
	section ("local");
	check ("sock_open (3, 0) -> EPROTONOSUPPORT", kapi_sock_open (3, 0) == -KAPI_EPROTONOSUPPORT, kapi_sock_open (3, 0));
	check ("sock_open (STREAM, 0x80) -> EINVAL", kapi_sock_open (KAPI_SOCK_STREAM, 0x80) == -KAPI_EINVAL, 0);
	check ("sock_close (-1) -> EBADF", kapi_sock_close (-1) == -KAPI_EBADF, 0);
	check ("sock_close (100000) -> EBADF", kapi_sock_close (100000) == -KAPI_EBADF, 0);

	int s = kapi_sock_open (KAPI_SOCK_STREAM, 0);
	check ("sock_open (STREAM) -> a socket", s >= 0, s);
	if (s < 0) return;
	int v = -1;
	check ("getopt SO_TYPE = STREAM", kapi_sock_getopt (s, KAPI_SO_TYPE, &v) == 0 && v == KAPI_SOCK_STREAM, v);
	check ("getopt SO_NONBLOCK = 0", kapi_sock_getopt (s, KAPI_SO_NONBLOCK, &v) == 0 && v == 0, v);
	check ("setopt SO_NONBLOCK 1", kapi_sock_setopt (s, KAPI_SO_NONBLOCK, 1) == 0
	       && kapi_sock_getopt (s, KAPI_SO_NONBLOCK, &v) == 0 && v == 1, v);
	check ("getopt (99) -> ENOPROTOOPT", kapi_sock_getopt (s, 99, &v) == -KAPI_ENOPROTOOPT, 0);
	check ("getopt (bad pointer) -> EFAULT", kapi_sock_getopt (s, KAPI_SO_TYPE, (int *) 16) == -KAPI_EFAULT, 0);
	char b[16];
	long long r = kapi_sock_recv (s, b, sizeof b, 0, 0);
	check ("recv, not connected -> ENOTCONN", r == -KAPI_ENOTCONN, (long) r);
	r = kapi_sock_send (s, "x", 1, 0, 0);
	check ("send, not connected -> ENOTCONN", r == -KAPI_ENOTCONN, (long) r);
	struct kapi_sockaddr a;
	check ("getpeername, not connected -> ENOTCONN", kapi_sock_name (s, 1, &a) == -KAPI_ENOTCONN, 0);
	check ("shutdown, not connected -> ENOTCONN", kapi_sock_shutdown (s, KAPI_SHUT_RDWR) == -KAPI_ENOTCONN, 0);
	unsigned char any[4] = {0, 0, 0, 0};
	a = addr (any, 0);
	a.family = 10;
	check ("connect AF_INET6 -> EAFNOSUPPORT", kapi_sock_connect (s, &a) == -KAPI_EAFNOSUPPORT, 0);
	check ("connect (bad pointer) -> EFAULT", kapi_sock_connect (s, (const struct kapi_sockaddr *) 16) == -KAPI_EFAULT, 0);

	// bind + listen, port 0: an ephemeral port
	a = addr (any, 0);
	int rb = kapi_sock_bind (s, &a);
	check ("bind 0.0.0.0:0", rb == 0, rb);
	int rl = kapi_sock_listen (s, 4);
	check ("listen", rl == 0, rl);
	check ("getopt SO_ACCEPTCONN = 1", kapi_sock_getopt (s, KAPI_SO_ACCEPTCONN, &v) == 0 && v == 1, v);
	struct kapi_sockaddr me;
	int rn = kapi_sock_name (s, 0, &me);
	check ("getsockname: a port", rn == 0 && me.family == KAPI_AF_INET && me.port != 0, me.port);
	struct kapi_sockaddr peer;
	int ra = kapi_sock_accept (s, &peer, 0);
	check ("non-blocking accept, nobody -> EAGAIN", ra == -KAPI_EAGAIN, ra);

	int s2 = kapi_sock_open (KAPI_SOCK_STREAM, 0);
	a = addr (any, me.port);
	int rb2 = kapi_sock_bind (s2, &a);
	check ("bind to the same port -> EADDRINUSE", rb2 == -KAPI_EADDRINUSE, rb2);
	kapi_sock_close (s2);

	// poll: a timeout, accuracy
	unsigned t0 = kapi_clock_us ();
	int rp = poll1 (s, KAPI_POLLIN, 100);
	unsigned el = ms_since (t0);
	check ("poll (listening, nobody), 100 ms -> 0", rp == 0, rp);
	check ("  ... after 100 +- 15 ms", el >= 85 && el <= 115, (long) el);
	t0 = kapi_clock_us ();
	rp = poll1 (s, KAPI_POLLIN, 0);
	check ("poll 0 ms: only a look", rp == 0 && ms_since (t0) < 5, (long) ms_since (t0));

	// poll over mixed handle kinds: a pipe (writable), an invalid handle, an ignored entry
	void *pipe = kapi_pipe ();
	struct kapi_pollfd f[5] = {
		{ KAPI_PK_SOCKET, s, KAPI_POLLIN, 0, 0 },
		{ KAPI_PK_STREAM, (int) (long) pipe, KAPI_POLLOUT, 0, 0 },
		{ KAPI_PK_STREAM, 0x123456, KAPI_POLLIN, 0, 0 },
		{ KAPI_PK_NONE, 5, KAPI_POLLIN, 0, 0 },
		{ KAPI_PK_SOCKET, -1, KAPI_POLLIN, 0, 0 },
	};
	int rm = kapi_poll (f, 5, 1000);
	check ("poll mixed: 2 ready (the pipe, the bad handle)", rm == 2, rm);
	check ("  ... socket: nothing", f[0].revents == 0, f[0].revents);
	check ("  ... pipe: POLLOUT", (f[1].revents & KAPI_POLLOUT) != 0, f[1].revents);
	check ("  ... a bad handle: POLLNVAL", f[2].revents == KAPI_POLLNVAL, f[2].revents);
	check ("  ... kind 0 and h < 0: ignored", f[3].revents == 0 && f[4].revents == 0, 0);
	check ("poll (n > 1024) -> EINVAL", kapi_poll (f, 1025, 0) == -KAPI_EINVAL, 0);
	check ("poll (bad pointer) -> EFAULT", kapi_poll ((struct kapi_pollfd *) 16, 1, 0) == -KAPI_EFAULT, 0);
	kapi_stream_close (pipe);

	kapi_sock_close (s);
	f[0].revents = 0;
	rm = kapi_poll (f, 1, 0);
	check ("poll a closed socket: POLLNVAL", rm == 1 && f[0].revents == KAPI_POLLNVAL, f[0].revents);

	// UDP: bind to port 0, the port; no default peer
	int u = kapi_sock_open (KAPI_SOCK_DGRAM, KAPI_SOCKF_NONBLOCK);
	check ("sock_open (DGRAM, NONBLOCK)", u >= 0, u);
	if (u >= 0)
	{
		a = addr (any, 0);
		check ("UDP bind :0", kapi_sock_bind (u, &a) == 0, 0);
		rn = kapi_sock_name (u, 0, &me);
		check ("UDP getsockname: a port", rn == 0 && me.port != 0, me.port);
		r = kapi_sock_send (u, "x", 1, 0, 0);
		check ("UDP send, no peer -> EDESTADDRREQ", r == -KAPI_EDESTADDRREQ, (long) r);
		r = kapi_sock_recv (u, b, sizeof b, 0, 0);
		check ("UDP recv, nothing -> EAGAIN", r == -KAPI_EAGAIN, (long) r);
		static char big[2000];
		unsigned char lo[4] = {192, 0, 2, 1};			// (TEST-NET-1: nobody)
		a = addr (lo, 9);
		r = kapi_sock_send (u, big, sizeof big, 0, &a);
		check ("UDP send 2000 bytes -> EMSGSIZE", r == -KAPI_EMSGSIZE, (long) r);
		check ("UDP poll: POLLOUT", poll1 (u, KAPI_POLLOUT, 0) == KAPI_POLLOUT, 0);
		kapi_sock_close (u);
	}

	// 200 sockets open, then closed (the table: 256, shared by every process)
	static int many[200];
	int n = 0;
	for (int i = 0; i < 200; i++)
	{
		many[i] = kapi_sock_open (i & 1 ? KAPI_SOCK_DGRAM : KAPI_SOCK_STREAM, 0);
		if (many[i] >= 0) n++;
	}
	check ("200 sockets opened", n == 200, n);
	int closed = 0;
	for (int i = 0; i < 200; i++) if (many[i] >= 0 && kapi_sock_close (many[i]) == 0) closed++;
	check ("  ... and closed", closed == n, closed);
}

// ---- the Internet ------------------------------------------------------------------------------

// An HTTP/1.0 GET / of host on s; the whole answer into buf (at most cap) using reads of `step`
// bytes -> its length, or <0.
static long http_get (int s, const char *host, char *buf, long cap, int step)
{
	char req[160]; int k = 0;
	const char *parts[3] = { "GET / HTTP/1.0\r\nHost: ", host, "\r\nConnection: close\r\n\r\n" };
	for (int p = 0; p < 3; p++) for (const char *q = parts[p]; *q && k < 159; q++) req[k++] = *q;
	if (send_all (s, req, k) != k) return -1;
	long n = 0;
	for (;;)
	{
		long want = cap - n < step ? cap - n : step;
		if (want <= 0) break;
		long long r = kapi_sock_recv (s, buf + n, (unsigned long long) want, 0, 0);
		if (r == 0) break;
		if (r < 0) return n > 0 ? n : (long) r;
		n += (long) r;
	}
	return n;
}

static long body_of (const char *buf, long n)		// offset after "\r\n\r\n", -1 none
{
	for (long i = 0; i + 3 < n; i++)
		if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') return i + 4;
	return -1;
}

static void test_internet (void)
{
	section ("Internet (example.com:80, DNS)");
	char ipt[40] = "";
	int ok = kapi_net_resolve ("example.com", ipt, sizeof ipt);
	check ("net_resolve (example.com)", ok == 1, ok);
	unsigned char ip[4];
	if (ok != 1 || !parse_ip (ipt, ip)) { ax_putln ("  (no network: the Internet checks skipped)"); return; }

	// blocking connect + GET, read in one go
	static char one[65536], small[65536];
	int s = tcp_to (ip, 80);
	check ("blocking connect :80", s >= 0, s);
	if (s < 0) return;
	struct kapi_sockaddr me, peer;
	check ("getsockname: our IP and port", kapi_sock_name (s, 0, &me) == 0 && me.port != 0 && me.addr[0] != 0, me.port);
	check ("getpeername: example.com:80", kapi_sock_name (s, 1, &peer) == 0 && peer.port == 80
	       && mem_eq (peer.addr, ip, 4), peer.port);
	long n1 = http_get (s, "example.com", one, sizeof one, 65536);
	check ("GET, big reads: an answer", n1 > 100 && str_starts (one, "HTTP/1."), n1);
	long long r = kapi_sock_recv (s, one, 16, 0, 0);
	check ("after the server's close: recv -> 0", r == 0, (long) r);
	int rv = poll1 (s, KAPI_POLLIN, 0);
	check ("  ... poll: POLLIN", (rv & KAPI_POLLIN) != 0, rv);
	kapi_sock_close (s);

	// non-blocking connect + poll (POLLOUT) + SO_ERROR, then 10-byte reads
	s = kapi_sock_open (KAPI_SOCK_STREAM, KAPI_SOCKF_NONBLOCK);
	struct kapi_sockaddr a = addr (ip, 80);
	int rc = kapi_sock_connect (s, &a);
	check ("non-blocking connect -> EINPROGRESS", rc == -KAPI_EINPROGRESS, rc);
	check ("  ... connect again -> EALREADY", rc != -KAPI_EINPROGRESS || kapi_sock_connect (s, &a) == -KAPI_EALREADY, 0);
	unsigned t0 = kapi_clock_us ();
	rv = poll1 (s, KAPI_POLLOUT, 10000);
	check ("  ... poll POLLOUT", (rv & KAPI_POLLOUT) != 0, (long) ms_since (t0));
	int err = -1;
	check ("  ... SO_ERROR = 0", kapi_sock_getopt (s, KAPI_SO_ERROR, &err) == 0 && err == 0, err);
	check ("  ... connect again -> EISCONN", kapi_sock_connect (s, &a) == -KAPI_EISCONN, 0);
	kapi_sock_setopt (s, KAPI_SO_NONBLOCK, 0);
	long n2 = http_get (s, "example.com", small, sizeof small, 10);
	long b1 = body_of (one, n1), b2 = body_of (small, n2);
	check ("GET, 10-byte reads: the same body", b1 > 0 && b2 > 0 && n1 - b1 == n2 - b2
	       && mem_eq (one + b1, small + b2, (int) (n1 - b1)), n2);
	kapi_sock_close (s);

	// MSG_PEEK, then the same bytes read
	s = tcp_to (ip, 80);
	if (s >= 0)
	{
		send_all (s, "HEAD / HTTP/1.0\r\nHost: example.com\r\n\r\n", 39);
		char p1[8], p2[8];
		rv = poll1 (s, KAPI_POLLIN, 5000);
		long long k1 = kapi_sock_recv (s, p1, 8, KAPI_MSG_PEEK, 0);
		int nread = -1;
		kapi_sock_getopt (s, KAPI_SO_NREAD, &nread);
		long long k2 = kapi_sock_recv (s, p2, 8, 0, 0);
		check ("MSG_PEEK then recv: the same 8 bytes", k1 == 8 && k2 == 8 && mem_eq (p1, p2, 8), (long) k1);
		check ("  ... SO_NREAD > 0 after a peek", nread > 0, nread);
		kapi_sock_close (s);
	}

	// legacy tcp_* on the same table: a tcp handle polled, read 7 bytes at a time
	int h = kapi_tcp_connect ("example.com", 80);
	check ("tcp_connect (legacy)", h >= 0, h);
	if (h >= 0)
	{
		const char *q = "GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n";
		kapi_tcp_send (h, q, (unsigned) ax_strlen (q));
		long n = 0;
		unsigned t1 = kapi_clock_us ();
		while (n < (long) sizeof small && ms_since (t1) < 10000)
		{
			int k = kapi_tcp_recv (h, small + n, 7);
			if (k < 0) break;
			if (k == 0) { poll1 (h, KAPI_POLLIN, 1000); continue; }
			n += k;
		}
		long b3 = body_of (small, n);
		check ("  ... tcp_recv by 7 bytes, poll on it: the same body", b3 > 0 && n - b3 == n1 - b1
		       && mem_eq (one + b1, small + b3, (int) (n1 - b1)), n);
		kapi_tcp_close (h);
	}

	// UDP: a DNS query (example.com, A) to the configured DNS server
	static char info[2048];
	kapi_net_info (info, sizeof info);
	unsigned char dns[4] = {8, 8, 8, 8};
	for (char *p = info; *p; p++)
		if ((p == info || p[-1] == '\n') && str_starts (p, "dns ")) parse_ip (p + 4, dns);
	int u = kapi_sock_open (KAPI_SOCK_DGRAM, 0);
	unsigned char qry[64] = { 0x4F, 0x4E, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0,
		7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0, 0, 1, 0, 1 };
	a = addr (dns, 53);
	r = kapi_sock_send (u, qry, 29, 0, &a);
	check ("UDP sendto the DNS server", r == 29, (long) r);
	kapi_sock_setopt (u, KAPI_SO_RCVTIMEO_MS, 3000);
	static unsigned char ans[1500];
	struct kapi_sockaddr from;
	r = kapi_sock_recv (u, ans, sizeof ans, 0, &from);
	check ("  ... recvfrom: the answer (id, QR, ANCOUNT > 0)", r > 12 && ans[0] == 0x4F && ans[1] == 0x4E
	       && (ans[2] & 0x80) && (ans[6] | ans[7]) != 0, (long) r);
	check ("  ... from the server's port 53", from.port == 53 && mem_eq (from.addr, dns, 4), from.port);
	t0 = kapi_clock_us ();
	r = kapi_sock_recv (u, ans, sizeof ans, 0, 0);
	check ("  ... recv again: SO_RCVTIMEO 3 s -> EAGAIN", r == -KAPI_EAGAIN && ms_since (t0) >= 2900, (long) ms_since (t0));
	kapi_sock_close (u);

	// a connect that is closed before it ends (TEST-NET-1: never answers)
	s = kapi_sock_open (KAPI_SOCK_STREAM, KAPI_SOCKF_NONBLOCK);
	unsigned char black[4] = {192, 0, 2, 1};
	a = addr (black, 80);
	rc = kapi_sock_connect (s, &a);
	rv = poll1 (s, KAPI_POLLOUT, 300);
	check ("connect to nobody: still in progress after 300 ms", rc == -KAPI_EINPROGRESS && rv == 0, rv);
	check ("  ... closed while connecting", kapi_sock_close (s) == 0, 0);
}

static void test_timeout (void)
{
	section ("ETIMEDOUT (about a minute)");
	int s = kapi_sock_open (KAPI_SOCK_STREAM, 0);
	unsigned char black[4] = {192, 0, 2, 1};
	struct kapi_sockaddr a = addr (black, 80);
	unsigned t0 = kapi_clock_us ();
	int r = kapi_sock_connect (s, &a);
	check ("blocking connect to nobody -> ETIMEDOUT", r == -KAPI_ETIMEDOUT, (long) ms_since (t0));
	kapi_sock_close (s);
}

// ---- against tools/tests/nettest_peer.py ---------------------------------------------------------

static void test_peer (const unsigned char *ip, unsigned port)
{
	section ("peer (tools/tests/nettest_peer.py)");

	// a closed port: refused (the PC answers RST)
	int s = tcp_to (ip, port + 1);
	check ("blocking connect to a closed port -> ECONNREFUSED", s == -KAPI_ECONNREFUSED, s);
	s = kapi_sock_open (KAPI_SOCK_STREAM, KAPI_SOCKF_NONBLOCK);
	struct kapi_sockaddr a = addr (ip, port + 1);
	kapi_sock_connect (s, &a);
	int rv = poll1 (s, KAPI_POLLOUT, 5000);
	int err = 0;
	kapi_sock_getopt (s, KAPI_SO_ERROR, &err);
	check ("non-blocking: POLLERR + SO_ERROR ECONNREFUSED", (rv & KAPI_POLLERR) && err == KAPI_ECONNREFUSED, err);
	kapi_sock_close (s);

	// a 100 KB stream read 10 bytes at a time: no byte lost
	s = tcp_to (ip, port);
	check ("connect to the peer", s >= 0, s);
	if (s < 0) return;
	send_all (s, "STREAM 102400\n", 14);
	long n = 0, bad = -1;
	char b[10];
	for (;;)
	{
		long long r = kapi_sock_recv (s, b, sizeof b, 0, 0);
		if (r <= 0) break;
		for (int i = 0; i < (int) r; i++) if (bad < 0 && (unsigned char) b[i] != pattern (n + i)) bad = n + i;
		n += (long) r;
	}
	check ("100 KB by 10-byte reads: every byte, in order", n == 102400 && bad < 0, bad < 0 ? n : bad);
	rv = poll1 (s, KAPI_POLLIN, 0);
	check ("  ... the peer's close: POLLIN, recv 0", (rv & KAPI_POLLIN) && kapi_sock_recv (s, b, 1, 0, 0) == 0, rv);
	kapi_sock_close (s);

	// MSG_WAITALL: one call for 50000 bytes
	s = tcp_to (ip, port);
	send_all (s, "STREAM 50000\n", 13);
	static char big[50000];
	long long r = kapi_sock_recv (s, big, sizeof big, KAPI_MSG_WAITALL, 0);
	check ("MSG_WAITALL 50000 in one call", r == 50000 && (unsigned char) big[49999] == pattern (49999), (long) r);
	kapi_sock_close (s);

	// echo, MSG_PEEK, non-blocking recv, MSG_DONTWAIT
	s = tcp_to (ip, port);
	send_all (s, "ECHO\n", 5);
	r = kapi_sock_recv (s, b, sizeof b, KAPI_MSG_DONTWAIT, 0);
	check ("MSG_DONTWAIT, nothing yet -> EAGAIN", r == -KAPI_EAGAIN, (long) r);
	send_all (s, "hello onyx", 10);
	rv = poll1 (s, KAPI_POLLIN, 3000);
	char p[10];
	long long k1 = kapi_sock_recv (s, p, 5, KAPI_MSG_PEEK, 0);
	long long k2 = kapi_sock_recv (s, b, 10, KAPI_MSG_WAITALL, 0);
	check ("echo: PEEK 5 then WAITALL 10", k1 == 5 && k2 == 10 && mem_eq (p, "hello", 5) && mem_eq (b, "hello onyx", 10), (long) k2);
	check ("shutdown (SHUT_WR)", kapi_sock_shutdown (s, KAPI_SHUT_WR) == 0, 0);
	r = kapi_sock_send (s, "x", 1, 0, 0);
	check ("  ... send after it -> EPIPE", r == -KAPI_EPIPE, (long) r);
	check ("shutdown (SHUT_RD): recv 0", kapi_sock_shutdown (s, KAPI_SHUT_RD) == 0 && kapi_sock_recv (s, b, 1, 0, 0) == 0, 0);
	kapi_sock_close (s);

	// a big blocking send (backpressure: the peer drains it)
	s = tcp_to (ip, port);
	send_all (s, "SINK\n", 5);
	static char out[300000];
	for (long i = 0; i < (long) sizeof out; i++) out[i] = (char) pattern (i);
	unsigned t0 = kapi_clock_us ();
	long sent = send_all (s, out, sizeof out);
	check ("blocking send 300 KB to a slow reader", sent == (long) sizeof out, (long) ms_since (t0));
	kapi_sock_shutdown (s, KAPI_SHUT_RDWR);
	kapi_sock_close (s);

	// UDP echo (the peer's UDP port = its TCP port)
	int u = kapi_sock_open (KAPI_SOCK_DGRAM, 0);
	a = addr (ip, port);
	check ("UDP connect (the default peer)", kapi_sock_connect (u, &a) == 0, 0);
	r = kapi_sock_send (u, "datagram!", 9, 0, 0);
	kapi_sock_setopt (u, KAPI_SO_RCVTIMEO_MS, 3000);
	struct kapi_sockaddr from;
	char d[4];
	r = kapi_sock_recv (u, d, 4, 0, &from);
	check ("UDP echo: cut to 4 bytes, the rest dropped", r == 4 && mem_eq (d, "data", 4) && from.port == port, (long) r);
	r = kapi_sock_recv (u, d, 4, KAPI_MSG_DONTWAIT, 0);
	check ("  ... nothing left", r == -KAPI_EAGAIN, (long) r);
	struct kapi_sockaddr pn;
	check ("  ... getpeername: the peer", kapi_sock_name (u, 1, &pn) == 0 && pn.port == port, pn.port);
	kapi_sock_close (u);
}

// ---- nettest serve <port> ------------------------------------------------------------------------

#define MAX_CLIENTS 8

static void serve (unsigned port)
{
	section ("serve");
	int l = kapi_sock_open (KAPI_SOCK_STREAM, KAPI_SOCKF_NONBLOCK);
	unsigned char any[4] = {0, 0, 0, 0};
	struct kapi_sockaddr a = addr (any, port);
	check ("bind", kapi_sock_bind (l, &a) == 0, port);
	check ("listen", kapi_sock_listen (l, 8) == 0, 0);
	ax_puts ("  waiting for nettest_peer.py --client <this IP> "); put_num (port); ax_putln (" (60 s)");
	int c[MAX_CLIENTS], nc = 0, accepted = 0, echoed = 0, hups = 0;
	unsigned t0 = kapi_clock_us ();
	while (ms_since (t0) < 60000 && !(accepted >= 3 && nc == 0))
	{
		struct kapi_pollfd f[1 + MAX_CLIENTS];
		f[0].kind = KAPI_PK_SOCKET; f[0].h = l; f[0].events = KAPI_POLLIN; f[0].revents = 0; f[0].reserved = 0;
		for (int i = 0; i < nc; i++)
		{
			f[1 + i].kind = KAPI_PK_SOCKET; f[1 + i].h = c[i]; f[1 + i].events = KAPI_POLLIN;
			f[1 + i].revents = 0; f[1 + i].reserved = 0;
		}
		int r = kapi_poll (f, (unsigned) (1 + nc), 1000);
		if (r < 0) { check ("poll", 0, r); break; }
		if (f[0].revents & KAPI_POLLIN)
		{
			struct kapi_sockaddr peer;
			int s;
			while (nc < MAX_CLIENTS && (s = kapi_sock_accept (l, &peer, KAPI_SOCKF_NONBLOCK)) >= 0)
			{
				c[nc++] = s; accepted++;
				ax_puts ("  accepted "); put_num (peer.addr[0]); ax_puts ("."); put_num (peer.addr[1]);
				ax_puts ("."); put_num (peer.addr[2]); ax_puts ("."); put_num (peer.addr[3]);
				ax_puts (":"); put_num (peer.port); ax_putln ("");
			}
		}
		for (int i = nc - 1; i >= 0; i--)
		{
			if (!(f[1 + i].revents & (KAPI_POLLIN | KAPI_POLLHUP | KAPI_POLLERR))) continue;
			char b[512];
			long long k = kapi_sock_recv (c[i], b, sizeof b, 0, 0);
			if (k > 0) { send_all (c[i], b, (long) k); echoed++; continue; }
			if (k == -KAPI_EAGAIN) continue;
			hups++;					// the end (0) or an error
			kapi_sock_close (c[i]);
			c[i] = c[--nc];
		}
	}
	check ("3 clients accepted (non-blocking, by poll)", accepted >= 3, accepted);
	check ("  ... echoed", echoed >= 3, echoed);
	check ("  ... their closes seen (POLLIN, recv 0)", hups >= 3, hups);
	for (int i = 0; i < nc; i++) kapi_sock_close (c[i]);
	kapi_sock_close (l);
}

int main (void)
{
	char args[128];
	kapi_get_args (args, sizeof args);
	char *w = args;
	while (*w == ' ') w++;
	if (kapi_sock_open (KAPI_SOCK_STREAM, 0) == -KAPI_ENOSYS)
	{
		ax_putln ("nettest: this kernel has no BSD sockets (kapi v75)");
		return 1;
	}
	// (the probe above opened a socket: closing the process closes it)
	if (str_starts (w, "serve"))
	{
		unsigned port = 0;
		for (char *p = w + 5; *p; p++) if (*p >= '0' && *p <= '9') port = port * 10 + (unsigned) (*p - '0');
		serve (port ? port : 7777);
	}
	else
	{
		test_local ();
		if (!str_starts (w, "local"))
		{
			test_internet ();
			if (str_starts (w, "timeout")) test_timeout ();
			if (str_starts (w, "peer"))
			{
				char *p = w + 4;
				while (*p == ' ') p++;
				unsigned char ip[4];
				unsigned port = 7777;
				char *q = p;
				while (*q && *q != ' ') q++;
				while (*q == ' ') q++;
				if (*q) { port = 0; while (*q >= '0' && *q <= '9') port = port * 10 + (unsigned) (*q++ - '0'); }
				if (parse_ip (p, ip)) test_peer (ip, port);
				else ax_putln ("usage: nettest peer <pc-ip> [port]");
			}
		}
	}
	ax_puts (s_fail ? "FAIL: " : "PASS: "); put_num (s_pass); ax_puts (" passed, "); put_num (s_fail); ax_putln (" failed");
	return s_fail;
}
