/*
 * Copyright 2022 Michael Drake <tlsa@netsurf-browser.org>
 *
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
 * HTML layout implementation: display: flex.
 *
 * Onyx: rewritten after CSS Flexible Box Layout Module Level 1, section 9 -- the
 * algorithm Blink, Taffy and Yoga implement, measured against Chromium element by
 * element (tools/tests/netsurf/layouttest.sh):
 *
 *  1. the items: the in-flow children in `order`; each one's margins, borders,
 *     paddings, specified sizes and min / max sizes (percentages of the container);
 *  2. each item's flex base size (flex-basis; auto: its main size property; content:
 *     its max-content width in a row, its height laid out at its cross size in a
 *     column; a ratio -- an image's, aspect-ratio -- and a definite cross size: the
 *     transferred size) and its automatic minimum size (4.5: its min-content size,
 *     no larger than its specified or transferred size), its hypothetical main size;
 *  3. a column's auto height: its items' hypothetical sizes, within min-height /
 *     max-height -- and the items flexed in that height;
 *  4. the lines (flex-wrap), the flexible lengths resolved line by line (9.7:
 *     inflexible items frozen, the free space shared by grow or scaled shrink, the
 *     min / max violations frozen, in floating point);
 *  5. the cross sizes: each item laid out at its main size, the lines' cross sizes
 *     (baselines), align-content, stretched items (their height is definite for
 *     their children's percentages: DEF_HEIGHT, and laid out again when their
 *     content depends on it);
 *  6. main-axis alignment (auto margins, justify-content, the gaps, reversed
 *     directions), cross-axis alignment (auto margins, align-self, wrap-reverse).
 *
 * Positions and sizes are kept in floating point and rounded when stored in the
 * boxes (the left edge and the size each rounded, as Chrome's rectangles are). The
 * container's width, height, margins, paddings and borders are its caller's (a
 * block context, a flex / grid container, an absolute or floated box's layout); its
 * min-height and max-height are applied here.
 */

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "utils/log.h"
#include "utils/utils.h"
#include "netsurf/content.h"
#include "content/content.h"
#include "css/utils.h"

#include "html/box.h"
#include "html/html.h"
#include "html/private.h"
#include "html/box_inspect.h"
#include "html/layout_internal.h"

#define FX_INF 1.0e9f

/** An item's state through the algorithm (sizes: content box, px) */
struct fx_item {
	struct box *box;
	int order;
	float grow, shrink;

	int spec_main, spec_cross;	/* specified sizes, or AUTO */
	float min_main, max_main;
	float min_cross, max_cross;
	float mbp_main, mbp_cross;	/* margins (auto: 0), borders, paddings */
	float bp_main, bp_cross;	/* borders and paddings */
	float ratio;			/* width / height, or 0 */

	float base;			/* flex base size */
	float hypo;			/* hypothetical main size */
	float target;			/* resolved main size */
	float cross;			/* cross size */
	float baseline;			/* from the margin box's top */
	float main_pos, cross_off;	/* margin box, logical */
	float am_start, am_end;		/* the main auto margins' used sizes */
	float ac_start;			/* the cross start auto margin's */

	uint8_t align;
	bool auto_ms, auto_me, auto_cs, auto_ce;
	bool stretch;			/* align stretch with an auto cross size */
	bool stretched;			/* its cross size was set by its container */
	bool frozen, min_v, max_v;
	int measured;			/* column: the height laid out, or AUTO */
	/* Onyx: the inputs of its last layout (fx_ensure) */
	bool laid, ldef;
	int lw, lh, lavail;
};

struct fx_line {
	size_t first, count;
	float cross;
	float pos;
	float ascent;
};

struct fx {
	html_content *content;
	const css_unit_ctx *uctx;
	struct box *flex;

	bool row;
	bool reverse;
	bool wrap_reverse;
	bool single;

	float main_size;
	bool main_definite;		/* for the items' percentages */
	float cross_size;		/* the container's inner cross size (row: its height) */
	bool cross_definite;
	int min_h, max_h;		/* the container's min / max-height */

	float main_gap, cross_gap;

	enum box_side ms, me, cs, ce;	/* logical sides */

	struct fx_item *item;
	size_t n;
	struct fx_line *line;
	size_t nlines;
};


static inline float fx_clamp(float v, float mn, float mx)
{
	if (v > mx)
		v = mx;
	if (v < mn)
		v = mn;
	return v;
}

static inline int fx_round(float v)
{
	return (int) lroundf(v);
}

static inline int fx_margin(const struct box *b, enum box_side side)
{
	return b->margin[side] == AUTO ? 0 : b->margin[side];
}


/*
 * Onyx: the layout memo (layout_internal.h). Two open-addressing tables, their entries
 * valid for the current pass only (lm_gen): the sizes a box's layout gave per inputs,
 * and the inputs each box's subtree is laid out at now.
 */
struct lm_key {
	int w, h, avail;
	int def;
	int p[4], bw[4];
};

struct lm_ent {
	const struct box *box;
	unsigned gen;
	struct lm_key k;
	int ow, oh;
};

struct lm_tab {
	struct lm_ent *e;
	size_t size, used;	/* size: a power of two */
};

static unsigned lm_gen;
static bool lm_active;
static struct lm_tab lm_sizes, lm_state;

void layout_memo_begin(void)
{
	lm_gen++;
	if (lm_gen == 0)
		lm_gen = 1;
	lm_sizes.used = lm_state.used = 0;
	lm_active = true;
}

void layout_memo_end(void)
{
	lm_active = false;
}

static void lm_key_of(const struct box *b, int w, int h, bool def, int avail,
		struct lm_key *k)
{
	int i;

	memset(k, 0, sizeof(*k));
	k->w = w;
	k->h = h;
	k->avail = avail;
	k->def = def ? 1 : 0;
	for (i = 0; i < 4; i++) {
		k->p[i] = b->padding[i];
		k->bw[i] = b->border[i].width;
	}
}

static size_t lm_hash(const struct box *b, const struct lm_key *k, bool with_key)
{
	uint64_t h = (uint64_t) (uintptr_t) b * 0x9E3779B97F4A7C15ull;

	if (with_key) {
		const int *v = (const int *) k;
		size_t i;
		for (i = 0; i < sizeof(*k) / sizeof(int); i++)
			h = (h ^ (uint32_t) v[i]) * 0x100000001B3ull;
	}
	return (size_t) (h ^ (h >> 29));
}

