/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 */

/**
 * \file
 * Onyx: XML documents -- expat into libdom (onyx_xml.h, docs/06 §43).
 *
 * The parse builds the document as the HTML parser's binding does: an element is made
 * with its attributes, then appended to its parent (the mutation events NetSurf listens
 * to see a <link>, a <style>, an <img> as with HTML), its children after it. Text is
 * gathered and appended once a markup event comes. Expat runs in namespace mode (names
 * "uri\nlocal\nprefix"); the namespace declarations become the xmlns attributes of the
 * element that declares them, as in the browsers' DOM. Entities: the five, the internal
 * subset's (expat), and -- in a document with an external DTD it does not read (XHTML's
 * doctypes) -- HTML's named character references (&nbsp; &copy;...), as Chrome. An
 * encoding expat does not know (windows-1252, ISO-8859-2...) is read through
 * libparserutils' single-byte codecs.
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <time.h>

#include <dom/dom.h>
#include <dom/bindings/hubbub/parser.h>
#include <parserutils/charset/codec.h>
#include <expat.h>

#include "utils/errors.h"
#include "utils/log.h"
#include "html/onyx_xml.h"

/* libdom's (the HTML binding sets it: getElementById by "id") */
void _dom_document_set_id_name(dom_document *doc, dom_string *name);

/* HTML's named character references (libhubbub's table, Onyx's) */
#include "../../../../libhubbub/src/tokeniser/onyx_entities.inc"

#define NS_SEP '\n'

static const char NS_XHTML[] = "http://www.w3.org/1999/xhtml";
static const char NS_SVG[] = "http://www.w3.org/2000/svg";
static const char NS_XMLNS[] = "http://www.w3.org/2000/xmlns/";
static const char NS_PARSERERROR[] = "http://www.mozilla.org/newlayout/xml/parsererror.xml";

struct onyx_xml_parser {
	XML_Parser xp;
	dom_document *doc;
	onyx_xml_params params;

	dom_node **stack;		/* the open nodes (refs): [0] the document */
	int depth, cap;

	char *text;			/* the text gathered, not yet a node */
	size_t text_len, text_cap;
	bool in_cdata;

	char **nsdecl;			/* the namespace declarations for the next element: */
	int n_nsdecl, cap_nsdecl;	/* (prefix or "", uri) pairs */

	struct onyx_xml_stylesheet *sheets;
	int n_sheets;
	bool root_seen;

	uint8_t *queue;			/* bytes given while suspended */
	size_t queue_len, queue_cap;

	bool suspended;			/* expat suspended (a script waits) */
	bool want_final;		/* completed() was called */
	bool final_sent;		/* expat was given the end */
	bool done;			/* parse over */
	bool error;
	int err_line, err_col;
	char err_msg[160];
	char *encoding;
};

/* ---- the MIME types -------------------------------------------------------------------- */

int onyx_xml_kind_of_type(const char *mime)
{
	size_t n;

	if (mime == NULL)
		return 0;
	if (strcasecmp(mime, "application/xhtml+xml") == 0)
		return DOM_HTML_DOCUMENT_XHTML;
	if (strcasecmp(mime, "image/svg+xml") == 0 ||
	    strcasecmp(mime, "application/x-onyx-svg-document") == 0)
		return DOM_HTML_DOCUMENT_SVG;
	if (strcasecmp(mime, "text/xml") == 0 || strcasecmp(mime, "application/xml") == 0 ||
	    strcasecmp(mime, "text/xsl") == 0 || strcasecmp(mime, "application/xslt+xml") == 0)
		return DOM_HTML_DOCUMENT_XML;
	n = strlen(mime);
	if (n > 4 && strcasecmp(mime + n - 4, "+xml") == 0)
		return DOM_HTML_DOCUMENT_XML;
	return 0;
}

/* ---- small helpers --------------------------------------------------------------------- */

static dom_string *dstr(const char *s, size_t n)
{
	dom_string *d = NULL;

	if (dom_string_create_interned((const uint8_t *) s, n, &d) != DOM_NO_ERR)
		return NULL;
	return d;
}

static dom_string *dstr_plain(const char *s, size_t n)
{
	dom_string *d = NULL;

	if (dom_string_create((const uint8_t *) s, n, &d) != DOM_NO_ERR)
		return NULL;
	return d;
}

/** append child to parent (both kept by the caller) */
static void append(dom_node *parent, dom_node *child)
{
	dom_node *res = NULL;

	if (dom_node_append_child(parent, child, &res) == DOM_NO_ERR && res != NULL)
		dom_node_unref(res);
}

static dom_node *top(onyx_xml_parser *p)
{
	return p->stack[p->depth - 1];
}

/** the text gathered becomes a text node (or a CDATA section) */
static void flush_text(onyx_xml_parser *p, bool cdata)
{
	dom_string *s;
	dom_node *n = NULL;

	if (p->text_len == 0 && !cdata)
		return;
	if (p->depth <= 1) {	/* (outside the root: whitespace, never a node) */
		p->text_len = 0;
		return;
	}
	s = dstr_plain(p->text, p->text_len);
	p->text_len = 0;
	if (s == NULL)
		return;
	if (cdata) {
		if (dom_document_create_cdata_section(p->doc, s,
				(dom_cdata_section **) &n) != DOM_NO_ERR)
			n = NULL;
	}
	if (n == NULL && dom_document_create_text_node(p->doc, s, (dom_text **) &n) != DOM_NO_ERR)
		n = NULL;
	dom_string_unref(s);
	if (n != NULL) {
		append(top(p), n);
		dom_node_unref(n);
	}
}

static void add_text(onyx_xml_parser *p, const char *s, size_t n)
{
	if (p->text_len + n > p->text_cap) {
		size_t cap = p->text_cap ? p->text_cap : 256;
		char *t;
		while (cap < p->text_len + n)
			cap *= 2;
		t = realloc(p->text, cap);
		if (t == NULL)
			return;
		p->text = t;
		p->text_cap = cap;
	}
	memcpy(p->text + p->text_len, s, n);
	p->text_len += n;
}

