/*
 * Copyright 2006 James Bursa <bursa@users.sourceforge.net>
 * Copyright 2006 Richard Wilson <info@tinct.net>
 * Copyright 2008 Michael Drake <tlsa@netsurf-browser.org>
 * Copyright 2009 Paul Blokus <paul_pl@users.sourceforge.net>
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
 * implementation of user interaction with a CONTENT_HTML.
 */

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>	/* (Onyx: strcasecmp) */
#include <limits.h>	/* (Onyx: UINT_MAX) */

#include <dom/dom.h>

#include "utils/corestrings.h"
#include "utils/messages.h"
#include "utils/utils.h"
#include "utils/log.h"
#include "utils/nsoption.h"
#include "utils/utf8.h"
#include "netsurf/content.h"
#include "netsurf/browser_window.h"
#include "netsurf/mouse.h"
#include "netsurf/misc.h"
#include "netsurf/layout.h"
#include "netsurf/keypress.h"
#include "netsurf/clipboard.h"	/* (Onyx: a paste's text) */
#include "content/hlcache.h"
#include "content/textsearch.h"
#include "desktop/browser_history.h"
#include "desktop/frames.h"
#include "desktop/scrollbar.h"
#include "desktop/selection.h"
#include "desktop/textarea.h"
#include "javascript/js.h"
#include "desktop/gui_internal.h"

#include "html/box.h"
#include "html/box_textarea.h"
#include "html/box_inspect.h"
#include "html/font.h"
#include "html/form_internal.h"
#include "html/private.h"
#include "html/imagemap.h"
#include "html/interaction.h"
#include "html/onyx_hover.h"
#include "netsurf/onyx_perf.h"
#include "html/onyx_webfont.h"
#include "html/onyx_fx.h"	/* Onyx: the hit test through transforms */
#include "html/html.h"		/* Onyx: html_box_viewport_fixed, html_box_fixed_shift */
#include "netsurf/onyx_jet.h"	/* Onyx: <a download> (docs/06 §38) */

/**
 * Get pointer shape for given box
 *
 * \param box       box in question
 * \param imagemap  whether an imagemap applies to the box
 */

static browser_pointer_shape get_pointer_shape(struct box *box, bool imagemap)
{
	browser_pointer_shape pointer;
	css_computed_style *style;
	enum css_cursor_e cursor;
	lwc_string **cursor_uris;

	if (box->type == BOX_FLOAT_LEFT || box->type == BOX_FLOAT_RIGHT)
		style = box->children->style;
	else
		style = box->style;

	if (style == NULL)
		return BROWSER_POINTER_DEFAULT;

	cursor = css_computed_cursor(style, &cursor_uris);

	switch (cursor) {
	case CSS_CURSOR_AUTO:
		if (box->href || (box->gadget &&
				(box->gadget->type == GADGET_IMAGE ||
				box->gadget->type == GADGET_SUBMIT)) ||
				imagemap) {
			/* link */
			pointer = BROWSER_POINTER_POINT;
		} else if (box->gadget &&
				(box->gadget->type == GADGET_TEXTBOX ||
				box->gadget->type == GADGET_PASSWORD ||
				box->gadget->type == GADGET_TEXTAREA)) {
			/* text input */
			pointer = BROWSER_POINTER_CARET;
		} else {
			/* html content doesn't mind */
			pointer = BROWSER_POINTER_AUTO;
		}
		break;
	case CSS_CURSOR_CROSSHAIR:
		pointer = BROWSER_POINTER_CROSS;
		break;
	case CSS_CURSOR_POINTER:
		pointer = BROWSER_POINTER_POINT;
		break;
	case CSS_CURSOR_MOVE:
		pointer = BROWSER_POINTER_MOVE;
		break;
	case CSS_CURSOR_E_RESIZE:
		pointer = BROWSER_POINTER_RIGHT;
		break;
	case CSS_CURSOR_W_RESIZE:
		pointer = BROWSER_POINTER_LEFT;
		break;
	case CSS_CURSOR_N_RESIZE:
		pointer = BROWSER_POINTER_UP;
		break;
	case CSS_CURSOR_S_RESIZE:
		pointer = BROWSER_POINTER_DOWN;
		break;
	case CSS_CURSOR_NE_RESIZE:
		pointer = BROWSER_POINTER_RU;
		break;
	case CSS_CURSOR_SW_RESIZE:
		pointer = BROWSER_POINTER_LD;
		break;
	case CSS_CURSOR_SE_RESIZE:
		pointer = BROWSER_POINTER_RD;
		break;
	case CSS_CURSOR_NW_RESIZE:
		pointer = BROWSER_POINTER_LU;
		break;
	case CSS_CURSOR_TEXT:
		pointer = BROWSER_POINTER_CARET;
		break;
	case CSS_CURSOR_WAIT:
		pointer = BROWSER_POINTER_WAIT;
		break;
	case CSS_CURSOR_PROGRESS:
		pointer = BROWSER_POINTER_PROGRESS;
		break;
	case CSS_CURSOR_HELP:
		pointer = BROWSER_POINTER_HELP;
		break;
	default:
		pointer = BROWSER_POINTER_DEFAULT;
		break;
	}

	return pointer;
}


/**
 * Start drag scrolling the contents of a box
 *
 * \param box	the box to be scrolled
 * \param x	x ordinate of initial mouse position
 * \param y	y ordinate
 */

/** Onyx: box_coords, where the box is painted (in a fixed box: moved by the scroll) */
static void onyx_box_coords(const html_content *html, struct box *box, int *x, int *y)
{
	int dx, dy;

	box_coords(box, x, y);
	if (html_box_fixed_shift(html, box, &dx, &dy)) {
		*x += dx;
		*y += dy;
	}
}

static void html_box_drag_start(const html_content *html, struct box *box, int x, int y)
{
	int box_x, box_y;
	int scroll_mouse_x, scroll_mouse_y;

	onyx_box_coords(html, box, &box_x, &box_y);

	if (box->scroll_x != NULL) {
		scroll_mouse_x = x - box_x ;
		scroll_mouse_y = y - (box_y + box->padding[TOP] +
				box->height + box->padding[BOTTOM] -
				SCROLLBAR_WIDTH);
		scrollbar_start_content_drag(box->scroll_x,
				scroll_mouse_x, scroll_mouse_y);
	} else if (box->scroll_y != NULL) {
		scroll_mouse_x = x - (box_x + box->padding[LEFT] +
				box->width + box->padding[RIGHT] -
				SCROLLBAR_WIDTH);
		scroll_mouse_y = y - box_y;

		scrollbar_start_content_drag(box->scroll_y,
				scroll_mouse_x, scroll_mouse_y);
	}
}


/**
 * End overflow scroll scrollbar drags
 *
 * \param html   html content
 * \param mouse  state of mouse buttons and modifier keys
 * \param x	 coordinate of mouse
 * \param y	 coordinate of mouse
 * \param dir    Direction of drag
 */
static size_t html_selection_drag_end(struct html_content *html,
		browser_mouse_state mouse, int x, int y, int dir)
{
	int pixel_offset;
	struct box *box;
	int dx, dy;
	size_t idx = 0;

	box = box_pick_text_box(html, x, y, dir, &dx, &dy);
	if (box) {
		plot_font_style_t fstyle;

		font_plot_style_from_css(&html->unit_len_ctx, box->style, &fstyle);

		guit->layout->position(&fstyle, box->text, box->length,
				       dx, &idx, &pixel_offset);

		idx += box->byte_offset;
	}

	return idx;
}


/**
 * Helper for file gadgets to store their filename.
 *
 * Stores the filename unencoded on the dom node associated with the
 * gadget.
 *
 * \todo Get rid of this crap eventually
 *
 * \param operation DOM operation
 * \param key DOM node key being considerd
 * \param _data The data assocated with the key
 * \param src The source DOM node.
 * \param dst The destination DOM node.
 */
static void
html__image_coords_dom_user_data_handler(dom_node_operation operation,
					 dom_string *key,
					 void *_data,
					 struct dom_node *src,
					 struct dom_node *dst)
{
	struct image_input_coords *oldcoords, *coords = _data, *newcoords;

	if (!dom_string_isequal(corestring_dom___ns_key_image_coords_node_data,
				key) || coords == NULL) {
		return;
	}

	switch (operation) {
	case DOM_NODE_CLONED:
		newcoords = calloc(1, sizeof(*newcoords));
		if (newcoords != NULL) {
			*newcoords = *coords;
			if (dom_node_set_user_data(dst,
				 corestring_dom___ns_key_image_coords_node_data,
				 newcoords,
				 html__image_coords_dom_user_data_handler,
				 &oldcoords) == DOM_NO_ERR) {
				free(oldcoords);
			}
		}
		break;

	case DOM_NODE_DELETED:
		free(coords);
		break;

	case DOM_NODE_RENAMED:
	case DOM_NODE_IMPORTED:
	case DOM_NODE_ADOPTED:
		break;

	default:
		NSLOG(netsurf, INFO, "User data operation not handled.");
		assert(0);
	}
}


/**
 * End overflow scroll scrollbar drags
 *
 * \param  scrollbar  scrollbar widget
 * \param  mouse   state of mouse buttons and modifier keys
 * \param  x	   coordinate of mouse
 * \param  y	   coordinate of mouse
 */
static void
html_overflow_scroll_drag_end(struct scrollbar *scrollbar,
			      browser_mouse_state mouse,
			      int x, int y)
{
	int scroll_mouse_x, scroll_mouse_y, box_x, box_y;
	struct html_scrollbar_data *data = scrollbar_get_data(scrollbar);
	struct box *box;

	box = data->box;
	onyx_box_coords((html_content *) data->c, box, &box_x, &box_y);

	if (scrollbar_is_horizontal(scrollbar)) {
		scroll_mouse_x = x - box_x;
		scroll_mouse_y = y - (box_y + box->padding[TOP] +
				box->height + box->padding[BOTTOM] -
				SCROLLBAR_WIDTH);
		scrollbar_mouse_drag_end(scrollbar, mouse,
				scroll_mouse_x, scroll_mouse_y);
	} else {
		scroll_mouse_x = x - (box_x + box->padding[LEFT] +
				box->width + box->padding[RIGHT] -
				SCROLLBAR_WIDTH);
		scroll_mouse_y = y - box_y;
		scrollbar_mouse_drag_end(scrollbar, mouse,
				scroll_mouse_x, scroll_mouse_y);
	}
}


/**
 * handle html mouse action when select menu is open
 *
 */
