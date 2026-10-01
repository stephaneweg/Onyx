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
#include <time.h>		/* (Onyx) time: the HTTP/1.1-only origins' day */

#include <zlib.h>
#include <brotli/decode.h>	/* Onyx: Content-Encoding: br */
#include <zstd.h>		/* Onyx: Content-Encoding: zstd */
#include <nghttp2/nghttp2.h>	/* Onyx: HTTP/2 */
#include <libwapcaplet/libwapcaplet.h>

#include "utils/nsurl.h"
#include "utils/nsoption.h"
#include "utils/useragent.h"
#include "utils/corestrings.h"
#include "utils/ring.h"
#include "utils/log.h"
#include "utils/messages.h"

#include "content/fetch.h"
#include "content/fetchers.h"
#include "content/urldb.h"
#include "netsurf/ssl_certs.h"
#include "netsurf/misc.h"		/* Onyx: the net:minute timer (guit->misc->schedule) */
#include "desktop/gui_internal.h"

#include "kapi.h"		/* Onyx TCP transport */
#include "onyx_nstls.h"		/* C-callable TLS transport (https) */
#include "onyx_ws.h"		/* Onyx: onyx_ws_shutdown (the app's end) */
#include "onyx_chrome.h"	/* Onyx: onyx_chrome_view_state (the window hidden) */
#include "netsurf/onyx_perf.h"	/* Onyx: NS_PERF timings (net:connect, net:done) */

void onyx_cache_fetched_as(nsurl *url);	/* Onyx: onyx_cache.c (the disk cache's key) */

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
	/* the Content-Encoding's decoder (Onyx: gzip / deflate with zlib, br with Brotli's,
	 * zstd with Zstandard's), streamed: the body is decoded as it comes */
	int enc;			/* ENC_NONE, ENC_ZLIB, ENC_BR, ENC_ZSTD */
	z_stream zs;
	BrotliDecoderState *br;
	ZSTD_DStream *zd;
	bool dec_end;			/* the encoded stream ended (or was corrupt) */
	size_t delivered;		/* body bytes handed to the core */
	bool script;			/* Onyx: a script's request (fetch / XHR: X-Onyx-Dest: empty) */
	bool insecure;			/* Onyx: the user accepted this host's bad certificate */
	bool no_cookies;		/* Onyx: a script's request without credentials (CORS):
					 * no Cookie sent, its Set-Cookie ignored */
	int retries;			/* Onyx: its job started again (HTTP/2: JOB_RETRY) */
	int status;			/* Onyx: the response's status (the perf log) */
	bool not_modified;		/* Onyx: a 304 -- FETCH_NOTMODIFIED given */
};

static struct fetch_onyx_context *ring = NULL;

/* ---- Onyx (docs/06 §41): what the page does on the network, a minute at a time -----------
 * With the perf log on (SD:/apps/jet.app/perf, NS_PERF), a line a minute while anything
 * happened: "ONYX-PERF net:minute ..." -- the requests ended (the page's, the scripts'; failed,
 * retried), the bytes in and out, the connections made and failed, the sockets open now (the
 * downloads', kept alive, HTTP/2), the WebSockets and EventSources (their bytes, connections,
 * throttled ones) and the hosts the most requests went to. The UI thread's (the threads add
 * their connects with atomics). */
#define ONYX_NT_HOSTS	12
static struct {
	unsigned req, script, fail, retry;
	unsigned long long in, out;
	unsigned t0;				/* kapi_get_ticks at the period's start */
	struct { char host[64]; unsigned req; unsigned long long in; } host[ONYX_NT_HOSTS];
} onyx_nt;
static unsigned onyx_nt_conn, onyx_nt_conn_fail, onyx_nt_backoff;	/* (atomics) */

static void onyx_nt_request(const char *host, bool script, bool failed, bool retried,
		unsigned long long in, unsigned long long out)
{
	int i, low = 0;

	if (!onyx_perf_on())
		return;
	onyx_nt.req++;
	onyx_nt.script += script;
	onyx_nt.fail += failed;
	onyx_nt.retry += retried;
	onyx_nt.in += in;
	onyx_nt.out += out;
	if (host == NULL)
		return;
	for (i = 0; i < ONYX_NT_HOSTS; i++) {
		if (strcasecmp(onyx_nt.host[i].host, host) == 0)
			break;
		if (onyx_nt.host[i].req < onyx_nt.host[low].req)
			low = i;
	}
	if (i == ONYX_NT_HOSTS) {	/* (a new host takes the least used place) */
		i = low;
		snprintf(onyx_nt.host[i].host, sizeof onyx_nt.host[i].host, "%s", host);
		onyx_nt.host[i].req = 0;
		onyx_nt.host[i].in = 0;
	}
	onyx_nt.host[i].req++;
	onyx_nt.host[i].in += in;
}

static void onyx_nt_sockets(int *workers, int *pool, int *h2, char *list, size_t cap);

/* the minute's line, when a minute went by (called from the poll and the timer below) */
static void onyx_nt_tick(bool force)
{
	unsigned now = kapi_get_ticks(), secs, ws_open, sse_open, ws_opens, ws_thr;
	unsigned long long ws_in, ws_out;
	unsigned conn, conn_fail, backoff;
	int workers = 0, pool = 0, h2 = 0, i, k, order[ONYX_NT_HOSTS], n = 0;
	char hosts[512], socks[512], wss[512];
	size_t hl = 0;

	if (!onyx_perf_on())
		return;
	if (onyx_nt.t0 == 0)
		onyx_nt.t0 = now;
	if (!force && now - onyx_nt.t0 < 6000)
		return;
	secs = (now - onyx_nt.t0) / 100;
	if (secs == 0)
		secs = 1;
	onyx_ws_tally(&ws_open, &sse_open, &ws_in, &ws_out, &ws_opens, &ws_thr, wss, sizeof wss);
	conn = __atomic_exchange_n(&onyx_nt_conn, 0, __ATOMIC_RELAXED);
	conn_fail = __atomic_exchange_n(&onyx_nt_conn_fail, 0, __ATOMIC_RELAXED);
	backoff = __atomic_exchange_n(&onyx_nt_backoff, 0, __ATOMIC_RELAXED);
	onyx_nt_sockets(&workers, &pool, &h2, socks, sizeof socks);
	/* the busiest hosts first (at most 5) */
	for (i = 0; i < ONYX_NT_HOSTS; i++)
		if (onyx_nt.host[i].req > 0)
			order[n++] = i;
	for (i = 1; i < n; i++)
		for (k = i; k > 0 && onyx_nt.host[order[k]].req > onyx_nt.host[order[k - 1]].req; k--) {
			int t = order[k];
			order[k] = order[k - 1];
			order[k - 1] = t;
		}
	hosts[0] = '\0';
	for (i = 0; i < n && i < 5 && hl < sizeof hosts - 100; i++)
		hl += (size_t) snprintf(hosts + hl, sizeof hosts - hl, "%s%s %u req %llu KB",
				i ? ", " : "", onyx_nt.host[order[i]].host,
				onyx_nt.host[order[i]].req, onyx_nt.host[order[i]].in / 1024);
	if (onyx_nt.req == 0 && conn == 0 && ws_in + ws_out == 0 && ws_opens == 0 &&
	    ws_open + sse_open == 0 && workers == 0 && h2 == 0 && pool == 0) {
		onyx_nt.t0 = now;	/* (a quiet minute: no line) */
		return;
	}
	fprintf(stderr, "ONYX-PERF net:minute %u s: %u requests (%u by scripts, %u failed, %u retried), "
			"%llu KB in (%llu KB/s), %llu KB out; %u connects (%u failed, %u held back); "
			"sockets: %d downloads, %d kept alive, %d HTTP/2 [%s]; %u WebSockets + %u "
			"EventSources open [%s] (%llu KB in, %llu KB out, %u connects, %u throttled); "
			"requests by host: %s\n",
			secs, onyx_nt.req, onyx_nt.script, onyx_nt.fail, onyx_nt.retry,
			onyx_nt.in / 1024, onyx_nt.in / 1024 / secs, onyx_nt.out / 1024,
			conn, conn_fail, backoff, workers, pool, h2, socks[0] ? socks : "-",
			ws_open, sse_open, wss[0] ? wss : "-", ws_in / 1024, ws_out / 1024, ws_opens,
			ws_thr, hl ? hosts : "-");
	memset(&onyx_nt, 0, sizeof onyx_nt);
	onyx_nt.t0 = now;
}

static void onyx_nt_timer(void *p);	/* (below) */

/* Onyx: the bench's NS_NETDEBUG=1 -- each response's head and each HTTP/2 frame on stderr */
static bool onyx_netdebug(void)
{
	static int v = -1;
	if (v < 0)
		v = getenv("NS_NETDEBUG") != NULL;
	return v != 0;
}

/* Onyx: the app is ending (onyx_fetch_shutdown); the download / connection threads alive */
static volatile int onyx_quit;
static volatile int onyx_threads;

/* Idle timeout of a download: no byte for 30 s (kapi_get_ticks counts at 100 Hz). */
#define ONYX_IDLE_TICKS	3000
/* Onyx: a script's request (fetch / XHR): 5 min (a long poll, a stream) */
#define ONYX_SCRIPT_IDLE_TICKS	30000

/* Onyx: the kernel has 16 TCP sockets in all (kernel/sys/net.cpp MAX_SOCKETS), for every app:
 * NetSurf keeps to about 12 of them -- 6 downloads at once, 4 connections kept alive, 4
 * HTTP/2 connections (their streams need no socket of their own), the WebSockets aside. A
 * connect that finds the table full closes the idle connections and waits for a socket
 * (onyx_connect); the app's end closes them all (onyx_fetch_shutdown). */
#define ONYX_MAX_WORKERS	6	/* downloads at once, each its own thread and socket */

/* Onyx: what the requests accept, as Chrome: every coding the fetcher decodes */
#define ONYX_ACCEPT_ENCODING "gzip, deflate, br, zstd"

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

/* Onyx: a request header's place in Chrome's order (its HTTP/1.1 and HTTP/2 requests: some
 * bot checks look at it) -- name: n bytes, any case. Unknown ones go after Accept. */
static int onyx_hdr_rank(const char *name, size_t n)
{
	static const struct { const char *n; int r; } ranks[] = {
		{ "host", 0 }, { "connection", 1 }, { "content-length", 2 }, { "pragma", 3 },
		{ "cache-control", 4 }, { "sec-ch-ua", 5 }, { "sec-ch-ua-mobile", 5 },
		{ "sec-ch-ua-full-version", 5 }, { "sec-ch-ua-arch", 5 },
		{ "sec-ch-ua-full-version-list", 5 }, { "sec-ch-ua-bitness", 5 },
		{ "sec-ch-ua-model", 5 }, { "sec-ch-ua-platform", 6 },
		{ "sec-ch-ua-platform-version", 6 }, { "sec-ch-ua-wow64", 6 },
		{ "sec-ch-ua-form-factors", 6 }, { "origin", 7 }, { "content-type", 8 },
		{ "upgrade-insecure-requests", 9 }, { "user-agent", 10 }, { "accept", 11 },
		{ "sec-fetch-site", 13 }, { "sec-fetch-mode", 14 }, { "sec-fetch-user", 15 },
		{ "sec-fetch-dest", 16 }, { "referer", 17 }, { "accept-encoding", 18 },
		{ "accept-language", 19 }, { "cookie", 20 }, { "if-none-match", 21 },
		{ "if-modified-since", 22 }, { "priority", 23 },
	};
	size_t i;
	for (i = 0; i < sizeof ranks / sizeof ranks[0]; i++)
		if (strlen(ranks[i].n) == n && strncasecmp(ranks[i].n, name, n) == 0)
			return ranks[i].r;
	return 12;
}

/* Onyx: header lines ("Name: value\r\n"...) put in Chrome's order in place (a stable sort) */
static void onyx_order_headers(char *block)
{
	char *line[96], *copy, *o;
	int rank[96], n = 0, i, j;
	size_t len = strlen(block);
	char *p = block;

	while (*p != '\0' && n < 96) {
		char *eol = strchr(p, '\n'), *colon = strchr(p, ':');
		line[n] = p;
		rank[n] = colon != NULL && (eol == NULL || colon < eol) ?
			onyx_hdr_rank(p, (size_t) (colon - p)) : 12;
		n++;
		if (eol == NULL)
			break;
		p = eol + 1;
	}
	if (*p != '\0' && n == 96)
		return;			/* (too many: as they are) */
	copy = malloc(len + 1);
	if (copy == NULL)
		return;
	o = copy;
	for (j = 0; j <= 23; j++)
		for (i = 0; i < n; i++) {
			if (rank[i] != j)
				continue;
			{
				char *eol = strchr(line[i], '\n');
				size_t l = eol != NULL ? (size_t) (eol - line[i]) + 1 : strlen(line[i]);
				memcpy(o, line[i], l);
				o += l;
			}
		}
	*o = '\0';
	if ((size_t) (o - copy) == len)
		memcpy(block, copy, len + 1);
	free(copy);
}

