/*
 * onyx_nstls.cpp -- C++ glue exposing user/tls/onyx_tls.hpp to onyx_fetch.c (C).
 *
 * No STL / exceptions / RTTI and no global constructors: the session is a malloc'd POD
 * holding mbedTLS C contexts, initialised at run time by onyx_tls::start(). So the object
 * links cleanly next to the C fetcher (compiled by g++, linked with g++ + mbedTLS).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>		/* (Onyx) the trusted roots' file */

#include "kapi.h"		/* kapi_tcp_connect / kapi_tcp_close */
#include "onyx_tls.hpp"		/* user/tls: the mbedTLS-over-kapi transport */

#include "onyx_nstls.h"

struct onyx_tls_sess {
	onyx_tls::Session s;
};

/* Onyx: once the handshake is done, a read that finds nothing on the socket returns at once
 * (MBEDTLS_ERR_SSL_WANT_READ: onyx_nstls_recv's 0) -- onyx_tls.hpp's own BIO waits for the
 * data up to 20 s and then reports the connection reset: a WebSocket, an EventSource or a
 * long poll quiet for 20 s was cut, and its thread could not send while it waited. The
 * callers poll (the fetcher's in_fill, onyx_ws.c) with their own idle rules. mbedTLS keeps a
 * record read in part and goes on with it at the next call. */
static int onyx_nb_recv(void *ctx, unsigned char *buf, size_t len)
{
	onyx_tls::Session *s = (onyx_tls::Session *) ctx;

	if (s->rxpos >= s->rxlen) {
		int n = kapi_tcp_recv(s->sock, s->rxbuf, (unsigned) sizeof s->rxbuf);
		if (n < 0)
			return MBEDTLS_ERR_NET_CONN_RESET;
		if (n == 0)
			return MBEDTLS_ERR_SSL_WANT_READ;
		s->rxlen = n;
		s->rxpos = 0;
	}
	int avail = s->rxlen - s->rxpos;
	int give = (int) len < avail ? (int) len : avail;
	memcpy(buf, s->rxbuf + s->rxpos, (size_t) give);
	s->rxpos += give;
	return give;
}

/* Onyx: the trusted roots -- the bundle's bytes read here (the app's main thread: the file
 * calls), parsed by onyx_tls at the first check (a download's thread) */
extern "C" void onyx_nstls_ca_bundle(const char *path)
{
	FILE *f = fopen(path, "rb");
	unsigned char *b = 0;
	size_t n = 0, cap = 0;

	if (f == 0)
		return;
	for (;;) {
		if (n + 4096 + 1 > cap) {
			size_t ncap = cap ? cap * 2 : 262144;
			unsigned char *nb = (unsigned char *) realloc(b, ncap);
			if (nb == 0)
				break;
			b = nb;
			cap = ncap;
		}
		size_t r = fread(b + n, 1, cap - n - 1, f);
		if (r == 0)
			break;
		n += r;
	}
	fclose(f);
	if (b == 0)
		return;
	b[n] = 0;
	onyx_tls::set_ca_bundle(b, n + 1);	/* (the trust takes the buffer) */
}

/* Onyx: a certificate's fault, from mbedTLS's flags (the first that applies) */
static int onyx_cert_err(uint32_t f, int depth, bool self_signed, bool top)
{
	if (f == 0)
		return ONYX_CERT_OK;
	if (f & MBEDTLS_X509_BADCERT_EXPIRED)
		return ONYX_CERT_TOO_OLD;
	if (f & MBEDTLS_X509_BADCERT_FUTURE)
		return ONYX_CERT_TOO_YOUNG;
	if (f & MBEDTLS_X509_BADCERT_REVOKED)
		return ONYX_CERT_REVOKED;
	if (f & MBEDTLS_X509_BADCERT_NOT_TRUSTED) {
		if (self_signed)
			return depth == 0 ? ONYX_CERT_SELF_SIGNED : ONYX_CERT_CHAIN_SELF_SIGNED;
		return top ? ONYX_CERT_BAD_ISSUER : ONYX_CERT_UNKNOWN;
	}
	if (f & MBEDTLS_X509_BADCERT_CN_MISMATCH)
		return ONYX_CERT_HOSTNAME_MISMATCH;
	if (f & (MBEDTLS_X509_BADCERT_BAD_MD | MBEDTLS_X509_BADCERT_BAD_PK |
			MBEDTLS_X509_BADCERT_BAD_KEY))
		return ONYX_CERT_BAD_SIG;
	return ONYX_CERT_UNKNOWN;
}

