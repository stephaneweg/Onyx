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
 *
 * Find: the chrome's find bar (user/netsurf/onyx_chrome.cpp) hands its words here; the core's
 * free text search (desktop/search.c, content/textsearch.c: literal, case and accents ignored
 * unless "Match case") finds every match, highlights them all (the current one orange) and
 * asks to scroll the current one into view -- taken here (onyx_find_take_scroll): the view
 * moves only when the match is out of it, and then shows it in its middle, as Chrome. The bar
 * shows "n of m". Typed words are searched at once while a search costs little; past
 * FIND_SLOW_MS (a big page on the Pi) the search waits for a pause in the typing.
 *
 * The context menu (a right press on the page): what is under the pointer (the core's
 * browser_window_get_features: a link, an image, a form's text field; the selection) decides
 * its items -- Open / Save / Copy the link, Open / Save / Copy the image or its address, Cut /
 * Copy / Paste / Select All, Back / Forward / Reload, Find in Page.
 *
 * Copy Image: the kernel's clipboard holds 64 KB, not a picture's pixels -- the image as the
 * page shows it (its decoded bitmap: a GIF's frame now, an alpha channel kept) is written as a
 * PNG in RAM:/jet/clip/ (image-1.png, image-2.png in turn: the one before stays good while
 * the new one is written) and the clipboard holds that file (CLIP_FILES), as the File Viewer's
 * Copy does: Paint's Paste reads it (user/Apps/paint, img_load). On Windows the picture goes
 * to the Windows clipboard as a DIB (pc/Jet/winkapi.cpp: onyx_win_clip_image).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libnsfb.h>
#include <nsutils/time.h>

#include "utils/errors.h"
#include "utils/nsurl.h"
#include "utils/nsoption.h"
#include "netsurf/types.h"
#include "netsurf/keypress.h"
#include "netsurf/content.h"
#include "netsurf/browser_window.h"
#include "netsurf/search.h"
#include "desktop/search.h"
#include "content/textsearch.h"

#include "framebuffer/gui.h"
#include "framebuffer/schedule.h"
#include "framebuffer/onyx_edit.h"

#include "netsurf/onyx_chrome.h"	/* user/netsurf: the find bar, the menu, the PNG */
#include "kapi.h"

/* the bench's log (each search, each menu); not the Pi's kernel log at each key */
#ifdef ONYX_HOST_SIM
#define EDIT_LOG(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)
#else
#define EDIT_LOG(...) do { } while (0)
#endif

#define FIND_SLOW_MS	30	/* a search that cost more: the next typed one waits for... */
#define FIND_PAUSE_MS	300	/* ... a pause that long in the typing */

static char find_words[1024];		/* the bar's words (UTF-8) */
static bool find_case;			/* Match case */
static unsigned find_cost;		/* the last new search's time (ms) */
static struct hlcache_handle *find_content;	/* the content searched last */
static bool find_taking;		/* a search runs: its scroll request is ours */
static struct rect find_rect;		/* ... the match it asked for */
static bool find_rect_set;

static uint64_t edit_now_ms(void)
{
	uint64_t t = 0;
	nsu_getmonotonic_ms(&t);
	return t;
}

/* The find bar told the state of the search on the page now */
static void find_report(void)
{
	int index = -1, count = -1;

	if (find_words[0] != '\0' && window_list != NULL &&
	    browser_window_get_content(window_list->bw) == find_content &&
	    find_content != NULL)
		content_textsearch_onyx_state(find_content, &index, &count);
	onyx_chrome_find_result(index, count);
}

static void find_report_cb(void *p)
{
	(void) p;
	find_report();
}

/* The match's rectangle (document px) shown: the view moved only when it is not all in it */
static void find_show(const struct rect *r, float scale)
{
	int sx = 0, sy = 0, w = 0, h = 0, nx, ny;
	int x0 = (int) (r->x0 * scale), y0 = (int) (r->y0 * scale);
	int x1 = (int) (r->x1 * scale), y1 = (int) (r->y1 * scale);

	onyx_view_get(&sx, &sy, &w, &h);
	nx = sx;
	ny = sy;
	if (y0 < sy || y1 > sy + h)
		ny = (y0 + y1) / 2 - h / 2;	/* (in the middle) */
	if (x1 - x0 > w || x0 < sx)
		nx = x0 - 16;
	else if (x1 > sx + w)
		nx = x1 - w + 16;
	if (nx < 0)
		nx = 0;
	if (ny < 0)
		ny = 0;
	if (nx != sx || ny != sy)
		onyx_view_scroll(nx, ny);
}

/* A search: dir 0 new (the first match), 2 new without moving the view, 1 / -1 a step */
static void find_run(int dir)
{
	struct browser_window *bw;
	struct hlcache_handle *h;
	search_flags_t flags;
	bool fresh;
	uint64_t t0;
	int index = -1, count = 0;

	if (window_list == NULL)
		return;
	bw = window_list->bw;
	h = browser_window_get_content(bw);
	if (h == NULL)
		return;
	if (find_words[0] == '\0') {
		browser_window_search_clear(bw);
		find_content = NULL;
		find_report();
		return;
	}
	fresh = dir == 0 || dir == 2 || h != find_content;
	if (fresh && h == find_content)
		browser_window_search_clear(bw);	/* (the same words: from the top again) */
	flags = SEARCH_FLAG_SHOWALL |
		(dir < 0 ? SEARCH_FLAG_BACKWARDS : SEARCH_FLAG_FORWARDS) |
		(find_case ? SEARCH_FLAG_CASE_SENSITIVE : 0);
	t0 = edit_now_ms();
	find_taking = true;
	find_rect_set = false;
	browser_window_search(bw, window_list, flags, find_words);
	find_taking = false;
	if (fresh)
		find_cost = (unsigned) (edit_now_ms() - t0);
	find_content = h;
	if (find_rect_set && dir != 2)
		find_show(&find_rect, browser_window_get_scale(bw));
	content_textsearch_onyx_state(h, &index, &count);
	onyx_chrome_find_result(index, count);
	EDIT_LOG("ONYX-FIND \"%s\" dir=%d case=%d: %d of %d (%u ms)\n", find_words, dir,
		 find_case ? 1 : 0, index + 1, count, find_cost);
}

static void find_typed_cb(void *p)
{
	(void) p;
	find_run(0);
}

/* exported interface documented in netsurf/onyx_chrome.h */
void onyx_browser_find(const char *utf8, int dir, int match_case)
{
	snprintf(find_words, sizeof find_words, "%s", utf8 != NULL ? utf8 : "");
	find_case = match_case != 0;
	if (dir == 0 && find_words[0] != '\0' && find_cost > FIND_SLOW_MS) {
		/* a page slow to search: after a pause in the typing */
		framebuffer_schedule(FIND_PAUSE_MS, find_typed_cb, NULL);
		return;
	}
	framebuffer_schedule(-1, find_typed_cb, NULL);
	find_run(dir);
}

/* exported interface documented in netsurf/onyx_chrome.h */
void onyx_browser_find_close(void)
{
	framebuffer_schedule(-1, find_typed_cb, NULL);
	if (window_list != NULL)
		browser_window_search_clear(window_list->bw);
	find_content = NULL;
	EDIT_LOG("ONYX-FIND closed\n");
}

/* exported interface documented in framebuffer/onyx_edit.h */
bool onyx_find_take_scroll(const struct rect *rect)
{
	if (!find_taking)
		return false;
	find_rect = *rect;
	find_rect_set = true;
	return true;
}

/* exported interface documented in framebuffer/onyx_edit.h */
void onyx_find_page_loaded(void)
{
	if (window_list == NULL || !onyx_chrome_find_shown() ||
	    browser_window_get_content(window_list->bw) == find_content)
		return;
	onyx_chrome_find_again();
}

/* the core's search callbacks: a match state broadcast (also when the core found the words
 * again after a layout) -> the bar's count, from the main loop */
static void onyx_search_status(bool found, void *p)
{
	(void) found;
	(void) p;
	if (!find_taking)
		framebuffer_schedule(0, find_report_cb, NULL);
}

static struct gui_search_table search_table = {
	.status = onyx_search_status,
};

struct gui_search_table *onyx_search_table = &search_table;


/* ---- copying ---- */

static void edit_copy_text(const char *s)
{
	if (s == NULL)
		return;
	kapi_clipboard_set(CLIP_TEXT, s, (unsigned) strlen(s));
	printf("ONYX-CLIPBOARD text %u bytes\n", (unsigned) strlen(s));
	fflush(stdout);
}

#ifdef _WIN32
/* pc/Jet/winkapi.cpp: the picture on the Windows clipboard (CF_DIB) */
extern int onyx_win_clip_image(const unsigned *px, int w, int h);
#endif

/* The image (its decoded bitmap) on the clipboard: a PNG file in RAM:/jet/clip/, CLIP_FILES */
static void edit_copy_image(struct hlcache_handle *obj)
{
	static int turn;
	struct bitmap *bm = obj != NULL ? content_get_bitmap(obj) : NULL;
	enum nsfb_format_e fmt = NSFB_FMT_ABGR8888;
	int w = 0, h = 0, stride = 0, x, y;
	uint8_t *ptr = NULL;
	unsigned *px;
	char path[64];

	if (bm == NULL ||
	    nsfb_get_geometry((nsfb_t *) bm, &w, &h, &fmt) != 0 ||
	    nsfb_get_buffer((nsfb_t *) bm, &ptr, &stride) != 0 ||
	    ptr == NULL || w <= 0 || h <= 0)
		return;
	px = malloc((size_t) w * h * sizeof *px);
	if (px == NULL)
		return;
	for (y = 0; y < h; y++) {
		const uint8_t *p = ptr + (size_t) y * stride;
		for (x = 0; x < w; x++, p += 4) {
			/* (NSFB_FMT_ABGR8888: the bytes R, G, B, A; XBGR: opaque) */
			unsigned a = fmt == NSFB_FMT_XBGR8888 ? 255 : p[3];
			px[(size_t) y * w + x] = a << 24 | (unsigned) p[0] << 16 |
					(unsigned) p[1] << 8 | p[2];
		}
	}
#ifdef _WIN32
	if (onyx_win_clip_image(px, w, h)) {
		printf("ONYX-CLIPBOARD image %dx%d (CF_DIB)\n", w, h);
		fflush(stdout);
		free(px);
		return;
	}
#endif
	turn = turn % 2 + 1;
	snprintf(path, sizeof path, "RAM:/jet/clip/image-%d.png", turn);
	kapi_mkdir("RAM:/jet");
	kapi_mkdir("RAM:/jet/clip");
	if (onyx_chrome_save_png(path, px, w, h)) {
		kapi_clipboard_set(CLIP_FILES, path, (unsigned) strlen(path));
		printf("ONYX-CLIPBOARD image %dx%d %s\n", w, h, path);
	} else {
		onyx_chrome_message("Copy Image", "The image could not be written in RAM:.");
		printf("ONYX-CLIPBOARD image %dx%d failed\n", w, h);
	}
	fflush(stdout);
	free(px);
}


/* ---- the context menu ---- */

/* exported interface documented in netsurf/onyx_chrome.h */
void onyx_browser_context_menu(int x, int y)
{
	struct browser_window *bw;
	struct browser_window_features f;
	struct hlcache_handle *page;
	browser_editor_flags ed;
	int sx = 0, sy = 0, flags = 0, cmd;
	nsurl *ref;

	if (window_list == NULL)
		return;
	bw = window_list->bw;
	onyx_view_get(&sx, &sy, NULL, NULL);
	x += sx;	/* (the core's coordinates: the document's, at the zoom's scale) */
	y += sy;
	memset(&f, 0, sizeof f);
	if (browser_window_get_features(bw, x, y, &f) != NSERROR_OK)
		memset(&f, 0, sizeof f);
	ed = browser_window_get_editor_flags(bw);
	if (f.link != NULL)
		flags |= ONYX_CTXF_LINK;
	if (f.object != NULL && content_get_type(f.object) == CONTENT_IMAGE) {
		flags |= ONYX_CTXF_IMAGE;
		if (content_get_bitmap(f.object) != NULL)
			flags |= ONYX_CTXF_IMAGE_PIXELS;
	}
	if (ed & BW_EDITOR_CAN_COPY)
		flags |= ONYX_CTXF_SELECTION;
	if (ed & BW_EDITOR_CAN_CUT)
		flags |= ONYX_CTXF_CAN_CUT;
	if (f.form_features == CTX_FORM_TEXT)
		flags |= ONYX_CTXF_EDITABLE;
	if (browser_window_back_available(bw))
		flags |= ONYX_CTXF_BACK;
	if (browser_window_forward_available(bw))
		flags |= ONYX_CTXF_FORWARD;
	EDIT_LOG("ONYX-CONTEXT at %d,%d flags=0x%x\n", x, y, flags);

	cmd = onyx_chrome_context_menu(x - sx, y - sy, flags);
	if (cmd == ONYX_CMD_NONE || window_list == NULL)
		return;
	/* (what is there, again: the menu ran the window's events) */
	bw = window_list->bw;
	memset(&f, 0, sizeof f);
	if (browser_window_get_features(bw, x, y, &f) != NSERROR_OK)
		memset(&f, 0, sizeof f);
	page = browser_window_get_content(bw);
	ref = page != NULL ? hlcache_handle_get_url(page) : NULL;
	EDIT_LOG("ONYX-CONTEXT command %d\n", cmd);

	if ((cmd == ONYX_CMD_PASTE || cmd == ONYX_CMD_SELECT_ALL) &&
	    f.form_features == CTX_FORM_TEXT &&
	    !(browser_window_get_editor_flags(bw) & BW_EDITOR_CAN_COPY)) {
		/* the field under the pointer (no selection kept): a click puts the
		 * caret there first */
		browser_window_mouse_click(bw, BROWSER_MOUSE_PRESS_1, x, y);
		browser_window_mouse_click(bw, BROWSER_MOUSE_CLICK_1, x, y);
	}
	switch (cmd) {
	case ONYX_CMD_BACK:
		onyx_browser_back();
		break;
	case ONYX_CMD_FORWARD:
		onyx_browser_forward();
		break;
	case ONYX_CMD_RELOAD:
		onyx_browser_reload();
		break;
	case ONYX_CMD_CUT:
		browser_window_key_press(bw, NS_KEY_CUT_SELECTION);
		break;
	case ONYX_CMD_COPY:
		browser_window_key_press(bw, NS_KEY_COPY_SELECTION);
		break;
	case ONYX_CMD_PASTE:
		browser_window_key_press(bw, NS_KEY_PASTE);
		break;
	case ONYX_CMD_SELECT_ALL:
		browser_window_key_press(bw, NS_KEY_SELECT_ALL);
		break;
	case ONYX_CMD_FIND:
		onyx_chrome_find_open();
		break;
	case ONYX_CMD_OPEN_LINK:
		if (f.link != NULL)
			browser_window_navigate(bw, f.link, ref, BW_NAVIGATE_HISTORY,
					NULL, NULL, NULL);
		break;
	case ONYX_CMD_SAVE_LINK:
		if (f.link != NULL)
			browser_window_navigate(bw, f.link, ref, BW_NAVIGATE_DOWNLOAD,
					NULL, NULL, NULL);
		break;
	case ONYX_CMD_COPY_LINK:
		if (f.link != NULL)
			edit_copy_text(nsurl_access(f.link));
		break;
	case ONYX_CMD_OPEN_IMAGE:
	case ONYX_CMD_SAVE_IMAGE:
		if (f.object != NULL && hlcache_handle_get_url(f.object) != NULL)
			browser_window_navigate(bw, hlcache_handle_get_url(f.object), ref,
					cmd == ONYX_CMD_SAVE_IMAGE ?
					BW_NAVIGATE_DOWNLOAD : BW_NAVIGATE_HISTORY,
					NULL, NULL, NULL);
		break;
	case ONYX_CMD_COPY_IMAGE:
		edit_copy_image(f.object);
		break;
	case ONYX_CMD_COPY_IMAGE_URL:
		if (f.object != NULL && hlcache_handle_get_url(f.object) != NULL)
			edit_copy_text(nsurl_access(hlcache_handle_get_url(f.object)));
		break;
	default:
		break;
	}
}
