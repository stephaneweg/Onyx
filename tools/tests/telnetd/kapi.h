// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
//
// kapi.h -- the mock kapi of tools/tests/run_telnetd_test.sh: what user/bin/telnetd.c and
// user/bin/shellend.h call, on the PC, in one thread and with a clock of its own (kapi_msleep moves
// it, and gives the fake shell its turn). The pipes are the kernel's (8 KB, a blocking write
// waits for the reader: here it fails the test -- nothing else runs); the "client" and the
// "shell" are scripted by the test (telnetd_test.c).
#ifndef MOCK_TELNETD_KAPI_H
#define MOCK_TELNETD_KAPI_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KAPI_ENOSYS		38
#define KAPI_WAIT_NOHANG	1
#define KAPI_WAIT_KEEP		2
struct kapi_proc_status { int code, reason, pid, pad; };

static struct { int version; } mock_kt = { 80 };
#define KT (&mock_kt)

// ---- the clock ---------------------------------------------------------------------------
static long mock_now;				// ms
static void mock_shell_step (void);		// (telnetd_test.c: the fake shell, every 10 ms)
static void mock_fail (const char *why) { printf ("FAIL: %s (t = %ld ms)\n", why, mock_now); exit (1); }
static inline void kapi_msleep (unsigned ms)
{
	for (unsigned k = 0; k < ms; k += 10) { mock_now += 10; mock_shell_step (); }
	if (mock_now > 3600000) mock_fail ("the session never ended");
}

// ---- pipes -------------------------------------------------------------------------------
#define MOCK_PIPE_CAP	8192
struct MockPipe { char buf[MOCK_PIPE_CAP]; int n, eof, closed; };
static struct MockPipe mock_pipes[4]; static int mock_npipes;

static inline void *kapi_pipe (void) { return &mock_pipes[mock_npipes++]; }
static inline int kapi_stream_write_nb (void *h, const void *b, unsigned n)
{
	struct MockPipe *p = (struct MockPipe *) h;
	unsigned room = (unsigned) (MOCK_PIPE_CAP - 1 - p->n);
	if (n == 0) return 0;
	if (room == 0) return -2;
	if (n > room) n = room;
	memcpy (p->buf + p->n, b, n); p->n += (int) n;
	return (int) n;
}
static inline int kapi_stream_write (void *h, const void *b, unsigned n)
{
	if (kapi_stream_write_nb (h, b, n) != (int) n) mock_fail ("a write on a full pipe: telnetd would wait for ever");
	return (int) n;
}
static inline int kapi_stream_read_nb (void *h, void *b, unsigned n)
{
	struct MockPipe *p = (struct MockPipe *) h;
	if (p->n == 0) return p->eof ? 0 : -1;
	if (n > (unsigned) p->n) n = (unsigned) p->n;
	memcpy (b, p->buf, n); memmove (p->buf, p->buf + n, (size_t) p->n - n); p->n -= (int) n;
	return (int) n;
}
static inline void kapi_stream_eof (void *h) { ((struct MockPipe *) h)->eof = 1; }
static inline void kapi_stream_close (void *h) { ((struct MockPipe *) h)->closed++; }

// ---- the shell's process -------------------------------------------------------------------
struct MockProc { int started, done, reaped, killed, kill_at; struct MockPipe *in, *out; };
static struct MockProc mock_proc;
#define MOCK_PID	42

static inline void *kapi_spawn (const char *path, const char *args, void *in, void *out)
{
	(void) args;
	if (strcmp (path, "SD:/bin/cmd") != 0) return 0;
	mock_proc.started = 1; mock_proc.in = (struct MockPipe *) in; mock_proc.out = (struct MockPipe *) out;
	return &mock_proc;
}
static inline int kapi_proc_done (void *p) { return ((struct MockProc *) p)->done; }
static inline int kapi_wait (void *p)
{
	if (!((struct MockProc *) p)->done) mock_fail ("kapi_wait on a running shell: telnetd would wait for ever");
	((struct MockProc *) p)->reaped++;
	return 0;
}
static inline int kapi_proc_wait (void *p, unsigned flags, struct kapi_proc_status *st)
{
	if (flags != (KAPI_WAIT_NOHANG | KAPI_WAIT_KEEP)) mock_fail ("proc_wait: unexpected flags");
	st->pid = MOCK_PID; st->reason = -1; st->code = 0;
	return ((struct MockProc *) p)->done;
}
static inline int kapi_kill_pid (int pid, int force)
{
	if (pid != MOCK_PID || force != 1) mock_fail ("kill_pid: not the shell's pid, or not forced");
	mock_proc.killed++; mock_proc.kill_at = (int) mock_now; mock_proc.done = 1;
	return 1;
}

// ---- the client's connection ---------------------------------------------------------------
// What the client types, and when; when it closes; or when it vanishes without a word (then the
// first thing sent to it afterwards is answered by a reset: tcp_recv fails from then on).
struct MockEvent { long at; const char *data; };
static struct
{
	const struct MockEvent *ev; int nev, next;
	long close_at, vanish_at;		// (-1: never)
	int  reset;				// (a send after vanish_at)
	char sent[65536]; int nsent;		// what telnetd sent
	long nop_at; int nops;			// the last IAC NOP
	int  closed;
} mock_sock;

static inline int kapi_tcp_recv (int h, void *b, unsigned n)
{
	(void) h;
	if (mock_sock.reset) return -1;
	if (mock_sock.vanish_at >= 0 && mock_now >= mock_sock.vanish_at) return 0;
	if (mock_sock.next < mock_sock.nev && mock_now >= mock_sock.ev[mock_sock.next].at)
	{
		const char *d = mock_sock.ev[mock_sock.next++].data;
		unsigned k = (unsigned) strlen (d);
		if (k > n) mock_fail ("the test's line is too long");
		memcpy (b, d, k);
		return (int) k;
	}
	if (mock_sock.close_at >= 0 && mock_now >= mock_sock.close_at) return -1;
	return 0;
}
static inline int kapi_tcp_send (int h, const void *b, unsigned n)
{
	(void) h;
	if (mock_sock.vanish_at >= 0 && mock_now >= mock_sock.vanish_at) { mock_sock.reset = 1; }
	if (n == 2 && ((const unsigned char *) b)[0] == 255 && ((const unsigned char *) b)[1] == 241)
		{ mock_sock.nops++; mock_sock.nop_at = mock_now; }
	if (mock_sock.nsent + (int) n < (int) sizeof mock_sock.sent)
		{ memcpy (mock_sock.sent + mock_sock.nsent, b, n); mock_sock.nsent += (int) n; }
	return (int) n;
}
static inline void kapi_tcp_close (int h) { (void) h; mock_sock.closed++; }

// ---- what telnetd's main uses (not run by the test) ------------------------------------------
static inline void kapi_lock (volatile int *l) { (void) l; }
static inline void kapi_unlock (volatile int *l) { (void) l; }
static inline int  kapi_stdout_write (const void *b, unsigned n) { (void) b; return (int) n; }
static inline int  kapi_get_args (char *b, unsigned n) { if (n) b[0] = '\0'; return 0; }
static inline int  kapi_net_status (char *b, unsigned n) { snprintf (b, n, "127.0.0.1"); return 1; }
static inline int  kapi_tcp_listen (unsigned port) { (void) port; return -1; }
static inline int  kapi_tcp_accept (int h, char *ip, unsigned n) { (void) h; (void) ip; (void) n; return -1; }
static inline int  kapi_thread_create (int (*fn) (void *), void *arg, unsigned stack, const char *name)
	{ (void) fn; (void) arg; (void) stack; (void) name; return -1; }
#endif
