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
