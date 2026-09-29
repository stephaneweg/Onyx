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
 * Onyx: HTML layout implementation: display: grid (CSS Grid Layout 1, the usual part).
 *
 * A grid container is a flex-like box: BOX_FLEX / BOX_INLINE_FLEX whose display is grid /
 * inline-grid (box_construct.c), its children blockified into its items (a run of text
 * shares an anonymous block). layout_flex() hands it to layout_grid(), layout_minmax_block()
 * to layout_minmax_grid() for its intrinsic widths.
 *
 * libcss keeps the grid properties as canonical texts (libcss onyx_css3b.c):
 *   track list  space separated: <len> (px % em rem vw vh fr) auto min-content max-content
 *               minmax(a,b) fit-content(len) repeat(<n>|auto-fill|auto-fit,<tracks>) [names]
 *   areas       the rows' strings joined by '/': "a a b/c c b"
 *   grid line   auto | <n> | span <n> | <name> | span <name> | <n> <name>
 *
 * What is done: the explicit grid (templates, repeat() -- auto-fill / auto-fit too --,
 * named lines, areas and their implicit -start / -end names) and the implicit one
 * (grid-auto-rows / -columns); placement by line numbers (negative ones too), spans, names
 * and areas, auto-placement in rows or columns, sparse or dense, in `order` order; track
 * sizing (lengths, percentages, auto, min- / max-content, minmax(), fit-content(), fr --
 * the intrinsic ones from the items' contributions, items spanning several tracks spread
 * over them), free space to auto tracks (normal = stretch), gaps; items aligned in their
 * areas (justify-items / -self, align-items / -self, auto margins); the grid's intrinsic
 * widths. Not done: subgrid, masonry, baseline alignment (as start), the fr of an
 * indefinite height (as auto).
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "utils/log.h"
#include "utils/utils.h"

#include "html/box.h"
#include "html/html.h"
#include "html/private.h"
#include "html/box_inspect.h"
#include "html/layout_internal.h"

#define GRID_MAX_TRACKS 512		/* explicit + implicit, per axis */
#define GRID_MAX_NAMES 128		/* named lines, per axis */
#define GRID_NAME_LEN 48
#define GRID_BIG (INT_MAX / 4)

/* ---- track sizing functions ----------------------------------------------------------- */

enum grid_kind {
	GK_FIXED,		/* a length, in px */
	GK_PCT,			/* a percentage of the grid's size (unknown: as auto) */
	GK_FR,			/* a flexible size */
	GK_AUTO,
	GK_MIN_CONTENT,
	GK_MAX_CONTENT,
	GK_FIT			/* fit-content(v px) (as a maximum) */
};

struct grid_breadth {
	enum grid_kind kind;
	float v;
};

struct grid_track_def {
	struct grid_breadth min, max;
};

struct grid_name {
	char name[GRID_NAME_LEN];
	int line;		/* 0-based line index in the explicit grid */
};

/** A track list, repeat() expanded (an auto repeat: once, then as many times as fit). */
struct grid_tracks {
	struct grid_track_def t[GRID_MAX_TRACKS];
	int n;
	struct grid_name names[GRID_MAX_NAMES];
	int nnames;
	int auto_at;		/* the auto repeat's first track, -1 none */
	int auto_len;		/* its track count */
	bool auto_fit;		/* (auto-fit: its empty tracks collapse) */
};

/** The grid being laid out. */
struct grid_ctx {
	struct box *grid;
	const css_computed_style *style;
	const css_unit_ctx *unit_len_ctx;
	html_content *content;

	struct grid_tracks cols, rows;	/* the explicit grid */
	struct grid_tracks acols, arows;	/* grid-auto-columns / -rows */
	int ecols, erows;		/* explicit track counts (areas included) */
	int col_off, row_off;		/* implicit tracks before the explicit grid */

	int col_gap, row_gap;
	bool column_flow, dense;
};

/** An item and its area. */
struct grid_item {
	struct box *box;
	int order;
	int index;		/* document order */
	int rs, re, cs, ce;	/* its area: lines, 0-based, end exclusive (after offsets) */
	bool placed;
	int width, height;	/* its outer (margin box) size once laid out */
};

/* ---- parsing the canonical texts ------------------------------------------------------- */

static const char *grid_skip(const char *p)
{
	while (*p == ' ')
		p++;
	return p;
}

static bool grid_word(const char *p, const char *w)
{
	size_t n = strlen(w);
	return strncmp(p, w, n) == 0 &&
		!((p[n] >= 'a' && p[n] <= 'z') || (p[n] >= '0' && p[n] <= '9') ||
		  p[n] == '-');
}

/** A length in the grid container's font / viewport, in px. */
static int grid_len_px(const struct grid_ctx *g, float v, css_unit unit)
{
	return FIXTOINT(css_unit_len2device_px(g->style, g->unit_len_ctx,
			FLTTOFIX(v), unit));
}

/** A breadth: <len> | <pct> | <fr> | auto | min-content | max-content. */
static const char *grid_parse_breadth(const struct grid_ctx *g, const char *p,
		struct grid_breadth *b)
{
	char *e;
	float v;

	p = grid_skip(p);
	if (grid_word(p, "auto")) {
		b->kind = GK_AUTO;
		b->v = 0;
		return p + 4;
	}
	if (grid_word(p, "min-content")) {
		b->kind = GK_MIN_CONTENT;
		b->v = 0;
		return p + 11;
	}
	if (grid_word(p, "max-content")) {
		b->kind = GK_MAX_CONTENT;
		b->v = 0;
		return p + 11;
	}
	v = strtof(p, &e);
	if (e == p)
		return NULL;
	p = e;
	if (strncmp(p, "px", 2) == 0) {
		b->kind = GK_FIXED;
		b->v = v;
		return p + 2;
	}
	if (strncmp(p, "fr", 2) == 0) {
		b->kind = GK_FR;
		b->v = v;
		return p + 2;
	}
	if (*p == '%') {
		b->kind = GK_PCT;
		b->v = v;
		return p + 1;
	}
	if (strncmp(p, "rem", 3) == 0) {
		b->kind = GK_FIXED;
		b->v = grid_len_px(g, v, CSS_UNIT_REM);
		return p + 3;
	}
	if (strncmp(p, "em", 2) == 0) {
		b->kind = GK_FIXED;
		b->v = grid_len_px(g, v, CSS_UNIT_EM);
		return p + 2;
	}
	if (strncmp(p, "vw", 2) == 0) {
		b->kind = GK_FIXED;
		b->v = grid_len_px(g, v, CSS_UNIT_VW);
		return p + 2;
	}
	if (strncmp(p, "vh", 2) == 0) {
		b->kind = GK_FIXED;
		b->v = grid_len_px(g, v, CSS_UNIT_VH);
		return p + 2;
	}
	/* a plain number (0) */
	b->kind = GK_FIXED;
	b->v = v;
	return p;
}

/** A track size: <breadth> | minmax(a,b) | fit-content(len). */
static const char *grid_parse_track(const struct grid_ctx *g, const char *p,
		struct grid_track_def *t)
{
	p = grid_skip(p);
	if (strncmp(p, "minmax(", 7) == 0) {
		p = grid_parse_breadth(g, p + 7, &t->min);
		if (p == NULL || *p != ',')
			return NULL;
		p = grid_parse_breadth(g, p + 1, &t->max);
		if (p == NULL || *p != ')')
			return NULL;
		if (t->min.kind == GK_FR)	/* (not valid as a minimum) */
			t->min.kind = GK_AUTO;
		return p + 1;
	}
	if (strncmp(p, "fit-content(", 12) == 0) {
		struct grid_breadth b;
		p = grid_parse_breadth(g, p + 12, &b);
		if (p == NULL || *p != ')')
			return NULL;
		t->min.kind = GK_AUTO;
		t->min.v = 0;
		t->max.kind = GK_FIT;
		t->max.v = b.kind == GK_FIXED ? b.v : 0;
		return p + 1;
	}
	p = grid_parse_breadth(g, p, &t->max);
	if (p == NULL)
		return NULL;
	if (t->max.kind == GK_FR) {
		t->min.kind = GK_AUTO;	/* 1fr = minmax(auto, 1fr) */
		t->min.v = 0;
	} else {
		t->min = t->max;
	}
	return p;
}

