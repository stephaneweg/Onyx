/*
 * Copyright 2004-2008 James Bursa <bursa@users.sourceforge.net>
 * Copyright 2004-2007 John M Bell <jmb202@ecs.soton.ac.uk>
 * Copyright 2004-2007 Richard Wilson <info@tinct.net>
 * Copyright 2005-2006 Adrian Lees <adrianl@users.sourceforge.net>
 * Copyright 2006 Rob Kendrick <rjek@netsurf-browser.org>
 * Copyright 2008 Michael Drake <tlsa@netsurf-browser.org>
 * Copyright 2009 Paul Blokus <paul_pl@users.sourceforge.net>
 *
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
 *
 * Redrawing CONTENT_HTML implementation.
 */

#include "utils/config.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dom/dom.h>

#include "utils/log.h"
#include "utils/messages.h"
#include "utils/utils.h"
#include "utils/nsoption.h"
#include "utils/corestrings.h"
#include "netsurf/content.h"
#include "netsurf/browser_window.h"
#include "netsurf/plotters.h"
#include "netsurf/bitmap.h"
#include "netsurf/layout.h"
#include "content/content.h"
#include "content/content_protected.h"
#include "content/textsearch.h"
#include "css/utils.h"
#include "desktop/selection.h"
#include "desktop/print.h"
#include "desktop/scrollbar.h"
#include "desktop/textarea.h"
#include "desktop/gui_internal.h"

#include "html/box.h"
#include "html/box_inspect.h"
#include "html/onyx_paint.h"	/* Onyx: radii, shadows, gradients */
#include "html/onyx_webfont.h"	/* Onyx: web fonts */
#include "html/onyx_mask.h"	/* Onyx: mask-image */
#include "html/onyx_fx.h"	/* Onyx: compositing layers */
#include "netsurf/onyx_perf.h"	/* Onyx: a layer's time (NS_PERF) */
#include "html/box_manipulate.h"
#include "html/font.h"
#include "html/form_internal.h"
#include "html/private.h"
#include "html/html.h"	/* Onyx: html_box_viewport_fixed */
#include "html/layout.h"
#include "javascript/quickjs/qjs_media.h"	/* Onyx: <video>, <audio> */


bool html_redraw_debug = false;

/* Onyx: the printing flags (desktop/print.h), defined in print.c upstream -- removed with it:
 * Jet does not print, they stay off */
bool html_redraw_printing = false;
int html_redraw_printing_border = 0;
int html_redraw_printing_top_cropped = 0;

/**
 * Onyx: background-clip: text -- the paint of the box being drawn whose background paints
 * its text (its descendants'), or NULL; set by html_redraw_box_inner, restored by
 * html_redraw_box.
 */
static const struct onyx_paint *onyx_text_fill;

/**
 * Determine if a box has a background that needs drawing
 *
 * \param box  Box to consider
 * \return True if box has a background, false otherwise.
 */
static bool html_redraw_box_has_background(struct box *box)
{
	if (box->background != NULL)
		return true;

	if (box->style != NULL) {
		css_color colour;

		css_computed_background_color(box->style, &colour);

		if (nscss_color_is_transparent(colour) == false)
			return true;

		/* Onyx: a gradient (not fetched: box->background is NULL) */
		if (onyx_background_gradient(box->style) != NULL)
			return true;
	}

	return false;
}

/**
 * Find the background box for a box
 *
 * \param box  Box to find background box for
 * \return Pointer to background box, or NULL if there is none
 */
static struct box *html_redraw_find_bg_box(struct box *box)
{
	/* Thanks to backwards compatibility, CSS defines the following:
	 *
	 * + If the box is for the root element and it has a background,
	 *   use that (and then process the body box with no special case)
	 * + If the box is for the root element and it has no background,
	 *   then use the background (if any) from the body element as if
	 *   it were specified on the root. Then, when the box for the body
	 *   element is processed, ignore the background.
	 * + For any other box, just use its own styling.
	 */
	if (box->parent == NULL) {
		/* Root box */
		if (html_redraw_box_has_background(box))
			return box;

		/* No background on root box: consider body box, if any */
		if (box->children != NULL) {
			if (html_redraw_box_has_background(box->children))
				return box->children;
		}
	} else if (box->parent != NULL && box->parent->parent == NULL) {
		/* Body box: only render background if root has its own */
		if (html_redraw_box_has_background(box) &&
				html_redraw_box_has_background(box->parent))
			return box;
	} else {
		/* Any other box */
		if (html_redraw_box_has_background(box))
			return box;
	}

	return NULL;
}

/**
 * Onyx (docs/06 §40): a text with the find's matches in it -- the text as ever, then each
 * match on its colour (Chrome's: the current one orange, the others yellow), in black,
 * clipped to the match.
 */
static bool
onyx_find_redraw(const char *utf8_text, size_t utf8_len, int space,
		 const plot_font_style_t *fstyle,
		 const plot_font_style_t *plot_fstyle,
		 int x, int y, int baseline, const struct rect *clip,
		 int height, float scale,
		 const struct content_textsearch_range *r, int n,
		 const struct redraw_context *ctx)
{
	int i;

	if (ctx->plot->text(ctx, plot_fstyle, x, y + baseline, utf8_text,
			utf8_len) != NSERROR_OK)
		return false;
	for (i = 0; i < n; i++) {
		plot_style_t fill = *plot_style_fill_white;
		plot_font_style_t hl = *plot_fstyle;
		unsigned s = r[i].start, e = r[i].end;
		unsigned et = e > utf8_len ? utf8_len : e;
		int sx = 0, ex = 0;
		struct rect rc, cl;

		if (s > utf8_len)
			s = utf8_len;
		if (guit->layout->width(fstyle, utf8_text, s, &sx) != NSERROR_OK)
			sx = 0;
		if (guit->layout->width(fstyle, utf8_text, et, &ex) != NSERROR_OK)
			ex = 0;
		if (e > utf8_len)
			ex += space;	/* (the trailing space) */
		if (scale != 1.0) {
			sx *= scale;
			ex *= scale;
		}
		if (ex <= sx)
			continue;
		fill.fill_colour = r[i].current ? 0x3296ff : 0x00ffff;
		rc.x0 = x + sx;
		rc.y0 = y;
		rc.x1 = x + ex;
		rc.y1 = y + height * scale;
		if (ctx->plot->rectangle(ctx, &fill, &rc) != NSERROR_OK)
			return false;
		cl.x0 = max(rc.x0, clip->x0);
		cl.y0 = clip->y0;
		cl.x1 = min(rc.x1, clip->x1);
		cl.y1 = clip->y1;
		if (cl.x0 >= cl.x1)
			continue;
		hl.foreground = 0x000000;
		hl.background = fill.fill_colour;
		if (ctx->plot->clip(ctx, &cl) != NSERROR_OK ||
		    ctx->plot->text(ctx, &hl, x, y + baseline, utf8_text,
				utf8_len) != NSERROR_OK ||
		    ctx->plot->clip(ctx, clip) != NSERROR_OK)
			return false;
	}
	return true;
}


/**
 * Redraw a short text string, complete with highlighting
 * (for selection/search)
 *
 * \param utf8_text pointer to UTF-8 text string
 * \param utf8_len  length of string, in bytes
 * \param offset    byte offset within textual representation
 * \param space     width of space that follows string (0 = no space)
 * \param fstyle    text style to use (pass text size unscaled)
 * \param x         x ordinate at which to plot text
 * \param y         y ordinate at which to plot text
 * \param clip      pointer to current clip rectangle
 * \param height    height of text string
 * \param scale     current display scale (1.0 = 100%)
 * \param excluded  exclude this text string from the selection
 * \param c         Content being redrawn.
 * \param sel       Selection context
 * \param search    Search context
 * \param ctx	    current redraw context
 * \return true iff successful and redraw should proceed
 */

static bool
text_redraw(const char *utf8_text,
	    size_t utf8_len,
	    size_t offset,
	    int space,
	    const plot_font_style_t *fstyle,
	    int x,
	    int y,
	    const struct rect *clip,
	    int height,
	    float scale,
	    bool excluded,
	    struct content *c,
	    const struct selection *sel,
	    const struct redraw_context *ctx)
{
	bool highlighted = false;
	plot_font_style_t plot_fstyle = *fstyle;
	nserror res;
	/* Onyx: the baseline where the layout put it (its font's ascent below the half
	 * leading), not three quarters down */
	int baseline = (int) (font_baseline(fstyle, height) * scale);

	/* Need scaled text size to pass to plotters */
	plot_fstyle.size *= scale;

	/* is this box part of a selection? */
	if (!excluded && ctx->interactive == true) {
		unsigned len = utf8_len + (space ? 1 : 0);
		unsigned start_idx;
		unsigned end_idx;

		/* first try the browser window's current selection */
		if (selection_highlighted(sel,
					  offset,
					  offset + len,
					  &start_idx,
					  &end_idx)) {
			highlighted = true;
		}

		/* what about the current search operation, if any? (Onyx: all
		 * of its matches in the text, each in its colour: §40) */
		if (!highlighted &&
		    (c->textsearch.context != NULL)) {
			struct content_textsearch_range r[16];
			int n = content_textsearch_onyx_ranges(
					c->textsearch.context,
					offset, offset + len, r, 16);
			if (n > 0)
				return onyx_find_redraw(utf8_text, utf8_len,
						space, fstyle, &plot_fstyle,
						x, y, baseline, clip, height,
						scale, r, n, ctx);
		}

		/* \todo make search terms visible within selected text */
		if (highlighted) {
			struct rect r;
			unsigned endtxt_idx = end_idx;
			bool clip_changed = false;
			bool text_visible = true;
			int startx, endx;
			plot_style_t pstyle_fill_hback = *plot_style_fill_white;
			plot_font_style_t fstyle_hback = plot_fstyle;

			if (end_idx > utf8_len) {
				/* adjust for trailing space, not present in
				 * utf8_text */
				assert(end_idx == utf8_len + 1);
				endtxt_idx = utf8_len;
			}

			res = guit->layout->width(fstyle,
						  utf8_text, start_idx,
						  &startx);
			if (res != NSERROR_OK) {
				startx = 0;
			}

			res = guit->layout->width(fstyle,
						  utf8_text, endtxt_idx,
						  &endx);
			if (res != NSERROR_OK) {
				endx = 0;
			}

			/* is there a trailing space that should be highlighted
			 * as well? */
			if (end_idx > utf8_len) {
					endx += space;
			}

			if (scale != 1.0) {
				startx *= scale;
				endx *= scale;
			}

			/* draw any text preceding highlighted portion */
			if ((start_idx > 0) &&
			    (ctx->plot->text(ctx,
					     &plot_fstyle,
					     x,
					     y + baseline,
					     utf8_text,
					     start_idx) != NSERROR_OK))
				return false;

			pstyle_fill_hback.fill_colour = fstyle->foreground;

			/* highlighted portion */
			r.x0 = x + startx;
			r.y0 = y;
			r.x1 = x + endx;
			r.y1 = y + height * scale;
			res = ctx->plot->rectangle(ctx, &pstyle_fill_hback, &r);
			if (res != NSERROR_OK) {
				return false;
			}

			if (start_idx > 0) {
				int px0 = max(x + startx, clip->x0);
				int px1 = min(x + endx, clip->x1);

				if (px0 < px1) {
					r.x0 = px0;
					r.y0 = clip->y0;
					r.x1 = px1;
					r.y1 = clip->y1;
					res = ctx->plot->clip(ctx, &r);
					if (res != NSERROR_OK) {
						return false;
					}

					clip_changed = true;
				} else {
					text_visible = false;
				}
			}

			fstyle_hback.background =
				pstyle_fill_hback.fill_colour;
			fstyle_hback.foreground = colour_to_bw_furthest(
				pstyle_fill_hback.fill_colour);

			if (text_visible &&
			    (ctx->plot->text(ctx,
					     &fstyle_hback,
					     x,
					     y + baseline,
					     utf8_text,
					     endtxt_idx) != NSERROR_OK)) {
				return false;
			}

			/* draw any text succeeding highlighted portion */
			if (endtxt_idx < utf8_len) {
				int px0 = max(x + endx, clip->x0);
				if (px0 < clip->x1) {

					r.x0 = px0;
					r.y0 = clip->y0;
					r.x1 = clip->x1;
					r.y1 = clip->y1;
					res = ctx->plot->clip(ctx, &r);
					if (res != NSERROR_OK) {
						return false;
					}

					clip_changed = true;

					res = ctx->plot->text(ctx,
							      &plot_fstyle,
							      x,
							      y + baseline,
							      utf8_text,
							      utf8_len);
					if (res != NSERROR_OK) {
						return false;
					}
				}
			}

			if (clip_changed &&
			    (ctx->plot->clip(ctx, clip) != NSERROR_OK)) {
				return false;
			}
		}
	}

	if (!highlighted) {
		if (onyx_text_fill != NULL && ctx->plot->onyx_text_paint != NULL)
			/* Onyx: background-clip: text */
			res = ctx->plot->onyx_text_paint(ctx, &plot_fstyle, x,
					y + baseline,
					utf8_text, utf8_len, onyx_text_fill);
		else
			res = ctx->plot->text(ctx,
				      &plot_fstyle,
				      x,
				      y + baseline,
				      utf8_text,
				      utf8_len);
		if (res != NSERROR_OK) {
			return false;
		}
	}
	return true;
}


/**
 * Plot a checkbox.
 *
 * \param  x	     left coordinate
 * \param  y	     top coordinate
 * \param  width     dimensions of checkbox
 * \param  height    dimensions of checkbox
 * \param  selected  the checkbox is selected
 * \param  ctx	     current redraw context
 * \return true if successful, false otherwise
 */

static bool html_redraw_checkbox(int x, int y, int width, int height,
		bool selected, const struct redraw_context *ctx)
{
	double z;
	nserror res;
	struct rect rect;

	z = width * 0.15;
	if (z == 0) {
		z = 1;
	}

	rect.x0 = x;
	rect.y0 = y ;
	rect.x1 = x + width;
	rect.y1 = y + height;
	res = ctx->plot->rectangle(ctx, plot_style_fill_wbasec, &rect);
	if (res != NSERROR_OK) {
		return false;
	}

	/* dark line across top */
	rect.y1 = y;
	res = ctx->plot->line(ctx, plot_style_stroke_darkwbasec, &rect);
	if (res != NSERROR_OK) {
		return false;
	}

	/* dark line across left */
	rect.x1 = x;
	rect.y1 = y + height;
	res = ctx->plot->line(ctx, plot_style_stroke_darkwbasec, &rect);
	if (res != NSERROR_OK) {
		return false;
	}

	/* light line across right */
	rect.x0 = x + width;
	rect.x1 = x + width;
	res = ctx->plot->line(ctx, plot_style_stroke_lightwbasec, &rect);
	if (res != NSERROR_OK) {
		return false;
	}

	/* light line across bottom */
	rect.x0 = x;
	rect.y0 = y + height;
	res = ctx->plot->line(ctx, plot_style_stroke_lightwbasec, &rect);
	if (res != NSERROR_OK) {
		return false;
	}

	if (selected) {
		if (width < 12 || height < 12) {
			/* render a solid box instead of a tick */
			rect.x0 = x + z + z;
			rect.y0 = y + z + z;
			rect.x1 = x + width - z;
			rect.y1 = y + height - z;
			res = ctx->plot->rectangle(ctx, plot_style_fill_wblobc, &rect);
			if (res != NSERROR_OK) {
				return false;
			}
		} else {
			/* render a tick, as it'll fit comfortably */
			rect.x0 = x + width - z;
			rect.y0 = y + z;
			rect.x1 = x + (z * 3);
			rect.y1 = y + height - z;
			res = ctx->plot->line(ctx, plot_style_stroke_wblobc, &rect);
			if (res != NSERROR_OK) {
				return false;
			}

			rect.x0 = x + (z * 3);
			rect.y0 = y + height - z;
			rect.x1 = x + z + z;
			rect.y1 = y + (height / 2);
			res = ctx->plot->line(ctx, plot_style_stroke_wblobc, &rect);
			if (res != NSERROR_OK) {
				return false;
			}
		}
	}
	return true;
}


/**
 * Plot a radio icon.
 *
 * \param  x	     left coordinate
 * \param  y	     top coordinate
 * \param  width     dimensions of radio icon
 * \param  height    dimensions of radio icon
 * \param  selected  the radio icon is selected
 * \param  ctx	     current redraw context
 * \return true if successful, false otherwise
 */
