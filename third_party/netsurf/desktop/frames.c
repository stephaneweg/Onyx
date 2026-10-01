/*
 * Copyright 2006 Richard Wilson <info@tinct.net>
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

/** \file
 * Frame and frameset creation and manipulation (implementation).
 */

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "utils/log.h"
#include "utils/utils.h"
#include "netsurf/content.h"
#include "content/hlcache.h"
#include "html/html.h"
#include "html/box.h"
#include "html/box_inspect.h"
#include "html/private.h"		/* Onyx: the document's iframes (onyx_frames_sync) */
#include "utils/corestrings.h"
#include "utils/nsurl.h"
#include "javascript/js.h"
#include <dom/dom.h>

#include "desktop/browser_private.h"
#include "desktop/frames.h"
#include "desktop/scrollbar.h"

/** maximum frame resize margin */
#define FRAME_RESIZE 6

static bool browser_window_resolve_frame_dimension(struct browser_window *bw,
		struct browser_window *sibling, int x, int y, bool width,
		bool height);


/**
 * Callback for (i)frame scrollbars.
 */
void browser_window_scroll_callback(void *client_data,
		struct scrollbar_msg_data *scrollbar_data)
{
	struct browser_window *bw = client_data;

	switch(scrollbar_data->msg) {
	case SCROLLBAR_MSG_MOVED:
		if (bw->browser_window_type == BROWSER_WINDOW_IFRAME) {
			browser_window_invalidate_iframe(bw);	/* (Onyx: no box: none) */
			browser_window_scrolled(bw);	/* (Onyx: its scripts' scroll event) */
		} else {
			struct rect rect;

			rect.x0 = scrollbar_get_offset(bw->scroll_x);
			rect.y0 = scrollbar_get_offset(bw->scroll_y);
			rect.x1 = rect.x0 + bw->width;
			rect.y1 = rect.y0 + bw->height;

			browser_window_invalidate_rect(bw, &rect);
		}
		break;
	case SCROLLBAR_MSG_SCROLL_START:
	{
		struct rect rect = {
			.x0 = scrollbar_data->x0,
			.y0 = scrollbar_data->y0,
			.x1 = scrollbar_data->x1,
			.y1 = scrollbar_data->y1
		};

		if (scrollbar_is_horizontal(scrollbar_data->scrollbar))
			browser_window_set_drag_type(bw, DRAGGING_SCR_X, &rect);
		else
			browser_window_set_drag_type(bw, DRAGGING_SCR_Y, &rect);
	}
		break;
	case SCROLLBAR_MSG_SCROLL_FINISHED:
		browser_window_set_drag_type(bw, DRAGGING_NONE, NULL);

		browser_window_set_pointer(bw, BROWSER_POINTER_DEFAULT);
		break;
	}
}

/* exported interface, documented in browser.h */
void browser_window_handle_scrollbars(struct browser_window *bw)
{
	struct hlcache_handle *h = bw->current_content;
	bool scroll_x;
	bool scroll_y;
	int c_width = 0;
	int c_height = 0;

	assert(!bw->window); /* Core-handled windows only */

	if (h != NULL) {
		c_width  = content_get_width(h);
		c_height = content_get_height(h);
	}

	if (bw->scrolling == BW_SCROLLING_YES) {
		scroll_x = true;
		scroll_y = true;
	} else if (bw->scrolling == BW_SCROLLING_AUTO &&
			bw->current_content) {
		/* Onyx: decided from the content alone (the scrollbars it had before -- a
		 * frame first laid out at another size -- kept one that was not needed any
		 * more): a scrollbar takes room from the other direction only when there */
		scroll_y = c_height > bw->height;
		scroll_x = c_width > bw->width - (scroll_y ? SCROLLBAR_WIDTH : 0);
		if (scroll_x && !scroll_y)
			scroll_y = c_height > bw->height - SCROLLBAR_WIDTH;
	} else {
		/* No scrollbars */
		scroll_x = false;
		scroll_y = false;
	}

	if (!scroll_x && bw->scroll_x != NULL) {
		scrollbar_destroy(bw->scroll_x);
		bw->scroll_x = NULL;
	}

	if (!scroll_y && bw->scroll_y != NULL) {
		scrollbar_destroy(bw->scroll_y);
		bw->scroll_y = NULL;
	}

	if (scroll_y) {
		int length = bw->height;
		int visible = bw->height - (scroll_x ? SCROLLBAR_WIDTH : 0);

		if (bw->scroll_y == NULL) {
			/* create vertical scrollbar */
			if (scrollbar_create(false, length, c_height, visible,
					     bw, browser_window_scroll_callback,
					     &(bw->scroll_y)) != NSERROR_OK) {
				return;
			}
		} else {
			/* update vertical scrollbar */
			scrollbar_set_extents(bw->scroll_y, length,
					visible, c_height);
		}
	}

	if (scroll_x) {
		int length = bw->width - (scroll_y ? SCROLLBAR_WIDTH : 0);
		int visible = length;

		if (bw->scroll_x == NULL) {
			/* create horizontal scrollbar */
			if (scrollbar_create(true, length, c_width, visible,
					     bw, browser_window_scroll_callback,
					     &(bw->scroll_x)) != NSERROR_OK) {
				return;
			}
		} else {
			/* update horizontal scrollbar */
			scrollbar_set_extents(bw->scroll_x, length,
					visible, c_width);
		}
	}

	if (scroll_x && scroll_y)
		scrollbar_make_pair(bw->scroll_x, bw->scroll_y);
}

/* exported function documented in desktop/frames.h */
nserror browser_window_invalidate_iframe(struct browser_window *bw)
{
	/* Onyx: a frame whose element has no box (not shown) has nothing to redraw */
	if (bw->box != NULL && bw->parent != NULL && bw->parent->current_content != NULL)
		html_redraw_a_box(bw->parent->current_content, bw->box);
	return NSERROR_OK;
}