static void grid_add_name(struct grid_tracks *tl, const char *name, size_t len,
		int line)
{
	struct grid_name *nm;

	if (tl->nnames >= GRID_MAX_NAMES || len == 0)
		return;
	if (len >= GRID_NAME_LEN)
		len = GRID_NAME_LEN - 1;
	nm = &tl->names[tl->nnames++];
	memcpy(nm->name, name, len);
	nm->name[len] = '\0';
	nm->line = line;
}

/** "[a b]": the names of line `line`. */
static const char *grid_parse_names(struct grid_tracks *tl, const char *p, int line)
{
	p++;					/* '[' */
	for (;;) {
		const char *s;
		p = grid_skip(p);
		if (*p == ']')
			return p + 1;
		if (*p == '\0')
			return NULL;
		s = p;
		while (*p != '\0' && *p != ' ' && *p != ']')
			p++;
		grid_add_name(tl, s, p - s, line);
	}
}

/** A track list, up to its end or `stop`: sizes, names, repeat(). */
static const char *grid_parse_list(const struct grid_ctx *g, const char *p,
		char stop, struct grid_tracks *tl, bool in_repeat)
{
	for (;;) {
		p = grid_skip(p);
		if (*p == '\0' || *p == stop)
			return p;
		if (*p == '[') {
			p = grid_parse_names(tl, p, tl->n);
		} else if (!in_repeat && strncmp(p, "repeat(", 7) == 0) {
			/* the repeated tracks, parsed once into a list of their own */
			static struct grid_tracks sub;
			int count = 0;
			bool is_auto = false, fit = false;

			p = grid_skip(p + 7);
			if (grid_word(p, "auto-fill")) {
				is_auto = true;
				p += 9;
			} else if (grid_word(p, "auto-fit")) {
				is_auto = fit = true;
				p += 8;
			} else {
				char *e;
				count = (int) strtol(p, &e, 10);
				if (e == p)
					return NULL;
				p = e;
			}
			p = grid_skip(p);
			if (*p != ',')
				return NULL;
			memset(&sub, 0, sizeof sub);
			sub.auto_at = -1;
			p = grid_parse_list(g, p + 1, ')', &sub, true);
			if (p == NULL || *p != ')' || sub.n == 0)
				return NULL;
			p++;
			if (is_auto) {
				if (tl->auto_at >= 0)
					return NULL;	/* (one auto repeat only) */
				count = 1;		/* (expanded later) */
				tl->auto_at = tl->n;
				tl->auto_len = sub.n;
				tl->auto_fit = fit;
			}
			for (int r = 0; r < count; r++) {
				int base = tl->n;
				for (int i = 0; i < sub.n; i++) {
					if (tl->n >= GRID_MAX_TRACKS)
						break;
					tl->t[tl->n++] = sub.t[i];
				}
				for (int k = 0; k < sub.nnames; k++)
					grid_add_name(tl, sub.names[k].name,
						strlen(sub.names[k].name),
						base + sub.names[k].line);
			}
		} else {
			if (tl->n >= GRID_MAX_TRACKS)
				return NULL;
			p = grid_parse_track(g, p, &tl->t[tl->n]);
			if (p != NULL)
				tl->n++;
		}
		if (p == NULL)
			return NULL;
	}
}

static void grid_tracks_init(struct grid_tracks *tl)
{
	tl->n = 0;
	tl->nnames = 0;
	tl->auto_at = -1;
	tl->auto_len = 0;
	tl->auto_fit = false;
}

/** A template (or auto tracks) text into `tl`; empty for none / invalid. */
static void grid_parse_tracks(const struct grid_ctx *g, lwc_string *text,
		struct grid_tracks *tl)
{
	grid_tracks_init(tl);
	if (text == NULL)
		return;
	if (grid_parse_list(g, lwc_string_data(text), 0, tl, false) == NULL) {
		NSLOG(layout, INFO, "grid: bad track list '%s'",
				lwc_string_data(text));
		grid_tracks_init(tl);
	}
}

/**
 * The auto repeat's count: as many repetitions as fit in `avail` px (at least 1), each
 * track counted at its fixed size (its maximum if fixed, else its minimum).
 */
static void grid_expand_auto(const struct grid_ctx *g, struct grid_tracks *tl,
		int avail, int gap)
{
	int one = 0, fixed = 0, count, i;
	struct grid_track_def rep[GRID_MAX_TRACKS];
	struct grid_track_def after[GRID_MAX_TRACKS];
	int nafter;

	(void) g;
	if (tl->auto_at < 0)
		return;
	for (i = 0; i < tl->auto_len; i++) {
		const struct grid_track_def *t = &tl->t[tl->auto_at + i];
		int sz = 0;
		if (t->max.kind == GK_FIXED)
			sz = (int) t->max.v;
		else if (t->max.kind == GK_PCT && avail > 0)
			sz = (int) (t->max.v * avail / 100);
		else if (t->min.kind == GK_FIXED)
			sz = (int) t->min.v;
		else if (t->min.kind == GK_PCT && avail > 0)
			sz = (int) (t->min.v * avail / 100);
		one += sz + gap;
		rep[i] = *t;
	}
	for (i = 0; i < tl->n; i++) {
		if (i >= tl->auto_at && i < tl->auto_at + tl->auto_len)
			continue;
		const struct grid_track_def *t = &tl->t[i];
		if (t->max.kind == GK_FIXED)
			fixed += (int) t->max.v + gap;
		else if (t->min.kind == GK_FIXED)
			fixed += (int) t->min.v + gap;
	}
	count = 1;
	if (avail > 0 && one > 0)
		count = (avail + gap - fixed) / one;
	if (count < 1)
		count = 1;
	if (tl->n + (count - 1) * tl->auto_len > GRID_MAX_TRACKS)
		count = (GRID_MAX_TRACKS - tl->n) / tl->auto_len + 1;
	if (count == 1)
		return;

	/* the tracks after the repeat move right; so do their names */
	nafter = tl->n - (tl->auto_at + tl->auto_len);
	memcpy(after, &tl->t[tl->auto_at + tl->auto_len], nafter * sizeof after[0]);
	for (int r = 1; r < count; r++)
		for (i = 0; i < tl->auto_len; i++)
			tl->t[tl->auto_at + r * tl->auto_len + i] = rep[i];
	memcpy(&tl->t[tl->auto_at + count * tl->auto_len], after,
			nafter * sizeof after[0]);
	for (i = 0; i < tl->nnames; i++) {
		if (tl->names[i].line > tl->auto_at + tl->auto_len - 1)
			tl->names[i].line += (count - 1) * tl->auto_len;
	}
	tl->n += (count - 1) * tl->auto_len;
	tl->auto_len *= count;
}

/**
 * grid-template-areas ("a a b/c c b") into the implicit line names: for each area,
 * <name>-start / <name>-end on both axes. Returns the areas' row / column counts.
 */
