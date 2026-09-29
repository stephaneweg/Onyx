/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Onyx: NetSurf's JavaScript on QuickJS (third_party/quickjs-ng-0.17.0: ES2023) in place of
 * Duktape (ES5).
 *
 * The DOM is libdom's, as NetSurf's own: this file wraps its nodes (one JS object a node,
 * kept for the document's life) and gives the prelude (dom.js, compiled in as
 * qjs_dom_js.h) a small set of native functions -- the tree, attributes, text, the
 * layout's boxes, the window's scroll, timers, navigation, cookies. The DOM API itself --
 * Node, Element, Document, events, selectors, classList, style, window's objects -- is
 * written in JavaScript on those. A script that changes the DOM marks the document: once
 * it is done, NetSurf builds its boxes again and lays it out (html_script_dom_changed).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>

#include <dom/dom.h>
#include <dom/bindings/hubbub/parser.h>

#include "quickjs.h"

#include "utils/utils.h"
#include "utils/log.h"
#include "utils/nsurl.h"
#include "utils/corestrings.h"
#include "utils/nsoption.h"
#include "netsurf/browser_window.h"
#include "netsurf/misc.h"
#include "netsurf/window.h"
#include "content/content.h"
#include "content/llcache.h"	/* Onyx: fetch / XMLHttpRequest (n_request) */
#include "utils/messages.h"	/* messages_get_errorcode (a request's error) */
#include "content/urldb.h"
#include "desktop/gui_internal.h"
#include "desktop/browser_private.h"
#include "desktop/browser_history.h"	/* browser_window_history_back / _forward */
#include <nsutils/time.h>		/* nsu_getmonotonic_ms */
#include "desktop/textarea.h"
#include "html/private.h"
#include "html/box.h"
#include "html/box_inspect.h"
#include "html/form_internal.h"

#include "javascript/js.h"
#include "javascript/content.h"

#include "qjs_dom_js.h"		/* dom.js, as a C string (the build makes it) */

/** the prototypes a node's wrapper gets, set by the prelude */
enum qjs_proto {
	QP_NODE, QP_ELEMENT, QP_TEXT, QP_COMMENT, QP_DOCUMENT, QP_FRAGMENT,
	QP_DOCTYPE, QP_COUNT
};

struct jsheap {
	JSRuntime *rt;
	int timeout;			/* s a script may run (0: no limit) */
	uint64_t start;			/* ms: when the running script started */
	int threads;
	bool pending_destroy;
};

struct qjs_wrap {
	dom_node *node;
	JSValue obj;
};

struct qjs_timer {
	struct qjs_timer *next;
	struct jsthread *t;
	int id;
	int ms;
	bool repeat;
	JSValue fn;
};

/* A request of fetch / XMLHttpRequest (dom.js), through the low-level cache. */
struct qjs_req {
	struct qjs_req *next;
	struct jsthread *t;
	int id;
	bool binary;			/* the body as an ArrayBuffer (else a string) */
	llcache_handle *handle;
	JSValue cb;			/* cb(error, { status, statusText, url, headers, body }) */
};

struct jsthread {
	jsheap *heap;
	JSContext *ctx;
	struct browser_window *bw;	/* the window (win_priv) */
	html_content *htmlc;		/* the document's content (doc_priv) */
	dom_document *doc;
	bool closed;			/* no more scripts nor callbacks */
	bool pending_destroy;
	int in_use;			/* calls into JS nested */
	bool dirty;			/* the DOM changed: a new layout due */
	struct qjs_wrap *wraps;		/* dom_node -> its wrapper (open addressing) */
	size_t nwraps, capwraps;
	JSValue protos[QP_COUNT];
	JSValue tag_protos;		/* { tag name: prototype } */
	JSValue dispatch;		/* dom.js' dispatcher: (target, type, init) */
	struct qjs_timer *timers;
	int next_timer;
	int load_waits;			/* the window's load: turns waited */
	struct qjs_req *reqs;		/* fetch / XMLHttpRequest in flight */
	int next_req;
};

static JSClassID qjs_node_class;
static bool qjs_debug;

static uint64_t qjs_now_ms(void)
{
	uint64_t ms = 0;
	nsu_getmonotonic_ms(&ms);
	return ms;
}


/* ---- errors -------------------------------------------------------------------------- */

static void qjs_report(JSContext *ctx, const char *where)
{
	JSValue e = JS_GetException(ctx);
	const char *msg = JS_ToCString(ctx, e);

	if (JS_IsError(e)) {
		JSValue st = JS_GetPropertyStr(ctx, e, "stack");
		const char *stack = JS_IsUndefined(st) ? NULL : JS_ToCString(ctx, st);

		NSLOG(netsurf, INFO, "JS %s: %s %s", where, msg ? msg : "?",
		      stack ? stack : "");
		if (qjs_debug)
			fprintf(stderr, "JS %s: %s\n%s\n", where, msg ? msg : "?",
				stack ? stack : "");
		if (stack)
			JS_FreeCString(ctx, stack);
		JS_FreeValue(ctx, st);
	} else {
		NSLOG(netsurf, INFO, "JS %s: %s", where, msg ? msg : "?");
		if (qjs_debug)
			fprintf(stderr, "JS %s: %s\n", where, msg ? msg : "?");
	}
	if (msg)
		JS_FreeCString(ctx, msg);
	JS_FreeValue(ctx, e);
}


/* ---- entering and leaving the engine ------------------------------------------------- */

static void qjs_thread_free(jsthread *t);
static void qjs_load_later(void *p);

static void qjs_enter(jsthread *t)
{
	if (t->in_use++ == 0 && t->heap != NULL)
		t->heap->start = qjs_now_ms();
}

/** Leaving JS: the promises' jobs run, a changed DOM laid out again. */
static void qjs_leave(jsthread *t)
{
	if (--t->in_use > 0)
		return;
	if (!t->closed) {
		JSContext *jctx;
		int r;

		while ((r = JS_ExecutePendingJob(JS_GetRuntime(t->ctx), &jctx)) != 0) {
			if (r < 0)
				qjs_report(jctx, "job");
		}
	}
	if (t->dirty) {
		t->dirty = false;
		if (!t->closed && t->htmlc != NULL)
			html_script_dom_changed(t->htmlc);
	}
	if (t->pending_destroy)
		qjs_thread_free(t);
}

/** a call into JS, its exception reported; the result (JS_UNDEFINED on error) */
static JSValue qjs_call(jsthread *t, JSValueConst fn, JSValueConst this_val, int argc,
		JSValueConst *argv, const char *where)
{
	JSValue r;

	qjs_enter(t);
	r = JS_Call(t->ctx, fn, this_val, argc, argv);
	if (JS_IsException(r)) {
		qjs_report(t->ctx, where);
		r = JS_UNDEFINED;
	}
	qjs_leave(t);
	return r;
}

static int qjs_interrupt(JSRuntime *rt, void *opaque)
{
	jsheap *heap = opaque;

	(void) rt;
	return heap->timeout > 0 &&
		qjs_now_ms() - heap->start > (uint64_t) heap->timeout * 1000;
}


/* ---- node wrappers --------------------------------------------------------------------- */

static void qjs_node_finalizer(JSRuntime *rt, JSValue val)
{
	dom_node *n = JS_GetOpaque(val, qjs_node_class);

	(void) rt;
	if (n != NULL)
		dom_node_unref(n);
}

static JSClassDef qjs_node_classdef = {
	.class_name = "OnyxNode",
	.finalizer = qjs_node_finalizer,
};

static size_t qjs_hash(dom_node *n, size_t cap)
{
	return (((uintptr_t) n >> 4) * 2654435761u) & (cap - 1);
}

static bool qjs_wraps_grow(jsthread *t)
{
	size_t cap = t->capwraps ? t->capwraps * 2 : 256, i;
	struct qjs_wrap *w = calloc(cap, sizeof(*w));

	if (w == NULL)
		return false;
	for (i = 0; i < t->capwraps; i++) {
		if (t->wraps[i].node != NULL) {
			size_t h = qjs_hash(t->wraps[i].node, cap);

			while (w[h].node != NULL)
				h = (h + 1) & (cap - 1);
			w[h] = t->wraps[i];
		}
	}
	free(t->wraps);
	t->wraps = w;
	t->capwraps = cap;
	return true;
}

static JSValue qjs_proto_for(jsthread *t, dom_node *n)
{
	dom_node_type type = DOM_ELEMENT_NODE;

	dom_node_get_node_type(n, &type);
	switch (type) {
	case DOM_ELEMENT_NODE: {
		dom_string *name = NULL;
		JSValue p = JS_UNDEFINED;

		if (dom_node_get_node_name(n, &name) == DOM_NO_ERR && name != NULL) {
			char tag[32];
			size_t len = dom_string_byte_length(name), i;

			if (len < sizeof(tag)) {
				for (i = 0; i < len; i++) {
					char ch = dom_string_data(name)[i];
					tag[i] = (ch >= 'A' && ch <= 'Z') ? ch + 32 : ch;
				}
				tag[len] = '\0';
				p = JS_GetPropertyStr(t->ctx, t->tag_protos, tag);
			}
			dom_string_unref(name);
		}
		if (JS_IsObject(p))
			return p;
		JS_FreeValue(t->ctx, p);
		return JS_DupValue(t->ctx, t->protos[QP_ELEMENT]);
	}
	case DOM_TEXT_NODE:
	case DOM_CDATA_SECTION_NODE:
		return JS_DupValue(t->ctx, t->protos[QP_TEXT]);
	case DOM_COMMENT_NODE:
		return JS_DupValue(t->ctx, t->protos[QP_COMMENT]);
	case DOM_DOCUMENT_NODE:
		return JS_DupValue(t->ctx, t->protos[QP_DOCUMENT]);
	case DOM_DOCUMENT_FRAGMENT_NODE:
		return JS_DupValue(t->ctx, t->protos[QP_FRAGMENT]);
	case DOM_DOCUMENT_TYPE_NODE:
		return JS_DupValue(t->ctx, t->protos[QP_DOCTYPE]);
	default:
		return JS_DupValue(t->ctx, t->protos[QP_NODE]);
	}
}

