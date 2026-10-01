/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 */

/**
 * \file
 * Onyx: the scripts' real-time and background APIs -- the natives under net.js.
 *
 *  - WebSocket and EventSource: a connection of user/netsurf/onyx_ws.c each (its own
 *    thread, outside the fetch queue and the cache). The UI thread builds the request's
 *    headers (the Origin, the cookie jar's cookies -- urldb is the UI thread's --, the
 *    User-Agent, the subprotocols, Last-Event-ID), and when the connection's thread posts,
 *    a scheduler callback (qnet_conn_drain) gives the events to net.js's callback: the
 *    handshake's Set-Cookie go to the jar there.
 *  - Workers: each in a context of its own in the window's runtime (qjs.c:
 *    qjs_worker_create), on the UI thread. A message between two contexts is written by
 *    QuickJS's object serializer (JS_WriteObject2: objects, arrays, typed arrays and their
 *    buffers, Map, Set, Date, RegExp, BigInt, a graph with cycles) in the sender's and read
 *    into the receiver's, from a scheduler callback (qnet_deliver): the structured clone
 *    between realms (net.js wraps what the serializer does not know: Blob, File, Error).
 *    BroadcastChannel reaches the page and all its workers the same way.
 *  - importScripts' sources a worker did not have fetched first: a blocking read
 *    (file:, http(s): onyx_http_get_sync).
 *
 * Everything a context owns goes when its scripts stop (qjs_net_stop, from js_closethread /
 * the context's free): its connections closed, its workers ended.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "quickjs.h"

#include "utils/errors.h"
#include "utils/log.h"
#include "utils/nsurl.h"
#include "utils/nsoption.h"
#include "utils/useragent.h"
#include "utils/file.h"
#include "netsurf/misc.h"
#include "content/urldb.h"
#include "desktop/gui_internal.h"

#include "onyx_ws.h"		/* user/netsurf: the connections' threads */
#include "javascript/quickjs/qjs_net.h"
#include "qjs_net_js.h"		/* net.js, as a C string (the build makes it) */

/* ---- the contexts' records ------------------------------------------------------------------- */

struct qnet_worker;

struct qnet_msg {
	struct qnet_msg *next;
	int kind;			/* 0 message, 1 error, 2 connect, 3 broadcast, 4 cannot read */
	int port;			/* a SharedWorker's port (0: the worker's own) */
	int wid;			/* from worker wid to its owner (its pcb); 0: to the hook */
	char *name;			/* a broadcast's channel */
	uint8_t *buf;			/* JS_WriteObject2's bytes (0: undefined) */
	size_t len;
};

struct qnet_ctx {
	struct qnet_ctx *next;
	JSContext *ctx;
	struct qnet_ctx *parent;	/* the context that made this worker (0: a document) */
	struct qnet_worker *self;	/* this worker's record (0: a document) */
	JSValue hook;			/* net.js: hook(kind, data, port, name) */
	struct qnet_msg *q, *qt;	/* to deliver */
	bool scheduled, stopped;
	int busy;
};

struct qnet_worker {
	struct qnet_worker *next;
	int id;
	struct qnet_ctx *owner;		/* the context that made it */
	JSContext *wctx;		/* its own context (0 once ended) */
	JSValue pcb;			/* the owner's: pcb(kind, data, port) */
	bool reaping;			/* its end scheduled */
	bool terminated;		/* ... by terminate(): its messages dropped (a worker that
					 * closed itself keeps its record: the messages it sent come) */
};

static struct qnet_ctx *qnet_ctxs;
static struct qnet_worker *qnet_workers;
static int qnet_next_wid;

/* the worker being made (qjs_worker_create runs qjs_net_setup for it; its leaving runs the
 * runtime's pending jobs, the page's among them: another worker may be made meanwhile) */
static struct qnet_making {
	struct qnet_worker *w;
	char *name, *type, *kind, *source;
	size_t srclen;
} qnet_making;

static struct qnet_ctx *qnet_ctx_of(JSContext *ctx)
{
	struct qnet_ctx *c;

	for (c = qnet_ctxs; c != NULL; c = c->next)
		if (c->ctx == ctx && !c->stopped)
			return c;
	return NULL;
}

static struct qnet_worker *qnet_worker_of(int id)
{
	struct qnet_worker *w;

	for (w = qnet_workers; w != NULL; w = w->next)
		if (w->id == id)
			return w;
	return NULL;
}

static void qnet_msg_free(struct qnet_msg *m)
{
	free(m->buf);			/* (qnet_serialize's copy: plain malloc) */
	free(m->name);
	free(m);
}

/* ---- messages between contexts ---------------------------------------------------------------- */

/* a value written by the sender's context (undefined: no bytes); false: it cannot be (an
 * exception is pending) */
static bool qnet_serialize(JSContext *ctx, JSValueConst v, uint8_t **buf, size_t *len)
{
	uint8_t *b;
	size_t n = 0;

	*buf = NULL;
	*len = 0;
	if (JS_IsUndefined(v))
		return true;
	b = JS_WriteObject2(ctx, &n, v, JS_WRITE_OBJ_REFERENCE, NULL);
	if (b == NULL)
		return false;
	/* a copy of our own (the runtime's allocator may be another one's) */
	*buf = malloc(n ? n : 1);
	if (*buf == NULL) {
		js_free(ctx, b);
		JS_ThrowOutOfMemory(ctx);
		return false;
	}
	memcpy(*buf, b, n);
	*len = n;
	js_free(ctx, b);
	return true;
}