static bool html_redraw_radio(int x, int y, int width, int height,
		bool selected, const struct redraw_context *ctx)
{
	nserror res;

	/* plot background of radio button */
	res = ctx->plot->disc(ctx,
			      plot_style_fill_wbasec,
			      x + width * 0.5,
			      y + height * 0.5,
			      width * 0.5 - 1);
	if (res != NSERROR_OK) {
		return false;
	}

	/* plot dark arc */
	res = ctx->plot->arc(ctx,
			     plot_style_fill_darkwbasec,
			     x + width * 0.5,
			     y + height * 0.5,
			     width * 0.5 - 1,
			     45,
			     225);
	if (res != NSERROR_OK) {
		return false;
	}

	/* plot light arc */
	res = ctx->plot->arc(ctx,
			     plot_style_fill_lightwbasec,
			     x + width * 0.5,
			     y + height * 0.5,
			     width * 0.5 - 1,
			     225,
			     45);
	if (res != NSERROR_OK) {
		return false;
	}

	if (selected) {
		/* plot selection blob */
		res = ctx->plot->disc(ctx,
				      plot_style_fill_wblobc,
				      x + width * 0.5,
				      y + height * 0.5,
				      width * 0.3 - 1);
		if (res != NSERROR_OK) {
			return false;
		}
	}

	return true;
}


/**
 * Plot a file upload input.
 *
 * \param  x	     left coordinate
 * \param  y	     top coordinate
 * \param  width     dimensions of input
 * \param  height    dimensions of input
 * \param  box	     box of input
 * \param  scale     scale for redraw
 * \param  background_colour  current background colour
 * \param  unit_len_ctx   Length conversion context
 * \param  ctx	     current redraw context
 * \return true if successful, false otherwise
 */

static bool html_redraw_file(int x, int y, int width, int height,
		struct box *box, float scale, colour background_colour,
		const css_unit_ctx *unit_len_ctx,
		const struct redraw_context *ctx)
{
	int text_width;
	const char *text;
	size_t length;
	plot_font_style_t fstyle;
	nserror res;

	font_plot_style_from_css(unit_len_ctx, box->style, &fstyle);
	fstyle.background = background_colour;

	if (box->gadget->value) {
		text = box->gadget->value;
	} else {
		text = messages_get("Form_Drop");
	}
	length = strlen(text);

	res = guit->layout->width(&fstyle, text, length, &text_width);
	if (res != NSERROR_OK) {
		return false;
	}
	text_width *= scale;
	if (width < text_width + 8) {
		x = x + width - text_width - 4;
	} else {
		x = x + 4;
	}

	res = ctx->plot->text(ctx, &fstyle, x, y + height * 0.75, text, length);
	if (res != NSERROR_OK) {
		return false;
	}
	return true;
}


/**
 * Onyx: a background image's drawn size (CSS background-size: auto, contain, cover,
 * lengths and percentages of the area; an auto side from the image's ratio), for an
 * area of area_w x area_h CSS px. libcss parsed it; the redraw used the image's own size.
 */
static void onyx_background_size(const css_computed_style *style,
		struct hlcache_handle *image, int area_w, int area_h,
		const css_unit_ctx *unit_len_ctx, int *w, int *h)
{
	css_fixed sw = 0, sh = 0;
	css_unit swu = CSS_UNIT_PX, shu = CSS_UNIT_PX;
	int iw = content_get_width(image), ih = content_get_height(image);
	float fw, fh;

	*w = iw;
	*h = ih;
	if (iw <= 0 || ih <= 0)
		return;
	switch (css_computed_background_size(style, &sw, &swu, &sh, &shu)) {
	case CSS_BACKGROUND_SIZE_CONTAIN:
	case CSS_BACKGROUND_SIZE_COVER: {
		float sx = (float) area_w / iw, sy = (float) area_h / ih, k;

		if (css_computed_background_size(style, &sw, &swu, &sh, &shu) ==
				CSS_BACKGROUND_SIZE_CONTAIN)
			k = sx < sy ? sx : sy;
		else
			k = sx > sy ? sx : sy;
		fw = iw * k;
		fh = ih * k;
		break;
	}
	case CSS_BACKGROUND_SIZE_SET:
	case CSS_BACKGROUND_SIZE_SET_WIDTH:
	case CSS_BACKGROUND_SIZE_SET_HEIGHT: {
		uint8_t t = css_computed_background_size(style, &sw, &swu, &sh, &shu);
		bool hasw = t != CSS_BACKGROUND_SIZE_SET_HEIGHT;
		bool hash = t != CSS_BACKGROUND_SIZE_SET_WIDTH;

		if (hasw)
			fw = swu == CSS_UNIT_PCT ? area_w * FIXTOFLT(sw) / 100.f :
				FIXTOFLT(css_unit_len2device_px(style, unit_len_ctx, sw, swu));
		if (hash)
			fh = shu == CSS_UNIT_PCT ? area_h * FIXTOFLT(sh) / 100.f :
				FIXTOFLT(css_unit_len2device_px(style, unit_len_ctx, sh, shu));
		if (!hasw)
			fw = fh * iw / ih;
		if (!hash)
			fh = fw * ih / iw;
		break;
	}
	default:
		return;
	}
	*w = fw < 1 ? 1 : (int) (fw + 0.5f);
	*h = fh < 1 ? 1 : (int) (fh + 0.5f);
}


/**
 * Plot background images.
 *
 * The reason for the presence of \a background is the backwards compatibility
 * mess that is backgrounds on &lt;body&gt;. The background will be drawn relative
 * to \a box, using the background information contained within \a background.
 *
 * \param  x	  coordinate of box
 * \param  y	  coordinate of box
 * \param  box	  box to draw background image of
 * \param  scale  scale for redraw
 * \param  clip   current clip rectangle
 * \param  background_colour  current background colour
 * \param  background  box containing background details (usually \a box)
 * \param  unit_len_ctx  Length conversion context
 * \param  ctx      current redraw context
 * \return true if successful, false otherwise
 */

/* Onyx: the viewport of the document being painted (CSS px: its scroll offset, its size;
 * html_redraw) -- what position: fixed and background-attachment: fixed are relative to */
static int onyx_view_sx, onyx_view_sy, onyx_view_w, onyx_view_h;

static bool html_redraw_background(int x, int y, struct box *box, float scale,
		const struct rect *clip, colour *background_colour,
		struct box *background,
		const css_unit_ctx *unit_len_ctx,
		const struct redraw_context *ctx)
{
	bool repeat_x = false;
	bool repeat_y = false;
	bool plot_colour = true;
	bool plot_content;
	bool clip_to_children = false;
	struct box *clip_box = box;
	int ox = x, oy = y;
	int width, height;
	int bg_w = 0, bg_h = 0;	/* (Onyx: the image's drawn size: background-size) */
	css_fixed hpos = 0, vpos = 0;
	css_unit hunit = CSS_UNIT_PX, vunit = CSS_UNIT_PX;
	struct box *parent;
	struct rect r = *clip;
	css_color bgcol;
	plot_style_t pstyle_fill_bg = {
		.fill_type = PLOT_OP_TYPE_SOLID,
		.fill_colour = *background_colour,
	};
	nserror res;

	if (ctx->background_images == false)
		return true;

	plot_content = (background->background != NULL);

	if (plot_content && onyx_view_w > 0 &&
	    css_computed_background_attachment(background->style) ==
			CSS_BACKGROUND_ATTACHMENT_FIXED) {
		/* Onyx: background-attachment: fixed -- its positioning area is
		 * the viewport (the box's painting area clips it as ever):
		 * Acid2's eyes and chin */
		int bx, by;

		box_coords(box, &bx, &by);
		x += (onyx_view_sx - bx) * scale;
		y += (onyx_view_sy - by) * scale;
		width = onyx_view_w;
		height = onyx_view_h;
		goto onyx_positioned_area;
	}
	if (plot_content) {
		if (!box->parent) {
			/* Root element, special case:
			 * background origin calc. is based on margin box */
			x -= box->margin[LEFT] * scale;
			y -= box->margin[TOP] * scale;
			width = box->margin[LEFT] + box->padding[LEFT] +
					box->width + box->padding[RIGHT] +
					box->margin[RIGHT];
			height = box->margin[TOP] + box->padding[TOP] +
					box->height + box->padding[BOTTOM] +
					box->margin[BOTTOM];
		} else {
			width = box->padding[LEFT] + box->width +
					box->padding[RIGHT];
			height = box->padding[TOP] + box->height +
					box->padding[BOTTOM];
		}
	onyx_positioned_area:
		/* handle background-repeat */
		switch (css_computed_background_repeat(background->style)) {
		case CSS_BACKGROUND_REPEAT_REPEAT:
			repeat_x = repeat_y = true;
			/* optimisation: only plot the colour if
			 * bitmap is not opaque */
			plot_colour = !content_get_opaque(background->background);
			break;

		case CSS_BACKGROUND_REPEAT_REPEAT_X:
			repeat_x = true;
			break;

		case CSS_BACKGROUND_REPEAT_REPEAT_Y:
			repeat_y = true;
			break;

		case CSS_BACKGROUND_REPEAT_NO_REPEAT:
			break;

		default:
			break;
		}

		onyx_background_size(background->style, background->background,
				width, height, unit_len_ctx, &bg_w, &bg_h);

		/* handle background-position */
		css_computed_background_position(background->style,
				&hpos, &hunit, &vpos, &vunit);
		if (hunit == CSS_UNIT_PCT) {
			x += (width - bg_w) *
				scale * FIXTOFLT(hpos) / 100.;
		} else {
			x += (int) (FIXTOFLT(css_unit_len2device_px(
					background->style, unit_len_ctx,
					hpos, hunit)) * scale);
		}

		if (vunit == CSS_UNIT_PCT) {
			y += (height - bg_h) *
				scale * FIXTOFLT(vpos) / 100.;
		} else {
			y += (int) (FIXTOFLT(css_unit_len2device_px(
					background->style, unit_len_ctx,
					vpos, vunit)) * scale);
		}
	}

	/* special case for table rows as their background needs
	 * to be clipped to all the cells */
	if (box->type == BOX_TABLE_ROW) {
		css_fixed h = 0, v = 0;
		css_unit hu = CSS_UNIT_PX, vu = CSS_UNIT_PX;

		for (parent = box->parent;
			((parent) && (parent->type != BOX_TABLE));
				parent = parent->parent);
		assert(parent && (parent->style));

		css_computed_border_spacing(parent->style, &h, &hu, &v, &vu);

		clip_to_children = (h > 0) || (v > 0);

		if (clip_to_children)
			clip_box = box->children;
	}

	for (; clip_box; clip_box = clip_box->next) {
		/* clip to child boxes if needed */
		if (clip_to_children) {
			assert(clip_box->type == BOX_TABLE_CELL);

			/* update clip.* to the child cell */
			r.x0 = ox + (clip_box->x * scale);
			r.y0 = oy + (clip_box->y * scale);
			r.x1 = r.x0 + (clip_box->padding[LEFT] +
					clip_box->width +
					clip_box->padding[RIGHT]) * scale;
			r.y1 = r.y0 + (clip_box->padding[TOP] +
					clip_box->height +
					clip_box->padding[BOTTOM]) * scale;

			if (r.x0 < clip->x0) r.x0 = clip->x0;
			if (r.y0 < clip->y0) r.y0 = clip->y0;
			if (r.x1 > clip->x1) r.x1 = clip->x1;
			if (r.y1 > clip->y1) r.y1 = clip->y1;

			css_computed_background_color(clip_box->style, &bgcol);

			/* <td> attributes override <tr> */
			/* if the background content is opaque there
			 * is no need to plot underneath it.
			 */
			if ((r.x0 >= r.x1) ||
			    (r.y0 >= r.y1) ||
			    (nscss_color_is_transparent(bgcol) == false) ||
			    ((clip_box->background != NULL) &&
			     content_get_opaque(clip_box->background)))
				continue;
		}

		/* plot the background colour */
		css_computed_background_color(background->style, &bgcol);

		if (nscss_color_is_transparent(bgcol) == false) {
			*background_colour = nscss_color_to_ns(bgcol);
			pstyle_fill_bg.fill_colour = *background_colour;
			if (plot_colour) {
				res = ctx->plot->rectangle(ctx, &pstyle_fill_bg, &r);
				if (res != NSERROR_OK) {
					return false;
				}
			}
		}
		/* and plot the image */
		if (plot_content) {
			width = bg_w;	/* (Onyx: background-size) */
			height = bg_h;

			/* ensure clip area only as large as required */
			if (!repeat_x) {
				if (r.x0 < x)
					r.x0 = x;
				if (r.x1 > x + width * scale)
					r.x1 = x + width * scale;
			}
			if (!repeat_y) {
				if (r.y0 < y)
					r.y0 = y;
				if (r.y1 > y + height * scale)
					r.y1 = y + height * scale;
			}
			/* valid clipping rectangles only */
			if ((r.x0 < r.x1) && (r.y0 < r.y1)) {
				struct content_redraw_data bg_data;

				res = ctx->plot->clip(ctx, &r);
				if (res != NSERROR_OK) {
					return false;
				}

				bg_data.x = x;
				bg_data.y = y;
				bg_data.width = ceilf(width * scale);
				bg_data.height = ceilf(height * scale);
				bg_data.background_colour = *background_colour;
				bg_data.scale = scale;
				bg_data.repeat_x = repeat_x;
				bg_data.repeat_y = repeat_y;

				/* We just continue if redraw fails */
				content_redraw(background->background,
						&bg_data, &r, ctx);
			}
		}

		/* only <tr> rows being clipped to child boxes loop */
		if (!clip_to_children)
			return true;
	}
	return true;
}


/**
 * Plot an inline's background and/or background image.
 *
 * \param  x	  coordinate of box
 * \param  y	  coordinate of box
 * \param  box	  BOX_INLINE which created the background
 * \param  scale  scale for redraw
 * \param  clip	  coordinates of clip rectangle
 * \param  b	  coordinates of border edge rectangle
 * \param  first  true if this is the first rectangle associated with the inline
 * \param  last   true if this is the last rectangle associated with the inline
 * \param  background_colour  updated to current background colour if plotted
 * \param  unit_len_ctx  Length conversion context
 * \param  ctx      current redraw context
 * \return true if successful, false otherwise
 */

static bool html_redraw_inline_background(int x, int y, struct box *box,
		float scale, const struct rect *clip, struct rect b,
		bool first, bool last, colour *background_colour,
		const css_unit_ctx *unit_len_ctx,
		const struct redraw_context *ctx)
{
	struct rect r = *clip;
	bool repeat_x = false;
	bool repeat_y = false;
	bool plot_colour = true;
	bool plot_content;
	css_fixed hpos = 0, vpos = 0;
	css_unit hunit = CSS_UNIT_PX, vunit = CSS_UNIT_PX;
	css_color bgcol;
	plot_style_t pstyle_fill_bg = {
		.fill_type = PLOT_OP_TYPE_SOLID,
		.fill_colour = *background_colour,
	};
	nserror res;
	int bg_w = 0, bg_h = 0;	/* (Onyx: background-size) */

	plot_content = (box->background != NULL);
	if (plot_content)
		onyx_background_size(box->style, box->background,
				(b.x1 - b.x0) / scale, (b.y1 - b.y0) / scale,
				unit_len_ctx, &bg_w, &bg_h);

	if (html_redraw_printing && nsoption_bool(remove_backgrounds))
		return true;

	if (plot_content) {
		/* handle background-repeat */
		switch (css_computed_background_repeat(box->style)) {
		case CSS_BACKGROUND_REPEAT_REPEAT:
			repeat_x = repeat_y = true;
			/* optimisation: only plot the colour if
			 * bitmap is not opaque
			 */
			plot_colour = !content_get_opaque(box->background);
			break;

		case CSS_BACKGROUND_REPEAT_REPEAT_X:
			repeat_x = true;
			break;

		case CSS_BACKGROUND_REPEAT_REPEAT_Y:
			repeat_y = true;
			break;

		case CSS_BACKGROUND_REPEAT_NO_REPEAT:
			break;

		default:
			break;
		}

		/* handle background-position */
		css_computed_background_position(box->style,
				&hpos, &hunit, &vpos, &vunit);
		if (hunit == CSS_UNIT_PCT) {
			x += (b.x1 - b.x0 -
					bg_w *
					scale) * FIXTOFLT(hpos) / 100.;

			if (!repeat_x && ((hpos < 2 && !first) ||
					(hpos > 98 && !last))){
				plot_content = false;
			}
		} else {
			x += (int) (FIXTOFLT(css_unit_len2device_px(
					box->style, unit_len_ctx,
					hpos, hunit)) * scale);
		}

		if (vunit == CSS_UNIT_PCT) {
			y += (b.y1 - b.y0 -
					bg_h *
					scale) * FIXTOFLT(vpos) / 100.;
		} else {
			y += (int) (FIXTOFLT(css_unit_len2device_px(
					box->style, unit_len_ctx,
					vpos, vunit)) * scale);
		}
	}

	/* plot the background colour */
	css_computed_background_color(box->style, &bgcol);

	if (nscss_color_is_transparent(bgcol) == false) {
		*background_colour = nscss_color_to_ns(bgcol);
		pstyle_fill_bg.fill_colour = *background_colour;

		if (plot_colour) {
			res = ctx->plot->rectangle(ctx, &pstyle_fill_bg, &r);
			if (res != NSERROR_OK) {
				return false;
			}
		}
	}
	/* and plot the image */
	if (plot_content) {
		int width = bg_w;
		int height = bg_h;

		if (!repeat_x) {
			if (r.x0 < x)
				r.x0 = x;
			if (r.x1 > x + width * scale)
				r.x1 = x + width * scale;
		}
		if (!repeat_y) {
			if (r.y0 < y)
				r.y0 = y;
			if (r.y1 > y + height * scale)
				r.y1 = y + height * scale;
		}
		/* valid clipping rectangles only */
		if ((r.x0 < r.x1) && (r.y0 < r.y1)) {
			struct content_redraw_data bg_data;

			res = ctx->plot->clip(ctx, &r);
			if (res != NSERROR_OK) {
				return false;
			}

			bg_data.x = x;
			bg_data.y = y;
			bg_data.width = ceilf(width * scale);
			bg_data.height = ceilf(height * scale);
			bg_data.background_colour = *background_colour;
			bg_data.scale = scale;
			bg_data.repeat_x = repeat_x;
			bg_data.repeat_y = repeat_y;

			/* We just continue if redraw fails */
			content_redraw(box->background, &bg_data, &r, ctx);
		}
	}

	return true;
}