static nserror
mouse_action_select_menu(html_content *html,
			 struct browser_window *bw,
			 browser_mouse_state mouse,
			 int x, int y)
{
	struct box *box;
	int box_x = 0;
	int box_y = 0;
	const char *status;
	int width, height;
	struct hlcache_handle *bw_content;
	browser_drag_type bw_drag_type;

	assert(html->visible_select_menu != NULL);

	bw_drag_type = browser_window_get_drag_type(bw);
	if (bw_drag_type != DRAGGING_NONE && !mouse) {
		/* drag end: select menu */
		form_select_mouse_drag_end(html->visible_select_menu, mouse, x, y);
	}

	box = html->visible_select_menu->box;
	box_coords(box, &box_x, &box_y);

	box_x -= box->border[LEFT].width;
	box_y += box->height + box->border[BOTTOM].width +
		box->padding[BOTTOM] + box->padding[TOP];

	status = form_select_mouse_action(html->visible_select_menu,
					  mouse,
					  x - box_x,
					  y - box_y);
	if (status != NULL) {
		/* set status if menu still open */
		union content_msg_data msg_data;
		msg_data.explicit_status_text = status;
		content_broadcast((struct content *)html,
				  CONTENT_MSG_STATUS,
				  &msg_data);
		return NSERROR_OK;
	}

	/* close menu and redraw where it was */
	form_select_get_dimensions(html->visible_select_menu, &width, &height);

	html->visible_select_menu = NULL;

	bw_content = browser_window_get_content(bw);
	content_request_redraw(bw_content,
			       box_x,
			       box_y,
			       width,
			       height);
	return NSERROR_OK;
}


/**
 * handle html mouse action when a selection drag is being performed
 *
 */
static nserror
mouse_action_drag_selection(html_content *html,
			    struct browser_window *bw,
			    browser_mouse_state mouse,
			    int x, int y)
{
	struct box *box;
	int dir = -1;
	int dx, dy;
	size_t idx;
	union html_drag_owner drag_owner;
	int pixel_offset;
	plot_font_style_t fstyle;

	if (!mouse) {
		/* End of selection drag */
		if (selection_dragging_start(html->sel)) {
			dir = 1;
		}

		idx = html_selection_drag_end(html, mouse, x, y, dir);

		if (idx != 0) {
			selection_track(html->sel, mouse, idx);
		}

		drag_owner.no_owner = true;
		html_set_drag_type(html, HTML_DRAG_NONE, drag_owner, NULL);

		return NSERROR_OK;
	}

	if (selection_dragging_start(html->sel)) {
		dir = 1;
	}

	box = box_pick_text_box(html, x, y, dir, &dx, &dy);
	if (box != NULL) {
		font_plot_style_from_css(&html->unit_len_ctx, box->style, &fstyle);

		guit->layout->position(&fstyle,
				       box->text,
				       box->length,
				       dx,
				       &idx,
				       &pixel_offset);

		selection_track(html->sel, mouse, box->byte_offset + idx);
	}
	return NSERROR_OK;
}


/**
 * handle html mouse action when a scrollbar drag is being performed
 *
 */
static nserror
mouse_action_drag_scrollbar(html_content *html,
			    struct browser_window *bw,
			    browser_mouse_state mouse,
			    int x, int y)
{
	struct scrollbar *scr;
	struct html_scrollbar_data *data;
	struct box *box;
	int box_x = 0;
	int box_y = 0;
	const char *status;
	int scroll_mouse_x = 0, scroll_mouse_y = 0;
	scrollbar_mouse_status scrollbar_status;

	scr = html->drag_owner.scrollbar;

	if (!mouse) {
		/* drag end: scrollbar */
		html_overflow_scroll_drag_end(scr, mouse, x, y);
	}

	data = scrollbar_get_data(scr);

	box = data->box;

	onyx_box_coords(html, box, &box_x, &box_y);

	if (scrollbar_is_horizontal(scr)) {
		scroll_mouse_x = x - box_x ;
		scroll_mouse_y = y - (box_y + box->padding[TOP] +
				      box->height + box->padding[BOTTOM] -
				      SCROLLBAR_WIDTH);
		scrollbar_status = scrollbar_mouse_action(scr,
							  mouse,
							  scroll_mouse_x,
							  scroll_mouse_y);
	} else {
		scroll_mouse_x = x - (box_x + box->padding[LEFT] +
				      box->width + box->padding[RIGHT] -
				      SCROLLBAR_WIDTH);
		scroll_mouse_y = y - box_y;

		scrollbar_status = scrollbar_mouse_action(scr,
							  mouse,
							  scroll_mouse_x,
							  scroll_mouse_y);
	}
	status = scrollbar_mouse_status_to_message(scrollbar_status);

	if (status != NULL) {
		union content_msg_data msg_data;

		msg_data.explicit_status_text = status;
		content_broadcast((struct content *)html,
				  CONTENT_MSG_STATUS,
				  &msg_data);
	}

	return NSERROR_OK;
}


/**
 * handle mouse actions while dragging in a text area
 */
static nserror
mouse_action_drag_textarea(html_content *html,
			    struct browser_window *bw,
			    browser_mouse_state mouse,
			    int x, int y)
{
	struct box *box;
	int box_x = 0;
	int box_y = 0;

	box = html->drag_owner.textarea;

	assert(box->gadget != NULL);
	assert(box->gadget->type == GADGET_TEXTAREA ||
	       box->gadget->type == GADGET_PASSWORD ||
	       box->gadget->type == GADGET_TEXTBOX);

	onyx_box_coords(html, box, &box_x, &box_y);
	textarea_mouse_action(box->gadget->data.text.ta,
			      mouse,
			      x - box_x,
			      y - box_y);

	/* TODO: Set appropriate statusbar message */
	return NSERROR_OK;
}


/**
 * handle mouse actions while dragging in a content
 */
static nserror
mouse_action_drag_content(html_content *html,
			  struct browser_window *bw,
			  browser_mouse_state mouse,
			  int x, int y)
{
	struct box *box;
	int box_x = 0;
	int box_y = 0;

	box = html->drag_owner.content;
	assert(box->object != NULL);

	onyx_box_coords(html, box, &box_x, &box_y);
	content_mouse_track(box->object,
			    bw, mouse,
			    x - box_x,
			    y - box_y);
	return NSERROR_OK;
}


/**
 * local structure containing all the mouse action state information
 */
struct mouse_action_state {
	struct {
		const char *status; /**< status text */
		browser_pointer_shape pointer; /**< pointer shape */
		enum {
		      ACTION_NONE, /**< default of no action */
		      ACTION_NOSEND, /**< do not send status and pointer message */
		      ACTION_SUBMIT, /**< submit form */
		      ACTION_NAVIGATE, /**< navigate to link url */
		      ACTION_JS, /**< execute link as script */
		      ACTION_BACK, /**< navigate back in history */
		      ACTION_FORWARD, /**< navigate forward in history */
		} action;
	} result;

	/** dom node */
	struct dom_node *node;

	/** html object */
	struct {
		struct box *box;
		int pos_x;
		int pos_y;
	} html_object;

	/** non html object */
	hlcache_handle *object;

	/** iframe */
	struct browser_window *iframe;

	/** link either from href or imagemap */
	struct {
		struct box *box;
		nsurl *url;
		const char *target;
		bool is_imagemap;
	} link;

	/** gadget */
	struct {
		struct form_control *control;
		struct box *box;
		int box_x;
		int box_y;
		const char *target;
	} gadget;

	/** title */
	const char *title;

	/** candidate box for drag operation */
	struct box *drag_candidate;

	/** scrollbar */
	struct {
		struct scrollbar *bar;
		int mouse_x;
		int mouse_y;
	} scroll;

	/** text in box */
	struct {
		struct box *box;
		int box_x;
	} text;
};


/**
 * Onyx: the inline element a text box is in -- the innermost one open before it in its
 * inline container (INLINE ... INLINE_END around it) -- or NULL (the block's text).
 */
static dom_node *html_text_box_element(struct box *text)
{
	struct box *b, *open[32];
	int depth = 0;

	if (text->parent == NULL)
		return NULL;
	for (b = text->parent->children; b != NULL && b != text; b = b->next) {
		if (b->type == BOX_INLINE && b->inline_end != NULL) {
			if (depth < 32)
				open[depth] = b;
			depth++;
		} else if (b->type == BOX_INLINE_END && depth > 0) {
			depth--;
		}
	}
	if (depth == 0)
		return NULL;
	return open[(depth < 32 ? depth : 32) - 1]->node;
}

/* ---- Onyx: the box under the pointer as painted ----------------------------------------
 * html_redraw paints a positioned box after the rest of its layer (redraw.c): the box
 * under the pointer is the last one painted there, which the same walk finds -- not the
 * last in the tree's order (a menu open over the page: its links, not the page's).
 */

/** a box put off, as html_redraw does */
struct onyx_hit_off {
	struct box *box;
	int x, y;		/* its parent's origin */
	struct onyx_layer_key key;	/* its z-index, its tree order */
};

struct onyx_hit {
	const html_content *html;
	int px, py;			/* the point */
	int vsx, vsy;			/* (the viewport's scroll: the fixed boxes) */
	struct box *box;		/* the last box painted under it */
	struct onyx_hit_off *off;
	int n, cap;
	struct onyx_hit_off *hoist;	/* (redraw.c's onyx_hoist_*) */
	int nh, caph;
	/* Onyx: a static box clipping its overflow, the point outside it: only the
	 * absolute boxes in it whose containing block is outside it are looked at
	 * (redraw.c's onyx_oclip_escape) */
	const struct box *escape;
};

static void onyx_hit_children(struct onyx_hit *h, struct box *box, int ox, int oy);
static void onyx_hit_layer(struct onyx_hit *h, int start, bool context, double lo,
		double span);

/** whether a box is visible -- an anonymous box (no style: an inline container...)
 * as its parent (visibility: hidden made the boxes of a hidden fixed panel's text
 * take the clicks: google.com's search overlay) */
static bool onyx_hit_visible(const struct box *box)
{
	while (box != NULL && box->style == NULL)
		box = box->parent;
	/* (Onyx: and pointer-events: none -- an overlay the clicks go through, as
	 * MediaWiki's notification area over the whole page: inherited, so its
	 * descendants too unless they set auto) */
	return box == NULL ||
		(css_computed_visibility(box->style) != CSS_VISIBILITY_HIDDEN &&
		 css_computed_pointer_events(box->style) != CSS_POINTER_EVENTS_NONE);
}

/** whether a box clipping its overflow, not a containing block, may hold absolute boxes
 * (whose containing block is outside it) at the point (its coordinates) */
static bool onyx_hit_escapes(const struct box *box, int x, int y)
{
	if (box->style == NULL || box->parent == NULL ||
	    css_computed_position(box->style) != CSS_POSITION_STATIC ||
	    (onyx_fx_style(box->style) && onyx_fx_box(box)) ||
	    (css_computed_overflow_x(box->style) == CSS_OVERFLOW_VISIBLE &&
	     css_computed_overflow_y(box->style) == CSS_OVERFLOW_VISIBLE))
		return false;
	return box->descendant_x0 <= x && x < box->descendant_x1 &&
		box->descendant_y0 <= y && y < box->descendant_y1;
}