static void qnet_deliver(void *p);

static void qnet_queue(struct qnet_ctx *to, struct qnet_msg *m)
{
	m->next = NULL;
	if (to->qt != NULL) to->qt->next = m; else to->q = m;
	to->qt = m;
	if (!to->scheduled) {
		to->scheduled = true;
		guit->misc->schedule(0, qnet_deliver, to);
	}
}

static struct qnet_msg *qnet_msg_new(int kind, int port, int wid, uint8_t *buf, size_t len,
		const char *name)
{
	struct qnet_msg *m = calloc(1, sizeof *m);

	if (m == NULL) {
		free(buf);
		return NULL;
	}
	m->kind = kind;
	m->port = port;
	m->wid = wid;
	m->buf = buf;
	m->len = len;
	if (name != NULL)
		m->name = strdup(name);
	return m;
}

static void qnet_ctx_free(struct qnet_ctx *c)
{
	while (c->q != NULL) {
		struct qnet_msg *m = c->q;
		c->q = m->next;
		qnet_msg_free(m);
	}
	free(c);
}

/* the messages to a context, each a task: to a Worker object (its pcb) or to net.js's hook */
static void qnet_deliver(void *p)
{
	struct qnet_ctx *c = p;

	c->scheduled = false;
	if (c->stopped || qjs_ctx_closed(c->ctx))
		return;
	c->busy++;
	while (c->q != NULL && !c->stopped) {
		struct qnet_msg *m = c->q;
		JSContext *ctx = c->ctx;
		JSRuntime *rt = JS_GetRuntime(ctx);
		JSValue fn = JS_UNDEFINED, args[4];
		int kind = m->kind;

		c->q = m->next;
		if (c->q == NULL)
			c->qt = NULL;
		if (m->wid != 0) {
			struct qnet_worker *w = qnet_worker_of(m->wid);
			if (w != NULL && w->owner == c && !w->terminated)
				fn = JS_DupValue(ctx, w->pcb);
		} else {
			fn = JS_DupValue(ctx, c->hook);
		}
		if (!JS_IsFunction(ctx, fn)) {
			JS_FreeValue(ctx, fn);
			qnet_msg_free(m);
			continue;
		}
		args[1] = JS_UNDEFINED;
		if (m->buf != NULL) {
			args[1] = JS_ReadObject(ctx, m->buf, m->len, JS_READ_OBJ_REFERENCE);
			if (JS_IsException(args[1])) {
				JS_FreeValue(ctx, JS_GetException(ctx));
				args[1] = JS_UNDEFINED;
				kind = 4;		/* (messageerror) */
			}
		}
		args[0] = JS_NewInt32(ctx, kind);
		args[2] = JS_NewInt32(ctx, m->port);
		args[3] = m->name != NULL ? JS_NewString(ctx, m->name) : JS_UNDEFINED;
		qnet_msg_free(m);
		qjs_invoke(ctx, fn, 4, args, "message");
		/* (the context may be gone now: the runtime's frees) */
		JS_FreeValueRT(rt, fn);
		JS_FreeValueRT(rt, args[1]);
		JS_FreeValueRT(rt, args[3]);
	}
	c->busy--;
	if (c->stopped && c->busy == 0)
		qnet_ctx_free(c);
}

/* ---- WebSocket and EventSource ------------------------------------------------------------------ */

struct qnet_conn {
	struct qnet_conn *next;
	struct qnet_ctx *c;
	int id;
	int kind;			/* ONYX_WS_SOCKET, ONYX_WS_EVENTS */
	onyx_ws *ws;
	JSValue cb;
	nsurl *url;			/* its http(s) URL (the cookies') */
	bool scheduled, dead;
	int busy;
};

static struct qnet_conn *qnet_conns;
static int qnet_next_conn;

static void qnet_conn_drain(void *p);

static void qnet_conn_free(struct qnet_conn *k)
{
	struct qnet_conn **pp;

	for (pp = &qnet_conns; *pp != NULL; pp = &(*pp)->next)
		if (*pp == k) {
			*pp = k->next;
			break;
		}
	if (k->url != NULL)
		nsurl_unref(k->url);
	free(k);
}

/* a connection ended (or dropped by its page): its thread stopped, its callback let go */
static void qnet_conn_end(struct qnet_conn *k)
{
	if (k->dead)
		return;
	k->dead = true;
	if (k->scheduled)
		guit->misc->schedule(-1, qnet_conn_drain, k);
	k->scheduled = false;
	onyx_ws_free(k->ws);
	k->ws = NULL;
	if (k->c != NULL && !k->c->stopped)
		JS_FreeValue(k->c->ctx, k->cb);
	k->cb = JS_UNDEFINED;
	if (k->busy == 0)
		qnet_conn_free(k);
}

/* on the UI thread, from the connection's post */
static void qnet_conn_notify(void *pw)
{
	struct qnet_conn *k = pw;

	if (!k->scheduled && !k->dead) {
		k->scheduled = true;
		guit->misc->schedule(0, qnet_conn_drain, k);
	}
}

static struct qnet_conn *qnet_conn_of(JSContext *ctx, JSValueConst v)
{
	int32_t id = 0;
	struct qnet_conn *k;

	if (JS_ToInt32(ctx, &id, v) < 0)
		return NULL;
	for (k = qnet_conns; k != NULL; k = k->next)
		if (k->id == id && !k->dead && k->c != NULL && k->c->ctx == ctx)
			return k;
	return NULL;
}

