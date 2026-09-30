/*
 * onyx_ws.c -- Onyx: WebSocket (RFC 6455) and event streams (EventSource) for NetSurf's
 * scripts, each connection in a THREAD of its own (kernel v67), outside the fetch queue and
 * the low-level cache: a socket open for hours takes no download slot, keeps no body in
 * memory and has no idle timeout. See onyx_ws.h.
 *
 * The thread does all the network: the DNS and the connect (in the downloads' turn:
 * onyx_fetch_connect, "the connects one at a time"), the TLS handshake (onyx_nstls, its reads
 * not blocking), the HTTP handshake, then the frames -- masked on the way out, unmasked,
 * reassembled (fragments), inflated (permessage-deflate, zlib) and checked (UTF-8, the
 * protocol's rules, the close codes) on the way in; pings answered; the closing handshake.
 * Each message becomes an event the UI thread takes (onyx_ws_take) after a kapi_post; the
 * messages to send wait in a queue the thread empties (it is woken by kapi_wake_word).
 * No NetSurf call here: the caller (quickjs/qjs_net.c) reads the cookies, builds the
 * headers and gives the events to the scripts.
 *
 * When there is nothing to do the thread sleeps -- 1 ms after some traffic, doubling to
 * 50 ms (a socket's recv does not block on Onyx: this is its wait), and a message to send
 * wakes it at once.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

#include <zlib.h>

#include "kapi.h"
#include "onyx_nstls.h"
#include "onyx_ws.h"

#define OWS_MAX_MSG	(64u * 1024 * 1024)	/* a message past it: close 1009 */
#define OWS_MAX_HEAD	(64 * 1024)
#define OWS_HEAD_TICKS	3000			/* the handshake's answer: 30 s */
#define OWS_CLOSE_TICKS	500			/* the peer's close frame: 5 s */
#define OWS_READ	32768			/* a read (a whole gathered segment) */

/* ---- SHA-1 and base64 (Sec-WebSocket-Accept) ------------------------------------------ */

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void sha1_block(uint32_t h[5], const uint8_t *p)
{
	uint32_t w[80], a, b, c, d, e, t;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = (uint32_t) p[i * 4] << 24 | (uint32_t) p[i * 4 + 1] << 16 |
			(uint32_t) p[i * 4 + 2] << 8 | p[i * 4 + 3];
	for (; i < 80; i++)
		w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];
	for (i = 0; i < 80; i++) {
		uint32_t f, k;
		if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
		else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
		else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
		else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
		t = rol(a, 5) + f + e + k + w[i];
		e = d; d = c; c = rol(b, 30); b = a; a = t;
	}
	h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

static void sha1(const uint8_t *m, size_t n, uint8_t out[20])
{
	uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
	uint8_t last[128];
	size_t i, rest;
	uint64_t bits = (uint64_t) n * 8;
	int k;

	for (i = 0; i + 64 <= n; i += 64)
		sha1_block(h, m + i);
	rest = n - i;
	memset(last, 0, sizeof last);
	memcpy(last, m + i, rest);
	last[rest] = 0x80;
	k = rest < 56 ? 64 : 128;
	for (i = 0; i < 8; i++)
		last[k - 1 - i] = (uint8_t) (bits >> (8 * i));
	sha1_block(h, last);
	if (k == 128)
		sha1_block(h, last + 64);
	for (i = 0; i < 5; i++) {
		out[i * 4] = (uint8_t) (h[i] >> 24);
		out[i * 4 + 1] = (uint8_t) (h[i] >> 16);
		out[i * 4 + 2] = (uint8_t) (h[i] >> 8);
		out[i * 4 + 3] = (uint8_t) h[i];
	}
}

static void b64(const uint8_t *in, size_t n, char *out)
{
	static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t i;

	for (i = 0; i + 2 < n; i += 3) {
		uint32_t v = (uint32_t) in[i] << 16 | (uint32_t) in[i + 1] << 8 | in[i + 2];
		*out++ = T[v >> 18]; *out++ = T[(v >> 12) & 63];
		*out++ = T[(v >> 6) & 63]; *out++ = T[v & 63];
	}
	if (n - i == 1) {
		uint32_t v = (uint32_t) in[i] << 16;
		*out++ = T[v >> 18]; *out++ = T[(v >> 12) & 63]; *out++ = '='; *out++ = '=';
	} else if (n - i == 2) {
		uint32_t v = (uint32_t) in[i] << 16 | (uint32_t) in[i + 1] << 8;
		*out++ = T[v >> 18]; *out++ = T[(v >> 12) & 63]; *out++ = T[(v >> 6) & 63];
		*out++ = '=';
	}
	*out = '\0';
}

/* ---- UTF-8 (a text message, a close reason) --------------------------------------------- */

static bool utf8_ok(const uint8_t *s, size_t n)
{
	size_t i = 0;

	while (i < n) {
		uint8_t c = s[i];
		uint32_t cp;
		int k;
		if (c < 0x80) { i++; continue; }
		if (c >= 0xC2 && c <= 0xDF) { k = 1; cp = c & 0x1F; }
		else if (c >= 0xE0 && c <= 0xEF) { k = 2; cp = c & 0x0F; }
		else if (c >= 0xF0 && c <= 0xF4) { k = 3; cp = c & 0x07; }
		else return false;
		if (i + (size_t) k >= n)
			return false;		/* (cut short) */
		for (int j = 1; j <= k; j++) {
			if ((s[i + j] & 0xC0) != 0x80)
				return false;
			cp = cp << 6 | (s[i + j] & 0x3F);
		}
		if ((k == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) ||
		    (k == 3 && (cp < 0x10000 || cp > 0x10FFFF)))
			return false;
		i += (size_t) k + 1;
	}
	return true;
}

/* ---- URLs ------------------------------------------------------------------------------- */

struct ows_url {
	bool tls;
	char scheme[8];
	char host[256];
	unsigned port;
	char hostport[300];	/* the Host header's */
	const char *path;	/* into the URL: its path and query (a fragment cut), or "/" */
	size_t pathlen;
};

