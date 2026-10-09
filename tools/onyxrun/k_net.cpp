//
// k_net.cpp -- the network (onyxrun.h), on the PC's own sockets (IPv4): the v21 / v37 / v43 calls (tcp_connect, send,
// recv, close, listen, accept; net_status, net_resolve, net_info, net_ping) and the v75 BSD sockets (sock_*, poll),
// as kernel/sys/net.cpp and bsdsock.cpp give them. One table of 256 sockets for the system, as the kernel's (a
// socket belongs to the process that made it: closed when it ends). The host's socket is always non-blocking; a
// blocking call is a wait in slices (the process's end ends it).
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include "kobj.h"
#include <chrono>
#include <string.h>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET HSock;
#define BAD_SOCK INVALID_SOCKET
static int sock_err (void) { return WSAGetLastError (); }
#define E_WOULDBLOCK WSAEWOULDBLOCK
#define E_INPROGRESS WSAEWOULDBLOCK
#define E_CONNREFUSED WSAECONNREFUSED
#define E_ADDRINUSE WSAEADDRINUSE
#define E_CONNRESET WSAECONNRESET
#define E_NOTCONN WSAENOTCONN
#define E_ISCONN WSAEISCONN
#define E_ALREADY WSAEALREADY
#define E_TIMEDOUT WSAETIMEDOUT
#define E_NETUNREACH WSAENETUNREACH
#define E_PIPE WSAESHUTDOWN
static void close_sock (HSock s) { closesocket (s); }
static void set_nb (HSock s) { u_long one = 1; ioctlsocket (s, FIONBIO, &one); }
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/ioctl.h>
typedef int HSock;
#define BAD_SOCK (-1)
static int sock_err (void) { return errno; }
#define E_WOULDBLOCK EWOULDBLOCK
#define E_INPROGRESS EINPROGRESS
#define E_CONNREFUSED ECONNREFUSED
#define E_ADDRINUSE EADDRINUSE
#define E_CONNRESET ECONNRESET
#define E_NOTCONN ENOTCONN
#define E_ISCONN EISCONN
#define E_ALREADY EALREADY
#define E_TIMEDOUT ETIMEDOUT
#define E_NETUNREACH ENETUNREACH
#define E_PIPE EPIPE
static void close_sock (HSock s) { close (s); }
static void set_nb (HSock s) { fcntl (s, F_SETFL, fcntl (s, F_GETFL) | O_NONBLOCK); }
#endif

typedef std::chrono::steady_clock Clock;

#define SOCKS 256

struct Sock
{
	HSock h = BAD_SOCK;
	int owner = 0;			// its process's pid
	int type = 0;			// KAPI_SOCK_STREAM / _DGRAM
	bool nb = false;		// the guest's non-blocking flag
	bool listening = false;
	int rcvTimeoMs = 0, sndTimeoMs = 0;
	int pendingErr = 0;
	bool eof = false;
};

static std::mutex s_M;
static Sock s_S[SOCKS];

static bool net_init (void)
{
	static bool s_Done = false, s_Ok = false;
	std::lock_guard<std::mutex> L (s_M);
	if (s_Done) return s_Ok;
	s_Done = true;
#ifdef _WIN32
	WSADATA wd;
	s_Ok = WSAStartup (MAKEWORD (2, 2), &wd) == 0;
#else
	s_Ok = true;
#endif
	return s_Ok;
}

static int kerr (int e)			// a host error -> -KAPI_E*
{
	if (e == E_WOULDBLOCK) return -KAPI_EAGAIN;
#ifndef _WIN32
	if (e == EAGAIN) return -KAPI_EAGAIN;
#endif
	if (e == E_CONNREFUSED) return -KAPI_ECONNREFUSED;
	if (e == E_ADDRINUSE) return -KAPI_EADDRINUSE;
	if (e == E_CONNRESET) return -KAPI_ECONNRESET;
	if (e == E_NETUNREACH) return -KAPI_ENETUNREACH;
	if (e == E_PIPE) return -KAPI_EPIPE;
	return -KAPI_EIO;
}