/* the handshake's Set-Cookie lines to the jar */
static void qnet_cookies(struct qnet_conn *k, const char *head)
{
	int i;

	if (head == NULL || k->url == NULL)
		return;
	for (i = 0; i < 50; i++) {
		char *v = onyx_ws_head_get(head, "Set-Cookie", i);
		if (v == NULL)
			break;
		urldb_set_cookie(v, k->url, NULL);
		free(v);
	}
}

static void qnet_conn_drain(void *p)
{
	struct qnet_conn *k = p;
	struct onyx_ws_event *e;

	k->scheduled = false;
	if (k->dead || k->ws == NULL || k->c == NULL || k->c->stopped)
		return;
	e = onyx_ws_take(k->ws);
	k->busy++;
	while (e != NULL) {
		struct onyx_ws_event *next = e->next;
		if (!k->dead && k->c != NULL && !k->c->stopped && !qjs_ctx_closed(k->c->ctx)) {
			JSContext *ctx = k->c->ctx;
			JSRuntime *rt = JS_GetRuntime(ctx);
			JSValue a[4], fn = JS_DupValue(ctx, k->cb);
			int n = 1, i;
			bool last = e->type == OWS_CLOSE;

			a[1] = a[2] = a[3] = JS_UNDEFINED;
			switch (e->type) {
			case OWS_OPEN:
				qnet_cookies(k, e->head);
				a[0] = JS_NewString(ctx, "open");
				if (k->kind == ONYX_WS_SOCKET) {
					char *pr = onyx_ws_head_get(e->head, "Sec-WebSocket-Protocol", 0);
					char *ex = onyx_ws_head_get(e->head, "Sec-WebSocket-Extensions", 0);
					a[1] = JS_NewString(ctx, pr != NULL ? pr : "");
					a[2] = JS_NewString(ctx, ex != NULL ? ex : "");
					free(pr);
					free(ex);
				} else {
					char *ct = onyx_ws_head_get(e->head, "Content-Type", 0);
					a[1] = JS_NewInt32(ctx, e->code);
					a[2] = JS_NewString(ctx, ct != NULL ? ct : "");
					free(ct);
				}
				n = 3;
				break;
			case OWS_TEXT:
			case OWS_DATA:
				a[0] = JS_NewString(ctx, e->type == OWS_TEXT ? "message" : "data");
				a[1] = JS_NewStringLen(ctx, (const char *) e->data, e->len);
				n = 2;
				break;
			case OWS_BINARY:
				a[0] = JS_NewString(ctx, "message");
				a[1] = JS_NewArrayBufferCopy(ctx, e->data, e->len);
				a[2] = JS_TRUE;
				n = 3;
				break;
			case OWS_ERROR:
				a[0] = JS_NewString(ctx, "error");
				a[1] = JS_NewStringLen(ctx, (const char *) e->data, e->len);
				a[2] = JS_NewInt32(ctx, e->code);
				n = 3;
				break;
			default:	/* OWS_CLOSE */
				a[0] = JS_NewString(ctx, "close");
				a[1] = JS_NewInt32(ctx, e->code);
				a[2] = JS_NewStringLen(ctx, (const char *) e->data, e->len);
				a[3] = JS_NewBool(ctx, e->clean);
				n = 4;
				break;
			}
			if (last)		/* (the end: no more events, the thread ended) */
				qnet_conn_end(k);
			qjs_invoke(ctx, fn, n, a, k->kind == ONYX_WS_SOCKET ? "websocket" : "eventsource");
			for (i = 0; i < 4; i++)
				JS_FreeValueRT(rt, a[i]);
			JS_FreeValueRT(rt, fn);
		}
		free(e->head);
		free(e);
		e = next;
	}
	k->busy--;
	if (k->dead && k->busy == 0)
		qnet_conn_free(k);
}

/* "scheme://host[:port]" of a URL (malloc'd), or NULL */
static char *qnet_origin(nsurl *u)
{
	char *s = NULL;
	size_t len = 0;

	if (u == NULL || nsurl_get(u, NSURL_SCHEME | NSURL_HOST | NSURL_PORT, &s, &len) != NSERROR_OK)
		return NULL;
	return s;
}

/* a header line added to a malloc'd block of them */
static void qnet_hdr(char **h, const char *name, const char *value)
{
	size_t old = *h != NULL ? strlen(*h) : 0;
	size_t n = old + strlen(name) + strlen(value) + 5;
	char *p;

	if (value == NULL || value[0] == '\0')
		return;
	p = realloc(*h, n);
	if (p == NULL)
		return;
	if (old == 0)
		p[0] = '\0';
	snprintf(p + old, n - old, "%s: %s\r\n", name, value);
	*h = p;
}

/* the headers every connection sends: Origin, User-Agent, Accept-Language, the cookies */
static char *qnet_headers(JSContext *ctx, nsurl *httpurl, bool origin, bool cookies)
{
	char *h = NULL, *o;
	const char *lang = nsoption_charp(accept_language);

	if (origin && (o = qnet_origin(qjs_ctx_url(ctx))) != NULL) {
		qnet_hdr(&h, "Origin", o);
		free(o);
	}
	qnet_hdr(&h, "User-Agent", user_agent_string());
	qnet_hdr(&h, "Accept-Language", lang != NULL ? lang : "fr-FR,fr;q=0.9,en-US;q=0.8,en;q=0.7");
	if (cookies && httpurl != NULL) {
		char *ck = urldb_get_cookie(httpurl, true);
		if (ck != NULL)
			qnet_hdr(&h, "Cookie", ck);
		free(ck);
	}
	return h;
}