/** split expat's "uri\nlocal\nprefix" (or "local") */
static void split_name(const char *name, const char **uri, size_t *uri_len,
		const char **local, size_t *local_len, const char **prefix, size_t *prefix_len)
{
	const char *a = strchr(name, NS_SEP), *b;

	if (a == NULL) {
		*uri = NULL;
		*uri_len = 0;
		*local = name;
		*local_len = strlen(name);
		*prefix = NULL;
		*prefix_len = 0;
		return;
	}
	*uri = name;
	*uri_len = a - name;
	*local = a + 1;
	b = strchr(*local, NS_SEP);
	if (b == NULL) {
		*local_len = strlen(*local);
		*prefix = NULL;
		*prefix_len = 0;
	} else {
		*local_len = b - *local;
		*prefix = b + 1;
		*prefix_len = strlen(*prefix);
	}
}

/** the qualified name "prefix:local" (or "local") in buf */
static size_t qname(char *buf, size_t cap, const char *local, size_t ll, const char *prefix,
		size_t pl)
{
	if (prefix != NULL && pl + ll + 2 <= cap) {
		memcpy(buf, prefix, pl);
		buf[pl] = ':';
		memcpy(buf + pl + 1, local, ll);
		return pl + 1 + ll;
	}
	if (ll > cap)
		ll = cap;
	memcpy(buf, local, ll);
	return ll;
}

/* ---- expat's handlers ------------------------------------------------------------------ */

static void XMLCALL on_ns_start(void *ud, const XML_Char *prefix, const XML_Char *uri)
{
	onyx_xml_parser *p = ud;

	if (p->n_nsdecl + 2 > p->cap_nsdecl) {
		int cap = p->cap_nsdecl ? p->cap_nsdecl * 2 : 8;
		char **t = realloc(p->nsdecl, cap * sizeof(char *));
		if (t == NULL)
			return;
		p->nsdecl = t;
		p->cap_nsdecl = cap;
	}
	p->nsdecl[p->n_nsdecl++] = strdup(prefix != NULL ? prefix : "");
	p->nsdecl[p->n_nsdecl++] = strdup(uri != NULL ? uri : "");
}

static void set_attr(dom_element *el, const char *uri, size_t ul, const char *qn, size_t ql,
		const char *value)
{
	dom_string *v = dstr_plain(value, strlen(value));
	dom_string *n = dstr(qn, ql);

	if (v != NULL && n != NULL) {
		if (uri != NULL) {
			dom_string *ns = dstr(uri, ul);
			if (ns != NULL) {
				dom_element_set_attribute_ns(el, ns, n, v);
				dom_string_unref(ns);
			}
		} else {
			dom_element_set_attribute(el, n, v);
		}
	}
	if (v != NULL)
		dom_string_unref(v);
	if (n != NULL)
		dom_string_unref(n);
}

static void XMLCALL on_start(void *ud, const XML_Char *name, const XML_Char **atts)
{
	onyx_xml_parser *p = ud;
	const char *uri, *local, *prefix;
	size_t ul, ll, pl, qn;
	char buf[512];
	dom_string *ns = NULL, *q;
	dom_element *el = NULL;
	int i;

	flush_text(p, false);
	split_name(name, &uri, &ul, &local, &ll, &prefix, &pl);
	qn = qname(buf, sizeof(buf), local, ll, prefix, pl);
	q = dstr(buf, qn);
	if (uri != NULL && ul > 0)
		ns = dstr(uri, ul);
	if (q != NULL)
		dom_document_create_element_ns(p->doc, ns, q, &el);
	if (ns != NULL)
		dom_string_unref(ns);
	if (q != NULL)
		dom_string_unref(q);
	if (el == NULL) {
		/* (out of memory: the rest is lost) */
		for (i = 0; i < p->n_nsdecl; i++)
			free(p->nsdecl[i]);
		p->n_nsdecl = 0;
		XML_StopParser(p->xp, XML_FALSE);
		return;
	}

	/* the namespace declarations: xmlns / xmlns:prefix attributes */
	for (i = 0; i + 1 < p->n_nsdecl; i += 2) {
		size_t n;
		if (p->nsdecl[i][0] == '\0') {
			set_attr(el, NS_XMLNS, sizeof(NS_XMLNS) - 1, "xmlns", 5, p->nsdecl[i + 1]);
		} else {
			n = snprintf(buf, sizeof(buf), "xmlns:%s", p->nsdecl[i]);
			if (n < sizeof(buf))
				set_attr(el, NS_XMLNS, sizeof(NS_XMLNS) - 1, buf, n,
						p->nsdecl[i + 1]);
		}
		free(p->nsdecl[i]);
		free(p->nsdecl[i + 1]);
	}
	p->n_nsdecl = 0;

	for (i = 0; atts[i] != NULL; i += 2) {
		split_name(atts[i], &uri, &ul, &local, &ll, &prefix, &pl);
		qn = qname(buf, sizeof(buf), local, ll, prefix, pl);
		set_attr(el, uri, ul, buf, qn, atts[i + 1]);
	}

	append(top(p), (dom_node *) el);
	p->root_seen = true;
	if (p->depth == p->cap) {
		int cap = p->cap * 2;
		dom_node **t = realloc(p->stack, cap * sizeof(dom_node *));
		if (t == NULL) {
			dom_node_unref(el);
			XML_StopParser(p->xp, XML_FALSE);
			return;
		}
		p->stack = t;
		p->cap = cap;
	}
	p->stack[p->depth++] = (dom_node *) el;	/* (its ref) */
}