/** the node's wrapper (made the first time), or null */
static JSValue qjs_wrap(jsthread *t, dom_node *n)
{
	JSValue obj, proto;
	size_t h;

	if (n == NULL)
		return JS_NULL;
	if (t->capwraps != 0) {
		for (h = qjs_hash(n, t->capwraps); t->wraps[h].node != NULL;
		     h = (h + 1) & (t->capwraps - 1)) {
			if (t->wraps[h].node == n)
				return JS_DupValue(t->ctx, t->wraps[h].obj);
		}
	}
	if ((t->nwraps + 1) * 2 > t->capwraps && !qjs_wraps_grow(t))
		return JS_NULL;

	proto = qjs_proto_for(t, n);
	obj = JS_NewObjectProtoClass(t->ctx, proto, qjs_node_class);
	JS_FreeValue(t->ctx, proto);
	if (JS_IsException(obj))
		return JS_NULL;
	dom_node_ref(n);
	JS_SetOpaque(obj, n);

	for (h = qjs_hash(n, t->capwraps); t->wraps[h].node != NULL;
	     h = (h + 1) & (t->capwraps - 1))
		;
	t->wraps[h].node = n;
	t->wraps[h].obj = JS_DupValue(t->ctx, obj);
	t->nwraps++;
	return obj;
}

/** the node a wrapper holds (NULL: not a node) */
static dom_node *qjs_node(JSValueConst v)
{
	return JS_IsObject(v) ? JS_GetOpaque(v, qjs_node_class) : NULL;
}

#define QJS_T(ctx) ((jsthread *) JS_GetContextOpaque(ctx))


/* ---- strings ------------------------------------------------------------------------ */

static JSValue qjs_str(JSContext *ctx, dom_string *s)
{
	JSValue v;

	if (s == NULL)
		return JS_NULL;
	v = JS_NewStringLen(ctx, dom_string_data(s), dom_string_byte_length(s));
	dom_string_unref(s);
	return v;
}

/** a JS value as a dom_string (NULL on failure) */
static dom_string *qjs_dstr(JSContext *ctx, JSValueConst v)
{
	size_t len;
	const char *s = JS_ToCStringLen(ctx, &len, v);
	dom_string *d = NULL;

	if (s == NULL)
		return NULL;
	if (dom_string_create((const uint8_t *) s, len, &d) != DOM_NO_ERR)
		d = NULL;
	JS_FreeCString(ctx, s);
	return d;
}

#define QJS_NODE_ARG(var, i)						\
	dom_node *var = argc > (i) ? qjs_node(argv[i]) : NULL;		\
	if (var == NULL)						\
		return JS_ThrowTypeError(ctx, "not a node")


/* ---- natives: the tree --------------------------------------------------------------------- */

static JSValue n_type(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_node_type type = 0;
	QJS_NODE_ARG(n, 0);
	dom_node_get_node_type(n, &type);
	return JS_NewInt32(ctx, type);
}

static JSValue n_name(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_string *s = NULL;
	QJS_NODE_ARG(n, 0);
	dom_node_get_node_name(n, &s);
	return qjs_str(ctx, s);
}

#define QJS_REL(fname, getter)						\
static JSValue fname(JSContext *ctx, JSValueConst this_val, int argc,	\
		JSValueConst *argv)					\
{									\
	dom_node *r = NULL;						\
	JSValue v;							\
	QJS_NODE_ARG(n, 0);						\
	if (getter(n, &r) != DOM_NO_ERR)				\
		return JS_NULL;						\
	v = qjs_wrap(QJS_T(ctx), r);					\
	if (r != NULL)							\
		dom_node_unref(r);					\
	return v;							\
}
QJS_REL(n_parent, dom_node_get_parent_node)
QJS_REL(n_first, dom_node_get_first_child)
QJS_REL(n_last, dom_node_get_last_child)
QJS_REL(n_next, dom_node_get_next_sibling)
QJS_REL(n_prev, dom_node_get_previous_sibling)

/** children(n): an array of its child nodes */
static JSValue n_children(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	JSValue arr = JS_NewArray(ctx);
	dom_node *c = NULL, *next;
	uint32_t i = 0;
	QJS_NODE_ARG(n, 0);

	if (dom_node_get_first_child(n, &c) != DOM_NO_ERR)
		return arr;
	while (c != NULL) {
		JS_SetPropertyUint32(ctx, arr, i++, qjs_wrap(t, c));
		next = NULL;
		dom_node_get_next_sibling(c, &next);
		dom_node_unref(c);
		c = next;
	}
	return arr;
}

static JSValue n_document(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	return qjs_wrap(t, (dom_node *) t->doc);
}

static JSValue n_value(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_string *s = NULL;
	QJS_NODE_ARG(n, 0);
	dom_node_get_node_value(n, &s);
	return qjs_str(ctx, s);
}