static int new_sock (HSock h, int type)
{
	std::lock_guard<std::mutex> L (s_M);
	for (int i = 0; i < SOCKS; i++)
		if (s_S[i].h == BAD_SOCK && s_S[i].owner == 0)
		{
			s_S[i] = Sock ();
			s_S[i].h = h; s_S[i].type = type; s_S[i].owner = cur () ? cur ()->pid : 0;
			set_nb (h);
			return i;
		}
	close_sock (h);
	return -KAPI_ENFILE;
}

static Sock *get (int s)
{
	if (s < 0 || s >= SOCKS) return 0;
	std::lock_guard<std::mutex> L (s_M);
	return s_S[s].h != BAD_SOCK ? &s_S[s] : 0;
}

static void free_sock (int s)
{
	std::lock_guard<std::mutex> L (s_M);
	if (s < 0 || s >= SOCKS || s_S[s].h == BAD_SOCK) return;
	close_sock (s_S[s].h);
	s_S[s] = Sock ();
}

// (proc.cpp) a process gone: its sockets closed
void net_proc_gone (Proc *P)
{
	std::lock_guard<std::mutex> L (s_M);
	for (auto &s : s_S) if (s.h != BAD_SOCK && s.owner == P->pid) { close_sock (s.h); s = Sock (); }
}

// Wait until h is readable (or writable) for at most ms (-1: no end), in slices -> 1 ready, 0 time up.
static int wait_ready (HSock h, bool write, int ms)
{
	auto until = Clock::now () + std::chrono::milliseconds (ms < 0 ? 0 : ms);
	for (;;)
	{
		fd_set fs;
		FD_ZERO (&fs);
		FD_SET (h, &fs);
		struct timeval tv = { 0, 50000 };
		int r = select ((int) h + 1, write ? 0 : &fs, write ? &fs : 0, 0, &tv);
		if (r > 0) return 1;
		check_dying ();
		if (ms >= 0 && Clock::now () >= until) return 0;
	}
}

static void to_host (const struct kapi_sockaddr &a, sockaddr_in &h)
{
	memset (&h, 0, sizeof h);
	h.sin_family = AF_INET;
	h.sin_port = htons (a.port);
	memcpy (&h.sin_addr, a.addr, 4);
}

static void from_host (const sockaddr_in &h, struct kapi_sockaddr &a)
{
	memset (&a, 0, sizeof a);
	a.family = KAPI_AF_INET;
	a.port = ntohs (h.sin_port);
	memcpy (a.addr, &h.sin_addr, 4);
}

