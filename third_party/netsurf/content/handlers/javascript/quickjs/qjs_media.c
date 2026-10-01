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
 * Onyx: <video>, <audio> and Media Source Extensions on the media library (user/av).
 *
 * media.js (compiled in as qjs_media_js.h) is the API -- HTMLMediaElement's state, events
 * and promises, MediaSource / SourceBuffer, TimeRanges, MediaError, MediaCapabilities, the
 * Fullscreen API, the native controls' clicks; the natives here (N.md*) hold a player each
 * (av_player: its decoding threads, the sound, the clock) and its store (the coded frames:
 * MSE's appends, or a file's bytes the script fetches).
 *
 * A player's turn (qm_poll, scheduled every 10 ms while it plays, seeks or waits; less when
 * paused): the frame due now is copied into the element's bitmap -- the node's user data
 * under __ns_key_canvas_node_data, as a canvas' -- and its box redrawn (onyx_media_redraw
 * fits it in the content box, and draws the controls); the state goes to media.js' callback
 * when it changed (the events come from there).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <dom/dom.h>

#include "quickjs.h"

#include "utils/utils.h"
#include "utils/log.h"
#include "utils/corestrings.h"
#include "netsurf/bitmap.h"
#include "netsurf/plotters.h"
#include "netsurf/plot_style.h"
#include "netsurf/misc.h"
#include "netsurf/layout.h"
#include "desktop/gui_internal.h"
#include "desktop/bitmap.h"
#include "html/private.h"
#include "html/box.h"

#define QJS_MEDIA_JS
#include "javascript/quickjs/qjs_media.h"
#include "javascript/quickjs/qjs_canvas.h"	/* qjs_node_of, qjs_html_of, qjs_invoke */
#include "javascript/quickjs/qjs_net.h"		/* qjs_eval_cached */
#include "av.h"
#include "qjs_media_js.h"	/* media.js, as a C string (the build makes it) */

JSValue qjs_wrap_node(JSContext *ctx, struct dom_node *n);
JSContext *qjs_thread_context(struct jsthread *t);

/* the native controls' bar (media.js has the same geometry for the clicks) */
#define CTRL_H 36

struct qmedia {
	struct qmedia *next;
	JSContext *ctx;			/* NULL once its document's scripts are gone */
	dom_node *node;			/* its element (a ref) */
	struct av_player *p;
	JSValue tick;			/* media.js' callback (the state changed) */
	bool scheduled;
	bool frame_cb;			/* media.js wants each presented frame (rVFC) */
	unsigned shown;			/* the serial of the frame shown */
	av_us shown_pts;
	unsigned presented;
	int pix;			/* the bitmaps' layout: AV_PIX_RGBA / AV_PIX_BGRA */
	bool swizzle;			/* neither: bytes moved */
	struct av_player_status st;	/* the last state */
	av_us sent_time;		/* the time media.js got last */
	bool sent;
	int nat_w, nat_h;		/* the natural size the layout has */
	bool controls_shown;		/* (media.js: the pointer is over it, or a click) */
	float volume;
	bool muted;
};

static JSClassID qm_class;
static struct qmedia *qm_all;

/* ---- the frame -------------------------------------------------------------------------- */

static void qm_user_data_handler(dom_node_operation operation, dom_string *key, void *data,
		struct dom_node *src, struct dom_node *dst)
{
	(void) src;
	(void) dst;
	if (data == NULL || !dom_string_isequal(key, corestring_dom___ns_key_canvas_node_data))
		return;
	if (operation == DOM_NODE_DELETED)
		guit->bitmap->destroy(data);
}

static struct box *qm_box(struct qmedia *m)
{
	struct box *box = NULL;

	if (m->node == NULL || dom_node_get_user_data(m->node, corestring_dom___ns_key_box_node_data,
			(void **) &box) != DOM_NO_ERR)
		return NULL;
	return box;
}

static void qm_redraw(struct qmedia *m)
{
	html_content *htmlc = m->ctx != NULL ? qjs_html_of(m->ctx) : NULL;
	struct box *box = qm_box(m);

	if (htmlc != NULL && box != NULL)
		html__redraw_a_box(htmlc, box);
}

/** the frame into the element's bitmap */
static void qm_present(struct qmedia *m, const struct av_video_out *v)
{
	struct bitmap *bm = NULL, *old = NULL;
	uint8_t *dst;
	size_t stride;
	int y;

	if (dom_node_get_user_data(m->node, corestring_dom___ns_key_canvas_node_data,
			(void **) &bm) != DOM_NO_ERR)
		bm = NULL;
	if (bm == NULL || guit->bitmap->get_width(bm) != v->width ||
	    guit->bitmap->get_height(bm) != v->height) {
		bm = guit->bitmap->create(v->width, v->height, BITMAP_NONE);
		if (bm == NULL)
			return;
		if (dom_node_set_user_data(m->node, corestring_dom___ns_key_canvas_node_data,
				bm, qm_user_data_handler, (void **) &old) != DOM_NO_ERR) {
			guit->bitmap->destroy(bm);
			return;
		}
		if (old != NULL && old != bm)
			guit->bitmap->destroy(old);
		guit->bitmap->set_opaque(bm, true);
	}
	dst = guit->bitmap->get_buffer(bm);
	stride = guit->bitmap->get_rowstride(bm);
	if (dst == NULL)
		return;
	for (y = 0; y < v->height; y++) {
		uint8_t *d = dst + (size_t) y * stride;
		const uint8_t *s = v->pixels + (size_t) y * v->stride;
		if (!m->swizzle) {
			memcpy(d, s, (size_t) v->width * 4);
		} else {
			const struct bitmap_colour_layout L = bitmap_layout;
			int x;
			for (x = 0; x < v->width; x++) {
				d[4 * x + L.r] = s[4 * x];
				d[4 * x + L.g] = s[4 * x + 1];
				d[4 * x + L.b] = s[4 * x + 2];
				d[4 * x + L.a] = 255;
			}
		}
	}
	guit->bitmap->modified(bm);
	m->shown = v->serial;
	m->shown_pts = v->pts;
	m->presented++;
	qm_redraw(m);
}

