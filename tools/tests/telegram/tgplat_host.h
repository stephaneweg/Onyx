//
// tgplat_host.h -- the Telegram app's platform on the PC, for its tests (tools/tests/telegram/): the
// clock, the log (stderr), /dev/urandom, the seed in /tmp, zlib's inflate, and an HTTP transport
// (MTProto over HTTP: each packet POSTed to http://<dc>:80/api with curl -- which goes through the
// environment's proxy, as any HTTP client here).
//
// MIT licence.
//
#ifndef TG_PLAT_HOST_H
#define TG_PLAT_HOST_H

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <zlib.h>
#include "mtproto.h"

long long tg_unix_ms () { struct timeval tv; gettimeofday (&tv, 0); return (long long) tv.tv_sec * 1000 + tv.tv_usec / 1000; }
unsigned long long tg_platform_counter () { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return (unsigned long long) t.tv_sec * 1000000000ull + (unsigned long long) t.tv_nsec; }
int tg_platform_entropy (unsigned char *buf, int n) { FILE *f = fopen ("/dev/urandom", "rb"); if (!f) return 0; int r = (int) fread (buf, 1, (size_t) n, f); fclose (f); return r; }
bool tg_seed_load (unsigned char *buf, int n) { FILE *f = fopen ("/tmp/tg_seed.bin", "rb"); if (!f) return false; bool ok = (int) fread (buf, 1, (size_t) n, f) == n; fclose (f); return ok; }
void tg_seed_save (const unsigned char *buf, int n) { FILE *f = fopen ("/tmp/tg_seed.bin", "wb"); if (f) { fwrite (buf, 1, (size_t) n, f); fclose (f); } }
static bool tg_quiet = false;
void tg_log (const char *fmt, ...)
{
	if (tg_quiet) return;
	va_list ap; va_start (ap, fmt);
	fprintf (stderr, "  [log] "); vfprintf (stderr, fmt, ap); fprintf (stderr, "\n");
	va_end (ap);
}
bool tg_gunzip (const unsigned char *in, int n, unsigned char **out, int *outn)
{
	z_stream z; memset (&z, 0, sizeof z);
	if (inflateInit2 (&z, 16 + MAX_WBITS) != Z_OK) return false;
	int cap = n * 4 + 1024, len = 0;
	unsigned char *o = (unsigned char *) malloc ((size_t) cap);
	z.next_in = (unsigned char *) in; z.avail_in = (unsigned) n;
	int r;
	do {
		if (cap - len < 4096) { cap *= 2; o = (unsigned char *) realloc (o, (size_t) cap); }
		z.next_out = o + len; z.avail_out = (unsigned) (cap - len);
		r = inflate (&z, Z_NO_FLUSH);
		len = cap - (int) z.avail_out;
	} while (r == Z_OK);
	inflateEnd (&z);
	if (r != Z_STREAM_END) { free (o); return false; }
	*out = o; *outn = len;
	return true;
}

static unsigned char *tg_load (const char *path, int *n)
{
	FILE *f = fopen (path, "rb"); if (!f) return 0;
	fseek (f, 0, SEEK_END); long sz = ftell (f); fseek (f, 0, SEEK_SET);
	unsigned char *b = (unsigned char *) malloc ((size_t) sz + 1);
	*n = (int) fread (b, 1, (size_t) sz, f); b[*n] = 0; fclose (f);
	return b;
}
static bool tg_save (const char *path, const void *d, int n) { FILE *f = fopen (path, "wb"); if (!f) return false; bool ok = (int) fwrite (d, 1, (size_t) n, f) == n; fclose (f); return ok; }

// MTProto over HTTP: a POST a packet, its answer (if any) queued.
class HttpTransport : public mt::Transport
{
public:
	HttpTransport () : m_head (0), m_tail (0), m_open (false) { m_url[0] = 0; }
	bool open (const char *host, int port) override { (void) port; snprintf (m_url, sizeof m_url, "http://%s:80/api", host); m_open = true; return true; }
	bool send (const unsigned char *p, int n) override
	{
		if (!m_open) return false;
		char in[64], out[64], cmd[400];
		snprintf (in, sizeof in, "/tmp/tg_http_in_%d", getpid ());
		snprintf (out, sizeof out, "/tmp/tg_http_out_%d", getpid ());
		if (!tg_save (in, p, n)) return false;
		unlink (out);
		snprintf (cmd, sizeof cmd, "curl -s --max-time 40 -H 'Content-Type: application/octet-stream' --data-binary @%s -o %s -w '%%{http_code}' %s", in, out, m_url);
		FILE *pp = popen (cmd, "r");
		if (!pp) return false;
		char code[16] = { 0 };
		if (!fgets (code, sizeof code, pp)) code[0] = 0;
		pclose (pp);
		int len = 0;
		unsigned char *b = tg_load (out, &len);
		if (atoi (code) != 200) { tg_log ("http: %s (%d bytes)", code, len); free (b); return atoi (code) == 404 ? push4 (-404) : false; }
		if (b && len > 0) { if (!push (b, len)) free (b); } else free (b);
		return true;
	}
	int recv (tl::Buf &o) override
	{
		if (!m_open) return -1;
		if (m_head == m_tail) return 0;
		Pk &k = m_q[m_head % 64]; m_head++;
		o.put (k.d, k.n); free (k.d);
		return 1;
	}
	void close () override { m_open = false; while (m_head != m_tail) { free (m_q[m_head % 64].d); m_head++; } }
	bool http () const override { return true; }
private:
	struct Pk { unsigned char *d; int n; };
	Pk m_q[64]; unsigned m_head, m_tail; bool m_open; char m_url[96];
	bool push (unsigned char *d, int n) { if (m_tail - m_head >= 64) return false; m_q[m_tail % 64].d = d; m_q[m_tail % 64].n = n; m_tail++; return true; }
	bool push4 (int code) { unsigned char *d = (unsigned char *) malloc (4); memcpy (d, &code, 4); return push (d, 4); }
};

// (TG_FAKE_NET: the offline tests -- every packet swallowed, nothing ever comes)
class NullTransport : public mt::Transport
{
public:
	bool open (const char *, int) override { return true; }
	bool send (const unsigned char *, int) override { return true; }
	int recv (tl::Buf &) override { return 0; }
	void close () override {}
};
#ifdef TG_FAKE_NET
static mt::Transport *tg_new_transport () { return new NullTransport; }
#else
static mt::Transport *tg_new_transport () { return new HttpTransport; }
#endif

#endif
