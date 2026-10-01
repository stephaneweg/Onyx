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
 * Onyx (Jet Browser, docs/06 §40): find in page, the page's context menu, copying an image.
 */

#ifndef NETSURF_FB_ONYX_EDIT_H
#define NETSURF_FB_ONYX_EDIT_H

#include <stdbool.h>

struct rect;
struct gui_window;

/** the core's search callbacks (the count told to the find bar when the core finds again) */
extern struct gui_search_table *onyx_search_table;

/**
 * The core asks to scroll a rectangle into view (gui_window_set_scroll): taken while a find
 * runs -- the match is then shown where Chrome shows it (the view moved only when it is out of
 * it, the match then in its middle). true: taken.
 */
bool onyx_find_take_scroll(const struct rect *rect);

/** A page's load ended: its words searched again when the find bar is open (a new page). */
void onyx_find_page_loaded(void);

/* gui.c: the root window's view -- its scroll offsets and its size (device px: the zoom's) */
void onyx_view_get(int *sx, int *sy, int *w, int *h);
void onyx_view_scroll(int sx, int sy);

#endif
