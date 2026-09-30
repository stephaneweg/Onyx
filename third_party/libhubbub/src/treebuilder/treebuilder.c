/*
 * This file is part of Hubbub.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 John-Mark Bell <jmb@netsurf-browser.org>
 * Copyright 2008 Andrew Sidwell <takkaria@netsurf-browser.org>
 *
 * Onyx: the tree builder rewritten after the current HTML standard's tree construction
 * (WHATWG, sections 13.2.4 and 13.2.6; 13.4 for fragments): every insertion mode of the
 * standard ("in template", "in table text"... ; <select> parsed as the standard now does,
 * without "in select"), foster parenting, the adoption agency algorithm (its loop counters and
 * bookmark), the list of active formatting elements with markers and the "Noah's ark" clause,
 * foreign content (SVG and MathML: the tag and attribute case adjustments, the xlink / xml /
 * xmlns attributes, the MathML text and HTML integration points), <template> contents, the
 * form element pointer, <noscript> as scripting says, fragments parsed in their context element
 * (innerHTML), quirks from the doctype. The text of consecutive character tokens goes into the
 * tree in one piece (and onto the text node already there). It still builds the tree through
 * the client's hubbub_tree_handler callbacks (the libdom binding), as the page downloads, and a
 * script's end tag still calls complete_script (which may pause the parser).
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <parserutils/charset/mibenum.h>

#include "charset/detect.h"
#include "treebuilder/treebuilder.h"
#include "utils/utils.h"
#include "utils/string.h"

/* ---- element types ------------------------------------------------------------------ */

enum {
	T_OTHER = 0,
	T_A, T_ADDRESS, T_ANNOTATION_XML, T_APPLET, T_AREA, T_ARTICLE, T_ASIDE, T_B, T_BASE,
	T_BASEFONT, T_BGSOUND, T_BIG, T_BLOCKQUOTE, T_BODY, T_BR, T_BUTTON, T_CAPTION, T_CENTER,
	T_CODE, T_COL, T_COLGROUP, T_DD, T_DESC, T_DETAILS, T_DIALOG, T_DIR, T_DIV, T_DL, T_DT,
	T_EM, T_EMBED, T_FIELDSET, T_FIGCAPTION, T_FIGURE, T_FONT, T_FOOTER, T_FOREIGNOBJECT,
	T_FORM, T_FRAME, T_FRAMESET, T_H1, T_H2, T_H3, T_H4, T_H5, T_H6, T_HEAD, T_HEADER,
	T_HGROUP, T_HR, T_HTML, T_I, T_IFRAME, T_IMAGE, T_IMG, T_INPUT, T_KEYGEN, T_LABEL, T_LI,
	T_LINK, T_LISTING, T_MAIN, T_MALIGNMARK, T_MARQUEE, T_MATH, T_MENU, T_META, T_MGLYPH,
	T_MI, T_MN, T_MO, T_MS, T_MTEXT, T_NAV, T_NOBR, T_NOEMBED, T_NOFRAMES, T_NOSCRIPT,
	T_OBJECT, T_OL, T_OPTGROUP, T_OPTION, T_OUTPUT, T_P, T_PARAM, T_PLAINTEXT, T_PRE, T_RB,
	T_RP, T_RT, T_RTC, T_RUBY, T_S, T_SCRIPT, T_SEARCH, T_SECTION, T_SELECT, T_SMALL,
	T_SOURCE, T_SPAN, T_STRIKE, T_STRONG, T_STYLE, T_SUB, T_SUMMARY, T_SUP, T_SVG, T_TABLE,
	T_TBODY, T_TD, T_TEMPLATE, T_TEXTAREA, T_TFOOT, T_TH, T_THEAD, T_TITLE, T_TR, T_TRACK,
	T_TT, T_U, T_UL, T_VAR, T_WBR, T_XMP, T_DATALIST, T_SELECTEDCONTENT,
	T__COUNT
};

/* sorted by name (bsearch) */
static const struct { const char *name; uint8_t len; uint8_t type; } type_names[] = {
#define E(n, t) { n, sizeof n - 1, t }
	E("a", T_A), E("address", T_ADDRESS), E("annotation-xml", T_ANNOTATION_XML),
	E("applet", T_APPLET), E("area", T_AREA), E("article", T_ARTICLE), E("aside", T_ASIDE),
	E("b", T_B), E("base", T_BASE), E("basefont", T_BASEFONT), E("bgsound", T_BGSOUND),
	E("big", T_BIG), E("blockquote", T_BLOCKQUOTE), E("body", T_BODY), E("br", T_BR),
	E("button", T_BUTTON), E("caption", T_CAPTION), E("center", T_CENTER), E("code", T_CODE),
	E("col", T_COL), E("colgroup", T_COLGROUP), E("datalist", T_DATALIST), E("dd", T_DD),
	E("desc", T_DESC),
	E("details", T_DETAILS), E("dialog", T_DIALOG), E("dir", T_DIR), E("div", T_DIV),
	E("dl", T_DL), E("dt", T_DT), E("em", T_EM), E("embed", T_EMBED),
	E("fieldset", T_FIELDSET), E("figcaption", T_FIGCAPTION), E("figure", T_FIGURE),
	E("font", T_FONT), E("footer", T_FOOTER), E("foreignobject", T_FOREIGNOBJECT),
	E("form", T_FORM), E("frame", T_FRAME), E("frameset", T_FRAMESET), E("h1", T_H1),
	E("h2", T_H2), E("h3", T_H3), E("h4", T_H4), E("h5", T_H5), E("h6", T_H6),
	E("head", T_HEAD), E("header", T_HEADER), E("hgroup", T_HGROUP), E("hr", T_HR),
	E("html", T_HTML), E("i", T_I), E("iframe", T_IFRAME), E("image", T_IMAGE),
	E("img", T_IMG), E("input", T_INPUT), E("keygen", T_KEYGEN), E("label", T_LABEL),
	E("li", T_LI), E("link", T_LINK), E("listing", T_LISTING), E("main", T_MAIN),
	E("malignmark", T_MALIGNMARK), E("marquee", T_MARQUEE), E("math", T_MATH),
	E("menu", T_MENU), E("meta", T_META), E("mglyph", T_MGLYPH), E("mi", T_MI),
	E("mn", T_MN), E("mo", T_MO), E("ms", T_MS), E("mtext", T_MTEXT), E("nav", T_NAV),
	E("nobr", T_NOBR), E("noembed", T_NOEMBED), E("noframes", T_NOFRAMES),
	E("noscript", T_NOSCRIPT), E("object", T_OBJECT), E("ol", T_OL),
	E("optgroup", T_OPTGROUP), E("option", T_OPTION), E("output", T_OUTPUT), E("p", T_P),
	E("param", T_PARAM), E("plaintext", T_PLAINTEXT), E("pre", T_PRE), E("rb", T_RB),
	E("rp", T_RP), E("rt", T_RT), E("rtc", T_RTC), E("ruby", T_RUBY), E("s", T_S),
	E("script", T_SCRIPT), E("search", T_SEARCH), E("section", T_SECTION),
	E("select", T_SELECT), E("selectedcontent", T_SELECTEDCONTENT), E("small", T_SMALL), E("source", T_SOURCE), E("span", T_SPAN),
	E("strike", T_STRIKE), E("strong", T_STRONG), E("style", T_STYLE), E("sub", T_SUB),
	E("summary", T_SUMMARY), E("sup", T_SUP), E("svg", T_SVG), E("table", T_TABLE),
	E("tbody", T_TBODY), E("td", T_TD), E("template", T_TEMPLATE),
	E("textarea", T_TEXTAREA), E("tfoot", T_TFOOT), E("th", T_TH), E("thead", T_THEAD),
	E("title", T_TITLE), E("tr", T_TR), E("track", T_TRACK), E("tt", T_TT), E("u", T_U),
	E("ul", T_UL), E("var", T_VAR), E("wbr", T_WBR), E("xmp", T_XMP),
#undef E
};
#define N_TYPE_NAMES (sizeof type_names / sizeof type_names[0])

static const char *type_name[T__COUNT];

