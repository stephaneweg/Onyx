/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 */

/**
 * \file
 * Onyx: <video>, <audio> and Media Source Extensions (qjs_media.c, media.js) on the media
 * library (user/av), and what the layout and the painting ask of them.
 */

#ifndef NETSURF_QJS_MEDIA_H
#define NETSURF_QJS_MEDIA_H

#include <stdbool.h>

struct dom_node;
struct html_content;
struct redraw_context;
struct rect;

#ifdef QJS_MEDIA_JS
#include "quickjs.h"
/** The media natives added to the prelude's natives (N.md*), then media.js run with them. */
void qjs_media_setup(JSContext *ctx, JSValueConst natives);
/** A document's scripts gone: its players stopped and freed. */
void qjs_media_context_gone(JSContext *ctx);
#endif

/** The video's natural size once known (css/hints.c: the box's size) -> true */
bool onyx_media_natural_size(struct dom_node *n, int *w, int *h);

/** A <video> / <audio> box made (box_special.c): its element set up for playing (a src or
 * <source> the parser gave, autoplay) if no script did. */
void onyx_media_box_made(struct dom_node *n, struct html_content *c);

/** Paint a media element's box content (the frame, fitted; the native controls) at x, y,
 * w x h (redraw.c) */
bool onyx_media_redraw(struct dom_node *n, int x, int y, int w, int h, float scale,
		const struct rect *clip, const struct redraw_context *ctx);

#endif