/* ---- the turn ----------------------------------------------------------------------------- */

static JSValue qm_state_array(JSContext *ctx, struct qmedia *m)
{
	const struct av_player_status *s = &m->st;
	JSValue a = JS_NewArray(ctx);
	double v[20];
	int i, n = 0;

	v[n++] = (double) s->time / 1e6;
	v[n++] = s->duration == AV_NOTIME ? 1.0 / 0.0 : (double) s->duration / 1e6;
	v[n++] = s->ready;
	v[n++] = s->paused;
	v[n++] = s->ended;
	v[n++] = s->seeking;
	v[n++] = s->waiting;
	v[n++] = s->width;
	v[n++] = s->height;
	v[n++] = s->decoded;
	v[n++] = s->dropped;
	v[n++] = s->error;
	v[n++] = (double) s->want;
	v[n++] = s->sound;
	v[n++] = s->has_audio;
	v[n++] = s->has_video;
	v[n++] = (double) s->decode_us;
	v[n++] = m->presented;
	v[n++] = (double) s->sync_us;
	for (i = 0; i < n; i++)
		JS_SetPropertyUint32(ctx, a, (uint32_t) i, JS_NewFloat64(ctx, v[i]));
	return a;
}

static void qm_poll(void *p);

static void qm_schedule(struct qmedia *m, int ms)
{
	if (!m->scheduled && m->ctx != NULL && m->p != NULL) {
		m->scheduled = true;
		guit->misc->schedule(ms, qm_poll, m);
	}
}

static void qm_poll(void *p)
{
	struct qmedia *m = p;
	struct av_player_status st, prev;
	struct av_video_out v;
	int got, delay;
	bool changed, size_changed;

	m->scheduled = false;
	if (m->p == NULL || m->ctx == NULL)
		return;
	memset(&v, 0, sizeof v);
	got = av_player_poll(m->p, &st, &v);
	prev = m->st;
	m->st = st;
	if (got && v.serial != m->shown)
		qm_present(m, &v);
	else
		got = 0;
	/* the natural size the layout gives the box */
	size_changed = st.width > 0 && (st.width != m->nat_w || st.height != m->nat_h);
	if (size_changed) {
		html_content *htmlc = qjs_html_of(m->ctx);
		m->nat_w = st.width;
		m->nat_h = st.height;
		if (htmlc != NULL)
			html_state_restyle(htmlc, m->node);
	}
	changed = !m->sent || st.ready != prev.ready || st.paused != prev.paused ||
		st.ended != prev.ended || st.seeking != prev.seeking || st.waiting != prev.waiting ||
		st.width != prev.width || st.height != prev.height || st.error != prev.error ||
		st.duration != prev.duration || st.want != prev.want ||
		(st.time - m->sent_time > 240000 || m->sent_time - st.time > 240000) ||
		(got && m->frame_cb);
	if (changed && JS_IsFunction(m->ctx, m->tick)) {
		JSValue arg = qm_state_array(m->ctx, m);
		JSValue fn = JS_DupValue(m->ctx, m->tick);
		m->sent = true;
		m->sent_time = st.time;
		qjs_invoke(m->ctx, fn, 1, &arg, "media");
		JS_FreeValue(m->ctx, fn);
		JS_FreeValue(m->ctx, arg);
		if (m->p == NULL || m->ctx == NULL)
			return;		/* (the callback closed it) */
	}
	/* the controls' time moves: their bar redrawn each second */
	if (m->controls_shown && !st.paused && (st.time / 1000000) != (prev.time / 1000000))
		qm_redraw(m);
	if (!st.paused || st.seeking || st.waiting)
		delay = 10;
	else if (st.ready < AV_HAVE_ENOUGH_DATA || m->presented == 0)
		delay = 40;
	else
		delay = 250;
	qm_schedule(m, delay);
}

/* ---- the object ------------------------------------------------------------------------- */

static void qm_stop(struct qmedia *m)
{
	if (m->scheduled)
		guit->misc->schedule(-1, qm_poll, m);
	m->scheduled = false;
	if (m->p != NULL) {
		av_player_free(m->p);
		m->p = NULL;
	}
}

static void qm_unlink(struct qmedia *m)
{
	struct qmedia **l;

	for (l = &qm_all; *l != NULL; l = &(*l)->next)
		if (*l == m) {
			*l = m->next;
			break;
		}
}

static void qm_finalizer(JSRuntime *rt, JSValue val)
{
	struct qmedia *m = JS_GetOpaque(val, qm_class);

	if (m == NULL)
		return;
	qm_stop(m);
	qm_unlink(m);
	if (m->ctx != NULL)
		JS_FreeValueRT(rt, m->tick);
	if (m->node != NULL)
		dom_node_unref(m->node);
	free(m);
}

static JSClassDef qm_classdef = { .class_name = "OnyxMedia", .finalizer = qm_finalizer };

static struct qmedia *qm_of(JSValueConst v)
{
	return JS_GetOpaque(v, qm_class);
}

static struct qmedia *qm_find(struct dom_node *n)
{
	struct qmedia *m;

	for (m = qm_all; m != NULL; m = m->next)
		if (m->node == n && m->ctx != NULL)
			return m;
	return NULL;
}

static double qd(JSContext *ctx, int argc, JSValueConst *argv, int i)
{
	double d = 0;

	if (i < argc)
		JS_ToFloat64(ctx, &d, argv[i]);
	return d;
}

static int qi(JSContext *ctx, int argc, JSValueConst *argv, int i)
{
	int32_t v = 0;

	if (i < argc)
		JS_ToInt32(ctx, &v, argv[i]);
	return v;
}

static av_us us_of(double s)
{
	if (s != s)
		return 0;
	if (s > 9e12)
		return INT64_MAX / 2;
	if (s < -9e12)
		return -INT64_MAX / 2;
	return (av_us) (s * 1e6);
}

