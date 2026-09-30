/*
 * Copyright 2013 Vincent Sanders <vince@netsurf-browser.org>
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
 * Processing for html content css operations.
 */

#include "utils/config.h"

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "utils/nsoption.h"
#include "utils/corestrings.h"
#include "utils/log.h"
#include "netsurf/inttypes.h"
#include "netsurf/misc.h"
#include "netsurf/content.h"
#include "content/hlcache.h"
#include "css/css.h"
#include "desktop/gui_internal.h"

#include "html/html.h"
#include "javascript/js.h"
#include "html/private.h"
#include "html/onyx_webfont.h"
#include "html/css.h"
#include "html/onyx_shadow.h"
#include "html/onyx_restyle.h"	/* Onyx */

static nsurl *html_default_stylesheet_url;
static nsurl *html_adblock_stylesheet_url;
static nsurl *html_quirks_stylesheet_url;
static nsurl *html_user_stylesheet_url;

/**
 * Convert css error to netsurf error.
 */
static nserror css_error_to_nserror(css_error error)
{
	switch (error) {
	case CSS_OK:
		return NSERROR_OK;

	case CSS_NOMEM:
		return NSERROR_NOMEM;

	case CSS_BADPARM:
		return NSERROR_BAD_PARAMETER;

	case CSS_INVALID:
		return NSERROR_INVALID;

	case CSS_FILENOTFOUND:
		return NSERROR_NOT_FOUND;

	case CSS_NEEDDATA:
		return NSERROR_NEED_DATA;

	case CSS_BADCHARSET:
		return NSERROR_BAD_ENCODING;

	case CSS_EOF:
	case CSS_IMPORTS_PENDING:
	case CSS_PROPERTY_NOT_SET:
	default:
		break;
	}
	return NSERROR_CSS;
}


/* Onyx: the style sheets changed after the conversion (a sheet come, replaced or gone):
 * the selection context is made again from the sheets there now and the boxes built
 * again (html_script_dom_changed). Before the conversion the context is made then. */
static void html_css_restyle(html_content *c)
{
	css_select_ctx *ctx = NULL;

	if (!c->conversion_begun && c->early_layout)
		c->early_stale = true;	/* (the early boxes made again when asked) */
	if (!c->conversion_begun || c->select_ctx == NULL || c->aborted)
		return;
	if (html_css_new_selection_context(c, &ctx) != NSERROR_OK)
		return;
	css_select_ctx_destroy(c->select_ctx);
	c->select_ctx = ctx;
	onyx_restyle_invalidate_all(c);	/* (Onyx: the kept selections were the old's) */
	html_script_dom_changed(c);
}


/**
 * Callback for fetchcache() for stylesheets.
 */
static nserror
html_convert_css_callback(hlcache_handle *css,
			  const hlcache_event *event,
			  void *pw)
{
	html_content *parent = pw;
	unsigned int i;
	struct html_stylesheet *s;

	/* Find sheet */
	for (i = 0, s = parent->stylesheets;
	     i != parent->stylesheet_count;
	     i++, s++) {
		if (s->sheet == css)
			break;
	}

	assert(i != parent->stylesheet_count);

	switch (event->type) {

	case CONTENT_MSG_DONE:
		NSLOG(netsurf, INFO, "done stylesheet slot %d '%s'", i,
		      nsurl_access(hlcache_handle_get_url(css)));
		parent->base.active--;
		NSLOG(netsurf, INFO, "%d fetches active", parent->base.active);
		/* Onyx: the <link> / <style>'s load event (async CSS loaders wait for it) */
		if (parent->jsthread != NULL && s->node != NULL)
			js_fire_event(parent->jsthread, "load", parent->document, s->node);
		break;

	case CONTENT_MSG_ERROR:
		NSLOG(netsurf, INFO, "stylesheet %s failed: %s",
		      nsurl_access(hlcache_handle_get_url(css)),
		      event->data.errordata.errormsg);
		printf("ONYX-CSS-ERR slot %d %s : %s\n", i,	/* DEBUG */
		       nsurl_access(hlcache_handle_get_url(css)),
		       event->data.errordata.errormsg ? event->data.errordata.errormsg : "?");
		fflush(stdout);

		hlcache_handle_release(css);
		s->sheet = NULL;
		parent->base.active--;
		NSLOG(netsurf, INFO, "%d fetches active", parent->base.active);
		if (parent->jsthread != NULL && s->node != NULL)	/* (Onyx) */
			js_fire_event(parent->jsthread, "error", parent->document, s->node);
		break;

	case CONTENT_MSG_POINTER:
		/* Really don't want this to continue after the switch */
		return NSERROR_OK;

	default:
		break;
	}

	/* Onyx: a script waiting for the sheets runs now they are in */
	if (event->type == CONTENT_MSG_DONE || event->type == CONTENT_MSG_ERROR)
		html_script_sheets_arrived(parent);
	/* Onyx: a sheet come after the conversion (a script's <style> or <link>: Facebook's
	 * Bloks, the single-page apps) restyles the page -- it was ignored */
	if (event->type == CONTENT_MSG_DONE || event->type == CONTENT_MSG_ERROR)
		html_css_restyle(parent);
	/* Onyx: a sheet come after the conversion (a script's <link>): its web fonts
	 * (@font-face) too -- the conversion's scan saw only the sheets before it */
	if (event->type == CONTENT_MSG_DONE && parent->conversion_begun)
		onyx_webfont_scan(parent);

	if (html_can_begin_conversion(parent)) {
		html_begin_conversion(parent);
	}

	return NSERROR_OK;
}


