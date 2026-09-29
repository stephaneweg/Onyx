/*
 * onyx_fetch.c -- a NetSurf fetch scheme handler for Onyx (brick 8).
 *
 * NetSurf fetches every resource through a registered scheme handler
 * (struct fetcher_operation_table; see content/fetchers.h). The reference HTTP fetcher
 * (content/fetchers/curl.c) drives libcurl. Onyx has no libcurl; this fetcher drives the
 * Onyx TCP kapis directly (kapi_tcp_connect/send/recv/close) and inflates gzip/deflate
 * with zlib -- the same transport our HttpClient (user/http.hpp) uses, exposed to the
 * NetSurf core as the "http"/"https" scheme.
 *
 * Concurrency model (network lever A): each fetch is a small STATE MACHINE, and
 * fetch_onyx_poll() advances every active fetch by ONE non-blocking step per call,
 * so the read (download) phases of all in-flight resources OVERLAP instead of running
 * one-at-a-time. The connect + (TLS) handshake is still blocking -- but the TLS layer
 * now resumes sessions (lever B), so a same-host handshake after the first is abbreviated.
 * (A persistent keep-alive connection pool -- lever C, HTTP/1.1 + Content-Length/chunked
 * -- is the next step; for now we keep the proven HTTP/1.0 read-to-close framing.)
 *
 * Scope: http:// and https:// (the latter over user/tls/onyx_tls.hpp = mbedTLS on the TCP
 * kapis, via the C-callable onyx_nstls wrapper); GET, POST (a url-encoded / text body) and
 * any method a script asks for (the pseudo-header "X-Onyx-Method: PUT", from JS fetch /
 * XMLHttpRequest, taken off the request); the request's own headers; gzip/deflate decoding.
 * The response goes to the core with its status line and headers (the cache-control ones
 * and those the inflate makes wrong left out: the cache behaves as before). Builds as
 * part of the NetSurf core (brick 9). See user/netsurf/README.md.
 *
 * Threads (kernel v67): each download runs in a THREAD of its own (fetch_onyx_worker) --
 * the DNS, the connect, the TLS handshake, the request and every read, all blocking there
 * while the UI thread goes on. The worker only touches its struct onyx_job (plain copies:
 * the URL as a string; the response bytes); everything NetSurf -- parsing the head, the
 * redirects, the inflate, fetch_send_callback, nsurl, the fetch queues -- stays on the UI
 * thread, which picks the finished jobs up in fetch_onyx_poll (every 10 ms). An aborted
 * fetch whose worker still runs is orphaned: the worker frees its job when it ends. Without
 * threads (an older kernel, the PC bench's fake kapi, a thread refused) a fetch takes the
 * state-machine path above.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>		/* strncasecmp */
#include <stdio.h>		/* snprintf */

#include <zlib.h>
#include <libwapcaplet/libwapcaplet.h>

#include "utils/nsurl.h"
#include "utils/corestrings.h"
#include "utils/ring.h"
#include "utils/log.h"

#include "content/fetch.h"
#include "content/fetchers.h"

#include "kapi.h"		/* Onyx TCP transport */
#include "onyx_nstls.h"		/* C-callable TLS transport (https) */

/* Per-fetch state machine phases. */
enum onyx_phase {
	PH_INIT = 0,	/* parse URL, connect, send request (one-shot, blocking) */
	PH_RECV,	/* non-blocking: accumulate the response until the peer closes */
	PH_DONE		/* response complete -> deliver + clean up */
};

struct fetch_onyx_context {
	struct fetch *parent_fetch;
	nsurl *url;
	bool aborted;
	bool locked;
	struct fetch_onyx_context *r_next, *r_prev;

	/* --- non-blocking state machine --- */
	enum onyx_phase phase;
	bool tls;
	int sock;			/* plaintext socket, or -1 */
	onyx_tls_sess *ts;		/* TLS session, or NULL */
	uint8_t *buf;			/* growable response accumulator (head + body) */
	size_t cap, len;
	unsigned t_last;		/* kapi_get_ticks at the last byte received (idle timeout) */

	struct onyx_job *job;		/* the download's thread, while it runs (or 0) */
	bool nothread;			/* no thread for this one: the state machine */

