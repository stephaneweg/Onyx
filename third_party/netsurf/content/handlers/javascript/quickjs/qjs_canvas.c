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
 * Onyx: <canvas> 2D for the scripts, on PlutoVG (third_party/plutovg-1.3.3).
 *
 * canvas.js (compiled in as qjs_canvas_js.h) is the API -- HTMLCanvasElement.getContext,
 * CanvasRenderingContext2D, CanvasGradient, CanvasPattern, Path2D, ImageData,
 * OffscreenCanvas, TextMetrics, DOMMatrix-like transforms -- its state (styles, fonts,
 * the save stack) kept in JavaScript; the natives here are thin: each draws on the
 * canvas' PlutoVG surface (premultiplied ARGB, anti-aliased).
 *
 * A canvas' path is kept in device space, each point transformed by the transform of
 * the moment it is added (as the HTML canvas does); a fill draws it with the identity,
 * a stroke draws it mapped back through the current transform (the line width scales
 * with it). Gradients and patterns are in the user space of the fill.
 *
 * The element's picture is a NetSurf bitmap, the node's user data under
 * __ns_key_canvas_node_data (redraw.c plots it into the canvas' box): after drawing, the
 * surface is copied into it (straight alpha, the client's layout) once a turn of the
 * main loop -- a scheduled flush -- and the box redrawn.
 *
 * drawImage takes a canvas, an <img> NetSurf has fetched (its content's bitmap), an SVG
 * image, or an Image a script made and never put in the page (loaded here through the
 * high-level cache: cvLoadImage). Text is drawn with the card's fonts (Liberation,
 * DejaVu, Gelasio, Selawik: the families of font_freetype.c; image/onyx_vgfont.c), read
 * by PlutoVG's stb_truetype on first use.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <stdio.h>

#include <dom/dom.h>
#include <plutovg.h>

#include "quickjs.h"

#include "utils/utils.h"
#include "utils/log.h"
#include "utils/nsurl.h"
#include "utils/corestrings.h"
#include "netsurf/bitmap.h"
#include "netsurf/content.h"
#include "netsurf/misc.h"
#include "content/content.h"
#include "content/hlcache.h"
#include "desktop/gui_internal.h"
#include "desktop/bitmap.h"
#include "html/private.h"
#include "html/box.h"

#include "image/onyx_vgfont.h"
#include "javascript/quickjs/qjs_canvas.h"
#include "qjs_canvas_js.h"	/* canvas.js, as a C string (the build makes it) */

/** the largest canvas (pixels): bigger ones are refused (a script's mistake, memory) */
#define QCV_MAX_PIXELS (4096 * 4096)

struct qcanvas {
	struct qcanvas *next;		/* the live canvases (qcv_all) */
	JSContext *ctx;			/* NULL once its document's scripts are gone */
	int width, height;
	plutovg_surface_t *surface;
	plutovg_canvas_t *canvas;
	plutovg_path_t *path;		/* the current path, in device space */
	plutovg_path_t *saved[4];	/* Path2D: the current path put aside */
	int nsaved;
	dom_node *node;			/* its <canvas> (NULL: an OffscreenCanvas) */
	bool scheduled;			/* a flush is due */
	plutovg_font_face_t *face;	/* the font (a qcv_faces entry) */
	float font_size;
};

enum { QP_LINEAR, QP_RADIAL, QP_CONIC, QP_PATTERN };

struct qpaint {
	int kind;
	float a[6];			/* the gradient's geometry */
	plutovg_gradient_stop_t *stops;
	int nstops, capstops;
	plutovg_surface_t *surface;	/* a pattern's picture (a copy) */
	int repeat;			/* 0 no-repeat, 1 repeat (x / y: repeat) */
};

struct qimage {
	struct qimage *next;		/* the live images (qimg_all) */
	JSContext *ctx;
	hlcache_handle *handle;
	JSValue cb;			/* cb(ok, width, height) */
	bool done;
};

static JSClassID qcv_class, qpaint_class, qimg_class;
static struct qcanvas *qcv_all;
static struct qimage *qimg_all;


/* ---- pixels -------------------------------------------------------------------------- */

/** PlutoVG's premultiplied 0xAARRGGBB words into a NetSurf bitmap of the same size */
static void qcv_to_bitmap(const plutovg_surface_t *s, struct bitmap *bm)
{
	int w = plutovg_surface_get_width(s), h = plutovg_surface_get_height(s);
	int sstride = plutovg_surface_get_stride(s);
	const uint8_t *src = plutovg_surface_get_data(s);
	uint8_t *dst = guit->bitmap->get_buffer(bm);
	size_t dstride = guit->bitmap->get_rowstride(bm);
	const struct bitmap_colour_layout L = bitmap_layout;
	bool pma = bitmap_fmt.pma;
	int x, y;

	if (dst == NULL)
		return;
	for (y = 0; y < h; y++) {
		const uint32_t *sp = (const uint32_t *) (const void *) (src + (size_t) y * sstride);
		uint8_t *dp = dst + (size_t) y * dstride;

		for (x = 0; x < w; x++, dp += 4) {
			uint32_t p = sp[x];
			uint32_t a = p >> 24, r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;

			if (!pma && a != 255 && a != 0) {
				r = r * 255 / a;
				g = g * 255 / a;
				b = b * 255 / a;
			}
			dp[L.r] = r;
			dp[L.g] = g;
			dp[L.b] = b;
			dp[L.a] = a;
		}
	}
	guit->bitmap->modified(bm);
}

/** a NetSurf bitmap as a new PlutoVG surface (premultiplied) */
static plutovg_surface_t *qcv_from_bitmap(struct bitmap *bm)
{
	int w = guit->bitmap->get_width(bm), h = guit->bitmap->get_height(bm);
	const uint8_t *src = guit->bitmap->get_buffer(bm);
	size_t sstride = guit->bitmap->get_rowstride(bm);
	const struct bitmap_colour_layout L = bitmap_layout;
	bool pma = bitmap_fmt.pma;
	bool opaque = guit->bitmap->get_opaque(bm);
	plutovg_surface_t *s;
	uint8_t *dst;
	int dstride, x, y;

	if (src == NULL || w <= 0 || h <= 0 || (int64_t) w * h > QCV_MAX_PIXELS)
		return NULL;
	s = plutovg_surface_create(w, h);
	if (s == NULL)
		return NULL;
	dst = plutovg_surface_get_data(s);
	dstride = plutovg_surface_get_stride(s);
	for (y = 0; y < h; y++) {
		const uint8_t *sp = src + (size_t) y * sstride;
		uint32_t *dp = (uint32_t *) (void *) (dst + (size_t) y * dstride);

		for (x = 0; x < w; x++, sp += 4) {
			uint32_t a = opaque ? 255 : sp[L.a], r = sp[L.r], g = sp[L.g], b = sp[L.b];

			if (!pma && a != 255) {
				r = r * a / 255;
				g = g * a / 255;
				b = b * a / 255;
			}
			dp[x] = (a << 24) | (r << 16) | (g << 8) | b;
		}
	}
	return s;
}

/* A few images' surfaces kept (a game draws the same sprites every frame): by bitmap,
 * checked against a sample of its pixels (a bitmap freed and another made there). */
#define QCV_IMGCACHE 8
static struct {
	struct bitmap *bm;
	int w, h;
	uint32_t sum;
	plutovg_surface_t *s;
	unsigned int used;
} qcv_imgcache[QCV_IMGCACHE];
static unsigned int qcv_imgclock;

static uint32_t qcv_sample(struct bitmap *bm)
{
	const uint8_t *p = guit->bitmap->get_buffer(bm);
	size_t stride = guit->bitmap->get_rowstride(bm);
	int w = guit->bitmap->get_width(bm), h = guit->bitmap->get_height(bm), i;
	uint32_t sum = 2166136261u;

	if (p == NULL)
		return 0;
	for (i = 0; i < 32; i++) {
		int x = (int) ((uint32_t) (i * 2654435761u) % (uint32_t) w);
		int y = (int) ((uint32_t) (i * 40503u + 7) % (uint32_t) h);
		const uint8_t *q = p + (size_t) y * stride + (size_t) x * 4;

		sum = (sum ^ (q[0] | q[1] << 8 | q[2] << 16 | (uint32_t) q[3] << 24)) * 16777619u;
	}
	return sum;
}

/** the surface of a bitmap (kept in the cache: not the caller's to destroy) */
static plutovg_surface_t *qcv_bitmap_surface(struct bitmap *bm)
{
	int w = guit->bitmap->get_width(bm), h = guit->bitmap->get_height(bm), i, slot = 0;
	uint32_t sum = qcv_sample(bm);

	qcv_imgclock++;
	for (i = 0; i < QCV_IMGCACHE; i++) {
		if (qcv_imgcache[i].s != NULL && qcv_imgcache[i].bm == bm &&
		    qcv_imgcache[i].w == w && qcv_imgcache[i].h == h &&
		    qcv_imgcache[i].sum == sum) {
			qcv_imgcache[i].used = qcv_imgclock;
			return qcv_imgcache[i].s;
		}
		if (qcv_imgcache[i].s == NULL ||
		    (qcv_imgcache[slot].s != NULL && qcv_imgcache[i].used < qcv_imgcache[slot].used))
			slot = i;
	}
	if (qcv_imgcache[slot].s != NULL)
		plutovg_surface_destroy(qcv_imgcache[slot].s);
	qcv_imgcache[slot].s = qcv_from_bitmap(bm);
	qcv_imgcache[slot].bm = bm;
	qcv_imgcache[slot].w = w;
	qcv_imgcache[slot].h = h;
	qcv_imgcache[slot].sum = sum;
	qcv_imgcache[slot].used = qcv_imgclock;
	return qcv_imgcache[slot].s;
}


/* ---- the element's picture ------------------------------------------------------------- */

static void qcv_user_data_handler(dom_node_operation operation, dom_string *key, void *data,
		struct dom_node *src, struct dom_node *dst)
{
	(void) src;
	(void) dst;
	if (data == NULL || !dom_string_isequal(key, corestring_dom___ns_key_canvas_node_data))
		return;
	if (operation == DOM_NODE_DELETED)
		guit->bitmap->destroy(data);
}

/** the surface copied into the element's bitmap, its box redrawn */
static void qcv_flush(void *p)
{
	struct qcanvas *c = p;
	struct bitmap *bm = NULL, *old = NULL;
	html_content *htmlc;
	struct box *box = NULL;

	c->scheduled = false;
	if (c->node == NULL || c->surface == NULL || c->ctx == NULL)
		return;
	if (dom_node_get_user_data(c->node, corestring_dom___ns_key_canvas_node_data,
			(void **) &bm) != DOM_NO_ERR)
		bm = NULL;
	if (bm == NULL || guit->bitmap->get_width(bm) != c->width ||
	    guit->bitmap->get_height(bm) != c->height) {
		bm = guit->bitmap->create(c->width, c->height, BITMAP_NONE);
		if (bm == NULL)
			return;
		if (dom_node_set_user_data(c->node, corestring_dom___ns_key_canvas_node_data,
				bm, qcv_user_data_handler, (void **) &old) != DOM_NO_ERR) {
			guit->bitmap->destroy(bm);
			return;
		}
		if (old != NULL && old != bm)
			guit->bitmap->destroy(old);
	}
	qcv_to_bitmap(c->surface, bm);

	htmlc = qjs_html_of(c->ctx);
	if (htmlc != NULL &&
	    dom_node_get_user_data(c->node, corestring_dom___ns_key_box_node_data,
			(void **) &box) == DOM_NO_ERR && box != NULL)
		html__redraw_a_box(htmlc, box);
}

/** drawn: the element's picture made again at the next turn */
static void qcv_dirty(struct qcanvas *c)
{
	if (c->node != NULL && !c->scheduled && c->ctx != NULL) {
		c->scheduled = true;
		guit->misc->schedule(0, qcv_flush, c);
	}
}


/* ---- the canvas object ------------------------------------------------------------------ */

static void qcv_unlink(struct qcanvas *c)
{
	struct qcanvas **l;

	for (l = &qcv_all; *l != NULL; l = &(*l)->next) {
		if (*l == c) {
			*l = c->next;
			break;
		}
	}
}

static void qcv_free_surface(struct qcanvas *c)
{
	if (c->canvas != NULL)
		plutovg_canvas_destroy(c->canvas);
	if (c->surface != NULL)
		plutovg_surface_destroy(c->surface);
	c->canvas = NULL;
	c->surface = NULL;
}

/** a new surface of w x h (transparent), the state reset */
static bool qcv_make_surface(struct qcanvas *c, int w, int h)
{
	qcv_free_surface(c);
	if (w < 1 || h < 1 || (int64_t) w * h > QCV_MAX_PIXELS) {
		/* (a 0-sized canvas: a 1 x 1 surface, never shown) */
		w = h = 1;
	}
	c->surface = plutovg_surface_create(w, h);
	if (c->surface == NULL)
		return false;
	c->canvas = plutovg_canvas_create(c->surface);
	if (c->canvas == NULL) {
		qcv_free_surface(c);
		return false;
	}
	c->width = w;
	c->height = h;
	plutovg_path_reset(c->path);
	return true;
}

static void qcv_finalizer(JSRuntime *rt, JSValue val)
{
	struct qcanvas *c = JS_GetOpaque(val, qcv_class);
	int i;

	(void) rt;
	if (c == NULL)
		return;
	if (c->scheduled)
		guit->misc->schedule(-1, qcv_flush, c);
	qcv_unlink(c);
	qcv_free_surface(c);
	plutovg_path_destroy(c->path);
	for (i = 0; i < c->nsaved; i++)
		plutovg_path_destroy(c->saved[i]);
	if (c->node != NULL)
		dom_node_unref(c->node);
	free(c);
}

static JSClassDef qcv_classdef = { .class_name = "OnyxCanvas", .finalizer = qcv_finalizer };

static void qpaint_finalizer(JSRuntime *rt, JSValue val)
{
	struct qpaint *p = JS_GetOpaque(val, qpaint_class);

	(void) rt;
	if (p == NULL)
		return;
	free(p->stops);
	if (p->surface != NULL)
		plutovg_surface_destroy(p->surface);
	free(p);
}

static JSClassDef qpaint_classdef = { .class_name = "OnyxPaint", .finalizer = qpaint_finalizer };

static void qimg_release(struct qimage *im)
{
	if (im->handle != NULL) {
		hlcache_handle *h = im->handle;

		im->handle = NULL;
		hlcache_handle_release(h);
	}
}

static void qimg_finalizer(JSRuntime *rt, JSValue val)
{
	struct qimage *im = JS_GetOpaque(val, qimg_class);
	struct qimage **l;

	if (im == NULL)
		return;
	for (l = &qimg_all; *l != NULL; l = &(*l)->next) {
		if (*l == im) {
			*l = im->next;
			break;
		}
	}
	qimg_release(im);
	JS_FreeValueRT(rt, im->cb);
	free(im);
}

static JSClassDef qimg_classdef = { .class_name = "OnyxImage", .finalizer = qimg_finalizer };

#define QCV_ARG(var)							\
	struct qcanvas *var = argc > 0 ? JS_GetOpaque(argv[0], qcv_class) : NULL; \
	if (var == NULL || var->canvas == NULL)				\
		return JS_UNDEFINED

static float qf(JSContext *ctx, int argc, JSValueConst *argv, int i)
{
	double d = 0;

	if (i < argc)
		JS_ToFloat64(ctx, &d, argv[i]);
	return (float) d;
}

static int qi(JSContext *ctx, int argc, JSValueConst *argv, int i)
{
	int32_t v = 0;

	if (i < argc)
		JS_ToInt32(ctx, &v, argv[i]);
	return v;
}

/** whether the numbers are all finite (the canvas ignores a call with NaN / Infinity) */
static bool qfinite(JSContext *ctx, int argc, JSValueConst *argv, int from, int to)
{
	int i;

	for (i = from; i < to && i < argc; i++) {
		double d = 0;

		JS_ToFloat64(ctx, &d, argv[i]);
		if (!isfinite(d))
			return false;
	}
	return true;
}

/* cvNew(node | null, w, h) */
static JSValue n_cv_new(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qcanvas *c = calloc(1, sizeof(*c));
	JSValue obj;

	(void) this_val;
	if (c == NULL)
		return JS_ThrowOutOfMemory(ctx);
	c->ctx = ctx;
	c->path = plutovg_path_create();
	if (!qcv_make_surface(c, qi(ctx, argc, argv, 1), qi(ctx, argc, argv, 2))) {
		plutovg_path_destroy(c->path);
		free(c);
		return JS_NULL;
	}
	c->node = argc > 0 ? qjs_node_of(argv[0]) : NULL;
	if (c->node != NULL)
		dom_node_ref(c->node);
	obj = JS_NewObjectClass(ctx, qcv_class);
	if (JS_IsException(obj)) {
		c->ctx = NULL;
		qcv_free_surface(c);
		plutovg_path_destroy(c->path);
		if (c->node != NULL)
			dom_node_unref(c->node);
		free(c);
		return obj;
	}
	JS_SetOpaque(obj, c);
	c->next = qcv_all;
	qcv_all = c;
	return obj;
}

/* cvResize(c, w, h): a new, transparent surface, the state reset */
static JSValue n_cv_resize(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qcanvas *c = argc > 0 ? JS_GetOpaque(argv[0], qcv_class) : NULL;

	(void) this_val;
	if (c == NULL)
		return JS_UNDEFINED;
	qcv_make_surface(c, qi(ctx, argc, argv, 1), qi(ctx, argc, argv, 2));
	qcv_dirty(c);
	return JS_UNDEFINED;
}

/* cvColor(css) -> 0xAARRGGBB or null */
static JSValue n_cv_color(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	size_t len;
	const char *s;
	plutovg_color_t col;
	int n;

	(void) this_val;
	if (argc < 1 || !JS_IsString(argv[0]))
		return JS_NULL;
	s = JS_ToCStringLen(ctx, &len, argv[0]);
	if (s == NULL)
		return JS_NULL;
	while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t'))
		len--;
	{
		const char *p = s;

		while (len > 0 && (*p == ' ' || *p == '\t')) {
			p++;
			len--;
		}
		n = plutovg_color_parse(&col, p, (int) len);
		if (n != (int) len)
			n = 0;
	}
	JS_FreeCString(ctx, s);
	if (n == 0)
		return JS_NULL;
	return JS_NewFloat64(ctx, (double) plutovg_color_to_argb32(&col));
}

static void qcv_set_color(struct qcanvas *c, uint32_t argb)
{
	plutovg_color_t col;

	plutovg_color_init_argb32(&col, argb);
	plutovg_canvas_set_color(c->canvas, &col);
}

/** the paint (a colour number or a gradient / pattern) set on the canvas, in the user
 * space of the transform m */
static bool qcv_set_paint(JSContext *ctx, struct qcanvas *c, JSValueConst paint,
		const plutovg_matrix_t *m)
{
	struct qpaint *p;

	if (JS_IsNumber(paint)) {
		double d = 0;

		JS_ToFloat64(ctx, &d, paint);
		qcv_set_color(c, (uint32_t) d);
		return true;
	}
	p = JS_GetOpaque(paint, qpaint_class);
	if (p == NULL)
		return false;
	switch (p->kind) {
	case QP_LINEAR:
		if (p->nstops == 0)
			return false;
		plutovg_canvas_set_linear_gradient(c->canvas, p->a[0], p->a[1], p->a[2], p->a[3],
				PLUTOVG_SPREAD_METHOD_PAD, p->stops, p->nstops, m);
		return true;
	case QP_RADIAL:
		if (p->nstops == 0)
			return false;
		/* the canvas' gradient from circle 0 to circle 1: PlutoVG's focal circle to
		 * its circle */
		plutovg_canvas_set_radial_gradient(c->canvas, p->a[3], p->a[4], p->a[5],
				p->a[0], p->a[1], p->a[2], PLUTOVG_SPREAD_METHOD_PAD,
				p->stops, p->nstops, m);
		return true;
	case QP_CONIC:
		/* (PlutoVG has no conic gradient: its middle colour) */
		if (p->nstops == 0)
			return false;
		plutovg_canvas_set_color(c->canvas, &p->stops[p->nstops / 2].color);
		return true;
	case QP_PATTERN:
		if (p->surface == NULL)
			return false;
		plutovg_canvas_set_texture(c->canvas, p->surface, p->repeat ?
				PLUTOVG_TEXTURE_TYPE_TILED : PLUTOVG_TEXTURE_TYPE_PLAIN, 1.f, m);
		return true;
	}
	return false;
}

/* ---- state ----------------------------------------------------------------------------- */

static JSValue n_cv_line_width(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QCV_ARG(c);
	(void) this_val;
	plutovg_canvas_set_line_width(c->canvas, qf(ctx, argc, argv, 1));
	return JS_UNDEFINED;
}

static JSValue n_cv_line_cap(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	static const plutovg_line_cap_t caps[] = { PLUTOVG_LINE_CAP_BUTT,
		PLUTOVG_LINE_CAP_ROUND, PLUTOVG_LINE_CAP_SQUARE };
	int i;
	QCV_ARG(c);
	(void) this_val;
	i = qi(ctx, argc, argv, 1);
	plutovg_canvas_set_line_cap(c->canvas, caps[i >= 0 && i < 3 ? i : 0]);
	return JS_UNDEFINED;
}

static JSValue n_cv_line_join(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	static const plutovg_line_join_t joins[] = { PLUTOVG_LINE_JOIN_MITER,
		PLUTOVG_LINE_JOIN_ROUND, PLUTOVG_LINE_JOIN_BEVEL };
	int i;
	QCV_ARG(c);
	(void) this_val;
	i = qi(ctx, argc, argv, 1);
	plutovg_canvas_set_line_join(c->canvas, joins[i >= 0 && i < 3 ? i : 0]);
	return JS_UNDEFINED;
}

static JSValue n_cv_miter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QCV_ARG(c);
	(void) this_val;
	plutovg_canvas_set_miter_limit(c->canvas, qf(ctx, argc, argv, 1));
	return JS_UNDEFINED;
}

static JSValue n_cv_alpha(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QCV_ARG(c);
	(void) this_val;
	plutovg_canvas_set_opacity(c->canvas, qf(ctx, argc, argv, 1));
	return JS_UNDEFINED;
}

/* cvOp(c, i): globalCompositeOperation, as canvas.js numbers them */
static JSValue n_cv_op(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	static const plutovg_operator_t ops[] = {
		PLUTOVG_OPERATOR_SRC_OVER, PLUTOVG_OPERATOR_SRC_IN, PLUTOVG_OPERATOR_SRC_OUT,
		PLUTOVG_OPERATOR_SRC_ATOP, PLUTOVG_OPERATOR_DST_OVER, PLUTOVG_OPERATOR_DST_IN,
		PLUTOVG_OPERATOR_DST_OUT, PLUTOVG_OPERATOR_DST_ATOP, PLUTOVG_OPERATOR_XOR,
		PLUTOVG_OPERATOR_SRC, PLUTOVG_OPERATOR_CLEAR,
	};
	int i;
	QCV_ARG(c);
	(void) this_val;
	i = qi(ctx, argc, argv, 1);
	plutovg_canvas_set_operator(c->canvas,
			ops[i >= 0 && i < (int) (sizeof(ops) / sizeof(ops[0])) ? i : 0]);
	return JS_UNDEFINED;
}

/* cvDash(c, [segments], offset) */
static JSValue n_cv_dash(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	float dashes[64];
	int64_t n = 0, i;
	QCV_ARG(c);
	(void) this_val;
	if (argc > 1 && JS_IsArray(argv[1]))
		JS_GetLength(ctx, argv[1], &n);
	if (n > 64)
		n = 64;
	for (i = 0; i < n; i++) {
		JSValue v = JS_GetPropertyUint32(ctx, argv[1], (uint32_t) i);
		double d = 0;

		JS_ToFloat64(ctx, &d, v);
		JS_FreeValue(ctx, v);
		dashes[i] = (float) d;
	}
	plutovg_canvas_set_dash_array(c->canvas, dashes, (int) n);
	plutovg_canvas_set_dash_offset(c->canvas, qf(ctx, argc, argv, 2));
	return JS_UNDEFINED;
}

static JSValue n_cv_save(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QCV_ARG(c);
	(void) ctx;
	(void) this_val;
	plutovg_canvas_save(c->canvas);
	return JS_UNDEFINED;
}

static JSValue n_cv_restore(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QCV_ARG(c);
	(void) ctx;
	(void) this_val;
	plutovg_canvas_restore(c->canvas);
	return JS_UNDEFINED;
}

/* cvTransform(c, a, b, c, d, e, f, set): multiplied (transform) or set (setTransform) */
static JSValue n_cv_transform(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	plutovg_matrix_t m;
	QCV_ARG(c);
	(void) this_val;
	if (!qfinite(ctx, argc, argv, 1, 7))
		return JS_UNDEFINED;
	plutovg_matrix_init(&m, qf(ctx, argc, argv, 1), qf(ctx, argc, argv, 2),
			qf(ctx, argc, argv, 3), qf(ctx, argc, argv, 4),
			qf(ctx, argc, argv, 5), qf(ctx, argc, argv, 6));
	if (argc > 7 && JS_ToBool(ctx, argv[7]))
		plutovg_canvas_set_matrix(c->canvas, &m);
	else
		plutovg_canvas_transform(c->canvas, &m);
	return JS_UNDEFINED;
}

static JSValue n_cv_get_transform(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	plutovg_matrix_t m;
	JSValue a;
	QCV_ARG(c);
	(void) this_val;
	plutovg_canvas_get_matrix(c->canvas, &m);
	a = JS_NewArray(ctx);
	JS_SetPropertyUint32(ctx, a, 0, JS_NewFloat64(ctx, m.a));
	JS_SetPropertyUint32(ctx, a, 1, JS_NewFloat64(ctx, m.b));
	JS_SetPropertyUint32(ctx, a, 2, JS_NewFloat64(ctx, m.c));
	JS_SetPropertyUint32(ctx, a, 3, JS_NewFloat64(ctx, m.d));
	JS_SetPropertyUint32(ctx, a, 4, JS_NewFloat64(ctx, m.e));
	JS_SetPropertyUint32(ctx, a, 5, JS_NewFloat64(ctx, m.f));
	return a;
}


/* ---- the path ----------------------------------------------------------------------------- */

static void qcv_matrix(struct qcanvas *c, plutovg_matrix_t *m)
{
	plutovg_canvas_get_matrix(c->canvas, m);
}

static void qcv_map(struct qcanvas *c, float x, float y, float *dx, float *dy)
{
	plutovg_matrix_t m;

	qcv_matrix(c, &m);
	plutovg_matrix_map(&m, x, y, dx, dy);
}

/** a user-space path appended to the canvas' (through the transform); its first move
 * made a line when `connect` and the path has a point (arc, ellipse) */
static void qcv_append(struct qcanvas *c, const plutovg_path_t *src, bool connect)
{
	plutovg_path_iterator_t it;
	plutovg_point_t p[3];
	plutovg_matrix_t m;
	bool first = true;
	const plutovg_path_element_t *el;
	bool has_point = plutovg_path_get_elements(c->path, &el) > 0;

	qcv_matrix(c, &m);
	plutovg_path_iterator_init(&it, src);
	while (plutovg_path_iterator_has_next(&it)) {
		plutovg_path_command_t cmd = plutovg_path_iterator_next(&it, p);
		int n = cmd == PLUTOVG_PATH_COMMAND_CUBIC_TO ? 3 :
			cmd == PLUTOVG_PATH_COMMAND_CLOSE ? 0 : 1;

		plutovg_matrix_map_points(&m, p, p, n);
		switch (cmd) {
		case PLUTOVG_PATH_COMMAND_MOVE_TO:
			if (first && connect && has_point)
				plutovg_path_line_to(c->path, p[0].x, p[0].y);
			else
				plutovg_path_move_to(c->path, p[0].x, p[0].y);
			break;
		case PLUTOVG_PATH_COMMAND_LINE_TO:
			plutovg_path_line_to(c->path, p[0].x, p[0].y);
			break;
		case PLUTOVG_PATH_COMMAND_CUBIC_TO:
			plutovg_path_cubic_to(c->path, p[0].x, p[0].y, p[1].x, p[1].y,
					p[2].x, p[2].y);
			break;
		case PLUTOVG_PATH_COMMAND_CLOSE:
			plutovg_path_close(c->path);
			break;
		}
		first = false;
	}
}

static bool qcv_empty(struct qcanvas *c)
{
	const plutovg_path_element_t *el;

	return plutovg_path_get_elements(c->path, &el) == 0;
}

static JSValue n_cv_begin(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QCV_ARG(c);
	(void) ctx;
	(void) this_val;
	plutovg_path_reset(c->path);
	return JS_UNDEFINED;
}

static JSValue n_cv_move(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	float x, y;
	QCV_ARG(c);
	(void) this_val;
	if (!qfinite(ctx, argc, argv, 1, 3))
		return JS_UNDEFINED;
	qcv_map(c, qf(ctx, argc, argv, 1), qf(ctx, argc, argv, 2), &x, &y);
	plutovg_path_move_to(c->path, x, y);
	return JS_UNDEFINED;
}

static JSValue n_cv_line(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	float x, y;
	QCV_ARG(c);
	(void) this_val;
	if (!qfinite(ctx, argc, argv, 1, 3))
		return JS_UNDEFINED;
	qcv_map(c, qf(ctx, argc, argv, 1), qf(ctx, argc, argv, 2), &x, &y);
	if (qcv_empty(c))
		plutovg_path_move_to(c->path, x, y);
	else
		plutovg_path_line_to(c->path, x, y);
	return JS_UNDEFINED;
}

static JSValue n_cv_quad(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	float x1, y1, x, y;
	QCV_ARG(c);
	(void) this_val;
	if (!qfinite(ctx, argc, argv, 1, 5))
		return JS_UNDEFINED;
	qcv_map(c, qf(ctx, argc, argv, 1), qf(ctx, argc, argv, 2), &x1, &y1);
	qcv_map(c, qf(ctx, argc, argv, 3), qf(ctx, argc, argv, 4), &x, &y);
	if (qcv_empty(c))
		plutovg_path_move_to(c->path, x1, y1);
	plutovg_path_quad_to(c->path, x1, y1, x, y);
	return JS_UNDEFINED;
}

static JSValue n_cv_cubic(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	float x1, y1, x2, y2, x, y;
	QCV_ARG(c);
	(void) this_val;
	if (!qfinite(ctx, argc, argv, 1, 7))
		return JS_UNDEFINED;
	qcv_map(c, qf(ctx, argc, argv, 1), qf(ctx, argc, argv, 2), &x1, &y1);
	qcv_map(c, qf(ctx, argc, argv, 3), qf(ctx, argc, argv, 4), &x2, &y2);
	qcv_map(c, qf(ctx, argc, argv, 5), qf(ctx, argc, argv, 6), &x, &y);
	if (qcv_empty(c))
		plutovg_path_move_to(c->path, x1, y1);
	plutovg_path_cubic_to(c->path, x1, y1, x2, y2, x, y);
	return JS_UNDEFINED;
}

/* an elliptical arc (centre x, y, radii, rotation, angles) as a user-space path */
static void qcv_ellipse_path(plutovg_path_t *p, float x, float y, float rx, float ry,
		float rot, float a0, float a1, bool ccw)
{
	plutovg_path_t *unit = plutovg_path_create();
	plutovg_matrix_t m;

	plutovg_path_add_arc(unit, 0, 0, 1, a0, a1, ccw);
	plutovg_matrix_init_translate(&m, x, y);
	plutovg_matrix_rotate(&m, rot);
	plutovg_matrix_scale(&m, rx, ry);
	plutovg_path_add_path(p, unit, &m);
	plutovg_path_destroy(unit);
}

/* cvArc(c, x, y, r, a0, a1, ccw) and cvEllipse(c, x, y, rx, ry, rot, a0, a1, ccw) */
static JSValue n_cv_arc(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	plutovg_path_t *p;
	float r;
	QCV_ARG(c);
	(void) this_val;
	if (!qfinite(ctx, argc, argv, 1, 6))
		return JS_UNDEFINED;
	r = qf(ctx, argc, argv, 3);
	if (r < 0)
		return JS_ThrowRangeError(ctx, "negative radius");
	p = plutovg_path_create();
	plutovg_path_add_arc(p, qf(ctx, argc, argv, 1), qf(ctx, argc, argv, 2), r,
			qf(ctx, argc, argv, 4), qf(ctx, argc, argv, 5),
			argc > 6 && JS_ToBool(ctx, argv[6]));
	qcv_append(c, p, true);
	plutovg_path_destroy(p);
	return JS_UNDEFINED;
}

static JSValue n_cv_ellipse(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	plutovg_path_t *p;
	float rx, ry;
	QCV_ARG(c);
	(void) this_val;
	if (!qfinite(ctx, argc, argv, 1, 8))
		return JS_UNDEFINED;
	rx = qf(ctx, argc, argv, 3);
	ry = qf(ctx, argc, argv, 4);
	if (rx < 0 || ry < 0)
		return JS_ThrowRangeError(ctx, "negative radius");
	p = plutovg_path_create();
	qcv_ellipse_path(p, qf(ctx, argc, argv, 1), qf(ctx, argc, argv, 2), rx, ry,
			qf(ctx, argc, argv, 5), qf(ctx, argc, argv, 6), qf(ctx, argc, argv, 7),
			argc > 8 && JS_ToBool(ctx, argv[8]));
	qcv_append(c, p, true);
	plutovg_path_destroy(p);
	return JS_UNDEFINED;
}

/* cvArcTo(c, x1, y1, x2, y2, r): the arc tangent to the lines (current, 1) and (1, 2) */
static JSValue n_cv_arc_to(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	float x0, y0, x1, y1, x2, y2, r, dx, dy;
	plutovg_matrix_t m, inv;
	QCV_ARG(c);
	(void) this_val;
	if (!qfinite(ctx, argc, argv, 1, 6))
		return JS_UNDEFINED;
	x1 = qf(ctx, argc, argv, 1);
	y1 = qf(ctx, argc, argv, 2);
	x2 = qf(ctx, argc, argv, 3);
	y2 = qf(ctx, argc, argv, 4);
	r = qf(ctx, argc, argv, 5);
	if (r < 0)
		return JS_ThrowRangeError(ctx, "negative radius");
	qcv_matrix(c, &m);
	if (qcv_empty(c)) {
		plutovg_matrix_map(&m, x1, y1, &dx, &dy);
		plutovg_path_move_to(c->path, dx, dy);
		return JS_UNDEFINED;
	}
	/* the current point, in user space */
	plutovg_path_get_current_point(c->path, &dx, &dy);
	if (!plutovg_matrix_invert(&m, &inv))
		return JS_UNDEFINED;
	plutovg_matrix_map(&inv, dx, dy, &x0, &y0);
	{
		float ux = x0 - x1, uy = y0 - y1, vx = x2 - x1, vy = y2 - y1;
		float lu = hypotf(ux, uy), lv = hypotf(vx, vy);
		float cross = ux * vy - uy * vx;

		if (lu == 0 || lv == 0 || r == 0 || fabsf(cross) < 1e-6f * lu * lv) {
			plutovg_matrix_map(&m, x1, y1, &dx, &dy);
			plutovg_path_line_to(c->path, dx, dy);
			return JS_UNDEFINED;
		}
		ux /= lu; uy /= lu; vx /= lv; vy /= lv;
		{
			float cosang = ux * vx + uy * vy;
			float ang = acosf(fmaxf(-1.f, fminf(1.f, cosang)));
			float t = r / tanf(ang / 2);	/* from (1) to the tangent points */
			float tx0 = x1 + ux * t, ty0 = y1 + uy * t;
			float tx1 = x1 + vx * t, ty1 = y1 + vy * t;
			/* the centre: along the bisector */
			float bx = ux + vx, by = uy + vy, lb = hypotf(bx, by);
			float dcen = r / sinf(ang / 2);
			float cx = x1 + bx / lb * dcen, cy = y1 + by / lb * dcen;
			float a0 = atan2f(ty0 - cy, tx0 - cx), a1 = atan2f(ty1 - cy, tx1 - cx);
			bool ccw = cross > 0;
			plutovg_path_t *p = plutovg_path_create();

			plutovg_path_add_arc(p, cx, cy, r, a0, a1, ccw);
			qcv_append(c, p, true);
			plutovg_path_destroy(p);
		}
	}
	return JS_UNDEFINED;
}

static JSValue n_cv_rect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	plutovg_path_t *p;
	float x, y;
	QCV_ARG(c);
	(void) this_val;
	if (!qfinite(ctx, argc, argv, 1, 5))
		return JS_UNDEFINED;
	x = qf(ctx, argc, argv, 1);
	y = qf(ctx, argc, argv, 2);
	p = plutovg_path_create();
	plutovg_path_add_rect(p, x, y, qf(ctx, argc, argv, 3), qf(ctx, argc, argv, 4));
	plutovg_path_move_to(p, x, y);
	qcv_append(c, p, false);
	plutovg_path_destroy(p);
	return JS_UNDEFINED;
}

