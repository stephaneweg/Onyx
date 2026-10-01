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
 * Onyx (Jet Browser, docs/06 §38): the core's hooks for the page zoom, the status bar and the
 * downloads. The frontend (frontends/framebuffer/gui.c, onyx_download.c) sets the function
 * pointers at its start; left NULL (another frontend, a test harness) the core behaves as
 * NetSurf's.
 */

#ifndef NETSURF_ONYX_JET_H
#define NETSURF_ONYX_JET_H

#include <stddef.h>

struct nsurl;
struct browser_window;

/**
 * The page zoom per site: a root window's new content is formatted at the scale this answers
 * for its address (browser_window_content_ready, before its first layout), so a site keeps its
 * zoom without a second layout. `scale` is the window's scale now.
 */
extern float (*onyx_zoom_hook)(struct nsurl *url, float scale);

/** The HTTP status of the window's page (its response's), 0 when none (file:, about:...). */
long onyx_browser_window_http_code(struct browser_window *bw);

/**
 * A fetch failed: about:query/fetcherror shows it (the reason: NetSurf's or the fetcher's
 * message). The status bar keeps it until the next load.
 */
extern void (*onyx_fetch_error_hook)(const char *url, const char *reason);

/**
 * The next download's name, from the link that asks for it (<a download="name">): taken by
 * the download context made next (desktop/download.c); a Content-Disposition filename wins.
 * NULL / "" clears it.
 */
void download_onyx_hint(const char *name);

/**
 * Bytes a script made (a blob: URL behind <a download>): saved as a download -- the frontend
 * asks where (onyx_download.c). The bytes are copied.
 */
extern void (*onyx_download_bytes_hook)(const void *data, size_t len, const char *name,
		const char *mime, const char *url);

#endif
