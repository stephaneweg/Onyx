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
 * Onyx: the content handler for image/svg+xml on PlutoSVG (third_party/plutosvg-0.0.8, on
 * PlutoVG) -- NetSurf's own svg.c needs libsvgtiny, not vendored.
 *
 * The document is parsed once (its intrinsic size: width / height, else the viewBox's,
 * else 300 x 150 as browsers do); it is rasterised, anti-aliased, at the size each redraw
 * asks for, into a NetSurf bitmap kept for the next redraws (a few sizes kept: an image
 * drawn at two sizes, a window resized) -- a redraw is then a bitmap plot, never a
 * rasterisation. An SVG with a viewBox is drawn in a viewport of the drawn size (its
 * preserveAspectRatio applies); one without is scaled, as browsers do for images.
 * currentColor is black in an image (the inline <svg> of a page give their CSS colour in
 * their markup: box_special.c).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <plutosvg.h>

#include "utils/utils.h"
#include "utils/log.h"
#include "utils/nsurl.h"
#include "netsurf/plotters.h"
#include "netsurf/bitmap.h"
#include "netsurf/content.h"
#include "netsurf/misc.h"
#include "content/content_protected.h"
#include "content/content_factory.h"
#include "desktop/gui_internal.h"
#include "desktop/bitmap.h"

#include "image/onyx_svg.h"
#include "image/onyx_vgfont.h"

/** the sizes kept rasterised per image */
#define ONYX_SVG_KEEP 6
/** the most pixels rasterised (a larger drawing is rasterised smaller and scaled up) */
#define ONYX_SVG_MAX_PIXELS (2048 * 2048)

struct onyx_svg_size {
	int width, height;	/* the bitmap's size */
	struct bitmap *bitmap;
	unsigned int used;	/* for the least recently used */
};

typedef struct onyx_svg_content {
	struct content base;
	plutosvg_document_t *doc;
	float iwidth, iheight;	/* the intrinsic size */
	bool view_box;
	struct onyx_svg_size sizes[ONYX_SVG_KEEP];
	unsigned int clock;
} onyx_svg_content;


static nserror onyx_svg_create(const content_handler *handler, lwc_string *imime_type,
		const struct http_parameter *params, struct llcache_handle *llcache,
		const char *fallback_charset, bool quirks, struct content **c)
{
	onyx_svg_content *svg;
	nserror error;

	svg = calloc(1, sizeof(onyx_svg_content));
	if (svg == NULL)
		return NSERROR_NOMEM;
	error = content__init(&svg->base, handler, imime_type, params, llcache,
			fallback_charset, quirks);
	if (error != NSERROR_OK) {
		free(svg);
		return error;
	}
	*c = (struct content *) svg;
	return NSERROR_OK;
}


static bool onyx_svg_convert(struct content *c)
{
	onyx_svg_content *svg = (onyx_svg_content *) c;
	const uint8_t *data;
	size_t size;

	data = content__get_source_data(c, &size);
	if (data == NULL || size == 0 || size > INT32_MAX) {
		content_broadcast_error(c, NSERROR_SVG_ERROR, NULL);
		return false;
	}
	plutosvg_set_font_func(onyx_vg_font);	/* <text>: the card's fonts */
	/* (the document points into the source data: it lives as long as the content) */
	svg->doc = plutosvg_document_load_from_data((const char *) data, (int) size,
			-1, -1, NULL, NULL);
	if (svg->doc == NULL) {
		NSLOG(netsurf, INFO, "SVG not parsed: %s", nsurl_access(content_get_url(c)));
		content_broadcast_error(c, NSERROR_SVG_ERROR, NULL);
		return false;
	}
	svg->iwidth = plutosvg_document_get_width(svg->doc);
	svg->iheight = plutosvg_document_get_height(svg->doc);
	svg->view_box = plutosvg_document_has_view_box(svg->doc);
	c->width = (int) ceilf(svg->iwidth);
	c->height = (int) ceilf(svg->iheight);
	if (c->width < 1)
		c->width = 1;
	if (c->height < 1)
		c->height = 1;

	content_set_ready(c);
	content_set_done(c);
	content_set_status(c, "");
	return true;
}