/* cvRoundRect(c, x, y, w, h, r): one radius (canvas.js takes the first of a list) */
static JSValue n_cv_round_rect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	plutovg_path_t *p;
	float x, y, w, h, r;
	QCV_ARG(c);
	(void) this_val;
	if (!qfinite(ctx, argc, argv, 1, 6))
		return JS_UNDEFINED;
	x = qf(ctx, argc, argv, 1);
	y = qf(ctx, argc, argv, 2);
	w = qf(ctx, argc, argv, 3);
	h = qf(ctx, argc, argv, 4);
	r = qf(ctx, argc, argv, 5);
	if (w < 0) { x += w; w = -w; }
	if (h < 0) { y += h; h = -h; }
	p = plutovg_path_create();
	plutovg_path_add_round_rect(p, x, y, w, h, r, r);
	plutovg_path_move_to(p, x, y);
	qcv_append(c, p, false);
	plutovg_path_destroy(p);
	return JS_UNDEFINED;
}

/* cvSvgPath(c, d): SVG path data (a Path2D made from a string) */
static JSValue n_cv_svg_path(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	plutovg_path_t *p;
	size_t len;
	const char *s;
	QCV_ARG(c);
	(void) this_val;
	if (argc < 2 || (s = JS_ToCStringLen(ctx, &len, argv[1])) == NULL)
		return JS_UNDEFINED;
	p = plutovg_path_create();
	if (plutovg_path_parse(p, s, (int) len))
		qcv_append(c, p, false);
	plutovg_path_destroy(p);
	JS_FreeCString(ctx, s);
	return JS_UNDEFINED;
}