/**
 * Plot text decoration for an inline box.
 *
 * \param  box     box to plot decorations for, of type BOX_INLINE
 * \param  x       x coordinate of parent of box
 * \param  y       y coordinate of parent of box
 * \param  scale   scale for redraw
 * \param  colour  colour for decorations
 * \param  ratio   position of line as a ratio of line height
 * \param  ctx	   current redraw context
 * \return true if successful, false otherwise
 */

static bool
html_redraw_text_decoration_inline(struct box *box,
				   int x, int y,
				   float scale,
				   colour colour,
				   float ratio,
				   const struct redraw_context *ctx)
{
	struct box *c;
	plot_style_t plot_style_box = {
		.stroke_type = PLOT_OP_TYPE_SOLID,
		.stroke_colour = colour,
	};
	nserror res;
	struct rect rect;

	for (c = box->next;
	     c && c != box->inline_end;
	     c = c->next) {
		if (c->type != BOX_TEXT) {
			continue;
		}
		rect.x0 = (x + c->x) * scale;
		rect.y0 = (y + c->y + c->height * ratio) * scale;
		rect.x1 = (x + c->x + c->width) * scale;
		rect.y1 = (y + c->y + c->height * ratio) * scale;
		res = ctx->plot->line(ctx, &plot_style_box, &rect);
		if (res != NSERROR_OK) {
			return false;
		}
	}
	return true;
}


/**
 * Plot text decoration for an non-inline box.
 *
 * \param  box     box to plot decorations for, of type other than BOX_INLINE
 * \param  x       x coordinate of box
 * \param  y       y coordinate of box
 * \param  scale   scale for redraw
 * \param  colour  colour for decorations
 * \param  ratio   position of line as a ratio of line height
 * \param  ctx	   current redraw context
 * \return true if successful, false otherwise
 */

static bool
html_redraw_text_decoration_block(struct box *box,
				  int x, int y,
				  float scale,
				  colour colour,
				  float ratio,
				  const struct redraw_context *ctx)
{
	struct box *c;
	plot_style_t plot_style_box = {
		.stroke_type = PLOT_OP_TYPE_SOLID,
		.stroke_colour = colour,
	};
	nserror res;
	struct rect rect;

	/* draw through text descendants */
	for (c = box->children; c; c = c->next) {
		if (c->type == BOX_TEXT) {
			rect.x0 = (x + c->x) * scale;
			rect.y0 = (y + c->y + c->height * ratio) * scale;
			rect.x1 = (x + c->x + c->width) * scale;
			rect.y1 = (y + c->y + c->height * ratio) * scale;
			res = ctx->plot->line(ctx, &plot_style_box, &rect);
			if (res != NSERROR_OK) {
				return false;
			}
		} else if ((c->type == BOX_INLINE_CONTAINER) || (c->type == BOX_BLOCK)) {
			if (!html_redraw_text_decoration_block(c,
					x + c->x, y + c->y,
					scale, colour, ratio, ctx))
				return false;
		}
	}
	return true;
}


/**
 * Plot text decoration for a box.
 *
 * \param  box       box to plot decorations for
 * \param  x_parent  x coordinate of parent of box
 * \param  y_parent  y coordinate of parent of box
 * \param  scale     scale for redraw
 * \param  background_colour  current background colour
 * \param  ctx	     current redraw context
 * \return true if successful, false otherwise
 */

static bool html_redraw_text_decoration(struct box *box,
		int x_parent, int y_parent, float scale,
		colour background_colour, const struct redraw_context *ctx)
{
	static const enum css_text_decoration_e decoration[] = {
		CSS_TEXT_DECORATION_UNDERLINE, CSS_TEXT_DECORATION_OVERLINE,
		CSS_TEXT_DECORATION_LINE_THROUGH };
	static const float line_ratio[] = { 0.9, 0.1, 0.5 };
	colour fgcol;
	unsigned int i;
	css_color col;

	css_computed_color(box->style, &col);
	fgcol = nscss_color_to_ns(col);

	/* antialias colour for under/overline */
	if (html_redraw_printing == false)
		fgcol = blend_colour(background_colour, fgcol);

	if (box->type == BOX_INLINE) {
		if (!box->inline_end)
			return true;
		for (i = 0; i != NOF_ELEMENTS(decoration); i++)
			if (css_computed_text_decoration(box->style) &
					decoration[i])
				if (!html_redraw_text_decoration_inline(box,
						x_parent, y_parent, scale,
						fgcol, line_ratio[i], ctx))
					return false;
	} else {
		for (i = 0; i != NOF_ELEMENTS(decoration); i++)
			if (css_computed_text_decoration(box->style) &
					decoration[i])
				if (!html_redraw_text_decoration_block(box,
						x_parent + box->x,
						y_parent + box->y,
						scale,
						fgcol, line_ratio[i], ctx))
					return false;
	}

	return true;
}


/**
 * Redraw the text content of a box, possibly partially highlighted
 * because the text has been selected, or matches a search operation.
 *
 * \param html The html content to redraw text within.
 * \param  box      box with text content
 * \param  x        x co-ord of box
 * \param  y        y co-ord of box
 * \param  clip     current clip rectangle
 * \param  scale    current scale setting (1.0 = 100%)
 * \param  current_background_color
 * \param  ctx	    current redraw context
 * \return true iff successful and redraw should proceed
 */

static bool html_redraw_text_box(const html_content *html, struct box *box,
		int x, int y, const struct rect *clip, float scale,
		colour current_background_color,
		const struct redraw_context *ctx)
{
	bool excluded = (box->object != NULL);
	plot_font_style_t fstyle;

	font_plot_style_from_css(&html->unit_len_ctx, box->style, &fstyle);
	fstyle.background = current_background_color;

	/* Onyx: text-shadow (libcss computes one) -- the text drawn first in the shadow's
	 * colour at its offset; a blur is not drawn (a blurred shadow: its colour half way to
	 * the background; none when it sits right under the text -- a glow) */
	if (!excluded && box->style != NULL && box->length > 0) {
		css_fixed sx, sy, sb;
		css_unit sxu, syu, sbu;
		css_color sc;

		if (css_computed_text_shadow(box->style, &sx, &sxu, &sy, &syu,
				&sb, &sbu, &sc) == CSS_TEXT_SHADOW_SET &&
				(sc >> 24) != 0) {
			int dx = FIXTOINT(css_unit_len2device_px(box->style,
					&html->unit_len_ctx, sx, sxu));
			int dy = FIXTOINT(css_unit_len2device_px(box->style,
					&html->unit_len_ctx, sy, syu));
			int blur = FIXTOINT(css_unit_len2device_px(box->style,
					&html->unit_len_ctx, sb, sbu));
			unsigned a = sc >> 24;

			if (blur > 0)
				a /= 2;
			if ((dx != 0 || dy != 0 || blur == 0) && a > 0) {
				plot_font_style_t sh = fstyle;
				colour fg = nscss_color_to_ns(sc | 0xff000000u);
				colour bg = current_background_color;
				unsigned k;
				colour mix = 0;

				for (k = 0; k < 24; k += 8) {
					unsigned f = (fg >> k) & 0xff, b = (bg >> k) & 0xff;
					mix |= ((f * a + b * (255 - a)) / 255) << k;
				}
				sh.foreground = mix;
				sh.size *= scale;
				ctx->plot->text(ctx, &sh,
						x + (int) (dx * scale),
						y + (int) (dy * scale) +
						(int) (font_baseline(&fstyle, box->height) * scale),
						box->text, box->length);
			}
		}
	}

	if (!text_redraw(box->text,
			 box->length,
			 box->byte_offset,
			 box->space,
			 &fstyle,
			 x, y,
			 clip,
			 box->height,
			 scale,
			 excluded,
			 (struct content *)html,
			 html->sel,
			 ctx))
		return false;

	return true;
}

bool html_redraw_box(const html_content *html, struct box *box,
		int x_parent, int y_parent,
		const struct rect *clip, float scale,
		colour current_background_color,
		const struct redraw_context *ctx);


/* ---- Onyx: the painting order of positioned boxes (CSS 2.1 appendix E, simplified) -----
 * NetSurf painted the boxes in the tree's order: a menu positioned over the page (a
 * header's absolute panel, a sticky bar with a z-index) under what follows it. Now a
 * positioned box (and a flex / grid item with a z-index) is put off while its layer is
 * painted -- the page, or the positioned box it is in -- and painted after that layer's
 * other content, the layer's put-off boxes sorted by z-index (auto: 0), each with its own
 * layer inside it. A negative z-index is painted in place. A positioned box with z-index
 * auto is no stacking context (html_redraw_layer_context): the boxes above 0 it puts off
 * are sorted with its stacking context's (onyx_layer_paint_in, docs/06 §42).
 */

/** a box put off: painted after its layer's in-flow content */
struct onyx_layer_box {
	struct box *box;
	int x_parent, y_parent;
	struct rect clip;
	colour background;
	struct onyx_layer_key key;	/* its z-index (libcss' fixed point), its tree order */
};

/* Onyx: the redraw's own clip (the document's): a fixed box is not clipped by
 * its ancestors' overflow (its containing block is the viewport) */
static struct rect onyx_redraw_root_clip;
static bool onyx_redraw_root_clip_set;

static struct onyx_layer_box *onyx_layer_boxes;
static int onyx_layer_count, onyx_layer_cap;
/* the boxes with a z-index above 0 a positioned box with z-index auto put off: painted
 * by its stacking context, sorted with its own (onyx_layer_paint) */
static struct onyx_layer_box *onyx_hoist_boxes;
static int onyx_hoist_count, onyx_hoist_cap;
static bool onyx_layering;	/* a redraw that paints by layers is going on */

/* exported interface documented in html/private.h */
bool html_redraw_layer_z(const struct box *box, int32_t *z)
{
	int32_t zi = 0;
	uint8_t zt, pos;

	if (box->style == NULL)
		return false;
	switch (box->type) {
	case BOX_BLOCK: case BOX_INLINE_BLOCK: case BOX_TABLE:
	case BOX_FLEX: case BOX_INLINE_FLEX:
		break;
	default:
		return false;	/* (an inline's pieces are its line's) */
	}
	pos = css_computed_position(box->style);
	zt = css_computed_z_index(box->style, &zi);
	if (pos == CSS_POSITION_STATIC &&
	    (zt != CSS_Z_INDEX_SET || box->parent == NULL ||
	     (box->parent->type != BOX_FLEX &&
	      box->parent->type != BOX_INLINE_FLEX))) {
		/* Onyx: a group (opacity, transform, filter...: html/onyx_fx.c) is a
		 * stacking context, painted as a positioned box with z-index 0 is */
		if (!onyx_fx_style(box->style) || !onyx_fx_box(box))
			return false;
		zt = CSS_Z_INDEX_AUTO;
	}
	*z = zt == CSS_Z_INDEX_SET ? zi : 0;
	return *z >= 0;
}

/* exported interface documented in html/private.h */
bool html_redraw_layer_context(const struct box *box)
{
	int32_t zi;
	uint8_t pos;

	if (box->style == NULL)
		return true;
	if (css_computed_z_index(box->style, &zi) == CSS_Z_INDEX_SET)
		return true;
	pos = css_computed_position(box->style);
	if (pos == CSS_POSITION_FIXED || pos == CSS_POSITION_STICKY)
		return true;
	return onyx_fx_style(box->style) && onyx_fx_box(box);
}

/* exported interface documented in html/private.h */
int onyx_layer_key_cmp(const struct onyx_layer_key *a, const struct onyx_layer_key *b)
{
	if (a->z != b->z)
		return a->z < b->z ? -1 : 1;
	return a->lo < b->lo ? -1 : a->lo > b->lo ? 1 : 0;
}

/* Onyx: the boxes clipping their overflow being painted, each with the clip it was given:
 * an absolutely positioned box is clipped by the overflow of its containing block and of
 * its containing block's ancestors only, not by a static scroller between them (Codex's
 * menu: its footer, absolute in the menu, under the scrolling list -- it was cut away) */
#define ONYX_OCLIP_MAX 64
static struct {
	const struct box *box;
	struct rect outer;
} onyx_oclip[ONYX_OCLIP_MAX];
static int onyx_oclip_depth;

/** an absolute box's clip: the one outside the static clipping boxes between it and its
 * containing block (clip unchanged when there are none) */
static void onyx_oclip_escape(const struct box *c, struct rect *clip)
{
	const struct box *a;
	int i;

	if (onyx_oclip_depth == 0)
		return;
	for (a = c->parent; a != NULL; a = a->parent) {
		if (a->style != NULL &&
		    (css_computed_position(a->style) != CSS_POSITION_STATIC ||
		     (onyx_fx_style(a->style) && onyx_fx_box(a))))
			break;	/* (the containing block: its own overflow clips) */
		for (i = onyx_oclip_depth - 1; i >= 0; i--)
			if (onyx_oclip[i].box == a) {
				*clip = onyx_oclip[i].outer;
				break;
			}
	}
}

/** a child put off (true), or to paint now */
static bool onyx_layer_defer(struct box *c, int x_parent, int y_parent,
		const struct rect *clip, colour background)
{
	struct onyx_layer_box *e;
	int32_t z;

	if (!onyx_layering || !html_redraw_layer_z(c, &z))
		return false;
	if (onyx_layer_count == onyx_layer_cap) {
		int cap = onyx_layer_cap ? onyx_layer_cap * 2 : 64;
		e = realloc(onyx_layer_boxes, cap * sizeof(*e));
		if (e == NULL)
			return false;	/* (painted in place) */
		onyx_layer_boxes = e;
		onyx_layer_cap = cap;
	}
	e = &onyx_layer_boxes[onyx_layer_count];
	e->box = c;
	e->x_parent = x_parent;
	e->y_parent = y_parent;
	e->clip = *clip;
	if (onyx_redraw_root_clip_set && c->style != NULL &&
	    css_computed_position(c->style) == CSS_POSITION_FIXED)
		e->clip = onyx_redraw_root_clip;
	else if (c->style != NULL &&
		 css_computed_position(c->style) == CSS_POSITION_ABSOLUTE)
		onyx_oclip_escape(c, &e->clip);
	e->background = background;
	e->key.z = z;
	e->key.lo = onyx_layer_count;	/* (onyx_layer_paint gives the tree's order) */
	e->key.span = 1;
	onyx_layer_count++;
	return true;
}

static int onyx_layer_cmp(const void *a, const void *b)
{
	const struct onyx_layer_box *x = a, *y = b;

	return onyx_layer_key_cmp(&x->key, &y->key);
}

/* ---- Onyx: the painting phases of CSS 2.1 appendix E within a stacking context ----
 * NetSurf painted each block whole (its background, then its content) in the tree's
 * order: a later block's background covered an earlier block's text, and the floats
 * came after all the in-flow content. Now, within each atomically painted box (the
 * page, a positioned box or a stacking context, a float, an inline-block, a table, a
 * flex item...), the in-flow blocks' backgrounds and borders are painted first in the
 * tree's order, then the floats, then the inline content (the lines: text, images,
 * inline-blocks) -- the positioned boxes after that, as before (onyx_layer_*). Acid2's
 * eyes: a block's background under a float under an image in a line.
 */