static nserror
html_stylesheet_from_domnode(html_content *c,
			     dom_node *node,
			     hlcache_handle **sheet)
{
	hlcache_child_context child;
	dom_string *style;
	nsurl *url;
	dom_exception exc;
	nserror error;
	uint32_t key;
	char urlbuf[64];

	child.charset = c->encoding;
	child.quirks = c->base.quirks;

	exc = dom_node_get_text_content(node, &style);
	if ((exc != DOM_NO_ERR) || (style == NULL)) {
		NSLOG(netsurf, INFO, "No text content");
		return NSERROR_OK;
	}

	error = html_css_fetcher_add_item(style, c->base_url, &key);
	if (error != NSERROR_OK) {
		dom_string_unref(style);
		return error;
	}

	dom_string_unref(style);

	snprintf(urlbuf, sizeof(urlbuf), "x-ns-css:%"PRIu32"", key);

	error = nsurl_create(urlbuf, &url);
	if (error != NSERROR_OK) {
		return error;
	}

	error = hlcache_handle_retrieve(url, 0,
			content_get_url(&c->base), NULL,
			html_convert_css_callback, c, &child, CONTENT_CSS,
			sheet);
	if (error != NSERROR_OK) {
		nsurl_unref(url);
		return error;
	}

	nsurl_unref(url);

	c->base.active++;
	NSLOG(netsurf, INFO, "%d fetches active", c->base.active);

	return NSERROR_OK;
}


/**
 * Process an inline stylesheet in the document.
 *
 * \param  c      content structure
 * \param  style  xml node of style element
 * \return  true on success, false if an error occurred
 */
static struct html_stylesheet *
html_create_style_element(html_content *c, dom_node *style)
{
	dom_string *val;
	dom_exception exc;
	struct html_stylesheet *stylesheets;

	/* type='text/css', or not present (invalid but common) */
	exc = dom_element_get_attribute(style, corestring_dom_type, &val);
	if (exc == DOM_NO_ERR && val != NULL) {
		if (!dom_string_caseless_lwc_isequal(val,
				corestring_lwc_text_css)) {
			dom_string_unref(val);
			return NULL;
		}
		dom_string_unref(val);
	}

	/* media contains 'screen' or 'all' or not present */
	exc = dom_element_get_attribute(style, corestring_dom_media, &val);
	if (exc == DOM_NO_ERR && val != NULL) {
		if (strcasestr(dom_string_data(val), "screen") == NULL &&
				strcasestr(dom_string_data(val),
						"all") == NULL) {
			dom_string_unref(val);
			return NULL;
		}
		dom_string_unref(val);
	}

	/* Extend array */
	stylesheets = realloc(c->stylesheets,
			      sizeof(struct html_stylesheet) *
			      (c->stylesheet_count + 1));
	if (stylesheets == NULL) {

		content_broadcast_error(&c->base, NSERROR_NOMEM, NULL);
		return false;

	}
	c->stylesheets = stylesheets;

	c->stylesheets[c->stylesheet_count].node = dom_node_ref(style);
	c->stylesheets[c->stylesheet_count].sheet = NULL;
	c->stylesheets[c->stylesheet_count].modified = false;
	c->stylesheets[c->stylesheet_count].removed = false;
	c->stylesheets[c->stylesheet_count].unused = false;
	c->stylesheet_count++;

	return c->stylesheets + (c->stylesheet_count - 1);
}


