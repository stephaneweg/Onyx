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
 * Onyx: an element's style selection kept from one box tree to the next.
 *
 * A script's DOM change builds the whole box tree again (html_rebox), and nine tenths of
 * that is the style selection of every element (github.com: ~100 ms on the PC, 8 times
 * while it loads). Each element keeps its last selection (the cascade's result, before
 * the transitions and animations) in its DOM user data, and a box tree selects again
 * only the elements a change since can have restyled. The marks, from the DOM's mutation
 * events (dom_event.c), stamped with the next box tree's serial:
 *
 *  - an element's attribute changed, a node inserted: it and its whole subtree (its
 *    classes and attributes match its descendants' descendant and child combinators);
 *  - a child of P inserted, removed, its attributes, its children or text changed:
 *    P's "children" mark -- the children whose selection looked at their siblings
 *    (NSCSS_STRUCT_SELF: sibling combinators, :nth-child, :first-child..., :empty) are
 *    selected again, and every descendant whose selection looked at the structure
 *    around another node (NSCSS_STRUCT_ANC: an ancestor's :nth-child...) when any
 *    ancestor has such a mark (the latest mark along its ancestors, "chain").
 *
 * An element selected again for a mark gives its subtree the mark ("subtree": its
 * descendants' older selections are stale). Kept selections are also checked against
 * their parent's and the root's computed styles (interned by libcss: the same pointer is
 * the same style; references held) -- the inheritance; and an epoch that a new selection
 * context (the sheets), a media change or a shadow DOM switch moves. Never kept: the
 * elements whose selection tried :hover (the pointer moves without a DOM change), those
 * in a shadow tree, shadow hosts and their light children (the scoping has more inputs).
 * css:has() never matches in libcss, and the other state pseudo-classes answer no.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dom/dom.h>
#include <dom/bindings/hubbub/parser.h>
#include <libcss/libcss.h>

#include "utils/corestrings.h"
#include "utils/log.h"
#include "css/select.h"
#include "netsurf/onyx_perf.h"

#include "html/private.h"
#include "html/onyx_shadow.h"
#include "html/onyx_restyle.h"
#include "html/onyx_hover.h"

#define OSR_TESTED 4

struct osr_memo {
	unsigned int serial;	/* the box tree its selection was made or kept in */
	unsigned int epoch;
	unsigned int self_mark;	/* a change of its own: it and its subtree */
	unsigned int kids_mark;	/* its children's list or one of them changed */
	unsigned int subtree;	/* selected again for a mark: older descendants stale */
	unsigned int chain;	/* the latest kids_mark along its ancestors */
	unsigned int structural;	/* NSCSS_STRUCT_* */
	/* the nodes its selection tried :hover on (itself, ancestors) and whether each
	 * was hovered: kept while they are the same */
	dom_node *tested[OSR_TESTED];
	uint8_t ntested, hovered;
	css_select_results *res;	/* the cascade's (NULL: not kept) */
	css_computed_style *parent, *root;
};

static unsigned int osr_hits, osr_misses;

/* the nodes the selection being made tried :hover on */
static dom_node *osr_tested[OSR_TESTED];
static unsigned int osr_ntested;
static bool osr_tested_over;

/* exported function documented in html/onyx_restyle.h */
void onyx_restyle_select_begin(void)
{
	osr_ntested = 0;
	osr_tested_over = false;
}

/* exported function documented in html/onyx_restyle.h */
void onyx_restyle_hover_note(void *ctx, struct dom_node *tested, struct dom_node *styled)
{
	unsigned int i;

	onyx_hover_note(ctx, tested, styled);
	for (i = 0; i < osr_ntested; i++) {
		if (osr_tested[i] == tested)
			return;
	}
	if (osr_ntested < OSR_TESTED)
		osr_tested[osr_ntested++] = tested;
	else
		osr_tested_over = true;
}

/** Whether n is the node under the pointer or a (shadow-including) ancestor of it: the
 * selection's :hover (css/select.c node_is_hover) */
static bool osr_hovered(html_content *c, dom_node *n)
{
	dom_node *h = c->hover_node;
	bool on = false;

	if (h == NULL)
		return false;
	dom_node_ref(h);
	while (h != NULL) {
		dom_node *parent = NULL, *host;
		if (h == n) {
			on = true;
			dom_node_unref(h);
			break;
		}
		if (dom_node_get_parent_node(h, &parent) != DOM_NO_ERR)
			parent = NULL;
		dom_node_unref(h);
		h = parent;
		if (h != NULL && (host = dom_onyx_shadow_host(h)) != NULL) {
			dom_node_unref(h);
			h = dom_node_ref(host);
		}
	}
	return on;
}

/** Whether a is n or an ancestor of it */
static bool osr_ancestor_or_self(dom_node *a, dom_node *n)
{
	dom_node *p;
	bool yes = false;

	dom_node_ref(n);
	while (n != NULL) {
		if (n == a) {
			yes = true;
			dom_node_unref(n);
			break;
		}
		p = NULL;
		if (dom_node_get_parent_node(n, &p) != DOM_NO_ERR)
			p = NULL;
		dom_node_unref(n);
		n = p;
	}
	return yes;
}

static void osr_release(struct osr_memo *m)
{
	if (m->res != NULL)
		css_select_results_destroy(m->res);
	if (m->parent != NULL)
		css_computed_style_destroy(m->parent);
	if (m->root != NULL)
		css_computed_style_destroy(m->root);
	m->res = NULL;
	m->parent = m->root = NULL;
}

static void osr_user_data_handler(dom_node_operation operation, dom_string *key,
		void *data, struct dom_node *src, struct dom_node *dst)
{
	struct osr_memo *m = data;

	(void) src;
	(void) dst;
	if (m == NULL || dom_string_isequal(corestring_dom___ns_key_onyx_style_memo,
			key) == false)
		return;
	switch (operation) {
	case DOM_NODE_DELETED:
		osr_release(m);
		free(m);
		break;
	case DOM_NODE_RENAMED:
		osr_release(m);
		m->serial = 0;
		break;
	default:	/* cloned, imported, adopted: the copy has none */
		break;
	}
}

static struct osr_memo *osr_get(dom_node *n)
{
	void *data = NULL;

	if (n == NULL || dom_node_get_user_data(n,
			corestring_dom___ns_key_onyx_style_memo, &data) != DOM_NO_ERR)
		return NULL;
	return data;
}

static struct osr_memo *osr_make(dom_node *n)
{
	struct osr_memo *m = osr_get(n);
	void *old = NULL;
	dom_node_type type;

	if (m != NULL)
		return m;
	if (n == NULL || dom_node_get_node_type(n, &type) != DOM_NO_ERR ||
	    type != DOM_ELEMENT_NODE)
		return NULL;
	m = calloc(1, sizeof(*m));
	if (m == NULL)
		return NULL;
	if (dom_node_set_user_data(n, corestring_dom___ns_key_onyx_style_memo, m,
			osr_user_data_handler, &old) != DOM_NO_ERR) {
		free(m);
		return NULL;
	}
	return m;
}

/** n's parent element (a reference), or NULL */
static dom_node *osr_parent(dom_node *n)
{
	dom_node *p = NULL;
	dom_node_type type;

	if (dom_node_get_parent_node(n, &p) != DOM_NO_ERR || p == NULL)
		return NULL;
	if (dom_node_get_node_type(p, &type) != DOM_NO_ERR || type != DOM_ELEMENT_NODE) {
		dom_node_unref(p);
		return NULL;
	}
	return p;
}

/** The serial of the next box tree: the marks' */
static inline unsigned int osr_pending(const html_content *c)
{
	return c->restyle_serial + 1;
}

static void osr_mark_kids(html_content *c, dom_node *p)
{
	struct osr_memo *m;

	if (p == NULL)
		return;
	m = osr_make(p);
	if (m != NULL)
		m->kids_mark = osr_pending(c);
}

/* exported function documented in html/onyx_restyle.h */
void onyx_restyle_begin(html_content *c)
{
	/* the media the selection sees: a change selects everything again */
	if (c->restyle_media_w != c->media.width ||
	    c->restyle_media_h != c->media.height ||
	    c->restyle_vw != c->unit_len_ctx.viewport_width ||
	    c->restyle_vh != c->unit_len_ctx.viewport_height ||
	    c->restyle_shadow != c->onyx_shadow ||
	    c->restyle_base != c->base_url) {
		c->restyle_media_w = c->media.width;
		c->restyle_media_h = c->media.height;
		c->restyle_vw = c->unit_len_ctx.viewport_width;
		c->restyle_vh = c->unit_len_ctx.viewport_height;
		c->restyle_shadow = c->onyx_shadow;
		c->restyle_base = c->base_url;
		c->restyle_epoch++;
	}
	c->restyle_serial++;
	osr_hits = osr_misses = 0;
}

/* exported function documented in html/onyx_restyle.h */
void onyx_restyle_end(html_content *c)
{
	(void) c;
	if (onyx_perf_on() && osr_hits + osr_misses > 0)
		fprintf(stderr, "ONYX-PERF styles: %u of %u kept\n", osr_hits,
				osr_hits + osr_misses);
}

/* exported function documented in html/onyx_restyle.h */
void onyx_restyle_invalidate_all(html_content *c)
{
	c->restyle_epoch++;
}

/** Whether the shadow DOM's scoping may enter n's selection */
static bool osr_shadowish(html_content *c, dom_node *n, dom_node *p)
{
	if (!c->onyx_shadow)
		return false;
	if (dom_onyx_shadow_root(n) != NULL)
		return true;	/* a host */
	if (p != NULL && dom_onyx_shadow_root(p) != NULL)
		return true;	/* a host's light child (slots) */
	return onyx_shadow_in_shadow_tree(n);
}

/* exported function documented in html/onyx_restyle.h */
css_select_results *onyx_restyle_lookup(html_content *c, dom_node *n,
		const css_computed_style *parent_style,
		const css_computed_style *root_style)
{
	struct osr_memo *m = osr_make(n), *pm;
	dom_node *p;
	unsigned int chain = 0, cur = c->restyle_serial;
	bool forced, valid;
	css_select_results *r;
	int i;

	if (m == NULL)
		return NULL;
	p = osr_parent(n);
	pm = osr_get(p);
	if (pm != NULL)
		chain = pm->chain > pm->kids_mark ? pm->chain : pm->kids_mark;
	m->chain = chain;

	forced = m->self_mark > m->serial || (pm != NULL && pm->subtree > m->serial);
	if (forced)
		m->subtree = cur;
	valid = !forced && m->res != NULL && m->serial != 0 &&
			m->epoch == c->restyle_epoch &&
			m->parent == parent_style && m->root == root_style &&
			!((m->structural & NSCSS_STRUCT_SELF) && pm != NULL &&
			  pm->kids_mark > m->serial) &&
			!((m->structural & NSCSS_STRUCT_ANC) && chain > m->serial) &&
			!osr_shadowish(c, n, p);
	if (valid && m->ntested > 0) {
		for (i = 0; i < m->ntested && valid; i++) {
			if (osr_hovered(c, m->tested[i]) != ((m->hovered >> i) & 1))
				valid = false;
		}
	}
	if (p != NULL)
		dom_node_unref(p);
	if (!valid) {
		osr_misses++;
		return NULL;
	}

	r = calloc(1, sizeof(*r));
	if (r == NULL)
		return NULL;
	for (i = 0; i < CSS_PSEUDO_ELEMENT_COUNT; i++) {
		if (m->res->styles[i] != NULL)
			r->styles[i] = css_computed_style_onyx_ref(m->res->styles[i]);
	}
	m->serial = cur;
	osr_hits++;
	/* (the :hover notes its selection made: onyx_hover.c restyles with them) */
	for (i = 0; i < m->ntested; i++)
		onyx_hover_note(c, m->tested[i], n);
	if (m->ntested > 0)
		c->uses_hover = true;
	return r;
}

/* exported function documented in html/onyx_restyle.h */
void onyx_restyle_store(html_content *c, dom_node *n, const css_select_results *res,
		const css_computed_style *parent_style,
		const css_computed_style *root_style,
		bool cacheable, unsigned int structural)
{
	struct osr_memo *m = osr_make(n);
	int i;

	if (m == NULL)
		return;
	osr_release(m);
	m->serial = c->restyle_serial;
	m->epoch = c->restyle_epoch;
	m->structural = structural;
	m->ntested = m->hovered = 0;
	if (!cacheable || res == NULL || osr_tested_over)
		return;
	for (i = 0; i < (int) osr_ntested; i++) {
		/* (a :hover tried on a sibling: E:hover + F -- not kept) */
		if (!osr_ancestor_or_self(osr_tested[i], n))
			return;
		m->tested[i] = osr_tested[i];
		if (osr_hovered(c, osr_tested[i]))
			m->hovered |= 1u << i;
	}
	m->ntested = osr_ntested;
	m->res = calloc(1, sizeof(*m->res));
	if (m->res == NULL)
		return;
	for (i = 0; i < CSS_PSEUDO_ELEMENT_COUNT; i++) {
		if (res->styles[i] != NULL)
			m->res->styles[i] = css_computed_style_onyx_ref(res->styles[i]);
	}
	if (parent_style != NULL)
		m->parent = css_computed_style_onyx_ref(parent_style);
	if (root_style != NULL)
		m->root = css_computed_style_onyx_ref(root_style);
}

/* exported function documented in html/onyx_restyle.h */
void onyx_restyle_attr_changed(html_content *c, dom_node *el)
{
	struct osr_memo *m;
	dom_node *p;

	if (c->restyle_serial == 0)
		return;	/* (no box tree yet: nothing kept) */
	m = osr_make(el);

	if (m != NULL)
		m->self_mark = osr_pending(c);
	p = osr_parent(el);
	if (p != NULL) {
		osr_mark_kids(c, p);	/* (its siblings' sibling combinators) */
		dom_node_unref(p);
	}
}

/* exported function documented in html/onyx_restyle.h */
void onyx_restyle_child_changed(html_content *c, dom_node *child, bool inserted)
{
	dom_node *p = NULL, *pp;
	dom_node_type type;

	if (c->restyle_serial == 0)
		return;	/* (no box tree yet: nothing kept) */

	if (inserted && dom_node_get_node_type(child, &type) == DOM_NO_ERR &&
	    type == DOM_ELEMENT_NODE) {
		/* (a node moved: its old selection matched other ancestors) */
		struct osr_memo *m = osr_make(child);
		if (m != NULL)
			m->self_mark = osr_pending(c);
	}
	p = osr_parent(child);
	if (p == NULL) {
		/* (the document element: all of it) */
		if (dom_node_get_parent_node(child, &p) == DOM_NO_ERR && p != NULL) {
			if (dom_node_get_node_type(p, &type) == DOM_NO_ERR &&
			    type == DOM_DOCUMENT_NODE)
				onyx_restyle_invalidate_all(c);
			dom_node_unref(p);
		}
		return;
	}
	osr_mark_kids(c, p);		/* its siblings: :nth-child, :last-child... */
	pp = osr_parent(p);		/* its parent: :empty, and :empty + X */
	if (pp != NULL) {
		osr_mark_kids(c, pp);
		dom_node_unref(pp);
	}
	dom_node_unref(p);
}

/* exported function documented in html/onyx_restyle.h */
void onyx_restyle_text_changed(html_content *c, dom_node *text)
{
	dom_node *p, *pp;

	if (c->restyle_serial == 0)
		return;	/* (no box tree yet: nothing kept) */
	p = osr_parent(text);

	if (p == NULL)
		return;
	pp = osr_parent(p);	/* (its :empty: its parent's children mark) */
	if (pp != NULL) {
		osr_mark_kids(c, pp);
		dom_node_unref(pp);
	}
	dom_node_unref(p);
}