static bool ows_parse_url(const char *url, struct ows_url *u)
{
	const char *p;
	size_t i = 0;
	unsigned def;

	memset(u, 0, sizeof *u);
	if (strncasecmp(url, "wss://", 6) == 0 || strncasecmp(url, "https://", 8) == 0) {
		u->tls = true; def = 443;
	} else if (strncasecmp(url, "ws://", 5) == 0 || strncasecmp(url, "http://", 7) == 0) {
		def = 80;
	} else {
		return false;
	}
	snprintf(u->scheme, sizeof u->scheme, "%s", u->tls ? "https" : "http");
	p = strstr(url, "://") + 3;
	if (*p == '[') {			/* an IPv6 literal */
		p++;
		while (*p && *p != ']' && i + 1 < sizeof u->host) u->host[i++] = *p++;
		if (*p == ']') p++;
	} else {
		while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#' &&
		       i + 1 < sizeof u->host)
			u->host[i++] = *p++;
	}
	u->host[i] = '\0';
	u->port = def;
	if (*p == ':') {
		unsigned v = 0;
		p++;
		while (*p >= '0' && *p <= '9') v = v * 10 + (unsigned) (*p++ - '0');
		if (v > 0 && v < 65536) u->port = v;
	}
	while (*p && *p != '/' && *p != '?' && *p != '#')
		p++;
	if (*p == '/' || *p == '?') {
		u->path = p;
		u->pathlen = strcspn(p, "#");
	} else {
		u->path = "/";
		u->pathlen = 1;
	}
	if (u->port == def)
		snprintf(u->hostport, sizeof u->hostport, "%s", u->host);
	else
		snprintf(u->hostport, sizeof u->hostport, "%s:%u", u->host, u->port);
	return u->host[0] != '\0';
}

/* a redirect's Location against the URL it came from (malloc'd) */
static char *ows_join(const char *base, const char *loc)
{
	struct ows_url u;
	size_t n;
	char *r;

	if (strstr(loc, "://") != NULL)
		return strdup(loc);
	if (!ows_parse_url(base, &u))
		return NULL;
	n = strlen(base) + strlen(loc) + 16;
	r = malloc(n);
	if (r == NULL)
		return NULL;
	if (loc[0] == '/' && loc[1] == '/') {
		snprintf(r, n, "%s:%s", u.scheme, loc);
	} else if (loc[0] == '/') {
		snprintf(r, n, "%s://%s%s", u.scheme, u.hostport, loc);
	} else {
		size_t dir = strcspn(u.path, "?");
		while (dir > 0 && u.path[dir - 1] != '/')
			dir--;
		snprintf(r, n, "%s://%s%.*s%s", u.scheme, u.hostport, (int) (dir ? dir : 1),
			dir ? u.path : "/", loc);
	}
	return r;
}

/* ---- the connection --------------------------------------------------------------------- */

struct ows_conn {
	bool tls;
	int sock;
	onyx_tls_sess *ts;
};

static bool oc_open(struct ows_conn *c, const struct ows_url *u)
{
	c->tls = u->tls;
	c->ts = NULL;
	c->sock = onyx_fetch_connect(u->host, u->port);
	if (c->sock < 0)
		return false;
	if (u->tls) {
		/* (the socket is the session's) -- Onyx: the certificate checked as the fetches
		 * check it, a host the user accepted let through (onyx_fetch_insecure_host) */
		c->ts = onyx_nstls_connect(c->sock, u->host, ONYX_TLS_VERIFY |
			(onyx_fetch_insecure_host(u->host) ? ONYX_TLS_INSECURE : 0), NULL);
		c->sock = -1;
		if (c->ts == NULL)
			return false;
	}
	return true;
}

static int oc_send(struct ows_conn *c, const void *b, size_t n)
{
	return c->tls ? onyx_nstls_send(c->ts, b, (int) n) : kapi_tcp_send(c->sock, b, (unsigned) n);
}

/* >0 bytes, 0 nothing yet, <0 closed */
static int oc_recv(struct ows_conn *c, void *b, size_t n)
{
	return c->tls ? onyx_nstls_recv(c->ts, b, (int) n) : kapi_tcp_recv(c->sock, b, (unsigned) n);
}

static void oc_close(struct ows_conn *c)
{
	if (c->ts != NULL)
		onyx_nstls_close(c->ts);
	else if (c->sock >= 0)
		kapi_tcp_close(c->sock);
	c->ts = NULL;
	c->sock = -1;
}

/* the bytes read and not yet used */
struct ows_rd {
	uint8_t *b;
	size_t len, cap;
	bool closed;
};

static bool rd_room(struct ows_rd *r, size_t n)
{
	if (r->cap - r->len >= n)
		return true;
	{
		size_t cap = r->cap ? r->cap : 65536;
		uint8_t *b;
		while (cap - r->len < n) cap *= 2;
		b = realloc(r->b, cap);
		if (b == NULL)
			return false;
		r->b = b;
		r->cap = cap;
	}
	return true;
}

static void rd_eat(struct ows_rd *r, size_t n)
{
	memmove(r->b, r->b + n, r->len - n);
	r->len -= n;
}

/* a read without waiting: >0 bytes, 0 nothing, <0 closed / no memory */
static int rd_try(struct ows_rd *r, struct ows_conn *c)
{
	int n;

	if (r->closed || !rd_room(r, OWS_READ))
		return -1;
	n = oc_recv(c, r->b + r->len, OWS_READ);
	if (n > 0)
		r->len += (size_t) n;
	else if (n < 0)
		r->closed = true;
	return n;
}

/* the response's head (interim 1xx ones but 101 skipped): malloc'd, its lines CRLF-separated,
 * taken out of the buffer; *status. NULL: closed, timeout, cancelled (*cancel set). */
