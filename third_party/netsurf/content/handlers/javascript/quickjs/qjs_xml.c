/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 */

/**
 * \file
 * Onyx: XML for the scripts (docs/06 §43) -- the natives under html5.js' XML section:
 *
 *  - parseXML(text, kind): a new document parsed by expat (html/onyx_xml.c) -- DOMParser's
 *    XML types, XMLHttpRequest's responseXML, XSLT style sheets; a well-formedness error
 *    gives the DOM Parsing standard's error document (its root a <parsererror>);
 *  - docKind(doc) / setDocKind(doc, kind): a document's XML kind (0 HTML, 1 XML, 2 XHTML,
 *    3 SVG: dom_html_document_xml_kind) -- contentType, createElement's case, the
 *    serializer;
 *  - loadXslt(): xslt.js (XPath 1.0, XSLT 1.0) evaluated in this realm, once, when a script
 *    first asks for document.evaluate or XSLTProcessor (not in every realm's prelude);
 *  - objectSource(el): an <object> / <embed>'s resource as [type, text] (its SVG document
 *    for contentDocument / getSVGDocument: the object is drawn as an image).
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <dom/dom.h>
#include "quickjs.h"

#include "utils/errors.h"
#include "utils/log.h"
#include "netsurf/content.h"
#include "content/content.h"
#include "content/hlcache.h"
#include "html/private.h"
#include "html/box.h"
#include "html/html.h"
#include "html/onyx_xml.h"

#include "javascript/quickjs/qjs_canvas.h"	/* qjs_html_of, qjs_node_of */
#include "javascript/quickjs/qjs_frames.h"	/* qjs_wrap_node */
#include "javascript/quickjs/qjs_net.h"		/* qjs_eval_cached */
#include "javascript/quickjs/qjs_xml.h"

#include "qjs_xslt_js.h"	/* xslt.js as a C string (generated) */

static uint8_t *xslt_bc;
static size_t xslt_bc_len;

/** parseXML(text, kind) -> a new document */
static JSValue n_parse_xml(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	size_t len;
	const char *s = JS_ToCStringLen(ctx, &len, argc > 0 ? argv[0] : JS_UNDEFINED);
	int32_t kind = DOM_HTML_DOCUMENT_XML;
	dom_document *doc = NULL;
	bool error = false;
	JSValue v;

	(void) this_val;
	if (s == NULL)
		return JS_EXCEPTION;
	if (argc > 1)
		JS_ToInt32(ctx, &kind, argv[1]);
	if (kind <= 0 || kind > 3)
		kind = DOM_HTML_DOCUMENT_XML;
	if (onyx_xml_parse_document(s, len, kind, &doc, &error) != NSERROR_OK || doc == NULL) {
		JS_FreeCString(ctx, s);
		return JS_NULL;
	}
	JS_FreeCString(ctx, s);
	v = qjs_wrap_node(ctx, (struct dom_node *) doc);
	dom_node_unref(doc);
	return v;
}

static dom_html_document *doc_arg(JSContext *ctx, int argc, JSValueConst *argv)
{
	dom_node *n = argc > 0 ? qjs_node_of(argv[0]) : NULL;
	dom_node_type t;

	(void) ctx;
	if (n == NULL || dom_node_get_node_type(n, &t) != DOM_NO_ERR || t != DOM_DOCUMENT_NODE)
		return NULL;
	return (dom_html_document *) n;
}

/** docKind(doc) -> 0 HTML, 1 XML, 2 XHTML, 3 SVG */
static JSValue n_doc_kind(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_html_document *d = doc_arg(ctx, argc, argv);

	(void) this_val;
	return JS_NewInt32(ctx, d != NULL ? dom_html_document_get_xml_kind(d) : 0);
}

/** setDocKind(doc, kind) */
static JSValue n_set_doc_kind(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_html_document *d = doc_arg(ctx, argc, argv);
	int32_t kind = 0;

	(void) this_val;
	if (d != NULL && argc > 1 && JS_ToInt32(ctx, &kind, argv[1]) == 0 && kind >= 0 && kind <= 3)
		dom_html_document_set_xml_kind(d, kind);
	return JS_UNDEFINED;
}