static int name_type(const uint8_t *s, size_t n)
{
	size_t lo = 0, hi = N_TYPE_NAMES;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		size_t m = type_names[mid].len < n ? type_names[mid].len : n;
		int r = memcmp(type_names[mid].name, s, m);
		if (r == 0)
			r = (int) type_names[mid].len - (int) n;
		if (r == 0)
			return type_names[mid].type;
		if (r < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return T_OTHER;
}

/* ---- insertion modes -------------------------------------------------------------- */

typedef enum {
	M_INITIAL, M_BEFORE_HTML, M_BEFORE_HEAD, M_IN_HEAD, M_IN_HEAD_NOSCRIPT, M_AFTER_HEAD,
	M_IN_BODY, M_TEXT, M_IN_TABLE, M_IN_TABLE_TEXT, M_IN_CAPTION, M_IN_COLUMN_GROUP,
	M_IN_TABLE_BODY, M_IN_ROW, M_IN_CELL, M_IN_TEMPLATE, M_AFTER_BODY, M_IN_FRAMESET,
	M_AFTER_FRAMESET, M_AFTER_AFTER_BODY, M_AFTER_AFTER_FRAMESET
} mode_t_;

/* ---- the parser's state ----------------------------------------------------------- */

#define NAME_INLINE 24

/** an element of the stack of open elements */
typedef struct elem {
	void *node;			/**< a reference is held */
	uint8_t type;
	uint8_t ns;			/**< hubbub_ns */
	bool hip;			/**< an annotation-xml HTML integration point */
	bool flag;			/**< option: selected; select: multiple (attributes) */
	char *heap;			/**< the token's (lower case) name if long (T_OTHER) */
	char inl[NAME_INLINE];		/**< else here */
} elem;

/** an entry of the list of active formatting elements: an element or a marker */
typedef struct fmt {
	void *node;			/**< NULL: a marker; else a reference is held */
	uint8_t type;
	uint32_t n_attrs;
	uint8_t *attrs;			/**< the token's attributes (Noah's ark), packed */
	size_t attrs_len;
} fmt;

typedef struct tstring {
	uint8_t *p;
	size_t len, cap;
} tstring;

struct hubbub_treebuilder {
	hubbub_tokeniser *tokeniser;
	hubbub_tree_handler *tree_handler;
	hubbub_error_handler error_handler;
	void *error_pw;

	void *document;			/**< the document (or the fragment) node */
	bool enable_scripting;

	mode_t_ mode, original_mode;
	uint8_t *template_modes;	/**< the stack of template insertion modes */
	uint32_t n_template_modes, template_modes_cap;

	elem *st;			/**< the stack of open elements */
	uint32_t n, cap;
	fmt *fl;			/**< the list of active formatting elements */
	uint32_t fn, fcap;

	void *head;			/**< the head element pointer (ref) */
	void *form;			/**< the form element pointer (ref) */

	bool frameset_ok;
	bool foster;			/**< foster parenting enabled */
	bool skip_lf;			/**< ignore a LF at the start of the next token */
	bool quirks;			/**< the document is in quirks mode */
	bool cdata;			/**< what the tokeniser was last told */

	/* the fragment case */
	bool fragment;
	elem ctx;			/**< the context element (node: a reference, may be NULL) */

	/* the pending table character tokens */
	tstring table_text;
	bool table_text_nonws;

	/* the characters gathered for one place of the tree */
	tstring text;
	void *text_parent, *text_before;	/**< references */

	/* <selectedcontent>: the selects' selected options and the selectedcontent elements
	 * that show them (the standard's customizable <select>) */
	struct sel_entry { void *select, *option; bool multiple; } *sels;
	uint32_t n_sels, sels_cap;
	struct sc_entry { void *select, *sc; } *scs;
	uint32_t n_scs, scs_cap;
};

/* ---- small helpers ---------------------------------------------------------------- */

#define TH (tb->tree_handler)
#define CTX (tb->tree_handler->ctx)

static inline void ref(hubbub_treebuilder *tb, void *n) { if (n) TH->ref_node(CTX, n); }
static inline void unref(hubbub_treebuilder *tb, void *n) { if (n) TH->unref_node(CTX, n); }

static bool ts_add(tstring *s, const uint8_t *p, size_t n)
{
	if (s->len + n > s->cap) {
		size_t c = s->cap ? s->cap * 2 : 256;
		uint8_t *q;
		while (c < s->len + n)
			c *= 2;
		q = realloc(s->p, c);
		if (q == NULL)
			return false;
		s->p = q;
		s->cap = c;
	}
	memcpy(s->p + s->len, p, n);
	s->len += n;
	return true;
}

static inline bool is_ws(uint8_t c)
{
	return c == '\t' || c == '\n' || c == '\f' || c == ' ' || c == '\r';
}

static bool str_eq(const hubbub_string *s, const char *lit)
{
	size_t n = strlen(lit);
	return s->len == n && memcmp(s->ptr, lit, n) == 0;
}

static bool str_eq_ci(const uint8_t *p, size_t len, const char *lit)
{
	size_t n = strlen(lit);
	if (len != n)
		return false;
	for (size_t i = 0; i < n; i++) {
		uint8_t c = p[i];
		if (c >= 'A' && c <= 'Z')
			c += 32;
		if (c != (uint8_t) lit[i])
			return false;
	}
	return true;
}

static bool starts_ci(const uint8_t *p, size_t len, const char *lit)
{
	size_t n = strlen(lit);
	return len >= n && str_eq_ci(p, n, lit);
}

static const hubbub_attribute *find_attr(const hubbub_tag *tag, const char *name)
{
	for (uint32_t i = 0; i < tag->n_attributes; i++)
		if (str_eq(&tag->attributes[i].name, name))
			return &tag->attributes[i];
	return NULL;
}

/* ---- the stack of open elements ------------------------------------------------------ */

static inline elem *cur(hubbub_treebuilder *tb) { return &tb->st[tb->n - 1]; }

static inline bool is_html(const elem *e, int type)
{
	return e->ns == HUBBUB_NS_HTML && e->type == type;
}

/* the adjusted current node */
static inline const elem *adjusted_current(hubbub_treebuilder *tb)
{
	if (tb->fragment && tb->n == 1)
		return &tb->ctx;
	return tb->n ? cur(tb) : NULL;
}

static const char *elem_name(const elem *e)
{
	if (e->type != T_OTHER)
		return type_name[e->type];
	return e->heap ? e->heap : e->inl;
}

static void elem_set_name(elem *e, const uint8_t *name, size_t len)
{
	char *d = e->inl;
	e->heap = NULL;
	e->inl[0] = 0;
	if (e->type != T_OTHER || name == NULL)
		return;
	if (len >= NAME_INLINE) {
		e->heap = malloc(len + 1);
		if (e->heap == NULL)
			return;
		d = e->heap;
	}
	memcpy(d, name, len);
	d[len] = 0;
}

static void elem_free_name(elem *e)
{
	free(e->heap);
	e->heap = NULL;
}

static bool st_reserve(hubbub_treebuilder *tb)
{
	if (tb->n == tb->cap) {
		uint32_t c = tb->cap ? tb->cap * 2 : 64;
		elem *s = realloc(tb->st, c * sizeof(elem));
		if (s == NULL)
			return false;
		tb->st = s;
		tb->cap = c;
	}
	return true;
}

/* push node (its reference is taken over) */
static hubbub_error push(hubbub_treebuilder *tb, void *node, hubbub_ns ns, int type,
		const uint8_t *name, size_t len, bool hip)
{
	elem *e;
	if (!st_reserve(tb))
		return HUBBUB_NOMEM;
	e = &tb->st[tb->n++];
	e->node = node;
	e->ns = ns;
	e->type = type;
	e->hip = hip;
	e->flag = false;
	elem_set_name(e, name, len);
	return HUBBUB_OK;
}

static void option_popped(hubbub_treebuilder *tb, uint32_t i);
static void selectedcontent_inserted(hubbub_treebuilder *tb);

static void pop(hubbub_treebuilder *tb)
{
	elem *e;
	if (tb->n == 0)
		return;
	if (tb->n_scs > 0 && is_html(&tb->st[tb->n - 1], T_OPTION))
		option_popped(tb, tb->n - 1);
	e = &tb->st[--tb->n];
	unref(tb, e->node);
	elem_free_name(e);
}

static void st_remove(hubbub_treebuilder *tb, uint32_t i)
{
	elem *e;
	if (tb->n_scs > 0 && is_html(&tb->st[i], T_OPTION))
		option_popped(tb, i);
	e = &tb->st[i];
	unref(tb, e->node);
	elem_free_name(e);
	memmove(&tb->st[i], &tb->st[i + 1], (tb->n - i - 1) * sizeof(elem));
	tb->n--;
}

/* insert e (its node's reference and its name taken over) at index i */
static hubbub_error st_insert(hubbub_treebuilder *tb, uint32_t i, const elem *e)
{
	if (!st_reserve(tb))
		return HUBBUB_NOMEM;
	memmove(&tb->st[i + 1], &tb->st[i], (tb->n - i) * sizeof(elem));
	tb->n++;
	tb->st[i] = *e;
	return HUBBUB_OK;
}

static int st_find_node(hubbub_treebuilder *tb, void *node)
{
	for (int i = (int) tb->n - 1; i >= 0; i--)
		if (tb->st[i].node == node)
			return i;
	return -1;
}

static bool has_template(hubbub_treebuilder *tb)
{
	for (uint32_t i = 0; i < tb->n; i++)
		if (is_html(&tb->st[i], T_TEMPLATE))
			return true;
	return false;
}

/* a template on the stack, or a template as the fragment's context */
static bool in_template_contents(hubbub_treebuilder *tb)
{
	return has_template(tb) || (tb->fragment && is_html(&tb->ctx, T_TEMPLATE));
}

/* scopes */
enum { SC_DEFAULT, SC_LIST_ITEM, SC_BUTTON, SC_TABLE };

static bool scope_boundary(const elem *e, int scope)
{
	if (scope == SC_TABLE)
		return e->ns == HUBBUB_NS_HTML &&
			(e->type == T_HTML || e->type == T_TABLE || e->type == T_TEMPLATE);
	if (e->ns == HUBBUB_NS_HTML) {
		switch (e->type) {
		case T_APPLET: case T_CAPTION: case T_HTML: case T_TABLE: case T_TD: case T_TH:
		case T_MARQUEE: case T_OBJECT: case T_SELECT: case T_TEMPLATE:
			return true;
		case T_OL: case T_UL:
			return scope == SC_LIST_ITEM;
		case T_BUTTON:
			return scope == SC_BUTTON;
		default:
			return false;
		}
	}
	if (e->ns == HUBBUB_NS_MATHML)
		return e->type == T_MI || e->type == T_MO || e->type == T_MN || e->type == T_MS ||
			e->type == T_MTEXT || e->type == T_ANNOTATION_XML;
	if (e->ns == HUBBUB_NS_SVG)
		return e->type == T_FOREIGNOBJECT || e->type == T_DESC || e->type == T_TITLE;
	return false;
}

/* has an HTML element of this type in the scope? */
static bool in_scope(hubbub_treebuilder *tb, int type, int scope)
{
	for (int i = (int) tb->n - 1; i >= 0; i--) {
		const elem *e = &tb->st[i];
		if (is_html(e, type))
			return true;
		if (scope_boundary(e, scope))
			return false;
	}
	return false;
}

static bool node_in_scope(hubbub_treebuilder *tb, void *node, int scope)
{
	for (int i = (int) tb->n - 1; i >= 0; i--) {
		const elem *e = &tb->st[i];
		if (e->node == node)
			return true;
		if (scope_boundary(e, scope))
			return false;
	}
	return false;
}

static bool is_special(const elem *e)
{
	if (e->ns == HUBBUB_NS_HTML) {
		switch (e->type) {
		case T_ADDRESS: case T_APPLET: case T_AREA: case T_ARTICLE: case T_ASIDE:
		case T_BASE: case T_BASEFONT: case T_BGSOUND: case T_BLOCKQUOTE: case T_BODY:
		case T_BR: case T_BUTTON: case T_CAPTION: case T_CENTER: case T_COL:
		case T_COLGROUP: case T_DD: case T_DETAILS: case T_DIR: case T_DIV: case T_DL:
		case T_DT: case T_EMBED: case T_FIELDSET: case T_FIGCAPTION: case T_FIGURE:
		case T_FOOTER: case T_FORM: case T_FRAME: case T_FRAMESET: case T_H1: case T_H2:
		case T_H3: case T_H4: case T_H5: case T_H6: case T_HEAD: case T_HEADER:
		case T_HGROUP: case T_HR: case T_HTML: case T_IFRAME: case T_IMG: case T_INPUT:
		case T_KEYGEN: case T_LI: case T_LINK: case T_LISTING: case T_MAIN: case T_MARQUEE:
		case T_MENU: case T_META: case T_NAV: case T_NOEMBED: case T_NOFRAMES:
		case T_NOSCRIPT: case T_OBJECT: case T_OL: case T_P: case T_PARAM:
		case T_PLAINTEXT: case T_PRE: case T_SCRIPT: case T_SEARCH: case T_SECTION:
		case T_SELECT: case T_SOURCE: case T_STYLE: case T_SUMMARY: case T_TABLE:
		case T_TBODY: case T_TD: case T_TEMPLATE: case T_TEXTAREA: case T_TFOOT:
		case T_TH: case T_THEAD: case T_TITLE: case T_TR: case T_TRACK: case T_UL:
		case T_WBR: case T_XMP:
			return true;
		default:
			return false;
		}
	}
	return scope_boundary(e, SC_DEFAULT);
}

static bool is_mathml_text_ip(const elem *e)
{
	return e->ns == HUBBUB_NS_MATHML && (e->type == T_MI || e->type == T_MO ||
			e->type == T_MN || e->type == T_MS || e->type == T_MTEXT);
}

static bool is_html_ip(const elem *e)
{
	if (e->ns == HUBBUB_NS_MATHML)
		return e->type == T_ANNOTATION_XML && e->hip;
	if (e->ns == HUBBUB_NS_SVG)
		return e->type == T_FOREIGNOBJECT || e->type == T_DESC || e->type == T_TITLE;
	return false;
}

/* ---- the list of active formatting elements ------------------------------------------ */

static bool fl_reserve(hubbub_treebuilder *tb)
{
	if (tb->fn == tb->fcap) {
		uint32_t c = tb->fcap ? tb->fcap * 2 : 32;
		fmt *f = realloc(tb->fl, c * sizeof(fmt));
		if (f == NULL)
			return false;
		tb->fl = f;
		tb->fcap = c;
	}
	return true;
}

static void fl_free(hubbub_treebuilder *tb, fmt *f)
{
	unref(tb, f->node);
	free(f->attrs);
}

static void fl_remove(hubbub_treebuilder *tb, uint32_t i)
{
	fl_free(tb, &tb->fl[i]);
	memmove(&tb->fl[i], &tb->fl[i + 1], (tb->fn - i - 1) * sizeof(fmt));
	tb->fn--;
}

static int fl_find_node(hubbub_treebuilder *tb, void *node)
{
	for (int i = (int) tb->fn - 1; i >= 0; i--)
		if (tb->fl[i].node == node)
			return i;
	return -1;
}

static hubbub_error insert_marker(hubbub_treebuilder *tb)
{
	if (!fl_reserve(tb))
		return HUBBUB_NOMEM;
	memset(&tb->fl[tb->fn++], 0, sizeof(fmt));
	return HUBBUB_OK;
}

static void clear_to_marker(hubbub_treebuilder *tb)
{
	while (tb->fn > 0) {
		bool marker = tb->fl[tb->fn - 1].node == NULL;
		fl_remove(tb, tb->fn - 1);
		if (marker)
			break;
	}
}

/* the attributes packed: name length, name, value length, value... (32-bit lengths) */
static uint8_t *pack_attrs(const hubbub_tag *tag, size_t *len)
{
	size_t n = 0, o = 0;
	uint8_t *p;
	for (uint32_t i = 0; i < tag->n_attributes; i++)
		n += 8 + tag->attributes[i].name.len + tag->attributes[i].value.len + 1;
	*len = n;
	if (n == 0)
		return NULL;
	p = malloc(n);
	if (p == NULL)
		return NULL;
	for (uint32_t i = 0; i < tag->n_attributes; i++) {
		const hubbub_attribute *a = &tag->attributes[i];
		uint32_t l = a->name.len;
		memcpy(p + o, &l, 4); o += 4;
		memcpy(p + o, a->name.ptr, l); o += l;
		p[o++] = (uint8_t) a->ns;
		l = a->value.len;
		memcpy(p + o, &l, 4); o += 4;
		memcpy(p + o, a->value.ptr, l); o += l;
	}
	*len = o;
	return p;
}

/* does the packed attribute list contain this one? */
static bool packed_has(const uint8_t *p, size_t len, const uint8_t *n, uint32_t nl,
		uint8_t ns, const uint8_t *v, uint32_t vl)
{
	size_t o = 0;
	while (o < len) {
		uint32_t l, l2;
		const uint8_t *name, *val;
		uint8_t ans;
		memcpy(&l, p + o, 4); o += 4;
		name = p + o; o += l;
		ans = p[o++];
		memcpy(&l2, p + o, 4); o += 4;
		val = p + o; o += l2;
		if (l == nl && l2 == vl && ans == ns && memcmp(name, n, nl) == 0 &&
				memcmp(val, v, vl) == 0)
			return true;
	}
	return false;
}

static bool same_attrs(const fmt *a, const fmt *b)
{
	size_t o = 0;
	if (a->n_attrs != b->n_attrs)
		return false;
	while (o < a->attrs_len) {
		uint32_t l, l2;
		const uint8_t *name, *val;
		uint8_t ns;
		memcpy(&l, a->attrs + o, 4); o += 4;
		name = a->attrs + o; o += l;
		ns = a->attrs[o++];
		memcpy(&l2, a->attrs + o, 4); o += 4;
		val = a->attrs + o; o += l2;
		if (!packed_has(b->attrs, b->attrs_len, name, l, ns, val, l2))
			return false;
	}
	return true;
}

/* push onto the list of active formatting elements (node: a new reference is taken) */
static hubbub_error push_formatting(hubbub_treebuilder *tb, void *node, int type,
		const hubbub_tag *tag)
{
	fmt f;
	int count = 0, earliest = -1;

	f.node = node;
	f.type = type;
	f.n_attrs = tag->n_attributes;
	f.attrs = pack_attrs(tag, &f.attrs_len);

	/* Noah's ark: at most three of a kind after the last marker */
	for (int i = (int) tb->fn - 1; i >= 0; i--) {
		fmt *e = &tb->fl[i];
		if (e->node == NULL)
			break;
		if (e->type == type && same_attrs(e, &f)) {
			count++;
			earliest = i;
		}
	}
	if (count >= 3)
		fl_remove(tb, earliest);

	if (!fl_reserve(tb)) {
		free(f.attrs);
		return HUBBUB_NOMEM;
	}
	ref(tb, node);
	tb->fl[tb->fn++] = f;
	return HUBBUB_OK;
}

/* ---- inserting nodes ------------------------------------------------------------------ */

typedef struct loc {
	void *parent;			/**< references */
	void *before;
} loc;

static void loc_release(hubbub_treebuilder *tb, loc *l)
{
	unref(tb, l->parent);
	unref(tb, l->before);
	l->parent = l->before = NULL;
}

static void *get_parent(hubbub_treebuilder *tb, void *node)
{
	void *p = NULL;
	if (TH->get_parent(CTX, node, false, &p) != HUBBUB_OK)
		return NULL;
	return p;			/* a reference */
}

/* a template's contents (a reference), or the template itself without the callback */
static void *template_contents(hubbub_treebuilder *tb, void *node)
{
	void *c = NULL;
	if (TH->template_content != NULL &&
			TH->template_content(CTX, node, &c) == HUBBUB_OK && c != NULL)
		return c;
	ref(tb, node);
	return node;
}

/* the appropriate place for inserting a node (adjusted: the fragment's root goes to the
 * fragment), overriding the target with the given element if any */
static loc appropriate_place(hubbub_treebuilder *tb, const elem *override)
{
	loc l = { NULL, NULL };
	const elem *target = override ? override : cur(tb);
	const elem *te = target;

	if (tb->foster && target->ns == HUBBUB_NS_HTML &&
			(target->type == T_TABLE || target->type == T_TBODY ||
			 target->type == T_TFOOT || target->type == T_THEAD || target->type == T_TR)) {
		int last = -1;
		for (int i = (int) tb->n - 1; i >= 0; i--) {
			if (is_html(&tb->st[i], T_TEMPLATE) || is_html(&tb->st[i], T_TABLE)) {
				last = i;
				break;
			}
		}
		if (last < 0) {
			te = &tb->st[0];
		} else if (is_html(&tb->st[last], T_TEMPLATE)) {
			te = &tb->st[last];
		} else {
			void *p = get_parent(tb, tb->st[last].node);
			if (p != NULL) {
				l.parent = p;
				l.before = tb->st[last].node;
				ref(tb, l.before);
				return l;
			}
			te = &tb->st[last - 1];
		}
	}
	if (is_html(te, T_TEMPLATE)) {
		l.parent = template_contents(tb, te->node);
		return l;
	}
	if (tb->fragment && te == &tb->st[0]) {
		/* the root insertion target: the fragment */
		l.parent = tb->document;
		ref(tb, l.parent);
		return l;
	}
	l.parent = te->node;
	ref(tb, l.parent);
	return l;
}

/* the text gathered goes into the tree */
static hubbub_error flush_text(hubbub_treebuilder *tb)
{
	hubbub_error err = HUBBUB_OK;
	hubbub_string s;

	if (tb->text.len == 0)
		return HUBBUB_OK;
	s.ptr = tb->text.p;
	s.len = tb->text.len;
	if (TH->insert_text != NULL) {
		err = TH->insert_text(CTX, tb->text_parent, tb->text_before, &s);
	} else {
		void *t = NULL, *res = NULL;
		err = TH->create_text(CTX, &s, &t);
		if (err == HUBBUB_OK) {
			if (tb->text_before != NULL)
				err = TH->insert_before(CTX, tb->text_parent, t, tb->text_before, &res);
			else
				err = TH->append_child(CTX, tb->text_parent, t, &res);
			if (err == HUBBUB_OK)
				unref(tb, res);
			unref(tb, t);
		}
	}
	tb->text.len = 0;
	unref(tb, tb->text_parent);
	unref(tb, tb->text_before);
	tb->text_parent = tb->text_before = NULL;
	return err;
}

void hubbub_treebuilder_flush(hubbub_treebuilder *tb)
{
	if (tb != NULL && tb->tree_handler != NULL)
		flush_text(tb);
}

/* insert characters at the appropriate place */
static hubbub_error insert_chars(hubbub_treebuilder *tb, const uint8_t *p, size_t n)
{
	loc l;
	hubbub_error err = HUBBUB_OK;

	if (n == 0)
		return HUBBUB_OK;
	l = appropriate_place(tb, NULL);
	if (l.parent == tb->document && !tb->fragment) {
		loc_release(tb, &l);
		return HUBBUB_OK;
	}
	if (tb->text.len > 0 && (tb->text_parent != l.parent || tb->text_before != l.before)) {
		err = flush_text(tb);
		if (err != HUBBUB_OK) {
			loc_release(tb, &l);
			return err;
		}
	}
	if (tb->text.len == 0) {
		unref(tb, tb->text_parent);
		unref(tb, tb->text_before);
		tb->text_parent = l.parent;
		tb->text_before = l.before;
	} else {
		loc_release(tb, &l);
	}
	return ts_add(&tb->text, p, n) ? HUBBUB_OK : HUBBUB_NOMEM;
}

static hubbub_error insert_node_at(hubbub_treebuilder *tb, loc *l, void *node)
{
	void *res = NULL;
	hubbub_error err;

	err = flush_text(tb);
	if (err != HUBBUB_OK)
		return err;
	if (l->before != NULL)
		err = TH->insert_before(CTX, l->parent, node, l->before, &res);
	else
		err = TH->append_child(CTX, l->parent, node, &res);
	if (err == HUBBUB_OK)
		unref(tb, res);
	return err;
}

static hubbub_error insert_comment_at(hubbub_treebuilder *tb, const hubbub_string *data,
		const elem *where, void *parent)
{
	void *c = NULL;
	loc l;
	hubbub_error err;

	if (parent != NULL) {
		l.parent = parent;
		l.before = NULL;
		ref(tb, parent);
	} else {
		l = appropriate_place(tb, where);
	}
	err = TH->create_comment(CTX, data, &c);
	if (err == HUBBUB_OK) {
		err = insert_node_at(tb, &l, c);
		unref(tb, c);
	}
	loc_release(tb, &l);
	return err;
}

static inline hubbub_error insert_comment(hubbub_treebuilder *tb, const hubbub_string *data)
{
	return insert_comment_at(tb, data, NULL, NULL);
}

static bool is_form_associated(int type)
{
	switch (type) {
	case T_BUTTON: case T_FIELDSET: case T_INPUT: case T_OBJECT: case T_OUTPUT:
	case T_SELECT: case T_TEXTAREA: case T_IMG:
		return true;
	default:
		return false;
	}
}

/* the SVG / MathML adjustments of names */
static const struct { const char *from, *to; } svg_tags[] = {
	{ "altglyph", "altGlyph" }, { "altglyphdef", "altGlyphDef" },
	{ "altglyphitem", "altGlyphItem" }, { "animatecolor", "animateColor" },
	{ "animatemotion", "animateMotion" }, { "animatetransform", "animateTransform" },
	{ "clippath", "clipPath" }, { "feblend", "feBlend" }, { "fecolormatrix", "feColorMatrix" },
	{ "fecomponenttransfer", "feComponentTransfer" }, { "fecomposite", "feComposite" },
	{ "feconvolvematrix", "feConvolveMatrix" }, { "fediffuselighting", "feDiffuseLighting" },
	{ "fedisplacementmap", "feDisplacementMap" }, { "fedistantlight", "feDistantLight" },
	{ "fedropshadow", "feDropShadow" }, { "feflood", "feFlood" }, { "fefunca", "feFuncA" },
	{ "fefuncb", "feFuncB" }, { "fefuncg", "feFuncG" }, { "fefuncr", "feFuncR" },
	{ "fegaussianblur", "feGaussianBlur" }, { "feimage", "feImage" }, { "femerge", "feMerge" },
	{ "femergenode", "feMergeNode" }, { "femorphology", "feMorphology" },
	{ "feoffset", "feOffset" }, { "fepointlight", "fePointLight" },
	{ "fespecularlighting", "feSpecularLighting" }, { "fespotlight", "feSpotLight" },
	{ "fetile", "feTile" }, { "feturbulence", "feTurbulence" },
	{ "foreignobject", "foreignObject" }, { "glyphref", "glyphRef" },
	{ "lineargradient", "linearGradient" }, { "radialgradient", "radialGradient" },
	{ "textpath", "textPath" },
};

static const struct { const char *from, *to; } svg_attrs[] = {
	{ "attributename", "attributeName" }, { "attributetype", "attributeType" },
	{ "basefrequency", "baseFrequency" }, { "baseprofile", "baseProfile" },
	{ "calcmode", "calcMode" }, { "clippathunits", "clipPathUnits" },
	{ "diffuseconstant", "diffuseConstant" }, { "edgemode", "edgeMode" },
	{ "filterunits", "filterUnits" }, { "glyphref", "glyphRef" },
	{ "gradienttransform", "gradientTransform" }, { "gradientunits", "gradientUnits" },
	{ "kernelmatrix", "kernelMatrix" }, { "kernelunitlength", "kernelUnitLength" },
	{ "keypoints", "keyPoints" }, { "keysplines", "keySplines" }, { "keytimes", "keyTimes" },
	{ "lengthadjust", "lengthAdjust" }, { "limitingconeangle", "limitingConeAngle" },
	{ "markerheight", "markerHeight" }, { "markerunits", "markerUnits" },
	{ "markerwidth", "markerWidth" }, { "maskcontentunits", "maskContentUnits" },
	{ "maskunits", "maskUnits" }, { "numoctaves", "numOctaves" },
	{ "pathlength", "pathLength" }, { "patterncontentunits", "patternContentUnits" },
	{ "patterntransform", "patternTransform" }, { "patternunits", "patternUnits" },
	{ "pointsatx", "pointsAtX" }, { "pointsaty", "pointsAtY" }, { "pointsatz", "pointsAtZ" },
	{ "preservealpha", "preserveAlpha" }, { "preserveaspectratio", "preserveAspectRatio" },
	{ "primitiveunits", "primitiveUnits" }, { "refx", "refX" }, { "refy", "refY" },
	{ "repeatcount", "repeatCount" }, { "repeatdur", "repeatDur" },
	{ "requiredextensions", "requiredExtensions" }, { "requiredfeatures", "requiredFeatures" },
	{ "specularconstant", "specularConstant" }, { "specularexponent", "specularExponent" },
	{ "spreadmethod", "spreadMethod" }, { "startoffset", "startOffset" },
	{ "stddeviation", "stdDeviation" }, { "stitchtiles", "stitchTiles" },
	{ "surfacescale", "surfaceScale" }, { "systemlanguage", "systemLanguage" },
	{ "tablevalues", "tableValues" }, { "targetx", "targetX" }, { "targety", "targetY" },
	{ "textlength", "textLength" }, { "viewbox", "viewBox" }, { "viewtarget", "viewTarget" },
	{ "xchannelselector", "xChannelSelector" }, { "ychannelselector", "yChannelSelector" },
	{ "zoomandpan", "zoomAndPan" },
};

static const struct { const char *from, *local; hubbub_ns ns; } foreign_attrs[] = {
	{ "xlink:actuate", "actuate", HUBBUB_NS_XLINK }, { "xlink:arcrole", "arcrole", HUBBUB_NS_XLINK },
	{ "xlink:href", "href", HUBBUB_NS_XLINK }, { "xlink:role", "role", HUBBUB_NS_XLINK },
	{ "xlink:show", "show", HUBBUB_NS_XLINK }, { "xlink:title", "title", HUBBUB_NS_XLINK },
	{ "xlink:type", "type", HUBBUB_NS_XLINK }, { "xml:lang", "lang", HUBBUB_NS_XML },
	{ "xml:space", "space", HUBBUB_NS_XML }, { "xmlns", "xmlns", HUBBUB_NS_XMLNS },
	{ "xmlns:xlink", "xlink", HUBBUB_NS_XMLNS },
};

/* a token of the tree builder: the tokeniser's, with room for adjusted attributes */
typedef struct token {
	hubbub_token t;
	hubbub_attribute *adj;		/**< a copy of the attributes, adjusted */
	uint32_t adj_cap;
} token;

static hubbub_error own_attrs(token *tk)
{
	hubbub_tag *tag = &tk->t.data.tag;
	if (tag->attributes == tk->adj)
		return HUBBUB_OK;
	if (tag->n_attributes > tk->adj_cap) {
		hubbub_attribute *a = realloc(tk->adj, tag->n_attributes * sizeof(*a));
		if (a == NULL)
			return HUBBUB_NOMEM;
		tk->adj = a;
		tk->adj_cap = tag->n_attributes;
	}
	if (tag->n_attributes)
		memcpy(tk->adj, tag->attributes, tag->n_attributes * sizeof(*tk->adj));
	tag->attributes = tk->adj;
	return HUBBUB_OK;
}

static void adjust_mathml_attrs(token *tk)
{
	hubbub_tag *tag = &tk->t.data.tag;
	for (uint32_t i = 0; i < tag->n_attributes; i++) {
		if (str_eq(&tag->attributes[i].name, "definitionurl")) {
			if (own_attrs(tk) != HUBBUB_OK)
				return;
			tag->attributes[i].name.ptr = (const uint8_t *) "definitionURL";
		}
	}
}

static void adjust_svg_attrs(token *tk)
{
	hubbub_tag *tag = &tk->t.data.tag;
	for (uint32_t i = 0; i < tag->n_attributes; i++) {
		for (size_t k = 0; k < sizeof svg_attrs / sizeof svg_attrs[0]; k++) {
			if (str_eq(&tag->attributes[i].name, svg_attrs[k].from)) {
				if (own_attrs(tk) != HUBBUB_OK)
					return;
				tag->attributes[i].name.ptr = (const uint8_t *) svg_attrs[k].to;
				break;
			}
		}
	}
}

static void adjust_foreign_attrs(token *tk)
{
	hubbub_tag *tag = &tk->t.data.tag;
	for (uint32_t i = 0; i < tag->n_attributes; i++) {
		const hubbub_string *n = &tag->attributes[i].name;
		if (n->len < 5 || (n->ptr[0] != 'x'))
			continue;
		for (size_t k = 0; k < sizeof foreign_attrs / sizeof foreign_attrs[0]; k++) {
			if (str_eq(n, foreign_attrs[k].from)) {
				if (own_attrs(tk) != HUBBUB_OK)
					return;
				/* the binding makes the qualified name from the namespace: the
				 * name given is the local part, the prefix is the namespace's */
				tag->attributes[i].ns = foreign_attrs[k].ns;
				tag->attributes[i].name.ptr = (const uint8_t *) foreign_attrs[k].from;
				break;
			}
		}
	}
}

/* create an element for the token (a reference), in namespace ns */
static hubbub_error create_element(hubbub_treebuilder *tb, const hubbub_tag *tag,
		hubbub_ns ns, int type, void **node)
{
	hubbub_tag t = *tag;
	hubbub_error err;

	t.ns = ns;
	/* SVG's element names in their case */
	if (ns == HUBBUB_NS_SVG) {
		for (size_t k = 0; k < sizeof svg_tags / sizeof svg_tags[0]; k++) {
			if (str_eq(&tag->name, svg_tags[k].from)) {
				t.name.ptr = (const uint8_t *) svg_tags[k].to;
				break;
			}
		}
	}
	err = TH->create_element(CTX, &t, node);
	if (err != HUBBUB_OK)
		return err;

	if (ns == HUBBUB_NS_HTML && is_form_associated(type) && tb->form != NULL &&
			!in_template_contents(tb) && !tb->fragment && TH->form_associate != NULL &&
			(type == T_IMG || find_attr(tag, "form") == NULL))
		TH->form_associate(CTX, tb->form, *node);
	return HUBBUB_OK;
}

/* the annotation-xml HTML integration point test (its start tag's encoding) */
static bool annotation_hip(const hubbub_tag *tag)
{
	const hubbub_attribute *a = find_attr(tag, "encoding");
	return a != NULL && (str_eq_ci(a->value.ptr, a->value.len, "text/html") ||
			str_eq_ci(a->value.ptr, a->value.len, "application/xhtml+xml"));
}

/* insert a foreign element for the token: created, inserted (unless only_stack) and pushed */
static hubbub_error insert_foreign(hubbub_treebuilder *tb, const hubbub_tag *tag,
		hubbub_ns ns, bool only_stack, void **out)
{
	loc l = appropriate_place(tb, NULL);
	void *node = NULL;
	int type = name_type(tag->name.ptr, tag->name.len);
	hubbub_error err;

	err = create_element(tb, tag, ns, type, &node);
	if (err == HUBBUB_OK && !only_stack)
		err = insert_node_at(tb, &l, node);
	loc_release(tb, &l);
	if (err != HUBBUB_OK) {
		unref(tb, node);
		return err;
	}
	if (out != NULL)
		*out = node;
	err = push(tb, node, ns, type, tag->name.ptr, tag->name.len,
			ns == HUBBUB_NS_MATHML && type == T_ANNOTATION_XML && annotation_hip(tag));
	if (err != HUBBUB_OK)
		return err;
	cur(tb)->flag = false;
	if (ns == HUBBUB_NS_HTML) {
		if (type == T_OPTION)
			cur(tb)->flag = find_attr(tag, "selected") != NULL;
		else if (type == T_SELECT)
			cur(tb)->flag = find_attr(tag, "multiple") != NULL;
		else if (type == T_SELECTEDCONTENT)
			selectedcontent_inserted(tb);
	}
	return HUBBUB_OK;
}

static inline hubbub_error insert_html(hubbub_treebuilder *tb, const hubbub_tag *tag)
{
	return insert_foreign(tb, tag, HUBBUB_NS_HTML, false, NULL);
}

/* Onyx: declarative shadow DOM -- a <template shadowrootmode> start tag: the template is
 * made and pushed; its contents are a shadow root attached to the adjusted current node,
 * and it is not inserted -- unless that node cannot take one: then as any template */
static hubbub_error insert_shadow_template(hubbub_treebuilder *tb, const hubbub_tag *tag)
{
	loc l = appropriate_place(tb, NULL);
	const elem *host = adjusted_current(tb);
	void *node = NULL;
	hubbub_error err;

	err = create_element(tb, tag, HUBBUB_NS_HTML, T_TEMPLATE, &node);
	if (err == HUBBUB_OK && (host->ns != HUBBUB_NS_HTML ||
			TH->attach_shadow(CTX, host->node, node) != HUBBUB_OK))
		err = insert_node_at(tb, &l, node);
	loc_release(tb, &l);
	if (err != HUBBUB_OK) {
		unref(tb, node);
		return err;
	}
	err = push(tb, node, HUBBUB_NS_HTML, T_TEMPLATE, tag->name.ptr, tag->name.len,
			false);
	if (err != HUBBUB_OK)
		return err;
	cur(tb)->flag = false;
	return HUBBUB_OK;
}

/* an HTML element for a start tag with this name and no attributes */
static hubbub_error insert_html_named(hubbub_treebuilder *tb, const char *name)
{
	hubbub_tag tag;
	memset(&tag, 0, sizeof tag);
	tag.ns = HUBBUB_NS_HTML;
	tag.name.ptr = (const uint8_t *) name;
	tag.name.len = strlen(name);
	return insert_html(tb, &tag);
}

static void set_tokeniser_state(hubbub_treebuilder *tb, hubbub_content_model m)
{
	hubbub_tokeniser_optparams p;
	p.content_model.model = m;
	hubbub_tokeniser_setopt(tb->tokeniser, HUBBUB_TOKENISER_CONTENT_MODEL, &p);
}

/* ---- <selectedcontent> ------------------------------------------------------------ */

/* the nearest ancestor select of the stack entry i (the standard's rules), or -1 */
static int nearest_select(hubbub_treebuilder *tb, int i)
{
	bool optgroup = false;
	for (int k = i - 1; k >= 0; k--) {
		const elem *e = &tb->st[k];
		if (e->ns != HUBBUB_NS_HTML)
			continue;
		if (e->type == T_DATALIST || e->type == T_HR || e->type == T_OPTION)
			return -1;
		if (e->type == T_OPTGROUP) {
			if (optgroup)
				return -1;
			optgroup = true;
		}
		if (e->type == T_SELECT)
			return k;
	}
	return -1;
}

static struct sel_entry *sel_entry(hubbub_treebuilder *tb, const elem *select)
{
	for (uint32_t i = 0; i < tb->n_sels; i++)
		if (tb->sels[i].select == select->node)
			return &tb->sels[i];
	if (tb->n_sels == tb->sels_cap) {
		uint32_t c = tb->sels_cap ? tb->sels_cap * 2 : 8;
		struct sel_entry *n = realloc(tb->sels, c * sizeof(*n));
		if (n == NULL)
			return NULL;
		tb->sels = n;
		tb->sels_cap = c;
	}
	tb->sels[tb->n_sels].select = select->node;
	ref(tb, select->node);
	tb->sels[tb->n_sels].option = NULL;
	tb->sels[tb->n_sels].multiple = select->flag;
	return &tb->sels[tb->n_sels++];
}

/* the option's contents cloned into the selectedcontent (its own contents replaced) */
static void clone_into(hubbub_treebuilder *tb, void *option, void *sc)
{
	void *clone = NULL, *junk = NULL;
	hubbub_tag tag;

	if (flush_text(tb) != HUBBUB_OK)
		return;
	memset(&tag, 0, sizeof tag);
	tag.ns = HUBBUB_NS_HTML;
	tag.name.ptr = (const uint8_t *) "div";
	tag.name.len = 3;
	if (TH->create_element(CTX, &tag, &junk) != HUBBUB_OK)
		return;
	TH->reparent_children(CTX, sc, junk);
	unref(tb, junk);
	if (TH->clone_node(CTX, option, true, &clone) != HUBBUB_OK)
		return;
	TH->reparent_children(CTX, clone, sc);
	unref(tb, clone);
}

/* an option popped off the stack: the selectedcontent elements of its select follow it */
static void option_popped(hubbub_treebuilder *tb, uint32_t i)
{
	int si = nearest_select(tb, (int) i);
	struct sel_entry *se;
	if (si < 0)
		return;
	se = sel_entry(tb, &tb->st[si]);
	if (se == NULL || se->multiple)
		return;
	if (se->option != NULL && !tb->st[i].flag)
		return;		/* not selected */
	unref(tb, se->option);
	se->option = tb->st[i].node;
	ref(tb, se->option);
	for (uint32_t k = 0; k < tb->n_scs; k++)
		if (tb->scs[k].select == se->select)
			clone_into(tb, se->option, tb->scs[k].sc);
}

/* a selectedcontent element inserted (the current node) */
static void selectedcontent_inserted(hubbub_treebuilder *tb)
{
	int si = -1;
	struct sel_entry *se;
	for (int k = (int) tb->n - 2; k >= 0; k--)
		if (is_html(&tb->st[k], T_SELECT)) {
			si = k;
			break;
		}
	if (si < 0)
		return;
	se = sel_entry(tb, &tb->st[si]);
	if (se == NULL)
		return;
	if (tb->n_scs == tb->scs_cap) {
		uint32_t c = tb->scs_cap ? tb->scs_cap * 2 : 4;
		struct sc_entry *n = realloc(tb->scs, c * sizeof(*n));
		if (n == NULL)
			return;
		tb->scs = n;
		tb->scs_cap = c;
	}
	tb->scs[tb->n_scs].select = se->select;
	tb->scs[tb->n_scs].sc = cur(tb)->node;
	ref(tb, cur(tb)->node);
	tb->n_scs++;
	if (se->option != NULL && !se->multiple)
		clone_into(tb, se->option, cur(tb)->node);
}

/* ---- the common algorithms ---------------------------------------------------------- */

static void generate_implied_end_tags(hubbub_treebuilder *tb, int except)
{
	while (tb->n > 0) {
		const elem *e = cur(tb);
		if (e->ns != HUBBUB_NS_HTML || e->type == except)
			return;
		switch (e->type) {
		case T_DD: case T_DT: case T_LI: case T_OPTGROUP: case T_OPTION: case T_P:
		case T_RB: case T_RP: case T_RT: case T_RTC:
			pop(tb);
			break;
		default:
			return;
		}
	}
}

static void generate_all_implied_end_tags(hubbub_treebuilder *tb)
{
	while (tb->n > 0) {
		const elem *e = cur(tb);
		if (e->ns != HUBBUB_NS_HTML)
			return;
		switch (e->type) {
		case T_CAPTION: case T_COLGROUP: case T_DD: case T_DT: case T_LI: case T_OPTGROUP:
		case T_OPTION: case T_P: case T_RB: case T_RP: case T_RT: case T_RTC: case T_TBODY:
		case T_TD: case T_TFOOT: case T_TH: case T_THEAD: case T_TR:
			pop(tb);
			break;
		default:
			return;
		}
	}
}

/* pop until an HTML element of this type has been popped */
static void pop_until(hubbub_treebuilder *tb, int type)
{
	while (tb->n > 0) {
		bool hit = is_html(cur(tb), type);
		pop(tb);
		if (hit)
			return;
	}
}

static void pop_until_heading(hubbub_treebuilder *tb)
{
	while (tb->n > 0) {
		const elem *e = cur(tb);
		bool hit = e->ns == HUBBUB_NS_HTML && e->type >= T_H1 && e->type <= T_H6;
		pop(tb);
		if (hit)
			return;
	}
}

static void close_p(hubbub_treebuilder *tb)
{
	generate_implied_end_tags(tb, T_P);
	pop_until(tb, T_P);
}

static inline void close_p_if_in_button_scope(hubbub_treebuilder *tb)
{
	if (in_scope(tb, T_P, SC_BUTTON))
		close_p(tb);
}

static hubbub_error reconstruct_formatting(hubbub_treebuilder *tb)
{
	int i;
	hubbub_error err;

	if (tb->fn == 0)
		return HUBBUB_OK;
	i = (int) tb->fn - 1;
	if (tb->fl[i].node == NULL || st_find_node(tb, tb->fl[i].node) >= 0)
		return HUBBUB_OK;
	/* rewind */
	while (i > 0) {
		i--;
		if (tb->fl[i].node == NULL || st_find_node(tb, tb->fl[i].node) >= 0) {
			i++;
			break;
		}
	}
	/* advance and create */
	for (; i < (int) tb->fn; i++) {
		fmt *f = &tb->fl[i];
		void *clone = NULL;
		loc l;
		err = TH->clone_node(CTX, f->node, false, &clone);
		if (err != HUBBUB_OK)
			return err;
		l = appropriate_place(tb, NULL);
		err = insert_node_at(tb, &l, clone);
		loc_release(tb, &l);
		if (err != HUBBUB_OK) {
			unref(tb, clone);
			return err;
		}
		err = push(tb, clone, HUBBUB_NS_HTML, f->type, (const uint8_t *) type_name[f->type],
				strlen(type_name[f->type]), false);
		if (err != HUBBUB_OK)
			return err;
		ref(tb, clone);
		unref(tb, f->node);
		f->node = clone;
	}
	return HUBBUB_OK;
}

static void reset_insertion_mode(hubbub_treebuilder *tb)
{
	for (int i = (int) tb->n - 1; i >= 0; i--) {
		bool last = i == 0;
		const elem *node = &tb->st[i];
		if (last && tb->fragment)
			node = &tb->ctx;
		if (node->ns == HUBBUB_NS_HTML) {
			switch (node->type) {
			case T_TD: case T_TH:
				if (!last) { tb->mode = M_IN_CELL; return; }
				break;
			case T_TR: tb->mode = M_IN_ROW; return;
			case T_TBODY: case T_THEAD: case T_TFOOT: tb->mode = M_IN_TABLE_BODY; return;
			case T_CAPTION: tb->mode = M_IN_CAPTION; return;
			case T_COLGROUP: tb->mode = M_IN_COLUMN_GROUP; return;
			case T_TABLE: tb->mode = M_IN_TABLE; return;
			case T_TEMPLATE:
				tb->mode = tb->n_template_modes ?
					tb->template_modes[tb->n_template_modes - 1] : M_IN_TEMPLATE;
				return;
			case T_HEAD:
				if (!last) { tb->mode = M_IN_HEAD; return; }
				break;
			case T_BODY: tb->mode = M_IN_BODY; return;
			case T_FRAMESET: tb->mode = M_IN_FRAMESET; return;
			case T_HTML:
				tb->mode = tb->head == NULL ? M_BEFORE_HEAD : M_AFTER_HEAD;
				return;
			default:
				break;
			}
		}
		if (last) {
			tb->mode = M_IN_BODY;
			return;
		}
	}
	tb->mode = M_IN_BODY;
}

static hubbub_error push_template_mode(hubbub_treebuilder *tb, mode_t_ m)
{
	if (tb->n_template_modes == tb->template_modes_cap) {
		uint32_t c = tb->template_modes_cap ? tb->template_modes_cap * 2 : 16;
		uint8_t *p = realloc(tb->template_modes, c);
		if (p == NULL)
			return HUBBUB_NOMEM;
		tb->template_modes = p;
		tb->template_modes_cap = c;
	}
	tb->template_modes[tb->n_template_modes++] = (uint8_t) m;
	return HUBBUB_OK;
}

static inline void pop_template_mode(hubbub_treebuilder *tb)
{
	if (tb->n_template_modes)
		tb->n_template_modes--;
}

/* ---- the token processing ----------------------------------------------------------- */

#define REPROCESS HUBBUB_REPROCESS

static hubbub_error process(hubbub_treebuilder *tb, token *tk);
static hubbub_error in_body(hubbub_treebuilder *tb, token *tk);
static hubbub_error in_head(hubbub_treebuilder *tb, token *tk);
static hubbub_error in_table(hubbub_treebuilder *tb, token *tk);
static hubbub_error in_template(hubbub_treebuilder *tb, token *tk);

#define IS_START(tk) ((tk)->t.type == HUBBUB_TOKEN_START_TAG)
#define IS_END(tk) ((tk)->t.type == HUBBUB_TOKEN_END_TAG)
#define IS_CHARS(tk) ((tk)->t.type == HUBBUB_TOKEN_CHARACTER)
#define TAG(tk) (&(tk)->t.data.tag)
#define TTYPE(tk) name_type((tk)->t.data.tag.name.ptr, (tk)->t.data.tag.name.len)

/* the leading whitespace of a character token: its length */
static size_t leading_ws(const token *tk)
{
	size_t i = 0;
	const hubbub_string *s = &tk->t.data.character;
	while (i < s->len && is_ws(s->ptr[i]))
		i++;
	return i;
}

/* process the whitespace first, then the rest (reprocessed) */
static hubbub_error split_ws(hubbub_treebuilder *tb, token *tk, size_t ws, bool insert)
{
	hubbub_error err = HUBBUB_OK;
	if (ws > 0 && insert)
		err = insert_chars(tb, tk->t.data.character.ptr, ws);
	tk->t.data.character.ptr += ws;
	tk->t.data.character.len -= ws;
	return err;
}

static bool is_nul_token(const token *tk)
{
	return tk->t.data.character.len == 1 && tk->t.data.character.ptr[0] == 0;
}

/* -- quirks ------------------------------------------------------------------------ */

static const char *const quirky_public_prefixes[] = {
	"+//silmaril//dtd html pro v0r11 19970101//",
	"-//as//dtd html 3.0 aswedit + extensions//",
	"-//advasoft ltd//dtd html 3.0 aswedit + extensions//",
	"-//ietf//dtd html 2.0 level 1//", "-//ietf//dtd html 2.0 level 2//",
	"-//ietf//dtd html 2.0 strict level 1//", "-//ietf//dtd html 2.0 strict level 2//",
	"-//ietf//dtd html 2.0 strict//", "-//ietf//dtd html 2.0//", "-//ietf//dtd html 2.1e//",
	"-//ietf//dtd html 3.0//", "-//ietf//dtd html 3.2 final//", "-//ietf//dtd html 3.2//",
	"-//ietf//dtd html 3//", "-//ietf//dtd html level 0//", "-//ietf//dtd html level 1//",
	"-//ietf//dtd html level 2//", "-//ietf//dtd html level 3//",
	"-//ietf//dtd html strict level 0//", "-//ietf//dtd html strict level 1//",
	"-//ietf//dtd html strict level 2//", "-//ietf//dtd html strict level 3//",
	"-//ietf//dtd html strict//", "-//ietf//dtd html//",
	"-//metrius//dtd metrius presentational//",
	"-//microsoft//dtd internet explorer 2.0 html strict//",
	"-//microsoft//dtd internet explorer 2.0 html//",
	"-//microsoft//dtd internet explorer 2.0 tables//",
	"-//microsoft//dtd internet explorer 3.0 html strict//",
	"-//microsoft//dtd internet explorer 3.0 html//",
	"-//microsoft//dtd internet explorer 3.0 tables//",
	"-//netscape comm. corp.//dtd html//", "-//netscape comm. corp.//dtd strict html//",
	"-//o'reilly and associates//dtd html 2.0//",
	"-//o'reilly and associates//dtd html extended 1.0//",
	"-//o'reilly and associates//dtd html extended relaxed 1.0//",
	"-//sq//dtd html 2.0 hotmetal + extensions//",
	"-//softquad software//dtd hotmetal pro 6.0::19990601::extensions to html 4.0//",
	"-//softquad//dtd hotmetal pro 4.0::19971010::extensions to html 4.0//",
	"-//spyglass//dtd html 2.0 extended//", "-//sun microsystems corp.//dtd hotjava html//",
	"-//sun microsystems corp.//dtd hotjava strict html//",
	"-//w3c//dtd html 3 1995-03-24//", "-//w3c//dtd html 3.2 draft//",
	"-//w3c//dtd html 3.2 final//", "-//w3c//dtd html 3.2//", "-//w3c//dtd html 3.2s draft//",
	"-//w3c//dtd html 4.0 frameset//", "-//w3c//dtd html 4.0 transitional//",
	"-//w3c//dtd html experimental 19960712//", "-//w3c//dtd html experimental 970421//",
	"-//w3c//dtd w3 html//", "-//w3o//dtd w3 html 3.0//", "-//webtechs//dtd mozilla html 2.0//",
	"-//webtechs//dtd mozilla html//",
};

static hubbub_quirks_mode doctype_quirks(const hubbub_doctype *d)
{
	const uint8_t *pub = d->public_id.ptr;
	size_t pl = d->public_id.len;
	bool sys_missing = d->system_missing;

	if (d->force_quirks || d->name.ptr == NULL || !str_eq_ci(d->name.ptr, d->name.len, "html"))
		return HUBBUB_QUIRKS_MODE_FULL;
	if (!d->public_missing) {
		if (str_eq_ci(pub, pl, "-//w3o//dtd w3 html strict 3.0//en//") ||
				str_eq_ci(pub, pl, "-/w3c/dtd html 4.0 transitional/en") ||
				str_eq_ci(pub, pl, "html"))
			return HUBBUB_QUIRKS_MODE_FULL;
		for (size_t i = 0; i < sizeof quirky_public_prefixes / sizeof quirky_public_prefixes[0]; i++)
			if (starts_ci(pub, pl, quirky_public_prefixes[i]))
				return HUBBUB_QUIRKS_MODE_FULL;
	}
	if (!d->system_missing && str_eq_ci(d->system_id.ptr, d->system_id.len,
			"http://www.ibm.com/data/dtd/v11/ibmxhtml1-transitional.dtd"))
		return HUBBUB_QUIRKS_MODE_FULL;
	if (!d->public_missing) {
		if (sys_missing && (starts_ci(pub, pl, "-//w3c//dtd html 4.01 frameset//") ||
				starts_ci(pub, pl, "-//w3c//dtd html 4.01 transitional//")))
			return HUBBUB_QUIRKS_MODE_FULL;
		if (starts_ci(pub, pl, "-//w3c//dtd xhtml 1.0 frameset//") ||
				starts_ci(pub, pl, "-//w3c//dtd xhtml 1.0 transitional//"))
			return HUBBUB_QUIRKS_MODE_LIMITED;
		if (!sys_missing && (starts_ci(pub, pl, "-//w3c//dtd html 4.01 frameset//") ||
				starts_ci(pub, pl, "-//w3c//dtd html 4.01 transitional//")))
			return HUBBUB_QUIRKS_MODE_LIMITED;
	}
	return HUBBUB_QUIRKS_MODE_NONE;
}

static void set_quirks(hubbub_treebuilder *tb, hubbub_quirks_mode m)
{
	tb->quirks = m == HUBBUB_QUIRKS_MODE_FULL;
	if (TH->set_quirks_mode != NULL)
		TH->set_quirks_mode(CTX, m);
}

/* -- the modes ---------------------------------------------------------------------- */

static hubbub_error initial(hubbub_treebuilder *tb, token *tk)
{
	switch (tk->t.type) {
	case HUBBUB_TOKEN_CHARACTER: {
		size_t ws = leading_ws(tk);
		split_ws(tb, tk, ws, false);
		if (tk->t.data.character.len == 0)
			return HUBBUB_OK;
		break;
	}
	case HUBBUB_TOKEN_COMMENT:
		return insert_comment_at(tb, &tk->t.data.comment, NULL, tb->document);
	case HUBBUB_TOKEN_DOCTYPE: {
		void *d = NULL;
		hubbub_error err;
		hubbub_doctype dt = tk->t.data.doctype;
		loc l = { tb->document, NULL };
		if (dt.name.ptr == NULL)
			dt.name.ptr = (const uint8_t *) "";
		err = TH->create_doctype(CTX, &dt, &d);
		if (err == HUBBUB_OK) {
			err = insert_node_at(tb, &l, d);
			unref(tb, d);
		}
		set_quirks(tb, doctype_quirks(&tk->t.data.doctype));
		tb->mode = M_BEFORE_HTML;
		return err;
	}
	default:
		break;
	}
	set_quirks(tb, HUBBUB_QUIRKS_MODE_FULL);
	tb->mode = M_BEFORE_HTML;
	return REPROCESS;
}

static hubbub_error before_html(hubbub_treebuilder *tb, token *tk)
{
	switch (tk->t.type) {
	case HUBBUB_TOKEN_DOCTYPE:
		return HUBBUB_OK;
	case HUBBUB_TOKEN_COMMENT:
		return insert_comment_at(tb, &tk->t.data.comment, NULL, tb->document);
	case HUBBUB_TOKEN_CHARACTER: {
		size_t ws = leading_ws(tk);
		split_ws(tb, tk, ws, false);
		if (tk->t.data.character.len == 0)
			return HUBBUB_OK;
		break;
	}
	case HUBBUB_TOKEN_START_TAG:
		if (TTYPE(tk) == T_HTML) {
			void *node = NULL;
			loc l = { tb->document, NULL };
			hubbub_error err = create_element(tb, TAG(tk), HUBBUB_NS_HTML, T_HTML, &node);
			if (err == HUBBUB_OK)
				err = insert_node_at(tb, &l, node);
			if (err == HUBBUB_OK)
				err = push(tb, node, HUBBUB_NS_HTML, T_HTML, NULL, 0, false);
			tb->mode = M_BEFORE_HEAD;
			return err;
		}
		break;
	case HUBBUB_TOKEN_END_TAG: {
		int t = TTYPE(tk);
		if (t != T_HEAD && t != T_BODY && t != T_HTML && t != T_BR)
			return HUBBUB_OK;
		break;
	}
	default:
		break;
	}
	{
		hubbub_tag tag;
		void *node = NULL;
		loc l = { tb->document, NULL };
		hubbub_error err;
		memset(&tag, 0, sizeof tag);
		tag.name.ptr = (const uint8_t *) "html";
		tag.name.len = 4;
		err = create_element(tb, &tag, HUBBUB_NS_HTML, T_HTML, &node);
		if (err == HUBBUB_OK)
			err = insert_node_at(tb, &l, node);
		if (err == HUBBUB_OK)
			err = push(tb, node, HUBBUB_NS_HTML, T_HTML, NULL, 0, false);
		if (err != HUBBUB_OK)
			return err;
	}
	tb->mode = M_BEFORE_HEAD;
	return REPROCESS;
}

static hubbub_error before_head(hubbub_treebuilder *tb, token *tk)
{
	hubbub_error err;
	switch (tk->t.type) {
	case HUBBUB_TOKEN_CHARACTER: {
		size_t ws = leading_ws(tk);
		split_ws(tb, tk, ws, false);
		if (tk->t.data.character.len == 0)
			return HUBBUB_OK;
		break;
	}
	case HUBBUB_TOKEN_COMMENT:
		return insert_comment(tb, &tk->t.data.comment);
	case HUBBUB_TOKEN_DOCTYPE:
		return HUBBUB_OK;
	case HUBBUB_TOKEN_START_TAG: {
		int t = TTYPE(tk);
		if (t == T_HTML)
			return in_body(tb, tk);
		if (t == T_HEAD) {
			err = insert_html(tb, TAG(tk));
			if (err != HUBBUB_OK)
				return err;
			tb->head = cur(tb)->node;
			ref(tb, tb->head);
			tb->mode = M_IN_HEAD;
			return HUBBUB_OK;
		}
		break;
	}
	case HUBBUB_TOKEN_END_TAG: {
		int t = TTYPE(tk);
		if (t != T_HEAD && t != T_BODY && t != T_HTML && t != T_BR)
			return HUBBUB_OK;
		break;
	}
	default:
		break;
	}
	err = insert_html_named(tb, "head");
	if (err != HUBBUB_OK)
		return err;
	tb->head = cur(tb)->node;
	ref(tb, tb->head);
	tb->mode = M_IN_HEAD;
	return REPROCESS;
}

/* <meta charset> / http-equiv content-type: the encoding may change */
static hubbub_error meta_encoding(hubbub_treebuilder *tb, const hubbub_tag *tag)
{
	static uint16_t utf16, utf16be, utf16le;
	uint16_t charset_enc = 0, content_type_enc = 0;
	const hubbub_attribute *a;

	if (TH->encoding_change == NULL)
		return HUBBUB_OK;
	if (utf16 == 0) {
		utf16 = parserutils_charset_mibenum_from_name("utf-16", SLEN("utf-16"));
		utf16be = parserutils_charset_mibenum_from_name("utf-16be", SLEN("utf-16be"));
		utf16le = parserutils_charset_mibenum_from_name("utf-16le", SLEN("utf-16le"));
	}
	a = find_attr(tag, "charset");
	if (a != NULL)
		charset_enc = parserutils_charset_mibenum_from_name((const char *) a->value.ptr,
				a->value.len);
	if (charset_enc == 0) {
		const hubbub_attribute *he = find_attr(tag, "http-equiv");
		a = find_attr(tag, "content");
		if (he != NULL && a != NULL && str_eq_ci(he->value.ptr, he->value.len, "content-type"))
			content_type_enc = hubbub_charset_parse_content(a->value.ptr, a->value.len);
		charset_enc = content_type_enc;
	}
	if (charset_enc != 0) {
		hubbub_charset_fix_charset(&charset_enc);
		if (charset_enc == utf16le || charset_enc == utf16be || charset_enc == utf16)
			charset_enc = parserutils_charset_mibenum_from_name("UTF-8", SLEN("UTF-8"));
		return TH->encoding_change(CTX, parserutils_charset_mibenum_to_name(charset_enc));
	}
	return HUBBUB_OK;
}

/* the generic raw text / RCDATA element parsing algorithm */
static hubbub_error generic_text(hubbub_treebuilder *tb, token *tk, hubbub_content_model m)
{
	hubbub_error err = insert_html(tb, TAG(tk));
	if (err != HUBBUB_OK)
		return err;
	set_tokeniser_state(tb, m);
	tb->original_mode = tb->mode;
	tb->mode = M_TEXT;
	return HUBBUB_OK;
}

static hubbub_error in_head(hubbub_treebuilder *tb, token *tk)
{
	hubbub_error err;
	switch (tk->t.type) {
	case HUBBUB_TOKEN_CHARACTER: {
		size_t ws = leading_ws(tk);
		err = split_ws(tb, tk, ws, true);
		if (err != HUBBUB_OK || tk->t.data.character.len == 0)
			return err;
		break;
	}
	case HUBBUB_TOKEN_COMMENT:
		return insert_comment(tb, &tk->t.data.comment);
	case HUBBUB_TOKEN_DOCTYPE:
		return HUBBUB_OK;
	case HUBBUB_TOKEN_START_TAG:
		switch (TTYPE(tk)) {
		case T_HTML:
			return in_body(tb, tk);
		case T_BASE: case T_BASEFONT: case T_BGSOUND: case T_LINK:
			err = insert_html(tb, TAG(tk));
			pop(tb);
			return err;
		case T_META:
			err = insert_html(tb, TAG(tk));
			pop(tb);
			if (err != HUBBUB_OK)
				return err;
			return meta_encoding(tb, TAG(tk));
		case T_TITLE:
			return generic_text(tb, tk, HUBBUB_CONTENT_MODEL_RCDATA);
		case T_NOSCRIPT:
			if (tb->enable_scripting)
				return generic_text(tb, tk, HUBBUB_CONTENT_MODEL_CDATA);
			err = insert_html(tb, TAG(tk));
			tb->mode = M_IN_HEAD_NOSCRIPT;
			return err;
		case T_NOFRAMES: case T_STYLE:
			return generic_text(tb, tk, HUBBUB_CONTENT_MODEL_CDATA);
		case T_SCRIPT:
			err = insert_html(tb, TAG(tk));
			if (err != HUBBUB_OK)
				return err;
			set_tokeniser_state(tb, HUBBUB_CONTENT_MODEL_SCRIPTDATA);
			tb->original_mode = tb->mode;
			tb->mode = M_TEXT;
			return HUBBUB_OK;
		case T_TEMPLATE:
			/* Onyx: declarative shadow DOM (the document parser only) */
			if (TH->attach_shadow != NULL && !tb->fragment && tb->n > 1 &&
					find_attr(TAG(tk), "shadowrootmode") != NULL)
				err = insert_shadow_template(tb, TAG(tk));
			else
				err = insert_html(tb, TAG(tk));
			if (err != HUBBUB_OK)
				return err;
			err = insert_marker(tb);
			tb->frameset_ok = false;
			tb->mode = M_IN_TEMPLATE;
			if (err == HUBBUB_OK)
				err = push_template_mode(tb, M_IN_TEMPLATE);
			return err;
		case T_HEAD:
			return HUBBUB_OK;
		default:
			break;
		}
		break;
	case HUBBUB_TOKEN_END_TAG:
		switch (TTYPE(tk)) {
		case T_HEAD:
			pop(tb);
			tb->mode = M_AFTER_HEAD;
			return HUBBUB_OK;
		case T_BODY: case T_HTML: case T_BR:
			break;
		case T_TEMPLATE:
			if (!has_template(tb))
				return HUBBUB_OK;
			generate_all_implied_end_tags(tb);
			pop_until(tb, T_TEMPLATE);
			clear_to_marker(tb);
			pop_template_mode(tb);
			reset_insertion_mode(tb);
			return HUBBUB_OK;
		default:
			return HUBBUB_OK;
		}
		break;
	default:
		break;
	}
	pop(tb);
	tb->mode = M_AFTER_HEAD;
	return REPROCESS;
}

static hubbub_error in_head_noscript(hubbub_treebuilder *tb, token *tk)
{
	switch (tk->t.type) {
	case HUBBUB_TOKEN_DOCTYPE:
		return HUBBUB_OK;
	case HUBBUB_TOKEN_START_TAG:
		switch (TTYPE(tk)) {
		case T_HTML:
			return in_body(tb, tk);
		case T_BASEFONT: case T_BGSOUND: case T_LINK: case T_META: case T_NOFRAMES:
		case T_STYLE:
			return in_head(tb, tk);
		case T_HEAD: case T_NOSCRIPT:
			return HUBBUB_OK;
		default:
			break;
		}
		break;
	case HUBBUB_TOKEN_END_TAG:
		switch (TTYPE(tk)) {
		case T_NOSCRIPT:
			pop(tb);
			tb->mode = M_IN_HEAD;
			return HUBBUB_OK;
		case T_BR:
			break;
		default:
			return HUBBUB_OK;
		}
		break;
	case HUBBUB_TOKEN_CHARACTER: {
		size_t ws = leading_ws(tk);
		if (ws > 0) {
			hubbub_error err = split_ws(tb, tk, ws, true);
			if (err != HUBBUB_OK || tk->t.data.character.len == 0)
				return err;
		}
		break;
	}
	case HUBBUB_TOKEN_COMMENT:
		return in_head(tb, tk);
	default:
		break;
	}
	pop(tb);
	tb->mode = M_IN_HEAD;
	return REPROCESS;
}

static hubbub_error after_head(hubbub_treebuilder *tb, token *tk)
{
	hubbub_error err;
	switch (tk->t.type) {
	case HUBBUB_TOKEN_CHARACTER: {
		size_t ws = leading_ws(tk);
		err = split_ws(tb, tk, ws, true);
		if (err != HUBBUB_OK || tk->t.data.character.len == 0)
			return err;
		break;
	}
	case HUBBUB_TOKEN_COMMENT:
		return insert_comment(tb, &tk->t.data.comment);
	case HUBBUB_TOKEN_DOCTYPE:
		return HUBBUB_OK;
	case HUBBUB_TOKEN_START_TAG:
		switch (TTYPE(tk)) {
		case T_HTML:
			return in_body(tb, tk);
		case T_BODY:
			err = insert_html(tb, TAG(tk));
			tb->frameset_ok = false;
			tb->mode = M_IN_BODY;
			return err;
		case T_FRAMESET:
			err = insert_html(tb, TAG(tk));
			tb->mode = M_IN_FRAMESET;
			return err;
		case T_BASE: case T_BASEFONT: case T_BGSOUND: case T_LINK: case T_META:
		case T_NOFRAMES: case T_SCRIPT: case T_STYLE: case T_TEMPLATE: case T_TITLE: {
			int i;
			if (tb->head == NULL)
				break;
			ref(tb, tb->head);
			err = push(tb, tb->head, HUBBUB_NS_HTML, T_HEAD, NULL, 0, false);
			if (err != HUBBUB_OK)
				return err;
			err = in_head(tb, tk);
			i = st_find_node(tb, tb->head);
			if (i >= 0)
				st_remove(tb, i);
			return err;
		}
		case T_HEAD:
			return HUBBUB_OK;
		default:
			break;
		}
		break;
	case HUBBUB_TOKEN_END_TAG:
		switch (TTYPE(tk)) {
		case T_TEMPLATE:
			return in_head(tb, tk);
		case T_BODY: case T_HTML: case T_BR:
			break;
		default:
			return HUBBUB_OK;
		}
		break;
	default:
		break;
	}
	err = insert_html_named(tb, "body");
	tb->frameset_ok = true;
	tb->mode = M_IN_BODY;
	return err != HUBBUB_OK ? err : REPROCESS;
}

/* stack elements other than these at EOF / </body> are parse errors (nothing to do) */

/* the adoption agency algorithm; returns true if "any other end tag" must run instead */
static hubbub_error adoption_agency(hubbub_treebuilder *tb, int subject, bool *other)
{
	hubbub_error err;
	*other = false;

	if (is_html(cur(tb), subject) && fl_find_node(tb, cur(tb)->node) < 0) {
		pop(tb);
		return HUBBUB_OK;
	}
	for (int outer = 0; outer < 8; outer++) {
		int fi = -1, si, fbi = -1, bookmark;
		void *fe_node, *common, *last_node, *node_node;
		int inner = 0;
		loc l;

		for (int i = (int) tb->fn - 1; i >= 0; i--) {
			if (tb->fl[i].node == NULL)
				break;
			if (tb->fl[i].type == subject) {
				fi = i;
				break;
			}
		}
		if (fi < 0) {
			*other = true;
			return HUBBUB_OK;
		}
		fe_node = tb->fl[fi].node;
		si = st_find_node(tb, fe_node);
		if (si < 0) {
			fl_remove(tb, fi);
			return HUBBUB_OK;
		}
		if (!node_in_scope(tb, fe_node, SC_DEFAULT))
			return HUBBUB_OK;
		for (int i = si + 1; i < (int) tb->n; i++) {
			if (is_special(&tb->st[i])) {
				fbi = i;
				break;
			}
		}
		if (fbi < 0) {
			while ((int) tb->n > si)
				pop(tb);
			fi = fl_find_node(tb, fe_node);
			if (fi >= 0)
				fl_remove(tb, fi);
			return HUBBUB_OK;
		}
		err = flush_text(tb);
		if (err != HUBBUB_OK)
			return err;

		common = tb->st[si - 1].node;
		bookmark = fi;
		{
			/* node and last node: indexes into the stack (it changes below) */
			int ni = fbi, li = fbi;
			void *furthest = tb->st[fbi].node;
			last_node = furthest;
			ref(tb, last_node);
			for (;;) {
				int nfi;
				void *clone = NULL, *res = NULL;
				inner++;
				ni--;
				node_node = tb->st[ni].node;
				if (node_node == fe_node)
					break;
				nfi = fl_find_node(tb, node_node);
				if (inner > 3 && nfi >= 0) {
					fl_remove(tb, nfi);
					if (nfi < bookmark)
						bookmark--;
					nfi = -1;
				}
				if (nfi < 0) {
					st_remove(tb, ni);
					continue;
				}
				/* a new element for node's token */
				err = TH->clone_node(CTX, node_node, false, &clone);
				if (err != HUBBUB_OK) {
					unref(tb, last_node);
					return err;
				}
				ref(tb, clone);
				unref(tb, tb->fl[nfi].node);
				tb->fl[nfi].node = clone;
				ref(tb, clone);
				unref(tb, tb->st[ni].node);
				tb->st[ni].node = clone;
				node_node = clone;
				if (last_node == furthest)
					bookmark = nfi + 1;
				/* last node into node */
				{
					void *p = get_parent(tb, last_node);
					if (p != NULL) {
						TH->remove_child(CTX, p, last_node, &res);
						unref(tb, res);
						unref(tb, p);
						res = NULL;
					}
				}
				err = TH->append_child(CTX, node_node, last_node, &res);
				if (err == HUBBUB_OK)
					unref(tb, res);
				unref(tb, last_node);
				last_node = node_node;
				ref(tb, last_node);
				unref(tb, clone);
				li = ni;
			}
			(void) li;

			/* last node at the appropriate place for common ancestor */
			{
				elem ce;
				int ci = st_find_node(tb, common);
				void *res = NULL, *p;
				if (ci >= 0) {
					l = appropriate_place(tb, &tb->st[ci]);
				} else {
					memset(&ce, 0, sizeof ce);
					ce.node = common;
					ce.ns = HUBBUB_NS_HTML;
					l = appropriate_place(tb, &ce);
				}
				p = get_parent(tb, last_node);
				if (p != NULL) {
					TH->remove_child(CTX, p, last_node, &res);
					unref(tb, res);
					unref(tb, p);
				}
				err = insert_node_at(tb, &l, last_node);
				loc_release(tb, &l);
				unref(tb, last_node);
				if (err != HUBBUB_OK)
					return err;
			}

			/* a new element for the formatting element's token, in furthest block */
			{
				void *clone = NULL, *res = NULL;
				fmt nf;
				elem ne;
				int fbi2;
				err = TH->clone_node(CTX, fe_node, false, &clone);
				if (err != HUBBUB_OK)
					return err;
				if (TH->reparent_children(CTX, furthest, clone) != HUBBUB_OK) {
					unref(tb, clone);
					return HUBBUB_UNKNOWN;
				}
				err = TH->append_child(CTX, furthest, clone, &res);
				if (err == HUBBUB_OK)
					unref(tb, res);

				/* the list: the formatting element out, the clone at the bookmark */
				fi = fl_find_node(tb, fe_node);
				nf = tb->fl[fi];
				nf.node = clone;
				ref(tb, clone);
				unref(tb, fe_node);
				memmove(&tb->fl[fi], &tb->fl[fi + 1], (tb->fn - fi - 1) * sizeof(fmt));
				tb->fn--;
				if (bookmark > fi)
					bookmark--;
				if (bookmark > (int) tb->fn)
					bookmark = tb->fn;
				if (!fl_reserve(tb)) {
					free(nf.attrs);
					unref(tb, clone);
					unref(tb, clone);
					return HUBBUB_NOMEM;
				}
				memmove(&tb->fl[bookmark + 1], &tb->fl[bookmark],
						(tb->fn - bookmark) * sizeof(fmt));
				tb->fl[bookmark] = nf;
				tb->fn++;

				/* the stack: the formatting element out, the clone below furthest */
				si = st_find_node(tb, fe_node);
				ne = tb->st[si];
				ne.node = clone;	/* the creation reference */
				ne.heap = NULL;
				st_remove(tb, si);
				fbi2 = st_find_node(tb, furthest);
				err = st_insert(tb, fbi2 + 1, &ne);
				if (err != HUBBUB_OK)
					return err;
			}
		}
	}
	return HUBBUB_OK;
}

/* in body's "any other end tag" */
static hubbub_error any_other_end_tag(hubbub_treebuilder *tb, token *tk)
{
	const hubbub_string *name = &TAG(tk)->name;
	for (int i = (int) tb->n - 1; i >= 0; i--) {
		const elem *e = &tb->st[i];
		const char *en = e->ns == HUBBUB_NS_HTML ? elem_name(e) : NULL;
		if (en != NULL && strlen(en) == name->len && memcmp(en, name->ptr, name->len) == 0) {
			int t = e->type;
			if (t != T_OTHER) {
				generate_implied_end_tags(tb, t);
			} else {
				/* (an unknown name is never one of the implied end tags) */
				generate_implied_end_tags(tb, -1);
			}
			while ((int) tb->n > i)
				pop(tb);
			return HUBBUB_OK;
		}
		if (is_special(e))
			return HUBBUB_OK;
	}
	return HUBBUB_OK;
}

static bool body_end_ok(hubbub_treebuilder *tb)
{
	return in_scope(tb, T_BODY, SC_DEFAULT);
}

static hubbub_error in_body_start(hubbub_treebuilder *tb, token *tk)
{
	hubbub_error err;
	int t = TTYPE(tk);

	switch (t) {
	case T_HTML:
		if (has_template(tb))
			return HUBBUB_OK;
		if (TH->add_attributes != NULL && TAG(tk)->n_attributes)
			return TH->add_attributes(CTX, tb->st[0].node, TAG(tk)->attributes,
					TAG(tk)->n_attributes);
		return HUBBUB_OK;
	case T_BASE: case T_BASEFONT: case T_BGSOUND: case T_LINK: case T_META: case T_NOFRAMES:
	case T_SCRIPT: case T_STYLE: case T_TEMPLATE: case T_TITLE:
		return in_head(tb, tk);
	case T_BODY:
		if (tb->n < 2 || !is_html(&tb->st[1], T_BODY) || has_template(tb))
			return HUBBUB_OK;
		tb->frameset_ok = false;
		if (TH->add_attributes != NULL && TAG(tk)->n_attributes)
			return TH->add_attributes(CTX, tb->st[1].node, TAG(tk)->attributes,
					TAG(tk)->n_attributes);
		return HUBBUB_OK;
	case T_FRAMESET:
		if (tb->n < 2 || !is_html(&tb->st[1], T_BODY))
			return HUBBUB_OK;
		if (!tb->frameset_ok)
			return HUBBUB_OK;
		{
			void *p, *res = NULL;
			err = flush_text(tb);
			if (err != HUBBUB_OK)
				return err;
			p = get_parent(tb, tb->st[1].node);
			if (p != NULL) {
				TH->remove_child(CTX, p, tb->st[1].node, &res);
				unref(tb, res);
				unref(tb, p);
			}
		}
		while (tb->n > 1)
			pop(tb);
		err = insert_html(tb, TAG(tk));
		tb->mode = M_IN_FRAMESET;
		return err;
	case T_ADDRESS: case T_ARTICLE: case T_ASIDE: case T_BLOCKQUOTE: case T_CENTER:
	case T_DETAILS: case T_DIALOG: case T_DIR: case T_DIV: case T_DL: case T_FIELDSET:
	case T_FIGCAPTION: case T_FIGURE: case T_FOOTER: case T_HEADER: case T_HGROUP:
	case T_MAIN: case T_MENU: case T_NAV: case T_OL: case T_P: case T_SEARCH:
	case T_SECTION: case T_SUMMARY: case T_UL:
		close_p_if_in_button_scope(tb);
		return insert_html(tb, TAG(tk));
	case T_H1: case T_H2: case T_H3: case T_H4: case T_H5: case T_H6:
		close_p_if_in_button_scope(tb);
		if (cur(tb)->ns == HUBBUB_NS_HTML && cur(tb)->type >= T_H1 && cur(tb)->type <= T_H6)
			pop(tb);
		return insert_html(tb, TAG(tk));
	case T_PRE: case T_LISTING:
		close_p_if_in_button_scope(tb);
		err = insert_html(tb, TAG(tk));
		tb->skip_lf = true;
		tb->frameset_ok = false;
		return err;
	case T_FORM:
		if (tb->form != NULL && !in_template_contents(tb))
			return HUBBUB_OK;
		close_p_if_in_button_scope(tb);
		err = insert_html(tb, TAG(tk));
		if (err == HUBBUB_OK && !in_template_contents(tb)) {
			unref(tb, tb->form);
			tb->form = cur(tb)->node;
			ref(tb, tb->form);
		}
		return err;
	case T_LI:
		tb->frameset_ok = false;
		for (int i = (int) tb->n - 1; i >= 0; i--) {
			const elem *e = &tb->st[i];
			if (is_html(e, T_LI)) {
				generate_implied_end_tags(tb, T_LI);
				pop_until(tb, T_LI);
				break;
			}
			if (is_special(e) && !is_html(e, T_ADDRESS) && !is_html(e, T_DIV) &&
					!is_html(e, T_P))
				break;
		}
		close_p_if_in_button_scope(tb);
		return insert_html(tb, TAG(tk));
	case T_DD: case T_DT:
		tb->frameset_ok = false;
		for (int i = (int) tb->n - 1; i >= 0; i--) {
			const elem *e = &tb->st[i];
			if (is_html(e, T_DD) || is_html(e, T_DT)) {
				int et = e->type;
				generate_implied_end_tags(tb, et);
				pop_until(tb, et);
				break;
			}
			if (is_special(e) && !is_html(e, T_ADDRESS) && !is_html(e, T_DIV) &&
					!is_html(e, T_P))
				break;
		}
		close_p_if_in_button_scope(tb);
		return insert_html(tb, TAG(tk));
	case T_PLAINTEXT:
		close_p_if_in_button_scope(tb);
		err = insert_html(tb, TAG(tk));
		set_tokeniser_state(tb, HUBBUB_CONTENT_MODEL_PLAINTEXT);
		return err;
	case T_BUTTON:
		if (in_scope(tb, T_BUTTON, SC_DEFAULT)) {
			generate_implied_end_tags(tb, -1);
			pop_until(tb, T_BUTTON);
		}
		err = reconstruct_formatting(tb);
		if (err != HUBBUB_OK)
			return err;
		err = insert_html(tb, TAG(tk));
		tb->frameset_ok = false;
		return err;
	case T_A:
		for (int i = (int) tb->fn - 1; i >= 0; i--) {
			if (tb->fl[i].node == NULL)
				break;
			if (tb->fl[i].type == T_A) {
				void *a = tb->fl[i].node;
				bool other;
				int k;
				ref(tb, a);
				err = adoption_agency(tb, T_A, &other);
				if (err != HUBBUB_OK) {
					unref(tb, a);
					return err;
				}
				k = fl_find_node(tb, a);
				if (k >= 0)
					fl_remove(tb, k);
				k = st_find_node(tb, a);
				if (k >= 0)
					st_remove(tb, k);
				unref(tb, a);
				break;
			}
		}
		/* fall through */
	case T_B: case T_BIG: case T_CODE: case T_EM: case T_FONT: case T_I: case T_S:
	case T_SMALL: case T_STRIKE: case T_STRONG: case T_TT: case T_U:
		err = reconstruct_formatting(tb);
		if (err != HUBBUB_OK)
			return err;
		err = insert_html(tb, TAG(tk));
		if (err != HUBBUB_OK)
			return err;
		return push_formatting(tb, cur(tb)->node, t, TAG(tk));
	case T_NOBR:
		err = reconstruct_formatting(tb);
		if (err != HUBBUB_OK)
			return err;
		if (in_scope(tb, T_NOBR, SC_DEFAULT)) {
			bool other;
			err = adoption_agency(tb, T_NOBR, &other);
			if (err == HUBBUB_OK && other)
				err = any_other_end_tag(tb, tk);
			if (err != HUBBUB_OK)
				return err;
			err = reconstruct_formatting(tb);
			if (err != HUBBUB_OK)
				return err;
		}
		err = insert_html(tb, TAG(tk));
		if (err != HUBBUB_OK)
			return err;
		return push_formatting(tb, cur(tb)->node, t, TAG(tk));
	case T_APPLET: case T_MARQUEE: case T_OBJECT:
		err = reconstruct_formatting(tb);
		if (err != HUBBUB_OK)
			return err;
		err = insert_html(tb, TAG(tk));
		if (err != HUBBUB_OK)
			return err;
		tb->frameset_ok = false;
		return insert_marker(tb);
	case T_TABLE:
		if (!tb->quirks)
			close_p_if_in_button_scope(tb);
		err = insert_html(tb, TAG(tk));
		tb->frameset_ok = false;
		tb->mode = M_IN_TABLE;
		return err;
	case T_AREA: case T_BR: case T_EMBED: case T_IMG: case T_KEYGEN: case T_WBR:
		err = reconstruct_formatting(tb);
		if (err != HUBBUB_OK)
			return err;
		err = insert_html(tb, TAG(tk));
		pop(tb);
		tb->frameset_ok = false;
		return err;
	case T_INPUT: {
		const hubbub_attribute *type;
		if (tb->fragment && is_html(&tb->ctx, T_SELECT))
			return HUBBUB_OK;
		if (in_scope(tb, T_SELECT, SC_DEFAULT))
			pop_until(tb, T_SELECT);
		err = reconstruct_formatting(tb);
		if (err != HUBBUB_OK)
			return err;
		err = insert_html(tb, TAG(tk));
		pop(tb);
		type = find_attr(TAG(tk), "type");
		if (type == NULL || !str_eq_ci(type->value.ptr, type->value.len, "hidden"))
			tb->frameset_ok = false;
		return err;
	}
	case T_PARAM: case T_SOURCE: case T_TRACK:
		err = insert_html(tb, TAG(tk));
		pop(tb);
		return err;
	case T_HR:
		close_p_if_in_button_scope(tb);
		if (in_scope(tb, T_SELECT, SC_DEFAULT))
			generate_implied_end_tags(tb, -1);
		err = insert_html(tb, TAG(tk));
		pop(tb);
		tb->frameset_ok = false;
		return err;
	case T_IMAGE:
		TAG(tk)->name.ptr = (const uint8_t *) "img";
		TAG(tk)->name.len = 3;
		return REPROCESS;
	case T_TEXTAREA:
		err = insert_html(tb, TAG(tk));
		if (err != HUBBUB_OK)
			return err;
		tb->skip_lf = true;
		set_tokeniser_state(tb, HUBBUB_CONTENT_MODEL_RCDATA);
		tb->original_mode = tb->mode;
		tb->frameset_ok = false;
		tb->mode = M_TEXT;
		return HUBBUB_OK;
	case T_XMP:
		close_p_if_in_button_scope(tb);
		err = reconstruct_formatting(tb);
		if (err != HUBBUB_OK)
			return err;
		tb->frameset_ok = false;
		return generic_text(tb, tk, HUBBUB_CONTENT_MODEL_CDATA);
	case T_IFRAME:
		tb->frameset_ok = false;
		return generic_text(tb, tk, HUBBUB_CONTENT_MODEL_CDATA);
	case T_NOEMBED:
		return generic_text(tb, tk, HUBBUB_CONTENT_MODEL_CDATA);
	case T_NOSCRIPT:
		if (tb->enable_scripting)
			return generic_text(tb, tk, HUBBUB_CONTENT_MODEL_CDATA);
		break;
	case T_SELECT:
		if (tb->fragment && is_html(&tb->ctx, T_SELECT))
			return HUBBUB_OK;
		if (in_scope(tb, T_SELECT, SC_DEFAULT)) {
			pop_until(tb, T_SELECT);
			return HUBBUB_OK;
		}
		err = reconstruct_formatting(tb);
		if (err != HUBBUB_OK)
			return err;
		err = insert_html(tb, TAG(tk));
		tb->frameset_ok = false;
		return err;
	case T_OPTION:
		if (in_scope(tb, T_SELECT, SC_DEFAULT)) {
			generate_implied_end_tags(tb, T_OPTGROUP);
		} else if (is_html(cur(tb), T_OPTION)) {
			pop(tb);
		}
		err = reconstruct_formatting(tb);
		if (err != HUBBUB_OK)
			return err;
		return insert_html(tb, TAG(tk));
	case T_OPTGROUP:
		if (in_scope(tb, T_SELECT, SC_DEFAULT)) {
			generate_implied_end_tags(tb, -1);
		} else if (is_html(cur(tb), T_OPTION)) {
			pop(tb);
		}
		err = reconstruct_formatting(tb);
		if (err != HUBBUB_OK)
			return err;
		return insert_html(tb, TAG(tk));
	case T_RB: case T_RTC:
		if (in_scope(tb, T_RUBY, SC_DEFAULT))
			generate_implied_end_tags(tb, -1);
		return insert_html(tb, TAG(tk));
	case T_RP: case T_RT:
		if (in_scope(tb, T_RUBY, SC_DEFAULT))
			generate_implied_end_tags(tb, T_RTC);
		return insert_html(tb, TAG(tk));
	case T_MATH:
		err = reconstruct_formatting(tb);
		if (err != HUBBUB_OK)
			return err;
		adjust_mathml_attrs(tk);
		adjust_foreign_attrs(tk);
		err = insert_foreign(tb, TAG(tk), HUBBUB_NS_MATHML, false, NULL);
		if (err == HUBBUB_OK && TAG(tk)->self_closing)
			pop(tb);
		return err;
	case T_SVG:
		err = reconstruct_formatting(tb);
		if (err != HUBBUB_OK)
			return err;
		adjust_svg_attrs(tk);
		adjust_foreign_attrs(tk);
		err = insert_foreign(tb, TAG(tk), HUBBUB_NS_SVG, false, NULL);
		if (err == HUBBUB_OK && TAG(tk)->self_closing)
			pop(tb);
		return err;
	case T_CAPTION: case T_COL: case T_COLGROUP: case T_FRAME: case T_HEAD: case T_TBODY:
	case T_TD: case T_TFOOT: case T_TH: case T_THEAD: case T_TR:
		return HUBBUB_OK;
	default:
		break;
	}
	err = reconstruct_formatting(tb);
	if (err != HUBBUB_OK)
		return err;
	return insert_html(tb, TAG(tk));
}

static hubbub_error in_body_end(hubbub_treebuilder *tb, token *tk)
{
	int t = TTYPE(tk);
	bool other;
	hubbub_error err;

	switch (t) {
	case T_TEMPLATE:
		return in_head(tb, tk);
	case T_BODY:
		if (!body_end_ok(tb))
			return HUBBUB_OK;
		tb->mode = M_AFTER_BODY;
		return HUBBUB_OK;
	case T_HTML:
		if (!body_end_ok(tb))
			return HUBBUB_OK;
		tb->mode = M_AFTER_BODY;
		return REPROCESS;
	case T_ADDRESS: case T_ARTICLE: case T_ASIDE: case T_BLOCKQUOTE: case T_BUTTON:
	case T_CENTER: case T_DETAILS: case T_DIALOG: case T_DIR: case T_DIV: case T_DL:
	case T_FIELDSET: case T_FIGCAPTION: case T_FIGURE: case T_FOOTER: case T_HEADER:
	case T_HGROUP: case T_LISTING: case T_MAIN: case T_MENU: case T_NAV: case T_OL:
	case T_PRE: case T_SEARCH: case T_SECTION: case T_SELECT: case T_SUMMARY: case T_UL:
		if (!in_scope(tb, t, SC_DEFAULT))
			return HUBBUB_OK;
		generate_implied_end_tags(tb, -1);
		pop_until(tb, t);
		return HUBBUB_OK;
	case T_FORM:
		if (!in_template_contents(tb)) {
			void *node = tb->form;
			int i;
			tb->form = NULL;
			if (node == NULL || !node_in_scope(tb, node, SC_DEFAULT)) {
				unref(tb, node);
				return HUBBUB_OK;
			}
			generate_implied_end_tags(tb, -1);
			i = st_find_node(tb, node);
			if (i >= 0)
				st_remove(tb, i);
			unref(tb, node);
			return HUBBUB_OK;
		}
		if (!in_scope(tb, T_FORM, SC_DEFAULT))
			return HUBBUB_OK;
		generate_implied_end_tags(tb, -1);
		pop_until(tb, T_FORM);
		return HUBBUB_OK;
	case T_P:
		if (!in_scope(tb, T_P, SC_BUTTON)) {
			err = insert_html_named(tb, "p");
			if (err != HUBBUB_OK)
				return err;
		}
		close_p(tb);
		return HUBBUB_OK;
	case T_LI:
		if (!in_scope(tb, T_LI, SC_LIST_ITEM))
			return HUBBUB_OK;
		generate_implied_end_tags(tb, T_LI);
		pop_until(tb, T_LI);
		return HUBBUB_OK;
	case T_DD: case T_DT:
		if (!in_scope(tb, t, SC_DEFAULT))
			return HUBBUB_OK;
		generate_implied_end_tags(tb, t);
		pop_until(tb, t);
		return HUBBUB_OK;
	case T_H1: case T_H2: case T_H3: case T_H4: case T_H5: case T_H6:
		if (!in_scope(tb, T_H1, SC_DEFAULT) && !in_scope(tb, T_H2, SC_DEFAULT) &&
				!in_scope(tb, T_H3, SC_DEFAULT) && !in_scope(tb, T_H4, SC_DEFAULT) &&
				!in_scope(tb, T_H5, SC_DEFAULT) && !in_scope(tb, T_H6, SC_DEFAULT))
			return HUBBUB_OK;
		generate_implied_end_tags(tb, -1);
		pop_until_heading(tb);
		return HUBBUB_OK;
	case T_A: case T_B: case T_BIG: case T_CODE: case T_EM: case T_FONT: case T_I:
	case T_NOBR: case T_S: case T_SMALL: case T_STRIKE: case T_STRONG: case T_TT: case T_U:
		err = adoption_agency(tb, t, &other);
		if (err != HUBBUB_OK || !other)
			return err;
		return any_other_end_tag(tb, tk);
	case T_APPLET: case T_MARQUEE: case T_OBJECT:
		if (!in_scope(tb, t, SC_DEFAULT))
			return HUBBUB_OK;
		generate_implied_end_tags(tb, -1);
		pop_until(tb, t);
		clear_to_marker(tb);
		return HUBBUB_OK;
	case T_BR: {
		/* as a <br> start tag without attributes */
		tk->t.type = HUBBUB_TOKEN_START_TAG;
		TAG(tk)->n_attributes = 0;
		TAG(tk)->attributes = NULL;
		TAG(tk)->self_closing = false;
		return in_body_start(tb, tk);
	}
	default:
		break;
	}
	return any_other_end_tag(tb, tk);
}

static hubbub_error in_body(hubbub_treebuilder *tb, token *tk)
{
	hubbub_error err;
	switch (tk->t.type) {
	case HUBBUB_TOKEN_CHARACTER: {
		const hubbub_string *s = &tk->t.data.character;
		if (is_nul_token(tk))
			return HUBBUB_OK;
		err = reconstruct_formatting(tb);
		if (err != HUBBUB_OK)
			return err;
		if (tb->frameset_ok) {
			for (size_t i = 0; i < s->len; i++)
				if (!is_ws(s->ptr[i])) {
					tb->frameset_ok = false;
					break;
				}
		}
		return insert_chars(tb, s->ptr, s->len);
	}
	case HUBBUB_TOKEN_COMMENT:
		return insert_comment(tb, &tk->t.data.comment);
	case HUBBUB_TOKEN_DOCTYPE:
		return HUBBUB_OK;
	case HUBBUB_TOKEN_START_TAG:
		return in_body_start(tb, tk);
	case HUBBUB_TOKEN_END_TAG:
		return in_body_end(tb, tk);
	case HUBBUB_TOKEN_EOF:
		if (tb->n_template_modes)
			return in_template(tb, tk);
		return HUBBUB_OK;
	}
	return HUBBUB_OK;
}

static hubbub_error complete_script(hubbub_treebuilder *tb, void *script)
{
	hubbub_error err = flush_text(tb);
	if (err != HUBBUB_OK)
		return err;
	if (TH->complete_script == NULL)
		return HUBBUB_OK;
	return TH->complete_script(CTX, script);
}

static hubbub_error text_mode(hubbub_treebuilder *tb, token *tk)
{
	switch (tk->t.type) {
	case HUBBUB_TOKEN_CHARACTER:
		return insert_chars(tb, tk->t.data.character.ptr, tk->t.data.character.len);
	case HUBBUB_TOKEN_EOF:
		pop(tb);
		tb->mode = tb->original_mode;
		return REPROCESS;
	case HUBBUB_TOKEN_END_TAG:
		if (TTYPE(tk) == T_SCRIPT && is_html(cur(tb), T_SCRIPT)) {
			void *script = cur(tb)->node;
			bool run = !has_template(tb) && !tb->fragment;
			hubbub_error err = HUBBUB_OK;
			ref(tb, script);
			pop(tb);
			tb->mode = tb->original_mode;
			if (run)
				err = complete_script(tb, script);
			unref(tb, script);
			return err;
		}
		pop(tb);
		tb->mode = tb->original_mode;
		return HUBBUB_OK;
	default:
		return HUBBUB_OK;
	}
}

static void clear_to_table_context(hubbub_treebuilder *tb)
{
	while (tb->n > 0 && !is_html(cur(tb), T_TABLE) && !is_html(cur(tb), T_TEMPLATE) &&
			!is_html(cur(tb), T_HTML))
		pop(tb);
}

static void clear_to_table_body_context(hubbub_treebuilder *tb)
{
	while (tb->n > 0 && !is_html(cur(tb), T_TBODY) && !is_html(cur(tb), T_TFOOT) &&
			!is_html(cur(tb), T_THEAD) && !is_html(cur(tb), T_TEMPLATE) &&
			!is_html(cur(tb), T_HTML))
		pop(tb);
}

static void clear_to_table_row_context(hubbub_treebuilder *tb)
{
	while (tb->n > 0 && !is_html(cur(tb), T_TR) && !is_html(cur(tb), T_TEMPLATE) &&
			!is_html(cur(tb), T_HTML))
		pop(tb);
}

static hubbub_error in_table_anything_else(hubbub_treebuilder *tb, token *tk)
{
	hubbub_error err;
	bool old = tb->foster;
	tb->foster = true;
	err = in_body(tb, tk);
	tb->foster = old;
	return err;
}

static hubbub_error in_table(hubbub_treebuilder *tb, token *tk)
{
	hubbub_error err;
	switch (tk->t.type) {
	case HUBBUB_TOKEN_CHARACTER: {
		const elem *c = cur(tb);
		if (c->ns == HUBBUB_NS_HTML && (c->type == T_TABLE || c->type == T_TBODY ||
				c->type == T_TEMPLATE || c->type == T_TFOOT || c->type == T_THEAD ||
				c->type == T_TR)) {
			tb->table_text.len = 0;
			tb->table_text_nonws = false;
			tb->original_mode = tb->mode;
			tb->mode = M_IN_TABLE_TEXT;
			return REPROCESS;
		}
		break;
	}
	case HUBBUB_TOKEN_COMMENT:
		return insert_comment(tb, &tk->t.data.comment);
	case HUBBUB_TOKEN_DOCTYPE:
		return HUBBUB_OK;
	case HUBBUB_TOKEN_START_TAG:
		switch (TTYPE(tk)) {
		case T_CAPTION:
			clear_to_table_context(tb);
			err = insert_marker(tb);
			if (err == HUBBUB_OK)
				err = insert_html(tb, TAG(tk));
			tb->mode = M_IN_CAPTION;
			return err;
		case T_COLGROUP:
			clear_to_table_context(tb);
			err = insert_html(tb, TAG(tk));
			tb->mode = M_IN_COLUMN_GROUP;
			return err;
		case T_COL:
			clear_to_table_context(tb);
			err = insert_html_named(tb, "colgroup");
			tb->mode = M_IN_COLUMN_GROUP;
			return err != HUBBUB_OK ? err : REPROCESS;
		case T_TBODY: case T_TFOOT: case T_THEAD:
			clear_to_table_context(tb);
			err = insert_html(tb, TAG(tk));
			tb->mode = M_IN_TABLE_BODY;
			return err;
		case T_TD: case T_TH: case T_TR:
			clear_to_table_context(tb);
			err = insert_html_named(tb, "tbody");
			tb->mode = M_IN_TABLE_BODY;
			return err != HUBBUB_OK ? err : REPROCESS;
		case T_TABLE:
			if (!in_scope(tb, T_TABLE, SC_TABLE))
				return HUBBUB_OK;
			pop_until(tb, T_TABLE);
			reset_insertion_mode(tb);
			return REPROCESS;
		case T_STYLE: case T_SCRIPT: case T_TEMPLATE:
			return in_head(tb, tk);
		case T_INPUT: {
			const hubbub_attribute *type = find_attr(TAG(tk), "type");
			if (type == NULL || !str_eq_ci(type->value.ptr, type->value.len, "hidden"))
				break;
			err = insert_html(tb, TAG(tk));
			pop(tb);
			return err;
		}
		case T_FORM:
			if (tb->form != NULL && !in_template_contents(tb))
				return HUBBUB_OK;
			err = insert_html(tb, TAG(tk));
			if (err != HUBBUB_OK)
				return err;
			if (!in_template_contents(tb)) {
				unref(tb, tb->form);
				tb->form = cur(tb)->node;
				ref(tb, tb->form);
			}
			pop(tb);
			return HUBBUB_OK;
		default:
			break;
		}
		break;
	case HUBBUB_TOKEN_END_TAG:
		switch (TTYPE(tk)) {
		case T_TABLE:
			if (!in_scope(tb, T_TABLE, SC_TABLE))
				return HUBBUB_OK;
			pop_until(tb, T_TABLE);
			reset_insertion_mode(tb);
			return HUBBUB_OK;
		case T_BODY: case T_CAPTION: case T_COL: case T_COLGROUP: case T_HTML: case T_TBODY:
		case T_TD: case T_TFOOT: case T_TH: case T_THEAD: case T_TR:
			return HUBBUB_OK;
		case T_TEMPLATE:
			return in_head(tb, tk);
		default:
			break;
		}
		break;
	case HUBBUB_TOKEN_EOF:
		return in_body(tb, tk);
	}
	return in_table_anything_else(tb, tk);
}

static hubbub_error in_table_text(hubbub_treebuilder *tb, token *tk)
{
	if (tk->t.type == HUBBUB_TOKEN_CHARACTER) {
		const hubbub_string *s = &tk->t.data.character;
		if (is_nul_token(tk))
			return HUBBUB_OK;
		if (!tb->table_text_nonws)
			for (size_t i = 0; i < s->len; i++)
				if (!is_ws(s->ptr[i])) {
					tb->table_text_nonws = true;
					break;
				}
		return ts_add(&tb->table_text, s->ptr, s->len) ? HUBBUB_OK : HUBBUB_NOMEM;
	}
	if (tb->table_text.len > 0) {
		hubbub_error err;
		if (tb->table_text_nonws) {
			/* as in table's "anything else": foster parented through in body */
			token ct;
			memset(&ct, 0, sizeof ct);
			ct.t.type = HUBBUB_TOKEN_CHARACTER;
			ct.t.data.character.ptr = tb->table_text.p;
			ct.t.data.character.len = tb->table_text.len;
			err = in_table_anything_else(tb, &ct);
		} else {
			err = insert_chars(tb, tb->table_text.p, tb->table_text.len);
		}
		tb->table_text.len = 0;
		if (err != HUBBUB_OK)
			return err;
	}
	tb->mode = tb->original_mode;
	return REPROCESS;
}

static hubbub_error close_caption(hubbub_treebuilder *tb)
{
	generate_implied_end_tags(tb, -1);
	pop_until(tb, T_CAPTION);
	clear_to_marker(tb);
	tb->mode = M_IN_TABLE;
	return HUBBUB_OK;
}

static hubbub_error in_caption(hubbub_treebuilder *tb, token *tk)
{
	if (IS_END(tk)) {
		switch (TTYPE(tk)) {
		case T_CAPTION:
			if (!in_scope(tb, T_CAPTION, SC_TABLE))
				return HUBBUB_OK;
			return close_caption(tb);
		case T_TABLE:
			if (!in_scope(tb, T_CAPTION, SC_TABLE))
				return HUBBUB_OK;
			close_caption(tb);
			return REPROCESS;
		case T_BODY: case T_COL: case T_COLGROUP: case T_HTML: case T_TBODY: case T_TD:
		case T_TFOOT: case T_TH: case T_THEAD: case T_TR:
			return HUBBUB_OK;
		default:
			break;
		}
	} else if (IS_START(tk)) {
		switch (TTYPE(tk)) {
		case T_CAPTION: case T_COL: case T_COLGROUP: case T_TBODY: case T_TD: case T_TFOOT:
		case T_TH: case T_THEAD: case T_TR:
			if (!in_scope(tb, T_CAPTION, SC_TABLE))
				return HUBBUB_OK;
			close_caption(tb);
			return REPROCESS;
		default:
			break;
		}
	}
	return in_body(tb, tk);
}

static hubbub_error in_column_group(hubbub_treebuilder *tb, token *tk)
{
	hubbub_error err;
	switch (tk->t.type) {
	case HUBBUB_TOKEN_CHARACTER: {
		size_t ws = leading_ws(tk);
		err = split_ws(tb, tk, ws, true);
		if (err != HUBBUB_OK || tk->t.data.character.len == 0)
			return err;
		break;
	}
	case HUBBUB_TOKEN_COMMENT:
		return insert_comment(tb, &tk->t.data.comment);
	case HUBBUB_TOKEN_DOCTYPE:
		return HUBBUB_OK;
	case HUBBUB_TOKEN_START_TAG:
		switch (TTYPE(tk)) {
		case T_HTML:
			return in_body(tb, tk);
		case T_COL:
			err = insert_html(tb, TAG(tk));
			pop(tb);
			return err;
		case T_TEMPLATE:
			return in_head(tb, tk);
		default:
			break;
		}
		break;
	case HUBBUB_TOKEN_END_TAG:
		switch (TTYPE(tk)) {
		case T_COLGROUP:
			if (!is_html(cur(tb), T_COLGROUP))
				return HUBBUB_OK;
			pop(tb);
			tb->mode = M_IN_TABLE;
			return HUBBUB_OK;
		case T_COL:
			return HUBBUB_OK;
		case T_TEMPLATE:
			return in_head(tb, tk);
		default:
			break;
		}
		break;
	case HUBBUB_TOKEN_EOF:
		return in_body(tb, tk);
	}
	if (!is_html(cur(tb), T_COLGROUP))
		return HUBBUB_OK;
	pop(tb);
	tb->mode = M_IN_TABLE;
	return REPROCESS;
}

static bool tbody_thead_tfoot_in_table_scope(hubbub_treebuilder *tb)
{
	return in_scope(tb, T_TBODY, SC_TABLE) || in_scope(tb, T_THEAD, SC_TABLE) ||
		in_scope(tb, T_TFOOT, SC_TABLE);
}

static hubbub_error in_table_body(hubbub_treebuilder *tb, token *tk)
{
	hubbub_error err;
	if (IS_START(tk)) {
		switch (TTYPE(tk)) {
		case T_TR:
			clear_to_table_body_context(tb);
			err = insert_html(tb, TAG(tk));
			tb->mode = M_IN_ROW;
			return err;
		case T_TH: case T_TD:
			clear_to_table_body_context(tb);
			err = insert_html_named(tb, "tr");
			tb->mode = M_IN_ROW;
			return err != HUBBUB_OK ? err : REPROCESS;
		case T_CAPTION: case T_COL: case T_COLGROUP: case T_TBODY: case T_TFOOT: case T_THEAD:
			if (!tbody_thead_tfoot_in_table_scope(tb))
				return HUBBUB_OK;
			clear_to_table_body_context(tb);
			pop(tb);
			tb->mode = M_IN_TABLE;
			return REPROCESS;
		default:
			break;
		}
	} else if (IS_END(tk)) {
		int t = TTYPE(tk);
		switch (t) {
		case T_TBODY: case T_TFOOT: case T_THEAD:
			if (!in_scope(tb, t, SC_TABLE))
				return HUBBUB_OK;
			clear_to_table_body_context(tb);
			pop(tb);
			tb->mode = M_IN_TABLE;
			return HUBBUB_OK;
		case T_TABLE:
			if (!tbody_thead_tfoot_in_table_scope(tb))
				return HUBBUB_OK;
			clear_to_table_body_context(tb);
			pop(tb);
			tb->mode = M_IN_TABLE;
			return REPROCESS;
		case T_BODY: case T_CAPTION: case T_COL: case T_COLGROUP: case T_HTML: case T_TD:
		case T_TH: case T_TR:
			return HUBBUB_OK;
		default:
			break;
		}
	}
	return in_table(tb, tk);
}

static hubbub_error in_row(hubbub_treebuilder *tb, token *tk)
{
	hubbub_error err;
	if (IS_START(tk)) {
		switch (TTYPE(tk)) {
		case T_TH: case T_TD:
			clear_to_table_row_context(tb);
			err = insert_html(tb, TAG(tk));
			tb->mode = M_IN_CELL;
			if (err == HUBBUB_OK)
				err = insert_marker(tb);
			return err;
		case T_CAPTION: case T_COL: case T_COLGROUP: case T_TBODY: case T_TFOOT: case T_THEAD:
		case T_TR:
			if (!in_scope(tb, T_TR, SC_TABLE))
				return HUBBUB_OK;
			clear_to_table_row_context(tb);
			pop(tb);
			tb->mode = M_IN_TABLE_BODY;
			return REPROCESS;
		default:
			break;
		}
	} else if (IS_END(tk)) {
		int t = TTYPE(tk);
		switch (t) {
		case T_TR:
			if (!in_scope(tb, T_TR, SC_TABLE))
				return HUBBUB_OK;
			clear_to_table_row_context(tb);
			pop(tb);
			tb->mode = M_IN_TABLE_BODY;
			return HUBBUB_OK;
		case T_TABLE:
			if (!in_scope(tb, T_TR, SC_TABLE))
				return HUBBUB_OK;
			clear_to_table_row_context(tb);
			pop(tb);
			tb->mode = M_IN_TABLE_BODY;
			return REPROCESS;
		case T_TBODY: case T_TFOOT: case T_THEAD:
			if (!in_scope(tb, t, SC_TABLE))
				return HUBBUB_OK;
			if (!in_scope(tb, T_TR, SC_TABLE))
				return HUBBUB_OK;
			clear_to_table_row_context(tb);
			pop(tb);
			tb->mode = M_IN_TABLE_BODY;
			return REPROCESS;
		case T_BODY: case T_CAPTION: case T_COL: case T_COLGROUP: case T_HTML: case T_TD:
		case T_TH:
			return HUBBUB_OK;
		default:
			break;
		}
	}
	return in_table(tb, tk);
}

static void close_cell(hubbub_treebuilder *tb)
{
	generate_implied_end_tags(tb, -1);
	while (tb->n > 0) {
		bool hit = is_html(cur(tb), T_TD) || is_html(cur(tb), T_TH);
		pop(tb);
		if (hit)
			break;
	}
	clear_to_marker(tb);
	tb->mode = M_IN_ROW;
}

static hubbub_error in_cell(hubbub_treebuilder *tb, token *tk)
{
	if (IS_END(tk)) {
		int t = TTYPE(tk);
		switch (t) {
		case T_TD: case T_TH:
			if (!in_scope(tb, t, SC_TABLE))
				return HUBBUB_OK;
			generate_implied_end_tags(tb, -1);
			pop_until(tb, t);
			clear_to_marker(tb);
			tb->mode = M_IN_ROW;
			return HUBBUB_OK;
		case T_BODY: case T_CAPTION: case T_COL: case T_COLGROUP: case T_HTML:
			return HUBBUB_OK;
		case T_TABLE: case T_TBODY: case T_TFOOT: case T_THEAD: case T_TR:
			if (!in_scope(tb, t, SC_TABLE))
				return HUBBUB_OK;
			close_cell(tb);
			return REPROCESS;
		default:
			break;
		}
	} else if (IS_START(tk)) {
		switch (TTYPE(tk)) {
		case T_CAPTION: case T_COL: case T_COLGROUP: case T_TBODY: case T_TD: case T_TFOOT:
		case T_TH: case T_THEAD: case T_TR:
			if (!in_scope(tb, T_TD, SC_TABLE) && !in_scope(tb, T_TH, SC_TABLE))
				return HUBBUB_OK;
			close_cell(tb);
			return REPROCESS;
		default:
			break;
		}
	}
	return in_body(tb, tk);
}

static hubbub_error switch_template_mode(hubbub_treebuilder *tb, mode_t_ m)
{
	pop_template_mode(tb);
	if (push_template_mode(tb, m) != HUBBUB_OK)
		return HUBBUB_NOMEM;
	tb->mode = m;
	return REPROCESS;
}

static hubbub_error in_template(hubbub_treebuilder *tb, token *tk)
{
	switch (tk->t.type) {
	case HUBBUB_TOKEN_CHARACTER: case HUBBUB_TOKEN_COMMENT: case HUBBUB_TOKEN_DOCTYPE:
		return in_body(tb, tk);
	case HUBBUB_TOKEN_START_TAG:
		switch (TTYPE(tk)) {
		case T_BASE: case T_BASEFONT: case T_BGSOUND: case T_LINK: case T_META:
		case T_NOFRAMES: case T_SCRIPT: case T_STYLE: case T_TEMPLATE: case T_TITLE:
			return in_head(tb, tk);
		case T_CAPTION: case T_COLGROUP: case T_TBODY: case T_TFOOT: case T_THEAD:
			return switch_template_mode(tb, M_IN_TABLE);
		case T_COL:
			return switch_template_mode(tb, M_IN_COLUMN_GROUP);
		case T_TR:
			return switch_template_mode(tb, M_IN_TABLE_BODY);
		case T_TD: case T_TH:
			return switch_template_mode(tb, M_IN_ROW);
		default:
			return switch_template_mode(tb, M_IN_BODY);
		}
	case HUBBUB_TOKEN_END_TAG:
		if (TTYPE(tk) == T_TEMPLATE)
			return in_head(tb, tk);
		return HUBBUB_OK;
	case HUBBUB_TOKEN_EOF:
		if (!has_template(tb))
			return HUBBUB_OK;
		pop_until(tb, T_TEMPLATE);
		clear_to_marker(tb);
		pop_template_mode(tb);
		reset_insertion_mode(tb);
		return REPROCESS;
	}
	return HUBBUB_OK;
}

static hubbub_error after_body(hubbub_treebuilder *tb, token *tk)
{
	switch (tk->t.type) {
	case HUBBUB_TOKEN_CHARACTER: {
		size_t ws = leading_ws(tk);
		if (ws > 0) {
			token w = *tk;
			hubbub_error err;
			w.t.data.character.len = ws;
			err = in_body(tb, &w);
			if (err != HUBBUB_OK)
				return err;
			split_ws(tb, tk, ws, false);
			if (tk->t.data.character.len == 0)
				return HUBBUB_OK;
		}
		break;
	}
	case HUBBUB_TOKEN_COMMENT:
		return insert_comment_at(tb, &tk->t.data.comment, &tb->st[0], NULL);
	case HUBBUB_TOKEN_DOCTYPE:
		return HUBBUB_OK;
	case HUBBUB_TOKEN_START_TAG:
		if (TTYPE(tk) == T_HTML)
			return in_body(tb, tk);
		break;
	case HUBBUB_TOKEN_END_TAG:
		if (TTYPE(tk) == T_HTML) {
			if (tb->fragment)
				return HUBBUB_OK;
			tb->mode = M_AFTER_AFTER_BODY;
			return HUBBUB_OK;
		}
		break;
	case HUBBUB_TOKEN_EOF:
		return HUBBUB_OK;
	}
	tb->mode = M_IN_BODY;
	return REPROCESS;
}

static hubbub_error in_frameset(hubbub_treebuilder *tb, token *tk)
{
	hubbub_error err;
	switch (tk->t.type) {
	case HUBBUB_TOKEN_CHARACTER: {
		/* only the whitespace goes in */
		const hubbub_string *s = &tk->t.data.character;
		for (size_t i = 0; i < s->len; i++) {
			if (is_ws(s->ptr[i])) {
				size_t j = i;
				while (j < s->len && is_ws(s->ptr[j]))
					j++;
				err = insert_chars(tb, s->ptr + i, j - i);
				if (err != HUBBUB_OK)
					return err;
				i = j - 1;
			}
		}
		return HUBBUB_OK;
	}
	case HUBBUB_TOKEN_COMMENT:
		return insert_comment(tb, &tk->t.data.comment);
	case HUBBUB_TOKEN_START_TAG:
		switch (TTYPE(tk)) {
		case T_HTML:
			return in_body(tb, tk);
		case T_FRAMESET:
			return insert_html(tb, TAG(tk));
		case T_FRAME:
			err = insert_html(tb, TAG(tk));
			pop(tb);
			return err;
		case T_NOFRAMES:
			return in_head(tb, tk);
		default:
			return HUBBUB_OK;
		}
	case HUBBUB_TOKEN_END_TAG:
		if (TTYPE(tk) == T_FRAMESET) {
			if (tb->n == 1)
				return HUBBUB_OK;
			pop(tb);
			if (!tb->fragment && !is_html(cur(tb), T_FRAMESET))
				tb->mode = M_AFTER_FRAMESET;
		}
		return HUBBUB_OK;
	default:
		return HUBBUB_OK;
	}
}

static hubbub_error whitespace_only(hubbub_treebuilder *tb, token *tk)
{
	const hubbub_string *s = &tk->t.data.character;
	for (size_t i = 0; i < s->len; i++) {
		if (is_ws(s->ptr[i])) {
			size_t j = i;
			hubbub_error err;
			while (j < s->len && is_ws(s->ptr[j]))
				j++;
			err = insert_chars(tb, s->ptr + i, j - i);
			if (err != HUBBUB_OK)
				return err;
			i = j - 1;
		}
	}
	return HUBBUB_OK;
}

static hubbub_error after_frameset(hubbub_treebuilder *tb, token *tk)
{
	switch (tk->t.type) {
	case HUBBUB_TOKEN_CHARACTER:
		return whitespace_only(tb, tk);
	case HUBBUB_TOKEN_COMMENT:
		return insert_comment(tb, &tk->t.data.comment);
	case HUBBUB_TOKEN_START_TAG:
		if (TTYPE(tk) == T_HTML)
			return in_body(tb, tk);
		if (TTYPE(tk) == T_NOFRAMES)
			return in_head(tb, tk);
		return HUBBUB_OK;
	case HUBBUB_TOKEN_END_TAG:
		if (TTYPE(tk) == T_HTML)
			tb->mode = M_AFTER_AFTER_FRAMESET;
		return HUBBUB_OK;
	default:
		return HUBBUB_OK;
	}
}

static hubbub_error after_after_body(hubbub_treebuilder *tb, token *tk)
{
	switch (tk->t.type) {
	case HUBBUB_TOKEN_COMMENT:
		return insert_comment_at(tb, &tk->t.data.comment, NULL, tb->document);
	case HUBBUB_TOKEN_DOCTYPE:
		return in_body(tb, tk);
	case HUBBUB_TOKEN_CHARACTER: {
		size_t ws = leading_ws(tk);
		if (ws > 0) {
			token w = *tk;
			hubbub_error err;
			w.t.data.character.len = ws;
			err = in_body(tb, &w);
			if (err != HUBBUB_OK)
				return err;
			split_ws(tb, tk, ws, false);
			if (tk->t.data.character.len == 0)
				return HUBBUB_OK;
		}
		break;
	}
	case HUBBUB_TOKEN_START_TAG:
		if (TTYPE(tk) == T_HTML)
			return in_body(tb, tk);
		break;
	case HUBBUB_TOKEN_EOF:
		return HUBBUB_OK;
	default:
		break;
	}
	tb->mode = M_IN_BODY;
	return REPROCESS;
}

static hubbub_error after_after_frameset(hubbub_treebuilder *tb, token *tk)
{
	switch (tk->t.type) {
	case HUBBUB_TOKEN_COMMENT:
		return insert_comment_at(tb, &tk->t.data.comment, NULL, tb->document);
	case HUBBUB_TOKEN_DOCTYPE:
		return in_body(tb, tk);
	case HUBBUB_TOKEN_CHARACTER: {
		/* whitespace: as in body; the rest ignored */
		const hubbub_string *s = &tk->t.data.character;
		for (size_t i = 0; i < s->len; i++) {
			if (is_ws(s->ptr[i])) {
				size_t j = i;
				token w = *tk;
				hubbub_error err;
				while (j < s->len && is_ws(s->ptr[j]))
					j++;
				w.t.data.character.ptr = s->ptr + i;
				w.t.data.character.len = j - i;
				err = in_body(tb, &w);
				if (err != HUBBUB_OK)
					return err;
				i = j - 1;
			}
		}
		return HUBBUB_OK;
	}
	case HUBBUB_TOKEN_START_TAG:
		if (TTYPE(tk) == T_HTML)
			return in_body(tb, tk);
		if (TTYPE(tk) == T_NOFRAMES)
			return in_head(tb, tk);
		return HUBBUB_OK;
	default:
		return HUBBUB_OK;
	}
}

/* -- foreign content -------------------------------------------------------------- */

static bool breaks_out_of_foreign(token *tk)
{
	switch (TTYPE(tk)) {
	case T_B: case T_BIG: case T_BLOCKQUOTE: case T_BODY: case T_BR: case T_CENTER:
	case T_CODE: case T_DD: case T_DIV: case T_DL: case T_DT: case T_EM: case T_EMBED:
	case T_H1: case T_H2: case T_H3: case T_H4: case T_H5: case T_H6: case T_HEAD:
	case T_HR: case T_I: case T_IMG: case T_LI: case T_LISTING: case T_MENU: case T_META:
	case T_NOBR: case T_OL: case T_P: case T_PRE: case T_RUBY: case T_S: case T_SMALL:
	case T_SPAN: case T_STRONG: case T_STRIKE: case T_SUB: case T_SUP: case T_TABLE:
	case T_TT: case T_U: case T_UL: case T_VAR:
		return true;
	case T_FONT:
		return find_attr(TAG(tk), "color") || find_attr(TAG(tk), "face") ||
			find_attr(TAG(tk), "size");
	default:
		return false;
	}
}

static hubbub_error mode_dispatch(hubbub_treebuilder *tb, token *tk);

static hubbub_error foreign_content(hubbub_treebuilder *tb, token *tk)
{
	hubbub_error err;
	switch (tk->t.type) {
	case HUBBUB_TOKEN_CHARACTER: {
		const hubbub_string *s = &tk->t.data.character;
		if (is_nul_token(tk))
			return insert_chars(tb, (const uint8_t *) "\xEF\xBF\xBD", 3);
		if (tb->frameset_ok)
			for (size_t i = 0; i < s->len; i++)
				if (!is_ws(s->ptr[i])) {
					tb->frameset_ok = false;
					break;
				}
		return insert_chars(tb, s->ptr, s->len);
	}
	case HUBBUB_TOKEN_COMMENT:
		return insert_comment(tb, &tk->t.data.comment);
	case HUBBUB_TOKEN_DOCTYPE:
		return HUBBUB_OK;
	case HUBBUB_TOKEN_START_TAG: {
		const elem *acn;
		hubbub_ns ns;
		if (breaks_out_of_foreign(tk)) {
			while (tb->n > 0 && !is_mathml_text_ip(cur(tb)) && !is_html_ip(cur(tb)) &&
					cur(tb)->ns != HUBBUB_NS_HTML)
				pop(tb);
			return mode_dispatch(tb, tk);
		}
		acn = adjusted_current(tb);
		ns = acn->ns;
		if (ns == HUBBUB_NS_MATHML)
			adjust_mathml_attrs(tk);
		if (ns == HUBBUB_NS_SVG)
			adjust_svg_attrs(tk);
		adjust_foreign_attrs(tk);
		err = insert_foreign(tb, TAG(tk), ns, false, NULL);
		if (err != HUBBUB_OK)
			return err;
		if (TAG(tk)->self_closing)
			pop(tb);	/* (an SVG script: no script processing) */
		return HUBBUB_OK;
	}
	case HUBBUB_TOKEN_END_TAG: {
		const hubbub_string *name = &TAG(tk)->name;
		int i = (int) tb->n - 1;
		if (TTYPE(tk) == T_BR || TTYPE(tk) == T_P) {
			while (tb->n > 0 && !is_mathml_text_ip(cur(tb)) && !is_html_ip(cur(tb)) &&
					cur(tb)->ns != HUBBUB_NS_HTML)
				pop(tb);
			return mode_dispatch(tb, tk);
		}
		for (;;) {
			const elem *e = &tb->st[i];
			const char *en = elem_name(e);
			if (i == 0)
				return HUBBUB_OK;
			if (en != NULL && strlen(en) == name->len &&
					memcmp(en, name->ptr, name->len) == 0) {
				while ((int) tb->n > i)
					pop(tb);
				return HUBBUB_OK;
			}
			i--;
			if (tb->st[i].ns == HUBBUB_NS_HTML)
				return mode_dispatch(tb, tk);
		}
	}
	default:
		return HUBBUB_OK;
	}
}

/* the current insertion mode's rules */
static hubbub_error mode_dispatch(hubbub_treebuilder *tb, token *tk)
{
	switch (tb->mode) {
	case M_INITIAL: return initial(tb, tk);
	case M_BEFORE_HTML: return before_html(tb, tk);
	case M_BEFORE_HEAD: return before_head(tb, tk);
	case M_IN_HEAD: return in_head(tb, tk);
	case M_IN_HEAD_NOSCRIPT: return in_head_noscript(tb, tk);
	case M_AFTER_HEAD: return after_head(tb, tk);
	case M_IN_BODY: return in_body(tb, tk);
	case M_TEXT: return text_mode(tb, tk);
	case M_IN_TABLE: return in_table(tb, tk);
	case M_IN_TABLE_TEXT: return in_table_text(tb, tk);
	case M_IN_CAPTION: return in_caption(tb, tk);
	case M_IN_COLUMN_GROUP: return in_column_group(tb, tk);
	case M_IN_TABLE_BODY: return in_table_body(tb, tk);
	case M_IN_ROW: return in_row(tb, tk);
	case M_IN_CELL: return in_cell(tb, tk);
	case M_IN_TEMPLATE: return in_template(tb, tk);
	case M_AFTER_BODY: return after_body(tb, tk);
	case M_IN_FRAMESET: return in_frameset(tb, tk);
	case M_AFTER_FRAMESET: return after_frameset(tb, tk);
	case M_AFTER_AFTER_BODY: return after_after_body(tb, tk);
	case M_AFTER_AFTER_FRAMESET: return after_after_frameset(tb, tk);
	}
	return HUBBUB_OK;
}

/* the tree construction dispatcher */
static hubbub_error process(hubbub_treebuilder *tb, token *tk)
{
	const elem *acn = adjusted_current(tb);
	bool html = true;

	if (acn != NULL && acn->ns != HUBBUB_NS_HTML && tk->t.type != HUBBUB_TOKEN_EOF) {
		html = false;
		if (is_mathml_text_ip(acn)) {
			if (IS_CHARS(tk))
				html = true;
			else if (IS_START(tk)) {
				int t = TTYPE(tk);
				if (t != T_MGLYPH && t != T_MALIGNMARK)
					html = true;
			}
		}
		if (!html && acn->ns == HUBBUB_NS_MATHML && acn->type == T_ANNOTATION_XML &&
				IS_START(tk) && TTYPE(tk) == T_SVG)
			html = true;
		if (!html && is_html_ip(acn) && (IS_START(tk) || IS_CHARS(tk)))
			html = true;
	}
	return html ? mode_dispatch(tb, tk) : foreign_content(tb, tk);
}

/* ---- the API ---------------------------------------------------------------------- */

hubbub_error hubbub_treebuilder_token_handler(const hubbub_token *token_in, void *pw)
{
	hubbub_treebuilder *tb = (hubbub_treebuilder *) pw;
	hubbub_error err;
	token tk;
	bool cdata;

	if (tb->document == NULL || tb->tree_handler == NULL)
		return HUBBUB_OK;

	tk.t = *token_in;
	tk.adj = NULL;
	tk.adj_cap = 0;

	/* a LF right after <pre>, <listing>, <textarea> */
	if (tb->skip_lf) {
		tb->skip_lf = false;
		if (tk.t.type == HUBBUB_TOKEN_CHARACTER && tk.t.data.character.len > 0 &&
				tk.t.data.character.ptr[0] == '\n') {
			tk.t.data.character.ptr++;
			tk.t.data.character.len--;
			if (tk.t.data.character.len == 0)
				return HUBBUB_OK;
		}
	}

	do {
		err = process(tb, &tk);
	} while (err == HUBBUB_REPROCESS);

	free(tk.adj);

	if (tk.t.type == HUBBUB_TOKEN_EOF) {
		hubbub_error e2;
		/* the end: the stack popped (an option's selectedcontent follows it) */
		if (tb->n_scs > 0)
			while (tb->n > 0)
				pop(tb);
		e2 = flush_text(tb);
		if (err == HUBBUB_OK)
			err = e2;
	}

	/* CDATA sections are allowed when the adjusted current node is foreign */
	{
		const elem *acn = adjusted_current(tb);
		cdata = acn != NULL && acn->ns != HUBBUB_NS_HTML;
		if (cdata != tb->cdata) {
			hubbub_tokeniser_optparams p;
			tb->cdata = cdata;
			p.process_cdata = cdata;
			hubbub_tokeniser_setopt(tb->tokeniser, HUBBUB_TOKENISER_PROCESS_CDATA, &p);
		}
	}
	return err;
}

hubbub_error hubbub_treebuilder_create(hubbub_tokeniser *tokeniser,
		hubbub_treebuilder **treebuilder)
{
	hubbub_error error;
	hubbub_treebuilder *tb;
	hubbub_tokeniser_optparams tokparams;

	if (tokeniser == NULL || treebuilder == NULL)
		return HUBBUB_BADPARM;

	if (type_name[T_A] == NULL)
		for (size_t i = 0; i < N_TYPE_NAMES; i++)
			type_name[type_names[i].type] = type_names[i].name;

	tb = calloc(1, sizeof(hubbub_treebuilder));
	if (tb == NULL)
		return HUBBUB_NOMEM;

	tb->tokeniser = tokeniser;
	tb->mode = M_INITIAL;
	tb->frameset_ok = true;

	tokparams.token_handler.handler = hubbub_treebuilder_token_handler;
	tokparams.token_handler.pw = tb;
	error = hubbub_tokeniser_setopt(tokeniser, HUBBUB_TOKENISER_TOKEN_HANDLER, &tokparams);
	if (error != HUBBUB_OK) {
		free(tb);
		return error;
	}

	*treebuilder = tb;
	return HUBBUB_OK;
}

hubbub_error hubbub_treebuilder_destroy(hubbub_treebuilder *tb)
{
	hubbub_tokeniser_optparams tokparams;

	if (tb == NULL)
		return HUBBUB_BADPARM;

	tokparams.token_handler.handler = NULL;
	tokparams.token_handler.pw = NULL;
	hubbub_tokeniser_setopt(tb->tokeniser, HUBBUB_TOKENISER_TOKEN_HANDLER, &tokparams);

	if (tb->tree_handler != NULL) {
		flush_text(tb);
		unref(tb, tb->head);
		unref(tb, tb->form);
		unref(tb, tb->document);
		unref(tb, tb->ctx.node);
		while (tb->n > 0)
			pop(tb);
		while (tb->fn > 0)
			fl_remove(tb, tb->fn - 1);
		for (uint32_t i = 0; i < tb->n_sels; i++) {
			unref(tb, tb->sels[i].select);
			unref(tb, tb->sels[i].option);
		}
		for (uint32_t i = 0; i < tb->n_scs; i++)
			unref(tb, tb->scs[i].sc);
	}
	free(tb->sels);
	free(tb->scs);
	elem_free_name(&tb->ctx);
	free(tb->st);
	free(tb->fl);
	free(tb->template_modes);
	free(tb->table_text.p);
	free(tb->text.p);
	free(tb);
	return HUBBUB_OK;
}

/* the fragment case: the root html element, the context's modes (13.4) */
static hubbub_error setup_fragment(hubbub_treebuilder *tb,
		const hubbub_treebuilder_optparams *p)
{
	hubbub_tag tag;
	void *root = NULL;
	hubbub_error err;
	const char *name = p->fragment_context.name ? p->fragment_context.name : "body";
	size_t len = strlen(name);
	int type;
	hubbub_content_model m = HUBBUB_CONTENT_MODEL_PCDATA;

	if (tb->tree_handler == NULL || tb->document == NULL)
		return HUBBUB_BADPARM;

	/* the context element (lower case names to match on) */
	{
		char lower[64];
		size_t n = len < sizeof lower - 1 ? len : sizeof lower - 1;
		for (size_t i = 0; i < n; i++)
			lower[i] = (name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i];
		lower[n] = 0;
		type = name_type((const uint8_t *) lower, n);
		memset(&tb->ctx, 0, sizeof tb->ctx);
		tb->ctx.ns = p->fragment_context.ns == HUBBUB_NS_NULL ? HUBBUB_NS_HTML :
				p->fragment_context.ns;
		tb->ctx.type = type;
		tb->ctx.hip = p->fragment_context.html_integration_point;
		elem_set_name(&tb->ctx, (const uint8_t *) lower, n);
		tb->ctx.node = p->fragment_context.node;
		ref(tb, tb->ctx.node);
	}
	tb->fragment = true;
	tb->quirks = p->fragment_context.quirks;

	if (tb->ctx.ns == HUBBUB_NS_HTML) {
		switch (type) {
		case T_TITLE: case T_TEXTAREA: m = HUBBUB_CONTENT_MODEL_RCDATA; break;
		case T_STYLE: case T_XMP: case T_IFRAME: case T_NOEMBED: case T_NOFRAMES:
			m = HUBBUB_CONTENT_MODEL_CDATA; break;
		case T_SCRIPT: m = HUBBUB_CONTENT_MODEL_SCRIPTDATA; break;
		case T_NOSCRIPT:
			if (tb->enable_scripting)
				m = HUBBUB_CONTENT_MODEL_CDATA;
			break;
		case T_PLAINTEXT: m = HUBBUB_CONTENT_MODEL_PLAINTEXT; break;
		default: break;
		}
	}
	if (m != HUBBUB_CONTENT_MODEL_PCDATA)
		set_tokeniser_state(tb, m);

	memset(&tag, 0, sizeof tag);
	tag.ns = HUBBUB_NS_HTML;
	tag.name.ptr = (const uint8_t *) "html";
	tag.name.len = 4;
	err = TH->create_element(CTX, &tag, &root);
	if (err != HUBBUB_OK)
		return err;
	err = push(tb, root, HUBBUB_NS_HTML, T_HTML, NULL, 0, false);
	if (err != HUBBUB_OK)
		return err;
	if (tb->ctx.ns == HUBBUB_NS_HTML && type == T_TEMPLATE) {
		err = push_template_mode(tb, M_IN_TEMPLATE);
		if (err != HUBBUB_OK)
			return err;
	}
	reset_insertion_mode(tb);
	if (p->fragment_context.form != NULL) {
		tb->form = p->fragment_context.form;
		ref(tb, tb->form);
	}
	/* the context decides whether CDATA sections are allowed at first */
	{
		hubbub_tokeniser_optparams tp;
		tb->cdata = tb->ctx.ns != HUBBUB_NS_HTML;
		tp.process_cdata = tb->cdata;
		hubbub_tokeniser_setopt(tb->tokeniser, HUBBUB_TOKENISER_PROCESS_CDATA, &tp);
	}
	return HUBBUB_OK;
}

hubbub_error hubbub_treebuilder_setopt(hubbub_treebuilder *tb,
		hubbub_treebuilder_opttype type,
		hubbub_treebuilder_optparams *params)
{
	if (tb == NULL || params == NULL)
		return HUBBUB_BADPARM;

	switch (type) {
	case HUBBUB_TREEBUILDER_ERROR_HANDLER:
		tb->error_handler = params->error_handler.handler;
		tb->error_pw = params->error_handler.pw;
		break;
	case HUBBUB_TREEBUILDER_TREE_HANDLER:
		tb->tree_handler = params->tree_handler;
		break;
	case HUBBUB_TREEBUILDER_DOCUMENT_NODE:
		tb->document = params->document_node;
		break;
	case HUBBUB_TREEBUILDER_ENABLE_SCRIPTING:
		tb->enable_scripting = params->enable_scripting;
		break;
	case HUBBUB_TREEBUILDER_FRAGMENT_CONTEXT:
		return setup_fragment(tb, params);
	}
	return HUBBUB_OK;
}