/* exported function documented in desktop/frames.h */
nserror browser_window_create_iframes(struct browser_window *bw)
{
	/* Onyx: the frames follow the document's <iframe> elements (onyx_frames_sync); the
	 * ones its scripts made while it loaded are kept */
	if (bw->current_content == NULL ||
	    content_get_type(bw->current_content) != CONTENT_HTML)
		return NSERROR_OK;
	return onyx_frames_sync(bw, hlcache_handle_get_content(bw->current_content), true);
}


/* exported function documented in desktop/frames.h */
void browser_window_recalculate_iframes(struct browser_window *bw)
{
	struct browser_window *window;
	int index;

	for (index = 0; index < bw->iframe_count; index++) {
		window = bw->iframes[index];

		if (window != NULL) {
			browser_window_handle_scrollbars(window);
		}
	}
}


/* exported function documented in desktop/frames.h */
nserror browser_window_destroy_iframes(struct browser_window *bw)
{
	int i;

	if (bw->iframes != NULL) {
		struct browser_window **frames = bw->iframes;
		int n = bw->iframe_count;

		/* (Onyx: unlinked first: a frame's destruction may look at its siblings) */
		bw->iframes = NULL;
		bw->iframe_count = 0;
		for (i = 0; i < n; i++) {
			if (frames[i] == NULL)
				continue;
			if (frames[i]->box != NULL) {
				frames[i]->box->iframe = NULL;
				frames[i]->box = NULL;
			}
			browser_window_destroy_internal(frames[i]);
			free(frames[i]);
		}
		free(frames);
	}
	return NSERROR_OK;
}


/* ---- Onyx: iframes as browsing contexts ------------------------------------------------
 * A document's frames are its <iframe> elements -- all of them, shown or not (display:
 * none, visibility: hidden: a hidden frame loads and runs as in Chrome), with a src, a
 * srcdoc or neither (about:blank) --, each a browser window kept by its element. The box
 * tree made again (html_rebox) only links the windows to the new boxes; a window is
 * navigated when what its element asks for changes (src, srcdoc), destroyed when its
 * element leaves the document. Each window has a frame id for the scripts. */

static struct browser_window **onyx_fids;
static int onyx_nfids, onyx_capfids, onyx_next_fid;

/* exported function documented in desktop/frames.h */
int onyx_frame_id(struct browser_window *bw)
{
	if (bw == NULL)
		return 0;
	if (bw->onyx_fid != 0)
		return bw->onyx_fid;
	if (onyx_nfids == onyx_capfids) {
		int cap = onyx_capfids ? onyx_capfids * 2 : 32;
		struct browser_window **a = realloc(onyx_fids, cap * sizeof(*a));
		if (a == NULL)
			return 0;
		onyx_fids = a;
		onyx_capfids = cap;
	}
	onyx_fids[onyx_nfids++] = bw;
	bw->onyx_fid = ++onyx_next_fid;
	return bw->onyx_fid;
}

/* exported function documented in desktop/frames.h */
struct browser_window *onyx_frame_by_id(int fid)
{
	int i;

	if (fid <= 0)
		return NULL;
	for (i = 0; i < onyx_nfids; i++)
		if (onyx_fids[i]->onyx_fid == fid)
			return onyx_fids[i];
	return NULL;
}

/* exported function documented in desktop/frames.h */
void onyx_frame_release(struct browser_window *bw)
{
	int i;

	if (bw->onyx_fid != 0) {
		for (i = 0; i < onyx_nfids; i++)
			if (onyx_fids[i] == bw) {
				onyx_fids[i] = onyx_fids[--onyx_nfids];
				break;
			}
		bw->onyx_fid = 0;
	}
	if (bw->onyx_el != NULL) {
		dom_node_unref(bw->onyx_el);
		bw->onyx_el = NULL;
	}
	free(bw->onyx_src);
	bw->onyx_src = NULL;
	if (bw->onyx_srcdoc_url != NULL)
		nsurl_unref(bw->onyx_srcdoc_url);
	if (bw->onyx_srcdoc_base != NULL)
		nsurl_unref(bw->onyx_srcdoc_base);
	bw->onyx_srcdoc_url = bw->onyx_srcdoc_base = NULL;
	bw->onyx_owner = NULL;
}

/* exported function documented in desktop/frames.h */
void onyx_frames_owner_gone(void *htmlc)
{
	for (;;) {
		struct browser_window *f = NULL, *p;
		int i;

		for (i = 0; i < onyx_nfids && f == NULL; i++)
			if (onyx_fids[i]->onyx_owner == htmlc)
				f = onyx_fids[i];
		if (f == NULL)
			return;
		f->onyx_owner = NULL;
		p = f->parent;
		if (p == NULL)
			continue;
		for (i = 0; i < p->iframe_count; i++)
			if (p->iframes[i] == f) {
				memmove(&p->iframes[i], &p->iframes[i + 1],
						(p->iframe_count - i - 1) * sizeof(*p->iframes));
				p->iframe_count--;
				if (f->box != NULL)
					f->box->iframe = NULL;
				f->box = NULL;
				browser_window_destroy_internal(f);
				free(f);
				break;
			}
	}
}

/* exported function documented in desktop/frames.h */
struct nsurl *onyx_frames_srcdoc_base(struct nsurl *url)
{
	int i;

	if (url == NULL)
		return NULL;
	for (i = 0; i < onyx_nfids; i++)
		if (onyx_fids[i]->onyx_srcdoc_url != NULL &&
		    nsurl_compare(onyx_fids[i]->onyx_srcdoc_url, url, NSURL_COMPLETE))
			return onyx_fids[i]->onyx_srcdoc_base;
	return NULL;
}

static dom_string *onyx_ds(dom_string **cache, const char *s)
{
	if (*cache == NULL)
		dom_string_create_interned((const uint8_t *) s, strlen(s), cache);
	return *cache;
}
static dom_string *onyx_ds_iframe, *onyx_ds_srcdoc, *onyx_ds_sandbox;