/** bytes of an ArrayBuffer / a view -> pointer (NULL: none) */
static const uint8_t *qm_bytes(JSContext *ctx, JSValueConst v, size_t *len)
{
	size_t size = 0, off = 0, n = 0, bpe = 0;
	uint8_t *p = JS_GetArrayBuffer(ctx, &size, v);
	JSValue buf;

	if (p != NULL) {
		*len = size;
		return p;
	}
	JS_FreeValue(ctx, JS_GetException(ctx));
	buf = JS_GetTypedArrayBuffer(ctx, v, &off, &n, &bpe);
	if (JS_IsException(buf)) {
		JS_FreeValue(ctx, JS_GetException(ctx));
		return NULL;
	}
	p = JS_GetArrayBuffer(ctx, &size, buf);
	JS_FreeValue(ctx, buf);
	if (p == NULL || off + n > size) {
		JS_FreeValue(ctx, JS_GetException(ctx));
		return NULL;
	}
	*len = n;
	return p + off;
}

/* mdNew(element) -> a handle */
static JSValue n_md_new(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qmedia *m = calloc(1, sizeof(*m));
	const struct bitmap_colour_layout L = bitmap_layout;
	JSValue obj;

	(void) this_val;
	if (m == NULL)
		return JS_ThrowOutOfMemory(ctx);
	m->ctx = ctx;
	m->tick = JS_UNDEFINED;
	m->volume = 1;
	m->node = argc > 0 ? qjs_node_of(argv[0]) : NULL;
	if (m->node != NULL)
		dom_node_ref(m->node);
	if (L.r == 0 && L.g == 1 && L.b == 2)
		m->pix = AV_PIX_RGBA;
	else if (L.b == 0 && L.g == 1 && L.r == 2)
		m->pix = AV_PIX_BGRA;
	else {
		m->pix = AV_PIX_RGBA;
		m->swizzle = true;
	}
	obj = JS_NewObjectClass(ctx, qm_class);
	if (JS_IsException(obj)) {
		if (m->node != NULL)
			dom_node_unref(m->node);
		free(m);
		return obj;
	}
	JS_SetOpaque(obj, m);
	m->next = qm_all;
	qm_all = m;
	return obj;
}

#define QM_ARG0 struct qmedia *m = argc > 0 ? qm_of(argv[0]) : NULL; (void) this_val; \
	if (m == NULL || m->ctx == NULL) return JS_UNDEFINED
#define QM_PLAYER QM_ARG0; if (m->p == NULL) return JS_UNDEFINED

/* mdOpen(h): its player made (threads, the store) -> true */
static JSValue n_md_open(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_ARG0;
	if (m->p == NULL) {
		m->p = av_player_new(m->pix, NULL);
		if (m->p != NULL) {
			av_player_set_volume(m->p, m->volume, m->muted);
			m->presented = 0;
			m->shown = 0;
			m->sent = false;
			memset(&m->st, 0, sizeof m->st);
			qm_schedule(m, 0);
		}
	}
	return JS_NewBool(ctx, m->p != NULL);
}

/* mdClose(h): the player stopped and freed (a new load, the element gone) */
static JSValue n_md_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_ARG0;
	(void) ctx;
	qm_stop(m);
	m->presented = 0;
	m->nat_w = m->nat_h = 0;
	return JS_UNDEFINED;
}

/* mdClear(h): the picture dropped (a new load) */
static JSValue n_md_clear(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct bitmap *old = NULL;
	QM_ARG0;
	(void) ctx;
	if (m->node != NULL && dom_node_set_user_data(m->node, corestring_dom___ns_key_canvas_node_data,
			NULL, NULL, (void **) &old) == DOM_NO_ERR && old != NULL)
		guit->bitmap->destroy(old);
	qm_redraw(m);
	return JS_UNDEFINED;
}

/* mdWatch(h, fn, frames): the state's callback; frames: each presented frame too */
static JSValue n_md_watch(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_ARG0;
	JS_FreeValue(ctx, m->tick);
	m->tick = argc > 1 ? JS_DupValue(ctx, argv[1]) : JS_UNDEFINED;
	m->frame_cb = argc > 2 && JS_ToBool(ctx, argv[2]);
	qm_schedule(m, 0);
	return JS_UNDEFINED;
}

/* mdFrames(h, on): each presented frame reported (requestVideoFrameCallback) */
static JSValue n_md_frames(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_ARG0;
	m->frame_cb = argc > 1 && JS_ToBool(ctx, argv[1]);
	return JS_UNDEFINED;
}

/* mdAddSource(h, mime) -> a source id (>= 0) or an AV_E* error */
static JSValue n_md_add_source(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const char *mime;
	int r;
	QM_ARG0;
	if (m->p == NULL)
		return JS_NewInt32(ctx, AV_ERR);
	mime = argc > 1 ? JS_ToCString(ctx, argv[1]) : NULL;
	r = av_store_add_source(av_player_store(m->p), mime != NULL ? mime : "", 0);
	if (mime != NULL)
		JS_FreeCString(ctx, mime);
	return JS_NewInt32(ctx, r);
}

static JSValue n_md_remove_source(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_PLAYER;
	av_store_remove_source(av_player_store(m->p), qi(ctx, argc, argv, 1));
	return JS_UNDEFINED;
}

/* mdChangeType(h, src, mime) -> 0 / an error */
static JSValue n_md_change_type(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const char *mime;
	int r;
	QM_ARG0;
	if (m->p == NULL)
		return JS_NewInt32(ctx, AV_ERR);
	mime = argc > 2 ? JS_ToCString(ctx, argv[2]) : NULL;
	r = av_store_change_type(av_player_store(m->p), qi(ctx, argc, argv, 1), mime != NULL ? mime : "");
	if (mime != NULL)
		JS_FreeCString(ctx, mime);
	return JS_NewInt32(ctx, r);
}

/* mdAppend(h, src, bytes) -> 0, AV_ERR (a parse error), AV_EUNSUP, AV_EFULL (quota) */
static JSValue n_md_append(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const uint8_t *b;
	size_t n = 0;
	int r;
	QM_ARG0;
	if (m->p == NULL)
		return JS_NewInt32(ctx, AV_ERR);
	b = argc > 2 ? qm_bytes(ctx, argv[2], &n) : NULL;
	if (b == NULL)
		return JS_NewInt32(ctx, AV_ERR);
	r = av_store_append(av_player_store(m->p), qi(ctx, argc, argv, 1), b, n);
	qm_schedule(m, 0);
	return JS_NewInt32(ctx, r);
}

