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
 * Onyx: web fonts. A document's @font-face rules, once its style sheets are in: each
 * face's first source the frontend reads (TrueType / OpenType, WOFF, WOFF2) is fetched
 * and handed to the font code for this document (guit->layout->add_face), and the
 * document laid out again as they come -- font-display: swap, its text drawn in the
 * fallback meanwhile. A face split in subsets (unicode-range) is fetched when it covers
 * some of Latin-1 (the pages this browser is for); the others fall back to the card's
 * fonts.
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <libcss/libcss.h>

#include "utils/log.h"
#include "utils/nsurl.h"
#include "netsurf/layout.h"
#include "netsurf/misc.h"
#include "content/llcache.h"
#include "content/content_protected.h"
#include "css/css.h"
#include "desktop/gui_internal.h"

#include "html/html.h"
#include "html/private.h"
#include "html/box.h"
#include "html/onyx_webfont.h"

/** a face fetched for a document */
struct onyx_webfont {
	struct onyx_webfont *next;
	html_content *html;
	llcache_handle *handle;		/* while fetching */
	char *family;
	nsurl *url;
	int wmin, wmax;
	bool italic;
};

/** the formats the frontend reads: WOFF2 when FreeType has Brotli */
#define ONYX_WEBFONT_FORMATS (CSS_FONT_FACE_FORMAT_OPENTYPE | \
		CSS_FONT_FACE_FORMAT_WOFF | CSS_FONT_FACE_FORMAT_WOFF2)

/**
 * The text measured in the old fonts forgotten: its widths, its spaces' and the boxes'
 * minimum / maximum widths (the layout keeps them from one reformat to the next).
 */
static void onyx_webfont_unmeasure(struct box *b)
{
	for (; b != NULL; b = b->next) {
		b->min_width = 0;
		b->max_width = UNKNOWN_MAX_WIDTH;
		if (b->text != NULL) {
			b->width = UNKNOWN_WIDTH;
			b->flags &= ~MEASURED;
		}
		if (b->space != 0)
			b->space = UNKNOWN_WIDTH;
		onyx_webfont_unmeasure(b->children);
	}
}

static void onyx_webfont_reflow(void *p)
{
	html_content *c = p;

	if ((c->base.status == CONTENT_STATUS_READY ||
	     c->base.status == CONTENT_STATUS_DONE) &&
	    !c->base.locked && c->layout != NULL) {
		NSLOG(netsurf, INFO, "web fonts in: %p laid out again", c);
		onyx_webfont_unmeasure(c->layout);
		content__reformat(&c->base, false, c->base.available_width,
				c->base.available_height);
	}
}

static nserror onyx_webfont_fetched(llcache_handle *handle,
		const llcache_event *event, void *pw)
{
	struct onyx_webfont *wf = pw;
	html_content *c = wf->html;
	const uint8_t *data;
	size_t size = 0;

	switch (event->type) {
	case LLCACHE_EVENT_DONE:
		data = llcache_handle_get_source_data(handle, &size);
		if (data != NULL && size > 0 &&
		    guit->layout->add_face(c, wf->family, wf->wmin, wf->wmax,
				wf->italic, data, size) == NSERROR_OK) {
			/* several faces come together: one layout for them */
			guit->misc->schedule(100, onyx_webfont_reflow, c);
		} else {
			NSLOG(netsurf, INFO, "web font %s: %s unread (%zu bytes)",
			      wf->family, nsurl_access(wf->url), size);
		}
		llcache_handle_release(handle);
		wf->handle = NULL;
		break;

	case LLCACHE_EVENT_ERROR:
		NSLOG(netsurf, INFO, "web font %s: %s failed", wf->family,
		      nsurl_access(wf->url));
		llcache_handle_release(handle);
		wf->handle = NULL;
		break;

	default:
		break;
	}
	return NSERROR_OK;
}

/** whether a face covers some of Latin-1 (or all: no unicode-range) */
static bool onyx_webfont_latin(const css_font_face *face)
{
	uint32_t n = css_font_face_count_unicode_ranges(face), i;

	if (n == 0)
		return true;
	for (i = 0; i < n; i++) {
		uint32_t first, last;

		css_font_face_get_unicode_range(face, i, &first, &last);
		if (first <= 0xff && last >= 0x20)
			return true;
	}
	return false;
}