/** a box, its parent's origin at (ox, oy) (html_redraw_box) */
static void onyx_hit_box(struct onyx_hit *h, struct box *box, int ox, int oy)
{
	int bx = ox + box->x, by = oy + box->y;
	bool physically = false;
	float m[6], inv[6];

	/* Onyx: a fixed box is where the viewport is, as painted (redraw.c) -- it was hit
	 * where it is laid out (the scroll offset 0): once the page was scrolled, the
	 * pointer missed a fixed dialog's scroller, buttons and fields (docs/06 §41) */
	if ((h->vsx != 0 || h->vsy != 0) && box->style != NULL &&
	    css_computed_position(box->style) == CSS_POSITION_FIXED &&
	    html_box_viewport_fixed(box)) {
		bx += h->vsx;
		by += h->vsy;
	}

	/* Onyx: a transformed box (html/onyx_fx.c) -- the point taken back through its
	 * matrix, for it and all its stacking context holds (what it puts off) */
	if (onyx_fx_box(box) && onyx_box_matrix(box->style, &h->html->unit_len_ctx, box, 1,
			m)) {
		int px = h->px, py = h->py, start = h->n;
		float lx = px - bx, ly = py - by;

		if (!onyx_matrix_invert(m, inv))
			return;
		h->px = bx + (int) floorf(inv[0] * lx + inv[2] * ly + inv[4]);
		h->py = by + (int) floorf(inv[1] * lx + inv[3] * ly + inv[5]);
		if (box_contains_point(&h->html->unit_len_ctx, box, h->px - bx,
				h->py - by, &physically)) {
			if (physically && onyx_hit_visible(box))
				h->box = box;
			onyx_hit_children(h, box, bx - scrollbar_get_offset(box->scroll_x),
					by - scrollbar_get_offset(box->scroll_y));
			onyx_hit_layer(h, start, true, 0, 1);
		}
		h->n = start;
		h->px = px;
		h->py = py;
		return;
	}

	if (!box_contains_point(&h->html->unit_len_ctx, box, h->px - bx,
			h->py - by, &physically)) {
		/* Onyx: a static box clipping its overflow: its absolute boxes whose
		 * containing block is outside it, outside it too (Codex's menu footer) */
		if (onyx_hit_escapes(box, h->px - bx, h->py - by)) {
			const struct box *was = h->escape;

			h->escape = box;
			onyx_hit_children(h, box, bx - scrollbar_get_offset(box->scroll_x),
					by - scrollbar_get_offset(box->scroll_y));
			h->escape = was;
		}
		return;
	}
	if (physically && h->escape == NULL && onyx_hit_visible(box))	/* (Onyx) */
		h->box = box;
	onyx_hit_children(h, box, bx - scrollbar_get_offset(box->scroll_x),
			by - scrollbar_get_offset(box->scroll_y));
}

/** a stacking context's boxes with a negative z-index (redraw.c's onyx_negz_paint: under
 * its in-flow content), its children's origin at (ox, oy) */
static void onyx_hit_negz(struct onyx_hit *h, struct box *box, int ox, int oy)
{
	struct onyx_negz *a;
	int cx = ox + scrollbar_get_offset(box->scroll_x);	/* (the box's origin) */
	int cy = oy + scrollbar_get_offset(box->scroll_y);
	struct rect cull = { h->px - cx, h->py - cy, h->px - cx + 1, h->py - cy + 1 };
	int n, i;

	/* (a fixed box is hit where the viewport is: no culling then) */
	n = html_redraw_negz(box, (h->vsx != 0 || h->vsy != 0) ? NULL : &cull, &a);
	for (i = 0; i < n; i++) {
		const struct box *was = h->escape;
		int start = h->n;

		if (h->escape != NULL && (a[i].box->style == NULL ||
				css_computed_position(a[i].box->style) !=
				CSS_POSITION_ABSOLUTE))
			continue;	/* (outside a clipping box: its absolute boxes) */
		if (a[i].clipped && (h->px - cx < a[i].clip.x0 ||
				h->px - cx >= a[i].clip.x1 ||
				h->py - cy < a[i].clip.y0 ||
				h->py - cy >= a[i].clip.y1))
			continue;
		h->escape = NULL;	/* (a layer: its own clip) */
		onyx_hit_box(h, a[i].box, cx + a[i].dx, cy + a[i].dy);
		h->escape = was;
		if (h->n > start)
			onyx_hit_layer(h, start, true, 0, 1);
		h->n = start;
	}
	free(a);
}

/** a box's children (html_redraw_box_children): the positioned ones put off */
static void onyx_hit_children(struct onyx_hit *h, struct box *box, int ox, int oy)
{
	struct box *c;
	int32_t z;

	/* Onyx: a stacking context: its boxes with a negative z-index first (under) */
	if (html_redraw_stacking_context(box))
		onyx_hit_negz(h, box, ox, oy);
	for (c = box->children; c != NULL; c = c->next) {
		if (c->type == BOX_FLOAT_LEFT || c->type == BOX_FLOAT_RIGHT)
			continue;
		if (html_redraw_negz_box(c))
			continue;	/* (Onyx: its stacking context's: onyx_hit_negz) */
		if (html_redraw_layer_z(c, &z)) {
			/* (Onyx: outside a clipping box, only its absolute boxes) */
			if (h->escape != NULL && (c->style == NULL ||
					css_computed_position(c->style) !=
					CSS_POSITION_ABSOLUTE))
				continue;
			if (h->n == h->cap) {
				int cap = h->cap ? h->cap * 2 : 32;
				struct onyx_hit_off *off = realloc(h->off,
						cap * sizeof(*off));
				if (off == NULL) {
					onyx_hit_box(h, c, ox, oy);
					continue;
				}
				h->off = off;
				h->cap = cap;
			}
			h->off[h->n].box = c;
			h->off[h->n].x = ox;
			h->off[h->n].y = oy;
			h->off[h->n].key.z = z;
			h->n++;
			continue;
		}
		onyx_hit_box(h, c, ox, oy);
	}
	for (c = box->float_children; c != NULL; c = c->next_float)
		onyx_hit_box(h, c, ox, oy);
}

static int onyx_hit_cmp(const void *a, const void *b)
{
	const struct onyx_hit_off *x = a, *y = b;

	return onyx_layer_key_cmp(&x->key, &y->key);
}

/** room for one more (false: none) */
static bool onyx_hit_room(struct onyx_hit_off **a, int n, int *cap)
{
	struct onyx_hit_off *off;
	int c;

	if (n < *cap)
		return true;
	c = *cap ? *cap * 2 : 32;
	off = realloc(*a, c * sizeof(*off));
	if (off == NULL)
		return false;
	*a = off;
	*cap = c;
	return true;
}

/** the boxes a layer put off (from start on), in their painting order (redraw.c's
 * onyx_layer_paint_in: a z-index auto box gives those above 0 to its stacking context) */
static void onyx_hit_layer(struct onyx_hit *h, int start, bool context, double lo,
		double span)
{
	int end = h->n, n = end - start, h0 = h->nh, i, k;

	for (i = start; i < end; i++) {
		h->off[i].key.lo = lo + span * (i - start + 1) / (n + 1);
		h->off[i].key.span = span / (n + 1);
	}
	if (!context) {
		for (i = k = start; i < end; i++) {
			if (h->off[i].key.z > 0 && onyx_hit_room(&h->hoist, h->nh, &h->caph))
				h->hoist[h->nh++] = h->off[i];
			else
				h->off[k++] = h->off[i];
		}
		end = h->n = k;
		h0 = h->nh;
	}
	if (end - start > 1)
		qsort(h->off + start, end - start, sizeof(*h->off), onyx_hit_cmp);
	for (i = start; i < end; i++) {
		struct onyx_hit_off e = h->off[i];
		const struct box *was = h->escape;

		h->escape = NULL;	/* (a box put off: its own clip) */
		onyx_hit_box(h, e.box, e.x, e.y);
		h->escape = was;
		if (h->n > end)
			onyx_hit_layer(h, end, html_redraw_layer_context(e.box),
					e.key.lo, e.key.span);
		h->n = end;
		while (context && h->nh > h0) {
			struct onyx_hit_off o = h->hoist[--h->nh];

			if (!onyx_hit_room(&h->off, end, &h->cap))
				continue;
			for (k = end; k > i + 1 &&
					onyx_layer_key_cmp(&o.key, &h->off[k - 1].key) < 0; k--)
				h->off[k] = h->off[k - 1];
			h->off[k] = o;
			h->n = ++end;
		}
	}
	h->n = start;
}

/**
 * The boxes from the root down to the one painted last under the point (its path in the
 * tree, root first; the root alone if none): a malloc'd array, its length in *n.
 */
static struct box **onyx_hit_path(html_content *html, int x, int y, int *n)
{
	struct onyx_hit h;
	struct box **path, *b;
	int depth = 0, i;

	memset(&h, 0, sizeof(h));
	h.html = html;
	h.px = x;
	h.py = y;
	if (html->bw != NULL) {
		int w, ht;
		browser_window_onyx_viewport(html->bw, &h.vsx, &h.vsy, &w, &ht);
	}
	onyx_hit_box(&h, html->layout, 0, 0);
	onyx_hit_layer(&h, 0, true, 0, 1);
	free(h.off);
	free(h.hoist);
	if (h.box == NULL)
		h.box = html->layout;

	for (b = h.box; b != NULL; b = b->parent)
		depth++;
	path = malloc(depth * sizeof(*path));
	if (path == NULL) {
		*n = 0;
		return NULL;
	}
	for (b = h.box, i = depth; b != NULL; b = b->parent)
		path[--i] = b;
	*n = depth;
	return path;
}

/* exported interface documented in html/private.h */
struct box **html_hit_path(html_content *html, int x, int y, int *n)
{
	if (html->layout == NULL) {
		*n = 0;
		return NULL;
	}
	return onyx_hit_path(html, x, y, n);
}

/**
 * iterate the box tree for deepest node at coordinates
 *
 * extracts mouse action node information by descending through
 *  visible boxes setting more specific values for:
 *
 * box - deepest box at point
 * html_object_box - html object
 * html_object_pos_x - html object
 * html_object_pos_y - html object
 * object - non html object
 * iframe - iframe
 * url - href or imagemap
 * target - href or imagemap or gadget
 * url_box - href or imagemap
 * imagemap - imagemap
 * gadget - gadget
 * gadget_box - gadget
 * gadget_box_x - gadget
 * gadget_box_y - gadget
 * title - title
 * pointer
 *
 * drag_candidate - first box with scroll
 * padding_left - box with scroll
 * padding_right
 * padding_top
 * padding_bottom
 * scrollbar - inside padding box stops decent
 * scroll_mouse_x - inside padding box stops decent
 * scroll_mouse_y - inside padding box stops decent
 *
 * text_box - text box
 * text_box_x - text_box
 */