/** The entry of (box, key) -- by box alone when !with_key -- or a free slot */
static struct lm_ent *lm_find(struct lm_tab *t, const struct box *b,
		const struct lm_key *k, bool with_key)
{
	size_t i;

	if (t->size == 0)
		return NULL;
	i = lm_hash(b, k, with_key) & (t->size - 1);
	for (;;) {
		struct lm_ent *e = &t->e[i];
		if (e->gen != lm_gen)
			return e;
		if (e->box == b && (!with_key ||
				memcmp(&e->k, k, sizeof(*k)) == 0))
			return e;
		i = (i + 1) & (t->size - 1);
	}
}

static struct lm_ent *lm_insert(struct lm_tab *t, const struct box *b,
		const struct lm_key *k, bool with_key)
{
	struct lm_ent *e;

	if ((t->used + 1) * 2 > t->size) {
		size_t n = t->size ? t->size * 2 : 1024, i, old = t->size;
		struct lm_ent *oe = t->e, *ne = calloc(n, sizeof(*ne));
		if (ne == NULL)
			return NULL;
		t->e = ne;
		t->size = n;
		for (i = 0; i < old; i++) {
			if (oe[i].gen == lm_gen)
				*lm_find(t, oe[i].box, &oe[i].k, with_key) = oe[i];
		}
		free(oe);
	}
	e = lm_find(t, b, k, with_key);
	if (e->gen != lm_gen)
		t->used++;
	e->box = b;
	e->gen = lm_gen;
	e->k = *k;
	return e;
}

bool layout_memo_state_is(const struct box *b, int w, int h, bool def, int avail)
{
	struct lm_key k;
	struct lm_ent *e;

	if (!lm_active)
		return true;
	lm_key_of(b, w, h, def, avail, &k);
	e = lm_find(&lm_state, b, &k, false);
	return e != NULL && e->gen == lm_gen &&
			memcmp(&e->k, &k, sizeof(k)) == 0;
}

bool layout_memo_layout(struct box *b, int avail, bool size_only,
		bool (*lay)(void *ctx, struct box *b, int avail), void *ctx)
{
	struct lm_key k;
	struct lm_ent *e;

	if (!lm_active || b->type == BOX_TABLE)
		return lay(ctx, b, avail);

	lm_key_of(b, b->width, b->height, (b->flags & DEF_HEIGHT) != 0, avail, &k);
	e = lm_find(&lm_state, b, &k, false);
	if (e != NULL && e->gen == lm_gen &&
			memcmp(&e->k, &k, sizeof(k)) == 0) {
		/* its subtree is laid out at these inputs */
		e = lm_find(&lm_sizes, b, &k, true);
		if (e != NULL && e->gen == lm_gen) {
			b->width = e->ow;
			b->height = e->oh;
			return true;
		}
	} else if (size_only) {
		e = lm_find(&lm_sizes, b, &k, true);
		if (e != NULL && e->gen == lm_gen) {
			b->width = e->ow;
			b->height = e->oh;
			return true;
		}
	}

	if (!lay(ctx, b, avail))
		return false;

	e = lm_insert(&lm_sizes, b, &k, true);
	if (e != NULL) {
		e->ow = b->width;
		e->oh = b->height;
	}
	if (e == NULL || lm_insert(&lm_state, b, &k, false) == NULL)
		lm_active = false;	/* (no memory: every layout made) */
	return true;
}


/**
 * Lay a flex item's contents out at its current width (and height, when not AUTO).
 */
static bool fx_layout_item_now(void *ctx, struct box *b, int avail)
{
	struct fx *fx = ctx;
	bool ok = true;

	switch (b->type) {
	case BOX_BLOCK:
		ok = layout_block_context(b, -1, fx->content);
		break;
	case BOX_TABLE:
		b->float_container = b->parent;
		ok = layout_table(b, avail, fx->content);
		b->float_container = NULL;
		break;
	case BOX_FLEX:
	case BOX_INLINE_FLEX:
		ok = layout_flex(b, avail, fx->content);
		break;
	default:
		break;
	}
	if (!ok) {
		NSLOG(flex, ERROR, "box %p: layout failed", b);
	}
	return ok;
}

/**
 * Onyx: lay an item out through the layout memo (size only: its subtree is made right
 * by fx_ensure() at the end), its inputs noted for fx_ensure().
 */
static bool fx_layout_item(struct fx *fx, struct fx_item *it, int avail)
{
	struct box *b = it->box;

	it->lw = b->width;
	it->lh = b->height;
	it->ldef = (b->flags & DEF_HEIGHT) != 0;
	it->lavail = avail;
	it->laid = true;
	return layout_memo_layout(b, avail, true, fx_layout_item_now, fx);
}


/** Whether a percentage height (min, max) is set in a style */
static bool fx_style_pct_height(const css_computed_style *s)
{
	css_fixed v = 0;
	css_unit u = CSS_UNIT_PX;

	if (s == NULL)
		return false;
	if (css_computed_height(s, &v, &u) == CSS_HEIGHT_SET &&
			u == CSS_UNIT_PCT)
		return true;
	if (css_computed_min_height(s, &v, &u) == CSS_MIN_HEIGHT_SET &&
			u == CSS_UNIT_PCT)
		return true;
	if (css_computed_max_height(s, &v, &u) == CSS_MAX_HEIGHT_SET &&
			u == CSS_UNIT_PCT)
		return true;
	return false;
}


/**
 * Whether a laid out item must be laid out again when its container sets its height (a
 * stretch, a column's flexing): FX_DEP_ALWAYS when children have percentage heights
 * (they did not resolve against an auto height), FX_DEP_CHANGE when its layout depends
 * on its height otherwise (a flex / grid container, a table, a form control), else
 * FX_DEP_NONE: its height is set, its content stays.
 */
enum { FX_DEP_NONE, FX_DEP_CHANGE, FX_DEP_ALWAYS };

static int fx_depends_on_height(const struct box *b)
{
	const struct box *c, *d;

	if (b->object != NULL)
		return FX_DEP_NONE;
	for (c = b->children; c != NULL; c = c->next) {
		if (c->type == BOX_INLINE_CONTAINER) {
			for (d = c->children; d != NULL; d = d->next) {
				if ((d->type == BOX_INLINE_BLOCK ||
				     d->type == BOX_INLINE_FLEX) &&
				    fx_style_pct_height(d->style))
					return FX_DEP_ALWAYS;
			}
		} else if (fx_style_pct_height(c->style)) {
			return FX_DEP_ALWAYS;
		}
	}
	if (b->type != BOX_BLOCK || b->gadget != NULL)
		return FX_DEP_CHANGE;
	return FX_DEP_NONE;
}