static char *rd_head(struct ows_rd *r, struct ows_conn *c, volatile int *cancel, int *status)
{
	unsigned t0 = kapi_get_ticks();
	size_t scan = 0;

	for (;;) {
		uint8_t *e = NULL;
		size_t i;
		for (i = scan; i + 3 < r->len; i++)
			if (r->b[i] == '\r' && r->b[i + 1] == '\n' && r->b[i + 2] == '\r' &&
			    r->b[i + 3] == '\n') { e = r->b + i; break; }
		if (e != NULL) {
			size_t hl = (size_t) (e - r->b);
			char *h = malloc(hl + 1);
			int st = 0;
			if (h == NULL)
				return NULL;
			memcpy(h, r->b, hl);
			h[hl] = '\0';
			rd_eat(r, hl + 4);
			{
				const char *sp = strchr(h, ' ');
				if (strncmp(h, "HTTP/", 5) == 0 && sp != NULL)
					st = atoi(sp + 1);
			}
			if (st >= 100 && st < 200 && st != 101) {	/* (100 Continue...) */
				free(h);
				scan = 0;
				continue;
			}
			*status = st;
			return h;
		}
		scan = r->len > 3 ? r->len - 3 : 0;
		if (r->len > OWS_MAX_HEAD)
			return NULL;
		if (cancel != NULL && *cancel)
			return NULL;
		{
			int n = rd_try(r, c);
			if (n < 0)
				return NULL;
			if (n == 0) {
				if (kapi_get_ticks() - t0 > OWS_HEAD_TICKS)
					return NULL;
				kapi_msleep(2);
			}
		}
	}
}

/* exported: the nth value of a header in a head */
char *onyx_ws_head_get(const char *head, const char *name, int nth)
{
	size_t nl = strlen(name);
	const char *l = head;

	while (l != NULL && *l != '\0') {
		if (strncasecmp(l, name, nl) == 0 && l[nl] == ':') {
			if (nth-- == 0) {
				const char *v = l + nl + 1, *e;
				char *r;
				while (*v == ' ' || *v == '\t') v++;
				e = strstr(v, "\r\n");
				if (e == NULL) e = v + strlen(v);
				while (e > v && (e[-1] == ' ' || e[-1] == '\t')) e--;
				r = malloc((size_t) (e - v) + 1);
				if (r != NULL) {
					memcpy(r, v, (size_t) (e - v));
					r[e - v] = '\0';
				}
				return r;
			}
		}
		l = strstr(l, "\r\n");
		if (l != NULL) l += 2;
	}
	return NULL;
}

/* whether a comma-separated header value holds token (case-insensitively) */
static bool ows_has_token(const char *v, const char *token)
{
	size_t n = strlen(token);

	while (v != NULL && *v != '\0') {
		while (*v == ' ' || *v == '\t' || *v == ',') v++;
		if (strncasecmp(v, token, n) == 0 && (v[n] == '\0' || v[n] == ',' ||
				v[n] == ' ' || v[n] == '\t' || v[n] == ';'))
			return true;
		v = strchr(v, ',');
	}
	return false;
}

/* ---- a chunked body, undone as it comes --------------------------------------------------- */

struct ows_chunk {
	int st;			/* 0 the size line, 1 its data, 2 the CRLF after, 3 the end */
	uint64_t left;
	char line[40];
	int ll;
};

/* b[0..n) undone in place: the body's bytes (fewer), their count */
static size_t ows_dechunk(struct ows_chunk *k, uint8_t *b, size_t n)
{
	size_t i = 0, o = 0;

	while (i < n) {
		switch (k->st) {
		case 0: {
			uint8_t c = b[i++];
			if (c == '\n') {
				k->line[k->ll] = '\0';
				k->left = strtoull(k->line, NULL, 16);
				k->ll = 0;
				k->st = k->left == 0 ? 3 : 1;
			} else if (c != '\r' && k->ll < (int) sizeof k->line - 1) {
				k->line[k->ll++] = (char) c;
			}
			break;
		}
		case 1: {
			size_t m = n - i;
			if ((uint64_t) m > k->left) m = (size_t) k->left;
			memmove(b + o, b + i, m);
			o += m;
			i += m;
			k->left -= m;
			if (k->left == 0) k->st = 2;
			break;
		}
		case 2:
			if (b[i++] == '\n') k->st = 0;
			break;
		default:
			i = n;
			break;
		}
	}
	return o;
}

/* ---- the connections (the UI thread's registry) --------------------------------------------- */

struct ows_out {
	struct ows_out *next;
	int opcode;
	size_t len;
	uint8_t data[];
};

struct onyx_ws {
	int id, mode;
	char *url, *hdrs;
	void (*notify)(void *pw);
	void *pw;
	struct onyx_ws *reg_next;		/* (the UI thread's list) */

	volatile int lk;			/* kapi_lock: everything below */
	struct ows_out *out, *out_tail;		/* to send */
	size_t buffered;
	int close_req, close_code;
	size_t close_rlen;
	char close_reason[124];
	struct onyx_ws_event *ev, *ev_tail;	/* come */
	int posted;
	volatile unsigned wake;			/* (a word the thread waits on) */
	volatile int cancel, orphan, done;
};

static struct onyx_ws *ows_all;
static int ows_next_id;

/* a post, on the UI thread: the connection's owner told (if it is still there) */
static void ows_posted(void *ctx, long id)
{
	struct onyx_ws *w;

	(void) ctx;
	for (w = ows_all; w != NULL; w = w->reg_next)
		if (w->id == (int) id) {
			if (w->notify != NULL)
				w->notify(w->pw);
			return;
		}
}

static void ows_destroy(struct onyx_ws *w)
{
	while (w->out != NULL) {
		struct ows_out *o = w->out;
		w->out = o->next;
		free(o);
	}
	while (w->ev != NULL) {
		struct onyx_ws_event *e = w->ev;
		w->ev = e->next;
		free(e->head);
		free(e);
	}
	free(w->url);
	free(w->hdrs);
	free(w);
}

