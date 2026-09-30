/*
 * tools/tests/netsurf/html5lib_tree.c -- the html5lib tree-construction tests (.dat files)
 * run against NetSurf's HTML parser (libhubbub + the libdom binding), on the PC.
 *
 *   html5lib_tree [-v] [-c N] [-f filter] file.dat ...
 *
 * Each test's #data is parsed as a document (or, with #document-fragment, as a fragment in
 * that context element); the libdom tree is written in html5lib's format and compared with
 * #document. Prints "file: passed/total" per file and the sums. -v prints each failure (the
 * input, the expected tree, the tree made); -c N gives the parser the data N bytes at a time
 * (as the network does: the result must not change); -f runs the tests whose data contains
 * the filter. #script-off tests run with scripting off. Built and run by html5lib.sh.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <dom/dom.h>
#include <dom/bindings/hubbub/parser.h>

typedef struct { char *p; size_t n, cap; } sbuf;

static void sb_add(sbuf *b, const char *s, size_t n)
{
	if (b->n + n + 1 > b->cap) {
		b->cap = (b->n + n + 1) * 2 + 64;
		b->p = realloc(b->p, b->cap);
	}
	memcpy(b->p + b->n, s, n);
	b->n += n;
	b->p[b->n] = 0;
}
static void sb_str(sbuf *b, const char *s) { sb_add(b, s, strlen(s)); }
static void sb_ds(sbuf *b, dom_string *s)
{
	if (s != NULL)
		sb_add(b, dom_string_data(s), dom_string_byte_length(s));
}
static void sb_indent(sbuf *b, int depth)
{
	sb_str(b, "| ");
	for (int i = 0; i < depth; i++)
		sb_str(b, "  ");
}

static const char *ns_prefix(dom_string *ns)
{
	if (ns == NULL)
		return NULL;
	if (dom_string_isequal(ns, dom_namespaces[DOM_NAMESPACE_SVG])) return "svg";
	if (dom_string_isequal(ns, dom_namespaces[DOM_NAMESPACE_MATHML])) return "math";
	if (dom_string_isequal(ns, dom_namespaces[DOM_NAMESPACE_XLINK])) return "xlink";
	if (dom_string_isequal(ns, dom_namespaces[DOM_NAMESPACE_XML])) return "xml";
	if (dom_string_isequal(ns, dom_namespaces[DOM_NAMESPACE_XMLNS])) return "xmlns";
	return NULL;
}

typedef struct { char *name; char *value; } attr_t;
static int attr_cmp(const void *a, const void *b)
{
	return strcmp(((const attr_t *) a)->name, ((const attr_t *) b)->name);
}

static void dump_children(sbuf *b, dom_node *n, int depth);

#ifdef DOM_HUBBUB_HAVE_TEMPLATE_CONTENT
/* Onyx: the template's contents (a fragment kept on the element) */
static dom_node *template_content(dom_node *n)
{
	dom_document_fragment *f = NULL;
	dom_string *ns = NULL, *name = NULL;
	bool is = false;
	dom_node_get_namespace(n, &ns);
	dom_node_get_local_name(n, &name);
	is = ns != NULL && name != NULL &&
		dom_string_isequal(ns, dom_namespaces[DOM_NAMESPACE_HTML]) &&
		dom_string_byte_length(name) == 8 &&
		strncasecmp(dom_string_data(name), "template", 8) == 0;
	if (ns) dom_string_unref(ns);
	if (name) dom_string_unref(name);
	if (!is || dom_hubbub_template_content((dom_element *) n, &f) != DOM_NO_ERR)
		return NULL;
	return (dom_node *) f;
}
#endif