static bool
html_css_process_modified_style(html_content *c, struct html_stylesheet *s)
{
	hlcache_handle *sheet = NULL;
	nserror error;

	error = html_stylesheet_from_domnode(c, s->node, &sheet);
	if (error != NSERROR_OK) {
		NSLOG(netsurf, INFO, "Failed to update sheet");
		content_broadcast_error(&c->base, error, NULL);
		return false;
	}

	if (sheet != NULL) {
		NSLOG(netsurf, INFO, "Updating sheet %p with %p", s->sheet,
		      sheet);

		hlcache_handle *old = s->sheet;

		s->sheet = sheet;
		/* Onyx: the laid out page's selection context made again without the
		 * old sheet before it goes (the new one joins when it is done) */
		html_css_restyle(c);
		if (old != NULL) {
			switch (content_get_status(old)) {
			case CONTENT_STATUS_DONE:
				break;
			default:
				hlcache_handle_abort(old);
				c->base.active--;
				NSLOG(netsurf, INFO, "%d fetches active",
				      c->base.active);
			}
			hlcache_handle_release(old);
		}
	}

	s->modified = false;

	return true;
}


/**
 * process a stylesheet that has been modified.
 */
static void html_css_process_modified_styles(void *pw)
{
	html_content *c = pw;
	struct html_stylesheet *s;
	unsigned int i;
	bool all_done = true;

	for (i = 0, s = c->stylesheets; i != c->stylesheet_count; i++, s++) {
		if (c->stylesheets[i].modified) {
			all_done &= html_css_process_modified_style(c, s);
		}
	}

	/* If we failed to process any sheet, schedule a retry */
	if (all_done == false) {
		guit->misc->schedule(1000, html_css_process_modified_styles, c);
	}
}


/* exported function documented in html/css.h */
bool html_css_update_style(html_content *c, dom_node *style)
{
	unsigned int i;
	struct html_stylesheet *s;

	/* Find sheet */
	for (i = 0, s = c->stylesheets;	i != c->stylesheet_count; i++, s++) {
		if (s->node == style)
			break;
	}
	if (i == c->stylesheet_count) {
		s = html_create_style_element(c, style);
	}
	if (s == NULL) {
		NSLOG(netsurf, INFO,
		      "Could not find or create inline stylesheet for %p",
		      style);
		return false;
	}

	s->modified = true;

	guit->misc->schedule(0, html_css_process_modified_styles, c);

	return true;
}


/* exported function documented in html/css.h */
bool html_css_process_style(html_content *c, dom_node *node)
{
	unsigned int i;
	dom_string *val;
	dom_exception exc;
	struct html_stylesheet *s;

	/* Find sheet */
	for (i = 0, s = c->stylesheets;	i != c->stylesheet_count; i++, s++) {
		if (s->node == node)
			break;
	}

		/* Should already exist */
	if (i == c->stylesheet_count) {
		return false;
	}

	/* Onyx: a <style> put back (html_css_node_removed): its rules again */
	if (s->removed) {
		s->removed = false;
		s->unused = false;
		if (s->sheet == NULL)
			return html_css_update_style(c, node);
		html_css_restyle(c);
	}

	exc = dom_element_get_attribute(node, corestring_dom_media, &val);
	if (exc == DOM_NO_ERR && val != NULL) {
		if (strcasestr(dom_string_data(val), "screen") == NULL &&
				strcasestr(dom_string_data(val),
						"all") == NULL) {
			s->unused = true;
		}
		dom_string_unref(val);
	}

	return true;
}


/* exported function documented in html/css.h (Onyx) */
void html_css_node_removed(html_content *c, dom_node *node)
{
	unsigned int i;

	bool changed = false;

	/* the <style>s and <link>s in the subtree taken out of the document: their rules no
	 * longer apply (a theme switched, a single-page app's view gone) */
	for (i = STYLESHEET_START; i < c->stylesheet_count; i++) {
		struct html_stylesheet *s = &c->stylesheets[i];
		dom_node *n, *p;

		if (s->node == NULL || s->removed)
			continue;
		n = dom_node_ref(s->node);
		while (n != NULL && n != node) {
			if (dom_node_get_parent_node(n, &p) != DOM_NO_ERR)
				p = NULL;
			dom_node_unref(n);
			n = p;
		}
		if (n != NULL) {
			dom_node_unref(n);
			s->removed = true;
			s->unused = true;
			changed = true;
		}
	}
	if (changed)
		html_css_restyle(c);
}