/* an event for the UI thread (head: malloc'd, the event's) */
static void ows_emit(struct onyx_ws *w, int type, int code, int clean, char *head,
		const void *data, size_t len)
{
	struct onyx_ws_event *e = malloc(sizeof *e + len + 1);
	bool post;

	if (e == NULL) {
		free(head);
		return;
	}
	e->next = NULL;
	e->type = type;
	e->code = code;
	e->clean = clean;
	e->head = head;
	e->len = len;
	if (len > 0)
		memcpy(e->data, data, len);
	e->data[len] = '\0';
	kapi_lock(&w->lk);
	if (w->ev_tail != NULL) w->ev_tail->next = e; else w->ev = e;
	w->ev_tail = e;
	post = !w->posted && !w->orphan;
	if (post) w->posted = 1;
	kapi_unlock(&w->lk);
	if (post)
		kapi_post(ows_posted, NULL, w->id);
}

static void ows_fail(struct onyx_ws *w, int status, const char *why)
{
	ows_emit(w, OWS_ERROR, status, 0, NULL, why, strlen(why));
	if (w->mode == ONYX_WS_SOCKET)
		ows_emit(w, OWS_CLOSE, 1006, 0, NULL, NULL, 0);
	else
		ows_emit(w, OWS_CLOSE, 0, 0, NULL, NULL, 0);
}

/* the thread's nap when nothing happened: woken by a message to send */
static void ows_nap(struct onyx_ws *w, unsigned seen, unsigned ms)
{
	if (kapi_wait_word(&w->wake, seen, ms) < 0)
		kapi_msleep(ms);
}

/* ---- WebSocket -------------------------------------------------------------------------------- */

static bool ows_random(void *b, unsigned n)
{
	static uint32_t x = 0x9e3779b9u;
	if (kapi_random(b, n) == (int) n)
		return true;
	for (unsigned i = 0; i < n; i++) {	/* (no hardware RNG: xorshift) */
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		((uint8_t *) b)[i] = (uint8_t) (x ^ kapi_get_ticks());
	}
	return true;
}

/* a frame out: masked (a client's always are) */
static bool ows_send_frame(struct ows_conn *c, int op, const uint8_t *p, size_t n)
{
	uint8_t *f = malloc(n + 14);
	uint8_t mask[4];
	size_t h = 0, i;
	bool ok;

	if (f == NULL)
		return false;
	f[h++] = (uint8_t) (0x80 | op);
	if (n < 126) {
		f[h++] = (uint8_t) (0x80 | n);
	} else if (n < 65536) {
		f[h++] = 0x80 | 126;
		f[h++] = (uint8_t) (n >> 8);
		f[h++] = (uint8_t) n;
	} else {
		f[h++] = 0x80 | 127;
		for (i = 0; i < 8; i++)
			f[h++] = (uint8_t) ((uint64_t) n >> (56 - 8 * i));
	}
	ows_random(mask, 4);
	memcpy(f + h, mask, 4);
	h += 4;
	for (i = 0; i < n; i++)
		f[h + i] = p[i] ^ mask[i & 3];
	ok = oc_send(c, f, h + n) >= 0;
	free(f);
	return ok;
}

static bool ows_send_close(struct ows_conn *c, int code, const char *reason, size_t rlen)
{
	uint8_t p[125];
	size_t n = 0;

	if (code > 0) {
		p[0] = (uint8_t) (code >> 8);
		p[1] = (uint8_t) code;
		n = 2;
		if (rlen > 123) rlen = 123;
		memcpy(p + 2, reason, rlen);
		n += rlen;
	}
	return ows_send_frame(c, 8, p, n);
}

static bool ows_close_code_ok(int code)
{
	return (code >= 1000 && code <= 1003) || (code >= 1007 && code <= 1014) ||
		(code >= 3000 && code <= 4999);
}

/* the server's answer to our extensions: false if it is not one we offered */
static bool ows_extensions(const char *v, bool *pmd, bool *srv_nct)
{
	const char *p = v;
	bool seen = false;

	*pmd = false;
	*srv_nct = false;
	if (v == NULL)
		return true;
	while (*p != '\0') {
		char tok[64];
		size_t n;
		while (*p == ' ' || *p == '\t' || *p == ';' || *p == ',') {
			if (*p == ',' && seen)
				return false;		/* (a second extension: not offered) */
			p++;
		}
		if (*p == '\0')
			break;
		n = strcspn(p, ";, \t");
		if (n >= sizeof tok)
			return false;
		memcpy(tok, p, n);
		tok[n] = '\0';
		p += n;
		while (*p == ' ' || *p == '\t') p++;
		if (!seen) {
			if (strcasecmp(tok, "permessage-deflate") != 0)
				return false;
			seen = true;
			*pmd = true;
			continue;
		}
		if (strcasecmp(tok, "server_no_context_takeover") == 0) {
			*srv_nct = true;
		} else if (strcasecmp(tok, "client_no_context_takeover") == 0) {
			/* (we send uncompressed messages: nothing to keep) */
		} else if (strncasecmp(tok, "server_max_window_bits", 22) == 0 ||
			   strncasecmp(tok, "client_max_window_bits", 22) == 0) {
			/* (=N: a smaller window is read by a 15-bit inflate too) */
			if (*p == '=') {
				p++;
				while (*p == '"' || (*p >= '0' && *p <= '9')) p++;
			}
		} else {
			return false;
		}
	}
	return true;
}

/* whether the server's protocol is one asked for (none asked: it must name none) */
static bool ows_protocol_ok(const char *hdrs, const char *chosen)
{
	char *asked = NULL;
	const char *l = hdrs;
	bool ok;

	while (l != NULL && *l != '\0') {
		if (strncasecmp(l, "Sec-WebSocket-Protocol:", 23) == 0) {
			const char *v = l + 23, *e = strstr(v, "\r\n");
			size_t n = e != NULL ? (size_t) (e - v) : strlen(v);
			asked = malloc(n + 1);
			if (asked != NULL) { memcpy(asked, v, n); asked[n] = '\0'; }
			break;
		}
		l = strstr(l, "\r\n");
		if (l != NULL) l += 2;
	}
	if (chosen == NULL || chosen[0] == '\0')
		ok = true;
	else
		ok = asked != NULL && ows_has_token(asked, chosen);
	free(asked);
	return ok;
}