/* mdFeed(h, src, pos, bytes): a file's bytes at pos -> 0 / an error */
static JSValue n_md_feed(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const uint8_t *b;
	size_t n = 0;
	int r;
	QM_ARG0;
	if (m->p == NULL)
		return JS_NewInt32(ctx, AV_ERR);
	b = argc > 3 ? qm_bytes(ctx, argv[3], &n) : NULL;
	if (b == NULL)
		return JS_NewInt32(ctx, AV_ERR);
	r = av_store_feed(av_player_store(m->p), qi(ctx, argc, argv, 1), (int64_t) qd(ctx, argc, argv, 2), b, n);
	qm_schedule(m, 0);
	return JS_NewInt32(ctx, r);
}

static JSValue n_md_feed_end(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_PLAYER;
	av_store_feed_end(av_player_store(m->p), qi(ctx, argc, argv, 1));
	return JS_UNDEFINED;
}

/* mdWant(h, src) -> the offset the file's demuxer wants next, -1 none */
static JSValue n_md_want(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qmedia *m = argc > 0 ? qm_of(argv[0]) : NULL;
	(void) this_val;
	if (m == NULL || m->p == NULL)
		return JS_NewFloat64(ctx, -1);
	return JS_NewFloat64(ctx, (double) av_store_want(av_player_store(m->p), qi(ctx, argc, argv, 1)));
}

/* mdAhead(h) -> seconds buffered after the current time */
static JSValue n_md_ahead(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qmedia *m = argc > 0 ? qm_of(argv[0]) : NULL;
	struct av_range r[16];
	int n, i, k;
	double t, best = 0;
	(void) this_val;
	if (m == NULL || m->p == NULL)
		return JS_NewFloat64(ctx, 0);
	t = (double) m->st.time / 1e6;
	for (k = 0; k < 4; k++) {
		n = av_store_buffered(av_player_store(m->p), k, r, 16);
		for (i = 0; i < n; i++)
			if (r[i].start / 1e6 <= t + 0.2 && r[i].end / 1e6 > t && r[i].end / 1e6 - t > best)
				best = r[i].end / 1e6 - t;
	}
	return JS_NewFloat64(ctx, best);
}

static JSValue n_md_remove(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_PLAYER;
	av_store_remove(av_player_store(m->p), qi(ctx, argc, argv, 1), us_of(qd(ctx, argc, argv, 2)),
			us_of(qd(ctx, argc, argv, 3)));
	return JS_UNDEFINED;
}

/* mdBuffered(h, src) -> [start, end, ...] in seconds; src -1: every source's intersected */
static JSValue n_md_buffered(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qmedia *m = argc > 0 ? qm_of(argv[0]) : NULL;
	struct av_range r[32], acc[32], tmp[32];
	int n = 0, src = qi(ctx, argc, argv, 1), i, k, na = -1;
	JSValue a = JS_NewArray(ctx);

	(void) this_val;
	if (m == NULL || m->p == NULL)
		return a;
	if (src >= 0) {
		n = av_store_buffered(av_player_store(m->p), src, r, 32);
	} else {
		for (k = 0; k < 4; k++) {
			int m2;
			if (av_store_ntracks(av_player_store(m->p), k) == 0)
				continue;
			m2 = av_store_buffered(av_player_store(m->p), k, r, 32);
			if (na < 0) {
				memcpy(acc, r, (size_t) m2 * sizeof r[0]);
				na = m2;
			} else {
				int a1 = 0, b1 = 0, o = 0;
				while (a1 < na && b1 < m2 && o < 32) {
					av_us s = acc[a1].start > r[b1].start ? acc[a1].start : r[b1].start;
					av_us e = acc[a1].end < r[b1].end ? acc[a1].end : r[b1].end;
					if (s < e)
						tmp[o++] = (struct av_range) { s, e };
					if (acc[a1].end < r[b1].end)
						a1++;
					else
						b1++;
				}
				memcpy(acc, tmp, (size_t) o * sizeof tmp[0]);
				na = o;
			}
		}
		n = na > 0 ? na : 0;
		memcpy(r, acc, (size_t) n * sizeof r[0]);
	}
	for (i = 0; i < n; i++) {
		JS_SetPropertyUint32(ctx, a, (uint32_t) (2 * i), JS_NewFloat64(ctx, (double) r[i].start / 1e6));
		JS_SetPropertyUint32(ctx, a, (uint32_t) (2 * i + 1), JS_NewFloat64(ctx, (double) r[i].end / 1e6));
	}
	return a;
}

static JSValue n_md_offset(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_PLAYER;
	if (argc > 2)
		av_store_set_offset(av_player_store(m->p), qi(ctx, argc, argv, 1), us_of(qd(ctx, argc, argv, 2)));
	return JS_NewFloat64(ctx, (double) av_store_get_offset(av_player_store(m->p), qi(ctx, argc, argv, 1)) / 1e6);
}

static JSValue n_md_mode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_PLAYER;
	av_store_set_mode(av_player_store(m->p), qi(ctx, argc, argv, 1), qi(ctx, argc, argv, 2));
	return JS_UNDEFINED;
}

static JSValue n_md_window(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_PLAYER;
	av_store_set_window(av_player_store(m->p), qi(ctx, argc, argv, 1), us_of(qd(ctx, argc, argv, 2)),
			us_of(qd(ctx, argc, argv, 3)));
	return JS_UNDEFINED;
}

static JSValue n_md_abort(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_PLAYER;
	av_store_reset_parser(av_player_store(m->p), qi(ctx, argc, argv, 1));
	return JS_UNDEFINED;
}

static JSValue n_md_eos(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_PLAYER;
	av_store_set_eos(av_player_store(m->p), qi(ctx, argc, argv, 1));
	qm_schedule(m, 0);
	return JS_UNDEFINED;
}

/* mdInit(h, src) -> null (no init segment yet) or [[kind, codec, width, height, rate,
 * channels, decodable], ...] */