static struct qnet_conn *qnet_conn_new(JSContext *ctx, int kind, const char *url,
		const char *httpurl, JSValueConst cb, char *hdrs)
{
	struct qnet_ctx *c = qnet_ctx_of(ctx);
	struct qnet_conn *k;

	if (c == NULL)
		return NULL;
	k = calloc(1, sizeof *k);
	if (k == NULL)
		return NULL;
	k->c = c;
	k->id = ++qnet_next_conn;
	k->kind = kind;
	k->cb = JS_DupValue(ctx, cb);
	if (httpurl != NULL)
		nsurl_create(httpurl, &k->url);
	k->next = qnet_conns;
	qnet_conns = k;
	k->ws = onyx_ws_open(kind, url, hdrs, qnet_conn_notify, k);
	if (k->ws == NULL) {		/* (no threads: the page is told at once) */
		qnet_conn_end(k);
		return NULL;
	}
	return k;
}

/** wsOpen(url, [protocols], cb(type, ...)) -> id, or 0 (it cannot be opened) */
static JSValue n_ws_open(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const char *url;
	char *http = NULL, *hdrs, *protos = NULL;
	nsurl *hu = NULL;
	struct qnet_conn *k;
	uint32_t i, n = 0;

	(void) this_val;
	if (argc < 3 || !JS_IsFunction(ctx, argv[2]) || qjs_ctx_closed(ctx))
		return JS_NewInt32(ctx, 0);
	url = JS_ToCString(ctx, argv[0]);
	if (url == NULL)
		return JS_EXCEPTION;
	/* the cookies are the http(s) URL's */
	http = malloc(strlen(url) + 4);
	if (http != NULL) {
		if (strncmp(url, "wss:", 4) == 0) sprintf(http, "https:%s", url + 4);
		else if (strncmp(url, "ws:", 3) == 0) sprintf(http, "http:%s", url + 3);
		else strcpy(http, url);
		nsurl_create(http, &hu);
	}
	hdrs = qnet_headers(ctx, hu, true, true);
	{
		JSValue l = JS_GetPropertyStr(ctx, argv[1], "length");
		JS_ToUint32(ctx, &n, l);
		JS_FreeValue(ctx, l);
	}
	for (i = 0; i < n && i < 64; i++) {
		JSValue v = JS_GetPropertyUint32(ctx, argv[1], i);
		const char *s = JS_ToCString(ctx, v);
		JS_FreeValue(ctx, v);
		if (s != NULL) {
			size_t old = protos != NULL ? strlen(protos) : 0;
			char *p = realloc(protos, old + strlen(s) + 3);
			if (p != NULL) {
				protos = p;
				sprintf(protos + old, "%s%s", old ? ", " : "", s);
			}
			JS_FreeCString(ctx, s);
		}
	}
	if (protos != NULL) {
		qnet_hdr(&hdrs, "Sec-WebSocket-Protocol", protos);
		free(protos);
	}
	k = qnet_conn_new(ctx, ONYX_WS_SOCKET, url, http, argv[2], hdrs);
	JS_FreeCString(ctx, url);
	free(http);
	free(hdrs);
	if (hu != NULL)
		nsurl_unref(hu);
	return JS_NewInt32(ctx, k != NULL ? k->id : 0);
}

/* bytes of a string (UTF-8), an ArrayBuffer or a view; *free_str: a JS C string to free */
static const uint8_t *qnet_bytes(JSContext *ctx, JSValueConst v, size_t *len, const char **str)
{
	size_t size = 0, off = 0, bl = 0, bpe = 0;
	uint8_t *p;

	*str = NULL;
	if (JS_IsString(v)) {
		*str = JS_ToCStringLen(ctx, len, v);
		return (const uint8_t *) *str;
	}
	p = JS_GetArrayBuffer(ctx, &size, v);
	if (p != NULL) {
		*len = size;
		return p;
	}
	JS_FreeValue(ctx, JS_GetException(ctx));
	{
		JSValue buf = JS_GetTypedArrayBuffer(ctx, v, &off, &bl, &bpe);
		if (JS_IsException(buf)) {
			JS_FreeValue(ctx, JS_GetException(ctx));
			return NULL;
		}
		p = JS_GetArrayBuffer(ctx, &size, buf);
		JS_FreeValue(ctx, buf);	/* (the view keeps it) */
		if (p == NULL || off + bl > size) {
			JS_FreeValue(ctx, JS_GetException(ctx));
			return NULL;
		}
		*len = bl;
		return p + off;
	}
}

/** wsSend(id, data, binary) -> true (queued) / false */
static JSValue n_ws_send(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qnet_conn *k = argc > 2 ? qnet_conn_of(ctx, argv[0]) : NULL;
	const uint8_t *b;
	const char *str;
	size_t len = 0;
	int r;

	(void) this_val;
	if (k == NULL || k->ws == NULL)
		return JS_FALSE;
	b = qnet_bytes(ctx, argv[1], &len, &str);
	if (b == NULL)
		return JS_FALSE;
	r = onyx_ws_send(k->ws, JS_ToBool(ctx, argv[2]) && str == NULL, b, len);
	if (str != NULL)
		JS_FreeCString(ctx, str);
	return JS_NewBool(ctx, r == 0);
}