/* exported function documented in html/css.h */
bool html_css_process_link(html_content *htmlc, dom_node *node)
{
	dom_string *rel, *type_attr, *media, *href;
	struct html_stylesheet *stylesheets;
	nsurl *joined;
	dom_exception exc;
	nserror ns_error;
	hlcache_child_context child;

	/* rel=<space separated list, including 'stylesheet'> */
	exc = dom_element_get_attribute(node, corestring_dom_rel, &rel);
	if (exc != DOM_NO_ERR || rel == NULL)
		return true;

	if (strcasestr(dom_string_data(rel), "stylesheet") == NULL) {
		dom_string_unref(rel);
		return true;
	} else if (strcasestr(dom_string_data(rel), "alternate") != NULL) {
		/* Ignore alternate stylesheets */
		dom_string_unref(rel);
		return true;
	}
	dom_string_unref(rel);

	if (nsoption_bool(author_level_css) == false) {
		return true;
	}

	/* type='text/css' or not present */
	exc = dom_element_get_attribute(node, corestring_dom_type, &type_attr);
	if (exc == DOM_NO_ERR && type_attr != NULL) {
		if (!dom_string_caseless_lwc_isequal(type_attr,
				corestring_lwc_text_css)) {
			dom_string_unref(type_attr);
			return true;
		}
		dom_string_unref(type_attr);
	}

	/* media contains 'screen' or 'all' or not present */
	exc = dom_element_get_attribute(node, corestring_dom_media, &media);
	if (exc == DOM_NO_ERR && media != NULL) {
		if (strcasestr(dom_string_data(media), "screen") == NULL &&
		    strcasestr(dom_string_data(media), "all") == NULL) {
			dom_string_unref(media);
			return true;
		}
		dom_string_unref(media);
	}

	/* href='...' */
	exc = dom_element_get_attribute(node, corestring_dom_href, &href);
	if (exc != DOM_NO_ERR || href == NULL)
		return true;

	/* TODO: only the first preferred stylesheets (ie.
	 * those with a title attribute) should be loaded
	 * (see HTML4 14.3) */

	ns_error = nsurl_join(htmlc->base_url, dom_string_data(href), &joined);
	if (ns_error != NSERROR_OK) {
		dom_string_unref(href);
		goto no_memory;
	}
	dom_string_unref(href);

	NSLOG(netsurf, INFO, "linked stylesheet %i '%s'",
	      htmlc->stylesheet_count, nsurl_access(joined));

	/* extend stylesheets array to allow for new sheet */
	stylesheets = realloc(htmlc->stylesheets,
			      sizeof(struct html_stylesheet) *
			      (htmlc->stylesheet_count + 1));
	if (stylesheets == NULL) {
		nsurl_unref(joined);
		ns_error = NSERROR_NOMEM;
		goto no_memory;
	}

	htmlc->stylesheets = stylesheets;
	/* Onyx: its <link>, for its load / error events and the CSSOM (link.sheet reads its
	 * rules) -- a ref, released with the sheets */
	htmlc->stylesheets[htmlc->stylesheet_count].node = dom_node_ref(node);
	htmlc->stylesheets[htmlc->stylesheet_count].modified = false;
	htmlc->stylesheets[htmlc->stylesheet_count].removed = false;
	htmlc->stylesheets[htmlc->stylesheet_count].unused = false;

	/* start fetch */
	child.charset = htmlc->encoding;
	child.quirks = htmlc->base.quirks;

	ns_error = hlcache_handle_retrieve(joined, 0,
			content_get_url(&htmlc->base),
			NULL, html_convert_css_callback,
			htmlc, &child, CONTENT_CSS,
			&htmlc->stylesheets[htmlc->stylesheet_count].sheet);

	nsurl_unref(joined);

	if (ns_error != NSERROR_OK) {
		dom_node_unref(htmlc->stylesheets[htmlc->stylesheet_count].node);
		goto no_memory;
	}

	htmlc->stylesheet_count++;

	htmlc->base.active++;
	NSLOG(netsurf, INFO, "%d fetches active", htmlc->base.active);

	return true;

no_memory:
	content_broadcast_error(&htmlc->base, ns_error, NULL);
	return false;
}


/* exported interface documented in html/html.h */
struct html_stylesheet *html_get_stylesheets(hlcache_handle *h, unsigned int *n)
{
	html_content *c = (html_content *) hlcache_handle_get_content(h);

