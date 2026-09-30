/*
 * host_stubs.c -- https for the PC build of NetSurf (host.mk): the Pi build's onyx_nstls.cpp
 * wraps mbedTLS over the Onyx TCP kapis; here the PC's OpenSSL over the simulator's real
 * sockets (SIM_REALNET=1: a socket handle is the file descriptor + 1000), the certificates
 * checked against the bench's bundle as the Pi checks them (onyx_nstls_connect: the chain and
 * each certificate's fault for NetSurf's certificate error page; NS_TLS_NOVERIFY=1: no check),
 * ALPN h2 offered when the fetcher asks. NS_MBEDTLS=1: the Pi's mbedTLS glue instead. Without
 * SIM_REALNET there is no network: every call fails.
 */
#include <stddef.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include "onyx_nstls.h"
#include "kapi.h"

/* Onyx: NS_MBEDTLS=1 -- the Pi's own TLS instead (user/netsurf/onyx_nstls.cpp: mbedTLS over the
 * simulator's sockets, compiled here with its calls renamed onyx_mb_*): its certificate check,
 * ALPN, reads, exercised on the PC */
onyx_tls_sess *onyx_mb_connect(int sock, const char *host, unsigned flags, struct onyx_tls_chain *chain);
void onyx_mb_ca_bundle(const char *path);
int onyx_mb_send(onyx_tls_sess *s, const void *buf, int len);
int onyx_mb_recv(onyx_tls_sess *s, void *buf, int len);
void onyx_mb_close(onyx_tls_sess *s);
const char *onyx_mb_alpn(onyx_tls_sess *s);

struct onyx_tls_sess { SSL *ssl; int fd; onyx_tls_sess *mb; struct onyx_tls_chain *rec; };

static int use_mb(void)
{
	static int v = -1;
	if (v < 0) v = getenv("NS_MBEDTLS") != NULL && atoi(getenv("NS_MBEDTLS")) > 0;
	return v;
}

static char ca_path[1024];

/* Onyx: the trusted roots -- the bench's bundle ($(OUT)/res/ca-bundle: the card's, with the
 * proxy's CA appended when there is one: host.mk), else the system's */
void onyx_nstls_ca_bundle(const char *path)
{
	snprintf(ca_path, sizeof ca_path, "%s", path);
	onyx_mb_ca_bundle(path);
}

/* OpenSSL's verdict on each certificate of the chain it built (the root first): recorded as
 * the Pi's check records it, the handshake let through -- judged after it */
static int verify_cb(int ok, X509_STORE_CTX *x)
{
	SSL *ssl = X509_STORE_CTX_get_ex_data(x, SSL_get_ex_data_X509_STORE_CTX_idx());
	onyx_tls_sess *s = ssl != NULL ? SSL_get_app_data(ssl) : NULL;
	int depth = X509_STORE_CTX_get_error_depth(x), e = X509_STORE_CTX_get_error(x), err;
	X509 *c = X509_STORE_CTX_get_current_cert(x);

	if (s == NULL || s->rec == NULL || depth < 0 || depth >= ONYX_TLS_CHAIN_MAX)
		return 1;
	switch (ok ? X509_V_OK : e) {
	case X509_V_OK: err = ONYX_CERT_OK; break;
	case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
	case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
	case X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY:
	case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE: err = ONYX_CERT_BAD_ISSUER; break;
	case X509_V_ERR_CERT_SIGNATURE_FAILURE: err = ONYX_CERT_BAD_SIG; break;
	case X509_V_ERR_CERT_NOT_YET_VALID: err = ONYX_CERT_TOO_YOUNG; break;
	case X509_V_ERR_CERT_HAS_EXPIRED: err = ONYX_CERT_TOO_OLD; break;
	case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT: err = ONYX_CERT_SELF_SIGNED; break;
	case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN: err = ONYX_CERT_CHAIN_SELF_SIGNED; break;
	case X509_V_ERR_CERT_REVOKED: err = ONYX_CERT_REVOKED; break;
	case X509_V_ERR_HOSTNAME_MISMATCH: err = ONYX_CERT_HOSTNAME_MISMATCH; break;
	default: err = ONYX_CERT_UNKNOWN; break;
	}
	if (s->rec->cert[depth].der == NULL && c != NULL) {
		unsigned char *d = NULL;
		int n = i2d_X509(c, &d);
		if (n > 0) {
			s->rec->cert[depth].der = malloc((size_t) n);
			if (s->rec->cert[depth].der != NULL) memcpy(s->rec->cert[depth].der, d, (size_t) n);
			s->rec->cert[depth].len = s->rec->cert[depth].der != NULL ? (unsigned long) n : 0;
			OPENSSL_free(d);
		}
	}
	if (s->rec->cert[depth].err == ONYX_CERT_OK)
		s->rec->cert[depth].err = err;
	if ((unsigned) depth + 1 > s->rec->depth)
		s->rec->depth = (unsigned) depth + 1;
	if (err != ONYX_CERT_OK)
		s->rec->failed = 1;
	return 1;
}