	/* --- the request --- */
	char *method;			/* "GET", "POST", or a script's (X-Onyx-Method) */
	char *hdrs;			/* its own header lines ("Name: value\r\n"...), or 0 */
	char *body;			/* the body (url-encoded / text), or 0 */
};

static struct fetch_onyx_context *ring = NULL;

/* Idle timeout of a download: no byte for 30 s (kapi_get_ticks counts at 100 Hz). */
#define ONYX_IDLE_TICKS	3000

/* ---- a download in a thread of its own (kernel v67) ----------------------- */
#if defined(__aarch64__)
#define ONYX_THREADS	1
#else
#define ONYX_THREADS	0	/* (the PC bench: no threads) */
#endif

#define ONYX_MAX_WORKERS	8	/* downloads at once (the kernel has 16 sockets in all) */

enum { JOB_RUNNING = 0, JOB_DONE };

struct onyx_job {
	char *url;			/* a copy: the worker never touches an nsurl */
	char *method, *hdrs, *body;	/* copies of the request's (see the context) */
	volatile int lk;		/* kapi_lock: state / orphan handoff */
	volatile int state;		/* JOB_RUNNING, JOB_DONE */
	volatile int cancel;		/* the fetch was aborted: stop soon */
	volatile int orphan;		/* ... and its context is gone: the worker frees the job */
	uint8_t *buf;			/* the response (head + body) */
	size_t cap, len;
	const char *err;		/* a failure before any byte (a static string), or 0 */
};

static int onyx_workers;		/* jobs running (UI thread's count) */

static void onyx_job_free(struct onyx_job *j)
{
	free(j->url);
	free(j->method);
	free(j->hdrs);
	free(j->body);
	free(j->buf);
	free(j);
}

#if ONYX_THREADS
static bool onyx_threads_ok(void)
{
	/* (a kernel >= 67 whose table has them: the PC bench's fake kapi says 67 but has none) */
	return KT->version >= 67 && KT->thread_create != 0;
}
#endif


/* ---- tiny URL split (host / port / path) over nsurl_access() ---------- */
static bool onyx_split_url(const char *url, char *host, size_t hcap,
		unsigned *port, char *path, size_t pcap, unsigned default_port)
{
	const char *p = url;
	const char *q;
	size_t i = 0, j = 0;

	*port = default_port;
	for (q = url; *q; q++)			/* skip "scheme://" */
		if (q[0] == ':' && q[1] == '/' && q[2] == '/') { p = q + 3; break; }

	while (*p && *p != ':' && *p != '/' && i + 1 < hcap)
		host[i++] = *p++;
	host[i] = '\0';
	while (*p && *p != ':' && *p != '/')	/* overflow tail */
		p++;

	if (*p == ':') {
		unsigned v = 0;
		p++;
		while (*p >= '0' && *p <= '9') v = v * 10 + (unsigned)(*p++ - '0');
		if (v) *port = v;
	}
	if (*p != '/' && pcap > 1) path[j++] = '/';
	while (*p && j + 1 < pcap) path[j++] = *p++;
	path[j] = '\0';
	return host[0] != '\0';
}

/* ---- gzip / deflate body inflate (zlib) ------------------------------- */
/* On success returns 0 and replaces the body/len with a fresh malloc'd plain buffer. */
static int onyx_inflate(const char *encoding, uint8_t **body, size_t *len)
{
	z_stream zs;
	size_t cap, have = 0;
	uint8_t *out;
	int wbits, ret;

	if (encoding == NULL) return 0;
	if (strstr(encoding, "gzip") != NULL)      wbits = 16 + MAX_WBITS;	/* gzip */
	else if (strstr(encoding, "deflate") != NULL) wbits = MAX_WBITS;		/* zlib */
	else return 0;							/* identity */

	memset(&zs, 0, sizeof zs);
	if (inflateInit2(&zs, wbits) != Z_OK)
		return -1;

	cap = (*len ? *len : 1) * 4 + 64;
	out = malloc(cap);
	if (out == NULL) { inflateEnd(&zs); return -1; }

	zs.next_in = *body;
	zs.avail_in = (uInt)*len;
	do {
		if (have + 4096 > cap) {
			uint8_t *nb = realloc(out, cap * 2);
			if (nb == NULL) { free(out); inflateEnd(&zs); return -1; }
			out = nb; cap *= 2;
		}
		zs.next_out = out + have;
		zs.avail_out = (uInt)(cap - have);
		ret = inflate(&zs, Z_NO_FLUSH);
		if (ret != Z_OK && ret != Z_STREAM_END) { free(out); inflateEnd(&zs); return -1; }
		have = cap - zs.avail_out;
	} while (ret != Z_STREAM_END && zs.avail_in > 0);
	inflateEnd(&zs);

	free(*body);
	*body = out;
	*len = have;
	return 0;
}

