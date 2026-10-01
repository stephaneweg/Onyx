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
  * Onyx (Jet Browser, docs/06 §40): the system's clipboard (the kernel's, kapi_clipboard_set /
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
	int type = 0, n;
	char *b;

	*buffer = NULL;
	*length = 0;

	n = kapi_clipboard_get(&type, NULL, 0, NULL);	/* (its length) */
	if (n <= 0 || type != CLIP_TEXT)
		return;
	if (n > CLIP_MAX)
		n = CLIP_MAX;
	b = malloc(n + 1);
	if (b == NULL)
		return;
	n = kapi_clipboard_get(&type, b, n, NULL);
	if (n <= 0 || type != CLIP_TEXT) {
		free(b);
		return;
	}
	if (n > CLIP_MAX)
		n = CLIP_MAX;
	b[n] = '\0';
	*buffer = b;
	*length = n;
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
	kapi_clipboard_set(CLIP_TEXT, buffer, (unsigned) length);
	printf("ONYX-CLIPBOARD text %u bytes\n", (unsigned) length);
	fflush(stdout);
}

static struct gui_clipboard_table clipboard_table = {
	.get = gui_get_clipboard,
	.set = gui_set_clipboard,
};

struct gui_clipboard_table *framebuffer_clipboard_table = &clipboard_table;