static JSValue n_cv_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QCV_ARG(c);
	(void) ctx;
	(void) this_val;
	if (!qcv_empty(c))
		plutovg_path_close(c->path);
	return JS_UNDEFINED;
}

/* Path2D: cvPathPush(c) puts the path aside (a new one), cvPathPop(c) takes it back */
static JSValue n_cv_path_push(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QCV_ARG(c);
	(void) ctx;
	(void) this_val;
	if (c->nsaved < 4) {
		c->saved[c->nsaved++] = c->path;
		c->path = plutovg_path_create();
	} else {
		plutovg_path_reset(c->path);
	}
	return JS_UNDEFINED;
}

static JSValue n_cv_path_pop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QCV_ARG(c);
	(void) ctx;
	(void) this_val;
	if (c->nsaved > 0) {
		plutovg_path_destroy(c->path);
		c->path = c->saved[--c->nsaved];
	}
	return JS_UNDEFINED;
}


/* ---- drawing ------------------------------------------------------------------------------ */

/** the device-space path filled with the paint (the identity transform, the paint in the
 * current user space) */
static void qcv_fill_path(JSContext *ctx, struct qcanvas *c, const plutovg_path_t *path,
		JSValueConst paint, int rule)
{
	plutovg_matrix_t m;

	qcv_matrix(c, &m);
	if (!qcv_set_paint(ctx, c, paint, &m))
		return;
	plutovg_canvas_set_fill_rule(c->canvas, rule ? PLUTOVG_FILL_RULE_EVEN_ODD :
			PLUTOVG_FILL_RULE_NON_ZERO);
	plutovg_canvas_reset_matrix(c->canvas);
	plutovg_canvas_fill_path(c->canvas, path);
	plutovg_canvas_set_matrix(c->canvas, &m);
	plutovg_canvas_new_path(c->canvas);
	qcv_dirty(c);
}

