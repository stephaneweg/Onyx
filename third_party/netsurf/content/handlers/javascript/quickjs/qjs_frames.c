/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 */

/**
 * \file
 * Onyx: a tab's frames to its scripts -- the natives under net.js' frames section.
 *
 * Each window (the tab's, each iframe's: desktop/frames.c) has a frame id; its document's
 * scripts run in a realm (a context) of the tab's runtime -- the iframes share it
 * (js_heap_share). The scripts see another window through a WindowProxy (net.js) that asks
 * here, by frame id, at each use:
 *
 *  - frameAccess: the window's global object when its document is same-origin with the
 *    caller's (contentWindow.document, a same-origin parent's functions: the objects
 *    themselves), false when it is cross-origin (the proxy then gives what HTML allows:
 *    postMessage, location's setter, closed, frames, parent, top...), null when it has no
 *    document running scripts;
 *  - the tree: frameSelf, frameRel (parent, top), frameCount / frameChild / frameNamed
 *    (window.length, window[i], frames[name]), frameOf (an <iframe>'s frame: its
 *    contentWindow, made at once for an element just inserted), frameElement;
 *  - framePost: window.postMessage to another window -- the value written by QuickJS'
 *    serializer in the sender's realm, read in the receiver's (objects of its own realm:
 *    nothing of the sender's leaks through), a task later; the targetOrigin checked
 *    against the receiver's document then, the sender's origin and window given
 *    (event.origin, event.source);
 *  - MessagePort across realms: a port transferred to another realm becomes a pair of
 *    ids here (portPair, portOwn, portPost, portClose); a channel within one realm stays
 *    html5.js' (no copy through C: React's scheduler posts on one at each task).
 *
 * The origins: scheme://host:port for http(s); file: URLs are one origin among themselves
 * (shown as "null", as Chrome); about:blank, a srcdoc and a data: frame made by srcdoc
 * take their parent's; a sandboxed frame without allow-same-origin has an opaque one.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>

#include <dom/dom.h>
#include "quickjs.h"

#include "utils/errors.h"
#include "utils/log.h"
#include "utils/nsurl.h"
#include "utils/corestrings.h"
#include "netsurf/misc.h"
#include "netsurf/content.h"
#include "netsurf/browser_window.h"
#include "content/content.h"
#include "content/hlcache.h"
#include "desktop/gui_internal.h"
#include "desktop/browser_private.h"
#include "desktop/frames.h"
#include "html/private.h"
#include "javascript/js.h"

#include "javascript/quickjs/qjs_canvas.h"	/* qjs_html_of, qjs_invoke, qjs_node_of */
#include "javascript/quickjs/qjs_frames.h"

/* ---- origins ------------------------------------------------------------------------------ */

static html_content *qf_html(struct hlcache_handle *h)
{
	if (h == NULL || content_get_type(h) != CONTENT_HTML)
		return NULL;
	return (html_content *) hlcache_handle_get_content(h);
}

/** the document a window's scripts run in now: the one loading once its scripts run (its
 *  parser's), else the one shown */
static html_content *qf_doc(struct browser_window *bw)
{
	html_content *l, *c;

	if (bw == NULL)
		return NULL;
	l = qf_html(bw->loading_content);
	c = qf_html(bw->current_content);
	if (l != NULL && l->jsthread != NULL && qjs_thread_context(l->jsthread) != NULL)
		return l;
	return c != NULL ? c : l;
}

/** the document whose <iframe>s the window's frames are: the one loading (its frames are
 *  made as its scripts ask; the shown one's went when the window was navigated), else the
 *  one shown */
static html_content *qf_frames_doc(struct browser_window *bw)
{
	html_content *l = qf_html(bw->loading_content);

	return l != NULL ? l : qf_html(bw->current_content);
}

/** that document's realm, or NULL */
static JSContext *qf_realm(struct browser_window *bw)
{
	html_content *h = qf_doc(bw);

	return h != NULL && h->jsthread != NULL ? qjs_thread_context(h->jsthread) : NULL;
}

/** a document's origin: key (compared: "null" is opaque, never the same) and shown (the
 *  serialization: event.origin) */
static void qf_origin_url(struct browser_window *bw, nsurl *url, char *key, char *shown,
		size_t n, int depth);

static void qf_origin(struct browser_window *bw, html_content *doc, char *key, char *shown,
		size_t n, int depth)
{
	snprintf(key, n, "null");
	snprintf(shown, n, "null");
	if (doc == NULL || depth > 12)
		return;
	qf_origin_url(bw, content_get_url(&doc->base), key, shown, n, depth);
}