static JSValue n_md_init(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qmedia *m = argc > 0 ? qm_of(argv[0]) : NULL;
	struct av_store *s;
	int src = qi(ctx, argc, argv, 1), i, n;
	JSValue a;

	(void) this_val;
	if (m == NULL || m->p == NULL)
		return JS_NULL;
	s = av_player_store(m->p);
	if (!av_store_has_init(s, src))
		return JS_NULL;
	a = JS_NewArray(ctx);
	n = av_store_ntracks(s, src);
	for (i = 0; i < n; i++) {
		const struct av_track *t = av_store_track(s, src, i);
		JSValue e = JS_NewArray(ctx);
		int ok = 0;
		const char *c = t->codec_str[0] ? t->codec_str : t->codec_id;
		av_codec_of_string(c, &ok);
		if (t->encrypted || t->codec == AV_C_NONE)
			ok = 0;
		JS_SetPropertyUint32(ctx, e, 0, JS_NewInt32(ctx, t->kind));
		JS_SetPropertyUint32(ctx, e, 1, JS_NewString(ctx, c));
		JS_SetPropertyUint32(ctx, e, 2, JS_NewInt32(ctx, t->dwidth ? t->dwidth : t->width));
		JS_SetPropertyUint32(ctx, e, 3, JS_NewInt32(ctx, t->dheight ? t->dheight : t->height));
		JS_SetPropertyUint32(ctx, e, 4, JS_NewInt32(ctx, t->rate));
		JS_SetPropertyUint32(ctx, e, 5, JS_NewInt32(ctx, t->channels));
		JS_SetPropertyUint32(ctx, e, 6, JS_NewBool(ctx, ok));
		JS_SetPropertyUint32(ctx, a, (uint32_t) i, e);
	}
	return a;
}

static JSValue n_md_play(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_PLAYER;
	(void) ctx;
	av_player_play(m->p);
	qm_schedule(m, 0);
	return JS_UNDEFINED;
}

static JSValue n_md_pause(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_PLAYER;
	(void) ctx;
	av_player_pause(m->p);
	qm_schedule(m, 0);
	return JS_UNDEFINED;
}

static JSValue n_md_seek(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_PLAYER;
	av_player_seek(m->p, us_of(qd(ctx, argc, argv, 1)));
	m->st.time = us_of(qd(ctx, argc, argv, 1));
	m->st.seeking = 1;
	qm_schedule(m, 0);
	return JS_UNDEFINED;
}

static JSValue n_md_rate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_PLAYER;
	av_player_set_rate(m->p, qd(ctx, argc, argv, 1));
	return JS_UNDEFINED;
}

static JSValue n_md_volume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_ARG0;
	m->volume = (float) qd(ctx, argc, argv, 1);
	m->muted = qi(ctx, argc, argv, 2) != 0;
	if (m->p != NULL)
		av_player_set_volume(m->p, m->volume, m->muted);
	if (m->controls_shown)
		qm_redraw(m);
	return JS_UNDEFINED;
}

static JSValue n_md_duration(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_PLAYER;
	av_player_set_duration(m->p, us_of(qd(ctx, argc, argv, 1)));
	qm_schedule(m, 0);
	return JS_UNDEFINED;
}

/* mdState(h) -> the state array now (media.js' getters between two turns) */
static JSValue n_md_state(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qmedia *m = argc > 0 ? qm_of(argv[0]) : NULL;
	(void) this_val;
	if (m == NULL || m->p == NULL)
		return JS_NULL;
	return qm_state_array(ctx, m);
}

/* mdContainerDuration(h) -> the duration the containers give (s), 0 unknown */
static JSValue n_md_container_duration(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qmedia *m = argc > 0 ? qm_of(argv[0]) : NULL;
	(void) this_val;
	if (m == NULL || m->p == NULL)
		return JS_NewFloat64(ctx, 0);
	return JS_NewFloat64(ctx, (double) av_store_duration(av_player_store(m->p)) / 1e6);
}

/* mdControls(h, shown): the native controls drawn (the pointer over the video, a click) */
static JSValue n_md_controls(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QM_ARG0;
	m->controls_shown = argc > 1 && JS_ToBool(ctx, argv[1]);
	qm_redraw(m);
	return JS_UNDEFINED;
}

/* mdFrameInfo(h) -> [the shown frame's time (s), the sum of its R, G and B bytes, width,
 * height] or null (the tests: the pixels against the decoded reference) */
static JSValue n_md_frame_info(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qmedia *m = argc > 0 ? qm_of(argv[0]) : NULL;
	struct bitmap *bm = NULL;
	const struct bitmap_colour_layout L = bitmap_layout;
	const uint8_t *px;
	size_t stride;
	double sum = 0;
	int w, h, x, y;
	JSValue a;

	(void) this_val;
	if (m == NULL || m->node == NULL || dom_node_get_user_data(m->node,
			corestring_dom___ns_key_canvas_node_data, (void **) &bm) != DOM_NO_ERR || bm == NULL)
		return JS_NULL;
	px = guit->bitmap->get_buffer(bm);
	stride = guit->bitmap->get_rowstride(bm);
	w = guit->bitmap->get_width(bm);
	h = guit->bitmap->get_height(bm);
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			const uint8_t *p = px + (size_t) y * stride + 4 * (size_t) x;
			sum += p[L.r] + p[L.g] + p[L.b];
		}
	a = JS_NewArray(ctx);
	JS_SetPropertyUint32(ctx, a, 0, JS_NewFloat64(ctx, (double) m->shown_pts / 1e6));
	JS_SetPropertyUint32(ctx, a, 1, JS_NewFloat64(ctx, sum));
	JS_SetPropertyUint32(ctx, a, 2, JS_NewInt32(ctx, w));
	JS_SetPropertyUint32(ctx, a, 3, JS_NewInt32(ctx, h));
	return a;
}

/* mdType(mime, mse) -> 0 / 1 maybe / 2 probably */
static JSValue n_md_type(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const char *s = argc > 0 ? JS_ToCString(ctx, argv[0]) : NULL;
	int r = 0;

	(void) this_val;
	if (s != NULL) {
		r = av_type_supported(s, qi(ctx, argc, argv, 1), NULL);
		JS_FreeCString(ctx, s);
	}
	return JS_NewInt32(ctx, r);
}

