//
// printerkit/ippnet.h -- printerkit/ipp.h's sockets on Onyx: the kapi's TCP. A send goes on until everything is
// queued (kapi_tcp_send returns a short count after 5 s: a printer that prints while it receives is slow
// to take the next rows) and gives up after a minute without a byte taken.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (the notice: printerkit/printerkit.h).
//
#ifndef ONYX_PRINT_IPPNET_H
#define ONYX_PRINT_IPPNET_H

#include "kapi.h"
#include "printerkit/ipp.h"

namespace ipp {

static void (*net_idle) (void);		// called while a send or a receive waits (the daemon answers its requests)
static bool (*net_stop) (void);		// true: give up (the job was cancelled)

static int k_connect (const char *host, int port)
{
	if (!kapi_net_status (0, 0)) return -1;
	return kapi_tcp_connect (host, (unsigned) port);
}
static bool k_send (int s, const void *b, unsigned n)
{
	const char *p = (const char *) b; unsigned stalled = 0;
	while (n)
	{
		if (net_stop && net_stop ()) return false;
		int k = kapi_tcp_send (s, p, n > 32768 ? 32768 : n);
		if (k < 0) return false;
		if (k == 0) { if (++stalled > 12) return false; }
		else stalled = 0;
		p += k; n -= (unsigned) k;
		if (net_idle) net_idle ();
	}
	return true;
}
static int k_recv (int s, void *b, unsigned n, unsigned wait_ms)
{
	for (unsigned t = 0; ; t += 10)
	{
		int k = kapi_tcp_recv (s, b, n);
		if (k != 0) return k;
		if (t >= wait_ms) return 0;
		kapi_msleep (10);
	}
}
static void k_close (int s) { kapi_tcp_close (s); }
static const Net KAPI_NET = { k_connect, k_send, k_recv, k_close };

} // namespace ipp

#endif