/** a float or a line container put off to its phase */
struct onyx_phase_box {
	struct box *box;
	int x_parent, y_parent;
	struct rect clip;
	colour background;
	bool is_float;
};

static struct onyx_phase_box *onyx_phase_boxes;
static int onyx_phase_count, onyx_phase_cap;
static int onyx_phase_depth;	/* atomic boxes being painted (phases open) */

/** a block painted in its enclosing atomic box's phases (not atomic itself) */
static bool onyx_phase_transparent(const struct box *box)
{
	int32_t z;

	if (box->type != BOX_BLOCK || box->style == NULL || box->parent == NULL ||
	    box->object != NULL || box->gadget != NULL || (box->flags & IFRAME))
		return false;
	if (css_computed_position(box->style) != CSS_POSITION_STATIC)
		return false;
	if (box->parent->type == BOX_FLEX || box->parent->type == BOX_INLINE_FLEX)
		return false;	/* (a flex / grid item: as an inline-block) */
	if (html_redraw_layer_z(box, &z))
		return false;	/* (a stacking context) */
	return true;
}

/** a float or a line container put off to the end of the atomic box's phases */
static bool onyx_phase_defer(struct box *c, int x_parent, int y_parent,
		const struct rect *clip, float scale, colour background, bool is_float)
{
	struct onyx_phase_box *e;

	if (onyx_phase_depth == 0 || !onyx_layering)
		return false;
	if (onyx_phase_count == onyx_phase_cap) {
		int cap = onyx_phase_cap ? onyx_phase_cap * 2 : 64;

		e = realloc(onyx_phase_boxes, cap * sizeof(*e));
		if (e == NULL)
			return false;	/* (painted in place) */
		onyx_phase_boxes = e;
		onyx_phase_cap = cap;
	}
	e = &onyx_phase_boxes[onyx_phase_count++];
	e->box = c;
	e->x_parent = x_parent;
	e->y_parent = y_parent;
	e->clip = *clip;
	e->background = background;
	e->is_float = is_float;
	return true;
}

/** the floats, then the line containers, an atomic box put off (from start on) */
static bool onyx_phase_paint(const html_content *html, int start, float scale,
		const struct redraw_context *ctx)
{
	int end = onyx_phase_count, i, pass;
	bool ok = true;

	for (pass = 0; pass < 2 && ok; pass++) {
		for (i = start; i < end && ok; i++) {
			/* (a copy: the array grows with what this one puts off) */
			struct onyx_phase_box e = onyx_phase_boxes[i];

			if (e.is_float != (pass == 0))
				continue;
			ok = html_redraw_box(html, e.box, e.x_parent, e.y_parent,
					&e.clip, scale, e.background, ctx);
			onyx_phase_count = end;
		}
	}
	onyx_phase_count = start;
	return ok;
}

/** room for one more box put off (false: none) */
static bool onyx_layer_room(struct onyx_layer_box **a, int count, int *cap)
{
	struct onyx_layer_box *e;
	int n;

	if (count < *cap)
		return true;
	n = *cap ? *cap * 2 : 64;
	e = realloc(*a, n * sizeof(*e));
	if (e == NULL)
		return false;
	*a = e;
	*cap = n;
	return true;
}

/**
 * The boxes a layer put off (from start on), painted in their order. A stacking
 * context's (context true) are all painted here; a positioned box's with z-index auto
 * (context false: its own z-index is 0) paints those at 0 only, after it in the tree's
 * order, and gives those above 0 to its stacking context (onyx_hoist_*), which sorts
 * them with its own -- CSS 2.1 appendix E: they are its context's, not the box's.
 * lo / span: the interval of the tree's order the boxes are in (their layer's own).
 */
static bool onyx_layer_paint_in(const html_content *html, int start, bool context,
		double lo, double span, float scale, const struct redraw_context *ctx)
{
	int end = onyx_layer_count, n = end - start, h0 = onyx_hoist_count, i, k;
	bool ok = true;

	for (i = start; i < end; i++) {
		struct onyx_layer_key *key = &onyx_layer_boxes[i].key;

		key->lo = lo + span * (i - start + 1) / (n + 1);
		key->span = span / (n + 1);
	}
	if (!context) {
		for (i = k = start; i < end; i++) {
			if (onyx_layer_boxes[i].key.z > 0 &&
			    onyx_layer_room(&onyx_hoist_boxes, onyx_hoist_count,
					&onyx_hoist_cap))
				onyx_hoist_boxes[onyx_hoist_count++] = onyx_layer_boxes[i];
			else
				onyx_layer_boxes[k++] = onyx_layer_boxes[i];
		}
		end = onyx_layer_count = k;
		h0 = onyx_hoist_count;
	}
	if (end - start > 1)
		qsort(onyx_layer_boxes + start, end - start,
				sizeof(*onyx_layer_boxes), onyx_layer_cmp);
	for (i = start; i < end && ok; i++) {
		/* (a copy: the array grows with the boxes this one puts off) */
		struct onyx_layer_box e = onyx_layer_boxes[i];

		ok = html_redraw_box(html, e.box, e.x_parent, e.y_parent, &e.clip,
				scale, e.background, ctx);
		if (ok && onyx_layer_count > end)
			ok = onyx_layer_paint_in(html, end,
					html_redraw_layer_context(e.box),
					e.key.lo, e.key.span, scale, ctx);
		onyx_layer_count = end;
		/* a stacking context: the boxes its descendants gave it, sorted in
		 * with those still to paint */
		while (context && onyx_hoist_count > h0) {
			struct onyx_layer_box h = onyx_hoist_boxes[--onyx_hoist_count];

			if (!onyx_layer_room(&onyx_layer_boxes, end, &onyx_layer_cap))
				continue;	/* (dropped: out of memory) */
			for (k = end; k > i + 1 && onyx_layer_key_cmp(&h.key,
					&onyx_layer_boxes[k - 1].key) < 0; k--)
				onyx_layer_boxes[k] = onyx_layer_boxes[k - 1];
			onyx_layer_boxes[k] = h;
			onyx_layer_count = ++end;
		}
	}
	onyx_layer_count = start;
	return ok;
}

/** a stacking context's boxes put off (from start on), painted in their order */
static bool onyx_layer_paint(const html_content *html, int start, float scale,
		const struct redraw_context *ctx)
{
	return onyx_layer_paint_in(html, start, true, 0, 1, scale, ctx);
}

/**
 * Draw the various children of a box.
 *
 * \param  html	     html content
 * \param  box	     box to draw children of
 * \param  x_parent  coordinate of parent box
 * \param  y_parent  coordinate of parent box
 * \param  clip      clip rectangle
 * \param  scale     scale for redraw
 * \param  current_background_color  background colour under this box
 * \param  ctx	     current redraw context
 * \return true if successful, false otherwise
 */

/**
 * Onyx: whether a float is inside a positioned box (a layer) below its float container:
 * it is painted with that layer, in the tree's order, not with its container's floats
 * (painted early, Wikipedia's figures were covered by their layer's background).
 */
static bool onyx_float_layered(const struct box *flt, const struct box *container)
{
	const struct box *p;
	int32_t z;

	for (p = flt->parent; p != NULL && p != container; p = p->parent)
		if (html_redraw_layer_z(p, &z))
			return true;
	return false;
}

/**
 * Onyx: a float child's float container (the ancestor of `from` whose floats it is in)
 * and the offset of `from` in it; NULL when not found.
 */
static struct box *onyx_float_container(const struct box *flt, struct box *from,
		int *dx, int *dy)
{
	struct box *p, *q;

	*dx = *dy = 0;
	for (p = from; p != NULL; p = p->parent) {
		for (q = p->float_children; q != NULL; q = q->next_float)
			if (q == flt)
				return p;
		*dx += p->x;
		*dy += p->y;
	}
	return NULL;
}

/* ---- Onyx: compositing groups (opacity, transform, filter, backdrop-filter, blend mode) ----
 * A box with effects (html/onyx_fx.c) is painted with all it holds -- its stacking context:
 * its positioned descendants, put off, are painted inside it -- between the plotter's
 * onyx_layer_begin and onyx_layer_end (netsurf/onyx_paint.h): in place over a copy of what is
 * under it for an opacity alone (blended back), else into a layer of its own, twice (over
 * black, over white), then filtered and drawn through its matrix. The layer covers only what
 * lands in the redraw's clip (its inverse image through the matrix), grown by the filters'
 * reach. Opacity 0: nothing is painted (the hit test still finds the box).
 */

#define ONYX_FX_MAX_PIXELS (4096 * 2048)	/* (a larger layer: painted without effects) */

static int onyx_fx_nest;	/* isolated groups being painted (each paints twice) */

static bool html_redraw_box_inner(const html_content *html, struct box *box,
		int x_parent, int y_parent,
		const struct rect *clip, const float scale,
		colour current_background_color,
		const struct redraw_context *ctx);

/** a box painted with its stacking context: what it put off painted after it */
static bool onyx_fx_paint_context(const html_content *html, struct box *box,
		int x_parent, int y_parent, const struct rect *clip, float scale,
		colour background, const struct redraw_context *ctx)
{
	int start = onyx_layer_count;
	bool ok = html_redraw_box_inner(html, box, x_parent, y_parent, clip, scale,
			background, ctx);

	if (ok && onyx_layering && onyx_layer_count > start)
		ok = onyx_layer_paint(html, start, scale, ctx);
	onyx_layer_count = start;
	return ok;
}

/** what a box may paint (target px, its origin at x, y): its border box, its descendants
 * (unless it clips them), its shadow */
static void onyx_fx_bounds(const html_content *html, const struct box *box, float x,
		float y, float scale, float b[4])
{
	struct onyx_box_shadow sh;
	float x0 = -box->border[LEFT].width, y0 = -box->border[TOP].width;
	float x1 = box->padding[LEFT] + box->width + box->padding[RIGHT] +
			box->border[RIGHT].width;
	float y1 = box->padding[TOP] + box->height + box->padding[BOTTOM] +
			box->border[BOTTOM].width;
	float e = 2;

	if (css_computed_overflow_x(box->style) == CSS_OVERFLOW_VISIBLE) {
		if (box->descendant_x0 < x0) x0 = box->descendant_x0;
		if (box->descendant_x1 > x1) x1 = box->descendant_x1;
	}
	if (css_computed_overflow_y(box->style) == CSS_OVERFLOW_VISIBLE) {
		if (box->descendant_y0 < y0) y0 = box->descendant_y0;
		if (box->descendant_y1 > y1) y1 = box->descendant_y1;
	}
	if (onyx_box_shadow(box->style, &html->unit_len_ctx, 1, &sh) && !sh.inset)
		e += fabsf(sh.x) + fabsf(sh.y) + sh.blur * 1.5f +
				(sh.spread > 0 ? sh.spread : 0);
	b[0] = x + (x0 - e) * scale;
	b[1] = y + (y0 - e) * scale;
	b[2] = x + (x1 + e) * scale;
	b[3] = y + (y1 + e) * scale;
}

/** a rectangle's intersection with a clip (int), false when empty */
static bool onyx_fx_clip(const float r[4], const struct rect *clip, struct rect *out)
{
	out->x0 = (int) floorf(r[0]);
	out->y0 = (int) floorf(r[1]);
	out->x1 = (int) ceilf(r[2]);
	out->y1 = (int) ceilf(r[3]);
	if (out->x0 < clip->x0) out->x0 = clip->x0;
	if (out->y0 < clip->y0) out->y0 = clip->y0;
	if (out->x1 > clip->x1) out->x1 = clip->x1;
	if (out->y1 > clip->y1) out->y1 = clip->y1;
	return out->x0 < out->x1 && out->y0 < out->y1;
}

/**
 * Whether a box paints exactly its (rounded) border box, opaque: an opaque background under
 * its borders, nothing past it (no shadow, its descendants inside it or clipped), no mask --
 * a layer of it then needs one pass (its coverage is the box's).
 */
static bool onyx_fx_opaque(const html_content *html, const struct box *box)
{
	css_color bg = 0;
	struct onyx_box_shadow sh;
	int bw = box->padding[LEFT] + box->width + box->padding[RIGHT] +
			box->border[RIGHT].width;
	int bh = box->padding[TOP] + box->height + box->padding[BOTTOM] +
			box->border[BOTTOM].width;
	bool clip_x = css_computed_overflow_x(box->style) != CSS_OVERFLOW_VISIBLE;
	bool clip_y = css_computed_overflow_y(box->style) != CSS_OVERFLOW_VISIBLE;

	if (css_computed_background_color(box->style, &bg) != CSS_BACKGROUND_COLOR_COLOR ||
	    (bg >> 24) != 0xff ||
	    css_computed_background_clip(box->style) != CSS_BACKGROUND_CLIP_BORDER_BOX ||
	    onyx_mask_set(box) || box->type == BOX_TABLE ||
	    (onyx_box_shadow(box->style, &html->unit_len_ctx, 1, &sh) && !sh.inset))
		return false;
	if (!clip_x && (box->descendant_x0 < -box->border[LEFT].width ||
			box->descendant_x1 > bw))
		return false;
	if (!clip_y && (box->descendant_y0 < -box->border[TOP].width ||
			box->descendant_y1 > bh))
		return false;
	return true;
}