static nserror
get_mouse_action_node(html_content *html,
		      int x, int y,
		      struct mouse_action_state *man)
{
	struct box *box;
	struct box **path;
	int path_n = 0, path_i;
	int box_x = 0;
	int box_y = 0;
	int fixed_dx = 0, fixed_dy = 0;	/* (Onyx: in a fixed box) */
	bool in_fixed = false;

	/* initialise the mouse action state data */
	memset(man, 0, sizeof(struct mouse_action_state));
	man->node = html->layout->node; /* Default dom node to the <HTML> */
	man->result.pointer = BROWSER_POINTER_DEFAULT;

	/* search the box tree for a link, imagemap, form control, or
	 * box with scrollbars -- Onyx: along the path to the box painted last
	 * under the pointer (onyx_hit_path), from the root down
	 */
	path = onyx_hit_path(html, x, y, &path_n);
	for (path_i = 0; path_i < path_n; path_i++) {
		box = path[path_i];
		box_coords(box, &box_x, &box_y);
		/* Onyx: from a fixed box down, where they are painted */
		if (!in_fixed && box->style != NULL &&
		    css_computed_position(box->style) == CSS_POSITION_FIXED)
			in_fixed = html_box_fixed_shift(html, box, &fixed_dx, &fixed_dy);
		box_x += fixed_dx - scrollbar_get_offset(box->scroll_x);
		box_y += fixed_dy - scrollbar_get_offset(box->scroll_y);

		/* skip hidden boxes */
		if ((box->style != NULL) &&
		    (css_computed_visibility(box->style) ==
		     CSS_VISIBILITY_HIDDEN)) {
			goto next_box;
		}

		if (box->node != NULL) {
			man->node = box->node;
		} else if (box->type == BOX_TEXT) {
			/* Onyx: a text's element: the inline one it is in (a link's
			 * text: the <a>), which the page's scripts get the click at */
			dom_node *element = html_text_box_element(box);
			if (element != NULL)
				man->node = element;
		}

		if (box->object) {
			if (content_get_type(box->object) == CONTENT_HTML) {
				man->html_object.box = box;
				man->html_object.pos_x = box_x;
				man->html_object.pos_y = box_y;
			} else {
				man->object = box->object;
			}
		}

		if (box->iframe) {
			man->iframe = box->iframe;
		}

		if (box->href) {
			man->link.url = box->href;
			man->link.target = box->target;
			man->link.box = box;
			man->link.is_imagemap = false;
		}

		if (box->usemap) {
			man->link.url = imagemap_get(html,
						     box->usemap,
						     box_x,
						     box_y,
						     x, y,
						     &man->link.target);
			man->link.box = box;
			man->link.is_imagemap = true;
		}

		if (box->gadget) {
			man->gadget.control = box->gadget;
			man->gadget.box = box;
			man->gadget.box_x = box_x;
			man->gadget.box_y = box_y;
			if (box->gadget->form) {
				man->gadget.target = box->gadget->form->target;
			}
		}

		if (box->title) {
			man->title = box->title;
		}

		man->result.pointer = get_pointer_shape(box, false);

		if ((box->scroll_x != NULL) ||
		    (box->scroll_y != NULL)) {
			int padding_left;
			int padding_right;
			int padding_top;
			int padding_bottom;

			if (man->drag_candidate == NULL) {
				man->drag_candidate = box;
			}

			padding_left = box_x +
					scrollbar_get_offset(box->scroll_x);
			padding_right = padding_left + box->padding[LEFT] +
					box->width + box->padding[RIGHT];
			padding_top = box_y +
					scrollbar_get_offset(box->scroll_y);
			padding_bottom = padding_top + box->padding[TOP] +
					box->height + box->padding[BOTTOM];

			if ((x > padding_left) &&
			    (x < padding_right) &&
			    (y > padding_top) &&
			    (y < padding_bottom)) {
				/* mouse inside padding box */

				if ((box->scroll_y != NULL) &&
				    (x > (padding_right - SCROLLBAR_WIDTH))) {
					/* mouse above vertical box scroll */

					man->scroll.bar = box->scroll_y;
					man->scroll.mouse_x = x - (padding_right - SCROLLBAR_WIDTH);
					man->scroll.mouse_y = y - padding_top;
					break;

				} else if ((box->scroll_x != NULL) &&
					   (y > (padding_bottom -
							SCROLLBAR_WIDTH))) {
					/* mouse above horizontal box scroll */

					man->scroll.bar = box->scroll_x;
					man->scroll.mouse_x = x - padding_left;
					man->scroll.mouse_y = y - (padding_bottom - SCROLLBAR_WIDTH);
					break;
				}
			}
		}

		if (box->text && !box->object) {
			man->text.box = box;
			man->text.box_x = box_x;
		}

	next_box:
		;
	}
	free(path);

	/* use of box_x, box_y, or content below this point is probably a
	 * mistake; they will refer to the last box of the path */

	assert(man->node != NULL);

	return NSERROR_OK;
}


/**
 * process mouse activity on a form gadget
 */
static nserror
gadget_mouse_action(html_content *html,
		    browser_mouse_state mouse,
		    int x, int y,
		    struct mouse_action_state *mas)
{
	struct content *c = (struct content *)html;
	textarea_mouse_status ta_status;
	union content_msg_data msg_data;
	nserror res;
	bool click;
	click = mouse & (BROWSER_MOUSE_PRESS_1 | BROWSER_MOUSE_PRESS_2 |
			 BROWSER_MOUSE_CLICK_1 | BROWSER_MOUSE_CLICK_2 |
			 BROWSER_MOUSE_DRAG_1 | BROWSER_MOUSE_DRAG_2);

	switch (mas->gadget.control->type) {
	case GADGET_SELECT:
		mas->result.status = messages_get("FormSelect");
		mas->result.pointer = BROWSER_POINTER_MENU;
		if (mouse & BROWSER_MOUSE_CLICK_1 &&
		    nsoption_bool(core_select_menu)) {
			html->visible_select_menu = mas->gadget.control;
			res = form_open_select_menu(c,
						    mas->gadget.control,
						    form_select_menu_callback,
						    c);
			if (res != NSERROR_OK) {
				NSLOG(netsurf, ERROR, "%s",
				      messages_get_errorcode(res));
				html->visible_select_menu = NULL;
			}
			mas->result.pointer = BROWSER_POINTER_DEFAULT;
		} else if (mouse & BROWSER_MOUSE_CLICK_1) {
			msg_data.select_menu.gadget = mas->gadget.control;
			content_broadcast(c,
					  CONTENT_MSG_SELECTMENU,
					  &msg_data);
		}
		break;

	case GADGET_CHECKBOX:
		mas->result.status = messages_get("FormCheckbox");
		if (mouse & BROWSER_MOUSE_CLICK_1) {
			mas->gadget.control->selected = !mas->gadget.control->selected;
			dom_html_input_element_set_checked(
				(dom_html_input_element *)(mas->gadget.control->node),
				mas->gadget.control->selected);
			html__redraw_a_box(html, mas->gadget.box);
			/* Onyx: its :checked styles */
			html_state_restyle(html, mas->gadget.control->node);
		}
		break;

	case GADGET_RADIO:
		mas->result.status = messages_get("FormRadio");
		if (mouse & BROWSER_MOUSE_CLICK_1) {
			form_radio_set(mas->gadget.control);
		}
		break;

	case GADGET_IMAGE:
		/* This falls through to SUBMIT */
		if (mouse & BROWSER_MOUSE_CLICK_1) {
			struct image_input_coords *coords, *oldcoords;
			/** \todo Find a way to not ignore errors */
			coords = calloc(1, sizeof(*coords));
			if (coords == NULL) {
				return NSERROR_OK;
			}
			coords->x = x - mas->gadget.box_x;
			coords->y = y - mas->gadget.box_y;
			if (dom_node_set_user_data(
				mas->gadget.control->node,
				corestring_dom___ns_key_image_coords_node_data,
				coords,
				html__image_coords_dom_user_data_handler,
				&oldcoords) != DOM_NO_ERR) {
				return NSERROR_OK;
			}
			free(oldcoords);
		}
		fallthrough;

	case GADGET_SUBMIT:
		if (mas->gadget.control->form) {
			static char status_buffer[200];

			snprintf(status_buffer,
				 sizeof status_buffer,
				 messages_get("FormSubmit"),
				 mas->gadget.control->form->action);
			mas->result.status = status_buffer;
			mas->result.pointer = get_pointer_shape(mas->gadget.box,
								false);
			if (mouse & (BROWSER_MOUSE_CLICK_1 |
				     BROWSER_MOUSE_CLICK_2)) {
				mas->result.action = ACTION_SUBMIT;
			}
		} else {
			mas->result.status = messages_get("FormBadSubmit");
		}
		break;

	case GADGET_TEXTBOX:
	case GADGET_PASSWORD:
	case GADGET_TEXTAREA:
		if (mas->gadget.control->type == GADGET_TEXTAREA) {
			mas->result.status = messages_get("FormTextarea");
		} else {
			mas->result.status = messages_get("FormTextbox");
		}

		if (click &&
		    (html->selection_type != HTML_SELECTION_TEXTAREA ||
		     html->selection_owner.textarea != mas->gadget.box)) {
			union html_selection_owner sel_owner;
			sel_owner.none = true;
			html_set_selection(html,
					   HTML_SELECTION_NONE,
					   sel_owner,
					   true);
		}

		ta_status = textarea_mouse_action(mas->gadget.control->data.text.ta,
						  mouse,
						  x - mas->gadget.box_x,
						  y - mas->gadget.box_y);

		if (ta_status & TEXTAREA_MOUSE_EDITOR) {
			mas->result.pointer = get_pointer_shape(mas->gadget.box, false);
		} else {
			mas->result.pointer = BROWSER_POINTER_DEFAULT;
			mas->result.status = scrollbar_mouse_status_to_message(ta_status >> 3);
		}
		break;

	case GADGET_HIDDEN:
		/* not possible: no box generated */
		break;

	case GADGET_RESET:
		mas->result.status = messages_get("FormReset");
		break;

	case GADGET_FILE:
		mas->result.status = messages_get("FormFile");
		if (mouse & BROWSER_MOUSE_CLICK_1) {
			msg_data.gadget_click.gadget = mas->gadget.control;
			content_broadcast(c,
					  CONTENT_MSG_GADGETCLICK,
					  &msg_data);
		}
		break;

	case GADGET_BUTTON:
		/* This gadget cannot be activated */
		mas->result.status = messages_get("FormButton");
		break;
	}

	return NSERROR_OK;
}


/**
 * process mouse activity on an iframe
 */
static nserror
iframe_mouse_action(struct browser_window *bw,
		    browser_mouse_state mouse,
		    int x, int y,
		    struct mouse_action_state *mas)
{
	int pos_x, pos_y;
	float scale;

