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

/** \file
 * Cookie Manager (interface).
 */

#ifndef _NETSURF_DESKTOP_COOKIE_MANAGER_H_
#define _NETSURF_DESKTOP_COOKIE_MANAGER_H_

#include <stdbool.h>
#include <stdint.h>

#include "utils/errors.h"
#include "netsurf/mouse.h"

struct redraw_context;
struct cookie_data;
struct rect;

/*
 * Onyx: cookie_manager.c (NetSurf's cookie manager tree view) is removed. It was never
 * initialised, so these always returned at once: they stay no-ops so urldb.c's call sites
 * keep their upstream shape. The cookies themselves are urldb's.
 */
struct cookie_data;

static inline bool cookie_manager_add(const struct cookie_data *data)
{
	(void)data;
	return true;
}

static inline void cookie_manager_remove(const struct cookie_data *data)
{
	(void)data;
}

#endif
