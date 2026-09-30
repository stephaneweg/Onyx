/*
 * onyx_nstls.cpp -- C++ glue exposing user/tls/onyx_tls.hpp to onyx_fetch.c (C).
 *
 * No STL / exceptions / RTTI and no global constructors: the session is a malloc'd POD
 * holding mbedTLS C contexts, initialised at run time by onyx_tls::start(). So the object
 * links cleanly next to the C fetcher (compiled by g++, linked with g++ + mbedTLS).
 */
#include <stdlib.h>
#include <string.h>

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

extern "C" onyx_tls_sess *onyx_nstls_start(int sock, const char *host)
{
	onyx_tls_sess *h = (onyx_tls_sess *) malloc(sizeof *h);
	if (h == 0) {
		kapi_tcp_close(sock);
		return 0;
	}

	if (onyx_tls::start(h->s, sock, host) != 0) {
		onyx_tls::stop(h->s);
		kapi_tcp_close(sock);
		free(h);
		return 0;
	}
	mbedtls_ssl_set_bio(&h->s.ssl, &h->s, onyx_tls::bio_send, onyx_nb_recv, 0);	/* (Onyx) */
	return h;
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
