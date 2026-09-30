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
 * Threads (kernel v67): each download runs in a THREAD of its own (fetch_onyx_worker) --
 * the DNS, the connect, the TLS handshake, the request and every read, all blocking there
 * while the UI thread goes on. The worker only touches its struct onyx_job (plain copies:
 * the URL as a string; the response bytes); everything NetSurf -- the head's headers, the
 * cookies, the redirects, the inflate, fetch_send_callback, nsurl, the fetch queues -- stays
 * on the UI thread, which picks the jobs' progress up in fetch_onyx_poll. A worker posts
 * (kapi_post) when the head has come, when the body has grown, when it ends: the UI thread,
 * waiting in kapi_pump_wait, wakes at once. An aborted fetch whose worker still runs is
 * orphaned: the worker frees its job when it ends.
 *
 * HTTP/1.1 (network lever C): the requests keep their connection (keep-alive); a response is
 * framed by its Content-Length or its chunks (read to the close only when it has neither),
 * and its connection then goes back to a POOL, per host / port / scheme, for the next
 * request there -- no DNS, no TCP handshake, no TLS handshake again. A pooled connection
 * the server closed meanwhile is noticed at its first request (nothing comes back): the
 * request is sent again on a new connection.
 *
 * Streaming: the head goes to the core as soon as it is in (FETCH_HEADER each line, the
 * redirect), then the body as it comes (FETCH_DATA, inflated on the fly): the HTML parser
 * finds the style sheets, the scripts and the images while the page still downloads.
 *
 * The request: GET, POST (a url-encoded / text body) and any method a script asks for (the
 * pseudo-header "X-Onyx-Method: PUT", from JS fetch / XMLHttpRequest, taken off the
 * request); the request's own headers; the page's cookies (urldb, read on the UI thread) and
 * every Set-Cookie the response brings (redirects' too) handed to NetSurf's cookie jar; the
 * Referer (strict-origin-when-cross-origin, as Chrome) and an Origin on the other methods;
 * the User-Agent and the Accept-Language of the options. The response goes to the core with
 * its status line and headers (the cache-control ones and those the inflate makes wrong
 * left out: the cache behaves as before).
 *
 * Without threads (an older kernel, a thread refused) a fetch takes a simpler path: a
 * state machine advanced one non-blocking step per poll, HTTP/1.0 read to the close, the
 * response delivered whole.
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
#include "utils/nsoption.h"
#include "utils/useragent.h"
#include "utils/corestrings.h"
#include "utils/ring.h"
#include "utils/log.h"

#include "content/fetch.h"
#include "content/fetchers.h"
#include "content/urldb.h"

#include "kapi.h"		/* Onyx TCP transport */
#include "onyx_nstls.h"		/* C-callable TLS transport (https) */

/* Per-fetch state machine phases (the path without threads). */
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

	/* --- non-blocking state machine (no threads) --- */
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
	char *hdrs;			/* its header lines ("Name: value\r\n"...): the caller's,
					 * the cookies, the referer */
	char *body;			/* the body (url-encoded / text), or 0 */

	/* --- the response, as it comes (threads) --- */
	bool head_done;			/* its head handed to the core */
	bool redirected;		/* ... and it was a redirect: nothing more to hand */
	z_stream zs;			/* the Content-Encoding's inflate, when zs_on */
	bool zs_on, zs_end;
	size_t delivered;		/* body bytes handed to the core */
};

static struct fetch_onyx_context *ring = NULL;

/* Idle timeout of a download: no byte for 30 s (kapi_get_ticks counts at 100 Hz). */
#define ONYX_IDLE_TICKS	3000

#define ONYX_MAX_WORKERS	8	/* downloads at once (the kernel has 16 sockets in all) */

/* ---- the request's pieces --------------------------------------------------- */

/* A case-insensitive "Name:" at the start of a header line. */
static bool hdr_is(const char *line, const char *name)
{
	size_t n = strlen(name);
	return strncasecmp(line, name, n) == 0 && line[n] == ':';
}

/* Whether s holds word, case-insensitively (a header value's token). */
static bool ci_has(const char *s, const char *word)
{
	size_t n = strlen(word);

	for (; *s != '\0'; s++)
		if (strncasecmp(s, word, n) == 0)
			return true;
	return false;
}

/* Whether the header lines hold a "Name:" line. */
static bool hdrs_have(const char *hdrs, const char *name)
{
	const char *l = hdrs;

	while (l != NULL && *l != '\0') {
		if (hdr_is(l, name))
			return true;
		l = strchr(l, '\n');
		if (l != NULL)
			l++;
	}
	return false;
}

/* Split "scheme://host[:port]/path?query": host (<= hcap) and port; *path a pointer into url
 * (its path and query, or "/"). No length limit on the path. */