/* ---- transport helpers (plaintext or TLS, same non-blocking convention) ---- */
static void onyx_conn_close(struct fetch_onyx_context *c)
{
	if (c->tls) { if (c->ts) onyx_nstls_close(c->ts); c->ts = NULL; }
	else        { if (c->sock >= 0) kapi_tcp_close(c->sock); c->sock = -1; }
}

/* ---- fetcher operations ----------------------------------------------- */
static bool fetch_onyx_initialise(lwc_string *scheme)
{
	NSLOG(netsurf, INFO, "onyx fetcher init: %s", lwc_string_data(scheme));
	return true;
}

static void fetch_onyx_finalise(lwc_string *scheme)
{
	(void)scheme;
}

static bool fetch_onyx_can_fetch(const nsurl *url)
{
	(void)url;
	return true;
}

/* A case-insensitive "Name:" at the start of a header line. */
static bool hdr_is(const char *line, const char *name)
{
	size_t n = strlen(name);
	return strncasecmp(line, name, n) == 0 && line[n] == ':';
}

/* The request: "METHOD path HTTP/1.0", our headers, the caller's, the body. malloc'd;
 * *len its length (the body may hold any byte but NUL). */
static char *onyx_request(const char *method, const char *path, const char *host,
		const char *hdrs, const char *body, int *len)
{
	/* (Google Fonts' style sheets as a current browser gets them: WOFF2 fonts split by
	 * unicode-range -- onyx_webfont.c fetches the Latin subset) */
	const char *ua = strcasecmp(host, "fonts.googleapis.com") == 0 ?
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
		"(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36" : "NetSurf (Onyx)";
	size_t blen = body != NULL ? strlen(body) : 0;
	bool has_ctype = false, has_accept = false;
	char extra[160];
	size_t cap;
	char *r;
	int n;

	if (hdrs != NULL) {
		const char *l = hdrs;
		while (*l != '\0') {
			if (hdr_is(l, "Content-Type")) has_ctype = true;
			if (hdr_is(l, "Accept")) has_accept = true;
			l = strchr(l, '\n');
			if (l == NULL) break;
			l++;
		}
	}
	extra[0] = '\0';
	if (body != NULL)
		snprintf(extra, sizeof extra, "%sContent-Length: %u\r\n",
			has_ctype ? "" : "Content-Type: application/x-www-form-urlencoded\r\n",
			(unsigned) blen);

	cap = 512 + strlen(method) + strlen(path) + strlen(host) + strlen(ua) +
		(hdrs != NULL ? strlen(hdrs) : 0) + strlen(extra) + blen;
	r = malloc(cap);
	if (r == NULL)
		return NULL;
	n = snprintf(r, cap,
		"%s %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\n%s"
		"Accept-Encoding: gzip, deflate\r\nConnection: close\r\n%s%s\r\n",
		method, path, host, ua, has_accept ? "" : "Accept: */*\r\n",
		hdrs != NULL ? hdrs : "", extra);
	if (n <= 0 || (size_t) n + blen >= cap) {
		free(r);
		return NULL;
	}
	if (blen > 0)
		memcpy(r + n, body, blen);
	*len = n + (int) blen;
	return r;
}

