/*
 * Copyright 2006 Richard Wilson <info@tinct.net>
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
 * Frame and frameset creation and manipulation (interface).
 */

#ifndef NETSURF_DESKTOP_FRAMES_H_
#define NETSURF_DESKTOP_FRAMES_H_

struct scrollbar_msg_data;
struct content_html_iframe;
struct content_html_frames;

/**
 * Create and open iframes for a browser window.
 *
 * \param bw The browser window to create iframes for.
 * \return NSERROR_OK or error code on faliure.
 */
nserror browser_window_create_iframes(struct browser_window *bw);

/**
 * Recalculate iframe positions following a resize.
 *
 * \param bw The browser window to reposition iframes for
 */
void browser_window_recalculate_iframes(struct browser_window *bw);

/**
 * Invalidate an iframe causing a redraw.
 *
 * \param bw The browser window to invalidate
 */
nserror browser_window_invalidate_iframe(struct browser_window *bw);

/**
 * Destroy iframes opened in browser_window_create_iframes()
 *
 * \param bw The browser window to destroy iframes for.
 * \return NSERROR_OK
 */
nserror browser_window_destroy_iframes(struct browser_window *bw);

/**
 * Create and open a frameset for a browser window.
 *
 * \param[in,out] bw The browser window to create the frameset for
 * \return NSERROR_OK or error code on faliure
 */
nserror browser_window_create_frameset(struct browser_window *bw);

void browser_window_recalculate_frameset(struct browser_window *bw);
bool browser_window_frame_resize_start(struct browser_window *bw,
		browser_mouse_state mouse, int x, int y,
		browser_pointer_shape *pointer);
void browser_window_resize_frame(struct browser_window *bw, int x, int y);

void browser_window_scroll_callback(void *client_data,
		struct scrollbar_msg_data *scrollbar_data);


/**
 * Create, remove, and update browser window scrollbars
 *
 * \param  bw    The browser window
 */
void browser_window_handle_scrollbars(struct browser_window *bw);


/* ---- Onyx: iframes as browsing contexts (desktop/frames.c) ------------------------------
 * Each <iframe> element of a document (in its tree, shown or not) has a browser window of
 * its own, kept by element: the document's boxes made again (html_rebox) keep the frames
 * and their scripts; a frame is navigated when its src / srcdoc changes, removed with its
 * element. The scripts know each window by a frame id (javascript/quickjs/qjs_frames.c). */

struct dom_node;
struct nsurl;

#define ONYX_SANDBOX		1u	/**< a sandbox attribute */
#define ONYX_SANDBOX_SCRIPTS	2u	/**< ... with allow-scripts */
#define ONYX_SANDBOX_ORIGIN	4u	/**< ... with allow-same-origin */

/**
 * The frames of a document brought in line with its <iframe> elements.
 *
 * \param bw       The window showing (or loading) the document
 * \param htmlc    The document's html content
 * \param remove   Frames whose element left the document are destroyed
 * \return NSERROR_OK or an error code
 */
nserror onyx_frames_sync(struct browser_window *bw, void *htmlc, bool remove);

/** a window's frame id (given the first time) */
int onyx_frame_id(struct browser_window *bw);

/** the window of a frame id, or NULL (gone) */
struct browser_window *onyx_frame_by_id(int fid);

/** the frame of an <iframe> element of the window's document (made now if it has none
 *  yet), or NULL */
struct browser_window *onyx_frame_for_element(struct browser_window *bw, void *htmlc,
		struct dom_node *el);

/** a frame's document loaded: its element's load event (once per document) */
void onyx_frame_loaded(struct browser_window *bw);

/** a window's document done: an iframe's with no scripts sends its element's load */
void onyx_frame_content_done(struct browser_window *bw);

/** true while a frame of the window has not loaded its document (its load waits) */
bool onyx_frames_loading(struct browser_window *bw);

/** a window's frame records released (its destruction) */
void onyx_frame_release(struct browser_window *bw);

/** the base URL of a srcdoc document by its data: URL, or NULL */
struct nsurl *onyx_frames_srcdoc_base(struct nsurl *url);

#endif