static void grid_parse_areas(struct grid_ctx *g, lwc_string *text)
{
	const char *s;
	char cells[64][32][GRID_NAME_LEN];
	int nrows = 0, ncols = 0;

	if (text == NULL)
		return;
	s = lwc_string_data(text);
	while (*s != '\0' && nrows < 64) {
		int c = 0;
		while (*s != '\0' && *s != '/') {
			const char *w;
			size_t len;
			s = grid_skip(s);
			if (*s == '/' || *s == '\0')
				break;
			w = s;
			while (*s != '\0' && *s != ' ' && *s != '/')
				s++;
			len = s - w;
			if (len >= GRID_NAME_LEN)
				len = GRID_NAME_LEN - 1;
			if (c < 32) {
				memcpy(cells[nrows][c], w, len);
				cells[nrows][c][len] = '\0';
				c++;
			}
		}
		if (c > ncols)
			ncols = c;
		for (int k = c; k < 32; k++)
			cells[nrows][k][0] = '\0';
		nrows++;
		if (*s == '/')
			s++;
	}

	/* each named area: its bounding rows and columns */
	for (int r = 0; r < nrows; r++) {
		for (int c = 0; c < ncols; c++) {
			const char *nm = cells[r][c];
			int r0 = r, r1 = r, c0 = c, c1 = c;
			bool seen = false;
			char buf[GRID_NAME_LEN + 8];

			if (nm[0] == '\0' || nm[0] == '.')
				continue;
			/* only at its first cell */
			for (int rr = 0; rr <= r && !seen; rr++)
				for (int cc = 0; cc < ncols; cc++) {
					if (rr == r && cc >= c)
						break;
					if (strcmp(cells[rr][cc], nm) == 0) {
						seen = true;
						break;
					}
				}
			if (seen)
				continue;
			for (int rr = r; rr < nrows; rr++)
				for (int cc = 0; cc < ncols; cc++)
					if (strcmp(cells[rr][cc], nm) == 0) {
						if (rr > r1) r1 = rr;
						if (cc < c0) c0 = cc;
						if (cc > c1) c1 = cc;
					}
			snprintf(buf, sizeof buf, "%s-start", nm);
			grid_add_name(&g->cols, buf, strlen(buf), c0);
			grid_add_name(&g->rows, buf, strlen(buf), r0);
			snprintf(buf, sizeof buf, "%s-end", nm);
			grid_add_name(&g->cols, buf, strlen(buf), c1 + 1);
			grid_add_name(&g->rows, buf, strlen(buf), r1 + 1);
		}
	}
	if (ncols > g->ecols)
		g->ecols = ncols;
	if (nrows > g->erows)
		g->erows = nrows;
}

/* ---- placement ------------------------------------------------------------------------- */

/** A grid line value: auto | <n> | span <n> | <name> | span <name> | <n> <name>. */
struct grid_line {
	bool is_auto;
	bool span;
	int n;				/* the number (0: none) */
	char name[GRID_NAME_LEN];	/* "" none */
};

static void grid_parse_line(lwc_string *text, struct grid_line *l)
{
	const char *p;

	memset(l, 0, sizeof *l);
	l->is_auto = true;
	if (text == NULL)
		return;
	p = grid_skip(lwc_string_data(text));
	if (*p == '\0' || grid_word(p, "auto"))
		return;
	l->is_auto = false;
	if (grid_word(p, "span")) {
		l->span = true;
		p = grid_skip(p + 4);
	}
	while (*p != '\0') {
		if ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+') {
			char *e;
			long v = strtol(p, &e, 10);
			if (e != p) {
				l->n = (int) v;
				p = grid_skip(e);
				continue;
			}
		}
		{
			const char *w = p;
			size_t len;
			while (*p != '\0' && *p != ' ')
				p++;
			len = p - w;
			if (len >= GRID_NAME_LEN)
				len = GRID_NAME_LEN - 1;
			memcpy(l->name, w, len);
			l->name[len] = '\0';
			p = grid_skip(p);
		}
	}
	if (l->span && l->n <= 0 && l->name[0] == '\0')
		l->n = 1;
	if (l->span && l->n <= 0)
		l->n = 1;
}

/** The nth (1-based; negative from the end) line named `name`, or -1. */
static int grid_find_name(const struct grid_tracks *tl, const char *name, int nth)
{
	int found[GRID_MAX_NAMES], nf = 0;

	for (int i = 0; i < tl->nnames; i++)
		if (strcmp(tl->names[i].name, name) == 0)
			found[nf++] = tl->names[i].line;
	if (nf == 0)
		return -1;
	/* (in line order) */
	for (int i = 1; i < nf; i++)
		for (int j = i; j > 0 && found[j - 1] > found[j]; j--) {
			int t = found[j];
			found[j] = found[j - 1];
			found[j - 1] = t;
		}
	if (nth == 0)
		nth = 1;
	if (nth > 0)
		return nth <= nf ? found[nth - 1] : -1;
	return -nth <= nf ? found[nf + nth] : -1;
}

/**
 * A definite line's index (0-based, in the explicit grid's numbering: may be negative or
 * beyond it), or INT_MIN when the line is auto or a span.
 */
static int grid_resolve_line(const struct grid_tracks *tl, int explicit_n,
		const struct grid_line *l, bool start)
{
	if (l->is_auto || l->span)
		return INT_MIN;
	if (l->name[0] != '\0') {
		char buf[GRID_NAME_LEN + 8];
		int line = -1;

		if (l->n == 0) {
			/* an area's name: its -start / -end line first */
			snprintf(buf, sizeof buf, "%s-%s", l->name,
					start ? "start" : "end");
			line = grid_find_name(tl, buf, 1);
		}
		if (line < 0)
			line = grid_find_name(tl, l->name, l->n);
		if (line >= 0)
			return line;
		/* not there: the implicit lines are all assumed to have the name */
		return explicit_n + (l->n > 0 ? l->n : 1);
	}
	if (l->n > 0)
		return l->n - 1;
	if (l->n < 0)
		return explicit_n + 1 + l->n;
	return INT_MIN;
}

/**
 * An item's area on one axis from its start / end lines: *s, *e (lines, end exclusive)
 * when definite (return true), else its span in *span (return false).
 */
static bool grid_resolve_axis(const struct grid_tracks *tl, int explicit_n,
		lwc_string *start_text, lwc_string *end_text,
		int *s, int *e, int *span)
{
	struct grid_line ls, le;
	int a, b;

	grid_parse_line(start_text, &ls);
	grid_parse_line(end_text, &le);
	a = grid_resolve_line(tl, explicit_n, &ls, true);
	b = grid_resolve_line(tl, explicit_n, &le, false);

	if (a != INT_MIN && b != INT_MIN) {
		if (b < a) {
			int t = a;
			a = b;
			b = t;
		}
		if (b == a)
			b = a + 1;
		*s = a;
		*e = b;
		return true;
	}
	if (a != INT_MIN) {
		*s = a;
		*e = a + (le.span ? le.n : 1);
		return true;
	}
	if (b != INT_MIN) {
		*e = b;
		*s = b - (ls.span ? ls.n : 1);
		return true;
	}
	*span = ls.span ? ls.n : le.span ? le.n : 1;
	if (*span < 1)
		*span = 1;
	return false;
}

/** The occupied cells: `nmajor` rows of `nminor` cells (grows in the major axis). */
struct grid_occ {
	unsigned char *cells;
	int nmajor, nminor, cap;
};

static bool grid_occ_grow(struct grid_occ *o, int nmajor)
{
	if (nmajor <= o->nmajor)
		return true;
	if (nmajor > o->cap) {
		int cap = o->cap ? o->cap * 2 : 16;
		unsigned char *c;
		while (cap < nmajor)
			cap *= 2;
		c = realloc(o->cells, (size_t) cap * o->nminor);
		if (c == NULL)
			return false;
		memset(c + (size_t) o->cap * o->nminor, 0,
				(size_t) (cap - o->cap) * o->nminor);
		o->cells = c;
		o->cap = cap;
	}
	o->nmajor = nmajor;
	return true;
}

static bool grid_occ_free(struct grid_occ *o, int m0, int m1, int n0, int n1)
{
	if (n0 < 0 || n1 > o->nminor)
		return false;
	for (int m = m0; m < m1; m++) {
		if (m >= o->nmajor)
			return true;	/* (beyond: free) */
		for (int n = n0; n < n1; n++)
			if (o->cells[(size_t) m * o->nminor + n])
				return false;
	}
	return true;
}