static void *fetch_onyx_setup(struct fetch *parent_fetch, nsurl *url,
		bool only_2xx, bool downgrade_tls, const char *post_urlenc,
		const struct fetch_multipart_data *post_multipart,
		const char **headers)
{
	struct fetch_onyx_context *ctx = calloc(1, sizeof(*ctx));
	size_t hlen = 0;
	int i;
	(void)only_2xx; (void)downgrade_tls; (void)post_multipart;	/* (multipart: not yet) */
	if (ctx == NULL)
		return NULL;
	ctx->parent_fetch = parent_fetch;
	ctx->url = nsurl_ref(url);

	/* the request: its method, its headers (not the conditional ones: a 304 would need
	 * FETCH_NOTMODIFIED, which this fetcher does not send), its body */
	for (i = 0; headers != NULL && headers[i] != NULL; i++)
		hlen += strlen(headers[i]) + 2;
	if (hlen > 0)
		ctx->hdrs = calloc(1, hlen + 1);
	for (i = 0; headers != NULL && headers[i] != NULL; i++) {
		const char *h = headers[i];
		if (hdr_is(h, "X-Onyx-Method")) {
			const char *v = h + 14;
			while (*v == ' ') v++;
			free(ctx->method);
			ctx->method = strdup(v);
			continue;
		}
		if (hdr_is(h, "If-None-Match") || hdr_is(h, "If-Modified-Since"))
			continue;
		if (ctx->hdrs != NULL) {
			strcat(ctx->hdrs, h);
			strcat(ctx->hdrs, "\r\n");
		}
	}
	if (post_urlenc != NULL)
		ctx->body = strdup(post_urlenc);
	if (ctx->method == NULL)
		ctx->method = strdup(post_urlenc != NULL ? "POST" : "GET");
	ctx->phase = PH_INIT;
	ctx->sock = -1;
	RING_INSERT(ring, ctx);
	return ctx;
}

static bool fetch_onyx_start(void *ctx)
{
	(void)ctx;
	return true;
}

#if ONYX_THREADS
static void onyx_job_drop(struct fetch_onyx_context *c);
#endif

static void fetch_onyx_free(void *ctx)
{
	struct fetch_onyx_context *c = ctx;
#if ONYX_THREADS
	onyx_job_drop(c);		/* a download still in its thread: orphaned */
#endif
	onyx_conn_close(c);
	free(c->buf);
	free(c->method);
	free(c->hdrs);
	free(c->body);
	nsurl_unref(c->url);
	free(c);
}

static void fetch_onyx_abort(void *ctx)
{
	struct fetch_onyx_context *c = ctx;
	c->aborted = true;	/* the poll loop performs the cleanup */
}

static void fetch_onyx_send(const fetch_msg *msg, struct fetch_onyx_context *c)
{
	c->locked = true;
	fetch_send_callback(msg, c->parent_fetch);
	c->locked = false;
}

static void fetch_onyx_error(struct fetch_onyx_context *c, const char *err)
{
	fetch_msg msg;
	msg.type = FETCH_ERROR;
	msg.data.error = err;
	fetch_onyx_send(&msg, c);
}

/* Locate a header value (case-insensitive) within the response head. Writes a
 * NUL-terminated copy of the value into val[vcap]. */
static bool header_value(const char *head, size_t headlen, const char *name,
		char *val, size_t vcap)
{
	size_t nlen = strlen(name);
	const char *p = head;
	const char *end = head + headlen;
	while (p < end) {
		const char *eol = memchr(p, '\n', (size_t)(end - p));
		size_t linelen = eol ? (size_t)(eol - p) : (size_t)(end - p);
		if (linelen >= nlen && strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
			const char *v = p + nlen + 1;
			size_t i = 0;
			while (v < p + linelen && (*v == ' ' || *v == '\t')) v++;
			while (v < p + linelen && *v != '\r' && i + 1 < vcap) val[i++] = *v++;
			val[i] = '\0';
			return true;
		}
		if (!eol) break;
		p = eol + 1;
	}
	return false;
}

/* PH_INIT: parse the URL, open the connection (blocking; TLS resumes per lever B),
 * send the request, and allocate the response buffer. Returns true to advance to
 * PH_RECV, false on a fatal error (an FETCH_ERROR has been delivered). */