	scale = browser_window_get_scale(bw);

	browser_window_get_position(mas->iframe, false, &pos_x, &pos_y);

	if (mouse & BROWSER_MOUSE_CLICK_1 ||
	    mouse & BROWSER_MOUSE_CLICK_2) {
		browser_window_mouse_click(mas->iframe,
					   mouse,
					   (x * scale) - pos_x,
					   (y * scale) - pos_y);
	} else {
		browser_window_mouse_track(mas->iframe,
					   mouse,
					   (x * scale) - pos_x,
					   (y * scale) - pos_y);
	}
	mas->result.action = ACTION_NOSEND;

	return NSERROR_OK;
}


/**
 * process mouse activity on an html object
 */
static nserror
html_object_mouse_action(html_content *html,
			 struct browser_window *bw,
			 browser_mouse_state mouse,
			 int x, int y,
			 struct mouse_action_state *mas)
{
	bool click;
	click = mouse & (BROWSER_MOUSE_PRESS_1 | BROWSER_MOUSE_PRESS_2 |
			 BROWSER_MOUSE_CLICK_1 | BROWSER_MOUSE_CLICK_2 |
			 BROWSER_MOUSE_DRAG_1 | BROWSER_MOUSE_DRAG_2);

	if (click &&
	    (html->selection_type != HTML_SELECTION_CONTENT ||
	     html->selection_owner.content != mas->html_object.box)) {
		union html_selection_owner sel_owner;
		sel_owner.none = true;
		html_set_selection(html, HTML_SELECTION_NONE, sel_owner, true);
	}

	if (mouse & BROWSER_MOUSE_CLICK_1 ||
	    mouse & BROWSER_MOUSE_CLICK_2) {
		content_mouse_action(mas->html_object.box->object,
				     bw,
				     mouse,
				     x - mas->html_object.pos_x,
				     y - mas->html_object.pos_y);
	} else {
		content_mouse_track(mas->html_object.box->object,
				    bw,
				    mouse,
				    x - mas->html_object.pos_x,
				    y - mas->html_object.pos_y);
	}

	mas->result.action = ACTION_NOSEND;
	return NSERROR_OK;
}


/**
 * determine if a url has a javascript scheme
 *
 * \param urm The url to check.
 * \return true if the url is a javascript scheme else false
 */
static bool is_javascript_navigate_url(nsurl *url)
{
	bool is_js = false;
	lwc_string *scheme;

	scheme = nsurl_get_component(url, NSURL_SCHEME);
	if (scheme != NULL) {
		if (scheme == corestring_lwc_javascript) {
			is_js = true;
		}
		lwc_string_unref(scheme);
	}
	return is_js;
}


/**
 * process mouse activity on a link
 */
static nserror
link_mouse_action(html_content *html,
		  struct browser_window *bw,
		  browser_mouse_state mouse,
		  int x, int y,
		  struct mouse_action_state *mas)
{
	nserror res;
	char *url_s = NULL;
	size_t url_l = 0;
	static char status_buffer[200];
	union content_msg_data msg_data;

	if (nsoption_bool(display_decoded_idn) == true) {
		res = nsurl_get_utf8(mas->link.url, &url_s, &url_l);
		if (res != NSERROR_OK) {
			/* Unable to obtain a decoded IDN. This is not
			 *  a fatal error.  Ensure the string pointer
			 *  is NULL so we use the encoded version.
			 */
			url_s = NULL;
		}
	}

	if (mas->title) {
		snprintf(status_buffer,
			 sizeof status_buffer,
			 "%s: %s",
			 url_s ? url_s : nsurl_access(mas->link.url),
			 mas->title);
	} else {
		snprintf(status_buffer,
			 sizeof status_buffer,
			 "%s",
			 url_s ? url_s : nsurl_access(mas->link.url));
	}

	if (url_s != NULL) {
		free(url_s);
	}

	mas->result.status = status_buffer;

	mas->result.pointer = get_pointer_shape(mas->link.box,
						mas->link.is_imagemap);

	if (mouse & BROWSER_MOUSE_CLICK_1 &&
	    mouse & BROWSER_MOUSE_MOD_1) {
		/* force download of link */
		browser_window_navigate(bw,
					mas->link.url,
					content_get_url((struct content *)html),
					BW_NAVIGATE_DOWNLOAD,
					NULL,
					NULL,
					NULL);

	} else if (mouse & BROWSER_MOUSE_CLICK_2 &&
		   mouse & BROWSER_MOUSE_MOD_1) {
		msg_data.savelink.url = mas->link.url;
		msg_data.savelink.title = mas->title;
		content_broadcast((struct content *)html,
				  CONTENT_MSG_SAVELINK,
				  &msg_data);

	} else if (mouse & (BROWSER_MOUSE_CLICK_1 | BROWSER_MOUSE_CLICK_2)) {
		if (is_javascript_navigate_url(mas->link.url)) {
			mas->result.action = ACTION_JS;
		} else {
			mas->result.action = ACTION_NAVIGATE;
		}
	}

	return NSERROR_OK;
}


/**
 * Onyx (docs/06 §38): a link with a download attribute (<a download>, <a download="name">)
 * is saved, not opened -- a download, its name the attribute's (a Content-Disposition
 * filename wins); a blob: link's bytes are the page script's (html5.js:
 * __onyxBlobDownload). False: not such a link.
 */
static bool onyx_link_download(struct content *c, struct browser_window *bw,
		struct box *box, nsurl *url, nserror *res)
{
	static dom_string *attr_download;
	dom_node *n, *next;
	dom_string *name = NULL, *v = NULL;
	char *hint = NULL;
	lwc_string *scheme;
	bool found = false;
	int depth;

	if (box == NULL || url == NULL)
		return false;
	if (attr_download == NULL &&
	    dom_string_create((const uint8_t *) "download", 8, &attr_download) != DOM_NO_ERR)
		return false;
	n = box->node != NULL ? dom_node_ref(box->node) : NULL;
	for (depth = 0; n != NULL && depth < 32 && !found; depth++) {
		dom_node_type type;
		if (dom_node_get_node_type(n, &type) == DOM_NO_ERR &&
		    type == DOM_ELEMENT_NODE &&
		    dom_node_get_node_name(n, &name) == DOM_NO_ERR && name != NULL) {
			bool a = dom_string_caseless_lwc_isequal(name, corestring_lwc_a);
			dom_string_unref(name);
			name = NULL;
			if (a) {
				if (dom_element_get_attribute(n, attr_download, &v) == DOM_NO_ERR &&
				    v != NULL) {
					found = true;
					hint = strndup(dom_string_data(v), dom_string_byte_length(v));
					dom_string_unref(v);
				}
				break;	/* (the nearest link: its attribute or none) */
			}
		}
		next = NULL;
		dom_node_get_parent_node(n, &next);
		dom_node_unref(n);
		n = next;
	}
	if (n != NULL)
		dom_node_unref(n);
	if (!found)
		return false;
	scheme = nsurl_get_component(url, NSURL_SCHEME);
	if (scheme != NULL && strcasecmp(lwc_string_data(scheme), "blob") == 0) {
		/* the page's script made the bytes: its blob: URL is html5.js's */
		const char *u = nsurl_access(url), *h = hint != NULL ? hint : "";
		size_t len = 64 + 2 * strlen(u) + 6 * strlen(h);
		char *src = malloc(len), *o = src;
		const char *parts[2] = { u, h };
		int i;
		if (src != NULL) {
			o += sprintf(o, "__onyxBlobDownload(");
			for (i = 0; i < 2; i++) {
				const unsigned char *q;
				*o++ = '"';
				for (q = (const unsigned char *) parts[i]; *q; q++) {
					if (*q == '"' || *q == '\\') {
						*o++ = '\\';
						*o++ = (char) *q;
					} else if (*q < 0x20) {
						o += sprintf(o, "\\u%04x", *q);
					} else {
						*o++ = (char) *q;
					}
				}
				*o++ = '"';
				*o++ = i == 0 ? ',' : ')';
			}
			*o = '\0';
			html_exec(c, src, o - src);
			free(src);
		}
		*res = NSERROR_OK;
	} else {
		download_onyx_hint(hint);
		*res = browser_window_navigate(bw, url, content_get_url(c),
				BW_NAVIGATE_DOWNLOAD, NULL, NULL, NULL);
		download_onyx_hint(NULL);	/* (taken, or not: never the next one's) */
	}
	if (scheme != NULL)
		lwc_string_unref(scheme);
	free(hint);
	return true;
}


static nserror
default_mouse_action_focus(html_content *html, browser_mouse_state mouse)
{
	/* Onyx: only a press moves the focus, as in the browsers (the focus is
	 * mousedown's default action): a release, a drag or a hold elsewhere keeps it --
	 * a script that moves the focused field away on mousedown (google.com's search
	 * overlay) had the release land beside it and the caret taken out */
	if (mouse & (BROWSER_MOUSE_PRESS_1 | BROWSER_MOUSE_PRESS_2)) {
		/* ensure key presses still act on the browser window */
		union html_focus_owner fo;
		fo.self = true;
		html_set_focus(html, HTML_FOCUS_SELF, fo, true, 0, 0, 0, NULL);
	}

	return NSERROR_OK;
}


/**
 * process mouse activity if it is not anything else
 */
static nserror
default_mouse_action(html_content *html,
		  struct browser_window *bw,
		  browser_mouse_state mouse,
		  int x, int y,
		  struct mouse_action_state *mas)
{
	struct content *c = (struct content *)html;

	/* frame resizing */
	if (browser_window_frame_resize_start(bw, mouse, x, y, &mas->result.pointer)) {
		if (mouse & (BROWSER_MOUSE_DRAG_1 | BROWSER_MOUSE_DRAG_2)) {
			mas->result.status = messages_get("FrameDrag");
		}
		return default_mouse_action_focus(html, mouse);
	}

	/* clicking in the main page removes the selection from any text areas.
	 */
	union html_selection_owner sel_owner;
	bool click;
	click = mouse & (BROWSER_MOUSE_PRESS_1 | BROWSER_MOUSE_PRESS_2 |
			 BROWSER_MOUSE_CLICK_1 | BROWSER_MOUSE_CLICK_2 |
			 BROWSER_MOUSE_DRAG_1 | BROWSER_MOUSE_DRAG_2);

	if ((mouse & (BROWSER_MOUSE_PRESS_1 | BROWSER_MOUSE_PRESS_2)) &&
	    html->focus_type != HTML_FOCUS_SELF) {	/* (Onyx: a press only, above) */
		union html_focus_owner fo;
		fo.self = true;
		html_set_focus(html, HTML_FOCUS_SELF, fo, true, 0, 0, 0, NULL);
	}
	if (click && html->selection_type != HTML_SELECTION_SELF) {
		sel_owner.none = true;
		html_set_selection(html, HTML_SELECTION_NONE, sel_owner, true);
	}