/** the device-space path stroked in the current user space (its line width, dashes) */
static void qcv_stroke_path(JSContext *ctx, struct qcanvas *c, const plutovg_path_t *path,
		JSValueConst paint)
{
	plutovg_matrix_t m, inv, id;
	plutovg_path_t *user;

	qcv_matrix(c, &m);
	if (!plutovg_matrix_invert(&m, &inv))
		return;
	plutovg_matrix_init_identity(&id);
	if (!qcv_set_paint(ctx, c, paint, &id))
		return;
	user = plutovg_path_clone(path);
	if (user == NULL)
		return;
	plutovg_path_transform(user, &inv);
	plutovg_canvas_stroke_path(c->canvas, user);
	plutovg_canvas_new_path(c->canvas);
	plutovg_path_destroy(user);
	qcv_dirty(c);
}

/* cvFill(c, paint, evenodd) */
static JSValue n_cv_fill(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QCV_ARG(c);
	(void) this_val;
	if (argc > 1)
		qcv_fill_path(ctx, c, c->path, argv[1], qi(ctx, argc, argv, 2));
	return JS_UNDEFINED;
}

/* cvStroke(c, paint) */
static JSValue n_cv_stroke(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	QCV_ARG(c);
	(void) this_val;
	if (argc > 1)
		qcv_stroke_path(ctx, c, c->path, argv[1]);
	return JS_UNDEFINED;
}