static bool fetch_onyx_begin(struct fetch_onyx_context *c)
{
	char host[256], path[1024], *req;
	unsigned port;
	int len;
	const char *url = nsurl_access(c->url);

	c->tls = (strncasecmp(url, "https:", 6) == 0);
	if (!onyx_split_url(url, host, sizeof host, &port, path, sizeof path,
			    c->tls ? 443 : 80)) {
		fetch_onyx_error(c, "Malformed URL");
		return false;
	}

	if (c->tls) {
		c->ts = onyx_nstls_open(host, port);		/* connect + (resumed) handshake */
		if (c->ts == NULL) { fetch_onyx_error(c, "Connection failed"); return false; }
	} else {
		c->sock = kapi_tcp_connect(host, port);
		if (c->sock < 0) { fetch_onyx_error(c, "Connection failed"); return false; }
	}

	req = onyx_request(c->method, path, host, c->hdrs, c->body, &len);
	if (req == NULL) {
		onyx_conn_close(c);
		fetch_onyx_error(c, "Request too large");
		return false;
	}
	if (c->tls) onyx_nstls_send(c->ts, req, len);
	else        kapi_tcp_send(c->sock, req, len);
	free(req);

	c->cap = 16384;
	c->len = 0;
	c->buf = malloc(c->cap);
	if (c->buf == NULL) { onyx_conn_close(c); fetch_onyx_error(c, "Out of memory"); return false; }

	c->t_last = kapi_get_ticks();
	return true;
}

/* PH_DONE: parse the accumulated response (status / headers / body), follow a
 * redirect, inflate gzip/deflate, and deliver it to the NetSurf core. */
static void fetch_onyx_deliver(struct fetch_onyx_context *c)
{
	uint8_t *resp = c->buf, *body;
	size_t resplen = c->len, headlen, bodylen;
	const char *blank;
	char ctype[128], cenc[64];
	fetch_msg msg;
	int code = 0;

	if (resplen == 0) { fetch_onyx_error(c, "Empty response"); return; }

	/* status line: "HTTP/1.x NNN ..." */
	{
		const char *sp = (const char *)resp;
		const char *limit = (const char *)resp + resplen;
		while (sp < limit && *sp != ' ') sp++;
		if (sp < limit) { sp++; while (sp < limit && *sp >= '0' && *sp <= '9') code = code * 10 + (*sp++ - '0'); }
	}
	if (code == 0) code = 200;

	/* split head / body at the blank line */
	blank = NULL;
	{
		size_t k;
		for (k = 0; k + 3 < resplen; k++)
			if (resp[k]=='\r' && resp[k+1]=='\n' && resp[k+2]=='\r' && resp[k+3]=='\n') {
				blank = (const char *)resp + k; break;
			}
	}
	headlen = blank ? (size_t)((const uint8_t *)blank - resp) : resplen;
	body = blank ? (uint8_t *)blank + 4 : resp + resplen;
	bodylen = blank ? resplen - (headlen + 4) : 0;

	/* 3xx redirect (but not 304 Not Modified): resolve Location against the request
	 * URL and hand NetSurf a FETCH_REDIRECT (its llcache enforces the limit + loops). */
	if (code >= 300 && code < 400 && code != 304) {
		char loc[2048];
		if (header_value((const char *)resp, headlen, "Location", loc, sizeof loc)) {
			nsurl *target = NULL;
			if (nsurl_join(c->url, loc, &target) == NSERROR_OK && target != NULL) {
				fetch_set_http_code(c->parent_fetch, code);
				if (!c->aborted) {
					msg.type = FETCH_REDIRECT;
					msg.data.redirect = nsurl_access(target);
					fetch_onyx_send(&msg, c);
				}
				nsurl_unref(target);
				return;
			}
		}
	}

	/* inflate gzip/deflate in place (body is a slice of resp; copy out) */
	if (header_value((const char *)resp, headlen, "Content-Encoding", cenc, sizeof cenc)) {
		uint8_t *b = malloc(bodylen ? bodylen : 1);
		if (b != NULL) {
			memcpy(b, body, bodylen);
			if (onyx_inflate(cenc, &b, &bodylen) == 0)
				body = b;	/* b now owned; freed below */
			else { free(b); }
		}
	}

	fetch_set_http_code(c->parent_fetch, code);

	/* The status line, then the headers, one FETCH_HEADER each (the core keeps them:
	 * scripts read them, llcache_handle_get_header_at). Left out: the cache-control ones
	 * (the cache behaves as when only Content-Type came) and those the inflate made wrong. */
	(void)ctype;
	{
		static const char *const skip[] = { "Content-Encoding", "Content-Length",
			"Transfer-Encoding", "Cache-Control", "Expires", "ETag", "Last-Modified",
			"Age", "Pragma", "Date", "Vary", "Set-Cookie", "Location", NULL };
		const char *p = (const char *)resp, *end = (const char *)resp + headlen;
		bool first = true;
		while (p < end && !c->aborted) {
			const char *eol = memchr(p, '\n', (size_t)(end - p));
			size_t ll = eol ? (size_t)(eol - p) : (size_t)(end - p);
			size_t k;
			bool keep = ll > 0;
			if (ll > 0 && p[ll - 1] == '\r') ll--;
			for (k = 0; keep && !first && skip[k] != NULL; k++)
				if (ll > strlen(skip[k]) && hdr_is(p, skip[k])) keep = false;
			if (keep && ll > 0 && ll < 4000) {
				char line[4001];		/* NUL-terminated: the core splits with strchr */
				memcpy(line, p, ll);
				line[ll] = '\0';
				msg.type = FETCH_HEADER;
				msg.data.header_or_data.buf = (const uint8_t *)line;
				msg.data.header_or_data.len = ll;
				fetch_onyx_send(&msg, c);
			}
			first = false;
			if (eol == NULL) break;
			p = eol + 1;
		}
	}
	if (!c->aborted) {
		msg.type = FETCH_DATA;
		msg.data.header_or_data.buf = body;
		msg.data.header_or_data.len = bodylen;
		fetch_onyx_send(&msg, c);
	}
	if (!c->aborted) {
		msg.type = FETCH_FINISHED;
		fetch_onyx_send(&msg, c);
	}

	if (body < resp || body >= resp + resplen)
		free(body);		/* inflated copy */
}

