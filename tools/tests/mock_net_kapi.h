#ifndef ONYX_MOCK_NET_KAPI_H
#define ONYX_MOCK_NET_KAPI_H
// mock_net_kapi.h -- host mock of the kapi used by user/bin/ftpc.c: files under $ROOT
// (mock_kapi.h), TCP over real BSD sockets, spawn = fork, IPC = one local "service".
#include "mock_kapi.h"
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
static inline unsigned kapi_get_ticks (void) { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return (unsigned) (t.tv_sec * 100 + t.tv_nsec / 10000000); }
static inline void kapi_msleep (unsigned ms) { usleep (ms * 1000); }
static inline int kapi_stdout_write (const char *s, unsigned n) { return (int) write (1, s, n); }
static inline void kapi_exit (int s) { exit (s); }
static inline int kapi_net_status (char *ip, unsigned cap) { if (ip && cap) snprintf (ip, cap, "127.0.0.1"); return 1; }
static inline int kapi_tcp_listen (unsigned port)
{
	int s = socket (AF_INET, SOCK_STREAM, 0), one = 1;
	setsockopt (s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	struct sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons (port); a.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	if (bind (s, (struct sockaddr *) &a, sizeof a) || listen (s, 8)) { close (s); return -6; }
	return s;
}
static inline int kapi_tcp_accept (int l, char *ip, unsigned cap)
{
	struct sockaddr_in a; socklen_t n = sizeof a;
	int s = accept (l, (struct sockaddr *) &a, &n);
	if (s >= 0 && ip) snprintf (ip, cap, "%s", inet_ntoa (a.sin_addr));
	return s;
}
static inline int kapi_tcp_connect (const char *host, unsigned port)
{
	int s = socket (AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons (port); inet_aton (host, &a.sin_addr);
	if (connect (s, (struct sockaddr *) &a, sizeof a)) { close (s); return -3; }
	return s;
}
static inline int kapi_tcp_send (int s, const void *b, unsigned n) { return (int) send (s, b, n, MSG_NOSIGNAL); }
static inline int kapi_tcp_recv (int s, void *b, unsigned n)	// non-blocking: >0 data / 0 none / <0 closed
{
	// Like Circle: one "segment" (<= 1460 bytes) per call, and what does not fit in n
	// is DROPPED -- so a caller reading in small pieces loses data here too.
	static char seg[1460];
	int r = (int) recv (s, seg, sizeof seg, MSG_DONTWAIT);
	if (r > 0) { int k = r < (int) n ? r : (int) n; memcpy (b, seg, k); return k; }
	if (r == 0) return -1;
	return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
}
static inline void kapi_tcp_close (int s) { close (s); }
static inline int kapi_ipc_register (const char *) { return 1; }
static inline int kapi_ipc_lookup (const char *) { return 0; }
static inline int kapi_mailbox_send (int, int, const void *, unsigned) { return 0; }
static inline int kapi_mailbox_recv (int *, int *, void *, unsigned, int) { return -1; }
static int mock_session (char *a);
static char mock_args[1024];
static inline int kapi_get_args (char *b, unsigned n) { snprintf (b, n, "%s", mock_args); return (int) strlen (b); }
static inline void *kapi_spawn (const char *, const char *args, void *, void *)
{
	pid_t p = fork ();
	if (p == 0) { static char a[1024]; snprintf (a, sizeof a, "%s", args + 10); exit (mock_session (a)); }	// skip "--session "
	return (void *) (long) p;
}
static inline int kapi_proc_done (void *h) { int st; return waitpid ((pid_t) (long) h, &st, WNOHANG) != 0; }
static inline int kapi_wait (void *h) { int st = 0; waitpid ((pid_t) (long) h, &st, 0); return st; }
#endif
