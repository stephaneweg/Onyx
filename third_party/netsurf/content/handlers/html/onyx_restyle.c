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
 * the same style; references held) -- the inheritance --, their parent's custom
 * properties (libcss keeps them in the node data, not in the computed style:
 * css_onyx_node_vars_ref / css_onyx_vars_same), the :hover state of each node their
 * selection tried :hover on (the element itself and ancestors; a :hover tried on another
 * node is not kept) -- whose notes for onyx_hover.c are replayed --, and an epoch that a
 * new selection context, a media change or a shadow DOM switch moves. When the new
 * context only adds sheets to the old one (a late <link>, a script's <style>: GitHub
 * loads its sheets after the page), a kept selection of an epoch since stays when no
 * selector of the added sheets matches the element (css_select_style_onyx_probe on a
 * context of those sheets only; a probe that looks at :hover on another node or at the
 * structure counts as a match). Never kept: the elements in a shadow tree, shadow hosts
 * and their light children (the scoping has more inputs). :has() never matches in
 * libcss, and the other state pseudo-classes answer no.
 *
 * The PC bench: NS_NORESTYLE=1 selects every element; NS_RESTYLE_CHECK=1 compares each
 * kept selection with a new one (RESTYLE-MISMATCH lines).
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
	const void *pvars;	/* its parent's custom properties (libcss, a reference) */
};

/* The sheets added since an epoch (html_css_restyle): the kept selections of that epoch
 * or later are still right for an element no selector of the added sheets matches */
