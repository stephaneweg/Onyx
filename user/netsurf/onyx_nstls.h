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

/* TCP connect to host:port, then run the TLS handshake. NULL on any failure. */
onyx_tls_sess *onyx_nstls_open(const char *host, unsigned port);

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