/* Advance one fetch by a single non-blocking step. Returns true when the fetch is
 * complete (delivered or errored) and should be removed from the ring. */
static bool fetch_onyx_step(struct fetch_onyx_context *c)
{
	int r;

	if (c->phase == PH_INIT) {
		if (!fetch_onyx_begin(c))	/* connect+send (blocking, once per resource) */
			return true;		/* error already delivered */
		c->phase = PH_RECV;
		return false;			/* yield; recv on subsequent polls */
	}

	/* PH_RECV: one non-blocking read. Grow the buffer as needed. */
	if (c->len + 4096 > c->cap) {
		uint8_t *nb = realloc(c->buf, c->cap * 2);
		if (nb == NULL) { onyx_conn_close(c); fetch_onyx_error(c, "Out of memory"); return true; }
		c->buf = nb; c->cap *= 2;
	}

	r = c->tls ? onyx_nstls_recv(c->ts, c->buf + c->len, (int)(c->cap - c->len))
		   : kapi_tcp_recv(c->sock, c->buf + c->len, (int)(c->cap - c->len));

	if (r > 0) {
		c->len += (size_t)r;
		c->t_last = kapi_get_ticks();
		return false;			/* more may follow; keep reading next poll */
	}
	if (r == 0) {				/* nothing yet -> yield to other fetches */
		if (kapi_get_ticks() - c->t_last > ONYX_IDLE_TICKS) {	/* idle too long */
			onyx_conn_close(c);
			if (c->len > 0) { fetch_onyx_deliver(c); }	/* deliver what we have */
			else            { fetch_onyx_error(c, "Timeout"); }
			return true;
		}
		return false;
	}

	/* r < 0: peer closed -> HTTP/1.0 end-of-response. Deliver. */
	onyx_conn_close(c);
	fetch_onyx_deliver(c);
	return true;
}

#if ONYX_THREADS
/* The connects, one at a time: several at once (the DNS lookups, the TCP handshakes on the
 * network core) failed together ("Connection failed" for a page's style sheet and images,
 * the page then laid out without them). Short (the TLS handshakes and the downloads still
 * overlap); a failed one is tried again once. */
static volatile int onyx_connect_lk;

