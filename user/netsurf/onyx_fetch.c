/*
 * onyx_fetch.c -- a NetSurf fetch scheme handler for Onyx (brick 8).
 *
 * NetSurf fetches every resource through a registered scheme handler
 * (struct fetcher_operation_table; see content/fetchers.h). The reference HTTP fetcher
 * (content/fetchers/curl.c) drives libcurl. Onyx has no libcurl; this fetcher drives the
 * Onyx TCP kapis directly (kapi_tcp_connect/send/recv/close) and inflates gzip/deflate
 * with zlib -- the same transport our HttpClient (user/http.hpp) uses, exposed to the
 * NetSurf core as the "http" scheme.
 *
 * Model (mirrors the simple data: fetcher): setup() rings a context, poll() runs each
 * fetch to completion synchronously and delivers FETCH_HEADER* / FETCH_DATA / FETCH_FINISHED
 * (or FETCH_ERROR) via fetch_send_callback(). Blocking is acceptable for a first bring-up
 * (the data: fetcher is synchronous too); a non-blocking, fdset-driven version comes later.
 *
 * Scope: http:// and https:// (the latter over user/tls/onyx_tls.hpp = mbedTLS on the TCP
 * kapis, via the C-callable onyx_nstls wrapper); GET only; gzip/deflate decoding. Builds as
 * part of the NetSurf core (brick 9) -- it needs the core headers + an Onyx frontend to
 * register it via fetch_onyx_register(). See user/netsurf/README.md.
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

struct fetch_onyx_context {
	struct fetch *parent_fetch;
	nsurl *url;
	bool aborted;
	bool locked;
	struct fetch_onyx_context *r_next, *r_prev;
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

/* ---- blocking HTTP/1.0 GET into a growable buffer --------------------- */
/* Returns the whole response (status line + headers + body) in out/outlen, or -1 on
 * connect/transport failure. Caller frees the buffer. `tls` selects the TLS transport
 * (onyx_nstls, mbedTLS) vs plaintext kapi_tcp -- both share kapi_tcp_recv's >0/0/<0
 * convention, so the read loop is identical. */
static int onyx_http_recv(const char *host, unsigned port, const char *path,
		bool tls, uint8_t **out, size_t *outlen)
{
	char req[1024];
	size_t cap = 16384, n = 0;
	uint8_t *buf;
	int sock = -1, idle = 0;
	onyx_tls_sess *ts = NULL;
	int len;

	if (tls) {
		ts = onyx_nstls_open(host, port);
		if (ts == NULL) return -1;
	} else {
		sock = kapi_tcp_connect(host, port);
		if (sock < 0) return -1;
	}

	len = snprintf(req, sizeof req,
		"GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: NetSurf (Onyx)\r\n"
		"Accept: */*\r\nAccept-Encoding: gzip, deflate\r\nConnection: close\r\n\r\n",
		path, host);
	if (len <= 0 || len >= (int)sizeof req) {
		if (tls) onyx_nstls_close(ts); else kapi_tcp_close(sock);
		return -1;
	}
	if (tls) onyx_nstls_send(ts, req, len); else kapi_tcp_send(sock, req, len);

	buf = malloc(cap);
	if (buf == NULL) {
		if (tls) onyx_nstls_close(ts); else kapi_tcp_close(sock);
		return -1;
	}

	for (;;) {
		int r;
		if (n + 4096 > cap) {
			uint8_t *nb = realloc(buf, cap * 2);
			if (nb == NULL) {
				free(buf);
				if (tls) onyx_nstls_close(ts); else kapi_tcp_close(sock);
				return -1;
			}
			buf = nb; cap *= 2;
		}
		r = tls ? onyx_nstls_recv(ts, buf + n, (int)(cap - n))
			: kapi_tcp_recv(sock, buf + n, (int)(cap - n));
		if (r > 0) { n += (size_t)r; idle = 0; }
		else if (r == 0) { if (++idle > 1000) break; kapi_msleep(10); }
		else break;			/* peer closed (HTTP/1.0 EOF) */
	}
	if (tls) onyx_nstls_close(ts); else kapi_tcp_close(sock);
	*out = buf;
	*outlen = n;
	return 0;
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
 * NUL-terminated, lowercased-key-agnostic copy of the value into val[vcap]. */
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

static void fetch_onyx_run(struct fetch_onyx_context *c)
{
	char host[256], path[1024], ctype[128], cenc[64];
	unsigned port;
	uint8_t *resp = NULL, *body;
	size_t resplen = 0, headlen, bodylen;
	const char *blank;
	fetch_msg msg;
	int code = 0;
	const char *url = nsurl_access(c->url);
	bool tls = (strncasecmp(url, "https:", 6) == 0);

	if (!onyx_split_url(url, host, sizeof host, &port, path, sizeof path,
			    tls ? 443 : 80)) {
		fetch_onyx_error(c, "Malformed URL");
		return;
	}
	if (onyx_http_recv(host, port, path, tls, &resp, &resplen) != 0 || resplen == 0) {
		free(resp);
		fetch_onyx_error(c, "Connection failed");
		return;
	}

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

	/* 3xx redirect (but not 304 Not Modified): resolve the Location against the
	 * request URL and hand NetSurf a FETCH_REDIRECT -- its llcache re-fetches and
	 * enforces the redirect limit + loop detection. Crucial for http->https sites. */
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
				free(resp);
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
				body = b;	/* b now owned; resp freed below either way */
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
	free(resp);
}

static void fetch_onyx_poll(lwc_string *scheme)
{
	struct fetch_onyx_context *c, *save_ring = NULL;
	(void)scheme;

	while (ring != NULL) {
		c = ring;
		RING_REMOVE(ring, c);
		if (c->locked) { RING_INSERT(save_ring, c); continue; }
		if (!c->aborted)
			fetch_onyx_run(c);
		fetch_remove_from_queues(c->parent_fetch);
		fetch_free(c->parent_fetch);
	}
	ring = save_ring;
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

	/* The fetcher is scheme-agnostic: fetch_onyx_run() reads the scheme off the URL
	 * and selects plaintext vs TLS. Register both http and https. */
	ret = fetcher_add(lwc_string_ref(corestring_lwc_http), &ops);
	if (ret != NSERROR_OK)
		return ret;

	return fetcher_add(lwc_string_ref(corestring_lwc_https), &ops);
}