/** an attribute's value as a C string (NULL: none); free() it */
static char *onyx_attr(dom_node *el, dom_string *name)
{
	dom_string *s = NULL;
	char *r;

	if (name == NULL || dom_element_get_attribute(el, name, &s) != DOM_NO_ERR || s == NULL)
		return NULL;
	r = strndup(dom_string_data(s), dom_string_byte_length(s));
	dom_string_unref(s);
	return r;
}

/** a new frame for an element of the window's document */
static struct browser_window *onyx_frame_new(struct browser_window *bw, html_content *htmlc,
		dom_node *el)
{
	struct browser_window *f = calloc(1, sizeof(*f));

	if (f == NULL)
		return NULL;
	/* the frames of a tab share its scripts' runtime: their windows reach each other
	 * (same-origin access, postMessage, MessagePort) -- javascript/js.h */
	f->jsheap = js_heap_share(bw->jsheap);
	if (browser_window_initialise_common(BW_CREATE_NONE, f, NULL) != NSERROR_OK) {
		browser_window_destroy_internal(f);
		free(f);
		return NULL;
	}
	f->browser_window_type = BROWSER_WINDOW_IFRAME;
	f->scrolling = BW_SCROLLING_AUTO;
	f->border = true;
	f->no_resize = true;
	f->scale = bw->scale;
	f->parent = bw;
	f->onyx_el = el;
	dom_node_ref(el);
	f->onyx_owner = htmlc;
	/* (a frame with no box: the default size of an iframe, its document laid out at it) */
	browser_window_set_dimensions(f, 300, 150);
	onyx_frame_id(f);
	return f;
}

/** true if a window up the frame's ancestors shows the URL (a frame of itself) */
static bool onyx_frame_recursive(struct browser_window *f, nsurl *url)
{
	struct browser_window *a;
	int depth = 0;

	for (a = f->parent; a != NULL; a = a->parent) {
		struct hlcache_handle *h = a->current_content != NULL ?
				a->current_content : a->loading_content;
		if (++depth > 10)
			return true;
		if (h != NULL && nsurl_compare(hlcache_handle_get_url(h), url,
				NSURL_SCHEME | NSURL_HOST | NSURL_PORT | NSURL_PATH | NSURL_QUERY))
			return true;
	}
	return false;
}

/** a srcdoc as a data: URL (its text percent-encoded) */
static nsurl *onyx_srcdoc_url(const char *doc)
{
	static const char pre[] = "data:text/html;charset=utf-8,";
	size_t n = strlen(doc), i, o = sizeof(pre) - 1;
	char *s = malloc(o + n * 3 + 1);
	nsurl *u = NULL;

	if (s == NULL)
		return NULL;
	memcpy(s, pre, o);
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char) doc[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
		    c == '-' || c == '_' || c == '.' || c == '~') {
			s[o++] = c;
		} else {
			s[o++] = '%';
			s[o++] = "0123456789ABCDEF"[c >> 4];
			s[o++] = "0123456789ABCDEF"[c & 15];
		}
	}
	s[o] = '\0';
	if (nsurl_create(s, &u) != NSERROR_OK)
		u = NULL;
	free(s);
	return u;
}

/** the frame's name, sandbox and document as its element says; navigated if that changed */
static void onyx_frame_update(struct browser_window *bw, html_content *htmlc,
		struct browser_window *f, bool current)
{
	dom_node *el = f->onyx_el;
	char *v, *key = NULL;
	nsurl *url = NULL;
	bool srcdoc = false;

	/* its name (targets: <a target>, <form target>, window.frames[name]) */
	v = onyx_attr(el, corestring_dom_name);
	if (v != NULL && (f->name == NULL || strcmp(v, f->name) != 0)) {
		free(f->name);
		f->name = v;
	} else {
		free(v);
	}

	/* what its document is: the srcdoc, else the src, else about:blank */
	v = onyx_attr(el, onyx_ds(&onyx_ds_srcdoc, "srcdoc"));
	if (v != NULL) {
		size_t n = strlen(v);
		key = malloc(n + 3);
		if (key != NULL) {
			memcpy(key, "D:", 2);
			memcpy(key + 2, v, n + 1);
		}
		srcdoc = true;
	} else {
		v = onyx_attr(el, corestring_dom_src);
		if (v != NULL) {
			char *p = v, *e = v + strlen(v);
			while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f')
				p++;
			while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' ||
					e[-1] == '\r' || e[-1] == '\f'))
				*--e = '\0';
			/* (a javascript: src -- "javascript:false", an old way to an empty
			 * frame: about:blank, as its document is in Chrome) */
			if (strncasecmp(p, "javascript:", 11) == 0)
				*p = '\0';
			if (*p != '\0' && htmlc->base_url != NULL &&
			    nsurl_join(htmlc->base_url, p, &url) != NSERROR_OK)
				url = NULL;
		}
		if (url == NULL)
			url = nsurl_ref(corestring_nsurl_about_blank);
		key = malloc(strlen(nsurl_access(url)) + 3);
		if (key != NULL) {
			memcpy(key, "U:", 2);
			strcpy(key + 2, nsurl_access(url));
		}
	}
	if (key == NULL || (f->onyx_src != NULL && strcmp(key, f->onyx_src) == 0)) {
		free(key);
		free(v);
		if (url != NULL)
			nsurl_unref(url);
		return;
	}
	free(f->onyx_src);
	f->onyx_src = key;

	/* the sandbox, as it is when the frame is navigated */
	{
		char *sb = onyx_attr(el, onyx_ds(&onyx_ds_sandbox, "sandbox"));
		f->onyx_sandbox = 0;
		if (sb != NULL) {
			char *tok, *save = NULL;
			f->onyx_sandbox = ONYX_SANDBOX;
			for (tok = strtok_r(sb, " \t\n\r\f", &save); tok != NULL;
			     tok = strtok_r(NULL, " \t\n\r\f", &save)) {
				if (strcasecmp(tok, "allow-scripts") == 0)
					f->onyx_sandbox |= ONYX_SANDBOX_SCRIPTS;
				else if (strcasecmp(tok, "allow-same-origin") == 0)
					f->onyx_sandbox |= ONYX_SANDBOX_ORIGIN;
				else if (strncasecmp(tok, "allow-top-navigation", 20) == 0)
					f->onyx_sandbox |= ONYX_SANDBOX_TOP;
			}
			free(sb);
		}
	}

	if (f->onyx_srcdoc_url != NULL)
		nsurl_unref(f->onyx_srcdoc_url);
	if (f->onyx_srcdoc_base != NULL)
		nsurl_unref(f->onyx_srcdoc_base);
	f->onyx_srcdoc_url = f->onyx_srcdoc_base = NULL;
	if (srcdoc) {
		url = onyx_srcdoc_url(v);
		if (url != NULL) {
			f->onyx_srcdoc_url = nsurl_ref(url);
			f->onyx_srcdoc_base = nsurl_ref(htmlc->base_url != NULL ?
					htmlc->base_url : content_get_url(&htmlc->base));
		}
	}
	free(v);
	if (url == NULL)
		return;
	if (!srcdoc && onyx_frame_recursive(f, url)) {
		/* (a page in a frame of itself: left empty, as in Chrome) */
		NSLOG(netsurf, INFO, "iframe of itself refused: %s", nsurl_access(url));
		nsurl_unref(url);
		url = nsurl_ref(corestring_nsurl_about_blank);
	}
	f->onyx_loaded = false;
	browser_window_navigate(f, url, content_get_url(&htmlc->base),
			BW_NAVIGATE_UNVERIFIABLE, NULL, NULL,
			current ? bw->current_content : NULL);
	nsurl_unref(url);
}