/** ... the origin of a window's resource at that URL (Onyx: also a frame showing an image
 *  or a text, no document of its own) */
static void qf_origin_url(struct browser_window *bw, nsurl *url, char *key, char *shown,
		size_t n, int depth)
{
	lwc_string *scheme;
	const char *sc;

	snprintf(key, n, "null");
	snprintf(shown, n, "null");
	if (bw != NULL && (bw->onyx_sandbox & (ONYX_SANDBOX | ONYX_SANDBOX_ORIGIN)) == ONYX_SANDBOX)
		return;
	if (url == NULL || depth > 12)
		return;
	scheme = nsurl_get_component(url, NSURL_SCHEME);
	sc = scheme != NULL ? lwc_string_data(scheme) : "";
	if (strcmp(sc, "about") == 0 ||
	    (bw != NULL && bw->onyx_srcdoc_url != NULL &&
	     nsurl_compare(bw->onyx_srcdoc_url, url, NSURL_COMPLETE))) {
		/* about:blank, a srcdoc: its parent's (the document that made the frame) */
		if (bw != NULL && bw->parent != NULL)
			qf_origin(bw->parent, bw->onyx_owner != NULL ? bw->onyx_owner :
					qf_doc(bw->parent), key, shown, n, depth + 1);
	} else if (strcmp(sc, "http") == 0 || strcmp(sc, "https") == 0) {
		lwc_string *host = nsurl_get_component(url, NSURL_HOST);
		lwc_string *port = nsurl_get_component(url, NSURL_PORT);
		snprintf(key, n, "%s://%s%s%s", sc, host != NULL ? lwc_string_data(host) : "",
				port != NULL ? ":" : "", port != NULL ? lwc_string_data(port) : "");
		snprintf(shown, n, "%s", key);
		if (host != NULL)
			lwc_string_unref(host);
		if (port != NULL)
			lwc_string_unref(port);
	} else if (strcmp(sc, "file") == 0) {
		snprintf(key, n, "file://");	/* (the files: one origin among themselves) */
	}
	if (scheme != NULL)
		lwc_string_unref(scheme);
}

#define QF_ORIGIN 256

/** the caller's window, document and origin */
static void qf_caller(JSContext *ctx, struct browser_window **bw, char *key, char *shown)
{
	*bw = qjs_ctx_window(ctx);
	qf_origin(*bw, qjs_html_of(ctx), key, shown, QF_ORIGIN, 0);
}

/** whether the caller may reach the window's document (same origin) */
static bool qf_same_origin(JSContext *ctx, struct browser_window *target)
{
	struct browser_window *self;
	char k1[QF_ORIGIN], s1[QF_ORIGIN], k2[QF_ORIGIN], s2[QF_ORIGIN];

	qf_caller(ctx, &self, k1, s1);
	qf_origin(target, qf_doc(target), k2, s2, QF_ORIGIN, 0);
	return strcmp(k1, "null") != 0 && strcmp(k1, k2) == 0;
}

static int qf_int(JSContext *ctx, int argc, JSValueConst *argv, int i)
{
	int32_t v = 0;

	if (i < argc)
		JS_ToInt32(ctx, &v, argv[i]);
	return v;
}

/* ---- the realms' hooks, the messages -------------------------------------------------------- */

struct qf_ctx {
	struct qf_ctx *next;
	JSContext *ctx;
	JSValue hook;			/* net.js: hook(kind, data, a, origin, ports) */
};

struct qf_msg {
	struct qf_msg *next;
	int kind;			/* 5: a window's message, 6: a port's */
	int target;			/* the frame id / the port id */
	int source;			/* the sender's frame id */
	uint8_t *buf;			/* JS_WriteObject2's bytes (NULL: undefined) */
	size_t len;
	char *origin;			/* the sender's (shown) */
	char *key;			/* ... (compared: targetOrigin "/") */
	char *to;			/* the targetOrigin: "*", "/" or an origin */
	int *ports;			/* the ports transferred (ids) */
	int nports;
};

struct qf_port {
	struct qf_port *next;
	int id, other;			/* this end, the entangled one (0: none) */
	JSContext *ctx;			/* the realm holding it (NULL: in flight) */
	bool closed;
	struct qf_msg *q, *qt;		/* its messages while in flight */
};

static struct qf_ctx *qf_ctxs;
static struct qf_msg *qf_q, *qf_qt;
static struct qf_port *qf_ports;
static int qf_next_port;
static bool qf_scheduled;