/** a box with effects painted as a group */
static bool onyx_fx_redraw(const html_content *html, struct box *box,
		int x_parent, int y_parent, const struct rect *clip, float scale,
		colour background, const struct redraw_context *ctx, struct onyx_fx *fx)
{
	struct onyx_layer *l;
	float x = (x_parent + box->x) * scale, y = (y_parent + box->y) * scale;
	float b[4], out[4];
	struct rect lr, lc, root_clip;
	bool isolated, ok = true;
	int pass, passes, s = 1, i;
	uint64_t t0;

	isolated = fx->matrix || fx->nfilter > 0 || fx->blend != ONYX_BLEND_NORMAL;
	if (isolated && (scale != 1.0f || onyx_fx_nest >= 3 || html_redraw_printing)) {
		/* (a scaled redraw -- a thumbnail --, groups too deep: those effects left) */
		isolated = false;
		fx->matrix = false;
		fx->nfilter = 0;
		fx->blend = ONYX_BLEND_NORMAL;
		if (fx->opacity >= 1 && fx->nbackdrop == 0)
			return onyx_fx_paint_context(html, box, x_parent, y_parent, clip,
					scale, background, ctx);
	}
	onyx_fx_bounds(html, box, x, y, scale, b);

	/* (the layer on the heap: the plotter keeps a pointer to it while it is painted) */
	l = calloc(1, sizeof(*l));
	if (l == NULL)
		return onyx_fx_paint_context(html, box, x_parent, y_parent, clip, scale,
				background, ctx);
	l->opacity = fx->opacity;
	l->blend = fx->blend;
	l->cx0 = clip->x0;
	l->cy0 = clip->y0;
	l->cx1 = clip->x1;
	l->cy1 = clip->y1;
	l->m[0] = l->m[3] = 1;
	if (fx->nbackdrop > 0) {
		struct onyx_rrect *r = &l->backdrop_box;
		r->x0 = x - box->border[LEFT].width * scale;
		r->y0 = y - box->border[TOP].width * scale;
		r->x1 = x + (box->padding[LEFT] + box->width + box->padding[RIGHT] +
				box->border[RIGHT].width) * scale;
		r->y1 = y + (box->padding[TOP] + box->height + box->padding[BOTTOM] +
				box->border[BOTTOM].width) * scale;
		onyx_box_radii(box->style, &html->unit_len_ctx, scale, r);
		l->nbackdrop = fx->nbackdrop;
		memcpy(l->backdrop, fx->backdrop, sizeof(fx->backdrop));
	}

	if (!isolated) {
		/* in place: what is under the group kept, blended back at its opacity */
		if (!onyx_fx_clip(b, clip, &lr)) {
			free(l);
			return true;
		}
		l->x0 = lr.x0;
		l->y0 = lr.y0;
		l->x1 = lr.x1;
		l->y1 = lr.y1;
		t0 = onyx_perf_now();
		if (ctx->plot->onyx_layer_begin(ctx, l, 0) != NSERROR_OK) {
			free(l);
			return onyx_fx_paint_context(html, box, x_parent, y_parent, clip,
					scale, background, ctx);
		}
		ok = onyx_fx_paint_context(html, box, x_parent, y_parent, clip, scale,
				background, ctx);
		ctx->plot->onyx_layer_end(ctx, l, 0);
		if (t0 != 0) {
			char what[80];
			snprintf(what, sizeof(what), "layer %dx%d in place%s",
					lr.x1 - lr.x0, lr.y1 - lr.y0,
					l->nbackdrop > 0 ? " backdrop" : "");
			onyx_perf_log(what, t0);
		}
		free(l);
		return ctx->plot->clip(ctx, clip) == NSERROR_OK && ok;
	}

	/* apart: the filters' reach, the matrix */
	onyx_filter_outset(fx->filter, fx->nfilter, out);
	b[0] -= out[0];
	b[1] -= out[1];
	b[2] += out[2];
	b[3] += out[3];
	if (fx->matrix) {
		float d[4], need[4], inv[6];
		memcpy(l->m, fx->m, sizeof(l->m));
		l->m[4] += x - (l->m[0] * x + l->m[2] * y);
		l->m[5] += y - (l->m[1] * x + l->m[3] * y);
		l->transformed = true;
		onyx_matrix_bbox(l->m, b[0], b[1], b[2], b[3], &d[0], &d[1], &d[2], &d[3]);
		if (!onyx_fx_clip(d, clip, &lr) || !onyx_matrix_invert(l->m, inv)) {
			free(l);
			return true;
		}
		/* the layer's part that lands in the clip (and what its filters read) */
		onyx_matrix_bbox(inv, clip->x0, clip->y0, clip->x1, clip->y1,
				&need[0], &need[1], &need[2], &need[3]);
		need[0] -= 2 + out[0] + out[2];
		need[1] -= 2 + out[1] + out[3];
		need[2] += 2 + out[0] + out[2];
		need[3] += 2 + out[1] + out[3];
		if (need[0] > b[0]) b[0] = need[0];
		if (need[1] > b[1]) b[1] = need[1];
		if (need[2] < b[2]) b[2] = need[2];
		if (need[3] < b[3]) b[3] = need[3];
		lr.x0 = (int) floorf(b[0]);
		lr.y0 = (int) floorf(b[1]);
		lr.x1 = (int) ceilf(b[2]);
		lr.y1 = (int) ceilf(b[3]);
	} else {
		struct rect grown = *clip;
		grown.x0 -= (int) (out[0] + out[2]) + 1;
		grown.y0 -= (int) (out[1] + out[3]) + 1;
		grown.x1 += (int) (out[0] + out[2]) + 1;
		grown.y1 += (int) (out[1] + out[3]) + 1;
		if (!onyx_fx_clip(b, &grown, &lr)) {
			free(l);
			return true;
		}
	}
	if (lr.x1 <= lr.x0 || lr.y1 <= lr.y0) {
		free(l);
		return true;
	}
	if ((double) (lr.x1 - lr.x0) * (lr.y1 - lr.y0) > ONYX_FX_MAX_PIXELS) {
		free(l);
		return onyx_fx_paint_context(html, box, x_parent, y_parent, clip, scale,
				background, ctx);
	}
	l->isolated = true;
	l->nfilter = fx->nfilter;
	memcpy(l->filter, fx->filter, sizeof(fx->filter));

	/* a large blur: the layer painted at 1 / s of the size (what the blur leaves
	 * is smooth), its filters scaled, drawn back through the matrix */
	for (i = 0; i < fx->nfilter; i++)
		if (fx->filter[i].op == ONYX_FILTER_BLUR)
			while (fx->filter[i].v / (s * 2) >= 4 && s < 8)
				s *= 2;
	if (s > 1) {
		float sm[6] = { s, 0, 0, s, 0, 0 };
		for (i = 0; i < l->nfilter; i++) {
			l->filter[i].v /= s;
			l->filter[i].dx /= s;
			l->filter[i].dy /= s;
		}
		lr.x0 = (int) floorf((float) lr.x0 / s);
		lr.y0 = (int) floorf((float) lr.y0 / s);
		lr.x1 = (int) ceilf((float) lr.x1 / s);
		lr.y1 = (int) ceilf((float) lr.y1 / s);
		onyx_matrix_mul(l->m, sm, l->m);
		l->transformed = true;
	}
	l->x0 = lr.x0;
	l->y0 = lr.y0;
	l->x1 = lr.x1;
	l->y1 = lr.y1;

	/* known opaque in its border box: one pass */
	if (s == 1 && onyx_fx_opaque(html, box)) {
		struct onyx_rrect *r = &l->opaque;
		r->x0 = x - box->border[LEFT].width - lr.x0;
		r->y0 = y - box->border[TOP].width - lr.y0;
		r->x1 = x + box->padding[LEFT] + box->width + box->padding[RIGHT] +
				box->border[RIGHT].width - lr.x0;
		r->y1 = y + box->padding[TOP] + box->height + box->padding[BOTTOM] +
				box->border[BOTTOM].width - lr.y0;
		onyx_box_radii(box->style, &html->unit_len_ctx, 1, r);
		l->single = true;
	}
	passes = l->single ? 1 : 2;

	lc.x0 = lc.y0 = 0;
	lc.x1 = lr.x1 - lr.x0;
	lc.y1 = lr.y1 - lr.y0;
	t0 = onyx_perf_now();
	onyx_fx_nest++;
	for (pass = 0; pass < passes && ok; pass++) {
		if (ctx->plot->onyx_layer_begin(ctx, l, pass) != NSERROR_OK) {
			if (pass == 0)
				ok = onyx_fx_paint_context(html, box, x_parent, y_parent,
						clip, scale, background, ctx);
			break;
		}
		/* (a fixed descendant's containing block is the transformed or
		 * filtered box, as in CSS: clipped to the layer, not the viewport) */
		root_clip = onyx_redraw_root_clip;
		onyx_redraw_root_clip = lc;
		ok = ctx->plot->clip(ctx, &lc) == NSERROR_OK &&
				onyx_fx_paint_context(html, box, x_parent - lr.x0 * s,
				y_parent - lr.y0 * s, &lc, scale / s, background, ctx);
		onyx_redraw_root_clip = root_clip;
		ctx->plot->onyx_layer_end(ctx, l, pass);
	}
	onyx_fx_nest--;
	if (t0 != 0) {
		char what[80];
		snprintf(what, sizeof(what), "layer %dx%d%s%s%s%s", lc.x1, lc.y1,
				l->transformed ? " transform" : "",
				l->nfilter > 0 ? " filter" : "",
				l->nbackdrop > 0 ? " backdrop" : "",
				l->single ? " (one pass)" : "");
		onyx_perf_log(what, t0);
	}
	free(l);
	return ctx->plot->clip(ctx, clip) == NSERROR_OK && ok;
}

/**
 * Onyx -- GPU compositing (docs/06 §25): a group the frontend can composite itself (an opacity
 * and / or a transform, nothing else) offered as a retained layer (netsurf/onyx_paint.h,
 * ONYX_LAYER_OFFER): accepted, its pixels are kept by the plotter between redraws and
 * composited over what is painted under it; only the part of them it asks for is painted,
 * apart (over black and white, as an isolated group's), and nothing is painted in place.
 * -1: not offered, or refused (the group is then painted as ever); else whether it went well.
 */
static int onyx_fx_retain(const html_content *html, struct box *box,
		int x_parent, int y_parent, const struct rect *clip, float scale,
		colour background, const struct redraw_context *ctx, struct onyx_fx *fx)
{
	struct onyx_layer *l, *p;
	float x = (x_parent + box->x) * scale, y = (y_parent + box->y) * scale;
	float b[4];
	struct rect lc, root_clip;
	bool ok = true;
	int pass;
	uint64_t t0;

	if (onyx_layer_props == NULL)
		return -1;	/* (no compositing frontend: painted as ever) */
	if (fx->nfilter > 0 || fx->nbackdrop > 0 || fx->blend != ONYX_BLEND_NORMAL ||
	    scale != 1.0f || onyx_fx_nest > 0 || html_redraw_printing || !ctx->interactive)
		return -1;
	onyx_fx_bounds(html, box, x, y, scale, b);
	{
		/* (not where this redraw paints: nothing to do) */
		float d[4];
		struct rect cr;
		if (fx->matrix) {
			float m[6];
			memcpy(m, fx->m, sizeof(m));
			m[4] += x - (m[0] * x + m[2] * y);
			m[5] += y - (m[1] * x + m[3] * y);
			onyx_matrix_bbox(m, b[0], b[1], b[2], b[3], &d[0], &d[1],
					&d[2], &d[3]);
		} else {
			memcpy(d, b, sizeof(d));
		}
		if (!onyx_fx_clip(d, clip, &cr))
			return 1;
	}
	l = calloc(2, sizeof(*l));
	if (l == NULL)
		return -1;
	p = l + 1;
	l->x0 = (int) floorf(b[0]);
	l->y0 = (int) floorf(b[1]);
	l->x1 = (int) ceilf(b[2]);
	l->y1 = (int) ceilf(b[3]);
	l->cx0 = clip->x0;
	l->cy0 = clip->y0;
	l->cx1 = clip->x1;
	l->cy1 = clip->y1;
	l->opacity = fx->opacity;
	l->m[0] = l->m[3] = 1;
	l->lm[0] = l->lm[3] = 1;
	if (fx->matrix) {
		memcpy(l->lm, fx->m, sizeof(l->lm));
		memcpy(l->m, fx->m, sizeof(l->m));
		l->m[4] += x - (l->m[0] * x + l->m[2] * y);
		l->m[5] += y - (l->m[1] * x + l->m[3] * y);
		l->transformed = true;
	}
	l->ox = x;
	l->oy = y;
	l->retain = true;
	l->key = box;
	l->tree = html->layout;
	if (ctx->plot->onyx_layer_begin(ctx, l, ONYX_LAYER_OFFER) != NSERROR_OK) {
		free(l);
		return -1;
	}

	/* the part of its pixels asked for, painted as an isolated group's passes */
	if (l->rx1 > l->rx0 && l->ry1 > l->ry0) {
		*p = *l;
		p->x0 = l->rx0;
		p->y0 = l->ry0;
		p->x1 = l->rx1;
		p->y1 = l->ry1;
		p->isolated = true;
		p->transformed = false;
		p->opacity = 1;
		p->m[0] = p->m[3] = 1;
		p->m[1] = p->m[2] = p->m[4] = p->m[5] = 0;
		if (onyx_fx_opaque(html, box)) {
			/* known opaque in its border box: one pass */
			struct onyx_rrect *r = &p->opaque;
			r->x0 = x - box->border[LEFT].width - p->x0;
			r->y0 = y - box->border[TOP].width - p->y0;
			r->x1 = x + box->padding[LEFT] + box->width + box->padding[RIGHT] +
					box->border[RIGHT].width - p->x0;
			r->y1 = y + box->padding[TOP] + box->height + box->padding[BOTTOM] +
					box->border[BOTTOM].width - p->y0;
			onyx_box_radii(box->style, &html->unit_len_ctx, 1, r);
			p->single = true;
		}
		lc.x0 = lc.y0 = 0;
		lc.x1 = p->x1 - p->x0;
		lc.y1 = p->y1 - p->y0;
		t0 = onyx_perf_now();
		onyx_fx_nest++;
		for (pass = 0; pass < (p->single ? 1 : 2) && ok; pass++) {
			if (ctx->plot->onyx_layer_begin(ctx, p, pass) != NSERROR_OK) {
				ok = false;
				break;
			}
			root_clip = onyx_redraw_root_clip;
			onyx_redraw_root_clip = lc;
			ok = ctx->plot->clip(ctx, &lc) == NSERROR_OK &&
					onyx_fx_paint_context(html, box, x_parent - p->x0,
					y_parent - p->y0, &lc, scale, background, ctx);
			onyx_redraw_root_clip = root_clip;
			ctx->plot->onyx_layer_end(ctx, p, pass);
		}
		onyx_fx_nest--;
		if (t0 != 0) {
			char what[80];
			snprintf(what, sizeof(what), "layer %dx%d retained%s", lc.x1, lc.y1,
					p->single ? " (one pass)" : "");
			onyx_perf_log(what, t0);
		}
	}
	ctx->plot->onyx_layer_end(ctx, l, ONYX_LAYER_OFFER);
	free(l);
	return ctx->plot->clip(ctx, clip) == NSERROR_OK && ok;
}

/* exported interface documented in netsurf/onyx_paint.h */
bool (*onyx_layer_props)(const void *key, const float *lm, float opacity);

/* exported interface documented in html/private.h */
bool html_redraw_layer_update(const html_content *html, struct box *box)
{
	struct onyx_fx fx;

	if (onyx_layer_props == NULL || !onyx_box_fx(html, box, 1, &fx) ||
	    fx.nfilter > 0 || fx.nbackdrop > 0 || fx.blend != ONYX_BLEND_NORMAL ||
	    fx.opacity <= 0)
		return false;
	return onyx_layer_props(box, fx.matrix ? fx.m : NULL, fx.opacity);
}

/** an inline's opacity (its pieces are its line's siblings: grouped from it to its end) */
static bool onyx_inline_opacity(const struct box *c, float *a)
{
	css_fixed o = INTTOFIX(1);

	if (c->type != BOX_INLINE || c->object != NULL || c->inline_end == NULL ||
	    c->style == NULL || (c->flags & STYLE_OWNED) ||
	    css_computed_opacity(c->style, &o) != CSS_OPACITY_SET || o >= INTTOFIX(1))
		return false;
	*a = o <= 0 ? 0 : FIXTOFLT(o);
	return true;
}

static bool html_redraw_children_range(const html_content *html, struct box *box,
		struct box *first, struct box *last, int x_parent, int y_parent,
		const struct rect *clip, float scale, colour current_background_color,
		const struct redraw_context *ctx);

/** an inline with an opacity: its pieces (from it to its end) painted as a group */
static bool onyx_inline_group(const html_content *html, struct box *box, struct box *c,
		float a, int x_parent, int y_parent, const struct rect *clip, float scale,
		colour background, const struct redraw_context *ctx)
{
	struct onyx_layer *l;
	struct box *d;
	float b[4] = { INFINITY, INFINITY, -INFINITY, -INFINITY };
	int ox = x_parent + box->x - scrollbar_get_offset(box->scroll_x);
	int oy = y_parent + box->y - scrollbar_get_offset(box->scroll_y);
	struct rect lr;
	bool ok;

	if (a <= 0)
		return true;
	for (d = c; d != NULL; d = d->next) {
		float x0 = d->x - d->border[LEFT].width, y0 = d->y - d->border[TOP].width;
		float x1 = d->x + d->padding[LEFT] + d->width + d->padding[RIGHT] +
				d->border[RIGHT].width;
		float y1 = d->y + d->padding[TOP] + d->height + d->padding[BOTTOM] +
				d->border[BOTTOM].width;
		if (d->descendant_x0 + d->x < x0) x0 = d->descendant_x0 + d->x;
		if (d->descendant_y0 + d->y < y0) y0 = d->descendant_y0 + d->y;
		if (d->descendant_x1 + d->x > x1) x1 = d->descendant_x1 + d->x;
		if (d->descendant_y1 + d->y > y1) y1 = d->descendant_y1 + d->y;
		if (x0 < b[0]) b[0] = x0;
		if (y0 < b[1]) b[1] = y0;
		if (x1 > b[2]) b[2] = x1;
		if (y1 > b[3]) b[3] = y1;
		if (d == c->inline_end)
			break;
	}
	b[0] = (ox + b[0] - 4) * scale;
	b[1] = (oy + b[1] - 4) * scale;
	b[2] = (ox + b[2] + 4) * scale;
	b[3] = (oy + b[3] + 4) * scale;
	if (!onyx_fx_clip(b, clip, &lr))
		return true;
	l = calloc(1, sizeof(*l));
	if (l == NULL)
		return html_redraw_children_range(html, box, c, c->inline_end, x_parent,
				y_parent, clip, scale, background, ctx);
	l->x0 = lr.x0;
	l->y0 = lr.y0;
	l->x1 = lr.x1;
	l->y1 = lr.y1;
	l->cx0 = clip->x0;
	l->cy0 = clip->y0;
	l->cx1 = clip->x1;
	l->cy1 = clip->y1;
	l->opacity = a;
	l->m[0] = l->m[3] = 1;
	if (ctx->plot->onyx_layer_begin(ctx, l, 0) != NSERROR_OK) {
		free(l);
		return html_redraw_children_range(html, box, c, c->inline_end, x_parent,
				y_parent, clip, scale, background, ctx);
	}
	/* (the inline's own box painted plainly, then its pieces) */
	ok = html_redraw_box_inner(html, c, ox, oy, clip, scale, background, ctx) &&
			(c == c->inline_end || c->next == NULL ||
			 html_redraw_children_range(html, box, c->next, c->inline_end,
					x_parent, y_parent, clip, scale, background, ctx));
	ctx->plot->onyx_layer_end(ctx, l, 0);
	free(l);
	return ctx->plot->clip(ctx, clip) == NSERROR_OK && ok;
}

