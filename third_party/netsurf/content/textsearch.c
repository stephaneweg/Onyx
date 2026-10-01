/*
 * Copyright 2004 John M Bell <jmb202@ecs.soton.ac.uk>
 * Copyright 2020 Vincent Sanders <vince@netsurf-browser.org>
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
 * Free text search
 *
 * Onyx (Jet Browser, docs/06 §40): find in page as in Chrome.
 *
 * NetSurf kept its matches in a list, each with a selection object of its own (made by a walk
 * of the whole box tree: selection_init, then selection_set_position's redraw: another walk)
 * and looked every match up for each text box painted -- O(boxes x matches) a search and a
 * paint. The matches are now an array in document order (the box walk finds them so); a
 * painted text's matches are found by a binary search (content_textsearch_onyx_ranges: all
 * of them, the current one told apart -- Chrome's orange, the others yellow); a new search
 * repaints the matches' boxes (or the whole content past HL_REDRAW_MAX of them), a step the
 * two current ones.
 *
 * The search is literal ('#' and '*' are no longer NetSurf's wildcards), case-insensitive by
 * default -- then without the accents too ("e" finds "é", as Chrome does), the typographic
 * quotes as ' and " -- and the no-break space is a space; a step wraps around at the ends.
 * A layout made after the search (the page changed, the window resized) leaves its boxes and
 * offsets stale: the matches are found again at the next paint, step or state query
 * (refresh_matches), the current one kept where it was, the frontend told
 * (CONTENT_TEXTSEARCH_MATCH).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "utils/errors.h"
#include "utils/utils.h"
#include "utils/ascii.h"
#include "netsurf/types.h"
#include "desktop/selection.h"

#include "content/content.h"
#include "content/content_protected.h"
#include "content/hlcache.h"
#include "content/textsearch.h"

#define HL_REDRAW_MAX	64	/* matches repainted one by one; more: the whole content */

/**
 * a search match
 */
struct ts_match {
	unsigned start_idx;	/**< its text offsets [start, end) */
	unsigned end_idx;
	struct box *start_box;	/**< content opaque pointers (html: the boxes) */
	struct box *end_box;
};

/**
 * The context for a free text search
 */
struct textsearch_context {
	struct content *c;	/**< content search was performed upon */
	void *gui_p;		/**< opaque pointer passed to constructor */
	struct ts_match *m;	/**< the matches, in document order */
	unsigned n, cap;
	int current;		/**< the current match, -1 none */
	char *string;		/**< query string search results are for */
	bool prev_case_sens;
	bool newsearch;
	unsigned gen;		/**< the content's layout_gen when found */
};


/**
 * broadcast textsearch message
 */
static inline void
textsearch_broadcast(struct textsearch_context *textsearch,
		     int type,
		     bool state,
		     const char *string)
{
	union content_msg_data msg_data;
	msg_data.textsearch.type = type;
	msg_data.textsearch.ctx = textsearch->gui_p;
	msg_data.textsearch.state = state;
	msg_data.textsearch.string = string;
	content_broadcast(textsearch->c, CONTENT_MSG_TEXTSEARCH, &msg_data);
}