/* cvClip(c, evenodd): the clip intersected with the path */
static JSValue n_cv_clip(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	plutovg_matrix_t m;
	QCV_ARG(c);
	(void) this_val;
	qcv_matrix(c, &m);
	plutovg_canvas_set_fill_rule(c->canvas, qi(ctx, argc, argv, 1) ?
			PLUTOVG_FILL_RULE_EVEN_ODD : PLUTOVG_FILL_RULE_NON_ZERO);
	plutovg_canvas_reset_matrix(c->canvas);
	plutovg_canvas_clip_path(c->canvas, c->path);
	plutovg_canvas_set_matrix(c->canvas, &m);
	plutovg_canvas_new_path(c->canvas);
	return JS_UNDEFINED;
}

/* a user-space rectangle as a device-space path */
static plutovg_path_t *qcv_rect_path(struct qcanvas *c, float x, float y, float w, float h)
{
	plutovg_path_t *p = plutovg_path_create();
	plutovg_matrix_t m;

	qcv_matrix(c, &m);
	plutovg_path_add_rect(p, x, y, w, h);
	plutovg_path_transform(p, &m);
	return p;
}

/* cvFillRect(c, paint, x, y, w, h), cvStrokeRect(...), cvClearRect(c, x, y, w, h) */
static JSValue n_cv_fill_rect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	plutovg_path_t *p;
	QCV_ARG(c);
	(void) this_val;
	if (argc < 6 || !qfinite(ctx, argc, argv, 2, 6))
		return JS_UNDEFINED;
	p = qcv_rect_path(c, qf(ctx, argc, argv, 2), qf(ctx, argc, argv, 3),
			qf(ctx, argc, argv, 4), qf(ctx, argc, argv, 5));
	qcv_fill_path(ctx, c, p, argv[1], 0);
	plutovg_path_destroy(p);
	return JS_UNDEFINED;
}

static JSValue n_cv_stroke_rect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	plutovg_path_t *p;
	QCV_ARG(c);
	(void) this_val;
	if (argc < 6 || !qfinite(ctx, argc, argv, 2, 6))
		return JS_UNDEFINED;
	p = qcv_rect_path(c, qf(ctx, argc, argv, 2), qf(ctx, argc, argv, 3),
			qf(ctx, argc, argv, 4), qf(ctx, argc, argv, 5));
	qcv_stroke_path(ctx, c, p, argv[1]);
	plutovg_path_destroy(p);
	return JS_UNDEFINED;
}

static JSValue n_cv_clear_rect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	plutovg_path_t *p;
	plutovg_operator_t op;
	float opacity;
	plutovg_matrix_t m;
	QCV_ARG(c);
	(void) this_val;
	if (!qfinite(ctx, argc, argv, 1, 5))
		return JS_UNDEFINED;
	p = qcv_rect_path(c, qf(ctx, argc, argv, 1), qf(ctx, argc, argv, 2),
			qf(ctx, argc, argv, 3), qf(ctx, argc, argv, 4));
	qcv_matrix(c, &m);
	op = plutovg_canvas_get_operator(c->canvas);
	opacity = plutovg_canvas_get_opacity(c->canvas);
	plutovg_canvas_set_operator(c->canvas, PLUTOVG_OPERATOR_CLEAR);
	plutovg_canvas_set_opacity(c->canvas, 1.f);
	plutovg_canvas_set_rgba(c->canvas, 0, 0, 0, 1);
	plutovg_canvas_set_fill_rule(c->canvas, PLUTOVG_FILL_RULE_NON_ZERO);
	plutovg_canvas_reset_matrix(c->canvas);
	plutovg_canvas_fill_path(c->canvas, p);
	plutovg_canvas_set_matrix(c->canvas, &m);
	plutovg_canvas_set_operator(c->canvas, op);
	plutovg_canvas_set_opacity(c->canvas, opacity);
	plutovg_canvas_new_path(c->canvas);
	plutovg_path_destroy(p);
	qcv_dirty(c);
	return JS_UNDEFINED;
}

/* cvInPath(c, x, y, evenodd), cvInStroke(c, x, y): x, y in the canvas' pixels */
static JSValue n_cv_in_path(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	plutovg_matrix_t m;
	bool in;
	QCV_ARG(c);
	(void) this_val;
	qcv_matrix(c, &m);
	plutovg_canvas_set_fill_rule(c->canvas, qi(ctx, argc, argv, 3) ?
			PLUTOVG_FILL_RULE_EVEN_ODD : PLUTOVG_FILL_RULE_NON_ZERO);
	plutovg_canvas_reset_matrix(c->canvas);
	plutovg_canvas_new_path(c->canvas);
	plutovg_canvas_add_path(c->canvas, c->path);
	in = plutovg_canvas_fill_contains(c->canvas, qf(ctx, argc, argv, 1),
			qf(ctx, argc, argv, 2));
	plutovg_canvas_new_path(c->canvas);
	plutovg_canvas_set_matrix(c->canvas, &m);
	return JS_NewBool(ctx, in);
}

static JSValue n_cv_in_stroke(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	plutovg_matrix_t m, inv;
	plutovg_path_t *user;
	bool in = false;
	QCV_ARG(c);
	(void) this_val;
	qcv_matrix(c, &m);
	if (!plutovg_matrix_invert(&m, &inv))
		return JS_FALSE;
	user = plutovg_path_clone(c->path);
	plutovg_path_transform(user, &inv);
	plutovg_canvas_new_path(c->canvas);
	plutovg_canvas_add_path(c->canvas, user);
	in = plutovg_canvas_stroke_contains(c->canvas, qf(ctx, argc, argv, 1),
			qf(ctx, argc, argv, 2));
	plutovg_canvas_new_path(c->canvas);
	plutovg_path_destroy(user);
	return JS_NewBool(ctx, in);
}


/* ---- gradients, patterns -------------------------------------------------------------------- */

/* cvGradient(kind, a0...a5) */
static JSValue n_cv_gradient(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qpaint *p = calloc(1, sizeof(*p));
	JSValue obj;
	int i;

	(void) this_val;
	if (p == NULL)
		return JS_ThrowOutOfMemory(ctx);
	p->kind = qi(ctx, argc, argv, 0);
	for (i = 0; i < 6; i++)
		p->a[i] = qf(ctx, argc, argv, i + 1);
	obj = JS_NewObjectClass(ctx, qpaint_class);
	if (JS_IsException(obj)) {
		free(p);
		return obj;
	}
	JS_SetOpaque(obj, p);
	return obj;
}

/* cvStop(gradient, offset, argb) */
static JSValue n_cv_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qpaint *p = argc > 0 ? JS_GetOpaque(argv[0], qpaint_class) : NULL;
	double argb = 0;
	float off;
	int i;

	(void) this_val;
	if (p == NULL || argc < 3)
		return JS_UNDEFINED;
	off = qf(ctx, argc, argv, 1);
	JS_ToFloat64(ctx, &argb, argv[2]);
	if (p->nstops == p->capstops) {
		int cap = p->capstops ? p->capstops * 2 : 4;
		plutovg_gradient_stop_t *s = realloc(p->stops, cap * sizeof(*s));

		if (s == NULL)
			return JS_ThrowOutOfMemory(ctx);
		p->stops = s;
		p->capstops = cap;
	}
	/* kept in order of offset (a stop at an equal offset after the others) */
	for (i = p->nstops; i > 0 && p->stops[i - 1].offset > off; i--)
		p->stops[i] = p->stops[i - 1];
	p->stops[i].offset = off;
	plutovg_color_init_argb32(&p->stops[i].color, (uint32_t) argb);
	p->nstops++;
	return JS_UNDEFINED;
}