static void dump_node(sbuf *b, dom_node *n, int depth)
{
	dom_node_type type;
	dom_string *s = NULL, *ns = NULL;

	dom_node_get_node_type(n, &type);
	switch (type) {
	case DOM_ELEMENT_NODE: {
		const char *pfx;
		dom_namednodemap *map = NULL;
		dom_ulong len = 0;

		sb_indent(b, depth);
		sb_str(b, "<");
		dom_node_get_namespace(n, &ns);
		pfx = ns_prefix(ns);
		if (pfx != NULL && (!strcmp(pfx, "svg") || !strcmp(pfx, "math"))) {
			sb_str(b, pfx);
			sb_str(b, " ");
		}
		dom_node_get_local_name(n, &s);
		if (pfx == NULL && s != NULL) {
			/* libdom keeps an HTML element's name in upper case */
			size_t k0 = b->n;
			sb_ds(b, s);
			for (size_t k = k0; k < b->n; k++)
				if (b->p[k] >= 'A' && b->p[k] <= 'Z')
					b->p[k] += 32;
		} else {
			sb_ds(b, s);
		}
		if (s) dom_string_unref(s);
		sb_str(b, ">\n");

		dom_node_get_attributes(n, &map);
		if (map != NULL) {
			dom_namednodemap_get_length(map, &len);
			attr_t *at = calloc(len + 1, sizeof(attr_t));
			for (dom_ulong i = 0; i < len; i++) {
				dom_node *a = NULL;
				dom_string *an = NULL, *ans = NULL, *av = NULL;
				sbuf nm = { 0 }, vl = { 0 };
				dom_namednodemap_item(map, i, &a);
				if (a == NULL)
					continue;
				dom_node_get_namespace(a, &ans);
				pfx = ns_prefix(ans);
				if (pfx != NULL) {
					sb_str(&nm, pfx);
					sb_str(&nm, " ");
					dom_node_get_local_name(a, &an);
				} else {
					dom_node_get_node_name(a, &an);
				}
				sb_ds(&nm, an);
				dom_attr_get_value((dom_attr *) a, &av);
				sb_str(&vl, "");
				sb_ds(&vl, av);
				at[i].name = nm.p ? nm.p : strdup("");
				at[i].value = vl.p;
				if (an) dom_string_unref(an);
				if (ans) dom_string_unref(ans);
				if (av) dom_string_unref(av);
				dom_node_unref(a);
			}
			qsort(at, len, sizeof(attr_t), attr_cmp);
			for (dom_ulong i = 0; i < len; i++) {
				sb_indent(b, depth + 1);
				sb_str(b, at[i].name);
				sb_str(b, "=\"");
				sb_str(b, at[i].value);
				sb_str(b, "\"\n");
				free(at[i].name);
				free(at[i].value);
			}
			free(at);
			dom_namednodemap_unref(map);
		}
#ifdef DOM_HUBBUB_HAVE_TEMPLATE_CONTENT
		{
			dom_node *c = template_content(n);
			if (c != NULL) {
				sb_indent(b, depth + 1);
				sb_str(b, "content\n");
				dump_children(b, c, depth + 2);
				dom_node_unref(c);
			}
		}
#endif
		if (ns) dom_string_unref(ns);
		dump_children(b, n, depth + 1);
		break;
	}
	case DOM_TEXT_NODE:
	case DOM_CDATA_SECTION_NODE:
		sb_indent(b, depth);
		sb_str(b, "\"");
		dom_characterdata_get_data(n, &s);
		sb_ds(b, s);
		if (s) dom_string_unref(s);
		sb_str(b, "\"\n");
		break;
	case DOM_COMMENT_NODE:
		sb_indent(b, depth);
		sb_str(b, "<!-- ");
		dom_characterdata_get_data(n, &s);
		sb_ds(b, s);
		if (s) dom_string_unref(s);
		sb_str(b, " -->\n");
		break;
	case DOM_DOCUMENT_TYPE_NODE: {
		dom_string *pub = NULL, *sys = NULL;
		sb_indent(b, depth);
		sb_str(b, "<!DOCTYPE ");
		dom_document_type_get_name((dom_document_type *) n, &s);
		sb_ds(b, s);
		dom_document_type_get_public_id((dom_document_type *) n, &pub);
		dom_document_type_get_system_id((dom_document_type *) n, &sys);
		if ((pub && dom_string_byte_length(pub)) || (sys && dom_string_byte_length(sys))) {
			sb_str(b, " \"");
			sb_ds(b, pub);
			sb_str(b, "\" \"");
			sb_ds(b, sys);
			sb_str(b, "\"");
		}
		sb_str(b, ">\n");
		if (s) dom_string_unref(s);
		if (pub) dom_string_unref(pub);
		if (sys) dom_string_unref(sys);
		break;
	}
	default:
		sb_indent(b, depth);
		sb_str(b, "?node\n");
	}
}

static void dump_children(sbuf *b, dom_node *n, int depth)
{
	dom_node *c = NULL, *next;
	dom_node_get_first_child(n, &c);
	while (c != NULL) {
		dump_node(b, c, depth);
		next = NULL;
		dom_node_get_next_sibling(c, &next);
		dom_node_unref(c);
		c = next;
	}
}

/* ---- the test files ---- */

typedef struct {
	sbuf data, doc, frag;
	int script;	/* -1 off, 1 on, 0 either */
	int line;
} test_t;

static char *read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *p;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	*len = ftell(f);
	fseek(f, 0, SEEK_SET);
	p = malloc(*len + 1);
	*len = fread(p, 1, *len, f);
	p[*len] = 0;
	fclose(f);
	return p;
}

