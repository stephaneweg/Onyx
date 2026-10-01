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
 * Onyx (Jet Browser, docs/06 §38): the downloads -- the Save dialog, the file written as the
 * bytes come (a writer thread), the toolbar's download button.
 */

#ifndef NETSURF_FB_ONYX_DOWNLOAD_H
#define NETSURF_FB_ONYX_DOWNLOAD_H

/** The frontend's download table (netsurf_table.download). */
extern struct gui_download_table *onyx_download_table;

/** At the start: the core's hook for a script's bytes (a blob: <a download>). */
void onyx_download_init(void);

/** At the end: the downloads still running stopped, their partial files removed. */
void onyx_download_finalise(void);

#endif