/** the frames linked to the boxes of their elements (the document laid out) */
static void onyx_frames_link(struct browser_window *bw, html_content *htmlc)
{
	struct content_html_iframe *cur;
	int k;

	for (k = 0; k < bw->iframe_count; k++)
		bw->iframes[k]->box = NULL;
	for (cur = htmlc->iframe; cur != NULL; cur = cur->next) {
		for (k = 0; k < bw->iframe_count; k++) {
			struct browser_window *f = bw->iframes[k];
			struct rect rect;

			if (f->onyx_el != cur->node || cur->box == NULL)
				continue;
			f->box = cur->box;
			cur->box->iframe = f;
			f->scrolling = cur->scrolling;
			f->border = cur->border;
			f->border_colour = cur->border_colour;
			f->margin_width = cur->margin_width;
			f->margin_height = cur->margin_height;
			box_bounds(cur->box, &rect);
			browser_window_set_position(f, rect.x0, rect.y0);
			browser_window_set_dimensions(f, rect.x1 - rect.x0, rect.y1 - rect.y0);
			break;
		}
	}
	browser_window_update_extent(bw);
	browser_window_recalculate_iframes(bw);
}

/* exported function documented in desktop/frames.h */
nserror onyx_frames_sync(struct browser_window *bw, void *pw, bool remove)
{
	html_content *htmlc = pw;
	dom_nodelist *list = NULL;
	struct browser_window **nf, **old;
	uint32_t n = 0, i;
	int nn = 0, nold, k;
	bool current;

	if (bw == NULL || htmlc == NULL || htmlc->document == NULL)
		return NSERROR_OK;
	current = bw->current_content != NULL &&
		hlcache_handle_get_content(bw->current_content) == &htmlc->base;
	if (onyx_ds(&onyx_ds_iframe, "iframe") != NULL &&
	    dom_document_get_elements_by_tag_name(htmlc->document, onyx_ds_iframe,
			&list) == DOM_NO_ERR && list != NULL)
		dom_nodelist_get_length(list, &n);
	if (n == 0 && bw->iframe_count == 0) {
		if (list != NULL)
			dom_nodelist_unref(list);
		return NSERROR_OK;
	}

	/* the frames in the document's order: kept by element, made for new elements */
	nf = calloc(n + bw->iframe_count + 1, sizeof(*nf));
	if (nf == NULL) {
		if (list != NULL)
			dom_nodelist_unref(list);
		return NSERROR_NOMEM;
	}
	old = bw->iframes;
	nold = bw->iframe_count;
	for (i = 0; i < n; i++) {
		dom_node *el = NULL;
		struct browser_window *f = NULL;

		if (dom_nodelist_item(list, i, &el) != DOM_NO_ERR || el == NULL)
			continue;
		for (k = 0; k < nold; k++)
			if (old[k] != NULL && old[k]->onyx_el == el) {
				f = old[k];
				old[k] = NULL;
				break;
			}
		if (f == NULL)
			f = onyx_frame_new(bw, htmlc, el);
		if (f != NULL)
			nf[nn++] = f;
		dom_node_unref(el);
	}
	if (list != NULL)
		dom_nodelist_unref(list);
	/* the frames whose element left the document (kept when asked: a script's sync) */
	for (k = 0; k < nold; k++) {
		if (old[k] != NULL && !remove) {
			nf[nn++] = old[k];
			old[k] = NULL;
		}
	}
	bw->iframes = nf;
	bw->iframe_count = nn;
	for (k = 0; k < nold; k++) {
		if (old[k] == NULL)
			continue;
		if (old[k]->box != NULL)
			old[k]->box->iframe = NULL;
		old[k]->box = NULL;
		browser_window_destroy_internal(old[k]);
		free(old[k]);
	}
	free(old);

	/* their boxes, then their documents */
	if (current)
		onyx_frames_link(bw, htmlc);
	for (k = 0; k < bw->iframe_count; k++)
		onyx_frame_update(bw, htmlc, bw->iframes[k], current);
	if (bw->iframe_count == 0) {
		free(bw->iframes);
		bw->iframes = NULL;
	}
	/* the window's named frames (window[name]) for its scripts */
	if (htmlc->jsthread != NULL)
		js_frames_changed(htmlc->jsthread);
	return NSERROR_OK;
}