static SSL_CTX *ctx(void)
{
	static SSL_CTX *c;
	if (c == NULL) {
		c = SSL_CTX_new(TLS_client_method());
		if (ca_path[0] == '\0' || SSL_CTX_load_verify_locations(c, ca_path, NULL) != 1)
			SSL_CTX_set_default_verify_paths(c);
		SSL_CTX_set_verify(c, getenv("NS_TLS_NOVERIFY") ? SSL_VERIFY_NONE : SSL_VERIFY_PEER,
				verify_cb);
	}
	return c;
}

onyx_tls_sess *onyx_nstls_connect(int sock, const char *host, unsigned flags,
		struct onyx_tls_chain *chain)
{
	struct onyx_tls_chain rec;
	onyx_tls_sess *s;

	if (chain != NULL) memset(chain, 0, sizeof *chain);
	if (sock < 1000) return NULL;
	if (use_mb()) {
		onyx_tls_sess *m = onyx_mb_connect(sock, host, flags, chain);
		if (m == NULL) return NULL;
		s = calloc(1, sizeof *s);
		s->mb = m;
		return s;
	}
	memset(&rec, 0, sizeof rec);
	s = calloc(1, sizeof *s);
	s->fd = sock - 1000;
	s->ssl = SSL_new(ctx());
	s->rec = &rec;
	SSL_set_app_data(s->ssl, s);
	SSL_set_fd(s->ssl, s->fd);
	SSL_set_tlsext_host_name(s->ssl, host);
	SSL_set1_host(s->ssl, host);
	if (flags & ONYX_TLS_H2)
		SSL_set_alpn_protos(s->ssl, (const unsigned char *) "\x02h2\x08http/1.1", 12);
	else
		SSL_set_alpn_protos(s->ssl, (const unsigned char *) "\x08http/1.1", 9);
	if (SSL_connect(s->ssl) != 1)
		goto fail;
	if (!(flags & ONYX_TLS_VERIFY) || (flags & ONYX_TLS_INSECURE) || getenv("NS_TLS_NOVERIFY"))
		rec.failed = 0;
	else if (SSL_get_verify_result(s->ssl) != X509_V_OK)
		rec.failed = 1;
	if (rec.failed)
		goto fail;
	s->rec = NULL;
	onyx_nstls_chain_free(&rec);
	return s;
fail:
	if (chain != NULL && rec.failed)
		*chain = rec;		/* (the chain's DER copies are the caller's) */
	else
		onyx_nstls_chain_free(&rec);
	SSL_free(s->ssl); close(s->fd); free(s);
	return NULL;
}

void onyx_nstls_chain_free(struct onyx_tls_chain *chain)
{
	unsigned i;
	if (chain == NULL) return;
	for (i = 0; i < ONYX_TLS_CHAIN_MAX; i++) {
		free(chain->cert[i].der);
		chain->cert[i].der = NULL;
	}
	chain->depth = 0;
}

const char *onyx_nstls_alpn(onyx_tls_sess *s)
{
	static const char h2[] = "h2", h1[] = "http/1.1";
	const unsigned char *p = NULL;
	unsigned n = 0;
	if (s == NULL) return NULL;
	if (s->mb != NULL) return onyx_mb_alpn(s->mb);
	SSL_get0_alpn_selected(s->ssl, &p, &n);
	if (n == 2 && memcmp(p, "h2", 2) == 0) return h2;
	if (n == 8 && memcmp(p, "http/1.1", 8) == 0) return h1;
	return NULL;
}

onyx_tls_sess *onyx_nstls_start(int sock, const char *host)
{
	return onyx_nstls_connect(sock, host, ONYX_TLS_VERIFY, NULL);
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
	if (s->mb != NULL) return onyx_mb_send(s->mb, buf, len);
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
	if (s->mb != NULL) return onyx_mb_recv(s->mb, buf, len);
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
	if (s->mb != NULL) {
		onyx_mb_close(s->mb);
		free(s);
		return;
	}
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
