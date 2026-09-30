/*
 * onyx_nstls.h -- a tiny C-callable TLS transport for NetSurf's Onyx http fetcher.
 *
 * onyx_fetch.c is C; the proven TLS transport (user/tls/onyx_tls.hpp, mbedTLS over the
 * Onyx TCP kapis -- the same one httpc / HttpClient use) is C++. This wraps it in four
 * extern "C" calls so the fetcher can speak https:// without pulling C++ into onyx_fetch.c.
 *
 * recv() keeps kapi_tcp_recv's convention so the fetcher's read loop is identical for
 * http and https:  >0 = bytes, 0 = nothing yet (poll again), <0 = closed / error.
 */
#ifndef ONYX_NSTLS_H
#define ONYX_NSTLS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct onyx_tls_sess onyx_tls_sess;

/* Onyx: the certificate check. ONYX_TLS_VERIFY: the server's chain against the trusted roots
 * (onyx_nstls_ca_bundle), its name (SNI + the certificate's SAN / CN), the validity dates
 * against the clock (skipped while the clock is unset: a year < 2025); ONYX_TLS_INSECURE:
 * checked, but the connection goes on when it fails (the user accepted the certificate);
 * ONYX_TLS_H2: HTTP/2 offered by ALPN ("h2", then "http/1.1"). */
#define ONYX_TLS_VERIFY		1
#define ONYX_TLS_INSECURE	2
#define ONYX_TLS_H2		4

/* What is wrong with a certificate: the values of NetSurf's ssl_cert_err. */
enum onyx_tls_certerr {
	ONYX_CERT_OK = 0, ONYX_CERT_UNKNOWN, ONYX_CERT_BAD_ISSUER, ONYX_CERT_BAD_SIG,
	ONYX_CERT_TOO_YOUNG, ONYX_CERT_TOO_OLD, ONYX_CERT_SELF_SIGNED, ONYX_CERT_CHAIN_SELF_SIGNED,
	ONYX_CERT_REVOKED, ONYX_CERT_HOSTNAME_MISMATCH
};

#define ONYX_TLS_CHAIN_MAX 8

/* The check's result: failed, and the chain the check built -- [0] the server's certificate,
 * the root last -- each with its DER (malloc'd: onyx_nstls_chain_free) and its fault. */
struct onyx_tls_chain {
	int failed;
	unsigned depth;
	struct {
		int err;		/* enum onyx_tls_certerr */
		unsigned char *der;
		unsigned long len;
	} cert[ONYX_TLS_CHAIN_MAX];
};

/* The trusted roots: a PEM bundle's path (SD:/res/ca-bundle), read now, parsed at the first
 * check. Call it once, on the app's main thread, before any ONYX_TLS_VERIFY connection. */
void onyx_nstls_ca_bundle(const char *path);

/* The handshake over a socket already connected (the socket is the session's from then on,
 * closed with it or here on a failure) -- flags ONYX_TLS_*. NULL on a failure: then
 * chain->failed says whether the certificate was refused (chain may be NULL). */
onyx_tls_sess *onyx_nstls_connect(int sock, const char *host, unsigned flags,
		struct onyx_tls_chain *chain);

/* Onyx: a flag another thread sets to stop the handshakes and writes in progress (the app's
 * end: their sockets given back at once) */
void onyx_nstls_cancel_flag(volatile int *flag);

/* Onyx: whether the handshake resumed a session (no certificate exchanged: one round trip
 * less) -- the perf log's "resumed" */
int onyx_nstls_resumed(onyx_tls_sess *s);

/* Onyx: the TLS sessions kept across launches -- read at the start (the next connections to
 * those hosts resume), written at the end (and after a page's load): a file on the card */
void onyx_nstls_sessions_load(const char *path);
void onyx_nstls_sessions_save(const char *path);

/* The chain's DER copies freed. */
void onyx_nstls_chain_free(struct onyx_tls_chain *chain);

/* The protocol ALPN chose ("h2", "http/1.1"), or NULL. */
const char *onyx_nstls_alpn(onyx_tls_sess *s);

/* Onyx: the TLS version negotiated ("TLSv1.3", "TLSv1.2"), for the perf log */
const char *onyx_nstls_version(onyx_tls_sess *s);

/* TCP connect to host:port, then run the TLS handshake (the certificate checked). NULL on any
 * failure. */
onyx_tls_sess *onyx_nstls_open(const char *host, unsigned port);

/* The same over a socket already connected (the fetcher connects itself: one connect at a
 * time); the socket is the session's from then on (closed with it, or here on a failure).
 * Onyx: the certificate checked (onyx_nstls_connect's ONYX_TLS_VERIFY). */
onyx_tls_sess *onyx_nstls_start(int sock, const char *host);

/* Write all `len` bytes (encrypted). Returns len, or <0 on error. */
int onyx_nstls_send(onyx_tls_sess *s, const void *buf, int len);

/* Read decrypted bytes: >0 bytes, 0 = nothing yet (poll), <0 = closed / error. */
int onyx_nstls_recv(onyx_tls_sess *s, void *buf, int len);

/* close_notify + free the session and its socket. */
void onyx_nstls_close(onyx_tls_sess *s);

#ifdef __cplusplus
}
#endif

#endif /* ONYX_NSTLS_H */