static bool grid_occ_mark(struct grid_occ *o, int m0, int m1, int n0, int n1)
{
	if (!grid_occ_grow(o, m1))
		return false;
	for (int m = m0; m < m1; m++)
		for (int n = n0; n < n1 && n < o->nminor; n++)
			if (n >= 0)
				o->cells[(size_t) m * o->nminor + n] = 1;
	return true;
}

static int grid_item_cmp(const void *a, const void *b)
{
	const struct grid_item *x = a, *y = b;
	if (x->order != y->order)
		return x->order < y->order ? -1 : 1;
	return x->index - y->index;
}

/**
 * Place the items (CSS Grid 8.5). On return the items' areas are set, 0-based with the
 * implicit tracks before the explicit grid counted in; g->col_off / row_off say how many.
 * Returns the grid's row and column counts in *nrows / *ncols.
 */
static bool grid_place(struct grid_ctx *g, struct grid_item *items, int nitems,
		int *nrows, int *ncols)
{
	struct grid_occ occ = { NULL, 0, 0, 0 };
	/* (major: the auto-flow's axis -- rows for grid-auto-flow: row) */
	int min_r = 0, min_c = 0, max_r = g->erows, max_c = g->ecols;
	int *rspan, *cspan;
	bool *rdef, *cdef;
	int nminor, nmajor;
	bool ok = true;

	rspan = calloc(nitems ? nitems : 1, sizeof *rspan);
	cspan = calloc(nitems ? nitems : 1, sizeof *cspan);
	rdef = calloc(nitems ? nitems : 1, sizeof *rdef);
	cdef = calloc(nitems ? nitems : 1, sizeof *cdef);
	if (rspan == NULL || cspan == NULL || rdef == NULL || cdef == NULL) {
		ok = false;
		goto done;
	}

	/* the lines each item names */
	for (int i = 0; i < nitems; i++) {
		struct grid_item *it = &items[i];
		const css_computed_style *st = it->box->style;
		lwc_string *rs = NULL, *re = NULL, *cs = NULL, *ce = NULL;

		it->placed = false;
		if (st != NULL) {
			css_computed_grid_row_start(st, &rs);
			css_computed_grid_row_end(st, &re);
			css_computed_grid_column_start(st, &cs);
			css_computed_grid_column_end(st, &ce);
		}
		rdef[i] = grid_resolve_axis(&g->rows, g->erows, rs, re,
				&it->rs, &it->re, &rspan[i]);
		cdef[i] = grid_resolve_axis(&g->cols, g->ecols, cs, ce,
				&it->cs, &it->ce, &cspan[i]);
		if (rdef[i]) {
			rspan[i] = it->re - it->rs;
			if (it->rs < min_r) min_r = it->rs;
			if (it->re > max_r) max_r = it->re;
		}
		if (cdef[i]) {
			cspan[i] = it->ce - it->cs;
			if (it->cs < min_c) min_c = it->cs;
			if (it->ce > max_c) max_c = it->ce;
		}
		if (!g->column_flow && !cdef[i] && cspan[i] > max_c)
			max_c = cspan[i];
		if (g->column_flow && !rdef[i] && rspan[i] > max_r)
			max_r = rspan[i];
	}

	/* the implicit tracks before the explicit grid: shift everything past them */
	g->row_off = -min_r;
	g->col_off = -min_c;
	for (int i = 0; i < nitems; i++) {
		if (rdef[i]) {
			items[i].rs -= min_r;
			items[i].re -= min_r;
		}
		if (cdef[i]) {
			items[i].cs -= min_c;
			items[i].ce -= min_c;
		}
	}
	max_r -= min_r;
	max_c -= min_c;
	if (max_r < 1 && g->column_flow)
		max_r = 1;
	if (max_c < 1 && !g->column_flow)
		max_c = 1;

	/* the minor axis' size is fixed now (the explicit grid, the definite items, the
	 * widest span); the major one grows */
	nminor = g->column_flow ? max_r : max_c;
	nmajor = g->column_flow ? max_c : max_r;
	occ.nminor = nminor;
	if (!grid_occ_grow(&occ, nmajor > 0 ? nmajor : 1)) {
		ok = false;
		goto done;
	}

#define MAJ0(it) (g->column_flow ? (it)->cs : (it)->rs)
#define MAJ1(it) (g->column_flow ? (it)->ce : (it)->re)
#define MIN0(it) (g->column_flow ? (it)->rs : (it)->cs)
#define MIN1(it) (g->column_flow ? (it)->re : (it)->ce)

	/* 1. the items with both positions */
	for (int i = 0; i < nitems; i++) {
		struct grid_item *it = &items[i];
		if (rdef[i] && cdef[i]) {
			if (!grid_occ_mark(&occ, MAJ0(it), MAJ1(it),
					MIN0(it), MIN1(it))) {
				ok = false;
				goto done;
			}
			it->placed = true;
		}
	}

	/* 2. the items with a definite major position (a row, for row flow) */
	{
		int *cursor = calloc(occ.nmajor + 64, sizeof *cursor);
		int ncursor = occ.nmajor + 64;
		for (int i = 0; i < nitems && cursor != NULL; i++) {
			struct grid_item *it = &items[i];
			bool maj_def = g->column_flow ? cdef[i] : rdef[i];
			int span = g->column_flow ? rspan[i] : cspan[i];
			int m0, m1, n;

			if (it->placed || !maj_def)
				continue;
			m0 = g->column_flow ? it->cs : it->rs;
			m1 = g->column_flow ? it->ce : it->re;
			n = (!g->dense && m0 < ncursor) ? cursor[m0] : 0;
			while (!grid_occ_free(&occ, m0, m1, n, n + span) &&
					n + span <= nminor)
				n++;
			if (n + span > nminor)
				n = 0;		/* (no room: overlap at the start) */
			if (g->column_flow) {
				it->rs = n;
				it->re = n + span;
			} else {
				it->cs = n;
				it->ce = n + span;
			}
			if (m0 < ncursor)
				cursor[m0] = n + span;
			if (!grid_occ_mark(&occ, m0, m1, n, n + span)) {
				ok = false;
				break;
			}
			it->placed = true;
		}
		free(cursor);
		if (!ok)
			goto done;
	}

	/* 3. the rest: the auto-placement cursor */
	{
		int cm = 0, cn = 0;		/* the cursor: major, minor */
		for (int i = 0; i < nitems; i++) {
			struct grid_item *it = &items[i];
			bool min_def = g->column_flow ? rdef[i] : cdef[i];
			int mspan = g->column_flow ? cspan[i] : rspan[i];
			int nspan = g->column_flow ? rspan[i] : cspan[i];

			if (it->placed)
				continue;
			if (g->dense) {
				cm = 0;
				cn = 0;
			}
			if (min_def) {
				/* a definite minor position: the next major where it fits */
				int n0 = MIN0(it), n1 = MIN1(it);
				if (!g->dense && n0 < cn)
					cm++;
				while (!grid_occ_free(&occ, cm, cm + mspan, n0, n1))
					cm++;
				cn = n0;
				if (g->column_flow) {
					it->cs = cm;
					it->ce = cm + mspan;
				} else {
					it->rs = cm;
					it->re = cm + mspan;
				}
			} else {
				/* fully auto: the next free place from the cursor */
				if (nspan > nminor)
					nspan = nminor;
				for (;;) {
					while (cn + nspan <= nminor &&
					       !grid_occ_free(&occ, cm, cm + mspan,
							cn, cn + nspan))
						cn++;
					if (cn + nspan <= nminor)
						break;
					cm++;
					cn = 0;
				}
				if (g->column_flow) {
					it->cs = cm;
					it->ce = cm + mspan;
					it->rs = cn;
					it->re = cn + nspan;
				} else {
					it->rs = cm;
					it->re = cm + mspan;
					it->cs = cn;
					it->ce = cn + nspan;
				}
				cn += nspan;
			}
			if (!grid_occ_mark(&occ, MAJ0(it), MAJ1(it), MIN0(it),
					MIN1(it))) {
				ok = false;
				goto done;
			}
			it->placed = true;
		}
	}

#undef MAJ0
#undef MAJ1
#undef MIN0
#undef MIN1

	/* the grid's size: the explicit grid and every item's area */
	*nrows = g->erows + g->row_off;
	*ncols = g->ecols + g->col_off;
	for (int i = 0; i < nitems; i++) {
		if (items[i].re > *nrows) *nrows = items[i].re;
		if (items[i].ce > *ncols) *ncols = items[i].ce;
	}
	if (*nrows > GRID_MAX_TRACKS) *nrows = GRID_MAX_TRACKS;
	if (*ncols > GRID_MAX_TRACKS) *ncols = GRID_MAX_TRACKS;

done:
	free(occ.cells);
	free(rspan);
	free(cspan);
	free(rdef);
	free(cdef);
	return ok;
}