	if (mas->text.box) {
		int pixel_offset;
		size_t idx;
		plot_font_style_t fstyle;

		font_plot_style_from_css(&html->unit_len_ctx,
					 mas->text.box->style,
					 &fstyle);

		guit->layout->position(&fstyle,
				       mas->text.box->text,
				       mas->text.box->length,
				       x - mas->text.box_x,
				       &idx,
				       &pixel_offset);

		if (selection_click(html->sel,
				    html->bw,
				    mouse,
				    mas->text.box->byte_offset + idx)) {
			/* key presses must be directed at the
			 * main browser window, paste text
			 * operations ignored */
			html_drag_type drag_type;
			union html_drag_owner drag_owner;

			if (selection_dragging(html->sel)) {
				drag_type = HTML_DRAG_SELECTION;
				drag_owner.no_owner = true;
				html_set_drag_type(html,
						   drag_type,
						   drag_owner,
						   NULL);
				mas->result.status = messages_get("Selecting");
			}

			if (selection_active(html->sel)) {
				sel_owner.none = false;
				html_set_selection(html, HTML_SELECTION_SELF,
						   sel_owner, true);
			} else if (click && html->selection_type != HTML_SELECTION_NONE) {
				sel_owner.none = true;
				html_set_selection(html, HTML_SELECTION_NONE,
						   sel_owner, true);
			}

			return default_mouse_action_focus(html, mouse);
		}

	} else if (mouse & BROWSER_MOUSE_PRESS_1) {
		sel_owner.none = true;
		selection_clear(html->sel, true);
	}

	if (selection_active(html->sel)) {
		sel_owner.none = false;
		html_set_selection(html, HTML_SELECTION_SELF, sel_owner, true);
	} else if (click && html->selection_type != HTML_SELECTION_NONE) {
		sel_owner.none = true;
		html_set_selection(html, HTML_SELECTION_NONE, sel_owner, true);
	}

	if (mas->title) {
		mas->result.status = mas->title;
	}

	if (mouse & BROWSER_MOUSE_DRAG_1) {
		if (mouse & BROWSER_MOUSE_MOD_2) {
			union content_msg_data msg_data;
			msg_data.dragsave.type = CONTENT_SAVE_COMPLETE;
			msg_data.dragsave.content = NULL;
			content_broadcast(c, CONTENT_MSG_DRAGSAVE, &msg_data);
		} else {
			if (mas->drag_candidate == NULL) {
				browser_window_page_drag_start(bw, x, y);
			} else {
				html_box_drag_start(html, mas->drag_candidate, x, y);
			}
			mas->result.pointer = BROWSER_POINTER_MOVE;
		}
	} else if (mouse & BROWSER_MOUSE_DRAG_2) {
		if (mouse & BROWSER_MOUSE_MOD_2) {
			union content_msg_data msg_data;
			msg_data.dragsave.type = CONTENT_SAVE_SOURCE;
			msg_data.dragsave.content = NULL;
			content_broadcast(c, CONTENT_MSG_DRAGSAVE, &msg_data);
		} else {
			if (mas->drag_candidate == NULL) {
				browser_window_page_drag_start(bw, x, y);
			} else {
				html_box_drag_start(html, mas->drag_candidate, x, y);
			}
			mas->result.pointer = BROWSER_POINTER_MOVE;
		}
	}


	return default_mouse_action_focus(html, mouse);
}


/**
 * handle non dragging mouse actions
 */
static nserror
mouse_action_drag_none(html_content *html,
		       struct browser_window *bw,
		       browser_mouse_state mouse,
		       int x, int y)
{
	nserror res;
	struct content *c = (struct content *)html;
	union content_msg_data msg_data;
	lwc_string *path;

	/**
	 * computed state
	 *
	 * not on heap to avoid allocation or stack because it is large
	 */
	static struct mouse_action_state mas;

	res = get_mouse_action_node(html, x, y, &mas);
	if (res != NSERROR_OK) {
		return res;
	}

	if (mouse & BROWSER_MOUSE_CLICK_4) {
		mas.result.action = ACTION_BACK;
	} else if (mouse & BROWSER_MOUSE_CLICK_5) {
		mas.result.action = ACTION_FORWARD;
	} else if (mas.scroll.bar) {
		mas.result.status = scrollbar_mouse_status_to_message(
				scrollbar_mouse_action(mas.scroll.bar,
						       mouse,
						       mas.scroll.mouse_x,
						       mas.scroll.mouse_y));
		mas.result.pointer = BROWSER_POINTER_DEFAULT;

	} else if (mas.gadget.control) {
		res = gadget_mouse_action(html, mouse, x, y, &mas);

	} else if ((mas.object != NULL) && (mouse & BROWSER_MOUSE_MOD_2)) {

		if (mouse & BROWSER_MOUSE_DRAG_2) {
			msg_data.dragsave.type = CONTENT_SAVE_NATIVE;
			msg_data.dragsave.content = mas.object;
			content_broadcast(c, CONTENT_MSG_DRAGSAVE, &msg_data);

		} else if (mouse & BROWSER_MOUSE_DRAG_1) {
			msg_data.dragsave.type = CONTENT_SAVE_ORIG;
			msg_data.dragsave.content = mas.object;
			content_broadcast(c, CONTENT_MSG_DRAGSAVE, &msg_data);
		}

		/* \todo should have a drag-saving object msg */

	} else if (mas.iframe != NULL) {
		res = iframe_mouse_action(bw, mouse, x, y, &mas);

	} else if (mas.html_object.box != NULL) {
		res = html_object_mouse_action(html, bw, mouse, x, y, &mas);

	} else if (mas.link.url != NULL) {
		res = link_mouse_action(html, bw, mouse, x, y, &mas);

	} else {
		res = default_mouse_action(html, bw, mouse, x, y, &mas);

	}
	if (res != NSERROR_OK) {
		return res;
	}

	/* send status and pointer message */
	if (mas.result.action != ACTION_NOSEND) {
		msg_data.explicit_status_text = mas.result.status;
		content_broadcast(c, CONTENT_MSG_STATUS, &msg_data);

		msg_data.pointer = mas.result.pointer;
		content_broadcast(c, CONTENT_MSG_POINTER, &msg_data);
	}

	/* Onyx: the pointer's moves -- dom.js makes mouseover / mouseout, mouseenter /
	 * mouseleave and mousemove of them (the element under the pointer, every move) */
	{
		struct js_event_init hinit;

		memset(&hinit, 0, sizeof(hinit));
		hinit.x = x;
		hinit.y = y;
		html_script_event(html, "onyx:hover", mas.node, &hinit);

		/* CSS :hover -- the styles made again when the node under the pointer changes,
		 * if the style sheets have :hover rules (a :hover selector was tried) */
		if (mas.node != html->hover_node) {
			struct dom_node *old_hover = html->hover_node;

			html->hover_node = mas.node != NULL ? dom_node_ref(mas.node) : NULL;
			/* only the elements whose :hover changed, repainted when they
			 * can be (html/onyx_hover.c), else the boxes built again */
			if (html->uses_hover) {
				uint64_t t0 = onyx_perf_now();
				if (!onyx_hover_restyle(html, old_hover))
					html_script_dom_changed(html);
				onyx_perf_log("hover:restyle", t0);
			}
			if (old_hover != NULL)
				dom_node_unref(old_hover);
		}
	}

	/* Onyx: the page's scripts see the main button -- mousedown, then mouseup and
	 * click (then a checkbox's, a radio's input and change); a click they prevent
	 * (event.preventDefault()) neither follows its link nor sends its form */
	if (mouse & (BROWSER_MOUSE_PRESS_1 | BROWSER_MOUSE_CLICK_1)) {
		struct js_event_init init;

		memset(&init, 0, sizeof(init));
		init.x = x;
		init.y = y;
		init.shift = (mouse & BROWSER_MOUSE_MOD_1) != 0;
		init.ctrl = (mouse & BROWSER_MOUSE_MOD_2) != 0;
		init.alt = (mouse & BROWSER_MOUSE_MOD_3) != 0;
		if (mouse & BROWSER_MOUSE_PRESS_1) {
			/* (Onyx: the pointer events first, as Chrome) */
			html_script_event(html, "pointerdown", mas.node, &init);
			html_script_event(html, "mousedown", mas.node, &init);
		} else {
			html_script_event(html, "pointerup", mas.node, &init);
			html_script_event(html, "mouseup", mas.node, &init);
			if (!html_script_event(html, "click", mas.node, &init) &&
			    (mas.result.action == ACTION_NAVIGATE ||
			     mas.result.action == ACTION_SUBMIT ||
			     mas.result.action == ACTION_JS))
				mas.result.action = ACTION_NONE;
			if (mas.result.action == ACTION_SUBMIT &&
			    mas.gadget.control->form != NULL &&
			    !html_script_event(html, "submit",
					mas.gadget.control->form->node, NULL))
				mas.result.action = ACTION_NONE;
			if (mas.gadget.control != NULL &&
			    (mas.gadget.control->type == GADGET_CHECKBOX ||
			     mas.gadget.control->type == GADGET_RADIO)) {
				html_script_event(html, "input",
						mas.gadget.control->node, NULL);
				html_script_event(html, "change",
						mas.gadget.control->node, NULL);
			}
		}
	}

	/* deferred actions that can cause this browser_window to be destroyed
	 * and must therefore be done after set_status/pointer
	 */
	switch (mas.result.action) {
	case ACTION_SUBMIT:
		res = form_submit(content_get_url(c),
				  browser_window_find_target(bw,
							     mas.gadget.target,
							     mouse),
				  mas.gadget.control->form,
				  mas.gadget.control);
		break;

	case ACTION_NAVIGATE:
		if (onyx_link_download(c, bw, mas.link.box, mas.link.url, &res))
			break;		/* (Onyx: <a download>: saved, not opened) */
		res = browser_window_navigate(
				browser_window_find_target(bw,
							   mas.link.target,
							   mouse),
				mas.link.url,
				content_get_url(c),
				BW_NAVIGATE_HISTORY,
				NULL,
				NULL,
				NULL);
		break;

	case ACTION_JS:
		path = nsurl_get_component(mas.link.url, NSURL_PATH);
		if (path != NULL) {
			html_exec(c,
				  lwc_string_data(path),
				  lwc_string_length(path));
			lwc_string_unref(path);
		}
		break;

	case ACTION_BACK:
		res = browser_window_history_back(bw, false);
		break;

	case ACTION_FORWARD:
		res = browser_window_history_forward(bw, false);
		break;

	case ACTION_NOSEND:
	case ACTION_NONE:
		res = NSERROR_OK;
		break;
	}

	return res;
}