	assert(c != NULL);
	assert(n != NULL);

	*n = c->stylesheet_count;

	return c->stylesheets;
}


/* exported function documented in html/css.h */
bool html_css_saw_insecure_stylesheets(html_content *html)
{
	struct html_stylesheet *s;
	unsigned int i;

	for (i = 0, s = html->stylesheets; i < html->stylesheet_count;
	     i++, s++) {
		if (s->sheet != NULL) {
			if (content_saw_insecure_objects(s->sheet)) {
				return true;
			}
		}
	}

	return false;
}


/* exported function documented in html/css.h */
nserror html_css_free_stylesheets(html_content *html)
{
	unsigned int i;

	guit->misc->schedule(-1, html_css_process_modified_styles, html);

	for (i = 0; i != html->stylesheet_count; i++) {
		if (html->stylesheets[i].sheet != NULL) {
			hlcache_handle_release(html->stylesheets[i].sheet);
		}
		if (html->stylesheets[i].node != NULL) {
			dom_node_unref(html->stylesheets[i].node);
		}
	}
	free(html->stylesheets);

	return NSERROR_OK;
}


/* exported function documented in html/css.h */
nserror html_css_quirks_stylesheets(html_content *c)
{
	nserror ns_error = NSERROR_OK;
	hlcache_child_context child;

	assert(c->stylesheets != NULL);

	if (c->quirks == DOM_DOCUMENT_QUIRKS_MODE_FULL) {
		child.charset = c->encoding;
		child.quirks = c->base.quirks;

		ns_error = hlcache_handle_retrieve(html_quirks_stylesheet_url,
				0, content_get_url(&c->base), NULL,
				html_convert_css_callback, c, &child,
				CONTENT_CSS,
				&c->stylesheets[STYLESHEET_QUIRKS].sheet);
		if (ns_error != NSERROR_OK) {
			return ns_error;
		}

		c->base.active++;
		NSLOG(netsurf, INFO, "%d fetches active", c->base.active);
	}

	return ns_error;
}


/* exported function documented in html/css.h */
nserror html_css_new_stylesheets(html_content *c)
{
	nserror ns_error;
	hlcache_child_context child;

	if (c->stylesheets != NULL) {
		return NSERROR_OK; /* already initialised */
	}

	/* stylesheet 0 is the base style sheet,
	 * stylesheet 1 is the quirks mode style sheet,
	 * stylesheet 2 is the adblocking stylesheet,
	 * stylesheet 3 is the user stylesheet */
	c->stylesheets = calloc(STYLESHEET_START,
			sizeof(struct html_stylesheet));
	if (c->stylesheets == NULL) {
		return NSERROR_NOMEM;
	}

	c->stylesheets[STYLESHEET_BASE].sheet = NULL;
	c->stylesheets[STYLESHEET_QUIRKS].sheet = NULL;
	c->stylesheets[STYLESHEET_ADBLOCK].sheet = NULL;
	c->stylesheets[STYLESHEET_USER].sheet = NULL;
	c->stylesheet_count = STYLESHEET_START;

	child.charset = c->encoding;
	child.quirks = c->base.quirks;

	ns_error = hlcache_handle_retrieve(html_default_stylesheet_url, 0,
			content_get_url(&c->base), NULL,
			html_convert_css_callback, c, &child, CONTENT_CSS,
			&c->stylesheets[STYLESHEET_BASE].sheet);
	if (ns_error != NSERROR_OK) {
		return ns_error;
	}

	c->base.active++;
	NSLOG(netsurf, INFO, "%d fetches active", c->base.active);


	if (nsoption_bool(block_advertisements)) {
		ns_error = hlcache_handle_retrieve(html_adblock_stylesheet_url,
				0, content_get_url(&c->base), NULL,
				html_convert_css_callback,
				c, &child, CONTENT_CSS,
				&c->stylesheets[STYLESHEET_ADBLOCK].sheet);
		if (ns_error != NSERROR_OK) {
			return ns_error;
		}

		c->base.active++;
		NSLOG(netsurf, INFO, "%d fetches active", c->base.active);

	}

	ns_error = hlcache_handle_retrieve(html_user_stylesheet_url, 0,
			content_get_url(&c->base), NULL,
			html_convert_css_callback, c, &child, CONTENT_CSS,
			&c->stylesheets[STYLESHEET_USER].sheet);
	if (ns_error != NSERROR_OK) {
		return ns_error;
	}

	c->base.active++;
	NSLOG(netsurf, INFO, "%d fetches active", c->base.active);

	return ns_error;
}