struct onyx_restyle {
	css_select_ctx *probe;		/* the sheets added since probe_epoch, or NULL */
	unsigned int probe_epoch;
	const css_stylesheet **base;	/* the context's sheets at probe_epoch */
	uint32_t nbase;
	bool chained;			/* base holds them */
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
	if (m->pvars != NULL)
		css_onyx_vars_release(m->pvars);
	m->pvars = NULL;
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

/** p's custom properties (its selection's node data): a reference, or NULL */
static const void *osr_vars_ref(dom_node *p)
{
	void *data = NULL;

	if (p == NULL || dom_node_get_user_data(p, corestring_dom___ns_key_libcss_node_data,
			&data) != DOM_NO_ERR || data == NULL)
		return NULL;
	return css_onyx_node_vars_ref(data);
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
		onyx_restyle_invalidate_all(c);
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
static void osr_chain_reset(html_content *c)
{
	struct onyx_restyle *st = c->onyx_rs;

	if (st == NULL)
		return;
	if (st->probe != NULL)
		css_select_ctx_destroy(st->probe);
	st->probe = NULL;
	free(st->base);
	st->base = NULL;
	st->nbase = 0;
	st->chained = false;
}

void onyx_restyle_invalidate_all(html_content *c)
{
	c->restyle_epoch++;
	osr_chain_reset(c);
}

/** A context's sheets (malloc'd), or NULL */
static const css_stylesheet **osr_sheets(css_select_ctx *ctx, uint32_t *n)
{
	const css_stylesheet **v;
	uint32_t i;

	*n = 0;
	if (ctx == NULL || css_select_ctx_count_sheets(ctx, n) != CSS_OK)
		return NULL;
	v = malloc((*n + 1) * sizeof(*v));
	if (v == NULL)
		return NULL;
	for (i = 0; i < *n; i++) {
		if (css_select_ctx_get_sheet(ctx, i, &v[i]) != CSS_OK)
			v[i] = NULL;
	}
	return v;
}

/* exported function documented in html/onyx_restyle.h */
void onyx_restyle_sheets_changed(html_content *c, css_select_ctx *old_ctx,
		css_select_ctx *new_ctx)
{
	struct onyx_restyle *st = c->onyx_rs;
	const css_stylesheet **ov, **nv;
	uint32_t on, nn, i, j, k;
	bool sub = true;

	if (st == NULL) {
		st = c->onyx_rs = calloc(1, sizeof(*st));
		if (st == NULL) {
			c->restyle_epoch++;
			return;
		}
	}
	ov = osr_sheets(old_ctx, &on);
	nv = osr_sheets(new_ctx, &nn);
	/* the old sheets all there, in the same order: sheets were added only */
	if (ov == NULL || nv == NULL) {
		sub = false;
	} else {
		for (i = 0, j = 0; i < on && sub; i++) {
			while (j < nn && nv[j] != ov[i])
				j++;
			if (j == nn)
				sub = false;
			else
				j++;
		}
	}
	if (!sub) {
		free(ov);
		free(nv);
		onyx_restyle_invalidate_all(c);
		return;
	}
	if (!st->chained) {
		/* (the chain starts at the old context's epoch) */
		st->base = ov;
		st->nbase = on;
		st->probe_epoch = c->restyle_epoch;
		st->chained = true;
		ov = NULL;
	}
	free(ov);
	c->restyle_epoch++;

	/* the probe: the sheets now that the chain's base had not */
	if (st->probe != NULL)
		css_select_ctx_destroy(st->probe);
	st->probe = NULL;
	if (css_select_ctx_create(&st->probe) != CSS_OK) {
		st->probe = NULL;
		free(nv);
		osr_chain_reset(c);
		return;
	}
	for (i = 0; i < nn; i++) {
		bool in_base = false;
		for (k = 0; k < st->nbase && !in_base; k++)
			in_base = st->base[k] == nv[i];
		if (!in_base && nv[i] != NULL &&
		    css_select_ctx_append_sheet(st->probe, nv[i], CSS_ORIGIN_AUTHOR,
				"screen") != CSS_OK) {
			free(nv);
			osr_chain_reset(c);
			return;
		}
	}
	free(nv);
}

/* exported function documented in html/onyx_restyle.h */
void onyx_restyle_fini(html_content *c)
{
	osr_chain_reset(c);
	free(c->onyx_rs);
	c->onyx_rs = NULL;
}

/* the probe tried :hover on another node than the one styled (libcss's selection state
 * tries it on the node itself always: the kept selection's own :hover state covers it) */
static bool osr_probe_hover_other;

static void osr_probe_note(void *ctx, struct dom_node *tested, struct dom_node *styled)
{
	(void) ctx;
	if (tested != styled)
		osr_probe_hover_other = true;
}

/** Whether a selector of the sheets added since the memo's epoch matches n (or the probe
 * looked at :hover on another node or at the tree's structure: its selection is made
 * again) */
static bool osr_probe(html_content *c, dom_node *n,
		const css_computed_style *parent_style,
		const css_computed_style *root_style)
{
	struct onyx_restyle *st = c->onyx_rs;
	nscss_select_ctx ctx;
	bool matched;

	memset(&ctx, 0, sizeof(ctx));
	ctx.ctx = st->probe;
	ctx.quirks = (c->quirks == DOM_DOCUMENT_QUIRKS_MODE_FULL);
	ctx.base_url = c->base_url;
	ctx.universal = c->universal;
	ctx.root_style = root_style;
	ctx.parent_style = parent_style;
	nscss_hover_node = c->hover_node;
	nscss_hover_note = osr_probe_note;
	nscss_hover_note_ctx = c;
	osr_probe_hover_other = false;
	nscss_struct_used = 0;
	nscss_styled_node = n;
	matched = nscss_probe_style(&ctx, st->probe, n, &c->media, &c->unit_len_ctx);
	if (osr_probe_hover_other || nscss_struct_used != 0)
		matched = true;
	nscss_hover_note = NULL;
	nscss_hover_node = NULL;
	nscss_styled_node = NULL;
	return matched;
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
	{
		/* (the PC bench: NS_NORESTYLE=1 selects every element, to compare) */
		static int off = -1;
		if (off < 0)
			off = getenv("NS_NORESTYLE") != NULL;
		if (off)
			return NULL;
	}
	p = osr_parent(n);
	pm = osr_get(p);
	if (pm != NULL)
		chain = pm->chain > pm->kids_mark ? pm->chain : pm->kids_mark;
	m->chain = chain;

	forced = m->self_mark > m->serial || (pm != NULL && pm->subtree > m->serial);
	if (forced)
		m->subtree = cur;
	valid = !forced && m->res != NULL && m->serial != 0 &&
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
	if (valid) {
		/* its parent's custom properties (not in its computed style) */
		const void *pv = osr_vars_ref(p);
		valid = css_onyx_vars_same(pv, m->pvars);
		if (valid && pv != m->pvars) {
			/* (the same, made again: a getComputedStyle's selection) */
			css_onyx_vars_release(m->pvars);
			m->pvars = pv;
		} else if (pv != NULL) {
			css_onyx_vars_release(pv);
		}
	}
	if (p != NULL)
		dom_node_unref(p);
	if (valid && m->epoch != c->restyle_epoch) {
		/* sheets added since: kept when none of theirs matches it */
		struct onyx_restyle *st = c->onyx_rs;
		if (st == NULL || st->probe == NULL || m->epoch < st->probe_epoch ||
		    osr_probe(c, n, parent_style, root_style))
			valid = false;
		else
			m->epoch = c->restyle_epoch;
	}
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
	{
		dom_node *p = osr_parent(n);
		m->pvars = osr_vars_ref(p);
		if (p != NULL)
			dom_node_unref(p);
	}
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

/* exported function documented in html/onyx_restyle.h */
bool onyx_restyle_checking(void)
{
	static int on = -1;

	if (on < 0)
		on = getenv("NS_RESTYLE_CHECK") != NULL;
	return on;
}

/* exported function documented in html/onyx_restyle.h */
void onyx_restyle_check(html_content *c, dom_node *n, const css_select_results *kept,
		css_select_results *fresh)
{
	int i;

	(void) c;
	if (fresh == NULL)
		return;
	for (i = 0; i < CSS_PSEUDO_ELEMENT_COUNT; i++) {
		if (kept->styles[i] != fresh->styles[i]) {
			dom_string *name = NULL, *id = NULL, *cls = NULL;
			dom_node_get_node_name(n, &name);
			dom_element_get_attribute(n, corestring_dom_id, &id);
			dom_element_get_attribute(n, corestring_dom_class, &cls);
			fprintf(stderr, "RESTYLE-MISMATCH %s id=%s class=%s pseudo %d\n",
					name ? dom_string_data(name) : "?",
					id ? dom_string_data(id) : "",
					cls ? dom_string_data(cls) : "", i);
			if (name != NULL)
				dom_string_unref(name);
			if (id != NULL)
				dom_string_unref(id);
			if (cls != NULL)
				dom_string_unref(cls);
			break;
		}
	}
	css_select_results_destroy(fresh);
}