/** Whether to lay an item out again at a new height (`changed`: it differs) */
static inline bool fx_relayout(const struct box *b, bool changed)
{
	int d = fx_depends_on_height(b);
	return d == FX_DEP_ALWAYS || (changed && d == FX_DEP_CHANGE);
}


/** An item's width / height ratio: its object's natural one, or aspect-ratio's */
static float fx_ratio(const struct box *b)
{
	css_fixed rw = 0, rh = 0;
	uint8_t t = CSS_ASPECT_RATIO_AUTO;

	if (b->style != NULL)
		t = css_computed_aspect_ratio(b->style, &rw, &rh);

	if (b->object != NULL && !(b->flags & REPLACE_DIM) &&
	    content_get_type(b->object) != CONTENT_HTML &&
	    t != CSS_ASPECT_RATIO_SET) {
		int iw = content_get_width(b->object);
		int ih = content_get_height(b->object);
		if (iw > 0 && ih > 0)
			return (float) iw / (float) ih;
	}
	if ((t == CSS_ASPECT_RATIO_SET || t == CSS_ASPECT_RATIO_AUTO_SET) &&
	    rw > 0 && rh > 0)
		return FIXTOFLT(rw) / FIXTOFLT(rh);
	return 0;
}


/** Whether an item's minimum main size is automatic (4.5) */
static bool fx_min_main_auto(const struct fx *fx, const struct box *b)
{
	css_fixed v = 0;
	css_unit u = CSS_UNIT_PX;

	if (b->style == NULL)
		return true;
	if (css_computed_overflow_x(b->style) != CSS_OVERFLOW_VISIBLE ||
	    css_computed_overflow_y(b->style) != CSS_OVERFLOW_VISIBLE)
		return false;
	if (fx->row)
		return css_computed_min_width(b->style, &v, &u) ==
				CSS_MIN_WIDTH_AUTO;
	return css_computed_min_height(b->style, &v, &u) == CSS_MIN_HEIGHT_AUTO;
}


/**
 * An item's max-content / min-content width (content box), in a row: its object's
 * (from its height and ratio when that is definite), else its content's.
 */
static float fx_content_width(const struct fx_item *it, bool min)
{
	const struct box *b = it->box;
	float w;

	if (b->object != NULL && it->ratio > 0) {
		if (it->spec_cross != AUTO)
			return it->spec_cross * it->ratio;
		if (b->object != NULL && !(b->flags & REPLACE_DIM))
			/* (its min / max height through its ratio: an
			 * image with max-height: 60px is 60 px wide) */
			return fx_clamp(content_get_width(b->object),
					it->min_cross * it->ratio,
					it->max_cross * it->ratio);
	}
	w = (min ? b->min_width : b->max_width) - (it->mbp_main - it->bp_main) -
			it->bp_main;
	return w > 0 ? w : 0;
}


/**
 * Step 1: an item's dimensions from its style.
 */
static void fx_item_init(struct fx *fx, struct fx_item *it, struct box *b)
{
	int w, h, maxw, minw, maxh, minh;
	float mbp_x, mbp_y, bp_x, bp_y;
	css_fixed f = 0;

	it->box = b;
	b->flags &= ~DEF_HEIGHT;
	b->float_container = NULL;
	layout_find_dimensions(fx->uctx, fx->flex->width, -1, b, b->style,
			&w, &h, &maxw, &minw, &maxh, &minh,
			b->margin, b->padding, b->border);

	bp_x = b->padding[LEFT] + b->padding[RIGHT] +
			b->border[LEFT].width + b->border[RIGHT].width;
	bp_y = b->padding[TOP] + b->padding[BOTTOM] +
			b->border[TOP].width + b->border[BOTTOM].width;
	mbp_x = bp_x + fx_margin(b, LEFT) + fx_margin(b, RIGHT);
	mbp_y = bp_y + fx_margin(b, TOP) + fx_margin(b, BOTTOM);

	it->ratio = fx_ratio(b);
	it->grow = 0;
	it->shrink = 1;
	if (b->style != NULL) {
		f = 0;
		css_computed_flex_grow(b->style, &f);
		it->grow = FIXTOFLT(f);
		f = INTTOFIX(1);
		css_computed_flex_shrink(b->style, &f);
		it->shrink = FIXTOFLT(f);
	}

	if (fx->row) {
		it->spec_main = w;
		it->spec_cross = h;
		it->min_main = minw;
		it->max_main = maxw >= 0 ? maxw : FX_INF;
		it->min_cross = minh;
		it->max_cross = maxh >= 0 ? maxh : FX_INF;
		it->mbp_main = mbp_x;
		it->mbp_cross = mbp_y;
		it->bp_main = bp_x;
		it->bp_cross = bp_y;
	} else {
		it->spec_main = h;
		it->spec_cross = w;
		it->min_main = minh;
		it->max_main = maxh >= 0 ? maxh : FX_INF;
		it->min_cross = minw;
		it->max_cross = maxw >= 0 ? maxw : FX_INF;
		it->mbp_main = mbp_y;
		it->mbp_cross = mbp_x;
		it->bp_main = bp_y;
		it->bp_cross = bp_x;
	}
	it->auto_ms = b->margin[fx->ms] == AUTO;
	it->auto_me = b->margin[fx->me] == AUTO;
	it->auto_cs = b->margin[fx->cs] == AUTO;
	it->auto_ce = b->margin[fx->ce] == AUTO;

	it->align = lh__box_align_self(fx->flex, b);
	it->stretch = it->align == CSS_ALIGN_SELF_STRETCH &&
			it->spec_cross == AUTO && !it->auto_cs && !it->auto_ce;
	it->stretched = false;
	it->measured = AUTO;
	it->frozen = it->min_v = it->max_v = false;
	it->am_start = it->am_end = it->ac_start = 0;
}


/**
 * The cross size an item stretched in a single line of a definite cross size takes,
 * known before the main sizes (so a ratio transfers it).
 */
static bool fx_prestretch(const struct fx *fx, const struct fx_item *it, float *cross)
{
	if (!it->stretch || !fx->single || !fx->cross_definite)
		return false;
	*cross = fx_clamp(fx->cross_size - it->mbp_cross, it->min_cross,
			it->max_cross);
	if (*cross < 0)
		*cross = 0;
	return true;
}


