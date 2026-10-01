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
#include <ctype.h>
#include <strings.h>
#include <stdint.h>

#include <dom/dom.h>
#include <dom/bindings/hubbub/parser.h>

#include "quickjs.h"

#include "utils/utils.h"
#include "utils/log.h"
#include "utils/nsurl.h"
#include "utils/useragent.h"
#include "css/select.h"
#include "netsurf/onyx_perf.h"
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
#include "content/hlcache.h"	/* Onyx: hlcache_handle_get_content (a frame's parent) */
#include "desktop/browser_history.h"	/* browser_window_history_back / _forward */
#include <nsutils/time.h>		/* nsu_getmonotonic_ms */
#include "desktop/textarea.h"
#include "desktop/scrollbar.h"	/* Onyx: an element's scroll position (n_box_scroll) */
#include "html/private.h"
#include "html/html.h"		/* Onyx: struct html_stylesheet (n_sheet_text) */
#include "netsurf/content.h"	/* Onyx: content_get_source_data, hlcache_handle_get_url */
#include "html/box.h"
#include "html/object.h"	/* Onyx: html_object_flush_sync */
#include "html/box_inspect.h"
#include "html/form_internal.h"
#include "css/utils.h"		/* Onyx: nscss_screen_dpi (unboxed styles) */
#include "html/css.h"		/* Onyx: html_css_new_selection_context (unboxed styles) */
#include "html/onyx_restyle.h"	/* Onyx: the kept selections (a state set) */
#include "html/box_construct.h"	/* Onyx: box_style_select (unboxed styles) */
#include "html/onyx_shadow.h"	/* Onyx: shadow DOM (adopted sheets) */
#include "html/onyx_anim.h"	/* Onyx: animations, requestAnimationFrame */

#include "javascript/js.h"
#include "html/onyx_webfont.h"
#include "html/onyx_fx.h"		/* Onyx: transformed rectangles */
#include "javascript/content.h"

#include "qjs_dom_js.h"		/* dom.js, as a C string (the build makes it) */
#include "qjs_html5_js.h"	/* Onyx: html5.js, the same way */
#include "javascript/quickjs/qjs_canvas.h"	/* Onyx: <canvas> 2D (qjs_canvas.c) */
#define QJS_MEDIA_JS
#include "javascript/quickjs/qjs_media.h"	/* Onyx: <video>, <audio>, MSE (qjs_media.c) */
#include "javascript/quickjs/qjs_wasm.h"	/* Onyx: WebAssembly, Web Crypto */
#include "qjs_intl.h"		/* Onyx: Intl (intl.js), before dom.js in each context */
#include "javascript/quickjs/qjs_net.h"	/* Onyx: WebSocket, EventSource, Workers (qjs_net.c) */
#include "javascript/quickjs/qjs_codecache.h"	/* Onyx: the scripts' bytecode on the card */
#include "javascript/quickjs/qjs_frames.h"
#include "javascript/quickjs/qjs_xml.h"	/* Onyx: the frames' windows (qjs_frames.c) */
#include "desktop/frames.h"		/* Onyx: an iframe's load (onyx_frame_loaded) */
#include "netsurf/onyx_jet.h"		/* Onyx: <a download> (n_download) */

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
	/* Onyx: the time limit spares a script still changing the page (qjs_interrupt) */
	uint64_t busy;			/* ms: the running script's last DOM change seen */
	uint64_t writes;		/* qjs_dom_writes then */
	bool told;			/* (logged once a script) */
	/* Onyx: a tab's iframes share its heap (js_heap_share): one runtime, the realms of
	 * its frames reach each other's objects */
	int refs;			/* its windows (js_destroyheap gives one back) */
	bool shared;			/* ever shared: a thread's record outlives it (zombies) */
	struct jsthread *live;		/* its threads (each one's hnext) */
	struct jsthread *zombies;	/* the threads freed while their realm may still be
					 * reached from another (freed with the heap) */
};

/* Onyx: the scripts' changes of the DOM, counted (QJS_DIRTY, where a change marks the page
 * to lay out again): the time limit's sign that a long script is making progress */
static uint64_t qjs_dom_writes;
#define QJS_DIRTY(t) ((t)->dirty = true, qjs_dom_writes++)

struct qjs_wrap {
	dom_node *node;
	JSValue obj;
};

struct qjs_timer {
	struct qjs_timer *next;
	struct qjs_timer *prev;		/* Onyx: its thread's list is doubly linked */
	struct qjs_timer *hnext;	/* Onyx: in its id's bucket (jsthread.tbuck) */
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
	/* Onyx: the response as it comes (streams, XHR's progress) -- pcb(0, head) when its
	 * head is in; pcb(1, ArrayBuffer) each part once the script reads the body as a stream
	 * (flags & QJS_REQ_CHUNKS); pcb(2, bytes so far) at each part (flags & QJS_REQ_COUNTS) */
	JSValue pcb;
	int flags;
	size_t given;			/* bytes given as parts */
	bool in_cb;			/* its callback runs: an abort waits till it returns */
	bool dead;			/* ... and one came */
};
#define QJS_REQ_CHUNKS	1
#define QJS_REQ_COUNTS	2

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
	int forced_layouts;		/* Onyx: layouts a script's reads forced this turn */
	uint64_t forced_ms;		/* Onyx: and the time they took (ms) */
	nsurl *url_override;		/* Onyx: history.pushState's URL (the document's now) */
	JSValue ce_hook;		/* Onyx: html5.js' custom elements check of an element
					 * the parser inserted (undefined: none defined) */
	dom_node **ce_pending;		/* Onyx: the elements inserted, for ce_hook (later: the
					 * DOM is read-only in a mutation event) */
	int ce_npending, ce_cappending;
	struct qjs_wrap *wraps;		/* dom_node -> its wrapper (open addressing) */
	size_t nwraps, capwraps;
	JSValue protos[QP_COUNT];
	JSValue tag_protos;		/* { tag name: prototype } */
	JSValue dispatch;		/* dom.js' dispatcher: (target, type, init) */
	struct qjs_timer *timers;
	/* Onyx: the timers by id (clearTimeout) -- the list was searched for the id, and
	 * searched again to unlink a timer firing: n timers, n^2 */
#define QJS_TBUCKETS 256
	struct qjs_timer *tbuck[QJS_TBUCKETS];
	int next_timer;
	int load_waits;			/* the window's load: turns waited */
	struct qjs_req *reqs;		/* fetch / XMLHttpRequest in flight */
	int next_req;
	JSValue shadow_proto;		/* Onyx: ShadowRoot.prototype (html5.js) */
	JSValue modsrc;			/* ES modules' sources: { url: text } (dom.js fills it) */
	JSValue modmissing;		/* the modules a moduleRun lacked: [url...] */
	bool worker;			/* Onyx: a worker's scripts (qjs_net.c): no document; its URL
					 * in url_override */
	struct jsthread *hnext;		/* Onyx: its heap's live list, then its zombie list */
	uint64_t activated;		/* Onyx: ms of the user's last click / key in it (a frame
					 * may then navigate its top window: qjs_frames.c) */
	bool zombie;			/* Onyx: freed, its record kept: a realm of a shared heap
					 * whose functions another frame may still call (each
					 * native finds it closed) */
	struct jsthread *vnext;		/* Onyx: every live thread (visibilitychange) */
};

/* Onyx: the live threads (js_view_visibility_changed) */
static struct jsthread *qjs_all;
static void qjs_visibility_later(void *p);

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

static void qjs_ce_flush(jsthread *t);
static void qjs_ce_later(void *p);

/* Onyx (the PC bench): NS_JSPROF=<file> -- the scripts sampled: at most once a millisecond,
 * from the interrupt handler, the running script's stack written as a line (its frames
 * separated by '|'); tools/tests/netsurf/jsprof.py sums them up */
static FILE *qjs_prof_f;
static JSContext *qjs_prof_ctx;
static uint64_t qjs_prof_last;

static void qjs_prof_sample(void)
{
	JSValue e, st;
	const char *s, *p;
	uint64_t now = qjs_now_ms();

	if (now == qjs_prof_last || qjs_prof_ctx == NULL)
		return;
	qjs_prof_last = now;
	e = JS_NewError(qjs_prof_ctx);
	if (JS_IsException(e))
		return;
	st = JS_GetPropertyStr(qjs_prof_ctx, e, "stack");
	s = JS_ToCString(qjs_prof_ctx, st);
	if (s != NULL) {
		for (p = s; *p != '\0'; p++)
			fputc(*p == '\n' ? '|' : *p, qjs_prof_f);
		fputc('\n', qjs_prof_f);
		JS_FreeCString(qjs_prof_ctx, s);
	}
	JS_FreeValue(qjs_prof_ctx, st);
	JS_FreeValue(qjs_prof_ctx, e);
}

/* Onyx: a script (a call, a job) starts: its time counted from now */
static void qjs_clock_start(jsheap *heap)
{
	heap->start = heap->busy = qjs_now_ms();
	heap->writes = qjs_dom_writes;
	heap->told = false;
}

static void qjs_enter(jsthread *t)
{
	/* Onyx: the custom elements the parser inserted since, upgraded before any script */
	if (t->in_use == 0 && t->ce_npending > 0)
		qjs_ce_flush(t);
	if (t->in_use++ == 0 && t->heap != NULL)
		qjs_clock_start(t->heap);
	qjs_prof_ctx = t->ctx;
}

static void qjs_jobs_later(void *p);

/** Leaving JS: the promises' jobs run, a changed DOM laid out again. */
static void qjs_leave(jsthread *t)
{
	if (--t->in_use > 0)
		return;
	if (!t->closed) {
		JSContext *jctx;
		JSRuntime *rt = JS_GetRuntime(t->ctx);
		uint64_t t0 = qjs_now_ms();
		int r, n = 0;

		/* Onyx: the jobs run for 200 ms at most, then the rest a turn later (the window
		 * answers): a chain of promises that never ends (bbc.co.uk's consent script, its
		 * Promise polyfill) held the loop for ever -- each job "interrupted" at once once
		 * the script's time was out, the next one queued again -- and took memory till
		 * none was left. Each turn gives the jobs their own time. */
		uint64_t tj = onyx_perf_now();

		while ((r = JS_ExecutePendingJob(rt, &jctx)) != 0) {
			if (r < 0)
				qjs_report(jctx, "job");
			if (onyx_perf_on()) {	/* (NS_PERF: the long jobs) */
				if (onyx_perf_now() - tj > 50000)
					onyx_perf_log("js:job", tj);
				tj = onyx_perf_now();
			}
			if ((++n & 63) == 0 && qjs_now_ms() - t0 > 200 &&
					JS_IsJobPending(rt)) {
				guit->misc->schedule(10, qjs_jobs_later, t);
				break;
			}
			if (t->heap != NULL)
				qjs_clock_start(t->heap);
		}
	}
	t->forced_layouts = 0;
	t->forced_ms = 0;
	if (t->dirty) {
		t->dirty = false;
		if (!t->closed && t->htmlc != NULL)
			html_script_dom_changed_by_script(t->htmlc);	/* (Onyx) */
	}
	/* Onyx: the other frames' documents a script changed (a same-origin frame's DOM, or
	 * their promises' jobs above: the runtime's queue is shared) */
	if (t->heap != NULL && t->heap->shared) {
		jsthread *o;
		for (o = t->heap->live; o != NULL; o = o->hnext)
			if (o != t && o->dirty && o->in_use == 0) {
				o->dirty = false;
				if (!o->closed && o->htmlc != NULL)
					html_script_dom_changed(o->htmlc);
			}
	}
	if (t->pending_destroy)
		qjs_thread_free(t);
}

/** the promises' jobs left over by qjs_leave (Onyx) */
static void qjs_jobs_later(void *p)
{
	jsthread *t = p;

	if (t->closed)
		return;
	qjs_enter(t);
	qjs_leave(t);
}

/** a call into JS, its exception reported; the result (JS_UNDEFINED on error) */
static JSValue qjs_call(jsthread *t, JSValueConst fn, JSValueConst this_val, int argc,
		JSValueConst *argv, const char *where)
{
	JSValue r;

	uint64_t t0 = onyx_perf_now();	/* (Onyx: onyx_perf.h) */

	qjs_enter(t);
	r = JS_Call(t->ctx, fn, this_val, argc, argv);
	if (JS_IsException(r)) {
		qjs_report(t->ctx, where);
		r = JS_UNDEFINED;
	}
	qjs_leave(t);
	if (onyx_perf_on()) {
		char what[80];
		snprintf(what, sizeof what, "js:call %s", where != NULL ? where : "?");
		onyx_perf_log(what, t0);
	}
	return r;
}

/* Onyx: the window's events pumped while a script runs long (onyx_chrome.cpp: kept, handled
 * after it) -- the system saw the browser frozen ("not pumping") in Google's 5 s script */
extern void onyx_chrome_pump_deferred(void) __attribute__((weak));
#define QJS_PUMP_MS 100
/* Onyx: the time limit (script_timeout) stops a script that is stuck, not one that works:
 * past the limit a script still changing the page -- a DOM change less than a quarter of the
 * limit ago -- runs on, up to QJS_TIMEOUT_MAX times the limit. A loop that never ends
 * changes nothing (or never stops changing: the hard limit). Browsers do not stop a script
 * at all; NetSurf has no "page unresponsive" dialog to ask. browserscore.dev's first render
 * (Vue mounting 1500 features) is one job of ~7 s on the PC, 40-60 s on the Pi. */
#define QJS_TIMEOUT_MAX 4