static struct qf_ctx *qf_ctx_of(JSContext *ctx, bool make)
{
	struct qf_ctx *c;

	for (c = qf_ctxs; c != NULL; c = c->next)
		if (c->ctx == ctx)
			return c;
	if (!make || (c = calloc(1, sizeof(*c))) == NULL)
		return NULL;
	c->ctx = ctx;
	c->hook = JS_UNDEFINED;
	c->next = qf_ctxs;
	qf_ctxs = c;
	return c;
}

static struct qf_port *qf_port(int id)
{
	struct qf_port *p;

	for (p = qf_ports; p != NULL; p = p->next)
		if (p->id == id)
			return p;
	return NULL;
}

static void qf_port_close(struct qf_port *p);

static void qf_msg_free(struct qf_msg *m, bool close_ports)
{
	int i;

	if (close_ports)	/* (the ports of a message dropped: no one holds them) */
		for (i = 0; i < m->nports; i++) {
			struct qf_port *p = qf_port(m->ports[i]);
			if (p != NULL && p->ctx == NULL)
				qf_port_close(p);
		}
	free(m->buf);
	free(m->origin);
	free(m->key);
	free(m->to);
	free(m->ports);
	free(m);
}

static void qf_port_close(struct qf_port *p)
{
	struct qf_port **pp, *o;

	while (p->q != NULL) {
		struct qf_msg *m = p->q;
		p->q = m->next;
		qf_msg_free(m, true);
	}
	p->qt = NULL;
	if ((o = qf_port(p->other)) != NULL)
		o->other = 0;
	for (pp = &qf_ports; *pp != NULL; pp = &(*pp)->next)
		if (*pp == p) {
			*pp = p->next;
			break;
		}
	free(p);
}

static void qf_deliver(void *pw);

static void qf_schedule(void)
{
	if (!qf_scheduled) {
		qf_scheduled = true;
		guit->misc->schedule(0, qf_deliver, NULL);
	}
}

static void qf_enqueue(struct qf_msg *m)
{
	m->next = NULL;
	if (qf_qt != NULL) qf_qt->next = m; else qf_q = m;
	qf_qt = m;
	qf_schedule();
}

/** the value written in the sender's realm; false: it cannot be (an exception pending) */
static bool qf_write(JSContext *ctx, JSValueConst v, struct qf_msg *m)
{
	uint8_t *b;
	size_t n = 0;

	if (JS_IsUndefined(v))
		return true;
	b = JS_WriteObject2(ctx, &n, v, JS_WRITE_OBJ_REFERENCE, NULL);
	if (b == NULL)
		return false;
	m->buf = malloc(n ? n : 1);
	if (m->buf == NULL) {
		js_free(ctx, b);
		JS_ThrowOutOfMemory(ctx);
		return false;
	}
	memcpy(m->buf, b, n);
	m->len = n;
	js_free(ctx, b);
	return true;
}

/** the ports a message takes (an array of ids) */
static void qf_take_ports(JSContext *ctx, JSValueConst a, struct qf_msg *m)
{
	int64_t n = 0, i;

	if (!JS_IsArray(a) || JS_GetLength(ctx, a, &n) < 0 || n <= 0 || n > 1024)
		return;
	m->ports = calloc((size_t) n, sizeof(int));
	if (m->ports == NULL)
		return;
	for (i = 0; i < n; i++) {
		JSValue v = JS_GetPropertyUint32(ctx, a, (uint32_t) i);
		int32_t id = 0;
		struct qf_port *p;
		JS_ToInt32(ctx, &id, v);
		JS_FreeValue(ctx, v);
		if ((p = qf_port(id)) != NULL) {
			p->ctx = NULL;		/* (in flight: the receiver's realm claims it) */
			m->ports[m->nports++] = id;
		}
	}
}