static int verbose, all_scripting = 1;
static size_t chunk;		/* -c N: the data given N bytes at a time (0: at once) */
static const char *filter;

static void feed(dom_hubbub_parser *parser, const char *p, size_t n)
{
	if (chunk == 0) {
		dom_hubbub_parser_parse_chunk(parser, (const uint8_t *) p, n);
		return;
	}
	for (size_t o = 0; o < n; o += chunk)
		dom_hubbub_parser_parse_chunk(parser, (const uint8_t *) p + o,
				n - o < chunk ? n - o : chunk);
}

/* parse t->data; the tree in out */
static void run_parse(test_t *t, bool scripting, sbuf *out)
{
	dom_hubbub_parser_params params;
	dom_hubbub_parser *parser = NULL;
	dom_document *doc = NULL;

	memset(&params, 0, sizeof(params));
	params.enc = "UTF-8";
	params.fix_enc = true;
	params.enable_script = scripting;

	if (t->frag.n == 0) {
		if (dom_hubbub_parser_create(&params, &parser, &doc) != DOM_HUBBUB_OK) {
			sb_str(out, "(parser create failed)\n");
			return;
		}
		feed(parser, t->data.p, t->data.n);
		dom_hubbub_parser_completed(parser);
		dump_children(out, (dom_node *) doc, 0);
		dom_hubbub_parser_destroy(parser);
		dom_node_unref(doc);
		return;
	}

	/* a fragment: the context element made in a document of its own */
	{
		dom_document *cdoc = NULL;
		dom_hubbub_parser *p2 = NULL;
		dom_document_fragment *frag = NULL;
		dom_element *ctx = NULL;
		dom_string *name = NULL;
		char *ctxname = t->frag.p;
		dom_namespace ns = DOM_NAMESPACE_HTML;

		while (t->frag.n && (t->frag.p[t->frag.n - 1] == '\n'))
			t->frag.p[--t->frag.n] = 0;
		if (!strncmp(ctxname, "svg ", 4)) { ns = DOM_NAMESPACE_SVG; ctxname += 4; }
		else if (!strncmp(ctxname, "math ", 5)) { ns = DOM_NAMESPACE_MATHML; ctxname += 5; }

		dom_hubbub_parser_create(&params, &p2, &cdoc);
		dom_hubbub_parser_parse_chunk(p2, (const uint8_t *) "", 0);
		dom_hubbub_parser_completed(p2);
		dom_hubbub_parser_destroy(p2);
		/* a no-quirks context document (the fragment parser takes its mode) */
		dom_document_set_quirks_mode(cdoc, DOM_DOCUMENT_QUIRKS_MODE_NONE);
		dom_string_create((const uint8_t *) ctxname, strlen(ctxname), &name);
		dom_document_create_element_ns(cdoc, dom_namespaces[ns], name, &ctx);
		dom_string_unref(name);

#ifdef DOM_HUBBUB_HAVE_FRAGMENT_CONTEXT
		if (dom_hubbub_fragment_parser_create_ctx(&params, cdoc, ctx, &parser,
				&frag) != DOM_HUBBUB_OK) {
			sb_str(out, "(fragment parser create failed)\n");
		} else {
			feed(parser, t->data.p, t->data.n);
			dom_hubbub_parser_completed(parser);
			dump_children(out, (dom_node *) frag, 0);
			dom_hubbub_parser_destroy(parser);
			dom_node_unref(frag);
		}
#else
		/* the old fragment parser: html > body, its children taken (as qjs.c did) */
		if (dom_hubbub_fragment_parser_create(&params, cdoc, &parser, &frag) ==
				DOM_HUBBUB_OK) {
			dom_node *html = NULL, *body = NULL;
			feed(parser, t->data.p, t->data.n);
			dom_hubbub_parser_completed(parser);
			dom_node_get_first_child(frag, &html);
			if (html) dom_node_get_last_child(html, &body);
			if (body) { dump_children(out, body, 0); dom_node_unref(body); }
			if (html) dom_node_unref(html);
			dom_hubbub_parser_destroy(parser);
			dom_node_unref(frag);
		}
#endif
		dom_node_unref(ctx);
		dom_node_unref(cdoc);
	}
}

