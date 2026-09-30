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
 * Onyx: inline <svg> in HTML documents.
 *
 * hubbub builds an <svg> and its descendants in the SVG namespace (their names and
 * attributes in their camelCase). At box construction an outer <svg> becomes a replaced
 * box: its subtree is written out as SVG text -- the elements and attributes, a <style>'s
 * text, the element's CSS `color` as the root's `color` (currentColor) -- and the
 * elements it names by `href="#id"` / `url(#id)` that are elsewhere in the page (an icon
 * sprite's <symbol>s, a shared gradient) copied into a <defs> at its end. The text is
 * the object of the box, as a data: URL of type image/svg+xml, drawn by the SVG image
 * handler (image/onyx_svg.c, PlutoSVG): its intrinsic size is the <svg>'s width / height
 * (else its viewBox's, else 300 x 150), CSS sizes it as an image. The same markup gives
 * the same URL: the low-level cache shares one content between identical icons. A
 * script's change to the subtree builds the boxes again (html_script_dom_changed): a new
 * text, a new URL.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <dom/dom.h>

#include <libcss/libcss.h>

#include "utils/log.h"
#include "utils/nsurl.h"
#include "utils/nsoption.h"
#include "content/content_factory.h"
#include "css/utils.h"
#include "html/html.h"
#include "html/private.h"
#include "html/object.h"
#include "html/box.h"
#include "html/box_special.h"
#include "html/onyx_svg_inline.h"

/** the most bytes of SVG text an inline <svg> makes (a bigger one is not drawn) */
#define ONYX_SVG_MAX_TEXT (2 * 1024 * 1024)
/** the most elements copied from elsewhere in the page */
#define ONYX_SVG_MAX_REFS 64

struct svgbuf {
	char *data;
	size_t len, cap;
	bool failed;
	/* the ids the text defines, and those it names (#id), each '\0'-terminated in a list */
	struct svgbuf *defined, *named;
};

static void sb_put(struct svgbuf *b, const char *s, size_t n)
{
	if (b->failed)
		return;
	if (b->len + n + 1 > b->cap) {
		size_t cap = b->cap ? b->cap * 2 : 1024;
		char *d;

		while (cap < b->len + n + 1)
			cap *= 2;
		if (cap > ONYX_SVG_MAX_TEXT + 1024 || (d = realloc(b->data, cap)) == NULL) {
			b->failed = true;
			return;
		}
		b->data = d;
		b->cap = cap;
	}
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = '\0';
}

static void sb_str(struct svgbuf *b, const char *s)
{
	sb_put(b, s, strlen(s));
}

/** whether the list (words separated by '\0') has the word */
static bool sb_has(const struct svgbuf *list, const char *w, size_t n)
{
	size_t i = 0;

	if (list == NULL || list->data == NULL)
		return false;
	while (i < list->len) {
		size_t l = strlen(list->data + i);

		if (l == n && memcmp(list->data + i, w, n) == 0)
			return true;
		i += l + 1;
	}
	return false;
}

static void sb_add_word(struct svgbuf *list, const char *w, size_t n)
{
	if (list == NULL || n == 0 || n > 200 || sb_has(list, w, n))
		return;
	sb_put(list, w, n);
	sb_put(list, "", 1);
}

/** the names (#id) in an attribute's value: href="#id", url(#id) */
static void svg_note_refs(struct svgbuf *b, const char *v, size_t n)
{
	size_t i;

	if (b->named == NULL)
		return;
	if (n > 1 && v[0] == '#') {
		sb_add_word(b->named, v + 1, n - 1);
		return;
	}
	for (i = 0; i + 5 < n; i++) {
		if (memcmp(v + i, "url(", 4) == 0) {
			size_t j = i + 4, k;

			while (j < n && (v[j] == ' ' || v[j] == '\'' || v[j] == '"'))
				j++;
			if (j < n && v[j] == '#') {
				j++;
				for (k = j; k < n && v[k] != ')' && v[k] != '\'' && v[k] != '"' &&
						v[k] != ' '; k++)
					;
				sb_add_word(b->named, v + j, k - j);
			}
		}
	}
}

/* camelCase SVG names a script may have made lower case (createElement, setAttribute) */
static const char *const svg_names[] = {
	"clipPath", "linearGradient", "radialGradient", "foreignObject", "textPath",
	"viewBox", "preserveAspectRatio", "gradientUnits", "gradientTransform",
	"clipPathUnits", "spreadMethod", "patternUnits", "patternTransform",
	"maskUnits", "maskContentUnits", "patternContentUnits", "markerWidth",
	"markerHeight", "refX", "refY", "stdDeviation",
};

static void svg_put_name(struct svgbuf *b, dom_string *name)
{
	const char *s = dom_string_data(name);
	size_t n = dom_string_byte_length(name), i;

	for (i = 0; i < sizeof(svg_names) / sizeof(svg_names[0]); i++) {
		if (strlen(svg_names[i]) == n && strncasecmp(svg_names[i], s, n) == 0) {
			sb_str(b, svg_names[i]);
			return;
		}
	}
	/* (libdom gives an HTML document's element names in upper case) */
	for (i = 0; i < n && !b->failed; i++) {
		char c = s[i];

		if (c >= 'A' && c <= 'Z')
			c += 32;
		sb_put(b, &c, 1);
	}
}

static bool svg_name_is(dom_string *name, const char *s)
{
	return dom_string_byte_length(name) == strlen(s) &&
		strncasecmp(dom_string_data(name), s, strlen(s)) == 0;
}

/**
 * An element and its subtree written out as SVG text.
 * \param root  the outer <svg>: its `color` given by CSS (color_attr, or NULL)
 */
static void svg_write(struct svgbuf *b, dom_node *n, const char *color_attr, int depth)
{
	dom_node_type type;
	dom_string *name = NULL;
	dom_namednodemap *map = NULL;
	dom_node *child = NULL;
	uint32_t len = 0, i;
	bool is_style;

	if (b->failed || depth > 200)
		return;
	if (dom_node_get_node_type(n, &type) != DOM_NO_ERR || type != DOM_ELEMENT_NODE)
		return;
	if (dom_node_get_local_name(n, &name) != DOM_NO_ERR || name == NULL) {
		if (dom_node_get_node_name(n, &name) != DOM_NO_ERR || name == NULL)
			return;
	}
	is_style = svg_name_is(name, "style");
	sb_str(b, "<");
	svg_put_name(b, name);

	if (dom_node_get_attributes(n, &map) == DOM_NO_ERR && map != NULL) {
		dom_namednodemap_get_length(map, &len);
		for (i = 0; i < len; i++) {
			dom_node *a = NULL;
			dom_string *an = NULL, *av = NULL;

			if (dom_namednodemap_item(map, i, &a) != DOM_NO_ERR || a == NULL)
				continue;
			dom_attr_get_name((dom_attr *) a, &an);
			dom_attr_get_value((dom_attr *) a, &av);
			if (an != NULL && !(color_attr != NULL && svg_name_is(an, "color")) &&
			    strchr(dom_string_data(an), '"') == NULL) {
				const char *v = av != NULL ? dom_string_data(av) : "";
				size_t vn = av != NULL ? dom_string_byte_length(av) : 0, k;
				char q = memchr(v, '"', vn) == NULL ? '"' :
					memchr(v, '\'', vn) == NULL ? '\'' : 0;

				sb_str(b, " ");
				svg_put_name(b, an);
				sb_str(b, "=");
				if (q != 0) {
					sb_put(b, &q, 1);
					sb_put(b, v, vn);
					sb_put(b, &q, 1);
				} else {
					/* both quotes in the value: its double ones made single */
					sb_str(b, "\"");
					for (k = 0; k < vn; k++)
						sb_put(b, v[k] == '"' ? "'" : v + k, 1);
					sb_str(b, "\"");
				}
				if (svg_name_is(an, "id"))
					sb_add_word(b->defined, v, vn);
				else
					svg_note_refs(b, v, vn);
			}
			if (an != NULL)
				dom_string_unref(an);
			if (av != NULL)
				dom_string_unref(av);
			dom_node_unref(a);
		}
		dom_namednodemap_unref(map);
	}
	if (color_attr != NULL) {
		sb_str(b, " color=\"");
		sb_str(b, color_attr);
		sb_str(b, "\"");
	}
	sb_str(b, ">");

	if (dom_node_get_first_child(n, &child) == DOM_NO_ERR) {
		while (child != NULL && !b->failed) {
			dom_node *next = NULL;
			dom_node_type ct;

			if (dom_node_get_node_type(child, &ct) == DOM_NO_ERR) {
				if (ct == DOM_ELEMENT_NODE) {
					svg_write(b, child, NULL, depth + 1);
				} else if (is_style && (ct == DOM_TEXT_NODE ||
						ct == DOM_CDATA_SECTION_NODE)) {
					/* a sheet's text as it is (PlutoSVG reads it raw) */
					dom_string *t = NULL;

					if (dom_characterdata_get_data(child, &t) == DOM_NO_ERR &&
					    t != NULL) {
						sb_put(b, dom_string_data(t),
								dom_string_byte_length(t));
						dom_string_unref(t);
					}
				}
			}
			dom_node_get_next_sibling(child, &next);
			dom_node_unref(child);
			child = next;
		}
		if (child != NULL)
			dom_node_unref(child);
	}

	sb_str(b, "</");
	svg_put_name(b, name);
	sb_str(b, ">");
	dom_string_unref(name);
}

/** whether the element is in the SVG namespace */
static bool svg_in_svg_namespace(dom_node *n)
{
	dom_string *ns = NULL;
	bool yes;

	if (dom_node_get_namespace(n, &ns) != DOM_NO_ERR || ns == NULL)
		return false;
	yes = dom_string_isequal(ns, dom_namespaces[DOM_NAMESPACE_SVG]);
	dom_string_unref(ns);
	return yes;
}

/** whether the element is an SVG <svg> (in the SVG namespace) */
static bool svg_is_svg(dom_node *n)
{
	dom_string *ns = NULL, *name = NULL;
	bool yes = false;

	if (dom_node_get_namespace(n, &ns) != DOM_NO_ERR || ns == NULL)
		return false;
	if (dom_string_isequal(ns, dom_namespaces[DOM_NAMESPACE_SVG]) &&
	    dom_node_get_local_name(n, &name) == DOM_NO_ERR && name != NULL) {
		yes = svg_name_is(name, "svg");
		dom_string_unref(name);
	}
	dom_string_unref(ns);
	return yes;
}

static const char b64[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/** the text as a data: URL (NULL: no memory) */
static nsurl *svg_data_url(const char *data, size_t len)
{
	static const char head[] = "data:image/svg+xml;base64,";
	size_t n = sizeof(head) - 1 + (len + 2) / 3 * 4, i;
	char *url = malloc(n + 1), *o;
	nsurl *u = NULL;

	if (url == NULL)
		return NULL;
	memcpy(url, head, sizeof(head) - 1);
	o = url + sizeof(head) - 1;
	for (i = 0; i + 2 < len; i += 3) {
		uint32_t v = ((uint8_t) data[i] << 16) | ((uint8_t) data[i + 1] << 8) |
			(uint8_t) data[i + 2];
		*o++ = b64[v >> 18];
		*o++ = b64[(v >> 12) & 63];
		*o++ = b64[(v >> 6) & 63];
		*o++ = b64[v & 63];
	}
	if (i < len) {
		uint32_t v = (uint8_t) data[i] << 16;

		if (i + 1 < len)
			v |= (uint8_t) data[i + 1] << 8;
		*o++ = b64[v >> 18];
		*o++ = b64[(v >> 12) & 63];
		*o++ = i + 1 < len ? b64[(v >> 6) & 63] : '=';
		*o++ = '=';
	}
	*o = '\0';
	if (nsurl_create(url, &u) != NSERROR_OK)
		u = NULL;
	free(url);
	return u;
}

/* exported interface documented in html/onyx_svg_inline.h */
bool onyx_svg_box(dom_node *n, html_content *content, struct box *box,
		bool *convert_children)
{
	struct svgbuf text = { 0 }, defined = { 0 }, named = { 0 }, done = { 0 };
	char color[16];
	css_color cc = 0xff000000;
	nsurl *url;
	bool ok = true;
	int refs = 0;

	if (!svg_is_svg(n))
		return true;
	/* its children are the drawing's, not boxes */
	*convert_children = false;
	if (box->style == NULL ||
	    ns_computed_display(box->style, false) == CSS_DISPLAY_NONE ||
	    nsoption_bool(foreground_images) == false)
		return true;

	css_computed_color(box->style, &cc);
	snprintf(color, sizeof color, "#%06x", (unsigned) (cc & 0xffffff));
	text.defined = &defined;
	text.named = &named;
	svg_write(&text, n, color, 0);

	/* the elements it names that are elsewhere in the page, into a <defs> */
	if (!text.failed && named.len > 0 && text.len > 6) {
		size_t end = text.len - 6;	/* before "</svg>" */
		size_t i = 0;

		text.len = end;
		sb_str(&text, "<defs>");
		while (i < named.len && refs < ONYX_SVG_MAX_REFS && !text.failed) {
			const char *id = named.data + i;
			size_t l = strlen(id);

			i += l + 1;
			if (sb_has(&defined, id, l) || sb_has(&done, id, l))
				continue;
			sb_add_word(&done, id, l);
			{
				dom_string *ds = NULL;
				dom_element *e = NULL;

				if (dom_string_create((const uint8_t *) id, l, &ds) == DOM_NO_ERR) {
					if (dom_document_get_element_by_id(content->document, ds,
							&e) == DOM_NO_ERR && e != NULL &&
					    svg_in_svg_namespace((dom_node *) e)) {
						/* (it may name more: named grows, the loop reads them) */
						svg_write(&text, (dom_node *) e, NULL, 0);
						refs++;
					}
					if (e != NULL)
						dom_node_unref(e);
					dom_string_unref(ds);
				}
			}
		}
		sb_str(&text, "</defs></svg>");
	}

	if (!text.failed && text.data != NULL) {
		if (getenv("NS_SVGDEBUG")) fprintf(stderr, "ONYX-SVG %s\n", text.data);
		url = svg_data_url(text.data, text.len);
		if (url != NULL) {
			box->flags |= IS_REPLACED;
			ok = html_fetch_object(content, url, box, CONTENT_IMAGE, false);
			nsurl_unref(url);
		}
	}
	free(text.data);
	free(defined.data);
	free(named.data);
	free(done.data);
	return ok;
}