/* mdSmooth(mime, mse) -> [supported 0..2, smooth] (MediaCapabilities) */
static JSValue n_md_smooth(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const char *s = argc > 0 ? JS_ToCString(ctx, argv[0]) : NULL;
	int r = 0, smooth = 0;
	JSValue a = JS_NewArray(ctx);

	(void) this_val;
	if (s != NULL) {
		r = av_type_supported(s, qi(ctx, argc, argv, 1), &smooth);
		JS_FreeCString(ctx, s);
	}
	JS_SetPropertyUint32(ctx, a, 0, JS_NewInt32(ctx, r));
	JS_SetPropertyUint32(ctx, a, 1, JS_NewBool(ctx, smooth));
	return a;
}

/* mdCodecs() -> "name (library), ..." (about:, the tests) */
static JSValue n_md_codecs(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	char buf[512];
	size_t o = 0;
	int n, i;
	const struct av_codec_info *c = av_codec_list(&n);

	(void) this_val;
	(void) argc;
	(void) argv;
	buf[0] = 0;
	for (i = 0; i < n && o < sizeof buf - 64; i++)
		o += (size_t) snprintf(buf + o, sizeof buf - o, "%s%s (%s)", i ? ", " : "", c[i].name, c[i].library);
	return JS_NewString(ctx, buf);
}

static const JSCFunctionListEntry qm_natives[] = {
	JS_CFUNC_DEF("mdNew", 1, n_md_new),
	JS_CFUNC_DEF("mdOpen", 1, n_md_open),
	JS_CFUNC_DEF("mdClose", 1, n_md_close),
	JS_CFUNC_DEF("mdClear", 1, n_md_clear),
	JS_CFUNC_DEF("mdWatch", 3, n_md_watch),
	JS_CFUNC_DEF("mdFrames", 2, n_md_frames),
	JS_CFUNC_DEF("mdAddSource", 2, n_md_add_source),
	JS_CFUNC_DEF("mdRemoveSource", 2, n_md_remove_source),
	JS_CFUNC_DEF("mdChangeType", 3, n_md_change_type),
	JS_CFUNC_DEF("mdAppend", 3, n_md_append),
	JS_CFUNC_DEF("mdFeed", 4, n_md_feed),
	JS_CFUNC_DEF("mdFeedEnd", 2, n_md_feed_end),
	JS_CFUNC_DEF("mdWant", 2, n_md_want),
	JS_CFUNC_DEF("mdAhead", 1, n_md_ahead),
	JS_CFUNC_DEF("mdRemove", 4, n_md_remove),
	JS_CFUNC_DEF("mdBuffered", 2, n_md_buffered),
	JS_CFUNC_DEF("mdOffset", 3, n_md_offset),
	JS_CFUNC_DEF("mdMode", 3, n_md_mode),
	JS_CFUNC_DEF("mdWindow", 4, n_md_window),
	JS_CFUNC_DEF("mdAbort", 2, n_md_abort),
	JS_CFUNC_DEF("mdEos", 2, n_md_eos),
	JS_CFUNC_DEF("mdInit", 2, n_md_init),
	JS_CFUNC_DEF("mdPlay", 1, n_md_play),
	JS_CFUNC_DEF("mdPause", 1, n_md_pause),
	JS_CFUNC_DEF("mdSeek", 2, n_md_seek),
	JS_CFUNC_DEF("mdRate", 2, n_md_rate),
	JS_CFUNC_DEF("mdVolume", 3, n_md_volume),
	JS_CFUNC_DEF("mdDuration", 2, n_md_duration),
	JS_CFUNC_DEF("mdState", 1, n_md_state),
	JS_CFUNC_DEF("mdContainerDuration", 1, n_md_container_duration),
	JS_CFUNC_DEF("mdControls", 2, n_md_controls),
	JS_CFUNC_DEF("mdType", 2, n_md_type),
	JS_CFUNC_DEF("mdSmooth", 2, n_md_smooth),
	JS_CFUNC_DEF("mdCodecs", 0, n_md_codecs),
	JS_CFUNC_DEF("mdFrameInfo", 1, n_md_frame_info),
};

/* ---- the layout's and the painting's side ------------------------------------------------ */

/* exported interface documented in qjs_media.h */
bool onyx_media_natural_size(struct dom_node *n, int *w, int *h)
{
	struct qmedia *m = qm_find(n);

	if (m == NULL || m->nat_w <= 0 || m->nat_h <= 0)
		return false;
	*w = m->nat_w;
	*h = m->nat_h;
	return true;
}

/* the boxes made since the last turn: their elements set up then (not during the box
 * construction: a script may change the DOM) */
static struct { JSContext *ctx; dom_node *node; } qm_boxes[16];
static int qm_nboxes;
static bool qm_boxes_scheduled;

static void qm_boxes_later(void *p)
{
	int i, n = qm_nboxes;

	(void) p;
	qm_boxes_scheduled = false;
	qm_nboxes = 0;
	for (i = 0; i < n; i++) {
		JSContext *ctx = qm_boxes[i].ctx;
		dom_node *node = qm_boxes[i].node;
		if (ctx != NULL && qm_find(node) == NULL && !qjs_ctx_closed(ctx)) {
			JSValue g = JS_GetGlobalObject(ctx);
			JSValue fn = JS_GetPropertyStr(ctx, g, "__onyxMediaBox");
			if (JS_IsFunction(ctx, fn)) {
				JSValue el = qjs_wrap_node(ctx, node);
				qjs_invoke(ctx, fn, 1, &el, "media box");
				JS_FreeValue(ctx, el);
			}
			JS_FreeValue(ctx, fn);
			JS_FreeValue(ctx, g);
		}
		dom_node_unref(node);
	}
}

/* exported interface documented in qjs_media.h */
void onyx_media_box_made(struct dom_node *n, struct html_content *c)
{
	JSContext *ctx;
	int i;

	if (c == NULL || qm_find(n) != NULL || c->jsthread == NULL)
		return;
	ctx = qjs_thread_context(c->jsthread);
	if (ctx == NULL || qm_nboxes == (int) (sizeof qm_boxes / sizeof qm_boxes[0]))
		return;
	for (i = 0; i < qm_nboxes; i++)
		if (qm_boxes[i].node == n)
			return;
	qm_boxes[qm_nboxes].ctx = ctx;
	qm_boxes[qm_nboxes].node = dom_node_ref(n);
	qm_nboxes++;
	if (!qm_boxes_scheduled) {
		qm_boxes_scheduled = true;
		guit->misc->schedule(0, qm_boxes_later, NULL);
	}
}