/**
 * Column: an item's width, then its height laid out at it (its content's height).
 */
static bool fx_measure_column_item(struct fx *fx, struct fx_item *it)
{
	struct box *b = it->box;
	float cross;

	if (it->spec_cross != AUTO) {
		cross = it->spec_cross;
	} else if (fx_prestretch(fx, it, &cross)) {
		it->stretched = true;
	} else if (b->object != NULL && it->ratio > 0 && it->spec_main != AUTO) {
		cross = it->spec_main * it->ratio;
	} else if (b->object != NULL && !(b->flags & REPLACE_DIM) &&
			content_get_type(b->object) != CONTENT_HTML) {
		cross = content_get_width(b->object);
		if (it->ratio > 0)	/* (its min / max height, through its ratio) */
			cross = fx_clamp(cross, it->min_main * it->ratio,
					it->max_main * it->ratio);
	} else {
		/* fit-content: min(max-content, max(min-content, available)) */
		float avail = fx->cross_size - it->mbp_cross;
		float mn = b->min_width - it->mbp_cross;
		float mx = b->max_width - it->mbp_cross;
		cross = avail;
		if (cross < mn)
			cross = mn;
		if (cross > mx)
			cross = mx;
	}
	cross = fx_clamp(cross, it->min_cross, it->max_cross);
	if (cross < 0)
		cross = 0;
	it->cross = cross;

	b->width = fx_round(cross);
	b->height = AUTO;
	if (!fx_layout_item(fx, it, b->width + fx_round(it->mbp_cross)))
		return false;
	it->measured = b->height;
	return true;
}


/**
 * Onyx: an item's subtree laid out at the inputs of its last layout, when the memo gave
 * that layout's size only (its current width, height and DEF_HEIGHT kept).
 */
static bool fx_ensure_item(struct fx *fx, struct fx_item *it)
{
	struct box *b = it->box;
	int w = b->width, h = b->height;
	unsigned int flags = b->flags;
	bool ok;

	if (!it->laid || b->type == BOX_TABLE ||
	    layout_memo_state_is(b, it->lw, it->lh, it->ldef, it->lavail))
		return true;
	b->width = it->lw;
	b->height = it->lh;
	if (it->ldef)
		b->flags |= DEF_HEIGHT;
	else
		b->flags &= ~DEF_HEIGHT;
	ok = layout_memo_layout(b, it->lavail, false, fx_layout_item_now, fx);
	b->width = w;
	b->height = h;
	b->flags = (b->flags & ~DEF_HEIGHT) | (flags & DEF_HEIGHT);
	return ok;
}


/**
 * Step 2: an item's flex base size, automatic minimum size and hypothetical main size.
 */
static bool fx_item_sizes(struct fx *fx, struct fx_item *it)
{
	struct box *b = it->box;
	css_fixed blen = 0;
	css_unit bunit = CSS_UNIT_PX;
	uint8_t btype = CSS_FLEX_BASIS_AUTO;
	float transferred = -1;
	float pre = 0;
	bool content_basis = false;

	if (fx->row) {
		/* a ratio: the main size from a definite (or stretched) cross size */
		if (it->ratio > 0) {
			if (it->spec_cross != AUTO) {
				transferred = it->spec_cross * it->ratio;
			} else if (fx_prestretch(fx, it, &pre)) {
				transferred = pre * it->ratio;
			}
		}
	} else {
		if (!fx_measure_column_item(fx, it))
			return false;
		if (it->ratio > 0 && (it->spec_cross != AUTO || it->stretched))
			transferred = it->cross / it->ratio;
	}

	if (b->style != NULL)
		btype = css_computed_flex_basis(b->style, &blen, &bunit);

	switch (btype) {
	case CSS_FLEX_BASIS_SET:
		if (bunit == CSS_UNIT_PCT) {
			if (fx->main_definite)
				it->base = FIXTOFLT(blen) * fx->main_size / 100;
			else
				content_basis = true;
		} else {
			it->base = FIXTOFLT(css_unit_len2device_px(b->style,
					fx->uctx, blen, bunit));
			if (css_computed_box_sizing(b->style) ==
					CSS_BOX_SIZING_BORDER_BOX)
				it->base -= it->bp_main;
		}
		break;
	case CSS_FLEX_BASIS_CONTENT:
		content_basis = true;
		break;
	default:
		if (it->spec_main != AUTO)
			it->base = it->spec_main;
		else
			content_basis = true;
		break;
	}
	if (content_basis) {
		if (transferred >= 0)
			it->base = transferred;
		else if (fx->row)
			it->base = fx_content_width(it, false);
		else
			it->base = it->measured;
	}
	if (it->base < 0)
		it->base = 0;

	/* the automatic minimum size (4.5) */
	if (fx_min_main_auto(fx, b)) {
		float content = fx->row ? fx_content_width(it, true) :
				(float) it->measured;
		float auto_min;

		if (b->object != NULL && transferred >= 0 && content > transferred)
			content = transferred;
		if (content > it->max_main)
			content = it->max_main;
		auto_min = content;
		if (it->spec_main != AUTO && it->spec_main < auto_min)
			auto_min = it->spec_main;
		else if (it->spec_main == AUTO && transferred >= 0 &&
				transferred < auto_min)
			auto_min = transferred;
		if (auto_min > it->min_main)
			it->min_main = auto_min;
	}

	it->hypo = fx_clamp(it->base, it->min_main, it->max_main);
	return true;
}


/**
 * Step 4: collect the items into lines.
 */
static bool fx_collect_lines(struct fx *fx)
{
	size_t i = 0;

	fx->line = calloc(fx->n > 0 ? fx->n : 1, sizeof(*fx->line));
	if (fx->line == NULL)
		return false;
	fx->nlines = 0;

	while (i < fx->n) {
		struct fx_line *l = &fx->line[fx->nlines++];
		float used = 0;

		l->first = i;
		l->count = 0;
		while (i < fx->n) {
			struct fx_item *it = &fx->item[i];
			float outer = it->hypo + it->mbp_main;
			float gap = l->count > 0 ? fx->main_gap : 0;

			if (!fx->single && l->count > 0 &&
			    used + gap + outer > fx->main_size + 0.01f)
				break;
			used += gap + outer;
			l->count++;
			i++;
		}
	}
	return true;
}