static int onyx_connect(const char *host, unsigned port)
{
	int sock, tries;
	for (tries = 0; tries < 2; tries++) {
		kapi_lock(&onyx_connect_lk);
		sock = kapi_tcp_connect(host, port);
		kapi_unlock(&onyx_connect_lk);
		if (sock >= 0)
			return sock;
		kapi_msleep(200);
	}
	return sock;
}

/* The download thread: connect, request, read to the end -- blocking, in its own thread.
 * It touches its job only; the UI thread delivers the response (fetch_onyx_poll). */
static int fetch_onyx_worker(void *arg)
{
	struct onyx_job *j = arg;
	char host[256], path[1024], *req;
	unsigned port;
	bool tls = strncasecmp(j->url, "https:", 6) == 0;
	int sock = -1, len, orphan;
	onyx_tls_sess *ts = NULL;
	unsigned t_last;

	if (!onyx_split_url(j->url, host, sizeof host, &port, path, sizeof path, tls ? 443 : 80)) {
		j->err = "Malformed URL";
		goto done;
	}
	sock = onyx_connect(host, port);			/* DNS + connect (one at a time) */
	if (sock >= 0 && tls) {
		ts = onyx_nstls_start(sock, host);		/* (resumed) handshake: in parallel */
		if (ts == NULL) sock = -1;			/* (closed by onyx_nstls_start) */
	}
	if (tls ? ts == NULL : sock < 0) { j->err = "Connection failed"; goto done; }

	req = onyx_request(j->method, path, host, j->hdrs, j->body, &len);
	if (req == NULL) { j->err = "Request too large"; goto close; }
	if (tls) onyx_nstls_send(ts, req, len);
	else     kapi_tcp_send(sock, req, len);
	free(req);

	j->cap = 16384;
	j->buf = malloc(j->cap);
	if (j->buf == NULL) { j->err = "Out of memory"; goto close; }

	t_last = kapi_get_ticks();
	while (!j->cancel) {
		int r;
		if (j->len + 4096 > j->cap) {
			uint8_t *nb = realloc(j->buf, j->cap * 2);
			if (nb == NULL) { j->err = "Out of memory"; break; }
			j->buf = nb; j->cap *= 2;
		}
		/* (a TLS read waits for its segment itself; a plain one says 0: nothing yet) */
		r = tls ? onyx_nstls_recv(ts, j->buf + j->len, (int)(j->cap - j->len))
			: kapi_tcp_recv(sock, j->buf + j->len, (int)(j->cap - j->len));
		if (r > 0) { j->len += (size_t)r; t_last = kapi_get_ticks(); continue; }
		if (r < 0) break;				/* closed: the end of the response */
		if (kapi_get_ticks() - t_last > ONYX_IDLE_TICKS) {
			if (j->len == 0) j->err = "Timeout";
			break;					/* (what came is delivered) */
		}
		kapi_msleep(2);
	}
close:
	if (tls) onyx_nstls_close(ts);
	else     kapi_tcp_close(sock);
done:
	kapi_lock(&j->lk);
	j->state = JOB_DONE;
	orphan = j->orphan;
	kapi_unlock(&j->lk);
	if (orphan) onyx_job_free(j);			/* its fetch was aborted and freed */
	return 0;
}

/* The UI thread: start a download's thread. false: none (the state machine takes it). */
static bool onyx_job_start(struct fetch_onyx_context *c)
{
	struct onyx_job *j = calloc(1, sizeof *j);
	if (j == NULL) return false;
	j->url = strdup(nsurl_access(c->url));
	j->method = strdup(c->method);
	j->hdrs = c->hdrs != NULL ? strdup(c->hdrs) : NULL;
	j->body = c->body != NULL ? strdup(c->body) : NULL;
	if (j->url == NULL || j->method == NULL) { onyx_job_free(j); return false; }
	if (kapi_thread_create(fetch_onyx_worker, j, 0, "fetch") < 0) {
		onyx_job_free(j);
		return false;
	}
	c->job = j;
	onyx_workers++;
	return true;
}