static void fill(const struct redraw_context *ctx, int x0, int y0, int x1, int y1, colour c)
{
	plot_style_t s = { .fill_type = PLOT_OP_TYPE_SOLID, .fill_colour = c };
	struct rect r = { x0, y0, x1, y1 };

	if (x1 > x0 && y1 > y0)
		ctx->plot->rectangle(ctx, &s, &r);
}

static void poly(const struct redraw_context *ctx, const int *p, unsigned n, colour c)
{
	plot_style_t s = { .fill_type = PLOT_OP_TYPE_SOLID, .fill_colour = c };

	ctx->plot->polygon(ctx, &s, p, n);
}

static int fmt_time(char *b, size_t n, double s)
{
	long t = s > 0 && s < 1e9 ? (long) s : 0;

	if (t >= 3600)
		return snprintf(b, n, "%ld:%02ld:%02ld", t / 3600, (t / 60) % 60, t % 60);
	return snprintf(b, n, "%ld:%02ld", t / 60, t % 60);
}

/* the controls' bar at the bottom of the content box */
static void draw_controls(struct qmedia *m, int x, int y, int w, int h, float scale,
		const struct redraw_context *ctx)
{
	int bh = (int) (CTRL_H * scale), by = y + h - bh, cy = by + bh / 2;
	int s = (int) (scale * 100 + 0.5);
	const struct av_player_status *st = m != NULL ? &m->st : NULL;
	bool paused = st == NULL || st->paused || st->ended;
	double t = st != NULL ? (double) st->time / 1e6 : 0;
	double d = st != NULL && st->duration > 0 && st->duration != AV_NOTIME ? (double) st->duration / 1e6 : 0;
	int px0 = x + (w >= 300 * s / 100 ? 150 * s / 100 : 40 * s / 100), px1 = x + w - 80 * s / 100;
	colour fg = 0xffffff;

	if (bh > h)
		bh = h, by = y;
	fill(ctx, x, by, x + w, y + h, 0x202020);
	/* play / pause */
	if (paused) {
		int p[6] = { x + 12 * s / 100, cy - 9 * s / 100, x + 12 * s / 100, cy + 9 * s / 100, x + 28 * s / 100, cy };
		poly(ctx, p, 3, fg);
	} else {
		fill(ctx, x + 12 * s / 100, cy - 9 * s / 100, x + 18 * s / 100, cy + 9 * s / 100, fg);
		fill(ctx, x + 22 * s / 100, cy - 9 * s / 100, x + 28 * s / 100, cy + 9 * s / 100, fg);
	}
	/* the time */
	if (w >= 300 * s / 100) {
		char b[48], b2[20];
		plot_font_style_t fs = { .family = PLOT_FONT_FAMILY_SANS_SERIF,
			.size = (plot_style_fixed) (10 * PLOT_STYLE_SCALE * scale), .weight = 400,
			.flags = FONTF_NONE, .background = 0x202020, .foreground = fg };
		fmt_time(b, sizeof b, t);
		fmt_time(b2, sizeof b2, d);
		strncat(b, " / ", sizeof b - strlen(b) - 1);
		strncat(b, b2, sizeof b - strlen(b) - 1);
		ctx->plot->text(ctx, &fs, x + 40 * s / 100, cy + 5 * s / 100, b, strlen(b));
	}
	/* the progress: buffered in grey, played in red */
	if (px1 > px0 + 10) {
		int pw = px1 - px0;
		fill(ctx, px0, cy - 2 * s / 100, px1, cy + 2 * s / 100, 0x606060);
		if (d > 0) {
			int pp = (int) (pw * (t / d));
			if (pp > pw)
				pp = pw;
			fill(ctx, px0, cy - 2 * s / 100, px0 + pp, cy + 2 * s / 100, 0x0000ff);
			fill(ctx, px0 + pp - 5 * s / 100, cy - 5 * s / 100, px0 + pp + 5 * s / 100, cy + 5 * s / 100, 0x0000ff);
		}
	}
	/* the volume: a speaker, crossed when muted */
	{
		int vx = x + w - 70 * s / 100;
		int p[8] = { vx, cy - 4 * s / 100, vx + 6 * s / 100, cy - 4 * s / 100, vx + 12 * s / 100, cy - 9 * s / 100,
			vx + 12 * s / 100, cy + 9 * s / 100 };
		int q[8] = { vx + 12 * s / 100, cy + 9 * s / 100, vx + 6 * s / 100, cy + 4 * s / 100, vx, cy + 4 * s / 100,
			vx, cy - 4 * s / 100 };
		poly(ctx, p, 4, fg);
		poly(ctx, q, 4, fg);
		if (m != NULL && (m->muted || m->volume <= 0)) {
			fill(ctx, vx + 16 * s / 100, cy - 1 * s / 100, vx + 26 * s / 100, cy + 1 * s / 100, 0x0000ff);
		} else {
			fill(ctx, vx + 16 * s / 100, cy - 5 * s / 100, vx + 18 * s / 100, cy + 5 * s / 100, fg);
			fill(ctx, vx + 21 * s / 100, cy - 8 * s / 100, vx + 23 * s / 100, cy + 8 * s / 100, fg);
		}
	}
	/* full screen: four corners */
	{
		int fx = x + w - 32 * s / 100, a = 7 * s / 100, l = 2 * s / 100;
		fill(ctx, fx, cy - 9 * s / 100, fx + a, cy - 9 * s / 100 + l, fg);
		fill(ctx, fx, cy - 9 * s / 100, fx + l, cy - 9 * s / 100 + a, fg);
		fill(ctx, fx + 18 * s / 100 - a, cy - 9 * s / 100, fx + 18 * s / 100, cy - 9 * s / 100 + l, fg);
		fill(ctx, fx + 18 * s / 100 - l, cy - 9 * s / 100, fx + 18 * s / 100, cy - 9 * s / 100 + a, fg);
		fill(ctx, fx, cy + 9 * s / 100 - l, fx + a, cy + 9 * s / 100, fg);
		fill(ctx, fx, cy + 9 * s / 100 - a, fx + l, cy + 9 * s / 100, fg);
		fill(ctx, fx + 18 * s / 100 - a, cy + 9 * s / 100 - l, fx + 18 * s / 100, cy + 9 * s / 100, fg);
		fill(ctx, fx + 18 * s / 100 - l, cy + 9 * s / 100 - a, fx + 18 * s / 100, cy + 9 * s / 100, fg);
	}
}