/** a face's first source the frontend reads -- a URL, in a format it knows */
static lwc_string *onyx_webfont_source(const css_font_face *face)
{
	uint32_t n = 0, i;

	css_font_face_count_srcs(face, &n);
	for (i = 0; i < n; i++) {
		const css_font_face_src *src;
		css_font_face_format format;
		lwc_string *location = NULL;
		const char *s, *dot;

		if (css_font_face_get_src(face, i, &src) != CSS_OK ||
		    css_font_face_src_location_type(src) !=
				CSS_FONT_FACE_LOCATION_TYPE_URI ||
		    css_font_face_src_get_location(src, &location) != CSS_OK ||
		    location == NULL)
			continue;
		format = css_font_face_src_format(src);
		if (format != CSS_FONT_FACE_FORMAT_UNSPECIFIED) {
			if (format & ONYX_WEBFONT_FORMATS)
				return location;
			continue;
		}
		/* no format(): by its name (not an .eot, an .svg) */
		s = lwc_string_data(location);
		dot = strrchr(s, '.');
		if (dot != NULL && (strncasecmp(dot, ".eot", 4) == 0 ||
				    strncasecmp(dot, ".svg", 4) == 0))
			continue;
		return location;
	}
	return NULL;
}

static void onyx_webfont_face(void *pw, const css_font_face *face)
{
	html_content *c = pw;
	struct onyx_webfont *wf, *w;
	lwc_string *family = NULL, *location;
	uint16_t wmin, wmax;
	nsurl *url;
	nserror err;

	if (css_font_face_get_font_family(face, &family) != CSS_OK ||
	    family == NULL || !onyx_webfont_latin(face))
		return;
	location = onyx_webfont_source(face);
	if (location == NULL)
		return;
	if (nsurl_create(lwc_string_data(location), &url) != NSERROR_OK)
		return;
	css_font_face_font_weight_range(face, &wmin, &wmax);

	/* once per face (a sheet twice, a rule repeated) */
	for (w = c->webfonts; w != NULL; w = w->next) {
		if (nsurl_compare(w->url, url, NSURL_COMPLETE) &&
		    w->wmin == wmin && w->italic == (css_font_face_font_style(
				face) != CSS_FONT_STYLE_NORMAL) &&
		    strcmp(w->family, lwc_string_data(family)) == 0) {
			nsurl_unref(url);
			return;
		}
	}

	wf = calloc(1, sizeof(*wf));
	if (wf == NULL) {
		nsurl_unref(url);
		return;
	}
	wf->html = c;
	wf->url = url;
	wf->family = strdup(lwc_string_data(family));
	wf->wmin = wmin;
	wf->wmax = wmax;
	wf->italic = css_font_face_font_style(face) != CSS_FONT_STYLE_NORMAL;
	if (wf->family == NULL) {
		nsurl_unref(url);
		free(wf);
		return;
	}
	wf->next = c->webfonts;
	c->webfonts = wf;

	err = llcache_handle_retrieve(url, LLCACHE_RETRIEVE_NO_ERROR_PAGES,
			content_get_url(&c->base), NULL,
			onyx_webfont_fetched, wf, &wf->handle);
	if (err != NSERROR_OK) {
		wf->handle = NULL;
		return;
	}
	NSLOG(netsurf, INFO, "web font %s %d-%d%s: %s", wf->family, wmin, wmax,
	      wf->italic ? " italic" : "", nsurl_access(url));
}

/* exported interface documented in html/onyx_webfont.h */
void onyx_webfont_scan(html_content *c)
{
	uint32_t i;

	if (guit->layout->add_face == NULL)
		return;
	for (i = STYLESHEET_START; i < c->stylesheet_count; i++) {
		const struct html_stylesheet *hsheet = &c->stylesheets[i];
		css_stylesheet *sheet;

		if (hsheet->unused || hsheet->sheet == NULL)
			continue;
		sheet = nscss_get_stylesheet(hsheet->sheet);
		if (sheet != NULL)
			css_stylesheet_font_faces(sheet, onyx_webfont_face, c);
	}
}

/* exported interface documented in html/onyx_webfont.h */
void onyx_webfont_release(html_content *c)
{
	struct onyx_webfont *wf, *next;

	guit->misc->schedule(-1, onyx_webfont_reflow, c);
	for (wf = c->webfonts; wf != NULL; wf = next) {
		next = wf->next;
		if (wf->handle != NULL) {
			llcache_handle_abort(wf->handle);
			llcache_handle_release(wf->handle);
		}
		nsurl_unref(wf->url);
		free(wf->family);
		free(wf);
	}
	c->webfonts = NULL;
	if (guit->layout->release_faces != NULL)
		guit->layout->release_faces(c);
}

/* exported interface documented in html/onyx_webfont.h */
void onyx_webfont_scope(html_content *c)
{
	if (guit->layout->set_scope != NULL)
		guit->layout->set_scope(c);
}
