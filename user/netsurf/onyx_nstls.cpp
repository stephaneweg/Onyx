/*
 * onyx_nstls.cpp -- C++ glue exposing user/tls/onyx_tls.hpp to onyx_fetch.c (C).
 *
 * No STL / exceptions / RTTI and no global constructors: the session is a malloc'd POD
 * holding mbedTLS C contexts, initialised at run time by onyx_tls::start(). So the object
 * links cleanly next to the C fetcher (compiled by g++, linked with g++ + mbedTLS).
 */
#include <stdlib.h>

#include "kapi.h"		/* kapi_tcp_connect / kapi_tcp_close */
#include "onyx_tls.hpp"		/* user/tls: the mbedTLS-over-kapi transport */

#include "onyx_nstls.h"

struct onyx_tls_sess {
	onyx_tls::Session s;
};

extern "C" onyx_tls_sess *onyx_nstls_open(const char *host, unsigned port)
{
	int sock = kapi_tcp_connect(host, port);
	if (sock < 0)
		return 0;

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
	return h;
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