/* ---- track sizing ---------------------------------------------------------------------- */

/** A track's sizing function: the explicit one, else the auto tracks' (in turn). */
static const struct grid_track_def *grid_track_def(const struct grid_tracks *expl,
		const struct grid_tracks *autos, int off, int i)
{
	static const struct grid_track_def dflt = {
		{ GK_AUTO, 0 }, { GK_AUTO, 0 }
	};
	int e = i - off;

	if (e >= 0 && e < expl->n)
		return &expl->t[e];
	if (autos->n == 0)
		return &dflt;
	if (e >= expl->n) {
		return &autos->t[(e - expl->n) % autos->n];
	}
	/* before the explicit grid: the auto tracks backwards */
	{
		int k = (-e - 1) % autos->n;
		return &autos->t[autos->n - 1 - k];
	}
}

static int grid_breadth_px(const struct grid_breadth *b, int avail)
{
	if (b->kind == GK_FIXED)
		return (int) b->v;
	if (b->kind == GK_PCT && avail >= 0)
		return (int) (b->v * avail / 100);
	return -1;
}

/**
 * Size the tracks of one axis (CSS Grid 11, simplified).
 *
 * \param defs     the tracks' sizing functions
 * \param n        track count
 * \param avail    the grid's inner size on this axis, or -1 (indefinite)
 * \param gap      the gap between tracks
 * \param items    the items, with their contributions: cmin / cmax (outer sizes)
 * \param s0 / s1  per item: its first track / one past its last
 * \param sizes    out: the tracks' sizes
 * \param stretch  whether auto tracks share the free space (justify- / align-content
 *                 normal)
 */
static void grid_size_tracks(const struct grid_track_def *const *defs, int n,
		int avail, int gap, int nitems, const int *cmin, const int *cmax,
		const int *s0, const int *s1, int *sizes, bool stretch)
{
	int *base, *limit;
	bool *flex;
	int gaps = n > 1 ? (n - 1) * gap : 0;

	base = calloc(n, sizeof *base);
	limit = calloc(n, sizeof *limit);
	flex = calloc(n, sizeof *flex);
	if (base == NULL || limit == NULL || flex == NULL) {
		for (int i = 0; i < n; i++)
			sizes[i] = 0;
		goto out;
	}

	/* 1. fixed sizes, and the contributions of the items in one track */
	for (int i = 0; i < n; i++) {
		const struct grid_track_def *d = defs[i];
		int mn = grid_breadth_px(&d->min, avail);
		int mx = grid_breadth_px(&d->max, avail);
		int imin = 0, imax = 0;

		for (int k = 0; k < nitems; k++) {
			if (s0[k] == i && s1[k] == i + 1) {
				if (cmin[k] > imin) imin = cmin[k];
				if (cmax[k] > imax) imax = cmax[k];
			}
		}
		flex[i] = d->max.kind == GK_FR;

		if (mn >= 0)
			base[i] = mn;
		else if (d->min.kind == GK_MAX_CONTENT)
			base[i] = imax;
		else
			base[i] = imin;	/* auto, min-content, an unknown % */

		if (mx >= 0)
			limit[i] = mx;
		else if (d->max.kind == GK_MIN_CONTENT)
			limit[i] = imin;
		else if (d->max.kind == GK_FIT)
			limit[i] = max(imin, min(imax, (int) d->max.v));
		else if (flex[i])
			limit[i] = base[i];
		else
			limit[i] = imax;	/* auto, max-content, an unknown % */
		if (limit[i] < base[i])
			limit[i] = base[i];
	}

	/* 2. the items spanning several tracks (none flexible): what they need beyond
	 * the tracks' sizes, shared among the spanned intrinsic tracks */
	for (int k = 0; k < nitems; k++) {
		int a = s0[k], b = s1[k], sum_b = 0, sum_l = 0, nint = 0;
		bool has_flex = false;

		if (b - a < 2 || a < 0 || b > n)
			continue;
		for (int i = a; i < b; i++) {
			sum_b += base[i];
			sum_l += limit[i];
			if (flex[i])
				has_flex = true;
			if (defs[i]->min.kind != GK_FIXED &&
			    defs[i]->min.kind != GK_PCT)
				nint++;
		}
		if (has_flex)
			continue;
		sum_b += (b - a - 1) * gap;
		sum_l += (b - a - 1) * gap;
		if (nint > 0 && cmin[k] > sum_b) {
			int extra = cmin[k] - sum_b, share = extra / nint,
				rem = extra % nint;
			for (int i = a; i < b; i++)
				if (defs[i]->min.kind != GK_FIXED &&
				    defs[i]->min.kind != GK_PCT) {
					base[i] += share + (rem-- > 0 ? 1 : 0);
					if (limit[i] < base[i])
						limit[i] = base[i];
				}
		}
		if (cmax[k] > sum_l) {
			int extra = cmax[k] - sum_l, m = 0;
			for (int i = a; i < b; i++)
				if (defs[i]->max.kind == GK_AUTO ||
				    defs[i]->max.kind == GK_MAX_CONTENT)
					m++;
			if (m > 0) {
				int share = extra / m, rem = extra % m;
				for (int i = a; i < b; i++)
					if (defs[i]->max.kind == GK_AUTO ||
					    defs[i]->max.kind == GK_MAX_CONTENT)
						limit[i] += share + (rem-- > 0 ? 1 : 0);
			}
		}
	}

	for (int i = 0; i < n; i++)
		sizes[i] = base[i];

	/* 3. the free space grows the tracks up to their limits (evenly) */
	if (avail >= 0) {
		int free_space = avail - gaps;
		for (int i = 0; i < n; i++)
			free_space -= sizes[i];
		while (free_space > 0) {
			int m = 0, share, given = 0;
			for (int i = 0; i < n; i++)
				if (!flex[i] && sizes[i] < limit[i])
					m++;
			if (m == 0)
				break;
			share = free_space / m;
			if (share < 1)
				share = 1;
			for (int i = 0; i < n && free_space - given > 0; i++) {
				if (flex[i] || sizes[i] >= limit[i])
					continue;
				int g = min(share, limit[i] - sizes[i]);
				g = min(g, free_space - given);
				sizes[i] += g;
				given += g;
			}
			if (given == 0)
				break;
			free_space -= given;
		}
	} else {
		/* (indefinite: the tracks at their limits -- max-content) */
		for (int i = 0; i < n; i++)
			if (!flex[i])
				sizes[i] = limit[i];
	}

	/* 4. the flexible tracks: the fr that fills the space (a track whose base is
	 * more than its share keeps its base) */
	{
		bool any = false;
		for (int i = 0; i < n; i++)
			if (flex[i])
				any = true;
		if (any) {
			bool *inflex = calloc(n, sizeof *inflex);
			float fr = 0;

			if (inflex == NULL)
				goto out;
			if (avail >= 0) {
				for (int pass = 0; pass < n + 1; pass++) {
					float factors = 0;
					int left = avail - gaps;
					bool again = false;
					for (int i = 0; i < n; i++) {
						if (flex[i] && !inflex[i])
							factors += defs[i]->max.v;
						else
							left -= sizes[i];
					}
					if (factors <= 0)
						break;
					fr = (left > 0 ? left : 0) /
						(factors < 1 ? 1 : factors);
					for (int i = 0; i < n; i++) {
						if (flex[i] && !inflex[i] &&
						    base[i] > fr * defs[i]->max.v) {
							inflex[i] = true;
							sizes[i] = base[i];
							again = true;
						}
					}
					if (!again)
						break;
				}
			} else {
				/* indefinite: 1fr the largest of the flexible tracks'
				 * contributions per fr */
				for (int i = 0; i < n; i++) {
					int imax = 0;
					if (!flex[i] || defs[i]->max.v <= 0)
						continue;
					for (int k = 0; k < nitems; k++)
						if (s0[k] == i && s1[k] == i + 1 &&
						    cmax[k] > imax)
							imax = cmax[k];
					if (imax / defs[i]->max.v > fr)
						fr = imax / defs[i]->max.v;
				}
			}
			for (int i = 0; i < n; i++) {
				if (flex[i] && !inflex[i]) {
					int s = (int) (fr * defs[i]->max.v + 0.5f);
					sizes[i] = max(base[i], s);
				}
			}
			free(inflex);
		}
	}

	/* 5. what is left: to the auto tracks (content-distribution normal: stretch) */
	if (stretch && avail >= 0) {
		int free_space = avail - gaps, m = 0;
		for (int i = 0; i < n; i++) {
			free_space -= sizes[i];
			if (defs[i]->max.kind == GK_AUTO)
				m++;
		}
		if (free_space > 0 && m > 0) {
			int share = free_space / m, rem = free_space % m;
			for (int i = 0; i < n; i++)
				if (defs[i]->max.kind == GK_AUTO)
					sizes[i] += share + (rem-- > 0 ? 1 : 0);
		}
	}

out:
	free(base);
	free(limit);
	free(flex);
}

