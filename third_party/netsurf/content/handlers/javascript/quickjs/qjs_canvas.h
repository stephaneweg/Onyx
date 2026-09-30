/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 */

/**
 * \file
 * Onyx: <canvas> 2D on PlutoVG (qjs_canvas.c, canvas.js) and what it uses of qjs.c.
 */

#ifndef NETSURF_QJS_CANVAS_H
#define NETSURF_QJS_CANVAS_H

#include "quickjs.h"

struct dom_node;
struct html_content;

/**
 * The canvas natives added to the prelude's natives (N.cv*), then canvas.js run with
 * them (after dom.js: it completes HTMLCanvasElement).
 */
void qjs_canvas_setup(JSContext *ctx, JSValueConst natives);

/** A document's scripts gone (before its context is freed): its canvases and images
 * let go of it. */
void qjs_canvas_context_gone(JSContext *ctx);

/* from qjs.c */

/** the node a wrapper holds (NULL: not a node) */
struct dom_node *qjs_node_of(JSValueConst v);

/** the document of a context's scripts (NULL once they are closed) */
struct html_content *qjs_html_of(JSContext *ctx);

/** a callback called from the browser (as the timers: its jobs run, a changed DOM
 * laid out again) */
void qjs_invoke(JSContext *ctx, JSValueConst fn, int argc, JSValueConst *argv,
		const char *where);

#endif