static plutovg_surface_t *qcv_source_surface(JSContext *ctx, JSValueConst src, bool *owned);

/* cvPattern(source, repeat) */
static JSValue n_cv_pattern(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qpaint *p;
	plutovg_surface_t *s;
	bool owned = false;
	JSValue obj;

	(void) this_val;
	if (argc < 1)
		return JS_NULL;
	s = qcv_source_surface(ctx, argv[0], &owned);
	if (s == NULL)
		return JS_NULL;
	p = calloc(1, sizeof(*p));
	if (p == NULL) {
		if (owned)
			plutovg_surface_destroy(s);
		return JS_ThrowOutOfMemory(ctx);
	}
	p->kind = QP_PATTERN;
	p->repeat = qi(ctx, argc, argv, 1);
	/* a copy: the source may change (a canvas) or leave the cache */
	if (owned) {
		p->surface = s;
	} else {
		p->surface = plutovg_surface_create(plutovg_surface_get_width(s),
				plutovg_surface_get_height(s));
		if (p->surface != NULL)
			memcpy(plutovg_surface_get_data(p->surface), plutovg_surface_get_data(s),
					(size_t) plutovg_surface_get_stride(s) *
					plutovg_surface_get_height(s));
	}
	obj = JS_NewObjectClass(ctx, qpaint_class);
	if (JS_IsException(obj)) {
		if (p->surface != NULL)
			plutovg_surface_destroy(p->surface);
		free(p);
		return obj;
	}
	JS_SetOpaque(obj, p);
	return obj;
}


/* ---- images ------------------------------------------------------------------------------ */

/** the bitmap of an <img> element NetSurf fetched (its box's object), else NULL */
static struct bitmap *qcv_element_bitmap(dom_node *n)
{
	struct box *box = NULL;

	if (dom_node_get_user_data(n, corestring_dom___ns_key_box_node_data,
			(void **) &box) != DOM_NO_ERR || box == NULL || box->object == NULL)
		return NULL;
	if (content_get_status(box->object) != CONTENT_STATUS_DONE &&
	    content_get_status(box->object) != CONTENT_STATUS_READY)
		return NULL;
	return content_get_bitmap(box->object);
}

/**
 * The surface of a drawImage / createPattern source: a canvas (its own surface), an
 * OnyxImage loaded, an <img> element's bitmap. *owned: the caller destroys it.
 */
static plutovg_surface_t *qcv_source_surface(JSContext *ctx, JSValueConst src, bool *owned)
{
	struct qcanvas *c = JS_GetOpaque(src, qcv_class);
	struct qimage *im;
	struct bitmap *bm = NULL;
	dom_node *n;

	(void) ctx;
	*owned = false;
	if (c != NULL)
		return c->surface;
	im = JS_GetOpaque(src, qimg_class);
	if (im != NULL) {
		if (im->handle != NULL && im->done)
			bm = content_get_bitmap(im->handle);
	} else if ((n = qjs_node_of(src)) != NULL) {
		bm = qcv_element_bitmap(n);
	}
	return bm != NULL ? qcv_bitmap_surface(bm) : NULL;
}

/* cvSourceSize(src) -> [w, h] or null */
static JSValue n_cv_source_size(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	plutovg_surface_t *s;
	bool owned;
	JSValue a;

	(void) this_val;
	if (argc < 1 || (s = qcv_source_surface(ctx, argv[0], &owned)) == NULL)
		return JS_NULL;
	a = JS_NewArray(ctx);
	JS_SetPropertyUint32(ctx, a, 0, JS_NewInt32(ctx, plutovg_surface_get_width(s)));
	JS_SetPropertyUint32(ctx, a, 1, JS_NewInt32(ctx, plutovg_surface_get_height(s)));
	if (owned)
		plutovg_surface_destroy(s);
	return a;
}

/* cvDrawImage(c, src, sx, sy, sw, sh, dx, dy, dw, dh) */
static JSValue n_cv_draw_image(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	plutovg_surface_t *s;
	bool owned = false;
	float sx, sy, sw, sh, dx, dy, dw, dh;
	plutovg_matrix_t m, saved, tex;
	QCV_ARG(c);
	(void) this_val;
	if (argc < 10 || !qfinite(ctx, argc, argv, 2, 10))
		return JS_UNDEFINED;
	s = qcv_source_surface(ctx, argv[1], &owned);
	if (s == NULL)
		return JS_UNDEFINED;
	sx = qf(ctx, argc, argv, 2); sy = qf(ctx, argc, argv, 3);
	sw = qf(ctx, argc, argv, 4); sh = qf(ctx, argc, argv, 5);
	dx = qf(ctx, argc, argv, 6); dy = qf(ctx, argc, argv, 7);
	dw = qf(ctx, argc, argv, 8); dh = qf(ctx, argc, argv, 9);
	if (sw != 0 && sh != 0 && dw != 0 && dh != 0) {
		/* the source rectangle clipped to the image, the destination with it */
		float iw = plutovg_surface_get_width(s), ih = plutovg_surface_get_height(s);
		float kx = dw / sw, ky = dh / sh;

		if (sw < 0) { sx += sw; sw = -sw; }
		if (sh < 0) { sy += sh; sh = -sh; }
		if (sx < 0) { dx -= sx * kx; sw += sx; sx = 0; }
		if (sy < 0) { dy -= sy * ky; sh += sy; sy = 0; }
		if (sx + sw > iw) sw = iw - sx;
		if (sy + sh > ih) sh = ih - sy;
		if (sw > 0 && sh > 0) {
			qcv_matrix(c, &saved);
			/* the destination's user space: the image's pixels (sx, sy) at (dx, dy) */
			m = saved;
			plutovg_matrix_translate(&m, dx, dy);
			plutovg_matrix_scale(&m, kx, ky);
			plutovg_matrix_translate(&m, -sx, -sy);
			plutovg_matrix_init_identity(&tex);
			plutovg_canvas_set_matrix(c->canvas, &m);
			plutovg_canvas_set_texture(c->canvas, s, PLUTOVG_TEXTURE_TYPE_PLAIN, 1.f, &tex);
			plutovg_canvas_set_fill_rule(c->canvas, PLUTOVG_FILL_RULE_NON_ZERO);
			plutovg_canvas_fill_rect(c->canvas, sx, sy, sw, sh);
			plutovg_canvas_set_matrix(c->canvas, &saved);
			plutovg_canvas_new_path(c->canvas);
			/* (a canvas drawn onto itself: the paint's reference kept the surface) */
			plutovg_canvas_set_rgba(c->canvas, 0, 0, 0, 1);
			qcv_dirty(c);
		}
	}
	if (owned)
		plutovg_surface_destroy(s);
	return JS_UNDEFINED;
}

/* cvGetImageData(c, x, y, w, h) -> ArrayBuffer of w * h RGBA (straight alpha) */
static JSValue n_cv_get_image_data(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int x0, y0, w, h, x, y, sw, sh, stride;
	uint8_t *buf;
	const uint8_t *src;
	JSValue ab;
	QCV_ARG(c);
	(void) this_val;
	x0 = qi(ctx, argc, argv, 1);
	y0 = qi(ctx, argc, argv, 2);
	w = qi(ctx, argc, argv, 3);
	h = qi(ctx, argc, argv, 4);
	if (w <= 0 || h <= 0 || (int64_t) w * h > QCV_MAX_PIXELS)
		return JS_ThrowRangeError(ctx, "bad size");
	buf = calloc((size_t) w * h, 4);
	if (buf == NULL)
		return JS_ThrowOutOfMemory(ctx);
	sw = plutovg_surface_get_width(c->surface);
	sh = plutovg_surface_get_height(c->surface);
	stride = plutovg_surface_get_stride(c->surface);
	src = plutovg_surface_get_data(c->surface);
	for (y = 0; y < h; y++) {
		int sy = y0 + y;

		if (sy < 0 || sy >= sh)
			continue;
		for (x = 0; x < w; x++) {
			int sx = x0 + x;
			uint32_t p, a, r, g, b;
			uint8_t *d;

			if (sx < 0 || sx >= sw)
				continue;
			p = ((const uint32_t *) (const void *) (src + (size_t) sy * stride))[sx];
			a = p >> 24; r = (p >> 16) & 255; g = (p >> 8) & 255; b = p & 255;
			if (a != 0 && a != 255) {
				r = (r * 255 + a / 2) / a;
				g = (g * 255 + a / 2) / a;
				b = (b * 255 + a / 2) / a;
			}
			d = buf + ((size_t) y * w + x) * 4;
			d[0] = r; d[1] = g; d[2] = b; d[3] = a;
		}
	}
	ab = JS_NewArrayBufferCopy(ctx, buf, (size_t) w * h * 4);
	free(buf);
	return ab;
}