static bool onyx_split_url(const char *url, char *host, size_t hcap, unsigned *port,
		const char **path, unsigned default_port)
{
	const char *p = url, *q;
	size_t i = 0;

	*port = default_port;
	for (q = url; *q; q++)			/* skip "scheme://" */
		if (q[0] == ':' && q[1] == '/' && q[2] == '/') { p = q + 3; break; }
	while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#' && i + 1 < hcap)
		host[i++] = *p++;
	host[i] = '\0';
	while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#')	/* overflow tail */
		p++;
	if (*p == ':') {
		unsigned v = 0;
		p++;
		while (*p >= '0' && *p <= '9') v = v * 10 + (unsigned)(*p++ - '0');
		if (v) *port = v;
	}
	*path = (*p == '/' || *p == '?') ? p : "/";
	return host[0] != '\0';
}

/* The request: "METHOD path HTTP/1.1", our headers, the caller's, the body. malloc'd; *len its
 * length (the body may hold any byte but NUL). The path stops at a fragment. */
static char *onyx_request(const char *method, const char *path, const char *host,
		unsigned port, bool tls, const char *ua, const char *lang, const char *hdrs,
		const char *body, bool keepalive, int *len)
{
	size_t blen = body != NULL ? strlen(body) : 0;
	size_t plen = strcspn(path, "#");
	bool has_ctype = hdrs_have(hdrs, "Content-Type");
	bool has_accept = hdrs_have(hdrs, "Accept");
	bool has_lang = hdrs_have(hdrs, "Accept-Language");
	char extra[160], hostport[300];
	size_t cap;
	char *r;
	int n;

	extra[0] = '\0';
	if (body != NULL)
		snprintf(extra, sizeof extra, "%sContent-Length: %u\r\n",
			has_ctype ? "" : "Content-Type: application/x-www-form-urlencoded\r\n",
			(unsigned) blen);
	if (port == (tls ? 443u : 80u))
		snprintf(hostport, sizeof hostport, "%s", host);
	else
		snprintf(hostport, sizeof hostport, "%s:%u", host, port);

	cap = 600 + strlen(method) + plen + strlen(hostport) + strlen(ua) + strlen(lang) +
		(hdrs != NULL ? strlen(hdrs) : 0) + strlen(extra) + blen;
	r = malloc(cap);
	if (r == NULL)
		return NULL;
	n = snprintf(r, cap,
		"%s %.*s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\n%s%s%s%s"
		"Accept-Encoding: gzip, deflate\r\nConnection: %s\r\n%s%s\r\n",
		method, (int) plen, path, hostport, ua,
		has_accept ? "" : "Accept: */*\r\n",
		has_lang ? "" : "Accept-Language: ", has_lang ? "" : lang,
		has_lang ? "" : "\r\n",
		keepalive ? "keep-alive" : "close",
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

/* Append "Name: value\r\n" to *hdrs (malloc'd, or NULL). */
static void hdrs_add(char **hdrs, const char *name, const char *value, size_t vlen)
{
	size_t old = *hdrs != NULL ? strlen(*hdrs) : 0;
	char *h = realloc(*hdrs, old + strlen(name) + vlen + 5);

	if (h == NULL)
		return;
	sprintf(h + old, "%s: %.*s\r\n", name, (int) vlen, value);
	*hdrs = h;
}

/* The referer and origin a request from `ref` to `url` sends (Chrome's default policy,
 * strict-origin-when-cross-origin): the whole referer (no fragment) within its origin, its
 * origin elsewhere, nothing from https to http. */
static void onyx_add_referer(char **hdrs, nsurl *url, nsurl *ref, bool origin_too)
{
	char *s = NULL;
	size_t l = 0;
	bool same, downgrade;
	lwc_string *us, *rs;

	if (ref == NULL)
		return;
	us = nsurl_get_component(url, NSURL_SCHEME);
	rs = nsurl_get_component(ref, NSURL_SCHEME);
	downgrade = rs != NULL && us != NULL &&
		strcasecmp(lwc_string_data(rs), "https") == 0 &&
		strcasecmp(lwc_string_data(us), "https") != 0;
	if (us != NULL) lwc_string_unref(us);
	if (rs != NULL) lwc_string_unref(rs);
	if (strncasecmp(nsurl_access(ref), "http", 4) != 0)
		return;		/* (a file: or about: page sends none) */
	same = nsurl_compare(url, ref, NSURL_SCHEME | NSURL_HOST | NSURL_PORT);
	if (!downgrade && !hdrs_have(*hdrs, "Referer")) {
		if (nsurl_get(ref, same ? NSURL_COMPLETE & ~NSURL_CREDENTIALS :
				NSURL_SCHEME | NSURL_HOST | NSURL_PORT, &s, &l) == NSERROR_OK) {
			if (same) {
				hdrs_add(hdrs, "Referer", s, l);
			} else {
				char *o = malloc(l + 2);
				if (o != NULL) {
					memcpy(o, s, l);
					o[l] = '/';
					hdrs_add(hdrs, "Referer", o, l + 1);
					free(o);
				}
			}
			free(s);
		}
	}
	if (origin_too && !hdrs_have(*hdrs, "Origin") &&
	    nsurl_get(ref, NSURL_SCHEME | NSURL_HOST | NSURL_PORT, &s, &l) == NSERROR_OK) {
		hdrs_add(hdrs, "Origin", s, l);
		free(s);
	}
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

	/* the cookies (read here, on the UI thread: urldb is not the workers'), the referer */
	if (!hdrs_have(ctx->hdrs, "Cookie")) {
		char *ck = urldb_get_cookie(url, true);
		if (ck != NULL && ck[0] != '\0')
			hdrs_add(&ctx->hdrs, "Cookie", ck, strlen(ck));
		free(ck);
	}
	onyx_add_referer(&ctx->hdrs, url, fetch_get_referer(parent_fetch),
			ctx->method != NULL && strcmp(ctx->method, "GET") != 0 &&
			strcmp(ctx->method, "HEAD") != 0);

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

static void onyx_job_drop(struct fetch_onyx_context *c);

static void fetch_onyx_free(void *ctx)
{
	struct fetch_onyx_context *c = ctx;
	onyx_job_drop(c);		/* a download still in its thread: orphaned */
	onyx_conn_close(c);
	if (c->zs_on)
		inflateEnd(&c->zs);
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

/* The status code of a head ("HTTP/1.x NNN ..."), 200 when it has none. */
static int head_status(const char *head, size_t headlen)
{
	const char *sp = head, *limit = head + headlen;
	int code = 0;

	while (sp < limit && *sp != ' ') sp++;
	if (sp < limit) {
		sp++;
		while (sp < limit && *sp >= '0' && *sp <= '9')
			code = code * 10 + (*sp++ - '0');
	}
	return code == 0 ? 200 : code;
}

/*
 * The UI thread: a response's head (its status line and header lines, without the blank
 * line) handed to the core -- its cookies to the jar, a redirect, the headers. True: the
 * body follows; false: done (a redirect sent, or the fetch aborted meanwhile).
 */
static bool onyx_head(struct fetch_onyx_context *c, const char *head, size_t headlen,
		char *cenc, size_t cenccap)
{
	static const char *const skip[] = { "Content-Encoding", "Content-Length",
		"Transfer-Encoding", "Cache-Control", "Expires", "ETag", "Last-Modified",
		"Age", "Pragma", "Date", "Vary", "Set-Cookie", "Location", "Connection",
		"Keep-Alive", NULL };
	const char *p = head, *end = head + headlen;
	int code = head_status(head, headlen);
	bool first = true;
	fetch_msg msg;

	/* every Set-Cookie to NetSurf's jar (a redirect's too: a login's session) */
	while (p < end) {
		const char *eol = memchr(p, '\n', (size_t)(end - p));
		size_t ll = eol ? (size_t)(eol - p) : (size_t)(end - p);
		if (ll > 0 && p[ll - 1] == '\r') ll--;
		if (ll > 11 && hdr_is(p, "Set-Cookie")) {
			char *v = malloc(ll + 1);
			if (v != NULL) {
				const char *s = p + 11;
				while (*s == ' ' || *s == '\t') s++;
				memcpy(v, s, (size_t)(p + ll - s));
				v[p + ll - s] = '\0';
				fetch_set_cookie(c->parent_fetch, v);
				free(v);
			}
		}
		if (eol == NULL) break;
		p = eol + 1;
	}

	/* 3xx redirect (but not 304 Not Modified): resolve Location against the request
	 * URL and hand NetSurf a FETCH_REDIRECT (its llcache enforces the limit + loops). */
	if (code >= 300 && code < 400 && code != 304) {
		char loc[8192];
		if (header_value(head, headlen, "Location", loc, sizeof loc)) {
			nsurl *target = NULL;
			if (nsurl_join(c->url, loc, &target) == NSERROR_OK && target != NULL) {
				fetch_set_http_code(c->parent_fetch, code);
				if (!c->aborted) {
					msg.type = FETCH_REDIRECT;
					msg.data.redirect = nsurl_access(target);
					fetch_onyx_send(&msg, c);
				}
				nsurl_unref(target);
				return false;
			}
		}
	}

	cenc[0] = '\0';
	header_value(head, headlen, "Content-Encoding", cenc, cenccap);
	fetch_set_http_code(c->parent_fetch, code);

	/* The status line, then the headers, one FETCH_HEADER each (the core keeps them:
	 * scripts read them, llcache_handle_get_header_at). Left out: the cache-control ones
	 * (the cache behaves as when only Content-Type came) and those the inflate made wrong. */
	p = head;
	while (p < end && !c->aborted) {
		const char *eol = memchr(p, '\n', (size_t)(end - p));
		size_t ll = eol ? (size_t)(eol - p) : (size_t)(end - p);
		size_t k;
		bool keep = ll > 0;
		if (ll > 0 && p[ll - 1] == '\r') ll--;
		for (k = 0; keep && !first && skip[k] != NULL; k++)
			if (ll > strlen(skip[k]) && hdr_is(p, skip[k])) keep = false;
		if (keep && ll > 0 && ll < 8000) {
			char line[8001];		/* NUL-terminated: the core splits with strchr */
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
	return !c->aborted;
}

/* PH_INIT (no threads): parse the URL, open the connection (blocking; TLS resumes), send
 * the request (HTTP/1.0, read to the close), and allocate the response buffer. Returns true
 * to advance to PH_RECV, false on a fatal error (an FETCH_ERROR has been delivered). */
static bool fetch_onyx_begin(struct fetch_onyx_context *c)
{
	char host[256], *req;
	const char *path;
	unsigned port;
	int len;
	const char *url = nsurl_access(c->url);

	c->tls = (strncasecmp(url, "https:", 6) == 0);
	if (!onyx_split_url(url, host, sizeof host, &port, &path, c->tls ? 443 : 80)) {
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

	req = onyx_request(c->method, path, host, port, c->tls, user_agent_string(),
			nsoption_charp(accept_language) != NULL ? nsoption_charp(accept_language) :
			"fr-FR,fr;q=0.9,en-US;q=0.8,en;q=0.7", c->hdrs, c->body, false, &len);
	if (req == NULL) {
		onyx_conn_close(c);
		fetch_onyx_error(c, "Request too large");
		return false;
	}
	/* (HTTP/1.1 with Connection: close -- read to the close, but a chunked answer is
	 * possible: the no-thread path undoes chunks in fetch_onyx_deliver) */
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

/* A chunked body undone in place: its length after (the chunks' data only). */
static size_t onyx_unchunk(uint8_t *b, size_t n)
{
	size_t i = 0, o = 0;

	while (i < n) {
		size_t sz = 0;
		int digits = 0;
		while (i < n) {
			int c = b[i], v = c >= '0' && c <= '9' ? c - '0' :
				c >= 'a' && c <= 'f' ? c - 'a' + 10 :
				c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
			if (v < 0) break;
			sz = sz * 16 + (size_t) v;
			i++;
			digits++;
		}
		while (i < n && b[i] != '\n') i++;	/* (chunk extensions) */
		i++;
		if (digits == 0 || sz == 0 || i > n)
			break;
		if (sz > n - i) sz = n - i;
		memmove(b + o, b + i, sz);
		o += sz;
		i += sz;
		while (i < n && (b[i] == '\r' || b[i] == '\n')) i++;
	}
	return o;
}

/* PH_DONE (no threads): parse the accumulated response (status / headers / body), follow
 * a redirect, inflate gzip/deflate, and deliver it to the NetSurf core. */
static void fetch_onyx_deliver(struct fetch_onyx_context *c)
{
	uint8_t *resp = c->buf, *body;
	size_t resplen = c->len, headlen, bodylen;
	const char *blank;
	char cenc[64], te[64];
	fetch_msg msg;

	if (resplen == 0) { fetch_onyx_error(c, "Empty response"); return; }

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

	if (!onyx_head(c, (const char *)resp, headlen, cenc, sizeof cenc))
		return;
	if (header_value((const char *)resp, headlen, "Transfer-Encoding", te, sizeof te) &&
	    strstr(te, "chunked") != NULL)
		bodylen = onyx_unchunk(body, bodylen);

	/* inflate gzip/deflate in place (body is a slice of resp; copy out) */
	if (cenc[0] != '\0') {
		uint8_t *b = malloc(bodylen ? bodylen : 1);
		if (b != NULL) {
			memcpy(b, body, bodylen);
			if (onyx_inflate(cenc, &b, &bodylen) == 0)
				body = b;	/* b now owned; freed below */
			else { free(b); }
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

	/* r < 0: peer closed -> the end of the response. Deliver. */
	onyx_conn_close(c);
	fetch_onyx_deliver(c);
	return true;
}

/* ==== the threaded path ==================================================== */

enum { JOB_RUNNING = 0, JOB_DONE };

struct onyx_job {
	char *url;			/* a copy: the worker never touches an nsurl */
	char *method, *hdrs, *body;	/* copies of the request's (see the context) */
	const char *ua, *lang;		/* (static / the options': they outlive the job) */
	volatile int lk;		/* kapi_lock: everything below */
	volatile int state;		/* JOB_RUNNING, JOB_DONE */
	volatile int cancel;		/* the fetch was aborted: stop soon */
	volatile int orphan;		/* ... and its context is gone: the worker frees the job */
	char *head;			/* the response's head (status line + headers), once in */
	size_t headlen;
	uint8_t *buf;			/* body bytes the UI thread has not taken yet */
	size_t cap, len;
	size_t total;			/* body bytes in all */
	const char *err;		/* a failure (a static string), or 0 */
};

static int onyx_workers;		/* jobs running (UI thread's count) */

static void onyx_job_free(struct onyx_job *j)
{
	free(j->url);
	free(j->method);
	free(j->hdrs);
	free(j->body);
	free(j->head);
	free(j->buf);
	free(j);
}

static bool onyx_threads_ok(void)
{
	return KT->version >= 67 && KT->thread_create != 0;
}

/* The UI thread wakes (kapi_pump_wait) on a post: this one only wakes it. */
static void onyx_wake(void *ctx, long value)
{
	(void) ctx;
	(void) value;
}

static void onyx_post_wake(void)
{
	if (KT->version >= 67 && KT->post != 0)
		kapi_post(onyx_wake, NULL, 0);
}

/* ---- the connections kept (keep-alive) -------------------------------------- */

struct onyx_conn {
	bool tls;
	int sock;			/* plaintext */
	onyx_tls_sess *ts;		/* TLS (owns its socket) */
	char host[256];
	unsigned port;
	unsigned idle_at;		/* kapi_get_ticks when it went back to the pool */
	unsigned uses;			/* requests it carried */
};

#define ONYX_POOL		8
#define ONYX_POOL_IDLE_TICKS	1000	/* kept 10 s idle (servers keep 5 .. 60 s) */

static struct onyx_conn onyx_pool[ONYX_POOL];
static int onyx_pool_n;
static volatile int onyx_pool_lk;

static void conn_close(struct onyx_conn *k)
{
	if (k->tls) {
		if (k->ts != NULL) onyx_nstls_close(k->ts);
	} else if (k->sock >= 0) {
		kapi_tcp_close(k->sock);
	}
	k->ts = NULL;
	k->sock = -1;
}

/* A connection kept for host / port / scheme -> true and *out (it is the caller's now). */
static bool pool_take(const char *host, unsigned port, bool tls, struct onyx_conn *out)
{
	struct onyx_conn stale[ONYX_POOL];
	int nstale = 0, i;
	bool found = false;
	unsigned now = kapi_get_ticks();

	kapi_lock(&onyx_pool_lk);
	for (i = 0; i < onyx_pool_n; ) {
		if (now - onyx_pool[i].idle_at > ONYX_POOL_IDLE_TICKS) {
			stale[nstale++] = onyx_pool[i];
			onyx_pool[i] = onyx_pool[--onyx_pool_n];
			continue;
		}
		i++;
	}
	for (i = onyx_pool_n - 1; i >= 0; i--) {	/* (the most recent: the likeliest alive) */
		if (onyx_pool[i].tls == tls && onyx_pool[i].port == port &&
		    strcasecmp(onyx_pool[i].host, host) == 0) {
			*out = onyx_pool[i];
			onyx_pool[i] = onyx_pool[--onyx_pool_n];
			found = true;
			break;
		}
	}
	kapi_unlock(&onyx_pool_lk);
	for (i = 0; i < nstale; i++)
		conn_close(&stale[i]);
	return found;
}

/* A connection back to the pool (the oldest one closed when it is full). */
static void pool_put(struct onyx_conn *k)
{
	struct onyx_conn old;
	bool drop = false;

	k->idle_at = kapi_get_ticks();
	kapi_lock(&onyx_pool_lk);
	if (onyx_pool_n == ONYX_POOL) {
		int o = 0;
		for (int i = 1; i < onyx_pool_n; i++)
			if (onyx_pool[i].idle_at - onyx_pool[o].idle_at > 0x80000000u)
				o = i;
		old = onyx_pool[o];
		onyx_pool[o] = onyx_pool[--onyx_pool_n];
		drop = true;
	}
	onyx_pool[onyx_pool_n++] = *k;
	kapi_unlock(&onyx_pool_lk);
	if (drop)
		conn_close(&old);
}

static int conn_send(struct onyx_conn *k, const void *b, int n)
{
	return k->tls ? onyx_nstls_send(k->ts, b, n) : kapi_tcp_send(k->sock, b, (unsigned) n);
}

/* >0 bytes, 0 nothing yet, <0 closed (a TLS read waits for its record itself) */
static int conn_recv(struct onyx_conn *k, void *b, int n)
{
	return k->tls ? onyx_nstls_recv(k->ts, b, n) : kapi_tcp_recv(k->sock, b, (unsigned) n);
}

/* The connects, one at a time: several at once failed together on the kernels that handed
 * the same socket to two connects (fixed in kernel/sys/net.cpp: SLOT_CONNECTING) -- short
 * now that the kernel caches the DNS answers and the connections are kept. A failed one is
 * tried again once. */
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

static bool conn_open(struct onyx_conn *k, const char *host, unsigned port, bool tls)
{
	memset(k, 0, sizeof *k);
	k->tls = tls;
	k->sock = -1;
	k->port = port;
	snprintf(k->host, sizeof k->host, "%s", host);
	k->sock = onyx_connect(host, port);		/* DNS + connect */
	if (k->sock < 0)
		return false;
	if (tls) {
		k->ts = onyx_nstls_start(k->sock, host);	/* (resumed) handshake */
		k->sock = -1;				/* (the session's, closed with it) */
		if (k->ts == NULL)
			return false;
	}
	return true;
}

/* ---- the worker's reading --------------------------------------------------- */

#define ONYX_IN 32768

struct onyx_in {
	struct onyx_conn *k;
	struct onyx_job *j;
	uint8_t b[ONYX_IN];
	size_t pos, len;
	unsigned t_last;
	bool closed;
};

/* More bytes in the input buffer: >0 read, 0 the connection closed, <0 cancelled / timeout. */
static int in_fill(struct onyx_in *in)
{
	if (in->pos > 0 && in->pos == in->len)
		in->pos = in->len = 0;
	if (in->pos > 0 && in->len > in->pos && in->len == ONYX_IN) {
		memmove(in->b, in->b + in->pos, in->len - in->pos);
		in->len -= in->pos;
		in->pos = 0;
	}
	if (in->len == ONYX_IN)
		return -1;		/* (a head line longer than the buffer) */
	while (!in->j->cancel) {
		int r = conn_recv(in->k, in->b + in->len, (int) (ONYX_IN - in->len));
		if (r > 0) {
			in->len += (size_t) r;
			in->t_last = kapi_get_ticks();
			return r;
		}
		if (r < 0) {
			in->closed = true;
			return 0;
		}
		if (kapi_get_ticks() - in->t_last > ONYX_IDLE_TICKS)
			return -1;
		kapi_msleep(2);
	}
	return -1;
}

/* A line of the head (without its CRLF) -> its length, or -1 (closed / cancelled). */
static int in_line(struct onyx_in *in, char *line, size_t cap)
{
	for (;;) {
		uint8_t *nl = memchr(in->b + in->pos, '\n', in->len - in->pos);
		if (nl != NULL) {
			size_t n = (size_t) (nl - (in->b + in->pos));
			size_t m = n > 0 && in->b[in->pos + n - 1] == '\r' ? n - 1 : n;
			if (m >= cap) m = cap - 1;
			memcpy(line, in->b + in->pos, m);
			line[m] = '\0';
			in->pos += n + 1;
			return (int) m;
		}
		if (in_fill(in) <= 0)
			return -1;
	}
}

/* Body bytes to the job (the UI thread takes them): false out of memory. */
static bool job_append(struct onyx_job *j, const uint8_t *b, size_t n, size_t *since_post)
{
	bool ok = true;

	if (n == 0)
		return true;
	kapi_lock(&j->lk);
	if (j->len + n > j->cap) {
		size_t cap = j->cap ? j->cap : 16384;
		uint8_t *nb;
		while (cap < j->len + n) cap *= 2;
		nb = realloc(j->buf, cap);
		if (nb == NULL) {
			ok = false;
		} else {
			j->buf = nb;
			j->cap = cap;
		}
	}
	if (ok) {
		memcpy(j->buf + j->len, b, n);
		j->len += n;
		j->total += n;
	}
	kapi_unlock(&j->lk);
	*since_post += n;
	if (*since_post >= 32768) {	/* (the UI thread told now and then, not each segment) */
		*since_post = 0;
		onyx_post_wake();
	}
	return ok;
}

/* n body bytes (or to the close when n is (size_t) -1) from the input to the job: true when
 * all came (to the close: when it closed). */
static bool in_body(struct onyx_in *in, size_t n, size_t *since_post)
{
	bool to_close = n == (size_t) -1;

	while (to_close || n > 0) {
		size_t have = in->len - in->pos, take;
		if (have == 0) {
			int r = in_fill(in);
			if (r == 0) return to_close;	/* closed */
			if (r < 0) return false;
			continue;
		}
		take = to_close || have < n ? have : n;
		if (!job_append(in->j, in->b + in->pos, take, since_post))
			return false;
		in->pos += take;
		if (!to_close) n -= take;
	}
	return true;
}

/* A chunked body from the input to the job: true when its last chunk came. */
static bool in_chunked(struct onyx_in *in, size_t *since_post)
{
	char line[256];

	for (;;) {
		size_t sz = 0;
		int digits = 0;
		if (in_line(in, line, sizeof line) < 0)
			return false;
		for (const char *p = line; ; p++) {
			int c = *p, v = c >= '0' && c <= '9' ? c - '0' :
				c >= 'a' && c <= 'f' ? c - 'a' + 10 :
				c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
			if (v < 0) break;
			sz = sz * 16 + (size_t) v;
			digits++;
		}
		if (digits == 0)
			return false;
		if (sz == 0) {
			/* the trailer, to its blank line */
			int l;
			do {
				l = in_line(in, line, sizeof line);
			} while (l > 0);
			return l == 0;
		}
		if (!in_body(in, sz, since_post))
			return false;
		if (in_line(in, line, sizeof line) != 0)	/* the chunk's CRLF */
			return false;
	}
}

/*
 * One request on connection k and its response read. The head goes to the job; the body
 * to it as it comes. Returns 1 done (*keep: the connection may carry another request), 0 the
 * connection gave nothing back (a pooled one the server closed: try a new one), -1 failed
 * (j->err set).
 */
static int onyx_exchange(struct onyx_job *j, struct onyx_conn *k, const char *req, int reqlen,
		bool *keep)
{
	struct onyx_in *in = malloc(sizeof *in);
	char line[8192], *head = NULL;
	size_t headlen = 0, headcap = 0, since_post = 0;
	long long clen = -1;
	bool chunked = false, close = false, http10 = false, ok;
	int code = 0, l;

	*keep = false;
	if (in == NULL) {
		j->err = "Out of memory";
		return -1;
	}
	in->k = k;
	in->j = j;
	in->pos = in->len = 0;
	in->closed = false;
	in->t_last = kapi_get_ticks();
	if (conn_send(k, req, reqlen) < 0) {
		free(in);
		return 0;
	}

	/* the head (a 1xx interim one skipped) */
	for (;;) {
		headlen = 0;
		l = in_line(in, line, sizeof line);
		if (l < 0) {
			bool nothing = in->len == 0 && in->closed;
			free(in);
			free(head);
			if (nothing)
				return 0;
			j->err = j->cancel ? "Aborted" : "Connection closed";
			return -1;
		}
		code = head_status(line, (size_t) l);
		http10 = strncmp(line, "HTTP/1.0", 8) == 0;
		for (;;) {
			size_t need = headlen + (size_t) l + 3;
			if (need > headcap) {
				size_t cap = headcap ? headcap * 2 : 4096;
				char *h;
				while (cap < need) cap *= 2;
				h = realloc(head, cap);
				if (h == NULL) { free(in); free(head); j->err = "Out of memory"; return -1; }
				head = h;
				headcap = cap;
			}
			memcpy(head + headlen, line, (size_t) l);
			headlen += (size_t) l;
			head[headlen++] = '\r';
			head[headlen++] = '\n';
			l = in_line(in, line, sizeof line);
			if (l < 0) { free(in); free(head); j->err = "Connection closed"; return -1; }
			if (l == 0) break;		/* the blank line */
		}
		if (code >= 200 || code == 101)
			break;
	}
	headlen -= 2;		/* (no final CRLF: the head's lines) */
	head[headlen] = '\0';
	{
		char v[64];
		if (header_value(head, headlen, "Content-Length", v, sizeof v))
			clen = strtoll(v, NULL, 10);
		if (header_value(head, headlen, "Transfer-Encoding", v, sizeof v) &&
		    strstr(v, "chunked") != NULL)
			chunked = true;
		if (header_value(head, headlen, "Connection", v, sizeof v)) {
			if (ci_has(v, "close")) close = true;
			if (ci_has(v, "keep-alive")) http10 = false;
		}
	}
	kapi_lock(&j->lk);
	j->head = head;
	j->headlen = headlen;
	kapi_unlock(&j->lk);
	onyx_post_wake();

	/* the body: none, its chunks, its length, or to the close */
	if (strcmp(j->method, "HEAD") == 0 || code == 204 || code == 304 || code < 200) {
		ok = true;
	} else if (chunked) {
		ok = in_chunked(in, &since_post);
	} else if (clen >= 0) {
		ok = in_body(in, (size_t) clen, &since_post);
	} else {
		ok = in_body(in, (size_t) -1, &since_post);
		close = true;
	}
	if (!ok && j->err == NULL)
		j->err = j->cancel ? "Aborted" : "Connection closed";
	*keep = ok && !close && !http10 && !in->closed && in->pos == in->len && !j->cancel;
	free(in);
	return ok ? 1 : -1;
}

/* The download thread: a connection (kept or new), the request, the response -- blocking,
 * in its own thread. It touches its job only; the UI thread delivers (fetch_onyx_poll). */
static int fetch_onyx_worker(void *arg)
{
	struct onyx_job *j = arg;
	char host[256], *req;
	const char *path;
	unsigned port;
	bool tls = strncasecmp(j->url, "https:", 6) == 0, keep = false;
	int len, orphan, r = -1, attempt;
	struct onyx_conn k;

	if (!onyx_split_url(j->url, host, sizeof host, &port, &path, tls ? 443 : 80)) {
		j->err = "Malformed URL";
		goto done;
	}
	req = onyx_request(j->method, path, host, port, tls, j->ua, j->lang, j->hdrs, j->body,
			true, &len);
	if (req == NULL) {
		j->err = "Request too large";
		goto done;
	}
	for (attempt = 0; attempt < 3 && !j->cancel; attempt++) {
		bool pooled = attempt < 2 && pool_take(host, port, tls, &k);
		if (!pooled && !conn_open(&k, host, port, tls)) {
			conn_close(&k);
			j->err = "Connection failed";
			r = -1;
			break;
		}
		k.uses++;
		r = onyx_exchange(j, &k, req, len, &keep);
		if (r == 0 && pooled) {		/* the server closed it while idle: again */
			conn_close(&k);
			continue;
		}
		if (r == 0)
			j->err = "Empty response";
		if (r == 1 && keep)
			pool_put(&k);
		else
			conn_close(&k);
		break;
	}
	free(req);
done:
	kapi_lock(&j->lk);
	j->state = JOB_DONE;
	orphan = j->orphan;
	kapi_unlock(&j->lk);
	if (orphan) onyx_job_free(j);			/* its fetch was aborted and freed */
	else onyx_post_wake();
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
	j->ua = user_agent_string();
	j->lang = nsoption_charp(accept_language) != NULL ? nsoption_charp(accept_language) :
		"fr-FR,fr;q=0.9,en-US;q=0.8,en;q=0.7";
	if (j->url == NULL || j->method == NULL) { onyx_job_free(j); return false; }
	if (kapi_thread_create(fetch_onyx_worker, j, 0, "fetch") < 0) {
		onyx_job_free(j);
		return false;
	}
	c->job = j;
	onyx_workers++;
	return true;
}

/* The UI thread: the fetch goes (aborted, freed, redirected) while its job may still run. */
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

/* The UI thread: body bytes to the core, inflated when the response is encoded. */
static void onyx_data(struct fetch_onyx_context *c, const uint8_t *b, size_t n, bool last)
{
	fetch_msg msg;

	if (!c->zs_on) {
		if (n > 0 && !c->aborted) {
			msg.type = FETCH_DATA;
			msg.data.header_or_data.buf = b;
			msg.data.header_or_data.len = n;
			fetch_onyx_send(&msg, c);
			c->delivered += n;
		}
		return;
	}
	c->zs.next_in = (Bytef *) b;
	c->zs.avail_in = (uInt) n;
	while (!c->zs_end && !c->aborted && (c->zs.avail_in > 0 || last)) {
		uint8_t out[32768];
		int ret;
		c->zs.next_out = out;
		c->zs.avail_out = sizeof out;
		ret = inflate(&c->zs, Z_NO_FLUSH);
		if (ret == Z_STREAM_END)
			c->zs_end = true;
		else if (ret != Z_OK && ret != Z_BUF_ERROR)
			c->zs_end = true;	/* (corrupt: what came is kept) */
		if (sizeof out - c->zs.avail_out > 0) {
			msg.type = FETCH_DATA;
			msg.data.header_or_data.buf = out;
			msg.data.header_or_data.len = sizeof out - c->zs.avail_out;
			fetch_onyx_send(&msg, c);
			c->delivered += msg.data.header_or_data.len;
		} else if (ret == Z_BUF_ERROR || c->zs.avail_in == 0) {
			break;
		}
	}
}

/* The UI thread, every poll: a threaded fetch's progress to the core. True: finished. */
static bool fetch_onyx_step_threaded(struct fetch_onyx_context *c)
{
	struct onyx_job *j = c->job;
	char *head;
	size_t headlen, n;
	uint8_t *b;
	bool done;
	fetch_msg msg;

	if (j == NULL) {
		if (onyx_workers >= ONYX_MAX_WORKERS) return false;	/* (a later poll) */
		if (!onyx_job_start(c)) c->nothread = true;		/* the state machine */
		return false;
	}

	kapi_lock(&j->lk);
	head = c->head_done ? NULL : j->head;
	headlen = j->headlen;
	b = j->buf;
	n = j->len;
	done = j->state == JOB_DONE;
	if (head != NULL) j->head = NULL;
	if (c->head_done || head != NULL) {	/* (the body only after its head) */
		j->buf = NULL;
		j->len = j->cap = 0;
	} else {
		b = NULL;
		n = 0;
	}
	kapi_unlock(&j->lk);

	if (head != NULL) {
		char cenc[64];
		c->head_done = true;
		if (!onyx_head(c, head, headlen, cenc, sizeof cenc)) {
			c->redirected = true;
		} else if (cenc[0] != '\0' && (strstr(cenc, "gzip") != NULL ||
				strstr(cenc, "deflate") != NULL)) {
			memset(&c->zs, 0, sizeof c->zs);
			if (inflateInit2(&c->zs, strstr(cenc, "gzip") != NULL ?
					16 + MAX_WBITS : MAX_WBITS) == Z_OK)
				c->zs_on = true;
		}
		free(head);
	}
	if (c->redirected || c->aborted) {
		free(b);
		onyx_job_drop(c);		/* (a redirect's body: the worker drains or drops it) */
		return true;
	}
	if (b != NULL) {
		onyx_data(c, b, n, false);
		free(b);
	}
	if (!done)
		return false;

	kapi__dmb();
	if (!c->head_done) {
		fetch_onyx_error(c, j->err != NULL ? j->err : "Empty response");
	} else if (!c->aborted) {
		if (c->zs_on)
			onyx_data(c, NULL, 0, true);
		if (!c->aborted) {
			msg.type = FETCH_FINISHED;
			fetch_onyx_send(&msg, c);
		}
	}
	onyx_job_drop(c);
	return true;
}

static void fetch_onyx_poll(lwc_string *scheme)
{
	struct fetch_onyx_context *active = NULL;	/* fetches still running after this round */
	struct fetch_onyx_context *c;
	bool did_connect = false;			/* at most one blocking connect per poll */
	(void)scheme;

	/* Drain the ring, advance each fetch, then re-queue the ones that aren't finished. */
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
		if (!c->nothread && c->phase == PH_INIT && onyx_threads_ok()) {
			if (fetch_onyx_step_threaded(c)) {	/* delivered (or errored) */
				fetch_remove_from_queues(c->parent_fetch);
				fetch_free(c->parent_fetch);
				continue;
			}
			RING_INSERT(active, c);		/* its thread still downloads */
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