/** A match's area repainted (its boxes fresh: found since the last layout) */
static void redraw_match(struct textsearch_context *ctx, int i)
{
	struct content *c = ctx->c;
	struct rect r;

	if (i < 0 || (unsigned) i >= ctx->n ||
	    ctx->gen != c->textsearch.layout_gen)
		return;
	if (c->handler->textsearch_bounds(c, ctx->m[i].start_idx,
			ctx->m[i].end_idx, ctx->m[i].start_box,
			ctx->m[i].end_box, &r) == NSERROR_OK &&
	    r.x1 > r.x0 && r.y1 > r.y0)
		content__request_redraw(c, r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
}

/** All the matches' areas repainted (their highlights appear or go) */
static void redraw_matches(struct textsearch_context *ctx)
{
	unsigned i;

	if (ctx->n == 0)
		return;
	if (ctx->n > HL_REDRAW_MAX || ctx->gen != ctx->c->textsearch.layout_gen) {
		content__request_redraw(ctx->c, 0, 0, ctx->c->width,
				ctx->c->height);
		return;
	}
	for (i = 0; i < ctx->n; i++)
		redraw_match(ctx, (int) i);
}


/** Forget the matches (no repaint) */
static void free_matches(struct textsearch_context *textsearch)
{
	textsearch->n = 0;
	textsearch->current = -1;
}


/** The first match that ends after offset (a binary search): ctx->n when none */
static unsigned first_ending_after(struct textsearch_context *ctx, unsigned offset)
{
	unsigned lo = 0, hi = ctx->n;

	while (lo < hi) {
		unsigned mid = lo + (hi - lo) / 2;
		if (ctx->m[mid].end_idx <= offset)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}


/** The matches found (the content's handler), no repaint */
static nserror find_all(struct textsearch_context *ctx, const char *string,
		int string_len, bool case_sensitive)
{
	nserror res;

	free_matches(ctx);
	ctx->gen = ctx->c->textsearch.layout_gen;
	textsearch_broadcast(ctx, CONTENT_TEXTSEARCH_FIND, true, NULL);
	res = ctx->c->handler->textsearch_find(ctx->c, ctx, string, string_len,
			case_sensitive);
	textsearch_broadcast(ctx, CONTENT_TEXTSEARCH_FIND, false, NULL);
	if (res != NSERROR_OK)
		free_matches(ctx);
	return res;
}


/** The match state told to the frontend */
static void broadcast_state(struct textsearch_context *ctx)
{
	textsearch_broadcast(ctx, CONTENT_TEXTSEARCH_MATCH, ctx->current >= 0, NULL);
	textsearch_broadcast(ctx, CONTENT_TEXTSEARCH_BACK, ctx->n > 1, NULL);
	textsearch_broadcast(ctx, CONTENT_TEXTSEARCH_FORWARD, ctx->n > 1, NULL);
}


/**
 * A layout came since the search: the matches found again (their boxes, their offsets), the
 * current one the first at or after where it was. No repaint (called while painting, or
 * before a step that repaints).
 */
static void refresh_matches(struct textsearch_context *ctx)
{
	unsigned at = 0;
	bool had = ctx->current >= 0;

	if (ctx->string == NULL || ctx->newsearch ||
	    ctx->gen == ctx->c->textsearch.layout_gen)
		return;
	if (had)
		at = ctx->m[ctx->current].start_idx;
	if (find_all(ctx, ctx->string, strlen(ctx->string),
			ctx->prev_case_sens) != NSERROR_OK)
		return;
	if (ctx->n > 0) {
		unsigned i = first_ending_after(ctx, at);
		ctx->current = had ? (int) (i < ctx->n ? i : ctx->n - 1) : 0;
	}
	broadcast_state(ctx);
}


/**
 * Search for a string in a content.
 *
 * \param context The search context.
 * \param string the string to search for
 * \param string_len length of search string
 * \param flags flags to control the search.
 */
static nserror
search_text(struct textsearch_context *context,
	    const char *string,
	    int string_len,
	    search_flags_t flags)
{
	struct rect bounds;
	union content_msg_data msg_data;
	bool case_sensitive, forwards;
	nserror res = NSERROR_OK;

	case_sensitive = ((flags & SEARCH_FLAG_CASE_SENSITIVE) != 0) ?
			true : false;
	forwards = ((flags & SEARCH_FLAG_BACKWARDS) == 0) ? true : false;

	if (context->c == NULL) {
		return res;
	}

	/* check if we need to start a new search or continue an old one */
	if ((context->newsearch) ||
	    (context->prev_case_sens != case_sensitive)) {
		/* the old matches' highlights go */
		redraw_matches(context);

		if (context->string != NULL) {
			free(context->string);
		}
		context->string = malloc(string_len + 1);
		if (context->string != NULL) {
			memcpy(context->string, string, string_len);
			context->string[string_len] = '\0';
		}

		res = find_all(context, string, string_len, case_sensitive);
		if (res != NSERROR_OK) {
			return res;
		}

		context->prev_case_sens = case_sensitive;

		/* new search, beginning at the top of the page */
		context->current = context->n > 0 ? 0 : -1;
		context->newsearch = false;

		/* the new matches' highlights */
		redraw_matches(context);

	} else {
		/* continued search in the direction given: around at the ends */
		int old = context->current;

		if (context->gen != context->c->textsearch.layout_gen) {
			redraw_matches(context);	/* (stale: the whole content) */
			refresh_matches(context);
			old = -1;
		}
		if (context->n > 0) {
			if (context->current < 0)
				context->current = 0;
			else if (forwards)
				context->current = (context->current + 1) %
						(int) context->n;
			else
				context->current = (context->current +
						(int) context->n - 1) %
						(int) context->n;
			redraw_match(context, old);
			redraw_match(context, context->current);
		}
	}

	broadcast_state(context);

	if (context->current < 0) {
		/* no current match */
		return res;
	}

	/* call content match bounds handler */
	res = context->c->handler->textsearch_bounds(context->c,
			context->m[context->current].start_idx,
			context->m[context->current].end_idx,
			context->m[context->current].start_box,
			context->m[context->current].end_box,
			&bounds);
	if (res == NSERROR_OK) {
		msg_data.scroll.area = true;
		msg_data.scroll.x0 = bounds.x0;
		msg_data.scroll.y0 = bounds.y0;
		msg_data.scroll.x1 = bounds.x1;
		msg_data.scroll.y1 = bounds.y1;
		content_broadcast(context->c, CONTENT_MSG_SCROLL, &msg_data);
	}

	return res;
}


/**
 * Begins/continues the search process
 *
 * \note that this may be called many times for a single search.
 *
 * \param context The search context in use.
 * \param flags   The flags forward/back etc
 * \param string  The string to match
 */
static nserror
content_textsearch_step(struct textsearch_context *textsearch,
			search_flags_t flags,
			const char *string)
{
	int string_len;
	nserror res = NSERROR_OK;

	assert(textsearch != NULL);

	/* broadcast recent query string */
	textsearch_broadcast(textsearch,
			     CONTENT_TEXTSEARCH_RECENT,
			     false,
			     string);

	string_len = strlen(string);
	if (string_len > 0) {
		res = search_text(textsearch, string, string_len, flags);
	} else {
		redraw_matches(textsearch);
		free_matches(textsearch);
		broadcast_state(textsearch);
	}

	return res;
}


/**
 * Terminate a search.
 *
 * \param c content to clear
 */
static nserror content_textsearch__clear(struct content *c)
{
	free(c->textsearch.string);
	c->textsearch.string = NULL;

	if (c->textsearch.context != NULL) {
		/* (the highlights go) */
		redraw_matches(c->textsearch.context);
		content_textsearch_destroy(c->textsearch.context);
		c->textsearch.context = NULL;
	}
	return NSERROR_OK;
}


/**
 * create a search_context
 *
 * \param c The content the search_context is connected to
 * \param gui_data A context pointer passed to the provider routines.
 * \param textsearch_out A pointer to recive the new text search context
 * \return NSERROR_OK on success and \a search_out updated else error code
 */
static nserror
content_textsearch_create(struct content *c,
			  void *gui_data,
			  struct textsearch_context **textsearch_out)
{
	struct textsearch_context *context;

	if ((c->handler->textsearch_find == NULL) ||
	    (c->handler->textsearch_bounds == NULL)) {
		/*
		 * content has no free text find handler so searching
		 *   is unsupported.
		 */
		return NSERROR_NOT_IMPLEMENTED;
	}

	context = calloc(1, sizeof(struct textsearch_context));
	if (context == NULL) {
		return NSERROR_NOMEM;
	}

	context->current = -1;
	context->newsearch = true;
	context->c = c;
	context->gui_p = gui_data;
	context->gen = c->textsearch.layout_gen;

	*textsearch_out = context;

	return NSERROR_OK;
}


/*
 * Onyx: the character of UTF-8 text at *sp (before es), folded for the search, *sp moved past
 * it. The no-break space is a space; with fold (case-insensitive): ASCII and Latin-1 letters
 * in lower case without their accents (é è ê ë É -> e, ç -> c, ñ -> n, ÿ -> y), œ Œ as one,
 * the typographic quotes as ' and ".
 */
static uint32_t fold_next(const char **sp, const char *es, bool fold)
{
	const unsigned char *s = (const unsigned char *) *sp;
	uint32_t u = *s;
	int k = u >= 0xF0 ? 3 : u >= 0xE0 ? 2 : u >= 0xC0 ? 1 : 0;

	if (k > 0 && (const char *) s + k < es) {
		uint32_t v = u & (0x3F >> k);
		int i;
		for (i = 1; i <= k; i++) {
			if ((s[i] & 0xC0) != 0x80)
				break;
			v = v << 6 | (s[i] & 0x3F);
		}
		if (i > k) {
			u = v;
			s += k;
		}
	}
	*sp = (const char *) (s + 1);
	if (u == 0xA0)
		return ' ';
	if (!fold)
		return u;
	if (u < 0x80)
		return (u >= 'A' && u <= 'Z') ? u + 32 : u;
	if (u >= 0xC0 && u <= 0xFF) {
		/* Latin-1's letters: their base letter (0: the letter itself, lower case) */
		static const char base[64] =
			"aaaaaa\0ceeeeiiii" "dnooooo\0ouuuuy\0\0"
			"aaaaaa\0ceeeeiiii" "dnooooo\0ouuuuy\0y";
		char b = base[u - 0xC0];
		if (b != '\0')
			return (uint32_t) (unsigned char) b;
		if (u >= 0xC0 && u <= 0xDE && u != 0xD7)
			return u + 0x20;	/* (Æ Ø Þ: æ ø þ) */
		return u;
	}
	if (u == 0x152)
		return 0x153;			/* (Œ: œ) */
	if (u == 0x2018 || u == 0x2019)
		return '\'';
	if (u == 0x201C || u == 0x201D)
		return '"';
	return u;
}


/* exported interface, documented in content/textsearch.h */
const char *
content_textsearch_find_pattern(const char *string,
				int s_len,
				const char *pattern,
				int p_len,
				bool case_sens,
				unsigned int *m_len)
{
	/* Onyx: a literal search (see the file's comment); the pattern folded once */
	uint32_t pat[256];
	int np = 0;
	const char *p = pattern, *ep = pattern + p_len;
	const char *s = string, *es = string + s_len;
	bool fold = !case_sens;

	while (p < ep && np < (int) NOF_ELEMENTS(pat))
		pat[np++] = fold_next(&p, ep, fold);
	if (np == 0)
		return NULL;

	while (s < es) {
		const char *t = s;
		const char *next;
		uint32_t ch = fold_next(&t, es, fold);

		next = t;
		if (ch == pat[0]) {
			int i = 1;
			while (i < np && t < es && fold_next(&t, es, fold) == pat[i])
				i++;
			if (i == np) {
				*m_len = t - s;
				return s;
			}
		}
		s = next;
	}
	return NULL;
}


/* exported interface, documented in content/textsearch.h */
nserror
content_textsearch_add_match(struct textsearch_context *context,
			     unsigned start_idx,
			     unsigned end_idx,
			     struct box *start_box,
			     struct box *end_box)
{
	struct ts_match t;
	unsigned i;

	if (context->n == context->cap) {
		unsigned cap = context->cap ? context->cap * 2 : 64;
		struct ts_match *m = realloc(context->m, cap * sizeof(*m));
		if (m == NULL) {
			return NSERROR_NOMEM;
		}
		context->m = m;
		context->cap = cap;
	}

	t.start_idx = start_idx;
	t.end_idx = end_idx;
	t.start_box = start_box;
	t.end_box = end_box;
	/* (in document order: the walks find them so -- one out of order kept sorted) */
	i = context->n;
	while (i > 0 && context->m[i - 1].start_idx > start_idx) {
		context->m[i] = context->m[i - 1];
		i--;
	}
	context->m[i] = t;
	context->n++;

	return NSERROR_OK;
}


/* exported interface, documented in content/textsearch.h */
int
content_textsearch_onyx_ranges(struct textsearch_context *textsearch,
			       unsigned start_offset,
			       unsigned end_offset,
			       struct content_textsearch_range *out,
			       int max)
{
	unsigned i;
	int n = 0;

	refresh_matches(textsearch);	/* (a layout since: found again) */
	for (i = first_ending_after(textsearch, start_offset);
	     i < textsearch->n && n < max; i++) {
		const struct ts_match *m = &textsearch->m[i];
		if (m->start_idx >= end_offset)
			break;
		out[n].start = (m->start_idx > start_offset) ?
				m->start_idx - start_offset : 0;
		out[n].end = min(end_offset, m->end_idx) - start_offset;
		out[n].current = (int) i == textsearch->current;
		n++;
	}
	return n;
}


/* exported interface, documented in content/textsearch.h */
bool
content_textsearch_ishighlighted(struct textsearch_context *textsearch,
				 unsigned start_offset,
				 unsigned end_offset,
				 unsigned *start_idx,
				 unsigned *end_idx)
{
	struct content_textsearch_range r;

	if (content_textsearch_onyx_ranges(textsearch, start_offset,
			end_offset, &r, 1) == 0)
		return false;
	*start_idx = r.start;
	*end_idx = r.end;
	return true;
}


/* exported interface, documented in content/textsearch.h */
nserror content_textsearch_destroy(struct textsearch_context *textsearch)
{
	assert(textsearch != NULL);

	if (textsearch->string != NULL) {
		/* broadcast recent query string */
		textsearch_broadcast(textsearch,
				     CONTENT_TEXTSEARCH_RECENT,
				     false,
				     textsearch->string);

		free(textsearch->string);
	}

	/* update back state */
	textsearch_broadcast(textsearch,
			     CONTENT_TEXTSEARCH_BACK,
			     true,
			     NULL);

	/* update forward state */
	textsearch_broadcast(textsearch,
			     CONTENT_TEXTSEARCH_FORWARD,
			     true,
			     NULL);

	free(textsearch->m);
	free(textsearch);

	return NSERROR_OK;
}


/* exported interface, documented in content/content.h */
nserror
content_textsearch(struct hlcache_handle *h,
		   void *context,
		   search_flags_t flags,
		   const char *string)
{
	struct content *c = hlcache_handle_get_content(h);
	nserror res;

	assert(c != NULL);

	if (string != NULL &&
	    c->textsearch.string != NULL &&
	    c->textsearch.context != NULL &&
	    strcmp(string, c->textsearch.string) == 0) {
		/* Continue prev. search */
		content_textsearch_step(c->textsearch.context, flags, string);

	} else if (string != NULL) {
		/* New search */
		free(c->textsearch.string);
		c->textsearch.string = strdup(string);
		if (c->textsearch.string == NULL) {
			return NSERROR_NOMEM;
		}

		if (c->textsearch.context != NULL) {
			/* (the old matches' highlights go) */
			redraw_matches(c->textsearch.context);
			content_textsearch_destroy(c->textsearch.context);
			c->textsearch.context = NULL;
		}

		res = content_textsearch_create(c,
						context,
						&c->textsearch.context);
		if (res != NSERROR_OK) {
			return res;
		}

		content_textsearch_step(c->textsearch.context, flags, string);

	} else {
		/* Clear search */
		content_textsearch__clear(c);

		free(c->textsearch.string);
		c->textsearch.string = NULL;
	}

	return NSERROR_OK;
}


/* exported interface, documented in content/content.h */
nserror content_textsearch_clear(struct hlcache_handle *h)
{
	struct content *c = hlcache_handle_get_content(h);
	assert(c != 0);

	return(content_textsearch__clear(c));
}


/* exported interface, documented in content/textsearch.h */
bool content_textsearch_onyx_state(struct hlcache_handle *h, int *index, int *count)
{
	struct content *c = h != NULL ? hlcache_handle_get_content(h) : NULL;
	struct textsearch_context *ctx;

	*index = -1;
	*count = 0;
	if (c == NULL || c->textsearch.context == NULL)
		return false;
	ctx = c->textsearch.context;
	refresh_matches(ctx);
	*index = ctx->current;
	*count = (int) ctx->n;
	return true;
}