/** loadXslt() -> xslt.js' library, evaluated in this realm (once: html5.js keeps it) */
static JSValue n_load_xslt(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	JSValue fn, r, args[2];

	(void) argc;
	(void) argv;
	fn = qjs_eval_cached(ctx, qjs_xslt_js, sizeof(qjs_xslt_js) - 1, "xslt.js", &xslt_bc,
			&xslt_bc_len);
	if (JS_IsException(fn))
		return fn;
	args[0] = JS_DupValue(ctx, this_val);	/* (the natives: N) */
	args[1] = JS_GetGlobalObject(ctx);
	r = JS_Call(ctx, fn, JS_UNDEFINED, 2, (JSValueConst *) args);
	JS_FreeValue(ctx, args[0]);
	JS_FreeValue(ctx, args[1]);
	JS_FreeValue(ctx, fn);
	return r;
}

/** objectSource(el) -> [type, text]: an <object> / <embed>'s resource, or null */
static JSValue n_object_source(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	html_content *htmlc = qjs_html_of(ctx);
	dom_node *el = argc > 0 ? qjs_node_of(argv[0]) : NULL;
	struct content_html_object *o;
	JSValue r;

	(void) this_val;
	if (htmlc == NULL || el == NULL)
		return JS_NULL;
	for (o = htmlc->object_list; o != NULL; o = o->next) {
		const uint8_t *data;
		size_t size = 0;
		lwc_string *mime;
		if (o->background || o->box == NULL || o->box->node != el || o->content == NULL)
			continue;
		if (content_get_status(o->content) != CONTENT_STATUS_DONE &&
		    content_get_status(o->content) != CONTENT_STATUS_READY)
			return JS_NULL;
		data = content_get_source_data(o->content, &size);
		mime = content_get_mime_type(o->content);
		r = JS_NewArray(ctx);
		JS_SetPropertyUint32(ctx, r, 0, JS_NewString(ctx, mime != NULL ?
				lwc_string_data(mime) : ""));
		JS_SetPropertyUint32(ctx, r, 1, JS_NewStringLen(ctx, data != NULL ?
				(const char *) data : "", data != NULL ? size : 0));
		if (mime != NULL)
			lwc_string_unref(mime);
		return r;
	}
	return JS_NULL;
}

/** a node's prefix (libdom keeps none for some elements: from its qualified name) */
static dom_string *prefix_of(dom_node *n)
{
	dom_string *px = NULL, *name = NULL;
	const char *d, *c;

	dom_node_get_prefix(n, &px);
	if (px != NULL)
		return px;
	if (dom_node_get_node_name(n, &name) != DOM_NO_ERR || name == NULL)
		return NULL;
	d = dom_string_data(name);
	c = memchr(d, ':', dom_string_byte_length(name));
	if (c != NULL && c > d)
		dom_string_create((const uint8_t *) d, c - d, &px);
	dom_string_unref(name);
	return px;
}

static JSValue dstr_js(JSContext *ctx, dom_string *s)
{
	JSValue v;

	if (s == NULL)
		return JS_NULL;
	v = JS_NewStringLen(ctx, dom_string_data(s), dom_string_byte_length(s));
	dom_string_unref(s);
	return v;
}

/**
 * xmlFlat(root) -> the subtree in document order, 7 slots a node: type, the node (null
 * for an attribute), its parent's record index (-1: none), local name (a processing
 * instruction's target), namespace, prefix, value (text, comment, attribute; null for an
 * element) -- XPath's data model built in one call (xslt.js)
 */