/** a box's children from first to last (NULL: to the end) */
static bool html_redraw_children_range(const html_content *html, struct box *box,
		struct box *first, struct box *last, int x_parent, int y_parent,
		const struct rect *clip, float scale, colour current_background_color,
		const struct redraw_context *ctx)
{
	struct box *c;

	for (c = first; c; c = c->next) {
		float a;

		if (c->type == BOX_FLOAT_LEFT || c->type == BOX_FLOAT_RIGHT) {
			/* Onyx: a float in a layer its container is outside of:
			 * painted here, with its layer */
			int dx, dy;
			struct box *fc = onyx_float_container(c, box, &dx, &dy);

			if (fc != NULL && fc != box && onyx_float_layered(c, fc) &&
			    !onyx_phase_defer(c, x_parent + box->x - dx,
					y_parent + box->y - dy, clip, scale,
					current_background_color, true) &&
			    !html_redraw_box(html, c, x_parent + box->x - dx,
					y_parent + box->y - dy, clip, scale,
					current_background_color, ctx))
				return false;
		} else if (ctx->plot->onyx_layer_begin != NULL &&
				onyx_inline_opacity(c, &a)) {
			/* Onyx: an inline with an opacity, its pieces a group */
			struct box *end = c->inline_end;
			bool reached = false;
			struct box *d;

			if (!onyx_inline_group(html, box, c, a, x_parent, y_parent,
					clip, scale, current_background_color, ctx))
				return false;
			for (d = c; last != NULL && d != NULL; d = d->next) {
				if (d == last)
					reached = true;
				if (d == end)
					break;
			}
			if (reached)
				break;
			c = end;
		} else if (c->type == BOX_INLINE_CONTAINER &&
				onyx_phase_defer(c, x_parent + box->x -
					scrollbar_get_offset(box->scroll_x),
					y_parent + box->y -
					scrollbar_get_offset(box->scroll_y),
					clip, scale, current_background_color,
					false)) {
			/* (Onyx: a line container: painted after the blocks'
			 * backgrounds and the floats of its atomic box) */
		} else if (!onyx_layer_defer(c, x_parent + box->x -
					scrollbar_get_offset(box->scroll_x),
					y_parent + box->y -
					scrollbar_get_offset(box->scroll_y),
					clip, current_background_color)) {
			/* (Onyx: a positioned child is put off: painted after its
			 * layer's content) */
			if (!html_redraw_box(html, c,
					x_parent + box->x -
					scrollbar_get_offset(box->scroll_x),
					y_parent + box->y -
					scrollbar_get_offset(box->scroll_y),
					clip, scale, current_background_color,
					ctx))
				return false;
		}
		if (c == last)
			break;
	}
	return true;
}

static bool html_redraw_box_children(const html_content *html, struct box *box,
		int x_parent, int y_parent,
		const struct rect *clip, float scale,
		colour current_background_color,
		const struct redraw_context *ctx)
{
	struct box *c;

	if (!html_redraw_children_range(html, box, box->children, NULL, x_parent,
			y_parent, clip, scale, current_background_color, ctx))
		return false;
	for (c = box->float_children; c; c = c->next_float) {
		int fx = x_parent + box->x - scrollbar_get_offset(box->scroll_x);
		int fy = y_parent + box->y - scrollbar_get_offset(box->scroll_y);

		if (onyx_float_layered(c, box))	/* (Onyx) */
			continue;
		/* (Onyx: after the blocks' backgrounds, before the lines) */
		if (onyx_phase_defer(c, fx, fy, clip, scale,
				current_background_color, true))
			continue;
		if (!html_redraw_box(html, c, fx, fy, clip, scale,
				current_background_color, ctx))
			return false;
	}

	return true;
}

/**
 * Recursively draw a box.
 *
 * \param  html	     html content
 * \param  box	     box to draw
 * \param  x_parent  coordinate of parent box
 * \param  y_parent  coordinate of parent box
 * \param  clip      clip rectangle
 * \param  scale     scale for redraw
 * \param  current_background_color  background colour under this box
 * \param  ctx	     current redraw context
 * \return true if successful, false otherwise
 *
 * x, y, clip_[xy][01] are in target coordinates.
 */

bool html_redraw_box(const html_content *html, struct box *box,
		int x_parent, int y_parent,
		const struct rect *clip, const float scale,
		colour current_background_color,
		const struct redraw_context *ctx)
{
	/* Onyx: a background-clip: text paint lasts for the box's descendants */
	const struct onyx_paint *fill = onyx_text_fill;
	struct onyx_fx fx;
	bool ok;

	/* Onyx: a fixed box is where the viewport is (laid out at its scroll 0) */
	if (box->style != NULL &&
	    css_computed_position(box->style) == CSS_POSITION_FIXED &&
	    (onyx_view_sx != 0 || onyx_view_sy != 0) &&
	    html_box_viewport_fixed(box)) {
		x_parent += onyx_view_sx;
		y_parent += onyx_view_sy;
	}

	/* Onyx: a box with effects painted as a group (with its stacking context) */
	if (onyx_box_fx(html, box, scale, &fx)) {
		if (fx.opacity <= 0)
			ok = true;	/* (opacity 0: nothing painted) */
		else if (ctx->plot->onyx_layer_begin != NULL) {
			/* (Onyx: a retained layer when the plotter composites) */
			int r = onyx_fx_retain(html, box, x_parent, y_parent, clip,
					scale, current_background_color, ctx, &fx);
			ok = r >= 0 ? r != 0 : onyx_fx_redraw(html, box, x_parent,
					y_parent, clip, scale,
					current_background_color, ctx, &fx);
		}
		else
			ok = onyx_fx_paint_context(html, box, x_parent, y_parent,
					clip, scale, current_background_color, ctx);
	} else {
		ok = html_redraw_box_inner(html, box, x_parent, y_parent, clip,
				scale, current_background_color, ctx);
	}

	onyx_text_fill = fill;
	return ok;
}

static bool html_redraw_box_body(const html_content *html, struct box *box,
		int x_parent, int y_parent,
		const struct rect *clip, const float scale,
		colour current_background_color,
		const struct redraw_context *ctx);

/* Onyx: a box; an atomic one (not a plain in-flow block) with its phases (above) */
static bool html_redraw_box_inner(const html_content *html, struct box *box,
		int x_parent, int y_parent,
		const struct rect *clip, const float scale,
		colour current_background_color,
		const struct redraw_context *ctx)
{
	int start, oclip = onyx_oclip_depth;	/* (the body may push its own) */
	bool ok;

	if (!onyx_layering || html_redraw_printing ||
	    (onyx_phase_depth > 0 && onyx_phase_transparent(box))) {
		ok = html_redraw_box_body(html, box, x_parent, y_parent, clip,
				scale, current_background_color, ctx);
		onyx_oclip_depth = oclip;
		return ok;
	}
	start = onyx_phase_count;
	onyx_phase_depth++;
	ok = html_redraw_box_body(html, box, x_parent, y_parent, clip, scale,
			current_background_color, ctx);
	if (ok && onyx_phase_count > start) {
		ok = onyx_phase_paint(html, start, scale, ctx);
		/* (the clip as the box was given it; an empty one -- a box holding
		 * a fixed box, out of the redraw -- is refused, harmlessly) */
		ctx->plot->clip(ctx, clip);
	}
	onyx_phase_count = start;
	onyx_phase_depth--;
	onyx_oclip_depth = oclip;
	return ok;
}