static int qjs_interrupt(JSRuntime *rt, void *opaque)
{
	jsheap *heap = opaque;
	static uint64_t last_pump;
	uint64_t now, run, limit;

	(void) rt;
	if (qjs_prof_f != NULL)
		qjs_prof_sample();
	now = qjs_now_ms();
	if (onyx_chrome_pump_deferred != NULL && now - heap->start > QJS_PUMP_MS &&
	    now - last_pump > QJS_PUMP_MS) {
		last_pump = now;
		onyx_chrome_pump_deferred();
	}
	if (heap->timeout <= 0)
		return 0;
	if (heap->writes != qjs_dom_writes) {
		heap->writes = qjs_dom_writes;
		heap->busy = now;
	}
	limit = (uint64_t) heap->timeout * 1000;
	run = now - heap->start;
	if (run <= limit)
		return 0;
	if (run <= limit * QJS_TIMEOUT_MAX && now - heap->busy <= limit / 4) {
		if (!heap->told) {
			heap->told = true;
			NSLOG(netsurf, INFO, "JS: a script past %d s still changing the page: "
					"let run", heap->timeout);
			if (qjs_debug)
				fprintf(stderr, "JS: a script past %d s still changing the page: "
						"let run\n", heap->timeout);
		}
		return 0;
	}
	return 1;
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
		dom_string *name = NULL, *ns = NULL;
		JSValue p = JS_UNDEFINED;
		/* Onyx: the element's namespace picks its class: "svg:<name>" / "svg:*",
		 * "math:*" in dom.js's TAGS; an HTML name of no class and without a '-' (not
		 * a custom element's) is an HTMLUnknownElement ("*unknown") */
		const char *pfx = "";
		bool unknown_ok = true;

		dom_node_get_namespace(n, &ns);
		if (ns != NULL) {
			if (dom_string_isequal(ns, dom_namespaces[DOM_NAMESPACE_SVG]))
				pfx = "svg:";
			else if (dom_string_isequal(ns, dom_namespaces[DOM_NAMESPACE_MATHML]))
				pfx = "math:";
			dom_string_unref(ns);
		} else {
			/* Onyx (docs/06 §43): an XML document's element in no namespace is an
			 * Element, none of HTML's classes (its <title> is not HTML's) */
			dom_document *od = NULL;
			int kind = 0;
			if (dom_node_get_owner_document(n, &od) == DOM_NO_ERR && od != NULL) {
				kind = dom_html_document_get_xml_kind((dom_html_document *) od);
				dom_node_unref(od);
			}
			if (kind != 0) {
				JSValue x = JS_GetPropertyStr(t->ctx, t->tag_protos, "*xml");
				if (JS_IsObject(x))
					return x;
				JS_FreeValue(t->ctx, x);
				return JS_DupValue(t->ctx, t->protos[QP_ELEMENT]);
			}
		}
		if (dom_node_get_node_name(n, &name) == DOM_NO_ERR && name != NULL) {
			char tag[48];
			size_t len = dom_string_byte_length(name), i, o = strlen(pfx);

			if (len + o < sizeof(tag)) {
				memcpy(tag, pfx, o);
				for (i = 0; i < len; i++) {
					char ch = dom_string_data(name)[i];
					tag[o + i] = (ch >= 'A' && ch <= 'Z') ? ch + 32 : ch;
					if (ch == '-')
						unknown_ok = false;
				}
				tag[o + len] = '\0';
				p = JS_GetPropertyStr(t->ctx, t->tag_protos, tag);
			}
			dom_string_unref(name);
		}
		if (!JS_IsObject(p) && pfx[0] != '\0') {
			char any[8];
			JS_FreeValue(t->ctx, p);
			snprintf(any, sizeof(any), "%s*", pfx);
			p = JS_GetPropertyStr(t->ctx, t->tag_protos, any);
		} else if (!JS_IsObject(p)) {
			/* ("*custom": a name with a '-', HTMLElement until it is upgraded) */
			JS_FreeValue(t->ctx, p);
			p = JS_GetPropertyStr(t->ctx, t->tag_protos,
					unknown_ok ? "*unknown" : "*custom");
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
		/* Onyx: a shadow root (libdom keeps it on its host) is a ShadowRoot */
		if (JS_IsObject(t->shadow_proto) && dom_onyx_shadow_host(n) != NULL)
			return JS_DupValue(t->ctx, t->shadow_proto);
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

	if (n == NULL || t->zombie)	/* (Onyx: a zombie has no wrappers any more) */
		return JS_NULL;
	if (t->capwraps != 0) {
		for (h = qjs_hash(n, t->capwraps); t->wraps[h].node != NULL;
		     h = (h + 1) & (t->capwraps - 1)) {
			if (t->wraps[h].node == n && !JS_IsUndefined(t->wraps[h].obj))
				return JS_DupValue(t->ctx, t->wraps[h].obj);
		}
	}
	/* Onyx: a node of another window's document (a same-origin frame's, its realm in
	 * this heap) is that window's object -- one object a node, whichever realm reads it
	 * (Acid3: a TreeWalker of this window over a frame's nodes gave other objects than
	 * the frame's own: never equal) */
	if (t->heap != NULL && t->heap->shared) {
		dom_document *od = NULL;
		dom_node_type nt = DOM_ELEMENT_NODE;

		if (dom_node_get_node_type(n, &nt) == DOM_NO_ERR &&
		    nt == DOM_DOCUMENT_NODE)
			od = (dom_document *) dom_node_ref(n);
		else if (dom_node_get_owner_document(n, &od) != DOM_NO_ERR)
			od = NULL;
		if (od != NULL) {
			dom_node_unref(od);
			if (od != t->doc) {
				jsthread *o;

				for (o = t->heap->live; o != NULL; o = o->hnext)
					if (o != t && o->doc == od && !o->closed &&
					    !o->zombie)
						return qjs_wrap(o, n);
			}
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

/* Onyx: for qjs_canvas.c (declared in qjs_canvas.h) */
struct dom_node *qjs_node_of(JSValueConst v)
{
	return qjs_node(v);
}

struct html_content *qjs_html_of(JSContext *ctx)
{
	jsthread *t = QJS_T(ctx);

	return (t == NULL || t->closed) ? NULL : t->htmlc;
}

static JSValue qjs_call(jsthread *t, JSValueConst fn, JSValueConst this_val, int argc,
		JSValueConst *argv, const char *where);

void qjs_invoke(JSContext *ctx, JSValueConst fn, int argc, JSValueConst *argv,
		const char *where)
{
	jsthread *t = QJS_T(ctx);

	if (t == NULL || t->closed)
		return;
	JS_FreeValue(ctx, qjs_call(t, fn, JS_UNDEFINED, argc, argv, where));
}


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
	dom_string *s = NULL, *ns = NULL;
	dom_node_type type = DOM_ELEMENT_NODE;
	QJS_NODE_ARG(n, 0);
	/* Onyx: an element outside the HTML namespace keeps its name's case (libdom's
	 * nodeName upper-cases every element of an HTML document: an SVG's linearGradient) */
	dom_node_get_node_type(n, &type);
	if (type == DOM_ELEMENT_NODE && dom_node_get_namespace(n, &ns) == DOM_NO_ERR &&
	    ns != NULL) {
		bool html = dom_string_isequal(ns, dom_namespaces[DOM_NAMESPACE_HTML]);
		dom_string_unref(ns);
		if (!html && dom_node_get_local_name(n, &s) == DOM_NO_ERR && s != NULL)
			return qjs_str(ctx, s);
	}
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

/* Onyx: treeGen() / attrGen(): libdom's change counters (a node inserted or removed in any
 * tree; an attribute set, changed or removed) -- dom.js keeps the child lists, the tag lists,
 * the style sheet list while they are unchanged (a loop reading el.childNodes[i] built the
 * whole list at each read: n^2) */
static JSValue n_tree_gen(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	(void) this_val; (void) argc; (void) argv;
	return JS_NewUint32(ctx, dom_onyx_tree_generation());
}

static JSValue n_attr_gen(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	(void) this_val; (void) argc; (void) argv;
	return JS_NewUint32(ctx, dom_onyx_attr_generation());
}

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
		QJS_DIRTY(QJS_T(ctx));
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
		QJS_DIRTY(QJS_T(ctx));
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

/** hasToken(node, name, token): the attribute, split at ASCII whitespace, has the token
 * (a class, a ~= selector, classList.contains: dom.js split the value into an array each
 * time -- selector matching over a page's nodes) */
static JSValue n_has_token(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_string *name, *v = NULL;
	const char *tok, *s, *end;
	size_t tlen;
	bool found = false;
	QJS_NODE_ARG(n, 0);
	name = qjs_dstr(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
	if (name == NULL)
		return JS_FALSE;
	dom_element_get_attribute((dom_element *) n, name, &v);
	dom_string_unref(name);
	if (v == NULL)
		return JS_FALSE;
	tok = JS_ToCStringLen(ctx, &tlen, argc > 2 ? argv[2] : JS_UNDEFINED);
	if (tok == NULL) {
		dom_string_unref(v);
		return JS_EXCEPTION;
	}
	s = dom_string_data(v);
	end = s + dom_string_byte_length(v);
	while (s < end && !found) {
		const char *w;

		while (s < end && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f'))
			s++;
		for (w = s; s < end && !(*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' ||
				*s == '\f'); s++)
			;
		found = s > w && (size_t) (s - w) == tlen && memcmp(w, tok, tlen) == 0;
	}
	JS_FreeCString(ctx, tok);
	dom_string_unref(v);
	return JS_NewBool(ctx, found);
}

static JSValue n_set_attr(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_string *name, *v;
	QJS_NODE_ARG(n, 0);
	name = qjs_dstr(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
	v = qjs_dstr(ctx, argc > 2 ? argv[2] : JS_UNDEFINED);
	if (name != NULL && v != NULL) {
		dom_exception e = dom_element_set_attribute((dom_element *) n, name, v);
		if (e != DOM_NO_ERR) {
			JSValue r = JS_ThrowTypeError(ctx, "bad attribute %.40s (%d)",
					dom_string_data(name), (int) e);
			dom_string_unref(name);
			dom_string_unref(v);
			return r;
		}
		QJS_DIRTY(QJS_T(ctx));
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
			QJS_DIRTY(QJS_T(ctx));
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
	if (argc > 1 && JS_IsString(argv[1])) {
		/* Onyx: create(qname, namespace): an element in its namespace (SVG, MathML) */
		dom_string *ns = qjs_dstr(ctx, argv[1]);
		dom_exception err = dom_document_create_element_ns(t->doc, ns, tag, &e);
		if (ns != NULL)
			dom_string_unref(ns);
		dom_string_unref(tag);
		if (err != DOM_NO_ERR || e == NULL)
			return JS_ThrowTypeError(ctx, "bad element name");
		v = qjs_wrap(t, (dom_node *) e);
		dom_node_unref(e);
		return v;
	}
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
/* Onyx: the document a node belongs to -- a document's is itself (a new reference) */
static dom_document *qjs_doc_of(dom_node *n)
{
	dom_node_type t;
	dom_document *d = NULL;

	if (dom_node_get_node_type(n, &t) == DOM_NO_ERR && t == DOM_DOCUMENT_NODE)
		return (dom_document *) dom_node_ref(n);
	if (dom_node_get_owner_document(n, &d) != DOM_NO_ERR)
		return NULL;
	return d;
}

/* Onyx: the live thread (realm) of a tab's shared heap whose document is d (NULL: none) */
static jsthread *qjs_thread_of_doc(jsthread *t, dom_document *d)
{
	jsthread *o;

	if (t->doc == d)
		return t;
	if (t->heap == NULL)
		return NULL;
	for (o = t->heap->live; o != NULL; o = o->hnext)
		if (o->doc == d && !o->closed && !o->zombie && !o->worker)
			return o;
	return NULL;
}

/* Onyx: the wrappers of a subtree adopted by another frame's document (in place: the same
 * nodes) moved to that document's realm -- its wrapper table and its prototypes -- so a node
 * is one object for the frames of the tab (el.ownerDocument, el.parentNode answered by the
 * new document's realm: iframetest.sh's frames-api.html) */
static void qjs_wraps_take(jsthread *from, jsthread *to, dom_node *root)
{
	dom_node *n = dom_node_ref(root), *next;
	bool moved = false;

	if (from == NULL || to == NULL || from == to || from->capwraps == 0)
		goto out;
	while (n != NULL) {
		size_t h;

		for (h = qjs_hash(n, from->capwraps); from->wraps[h].node != NULL;
		     h = (h + 1) & (from->capwraps - 1)) {
			if (from->wraps[h].node != n || JS_IsUndefined(from->wraps[h].obj))
				continue;
			if ((to->nwraps + 1) * 2 <= to->capwraps || qjs_wraps_grow(to)) {
				JSValue obj = from->wraps[h].obj, proto = qjs_proto_for(to, n);
				size_t k;

				JS_SetPrototype(to->ctx, obj, proto);
				JS_FreeValue(to->ctx, proto);
				for (k = qjs_hash(n, to->capwraps); to->wraps[k].node != NULL;
				     k = (k + 1) & (to->capwraps - 1))
					;
				to->wraps[k].node = n;	/* (the table's reference moves) */
				to->wraps[k].obj = obj;
				to->nwraps++;
				/* (the node kept as a tombstone until the table is made
				 * again below: the other nodes' probes go past it) */
				from->wraps[h].obj = JS_UNDEFINED;
				from->nwraps--;
				moved = true;
			}
			break;
		}
		/* the next node of the subtree, in document order */
		next = NULL;
		if (dom_node_get_first_child(n, &next) == DOM_NO_ERR && next != NULL) {
			dom_node_unref(n);
			n = next;
			continue;
		}
		while (n != NULL) {
			if (n == root) {
				dom_node_unref(n);
				n = NULL;
				break;
			}
			next = NULL;
			if (dom_node_get_next_sibling(n, &next) == DOM_NO_ERR && next != NULL) {
				dom_node_unref(n);
				n = next;
				break;
			}
			next = NULL;
			dom_node_get_parent_node(n, &next);
			dom_node_unref(n);
			n = next;
		}
	}
	if (moved) {
		/* (the tombstones dropped: the table made again) */
		size_t cap = from->capwraps;
		{
			struct qjs_wrap *old = from->wraps;
			struct qjs_wrap *w = calloc(cap, sizeof(*w));
			size_t i;
			if (w != NULL) {
				for (i = 0; i < cap; i++) {
					if (old[i].node != NULL && !JS_IsUndefined(old[i].obj)) {
						size_t h2 = qjs_hash(old[i].node, cap);
						while (w[h2].node != NULL)
							h2 = (h2 + 1) & (cap - 1);
						w[h2] = old[i];
					}
				}
				free(old);
				from->wraps = w;
			}
		}
	}
	return;
out:
	dom_node_unref(n);
}

/* Onyx: a node inserted into another document's tree is adopted by that document first,
 * in place (the DOM's insertion steps: libdom refused it, WRONG_DOCUMENT_ERR) */
static void qjs_adopt_for(jsthread *t, dom_node *parent, dom_node *child)
{
	dom_document *pd = qjs_doc_of(parent), *cd = qjs_doc_of(child);
	dom_node_type type;

	if (pd != NULL && cd != NULL && pd != cd &&
	    dom_node_get_node_type(child, &type) == DOM_NO_ERR && type != DOM_DOCUMENT_NODE &&
	    dom_document_onyx_adopt(pd, child) == DOM_NO_ERR && t != NULL) {
		jsthread *from = qjs_thread_of_doc(t, cd), *to = qjs_thread_of_doc(t, pd);
		/* (a document without a realm -- a DOMParser's -- has its wrappers in the
		 * realm that made them: the caller's) */
		qjs_wraps_take(from != NULL ? from : t, to, child);
	}
	if (pd != NULL) dom_node_unref(pd);
	if (cd != NULL) dom_node_unref(cd);
}

static JSValue n_insert(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_node *ref = argc > 2 ? qjs_node(argv[2]) : NULL;
	dom_node *res = NULL;
	dom_exception e;
	QJS_NODE_ARG(p, 0);
	QJS_NODE_ARG(c, 1);

	qjs_adopt_for(QJS_T(ctx), p, c);	/* (Onyx: a node of another document adopted first) */
	if (ref != NULL)
		e = dom_node_insert_before(p, c, ref, &res);
	else
		e = dom_node_append_child(p, c, &res);
	if (e != DOM_NO_ERR)
		return JS_ThrowTypeError(ctx, "cannot insert that node here (%d)", e);
	if (res != NULL)
		dom_node_unref(res);
	QJS_DIRTY(QJS_T(ctx));
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
	QJS_DIRTY(QJS_T(ctx));
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

/* ---- ES modules (Onyx) -------------------------------------------------------------------
 *
 * <script type="module"> goes to dom.js (the event "onyx:module", html/script.c): it fetches
 * the module and every module its imports name, in parallel, and gives their sources to
 * moduleSource (t->modsrc) -- a loader cannot wait for the network. moduleRun then compiles
 * the root module, the loader below compiles its imports from the sources, and the module
 * runs; a source not there yet (an import dom.js did not see) is named in the array
 * moduleRun returns, and dom.js fetches it and tries again. */

/** an import's specifier resolved against the importing module's URL (js_malloc'd) */
static char *qjs_mod_normalize(JSContext *ctx, const char *base, const char *name,
		void *opaque)
{
	nsurl *b = NULL, *u = NULL;
	char *r = NULL;

	(void) opaque;
	if (nsurl_create(base, &b) == NSERROR_OK && nsurl_join(b, name, &u) == NSERROR_OK) {
		const char *s = nsurl_access(u);
		size_t n = strlen(s);
		r = js_malloc(ctx, n + 1);
		if (r != NULL)
			memcpy(r, s, n + 1);
	} else {
		size_t n = strlen(name);
		r = js_malloc(ctx, n + 1);	/* (a bare specifier: it will not load) */
		if (r != NULL)
			memcpy(r, name, n + 1);
	}
	if (u != NULL) nsurl_unref(u);
	if (b != NULL) nsurl_unref(b);
	return r;
}

/** import.meta.url of a compiled module */
static void qjs_mod_meta(JSContext *ctx, JSValueConst func, const char *url)
{
	JSModuleDef *m = JS_VALUE_GET_PTR(func);
	JSValue meta = JS_GetImportMeta(ctx, m);

	if (!JS_IsException(meta)) {
		JS_DefinePropertyValueStr(ctx, meta, "url", JS_NewString(ctx, url),
				JS_PROP_C_W_E);
		JS_FreeValue(ctx, meta);
	}
}

/** a module compiled from its source in t->modsrc, else named in t->modmissing */
/* Onyx: a script's dynamic imports -- import(x) becomes __onyxImport(<base>, x) (dom.js): it
 * fetches the module graph, then imports it (QuickJS's loader cannot wait for the network:
 * developer.mozilla.org's chunks, import()ed by URL, were "not loaded yet"). <base>: the
 * module's import.meta.url, null in a classic script (the document's URL). Strings and
 * comments are skipped. NULL: nothing to change. */
static char *qjs_rewrite_import(const char *src, size_t len, bool module, size_t *outlen)
{
	const char *ins = module ? "import.meta.url, " : "null, ";
	size_t insl = strlen(ins), cap = 0, o = 0, i, from = 0;
	char *out = NULL;
	char q = 0;

	if (len < 8)
		return NULL;
	for (i = 0; i + 6 < len; i++) {
		char c = src[i];
		if (q != 0) {
			if (c == '\\') { i++; continue; }
			if (c == q) q = 0;
			continue;
		}
		if (c == '"' || c == '\'' || c == '`') { q = c; continue; }
		if (c == '/' && src[i + 1] == '/') {
			while (i < len && src[i] != '\n') i++;
			continue;
		}
		if (c == '/' && src[i + 1] == '*') {
			i += 2;
			while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/')) i++;
			i++;
			continue;
		}
		if (c == 'i' && memcmp(src + i, "import", 6) == 0 &&
		    (i == 0 || !(isalnum((unsigned char) src[i - 1]) || src[i - 1] == '_' ||
				 src[i - 1] == '$' || src[i - 1] == '.'))) {
			size_t j = i + 6;
			while (j < len && (src[j] == ' ' || src[j] == '\t' || src[j] == '\n' || src[j] == '\r'))
				j++;
			if (j < len && src[j] == '(') {
				/* not a method named import: import(a) { ... } */
				size_t k = j + 1;
				int depth = 1;
				char kq = 0;
				for (; k < len && depth > 0; k++) {
					char d = src[k];
					if (kq != 0) {
						if (d == '\\') k++;
						else if (d == kq) kq = 0;
						continue;
					}
					if (d == '"' || d == '\'' || d == '`') kq = d;
					else if (d == '(') depth++;
					else if (d == ')') depth--;
				}
				while (k < len && (src[k] == ' ' || src[k] == '\t' || src[k] == '\n' || src[k] == '\r'))
					k++;
				if (k < len && src[k] == '{') {
					i = j;
					continue;
				}
			}
			if (j < len && src[j] == '(') {
				size_t need = o + (i - from) + 12 + (j + 1 - (i + 6)) + insl + (len - j) + 1;
				if (need > cap) {
					char *n;
					cap = need + len / 4 + 64;
					n = realloc(out, cap);
					if (n == NULL) { free(out); return NULL; }
					out = n;
				}
				memcpy(out + o, src + from, i - from); o += i - from;
				memcpy(out + o, "__onyxImport", 12); o += 12;
				memcpy(out + o, src + i + 6, j + 1 - (i + 6)); o += j + 1 - (i + 6);
				memcpy(out + o, ins, insl); o += insl;
				from = j + 1;
				i = j;
			}
		}
	}
	if (out == NULL)
		return NULL;
	if (o + (len - from) + 1 > cap) {
		char *n = realloc(out, o + (len - from) + 1);
		if (n == NULL) { free(out); return NULL; }
		out = n;
	}
	memcpy(out + o, src + from, len - from); o += len - from;
	out[o] = '\0';
	*outlen = o;
	return out;
}

static JSModuleDef *qjs_mod_loader(JSContext *ctx, const char *name, void *opaque)
{
	jsthread *t = QJS_T(ctx);
	JSValue src, func;
	const char *text;
	size_t len;
	JSModuleDef *m;

	(void) opaque;
	src = JS_GetPropertyStr(ctx, t->modsrc, name);
	if (!JS_IsString(src)) {
		JSValue len_v = JS_GetPropertyStr(ctx, t->modmissing, "length");
		uint32_t n = 0;
		JS_ToUint32(ctx, &n, len_v);
		JS_FreeValue(ctx, len_v);
		JS_SetPropertyUint32(ctx, t->modmissing, n, JS_NewString(ctx, name));
		JS_FreeValue(ctx, src);
		JS_ThrowReferenceError(ctx, "module not loaded yet: %s", name);
		return NULL;
	}
	text = JS_ToCStringLen(ctx, &len, src);
	JS_FreeValue(ctx, src);
	if (text == NULL)
		return NULL;
	{
		size_t rl;
		char *rw = qjs_rewrite_import(text, len, true, &rl);	/* (Onyx) */
		func = JS_Eval(ctx, rw ? rw : text, rw ? rl : len, name,
				JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
		free(rw);
	}
	JS_FreeCString(ctx, text);
	if (JS_IsException(func))
		return NULL;
	qjs_mod_meta(ctx, func, name);
	m = JS_VALUE_GET_PTR(func);
	JS_FreeValue(ctx, func);
	return m;
}

/** moduleSource(url, text): a module's source, for the loader */
static JSValue n_module_source(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	const char *url;

	(void) this_val;
	if (argc < 2 || (url = JS_ToCString(ctx, argv[0])) == NULL)
		return JS_UNDEFINED;
	JS_SetPropertyStr(ctx, t->modsrc, url, JS_DupValue(ctx, argv[1]));
	JS_FreeCString(ctx, url);
	return JS_UNDEFINED;
}

/* moduleImports' scanner: the regular expression dom.js had, by hand (its backtracking over
 * a big module was 13 % of github.com's scripts) */
static bool mi_space(char ch)
{
	return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f';
}

static bool mi_word(char ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
		(ch >= '0' && ch <= '9') || ch == '_';
}

/* a quoted specifier at s[i] ('...' or "...", no newline): its end past the quote, or 0 */
static size_t mi_quoted(const char *s, size_t len, size_t i, size_t *b, size_t *e)
{
	size_t j;

	if (i >= len || (s[i] != '\'' && s[i] != '"'))
		return 0;
	for (j = i + 1; j < len && s[j] != '\'' && s[j] != '"' && s[j] != '\n'; j++)
		;
	if (j == i + 1 || j >= len || s[j] != s[i])
		return 0;
	*b = i + 1;
	*e = j;
	return j + 1;
}

/* the static form after "import" / "export" at s[k]: [names from] 'spec' */
static size_t mi_static(const char *s, size_t len, size_t k, size_t *b, size_t *e)
{
	size_t i, j, end;

	/* (names) from 'spec' -- the first "from" that one follows */
	for (i = k; i < len && (mi_word(s[i]) || mi_space(s[i]) || s[i] == '*' ||
			s[i] == '{' || s[i] == '}' || s[i] == ',' || s[i] == '$'); i++) {
		if (i == k || len - i < 4 || memcmp(s + i, "from", 4) != 0)
			continue;
		for (j = i + 4; j < len && mi_space(s[j]); j++)
			;
		if ((end = mi_quoted(s, len, j, b, e)) != 0)
			return end;
	}
	for (i = k; i < len && mi_space(s[i]); i++)
		;
	return mi_quoted(s, len, i, b, e);
}

/**
 * moduleImports(text): the specifiers a module's import / export ... from and import()
 * name, as dom.js's MOD_IMPORT found them
 */
static JSValue n_module_imports(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	const char *s;
	size_t len, pos = 0, k, b, e, end;
	uint32_t n = 0;
	JSValue arr;

	(void) this_val;
	if (argc < 1 || (s = JS_ToCStringLen(ctx, &len, argv[0])) == NULL)
		return JS_EXCEPTION;
	arr = JS_NewArray(ctx);
	for (k = pos; k + 6 <= len; k++) {
		bool imp;
		size_t i;

		if (s[k] == 'i')
			imp = memcmp(s + k, "import", 6) == 0;
		else if (s[k] == 'e')
			imp = false;
		else
			continue;
		if (!imp && memcmp(s + k, "export", 6) != 0)
			continue;
		end = 0;
		/* (^|[;\n\r}])\s* before it */
		for (i = k; i > pos && mi_space(s[i - 1]) && s[i - 1] != '\n' && s[i - 1] != '\r'; i--)
			;
		if (i == 0 || (i > pos && (s[i - 1] == ';' || s[i - 1] == '}' ||
				s[i - 1] == '\n' || s[i - 1] == '\r')))
			end = mi_static(s, len, k + 6, &b, &e);
		/* \bimport\s*\(\s*'spec'\s*\) */
		if (end == 0 && imp && (k == 0 || !mi_word(s[k - 1]))) {
			for (i = k + 6; i < len && mi_space(s[i]); i++)
				;
			if (i < len && s[i] == '(') {
				for (i++; i < len && mi_space(s[i]); i++)
					;
				if ((i = mi_quoted(s, len, i, &b, &e)) != 0) {
					for (; i < len && mi_space(s[i]); i++)
						;
					if (i < len && s[i] == ')')
						end = i + 1;
				}
			}
		}
		if (end == 0)
			continue;
		JS_SetPropertyUint32(ctx, arr, n++, JS_NewStringLen(ctx, s + b, e - b));
		pos = end;
		k = end - 1;
	}
	JS_FreeCString(ctx, s);
	return arr;
}

/**
 * moduleRun(url, text): the module compiled, its imports loaded and it run -> true; an array
 * of the modules' URLs whose source is missing (fetch them, then again); false: an error
 * (reported).
 */
static JSValue n_module_run(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	const char *url, *text;
	size_t len;
	JSValue func, r;
	uint32_t nmiss = 0;

	(void) this_val;
	if (argc < 2)
		return JS_FALSE;
	url = JS_ToCString(ctx, argv[0]);
	text = JS_ToCStringLen(ctx, &len, argv[1]);
	if (url == NULL || text == NULL) {
		if (url) JS_FreeCString(ctx, url);
		if (text) JS_FreeCString(ctx, text);
		return JS_EXCEPTION;
	}
	JS_FreeValue(ctx, t->modmissing);
	t->modmissing = JS_NewArray(ctx);
	{
		size_t rl;
		char *rw = qjs_rewrite_import(text, len, true, &rl);	/* (Onyx) */
		func = JS_Eval(ctx, rw ? rw : text, rw ? rl : len, url,
				JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
		free(rw);
	}
	JS_FreeCString(ctx, text);
	if (!JS_IsException(func)) {
		qjs_mod_meta(ctx, func, url);
		if (JS_ResolveModule(ctx, func) < 0) {
			JS_FreeValue(ctx, func);
			func = JS_EXCEPTION;
		}
	}
	if (JS_IsException(func)) {
		JSValue len_v = JS_GetPropertyStr(ctx, t->modmissing, "length");
		JS_ToUint32(ctx, &nmiss, len_v);
		JS_FreeValue(ctx, len_v);
		if (nmiss > 0) {
			JS_FreeValue(ctx, JS_GetException(ctx));
			JS_FreeCString(ctx, url);
			return JS_DupValue(ctx, t->modmissing);
		}
		qjs_report(ctx, url);
		JS_FreeCString(ctx, url);
		return JS_FALSE;
	}
	r = JS_EvalFunction(ctx, func);		/* (func consumed; a promise: top-level await) */
	if (JS_IsException(r)) {
		qjs_report(ctx, url);
		JS_FreeCString(ctx, url);
		return JS_FALSE;
	}
	JS_FreeValue(ctx, r);
	JS_FreeCString(ctx, url);
	return JS_TRUE;
}

/* exported interface documented in js.h (Onyx) */
void js_module_script(jsthread *thread, struct dom_node *node)
{
	js_dispatch_event(thread, "onyx:module", node, NULL);
}

/**
 * cssKept(text, inline): what libcss keeps of the text -- [rules, declaration words] -- parsed
 * as an inline style (inline true: "prop: value") or a style sheet; null if unparsable.
 * CSS.supports and element.style (dom.js) answer from it.
 */
static JSValue n_css_kept(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	size_t len;
	const char *text;
	uint32_t rules = 0, words = 0;
	bool ok;
	JSValue a;

	(void) this_val;
	if (argc < 1)
		return JS_NULL;
	text = JS_ToCStringLen(ctx, &len, argv[0]);
	if (text == NULL)
		return JS_EXCEPTION;
	ok = nscss_text_kept(text, len, argc > 1 && JS_ToBool(ctx, argv[1]), &rules, &words);
	JS_FreeCString(ctx, text);
	if (!ok)
		return JS_NULL;
	a = JS_NewArray(ctx);
	JS_SetPropertyUint32(ctx, a, 0, JS_NewUint32(ctx, rules));
	JS_SetPropertyUint32(ctx, a, 1, JS_NewUint32(ctx, words));
	return a;
}

/**
 * Onyx: sheetText(link): the text of the style sheet a <link rel=stylesheet> loaded and its
 * URL, [text, url]; null if it has none (not loaded, not a style sheet). The CSSOM's
 * document.styleSheets (dom.js) reads the linked sheets' rules from it.
 */
static JSValue n_sheet_text(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	unsigned int i;
	QJS_NODE_ARG(n, 0);

	(void) this_val;
	if (t->htmlc == NULL || t->htmlc->stylesheets == NULL)
		return JS_NULL;
	for (i = 0; i < t->htmlc->stylesheet_count; i++) {
		struct html_stylesheet *s = &t->htmlc->stylesheets[i];
		const uint8_t *data;
		size_t size = 0;
		JSValue a;
		if (s->node != n || s->sheet == NULL)
			continue;
		data = content_get_source_data(s->sheet, &size);
		a = JS_NewArray(ctx);
		JS_SetPropertyUint32(ctx, a, 0, data != NULL ?
				JS_NewStringLen(ctx, (const char *) data, size) :
				JS_NewString(ctx, ""));
		JS_SetPropertyUint32(ctx, a, 1, JS_NewString(ctx,
				nsurl_access(hlcache_handle_get_url(s->sheet))));
		return a;
	}
	return JS_NULL;
}

/* Onyx: the <script> element running (document.currentScript), set around js_exec */
static struct dom_node *qjs_current_script;

/* exported interface documented in js.h */
void js_set_current_script(jsthread *thread, struct dom_node *node)
{
	(void) thread;
	qjs_current_script = node;
}

/** currentScript(): the <script> element whose code runs, or null */
static JSValue n_current_script(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	(void) this_val; (void) argc; (void) argv;
	return qjs_current_script != NULL ? qjs_wrap(QJS_T(ctx), qjs_current_script) : JS_NULL;
}

static JSValue n_by_id(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	dom_string *id = qjs_dstr(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
	dom_element *e = NULL;
	dom_document *doc = t->doc;
	dom_node_type nt;
	JSValue v;

	if (id == NULL)
		return JS_NULL;
	/* (Onyx: byId(id, doc) -- another document's: DOMParser's, createHTMLDocument's, an
	 * XML document's -- docs/06 §43) */
	if (argc > 1) {
		dom_node *d = qjs_node_of(argv[1]);
		if (d != NULL && dom_node_get_node_type(d, &nt) == DOM_NO_ERR &&
		    nt == DOM_DOCUMENT_NODE)
			doc = (dom_document *) d;
	}
	dom_document_get_element_by_id(doc, id, &e);
	dom_string_unref(id);
	v = qjs_wrap(t, (dom_node *) e);
	if (e != NULL)
		dom_node_unref(e);
	return v;
}

/**
 * setHTML(n, html [, context]): n's children replaced by html parsed as a fragment (innerHTML).
 * Onyx: the HTML standard's fragment parsing algorithm -- in the context of n (or of the
 * given element: outerHTML, insertAdjacentHTML), so "<td>" in a <tr>, "<col>" in a <table>,
 * script / style / textarea / title text all parse as in a browser; a template's contents
 * receive its innerHTML. Scripts in the fragment do not run.
 */
static JSValue n_set_html(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	dom_hubbub_parser_params params;
	dom_hubbub_parser *parser = NULL;
	dom_document_fragment *fragment = NULL;
	dom_node *child = NULL, *res = NULL, *target, *context = NULL;
	dom_node_type type = DOM_ELEMENT_NODE;
	size_t len;
	const char *s;
	QJS_NODE_ARG(n, 0);

	s = JS_ToCStringLen(ctx, &len, argc > 1 ? argv[1] : JS_UNDEFINED);
	if (s == NULL)
		return JS_EXCEPTION;
	if (argc > 2 && qjs_node(argv[2]) != NULL)
		context = qjs_node(argv[2]);
	dom_node_get_node_type(n, &type);
	if (context == NULL && type == DOM_ELEMENT_NODE)
		context = n;
	target = dom_node_ref(n);
	if (type == DOM_ELEMENT_NODE && context == n) {
		/* a template: its contents take the nodes */
		dom_document_fragment *c = NULL;
		if (dom_hubbub_template_content((dom_element *) n, &c) == DOM_NO_ERR &&
				c != NULL) {
			dom_node_unref(target);
			target = (dom_node *) c;
		}
	}

	/* the old children out */
	while (dom_node_get_first_child(target, &child) == DOM_NO_ERR && child != NULL) {
		dom_node_remove_child(target, child, &res);
		if (res != NULL)
			dom_node_unref(res);
		dom_node_unref(child);
		child = NULL;
	}

	memset(&params, 0, sizeof(params));
	params.enc = "UTF-8";
	params.fix_enc = true;
	params.enable_script = true;	/* (<noscript> as raw text, as a browser) */
	if (dom_hubbub_fragment_parser_create_ctx(&params, t->doc, (dom_element *) context,
			&parser, &fragment) != DOM_HUBBUB_OK) {
		JS_FreeCString(ctx, s);
		dom_node_unref(target);
		return JS_UNDEFINED;
	}
	dom_hubbub_parser_parse_chunk(parser, (const uint8_t *) s, len);
	dom_hubbub_parser_completed(parser);
	dom_hubbub_parser_destroy(parser);
	JS_FreeCString(ctx, s);

	/* the fragment's nodes into the target */
	while (dom_node_get_first_child(fragment, &child) == DOM_NO_ERR && child != NULL) {
		/* (Onyx: res reset -- an append refused, e.g. a doctype into a shadow root,
		 * left the removal's result, unreferenced twice: a freed node) */
		res = NULL;
		dom_node_remove_child(fragment, child, &res);
		if (res != NULL)
			dom_node_unref(res);
		res = NULL;
		dom_node_append_child(target, child, &res);
		if (res != NULL)
			dom_node_unref(res);
		dom_node_unref(child);
		child = NULL;
	}
	dom_node_unref(fragment);
	dom_node_unref(target);
	QJS_DIRTY(t);
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

/** Onyx: nextElement(n, root): the element after n in document order under root, or null --
 * querySelector walks with it and stops at the first match (descendants() wrapped every
 * element of the tree first) */
static JSValue n_next_element(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	dom_node *d;
	JSValue v;
	QJS_NODE_ARG(n, 0);
	QJS_NODE_ARG(root, 1);

	dom_node_ref(n);
	for (d = qjs_following(n, root); d != NULL; d = qjs_following(d, root)) {
		dom_node_type type = 0;

		if (dom_node_get_node_type(d, &type) == DOM_NO_ERR &&
		    type == DOM_ELEMENT_NODE) {
			v = qjs_wrap(t, d);
			dom_node_unref(d);
			return v;
		}
	}
	return JS_NULL;
}

/* Onyx: whether the class list (an attribute's text) holds every one of the names (a list
 * separated by ASCII white space) -- getElementsByClassName */
static bool qjs_has_classes(const char *list, size_t llen, const char *names, size_t nlen)
{
	size_t i = 0;

#define QJS_WS(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\f' || (c) == '\r')
	while (i < nlen) {
		size_t s, w, j = 0;
		bool found = false;

		while (i < nlen && QJS_WS(names[i]))
			i++;
		if (i == nlen)
			break;
		s = i;
		while (i < nlen && !QJS_WS(names[i]))
			i++;
		w = i - s;
		while (j < llen && !found) {
			size_t ts;
			while (j < llen && QJS_WS(list[j]))
				j++;
			ts = j;
			while (j < llen && !QJS_WS(list[j]))
				j++;
			found = j - ts == w && w > 0 && memcmp(list + ts, names + s, w) == 0;
		}
		if (!found)
			return false;
	}
#undef QJS_WS
	return true;
}

/** descendants(n[, classes]): its descendant elements, in document order -- Onyx: with
 * classes, those whose class attribute holds them all (getElementsByClassName: the filter in
 * JS wrapped every element of the document and split its classes -- carbon ads asks it of
 * browserscore.dev's 10000 elements) */
static JSValue n_descendants(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	JSValue arr = JS_NewArray(ctx);
	dom_node *d;
	uint32_t i = 0;
	const char *names = NULL;
	size_t nlen = 0;
	QJS_NODE_ARG(root, 0);

	if (argc > 1 && JS_IsString(argv[1]))
		names = JS_ToCStringLen(ctx, &nlen, argv[1]);
	dom_node_ref(root);
	for (d = qjs_following(root, root); d != NULL; d = qjs_following(d, root)) {
		dom_node_type type = 0;

		if (dom_node_get_node_type(d, &type) != DOM_NO_ERR ||
		    type != DOM_ELEMENT_NODE)
			continue;
		if (names != NULL) {
			dom_string *cls = NULL;
			bool ok;

			if (dom_element_get_attribute((dom_element *) d, corestring_dom_class,
					&cls) != DOM_NO_ERR || cls == NULL)
				continue;
			ok = qjs_has_classes(dom_string_data(cls), dom_string_byte_length(cls),
					names, nlen);
			dom_string_unref(cls);
			if (!ok)
				continue;
		}
		JS_SetPropertyUint32(ctx, arr, i++, qjs_wrap(t, d));
	}
	if (names != NULL)
		JS_FreeCString(ctx, names);
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

/* Onyx (docs/06 §38): the page zoom -- the window's scale (CSS px are scale device px) */
static float qjs_scale(jsthread *t)
{
	return t->bw != NULL && t->bw->scale > 0 ? t->bw->scale : 1.0f;
}

static void qjs_scroll(jsthread *t, int *sx, int *sy)
{
	*sx = *sy = 0;
	if (t->bw != NULL && t->bw->window != NULL &&
	    !guit->window->get_scroll(t->bw->window, sx, sy)) {
		*sx = *sy = 0;
	} else if (t->bw != NULL && t->bw->window != NULL && qjs_scale(t) != 1.0f) {
		/* (the frontend's scroll offsets are device px: in CSS px, zoomed) */
		*sx = (int) (*sx / qjs_scale(t) + 0.5f);
		*sy = (int) (*sy / qjs_scale(t) + 0.5f);
	} else if (t->bw != NULL && t->bw->window == NULL) {
		/* Onyx: an iframe's window (the core's): its scrollbars */
		*sx = scrollbar_get_offset(t->bw->scroll_x);
		*sy = scrollbar_get_offset(t->bw->scroll_y);
	}
}

/** rect(n[, painted]): [x, y, width, height] of its border box in the viewport (0s: no box)
 * -- Onyx: painted, where transforms put it (getBoundingClientRect) */
/* Onyx: a layout asked for (a rectangle, a style): the changes of this script turn laid out
 * first (they used to wait for the turn's end: offsetHeight after details.open = true) */
static void qjs_layout_now(jsthread *t)
{
	/* Onyx: a frame's viewport is its element's box: its parent's changes (the
	 * element resized: Acid3's media queries) laid out first, the frame's new size
	 * (a media change: html_reformat) then lays it out again */
	if (t->bw != NULL && t->bw->parent != NULL &&
	    t->bw->parent->current_content != NULL &&
	    content_get_type(t->bw->parent->current_content) == CONTENT_HTML) {
		html_content *ph = (html_content *)
				hlcache_handle_get_content(t->bw->parent->current_content);

		if (ph != NULL && ph->jsthread != NULL && ph->jsthread != t &&
		    ph->jsthread->dirty)
			qjs_layout_now(ph->jsthread);
		if (t->htmlc != NULL && t->htmlc->rebox_pending)
			t->dirty = true;
	}
	/* (at most 16 a turn, or 100 ms of them: a script alternating writes and reads would
	 * rebuild the whole layout at each read -- past that, the reads see the layout before
	 * the turn; a small document's are cheap: Acid3's selector tests read a style after
	 * each rule they add, dozens a turn) */
	if (t->dirty && !t->closed && (t->forced_layouts < 16 || t->forced_ms < 100)) {
		uint64_t t0 = onyx_perf_now(), ms0 = qjs_now_ms();

		t->forced_layouts++;
		t->dirty = false;
		/* Onyx: the <style>s it changed converted first (their rules apply) */
		html_css_flush_sync(t->htmlc);
		html_script_dom_changed_by_script(t->htmlc);	/* (Onyx) */
		html_script_layout_now(t->htmlc);
		t->forced_ms += qjs_now_ms() - ms0;
		/* Onyx (NS_PERF): a long layout a script's read forced, and where (the script's
		 * stack: its first frames) -- a whole rebox of a big page each time */
		if (onyx_perf_on() && onyx_perf_now() - t0 > 50000) {
			JSValue e = JS_NewError(t->ctx);
			JSValue st = JS_GetPropertyStr(t->ctx, e, "stack");
			const char *s = JS_ToCString(t->ctx, st);
			char what[200];
			size_t i;

			snprintf(what, sizeof what, "js:forced-layout %s", s != NULL ? s : "?");
			for (i = 0; what[i] != '\0'; i++)
				if (what[i] == '\n')
					what[i] = '|';
			onyx_perf_log(what, t0);
			if (s != NULL)
				JS_FreeCString(t->ctx, s);
			JS_FreeValue(t->ctx, st);
			JS_FreeValue(t->ctx, e);
		}
	}
	html_script_layout_now(t->htmlc);
	/* Onyx: the data: URL images in (a pending image's box is its alt text's: Acid3's
	 * test 72 reads the height a style gives one) */
	if (t->htmlc != NULL)
		html_object_flush_sync(t->htmlc);
	onyx_anim_flush(t->htmlc);	/* (Onyx: an animation a script changed) */
}

/* Onyx: an element's scroll position and extent, its scroller's (overflow: auto / scroll):
 * [scrollLeft, scrollTop, scrollWidth, scrollHeight, clientWidth, clientHeight] */
static JSValue n_box_scroll(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	JSValue arr = JS_NewArray(ctx);
	int v[6] = { 0, 0, 0, 0, 0, 0 }, i;
	struct box *box;
	QJS_NODE_ARG(n, 0);

	if (t->htmlc != NULL)
		qjs_layout_now(t);
	box = qjs_box(n);
	if (box != NULL && t->htmlc != NULL && t->htmlc->layout != NULL) {
		int w = box->padding[LEFT] + box->width + box->padding[RIGHT];
		int h = box->padding[TOP] + box->height + box->padding[BOTTOM];

		v[0] = scrollbar_get_offset(box->scroll_x);
		v[1] = scrollbar_get_offset(box->scroll_y);
		/* (Onyx: the extent it scrolls over: not its fixed descendants) */
		v[2] = box->scroll_ext_x1 > w ? box->scroll_ext_x1 : w;
		v[3] = box->scroll_ext_y1 > h ? box->scroll_ext_y1 : h;
		if (box->style != NULL &&
		    css_computed_overflow_x(box->style) == CSS_OVERFLOW_VISIBLE)
			v[2] = w;	/* (not a scroller: its own size) */
		if (box->style != NULL &&
		    css_computed_overflow_y(box->style) == CSS_OVERFLOW_VISIBLE)
			v[3] = h;
		v[4] = w - (box->scroll_y != NULL ? SCROLLBAR_WIDTH : 0);
		v[5] = h - (box->scroll_x != NULL ? SCROLLBAR_WIDTH : 0);
	}
	for (i = 0; i < 6; i++)
		JS_SetPropertyUint32(ctx, arr, i, JS_NewInt32(ctx, v[i]));
	return arr;
}

/* Onyx: an element's scroller scrolled to (x, y) (its scroll event follows) */
static JSValue n_box_scroll_to(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	struct box *box;
	int32_t x = 0, y = 0;
	QJS_NODE_ARG(n, 0);

	if (argc > 1)
		JS_ToInt32(ctx, &x, argv[1]);
	if (argc > 2)
		JS_ToInt32(ctx, &y, argv[2]);
	if (t->htmlc == NULL)
		return JS_UNDEFINED;
	qjs_layout_now(t);
	box = qjs_box(n);
	if (box == NULL || t->htmlc->layout == NULL)
		return JS_UNDEFINED;
	if (box->scroll_x != NULL && x != scrollbar_get_offset(box->scroll_x))
		scrollbar_set(box->scroll_x, x < 0 ? 0 : x, false);
	if (box->scroll_y != NULL && y != scrollbar_get_offset(box->scroll_y))
		scrollbar_set(box->scroll_y, y < 0 ? 0 : y, false);
	return JS_UNDEFINED;
}

/* Onyx: element.focus() on a text field or a textarea: the browser's caret in it, at the
 * end of its text (what the user types goes there, as after a click) */
static JSValue n_focus_control(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	QJS_NODE_ARG(n, 0);

	if (t->htmlc == NULL || t->closed)
		return JS_FALSE;
	/* (the DOM's changes of this turn: a rebox due, the caret put after it) */
	if (t->dirty) {
		t->dirty = false;
		html_script_dom_changed(t->htmlc);
	}
	return JS_NewBool(ctx, html_script_focus_control(t->htmlc, n));
}

/* Onyx: hitNode(x, y): the element under a point of the viewport as a click finds it
 * (html_hit_path: the box painted last there, visibility: hidden ones passed over) --
 * document.elementFromPoint; null outside the page */
static JSValue n_hit_node(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	int32_t x = 0, y = 0;
	int sx = 0, sy = 0, n = 0, i;
	struct box **path;
	dom_node *node = NULL;
	JSValue v = JS_NULL;

	if (t->htmlc == NULL || argc < 2 || JS_ToInt32(ctx, &x, argv[0]) ||
	    JS_ToInt32(ctx, &y, argv[1]))
		return JS_NULL;
	qjs_layout_now(t);
	if (t->htmlc->layout == NULL)
		return JS_NULL;
	qjs_scroll(t, &sx, &sy);
	path = html_hit_path(t->htmlc, x + sx, y + sy, &n);
	if (path == NULL)
		return JS_NULL;
	for (i = n - 1; i >= 0 && node == NULL; i--) {
		dom_node_type type;
		if (path[i]->node == NULL ||
		    dom_node_get_node_type(path[i]->node, &type) != DOM_NO_ERR ||
		    type != DOM_ELEMENT_NODE)
			continue;
		node = path[i]->node;
	}
	if (node != NULL)
		v = qjs_wrap(t, node);
	free(path);
	return v;
}

static JSValue n_rect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	JSValue arr = JS_NewArray(ctx);
	int x = 0, y = 0, w = 0, h = 0, sx, sy;
	struct box *box;
	QJS_NODE_ARG(n, 0);

	if (t->htmlc != NULL)
		qjs_layout_now(t);	/* a pending layout done */
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
			/* an inline: from its start to its end on the line --
			 * Onyx: on several lines, the union of its lines (its
			 * content's extent across, its first line's top to its
			 * last's bottom), as Chrome's getBoundingClientRect */
			int ex, ey, x0 = INT_MAX, x1 = INT_MIN, y0 = INT_MAX, y1;
			struct box *d, *e = box->inline_end;

			box_coords(e, &ex, &ey);
			ex += e->padding[RIGHT] + e->border[RIGHT].width;
			if (ey == y + box->border[TOP].width && ex > x) {
				w = ex - x;
			} else if (ey > y + box->border[TOP].width &&
					e->parent == box->parent) {
				/* (its content's boxes: an empty start left at
				 * the end of the line before is not a line of it) */
				for (d = box->next; d != NULL && d != e; d = d->next) {
					int dx, dy;
					if ((d->type != BOX_TEXT || d->length == 0) &&
					    d->type != BOX_INLINE_BLOCK &&
					    d->type != BOX_INLINE_FLEX)
						continue;
					box_coords(d, &dx, &dy);
					if (dx < x0)
						x0 = dx;
					if (dy < y0)
						y0 = dy;
					if (dx + d->width + d->padding[LEFT] +
						d->padding[RIGHT] > x1)
						x1 = dx + d->width +
							d->padding[LEFT] +
							d->padding[RIGHT];
				}
				if (x0 != INT_MAX) {
					if (ex > x1)
						x1 = ex;
					if (y0 > y && y0 - box->padding[TOP] -
						box->border[TOP].width > y)
						y = y0 - box->padding[TOP] -
							box->border[TOP].width;
					y1 = ey + e->height + e->padding[BOTTOM] +
						e->border[BOTTOM].width;
					x = x0 - box->padding[LEFT] -
						box->border[LEFT].width;
					w = x1 - x;
					h = y1 - y;
				}
			}
		}
		qjs_scroll(t, &sx, &sy);
		{	/* Onyx: in a fixed box, where it is painted (docs/06 §41) */
			int fdx, fdy;
			if (html_box_fixed_shift(t->htmlc, box, &fdx, &fdy)) {
				x += fdx;
				y += fdy;
			}
		}
		x -= sx;
		y -= sy;
		/* Onyx: a box transformed (or in a transformed box): the bounding box of
		 * where it is painted (html/onyx_fx.c), in fractions of a px as Chrome's */
		{
			float r[4] = { x + sx, y + sy, x + sx + w, y + sy + h };
			if (argc > 1 && JS_ToBool(ctx, argv[1]) &&
			    onyx_fx_page_rect(t->htmlc, box, true, false, r)) {
				JS_SetPropertyUint32(ctx, arr, 0, JS_NewFloat64(ctx, r[0] - sx));
				JS_SetPropertyUint32(ctx, arr, 1, JS_NewFloat64(ctx, r[1] - sy));
				JS_SetPropertyUint32(ctx, arr, 2,
						JS_NewFloat64(ctx, r[2] - r[0]));
				JS_SetPropertyUint32(ctx, arr, 3,
						JS_NewFloat64(ctx, r[3] - r[1]));
				return arr;
			}
		}
	}
	JS_SetPropertyUint32(ctx, arr, 0, JS_NewInt32(ctx, x));
	JS_SetPropertyUint32(ctx, arr, 1, JS_NewInt32(ctx, y));
	JS_SetPropertyUint32(ctx, arr, 2, JS_NewInt32(ctx, w));
	JS_SetPropertyUint32(ctx, arr, 3, JS_NewInt32(ctx, h));
	return arr;
}

/** image(n): an <img>'s picture as the page has it -- [state (0 loading, 1 loaded, 2 broken),
 * natural width, natural height], or null when it has no box (not displayed) (Onyx) */
static JSValue n_image(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct box *box;
	JSValue arr;
	int st = 0, w = 0, h = 0;
	QJS_NODE_ARG(n, 0);

	box = qjs_box(n);
	if (box == NULL)
		return JS_NULL;
	if (box->object != NULL) {
		content_status cs = content_get_status(box->object);
		if (cs == CONTENT_STATUS_DONE || cs == CONTENT_STATUS_READY) {
			st = 1;
			w = content_get_width(box->object);
			h = content_get_height(box->object);
		} else if (cs == CONTENT_STATUS_ERROR) {
			st = 2;
		}
	} else {
		st = 2;		/* (a box without its picture: failed or none) */
	}
	arr = JS_NewArray(ctx);
	JS_SetPropertyUint32(ctx, arr, 0, JS_NewInt32(ctx, st));
	JS_SetPropertyUint32(ctx, arr, 1, JS_NewInt32(ctx, w));
	JS_SetPropertyUint32(ctx, arr, 2, JS_NewInt32(ctx, h));
	return arr;
}

/** addFontFace(family, wmin, wmax, italic, bytes): a font a script loaded (the CSS Font
 * Loading API) given to the document's font code -> whether it was read (Onyx) */
static JSValue n_add_font_face(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	const char *family;
	int32_t wmin = 400, wmax = 400;
	size_t size = 0;
	uint8_t *data;
	bool ok = false;

	(void) this_val;
	if (argc < 5 || t->htmlc == NULL || (family = JS_ToCString(ctx, argv[0])) == NULL)
		return JS_FALSE;
	JS_ToInt32(ctx, &wmin, argv[1]);
	JS_ToInt32(ctx, &wmax, argv[2]);
	data = JS_GetArrayBuffer(ctx, &size, argv[4]);
	if (data == NULL) {
		size_t off = 0, len = 0, bpe = 0;
		JSValue ab;
		JS_FreeValue(ctx, JS_GetException(ctx));
		ab = JS_GetTypedArrayBuffer(ctx, argv[4], &off, &len, &bpe);
		if (!JS_IsException(ab)) {
			uint8_t *d = JS_GetArrayBuffer(ctx, &size, ab);
			if (d != NULL && off + len <= size) {
				data = d + off;
				size = len;
			}
			JS_FreeValue(ctx, ab);
		} else {
			JS_FreeValue(ctx, JS_GetException(ctx));
		}
	}
	if (data != NULL)
		ok = onyx_webfont_add_script_face(t->htmlc, family, wmin, wmax,
				JS_ToBool(ctx, argv[3]), data, size);
	JS_FreeCString(ctx, family);
	return JS_NewBool(ctx, ok);
}

/** boxed(n): whether the node has a box (it is displayed) */
static JSValue n_boxed(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	QJS_NODE_ARG(n, 0);

	if (t->htmlc != NULL)
		qjs_layout_now(t);
	return JS_NewBool(ctx, qjs_box(n) != NULL);
}

/**
 * offset(n, body): [offsetParent, offsetLeft, offsetTop] as CSSOM View defines them (Onyx,
 * docs/06 §41), or null when the element has no box. The offsetParent: the nearest
 * positioned ancestor, the body, or a td / th / table for a static element; none (null) for
 * a fixed element. The offsets: its border box from that parent's padding box -- from the
 * document's origin when the parent is the body or none -- the scrolls between left out
 * (box_coords counts them). The offsetParent was always the body, the body's own included:
 * a loop up the chain (`while (e.offsetParent) e = e.offsetParent`, Facebook's
 * VisualCompletion) never ended -- 60 s of a frozen window on facebook.com's login page.
 */
static JSValue n_offset(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	dom_node *body = argc > 1 ? qjs_node(argv[1]) : NULL;
	struct box *box, *b, *op = NULL;
	dom_node *opn = NULL;
	bool fixed, stat;
	int x, y;
	JSValue arr;
	QJS_NODE_ARG(n, 0);

	if (t->htmlc != NULL)
		qjs_layout_now(t);
	box = qjs_box(n);
	if (box == NULL || box->style == NULL || t->htmlc == NULL || t->htmlc->layout == NULL)
		return JS_NULL;
	fixed = css_computed_position(box->style) == CSS_POSITION_FIXED;
	stat = css_computed_position(box->style) == CSS_POSITION_STATIC;
	x = box->x - box->border[LEFT].width;
	y = box->y - box->border[TOP].width;
	/* (up the boxes: a float's position is its float container's) */
#define QJS_UP(b) ((((b)->type == BOX_FLOAT_LEFT || (b)->type == BOX_FLOAT_RIGHT) && \
		(b)->float_container != NULL) ? (b)->float_container : (b)->parent)
	for (b = box; b->parent != NULL; ) {
		b = QJS_UP(b);
		if (!fixed && b->node != NULL && b->node != n && b->style != NULL &&
		    (b->node == body ||
		     css_computed_position(b->style) != CSS_POSITION_STATIC ||
		     (stat && (b->type == BOX_TABLE_CELL || b->type == BOX_TABLE)))) {
			op = b;
			opn = b->node;
			break;
		}
		x += b->x;
		y += b->y;
	}
	if (op != NULL && opn == body) {
		/* (the body: from the document's origin) */
		for (b = op; b != NULL; b = QJS_UP(b)) {
			x += b->x;
			y += b->y;
		}
	}
#undef QJS_UP
	arr = JS_NewArray(ctx);
	JS_SetPropertyUint32(ctx, arr, 0, opn != NULL ? qjs_wrap(t, opn) : JS_NULL);
	JS_SetPropertyUint32(ctx, arr, 1, JS_NewInt32(ctx, x));
	JS_SetPropertyUint32(ctx, arr, 2, JS_NewInt32(ctx, y));
	return arr;
}

/** scroll(): [x, y, viewport width, viewport height, page width, page height] */
static JSValue n_scroll(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	JSValue arr = JS_NewArray(ctx);
	int sx, sy, w = 0, h = 0, pw = 0, ph = 0;

	qjs_scroll(t, &sx, &sy);
	if (t->bw != NULL && t->bw->window != NULL) {
		guit->window->get_dimensions(t->bw->window, &w, &h);
		w = (int) (w / qjs_scale(t));	/* (Onyx: zoomed, in CSS px) */
		h = (int) (h / qjs_scale(t));
	} else if (t->bw != NULL)	/* (Onyx: an iframe's window: its size, innerWidth) */
		browser_window_get_dimensions(t->bw, &w, &h);
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
	JS_SetPropertyUint32(ctx, arr, 6, JS_NewFloat64(ctx,	/* (Onyx: the zoom, as 1.1 not 1.10000002) */
			(double) (long) (qjs_scale(t) * 10000.0f + 0.5f) / 10000.0));
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
		float sc = qjs_scale(t);	/* (Onyx: CSS px -> the window's, zoomed) */
		r.x0 = r.x1 = x < 0 ? 0 : (int) (x * sc + 0.5f);
		r.y0 = r.y1 = y < 0 ? 0 : (int) (y * sc + 0.5f);
		guit->window->set_scroll(t->bw->window, &r);
	} else if (t->bw != NULL && !t->closed) {
		/* Onyx: an iframe's window: its scrollbars (the scroll event comes from them) */
		if (t->bw->scroll_x != NULL)
			scrollbar_set(t->bw->scroll_x, x < 0 ? 0 : x, false);
		if (t->bw->scroll_y != NULL)
			scrollbar_set(t->bw->scroll_y, y < 0 ? 0 : y, false);
	}
	return JS_UNDEFINED;
}

/** cstyle(n, property): a few computed properties of its box, as CSS text */
/* Onyx: the computed style of an element without a box (display: none, in a hidden subtree,
 * or before the first layout: a script of the page's parse) -- selected as the box tree's
 * construction would, from the root down. The results are pushed on res[] (the caller
 * destroys them); NULL: not in the document, or no styles yet. */
#define QJS_UNBOXED_DEPTH 64
static const css_computed_style *qjs_unboxed_style(jsthread *t, dom_node *n,
		css_select_results **res, int *nres)
{
	html_content *c = t->htmlc;
	dom_node *chain[QJS_UNBOXED_DEPTH], *root = NULL, *cur, *next;
	const css_computed_style *parent = NULL, *root_style = NULL;
	css_select_ctx *temp = NULL;
	css_fixed dpi = 0, fdef = 0, fmin = 0;
	int depth = 0, i;

	*nres = 0;
	if (c == NULL || c->document == NULL)
		return NULL;
	if (dom_document_get_document_element(c->document, &root) != DOM_NO_ERR ||
	    root == NULL)
		return NULL;
	/* the element and its ancestors, up to the root element (Onyx: its flat tree
	 * ancestors when the document has shadow roots: a slot, a host) */
	if (dom_onyx_has_shadow(c->document))
		c->onyx_shadow = true;
	cur = n;
	dom_node_ref(cur);
	while (cur != NULL && cur != root) {
		dom_node_type type;
		if (dom_node_get_node_type(cur, &type) != DOM_NO_ERR ||
		    type != DOM_ELEMENT_NODE || depth == QJS_UNBOXED_DEPTH - 1) {
			dom_node_unref(cur);
			cur = NULL;
			break;
		}
		chain[depth++] = cur;
		if (c->onyx_shadow)
			next = onyx_flat_parent(c, cur);
		else if (dom_node_get_parent_node(cur, &next) != DOM_NO_ERR)
			next = NULL;
		cur = next;
	}
	if (cur == NULL) {
		/* detached (or too deep) */
		for (i = 0; i < depth; i++)
			dom_node_unref(chain[i]);
		dom_node_unref(root);
		return NULL;
	}
	dom_node_unref(cur);
	chain[depth++] = root;	/* (the root's reference taken above) */

	if (c->select_ctx == NULL) {
		/* still parsing: the style sheets loaded so far */
		if (html_css_new_selection_context(c, &temp) != NSERROR_OK)
			temp = NULL;
		c->select_ctx = temp;
	}
	if (c->unit_len_ctx.device_dpi == 0) {
		/* (the viewport not measured yet: the default font sizes) */
		dpi = nscss_screen_dpi;
		fdef = FDIV(FMUL(F_96, FDIV(INTTOFIX(nsoption_int(font_size)), F_10)), F_72);
		fmin = FDIV(FMUL(F_96, FDIV(INTTOFIX(nsoption_int(font_min_size)), F_10)), F_72);
		c->unit_len_ctx.device_dpi = dpi;
		c->unit_len_ctx.font_size_default = fdef;
		c->unit_len_ctx.font_size_minimum = fmin;
	}
	c->onyx_anim_probe++;	/* (Onyx: not the elements' styles: no animation) */
	if (c->select_ctx != NULL) {
		for (i = depth - 1; i >= 0; i--) {
			css_select_results *r = box_style_select(c, parent,
					root_style, chain[i]);
			if (r == NULL || r->styles[CSS_PSEUDO_ELEMENT_NONE] == NULL) {
				if (r != NULL)
					css_select_results_destroy(r);
				parent = NULL;
				break;
			}
			res[(*nres)++] = r;
			parent = r->styles[CSS_PSEUDO_ELEMENT_NONE];
			if (i == depth - 1)
				root_style = parent;
		}
	}
	c->onyx_anim_probe--;
	if (dpi != 0) {
		c->unit_len_ctx.device_dpi = 0;
		c->unit_len_ctx.font_size_default = 0;
		c->unit_len_ctx.font_size_minimum = 0;
	}
	if (temp != NULL) {
		c->select_ctx = NULL;
		css_select_ctx_destroy(temp);
	}
	for (i = 0; i < depth; i++)
		dom_node_unref(chain[i]);
	return parent;
}

/* Onyx: a length of a computed style as CSS text ("auto" for its other types) */
static void qjs_len_text(uint8_t type, uint8_t set_type, css_fixed len, css_unit unit,
		char *buf, size_t size)
{
	if (type != set_type) {
		snprintf(buf, size, "auto");
		return;
	}
	snprintf(buf, size, "%g%s", FIXTOFLT(len), unit == CSS_UNIT_PCT ? "%" :
			unit == CSS_UNIT_EM ? "em" : "px");
}

static void qjs_color_text(css_color c, char *buf, size_t size)
{
	if ((c >> 24) == 0xff)
		snprintf(buf, size, "rgb(%u, %u, %u)", (c >> 16) & 0xff, (c >> 8) & 0xff,
				c & 0xff);
	else
		snprintf(buf, size, "rgba(%u, %u, %u, %g)", (c >> 16) & 0xff, (c >> 8) & 0xff,
				c & 0xff, ((c >> 24) & 0xff) / 255.0);
}

/* Onyx: a list text of the transition / animation properties as CSS writes it */
static void qjs_list_text(uint8_t type, lwc_string *text, const char *dflt, char *buf,
		size_t size)
{
	const char *p;
	size_t n = 0;

	if (type != CSS_ONYX_TEXT_SET || text == NULL) {
		snprintf(buf, size, "%s", dflt);
		return;
	}
	for (p = lwc_string_data(text); *p != '\0' && n + 3 < size; p++) {
		buf[n++] = *p;
		if (*p == ',')
			buf[n++] = ' ';
	}
	buf[n] = '\0';
}

/* Onyx: getComputedStyle's keyword properties (their computed values' names, by the
 * libcss enum's value) -- '' when not one of these */
static const char *qjs_cstyle_keyword(const css_computed_style *style, const char *prop)
{
	static const char *const ws[] = { NULL, "normal", "pre", "nowrap", "pre-wrap",
		"pre-line" };
	static const char *const tt[] = { NULL, "capitalize", "uppercase", "lowercase", "none" };
	static const char *const cur[] = { NULL, "auto", "crosshair", "default", "pointer",
		"move", "e-resize", "ne-resize", "nw-resize", "n-resize", "se-resize",
		"sw-resize", "s-resize", "w-resize", "text", "wait", "help", "progress",
		"none", "context-menu", "cell", "vertical-text", "alias", "copy", "no-drop",
		"not-allowed", "ew-resize", "ns-resize", "nesw-resize", "nwse-resize",
		"col-resize", "row-resize", "all-scroll" };
	static const char *const fl[] = { NULL, "left", "right", "none" };
	static const char *const cl[] = { NULL, "none", "left", "right", "both" };
	static const char *const ta[] = { NULL, "start", "left", "right", "center", "justify",
		"start", "-webkit-left", "-webkit-center", "-webkit-right" };
	static const char *const fs[] = { NULL, "normal", "italic", "oblique" };
	static const char *const fv[] = { NULL, "normal", "small-caps" };
	static const char *const fw[] = { NULL, "400", "700", "700", "100", "100", "200", "300",
		"400", "500", "600", "700", "800", "900" };
	static const char *const dir[] = { NULL, "ltr", "rtl" };
	static const char *const ub[] = { NULL, "normal", "embed", "bidi-override" };
	static const char *const bs[] = { NULL, "content-box", "border-box" };
	static const char *const tl[] = { NULL, "auto", "fixed" };
	static const char *const bc[] = { NULL, "separate", "collapse" };
	static const char *const ec[] = { NULL, "show", "hide" };
	static const char *const cs[] = { NULL, "top", "bottom" };
	static const char *const lp[] = { NULL, "inside", "outside" };
	static const char *const bst[] = { NULL, "none", "hidden", "dotted", "dashed", "solid",
		"double", "groove", "ridge", "inset", "outset" };
	static const char *const va[] = { NULL, "baseline", "sub", "super", "top", "text-top",
		"middle", "bottom", "text-bottom" };
#define KW(name, tab, val) \
	if (strcmp(prop, name) == 0) { \
		unsigned v_ = (val); \
		return v_ < sizeof(tab) / sizeof(tab[0]) && tab[v_] != NULL ? tab[v_] : ""; \
	}
	lwc_string **urls = NULL;
	css_fixed len = 0;
	css_unit unit = CSS_UNIT_PX;

	KW("white-space", ws, css_computed_white_space(style));
	KW("text-transform", tt, css_computed_text_transform(style));
	KW("cursor", cur, css_computed_cursor(style, &urls));
	KW("float", fl, css_computed_float(style));
	KW("clear", cl, css_computed_clear(style));
	KW("text-align", ta, css_computed_text_align(style));
	KW("font-style", fs, css_computed_font_style(style));
	KW("font-variant", fv, css_computed_font_variant(style));
	KW("font-weight", fw, css_computed_font_weight(style));
	KW("direction", dir, css_computed_direction(style));
	KW("unicode-bidi", ub, css_computed_unicode_bidi(style));
	KW("box-sizing", bs, css_computed_box_sizing(style));
	KW("table-layout", tl, css_computed_table_layout(style));
	KW("border-collapse", bc, css_computed_border_collapse(style));
	KW("empty-cells", ec, css_computed_empty_cells(style));
	KW("caption-side", cs, css_computed_caption_side(style));
	KW("list-style-position", lp, css_computed_list_style_position(style));
	KW("border-top-style", bst, css_computed_border_top_style(style));
	KW("border-right-style", bst, css_computed_border_right_style(style));
	KW("border-bottom-style", bst, css_computed_border_bottom_style(style));
	KW("border-left-style", bst, css_computed_border_left_style(style));
	KW("outline-style", bst, css_computed_outline_style(style));
	if (strcmp(prop, "vertical-align") == 0) {
		uint8_t v = css_computed_vertical_align(style, &len, &unit);
		return v < sizeof(va) / sizeof(va[0]) && va[v] != NULL ? va[v] : "";
	}
#undef KW
	return NULL;
}

/* Onyx: getComputedStyle's other properties (html/onyx_anim.c animates them) */
static void qjs_cstyle_more(jsthread *t, const css_computed_style *style, struct box *box,
		const char *prop, char *buf, size_t size)
{
	css_fixed len = 0;
	css_unit unit = CSS_UNIT_PX;
	css_color c = 0;
	lwc_string *text = NULL;
	uint8_t ty;
	const char *kw = qjs_cstyle_keyword(style, prop);

	if (kw != NULL && kw[0] != '\0') {
		snprintf(buf, size, "%s", kw);
	} else if (strcmp(prop, "vertical-align") == 0) {
		ty = css_computed_vertical_align(style, &len, &unit);
		qjs_len_text(ty, CSS_VERTICAL_ALIGN_SET, len, unit, buf, size);
	} else if (strcmp(prop, "transform") == 0) {
		float w = 0, h = 0;
		if (box != NULL) {
			w = box->padding[LEFT] + box->width + box->padding[RIGHT] +
				box->border[LEFT].width + box->border[RIGHT].width;
			h = box->padding[TOP] + box->height + box->padding[BOTTOM] +
				box->border[TOP].width + box->border[BOTTOM].width;
		}
		if (t->htmlc != NULL)
			onyx_anim_transform_text(style, t->htmlc, w, h, buf, (int) size);
	} else if (strcmp(prop, "translate") == 0 || strcmp(prop, "scale") == 0 ||
		   strcmp(prop, "rotate") == 0) {
		ty = prop[0] == 't' ? css_computed_translate(style, &text) :
			prop[0] == 's' ? css_computed_scale(style, &text) :
			css_computed_rotate(style, &text);
		snprintf(buf, size, "%s", ty == CSS_ONYX_TEXT_SET && text != NULL ?
				lwc_string_data(text) : "none");
	} else if (strcmp(prop, "filter") == 0 || strcmp(prop, "backdrop-filter") == 0) {
		/* (their canonical texts: "blur(4px) brightness(1.2)") */
		ty = prop[0] == 'f' ? css_computed_filter(style, &text) :
			css_computed_backdrop_filter(style, &text);
		snprintf(buf, size, "%s", ty == CSS_ONYX_TEXT_SET && text != NULL ?
				lwc_string_data(text) : "none");
	} else if (strcmp(prop, "left") == 0) {
		ty = css_computed_left(style, &len, &unit);
		qjs_len_text(ty, CSS_LEFT_SET, len, unit, buf, size);
	} else if (strcmp(prop, "top") == 0) {
		ty = css_computed_top(style, &len, &unit);
		qjs_len_text(ty, CSS_TOP_SET, len, unit, buf, size);
	} else if (strcmp(prop, "right") == 0) {
		ty = css_computed_right(style, &len, &unit);
		qjs_len_text(ty, CSS_RIGHT_SET, len, unit, buf, size);
	} else if (strcmp(prop, "bottom") == 0) {
		ty = css_computed_bottom(style, &len, &unit);
		qjs_len_text(ty, CSS_BOTTOM_SET, len, unit, buf, size);
	} else if (strncmp(prop, "margin-", 7) == 0) {
		const char *side = prop + 7;
		ty = side[0] == 't' ? css_computed_margin_top(style, &len, &unit) :
			side[0] == 'r' ? css_computed_margin_right(style, &len, &unit) :
			side[0] == 'b' ? css_computed_margin_bottom(style, &len, &unit) :
			css_computed_margin_left(style, &len, &unit);
		qjs_len_text(ty, CSS_MARGIN_SET, len, unit, buf, size);
	} else if (strncmp(prop, "padding-", 8) == 0) {
		const char *side = prop + 8;
		ty = side[0] == 't' ? css_computed_padding_top(style, &len, &unit) :
			side[0] == 'r' ? css_computed_padding_right(style, &len, &unit) :
			side[0] == 'b' ? css_computed_padding_bottom(style, &len, &unit) :
			css_computed_padding_left(style, &len, &unit);
		qjs_len_text(ty, CSS_PADDING_SET, len, unit, buf, size);
	} else if (strncmp(prop, "border-", 7) == 0 && strstr(prop, "-color") != NULL) {
		const char *side = prop + 7;
		if (side[0] == 'r') css_computed_border_right_color(style, &c);
		else if (side[0] == 'b') css_computed_border_bottom_color(style, &c);
		else if (side[0] == 'l') css_computed_border_left_color(style, &c);
		else css_computed_border_top_color(style, &c);
		qjs_color_text(c, buf, size);
	} else if (strncmp(prop, "border-", 7) == 0 && strstr(prop, "-width") != NULL) {
		const char *side = prop + 7;
		if (side[0] == 'r') css_computed_border_right_width(style, &len, &unit);
		else if (side[0] == 'b') css_computed_border_bottom_width(style, &len, &unit);
		else if (side[0] == 'l') css_computed_border_left_width(style, &len, &unit);
		else css_computed_border_top_width(style, &len, &unit);
		snprintf(buf, size, "%gpx", FIXTOFLT(len));
	} else if (strcmp(prop, "outline-color") == 0) {
		css_computed_outline_color(style, &c);
		qjs_color_text(c, buf, size);
	} else if (strcmp(prop, "letter-spacing") == 0) {
		ty = css_computed_letter_spacing(style, &len, &unit);
		if (ty == CSS_LETTER_SPACING_SET)
			qjs_len_text(ty, CSS_LETTER_SPACING_SET, len, unit, buf, size);
		else
			snprintf(buf, size, "normal");
	} else if (strcmp(prop, "z-index") == 0) {
		int32_t z = 0;
		if (css_computed_z_index(style, &z) == CSS_Z_INDEX_SET)	/* (a css_fixed) */
			snprintf(buf, size, "%d", (int) FIXTOINT(z));
		else
			snprintf(buf, size, "auto");
	} else if (strcmp(prop, "box-shadow") == 0) {
		snprintf(buf, size, "none");
		{
			css_fixed x = 0, y = 0, b = 0, sp = 0;
			css_unit ux, uy, ub, us;
			ty = css_computed_box_shadow(style, &x, &ux, &y, &uy, &b, &ub, &sp, &us, &c);
			if (ty != CSS_BOX_SHADOW_NONE && ty != 0) {
				char col[48];
				qjs_color_text(c, col, sizeof(col));
				snprintf(buf, size, "%s %gpx %gpx %gpx %gpx%s", col, FIXTOFLT(x),
						FIXTOFLT(y), FIXTOFLT(b), FIXTOFLT(sp),
						ty == CSS_BOX_SHADOW_SET_INSET ||
						ty == CSS_BOX_SHADOW_SET_INSET_CURRENT_COLOR ?
						" inset" : "");
			}
		}
	} else if (strcmp(prop, "transition-property") == 0) {
		ty = css_computed_transition_property(style, &text);
		qjs_list_text(ty, text, "all", buf, size);
	} else if (strcmp(prop, "transition-duration") == 0) {
		ty = css_computed_transition_duration(style, &text);
		qjs_list_text(ty, text, "0s", buf, size);
	} else if (strcmp(prop, "transition-timing-function") == 0) {
		ty = css_computed_transition_timing_function(style, &text);
		qjs_list_text(ty, text, "ease",
				buf, size);
	} else if (strcmp(prop, "transition-delay") == 0) {
		ty = css_computed_transition_delay(style, &text);
		qjs_list_text(ty, text, "0s", buf, size);
	} else if (strcmp(prop, "animation-name") == 0) {
		ty = css_computed_animation_name(style, &text);
		qjs_list_text(ty, text, "none", buf, size);
	} else if (strcmp(prop, "animation-duration") == 0) {
		ty = css_computed_animation_duration(style, &text);
		qjs_list_text(ty, text, "0s", buf, size);
	} else if (strcmp(prop, "animation-timing-function") == 0) {
		ty = css_computed_animation_timing_function(style, &text);
		qjs_list_text(ty, text, "ease",
				buf, size);
	} else if (strcmp(prop, "animation-delay") == 0) {
		ty = css_computed_animation_delay(style, &text);
		qjs_list_text(ty, text, "0s", buf, size);
	} else if (strcmp(prop, "animation-iteration-count") == 0) {
		ty = css_computed_animation_iteration_count(style, &text);
		qjs_list_text(ty, text, "1",
				buf, size);
	} else if (strcmp(prop, "animation-direction") == 0) {
		ty = css_computed_animation_direction(style, &text);
		qjs_list_text(ty, text, "normal",
				buf, size);
	} else if (strcmp(prop, "animation-fill-mode") == 0) {
		ty = css_computed_animation_fill_mode(style, &text);
		qjs_list_text(ty, text, "none",
				buf, size);
	} else if (strcmp(prop, "animation-play-state") == 0) {
		ty = css_computed_animation_play_state(style, &text);
		qjs_list_text(ty, text, "running",
				buf, size);
	}
}

/* Onyx: whether cstyle answers a property (n_cstyle, qjs_cstyle_more) -- for the others
 * (a custom property, one not computed here) the page is not laid out first: dom.js answers
 * from the style attribute. browserscore.dev reads '--color' after each render: a whole
 * rebox of its 10000 elements for nothing (0.4 s on the PC, seconds on the Pi). */
static bool qjs_cstyle_known(const char *p)
{
	static const char *const names[] = { "display", "visibility", "position", "opacity",
		"fill", "stroke", "stroke-width", "width", "height", "color", "background-color",
		"font-size", "transform", "translate", "scale", "rotate", "filter",
		"backdrop-filter", "left", "top", "right", "bottom", "outline-color",
		"letter-spacing", "z-index", "box-shadow",
		"overflow", "overflow-x", "overflow-y",	/* (Onyx: overflow, google search) */
		/* (Onyx: the keyword properties: qjs_cstyle_keyword) */
		"white-space", "text-transform", "cursor", "float", "clear", "text-align",
		"font-style", "font-variant", "font-weight", "direction", "unicode-bidi",
		"box-sizing", "table-layout", "border-collapse", "empty-cells", "caption-side",
		"list-style-position", "outline-style", "vertical-align", "pointer-events" };
	size_t i;

	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		if (strcmp(p, names[i]) == 0)
			return true;
	if (strncmp(p, "margin-", 7) == 0 || strncmp(p, "padding-", 8) == 0 ||
	    strncmp(p, "transition-", 11) == 0 || strncmp(p, "animation-", 10) == 0)
		return true;
	return strncmp(p, "border-", 7) == 0 &&
		(strstr(p, "-color") != NULL || strstr(p, "-width") != NULL ||
		 strstr(p, "-style") != NULL);
}

static JSValue n_cstyle(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	const char *prop;
	struct box *box;
	char buf[512];	/* (Onyx: the transition / animation lists) */
	JSValue v = JS_NewString(ctx, "");
	/* Onyx: without a box, the style selected for it (qjs_unboxed_style) */
	css_select_results *ures[QJS_UNBOXED_DEPTH];
	int nures = 0;
	const css_computed_style *style = NULL;
	QJS_NODE_ARG(n, 0);

	prop = JS_ToCString(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
	if (prop == NULL)
		return v;
	if (!qjs_cstyle_known(prop)) {
		JS_FreeCString(ctx, prop);
		return v;
	}
	if (t->htmlc != NULL)
		qjs_layout_now(t);
	box = qjs_box(n);
	if (box != NULL && box->style != NULL)
		style = box->style;
	else
		style = qjs_unboxed_style(t, n, ures, &nures);
	if (style == NULL) {
		while (nures > 0)
			css_select_results_destroy(ures[--nures]);
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
			"flex", "inline-flex", "grid", "inline-grid", "contents" };
		uint8_t dv = css_computed_display(style, false);
		snprintf(buf, sizeof(buf), "%s", dv < sizeof(d) / sizeof(d[0]) ?
				d[dv] : "block");
	} else if (strcmp(prop, "visibility") == 0) {
		snprintf(buf, sizeof(buf), "%s", css_computed_visibility(
				style) == CSS_VISIBILITY_HIDDEN ? "hidden" :
				"visible");
	} else if (strcmp(prop, "pointer-events") == 0) {	/* (Onyx) */
		snprintf(buf, sizeof(buf), "%s", css_computed_pointer_events(style) ==
				CSS_POINTER_EVENTS_NONE ? "none" : "auto");
	} else if (strcmp(prop, "overflow") == 0 || strcmp(prop, "overflow-x") == 0 ||
		   strcmp(prop, "overflow-y") == 0) {
		/* Onyx: the scrollers' (a script looking for its scrolling ancestor) */
		static const char *o[] = { "visible", "visible", "hidden", "scroll", "auto" };
		uint8_t x = css_computed_overflow_x(style), y = css_computed_overflow_y(style);
		const char *xs = x < 5 ? o[x] : "visible", *ys = y < 5 ? o[y] : "visible";
		if (prop[8] == '-')
			snprintf(buf, sizeof(buf), "%s", prop[9] == 'x' ? xs : ys);
		else if (x == y)
			snprintf(buf, sizeof(buf), "%s", xs);
		else
			snprintf(buf, sizeof(buf), "%s %s", xs, ys);
	} else if (strcmp(prop, "position") == 0) {
		static const char *p[] = { "static", "static", "relative",
			"absolute", "fixed", "sticky" };
		uint8_t pv = css_computed_position(style);
		snprintf(buf, sizeof(buf), "%s", pv < 6 ? p[pv] : "static");
	} else if (strcmp(prop, "opacity") == 0) {
		css_fixed o = INTTOFIX(1);
		css_computed_opacity(style, &o);
		snprintf(buf, sizeof(buf), "%g", FIXTOFLT(o));
	} else if (strcmp(prop, "fill") == 0 || strcmp(prop, "stroke") == 0) {
		/* Onyx: SVG's paints (libcss computes them) */
		css_color c = 0, cur = 0;
		lwc_string *url = NULL;
		uint8_t pt = prop[0] == 'f' ? css_computed_fill(style, &c, &url) :
				css_computed_stroke(style, &c, &url);
		css_computed_color(style, &cur);
		if (pt == CSS_PAINT_CURRENT_COLOR)
			c = cur;
		if (pt == CSS_PAINT_NONE)
			snprintf(buf, sizeof(buf), "none");
		else if ((pt == CSS_PAINT_URL || pt == CSS_PAINT_URL_COLOR ||
				pt == CSS_PAINT_URL_CURRENT_COLOR) && url != NULL)
			snprintf(buf, sizeof(buf), "url(\"%.50s\")", lwc_string_data(url));
		else if (pt == CSS_PAINT_COLOR || pt == CSS_PAINT_CURRENT_COLOR)
			snprintf(buf, sizeof(buf), "rgb(%u, %u, %u)",
				 (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
	} else if (strcmp(prop, "stroke-width") == 0) {
		css_fixed len = 0;
		css_unit unit = CSS_UNIT_PX;
		css_computed_stroke_width(style, &len, &unit);
		snprintf(buf, sizeof(buf), "%g%s", FIXTOFLT(len),
				unit == CSS_UNIT_PCT ? "%" : unit == CSS_UNIT_EM ? "em" : "px");
	} else if (strcmp(prop, "width") == 0) {
		if (box != NULL && box->style == style)
			snprintf(buf, sizeof(buf), "%dpx", box->width);
		else
			snprintf(buf, sizeof(buf), "auto");
	} else if (strcmp(prop, "height") == 0) {
		if (box != NULL && box->style == style)
			snprintf(buf, sizeof(buf), "%dpx", box->height);
		else
			snprintf(buf, sizeof(buf), "auto");
	} else if (strcmp(prop, "color") == 0 ||
		   strcmp(prop, "background-color") == 0) {
		css_color c = 0;
		if (prop[0] == 'c')
			css_computed_color(style, &c);
		else
			css_computed_background_color(style, &c);
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
		css_computed_font_size(style, &len, &unit);
		snprintf(buf, sizeof(buf), "%gpx", FIXTOFLT(len));
	} else {
		/* Onyx: the properties animations change (their animated values) */
		qjs_cstyle_more(t, style, box != NULL && box->style == style ? box : NULL,
				prop, buf, sizeof(buf));
	}
	JS_FreeCString(ctx, prop);
	JS_FreeValue(ctx, v);
	while (nures > 0)
		css_select_results_destroy(ures[--nures]);
	return JS_NewString(ctx, buf);
}


/* ---- natives: animations (Onyx: html/onyx_anim.c) ----------------------------------------- */

/** frame(): an animation frame wanted (requestAnimationFrame) */
static JSValue n_frame(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);

	if (t->htmlc != NULL)
		onyx_anim_request_frame(t->htmlc);
	return JS_UNDEFINED;
}

static double qjs_num_prop(JSContext *ctx, JSValueConst o, const char *name, double dflt)
{
	JSValue v = JS_GetPropertyStr(ctx, o, name);
	double d = dflt;

	if (!JS_IsUndefined(v) && !JS_IsNull(v))
		JS_ToFloat64(ctx, &d, v);
	JS_FreeValue(ctx, v);
	return d;
}

/** animate(el, css[], offsets[], easings[], timing): a script's animation's id (0: none) */
static JSValue n_animate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	struct onyx_anim_timing tm;
	const char **css = NULL, **eas = NULL;
	double *off = NULL;
	const char *easing = NULL;
	int64_t n = 0;
	int k, id = 0;
	JSValue v;
	QJS_NODE_ARG(el, 0);

	if (t->htmlc == NULL || argc < 5)
		return JS_NewInt32(ctx, 0);
	qjs_layout_now(t);
	if (JS_GetLength(ctx, argv[1], &n) < 0 || n < 1 || n > 256)
		return JS_NewInt32(ctx, 0);
	css = calloc(n, sizeof(*css));
	eas = calloc(n, sizeof(*eas));
	off = calloc(n, sizeof(*off));
	if (css == NULL || eas == NULL || off == NULL)
		goto done;
	for (k = 0; k < n; k++) {
		v = JS_GetPropertyUint32(ctx, argv[1], k);
		css[k] = JS_ToCString(ctx, v);
		JS_FreeValue(ctx, v);
		v = JS_GetPropertyUint32(ctx, argv[2], k);
		JS_ToFloat64(ctx, &off[k], v);
		JS_FreeValue(ctx, v);
		v = JS_GetPropertyUint32(ctx, argv[3], k);
		if (JS_IsString(v))
			eas[k] = JS_ToCString(ctx, v);
		JS_FreeValue(ctx, v);
	}
	memset(&tm, 0, sizeof(tm));
	tm.delay = qjs_num_prop(ctx, argv[4], "delay", 0);
	tm.end_delay = qjs_num_prop(ctx, argv[4], "endDelay", 0);
	tm.duration = qjs_num_prop(ctx, argv[4], "duration", 0);
	tm.iterations = qjs_num_prop(ctx, argv[4], "iterations", 1);
	tm.iteration_start = qjs_num_prop(ctx, argv[4], "iterationStart", 0);
	tm.rate = qjs_num_prop(ctx, argv[4], "playbackRate", 1);
	tm.direction = (int) qjs_num_prop(ctx, argv[4], "direction", 0);
	tm.fill = (int) qjs_num_prop(ctx, argv[4], "fill", 0);
	v = JS_GetPropertyStr(ctx, argv[4], "easing");
	if (JS_IsString(v))
		easing = JS_ToCString(ctx, v);
	JS_FreeValue(ctx, v);
	tm.easing = easing;
	id = onyx_anim_create(t->htmlc, el, css, off, eas, (int) n, &tm);
done:
	for (k = 0; k < n && css != NULL; k++) {
		if (css[k] != NULL)
			JS_FreeCString(ctx, css[k]);
		if (eas != NULL && eas[k] != NULL)
			JS_FreeCString(ctx, eas[k]);
	}
	if (easing != NULL)
		JS_FreeCString(ctx, easing);
	free(css);
	free(eas);
	free(off);
	return JS_NewInt32(ctx, id);
}

/** animCtl(id, op, arg): play 0, pause 1, cancel 2, finish 3, reverse 4, currentTime 5,
 * playbackRate 6, startTime 7; false if the animation is gone */
static JSValue n_anim_ctl(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	int id = 0, op = 0;
	double arg = 0;

	if (t->htmlc == NULL || argc < 2)
		return JS_FALSE;
	JS_ToInt32(ctx, &id, argv[0]);
	JS_ToInt32(ctx, &op, argv[1]);
	if (argc > 2)
		JS_ToFloat64(ctx, &arg, argv[2]);
	return JS_NewBool(ctx, onyx_anim_control(t->htmlc, id, (enum onyx_anim_op) op, arg));
}

/** animInfo(id): [currentTime, startTime, rate, endTime, progress, iteration, kind,
 * playState, name, node, delay, duration, iterations, endDelay, direction, fill] (times in
 * ms, NaN unresolved, the start time on performance.now()'s clock), null if gone */
static JSValue n_anim_info(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	struct onyx_anim_info in;
	int id = 0, k = 0;
	JSValue a;

	if (t->htmlc == NULL || argc < 1)
		return JS_NULL;
	JS_ToInt32(ctx, &id, argv[0]);
	if (!onyx_anim_info(t->htmlc, id, &in))
		return JS_NULL;
	a = JS_NewArray(ctx);
	JS_SetPropertyUint32(ctx, a, k++, JS_NewFloat64(ctx, in.current_time));
	JS_SetPropertyUint32(ctx, a, k++, JS_NewFloat64(ctx, in.start_time));
	JS_SetPropertyUint32(ctx, a, k++, JS_NewFloat64(ctx, in.rate));
	JS_SetPropertyUint32(ctx, a, k++, JS_NewFloat64(ctx, in.end_time));
	JS_SetPropertyUint32(ctx, a, k++, JS_NewFloat64(ctx, in.progress));
	JS_SetPropertyUint32(ctx, a, k++, JS_NewFloat64(ctx, in.iteration));
	JS_SetPropertyUint32(ctx, a, k++, JS_NewInt32(ctx, in.kind));
	JS_SetPropertyUint32(ctx, a, k++, JS_NewString(ctx, in.play_state));
	JS_SetPropertyUint32(ctx, a, k++, JS_NewString(ctx, in.name != NULL ? in.name : ""));
	JS_SetPropertyUint32(ctx, a, k++, in.node != NULL ? qjs_wrap(t, in.node) : JS_NULL);
	JS_SetPropertyUint32(ctx, a, k++, JS_NewFloat64(ctx, in.delay));
	JS_SetPropertyUint32(ctx, a, k++, JS_NewFloat64(ctx, in.duration));
	JS_SetPropertyUint32(ctx, a, k++, JS_NewFloat64(ctx, in.iterations));
	JS_SetPropertyUint32(ctx, a, k++, JS_NewFloat64(ctx, in.end_delay));
	JS_SetPropertyUint32(ctx, a, k++, JS_NewInt32(ctx, in.direction));
	JS_SetPropertyUint32(ctx, a, k++, JS_NewInt32(ctx, in.fill));
	return a;
}

/** animList(el | null): the ids of the element's (the document's) animations */
static JSValue n_anim_list(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	dom_node *el = argc > 0 && !JS_IsNull(argv[0]) && !JS_IsUndefined(argv[0]) ?
			qjs_node(argv[0]) : NULL;
	int ids[512], n, k;
	JSValue a = JS_NewArray(ctx);

	if (t->htmlc == NULL)
		return a;
	if (argc > 0 && !JS_IsNull(argv[0]) && !JS_IsUndefined(argv[0]) && el == NULL)
		return a;
	n = onyx_anim_list(t->htmlc, el, ids, 512);
	for (k = 0; k < n; k++)
		JS_SetPropertyUint32(ctx, a, k, JS_NewInt32(ctx, ids[k]));
	return a;
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
	if (ctl == NULL && qjs_is_tag(n, "input")) {
		/* Onyx: no control (not boxed): the DOM's value (libdom keeps the value
		 * a script set apart from the value attribute) */
		dom_string *v = NULL;

		if (dom_html_input_element_get_value((dom_html_input_element *) n, &v) ==
				DOM_NO_ERR && v != NULL)
			return qjs_str(ctx, v);
		return JS_NULL;
	}
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
	/* Onyx: no control (not boxed): the DOM's checkedness, which the control
	 * starts from */
	if (qjs_is_tag(n, "input")) {
		bool c = false;
		if (dom_html_input_element_get_checked((dom_html_input_element *) n,
				&c) == DOM_NO_ERR)
			return JS_NewBool(ctx, c);
	}
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

/* Onyx: an element's checkedness / selectedness changed: its :checked restyles (an
 * attribute change's marks) */
static void qjs_state_restyle(jsthread *t, dom_node *n)
{
	if (t->htmlc != NULL && !t->closed) {
		onyx_restyle_attr_changed(t->htmlc, n);
		html_script_mutation(t->htmlc, n, true);
	}
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
			qjs_state_restyle(t, n);
			QJS_DIRTY(t);	/* (:checked) */
		}
		return JS_UNDEFINED;
	}
	o = qjs_option(t, n, &ctl);
	if (o != NULL) {
		qjs_select_option(t, ctl, o, on);
		qjs_state_restyle(t, n);
		return JS_UNDEFINED;
	}
	/* no control yet: the DOM's state, which the control will start from */
	if (qjs_is_tag(n, "option"))
		dom_html_option_element_set_selected((dom_html_option_element *) n, on);
	else
		dom_html_input_element_set_checked((dom_html_input_element *) n, on);
	qjs_state_restyle(t, n);
	QJS_DIRTY(t);
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

	if (t->url_override != NULL)	/* Onyx: history.pushState / replaceState */
		url = t->url_override;
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
	if (qjs_debug)	/* Onyx: which script moved the page away (NS_JSDEBUG) */
		fprintf(stderr, "JS navigate %s\n", s);
	JS_FreeCString(ctx, s);
	if (url != NULL && t->bw != NULL && !t->closed) {
		browser_window_navigate(t->bw, url, base, BW_NAVIGATE_HISTORY,
				NULL, NULL, NULL);
	}
	if (url != NULL)
		nsurl_unref(url);
	return JS_UNDEFINED;
}

/** download(url | bytes, name[, mime]): Onyx (docs/06 §38) -- <a download>'s default action
 * from a script's click(): an address saved as a download (its name the attribute's), or a
 * blob:'s bytes (html5.js) handed to the frontend, which asks where to save them */
static JSValue n_download(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	const char *name = argc > 1 ? JS_ToCString(ctx, argv[1]) : NULL;
	const char *mime = argc > 2 && JS_IsString(argv[2]) ? JS_ToCString(ctx, argv[2]) : NULL;

	(void) this_val;
	if (t->bw == NULL || t->closed || argc < 1) {
		/* (nothing to save into) */
	} else if (JS_IsString(argv[0])) {
		nsurl *base = t->htmlc != NULL ? content_get_url(&t->htmlc->base) : NULL;
		nsurl *url = NULL;
		const char *s = JS_ToCString(ctx, argv[0]);
		if (s != NULL) {
			if (base != NULL)
				nsurl_join(base, s, &url);
			else
				nsurl_create(s, &url);
			JS_FreeCString(ctx, s);
		}
		if (url != NULL) {
			download_onyx_hint(name);
			browser_window_navigate(t->bw, url, base, BW_NAVIGATE_DOWNLOAD,
					NULL, NULL, NULL);
			download_onyx_hint(NULL);
			nsurl_unref(url);
		}
	} else if (onyx_download_bytes_hook != NULL) {
		size_t size = 0, off = 0, len = 0, bpe = 0;
		uint8_t *p = JS_GetArrayBuffer(ctx, &size, argv[0]);
		if (p != NULL) {
			off = 0;
			len = size;
		} else {
			JSValue buf;
			JS_FreeValue(ctx, JS_GetException(ctx));
			buf = JS_GetTypedArrayBuffer(ctx, argv[0], &off, &len, &bpe);
			if (JS_IsException(buf)) {
				JS_FreeValue(ctx, JS_GetException(ctx));
			} else {
				p = JS_GetArrayBuffer(ctx, &size, buf);
				JS_FreeValue(ctx, buf);
				if (p == NULL || off + len > size) {
					JS_FreeValue(ctx, JS_GetException(ctx));
					p = NULL;
				}
			}
		}
		if (p != NULL)
			onyx_download_bytes_hook(p + off, len, name, mime,
					t->htmlc != NULL ? nsurl_access(content_get_url(
						&t->htmlc->base)) : "");
	}
	if (name != NULL)
		JS_FreeCString(ctx, name);
	if (mime != NULL)
		JS_FreeCString(ctx, mime);
	return JS_UNDEFINED;
}

static JSValue n_reload(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);

	if (qjs_debug)
		fprintf(stderr, "JS reload\n");
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

/* Onyx: parsing(): whether this window's document is still parsed (document.write
 * inserts into the parser; after it, document.open() starts a new document) */
static JSValue n_parsing(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);

	return JS_NewBool(ctx, t->htmlc != NULL && t->htmlc->parser != NULL);
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

/* Onyx: logOn(): whether console output goes anywhere (NS_JSDEBUG / jsdebug, or NetSurf's
 * verbose log) -- console.* formats its arguments only then: Vue's development build passes
 * whole component trees to console.warn, and making text of them took browserscore.dev
 * minutes on the Pi, for a log nobody reads */
/* Onyx: mediaMatch(query, width, height): [matches, number of queries, mask of the queries
 * libcss could not read] -- libcss parses and evaluates the media query list for the page's
 * media (the viewport: width x height CSS px until the page is laid out); null without a
 * page. matchMedia answers from it. */
static JSValue n_media_match(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	html_content *c = t->htmlc;
	uint32_t n = 0, invalid = 0;
	int32_t w = 0, h = 0;
	bool match = false, ok;
	css_media media;
	css_unit_ctx unit;
	const char *q;
	size_t len;
	JSValue a;

	if (c == NULL || argc < 1)
		return JS_NULL;
	media = c->media;
	memcpy(&unit, &c->unit_len_ctx, sizeof unit);	/* (a const member: no assignment) */
	if (argc > 2 && JS_ToInt32(ctx, &w, argv[1]) == 0 && JS_ToInt32(ctx, &h, argv[2]) == 0 &&
	    (media.width == 0 || media.height == 0) && w > 0 && h > 0) {
		media.width = unit.viewport_width = INTTOFIX(w);
		media.height = unit.viewport_height = INTTOFIX(h);
	}
	if (unit.device_dpi == 0)
		unit.device_dpi = nscss_screen_dpi;
	if (unit.font_size_default == 0)
		unit.font_size_default = INTTOFIX(16);
	q = JS_ToCStringLen(ctx, &len, argv[0]);
	if (q == NULL)
		return JS_EXCEPTION;
	ok = nscss_media_match(c->select_ctx, &media, &unit, q, len, &match, &n, &invalid);
	JS_FreeCString(ctx, q);
	if (!ok)
		return JS_NULL;
	a = JS_NewArray(ctx);
	JS_SetPropertyUint32(ctx, a, 0, JS_NewBool(ctx, match));
	JS_SetPropertyUint32(ctx, a, 1, JS_NewUint32(ctx, n));
	JS_SetPropertyUint32(ctx, a, 2, JS_NewUint32(ctx, invalid));
	return a;
}

/* Onyx: setState(n, state, on): an element's state only the scripts know, for the style
 * sheets' pseudo-classes (css/select.h NSCSS_STATE_*: 1 :popover-open, 2 :modal); the page
 * laid out again */
static JSValue n_set_state(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	uint32_t state = 0;
	QJS_NODE_ARG(n, 0);

	if (argc < 3 || JS_ToUint32(ctx, &state, argv[1]) != 0)
		return JS_UNDEFINED;
	nscss_node_state_set(n, state, JS_ToBool(ctx, argv[2]));
	if (t->htmlc != NULL) {
		/* (Onyx: its kept style selection made again, as for an attribute) */
		onyx_restyle_attr_changed(t->htmlc, n);
		html_script_mutation(t->htmlc, n, true);
	}
	QJS_DIRTY(t);
	return JS_UNDEFINED;
}

static JSValue n_log_on(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	return JS_NewBool(ctx, qjs_debug || verbose_log);
}

static JSValue n_now(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	return JS_NewFloat64(ctx, (double) qjs_now_ms());
}

static JSValue n_user_agent(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	/* (the HTTP requests' too -- Onyx: the page's site's, desktop or mobile) */
	jsthread *t = QJS_T(ctx);
	nsurl *url = t != NULL && t->htmlc != NULL ? content_get_url(&t->htmlc->base) : NULL;
	lwc_string *h = url != NULL ? nsurl_get_component(url, NSURL_HOST) : NULL;
	JSValue r = JS_NewString(ctx, user_agent_for_host(h != NULL ? lwc_string_data(h) : NULL));

	if (h != NULL)
		lwc_string_unref(h);
	return r;
}


/* Onyx (docs/06 §32): document.hidden / visibilityState -- the window minimised, on another
 * workspace, covered */
static JSValue n_view_hidden(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	return JS_NewBool(ctx, onyx_view_state == ONYX_VIEW_HIDDEN);
}

/* ---- natives: timers -------------------------------------------------------------------- */

static void qjs_timer_unlink(jsthread *t, struct qjs_timer *tm)
{
	struct qjs_timer **p;

	if (tm->prev != NULL)
		tm->prev->next = tm->next;
	else if (t->timers == tm)
		t->timers = tm->next;
	if (tm->next != NULL)
		tm->next->prev = tm->prev;
	tm->next = tm->prev = NULL;
	for (p = &t->tbuck[(unsigned) tm->id % QJS_TBUCKETS]; *p != NULL; p = &(*p)->hnext) {
		if (*p == tm) {
			*p = tm->hnext;
			break;
		}
	}
}

static void qjs_timer_link(jsthread *t, struct qjs_timer *tm)
{
	struct qjs_timer **b = &t->tbuck[(unsigned) tm->id % QJS_TBUCKETS];

	tm->prev = NULL;
	tm->next = t->timers;
	if (t->timers != NULL)
		t->timers->prev = tm;
	t->timers = tm;
	tm->hnext = *b;
	*b = tm;
}

/* Onyx (docs/06 §32): a timer's delay -- while the window is hidden (minimised, on another
 * workspace, covered), a repeating timer, or one a timer's callback sets, waits a second at
 * least (as a background tab's in Chrome): a page's polling costs nothing unseen */
static int qjs_in_timer;
#define QJS_HIDDEN_TIMER_MS 1000

static int qjs_timer_delay(int ms, bool repeat)
{
	if (onyx_view_state == ONYX_VIEW_HIDDEN && (repeat || qjs_in_timer > 0) &&
	    ms < QJS_HIDDEN_TIMER_MS)
		return QJS_HIDDEN_TIMER_MS;
	return ms;
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
		guit->misc->schedule(qjs_timer_delay(tm->ms, true), qjs_timer_fire, tm);
	} else {
		qjs_timer_unlink(t, tm);
		JS_FreeValue(t->ctx, tm->fn);
		free(tm);
	}
	qjs_in_timer++;
	r = qjs_call(t, fn, JS_UNDEFINED, 0, NULL, "timer");
	qjs_in_timer--;
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
	qjs_timer_link(t, tm);
	guit->misc->schedule(qjs_timer_delay(tm->ms, tm->repeat), qjs_timer_fire, tm);
	return JS_NewInt32(ctx, tm->id);
}

static JSValue n_clear_timer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	struct qjs_timer *tm;
	int id = 0;

	if (argc > 0)
		JS_ToInt32(ctx, &id, argv[0]);
	for (tm = t->tbuck[(unsigned) id % QJS_TBUCKETS]; tm != NULL; tm = tm->hnext) {
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
		qjs_timer_unlink(t, tm);
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
	JS_FreeValue(r->t->ctx, r->pcb);	/* (Onyx) */
	free(r);
}

/** The head of a request's response (status, statusText, url, headers), for dom.js. */
static JSValue qjs_req_head(JSContext *ctx, struct qjs_req *r)
{
	JSValue o = JS_NewObject(ctx), hs = JS_NewArray(ctx);
	const char *name, *value;
	size_t i;
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
	JS_SetPropertyStr(ctx, o, "status", JS_NewInt32(ctx, (int32_t) (code != 0 ? code : 200)));
	JS_SetPropertyStr(ctx, o, "statusText", JS_NewString(ctx, text));
	JS_SetPropertyStr(ctx, o, "url", JS_NewString(ctx, u != NULL ? nsurl_access(u) : ""));
	JS_SetPropertyStr(ctx, o, "headers", hs);
	return o;
}

/** The response of a finished request, for dom.js (its body null when it went as parts). */
static JSValue qjs_req_result(JSContext *ctx, struct qjs_req *r)
{
	JSValue o = qjs_req_head(ctx, r);
	const uint8_t *data;
	size_t size = 0;

	data = llcache_handle_get_source_data(r->handle, &size);
	if (r->flags & QJS_REQ_CHUNKS)
		JS_SetPropertyStr(ctx, o, "body", JS_NULL);
	else if (r->binary)
		JS_SetPropertyStr(ctx, o, "body", JS_NewArrayBufferCopy(ctx, data, size));
	else
		JS_SetPropertyStr(ctx, o, "body", JS_NewStringLen(ctx, (const char *) data, size));
	return o;
}

/* Onyx: pcb(kind, value) as the response comes -- an abort from the script waits till it
 * returns (the llcache handle is in its callback); true: the request is gone */
static bool qjs_req_progress(struct qjs_req *r, int kind, JSValue v)
{
	jsthread *t = r->t;
	JSContext *ctx = t->ctx;
	JSValue args[2], res, fn = JS_DupValue(ctx, r->pcb);

	args[0] = JS_NewInt32(ctx, kind);
	args[1] = v;
	r->in_cb = true;
	qjs_enter(t);
	res = JS_Call(ctx, fn, JS_UNDEFINED, 2, args);
	if (JS_IsException(res))
		qjs_report(ctx, "request");
	JS_FreeValue(ctx, res);
	JS_FreeValue(ctx, v);
	JS_FreeValue(ctx, fn);
	r->in_cb = false;
	if (r->dead) {				/* (aborted meanwhile: unlinked already) */
		qjs_req_free(r, true);
		qjs_leave(t);
		return true;
	}
	qjs_leave(t);
	return false;
}

/* Onyx: the parts of the body not given yet, as an ArrayBuffer */
static JSValue qjs_req_rest(JSContext *ctx, struct qjs_req *r)
{
	size_t size = 0;
	const uint8_t *data = llcache_handle_get_source_data(r->handle, &size);
	JSValue v;

	if (data == NULL || size <= r->given)
		return JS_NewArrayBufferCopy(ctx, (const uint8_t *) "", 0);
	v = JS_NewArrayBufferCopy(ctx, data + r->given, size - r->given);
	r->given = size;
	return v;
}

static nserror qjs_req_cb(llcache_handle *handle, const llcache_event *event, void *pw)
{
	struct qjs_req *r = pw;
	jsthread *t = r->t;
	JSValue args[2];

	(void) handle;
	/* Onyx: the head and the body's parts, as they come, to a script that asked for them */
	if ((event->type == LLCACHE_EVENT_HAD_HEADERS || event->type == LLCACHE_EVENT_HAD_DATA) &&
	    !t->closed && JS_IsFunction(t->ctx, r->pcb)) {
		if (event->type == LLCACHE_EVENT_HAD_HEADERS) {
			qjs_req_progress(r, 0, qjs_req_head(t->ctx, r));
		} else if (r->flags & QJS_REQ_CHUNKS) {
			qjs_req_progress(r, 1, qjs_req_rest(t->ctx, r));
		} else if (r->flags & QJS_REQ_COUNTS) {
			size_t size = 0;
			llcache_handle_get_source_data(r->handle, &size);
			qjs_req_progress(r, 2, JS_NewFloat64(t->ctx, (double) size));
		}
		return NSERROR_OK;
	}
	if (event->type != LLCACHE_EVENT_DONE && event->type != LLCACHE_EVENT_ERROR)
		return NSERROR_OK;		/* (progress, redirects: the end only) */

	qjs_req_unlink(t, r);
	if (t->closed) {			/* (the document went meanwhile) */
		qjs_req_free(r, false);
		return NSERROR_OK;
	}
	/* Onyx: a body given as parts: its last part first */
	if (event->type == LLCACHE_EVENT_DONE && (r->flags & QJS_REQ_CHUNKS) &&
	    JS_IsFunction(t->ctx, r->pcb)) {
		size_t size = 0;
		llcache_handle_get_source_data(r->handle, &size);
		if (size > r->given) {
			/* (no abort can reach it now: it is unlinked) */
			qjs_req_progress(r, 1, qjs_req_rest(t->ctx, r));
			if (t->closed) {
				qjs_req_free(r, false);
				return NSERROR_OK;
			}
		}
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

/** request(method, url, body | null, [ "Name: value", ... ], binary, cb(error, response)
 *  [, pcb(kind, value), flags]) -> id, or -1: a bad URL (the caller reports a network error).
 *  (Onyx: pcb and flags, the response as it comes: see struct qjs_req) */
static JSValue n_request(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	const char *method, *urls, *body = NULL;
	nsurl *base, *url = NULL, *referer;
	llcache_post_data post;
	char **hv = NULL;
	uint32_t nh = 0, i, k = 0;
	struct qjs_req *r;
	nserror err;
	bool get;

	(void) this_val;
	if (argc < 6 || t->closed || (t->htmlc == NULL && !t->worker) ||
	    !JS_IsFunction(ctx, argv[5]))
		return JS_NewInt32(ctx, -1);
	if (t->htmlc != NULL) {
		base = t->htmlc->base_url != NULL ? t->htmlc->base_url : content_get_url(&t->htmlc->base);
		referer = content_get_url(&t->htmlc->base);
	} else {
		base = referer = t->url_override;	/* (Onyx: a worker's: its script's URL) */
	}
	if (base == NULL)
		return JS_NewInt32(ctx, -1);
	method = JS_ToCString(ctx, argv[0]);
	urls = JS_ToCString(ctx, argv[1]);
	if (method == NULL || urls == NULL) {
		JS_FreeCString(ctx, method);
		JS_FreeCString(ctx, urls);
		return JS_NewInt32(ctx, -1);
	}
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
	hv = calloc(nh + 3, sizeof(char *));
	get = strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD") == 0;
	if (hv != NULL)
		hv[k++] = strdup("X-Onyx-Dest: empty");	/* (Onyx: fetch / XHR, Fetch Metadata) */
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
	r->pcb = JS_UNDEFINED;			/* (Onyx: the response as it comes) */
	if (argc > 6 && JS_IsFunction(ctx, argv[6]))
		r->pcb = JS_DupValue(ctx, argv[6]);
	if (argc > 7)
		JS_ToInt32(ctx, &r->flags, argv[7]);

	/* a body (or a method with one): as a POST's url-encoded data -- any text (not NUL) */
	post.type = LLCACHE_POST_URL_ENCODED;
	post.data.urlenc = (char *) (body != NULL ? body : "");
	err = llcache_handle_retrieve_ex(url,
		LLCACHE_RETRIEVE_FORCE_FETCH | LLCACHE_RETRIEVE_NO_ERROR_PAGES,
		referer, (!get || body != NULL) ? &post : NULL,
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
			if (r->in_cb)		/* (Onyx: in its callback: freed once it returns) */
				r->dead = true;
			else
				qjs_req_free(r, true);
			break;
		}
	}
	return JS_UNDEFINED;
}

/* Onyx: the request of an id still in flight (not aborted) */
static struct qjs_req *qjs_req_find(jsthread *t, JSValueConst v)
{
	int32_t id = 0;
	struct qjs_req *r;

	if (JS_ToInt32(t->ctx, &id, v) < 0)
		return NULL;
	for (r = t->reqs; r != NULL; r = r->next)
		if (r->id == id && !r->dead)
			return r;
	return NULL;
}

/** requestMode(id, flags) -> (Onyx) the parts wanted from now (QJS_REQ_CHUNKS, _COUNTS); when
 *  the parts begin, the body come so far (an ArrayBuffer), else undefined */
static JSValue n_request_mode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	struct qjs_req *r = argc > 1 ? qjs_req_find(t, argv[0]) : NULL;
	int32_t flags = 0;
	bool begin;

	(void) this_val;
	if (r == NULL)
		return JS_UNDEFINED;
	JS_ToInt32(ctx, &flags, argv[1]);
	begin = (flags & QJS_REQ_CHUNKS) && !(r->flags & QJS_REQ_CHUNKS);
	r->flags = flags;
	return begin ? qjs_req_rest(ctx, r) : JS_UNDEFINED;
}

/** requestSoFar(id) -> (Onyx) the body come so far, as text (XHR's responseText while it
 *  loads), or null */
static JSValue n_request_so_far(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	struct qjs_req *r = argc > 0 ? qjs_req_find(t, argv[0]) : NULL;
	const uint8_t *data;
	size_t size = 0;

	(void) this_val;
	if (r == NULL || r->handle == NULL)
		return JS_NULL;
	data = llcache_handle_get_source_data(r->handle, &size);
	return JS_NewStringLen(ctx, data != NULL ? (const char *) data : "", data != NULL ? size : 0);
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
		if (r->in_cb)			/* (Onyx: in its callback: freed once it returns) */
			r->dead = true;
		else
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

/* ---- Onyx: HTML5 natives (namespaces, templates, documents parsed) ----------------------
 *
 * nsURI(n): an element's namespace URI; lname(n): its local name (lower case for HTML);
 * qname(n): its qualified name as the DOM's tagName wants it (upper case for HTML);
 * attrsNS(n): [[qualified name, value, namespace, local name]...]; createNS(ns, qname);
 * attrNS(n, ns, local); setAttrNS(n, ns, qname, value); removeAttrNS(n, ns, local);
 * templateContent(n): a template's contents; parseDocument(html): a new document
 * (DOMParser), parsed by the same parser. */

/** Onyx (docs/06 §43): the XML kind of a node's document (0: HTML) */
static int qjs_doc_kind(dom_node *n)
{
	dom_document *od = NULL;
	dom_node_type t = DOM_ELEMENT_NODE;
	int kind = 0;

	if (dom_node_get_node_type(n, &t) == DOM_NO_ERR && t == DOM_DOCUMENT_NODE)
		return dom_html_document_get_xml_kind((dom_html_document *) n);
	if (dom_node_get_owner_document(n, &od) == DOM_NO_ERR && od != NULL) {
		kind = dom_html_document_get_xml_kind((dom_html_document *) od);
		dom_node_unref(od);
	}
	return kind;
}

static bool qjs_is_html_ns(dom_node *n)
{
	dom_string *ns = NULL;
	bool r;
	dom_node_get_namespace(n, &ns);
	r = ns == NULL || dom_string_isequal(ns, dom_namespaces[DOM_NAMESPACE_HTML]);
	if (ns != NULL)
		dom_string_unref(ns);
	else if (qjs_doc_kind(n) != 0)
		r = false;	/* (Onyx: an XML document's element in no namespace) */
	return r;
}

static JSValue n_ns_uri(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_string *ns = NULL;
	dom_node_type type = DOM_ELEMENT_NODE;
	QJS_NODE_ARG(n, 0);
	dom_node_get_node_type(n, &type);
	if (type != DOM_ELEMENT_NODE)
		return JS_NULL;
	dom_node_get_namespace(n, &ns);
	if (ns == NULL)	/* createElement in an HTML document: the HTML namespace */
		return qjs_doc_kind(n) != 0 ? JS_NULL :	/* (Onyx: XML's none) */
			JS_NewString(ctx, "http://www.w3.org/1999/xhtml");
	return qjs_str(ctx, ns);
}

static JSValue qjs_name_case(JSContext *ctx, dom_node *n, bool upper)
{
	dom_string *s = NULL;
	char buf[128], *b = buf;
	size_t len, i;
	JSValue v;
	bool html = qjs_is_html_ns(n);

	dom_node_get_node_name(n, &s);
	if (s == NULL)
		return JS_NewString(ctx, "");
	len = dom_string_byte_length(s);
	if (html && upper) {
		/* (Onyx, docs/06 §43: an XHTML element of an XML document keeps its case) */
		dom_document *od = NULL;
		if (dom_node_get_owner_document(n, &od) == DOM_NO_ERR && od != NULL) {
			if (dom_html_document_get_xml_kind((dom_html_document *) od) != 0)
				html = false;
			dom_node_unref(od);
		}
	}
	if (!html) {
		v = JS_NewStringLen(ctx, dom_string_data(s), len);
		dom_string_unref(s);
		return v;
	}
	if (len >= sizeof(buf) && (b = malloc(len + 1)) == NULL) {
		dom_string_unref(s);
		return JS_NewString(ctx, "");
	}
	for (i = 0; i < len; i++) {
		char c = dom_string_data(s)[i];
		b[i] = upper ? ((c >= 'a' && c <= 'z') ? c - 32 : c) :
				((c >= 'A' && c <= 'Z') ? c + 32 : c);
	}
	v = JS_NewStringLen(ctx, b, len);
	if (b != buf)
		free(b);
	dom_string_unref(s);
	return v;
}

static JSValue n_lname(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QJS_NODE_ARG(n, 0);
	return qjs_name_case(ctx, n, false);
}

static JSValue n_qname(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QJS_NODE_ARG(n, 0);
	return qjs_name_case(ctx, n, true);
}

static JSValue n_attrs_ns(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
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
		dom_string *name = NULL, *value = NULL, *ns = NULL, *local = NULL;
		JSValue e;

		if (dom_namednodemap_item(map, i, &a) != DOM_NO_ERR || a == NULL)
			continue;
		dom_attr_get_name((dom_attr *) a, &name);
		dom_attr_get_value((dom_attr *) a, &value);
		dom_node_get_namespace(a, &ns);
		dom_node_get_local_name(a, &local);
		e = JS_NewArray(ctx);
		JS_SetPropertyUint32(ctx, e, 0, name ? qjs_str(ctx, name) : JS_NewString(ctx, ""));
		JS_SetPropertyUint32(ctx, e, 1, value ? qjs_str(ctx, value) : JS_NewString(ctx, ""));
		JS_SetPropertyUint32(ctx, e, 2, ns ? qjs_str(ctx, ns) : JS_NULL);
		JS_SetPropertyUint32(ctx, e, 3, local ? qjs_str(ctx, local) : JS_NULL);
		JS_SetPropertyUint32(ctx, arr, k++, e);
		dom_node_unref(a);
	}
	dom_namednodemap_unref(map);
	return arr;
}

/* a JS value as a dom_string, NULL for null / undefined / "" (a namespace) */
static dom_string *qjs_ns_arg(JSContext *ctx, JSValueConst v)
{
	dom_string *s;
	if (JS_IsNull(v) || JS_IsUndefined(v))
		return NULL;
	s = qjs_dstr(ctx, v);
	if (s != NULL && dom_string_byte_length(s) == 0) {
		dom_string_unref(s);
		return NULL;
	}
	return s;
}

static JSValue n_create_ns(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	dom_string *ns = qjs_ns_arg(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
	dom_string *qn = qjs_dstr(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
	dom_element *e = NULL;
	dom_exception err;
	JSValue v;

	if (qn == NULL) {
		if (ns != NULL)
			dom_string_unref(ns);
		return JS_NULL;
	}
	err = dom_document_create_element_ns(t->doc, ns, qn, &e);
	if (ns != NULL)
		dom_string_unref(ns);
	dom_string_unref(qn);
	if (err != DOM_NO_ERR || e == NULL)
		return JS_ThrowTypeError(ctx, err == DOM_NAMESPACE_ERR ? "NamespaceError" :
				"InvalidCharacterError");
	v = qjs_wrap(t, (dom_node *) e);
	dom_node_unref(e);
	return v;
}

static JSValue n_attr_ns(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_string *ns, *local, *v = NULL;
	QJS_NODE_ARG(n, 0);
	ns = qjs_ns_arg(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
	local = qjs_dstr(ctx, argc > 2 ? argv[2] : JS_UNDEFINED);
	if (local != NULL)
		dom_element_get_attribute_ns((dom_element *) n, ns, local, &v);
	if (ns != NULL)
		dom_string_unref(ns);
	if (local != NULL)
		dom_string_unref(local);
	return qjs_str(ctx, v);
}

static JSValue n_set_attr_ns(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_string *ns, *qn, *v;
	dom_exception err = DOM_NO_ERR;
	QJS_NODE_ARG(n, 0);
	ns = qjs_ns_arg(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
	qn = qjs_dstr(ctx, argc > 2 ? argv[2] : JS_UNDEFINED);
	v = qjs_dstr(ctx, argc > 3 ? argv[3] : JS_UNDEFINED);
	if (qn != NULL && v != NULL) {
		if (ns == NULL)
			err = dom_element_set_attribute((dom_element *) n, qn, v);
		else
			err = dom_element_set_attribute_ns((dom_element *) n, ns, qn, v);
		QJS_DIRTY(QJS_T(ctx));
	}
	if (ns != NULL)
		dom_string_unref(ns);
	if (qn != NULL)
		dom_string_unref(qn);
	if (v != NULL)
		dom_string_unref(v);
	if (err != DOM_NO_ERR)
		return JS_ThrowTypeError(ctx, err == DOM_NAMESPACE_ERR ? "NamespaceError" :
				"InvalidCharacterError");
	return JS_UNDEFINED;
}

static JSValue n_remove_attr_ns(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_string *ns, *local;
	QJS_NODE_ARG(n, 0);
	ns = qjs_ns_arg(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
	local = qjs_dstr(ctx, argc > 2 ? argv[2] : JS_UNDEFINED);
	if (local != NULL) {
		dom_element_remove_attribute_ns((dom_element *) n, ns, local);
		QJS_DIRTY(QJS_T(ctx));
		dom_string_unref(local);
	}
	if (ns != NULL)
		dom_string_unref(ns);
	return JS_UNDEFINED;
}

static JSValue n_template_content(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_document_fragment *f = NULL;
	JSValue v;
	QJS_NODE_ARG(n, 0);
	if (dom_hubbub_template_content((dom_element *) n, &f) != DOM_NO_ERR || f == NULL)
		return JS_NULL;
	v = qjs_wrap(QJS_T(ctx), (dom_node *) f);
	dom_node_unref(f);
	return v;
}

/* parseDocument(html): a new HTML document (DOMParser, createHTMLDocument), no scripts run */
static JSValue n_parse_document(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_hubbub_parser_params params;
	dom_hubbub_parser *parser = NULL;
	dom_document *doc = NULL;
	size_t len;
	const char *s = JS_ToCStringLen(ctx, &len, argc > 0 ? argv[0] : JS_UNDEFINED);
	JSValue v;

	if (s == NULL)
		return JS_EXCEPTION;
	memset(&params, 0, sizeof(params));
	params.enc = "UTF-8";
	params.fix_enc = true;
	params.enable_script = false;	/* (DOMParser: scripting disabled, <noscript> parsed) */
	if (dom_hubbub_parser_create(&params, &parser, &doc) != DOM_HUBBUB_OK) {
		JS_FreeCString(ctx, s);
		return JS_NULL;
	}
	dom_hubbub_parser_parse_chunk(parser, (const uint8_t *) s, len);
	dom_hubbub_parser_completed(parser);
	dom_hubbub_parser_destroy(parser);
	JS_FreeCString(ctx, s);
	v = qjs_wrap(QJS_T(ctx), (dom_node *) doc);
	dom_node_unref(doc);
	return v;
}

/* createDocument(): a new empty HTML document (no html element: DOMParser's XML documents) */
static JSValue n_create_document(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_document *doc = NULL;
	/* Onyx: createDocument's doctype (createDoctype's: no parent yet), its first child */
	dom_node *dt = argc > 0 ? qjs_node(argv[0]) : NULL;
	dom_node_type t = DOM_ELEMENT_NODE;
	JSValue v;

	if (dt != NULL && (dom_node_get_node_type(dt, &t) != DOM_NO_ERR ||
			t != DOM_DOCUMENT_TYPE_NODE))
		dt = NULL;
	if (dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL,
			(struct dom_document_type *) dt, NULL, NULL, &doc) != DOM_NO_ERR ||
	    doc == NULL)
		return JS_NULL;
	v = qjs_wrap(QJS_T(ctx), (dom_node *) doc);
	dom_node_unref(doc);
	return v;
}

/* Onyx: createDoctype(name, publicId, systemId): a DocumentType node (no document yet) */
static JSValue n_create_doctype(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const char *name = JS_ToCString(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
	const char *pub = JS_ToCString(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
	const char *sys = JS_ToCString(ctx, argc > 2 ? argv[2] : JS_UNDEFINED);
	struct dom_document_type *dt = NULL;
	JSValue v = JS_NULL;

	if (name != NULL && pub != NULL && sys != NULL &&
	    dom_implementation_create_document_type(name, pub, sys, &dt) == DOM_NO_ERR &&
	    dt != NULL) {
		v = qjs_wrap(QJS_T(ctx), (dom_node *) dt);
		dom_node_unref(dt);
	}
	JS_FreeCString(ctx, name);
	JS_FreeCString(ctx, pub);
	JS_FreeCString(ctx, sys);
	return v;
}

/* Onyx: doctypeIds(dt): [publicId, systemId] */
static JSValue n_doctype_ids(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	JSValue arr = JS_NewArray(ctx);
	dom_string *p = NULL, *s = NULL;
	QJS_NODE_ARG(n, 0);

	dom_document_type_get_public_id((dom_document_type *) n, &p);
	dom_document_type_get_system_id((dom_document_type *) n, &s);
	JS_SetPropertyUint32(ctx, arr, 0, p != NULL ? qjs_str(ctx, p) : JS_NewString(ctx, ""));
	JS_SetPropertyUint32(ctx, arr, 1, s != NULL ? qjs_str(ctx, s) : JS_NewString(ctx, ""));
	return arr;
}

/* createIn(doc, kind, a, b): a node of another document -- kind "element" (name a),
 * "elementNS" (namespace a, qualified name b), "text" / "comment" (data a), "fragment" */
static JSValue n_create_in(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_node *r = NULL;
	dom_exception err = DOM_NO_ERR;
	const char *kind;
	JSValue v;
	QJS_NODE_ARG(d, 0);

	kind = JS_ToCString(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
	if (kind == NULL)
		return JS_EXCEPTION;
	if (strcmp(kind, "fragment") == 0) {
		err = dom_document_create_document_fragment((dom_document *) d,
				(dom_document_fragment **) &r);
	} else {
		dom_string *a = qjs_ns_arg(ctx, argc > 2 ? argv[2] : JS_UNDEFINED);
		if (strcmp(kind, "elementNS") == 0) {
			dom_string *b = qjs_dstr(ctx, argc > 3 ? argv[3] : JS_UNDEFINED);
			if (b != NULL) {
				err = dom_document_create_element_ns((dom_document *) d, a, b,
						(dom_element **) &r);
				dom_string_unref(b);
			}
		} else {
			if (a == NULL)
				dom_string_create((const uint8_t *) "", 0, &a);
			if (strcmp(kind, "element") == 0)
				err = dom_document_create_element((dom_document *) d, a,
						(dom_element **) &r);
			else if (strcmp(kind, "text") == 0)
				err = dom_document_create_text_node((dom_document *) d, a,
						(dom_text **) &r);
			else if (strcmp(kind, "comment") == 0)
				err = dom_document_create_comment((dom_document *) d, a,
						(dom_comment **) &r);
		}
		if (a != NULL)
			dom_string_unref(a);
	}
	JS_FreeCString(ctx, kind);
	if (err != DOM_NO_ERR || r == NULL)
		return JS_ThrowTypeError(ctx, "InvalidCharacterError");
	v = qjs_wrap(QJS_T(ctx), r);
	dom_node_unref(r);
	return v;
}

/* adopt(doc, node): node (out of its tree) and its subtree made doc's, in place (Onyx) */
static JSValue n_adopt(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx), *from;
	dom_document *cd = NULL;
	QJS_NODE_ARG(d, 0);
	QJS_NODE_ARG(n, 1);
	if (dom_node_get_owner_document(n, &cd) != DOM_NO_ERR)
		cd = NULL;
	if (dom_document_onyx_adopt((dom_document *) d, n) != DOM_NO_ERR) {
		if (cd != NULL)
			dom_node_unref(cd);
		return JS_ThrowTypeError(ctx, "NotSupportedError");
	}
	/* (Onyx: its wrappers to the adopting document's realm: qjs_wraps_take) */
	from = cd != NULL ? qjs_thread_of_doc(t, cd) : NULL;
	qjs_wraps_take(from != NULL ? from : t, qjs_thread_of_doc(t, (dom_document *) d), n);
	if (cd != NULL)
		dom_node_unref(cd);
	QJS_DIRTY(t);
	return JS_DupValue(ctx, argv[1]);
}

/* importTo(doc, node, deep): a copy of node owned by doc */
static JSValue n_import_to(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_node *r = NULL;
	JSValue v;
	QJS_NODE_ARG(d, 0);
	QJS_NODE_ARG(n, 1);
	if (dom_document_import_node((dom_document *) d, n,
			argc > 2 && JS_ToBool(ctx, argv[2]), &r) != DOM_NO_ERR || r == NULL)
		return JS_ThrowTypeError(ctx, "NotSupportedError");
	v = qjs_wrap(QJS_T(ctx), r);
	dom_node_unref(r);
	return v;
}

/* ownerDoc(n): the document a node belongs to */
static JSValue n_owner_doc(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_document *d = NULL;
	JSValue v;
	QJS_NODE_ARG(n, 0);
	if (dom_node_get_owner_document(n, &d) != DOM_NO_ERR || d == NULL)
		return JS_NULL;
	v = qjs_wrap(QJS_T(ctx), (dom_node *) d);
	dom_node_unref(d);
	return v;
}

/* setURL(url): the document's URL becomes url (history.pushState / replaceState: same origin,
 * checked by html5.js) -- location and the address bar show it; no navigation */
static JSValue n_set_url(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	nsurl *base, *url = NULL;
	const char *s = JS_ToCString(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);

	if (s == NULL)
		return JS_EXCEPTION;
	base = t->url_override != NULL ? t->url_override :
		t->htmlc != NULL ? content_get_url(&t->htmlc->base) : NULL;
	if (base != NULL)
		nsurl_join(base, s, &url);
	else
		nsurl_create(s, &url);
	JS_FreeCString(ctx, s);
	if (url == NULL)
		return JS_FALSE;
	if (t->url_override != NULL)
		nsurl_unref(t->url_override);
	t->url_override = url;
	if (t->bw != NULL && !t->closed && t->bw->parent == NULL && t->bw->window != NULL)
		guit->window->set_url(t->bw->window, url);
	return JS_TRUE;
}

/* ceHook(fn): fn(element) is called for each element inserted outside the scripts (the
 * parser): html5.js upgrades the custom elements there (Onyx) */
static JSValue n_ce_hook(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);

	JS_FreeValue(ctx, t->ce_hook);
	t->ce_hook = argc > 0 && JS_IsFunction(ctx, argv[0]) ?
		JS_DupValue(ctx, argv[0]) : JS_UNDEFINED;
	return JS_UNDEFINED;
}

/* the elements js_handle_new_element kept, shown to html5.js' hook (in tree order) */
static void qjs_ce_flush(jsthread *t)
{
	dom_node **list = t->ce_pending;
	int n = t->ce_npending, i;

	t->ce_pending = NULL;
	t->ce_npending = t->ce_cappending = 0;
	guit->misc->schedule(-1, qjs_ce_later, t);
	for (i = 0; i < n; i++) {
		if (!t->closed && JS_IsFunction(t->ctx, t->ce_hook)) {
			JSValue fn = JS_DupValue(t->ctx, t->ce_hook);
			JSValue w = qjs_wrap(t, list[i]);
			JSValue r;

			t->in_use++;	/* (the jobs run when the caller leaves) */
			r = JS_Call(t->ctx, fn, JS_UNDEFINED, 1, (JSValueConst *) &w);
			if (JS_IsException(r))
				qjs_report(t->ctx, "customElements");
			t->in_use--;
			JS_FreeValue(t->ctx, r);
			JS_FreeValue(t->ctx, w);
			JS_FreeValue(t->ctx, fn);
		}
		dom_node_unref(list[i]);
	}
	free(list);
}

static void qjs_ce_later(void *p)
{
	jsthread *t = p;

	if (t->ce_npending > 0 && t->in_use == 0) {
		qjs_enter(t);	/* (flushes) */
		qjs_leave(t);
	}
}

/* ---- Onyx: shadow DOM ---------------------------------------------------------------------
 *
 * The shadow roots are libdom document fragments kept on their hosts (the hubbub binding's
 * dom_onyx_attach_shadow; the parser's <template shadowrootmode> too); NetSurf builds the
 * boxes from the flat tree and scopes the styles (html/onyx_shadow.c). html5.js is the DOM.
 *
 * attachShadow(host, flags): its new shadow root (DOM_ONYX_SHADOW_* flags), null if the
 * host cannot have one or has one; shadowRoot(host): its shadow root (any mode) or null;
 * shadowHost(node): the host if node is a shadow root, else null; shadowFlags(root);
 * hasShadow(): whether the document has shadow roots; shadowProto(proto): the ShadowRoot
 * prototype the wrappers of shadow roots get; shadowSheets(root, [texts]): the texts of its
 * adopted style sheets (adoptedStyleSheets).
 */
static JSValue n_attach_shadow(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	dom_document_fragment *f = NULL;
	dom_node_type type = 0;
	int32_t flags = 0;
	JSValue v;
	QJS_NODE_ARG(n, 0);

	if (dom_node_get_node_type(n, &type) != DOM_NO_ERR || type != DOM_ELEMENT_NODE)
		return JS_NULL;
	if (argc > 1)
		JS_ToInt32(ctx, &flags, argv[1]);
	if (dom_onyx_attach_shadow((dom_element *) n, (unsigned int) flags & 0x3f, &f) !=
			DOM_NO_ERR || f == NULL)
		return JS_NULL;
	v = qjs_wrap(t, (dom_node *) f);
	dom_node_unref(f);
	QJS_DIRTY(t);	/* (its host now shows its shadow tree) */
	return v;
}

static JSValue n_shadow_root(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	QJS_NODE_ARG(n, 0);
	return qjs_wrap(QJS_T(ctx), dom_onyx_shadow_root(n));
}

static JSValue n_shadow_host(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	QJS_NODE_ARG(n, 0);
	return qjs_wrap(QJS_T(ctx), dom_onyx_shadow_host(n));
}

static JSValue n_shadow_flags(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	QJS_NODE_ARG(n, 0);
	return JS_NewInt32(ctx, (int32_t) dom_onyx_shadow_flags(n));
}

static JSValue n_has_shadow(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	return JS_NewBool(ctx, t->doc != NULL && dom_onyx_has_shadow(t->doc));
}

static JSValue n_shadow_proto(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);

	JS_FreeValue(ctx, t->shadow_proto);
	t->shadow_proto = argc > 0 && JS_IsObject(argv[0]) ? JS_DupValue(ctx, argv[0]) :
		JS_UNDEFINED;
	return JS_UNDEFINED;
}

static JSValue n_shadow_sheets(JSContext *ctx, JSValueConst this_val, int argc,
		JSValueConst *argv)
{
	jsthread *t = QJS_T(ctx);
	const char *texts[64];
	uint32_t n = 0, i, len = 0;
	JSValue lv;
	QJS_NODE_ARG(root, 0);

	if (dom_onyx_shadow_host(root) == NULL)
		return JS_UNDEFINED;
	if (argc > 1 && JS_IsArray(argv[1])) {
		lv = JS_GetPropertyStr(ctx, argv[1], "length");
		JS_ToUint32(ctx, &len, lv);
		JS_FreeValue(ctx, lv);
	}
	for (i = 0; i < len && n < 64; i++) {
		JSValue e = JS_GetPropertyUint32(ctx, argv[1], i);
		const char *s = JS_ToCString(ctx, e);
		JS_FreeValue(ctx, e);
		if (s != NULL)
			texts[n++] = s;
	}
	onyx_shadow_set_adopted(root, texts, n);
	for (i = 0; i < n; i++)
		JS_FreeCString(ctx, texts[i]);
	QJS_DIRTY(t);
	return JS_UNDEFINED;
}

static const JSCFunctionListEntry qjs_natives_shadow[] = {
	JS_CFUNC_DEF("attachShadow", 2, n_attach_shadow),
	JS_CFUNC_DEF("shadowRoot", 1, n_shadow_root),
	JS_CFUNC_DEF("shadowHost", 1, n_shadow_host),
	JS_CFUNC_DEF("shadowFlags", 1, n_shadow_flags),
	JS_CFUNC_DEF("hasShadow", 0, n_has_shadow),
	JS_CFUNC_DEF("shadowProto", 1, n_shadow_proto),
	JS_CFUNC_DEF("shadowSheets", 2, n_shadow_sheets),
};

static const JSCFunctionListEntry qjs_natives_html5[] = {
	JS_CFUNC_DEF("ceHook", 1, n_ce_hook),
	JS_CFUNC_DEF("setURL", 1, n_set_url),
	JS_CFUNC_DEF("createDocument", 0, n_create_document),
	JS_CFUNC_DEF("createIn", 4, n_create_in),
	JS_CFUNC_DEF("createDoctype", 3, n_create_doctype),	/* (Onyx) */
	JS_CFUNC_DEF("parsing", 0, n_parsing),
	JS_CFUNC_DEF("doctypeIds", 1, n_doctype_ids),
	JS_CFUNC_DEF("importTo", 3, n_import_to),
	JS_CFUNC_DEF("adopt", 2, n_adopt),
	JS_CFUNC_DEF("ownerDoc", 1, n_owner_doc),
	JS_CFUNC_DEF("nsURI", 1, n_ns_uri),
	JS_CFUNC_DEF("lname", 1, n_lname),
	JS_CFUNC_DEF("qname", 1, n_qname),
	JS_CFUNC_DEF("attrsNS", 1, n_attrs_ns),
	JS_CFUNC_DEF("createNS", 2, n_create_ns),
	JS_CFUNC_DEF("attrNS", 3, n_attr_ns),
	JS_CFUNC_DEF("setAttrNS", 4, n_set_attr_ns),
	JS_CFUNC_DEF("removeAttrNS", 3, n_remove_attr_ns),
	JS_CFUNC_DEF("templateContent", 1, n_template_content),
	JS_CFUNC_DEF("parseDocument", 1, n_parse_document),
};

static const JSCFunctionListEntry qjs_natives[] = {
	JS_CFUNC_DEF("type", 1, n_type),
	JS_CFUNC_DEF("name", 1, n_name),
	JS_CFUNC_DEF("parent", 1, n_parent),
	JS_CFUNC_DEF("first", 1, n_first),
	JS_CFUNC_DEF("last", 1, n_last),
	JS_CFUNC_DEF("next", 1, n_next),
	JS_CFUNC_DEF("prev", 1, n_prev),
	JS_CFUNC_DEF("children", 1, n_children),
	JS_CFUNC_DEF("treeGen", 0, n_tree_gen),		/* (Onyx) */
	JS_CFUNC_DEF("attrGen", 0, n_attr_gen),		/* (Onyx) */
	JS_CFUNC_DEF("document", 0, n_document),
	JS_CFUNC_DEF("value", 1, n_value),
	JS_CFUNC_DEF("setValue", 2, n_set_value),
	JS_CFUNC_DEF("text", 1, n_text),
	JS_CFUNC_DEF("setText", 2, n_set_text),
	JS_CFUNC_DEF("attr", 2, n_attr),
	JS_CFUNC_DEF("hasToken", 3, n_has_token),
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
	JS_CFUNC_DEF("currentScript", 0, n_current_script),
	JS_CFUNC_DEF("image", 1, n_image),
	JS_CFUNC_DEF("addFontFace", 5, n_add_font_face),
	JS_CFUNC_DEF("cssKept", 2, n_css_kept),
	JS_CFUNC_DEF("sheetText", 1, n_sheet_text),
	JS_CFUNC_DEF("moduleSource", 2, n_module_source),
	JS_CFUNC_DEF("moduleImports", 1, n_module_imports),
	JS_CFUNC_DEF("moduleRun", 2, n_module_run),
	JS_CFUNC_DEF("setHTML", 2, n_set_html),
	JS_CFUNC_DEF("descendants", 1, n_descendants),
	JS_CFUNC_DEF("nextElement", 2, n_next_element),
	JS_CFUNC_DEF("formValue", 1, n_form_value),
	JS_CFUNC_DEF("setFormValue", 2, n_set_form_value),
	JS_CFUNC_DEF("formChecked", 1, n_form_checked),
	JS_CFUNC_DEF("setFormChecked", 2, n_set_form_checked),
	JS_CFUNC_DEF("submit", 2, n_submit),
	JS_CFUNC_DEF("rect", 1, n_rect),
	JS_CFUNC_DEF("hitNode", 2, n_hit_node),
	JS_CFUNC_DEF("focusControl", 1, n_focus_control),
	JS_CFUNC_DEF("boxScroll", 1, n_box_scroll),
	JS_CFUNC_DEF("boxScrollTo", 3, n_box_scroll_to),
	JS_CFUNC_DEF("boxed", 1, n_boxed),
	JS_CFUNC_DEF("offset", 2, n_offset),
	JS_CFUNC_DEF("scroll", 0, n_scroll),
	JS_CFUNC_DEF("scrollTo", 2, n_scroll_to),
	JS_CFUNC_DEF("cstyle", 2, n_cstyle),
	JS_CFUNC_DEF("frame", 0, n_frame),
	JS_CFUNC_DEF("animate", 5, n_animate),
	JS_CFUNC_DEF("animCtl", 3, n_anim_ctl),
	JS_CFUNC_DEF("animInfo", 1, n_anim_info),
	JS_CFUNC_DEF("animList", 1, n_anim_list),
	JS_CFUNC_DEF("url", 0, n_url),
	JS_CFUNC_DEF("navigate", 1, n_navigate),
	JS_CFUNC_DEF("download", 3, n_download),
	JS_CFUNC_DEF("reload", 0, n_reload),
	JS_CFUNC_DEF("write", 1, n_write),
	JS_CFUNC_DEF("history", 1, n_history),
	JS_CFUNC_DEF("cookie", 0, n_cookie),
	JS_CFUNC_DEF("setCookie", 1, n_set_cookie),
	JS_CFUNC_DEF("log", 1, n_log),
	JS_CFUNC_DEF("logOn", 0, n_log_on),
	JS_CFUNC_DEF("setState", 3, n_set_state),
	JS_CFUNC_DEF("mediaMatch", 3, n_media_match),
	JS_CFUNC_DEF("now", 0, n_now),
	JS_CFUNC_DEF("userAgent", 0, n_user_agent),
	JS_CFUNC_DEF("viewHidden", 0, n_view_hidden),	/* Onyx: document.hidden */
	JS_CFUNC_DEF("timer", 3, n_timer),
	JS_CFUNC_DEF("clearTimer", 1, n_clear_timer),
	JS_CFUNC_DEF("request", 6, n_request),
	JS_CFUNC_DEF("requestMode", 2, n_request_mode),		/* (Onyx) */
	JS_CFUNC_DEF("requestSoFar", 1, n_request_so_far),	/* (Onyx) */
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
	if (getenv("NS_JSPROF") != NULL)
		qjs_prof_f = fopen(getenv("NS_JSPROF"), "w");
#ifdef ONYX_NS_DATAPATH
	{	/* Onyx: or the file SD:/apps/jet.app/jsdebug (no environment on the Pi) */
		FILE *f = fopen(ONYX_NS_DATAPATH "jsdebug", "r");
		if (f != NULL) {
			qjs_debug = true;
			fclose(f);
		}
	}
#endif
	NSLOG(netsurf, INFO, "JavaScript: QuickJS %s", JS_GetVersion());
	javascript_init();	/* the script types: text/javascript... */
}

/* exported interface documented in js.h */
void js_finalise(void)
{
}

#ifndef ONYX_HOST_SIM
#include "kapi.h"

/* Onyx: the JIT's code chunks (kapi v58: executable, the app's until it ends) */
static void *qjs_code_alloc(size_t size)
{
	return kapi_code_alloc(size);
}
#endif

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
	if (getenv("NS_SCRIPT_TIMEOUT") != NULL)	/* (Onyx: the PC bench's tests) */
		h->timeout = atoi(getenv("NS_SCRIPT_TIMEOUT"));
	h->refs = 1;	/* (Onyx: js_heap_share) */
	/* a script's recursion stopped (RangeError) past 4 MB of stack -- the Onyx app's is
	 * 8 MB (its app.txt: stack = 8M), NetSurf's own frames below the JS */
	JS_SetMaxStackSize(h->rt, 4 * 1024 * 1024);
#ifndef ONYX_HOST_SIM
	/* Onyx: the JIT (Choices: js_jit, the calls before a function is compiled; 0 off) in
	 * the kernel's executable memory */
	if (nsoption_int(js_jit) > 0)
		JS_SetJIT(h->rt, nsoption_int(js_jit), qjs_code_alloc);
#endif
	/* Onyx: a window's scripts take 384 MB at most -- past it an allocation throws
	 * (InternalError: out of memory) where it would stop the whole app on the Pi */
	JS_SetMemoryLimit(h->rt, 384 * 1024 * 1024);
	JS_SetInterruptHandler(h->rt, qjs_interrupt, h);
	JS_SetModuleLoaderFunc(h->rt, qjs_mod_normalize, qjs_mod_loader, NULL);	/* (Onyx) */
	JS_NewClassID(h->rt, &qjs_node_class);
	JS_NewClass(h->rt, qjs_node_class, &qjs_node_classdef);
	*heap = h;
	return NSERROR_OK;
}

static void qjs_heap_free(jsheap *h)
{
	JS_FreeRuntime(h->rt);
	while (h->zombies != NULL) {	/* (Onyx) */
		jsthread *z = h->zombies;
		h->zombies = z->hnext;
		free(z);
	}
	free(h);
}

/* exported interface documented in js.h */
jsheap *js_heap_share(jsheap *heap)
{
	if (heap != NULL) {
		heap->refs++;
		heap->shared = true;
	}
	return heap;
}

/* exported interface documented in js.h */
void js_destroyheap(jsheap *heap)
{
	if (heap == NULL)
		return;
	if (--heap->refs > 0)	/* (Onyx: still a frame's, js_heap_share) */
		return;
	if (heap->threads > 0) {
		heap->pending_destroy = true;
		return;
	}
	qjs_heap_free(heap);
}

/* Onyx: a prelude (dom.js, html5.js, canvas.js) is compiled once per process; its bytecode
 * is kept (in the process's heap) and read back in the next contexts -- several times faster
 * than parsing it again (the same as Intl, qjs_intl.h). The first context of a process reads
 * it from the code cache on the card (qjs_codecache.c: 0.5 s of parsing on the Pi at each
 * launch), or writes it there. Returns the prelude's value. */
JSValue qjs_eval_cached(JSContext *ctx, const char *src, size_t len, const char *name,
		uint8_t **bc, size_t *bclen)
{
	JSValue obj;

	if (*bc == NULL)
		*bc = qjs_cc_load(src, len, bclen);
	if (*bc == NULL) {
		obj = JS_Eval(ctx, src, len, name, JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
		if (JS_IsException(obj))
			return obj;
		uint8_t *b = JS_WriteObject(ctx, bclen, obj,
				JS_WRITE_OBJ_BYTECODE | JS_WRITE_OBJ_STRIP_SOURCE);
		if (b != NULL) {
			*bc = malloc(*bclen);
			if (*bc != NULL)
				memcpy(*bc, b, *bclen);
			qjs_cc_store(src, len, b, *bclen);
			js_free(ctx, b);
		}
	} else {
		obj = JS_ReadObject(ctx, *bc, *bclen, JS_READ_OBJ_BYTECODE);
		if (JS_IsException(obj))
			return obj;
	}
	return JS_EvalFunction(ctx, obj);
}

static uint8_t *qjs_dom_bc, *qjs_html5_bc;
static size_t qjs_dom_bc_len, qjs_html5_bc_len;

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
	t->modsrc = JS_NewObject(t->ctx);	/* (Onyx: ES modules) */
	t->modmissing = JS_NewArray(t->ctx);
	t->ce_hook = JS_UNDEFINED;	/* (Onyx: custom elements) */
	t->shadow_proto = JS_UNDEFINED;	/* (Onyx: shadow DOM) */
	heap->threads++;
	t->hnext = heap->live;		/* (Onyx: the heap's threads) */
	heap->live = t;
	t->vnext = qjs_all;		/* (Onyx: every thread) */
	qjs_all = t;

	/* the prelude: a function of the natives, run once */
	natives = JS_NewObject(t->ctx);
	JS_SetPropertyFunctionList(t->ctx, natives, qjs_natives,
			sizeof(qjs_natives) / sizeof(qjs_natives[0]));
	JS_SetPropertyFunctionList(t->ctx, natives, qjs_natives_html5,	/* Onyx: HTML5 */
			sizeof(qjs_natives_html5) / sizeof(qjs_natives_html5[0]));
	JS_SetPropertyFunctionList(t->ctx, natives, qjs_natives_shadow,	/* Onyx: shadow DOM */
			sizeof(qjs_natives_shadow) / sizeof(qjs_natives_shadow[0]));
	qjs_frames_natives(t->ctx, natives);	/* Onyx: the frames' windows (qjs_frames.c) */
	qjs_xml_natives(t->ctx, natives);	/* Onyx: XML, XPath, XSLT (qjs_xml.c) */
	qjs_enter(t);
	uint64_t t_prelude = onyx_perf_now();	/* (Onyx: onyx_perf.h) */
	qjs_intl_init(t->ctx);	/* (Onyx: Intl) */
	prelude = qjs_eval_cached(t->ctx, qjs_dom_js, sizeof(qjs_dom_js) - 1, "dom.js",
			&qjs_dom_bc, &qjs_dom_bc_len);
	if (JS_IsException(prelude)) {
		qjs_report(t->ctx, "dom.js");
	} else {
		r = JS_Call(t->ctx, prelude, JS_UNDEFINED, 1, (JSValueConst *) &natives);
		if (JS_IsException(r))
			qjs_report(t->ctx, "dom.js setup");
		JS_FreeValue(t->ctx, r);
	}
	JS_FreeValue(t->ctx, prelude);
	/* Onyx: html5.js, a function of the natives and dom.js's element classes (TAGS) */
	prelude = qjs_eval_cached(t->ctx, qjs_html5_js, sizeof(qjs_html5_js) - 1, "html5.js",
			&qjs_html5_bc, &qjs_html5_bc_len);
	if (JS_IsException(prelude)) {
		qjs_report(t->ctx, "html5.js");
	} else {
		JSValue args[2] = { natives, t->tag_protos };
		r = JS_Call(t->ctx, prelude, JS_UNDEFINED, 2, (JSValueConst *) args);
		if (JS_IsException(r))
			qjs_report(t->ctx, "html5.js setup");
		JS_FreeValue(t->ctx, r);
	}
	JS_FreeValue(t->ctx, prelude);
	qjs_canvas_setup(t->ctx, natives);	/* Onyx: <canvas> 2D (canvas.js) */
	qjs_net_setup(t->ctx, natives, NULL);	/* Onyx: WebSocket, EventSource, Workers */
	qjs_wasm_setup(t->ctx, natives);	/* Onyx: WebAssembly (wasm.js, on wasm3) */
	qjs_crypto_setup(t->ctx, natives);	/* Onyx: Web Crypto (crypto.js, on mbedTLS) */
	qjs_media_setup(t->ctx, natives);	/* Onyx: <video>, <audio>, MSE (media.js) */
	JS_FreeValue(t->ctx, natives);
	onyx_perf_log("js:prelude", t_prelude);	/* (a context's dom.js, html5.js, canvas.js, Intl) */
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
	qjs_net_stop(thread->ctx);	/* (Onyx: its sockets, streams, workers) */
	qjs_frames_stop(thread->ctx);	/* (Onyx: its message hook, its ports) */
	guit->misc->schedule(-1, qjs_load_later, thread);
	return NSERROR_OK;
}

static void qjs_thread_free(jsthread *t)
{
	jsheap *heap = t->heap;
	size_t i;
	int k;

	guit->misc->schedule(-1, qjs_jobs_later, t);	/* (Onyx) */
	qjs_timers_stop(t);
	qjs_reqs_stop(t);
	qjs_net_stop(t->ctx);	/* (Onyx: its sockets, streams, workers) */
	qjs_frames_stop(t->ctx);	/* (Onyx: its message hook, its ports) */
	guit->misc->schedule(-1, qjs_load_later, t);
	guit->misc->schedule(-1, qjs_ce_later, t);	/* (Onyx: custom elements) */
	while (t->ce_npending > 0)
		dom_node_unref(t->ce_pending[--t->ce_npending]);
	free(t->ce_pending);
	for (i = 0; i < t->capwraps; i++) {
		if (t->wraps[i].node != NULL)
			JS_FreeValue(t->ctx, t->wraps[i].obj);
	}
	free(t->wraps);
	for (k = 0; k < QP_COUNT; k++)
		JS_FreeValue(t->ctx, t->protos[k]);
	JS_FreeValue(t->ctx, t->tag_protos);
	JS_FreeValue(t->ctx, t->dispatch);
	JS_FreeValue(t->ctx, t->modsrc);
	JS_FreeValue(t->ctx, t->modmissing);
	JS_FreeValue(t->ctx, t->ce_hook);
	JS_FreeValue(t->ctx, t->shadow_proto);
	qjs_canvas_context_gone(t->ctx);	/* Onyx: its canvases, images */
	qjs_wasm_context_gone(t->ctx);	/* Onyx: its WebAssembly store */
	qjs_media_context_gone(t->ctx);	/* Onyx: its players */
	JS_FreeContext(t->ctx);
	if (t->doc != NULL)
		dom_node_unref(t->doc);
	if (t->url_override != NULL)
		nsurl_unref(t->url_override);
	{	/* Onyx: out of the heap's live threads */
		jsthread **pp;
		for (pp = &heap->live; *pp != NULL; pp = &(*pp)->hnext)
			if (*pp == t) {
				*pp = t->hnext;
				break;
			}
		for (pp = &qjs_all; *pp != NULL; pp = &(*pp)->vnext)
			if (*pp == t) {
				*pp = t->vnext;
				break;
			}
	}
	guit->misc->schedule(-1, qjs_visibility_later, t);
	if (heap->shared) {
		/* Onyx: another frame may hold this realm's objects (its context lives on
		 * while they do) and call its functions: their natives find a closed thread
		 * with nothing in it, not freed memory -- the record goes with the heap */
		t->zombie = true;
		t->closed = true;
		t->htmlc = NULL;
		t->bw = NULL;
		t->doc = NULL;
		t->url_override = NULL;
		t->wraps = NULL;
		t->nwraps = t->capwraps = 0;
		t->timers = NULL;
		memset(t->tbuck, 0, sizeof(t->tbuck));
		t->reqs = NULL;
		t->ce_pending = NULL;
		t->ce_npending = t->ce_cappending = 0;
		for (k = 0; k < QP_COUNT; k++)
			t->protos[k] = JS_UNDEFINED;
		t->tag_protos = t->dispatch = t->modsrc = t->modmissing = JS_UNDEFINED;
		t->ce_hook = t->shadow_proto = JS_UNDEFINED;
		t->hnext = heap->zombies;
		heap->zombies = t;
	} else {
		free(t);
	}
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

	uint64_t t0 = onyx_perf_now();	/* (Onyx: onyx_perf.h) */

	qjs_enter(thread);
	{
		size_t rl;
		char *rw = qjs_rewrite_import(src, txtlen, false, &rl);	/* (Onyx) */
		if (rw != NULL) {
			free(src);
			src = rw;
			txtlen = rl;
		}
	}
	/* Onyx: compiled, or read from the code cache (qjs_codecache.c), then run */
	r = qjs_cc_compile(thread->ctx, src, txtlen, name != NULL ? name : "script", false);
	if (!JS_IsException(r))
		r = JS_EvalFunction(thread->ctx, r);
	if (JS_IsException(r)) {
		qjs_report(thread->ctx, name != NULL ? name : "script");
#ifdef ONYX_HOST_SIM
		/* Onyx (the PC bench): NS_JSDUMP=<dir> -- a script that failed is written there */
		if (getenv("NS_JSDUMP") != NULL) {
			static int nd;
			char path[512];
			FILE *f;
			snprintf(path, sizeof path, "%s/failed-%d.js", getenv("NS_JSDUMP"), nd++);
			if ((f = fopen(path, "w")) != NULL) {
				fwrite(src, 1, txtlen, f);
				fclose(f);
			}
		}
#endif
	} else {
		ok = JS_ToBool(thread->ctx, r);
	}
	JS_FreeValue(thread->ctx, r);
	qjs_leave(thread);
	free(src);
	if (onyx_perf_on()) {
		char what[160];
		snprintf(what, sizeof what, "js:exec %.100s (%u KB)",
				name != NULL ? name : "script", (unsigned) (txtlen / 1024));
		onyx_perf_log(what, t0);
	}
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
	if (init != NULL && (strcmp(type, "click") == 0 || strcmp(type, "mousedown") == 0 ||
			strcmp(type, "keydown") == 0 || strcmp(type, "pointerdown") == 0))
		thread->activated = qjs_now_ms();	/* (Onyx: the user's activation) */
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
		/* (Onyx: a wheel's movement) */
		JS_SetPropertyStr(ctx, o, "deltaX", JS_NewInt32(ctx, init->delta_x));
		JS_SetPropertyStr(ctx, o, "deltaY", JS_NewInt32(ctx, init->delta_y));
		JS_SetPropertyStr(ctx, o, "buttons",
				JS_NewInt32(ctx, init->button == 0 ? 1 : 0));
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

/* exported interface documented in js.h (Onyx) */
void js_dispatch_anim_event(jsthread *thread, const char *type, struct dom_node *target,
		const char *name, double elapsed, int id)
{
	JSContext *ctx;
	JSValue args[3], r, o;

	if (thread == NULL || thread->closed || !JS_IsFunction(thread->ctx, thread->dispatch))
		return;
	ctx = thread->ctx;
	o = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, o, "type", JS_NewString(ctx, type));
	JS_SetPropertyStr(ctx, o, "name", JS_NewString(ctx, name != NULL ? name : ""));
	JS_SetPropertyStr(ctx, o, "elapsed", JS_NewFloat64(ctx, elapsed));
	JS_SetPropertyStr(ctx, o, "id", JS_NewInt32(ctx, id));
	args[0] = target != NULL ? qjs_wrap(thread, target) : JS_NULL;
	args[1] = JS_NewString(ctx, "onyx:anim");
	args[2] = o;
	r = qjs_call(thread, thread->dispatch, JS_UNDEFINED, 3, (JSValueConst *) args, type);
	JS_FreeValue(ctx, r);
	JS_FreeValue(ctx, args[0]);
	JS_FreeValue(ctx, args[1]);
	JS_FreeValue(ctx, args[2]);
}

/* exported interface documented in js.h (Onyx) */
void js_animation_frame(jsthread *thread, double now)
{
	JSContext *ctx;
	JSValue args[3], r, o;

	if (thread == NULL || thread->closed || !JS_IsFunction(thread->ctx, thread->dispatch))
		return;
	ctx = thread->ctx;
	o = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, o, "time", JS_NewFloat64(ctx, now));
	args[0] = JS_NULL;
	args[1] = JS_NewString(ctx, "onyx:frame");
	args[2] = o;
	r = qjs_call(thread, thread->dispatch, JS_UNDEFINED, 3, (JSValueConst *) args,
			"requestAnimationFrame");
	JS_FreeValue(ctx, r);
	JS_FreeValue(ctx, args[1]);
	JS_FreeValue(ctx, args[2]);
}

/* Onyx (docs/06 §32): visibilitychange */
static void qjs_visibility_later(void *p)
{
	jsthread *t = p;
	JSValue args[3], r;

	if (t->closed || t->worker || t->doc == NULL || !JS_IsFunction(t->ctx, t->dispatch))
		return;
	args[0] = JS_NULL;
	args[1] = JS_NewString(t->ctx, "onyx:visibility");
	args[2] = JS_UNDEFINED;
	r = qjs_call(t, t->dispatch, JS_UNDEFINED, 3, (JSValueConst *) args, "visibilitychange");
	JS_FreeValue(t->ctx, r);
	JS_FreeValue(t->ctx, args[1]);
}

/* exported interface documented in js.h */
void js_view_visibility_changed(void)
{
	for (jsthread *t = qjs_all; t != NULL; t = t->vnext)
		if (!t->closed && !t->worker)
			guit->misc->schedule(0, qjs_visibility_later, t);
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
	/* Onyx: and the fetches the page still waits for (base.active): a script a script
	 * inserted delays the load, as in a browser (Facebook's bootloader: its modules were
	 * not in when load fired, a handler used what they set up) */
	if ((!t->htmlc->had_initial_layout ||
	     t->htmlc->base.status != CONTENT_STATUS_DONE ||
	     t->htmlc->base.active > 0) &&
	    ++t->load_waits < 600) {
		guit->misc->schedule(50, qjs_load_later, t);
		return;
	}
	/* Onyx: and its iframes' documents (a window's load comes after its frames') */
	if (t->bw != NULL && onyx_frames_loading(t->bw) && ++t->load_waits < 600) {
		guit->misc->schedule(50, qjs_load_later, t);
		return;
	}
	js_dispatch_event(t, "onyx:complete", NULL, NULL);
	/* Onyx: an iframe's document loaded: its element's load event in its parent */
	if (!t->closed && t->bw != NULL)
		onyx_frame_loaded(t->bw);
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
	/* Onyx: once a custom element is defined, the elements inserted are shown to html5.js
	 * -- not now: in a mutation event the DOM is read-only (libdom); before the next
	 * script runs, else from the scheduler (qjs_ce_flush) */
	jsthread *t = thread;

	if (t == NULL || t->closed || !JS_IsFunction(t->ctx, t->ce_hook))
		return;
	if (t->ce_npending == t->ce_cappending) {
		int cap = t->ce_cappending ? t->ce_cappending * 2 : 64;
		dom_node **p = realloc(t->ce_pending, cap * sizeof(*p));
		if (p == NULL)
			return;
		t->ce_pending = p;
		t->ce_cappending = cap;
	}
	dom_node_ref((dom_node *) node);
	t->ce_pending[t->ce_npending++] = (dom_node *) node;
	if (t->ce_npending == 1)
		guit->misc->schedule(0, qjs_ce_later, t);
}

/* exported interface documented in js.h */
void js_event_cleanup(jsthread *thread, struct dom_event *evt)
{
	(void) thread;
	(void) evt;
}


/* ---- Onyx: the workers' contexts (qjs_net.c) ---------------------------------------------------
 * A worker's scripts run in a context of their own in the window's runtime, on the UI thread
 * (NetSurf's scheduler: its timers and messages are tasks as the page's are): a jsthread
 * with no document (htmlc, doc NULL; worker set), its URL in url_override (location, the
 * base of its requests). The same preludes as a page's -- Intl, dom.js, html5.js -- then
 * net.js makes its global a worker's (no DOM, self, postMessage, importScripts). */

/* exported interface documented in qjs_net.h */
struct nsurl *qjs_ctx_url(JSContext *ctx)
{
	jsthread *t = QJS_T(ctx);

	if (t == NULL)
		return NULL;
	if (t->url_override != NULL)
		return t->url_override;
	return t->htmlc != NULL ? content_get_url(&t->htmlc->base) : NULL;
}

/* exported interface documented in qjs_wasm.h */
bool qjs_ctx_timed_out(JSContext *ctx)
{
	jsthread *t = QJS_T(ctx);

	return t != NULL && t->heap != NULL && qjs_interrupt(JS_GetRuntime(ctx), t->heap);
}

/* ---- Onyx: what qjs_frames.c uses ------------------------------------------------------- */

/* exported interface documented in qjs_frames.h */
struct browser_window *qjs_ctx_window(JSContext *ctx)
{
	jsthread *t = QJS_T(ctx);

	return (t == NULL || t->closed || t->worker) ? NULL : t->bw;
}

/* exported interface documented in qjs_frames.h */
bool qjs_ctx_activated(JSContext *ctx, unsigned int ms)
{
	jsthread *t = QJS_T(ctx);

	return t != NULL && !t->closed && t->activated != 0 &&
		qjs_now_ms() - t->activated <= ms;
}

/* exported interface documented in qjs_frames.h */
JSContext *qjs_thread_context(struct jsthread *t)
{
	return (t == NULL || t->closed) ? NULL : t->ctx;
}

/* exported interface documented in qjs_frames.h */
JSValue qjs_wrap_node(JSContext *ctx, struct dom_node *n)
{
	jsthread *t = QJS_T(ctx);

	if (t == NULL || t->closed)
		return JS_NULL;
	return qjs_wrap(t, n);
}

/* exported interface documented in js.h (Onyx, docs/06 §43) */
nserror js_xslt_transform(jsthread *t, struct dom_document *doc, const char *xsl,
		size_t xsl_len, const char *xsl_url, char **out, size_t *out_len, char *method,
		size_t method_size)
{
	JSContext *ctx;
	JSValue g, fn, args[3], r;
	nserror e = NSERROR_INVALID;

	*out = NULL;
	*out_len = 0;
	if (t == NULL || t->closed || t->ctx == NULL)
		return NSERROR_BAD_PARAMETER;
	ctx = t->ctx;
	g = JS_GetGlobalObject(ctx);
	fn = JS_GetPropertyStr(ctx, g, "\x01onyx_xslt");	/* (html5.js: hidden) */
	JS_FreeValue(ctx, g);
	if (!JS_IsFunction(ctx, fn)) {
		JS_FreeValue(ctx, fn);
		return NSERROR_INVALID;
	}
	args[0] = qjs_wrap(t, (dom_node *) doc);
	args[1] = JS_NewStringLen(ctx, xsl, xsl_len);
	args[2] = JS_NewString(ctx, xsl_url != NULL ? xsl_url : "");
	qjs_enter(t);
	r = JS_Call(ctx, fn, JS_UNDEFINED, 3, (JSValueConst *) args);
	if (JS_IsException(r)) {
		qjs_report(ctx, "XSLT");
		r = JS_UNDEFINED;
	}
	qjs_leave(t);
	if (JS_IsArray(r)) {
		JSValue m = JS_GetPropertyUint32(ctx, r, 0), x = JS_GetPropertyUint32(ctx, r, 1);
		const char *ms = JS_ToCString(ctx, m);
		size_t n = 0;
		const char *xs = JS_ToCStringLen(ctx, &n, x);
		if (ms != NULL && xs != NULL) {
			snprintf(method, method_size, "%s", ms);
			*out = malloc(n + 1);
			if (*out != NULL) {
				memcpy(*out, xs, n);
				(*out)[n] = '\0';
				*out_len = n;
				e = NSERROR_OK;
			} else {
				e = NSERROR_NOMEM;
			}
		}
		if (ms != NULL)
			JS_FreeCString(ctx, ms);
		if (xs != NULL)
			JS_FreeCString(ctx, xs);
		JS_FreeValue(ctx, m);
		JS_FreeValue(ctx, x);
	}
	JS_FreeValue(ctx, r);
	JS_FreeValue(ctx, fn);
	JS_FreeValue(ctx, args[0]);
	JS_FreeValue(ctx, args[1]);
	JS_FreeValue(ctx, args[2]);
	return e;
}

/* exported interface documented in qjs_net.h */
bool qjs_ctx_closed(JSContext *ctx)
{
	jsthread *t = QJS_T(ctx);

	return t == NULL || t->closed;
}

/* exported interface documented in qjs_net.h */
JSContext *qjs_worker_create(JSContext *parent, const char *url)
{
	jsthread *pt = QJS_T(parent), *t;
	jsheap *heap = pt != NULL ? pt->heap : NULL;
	JSValue natives, prelude, r;
	nsurl *u = NULL;
	int i;

	if (heap == NULL || pt->closed || nsurl_create(url, &u) != NSERROR_OK)
		return NULL;
	t = calloc(1, sizeof(*t));
	if (t == NULL) {
		nsurl_unref(u);
		return NULL;
	}
	t->heap = heap;
	t->worker = true;
	t->url_override = u;
	t->ctx = JS_NewContext(heap->rt);
	if (t->ctx == NULL) {
		nsurl_unref(u);
		free(t);
		return NULL;
	}
	for (i = 0; i < QP_COUNT; i++)
		t->protos[i] = JS_UNDEFINED;
	t->tag_protos = JS_UNDEFINED;
	t->dispatch = JS_UNDEFINED;
	JS_SetContextOpaque(t->ctx, t);
	t->modsrc = JS_NewObject(t->ctx);
	t->modmissing = JS_NewArray(t->ctx);
	t->ce_hook = JS_UNDEFINED;
	heap->threads++;

	natives = JS_NewObject(t->ctx);
	JS_SetPropertyFunctionList(t->ctx, natives, qjs_natives,
			sizeof(qjs_natives) / sizeof(qjs_natives[0]));
	JS_SetPropertyFunctionList(t->ctx, natives, qjs_natives_html5,
			sizeof(qjs_natives_html5) / sizeof(qjs_natives_html5[0]));
	qjs_enter(t);
	qjs_intl_init(t->ctx);
	prelude = qjs_eval_cached(t->ctx, qjs_dom_js, sizeof(qjs_dom_js) - 1, "dom.js",
			&qjs_dom_bc, &qjs_dom_bc_len);
	if (JS_IsException(prelude)) {
		qjs_report(t->ctx, "worker dom.js");
	} else {
		r = JS_Call(t->ctx, prelude, JS_UNDEFINED, 1, (JSValueConst *) &natives);
		if (JS_IsException(r))
			qjs_report(t->ctx, "worker dom.js setup");
		JS_FreeValue(t->ctx, r);
	}
	JS_FreeValue(t->ctx, prelude);
	{	/* (html5.js listens to the document as it sets up: a stand-in, net.js removes it) */
		static const char stub[] = "Object.defineProperty(globalThis, 'document', "
			"{ configurable: true, writable: true, value: new EventTarget() });";
		JS_FreeValue(t->ctx, JS_Eval(t->ctx, stub, sizeof(stub) - 1, "worker",
				JS_EVAL_TYPE_GLOBAL));
	}
	prelude = qjs_eval_cached(t->ctx, qjs_html5_js, sizeof(qjs_html5_js) - 1, "html5.js",
			&qjs_html5_bc, &qjs_html5_bc_len);
	if (JS_IsException(prelude)) {
		qjs_report(t->ctx, "worker html5.js");
	} else {
		JSValue args[2] = { natives, t->tag_protos };
		r = JS_Call(t->ctx, prelude, JS_UNDEFINED, 2, (JSValueConst *) args);
		if (JS_IsException(r))
			qjs_report(t->ctx, "worker html5.js setup");
		JS_FreeValue(t->ctx, r);
	}
	JS_FreeValue(t->ctx, prelude);
	qjs_net_setup(t->ctx, natives, parent);
	qjs_wasm_setup(t->ctx, natives);	/* (Onyx: WebAssembly) */
	qjs_crypto_setup(t->ctx, natives);	/* (Onyx: Web Crypto) */
	JS_FreeValue(t->ctx, natives);
	t->dirty = false;
	qjs_leave(t);
	return t->ctx;
}

/* exported interface documented in qjs_net.h */
void qjs_worker_destroy(JSContext *wctx)
{
	jsthread *t = QJS_T(wctx);

	if (t == NULL || !t->worker)
		return;
	js_closethread(t);
	js_destroythread(t);
}