/* exported function documented in desktop/frames.h */
struct browser_window *onyx_frame_for_element(struct browser_window *bw, void *htmlc,
		struct dom_node *el)
{
	int k, pass;

	if (bw == NULL || el == NULL)
		return NULL;
	for (pass = 0; pass < 2; pass++) {
		for (k = 0; k < bw->iframe_count; k++)
			if (bw->iframes[k]->onyx_el == el)
				return bw->iframes[k];
		/* (an element added since the last sync: its frame made now, as a script
		 * expects contentWindow right after appendChild) */
		if (pass == 0)
			onyx_frames_sync(bw, htmlc, false);
	}
	return NULL;
}

/* exported function documented in desktop/frames.h */
void onyx_frame_loaded(struct browser_window *bw)
{
	html_content *owner;

	if (bw == NULL || bw->browser_window_type != BROWSER_WINDOW_IFRAME ||
	    bw->onyx_loaded || bw->onyx_el == NULL)
		return;
	bw->onyx_loaded = true;
	owner = bw->onyx_owner;
	if (owner != NULL && owner->jsthread != NULL)
		js_fire_event(owner->jsthread, "load", owner->document, bw->onyx_el);
}

/* exported function documented in desktop/frames.h */
void onyx_frame_content_done(struct browser_window *bw)
{
	struct content *c;

	if (bw->browser_window_type != BROWSER_WINDOW_IFRAME || bw->current_content == NULL)
		return;
	c = hlcache_handle_get_content(bw->current_content);
	if (content_get_type(bw->current_content) != CONTENT_HTML ||
	    ((html_content *) c)->jsthread == NULL)
		onyx_frame_loaded(bw);
}

/* exported function documented in desktop/frames.h */
bool onyx_frames_loading(struct browser_window *bw)
{
	int k;

	if (bw == NULL)
		return false;
	for (k = 0; k < bw->iframe_count; k++)
		if (!bw->iframes[k]->onyx_loaded)
			return true;
	return false;
}


/**
 * Recalculate frameset positions following a resize.
 *
 * \param bw The browser window to reposition framesets for
 */
static void browser_window_recalculate_frameset_internal(struct browser_window *bw)
{
	int widths[bw->cols][bw->rows];
	int heights[bw->cols][bw->rows];
	int bw_width, bw_height;
	int avail_width, avail_height;
	int row, row2, col, index;
	struct browser_window *window;
	float relative;
	int size, extent, applied;
	int x, y;
	int new_width, new_height;

	assert(bw);

	/* window dimensions */
	if (!bw->parent) {
		browser_window_get_dimensions(bw, &bw_width, &bw_height);
		bw_width /= bw->scale;
		bw_height /= bw->scale;
		bw->x = 0;
		bw->y = 0;
		bw->width = bw_width;
		bw->height = bw_height;
	} else {
		bw_width = bw->width;
		bw_height = bw->height;
	}
	bw_width++;
	bw_height++;

	/* widths */
	for (row = 0; row < bw->rows; row++) {
		avail_width = bw_width;
		relative = 0;
		for (col = 0; col < bw->cols; col++) {
			index = (row * bw->cols) + col;
			window = &bw->children[index];

			switch (window->frame_width.unit) {
			case FRAME_DIMENSION_PIXELS:
				widths[col][row] = window->frame_width.value *
						window->scale;
				if (window->border) {
					if (col != 0)
						widths[col][row] += 1;
					if (col != bw->cols - 1)
						widths[col][row] += 1;
				}
				break;
			case FRAME_DIMENSION_PERCENT:
				widths[col][row] = bw_width *
						window->frame_width.value / 100;
				break;
			case FRAME_DIMENSION_RELATIVE:
				widths[col][row] = 0;
				relative += window->frame_width.value;
				break;
			default:
				/* unknown frame dimension unit */
				assert(window->frame_width.unit ==
						FRAME_DIMENSION_PIXELS ||
						window->frame_width.unit ==
						FRAME_DIMENSION_PERCENT ||
						window->frame_width.unit ==
						FRAME_DIMENSION_RELATIVE);
				break;
			}
			avail_width -= widths[col][row];
		}

		/* Redistribute to fit window */
		if ((relative > 0) && (avail_width > 0)) {
			/* Expand the relative sections to fill remainder */
			for (col = 0; col < bw->cols; col++) {
				index = (row * bw->cols) + col;
				window = &bw->children[index];

				if (window->frame_width.unit ==
						FRAME_DIMENSION_RELATIVE) {
					size = avail_width * window->
							frame_width.value /
							relative;
					avail_width -= size;
					relative -= window->frame_width.value;
					widths[col][row] += size;
				}
			}
		} else if (bw_width != avail_width) {
			/* proportionally distribute error */
			extent = avail_width;
			applied = 0;
			for (col = 0; col < bw->cols; col++) {
				if (col == bw->cols - 1) {
					/* Last cell, use up remainder */
					widths[col][row] += extent - applied;
					widths[col][row] =
							widths[col][row] < 0 ?
							0 : widths[col][row];
				} else {
					/* Find size of cell adjustment */
					size = (widths[col][row] * extent) /
							(bw_width - extent);
					/* Modify cell */
					widths[col][row] += size;
					applied += size;
				}
			}
		}
	}

	/* heights */
	for (col = 0; col < bw->cols; col++) {
		avail_height = bw_height;
		relative = 0;
		for (row = 0; row < bw->rows; row++) {
			index = (row * bw->cols) + col;
			window = &bw->children[index];

			switch (window->frame_height.unit) {
			case FRAME_DIMENSION_PIXELS:
				heights[col][row] = window->frame_height.value *
						window->scale;
				if (window->border) {
					if (row != 0)
						heights[col][row] += 1;
					if (row != bw->rows - 1)
						heights[col][row] += 1;
				}
				break;
			case FRAME_DIMENSION_PERCENT:
				heights[col][row] = bw_height *
						window->frame_height.value / 100;
				break;
			case FRAME_DIMENSION_RELATIVE:
				heights[col][row] = 0;
				relative += window->frame_height.value;
				break;
			default:
				/* unknown frame dimension unit */
				assert(window->frame_height.unit ==
						FRAME_DIMENSION_PIXELS ||
						window->frame_height.unit ==
						FRAME_DIMENSION_PERCENT ||
						window->frame_height.unit ==
						FRAME_DIMENSION_RELATIVE);
				break;
			}
			avail_height -= heights[col][row];
		}

		if (avail_height == 0)
			continue;

		/* Redistribute to fit window */
		if ((relative > 0) && (avail_height > 0)) {
			/* Expand the relative sections to fill remainder */
			for (row = 0; row < bw->rows; row++) {
				index = (row * bw->cols) + col;
				window = &bw->children[index];

				if (window->frame_height.unit ==
						FRAME_DIMENSION_RELATIVE) {
					size = avail_height * window->
							frame_height.value /
							relative;
					avail_height -= size;
					relative -= window->frame_height.value;
					heights[col][row] += size;
				}
			}
		} else if (bw_height != avail_height) {
			/* proportionally distribute error */
			extent = avail_height;
			applied = 0;
			for (row = 0; row < bw->rows; row++) {
				if (row == bw->rows - 1) {
					/* Last cell, use up remainder */
					heights[col][row] += extent - applied;
					heights[col][row] =
							heights[col][row] < 0 ?
							0 : heights[col][row];
				} else {
					/* Find size of cell adjustment */
					size = (heights[col][row] * extent) /
							(bw_height - extent);
					/* Modify cell */
					heights[col][row] += size;
					applied += size;
				}
			}
		}
	}

	/* position frames and calculate children */
	for (row = 0; row < bw->rows; row++) {
		x = 0;
		for (col = 0; col < bw->cols; col++) {
			index = (row * bw->cols) + col;
			window = &bw->children[index];

			y = 0;
			for (row2 = 0; row2 < row; row2++)
				y+= heights[col][row2];

			window->x = x;
			window->y = y;

			new_width = widths[col][row] - 1;
			new_height = heights[col][row] - 1;

			if (window->width != new_width ||
					window->height != new_height) {
				/* Change in frame size */
				browser_window_reformat(window, false,
						new_width * bw->scale,
						new_height * bw->scale);
				window->width = new_width;
				window->height = new_height;

				browser_window_handle_scrollbars(window);
			}

			x += widths[col][row];

			if (window->children)
				browser_window_recalculate_frameset_internal(window);
		}
	}
}


