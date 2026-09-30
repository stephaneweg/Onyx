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
 * Onyx: a box's mask-image, for the HTML redraw.
 *
 * Pages draw their icons as a box with a background colour and a mask-image (Facebook's
 * <img>s, their own picture moved away with object-position; Wikipedia's icons): what
 * shows is the mask's shape in the background colour. The mask (its first layer: the
 * image, mask-size, mask-position, mask-repeat; libcss keeps them as texts,
 * src/parse/properties/onyx_css3b.c) is drawn over the border box, the colour taking the
 * mask image's alpha: the image is redrawn through a plotter table whose bitmap plot
 * paints a copy of the bitmap tinted. The box's children are painted as usual (not
 * masked).
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "utils/utils.h"
#include "utils/nsoption.h"
#include "netsurf/content.h"
#include "netsurf/plotters.h"
#include "netsurf/bitmap.h"
#include "netsurf/misc.h"
#include "content/content.h"
#include "css/utils.h"
#include "desktop/bitmap.h"
#include "desktop/gui_internal.h"

#include "html/box.h"
#include "html/private.h"
#include "html/onyx_mask.h"


/* exported function documented in html/onyx_mask.h */
bool onyx_mask_set(const struct box *box)
{
	lwc_string *text = NULL;

	return box->style != NULL &&
		css_computed_mask_image(box->style, &text) == CSS_ONYX_TEXT_SET &&
		text != NULL && nsoption_bool(background_images);
}


/*
 * The tinted bitmaps: a plot may be queued (desktop/knockout.c plots later), so each is
 * destroyed once the main loop turns.
 */
static struct bitmap **onyx_mask_later;
static int onyx_mask_nlater, onyx_mask_cap;

static void onyx_mask_destroy_pending(void *p)
{
	int i;

	(void) p;
	for (i = 0; i < onyx_mask_nlater; i++)
		guit->bitmap->destroy(onyx_mask_later[i]);
	onyx_mask_nlater = 0;
}

static bool onyx_mask_destroy_later(struct bitmap *bitmap)
{
	if (onyx_mask_nlater == onyx_mask_cap) {
		int cap = onyx_mask_cap ? onyx_mask_cap * 2 : 16;
		struct bitmap **l = realloc(onyx_mask_later, cap * sizeof(*l));

		if (l == NULL)
			return false;
		onyx_mask_later = l;
		onyx_mask_cap = cap;
	}
	if (onyx_mask_nlater == 0)
		guit->misc->schedule(0, onyx_mask_destroy_pending, NULL);
	onyx_mask_later[onyx_mask_nlater++] = bitmap;
	return true;
}


/* the redraw going on: the real plotters, the colour (css ARGB) */
static const struct plotter_table *onyx_mask_plot;
static css_color onyx_mask_colour;

/** the mask's bitmap plotted: a copy, the colour with the bitmap's alpha */
static nserror onyx_mask_bitmap(const struct redraw_context *ctx, struct bitmap *src,
		int x, int y, int width, int height, colour bg, bitmap_flags_t flags)
{
	struct redraw_context real = *ctx;
	struct bitmap *dst;
	const uint8_t *sb;
	uint8_t *db;
	size_t ss, ds;
	int w, h, i, j;
	unsigned int ca = onyx_mask_colour >> 24;
	unsigned int cr = (onyx_mask_colour >> 16) & 0xff;
	unsigned int cg = (onyx_mask_colour >> 8) & 0xff;
	unsigned int cb = onyx_mask_colour & 0xff;

	real.plot = onyx_mask_plot;
	w = guit->bitmap->get_width(src);
	h = guit->bitmap->get_height(src);
	sb = guit->bitmap->get_buffer(src);
	if (w <= 0 || h <= 0 || sb == NULL)
		return NSERROR_OK;
	dst = guit->bitmap->create(w, h, BITMAP_NONE);
	if (dst == NULL)
		return NSERROR_NOMEM;
	db = guit->bitmap->get_buffer(dst);
	if (db == NULL) {
		guit->bitmap->destroy(dst);
		return NSERROR_NOMEM;
	}
	ss = guit->bitmap->get_rowstride(src);
	ds = guit->bitmap->get_rowstride(dst);
	for (j = 0; j < h; j++) {
		const uint8_t *s = sb + j * ss;
		uint8_t *d = db + j * ds;

		for (i = 0; i < w; i++, s += 4, d += 4) {
			unsigned int a = s[bitmap_layout.a] * ca / 255;

			d[bitmap_layout.a] = a;
			if (bitmap_fmt.pma) {
				d[bitmap_layout.r] = cr * a / 255;
				d[bitmap_layout.g] = cg * a / 255;
				d[bitmap_layout.b] = cb * a / 255;
			} else {
				d[bitmap_layout.r] = cr;
				d[bitmap_layout.g] = cg;
				d[bitmap_layout.b] = cb;
			}
		}
	}
	if (guit->bitmap->set_opaque != NULL)
		guit->bitmap->set_opaque(dst, false);
	guit->bitmap->modified(dst);
	if (!onyx_mask_destroy_later(dst)) {
		guit->bitmap->destroy(dst);
		return NSERROR_NOMEM;
	}
	return onyx_mask_plot->bitmap(&real, dst, x, y, width, height, bg, flags);
}