static JSValue n_xml_flat(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	dom_node *root = argc > 0 ? qjs_node_of(argv[0]) : NULL;
	JSValue arr;
	uint32_t k = 0;
	struct flat_level { dom_node *n; int32_t rec; } *stack = NULL;
	int depth = 0, cap = 0;
	dom_node *n;
	int32_t parent = -1;

	(void) this_val;
	if (root == NULL)
		return JS_NULL;
	arr = JS_NewArray(ctx);
	n = dom_node_ref(root);
	while (n != NULL) {
		dom_node_type t = 0;
		int32_t me = (int32_t) (k / 7);
		dom_node *next = NULL;

		dom_node_get_node_type(n, &t);
		JS_SetPropertyUint32(ctx, arr, k++, JS_NewInt32(ctx, t));
		JS_SetPropertyUint32(ctx, arr, k++, qjs_wrap_node(ctx, n));
		JS_SetPropertyUint32(ctx, arr, k++, JS_NewInt32(ctx, parent));
		if (t == DOM_ELEMENT_NODE) {
			dom_string *ln = NULL, *ns = NULL, *px = NULL;
			dom_namednodemap *attrs = NULL;
			uint32_t i, na = 0;
			dom_node_get_local_name(n, &ln);
			if (ln == NULL)
				dom_node_get_node_name(n, &ln);
			dom_node_get_namespace(n, &ns);
			if (ns != NULL)
				px = prefix_of(n);
			JS_SetPropertyUint32(ctx, arr, k++, dstr_js(ctx, ln));
			JS_SetPropertyUint32(ctx, arr, k++, dstr_js(ctx, ns));
			JS_SetPropertyUint32(ctx, arr, k++, dstr_js(ctx, px));
			JS_SetPropertyUint32(ctx, arr, k++, JS_NULL);
			if (dom_node_get_attributes(n, &attrs) == DOM_NO_ERR && attrs != NULL) {
				dom_namednodemap_get_length(attrs, &na);
				for (i = 0; i < na; i++) {
					dom_node *a = NULL;
					dom_string *al = NULL, *an = NULL, *ap = NULL, *av = NULL;
					if (dom_namednodemap_item(attrs, i, &a) != DOM_NO_ERR || a == NULL)
						continue;
					dom_node_get_local_name(a, &al);
					if (al == NULL)
						dom_node_get_node_name(a, &al);
					dom_node_get_namespace(a, &an);
					if (an != NULL)
						ap = prefix_of(a);
					dom_node_get_node_value(a, &av);
					JS_SetPropertyUint32(ctx, arr, k++, JS_NewInt32(ctx, 2));
					JS_SetPropertyUint32(ctx, arr, k++, JS_NULL);
					JS_SetPropertyUint32(ctx, arr, k++, JS_NewInt32(ctx, me));
					JS_SetPropertyUint32(ctx, arr, k++, dstr_js(ctx, al));
					JS_SetPropertyUint32(ctx, arr, k++, dstr_js(ctx, an));
					JS_SetPropertyUint32(ctx, arr, k++, dstr_js(ctx, ap));
					JS_SetPropertyUint32(ctx, arr, k++, av != NULL ? dstr_js(ctx, av) :
							JS_NewString(ctx, ""));
					dom_node_unref(a);
				}
				dom_namednodemap_unref(attrs);
			}
		} else {
			dom_string *name = NULL, *val = NULL;
			if (t == DOM_PROCESSING_INSTRUCTION_NODE || t == DOM_DOCUMENT_TYPE_NODE)
				dom_node_get_node_name(n, &name);
			dom_node_get_node_value(n, &val);
			JS_SetPropertyUint32(ctx, arr, k++, dstr_js(ctx, name));
			JS_SetPropertyUint32(ctx, arr, k++, JS_NULL);
			JS_SetPropertyUint32(ctx, arr, k++, JS_NULL);
			JS_SetPropertyUint32(ctx, arr, k++, dstr_js(ctx, val));
		}
		/* next: its first child, else its next sibling, else an ancestor's */
		if (t == DOM_ELEMENT_NODE || t == DOM_DOCUMENT_NODE ||
		    t == DOM_DOCUMENT_FRAGMENT_NODE)
			dom_node_get_first_child(n, &next);
		if (next != NULL) {
			if (depth == cap) {
				int nc = cap ? cap * 2 : 32;
				struct flat_level *s2 = realloc(stack, nc * sizeof(*stack));
				if (s2 == NULL) {
					dom_node_unref(next);
					dom_node_unref(n);
					break;
				}
				stack = s2;
				cap = nc;
			}
			stack[depth].n = n;
			stack[depth].rec = parent;
			depth++;
			parent = me;
			n = next;
			continue;
		}
		for (;;) {
			if (n == root) {
				dom_node_unref(n);
				n = NULL;
				break;
			}
			dom_node_get_next_sibling(n, &next);
			dom_node_unref(n);
			if (next != NULL) {
				n = next;
				break;
			}
			if (depth == 0) {
				n = NULL;
				break;
			}
			depth--;
			n = stack[depth].n;
			parent = stack[depth].rec;
		}
	}
	while (depth > 0)
		dom_node_unref(stack[--depth].n);
	free(stack);
	return arr;
}

static const JSCFunctionListEntry qjs_xml_fns[] = {
	JS_CFUNC_DEF("xmlFlat", 1, n_xml_flat),
	JS_CFUNC_DEF("parseXML", 2, n_parse_xml),
	JS_CFUNC_DEF("docKind", 1, n_doc_kind),
	JS_CFUNC_DEF("setDocKind", 2, n_set_doc_kind),
	JS_CFUNC_DEF("loadXslt", 0, n_load_xslt),
	JS_CFUNC_DEF("objectSource", 1, n_object_source),
};

void qjs_xml_natives(JSContext *ctx, JSValueConst natives)
{
	JS_SetPropertyFunctionList(ctx, natives, qjs_xml_fns,
			sizeof(qjs_xml_fns) / sizeof(qjs_xml_fns[0]));
}
