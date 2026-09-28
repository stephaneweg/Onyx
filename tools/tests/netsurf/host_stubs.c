/*
 * host_stubs.c -- what the PC build of NetSurf (host.mk) does without: https (the Pi build's
 * onyx_nstls.cpp wraps mbedTLS; the test pages are local files).
 */
#include <stddef.h>
#include "onyx_nstls.h"

onyx_tls_sess *onyx_nstls_open(const char *host, unsigned port) { (void) host; (void) port; return NULL; }
int onyx_nstls_send(onyx_tls_sess *s, const void *buf, int len) { (void) s; (void) buf; (void) len; return -1; }
int onyx_nstls_recv(onyx_tls_sess *s, void *buf, int len) { (void) s; (void) buf; (void) len; return -1; }
void onyx_nstls_close(onyx_tls_sess *s) { (void) s; }
