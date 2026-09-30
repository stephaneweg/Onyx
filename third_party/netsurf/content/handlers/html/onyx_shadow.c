/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 *
 * Onyx: shadow DOM in the box tree (see onyx_shadow.h).
 *
 * The shadow roots are libdom document fragments their hosts keep (libdom's hubbub binding:
 * dom_onyx_attach_shadow): no child of the host, so the document's walks (its serialization,
 * its selectors, the scripts' child lists) never see them. The boxes are built from the
 * flat tree instead of the DOM tree when the document has shadow roots: box_construct.c
 * asks here for a node's first child, next sibling and parent.
 *
 * Slot assignment (the DOM standard's "named" mode): the slottables of a host -- its
 * element children (their slot attribute, default "") and text children ("") -- each go to
 * the first slot of its shadow tree, in tree order, with that name. Worked out per host when
 * the boxes first need it and kept until the next box tree (onyx_shadow_begin).
 *
 * Style scoping (CSS Scoping): each shadow tree has its own selection context -- the user
 * agent's and user's sheets, then its <style> elements' and its adopted sheets' (parsed once
 * per text for the document's life) --, the document's author sheets never match in it; an
 * element's style takes its own tree's rules, its shadow tree's :host rules (a host), the
 * ::slotted() rules of the slots it is assigned to, the ::part() rules of the tree around
 * its host (libcss's css_select_style_onyx and its encapsulation contexts).
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <dom/dom.h>
#include <dom/bindings/hubbub/parser.h>

#include "utils/corestrings.h"
#include "utils/nsurl.h"
#include "netsurf/content.h"
#include "content/hlcache.h"
#include "css/css.h"
#include "utils/log.h"
#include "desktop/system_colour.h"
#include "css/internal.h"
#include "css/select.h"

#include "html/html.h"
#include "html/private.h"
#include "html/box.h"
#include "html/onyx_shadow.h"

enum { K_HOST = 1, K_ASSIGNED, K_SLOT, K_CONTENTS, K_TREE };

struct osh_ent {
	void *key;		/* a node (a reference) */
	void *val;
	uint32_t idx;
	uint8_t kind;
};

struct osh_slot {
	dom_node *slot;		/* a reference */
	dom_string *name;
	dom_node **nodes;	/* the nodes assigned (references) */
	unsigned int n, cap;
};

struct osh_host {
	struct osh_slot *slots;
	unsigned int n;
	struct osh_host *next;	/* (the list freed at the reset) */
};

struct onyx_tree {
	dom_node *root;		/* the shadow root (a reference) */
	dom_node *host;		/* its host (a reference) */
	css_select_ctx *sel;	/* its selection context (owned) */
	bool has_author;	/* it has style sheets of its own */
	nscss_select_ctx pw;	/* the handler's data matching in it */
	struct onyx_tree *next;
};

struct osh_sheet {
	char *text;
	size_t len;
	uint32_t hash;
	css_stylesheet *sheet;
};

struct onyx_shadow {
	struct osh_ent *tab;
	unsigned int cap, n;
	struct osh_host *hosts;
	struct onyx_tree *trees;
	nscss_select_ctx doc;		/* the document's own context */
	struct osh_sheet *sheets;	/* the shadow trees' style sheets, by text */
	unsigned int n_sheets, cap_sheets;
};

/* ---- the map: (node, kind) -> value ----------------------------------------------------- */

static inline uint32_t osh_hash(const void *key, int kind)
{
	uintptr_t k = (uintptr_t) key;
	return (uint32_t) (((k >> 3) ^ (k >> 17)) * 2654435761u) ^ (uint32_t) kind;
}

static struct osh_ent *osh_find(struct onyx_shadow *sh, const void *key, int kind)
{
	uint32_t i;

	if (sh == NULL || sh->n == 0)
		return NULL;
	for (i = osh_hash(key, kind) & (sh->cap - 1); sh->tab[i].key != NULL;
			i = (i + 1) & (sh->cap - 1))
		if (sh->tab[i].key == key && sh->tab[i].kind == kind)
			return &sh->tab[i];
	return NULL;
}

static struct osh_ent *osh_add(struct onyx_shadow *sh, void *key, int kind)
{
	uint32_t i;

	if ((sh->n + 1) * 2 > sh->cap) {
		unsigned int ncap = sh->cap ? sh->cap * 2 : 64, j;
		struct osh_ent *nt = calloc(ncap, sizeof(*nt));
		if (nt == NULL)
			return NULL;
		for (j = 0; j < sh->cap; j++) {
			if (sh->tab[j].key == NULL)
				continue;
			for (i = osh_hash(sh->tab[j].key, sh->tab[j].kind) & (ncap - 1);
					nt[i].key != NULL; i = (i + 1) & (ncap - 1))
				;
			nt[i] = sh->tab[j];
		}
		free(sh->tab);
		sh->tab = nt;
		sh->cap = ncap;
	}
	for (i = osh_hash(key, kind) & (sh->cap - 1); sh->tab[i].key != NULL;
			i = (i + 1) & (sh->cap - 1))
		;
	sh->tab[i].key = dom_node_ref(key);
	sh->tab[i].kind = kind;
	sh->tab[i].val = NULL;
	sh->tab[i].idx = 0;
	sh->n++;
	return &sh->tab[i];
}

static struct onyx_shadow *osh_get(html_content *c)
{
	if (c->onyx_sh == NULL)
		c->onyx_sh = calloc(1, sizeof(struct onyx_shadow));
	return c->onyx_sh;
}

/* everything but the sheets' cache emptied */
static void osh_reset(struct onyx_shadow *sh)
{
	unsigned int i, j;

	for (i = 0; i < sh->cap; i++) {
		struct osh_ent *e = &sh->tab[i];
		if (e->key == NULL)
			continue;
		dom_node_unref(e->key);
		e->key = NULL;
	}
	sh->n = 0;
	while (sh->hosts != NULL) {
		struct osh_host *h = sh->hosts;
		sh->hosts = h->next;
		for (i = 0; i < h->n; i++) {
			for (j = 0; j < h->slots[i].n; j++)
				dom_node_unref(h->slots[i].nodes[j]);
			free(h->slots[i].nodes);
			if (h->slots[i].name != NULL)
				dom_string_unref(h->slots[i].name);
			dom_node_unref(h->slots[i].slot);
		}
		free(h->slots);
		free(h);
	}
	while (sh->trees != NULL) {
		struct onyx_tree *t = sh->trees;
		sh->trees = t->next;
		if (t->sel != NULL)
			css_select_ctx_destroy(t->sel);
		dom_node_unref(t->root);
		dom_node_unref(t->host);
		free(t);
	}
}

/* exported function documented in html/onyx_shadow.h */
void onyx_shadow_begin(html_content *c)
{
	c->onyx_shadow = dom_onyx_has_shadow(c->document);
	if (c->onyx_sh != NULL)
		osh_reset(c->onyx_sh);
}

/* exported function documented in html/onyx_shadow.h */
void onyx_shadow_destroy(html_content *c)
{
	struct onyx_shadow *sh = c->onyx_sh;
	unsigned int i;

	if (sh == NULL)
		return;
	osh_reset(sh);
	for (i = 0; i < sh->n_sheets; i++) {
		free(sh->sheets[i].text);
		if (sh->sheets[i].sheet != NULL)
			css_stylesheet_destroy(sh->sheets[i].sheet);
	}
	free(sh->sheets);
	free(sh->tab);
	free(sh);
	c->onyx_sh = NULL;
}

/* ---- helpers -------------------------------------------------------------------------- */

static bool osh_is_element(dom_node *n)
{
	dom_node_type t;
	return dom_node_get_node_type(n, &t) == DOM_NO_ERR && t == DOM_ELEMENT_NODE;
}

static bool osh_local_name_is(dom_node *n, const char *name)
{
	dom_string *ln = NULL;
	bool r = false;
	if (dom_node_get_local_name(n, &ln) == DOM_NO_ERR && ln != NULL) {
		r = dom_string_byte_length(ln) == strlen(name) &&
			strncasecmp(dom_string_data(ln), name, strlen(name)) == 0;
		dom_string_unref(ln);
	}
	return r;
}

static bool osh_is_slot(dom_node *n)
{
	return osh_is_element(n) && osh_local_name_is(n, "slot");
}

static dom_string *osh_attr(dom_node *n, const char *name)
{
	dom_string *k = NULL, *v = NULL;
	if (dom_string_create_interned((const uint8_t *) name, strlen(name), &k) !=
			DOM_NO_ERR)
		return NULL;
	if (dom_element_get_attribute(n, k, &v) != DOM_NO_ERR)
		v = NULL;
	dom_string_unref(k);
	return v;
}

static bool osh_names_equal(dom_string *a, dom_string *b)
{
	size_t la = a ? dom_string_byte_length(a) : 0;
	size_t lb = b ? dom_string_byte_length(b) : 0;
	return la == lb && (la == 0 || memcmp(dom_string_data(a), dom_string_data(b), la) == 0);
}

/* the next node of root's subtree in tree order after n (NULL at the end); a reference */
static dom_node *osh_following(dom_node *n, dom_node *root, bool into)
{
	dom_node *next = NULL, *p;

	if (into && dom_node_get_first_child(n, &next) == DOM_NO_ERR && next != NULL)
		return next;
	n = dom_node_ref(n);
	while (n != NULL && n != root) {
		if (dom_node_get_next_sibling(n, &next) == DOM_NO_ERR && next != NULL) {
			dom_node_unref(n);
			return next;
		}
		p = NULL;
		dom_node_get_parent_node(n, &p);
		dom_node_unref(n);
		n = p;
	}
	if (n != NULL)
		dom_node_unref(n);
	return NULL;
}

/* ---- slot assignment ------------------------------------------------------------------ */

static bool osh_slot_push(struct osh_slot *s, dom_node *n)
{
	if (s->n == s->cap) {
		unsigned int nc = s->cap ? s->cap * 2 : 4;
		dom_node **nn = realloc(s->nodes, nc * sizeof(*nn));
		if (nn == NULL)
			return false;
		s->nodes = nn;
		s->cap = nc;
	}
	s->nodes[s->n++] = dom_node_ref(n);
	return true;
}

/* the host's slots and the nodes assigned to them (made once per box tree) */
static struct osh_host *osh_host_info(html_content *c, dom_node *host)
{
	struct onyx_shadow *sh = osh_get(c);
	struct osh_ent *e;
	struct osh_host *h;
	dom_node *root, *n, *child = NULL;
	unsigned int cap = 0, i;

	if (sh == NULL)
		return NULL;
	e = osh_find(sh, host, K_HOST);
	if (e != NULL)
		return e->val;
	root = dom_onyx_shadow_root(host);
	if (root == NULL)
		return NULL;
	h = calloc(1, sizeof(*h));
	if (h == NULL)
		return NULL;
	h->next = sh->hosts;
	sh->hosts = h;

	/* the slots, in tree order */
	for (n = osh_following(root, root, true); n != NULL; ) {
		dom_node *next;
		if (osh_is_slot(n)) {
			if (h->n == cap) {
				unsigned int nc = cap ? cap * 2 : 4;
				struct osh_slot *ns = realloc(h->slots, nc * sizeof(*ns));
				if (ns == NULL) {
					dom_node_unref(n);
					break;
				}
				h->slots = ns;
				cap = nc;
			}
			memset(&h->slots[h->n], 0, sizeof(h->slots[0]));
			h->slots[h->n].slot = dom_node_ref(n);
			h->slots[h->n].name = osh_attr(n, "name");
			h->n++;
		}
		next = osh_following(n, root, true);
		dom_node_unref(n);
		n = next;
	}

	/* the slottables: element and text children, to the first slot of their name */
	if (h->n > 0 && dom_node_get_first_child(host, &child) == DOM_NO_ERR) {
		while (child != NULL) {
			dom_node_type t;
			dom_node *next = NULL;
			if (dom_node_get_node_type(child, &t) == DOM_NO_ERR &&
					(t == DOM_ELEMENT_NODE || t == DOM_TEXT_NODE)) {
				dom_string *name = t == DOM_ELEMENT_NODE ?
						osh_attr(child, "slot") : NULL;
				for (i = 0; i < h->n; i++) {
					if (osh_names_equal(h->slots[i].name, name))
						break;
				}
				if (i < h->n && osh_slot_push(&h->slots[i], child)) {
					struct osh_ent *a = osh_add(sh, child, K_ASSIGNED);
					if (a != NULL) {
						a->val = &h->slots[i];
						a->idx = h->slots[i].n - 1;
					}
				}
				if (name != NULL)
					dom_string_unref(name);
			}
			dom_node_get_next_sibling(child, &next);
			dom_node_unref(child);
			child = next;
		}
	}
	for (i = 0; i < h->n; i++) {
		struct osh_ent *s = osh_add(sh, h->slots[i].slot, K_SLOT);
		if (s != NULL)
			s->val = &h->slots[i];
	}
	e = osh_add(sh, host, K_HOST);
	if (e != NULL)
		e->val = h;
	return h;
}

/* the slot a light node is assigned to (its parent a host), NULL if none */
static struct osh_ent *osh_assigned(html_content *c, dom_node *n, dom_node *parent)
{
	struct osh_ent *e = osh_find(c->onyx_sh, n, K_ASSIGNED);
	if (e != NULL)
		return e;
	if (parent == NULL || dom_onyx_shadow_root(parent) == NULL)
		return NULL;
	if (osh_host_info(c, parent) == NULL)
		return NULL;
	return osh_find(c->onyx_sh, n, K_ASSIGNED);
}

/* a slot's record (its tree's host worked out), NULL if the node is none */
static struct osh_slot *osh_slot_of(html_content *c, dom_node *n)
{
	struct osh_ent *e;
	dom_node *host;

	if (!osh_is_slot(n))
		return NULL;
	e = osh_find(c->onyx_sh, n, K_SLOT);
	if (e != NULL)
		return e->val;
	host = dom_onyx_shadow_host(dom_onyx_node_root(n));
	if (host == NULL || osh_host_info(c, host) == NULL)
		return NULL;
	e = osh_find(c->onyx_sh, n, K_SLOT);
	return e != NULL ? e->val : NULL;
}

/* ---- the flat tree -------------------------------------------------------------------- */

/* exported function documented in html/onyx_shadow.h */
dom_node *onyx_flat_first_child(html_content *c, dom_node *n)
{
	dom_node *r = NULL, *root;
	struct osh_slot *s;

	if (osh_is_element(n)) {
		root = dom_onyx_shadow_root(n);
		if (root != NULL) {
			dom_node_get_first_child(root, &r);
			return r;
		}
		s = osh_slot_of(c, n);
		if (s != NULL && s->n > 0)
			return dom_node_ref(s->nodes[0]);
	}
	dom_node_get_first_child(n, &r);
	return r;
}

/* exported function documented in html/onyx_shadow.h */
dom_node *onyx_flat_next_sibling(html_content *c, dom_node *n)
{
	dom_node *r = NULL, *p = NULL;
	struct osh_ent *e = osh_find(c->onyx_sh, n, K_ASSIGNED);

	if (e == NULL) {
		dom_node_get_parent_node(n, &p);
		if (p != NULL) {
			e = osh_assigned(c, n, p);
			dom_node_unref(p);
		}
	}
	if (e != NULL) {
		struct osh_slot *s = e->val;
		return e->idx + 1 < s->n ? dom_node_ref(s->nodes[e->idx + 1]) : NULL;
	}
	dom_node_get_next_sibling(n, &r);
	return r;
}

/* exported function documented in html/onyx_shadow.h */
dom_node *onyx_flat_parent(html_content *c, dom_node *n)
{
	dom_node *p = NULL, *host;
	struct osh_ent *e;

	if (dom_node_get_parent_node(n, &p) != DOM_NO_ERR || p == NULL)
		return NULL;
	host = dom_onyx_shadow_host(p);
	if (host != NULL) {
		dom_node_unref(p);
		return dom_node_ref(host);
	}
	e = osh_assigned(c, n, p);
	if (e != NULL) {
		dom_node_unref(p);
		return dom_node_ref(((struct osh_slot *) e->val)->slot);
	}
	return p;
}

/* ---- display: contents ---------------------------------------------------------------- */

/* exported function documented in html/onyx_shadow.h */
void onyx_contents_keep(html_content *c, dom_node *n, struct box *box)
{
	struct onyx_shadow *sh = osh_get(c);
	struct osh_ent *e = sh != NULL ? osh_find(sh, n, K_CONTENTS) : NULL;

	if (sh == NULL)
		return;
	if (e == NULL)
		e = osh_add(sh, n, K_CONTENTS);
	if (e != NULL)
		e->val = box;
}

/* exported function documented in html/onyx_shadow.h */
const css_computed_style *onyx_contents_style(html_content *c, dom_node *n)
{
	struct osh_ent *e = osh_find(c->onyx_sh, n, K_CONTENTS);
	struct box *b = e != NULL ? e->val : NULL;
	return b != NULL ? b->style : NULL;
}

/* ---- the shadow trees' style sheets --------------------------------------------------- */

static dom_string *adopted_key;

struct osh_adopted {
	unsigned int n;
	char *text[];
};

static void osh_adopted_handler(dom_node_operation operation, dom_string *key, void *data,
		struct dom_node *src, struct dom_node *dst)
{
	struct osh_adopted *a = data;
	unsigned int i;

	(void) key;
	(void) src;
	(void) dst;
	if (operation != DOM_NODE_DELETED || a == NULL)
		return;
	for (i = 0; i < a->n; i++)
		free(a->text[i]);
	free(a);
}

/* exported function documented in html/onyx_shadow.h */
void onyx_shadow_set_adopted(dom_node *root, const char *const *texts, unsigned int n)
{
	struct osh_adopted *a = NULL, *old = NULL;
	unsigned int i;

	if (adopted_key == NULL && dom_string_create_interned(
			(const uint8_t *) "__onyx_adopted", 14, &adopted_key) != DOM_NO_ERR)
		return;
	if (n > 0) {
		a = calloc(1, sizeof(*a) + n * sizeof(char *));
		if (a == NULL)
			return;
		for (i = 0; i < n; i++) {
			a->text[i] = strdup(texts[i] != NULL ? texts[i] : "");
			if (a->text[i] == NULL)
				break;
		}
		a->n = i;
	}
	if (dom_node_set_user_data(root, adopted_key, a,
			a != NULL ? osh_adopted_handler : NULL, (void **) &old) != DOM_NO_ERR) {
		if (a != NULL)
			osh_adopted_handler(DOM_NODE_DELETED, NULL, a, NULL, NULL);
		return;
	}
	if (old != NULL)
		osh_adopted_handler(DOM_NODE_DELETED, NULL, old, NULL, NULL);
}

static uint32_t osh_text_hash(const char *s, size_t len)
{
	uint32_t h = 2166136261u;
	size_t i;
	for (i = 0; i < len; i++)
		h = (h ^ (uint8_t) s[i]) * 16777619u;
	return h;
}

/* the style sheet of a text (parsed once for the document's life) */
static css_stylesheet *osh_sheet(html_content *c, const char *text, size_t len)
{
	struct onyx_shadow *sh = osh_get(c);
	css_stylesheet_params params;
	css_stylesheet *sheet = NULL;
	css_error error;
	uint32_t h = osh_text_hash(text, len);
	unsigned int i;

	if (sh == NULL)
		return NULL;
	for (i = 0; i < sh->n_sheets; i++)
		if (sh->sheets[i].hash == h && sh->sheets[i].len == len &&
				memcmp(sh->sheets[i].text, text, len) == 0)
			return sh->sheets[i].sheet;

	memset(&params, 0, sizeof(params));
	params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
	params.level = CSS_LEVEL_DEFAULT;
	params.charset = "UTF-8";
	params.url = c->base_url != NULL ? nsurl_access(c->base_url) : "about:blank";
	params.allow_quirks = c->quirks != DOM_DOCUMENT_QUIRKS_MODE_NONE;
	params.inline_style = false;
	params.resolve = nscss_resolve_url;
	params.color = ns_system_colour;
	if (css_stylesheet_create(&params, &sheet) == CSS_OK) {
		error = css_stylesheet_append_data(sheet, (const uint8_t *) text, len);
		if (error == CSS_OK || error == CSS_NEEDDATA)
			error = css_stylesheet_data_done(sheet);
		if (error != CSS_OK && error != CSS_IMPORTS_PENDING) {
			css_stylesheet_destroy(sheet);
			sheet = NULL;
		}
	}
	if (sh->n_sheets == sh->cap_sheets) {
		unsigned int nc = sh->cap_sheets ? sh->cap_sheets * 2 : 16;
		struct osh_sheet *ns = realloc(sh->sheets, nc * sizeof(*ns));
		if (ns == NULL)
			return sheet;	/* (leaked: memory is short anyway) */
		sh->sheets = ns;
		sh->cap_sheets = nc;
	}
	sh->sheets[sh->n_sheets].text = malloc(len + 1);
	if (sh->sheets[sh->n_sheets].text == NULL)
		return sheet;
	memcpy(sh->sheets[sh->n_sheets].text, text, len);
	sh->sheets[sh->n_sheets].text[len] = '\0';
	sh->sheets[sh->n_sheets].len = len;
	sh->sheets[sh->n_sheets].hash = h;
	sh->sheets[sh->n_sheets].sheet = sheet;
	sh->n_sheets++;
	return sheet;
}

static void osh_append(css_select_ctx *sel, css_stylesheet *sheet, dom_string *media,
		bool *any)
{
	char m[256] = "screen";

	if (sheet == NULL)
		return;
	if (media != NULL && dom_string_byte_length(media) > 0 &&
			dom_string_byte_length(media) < sizeof m) {
		memcpy(m, dom_string_data(media), dom_string_byte_length(media));
		m[dom_string_byte_length(media)] = '\0';
	}
	if (css_select_ctx_append_sheet(sel, sheet, CSS_ORIGIN_AUTHOR, m) == CSS_OK)
		*any = true;
}

/* the document's own context (kept for the host contexts' sake) */
static nscss_select_ctx *osh_doc_ctx(html_content *c)
{
	struct onyx_shadow *sh = osh_get(c);
	if (sh == NULL)
		return NULL;
	memset(&sh->doc, 0, sizeof(sh->doc));
	sh->doc.ctx = c->select_ctx;
	sh->doc.quirks = (c->quirks == DOM_DOCUMENT_QUIRKS_MODE_FULL);
	sh->doc.base_url = c->base_url;
	sh->doc.universal = c->universal;
	return &sh->doc;
}

static struct onyx_tree *osh_tree_get(html_content *c, dom_node *root, dom_node *host,
		int depth);

/* the context of the tree a node is in: its shadow tree's, NULL for the document's */
static struct onyx_tree *osh_tree_of(html_content *c, dom_node *n, int depth)
{
	dom_node *root = dom_onyx_node_root(n);
	dom_node *host = root != NULL ? dom_onyx_shadow_host(root) : NULL;
	if (host == NULL)
		return NULL;
	return osh_tree_get(c, root, host, depth);
}

static struct onyx_tree *osh_tree_get(html_content *c, dom_node *root, dom_node *host,
		int depth)
{
	struct onyx_shadow *sh = osh_get(c);
	struct osh_ent *e;
	struct onyx_tree *t, *outer;
	dom_node *n;
	uint32_t i;
	void *data = NULL;

	if (sh == NULL || depth > 32)
		return NULL;
	e = osh_find(sh, root, K_TREE);
	if (e != NULL)
		return e->val;
	outer = osh_tree_of(c, host, depth + 1);

	t = calloc(1, sizeof(*t));
	if (t == NULL)
		return NULL;
	if (css_select_ctx_create(&t->sel) != CSS_OK) {
		free(t);
		return NULL;
	}
	t->root = dom_node_ref(root);
	t->host = dom_node_ref(host);
	t->next = sh->trees;
	sh->trees = t;

	/* the user agent's and the user's sheets, as the document's context */
	for (i = STYLESHEET_BASE; i < STYLESHEET_START && i < c->stylesheet_count; i++) {
		const struct html_stylesheet *hs = &c->stylesheets[i];
		if (hs->unused || hs->sheet == NULL ||
				hlcache_handle_get_content(hs->sheet) == NULL ||
				content_get_status(hs->sheet) != CONTENT_STATUS_DONE)
			continue;
		css_select_ctx_append_sheet(t->sel, nscss_get_stylesheet(hs->sheet),
				i < STYLESHEET_USER ? CSS_ORIGIN_UA : CSS_ORIGIN_USER, "screen");
	}
	/* its <style> elements, in tree order */
	for (n = osh_following(root, root, true); n != NULL; ) {
		dom_node *next;
		dom_html_element_type type;
		if (osh_is_element(n) &&
				dom_html_element_get_tag_type(n, &type) == DOM_NO_ERR &&
				type == DOM_HTML_ELEMENT_TYPE_STYLE) {
			dom_string *text = NULL, *media = osh_attr(n, "media");
			if (dom_node_get_text_content(n, &text) == DOM_NO_ERR && text != NULL) {
				osh_append(t->sel, osh_sheet(c, dom_string_data(text),
						dom_string_byte_length(text)), media, &t->has_author);
				dom_string_unref(text);
			}
			if (media != NULL)
				dom_string_unref(media);
			next = osh_following(n, root, false);	/* (not into its text) */
		} else {
			next = osh_following(n, root, true);
		}
		dom_node_unref(n);
		n = next;
	}
	/* its adopted sheets (adoptedStyleSheets), after */
	if (adopted_key != NULL &&
			dom_node_get_user_data(root, adopted_key, &data) == DOM_NO_ERR &&
			data != NULL) {
		struct osh_adopted *a = data;
		for (i = 0; i < a->n; i++)
			osh_append(t->sel, osh_sheet(c, a->text[i], strlen(a->text[i])), NULL,
					&t->has_author);
	}

	t->pw.ctx = t->sel;
	t->pw.quirks = (c->quirks == DOM_DOCUMENT_QUIRKS_MODE_FULL);
	t->pw.base_url = c->base_url;
	t->pw.universal = c->universal;
	t->pw.scope_host = host;
	t->pw.host_ctx = outer != NULL ? &outer->pw : &sh->doc;

	e = osh_add(sh, root, K_TREE);
	if (e != NULL)
		e->val = t;
	return t;
}

/* exported function documented in html/onyx_shadow.h */
bool onyx_box_is_slot(dom_node *n)
{
	return osh_is_slot(n);
}

/* exported function documented in html/onyx_shadow.h */
bool onyx_shadow_in_shadow_tree(dom_node *n)
{
	dom_node *root = dom_onyx_node_root(n);
	return root != NULL && dom_onyx_shadow_host(root) != NULL;
}

/* ---- an element's style --------------------------------------------------------------- */

#define OSH_MAX_SCOPES 8
#define OSH_MAX_PARTS 8

/* exported function documented in html/onyx_shadow.h */
css_select_results *onyx_shadow_style(html_content *c, nscss_select_ctx *ctx, dom_node *n,
		const css_stylesheet *inline_style)
{
	css_select_onyx_scope sc[OSH_MAX_SCOPES];
	lwc_string *parts[OSH_MAX_PARTS];
	unsigned int k = 0, n_parts = 0, i;
	nscss_select_ctx pw, *doc = osh_doc_ctx(c);
	struct onyx_tree *t, *tr;
	css_select_results *styles;
	dom_node *root, *p = NULL, *inherit = NULL, *node;
	bool no_share = false;
	int level;

	if (doc == NULL)
		return nscss_get_style(ctx, n, &c->media, &c->unit_len_ctx, inline_style);

	/* its own tree's context */
	t = osh_tree_of(c, n, 0);
	pw = t != NULL ? t->pw : *doc;
	pw.parent_style = ctx->parent_style;
	pw.root_style = ctx->root_style;

	/* a host: its shadow tree's :host rules (inner) */
	root = dom_onyx_shadow_root(n);
	if (root != NULL) {
		tr = osh_tree_get(c, root, n, 0);
		if (tr != NULL && tr->has_author) {
			memset(&sc[k], 0, sizeof(sc[k]));
			sc[k].ctx = tr->sel;
			sc[k].pw = &tr->pw;
			sc[k].node = n;
			sc[k].kind = CSS_ONYX_SCOPE_HOST;
			sc[k].level = 1;
			k++;
		}
		no_share = true;
	}

	/* assigned to slots: their trees' ::slotted() rules (inner, the deeper the more) */
	dom_node_get_parent_node(n, &p);
	if (p != NULL && dom_onyx_shadow_host(p) != NULL)
		inherit = dom_onyx_shadow_host(p);	/* (a shadow tree's top) */
	node = n;
	level = 1;
	while (p != NULL && k < OSH_MAX_SCOPES) {
		struct osh_ent *e;
		dom_node *next = NULL, *host = p;

		if (dom_onyx_shadow_root(host) == NULL)
			break;
		no_share = true;
		e = osh_assigned(c, node, host);
		if (e == NULL)
			break;
		node = ((struct osh_slot *) e->val)->slot;
		if (inherit == NULL)
			inherit = node;
		tr = osh_tree_get(c, dom_onyx_shadow_root(host), host, 0);
		if (tr != NULL && tr->has_author) {
			memset(&sc[k], 0, sizeof(sc[k]));
			sc[k].ctx = tr->sel;
			sc[k].pw = &tr->pw;
			sc[k].node = node;
			sc[k].kind = CSS_ONYX_SCOPE_SLOTTED;
			sc[k].level = level++;
			k++;
		}
		dom_node_get_parent_node(node, &next);	/* (the slot's parent: a host?) */
		dom_node_unref(p);
		p = next;
	}
	if (p != NULL)
		dom_node_unref(p);

	/* in a shadow tree with a part attribute: the ::part() rules of the tree around
	 * its host (outer) */
	if (t != NULL && k < OSH_MAX_SCOPES) {
		dom_string *pa = osh_attr(n, "part");
		if (pa != NULL) {
			const char *s = dom_string_data(pa), *e = s + dom_string_byte_length(pa);
			while (s < e && n_parts < OSH_MAX_PARTS) {
				const char *q;
				while (s < e && (*s == ' ' || *s == '\t' || *s == '\n'))
					s++;
				q = s;
				while (q < e && *q != ' ' && *q != '\t' && *q != '\n')
					q++;
				if (q > s && lwc_intern_string(s, q - s, &parts[n_parts]) ==
						lwc_error_ok)
					n_parts++;
				s = q;
			}
			dom_string_unref(pa);
		}
		if (n_parts > 0) {
			struct onyx_tree *outer = osh_tree_of(c, t->host, 0);
			memset(&sc[k], 0, sizeof(sc[k]));
			sc[k].ctx = outer != NULL ? outer->sel : c->select_ctx;
			sc[k].pw = outer != NULL ? &outer->pw : doc;
			sc[k].node = t->host;
			sc[k].kind = CSS_ONYX_SCOPE_PART;
			sc[k].level = -1;
			sc[k].parts = parts;
			sc[k].n_parts = n_parts;
			if (sc[k].ctx != NULL)
				k++;
			no_share = true;
		}
	}

	styles = nscss_get_style_onyx(&pw, n, &c->media, &c->unit_len_ctx, inline_style,
			inherit, no_share, sc, k);
	for (i = 0; i < n_parts; i++)
		lwc_string_unref(parts[i]);
	return styles;
}