static void XMLCALL on_end(void *ud, const XML_Char *name)
{
	onyx_xml_parser *p = ud;
	const char *uri, *local, *prefix;
	size_t ul, ll, pl;
	dom_node *el;

	flush_text(p, false);
	if (p->depth <= 1)
		return;
	el = p->stack[--p->depth];
	split_name(name, &uri, &ul, &local, &ll, &prefix, &pl);
	if (ll == 6 && memcmp(local, "script", 6) == 0 && uri != NULL &&
	    ((ul == sizeof(NS_XHTML) - 1 && memcmp(uri, NS_XHTML, ul) == 0) ||
	     (ul == sizeof(NS_SVG) - 1 && memcmp(uri, NS_SVG, ul) == 0)) &&
	    p->params.enable_script && p->params.script != NULL) {
		dom_hubbub_error e = p->params.script(p->params.ctx, el);
		if (e == DOM_HUBBUB_HUBBUB_ERR_PAUSED) {
			/* (a script fetched: the parse waits for it) */
			p->suspended = true;
			XML_StopParser(p->xp, XML_TRUE);
		}
	}
	dom_node_unref(el);
}

static void XMLCALL on_text(void *ud, const XML_Char *s, int len)
{
	add_text(ud, s, len);
}

static void XMLCALL on_cdata_start(void *ud)
{
	onyx_xml_parser *p = ud;

	flush_text(p, false);
	p->in_cdata = true;
}

static void XMLCALL on_cdata_end(void *ud)
{
	onyx_xml_parser *p = ud;

	flush_text(p, true);
	p->in_cdata = false;
}

static void XMLCALL on_comment(void *ud, const XML_Char *data)
{
	onyx_xml_parser *p = ud;
	dom_string *s;
	dom_comment *c = NULL;

	flush_text(p, false);
	s = dstr_plain(data, strlen(data));
	if (s == NULL)
		return;
	if (dom_document_create_comment(p->doc, s, &c) == DOM_NO_ERR && c != NULL) {
		append(top(p), (dom_node *) c);
		dom_node_unref(c);
	}
	dom_string_unref(s);
}

/** a pseudo-attribute of a processing instruction's data (name="value"), malloc'd */
static char *pi_attr(const char *data, const char *name)
{
	size_t n = strlen(name);
	const char *s = data;

	while ((s = strstr(s, name)) != NULL) {
		const char *v = s + n;
		bool at_start = s == data || s[-1] == ' ' || s[-1] == '\t' || s[-1] == '\n' ||
				s[-1] == '\r';
		while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r')
			v++;
		if (at_start && *v == '=') {
			char q;
			const char *e;
			v++;
			while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r')
				v++;
			q = *v;
			if (q != '"' && q != '\'')
				return NULL;
			e = strchr(v + 1, q);
			if (e == NULL)
				return NULL;
			{
				char *r = malloc(e - v);
				size_t i, j;
				if (r == NULL)
					return NULL;
				/* (the entities a pseudo-attribute may hold) */
				for (i = 1, j = 0; v + i < e; i++) {
					if (strncmp(v + i, "&amp;", 5) == 0) { r[j++] = '&'; i += 4; }
					else if (strncmp(v + i, "&lt;", 4) == 0) { r[j++] = '<'; i += 3; }
					else if (strncmp(v + i, "&gt;", 4) == 0) { r[j++] = '>'; i += 3; }
					else if (strncmp(v + i, "&quot;", 6) == 0) { r[j++] = '"'; i += 5; }
					else if (strncmp(v + i, "&apos;", 6) == 0) { r[j++] = '\''; i += 5; }
					else r[j++] = v[i];
				}
				r[j] = '\0';
				return r;
			}
		}
		s += n;
	}
	return NULL;
}

static void XMLCALL on_pi(void *ud, const XML_Char *target, const XML_Char *data)
{
	onyx_xml_parser *p = ud;
	dom_string *t, *d;
	dom_processing_instruction *pi = NULL;

	flush_text(p, false);
	t = dstr(target, strlen(target));
	d = dstr_plain(data, strlen(data));
	if (t != NULL && d != NULL &&
	    dom_document_create_processing_instruction(p->doc, t, d, &pi) == DOM_NO_ERR &&
	    pi != NULL) {
		append(top(p), (dom_node *) pi);
		/* an xml-stylesheet of the prolog */
		if (!p->root_seen && strcmp(target, "xml-stylesheet") == 0) {
			struct onyx_xml_stylesheet s, *ns;
			char *alt;
			memset(&s, 0, sizeof(s));
			s.href = pi_attr(data, "href");
			s.type = pi_attr(data, "type");
			s.media = pi_attr(data, "media");
			alt = pi_attr(data, "alternate");
			s.alternate = alt != NULL && strcmp(alt, "yes") == 0;
			free(alt);
			if (s.type != NULL) {
				char *c;
				for (c = s.type; *c; c++)
					if (*c >= 'A' && *c <= 'Z')
						*c += 'a' - 'A';
			}
			ns = s.href != NULL ? realloc(p->sheets, (p->n_sheets + 1) * sizeof(s)) : NULL;
			if (ns != NULL) {
				s.pi = dom_node_ref(pi);
				p->sheets = ns;
				p->sheets[p->n_sheets++] = s;
				if (p->params.stylesheet != NULL)
					p->params.stylesheet(p->params.ctx, &s);
			} else {
				free(s.href);
				free(s.type);
				free(s.media);
			}
		}
		dom_node_unref(pi);
	}
	if (t != NULL)
		dom_string_unref(t);
	if (d != NULL)
		dom_string_unref(d);
}

static void XMLCALL on_doctype(void *ud, const XML_Char *name, const XML_Char *sysid,
		const XML_Char *pubid, int has_internal_subset)
{
	onyx_xml_parser *p = ud;
	struct dom_document_type *dt = NULL;

	(void) has_internal_subset;
	flush_text(p, false);
	if (dom_implementation_create_document_type(name, pubid != NULL ? pubid : "",
			sysid != NULL ? sysid : "", &dt) == DOM_NO_ERR && dt != NULL) {
		append(top(p), (dom_node *) dt);
		dom_node_unref(dt);
	}
}

