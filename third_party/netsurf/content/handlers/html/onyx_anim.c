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
 * Onyx: CSS transitions, CSS animations, the Web Animations API and the animation frames
 * (requestAnimationFrame) of an HTML content, on one timeline.
 *
 * The style selection (box_get_style) hands every element's new style here
 * (onyx_anim_styled). An element with a transition or an animation gets a record: its base
 * style (the cascade's, what the boxes would show without animations) and its current one
 * (the animated style its boxes show). When the base changes, the animatable properties the
 * transition-property list names whose value changed start transitions (from the current
 * value to the new one); a new animation-name starts a CSS animation (the @keyframes of the
 * page's sheets, css_select_ctx_onyx_keyframes); element.animate() adds a script's
 * animation. Each is an effect: its keyframes (styles made by libcss from the declarations,
 * css_computed_style_onyx_apply), a timing (delay, duration, iterations, direction, fill,
 * easing) and a player (start time, hold time, rate) as the Web Animations model has them.
 *
 * A frame (oa_tick, scheduled only while an effect runs or a script asked for a frame; at
 * ~60 Hz, less when a frame's work is long): each record's animated style is made again --
 * a copy of its base with each animated property blended between its two keyframes
 * (css_computed_style_onyx_blend), interned -- and, when it changed, given to its boxes as
 * a :hover restyle does (onyx_hover_restyle_elements: its rectangles redrawn when only
 * painting changed; else the styles swapped and the page laid out again). Then the events
 * (transitionrun / start / end / cancel, animationstart / iteration / end / cancel, an
 * Animation's finish / cancel) go to the scripts, then the requestAnimationFrame callbacks
 * run. A page without transitions nor animations never creates the timeline.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dom/dom.h>
#include <libcss/libcss.h>
#include <nsutils/time.h>

#include "utils/corestrings.h"
#include "utils/log.h"
#include "utils/nsurl.h"
#include "netsurf/misc.h"
#include "desktop/gui_internal.h"
#include "content/content_protected.h"
#include "css/utils.h"
#include "css/select.h"
#include "javascript/js.h"

#include "html/private.h"
#include "html/box.h"
#include "html/box_inspect.h"
#include "netsurf/types.h"
#include "netsurf/browser_window.h"
#include "netsurf/window.h"
#include "desktop/browser_private.h"
#include "html/box_construct.h"
#include "html/onyx_hover.h"
#include "html/onyx_anim.h"
#include "netsurf/onyx_perf.h"

/* ---- the animatable properties ------------------------------------------------------------ */

struct oa_prop {
	const char *name;
	uint16_t prop;
	bool inherited;		/* (its change styles the subtree again) */
};

static const struct oa_prop oa_props[] = {
	{ "opacity", CSS_PROP_OPACITY, false },
	{ "color", CSS_PROP_COLOR, true },
	{ "background-color", CSS_PROP_BACKGROUND_COLOR, false },
	{ "border-top-color", CSS_PROP_BORDER_TOP_COLOR, false },
	{ "border-right-color", CSS_PROP_BORDER_RIGHT_COLOR, false },
	{ "border-bottom-color", CSS_PROP_BORDER_BOTTOM_COLOR, false },
	{ "border-left-color", CSS_PROP_BORDER_LEFT_COLOR, false },
	{ "outline-color", CSS_PROP_OUTLINE_COLOR, false },
	{ "column-rule-color", CSS_PROP_COLUMN_RULE_COLOR, false },
	{ "width", CSS_PROP_WIDTH, false },
	{ "height", CSS_PROP_HEIGHT, false },
	{ "min-width", CSS_PROP_MIN_WIDTH, false },
	{ "min-height", CSS_PROP_MIN_HEIGHT, false },
	{ "max-width", CSS_PROP_MAX_WIDTH, false },
	{ "max-height", CSS_PROP_MAX_HEIGHT, false },
	{ "top", CSS_PROP_TOP, false },
	{ "right", CSS_PROP_RIGHT, false },
	{ "bottom", CSS_PROP_BOTTOM, false },
	{ "left", CSS_PROP_LEFT, false },
	{ "margin-top", CSS_PROP_MARGIN_TOP, false },
	{ "margin-right", CSS_PROP_MARGIN_RIGHT, false },
	{ "margin-bottom", CSS_PROP_MARGIN_BOTTOM, false },
	{ "margin-left", CSS_PROP_MARGIN_LEFT, false },
	{ "padding-top", CSS_PROP_PADDING_TOP, false },
	{ "padding-right", CSS_PROP_PADDING_RIGHT, false },
	{ "padding-bottom", CSS_PROP_PADDING_BOTTOM, false },
	{ "padding-left", CSS_PROP_PADDING_LEFT, false },
	{ "border-top-width", CSS_PROP_BORDER_TOP_WIDTH, false },
	{ "border-right-width", CSS_PROP_BORDER_RIGHT_WIDTH, false },
	{ "border-bottom-width", CSS_PROP_BORDER_BOTTOM_WIDTH, false },
	{ "border-left-width", CSS_PROP_BORDER_LEFT_WIDTH, false },
	{ "outline-width", CSS_PROP_OUTLINE_WIDTH, false },
	{ "border-top-left-radius", CSS_PROP_BORDER_TOP_LEFT_RADIUS, false },
	{ "border-top-right-radius", CSS_PROP_BORDER_TOP_RIGHT_RADIUS, false },
	{ "border-bottom-right-radius", CSS_PROP_BORDER_BOTTOM_RIGHT_RADIUS, false },
	{ "border-bottom-left-radius", CSS_PROP_BORDER_BOTTOM_LEFT_RADIUS, false },
	{ "letter-spacing", CSS_PROP_LETTER_SPACING, true },
	{ "word-spacing", CSS_PROP_WORD_SPACING, true },
	{ "text-indent", CSS_PROP_TEXT_INDENT, true },
	{ "font-size", CSS_PROP_FONT_SIZE, true },
	{ "line-height", CSS_PROP_LINE_HEIGHT, true },
	{ "row-gap", CSS_PROP_ROW_GAP, false },
	{ "column-gap", CSS_PROP_COLUMN_GAP, false },
	{ "flex-basis", CSS_PROP_FLEX_BASIS, false },
	{ "flex-grow", CSS_PROP_FLEX_GROW, false },
	{ "flex-shrink", CSS_PROP_FLEX_SHRINK, false },
	{ "transform", CSS_PROP_TRANSFORM, false },
	{ "translate", CSS_PROP_TRANSLATE, false },
	{ "scale", CSS_PROP_SCALE, false },
	{ "rotate", CSS_PROP_ROTATE, false },
	{ "box-shadow", CSS_PROP_BOX_SHADOW, false },
	{ "filter", CSS_PROP_FILTER, false },
	{ "backdrop-filter", CSS_PROP_BACKDROP_FILTER, false },
	{ "visibility", CSS_PROP_VISIBILITY, true },
	{ "z-index", CSS_PROP_Z_INDEX, false },
	{ "fill", CSS_PROP_FILL, true },
	{ "stroke", CSS_PROP_STROKE, true },
	{ "stroke-width", CSS_PROP_STROKE_WIDTH, true },
	{ "fill-opacity", CSS_PROP_FILL_OPACITY, true },
	{ "stroke-opacity", CSS_PROP_STROKE_OPACITY, true },
	{ "stop-color", CSS_PROP_STOP_COLOR, false },
	{ "stop-opacity", CSS_PROP_STOP_OPACITY, false },
};
#define OA_NPROPS ((int) (sizeof(oa_props) / sizeof(oa_props[0])))

/* the shorthands transition-property may name: their longhands */
static const struct {
	const char *name;
	const char *longhands;
} oa_shorthands[] = {
	{ "border-color", "border-top-color border-right-color border-bottom-color border-left-color" },
	{ "border-width", "border-top-width border-right-width border-bottom-width border-left-width" },
	{ "border", "border-top-color border-right-color border-bottom-color border-left-color "
		"border-top-width border-right-width border-bottom-width border-left-width" },
	{ "border-top", "border-top-color border-top-width" },
	{ "border-right", "border-right-color border-right-width" },
	{ "border-bottom", "border-bottom-color border-bottom-width" },
	{ "border-left", "border-left-color border-left-width" },
	{ "border-radius", "border-top-left-radius border-top-right-radius "
		"border-bottom-right-radius border-bottom-left-radius" },
	{ "margin", "margin-top margin-right margin-bottom margin-left" },
	{ "padding", "padding-top padding-right padding-bottom padding-left" },
	{ "inset", "top right bottom left" },
	{ "outline", "outline-color outline-width" },
	{ "gap", "row-gap column-gap" },
	{ "grid-gap", "row-gap column-gap" },
	{ "flex", "flex-grow flex-shrink flex-basis" },
	{ "background", "background-color" },
	{ "font", "font-size line-height" },
};

/* the table's index of a libcss property, -1 if it is not animated */
static int8_t oa_index[CSS_N_PROPERTIES];
static bool oa_index_made;

static void oa_make_index(void)
{
	if (oa_index_made)
		return;
	memset(oa_index, -1, sizeof(oa_index));
	for (int k = 0; k < OA_NPROPS; k++)
		oa_index[oa_props[k].prop] = (int8_t) k;
	oa_index_made = true;
}

/* the table's indexes a name stands for (a longhand, a shorthand, "all"): how many */
static int oa_lookup(const char *name, size_t len, int *out, int max)
{
	int n = 0;

	if (len > 0 && name[0] == '-') {
		/* (a vendor's prefix: -webkit-transform) */
		const char *d = memchr(name + 1, '-', len - 1);
		if (d != NULL) {
			len -= (size_t) (d + 1 - name);
			name = d + 1;
		}
	}
	if (len == 3 && strncmp(name, "all", 3) == 0) {
		for (int k = 0; k < OA_NPROPS && n < max; k++)
			out[n++] = k;
		return n;
	}
	for (int k = 0; k < OA_NPROPS; k++)
		if (strlen(oa_props[k].name) == len &&
		    strncmp(oa_props[k].name, name, len) == 0) {
			out[0] = k;
			return 1;
		}
	for (size_t s = 0; s < sizeof(oa_shorthands) / sizeof(oa_shorthands[0]); s++) {
		const char *l = oa_shorthands[s].longhands;
		if (strlen(oa_shorthands[s].name) != len ||
		    strncmp(oa_shorthands[s].name, name, len) != 0)
			continue;
		while (*l != '\0' && n < max) {
			const char *e = strchr(l, ' ');
			size_t ll = e != NULL ? (size_t) (e - l) : strlen(l);
			n += oa_lookup(l, ll, out + n, max - n);
			l += ll;
			while (*l == ' ')
				l++;
		}
		return n;
	}
	return 0;
}

/* ---- the lists' texts (libcss keeps them comma separated) --------------------------------- */

#define OA_LIST_MAX 32

struct oa_list {
	int n;
	const char *item[OA_LIST_MAX];
	size_t len[OA_LIST_MAX];
};

/* the items of a list text (commas inside parentheses kept) */
static void oa_split(lwc_string *s, struct oa_list *l)
{
	const char *p, *start;
	int depth = 0;

	l->n = 0;
	if (s == NULL)
		return;
	p = start = lwc_string_data(s);
	for (;; p++) {
		if (*p == '(') {
			depth++;
		} else if (*p == ')') {
			depth--;
		} else if ((*p == ',' && depth == 0) || *p == '\0') {
			if (l->n < OA_LIST_MAX) {
				l->item[l->n] = start;
				l->len[l->n] = (size_t) (p - start);
				l->n++;
			}
			if (*p == '\0')
				break;
			start = p + 1;
		}
	}
}

/* a list property's text: NULL if it has its initial value */
static lwc_string *oa_text(uint8_t (*get)(const css_computed_style *, lwc_string **),
		const css_computed_style *style)
{
	lwc_string *s = NULL;
	return get(style, &s) == CSS_ONYX_TEXT_SET ? s : NULL;
}

static double oa_seconds(const struct oa_list *l, int i, double dflt)
{
	if (l->n == 0)
		return dflt;
	return strtod(l->item[i % l->n], NULL);
}

static bool oa_item_is(const struct oa_list *l, int i, const char *w)
{
	size_t n = strlen(w);
	if (l->n == 0)
		return false;
	i %= l->n;
	return l->len[i] == n && strncmp(l->item[i], w, n) == 0;
}

/* ---- easing functions (CSS Easing 2) ---------------------------------------------------- */

enum { OE_LINEAR, OE_CUBIC, OE_STEPS, OE_POINTS };
#define OE_POINTS_MAX 24

struct oa_easing {
	uint8_t kind;
	float x1, y1, x2, y2;		/* cubic-bezier */
	int steps;
	uint8_t pos;			/* 0 jump-start, 1 jump-end, 2 jump-none, 3 jump-both */
	int npts;			/* linear(): its points (input, output) */
	float px[OE_POINTS_MAX], py[OE_POINTS_MAX];
};

static void oe_cubic(struct oa_easing *e, float x1, float y1, float x2, float y2)
{
	e->kind = OE_CUBIC;
	e->x1 = x1; e->y1 = y1; e->x2 = x2; e->y2 = y2;
}

/* an <easing-function> text (libcss's canonical form) */
static void oa_parse_easing(const char *t, size_t len, struct oa_easing *e)
{
	char buf[512];

	memset(e, 0, sizeof(*e));
	e->kind = OE_LINEAR;
	if (t == NULL) {
		oe_cubic(e, 0.25f, 0.1f, 0.25f, 1);	/* (ease: the initial value) */
		return;
	}
	if (len >= sizeof(buf))
		len = sizeof(buf) - 1;
	memcpy(buf, t, len);
	buf[len] = '\0';
	if (strcmp(buf, "ease") == 0) {
		oe_cubic(e, 0.25f, 0.1f, 0.25f, 1);
	} else if (strcmp(buf, "ease-in") == 0) {
		oe_cubic(e, 0.42f, 0, 1, 1);
	} else if (strcmp(buf, "ease-out") == 0) {
		oe_cubic(e, 0, 0, 0.58f, 1);
	} else if (strcmp(buf, "ease-in-out") == 0) {
		oe_cubic(e, 0.42f, 0, 0.58f, 1);
	} else if (strcmp(buf, "step-start") == 0) {
		e->kind = OE_STEPS; e->steps = 1; e->pos = 0;
	} else if (strcmp(buf, "step-end") == 0) {
		e->kind = OE_STEPS; e->steps = 1; e->pos = 1;
	} else if (strncmp(buf, "cubic-bezier(", 13) == 0) {
		float v[4] = { 0, 0, 1, 1 };
		if (sscanf(buf + 13, "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]) == 4)
			oe_cubic(e, v[0] < 0 ? 0 : v[0] > 1 ? 1 : v[0], v[1],
					v[2] < 0 ? 0 : v[2] > 1 ? 1 : v[2], v[3]);
	} else if (strncmp(buf, "steps(", 6) == 0) {
		int n = 1;
		char pos[16] = "jump-end";
		sscanf(buf + 6, "%d,%15[a-z-]", &n, pos);
		e->kind = OE_STEPS;
		e->steps = n < 1 ? 1 : n;
		e->pos = strcmp(pos, "jump-start") == 0 ? 0 : strcmp(pos, "jump-none") == 0 ? 2 :
				strcmp(pos, "jump-both") == 0 ? 3 : 1;
		if (e->pos == 2 && e->steps < 2)
			e->steps = 2;
	} else if (strncmp(buf, "linear(", 7) == 0) {
		/* the stops: "v [p%] [p%]", comma separated; missing inputs spread evenly */
		char *p = buf + 7;
		bool known[OE_POINTS_MAX];
		int n = 0;
		while (*p != '\0' && *p != ')' && n < OE_POINTS_MAX) {
			char *q;
			float v = strtof(p, &q);
			int parts = 0;
			if (q == p)
				break;
			p = q;
			e->py[n] = v;
			known[n] = false;
			while (*p == ' ' && n < OE_POINTS_MAX) {
				float pc = strtof(p + 1, &q);
				if (q == p + 1)
					break;
				p = q;
				if (*p == '%')
					p++;
				if (parts++ > 0) {
					/* (a second input: the point repeated) */
					n++;
					if (n == OE_POINTS_MAX)
						break;
					e->py[n] = v;
				}
				e->px[n] = pc / 100;
				known[n] = true;
			}
			n++;
			if (*p == ',')
				p++;
		}
		if (n >= 2) {
			int i = 0;
			if (!known[0]) { e->px[0] = 0; known[0] = true; }
			if (!known[n - 1]) {
				e->px[n - 1] = e->px[n - 2] > 1 ? e->px[n - 2] : 1;
				known[n - 1] = true;
			}
			for (i = 1; i < n; i++)	/* (never decreasing) */
				if (known[i] && e->px[i] < e->px[i - 1])
					e->px[i] = e->px[i - 1];
			for (i = 1; i < n - 1; i++) {
				int j;
				if (known[i])
					continue;
				for (j = i; !known[j]; j++)
					;
				for (int k = i; k < j; k++)
					e->px[k] = e->px[i - 1] + (e->px[j] - e->px[i - 1]) *
							(k - i + 1) / (j - i + 1);
				i = j;
			}
			e->kind = OE_POINTS;
			e->npts = n;
		}
	}
}

static float oe_bez(float a1, float a2, float t)
{
	/* B(t) with P0 = 0, P3 = 1 */
	float u = 1 - t;
	return 3 * u * u * t * a1 + 3 * u * t * t * a2 + t * t * t;
}

static float oe_bez_d(float a1, float a2, float t)
{
	float u = 1 - t;
	return 3 * u * u * a1 + 6 * u * t * (a2 - a1) + 3 * t * t * (1 - a2);
}

/* the eased progress; before_flag: the before phase (steps' jump at 0) */
static double oa_ease(const struct oa_easing *e, double x, bool before_flag)
{
	switch (e->kind) {
	case OE_CUBIC: {
		float t, lo = 0, hi = 1;
		int i;
		if (x <= 0) {
			/* (beyond the ends: the tangent's line) */
			if (e->x1 > 0) return x * e->y1 / e->x1;
			if (e->y1 == 0 && e->x2 > 0) return x * e->y2 / e->x2;
			return 0;
		}
		if (x >= 1) {
			if (e->x2 < 1) return 1 + (x - 1) * (e->y2 - 1) / (e->x2 - 1);
			if (e->y2 == 1 && e->x1 < 1) return 1 + (x - 1) * (e->y1 - 1) / (e->x1 - 1);
			return 1;
		}
		t = (float) x;
		for (i = 0; i < 8; i++) {
			float fx = oe_bez(e->x1, e->x2, t) - (float) x, d = oe_bez_d(e->x1, e->x2, t);
			if (fabsf(fx) < 1e-5f)
				return oe_bez(e->y1, e->y2, t);
			if (fabsf(d) < 1e-6f)
				break;
			t -= fx / d;
			if (t < 0 || t > 1)
				break;
		}
		for (i = 0; i < 40; i++) {
			t = (lo + hi) / 2;
			if (oe_bez(e->x1, e->x2, t) < x)
				lo = t;
			else
				hi = t;
		}
		return oe_bez(e->y1, e->y2, (lo + hi) / 2);
	}
	case OE_STEPS: {
		int jumps = e->pos == 3 ? e->steps + 1 : e->pos == 2 ? e->steps - 1 : e->steps;
		double step = floor(x * e->steps);
		if (e->pos == 0 || e->pos == 3)
			step += 1;
		if (before_flag && fmod(x * e->steps, 1) == 0)
			step -= 1;
		if (x >= 0 && step < 0)
			step = 0;
		if (x <= 1 && step > jumps)
			step = jumps;
		return step / jumps;
	}
	case OE_POINTS: {
		int i;
		if (x <= e->px[0])
			return e->py[0];
		for (i = 1; i < e->npts; i++) {
			if (x <= e->px[i]) {
				double w = e->px[i] - e->px[i - 1];
				if (w <= 0)
					return e->py[i];
				return e->py[i - 1] + (e->py[i] - e->py[i - 1]) *
						(x - e->px[i - 1]) / w;
			}
		}
		return e->py[e->npts - 1];
	}
	default:
		return x;
	}
}

/* ---- the timeline's data ---------------------------------------------------------------- */

enum { OA_TRANSITION, OA_CSS, OA_SCRIPT };
enum { OA_FILL_NONE, OA_FILL_FORWARDS, OA_FILL_BACKWARDS, OA_FILL_BOTH };
enum { OA_PHASE_NONE = -1, OA_BEFORE, OA_ACTIVE, OA_AFTER };

struct oa_keyframe {
	double offset;
	struct oa_easing easing;
	css_computed_style *style;	/* a reference */
};

/* a property's keyframes (those that set it), by offset */
struct oa_track {
	uint16_t prop;
	int n;
	int *kf;
};

struct oa_rec;

struct oa_effect {
	struct oa_effect *next;
	struct oa_rec *rec;
	int kind, id;
	/* the timing (ms) */
	double delay, duration, end_delay, iterations, iter_start;
	int direction, fill;
	struct oa_easing easing;	/* the iteration's (a transition's, a script's) */
	/* the player */
	double start_time, hold_time;	/* NAN: unresolved */
	double rate;
	bool idle, paused;
	/* the keyframes */
	int nkf;
	struct oa_keyframe *kf;
	int ntracks;
	struct oa_track *tracks;
	struct oa_easing kf_easing;	/* the implicit keyframes' */
	/* a transition's property (the table's index) */
	int pidx;
	/* a CSS animation's name and its place in the list */
	lwc_string *name;
	int index;
	/* a script's keyframes' sheets */
	css_stylesheet **sheets;
	int nsheets;
	/* what the events have told */
	int phase;
	double iteration;
	bool run_sent, start_sent, end_sent, finish_sent;
};

struct oa_rec {
	struct oa_rec *next;		/* (the hash chain) */
	struct dom_node *node;		/* a reference */
	css_computed_style *base;	/* the cascade's style: a reference */
	css_computed_style *cur;	/* what the boxes show: a reference */
	struct oa_effect *effects;
	unsigned int gen;
	bool pending;			/* (a frame's new style) */
	css_computed_style *next_style;
	double last_apply;		/* (off screen: a few frames a second) */
};

struct oa_event {
	struct dom_node *node;		/* a reference (NULL: an Animation's state) */
	char type[24];
	char name[64];
	double elapsed;			/* s */
	int id;
};

struct oa_capture {
	struct dom_node *node;
	css_computed_style *style;	/* a reference */
};

struct onyx_anim {
	html_content *c;
	struct oa_rec **tab;
	unsigned int cap, n;
	int next_id;
	double now;
	bool in_tick, tick_scheduled, raf;
	double last_tick, last_work, last_rebox;
	unsigned int gen;
	/* a rebox's old styles */
	struct oa_capture *capt;
	unsigned int capt_n, capt_cap;
	bool reboxing;
	bool applying;		/* (a frame's styles given to the boxes: records stay) */
	bool dirty;		/* (a script changed an animation: its style before a read) */
	bool slow;		/* (only off-screen animations run: a few frames a second) */
	bool flushing;		/* (a script reads: every element's style now) */
	struct oa_event *ev;
	int nev, capev;
	unsigned int frames;
	/* Onyx (docs/06 §32): the frames' pace -- the last frames' change was small (a few
	 * px painted: fewer frames); parked while the window is hidden (no frames until it
	 * shows again: onyx_anim_set_view_state); every timeline, for that */
	bool tiny, parked;
	struct onyx_anim *gnext, **gprev;
	/* NS_PERF: the frames' work (us), printed every 120 frames */
	unsigned int stat_n, stat_layouts, stat_reboxes;
	uint64_t stat_sum, stat_max;
	double stat_start;
};

static double oa_clock(void)
{
	uint64_t ms = 0;
	nsu_getmonotonic_ms(&ms);
	return (double) ms;
}

static unsigned int oa_slot(const void *p, unsigned int cap)
{
	uint64_t x = (uint64_t) (uintptr_t) p;
	x ^= x >> 29;
	x *= 0x9E3779B97F4A7C15ull;
	return (unsigned int) (x >> 32) & (cap - 1);
}

/* Onyx: every timeline (a window shown again wakes the parked ones) */
static struct onyx_anim *oa_all;

static struct onyx_anim *oa_get(html_content *c, bool make)
{
	struct onyx_anim *a = c->onyx_anim;

	if (a != NULL || !make)
		return a;
	a = calloc(1, sizeof(*a));
	if (a == NULL)
		return NULL;
	a->cap = 64;
	a->tab = calloc(a->cap, sizeof(*a->tab));
	if (a->tab == NULL) {
		free(a);
		return NULL;
	}
	a->c = c;
	a->next_id = 1;
	a->last_tick = -1000;
	oa_make_index();
	c->onyx_anim = a;
	a->gnext = oa_all;	/* (Onyx: the timelines, for a window shown again) */
	if (oa_all != NULL)
		oa_all->gprev = &a->gnext;
	a->gprev = &oa_all;
	oa_all = a;
	return a;
}

static struct oa_rec *oa_find(struct onyx_anim *a, struct dom_node *n)
{
	struct oa_rec *r;

	for (r = a->tab[oa_slot(n, a->cap)]; r != NULL; r = r->next)
		if (r->node == n)
			return r;
	return NULL;
}

static struct oa_rec *oa_add(struct onyx_anim *a, struct dom_node *n)
{
	struct oa_rec *r = calloc(1, sizeof(*r));
	unsigned int i;

	if (r == NULL)
		return NULL;
	if ((a->n + 1) * 2 > a->cap) {
		unsigned int cap = a->cap * 2, k;
		struct oa_rec **t = calloc(cap, sizeof(*t));
		if (t != NULL) {
			for (k = 0; k < a->cap; k++) {
				struct oa_rec *q = a->tab[k], *nx;
				for (; q != NULL; q = nx) {
					nx = q->next;
					i = oa_slot(q->node, cap);
					q->next = t[i];
					t[i] = q;
				}
			}
			free(a->tab);
			a->tab = t;
			a->cap = cap;
		}
	}
	r->node = dom_node_ref(n);
	r->gen = a->gen;
	i = oa_slot(n, a->cap);
	r->next = a->tab[i];
	a->tab[i] = r;
	a->n++;
	return r;
}

static void oa_style_unref(css_computed_style *s)
{
	if (s != NULL)
		css_computed_style_destroy(s);
}

static void oa_effect_free(struct oa_effect *e)
{
	int k;

	for (k = 0; k < e->nkf; k++)
		oa_style_unref(e->kf[k].style);
	free(e->kf);
	for (k = 0; k < e->ntracks; k++)
		free(e->tracks[k].kf);
	free(e->tracks);
	for (k = 0; k < e->nsheets; k++)
		if (e->sheets[k] != NULL)
			css_stylesheet_destroy(e->sheets[k]);
	free(e->sheets);
	if (e->name != NULL)
		lwc_string_unref(e->name);
	free(e);
}

/* ---- the events ------------------------------------------------------------------------- */

static void oa_queue(struct onyx_anim *a, struct dom_node *node, const char *type,
		const char *name, double elapsed, int id)
{
	struct oa_event *ev;

	if (a->c->jsthread == NULL)
		return;
	if (a->nev == a->capev) {
		int cap = a->capev ? a->capev * 2 : 16;
		struct oa_event *t = realloc(a->ev, cap * sizeof(*t));
		if (t == NULL)
			return;
		a->ev = t;
		a->capev = cap;
	}
	ev = &a->ev[a->nev++];
	ev->node = node != NULL ? dom_node_ref(node) : NULL;
	snprintf(ev->type, sizeof(ev->type), "%s", type);
	snprintf(ev->name, sizeof(ev->name), "%s", name != NULL ? name : "");
	ev->elapsed = elapsed;
	ev->id = id;
}

static void oa_flush_events(html_content *c, struct onyx_anim *a)
{
	struct oa_event *list = a->ev;
	int n = a->nev, k;

	if (n == 0)
		return;
	a->ev = NULL;
	a->nev = a->capev = 0;
	for (k = 0; k < n; k++) {
		if (c->jsthread != NULL) {
			c->script_hold++;
			js_dispatch_anim_event(c->jsthread, list[k].type, list[k].node,
					list[k].name, list[k].elapsed, list[k].id);
			c->script_hold--;
		}
		if (list[k].node != NULL)
			dom_node_unref(list[k].node);
	}
	free(list);
}

static const char *oa_effect_name(const struct oa_effect *e)
{
	if (e->kind == OA_TRANSITION)
		return oa_props[e->pidx].name;
	if (e->name != NULL)
		return lwc_string_data(e->name);
	return "";
}

/* ---- timing ----------------------------------------------------------------------------- */

static double oa_active_duration(const struct oa_effect *e)
{
	if (e->duration <= 0 || e->iterations <= 0)
		return 0;
	return e->duration * e->iterations;	/* (INFINITY for infinite) */
}

static double oa_end_time(const struct oa_effect *e)
{
	double t = e->delay + oa_active_duration(e) + e->end_delay;
	return t < 0 ? 0 : t;
}

static double oa_current(const struct oa_effect *e, double now)
{
	if (e->idle)
		return NAN;
	if (!isnan(e->hold_time))
		return e->hold_time;
	if (isnan(e->start_time))
		return NAN;
	return (now - e->start_time) * e->rate;
}

struct oa_timing {
	int phase;
	double progress;	/* the transformed progress, NAN: no effect */
	double iteration;
	bool before_flag;
};

static void oa_compute(const struct oa_effect *e, double t, struct oa_timing *out)
{
	double active = oa_active_duration(e), at, overall, simple, iter;
	bool fill_back = e->fill == OA_FILL_BACKWARDS || e->fill == OA_FILL_BOTH;
	bool fill_fwd = e->fill == OA_FILL_FORWARDS || e->fill == OA_FILL_BOTH;
	int dir;

	out->phase = OA_PHASE_NONE;
	out->progress = NAN;
	out->iteration = NAN;
	out->before_flag = false;
	if (isnan(t))
		return;
	if (t < e->delay || (e->rate < 0 && t == e->delay && active > 0)) {
		out->phase = OA_BEFORE;
		if (!fill_back)
			return;
		at = 0;
		out->before_flag = true;
	} else if (t >= e->delay + active) {
		out->phase = OA_AFTER;
		if (!fill_fwd)
			return;
		at = active;
	} else {
		out->phase = OA_ACTIVE;
		at = t - e->delay;
	}
	if (e->duration <= 0)
		overall = out->phase == OA_BEFORE ? 0 : e->iterations;
	else
		overall = at / e->duration;
	overall += e->iter_start;
	if (isinf(overall)) {
		simple = fmod(e->iter_start, 1);
	} else {
		simple = fmod(overall, 1);
		if (simple == 0 && (out->phase == OA_AFTER || (out->phase == OA_ACTIVE &&
				at == active)) && e->iterations != 0 && overall != 0)
			simple = 1;
	}
	if (out->phase == OA_AFTER && isinf(e->iterations))
		iter = INFINITY;
	else if (simple == 1)
		iter = floor(overall) - 1;
	else
		iter = floor(overall);
	out->iteration = iter;
	dir = e->direction;
	if (dir == 2 || dir == 3) {
		bool odd = !isinf(iter) && fmod(iter, 2) != 0;
		if (dir == 3)
			odd = !odd;
		dir = odd ? 1 : 0;
	}
	if (dir == 1)
		simple = 1 - simple;
	out->progress = oa_ease(&e->easing, simple, out->before_flag);
}

/* running: its time goes on (a frame is needed) */
static bool oa_effect_running(const struct oa_effect *e, double now)
{
	double t;

	if (e->idle || e->paused || !isnan(e->hold_time))
		return false;
	t = oa_current(e, now);
	if (isnan(t))
		return false;
	if (e->rate > 0)
		return t < oa_end_time(e);
	if (e->rate < 0)
		return t > 0;
	return false;
}

/* ---- keyframes ---------------------------------------------------------------------------- */

/* the tracks: for each animatable property the keyframes set, their indexes (one per offset:
 * the last) */
static bool oa_make_tracks(struct oa_effect *e, uint8_t (*set)[CSS_N_PROPERTIES])
{
	int counts[OA_NPROPS];

	memset(counts, 0, sizeof(counts));
	for (int k = 0; k < e->nkf; k++)
		for (int p = 0; p < OA_NPROPS; p++)
			if (set[k][oa_props[p].prop])
				counts[p]++;
	e->ntracks = 0;
	e->tracks = calloc(OA_NPROPS, sizeof(*e->tracks));
	if (e->tracks == NULL)
		return false;
	for (int p = 0; p < OA_NPROPS; p++) {
		struct oa_track *t;
		if (counts[p] == 0)
			continue;
		t = &e->tracks[e->ntracks];
		t->prop = oa_props[p].prop;
		t->kf = malloc(counts[p] * sizeof(int));
		if (t->kf == NULL)
			return false;
		t->n = 0;
		for (int k = 0; k < e->nkf; k++) {
			if (!set[k][oa_props[p].prop])
				continue;
			if (t->n > 0 && e->kf[t->kf[t->n - 1]].offset == e->kf[k].offset)
				t->kf[t->n - 1] = k;	/* (the later one at an offset) */
			else
				t->kf[t->n++] = k;
		}
		e->ntracks++;
	}
	return true;
}

/* a keyframe's easing: its own animation-timing-function (set in it), else the default */
static void oa_kf_easing(const css_computed_style *s, bool own, const struct oa_easing *dflt,
		struct oa_easing *out)
{
	lwc_string *t = NULL;
	struct oa_list l;

	if (own && css_computed_animation_timing_function(s, &t) == CSS_ONYX_TEXT_SET &&
	    t != NULL) {
		oa_split(t, &l);
		if (l.n > 0) {
			oa_parse_easing(l.item[0], l.len[0], out);
			return;
		}
	}
	*out = *dflt;
}

/* a CSS animation's keyframes: its @keyframes' declarations over the base; false if there
 * is no such @keyframes */
static bool oa_css_keyframes(html_content *c, struct oa_effect *e,
		const css_computed_style *base, const css_computed_style *parent)
{
	css_onyx_keyframe *kfs = NULL;
	uint32_t n = 0, k;
	uint8_t (*set)[CSS_N_PROPERTIES];
	bool ok = true;

	if (c->select_ctx == NULL ||
	    css_select_ctx_onyx_keyframes(c->select_ctx, e->name, &kfs, &n) != CSS_OK ||
	    n == 0) {
		free(kfs);
		return false;
	}
	e->kf = calloc(n, sizeof(*e->kf));
	set = calloc(n, sizeof(*set));
	if (e->kf == NULL || set == NULL) {
		free(set);
		free(kfs);
		return false;
	}
	for (k = 0; k < n; k++) {
		css_computed_style *s = NULL;
		if (css_computed_style_onyx_apply(base, parent, kfs[k].decls, &c->unit_len_ctx,
				&s, set[k]) != CSS_OK) {
			ok = false;
			break;
		}
		e->kf[k].offset = kfs[k].offset;
		e->kf[k].style = s;
		e->nkf++;
		oa_kf_easing(s, set[k][CSS_PROP_ANIMATION_TIMING_FUNCTION] != 0, &e->kf_easing,
				&e->kf[k].easing);
	}
	if (ok)
		ok = oa_make_tracks(e, set);
	free(set);
	free(kfs);
	return ok;
}

/* ---- the animated style --------------------------------------------------------------------- */

/* the effect's values over the style being made */
static void oa_effect_apply(const struct oa_rec *r, const struct oa_effect *e,
		double progress, css_computed_style **work)
{
	for (int t = 0; t < e->ntracks; t++) {
		const struct oa_track *tr = &e->tracks[t];
		double o0, o1, local;
		const css_computed_style *s0, *s1;
		const struct oa_easing *ease;
		int i;

		/* the keyframes around the progress, the implicit 0 / 1 ones the base's */
		struct { double off; const css_computed_style *s; const struct oa_easing *e; }
				pts[2 + 64];
		int np = 0;
		if (tr->n == 0)
			continue;
		if (e->kf[tr->kf[0]].offset > 0) {
			pts[np].off = 0;
			pts[np].s = r->base;
			pts[np].e = &e->kf_easing;
			np++;
		}
		for (i = 0; i < tr->n && np < 64; i++) {
			pts[np].off = e->kf[tr->kf[i]].offset;
			pts[np].s = e->kf[tr->kf[i]].style;
			pts[np].e = &e->kf[tr->kf[i]].easing;
			np++;
		}
		if (pts[np - 1].off < 1) {
			pts[np].off = 1;
			pts[np].s = r->base;
			pts[np].e = &e->kf_easing;
			np++;
		}
		if (np == 1) {
			s0 = s1 = pts[0].s;
			local = 0;
			ease = pts[0].e;
		} else {
			/* the interval (the first / last beyond the ends) */
			for (i = 0; i < np - 2; i++)
				if (progress < pts[i + 1].off)
					break;
			while (i < np - 2 && pts[i + 1].off == pts[i].off)
				i++;
			if (progress >= 1 && pts[np - 1].off == 1)
				i = np - 2;
			o0 = pts[i].off;
			o1 = pts[i + 1].off;
			s0 = pts[i].s;
			s1 = pts[i + 1].s;
			ease = pts[i].e;
			local = o1 > o0 ? (progress - o0) / (o1 - o0) : (progress >= o1 ? 1 : 0);
		}
		local = oa_ease(ease, local, false);
		if (*work == NULL && css_computed_style_onyx_clone(r->base, work) != CSS_OK) {
			*work = NULL;
			return;
		}
		css_computed_style_onyx_blend(*work, tr->prop, s0, s1, (float) local, NULL);
	}
}

/* the record's animated style now: a new reference */
static css_computed_style *oa_compose(struct oa_rec *r, double now)
{
	css_computed_style *work = NULL;

	for (struct oa_effect *e = r->effects; e != NULL; e = e->next) {
		struct oa_timing tm;
		if (e->idle)
			continue;
		oa_compute(e, oa_current(e, now), &tm);
		if (isnan(tm.progress))
			continue;
		oa_effect_apply(r, e, tm.progress, &work);
	}
	if (work == NULL)
		return css_computed_style_onyx_ref(r->base);
	if (css_computed_style_onyx_intern(&work) != CSS_OK)
		return css_computed_style_onyx_ref(r->base);
	return work;
}

/* the events an effect's progress calls for (transitions, CSS animations), and whether it is
 * done (a transition over: removed) */
static void oa_replace(struct onyx_anim *a, struct oa_effect *e);
static void oa_unlink(struct oa_rec *r, struct oa_effect *e);

static bool oa_effect_events(struct onyx_anim *a, struct oa_effect *e, double now)
{
	struct oa_timing tm;
	double t = oa_current(e, now);
	double active = oa_active_duration(e) / 1000;
	int saved = e->fill;

	if (e->idle)
		return false;
	e->fill = OA_FILL_BOTH;		/* (the phase and iteration whatever the fill) */
	oa_compute(e, t, &tm);
	e->fill = saved;
	if (e->kind == OA_SCRIPT || tm.phase == OA_PHASE_NONE) {
		if (e->kind == OA_SCRIPT && !e->finish_sent && !isnan(t) && !e->paused &&
		    ((e->rate > 0 && t >= oa_end_time(e)) || (e->rate < 0 && t <= 0))) {
			/* finished: its time held at the end (told to its Animation) */
			bool fills = e->rate > 0 ? (e->fill == OA_FILL_FORWARDS ||
					e->fill == OA_FILL_BOTH) : (e->fill == OA_FILL_BACKWARDS ||
					e->fill == OA_FILL_BOTH);
			e->finish_sent = true;
			e->hold_time = e->rate > 0 ? oa_end_time(e) : 0;
			oa_queue(a, NULL, "finish", NULL, e->hold_time, e->id);
			if (!fills)
				return true;	/* (no effect now: gone -- play() makes it again) */
			oa_replace(a, e);
		}
		return false;
	}
	if (e->kind == OA_TRANSITION) {
		if (!e->start_sent && tm.phase != OA_BEFORE) {
			e->start_sent = true;
			oa_queue(a, e->rec->node, "transitionstart", oa_effect_name(e), 0, e->id);
		}
		if (tm.phase == OA_AFTER && !e->end_sent) {
			e->end_sent = true;
			oa_queue(a, e->rec->node, "transitionend", oa_effect_name(e), active,
					e->id);
			oa_queue(a, NULL, "finish", NULL, 0, e->id);
			return true;
		}
		return false;
	}
	/* a CSS animation */
	if (!e->start_sent && tm.phase != OA_BEFORE) {
		e->start_sent = true;
		e->iteration = tm.phase == OA_ACTIVE ? tm.iteration : 0;
		oa_queue(a, e->rec->node, "animationstart", oa_effect_name(e),
				e->delay < 0 ? -e->delay / 1000 : 0, e->id);
	}
	if (tm.phase == OA_ACTIVE && e->start_sent && tm.iteration != e->iteration &&
	    !isnan(tm.iteration)) {
		e->iteration = tm.iteration;
		oa_queue(a, e->rec->node, "animationiteration", oa_effect_name(e),
				(tm.iteration - e->iter_start) * e->duration / 1000, e->id);
	}
	if (tm.phase == OA_AFTER && !e->end_sent) {
		e->end_sent = true;
		oa_queue(a, e->rec->node, "animationend", oa_effect_name(e), active, e->id);
		oa_queue(a, NULL, "finish", NULL, 0, e->id);
	}
	if (tm.phase == OA_BEFORE && e->end_sent && e->rate < 0 && !e->finish_sent) {
		e->finish_sent = true;
	}
	e->phase = tm.phase;
	return false;
}

/* the animation replacement (Web Animations 1, 5.5): a finished script's animation that
 * fills forwards removes the earlier finished filling ones whose properties it all covers
 * ("remove" to their Animations) -- a page calling animate() at each hover does not pile
 * them up */
static void oa_replace(struct onyx_anim *a, struct oa_effect *e)
{
	struct oa_effect *o, *nx;

	for (o = e->rec->effects; o != NULL && o != e; o = nx) {
		bool covered = true;
		nx = o->next;
		if (o->kind != OA_SCRIPT || !o->finish_sent || o->paused || o->idle)
			continue;
		for (int i = 0; i < o->ntracks && covered; i++) {
			covered = false;
			for (int j = 0; j < e->ntracks; j++)
				if (e->tracks[j].prop == o->tracks[i].prop)
					covered = true;
		}
		if (!covered)
			continue;
		oa_queue(a, NULL, "remove", NULL, 0, o->id);
		oa_unlink(e->rec, o);
		oa_effect_free(o);
	}
}

/* an effect cancelled (its events), unlinked by the caller */
static void oa_cancel_events(struct onyx_anim *a, struct oa_effect *e, double now)
{
	double t = oa_current(e, now);
	double el = isnan(t) ? 0 : (t - e->delay) / 1000;

	if (el < 0)
		el = 0;
	if (e->kind == OA_TRANSITION && !e->end_sent)
		oa_queue(a, e->rec->node, "transitioncancel", oa_effect_name(e), el, e->id);
	else if (e->kind == OA_CSS && !e->end_sent && e->start_sent)
		oa_queue(a, e->rec->node, "animationcancel", oa_effect_name(e), el, e->id);
	oa_queue(a, NULL, "cancel", NULL, 0, e->id);
}

static void oa_unlink(struct oa_rec *r, struct oa_effect *e)
{
	struct oa_effect **p;

	for (p = &r->effects; *p != NULL; p = &(*p)->next)
		if (*p == e) {
			*p = e->next;
			return;
		}
}

/* an effect put in the composite order: transitions, CSS animations (by their place in
 * the list), the scripts' (by creation) */
static void oa_link(struct oa_rec *r, struct oa_effect *e)
{
	struct oa_effect **p;

	e->rec = r;
	for (p = &r->effects; *p != NULL; p = &(*p)->next) {
		if ((*p)->kind > e->kind)
			break;
		if ((*p)->kind == OA_CSS && e->kind == OA_CSS && (*p)->index > e->index)
			break;
	}
	e->next = *p;
	*p = e;
}

static struct oa_effect *oa_new_effect(struct onyx_anim *a, int kind, double now)
{
	struct oa_effect *e = calloc(1, sizeof(*e));

	if (e == NULL)
		return NULL;
	e->kind = kind;
	e->id = a->next_id++;
	e->rate = 1;
	e->iterations = 1;
	e->start_time = now;
	e->hold_time = NAN;
	e->phase = OA_PHASE_NONE;
	e->iteration = NAN;
	e->easing.kind = OE_LINEAR;
	e->kf_easing.kind = OE_LINEAR;
	return e;
}

/* ---- the scheduler ---------------------------------------------------------------------------- */

static void oa_tick(void *p);

static bool oa_any_running(struct onyx_anim *a, double now)
{
	for (unsigned int k = 0; k < a->cap; k++)
		for (struct oa_rec *r = a->tab[k]; r != NULL; r = r->next)
			for (struct oa_effect *e = r->effects; e != NULL; e = e->next)
				if (oa_effect_running(e, now) ||
				    (e->kind == OA_SCRIPT && !e->finish_sent && !e->idle &&
				     !e->paused && isnan(e->hold_time)))
					return true;
	return false;
}

/* Onyx (docs/06 §32): the window's state, as the frontend sees it (onyx_anim_set_view_state) */
int onyx_view_state = ONYX_VIEW_FOCUSED;

/* the frames' pace (ms between two): 30 a second, 15 when the last frames changed only a few
 * px; half that in a window without the keyboard */
#define OA_FRAME_MS			33
#define OA_FRAME_TINY_MS		66
#define OA_FRAME_UNFOCUSED_MS		66
#define OA_FRAME_UNFOCUSED_TINY_MS	125
/* a frame's change is "tiny" when what it paints again is at most this many px */
#define OA_TINY_PX			(48 * 48)

/* the next frame: ~30 Hz (Onyx: was ~60 -- on the Pi every app shares core 0 with the
 * browser), fewer when the change is tiny or the window not focused, less when frames take
 * long (the Pi: never more than 2/3 of the time on them, the events and timers keep the
 * rest) */
static void oa_schedule(struct onyx_anim *a)
{
	double now = oa_clock(), interval, wait;
	bool unfocused = onyx_view_state == ONYX_VIEW_UNFOCUSED;

	if (a->tick_scheduled)
		return;
	if (a->tiny && !a->raf)
		interval = unfocused ? OA_FRAME_UNFOCUSED_TINY_MS : OA_FRAME_TINY_MS;
	else
		interval = unfocused ? OA_FRAME_UNFOCUSED_MS : OA_FRAME_MS;
	if (a->last_work * 1.5 > interval)
		interval = a->last_work * 1.5;
	if (a->slow && !a->raf && a->nev == 0 && !a->dirty)
		interval = 250;
	wait = a->last_tick + interval - now;
	if (wait < 1)
		wait = 1;
	a->tick_scheduled = true;
	guit->misc->schedule((int) wait, oa_tick, a->c);
}

/* exported function documented in html/onyx_anim.h */
void onyx_anim_set_view_state(int state)
{
	bool was_hidden = onyx_view_state == ONYX_VIEW_HIDDEN;

	if (state == onyx_view_state)
		return;
	onyx_view_state = state;
	if (was_hidden != (state == ONYX_VIEW_HIDDEN))
		js_view_visibility_changed();	/* (document.hidden, visibilitychange) */
	if (!was_hidden)
		return;
	/* shown again: the parked timelines' frames go on */
	for (struct onyx_anim *a = oa_all; a != NULL; a = a->gnext) {
		if (!a->parked)
			continue;
		a->parked = false;
		guit->misc->schedule(-1, oa_tick, a->c);
		a->tick_scheduled = false;
		oa_schedule(a);
	}
}

/* ---- transitions ------------------------------------------------------------------------------ */

static struct oa_effect *oa_find_transition(struct oa_rec *r, int pidx)
{
	for (struct oa_effect *e = r->effects; e != NULL; e = e->next)
		if (e->kind == OA_TRANSITION && e->pidx == pidx && !e->idle)
			return e;
	return NULL;
}

static void oa_drop(struct onyx_anim *a, struct oa_rec *r, struct oa_effect *e, double now,
		bool events)
{
	if (events)
		oa_cancel_events(a, e, now);
	oa_unlink(r, e);
	oa_effect_free(e);
}

/* the transitions a style change starts (CSS Transitions 1, 3): old the before-change
 * style (NULL: none -- the element was not rendered), nw the after-change one */
static void oa_transitions(struct onyx_anim *a, struct oa_rec *r,
		const css_computed_style *old, const css_computed_style *nw, double now)
{
	struct oa_list lp, ld, lf, ll;
	int item_of[OA_NPROPS];
	css_computed_style *scratch = NULL;

	oa_split(oa_text(css_computed_transition_property, nw), &lp);
	oa_split(oa_text(css_computed_transition_duration, nw), &ld);
	oa_split(oa_text(css_computed_transition_timing_function, nw), &lf);
	oa_split(oa_text(css_computed_transition_delay, nw), &ll);

	/* each property's item (the last that names it) */
	for (int p = 0; p < OA_NPROPS; p++)
		item_of[p] = -1;
	if (lp.n == 0) {
		for (int p = 0; p < OA_NPROPS; p++)
			item_of[p] = 0;		/* ("all") */
	} else {
		for (int i = 0; i < lp.n; i++) {
			int idx[OA_NPROPS], n = oa_lookup(lp.item[i], lp.len[i], idx, OA_NPROPS);
			for (int k = 0; k < n; k++)
				item_of[idx[k]] = i;
		}
	}

	for (int p = 0; p < OA_NPROPS; p++) {
		uint16_t prop = oa_props[p].prop;
		struct oa_effect *run = oa_find_transition(r, p), *e;
		int i = item_of[p];
		double dur, delay;
		bool interp = true;

		if (i < 0) {
			if (run != NULL)
				oa_drop(a, r, run, now, true);
			continue;
		}
		dur = oa_seconds(&ld, i, 0) * 1000;
		delay = oa_seconds(&ll, i, 0) * 1000;
		if (run != NULL && run->nkf == 2 &&
		    css_computed_style_onyx_same(run->kf[1].style, nw, prop))
			continue;	/* (still going to that value) */
		if (old == NULL || css_computed_style_onyx_same(old, nw, prop) ||
		    dur < 0 || dur + delay <= 0) {
			if (run != NULL)
				oa_drop(a, r, run, now, true);
			continue;
		}
		/* discrete values (auto, another unit...): no transition */
		if (scratch == NULL && css_computed_style_onyx_clone(nw, &scratch) != CSS_OK) {
			scratch = NULL;
			break;
		}
		css_computed_style_onyx_blend(scratch, prop, old, nw, 0.5f, &interp);
		if (!interp) {
			if (run != NULL)
				oa_drop(a, r, run, now, true);
			continue;
		}
		if (run != NULL)
			oa_drop(a, r, run, now, true);
		e = oa_new_effect(a, OA_TRANSITION, now);
		if (e == NULL)
			break;
		e->pidx = p;
		e->duration = dur;
		e->delay = delay;
		e->fill = OA_FILL_BACKWARDS;
		oa_parse_easing(lf.n > 0 ? lf.item[i % lf.n] : NULL,
				lf.n > 0 ? lf.len[i % lf.n] : 0, &e->easing);
		e->kf = calloc(2, sizeof(*e->kf));
		e->tracks = calloc(1, sizeof(*e->tracks));
		if (e->kf == NULL || e->tracks == NULL ||
		    (e->tracks[0].kf = malloc(2 * sizeof(int))) == NULL) {
			oa_effect_free(e);
			break;
		}
		e->nkf = 2;
		e->kf[0].offset = 0;
		e->kf[0].style = css_computed_style_onyx_ref(old);
		e->kf[1].offset = 1;
		e->kf[1].style = css_computed_style_onyx_ref(nw);
		e->ntracks = 1;
		e->tracks[0].prop = prop;
		e->tracks[0].n = 2;
		e->tracks[0].kf[0] = 0;
		e->tracks[0].kf[1] = 1;
		oa_link(r, e);
		e->run_sent = true;
		oa_queue(a, r->node, "transitionrun", oa_props[p].name, 0, e->id);
	}
	if (scratch != NULL)
		css_computed_style_destroy(scratch);
}

/* ---- CSS animations ------------------------------------------------------------------------ */

static void oa_css_timing(struct oa_effect *e, int i, const struct oa_list *ld,
		const struct oa_list *lf, const struct oa_list *ll, const struct oa_list *lc,
		const struct oa_list *lr, const struct oa_list *lm)
{
	e->duration = oa_seconds(ld, i, 0) * 1000;
	e->delay = oa_seconds(ll, i, 0) * 1000;
	if (lc->n > 0 && oa_item_is(lc, i, "infinite"))
		e->iterations = INFINITY;
	else
		e->iterations = lc->n > 0 ? strtod(lc->item[i % lc->n], NULL) : 1;
	e->direction = oa_item_is(lr, i, "reverse") ? 1 : oa_item_is(lr, i, "alternate") ? 2 :
			oa_item_is(lr, i, "alternate-reverse") ? 3 : 0;
	e->fill = oa_item_is(lm, i, "forwards") ? OA_FILL_FORWARDS :
			oa_item_is(lm, i, "backwards") ? OA_FILL_BACKWARDS :
			oa_item_is(lm, i, "both") ? OA_FILL_BOTH : OA_FILL_NONE;
	oa_parse_easing(lf->n > 0 ? lf->item[i % lf->n] : NULL, lf->n > 0 ? lf->len[i % lf->n] : 0,
			&e->kf_easing);
}

static void oa_set_paused(struct oa_effect *e, bool paused, double now)
{
	if (paused == e->paused)
		return;
	if (paused) {
		double t = oa_current(e, now);
		e->hold_time = isnan(t) ? 0 : t;
		e->paused = true;
	} else {
		if (!isnan(e->hold_time) && e->rate != 0)
			e->start_time = now - e->hold_time / e->rate;
		e->hold_time = NAN;
		e->paused = false;
	}
}

static void oa_css_animations(struct onyx_anim *a, struct oa_rec *r,
		const css_computed_style *nw, const css_computed_style *parent, double now)
{
	struct oa_list ln, ld, lf, ll, lc, lr, lm, lpl;
	struct oa_effect *e, *nx;
	bool used[OA_LIST_MAX];

	oa_split(oa_text(css_computed_animation_name, nw), &ln);
	oa_split(oa_text(css_computed_animation_duration, nw), &ld);
	oa_split(oa_text(css_computed_animation_timing_function, nw), &lf);
	oa_split(oa_text(css_computed_animation_delay, nw), &ll);
	oa_split(oa_text(css_computed_animation_iteration_count, nw), &lc);
	oa_split(oa_text(css_computed_animation_direction, nw), &lr);
	oa_split(oa_text(css_computed_animation_fill_mode, nw), &lm);
	oa_split(oa_text(css_computed_animation_play_state, nw), &lpl);
	memset(used, 0, sizeof(used));

	/* the running ones: kept if their name is still in the list (the first match) */
	for (e = r->effects; e != NULL; e = nx) {
		int i;
		nx = e->next;
		if (e->kind != OA_CSS)
			continue;
		for (i = 0; i < ln.n; i++)
			if (!used[i] && ln.len[i] == lwc_string_length(e->name) &&
			    strncmp(ln.item[i], lwc_string_data(e->name), ln.len[i]) == 0)
				break;
		if (i == ln.n) {
			oa_drop(a, r, e, now, true);
			continue;
		}
		used[i] = true;
		e->index = i;
		oa_css_timing(e, i, &ld, &lf, &ll, &lc, &lr, &lm);
		oa_set_paused(e, oa_item_is(&lpl, i, "paused"), now);
	}
	/* the new ones */
	for (int i = 0; i < ln.n; i++) {
		if (used[i] || (ln.len[i] == 4 && strncmp(ln.item[i], "none", 4) == 0))
			continue;
		e = oa_new_effect(a, OA_CSS, now);
		if (e == NULL)
			return;
		if (lwc_intern_string(ln.item[i], ln.len[i], &e->name) != lwc_error_ok) {
			e->name = NULL;
			oa_effect_free(e);
			return;
		}
		e->index = i;
		oa_css_timing(e, i, &ld, &lf, &ll, &lc, &lr, &lm);
		if (!oa_css_keyframes(a->c, e, nw, parent)) {
			oa_effect_free(e);	/* (no such @keyframes: nothing runs) */
			continue;
		}
		if (oa_item_is(&lpl, i, "paused")) {
			e->paused = true;
			e->hold_time = 0;
		}
		oa_link(r, e);
	}
}

/* ---- the selection's hook ------------------------------------------------------------------- */

static bool oa_wants(const css_computed_style *s)
{
	lwc_string *t = NULL;

	if (css_computed_animation_name(s, &t) == CSS_ONYX_TEXT_SET && t != NULL &&
	    !(lwc_string_length(t) == 4 && strcmp(lwc_string_data(t), "none") == 0))
		return true;
	t = NULL;
	if (css_computed_transition_duration(s, &t) == CSS_ONYX_TEXT_SET && t != NULL) {
		/* any non-zero duration */
		const char *p = lwc_string_data(t);
		while (*p != '\0') {
			if (strtod(p, NULL) > 0)
				return true;
			p = strchr(p, ',');
			if (p == NULL)
				break;
			p++;
		}
	}
	t = NULL;
	return false;
}

static css_computed_style *oa_captured(struct onyx_anim *a, struct dom_node *n)
{
	for (unsigned int k = 0; k < a->capt_n; k++)
		if (a->capt[k].node == n)
			return a->capt[k].style;
	return NULL;
}

static void oa_rec_drop(struct onyx_anim *a, struct oa_rec *r, double now, bool events)
{
	struct oa_rec **p;
	unsigned int i = oa_slot(r->node, a->cap);

	while (r->effects != NULL)
		oa_drop(a, r, r->effects, now, events);
	for (p = &a->tab[i]; *p != NULL; p = &(*p)->next)
		if (*p == r) {
			*p = r->next;
			break;
		}
	a->n--;
	oa_style_unref(r->base);
	oa_style_unref(r->cur);
	oa_style_unref(r->next_style);
	dom_node_unref(r->node);
	free(r);
}

/* exported function documented in html/onyx_anim.h */
void onyx_anim_styled(struct html_content *c, struct dom_node *n,
		struct css_select_results *res, const struct css_computed_style *parent_style)
{
	struct onyx_anim *a = c->onyx_anim;
	css_computed_style *nw, *old = NULL, *cur;
	struct oa_rec *r;
	struct box *b;
	double now;
	bool wants;

	static int off = -1;

	if (off < 0)	/* (NS_NO_ANIM: no transitions nor animations -- to compare) */
		off = getenv("NS_NO_ANIM") != NULL;
	if (off || res == NULL || (nw = res->styles[CSS_PSEUDO_ELEMENT_NONE]) == NULL ||
	    c->onyx_anim_probe)
		return;
	if (a == NULL) {
		if (!oa_wants(nw))
			return;		/* (the page has no animation so far: nothing to do) */
		a = oa_get(c, true);
		if (a == NULL)
			return;
	}
	now = a->in_tick ? a->now : oa_clock();
	r = oa_find(a, n);
	if (ns_computed_display(nw, false) == CSS_DISPLAY_NONE) {
		/* not rendered: its animations stop */
		if (r != NULL && !a->applying)
			oa_rec_drop(a, r, now, true);
		return;
	}
	wants = oa_wants(nw);
	if (r == NULL && !wants)
		return;
	if (r == NULL) {
		/* the before-change style: what its boxes showed */
		b = box_for_node(n);
		if (b != NULL && b->style != NULL && !a->reboxing)
			old = b->style;
		else if (a->reboxing)
			old = oa_captured(a, n);
		r = oa_add(a, n);
		if (r == NULL)
			return;
	} else {
		old = r->cur;
	}
	r->gen = a->gen;
	if (r->base != nw) {
		oa_transitions(a, r, old, nw, now);
		oa_css_animations(a, r, nw, parent_style, now);
		oa_style_unref(r->base);
		r->base = css_computed_style_onyx_ref(nw);
	}
	if (r->effects == NULL && !wants && !a->applying) {
		oa_rec_drop(a, r, now, false);
		return;
	}
	/* the result's style: the animated one */
	cur = oa_compose(r, now);
	if (cur != nw) {
		res->styles[CSS_PSEUDO_ELEMENT_NONE] = cur;
		css_computed_style_destroy(nw);		/* (the record keeps the base) */
	} else {
		css_computed_style_destroy(cur);
	}
	if (!a->applying) {
		/* (a frame's: the frame sets it once the boxes have it) */
		oa_style_unref(r->cur);
		r->cur = css_computed_style_onyx_ref(res->styles[CSS_PSEUDO_ELEMENT_NONE]);
	}
	if (a->nev > 0)
		oa_schedule(a);
	for (struct oa_effect *e = r->effects; e != NULL && !a->tick_scheduled; e = e->next)
		if (oa_effect_running(e, now) || (e->kind == OA_SCRIPT && !e->finish_sent))
			oa_schedule(a);
}

/* exported function documented in html/onyx_anim.h */
void onyx_anim_rebox_begin(struct html_content *c, struct box *old_layout)
{
	struct onyx_anim *a = c->onyx_anim;
	struct box *stack[512];
	int sp = 0;

	if (a == NULL)
		return;
	a->gen++;
	a->reboxing = true;
	a->capt_n = 0;
	/* the element boxes' styles (depth-first, iterative) */
	if (old_layout != NULL)
		stack[sp++] = old_layout;
	while (sp > 0) {
		struct box *b = stack[--sp];
		for (; b != NULL; b = b->next) {
			if (b->node != NULL && b->type != BOX_TEXT && b->style != NULL &&
			    box_for_node(b->node) == b) {
				if (a->capt_n == a->capt_cap) {
					unsigned int cap = a->capt_cap ? a->capt_cap * 2 : 256;
					struct oa_capture *t = realloc(a->capt,
							cap * sizeof(*t));
					if (t == NULL)
						break;
					a->capt = t;
					a->capt_cap = cap;
				}
				a->capt[a->capt_n].node = b->node;
				a->capt[a->capt_n].style =
						css_computed_style_onyx_ref(b->style);
				a->capt_n++;
			}
			if (b->children != NULL && sp < 512)
				stack[sp++] = b->children;
		}
	}
}

/* exported function documented in html/onyx_anim.h */
void onyx_anim_rebox_end(struct html_content *c, bool success)
{
	struct onyx_anim *a = c->onyx_anim;
	double now;

	if (a == NULL)
		return;
	now = oa_clock();
	for (unsigned int k = 0; k < a->capt_n; k++)
		oa_style_unref(a->capt[k].style);
	a->capt_n = 0;
	a->reboxing = false;
	if (success) {
		/* the elements the new boxes do not show: their animations cancelled */
		for (unsigned int k = 0; k < a->cap; k++) {
			struct oa_rec *r = a->tab[k], *nx;
			for (; r != NULL; r = nx) {
				nx = r->next;
				if (r->gen != a->gen)
					oa_rec_drop(a, r, now, true);
			}
		}
	}
	if (a->nev > 0)
		oa_schedule(a);
}

/* ---- the frames ------------------------------------------------------------------------------ */

static bool oa_inherited_changed(const css_computed_style *a, const css_computed_style *b)
{
	for (int p = 0; p < OA_NPROPS; p++)
		if (oa_props[p].inherited &&
		    !css_computed_style_onyx_same(a, b, oa_props[p].prop))
			return true;
	return false;
}

/* the properties that only change how a box is painted, not where: off screen, such an
 * animation is shown a few times a second only (nothing to see; the Pi's CPU) */
static bool oa_paint_prop(uint16_t prop)
{
	switch (prop) {
	case CSS_PROP_OPACITY: case CSS_PROP_COLOR: case CSS_PROP_BACKGROUND_COLOR:
	case CSS_PROP_BORDER_TOP_COLOR: case CSS_PROP_BORDER_RIGHT_COLOR:
	case CSS_PROP_BORDER_BOTTOM_COLOR: case CSS_PROP_BORDER_LEFT_COLOR:
	case CSS_PROP_OUTLINE_COLOR: case CSS_PROP_COLUMN_RULE_COLOR: case CSS_PROP_BOX_SHADOW:
	case CSS_PROP_VISIBILITY: case CSS_PROP_BORDER_TOP_LEFT_RADIUS:
	case CSS_PROP_BORDER_TOP_RIGHT_RADIUS: case CSS_PROP_BORDER_BOTTOM_LEFT_RADIUS:
	case CSS_PROP_BORDER_BOTTOM_RIGHT_RADIUS: case CSS_PROP_FILL: case CSS_PROP_STROKE:
	case CSS_PROP_FILL_OPACITY: case CSS_PROP_STROKE_OPACITY: case CSS_PROP_STOP_COLOR:
	case CSS_PROP_STOP_OPACITY: case CSS_PROP_FILTER: case CSS_PROP_BACKDROP_FILTER:
		return true;
	default:
		return false;
	}
}

/* whether the record's element is out of the window's view and animates only painting */
static bool oa_off_screen(html_content *c, struct oa_rec *r, struct box *b)
{
	int sx = 0, sy = 0, x, y, x0, y0, x1, y1, m = 64;

	for (struct oa_effect *e = r->effects; e != NULL; e = e->next)
		for (int t = 0; t < e->ntracks; t++)
			if (!oa_paint_prop(e->tracks[t].prop))
				return false;
	if (c->bw == NULL || c->bw->window == NULL || c->bw->parent != NULL ||
	    !guit->window->get_scroll(c->bw->window, &sx, &sy))
		return false;
	box_coords(b, &x, &y);
	x0 = x + (b->descendant_x0 < 0 ? b->descendant_x0 : 0) - m;
	y0 = y + (b->descendant_y0 < 0 ? b->descendant_y0 : 0) - m;
	x1 = x + (b->descendant_x1 > b->width ? b->descendant_x1 : b->width) + m;
	y1 = y + (b->descendant_y1 > b->height ? b->descendant_y1 : b->height) + m;
	return x1 < sx || y1 < sy || x0 > sx + c->base.available_width ||
		y0 > sy + c->base.available_height;
}

/* each record's style at now, given to its boxes */
static void oa_update(html_content *c, struct onyx_anim *a, double now)
{
	struct onyx_restyle_item *items = NULL;
	struct oa_rec **recs = NULL;
	int n = 0, cap = 0;
	bool can_apply = c->layout != NULL && c->box_conversion_context == NULL &&
			!c->base.locked && !c->rebox_pending && !c->aborted;

	a->slow = can_apply;

	for (unsigned int k = 0; k < a->cap; k++) {
		for (struct oa_rec *r = a->tab[k]; r != NULL; r = r->next) {
			struct oa_effect *e, *nx;
			css_computed_style *s;
			bool had = r->effects != NULL;

			for (e = r->effects; e != NULL; e = nx) {
				nx = e->next;
				if (oa_effect_events(a, e, now)) {
					/* a transition over: the base's value from now */
					oa_unlink(r, e);
					oa_effect_free(e);
				}
			}
			if (!had || !can_apply)
				continue;
			if (box_for_node(r->node) == NULL ||
			    !oa_off_screen(c, r, box_for_node(r->node))) {
				for (e = r->effects; e != NULL && a->slow; e = e->next)
					if (oa_effect_running(e, now))
						a->slow = false;
			} else if (a->in_tick && !a->flushing && now - r->last_apply < 240) {
				continue;
			}
			r->last_apply = now;
			s = oa_compose(r, now);
			if (s == r->cur || box_for_node(r->node) == NULL) {
				css_computed_style_destroy(s);
				continue;
			}
			if (n == cap) {
				int nc = cap ? cap * 2 : 16;
				void *t1 = realloc(items, nc * sizeof(*items));
				void *t2 = t1 != NULL ? realloc(recs, nc * sizeof(*recs)) : NULL;
				if (t1 != NULL)
					items = t1;
				if (t2 == NULL) {
					css_computed_style_destroy(s);
					continue;
				}
				recs = t2;
				cap = nc;
			}
			items[n].node = r->node;
			items[n].style = s;
			items[n].deep = oa_inherited_changed(r->cur, s);
			recs[n] = r;
			n++;
		}
	}
	if (n > 0) {
		bool relayout = false, ok, rebox = false;
		uint64_t t0 = onyx_perf_now();

		/* all at once: their rectangles redrawn, else the page laid out again; else
		 * one by one (an element this cannot restyle -- the root, a pseudo-element
		 * appearing -- has its boxes built again, a few times a second at most) */
		a->applying = true;
		ok = onyx_hover_restyle_elements(c, items, n, false) ||
				(relayout = onyx_hover_restyle_elements(c, items, n, true));
		for (int k = 0; k < n; k++) {
			bool done = ok;
			if (!ok) {
				done = onyx_hover_restyle_elements(c, &items[k], 1, false);
				if (!done && onyx_hover_restyle_elements(c, &items[k], 1, true))
					done = relayout = true;
			}
			if (done) {
				oa_style_unref(recs[k]->cur);
				recs[k]->cur = (css_computed_style *) items[k].style;
			} else {
				css_computed_style_destroy((css_computed_style *) items[k].style);
				rebox = true;
			}
		}
		a->applying = false;
		/* Onyx (docs/06 §32): the change's size -- painting only, over a few px (a
		 * pulsing dot's shadow): the next frames come at half the pace */
		if (a->in_tick && !a->flushing) {
			long long px = 0;
			bool tiny = ok && !relayout && !rebox;
			for (int k = 0; k < n && tiny; k++) {
				struct box *b = box_for_node(recs[k]->node);
				int w, h;
				if (b == NULL)
					continue;
				w = (b->descendant_x1 > b->width ? b->descendant_x1 : b->width) -
					(b->descendant_x0 < 0 ? b->descendant_x0 : 0) + 16;
				h = (b->descendant_y1 > b->height ? b->descendant_y1 : b->height) -
					(b->descendant_y0 < 0 ? b->descendant_y0 : 0) + 16;
				px += (long long) w * h;
				if (px > OA_TINY_PX)
					tiny = false;
			}
			a->tiny = tiny;
		}
		onyx_perf_log(relayout ? "anim:restyle+layout" : "anim:restyle", t0);
		if (rebox && now - a->last_rebox >= 200) {
			/* (the selection gives the boxes their animated styles) */
			a->last_rebox = now;
			a->stat_reboxes++;
			html_script_dom_changed(c);
		}
		if (relayout && !c->rebox_pending) {
			a->stat_layouts++;
			t0 = onyx_perf_now();
			content__reformat(&c->base, false, c->base.available_width,
					c->base.available_height);
			onyx_perf_log("anim:layout", t0);
		}
	}
	free(items);
	free(recs);
	/* the replaced styles no box sees freed now and then */
	if (++a->frames % 120 == 0 && c->hover_old_n > 0)
		onyx_hover_collect(c);
}

static void oa_tick(void *p)
{
	html_content *c = p;
	struct onyx_anim *a = c->onyx_anim;
	uint64_t t0 = onyx_perf_now(), work = 0;
	double now, start;

	if (a == NULL)
		return;
	a->tick_scheduled = false;
	if (onyx_view_state == ONYX_VIEW_HIDDEN && !c->aborted) {
		/* Onyx: the window is not seen (minimised, on another workspace, covered):
		 * no frame -- no style made, nothing painted, no requestAnimationFrame
		 * callback -- until it shows again (onyx_anim_set_view_state) */
		a->parked = true;
		return;
	}
	if (c->onyx_closed && !c->aborted) {
		/* closed (another page shows in its window, this one kept for the history): no
		 * frames, its boxes' layers not touched; looked at again in a second */
		a->tick_scheduled = true;
		guit->misc->schedule(1000, oa_tick, c);
		return;
	}
	start = now = oa_clock();
	a->last_tick = now;
	if (c->aborted)
		return;
	a->now = now;
	a->in_tick = true;
	oa_update(c, a, now);
	a->in_tick = false;
	work = onyx_perf_now() - t0;	/* (the animations' work: not the scripts') */
	oa_flush_events(c, a);
	if (a->raf && c->jsthread != NULL) {
		a->raf = false;
		c->script_hold++;
		js_animation_frame(c->jsthread, now);
		c->script_hold--;
	}
	a->last_work = oa_clock() - start;
	if (onyx_perf_on() && a->n > 0) {
		uint64_t d = work;
		a->stat_n++;
		a->stat_sum += d;
		if (d > a->stat_max)
			a->stat_max = d;
		if (a->stat_n == 1)
			a->stat_start = start;
		if (a->stat_n == 120) {
			fprintf(stderr, "ONYX-PERF anim:frames 120 in %.0f ms, %u elements: %lu us a "
					"frame on average, %lu at most; %u laid out again, %u "
					"reboxed\n", start - a->stat_start, a->n,
					(unsigned long) (a->stat_sum / a->stat_n),
					(unsigned long) a->stat_max, a->stat_layouts, a->stat_reboxes);
			a->stat_n = a->stat_layouts = a->stat_reboxes = 0;
			a->stat_sum = a->stat_max = 0;
		}
		if (d >= ONYX_PERF_MIN_US)
			fprintf(stderr, "ONYX-PERF anim:frame %lu us\n", (unsigned long) d);
	}
	if (a->raf || a->nev > 0 || oa_any_running(a, oa_clock()))
		oa_schedule(a);
}

/* exported function documented in html/onyx_anim.h */
void onyx_anim_flush(struct html_content *c)
{
	struct onyx_anim *a = c->onyx_anim;

	if (a == NULL || !a->dirty || a->in_tick || a->applying)
		return;
	a->dirty = false;
	a->in_tick = true;
	a->flushing = true;
	a->now = oa_clock();
	oa_update(c, a, a->now);
	a->flushing = false;
	a->in_tick = false;
	if (a->nev > 0)
		oa_schedule(a);
}

/* exported function documented in html/onyx_anim.h */
void onyx_anim_request_frame(struct html_content *c)
{
	struct onyx_anim *a = oa_get(c, true);

	if (a == NULL)
		return;
	a->raf = true;
	oa_schedule(a);
}

/* exported function documented in html/onyx_anim.h */
bool onyx_anim_running(struct html_content *c)
{
	struct onyx_anim *a = c->onyx_anim;
	return a != NULL && oa_any_running(a, oa_clock());
}

/* exported function documented in html/onyx_anim.h */
void onyx_anim_fini(struct html_content *c)
{
	struct onyx_anim *a = c->onyx_anim;

	if (a == NULL)
		return;
	guit->misc->schedule(-1, oa_tick, c);
	*a->gprev = a->gnext;
	if (a->gnext != NULL)
		a->gnext->gprev = a->gprev;
	for (unsigned int k = 0; k < a->cap; k++)
		while (a->tab[k] != NULL)
			oa_rec_drop(a, a->tab[k], 0, false);
	for (unsigned int k = 0; k < a->capt_n; k++)
		oa_style_unref(a->capt[k].style);
	for (int k = 0; k < a->nev; k++)
		if (a->ev[k].node != NULL)
			dom_node_unref(a->ev[k].node);
	free(a->ev);
	free(a->capt);
	free(a->tab);
	free(a);
	c->onyx_anim = NULL;
}

/* ---- the Web Animations API ---------------------------------------------------------------- */

static struct oa_effect *oa_by_id(struct onyx_anim *a, int id)
{
	if (a == NULL)
		return NULL;
	for (unsigned int k = 0; k < a->cap; k++)
		for (struct oa_rec *r = a->tab[k]; r != NULL; r = r->next)
			for (struct oa_effect *e = r->effects; e != NULL; e = e->next)
				if (e->id == id)
					return e;
	return NULL;
}

/* an element's style for scripts' keyframes: its record's base, else its box's */
static const css_computed_style *oa_base_of(struct onyx_anim *a, struct dom_node *n,
		struct oa_rec **rp)
{
	struct oa_rec *r = oa_find(a, n);
	struct box *b;

	*rp = r;
	if (r != NULL)
		return r->base;
	b = box_for_node(n);
	return b != NULL ? b->style : NULL;
}

static const css_computed_style *oa_parent_style(struct dom_node *n)
{
	struct dom_node *p = NULL;
	struct box *b = NULL;

	if (dom_node_get_parent_node(n, &p) != DOM_NO_ERR || p == NULL)
		return NULL;
	b = box_for_node(p);
	dom_node_unref(p);
	return b != NULL ? b->style : NULL;
}

/* exported function documented in html/onyx_anim.h */
int onyx_anim_create(struct html_content *c, struct dom_node *n, const char *const *css,
		const double *offsets, const char *const *easings, int nkf,
		const struct onyx_anim_timing *timing)
{
	struct onyx_anim *a = oa_get(c, true);
	const css_computed_style *base, *parent;
	uint8_t (*set)[CSS_N_PROPERTIES] = NULL;
	struct oa_effect *e;
	struct oa_rec *r;
	double now;
	int k;

	if (a == NULL || nkf < 1 || nkf > 256)
		return 0;
	base = oa_base_of(a, n, &r);
	if (base == NULL)
		return 0;	/* (not rendered) */
	parent = oa_parent_style(n);
	now = oa_clock();
	e = oa_new_effect(a, OA_SCRIPT, now);
	if (e == NULL)
		return 0;
	e->kf = calloc(nkf, sizeof(*e->kf));
	e->sheets = calloc(nkf, sizeof(*e->sheets));
	set = calloc(nkf, sizeof(*set));
	if (e->kf == NULL || e->sheets == NULL || set == NULL)
		goto fail;
	e->nsheets = nkf;
	for (k = 0; k < nkf; k++) {
		const char *t = css[k] != NULL ? css[k] : "";
		css_computed_style *s = NULL;
		const void *decls;
		e->sheets[k] = nscss_create_inline_style((const uint8_t *) t, strlen(t),
				c->encoding, nsurl_access(c->base_url),
				c->quirks != DOM_DOCUMENT_QUIRKS_MODE_NONE);
		decls = css_stylesheet_onyx_inline_decls(e->sheets[k]);
		if (css_computed_style_onyx_apply(base, parent, decls, &c->unit_len_ctx, &s,
				set[k]) != CSS_OK)
			goto fail;
		e->kf[k].offset = offsets[k];
		e->kf[k].style = s;
		e->nkf++;
		oa_parse_easing(easings != NULL && easings[k] != NULL ? easings[k] : "linear",
				easings != NULL && easings[k] != NULL ? strlen(easings[k]) : 6,
				&e->kf[k].easing);
	}
	/* (the offsets never decrease: the scripts' checked them) */
	if (!oa_make_tracks(e, set))
		goto fail;
	free(set);
	set = NULL;
	e->delay = timing->delay;
	e->end_delay = timing->end_delay;
	e->duration = timing->duration < 0 ? 0 : timing->duration;
	e->iterations = timing->iterations < 0 ? 1 : timing->iterations;
	e->iter_start = timing->iteration_start < 0 ? 0 : timing->iteration_start;
	e->direction = timing->direction;
	e->fill = timing->fill;
	e->rate = timing->rate;
	e->kf_easing.kind = OE_LINEAR;
	if (timing->easing != NULL)
		oa_parse_easing(timing->easing, strlen(timing->easing), &e->easing);
	if (e->rate < 0)
		e->start_time = now - oa_end_time(e) / e->rate;
	if (r == NULL) {
		r = oa_add(a, n);
		if (r == NULL)
			goto fail;
		r->base = css_computed_style_onyx_ref(base);
		r->cur = css_computed_style_onyx_ref(base);
	}
	oa_link(r, e);
	a->dirty = true;
	oa_schedule(a);
	return e->id;
fail:
	free(set);
	oa_effect_free(e);
	return 0;
}

static bool oa_finished(const struct oa_effect *e, double now)
{
	double t = oa_current(e, now);
	if (isnan(t))
		return false;
	return (e->rate > 0 && t >= oa_end_time(e)) || (e->rate < 0 && t <= 0);
}

static void oa_play(struct oa_effect *e, double now)
{
	double t = oa_current(e, now), end = oa_end_time(e);

	if (e->idle || isnan(t))
		t = e->rate < 0 ? end : 0;
	if (e->rate > 0 && (t < 0 || t >= end))
		t = 0;
	else if (e->rate < 0 && (t <= 0 || t > end))
		t = isinf(end) ? 0 : end;
	e->idle = false;
	e->paused = false;
	e->finish_sent = false;
	e->end_sent = false;
	e->start_sent = false;
	e->hold_time = NAN;
	if (e->rate != 0)
		e->start_time = now - t / e->rate;
	else
		e->hold_time = t;
}

/* exported function documented in html/onyx_anim.h */
bool onyx_anim_control(struct html_content *c, int id, enum onyx_anim_op op, double arg)
{
	struct onyx_anim *a = c->onyx_anim;
	struct oa_effect *e = oa_by_id(a, id);
	double now = oa_clock(), t;

	if (e == NULL)
		return false;
	t = oa_current(e, now);
	switch (op) {
	case ONYX_ANIM_PLAY:
		if (e->paused && !isnan(e->hold_time) && !oa_finished(e, now)) {
			e->paused = false;
			e->start_time = e->rate != 0 ? now - e->hold_time / e->rate : now;
			e->hold_time = NAN;
			e->idle = false;
		} else {
			oa_play(e, now);
		}
		break;
	case ONYX_ANIM_PAUSE:
		if (e->idle || isnan(t))
			t = e->rate < 0 ? oa_end_time(e) : 0;
		e->idle = false;
		e->hold_time = t;
		e->paused = true;
		break;
	case ONYX_ANIM_CANCEL:
		if (!e->idle) {
			oa_cancel_events(a, e, now);
			e->idle = true;
			e->paused = false;
			e->hold_time = NAN;
			e->start_time = NAN;
			if (e->kind == OA_TRANSITION) {
				struct oa_rec *r = e->rec;
				oa_unlink(r, e);
				oa_effect_free(e);
			}
		}
		break;
	case ONYX_ANIM_FINISH: {
		double end = oa_end_time(e);
		if (e->rate > 0 && isinf(end))
			return false;
		e->idle = false;
		e->paused = false;
		e->hold_time = e->rate < 0 ? 0 : end;
		e->start_time = e->rate != 0 ? now - e->hold_time / e->rate : now;
		break;
	}
	case ONYX_ANIM_REVERSE:
		e->rate = -e->rate;
		if (isnan(t) || e->idle)
			oa_play(e, now);
		else {
			e->finish_sent = false;
			e->end_sent = false;
			e->idle = false;
			e->paused = false;
			e->hold_time = NAN;
			if (t > oa_end_time(e))
				t = oa_end_time(e);
			if (t < 0)
				t = 0;
			e->start_time = now - t / e->rate;
			if (e->rate > 0 && t >= oa_end_time(e))
				oa_play(e, now);
			else if (e->rate < 0 && t <= 0)
				oa_play(e, now);
		}
		break;
	case ONYX_ANIM_SET_TIME:
		e->finish_sent = false;
		if (!isnan(e->hold_time) || e->paused || e->idle || e->rate == 0) {
			e->hold_time = arg;
			e->idle = false;
			if (!e->paused && e->rate != 0) {
				e->start_time = now - arg / e->rate;
				e->hold_time = NAN;
			}
		} else {
			e->start_time = now - arg / e->rate;
		}
		break;
	case ONYX_ANIM_SET_RATE:
		if (!isnan(t) && isnan(e->hold_time) && arg != 0)
			e->start_time = now - t / arg;
		else if (!isnan(t) && isnan(e->hold_time))
			e->hold_time = t;
		e->rate = arg;
		e->finish_sent = false;
		break;
	case ONYX_ANIM_SET_START:
		e->start_time = arg;
		e->hold_time = NAN;
		e->paused = false;
		e->idle = false;
		e->finish_sent = false;
		break;
	case ONYX_ANIM_COMMIT:
		break;
	}
	a->dirty = true;
	oa_schedule(a);
	return true;
}

/* exported function documented in html/onyx_anim.h */
bool onyx_anim_info(struct html_content *c, int id, struct onyx_anim_info *out)
{
	struct onyx_anim *a = c->onyx_anim;
	struct oa_effect *e = oa_by_id(a, id);
	struct oa_timing tm;
	double now = a != NULL && a->in_tick ? a->now : oa_clock();

	if (e == NULL)
		return false;
	memset(out, 0, sizeof(*out));
	out->current_time = oa_current(e, now);
	out->start_time = isnan(e->hold_time) ? e->start_time : NAN;
	out->rate = e->rate;
	out->end_time = oa_end_time(e);
	oa_compute(e, out->current_time, &tm);
	out->progress = tm.progress;
	out->iteration = tm.iteration;
	out->kind = e->kind;
	out->play_state = e->idle ? "idle" : e->paused ? "paused" :
			oa_finished(e, now) ? "finished" : "running";
	out->name = oa_effect_name(e);
	out->node = e->rec->node;
	out->delay = e->delay;
	out->duration = e->duration;
	out->iterations = e->iterations;
	out->end_delay = e->end_delay;
	out->direction = e->direction;
	out->fill = e->fill;
	return true;
}

/* exported function documented in html/onyx_anim.h */
int onyx_anim_list(struct html_content *c, struct dom_node *n, int *ids, int max)
{
	struct onyx_anim *a = c->onyx_anim;
	int count = 0;

	if (a == NULL)
		return 0;
	for (unsigned int k = 0; k < a->cap; k++)
		for (struct oa_rec *r = a->tab[k]; r != NULL; r = r->next) {
			if (n != NULL && r->node != n)
				continue;
			for (struct oa_effect *e = r->effects; e != NULL && count < max;
			     e = e->next)
				if (!e->idle)
					ids[count++] = e->id;
		}
	return count;
}

/* ---- getComputedStyle's transform ---------------------------------------------------------- */

/* exported function documented in html/onyx_anim.h */
void onyx_anim_transform_text(const struct css_computed_style *style,
		const struct html_content *c, float w, float h, char *buf, int len)
{
	lwc_string *t = NULL;
	const char *p;
	float m[6] = { 1, 0, 0, 1, 0, 0 };

	if (css_computed_transform(style, &t) != CSS_ONYX_TEXT_SET || t == NULL) {
		snprintf(buf, len, "none");
		return;
	}
	for (p = lwc_string_data(t); p != NULL && *p != '\0'; ) {
		float v[6] = { 0 }, f[6] = { 1, 0, 0, 1, 0, 0 }, r[6];
		char unit[6][4];
		int n = 0;
		const char *q = strchr(p, '(');
		if (q == NULL)
			break;
		memset(unit, 0, sizeof(unit));
		for (q++; *q != ')' && *q != '\0' && n < 6; n++) {
			char *e;
			int ul = 0;
			v[n] = strtof(q, &e);
			if (e == q)
				break;
			q = e;
			while (*q != ',' && *q != ')' && *q != '\0' && ul < 3)
				unit[n][ul++] = *q++;
			if (*q == ',')
				q++;
		}
		for (int i = 0; i < n; i++) {
			/* the lengths: px (% of the box, em / rem of the font) */
			float scale = 1;
			if (strcmp(unit[i], "%") == 0)
				scale = (i == 0 ? w : h) / 100;
			else if (strcmp(unit[i], "em") == 0 || strcmp(unit[i], "rem") == 0)
				scale = 16;
			else if (strcmp(unit[i], "vw") == 0)
				scale = c->base.available_width / 100.0f;
			else if (strcmp(unit[i], "vh") == 0)
				scale = c->base.available_height / 100.0f;
			v[i] *= scale;
		}
		if (strncmp(p, "translate(", 10) == 0) {
			f[4] = v[0]; f[5] = v[1];
		} else if (strncmp(p, "scale(", 6) == 0) {
			f[0] = v[0]; f[3] = n > 1 ? v[1] : v[0];
		} else if (strncmp(p, "rotate(", 7) == 0) {
			float an = v[0] * (float) M_PI / 180;
			f[0] = cosf(an); f[1] = sinf(an); f[2] = -sinf(an); f[3] = cosf(an);
		} else if (strncmp(p, "skew(", 5) == 0) {
			f[2] = tanf(v[0] * (float) M_PI / 180);
			f[1] = tanf(v[1] * (float) M_PI / 180);
		} else if (strncmp(p, "matrix(", 7) == 0 && n == 6) {
			memcpy(f, v, sizeof(f));
		}
		r[0] = m[0] * f[0] + m[2] * f[1];
		r[1] = m[1] * f[0] + m[3] * f[1];
		r[2] = m[0] * f[2] + m[2] * f[3];
		r[3] = m[1] * f[2] + m[3] * f[3];
		r[4] = m[0] * f[4] + m[2] * f[5] + m[4];
		r[5] = m[1] * f[4] + m[3] * f[5] + m[5];
		memcpy(m, r, sizeof(m));
		p = strchr(q, ' ');
		if (p != NULL)
			p++;
	}
	for (int i = 0; i < 6; i++)
		if (fabsf(m[i]) < 1e-6f)
			m[i] = 0;
	snprintf(buf, len, "matrix(%g, %g, %g, %g, %g, %g)", m[0], m[1], m[2], m[3], m[4], m[5]);
}