/** the document rasterised at width x height, as a new bitmap (NULL: no memory) */
static struct bitmap *onyx_svg_rasterise(onyx_svg_content *svg, int width, int height)
{
	static const plutovg_color_t black = { 0, 0, 0, 1 };
	struct bitmap *bitmap;
	plutovg_surface_t *surface;
	plutovg_canvas_t *canvas;
	unsigned char *buffer;
	size_t stride;

	bitmap = guit->bitmap->create(width, height, BITMAP_NONE);
	if (bitmap == NULL)
		return NULL;
	buffer = guit->bitmap->get_buffer(bitmap);
	stride = guit->bitmap->get_rowstride(bitmap);
	if (buffer == NULL) {
		guit->bitmap->destroy(bitmap);
		return NULL;
	}
	memset(buffer, 0, stride * height);
	surface = plutovg_surface_create_for_data(buffer, width, height, (int) stride);
	canvas = surface != NULL ? plutovg_canvas_create(surface) : NULL;
	if (canvas != NULL) {
		if (svg->view_box) {
			/* the viewport is the drawn box: the viewBox fitted in it */
			plutosvg_document_set_size(svg->doc, width, height);
		} else {
			plutovg_canvas_scale(canvas, width / svg->iwidth, height / svg->iheight);
		}
		plutosvg_document_render(svg->doc, NULL, canvas, &black, NULL, NULL);
		plutosvg_document_set_size(svg->doc, svg->iwidth, svg->iheight);
		plutovg_canvas_destroy(canvas);
	}
	if (surface != NULL)
		plutovg_surface_destroy(surface);	/* (not the buffer: the bitmap's) */

	/* PlutoVG's pixels: 0xAARRGGBB words, premultiplied */
	bitmap_format_to_client(bitmap, &(bitmap_fmt_t) {
			.layout = BITMAP_LAYOUT_ARGB8888,
			.pma = true,
		});
	guit->bitmap->modified(bitmap);
	return bitmap;
}


/*
 * A size's bitmap replaced during a redraw may still be in the knockout's queue of plots
 * (desktop/knockout.c plots later): it is destroyed once the main loop turns.
 */
#define ONYX_SVG_LATER 32
static struct bitmap *onyx_svg_later[ONYX_SVG_LATER];
static int onyx_svg_nlater;

static void onyx_svg_destroy_pending(void *p)
{
	int i;

	(void) p;
	for (i = 0; i < onyx_svg_nlater; i++)
		guit->bitmap->destroy(onyx_svg_later[i]);
	onyx_svg_nlater = 0;
}

static void onyx_svg_destroy_later(struct bitmap *bitmap)
{
	if (onyx_svg_nlater == ONYX_SVG_LATER) {
		/* (32 replaced within one turn: this one leaked rather than freed under a
		 * plot still queued) */
		return;
	}
	if (onyx_svg_nlater == 0)
		guit->misc->schedule(0, onyx_svg_destroy_pending, NULL);
	onyx_svg_later[onyx_svg_nlater++] = bitmap;
}


/** the bitmap for a drawing of width x height, rasterised if not kept */
static struct bitmap *onyx_svg_bitmap(onyx_svg_content *svg, int width, int height)
{
	struct onyx_svg_size *slot = &svg->sizes[0];
	int i;

	/* a very large drawing rasterised smaller (the plot scales it up) */
	while ((int64_t) width * height > ONYX_SVG_MAX_PIXELS) {
		width = (width + 1) / 2;
		height = (height + 1) / 2;
	}
	if (width < 1)
		width = 1;
	if (height < 1)
		height = 1;