/* a message's inflate (permessage-deflate): the bytes in *out (malloc'd) or false */
static bool ows_inflate(z_stream *zs, uint8_t *msg, size_t mlen, uint8_t **out, size_t *olen,
		bool *toobig)
{
	size_t cap = mlen * 3 + 1024, n = 0;
	uint8_t *o = malloc(cap);
	int ret;

	*toobig = false;
	if (o == NULL)
		return false;
	memcpy(msg + mlen, "\x00\x00\xff\xff", 4);	/* (the room was kept) */
	zs->next_in = msg;
	zs->avail_in = (uInt) (mlen + 4);
	for (;;) {
		if (cap - n < 4096) {
			uint8_t *p;
			if (cap * 2 > OWS_MAX_MSG + 8192) { *toobig = true; free(o); return false; }
			p = realloc(o, cap * 2);
			if (p == NULL) { free(o); return false; }
			o = p;
			cap *= 2;
		}
		zs->next_out = o + n;
		zs->avail_out = (uInt) (cap - n);
		ret = inflate(zs, Z_SYNC_FLUSH);
		n = cap - zs->avail_out;
		if (ret != Z_OK && ret != Z_BUF_ERROR && ret != Z_STREAM_END) {
			free(o);
			return false;
		}
		if (zs->avail_in == 0 && zs->avail_out > 0)
			break;
		if (ret == Z_BUF_ERROR && zs->avail_out > 0)
			break;
	}
	*out = o;
	*olen = n;
	return true;
}

