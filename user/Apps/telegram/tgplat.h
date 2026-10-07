//
// tgplat.h -- what the Telegram app's core (tl.h, tgcrypto.h, mtproto.h, client.h) asks of Onyx: the
// clock (UTC, kapi_clock_info), the log (a ring kept in memory, written to the app's folder: log.txt),
// the entropy (kapi_random, the CPU counter), the seed file, gzip's inflate (FileKit), the files
// (the session, the cache), and the TCP transport: MTProto's "intermediate" framing (0xeeeeeeee, then
// every packet its length on 4 bytes) over the WLAN sockets (kapi_tcp_*), to a data centre's port 443.
//
// MIT licence.
//
#ifndef TG_PLAT_H
#define TG_PLAT_H

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "appkit/appkit.h"
#include "filekit/filekit.h"
#include "mtproto.h"

#define TG_DIR		"SD:/apps/telegram.app/"

// ---- the clock -------------------------------------------------------------------------------------

long long tg_unix_ms ()
{
	struct kapi_clock_info ci;
	if (kapi_clock_info (&ci) == 0 && ci.freq)
	{
#ifdef __aarch64__
		unsigned long c;
		__asm__ volatile ("isb\n\tmrs %0, cntpct_el0" : "=r" (c));
		long long dc = (long long) (c - ci.cnt);
		return ci.utc_us / 1000 + dc / (long long) (ci.freq / 1000);
#else
		return ci.utc_us / 1000 + (long long) kapi_get_ticks () * 10;
#endif
	}
	return (long long) kapi_get_ticks () * 10;	// (no clock: the server's time corrects us)
}

unsigned long long tg_platform_counter ()
{
#ifdef __aarch64__
	unsigned long c;
	__asm__ volatile ("isb\n\tmrs %0, cntpct_el0" : "=r" (c));
	return c;
#else
	return (unsigned long long) kapi_clock_us ();
#endif
}

int tg_platform_entropy (unsigned char *buf, int n)
{
	int got = kapi_random (buf, (unsigned) n);
	unsigned long long c = tg_platform_counter ();
	for (int i = 0; i < n && i < 8; i++) buf[i] ^= (unsigned char) (c >> (8 * i));
	return got > 0 ? got : n;
}

// ---- files -----------------------------------------------------------------------------------------

// A whole file -> malloc'd bytes (NUL-terminated), *n its size; 0 if there is none.
static unsigned char *tg_load (const char *path, int *n)
{
	void *h = kapi_open (path);
	if (!h) return 0;
	unsigned sz = kapi_fsize (h);
	if (sz > 64u << 20) { kapi_close (h); return 0; }
	unsigned char *b = (unsigned char *) malloc (sz + 1);
	if (!b) { kapi_close (h); return 0; }
	unsigned got = 0;
	while (got < sz)
	{
		int r = kapi_read (h, b + got, sz - got);
		if (r <= 0) break;
		got += (unsigned) r;
	}
	kapi_close (h);
	b[got] = 0;
	*n = (int) got;
	return b;
}
static bool tg_save (const char *path, const void *d, int n) { return kapi_save_file (path, d, (unsigned) n) == n; }

bool tg_seed_load (unsigned char *buf, int n)
{
	int m = 0;
	unsigned char *b = tg_load (TG_DIR "seed.bin", &m);
	if (!b) return false;
	bool ok = m >= n;
	if (ok) memcpy (buf, b, (size_t) n);
	free (b);
	return ok;
}
void tg_seed_save (const unsigned char *buf, int n) { tg_save (TG_DIR "seed.bin", buf, n); }

// ---- inflate ---------------------------------------------------------------------------------------

bool tg_gunzip (const unsigned char *in, int n, unsigned char **out, int *outn)
{
	void *o = 0; unsigned on = 0;
	if (fk_inflate (in, (unsigned) n, FK_GZIP, (unsigned) n * 4, &o, &on) != 0 || !o) return false;
	*out = (unsigned char *) malloc (on + 1);		// (the caller frees with free: FileKit's own is fk_free)
	if (*out) { memcpy (*out, o, on); *outn = (int) on; }
	fk_free (o);
	return *out != 0;
}

