/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 */

/**
 * \file
 * Onyx: the windows of a tab's frames to its scripts (qjs_frames.c) -- WindowProxy,
 * window.parent / top / frames / frameElement, an iframe's contentWindow /
 * contentDocument, postMessage between the frames, MessagePort across them -- and what
 * it uses of qjs.c.
 */

#ifndef NETSURF_QJS_FRAMES_H
#define NETSURF_QJS_FRAMES_H

#include <stdbool.h>
#include "quickjs.h"

struct browser_window;
struct html_content;
struct dom_node;
struct jsthread;

/** the frames' natives (N.frame*, N.port*), added to the prelude's natives */
void qjs_frames_natives(JSContext *ctx, JSValueConst natives);

/** A context's scripts stopped: its message hook dropped, the ports it holds closed. */
void qjs_frames_stop(JSContext *ctx);

/* from qjs.c */

/** the window whose document a context's scripts are (NULL: a worker's, or stopped) */
struct browser_window *qjs_ctx_window(JSContext *ctx);

/** a thread's context, NULL once its scripts stopped */
JSContext *qjs_thread_context(struct jsthread *t);

/** a node's wrapper in a context's realm (the context's document's node), or null */
JSValue qjs_wrap_node(JSContext *ctx, struct dom_node *n);

#endif
