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
#include "utils/corestrings.h"
#include "utils/nsoption.h"
#include "content/content_factory.h"
#include "css/utils.h"
#include "html/html.h"
#include "html/private.h"
#include "html/object.h"
#include "html/box.h"
#include "html/box_special.h"
#include "html/onyx_svg_inline.h"
#include "desktop/gui_internal.h"
#include "netsurf/misc.h"

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
	void *vars;		/* the <svg>'s libcss node data: its custom properties */
	float font_px;		/* the <svg>'s font size (its width="2em") */
};

static void sb_str(struct svgbuf *b, const char *s);

/** a root width / height in em / rem / ex written in px (the element's font size, as the
 * browsers' presentational width / height); false: not such a length */
static bool svg_put_font_length(struct svgbuf *b, const char *v, size_t n)
{
	char num[32], *end;
	float f, k;

	if (n == 0 || n >= sizeof num || b->font_px <= 0)
		return false;
	memcpy(num, v, n);
	num[n] = '\0';
	f = strtof(num, &end);
	if (end == num)
		return false;
	if (strcmp(end, "em") == 0)
		k = b->font_px;
	else if (strcmp(end, "rem") == 0)
		k = 16.f;
	else if (strcmp(end, "ex") == 0)
		k = b->font_px / 2;
	else
		return false;
	snprintf(num, sizeof num, "%g", f * k);
	sb_str(b, num);
	return true;
}

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

/**
 * A value written with its var(--x[, fallback]) replaced by the <svg>'s custom property
 * (libcss keeps them in the element's node data), else the fallback -- `fill: var(--c)`
 * in an icon's style is common. `depth` bounds a fallback's own var().
 */
