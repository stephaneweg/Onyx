/*
 * Copyright 2012 - 2013 Michael Drake <tlsa@netsurf-browser.org>
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
 
#ifndef _NETSURF_DESKTOP_GLOBAL_HISTORY_H_
#define _NETSURF_DESKTOP_GLOBAL_HISTORY_H_

#include <stdbool.h>
#include <stdint.h>

#include "utils/errors.h"
#include "netsurf/mouse.h"

struct redraw_context;
struct nsurl;
struct rect;

/*
 * Onyx: global_history.c (NetSurf's global history tree view) is removed. Jet's History
 * dialog (onyx_chrome.cpp) reads urldb, and the tree view was never created, so
 * global_history_add() always returned at once: it stays a no-op so the call site in
 * browser_window.c keeps its upstream shape.
 */
static inline nserror global_history_add(struct nsurl *url)
{
	(void)url;
	return NSERROR_OK;
}

#endif