extern "C" onyx_tls_sess *onyx_nstls_connect(int sock, const char *host, unsigned flags,
		struct onyx_tls_chain *chain)
{
	onyx_tls_sess *h = (onyx_tls_sess *) malloc(sizeof *h);
	onyx_tls::Verify *v = 0;
	unsigned opts = 0;
	int r;

	if (chain != 0)
		memset(chain, 0, sizeof *chain);
	if (h == 0) {
		kapi_tcp_close(sock);
		return 0;
	}
	if (flags & ONYX_TLS_VERIFY) {
		opts |= onyx_tls::START_VERIFY;
		v = (onyx_tls::Verify *) calloc(1, sizeof *v);
	}
	if (flags & ONYX_TLS_INSECURE)
		opts |= onyx_tls::START_INSECURE;
	opts |= (flags & ONYX_TLS_H2) ? onyx_tls::START_ALPN_H2 : onyx_tls::START_ALPN_H1;

	r = onyx_tls::start(h->s, sock, host, opts, v);
	if (r == -2 && chain != 0 && v != 0) {
		/* the chain the check built, for the certificate error page */
		int i, n = v->depth < ONYX_TLS_CHAIN_MAX ? v->depth : ONYX_TLS_CHAIN_MAX;
		chain->failed = 1;
		chain->depth = (unsigned) n;
		for (i = 0; i < n; i++) {
			chain->cert[i].err = onyx_cert_err(v->cert[i].flags, i,
					v->cert[i].self_signed, i == n - 1);
			chain->cert[i].der = v->cert[i].der;	/* (the chain takes it) */
			chain->cert[i].len = v->cert[i].len;
			v->cert[i].der = 0;
		}
	} else if (r == -2 && chain != 0) {
		chain->failed = 1;
	}
	if (v != 0) {
		onyx_tls::verify_free(v);
		free(v);
	}
	if (r != 0) {
		onyx_tls::stop(h->s);
		kapi_tcp_close(sock);
		free(h);
		return 0;
	}
	mbedtls_ssl_set_bio(&h->s.ssl, &h->s, onyx_tls::bio_send, onyx_nb_recv, 0);	/* (Onyx) */
	return h;
}

extern "C" void onyx_nstls_chain_free(struct onyx_tls_chain *chain)
{
	unsigned i;

	if (chain == 0)
		return;
	for (i = 0; i < ONYX_TLS_CHAIN_MAX; i++) {
		free(chain->cert[i].der);
		chain->cert[i].der = 0;
	}
	chain->depth = 0;
}

extern "C" const char *onyx_nstls_alpn(onyx_tls_sess *h)
{
	return h != 0 ? mbedtls_ssl_get_alpn_protocol(&h->s.ssl) : 0;
}

extern "C" onyx_tls_sess *onyx_nstls_start(int sock, const char *host)
{
	return onyx_nstls_connect(sock, host, ONYX_TLS_VERIFY, 0);
}

extern "C" onyx_tls_sess *onyx_nstls_open(const char *host, unsigned port)
{
	int sock = kapi_tcp_connect(host, port);
	if (sock < 0)
		return 0;
	return onyx_nstls_start(sock, host);
}

extern "C" int onyx_nstls_send(onyx_tls_sess *h, const void *buf, int len)
{
	return onyx_tls::send(h->s, buf, len);
}

extern "C" int onyx_nstls_recv(onyx_tls_sess *h, void *buf, int len)
{
	return onyx_tls::recv(h->s, buf, len);
}

extern "C" void onyx_nstls_close(onyx_tls_sess *h)
{
	if (h == 0)
		return;
	onyx_tls::stop(h->s);
	kapi_tcp_close(h->s.sock);
	free(h);
}