	svg->clock++;
	for (i = 0; i < ONYX_SVG_KEEP; i++) {
		struct onyx_svg_size *s = &svg->sizes[i];

		if (s->bitmap != NULL && s->width == width && s->height == height) {
			s->used = svg->clock;
			return s->bitmap;
		}
		if (s->bitmap == NULL || (slot->bitmap != NULL && s->used < slot->used))
			slot = s;
	}
	if (slot->bitmap != NULL)
		onyx_svg_destroy_later(slot->bitmap);
	slot->bitmap = onyx_svg_rasterise(svg, width, height);
	slot->width = width;
	slot->height = height;
	slot->used = svg->clock;
	return slot->bitmap;
}


static bool onyx_svg_redraw(struct content *c, struct content_redraw_data *data,
		const struct rect *clip, const struct redraw_context *ctx)
{
	onyx_svg_content *svg = (onyx_svg_content *) c;
	bitmap_flags_t flags = BITMAPF_NONE;
	struct bitmap *bitmap;

	if (svg->doc == NULL || data->width <= 0 || data->height <= 0)
		return true;
	bitmap = onyx_svg_bitmap(svg, data->width, data->height);
	if (bitmap == NULL)
		return true;
	if (data->repeat_x)
		flags |= BITMAPF_REPEAT_X;
	if (data->repeat_y)
		flags |= BITMAPF_REPEAT_Y;
	return ctx->plot->bitmap(ctx, bitmap, data->x, data->y, data->width, data->height,
			data->background_colour, flags) == NSERROR_OK;
}


static void onyx_svg_destroy(struct content *c)
{
	onyx_svg_content *svg = (onyx_svg_content *) c;
	int i;

	for (i = 0; i < ONYX_SVG_KEEP; i++) {
		if (svg->sizes[i].bitmap != NULL)
			guit->bitmap->destroy(svg->sizes[i].bitmap);
		svg->sizes[i].bitmap = NULL;
	}
	if (svg->doc != NULL)
		plutosvg_document_destroy(svg->doc);
	svg->doc = NULL;
}


static nserror onyx_svg_clone(const struct content *old, struct content **newc)
{
	onyx_svg_content *svg;
	nserror error;

	svg = calloc(1, sizeof(onyx_svg_content));
	if (svg == NULL)
		return NSERROR_NOMEM;
	error = content__clone(old, &svg->base);
	if (error != NSERROR_OK) {
		content_destroy(&svg->base);
		return error;
	}
	if (old->status == CONTENT_STATUS_READY || old->status == CONTENT_STATUS_DONE) {
		if (onyx_svg_convert(&svg->base) == false) {
			content_destroy(&svg->base);
			return NSERROR_CLONE_FAILED;
		}
	}
	*newc = (struct content *) svg;
	return NSERROR_OK;
}


/** the image as a bitmap (favicons, a canvas' drawImage): the last size drawn, else the
 * intrinsic size */
static void *onyx_svg_get_internal(const struct content *c, void *context)
{
	onyx_svg_content *svg = (onyx_svg_content *) c;
	struct onyx_svg_size *best = NULL;
	int i;

	(void) context;
	if (svg->doc == NULL)
		return NULL;
	for (i = 0; i < ONYX_SVG_KEEP; i++) {
		if (svg->sizes[i].bitmap != NULL &&
		    (best == NULL || svg->sizes[i].used > best->used))
			best = &svg->sizes[i];
	}
	if (best != NULL)
		return best->bitmap;
	return onyx_svg_bitmap(svg, c->width, c->height);
}


static content_type onyx_svg_content_type(void)
{
	return CONTENT_IMAGE;
}


static const content_handler onyx_svg_content_handler = {
	.create = onyx_svg_create,
	.data_complete = onyx_svg_convert,
	.destroy = onyx_svg_destroy,
	.redraw = onyx_svg_redraw,
	.clone = onyx_svg_clone,
	.get_internal = onyx_svg_get_internal,
	.type = onyx_svg_content_type,
	.no_share = false,
};

static const char *onyx_svg_types[] = {
	"image/svg+xml",
	"image/svg",
};

CONTENT_FACTORY_REGISTER_TYPES(onyx_svg, onyx_svg_types, onyx_svg_content_handler);