/* cvPutImageData(c, bytes, w, h, dx, dy, dirtyX, dirtyY, dirtyW, dirtyH) */
static JSValue n_cv_put_image_data(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	size_t len = 0;
	uint8_t *data;
	int w, h, dx, dy, x0, y0, x1, y1, x, y, sw, sh, stride;
	uint8_t *dst;
	QCV_ARG(c);
	(void) this_val;
	if (argc < 10)
		return JS_UNDEFINED;
	data = JS_GetUint8Array(ctx, &len, argv[1]);
	if (data == NULL) {
		JS_FreeValue(ctx, JS_GetException(ctx));
		data = JS_GetArrayBuffer(ctx, &len, argv[1]);
	}
	if (data == NULL)
		return JS_UNDEFINED;
	w = qi(ctx, argc, argv, 2); h = qi(ctx, argc, argv, 3);
	dx = qi(ctx, argc, argv, 4); dy = qi(ctx, argc, argv, 5);
	x0 = qi(ctx, argc, argv, 6); y0 = qi(ctx, argc, argv, 7);
	x1 = x0 + qi(ctx, argc, argv, 8); y1 = y0 + qi(ctx, argc, argv, 9);
	if (w <= 0 || h <= 0 || len < (size_t) w * h * 4)
		return JS_UNDEFINED;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > w) x1 = w;
	if (y1 > h) y1 = h;
	sw = plutovg_surface_get_width(c->surface);
	sh = plutovg_surface_get_height(c->surface);
	stride = plutovg_surface_get_stride(c->surface);
	dst = plutovg_surface_get_data(c->surface);
	for (y = y0; y < y1; y++) {
		int ty = dy + y;

		if (ty < 0 || ty >= sh)
			continue;
		for (x = x0; x < x1; x++) {
			int tx = dx + x;
			const uint8_t *s = data + ((size_t) y * w + x) * 4;
			uint32_t a = s[3], r = s[0], g = s[1], b = s[2];

			if (tx < 0 || tx >= sw)
				continue;
			if (a != 255) {
				r = (r * a + 127) / 255;
				g = (g * a + 127) / 255;
				b = (b * a + 127) / 255;
			}
			((uint32_t *) (void *) (dst + (size_t) ty * stride))[tx] =
				(a << 24) | (r << 16) | (g << 8) | b;
		}
	}
	qcv_dirty(c);
	return JS_UNDEFINED;
}

/* a PNG of the canvas, collected (plutovg_surface_write_to_png_stream) */
struct qpng {
	uint8_t *data;
	size_t len, cap;
};

static void qcv_png_write(void *closure, void *data, int size)
{
	struct qpng *p = closure;

	if (size <= 0)
		return;
	if (p->len + size > p->cap) {
		size_t cap = p->cap ? p->cap * 2 : 4096;
		uint8_t *d;

		while (cap < p->len + size)
			cap *= 2;
		d = realloc(p->data, cap);
		if (d == NULL)
			return;
		p->data = d;
		p->cap = cap;
	}
	memcpy(p->data + p->len, data, size);
	p->len += size;
}