/**
 * Create and open a frameset for a browser window.
 *
 * \param[in,out] bw The browser window to create the frameset for
 * \param[in] frameset The frameset to create
 * \return NSERROR_OK or error code on faliure
 */
static nserror
browser_window_create_frameset_internal(struct browser_window *bw,
					struct content_html_frames *frameset)
{
	int row, col, index;
	struct content_html_frames *frame;
	struct browser_window *window;
	hlcache_handle *parent;

	assert(bw && frameset);

	/* 1. Create children */
	assert(bw->children == NULL);
	assert(frameset->cols + frameset->rows != 0);

	bw->children = calloc((frameset->cols * frameset->rows), sizeof(*bw));
	if (!bw->children) {
		return NSERROR_NOMEM;
	}

	bw->cols = frameset->cols;
	bw->rows = frameset->rows;
	for (row = 0; row < bw->rows; row++) {
		for (col = 0; col < bw->cols; col++) {
			index = (row * bw->cols) + col;
			frame = &frameset->children[index];
			window = &bw->children[index];

			/* Initialise common parts */
			browser_window_initialise_common(BW_CREATE_NONE,
					window, NULL);

			/* window characteristics */
			if (frame->children)
				window->browser_window_type =
						BROWSER_WINDOW_FRAMESET;
			else
				window->browser_window_type =
						BROWSER_WINDOW_FRAME;
			window->scrolling = frame->scrolling;
			window->border = frame->border;
			window->border_colour = frame->border_colour;
			window->no_resize = frame->no_resize;
			window->frame_width = frame->width;
			window->frame_height = frame->height;
			window->margin_width = frame->margin_width;
			window->margin_height = frame->margin_height;
			if (frame->name) {
				window->name = strdup(frame->name);
				if (!window->name) {
					free(bw->children);
					bw->children = NULL;
					return NSERROR_NOMEM;
				}
			}

			window->scale = bw->scale;

			/* linking */
			window->parent = bw;

			if (window->name)
				NSLOG(netsurf, INFO, "Created frame '%s'",
				      window->name);
			else
				NSLOG(netsurf, INFO,
				      "Created frame (unnamed)");
		}
	}

	/* 2. Calculate dimensions */
	browser_window_update_extent(bw);
	browser_window_recalculate_frameset_internal(bw);

	/* 3. Recurse for grandchildren */
	for (row = 0; row < bw->rows; row++) {
		for (col = 0; col < bw->cols; col++) {
			index = (row * bw->cols) + col;
			frame = &frameset->children[index];
			window = &bw->children[index];

			if (frame->children)
				browser_window_create_frameset_internal(window, frame);
		}
	}

	/* Use the URL of the first ancestor window containing html content
	 * as the referer */
	for (window = bw; window->parent; window = window->parent) {
		if (window->current_content &&
				content_get_type(window->current_content) ==
				CONTENT_HTML)
			break;
	}

	parent = window->current_content;

	/* 4. Launch content */
	for (row = 0; row < bw->rows; row++) {
		for (col = 0; col < bw->cols; col++) {
			index = (row * bw->cols) + col;
			frame = &frameset->children[index];
			window = &bw->children[index];

			if (frame->url) {
				browser_window_navigate(window,
					frame->url,
					hlcache_handle_get_url(parent),
					BW_NAVIGATE_HISTORY |
					BW_NAVIGATE_UNVERIFIABLE,
					NULL,
					NULL,
					parent);
			}
		}
	}

