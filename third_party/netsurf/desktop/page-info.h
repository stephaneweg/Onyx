/*
 * Copyright 2020 Michael Drake <tlsa@netsurf-browser.org>
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
 * Pave info viewer window interface
 */

#ifndef NETSURF_DESKTOP_PAGE_INFO_H
#define NETSURF_DESKTOP_PAGE_INFO_H

#include <stdint.h>
#include <stdbool.h>

#include "utils/errors.h"
#include "netsurf/mouse.h"

struct rect;
struct nsurl;
struct page_info;
struct core_window;
struct browser_window;
struct redraw_context;

/*
 * Onyx: page-info.c (NetSurf's page info window) is removed: Jet never opens it (its padlock
 * is onyx_chrome.cpp's), and page_info_init() only set that window's colours. They stay
 * no-ops so netsurf.c's call sites keep their upstream shape.
 */
static inline nserror page_info_init(void)
{
	return NSERROR_OK;
}

static inline nserror page_info_fini(void)
{
	return NSERROR_OK;
}

#endif
