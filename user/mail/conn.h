//
// mail/conn.h -- a mail server's connection: TCP over the kapi sockets, TLS from the start (993, 995, 465) or after
// STARTTLS (143, 110, 587) through mbedTLS (tls/onyx_tls.hpp: the certificate checked against SD:/res/ca-bundle unless
// the account says not to), read line by line (CRLF) or by an exact count (IMAP's literals), with a time-out and a
// cancel flag. Blocking: used from Mail's worker thread. Part of Onyx's mail (docs/mail/README.md).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See mail/util.h for the full notice.
//
#ifndef ONYX_MAIL_CONN_H
#define ONYX_MAIL_CONN_H

#include "kapi.h"
#include "mail/util.h"
#include "tls/onyx_tls.hpp"

namespace mail {

enum Security { SEC_TLS = 0, SEC_STARTTLS = 1, SEC_NONE = 2 };

// the trusted roots, once (SD:/res/ca-bundle: the bundle Jet Browser uses)
static void ca_load ()
{
	static bool done;
	if (done) return;
	done = true;
	void *f = kapi_open ("SD:/res/ca-bundle");
	if (!f) return;
	unsigned n = kapi_fsize (f);
	unsigned char *b = (unsigned char *) malloc (n + 1);
	int r = b ? kapi_read (f, b, n) : -1;
	kapi_close (f);
	if (r != (int) n) { free (b); return; }
	b[n] = 0;
	onyx_tls::set_ca_bundle (b, n + 1);
}

struct Conn
{
	int sock; bool tls, open_; onyx_tls::Session s;
	char host[128];
	char in[16384]; int rp, wp;
	volatile int *cancel;				// set by the window: give up at once
	unsigned timeoutMs;
	bool verify, insecureOk;			// the certificate checked; refused -> go on anyway (the user said so)
	bool softTimeout, timedOut;			// (IDLE: the time-out only says so, the connection kept)
	char err[240];
	void (*trace) (void *, bool out, const char *line, int n); void *traceCtx;	// (the account's log: what went, what came)

	Conn () : sock (-1), tls (false), open_ (false), rp (0), wp (0), cancel (0), timeoutMs (30000), verify (true), insecureOk (false), softTimeout (false), timedOut (false), trace (0), traceCtx (0)
	{ host[0] = err[0] = 0; memset (&s, 0, sizeof s); }
	~Conn () { close (); }

	bool fail (const char *fmt, ...)
	{
		va_list a; va_start (a, fmt); vsnprintf (err, sizeof err, fmt, a); va_end (a);
		return false;
	}
	bool cancelled () const { return cancel && *cancel; }

	bool tls_start ()
	{
		ca_load ();
		s.cancel = cancel;
		onyx_tls::Verify vr; memset (&vr, 0, sizeof vr);
		unsigned opts = verify ? onyx_tls::START_VERIFY | (insecureOk ? onyx_tls::START_INSECURE : 0) : 0;
		int r = onyx_tls::start (s, sock, host, opts, verify ? &vr : 0);
		if (verify) onyx_tls::verify_free (&vr);
		if (r == -2) return fail ("The server's certificate is not trusted (%s): it may not be the real one.", host);
		if (r != 0) return fail ("The secure connection to %s failed.", host);
		tls = true;
		return true;
	}
	// open: TLS at once (sec SEC_TLS) or plain (STARTTLS comes later: starttls ())
	bool open (const char *h, int port, int sec)
	{
		close ();
		scpy (host, h, sizeof host); rp = wp = 0; err[0] = 0;
		char ip[32];
		if (kapi_net_status (ip, sizeof ip) <= 0 && !getenv_sim ()) return fail ("The network is not up (Wi-Fi?).");
		sock = kapi_tcp_connect (h, (unsigned) port);
		if (sock < 0) return fail (sock == -4 ? "The server %s was not found." : "The server %s did not answer (port %d).", h, port);
		open_ = true;
		if (sec == SEC_TLS && !tls_start ()) { close (); return false; }
		return true;
	}
	static bool getenv_sim () { return getenv ("SIM_REALNET") != 0; }
	// after the server said yes to STARTTLS: what was read before is dropped (RFC 3207)
	bool starttls () { rp = wp = 0; return tls_start (); }
	void close ()
	{
		if (!open_) return;
		if (tls) onyx_tls::stop (s);
		kapi_tcp_close (sock);
		tls = false; open_ = false; sock = -1; rp = wp = 0;
	}

	bool send (const void *b, int n)
	{
		if (!open_) return fail ("Not connected.");
		if (trace) trace (traceCtx, true, (const char *) b, n);
		int r = tls ? onyx_tls::send (s, b, n) : kapi_tcp_send (sock, b, (unsigned) n);
		if (r != n) { close (); return fail ("The connection to %s was lost.", host); }
		return true;
	}
	bool send (const char *str) { return send (str, (int) strlen (str)); }
	bool sendf (const char *fmt, ...)
	{
		char t[2048]; va_list a; va_start (a, fmt); int k = vsnprintf (t, sizeof t, fmt, a); va_end (a);
		if (k >= (int) sizeof t) k = sizeof t - 1;
		return send (t, k);
	}
	// more bytes into in[]: false on the connection's end, the time-out or a cancel
	bool fill ()
	{
		if (rp > 0 && rp == wp) rp = wp = 0;
		if (wp == (int) sizeof in) { memmove (in, in + rp, wp - rp); wp -= rp; rp = 0; }
		unsigned t0 = kapi_clock_us (); timedOut = false;
		for (;;)
		{
			if (cancelled ()) { close (); return fail ("Cancelled."); }
			int r = tls ? onyx_tls::recv (s, in + wp, (int) sizeof in - wp) : kapi_tcp_recv (sock, in + wp, (unsigned) sizeof in - wp);
			if (r > 0) { wp += r; return true; }
			if (r < 0) { close (); return fail ("%s closed the connection.", host); }
			if ((kapi_clock_us () - t0) / 1000 > timeoutMs)
			{
				timedOut = true;
				if (softTimeout) return false;
				close (); return fail ("%s does not answer any more.", host);
			}
			kapi_msleep (4);
		}
	}
	// a line without its CRLF into o (cleared first): false when the connection ends (a time-out loses nothing: what
	// came of the line stays in in[] until its end comes)
	bool line (Buf &o)
	{
		o.clear ();
		int scan = rp;
		for (;;)
		{
			for (int i = scan; i < wp; i++)
				if (in[i] == '\n')
				{
					o.add (in + rp, i - rp); rp = i + 1;
					if (o.n && o.p[o.n - 1] == '\r') o.p[--o.n] = 0;
					if (trace) trace (traceCtx, false, o.c (), o.n);
					return true;
				}
			if (rp == 0 && wp == (int) sizeof in) { o.add (in, wp); rp = wp = 0; }	// (a long line: kept aside)
			scan = wp;
			if (o.n > (8 << 20)) { close (); return fail ("A line too long from %s.", host); }
			if (!open_) return false;
			int had = rp; if (!fill ()) return false;
			scan -= had - rp;					// (fill () may have moved in[])
		}
	}
	// exactly n bytes appended to o
	bool bytes (Buf &o, long n)
	{
		if (!o.reserve ((int) n)) { close (); return fail ("Not enough memory (%ld bytes).", n); }
		while (n > 0)
		{
			if (rp == wp && (!open_ || !fill ())) return false;
			int k = wp - rp < n ? wp - rp : (int) n;
			o.add (in + rp, k); rp += k; n -= k;
		}
		return true;
	}
};

} // namespace mail

#endif