/** wsBuffered(id) -> the bytes queued, not sent yet */
static JSValue n_ws_buffered(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qnet_conn *k = argc > 0 ? qnet_conn_of(ctx, argv[0]) : NULL;

	(void) this_val;
	return JS_NewFloat64(ctx, k != NULL && k->ws != NULL ? (double) onyx_ws_buffered(k->ws) : 0);
}

/** wsClose(id, code (0: none), reason): the closing handshake begins */
static JSValue n_ws_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qnet_conn *k = argc > 2 ? qnet_conn_of(ctx, argv[0]) : NULL;
	int32_t code = 0;
	size_t rl = 0;
	const char *reason;

	(void) this_val;
	if (k == NULL || k->ws == NULL)
		return JS_UNDEFINED;
	JS_ToInt32(ctx, &code, argv[1]);
	reason = JS_ToCStringLen(ctx, &rl, argv[2]);
	onyx_ws_close(k->ws, code, reason != NULL ? reason : "", reason != NULL ? rl : 0);
	if (reason != NULL)
		JS_FreeCString(ctx, reason);
	return JS_UNDEFINED;
}

/** netAbort(id): a WebSocket or an EventSource dropped at once (no more events) */
static JSValue n_net_abort(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qnet_conn *k = argc > 0 ? qnet_conn_of(ctx, argv[0]) : NULL;

	(void) this_val;
	if (k != NULL)
		qnet_conn_end(k);
	return JS_UNDEFINED;
}

/** sseOpen(url, lastEventId, withCredentials, cb(type, ...)) -> id, or 0 */
static JSValue n_sse_open(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const char *url, *last;
	nsurl *u = NULL;
	char *hdrs, *mine, *theirs;
	bool same;
	struct qnet_conn *k;

	(void) this_val;
	if (argc < 4 || !JS_IsFunction(ctx, argv[3]) || qjs_ctx_closed(ctx))
		return JS_NewInt32(ctx, 0);
	url = JS_ToCString(ctx, argv[0]);
	if (url == NULL)
		return JS_EXCEPTION;
	if (nsurl_create(url, &u) != NSERROR_OK) {
		JS_FreeCString(ctx, url);
		return JS_NewInt32(ctx, 0);
	}
	mine = qnet_origin(qjs_ctx_url(ctx));
	theirs = qnet_origin(u);
	same = mine != NULL && theirs != NULL && strcmp(mine, theirs) == 0;
	free(mine);
	free(theirs);
	/* (cross-origin: its Origin, and the cookies only withCredentials) */
	hdrs = qnet_headers(ctx, u, !same, same || JS_ToBool(ctx, argv[2]));
	last = JS_ToCString(ctx, argv[1]);
	if (last != NULL && last[0] != '\0')
		qnet_hdr(&hdrs, "Last-Event-ID", last);
	if (last != NULL)
		JS_FreeCString(ctx, last);
	k = qnet_conn_new(ctx, ONYX_WS_EVENTS, url, url, argv[3], hdrs);
	free(hdrs);
	nsurl_unref(u);
	JS_FreeCString(ctx, url);
	return JS_NewInt32(ctx, k != NULL ? k->id : 0);
}

/* ---- workers -------------------------------------------------------------------------------------- */

static void qnet_worker_free(struct qnet_worker *w)
{
	struct qnet_worker **pp;
	JSContext *wctx = w->wctx;

	for (pp = &qnet_workers; *pp != NULL; pp = &(*pp)->next)
		if (*pp == w) {
			*pp = w->next;
			break;
		}
	w->wctx = NULL;
	if (wctx != NULL) {		/* (its context's record no longer points here) */
		struct qnet_ctx *wc = qnet_ctx_of(wctx);
		if (wc != NULL)
			wc->self = NULL;
	}
	if (w->owner != NULL && !w->owner->stopped)
		JS_FreeValue(w->owner->ctx, w->pcb);
	w->pcb = JS_UNDEFINED;
	free(w);
	if (wctx != NULL)
		qjs_worker_destroy(wctx);	/* (its own workers with it: qjs_net_stop) */
}

/* a worker ended outside its own code: terminated (its record freed), or closed by itself
 * (its context only: the messages it sent before still reach its owner) */
static void qnet_worker_reap(void *p)
{
	struct qnet_worker *w;

	for (w = qnet_workers; w != NULL; w = w->next)
		if (w == p) {
			if (w->terminated) {
				qnet_worker_free(w);
			} else if (w->wctx != NULL) {
				JSContext *wctx = w->wctx;
				struct qnet_ctx *wc = qnet_ctx_of(wctx);
				if (wc != NULL)
					wc->self = NULL;
				w->wctx = NULL;
				qjs_worker_destroy(wctx);
			}
			return;
		}
}

/** workerNew(url, name, type, kind, source, pcb(kind, data, port)) -> its id, or 0: a context
 *  for the worker, its script (fetched by net.js) run a task later */