/* the messages, each a task (the receiver's microtasks run between them) */
static void qf_deliver(void *pw)
{
	int budget = 0;
	struct qf_msg *m;

	(void) pw;
	qf_scheduled = false;
	for (m = qf_q; m != NULL; m = m->next)
		budget++;
	while (qf_q != NULL && budget-- > 0) {
		JSContext *ctx = NULL;
		struct qf_ctx *c;
		JSValue args[5], fn;
		JSRuntime *rt;
		int kind, i;

		m = qf_q;
		qf_q = m->next;
		if (qf_q == NULL)
			qf_qt = NULL;
		kind = m->kind;
		if (kind == 5) {
			struct browser_window *bw = onyx_frame_by_id(m->target);
			ctx = qf_realm(bw);
			if (ctx != NULL && strcmp(m->to, "*") != 0) {
				char key[QF_ORIGIN], shown[QF_ORIGIN];
				qf_origin(bw, qf_doc(bw), key, shown, sizeof key, 0);
				if (strcmp(m->to, "/") == 0 ? (strcmp(key, "null") == 0 ||
						strcmp(key, m->key) != 0) :
						(strcmp(shown, m->to) != 0 || strcmp(shown, "null") == 0))
					ctx = NULL;	/* (not for that document: dropped) */
			}
		} else {
			struct qf_port *p = qf_port(m->target);
			if (p != NULL && !p->closed && p->ctx == NULL) {
				/* (the port in flight: its messages wait for its realm) */
				m->next = NULL;
				if (p->qt != NULL) p->qt->next = m; else p->q = m;
				p->qt = m;
				continue;
			}
			if (p != NULL && !p->closed)
				ctx = p->ctx;
		}
		c = ctx != NULL ? qf_ctx_of(ctx, false) : NULL;
		if (c == NULL || !JS_IsFunction(ctx, c->hook)) {
			qf_msg_free(m, true);
			continue;
		}
		rt = JS_GetRuntime(ctx);
		fn = JS_DupValue(ctx, c->hook);
		args[1] = JS_UNDEFINED;
		if (m->buf != NULL) {
			args[1] = JS_ReadObject(ctx, m->buf, m->len, JS_READ_OBJ_REFERENCE);
			if (JS_IsException(args[1])) {
				JS_FreeValue(ctx, JS_GetException(ctx));
				args[1] = JS_UNDEFINED;
				kind += 2;		/* (7, 8: messageerror) */
			}
		}
		args[0] = JS_NewInt32(ctx, kind);
		args[2] = JS_NewInt32(ctx, m->kind == 5 ? m->source : m->target);
		args[3] = JS_NewString(ctx, m->origin != NULL ? m->origin : "");
		args[4] = JS_NewArray(ctx);
		for (i = 0; i < m->nports; i++)
			JS_SetPropertyUint32(ctx, args[4], (uint32_t) i, JS_NewInt32(ctx, m->ports[i]));
		qf_msg_free(m, false);
		qjs_invoke(ctx, fn, 5, args, "message");
		/* (the realm may be gone now: the runtime's frees) */
		JS_FreeValueRT(rt, fn);
		JS_FreeValueRT(rt, args[1]);
		JS_FreeValueRT(rt, args[3]);
		JS_FreeValueRT(rt, args[4]);
	}
	if (qf_q != NULL)
		qf_schedule();
}

/* ---- natives: the windows ------------------------------------------------------------------ */

/** frameSelf() -> this realm's window's frame id (0: a worker's) */
static JSValue n_frame_self(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	(void) this_val; (void) argc; (void) argv;
	return JS_NewInt32(ctx, onyx_frame_id(qjs_ctx_window(ctx)));
}

/** frameRel(id, which) -> the window's parent (0), top (1) frame id; 0: none */
static JSValue n_frame_rel(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct browser_window *bw = onyx_frame_by_id(qf_int(ctx, argc, argv, 0));

	(void) this_val;
	if (bw == NULL)
		return JS_NewInt32(ctx, 0);
	if (qf_int(ctx, argc, argv, 1) == 0)
		return JS_NewInt32(ctx, bw->parent != NULL ? onyx_frame_id(bw->parent) : 0);
	while (bw->parent != NULL)
		bw = bw->parent;
	return JS_NewInt32(ctx, onyx_frame_id(bw));
}

/** the caller's own window: its frames made for the <iframe>s its document has now (the
 *  parser's, a script's: window.length, window[i] see them at once, as in Chrome) */
static void qf_sync_own(JSContext *ctx, struct browser_window *bw)
{
	html_content *htmlc;

	if (bw == NULL || bw != qjs_ctx_window(ctx) || (htmlc = qjs_html_of(ctx)) == NULL)
		return;
	if (qf_frames_doc(bw) == htmlc)
		onyx_frames_sync(bw, htmlc, false);
}

/** frameCount(id) -> its child frames (window.length) */
static JSValue n_frame_count(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct browser_window *bw = onyx_frame_by_id(qf_int(ctx, argc, argv, 0));

	(void) this_val;
	qf_sync_own(ctx, bw);
	return JS_NewInt32(ctx, bw != NULL ? bw->iframe_count : 0);
}

