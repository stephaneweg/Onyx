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
 * Onyx: CSS :hover without building the boxes again.
 *
 * The big engines' way: when the node under the pointer changes, only the elements whose
 * :hover state changed -- and only those a :hover selector was ever tried on (the selection
 * tells: onyx_hover_note) -- have their subtree's styles selected again. When the new styles
 * differ from the old ones only in how boxes are painted (libcss:
 * css_computed_style_paint_only_change), the boxes take the new styles in place and just
 * their rectangles are redrawn: no box tree built again, no layout. Anything else (a layout
 * property, a pseudo-element appearing, an image to fetch, a :hover tried on a sibling, a
 * table's collapsed borders...) answers false: the caller builds the boxes again as before.
 *
 * libcss interns computed styles: two sibling links with the same rules share one style
 * pointer. A box is therefore matched by (its owning element, its style): an element's own
 * box carries its node; the boxes without one (text, ::before / ::after, list markers, an
 * inline's end) belong to the element around them in the box tree -- or, in an inline
 * container, to the inline element whose INLINE ... INLINE_END range they are in.
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dom/dom.h>
#include <libcss/libcss.h>

#include "utils/corestrings.h"
#include "utils/log.h"
#include "content/content_protected.h"
#include "css/utils.h"
#include "css/dump.h"
#include "css/select.h"

#include "html/private.h"
#include "html/box.h"
#include "html/box_inspect.h"
#include "html/box_construct.h"
#include "html/onyx_hover.h"
#include "html/onyx_paint.h"
#include "netsurf/onyx_perf.h"

#define HV_FAIL(h, reason) do { if (!(h)->fail) (h)->why = (reason); (h)->fail = true; } while (0)

/* ---- the set of nodes :hover was tried on ------------------------------------------------- */

static unsigned int hv_slot(const void *p, unsigned int cap)
{
	uint64_t x = (uint64_t) (uintptr_t) p;

	x ^= x >> 29;
	x *= 0x9E3779B97F4A7C15ull;
	return (unsigned int) (x >> 32) & (cap - 1);
}

static bool hv_set_has(const html_content *c, const struct dom_node *n)
{
	unsigned int i;

	if (c->hover_tested_n == 0)
		return false;
	for (i = hv_slot(n, c->hover_tested_cap); c->hover_tested[i] != NULL;
	     i = (i + 1) & (c->hover_tested_cap - 1))
		if (c->hover_tested[i] == n)
			return true;
	return false;
}

static void hv_set_add(html_content *c, struct dom_node *n)
{
	unsigned int i;

	if (hv_set_has(c, n))
		return;
	if ((c->hover_tested_n + 1) * 2 > c->hover_tested_cap) {
		unsigned int cap = c->hover_tested_cap ? c->hover_tested_cap * 2 : 64, k;
		struct dom_node **t = calloc(cap, sizeof(*t)), **old = c->hover_tested;
		unsigned int oldcap = c->hover_tested_cap;

		if (t == NULL) {
			c->hover_other = true;	/* (then every change builds the boxes again) */
			return;
		}
		c->hover_tested = t;
		c->hover_tested_cap = cap;
		for (k = 0; k < oldcap; k++) {
			if (old[k] == NULL)
				continue;
			for (i = hv_slot(old[k], cap); t[i] != NULL; i = (i + 1) & (cap - 1))
				;
			t[i] = old[k];
		}
		free(old);
	}
	for (i = hv_slot(n, c->hover_tested_cap); c->hover_tested[i] != NULL;
	     i = (i + 1) & (c->hover_tested_cap - 1))
		;
	c->hover_tested[i] = n;
	c->hover_tested_n++;
}

/** Whether a is b or one of its ancestors. */
static bool hv_is_ancestor(struct dom_node *a, struct dom_node *b)
{
	struct dom_node *n = b;

	if (n == NULL)
		return false;
	dom_node_ref(n);
	while (n != NULL) {
		struct dom_node *parent = NULL;
		if (n == a) {
			dom_node_unref(n);
			return true;
		}
		if (dom_node_get_parent_node(n, &parent) != DOM_NO_ERR)
			parent = NULL;
		dom_node_unref(n);
		n = parent;
	}
	return false;
}

/* exported function documented in html/onyx_hover.h */
void onyx_hover_note(void *ctx, struct dom_node *tested, struct dom_node *styled)
{
	html_content *c = ctx;

	if (c == NULL || tested == NULL)
		return;
	hv_set_add(c, tested);
	if (tested != styled && !c->hover_other && !hv_is_ancestor(tested, styled))
		c->hover_other = true;	/* (E:hover + F: F's style depends on a sibling) */
}

/* exported function documented in html/onyx_hover.h */
void onyx_hover_reset(struct html_content *c)
{
	if (c->hover_tested != NULL)
		memset(c->hover_tested, 0, c->hover_tested_cap * sizeof(*c->hover_tested));
	c->hover_tested_n = 0;
	c->hover_other = false;
}

/* exported function documented in html/onyx_hover.h */
void onyx_hover_release(struct html_content *c)
{
	for (unsigned int k = 0; k < c->hover_old_n; k++)
		css_select_results_destroy(c->hover_old[k]);
	c->hover_old_n = 0;
}

/* exported function documented in html/onyx_hover.h */
void onyx_hover_fini(struct html_content *c)
{
	onyx_hover_release(c);
	free(c->hover_old);
	c->hover_old = NULL;
	c->hover_old_cap = 0;
	free(c->hover_tested);
	c->hover_tested = NULL;
	c->hover_tested_cap = c->hover_tested_n = 0;
}

/** Keep results a hover replaced until the box tree goes (false: no room). */
static bool hv_keep_old(html_content *c, css_select_results *res)
{
	if (c->hover_old_n == c->hover_old_cap) {
		unsigned int cap = c->hover_old_cap ? c->hover_old_cap * 2 : 32;
		void **t = realloc(c->hover_old, cap * sizeof(*t));
		if (t == NULL)
			return false;
		c->hover_old = t;
		c->hover_old_cap = cap;
	}
	c->hover_old[c->hover_old_n++] = res;
	return true;
}

/* ---- the restyle ------------------------------------------------------------------------ */

#define HV_DEPTH 256
#define HV_KEEP_MAX 4096	/* results kept past it: the boxes built again */

struct hv_map {
	struct dom_node *el;
	const css_computed_style *old, *nw;
	bool moved;		/* its translation changed: the box moves */
};

struct hv_job {
	struct box *box;
	css_select_results *res;
};

struct hv {
	const char *why;	/* (the first reason it fails: NS_PERF logs it) */
	html_content *c;
	struct hv_map *map;
	int nmap, capmap;
	struct hv_job *job;
	int njob, capjob;
	bool fail;
};

/** The element (and its ancestors) of n, from n up: *count of them. */
static int hv_chain(struct dom_node *n, struct dom_node **out, int max)
{
	int k = 0;

	if (n == NULL)
		return 0;
	dom_node_ref(n);
	while (n != NULL && k < max) {
		struct dom_node *parent = NULL;
		dom_node_type type;

		if (dom_node_get_node_type(n, &type) == DOM_NO_ERR &&
		    type == DOM_ELEMENT_NODE)
			out[k++] = n;	/* (held by the tree: no ref kept) */
		if (dom_node_get_parent_node(n, &parent) != DOM_NO_ERR)
			parent = NULL;
		dom_node_unref(n);
		n = parent;
	}
	if (n != NULL) {
		dom_node_unref(n);
		return -1;	/* (deeper than max) */
	}
	return k;
}

static bool hv_in(struct dom_node *n, struct dom_node **list, int count)
{
	for (int k = 0; k < count; k++)
		if (list[k] == n)
			return true;
	return false;
}

static struct box *hv_box_of(struct dom_node *n)
{
	struct box *b = NULL;

	if (dom_node_get_user_data(n, corestring_dom___ns_key_box_node_data,
			(void *) &b) != DOM_NO_ERR)
		return NULL;
	return b;
}

static void hv_map_add(struct hv *h, struct dom_node *el, const css_computed_style *old,
		const css_computed_style *nw, bool moved)
{
	if (h->nmap == h->capmap) {
		int cap = h->capmap ? h->capmap * 2 : 32;
		struct hv_map *m = realloc(h->map, cap * sizeof(*m));
		if (m == NULL) {
			h->fail = true;
			return;
		}
		h->map = m;
		h->capmap = cap;
	}
	h->map[h->nmap].el = el;
	h->map[h->nmap].old = old;
	h->map[h->nmap].nw = nw;
	h->map[h->nmap].moved = moved;
	h->nmap++;
}

static void hv_job_add(struct hv *h, struct box *b, css_select_results *res)
{
	if (h->njob == h->capjob) {
		int cap = h->capjob ? h->capjob * 2 : 16;
		struct hv_job *j = realloc(h->job, cap * sizeof(*j));
		if (j == NULL) {
			h->fail = true;
			css_select_results_destroy(res);
			return;
		}
		h->job = j;
		h->capjob = cap;
	}
	h->job[h->njob].box = b;
	h->job[h->njob].res = res;
	h->njob++;
}

static const struct hv_map *hv_find(const struct hv *h, struct dom_node *el,
		const css_computed_style *old)
{
	for (int k = 0; k < h->nmap; k++)
		if (h->map[k].old == old && h->map[k].el == el)
			return &h->map[k];
	return NULL;
}

/** Whether the old and new style's background images differ by one that must be fetched. */
static bool hv_new_image(const css_computed_style *a, const css_computed_style *b)
{
	lwc_string *ia = NULL, *ib = NULL;
	uint8_t ta = css_computed_background_image(a, &ia);
	uint8_t tb = css_computed_background_image(b, &ib);
	bool ga, gb;

	if (ta != CSS_BACKGROUND_IMAGE_IMAGE)
		ia = NULL;
	if (tb != CSS_BACKGROUND_IMAGE_IMAGE)
		ib = NULL;
	if (ia == ib)
		return false;
	ga = ia == NULL || onyx_is_gradient_url(lwc_string_data(ia));
	gb = ib == NULL || onyx_is_gradient_url(lwc_string_data(ib));
	return !(ga && gb);
}

static bool hv_borders_differ(const css_computed_style *a, const css_computed_style *b)
{
	css_color ca, cb;

	css_computed_border_top_color(a, &ca); css_computed_border_top_color(b, &cb);
	if (ca != cb) return true;
	css_computed_border_right_color(a, &ca); css_computed_border_right_color(b, &cb);
	if (ca != cb) return true;
	css_computed_border_bottom_color(a, &ca); css_computed_border_bottom_color(b, &cb);
	if (ca != cb) return true;
	css_computed_border_left_color(a, &ca); css_computed_border_left_color(b, &cb);
	return ca != cb;
}

/** The element n's subtree styled again, under parent_style: the map and jobs filled. */
static void hv_restyle(struct hv *h, struct dom_node *n,
		const css_computed_style *parent_style, bool root, int depth)
{
	html_content *c = h->c;
	struct box *b;
	css_select_results *res;
	const css_computed_style *child_style;
	struct dom_node *ch = NULL, *next;
	bool changed = false;

	if (h->fail)
		return;
	if (depth > HV_DEPTH) {
		HV_FAIL(h, "depth");
		return;
	}
	b = hv_box_of(n);
	res = box_style_select(c, parent_style, root ? NULL : c->layout->style, n);
	if (res == NULL || res->styles[CSS_PSEUDO_ELEMENT_NONE] == NULL) {
		if (res != NULL)
			css_select_results_destroy(res);
		HV_FAIL(h, "select");
		return;
	}
	if (b == NULL) {
		/* not boxed: still not shown, else it would need boxes */
		bool none = ns_computed_display(res->styles[CSS_PSEUDO_ELEMENT_NONE],
				false) == CSS_DISPLAY_NONE;
		css_select_results_destroy(res);
		if (!none)
			HV_FAIL(h, "unboxed element shown");
		return;
	}
	if (b->styles == NULL ||
	    b->style != b->styles->styles[CSS_PSEUDO_ELEMENT_NONE]) {
		css_select_results_destroy(res);
		HV_FAIL(h, "box without its styles");
		return;
	}
	for (int p = 0; p < CSS_PSEUDO_ELEMENT_COUNT; p++) {
		const css_computed_style *o = b->styles->styles[p];
		const css_computed_style *nw = res->styles[p];
		bool moved = false;

		if (o == nw)
			continue;
		if (root) {
			/* the root's own style: the units' (rem) and the canvas' */
			css_select_results_destroy(res);
			HV_FAIL(h, "root style");
			return;
		}
		if (o == NULL || nw == NULL ||
		    !css_computed_style_paint_only_change(o, nw, &moved) ||
		    hv_new_image(o, nw) ||
		    ((b->type == BOX_TABLE || b->type == BOX_TABLE_CELL ||
		      b->type == BOX_TABLE_ROW || b->type == BOX_TABLE_ROW_GROUP) &&
		     hv_borders_differ(o, nw))) {
			if (getenv("NS_HOVER_DUMP") != NULL && o != NULL && nw != NULL) {
				fprintf(stderr, "HOVER-OLD ");
				nscss_dump_computed_style(stderr, o);
				fprintf(stderr, "\nHOVER-NEW ");
				nscss_dump_computed_style(stderr, nw);
				fprintf(stderr, "\n");
			}
			HV_FAIL(h, o == NULL || nw == NULL ? "pseudo-element appears" :
				!css_computed_style_paint_only_change(o, nw, &moved) ?
				"layout property" :
				hv_new_image(o, nw) ? "new image" : "table borders");
			css_select_results_destroy(res);
			return;
		}
		hv_map_add(h, n, o, nw, moved);
		changed = true;
	}
	if (changed) {
		child_style = res->styles[CSS_PSEUDO_ELEMENT_NONE];
		hv_job_add(h, b, res);
	} else {
		child_style = b->style;
		css_select_results_destroy(res);	/* (the same interned styles) */
	}
	if (h->fail || b->gadget != NULL || b->object != NULL)
		return;	/* (a form control's or an object's insides are not boxes) */

	if (dom_node_get_first_child(n, &ch) != DOM_NO_ERR)
		ch = NULL;
	while (ch != NULL && !h->fail) {
		dom_node_type type;

		if (dom_node_get_node_type(ch, &type) == DOM_NO_ERR &&
		    type == DOM_ELEMENT_NODE)
			hv_restyle(h, ch, child_style, false, depth + 1);
		next = NULL;
		dom_node_get_next_sibling(ch, &next);
		dom_node_unref(ch);
		ch = next;
	}
	if (ch != NULL)
		dom_node_unref(ch);
}

/** How far a box's painting may reach past its border box with this style. */
static int hv_reach(const html_content *c, const css_computed_style *style)
{
	struct onyx_box_shadow sh;
	int e = 2;

	if (onyx_box_shadow(style, &c->unit_len_ctx, 1.0f, &sh) && !sh.inset)
		e += (int) (fabsf(sh.x) + fabsf(sh.y) + sh.blur * 1.5f +
				(sh.spread > 0 ? sh.spread : 0) + 1);
	if (css_computed_outline_style(style) != CSS_OUTLINE_STYLE_NONE) {
		css_fixed len = 0;
		css_unit unit = CSS_UNIT_PX;
		css_computed_outline_width(style, &len, &unit);
		e += FIXTOINT(css_unit_len2device_px(style, &c->unit_len_ctx, len, unit)) + 1;
	}
	return e;
}

/** The box's painted rectangle (its border box, reach e around) redrawn. */
static void hv_redraw_box(html_content *c, struct box *b, int e)
{
	int x, y, w, h;

	box_coords(b, &x, &y);
	x -= b->border[LEFT].width + e;
	y -= b->border[TOP].width + e;
	w = b->border[LEFT].width + b->padding[LEFT] + b->width + b->padding[RIGHT] +
		b->border[RIGHT].width + 2 * e;
	h = b->border[TOP].width + b->padding[TOP] + b->height + b->padding[BOTTOM] +
		b->border[BOTTOM].width + 2 * e;
	if (b->type == BOX_INLINE && b->inline_end != NULL) {
		/* an inline's pieces run to its end box (maybe lines below) */
		int ex, ey;
		box_coords(b->inline_end, &ex, &ey);
		ex += b->inline_end->width + e;
		ey += b->inline_end->height + e;
		if (ex > x + w) w = ex - x;
		if (ey > y + h) h = ey - y;
		if (ey - b->inline_end->height - 2 * e < y) {
			/* (it ends on a later line, left of where it starts) */
			int lx = ex - b->inline_end->width - 2 * e;
			if (lx < x) {
				w += x - lx;
				x = lx;
			}
		}
	}
	if (w > 0 && h > 0)
		content__request_redraw(&c->base, x, y, w, h);
}

/** The box and all it holds (its descendants' bounds) redrawn, reach e around. */
static void hv_redraw_all(html_content *c, struct box *b, int e)
{
	int x, y, x0, y0, x1, y1;

	box_coords(b, &x, &y);
	x0 = -b->border[LEFT].width;
	y0 = -b->border[TOP].width;
	x1 = b->padding[LEFT] + b->width + b->padding[RIGHT] + b->border[RIGHT].width;
	y1 = b->padding[TOP] + b->height + b->padding[BOTTOM] + b->border[BOTTOM].width;
	if (b->descendant_x0 < x0) x0 = b->descendant_x0;
	if (b->descendant_y0 < y0) y0 = b->descendant_y0;
	if (b->descendant_x1 > x1) x1 = b->descendant_x1;
	if (b->descendant_y1 > y1) y1 = b->descendant_y1;
	content__request_redraw(&c->base, x + x0 - e, y + y0 - e,
			x1 - x0 + 2 * e, y1 - y0 + 2 * e);
}

/** The box's translation by style (rounded as the layout does), 0 when it has none. */
static void hv_translation(const html_content *c, const struct box *b,
		const css_computed_style *style, int *dx, int *dy)
{
	float tx, ty;
	int bw = b->border[LEFT].width + b->padding[LEFT] + b->width +
			b->padding[RIGHT] + b->border[RIGHT].width;
	int bh = b->border[TOP].width + b->padding[TOP] + b->height +
			b->padding[BOTTOM] + b->border[BOTTOM].width;

	*dx = *dy = 0;
	if (onyx_box_translate(style, &c->unit_len_ctx, bw, bh, &tx, &ty)) {
		*dx = (int) (tx < 0 ? tx - 0.5f : tx + 0.5f);
		*dy = (int) (ty < 0 ? ty - 0.5f : ty + 0.5f);
	}
}

/** Its ancestors' descendant bounds grown to hold the moved box. */
static void hv_grow_ancestors(struct box *b)
{
	int x0 = b->x + (b->descendant_x0 < -b->border[LEFT].width ? b->descendant_x0 :
			-b->border[LEFT].width);
	int y0 = b->y + (b->descendant_y0 < -b->border[TOP].width ? b->descendant_y0 :
			-b->border[TOP].width);
	int x1 = b->x + b->descendant_x1;
	int y1 = b->y + b->descendant_y1;
	int bx1 = b->x + b->padding[LEFT] + b->width + b->padding[RIGHT] +
			b->border[RIGHT].width;
	int by1 = b->y + b->padding[TOP] + b->height + b->padding[BOTTOM] +
			b->border[BOTTOM].width;

	if (bx1 > x1) x1 = bx1;
	if (by1 > y1) y1 = by1;
	for (struct box *p = b->parent; p != NULL; p = p->parent) {
		bool grew = false;
		if (x0 < p->descendant_x0) { p->descendant_x0 = x0; grew = true; }
		if (y0 < p->descendant_y0) { p->descendant_y0 = y0; grew = true; }
		if (x1 > p->descendant_x1) { p->descendant_x1 = x1; grew = true; }
		if (y1 > p->descendant_y1) { p->descendant_y1 = y1; grew = true; }
		if (!grew)
			break;
		x0 += p->x; y0 += p->y; x1 += p->x; y1 += p->y;
	}
}

/** One box: its new style when it has one (apply), or whether it could take it (check). */
static bool hv_box(struct hv *h, struct box *b, struct dom_node *own, bool pchanged,
		bool apply)
{
	const struct hv_map *m;
	const css_computed_style *nw;

	if (b->flags & STYLE_OWNED) {
		/* an anonymous box's style derived from its parent's (box_normalise.c):
		 * derived again from the parent's new one (the parent is done first) */
		if (!pchanged)
			return false;
		if (b->parent == NULL || b->parent->style == NULL || b->style == NULL) {
			HV_FAIL(h, "anonymous box style");
			return true;
		}
		if (apply) {
			html_content *c = h->c;
			nscss_select_ctx ctx;
			css_computed_style *st;

			ctx.ctx = c->select_ctx;
			ctx.quirks = (c->quirks == DOM_DOCUMENT_QUIRKS_MODE_FULL);
			ctx.base_url = c->base_url;
			ctx.universal = c->universal;
			ctx.root_style = NULL;
			ctx.parent_style = NULL;
			st = nscss_get_blank_style(&ctx, &c->unit_len_ctx, b->parent->style);
			if (st != NULL) {
				css_computed_style_destroy(b->style);
				b->style = st;
				hv_redraw_box(c, b, 2);
			}
		}
		return true;
	}
	if (b->style == NULL)
		return pchanged;
	m = hv_find(h, own, b->style);
	if (m == NULL)
		return false;
	nw = m->nw;
	if (apply) {
		int e = hv_reach(h->c, b->style), e2 = hv_reach(h->c, nw);
		/* moved as the layout moves it: not text, not a non-replaced inline */
		bool moves = m->moved && b->type != BOX_TEXT && b->type != BOX_INLINE_END &&
			(b->type != BOX_INLINE || b->object != NULL);
		int ox = 0, oy = 0, nx = 0, ny = 0;

		if (moves) {
			hv_translation(h->c, b, b->style, &ox, &oy);
			hv_translation(h->c, b, nw, &nx, &ny);
			moves = ox != nx || oy != ny;
		}
		if (moves)
			hv_redraw_all(h->c, b, e > e2 ? e : e2);	/* where it was */
		b->style = (css_computed_style *) nw;
		if (b->type != BOX_TABLE_CELL) {
			css_computed_border_top_color(nw, &b->border[TOP].c);
			css_computed_border_right_color(nw, &b->border[RIGHT].c);
			css_computed_border_bottom_color(nw, &b->border[BOTTOM].c);
			css_computed_border_left_color(nw, &b->border[LEFT].c);
		}
		if (moves) {
			b->x += nx - ox;
			b->y += ny - oy;
			hv_grow_ancestors(b);
			hv_redraw_all(h->c, b, e > e2 ? e : e2);	/* where it is */
		} else {
			hv_redraw_box(h->c, b, e > e2 ? e : e2);
		}
	}
	return true;
}

/** A list of sibling boxes (and their descendants) under the element `owner`. */
static void hv_walk(struct hv *h, struct box *first, struct dom_node *owner, bool pchanged,
		bool apply, int depth)
{
	struct dom_node *stack[64];
	int sp = 0;
	struct dom_node *cur = owner;

	if (depth > HV_DEPTH) {
		HV_FAIL(h, "box depth");
		return;
	}
	for (struct box *b = first; b != NULL && !h->fail; b = b->next) {
		struct dom_node *own = b->type == BOX_INLINE_END ? cur :
			b->node != NULL ? b->node : cur;
		bool ch = hv_box(h, b, own, pchanged, apply);

		if (b->list_marker != NULL)
			hv_box(h, b->list_marker, own, ch, apply);
		if (b->type == BOX_INLINE_END) {
			if (sp > 0)
				cur = stack[--sp];
		} else if (b->type == BOX_INLINE && b->node != NULL) {
			if (sp == 64) {
				HV_FAIL(h, "inline depth");
				return;
			}
			stack[sp++] = cur;
			cur = b->node;
		}
		if (b->children != NULL)
			hv_walk(h, b->children, b->type == BOX_INLINE_END ? cur :
					b->node != NULL ? b->node : cur, ch, apply, depth + 1);
	}
}

/** A subtree root: its parent element's box style, else NULL (then no restyle). */
static const css_computed_style *hv_parent_style(struct dom_node *n)
{
	struct dom_node *parent = NULL;
	struct box *pb;
	dom_node_type type;

	if (dom_node_get_parent_node(n, &parent) != DOM_NO_ERR || parent == NULL)
		return NULL;
	if (dom_node_get_node_type(parent, &type) != DOM_NO_ERR ||
	    type != DOM_ELEMENT_NODE) {
		dom_node_unref(parent);
		return NULL;	/* (the document element itself) */
	}
	pb = hv_box_of(parent);
	dom_node_unref(parent);
	return pb != NULL ? pb->style : NULL;
}

/* exported function documented in html/onyx_hover.h */
bool onyx_hover_restyle(struct html_content *c, struct dom_node *old_node)
{
	struct dom_node *oldc[HV_DEPTH], *newc[HV_DEPTH], *roots[2];
	int no, nn, nroots = 0;
	struct hv h;

	static int full = -1;

	if (full < 0)	/* (NS_HOVER_FULL: always the boxes built again -- to compare) */
		full = getenv("NS_HOVER_FULL") != NULL;
	if (full)
		return false;
	if (c->layout == NULL || c->box_conversion_context != NULL || c->base.locked ||
	    c->rebox_pending || c->hover_other || c->aborted ||
	    c->hover_old_n > HV_KEEP_MAX) {
		if (onyx_perf_on())
			fprintf(stderr, "ONYX-PERF hover:rebox (%s)\n",
				c->hover_other ? ":hover on a sibling" :
				c->rebox_pending ? "rebox pending" : "busy");
		return false;
	}
	no = hv_chain(old_node, oldc, HV_DEPTH);
	nn = hv_chain(c->hover_node, newc, HV_DEPTH);
	if (no < 0 || nn < 0)
		return false;

	/* the topmost element that left (entered) the hover chain and that :hover was tried
	 * on: its subtree holds every other such one of its chain */
	for (int k = no - 1; k >= 0; k--)
		if (!hv_in(oldc[k], newc, nn) && hv_set_has(c, oldc[k])) {
			roots[nroots++] = oldc[k];
			break;
		}
	for (int k = nn - 1; k >= 0; k--)
		if (!hv_in(newc[k], oldc, no) && hv_set_has(c, newc[k])) {
			roots[nroots++] = newc[k];
			break;
		}
	if (nroots == 0)
		return true;	/* no :hover rule sees the change */

	memset(&h, 0, sizeof(h));
	h.c = c;
	for (int r = 0; r < nroots && !h.fail; r++) {
		struct box *rb = hv_box_of(roots[r]);
		if (rb != NULL && rb == c->layout) {
			hv_restyle(&h, roots[r], NULL, true, 0);	/* the root */
		} else {
			const css_computed_style *ps = hv_parent_style(roots[r]);
			if (ps == NULL)
				HV_FAIL(&h, "no parent style");
			else
				hv_restyle(&h, roots[r], ps, false, 0);
		}
	}
	if (!h.fail && h.nmap > 0) {
		hv_walk(&h, c->layout, NULL, false, false, 0);		/* check */
		if (!h.fail)
			hv_walk(&h, c->layout, NULL, false, true, 0);	/* apply */
	}
	for (int k = 0; k < h.njob; k++) {
		if (h.fail) {
			css_select_results_destroy(h.job[k].res);
		} else {
			/* the box's own style was set by the walk; its results swapped (the
			 * old ones kept: a box the walk missed may still point at them) */
			css_select_results *old = h.job[k].box->styles;
			h.job[k].box->styles = h.job[k].res;
			h.job[k].box->style = h.job[k].res->styles[CSS_PSEUDO_ELEMENT_NONE];
			if (!hv_keep_old(c, old))
				css_select_results_destroy(old);
		}
	}
	free(h.map);
	free(h.job);
	if (h.fail && onyx_perf_on())
		fprintf(stderr, "ONYX-PERF hover:rebox (%s)\n", h.why ? h.why : "memory");
	return !h.fail;
}