static void ows_run_socket(struct onyx_ws *w)
{
	struct ows_url u;
	struct ows_conn c = { false, -1, NULL };
	struct ows_rd r = { NULL, 0, 0, false };
	uint8_t key[16], sum[20];
	char key64[32], want[32], *req = NULL, *head = NULL;
	size_t reqcap;
	int status = 0, n;
	bool pmd = false, srv_nct = false;
	/* the message being put together */
	uint8_t *msg = NULL;
	size_t mlen = 0, mcap = 0;
	int mop = 0;
	bool in_msg = false, mdeflate = false;
	z_stream zs;
	bool zs_on = false;
	/* the closing */
	bool close_sent = false;
	unsigned close_at = 0, nap = 1;
	int fail_code = 0;
	const char *fail_why = NULL;

	memset(&zs, 0, sizeof zs);
	if (!ows_parse_url(w->url, &u)) {
		ows_fail(w, 0, "bad URL");
		return;
	}
	if (!oc_open(&c, &u)) {
		oc_close(&c);
		if (!w->cancel)
			ows_fail(w, 0, "connection failed");
		return;
	}

	/* the handshake */
	ows_random(key, sizeof key);
	b64(key, sizeof key, key64);
	{
		char acc[80];
		snprintf(acc, sizeof acc, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key64);
		sha1((const uint8_t *) acc, strlen(acc), sum);
		b64(sum, 20, want);
	}
	reqcap = u.pathlen + strlen(w->hdrs != NULL ? w->hdrs : "") + 700;
	req = malloc(reqcap);
	if (req == NULL) {
		oc_close(&c);
		ows_fail(w, 0, "out of memory");
		return;
	}
	n = snprintf(req, reqcap, "GET %.*s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\n"
		"Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n"
		"Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits\r\n"
		"Pragma: no-cache\r\nCache-Control: no-cache\r\n%s\r\n",
		(int) u.pathlen, u.path, u.hostport, key64, w->hdrs != NULL ? w->hdrs : "");
	if (n <= 0 || (size_t) n >= reqcap || oc_send(&c, req, (size_t) n) < 0) {
		free(req);
		oc_close(&c);
		if (!w->cancel)
			ows_fail(w, 0, "connection failed");
		return;
	}
	free(req);
	head = rd_head(&r, &c, &w->cancel, &status);
	if (head == NULL) {
		oc_close(&c);
		free(r.b);
		if (!w->cancel)
			ows_fail(w, 0, "no handshake answer");
		return;
	}
	{
		const char *why = NULL;
		if (status != 101) {
			why = "the server refused the upgrade";
		} else {
			char *up = onyx_ws_head_get(head, "Upgrade", 0);
			char *co = onyx_ws_head_get(head, "Connection", 0);
			char *ac = onyx_ws_head_get(head, "Sec-WebSocket-Accept", 0);
			char *ex = onyx_ws_head_get(head, "Sec-WebSocket-Extensions", 0);
			char *pr = onyx_ws_head_get(head, "Sec-WebSocket-Protocol", 0);
			if (up == NULL || strcasecmp(up, "websocket") != 0)
				why = "no Upgrade: websocket";
			else if (co == NULL || !ows_has_token(co, "upgrade"))
				why = "no Connection: Upgrade";
			else if (ac == NULL || strcmp(ac, want) != 0)
				why = "a wrong Sec-WebSocket-Accept";
			else if (!ows_extensions(ex, &pmd, &srv_nct))
				why = "an extension not offered";
			else if (!ows_protocol_ok(w->hdrs, pr))
				why = "a subprotocol not asked for";
			free(up); free(co); free(ac); free(ex); free(pr);
		}
		if (why != NULL) {
			free(head);
			oc_close(&c);
			free(r.b);
			ows_fail(w, status == 101 ? 0 : status, why);
			return;
		}
	}
	ows_emit(w, OWS_OPEN, status, 0, head, NULL, 0);
	head = NULL;

	/* the messages */
	for (;;) {
		bool active = false;
		unsigned seen = w->wake;
		struct ows_out *q;
		int creq, ccode = 0;
		size_t crlen = 0;
		char creason[124];

		if (w->cancel)
			break;
		/* what the page sends */
		kapi_lock(&w->lk);
		q = w->out;
		w->out = w->out_tail = NULL;
		creq = w->close_req && !close_sent;
		if (creq) {
			ccode = w->close_code;
			crlen = w->close_rlen;
			memcpy(creason, w->close_reason, crlen);
		}
		kapi_unlock(&w->lk);
		while (q != NULL) {
			struct ows_out *o = q;
			q = o->next;
			if (!close_sent && !ows_send_frame(&c, o->opcode, o->data, o->len))
				r.closed = true;
			kapi_lock(&w->lk);
			w->buffered -= o->len;
			kapi_unlock(&w->lk);
			free(o);
			active = true;
		}
		if (creq) {
			ows_send_close(&c, ccode, creason, crlen);
			close_sent = true;
			close_at = kapi_get_ticks();
			active = true;
		}

		/* what the server sends */
		n = rd_try(&r, &c);
		if (n > 0)
			active = true;
		while (r.len >= 2 && fail_code == 0) {
			const uint8_t *b = r.b;
			int fin = b[0] >> 7, rsv = (b[0] >> 4) & 7, op = b[0] & 15;
			uint64_t plen = b[1] & 127;
			size_t hl = 2;
			const uint8_t *pl;

			if (b[1] & 0x80) { fail_code = 1002; fail_why = "a masked frame"; break; }
			if (plen == 126) {
				if (r.len < 4) break;
				plen = (uint64_t) b[2] << 8 | b[3];
				hl = 4;
			} else if (plen == 127) {
				if (r.len < 10) break;
				plen = 0;
				for (int i = 0; i < 8; i++) plen = plen << 8 | b[2 + i];
				hl = 10;
			}
			if (plen > OWS_MAX_MSG) { fail_code = 1009; fail_why = "a frame too big"; break; }
			if (r.len < hl + plen) {
				rd_room(&r, (size_t) (hl + plen - r.len));	/* (it will come) */
				break;
			}
			pl = b + hl;
			if ((rsv & 3) != 0 || ((rsv & 4) && (!pmd || op == 0 || op >= 8))) {
				fail_code = 1002; fail_why = "reserved bits set"; break;
			}
			if (op >= 8) {			/* control frames */
				if (!fin || plen > 125) { fail_code = 1002; fail_why = "a bad control frame"; break; }
				if (op == 9) {
					if (!close_sent)
						ows_send_frame(&c, 10, pl, (size_t) plen);
				} else if (op == 8) {
					int code = 1005;
					const char *reason = "";
					size_t rl = 0;
					if (plen == 1) { fail_code = 1002; fail_why = "a bad close frame"; break; }
					if (plen >= 2) {
						code = pl[0] << 8 | pl[1];
						reason = (const char *) pl + 2;
						rl = (size_t) plen - 2;
						if (!ows_close_code_ok(code)) { fail_code = 1002; fail_why = "a bad close code"; break; }
						if (!utf8_ok((const uint8_t *) reason, rl)) { fail_code = 1007; fail_why = "a bad close reason"; break; }
					}
					if (!close_sent) {	/* (the answer: the same code) */
						ows_send_close(&c, code == 1005 ? 0 : code, reason, rl);
						close_sent = true;
					}
					ows_emit(w, OWS_CLOSE, code, 1, NULL, reason, rl);
					rd_eat(&r, (size_t) (hl + plen));
					goto end;
				} else if (op != 10) {
					fail_code = 1002; fail_why = "an unknown opcode"; break;
				}
			} else {			/* data frames */
				if (op == 0 && !in_msg) { fail_code = 1002; fail_why = "a continuation alone"; break; }
				if (op != 0 && in_msg) { fail_code = 1002; fail_why = "a message in a message"; break; }
				if (op > 2) { fail_code = 1002; fail_why = "an unknown opcode"; break; }
				if (op != 0) {
					in_msg = true;
					mop = op;
					mlen = 0;
					mdeflate = (rsv & 4) != 0;
				}
				if (mlen + plen > OWS_MAX_MSG) { fail_code = 1009; fail_why = "a message too big"; break; }
				if (mlen + plen + 4 > mcap) {
					size_t cap = mcap ? mcap : 4096;
					uint8_t *m;
					while (cap < mlen + plen + 4) cap *= 2;
					m = realloc(msg, cap);
					if (m == NULL) { fail_code = 1009; fail_why = "out of memory"; break; }
					msg = m;
					mcap = cap;
				}
				memcpy(msg + mlen, pl, (size_t) plen);
				mlen += (size_t) plen;
				if (fin) {
					uint8_t *data = msg, *inf = NULL;
					size_t dlen = mlen;
					in_msg = false;
					if (mdeflate) {
						bool big;
						if (!zs_on) {
							if (inflateInit2(&zs, -15) != Z_OK) { fail_code = 1011; fail_why = "no inflate"; break; }
							zs_on = true;
						}
						if (!ows_inflate(&zs, msg, mlen, &inf, &dlen, &big)) {
							fail_code = big ? 1009 : 1007;
							fail_why = "a bad compressed message";
							break;
						}
						if (srv_nct)
							inflateReset(&zs);
						data = inf;
					}
					if (!close_sent) {
						if (mop == 1 && !utf8_ok(data, dlen)) {
							free(inf);
							fail_code = 1007; fail_why = "a text message not UTF-8";
							break;
						}
						ows_emit(w, mop == 1 ? OWS_TEXT : OWS_BINARY, 0, 0, NULL, data, dlen);
					}
					free(inf);
					if (mcap > 1024 * 1024) {	/* (a big message's room given back) */
						free(msg);
						msg = NULL;
						mcap = 0;
					}
				}
			}
			rd_eat(&r, (size_t) (hl + plen));
		}
		if (fail_code != 0) {
			if (!close_sent)
				ows_send_close(&c, fail_code, "", 0);
			ows_fail(w, 0, fail_why != NULL ? fail_why : "protocol error");
			goto end;
		}
		if (r.closed) {				/* (no close frame: not clean) */
			ows_emit(w, OWS_CLOSE, 1006, 0, NULL, NULL, 0);
			goto end;
		}
		if (close_sent && kapi_get_ticks() - close_at > OWS_CLOSE_TICKS) {
			ows_emit(w, OWS_CLOSE, 1006, 0, NULL, NULL, 0);
			goto end;
		}
		if (active) {
			nap = 1;
		} else {
			ows_nap(w, seen, nap);
			if (nap < 50) nap *= 2;
			if (nap > 50) nap = 50;
		}
	}
end:
	if (zs_on)
		inflateEnd(&zs);
	free(msg);
	free(r.b);
	oc_close(&c);
}