static JSValue n_worker_new(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qnet_ctx *c = qnet_ctx_of(ctx);
	struct qnet_worker *w;
	const char *url;
	JSContext *wctx;
	struct qnet_making saved = qnet_making;

	(void) this_val;
	if (c == NULL || argc < 6 || !JS_IsFunction(ctx, argv[5]))
		return JS_NewInt32(ctx, 0);
	url = JS_ToCString(ctx, argv[0]);
	if (url == NULL)
		return JS_EXCEPTION;
	w = calloc(1, sizeof *w);
	if (w == NULL) {
		JS_FreeCString(ctx, url);
		return JS_NewInt32(ctx, 0);
	}
	w->id = ++qnet_next_wid;
	w->owner = c;
	w->pcb = JS_DupValue(ctx, argv[5]);
	w->next = qnet_workers;
	qnet_workers = w;
	{
		const char *s;
		qnet_making.w = w;
		s = JS_ToCString(ctx, argv[1]); qnet_making.name = strdup(s ? s : ""); JS_FreeCString(ctx, s);
		s = JS_ToCString(ctx, argv[2]); qnet_making.type = strdup(s ? s : ""); JS_FreeCString(ctx, s);
		s = JS_ToCString(ctx, argv[3]); qnet_making.kind = strdup(s ? s : ""); JS_FreeCString(ctx, s);
		s = JS_ToCStringLen(ctx, &qnet_making.srclen, argv[4]);
		qnet_making.source = malloc(qnet_making.srclen + 1);
		if (qnet_making.source != NULL && s != NULL) {
			memcpy(qnet_making.source, s, qnet_making.srclen);
			qnet_making.source[qnet_making.srclen] = '\0';
		}
		JS_FreeCString(ctx, s);
	}
	wctx = qjs_worker_create(ctx, url);
	free(qnet_making.name);
	free(qnet_making.type);
	free(qnet_making.kind);
	free(qnet_making.source);
	qnet_making = saved;
	JS_FreeCString(ctx, url);
	if (wctx == NULL) {
		qnet_worker_free(w);
		return JS_NewInt32(ctx, 0);
	}
	w->wctx = wctx;
	return JS_NewInt32(ctx, w->id);
}

/** workerPost(id, value, kind, port): a message to a worker (the value written here) */
static JSValue n_worker_post(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int32_t id = 0, kind = 0, port = 0;
	struct qnet_worker *w;
	struct qnet_ctx *to;
	struct qnet_msg *m;
	uint8_t *buf;
	size_t len;

	(void) this_val;
	if (argc < 4 || JS_ToInt32(ctx, &id, argv[0]) < 0)
		return JS_UNDEFINED;
	JS_ToInt32(ctx, &kind, argv[2]);
	JS_ToInt32(ctx, &port, argv[3]);
	w = qnet_worker_of(id);
	if (w == NULL || w->owner == NULL || w->owner->ctx != ctx || w->wctx == NULL || w->reaping)
		return JS_UNDEFINED;
	to = qnet_ctx_of(w->wctx);
	if (to == NULL)
		return JS_UNDEFINED;
	if (!qnet_serialize(ctx, argv[1], &buf, &len))
		return JS_EXCEPTION;
	m = qnet_msg_new(kind, port, 0, buf, len, NULL);
	if (m != NULL)
		qnet_queue(to, m);
	return JS_UNDEFINED;
}

/** workerTerminate(id): the worker ended at once */
static JSValue n_worker_terminate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int32_t id = 0;
	struct qnet_worker *w;

	(void) this_val;
	if (argc < 1 || JS_ToInt32(ctx, &id, argv[0]) < 0)
		return JS_UNDEFINED;
	w = qnet_worker_of(id);
	if (w != NULL && w->owner != NULL && w->owner->ctx == ctx && !w->terminated) {
		w->terminated = true;
		w->reaping = true;		/* (ended from the scheduler: its code may be on the stack) */
		guit->misc->schedule(0, qnet_worker_reap, w);
	}
	return JS_UNDEFINED;
}

/** hook(fn): (a context's) fn(kind, data, port, name) receives the messages to this context */
static JSValue n_net_hook(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qnet_ctx *c = qnet_ctx_of(ctx);

	(void) this_val;
	if (c != NULL && argc > 0) {
		JS_FreeValue(ctx, c->hook);
		c->hook = JS_DupValue(ctx, argv[0]);
	}
	return JS_UNDEFINED;
}

/** workerPostParent(value, kind, port): (a worker's) a message to the Worker object */
static JSValue n_worker_post_parent(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	struct qnet_ctx *c = qnet_ctx_of(ctx);
	int32_t kind = 0, port = 0;
	struct qnet_msg *m;
	uint8_t *buf;
	size_t len;

	(void) this_val;
	if (c == NULL || c->self == NULL || c->parent == NULL || c->parent->stopped || argc < 3 ||
	    c->self->reaping)
		return JS_UNDEFINED;
	JS_ToInt32(ctx, &kind, argv[1]);
	JS_ToInt32(ctx, &port, argv[2]);
	if (!qnet_serialize(ctx, argv[0], &buf, &len))
		return JS_EXCEPTION;
	m = qnet_msg_new(kind, port, c->self->id, buf, len, NULL);
	if (m != NULL)
		qnet_queue(c->parent, m);
	return JS_UNDEFINED;
}

/** workerClose(): (a worker's) self.close() -- it ends once this task is done */
static JSValue n_worker_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qnet_ctx *c = qnet_ctx_of(ctx);

	(void) this_val; (void) argc; (void) argv;
	if (c != NULL && c->self != NULL && !c->self->reaping) {
		c->self->reaping = true;
		guit->misc->schedule(0, qnet_worker_reap, c->self);
	}
	return JS_UNDEFINED;
}

/** Onyx: whether two contexts' scripts are of the same origin (http(s): scheme, host,
 *  port; file: URLs one origin) -- a worker's by its script's URL */