/** The tracks' positions: start offsets, with the gaps and the content-distribution. */
static void grid_track_positions(const int *sizes, int n, int gap, int avail,
		uint8_t distribution, bool collapse_empty, int *pos)
{
	int total = 0, lead = 0, between = 0, rem = 0, free_space;

	(void) collapse_empty;
	for (int i = 0; i < n; i++)
		total += sizes[i];
	if (n > 1)
		total += (n - 1) * gap;
	free_space = avail >= 0 ? avail - total : 0;
	if (free_space > 0) {
		switch (distribution) {
		case CSS_JUSTIFY_CONTENT_FLEX_END:
			lead = free_space;
			break;
		case CSS_JUSTIFY_CONTENT_CENTER:
			lead = free_space / 2;
			break;
		case CSS_JUSTIFY_CONTENT_SPACE_BETWEEN:
			if (n > 1) {
				between = free_space / (n - 1);
				rem = free_space % (n - 1);
			}
			break;
		case CSS_JUSTIFY_CONTENT_SPACE_AROUND:
			between = free_space / n;
			lead = between / 2;
			break;
		case CSS_JUSTIFY_CONTENT_SPACE_EVENLY:
			between = free_space / (n + 1);
			lead = between;
			break;
		default:
			break;
		}
	}
	{
		int p = lead;
		for (int i = 0; i < n; i++) {
			pos[i] = p;
			p += sizes[i] + gap + between;
			if (rem > 0) {
				p++;
				rem--;
			}
		}
	}
}

/* ---- the grid's context ---------------------------------------------------------------- */

static int grid_gap_px(const struct grid_ctx *g, bool column, int avail)
{
	css_fixed len = 0;
	css_unit unit = CSS_UNIT_PX;
	uint8_t type;
	int px;

	type = column ? css_computed_column_gap(g->style, &len, &unit)
		      : css_computed_row_gap(g->style, &len, &unit);
	if (type != CSS_COLUMN_GAP_SET)		/* (CSS_ROW_GAP_SET too) */
		return 0;
	if (unit == CSS_UNIT_PCT)
		return avail > 0 ? FPCT_OF_INT_TOINT(len, avail) : 0;
	px = FIXTOINT(css_unit_len2device_px(g->style, g->unit_len_ctx, len, unit));
	return px > 0 ? px : 0;
}

/** Read the grid's templates; the auto repeats expanded for an inner width `avail_w`. */
static void grid_ctx_init(struct grid_ctx *g, struct box *grid,
		const css_unit_ctx *unit_len_ctx, html_content *content,
		int avail_w, int avail_h)
{
	lwc_string *text;
	uint8_t flow;

	memset(g, 0, sizeof *g);
	g->grid = grid;
	g->style = grid->style;
	g->unit_len_ctx = unit_len_ctx;
	g->content = content;

	g->col_gap = grid_gap_px(g, true, avail_w);
	g->row_gap = grid_gap_px(g, false, avail_h);

	text = NULL;
	if (css_computed_grid_template_columns(g->style, &text) != CSS_ONYX_TEXT_SET)
		text = NULL;
	grid_parse_tracks(g, text, &g->cols);
	grid_expand_auto(g, &g->cols, avail_w, g->col_gap);

	text = NULL;
	if (css_computed_grid_template_rows(g->style, &text) != CSS_ONYX_TEXT_SET)
		text = NULL;
	grid_parse_tracks(g, text, &g->rows);
	grid_expand_auto(g, &g->rows, avail_h, g->row_gap);

	text = NULL;
	if (css_computed_grid_auto_columns(g->style, &text) != CSS_ONYX_TEXT_SET)
		text = NULL;
	grid_parse_tracks(g, text, &g->acols);
	text = NULL;
	if (css_computed_grid_auto_rows(g->style, &text) != CSS_ONYX_TEXT_SET)
		text = NULL;
	grid_parse_tracks(g, text, &g->arows);

	g->ecols = g->cols.n;
	g->erows = g->rows.n;
	text = NULL;
	if (css_computed_grid_template_areas(g->style, &text) == CSS_ONYX_TEXT_SET)
		grid_parse_areas(g, text);

	flow = css_computed_grid_auto_flow(g->style);
	g->column_flow = flow == CSS_GRID_AUTO_FLOW_COLUMN ||
			 flow == CSS_GRID_AUTO_FLOW_COLUMN_DENSE;
	g->dense = flow == CSS_GRID_AUTO_FLOW_ROW_DENSE ||
		   flow == CSS_GRID_AUTO_FLOW_COLUMN_DENSE;
}

/** The items: the grid's in-flow children, in `order` then document order. */
static struct grid_item *grid_items(struct box *grid, int *count, bool in_flow_only)
{
	struct grid_item *items;
	int n = 0, i = 0;

	for (struct box *c = grid->children; c != NULL; c = c->next)
		n++;
	items = calloc(n ? n : 1, sizeof *items);
	if (items == NULL)
		return NULL;
	for (struct box *c = grid->children; c != NULL; c = c->next) {
		int32_t order = 0;
		if (in_flow_only && lh__box_is_absolute(c))
			continue;
		if (c->style != NULL)
			css_computed_order(c->style, &order);
		items[i].box = c;
		items[i].order = order;
		items[i].index = i;
		i++;
	}
	qsort(items, i, sizeof *items, grid_item_cmp);
	*count = i;
	return items;
}

/* ---- layout ------------------------------------------------------------------------------ */

/** Lay an item out at its (content) width b->width. */
static bool grid_layout_item(struct grid_ctx *g, struct box *b, int avail)
{
	bool ok = true;

	switch (b->type) {
	case BOX_BLOCK:
		ok = layout_block_context(b, -1, g->content);
		break;
	case BOX_TABLE:
		b->float_container = b->parent;
		ok = layout_table(b, avail, g->content);
		b->float_container = NULL;
		break;
	case BOX_FLEX:
		b->float_container = b->parent;
		ok = layout_flex(b, avail, g->content);
		b->float_container = NULL;
		break;
	default:
		break;
	}
	return ok;
}