static bool html_redraw_box_body(const html_content *html, struct box *box,
		int x_parent, int y_parent,
		const struct rect *clip, const float scale,
		colour current_background_color,
		const struct redraw_context *ctx)
{
	const struct plotter_table *plot = ctx->plot;
	struct onyx_gradient tf_grad;	/* (Onyx: background-clip: text) */
	struct onyx_paint tf_paint;
	int x, y;
	int width, height;
	int padding_left, padding_top, padding_width, padding_height;
	int border_left, border_top, border_right, border_bottom;
	struct rect r;
	struct rect rect;
	int x_scrolled, y_scrolled;
	struct box *bg_box = NULL;
	css_computed_clip_rect css_rect;
	enum css_overflow_e overflow_x = CSS_OVERFLOW_VISIBLE;
	enum css_overflow_e overflow_y = CSS_OVERFLOW_VISIBLE;
	dom_exception exc;
	dom_html_element_type tag_type;
	/* Onyx: CSS3 painting -- the border box, rounded; the box-shadow */
	struct onyx_rrect orr;
	bool rounded = false, has_shadow = false, round_clipped = false;
	bool masked = false;	/* Onyx: a mask-image (html/onyx_mask.c) */
	struct onyx_box_shadow shadow;


	if (html_redraw_printing && (box->flags & PRINTED))
		return true;

	if (box->style != NULL) {
		overflow_x = css_computed_overflow_x(box->style);
		overflow_y = css_computed_overflow_y(box->style);
	}

	/* avoid trivial FP maths */
	if (scale == 1.0) {
		x = x_parent + box->x;
		y = y_parent + box->y;
		width = box->width;
		height = box->height;
		padding_left = box->padding[LEFT];
		padding_top = box->padding[TOP];
		padding_width = padding_left + box->width + box->padding[RIGHT];
		padding_height = padding_top + box->height +
				box->padding[BOTTOM];
		border_left = box->border[LEFT].width;
		border_top = box->border[TOP].width;
		border_right = box->border[RIGHT].width;
		border_bottom = box->border[BOTTOM].width;
	} else {
		x = (x_parent + box->x) * scale;
		y = (y_parent + box->y) * scale;
		width = box->width * scale;
		height = box->height * scale;
		/* left and top padding values are normally zero,
		 * so avoid trivial FP maths */
		padding_left = box->padding[LEFT] ? box->padding[LEFT] * scale
				: 0;
		padding_top = box->padding[TOP] ? box->padding[TOP] * scale
				: 0;
		padding_width = (box->padding[LEFT] + box->width +
				box->padding[RIGHT]) * scale;
		padding_height = (box->padding[TOP] + box->height +
				box->padding[BOTTOM]) * scale;
		border_left = box->border[LEFT].width * scale;
		border_top = box->border[TOP].width * scale;
		border_right = box->border[RIGHT].width * scale;
		border_bottom = box->border[BOTTOM].width * scale;
	}

	/* calculate rectangle covering this box and descendants */
	if (box->style && overflow_x != CSS_OVERFLOW_VISIBLE &&
			box->parent != NULL) {
		/* box contents clipped to box size */
		r.x0 = x - border_left;
		r.x1 = x + padding_width + border_right;
	} else {
		/* box contents can hang out of the box; use descendant box */
		if (scale == 1.0) {
			r.x0 = x + box->descendant_x0;
			r.x1 = x + box->descendant_x1 + 1;
		} else {
			r.x0 = x + box->descendant_x0 * scale;
			r.x1 = x + box->descendant_x1 * scale + 1;
		}
		if (!box->parent) {
			/* root element */
			int margin_left, margin_right;
			if (scale == 1.0) {
				margin_left = box->margin[LEFT];
				margin_right = box->margin[RIGHT];
			} else {
				margin_left = box->margin[LEFT] * scale;
				margin_right = box->margin[RIGHT] * scale;
			}
			r.x0 = x - border_left - margin_left < r.x0 ?
					x - border_left - margin_left : r.x0;
			r.x1 = x + padding_width + border_right +
					margin_right > r.x1 ?
					x + padding_width + border_right +
					margin_right : r.x1;
		}
	}

	/* calculate rectangle covering this box and descendants */
	if (box->style && overflow_y != CSS_OVERFLOW_VISIBLE &&
			box->parent != NULL) {
		/* box contents clipped to box size */
		r.y0 = y - border_top;
		r.y1 = y + padding_height + border_bottom;
	} else {
		/* box contents can hang out of the box; use descendant box */
		if (scale == 1.0) {
			r.y0 = y + box->descendant_y0;
			r.y1 = y + box->descendant_y1 + 1;
		} else {
			r.y0 = y + box->descendant_y0 * scale;
			r.y1 = y + box->descendant_y1 * scale + 1;
		}
		if (!box->parent) {
			/* root element */
			int margin_top, margin_bottom;
			if (scale == 1.0) {
				margin_top = box->margin[TOP];
				margin_bottom = box->margin[BOTTOM];
			} else {
				margin_top = box->margin[TOP] * scale;
				margin_bottom = box->margin[BOTTOM] * scale;
			}
			r.y0 = y - border_top - margin_top < r.y0 ?
					y - border_top - margin_top : r.y0;
			r.y1 = y + padding_height + border_bottom +
					margin_bottom > r.y1 ?
					y + padding_height + border_bottom +
					margin_bottom : r.y1;
		}
	}

	/* Onyx: a table's captions are outside its border box (layout_table):
	 * its shadow, background and borders around its grid only */
	if (box->type == BOX_TABLE && box->children != NULL &&
	    ((box->children->flags & TABLE_CAPTION) ||
	     (box->last->flags & TABLE_CAPTION))) {
		int top = 0, bottom = 0;
		struct box *c;

		for (c = box->children; c != NULL && (c->flags & TABLE_CAPTION);
				c = c->next)
			top = c->y + c->padding[TOP] + c->height +
					c->padding[BOTTOM] +
					c->border[BOTTOM].width +
					c->margin[BOTTOM] + box->border[TOP].width;
		for (c = box->last; c != NULL && (c->flags & TABLE_CAPTION);
				c = c->prev)
			bottom = box->padding[TOP] + box->height +
					box->padding[BOTTOM] +
					box->border[BOTTOM].width -
					(c->y - c->border[TOP].width -
					 c->margin[TOP]);
		y += top * scale;
		padding_height -= (top + bottom) * scale;
	}

	/* Onyx: the border box, its corners' radii, its shadow (block-level boxes and
	 * replaced ones: not an inline's pieces) */
	if (box->style != NULL && ctx->plot->onyx_shape != NULL &&
	    box->type != BOX_TEXT && box->type != BOX_INLINE_END &&
	    box->type != BOX_BR &&
	    (box->type != BOX_INLINE || box->object)) {
		orr.x0 = x - border_left;
		orr.y0 = y - border_top;
		orr.x1 = x + padding_width + border_right;
		orr.y1 = y + padding_height + border_bottom;
		rounded = onyx_box_radii(box->style, &html->unit_len_ctx, scale, &orr);
		has_shadow = onyx_box_shadow(box->style, &html->unit_len_ctx, scale,
				&shadow) && !shadow.inset;
	}

	/* return if the rectangle is completely outside the clip rectangle --
	 * Onyx: its shadow included */
	{
		struct rect rc = r;
		if (has_shadow) {
			int e = (int) ceilf(fabsf(shadow.x) + fabsf(shadow.y) +
					shadow.blur * 1.5f +
					(shadow.spread > 0 ? shadow.spread : 0) + 2);
			rc.x0 -= e;
			rc.y0 -= e;
			rc.x1 += e;
			rc.y1 += e;
		}
		/* (Onyx: not a box holding a fixed box: the viewport's; nor a
		 * line holding floats: theirs) */
		if (!(box->flags & (HAS_FIXED | HAS_FLOATS)) &&
				(clip->y1 < rc.y0 || rc.y1 < clip->y0 ||
				clip->x1 < rc.x0 || rc.x1 < clip->x0))
			return true;
	}

	/*if the rectangle is under the page bottom but it can fit in a page,
	don't print it now*/
	if (html_redraw_printing) {
		if (r.y1 > html_redraw_printing_border) {
			if (r.y1 - r.y0 <= html_redraw_printing_border &&
					(box->type == BOX_TEXT ||
					box->type == BOX_TABLE_CELL
					|| box->object || box->gadget)) {
				/*remember the highest of all points from the
				not printed elements*/
				if (r.y0 < html_redraw_printing_top_cropped)
					html_redraw_printing_top_cropped = r.y0;
				return true;
			}
		}
		else box->flags |= PRINTED; /*it won't be printed anymore*/
	}

	/* if visibility is hidden render children only */
	if (box->style && css_computed_visibility(box->style) ==
			CSS_VISIBILITY_HIDDEN) {
		if ((ctx->plot->group_start) &&
		    (ctx->plot->group_start(ctx, "hidden box") != NSERROR_OK))
			return false;
		if (!html_redraw_box_children(html, box, x_parent, y_parent,
				&r, scale, current_background_color, ctx))
			return false;
		return ((!ctx->plot->group_end) || (ctx->plot->group_end(ctx) == NSERROR_OK));
	}

	if ((ctx->plot->group_start) &&
	    (ctx->plot->group_start(ctx,"vis box") != NSERROR_OK)) {
		return false;
	}

	/* Onyx: the box-shadow, under the box (and not where the box is), in the
	 * parent's clip */
	if (has_shadow) {
		struct onyx_shape sh;

		memset(&sh, 0, sizeof sh);
		onyx_rrect_outset(&orr, shadow.spread, &sh.outer);
		sh.outer.x0 += shadow.x;
		sh.outer.x1 += shadow.x;
		sh.outer.y0 += shadow.y;
		sh.outer.y1 += shadow.y;
		sh.blur = shadow.blur;
		sh.hole = true;
		sh.hole_rect = orr;
		sh.paint.colour = shadow.colour;
		if (ctx->plot->clip(ctx, clip) != NSERROR_OK ||
		    ctx->plot->onyx_shape(ctx, &sh) != NSERROR_OK)
			return false;
	}

	if (box->style != NULL &&
			css_computed_position(box->style) ==
					CSS_POSITION_ABSOLUTE &&
			css_computed_clip(box->style, &css_rect) ==
					CSS_CLIP_RECT) {
		/* We have an absolutly positioned box with a clip rect */
		if (css_rect.left_auto == false)
			r.x0 = x - border_left + FIXTOINT(css_unit_len2device_px(
					box->style, &html->unit_len_ctx,
					css_rect.left, css_rect.lunit));

		if (css_rect.top_auto == false)
			r.y0 = y - border_top + FIXTOINT(css_unit_len2device_px(
					box->style, &html->unit_len_ctx,
					css_rect.top, css_rect.tunit));

		if (css_rect.right_auto == false)
			r.x1 = x - border_left + FIXTOINT(css_unit_len2device_px(
					box->style, &html->unit_len_ctx,
					css_rect.right, css_rect.runit));

		if (css_rect.bottom_auto == false)
			r.y1 = y - border_top + FIXTOINT(css_unit_len2device_px(
					box->style, &html->unit_len_ctx,
					css_rect.bottom, css_rect.bunit));

		/* find intersection of clip rectangle and box */
		if (r.x0 < clip->x0) r.x0 = clip->x0;
		if (r.y0 < clip->y0) r.y0 = clip->y0;
		if (clip->x1 < r.x1) r.x1 = clip->x1;
		if (clip->y1 < r.y1) r.y1 = clip->y1;
		/* Nothing to do for invalid rectangles */
		if (r.x0 >= r.x1 || r.y0 >= r.y1)
			/* not an error */
			return ((!ctx->plot->group_end) ||
				(ctx->plot->group_end(ctx) == NSERROR_OK));
		/* clip to it */
		if (ctx->plot->clip(ctx, &r) != NSERROR_OK)
			return false;

	} else if (box->type == BOX_BLOCK || box->type == BOX_INLINE_BLOCK ||
			box->type == BOX_FLEX || box->type == BOX_INLINE_FLEX ||
			box->type == BOX_TABLE_CELL || box->object) {
		/* find intersection of clip rectangle and box (Onyx: and a flex /
		 * grid container's) */
		if (r.x0 < clip->x0) r.x0 = clip->x0;
		if (r.y0 < clip->y0) r.y0 = clip->y0;
		if (clip->x1 < r.x1) r.x1 = clip->x1;
		if (clip->y1 < r.y1) r.y1 = clip->y1;
		/* no point trying to draw 0-width/height boxes (Onyx: unless
		 * they hold a fixed box) */
		if ((r.x0 >= r.x1 || r.y0 >= r.y1) && (box->flags & HAS_FIXED)) {
			r.x0 = r.x1 = clip->x0;	/* (an empty clip, valid) */
			r.y0 = r.y1 = clip->y0;
		}
		/* (Onyx: nor inverted ones -- a box whose shadow reaches the clip
		 * but not its border box: its clip was set upside down, which the
		 * knockout refused, and the rest of the redraw was dropped) */
		if ((r.x0 >= r.x1 || r.y0 >= r.y1) && !(box->flags & HAS_FIXED))
			/* not an error */
			return ((!ctx->plot->group_end) ||
				(ctx->plot->group_end(ctx) == NSERROR_OK));
		/* clip to it */
		if (ctx->plot->clip(ctx, &r) != NSERROR_OK)
			return false;
	} else {
		/* clip box is fine, clip to it */
		r = *clip;
		if (ctx->plot->clip(ctx, &r) != NSERROR_OK)
			return false;
	}

	/* background colour and image for block level content and replaced
	 * inlines */

	bg_box = html_redraw_find_bg_box(box);

	/* Onyx: background-clip: text -- this box's background paints its text (its
	 * descendants'), not the box */
	if (box->style != NULL && ctx->plot->onyx_text_paint != NULL &&
	    box->type != BOX_TEXT && box->type != BOX_INLINE_END &&
	    css_computed_background_clip(box->style) ==
			CSS_BACKGROUND_CLIP_TEXT) {
		const char *grad = onyx_background_gradient(box->style);

		memset(&tf_paint, 0, sizeof tf_paint);
		if (grad != NULL && onyx_gradient_resolve(grad, box->style,
				&html->unit_len_ctx, scale, x, y,
				padding_width, padding_height, &tf_grad)) {
			tf_paint.gradient = &tf_grad;
		} else {
			css_color bgc;

			css_computed_background_color(box->style, &bgc);
			tf_paint.colour = nscss_color_to_ns(bgc);
		}
		onyx_text_fill = &tf_paint;
		if (bg_box == box)
			bg_box = NULL;	/* no box background */
	}

	/* Onyx: a mask-image: the box's background colour through the mask
	 * (below), in place of its background and its object */
	if (box->type != BOX_TEXT && box->type != BOX_INLINE_END &&
	    box->type != BOX_BR && onyx_mask_set(box)) {
		masked = true;
		if (bg_box == box)
			bg_box = NULL;
	}

	/* bg_box == NULL implies that this box should not have
	* its background rendered. Otherwise filter out linebreaks,
	* optimize away non-differing inlines, only plot background
	* for BOX_TEXT it's in an inline */
	if (bg_box && bg_box->type != BOX_BR &&
			bg_box->type != BOX_TEXT &&
			bg_box->type != BOX_INLINE_END &&
			(bg_box->type != BOX_INLINE || bg_box->object ||
			bg_box->flags & IFRAME || box->flags & REPLACE_DIM ||
			(bg_box->gadget != NULL &&
			(bg_box->gadget->type == GADGET_TEXTAREA ||
			bg_box->gadget->type == GADGET_TEXTBOX ||
			bg_box->gadget->type == GADGET_PASSWORD)))) {
		/* find intersection of clip box and border edge */
		struct rect p;
		p.x0 = x - border_left < r.x0 ? r.x0 : x - border_left;
		p.y0 = y - border_top < r.y0 ? r.y0 : y - border_top;
		p.x1 = x + padding_width + border_right < r.x1 ?
				x + padding_width + border_right : r.x1;
		p.y1 = y + padding_height + border_bottom < r.y1 ?
				y + padding_height + border_bottom : r.y1;
		if (!box->parent) {
			/* Root element, special case:
			 * background covers margins too */
			int m_left, m_top, m_right, m_bottom;
			if (scale == 1.0) {
				m_left = box->margin[LEFT];
				m_top = box->margin[TOP];
				m_right = box->margin[RIGHT];
				m_bottom = box->margin[BOTTOM];
			} else {
				m_left = box->margin[LEFT] * scale;
				m_top = box->margin[TOP] * scale;
				m_right = box->margin[RIGHT] * scale;
				m_bottom = box->margin[BOTTOM] * scale;
			}
			p.x0 = p.x0 - m_left < r.x0 ? r.x0 : p.x0 - m_left;
			p.y0 = p.y0 - m_top < r.y0 ? r.y0 : p.y0 - m_top;
			p.x1 = p.x1 + m_right < r.x1 ? p.x1 + m_right : r.x1;
			p.y1 = p.y1 + m_bottom < r.y1 ? p.y1 + m_bottom : r.y1;
		}
		/* valid clipping rectangles only */
		if ((p.x0 < p.x1) && (p.y0 < p.y1)) {
			/* Onyx: a rounded box's background is clipped to its
			 * corners; a gradient is painted over its colour */
			bool rclip = rounded && box->parent != NULL &&
					ctx->plot->onyx_round_clip != NULL;
			const char *grad = ctx->plot->onyx_shape != NULL ?
					onyx_background_gradient(bg_box->style) : NULL;

			if (rclip && (ctx->plot->clip(ctx, &p) != NSERROR_OK ||
			    ctx->plot->onyx_round_clip(ctx, &orr) != NSERROR_OK))
				return false;
			/* plot background */
			if (!html_redraw_background(x, y, box, scale, &p,
					&current_background_color, bg_box,
					&html->unit_len_ctx, ctx))
				return false;
			if (grad != NULL) {
				/* its box: the padding box (the root's: its margin
				 * box, the canvas) */
				struct onyx_gradient g;
				float gx = x, gy = y, gw = padding_width,
					gh = padding_height;
				if (!box->parent) {
					gx = p.x0 < x ? p.x0 : x;
					gy = y - (box->margin[TOP] + border_top) * scale;
					gw = (box->margin[LEFT] + box->margin[RIGHT]) *
						scale + padding_width;
					gh = (box->margin[TOP] + box->margin[BOTTOM]) *
						scale + padding_height;
					if (gh < p.y1 - gy)
						gh = p.y1 - gy;
				}
				if (onyx_gradient_resolve(grad, bg_box->style,
						&html->unit_len_ctx, scale,
						gx, gy, gw, gh, &g)) {
					struct onyx_shape sh;
					memset(&sh, 0, sizeof sh);
					sh.outer.x0 = p.x0;
					sh.outer.y0 = p.y0;
					sh.outer.x1 = p.x1;
					sh.outer.y1 = p.y1;
					sh.paint.gradient = &g;
					if (ctx->plot->clip(ctx, &p) != NSERROR_OK ||
					    ctx->plot->onyx_shape(ctx, &sh) != NSERROR_OK)
						return false;
				}
			}
			if (rclip && (ctx->plot->clip(ctx, &p) != NSERROR_OK ||
			    ctx->plot->onyx_round_clip(ctx, NULL) != NSERROR_OK))
				return false;
			/* restore previous graphics window */
			if (ctx->plot->clip(ctx, &r) != NSERROR_OK)
				return false;
		}
	}

	/* Onyx: the mask (the background colour through its alpha) */
	if (masked && (box->type != BOX_INLINE || box->object ||
			box->flags & REPLACE_DIM) &&
	    !onyx_mask_redraw(html, box, x - border_left, y - border_top,
			padding_width + border_left + border_right,
			padding_height + border_top + border_bottom,
			scale, &r, ctx))
		return false;

	/* borders for block level content and replaced inlines */
	if (box->style &&
	    box->type != BOX_TEXT &&
	    box->type != BOX_INLINE_END &&
	    (box->type != BOX_INLINE || box->object ||
	     box->flags & IFRAME || box->flags & REPLACE_DIM ||
	     (box->gadget != NULL &&
	      (box->gadget->type == GADGET_TEXTAREA ||
	       box->gadget->type == GADGET_TEXTBOX ||
	       box->gadget->type == GADGET_PASSWORD))) &&
	    (border_top || border_right || border_bottom || border_left)) {
		if (rounded) {
			/* Onyx: a rounded box's borders: its ring, each side in
			 * its colour (every style drawn solid) */
			struct onyx_shape sh;
			memset(&sh, 0, sizeof sh);
			sh.outer = orr;
			sh.ring = true;
			onyx_rrect_inset(&orr, border_left, border_top,
					border_right, border_bottom, &sh.inner);
			sh.per_side = true;
			for (int k = 0; k < 4; k++) {
				enum css_border_style_e bs = box->border[k].style;
				/* (nscss_color_to_ns is a macro: a plain value) */
				css_color bc = (box->border[k].width == 0 ||
						bs == CSS_BORDER_STYLE_NONE ||
						bs == CSS_BORDER_STYLE_HIDDEN) ?
						0 : box->border[k].c;
				sh.side_colour[k] = nscss_color_to_ns(bc);
			}
			if (ctx->plot->onyx_shape(ctx, &sh) != NSERROR_OK)
				return false;
		} else if (!html_redraw_borders(box, x_parent, y_parent,
				padding_width, padding_height, &r,
				scale, ctx))
			return false;
	}

	/* backgrounds and borders for non-replaced inlines */
	if (box->style && box->type == BOX_INLINE && box->inline_end &&
			(html_redraw_box_has_background(box) ||
			border_top || border_right ||
			border_bottom || border_left)) {
		/* inline backgrounds and borders span other boxes and may
		 * wrap onto separate lines */
		struct box *ib;
		struct rect b; /* border edge rectangle */
		struct rect p; /* clipped rect */
		bool first = true;
		int ib_x;
		int ib_y = y;
		int ib_p_width;
		int ib_b_left, ib_b_right;

		b.x0 = x - border_left;
		b.x1 = x + padding_width + border_right;
		b.y0 = y - border_top;
		b.y1 = y + padding_height + border_bottom;

		p.x0 = b.x0 < r.x0 ? r.x0 : b.x0;
		p.x1 = b.x1 < r.x1 ? b.x1 : r.x1;
		p.y0 = b.y0 < r.y0 ? r.y0 : b.y0;
		p.y1 = b.y1 < r.y1 ? b.y1 : r.y1;
		for (ib = box; ib; ib = ib->next) {
			/* to get extents of rectangle(s) associated with
			 * inline, cycle though all boxes in inline, skipping
			 * over floats */
			if (ib->type == BOX_FLOAT_LEFT ||
					ib->type == BOX_FLOAT_RIGHT)
				continue;
			if (scale == 1.0) {
				ib_x = x_parent + ib->x;
				ib_y = y_parent + ib->y;
				ib_p_width = ib->padding[LEFT] + ib->width +
						ib->padding[RIGHT];
				ib_b_left = ib->border[LEFT].width;
				ib_b_right = ib->border[RIGHT].width;
			} else {
				ib_x = (x_parent + ib->x) * scale;
				ib_y = (y_parent + ib->y) * scale;
				ib_p_width = (ib->padding[LEFT] + ib->width +
						ib->padding[RIGHT]) * scale;
				ib_b_left = ib->border[LEFT].width * scale;
				ib_b_right = ib->border[RIGHT].width * scale;
			}

			if ((ib->flags & NEW_LINE) && ib != box) {
				/* inline element has wrapped, plot background
				 * and borders */
				if (!html_redraw_inline_background(
						x, y, box, scale, &p, b,
						first, false,
						&current_background_color,
						&html->unit_len_ctx, ctx))
					return false;
				/* restore previous graphics window */
				if (ctx->plot->clip(ctx, &r) != NSERROR_OK)
					return false;
				if (!html_redraw_inline_borders(box, b, &r,
						scale, first, false, ctx))
					return false;
				/* reset coords */
				b.x0 = ib_x - ib_b_left;
				b.y0 = ib_y - border_top - padding_top;
				b.y1 = ib_y + padding_height - padding_top +
						border_bottom;

				p.x0 = b.x0 < r.x0 ? r.x0 : b.x0;
				p.y0 = b.y0 < r.y0 ? r.y0 : b.y0;
				p.y1 = b.y1 < r.y1 ? b.y1 : r.y1;

				first = false;
			}

			/* increase width for current box */
			b.x1 = ib_x + ib_p_width + ib_b_right;
			p.x1 = b.x1 < r.x1 ? b.x1 : r.x1;

			if (ib == box->inline_end)
				/* reached end of BOX_INLINE span */
				break;
		}
		/* plot background and borders for last rectangle of
		 * the inline */
		if (!html_redraw_inline_background(x, ib_y, box, scale, &p, b,
				first, true, &current_background_color,
				&html->unit_len_ctx, ctx))
			return false;
		/* restore previous graphics window */
		if (ctx->plot->clip(ctx, &r) != NSERROR_OK)
			return false;
		if (!html_redraw_inline_borders(box, b, &r, scale, first, true,
				ctx))
			return false;

	}

	/* Debug outlines */
	if (html_redraw_debug) {
		int margin_left, margin_right;
		int margin_top, margin_bottom;
		if (scale == 1.0) {
			/* avoid trivial fp maths */
			margin_left = box->margin[LEFT];
			margin_top = box->margin[TOP];
			margin_right = box->margin[RIGHT];
			margin_bottom = box->margin[BOTTOM];
		} else {
			margin_left = box->margin[LEFT] * scale;
			margin_top = box->margin[TOP] * scale;
			margin_right = box->margin[RIGHT] * scale;
			margin_bottom = box->margin[BOTTOM] * scale;
		}
		/* Content edge -- blue */
		rect.x0 = x + padding_left;
		rect.y0 = y + padding_top;
		rect.x1 = x + padding_left + width;
		rect.y1 = y + padding_top + height;
		if (ctx->plot->rectangle(ctx, plot_style_content_edge, &rect) != NSERROR_OK)
			return false;

		/* Padding edge -- red */
		rect.x0 = x;
		rect.y0 = y;
		rect.x1 = x + padding_width;
		rect.y1 = y + padding_height;
		if (ctx->plot->rectangle(ctx, plot_style_padding_edge, &rect) != NSERROR_OK)
			return false;

		/* Margin edge -- yellow */
		rect.x0 = x - border_left - margin_left;
		rect.y0 = y - border_top - margin_top;
		rect.x1 = x + padding_width + border_right + margin_right;
		rect.y1 = y + padding_height + border_bottom + margin_bottom;
		if (ctx->plot->rectangle(ctx, plot_style_margin_edge, &rect) != NSERROR_OK)
			return false;
	}

	/* clip to the padding edge for objects, or boxes with overflow hidden
	 * or scroll, unless it's the root element */
	if (box->parent != NULL) {
		bool need_clip = false;
		if (box->object || box->flags & IFRAME ||
				(overflow_x != CSS_OVERFLOW_VISIBLE &&
				 overflow_y != CSS_OVERFLOW_VISIBLE)) {
			r.x0 = x;
			r.y0 = y;
			r.x1 = x + padding_width;
			r.y1 = y + padding_height;
			if (r.x0 < clip->x0) r.x0 = clip->x0;
			if (r.y0 < clip->y0) r.y0 = clip->y0;
			if (clip->x1 < r.x1) r.x1 = clip->x1;
			if (clip->y1 < r.y1) r.y1 = clip->y1;
			if (r.x1 <= r.x0 || r.y1 <= r.y0) {
				/* Onyx: a fixed descendant is not clipped:
				 * the children painted in an empty clip */
				if (!(box->flags & HAS_FIXED))
					return (!ctx->plot->group_end ||
						(ctx->plot->group_end(ctx) ==
						 NSERROR_OK));
				r.x0 = r.x1 = clip->x0;
				r.y0 = r.y1 = clip->y0;
			}
			need_clip = true;

		} else if (overflow_x != CSS_OVERFLOW_VISIBLE) {
			r.x0 = x;
			r.y0 = clip->y0;
			r.x1 = x + padding_width;
			r.y1 = clip->y1;
			if (r.x0 < clip->x0) r.x0 = clip->x0;
			if (clip->x1 < r.x1) r.x1 = clip->x1;
			if (r.x1 <= r.x0) {
				/* Onyx: a fixed descendant is not clipped:
				 * the children painted in an empty clip */
				if (!(box->flags & HAS_FIXED))
					return (!ctx->plot->group_end ||
						(ctx->plot->group_end(ctx) ==
						 NSERROR_OK));
				r.x0 = r.x1 = clip->x0;
				r.y0 = r.y1 = clip->y0;
			}
			need_clip = true;

		} else if (overflow_y != CSS_OVERFLOW_VISIBLE) {
			r.x0 = clip->x0;
			r.y0 = y;
			r.x1 = clip->x1;
			r.y1 = y + padding_height;
			if (r.y0 < clip->y0) r.y0 = clip->y0;
			if (clip->y1 < r.y1) r.y1 = clip->y1;
			if (r.y1 <= r.y0) {
				/* Onyx: a fixed descendant is not clipped:
				 * the children painted in an empty clip */
				if (!(box->flags & HAS_FIXED))
					return (!ctx->plot->group_end ||
						(ctx->plot->group_end(ctx) ==
						 NSERROR_OK));
				r.x0 = r.x1 = clip->x0;
				r.y0 = r.y1 = clip->y0;
			}
			need_clip = true;
		}

		if (need_clip &&
		    (box->type == BOX_BLOCK ||
		     box->type == BOX_INLINE_BLOCK ||
		     box->type == BOX_FLEX || box->type == BOX_INLINE_FLEX ||
		     box->type == BOX_TABLE_CELL || box->object)) {
			/* Onyx: the clip outside it, for the absolute boxes in it
			 * whose containing block is outside it (onyx_oclip_escape) */
			if (onyx_layering && onyx_oclip_depth < ONYX_OCLIP_MAX) {
				onyx_oclip[onyx_oclip_depth].box = box;
				onyx_oclip[onyx_oclip_depth].outer = *clip;
				onyx_oclip_depth++;
			}
			if (ctx->plot->clip(ctx, &r) != NSERROR_OK)
				return false;
			/* Onyx: and to its padding box's rounded corners */
			if (rounded && ctx->plot->onyx_round_clip != NULL) {
				struct onyx_rrect pr;
				onyx_rrect_inset(&orr, border_left, border_top,
						border_right, border_bottom, &pr);
				if (ctx->plot->onyx_round_clip(ctx, &pr) !=
						NSERROR_OK)
					return false;
				round_clipped = true;
			}
		}
	}

	/* text decoration */
	if ((box->type != BOX_TEXT) &&
	    box->style &&
	    css_computed_text_decoration(box->style) !=	CSS_TEXT_DECORATION_NONE) {
		if (!html_redraw_text_decoration(box, x_parent, y_parent,
				scale, current_background_color, ctx))
			return false;
	}

	if (box->node != NULL) {
		exc = dom_html_element_get_tag_type(box->node, &tag_type);
		if (exc != DOM_NO_ERR) {
			tag_type = DOM_HTML_ELEMENT_TYPE__UNKNOWN;
		}
	} else {
		tag_type = DOM_HTML_ELEMENT_TYPE__UNKNOWN;
	}

	if (box->object && !masked && width != 0 && height != 0) {
		struct content_redraw_data obj_data;

		x_scrolled = x - scrollbar_get_offset(box->scroll_x) * scale;
		y_scrolled = y - scrollbar_get_offset(box->scroll_y) * scale;

		obj_data.x = x_scrolled + padding_left;
		obj_data.y = y_scrolled + padding_top;
		obj_data.width = width;
		obj_data.height = height;
		obj_data.background_colour = current_background_color;
		obj_data.scale = scale;
		obj_data.repeat_x = false;
		obj_data.repeat_y = false;

		if (content_get_type(box->object) == CONTENT_HTML) {
			obj_data.x /= scale;
			obj_data.y /= scale;
		}

		if (!content_redraw(box->object, &obj_data, &r, ctx)) {
			/* Show image fail */
			/* Unicode (U+FFFC) 'OBJECT REPLACEMENT CHARACTER' */
			const char *obj = "\xef\xbf\xbc";
			int obj_width;
			int obj_x = x + padding_left;
			nserror res;

			rect.x0 = x + padding_left;
			rect.y0 = y + padding_top;
			rect.x1 = x + padding_left + width - 1;
			rect.y1 = y + padding_top + height - 1;
			res = ctx->plot->rectangle(ctx, plot_style_broken_object, &rect);
			if (res != NSERROR_OK) {
				return false;
			}

			res = guit->layout->width(plot_fstyle_broken_object,
						  obj,
						  sizeof(obj) - 1,
						  &obj_width);
			if (res != NSERROR_OK) {
				obj_x += 1;
			} else {
				obj_x += width / 2 - obj_width / 2;
			}

			if (ctx->plot->text(ctx,
					    plot_fstyle_broken_object,
					    obj_x, y + padding_top + (int)(height * 0.75),
					    obj, sizeof(obj) - 1) != NSERROR_OK)
				return false;
		}
	} else if (tag_type == DOM_HTML_ELEMENT_TYPE_CANVAS &&
		   box->node != NULL &&
		   box->flags & REPLACE_DIM) {
		/* Canvas to draw */
		struct bitmap *bitmap = NULL;
		exc = dom_node_get_user_data(box->node,
					     corestring_dom___ns_key_canvas_node_data,
					     &bitmap);
		if (exc != DOM_NO_ERR) {
			bitmap = NULL;
		}
		if (bitmap != NULL &&
		    ctx->plot->bitmap(ctx, bitmap, x + padding_left, y + padding_top,
				      width, height, current_background_color,
				      BITMAPF_NONE) != NSERROR_OK)
			return false;
	} else if ((tag_type == DOM_HTML_ELEMENT_TYPE_VIDEO ||
		    tag_type == DOM_HTML_ELEMENT_TYPE_AUDIO) &&
		   box->node != NULL && box->flags & REPLACE_DIM) {
		/* Onyx: a <video>'s frame, the native controls (qjs_media.c) */
		if (!onyx_media_redraw(box->node, x + padding_left, y + padding_top,
				width, height, scale, &r, ctx))
			return false;
	} else if (box->iframe) {
		/* Offset is passed to browser window redraw unscaled */
		browser_window_redraw(box->iframe,
				x + padding_left,
				y + padding_top, &r, ctx);

	} else if (box->gadget && box->gadget->type == GADGET_CHECKBOX) {
		if (!html_redraw_checkbox(x + padding_left, y + padding_top,
				width, height, box->gadget->selected, ctx))
			return false;

	} else if (box->gadget && box->gadget->type == GADGET_RADIO) {
		if (!html_redraw_radio(x + padding_left, y + padding_top,
				width, height, box->gadget->selected, ctx))
			return false;

	} else if (box->gadget && box->gadget->type == GADGET_FILE) {
		if (!html_redraw_file(x + padding_left, y + padding_top,
				width, height, box, scale,
				current_background_color, &html->unit_len_ctx, ctx))
			return false;

	} else if (box->gadget &&
			(box->gadget->type == GADGET_TEXTAREA ||
			box->gadget->type == GADGET_PASSWORD ||
			box->gadget->type == GADGET_TEXTBOX)) {
		textarea_redraw(box->gadget->data.text.ta, x, y,
				current_background_color, scale, &r, ctx);

	} else if (box->text) {
		if (!html_redraw_text_box(html, box, x, y, &r, scale,
				current_background_color, ctx))
			return false;

	} else {
		if (!html_redraw_box_children(html, box, x_parent, y_parent, &r,
				scale, current_background_color, ctx))
			return false;
	}

	/* Onyx: the content's rounded clip ends (in the clip it began in) */
	if (round_clipped) {
		if (ctx->plot->clip(ctx, &r) != NSERROR_OK ||
		    ctx->plot->onyx_round_clip(ctx, NULL) != NSERROR_OK)
			return false;
	}

	if (box->type == BOX_BLOCK || box->type == BOX_INLINE_BLOCK ||
			box->type == BOX_FLEX || box->type == BOX_INLINE_FLEX ||
			box->type == BOX_TABLE_CELL || box->type == BOX_INLINE)
		if (ctx->plot->clip(ctx, clip) != NSERROR_OK)
			return false;

	/* list marker */
	if (box->list_marker) {
		if (!html_redraw_box(html, box->list_marker,
				x_parent + box->x -
				scrollbar_get_offset(box->scroll_x),
				y_parent + box->y -
				scrollbar_get_offset(box->scroll_y),
				clip, scale, current_background_color, ctx))
			return false;
	}

	/* scrollbars */
	if (((box->style && box->type != BOX_BR &&
	      box->type != BOX_TABLE && box->type != BOX_INLINE &&
	      (box->gadget == NULL || box->gadget->type != GADGET_TEXTAREA) &&
	      (overflow_x == CSS_OVERFLOW_SCROLL ||
	       overflow_x == CSS_OVERFLOW_AUTO ||
	       overflow_y == CSS_OVERFLOW_SCROLL ||
	       overflow_y == CSS_OVERFLOW_AUTO)) ||
	     (box->object && content_get_type(box->object) ==
	      CONTENT_HTML)) && box->parent != NULL) {
		nserror res;
		bool has_x_scroll = (overflow_x == CSS_OVERFLOW_SCROLL);
		bool has_y_scroll = (overflow_y == CSS_OVERFLOW_SCROLL);

		has_x_scroll |= (overflow_x == CSS_OVERFLOW_AUTO) &&
				box_hscrollbar_present(box);
		has_y_scroll |= (overflow_y == CSS_OVERFLOW_AUTO) &&
				box_vscrollbar_present(box);

		res = box_handle_scrollbars((struct content *)html,
					    box, has_x_scroll, has_y_scroll);
		if (res != NSERROR_OK) {
			NSLOG(netsurf, INFO, "%s", messages_get_errorcode(res));
			return false;
		}

		if (box->scroll_x != NULL)
			scrollbar_redraw(box->scroll_x,
					x_parent + box->x,
					y_parent + box->y + box->padding[TOP] +
					box->height + box->padding[BOTTOM] -
					SCROLLBAR_WIDTH, clip, scale, ctx);
		if (box->scroll_y != NULL)
			scrollbar_redraw(box->scroll_y,
					x_parent + box->x + box->padding[LEFT] +
					box->width + box->padding[RIGHT] -
					SCROLLBAR_WIDTH,
					y_parent + box->y, clip, scale, ctx);
	}

	if (box->type == BOX_BLOCK || box->type == BOX_INLINE_BLOCK ||
	    box->type == BOX_TABLE_CELL || box->type == BOX_INLINE) {
		if (ctx->plot->clip(ctx, clip) != NSERROR_OK)
			return false;
	}

	return ((!plot->group_end) || (ctx->plot->group_end(ctx) == NSERROR_OK));
}