/* The UI thread: the fetch goes (aborted, freed) while its job may still run. */
static void onyx_job_drop(struct fetch_onyx_context *c)
{
	struct onyx_job *j = c->job;
	bool done;
	if (j == NULL) return;
	c->job = NULL;
	onyx_workers--;
	kapi_lock(&j->lk);
	done = j->state == JOB_DONE;
	if (!done) { j->cancel = 1; j->orphan = 1; }	/* the worker frees it when it ends */
	kapi_unlock(&j->lk);
	if (done) onyx_job_free(j);
}

/* The UI thread, every poll: a threaded fetch. Returns true when it is finished. */
static bool fetch_onyx_step_threaded(struct fetch_onyx_context *c)
{
	struct onyx_job *j = c->job;
	if (j == NULL) {
		if (onyx_workers >= ONYX_MAX_WORKERS) return false;	/* (a later poll) */
		if (!onyx_job_start(c)) c->nothread = true;		/* the state machine */
		return false;
	}
	if (j->state != JOB_DONE) return false;
	__asm__ volatile ("dmb ish" ::: "memory");		/* (the worker's writes, then) */

	c->job = NULL;
	onyx_workers--;
	if (j->err != NULL && j->len == 0) {
		fetch_onyx_error(c, j->err);
	} else {
		c->buf = j->buf; c->len = j->len; c->cap = j->cap;	/* the response, taken over */
		j->buf = NULL;
		fetch_onyx_deliver(c);
	}
	onyx_job_free(j);
	return true;
}
#endif

static void fetch_onyx_poll(lwc_string *scheme)
{
	struct fetch_onyx_context *active = NULL;	/* fetches still running after this round */
	struct fetch_onyx_context *c;
	bool did_connect = false;			/* at most one blocking connect per poll */
	(void)scheme;

	/* Drain the ring, advance each fetch by ONE non-blocking step, then re-queue the
	 * ones that aren't finished -- so their read (download) phases overlap across the
	 * NetSurf main-loop's repeated poll() calls. The only blocking op is a connect in
	 * PH_INIT; we do at most one per poll so the main loop stays responsive. */
	while (ring != NULL) {
		c = ring;
		RING_REMOVE(ring, c);

		if (c->locked) {			/* mid-callback -> revisit next poll */
			RING_INSERT(active, c);
			continue;
		}
		if (c->aborted) {
			fetch_remove_from_queues(c->parent_fetch);
			fetch_free(c->parent_fetch);	/* -> fetch_onyx_free: closes conn, frees ctx */
			continue;
		}
#if ONYX_THREADS
		if (!c->nothread && c->phase == PH_INIT && onyx_threads_ok()) {
			if (fetch_onyx_step_threaded(c)) {	/* delivered (or errored) */
				fetch_remove_from_queues(c->parent_fetch);
				fetch_free(c->parent_fetch);
				continue;
			}
			RING_INSERT(active, c);		/* its thread still downloads */
			continue;
		}
#endif
		if (c->phase == PH_INIT && did_connect) {	/* defer this connect to a later poll */
			RING_INSERT(active, c);
			continue;
		}
		if (c->phase == PH_INIT)
			did_connect = true;

		if (fetch_onyx_step(c)) {		/* finished (delivered or errored) */
			fetch_remove_from_queues(c->parent_fetch);
			fetch_free(c->parent_fetch);
			continue;
		}
		RING_INSERT(active, c);			/* still in progress -> keep for next poll */
	}

	ring = active;
}

nserror fetch_onyx_register(void)
{
	const struct fetcher_operation_table ops = {
		.initialise = fetch_onyx_initialise,
		.acceptable = fetch_onyx_can_fetch,
		.setup      = fetch_onyx_setup,
		.start      = fetch_onyx_start,
		.abort      = fetch_onyx_abort,
		.free       = fetch_onyx_free,
		.poll       = fetch_onyx_poll,
		.finalise   = fetch_onyx_finalise,
	};
	nserror ret;

	/* The fetcher is scheme-agnostic: it reads the scheme off the URL and selects
	 * plaintext vs TLS. Register both http and https. */
	ret = fetcher_add(lwc_string_ref(corestring_lwc_http), &ops);
	if (ret != NSERROR_OK)
		return ret;

	return fetcher_add(lwc_string_ref(corestring_lwc_https), &ops);
}