/**
 * Step 4: resolve the flexible lengths of a line (9.7).
 */
static void fx_resolve_line(struct fx *fx, struct fx_line *l)
{
	size_t i, end = l->first + l->count;
	float gaps = l->count > 1 ? (l->count - 1) * fx->main_gap : 0;
	float avail = fx->main_size - gaps;
	float sum_hypo = 0, initial_free;
	bool grow;
	int guard;

	for (i = l->first; i < end; i++)
		sum_hypo += fx->item[i].hypo + fx->item[i].mbp_main;
	grow = sum_hypo < avail;

	/* inflexible items */
	initial_free = avail;
	for (i = l->first; i < end; i++) {
		struct fx_item *it = &fx->item[i];
		float factor = grow ? it->grow : it->shrink;

		it->frozen = false;
		it->target = it->hypo;
		if (factor == 0 || (grow && it->base > it->hypo) ||
		    (!grow && it->base < it->hypo))
			it->frozen = true;
		initial_free -= (it->frozen ? it->target : it->base) +
				it->mbp_main;
	}

	for (guard = 0; guard < 64; guard++) {
		float remaining = avail, sum_factor = 0, total_v = 0;
		bool any = false;

		for (i = l->first; i < end; i++) {
			struct fx_item *it = &fx->item[i];
			remaining -= (it->frozen ? it->target : it->base) +
					it->mbp_main;
			if (!it->frozen) {
				any = true;
				sum_factor += grow ? it->grow : it->shrink;
			}
		}
		if (!any)
			break;
		if (sum_factor < 1) {
			float f = initial_free * sum_factor;
			if (fabsf(f) < fabsf(remaining))
				remaining = f;
		}

		if (grow) {
			for (i = l->first; i < end; i++) {
				struct fx_item *it = &fx->item[i];
				if (it->frozen)
					continue;
				it->target = it->base + (sum_factor > 0 ?
						remaining * it->grow / sum_factor : 0);
			}
		} else {
			float sum_scaled = 0;
			for (i = l->first; i < end; i++) {
				struct fx_item *it = &fx->item[i];
				if (!it->frozen)
					sum_scaled += it->shrink * it->base;
			}
			for (i = l->first; i < end; i++) {
				struct fx_item *it = &fx->item[i];
				if (it->frozen)
					continue;
				it->target = it->base + (sum_scaled > 0 ?
						remaining * it->shrink * it->base /
						sum_scaled : 0);
			}
		}

		/* min / max violations */
		for (i = l->first; i < end; i++) {
			struct fx_item *it = &fx->item[i];
			float c;
			if (it->frozen)
				continue;
			c = fx_clamp(it->target, it->min_main, it->max_main);
			if (c < 0)
				c = 0;
			it->min_v = c > it->target;
			it->max_v = c < it->target;
			total_v += c - it->target;
			it->target = c;
		}
		for (i = l->first; i < end; i++) {
			struct fx_item *it = &fx->item[i];
			if (it->frozen)
				continue;
			if (total_v == 0 || (total_v > 0 && it->min_v) ||
			    (total_v < 0 && it->max_v))
				it->frozen = true;
		}
	}
}


/** The first baseline of a laid out item, from its margin box's top */
static float fx_item_baseline(struct fx *fx, const struct box *b)
{
	int bl;

	if (layout_onyx_first_baseline(b, fx->uctx, &bl))
		return fx_margin(b, TOP) + b->border[TOP].width + bl;
	/* none: synthesised from the border box's bottom edge */
	return fx_margin(b, TOP) + b->border[TOP].width + b->padding[TOP] +
			b->height + b->padding[BOTTOM] + b->border[BOTTOM].width;
}


/**
 * Step 5: lay each item out at its main size (a row's), its cross size.
 */
static bool fx_item_cross(struct fx *fx, struct fx_item *it)
{
	struct box *b = it->box;

	if (fx->row) {
		float pre;

		b->width = fx_round(it->target);
		if (it->spec_cross != AUTO) {
			b->height = it->spec_cross;
		} else if (fx_prestretch(fx, it, &pre)) {
			b->height = fx_round(pre);
			b->flags |= DEF_HEIGHT;
			it->stretched = true;
		} else if (it->ratio > 0 && b->object == NULL) {
			/* aspect-ratio: the height from the width */
			b->height = fx_round(fx_clamp(it->target / it->ratio,
					it->min_cross, it->max_cross));
			it->stretch = false;
		} else {
			b->height = AUTO;
		}
		if (!fx_layout_item(fx, it, b->width + fx_round(it->mbp_main)))
			return false;
		it->cross = b->height;
		if (it->align == CSS_ALIGN_SELF_BASELINE) {
			/* (read from its laid out subtree) */
			int ch = b->height;
			if (!fx_ensure_item(fx, it))
				return false;
			b->height = ch;
			it->baseline = fx_item_baseline(fx, b);
		}
	} else {
		/* the width was set when measured; the height is the main size */
		int h = fx_round(it->target);

		b->height = h;
		if (fx->main_definite)
			b->flags |= DEF_HEIGHT;
		if (fx_relayout(b, h != it->measured)) {
			if (!fx_layout_item(fx, it, b->width +
					fx_round(it->mbp_cross)))
				return false;
			b->height = h;
		}
		it->cross = b->width;
	}
	return true;
}


/**
 * Step 5: the lines' cross sizes, the container's cross size, align-content.
 */
