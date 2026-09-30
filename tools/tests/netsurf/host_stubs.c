/*
 * host_stubs.c -- https for the PC build of NetSurf (host.mk): the Pi build's onyx_nstls.cpp
 * wraps mbedTLS over the Onyx TCP kapis; here the PC's OpenSSL over the simulator's real
 * sockets (SIM_REALNET=1: a socket handle is the file descriptor + 1000), the system's
 * certificates checked. Without SIM_REALNET there is no network: every call fails.
 */
#include <stddef.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "onyx_nstls.h"
#include "kapi.h"

struct onyx_tls_sess { SSL *ssl; int fd; };

static SSL_CTX *ctx(void)
{
	static SSL_CTX *c;
	if (c == NULL) {
		c = SSL_CTX_new(TLS_client_method());
		SSL_CTX_set_default_verify_paths(c);
		SSL_CTX_set_verify(c, getenv("NS_TLS_NOVERIFY") ? SSL_VERIFY_NONE : SSL_VERIFY_PEER, NULL);
		SSL_CTX_set_alpn_protos(c, (const unsigned char *) "\x08http/1.1", 9);
	}
	return c;
}

onyx_tls_sess *onyx_nstls_start(int sock, const char *host)
{
	if (sock < 1000) return NULL;
	onyx_tls_sess *s = calloc(1, sizeof *s);
	s->fd = sock - 1000;
	s->ssl = SSL_new(ctx());
	SSL_set_fd(s->ssl, s->fd);
	SSL_set_tlsext_host_name(s->ssl, host);
	SSL_set1_host(s->ssl, host);
	if (SSL_connect(s->ssl) != 1) {
		SSL_free(s->ssl); close(s->fd); free(s);
		return NULL;
	}
	return s;
}

onyx_tls_sess *onyx_nstls_open(const char *host, unsigned port)
{
	int sock = kapi_tcp_connect(host, port);
	if (sock < 0) return NULL;
	return onyx_nstls_start(sock, host);
}

int onyx_nstls_send(onyx_tls_sess *s, const void *buf, int len)
{
	if (s == NULL) return -1;
	int o = 0;
	while (o < len) {
		int k = SSL_write(s->ssl, (const char *) buf + o, len - o);
		if (k <= 0) return -1;
		o += k;
	}
	return len;
}

int onyx_nstls_recv(onyx_tls_sess *s, void *buf, int len)
{
	if (s == NULL) return -1;
	if (SSL_pending(s->ssl) == 0) {
		struct pollfd p = { s->fd, POLLIN, 0 };
		if (poll(&p, 1, 0) <= 0) return 0;
	}
	int k = SSL_read(s->ssl, buf, len);
	if (k > 0) return k;
	int e = SSL_get_error(s->ssl, k);
	if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) return 0;
	return -1;
}

void onyx_nstls_close(onyx_tls_sess *s)
{
	if (s == NULL) return;
	SSL_shutdown(s->ssl);
	SSL_free(s->ssl);
	close(s->fd);
	free(s);
}

/* NS_MEMSTAT=<seconds>: the heap in use (glibc's mallinfo2) printed on stderr every so many
 * seconds, and its peak -- the bench's view of how much memory a page takes */
#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
static void *memstat_run(void *arg)
{
	unsigned s = (unsigned) (unsigned long) arg;
	size_t peak = 0;
	for (;;) {
		struct mallinfo2 m;
		sleep(s);
		m = mallinfo2();
		if (m.uordblks + m.hblkhd > peak) peak = m.uordblks + m.hblkhd;
		fprintf(stderr, "ONYX-MEM in use %zu KB (mmap %zu KB), peak %zu KB, heap %zu KB\n",
				(m.uordblks + m.hblkhd) / 1024, m.hblkhd / 1024, peak / 1024, m.arena / 1024);
	}
	return NULL;
}
__attribute__((constructor)) static void memstat_start(void)
{
	const char *e = getenv("NS_MEMSTAT");
	pthread_t t;
	if (e != NULL && atoi(e) > 0)
		pthread_create(&t, NULL, memstat_run, (void *) (unsigned long) atoi(e));
}

/* NS_PROF=<file>: a sampling profiler (no perf here) -- every 1 ms of CPU the native stack
 * (backtrace) is kept; at exit each sample's addresses, as offsets in the executable, go to
 * <file>, one line per sample, for tools/tests/netsurf/prof.sh (addr2line) to name */
#include <execinfo.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#define PROF_MAX 200000
#define PROF_DEPTH 24
static void *prof_buf[PROF_MAX][PROF_DEPTH];
static unsigned char prof_n[PROF_MAX];
static volatile int prof_count;
static char prof_path[512];
static unsigned long prof_base;	/* the executable's load address */	/* (NS_PROF, kept: the environment may be gone at exit) */
static void prof_sig(int sig)
{
	int i = prof_count;
	(void) sig;
	if (i >= PROF_MAX) return;
	prof_n[i] = (unsigned char) backtrace(prof_buf[i], PROF_DEPTH);
	prof_count = i + 1;
}
static void prof_write(void)
{
	FILE *f = fopen(prof_path, "w");
	unsigned long base = prof_base;
	int i, k;
	fprintf(stderr, "prof: %d samples to %s (%s)\n", prof_count, prof_path, f ? "ok" : "cannot write");
	if (f == NULL) return;
	for (i = 0; i < prof_count && i < PROF_MAX; i++) {
		for (k = 2; k < prof_n[i]; k++)	/* (the handler and the signal frame skipped) */
			fprintf(f, "%lx ", (unsigned long) prof_buf[i][k] - base);
		fputc('\n', f);
	}
	fclose(f);
}
void onyx_host_exit_hook(void)	/* (fakekapi.cpp: a SIM exit -- atexit comes too late) */
{
	struct itimerval off = { { 0, 0 }, { 0, 0 } };
	if (prof_path[0] == 0) return;
	setitimer(ITIMER_PROF, &off, NULL);
	prof_write();
	prof_path[0] = 0;
}
static void prof_term(int sig)
{
	struct itimerval off = { { 0, 0 }, { 0, 0 } };
	(void) sig;
	setitimer(ITIMER_PROF, &off, NULL);
	prof_write();
	_exit(143);
}
__attribute__((constructor)) static void prof_start(void)
{
	struct itimerval it = { { 0, 1000 }, { 0, 1000 } };
	void *warm[2];
	if (getenv("NS_PROF") == NULL) return;
	snprintf(prof_path, sizeof prof_path, "%s", getenv("NS_PROF"));
	{
		FILE *m = fopen("/proc/self/maps", "r");	/* its first mapping */
		char line[512];
		if (m != NULL) {
			if (fgets(line, sizeof line, m) != NULL)
				prof_base = strtoul(line, NULL, 16);
			fclose(m);
		}
	}
	struct sigaction sa;
	backtrace(warm, 2);	/* (libgcc loaded now, not in the handler) */
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = prof_sig;
	sa.sa_flags = SA_RESTART;	/* (the loop's poll and reads not cut short) */
	sigaction(SIGPROF, &sa, NULL);
	sa.sa_handler = prof_term;	/* (a run stopped by timeout writes its samples too) */
	sa.sa_flags = 0;
	sigaction(SIGTERM, &sa, NULL);
	setitimer(ITIMER_PROF, &it, NULL);
	atexit(onyx_host_exit_hook);
}
