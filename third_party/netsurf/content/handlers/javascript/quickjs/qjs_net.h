/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 */

/**
 * \file
 * Onyx: the scripts' real-time and background APIs (qjs_net.c, net.js) -- WebSocket,
 * EventSource, the streamed fetch / XHR, Worker / SharedWorker, BroadcastChannel between
 * a page and its workers -- and what they use of qjs.c.
 */

#ifndef NETSURF_QJS_NET_H
#define NETSURF_QJS_NET_H

#include <stdbool.h>
#include "quickjs.h"

struct nsurl;

/**
 * The natives added to the prelude's natives (N.ws*, N.sse*, N.worker*...), then net.js run
 * with them (after html5.js). parent: NULL for a document's context, else the context that
 * made this worker (its script, name, type: the worker being made, qjs_net.c's).
 */
void qjs_net_setup(JSContext *ctx, JSValueConst natives, JSContext *parent);

/** A context's scripts stopped: its sockets and streams closed, its workers ended. */
void qjs_net_stop(JSContext *ctx);

/* from qjs.c */

/** A worker's context (url: its script's), its preludes run (then net.js); or NULL. */
JSContext *qjs_worker_create(JSContext *parent, const char *url);

/** A worker's context ended (freed once none of its code runs). */
void qjs_worker_destroy(JSContext *wctx);

/** The URL of a context's scripts: its document's, or its worker script's (not a ref). */
struct nsurl *qjs_ctx_url(JSContext *ctx);

/** Whether a context's scripts are stopped. */
bool qjs_ctx_closed(JSContext *ctx);

/** a callback called from the browser (as the timers: its jobs run, a changed DOM laid
 * out again) -- qjs.c */
void qjs_invoke(JSContext *ctx, JSValueConst fn, int argc, JSValueConst *argv,
		const char *where);

#endif