/* the other plots of the mask's content (a vector image's): nothing (the mask is an
 * image: a bitmap plotted) */
static nserror onyx_mask_none_rect(const struct redraw_context *ctx,
		const plot_style_t *style, const struct rect *r)
{
	(void) ctx; (void) style; (void) r;
	return NSERROR_OK;
}


/** A length ("12px", "50%", "1.5em", "0"): device px (a percentage of ref); false if not
 * one. */
static bool onyx_mask_len(const char *s, float ref, const css_computed_style *style,
		const css_unit_ctx *uctx, float scale, float *px)
{
	char *e;
	float v = strtof(s, &e);
	css_unit unit;

	if (e == s)
		return false;
	if (*e == '%') {
		*px = v * ref / 100;
		return true;
	}
	if (*e == '\0' || strcmp(e, "px") == 0) {
		*px = v * scale;
		return true;
	}
	if (strcmp(e, "em") == 0)
		unit = CSS_UNIT_EM;
	else if (strcmp(e, "rem") == 0)
		unit = CSS_UNIT_REM;
	else if (strcmp(e, "vw") == 0)
		unit = CSS_UNIT_VW;
	else if (strcmp(e, "vh") == 0)
		unit = CSS_UNIT_VH;
	else
		return false;
	*px = FIXTOFLT(css_unit_len2device_px(style, uctx, FLTTOFIX(v), unit)) * scale;
	return true;
}

/** the words of a text (at most n), each at most 31 characters */
static int onyx_mask_words(lwc_string *text, char words[][32], int n)
{
	const char *p;
	int k = 0;

	if (text == NULL)
		return 0;
	p = lwc_string_data(text);
	while (*p != '\0' && k < n) {
		int l = 0;

		while (*p == ' ')
			p++;
		if (*p == '\0')
			break;
		while (*p != '\0' && *p != ' ') {
			if (l < 31)
				words[k][l++] = *p;
			p++;
		}
		words[k++][l] = '\0';
	}
	return k;
}

/** one axis of mask-position: the offset of an image of size `img` in `area` */
static float onyx_mask_offset(const char *edge, const char *len, float area, float img,
		const css_computed_style *style, const css_unit_ctx *uctx, float scale)
{
	float v = 0;
	bool far = (strcmp(edge, "right") == 0 || strcmp(edge, "bottom") == 0);

	if (strcmp(edge, "center") == 0)
		return (area - img) / 2;
	if (len != NULL && onyx_mask_len(len, area - img, style, uctx, scale, &v))
		return far ? area - img - v : v;
	return far ? area - img : 0;
}

static bool onyx_mask_keyword(const char *w)
{
	return strcmp(w, "left") == 0 || strcmp(w, "right") == 0 ||
		strcmp(w, "top") == 0 || strcmp(w, "bottom") == 0 ||
		strcmp(w, "center") == 0;
}

static bool onyx_mask_vertical(const char *w)
{
	return strcmp(w, "top") == 0 || strcmp(w, "bottom") == 0;
}