/* exported interface documented in html/interaction.h */
nserror html_mouse_track(struct content *c,
			 struct browser_window *bw,
			 browser_mouse_state mouse,
			 int x, int y)
{
	return html_mouse_action(c, bw, mouse, x, y);
}


/* exported interface documented in html/interaction.h */
nserror
html_mouse_action(struct content *c,
		  struct browser_window *bw,
		  browser_mouse_state mouse,
		  int x, int y)
{
	html_content *html = (html_content *)c;
	nserror res = NSERROR_OK;

	onyx_webfont_scope(html);	/* Onyx: text positions in its fonts */
	html->pointer_x = x;		/* (Onyx: the keys' scroller) */
	html->pointer_y = y;

	/* handle open select menu */
	if (html->visible_select_menu != NULL) {
		return mouse_action_select_menu(html, bw, mouse, x, y);
	}

	/* handle content drag */
	switch (html->drag_type) {
	case HTML_DRAG_SELECTION:
		res = mouse_action_drag_selection(html, bw, mouse, x, y);
		break;

	case HTML_DRAG_SCROLLBAR:
		res = mouse_action_drag_scrollbar(html, bw, mouse, x, y);
		break;

	case HTML_DRAG_TEXTAREA_SELECTION:
	case HTML_DRAG_TEXTAREA_SCROLLBAR:
		res = mouse_action_drag_textarea(html, bw, mouse, x, y);
		break;

	case HTML_DRAG_CONTENT_SELECTION:
	case HTML_DRAG_CONTENT_SCROLL:
		res = mouse_action_drag_content(html, bw, mouse, x, y);
		break;

	case HTML_DRAG_NONE:
		res = mouse_action_drag_none(html, bw, mouse, x, y);
		break;

	default:
		/* Unknown content related drag type */
		assert(0 && "Unknown content related drag type");
	}

	if (res != NSERROR_OK) {
		NSLOG(netsurf, ERROR, "%s", messages_get_errorcode(res));
	}

	return res;
}


/**
 * Onyx: a key's name for the scripts (KeyboardEvent.key): the character, or the name of
 * a key NetSurf's keypress codes tell (NULL: one that is not a key, as copy).
 */
static const char *html_script_key_name(uint32_t key, char buf[8])
{
	switch (key) {
	case NS_KEY_ESCAPE: return "Escape";
	case NS_KEY_LEFT: return "ArrowLeft";
	case NS_KEY_RIGHT: return "ArrowRight";
	case NS_KEY_UP: return "ArrowUp";
	case NS_KEY_DOWN: return "ArrowDown";
	case NS_KEY_PAGE_UP: return "PageUp";
	case NS_KEY_PAGE_DOWN: return "PageDown";
	case NS_KEY_TEXT_START: case NS_KEY_LINE_START: return "Home";
	case NS_KEY_TEXT_END: case NS_KEY_LINE_END: return "End";
	case NS_KEY_DELETE_LEFT: return "Backspace";
	case NS_KEY_DELETE_RIGHT: return "Delete";
	case NS_KEY_TAB: return "Tab";
	case NS_KEY_NL: case NS_KEY_CR: return "Enter";
	default: break;
	}
	if (key < 0x20 || (key >= 0x7f && key <= 0x9f) || key >= 0x110000)
		return NULL;	/* (0x80-0x9f: NetSurf's editing keys) */
	buf[utf8_from_ucs4(key, buf)] = '\0';
	return buf;
}

/** Onyx: the letter of an editing shortcut (Ctrl+V for NS_KEY_PASTE...), NULL: none */
static const char *html_script_ctrl_key(uint32_t key)
{
	switch (key) {
	case NS_KEY_SELECT_ALL: return "a";
	case NS_KEY_COPY_SELECTION: return "c";
	case NS_KEY_PASTE: return "v";
	case NS_KEY_CUT_SELECTION: return "x";
	case NS_KEY_UNDO: return "z";
	case NS_KEY_REDO: return "y";
	default: return NULL;
	}
}

static bool html_keypress_action(html_content *html, uint32_t key);

/**
 * Onyx: a key in the focused text field -- the edit it makes told to the scripts:
 * a paste's paste event (a ClipboardEvent: its text; prevented: not pasted), the
 * beforeinput event (prevented: no edit), the field's maxlength kept (what is typed or
 * pasted beyond it dropped), then the input event telling the edit (inputType, data:
 * html_changed_event). false: not an edit (the key's own action then).
 */
static bool html_keypress_edit(html_content *html, struct box *box, uint32_t key,
		bool *handled)
{
	struct form_control *gadget = box->gadget;
	struct textarea *ta = gadget->data.text.ta;
	struct js_event_init init;
	char utf8[8], *paste = NULL;
	size_t paste_len = 0;
	const char *type = NULL;
	unsigned int maxlength = UINT_MAX;
	int32_t ml;

	/* (its maxlength now: a script may have set it since the control was made) */
	if ((gadget->type == GADGET_TEXTBOX || gadget->type == GADGET_PASSWORD) &&
	    box->node != NULL && dom_html_input_element_get_max_length(
			(dom_html_input_element *) box->node, &ml) == DOM_NO_ERR &&
	    ml >= 0)
		maxlength = ml;
	memset(&init, 0, sizeof(init));
	if (key >= 0x20 && !(key >= 0x7f && key <= 0x9f) && key < 0x110000) {
		utf8[utf8_from_ucs4(key, utf8)] = '\0';
		type = "insertText";
		init.data = utf8;
	} else switch (key) {
	case NS_KEY_DELETE_LEFT: type = "deleteContentBackward"; break;
	case NS_KEY_DELETE_RIGHT: type = "deleteContentForward"; break;
	case NS_KEY_CUT_SELECTION: type = "deleteByCut"; break;
	case NS_KEY_UNDO: type = "historyUndo"; break;
	case NS_KEY_REDO: type = "historyRedo"; break;
	case NS_KEY_NL: case NS_KEY_CR:
		if (gadget->type == GADGET_TEXTAREA)
			type = "insertLineBreak";
		break;
	case NS_KEY_PASTE:
		type = "insertFromPaste";
		guit->clipboard->get(&paste, &paste_len);
		if (paste == NULL)
			return false;
		init.data = paste;
		/* the page's paste handler first (an OTP's boxes share the code out) */
		if (!html_script_event(html, "paste", box->node, &init)) {
			free(paste);
			*handled = true;
			return true;
		}
		break;
	default:
		break;
	}
	if (type == NULL)
		return false;
	init.input_type = type;
	if (!html_script_event(html, "beforeinput", box->node, &init)) {
		free(paste);
		*handled = true;
		return true;
	}
	/* the edit, the input event told what it was */
	free(html->script_input_data);
	html->script_input_data = init.data != NULL ? strdup(init.data) : NULL;
	html->script_input_type = type;
	if (init.data != NULL && maxlength != UINT_MAX && box->gadget == gadget &&
	    gadget->data.text.ta == ta) {
		/* (the maxlength attribute: the user's edits only) */
		unsigned int len = 0, have, room;
		const char *text = textarea_data(ta, &len);
		int s, e;
		size_t n, b;

		/* (its length counts its terminator) */
		have = text != NULL && len > 0 ? utf8_bounded_length(text, len - 1) : 0;
		textarea_onyx_get_selection(ta, &s, &e);
		have -= (e > s ? (unsigned int) (e - s) : 0);
		room = have < maxlength ? maxlength - have : 0;
		n = utf8_bounded_length(init.data, strlen(init.data));
		if (room == 0) {
			*handled = true;	/* (full: nothing typed) */
		} else if (n > room) {
			b = utf8_bounded_byte_length(init.data, strlen(init.data), room);
			free(html->script_input_data);
			html->script_input_data = strndup(init.data, b);
			*handled = textarea_drop_text(ta, init.data, b);
		} else if (paste != NULL) {
			*handled = textarea_drop_text(ta, paste, paste_len);
		} else {
			*handled = box_textarea_keypress(html, box, key) == NSERROR_OK;
		}
		free(paste);
		html_script_changed_flush(html);
		html->script_input_type = NULL;
		return true;
	}
	free(paste);
	*handled = box_textarea_keypress(html, box, key) == NSERROR_OK;
	html_script_changed_flush(html);
	html->script_input_type = NULL;
	return true;
}

/**
 * Handle keypresses.
 *
 * \param  c	content of type HTML
 * \param  key	The UCS4 character codepoint
 * \return true if key handled, false otherwise
 */
bool html_keypress(struct content *c, uint32_t key)
{
	html_content *html = (html_content *) c;
	struct js_event_init init;
	char name[8];
	dom_node *target = NULL;
	bool handled, ok = true;

	onyx_webfont_scope(html);	/* Onyx: text positions in its fonts */

	/* Onyx: the page's scripts see the key first (keydown, keypress for a character),
	 * at the element with the focus; one they prevent is not typed. Then the key's
	 * edit in a text field (beforeinput, input: html_keypress_edit) or its action; its
	 * keyup last -- after the input event, as browsers do: a code's box that moves on
	 * at keyup sees its value typed. Ctrl+V, Ctrl+C... are keys too ("v", ctrlKey). */
	memset(&init, 0, sizeof(init));
	if (html->jsthread != NULL && html->layout != NULL) {
		target = html->layout->node;
		if (html->focus_type == HTML_FOCUS_TEXTAREA &&
		    html->focus_owner.textarea != NULL &&
		    html->focus_owner.textarea->node != NULL)
			target = html->focus_owner.textarea->node;
		init.key = html_script_key_name(key, name);
		if (init.key == NULL && (init.key = html_script_ctrl_key(key)) != NULL)
			init.ctrl = true;
		if (init.key != NULL) {
			ok = html_script_event(html, "keydown", target, &init);
			if (ok && !init.ctrl && ((key >= 0x20 && key < 0x7f) ||
				   (key >= 0xa0 && key < 0x110000)))
				ok = html_script_event(html, "keypress", target,
						&init);
		}
	}
	if (!ok) {
		handled = true;	/* (prevented: not typed) */
	} else if (html->jsthread != NULL &&
		   html->focus_type == HTML_FOCUS_TEXTAREA &&
		   html->focus_owner.textarea != NULL &&
		   html->focus_owner.textarea->gadget != NULL &&
		   html->focus_owner.textarea->gadget->data.text.ta != NULL &&
		   html_keypress_edit(html, html->focus_owner.textarea, key, &handled)) {
		/* (an edit, told to the scripts) */
	} else {
		handled = html_keypress_action(html, key);
	}
	if (target != NULL && init.key != NULL && html->layout != NULL) {
		/* (the focus may have moved on meanwhile: the keyup goes there) */
		if (html->focus_type == HTML_FOCUS_TEXTAREA &&
		    html->focus_owner.textarea != NULL &&
		    html->focus_owner.textarea->node != NULL)
			target = html->focus_owner.textarea->node;
		else
			target = html->layout->node;
		html_script_event(html, "keyup", target, &init);
	}
	return handled;
}

