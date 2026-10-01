/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 */

/**
 * \file
 * Onyx: XML documents (docs/06 §43) -- expat's parse into a libdom document, as the HTML
 * parser's binding (libdom's hubbub binding) does for HTML: the document is an HTML
 * document underneath (NetSurf lays it out as one) of an XML kind
 * (dom_html_document_set_xml_kind: names keep their case, elements in no namespace are
 * none of HTML's), its XHTML elements HTML elements; scripts (XHTML's, SVG's) run at their
 * end tag, the parse suspended while a script is fetched; the xml-stylesheet processing
 * instructions of the prolog kept; a well-formedness error stops the parse, its line,
 * column and message kept (onyx_xml_error_banner: Chrome's "This page contains the
 * following errors" box).
 *
 * Also here: the document tree view of an XML file with no style information
 * (onyx_xml_tree_view), an XML document parsed at once (DOMParser, responseXML, XSLT's
 * style sheets: onyx_xml_parse_document) and the XML MIME types (onyx_xml_kind_of_type).
 */

#ifndef NETSURF_HTML_ONYX_XML_H
#define NETSURF_HTML_ONYX_XML_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <dom/dom.h>
#include <dom/bindings/hubbub/parser.h>

typedef struct onyx_xml_parser onyx_xml_parser;

/** an xml-stylesheet processing instruction of the prolog */
struct onyx_xml_stylesheet {
	char *href;
	char *type;		/**< lower case, or NULL */
	char *media;		/**< or NULL */
	bool alternate;
	struct dom_node *pi;	/**< the processing instruction node (a ref) */
};

typedef struct onyx_xml_params {
	const char *enc;	/**< the transport's charset, or NULL (the document's own) */
	bool enable_script;
	/** a script element's end (XHTML's or SVG's <script>): NetSurf's html_process_script */
	dom_hubbub_error (*script)(void *ctx, struct dom_node *node);
	/** an xml-stylesheet processing instruction of the prolog (may be NULL) */
	void (*stylesheet)(void *ctx, const struct onyx_xml_stylesheet *s);
	void *ctx;
	dom_events_default_action_fetcher daf;
	int kind;		/**< the new document's dom_html_document_xml_kind */
} onyx_xml_params;

/** the XML kind of a MIME type: 0 not XML, else a dom_html_document_xml_kind */
int onyx_xml_kind_of_type(const char *mime);

/** a parser and its new document */
nserror onyx_xml_parser_create(const onyx_xml_params *params, onyx_xml_parser **parser,
		struct dom_document **doc);

/** some of the document's bytes (queued while the parse is suspended for a script) */
dom_hubbub_error onyx_xml_parser_parse_chunk(onyx_xml_parser *p, const uint8_t *data,
		size_t len);

/** the end of the document's bytes: DOM_HUBBUB_HUBBUB_ERR_PAUSED while a script holds the
 *  parse (it ends when that is resumed) */
dom_hubbub_error onyx_xml_parser_completed(onyx_xml_parser *p);

/** the parse resumed after a script it waited for */
void onyx_xml_parser_resume(onyx_xml_parser *p);

/** whether the parse is over (completed, or stopped by an error) */
bool onyx_xml_parser_done(onyx_xml_parser *p);

/** the well-formedness error, if any: its line and column (from 1) and message */
bool onyx_xml_parser_error(onyx_xml_parser *p, int *line, int *col, const char **msg);

/** the xml-stylesheet processing instructions of the prolog (in order) */
const struct onyx_xml_stylesheet *onyx_xml_parser_stylesheets(onyx_xml_parser *p, int *n);

/** the encoding (the transport's, else the declaration's, else UTF-8) */
const char *onyx_xml_parser_encoding(onyx_xml_parser *p);

void onyx_xml_parser_destroy(onyx_xml_parser *p);

/**
 * A whole document parsed at once into a new document of that kind (DOMParser,
 * XMLHttpRequest's responseXML, an XSLT style sheet). A well-formedness error gives the
 * document emptied, its root a <parsererror> (the DOM Parsing standard's, as Firefox) and *error set.
 */
nserror onyx_xml_parse_document(const char *data, size_t len, int kind,
		struct dom_document **doc, bool *error);

/** Chrome's error box put in a document parsed with an error: a <parsererror> (XHTML
 *  namespace) first in the root element (in <body> for XHTML), or the root itself */
void onyx_xml_error_banner(struct dom_document *doc, int line, int col, const char *msg);

/**
 * The tree view of an XML document with no style information (Chrome's "This XML file
 * does not appear to have any style information..."): an HTML page, malloc'd, *len bytes.
 * err_* : the parse error to show above (line 0: none).
 */
char *onyx_xml_tree_view(struct dom_document *doc, size_t *len, int err_line, int err_col,
		const char *err_msg);

/** the namespace of a document's root element: 0 none / other, 1 XHTML, 2 SVG */
int onyx_xml_root_ns(struct dom_document *doc);

#endif