/* The request: "METHOD path HTTP/1.1", our headers, the caller's, the body. malloc'd; *len its
 * length (the body may hold any byte but NUL). The path stops at a fragment. Onyx: the
 * headers in Chrome's order and casing (Host, Connection first; the client hints' names in
 * lower case). */
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
	{
		int rl = snprintf(r, cap, "%s %.*s HTTP/1.1\r\n", method, (int) plen, path), hl;
		if (rl <= 0 || (size_t) rl >= cap) {
			free(r);
			return NULL;
		}
		hl = snprintf(r + rl, cap - (size_t) rl,
			"Host: %s\r\nUser-Agent: %s\r\n%s%s%s%s"
			"Accept-Encoding: " ONYX_ACCEPT_ENCODING "\r\nConnection: %s\r\n%s%s",
			hostport, ua,
			has_accept ? "" : "Accept: */*\r\n",
			has_lang ? "" : "Accept-Language: ", has_lang ? "" : lang,
			has_lang ? "" : "\r\n",
			keepalive ? "keep-alive" : "close",
			hdrs != NULL ? hdrs : "", extra);
		if (hl <= 0 || (size_t) (rl + hl) + 2 >= cap) {
			free(r);
			return NULL;
		}
		onyx_order_headers(r + rl);		/* (Onyx: Chrome's order) */
		memcpy(r + rl + hl, "\r\n", 3);
		n = rl + hl + 2;
	}
	if ((size_t) n + blen >= cap) {
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

/* ---- the Content-Encoding (Onyx: gzip, deflate, br, zstd) ------------------------- */

enum { ENC_NONE = 0, ENC_ZLIB, ENC_BR, ENC_ZSTD };


/* The decoder of a response's Content-Encoding (its value in cenc), set up on the context:
 * false when it is none the fetcher knows (the body goes as it is). */
static bool onyx_decoder_start(struct fetch_onyx_context *c, const char *cenc)
{
	c->enc = ENC_NONE;
	c->dec_end = false;
	if (cenc == NULL || cenc[0] == '\0')
		return false;
	if (ci_has(cenc, "gzip") || ci_has(cenc, "deflate")) {
		memset(&c->zs, 0, sizeof c->zs);
		if (inflateInit2(&c->zs, ci_has(cenc, "gzip") ? 16 + MAX_WBITS : MAX_WBITS) != Z_OK)
			return false;
		c->enc = ENC_ZLIB;
	} else if (ci_has(cenc, "br")) {
		c->br = BrotliDecoderCreateInstance(NULL, NULL, NULL);
		if (c->br == NULL)
			return false;
		c->enc = ENC_BR;
	} else if (ci_has(cenc, "zstd")) {
		c->zd = ZSTD_createDStream();
		if (c->zd == NULL)
			return false;
		/* (a window of 8 MB at most, as Chrome's: a bigger one is an error) */
		ZSTD_DCtx_setParameter(c->zd, ZSTD_d_windowLogMax, 23);
		c->enc = ENC_ZSTD;
	}
	return c->enc != ENC_NONE;
}

static void onyx_decoder_end(struct fetch_onyx_context *c)
{
	if (c->enc == ENC_ZLIB)
		inflateEnd(&c->zs);
	if (c->br != NULL)
		BrotliDecoderDestroyInstance(c->br);
	if (c->zd != NULL)
		ZSTD_freeDStream(c->zd);
	c->br = NULL;
	c->zd = NULL;
	c->enc = ENC_NONE;
}

/* One step of the decoder: in -> out; *used the input taken, *made the output written.
 * Returns 1 more output may come from this input (call again), 0 the input is used up (or
 * the stream ended: c->dec_end). */
static int onyx_decode(struct fetch_onyx_context *c, const uint8_t *in, size_t n, size_t *used,
		uint8_t *out, size_t cap, size_t *made, bool last)
{
	*used = *made = 0;
	if (c->dec_end)
		return 0;
	if (c->enc == ENC_ZLIB) {
		int ret;
		c->zs.next_in = (Bytef *) in;
		c->zs.avail_in = (uInt) n;
		c->zs.next_out = out;
		c->zs.avail_out = (uInt) cap;
		ret = inflate(&c->zs, Z_NO_FLUSH);
		*used = n - c->zs.avail_in;
		*made = cap - c->zs.avail_out;
		if (ret == Z_STREAM_END || (ret != Z_OK && ret != Z_BUF_ERROR))
			c->dec_end = true;	/* (corrupt: what came is kept) */
		return !c->dec_end && c->zs.avail_out == 0 ? 1 : 0;
	}
	if (c->enc == ENC_BR) {
		size_t ain = n, aout = cap;
		const uint8_t *nin = in;
		uint8_t *nout = out;
		BrotliDecoderResult r = BrotliDecoderDecompressStream(c->br, &ain, &nin, &aout,
				&nout, NULL);
		*used = n - ain;
		*made = cap - aout;
		if (r == BROTLI_DECODER_RESULT_SUCCESS || r == BROTLI_DECODER_RESULT_ERROR)
			c->dec_end = true;
		return r == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT ? 1 : 0;
	}
	if (c->enc == ENC_ZSTD) {
		ZSTD_inBuffer ib = { in, n, 0 };
		ZSTD_outBuffer ob = { out, cap, 0 };
		size_t r = ZSTD_decompressStream(c->zd, &ob, &ib);
		*used = ib.pos;
		*made = ob.pos;
		if (ZSTD_isError(r))
			c->dec_end = true;
		/* (r == 0: a frame ended -- another may follow in the same body) */
		return !c->dec_end && (ob.pos == ob.size || ib.pos < ib.size) ? 1 : 0;
	}
	(void) last;
	return 0;
}

/* ---- transport helpers (plaintext or TLS, same non-blocking convention) ---- */
static void onyx_conn_close(struct fetch_onyx_context *c)
{
	if (c->tls) { if (c->ts) onyx_nstls_close(c->ts); c->ts = NULL; }
	else        { if (c->sock >= 0) kapi_tcp_close(c->sock); c->sock = -1; }
}

/* ---- fetcher operations ----------------------------------------------- */
static void onyx_state_load(void);

static bool fetch_onyx_initialise(lwc_string *scheme)
{
	static bool ca_read;

	NSLOG(netsurf, INFO, "onyx fetcher init: %s", lwc_string_data(scheme));
	/* Onyx: the trusted roots for the certificate check (Choices' ca_bundle, else the card's
	 * SD:/res/ca-bundle) -- read here, on the UI thread, once */
	if (!ca_read) {
		char path[512];
		const char *rp = NETSURF_FB_RESPATH;
		ca_read = true;
		if (nsoption_charp(ca_bundle) != NULL && nsoption_charp(ca_bundle)[0] != '\0')
			snprintf(path, sizeof path, "%s", nsoption_charp(ca_bundle));
		else
			snprintf(path, sizeof path, "%s%sca-bundle", rp,
					rp[0] != '\0' && rp[strlen(rp) - 1] == '/' ? "" : "/");
		onyx_nstls_ca_bundle(path);
		onyx_nstls_cancel_flag(&onyx_quit);	/* (the app's end: onyx_fetch_shutdown) */
		onyx_state_load();
		/* (Onyx, docs/06 §41: the kept connections' sweep, net:minute) */
		guit->misc->schedule(10000, onyx_nt_timer, NULL);
	}
	return true;
}

static void onyx_fetch_shutdown(void);

static void fetch_onyx_finalise(lwc_string *scheme)
{
	(void)scheme;
	onyx_fetch_shutdown();		/* (Onyx: once, at the first scheme) */
}

static bool fetch_onyx_can_fetch(const nsurl *url)
{
	(void)url;
	return true;
}

/* Onyx: two hosts on the same site -- the same registrable domain, approximated: the last
 * two labels, or three under a two-letter country code's second level (co.uk, com.au...) */
static const char *onyx_site_of(const char *host)
{
	const char *p = host + strlen(host), *dots[3] = { NULL, NULL, NULL };
	int n = 0;

	while (p > host && n < 3) {
		p--;
		if (*p == '.')
			dots[n++] = p;
	}
	if (n < 1)
		return host;
	/* dots[0]: before the TLD, dots[1]: before the second level */
	if (n >= 2 && strlen(dots[0] + 1) == 2 && (dots[0] - dots[1] - 1) <= 3) {
		/* a ccTLD with a short second level: example.co.uk */
		return n >= 3 ? dots[2] + 1 : host;
	}
	return n >= 2 ? dots[1] + 1 : host;
}

/* ---- Onyx: the User-Agent client hints as Chrome sends them ------------------------------
 * sec-ch-ua: its brand list with Chrome's GREASE brand -- the name, version and order drawn
 * from the major version as Chromium does (user_agent_utils.cc, GenerateBrandVersionList);
 * a hand-made list is a bot's mark. The high-entropy hints (the full version list, the
 * architecture, the platform's version...) only to the origins that asked for them by
 * Accept-CH (Google does), as Chrome: remembered per origin for the app's life. */
static int onyx_ua_brands(char *b, size_t n, const char *major, const char *full)
{
	static const char chars[] = " (:-./);=?_";
	static const char *const gver[] = { "8", "99", "24" };
	static const int order[6][3] = { {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1},
		{2, 1, 0} };
	int seed = atoi(major);
	const int *o = order[seed % 6];
	char e[3][80];

	snprintf(e[o[0]], sizeof e[0], "\"Not%cA%cBrand\";v=\"%s%s\"", chars[seed % 11],
			chars[(seed + 1) % 11], gver[seed % 3], full != NULL ? ".0.0.0" : "");
	snprintf(e[o[1]], sizeof e[1], "\"Chromium\";v=\"%s\"", full != NULL ? full : major);
	snprintf(e[o[2]], sizeof e[2], "\"Google Chrome\";v=\"%s\"", full != NULL ? full : major);
	return snprintf(b, n, "%s, %s, %s", e[0], e[1], e[2]);
}

#define ONYX_CH_ORIGINS	32
enum { CH_FULL_LIST = 1, CH_FULL = 2, CH_ARCH = 4, CH_BITNESS = 8, CH_MODEL = 16,
	CH_PLATFORM_VERSION = 32, CH_WOW64 = 64, CH_FORM_FACTORS = 128 };
static struct { char host[128]; unsigned hints; } onyx_ch[ONYX_CH_ORIGINS];
static unsigned onyx_ch_next;

/* (the UI thread) a response's Accept-CH: the hints its origin wants from now on */
static void onyx_accept_ch(const char *host, const char *v)
{
	static const struct { const char *n; unsigned bit; } names[] = {
		{ "sec-ch-ua-full-version-list", CH_FULL_LIST }, { "sec-ch-ua-full-version", CH_FULL },
		{ "sec-ch-ua-arch", CH_ARCH }, { "sec-ch-ua-bitness", CH_BITNESS },
		{ "sec-ch-ua-model", CH_MODEL }, { "sec-ch-ua-platform-version", CH_PLATFORM_VERSION },
		{ "sec-ch-ua-wow64", CH_WOW64 }, { "sec-ch-ua-form-factors", CH_FORM_FACTORS },
	};
	unsigned hints = 0, i;
	int slot = -1;

	for (i = 0; i < sizeof names / sizeof names[0]; i++) {
		const char *p = v;
		size_t l = strlen(names[i].n);
		while ((p = strcasestr(p, names[i].n)) != NULL) {
			if (p[l] == '\0' || p[l] == ',' || p[l] == ' ') {
				hints |= names[i].bit;
				break;
			}
			p += l;
		}
	}
	for (i = 0; i < ONYX_CH_ORIGINS; i++)
		if (strcasecmp(onyx_ch[i].host, host) == 0)
			slot = (int) i;
	if (slot < 0) {
		if (hints == 0)
			return;
		slot = (int) (onyx_ch_next++ % ONYX_CH_ORIGINS);
		snprintf(onyx_ch[slot].host, sizeof onyx_ch[slot].host, "%s", host);
	}
	onyx_ch[slot].hints = hints;
}

static unsigned onyx_ch_hints(const char *host)
{
	unsigned i;
	for (i = 0; i < ONYX_CH_ORIGINS; i++)
		if (onyx_ch[i].host[0] != '\0' && strcasecmp(onyx_ch[i].host, host) == 0)
			return onyx_ch[i].hints;
	return 0;
}

/* the client hints of a request to host (https only), for its User-Agent */
static void onyx_add_client_hints(char **hdrs, const char *host, const char *ua)
{
	const char *cv = strstr(ua, "Chrome/"), *full = user_agent_chrome_full();
	char v[16] = "142", b[256];
	bool mobile = strstr(ua, "Mobile") != NULL, android = strstr(ua, "Android") != NULL;
	bool windows = strstr(ua, "Windows") != NULL;
	unsigned hints = host != NULL ? onyx_ch_hints(host) : 0;
	int k;

	if (cv == NULL)
		return;		/* (Onyx: not a Chrome -- NetSurf's own User-Agent: no client hints) */
	if (cv != NULL) {
		for (k = 0, cv += 7; k < 15 && cv[k] >= '0' && cv[k] <= '9'; k++)
			v[k] = cv[k];
		if (k > 0) v[k] = '\0';
	}
	if (strncmp(full, v, strlen(v)) != 0 || full[strlen(v)] != '.')
		full = NULL;		/* (a User-Agent of Choices' naming another Chrome) */
	k = onyx_ua_brands(b, sizeof b, v, NULL);
	hdrs_add(hdrs, "sec-ch-ua", b, (size_t) k);
	hdrs_add(hdrs, "sec-ch-ua-mobile", mobile ? "?1" : "?0", 2);
	{
		const char *pf = android ? "\"Android\"" : windows ? "\"Windows\"" :
			strstr(ua, "Mac OS") ? "\"macOS\"" : "\"Linux\"";
		hdrs_add(hdrs, "sec-ch-ua-platform", pf, strlen(pf));
	}
	if (hints & CH_ARCH)
		hdrs_add(hdrs, "sec-ch-ua-arch", android ? "\"\"" : "\"x86\"", android ? 2 : 5);
	if (hints & CH_BITNESS)
		hdrs_add(hdrs, "sec-ch-ua-bitness", "\"64\"", 4);
	if ((hints & CH_FULL) && full != NULL) {
		k = snprintf(b, sizeof b, "\"%s\"", full);
		hdrs_add(hdrs, "sec-ch-ua-full-version", b, (size_t) k);
	}
	if ((hints & CH_FULL_LIST) && full != NULL) {
		k = onyx_ua_brands(b, sizeof b, v, full);
		hdrs_add(hdrs, "sec-ch-ua-full-version-list", b, (size_t) k);
	}
	if (hints & CH_MODEL)
		hdrs_add(hdrs, "sec-ch-ua-model", android ? "\"K\"" : "\"\"", android ? 3 : 2);
	if (hints & CH_PLATFORM_VERSION) {
		const char *pv = android ? "\"10.0.0\"" : windows ? "\"19.0.0\"" : "\"6.12.0\"";
		hdrs_add(hdrs, "sec-ch-ua-platform-version", pv, strlen(pv));
	}
	if (hints & CH_WOW64)
		hdrs_add(hdrs, "sec-ch-ua-wow64", "?0", 2);
	if (hints & CH_FORM_FACTORS) {
		const char *ff = mobile ? "\"Mobile\"" : "\"Desktop\"";
		hdrs_add(hdrs, "sec-ch-ua-form-factors", ff, strlen(ff));
	}
}

/* Onyx: Chrome's Fetch Metadata request headers (Sec-Fetch-Site / -Mode / -Dest / -User,
 * Upgrade-Insecure-Requests), an Accept header by destination, and on https the User-Agent
 * client hints (sec-ch-ua...) -- m.facebook.com answers "Sorry, something went wrong" (a 400)
 * to a navigation without Sec-Fetch-Mode. `dest`: X-Onyx-Dest, set by the cache from what the
 * caller accepts ("document", "iframe", "style", "script", "image", "font", "empty"...). */
static void onyx_add_fetch_metadata(char **hdrs, nsurl *url, nsurl *ref, const char *dest,
		bool user, const char *smode)
{
	const char *site = "none", *mode = "no-cors", *accept = NULL;
	bool nav = false;
	lwc_string *uh, *us;

	if (dest[0] == '\0')
		dest = ref == NULL ? "document" : "empty";
	if (strcmp(dest, "document") == 0 || strcmp(dest, "iframe") == 0) {
		nav = true;
		mode = "navigate";
		accept = "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,"
			"image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7";
	} else if (strcmp(dest, "image") == 0) {
		accept = "image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8";
	} else if (strcmp(dest, "style") == 0) {
		accept = "text/css,*/*;q=0.1";
	} else if (strcmp(dest, "font") == 0 || strcmp(dest, "empty") == 0) {
		mode = "cors";
	}
	if (strcmp(dest, "object") == 0)
		dest = "empty";

	uh = nsurl_get_component(url, NSURL_HOST);
	us = nsurl_get_component(url, NSURL_SCHEME);
	if (ref != NULL && strncasecmp(nsurl_access(ref), "http", 4) == 0) {
		lwc_string *rh = nsurl_get_component(ref, NSURL_HOST);
		if (nsurl_compare(url, ref, NSURL_SCHEME | NSURL_HOST | NSURL_PORT))
			site = "same-origin";
		else if (uh != NULL && rh != NULL && strcasecmp(
				onyx_site_of(lwc_string_data(uh)),
				onyx_site_of(lwc_string_data(rh))) == 0)
			site = "same-site";
		else
			site = "cross-site";
		if (rh != NULL)
			lwc_string_unref(rh);
	}
	if (smode != NULL && smode[0] != '\0')
		mode = smode;		/* (a script's request: its fetch mode, X-Onyx-Mode) */
	if (accept != NULL && !hdrs_have(*hdrs, "Accept"))
		hdrs_add(hdrs, "Accept", accept, strlen(accept));
	if (nav)
		hdrs_add(hdrs, "Upgrade-Insecure-Requests", "1", 1);
	if (us != NULL && strcasecmp(lwc_string_data(us), "https") == 0) {
		/* the client hints a Chrome sends on https (Onyx: for the site's User-Agent --
		 * the version the toolbar's pill chose for the site) */
		lwc_string *hh = nsurl_get_component(url, NSURL_HOST);
		const char *h = hh != NULL ? lwc_string_data(hh) : NULL;
		onyx_add_client_hints(hdrs, h, user_agent_for_host(h));
		if (hh != NULL)
			lwc_string_unref(hh);
	}
	hdrs_add(hdrs, "Sec-Fetch-Site", site, strlen(site));
	hdrs_add(hdrs, "Sec-Fetch-Mode", mode, strlen(mode));
	if (nav && user)
		hdrs_add(hdrs, "Sec-Fetch-User", "?1", 2);
	hdrs_add(hdrs, "Sec-Fetch-Dest", dest, strlen(dest));
	if (uh != NULL)
		lwc_string_unref(uh);
	if (us != NULL)
		lwc_string_unref(us);
}

/* Onyx: the hosts whose bad certificate the user accepted, as the threads see them (urldb is
 * the UI thread's): onyx_ws.c's connections to them go on as the fetches do */
#define ONYX_INSECURE_MAX 16
static char onyx_insecure[ONYX_INSECURE_MAX][128];
static volatile int onyx_insecure_lk;

static void onyx_insecure_add(const char *host)
{
	int i, free_at = -1;

	kapi_lock(&onyx_insecure_lk);
	for (i = 0; i < ONYX_INSECURE_MAX; i++) {
		if (strcasecmp(onyx_insecure[i], host) == 0)
			break;
		if (onyx_insecure[i][0] == '\0' && free_at < 0)
			free_at = i;
	}
	if (i == ONYX_INSECURE_MAX && free_at >= 0)
		snprintf(onyx_insecure[free_at], sizeof onyx_insecure[0], "%s", host);
	kapi_unlock(&onyx_insecure_lk);
}

int onyx_fetch_insecure_host(const char *host)
{
	int i, r = 0;

	kapi_lock(&onyx_insecure_lk);
	for (i = 0; i < ONYX_INSECURE_MAX && !r; i++)
		r = onyx_insecure[i][0] != '\0' && strcasecmp(onyx_insecure[i], host) == 0;
	kapi_unlock(&onyx_insecure_lk);
	return r;
}

/* Onyx (docs/06 §38): the fetches made and ended so far (the status bar's "Loading... 12 of
 * 30": the UI thread's counts) */
static unsigned onyx_fetches_made, onyx_fetches_ended;

void onyx_fetch_counts(unsigned *made, unsigned *ended)
{
	*made = onyx_fetches_made;
	*ended = onyx_fetches_ended;
}

static void *fetch_onyx_setup(struct fetch *parent_fetch, nsurl *url,
		bool only_2xx, bool downgrade_tls, const char *post_urlenc,
		const struct fetch_multipart_data *post_multipart,
		const char **headers)
{
	struct fetch_onyx_context *ctx = calloc(1, sizeof(*ctx));
	size_t hlen = 0;
	char dest[16] = "", smode[16] = "";
	int i;
	(void)only_2xx; (void)downgrade_tls; (void)post_multipart;	/* (multipart: not yet) */
	if (ctx == NULL)
		return NULL;
	onyx_fetches_made++;
	ctx->parent_fetch = parent_fetch;
	ctx->url = nsurl_ref(url);
	onyx_cache_fetched_as(url);	/* (Onyx: the disk cache's key -- the site's version now) */

	/* the request: its method, its headers (Onyx: the conditional ones too -- llcache's
	 * revalidation of a stale object, a 304 answered with FETCH_NOTMODIFIED), its body */
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
		if (hdr_is(h, "X-Onyx-Dest")) {		/* (Onyx: Fetch Metadata) */
			const char *v = h + 12;
			while (*v == ' ') v++;
			snprintf(dest, sizeof dest, "%s", v);
			ctx->script = strcmp(dest, "empty") == 0;
			continue;
		}
		if (hdr_is(h, "X-Onyx-Mode")) {		/* (Onyx: a script's fetch mode) */
			const char *v = h + 12;
			while (*v == ' ') v++;
			snprintf(smode, sizeof smode, "%s", v);
			continue;
		}
		if (hdr_is(h, "X-Onyx-Credentials")) {	/* (Onyx: CORS, "omit") */
			ctx->no_cookies = true;
			continue;
		}
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
	if (!ctx->no_cookies && !hdrs_have(ctx->hdrs, "Cookie")) {
		char *ck = urldb_get_cookie(url, true);
		if (ck != NULL && ck[0] != '\0')
			hdrs_add(&ctx->hdrs, "Cookie", ck, strlen(ck));
		free(ck);
	}
	onyx_add_referer(&ctx->hdrs, url, fetch_get_referer(parent_fetch),
			ctx->method != NULL && strcmp(ctx->method, "GET") != 0 &&
			strcmp(ctx->method, "HEAD") != 0);
	onyx_add_fetch_metadata(&ctx->hdrs, url, fetch_get_referer(parent_fetch), dest,
			fetch_is_verifiable(parent_fetch), smode);

	/* Onyx: a host whose bad certificate the user accepted ("proceed" on the certificate
	 * error page: urldb, the UI thread's) is fetched without failing the check */
	ctx->insecure = strncasecmp(nsurl_access(url), "https:", 6) == 0 &&
		urldb_get_cert_permissions(url);
	if (ctx->insecure) {
		lwc_string *h = nsurl_get_component(url, NSURL_HOST);
		if (h != NULL) {
			onyx_insecure_add(lwc_string_data(h));
			lwc_string_unref(h);
		}
	}

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
	onyx_fetches_ended++;
	onyx_job_drop(c);		/* a download still in its thread: orphaned */
	onyx_conn_close(c);
	onyx_decoder_end(c);
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

/* Onyx: the certificates of the hosts connected to -- the chain each one's last checked
 * connection showed (its faults: a host the user accepted keeps them), for the toolbar's
 * padlock: a click shows the page's host's in the certificate viewer (about:certificate). Kept
 * for the app's life, the 24 hosts used last; a resumed session's lone certificate does not
 * replace a whole chain. The download threads write it, the UI reads it: a lock. */
#define ONYX_CHAINS 24
static struct onyx_kept_chain {
	char host[128];
	unsigned depth, used;
	struct { int err; unsigned char *der; unsigned long len; } cert[ONYX_TLS_CHAIN_MAX];
} onyx_chains[ONYX_CHAINS];
static unsigned onyx_chains_clock;
static volatile int onyx_chains_lk;

/* a connection's chain kept (then freed: the caller's) */
static void onyx_chain_keep(const char *host, struct onyx_tls_chain *ch)
{
	struct onyx_kept_chain *k = NULL;
	unsigned i, n = ch->depth < ONYX_TLS_CHAIN_MAX ? ch->depth : ONYX_TLS_CHAIN_MAX;

	if (n > 0 && ch->cert[0].der != NULL) {
		while (kapi__xchg(&onyx_chains_lk, 1) != 0)
			kapi_msleep(1);
		for (i = 0; i < ONYX_CHAINS && k == NULL; i++)
			if (strcasecmp(onyx_chains[i].host, host) == 0)
				k = &onyx_chains[i];
		if (k != NULL && n < k->depth) {
			k->used = ++onyx_chains_clock;		/* (a resumed session: kept as is) */
			k = NULL;
		} else if (k == NULL) {
			k = &onyx_chains[0];
			for (i = 1; i < ONYX_CHAINS; i++)		/* (the least recently used) */
				if (onyx_chains[i].used < k->used)
					k = &onyx_chains[i];
		}
		if (k != NULL) {
			for (i = 0; i < k->depth; i++)
				free(k->cert[i].der);
			snprintf(k->host, sizeof k->host, "%s", host);
			k->depth = n;
			k->used = ++onyx_chains_clock;
			for (i = 0; i < n; i++) {
				k->cert[i].err = ch->cert[i].err;
				k->cert[i].der = ch->cert[i].der;	/* (taken) */
				k->cert[i].len = ch->cert[i].len;
				ch->cert[i].der = NULL;
			}
		}
		kapi_unlock(&onyx_chains_lk);
	}
	onyx_nstls_chain_free(ch);
}

/* Onyx (the toolbar's padlock): the certificate viewer's address for a host's chain, or
 * NSERROR_NOT_FOUND when no checked connection to it is known */
nserror onyx_fetch_cert_url(const char *host, nsurl **url)
{
	struct cert_chain chain;
	nserror err = NSERROR_NOT_FOUND;
	unsigned i, j;

	if (host == NULL)
		return err;
	while (kapi__xchg(&onyx_chains_lk, 1) != 0)
		kapi_msleep(1);
	for (i = 0; i < ONYX_CHAINS; i++)
		if (onyx_chains[i].depth > 0 && strcasecmp(onyx_chains[i].host, host) == 0) {
			struct onyx_kept_chain *k = &onyx_chains[i];
			memset(&chain, 0, sizeof chain);
			chain.depth = k->depth < MAX_CERT_DEPTH ? k->depth : MAX_CERT_DEPTH;
			for (j = 0; j < chain.depth; j++) {
				int e = k->cert[j].err;
				chain.certs[j].err = e >= ONYX_CERT_OK && e <= ONYX_CERT_HOSTNAME_MISMATCH ?
					(ssl_cert_err) e : SSL_CERT_ERR_UNKNOWN;
				chain.certs[j].der = k->cert[j].der;
				chain.certs[j].der_length = k->cert[j].len;
			}
			err = cert_chain_to_query(&chain, url);
			break;
		}
	kapi_unlock(&onyx_chains_lk);
	return err;
}

/* Onyx: the server's certificate refused -- the chain the check built to the core
 * (FETCH_CERTS: the certificate error page lists it, about:certificate shows each), then
 * FETCH_CERT_ERR: a page's own fetch becomes that error page, with its "proceed anyway" (which
 * sets urldb's cert permission for the host: fetch_onyx_setup's insecure) */
static void onyx_cert_error(struct fetch_onyx_context *c, const struct onyx_tls_chain *oc)
{
	struct cert_chain chain;
	fetch_msg msg;
	unsigned i;

	memset(&chain, 0, sizeof chain);
	chain.depth = oc->depth < MAX_CERT_DEPTH ? oc->depth : MAX_CERT_DEPTH;
	for (i = 0; i < chain.depth; i++) {
		int e = oc->cert[i].err;
		chain.certs[i].err = e >= ONYX_CERT_OK && e <= ONYX_CERT_HOSTNAME_MISMATCH ?
			(ssl_cert_err) e : SSL_CERT_ERR_UNKNOWN;	/* (the same values) */
		chain.certs[i].der = oc->cert[i].der;
		chain.certs[i].der_length = oc->cert[i].len;
		if (chain.certs[i].der == NULL)
			chain.certs[i].err = SSL_CERT_ERR_CERT_MISSING;
		printf("ONYX-TLS refused %s: certificate %u/%u: %s\n", nsurl_access(c->url),
				i, (unsigned) chain.depth, chain.certs[i].err == SSL_CERT_ERR_OK ?
				"ok" : messages_get_sslcode(chain.certs[i].err));
	}
	fflush(stdout);
	if (chain.depth > 0 && !c->aborted) {
		msg.type = FETCH_CERTS;
		msg.data.chain = &chain;
		fetch_onyx_send(&msg, c);
	}
	if (!c->aborted) {
		msg.type = FETCH_CERT_ERR;
		fetch_onyx_send(&msg, c);
	}
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
	/* (Onyx: the cache-control headers go to the core now -- Cache-Control, Expires, ETag,
	 * Last-Modified, Age, Date: llcache keeps a fresh object without a request and
	 * revalidates a stale one, in memory and on the card: onyx_cache.c) */
	static const char *const skip[] = { "Content-Encoding", "Content-Length",
		"Transfer-Encoding", "Set-Cookie", "Location", "Connection",
		"Keep-Alive", NULL };
	const char *p = head, *end = head + headlen;
	int code = head_status(head, headlen);
	bool first = true, plain;
	fetch_msg msg;

	c->status = code;		/* (Onyx: the perf log) */
	if (onyx_netdebug())		/* (Onyx, the bench: NS_NETDEBUG=1) */
		fprintf(stderr, "ONYX-NET head %s\n%.*s\n", nsurl_access(c->url), (int) headlen, head);

	/* every Set-Cookie to NetSurf's jar (a redirect's too: a login's session) */
	while (p < end) {
		const char *eol = memchr(p, '\n', (size_t)(end - p));
		size_t ll = eol ? (size_t)(eol - p) : (size_t)(end - p);
		if (ll > 0 && p[ll - 1] == '\r') ll--;
		if (ll > 10 && hdr_is(p, "Accept-CH") && p[9] == ':') {
			/* (Onyx: the client hints this origin wants: onyx_add_client_hints) */
			char v[512];
			size_t vl = ll - 10 < sizeof v - 1 ? ll - 10 : sizeof v - 1;
			lwc_string *hh = nsurl_get_component(c->url, NSURL_HOST);
			memcpy(v, p + 10, vl);
			v[vl] = '\0';
			if (hh != NULL) {
				onyx_accept_ch(lwc_string_data(hh), v);
				lwc_string_unref(hh);
			}
		}
		if (ll > 11 && !c->no_cookies && hdr_is(p, "Set-Cookie")) {
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
	plain = cenc[0] == '\0' || strcasecmp(cenc, "identity") == 0;
	fetch_set_http_code(c->parent_fetch, code);
	if (code == 304 && c->body == NULL)
		c->not_modified = true;		/* (Onyx: sent after the headers, below) */

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
			if (ll > strlen(skip[k]) && hdr_is(p, skip[k]) &&
			    !(k == 1 && plain && code == 200 && !c->script))
				keep = false;	/* (Onyx: a plain body's length kept: a
						 * download's progress, docs/06 §38) */
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
	if (c->not_modified && !c->aborted) {
		/* Onyx: a 304 to llcache's revalidation -- its stored object is still good (the
		 * headers above bring its new cache data); llcache aborts this fetch */
		msg.type = FETCH_NOTMODIFIED;
		fetch_onyx_send(&msg, c);
		return false;
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
		/* connect + (resumed) handshake, the certificate checked (Onyx) */
		struct onyx_tls_chain ch;
		int sock = kapi_tcp_connect(host, port);
		if (sock < 0) { fetch_onyx_error(c, "Connection failed"); return false; }
		c->ts = onyx_nstls_connect(sock, host, ONYX_TLS_VERIFY |
				(c->insecure ? ONYX_TLS_INSECURE : 0), &ch);
		if (c->ts == NULL) {
			if (ch.failed)
				onyx_cert_error(c, &ch);
			else
				fetch_onyx_error(c, "Connection failed");
			onyx_nstls_chain_free(&ch);
			return false;
		}
		onyx_chain_keep(host, &ch);	/* (Onyx: the padlock's viewer) */
	} else {
		c->sock = kapi_tcp_connect(host, port);
		if (c->sock < 0) { fetch_onyx_error(c, "Connection failed"); return false; }
	}

	req = onyx_request(c->method, path, host, port, c->tls, user_agent_for_host(host),
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
static void onyx_data(struct fetch_onyx_context *c, const uint8_t *b, size_t n, bool last);

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

	/* the body decoded (its Content-Encoding) and handed on */
	onyx_decoder_start(c, cenc);
	onyx_data(c, body, bodylen, true);
	if (!c->aborted) {
		msg.type = FETCH_FINISHED;
		fetch_onyx_send(&msg, c);
	}
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

enum { JOB_RUNNING = 0, JOB_DONE, JOB_RETRY };	/* (JOB_RETRY: Onyx, HTTP/2 -- start again) */

struct onyx_h2;

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
	unsigned idle;			/* Onyx: ticks without a byte before it fails */
	bool insecure;			/* Onyx: the host's bad certificate accepted */
	struct onyx_tls_chain *chain;	/* Onyx: the certificate refused (its chain), or 0 */
	/* Onyx: HTTP/2 */
	bool threaded;			/* a thread of its own (counted in onyx_workers) */
	struct onyx_h2 *h2open;		/* its thread opens this origin's connection, h2 offered */
	struct onyx_job *h2next;	/* the connection's queue */
	const char *proto;		/* "h2" / "http/1.1" (NS_PERF) */
	uint64_t t0;			/* started (NS_PERF) */
	uint64_t t_head;		/* its head came (NS_PERF: the time to the first byte) */
	bool reused;			/* on a connection kept alive (NS_PERF) */
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
	if (j->chain != NULL) {
		onyx_nstls_chain_free(j->chain);
		free(j->chain);
	}
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
	bool insecure;			/* Onyx: its certificate not trusted (the user went on) */
};

#define ONYX_POOL		4
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
static bool pool_take(const char *host, unsigned port, bool tls, bool insecure,
		struct onyx_conn *out)
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
		    (insecure || !onyx_pool[i].insecure) &&
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
	if (onyx_quit) {		/* (Onyx: the app ends: nothing kept) */
		conn_close(k);
		return;
	}
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

static void onyx_sockets_free(void);


/* Onyx: a connection's timings for the perf log (NS_PERF / SD:/apps/jet.app/perf) */
struct onyx_ctime {
	uint64_t queue;		/* waiting for the connects' turn */
	uint64_t dns;		/* the name resolved (the kernel caches it: then ~0) */
	uint64_t tcp;		/* the TCP connect */
	uint64_t wait;		/* waiting for a free socket (the kernel's table full) */
};

/* Onyx (docs/06 §41): a host whose connects keep failing (down, refused, no DNS, the Wi-Fi
 * gone) is not asked again at once -- a page retrying in a loop (a script's fetch on an
 * error, a poll) made a connect, its retry 200 ms later and their DNS a turn, for ever, on
 * the network the remote desktop and telnet share. From the third failure in a row, the
 * connects to it within 1, 2, 4, 8 s (at most) of the last failure fail at once, without
 * the network; a connect that works clears it. */
#define ONYX_CF_HOSTS	8
static struct { char host[64]; unsigned port, fails, last; } onyx_cf[ONYX_CF_HOSTS];
static volatile int onyx_cf_lk;

static bool onyx_cf_held_back(const char *host, unsigned port)
{
	bool held = false;
	unsigned now = kapi_get_ticks();
	int i;

	kapi_lock(&onyx_cf_lk);
	for (i = 0; i < ONYX_CF_HOSTS; i++)
		if (onyx_cf[i].port == port && strcasecmp(onyx_cf[i].host, host) == 0) {
			unsigned n = onyx_cf[i].fails;
			if (n >= 3) {
				unsigned ticks = n - 3 >= 3 ? 800 : 100u << (n - 3);
				held = now - onyx_cf[i].last < ticks;
			}
			break;
		}
	kapi_unlock(&onyx_cf_lk);
	return held;
}

static void onyx_cf_note(const char *host, unsigned port, bool ok)
{
	unsigned now = kapi_get_ticks();
	int i, slot = 0;

	kapi_lock(&onyx_cf_lk);
	for (i = 0; i < ONYX_CF_HOSTS; i++) {
		if (onyx_cf[i].port == port && strcasecmp(onyx_cf[i].host, host) == 0)
			break;
		if (now - onyx_cf[i].last > now - onyx_cf[slot].last)
			slot = i;
	}
	if (i == ONYX_CF_HOSTS) {
		if (ok) {
			kapi_unlock(&onyx_cf_lk);
			return;		/* (nothing to clear) */
		}
		i = slot;
		snprintf(onyx_cf[i].host, sizeof onyx_cf[i].host, "%s", host);
		onyx_cf[i].port = port;
		onyx_cf[i].fails = 0;
	}
	if (ok)
		onyx_cf[i].fails = 0;
	else
		onyx_cf[i].fails++;
	onyx_cf[i].last = now;
	kapi_unlock(&onyx_cf_lk);
}

static int onyx_connect(const char *host, unsigned port, struct onyx_ctime *ct)
{
	int sock = -1, tries;
	unsigned waited = 0;
	uint64_t t0 = onyx_perf_now(), t1;

	if (onyx_cf_held_back(host, port)) {
		__atomic_add_fetch(&onyx_nt_backoff, 1, __ATOMIC_RELAXED);
		if (onyx_perf_on())
			fprintf(stderr, "ONYX-PERF net:tcp %s:%u held back (its connects keep "
					"failing)\n", host, port);
		return -1;
	}
	__atomic_add_fetch(&onyx_nt_conn, 1, __ATOMIC_RELAXED);
	for (tries = 0; tries < 2 && !onyx_quit; tries++) {
		/* (a connect takes 100 ms and more: the others wait asleep -- kapi_lock spun,
		 * seven workers burning the cores the page needs) */
		while (kapi__xchg(&onyx_connect_lk, 1) != 0)
			kapi_msleep(5);
		t1 = onyx_perf_now();
		if (ct != NULL && tries == 0) {
			ct->queue = t1 - t0;
			if (t1 != 0 && KT->net_resolve != 0) {	/* (the perf log: DNS apart) */
				char ip[64];
				kapi_net_resolve(host, ip, sizeof ip);
				ct->dns = onyx_perf_now() - t1;
				t1 = onyx_perf_now();
			}
		}
		sock = kapi_tcp_connect(host, port);
		if (ct != NULL)
			ct->tcp = onyx_perf_now() - t1;
		kapi_unlock(&onyx_connect_lk);
		if (sock >= 0)
			break;
		if (sock == -2) {
			/* Onyx: the kernel's socket table is full (16 sockets for every app: another
			 * app's, or ours kept idle) -- ours closed, then a socket waited for (5 s at
			 * most), not a network error */
			onyx_sockets_free();
			while (sock == -2 && waited < 5000 && !onyx_quit) {
				kapi_msleep(50);
				waited += 50;
				while (kapi__xchg(&onyx_connect_lk, 1) != 0)
					kapi_msleep(5);
				sock = kapi_tcp_connect(host, port);
				kapi_unlock(&onyx_connect_lk);
			}
			if (sock >= 0 || sock == -2)
				break;
		}
		if (sock == -3)
			break;			/* (the name does not resolve: no second try) */
		kapi_msleep(200);
	}
	if (ct != NULL)
		ct->wait = (uint64_t) waited * 1000;
	if (sock != -2 && !onyx_quit)	/* (Onyx: no socket free is not the host's fault) */
		onyx_cf_note(host, port, sock >= 0);
	if (sock < 0)
		__atomic_add_fetch(&onyx_nt_conn_fail, 1, __ATOMIC_RELAXED);
	if (onyx_perf_on() && (sock < 0 || waited)) {
		fprintf(stderr, "ONYX-PERF net:tcp %s:%u %s %lu us%s\n", host, port,
				sock >= 0 ? "ok" : sock == -2 ? "no socket free" : sock == -3 ? "no DNS" : "failed",
				(unsigned long) (onyx_perf_now() - t0), waited ? " (waited for a socket)" : "");
	}
	return sock;
}

/* Onyx: the connects of onyx_ws.c (WebSocket, EventSource, importScripts) take the same
 * turn as the downloads' */
int onyx_fetch_connect(const char *host, unsigned port)
{
	return onyx_connect(host, port, NULL);
}

/* A new connection (TLS: the certificate checked -- *chain set when it is refused). */
static bool conn_open(struct onyx_conn *k, const char *host, unsigned port, bool tls,
		bool insecure, struct onyx_tls_chain **chain, bool h2)
{
	struct onyx_ctime ct;
	uint64_t t0;

	memset(k, 0, sizeof *k);
	memset(&ct, 0, sizeof ct);
	k->tls = tls;
	k->sock = -1;
	k->port = port;
	snprintf(k->host, sizeof k->host, "%s", host);
	k->sock = onyx_connect(host, port, &ct);	/* DNS + connect */
	if (k->sock < 0)
		return false;
	t0 = onyx_perf_now();
	if (tls) {
		struct onyx_tls_chain ch;
		k->ts = onyx_nstls_connect(k->sock, host, ONYX_TLS_VERIFY |	/* (resumed) handshake */
				(insecure ? ONYX_TLS_INSECURE : 0) | (h2 ? ONYX_TLS_H2 : 0), &ch);
		k->sock = -1;				/* (the session's, closed with it) */
		k->insecure = insecure;
		if (k->ts == NULL) {
			if (ch.failed && chain != NULL && *chain == NULL) {
				*chain = malloc(sizeof ch);
				if (*chain != NULL) {
					**chain = ch;	/* (its DER copies with it) */
					return false;
				}
			}
			onyx_nstls_chain_free(&ch);
			return false;
		}
		onyx_chain_keep(host, &ch);	/* (Onyx: the padlock's viewer) */
	}
	/* Onyx: the perf log -- where a new connection's time went */
	if (onyx_perf_on()) {
		const char *alpn = tls ? onyx_nstls_alpn(k->ts) : NULL;
		fprintf(stderr, "ONYX-PERF net:conn %s:%u queue %lu dns %lu tcp %lu%s tls %lu %s %s us %s\n",
				host, port, (unsigned long) ct.queue, (unsigned long) ct.dns,
				(unsigned long) ct.tcp, ct.wait ? " (waited for a socket)" : "",
				(unsigned long) (tls ? onyx_perf_now() - t0 : 0),
				!tls ? "-" : onyx_nstls_resumed(k->ts) ? "resumed" : "full",
				alpn != NULL ? alpn : "http/1.1",
				tls && onyx_nstls_version(k->ts) != NULL ? onyx_nstls_version(k->ts) : "");
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
		if (kapi_get_ticks() - in->t_last > in->j->idle)
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

/* (Onyx: a job's bytes not taken yet past which its HTTP/1.1 thread stops reading) */
#define ONYX_JOB_BACKLOG (8u << 20)

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
		/* Onyx (docs/06 §38): a big body the UI thread does not take for now (a dialog is
		 * open over the page: the download's Save as) waits in the socket, not in memory */
		while (in->j->len >= ONYX_JOB_BACKLOG && !in->j->cancel)
			kapi_msleep(20);
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
	j->t_head = onyx_perf_now();
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

/* A job's end, in its thread (or its HTTP/2 connection's): the UI thread told, or the job
 * freed when its fetch is gone. state: JOB_DONE, JOB_RETRY (Onyx: HTTP/2, to start again). */
static void job_finish(struct onyx_job *j, int state)
{
	int orphan;

	kapi_lock(&j->lk);
	j->state = state;
	orphan = j->orphan;
	kapi_unlock(&j->lk);
	if (orphan) onyx_job_free(j);			/* its fetch was aborted and freed */
	else onyx_post_wake();
}

/* ==== HTTP/2 (Onyx) ==========================================================
 *
 * An origin whose server chooses "h2" by ALPN gets ONE connection, run by a thread: the
 * download thread that opened it becomes its owner (h2_run) for as long as it lives. Every
 * request to that origin is a stream multiplexed on it -- nghttp2 does the framing, HPACK and
 * the flow control. The UI thread gives the next jobs for the origin straight to the
 * connection (h2_queue: no thread, no connect, no handshake of their own); the owner submits
 * them and fills each stream's job as a download thread fills its own (the head, then the
 * body as it comes: job_append; job_finish), so the UI side (fetch_onyx_step_threaded, the
 * streaming to the core, the decoders) is the same for both protocols. While the first
 * connection to an origin is being made (its ALPN not known yet) the other fetches for it
 * wait a poll or two; a server that chose http/1.1 is noted (onyx_h1_only) and its fetches go
 * as before (a connection each, kept alive). Server push is refused (SETTINGS_ENABLE_PUSH 0).
 * A stream refused (the server's GOAWAY, REFUSED_STREAM) or a connection lost before a
 * stream's head came makes its job JOB_RETRY: the UI thread starts the fetch again.
 */
enum { H2_CONNECTING = 0, H2_READY, H2_GONE };

/* (the Pi's kernel has 16 sockets in all: the HTTP/2 connections are few, an idle one is
 * closed for a new origin's -- h2_evict -- and all the idle ones when a connect fails) */
#define ONYX_H2_MAX		4		/* origins with an HTTP/2 connection at once */
#define ONYX_H2_IDLE_TICKS	3000		/* a connection without a stream for 30 s: closed */
#define ONYX_H1_ONLY		32		/* the origins that chose http/1.1, remembered */
#define ONYX_H2_WINDOW		6291456		/* a stream's receive window: 6 MB (Chrome's) */
#define ONYX_H2_CONN_WINDOW	15728640	/* the connection's: 15 MB (Chrome's) */

struct onyx_h2;

struct h2_stream {
	struct onyx_job *j;
	struct onyx_h2 *e;
	int32_t id;
	char *head;			/* the head being read: "HTTP/2 200\r\nname: value\r\n"... */
	size_t hlen, hcap;
	bool interim;			/* ... a 1xx one (dropped at its end) */
	bool head_done;			/* the final head given to the job */
	bool rst;			/* RST_STREAM sent: its fetch was aborted, or it timed out */
	bool retry;			/* its job to start again over HTTP/1.1 (a 403: below) */
	size_t since_post;
	size_t boff, blen;		/* the request's body: sent so far, in all */
	unsigned t_last;		/* kapi_get_ticks at its last frame */
	struct h2_stream *next;
};

struct onyx_h2 {
	char host[256];
	unsigned port;
	volatile int state;		/* H2_CONNECTING, H2_READY, H2_GONE (onyx_h2_lk) */
	bool insecure;			/* its certificate not trusted (the user went on) */
	onyx_tls_sess *ts;
	nghttp2_session *ng;
	volatile int lk;		/* pending, pend_tail */
	struct onyx_job *pending, *pend_tail;	/* queued by the UI thread (j->h2next) */
	volatile unsigned wake;		/* bumped when a job is queued (kapi_wake_word) */
	struct h2_stream *streams;	/* the owner thread's */
	int nstreams;
	bool dead;			/* the connection failed */
	bool goaway;			/* the server said GOAWAY: no new stream on it */
	bool closing;			/* the owner is tearing it down: callbacks do nothing */
	volatile int evict;		/* out of the table for another origin: close when idle */
	volatile unsigned idle_at;	/* kapi_get_ticks when its last stream ended */
};

static struct onyx_h2 *onyx_h2s[ONYX_H2_MAX];
static struct { char host[256]; unsigned port; long day; } onyx_h1_only[ONYX_H1_ONLY];
static unsigned onyx_h1_next;
static volatile int onyx_h2_lk;		/* the two tables, an entry's state */

/* HTTP/2 on (NS_H2=0 on the PC bench: off -- the before / after of loadtime.sh) */
static bool onyx_h2_enabled(void)
{
	static int v = -1;
	if (v < 0) {
		const char *e = getenv("NS_H2");
		v = e == NULL || atoi(e) != 0;
	}
	return v != 0;
}

/* (onyx_h2_lk held) the live entry of an origin, or NULL */
static struct onyx_h2 *h2_find(const char *host, unsigned port)
{
	int i;
	for (i = 0; i < ONYX_H2_MAX; i++)
		if (onyx_h2s[i] != NULL && onyx_h2s[i]->port == port &&
		    strcasecmp(onyx_h2s[i]->host, host) == 0)
			return onyx_h2s[i];
	return NULL;
}

/* (onyx_h2_lk held) whether an origin chose http/1.1 */
static bool h1_only(const char *host, unsigned port)
{
	int i;
	for (i = 0; i < ONYX_H1_ONLY; i++)
		if (onyx_h1_only[i].port == port && strcasecmp(onyx_h1_only[i].host, host) == 0)
			return true;
	return false;
}

static void h1_only_add(const char *host, unsigned port)
{
	kapi_lock(&onyx_h2_lk);
	if (!h1_only(host, port)) {
		unsigned i = onyx_h1_next++ % ONYX_H1_ONLY;
		snprintf(onyx_h1_only[i].host, sizeof onyx_h1_only[i].host, "%s", host);
		onyx_h1_only[i].port = port;
		onyx_h1_only[i].day = (long) (time(NULL) / 86400);
	}
	kapi_unlock(&onyx_h2_lk);
}

/* The entry out of the table: no job is queued to it any more (the UI thread queues with
 * onyx_h2_lk held). */
static void h2_retire(struct onyx_h2 *e)
{
	int i;
	kapi_lock(&onyx_h2_lk);
	for (i = 0; i < ONYX_H2_MAX; i++)
		if (onyx_h2s[i] == e)
			onyx_h2s[i] = NULL;
	e->state = H2_GONE;
	kapi_unlock(&onyx_h2_lk);
}

/* (onyx_h2_lk held) the idle connections closed -- the one idle longest (all: every idle one):
 * out of the table, their owners told (they close them once idle). Returns a free slot, or -1. */
static int h2_evict(bool all)
{
	int i, best = -1;
	unsigned now = kapi_get_ticks();

	for (i = 0; i < ONYX_H2_MAX; i++) {
		struct onyx_h2 *e = onyx_h2s[i];
		if (e == NULL || e->state != H2_READY || e->nstreams > 0 || e->pending != NULL)
			continue;
		if (all) {
			e->evict = 1;
			e->state = H2_GONE;
			onyx_h2s[i] = NULL;
			e->wake++;
			kapi_wake_word(&e->wake);
			best = i;
		} else if (best < 0 || now - e->idle_at > now - onyx_h2s[best]->idle_at) {
			best = i;
		}
	}
	if (!all && best >= 0) {
		struct onyx_h2 *e = onyx_h2s[best];
		e->evict = 1;
		e->state = H2_GONE;
		onyx_h2s[best] = NULL;
		e->wake++;
		kapi_wake_word(&e->wake);
	}
	return best;
}

/* Onyx: a connect failed -- the kernel's sockets (16 in all on the Pi) may be taken by the
 * idle connections: the kept HTTP/1.1 ones closed, the idle HTTP/2 ones told to close */
static void onyx_sockets_free(void)
{
	struct onyx_conn k;

	kapi_lock(&onyx_h2_lk);
	h2_evict(true);
	kapi_unlock(&onyx_h2_lk);
	for (;;) {
		kapi_lock(&onyx_pool_lk);
		if (onyx_pool_n == 0) {
			kapi_unlock(&onyx_pool_lk);
			break;
		}
		k = onyx_pool[--onyx_pool_n];
		kapi_unlock(&onyx_pool_lk);
		conn_close(&k);
	}
}

/* Onyx: what the fetcher remembers across launches, on the card beside the app
 * (SD:/apps/jet.app/): the TLS sessions (TLSSessions: the first connection to a known
 * host resumes -- one round trip less, no certificate chain) and the origins that answer over
 * HTTP/1.1 only (HTTP1Hosts, "host port day" -- kept a week: their fetches need not wait for
 * the ALPN of a first connection, and a CDN that refused HTTP/2 with a 403 is not tried again) */
#define ONYX_STATE_TLS	ONYX_NS_DATAPATH "TLSSessions"
#define ONYX_STATE_H1	ONYX_NS_DATAPATH "HTTP1Hosts"

static void onyx_state_load(void)
{
	FILE *f;
	char line[300], host[256];
	unsigned port;
	long day, today = (long) (time(NULL) / 86400);

	onyx_nstls_sessions_load(ONYX_STATE_TLS);
	f = fopen(ONYX_STATE_H1, "r");
	if (f == NULL)
		return;
	while (fgets(line, sizeof line, f) != NULL)
		if (sscanf(line, "%255s %u %ld", host, &port, &day) == 3 &&
		    (today < 20000 || day > today - 7)) {	/* (a week; no clock: kept) */
			unsigned i = onyx_h1_next++ % ONYX_H1_ONLY;
			snprintf(onyx_h1_only[i].host, sizeof onyx_h1_only[i].host, "%s", host);
			onyx_h1_only[i].port = port;
			onyx_h1_only[i].day = day;
		}
	fclose(f);
}

/* Onyx: the state written (the app's end; gui.c after a page's load, with the cookies) */
void onyx_fetch_save_state(void)
{
	FILE *f;
	int i;

	onyx_nstls_sessions_save(ONYX_STATE_TLS);
	f = fopen(ONYX_STATE_H1, "w");
	if (f == NULL)
		return;
	kapi_lock(&onyx_h2_lk);
	for (i = 0; i < ONYX_H1_ONLY; i++)
		if (onyx_h1_only[i].host[0] != '\0')
			fprintf(f, "%s %u %ld\n", onyx_h1_only[i].host, onyx_h1_only[i].port,
					onyx_h1_only[i].day);
	kapi_unlock(&onyx_h2_lk);
	fclose(f);
}

/* Onyx: fetch.c -- whether a host's fetches are streams on an HTTP/2 connection (they are
 * not limited by max_fetchers_per_host then) */
bool onyx_fetch_multiplexed(lwc_string *host)
{
	bool r = false;
	int i;

	if (host == NULL || !onyx_h2_enabled())
		return false;
	kapi_lock(&onyx_h2_lk);
	for (i = 0; i < ONYX_H2_MAX && !r; i++)
		r = onyx_h2s[i] != NULL && onyx_h2s[i]->state == H2_READY &&
			!onyx_h2s[i]->dead && strcasecmp(onyx_h2s[i]->host, lwc_string_data(host)) == 0;
	kapi_unlock(&onyx_h2_lk);
	return r;
}

/* A job to the connection (onyx_h2_lk held: the entry is READY) */
static void h2_queue(struct onyx_h2 *e, struct onyx_job *j)
{
	j->h2next = NULL;
	kapi_lock(&e->lk);
	if (e->pend_tail != NULL)
		e->pend_tail->h2next = j;
	else
		e->pending = j;
	e->pend_tail = j;
	kapi_unlock(&e->lk);
	e->wake++;
	kapi_wake_word(&e->wake);
}

static struct onyx_job *h2_take_pending(struct onyx_h2 *e)
{
	struct onyx_job *q;
	kapi_lock(&e->lk);
	q = e->pending;
	e->pending = e->pend_tail = NULL;
	kapi_unlock(&e->lk);
	return q;
}

static void h2_head_add(struct h2_stream *s, const char *a, size_t an, const char *b, size_t bn,
		const char *c, size_t cn)
{
	size_t need = s->hlen + an + bn + cn + 1;
	if (need > s->hcap) {
		size_t cap = s->hcap ? s->hcap * 2 : 1024;
		char *h;
		while (cap < need) cap *= 2;
		h = realloc(s->head, cap);
		if (h == NULL)
			return;
		s->head = h;
		s->hcap = cap;
	}
	memcpy(s->head + s->hlen, a, an); s->hlen += an;
	memcpy(s->head + s->hlen, b, bn); s->hlen += bn;
	memcpy(s->head + s->hlen, c, cn); s->hlen += cn;
	s->head[s->hlen] = '\0';
}

static int h2_on_header(nghttp2_session *ng, const nghttp2_frame *frame, nghttp2_rcbuf *name,
		nghttp2_rcbuf *value, uint8_t flags, void *user)
{
	struct onyx_h2 *e = user;
	struct h2_stream *s;
	nghttp2_vec n = nghttp2_rcbuf_get_buf(name), v = nghttp2_rcbuf_get_buf(value);

	(void) flags;
	if (e->closing || frame->hd.type != NGHTTP2_HEADERS)
		return 0;
	s = nghttp2_session_get_stream_user_data(ng, frame->hd.stream_id);
	if (s == NULL || s->head_done)
		return 0;		/* (a trailer) */
	s->t_last = kapi_get_ticks();
	if (n.len == 7 && memcmp(n.base, ":status", 7) == 0) {
		s->hlen = 0;
		s->interim = v.len > 0 && v.base[0] == '1';
		h2_head_add(s, "HTTP/2 ", 7, (const char *) v.base, v.len, "\r\n", 2);
	} else if (n.len > 0 && n.base[0] != ':') {
		h2_head_add(s, (const char *) n.base, n.len, ": ", 2, (const char *) v.base, v.len);
		h2_head_add(s, "\r\n", 2, "", 0, "", 0);
	}
	return 0;
}

static int h2_on_frame_recv(nghttp2_session *ng, const nghttp2_frame *frame, void *user)
{
	struct onyx_h2 *e = user;
	struct h2_stream *s;

	if (onyx_netdebug())
		fprintf(stderr, "ONYX-NET h2 %s frame type %d stream %d flags %x length %u\n", e->host,
				frame->hd.type, frame->hd.stream_id, frame->hd.flags, (unsigned) frame->hd.length);
	if (e->closing)
		return 0;
	if (frame->hd.type == NGHTTP2_GOAWAY) {
		/* no new stream on it: the next jobs get a new connection (the streams above
		 * its last stream id are closed REFUSED_STREAM: retried) */
		e->goaway = true;
		h2_retire(e);
		return 0;
	}
	if (frame->hd.type != NGHTTP2_HEADERS || !(frame->hd.flags & NGHTTP2_FLAG_END_HEADERS))
		return 0;
	s = nghttp2_session_get_stream_user_data(ng, frame->hd.stream_id);
	if (s == NULL || s->head_done || s->head == NULL)
		return 0;
	if (s->interim) {		/* (100 Continue, 103 Early Hints: not the answer) */
		s->hlen = 0;
		s->interim = false;
		return 0;
	}
	if (s->id == 1 && s->hlen > 10 && strncmp(s->head, "HTTP/2 403", 10) == 0) {
		/* the first answer on the connection is a 403: some CDNs (Fastly: www.bbc.com)
		 * refuse a Chrome User-Agent whose TLS and HTTP/2 do not look like Chrome's --
		 * over HTTP/1.1 they answer. The origin noted as HTTP/1.1 only, the connection
		 * given up, the fetch started again. */
		h1_only_add(e->host, e->port);
		e->goaway = true;
		h2_retire(e);
		s->retry = true;
		s->rst = true;
		nghttp2_submit_rst_stream(ng, NGHTTP2_FLAG_NONE, s->id, NGHTTP2_CANCEL);
		return 0;
	}
	if (s->hlen >= 2)
		s->hlen -= 2;		/* (no final CRLF: the head's lines, as HTTP/1.1's) */
	s->head[s->hlen] = '\0';
	kapi_lock(&s->j->lk);
	s->j->head = s->head;
	s->j->t_head = onyx_perf_now();
	s->j->headlen = s->hlen;
	kapi_unlock(&s->j->lk);
	s->head = NULL;
	s->hlen = s->hcap = 0;
	s->head_done = true;
	onyx_post_wake();
	return 0;
}

static int h2_on_data(nghttp2_session *ng, uint8_t flags, int32_t id, const uint8_t *data,
		size_t len, void *user)
{
	struct onyx_h2 *e = user;
	struct h2_stream *s;

	(void) flags;
	if (e->closing)
		return 0;
	s = nghttp2_session_get_stream_user_data(ng, id);
	if (s == NULL || s->rst)
		return 0;
	s->t_last = kapi_get_ticks();
	if (!job_append(s->j, data, len, &s->since_post)) {
		s->j->err = "Out of memory";
		nghttp2_submit_rst_stream(ng, NGHTTP2_FLAG_NONE, id, NGHTTP2_INTERNAL_ERROR);
		s->rst = true;
	}
	return 0;
}

/* A stream's end: its job finished (retried when nothing of the answer came and the server
 * refused it or the connection went) */
static void h2_stream_end(struct onyx_h2 *e, struct h2_stream *s, uint32_t error)
{
	struct h2_stream **p;
	int state = JOB_DONE;

	for (p = &e->streams; *p != NULL; p = &(*p)->next)
		if (*p == s) {
			*p = s->next;
			e->nstreams--;
			break;
		}
	if (s->retry && !s->j->cancel)
		state = JOB_RETRY;
	else if (error != NGHTTP2_NO_ERROR && !s->j->cancel) {
		if (!s->head_done && (error == NGHTTP2_REFUSED_STREAM || e->dead || e->goaway))
			state = JOB_RETRY;
		else if (s->j->err == NULL)
			s->j->err = "HTTP/2 stream reset";
	}
	if (s->since_post > 0)
		onyx_post_wake();
	free(s->head);
	job_finish(s->j, state);
	free(s);
}

static int h2_on_close(nghttp2_session *ng, int32_t id, uint32_t error, void *user)
{
	struct onyx_h2 *e = user;
	struct h2_stream *s;

	if (e->closing)
		return 0;
	s = nghttp2_session_get_stream_user_data(ng, id);
	if (s != NULL)
		h2_stream_end(e, s, error);
	return 0;
}

static nghttp2_ssize h2_body_read(nghttp2_session *ng, int32_t id, uint8_t *buf, size_t len,
		uint32_t *flags, nghttp2_data_source *src, void *user)
{
	struct h2_stream *s = src->ptr;
	size_t n = s->blen - s->boff;

	(void) ng; (void) id; (void) user;
	if (n > len)
		n = len;
	memcpy(buf, s->j->body + s->boff, n);
	s->boff += n;
	if (s->boff == s->blen)
		*flags |= NGHTTP2_DATA_FLAG_EOF;
	return (nghttp2_ssize) n;
}

#define H2_NV(nv, n, nl, v, vl) do { (nv).name = (uint8_t *) (n); (nv).namelen = (nl); \
	(nv).value = (uint8_t *) (v); (nv).valuelen = (vl); (nv).flags = NGHTTP2_NV_FLAG_NONE; } while (0)

/* A job's request, a new stream: false when it cannot be (the job is then retried) */
static bool h2_submit(struct onyx_h2 *e, struct onyx_job *j)
{
	char host[256], auth[300], clen[24], *hd = NULL, *l;
	const char *path;
	unsigned port;
	size_t nh = 8, pl;
	nghttp2_nv *nv;
	int n = 0;
	int32_t id;
	struct h2_stream *s;
	nghttp2_data_provider2 prd;
	nghttp2_priority_spec pri;
	bool has_accept, has_lang, has_ctype;

	if (!onyx_split_url(j->url, host, sizeof host, &port, &path, 443))
		return false;
	pl = strcspn(path, "#");
	if (port == 443)
		snprintf(auth, sizeof auth, "%s", host);
	else
		snprintf(auth, sizeof auth, "%s:%u", host, port);
	if (j->hdrs != NULL) {
		hd = strdup(j->hdrs);		/* (the names lower-cased in place) */
		if (hd == NULL)
			return false;
		for (l = hd; *l != '\0'; l++)
			if (*l == '\n')
				nh++;
	}
	nv = calloc(nh + 13, sizeof *nv);	/* (8 ours, nh + 1 the caller's, 2 the body's, priority) */
	s = calloc(1, sizeof *s);
	if (nv == NULL || s == NULL) {
		free(nv); free(s); free(hd);
		return false;
	}
	has_accept = hdrs_have(j->hdrs, "Accept");
	has_lang = hdrs_have(j->hdrs, "Accept-Language");
	has_ctype = hdrs_have(j->hdrs, "Content-Type");
	/* (Chrome's order of the pseudo-headers: some CDNs' bot checks look at it) */
	H2_NV(nv[n], ":method", 7, j->method, strlen(j->method)); n++;
	H2_NV(nv[n], ":authority", 10, auth, strlen(auth)); n++;
	H2_NV(nv[n], ":scheme", 7, "https", 5); n++;
	H2_NV(nv[n], ":path", 5, path, pl); n++;
	H2_NV(nv[n], "user-agent", 10, j->ua, strlen(j->ua)); n++;
	if (!has_accept) { H2_NV(nv[n], "accept", 6, "*/*", 3); n++; }
	if (!has_lang) { H2_NV(nv[n], "accept-language", 15, j->lang, strlen(j->lang)); n++; }
	H2_NV(nv[n], "accept-encoding", 15, ONYX_ACCEPT_ENCODING, strlen(ONYX_ACCEPT_ENCODING)); n++;
	/* the caller's headers (the cookies, the referer, the fetch metadata...), their names
	 * in lower case; those HTTP/2 forbids left out */
	for (l = hd; l != NULL && *l != '\0' && n < (int) nh + 9; ) {
		char *eol = strchr(l, '\n'), *colon = strchr(l, ':'), *v, *ve, *k;
		size_t nl;
		if (eol == NULL)
			eol = l + strlen(l);
		if (colon == NULL || colon > eol || colon == l) {
			l = *eol ? eol + 1 : eol;
			continue;
		}
		for (k = l; k < colon; k++)
			if (*k >= 'A' && *k <= 'Z')
				*k = (char) (*k - 'A' + 'a');
		nl = (size_t) (colon - l);
		v = colon + 1;
		while (v < eol && (*v == ' ' || *v == '\t')) v++;
		ve = eol;
		while (ve > v && (ve[-1] == '\r' || ve[-1] == ' ')) ve--;
		if (!((nl == 4 && memcmp(l, "host", 4) == 0) ||
		      (nl == 10 && memcmp(l, "connection", 10) == 0) ||
		      (nl == 10 && memcmp(l, "keep-alive", 10) == 0) ||
		      (nl == 16 && memcmp(l, "proxy-connection", 16) == 0) ||
		      (nl == 17 && memcmp(l, "transfer-encoding", 17) == 0) ||
		      (nl == 7 && memcmp(l, "upgrade", 7) == 0) ||
		      (nl == 2 && memcmp(l, "te", 2) == 0) ||
		      (nl == 14 && memcmp(l, "content-length", 14) == 0))) {
			H2_NV(nv[n], l, nl, v, (size_t) (ve - v));
			n++;
		}
		l = *eol ? eol + 1 : eol;
	}
	s->j = j;
	s->e = e;
	s->t_last = kapi_get_ticks();
	if (j->body != NULL) {
		s->blen = strlen(j->body);
		if (!has_ctype) {
			H2_NV(nv[n], "content-type", 12, "application/x-www-form-urlencoded", 33);
			n++;
		}
		snprintf(clen, sizeof clen, "%u", (unsigned) s->blen);
		H2_NV(nv[n], "content-length", 14, clen, strlen(clen)); n++;
		prd.source.ptr = s;
		prd.read_callback = h2_body_read;
	}
	/* (Onyx: the regular headers in Chrome's order, after the pseudo-headers -- a stable
	 * insertion sort by onyx_hdr_rank -- and its priority header, by destination) */
	{
		const char *dest = NULL;
		int a, b;
		for (a = 4; a < n; a++)
			if (nv[a].namelen == 14 && memcmp(nv[a].name, "sec-fetch-dest", 14) == 0)
				dest = (const char *) nv[a].value;
		if (dest != NULL) {
			const char *pr = strncmp(dest, "document", 8) == 0 ||
				strncmp(dest, "iframe", 6) == 0 ? "u=0, i" :
				strncmp(dest, "style", 5) == 0 || strncmp(dest, "font", 4) == 0 ? "u=0" :
				strncmp(dest, "script", 6) == 0 ? "u=1" : "u=1, i";
			H2_NV(nv[n], "priority", 8, pr, strlen(pr));
			n++;
		}
		for (a = 5; a < n; a++) {
			nghttp2_nv t = nv[a];
			int r = onyx_hdr_rank((const char *) t.name, t.namelen);
			for (b = a - 1; b >= 4 &&
			     onyx_hdr_rank((const char *) nv[b].name, nv[b].namelen) > r; b--)
				nv[b + 1] = nv[b];
			nv[b + 1] = t;
		}
	}
	/* (Chrome's priority on its HEADERS: exclusive, on stream 0, weight 256) */
	nghttp2_priority_spec_init(&pri, 0, 256, 1);
	id = nghttp2_submit_request2(e->ng, &pri, nv, (size_t) n, j->body != NULL ? &prd : NULL, s);
	free(nv);
	free(hd);
	if (id < 0) {
		free(s);
		return false;
	}
	s->id = id;
	s->next = e->streams;
	e->streams = s;
	e->nstreams++;
	return true;
}

/* The connection's session: nghttp2 as a client, push refused, big windows */
static bool h2_session_new(struct onyx_h2 *e)
{
	nghttp2_session_callbacks *cb;
	/* (Chrome's SETTINGS and connection window: a CDN (Fastly: bbc.com) answered 403 to a
	 * Chrome User-Agent whose HTTP/2 settings were not Chrome's) */
	nghttp2_settings_entry iv[4] = {
		{ NGHTTP2_SETTINGS_HEADER_TABLE_SIZE, 65536 },
		{ NGHTTP2_SETTINGS_ENABLE_PUSH, 0 },
		{ NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, ONYX_H2_WINDOW },
		{ NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE, 262144 },
	};
	int rv;

	if (nghttp2_session_callbacks_new(&cb) != 0)
		return false;
	nghttp2_session_callbacks_set_on_header_callback2(cb, h2_on_header);
	nghttp2_session_callbacks_set_on_frame_recv_callback(cb, h2_on_frame_recv);
	nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cb, h2_on_data);
	nghttp2_session_callbacks_set_on_stream_close_callback(cb, h2_on_close);
	rv = nghttp2_session_client_new(&e->ng, cb, e);
	nghttp2_session_callbacks_del(cb);
	if (rv != 0)
		return false;
	nghttp2_submit_settings(e->ng, NGHTTP2_FLAG_NONE, iv, 4);
	nghttp2_session_set_local_window_size(e->ng, NGHTTP2_FLAG_NONE, 0, ONYX_H2_CONN_WINDOW);
	return true;
}

/* The owner thread: the connection's life -- the jobs queued submitted as streams, the frames
 * sent and read, the aborted fetches' streams reset -- until it fails, the server ends it or
 * it has been idle ONYX_H2_IDLE_TICKS. Then its jobs are finished (or retried) and it is
 * freed. */
static void h2_run(struct onyx_h2 *e)
{
	static const unsigned nap_max = 10;
	/* Onyx (docs/06 §32): an idle connection (no stream: kept for the next request, up to
	 * ONYX_H2_IDLE_TICKS) looks at its socket 10 times a second, not 100 -- a new job
	 * wakes it at once (e->wake) */
	static const unsigned nap_idle_max = 100;
	uint8_t *buf = malloc(16384);
	unsigned idle_since = kapi_get_ticks(), nap = 1;
	struct h2_stream *s, *sn;
	struct onyx_job *q, *qn;

	while (buf != NULL && !onyx_quit) {
		unsigned seen = e->wake, now;
		bool busy = false;
		const uint8_t *data;
		nghttp2_ssize n;
		int r;

		/* the jobs the UI thread queued */
		for (q = h2_take_pending(e); q != NULL; q = qn) {
			qn = q->h2next;
			busy = true;
			if (q->cancel)
				job_finish(q, JOB_DONE);
			else if (e->dead || e->goaway || !h2_submit(e, q))
				job_finish(q, JOB_RETRY);
		}
		/* the streams of the aborted fetches, those silent too long */
		now = kapi_get_ticks();
		for (s = e->streams; s != NULL; s = s->next) {
			if (s->rst)
				continue;
			if (s->j->cancel || now - s->t_last > s->j->idle) {
				if (!s->j->cancel && s->j->err == NULL)
					s->j->err = "Timeout";
				nghttp2_submit_rst_stream(e->ng, NGHTTP2_FLAG_NONE, s->id,
						NGHTTP2_CANCEL);
				s->rst = true;
			}
		}
		/* out */
		while ((n = nghttp2_session_mem_send2(e->ng, &data)) > 0) {
			if (onyx_nstls_send(e->ts, data, (int) n) < 0) {
				e->dead = true;
				break;
			}
			busy = true;
		}
		if (n < 0)
			e->dead = true;
		/* in */
		if (!e->dead) {
			r = onyx_nstls_recv(e->ts, buf, 16384);
			if (r > 0) {
				if (nghttp2_session_mem_recv2(e->ng, buf, (size_t) r) < 0)
					e->dead = true;
				busy = true;
			} else if (r < 0) {
				e->dead = true;		/* (the server closed it) */
			}
		}
		if (e->dead || (e->goaway && e->nstreams == 0))
			break;
		if (!nghttp2_session_want_read(e->ng) && !nghttp2_session_want_write(e->ng))
			break;
		if (e->nstreams > 0 || e->pending != NULL) {
			idle_since = now;
			e->idle_at = now;
		} else if (e->evict || now - idle_since > ONYX_H2_IDLE_TICKS) {
			h2_retire(e);		/* (no job queued from now on) */
			if (e->pending != NULL)
				continue;	/* (one came meanwhile: served first) */
			nghttp2_session_terminate_session(e->ng, NGHTTP2_NO_ERROR);
			while ((n = nghttp2_session_mem_send2(e->ng, &data)) > 0)
				if (onyx_nstls_send(e->ts, data, (int) n) < 0)
					break;
			break;
		}
		if (busy) {
			nap = 1;
			continue;
		}
		/* nothing to do: a nap, cut short when a job is queued (a socket's recv does not
		 * block on Onyx: this is the connection's wait) */
		if (kapi_wait_word(&e->wake, seen, nap) < 0)
			kapi_msleep(nap);
		if (nap < nap_max)
			nap *= 2;
		else if (e->nstreams == 0 && e->pending == NULL && nap < nap_idle_max)
			nap += 10;	/* (Onyx: no stream -- a server's ping or goaway can wait) */
		else if (e->nstreams > 0 && nap > nap_max)
			nap = nap_max;
	}
	/* the end */
	h2_retire(e);
	e->dead = true;
	for (s = e->streams; s != NULL; s = sn) {
		sn = s->next;
		h2_stream_end(e, s, NGHTTP2_CANCEL);
	}
	for (q = h2_take_pending(e); q != NULL; q = qn) {
		qn = q->h2next;
		job_finish(q, q->cancel ? JOB_DONE : JOB_RETRY);
	}
	e->closing = true;
	nghttp2_session_del(e->ng);
	onyx_nstls_close(e->ts);
	free(buf);
	free(e);
}

/* The download thread: a connection (kept or new), the request, the response -- blocking,
 * in its own thread. It touches its job only; the UI thread delivers (fetch_onyx_poll).
 * Onyx: the first download to an origin (j->h2open) offers HTTP/2 -- when the server takes
 * it, this thread runs the connection from then on (h2_run) and its job is its first stream. */
static int fetch_onyx_worker(void *arg)
{
	struct onyx_job *j = arg;
	struct onyx_h2 *e = j->h2open;
	char host[256], *req = NULL;
	const char *path;
	unsigned port;
	bool tls = strncasecmp(j->url, "https:", 6) == 0, keep = false, have = false;
	int len, r = -1, attempt;
	struct onyx_conn k;

	if (!onyx_split_url(j->url, host, sizeof host, &port, &path, tls ? 443 : 80)) {
		j->err = "Malformed URL";
		goto done;
	}
	if (e != NULL) {
		/* Onyx: HTTP/2 offered by ALPN */
		const char *alpn;
		if (!conn_open(&k, host, port, true, j->insecure, &j->chain, true)) {
			conn_close(&k);
			h2_retire(e);
			free(e);
			j->err = j->chain != NULL ? "Certificate not trusted" : "Connection failed";
			goto done;
		}
		alpn = onyx_nstls_alpn(k.ts);
		if (alpn != NULL && strcmp(alpn, "h2") == 0) {
			e->ts = k.ts;
			e->insecure = j->insecure;
			if (!h2_session_new(e)) {
				h2_retire(e);
				free(e);
				conn_close(&k);
				j->err = "Out of memory";
				goto done;
			}
			j->proto = "h2";
			kapi_lock(&onyx_h2_lk);
			e->state = H2_READY;
			h2_queue(e, j);			/* (this job: the first stream) */
			kapi_unlock(&onyx_h2_lk);
			h2_run(e);			/* (the connection's life; e freed) */
			__atomic_sub_fetch(&onyx_threads, 1, __ATOMIC_SEQ_CST);
			return 0;
		}
		/* http/1.1: the origin noted, the fetches waiting for it go as before */
		h1_only_add(host, port);
		h2_retire(e);
		free(e);
		have = true;
	}
	j->proto = "http/1.1";
	req = onyx_request(j->method, path, host, port, tls, j->ua, j->lang, j->hdrs, j->body,
			true, &len);
	if (req == NULL) {
		if (have)
			conn_close(&k);
		j->err = "Request too large";
		goto done;
	}
	for (attempt = 0; attempt < 3 && !j->cancel; attempt++) {
		bool pooled = !have && attempt < 2 && pool_take(host, port, tls, j->insecure, &k);
		if (have) {
			have = false;			/* (the connection HTTP/2 was offered on) */
		} else if (!pooled) {
			if (!conn_open(&k, host, port, tls, j->insecure, &j->chain, false)) {
				conn_close(&k);
				j->err = j->chain != NULL ? "Certificate not trusted" :
					"Connection failed";
				r = -1;
				break;
			}
		}
		k.uses++;
		j->reused = k.uses > 1;
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
	job_finish(j, JOB_DONE);
	__atomic_sub_fetch(&onyx_threads, 1, __ATOMIC_SEQ_CST);	/* (its socket closed) */
	return 0;
}

/* The UI thread: start a download -- 1 started (a thread of its own, or a stream on its
 * origin's HTTP/2 connection), 0 not yet (a later poll: the downloads at their limit, or the
 * origin's first connection still being made), -1 no thread (the state machine takes it). */
static int onyx_job_start(struct fetch_onyx_context *c)
{
	struct onyx_job *j;
	struct onyx_h2 *e = NULL;
	char host[256];
	const char *path;
	unsigned port = 0;
	bool https = strncasecmp(nsurl_access(c->url), "https:", 6) == 0;

	/* Onyx (docs/06 §41): the window hidden (minimised, covered: its timers already slowed
	 * to a second), the scripts' requests start one a second -- a page's polls, beacons and
	 * prefetches do not keep the network busy in the background */
	if (c->script && c->retries == 0 && onyx_chrome_view_state() == 2) {
		static unsigned last_bg;
		unsigned now = kapi_get_ticks();
		if (now - last_bg < 100)
			return 0;		/* (a later poll) */
		last_bg = now;
	}

	/* Onyx: HTTP/2 -- the origin's connection, if it has one (onyx_h2_lk held while it
	 * is used: it stays in the table) */
	if (https && onyx_h2_enabled() &&
	    onyx_split_url(nsurl_access(c->url), host, sizeof host, &port, &path, 443)) {
		kapi_lock(&onyx_h2_lk);
		e = h2_find(host, port);
		if (e != NULL && e->state == H2_CONNECTING) {
			kapi_unlock(&onyx_h2_lk);
			return 0;		/* (its ALPN in a moment) */
		}
		if (e != NULL && (e->state != H2_READY || e->dead || e->goaway ||
				(e->insecure && !c->insecure)))
			e = NULL;
		if (e == NULL)
			kapi_unlock(&onyx_h2_lk);
	}
	if (e == NULL && onyx_workers >= ONYX_MAX_WORKERS)
		return 0;			/* (a later poll) */
	j = calloc(1, sizeof *j);
	if (j == NULL) {
		if (e != NULL)
			kapi_unlock(&onyx_h2_lk);
		return e != NULL ? 0 : -1;
	}
	j->url = strdup(nsurl_access(c->url));
	j->method = strdup(c->method);
	j->hdrs = c->hdrs != NULL ? strdup(c->hdrs) : NULL;
	j->body = c->body != NULL ? strdup(c->body) : NULL;
	{	/* (Onyx: the host's -- the site's version, the toolbar's pill) */
		lwc_string *hh = nsurl_get_component(c->url, NSURL_HOST);
		j->ua = user_agent_for_host(hh != NULL ? lwc_string_data(hh) : NULL);
		if (hh != NULL)
			lwc_string_unref(hh);
	}
	/* Onyx: a script's request may wait long for its answer (a long poll: a chat's server
	 * holds it till something happens, a streamed response between its events) */
	j->idle = c->script ? ONYX_SCRIPT_IDLE_TICKS : ONYX_IDLE_TICKS;
	j->insecure = c->insecure;
	j->lang = nsoption_charp(accept_language) != NULL ? nsoption_charp(accept_language) :
		"fr-FR,fr;q=0.9,en-US;q=0.8,en;q=0.7";
	j->t0 = onyx_perf_now();
	if (j->url == NULL || j->method == NULL) {
		if (e != NULL) kapi_unlock(&onyx_h2_lk);
		onyx_job_free(j);
		return -1;
	}
	if (e != NULL) {			/* a stream on the origin's connection */
		j->proto = "h2";
		h2_queue(e, j);
		kapi_unlock(&onyx_h2_lk);
		c->job = j;
		return 1;
	}
	/* the first download to an https origin not known to be http/1.1 only: HTTP/2 offered,
	 * the origin's other fetches wait for its answer */
	if (https && onyx_h2_enabled() && port != 0) {
		kapi_lock(&onyx_h2_lk);
		if (h2_find(host, port) == NULL && !h1_only(host, port)) {
			int i;
			for (i = 0; i < ONYX_H2_MAX && onyx_h2s[i] != NULL; i++)
				;
			if (i == ONYX_H2_MAX)
				i = h2_evict(false);	/* (-1: none idle -- HTTP/1.1) */
			if (i >= 0) {
				e = calloc(1, sizeof *e);
				if (e != NULL) {
					snprintf(e->host, sizeof e->host, "%s", host);
					e->port = port;
					e->state = H2_CONNECTING;
					e->idle_at = kapi_get_ticks();
					onyx_h2s[i] = e;
					j->h2open = e;
				}
			}
		}
		kapi_unlock(&onyx_h2_lk);
	}
	j->threaded = true;
	__atomic_add_fetch(&onyx_threads, 1, __ATOMIC_SEQ_CST);
	if (kapi_thread_create(fetch_onyx_worker, j, 0, "fetch") < 0) {
		__atomic_sub_fetch(&onyx_threads, 1, __ATOMIC_SEQ_CST);
		if (j->h2open != NULL) {
			h2_retire(j->h2open);
			free(j->h2open);
		}
		onyx_job_free(j);
		return -1;
	}
	c->job = j;
	onyx_workers++;
	return 1;
}

/* Onyx (docs/06 §41): the sockets the downloads hold now, for net:minute */
static void onyx_nt_sockets(int *workers, int *pool, int *h2, char *list, size_t cap)
{
	size_t n = 0;
	int i;

	list[0] = '\0';
	*workers = onyx_workers;
	kapi_lock(&onyx_pool_lk);
	*pool = onyx_pool_n;
	for (i = 0; i < onyx_pool_n && n + 80 < cap; i++)
		n += (size_t) snprintf(list + n, cap - n, "%s%.60s (kept)", n ? ", " : "",
				onyx_pool[i].host);
	kapi_unlock(&onyx_pool_lk);
	*h2 = 0;
	kapi_lock(&onyx_h2_lk);
	for (i = 0; i < ONYX_H2_MAX; i++)
		if (onyx_h2s[i] != NULL && !onyx_h2s[i]->dead) {
			(*h2)++;
			if (n + 80 < cap)
				n += (size_t) snprintf(list + n, cap - n, "%s%.60s (h2, %d streams)",
						n ? ", " : "", onyx_h2s[i]->host,
						onyx_h2s[i]->nstreams);
		}
	kapi_unlock(&onyx_h2_lk);
}

/* Onyx (docs/06 §41): every 10 s -- the connections kept alive and idle for 10 s closed (they
 * were closed only when the next request came: a page gone quiet kept its sockets, of the
 * kernel's 16 for every app, the remote desktop's and telnet's too), and, the perf log on, a
 * minute gone by printed (net:minute) even when nothing is fetched */
static void onyx_nt_timer(void *p)
{
	struct onyx_conn none;

	(void) p;
	if (onyx_quit)
		return;
	(void) pool_take("", 0, false, false, &none);	/* (no such host: the stale ones closed) */
	onyx_nt_tick(false);
	guit->misc->schedule(10000, onyx_nt_timer, NULL);
}

/* ---- Onyx: <link rel=preconnect> / <link rel=dns-prefetch> ------------------------------
 * A preconnect opens the origin's connection in a thread of its own while the page is parsed:
 * HTTP/2 offered as a first download would (the entry is H2_CONNECTING meanwhile: the
 * origin's fetches wait for it, then go as its streams; idle, it closes after 30 s like any),
 * an http/1.1 one goes to the pool. A dns-prefetch only resolves the name (the kernel caches
 * the answer). Few at once (the kernel's sockets): 2 in flight, an HTTP/2 slot free (none
 * evicted for it), the origin not already connected. */
#define ONYX_PRECONNECT_MAX	2

struct onyx_preconn {
	char host[256];
	unsigned port;
	bool tls, dns_only;
	struct onyx_h2 *e;		/* HTTP/2 offered: its entry (H2_CONNECTING) */
};

static volatile int onyx_preconns;

static int onyx_preconnect_worker(void *arg)
{
	struct onyx_preconn *p = arg;
	struct onyx_conn k;
	uint64_t t0 = onyx_perf_now();
	const char *what = "failed";

	if (p->dns_only) {
		char ip[64];
		what = KT->net_resolve != 0 && kapi_net_resolve(p->host, ip, sizeof ip) > 0 ?
			"resolved" : "not resolved";
	} else if (!conn_open(&k, p->host, p->port, p->tls, false, NULL, p->e != NULL)) {
		conn_close(&k);
		if (p->e != NULL) {
			h2_retire(p->e);
			free(p->e);
		}
	} else if (p->e != NULL && onyx_nstls_alpn(k.ts) != NULL &&
			strcmp(onyx_nstls_alpn(k.ts), "h2") == 0) {
		struct onyx_h2 *e = p->e;
		e->ts = k.ts;
		if (!h2_session_new(e)) {
			h2_retire(e);
			free(e);
			conn_close(&k);
		} else {
			kapi_lock(&onyx_h2_lk);
			e->state = H2_READY;
			kapi_unlock(&onyx_h2_lk);
			if (onyx_perf_on())
				fprintf(stderr, "ONYX-PERF net:preconnect %s:%u h2 %lu us\n", p->host,
						p->port, (unsigned long) (onyx_perf_now() - t0));
			__atomic_sub_fetch(&onyx_preconns, 1, __ATOMIC_SEQ_CST);
			onyx_post_wake();		/* (the fetches waiting for it) */
			free(p);
			h2_run(e);			/* (the connection's life; e freed) */
			__atomic_sub_fetch(&onyx_threads, 1, __ATOMIC_SEQ_CST);
			return 0;
		}
	} else {
		if (p->e != NULL) {		/* (http/1.1 chosen: noted, the connection kept) */
			h1_only_add(p->host, p->port);
			h2_retire(p->e);
			free(p->e);
		}
		pool_put(&k);
		what = "http/1.1";
	}
	if (onyx_perf_on())
		fprintf(stderr, "ONYX-PERF net:preconnect %s:%u %s %lu us\n", p->host, p->port,
				what, (unsigned long) (onyx_perf_now() - t0));
	__atomic_sub_fetch(&onyx_preconns, 1, __ATOMIC_SEQ_CST);
	onyx_post_wake();
	free(p);
	__atomic_sub_fetch(&onyx_threads, 1, __ATOMIC_SEQ_CST);
	return 0;
}

/* The UI thread (html/css.c: the page's <link>) */
void onyx_fetch_preconnect(const char *url, bool dns_only)
{
	static struct { char host[256]; unsigned port; } done[16];
	static unsigned done_n;
	struct onyx_preconn *p;
	const char *path;
	bool tls = strncasecmp(url, "https:", 6) == 0;
	unsigned port, i;
	int s;

	if (onyx_quit || !onyx_threads_ok() || getenv("NS_NOPRECONNECT") != NULL ||
	    (!tls && strncasecmp(url, "http:", 5) != 0) ||
	    __atomic_load_n(&onyx_preconns, __ATOMIC_SEQ_CST) >= ONYX_PRECONNECT_MAX)
		return;
	p = calloc(1, sizeof *p);
	if (p == NULL)
		return;
	if (!onyx_split_url(url, p->host, sizeof p->host, &port, &path, tls ? 443 : 80))
		goto no;
	p->port = port;
	p->tls = tls;
	p->dns_only = dns_only;
	for (i = 0; i < 16; i++)		/* (once per origin: the pages repeat them) */
		if (done[i].port == port && strcasecmp(done[i].host, p->host) == 0)
			goto no;
	if (!dns_only) {
		struct onyx_conn *q;
		bool pooled = false;
		kapi_lock(&onyx_pool_lk);
		for (s = 0; s < onyx_pool_n; s++) {
			q = &onyx_pool[s];
			if (q->port == port && q->tls == tls && strcasecmp(q->host, p->host) == 0)
				pooled = true;
		}
		kapi_unlock(&onyx_pool_lk);
		if (pooled)
			goto no;
		if (tls && onyx_h2_enabled()) {
			kapi_lock(&onyx_h2_lk);
			if (h2_find(p->host, port) != NULL) {
				kapi_unlock(&onyx_h2_lk);
				goto no;
			}
			if (!h1_only(p->host, port)) {
				for (s = 0; s < ONYX_H2_MAX && onyx_h2s[s] != NULL; s++)
					;
				if (s == ONYX_H2_MAX) {		/* (no slot free: not worth one) */
					kapi_unlock(&onyx_h2_lk);
					goto no;
				}
				p->e = calloc(1, sizeof *p->e);
				if (p->e != NULL) {
					snprintf(p->e->host, sizeof p->e->host, "%s", p->host);
					p->e->port = port;
					p->e->state = H2_CONNECTING;
					p->e->idle_at = kapi_get_ticks();
					onyx_h2s[s] = p->e;
				}
			}
			kapi_unlock(&onyx_h2_lk);
		}
	}
	i = done_n++ % 16;
	snprintf(done[i].host, sizeof done[i].host, "%s", p->host);
	done[i].port = port;
	__atomic_add_fetch(&onyx_preconns, 1, __ATOMIC_SEQ_CST);
	__atomic_add_fetch(&onyx_threads, 1, __ATOMIC_SEQ_CST);
	if (kapi_thread_create(onyx_preconnect_worker, p, 0, "preconnect") < 0) {
		__atomic_sub_fetch(&onyx_threads, 1, __ATOMIC_SEQ_CST);
		__atomic_sub_fetch(&onyx_preconns, 1, __ATOMIC_SEQ_CST);
		if (p->e != NULL) {
			h2_retire(p->e);
			free(p->e);
		}
		goto no;
	}
	return;
no:
	free(p);
}

/* The UI thread: the fetch goes (aborted, freed, redirected) while its job may still run. */
static void onyx_job_drop(struct fetch_onyx_context *c)
{
	struct onyx_job *j = c->job;
	bool done;
	if (j == NULL) return;
	c->job = NULL;
	if (j->threaded)
		onyx_workers--;
	kapi_lock(&j->lk);
	done = j->state != JOB_RUNNING;
	if (!done) { j->cancel = 1; j->orphan = 1; }	/* the worker frees it when it ends */
	kapi_unlock(&j->lk);
	if (done) onyx_job_free(j);
}

/* The UI thread: body bytes to the core, decoded when the response is encoded (Onyx:
 * gzip / deflate, br, zstd -- onyx_decode). last: no more bytes will come. */
static void onyx_data(struct fetch_onyx_context *c, const uint8_t *b, size_t n, bool last)
{
	fetch_msg msg;

	if (c->enc == ENC_NONE) {
		if (n > 0 && !c->aborted) {
			msg.type = FETCH_DATA;
			msg.data.header_or_data.buf = b;
			msg.data.header_or_data.len = n;
			fetch_onyx_send(&msg, c);
			c->delivered += n;
		}
		return;
	}
	while (!c->dec_end && !c->aborted) {
		static uint8_t out[32768];	/* (the UI thread's only) */
		size_t used, made;
		int more = onyx_decode(c, b, n, &used, out, sizeof out, &made, last);
		b += used;
		n -= used;
		if (made > 0) {
			msg.type = FETCH_DATA;
			msg.data.header_or_data.buf = out;
			msg.data.header_or_data.len = made;
			fetch_onyx_send(&msg, c);
			c->delivered += made;
		}
		if (!more)
			break;
	}
}

/* Onyx (docs/06 §41): a request ended, in net:minute's tally */
static void onyx_nt_done(struct fetch_onyx_context *c, struct onyx_job *j, bool failed)
{
	lwc_string *h;
	unsigned long long out;

	if (!onyx_perf_on())
		return;
	h = nsurl_get_component(c->url, NSURL_HOST);
	out = failed ? 0 : strlen(nsurl_access(c->url)) +	/* (~ the request) */
			(c->hdrs != NULL ? strlen(c->hdrs) : 0) +
			(c->body != NULL ? strlen(c->body) : 0) + 200;
	onyx_nt_request(h != NULL ? lwc_string_data(h) : NULL, c->script, failed, c->retries > 0,
			j->total, out);
	if (h != NULL)
		lwc_string_unref(h);
	onyx_nt_tick(false);
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
		if (onyx_job_start(c) < 0) c->nothread = true;		/* the state machine */
		return false;
	}

	kapi_lock(&j->lk);
	head = c->head_done ? NULL : j->head;
	headlen = j->headlen;
	b = j->buf;
	n = j->len;
	done = j->state != JOB_RUNNING;
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
		if (!onyx_head(c, head, headlen, cenc, sizeof cenc))
			c->redirected = true;
		else
			onyx_decoder_start(c, cenc);	/* (Onyx: gzip, deflate, br, zstd) */
		free(head);
	}
	if (c->redirected || c->aborted) {
		onyx_nt_done(c, j, false);
		if (c->not_modified && onyx_perf_on())	/* (Onyx: the perf log) */
			fprintf(stderr, "ONYX-PERF net:done %s %lu us ttfb %lu us %s%s 0 bytes "
					"revalidated (304)\n", nsurl_access(c->url),
					(unsigned long) (onyx_perf_now() - j->t0),
					(unsigned long) (j->t_head != 0 ? j->t_head - j->t0 : 0),
					j->proto != NULL ? j->proto : "?",
					j->reused ? " (kept connection)" : "");
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
	if (j->state == JOB_RETRY && !c->head_done && c->retries < 3 && !c->aborted) {
		/* Onyx: HTTP/2 -- its stream refused or its connection gone before the answer:
		 * started again at the next poll (a new connection, or HTTP/1.1) */
		c->retries++;
		onyx_job_drop(c);
		return false;
	}
	onyx_nt_done(c, j, !c->head_done);
	if (onyx_perf_on()) {
		unsigned long us = (unsigned long) (onyx_perf_now() - j->t0);
		fprintf(stderr, "ONYX-PERF net:done %s %lu us ttfb %lu us %s%s %lu bytes%s\n",
				nsurl_access(c->url), us,
				(unsigned long) (j->t_head != 0 ? j->t_head - j->t0 : 0),
				j->proto != NULL ? j->proto : "?", j->reused ? " (kept connection)" : "",
				(unsigned long) j->total,
				c->status == 304 ? " revalidated (304)" : "");
	}
	if (!c->head_done && j->chain != NULL) {
		onyx_cert_error(c, j->chain);		/* (Onyx) */
	} else if (!c->head_done) {
		fetch_onyx_error(c, j->err != NULL ? j->err : "Empty response");
	} else if (!c->aborted) {
		if (c->enc != ENC_NONE)
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

/* Onyx: the app ends (netsurf_exit -> fetcher_quit -> finalise) -- its sockets closed now, not
 * when the kernel reaps the process: the kernel has 16 in all, and a NetSurf launched again
 * soon after found them taken (its connects failed and waited: 10 s loads). The kept
 * connections closed (TLS close_notify, then the socket), the downloads and the HTTP/2
 * connections told to stop, the WebSockets too; their threads waited for 300 ms at most. */
static void onyx_fetch_shutdown(void)
{
	struct fetch_onyx_context *c;
	int ws = 0, ms = 0;
	bool again = true;

	if (onyx_quit)
		return;
	onyx_nt_tick(true);	/* (Onyx: the last net:minute, docs/06 §41) */
	onyx_quit = 1;
	for (c = ring; c != NULL; c = c->r_next) {
		if (c->job != NULL) {
			kapi_lock(&c->job->lk);
			c->job->cancel = 1;
			kapi_unlock(&c->job->lk);
		}
		if (c->r_next == ring)
			break;
	}
	if (onyx_threads_ok()) {
		int i;
		kapi_lock(&onyx_h2_lk);
		for (i = 0; i < ONYX_H2_MAX; i++)
			if (onyx_h2s[i] != NULL) {
				onyx_h2s[i]->wake++;
				kapi_wake_word(&onyx_h2s[i]->wake);
			}
		kapi_unlock(&onyx_h2_lk);
	}
	onyx_sockets_free();
	while (again && ms < 300) {
		ws = onyx_ws_shutdown();
		again = ws > 0 || __atomic_load_n(&onyx_threads, __ATOMIC_SEQ_CST) > 0;
		if (again) {
			kapi_msleep(10);
			ms += 10;
		}
	}
	onyx_sockets_free();		/* (what the threads kept meanwhile) */
	onyx_fetch_save_state();
	if (onyx_perf_on())
		fprintf(stderr, "ONYX-PERF net:shutdown %d ms, %d download / connection threads "
				"and %d WebSocket ones still running\n", ms,
				__atomic_load_n(&onyx_threads, __ATOMIC_SEQ_CST), ws);
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