static void svg_put_value(struct svgbuf *b, const char *v, size_t n, int depth)
{
	size_t i = 0;

	while (i < n) {
		const char *p = NULL;
		size_t k;

		for (k = i; k + 6 < n; k++) {
			if (memcmp(v + k, "var(", 4) == 0) {
				p = v + k;
				break;
			}
		}
		if (p == NULL || b->vars == NULL || depth > 4) {
			sb_put(b, v + i, n - i);
			return;
		}
		sb_put(b, v + i, p - (v + i));
		{
			size_t s = (p - v) + 4, e, nb, ne;
			int paren = 1;
			lwc_string *val;

			while (s < n && v[s] == ' ')
				s++;
			nb = s;
			while (s < n && v[s] != ',' && v[s] != ')' && v[s] != ' ')
				s++;
			ne = s;
			for (e = s; e < n && paren > 0; e++) {
				if (v[e] == '(')
					paren++;
				else if (v[e] == ')')
					paren--;
			}
			/* e: past the closing ')' */
			val = css_onyx_node_var(b->vars, v + nb, ne - nb);
			if (val != NULL) {
				svg_put_value(b, lwc_string_data(val), lwc_string_length(val),
						depth + 1);
				lwc_string_unref(val);
			} else {
				size_t f = ne;

				while (f < n && v[f] == ' ')
					f++;
				if (f < n && v[f] == ',') {
					size_t fe = e > 0 ? e - 1 : e;	/* before ')' */

					f++;
					while (f < fe && v[f] == ' ')
						f++;
					svg_put_value(b, v + f, fe - f, depth + 1);
				} else {
					/* undefined, no fallback: as written (PlutoSVG's own) */
					sb_put(b, p, (v + e) - p);
				}
			}
			i = e;
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
					if (color_attr == NULL || !(svg_name_is(an, "width") ||
							svg_name_is(an, "height")) ||
					    !svg_put_font_length(b, v, vn))
						svg_put_value(b, v, vn, 0);
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

/*
 * The elements named by id (a sprite's <symbol>s): each looked up (libdom walks the
 * tree) and written once per turn of the main loop -- a box construction is one turn, no
 * script runs in it -- kept raw (their var() substituted when copied: they take the
 * using <svg>'s custom properties), with the ids they define and name.
 */
#define SVG_REFS 128
static struct svg_ref {
	char *id;
	struct svgbuf text, defined, named;
	bool found;
} svg_refs[SVG_REFS];
static int svg_nrefs;
static bool svg_refs_scheduled;
static int svg_debug = -1;

static void svg_refs_clear(void *p)
{
	int i;

	(void) p;
	for (i = 0; i < svg_nrefs; i++) {
		free(svg_refs[i].id);
		free(svg_refs[i].text.data);
		free(svg_refs[i].defined.data);
		free(svg_refs[i].named.data);
	}
	memset(svg_refs, 0, sizeof svg_refs);
	svg_nrefs = 0;
	svg_refs_scheduled = false;
}

/** the element of that id, if an SVG one, written into b (with its ids noted) */
static bool svg_put_ref(struct svgbuf *b, html_content *content, const char *id, size_t l)
{
	struct svg_ref *r = NULL;
	size_t i;
	int k;

	for (k = 0; k < svg_nrefs; k++) {
		if (strlen(svg_refs[k].id) == l && memcmp(svg_refs[k].id, id, l) == 0) {
			r = &svg_refs[k];
			break;
		}
	}
	if (r == NULL) {
		dom_string *ds = NULL;
		dom_element *e = NULL;

		if (svg_nrefs == SVG_REFS)
			svg_refs_clear(NULL);
		r = &svg_refs[svg_nrefs];
		memset(r, 0, sizeof *r);
		r->id = malloc(l + 1);
		if (r->id == NULL)
			return false;
		memcpy(r->id, id, l);
		r->id[l] = '\0';
		svg_nrefs++;
		if (!svg_refs_scheduled) {
			svg_refs_scheduled = true;
			guit->misc->schedule(0, svg_refs_clear, NULL);
		}
		r->text.defined = &r->defined;
		r->text.named = &r->named;
		if (dom_string_create((const uint8_t *) id, l, &ds) == DOM_NO_ERR) {
			if (dom_document_get_element_by_id(content->document, ds, &e) ==
					DOM_NO_ERR && e != NULL &&
			    svg_in_svg_namespace((dom_node *) e)) {
				svg_write(&r->text, (dom_node *) e, NULL, 0);
				r->found = !r->text.failed;
			}
			if (e != NULL)
				dom_node_unref(e);
			dom_string_unref(ds);
		}
	}
	if (!r->found)
		return false;
	svg_put_value(b, r->text.data, r->text.len, 0);
	for (i = 0; i < r->defined.len; i += strlen(r->defined.data + i) + 1)
		sb_add_word(b->defined, r->defined.data + i, strlen(r->defined.data + i));
	for (i = 0; i < r->named.len; i += strlen(r->named.data + i) + 1)
		sb_add_word(b->named, r->named.data + i, strlen(r->named.data + i));
	return true;
}

/*
 * The data: URLs made, by their SVG text (the same icon many times in a page, and at each
 * new box construction): a small table, the least recently used replaced.
 */
#define SVG_URLS 256
#define SVG_URLS_BYTES (512 * 1024)
static struct svg_url {
	uint32_t hash;
	char *text;
	size_t len;
	nsurl *url;
	unsigned int used;
} svg_urls[SVG_URLS];
static size_t svg_urls_bytes;
static unsigned int svg_urls_clock;

static nsurl *svg_url_for(const char *text, size_t len)
{
	uint32_t h = 2166136261u;
	struct svg_url *slot = &svg_urls[0];
	size_t i;
	nsurl *u;

	for (i = 0; i < len; i++)
		h = (h ^ (uint8_t) text[i]) * 16777619u;
	svg_urls_clock++;
	for (i = 0; i < SVG_URLS; i++) {
		struct svg_url *e = &svg_urls[i];

		if (e->url != NULL && e->hash == h && e->len == len &&
		    memcmp(e->text, text, len) == 0) {
			e->used = svg_urls_clock;
			return nsurl_ref(e->url);
		}
		if (e->url == NULL || (slot->url != NULL && e->used < slot->used))
			slot = e;
	}
	u = svg_data_url(text, len);
	if (u == NULL || len > SVG_URLS_BYTES / 8)
		return u;
	if (slot->url != NULL) {
		nsurl_unref(slot->url);
		free(slot->text);
		svg_urls_bytes -= slot->len;
		slot->url = NULL;
	}
	while (svg_urls_bytes + len > SVG_URLS_BYTES) {
		/* (over the bytes: the oldest dropped) */
		struct svg_url *old = NULL;

		for (i = 0; i < SVG_URLS; i++) {
			if (svg_urls[i].url != NULL && (old == NULL || svg_urls[i].used < old->used))
				old = &svg_urls[i];
		}
		if (old == NULL)
			break;
		nsurl_unref(old->url);
		free(old->text);
		svg_urls_bytes -= old->len;
		old->url = NULL;
	}
	slot->text = malloc(len);
	if (slot->text == NULL)
		return u;
	memcpy(slot->text, text, len);
	slot->len = len;
	slot->hash = h;
	slot->url = nsurl_ref(u);
	slot->used = svg_urls_clock;
	svg_urls_bytes += len;
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
	if (svg_debug < 0)
		svg_debug = getenv("NS_SVGDEBUG") != NULL;
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
	if (dom_node_get_user_data(n, corestring_dom___ns_key_libcss_node_data,
			&text.vars) != DOM_NO_ERR)
		text.vars = NULL;
	{
		css_fixed fs = 0;
		css_unit fu = CSS_UNIT_PX;

		css_computed_font_size(box->style, &fs, &fu);
		text.font_px = FIXTOFLT(css_unit_len2device_px(box->style,
				&content->unit_len_ctx, fs, fu));
	}
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
			/* (it may name more: named grows, the loop reads them) */
			if (svg_put_ref(&text, content, id, l))
				refs++;
		}
		sb_str(&text, "</defs></svg>");
	}

	if (!text.failed && text.data != NULL) {
		if (svg_debug)
			fprintf(stderr, "ONYX-SVG %s\n", text.data);
		url = svg_url_for(text.data, text.len);
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