/* exported function documented in html/css.h */
nserror
html_css_new_selection_context(html_content *c, css_select_ctx **ret_select_ctx)
{
	uint32_t i;
	css_error css_ret;
	css_select_ctx *select_ctx;

	/* check that the base stylesheet loaded; layout fails without it */
	if (c->stylesheets[STYLESHEET_BASE].sheet == NULL) {
		return NSERROR_CSS_BASE;
	}
	/* Onyx: (nor before it is fetched: a script's getComputedStyle during the parse) */
	if (hlcache_handle_get_content(c->stylesheets[STYLESHEET_BASE].sheet) == NULL) {
		return NSERROR_CSS_BASE;
	}

	/* Create selection context */
	css_ret = css_select_ctx_create(&select_ctx);
	if (css_ret != CSS_OK) {
		return css_error_to_nserror(css_ret);
	}

	/* Add sheets to it */
	for (i = STYLESHEET_BASE; i != c->stylesheet_count; i++) {
		const struct html_stylesheet *hsheet = &c->stylesheets[i];
		css_stylesheet *sheet = NULL;
		css_origin origin = CSS_ORIGIN_AUTHOR;

		/* Filter out stylesheets for non-screen media. */
		/* TODO: We should probably pass the sheet in anyway, and let
		 *       libcss handle the filtering.
		 */
		if (hsheet->unused) {
			continue;
		}
		/* Onyx: a shadow tree's <style> is its own (html/onyx_shadow.c) */
		if (i >= STYLESHEET_START && hsheet->node != NULL &&
				dom_onyx_has_shadow(c->document) &&
				onyx_shadow_in_shadow_tree(hsheet->node)) {
			continue;
		}

		if (i < STYLESHEET_USER) {
			origin = CSS_ORIGIN_UA;
		} else if (i < STYLESHEET_START) {
			origin = CSS_ORIGIN_USER;
		}

		/* Onyx: only the sheets come (an early layout, while the others are
		 * still fetched); the variable was kept from the sheet before */
		sheet = NULL;
		if (hsheet->sheet != NULL &&
		    hlcache_handle_get_content(hsheet->sheet) != NULL &&
		    content_get_status(hsheet->sheet) == CONTENT_STATUS_DONE) {
			sheet = nscss_get_stylesheet(hsheet->sheet);
		}

		if (sheet != NULL) {
			/* TODO: Pass the sheet's full media query, instead of
			 *       "screen".
			 */
			css_ret = css_select_ctx_append_sheet(select_ctx,
							      sheet,
							      origin,
							      "screen");
			if (css_ret != CSS_OK) {
				css_select_ctx_destroy(select_ctx);
				return css_error_to_nserror(css_ret);
			}
		}
	}

	/* return new selection context to caller */
	*ret_select_ctx = select_ctx;
	return NSERROR_OK;
}


/* exported function documented in html/css.h */
nserror html_css_init(void)
{
	nserror error;

	error = html_css_fetcher_register();
	if (error != NSERROR_OK)
		return error;

	error = nsurl_create("resource:default.css",
			&html_default_stylesheet_url);
	if (error != NSERROR_OK)
		return error;

	error = nsurl_create("resource:adblock.css",
			&html_adblock_stylesheet_url);
	if (error != NSERROR_OK)
		return error;

	error = nsurl_create("resource:quirks.css",
			&html_quirks_stylesheet_url);
	if (error != NSERROR_OK)
		return error;

	error = nsurl_create("resource:user.css",
			&html_user_stylesheet_url);

	return error;
}


/* exported function documented in html/css.h */
void html_css_fini(void)
{
	if (html_user_stylesheet_url != NULL) {
		nsurl_unref(html_user_stylesheet_url);
		html_user_stylesheet_url = NULL;
	}

	if (html_quirks_stylesheet_url != NULL) {
		nsurl_unref(html_quirks_stylesheet_url);
		html_quirks_stylesheet_url = NULL;
	}

	if (html_adblock_stylesheet_url != NULL) {
		nsurl_unref(html_adblock_stylesheet_url);
		html_adblock_stylesheet_url = NULL;
	}

	if (html_default_stylesheet_url != NULL) {
		nsurl_unref(html_default_stylesheet_url);
		html_default_stylesheet_url = NULL;
	}
}
