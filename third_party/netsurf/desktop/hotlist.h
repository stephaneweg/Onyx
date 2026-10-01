/*
 * Copyright 2013 Michael Drake <tlsa@netsurf-browser.org>
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
 
#ifndef _NETSURF_DESKTOP_HOTLIST_H_
#define _NETSURF_DESKTOP_HOTLIST_H_

#include <stdbool.h>
#include <stdint.h>

#include "utils/errors.h"
#include "netsurf/mouse.h"

struct redraw_context;
struct nsurl;
struct rect;

/*
 * Onyx: hotlist.c (NetSurf's bookmarks tree view) is removed. It was never initialised, so
 * hotlist_update_url() always returned at once: it stays a no-op so the call site in
 * browser_window.c keeps its upstream shape.
 */
static inline void hotlist_update_url(struct nsurl *url)
{
	(void)url;
}

#endif