/* ---- event streams (EventSource) ------------------------------------------------------------------ */

static void ows_run_events(struct onyx_ws *w)
{
	char *url = strdup(w->url), *head = NULL, *req;
	struct ows_url u;
	struct ows_conn c = { false, -1, NULL };
	struct ows_rd r = { NULL, 0, 0, false };
	struct ows_chunk ck;
	int status = 0, redirects, n;
	bool chunked = false;
	long long clen = -1, got = 0;
	size_t pend = 0;			/* bytes of the body not yet given (no line end yet) */
	uint8_t *body = NULL;
	size_t bcap = 0;
	unsigned nap = 1;

	memset(&ck, 0, sizeof ck);
	for (redirects = 0; ; redirects++) {
		size_t reqcap;
		if (url == NULL || !ows_parse_url(url, &u)) {
			free(url);
			ows_fail(w, 0, "bad URL");
			return;
		}
		if (!oc_open(&c, &u)) {
			oc_close(&c);
			free(url);
			if (!w->cancel)
				ows_fail(w, 0, "connection failed");
			return;
		}
		reqcap = u.pathlen + strlen(w->hdrs != NULL ? w->hdrs : "") + 400;
		req = malloc(reqcap);
		n = req == NULL ? -1 : snprintf(req, reqcap, "GET %.*s HTTP/1.1\r\nHost: %s\r\n"
			"Accept: text/event-stream\r\nCache-Control: no-cache\r\n%s\r\n",
			(int) u.pathlen, u.path, u.hostport, w->hdrs != NULL ? w->hdrs : "");
		if (n <= 0 || (size_t) n >= reqcap || oc_send(&c, req, (size_t) n) < 0) {
			free(req);
			oc_close(&c);
			free(url);
			if (!w->cancel)
				ows_fail(w, 0, "connection failed");
			return;
		}
		free(req);
		r.len = 0;
		r.closed = false;
		head = rd_head(&r, &c, &w->cancel, &status);
		if (head == NULL) {
			oc_close(&c);
			free(url);
			free(r.b);
			if (!w->cancel)
				ows_fail(w, 0, "no answer");
			return;
		}
		if ((status == 301 || status == 302 || status == 303 || status == 307 ||
		     status == 308) && redirects < 5) {
			char *loc = onyx_ws_head_get(head, "Location", 0);
			if (loc != NULL) {
				char *nu = ows_join(url, loc);
				free(loc);
				free(url);
				free(head);
				oc_close(&c);
				url = nu;
				continue;
			}
		}
		break;
	}
	free(url);
	{
		char *te = onyx_ws_head_get(head, "Transfer-Encoding", 0);
		char *cl = onyx_ws_head_get(head, "Content-Length", 0);
		chunked = te != NULL && ows_has_token(te, "chunked");
		if (cl != NULL && !chunked)
			clen = strtoll(cl, NULL, 10);
		free(te);
		free(cl);
	}
	ows_emit(w, OWS_OPEN, status, 0, head, NULL, 0);
	if (status != 200) {			/* (the page fails the source: no reconnection) */
		oc_close(&c);
		free(r.b);
		return;
	}

	/* the body as it comes, given up to its last line end */
	for (;;) {
		bool active = false, ended = false;
		if (w->cancel)
			break;
		if (r.len > 0) {
			size_t m = chunked ? ows_dechunk(&ck, r.b, r.len) : r.len;
			if (!chunked && clen >= 0 && (long long) m > clen - got)
				m = (size_t) (clen - got);
			got += (long long) m;
			if (pend + m > bcap) {
				size_t cap = bcap ? bcap : 8192;
				uint8_t *p;
				while (cap < pend + m) cap *= 2;
				p = realloc(body, cap);
				if (p == NULL) break;
				body = p;
				bcap = cap;
			}
			memcpy(body + pend, r.b, m);
			pend += m;
			r.len = 0;
			if ((chunked && ck.st == 3) || (!chunked && clen >= 0 && got >= clen))
				ended = true;
			{
				size_t cut = pend;
				while (cut > 0 && body[cut - 1] != '\n' && body[cut - 1] != '\r')
					cut--;
				if (cut > 0) {
					ows_emit(w, OWS_DATA, 0, 0, NULL, body, cut);
					memmove(body, body + cut, pend - cut);
					pend -= cut;
				}
			}
			if (pend > 16 * 1024 * 1024)	/* (a line without end: too long) */
				ended = true;
		}
		if (ended || r.closed) {
			ows_emit(w, OWS_CLOSE, 0, 1, NULL, NULL, 0);
			break;
		}
		n = rd_try(&r, &c);
		if (n > 0)
			active = true;
		if (active) {
			nap = 1;
		} else if (!r.closed) {
			ows_nap(w, w->wake, nap);
			if (nap < 100) nap *= 2;
			if (nap > 100) nap = 100;
		}
	}
	free(body);
	free(r.b);
	oc_close(&c);
}

/* ---- the thread, and the UI thread's calls ------------------------------------------------------------ */

static int ows_thread(void *arg)
{
	struct onyx_ws *w = arg;
	int orphan;

	if (w->mode == ONYX_WS_SOCKET)
		ows_run_socket(w);
	else
		ows_run_events(w);
	kapi_lock(&w->lk);
	w->done = 1;
	orphan = w->orphan;
	kapi_unlock(&w->lk);
	if (orphan)
		ows_destroy(w);
	return 0;
}

onyx_ws *onyx_ws_open(int mode, const char *url, const char *hdrs,
		void (*notify)(void *pw), void *pw)
{
	struct onyx_ws *w;

	if (KT->version < 67 || KT->thread_create == 0)
		return NULL;
	w = calloc(1, sizeof *w);
	if (w == NULL)
		return NULL;
	w->id = ++ows_next_id;
	w->mode = mode;
	w->url = strdup(url);
	w->hdrs = strdup(hdrs != NULL ? hdrs : "");
	w->notify = notify;
	w->pw = pw;
	if (w->url == NULL || w->hdrs == NULL ||
	    kapi_thread_create(ows_thread, w, 0, mode == ONYX_WS_SOCKET ? "websocket" : "events") < 0) {
		ows_destroy(w);
		return NULL;
	}
	w->reg_next = ows_all;
	ows_all = w;
	return w;
}