static bool resolve (const std::string &host, in_addr *out)
{
	if (!net_init ()) return false;
	addrinfo hints, *res = 0;
	memset (&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	if (getaddrinfo (host.c_str (), 0, &hints, &res) != 0 || !res) return false;
	*out = ((sockaddr_in *) res->ai_addr)->sin_addr;
	freeaddrinfo (res);
	return true;
}

static std::string ip_text (const in_addr &a)
{
	const u8 *b = (const u8 *) &a;
	char s[20];
	snprintf (s, sizeof s, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
	return s;
}

// The PC's own address on its main network (no packet sent: a UDP socket "connected" to a public address).
static std::string local_ip (void)
{
	if (!net_init ()) return "";
	HSock h = socket (AF_INET, SOCK_DGRAM, 0);
	if (h == BAD_SOCK) return "";
	sockaddr_in a;
	memset (&a, 0, sizeof a);
	a.sin_family = AF_INET; a.sin_port = htons (53);
	inet_pton (AF_INET, "8.8.8.8", &a.sin_addr);
	std::string ip = "127.0.0.1";
	if (connect (h, (sockaddr *) &a, sizeof a) == 0)
	{
		sockaddr_in me;
		socklen_t n = sizeof me;
		if (getsockname (h, (sockaddr *) &me, &n) == 0) ip = ip_text (me.sin_addr);
	}
	close_sock (h);
	return ip;
}

// ---- the old calls (v21, v37, v43) ---------------------------------------------------------------------------------
static int k_net_status (u64 ip, unsigned cap)
{
	std::string s = local_ip ();
	gstr_out (ip, cap, s);
	return s.empty () ? 0 : 1;
}

static int k_net_resolve (u64 host, u64 ip, unsigned cap)
{
	std::string h;
	in_addr a;
	if (!gstr (host, h, 256) || !resolve (h, &a)) return 0;
	gstr_out (ip, cap, ip_text (a));
	return 1;
}

static int k_net_ping (u64 host, unsigned seq, unsigned timeoutMs, u64 ip, unsigned cap)
{
	(void) seq; (void) timeoutMs;
	std::string h;
	in_addr a;
	if (!gstr (host, h, 256)) return -3;
	if (!resolve (h, &a)) return -3;
	gstr_out (ip, cap, ip_text (a));
	return -5;			// (no ICMP from a program on the PC without its rights)
}

static int k_net_info (u64 buf, unsigned cap)
{
	std::string ip = local_ip ();
	std::string s = "up " + std::string (ip.empty () ? "0" : "1") + "\nhostname onyx\nip " + ip + "\nmask 255.255.255.0\ngateway \ndns \ndhcp 1\n";
	std::lock_guard<std::mutex> L (s_M);
	for (int i = 0; i < SOCKS; i++)
		if (s_S[i].h != BAD_SOCK && s_S[i].type == KAPI_SOCK_STREAM)
			s += "tcp " + std::to_string (i) + (s_S[i].listening ? " listen" : " conn") + " 0 0.0.0.0 " + std::to_string (s_S[i].owner) + "\n";
	return gstr_out (buf, cap, s);
}

static int k_tcp_connect (u64 host, unsigned port)
{
	std::string h;
	in_addr a;
	if (!gstr (host, h, 256)) return -1;
	if (!net_init ()) return -1;
	if (!resolve (h, &a)) return -3;
	HSock hs = socket (AF_INET, SOCK_STREAM, 0);
	if (hs == BAD_SOCK) return -2;
	int s = new_sock (hs, KAPI_SOCK_STREAM);
	if (s < 0) return -2;
	sockaddr_in sa;
	memset (&sa, 0, sizeof sa);
	sa.sin_family = AF_INET; sa.sin_port = htons ((u16) port); sa.sin_addr = a;
	int r = connect (hs, (sockaddr *) &sa, sizeof sa);
	if (r != 0 && (sock_err () == E_INPROGRESS || sock_err () == E_WOULDBLOCK))
	{
		if (wait_ready (hs, true, 15000))
		{
			int e = 0;
			socklen_t n = sizeof e;
			getsockopt (hs, SOL_SOCKET, SO_ERROR, (char *) &e, &n);
			r = e == 0 ? 0 : -1;
		}
	}
	if (r != 0) { free_sock (s); return -5; }
	return s;
}

static int k_tcp_send (int s, u64 buf, unsigned len)
{
	Sock *S = get (s);
	u8 *b = GB (buf, len, MEM_R);
	if (!S || !b) return -1;
	unsigned done = 0;
	auto until = Clock::now () + std::chrono::seconds (5);
	while (done < len)
	{
		int n = (int) send (S->h, (const char *) b + done, (int) (len - done), 0);
		if (n > 0) { done += (unsigned) n; continue; }
		if (n < 0 && sock_err () != E_WOULDBLOCK
#ifndef _WIN32
		    && errno != EAGAIN
#endif
		   ) return done ? (int) done : -1;
		if (Clock::now () >= until || !wait_ready (S->h, true, 100)) { if (Clock::now () >= until) break; }
	}
	return (int) done;
}

static int k_tcp_recv (int s, u64 buf, unsigned len)
{
	Sock *S = get (s);
	u8 *b = GB (buf, len, MEM_W);
	if (!S || !b) return -1;
	int n = (int) recv (S->h, (char *) b, (int) len, 0);
	if (n > 0) return n;
	if (n == 0) return -1;				// closed
	int e = sock_err ();
	if (e == E_WOULDBLOCK
#ifndef _WIN32
	    || e == EAGAIN
#endif
	   ) return 0;
	return -1;
}

static void k_tcp_close (int s) { free_sock (s); }

static int listen_on (unsigned port, int backlog)
{
	if (!net_init ()) return -1;
	HSock hs = socket (AF_INET, SOCK_STREAM, 0);
	if (hs == BAD_SOCK) return -2;
	int one = 1;
	setsockopt (hs, SOL_SOCKET, SO_REUSEADDR, (const char *) &one, sizeof one);
	sockaddr_in sa;
	memset (&sa, 0, sizeof sa);
	sa.sin_family = AF_INET; sa.sin_port = htons ((u16) port); sa.sin_addr.s_addr = htonl (INADDR_ANY);
	if (bind (hs, (sockaddr *) &sa, sizeof sa) != 0 || listen (hs, backlog) != 0) { close_sock (hs); return -6; }
	int s = new_sock (hs, KAPI_SOCK_STREAM);
	if (s >= 0) get (s)->listening = true;
	return s < 0 ? -2 : s;
}

static int k_tcp_listen (unsigned port) { return port == 0 || port > 65535 ? -1 : listen_on (port, 8); }

static int k_tcp_accept (int ls, u64 ip, unsigned cap)
{
	Sock *L = get (ls);
	if (!L || !L->listening) return -1;
	for (;;)
	{
		sockaddr_in peer;
		socklen_t n = sizeof peer;
		HSock h = accept (L->h, (sockaddr *) &peer, &n);
		if (h != BAD_SOCK)
		{
			int s = new_sock (h, KAPI_SOCK_STREAM);
			if (s >= 0) gstr_out (ip, cap, ip_text (peer.sin_addr));
			return s;
		}
		wait_ready (L->h, false, -1);
	}
}

// ---- the BSD sockets (v75) -------------------------------------------------------------------------------------
static int k_sock_open (int type, unsigned flags)
{
	if (type != KAPI_SOCK_STREAM && type != KAPI_SOCK_DGRAM) return -KAPI_EAFNOSUPPORT;
	if (!net_init ()) return -KAPI_ENETDOWN;
	HSock h = socket (AF_INET, type == KAPI_SOCK_STREAM ? SOCK_STREAM : SOCK_DGRAM, 0);
	if (h == BAD_SOCK) return -KAPI_ENFILE;
	int s = new_sock (h, type);
	if (s >= 0) get (s)->nb = (flags & KAPI_SOCKF_NONBLOCK) != 0;
	return s;
}

static int k_sock_connect (int s, u64 to)
{
	Sock *S = get (s);
	if (!S) return -KAPI_EBADF;
	struct kapi_sockaddr *a = G<struct kapi_sockaddr> (to, MEM_R);
	if (!a) return -KAPI_EFAULT;
	sockaddr_in sa;
	to_host (*a, sa);
	if (connect (S->h, (sockaddr *) &sa, sizeof sa) == 0) return 0;
	int e = sock_err ();
	if (e == E_ISCONN) return -KAPI_EISCONN;
	if (e != E_INPROGRESS && e != E_WOULDBLOCK && e != E_ALREADY) return kerr (e);
	if (S->nb) return -KAPI_EINPROGRESS;
	if (!wait_ready (S->h, true, 30000)) return -KAPI_ETIMEDOUT;
	int err = 0;
	socklen_t n = sizeof err;
	getsockopt (S->h, SOL_SOCKET, SO_ERROR, (char *) &err, &n);
	return err == 0 ? 0 : kerr (err);
}

static int k_sock_bind (int s, u64 addr)
{
	Sock *S = get (s);
	if (!S) return -KAPI_EBADF;
	struct kapi_sockaddr *a = G<struct kapi_sockaddr> (addr, MEM_R);
	if (!a) return -KAPI_EFAULT;
	int one = 1;
	setsockopt (S->h, SOL_SOCKET, SO_REUSEADDR, (const char *) &one, sizeof one);
	sockaddr_in sa;
	to_host (*a, sa);
	return bind (S->h, (sockaddr *) &sa, sizeof sa) == 0 ? 0 : kerr (sock_err ());
}

static int k_sock_listen (int s, int backlog)
{
	Sock *S = get (s);
	if (!S) return -KAPI_EBADF;
	if (backlog < 1) backlog = 1; else if (backlog > 32) backlog = 32;
	if (listen (S->h, backlog) != 0) return kerr (sock_err ());
	S->listening = true;
	return 0;
}

static int k_sock_accept (int s, u64 peer, unsigned flags)
{
	Sock *S = get (s);
	if (!S) return -KAPI_EBADF;
	for (;;)
	{
		sockaddr_in p;
		socklen_t n = sizeof p;
		HSock h = accept (S->h, (sockaddr *) &p, &n);
		if (h != BAD_SOCK)
		{
			int ns = new_sock (h, KAPI_SOCK_STREAM);
			if (ns < 0) return ns;
			get (ns)->nb = (flags & KAPI_SOCKF_NONBLOCK) != 0;
			if (peer) { struct kapi_sockaddr *a = G<struct kapi_sockaddr> (peer, MEM_W); if (a) from_host (p, *a); }
			return ns;
		}
		if (S->nb) return -KAPI_EAGAIN;
		wait_ready (S->h, false, -1);
	}
}

static long long k_sock_send (int s, u64 buf, unsigned long long len, unsigned flags, u64 to)
{
	Sock *S = get (s);
	if (!S) return -KAPI_EBADF;
	u8 *b = GB (buf, len, MEM_R);
	if (!b && len) return -KAPI_EFAULT;
	sockaddr_in sa;
	struct kapi_sockaddr *a = to ? G<struct kapi_sockaddr> (to, MEM_R) : 0;
	if (a) to_host (*a, sa);
	bool nb = S->nb || (flags & KAPI_MSG_DONTWAIT);
	for (;;)
	{
		int n = a ? (int) sendto (S->h, (const char *) b, (int) len, 0, (sockaddr *) &sa, sizeof sa)
			  : (int) send (S->h, (const char *) b, (int) len, 0);
		if (n >= 0) return n;
		int e = sock_err ();
		if (e != E_WOULDBLOCK
#ifndef _WIN32
		    && e != EAGAIN
#endif
		   ) return kerr (e);
		if (nb) return -KAPI_EAGAIN;
		if (!wait_ready (S->h, true, S->sndTimeoMs > 0 ? S->sndTimeoMs : -1)) return -KAPI_EAGAIN;
	}
}

static long long k_sock_recv (int s, u64 buf, unsigned long long len, unsigned flags, u64 from)
{
	Sock *S = get (s);
	if (!S) return -KAPI_EBADF;
	u8 *b = GB (buf, len, MEM_W);
	if (!b && len) return -KAPI_EFAULT;
	bool nb = S->nb || (flags & KAPI_MSG_DONTWAIT);
	int hf = (flags & KAPI_MSG_PEEK) ? MSG_PEEK : 0;
	for (;;)
	{
		sockaddr_in p;
		socklen_t pn = sizeof p;
		int n = (int) recvfrom (S->h, (char *) b, (int) len, hf, (sockaddr *) &p, &pn);
		if (n >= 0)
		{
			if (from) { struct kapi_sockaddr *a = G<struct kapi_sockaddr> (from, MEM_W); if (a) from_host (p, *a); }
			return n;
		}
		int e = sock_err ();
		if (e != E_WOULDBLOCK
#ifndef _WIN32
		    && e != EAGAIN
#endif
		   ) return kerr (e);
		if (nb) return -KAPI_EAGAIN;
		if (!wait_ready (S->h, false, S->rcvTimeoMs > 0 ? S->rcvTimeoMs : -1)) return -KAPI_EAGAIN;
	}
}

static int k_sock_shutdown (int s, int how)
{
	Sock *S = get (s);
	if (!S) return -KAPI_EBADF;
	return shutdown (S->h, how) == 0 ? 0 : kerr (sock_err ());
}

static int k_sock_close (int s) { if (!get (s)) return -KAPI_EBADF; free_sock (s); return 0; }

static int k_sock_getopt (int s, int opt, u64 value)
{
	Sock *S = get (s);
	if (!S) return -KAPI_EBADF;
	int v = 0;
	switch (opt)
	{
	case KAPI_SO_ERROR: { socklen_t n = sizeof v; getsockopt (S->h, SOL_SOCKET, SO_ERROR, (char *) &v, &n); v = v ? -kerr (v) : 0; break; }
	case KAPI_SO_NONBLOCK: v = S->nb; break;
	case KAPI_SO_RCVTIMEO_MS: v = S->rcvTimeoMs; break;
	case KAPI_SO_SNDTIMEO_MS: v = S->sndTimeoMs; break;
	case KAPI_SO_TYPE: v = S->type; break;
	case KAPI_SO_ACCEPTCONN: v = S->listening; break;
	case KAPI_SO_DOMAIN: v = KAPI_AF_INET; break;
	case KAPI_SO_NREAD:
	{
#ifdef _WIN32
		u_long n = 0; ioctlsocket (S->h, FIONREAD, &n); v = (int) n;
#else
		int n = 0; ioctl (S->h, FIONREAD, &n); v = n;
#endif
		break;
	}
	default: return -KAPI_ENOPROTOOPT;
	}
	gput<int> (value, v);
	return 0;
}

static int k_sock_setopt (int s, int opt, int value)
{
	Sock *S = get (s);
	if (!S) return -KAPI_EBADF;
	switch (opt)
	{
	case KAPI_SO_NONBLOCK: S->nb = value != 0; return 0;
	case KAPI_SO_RCVTIMEO_MS: S->rcvTimeoMs = value; return 0;
	case KAPI_SO_SNDTIMEO_MS: S->sndTimeoMs = value; return 0;
	case KAPI_SO_BROADCAST: { int v = value != 0; setsockopt (S->h, SOL_SOCKET, SO_BROADCAST, (const char *) &v, sizeof v); return 0; }
	}
	return 0;
}

static int k_sock_name (int s, int peer, u64 out)
{
	Sock *S = get (s);
	if (!S) return -KAPI_EBADF;
	struct kapi_sockaddr *a = G<struct kapi_sockaddr> (out, MEM_W);
	if (!a) return -KAPI_EFAULT;
	sockaddr_in p;
	socklen_t n = sizeof p;
	int r = peer ? getpeername (S->h, (sockaddr *) &p, &n) : getsockname (S->h, (sockaddr *) &p, &n);
	if (r != 0) return peer ? -KAPI_ENOTCONN : kerr (sock_err ());
	from_host (p, *a);
	return 0;
}

// poll: the sockets by select, the streams by their state, a file always ready
static int k_poll (u64 fds, unsigned n, int timeout)
{
	if (n > KAPI_POLL_MAX) return -KAPI_EINVAL;
	struct kapi_pollfd *f = n ? (struct kapi_pollfd *) GB (fds, (u64) n * sizeof (struct kapi_pollfd), MEM_R | MEM_W) : 0;
	if (n && !f) return -KAPI_EFAULT;
	auto until = Clock::now () + std::chrono::milliseconds (timeout < 0 ? 0 : timeout);
	for (;;)
	{
		int ready = 0;
		fd_set rs, ws;
		FD_ZERO (&rs); FD_ZERO (&ws);
		int maxfd = 0;
		bool anySock = false;
		for (unsigned i = 0; i < n; i++)
		{
			f[i].revents = 0;
			if (f[i].kind == KAPI_PK_SOCKET)
			{
				Sock *S = get (f[i].h);
				if (!S) { f[i].revents = KAPI_POLLNVAL; continue; }
				FD_SET (S->h, &rs); FD_SET (S->h, &ws);
				if ((int) S->h > maxfd) maxfd = (int) S->h;
				anySock = true;
			}
			else if (f[i].kind == KAPI_PK_STREAM)
			{
				auto o = h_get (cur (), (u64) (unsigned) f[i].h);
				if (!o) { f[i].revents = KAPI_POLLNVAL; continue; }
				if (o->kind == H_PIPE)
				{
					std::lock_guard<std::mutex> L (o->pipe->m);
					if (!o->pipe->data.empty ()) f[i].revents |= f[i].events & KAPI_POLLIN;
					if (o->pipe->eof) f[i].revents |= KAPI_POLLHUP | (f[i].events & KAPI_POLLIN);
					if (o->pipe->data.size () < 65536) f[i].revents |= f[i].events & KAPI_POLLOUT;
				}
				else f[i].revents = f[i].events & (KAPI_POLLIN | KAPI_POLLOUT);
			}
			else if (f[i].kind == KAPI_PK_FILE) f[i].revents = f[i].events & (KAPI_POLLIN | KAPI_POLLOUT);
		}
		if (anySock)
		{
			struct timeval tv = { 0, 0 };
			if (select (maxfd + 1, &rs, &ws, 0, &tv) > 0)
				for (unsigned i = 0; i < n; i++)
				{
					if (f[i].kind != KAPI_PK_SOCKET) continue;
					Sock *S = get (f[i].h);
					if (!S) continue;
					if (FD_ISSET (S->h, &rs)) f[i].revents |= f[i].events & KAPI_POLLIN;
					if (FD_ISSET (S->h, &ws)) f[i].revents |= f[i].events & KAPI_POLLOUT;
				}
		}
		for (unsigned i = 0; i < n; i++) if (f[i].revents) ready++;
		if (ready || timeout == 0) return ready;
		if (timeout > 0 && Clock::now () >= until) return 0;
		std::this_thread::sleep_for (std::chrono::milliseconds (5));
		check_dying ();
	}
}

KAPI (net_status, k_net_status);
KAPI (net_resolve, k_net_resolve);
KAPI (net_ping, k_net_ping);
KAPI (net_info, k_net_info);
KAPI (tcp_connect, k_tcp_connect);
KAPI (tcp_send, k_tcp_send);
KAPI (tcp_recv, k_tcp_recv);
KAPI (tcp_close, k_tcp_close);
KAPI (tcp_listen, k_tcp_listen);
KAPI (tcp_accept, k_tcp_accept);
KAPI (sock_open, k_sock_open);
KAPI (sock_connect, k_sock_connect);
KAPI (sock_bind, k_sock_bind);
KAPI (sock_listen, k_sock_listen);
KAPI (sock_accept, k_sock_accept);
KAPI (sock_send, k_sock_send);
KAPI (sock_recv, k_sock_recv);
KAPI (sock_shutdown, k_sock_shutdown);
KAPI (sock_close, k_sock_close);
KAPI (sock_getopt, k_sock_getopt);
KAPI (sock_setopt, k_sock_setopt);
KAPI (sock_name, k_sock_name);
KAPI (poll, k_poll);