/* exported function documented in html/onyx_mask.h */
bool onyx_mask_redraw(const html_content *html, struct box *box, int x, int y,
		int width, int height, float scale, const struct rect *clip,
		const struct redraw_context *ctx)
{
	const css_computed_style *style = box->style;
	const css_unit_ctx *uctx = &html->unit_len_ctx;
	struct plotter_table table;
	struct redraw_context mctx;
	struct content_redraw_data data;
	content_status st;
	lwc_string *text = NULL;
	char w[4][32];
	int n, iw, ih;
	float mw, mh, px, py;
	bool rx = true, ry = true;
	css_color bgc;
	struct rect r;

	if (box->mask == NULL || width <= 0 || height <= 0)
		return true;
	st = content_get_status(box->mask);
	if (st != CONTENT_STATUS_READY && st != CONTENT_STATUS_DONE)
		return true;
	css_computed_background_color(style, &bgc);
	if ((bgc >> 24) == 0)
		return true;	/* (a transparent colour: nothing shows) */

	/* the image's size: mask-size (its own size, cover, contain, lengths) */
	iw = content_get_width(box->mask);
	ih = content_get_height(box->mask);
	if (iw <= 0 || ih <= 0) {
		iw = width;	/* (an image without a size: the area) */
		ih = height;
	} else {
		iw *= scale;
		ih *= scale;
	}
	mw = iw;
	mh = ih;
	css_computed_mask_size(style, &text);
	n = onyx_mask_words(text, w, 2);
	if (n >= 1 && (strcmp(w[0], "cover") == 0 || strcmp(w[0], "contain") == 0)) {
		float sx = (float) width / iw, sy = (float) height / ih;
		float s;

		/* (cover: the larger scale, contain: the smaller) */
		if (strcmp(w[0], "contain") == 0)
			s = sx < sy ? sx : sy;
		else
			s = sx > sy ? sx : sy;
		mw = iw * s;
		mh = ih * s;
	} else if (n >= 1) {
		bool aw = !onyx_mask_len(w[0], width, style, uctx, scale, &mw);
		bool ah = n < 2 ? true :
			!onyx_mask_len(w[1], height, style, uctx, scale, &mh);

		if (n < 2 && !aw) {
			mh = mw * ih / iw;	/* (one length: the height by the ratio) */
		} else if (aw && ah) {
			mw = iw;
			mh = ih;
		} else if (aw) {
			mw = mh * iw / ih;
		} else if (ah) {
			mh = mw * ih / iw;
		}
	}
	if (mw < 1 || mh < 1)
		return true;

	/* its place: mask-position (0% 0% by default) */
	px = py = 0;
	css_computed_mask_position(style, &text);
	n = onyx_mask_words(text, w, 4);
	if (n == 1) {
		if (onyx_mask_vertical(w[0])) {
			px = (width - mw) / 2;
			py = onyx_mask_offset(w[0], NULL, height, mh, style, uctx, scale);
		} else {
			px = onyx_mask_keyword(w[0]) ?
				onyx_mask_offset(w[0], NULL, width, mw, style, uctx, scale) :
				onyx_mask_offset("left", w[0], width, mw, style, uctx, scale);
			py = (height - mh) / 2;
		}
	} else if (n == 2) {
		const char *a = w[0], *b = w[1];

		if (onyx_mask_vertical(a) || (strcmp(b, "left") == 0 ||
				strcmp(b, "right") == 0)) {
			a = w[1];	/* ("top left": the horizontal one first) */
			b = w[0];
		}
		px = onyx_mask_keyword(a) ?
			onyx_mask_offset(a, NULL, width, mw, style, uctx, scale) :
			onyx_mask_offset("left", a, width, mw, style, uctx, scale);
		py = onyx_mask_keyword(b) ?
			onyx_mask_offset(b, NULL, height, mh, style, uctx, scale) :
			onyx_mask_offset("top", b, height, mh, style, uctx, scale);
	} else if (n >= 3) {
		/* edges with offsets: "right 10px bottom 5px", "left top 5px"... */
		int i = 0;

		while (i < n) {
			const char *edge = w[i];
			const char *len = (i + 1 < n && !onyx_mask_keyword(w[i + 1])) ?
					w[i + 1] : NULL;

			if (onyx_mask_vertical(edge))
				py = onyx_mask_offset(edge, len, height, mh, style,
						uctx, scale);
			else
				px = onyx_mask_offset(edge, len, width, mw, style,
						uctx, scale);
			i += len != NULL ? 2 : 1;
		}
	}

	/* mask-repeat (repeat by default; space and round as repeat) */
	css_computed_mask_repeat(style, &text);
	n = onyx_mask_words(text, w, 2);
	if (n == 1) {
		if (strcmp(w[0], "no-repeat") == 0)
			rx = ry = false;
		else if (strcmp(w[0], "repeat-x") == 0)
			ry = false;
		else if (strcmp(w[0], "repeat-y") == 0)
			rx = false;
	} else if (n == 2) {
		rx = strcmp(w[0], "no-repeat") != 0;
		ry = strcmp(w[1], "no-repeat") != 0;
	}

	/* the border box (the mask's clip), within the redraw's clip */
	r.x0 = x > clip->x0 ? x : clip->x0;
	r.y0 = y > clip->y0 ? y : clip->y0;
	r.x1 = x + width < clip->x1 ? x + width : clip->x1;
	r.y1 = y + height < clip->y1 ? y + height : clip->y1;
	if (r.x0 >= r.x1 || r.y0 >= r.y1)
		return true;
	if (!rx) {
		/* (no repeat: the image's own box only) */
		if (x + px > r.x0)
			r.x0 = x + px;
		if (x + px + mw < r.x1)
			r.x1 = x + px + mw;
	}
	if (!ry) {
		if (y + py > r.y0)
			r.y0 = y + py;
		if (y + py + mh < r.y1)
			r.y1 = y + py + mh;
	}
	if (r.x0 >= r.x1 || r.y0 >= r.y1)
		return true;
	if (ctx->plot->clip(ctx, &r) != NSERROR_OK)
		return false;

	/* the image redrawn: its bitmap plots tinted */
	table = *ctx->plot;
	table.bitmap = onyx_mask_bitmap;
	table.rectangle = onyx_mask_none_rect;
	mctx = *ctx;
	mctx.plot = &table;
	onyx_mask_plot = ctx->plot;
	onyx_mask_colour = bgc;

	data.x = x + (int) px;
	data.y = y + (int) py;
	data.width = (int) (mw + 0.5f);
	data.height = (int) (mh + 0.5f);
	data.background_colour = 0xffffff;
	data.scale = scale;
	data.repeat_x = rx;
	data.repeat_y = ry;
	content_redraw(box->mask, &data, &r, &mctx);

	/* the redraw's clip again */
	return ctx->plot->clip(ctx, clip) == NSERROR_OK;
}