/* exported interface documented in qjs_media.h */
bool onyx_media_redraw(struct dom_node *n, int x, int y, int w, int h, float scale,
		const struct rect *clip, const struct redraw_context *ctx)
{
	struct qmedia *m = qm_find(n);
	struct bitmap *bm = NULL;
	bool controls = false, is_video = true;
	dom_html_element_type tag = DOM_HTML_ELEMENT_TYPE__UNKNOWN;
	static dom_string *s_controls;

	(void) clip;
	if (dom_html_element_get_tag_type(n, &tag) == DOM_NO_ERR)
		is_video = tag == DOM_HTML_ELEMENT_TYPE_VIDEO;
	if (s_controls == NULL)
		dom_string_create((const uint8_t *) "controls", 8, &s_controls);
	if (s_controls == NULL || dom_element_has_attribute(n, s_controls, &controls) != DOM_NO_ERR)
		controls = false;
	if (is_video && dom_node_get_user_data(n, corestring_dom___ns_key_canvas_node_data,
			(void **) &bm) == DOM_NO_ERR && bm != NULL) {
		/* the frame fitted in the box (object-fit: contain), centred */
		int bw = guit->bitmap->get_width(bm), bh = guit->bitmap->get_height(bm);
		int nw = m != NULL && m->nat_w > 0 ? m->nat_w : bw, nh = m != NULL && m->nat_h > 0 ? m->nat_h : bh;
		int dw = w, dh = h;
		if (nw > 0 && nh > 0) {
			if ((long) w * nh > (long) h * nw)
				dw = (int) ((long) h * nw / nh);
			else
				dh = (int) ((long) w * nh / nw);
		}
		if (dw > 0 && dh > 0 &&
		    ctx->plot->bitmap(ctx, bm, x + (w - dw) / 2, y + (h - dh) / 2, dw, dh, 0, BITMAPF_NONE) != NSERROR_OK)
			return false;
	}
	if (controls && (!is_video || m == NULL || m->controls_shown || m->st.paused || m->p == NULL))
		draw_controls(m, x, y, w, h, scale > 0 ? scale : 1, ctx);
	return true;
}

/* ---- set up / gone ------------------------------------------------------------------------- */

static uint8_t *qjs_media_bc;
static size_t qjs_media_bc_len;

/* exported interface documented in qjs_media.h */
void qjs_media_setup(JSContext *ctx, JSValueConst natives)
{
	JSRuntime *rt = JS_GetRuntime(ctx);
	JSValue fn, r;

	if (qm_class == 0)
		JS_NewClassID(rt, &qm_class);
	if (!JS_IsRegisteredClass(rt, qm_class))
		JS_NewClass(rt, qm_class, &qm_classdef);
	JS_SetPropertyFunctionList(ctx, natives, qm_natives, sizeof(qm_natives) / sizeof(qm_natives[0]));
#ifdef ONYX_HOST_SIM
	/* (the PC bench: NS_MEDIASTUB=1 -- the codecs not built in decoded into grey frames and
	 * silence: a site's media path runs on its real streams, user/av/av_stub.c) */
	if (getenv("NS_MEDIASTUB") != NULL)
		av_codec_enable_stubs();
#endif
	if (getenv("NS_MEDIADEBUG") != NULL) {
		/* (the media's steps on the console: media.js' debug()) */
		JSValue g = JS_GetGlobalObject(ctx);
		JS_SetPropertyStr(ctx, g, "__onyxMediaDebug", JS_TRUE);
		JS_FreeValue(ctx, g);
	}
	fn = qjs_eval_cached(ctx, qjs_media_js, sizeof(qjs_media_js) - 1, "media.js",
			&qjs_media_bc, &qjs_media_bc_len);
	if (JS_IsException(fn)) {
		JSValue e = JS_GetException(ctx);
		const char *msg = JS_ToCString(ctx, e);
		NSLOG(netsurf, INFO, "media.js: %s", msg ? msg : "?");
		if (getenv("NS_JSDEBUG"))
			fprintf(stderr, "JS media.js: %s\n", msg ? msg : "?");
		if (msg)
			JS_FreeCString(ctx, msg);
		JS_FreeValue(ctx, e);
		return;
	}
	r = JS_Call(ctx, fn, JS_UNDEFINED, 1, &natives);
	if (JS_IsException(r)) {
		JSValue e = JS_GetException(ctx);
		const char *msg = JS_ToCString(ctx, e);
		NSLOG(netsurf, INFO, "media.js setup: %s", msg ? msg : "?");
		if (getenv("NS_JSDEBUG"))
			fprintf(stderr, "JS media.js setup: %s\n", msg ? msg : "?");
		if (msg)
			JS_FreeCString(ctx, msg);
		JS_FreeValue(ctx, e);
	}
	JS_FreeValue(ctx, r);
	JS_FreeValue(ctx, fn);
}

/* exported interface documented in qjs_media.h */
void qjs_media_context_gone(JSContext *ctx)
{
	struct qmedia *m;
	int i;

	for (i = 0; i < qm_nboxes; i++)
		if (qm_boxes[i].ctx == ctx)
			qm_boxes[i].ctx = NULL;
	for (m = qm_all; m != NULL; m = m->next) {
		JSValue tick;
		if (m->ctx != ctx)
			continue;
		qm_stop(m);
		tick = m->tick;
		m->tick = JS_UNDEFINED;
		m->ctx = NULL;
		JS_FreeValue(ctx, tick);
	}
}