/** frameChild(id, i) -> its i-th child frame's id, 0: none */
static JSValue n_frame_child(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct browser_window *bw = onyx_frame_by_id(qf_int(ctx, argc, argv, 0));
	int i = qf_int(ctx, argc, argv, 1);

	(void) this_val;
	if (bw != NULL && i >= bw->iframe_count)
		qf_sync_own(ctx, bw);
	if (bw == NULL || i < 0 || i >= bw->iframe_count)
		return JS_NewInt32(ctx, 0);
	return JS_NewInt32(ctx, onyx_frame_id(bw->iframes[i]));
}

/** frameNamed(id, name) -> its child frame of that name, 0: none */
static JSValue n_frame_named(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct browser_window *bw = onyx_frame_by_id(qf_int(ctx, argc, argv, 0));
	const char *name;
	int i, r = 0;

	(void) this_val;
	if (bw == NULL || argc < 2 || (name = JS_ToCString(ctx, argv[1])) == NULL)
		return JS_NewInt32(ctx, 0);
	for (i = 0; i < bw->iframe_count && r == 0; i++)
		if (bw->iframes[i]->name != NULL && strcmp(bw->iframes[i]->name, name) == 0)
			r = onyx_frame_id(bw->iframes[i]);
	JS_FreeCString(ctx, name);
	return JS_NewInt32(ctx, r);
}

/** frameAccess(id) -> the window's global object (same origin), false (another origin),
 *  null (no document running scripts, or the window is gone) */
static JSValue n_frame_access(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct browser_window *bw = onyx_frame_by_id(qf_int(ctx, argc, argv, 0));
	JSContext *tctx;

	(void) this_val;
	if (bw == NULL || (tctx = qf_realm(bw)) == NULL)
		return JS_NULL;
	if (!qf_same_origin(ctx, bw))
		return JS_FALSE;
	return JS_GetGlobalObject(tctx);
}

/** frameShown(id) -> whether the window shows a resource that is not a document (an image,
 *  a text) of the caller's origin: its contentDocument is then an empty document (Onyx,
 *  docs/06 §43; the other browsers make an image or a text document) */
static JSValue n_frame_shown(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct browser_window *bw = onyx_frame_by_id(qf_int(ctx, argc, argv, 0)), *self;
	char k1[QF_ORIGIN], s1[QF_ORIGIN], k2[QF_ORIGIN], s2[QF_ORIGIN];
	struct hlcache_handle *h;

	(void) this_val;
	if (bw == NULL || (h = bw->current_content) == NULL || content_get_type(h) == CONTENT_HTML)
		return JS_FALSE;
	qf_caller(ctx, &self, k1, s1);
	qf_origin_url(bw, hlcache_handle_get_url(h), k2, s2, QF_ORIGIN, 0);
	return JS_NewBool(ctx, strcmp(k1, "null") != 0 && strcmp(k1, k2) == 0);
}

/** frameAlive(id) -> whether the window is still there (closed) */
static JSValue n_frame_alive(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	(void) this_val;
	return JS_NewBool(ctx, onyx_frame_by_id(qf_int(ctx, argc, argv, 0)) != NULL);
}

/** frameOf(iframe) -> the frame id of an <iframe> of this document (made now if the
 *  element was just inserted), 0: none (not in the document) */
static JSValue n_frame_of(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct browser_window *bw = qjs_ctx_window(ctx), *f;
	html_content *htmlc = qjs_html_of(ctx);
	struct dom_node *el = argc > 0 ? qjs_node_of(argv[0]) : NULL;

	(void) this_val;
	if (bw == NULL || htmlc == NULL || el == NULL)
		return JS_NewInt32(ctx, 0);
	if (qf_frames_doc(bw) != htmlc)
		return JS_NewInt32(ctx, 0);
	f = onyx_frame_for_element(bw, htmlc, el);
	return JS_NewInt32(ctx, f != NULL ? onyx_frame_id(f) : 0);
}

/** frameElement() -> this window's <iframe> in its parent's document (same origin), else
 *  null */
static JSValue n_frame_element(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct browser_window *bw = qjs_ctx_window(ctx);
	html_content *owner;
	JSContext *pctx;

	(void) this_val; (void) argc; (void) argv;
	if (bw == NULL || bw->parent == NULL || bw->onyx_el == NULL ||
	    (owner = bw->onyx_owner) == NULL || owner->jsthread == NULL ||
	    (pctx = qjs_thread_context(owner->jsthread)) == NULL ||
	    !qf_same_origin(ctx, bw->parent))
		return JS_NULL;
	return qjs_wrap_node(pctx, bw->onyx_el);
}