/** The item's self-alignment on one axis (its own, else the container's). */
static uint8_t grid_justify_self(const struct box *grid, const struct box *b)
{
	uint8_t v = b->style ? css_computed_justify_self(b->style) : CSS_JUSTIFY_SELF_AUTO;
	if (v == CSS_JUSTIFY_SELF_AUTO || v == CSS_JUSTIFY_SELF_INHERIT)
		v = css_computed_justify_items(grid->style);
	return v;
}

static uint8_t grid_align_self(const struct box *grid, const struct box *b)
{
	uint8_t v = b->style ? css_computed_align_self(b->style) : CSS_ALIGN_SELF_AUTO;
	if (v == CSS_ALIGN_SELF_AUTO || v == CSS_ALIGN_SELF_INHERIT)
		v = css_computed_align_items(grid->style);
	return v;
}

/* exported function documented in html/layout_internal.h */
bool layout_grid(struct box *grid, int available_width, html_content *content)
{
	struct grid_ctx *g;
	struct grid_item *items = NULL, *abs_items = NULL;
	const struct grid_track_def **cdefs = NULL, **rdefs = NULL;
	int *cmin = NULL, *cmax = NULL, *s0 = NULL, *s1 = NULL;
	int *csize = NULL, *cpos = NULL, *rsize = NULL, *rpos = NULL;
	int nitems = 0, nabs = 0, nrows = 0, ncols = 0;
	int max_height, min_height, inner_w, inner_h;
	bool ok = false;

	g = malloc(sizeof *g);
	if (g == NULL)
		return false;

	layout_find_dimensions(&content->unit_len_ctx, available_width, -1,
			grid, grid->style, NULL, &grid->height,
			NULL, NULL, &max_height, &min_height,
			grid->margin, grid->padding, grid->border);
	inner_w = min(available_width, grid->width);
	inner_h = grid->height;			/* (AUTO: indefinite) */

	grid_ctx_init(g, grid, &content->unit_len_ctx, content, inner_w,
			inner_h == AUTO ? -1 : inner_h);

	items = grid_items(grid, &nitems, true);
	if (items == NULL)
		goto out;
	if (!grid_place(g, items, nitems, &nrows, &ncols))
		goto out;

	cdefs = calloc(ncols + 1, sizeof *cdefs);
	rdefs = calloc(nrows + 1, sizeof *rdefs);
	cmin = calloc(nitems + 1, sizeof *cmin);
	cmax = calloc(nitems + 1, sizeof *cmax);
	s0 = calloc(nitems + 1, sizeof *s0);
	s1 = calloc(nitems + 1, sizeof *s1);
	csize = calloc(ncols + 1, sizeof *csize);
	cpos = calloc(ncols + 1, sizeof *cpos);
	rsize = calloc(nrows + 1, sizeof *rsize);
	rpos = calloc(nrows + 1, sizeof *rpos);
	if (!cdefs || !rdefs || !cmin || !cmax || !s0 || !s1 || !csize ||
	    !cpos || !rsize || !rpos)
		goto out;

	for (int i = 0; i < ncols; i++)
		cdefs[i] = grid_track_def(&g->cols, &g->acols, g->col_off, i);
	for (int i = 0; i < nrows; i++)
		rdefs[i] = grid_track_def(&g->rows, &g->arows, g->row_off, i);

	/* the item boxes' dimensions (margins, paddings, borders, width, height) */
	for (int k = 0; k < nitems; k++) {
		struct box *b = items[k].box;
		b->float_container = grid;
		layout_find_dimensions(&content->unit_len_ctx, inner_w, -1,
				b, b->style, &b->width, &b->height,
				NULL, NULL, NULL, NULL,
				b->margin, b->padding, b->border);
		b->float_container = NULL;
	}

	/* the columns, from the items' min / max widths */
	for (int k = 0; k < nitems; k++) {
		struct box *b = items[k].box;
		int delta = lh__delta_outer_width(b);
		if (b->width != AUTO) {
			cmin[k] = cmax[k] = b->width + delta;
		} else {
			cmin[k] = b->min_width;
			cmax[k] = b->max_width;
		}
		/* (an item that may shrink below its content: its minimum is 0) */
		if (b->style != NULL) {
			css_fixed v;
			css_unit u;
			if (css_computed_min_width(b->style, &v, &u) ==
					CSS_MIN_WIDTH_SET) {
				int mw = FIXTOINT(css_unit_len2device_px(b->style,
						&content->unit_len_ctx, v, u));
				if (u != CSS_UNIT_PCT && cmin[k] > mw + delta &&
				    b->width == AUTO)
					cmin[k] = mw + delta;
			} else if (css_computed_overflow_x(b->style) !=
					CSS_OVERFLOW_VISIBLE && b->width == AUTO) {
				cmin[k] = delta;
			}
		}
		s0[k] = items[k].cs;
		s1[k] = items[k].ce;
	}
	grid_size_tracks(cdefs, ncols, inner_w, g->col_gap, nitems, cmin, cmax,
			s0, s1, csize, true);
	grid_track_positions(csize, ncols, g->col_gap, inner_w,
			css_computed_justify_content(grid->style),
			g->cols.auto_fit, cpos);

	/* each item laid out at its area's width: its height */
	for (int k = 0; k < nitems; k++) {
		struct grid_item *it = &items[k];
		struct box *b = it->box;
		int area_w = cpos[it->ce - 1] + csize[it->ce - 1] - cpos[it->cs];
		int delta = lh__delta_outer_width(b);
		uint8_t js = grid_justify_self(grid, b);
		bool auto_ml = b->margin[LEFT] == AUTO, auto_mr = b->margin[RIGHT] == AUTO;

		if (b->margin[LEFT] == AUTO)
			b->margin[LEFT] = 0;
		if (b->margin[RIGHT] == AUTO)
			b->margin[RIGHT] = 0;
		delta = lh__delta_outer_width(b);
		if (b->width == AUTO) {
			if ((js == CSS_JUSTIFY_ITEMS_STRETCH ||
			     js == CSS_JUSTIFY_ITEMS_INHERIT) &&
			    b->object == NULL && !auto_ml && !auto_mr) {
				b->width = area_w - delta;
			} else {
				/* shrink-to-fit in the area */
				int w = min(max(b->min_width, area_w), b->max_width);
				b->width = w - delta;
			}
			if (b->width < 0)
				b->width = 0;
		}
		/* min-width / max-width */
		{
			int mxw = -1, mnw = 0;
			b->float_container = grid;
			layout_find_dimensions(&content->unit_len_ctx, area_w, -1,
					b, b->style, NULL, NULL, &mxw, &mnw,
					NULL, NULL, NULL, NULL, NULL);
			b->float_container = NULL;
			if (mxw >= 0 && b->width > mxw)
				b->width = mxw;
			if (b->width < mnw)
				b->width = mnw;
		}
		it->width = b->width + delta;
		if (!grid_layout_item(g, b, area_w))
			goto out;
		/* where it goes in its area */
		{
			int x = cpos[it->cs], free_w = area_w - it->width;
			if (auto_ml && auto_mr)
				x += free_w / 2;
			else if (auto_ml)
				x += free_w;
			else if (!auto_mr) {
				if (js == CSS_JUSTIFY_ITEMS_FLEX_END)
					x += free_w;
				else if (js == CSS_JUSTIFY_ITEMS_CENTER)
					x += free_w / 2;
			}
			b->x = grid->padding[LEFT] + x + b->margin[LEFT] +
					b->border[LEFT].width;
		}
	}

	/* the rows, from the items' heights */
	for (int k = 0; k < nitems; k++) {
		struct box *b = items[k].box;
		int h = b->height == AUTO ? 0 : b->height;
		items[k].height = h + lh__delta_outer_height(b);
		cmin[k] = cmax[k] = items[k].height;
		s0[k] = items[k].rs;
		s1[k] = items[k].re;
	}
	{
		int avail_h = inner_h;
		if (avail_h == AUTO) {
			avail_h = -1;
		}
		grid_size_tracks(rdefs, nrows, avail_h, g->row_gap, nitems, cmin,
				cmax, s0, s1, rsize, true);
		if (avail_h < 0) {
			/* the grid's height: its rows, within min- / max-height */
			int total = 0;
			for (int i = 0; i < nrows; i++)
				total += rsize[i];
			if (nrows > 1)
				total += (nrows - 1) * g->row_gap;
			avail_h = total;
			if (max_height >= 0 && avail_h > max_height)
				avail_h = max_height;
			if (min_height > 0 && avail_h < min_height)
				avail_h = min_height;
			if (avail_h != total) {
				/* (min-height: the auto rows share the extra) */
				grid_size_tracks(rdefs, nrows, avail_h,
						g->row_gap, nitems, cmin, cmax,
						s0, s1, rsize, true);
			}
		}
		grid_track_positions(rsize, nrows, g->row_gap, avail_h,
				css_computed_align_content(grid->style) ==
					CSS_ALIGN_CONTENT_FLEX_END ?
					CSS_JUSTIFY_CONTENT_FLEX_END :
				css_computed_align_content(grid->style) ==
					CSS_ALIGN_CONTENT_CENTER ?
					CSS_JUSTIFY_CONTENT_CENTER :
				css_computed_align_content(grid->style) ==
					CSS_ALIGN_CONTENT_SPACE_BETWEEN ?
					CSS_JUSTIFY_CONTENT_SPACE_BETWEEN :
				css_computed_align_content(grid->style) ==
					CSS_ALIGN_CONTENT_SPACE_AROUND ?
					CSS_JUSTIFY_CONTENT_SPACE_AROUND :
				css_computed_align_content(grid->style) ==
					CSS_ALIGN_CONTENT_SPACE_EVENLY ?
					CSS_JUSTIFY_CONTENT_SPACE_EVENLY :
					CSS_JUSTIFY_CONTENT_FLEX_START,
				g->rows.auto_fit, rpos);
		if (grid->height == AUTO)
			grid->height = avail_h;
	}

	/* the items in their areas' height */
	for (int k = 0; k < nitems; k++) {
		struct grid_item *it = &items[k];
		struct box *b = it->box;
		int area_h = rpos[it->re - 1] + rsize[it->re - 1] - rpos[it->rs];
		uint8_t as = grid_align_self(grid, b);
		bool auto_mt = b->margin[TOP] == AUTO, auto_mb = b->margin[BOTTOM] == AUTO;
		int y = rpos[it->rs], free_h;
		css_fixed hv;
		css_unit hu;

		if (auto_mt)
			b->margin[TOP] = 0;
		if (auto_mb)
			b->margin[BOTTOM] = 0;
		if ((as == CSS_ALIGN_ITEMS_STRETCH || as == CSS_ALIGN_ITEMS_INHERIT) &&
		    !auto_mt && !auto_mb && b->object == NULL &&
		    b->style != NULL &&
		    css_computed_height(b->style, &hv, &hu) == CSS_HEIGHT_AUTO) {
			int h = area_h - lh__delta_outer_height(b);
			if (h > b->height) {
				b->height = h;
				if (b->type == BOX_FLEX) {
					/* (its own items stretch in it) */
					grid_layout_item(g, b, b->width +
						lh__delta_outer_width(b));
					if (b->height < h)
						b->height = h;
				}
			}
		}
		it->height = b->height + lh__delta_outer_height(b);
		free_h = area_h - it->height;
		if (auto_mt && auto_mb)
			y += free_h / 2;
		else if (auto_mt)
			y += free_h;
		else if (!auto_mb) {
			if (as == CSS_ALIGN_ITEMS_FLEX_END)
				y += free_h;
			else if (as == CSS_ALIGN_ITEMS_CENTER)
				y += free_h / 2;
		}
		b->y = grid->padding[TOP] + y + b->margin[TOP] + b->border[TOP].width;
	}

	/* the absolutely positioned children: laid out, at the content's origin (their
	 * place is decided later, by their containing block) */
	abs_items = grid_items(grid, &nabs, false);
	if (abs_items != NULL) {
		for (int k = 0; k < nabs; k++) {
			struct box *b = abs_items[k].box;
			if (!lh__box_is_absolute(b))
				continue;
			b->x = grid->padding[LEFT];
			b->y = grid->padding[TOP];
		}
	}

	ok = true;

out:
	if (!ok)
		NSLOG(layout, ERROR, "grid %p: layout failed", grid);
	free(items);
	free(abs_items);
	free(cdefs);
	free(rdefs);
	free(cmin);
	free(cmax);
	free(s0);
	free(s1);
	free(csize);
	free(cpos);
	free(rsize);
	free(rpos);
	free(g);
	return ok;
}