static void fx_lines_cross(struct fx *fx)
{
	size_t li, i;
	float sum = 0, free, lead = 0, between = 0, extra = 0;
	float total;
	uint8_t ac = CSS_ALIGN_CONTENT_STRETCH;

	for (li = 0; li < fx->nlines; li++) {
		struct fx_line *l = &fx->line[li];
		float mx = 0, asc = 0, desc = 0;

		for (i = l->first; i < l->first + l->count; i++) {
			struct fx_item *it = &fx->item[i];
			float outer = it->cross + it->mbp_cross;

			if (fx->row && it->align == CSS_ALIGN_SELF_BASELINE &&
			    !it->auto_cs && !it->auto_ce) {
				if (it->baseline > asc)
					asc = it->baseline;
				if (outer - it->baseline > desc)
					desc = outer - it->baseline;
			} else if (outer > mx) {
				mx = outer;
			}
		}
		l->ascent = asc;
		l->cross = asc + desc > mx ? asc + desc : mx;
		if (fx->single) {
			if (fx->cross_definite) {
				l->cross = fx->cross_size;
			} else if (fx->row) {
				if (fx->max_h >= 0 && l->cross > fx->max_h)
					l->cross = fx->max_h;
				if (l->cross < fx->min_h)
					l->cross = fx->min_h;
			}
		}
		sum += l->cross;
	}
	if (fx->nlines > 1)
		sum += (fx->nlines - 1) * fx->cross_gap;

	/* the container's cross size */
	if (fx->row && !fx->cross_definite) {
		total = sum;
		if (fx->max_h >= 0 && total > fx->max_h)
			total = fx->max_h;
		if (total < fx->min_h)
			total = fx->min_h;
		fx->cross_size = total;
	}
	total = fx->cross_size;

	/* align-content (a multi-line container's) */
	free = total - sum;
	if (!fx->single && fx->flex->style != NULL)
		ac = css_computed_align_content(fx->flex->style);
	if (!fx->single) {
		size_t n = fx->nlines;
		switch (ac) {
		case CSS_ALIGN_CONTENT_FLEX_START:
			break;
		case CSS_ALIGN_CONTENT_FLEX_END:
			lead = free;
			break;
		case CSS_ALIGN_CONTENT_CENTER:
			lead = free / 2;
			break;
		case CSS_ALIGN_CONTENT_SPACE_BETWEEN:
			if (free > 0 && n > 1)
				between = free / (n - 1);
			break;
		case CSS_ALIGN_CONTENT_SPACE_AROUND:
			if (free > 0) {
				between = free / n;
				lead = between / 2;
			} else {
				lead = free / 2;
			}
			break;
		case CSS_ALIGN_CONTENT_SPACE_EVENLY:
			if (free > 0) {
				between = free / (n + 1);
				lead = between;
			} else {
				lead = free / 2;
			}
			break;
		default:	/* stretch, normal */
			if (free > 0)
				extra = free / n;
			break;
		}
	}

	{
		float pos = lead;
		for (li = 0; li < fx->nlines; li++) {
			struct fx_line *l = &fx->line[li];
			l->cross += extra;
			l->pos = pos;
			pos += l->cross + fx->cross_gap + between;
		}
	}
}


/**
 * Step 5: stretch the items with an auto cross size to their line's.
 */
static bool fx_stretch(struct fx *fx)
{
	size_t li, i;

	for (li = 0; li < fx->nlines; li++) {
		struct fx_line *l = &fx->line[li];

		for (i = l->first; i < l->first + l->count; i++) {
			struct fx_item *it = &fx->item[i];
			struct box *b = it->box;
			float c;
			int ci;

			if (!it->stretch)
				continue;
			c = fx_clamp(l->cross - it->mbp_cross, it->min_cross,
					it->max_cross);
			if (c < 0)
				c = 0;
			ci = fx_round(c);
			if (fx->row) {
				bool changed = ci != b->height;
				b->flags |= DEF_HEIGHT;
				b->height = ci;
				if (fx_relayout(b, changed)) {
					if (!fx_layout_item(fx, it, b->width +
							fx_round(it->mbp_main)))
						return false;
					b->height = ci;
				}
			} else if (ci != b->width) {
				int h = b->height;
				b->width = ci;
				if (!fx_layout_item(fx, it, ci +
						fx_round(it->mbp_cross)))
					return false;
				b->height = h;
			}
			it->cross = c;
		}
	}
	return true;
}


/**
 * Step 6: main-axis alignment of a line.
 */
static void fx_align_main(struct fx *fx, struct fx_line *l)
{
	size_t i, end = l->first + l->count;
	float used = 0, free, lead = 0, between = 0, am = 0;
	int n_am = 0;
	float pos;

	for (i = l->first; i < end; i++) {
		struct fx_item *it = &fx->item[i];
		used += it->target + it->mbp_main;
		n_am += it->auto_ms + it->auto_me;
	}
	if (l->count > 1)
		used += (l->count - 1) * fx->main_gap;
	free = fx->main_size - used;

	if (n_am > 0) {
		if (free > 0)
			am = free / n_am;
	} else if (fx->flex->style != NULL) {
		switch (css_computed_justify_content(fx->flex->style)) {
		case CSS_JUSTIFY_CONTENT_FLEX_END:
			lead = free;
			break;
		case CSS_JUSTIFY_CONTENT_CENTER:
			lead = free / 2;
			break;
		case CSS_JUSTIFY_CONTENT_SPACE_BETWEEN:
			if (free > 0 && l->count > 1)
				between = free / (l->count - 1);
			break;
		case CSS_JUSTIFY_CONTENT_SPACE_AROUND:
			if (free > 0) {
				between = free / l->count;
				lead = between / 2;
			} else {
				lead = free / 2;
			}
			break;
		case CSS_JUSTIFY_CONTENT_SPACE_EVENLY:
			if (free > 0) {
				between = free / (l->count + 1);
				lead = between;
			} else {
				lead = free / 2;
			}
			break;
		default:
			break;
		}
	}

	pos = lead;
	for (i = l->first; i < end; i++) {
		struct fx_item *it = &fx->item[i];

		it->am_start = it->auto_ms ? am : 0;
		it->am_end = it->auto_me ? am : 0;
		it->main_pos = pos;
		pos += it->am_start + it->target + it->mbp_main + it->am_end +
				fx->main_gap + between;
	}
}


/**
 * Step 6: cross-axis alignment of an item in its line.
 */
static void fx_align_cross(struct fx *fx, struct fx_line *l, struct fx_item *it)
{
	float free = l->cross - (it->cross + it->mbp_cross);

	it->ac_start = 0;
	if (it->auto_cs || it->auto_ce) {
		if (free > 0) {
			if (it->auto_cs && it->auto_ce)
				it->ac_start = free / 2;
			else if (it->auto_cs)
				it->ac_start = free;
		}
		it->cross_off = it->ac_start;
		return;
	}
	switch (it->align) {
	case CSS_ALIGN_SELF_FLEX_END:
		it->cross_off = free;
		break;
	case CSS_ALIGN_SELF_CENTER:
		it->cross_off = free / 2;
		break;
	case CSS_ALIGN_SELF_BASELINE:
		it->cross_off = fx->row ? l->ascent - it->baseline : 0;
		break;
	default:
		it->cross_off = 0;
		break;
	}
}