/** frameName() -> window.name; frameName(v) sets it (the frame's target name) */
static JSValue n_frame_name(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct browser_window *bw = qjs_ctx_window(ctx);

	(void) this_val;
	if (bw == NULL)
		return JS_NewString(ctx, "");
	if (argc > 0) {
		const char *s = JS_ToCString(ctx, argv[0]);
		if (s == NULL)
			return JS_EXCEPTION;
		free(bw->name);
		bw->name = s[0] != '\0' ? strdup(s) : NULL;
		JS_FreeCString(ctx, s);
		return JS_UNDEFINED;
	}
	return JS_NewString(ctx, bw->name != NULL ? bw->name : "");
}

/** frameOrigin() -> this document's origin (as event.origin shows it) */
static JSValue n_frame_origin(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct browser_window *bw;
	char key[QF_ORIGIN], shown[QF_ORIGIN];

	(void) this_val; (void) argc; (void) argv;
	qf_caller(ctx, &bw, key, shown);
	return JS_NewString(ctx, shown);
}

/** framePost(id, value, targetOrigin, [port ids]): a message to the window, a task later */
static JSValue n_frame_post(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct browser_window *self;
	char key[QF_ORIGIN], shown[QF_ORIGIN];
	struct qf_msg *m;
	const char *to;

	(void) this_val;
	if (argc < 3)
		return JS_UNDEFINED;
	m = calloc(1, sizeof(*m));
	if (m == NULL)
		return JS_ThrowOutOfMemory(ctx);
	if (!qf_write(ctx, argv[1], m)) {
		qf_msg_free(m, false);
		return JS_EXCEPTION;
	}
	qf_caller(ctx, &self, key, shown);
	m->kind = 5;
	m->target = qf_int(ctx, argc, argv, 0);
	m->source = onyx_frame_id(self);
	m->origin = strdup(shown);
	m->key = strdup(key);
	to = JS_ToCString(ctx, argv[2]);
	m->to = strdup(to != NULL ? to : "/");
	if (to != NULL)
		JS_FreeCString(ctx, to);
	if (argc > 3)
		qf_take_ports(ctx, argv[3], m);
	if (m->origin == NULL || m->key == NULL || m->to == NULL) {
		qf_msg_free(m, true);
		return JS_ThrowOutOfMemory(ctx);
	}
	qf_enqueue(m);
	return JS_UNDEFINED;
}

/** whether the caller may send another window elsewhere: its own frames (and theirs), a
 *  same-origin window; an ancestor (top, parent: "frame busting") or another frame only with
 *  the user's activation (a click or a key in the caller's document in the last 5 s), and
 *  from a sandboxed frame only with allow-top-navigation -- Chrome's rules, simplified */
static bool qf_may_navigate(JSContext *ctx, struct browser_window *target)
{
	struct browser_window *self = qjs_ctx_window(ctx), *a;

	if (self == NULL)
		return false;
	for (a = target; a != NULL; a = a->parent)
		if (a == self)
			return true;
	if ((self->onyx_sandbox & ONYX_SANDBOX) && !(self->onyx_sandbox & ONYX_SANDBOX_TOP))
		return false;
	if (qf_same_origin(ctx, target))
		return true;
	if (qjs_ctx_activated(ctx, 5000))
		return true;
	NSLOG(netsurf, INFO, "a frame's navigation of another window refused (no user activation)");
	return false;
}

/** frameNavigate(id, url): another window sent to a URL (its location set: allowed across
 *  origins, within qf_may_navigate's rules) */
static JSValue n_frame_navigate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct browser_window *bw = onyx_frame_by_id(qf_int(ctx, argc, argv, 0));
	html_content *htmlc = qjs_html_of(ctx);
	const char *s;
	nsurl *url = NULL;

	(void) this_val;
	if (bw == NULL || argc < 2 || !qf_may_navigate(ctx, bw))
		return JS_UNDEFINED;
	if ((s = JS_ToCString(ctx, argv[1])) == NULL)
		return JS_EXCEPTION;
	if (htmlc != NULL)
		nsurl_join(content_get_url(&htmlc->base), s, &url);
	else
		nsurl_create(s, &url);
	JS_FreeCString(ctx, s);
	if (url != NULL) {
		browser_window_navigate(bw, url, htmlc != NULL ? content_get_url(&htmlc->base) : NULL,
				BW_NAVIGATE_HISTORY, NULL, NULL, NULL);
		nsurl_unref(url);
	}
	return JS_UNDEFINED;
}