/** an entity of an external DTD expat does not read: HTML's named references */
static void XMLCALL on_skipped(void *ud, const XML_Char *name, int is_param)
{
	onyx_xml_parser *p = ud;
	size_t n = strlen(name), lo = 0, hi = ONYX_ENTITY_COUNT;
	char key[ONYX_ENTITY_MAXLEN + 2];

	if (is_param || n + 1 > ONYX_ENTITY_MAXLEN)
		return;
	memcpy(key, name, n);
	key[n++] = ';';
	while (lo < hi) {
		size_t mid = (lo + hi) / 2, m;
		const struct onyx_entity *e = &onyx_entities[mid];
		int r;
		m = e->len < n ? e->len : n;
		r = memcmp(onyx_entity_names + e->off, key, m);
		if (r == 0)
			r = (int) e->len - (int) n;
		if (r == 0) {
			uint32_t cps[2] = { e->cp1, e->cp2 };
			int k;
			for (k = 0; k < 2 && cps[k] != 0; k++) {
				char u[4];
				uint32_t c = cps[k];
				size_t l;
				if (c < 0x80) { u[0] = c; l = 1; }
				else if (c < 0x800) { u[0] = 0xC0 | (c >> 6); u[1] = 0x80 | (c & 0x3F); l = 2; }
				else if (c < 0x10000) { u[0] = 0xE0 | (c >> 12); u[1] = 0x80 | ((c >> 6) & 0x3F);
					u[2] = 0x80 | (c & 0x3F); l = 3; }
				else { u[0] = 0xF0 | (c >> 18); u[1] = 0x80 | ((c >> 12) & 0x3F);
					u[2] = 0x80 | ((c >> 6) & 0x3F); u[3] = 0x80 | (c & 0x3F); l = 4; }
				add_text(p, u, l);
			}
			return;
		}
		if (r < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
}

/** an encoding expat does not know: a single-byte one of libparserutils */
static int XMLCALL on_unknown_encoding(void *data, const XML_Char *name, XML_Encoding *info)
{
	parserutils_charset_codec *codec = NULL;
	int i;

	(void) data;
	if (parserutils_charset_codec_create(name, &codec) != PARSERUTILS_OK)
		return XML_STATUS_ERROR;
	for (i = 0; i < 256; i++) {
		uint8_t in[1] = { (uint8_t) i };
		const uint8_t *src = in;
		size_t srclen = 1;
		uint8_t out[16];
		uint8_t *dst = out;
		size_t dstlen = sizeof(out);
		parserutils_charset_codec_reset(codec);
		if (parserutils_charset_codec_decode(codec, &src, &srclen, &dst, &dstlen) !=
				PARSERUTILS_OK || srclen != 0 || dst == out) {
			info->map[i] = i < 0x80 ? i : 0xFFFD;
			continue;
		}
		/* (UTF-8 out: one code point) */
		{
			size_t n = dst - out;
			uint32_t c = out[0];
			if (n == 2) c = ((out[0] & 0x1F) << 6) | (out[1] & 0x3F);
			else if (n == 3) c = ((out[0] & 0x0F) << 12) | ((out[1] & 0x3F) << 6) | (out[2] & 0x3F);
			else if (n == 4) c = ((out[0] & 0x07) << 18) | ((out[1] & 0x3F) << 12) |
					((out[2] & 0x3F) << 6) | (out[3] & 0x3F);
			info->map[i] = (int) c;
		}
	}
	parserutils_charset_codec_destroy(codec);
	info->data = NULL;
	info->convert = NULL;
	info->release = NULL;
	return XML_STATUS_OK;
}

static void XMLCALL on_xmldecl(void *ud, const XML_Char *version, const XML_Char *encoding,
		int standalone)
{
	onyx_xml_parser *p = ud;

	(void) version;
	(void) standalone;
	if (p->encoding == NULL && encoding != NULL)
		p->encoding = strdup(encoding);
}

/* ---- the parser -------------------------------------------------------------------------- */

nserror onyx_xml_parser_create(const onyx_xml_params *params, onyx_xml_parser **parser,
		struct dom_document **doc)
{
	onyx_xml_parser *p = calloc(1, sizeof(*p));
	dom_string *idname;

	if (p == NULL)
		return NSERROR_NOMEM;
	p->params = *params;
	p->cap = 64;
	p->stack = malloc(p->cap * sizeof(dom_node *));
	if (p->stack == NULL ||
	    dom_implementation_create_document(DOM_IMPLEMENTATION_HTML, NULL, NULL, NULL,
			params->daf, params->ctx, &p->doc) != DOM_NO_ERR || p->doc == NULL) {
		free(p->stack);
		free(p);
		return NSERROR_NOMEM;
	}
	dom_html_document_set_xml_kind((dom_html_document *) p->doc,
			params->kind != 0 ? params->kind : DOM_HTML_DOCUMENT_XML);
	idname = dstr("id", 2);
	if (idname != NULL) {
		_dom_document_set_id_name(p->doc, idname);
		dom_string_unref(idname);
	}
	if (params->enc != NULL)
		p->encoding = strdup(params->enc);

	p->xp = XML_ParserCreateNS(params->enc, NS_SEP);
	if (p->xp == NULL) {
		dom_node_unref(p->doc);
		free(p->stack);
		free(p->encoding);
		free(p);
		return NSERROR_NOMEM;
	}
	{	/* (the hash salt: the Pi has no entropy source expat knows) */
		static unsigned long n;
		XML_SetHashSalt(p->xp, (unsigned long) time(NULL) ^ ((unsigned long) (uintptr_t) p << 3) ^
				(++n * 2654435761UL));
	}
	XML_SetReturnNSTriplet(p->xp, XML_TRUE);
	XML_SetUserData(p->xp, p);
	XML_SetElementHandler(p->xp, on_start, on_end);
	XML_SetCharacterDataHandler(p->xp, on_text);
	XML_SetCdataSectionHandler(p->xp, on_cdata_start, on_cdata_end);
	XML_SetCommentHandler(p->xp, on_comment);
	XML_SetProcessingInstructionHandler(p->xp, on_pi);
	XML_SetStartNamespaceDeclHandler(p->xp, on_ns_start);
	XML_SetStartDoctypeDeclHandler(p->xp, on_doctype);
	XML_SetSkippedEntityHandler(p->xp, on_skipped);
	XML_SetUnknownEncodingHandler(p->xp, on_unknown_encoding, NULL);
	XML_SetXmlDeclHandler(p->xp, on_xmldecl);

	p->stack[0] = dom_node_ref(p->doc);
	p->depth = 1;
	*parser = p;
	*doc = (dom_document *) dom_node_ref(p->doc);
	return NSERROR_OK;
}

/** after XML_Parse / XML_ResumeParser */
static void parsed(onyx_xml_parser *p, enum XML_Status st)
{
	if (st == XML_STATUS_SUSPENDED) {
		p->suspended = true;
		return;
	}
	p->suspended = false;
	if (st == XML_STATUS_ERROR) {
		enum XML_Error e = XML_GetErrorCode(p->xp);
		if (e == XML_ERROR_ABORTED && p->error)
			return;
		p->error = true;
		p->done = true;
		p->err_line = (int) XML_GetCurrentLineNumber(p->xp);
		p->err_col = (int) XML_GetCurrentColumnNumber(p->xp) + 1;
		snprintf(p->err_msg, sizeof(p->err_msg), "%s", XML_ErrorString(e));
		flush_text(p, false);
		NSLOG(netsurf, INFO, "XML error line %d column %d: %s", p->err_line,
				p->err_col, p->err_msg);
		return;
	}
	if (p->final_sent) {
		flush_text(p, false);
		p->done = true;
	}
}

/** go on while not suspended: the queued bytes, then the end */
static void pump(onyx_xml_parser *p)
{
	while (!p->suspended && !p->done && p->queue_len > 0) {
		/* (expat copies what it does not parse: the queue is free at once) */
		uint8_t *q = p->queue;
		size_t n = p->queue_len;
		p->queue = NULL;
		p->queue_len = p->queue_cap = 0;
		parsed(p, XML_Parse(p->xp, (const char *) q, (int) n, XML_FALSE));
		free(q);
	}
	if (!p->suspended && !p->done && p->want_final && !p->final_sent) {
		p->final_sent = true;
		parsed(p, XML_Parse(p->xp, NULL, 0, XML_TRUE));
	}
}

dom_hubbub_error onyx_xml_parser_parse_chunk(onyx_xml_parser *p, const uint8_t *data,
		size_t len)
{
	if (p->done || len == 0)
		return DOM_HUBBUB_OK;
	if (p->suspended || p->queue_len > 0) {
		if (p->queue_len + len > p->queue_cap) {
			size_t cap = p->queue_cap ? p->queue_cap : 4096;
			uint8_t *q;
			while (cap < p->queue_len + len)
				cap *= 2;
			q = realloc(p->queue, cap);
			if (q == NULL)
				return DOM_HUBBUB_NOMEM;
			p->queue = q;
			p->queue_cap = cap;
		}
		memcpy(p->queue + p->queue_len, data, len);
		p->queue_len += len;
		if (!p->suspended)
			pump(p);
		return DOM_HUBBUB_OK;
	}
	/* (in pieces: expat takes an int) */
	while (len > 0 && !p->suspended && !p->done) {
		size_t n = len > (1u << 30) ? (1u << 30) : len;
		parsed(p, XML_Parse(p->xp, (const char *) data, (int) n, XML_FALSE));
		data += n;
		len -= n;
	}
	if (len > 0 && p->suspended)
		return onyx_xml_parser_parse_chunk(p, data, len);	/* (queued) */
	return DOM_HUBBUB_OK;
}

dom_hubbub_error onyx_xml_parser_completed(onyx_xml_parser *p)
{
	p->want_final = true;
	if (!p->suspended)
		pump(p);
	return p->suspended ? DOM_HUBBUB_HUBBUB_ERR_PAUSED : DOM_HUBBUB_OK;
}

void onyx_xml_parser_resume(onyx_xml_parser *p)
{
	if (!p->suspended || p->done)
		return;
	p->suspended = false;
	parsed(p, XML_ResumeParser(p->xp));
	pump(p);
}

bool onyx_xml_parser_done(onyx_xml_parser *p)
{
	return p->done;
}

bool onyx_xml_parser_error(onyx_xml_parser *p, int *line, int *col, const char **msg)
{
	if (!p->error)
		return false;
	*line = p->err_line;
	*col = p->err_col;
	*msg = p->err_msg;
	return true;
}

const struct onyx_xml_stylesheet *onyx_xml_parser_stylesheets(onyx_xml_parser *p, int *n)
{
	*n = p->n_sheets;
	return p->sheets;
}

const char *onyx_xml_parser_encoding(onyx_xml_parser *p)
{
	return p->encoding != NULL ? p->encoding : "UTF-8";
}

void onyx_xml_parser_destroy(onyx_xml_parser *p)
{
	int i;

	if (p == NULL)
		return;
	XML_ParserFree(p->xp);
	for (i = 0; i < p->depth; i++)
		dom_node_unref(p->stack[i]);
	for (i = 0; i < p->n_nsdecl; i++)
		free(p->nsdecl[i]);
	for (i = 0; i < p->n_sheets; i++) {
		free(p->sheets[i].href);
		free(p->sheets[i].type);
		free(p->sheets[i].media);
		dom_node_unref(p->sheets[i].pi);
	}
	free(p->sheets);
	free(p->nsdecl);
	free(p->stack);
	free(p->text);
	free(p->queue);
	free(p->encoding);
	dom_node_unref(p->doc);
	free(p);
}

/* ---- the error box ------------------------------------------------------------------------ */

static dom_element *xhtml_el(dom_document *doc, const char *name)
{
	dom_string *ns = dstr(NS_XHTML, sizeof(NS_XHTML) - 1), *n = dstr(name, strlen(name));
	dom_element *el = NULL;

	if (ns != NULL && n != NULL)
		dom_document_create_element_ns(doc, ns, n, &el);
	if (ns != NULL)
		dom_string_unref(ns);
	if (n != NULL)
		dom_string_unref(n);
	return el;
}

static void el_text(dom_document *doc, dom_element *el, const char *text)
{
	dom_string *s = dstr_plain(text, strlen(text));
	dom_text *t = NULL;

	if (s != NULL && dom_document_create_text_node(doc, s, &t) == DOM_NO_ERR && t != NULL) {
		append((dom_node *) el, (dom_node *) t);
		dom_node_unref(t);
	}
	if (s != NULL)
		dom_string_unref(s);
}

static void el_attr(dom_element *el, const char *name, const char *value)
{
	set_attr(el, NULL, 0, name, strlen(name), value);
}

void onyx_xml_error_banner(struct dom_document *doc, int line, int col, const char *msg)
{
	dom_element *pe = xhtml_el(doc, "parsererror"), *h, *d;
	dom_element *root = NULL, *where = NULL;
	char buf[256];

	if (pe == NULL)
		return;
	el_attr(pe, "style", "display: block; white-space: pre; border: 2px solid #c77; "
			"padding: 0 1em 0 1em; margin: 1em; background-color: #fdd; color: black");
	if ((h = xhtml_el(doc, "h3")) != NULL) {
		el_text(doc, h, "This page contains the following errors:");
		append((dom_node *) pe, (dom_node *) h);
		dom_node_unref(h);
	}
	if ((d = xhtml_el(doc, "div")) != NULL) {
		el_attr(d, "style", "font-family:monospace;font-size:12px");
		snprintf(buf, sizeof(buf), "error on line %d at column %d: %s\n", line, col, msg);
		el_text(doc, d, buf);
		append((dom_node *) pe, (dom_node *) d);
		dom_node_unref(d);
	}
	if ((h = xhtml_el(doc, "h3")) != NULL) {
		el_text(doc, h, "Below is a rendering of the page up to the first error.");
		append((dom_node *) pe, (dom_node *) h);
		dom_node_unref(h);
	}
	dom_document_get_document_element(doc, &root);
	if (root == NULL) {
		append((dom_node *) doc, (dom_node *) pe);
	} else {
		dom_node *first = NULL, *res = NULL;
		dom_html_element_type type = DOM_HTML_ELEMENT_TYPE__UNKNOWN;
		/* (XHTML: first in <body>, when there is one) */
		if (onyx_xml_root_ns(doc) == 1) {
			dom_node *c = NULL, *next;
			dom_node_get_first_child(root, &c);
			while (c != NULL) {
				dom_node_type nt;
				if (dom_node_get_node_type(c, &nt) == DOM_NO_ERR &&
				    nt == DOM_ELEMENT_NODE &&
				    dom_html_element_get_tag_type(c, &type) == DOM_NO_ERR &&
				    type == DOM_HTML_ELEMENT_TYPE_BODY) {
					where = (dom_element *) c;
					break;
				}
				dom_node_get_next_sibling(c, &next);
				dom_node_unref(c);
				c = next;
			}
		}
		if (where == NULL)
			where = (dom_element *) dom_node_ref(root);
		dom_node_get_first_child(where, &first);
		if (dom_node_insert_before(where, pe, first, &res) == DOM_NO_ERR && res != NULL)
			dom_node_unref(res);
		if (first != NULL)
			dom_node_unref(first);
		dom_node_unref(where);
		dom_node_unref(root);
	}
	dom_node_unref(pe);
}

/* ---- a document at once ----------------------------------------------------------------- */

nserror onyx_xml_parse_document(const char *data, size_t len, int kind,
		struct dom_document **doc, bool *error)
{
	onyx_xml_params params;
	onyx_xml_parser *p = NULL;
	int line, col;
	const char *msg;
	nserror e;

	memset(&params, 0, sizeof(params));
	params.kind = kind;
	e = onyx_xml_parser_create(&params, &p, doc);
	if (e != NSERROR_OK)
		return e;
	onyx_xml_parser_parse_chunk(p, (const uint8_t *) data, len);
	onyx_xml_parser_completed(p);
	*error = false;
	if (onyx_xml_parser_error(p, &line, &col, &msg)) {
		*error = true;
	} else {
		dom_element *root = NULL;
		dom_document_get_document_element(*doc, &root);
		if (root == NULL) {	/* (no root element: an error too) */
			*error = true;
			line = col = 1;
			msg = "Document is empty";
		} else {
			dom_node_unref(root);
		}
	}
	if (*error) {
		/* (DOMParser's error document, as the DOM Parsing standard and Firefox: emptied,
		 * its root a <parsererror> -- Chrome's box is for a page shown) */
		dom_node *c = NULL, *res = NULL;
		dom_string *ns = dstr(NS_PARSERERROR, sizeof(NS_PARSERERROR) - 1);
		dom_string *nm = dstr("parsererror", 11);
		dom_element *pe = NULL;
		char buf[256];
		while (dom_node_get_first_child(*doc, &c) == DOM_NO_ERR && c != NULL) {
			if (dom_node_remove_child(*doc, c, &res) == DOM_NO_ERR && res != NULL)
				dom_node_unref(res);
			dom_node_unref(c);
			c = NULL;
		}
		if (ns != NULL && nm != NULL &&
		    dom_document_create_element_ns(*doc, ns, nm, &pe) == DOM_NO_ERR && pe != NULL) {
			snprintf(buf, sizeof(buf), "XML Parsing Error: %s\nLine Number %d, Column %d:",
					msg, line, col);
			el_text(*doc, pe, buf);
			append((dom_node *) *doc, (dom_node *) pe);
			dom_node_unref(pe);
		}
		if (ns != NULL)
			dom_string_unref(ns);
		if (nm != NULL)
			dom_string_unref(nm);
	}
	onyx_xml_parser_destroy(p);
	return NSERROR_OK;
}

int onyx_xml_root_ns(struct dom_document *doc)
{
	dom_element *root = NULL;
	dom_string *ns = NULL;
	int r = 0;

	if (doc == NULL || dom_document_get_document_element(doc, &root) != DOM_NO_ERR ||
	    root == NULL)
		return 0;
	if (dom_node_get_namespace(root, &ns) == DOM_NO_ERR && ns != NULL) {
		if (dom_string_byte_length(ns) == sizeof(NS_XHTML) - 1 &&
		    memcmp(dom_string_data(ns), NS_XHTML, sizeof(NS_XHTML) - 1) == 0)
			r = 1;
		else if (dom_string_byte_length(ns) == sizeof(NS_SVG) - 1 &&
			 memcmp(dom_string_data(ns), NS_SVG, sizeof(NS_SVG) - 1) == 0)
			r = 2;
		dom_string_unref(ns);
	}
	dom_node_unref(root);
	return r;
}

/* ---- the tree view ------------------------------------------------------------------------ */

struct buf {
	char *s;
	size_t len, cap;
	bool fail;
};

static void put(struct buf *b, const char *s, size_t n)
{
	if (b->fail)
		return;
	if (b->len + n + 1 > b->cap) {
		size_t cap = b->cap ? b->cap : 4096;
		char *t;
		while (cap < b->len + n + 1)
			cap *= 2;
		t = realloc(b->s, cap);
		if (t == NULL) {
			b->fail = true;
			return;
		}
		b->s = t;
		b->cap = cap;
	}
	memcpy(b->s + b->len, s, n);
	b->len += n;
	b->s[b->len] = '\0';
}

static void puts_(struct buf *b, const char *s)
{
	put(b, s, strlen(s));
}

/** text escaped for HTML */
static void put_esc(struct buf *b, const char *s, size_t n)
{
	size_t i, from = 0;

	for (i = 0; i < n; i++) {
		const char *r = NULL;
		switch (s[i]) {
		case '&': r = "&amp;"; break;
		case '<': r = "&lt;"; break;
		case '>': r = "&gt;"; break;
		case '"': r = "&quot;"; break;
		default: break;
		}
		if (r != NULL) {
			put(b, s + from, i - from);
			puts_(b, r);
			from = i + 1;
		}
	}
	put(b, s + from, n - from);
}

static void put_dstr(struct buf *b, dom_string *s)
{
	if (s != NULL)
		put_esc(b, dom_string_data(s), dom_string_byte_length(s));
}

/** whitespace only */
static bool blank(dom_string *s)
{
	const char *d = s != NULL ? dom_string_data(s) : "";
	size_t i, n = s != NULL ? dom_string_byte_length(s) : 0;

	for (i = 0; i < n; i++)
		if (d[i] != ' ' && d[i] != '\t' && d[i] != '\n' && d[i] != '\r')
			return false;
	return true;
}

/** the text trimmed */
static void put_trimmed(struct buf *b, dom_string *s)
{
	const char *d = dom_string_data(s);
	size_t a = 0, e = dom_string_byte_length(s);

	while (a < e && (d[a] == ' ' || d[a] == '\t' || d[a] == '\n' || d[a] == '\r'))
		a++;
	while (e > a && (d[e - 1] == ' ' || d[e - 1] == '\t' || d[e - 1] == '\n' ||
			 d[e - 1] == '\r'))
		e--;
	put_esc(b, d + a, e - a);
}

static void put_start_tag(struct buf *b, dom_node *el, bool empty)
{
	dom_string *name = NULL;
	dom_namednodemap *attrs = NULL;
	uint32_t i, n = 0;

	dom_node_get_node_name(el, &name);
	puts_(b, "<span class=\"t\">&lt;");
	put_dstr(b, name);
	if (dom_node_get_attributes(el, &attrs) == DOM_NO_ERR && attrs != NULL) {
		dom_namednodemap_get_length(attrs, &n);
		for (i = 0; i < n; i++) {
			dom_node *a = NULL;
			dom_string *an = NULL, *av = NULL;
			if (dom_namednodemap_item(attrs, i, &a) != DOM_NO_ERR || a == NULL)
				continue;
			dom_node_get_node_name(a, &an);
			dom_node_get_node_value(a, &av);
			puts_(b, " <span class=\"an\">");
			{	/* (the qualified name: prefix:local) */
				dom_string *pre = NULL, *loc = NULL;
				dom_node_get_prefix(a, &pre);
				dom_node_get_local_name(a, &loc);
				if (pre != NULL && loc != NULL) {
					put_dstr(b, pre);
					puts_(b, ":");
					put_dstr(b, loc);
				} else {
					put_dstr(b, an);
				}
				if (pre != NULL)
					dom_string_unref(pre);
				if (loc != NULL)
					dom_string_unref(loc);
			}
			puts_(b, "</span>=\"<span class=\"av\">");
			put_dstr(b, av);
			puts_(b, "</span>\"");
			if (an != NULL)
				dom_string_unref(an);
			if (av != NULL)
				dom_string_unref(av);
			dom_node_unref(a);
		}
		dom_namednodemap_unref(attrs);
	}
	puts_(b, empty ? "/&gt;</span>" : "&gt;</span>");
	if (name != NULL)
		dom_string_unref(name);
}

static void put_end_tag(struct buf *b, dom_node *el)
{
	dom_string *name = NULL;

	dom_node_get_node_name(el, &name);
	puts_(b, "<span class=\"t\">&lt;/");
	put_dstr(b, name);
	puts_(b, "&gt;</span>");
	if (name != NULL)
		dom_string_unref(name);
}

/** a leaf node (not an element) on its line; false: nothing to show */
static bool put_leaf(struct buf *b, dom_node *n, dom_node_type t)
{
	dom_string *v = NULL, *name = NULL;
	bool shown = true;

	dom_node_get_node_value(n, &v);
	switch (t) {
	case DOM_TEXT_NODE:
		if (blank(v)) {
			shown = false;
			break;
		}
		puts_(b, "<div class=\"l\">");
		put_trimmed(b, v);
		puts_(b, "</div>");
		break;
	case DOM_CDATA_SECTION_NODE:
		puts_(b, "<div class=\"l\">&lt;![CDATA[");
		put_dstr(b, v);
		puts_(b, "]]&gt;</div>");
		break;
	case DOM_COMMENT_NODE:
		puts_(b, "<div class=\"l c\">&lt;!--");
		put_dstr(b, v);
		puts_(b, "--&gt;</div>");
		break;
	case DOM_PROCESSING_INSTRUCTION_NODE:
		dom_node_get_node_name(n, &name);
		puts_(b, "<div class=\"l c\">&lt;?");
		put_dstr(b, name);
		puts_(b, " ");
		put_dstr(b, v);
		puts_(b, "?&gt;</div>");
		break;
	default:
		shown = false;
		break;
	}
	if (v != NULL)
		dom_string_unref(v);
	if (name != NULL)
		dom_string_unref(name);
	return shown;
}

/** an element's children: none, one short text (on its line), or others */
static int child_shape(dom_node *el, dom_string **only_text)
{
	dom_node *c = NULL, *next;
	int n = 0, shape = 0;

	*only_text = NULL;
	dom_node_get_first_child(el, &c);
	while (c != NULL) {
		dom_node_type t;
		dom_node_get_node_type(c, &t);
		if (t == DOM_TEXT_NODE) {
			dom_string *v = NULL;
			dom_node_get_node_value(c, &v);
			if (!blank(v)) {
				n++;
				if (n == 1 && dom_string_byte_length(v) < 200 &&
				    memchr(dom_string_data(v), '\n', dom_string_byte_length(v)) == NULL)
					*only_text = dom_string_ref(v);
				else
					shape = 2;
			}
			if (v != NULL)
				dom_string_unref(v);
		} else {
			n++;
			shape = 2;
		}
		dom_node_get_next_sibling(c, &next);
		dom_node_unref(c);
		c = next;
	}
	if (shape == 2 || n > 1) {
		if (*only_text != NULL) {
			dom_string_unref(*only_text);
			*only_text = NULL;
		}
		return 2;
	}
	return n == 0 ? 0 : 1;
}

char *onyx_xml_tree_view(struct dom_document *doc, size_t *len, int err_line, int err_col,
		const char *err_msg)
{
	struct buf b = { NULL, 0, 0, false };
	/* the walk: the nodes whose children are being shown (refs) */
	dom_node **stack = NULL;
	int depth = 0, cap = 0;
	dom_node *n = NULL;

	puts_(&b, "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>"
		"body{margin:8px;font:13px monospace;color:#000;background:#fff}"
		".h{font:13px sans-serif;padding:0 0 .5em;margin-bottom:1em;border-bottom:1px solid #ccc}"
		".e{margin-left:1em}.l{white-space:pre-wrap;word-break:break-all}"
		".t{color:#881280}.an{color:#994500}.av{color:#1a1aa6}.c{color:#236e25}"
		"parsererror{display:block;white-space:pre;border:2px solid #c77;padding:0 1em;"
		"margin:1em 0;background-color:#fdd;color:#000;font:13px sans-serif}"
		"</style></head><body>");
	if (err_line > 0) {
		char eb[256];
		puts_(&b, "<parsererror><h3>This page contains the following errors:</h3>"
			"<div style=\"font-family:monospace;font-size:12px\">");
		snprintf(eb, sizeof(eb), "error on line %d at column %d: ", err_line, err_col);
		puts_(&b, eb);
		put_esc(&b, err_msg, strlen(err_msg));
		puts_(&b, "\n</div><h3>Below is a rendering of the page up to the first error."
			"</h3></parsererror>");
	}
	puts_(&b, "<div class=\"h\">This XML file does not appear to have any style information "
		"associated with it. The document tree is shown below.</div><div class=\"x\">");

	dom_node_get_first_child(doc, &n);
	for (;;) {
		while (n != NULL) {
			dom_node_type t;
			dom_node *next = NULL;
			dom_node_get_node_type(n, &t);
			if (t == DOM_ELEMENT_NODE) {
				dom_string *only = NULL;
				int shape = child_shape(n, &only);
				if (shape == 0) {
					puts_(&b, "<div class=\"l\">");
					put_start_tag(&b, n, true);
					puts_(&b, "</div>");
				} else if (shape == 1) {
					puts_(&b, "<div class=\"l\">");
					put_start_tag(&b, n, false);
					if (only != NULL)
						put_trimmed(&b, only);
					put_end_tag(&b, n);
					puts_(&b, "</div>");
				} else {
					/* (its children next, its end tag when they are done) */
					puts_(&b, "<div class=\"l\">");
					put_start_tag(&b, n, false);
					puts_(&b, "</div><div class=\"e\">");
					if (depth == cap) {
						int nc = cap ? cap * 2 : 64;
						dom_node **s = realloc(stack, nc * sizeof(dom_node *));
						if (s == NULL) {
							b.fail = true;
							dom_node_unref(n);
							n = NULL;
							break;
						}
						stack = s;
						cap = nc;
					}
					stack[depth++] = n;	/* (its ref) */
					dom_node_get_first_child(n, &next);
					n = next;
					if (only != NULL)
						dom_string_unref(only);
					continue;
				}
				if (only != NULL)
					dom_string_unref(only);
			} else {
				put_leaf(&b, n, t);
			}
			dom_node_get_next_sibling(n, &next);
			dom_node_unref(n);
			n = next;
		}
		if (depth == 0)
			break;
		n = stack[--depth];
		puts_(&b, "</div><div class=\"l\">");
		put_end_tag(&b, n);
		puts_(&b, "</div>");
		{
			dom_node *next = NULL;
			dom_node_get_next_sibling(n, &next);
			dom_node_unref(n);
			n = next;
		}
	}
	while (depth > 0)
		dom_node_unref(stack[--depth]);
	free(stack);
	puts_(&b, "</div></body></html>");
	if (b.fail) {
		free(b.s);
		return NULL;
	}
	*len = b.len;
	return b.s;
}