static bool qnet_same_origin(JSContext *a, JSContext *b)
{
	nsurl *ua = qjs_ctx_url(a), *ub = qjs_ctx_url(b);
	lwc_string *sa;
	bool same = false;

	if (ua == NULL || ub == NULL)
		return false;
	sa = nsurl_get_component(ua, NSURL_SCHEME);
	if (sa != NULL && (strcmp(lwc_string_data(sa), "http") == 0 ||
			strcmp(lwc_string_data(sa), "https") == 0 ||
			strcmp(lwc_string_data(sa), "file") == 0))
		same = nsurl_compare(ua, ub, NSURL_SCHEME | NSURL_HOST | NSURL_PORT);
	if (sa != NULL)
		lwc_string_unref(sa);
	return same;
}

/** broadcast(name, value): a BroadcastChannel's message to the page's other contexts */
static JSValue n_broadcast(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qnet_ctx *c = qnet_ctx_of(ctx), *root, *x;
	const char *name;
	uint8_t *buf;
	size_t len;

	(void) this_val;
	if (c == NULL || argc < 2)
		return JS_UNDEFINED;
	for (root = c; root->parent != NULL; root = root->parent)
		;
	name = JS_ToCString(ctx, argv[0]);
	if (name == NULL)
		return JS_EXCEPTION;
	if (!qnet_serialize(ctx, argv[1], &buf, &len)) {
		JS_FreeCString(ctx, name);
		return JS_EXCEPTION;
	}
	for (x = qnet_ctxs; x != NULL; x = x->next) {
		struct qnet_ctx *a;
		struct qnet_msg *m;
		uint8_t *copy = NULL;
		if (x == c || x->stopped)
			continue;
		for (a = x; a != NULL && a != root; a = a->parent)
			;
		/* Onyx: and the other same-origin documents of the app (a tab's frames, other
		 * tabs) with their workers */
		if (a != root && !qnet_same_origin(root->ctx, x->ctx))
			continue;
		if (len > 0 && (copy = malloc(len)) == NULL)
			continue;
		if (len > 0)
			memcpy(copy, buf, len);
		m = qnet_msg_new(3, 0, 0, copy, len, name);
		if (m != NULL)
			qnet_queue(x, m);
	}
	free(buf);
	JS_FreeCString(ctx, name);
	return JS_UNDEFINED;
}

/** importSync(url) -> the script's text (a worker's importScripts): file:, http(s): read
 *  now (blocking) -- the scripts fetched beforehand never come here */
static JSValue n_import_sync(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const char *url;
	nsurl *u = NULL;
	JSValue r = JS_NULL;

	(void) this_val;
	if (argc < 1 || (url = JS_ToCString(ctx, argv[0])) == NULL)
		return JS_NULL;
	if (nsurl_create(url, &u) == NSERROR_OK) {
		if (strncmp(url, "file:", 5) == 0) {
			char *path = NULL;
			if (guit->file->nsurl_to_path(u, &path) == NSERROR_OK && path != NULL) {
				FILE *f = fopen(path, "rb");
				if (f != NULL) {
					long n;
					char *b;
					fseek(f, 0, SEEK_END);
					n = ftell(f);
					fseek(f, 0, SEEK_SET);
					if (n >= 0 && n < 32 * 1024 * 1024 && (b = malloc((size_t) n + 1)) != NULL) {
						if (fread(b, 1, (size_t) n, f) == (size_t) n)
							r = JS_NewStringLen(ctx, b, (size_t) n);
						free(b);
					}
					fclose(f);
				}
				free(path);
			}
		} else if (strncmp(url, "http:", 5) == 0 || strncmp(url, "https:", 6) == 0) {
			char *hdrs = qnet_headers(ctx, u, false, true), *body;
			size_t len = 0;
			int status = 0;
			body = onyx_http_get_sync(url, hdrs, &len, &status);
			if (body != NULL && status >= 200 && status < 300)
				r = JS_NewStringLen(ctx, body, len);
			free(body);
			free(hdrs);
		}
		nsurl_unref(u);
	}
	JS_FreeCString(ctx, url);
	return r;
}

/** evalScript(text, name) -> its value (a classic script run in the global scope: a
 *  worker's, importScripts'); its exception thrown */
static JSValue n_eval_script(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const char *src, *name;
	size_t len;
	JSValue r;

	(void) this_val;
	if (argc < 2)
		return JS_UNDEFINED;
	src = JS_ToCStringLen(ctx, &len, argv[0]);
	if (src == NULL)
		return JS_EXCEPTION;
	name = JS_ToCString(ctx, argv[1]);
	r = JS_Eval(ctx, src, len, name != NULL ? name : "worker", JS_EVAL_TYPE_GLOBAL);
	JS_FreeCString(ctx, src);
	if (name != NULL)
		JS_FreeCString(ctx, name);
	return r;
}

static const JSCFunctionListEntry qnet_natives[] = {
	JS_CFUNC_DEF("wsOpen", 3, n_ws_open),
	JS_CFUNC_DEF("wsSend", 3, n_ws_send),
	JS_CFUNC_DEF("wsBuffered", 1, n_ws_buffered),
	JS_CFUNC_DEF("wsClose", 3, n_ws_close),
	JS_CFUNC_DEF("netAbort", 1, n_net_abort),
	JS_CFUNC_DEF("sseOpen", 4, n_sse_open),
	JS_CFUNC_DEF("workerNew", 6, n_worker_new),
	JS_CFUNC_DEF("workerPost", 4, n_worker_post),
	JS_CFUNC_DEF("workerTerminate", 1, n_worker_terminate),
	JS_CFUNC_DEF("netHook", 1, n_net_hook),
	JS_CFUNC_DEF("workerPostParent", 3, n_worker_post_parent),
	JS_CFUNC_DEF("workerClose", 0, n_worker_close),
	JS_CFUNC_DEF("broadcast", 2, n_broadcast),
	JS_CFUNC_DEF("importSync", 1, n_import_sync),
	JS_CFUNC_DEF("evalScript", 2, n_eval_script),
};