/**
 * Step 6: the boxes' positions (and their auto margins' used sizes).
 */
static void fx_place(struct fx *fx)
{
	struct box *flex = fx->flex;
	float cross_total = fx->row ? fx->cross_size : flex->width;
	size_t li, i;

	for (li = 0; li < fx->nlines; li++) {
		struct fx_line *l = &fx->line[li];

		fx_align_main(fx, l);
		for (i = l->first; i < l->first + l->count; i++) {
			struct fx_item *it = &fx->item[i];
			struct box *b = it->box;
			float m_ms, bsize_main, bsize_cross;
			float main_bb, cross_bb;

			fx_align_cross(fx, l, it);

			m_ms = fx_margin(b, fx->ms) + it->am_start;
			bsize_main = (fx->row ? b->width : b->height) +
					it->bp_main;
			bsize_cross = (fx->row ? b->height : b->width) +
					it->bp_cross;

			/* the border box's start, logical then physical */
			main_bb = it->main_pos + m_ms;
			if (fx->reverse)
				main_bb = fx->main_size - main_bb - bsize_main;
			cross_bb = l->pos + it->cross_off + fx_margin(b, fx->cs);
			if (fx->wrap_reverse)
				cross_bb = cross_total - cross_bb - bsize_cross;

			if (fx->row) {
				b->x = flex->padding[LEFT] + fx_round(main_bb) +
						b->border[LEFT].width;
				b->y = flex->padding[TOP] + fx_round(cross_bb) +
						b->border[TOP].width;
			} else {
				b->y = flex->padding[TOP] + fx_round(main_bb) +
						b->border[TOP].width;
				b->x = flex->padding[LEFT] + fx_round(cross_bb) +
						b->border[LEFT].width;
			}

			/* the auto margins' used sizes */
			if (it->auto_ms)
				b->margin[fx->ms] = fx_round(it->am_start);
			if (it->auto_me)
				b->margin[fx->me] = fx_round(it->am_end);
			if (it->auto_cs)
				b->margin[fx->cs] = fx_round(it->ac_start);
			if (it->auto_ce)
				b->margin[fx->ce] = 0;
		}
	}
}


/**
 * The absolutely positioned children's static position: the container's content
 * box's start (their layout aligns them: layout_absolute).
 */
static void fx_place_absolute(struct fx *fx)
{
	struct box *c;

	for (c = fx->flex->children; c != NULL; c = c->next) {
		if (c->style != NULL && lh__box_is_absolute(c)) {
			c->x = fx->flex->padding[LEFT];
			c->y = fx->flex->padding[TOP];
		}
	}
}


/** Sort the items by `order` (stable: an insertion sort, the orders mostly equal) */
static void fx_sort(struct fx *fx)
{
	size_t i, j;

	for (i = 1; i < fx->n; i++) {
		struct fx_item t = fx->item[i];
		if (fx->item[i - 1].order <= t.order)
			continue;
		for (j = i; j > 0 && fx->item[j - 1].order > t.order; j--)
			fx->item[j] = fx->item[j - 1];
		fx->item[j] = t;
	}
}


/* exported function documented in html/layout_internal.h */
bool layout_flex(struct box *flex, int available_width, html_content *content)
{
	struct fx fx;
	struct box *c;
	bool ok = false;
	int min_h = 0, max_h = -1;
	size_t i;

	/* Onyx: a grid container is a flex-like box; its layout is layout_grid.c's */
	if (lh__box_is_grid(flex)) {
		return layout_grid(flex, available_width, content);
	}

	memset(&fx, 0, sizeof(fx));
	fx.content = content;
	fx.uctx = &content->unit_len_ctx;
	fx.flex = flex;
	fx.row = lh__flex_main_is_horizontal(flex);
	fx.reverse = lh__flex_direction_reversed(flex);
	fx.wrap_reverse = css_computed_flex_wrap(flex->style) ==
			CSS_FLEX_WRAP_WRAP_REVERSE;
	fx.single = css_computed_flex_wrap(flex->style) == CSS_FLEX_WRAP_NOWRAP;
	fx.ms = fx.row ? LEFT : TOP;
	fx.me = fx.row ? RIGHT : BOTTOM;
	fx.cs = fx.row ? TOP : LEFT;
	fx.ce = fx.row ? BOTTOM : RIGHT;
	if (fx.reverse) {
		enum box_side t = fx.ms;
		fx.ms = fx.me;
		fx.me = t;
	}
	if (fx.wrap_reverse) {
		enum box_side t = fx.cs;
		fx.cs = fx.ce;
		fx.ce = t;
	}

	layout_find_dimensions(fx.uctx, available_width, -1, flex, flex->style,
			NULL, NULL, NULL, NULL, &max_h, &min_h, NULL, NULL, NULL);
	fx.min_h = min_h;
	fx.max_h = max_h;
	if (flex->height != AUTO) {
		if (max_h >= 0 && flex->height > max_h)
			flex->height = max_h;
		if (flex->height < min_h)
			flex->height = min_h;
	}

	if (fx.row) {
		fx.main_size = flex->width;
		fx.main_definite = true;
		fx.cross_definite = flex->height != AUTO;
		fx.cross_size = fx.cross_definite ? flex->height : 0;
	} else {
		fx.main_definite = flex->height != AUTO;
		fx.main_size = fx.main_definite ? flex->height : 0;
		fx.cross_size = flex->width;
		fx.cross_definite = true;
	}
	fx.main_gap = lh__flex_gap(fx.uctx, flex, true,
			fx.row ? flex->width : (fx.main_definite ? flex->height : -1));
	fx.cross_gap = lh__flex_gap(fx.uctx, flex, false,
			fx.row ? (fx.cross_definite ? flex->height : -1) : flex->width);

	/* 1. the items */
	for (c = flex->children; c != NULL; c = c->next) {
		if (c->style == NULL || !lh__box_is_absolute(c))
			fx.n++;
	}
	fx.item = calloc(fx.n > 0 ? fx.n : 1, sizeof(*fx.item));
	if (fx.item == NULL)
		return false;
	i = 0;
	for (c = flex->children; c != NULL; c = c->next) {
		int32_t order = 0;
		if (c->style != NULL && lh__box_is_absolute(c))
			continue;
		if (c->style != NULL)
			css_computed_order(c->style, &order);
		fx.item[i].order = order;
		fx.item[i].box = c;
		i++;
	}
	fx_sort(&fx);

	/* 2. the base and hypothetical sizes */
	for (i = 0; i < fx.n; i++) {
		fx_item_init(&fx, &fx.item[i], fx.item[i].box);
		if (!fx_item_sizes(&fx, &fx.item[i]))
			goto done;
	}

	/* 3. a column's auto height */
	if (!fx.row && !fx.main_definite) {
		float sum = 0;
		for (i = 0; i < fx.n; i++)
			sum += fx.item[i].hypo + fx.item[i].mbp_main;
		if (fx.n > 1)
			sum += (fx.n - 1) * fx.main_gap;
		if (max_h >= 0 && sum > max_h)
			sum = max_h;
		if (sum < min_h)
			sum = min_h;
		fx.main_size = sum;
	}

	/* 4. lines, flexible lengths */
	if (!fx_collect_lines(&fx))
		goto done;
	for (i = 0; i < fx.nlines; i++)
		fx_resolve_line(&fx, &fx.line[i]);

	/* 5. cross sizes */
	for (i = 0; i < fx.n; i++) {
		if (!fx_item_cross(&fx, &fx.item[i]))
			goto done;
	}
	fx_lines_cross(&fx);
	if (!fx_stretch(&fx))
		goto done;

	/* Onyx: each item's subtree as its last layout made it (the memo) */
	for (i = 0; i < fx.n; i++) {
		if (!fx_ensure_item(&fx, &fx.item[i]))
			goto done;
	}

	/* 6. positions */
	fx_place(&fx);
	fx_place_absolute(&fx);

	if (fx.row) {
		flex->height = fx_round(fx.cross_size);
	} else {
		flex->height = fx_round(fx.main_size);
	}
	ok = true;

done:
	free(fx.item);
	free(fx.line);
	return ok;
}


