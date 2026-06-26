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
 * kapis, via the C-callable onyx_nstls wrapper); GET only; gzip/deflate decoding. Builds as
 * part of the NetSurf core (brick 9). See user/netsurf/README.md.
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
};

static struct fetch_onyx_context *ring = NULL;

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

static void *fetch_onyx_setup(struct fetch *parent_fetch, nsurl *url,
		bool only_2xx, bool downgrade_tls, const char *post_urlenc,
		const struct fetch_multipart_data *post_multipart,
		const char **headers)
{
	struct fetch_onyx_context *ctx = calloc(1, sizeof(*ctx));
	(void)only_2xx; (void)downgrade_tls; (void)post_urlenc;
	(void)post_multipart; (void)headers;
	if (ctx == NULL)
		return NULL;
	ctx->parent_fetch = parent_fetch;
	ctx->url = nsurl_ref(url);
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

static void fetch_onyx_free(void *ctx)
{
	struct fetch_onyx_context *c = ctx;
	onyx_conn_close(c);
	free(c->buf);
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
	char host[256], path[1024], req[1024];
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

	len = snprintf(req, sizeof req,
		"GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: NetSurf (Onyx)\r\n"
		"Accept: */*\r\nAccept-Encoding: gzip, deflate\r\nConnection: close\r\n\r\n",
		path, host);
	if (len <= 0 || len >= (int)sizeof req) {
		onyx_conn_close(c);
		fetch_onyx_error(c, "Request too large");
		return false;
	}
	if (c->tls) onyx_nstls_send(c->ts, req, len);
	else        kapi_tcp_send(c->sock, req, len);

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

	if (!c->aborted && header_value((const char *)resp, headlen, "Content-Type", ctype, sizeof ctype)) {
		char line[160];
		int n = snprintf(line, sizeof line, "Content-Type: %s", ctype);
		if (n > 0 && n < (int)sizeof line) {
			msg.type = FETCH_HEADER;
			msg.data.header_or_data.buf = (const uint8_t *)line;
			msg.data.header_or_data.len = (size_t)n;
			fetch_onyx_send(&msg, c);
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
		if (kapi_get_ticks() - c->t_last > 30000) {	/* idle too long */
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