static int run_file(const char *path, int *total_pass, int *total_run, int *total_skip)
{
	size_t len;
	char *text = read_file(path, &len);
	char *p, *line;
	int pass = 0, run = 0, skip = 0;
	test_t t;
	enum { NONE, DATA, ERRORS, DOC, FRAG, OTHER } sec = NONE;
	int lineno = 0;
	const char *base = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;

	if (text == NULL) {
		fprintf(stderr, "cannot read %s\n", path);
		return 1;
	}
	memset(&t, 0, sizeof(t));

	/* the tests, one after the other; a test ends at the next "#data" */
	p = text;
	for (;;) {
		char *eol;
		int end = (p >= text + len);
		if (!end) {
			/* (binary safe: the data may hold NUL characters) */
			eol = memchr(p, '\n', text + len - p);
			if (!eol) eol = text + len;
		} else {
			eol = p;
		}
		line = p;
		lineno++;
		if (end || (eol - line == 5 && !strncmp(line, "#data", 5))) {
			/* the previous test */
			if (t.line > 0) {
				int skipit = 0;
				if (t.script == -1 && !all_scripting)
					skipit = 1;
				if (filter && !strstr(t.data.p ? t.data.p : "", filter))
					skipit = 2;
				if (skipit == 1)
					skip++;
				if (!skipit) {
					sbuf out = { 0 };
					/* the data's final newline is the section's */
					if (t.data.n && t.data.p[t.data.n - 1] == '\n')
						t.data.p[--t.data.n] = 0;
					while (t.doc.n && t.doc.p[t.doc.n - 1] == '\n')
						t.doc.p[--t.doc.n] = 0;
					sb_str(&t.doc, "\n");
					if (!t.data.p) sb_str(&t.data, "");
					run_parse(&t, t.script != -1, &out);
					if (!out.p) sb_str(&out, "");
					run++;
					if (out.n == t.doc.n && memcmp(out.p, t.doc.p, out.n) == 0) {
						pass++;
					} else if (verbose) {
						printf("=== FAIL %s:%d%s%s\n--- data:\n%s\n--- expected:\n%s--- got:\n%s\n",
								base, t.line,
								t.frag.n ? " fragment " : "",
								t.frag.n ? t.frag.p : "",
								t.data.p, t.doc.p, out.p);
					}
					free(out.p);
				}
			}
			free(t.data.p); free(t.doc.p); free(t.frag.p);
			memset(&t, 0, sizeof(t));
			if (end)
				break;
			t.line = lineno;
			sec = DATA;
			p = (eol < text + len) ? eol + 1 : eol;
			continue;
		}
		if (line[0] == '#' && sec != DATA) {
			if (!strncmp(line, "#errors", 7) || !strncmp(line, "#new-errors", 11))
				sec = ERRORS;
			else if (!strncmp(line, "#document-fragment", 18))
				sec = FRAG;
			else if (!strncmp(line, "#document", 9))
				sec = DOC;
			else if (!strncmp(line, "#script-on", 10))
				{ t.script = 1; sec = OTHER; }
			else if (!strncmp(line, "#script-off", 11))
				{ t.script = -1; sec = OTHER; }
			else
				sec = OTHER;
		} else if (line[0] == '#' && sec == DATA &&
				(!strncmp(line, "#errors", 7) || !strncmp(line, "#new-errors", 11))) {
			sec = ERRORS;
		} else {
			size_t n = eol - line + (eol < text + len ? 1 : 0);
			switch (sec) {
			case DATA: sb_add(&t.data, line, n); break;
			case DOC: sb_add(&t.doc, line, n); break;
			case FRAG: sb_add(&t.frag, line, n); break;
			default: break;
			}
		}
		p = (eol < text + len) ? eol + 1 : eol;
	}
	free(text);
	printf("%-44s %4d / %4d%s\n", base, pass, run,
			pass == run ? "" : "  *");
	*total_pass += pass;
	*total_run += run;
	*total_skip += skip;
	return 0;
}

int main(int argc, char **argv)
{
	int total_pass = 0, total_run = 0, total_skip = 0;
	int i = 1;

	for (; i < argc && argv[i][0] == '-'; i++) {
		if (!strcmp(argv[i], "-v")) verbose = 1;
		else if (!strcmp(argv[i], "-s")) all_scripting = 1;
		else if (!strcmp(argv[i], "-f") && i + 1 < argc) filter = argv[++i];
		else if (!strcmp(argv[i], "-c") && i + 1 < argc) chunk = atoi(argv[++i]);
	}
	for (; i < argc; i++)
		run_file(argv[i], &total_pass, &total_run, &total_skip);
	printf("TOTAL tree-construction: %d / %d passed (%.1f%%)%s\n", total_pass, total_run,
			total_run ? 100.0 * total_pass / total_run : 0.0,
			total_skip ? " (script-off tests skipped: run with -s)" : "");
	return 0;
}
