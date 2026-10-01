/*
 * Copyright 2012 Michael Drake <tlsa@netsurf-browser.org>
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
  * nsfb clipboard handling
  *
  * Onyx (Jet Browser, docs/06 §40): the system's clipboard (clipd's shared one through onyx_chrome.cpp, else the kernel's kapi_clipboard_set /
  * _get: one typed blob that every app shares, 64 KB at most) -- not a buffer of the browser's
  * own: text selected in a page or a form field and copied (Ctrl+C, Ctrl+X, the context menu)
  * pastes into the other apps, and their text into the page's fields (Ctrl+V). UTF-8 both
  * ways; a copy longer than the kernel keeps is cut at a character's boundary. On Windows
  * (pc/Jet/winkapi.cpp) the kapi is the Windows clipboard (CF_UNICODETEXT).
  */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "utils/log.h"
#include "netsurf/browser_window.h"
#include "netsurf/clipboard.h"

#include "framebuffer/gui.h"
#include "framebuffer/clipboard.h"
#include "netsurf/onyx_chrome.h"	/* Onyx: the shared clipboard (onyx_clip_*) */

#include "kapi.h"

#define CLIP_MAX (64 * 1024)	/* what the kernel keeps (kernel/sys/kapi.cpp CLIPBOARD_MAX) */


/**
 * Core asks front end for clipboard contents.
 *
 * \param  buffer  UTF-8 text, allocated by front end, ownership yeilded to core
 * \param  length  Byte length of UTF-8 text in buffer
 */
static void gui_get_clipboard(char **buffer, size_t *length)
{
	unsigned n = 0;

	/* (the shared clipboard -- clipd's current item, else the kernel's: onyx_chrome.cpp) */
	*buffer = onyx_clip_get_text(CLIP_MAX, &n);
	*length = *buffer != NULL ? n : 0;
}


/**
 * Core tells front end to put given text in clipboard
 *
 * \param  buffer    UTF-8 text, owned by core
 * \param  length    Byte length of UTF-8 text in buffer
 * \param  styles    Array of styles given to text runs, owned by core, or NULL
 * \param  n_styles  Number of text run styles in array
 */
static void gui_set_clipboard(const char *buffer, size_t length,
		nsclipboard_styles styles[], int n_styles)
{
	(void) styles;
	(void) n_styles;

	if (buffer == NULL || length == 0)
		return;		/* (nothing selected: the clipboard kept) */
	if (length > CLIP_MAX) {
		/* cut at a character's start (not inside a UTF-8 sequence) */
		length = CLIP_MAX;
		while (length > 0 && (((unsigned char) buffer[length]) & 0xC0) == 0x80)
			length--;
	}
	onyx_clip_set_text(buffer, (unsigned) length);
	printf("ONYX-CLIPBOARD text %u bytes\n", (unsigned) length);
	fflush(stdout);
}

static struct gui_clipboard_table clipboard_table = {
	.get = gui_get_clipboard,
	.set = gui_set_clipboard,
};

struct gui_clipboard_table *framebuffer_clipboard_table = &clipboard_table;