static void ows_wake(struct onyx_ws *w)
{
	w->wake++;
	kapi_wake_word(&w->wake);
}

int onyx_ws_send(onyx_ws *w, int binary, const void *data, size_t len)
{
	struct ows_out *o = malloc(sizeof *o + (len ? len : 1));

	if (o == NULL)
		return -1;
	o->next = NULL;
	o->opcode = binary ? 2 : 1;
	o->len = len;
	if (len > 0)
		memcpy(o->data, data, len);
	kapi_lock(&w->lk);
	if (w->out_tail != NULL) w->out_tail->next = o; else w->out = o;
	w->out_tail = o;
	w->buffered += len;
	kapi_unlock(&w->lk);
	ows_wake(w);
	return 0;
}

void onyx_ws_close(onyx_ws *w, int code, const char *reason, size_t rlen)
{
	kapi_lock(&w->lk);
	if (!w->close_req) {
		w->close_req = 1;
		w->close_code = code;
		if (rlen > 123) rlen = 123;
		w->close_rlen = rlen;
		if (rlen > 0)
			memcpy(w->close_reason, reason, rlen);
	}
	kapi_unlock(&w->lk);
	ows_wake(w);
}

struct onyx_ws_event *onyx_ws_take(onyx_ws *w)
{
	struct onyx_ws_event *e;

	kapi_lock(&w->lk);
	e = w->ev;
	w->ev = w->ev_tail = NULL;
	w->posted = 0;
	kapi_unlock(&w->lk);
	return e;
}

size_t onyx_ws_buffered(onyx_ws *w)
{
	size_t n;

	kapi_lock(&w->lk);
	n = w->buffered;
	kapi_unlock(&w->lk);
	return n;
}

void onyx_ws_free(onyx_ws *w)
{
	struct onyx_ws **pp;
	int done;

	if (w == NULL)
		return;
	for (pp = &ows_all; *pp != NULL; pp = &(*pp)->reg_next)
		if (*pp == w) {
			*pp = w->reg_next;
			break;
		}
	kapi_lock(&w->lk);
	done = w->done;
	w->orphan = 1;
	w->cancel = 1;
	kapi_unlock(&w->lk);
	if (done)
		ows_destroy(w);
	else
		ows_wake(w);
}

/* ---- a blocking GET (importScripts) ------------------------------------------------------------------ */

char *onyx_http_get_sync(const char *url0, const char *hdrs, size_t *len, int *status)
{
	char *url = strdup(url0), *head = NULL, *req, *out = NULL;
	struct ows_url u;
	struct ows_conn c = { false, -1, NULL };
	struct ows_rd r = { NULL, 0, 0, false };
	int redirects, n;

	*len = 0;
	*status = 0;
	for (redirects = 0; url != NULL && redirects < 6; redirects++) {
		size_t reqcap;
		bool chunked;
		long long clen = -1;
		unsigned t0;

		if (!ows_parse_url(url, &u) || !oc_open(&c, &u))
			break;
		reqcap = u.pathlen + strlen(hdrs != NULL ? hdrs : "") + 300;
		req = malloc(reqcap);
		n = req == NULL ? -1 : snprintf(req, reqcap, "GET %.*s HTTP/1.1\r\nHost: %s\r\n"
			"Accept: */*\r\nConnection: close\r\n%s\r\n",
			(int) u.pathlen, u.path, u.hostport, hdrs != NULL ? hdrs : "");
		if (n <= 0 || (size_t) n >= reqcap || oc_send(&c, req, (size_t) n) < 0) {
			free(req);
			break;
		}
		free(req);
		r.len = 0;
		r.closed = false;
		head = rd_head(&r, &c, NULL, status);
		if (head == NULL)
			break;
		if (*status >= 300 && *status < 400) {
			char *loc = onyx_ws_head_get(head, "Location", 0);
			if (loc != NULL) {
				char *nu = ows_join(url, loc);
				free(loc);
				free(url);
				free(head);
				head = NULL;
				oc_close(&c);
				url = nu;
				continue;
			}
		}
		{
			char *te = onyx_ws_head_get(head, "Transfer-Encoding", 0);
			char *cl = onyx_ws_head_get(head, "Content-Length", 0);
			chunked = te != NULL && ows_has_token(te, "chunked");
			if (cl != NULL && !chunked)
				clen = strtoll(cl, NULL, 10);
			free(te);
			free(cl);
		}
		/* the body: to its length, its last chunk or the close (15 s without a byte: fail) */
		t0 = kapi_get_ticks();
		for (;;) {
			if (clen >= 0 && (long long) r.len >= clen)
				break;
			if (r.len > 16 * 1024 * 1024 || r.closed)
				break;
			if (chunked) {		/* (the last chunk "0\r\n\r\n" seen: done) */
				if (r.len >= 5 && memcmp(r.b + r.len - 5, "0\r\n\r\n", 5) == 0)
					break;
			}
			n = rd_try(&r, &c);
			if (n > 0) {
				t0 = kapi_get_ticks();
			} else if (n == 0) {
				if (kapi_get_ticks() - t0 > 1500) {
					r.closed = true;
					clen = -2;	/* (timeout) */
				}
				kapi_msleep(2);
			}
		}
		if (clen == -2)
			break;
		{
			size_t m = r.len;
			if (chunked) {
				struct ows_chunk ck;
				memset(&ck, 0, sizeof ck);
				m = ows_dechunk(&ck, r.b, r.len);
			} else if (clen >= 0 && (long long) m > clen) {
				m = (size_t) clen;
			}
			out = malloc(m + 1);
			if (out != NULL) {
				memcpy(out, r.b, m);
				out[m] = '\0';
				*len = m;
			}
		}
		break;
	}
	free(head);
	free(url);
	free(r.b);
	oc_close(&c);
	return out;
}