/* exported function documented in html/layout_internal.h */
void layout_minmax_grid(struct box *grid, const css_unit_ctx *unit_len_ctx,
		int *min_width, int *max_width)
{
	struct grid_ctx *g;
	struct grid_item *items = NULL;
	const struct grid_track_def **cdefs = NULL;
	int *cmin = NULL, *cmax = NULL, *s0 = NULL, *s1 = NULL, *sizes = NULL;
	int nitems = 0, nrows = 0, ncols = 0;

	g = malloc(sizeof *g);
	if (g == NULL)
		return;
	grid_ctx_init(g, grid, unit_len_ctx, NULL, -1, -1);
	items = grid_items(grid, &nitems, true);
	if (items == NULL || !grid_place(g, items, nitems, &nrows, &ncols))
		goto out;

	cdefs = calloc(ncols + 1, sizeof *cdefs);
	cmin = calloc(nitems + 1, sizeof *cmin);
	cmax = calloc(nitems + 1, sizeof *cmax);
	s0 = calloc(nitems + 1, sizeof *s0);
	s1 = calloc(nitems + 1, sizeof *s1);
	sizes = calloc(ncols + 1, sizeof *sizes);
	if (!cdefs || !cmin || !cmax || !s0 || !s1 || !sizes)
		goto out;
	for (int i = 0; i < ncols; i++)
		cdefs[i] = grid_track_def(&g->cols, &g->acols, g->col_off, i);
	for (int k = 0; k < nitems; k++) {
		cmin[k] = items[k].box->min_width;
		cmax[k] = items[k].box->max_width;
		s0[k] = items[k].cs;
		s1[k] = items[k].ce;
	}

	/* the min-content width: the tracks at their minimums (an fr's: its items'
	 * min-content, or 0 for minmax(0, fr)) */
	{
		int total = 0;
		int *zero = calloc(nitems + 1, sizeof *zero);
		if (zero == NULL)
			goto out;
		for (int k = 0; k < nitems; k++)
			zero[k] = cmin[k];
		grid_size_tracks(cdefs, ncols, -1, g->col_gap, nitems, cmin, zero,
				s0, s1, sizes, false);
		for (int i = 0; i < ncols; i++)
			total += sizes[i];
		if (ncols > 1)
			total += (ncols - 1) * g->col_gap;
		*min_width = total;
		free(zero);
	}
	/* the max-content width */
	{
		int total = 0;
		grid_size_tracks(cdefs, ncols, -1, g->col_gap, nitems, cmin, cmax,
				s0, s1, sizes, false);
		for (int i = 0; i < ncols; i++)
			total += sizes[i];
		if (ncols > 1)
			total += (ncols - 1) * g->col_gap;
		*max_width = total;
	}
	if (*max_width < *min_width)
		*max_width = *min_width;

out:
	free(items);
	free(cdefs);
	free(cmin);
	free(cmax);
	free(s0);
	free(s1);
	free(sizes);
	free(g);
}