/**
 * An item's min-content and max-content contributions to its container's width
 * (outer), CSS Flexbox 9.9.1: the larger of its content's and its specified width,
 * in a row clamped by its flex base size -- as a maximum when it does not grow, a
 * minimum when it does not shrink --, then by its min-width / max-width.
 */
static void fx_width_contribution(const css_unit_ctx *uctx, const struct box *b,
		bool row, int *cmin, int *cmax)
{
	int mn = b->min_width, mx = b->max_width;
	int fixed = 0, bp = 0, w, v;
	float frac = 0;
	css_fixed len = 0;
	css_unit unit = CSS_UNIT_PX;
	bool border_box;

	if (b->style == NULL) {
		*cmin = mn;
		*cmax = mx;
		return;
	}
	border_box = css_computed_box_sizing(b->style) == CSS_BOX_SIZING_BORDER_BOX;
	calculate_mbp_width(uctx, b->style, LEFT, false, true, true, &bp, &frac);
	calculate_mbp_width(uctx, b->style, RIGHT, false, true, true, &bp, &frac);
	calculate_mbp_width(uctx, b->style, LEFT, true, false, false, &fixed, &frac);
	calculate_mbp_width(uctx, b->style, RIGHT, true, false, false, &fixed, &frac);
	fixed += bp;

	if (css_computed_width_px(b->style, uctx, -1, &w) == CSS_WIDTH_SET) {
		w = (border_box ? max(w - bp, 0) : w) + fixed;
		if (row) {
			mn = max(mn, w);
			mx = max(mx, w);
		} else {
			mn = mx = w;
		}
	}

	if (row) {
		uint8_t bt = css_computed_flex_basis(b->style, &len, &unit);
		int base = -1;

		if (bt == CSS_FLEX_BASIS_SET && unit != CSS_UNIT_PCT) {
			base = FIXTOINT(css_unit_len2device_px(b->style, uctx,
					len, unit));
			base = (border_box ? max(base - bp, 0) : base) + fixed;
		} else if (bt == CSS_FLEX_BASIS_AUTO &&
				css_computed_width_px(b->style, uctx, -1, &w) ==
						CSS_WIDTH_SET) {
			base = (border_box ? max(w - bp, 0) : w) + fixed;
		}
		if (base >= 0) {
			css_fixed grow = 0, shrink = INTTOFIX(1);
			css_computed_flex_grow(b->style, &grow);
			css_computed_flex_shrink(b->style, &shrink);
			if (grow == 0) {
				mn = min(mn, base);
				mx = min(mx, base);
			}
			if (shrink == 0) {
				mn = max(mn, base);
				mx = max(mx, base);
			}
		}
	}

	if (css_computed_min_width(b->style, &len, &unit) == CSS_MIN_WIDTH_SET &&
	    unit != CSS_UNIT_PCT) {
		v = FIXTOINT(css_unit_len2device_px(b->style, uctx, len, unit));
		v = (border_box ? max(v - bp, 0) : v) + fixed;
		mn = max(mn, v);
		mx = max(mx, v);
	}
	if (css_computed_max_width(b->style, &len, &unit) == CSS_MAX_WIDTH_SET &&
	    unit != CSS_UNIT_PCT) {
		v = FIXTOINT(css_unit_len2device_px(b->style, uctx, len, unit));
		v = (border_box ? max(v - bp, 0) : v) + fixed;
		mn = min(mn, v);
		mx = min(mx, v);
	}
	*cmin = max(mn, 0);
	*cmax = max(mx, *cmin);
}


/* exported function documented in html/layout_internal.h */
void layout_minmax_flex(struct box *flex, const css_unit_ctx *uctx,
		int *min_width, int *max_width)
{
	bool row = lh__flex_main_is_horizontal(flex);
	bool wrap = css_computed_flex_wrap(flex->style) != CSS_FLEX_WRAP_NOWRAP;
	int gap = lh__flex_gap(uctx, flex, true, -1);
	int mn = 0, mx = 0, n = 0;
	struct box *c;

	for (c = flex->children; c != NULL; c = c->next) {
		int cmin, cmax;

		if (c->style != NULL && lh__box_is_absolute(c))
			continue;
		fx_width_contribution(uctx, c, row, &cmin, &cmax);
		if (row) {
			if (wrap)
				mn = max(mn, cmin);
			else
				mn += cmin;
			mx += cmax;
		} else {
			mn = max(mn, cmin);
			mx = max(mx, cmax);
		}
		n++;
	}
	if (row && n > 1 && gap > 0) {
		mx += gap * (n - 1);
		if (!wrap)
			mn += gap * (n - 1);
	}
	if (mx < mn)
		mx = mn;
	*min_width = mn;
	*max_width = mx;
}