/** a key's own action (the focused field's editing, the page's shortcuts, scrolling) */
static bool html_keypress_action(html_content *html, uint32_t key)
{
	struct content *c = (struct content *) html;
	struct selection *sel = html->sel;

	/** \todo
	 * At the moment, the front end interface for keypress only gives
	 * us a UCS4 key value.  This doesn't doesn't have all the information
	 * we need to fill out the event properly.  We don't get to know about
	 * modifier keys, and things like CTRL+C are passed in as
	 * \ref NS_KEY_COPY_SELECTION, a magic value outside the valid Unicode
	 * range.
	 *
	 * We need to:
	 *
	 * 1. Update the front end interface so that both press and release
	 *    events reach the core.
	 * 2. Stop encoding the special keys like \ref NS_KEY_COPY_SELECTION as
	 *    magic values in the front ends, so we just get the events, e.g.:
	 *    1. Press ctrl
	 *    2. Press c
	 *    3. Release c
	 *    4. Release ctrl
	 * 3. Pass all the new info to the DOM KeyboardEvent events.
	 * 4. If there is a focused element, fire the event at that, instead of
	 *    `html->layout->node`.
	 * 5. Rebuild the \ref NS_KEY_COPY_SELECTION values from the info we
	 *    now get given, and use that for the code below this
	 *    \ref fire_dom_keyboard_event call.
	 * 6. Move the code after this \ref fire_dom_keyboard_event call into
	 *    the default action handler for DOM events.
	 *
	 * This will mean that if the JavaScript event listener does
	 * `event.preventDefault()` then we won't handle the event when
	 * we're not supposed to.
	 */
	/* (Onyx: the scripts' events: html_keypress, above) */
	switch (html->focus_type) {
	case HTML_FOCUS_CONTENT:
		return content_keypress(html->focus_owner.content->object, key);

	case HTML_FOCUS_TEXTAREA:
		if (box_textarea_keypress(html, html->focus_owner.textarea, key) == NSERROR_OK) {
			return true;
		} else {
			return false;
		}

	default:
		/* Deal with it below */
		break;
	}

	switch (key) {
	case NS_KEY_COPY_SELECTION:
		selection_copy_to_clipboard(sel);
		return true;

	case NS_KEY_CLEAR_SELECTION:
		selection_clear(sel, true);
		return true;

	case NS_KEY_SELECT_ALL:
		selection_select_all(sel);
		return true;

	case NS_KEY_ESCAPE:
		/* if there's no selection, leave Escape for the caller */
		return selection_clear(sel, true);

	/* Onyx: the scrolling keys scroll the scroller under the pointer (an
	 * overflow: auto panel, a consent screen's), else the window (the
	 * caller) */
	case NS_KEY_UP:
		return html_scroll_boxes_at_point(c, html->pointer_x,
				html->pointer_y, 0, -40);
	case NS_KEY_DOWN:
		return html_scroll_boxes_at_point(c, html->pointer_x,
				html->pointer_y, 0, 40);
	case NS_KEY_PAGE_UP:
		return html_scroll_boxes_at_point(c, html->pointer_x,
				html->pointer_y, 0, SCROLL_PAGE_UP);
	case NS_KEY_PAGE_DOWN:
	case ' ':
		return html_scroll_boxes_at_point(c, html->pointer_x,
				html->pointer_y, 0, SCROLL_PAGE_DOWN);
	case NS_KEY_TEXT_START:
		return html_scroll_boxes_at_point(c, html->pointer_x,
				html->pointer_y, 0, SCROLL_TOP);
	case NS_KEY_TEXT_END:
		return html_scroll_boxes_at_point(c, html->pointer_x,
				html->pointer_y, 0, SCROLL_BOTTOM);
	}

	return false;
}


/**
 * Callback for in-page scrollbars.
 */
void html_overflow_scroll_callback(void *client_data,
		struct scrollbar_msg_data *scrollbar_data)
{
	struct html_scrollbar_data *data = client_data;
	html_content *html = (html_content *)data->c;
	struct box *box = data->box;
	union content_msg_data msg_data;
	html_drag_type drag_type;
	union html_drag_owner drag_owner;

	switch(scrollbar_data->msg) {
	case SCROLLBAR_MSG_MOVED:

		if (html->reflowing == true) {
			/* Can't redraw during layout, and it will
			 * be redrawn after layout anyway. */
			break;
		}

		html__redraw_a_box(html, box);
		/* Onyx: the element's scroll event */
		if (box->node != NULL && html->jsthread != NULL)
			html_script_event(html, "scroll", box->node, NULL);
		break;
	case SCROLLBAR_MSG_SCROLL_START:
	{
		struct rect rect = {
			.x0 = scrollbar_data->x0,
			.y0 = scrollbar_data->y0,
			.x1 = scrollbar_data->x1,
			.y1 = scrollbar_data->y1
		};
		drag_type = HTML_DRAG_SCROLLBAR;
		drag_owner.scrollbar = scrollbar_data->scrollbar;
		html_set_drag_type(html, drag_type, drag_owner, &rect);
	}
		break;
	case SCROLLBAR_MSG_SCROLL_FINISHED:
		drag_type = HTML_DRAG_NONE;
		drag_owner.no_owner = true;
		html_set_drag_type(html, drag_type, drag_owner, NULL);

		msg_data.pointer = BROWSER_POINTER_AUTO;
		content_broadcast(data->c, CONTENT_MSG_POINTER, &msg_data);
		break;
	}
}


/* Documented in html_internal.h */
void html_set_drag_type(html_content *html, html_drag_type drag_type,
		union html_drag_owner drag_owner, const struct rect *rect)
{
	union content_msg_data msg_data;

	assert(html != NULL);

	html->drag_type = drag_type;
	html->drag_owner = drag_owner;

	switch (drag_type) {
	case HTML_DRAG_NONE:
		assert(drag_owner.no_owner == true);
		msg_data.drag.type = CONTENT_DRAG_NONE;
		break;

	case HTML_DRAG_SCROLLBAR:
	case HTML_DRAG_TEXTAREA_SCROLLBAR:
	case HTML_DRAG_CONTENT_SCROLL:
		msg_data.drag.type = CONTENT_DRAG_SCROLL;
		break;

	case HTML_DRAG_SELECTION:
		assert(drag_owner.no_owner == true);
		fallthrough;
	case HTML_DRAG_TEXTAREA_SELECTION:
	case HTML_DRAG_CONTENT_SELECTION:
		msg_data.drag.type = CONTENT_DRAG_SELECTION;
		break;
	}
	msg_data.drag.rect = rect;

	/* Inform of the content's drag status change */
	content_broadcast((struct content *)html, CONTENT_MSG_DRAG, &msg_data);
}

/* Documented in html_internal.h */
void html_set_focus(html_content *html, html_focus_type focus_type,
		union html_focus_owner focus_owner, bool hide_caret,
		int x, int y, int height, const struct rect *clip)
{
	union content_msg_data msg_data;
	int x_off = 0;
	int y_off = 0;
	struct rect cr;
	bool textarea_lost_focus = html->focus_type == HTML_FOCUS_TEXTAREA &&
			focus_type != HTML_FOCUS_TEXTAREA;

	assert(html != NULL);

	switch (focus_type) {
	case HTML_FOCUS_SELF:
		assert(focus_owner.self == true);
		if (html->focus_type == HTML_FOCUS_SELF)
			/* Don't need to tell anyone anything */
			return;
		break;

	case HTML_FOCUS_CONTENT:
		onyx_box_coords(html, focus_owner.content, &x_off, &y_off);
		break;

	case HTML_FOCUS_TEXTAREA:
		onyx_box_coords(html, focus_owner.textarea, &x_off, &y_off);
		break;
	}

	html->focus_type = focus_type;
	html->focus_owner = focus_owner;

	if (textarea_lost_focus) {
		msg_data.caret.type = CONTENT_CARET_REMOVE;
	} else if (focus_type != HTML_FOCUS_SELF && hide_caret) {
		msg_data.caret.type = CONTENT_CARET_HIDE;
	} else {
		if (clip != NULL) {
			cr = *clip;
			cr.x0 += x_off;
			cr.y0 += y_off;
			cr.x1 += x_off;
			cr.y1 += y_off;
		}

		msg_data.caret.type = CONTENT_CARET_SET_POS;
		msg_data.caret.pos.x = x + x_off;
		msg_data.caret.pos.y = y + y_off;
		msg_data.caret.pos.height = height;
		msg_data.caret.pos.clip = (clip == NULL) ? NULL : &cr;
	}

	/* Inform of the content's drag status change */
	content_broadcast((struct content *)html, CONTENT_MSG_CARET, &msg_data);
}

/* Documented in html_internal.h */
void html_set_selection(html_content *html, html_selection_type selection_type,
		union html_selection_owner selection_owner, bool read_only)
{
	union content_msg_data msg_data;
	struct box *box;
	bool changed = false;
	bool same_type = html->selection_type == selection_type;

	assert(html != NULL);

	if ((selection_type == HTML_SELECTION_NONE &&
			html->selection_type != HTML_SELECTION_NONE) ||
			(selection_type != HTML_SELECTION_NONE &&
			html->selection_type == HTML_SELECTION_NONE))
		/* Existance of selection has changed, and we'll need to
		 * inform our owner */
		changed = true;

	/* Clear any existing selection */
	if (html->selection_type != HTML_SELECTION_NONE) {
		switch (html->selection_type) {
		case HTML_SELECTION_SELF:
			if (same_type)
				break;
			selection_clear(html->sel, true);
			break;
		case HTML_SELECTION_TEXTAREA:
			if (same_type && html->selection_owner.textarea ==
					selection_owner.textarea)
				break;
			box = html->selection_owner.textarea;
			textarea_clear_selection(box->gadget->data.text.ta);
			break;
		case HTML_SELECTION_CONTENT:
			if (same_type && html->selection_owner.content ==
					selection_owner.content)
				break;
			box = html->selection_owner.content;
			content_clear_selection(box->object);
			break;
		default:
			break;
		}
	}

	html->selection_type = selection_type;
	html->selection_owner = selection_owner;

	if (!changed)
		/* Don't need to report lack of change to owner */
		return;

	/* Prepare msg */
	switch (selection_type) {
	case HTML_SELECTION_NONE:
		assert(selection_owner.none == true);
		msg_data.selection.selection = false;
		break;
	case HTML_SELECTION_SELF:
		assert(selection_owner.none == false);
		fallthrough;
	case HTML_SELECTION_TEXTAREA:
	case HTML_SELECTION_CONTENT:
		msg_data.selection.selection = true;
		break;
	default:
		break;
	}
	msg_data.selection.read_only = read_only;

	/* Inform of the content's selection status change */
	content_broadcast((struct content *)html, CONTENT_MSG_SELECTION,
			&msg_data);
}