	return NSERROR_OK;
}


/* exported interface documented in desktop/frames.h */
nserror browser_window_create_frameset(struct browser_window *bw)
{
	struct content_html_frames *frameset;

	if (content_get_type(bw->current_content) != CONTENT_HTML) {
		return NSERROR_OK;
	}

	frameset = html_get_frameset(bw->current_content);
	if (frameset == NULL) {
		return NSERROR_OK;
	}

	return browser_window_create_frameset_internal(bw, frameset);
}




/**
 * Recalculate frameset positions following a resize.
 *
 * \param bw The browser window to reposition framesets for
 */

void browser_window_recalculate_frameset(struct browser_window *bw)
{
	if (content_get_type(bw->current_content) != CONTENT_HTML) {
		return;
	}

	if (html_get_frameset(bw->current_content) == NULL) {
		return;
	}

	browser_window_recalculate_frameset_internal(bw);
}


/**
 * Resize a browser window that is a frame.
 *
 * \param bw The browser window to resize
 * \param x The new width to set.
 * \param y The new height to set.
 */

void browser_window_resize_frame(struct browser_window *bw, int x, int y)
{
	struct browser_window *parent;
	struct browser_window *sibling;
	int col = -1, row = -1, i;
	bool change = false;

	parent = bw->parent;
	assert(parent);

	/* get frame location */
	for (i = 0; i < (parent->cols * parent->rows); i++) {
		if (&parent->children[i] == bw) {
			col = i % parent->cols;
			row = i / parent->cols;
		 }
	}
	assert((col >= 0) && (row >= 0));

	sibling = NULL;
	if (bw->drag.resize_left) {
		sibling = &parent->children[row * parent->cols + (col - 1)];
	} else if (bw->drag.resize_right) {
		sibling = &parent->children[row * parent->cols + (col + 1)];
	}
	if (sibling) {
		change |= browser_window_resolve_frame_dimension(bw, sibling,
				x, y, true, false);
	}

	sibling = NULL;
	if (bw->drag.resize_up) {
		sibling = &parent->children[(row - 1) * parent->cols + col];
	} else if (bw->drag.resize_down) {
		sibling = &parent->children[(row + 1) * parent->cols + col];
	}

	if (sibling) {
		change |= browser_window_resolve_frame_dimension(bw, sibling,
				x, y, false, true);
	}

	if (change) {
		browser_window_recalculate_frameset_internal(parent);
	}
}


bool browser_window_resolve_frame_dimension(struct browser_window *bw,
		struct browser_window *sibling,
		int x, int y, bool width, bool height)
{
	int bw_dimension, sibling_dimension;
	int bw_pixels, sibling_pixels;
	struct frame_dimension *bw_d, *sibling_d;
	float total_new;
	int frame_size;

	assert(!(width && height));

	/* extend/shrink the box to the pointer */
	if (width) {
		if (bw->drag.resize_left) {
			bw_dimension = bw->x + bw->width - x;
		} else {
			bw_dimension = x - bw->x;
		}
		bw_pixels = bw->width;
		sibling_pixels = sibling->width;
		bw_d = &bw->frame_width;
		sibling_d = &sibling->frame_width;
		frame_size = bw->parent->width;
	} else {
		if (bw->drag.resize_up) {
			bw_dimension = bw->y + bw->height - y;
		} else {
			bw_dimension = y - bw->y;
		}
		bw_pixels = bw->height;
		sibling_pixels = sibling->height;
		bw_d = &bw->frame_height;
		sibling_d = &sibling->frame_height;
		frame_size = bw->parent->height;
	}
	sibling_dimension = bw_pixels + sibling_pixels - bw_dimension;

	/* check for no change or no frame size*/
	if ((bw_dimension == bw_pixels) || (frame_size == 0))
		return false;
	/* check for both being 0 */
	total_new = bw_dimension + sibling_dimension;
	if ((bw_dimension + sibling_dimension) == 0)
		return false;

	/* our frame dimensions are now known to be:
	 *
	 * <--		    frame_size		    --> [VISIBLE PIXELS]
	 * |<--  bw_pixels -->|<--  sibling_pixels -->|	[VISIBLE PIXELS, BEFORE RESIZE]
	 * |<-- bw_d->value-->|<-- sibling_d->value-->| [SPECIFIED UNITS, BEFORE RESIZE]
	 * |<--bw_dimension-->|<--sibling_dimension-->|	[VISIBLE PIXELS, AFTER RESIZE]
	 * |<--		     total_new		   -->|	[VISIBLE PIXELS, AFTER RESIZE]
	 *
	 * when we resize, we must retain the original unit specification such that any
	 * subsequent resizing of the parent window will recalculate the page as the
	 * author specified.
	 *
	 * if the units of both frames are the same then we can resize the values simply
	 * by updating the values to be a percentage of the original widths.
	 */
	if (bw_d->unit == sibling_d->unit) {
		float total_specified = bw_d->value + sibling_d->value;
		bw_d->value = (total_specified * bw_dimension) / total_new;
		sibling_d->value = total_specified - bw_d->value;
		return true;
	}

	/* if one of the sizes is relative then we don't alter the relative width and
	 * just let it reflow across. the non-relative (pixel/percentage) value can
	 * simply be resolved to the specified width that will result in the required
	 * dimension.
	 */
	if (bw_d->unit == FRAME_DIMENSION_RELATIVE) {
		if ((sibling_pixels == 0) && (bw_dimension == 0))
			return false;
		if (fabs(sibling_d->value) < 0.0001)
			bw_d->value = 1;
		if (sibling_pixels == 0)
			sibling_d->value = (sibling_d->value * bw_pixels) / bw_dimension;
		else
			sibling_d->value =
					(sibling_d->value * sibling_dimension) / sibling_pixels;

		/* todo: the availble resize may have changed, update the drag box */
		return true;
	} else if (sibling_d->unit == FRAME_DIMENSION_RELATIVE) {
		if ((bw_pixels == 0) && (sibling_dimension == 0))
			return false;
		if (fabs(bw_d->value) < 0.0001)
			bw_d->value = 1;
		if (bw_pixels == 0)
			bw_d->value = (bw_d->value * sibling_pixels) / sibling_dimension;
		else
			bw_d->value = (bw_d->value * bw_dimension) / bw_pixels;

		/* todo: the availble resize may have changed, update the drag box */
		return true;
	}

	/* finally we have a pixel/percentage mix. unlike relative values, percentages
	 * can easily be backwards-calculated as they can simply be scaled like pixel
	 * values
	 */
	if (bw_d->unit == FRAME_DIMENSION_PIXELS) {
		float total_specified = bw_d->value + frame_size * sibling_d->value / 100;
		bw_d->value = (total_specified * bw_dimension) / total_new;
		sibling_d->value = (total_specified - bw_d->value) * 100 / frame_size;
		return true;
	} else if (sibling_d->unit == FRAME_DIMENSION_PIXELS) {
		float total_specified = bw_d->value * frame_size / 100 + sibling_d->value;
		sibling_d->value = (total_specified * sibling_dimension) / total_new;
		bw_d->value = (total_specified - sibling_d->value) * 100 / frame_size;
		return true;
	}
	assert(!"Invalid frame dimension unit");
	return false;
}