/**
 * Draw a CONTENT_HTML using the current set of plotters (plot).
 *
 * \param  c	 content of type CONTENT_HTML
 * \param  data	 redraw data for this content redraw
 * \param  clip	 current clip region
 * \param  ctx	 current redraw context
 * \return true if successful, false otherwise
 *
 * x, y, clip_[xy][01] are in target coordinates.
 */

bool html_redraw(struct content *c, struct content_redraw_data *data,
		const struct rect *clip, const struct redraw_context *ctx)
{
	html_content *html = (html_content *) c;
	struct box *box;
	bool result = true;
	bool select, select_only;
	plot_style_t pstyle_fill_bg = {
		.fill_type = PLOT_OP_TYPE_SOLID,
		.fill_colour = data->background_colour,
	};

	int was_sx = onyx_view_sx, was_sy = onyx_view_sy;	/* (Onyx: an iframe's */
	int was_w = onyx_view_w, was_h = onyx_view_h;		/* redraw inside) */

	box = html->layout;
	assert(box);

	onyx_webfont_scope(html);	/* Onyx: drawn with its web fonts */
	if (html->bw != NULL && !html_redraw_printing)
		browser_window_onyx_viewport(html->bw, &onyx_view_sx, &onyx_view_sy,
				&onyx_view_w, &onyx_view_h);
	else
		onyx_view_sx = onyx_view_sy = onyx_view_w = onyx_view_h = 0;

	/* The select menu needs special treating because, when opened, it
	 * reaches beyond its layout box.
	 */
	select = false;
	select_only = false;
	if (ctx->interactive && html->visible_select_menu != NULL) {
		struct form_control *control = html->visible_select_menu;
		select = true;
		/* check if the redraw rectangle is completely inside of the
		   select menu */
		select_only = form_clip_inside_select_menu(control,
				data->scale, clip);
	}

	if (!select_only) {
		/* clear to background colour */
		result = (ctx->plot->clip(ctx, clip) == NSERROR_OK);

		if (html->background_colour != NS_TRANSPARENT)
			pstyle_fill_bg.fill_colour = html->background_colour;

		result &= (ctx->plot->rectangle(ctx, &pstyle_fill_bg, clip) == NSERROR_OK);

		/* Onyx: painted by layers (the positioned boxes after the rest) --
		 * not when printing (its pages' logic goes box by box) */
		{
			bool was = onyx_layering;
			int start = onyx_layer_count;
			struct rect was_clip = onyx_redraw_root_clip;
			bool was_set = onyx_redraw_root_clip_set;

			onyx_redraw_root_clip = *clip;
			onyx_redraw_root_clip_set = true;
			onyx_layering = !html_redraw_printing;
			result &= html_redraw_box(html, box, data->x, data->y, clip,
					data->scale, pstyle_fill_bg.fill_colour, ctx);
			if (onyx_layering)
				result &= onyx_layer_paint(html, start, data->scale,
						ctx);
			onyx_layer_count = start;
			onyx_layering = was;
			onyx_redraw_root_clip = was_clip;
			onyx_redraw_root_clip_set = was_set;
		}
	}

	if (select) {
		int menu_x, menu_y;
		box = html->visible_select_menu->box;
		box_coords(box, &menu_x, &menu_y);

		menu_x -= box->border[LEFT].width;
		menu_y += box->height + box->border[BOTTOM].width +
				box->padding[BOTTOM] + box->padding[TOP];
		result &= form_redraw_select_menu(html->visible_select_menu,
				data->x + menu_x, data->y + menu_y,
				data->scale, clip, ctx);
	}

	onyx_view_sx = was_sx;	/* (Onyx) */
	onyx_view_sy = was_sy;
	onyx_view_w = was_w;
	onyx_view_h = was_h;
	return result;

}