/** frameHook(fn): fn(kind, data, a, origin, ports) gets this realm's messages -- 5 a
 *  window's (a: its source's frame id), 6 a port's (a: the port's id); 7, 8 the same that
 *  could not be read (messageerror) */
static JSValue n_frame_hook(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qf_ctx *c = qf_ctx_of(ctx, true);

	(void) this_val;
	if (c != NULL && argc > 0) {
		JS_FreeValue(ctx, c->hook);
		c->hook = JS_DupValue(ctx, argv[0]);
	}
	return JS_UNDEFINED;
}

/* ---- natives: the ports across realms -------------------------------------------------------- */

static struct qf_port *qf_port_new(void)
{
	struct qf_port *p = calloc(1, sizeof(*p));

	if (p == NULL)
		return NULL;
	p->id = ++qf_next_port;
	p->next = qf_ports;
	qf_ports = p;
	return p;
}

/** portPair() -> [a, b]: two entangled ports, held by no realm yet */
static JSValue n_port_pair(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qf_port *a = qf_port_new(), *b = qf_port_new();
	JSValue r;

	(void) this_val; (void) argc; (void) argv;
	if (a == NULL || b == NULL) {
		if (a != NULL) qf_port_close(a);
		if (b != NULL) qf_port_close(b);
		return JS_ThrowOutOfMemory(ctx);
	}
	a->other = b->id;
	b->other = a->id;
	r = JS_NewArray(ctx);
	JS_SetPropertyUint32(ctx, r, 0, JS_NewInt32(ctx, a->id));
	JS_SetPropertyUint32(ctx, r, 1, JS_NewInt32(ctx, b->id));
	return r;
}

/** portOwn(id): this realm holds the port (the messages that waited for it come) */
static JSValue n_port_own(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qf_port *p = qf_port(qf_int(ctx, argc, argv, 0));

	(void) this_val;
	if (p == NULL || p->closed)
		return JS_FALSE;
	p->ctx = ctx;
	qf_ctx_of(ctx, true);
	while (p->q != NULL) {
		struct qf_msg *m = p->q;
		p->q = m->next;
		qf_enqueue(m);
	}
	p->qt = NULL;
	return JS_TRUE;
}

/** portPost(id, value, [port ids]): a message from this end to the other */
static JSValue n_port_post(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qf_port *p = qf_port(qf_int(ctx, argc, argv, 0));
	struct qf_msg *m;

	(void) this_val;
	if (p == NULL || p->closed || p->other == 0 || argc < 2)
		return JS_UNDEFINED;
	m = calloc(1, sizeof(*m));
	if (m == NULL)
		return JS_ThrowOutOfMemory(ctx);
	if (!qf_write(ctx, argv[1], m)) {
		qf_msg_free(m, false);
		return JS_EXCEPTION;
	}
	m->kind = 6;
	m->target = p->other;
	if (argc > 2)
		qf_take_ports(ctx, argv[2], m);
	qf_enqueue(m);
	return JS_UNDEFINED;
}

/** portQueue(id, [values]): messages this realm's end had not given its scripts yet go
 *  with it (written here, before any other message for it) */
static JSValue n_port_queue(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qf_port *p = qf_port(qf_int(ctx, argc, argv, 0));
	struct qf_msg *first = NULL, *last = NULL;
	int64_t n = 0, i;

	(void) this_val;
	if (p == NULL || argc < 2 || !JS_IsArray(argv[1]) ||
	    JS_GetLength(ctx, argv[1], &n) < 0)
		return JS_UNDEFINED;
	for (i = 0; i < n; i++) {
		JSValue v = JS_GetPropertyUint32(ctx, argv[1], (uint32_t) i);
		struct qf_msg *m = calloc(1, sizeof(*m));
		if (m == NULL || !qf_write(ctx, v, m)) {
			JS_FreeValue(ctx, v);
			if (m != NULL)
				qf_msg_free(m, false);
			if (JS_HasException(ctx))
				JS_FreeValue(ctx, JS_GetException(ctx));
			continue;
		}
		JS_FreeValue(ctx, v);
		m->kind = 6;
		m->target = p->id;
		if (last != NULL) last->next = m; else first = m;
		last = m;
	}
	if (last != NULL) {
		last->next = p->q;
		if (p->q == NULL)
			p->qt = last;
		p->q = first;
	}
	return JS_UNDEFINED;
}

/** portClose(id): the port closed (its other end gets nothing more) */
static JSValue n_port_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qf_port *p = qf_port(qf_int(ctx, argc, argv, 0));

	(void) this_val;
	if (p != NULL)
		qf_port_close(p);
	return JS_UNDEFINED;
}