static JSValue n_set_value(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_string *s;
	QJS_NODE_ARG(n, 0);
	s = qjs_dstr(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
	if (s != NULL) {
		dom_node_set_node_value(n, s);
		dom_string_unref(s);
		QJS_T(ctx)->dirty = true;
	}
	return JS_UNDEFINED;
}

static JSValue n_text(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_string *s = NULL;
	QJS_NODE_ARG(n, 0);
	dom_node_get_text_content(n, &s);
	if (s == NULL)
		return JS_NewString(ctx, "");
	return qjs_str(ctx, s);
}

static JSValue n_set_text(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_string *s;
	QJS_NODE_ARG(n, 0);
	s = qjs_dstr(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
	if (s != NULL) {
		dom_node_set_text_content(n, s);
		dom_string_unref(s);
		QJS_T(ctx)->dirty = true;
	}
	return JS_UNDEFINED;
}

static JSValue n_attr(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_string *name, *v = NULL;
	QJS_NODE_ARG(n, 0);
	name = qjs_dstr(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
	if (name == NULL)
		return JS_NULL;
	dom_element_get_attribute((dom_element *) n, name, &v);
	dom_string_unref(name);
	return qjs_str(ctx, v);
}

static JSValue n_set_attr(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_string *name, *v;
	QJS_NODE_ARG(n, 0);
	name = qjs_dstr(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
	v = qjs_dstr(ctx, argc > 2 ? argv[2] : JS_UNDEFINED);
	if (name != NULL && v != NULL) {
		if (dom_element_set_attribute((dom_element *) n, name, v) !=
				DOM_NO_ERR) {
			dom_string_unref(name);
			dom_string_unref(v);
			return JS_ThrowTypeError(ctx, "bad attribute");
		}
		QJS_T(ctx)->dirty = true;
	}
	if (name != NULL)
		dom_string_unref(name);
	if (v != NULL)
		dom_string_unref(v);
	return JS_UNDEFINED;
}

static JSValue n_remove_attr(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_string *name;
	QJS_NODE_ARG(n, 0);
	name = qjs_dstr(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
	if (name != NULL) {
		bool had = false;

		dom_element_has_attribute((dom_element *) n, name, &had);
		if (had) {
			dom_element_remove_attribute((dom_element *) n, name);
			QJS_T(ctx)->dirty = true;
		}
		dom_string_unref(name);
	}
	return JS_UNDEFINED;
}

/** attrs(n): [[name, value], ...] */
static JSValue n_attrs(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	JSValue arr = JS_NewArray(ctx);
	dom_namednodemap *map = NULL;
	uint32_t len = 0, i, k = 0;
	QJS_NODE_ARG(n, 0);

	if (dom_node_get_attributes(n, &map) != DOM_NO_ERR || map == NULL)
		return arr;
	dom_namednodemap_get_length(map, &len);
	for (i = 0; i < len; i++) {
		dom_node *a = NULL;
		dom_string *name = NULL, *value = NULL;

		if (dom_namednodemap_item(map, i, &a) != DOM_NO_ERR || a == NULL)
			continue;
		dom_attr_get_name((dom_attr *) a, &name);
		dom_attr_get_value((dom_attr *) a, &value);
		if (name != NULL) {
			JSValue pair = JS_NewArray(ctx);

			JS_SetPropertyUint32(ctx, pair, 0, qjs_str(ctx, name));
			JS_SetPropertyUint32(ctx, pair, 1, value != NULL ?
					qjs_str(ctx, value) : JS_NewString(ctx, ""));
			JS_SetPropertyUint32(ctx, arr, k++, pair);
		} else if (value != NULL) {
			dom_string_unref(value);
		}
		dom_node_unref(a);
	}
	dom_namednodemap_unref(map);
	return arr;
}

static JSValue n_create(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	dom_string *tag = qjs_dstr(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
	dom_element *e = NULL;
	JSValue v;

	if (tag == NULL)
		return JS_NULL;
	if (dom_document_create_element(t->doc, tag, &e) != DOM_NO_ERR || e == NULL) {
		dom_string_unref(tag);
		return JS_ThrowTypeError(ctx, "bad element name");
	}
	dom_string_unref(tag);
	v = qjs_wrap(t, (dom_node *) e);
	dom_node_unref(e);
	return v;
}

static JSValue n_create_text(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	dom_string *s = qjs_dstr(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
	dom_text *n = NULL;
	JSValue v;

	if (s == NULL)
		return JS_NULL;
	dom_document_create_text_node(t->doc, s, &n);
	dom_string_unref(s);
	v = qjs_wrap(t, (dom_node *) n);
	if (n != NULL)
		dom_node_unref(n);
	return v;
}

static JSValue n_create_comment(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	dom_string *s = qjs_dstr(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
	dom_comment *n = NULL;
	JSValue v;

	if (s == NULL)
		return JS_NULL;
	dom_document_create_comment(t->doc, s, &n);
	dom_string_unref(s);
	v = qjs_wrap(t, (dom_node *) n);
	if (n != NULL)
		dom_node_unref(n);
	return v;
}

static JSValue n_create_fragment(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	dom_document_fragment *f = NULL;
	JSValue v;

	dom_document_create_document_fragment(t->doc, &f);
	v = qjs_wrap(t, (dom_node *) f);
	if (f != NULL)
		dom_node_unref(f);
	return v;
}

/** insert(parent, child, before): before null appends; the child (thrown on error) */
static JSValue n_insert(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_node *ref = argc > 2 ? qjs_node(argv[2]) : NULL;
	dom_node *res = NULL;
	dom_exception e;
	QJS_NODE_ARG(p, 0);
	QJS_NODE_ARG(c, 1);

	if (ref != NULL)
		e = dom_node_insert_before(p, c, ref, &res);
	else
		e = dom_node_append_child(p, c, &res);
	if (e != DOM_NO_ERR)
		return JS_ThrowTypeError(ctx, "cannot insert that node here (%d)", e);
	if (res != NULL)
		dom_node_unref(res);
	QJS_T(ctx)->dirty = true;
	return JS_DupValue(ctx, argv[1]);
}

static JSValue n_remove(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_node *res = NULL;
	QJS_NODE_ARG(p, 0);
	QJS_NODE_ARG(c, 1);

	if (dom_node_remove_child(p, c, &res) != DOM_NO_ERR)
		return JS_ThrowTypeError(ctx, "not a child");
	if (res != NULL)
		dom_node_unref(res);
	QJS_T(ctx)->dirty = true;
	return JS_DupValue(ctx, argv[1]);
}

static JSValue n_clone(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_node *res = NULL;
	JSValue v;
	QJS_NODE_ARG(n, 0);

	if (dom_node_clone_node(n, argc > 1 && JS_ToBool(ctx, argv[1]), &res) !=
			DOM_NO_ERR)
		return JS_NULL;
	v = qjs_wrap(QJS_T(ctx), res);
	if (res != NULL)
		dom_node_unref(res);
	return v;
}

static JSValue n_by_id(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	dom_string *id = qjs_dstr(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
	dom_element *e = NULL;
	JSValue v;

	if (id == NULL)
		return JS_NULL;
	dom_document_get_element_by_id(t->doc, id, &e);
	dom_string_unref(id);
	v = qjs_wrap(t, (dom_node *) e);
	if (e != NULL)
		dom_node_unref(e);
	return v;
}

/**
 * setHTML(n, html): n's children replaced by the fragment parsed (innerHTML); on a
 * fragment of the document, not attached.
 */
static JSValue n_set_html(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	dom_hubbub_parser_params params;
	dom_hubbub_parser *parser = NULL;
	dom_document_fragment *fragment = NULL;
	dom_node *child = NULL, *html = NULL, *body = NULL, *res = NULL;
	dom_document_quirks_mode quirks = DOM_DOCUMENT_QUIRKS_MODE_NONE;
	size_t len;
	const char *s;
	QJS_NODE_ARG(n, 0);

	s = JS_ToCStringLen(ctx, &len, argc > 1 ? argv[1] : JS_UNDEFINED);
	if (s == NULL)
		return JS_EXCEPTION;
	/* (a fragment parser sets its document's quirks mode: the page's kept) */
	dom_document_get_quirks_mode(t->doc, &quirks);

	/* the old children out */
	while (dom_node_get_first_child(n, &child) == DOM_NO_ERR && child != NULL) {
		dom_node_remove_child(n, child, &res);
		if (res != NULL)
			dom_node_unref(res);
		dom_node_unref(child);
		child = NULL;
	}

	memset(&params, 0, sizeof(params));
	params.enc = "UTF-8";
	params.fix_enc = true;
	params.enable_script = false;
	if (dom_hubbub_fragment_parser_create(&params, t->doc, &parser,
			&fragment) != DOM_HUBBUB_OK) {
		JS_FreeCString(ctx, s);
		dom_document_set_quirks_mode(t->doc, quirks);
		return JS_UNDEFINED;
	}
	dom_hubbub_parser_parse_chunk(parser, (const uint8_t *) s, len);
	dom_hubbub_parser_completed(parser);
	JS_FreeCString(ctx, s);

	/* the fragment parser builds <html><body>...: its body's children moved in */
	if (dom_node_get_first_child(fragment, &html) == DOM_NO_ERR && html != NULL) {
		if (dom_node_get_last_child(html, &body) == DOM_NO_ERR && body != NULL) {
			while (dom_node_get_first_child(body, &child) == DOM_NO_ERR &&
			       child != NULL) {
				dom_node_remove_child(body, child, &res);
				if (res != NULL)
					dom_node_unref(res);
				dom_node_append_child(n, child, &res);
				if (res != NULL)
					dom_node_unref(res);
				dom_node_unref(child);
				child = NULL;
			}
			dom_node_unref(body);
		}
		dom_node_unref(html);
	}
	dom_hubbub_parser_destroy(parser);
	dom_node_unref(fragment);
	dom_document_set_quirks_mode(t->doc, quirks);
	t->dirty = true;
	return JS_UNDEFINED;
}

/** the node after n in document order under root, or NULL (n's reference taken) */
static dom_node *qjs_following(dom_node *n, dom_node *root)
{
	dom_node *next = NULL, *parent;

	if (dom_node_get_first_child(n, &next) == DOM_NO_ERR && next != NULL) {
		dom_node_unref(n);
		return next;
	}
	while (n != root) {
		next = NULL;
		if (dom_node_get_next_sibling(n, &next) == DOM_NO_ERR && next != NULL) {
			dom_node_unref(n);
			return next;
		}
		parent = NULL;
		dom_node_get_parent_node(n, &parent);
		dom_node_unref(n);
		n = parent;
		if (n == NULL)
			return NULL;
	}
	dom_node_unref(n);
	return NULL;
}

/** descendants(n): its descendant elements, in document order */
static JSValue n_descendants(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	JSValue arr = JS_NewArray(ctx);
	dom_node *d;
	uint32_t i = 0;
	QJS_NODE_ARG(root, 0);

	dom_node_ref(root);
	for (d = qjs_following(root, root); d != NULL; d = qjs_following(d, root)) {
		dom_node_type type = 0;

		if (dom_node_get_node_type(d, &type) == DOM_NO_ERR &&
		    type == DOM_ELEMENT_NODE)
			JS_SetPropertyUint32(ctx, arr, i++, qjs_wrap(t, d));
	}
	return arr;
}


/* ---- natives: the layout ---------------------------------------------------------------- */

static struct box *qjs_box(dom_node *n)
{
	struct box *box = NULL;

	if (dom_node_get_user_data(n, corestring_dom___ns_key_box_node_data,
			(void **) &box) != DOM_NO_ERR)
		return NULL;
	return box;
}

static void qjs_scroll(jsthread *t, int *sx, int *sy)
{
	*sx = *sy = 0;
	if (t->bw != NULL && t->bw->window != NULL &&
	    !guit->window->get_scroll(t->bw->window, sx, sy)) {
		*sx = *sy = 0;
	}
}

/** rect(n): [x, y, width, height] of its border box in the viewport (0s: no box) */
static JSValue n_rect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	JSValue arr = JS_NewArray(ctx);
	int x = 0, y = 0, w = 0, h = 0, sx, sy;
	struct box *box;
	QJS_NODE_ARG(n, 0);

	if (t->htmlc != NULL)
		html_script_layout_now(t->htmlc);	/* a pending layout done */
	box = qjs_box(n);
	if (box != NULL && t->htmlc != NULL && t->htmlc->layout != NULL) {
		box_coords(box, &x, &y);
		x -= box->border[LEFT].width;
		y -= box->border[TOP].width;
		w = box->padding[LEFT] + box->width + box->padding[RIGHT] +
			box->border[LEFT].width + box->border[RIGHT].width;
		h = box->padding[TOP] + box->height + box->padding[BOTTOM] +
			box->border[TOP].width + box->border[BOTTOM].width;
		if (box->type == BOX_INLINE && box->inline_end != NULL) {
			/* an inline: from its start to its end on the line */
			int ex, ey;

			box_coords(box->inline_end, &ex, &ey);
			if (ey == y + box->border[TOP].width && ex > x)
				w = ex - x;
		}
		qjs_scroll(t, &sx, &sy);
		x -= sx;
		y -= sy;
	}
	JS_SetPropertyUint32(ctx, arr, 0, JS_NewInt32(ctx, x));
	JS_SetPropertyUint32(ctx, arr, 1, JS_NewInt32(ctx, y));
	JS_SetPropertyUint32(ctx, arr, 2, JS_NewInt32(ctx, w));
	JS_SetPropertyUint32(ctx, arr, 3, JS_NewInt32(ctx, h));
	return arr;
}

/** boxed(n): whether the node has a box (it is displayed) */
static JSValue n_boxed(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	QJS_NODE_ARG(n, 0);

	if (t->htmlc != NULL)
		html_script_layout_now(t->htmlc);
	return JS_NewBool(ctx, qjs_box(n) != NULL);
}

/** scroll(): [x, y, viewport width, viewport height, page width, page height] */
static JSValue n_scroll(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	JSValue arr = JS_NewArray(ctx);
	int sx, sy, w = 0, h = 0, pw = 0, ph = 0;

	qjs_scroll(t, &sx, &sy);
	if (t->bw != NULL && t->bw->window != NULL)
		guit->window->get_dimensions(t->bw->window, &w, &h);
	if (t->htmlc != NULL && t->htmlc->layout != NULL) {
		pw = t->htmlc->layout->descendant_x1;
		ph = t->htmlc->layout->descendant_y1;
	}
	JS_SetPropertyUint32(ctx, arr, 0, JS_NewInt32(ctx, sx));
	JS_SetPropertyUint32(ctx, arr, 1, JS_NewInt32(ctx, sy));
	JS_SetPropertyUint32(ctx, arr, 2, JS_NewInt32(ctx, w));
	JS_SetPropertyUint32(ctx, arr, 3, JS_NewInt32(ctx, h));
	JS_SetPropertyUint32(ctx, arr, 4, JS_NewInt32(ctx, pw));
	JS_SetPropertyUint32(ctx, arr, 5, JS_NewInt32(ctx, ph));
	return arr;
}

static JSValue n_scroll_to(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	int x = 0, y = 0;
	struct rect r;

	if (argc > 0)
		JS_ToInt32(ctx, &x, argv[0]);
	if (argc > 1)
		JS_ToInt32(ctx, &y, argv[1]);
	if (t->bw != NULL && t->bw->window != NULL) {
		r.x0 = r.x1 = x < 0 ? 0 : x;
		r.y0 = r.y1 = y < 0 ? 0 : y;
		guit->window->set_scroll(t->bw->window, &r);
	}
	return JS_UNDEFINED;
}

/** cstyle(n, property): a few computed properties of its box, as CSS text */
static JSValue n_cstyle(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	const char *prop;
	struct box *box;
	char buf[64];
	JSValue v = JS_NewString(ctx, "");
	QJS_NODE_ARG(n, 0);

	if (t->htmlc != NULL)
		html_script_layout_now(t->htmlc);
	box = qjs_box(n);
	prop = JS_ToCString(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
	if (prop == NULL)
		return v;
	if (box == NULL || box->style == NULL) {
		if (strcmp(prop, "display") == 0) {
			JS_FreeValue(ctx, v);
			v = JS_NewString(ctx, "none");
		}
		JS_FreeCString(ctx, prop);
		return v;
	}
	buf[0] = '\0';
	if (strcmp(prop, "display") == 0) {
		static const char *d[] = { "inline", "inline", "block",
			"list-item", "run-in", "inline-block", "table",
			"inline-table", "table-row-group", "table-header-group",
			"table-footer-group", "table-row", "table-column-group",
			"table-column", "table-cell", "table-caption", "none",
			"flex", "inline-flex", "grid", "inline-grid" };
		uint8_t dv = css_computed_display(box->style, false);
		snprintf(buf, sizeof(buf), "%s", dv < sizeof(d) / sizeof(d[0]) ?
				d[dv] : "block");
	} else if (strcmp(prop, "visibility") == 0) {
		snprintf(buf, sizeof(buf), "%s", css_computed_visibility(
				box->style) == CSS_VISIBILITY_HIDDEN ? "hidden" :
				"visible");
	} else if (strcmp(prop, "position") == 0) {
		static const char *p[] = { "static", "static", "relative",
			"absolute", "fixed", "sticky" };
		uint8_t pv = css_computed_position(box->style);
		snprintf(buf, sizeof(buf), "%s", pv < 6 ? p[pv] : "static");
	} else if (strcmp(prop, "opacity") == 0) {
		css_fixed o = INTTOFIX(1);
		css_computed_opacity(box->style, &o);
		snprintf(buf, sizeof(buf), "%g", FIXTOFLT(o));
	} else if (strcmp(prop, "width") == 0) {
		snprintf(buf, sizeof(buf), "%dpx", box->width);
	} else if (strcmp(prop, "height") == 0) {
		snprintf(buf, sizeof(buf), "%dpx", box->height);
	} else if (strcmp(prop, "color") == 0 ||
		   strcmp(prop, "background-color") == 0) {
		css_color c = 0;
		if (prop[0] == 'c')
			css_computed_color(box->style, &c);
		else
			css_computed_background_color(box->style, &c);
		if ((c >> 24) == 0xff)
			snprintf(buf, sizeof(buf), "rgb(%u, %u, %u)",
				 (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
		else
			snprintf(buf, sizeof(buf), "rgba(%u, %u, %u, %g)",
				 (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff,
				 (c >> 24) / 255.0);
	} else if (strcmp(prop, "font-size") == 0) {
		css_fixed len = 0;
		css_unit unit = CSS_UNIT_PX;
		css_computed_font_size(box->style, &len, &unit);
		snprintf(buf, sizeof(buf), "%gpx", FIXTOFLT(len));
	}
	JS_FreeCString(ctx, prop);
	JS_FreeValue(ctx, v);
	return JS_NewString(ctx, buf);
}


/* ---- natives: form controls ------------------------------------------------------------- */

/** a form element's NetSurf control: its box's, else its form's (NULL: none yet) */
static struct form_control *qjs_control(jsthread *t, dom_node *n)
{
	struct box *box = qjs_box(n);
	struct form_control *ctl;
	struct form *f;

	if (box != NULL && box->gadget != NULL && box->gadget->node == n)
		return box->gadget;
	if (t->htmlc == NULL)
		return NULL;
	for (f = t->htmlc->forms; f != NULL; f = f->prev) {
		for (ctl = f->controls; ctl != NULL; ctl = ctl->next) {
			if (ctl->node == n)
				return ctl;
		}
	}
	return NULL;
}

static bool qjs_is_tag(dom_node *n, const char *tag)
{
	dom_string *name = NULL;
	bool is = false;

	if (dom_node_get_node_name(n, &name) == DOM_NO_ERR && name != NULL) {
		is = dom_string_byte_length(name) == strlen(tag) &&
			strncasecmp(dom_string_data(name), tag, strlen(tag)) == 0;
		dom_string_unref(name);
	}
	return is;
}

/** an option's item in its select's control (NULL: none), the control in *sel */
static struct form_option *qjs_option(jsthread *t, dom_node *n,
		struct form_control **sel)
{
	dom_node *p, *up;
	struct form_option *o;
	int depth;

	*sel = NULL;
	dom_node_ref(n);
	p = n;
	for (depth = 0; depth < 3 && p != NULL; depth++) {
		up = NULL;
		dom_node_get_parent_node(p, &up);
		dom_node_unref(p);
		p = up;
		if (p != NULL && qjs_is_tag(p, "select"))
			break;
	}
	if (p == NULL)
		return NULL;
	*sel = qjs_is_tag(p, "select") ? qjs_control(t, p) : NULL;
	dom_node_unref(p);
	if (*sel == NULL || (*sel)->type != GADGET_SELECT)
		return NULL;
	for (o = (*sel)->data.select.items; o != NULL; o = o->next) {
		if (o->node == n)
			return o;
	}
	return NULL;
}

static bool qjs_texty(struct form_control *ctl)
{
	return ctl->type == GADGET_TEXTBOX || ctl->type == GADGET_PASSWORD ||
		ctl->type == GADGET_TEXTAREA;
}

/** formValue(el): what a text control holds (null: no control; the attribute then) */
static JSValue n_form_value(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct form_control *ctl;
	QJS_NODE_ARG(n, 0);

	ctl = qjs_control(QJS_T(ctx), n);
	if (ctl == NULL || !qjs_texty(ctl) || ctl->value == NULL)
		return JS_NULL;
	return JS_NewString(ctx, ctl->value);
}

/** setFormValue(el, v): a text control's value, shown; without one, the DOM's */
static JSValue n_set_form_value(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	struct form_control *ctl;
	dom_string *v;
	const char *s;
	QJS_NODE_ARG(n, 0);

	s = JS_ToCString(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
	if (s == NULL)
		return JS_EXCEPTION;
	ctl = qjs_control(t, n);
	if (ctl != NULL && (qjs_texty(ctl) || ctl->type == GADGET_HIDDEN)) {
		char *dup = strdup(s);

		if (dup != NULL) {
			/* the control's value and the DOM's, then the text shown
			 * (syncing: not the user's input) */
			form_gadget_update_value(ctl, dup);
			if (ctl->type != GADGET_HIDDEN && ctl->data.text.ta != NULL &&
			    !ctl->syncing) {
				ctl->syncing = true;
				textarea_set_text(ctl->data.text.ta, s);
				ctl->syncing = false;
			}
			if (ctl->box != NULL && t->htmlc != NULL)
				html__redraw_a_box(t->htmlc, ctl->box);
		}
	} else if (dom_string_create((const uint8_t *) s, strlen(s), &v) ==
			DOM_NO_ERR) {
		if (qjs_is_tag(n, "textarea"))
			dom_html_text_area_element_set_value(
					(dom_html_text_area_element *) n, v);
		else
			dom_html_input_element_set_value(
					(dom_html_input_element *) n, v);
		dom_string_unref(v);
	}
	JS_FreeCString(ctx, s);
	return JS_UNDEFINED;
}

/** formChecked(el): a checkbox's, a radio's, an option's state (null: no control) */
static JSValue n_form_checked(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	struct form_control *ctl;
	struct form_option *o;
	QJS_NODE_ARG(n, 0);

	ctl = qjs_control(t, n);
	if (ctl != NULL && (ctl->type == GADGET_CHECKBOX || ctl->type == GADGET_RADIO))
		return JS_NewBool(ctx, ctl->selected);
	o = qjs_option(t, n, &ctl);
	if (o != NULL)
		return JS_NewBool(ctx, o->selected);
	return JS_NULL;
}

/** an option chosen or not in its select: the control, the DOM, the text shown */
static void qjs_select_option(jsthread *t, struct form_control *sel,
		struct form_option *opt, bool on)
{
	struct form_option *o;
	int i = 0, index = 0;

	for (o = sel->data.select.items; o != NULL; o = o->next, i++) {
		if (o == opt)
			index = i;
	}
	if (sel->box != NULL && sel->box->children != NULL &&
	    sel->box->children->children != NULL && sel->html != NULL) {
		/* as a choice in its menu: a single select's other options off */
		if (sel->data.select.multiple) {
			if (opt->selected != on)
				form_select_process_selection(sel, index);
		} else if (on) {
			form_select_process_selection(sel, index);
		} else if (opt->selected) {
			form_select_process_selection(sel, 0);
		}
		return;
	}
	/* not shown: the states alone */
	sel->data.select.num_selected = 0;
	sel->data.select.current = NULL;
	for (o = sel->data.select.items; o != NULL; o = o->next) {
		if (o == opt)
			o->selected = on;
		else if (on && !sel->data.select.multiple)
			o->selected = false;
		dom_html_option_element_set_selected(o->node, o->selected);
		if (o->selected) {
			sel->data.select.num_selected++;
			sel->data.select.current = o;
		}
	}
	(void) t;
}

/** setFormChecked(el, on): a checkbox, a radio, an option set; shown */
static JSValue n_set_form_checked(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	struct form_control *ctl;
	struct form_option *o;
	bool on = argc > 1 && JS_ToBool(ctx, argv[1]);
	QJS_NODE_ARG(n, 0);

	ctl = qjs_control(t, n);
	if (ctl != NULL && (ctl->type == GADGET_CHECKBOX || ctl->type == GADGET_RADIO)) {
		if (ctl->selected != on) {
			ctl->selected = on;
			dom_html_input_element_set_checked(
					(dom_html_input_element *) n, on);
			if (ctl->box != NULL && t->htmlc != NULL)
				html__redraw_a_box(t->htmlc, ctl->box);
			t->dirty = true;	/* (:checked) */
		}
		return JS_UNDEFINED;
	}
	o = qjs_option(t, n, &ctl);
	if (o != NULL) {
		qjs_select_option(t, ctl, o, on);
		return JS_UNDEFINED;
	}
	/* no control yet: the DOM's state, which the control will start from */
	if (qjs_is_tag(n, "option"))
		dom_html_option_element_set_selected((dom_html_option_element *) n, on);
	else
		dom_html_input_element_set_checked((dom_html_input_element *) n, on);
	t->dirty = true;
	return JS_UNDEFINED;
}


/**
 * submit(form, submitter): the form sent by NetSurf (its method, its encoding); false:
 * a form it does not know (made by a script), for dom.js to send
 */
static JSValue n_submit(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	dom_node *submitter = argc > 1 ? qjs_node(argv[1]) : NULL;
	struct form_control *ctl = NULL;
	struct form *f;
	QJS_NODE_ARG(n, 0);

	if (t->htmlc == NULL || t->bw == NULL || t->closed)
		return JS_FALSE;
	for (f = t->htmlc->forms; f != NULL; f = f->prev) {
		if (f->node == n)
			break;
	}
	if (f == NULL)
		return JS_FALSE;
	if (submitter != NULL) {
		ctl = qjs_control(t, submitter);
		if (ctl != NULL && ctl->form != f)
			ctl = NULL;
	}
	form_submit(content_get_url(&t->htmlc->base), t->bw, f, ctl);
	return JS_TRUE;
}


/* ---- natives: the window ------------------------------------------------------------------ */

static JSValue n_url(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	nsurl *url = t->htmlc != NULL ? content_get_url(&t->htmlc->base) : NULL;

	return JS_NewString(ctx, url != NULL ? nsurl_access(url) : "about:blank");
}

/** navigate(url): the window goes there (relative to the document) */
static JSValue n_navigate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	nsurl *base = t->htmlc != NULL ? content_get_url(&t->htmlc->base) : NULL;
	nsurl *url = NULL;
	const char *s = JS_ToCString(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);

	if (s == NULL)
		return JS_EXCEPTION;
	if (base != NULL)
		nsurl_join(base, s, &url);
	else
		nsurl_create(s, &url);
	JS_FreeCString(ctx, s);
	if (url != NULL && t->bw != NULL && !t->closed) {
		browser_window_navigate(t->bw, url, base, BW_NAVIGATE_HISTORY,
				NULL, NULL, NULL);
	}
	if (url != NULL)
		nsurl_unref(url);
	return JS_UNDEFINED;
}

static JSValue n_reload(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);

	if (t->bw != NULL && !t->closed)
		browser_window_reload(t->bw, false);
	return JS_UNDEFINED;
}

/** write(html): document.write while the document is parsed (after, nothing) */
static JSValue n_write(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	size_t len = 0;
	const char *s = JS_ToCStringLen(ctx, &len, argc > 0 ? argv[0] : JS_UNDEFINED);

	if (s == NULL)
		return JS_EXCEPTION;
	if (t->htmlc != NULL && t->htmlc->parser != NULL && len > 0)
		dom_hubbub_parser_insert_chunk(t->htmlc->parser, (const uint8_t *) s, len);
	JS_FreeCString(ctx, s);
	return JS_UNDEFINED;
}

static JSValue n_history(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	int d = 0;

	if (argc > 0)
		JS_ToInt32(ctx, &d, argv[0]);
	if (t->bw != NULL && !t->closed) {
		if (d < 0)
			browser_window_history_back(t->bw, false);
		else if (d > 0)
			browser_window_history_forward(t->bw, false);
	}
	return JS_UNDEFINED;
}

static JSValue n_cookie(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	nsurl *url = t->htmlc != NULL ? content_get_url(&t->htmlc->base) : NULL;
	char *c = url != NULL ? urldb_get_cookie(url, false) : NULL;
	JSValue v = JS_NewString(ctx, c != NULL ? c : "");

	free(c);
	return v;
}

static JSValue n_set_cookie(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	nsurl *url = t->htmlc != NULL ? content_get_url(&t->htmlc->base) : NULL;
	const char *s = JS_ToCString(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);

	if (s == NULL)
		return JS_EXCEPTION;
	if (url != NULL)
		urldb_set_cookie(s, url, NULL);
	JS_FreeCString(ctx, s);
	return JS_UNDEFINED;
}

static JSValue n_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const char *s = JS_ToCString(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);

	if (s != NULL) {
		NSLOG(netsurf, INFO, "console: %s", s);
		if (qjs_debug)
			fprintf(stderr, "console: %s\n", s);
		JS_FreeCString(ctx, s);
	}
	return JS_UNDEFINED;
}

static JSValue n_now(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	return JS_NewFloat64(ctx, (double) qjs_now_ms());
}

static JSValue n_user_agent(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	return JS_NewString(ctx, "Mozilla/5.0 (Onyx; aarch64) NetSurf/3.11");
}


/* ---- natives: timers -------------------------------------------------------------------- */

static void qjs_timer_unlink(jsthread *t, struct qjs_timer *tm)
{
	struct qjs_timer **p;

	for (p = &t->timers; *p != NULL; p = &(*p)->next) {
		if (*p == tm) {
			*p = tm->next;
			return;
		}
	}
}

static void qjs_timer_fire(void *p)
{
	struct qjs_timer *tm = p;
	jsthread *t = tm->t;
	JSValue fn, r;

	if (t->closed)
		return;
	fn = JS_DupValue(t->ctx, tm->fn);
	if (tm->repeat) {
		guit->misc->schedule(tm->ms, qjs_timer_fire, tm);
	} else {
		qjs_timer_unlink(t, tm);
		JS_FreeValue(t->ctx, tm->fn);
		free(tm);
	}
	r = qjs_call(t, fn, JS_UNDEFINED, 0, NULL, "timer");
	JS_FreeValue(t->ctx, r);
	JS_FreeValue(t->ctx, fn);
}

/** timer(fn, ms, repeat): its id */
static JSValue n_timer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	struct qjs_timer *tm;
	int ms = 0;

	if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
		return JS_NewInt32(ctx, 0);
	if (argc > 1)
		JS_ToInt32(ctx, &ms, argv[1]);
	if (ms < 0)
		ms = 0;
	tm = calloc(1, sizeof(*tm));
	if (tm == NULL)
		return JS_NewInt32(ctx, 0);
	tm->t = t;
	tm->id = ++t->next_timer;
	tm->repeat = argc > 2 && JS_ToBool(ctx, argv[2]);
	tm->ms = tm->repeat && ms < 4 ? 4 : ms;
	tm->fn = JS_DupValue(ctx, argv[0]);
	tm->next = t->timers;
	t->timers = tm;
	guit->misc->schedule(tm->ms, qjs_timer_fire, tm);
	return JS_NewInt32(ctx, tm->id);
}

static JSValue n_clear_timer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	struct qjs_timer *tm;
	int id = 0;

	if (argc > 0)
		JS_ToInt32(ctx, &id, argv[0]);
	for (tm = t->timers; tm != NULL; tm = tm->next) {
		if (tm->id == id) {
			guit->misc->schedule(-1, qjs_timer_fire, tm);
			qjs_timer_unlink(t, tm);
			JS_FreeValue(ctx, tm->fn);
			free(tm);
			break;
		}
	}
	return JS_UNDEFINED;
}

static void qjs_timers_stop(jsthread *t)
{
	while (t->timers != NULL) {
		struct qjs_timer *tm = t->timers;

		guit->misc->schedule(-1, qjs_timer_fire, tm);
		t->timers = tm->next;
		JS_FreeValue(t->ctx, tm->fn);
		free(tm);
	}
}


/* ---- natives: network requests (fetch, XMLHttpRequest) ----------------------------------- */

static void qjs_req_unlink(jsthread *t, struct qjs_req *r)
{
	struct qjs_req **pp;
	for (pp = &t->reqs; *pp != NULL; pp = &(*pp)->next) {
		if (*pp == r) {
			*pp = r->next;
			break;
		}
	}
}

static void qjs_req_free(struct qjs_req *r, bool abort)
{
	if (r->handle != NULL) {
		if (abort)
			llcache_handle_abort(r->handle);
		llcache_handle_release(r->handle);
	}
	JS_FreeValue(r->t->ctx, r->cb);
	free(r);
}

/** The response of a finished request, for dom.js. */
static JSValue qjs_req_result(JSContext *ctx, struct qjs_req *r)
{
	JSValue o = JS_NewObject(ctx), hs = JS_NewArray(ctx);
	const char *name, *value;
	const uint8_t *data;
	size_t size = 0, i;
	uint32_t n = 0;
	long code = llcache_handle_get_http_code(r->handle);
	const char *text = "";
	nsurl *u = llcache_handle_get_url(r->handle);

	for (i = 0; llcache_handle_get_header_at(r->handle, i, &name, &value); i++) {
		if (strncmp(name, "HTTP/", 5) == 0) {	/* the status line: its reason phrase */
			const char *sp = strchr(name, ' ');
			if (sp != NULL) sp = strchr(sp + 1, ' ');
			if (sp != NULL) text = sp + 1;
			continue;
		}
		JSValue pair = JS_NewArray(ctx);
		JS_SetPropertyUint32(ctx, pair, 0, JS_NewString(ctx, name));
		JS_SetPropertyUint32(ctx, pair, 1, JS_NewString(ctx, value));
		JS_SetPropertyUint32(ctx, hs, n++, pair);
	}
	data = llcache_handle_get_source_data(r->handle, &size);
	JS_SetPropertyStr(ctx, o, "status", JS_NewInt32(ctx, (int32_t) (code != 0 ? code : 200)));
	JS_SetPropertyStr(ctx, o, "statusText", JS_NewString(ctx, text));
	JS_SetPropertyStr(ctx, o, "url", JS_NewString(ctx, u != NULL ? nsurl_access(u) : ""));
	JS_SetPropertyStr(ctx, o, "headers", hs);
	if (r->binary)
		JS_SetPropertyStr(ctx, o, "body", JS_NewArrayBufferCopy(ctx, data, size));
	else
		JS_SetPropertyStr(ctx, o, "body", JS_NewStringLen(ctx, (const char *) data, size));
	return o;
}

static nserror qjs_req_cb(llcache_handle *handle, const llcache_event *event, void *pw)
{
	struct qjs_req *r = pw;
	jsthread *t = r->t;
	JSValue args[2];

	(void) handle;
	if (event->type != LLCACHE_EVENT_DONE && event->type != LLCACHE_EVENT_ERROR)
		return NSERROR_OK;		/* (headers, data, progress, redirects: the end only) */

	qjs_req_unlink(t, r);
	if (t->closed) {			/* (the document went meanwhile) */
		qjs_req_free(r, false);
		return NSERROR_OK;
	}
	if (event->type == LLCACHE_EVENT_DONE) {
		args[0] = JS_NULL;
		args[1] = qjs_req_result(t->ctx, r);
	} else {
		const char *m = event->data.error.msg;
		args[0] = JS_NewString(t->ctx, m != NULL ? m : messages_get_errorcode(event->data.error.code));
		args[1] = JS_NULL;
	}
	{
		/* (everything freed before qjs_leave: it frees a thread destroyed meanwhile) */
		JSContext *ctx = t->ctx;
		JSValue cb = JS_DupValue(ctx, r->cb), res;
		qjs_req_free(r, false);		/* (the handle released: the response copied) */
		qjs_enter(t);
		res = JS_Call(ctx, cb, JS_UNDEFINED, 2, args);
		if (JS_IsException(res))
			qjs_report(ctx, "request");
		JS_FreeValue(ctx, res);
		JS_FreeValue(ctx, cb);
		JS_FreeValue(ctx, args[0]);
		JS_FreeValue(ctx, args[1]);
		qjs_leave(t);
	}
	return NSERROR_OK;
}

/** request(method, url, body | null, [ "Name: value", ... ], binary, cb(error, response)) -> id,
 *  or -1: a bad URL (the caller reports a network error). */
static JSValue n_request(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	const char *method, *urls, *body = NULL;
	nsurl *base, *url = NULL;
	llcache_post_data post;
	char **hv = NULL;
	uint32_t nh = 0, i, k = 0;
	struct qjs_req *r;
	nserror err;
	bool get;

	(void) this_val;
	if (argc < 6 || t->closed || t->htmlc == NULL || !JS_IsFunction(ctx, argv[5]))
		return JS_NewInt32(ctx, -1);
	method = JS_ToCString(ctx, argv[0]);
	urls = JS_ToCString(ctx, argv[1]);
	if (method == NULL || urls == NULL) {
		JS_FreeCString(ctx, method);
		JS_FreeCString(ctx, urls);
		return JS_NewInt32(ctx, -1);
	}
	base = t->htmlc->base_url != NULL ? t->htmlc->base_url : content_get_url(&t->htmlc->base);
	err = nsurl_join(base, urls, &url);
	JS_FreeCString(ctx, urls);
	if (err != NSERROR_OK) {
		JS_FreeCString(ctx, method);
		return JS_NewInt32(ctx, -1);
	}
	if (!JS_IsNull(argv[2]) && !JS_IsUndefined(argv[2]))
		body = JS_ToCString(ctx, argv[2]);

	/* the headers: the caller's, and the method when it is neither GET nor POST (the Onyx
	 * fetcher takes "X-Onyx-Method" off the request and uses it) */
	{
		JSValue len = JS_GetPropertyStr(ctx, argv[3], "length");
		int32_t l = 0;
		JS_ToInt32(ctx, &l, len);
		JS_FreeValue(ctx, len);
		nh = l > 0 && l < 200 ? (uint32_t) l : 0;
	}
	hv = calloc(nh + 2, sizeof(char *));
	get = strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD") == 0;
	if (hv != NULL && !get && strcasecmp(method, "POST") != 0) {
		size_t ml = strlen(method) + 16;
		hv[k] = malloc(ml);
		if (hv[k] != NULL) { snprintf(hv[k], ml, "X-Onyx-Method: %s", method); k++; }
	}
	for (i = 0; hv != NULL && i < nh; i++) {
		JSValue v = JS_GetPropertyUint32(ctx, argv[3], i);
		const char *h = JS_ToCString(ctx, v);
		if (h != NULL) { hv[k++] = strdup(h); JS_FreeCString(ctx, h); }
		JS_FreeValue(ctx, v);
	}
	JS_FreeCString(ctx, method);

	r = calloc(1, sizeof *r);
	if (r == NULL) {
		nsurl_unref(url);
		if (body) JS_FreeCString(ctx, body);
		for (i = 0; hv != NULL && hv[i] != NULL; i++) free(hv[i]);
		free(hv);
		return JS_NewInt32(ctx, -1);
	}
	r->t = t;
	r->id = ++t->next_req;
	r->binary = JS_ToBool(ctx, argv[4]);
	r->cb = JS_DupValue(ctx, argv[5]);

	/* a body (or a method with one): as a POST's url-encoded data -- any text (not NUL) */
	post.type = LLCACHE_POST_URL_ENCODED;
	post.data.urlenc = (char *) (body != NULL ? body : "");
	err = llcache_handle_retrieve_ex(url,
		LLCACHE_RETRIEVE_FORCE_FETCH | LLCACHE_RETRIEVE_NO_ERROR_PAGES,
		content_get_url(&t->htmlc->base), (!get || body != NULL) ? &post : NULL,
		(const char *const *) hv, qjs_req_cb, r, &r->handle);
	nsurl_unref(url);
	if (body) JS_FreeCString(ctx, body);
	for (i = 0; hv != NULL && hv[i] != NULL; i++) free(hv[i]);
	free(hv);
	if (err != NSERROR_OK) {
		r->handle = NULL;
		qjs_req_free(r, false);
		return JS_NewInt32(ctx, -1);
	}
	r->next = t->reqs;
	t->reqs = r;
	return JS_NewInt32(ctx, r->id);
}

/** abortRequest(id) */
static JSValue n_abort_request(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	int32_t id = 0;
	struct qjs_req *r;

	(void) this_val;
	if (argc < 1 || JS_ToInt32(ctx, &id, argv[0]) < 0)
		return JS_UNDEFINED;
	for (r = t->reqs; r != NULL; r = r->next) {
		if (r->id == id) {
			qjs_req_unlink(t, r);
			qjs_req_free(r, true);
			break;
		}
	}
	return JS_UNDEFINED;
}

/** utf8(ArrayBuffer | typed array | string) -> the string its UTF-8 bytes make (TextDecoder,
 *  Response.text) */
static JSValue n_utf8(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	size_t size = 0, off = 0, len = 0, bpe = 0;
	uint8_t *p;

	(void) this_val;
	if (argc < 1)
		return JS_NewString(ctx, "");
	if (JS_IsString(argv[0]))
		return JS_DupValue(ctx, argv[0]);
	p = JS_GetArrayBuffer(ctx, &size, argv[0]);
	if (p != NULL)
		return JS_NewStringLen(ctx, (const char *) p, size);
	JS_FreeValue(ctx, JS_GetException(ctx));	/* (not an ArrayBuffer: a view?) */
	{
		JSValue buf = JS_GetTypedArrayBuffer(ctx, argv[0], &off, &len, &bpe);
		if (JS_IsException(buf)) {
			JS_FreeValue(ctx, JS_GetException(ctx));
			return JS_NewString(ctx, "");
		}
		p = JS_GetArrayBuffer(ctx, &size, buf);
		JS_FreeValue(ctx, buf);
		if (p == NULL || off + len > size) {
			JS_FreeValue(ctx, JS_GetException(ctx));
			return JS_NewString(ctx, "");
		}
		return JS_NewStringLen(ctx, (const char *) p + off, len);
	}
}

/* ---- natives: localStorage kept (a file per origin, next to the cookie file) ------------- */

/** the file of an origin's localStorage: <the cookie file's folder>/ls-<origin, cleaned>.json */
static bool qjs_storage_path(const char *origin, char *path, size_t cap)
{
	const char *cf = nsoption_charp(cookie_file);
	size_t n, i;

	if (cf == NULL || origin == NULL || origin[0] == '\0')
		return false;
	n = strlen(cf);
	while (n > 0 && cf[n - 1] != '/' && cf[n - 1] != ':')
		n--;				/* (its folder: up to the last '/' or the volume's ':') */
	if (n + 16 + strlen(origin) >= cap)
		return false;
	memcpy(path, cf, n);
	memcpy(path + n, "ls-", 3);
	n += 3;
	for (i = 0; origin[i] != '\0' && i < 120; i++) {
		char c = origin[i];
		path[n++] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			     (c >= '0' && c <= '9') || c == '.' || c == '-') ? c : '_';
	}
	memcpy(path + n, ".json", 6);
	return true;
}

/** storage(origin) -> the JSON kept for it, or null; storage(origin, json): keep it */
static JSValue n_storage(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	char path[512];
	const char *origin;
	bool ok;
	FILE *f;

	(void) this_val;
	if (argc < 1)
		return JS_NULL;
	origin = JS_ToCString(ctx, argv[0]);
	ok = qjs_storage_path(origin, path, sizeof path);
	JS_FreeCString(ctx, origin);
	if (!ok)
		return JS_NULL;
	if (argc < 2 || JS_IsUndefined(argv[1])) {
		JSValue v = JS_NULL;
		long len;
		char *b;
		f = fopen(path, "rb");
		if (f == NULL)
			return JS_NULL;
		fseek(f, 0, SEEK_END);
		len = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (len > 0 && len <= 5 * 1024 * 1024 && (b = malloc((size_t) len)) != NULL) {
			if (fread(b, 1, (size_t) len, f) == (size_t) len)
				v = JS_NewStringLen(ctx, b, (size_t) len);
			free(b);
		}
		fclose(f);
		return v;
	} else {
		size_t len;
		const char *s = JS_ToCStringLen(ctx, &len, argv[1]);
		if (s == NULL)
			return JS_FALSE;
		ok = false;
		if (len <= 5 * 1024 * 1024 && (f = fopen(path, "wb")) != NULL) {
			ok = fwrite(s, 1, len, f) == len;
			ok = fclose(f) == 0 && ok;
		}
		JS_FreeCString(ctx, s);
		return JS_NewBool(ctx, ok);
	}
}

static void qjs_reqs_stop(jsthread *t)
{
	while (t->reqs != NULL) {
		struct qjs_req *r = t->reqs;
		t->reqs = r->next;
		qjs_req_free(r, true);
	}
}


/* ---- natives: the prelude's hooks -------------------------------------------------------- */

/** setup({ node, element, text, comment, document, fragment, doctype, tags, dispatch }) */
static JSValue n_setup(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	static const char *names[QP_COUNT] = { "node", "element", "text",
		"comment", "document", "fragment", "doctype" };
	int i;

	if (argc < 1 || !JS_IsObject(argv[0]))
		return JS_UNDEFINED;
	for (i = 0; i < QP_COUNT; i++) {
		JS_FreeValue(ctx, t->protos[i]);
		t->protos[i] = JS_GetPropertyStr(ctx, argv[0], names[i]);
	}
	JS_FreeValue(ctx, t->tag_protos);
	t->tag_protos = JS_GetPropertyStr(ctx, argv[0], "tags");
	JS_FreeValue(ctx, t->dispatch);
	t->dispatch = JS_GetPropertyStr(ctx, argv[0], "dispatch");
	return JS_UNDEFINED;
}

static const JSCFunctionListEntry qjs_natives[] = {
	JS_CFUNC_DEF("type", 1, n_type),
	JS_CFUNC_DEF("name", 1, n_name),
	JS_CFUNC_DEF("parent", 1, n_parent),
	JS_CFUNC_DEF("first", 1, n_first),
	JS_CFUNC_DEF("last", 1, n_last),
	JS_CFUNC_DEF("next", 1, n_next),
	JS_CFUNC_DEF("prev", 1, n_prev),
	JS_CFUNC_DEF("children", 1, n_children),
	JS_CFUNC_DEF("document", 0, n_document),
	JS_CFUNC_DEF("value", 1, n_value),
	JS_CFUNC_DEF("setValue", 2, n_set_value),
	JS_CFUNC_DEF("text", 1, n_text),
	JS_CFUNC_DEF("setText", 2, n_set_text),
	JS_CFUNC_DEF("attr", 2, n_attr),
	JS_CFUNC_DEF("setAttr", 3, n_set_attr),
	JS_CFUNC_DEF("removeAttr", 2, n_remove_attr),
	JS_CFUNC_DEF("attrs", 1, n_attrs),
	JS_CFUNC_DEF("create", 1, n_create),
	JS_CFUNC_DEF("createText", 1, n_create_text),
	JS_CFUNC_DEF("createComment", 1, n_create_comment),
	JS_CFUNC_DEF("createFragment", 0, n_create_fragment),
	JS_CFUNC_DEF("insert", 3, n_insert),
	JS_CFUNC_DEF("remove", 2, n_remove),
	JS_CFUNC_DEF("clone", 2, n_clone),
	JS_CFUNC_DEF("byId", 1, n_by_id),
	JS_CFUNC_DEF("setHTML", 2, n_set_html),
	JS_CFUNC_DEF("descendants", 1, n_descendants),
	JS_CFUNC_DEF("formValue", 1, n_form_value),
	JS_CFUNC_DEF("setFormValue", 2, n_set_form_value),
	JS_CFUNC_DEF("formChecked", 1, n_form_checked),
	JS_CFUNC_DEF("setFormChecked", 2, n_set_form_checked),
	JS_CFUNC_DEF("submit", 2, n_submit),
	JS_CFUNC_DEF("rect", 1, n_rect),
	JS_CFUNC_DEF("boxed", 1, n_boxed),
	JS_CFUNC_DEF("scroll", 0, n_scroll),
	JS_CFUNC_DEF("scrollTo", 2, n_scroll_to),
	JS_CFUNC_DEF("cstyle", 2, n_cstyle),
	JS_CFUNC_DEF("url", 0, n_url),
	JS_CFUNC_DEF("navigate", 1, n_navigate),
	JS_CFUNC_DEF("reload", 0, n_reload),
	JS_CFUNC_DEF("write", 1, n_write),
	JS_CFUNC_DEF("history", 1, n_history),
	JS_CFUNC_DEF("cookie", 0, n_cookie),
	JS_CFUNC_DEF("setCookie", 1, n_set_cookie),
	JS_CFUNC_DEF("log", 1, n_log),
	JS_CFUNC_DEF("now", 0, n_now),
	JS_CFUNC_DEF("userAgent", 0, n_user_agent),
	JS_CFUNC_DEF("timer", 3, n_timer),
	JS_CFUNC_DEF("clearTimer", 1, n_clear_timer),
	JS_CFUNC_DEF("request", 6, n_request),
	JS_CFUNC_DEF("abortRequest", 1, n_abort_request),
	JS_CFUNC_DEF("utf8", 1, n_utf8),
	JS_CFUNC_DEF("storage", 2, n_storage),
	JS_CFUNC_DEF("setup", 1, n_setup),
};


/* ---- NetSurf's javascript interface ------------------------------------------------------- */

/* exported interface documented in js.h */
void js_initialise(void)
{
	qjs_debug = getenv("NS_JSDEBUG") != NULL;
	NSLOG(netsurf, INFO, "JavaScript: QuickJS %s", JS_GetVersion());
	javascript_init();	/* the script types: text/javascript... */
}

/* exported interface documented in js.h */
void js_finalise(void)
{
}

/* exported interface documented in js.h */
nserror js_newheap(int timeout, jsheap **heap)
{
	jsheap *h = calloc(1, sizeof(*h));

	if (h == NULL)
		return NSERROR_NOMEM;
	h->rt = JS_NewRuntime();
	if (h->rt == NULL) {
		free(h);
		return NSERROR_NOMEM;
	}
	h->timeout = timeout;
	/* a script's recursion stopped (RangeError) past 4 MB of stack -- the Onyx app's is
	 * 8 MB (its app.txt: stack = 8M), NetSurf's own frames below the JS */
	JS_SetMaxStackSize(h->rt, 4 * 1024 * 1024);
	JS_SetInterruptHandler(h->rt, qjs_interrupt, h);
	JS_NewClassID(h->rt, &qjs_node_class);
	JS_NewClass(h->rt, qjs_node_class, &qjs_node_classdef);
	*heap = h;
	return NSERROR_OK;
}

static void qjs_heap_free(jsheap *h)
{
	JS_FreeRuntime(h->rt);
	free(h);
}

/* exported interface documented in js.h */
void js_destroyheap(jsheap *heap)
{
	if (heap == NULL)
		return;
	if (heap->threads > 0) {
		heap->pending_destroy = true;
		return;
	}
	qjs_heap_free(heap);
}

/* exported interface documented in js.h */
nserror js_newthread(jsheap *heap, void *win_priv, void *doc_priv, jsthread **thread)
{
	jsthread *t;
	JSValue natives, prelude, r;
	int i;

	t = calloc(1, sizeof(*t));
	if (t == NULL)
		return NSERROR_NOMEM;
	t->heap = heap;
	t->bw = win_priv;
	t->htmlc = doc_priv;
	t->doc = t->htmlc != NULL ? t->htmlc->document : NULL;
	if (t->doc != NULL)
		dom_node_ref(t->doc);
	t->ctx = JS_NewContext(heap->rt);
	if (t->ctx == NULL) {
		if (t->doc != NULL)
			dom_node_unref(t->doc);
		free(t);
		return NSERROR_NOMEM;
	}
	for (i = 0; i < QP_COUNT; i++)
		t->protos[i] = JS_UNDEFINED;
	t->tag_protos = JS_UNDEFINED;
	t->dispatch = JS_UNDEFINED;
	JS_SetContextOpaque(t->ctx, t);
	heap->threads++;

	/* the prelude: a function of the natives, run once */
	natives = JS_NewObject(t->ctx);
	JS_SetPropertyFunctionList(t->ctx, natives, qjs_natives,
			sizeof(qjs_natives) / sizeof(qjs_natives[0]));
	qjs_enter(t);
	prelude = JS_Eval(t->ctx, qjs_dom_js, sizeof(qjs_dom_js) - 1, "dom.js",
			JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(prelude)) {
		qjs_report(t->ctx, "dom.js");
	} else {
		r = JS_Call(t->ctx, prelude, JS_UNDEFINED, 1, (JSValueConst *) &natives);
		if (JS_IsException(r))
			qjs_report(t->ctx, "dom.js setup");
		JS_FreeValue(t->ctx, r);
	}
	JS_FreeValue(t->ctx, prelude);
	JS_FreeValue(t->ctx, natives);
	t->dirty = false;	/* (nothing laid out yet) */
	qjs_leave(t);

	*thread = t;
	return NSERROR_OK;
}

/* exported interface documented in js.h */
nserror js_closethread(jsthread *thread)
{
	if (thread == NULL)
		return NSERROR_OK;
	thread->closed = true;
	qjs_timers_stop(thread);
	qjs_reqs_stop(thread);
	guit->misc->schedule(-1, qjs_load_later, thread);
	return NSERROR_OK;
}

static void qjs_thread_free(jsthread *t)
{
	jsheap *heap = t->heap;
	size_t i;
	int k;

	qjs_timers_stop(t);
	qjs_reqs_stop(t);
	guit->misc->schedule(-1, qjs_load_later, t);
	for (i = 0; i < t->capwraps; i++) {
		if (t->wraps[i].node != NULL)
			JS_FreeValue(t->ctx, t->wraps[i].obj);
	}
	free(t->wraps);
	for (k = 0; k < QP_COUNT; k++)
		JS_FreeValue(t->ctx, t->protos[k]);
	JS_FreeValue(t->ctx, t->tag_protos);
	JS_FreeValue(t->ctx, t->dispatch);
	JS_FreeContext(t->ctx);
	if (t->doc != NULL)
		dom_node_unref(t->doc);
	free(t);
	heap->threads--;
	if (heap->pending_destroy && heap->threads == 0)
		qjs_heap_free(heap);
}

/* exported interface documented in js.h */
void js_destroythread(jsthread *thread)
{
	if (thread == NULL)
		return;
	thread->closed = true;
	thread->htmlc = NULL;
	thread->bw = NULL;
	if (thread->in_use > 0) {
		thread->pending_destroy = true;
		return;
	}
	qjs_thread_free(thread);
}

/* exported interface documented in js.h */
bool js_exec(jsthread *thread, const uint8_t *txt, size_t txtlen, const char *name)
{
	char *src;
	JSValue r;
	bool ok = false;

	if (thread == NULL || thread->closed || txt == NULL || txtlen == 0)
		return false;
	/* JS_Eval wants its source NUL-terminated */
	src = malloc(txtlen + 1);
	if (src == NULL)
		return false;
	memcpy(src, txt, txtlen);
	src[txtlen] = '\0';

	qjs_enter(thread);
	r = JS_Eval(thread->ctx, src, txtlen, name != NULL ? name : "script",
			JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(r)) {
		qjs_report(thread->ctx, name != NULL ? name : "script");
	} else {
		ok = JS_ToBool(thread->ctx, r);
	}
	JS_FreeValue(thread->ctx, r);
	qjs_leave(thread);
	free(src);
	return ok;
}

/* exported interface documented in js.h */
bool js_dispatch_event(jsthread *thread, const char *type, struct dom_node *target,
		const struct js_event_init *init)
{
	JSContext *ctx;
	JSValue args[3], r, o;
	bool ok = true;

	if (thread == NULL || thread->closed || !JS_IsFunction(thread->ctx,
			thread->dispatch))
		return true;
	ctx = thread->ctx;
	o = JS_NewObject(ctx);
	if (init != NULL) {
		int sx, sy;

		qjs_scroll(thread, &sx, &sy);	/* (the page's point in the viewport) */
		JS_SetPropertyStr(ctx, o, "clientX", JS_NewInt32(ctx, init->x - sx));
		JS_SetPropertyStr(ctx, o, "clientY", JS_NewInt32(ctx, init->y - sy));
		JS_SetPropertyStr(ctx, o, "button", JS_NewInt32(ctx, init->button));
		if (init->key != NULL)
			JS_SetPropertyStr(ctx, o, "key", JS_NewString(ctx, init->key));
		JS_SetPropertyStr(ctx, o, "keyCode", JS_NewInt32(ctx, init->key_code));
		JS_SetPropertyStr(ctx, o, "shiftKey", JS_NewBool(ctx, init->shift));
		JS_SetPropertyStr(ctx, o, "ctrlKey", JS_NewBool(ctx, init->ctrl));
		JS_SetPropertyStr(ctx, o, "altKey", JS_NewBool(ctx, init->alt));
	}
	args[0] = target != NULL ? qjs_wrap(thread, target) : JS_NULL;
	args[1] = JS_NewString(ctx, type);
	args[2] = o;
	r = qjs_call(thread, thread->dispatch, JS_UNDEFINED, 3,
			(JSValueConst *) args, type);
	if (JS_IsBool(r))
		ok = JS_ToBool(ctx, r);
	JS_FreeValue(ctx, r);
	JS_FreeValue(ctx, args[0]);
	JS_FreeValue(ctx, args[1]);
	JS_FreeValue(ctx, args[2]);
	return ok;
}

/**
 * The window's load: once the document is laid out and its objects are in (its images:
 * the content done) -- a script then measures the boxes. Given up waiting after 30 s.
 */
static void qjs_load_later(void *p)
{
	jsthread *t = p;

	if (t->closed || t->htmlc == NULL)
		return;
	if ((!t->htmlc->had_initial_layout ||
	     t->htmlc->base.status != CONTENT_STATUS_DONE) &&
	    ++t->load_waits < 600) {
		guit->misc->schedule(50, qjs_load_later, t);
		return;
	}
	js_dispatch_event(t, "onyx:complete", NULL, NULL);
}

/* exported interface documented in js.h */
bool js_fire_event(jsthread *thread, const char *type, struct dom_document *doc,
		struct dom_node *target)
{
	(void) doc;
	if (thread == NULL)
		return false;
	if (strcmp(type, "load") == 0 && target == NULL) {
		/* the document parsed: readyState interactive, DOMContentLoaded (dom.js);
		 * the window's load later */
		js_dispatch_event(thread, "onyx:interactive", NULL, NULL);
		guit->misc->schedule(50, qjs_load_later, thread);
		return true;
	}
	return js_dispatch_event(thread, type, target, NULL);
}

/* exported interface documented in js.h */
bool js_dom_event_add_listener(jsthread *thread, struct dom_document *document,
		struct dom_node *node, struct dom_string *event_type_dom,
		void *js_funcval)
{
	(void) thread; (void) document; (void) node; (void) event_type_dom;
	(void) js_funcval;
	return false;
}

/* exported interface documented in js.h */
void js_handle_new_element(jsthread *thread, struct dom_element *node)
{
	/* (the on* attributes are read when an event is dispatched: dom.js) */
	(void) thread;
	(void) node;
}

/* exported interface documented in js.h */
void js_event_cleanup(jsthread *thread, struct dom_event *evt)
{
	(void) thread;
	(void) evt;
}