/* cvDataURL(c) -> "data:image/png;base64,..." */
static JSValue n_cv_data_url(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	static const char b64[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	static const char head[] = "data:image/png;base64,";
	struct qpng png = { 0 };
	char *out, *o;
	size_t i;
	JSValue r;
	QCV_ARG(c);
	(void) this_val;
	if (!plutovg_surface_write_to_png_stream(c->surface, qcv_png_write, &png) ||
	    png.data == NULL) {
		free(png.data);
		return JS_NewString(ctx, "data:,");
	}
	out = malloc(sizeof(head) + (png.len + 2) / 3 * 4 + 1);
	if (out == NULL) {
		free(png.data);
		return JS_ThrowOutOfMemory(ctx);
	}
	memcpy(out, head, sizeof(head) - 1);
	o = out + sizeof(head) - 1;
	for (i = 0; i + 2 < png.len; i += 3) {
		uint32_t v = png.data[i] << 16 | png.data[i + 1] << 8 | png.data[i + 2];

		*o++ = b64[v >> 18]; *o++ = b64[(v >> 12) & 63];
		*o++ = b64[(v >> 6) & 63]; *o++ = b64[v & 63];
	}
	if (i < png.len) {
		uint32_t v = png.data[i] << 16 | (i + 1 < png.len ? png.data[i + 1] << 8 : 0);

		*o++ = b64[v >> 18]; *o++ = b64[(v >> 12) & 63];
		*o++ = i + 1 < png.len ? b64[(v >> 6) & 63] : '=';
		*o++ = '=';
	}
	*o = '\0';
	r = JS_NewString(ctx, out);
	free(out);
	free(png.data);
	return r;
}


/* ---- images a script loads (new Image(): never in the page) ---------------------------------- */

static nserror qimg_callback(hlcache_handle *handle, const hlcache_event *event, void *pw)
{
	struct qimage *im = pw;
	JSValue args[3];
	bool ok;

	(void) handle;
	if (event->type != CONTENT_MSG_DONE && event->type != CONTENT_MSG_ERROR)
		return NSERROR_OK;
	if (im->done || im->ctx == NULL)
		return NSERROR_OK;
	im->done = true;
	ok = event->type == CONTENT_MSG_DONE && content_get_bitmap(im->handle) != NULL;
	args[0] = JS_NewBool(im->ctx, ok);
	args[1] = JS_NewInt32(im->ctx, ok ? content_get_width(im->handle) : 0);
	args[2] = JS_NewInt32(im->ctx, ok ? content_get_height(im->handle) : 0);
	if (JS_IsFunction(im->ctx, im->cb)) {
		JSValue cb = JS_DupValue(im->ctx, im->cb);

		qjs_invoke(im->ctx, cb, 3, args, "image load");
		JS_FreeValue(im->ctx, cb);
	}
	if (!ok)
		qimg_release(im);
	return NSERROR_OK;
}

/* cvLoadImage(url, cb) -> an OnyxImage (cb(ok, w, h) once loaded) */
static JSValue n_cv_load_image(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	struct qimage *im;
	const char *s;
	nsurl *url = NULL;
	JSValue obj;
	html_content *htmlc = qjs_html_of(ctx);
	hlcache_child_context child = { NULL, false };

	(void) this_val;
	if (argc < 2 || htmlc == NULL)
		return JS_NULL;
	s = JS_ToCString(ctx, argv[0]);
	if (s == NULL)
		return JS_NULL;
	if (nsurl_join(htmlc->base_url, s, &url) != NSERROR_OK)
		url = NULL;
	JS_FreeCString(ctx, s);
	if (url == NULL)
		return JS_NULL;
	im = calloc(1, sizeof(*im));
	if (im == NULL) {
		nsurl_unref(url);
		return JS_ThrowOutOfMemory(ctx);
	}
	im->ctx = ctx;
	im->cb = JS_DupValue(ctx, argv[1]);
	obj = JS_NewObjectClass(ctx, qimg_class);
	if (JS_IsException(obj)) {
		JS_FreeValue(ctx, im->cb);
		free(im);
		nsurl_unref(url);
		return obj;
	}
	JS_SetOpaque(obj, im);
	im->next = qimg_all;
	qimg_all = im;
	child.charset = htmlc->encoding;
	child.quirks = htmlc->base.quirks;
	if (hlcache_handle_retrieve(url, HLCACHE_RETRIEVE_SNIFF_TYPE,
			content_get_url(&htmlc->base), NULL, qimg_callback, im, &child,
			CONTENT_IMAGE, &im->handle) != NSERROR_OK)
		im->handle = NULL;
	nsurl_unref(url);
	return obj;
}


/* ---- text ------------------------------------------------------------------------------------ */

/* cvFont(c, "family1, family2", bold, italic, size): the first family the card has
 * (image/onyx_vgfont.c, shared with the SVG images' <text>) */
static JSValue n_cv_font(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const char *fam;
	bool bold, italic;
	QCV_ARG(c);
	(void) this_val;
	fam = argc > 1 ? JS_ToCString(ctx, argv[1]) : NULL;
	bold = argc > 2 && JS_ToBool(ctx, argv[2]);
	italic = argc > 3 && JS_ToBool(ctx, argv[3]);
	c->face = onyx_vg_font(fam != NULL ? fam : "sans-serif", -1, bold, italic);
	if (fam != NULL)
		JS_FreeCString(ctx, fam);
	c->font_size = qf(ctx, argc, argv, 4);
	return JS_NewBool(ctx, c->face != NULL);
}

/** the text's advance (its width) at the canvas' font */
static float qcv_text_width(struct qcanvas *c, const char *s, size_t len)
{
	plutovg_text_iterator_t it;
	float w = 0;

	plutovg_text_iterator_init(&it, s, (int) len, PLUTOVG_TEXT_ENCODING_UTF8);
	while (plutovg_text_iterator_has_next(&it)) {
		float adv = 0;

		plutovg_font_face_get_glyph_metrics(c->face, c->font_size,
				plutovg_text_iterator_next(&it), &adv, NULL, NULL);
		w += adv;
	}
	return w;
}

/* cvMeasure(c, text) -> [width, ascent, descent, left, right, top, bottom] (the font's
 * ascent / descent, the ink's box) */
static JSValue n_cv_measure(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	size_t len;
	const char *s;
	float asc = 0, desc = 0, w;
	plutovg_rect_t ink = { 0, 0, 0, 0 };
	JSValue a;
	QCV_ARG(c);
	(void) this_val;
	if (c->face == NULL || argc < 2 || (s = JS_ToCStringLen(ctx, &len, argv[1])) == NULL)
		return JS_NULL;
	w = qcv_text_width(c, s, len);
	plutovg_font_face_get_metrics(c->face, c->font_size, &asc, &desc, NULL, NULL);
	plutovg_font_face_text_extents(c->face, c->font_size, s, (int) len,
			PLUTOVG_TEXT_ENCODING_UTF8, &ink);
	JS_FreeCString(ctx, s);
	a = JS_NewArray(ctx);
	JS_SetPropertyUint32(ctx, a, 0, JS_NewFloat64(ctx, w));
	JS_SetPropertyUint32(ctx, a, 1, JS_NewFloat64(ctx, asc));
	JS_SetPropertyUint32(ctx, a, 2, JS_NewFloat64(ctx, -desc));
	JS_SetPropertyUint32(ctx, a, 3, JS_NewFloat64(ctx, -ink.x));
	JS_SetPropertyUint32(ctx, a, 4, JS_NewFloat64(ctx, ink.x + ink.w));
	JS_SetPropertyUint32(ctx, a, 5, JS_NewFloat64(ctx, -ink.y));
	JS_SetPropertyUint32(ctx, a, 6, JS_NewFloat64(ctx, ink.y + ink.h));
	return a;
}

/* cvText(c, paint, text, x, y, align, baseline, maxWidth, stroke): align 0 left, 1 right,
 * 2 center (canvas.js resolves start / end); baseline 0 alphabetic, 1 top, 2 hanging,
 * 3 middle, 4 ideographic, 5 bottom */
static JSValue n_cv_text(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	size_t len;
	const char *s;
	float x, y, w, asc = 0, desc = 0, maxw, sx = 1.f, pen;
	int align, baseline;
	plutovg_text_iterator_t it;
	plutovg_path_t *p;
	plutovg_matrix_t m, glyphs;
	QCV_ARG(c);
	(void) this_val;
	if (c->face == NULL || argc < 9 || !qfinite(ctx, argc, argv, 3, 5))
		return JS_UNDEFINED;
	s = JS_ToCStringLen(ctx, &len, argv[2]);
	if (s == NULL)
		return JS_UNDEFINED;
	x = qf(ctx, argc, argv, 3);
	y = qf(ctx, argc, argv, 4);
	align = qi(ctx, argc, argv, 5);
	baseline = qi(ctx, argc, argv, 6);
	maxw = qf(ctx, argc, argv, 7);
	w = qcv_text_width(c, s, len);
	if (maxw > 0 && w > maxw) {
		sx = maxw / w;
		w = maxw;
	}
	if (align == 1)
		x -= w;
	else if (align == 2)
		x -= w / 2;
	plutovg_font_face_get_metrics(c->face, c->font_size, &asc, &desc, NULL, NULL);
	switch (baseline) {
	case 1: y += asc; break;			/* top: the em box's top */
	case 2: y += asc * 0.8f; break;			/* hanging */
	case 3: y += (asc + desc) / 2; break;		/* middle (desc < 0) */
	case 4: case 5: y += desc; break;		/* ideographic, bottom */
	default: break;
	}
	p = plutovg_path_create();
	pen = 0;
	plutovg_text_iterator_init(&it, s, (int) len, PLUTOVG_TEXT_ENCODING_UTF8);
	while (plutovg_text_iterator_has_next(&it))
		pen += plutovg_font_face_get_glyph_path(c->face, c->font_size, pen, 0,
				plutovg_text_iterator_next(&it), p);
	JS_FreeCString(ctx, s);
	/* the glyphs placed (x, y, maxWidth's squeeze), then through the transform */
	qcv_matrix(c, &m);
	plutovg_matrix_init_translate(&glyphs, x, y);
	plutovg_matrix_scale(&glyphs, sx, 1.f);
	plutovg_matrix_multiply(&glyphs, &glyphs, &m);
	plutovg_path_transform(p, &glyphs);
	if (argc > 8 && JS_ToBool(ctx, argv[8]))
		qcv_stroke_path(ctx, c, p, argv[1]);
	else
		qcv_fill_path(ctx, c, p, argv[1], 0);
	plutovg_path_destroy(p);
	return JS_UNDEFINED;
}


/* ---- setup ------------------------------------------------------------------------------------- */

static const JSCFunctionListEntry qcv_natives[] = {
	JS_CFUNC_DEF("cvNew", 3, n_cv_new),
	JS_CFUNC_DEF("cvResize", 3, n_cv_resize),
	JS_CFUNC_DEF("cvColor", 1, n_cv_color),
	JS_CFUNC_DEF("cvLineWidth", 2, n_cv_line_width),
	JS_CFUNC_DEF("cvLineCap", 2, n_cv_line_cap),
	JS_CFUNC_DEF("cvLineJoin", 2, n_cv_line_join),
	JS_CFUNC_DEF("cvMiter", 2, n_cv_miter),
	JS_CFUNC_DEF("cvAlpha", 2, n_cv_alpha),
	JS_CFUNC_DEF("cvOp", 2, n_cv_op),
	JS_CFUNC_DEF("cvDash", 3, n_cv_dash),
	JS_CFUNC_DEF("cvSave", 1, n_cv_save),
	JS_CFUNC_DEF("cvRestore", 1, n_cv_restore),
	JS_CFUNC_DEF("cvTransform", 8, n_cv_transform),
	JS_CFUNC_DEF("cvGetTransform", 1, n_cv_get_transform),
	JS_CFUNC_DEF("cvBegin", 1, n_cv_begin),
	JS_CFUNC_DEF("cvMove", 3, n_cv_move),
	JS_CFUNC_DEF("cvLine", 3, n_cv_line),
	JS_CFUNC_DEF("cvQuad", 5, n_cv_quad),
	JS_CFUNC_DEF("cvCubic", 7, n_cv_cubic),
	JS_CFUNC_DEF("cvArc", 7, n_cv_arc),
	JS_CFUNC_DEF("cvArcTo", 6, n_cv_arc_to),
	JS_CFUNC_DEF("cvEllipse", 9, n_cv_ellipse),
	JS_CFUNC_DEF("cvRect", 5, n_cv_rect),
	JS_CFUNC_DEF("cvRoundRect", 6, n_cv_round_rect),
	JS_CFUNC_DEF("cvClose", 1, n_cv_close),
	JS_CFUNC_DEF("cvSvgPath", 2, n_cv_svg_path),
	JS_CFUNC_DEF("cvPathPush", 1, n_cv_path_push),
	JS_CFUNC_DEF("cvPathPop", 1, n_cv_path_pop),
	JS_CFUNC_DEF("cvFill", 3, n_cv_fill),
	JS_CFUNC_DEF("cvStroke", 2, n_cv_stroke),
	JS_CFUNC_DEF("cvClip", 2, n_cv_clip),
	JS_CFUNC_DEF("cvFillRect", 6, n_cv_fill_rect),
	JS_CFUNC_DEF("cvStrokeRect", 6, n_cv_stroke_rect),
	JS_CFUNC_DEF("cvClearRect", 5, n_cv_clear_rect),
	JS_CFUNC_DEF("cvInPath", 4, n_cv_in_path),
	JS_CFUNC_DEF("cvInStroke", 3, n_cv_in_stroke),
	JS_CFUNC_DEF("cvGradient", 7, n_cv_gradient),
	JS_CFUNC_DEF("cvStop", 3, n_cv_stop),
	JS_CFUNC_DEF("cvPattern", 2, n_cv_pattern),
	JS_CFUNC_DEF("cvSourceSize", 1, n_cv_source_size),
	JS_CFUNC_DEF("cvDrawImage", 10, n_cv_draw_image),
	JS_CFUNC_DEF("cvGetImageData", 5, n_cv_get_image_data),
	JS_CFUNC_DEF("cvPutImageData", 10, n_cv_put_image_data),
	JS_CFUNC_DEF("cvDataURL", 1, n_cv_data_url),
	JS_CFUNC_DEF("cvLoadImage", 2, n_cv_load_image),
	JS_CFUNC_DEF("cvFont", 5, n_cv_font),
	JS_CFUNC_DEF("cvMeasure", 2, n_cv_measure),
	JS_CFUNC_DEF("cvText", 9, n_cv_text),
};

/* exported interface documented in qjs_canvas.h */
/* compiled once per process (qjs.c) */
JSValue qjs_eval_cached(JSContext *ctx, const char *src, size_t len, const char *name,
		uint8_t **bc, size_t *bclen);
static uint8_t *qjs_canvas_bc;
static size_t qjs_canvas_bc_len;

void qjs_canvas_setup(JSContext *ctx, JSValueConst natives)
{
	JSRuntime *rt = JS_GetRuntime(ctx);
	JSValue fn, r;

	if (qcv_class == 0) {
		JS_NewClassID(rt, &qcv_class);
		JS_NewClassID(rt, &qpaint_class);
		JS_NewClassID(rt, &qimg_class);
	}
	if (!JS_IsRegisteredClass(rt, qcv_class)) {
		JS_NewClass(rt, qcv_class, &qcv_classdef);
		JS_NewClass(rt, qpaint_class, &qpaint_classdef);
		JS_NewClass(rt, qimg_class, &qimg_classdef);
	}
	JS_SetPropertyFunctionList(ctx, natives, qcv_natives,
			sizeof(qcv_natives) / sizeof(qcv_natives[0]));
	fn = qjs_eval_cached(ctx, qjs_canvas_js, sizeof(qjs_canvas_js) - 1, "canvas.js",
			&qjs_canvas_bc, &qjs_canvas_bc_len);	/* (Onyx: qjs.c) */
	if (JS_IsException(fn)) {
		JSValue e = JS_GetException(ctx);
		const char *msg = JS_ToCString(ctx, e);

		NSLOG(netsurf, INFO, "canvas.js: %s", msg ? msg : "?");
		if (msg)
			JS_FreeCString(ctx, msg);
		JS_FreeValue(ctx, e);
		return;
	}
	r = JS_Call(ctx, fn, JS_UNDEFINED, 1, &natives);
	if (JS_IsException(r)) {
		JSValue e = JS_GetException(ctx);
		const char *msg = JS_ToCString(ctx, e);

		NSLOG(netsurf, INFO, "canvas.js setup: %s", msg ? msg : "?");
		if (getenv("NS_JSDEBUG"))
			fprintf(stderr, "JS canvas.js setup: %s\n", msg ? msg : "?");
		if (msg)
			JS_FreeCString(ctx, msg);
		JS_FreeValue(ctx, e);
	}
	JS_FreeValue(ctx, r);
	JS_FreeValue(ctx, fn);
}

/* exported interface documented in qjs_canvas.h */
void qjs_canvas_context_gone(JSContext *ctx)
{
	struct qcanvas *c;
	struct qimage *im;

	for (c = qcv_all; c != NULL; c = c->next) {
		if (c->ctx != ctx)
			continue;
		if (c->scheduled)
			guit->misc->schedule(-1, qcv_flush, c);
		c->scheduled = false;
		c->ctx = NULL;
	}
	for (im = qimg_all; im != NULL; im = im->next) {
		if (im->ctx != ctx)
			continue;
		qimg_release(im);
		JS_FreeValue(ctx, im->cb);
		im->cb = JS_UNDEFINED;
		im->ctx = NULL;
	}
}