/** isNode(v) -> whether v is a node's wrapper, of any realm (a same-origin frame's node is
 *  not an instance of this realm's Node) */
static JSValue n_is_node(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	(void) this_val;
	return JS_NewBool(ctx, argc > 0 && qjs_node_of(argv[0]) != NULL);
}

static const JSCFunctionListEntry qf_natives[] = {
	JS_CFUNC_DEF("isNode", 1, n_is_node),
	JS_CFUNC_DEF("frameSelf", 0, n_frame_self),
	JS_CFUNC_DEF("frameRel", 2, n_frame_rel),
	JS_CFUNC_DEF("frameCount", 1, n_frame_count),
	JS_CFUNC_DEF("frameChild", 2, n_frame_child),
	JS_CFUNC_DEF("frameNamed", 2, n_frame_named),
	JS_CFUNC_DEF("frameAccess", 1, n_frame_access),
	JS_CFUNC_DEF("frameShown", 1, n_frame_shown),
	JS_CFUNC_DEF("frameAlive", 1, n_frame_alive),
	JS_CFUNC_DEF("frameOf", 1, n_frame_of),
	JS_CFUNC_DEF("frameElement", 0, n_frame_element),
	JS_CFUNC_DEF("frameName", 1, n_frame_name),
	JS_CFUNC_DEF("frameOrigin", 0, n_frame_origin),
	JS_CFUNC_DEF("framePost", 4, n_frame_post),
	JS_CFUNC_DEF("frameNavigate", 2, n_frame_navigate),
	JS_CFUNC_DEF("frameHook", 1, n_frame_hook),
	JS_CFUNC_DEF("portPair", 0, n_port_pair),
	JS_CFUNC_DEF("portOwn", 1, n_port_own),
	JS_CFUNC_DEF("portPost", 3, n_port_post),
	JS_CFUNC_DEF("portQueue", 2, n_port_queue),
	JS_CFUNC_DEF("portClose", 1, n_port_close),
};

/* exported interface documented in javascript/js.h */
void js_frames_changed(jsthread *thread)
{
	JSContext *ctx = qjs_thread_context(thread);
	struct browser_window *bw;
	struct qf_ctx *c;
	JSValue args[5], r;
	int k, n = 0;

	if (ctx == NULL || (c = qf_ctx_of(ctx, false)) == NULL || !JS_IsFunction(ctx, c->hook) ||
	    (bw = qjs_ctx_window(ctx)) == NULL)
		return;
	args[1] = JS_NewArray(ctx);
	for (k = 0; k < bw->iframe_count; k++)
		if (bw->iframes[k]->name != NULL && bw->iframes[k]->name[0] != '\0')
			JS_SetPropertyUint32(ctx, args[1], (uint32_t) n++,
					JS_NewString(ctx, bw->iframes[k]->name));
	args[0] = JS_NewInt32(ctx, 9);
	args[2] = JS_NewInt32(ctx, 0);
	args[3] = JS_NewString(ctx, "");
	args[4] = JS_NewArray(ctx);
	/* (called from a layout: no microtask checkpoint here -- only net.js' own code runs) */
	r = JS_Call(ctx, c->hook, JS_UNDEFINED, 5, (JSValueConst *) args);
	if (JS_IsException(r))
		JS_FreeValue(ctx, JS_GetException(ctx));
	JS_FreeValue(ctx, r);
	for (k = 0; k < 5; k++)
		JS_FreeValue(ctx, args[k]);
}

/* exported interface documented in qjs_frames.h */
void qjs_frames_natives(JSContext *ctx, JSValueConst natives)
{
	JS_SetPropertyFunctionList(ctx, natives, qf_natives,
			sizeof(qf_natives) / sizeof(qf_natives[0]));
}

/* exported interface documented in qjs_frames.h */
void qjs_frames_stop(JSContext *ctx)
{
	struct qf_ctx **pp, *c;
	struct qf_port *p, *pn;

	for (pp = &qf_ctxs; (c = *pp) != NULL; pp = &c->next)
		if (c->ctx == ctx) {
			*pp = c->next;
			JS_FreeValue(ctx, c->hook);
			free(c);
			break;
		}
	for (p = qf_ports; p != NULL; p = pn) {
		pn = p->next;
		if (p->ctx == ctx) {
			qf_port_close(p);
			pn = qf_ports;	/* (the list changed: again from its start) */
		}
	}
}