// ---- the log ---------------------------------------------------------------------------------------

struct TgLog { char buf[32768]; int n; bool dirty; unsigned saved; };
static TgLog &tglog () { static TgLog l; return l; }

void tg_log (const char *fmt, ...)
{
	char line[300];
	int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
	kapi_get_datetime (&y, &mo, &d, &h, &mi, &s);
	int o = snprintf (line, sizeof line, "%02d:%02d:%02d ", h, mi, s);
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (line + o, sizeof line - (size_t) o - 1, fmt, ap);
	va_end (ap);
	int n = (int) strlen (line);
	line[n++] = '\n';
	TgLog &L = tglog ();
	if (L.n + n > (int) sizeof L.buf)		// (the older half dropped)
	{
		int keep = (int) sizeof L.buf / 2;
		memmove (L.buf, L.buf + L.n - keep, (size_t) keep);
		L.n = keep;
	}
	memcpy (L.buf + L.n, line, (size_t) n);
	L.n += n;
	L.dirty = true;
}

// (from the app's tick: the log written once in a while)
static void tg_log_flush (bool now)
{
	TgLog &L = tglog ();
	unsigned t = kapi_get_ticks ();
	if (!L.dirty || (!now && t - L.saved < 500)) return;
	tg_save (TG_DIR "log.txt", L.buf, L.n);
	L.dirty = false;
	L.saved = t;
}

// ---- TCP: the intermediate transport ---------------------------------------------------------------

class TcpTransport : public mt::Transport
{
public:
	TcpTransport () : m_s (-1), m_in (0), m_n (0), m_cap (0) {}
	~TcpTransport () { close (); free (m_in); }
	bool open (const char *host, int port) override
	{
		m_s = kapi_tcp_connect (host, (unsigned) port);
		if (m_s < 0) return false;
		static const unsigned char tag[4] = { 0xee, 0xee, 0xee, 0xee };
		return kapi_tcp_send (m_s, tag, 4) == 4;
	}
	bool send (const unsigned char *p, int n) override
	{
		if (m_s < 0) return false;
		unsigned char h[4] = { (unsigned char) n, (unsigned char) (n >> 8), (unsigned char) (n >> 16), (unsigned char) (n >> 24) };
		if (!all (h, 4)) return false;
		return all (p, n);
	}
	int recv (tl::Buf &out) override
	{
		if (m_s < 0) return -1;
		for (;;)
		{
			if (m_n >= 4)
			{
				int len = (int) ((unsigned) m_in[0] | ((unsigned) m_in[1] << 8) | ((unsigned) m_in[2] << 16) | ((unsigned) m_in[3] << 24));
				if (len < 0 || len > (16 << 20)) return -1;
				if (m_n >= 4 + len)
				{
					out.put (m_in + 4, len);
					memmove (m_in, m_in + 4 + len, (size_t) (m_n - 4 - len));
					m_n -= 4 + len;
					return 1;
				}
				if (m_cap < 4 + len && !grow (4 + len)) return -1;
			}
			if (m_cap - m_n < 4096 && !grow (m_n + 65536)) return -1;
			int r = kapi_tcp_recv (m_s, m_in + m_n, (unsigned) (m_cap - m_n));
			if (r < 0) return -1;
			if (r == 0) return 0;
			m_n += r;
		}
	}
	void close () override { if (m_s >= 0) kapi_tcp_close (m_s); m_s = -1; m_n = 0; }
private:
	int m_s;
	unsigned char *m_in;
	int m_n, m_cap;
	bool grow (int c)
	{
		if (c <= m_cap) return true;
		unsigned char *x = (unsigned char *) realloc (m_in, (size_t) c);
		if (!x) return false;
		m_in = x; m_cap = c;
		return true;
	}
	bool all (const unsigned char *p, int n)
	{
		while (n > 0)
		{
			int r = kapi_tcp_send (m_s, p, (unsigned) n);
			if (r <= 0) return false;
			p += r; n -= r;
		}
		return true;
	}
};

static mt::Transport *tg_new_transport () { return new TcpTransport; }

#endif