static bool browser_window_resize_frames(struct browser_window *bw,
		browser_mouse_state mouse, int x, int y,
		browser_pointer_shape *pointer)
{
	struct browser_window *parent;
	bool left, right, up, down;
	int i, resize_margin;

	if ((x < bw->x) || (x > bw->x + bw->width) ||
			(y < bw->y) || (y > bw->y + bw->height))
		return false;

	parent = bw->parent;
	if ((!bw->no_resize) && parent) {
		resize_margin = FRAME_RESIZE;
		if (resize_margin * 2 > bw->width)
			resize_margin = bw->width / 2;
		left = (x < bw->x + resize_margin);
		right = (x > bw->x + bw->width - resize_margin);
		resize_margin = FRAME_RESIZE;
		if (resize_margin * 2 > bw->height)
			resize_margin = bw->height / 2;
		up = (y < bw->y + resize_margin);
		down = (y > bw->y + bw-> height - resize_margin);

		/* check if the edges can actually be moved */
		if (left || right || up || down) {
			int row = -1, col = -1;
			switch (bw->browser_window_type) {
				case BROWSER_WINDOW_NORMAL:
				case BROWSER_WINDOW_IFRAME:
					assert(0);
					break;
				case BROWSER_WINDOW_FRAME:
				case BROWSER_WINDOW_FRAMESET:
					break;
			}
			for (i = 0; i < (parent->cols * parent->rows); i++) {
				if (&parent->children[i] == bw) {
					col = i % parent->cols;
					row = i / parent->cols;
					break;
				}
			}
			assert((row >= 0) && (col >= 0));

			/* check the sibling frame is within bounds */
			left &= (col > 0);
			right &= (col < parent->cols - 1);
			up &= (row > 0);
			down &= (row < parent->rows - 1);

			/* check the sibling frames can be resized */
			if (left)
				left &= !parent->children[row *
						parent->cols + (col - 1)].
						no_resize;
			if (right)
				right &= !parent->children[row *
						parent->cols + (col + 1)].
						no_resize;
			if (up)
				up &= !parent->children[(row - 1) *
						parent->cols + col].
						no_resize;
			if (down)
				down &= !parent->children[(row + 1) *
						parent->cols + col].
						no_resize;

			/* can't have opposite directions simultaneously */
			if (up)
				down = false;
			if (left)
				right = false;
		}

		if (left || right || up || down) {
			if (left) {
				if (down)
					*pointer = BROWSER_POINTER_LD;
				else if (up)
					*pointer = BROWSER_POINTER_LU;
				else
					*pointer = BROWSER_POINTER_LEFT;
			} else if (right) {
				if (down)
					*pointer = BROWSER_POINTER_RD;
				else if (up)
					*pointer = BROWSER_POINTER_RU;
				else
					*pointer = BROWSER_POINTER_RIGHT;
			} else if (up) {
				*pointer = BROWSER_POINTER_UP;
			} else {
				*pointer = BROWSER_POINTER_DOWN;
			}
			if (mouse & (BROWSER_MOUSE_DRAG_1 |
					BROWSER_MOUSE_DRAG_2)) {

				/* TODO: Pass appropriate rectangle to allow
				 *	 front end to clamp pointer range */
				browser_window_set_drag_type(bw,
						DRAGGING_FRAME, NULL);
				bw->drag.start_x = x;
				bw->drag.start_y = y;
				bw->drag.resize_left = left;
				bw->drag.resize_right = right;
				bw->drag.resize_up = up;
				bw->drag.resize_down = down;
			}
			return true;
		}
	}

	if (bw->children) {
		for (i = 0; i < (bw->cols * bw->rows); i++)
			if (browser_window_resize_frames(&bw->children[i],
					mouse, x, y, pointer))
				return true;
	}
	if (bw->iframes) {
		for (i = 0; i < bw->iframe_count; i++)
			if (browser_window_resize_frames(bw->iframes[i],
					mouse, x, y, pointer))
				return true;
	}
	return false;
}


bool browser_window_frame_resize_start(struct browser_window *bw,
		browser_mouse_state mouse, int x, int y,
		browser_pointer_shape *pointer)
{
	struct browser_window *root = browser_window_get_root(bw);
	int offx, offy;

	browser_window_get_position(bw, true, &offx, &offy);

	return browser_window_resize_frames(root, mouse,
			x + offx, y + offy, pointer);
}