/* ---- setup and stop --------------------------------------------------------------------------------- */

static void qnet_report(JSContext *ctx, const char *where)
{
	JSValue e = JS_GetException(ctx);
	const char *msg = JS_ToCString(ctx, e);

	NSLOG(netsurf, INFO, "%s: %s", where, msg ? msg : "?");
	if (getenv("NS_JSDEBUG"))
		fprintf(stderr, "JS %s: %s\n", where, msg ? msg : "?");
	if (msg)
		JS_FreeCString(ctx, msg);
	JS_FreeValue(ctx, e);
}

static uint8_t *qnet_bc;		/* net.js' bytecode (compiled once: qjs.c) */
static size_t qnet_bc_len;

/* exported interface documented in qjs_net.h */
void qjs_net_setup(JSContext *ctx, JSValueConst natives, JSContext *parent)
{
	struct qnet_ctx *c = calloc(1, sizeof *c);
	JSValue fn, r, info = JS_NULL;

	if (c == NULL)
		return;
	c->ctx = ctx;
	c->hook = JS_UNDEFINED;
	if (parent != NULL && qnet_making.w != NULL) {
		c->parent = qnet_ctx_of(parent);
		c->self = qnet_making.w;
		info = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, info, "url", JS_NewString(ctx, nsurl_access(qjs_ctx_url(ctx))));
		JS_SetPropertyStr(ctx, info, "name", JS_NewString(ctx, qnet_making.name ? qnet_making.name : ""));
		JS_SetPropertyStr(ctx, info, "type", JS_NewString(ctx, qnet_making.type ? qnet_making.type : "classic"));
		JS_SetPropertyStr(ctx, info, "kind", JS_NewString(ctx, qnet_making.kind ? qnet_making.kind : "dedicated"));
		JS_SetPropertyStr(ctx, info, "source", qnet_making.source != NULL ?
			JS_NewStringLen(ctx, qnet_making.source, qnet_making.srclen) : JS_NewString(ctx, ""));
	}
	c->next = qnet_ctxs;
	qnet_ctxs = c;

	JS_SetPropertyFunctionList(ctx, natives, qnet_natives,
			sizeof(qnet_natives) / sizeof(qnet_natives[0]));
	fn = qjs_eval_cached(ctx, qjs_net_js, sizeof(qjs_net_js) - 1, "net.js", &qnet_bc,
			&qnet_bc_len);
	if (JS_IsException(fn)) {
		qnet_report(ctx, "net.js");
		JS_FreeValue(ctx, info);
		return;
	}
	{
		JSValue args[2] = { natives, info };
		r = JS_Call(ctx, fn, JS_UNDEFINED, 2, (JSValueConst *) args);
	}
	if (JS_IsException(r))
		qnet_report(ctx, "net.js setup");
	JS_FreeValue(ctx, r);
	JS_FreeValue(ctx, fn);
	JS_FreeValue(ctx, info);
}

/* exported interface documented in qjs_net.h */
void qjs_net_stop(JSContext *ctx)
{
	struct qnet_ctx *c = qnet_ctx_of(ctx), **pp;
	struct qnet_conn *k, *kn;
	struct qnet_worker *w;

	if (c == NULL)
		return;
	c->stopped = true;
	/* its sockets and streams */
	for (k = qnet_conns; k != NULL; k = kn) {
		kn = k->next;
		if (k->c != c)
			continue;
		if (!k->dead) {
			JS_FreeValue(ctx, k->cb);	/* (here: qnet_conn_end has no context) */
			k->cb = JS_UNDEFINED;
			k->c = NULL;
			qnet_conn_end(k);
		} else {
			k->c = NULL;
		}
	}
	/* its workers (and theirs) */
	for (;;) {
		for (w = qnet_workers; w != NULL; w = w->next)
			if (w->owner == c)
				break;
		if (w == NULL)
			break;
		if (w->reaping)
			guit->misc->schedule(-1, qnet_worker_reap, w);
		JS_FreeValue(ctx, w->pcb);
		w->pcb = JS_UNDEFINED;
		w->owner = NULL;
		qnet_worker_free(w);
	}
	/* a worker's record: its owner keeps it, its context is gone */
	if (c->self != NULL) {
		c->self->wctx = NULL;
		c->self = NULL;
	}
	/* the workers made by this one pointed at it */
	{
		struct qnet_ctx *x;
		for (x = qnet_ctxs; x != NULL; x = x->next)
			if (x->parent == c)
				x->parent = NULL;
	}
	JS_FreeValue(ctx, c->hook);
	c->hook = JS_UNDEFINED;
	if (c->scheduled)
		guit->misc->schedule(-1, qnet_deliver, c);
	c->scheduled = false;
	for (pp = &qnet_ctxs; *pp != NULL; pp = &(*pp)->next)
		if (*pp == c) {
			*pp = c->next;
			break;
		}
	if (c->busy == 0)
		qnet_ctx_free(c);
}
